/*=============================================================================
	WebRTCNetDriver.cpp: the browser's network driver (E-2, web build only).

	engine-ut2004's WebRTCNetDriver.cpp (ufront steps 2.36b, 2.38, 2.40), adapted to UC2:
	- every received packet updates the driver's receive clock (UNetDriver::UpdateLastReceiveTime, as
	  UTcpNetDriver does): UC2's "standby cheat" check fires NotifyStandbyHack after 1 s (server) or 3 s
	  (client) without one;
	- no emscripten_sleep while waiting for a pipe: the template slept 100 ms per tick (a listen server with
	  nobody connected ran at 10 frames a second) because its engine once shared the browser's main thread
	  with PeerJS; here the engine has run on a worker since M4w and the transport lives on the main thread;
	- the listen server's status (name, map, players) comes from the level the driver serves
	  (Notify->NotifyGetLevel()) rather than a launcher global;
	- the server name the peer id is built from is the CONFIG value [Engine.GameReplicationInfo] ServerName
	  (the launcher's seed), not GameReplicationInfo's class default: UC2's profile code overwrites that
	  default with the profile's own server name at start-up (Engine/Classes/PlayerProfile.uc:764), the
	  same "Liandri Corporation" on every install, and never writes the ini;
	- no in-game server-browser hook and no deferred-connect export (UC2 has no UT2004 MasterServerClient
	  browser; the frontend joins by putting "<room>_<serverSlug>?connect" on the command line).
	Notebook: docs/porting/analysis/webrtc.md.
=============================================================================*/

#include "WebRTCDrv.h"

// The peer-id namespace of this game: targets/uc2/target.json network.peerPrefix in the frontend. The glue
// registers a listen server as <prefix>-<room>_<serverSlug>-srv (packages/glue/src/webrtc/join-key.js).
#define WEBRTC_PEER_PREFIX		"uc2"

// Receive buffer: as UTcpNetDriver's NETWORK_MAX_PACKET.
#define WEBRTC_RECV_BUFFER		576

// The client has exactly ONE transport slot: the glue gives every client connect the same connection id (0).
// A server travel calls InitConnect for the pending level while the old level's driver still ticks, so the
// new driver takes the slot and detaches the old one (see InitConnect / LowLevelDestroy).
static UWebRTCNetDriver* GClientDriver = NULL;

/*-----------------------------------------------------------------------------
	Config defaults.

	UNetDriver's config properties are read from [WebRTCDrv.WebRTCNetDriver]. The web Default.ini carries that
	section, but an Avaris.ini derived by an older build (the frontend keeps it in IndexedDB) has none, and
	then every property UNetDriver::StaticConstructor does not default is ZERO: ConnectionTimeout 0 drops
	every connection at once, KeepAliveTime 0 sends no keep-alives, RelevantTimeout 0 closes actor channels
	(the guest's own PlayerController) a tick after opening them (engine-ut2004, ufront 2.36b). Substitute the
	web Default.ini values for anything not positive, and say what was substituted.
-----------------------------------------------------------------------------*/

void UWebRTCNetDriver::NormalizeConfigDefaults( const TCHAR* Context )
{
	guard(UWebRTCNetDriver::NormalizeConfigDefaults);

	FString Patched;
	#define WEBRTC_NETCFG( Field, Default ) \
		if( Field <= 0 ) \
		{ \
			Field = Default; \
			Patched += FString::Printf( TEXT(" %s=%g"), TEXT(#Field), (DOUBLE)(Default) ); \
		}
	WEBRTC_NETCFG( ConnectionTimeout,		60.f	)
	WEBRTC_NETCFG( InitialConnectTimeout,	300.f	)
	WEBRTC_NETCFG( KeepAliveTime,			0.2f	)
	WEBRTC_NETCFG( RelevantTimeout,			5.f		)
	WEBRTC_NETCFG( SpawnPrioritySeconds,	1.f		)
	WEBRTC_NETCFG( ServerTravelPause,		4.f		)
	WEBRTC_NETCFG( MaxClientRate,			20000	)
	WEBRTC_NETCFG( MaxInternetClientRate,	7000	)
	WEBRTC_NETCFG( NetServerMaxTickRate,	60		)
	WEBRTC_NETCFG( LanServerMaxTickRate,	60		)
	#undef WEBRTC_NETCFG

	debugf( NAME_DevNet, TEXT("[WebRTC] %s: net config ConnectionTimeout=%.1f InitialConnectTimeout=%.1f KeepAliveTime=%.2f RelevantTimeout=%.1f SpawnPrioritySeconds=%.1f ServerTravelPause=%.1f MaxClientRate=%i MaxInternetClientRate=%i NetServerMaxTickRate=%i%s%s%s"),
		Context, ConnectionTimeout, InitialConnectTimeout, KeepAliveTime, RelevantTimeout, SpawnPrioritySeconds,
		ServerTravelPause, MaxClientRate, MaxInternetClientRate, NetServerMaxTickRate,
		Patched.Len() ? TEXT(" [substituted:") : TEXT(" [all from ini]"), *Patched, Patched.Len() ? TEXT(" ]") : TEXT("") );

	unguard;
}

/*-----------------------------------------------------------------------------
	UWebRTCNetDriver.
-----------------------------------------------------------------------------*/

UBOOL UWebRTCNetDriver::InitConnect( FNetworkNotify* InNotify, FURL& ConnectURL, FString& Error )
{
	guard(UWebRTCNetDriver::InitConnect);
	if( !Super::InitConnect( InNotify, ConnectURL, Error ) )
		return 0;
	NormalizeConfigDefaults( TEXT("InitConnect") );

	// The URL's host is the join key, "<room>_<serverSlug>" (UGameEngine::Browse turns the frontend's
	// "<key>?connect" into a network URL with that host).
	RoomName = ConnectURL.Host;
	bIsListenServer = 0;
	if( !RoomName.Len() )
	{
		Error = TEXT("WebRTC: no room to connect to");
		return 0;
	}
	FString ServerPeerId = FString::Printf( TEXT("%s-%s-srv"), TEXT(WEBRTC_PEER_PREFIX), *RoomName );
	debugf( NAME_DevNet, TEXT("[WebRTC] InitConnect: room key '%s', server peer '%s'"), *RoomName, *ServerPeerId );

	// Take the one client slot. Detach (do not close) the previous level's connection: Closed would read as
	// "connection lost" and abort the travel; its LowLevelDestroy must then leave the transport alone.
	bDetached = 0;
	if( GClientDriver && GClientDriver!=this && GClientDriver->GetServerConnection() )
	{
		debugf( NAME_DevNet, TEXT("[WebRTC] InitConnect: detaching the previous client connection (one transport slot)") );
		GClientDriver->GetServerConnection()->WebRTCConnId = -1;
		GClientDriver->bDetached = 1;
	}
	GClientDriver = this;
	webrtc_connect( TCHAR_TO_ANSI(*ServerPeerId) );

	// The server connection, pending until HandleClientPlayer opens it (as UTcpNetDriver), and channel 0.
	ServerConnection = new UWebRTCConnection( 0, this, USOCK_Pending, 1, ConnectURL );
	GetServerConnection()->CreateChannel( CHTYPE_Control, 1, 0 );
	return 1;
	unguard;
}

UBOOL UWebRTCNetDriver::InitListen( FNetworkNotify* InNotify, FURL& LocalURL, FString& Error )
{
	guard(UWebRTCNetDriver::InitListen);
	if( !Super::InitListen( InNotify, LocalURL, Error ) )
		return 0;
	NormalizeConfigDefaults( TEXT("InitListen") );

	// The room: the launcher's listen URL carries room=<room>; a game hosted from the engine's own menus has
	// none, and the glue then uses the room the launcher persisted.
	RoomName = LocalURL.GetOption( TEXT("room="), TEXT("default") );
	// The server name is half of the peer id: ?ServerName= wins, else the config (see the file header).
	ServerName = LocalURL.GetOption( TEXT("ServerName="), TEXT("") );
	if( !ServerName.Len() )
		GConfig->GetString( TEXT("Engine.GameReplicationInfo"), TEXT("ServerName"), ServerName );
	bIsListenServer = 1;
	LastStatusPush = 0.0;

	// "<room>/<server name>": the glue slugs the name (a '/' because a name may contain anything).
	FString ListenKey = ServerName.Len() ? (RoomName + TEXT("/") + ServerName) : RoomName;
	debugf( NAME_DevNet, TEXT("[WebRTC] InitListen: room '%s' server name '%s'"), *RoomName, *ServerName );
	webrtc_init_listen( TCHAR_TO_ANSI(*ListenKey) );

	// Not an address anyone dials (the peer id is); the level's own URL keeps its host empty.
	return 1;
	unguard;
}

void UWebRTCNetDriver::TickDispatch( FLOAT DeltaTime )
{
	guard(UWebRTCNetDriver::TickDispatch);
	Super::TickDispatch( DeltaTime );

	// Accept new incoming connections (listen server).
	// ACCEPTC_Ignore is not a refusal: ULevel::NotifyAcceptingConnection returns it during a server travel.
	// Over UDP that drops one connect packet and the client retries; a pipe closed here would make the guest
	// reconnect, be ignored again and livelock (engine-ut2004 measured this). So park the pipe and offer it
	// again on a later tick, the parked ones before new arrivals.
	TArray<INT> Incoming = ParkedAccepts;
	ParkedAccepts.Empty();
	INT NewConnId;
	while( (NewConnId = webrtc_accept_connection()) > 0 )
		Incoming.AddItem( NewConnId );
	for( INT i=0; i<Incoming.Num(); i++ )
	{
		INT ConnId = Incoming(i);
		if( webrtc_get_connection_state( ConnId ) == WEBRTC_STATE_CLOSED )
			continue;
		EAcceptConnection AcceptResult = Notify->NotifyAcceptingConnection();
		if( AcceptResult == ACCEPTC_Accept )
		{
			UWebRTCConnection* NewConn = new UWebRTCConnection( ConnId, this, USOCK_Open, 0, FURL() );
			NewConn->URL.Host = FString::Printf( TEXT("webrtc:%i"), ConnId );
			Notify->NotifyAcceptedConnection( NewConn );
			ClientConnections.AddItem( NewConn );
			debugf( NAME_DevNet, TEXT("[WebRTC] TickDispatch: ACCEPTED connection %i (total clients: %i)"), ConnId, ClientConnections.Num() );
		}
		else if( AcceptResult == ACCEPTC_Ignore )
		{
			ParkedAccepts.AddItem( ConnId );
		}
		else
		{
			debugf( NAME_DevNet, TEXT("[WebRTC] TickDispatch: REFUSED connection %i (policy %i)"), ConnId, (INT)AcceptResult );
			webrtc_close( ConnId );
		}
	}

	// Client connections (listen server): packets in, and a pipe the remote closed = a dropped player at once
	// (UTcpNetDriver gets the same from ICMP port-unreachable; otherwise only ConnectionTimeout would end it).
	BYTE Data[WEBRTC_RECV_BUFFER];
	for( INT i=0; i<ClientConnections.Num(); i++ )
	{
		UWebRTCConnection* Conn = (UWebRTCConnection*)ClientConnections(i);
		if( Conn->State!=USOCK_Open && Conn->State!=USOCK_Pending )
			continue;
		INT Size;
		while( (Size = webrtc_recv( Conn->WebRTCConnId, Data, sizeof(Data) )) > 0 )
		{
			UpdateLastReceiveTime();
			Conn->ReceivedRawPacket( Data, Size );
		}
		if( webrtc_get_connection_state( Conn->WebRTCConnId ) == WEBRTC_STATE_CLOSED && Conn->State != USOCK_Closed )
		{
			debugf( NAME_DevNet, TEXT("[WebRTC] TickDispatch: client connId=%i connection CLOSED by remote"), Conn->WebRTCConnId );
			Conn->State = USOCK_Closed;
		}
	}

	// The server connection (client). A detached one (connId -1) no longer owns the slot: asking the glue
	// about an id we gave away would report CLOSED and call the departing level "connection lost".
	UWebRTCConnection* Server = GetServerConnection();
	if( Server && Server->WebRTCConnId >= 0 )
	{
		// The engine's State is left to the join flow (Pending until HandleClientPlayer); only a dead pipe
		// is turned into a closed connection, which the engine reports as a lost connection.
		if( webrtc_get_connection_state( Server->WebRTCConnId ) == WEBRTC_STATE_CLOSED && Server->State != USOCK_Closed )
		{
			debugf( NAME_DevNet, TEXT("[WebRTC] TickDispatch: server connection CLOSED by remote (connId=%i)"), Server->WebRTCConnId );
			Server->State = USOCK_Closed;
		}
		if( Server->State==USOCK_Open || Server->State==USOCK_Pending )
		{
			INT Size;
			while( (Size = webrtc_recv( Server->WebRTCConnId, Data, sizeof(Data) )) > 0 )
			{
				UpdateLastReceiveTime();
				Server->ReceivedRawPacket( Data, Size );
			}
		}
	}

	if( bIsListenServer )
		PushListenStatus();

	unguard;
}

//
// Listen server: publish name, map, players and game type about once a second; the glue answers the status
// pings of browsing peers with it (its server rows).
//
void UWebRTCNetDriver::PushListenStatus()
{
	guard(UWebRTCNetDriver::PushListenStatus);
	DOUBLE Now = appSeconds();
	if( Now - LastStatusPush < 1.0 )
		return;
	LastStatusPush = Now;

	FString Name = ServerName, MapName = TEXT("?"), GameType;
	INT Players = 0, MaxPlayers = 0;
	ULevel* Level = Notify ? Notify->NotifyGetLevel() : NULL;
	if( Level )
	{
		if( Level->URL.Map.Len() )
			MapName = Level->URL.Map;
		ALevelInfo* Info = Level->GetLevelInfo();
		AGameInfo* Game = Info ? Info->Game : NULL;
		if( Game )
		{
			Players = Game->NumPlayers;
			MaxPlayers = Game->MaxPlayers;
			GameType = Game->GameName;
			if( !Name.Len() && Game->GameReplicationInfo )
				Name = Game->GameReplicationInfo->ServerName;
		}
	}
	if( !Name.Len() )
		Name = TEXT("UC2 Server");
	webrtc_set_listen_status( TCHAR_TO_ANSI(*Name), TCHAR_TO_ANSI(*MapName), Players, MaxPlayers, TCHAR_TO_ANSI(*GameType) );
	unguard;
}

FString UWebRTCNetDriver::LowLevelGetNetworkNumber()
{
	guard(UWebRTCNetDriver::LowLevelGetNetworkNumber);
	return RoomName.Len() ? RoomName : FString(TEXT("webrtc"));
	unguard;
}

void UWebRTCNetDriver::LowLevelDestroy()
{
	guard(UWebRTCNetDriver::LowLevelDestroy);
	debugf( NAME_DevNet, TEXT("[WebRTC] LowLevelDestroy: room '%s', %i client connection(s)%s"), *RoomName, ClientConnections.Num(),
		bDetached ? TEXT(" (detached: transport left to the newer driver)") : TEXT("") );
	ParkedAccepts.Empty();
	if( GClientDriver==this )
		GClientDriver = NULL;
	// webrtc_destroy() is global. On a client the old level's driver dies AFTER the pending level's
	// InitConnect (inside LoadMap), so a detached driver must not tear down the newer one's transport. A
	// listen server's old driver dies before the next InitListen.
	if( !bDetached )
		webrtc_destroy();
	unguard;
}

UWebRTCConnection* UWebRTCNetDriver::GetServerConnection()
{
	return (UWebRTCConnection*)ServerConnection;
}

IMPLEMENT_CLASS(UWebRTCNetDriver);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
