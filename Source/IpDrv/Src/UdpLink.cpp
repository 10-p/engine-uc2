/*=============================================================================
	IpDrv.cpp: Unreal TCP/IP driver.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Tim Sweeney.
	* Additions by Brandon Reinhart.
=============================================================================*/

#include "UnIpDrv.h"
#include "UnTcpNetDriver.h"
#include "UnTcpNetDriverXbox.h"
#ifdef __EMSCRIPTEN__
#include "UdpLinkWeb.h"

// netplay: the web's synthetic LAN (UdpLinkWeb.h). Filled in by WebRTCDrv when it is linked in.
FUdpLinkWebHooks* GUdpLinkWebHooks = NULL;
#endif

/*-----------------------------------------------------------------------------
	AUdpLink implementation.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(AUdpLink);

#define MAXRECVDATASIZE 4096

//
// Constructor.
//
AUdpLink::AUdpLink()
{
	guard(AUdpLink::AUdpLink);
	unguard;
}

//
// PostScriptDestroyed.
//
void AUdpLink::PostScriptDestroyed()
{
	guard(AUdpLink::PostScriptDestroyed);
#ifdef __EMSCRIPTEN__
	if( GUdpLinkWebHooks )
		GUdpLinkWebHooks->Unbind( this );
#endif
	if( GetSocket() != INVALID_SOCKET )
		closesocket(GetSocket());
	Super::PostScriptDestroyed();
	unguard;
}

//
// BindPort: Binds a free port or optional port specified in argument one.
//
void AUdpLink::execBindPort( FFrame& Stack, RESULT_DECL )
{
	guard(AUdpLink::execBindPort);
	P_GET_INT_OPTX(InPort,0);
	P_GET_UBOOL_OPTX(bUseNextAvailable,0);
	P_FINISH;
#ifdef __EMSCRIPTEN__
	// netplay: a browser has no UDP socket (socket() fails here, and Epic's callers treat a 0 as fatal:
	// ClientBeaconReceiver never starts, UdpBeacon logs "failed to bind a port"). With the synthetic LAN
	// present the bind is answered by it: the port is the one asked for (or a made-up free one), and SendText
	// and Tick are served from the LAN. UdpLinkWeb.h.
	if( GUdpLinkWebHooks )
	{
		static INT GNextWebPort = 40000;
		INT Bound = InPort ? InPort : GNextWebPort++;
		Port = Bound;
		GUdpLinkWebHooks->Bind( this, Bound );
		*(INT*)Result = Bound;
		return;
	}
#endif
	if( GIpDrvInitialized )
	{
		if( GetSocket()==INVALID_SOCKET )
		{
			Socket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
			if( GetSocket() != INVALID_SOCKET )
			{
				UBOOL TrueBuffer=1;
				if( setsockopt( GetSocket(), SOL_SOCKET, SO_BROADCAST, (char*)&TrueBuffer, sizeof(TrueBuffer) )==0 )
				{
					sockaddr_in Addr;
					Addr.sin_family      = AF_INET;
					Addr.sin_addr        = getlocalbindaddr( Stack );
					Addr.sin_port        = htons(InPort);
					INT boundport = bindnextport( Socket, &Addr, bUseNextAvailable ? 20 : 1, 1 );
					if( boundport )
					{
						#if __UNIX__
						INT pd_flags;
						pd_flags = fcntl( Socket, F_GETFL, 0 );
						pd_flags |= O_NONBLOCK;
						if( fcntl( Socket, F_SETFL, pd_flags ) == 0 )
						#else
						DWORD NoBlock = 1;
						if( ioctlsocket( Socket, FIONBIO, &NoBlock )==0 )
						#endif
						{
							// Success.
							*(INT*)Result = boundport;
							Port = ntohs( Addr.sin_port );
							return;
						}
						else Stack.Logf( TEXT("BindPort: ioctlsocket failed") );
					}
					else Stack.Logf( TEXT("BindPort: bind failed") );
				}
				else Stack.Logf( TEXT("BindPort: setsockopt failed") );
			}
			else Stack.Logf( TEXT("BindPort: socket failed") );
			closesocket(GetSocket());
			// INVALID_SOCKET, not 0: fd 0 is stdin, and every later test is `!= INVALID_SOCKET`, so with 0 the
			// link would poll recvfrom(0) and PostScriptDestroyed would close stdin (under Node, ucc-bin.js, the
			// socket emulation throws on the first such poll).
			Socket=INVALID_SOCKET;
		}
		else Stack.Logf( TEXT("BindPort: already bound") );
	}
	else Stack.Logf( TEXT("BindPort: winsock failed") );
	*(INT*)Result = 0;
	unguard;
}

//
// Send text in a UDP packet.
//
void AUdpLink::execSendText( FFrame& Stack, RESULT_DECL )
{
	guard(AUdpLink::execSendText);
	P_GET_STRUCT(FIpAddr,IpAddr);
	P_GET_STR(Str);
	P_FINISH;
#ifdef __EMSCRIPTEN__
	// netplay: the datagram goes to the synthetic LAN (it answers the System Link REPORT broadcast).
	if( GUdpLinkWebHooks )
	{
		GUdpLinkWebHooks->Send( this, IpAddr.Addr, IpAddr.Port, *Str );
		*(DWORD*)Result = 1;
		return;
	}
#endif
	if( GetSocket() != INVALID_SOCKET )
	{
		sockaddr_in Addr;
		Addr.sin_family      = AF_INET;
		Addr.sin_port        = htons(IpAddr.Port);
		Addr.sin_addr.s_addr = htonl(IpAddr.Addr);
		INT SentBytes = sendto( Socket, (char*)appToAnsi(*Str), sizeof(ANSICHAR)*Str.Len(), MSG_NOSIGNAL, (sockaddr*)&Addr, sizeof(Addr) );
		if ( SentBytes == SOCKET_ERROR )
		{
            TCHAR* error = SocketError(WSAGetLastError());
            Stack.Logf( TEXT("SentText: sendto failed: %s"), error );
			*(DWORD*)Result = 0;
			return;
		}
		else
		{
			//debugf("Sent %i bytes.", SentBytes);
		}
	}
	*(DWORD*)Result = 1;
	unguard;
}

//
// Send binary data.
//
void AUdpLink::execSendBinary( FFrame& Stack, RESULT_DECL )
{
	guard(AUdpLink::execSendBinary);
	P_GET_STRUCT(FIpAddr,IpAddr);
	P_GET_INT(Count);
	P_GET_ARRAY_REF(BYTE,B);
	P_FINISH;
	if( GetSocket() != INVALID_SOCKET )
	{
		sockaddr_in Addr;
		Addr.sin_family      = AF_INET;
		Addr.sin_port        = htons(IpAddr.Port);
		Addr.sin_addr.s_addr = htonl(IpAddr.Addr);
		if( sendto( Socket, (char*)B, Count, MSG_NOSIGNAL, (sockaddr*)&Addr, sizeof(Addr) ) == SOCKET_ERROR )
		{
			Stack.Logf( TEXT("SendBinary: sendto failed") );
			*(DWORD*)Result = 1;
			return;
		}
	}
	*(DWORD*)Result = 0;
	unguard;
}

//
// Time passes...
//
UBOOL AUdpLink::Tick( FLOAT DeltaTime, enum ELevelTick TickType )
{
	guard(AUdpLink::Tick);
	UBOOL Result = Super::Tick( DeltaTime, TickType );
#ifdef __EMSCRIPTEN__
	// netplay: the synthetic LAN's datagrams reach the script through Epic's own receive events, as a
	// received packet would (RMODE_Event; nothing in UC2's System Link uses RMODE_Manual).
	if( GUdpLinkWebHooks )
	{
		DWORD FromAddr = 0;
		INT FromPort = 0;
		FString Text;
		while( GUdpLinkWebHooks->NextReply( this, &FromAddr, &FromPort, Text ) )
		{
			if( ReceiveMode != RMODE_Event )
				continue;
			FIpAddr Addr;
			Addr.Addr = FromAddr;
			Addr.Port = FromPort;
			if( LinkMode == MODE_Line )
				eventReceivedLine( Addr, Text );
			else if( LinkMode == MODE_Binary )
				eventReceivedBinary( Addr, Text.Len(), (BYTE*)appToAnsi(*Text) );
			else
				eventReceivedText( Addr, Text );
		}
		return Result;
	}
#endif
	if( GetSocket() != INVALID_SOCKET )
	{
//scion bcl -- stolen (and modified) from Championship for Xbox encrypted commo
#ifdef _XBOX    // make sure all of our udplink classes have the proper xnet info
		if( XLevel && XLevel->NetDriver ) {
			if (XLevel->NetDriver->IsA(UTcpNetDriverXbox::StaticClass())) {
				UTcpNetDriverXbox * xbdriver = ((UTcpNetDriverXbox *) XLevel->NetDriver);
				UTcpNetDriverXbox::FSessionInfo* HostInfo = xbdriver->GetHostInfo();
				if( HostInfo ) {
					BytesToString( (BYTE*)&HostInfo->SessionID, sizeof(HostInfo->SessionID), LocalSessionID);
					BytesToString( (BYTE*)&HostInfo->ExchangeKey, sizeof(HostInfo->ExchangeKey), LocalExchangeKey);
					BytesToString( (BYTE*)&HostInfo->HostAddr, sizeof(HostInfo->HostAddr), LocalHostAddr);
				}
			}
		}
#else
		//epic bcl -- PC's should have no SessionID or similar.
		LocalSessionID = "";
		LocalExchangeKey = "";
		LocalHostAddr = "";
#endif

		if( ReceiveMode == RMODE_Event )
		{
			BYTE Buffer[MAXRECVDATASIZE];
			sockaddr_in FromAddr;
			SOCKLEN FromSize = sizeof(FromAddr);
			INT Count = recvfrom( GetSocket(), (char*)Buffer, ARRAY_COUNT(Buffer)-1, MSG_NOSIGNAL, (sockaddr*)&FromAddr, &FromSize );
			if( Count!=SOCKET_ERROR )
			{
				FIpAddr Addr;
				Addr.Addr = ntohl( FromAddr.sin_addr.s_addr );
				Addr.Port = ntohs( FromAddr.sin_port );
				if( LinkMode == MODE_Text )
				{
					Buffer[Count]=0;
					eventReceivedText( Addr, appFromAnsi((ANSICHAR*)Buffer) );
				}
				else if ( LinkMode == MODE_Line )
				{
					Buffer[Count]=0;
					eventReceivedLine( Addr, appFromAnsi((ANSICHAR*)Buffer) );
				}
				else if( LinkMode == MODE_Binary )
				{
					eventReceivedBinary( Addr, Count, (BYTE*)Buffer );
				}
			}
		}
		else if( ReceiveMode == RMODE_Manual )
		{
			fd_set SocketSet;
			TIMEVAL SelectTime = {0, 0};
			INT Error;

			FD_ZERO( &SocketSet );
			FD_SET( Socket, &SocketSet );
			Error = select( Socket + 1, &SocketSet, 0, 0, &SelectTime);
			if( Error==0 || Error==SOCKET_ERROR )
			{
				DataPending = 0;
			}
			else
			{
				DataPending = 1;
			}
		}
	}

	return Result;
	unguard;
}

//
// Read text.
//
void AUdpLink::execReadText( FFrame& Stack, RESULT_DECL )
{
	guard(AUdpLink::execReadText);
	P_GET_STRUCT_REF( FIpAddr, Addr );
	P_GET_STR_REF( Str );
	P_FINISH;
	*Str = TEXT("");
	if( GetSocket() != INVALID_SOCKET )
	{
		BYTE Buffer[MAXRECVDATASIZE];
		sockaddr_in FromAddr;
		SOCKLEN FromSize = sizeof(FromAddr);
		INT BytesReceived = recvfrom( (SOCKET)Socket, (char*)Buffer, sizeof(Buffer), MSG_NOSIGNAL, (sockaddr*)&FromAddr, &FromSize );
		if( BytesReceived != SOCKET_ERROR )
		{
			Addr->Addr = ntohl( FromAddr.sin_addr.s_addr );
			Addr->Port = ntohs( FromAddr.sin_port );
			*Str = appFromAnsi((ANSICHAR*)Buffer);
			*(DWORD*)Result = BytesReceived;
		}
		else
		{
			*(DWORD*) Result = 0;
			if ( WSAGetLastError() != WSAEWOULDBLOCK )
				debugf( NAME_Log, TEXT("ReadText: Error reading text.") );
			return;
		}
		return;
	}
	*(DWORD*)Result = 0;

	unguardexec;
}

//
// Read Binary.
//
void AUdpLink::execReadBinary( FFrame& Stack, RESULT_DECL )
{
	guard(AUdpLink::execReadBinary);
	P_GET_STRUCT_REF(FIpAddr, Addr);
	P_GET_INT(Count);
	P_GET_ARRAY_REF(BYTE,B);
	P_FINISH;
	if( GetSocket() != INVALID_SOCKET )
	{
		sockaddr_in FromAddr;
		SOCKLEN FromSize = sizeof(FromAddr);
		INT BytesReceived = recvfrom( (SOCKET) Socket, (char*)B, Min(Count,255), MSG_NOSIGNAL, (sockaddr*)&FromAddr, &FromSize );
		if( BytesReceived != SOCKET_ERROR )
		{
			Addr->Addr = ntohl( FromAddr.sin_addr.s_addr );
			Addr->Port = ntohs( FromAddr.sin_port );
			*(DWORD*) Result = BytesReceived;
		}
		else
		{
			*(DWORD*)Result = 0;
			if( WSAGetLastError() != WSAEWOULDBLOCK )
				debugf( NAME_Log, TEXT("ReadBinary: Error reading text.") );
			return;
		}
		return;
	}
	*(DWORD*)Result = 0;

	unguardexec;
}

//
// Return the UdpLink's socket.  For master server NAT socket opening purposes.
//
FSocketData AUdpLink::GetSocketData()
{
	FSocketData Result;
	Result.Socket = GetSocket();
	Result.UpdateFromSocket();
	return Result;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

