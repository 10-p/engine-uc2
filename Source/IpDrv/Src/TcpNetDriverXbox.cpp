/*=============================================================================
	TcpNetDriver.cpp: Unreal TCP/IP driver.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Tim Sweeney.
	* Modified by Brian C. Ladd to incorporate Xbox Live features from Championship
Notes:
	* See \msdev\vc98\include\winsock.h and \msdev\vc98\include\winsock2.h
	  for Winsock WSAE* errors returned by Windows Sockets.
=============================================================================*/

#include "UnTcpNetDriverXbox.h"
#ifdef _XBOX
#include "XboxLive.h"
#endif
/*-----------------------------------------------------------------------------
	Declarations.
-----------------------------------------------------------------------------*/

/* yucky legacy hack. --ryan. */
#ifdef __linux__
#  ifndef MSG_ERRQUEUE
#    define MSG_ERRQUEUE 0x2000
#  endif
#endif

// Size of a UDP header.
#define IP_HEADER_SIZE     (20)
#define UDP_HEADER_SIZE    (IP_HEADER_SIZE+8)
#define SLIP_HEADER_SIZE   (UDP_HEADER_SIZE+4)
#define WINSOCK_MAX_PACKET (960) // sjs! (512)
#define NETWORK_MAX_PACKET (1304) // sjs! (576)

// Variables.
extern UBOOL GIpDrvInitialized;

//scion bcl -- from Championship
#ifdef _XBOX
UBOOL failedToGetXNet = 0;
#endif

/*-----------------------------------------------------------------------------
	UTcpipConnectionXbox.
-----------------------------------------------------------------------------*/

//
// Windows socket class.
//
// Constructors and destructors.
UTcpipConnectionXbox::UTcpipConnectionXbox( SOCKET InSocket, UNetDriver* InDriver, sockaddr_in InRemoteAddr, EConnectionState InState, UBOOL InOpenedLocally, const FURL& InURL, UBOOL InbUseVDP )
:	UNetConnection	( InDriver, InURL )
,	Socket			( InSocket )
,	RemoteAddr		( InRemoteAddr )
,	OpenedLocally	( InOpenedLocally )
,	bUseVDP			( InbUseVDP )		//epic rwd - read vdp option
{
	guard(UTcpipConnectionXbox::UTcpipConnectionXbox);

	// Init the connection.
	State                 = InState;
	MaxPacket			  = WINSOCK_MAX_PACKET;
	PacketOverhead		  = UDP_HEADER_SIZE;
#ifdef _XBOX
	PacketOverhead       += 16; // sjs - addition xbox udp header

	appMemzero(&LiveAddress, sizeof(XNADDR));
#endif
	OpenedTime			  = appSeconds();
	InitOut();

	// In connecting, figure out IP address.
	if( InOpenedLocally )
	{
		const TCHAR* s = *InURL.Host;
		INT i;
		for( i=0; i<4 && s!=NULL && *s>='0' && *s<='9'; i++ )
		{
			s = appStrchr(s,'.');
			if( s )
				s++;
		}
		if( i==4 && !s )
		{
			// Get numerical address directly.
			IpSetInt(RemoteAddr.sin_addr, inet_addr( appToAnsi(*InURL.Host)));
		}
		else
		{
			// Create thread to resolve the address.
			ResolveInfo = NEW FResolveInfo( *InURL.Host );
		}
#ifdef _XBOX
		
		UTcpNetDriverXbox::FSessionInfo* SessionInfo = ((UTcpNetDriverXbox*)Driver)->GetRemoteInfo();
		if ( SessionInfo )
		{
			LiveAddress = SessionInfo->HostAddr;
			debugf(NAME_DevLive, TEXT("Opening connection to remote IP address %s"), *IpString(LiveAddress.inaOnline,LiveAddress.wPortOnline));
		}
	}
	else
	{
		FString AddressString;
		if ( SUCCEEDED(XNetInAddrToXnAddr(RemoteAddr.sin_addr, &LiveAddress, NULL)) )
			AddressString = IpString(LiveAddress.inaOnline,ntohs(RemoteAddr.sin_port));
		else
			AddressString = IpString(RemoteAddr.sin_addr,ntohs(RemoteAddr.sin_port));

		debugf(TEXT("Received connection from '%s' (%s)"), *AddressString, *LowLevelDescribe());
#endif
	}
	unguard;
}

//scion bcl -- Championship
void UTcpipConnectionXbox::Destroy()
{
	guard(UTcpipConnectionXbox::Destroy);
#ifdef _XBOX
	XNetUnregisterInAddr(RemoteAddr.sin_addr);
	VoicePackets.Empty();
#endif
	Super::Destroy();
	unguard;
} // Destroy

void UTcpipConnectionXbox::LowLevelSend( void* Data, INT Count )
{
	guard(UTcpipConnectionXbox::LowLevelSend);
	if( ResolveInfo )
	{
		// If destination address isn't resolved yet, send nowhere.
		if( !ResolveInfo->Resolved() )
		{
			// Host name still resolving.
			return;
		}
		else if( ResolveInfo->GetError() )
		{
			// Host name resolution just now failed.
			debugf( NAME_Log, TEXT("%s"), ResolveInfo->GetError() );
			Driver->ServerConnection->State = USOCK_Closed;
			delete ResolveInfo;
			ResolveInfo = NULL;
			return;
		}
		else
		{
			// Host name resolution just now succeeded.
			RemoteAddr.sin_addr = ResolveInfo->GetAddr();
			debugf( NAME_Log, TEXT("Resolved %s (%s)"), ResolveInfo->GetHostName(), *IpString(ResolveInfo->GetAddr()) );
			delete ResolveInfo;
			ResolveInfo = NULL;
		}
	}
	// Send to remote.
	FUNC_CLOCK(SenCyc,Driver->SendCycles);
	INT result = SOCKET_ERROR;
	if (bUseVDP)
	{
		TArray<BYTE> PacketBuffer;

		// Make room in the buffer for the game data size and the game data itself.
		PacketBuffer.Add(Count + sizeof(_WORD));

		// Set the size of game data (1 word = 2 bytes).
		*((_WORD*) PacketBuffer.GetData()) = (_WORD) Count;

		check(Count < NETWORK_MAX_PACKET && "VDP game data packets must be NETWORK_MAX_PACKET bytes or less");

		// Copy the game data into the buffer.
		if (Count > 0)
		{
			appMemcpy((BYTE*) PacketBuffer.GetData() + sizeof(_WORD), Data, Count);
		}

#ifdef _XBOX
		// Initialize the offset to point just past the game data.
		_WORD pktOffset = Count + sizeof(_WORD);
		// While the buffer isn't full and there are pending voice packets
		while (pktOffset < NETWORK_MAX_PACKET && VoicePackets.Num() > 0)
		{
			// Calculate the size of the next pending voice packet.
			_WORD pktSize = VoicePackets(0).GetPacketSize();
			// If the next pending voice packet fits in the buffer
			if (pktOffset + pktSize < NETWORK_MAX_PACKET)
			{
				// Increase the size of the buffer to accomodate the voice packet.
				PacketBuffer.Add(pktSize);
				// Copy the voice packet into the buffer at the offset.
				appMemcpy((BYTE*) PacketBuffer.GetData() + pktOffset, (BYTE*) &VoicePackets(0), pktSize);
				// Adjust the offset into the buffer by the size of the voice packet.
				pktOffset += pktSize;
				// Remove the voice packet from the queue of pending voice packets.
				VoicePackets.Remove(0,1);
			}
			else
			{
				// No more voice packets can be packed into the buffer.
				break;
			}
		}
#endif
		// Send the packet over the network.
		result = sendto( Socket, (char *) PacketBuffer.GetData(), PacketBuffer.Num(), 0, (sockaddr*)&RemoteAddr, sizeof(RemoteAddr) );
	}
	else
	{
		result = sendto( Socket, (char *)Data, Count, 0, (sockaddr*)&RemoteAddr, sizeof(RemoteAddr) );
	}

	if (result == SOCKET_ERROR)
	{
		if (!bSocketError)
		{
			bSocketError = true;
			
			//epic bcl -- Added full low-level description to the error message; port number and state as well as address
			debugf(NAME_Error, TEXT("%s::LowLevelSend() got SOCKET_ERROR: %s ** bForceTimeout:%s"), GetName(), *LowLevelDescribe(), bForceTimeout ? TEXT("True") : TEXT("False"));

			// if we're a client, and the other end of the connection no longer "exists"
			if (Driver->ServerConnection && WSAGetLastError() == WSAEHOSTUNREACH)
			{
				// possibly the server's encrypted IP changed, so try to re-establish the connection
				// if we haven't already started a reconnect attempt
				if (!bTryLastMatch)
				{
					bTryLastMatch = true;
					Driver->Notify->NotifyReceivedText(this, TEXT("TRY_LAST_MATCH"));
				}

			}

#ifdef DEBUG_SERVERTRAVEL
			if ( bForceTimeout && bTryLastMatch )
			{
				bTryLastMatch = false;
				bSocketError = false;
			}
#endif
		}
		// if a pending client to server xbox connection failed
#ifdef _XBOX
		else if (State == USOCK_Pending && Driver->ServerConnection == this && Driver->IsA(UTcpNetDriverXbox::StaticClass()))
		{
			UTcpNetDriverXbox* XboxDriver = (UTcpNetDriverXbox*)Driver;
			// and we're not using xbox live
			if (!XboxDriver->bUsingXboxLive)
			{
				if ( XboxDriver->Time - XboxDriver->RetryStartTime >= XboxDriver->InitialConnectTimeout )
				{
					debugf(NAME_DevNet, TEXT("Failed to match session ID within InitialConnectTimeout, aborting."));
					State = USOCK_Closed;
				}

				// continue to try again every few seconds in case the server still didn't get its new IP yet
				else if (XboxDriver->Time > XboxDriver->NextRetryTime)
				{
					debugf(NAME_DevNet, TEXT("Retrying connection (%f seconds left)"), XboxDriver->Time - XboxDriver->RetryStartTime);
					XboxDriver->RetryConnection( URL.GetOption(TEXT("SessionID="), TEXT("0")),
												URL.GetOption(TEXT("ExchangeKey="), TEXT("0")),
												URL.GetOption(TEXT("HostAddr="), TEXT("0")) );

					XboxDriver->NextRetryTime = XboxDriver->Time + LAST_MATCH_RETRY_TIME;
				}
			}
		}
#endif
	}
	else 
	{
		bSocketError = false;
#ifdef DEBUG_LOW_LEVEL_DESTINATION
		//epic bcl -- Sometimes a lot is revealed watching low-level send destination addresses...
		//            Not efficient but sometimes useful
		debugf(NAME_Error, TEXT("%s::LowLevelSend: %s"), GetName(), *LowLevelDescribe());
#endif
	}
	unguard;
}

INT UTcpipConnectionXbox::CalcPacketOverhead(INT PacketSize)
{
#ifdef _XBOX
	// Find the amount of padding that will be added to the packet.
	INT PacketSizeRemainder = PacketSize % 8;
	INT Padding = 0;
	if (PacketSizeRemainder > 0)
	{
		Padding = 8 - PacketSizeRemainder;
	}
	return PacketOverhead + Padding;
#else
	return PacketOverhead;
#endif
}

#ifdef _XBOX
UBOOL UTcpipConnectionXbox::HasPendingData()
{
	return (VoicePackets.Num() > 0 || Super::HasPendingData());
}
#endif

void UTcpipConnectionXbox::ReceivedRawPacket(void *Data,INT Count)
{
	guard(UTcpipConnectionXbox::ReceivedRawPacket);

	//epic rwd - read the vdp packet
	if (bUseVDP && Count > 0)
	{
		check(Data);
		// read out the game data size
		_WORD gamePktSize = *(_WORD*)Data;
#ifdef _XBOX
		// read any voice packets, everything after game packet is voice
		_WORD pktOffset = sizeof(_WORD)+gamePktSize;		// skip over the game packet
		_WORD pktSizeTotal = Count-pktOffset;
		while (pktSizeTotal >= sizeof(FVoicePacket))
		{
			FVoicePacket newPkt = *(FVoicePacket*)((BYTE*)Data+pktOffset);
			// decrement the size total left
			pktSizeTotal -= newPkt.GetPacketSize();
			// and the offset
			pktOffset += newPkt.GetPacketSize();
			// send the packet to the voice system for handling
			if (UEngine::GEngine != NULL &&
				UEngine::GEngine->Voice != NULL)
			{
				UEngine::GEngine->Voice->HandleIncomingVoicePacket(this,newPkt);
			}
		}
#endif
		if (gamePktSize > 0)
		{
			Super::ReceivedRawPacket((BYTE*)Data+sizeof(_WORD),gamePktSize);
		}
	}
	else
	{
		// normal packet handling
		Super::ReceivedRawPacket(Data,Count);
	}
	unguard;
}

FString UTcpipConnectionXbox::LowLevelGetRemoteAddress()
{
	guard(UTcpipConnectionXbox::LowLevelGetRemoteAddress);
	return IpString(RemoteAddr.sin_addr,ntohs(RemoteAddr.sin_port));
	unguard;
}

FString UTcpipConnectionXbox::LowLevelDescribe()
{
	guard(UTcpipConnectionXbox::LowLevelDescribe);
	return FString::Printf
	(
		TEXT("%s %s %s state: %s, error: %d"),
		GetName(),
		*URL.Host,
		*IpString(RemoteAddr.sin_addr,ntohs(RemoteAddr.sin_port)),
			State==USOCK_Pending	?	TEXT("Pending")
		:	State==USOCK_Open		?	TEXT("Open")
		:	State==USOCK_Closed		?	TEXT("Closed")
		:								TEXT("Invalid"),
			bSocketError			?	WSAGetLastError()
		:	0
	);
	unguard;
}

IMPLEMENT_CLASS(UTcpipConnectionXbox);

/*-----------------------------------------------------------------------------
	UTcpNetDriverXbox.
-----------------------------------------------------------------------------*/

//scion bcl -- Championship
#ifdef _XBOX
INT UTcpNetDriverXbox::PortsInUse[10];
INT UTcpNetDriverXbox::PortsInUseCount;
// gam ---
UTcpNetDriverXbox::FSessionInfo UTcpNetDriverXbox::HostInfo;
UTcpNetDriverXbox::FSessionInfo UTcpNetDriverXbox::RemoteInfo[2];
UBOOL UTcpNetDriverXbox::SessionInfoInitialized = 0;
// --- gam
#endif

UBOOL UTcpNetDriverXbox::ShouldIncludeInStandbyCheck( const UNetConnection* Connection ) const
{
	guard(UTcpNetDriverXbox::ShouldIncludeInStandbyCheck);

	UBOOL bResult = Super::ShouldIncludeInStandbyCheck(Connection);
#ifdef _XBOX
	if ( bResult )
	{
		// if this client is on the same LAN as the server, ignore it.
		bResult = !ExternalAddressMatches( (const UTcpipConnectionXbox*)Connection );
	}
#endif
	return bResult;

	unguard;
}

#ifdef _XBOX
UBOOL UTcpNetDriverXbox::ExternalAddressMatches( const UTcpipConnectionXbox* Connection ) const
{
	guard(UTcpNetDriverXbox::ExternalAddressMatches);

	UBOOL bResult = 0;
	const FSessionInfo* HostInfo = GetHostInfo();
	if ( HostInfo )
	{
		return (HostInfo->HostAddr.inaOnline.S_un.S_addr != 0 &&
				HostInfo->HostAddr.inaOnline.S_un.S_addr == Connection->LiveAddress.inaOnline.S_un.S_addr);
	}

	return bResult;
	unguard;
}

void UTcpNetDriverXbox::RetryConnection(const FString & XSessionID, const FString & XExchangeKey, const FString & XHostAddr)
{
	guard(UTcpNetDriverXbox::RetryConnection);

	UTcpipConnectionXbox* XboxServerConnection = Cast<UTcpipConnectionXbox>(ServerConnection);
	if (XboxServerConnection)
	{
		ClearStoredHostInfo();
		UnregisterRemoteKey();
		RegisterRemoteKey( XSessionID, XExchangeKey, XHostAddr );

		// Note: On a client, only the sessionID will be of interest on the next change of level so we don't need to update
		// the other (exchange key will be regenerated as will hostaddr)
		UGameEngine * Engine = (UGameEngine *)UEngine::GEngine;
		FString newSessionIDOption = (FString(TEXT("SessionID=")) + XSessionID);

		if ( Engine->GLevel )
			Engine->GLevel->URL.AddOption(*newSessionIDOption);

		if (Engine->GPendingLevel)
			Engine->GPendingLevel->URL.AddOption(*newSessionIDOption);

		if ( Engine->Client && Engine->Client->Viewports.Num() )
		{
			// only the first viewport tracks session ID info
			UViewport* Viewport = Engine->Client->Viewports(0);
			if ( Viewport )
			{
				Viewport->PlayerURL.AddOption(*newSessionIDOption);
			}
		}
		debugf(NAME_Log, TEXT("newSessionIDOption = \"%s\" LastURL = \"%s\""), *newSessionIDOption, *Engine->LastURL.String());

		IN_ADDR RemoteAddr;
		if( XNetXnAddrToInAddr( &GetRemoteInfo()->HostAddr, &GetRemoteInfo()->SessionID, &RemoteAddr ) )
			debugf( NAME_Error, TEXT("XNetXnAddrToInAddr: failed translating remote addr!"));

		FURL& ConnectURL = XboxServerConnection->URL;
		ConnectURL.Host = IpString( RemoteAddr, 0 );
		debugf( NAME_Log, TEXT("RetryConnection: XNet translated host to: %s"), *ConnectURL.Host );
		
		// Rebuild the remote address; we got here because there was a problem and it could well be that the
		// other end has advanced to a new "IP" address. We just got the address from XNetXnAddrtoInAddr so we
		// will point the outbound socket at it.
		sockaddr_in TempAddr;
		TempAddr.sin_family           = AF_INET;
		TempAddr.sin_port             = htons(ConnectURL.Port);
		IpSetBytes(TempAddr.sin_addr, 0, 0, 0, 0);

		IpSetInt(TempAddr.sin_addr, inet_addr( appToAnsi(*ConnectURL.Host)));
		XboxServerConnection->RemoteAddr = TempAddr;
		debugf( NAME_Log, TEXT("< RetryConnection (%s)"), *XboxServerConnection->LowLevelDescribe());
	}
	else
	{
		debugf(NAME_Warning, TEXT("NULL ServerConnection in RetryConnection()"));
	}

	unguard;
} // RetryConnection
#endif

INT UTcpNetDriverXbox::Exec( const TCHAR* Cmd, FOutputDevice& Ar ) // sjs
{
	guard(UTcpNetDriverXbox::Exec);

	if( ParseCommand(&Cmd,TEXT("RETRY_XNET")) )
	{
#ifdef _XBOX// recreate the ConnectURL.Port and ConnectURL.Host using xnet secure libs
		if( ServerConnection ) 
		{
			guard(Retry_Xnet);

			FURL& ConnectURL = ServerConnection->URL;

			FString XSessionID(ConnectURL.GetOption(TEXT("SessionID="),TEXT("0")));
			FString XExchangeKey(ConnectURL.GetOption(TEXT("ExchangeKey="),TEXT("0")));
			FString XHostAddr(ConnectURL.GetOption(TEXT("HostAddr="),TEXT("0")));

			/* If the connection was from XboxLive, a retry requires us to start a query by XNKID
			   (the SessionID).
			*/
			if (bUsingXboxLive)
			{
				XNKID   SessionID;
				StringToBytes((BYTE*)&SessionID, sizeof(SessionID), XSessionID);

				UBOOL WasInvited = 0;
				if ( ConnectURL.HasOption(TEXT("WasInvited")) )
				{
					FString WasInvitedString(ConnectURL.GetOption(TEXT("WasInvited="),TEXT("1")));
					WasInvited = appAtoi(*WasInvitedString);
				}

				FLOAT StartTime = appSeconds() - (Time - ServerConnection->LastReceiveTime);
				XboxLiveManager::getXboxLiveManager()->StartFindMatchCounter( StartTime );
				XboxLiveManager::getXboxLiveManager()->StartFindFromPreviousIDQuery(SessionID, WasInvited, true, this);
			} 
			else
			{
				// start retrying
				RetryConnection(XSessionID, XExchangeKey, XHostAddr);
				NextRetryTime = Time + LAST_MATCH_RETRY_TIME;
				RetryStartTime = Time;
			}

			debugf( NAME_Log, TEXT("RETRY_XNET: Session key decoded (for connect!): ") );
			debugf( NAME_Log, TEXT("RETRY_XNET: SessionID: %s"), *XSessionID );
			debugf( NAME_Log, TEXT("RETRY_XNET: ExchangeKey: %s"), *XExchangeKey );
			debugf( NAME_Log, TEXT("RETRY_XNET: HostAddr: %s"), *XHostAddr );


			unguard;
		}
#endif
		return 1;
	}
	return Super::Exec(Cmd, Ar);

	unguard;
} // Exec

// UTcpNetDriverXbox interface.
UBOOL UTcpNetDriverXbox::InitBase( UBOOL Connect, FNetworkNotify* InNotify, FURL& URL, FString& Error )
{
	guard(UTcpNetDriverXbox::UTcpNetDriverXbox);

	// Init WSA.
	if( !InitSockets( Error ) )
		return 0;

#ifdef _XBOX
	if( !SessionInfoInitialized )
	{
		appMemzero(&HostInfo, sizeof(HostInfo));
		appMemzero(&RemoteInfo[0], sizeof(RemoteInfo[0]));
		appMemzero(&RemoteInfo[1], sizeof(RemoteInfo[1]));
		appMemzero(PortsInUse, sizeof(PortsInUse));
		PortsInUseCount = 0;
		SessionInfoInitialized = 1;
	}
	CurrentRemoteInfo = NULL;
#endif

	// Create UDP socket and enable broadcasting.
#ifdef _XBOX
	if (bUseVDP)
	{
		Socket = socket( AF_INET, SOCK_DGRAM, IPPROTO_VDP );
	}
	else
#endif
	{
		Socket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	}
	if( Socket == INVALID_SOCKET )
	{
		Socket = 0;
		Error = FString::Printf( TEXT("WinSock: socket failed (%i)"), SocketError() );
		return 0;
	}
	UBOOL TrueBuffer=1;
	if (!bUseVDP)		
	{
		if( setsockopt( Socket, SOL_SOCKET, SO_BROADCAST, (char*)&TrueBuffer, sizeof(TrueBuffer) ) )
		{
			Error = FString::Printf( TEXT("%s: setsockopt SO_BROADCAST failed (%i)"), SOCKET_API, SocketError() );
			return 0;
		}
	}
	if( SetSocketReuseAddr( Socket ) )
		debugf(TEXT("setsockopt with SO_REUSEADDR failed"));

	if( SetSocketRecvErr( Socket ) )
		debugf(TEXT("setsockopt with IP_RECVERR failed"));

	// Increase socket queue size, because we are polling rather than threading
	// and thus we rely on Windows Sockets to buffer a lot of data on the server.
	INT RecvSize = Connect ? 0x8000 : 0x20000, SizeSize=sizeof(RecvSize);
	INT SendSize = Connect ? 0x8000 : 0x20000;
	setsockopt( Socket, SOL_SOCKET, SO_RCVBUF, (char*)&RecvSize, SizeSize );
	getsockopt( Socket, SOL_SOCKET, SO_RCVBUF, (char*)&RecvSize, GCC_OPT_INT_CAST &SizeSize );
	setsockopt( Socket, SOL_SOCKET, SO_SNDBUF, (char*)&SendSize, SizeSize );
	getsockopt( Socket, SOL_SOCKET, SO_SNDBUF, (char*)&SendSize, GCC_OPT_INT_CAST &SizeSize );
	debugf( NAME_Init, TEXT("%s: Socket queue %i / %i"), SOCKET_API, RecvSize, SendSize );

	// Bind socket to our port.
	LocalAddr.sin_family    = AF_INET;
	LocalAddr.sin_addr		= getlocalbindaddr( *GLog );
	LocalAddr.sin_port      = 0;
	UBOOL HardcodedPort     = 0;
	if( !Connect )
	{
		// Init as a server.
		HardcodedPort = Parse( appCmdLine(), TEXT("PORT="), URL.Port );
		LocalAddr.sin_port = htons(URL.Port);
	}
#ifdef _XBOX
	else // even clients should try 1000; Note that only the first trip through this code will get 1000...need a way to check whether the port has been assigned
	{
		/* Note that there is an implicit "punt" condition if there all of the ports that can be held in PortsInUse are allocated.
		In that condition, the port number remains 0 and is system allocated; hope we don't try to use that port number the next time
		through this function...unlikely but a possible collision.
		*/
		if (PortsInUseCount != ARRAY_COUNT(PortsInUse))
		{		
			INT myPort = 1000;
			bool usable = false;
			while(!usable) {
				usable = true;
				for (INT i = 0; i < PortsInUseCount; ++i) {
					if (PortsInUse[i] == myPort) {
						usable = false;
						break;
					}
				}
				if (!usable) {
					++myPort;
				}
			}
			PortsInUse[PortsInUseCount++] = myPort;

			LocalAddr.sin_port = htons(myPort);
		}
	}
#endif

	INT AttemptPort = ntohs(LocalAddr.sin_port);
	INT boundport   = bindnextport( Socket, &LocalAddr, HardcodedPort ? 1 : 20, 1 );
	if( boundport==0 )
	{
		Error = FString::Printf( TEXT("%s: binding to port %i failed (%i)"), SOCKET_API, AttemptPort, SocketError() );
		return 0;
	}
	if( !SetNonBlocking( Socket ) )
	{
		Error = FString::Printf( TEXT("%s: SetNonBlocking failed (%i)"), SOCKET_API, SocketError() );
		return 0;
	}

	// Success.
	return 1;
	unguard;
}

//
// Windows sockets network driver.
//
UBOOL UTcpNetDriverXbox::InitConnect( FNetworkNotify* InNotify, FURL& ConnectURL, FString& Error )
{
	guard(UTcpNetDriverXbox::InitConnect);

	if( !Super::InitConnect( InNotify, ConnectURL, Error ) )
		return 0;

	if( !InitBase( 1, InNotify, ConnectURL, Error ) )
		return 0;

#ifdef _XBOX // recreate the ConnectURL.Port and ConnectURL.Host using xnet secure libs


	UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
	if ( UIX && UIX->m_pLiveEngine )
		UIX->m_pLiveEngine->SetProperty(UIX_PROPERTY_HOST_NO_MIGRATION, FALSE);

	ClearStoredHostInfo();

	guard(ParseHostStructures);
	FString XSessionID(ConnectURL.GetOption(TEXT("SessionID="),TEXT("0")));
	FString XExchangeKey(ConnectURL.GetOption(TEXT("ExchangeKey="),TEXT("0")));
	FString XHostAddr(ConnectURL.GetOption(TEXT("HostAddr="),TEXT("0")));

	FString ClientOption(ConnectURL.GetOption(TEXT("XboxLiveClient"), GFalse));
	if ( ClientOption.Left(1) == TEXT("=") )
		ClientOption = ClientOption.Mid(1);

	if ( ClientOption == GTrue || appAtoi(*ClientOption) != 0 )
		bUsingXboxLive = true;
	else bUsingXboxLive = false;
	debugf(NAME_DevLive, TEXT("XBoxLiveClient: %s"), bUsingXboxLive ? TEXT("True") : TEXT("False"));

	debugf( NAME_Log, TEXT("InitConnect: SessionID: %s"), *XSessionID );
	debugf( NAME_Log, TEXT("InitConnect: ExchangeKey: %s"), *XExchangeKey );
	debugf( NAME_Log, TEXT("InitConnect: HostAddr: %s"), *XHostAddr );

	RegisterRemoteKey( XSessionID, XExchangeKey, XHostAddr );

	// Get the XNADDR for the server
	IN_ADDR RemoteAddr;

	// XNetXnAddrToInAddr returns 0 if successful, non-zero otherwise
	if( XNetXnAddrToInAddr( &GetRemoteInfo()->HostAddr, &GetRemoteInfo()->SessionID, &RemoteAddr ) )
	{
		debugf( NAME_Error, TEXT("XNetXnAddrToInAddr: failed translating remote addr!"));
	}

	ConnectURL.Host = IpString( RemoteAddr, 0 );
	FString OnlineAddress = IpString(GetRemoteInfo()->HostAddr.inaOnline, GetRemoteInfo()->HostAddr.wPortOnline);
	debugf(TEXT("XNet translated host to: %s (%s)"), *ConnectURL.Host, *OnlineAddress );
	unguard;

	// Add this machine's MAC address to the connect URL
	DWORD dwStatus = XNET_GET_XNADDR_NONE;
	XNADDR XboxAddr;
	appMemzero( &XboxAddr, sizeof(XNADDR) );
	do{
		dwStatus = XNetGetTitleXnAddr( &XboxAddr );
	} while( dwStatus == XNET_GET_XNADDR_PENDING );

	if( dwStatus == XNET_GET_XNADDR_NONE )
		debugf( NAME_Error, TEXT("XNetGetTitleXnAddr failed!"));
	else
	{
		FString Option(TEXT("UniqueID=")), MacAddress;
		BytesToString(XboxAddr.abEnet, 6, MacAddress);

		debugf(NAME_DevNet, TEXT("Appending MAC address to connect URL: %s"), MacAddress);
		ConnectURL.AddOption(*(Option + MacAddress));
		BytesToString(XboxAddr.abEnet, 6, MacAddress);
	}

#endif


	// Connect to remote.
	sockaddr_in TempAddr;
	TempAddr.sin_family           = AF_INET;
	TempAddr.sin_port             = htons(ConnectURL.Port);
	IpSetBytes(TempAddr.sin_addr, 0, 0, 0, 0);

	// Create new connection.
	//epic rwd - check if we should use vdp connection for XHV
	ServerConnection = new UTcpipConnectionXbox( Socket, this, TempAddr, USOCK_Pending, 1, ConnectURL, bUseVDP );
	debugf( NAME_DevNet, TEXT("Game client on port %i, rate %i"), ntohs(LocalAddr.sin_port), ServerConnection->CurrentNetSpeed );

	// Create channel zero.
	GetServerConnection()->CreateChannel( CHTYPE_Control, 1, 0 );

	return 1;
	unguard;
}

UBOOL UTcpNetDriverXbox::InitListen( FNetworkNotify* InNotify, FURL& LocalURL, FString& Error )
{
	guard(UTcpNetDriverXbox::InitListen);
	if( !Super::InitListen( InNotify, LocalURL, Error ) )
		return 0;
	if( !InitBase( 0, InNotify, LocalURL, Error ) )
		return 0;

	// Update result URL.
	LocalURL.Host = IpString(LocalAddr.sin_addr);
	LocalURL.Port = ntohs( LocalAddr.sin_port );

#ifdef _XBOX
	//epic bcl -- Lifted from Championship
	bUsingXboxLive = LocalURL.HasOption(TEXT("XboxLiveHost"));

	UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
	if ( UIX && UIX->m_pLiveEngine )
		UIX->m_pLiveEngine->SetProperty(UIX_PROPERTY_HOST_NO_MIGRATION, TRUE);

	if( !bUsingXboxLive ) // This registers/reloads a session for system link.
	{
		if( LoadHostInfo() )
			debugf( NAME_Log, TEXT("Session key loaded (for listen!): ") );
		else
		{
			XNKID   SessionID;
			XNKEY   ExchangeKey;

			if( XNetCreateKey( &SessionID, &ExchangeKey ) )
				debugf( NAME_Error, TEXT("XNetCreateKey failed.") );

			if( XNetRegisterKey( &SessionID, &ExchangeKey ) )
				debugf( NAME_Error, TEXT("XNetRegisterKey failed."));

			SetHostKey( &SessionID, &ExchangeKey );
			debugf( NAME_Log, TEXT("Session key registered (for listen!): ") );
			{

				FString SessionID, ExchangeKey, HostAddr;

				BytesToString((BYTE*)&HostInfo.SessionID, sizeof(HostInfo.SessionID), SessionID);
				BytesToString((BYTE*)&HostInfo.ExchangeKey, sizeof(HostInfo.ExchangeKey), ExchangeKey);
				BytesToString((BYTE*)&HostInfo.HostAddr, sizeof(HostInfo.HostAddr), HostAddr);

				debugf( NAME_Log, TEXT("InitListen: SessionID: %s"), *SessionID );
				debugf( NAME_Log, TEXT("InitListen: ExchangeKey: %s"), *ExchangeKey );
				debugf( NAME_Log, TEXT("InitListen: HostAddr: %s"), *HostAddr );
			}

		}
	}

#endif

	return 1;
	unguard;
}

// gam ---
void UTcpNetDriverXbox::PreServerTravel()
{
	guard(UTcpNetDriverXbox::PreServerTravel);

#ifdef _XBOX
	SaveHostInfo();
#endif

	Super::PreServerTravel();

	unguard;
}

void UTcpNetDriverXbox::StaticConstructor()
{
	guard(UTcpNetDriverXbox::StaticConstructor);

#ifdef _XBOX
	//epic rwd - read in VDP support option
	new(GetClass(),TEXT("bUseVDP"),          		RF_Public)UBoolProperty (CPP_PROPERTY(bUseVDP		        ), TEXT("UTcpNetDriverXbox"), CPF_Config );
#else
	new(GetClass(),TEXT("AllowPlayerPortUnreach"),	RF_Public)UBoolProperty (CPP_PROPERTY(AllowPlayerPortUnreach), TEXT("Client"), CPF_Config );
	new(GetClass(),TEXT("LogPortUnreach"),			RF_Public)UBoolProperty (CPP_PROPERTY(LogPortUnreach        ), TEXT("Client"), CPF_Config );
	new(GetClass(),TEXT("MaxConnPerIPPerMinute"),	RF_Public)UIntProperty  (CPP_PROPERTY(MaxConnPerIPPerMinute ), TEXT("Client"), CPF_Config );
	new(GetClass(),TEXT("LogMaxConnPerIPPerMin"),	RF_Public)UBoolProperty (CPP_PROPERTY(LogMaxConnPerIPPerMin ), TEXT("Client"), CPF_Config );
#endif

	unguard;
}
// --- gam

void UTcpNetDriverXbox::TickDispatch( FLOAT DeltaTime )
{
	guard(UTcpNetDriverXbox::TickDispatch);
	Super::TickDispatch( DeltaTime );

	// Process all incoming packets.
	BYTE Data[NETWORK_MAX_PACKET];
	sockaddr_in FromAddr;
	for( ; ; )
	{
		// Get data, if any.
		RecvCycles -= appCycles();
		INT FromSize = sizeof(FromAddr);
		INT Size = recvfrom( Socket, (char*)Data, sizeof(Data), 0, (sockaddr*)&FromAddr, GCC_OPT_INT_CAST &FromSize );

		RecvCycles += appCycles();
		// Handle result.
		if( Size==SOCKET_ERROR )
		{
			INT Error = WSAGetLastError();
			if( Error == WSAEWOULDBLOCK )
			{
				// No data
				break;
			}
			else
			{
                #ifdef __linux__
                    // determine IP address where problem originated. --ryan.
                    recvfrom(Socket, NULL, 0, MSG_ERRQUEUE, (sockaddr*)&FromAddr, GCC_OPT_INT_CAST &FromSize );
                #endif

				if( Error != UDP_ERR_PORT_UNREACH )
				{
					static UBOOL FirstError=1;
					if( FirstError )
						debugf( TEXT("UDP recvfrom error: %i from %s:%d"), WSAGetLastError(), *IpString(FromAddr.sin_addr), ntohs(FromAddr.sin_port) );
					FirstError = 0;
					break;
				}
			}
		}
		// Figure out which socket the received data came from.
		UTcpipConnectionXbox* Connection = NULL;
		if( GetServerConnection() && IpMatches(GetServerConnection()->RemoteAddr,FromAddr) )
			Connection = GetServerConnection();
		for( INT i=0; i<ClientConnections.Num() && !Connection; i++ )
			if( IpMatches( ((UTcpipConnectionXbox*)ClientConnections(i))->RemoteAddr, FromAddr ) )
				Connection = (UTcpipConnectionXbox*)ClientConnections(i);

		if( Size==SOCKET_ERROR )
		{
			if( Connection )
			{
				if( Connection != GetServerConnection() )
				{
					// We received an ICMP port unreachable from the client, meaning the client is no longer running the game
					// (or someone is trying to perform a DoS attack on the client)

					// rcg08182002 Some buggy firewalls get occasional ICMP port
					// unreachable messages from legitimate players. Still, this code
					// will drop them unceremoniously, so there's an option in the .INI
					// file for servers with such flakey connections to let these
					// players slide...which means if the client's game crashes, they
					// might get flooded to some degree with packets until they timeout.
					// Either way, this should close up the usual DoS attacks.
					if ((Connection->State != USOCK_Open) || (!AllowPlayerPortUnreach))
					{
						if (LogPortUnreach)
							debugf( TEXT("Received ICMP port unreachable from client %s:%d.  Disconnecting."), *IpString(FromAddr.sin_addr), ntohs(FromAddr.sin_port) );
						delete Connection;
					}
				}
			}
			else
			{
				if (LogPortUnreach)
					debugf( TEXT("Received ICMP port unreachable from %s:%d.  No matching connection found."), *IpString(FromAddr.sin_addr), ntohs(FromAddr.sin_port) );
			}
		}
		else
		{
			// Go through existing connection list and see how many connections for this IP we have opened in the last minute.
			if( MaxConnPerIPPerMinute == 0 )
				MaxConnPerIPPerMinute = 6;
			INT SameIPCount=0;
			for( INT i=0; i<ClientConnections.Num() && !Connection; i++ )
				if( IpAddressMatches( ((UTcpipConnectionXbox*)ClientConnections(i))->RemoteAddr, FromAddr ) && ((UTcpipConnectionXbox*)ClientConnections(i))->OpenedTime > (appSeconds() - 60.0) )
					SameIPCount++;

			if( SameIPCount < MaxConnPerIPPerMinute )
			{
			    // If we didn't find a client connection, maybe create a new one.
				//epic bcl -- Changed format for debugging of secure connections
				if ( !Connection ) 
				{
					EAcceptConnection AcceptingConnection = Notify->NotifyAcceptingConnection();
					if (AcceptingConnection == ACCEPTC_Accept)
					//if( !Connection && Notify->NotifyAcceptingConnection()==ACCEPTC_Accept )
					{
							//epic rwd - check if we shouldd use vdp connection for XHV
							Connection = new UTcpipConnectionXbox( Socket, this, FromAddr, USOCK_Open, 0, FURL(), bUseVDP );
							Connection->URL.Host = IpString(FromAddr.sin_addr);
							Notify->NotifyAcceptedConnection( Connection );
							ClientConnections.AddItem( Connection );
					}
				}
			    // Send the packet to the connection for processing.
			    if( Connection )
				{
#ifdef _XBOX
					if ( !ExternalAddressMatches(Connection) )
#endif
						UpdateLastReceiveTime();

					Connection->ReceivedRawPacket( Data, Size );
				}
			}
			else if( LogMaxConnPerIPPerMin )
			{
				debugf( TEXT("More than %d logins per minute for %s.  Ignoring."), MaxConnPerIPPerMinute, *IpString(FromAddr.sin_addr) );
			}
		}
	}
	unguard;
}

FString UTcpNetDriverXbox::LowLevelGetNetworkNumber()
{
	guard(UTcpNetDriverXbox::LowLevelGetNetworkNumber);
	return IpString(LocalAddr.sin_addr);
	unguard;
}

void UTcpNetDriverXbox::LowLevelDestroy()
{
	guard(UTcpNetDriverXbox::LowLevelDestroy);

	// Close the socket.
	if( Socket )
	{
		if( closesocket(Socket) )
			debugf( NAME_Exit, TEXT("WinSock closesocket error (%i)"), WSAGetLastError() );
		Socket=NULL;
		debugf( NAME_Exit, TEXT("WinSock shut down") );
	}

#ifdef _XBOX
	// clean up the PortsInUse array: Find the port that is going away and remove it from the list
	// linear search
	INT myPort = ntohs(LocalAddr.sin_port);
	bool found = false;
	for (INT i = 0; i < PortsInUseCount; ++i) {
		if (!found) {
			found = (myPort == PortsInUse[i]);
		} else {
			PortsInUse[i-1] = PortsInUse[i];
		}
	}
	if (found)
		--PortsInUseCount;

	UnregisterHostKey();
	UnregisterRemoteKey();
#endif

	unguard;
}

UTcpipConnectionXbox* UTcpNetDriverXbox::GetServerConnection()
{
	return (UTcpipConnectionXbox*)ServerConnection;
}

//
// Return the NetDriver's socket.  For master server NAT socket opening purposes.
//
FSocketData UTcpNetDriverXbox::GetSocketData()
{
	guard(UTcpNetDriverXbox::GetSocketData);

	FSocketData Result;
	Result.Socket = Socket;
	Result.UpdateFromSocket();
	return Result;

	unguard;
}

// gam ---

#ifdef _XBOX
void UTcpNetDriverXbox::SetHostKey( XNKID* SessionID, XNKEY* ExchangeKey )
{
	guard(UTcpNetDriverXbox::SetHostKey);

	if
		(
		HostInfo.RefCount &&
		!appMemcmp( &HostInfo.SessionID, SessionID, sizeof(*SessionID) ) &&
		!appMemcmp( &HostInfo.ExchangeKey, ExchangeKey, sizeof(*ExchangeKey) )
		)
	{
		++HostInfo.RefCount;
		return;
	}

	if( HostInfo.RefCount )
	{
		check( HostInfo.RefCount == 1 );
		debugf( NAME_Log, TEXT("XNetUnregisterKey(HostKey) and release QOS listen.") );
		// listen for qos determination
		XNetQosListen( &HostInfo.SessionID, NULL, 0, 0, XNET_QOS_LISTEN_RELEASE );
		XNetUnregisterKey( &HostInfo.SessionID );
	}

	appMemcpy( &HostInfo.SessionID, SessionID, sizeof(*SessionID) );
	appMemcpy( &HostInfo.ExchangeKey, ExchangeKey, sizeof(*ExchangeKey) );

	HostInfo.RefCount = 1;

	DWORD dwStatus;

	do{
		dwStatus = XNetGetTitleXnAddr( &HostInfo.HostAddr );
	} while( dwStatus == XNET_GET_XNADDR_PENDING );

	if( dwStatus == XNET_GET_XNADDR_NONE )
		debugf( NAME_Error, TEXT("XNetGetTitleXnAddr failed!"));

	// listen for qos determination
	if( XNetQosListen(&HostInfo.SessionID, NULL, 0, 0, XNET_QOS_LISTEN_ENABLE )==0 )
	{
		debugf(TEXT("Host is listening for QOS packets."));
	}


	debugf( NAME_DevNet, TEXT("Game host listening for clients.  TitleAddress:%s   LiveAddress:%s"), *IpString(LocalAddr.sin_addr,ntohs(LocalAddr.sin_port)), *IpString(HostInfo.HostAddr.inaOnline, HostInfo.HostAddr.wPortOnline) );
	unguard;
}

void UTcpNetDriverXbox::UnregisterHostKey()
{
	guard(UTcpNetDriverXbox::UnregisterHostKey);

	UnregisterInfo( GetHostInfo() );
	
	unguard;
}

UTcpNetDriverXbox::FSessionInfo* UTcpNetDriverXbox::GetHostInfo()
{
	guard(UTcpNetDriverXbox::GetHostInfo);
	return ( HostInfo.RefCount == 0 ) ? NULL : &HostInfo;
	unguard;
}

const UTcpNetDriverXbox::FSessionInfo* UTcpNetDriverXbox::GetHostInfo() const
{
	guard(UTcpNetDriverXbox::GetHostInfo);
	return ( HostInfo.RefCount == 0 ) ? NULL : &HostInfo;
	unguard;
}

void UTcpNetDriverXbox::RegisterRemoteKey( const FString &XSessionID, const FString &XExchangeKey, const FString &XHostAddr )
{
	guard(UTcpNetDriverXbox::RegisterRemoteKey);

	XNKID   SessionID;
	XNKEY   ExchangeKey;
	XNADDR  HostAddr;

	StringToBytes((BYTE*)&SessionID, sizeof(SessionID), XSessionID);
	StringToBytes((BYTE*)&ExchangeKey, sizeof(ExchangeKey), XExchangeKey);
	StringToBytes((BYTE*)&HostAddr, sizeof(HostAddr), XHostAddr);

	for( INT i = 0 ; i < ARRAY_COUNT(RemoteInfo); i++ )
	{
		if
			(
			RemoteInfo[i].RefCount &&
			!appMemcmp( &RemoteInfo[i].SessionID, &SessionID, sizeof(SessionID) ) &&
			!appMemcmp( &RemoteInfo[i].ExchangeKey, &ExchangeKey, sizeof(ExchangeKey) )
			)
		{
			CurrentRemoteInfo = &RemoteInfo[i];
			RemoteInfo[i].RefCount++;
			return;
		}
	}

	CurrentRemoteInfo = NULL;

	for( INT i = 0 ; i < ARRAY_COUNT(RemoteInfo); i++ )
	{
		if( !RemoteInfo[i].RefCount )
		{
			CurrentRemoteInfo = &RemoteInfo[i];
			break;
		}
	}

	check( CurrentRemoteInfo );

	appMemcpy( &CurrentRemoteInfo->SessionID, &SessionID, sizeof(SessionID) );
	appMemcpy( &CurrentRemoteInfo->ExchangeKey, &ExchangeKey, sizeof(ExchangeKey) );
	appMemcpy( &CurrentRemoteInfo->HostAddr, &HostAddr, sizeof(HostAddr) );

	if( XNetRegisterKey( &SessionID, &ExchangeKey ) )
		debugf( NAME_Error, TEXT("RegisterRemoteKey: failed registering remote key!") );

	CurrentRemoteInfo->RefCount = 1;

	unguard;
}

void UTcpNetDriverXbox::UnregisterRemoteKey()
{
	guard(UTcpNetDriverXbox::UnregisterRemoteKey);

	UnregisterInfo( CurrentRemoteInfo );

	unguard;
}

void UTcpNetDriverXbox::UnregisterInfo( FSessionInfo* SessionInfo )
{
	guard(UTcpNetDriverXbox::UnregisterInfo);

	if( !SessionInfo )
		return;

	// if the specified SessionInfo was never registered, just bail
	if( !SessionInfo->RefCount )
		return;

	// if the SessionInfo was registered more than once, and this wasn't the first one, bail
	if( --(SessionInfo->RefCount) )
		return;

	debugf( NAME_Log, TEXT("UnregisterInfo: XNetUnregisterKey(%s)"), (SessionInfo == GetHostInfo() ? TEXT("HostKey") : TEXT("RemoteKey")) );
	XNetUnregisterKey( &SessionInfo->SessionID );

	unguard;
}

UTcpNetDriverXbox::FSessionInfo* UTcpNetDriverXbox::GetRemoteInfo()
{
	return( CurrentRemoteInfo );
}

UBOOL UTcpNetDriverXbox::LoadHostInfo()
{
	guard(UTcpNetDriverXbox::LoadHostInfo);

	UBOOL bResult = false;

	XboxLiveManager* Manager = XboxLiveManager::getXboxLiveManager();
	if( Manager->StoredHostInfo )
	{
		check( !HostInfo.RefCount );

		StringToBytes((BYTE*)&HostInfo.SessionID, sizeof(HostInfo.SessionID), Manager->StoredHostSessionID);
		StringToBytes((BYTE*)&HostInfo.ExchangeKey, sizeof(HostInfo.ExchangeKey), Manager->StoredHostExchangeKey);
		PreviousSessionID = HostInfo.SessionID;

		if ( bUsingXboxLive )
		{
			FString SessionID, ExchangeKey, HostAddr;

			BytesToString((BYTE*)&HostInfo.SessionID, sizeof(HostInfo.SessionID), SessionID);
			BytesToString((BYTE*)&HostInfo.ExchangeKey, sizeof(HostInfo.ExchangeKey), ExchangeKey);
			BytesToString((BYTE*)&HostInfo.HostAddr, sizeof(HostInfo.HostAddr), HostAddr);

			debugf( NAME_Log, TEXT("LoadHostInfo: SessionID: %s"), *SessionID );
			debugf( NAME_Log, TEXT("LoadHostInfo: ExchangeKey: %s"), *ExchangeKey );
			debugf( NAME_Log, TEXT("LoadHostInfo: HostAddr: %s"), *HostAddr );
		}

		// If we're hosting on XboxLive, the stored information is of no use (except for
		// the PreviousSessionID). Go home without registering the key.
		if (bUsingXboxLive)
			ClearStoredHostInfo();
		else
		{
			if( XNetRegisterKey( &HostInfo.SessionID, &HostInfo.ExchangeKey ) )
				debugf( NAME_Error, TEXT("LoadHostInfo: XNetRegisterKey failed."));

			HostInfo.RefCount++;

			DWORD dwStatus;
			do{
				dwStatus = XNetGetTitleXnAddr( &HostInfo.HostAddr );
			} while( dwStatus == XNET_GET_XNADDR_PENDING );

			if( dwStatus == XNET_GET_XNADDR_NONE )
				debugf( NAME_Error, TEXT("LoadHostInfo: XNetGetTitleXnAddr failed!"));

			ClearStoredHostInfo();
			bResult = true;
		}
	}

	return bResult;

	unguard;
}

void UTcpNetDriverXbox::SaveHostInfo()
{
	guard(UTcpNetDriverXbox::SaveHostInfo);

	if( !HostInfo.RefCount )
	{
		debugf( NAME_Error, TEXT("Can't PreServerTravel without a registered host key." ) );
		return;
	}

	XboxLiveManager* Manager = XboxLiveManager::getXboxLiveManager();
	Manager->StoredHostInfo = 1;
	BytesToString((BYTE*)&HostInfo.SessionID, sizeof(HostInfo.SessionID), Manager->StoredHostSessionID);
	BytesToString((BYTE*)&HostInfo.ExchangeKey, sizeof(HostInfo.ExchangeKey), Manager->StoredHostExchangeKey);

	unguard;
}

void UTcpNetDriverXbox::ClearStoredHostInfo()
{
	guard(UTcpNetDriverXbox::ClearStoredHostInfo);

	XboxLiveManager* Manager = XboxLiveManager::getXboxLiveManager();
	Manager->StoredHostInfo = 0;
	Manager->StoredHostSessionID.Empty();
	Manager->StoredHostExchangeKey.Empty();

	unguard;
}

#endif
// --- gam


// sjs - I did this because beacon links are text - I probably should have changed that!
void IPDRV_API BytesToString(const BYTE* value, int size, FString& outString)
{
	guard(BytesToString);
	// write size
	outString = FString::Printf(TEXT("%X"), size);
	int chunks = size / sizeof(int);
	const BYTE* cursor = value;
	for( int i=0; i<chunks; i++ )
	{
		outString += FString::Printf(TEXT(".%X"), *(int*)cursor);
		cursor += sizeof(int);
	}
	int remaining = size - (chunks*sizeof(int));
	if( remaining )
	{
		int last = 0;
		for( int i=0; i<remaining; i++ )
		{
			last |= *cursor << (i*8);
			cursor++;
		}
		outString += FString::Printf(TEXT(".%X"), last);
	}
	unguard;
}

void IPDRV_API StringToBytes(BYTE* value, int size, const FString& inString)
{
	guard(StringToBytes);
	TCHAR tmp[1024];
	appStrncpy(tmp, *inString, 1024);
	// read number of bytes
	TCHAR* strCursor = tmp;
	TCHAR* delim = appStrstr(strCursor, TEXT("."));
	if( delim == NULL )
	{
		debugf(TEXT("StringToBytes failed: insufficient data."));
		return;
	}
	*delim = '\0';

	int inBytes;
	appSSCANF(strCursor,TEXT("%X"), &inBytes);
	if( inBytes != size )
	{
		debugf(TEXT("StringToBytes failed: expected %d bytes got %d"), size, inBytes );
		return;
	}

	strCursor = delim;
	strCursor++;

	int chunks = size / sizeof(int);
	BYTE* cursor = value;
	for( int i=0; i<chunks; i++ )
	{
		TCHAR* delim = appStrstr(strCursor, TEXT("."));
		if( delim != NULL )
		{
			*delim = '\0';
		}
		int value;
		appSSCANF(strCursor,TEXT("%X"), &value);
		memcpy(cursor, &value, sizeof(int));
		cursor += sizeof(int);
		strCursor = delim;
		strCursor++;
	}

	int remaining = size - (chunks*sizeof(int));
	if( remaining )
	{
		TCHAR* delim = appStrstr(strCursor, TEXT("."));
		if( delim != NULL )
		{
			*delim = '\0';
		}
		int value;
		appSSCANF(strCursor,TEXT("%X"), &value);
		memcpy(cursor, &value, remaining);
	}
	unguard;
}



IMPLEMENT_CLASS(UTcpNetDriverXbox);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

