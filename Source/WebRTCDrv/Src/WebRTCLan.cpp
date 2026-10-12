/*=============================================================================
	WebRTCLan.cpp: UC2's System Link page on the web (netplay, web build only).

	THE PROBLEM. UC2's "Join System Link Game" page (UCMenu.UC2Browser) lists what IpDrv.ClientBeaconReceiver
	hears: the receiver binds BeaconPort (9777), broadcasts "REPORT?<nonce>" to ServerBeaconPort (8777) once a
	second, and keeps every answer of the form
	    <BeaconProduct> <game port> ?BeaconText=<GameInfo.GetBeaconText()>?Nonce=<nonce>
	for BeaconTimeout (5 s), the answer's SOURCE ADDRESS (with the port from the text) being the row's join
	address. The answer comes from IpDrv.UdpBeacon on a listen server. A browser has no UDP, so on the web
	neither half ever ran (AUdpLink::BindPort fails at socket(); E-2, webrtc.md) and the page stayed empty.

	THE ANSWER, the UE1 siblings' (engine-ut99 WebRTCLan.cpp, ufront 2.38): a synthetic LAN at the AUdpLink
	level, so Epic's script runs unmodified. The frontend's broker already knows the servers: the glue's
	discovery lists the broker's peers (GET /peerjs/peers, the page's own room) and status-pings each for what
	its host pushed (webrtc_set_listen_status, which UC2's driver fills with the host's real beacon text). Here a
	receiver's REPORT is answered once per discovered server, from a synthetic address 10.199.0.N whose N
	indexes an append-only room table, so that the page's join (a travel to "10.199.0.N:7777?...") can be turned
	back into the room key by UWebRTCNetDriver::InitConnect.

	Two rules this file inherits from WebRTCLan's measurements (both cost the siblings several runs):
	- every webrtc_* call is a synchronous proxy to the browser's main thread: make them only from a link's
	  TICK (NextReply), never from inside script (Send is reached from the receiver's script Timer); a proxied
	  call there can hang the engine;
	- never ask the glue every frame: the proxy queue overflows under load and the module aborts. Discovery is
	  started at most every LAN_DISCOVERY_MIN_INTERVAL and polled at most four times a second.
	And one UE fact: TArray<T>::AddItem assigns into raw memory; an element that owns FStrings is appended with
	placement new (`new(Array)T(Item)`), never AddItem.

	Notebook: docs/porting/analysis/netplay-browser.md.
=============================================================================*/

#include "WebRTCDrv.h"
#include "UdpLinkWeb.h"

// The v1 ABI's discovery calls (vendor/glue/webrtc-shim.js; the frontend's packages/glue/src/webrtc/index.js).
extern "C"
{
	void webrtc_discover_servers( const char* Room );
	int  webrtc_get_server_count();
	int  webrtc_is_discovery_done();
	int  webrtc_get_server_info( int Index, char* Name, int NameMax, char* JoinKey, int JoinKeyMax );
	int  webrtc_get_server_map( int Index, char* Buf, int MaxLen );
	int  webrtc_get_server_gametype( int Index, char* Buf, int MaxLen );
	int  webrtc_get_server_players( int Index );
	int  webrtc_get_server_maxplayers( int Index );
	int  webrtc_get_server_ping( int Index );
}

// 10.199.0.0/16: private, far from anything a real LAN hands out (the siblings use the same block).
#define LAN_ADDR_BASE				0x0AC70000u
#define LAN_ADDR_MASK				0xFFFF0000u
// The game port in the answer (UdpBeacon sends the port of Level.GetAddressURL()); cosmetic: the room routes.
#define LAN_GAME_PORT				7777
// A discovery is a broker request plus a WebRTC status ping per server: not more often than this. The page
// asks every second and gets the cached list; its rows live BeaconTimeout (5 s) after the last answer.
#define LAN_DISCOVERY_MIN_INTERVAL	4.0
// How often a pending discovery is polled (a proxied call).
#define LAN_PUMP_INTERVAL			0.25

struct FLanServer
{
	FString Room, Name, Map, Beacon;
	INT		Players, MaxPlayers, Ping;
	FLanServer() : Players(0), MaxPlayers(0), Ping(0) {}
};

struct FLanLink
{
	const void*	Id;
	INT			BoundPort;
	UBOOL		bReportPending;		// a REPORT to answer at the link's next tick
	FString		Nonce;				// the receiver's nonce, echoed in every answer
	INT			LastAnswered;		// how many servers the last answer carried (-1: none yet), for the log
	FLanLink() : Id(NULL), BoundPort(0), bReportPending(0), LastAnswered(-1) {}
};

struct FLanReply
{
	const void*	LinkId;
	DWORD		Addr;
	INT			Port;
	FString		Text;
	FLanReply() : LinkId(NULL), Addr(0), Port(0) {}
	FLanReply( const void* InLinkId, DWORD InAddr, INT InPort, const FString& InText )
	: LinkId(InLinkId), Addr(InAddr), Port(InPort), Text(InText) {}
};

static TArray<FString>		GRooms;			// append-only: an address handed out must keep meaning its room
static TArray<FLanServer>	GServers;		// the last finished discovery
static TArray<FLanLink>		GLinks;
static TArray<FLanReply>	GReplies;
static UBOOL				GWantDiscovery = 0;	// asked for from script (Send); started from a tick
static UBOOL				GDiscoveryPending = 0;
static DOUBLE				GLastDiscoveryAt = -1000.0;
static DOUBLE				GLastPumpAt = 0.0;

/*-----------------------------------------------------------------------------
	Helpers.
-----------------------------------------------------------------------------*/

static FLanLink* FindLink( const void* Id )
{
	for( INT i=0; i<GLinks.Num(); i++ )
		if( GLinks(i).Id == Id )
			return &GLinks(i);
	return NULL;
}

static FLanLink& TouchLink( const void* Id )
{
	FLanLink* L = FindLink( Id );
	if( L )
		return *L;
	FLanLink N;
	N.Id = Id;
	new( GLinks )FLanLink( N );
	return GLinks( GLinks.Num()-1 );
}

static INT RoomIndex( const FString& Room )
{
	for( INT i=0; i<GRooms.Num(); i++ )
		if( appStricmp( *GRooms(i), *Room )==0 )
			return i;
	new( GRooms )FString( Room );
	return GRooms.Num() - 1;
}

static DWORD AddrForRoom( const FString& Room )
{
	return LAN_ADDR_BASE | (DWORD)(RoomIndex( Room ) & 0xFFFF);
}

static FString AddrString( DWORD A )
{
	return FString::Printf( TEXT("%i.%i.%i.%i"), (A>>24)&0xFF, (A>>16)&0xFF, (A>>8)&0xFF, A&0xFF );
}

// The product the receiver compares the answer's first word with (case-insensitively): the ini's, else
// UdpBeacon's default.
static FString BeaconProduct()
{
	FString P;
	if( GConfig->GetString( TEXT("IpDrv.UdpBeacon"), TEXT("BeaconProduct"), P ) && P.Len() )
		return P;
	if( GConfig->GetString( TEXT("IpDrv.ClientBeaconReceiver"), TEXT("BeaconProduct"), P ) && P.Len() )
		return P;
	return FString(TEXT("Unreal(Avaris)"));
}

/*-----------------------------------------------------------------------------
	Discovery (proxied calls: tick context only).
-----------------------------------------------------------------------------*/

static void KickDiscovery()
{
	DOUBLE Now = appSeconds();
	if( GDiscoveryPending || Now - GLastDiscoveryAt < LAN_DISCOVERY_MIN_INTERVAL )
		return;
	GLastDiscoveryAt = Now;
	GDiscoveryPending = 1;
	webrtc_discover_servers( "" );	// "" = the room this page is on (the glue resolves it)
	debugf( NAME_DevNet, TEXT("[WebRTC-LAN] discovery started") );
}

static void PumpDiscovery()
{
	if( !GDiscoveryPending )
		return;
	DOUBLE Now = appSeconds();
	if( Now - GLastPumpAt < LAN_PUMP_INTERVAL )
		return;
	GLastPumpAt = Now;
	if( !webrtc_is_discovery_done() )
		return;
	GDiscoveryPending = 0;

	// Logged when the list changes, not every few seconds.
	FString Before;
	for( INT i=0; i<GServers.Num(); i++ )
		Before += GServers(i).Room + TEXT("|") + GServers(i).Beacon + TEXT("\n");

	GServers.Empty();
	INT Count = webrtc_get_server_count();
	char NameBuf[256], RoomBuf[256], MapBuf[128], BeaconBuf[1024];
	for( INT i=0; i<Count; i++ )
	{
		appMemzero( NameBuf, sizeof(NameBuf) );
		appMemzero( RoomBuf, sizeof(RoomBuf) );
		appMemzero( MapBuf, sizeof(MapBuf) );
		appMemzero( BeaconBuf, sizeof(BeaconBuf) );
		if( !webrtc_get_server_info( i, NameBuf, sizeof(NameBuf), RoomBuf, sizeof(RoomBuf) ) || !RoomBuf[0] )
			continue;
		webrtc_get_server_map( i, MapBuf, sizeof(MapBuf) );
		webrtc_get_server_gametype( i, BeaconBuf, sizeof(BeaconBuf) );
		FLanServer S;
		S.Room			= ANSI_TO_TCHAR(RoomBuf);
		S.Name			= ANSI_TO_TCHAR(NameBuf);
		S.Map			= MapBuf[0] ? FString(ANSI_TO_TCHAR(MapBuf)) : FString(TEXT("?"));
		S.Players		= webrtc_get_server_players( i );
		S.MaxPlayers	= webrtc_get_server_maxplayers( i );
		S.Ping			= webrtc_get_server_ping( i );
		// A UC2 host's driver sends its beacon text (GameInfo.GetBeaconText(): "type|name|map|maplist|players|
		// max|purity|mutators|player names|handicap") in the row's game-type field. A host that sent a bare game
		// type (an older build, or a status ping that did not answer) gets a row built from the plain fields.
		FString Beacon = ANSI_TO_TCHAR(BeaconBuf);
		if( Beacon.InStr( TEXT("|") ) < 0 )
			Beacon = FString::Printf( TEXT("%s|%s|%s||%i|%i|0|||False"), *Beacon, *S.Name, *S.Map, S.Players, S.MaxPlayers > 0 ? S.MaxPlayers : 8 );
		S.Beacon = Beacon;
		new( GServers )FLanServer( S );
	}

	FString After;
	for( INT i=0; i<GServers.Num(); i++ )
		After += GServers(i).Room + TEXT("|") + GServers(i).Beacon + TEXT("\n");
	if( After != Before )
	{
		for( INT i=0; i<GServers.Num(); i++ )
		{
			const FLanServer& S = GServers(i);
			debugf( NAME_DevNet, TEXT("[WebRTC-LAN] server '%s' room=%s map=%s %i/%i %ims -> %s beacon '%s'"),
				*S.Name, *S.Room, *S.Map, S.Players, S.MaxPlayers, S.Ping, *AddrString( AddrForRoom( S.Room ) ), *S.Beacon );
		}
		debugf( NAME_DevNet, TEXT("[WebRTC-LAN] discovery done: %i server(s)"), GServers.Num() );
	}
}

/*-----------------------------------------------------------------------------
	The hooks (UdpLinkWeb.h).
-----------------------------------------------------------------------------*/

static void LanBind( const void* LinkId, INT Port )
{
	FLanLink& L = TouchLink( LinkId );
	L.BoundPort = Port;
	debugf( NAME_DevNet, TEXT("[WebRTC-LAN] link bound on port %i (no UDP in a browser: the synthetic LAN answers it)"), Port );
}

static void LanUnbind( const void* LinkId )
{
	for( INT i=GLinks.Num()-1; i>=0; i-- )
		if( GLinks(i).Id == LinkId )
			GLinks.Remove( i );
	for( INT i=GReplies.Num()-1; i>=0; i-- )
		if( GReplies(i).LinkId == LinkId )
			GReplies.Remove( i );
}

// Reached from script (ClientBeaconReceiver.BroadcastQuery, from its Timer): record only, no proxied call.
static void LanSend( const void* LinkId, DWORD DestAddr, INT DestPort, const TCHAR* Text )
{
	if( !Text )
		return;
	// "REPORT?<nonce>" is the System Link sweep; "REPORTQUERY" (UdpBeacon's query-port question) has no
	// UC2 browser behind it and, like anything else, is swallowed: there is no socket to carry it.
	if( appStrnicmp( Text, TEXT("REPORT"), 6 )==0 && appStrnicmp( Text, TEXT("REPORTQUERY"), 11 )!=0 )
	{
		FLanLink& L = TouchLink( LinkId );
		const TCHAR* Q = appStrstr( Text, TEXT("?") );
		L.Nonce = Q ? FString( Q+1 ) : FString();
		L.bReportPending = 1;
		GWantDiscovery = 1;
	}
}

// Tick context: start or poll discovery, answer a pending REPORT, hand out one queued datagram.
static UBOOL LanNextReply( const void* LinkId, DWORD* OutAddr, INT* OutPort, FString& OutText )
{
	FLanLink* L = FindLink( LinkId );
	if( L && L->bReportPending )
	{
		if( GWantDiscovery )
		{
			GWantDiscovery = 0;
			KickDiscovery();
		}
		PumpDiscovery();
		L->bReportPending = 0;
		const FString Product = BeaconProduct();
		for( INT i=0; i<GServers.Num(); i++ )
		{
			const FLanServer& S = GServers(i);
			FString Answer = FString::Printf( TEXT("%s %i ?BeaconText=%s"), *Product, LAN_GAME_PORT, *S.Beacon );
			if( L->Nonce.Len() )
				Answer += FString::Printf( TEXT("?Nonce=%s"), *L->Nonce );
			new( GReplies )FLanReply( LinkId, AddrForRoom( S.Room ), LAN_GAME_PORT, Answer );
		}
		if( L->LastAnswered != GServers.Num() )
		{
			L->LastAnswered = GServers.Num();
			debugf( NAME_DevNet, TEXT("[WebRTC-LAN] REPORT from the System Link page answered with %i beacon(s)%s%s"),
				GServers.Num(), GServers.Num() ? TEXT(": ") : TEXT(""),
				GServers.Num() ? *FString::Printf( TEXT("'%s' from %s:%i"), *GServers(0).Beacon, *AddrString( AddrForRoom( GServers(0).Room ) ), LAN_GAME_PORT ) : TEXT("") );
		}
	}
	else if( GDiscoveryPending )
		PumpDiscovery();

	for( INT i=0; i<GReplies.Num(); i++ )
	{
		if( GReplies(i).LinkId != LinkId )
			continue;
		*OutAddr = GReplies(i).Addr;
		*OutPort = GReplies(i).Port;
		OutText = GReplies(i).Text;
		GReplies.Remove( i );
		return 1;
	}
	return 0;
}

static FUdpLinkWebHooks GLanHooks = { LanBind, LanUnbind, LanSend, LanNextReply };

void WebRTCLan_Install()
{
	GUdpLinkWebHooks = &GLanHooks;
}

//
// The room key a synthetic address stands for, or "" when the host is not one of ours (a real address, or
// the frontend's "<key>?connect" join, whose host is the key itself).
//
FString WebRTCLan_RoomForHost( const TCHAR* Host )
{
	if( !Host )
		return FString();
	INT Oct[4] = {0,0,0,0}, N = 0;
	const TCHAR* P = Host;
	while( N < 4 )
	{
		if( *P < '0' || *P > '9' )
			return FString();
		INT V = 0;
		while( *P >= '0' && *P <= '9' )
			V = V*10 + (*P++ - '0');
		if( V > 255 )
			return FString();
		Oct[N++] = V;
		if( N < 4 )
		{
			if( *P != '.' )
				return FString();
			P++;
		}
	}
	if( *P != 0 )
		return FString();
	DWORD Addr = ((DWORD)Oct[0]<<24) | ((DWORD)Oct[1]<<16) | ((DWORD)Oct[2]<<8) | (DWORD)Oct[3];
	if( (Addr & LAN_ADDR_MASK) != LAN_ADDR_BASE )
		return FString();
	INT Index = (INT)(Addr & 0xFFFF);
	if( Index >= GRooms.Num() )
		return FString();
	return GRooms( Index );
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
