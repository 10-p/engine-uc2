/*=============================================================================
	WebRTCDrv.h: the browser's network driver (E-2, web build only).

	Ported from engine-ut2004's Source/WebRTCDrv (ufront's WebRTC net driver) and adapted to UC2's
	UNetDriver (Engine/Inc/UnNetDrv.h: FLOAT time, the "standby cheat" receive clock, PreServerTravel).
	Packets travel over the frontend's WebRTC data channel, which the glue opens reliable and ordered
	(measured: "rtc=ordered=true/maxRetx=null", docs/porting/analysis/webrtc.md): a packet is lost only
	while a pipe is down and the glue reconnects, and the engine's own acks and resends (written for lossy
	datagrams, network.md) run on top of it. The transport lives in
	the frontend (@ufront/glue, packages/glue/src/webrtc); the engine reaches it through the webrtc_*
	imports that vendor/glue/webrtc-shim.js forwards to Module.UE.net.

	Notebook: docs/porting/analysis/webrtc.md.
=============================================================================*/

#ifndef _INC_WEBRTCDRV
#define _INC_WEBRTCDRV

#include "Engine.h"
#include "UnNet.h"

#ifndef __EMSCRIPTEN__
#error WebRTCDrv is built for the web only (Source/WebRTCDrv/CMakeLists.txt); native builds use IpDrv.TcpNetDriver.
#endif

/*-----------------------------------------------------------------------------
	The frozen C ABI (frontend docs/02-abi-contract.md; vendor/glue/webrtc-shim.js, ABI version 1).
	Only the calls this engine makes are declared; the shim defines all of them.
-----------------------------------------------------------------------------*/

extern "C"
{
	void webrtc_init_listen( const char* RoomAndServerName );	// "<room>/<server name>"
	void webrtc_connect( const char* PeerId );				// "<peerPrefix>-<room>_<serverSlug>-srv"
	int  webrtc_send( int ConnId, const void* Data, int Len );
	int  webrtc_recv( int ConnId, void* Buffer, int MaxLen );
	int  webrtc_accept_connection();
	void webrtc_close( int ConnId );
	void webrtc_destroy();
	int  webrtc_get_connection_state( int ConnId );
	void webrtc_set_listen_status( const char* Name, const char* Map, int Players, int MaxPlayers, const char* GameType );
}

// webrtc_get_connection_state() values (the glue's session states).
#define WEBRTC_STATE_INVALID	0
#define WEBRTC_STATE_CONNECTING	1
#define WEBRTC_STATE_OPEN		2
#define WEBRTC_STATE_CLOSED		3

/*-----------------------------------------------------------------------------
	UWebRTCConnection: one data channel (a glue connection id) as a UNetConnection.
-----------------------------------------------------------------------------*/

class UWebRTCConnection : public UNetConnection
{
	DECLARE_CLASS(UWebRTCConnection,UNetConnection,CLASS_Config|CLASS_Transient,WebRTCDrv)
	NO_DEFAULT_CONSTRUCTOR(UWebRTCConnection)

	// Variables.
	INT		WebRTCConnId;	// glue connection id; -1 = detached (a newer client driver owns the wire)
	UBOOL	OpenedLocally;

	// Constructors.
	UWebRTCConnection( INT InWebRTCConnId, UNetDriver* InDriver, EConnectionState InState, UBOOL InOpenedLocally, const FURL& InURL );

	// UNetConnection interface.
	void LowLevelSend( void* Data, INT Count );
	FString LowLevelGetRemoteAddress();
	FString LowLevelDescribe();
};

/*-----------------------------------------------------------------------------
	UWebRTCNetDriver.
-----------------------------------------------------------------------------*/

class UWebRTCNetDriver : public UNetDriver
{
	DECLARE_CLASS(UWebRTCNetDriver,UNetDriver,CLASS_Transient|CLASS_Config,WebRTCDrv)

	// Variables.
	FString		RoomName;			// client: the join key it dialled; listen server: the room= option
	FString		ServerName;			// listen server: the name the peer id is built from
	UBOOL		bIsListenServer;
	UBOOL		bDetached;			// a newer client driver took the one client transport slot
	TArray<INT>	ParkedAccepts;		// incoming pipes the level told us to IGNORE (not refuse)
	DOUBLE		LastStatusPush;
	DOUBLE		LastStatsLog;		// the per-connection traffic line, every 10 s (LogStats)

	// Constructor.
	UWebRTCNetDriver()
	{}

	// UNetDriver interface.
	UBOOL InitConnect( FNetworkNotify* InNotify, FURL& ConnectURL, FString& Error );
	UBOOL InitListen( FNetworkNotify* InNotify, FURL& LocalURL, FString& Error );
	void TickDispatch( FLOAT DeltaTime );
	FString LowLevelGetNetworkNumber();
	void LowLevelDestroy();
	// As UTcpNetDriver (IpDrv/Inc/UnTcpNetDriver.h): connections still loading are not force-failed at a
	// server travel, which is what the native travel tests verified.
	void PreServerTravel() {}
	// G-10 (owner: "yes exclude webrtc"): no web guest counts for UC2's standby check (see the .cpp).
	UBOOL ShouldIncludeInStandbyCheck( const UNetConnection* Connection ) const;

	// UWebRTCNetDriver interface.
	UWebRTCConnection* GetServerConnection();
	void NormalizeConfigDefaults( const TCHAR* Context );
	void PushListenStatus();
	void LogStats();
};

#define AUTO_INITIALIZE_REGISTRANTS_WEBRTCDRV \
	UWebRTCNetDriver::StaticClass(); \
	UWebRTCConnection::StaticClass();

#endif // _INC_WEBRTCDRV

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
