/*=============================================================================
	WebRTCConnection.cpp: a WebRTC data channel as a UNetConnection (E-2).

	engine-ut2004's WebRTCConnection.cpp, which follows UTcpipConnection (IpDrv/Src/TcpNetDriver.cpp).
	Notebook: docs/porting/analysis/webrtc.md.
=============================================================================*/

#include "WebRTCDrv.h"

// The datagram size the engine fills, as UTcpipConnection's WINSOCK_MAX_PACKET: the data channel carries one
// engine packet per message, well under the SCTP message size every browser accepts.
#define WEBRTC_MAX_PACKET_SIZE	512

/*-----------------------------------------------------------------------------
	UWebRTCConnection.
-----------------------------------------------------------------------------*/

UWebRTCConnection::UWebRTCConnection( INT InWebRTCConnId, UNetDriver* InDriver, EConnectionState InState, UBOOL InOpenedLocally, const FURL& InURL )
:	UNetConnection	( InDriver, InURL )
,	WebRTCConnId	( InWebRTCConnId )
,	OpenedLocally	( InOpenedLocally )
{
	guard(UWebRTCConnection::UWebRTCConnection);

	State			= InState;
	MaxPacket		= WEBRTC_MAX_PACKET_SIZE;
	PacketOverhead	= 0;	// the template's choice: no IP/UDP header of ours in the rate accounting
	InitOut();

	debugf( NAME_DevNet, TEXT("[WebRTC] Connection created: connId=%i state=%s openedLocally=%i MaxPacket=%i"),
		WebRTCConnId,
		State==USOCK_Pending ? TEXT("Pending") : State==USOCK_Open ? TEXT("Open") : TEXT("Other"),
		(INT)OpenedLocally, (INT)MaxPacket );

	unguard;
}

void UWebRTCConnection::LowLevelSend( void* Data, INT Count )
{
	guard(UWebRTCConnection::LowLevelSend);

	// As UTcpipConnection: send whatever the engine's State. A client's server connection stays
	// USOCK_Pending through the whole handshake (HandleClientPlayer opens it, and requires Pending), so a
	// State gate here would stop the join. The glue drops the datagram (returns 0) until the pipe is open;
	// the engine's reliability resends what matters, as over UDP.
	if( State == USOCK_Closed )
		return;
	if( Count > MaxPacket )
		debugf( NAME_DevNet, TEXT("[WebRTC] LowLevelSend: packet of %i bytes > MaxPacket %i on connId=%i"), Count, (INT)MaxPacket, WebRTCConnId );

	// A DETACHED connection (connId -1, see UWebRTCNetDriver::InitConnect) no longer owns the wire.
	FUNC_CLOCK(WebRTCSend,Driver->SendCycles);
	if( WebRTCConnId >= 0 )
		webrtc_send( WebRTCConnId, Data, Count );

	unguard;
}

FString UWebRTCConnection::LowLevelGetRemoteAddress()
{
	guard(UWebRTCConnection::LowLevelGetRemoteAddress);
	// One distinct pseudo-address per peer: nothing in the join path parses it as an IP (network.md).
	return FString::Printf( TEXT("webrtc:%i"), WebRTCConnId );
	unguard;
}

FString UWebRTCConnection::LowLevelDescribe()
{
	guard(UWebRTCConnection::LowLevelDescribe);
	return FString::Printf
	(
		TEXT("%s webrtc:%i state: %s"),
		*URL.Host,
		WebRTCConnId,
			State==USOCK_Pending	?	TEXT("Pending")
		:	State==USOCK_Open		?	TEXT("Open")
		:	State==USOCK_Closed		?	TEXT("Closed")
		:								TEXT("Invalid")
	);
	unguard;
}

IMPLEMENT_CLASS(UWebRTCConnection);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
