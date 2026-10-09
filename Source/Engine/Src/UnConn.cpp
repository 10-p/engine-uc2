/*=============================================================================
	UnConn.h: Unreal connection base class.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "EnginePrivate.h"
#include "UnNet.h"

#if DO_ENABLE_NET_TEST
// uc2 port: the network tests' deterministic packet drop (Engine/Src/UC2NetTest.cpp).
ENGINE_API UBOOL UC2NetTestDropPacket( UNetConnection* Connection );
ENGINE_API void UC2NetTestNoteText( UNetConnection* Connection, const TCHAR* Text, UBOOL bSending );
#endif

// uc2 port: packet ids are _WORDs that wrap modulo MAX_PACKETID-1 (FlushNet), so "later than" must be decided
// modulo that range, as UT2004 does with MakeRelative on INT ids. Signed distance from B to A.
#if DO_ENABLE_NET_TEST
// The network tests' compatibility test key [UC2NetTest] OldAcks=True (-ini:; was NETOLDACKS): this process keeps
// Epic's UC2 ack logic (ids from 0 with OutAckPacketId 0, plain comparisons), to run a fixed and an unfixed end
// against each other.
static INT GUC2OldAcks = -1;
static UBOOL UC2OldAcks()
{
	if( GUC2OldAcks<0 )
		GUC2OldAcks = appReadTestFlag( TEXT("OldAcks"), TEXT("UC2NetTest") ) ? 1 : 0;
	return GUC2OldAcks;
}
// [UC2NetTest] PacketIdStart=<n> (-ini:; was NETPKTID=): every connection of this process starts its packet ids at n.
// Read once (test keys are one-shot), kept for every later connection.
static INT GUC2PacketIdStart = -2;
static UBOOL UC2PacketIdStart( INT& StartId )
{
	if( GUC2PacketIdStart == -2 )
	{
		FString V;
		GUC2PacketIdStart = appReadTestKey( TEXT("PacketIdStart"), V, TEXT("UC2NetTest") ) && V.Len() ? Max( 0, appAtoi(*V) ) : -1;
	}
	StartId = GUC2PacketIdStart;
	return GUC2PacketIdStart >= 0;
}
#else
static UBOOL UC2OldAcks() { return 0; }
#endif
static inline INT PacketIdDelta( INT A, INT B )
{
	if( UC2OldAcks() )
		return A - B;
	const INT M = MAX_PACKETID - 1;
	INT D = ((A - B) % M + M) % M;
	return D > M/2 ? D - M : D;
}


/*-----------------------------------------------------------------------------
	UNetConnection implementation.
-----------------------------------------------------------------------------*/

UNetConnection::UNetConnection()
:	Out					( 0 )
{}
UNetConnection::UNetConnection( UNetDriver* InDriver, const FURL& InURL )
:	Driver				( InDriver )
,	Out					( 0 )
,	State				( USOCK_Invalid )
,	ProtocolVersion		( MIN_PROTOCOL_VERSION )
,	MaxPacket			( 0 )
,	LastReceiveTime		( Driver->Time )
,	LastSendTime		( Driver->Time )
,	LastTickTime		( Driver->Time )
,	StatUpdateTime		( Driver->Time )
,	LastRepTime			( 0.f )
,	QueuedBytes			( 0 )
,	URL					( InURL )
,	StatPeriod          ( 1.f  )
,	OutAckPacketId		( MAX_PACKETID - 2 )	// uc2 port: "the id before 0" (UT2004: -1), so packet 0 can be NAKed
,	InPacketId			( MAX_PACKETID - 2 )
,	LagAcc				( 9999 )
,	BestLagAcc			( 9999 )
,	BestLag				( 9999 )
,	AvgLag				( 9999 )
,	NegotiatedVer		( ENGINE_NEGOTIATION_VERSION )
,	bTryLastMatch		( 0 )
{
	guard(UNetConnection::UNetConnection);

	// Command-line parameters.
#if DO_ENABLE_NET_TEST
	Parse(appCmdLine(),TEXT("PktLoss="), PktLoss);
	Parse(appCmdLine(),TEXT("PktOrder="),PktOrder);
	Parse(appCmdLine(),TEXT("PktDup="),  PktDup);
	Parse(appCmdLine(),TEXT("PktLag="),  PktLag);
	// uc2 port: the network tests' wrap test: [UC2NetTest] PacketIdStart=<n> starts this side's packet ids at n, so a short test
	// crosses the packet-id wrap (16382 -> 0) that a real connection reaches after ~16k packets (~4.5 min at 60/s).
	{
		INT StartId = 0;
		if( UC2OldAcks() )
			OutAckPacketId = InPacketId = 0;
		if( UC2PacketIdStart( StartId ) )
		{
			// As on a connection that has been running: everything up to the previous id counts as acked.
			OutPacketId    = (_WORD)(StartId % (MAX_PACKETID - 1));
			OutAckPacketId = (_WORD)((StartId + MAX_PACKETID - 2) % (MAX_PACKETID - 1));
		}
	}
#endif

	// Other parameters.
	CurrentNetSpeed = URL.HasOption(TEXT("LAN")) ? GetDefault<UPlayer>()->ConfiguredLanSpeed : GetDefault<UPlayer>()->ConfiguredInternetSpeed;

	if ( CurrentNetSpeed == 0 )
		CurrentNetSpeed = 2600;
	else
		CurrentNetSpeed = ::Max(CurrentNetSpeed, 1800);

	// Create package map.
	PackageMap = new(this,NAME_None,0)UPackageMapLevel(this);

	unguard;
}
void UNetConnection::Serialize( FArchive& Ar )
{
	guard(UNetConnection::Serialize);

	Super::Serialize( Ar );
	Ar << PackageMap;
	for( INT i=0; i<MAX_CHANNELS; i++ )
		Ar << Channels[i];
	Ar << Download;

	unguard;
}
void UNetConnection::Destroy()
{
	guard(UNetConnection::Destroy);

	// Log.
	debugf( NAME_NetComeGo, TEXT("Close %s %s %s"), GetName(), *LowLevelGetRemoteAddress(), appTimestamp() );

	// scion sz -
	// from UC1. Let the parent connection handle closing 
	// the actual connection if we are a child connection
	if( IsA(UChildConnection::StaticClass()) )
	{
		return Super::Destroy();
	}
	
	// scion sz -
	// from UC1. Remove UChildConnection(s)
	for( INT i=0; i < Children.Num(); i++ )
	{
		delete Children(i);
	}
	Children.Empty();

	// Close the control channel.
	if( Channels[0] )
	{
		Channels[0]->Close();
	}
	FlushNet();

	// Remove from driver.
	if( Driver->ServerConnection )
	{
		check(Driver->ServerConnection==this);
		Driver->ServerConnection=NULL;
	}
	else
	{
		check(Driver->ServerConnection==NULL);
		verify(Driver->ClientConnections.RemoveItem( this )==1);
	}

	// Set to closed so the channels don't try to send data.
	State = USOCK_Closed;

	// Kill all channels.
	// uc2 port: during garbage collection only destroy them; the collector deletes them afterwards (they are
	// unreachable with this connection). The collector may have reached a channel first: its Destroy then
	// routes here through UChannel::RouteDestroy, and deleting it here freed the very object whose Destroy and
	// ConditionalDestroy were still running up the stack (they read its flags on return). Epic's allocator left
	// freed memory readable; glibc's does not ("<channel> failed to route Destroy" at exit, AddressSanitizer:
	// heap-use-after-free in UObject::ConditionalDestroy, freed by ~UActorChannel from here).
	for( INT i=OpenChannels.Num()-1; i>=0; i-- )
	{
		if( GIsGarbageCollecting )
			OpenChannels(i)->ConditionalDestroy();
		else
			delete OpenChannels(i);
	}

	// Kill package map.
	delete PackageMap;

	// Kill download object.
	if( Download )
		delete Download;

	Super::Destroy();
	unguard;
}
UBOOL UNetConnection::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UNetConnection::Exec);
	if( ParseCommand(&Cmd,TEXT("GETPING")) )
	{
		Ar.Logf( TEXT(" %i"), ::Max(5,(INT)(BestLag*1000.f)) );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("GETLOSS")) )
	{
		Ar.Logf( TEXT(" %i"), (INT) InLoss );
		return 1;
	}
	else if( ParseCommand( &Cmd, TEXT("GETPACKETID") ) )
	{
		Ar.Logf( TEXT("InPacketId: %i OutPacketId: %i OutAckPacketId: %i"), (INT) InPacketId, (INT)OutPacketId, (INT)OutAckPacketId );
	}
	else if( UPlayer::Exec(Cmd,Ar) )
	{
		return 1;
	}
	return 0;
	unguard;
}
void UNetConnection::AssertValid()
{
	guard(UNetConnection::AssertValid);

	// Make sure this connection is in a reasonable state.
	check(ProtocolVersion>=MIN_PROTOCOL_VERSION);
	check(ProtocolVersion<=MAX_PROTOCOL_VERSION);
	check(State==USOCK_Closed || State==USOCK_Pending || State==USOCK_Open);

	unguard;
}
void UNetConnection::SendPackageMap()
{
	guard(UNetConnection::SendPackageMap);

	// Send package map to the remote.
	for( TArray<FPackageInfo>::TIterator It(*PackageMap->List); It; ++It )
	{
		FString Filename = *It->URL;
		INT i;
		while( (i = Filename.InStr( PATH_SEPARATOR )) != -1 )
			Filename = Filename.Mid( i+1 );
		while( (i = Filename.InStr( TEXT(":") )) != -1 )
			Filename = Filename.Mid( i+1 );
		while( (i = Filename.InStr( TEXT("/") )) != -1 )		//!! ini doesn't use same format as PATH_SEPARATOR!
			Filename = Filename.Mid( i+1 );
		while( (i = Filename.InStr( TEXT("\\") )) != -1 )
			Filename = Filename.Mid( i+1 );
		Logf
		(
			TEXT("USES GUID=%s PKG=%s FLAGS=%i SIZE=%i GEN=%i FNAME=%s"),
			It->Guid.String(),
			It->Parent->GetName(),
			It->PackageFlags,
			It->FileSize,
			It->LocalGeneration,
			*Filename
		);
	}
	for( INT i=0; i<Driver->DownloadManagers.Num(); i++ )
	{
		UClass* DownloadClass = StaticLoadClass( UDownload::StaticClass(), NULL, *Driver->DownloadManagers(i), NULL, LOAD_NoWarn, NULL );
		if( DownloadClass )
		{
			FString Params = *CastChecked<UDownload>(DownloadClass->GetDefaultObject())->DownloadParams;
			UBOOL Compression = CastChecked<UDownload>(DownloadClass->GetDefaultObject())->UseCompression;
			if( *(*Params) )
			{
				Logf
				(
					TEXT("DLMGR CLASS=%s PARAMS=%s COMPRESSION=%d"),
					DownloadClass->GetPathName(),
					*Params,
					Compression
				);
			}
		}
	}
	unguard;
}
void UNetConnection::InitOut()
{
	guard(UDemoRecConnection::InitOut);

	// Initialize the one outgoing buffer.
	if (MaxPacket == Out.GetByteCount())
	{
		// Reset all of our values to their initial state without a malloc/free
		Out.Reset();
	}
	else
	{
		// First time initialization needs to allocate the buffer
		Out = FBitWriter(MaxPacket);
	}
	unguard;
}

void UNetConnection::ReceivedRawPacket( void* InData, INT Count )
{
	guard(UNetConnection::ReceivedRawPacket);
	BYTE* Data = (BYTE*)InData;

	// Handle an incoming raw packet from the driver.
	debugf( NAME_DevNetTraffic, TEXT("%03i: Received %i"), (INT)(appSeconds()*1000)%1000, Count );
	InByteAcc += Count + CalcPacketOverhead(Count);
	InPktAcc++;
	if( Count>0 )
	{
		BYTE LastByte = Data[Count-1];
		if( LastByte )
		{
			INT BitSize = Count*8-1;
			while( !(LastByte & 0x80) )
			{
				LastByte *= 2;
				BitSize--;
			}
			FBitReader Reader( Data, BitSize );
			ReceivedPacket( Reader );
		}
		else appErrorf( TEXT("Packet missing trailing 1") );
	}
	else appErrorf( TEXT("Received zero-size packet") );

	unguard;
}

/* epic ===============================================
* ::HasPendingData
* Author: rayd
*
* Returns whether or not we have data waiting to be sent.
*
* =====================================================
*/
UBOOL UNetConnection::HasPendingData()
{
	return (Out.GetNumBits()  || ((Driver->ServerConnection || bPassedChallenge)  && Driver->Time-LastSendTime>Driver->KeepAliveTime));
}

/**
 * Returns whether this connection is still preloading assets for the match.
 *
 * @return	TRUE if the connection is still in the loading phase of the connection process.
 *
 */
UBOOL UNetConnection::IsLoadingAssets() const
{
	guard(UNetConnection::IsLoadingAssets);

	return bPassedChallenge && (State == USOCK_Pending || Actor == NULL);

	unguard;
}

void UNetConnection::FlushNet()
{
	guard(UNetConnection::FlushNet);

	// Update info.
	check(!Out.IsError());
	LastEnd = FBitWriterMark();
	bTimeSensitive = false;

	// If there is any pending data to send, send it.
	//epic rwd - changed to use HasPendingData(), so that voice can be sent w/o a waiting game packet
	if ( HasPendingData() )
	{
		// If sending keepalive packet, still generate header.
		if( Out.GetNumBits()==0 )
			PreSend( 0 );

		// Make sure packet size is byte-aligned.
		Out.WriteBit( 1 );
		check(!Out.IsError());
		while( Out.GetNumBits() & 7 )
			Out.WriteBit( 0 );
		check(!Out.IsError());

		// Send now.
#if DO_ENABLE_NET_TEST
		// uc2 port: the network tests' deterministic packet drop ([UC2NetTest] Drop=, UC2NetTest.cpp): the packet is lost
		// as if on the wire; everything below (packet id, lag bookkeeping, LastSendTime) still happens.
		if( UC2NetTestDropPacket( this ) )
		{
		}
		else if( PktOrder )
		{
			DelayedPacket& B = *(new(Delayed)DelayedPacket);
			B.Data.Add( Out.GetNumBytes() );
			appMemcpy( &B.Data(0), Out.GetData(), Out.GetNumBytes() );

			for( INT i=Delayed.Num()-1; i>=0; i-- )
			{
				if( appFrand()>0.50 )
				{
					if( !PktLoss || appFrand()*100.f>PktLoss )
						LowLevelSend( (char*)&Delayed(i).Data(0), Delayed(i).Data.Num() );
					Delayed.Remove( i );
				}
			}
		}
		else if( PktLag )
		{
			if( !PktLoss || appFrand()*100.f>PktLoss )
			{
				DelayedPacket& B = *(new(Delayed)DelayedPacket);
				B.Data.Add( Out.GetNumBytes() );
				appMemcpy( &B.Data(0), Out.GetData(), Out.GetNumBytes() );
				B.SendTime = appSeconds() + DOUBLE(PktLag) / 1000.f;
			}
		}
		else if( !PktLoss || appFrand()*100.f>=PktLoss )
		{
#endif
			LowLevelSend( Out.GetData(), Out.GetNumBytes() );
#if DO_ENABLE_NET_TEST
			if( PktDup && appFrand()*100.f<PktDup )
				LowLevelSend( (char*)Out.GetData(), Out.GetNumBytes() );
		}
#endif

		// Update stuff.
		INT Index = OutPacketId & (ARRAY_COUNT(OutLagPacketId)-1);
		OutLagPacketId [Index] = OutPacketId;
		OutLagTime     [Index] = Driver->Time;
//		OutPacketId++;
		//epic superville: Wrap packet id properly
		// Zero reserved for invalid id
		OutPacketId = (OutPacketId + 1) % (MAX_PACKETID - 1);
		
		OutPktAcc++;
		LastSendTime = Driver->Time;
		QueuedBytes += Out.GetNumBytes() + CalcPacketOverhead(Out.GetNumBytes());
		OutByteAcc  += Out.GetNumBytes() + CalcPacketOverhead(Out.GetNumBytes());
		InitOut();
	}

	// Move acks around.
	for( INT i=0; i<QueuedAcks.Num(); i++ )
		ResendAcks.AddItem(QueuedAcks(i));
	QueuedAcks.Empty(32);

	unguard;
}
void UNetConnection::Serialize( const TCHAR* Data, EName MsgType )
{
	guard(UNetConnection::Serialize);

	debugf(NAME_DevNet, TEXT("NetConnection sending text: %s"), Data);
#if DO_ENABLE_NET_TEST
	UC2NetTestNoteText( this, Data, 1 );	// uc2 port: [UC2NetTest] Drop= triggers (UC2NetTest.cpp)
#endif

	// Send data to the control channel.
	if( Channels[0] && !Channels[0]->bClosing )
		((UControlChannel*)Channels[0])->Serialize( Data, MsgType );

	unguard;
}
INT UNetConnection::IsNetReady( UBOOL Saturate )
{
	guard(UNetConnection::IsReady);

	// Return whether we can send more data without saturation the connection.
	if( Saturate )
		QueuedBytes = -Out.GetNumBytes();
	return QueuedBytes+Out.GetNumBytes() <= 0;

	unguard;
}
void UNetConnection::ReadInput( FLOAT DeltaSeconds )
{}
IMPLEMENT_CLASS(UNetConnection);

/*-----------------------------------------------------------------------------
	Packet reception.
-----------------------------------------------------------------------------*/

//
// Packet was negatively acknowledged.
//
void UNetConnection::ReceivedNak( INT NakPacketId )
{
	guard(UNetConnection::ReceivedNak);

	// Make note of the nak.
	for( INT i=OpenChannels.Num()-1; i>=0; i-- )
	{
		UChannel* Channel = OpenChannels(i);
		Channel->ReceivedNak( NakPacketId );
		if( Channel->OpenPacketId==NakPacketId )
			Channel->ReceivedAcks(); //warning: May destroy Channel.
	}
	unguard;
}

//
// Handle a packet we just received.
//
void UNetConnection::ReceivedPacket( FBitReader& Reader )
{
	guard(UNetConnection::ReceivedPacket);
	AssertValid();

	// Handle PacketId.
	if( Reader.IsError() )
	{
		appErrorf( TEXT("Packet too small") );
		return;
	}

	// Update receive time to avoid timeout.
	LastReceiveTime = Driver->Time;

	// Check packet ordering.
	INT PacketId = Reader.ReadInt(MAX_PACKETID);;
	INT PacketDelta = PacketIdDelta( PacketId, InPacketId );	// uc2 port: wrap-aware
	if( PacketDelta > 0 )
	{
		InLossAcc += PacketDelta - 1;
		InPacketId = PacketId;
	}
	else 
	{
		InOrdAcc++;
	}

	// Disassemble and dispatch all bunches in the packet.
	while( !Reader.AtEnd() && State!=USOCK_Closed )
	{
		// Parse the bunch.
		INT StartPos = Reader.GetPosBits();
		UBOOL IsAck = Reader.ReadBit();
		if( Reader.IsError() )
		{
			appErrorf( TEXT("Bunch missing ack flag") );
			SendAck(PacketId);
			return;
		}

		// Process the bunch.
		if( IsAck )
		{
			// This is an acknowledgement.
			_WORD AckPacketId = Reader.ReadInt(MAX_PACKETID);
			if( Reader.IsError() )
			{
				appErrorf( TEXT("Bunch missing ack") );
				SendAck(PacketId);
				return;
			}

			// Resend any old reliable packets that the receiver hasn't acknowledged.
			guard(ReceivedVirtualNak);
			// uc2 port: wrap-aware (PacketIdDelta); the plain '>' stopped all NAKs after the first packet-id wrap.
			INT AckDelta = PacketIdDelta( AckPacketId, OutAckPacketId );
			if( AckDelta>0 )
			{
				for( INT k=1; k<AckDelta; k++,OutLossAcc++ )
				{
					INT NakPacketId = (OutAckPacketId + k) % (MAX_PACKETID - 1);
					debugf( NAME_DevNetTraffic, TEXT("   Received virtual nak %i (%.1f)"), NakPacketId, (Reader.GetPosBits()-StartPos)/8.f );
					ReceivedNak( NakPacketId );
				}
				OutAckPacketId = AckPacketId;
			}
			else if( AckDelta<0 )
			{
				//warning: Double-ack logic makes this unmeasurable.
				//OutOrdAcc++;
			}
			unguard;

			// Update lag.
			INT Index = AckPacketId & (ARRAY_COUNT(OutLagPacketId)-1);
			if( OutLagPacketId[Index]==AckPacketId )
			{
				FLOAT NewLag = Driver->Time - OutLagTime[Index] - (FrameTime/2.f);
				LagAcc += NewLag;
				LagCount++;
			}

			// Forward the ack to the channel.
			guard(ReceivedAck);
			debugf( NAME_DevNetTraffic, TEXT("   Received ack %i (%.1f)"), AckPacketId, (Reader.GetPosBits()-StartPos)/8.f );
			for( INT i=OpenChannels.Num()-1; i>=0; i-- )
			{
				UChannel* Channel = OpenChannels(i);
				for( FOutBunch* Out=Channel->OutRec; Out; Out=Out->Next )
				{
					if( Out->GetPacketID()==AckPacketId )
					{
						Out->SetbReceivedAck( 1 );
						if( Out->GetbOpen() )
							Channel->bOpenAcked = true;
					}
				}
				if( Channel->OpenPacketId==AckPacketId ) // Necessary for unreliable "bNetTemporary" channels.
					Channel->bOpenAcked = true;
				Channel->ReceivedAcks(); //warning: May destroy Channel.
			}
			unguard;
		}
		else
		{
			// Parse the incoming data.
			FInBunch Bunch( this );
			guard(BunchHeader);
			INT StartPos       = Reader.GetPosBits();
			BYTE bControl      = Reader.ReadBit();
			Bunch.SetPacketID( PacketId );
			Bunch.SetbOpen(  bControl ? Reader.ReadBit() : 0 );
			Bunch.SetbClose( bControl ? Reader.ReadBit() : 0 );
			Bunch.SetbReliable( Reader.ReadBit() );
			Bunch.SetChannelIndex( Reader.ReadInt( MAX_CHANNELS ) );
			Bunch.ChSequence   = Bunch.GetbReliable() ? MakeRelative(Reader.ReadInt(MAX_CHSEQUENCE),InReliable[Bunch.GetChannelIndex()],MAX_CHSEQUENCE) : 0;
			Bunch.ChType       = (Bunch.GetbReliable()||Bunch.GetbOpen()) ? Reader.ReadInt(CHTYPE_MAX) : CHTYPE_None;
			INT BunchDataBits  = Reader.ReadInt( UNetConnection::MaxPacket*8 );
			INT HeaderPos      = Reader.GetPosBits();
			if( Reader.IsError() )
			{
				appErrorf( TEXT("Bunch header overflowed") );
				SendAck(PacketId);
				return;
			}
			Bunch.SetData( Reader, BunchDataBits );
			if( Reader.IsError() )
			{
				// Bunch claims it's larger than the enclosing packet.
				appErrorf( TEXT("Bunch data overflowed (%i %i+%i/%i)"), StartPos, HeaderPos, BunchDataBits, Reader.GetNumBits() );
				SendAck(PacketId);
				return;
			}
			if( Bunch.GetbReliable() )
				debugf( NAME_DevNetTraffic, TEXT("   Reliable Bunch, Channel %i Sequence %i: Size %.1f+%.1f"), Bunch.GetChannelIndex(), Bunch.ChSequence, (HeaderPos-StartPos)/8.f, (Reader.GetPosBits()-HeaderPos)/8.f );
			else
				debugf( NAME_DevNetTraffic, TEXT("   Unreliable Bunch, Channel %i: Size %.1f+%.1f"), Bunch.GetChannelIndex(), (HeaderPos-StartPos)/8.f, (Reader.GetPosBits()-HeaderPos)/8.f );
			unguard;

			// Can't handle other channels until control channel exists.
			if( !Channels[Bunch.GetChannelIndex()] && !Channels[0] && (Bunch.GetChannelIndex()!=0 || Bunch.ChType!=CHTYPE_Control) )
			{
				appErrorf( NAME_DevNetTraffic, TEXT("Received bunch before connected") );
				SendAck(PacketId);
				return;
			}

			// Receiving data.
			guard(BunchData);
			UChannel* Channel = Channels[Bunch.GetChannelIndex()];

			// Ignore if reliable packet has already been processed.
			if( Bunch.GetbReliable() && Bunch.ChSequence<=InReliable[Bunch.GetChannelIndex()] )
			{
				debugf( NAME_DevNetTraffic, TEXT("      Received outdated bunch (Current Sequence %i)"), InReliable[Bunch.GetChannelIndex()] );
				continue;
			}

			// If unreliable but not one-shot open+close "bNetTemporary" packet, discard it.
			if( !Bunch.GetbReliable() && (!Bunch.GetbOpen() || !Bunch.GetbClose()) && (!Channel || Channel->OpenPacketId==MAXWORD) )
			{
				debugf( NAME_DevNetTraffic, TEXT("      Received unreliable bunch before open (Current Sequence %i)"), InReliable[Bunch.GetChannelIndex()] );
				continue;
			}

			// Create channel if necessary.
			if( !Channel )
			{
				guard(CreateNewChannel);

				// Validate channel type.
				if( !UChannel::IsKnownChannelType(Bunch.ChType) )
				{
					// Unknown type.
					appErrorf( TEXT("Connection unknown channel type (%i)"), Bunch.ChType );
					SendAck(PacketId);
					return;
				}

				// Reliable (either open or later), so create new channel.
				debugf( NAME_DevNetTraffic, TEXT("      Bunch Create %i: ChType %i"), Bunch.GetChannelIndex(), Bunch.ChType );
				Channel = CreateChannel( (EChannelType)Bunch.ChType, 0, Bunch.GetChannelIndex() );

				// Notify the server of the new channel.
				if( !Driver->Notify->NotifyAcceptingChannel( Channel ) )
				{
					// Channel refused, so close it, flush it, and delete it.
					guard(NotifyAcceptingRefused);
					FOutBunch CloseBunch( Channel, 1 );
					check(!CloseBunch.IsError());
					check(CloseBunch.GetbClose());
					CloseBunch.SetbReliable( 1 );
					Channel->SendBunch( &CloseBunch, 0 );
					FlushNet();
					delete Channel;
					if( Bunch.GetChannelIndex()==0 )
					{
						debugf( NAME_DevNetTraffic, TEXT("Channel 0 create failed") );
						State = USOCK_Closed;
					}
					continue;
					unguard;
				}
				unguard;
			}
			if( Bunch.GetbOpen() )
			{
				Channel->bOpenAcked = true;
				Channel->OpenPacketId = PacketId;
			}

			// Dispatch the raw, unsequenced bunch to the channel.
			guard(DispatchDataToChannel);
			Channel->ReceivedRawBunch( Bunch ); //warning: May destroy channel.
			InBunAcc++;

			// Disconnect if we received a corrupted packet from the client (eg server crash attempt).
			if( !Driver->ServerConnection && Bunch.IsCriticalError() )
			{
				debugf( NAME_DevNet, TEXT("Received corrupted packet data from client %s.  Disconnecting."), *LowLevelGetRemoteAddress() );
				State = USOCK_Closed;
			}
			unguard;

			unguard;
		}
	}

	// Acknowledge the packet.
	//epic moelfke: moved this to acknowledge *AFTER* the data was processed
	//this fixes the bug where the client receives a packet containing both a close and RPCs,
	//the received RPCs send RPCs back to the server, then the client processes the close and deletes the channel
	//the server would then receive the ack first (since it was sent before processing the data), delete the channel,
	//then receive RPCs for a now-invalid channel, breaking it and causing whatever gets assigned to that channel next
	//to never reach the client
	SendAck(PacketId);

	unguard;
}

/*-----------------------------------------------------------------------------
	All raw sending functions.
-----------------------------------------------------------------------------*/

//
// Called before sending anything.
//
void UNetConnection::PreSend( INT SizeBits )
{
	guard(UNetConnection::PreSend);

	// Flush if not enough space.
	if( Out.GetNumBits() + SizeBits + MAX_PACKET_TRAILER_BITS > MaxPacket*8 )
		FlushNet();

	// If start of packet, send packet id.
	if( Out.GetNumBits()==0 )
	{
		Out.WriteInt( OutPacketId, MAX_PACKETID );
		check(Out.GetNumBits()<=MAX_PACKET_HEADER_BITS);
	}

	// Make sure there's enough space now.
	if( Out.GetNumBits() + SizeBits + MAX_PACKET_TRAILER_BITS > MaxPacket*8 )
		appErrorf( TEXT("PreSend overflowed: %i+%i>%i"), Out.GetNumBits(), SizeBits, MaxPacket*8 );

	unguard;
}

//
// Called after sending anything.
//
void UNetConnection::PostSend()
{
	guard(UNetConnection::PostSend);

	// If absolutely filled now, flush so that MaxSend() doesn't return zero unnecessarily.
	check(Out.GetNumBits()<=MaxPacket*8);
	if( Out.GetNumBits()==MaxPacket*8 )
		FlushNet();

	unguard;
}

//
// Resend any pending acks.
//
void UNetConnection::PurgeAcks()
{
	guard(UNetConnection::PurgeAcks);
	for( INT i=0; i<ResendAcks.Num(); i++ )
		SendAck( ResendAcks(i), 0 );
	ResendAcks.Empty(32);
	unguard;
}

//
// Send an acknowledgement.
//
void UNetConnection::SendAck( INT AckPacketId, UBOOL FirstTime )
{
	guard(UNetConnection::SendAck);
	if( !bInternalAck )
	{
		if( FirstTime )
		{
			PurgeAcks();
			QueuedAcks.AddItem(AckPacketId);
		}
		PreSend( appCeilLogTwo(MAX_PACKETID)+1 );
		Out.WriteBit( 1 );
		Out.WriteInt( AckPacketId, MAX_PACKETID );
		bAllowMerge = false;
		PostSend();
	}
	unguard;
}

//
// Send a raw bunch.
//
INT UNetConnection::SendRawBunch( FOutBunch& Bunch, UBOOL InAllowMerge )
{
	guard(UNetConnection::SendRawBunch);
	check(!Bunch.GetbReceivedAck());
	check(!Bunch.IsError());
	OutBunAcc++;
	bTimeSensitive = true;

	// Build header.
	FBitWriter Header( MAX_BUNCH_HEADER_BITS );
	Header.WriteBit( 0 );
	Header.WriteBit( Bunch.GetbOpen() || Bunch.GetbClose() );
	if( Bunch.GetbOpen() || Bunch.GetbClose() )
	{
		Header.WriteBit( Bunch.GetbOpen()  );
		Header.WriteBit( Bunch.GetbClose() );
	}
	Header.WriteBit( Bunch.GetbReliable() );
	Header.WriteInt( Bunch.GetChannelIndex(), MAX_CHANNELS );
	if( Bunch.GetbReliable() )
		Header.WriteInt( Bunch.ChSequence, MAX_CHSEQUENCE );
	if( Bunch.GetbReliable() || Bunch.GetbOpen() )
		Header.WriteInt( Bunch.ChType, CHTYPE_MAX );
	Header.WriteInt( Bunch.GetNumBits(), UNetConnection::MaxPacket*8 );
	check(!Header.IsError());

	// If this data doesn't fit in the current packet, flush it.
	PreSend( Header.GetNumBits() + Bunch.GetNumBits() );

	// Remember start position.
	check(InAllowMerge==0||InAllowMerge==1);
	bAllowMerge     = InAllowMerge;
	Bunch.SetPacketID( OutPacketId );
	Bunch.Time      = Driver->Time;

	// Remember start position, and write data.
	LastStart = FBitWriterMark( Out );
	Out.SerializeBits( Header.GetData(), Header.GetNumBits() );
	Out.SerializeBits( Bunch .GetData(), Bunch .GetNumBits() );

	// Finished.
	PostSend();

	return Bunch.GetPacketID();
	unguard;
}

/*-----------------------------------------------------------------------------
	Channel creation.
-----------------------------------------------------------------------------*/

//
// Create a channel.
//
UChannel* UNetConnection::CreateChannel( EChannelType ChType, UBOOL bOpenedLocally, INT ChIndex )
{
	guard(UNetConnection::CreateChannel);
	check(UChannel::IsKnownChannelType(ChType));
	AssertValid();

	// If no channel index was specified, find the first available.
	if( ChIndex==INDEX_NONE )
	{
		INT FirstChannel = 1;
		if ( ChType == CHTYPE_Control )
			FirstChannel = 0;
		for( ChIndex=FirstChannel; ChIndex<MAX_CHANNELS; ChIndex++ )
			if( !Channels[ChIndex] )
				break;
		if( ChIndex==MAX_CHANNELS )
			return NULL;
	}

	// Make sure channel is valid.
	check(ChIndex<MAX_CHANNELS);
	check(Channels[ChIndex]==NULL);

	// Create channel.
	UChannel* Channel = ConstructObject<UChannel>( UChannel::ChannelClasses[ChType] );
	Channel->Init( this, ChIndex, bOpenedLocally );
	Channels[ChIndex] = Channel;
	OpenChannels.AddItem(Channel);
	//debugf( "Created channel %i of type %i", ChIndex, ChType);

	return Channel;
	unguard;
}

/*-----------------------------------------------------------------------------
	Connection polling.
-----------------------------------------------------------------------------*/

//
// Poll the connection.
// If it is timed out, close it.
//
void UNetConnection::Tick()
{
	guard(UNetConnection::Tick);
	AssertValid();

	// Lag simulation.
#if DO_ENABLE_NET_TEST
	if( PktLag )
	{
		for( INT i=0; i < Delayed.Num(); i++ )
		{
			if( appSeconds() > Delayed(i).SendTime )
			{
				LowLevelSend( (char*)&Delayed(i).Data(0), Delayed(i).Data.Num() );
				Delayed.Remove( i );
				i--;
			}
		}
	}
#endif

	// Get frame time.
	DOUBLE CurrentTime = appSeconds();
	FrameTime = CurrentTime - LastTime;
	LastTime = CurrentTime;
	CumulativeTime += FrameTime;
	CountedFrames++;
	if(CumulativeTime > 1.f)
	{
		AverageFrameTime = CumulativeTime / CountedFrames;
		CumulativeTime = 0;
		CountedFrames = 0;
	}

	// Pretend everything was acked, for 100% reliable connections or demo recording.
	if( bInternalAck )
	{
		LastReceiveTime = Driver->Time;
		for( INT i=OpenChannels.Num()-1; i>=0; i-- )
		{
			UChannel* It = OpenChannels(i);
			for( FOutBunch* Out=It->OutRec; Out; Out=Out->Next )
			{
				Out->SetbReceivedAck( 1 );
			}
			It->bOpenAcked = true;
			It->ReceivedAcks();
		}
	}

	// Update stats.
	if( Driver->Time-StatUpdateTime>StatPeriod )
	{
		// Update stats.
		guard(UpdateStats);
		FLOAT RealTime	= Driver->Time - StatUpdateTime;
		InRate			= InByteAcc  / RealTime;
		OutRate			= OutByteAcc / RealTime;
		InPackets		= InPktAcc   / RealTime;
		OutPackets		= OutPktAcc  / RealTime;
		InBunches		= InBunAcc   / RealTime;
		OutBunches		= OutBunAcc  / RealTime;
		InOrder         = InOrdAcc   / RealTime;
		OutOrder        = OutOrdAcc  / RealTime;
		OutLoss         = 100.f * OutLossAcc / Max(OutPackets,1.f);
		InLoss          = 100.f * InLossAcc  / Max(InPackets+InLossAcc,1.f);
		if( LagCount )
			AvgLag = LagAcc/LagCount;
		BestLag = AvgLag;

		// See if we're experiencing high packet loss.
		if( OutLoss>20 || InLoss>20 )
			HighLossCount++;
		else
			HighLossCount=0;

		if( Actor )
		{
			FLOAT PktLoss = ::Max(InLoss, OutLoss) * 0.01f;
			FLOAT ModifiedLag = BestLag + 1.2 * PktLoss;
			if ( Actor->myHUD )
			Actor->myHUD->bBadConnectionAlert = !bInternalAck && ((ModifiedLag>0.8 || CurrentNetSpeed * (1 - PktLoss)<2000) && ActorChannels.FindRef(Actor)) || InPackets < 2;
		}

		// Init counters.
		LagAcc			= 0;
		BestLagAcc		= 9999;
		InByteAcc		= 0;
		OutByteAcc		= 0;
		InPktAcc		= 0;
		OutPktAcc		= 0;
		InBunAcc		= 0;
		OutBunAcc		= 0;
		InLossAcc       = 0;
		OutLossAcc      = 0;
		InOrdAcc        = 0;
		OutOrdAcc       = 0;
		LagCount        = 0;
		StatUpdateTime	= Driver->Time;
		unguard;
	}

	// Compute time passed since last update.
	FLOAT DeltaTime     = Driver->Time - LastTickTime;
	LastTickTime        = Driver->Time;

	// Update queued byte count.
	FLOAT DeltaBytes = CurrentNetSpeed * DeltaTime;
	QueuedBytes     -= (INT) DeltaBytes;
	FLOAT AllowedLag = 2.f * DeltaBytes;
	if( QueuedBytes < -AllowedLag )
		QueuedBytes = (INT) -AllowedLag;

	/* bShortConnectTimeOut is set by a replicated function call from the client,
	   after we know that the level has been loaded and completely precached.
	   Steve Polge - UDN Mailing list
	*/
	// Handle timeouts.
	FLOAT Timeout = Driver->InitialConnectTimeout;
	if ( (State!=USOCK_Pending) && Actor && (Actor->bPendingDestroy || Actor->bShortConnectTimeOut) )
		Timeout = Actor->bPendingDestroy ? 2.f : Driver->ConnectionTimeout;
	FLOAT sinceLastPacket = Driver->Time - LastReceiveTime;
	if( sinceLastPacket > Timeout )
	{
		// Timeout.
		if( State != USOCK_Closed )
			debugf( NAME_DevNet, TEXT("Connection timed out after %f seconds (%f)"), Timeout, Driver->Time - LastReceiveTime );
		State = USOCK_Closed;
#ifdef DEBUG_SERVERTRAVEL
		debugf(TEXT("%s Closing socket"), GetName());
#endif
	}
	else
	{
		// Tick the channels.
		for( INT i=OpenChannels.Num()-1; i>=0; i-- )
			OpenChannels(i)->Tick();

		// If channel 0 has closed, mark the conection as closed.
		if( Channels[0]==NULL && (OutReliable[0]!=0 || InReliable[0]!=0) )
			State = USOCK_Closed;
	}

//scion bcl -- Championship has some intercept code at this point to handle "retrying previous match"; 
#ifdef _XBOX
	if ( Driver->ServerConnection && !bTryLastMatch )
	{
		if( State==USOCK_Pending || !Actor )
		{
	#ifdef DEBUG_SERVERTRAVEL
			debugf(TEXT("%s USOCK_Pending:%s  Actor:%s  bTryLastMatch:%s  Driver->ServerConnection:%s   sinceLastPacket:%f"),
				GetName(),
				State==USOCK_Pending ? TEXT("True") : TEXT("False"),
				Actor != NULL ? TEXT("True") : TEXT("False"),
				bTryLastMatch ? TEXT("True") : TEXT("False"),
				Driver->ServerConnection != NULL ? Driver->ServerConnection->GetName() : TEXT("NULL"),
				sinceLastPacket);
	#endif
			if( sinceLastPacket > 15.0f )
			{
				bTryLastMatch = true;
				Driver->Notify->NotifyReceivedText( this, TEXT("TRY_LAST_MATCH"));
			}
		}
	}
#endif

	// Flush.
	PurgeAcks();
	if( bTimeSensitive || Driver->Time-LastSendTime>Driver->KeepAliveTime )
		FlushNet();

	guard(TickDownload);
	if( Download )
		Download->Tick();
	unguard;

	unguard;
}

/* scion ======================================================================
* UChildConnection::Tick
* Author: sz (from UC1)
* 
* All channels and actual data transfers are handled in the parent 
* UNetConnection::Tick
* =============================================================================
*/
void UChildConnection::Tick()
{
	State = Parent->State;
}

/* scion ======================================================================
* UChildConnection::HandleClientPlayer
* Author: sz (from UC1)
* 
* After a new player controller is created on the client we can setup
* the splitscreen viewport for it.  Assumes that the player controller has a 
* valid gamepad id.
* =============================================================================
*/
void UChildConnection::HandleClientPlayer( APlayerController *PC )
{
	guard(UChildConnection::HandleClientPlayer);
	check(PC);	

	// get client and splitscreen manager ptrs
	ULevel* Level = PC->GetLevel();	check(Level);
	UEngine* Engine = Level->Engine; check(Engine);
	UClient* Client = Engine->Client; check(Client);
	USplitScreenMgr* SplitScreenMgr = Client->GetSplitScreenMgr();	check(SplitScreenMgr);

	PC->Role        = ROLE_AutonomousProxy;
	PC->ShowFlags   = SHOW_GameDefault;
	PC->RendMap	    = REN_DynLight;
	PC->Level->LevelAction = LEVACT_None;

	debugf( NAME_DevSplitScreen, TEXT("Received child client player [%s].NetGamepadID=%d"), 
		PC->GetName(), 
		PC->NetGamepadID );

	// try to find an existing viewport that matches this player's GamepadID
	UViewport* SplitViewMatch = NULL;
	for( INT SplitIdx=0; SplitIdx < SplitScreenMgr->NumActiveSplitViews(); SplitIdx++ )
	{
		UViewport* SplitView = SplitScreenMgr->GetSplitView(SplitIdx);
		if( SplitView && 
			SplitView->GamepadID == PC->NetGamepadID )
		{
			SplitViewMatch = SplitView;
			break;
		}
	}

	// remove the temporary player attached to the viewport
	if( SplitViewMatch )
	{
		debugf( NAME_DevSplitScreen, TEXT("Found matching viewport [%s] for player [%s].NetGamepadID=%d"),
			SplitViewMatch->GetName(),
			PC->GetName(), 
			PC->NetGamepadID );

		if( SplitViewMatch->Actor )
		{
			// remove the dummy actor that was created for this viewport        
			if( !Level->DestroyActor( SplitViewMatch->Actor ) )
			{
				debugf( NAME_DevSplitScreen, TEXT("Could not remove dummy actor for viewport=%s"), SplitViewMatch->GetName() );
			}
			SplitViewMatch->Actor = NULL;
		}		
        
        // set the new actor for this viewport to the player we received
		PC->SetPlayer( SplitViewMatch );
	}
	// bind this player to a new splitscreen viewport if it doesn't already have
	// a matching viewport
	else
	{
		// create a new viewport
		SplitViewMatch = SplitScreenMgr->CreateSplitViewport();
		check(SplitViewMatch);
		// set the new actor for this viewport to the player we received
		PC->SetPlayer( SplitViewMatch );
	}

	// set new gamepad id
	SplitViewMatch->SetGamepadID( PC->NetGamepadID );

	unguard;
}

IMPLEMENT_CLASS(UChildConnection);

/*---------------------------------------------------------------------------------------
	Client Player Connection.
---------------------------------------------------------------------------------------*/

void UNetConnection::HandleClientPlayer( APlayerController *PC )
{
	guard(UNetConnection::HandleClientPlayer);

	// Hook up the Viewport to the new player actor.
	UViewport* Viewport = NULL;
	check(PC->GetLevel()->Engine->Client);
	check(PC->GetLevel()->Engine->Client->Viewports.Num());
	Viewport = PC->GetLevel()->Engine->Client->Viewports(0);
	Viewport->Actor->Player = NULL;
	Viewport->CurrentNetSpeed = CurrentNetSpeed;

	// Init the new playerpawn.
	guard(InitialPlayerPawn);
	PC->Role        = ROLE_AutonomousProxy;
	PC->ShowFlags   = SHOW_GameDefault;
	PC->RendMap	  = REN_DynLight;
	PC->SetPlayer( Viewport );
	debugf(TEXT("%s setplayer %s"),PC->GetName(), Viewport->GetName());
	PC->GetLevel()->Engine->Client->Viewports(0)->Input->ResetInput();
#ifndef _XBOX
	PC->GetLevel()->Engine->Flush( 1 );
#endif
	PC->Level->LevelAction = LEVACT_None;
	unguard;

	// Mark this connection as open.
	check(State==USOCK_Pending);
	State = USOCK_Open;
	Actor = PC;

	// scion sz - let splitscreen manager know that the default viewport
	// has received its replicated actor
	UClient* Client = PC->GetLevel()->Engine->Client;	check(Client);
	USplitScreenMgr* SplitScreenMgr = Client->GetSplitScreenMgr();
	if( SplitScreenMgr )
	{
		SplitScreenMgr->NotifyReceivedClientPlayer();
	}	

	unguard;
}

/*---------------------------------------------------------------------------------------
	File transfer.
---------------------------------------------------------------------------------------*/
//
// Initiate downloading a file to the cache directory.
// The transfer will eventually succeed or fail, and the
// NotifyReceivedFile will be called with the results.
//
void UNetConnection::ReceiveFile( INT PackageIndex )
{
	guard(UNetConnection::ReceiveFile);
	check(PackageMap->List->IsValidIndex(PackageIndex));
	if( DownloadInfo.Num() == 0 )
	{
		DownloadInfo.AddZeroed();
		DownloadInfo(0).Class = UChannelDownload::StaticClass();
		DownloadInfo(0).ClassName = TEXT("Engine.UChannelDownload");
		DownloadInfo(0).Params = TEXT("");
		DownloadInfo(0).Compression = 0;
	}
	if( Download )
		delete Download;
	Download = ConstructObject<UDownload>( DownloadInfo(0).Class );	
	Download->ReceiveFile( this, PackageIndex, *DownloadInfo(0).Params, DownloadInfo(0).Compression );
	unguard;
}

INT UNetConnection::CalcPacketOverhead(INT PacketSize)
{
	return PacketOverhead;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

