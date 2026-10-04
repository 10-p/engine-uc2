#pragma once

/*=============================================================================
	UnTcpNetDriverXbox.h: Unreal TCP/IP driver.
	Copyright 1997-2000 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Brandon Reinhart.
	* Modified by Brian C. Ladd to include XboxLive stuff from Championship.
=============================================================================*/
#include "UnIpDrv.h"

// sjs ---
IPDRV_API void BytesToString(const BYTE* value, int size, FString& outString);
IPDRV_API void StringToBytes(BYTE* value, int size, const FString& inString);
// --- sjs

/*-----------------------------------------------------------------------------
	UTcpipConnectionXbox.
-----------------------------------------------------------------------------*/

#ifdef _XBOX
#include "UnAudio.h"
#endif

//
// Windows socket class.
//
class IPDRV_API UTcpipConnectionXbox : public UNetConnection
{
	DECLARE_CLASS(UTcpipConnectionXbox,UNetConnection,CLASS_Config|CLASS_Transient,IpDrv)
	NO_DEFAULT_CONSTRUCTOR(UTcpipConnectionXbox)

	// Variables.
	sockaddr_in		RemoteAddr;
	SOCKET			Socket;
	UBOOL			OpenedLocally;
	FResolveInfo*	ResolveInfo;
	FLOAT			OpenedTime;

	UBOOL			bSocketError;			// Whether this socket is giving errors - used for server travelling
	UBOOL			bUseVDP;

#ifdef _XBOX

	// the address of this connection according to Xbox Live
	XNADDR			LiveAddress;

	// queue of voice packets to send
	TArray<FVoicePacket>	VoicePackets;
	TArray<ULONGLONG>		MuteList;			// mute list for this connection, valid only for servers.

	UBOOL HasPendingData();
#endif

	// Constructors and destructors.
	UTcpipConnectionXbox( SOCKET InSocket, UNetDriver* InDriver, sockaddr_in InRemoteAddr, EConnectionState InState, UBOOL InOpenedLocally, const FURL& InURL, UBOOL InbUseVDP = 0 );

	virtual void Destroy();
	void LowLevelSend( void* Data, INT Count );
	void ReceivedRawPacket(void *Data,INT Count);
	FString LowLevelGetRemoteAddress();
	FString LowLevelDescribe();

	virtual INT CalcPacketOverhead(INT PacketSize);
};

/*-----------------------------------------------------------------------------
	UTcpNetDriverXbox.
-----------------------------------------------------------------------------*/

//
// BSD sockets network driver.
//
class IPDRV_API UTcpNetDriverXbox : public UNetDriver
{
	DECLARE_CLASS(UTcpNetDriverXbox,UNetDriver,CLASS_Transient|CLASS_Config,IpDrv)

	UBOOL AllowPlayerPortUnreach;
	UBOOL LogPortUnreach;
	INT MaxConnPerIPPerMinute;
	UBOOL LogMaxConnPerIPPerMin;
	//epic rwd - use VDP socket?
	UBOOL bUseVDP;
	// set to true if the incoming URL included "XboxLive"; used when advancing to the next 
	// map and a TRY_LAST_MATCH happens.
	UBOOL			bUsingXboxLive;

	// retry info used when advancing to the next map in "system link" (LAN) games
	FLOAT NextRetryTime;
	FLOAT RetryStartTime;

	// Variables.
	sockaddr_in	LocalAddr;
	SOCKET		Socket;

	// Constructor.
	UTcpNetDriverXbox()
	{}
	void StaticConstructor();

	// UNetDriver interface.
	UBOOL InitConnect( FNetworkNotify* InNotify, FURL& ConnectURL, FString& Error );
	UBOOL InitListen( FNetworkNotify* InNotify, FURL& LocalURL, FString& Error );
	void TickDispatch( FLOAT DeltaTime );
	FString LowLevelGetNetworkNumber();
	void LowLevelDestroy();	
	UBOOL ShouldIncludeInStandbyCheck( const UNetConnection* Connection ) const;

	virtual void PreServerTravel();
	virtual INT Exec( const TCHAR* Cmd, FOutputDevice& Ar=*GLog );

	// UTcpNetDriverXbox interface.
	UBOOL InitBase( UBOOL Connect, FNetworkNotify* InNotify, FURL& URL, FString& Error );
	UTcpipConnectionXbox* GetServerConnection();
	FSocketData GetSocketData();

//scion bcl -- Scavanged from Championship's version of this file
#ifdef _XBOX
	// gam ---

	struct FSessionInfo
	{
		XNKID   SessionID;
		XNKEY   ExchangeKey;
		XNADDR  HostAddr;
		UINT    RefCount;
	};

	void RetryConnection(const FString & XSessionID, const FString & XExchangeKey, const FString & XHostAddr);

	void SetHostKey( XNKID* SessionID, XNKEY* ExchangeKey );
	void UnregisterHostKey();

	FSessionInfo* GetHostInfo();
	const FSessionInfo* GetHostInfo() const;

	void RegisterRemoteKey( const FString &XSessionID, const FString &XExchangeKey, const FString &XHostAddr );
	void UnregisterRemoteKey();

	FSessionInfo* GetRemoteInfo();
	XNKID getPreviousSessionID() { return PreviousSessionID; }
	void SaveHostInfo();
	UBOOL LoadHostInfo();
	void ClearStoredHostInfo();

	UBOOL ExternalAddressMatches( const UTcpipConnectionXbox* Connection ) const;

protected:
	void UnregisterInfo( FSessionInfo* SessionInfo );

	/* Since bindnextport seems to ignore previously "bound" ports on the client side,
	   this structure keeps track of ports as they are assigned; the port used by
	   a Driver is removed from this list in Destroy. It is assumed there will never be
	   more than 2 Driver objects so having room for 10 ports should be no problem. */
	static INT PortsInUse[10];
	static INT PortsInUseCount;

	static FSessionInfo HostInfo;
	static FSessionInfo RemoteInfo[2]; // One for current, one for pending.
	static UBOOL SessionInfoInitialized;
	XNKID	PreviousSessionID;

	FSessionInfo* CurrentRemoteInfo;
	// --- gam
#endif
};
