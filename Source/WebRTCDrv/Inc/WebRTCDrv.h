/*=============================================================================
	WebRTCDrv.h: the browser's network driver (web build only).

	The WebRTC net driver on UC2's UNetDriver (Engine/Inc/UnNetDrv.h: FLOAT time, the "standby cheat"
	receive clock, PreServerTravel). Packets travel over the frontend's WebRTC data channel, which the
	glue opens reliable and ordered: a packet is lost only while a pipe is down and the glue reconnects,
	and the engine's own acks and resends (written for lossy datagrams) run on top of it. The transport lives in
	the frontend (@ufront/glue, packages/glue/src/webrtc); the engine reaches it through the ue_net_*
	imports of vendor/glue/ue_abi.h, which vendor/glue/net-shim.js forwards to Module.UE.net.
=============================================================================*/

#ifndef _INC_WEBRTCDRV
#define _INC_WEBRTCDRV

#include "Engine.h"
#include "UnNet.h"

#ifndef __EMSCRIPTEN__
#error WebRTCDrv is built for the web only (Source/WebRTCDrv/CMakeLists.txt); native builds use IpDrv.TcpNetDriver.
#endif

/*-----------------------------------------------------------------------------
	The frozen C ABI (frontend docs/02-abi-contract.md), ABI version 2: the ue_net_* imports and the
	UE_NET_STATE_* connection states. ue_net_init_listen takes "<room>/<server name>", ue_net_connect
	"<peerPrefix>-<room>_<serverSlug>-srv".
-----------------------------------------------------------------------------*/

#ifdef __EMSCRIPTEN__
#include "ue_abi.h"
#endif

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
	FString		LastBeacon;			// listen server: the beacon text last pushed (logged when it changes)

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
	// No standby check over WebRTC, neither half (the .cpp says why).
	UBOOL StandbyCheckEnabled() const;

	// UWebRTCNetDriver interface.
	UWebRTCConnection* GetServerConnection();
	void NormalizeConfigDefaults( const TCHAR* Context );
	void PushListenStatus();
	void LogStats();
};

/*-----------------------------------------------------------------------------
	The System Link page's synthetic LAN (netplay, Src/WebRTCLan.cpp).
-----------------------------------------------------------------------------*/

// Fill in IpDrv's AUdpLink hooks (IpDrv/Inc/UdpLinkWeb.h); done with the class registration below.
void WebRTCLan_Install();
// The room key a synthetic System Link address (10.199.0.N) stands for, or "" if it is not one.
FString WebRTCLan_RoomForHost( const TCHAR* Host );

#define AUTO_INITIALIZE_REGISTRANTS_WEBRTCDRV \
	UWebRTCNetDriver::StaticClass(); \
	UWebRTCConnection::StaticClass(); \
	WebRTCLan_Install();

#endif // _INC_WEBRTCDRV

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
