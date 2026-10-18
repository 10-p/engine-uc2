/*=============================================================================
	UnChan.cpp: Unreal datachannel implementation.
	Copyright 1997-2003 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "EnginePrivate.h"
#include "UnNet.h"

#if DO_ENABLE_NET_TEST
ENGINE_API void UC2NetTestNoteText( UNetConnection* Connection, const TCHAR* Text, UBOOL bSending );	// UC2NetTest.cpp
#endif
#include "ULoadSaveManager.h"

static inline void SerializeCompVector( FArchive& Ar, FVector& V )
{
	INT X(appRound(V.X)), Y(appRound(V.Y)), Z(appRound(V.Z));
	DWORD Bits = Clamp<DWORD>( appCeilLogTwo(1+Max(Max(Abs(X),Abs(Y)),Abs(Z))), 1, 20 )-1;
	Ar.SerializeInt( Bits, 20 );
	INT   Bias = 1<<(Bits+1);
	DWORD Max  = 1<<(Bits+2);
	DWORD DX(X+Bias), DY(Y+Bias), DZ(Z+Bias);
	Ar.SerializeInt( DX, Max );
	Ar.SerializeInt( DY, Max );
	Ar.SerializeInt( DZ, Max );
	if( Ar.IsLoading() )
		V = FVector((INT)DX-Bias,(INT)DY-Bias,(INT)DZ-Bias);
}

static inline void SerializeCompRotator( FArchive& Ar, FRotator& R )
{
	BYTE Pitch(R.Pitch>>8), Yaw(R.Yaw>>8), Roll(R.Roll>>8), B;
	B = (Pitch!=0);
	Ar.SerializeBits( &B, 1 );
	if( B )
		Ar << Pitch;
	else
		Pitch = 0;
	B = (Yaw!=0);
	Ar.SerializeBits( &B, 1 );
	if( B )
		Ar << Yaw;
	else
		Yaw = 0;
	B = (Roll!=0);
	Ar.SerializeBits( &B, 1 );
	if( B )
		Ar << Roll;
	else
		Roll = 0;
	if( Ar.IsLoading() )
		R = FRotator(Pitch<<8,Yaw<<8,Roll<<8);
}

// passing NULL for UActorChannel will skip recent property update
static inline void SerializeCompressedInitial( FArchive& Bunch, FVector& Location, FRotator& Rotation, UBOOL bSerializeRotation, UActorChannel* Ch )
{
	guardSlow(SerializeCompressedInitial);

    // read/write compressed location
    SerializeCompVector( Bunch, Location );
    if( Ch )
	{
		static UProperty *spLocation = FindObjectChecked<UProperty>(AActor::StaticClass(),TEXT("Location"));
		if( spLocation )
		{
			BYTE *Dest = Ch->GetMapValueAddr(spLocation->Offset, 1);
			if( Dest )
			{
				appMemcpy( Dest, &Location, spLocation->GetSize() );
			}
		}
	}

    // optionally read/write compressed rotation
    if( bSerializeRotation )
    {
        SerializeCompRotator( Bunch, Rotation );
	    if( Ch )
		{
			static UProperty *spRotation = FindObjectChecked<UProperty>(AActor::StaticClass(),TEXT("Rotation"));
			if( spRotation )
			{
				BYTE *Dest = Ch->GetMapValueAddr(spRotation->Offset, 1);
				if( Dest )
				{
					appMemcpy( Dest, &Rotation, spRotation->GetSize() );
				}
			}
		}
    }
	unguardSlow;
}

/**
 * Method for serializing a FNetString. Note it is more efficient to
 * send UNICODE data than to convert each character to ANSI and send/read.
 */
FArchive& operator<<(FArchive& Ar,FNetString& NetString)
{
	Ar << NetString.Length;
	// Detect buffer overrun attacks
	if (Ar.IsLoading() && NetString.Length > (MAX_STRING_SERIALIZE_SIZE - 1))
	{
		Ar.SetInError();
		return Ar;
	}
	if (Ar.IsSaving())
	{
		TCHAR* ReadFrom = NetString.GetRaw();
		// Serialize each byte in the string to the archive
		// This must be done a single byte at a time otherwise there can be
		// buffer overruns in the FBunchOut class
		for (INT Index = 0; Index < NetString.Length; Index++)
		{
			BYTE b = ReadFrom[Index];
			Ar << b;
		}
	}
	else
	{
		TCHAR* ReadFrom = NetString.GetRaw();
		// Serialize each byte from the archive into the string
		// This must be done a single byte at a time otherwise there can be
		// buffer overruns in the FBunchOut class
		for (INT Index = 0; Index < NetString.Length; Index++)
		{
			BYTE b;
			Ar << b;
			ReadFrom[Index] = b;
		}
		// Zero terminate the string (safe because we know the max size)
		NetString.GetRaw()[NetString.Length] = TEXT('\0');
	}
	return Ar;
}

/*-----------------------------------------------------------------------------
	UChannel implementation.
-----------------------------------------------------------------------------*/

//
// Initialize the base channel.
//
UChannel::UChannel()
{}
void UChannel::Init( UNetConnection* InConnection, INT InChIndex, INT InOpenedLocally )
{
	guard(UChannel::Init);

	check(InOpenedLocally == 0 || InOpenedLocally == 1);

	// scion sz - if child connection then use its parent
	if( InConnection->IsA( UChildConnection::StaticClass() ) )
	{
		Connection = ((UChildConnection*)InConnection)->Parent;
	}
	else
	{
		Connection		= InConnection;
	}
	
	ChIndex			= InChIndex;
	bOpenedLocally	= InOpenedLocally;
	OpenPacketId	= MAXWORD;

	unguard;
}

//
// Route the UObject::Destroy.
//
INT UChannel::RouteDestroy()
{
	guard(UChannel::RouteDestroy);
	if( Connection && (Connection->GetFlags() & RF_Unreachable) )
	{
		ClearFlags( RF_Destroyed );
		if( Connection->ConditionalDestroy() )
		{
			// This channel was destroyed (UObject::Destroy ran) inside the connection's Destroy, in a
			// nested ConditionalDestroy that restores the flags it found, i.e. without RF_DebugDestroy, so the
			// ConditionalDestroy up the stack reported "failed to route Destroy" (non-final builds only; the
			// Xbox build skipped that check). The object is still allocated here: UNetConnection::Destroy
			// leaves the deleting to the garbage collector.
			SetFlags( RF_DebugDestroy );
			return 1;
		}
		SetFlags( RF_Destroyed );
	}
	return 0;
	unguard;
}

//
// Set the closing flag.
//
void UChannel::SetClosingFlag()
{
	guard(UChannel::SetClosingFlag);
	bClosing = 1;
	unguard;
}

//
// Close the base channel.
//
void UChannel::Close()
{
	guard(UChannel::Close);
	check(Connection->Channels[ChIndex]==this);
	if
	(	!bClosing
	&&	(Connection->State==USOCK_Open || Connection->State==USOCK_Pending) )
	{
		// Send a close notify, and wait for ack.
		FOutBunch CloseBunch( this, 1 );
		check(!CloseBunch.IsError());
		check(CloseBunch.GetbClose());
		CloseBunch.SetbReliable( 1 );
		SendBunch( &CloseBunch, 0 );
	}
	unguard;
}

//
// Base channel destructor.
//
void UChannel::Destroy()
{
	guard(UChannel::Destroy);
	check(Connection);
	check(Connection->Channels[ChIndex]==this);

	// remember sequence number of first non-acked outgoing reliable bunch for this slot
	if ( OutRec )
	{
		Connection->PendingOutRec[ChIndex] = OutRec->ChSequence;
		//debugf(TEXT("%i save pending out bunch %i"),ChIndex,Connection->PendingOutRec[ChIndex]);
	}

	// Free any pending incoming and outgoing bunches.
	for( FOutBunch* Out=OutRec, *NextOut; Out; Out=NextOut )
		{NextOut = Out->Next; delete Out;}
	for( FInBunch* In=InRec, *NextIn; In; In=NextIn )
		{NextIn = In->Next; delete In;}

	// Remove from connection's channel table.
	verify(Connection->OpenChannels.RemoveItem(this)==1);
	Connection->Channels[ChIndex] = NULL;
	Connection                    = NULL;

	Super::Destroy();
	unguard;
}

//
// Handle an acknowledgement on this channel.
//
void UChannel::ReceivedAcks()
{
	guard(UChannel::ReceivedAcks);
	check(Connection->Channels[ChIndex]==this);

	// Verify in sequence.
	/*
	for( FOutBunch* Out=OutRec; Out && Out->Next; Out=Out->Next )
		check(Out->Next->ChSequence>Out->ChSequence);
	*/

	// Release all acknowledged outgoing queued bunches.
	UBOOL DoClose = 0;
	while( OutRec && OutRec->GetbReceivedAck() )
	{
		DoClose |= OutRec->GetbClose();
		FOutBunch* Release = OutRec;
		OutRec = OutRec->Next;
		delete Release;
		NumOutRec--;
	}

	// If a close has been acknowledged in sequence, we're done.
	if( DoClose || (bOpenTemporary && bOpenAcked) )
	{
		check(!OutRec);
		delete this;
	}

	unguard;
}

//
// Return the maximum amount of data that can be sent in this bunch without overflow.
//
INT UChannel::MaxSendBytes()
{
	guard(UChannel::MaxSendBytes);
	INT ResultBits
	=	Connection->MaxPacket*8
	-	(Connection->Out.GetNumBits() ? 0 : MAX_PACKET_HEADER_BITS)
	-	Connection->Out.GetNumBits()
	-	MAX_PACKET_TRAILER_BITS
	-	MAX_BUNCH_HEADER_BITS;
	return Max( 0, ResultBits/8 );
	unguard;
}

//
// Handle time passing on this channel.
//
void UChannel::Tick()
{
	guard(UChannel::Tick);
	check(Connection->Channels[ChIndex]==this);
	if( ChIndex==0 && !bOpenAcked )
	{
		// Resend any pending packets if we didn't get the appropriate acks.
		for( FOutBunch* Out=OutRec; Out; Out=Out->Next )
		{
			if( !Out->GetbReceivedAck() )
			{
				FLOAT Wait = Connection->Driver->Time-Out->Time;
				checkSlow(Wait>=0.f);
				if( Wait>1.f )
				{
					debugf( NAME_DevNetTraffic, TEXT("Channel %i ack timeout; resending %i..."), ChIndex, Out->ChSequence );
					check(Out->GetbReliable());
					Connection->SendRawBunch( *Out, 0 );
				}
			}
		}
	}
	unguard;
}

//
// Make sure the incoming buffer is in sequence and there are no duplicates.
//
void UChannel::AssertInSequenced()
{
	guard(UChannel::AssertInSequenced);

	// Verify that buffer is in order with no duplicates.
	for( FInBunch* In=InRec; In && In->Next; In=In->Next )
		check(In->Next->ChSequence>In->ChSequence);

	unguard;
}

//
// Process a properly-sequenced bunch.
//
UBOOL UChannel::ReceivedSequencedBunch( FInBunch& Bunch )
{
	guard(UChannel::ReceivedSequencedBunch);

	// Note this bunch's retirement.
	if( Bunch.GetbReliable() )
		Connection->InReliable[ChIndex] = Bunch.ChSequence;

	// Handle a regular bunch.
	if( !bClosing )
		ReceivedBunch( Bunch );

	// We have fully received the bunch, so process it.
	if( Bunch.GetbClose() )
	{
		// Handle a close-notify.
		if( InRec )
		{
			// InRec should be NULL here, so something went wrong, probably due to extreme lag/packet loss
			debugf(NAME_Error, TEXT("Close Anomaly %i / %i, Channel info: %s"), Bunch.ChSequence, InRec->ChSequence, *Describe());
			if (Connection->Actor != NULL && Connection->Driver != NULL && Connection->Driver->ServerConnection != NULL)
			{
				// we are the client
				// clear the disconnect flag to avoid receiving a disconnect on stats due to poor net conditions
				ULoadSaveManager* Manager = Cast<ULoadSaveManager>(GLoadSaveManager);
				if (Manager != NULL)
				{
					Manager->SetDisconnectFlag(0, 0);
				}
			}
			else
			{
				// we are the server
				// try to tell the client they were kicked, to avoid disconnect on stats, but who knows if it'll get through...
				Connection->Logf(TEXT("FAILURE FORCED"));
			}

			// close the connection
			if (Connection->Channels[0] != NULL)
			{
				Connection->Channels[0]->Close();
			}
			Connection->FlushNet();
		}
		debugf( NAME_DevNetTraffic, TEXT("      Channel %i got close-notify"), ChIndex );
		delete this;
		return 1;
	}
	return 0;
	unguard;
}

//
// Process a raw, possibly out-of-sequence bunch: either queue it or dispatch it.
// The bunch is sure not to be discarded.
//
void UChannel::ReceivedRawBunch( FInBunch& Bunch )
{
	guard(UChannel::ReceivedRawBunch);
	FUNC_CLOCK(ReceivedBunch,GStats.DWORDStats(GEngineStats.STATS_Net_ReceivedRawBunchCyles));
	check(Connection->Channels[ChIndex]==this);
	if
	(	Bunch.GetbReliable()
	&&	Bunch.ChSequence!=Connection->InReliable[ChIndex]+1 )
	{
		// If this bunch has a dependency on a previous unreceived bunch, buffer it.
		guard(QueueIt);
		checkSlow(!Bunch.GetbOpen());

		// Verify that UConnection::ReceivedPacket has passed us a valid bunch.
		check(Bunch.ChSequence>Connection->InReliable[ChIndex]);

		// Find the place for this item, sorted in sequence.
		debugf( NAME_DevNetTraffic, TEXT("      Queuing bunch with unreceived dependency") );
		FInBunch** InPtr;
		for( InPtr=&InRec; *InPtr; InPtr=&(*InPtr)->Next )
		{
			if( Bunch.ChSequence==(*InPtr)->ChSequence )
			{
				// Already queued.
				return;
			}
			else if( Bunch.ChSequence<(*InPtr)->ChSequence )
			{
				// Stick before this one.
				break;
			}
		}
		FInBunch* New = NEW FInBunch(Bunch);
		New->Next     = *InPtr;
		*InPtr        = New;
		NumInRec++;
		checkSlow(NumInRec<=RELIABLE_BUFFER);
		//AssertInSequenced();
		unguard;
	}
	else
	{
		// Receive it in sequence.
		guard(Direct);
		UBOOL Deleted = ReceivedSequencedBunch( Bunch );
		if( Deleted )
			return;
		unguard;

		// Dispatch any waiting bunches.
		guard(ReleaseQueued);
		while( InRec )
		{
			if( InRec->ChSequence!=Connection->InReliable[ChIndex]+1 )
				break;
			debugf( NAME_DevNetTraffic, TEXT("      Unleashing queued bunch") );
			FInBunch* Release = InRec;
			InRec = InRec->Next;
			NumInRec--;
			UBOOL Deleted = ReceivedSequencedBunch( *Release );
			delete Release;
			if( Deleted )
				return;
			//AssertInSequenced();
		}
		unguard;
	}
	unguard;
}

//
// Send a bunch if it's not overflowed, and queue it if it's reliable.
//
INT UChannel::SendBunch( FOutBunch* Bunch, UBOOL Merge )
{
	guard(UChannel::SendBunch);

	if (!Bunch)
	{
		debugf(NAME_Error, TEXT("NULL Bunch in UChannel::SendBunch()!"));
		return MAXWORD;
	}
	check(!bClosing);
	check(Connection->Channels[ChIndex]==this);
	check(!Bunch->IsError());

	// Set bunch flags.
	if( OpenPacketId == MAXWORD && bOpenedLocally )
	{
		Bunch->SetbOpen( 1 );
		bOpenTemporary = !Bunch->GetbReliable();
	}

	// If channel was opened temporarily, we are never allowed to send reliable packets on it.
	if( bOpenTemporary )
	{
		check(!Bunch->GetbReliable());
	}

	// Contemplate merging.
	FOutBunch* OutBunch = NULL;
	if
	(	Merge
	&&	Connection->LastOut.GetChannelIndex()==Bunch->GetChannelIndex()
	&&	Connection->bAllowMerge
	&&	Connection->LastEnd.GetNumBits()
	&&	Connection->LastEnd.GetNumBits()==Connection->Out.GetNumBits()
	&&	Connection->Out.GetNumBytes()+Bunch->GetNumBytes()+(MAX_BUNCH_HEADER_BITS+MAX_PACKET_TRAILER_BITS+7)/8<=Connection->MaxPacket )
	{
		// Merge.
		check(!Connection->LastOut.IsError());
		Connection->LastOut.SerializeBits( Bunch->GetData(), Bunch->GetNumBits() );
		Connection->LastOut.SetbReliable( Connection->LastOut.GetbReliable() | Bunch->GetbReliable() );
		Connection->LastOut.SetbOpen(  Connection->LastOut.GetbOpen()  | Bunch->GetbOpen()  );
		Connection->LastOut.SetbClose( Connection->LastOut.GetbClose() | Bunch->GetbClose() );
		OutBunch                       = Connection->LastOutBunch;
		Bunch                          = &Connection->LastOut;
		check(!Bunch->IsError());
		Connection->LastStart.Pop( Connection->Out );
		Connection->OutBunAcc--;
	}
	else
	{
		Merge = 0;
	}

	// Find outgoing bunch index.
	if( Bunch->GetbReliable() )
	{
		// Find spot, which was guaranteed available by FOutBunch constructor.
		if( OutBunch==NULL )
		{
			check(NumOutRec<RELIABLE_BUFFER-1+Bunch->GetbClose());
			Bunch->Next	= NULL;
			Bunch->ChSequence = ++Connection->OutReliable[ChIndex];
			NumOutRec++;
			OutBunch = NEW FOutBunch(*Bunch);
			FOutBunch** OutLink;
			for( OutLink=&OutRec; *OutLink; OutLink=&(*OutLink)->Next );
			*OutLink = OutBunch;
		}
		else
		{
			Bunch->Next = OutBunch->Next;
			*OutBunch = *Bunch;
		}
		Connection->LastOutBunch = OutBunch;
	}
	else
	{
		OutBunch = Bunch;
		Connection->LastOutBunch = NULL;//warning: Complex code, don't mess with this!
	}

	// Send the raw bunch.
	OutBunch->SetbReceivedAck( 0 );
	INT PacketId = Connection->SendRawBunch( *OutBunch, 1 );
	if( OpenPacketId == MAXWORD && bOpenedLocally )
	{
		OpenPacketId = PacketId;
	}
	if( OutBunch->GetbClose() )
	{
		SetClosingFlag();
	}

	// Update channel sequence count.
	Connection->LastOut = *OutBunch;
	Connection->LastEnd	= FBitWriterMark(Connection->Out);

	return PacketId;
	unguard;
}

//
// Describe the channel.
//
FString UChannel::Describe()
{
	guard(UChannel::Describe);
	return FString(TEXT("State=")) + (bClosing ? TEXT("closing") : TEXT("open") );
	unguard;
}

//
// Return whether this channel is ready for sending.
//
INT UChannel::IsNetReady( UBOOL Saturate )
{
	guard(UChannel::IsNetReady);

	// If saturation allowed, ignore queued byte count.
	if( NumOutRec>=RELIABLE_BUFFER-1 )
		return 0;
	return Connection->IsNetReady( Saturate );

	unguard;
}

//
// Returns whether the specified channel type exists.
//
UBOOL UChannel::IsKnownChannelType( INT Type )
{
	guard(UChannel::IsKnownChannelType);
	return Type>=0 && Type<CHTYPE_MAX && ChannelClasses[Type];
	unguard;
}

//
// Negative acknowledgement processing.
//
void UChannel::ReceivedNak( INT NakPacketId )
{
	guard(UChannel::ReceivedNak);
	for( FOutBunch* Out=OutRec; Out; Out=Out->Next )
	{
		// Retransmit reliable bunches in the lost packet.
		if( Out->GetPacketID()==NakPacketId && !Out->GetbReceivedAck() )
		{
			check(Out->GetbReliable());
			debugf( NAME_DevNetTraffic, TEXT("      Channel %i nak; resending %i..."), Out->GetChannelIndex(), Out->ChSequence );
			Connection->SendRawBunch( *Out, 0 );
		}
	}
	unguard;
}

// UChannel statics.
UClass* UChannel::ChannelClasses[CHTYPE_MAX]={0,0,0,0,0,0,0,0};
IMPLEMENT_CLASS(UChannel)

/*-----------------------------------------------------------------------------
	UControlChannel implementation.
-----------------------------------------------------------------------------*/

//
// Initialize the text channel.
//
UControlChannel::UControlChannel()
{}
void UControlChannel::Init( UNetConnection* InConnection, INT InChannelIndex, INT InOpenedLocally )
{
	guard(UControlChannel::UControlChannel);
	Super::Init( InConnection, InChannelIndex, InOpenedLocally );
	unguard;
}

//
// UControlChannel destructor.
//
void UControlChannel::Destroy()
{
	guard(UControlChannel::Destroy);
	check(Connection);
	if( RouteDestroy() )
		return;
	check(Connection->Channels[ChIndex]==this);

	Super::Destroy();
	unguard;
}

//
// Handle an incoming bunch.
//
void UControlChannel::ReceivedBunch( FInBunch& Bunch )
{
	guard(UControlChannel::ReceivedBunch);
	check(!bClosing);
	for( ; ; )
	{
		//jg -- Changed to use my memory friendly class
		FNetStringRead Text;
		Bunch << Text;
		if( Bunch.IsError() )
			break;
#if DO_ENABLE_NET_TEST
		UC2NetTestNoteText( Connection, *Text, 0 );	// [UC2NetTest] Drop= triggers (UC2NetTest.cpp)
#endif
		Connection->Driver->Notify->NotifyReceivedText( Connection, *Text );
	}
	unguard;
}

//
// Text channel FArchive interface.
//
void UControlChannel::Serialize( const TCHAR* Data, EName MsgType )
{
	guard(UControlChannel::Serialize);
	// Delivery is not guaranteed because NewBunch may fail.
	FOutBunch Bunch( this, 0 );
	Bunch.SetbReliable( 1 );
	//jg -- Changed to use my memory friendly class
	FNetString Text(Data);
	Bunch << Text;
	if( !Bunch.IsError() )
	{
		SendBunch( &Bunch, 1 );
	}
	else
	{
		debugf( NAME_DevNet, TEXT("Control channel bunch overflowed") );
		//!!should signal error somehow
	}
	unguard;
}

//
// Describe the text channel.
//
FString UControlChannel::Describe()
{
	guard(UControlChannel::Describe);
	return FString(TEXT("Text ")) + UChannel::Describe();
	unguard;
}

IMPLEMENT_CLASS(UControlChannel);

/*-----------------------------------------------------------------------------
	UActorChannel.
-----------------------------------------------------------------------------*/

//
// Initialize this actor channel.
//
UActorChannel::UActorChannel()
{}
void UActorChannel::Init( UNetConnection* InConnection, INT InChannelIndex, INT InOpenedLocally )
{
	guard(UActorChannel::UActorChannel);
	Super::Init( InConnection, InChannelIndex, InOpenedLocally );
	Level			= Connection->Driver->Notify->NotifyGetLevel();
	RelevantTime	= Connection->Driver->Time;
	LastUpdateTime	= Connection->Driver->Time - Connection->Driver->SpawnPrioritySeconds;
	LastFullUpdateTime = -1.f;
	bActorDirty = false;
	bActorMustStayDirty = false;
	bActorStillInitial = false;
	unguard;
}

//
// Set the closing flag.
//
void UActorChannel::SetClosingFlag()
{
	guard(UActorChannel::SetClosingFlag);
	if( Actor )
		Connection->ActorChannels.Remove( Actor );
	UChannel::SetClosingFlag();
	unguard;
}

//
// Close it.
//
void UActorChannel::Close()
{
	guard(UActorChannel::Close);
	debugf( NAME_DevSam, TEXT("Closing actor channel [%s]"), Actor->GetName() );
	UChannel::Close();
	Actor = NULL;
	unguard;
}

//
// Time passes...
//
void UActorChannel::Tick()
{
	guard(UActorChannel::Tick);

	UChannel::Tick();

	//epic moelfke: if this is a temporary channel and we've been around for a really long time, give up
	//(if our Actor is still around this will cause it to get resent, otherwise it's irrelevant)
	if (bOpenTemporary && !bOpenAcked)
	{
		FLOAT Wait = Connection->Driver->Time - LastUpdateTime;
		if (Wait > 15.f)
		{
			debugf(NAME_DevNet, TEXT("Temporary channel %i with ActorClass %s ack timeout; destroying..."), ChIndex, ActorClass ? ActorClass->GetPathName() : TEXT("None"));
			delete this;
		}
	}
	unguard;
}

//
// Actor channel destructor.
//
void UActorChannel::Destroy()
{
	guard(UActorChannel::Destroy);
	check(Connection);
	if( RouteDestroy() )
		return;
	check(Connection->Channels[ChIndex]==this);

	// Remove from hash and stuff.
	SetClosingFlag();

	//epic superville: Clean up all allocated arrays in the recent map
	if( RecentMap.Num() )
	{
		for( TMap< _WORD, TArray<BYTE>* >::TIterator It(RecentMap); It; ++It )
		{
			// check if this value belongs to a property that needs destruction and if so allow that property to destroy it
			INT Offset = It.Key();
			UProperty* Result = NULL;
			for (UProperty* Prop = ActorClass->PropertyLink; Prop && !Result; Prop = Prop->PropertyLinkNext)
			{
				if (Prop->Offset == Offset)
				{
					Result = Prop;
				}
			}

			if (Result && (Result->PropertyFlags & CPF_NeedCtorLink))
			{
				Result->DestroyValue(It.Value());
			}
#ifdef _DEBUG
			else if (!Result)
			{
				debugf(NAME_Warning, TEXT("Failed to find property with offset %d in class %s"), Offset, ActorClass->GetName());
			}
#endif

			// actually free the memory
			delete It.Value();
		}
	}

	// If we're the client, destroy this actor.
	guard(DestroyChannelActor);
	if( Connection->Driver->ServerConnection )
	{
		check(!Actor || Actor->IsValid());
		checkSlow(Level);
		checkSlow(Level->IsValid());
		checkSlow(Connection);
		checkSlow(Connection->IsValid());
		checkSlow(Connection->Driver);
		checkSlow(Connection->Driver->IsValid());
		if( Actor )
		{
			if( Actor->bTearOff )
			{
				Actor->Role = ROLE_Authority;
				Actor->RemoteRole = ROLE_None;
			}
			else if( !Actor->bNetTemporary )
				Actor->GetLevel()->DestroyActor( Actor, 1 );
		}
	}
	else if( Actor && !bOpenAcked )
	{
		// Resend temporary actors if nak'd.
		Connection->SentTemporaries.RemoveItem( Actor );
	}
	unguard;

	Super::Destroy();
	unguard;
}

//
// Negative acknowledgements.
//
void UActorChannel::ReceivedNak( INT NakPacketId )
{
	guard(UActorChannel::ReceivedNak);
	UChannel::ReceivedNak(NakPacketId);
	if( ActorClass )
	{
		for( INT i = Retirement.Num() - 1; i >= 0; i-- )
		{
			if( Retirement(i).OutPacketId == NakPacketId && !Retirement(i).Reliable )
			{
				Dirty.AddUniqueItem(i);
			}
		}
	}
    bActorDirty = true;
	unguard;
}

//
// Allocate replication tables for the actor channel.
//
void UActorChannel::SetChannelActor( AActor* InActor )
{
	guard(UActorChannel::SetChannelActor);
	check(!bClosing);
	check(Actor==NULL);

	//debug
	if( InActor->bNetDebug )
	{
		debugf( TEXT("SetChannelActor %s Idx %d PktId %d"), InActor->GetName(), ChIndex, OpenPacketId );
	}
    
	// Set stuff.
	Actor                      = InActor;
	ActorClass                 = Actor->GetClass();
	FClassNetCache* ClassCache = Connection->PackageMap->GetClassNetCache( ActorClass );

	if ( Connection->PendingOutRec[ChIndex] > 0 )
	{
		// send empty reliable bunches to synchronize both sides
		// debugf(TEXT("%i Actor %s WILL BE sending %i vs first %i"), ChIndex, Actor->GetName(), Connection->PendingOutRec[ChIndex],Connection->OutReliable[ChIndex]);
		INT RealOutReliable = Connection->OutReliable[ChIndex];
		Connection->OutReliable[ChIndex] = Connection->PendingOutRec[ChIndex] - 1;
		while ( Connection->PendingOutRec[ChIndex] <= RealOutReliable )
		{
			// debugf(TEXT("%i SYNCHRONIZING by sending %i"), ChIndex, Connection->PendingOutRec[ChIndex]);
	
			FOutBunch Bunch( this, 0 );
			if( !Bunch.IsError() )
			{
				Bunch.SetbReliable( 1 );
				SendBunch( &Bunch, 0 );
				Connection->PendingOutRec[ChIndex]++;
			}
		}
	
		Connection->OutReliable[ChIndex] = RealOutReliable;
		Connection->PendingOutRec[ChIndex] = 0;
	}

	// Add to map.
	Connection->ActorChannels.Set( Actor, this );

	// Allocate replication condition evaluation cache.
	RepEval.AddZeroed( ClassCache->GetRepConditionCount() );

	//epic superville: 
	// Init recent properties
	// Only needed on server (variables are no longer replicated from client to server)
	if (!InActor->bNetTemporary && InActor->Role == ROLE_Authority)
	{
		//jg -- Nasty ass hack to prevent subobjects from being allocated
		GUglyHackFlags |= 0x20000;
		InActor->CreateRecentMap(RecentMap);
		GUglyHackFlags &= ~0x20000;
	}

	// Allocate retirement list.
	Retirement.Empty( ActorClass->ClassReps.Num() );
	while( Retirement.Num() < ActorClass->ClassReps.Num() )
	{
		new(Retirement)FPropertyRetirement;
	}

	unguard;
}

//
// Handle receiving a bunch of data on this actor channel.
//
void UActorChannel::ReceivedBunch( FInBunch& Bunch )
{
	guard(UActorChannel::ReceivedBunch);
	FUNC_CLOCK(ReceivedBunch,GStats.DWORDStats(GEngineStats.STATS_Net_ReceivedBunchCyles));
	check(!bClosing);
	if ( bBroken || bTornOff )
		return;

	// Initialize client if first time through.
	INT bJustSpawned = 0;
	FClassNetCache* ClassCache = NULL;
	if( Actor==NULL )
	{
		guard(InitialClientActor);
		if( !Bunch.GetbOpen() )
		{
			warnf(TEXT("New actor channel received non-open packet: %i/%i/%i"),Bunch.GetbOpen(),Bunch.GetbClose(),Bunch.GetbReliable());
			bBroken = true;
			return;
		}

		// Read class.
		UObject* Object;
		Bunch << Object;
		AActor* InActor = Cast<AActor>( Object );
		if( InActor==NULL )
		{
			// Transient actor.
			UClass* ActorClass = Cast<UClass>( Object );
			if (!ActorClass || !ActorClass->IsChildOf(AActor::StaticClass()))
			{
				// We are unsynchronized. Instead of crashing, let's try to recover.
				warnf(TEXT("Received invalid actor class"));
				// Say what arrived instead (diagnostic).
				debugf( NAME_DevNet, TEXT("  channel %i: got %s (bunch error %i, bits %i, reliable %i, seq %i)"), ChIndex, Object ? Object->GetFullName() : TEXT("None"), Bunch.IsError(), Bunch.GetNumBits(), Bunch.GetbReliable(), Bunch.ChSequence );
				bBroken = true;
				return;
			}
			check(ActorClass);
			check(ActorClass->IsChildOf(AActor::StaticClass()));

			FVector Location;
            FRotator Rotation(0,0,0);
            SerializeCompressedInitial( Bunch, Location, Rotation, ActorClass->GetDefaultActor()->bNetInitialRotation, NULL );

            InActor = Level->SpawnActor( ActorClass, NAME_None, Location, Rotation, NULL, IGNORE_SPAWN_Collision, 1 );
			bJustSpawned = 1;
			if ( !InActor )
			{
				debugf(TEXT("Couldn't spawn %s high detail=%d"),ActorClass->GetName(), ActorClass->GetDefaultActor()->DetailLevel );
				if ( ActorClass->GetDefaultActor()->bNoDelete )
				{
					ActorClass->GetDefaultActor()->bNoDelete = false;
					InActor = Level->SpawnActor( ActorClass, NAME_None, Location, Rotation, NULL, IGNORE_SPAWN_Collision, 1 );
				}
				if ( !InActor )
				{
					InActor = Level->SpawnActor( ActorClass, NAME_None, Location, Rotation, NULL, IGNORE_SPAWN_Collision|IGNORE_SPAWN_DetailLevel, 1 );					
				}
			}
			check(InActor);
		}
		//scion rwd - net debug
		if (InActor->bNetDebug)
		{
			debugf(TEXT("bNetDebug %2.1f - Spawn %s:"), Level->TimeSeconds, InActor->GetFullName() );
		}
		else
		{
			debugf( NAME_DevNetTraffic, TEXT("      Spawn %s:"), InActor->GetFullName() );
		}
		SetChannelActor( InActor );
		unguard;
	}
	else
	{
		debugf( NAME_DevNetTraffic, TEXT("      Actor %s:"), Actor->GetFullName() );
	}
	ClassCache = Connection->PackageMap->GetClassNetCache(ActorClass);

	// Owned by connection's player?
	guard(SetNetMode);
	Actor->bNetOwner = 0;
	APlayerController* Top = Actor->GetTopPlayerController();//Cast<APlayerController>( Actor->GetTopOwner() );
	UPlayer* Player = Top ? Top->Player : NULL;
	// Set quickie replication variables.
	if( Connection->Driver->ServerConnection )
	{
		// We are the client.
		if( Player && Player->IsA( UViewport::StaticClass() ) )
			Actor->bNetOwner = 1;
	}
	else
	{
		// We are the server.
		if( Player==Connection )
		{
			Actor->bNetOwner = 1;
		}

		// scion sz - if child connection then use its parent
		if( Player && Player->IsA(UChildConnection::StaticClass()) )
		{
			if( ((UChildConnection*)Player)->Parent == Connection )
				Actor->bNetOwner = 1;
		}
	}
	unguard;

	// Handle the data stream.
	guard(HandleStream);
	checkSlow(ClassCache);
	INT             RepIndex   = Bunch.ReadInt( ClassCache->GetMaxIndex() );
	FFieldNetCache* FieldCache = Bunch.IsError() ? NULL : ClassCache->GetFromIndex( RepIndex );
	while (FieldCache || bJustSpawned)
	{
		checkSlow(Actor);
		// Save current properties.
		//debugf(TEXT("Rep %s: %i"),FieldCache->Field->GetFullName(),RepIndex);
		UBOOL bHasFieldCache = (FieldCache != NULL);
		// only call PreNetReceive() if we've received properties
		// since the server doesn't receive properties anymore, we don't ever need to call PreNetReceive() on it
		if (bHasFieldCache && Actor->Role != ROLE_Authority)
		{
			Actor->PreNetReceive();
		}

		// Receive properties from the net.
		guard(Properties);
		UProperty* It;
		while( FieldCache && (It=Cast<UProperty>(FieldCache->Field,CLASS_IsAUProperty))!=NULL )
		{
			// Receive array index.
			BYTE Element=0;
			if( It->ArrayDim != 1 )
				Bunch << Element;

			// Pointer to destiation.
			BYTE* DestActor  = (BYTE*)Actor;
			UBOOL DestValid	 = RecentMap.Num() ? 1 : 0;

			// Server, see if UnrealScript replication condition is met.
			if( !Connection->Driver->ServerConnection )
			{
				guard(EvalPropertyCondition);
				
				debugf(NAME_DevNet, TEXT("Server received unwanted property value %s in %s"), It->GetName(), Actor->GetFullName());

				DestActor = NULL;
				DestValid = 0;
				unguard;
			}

			// Check property ordering.
			FPropertyRetirement& Retire = Retirement( It->RepIndex + Element );

			//superville: Since changing packetid from INT to _WORD means wrap around will occur, need to check for it
			if( Bunch.GetPacketID() >= Retire.InPacketId || 
				(Retire.InPacketId - Bunch.GetPacketID()) > (MAXWORD>>6) ) 
			{
				//!! problem with reliable pkts containing dynamic references, being retransmitted, and overriding newer versions. Want "OriginalPacketId" for retransmissions?
				// Receive this new property.
				Retire.InPacketId = Bunch.GetPacketID();
			}
			else
			{
				// Skip this property, because it's out-of-date.
				debugf( NAME_DevNet, TEXT("%s Received out-of-date %s BunchID %d RetireID %i"), Actor->GetName(), It->GetName(), Bunch.GetPacketID(), Retire.InPacketId );
	
				DestActor  = NULL;
				DestValid  = 0;
			}

			// Receive property.
			guard(ReceiveProperty);
			FMemMark Mark(GMem);
			INT   Offset = It->Offset + Element*It->ElementSize;
			BYTE* Data   = DestActor ? (DestActor + Offset) : NewZeroed<BYTE>(GMem,It->ElementSize);
			It->NetSerializeItem( Bunch, Connection->PackageMap, Data );

			if( DestValid )
			{
				BYTE* Dest = GetMapValueAddr(Offset - Element * It->ElementSize);
				if( Dest )
				{
					It->CopySingleValue(Dest + Element * It->ElementSize, Data);
				}
			}


			Mark.Pop();
			unguard;

			// Successfully received it.
			//scion rwd - net debug
			if (Actor->bNetDebug)
			{
				debugf(TEXT("bNetDebug %2.1f %s -- %s prop: %s"),
					   Level->TimeSeconds,
					   Actor->bNetOwner ? TEXT("[NetOwner]"): TEXT(""),
					   Actor->GetName(),
					   It->GetName());
			}
			else
			{
				debugf( NAME_DevNetTraffic, TEXT("         %s"), It->GetName() );
			}

			// Get next.
			RepIndex   = Bunch.ReadInt( ClassCache->GetMaxIndex() );
			FieldCache = Bunch.IsError() ? NULL : ClassCache->GetFromIndex( RepIndex );
		}
		unguard;

		// Process important changed properties.
		// since the server doesn't receive properties anymore, we don't ever need to call PostNetReceive() on it
		if (Actor->Role != ROLE_Authority)
		{
			if (bHasFieldCache)
			{
				Actor->PostNetReceive(); // WARNING: may destroy Actor (which closes channel and sets Actor to NULL)
				if (!Actor)
				{
					return;
				}
			}
			if (bJustSpawned)
			{
				bJustSpawned = 0;
				Actor->eventPostNetBeginPlay(); // WARNING: may destroy Actor (which closes channel and sets Actor to NULL)
				if (!Actor)
				{
					return;
				}
			}
		}

		// Handle function calls.
		if( FieldCache && Cast<UFunction>(FieldCache->Field,CLASS_IsAUFunction) )
		{
			guard(RemoteCall);
			FName Message = FieldCache->Field->GetFName();
			UFunction* Function = Actor->FindFunction( Message );
			check(Function);

			// See if UnrealScript replication condition is met.
			UBOOL Ignore=0;
			UFunction* Test;
			for( Test=Function; Test->GetSuperFunction(); Test=Test->GetSuperFunction() );
			if( !Connection->Driver->ServerConnection )
			{
				guard(EvalRPCCondition);
				Exchange(Actor->Role,Actor->RemoteRole);
				DWORD Val=0;
				FFrame( Actor, Test->GetOwnerClass(), Test->RepOffset, NULL ).Step( Actor, &Val );
				Exchange(Actor->Role,Actor->RemoteRole);
				if( !Val || !Actor->bNetOwner )
				{
					debugf( NAME_DevNet, TEXT("Received unwanted function %s in %s"), *Message, Actor->GetFullName() );
					Ignore = 1;
				}
				unguard;
			}
			//scion rwd - net debug
			if (Actor->bNetDebug)
			{
				debugf(TEXT("bNetDebug %2.1f -- RPC: %s"),Level->TimeSeconds,*Message);
			}
			else
			{
				debugf( NAME_DevNetTraffic, TEXT("      Received RPC: %s"), *Message );
			}

			// Get the parameters.
			FMemMark Mark(GMem);
			BYTE* Parms = new(GMem,MEM_Zeroed,Function->ParmsSize)BYTE;
			for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(Function); It && (It->PropertyFlags & (CPF_Parm|CPF_ReturnParm))==CPF_Parm; ++It )
				if( Connection->PackageMap->ObjectToIndex(*It)!=INDEX_NONE )
					if( Cast<UBoolProperty>(*It,CLASS_IsAUBoolProperty) || Bunch.ReadBit() )
						It->NetSerializeItem(Bunch,Connection->PackageMap,Parms+It->Offset);

			// Call the function.
			if( !Ignore )
			{
				// The bClientDemoNetFunc flag gets cleared in ProcessDemoRecFunction
				Actor->bClientDemoNetFunc = 1;
				Actor->ProcessEvent( Function, Parms );
//				debugf( NAME_DevNetTraffic, TEXT("      Call RPC: %s"), Function->GetName() );
			}

			// Destroy the parameters.
			//warning: highly dependent on UObject::ProcessEvent freeing of parms!
			{for( UProperty* Destruct=Function->ConstructorLink; Destruct; Destruct=Destruct->ConstructorLinkNext )
				if( Destruct->Offset < Function->ParmsSize )
					Destruct->DestroyValue( Parms + Destruct->Offset );}
			Mark.Pop();

			// Next.
			RepIndex   = Bunch.ReadInt( ClassCache->GetMaxIndex() );
			FieldCache = Bunch.IsError() ? NULL : ClassCache->GetFromIndex( RepIndex );
			if (!Actor)
			{
				// actor was destroyed when replicated function was called
				return;
			}
			unguard;
		}
		else if( FieldCache )
		{
			appErrorf( TEXT("Invalid replicated field %i in %s"), RepIndex, Actor->GetFullName() );
			return;
		}
	}
	unguard;

	if (Actor != NULL)
	{
		// Tear off an actor on the client-side
		if ( Actor->bTearOff && (Actor->Level->NetMode == NM_Client) )
		{
			Actor->Role = ROLE_Authority;
			Actor->RemoteRole = ROLE_None;
			bTornOff = true;
			Connection->ActorChannels.Remove( Actor );
			Actor->eventTornOff();
			Actor = NULL;
			return;
		}

		// If this is the player's channel and the connection was pending, mark it open.
		if
		(	Connection->Driver
		&&	Connection == Connection->Driver->ServerConnection
		&&	Connection->State==USOCK_Pending
		&&	Actor->bNetOwner
		&&	Actor->IsAPlayerController() )
		{
			Connection->HandleClientPlayer( (APlayerController*)(Actor) );
		}

		// scion sz -
		// from UC1. Create a child connection for the client
		// if the socket is already open and we just received
		// a new player controller.
		if ( Connection->Driver
			&&	Connection == Connection->Driver->ServerConnection
			&&	Connection->State==USOCK_Open
			&&	Actor->bNetOwner
			&&	Actor->IsAPlayerController()
			&&	Actor != Connection->Actor )
		{
			APlayerController* PC = (APlayerController*)(Actor);
			UClient* Client = Actor->GetLevel()->Engine->Client;

			// Does the PC have a viewport?
			UBOOL FoundExisting=0;
			for( INT i=0; i<Client->Viewports.Num(); i++ )
			{
				if( Client->Viewports(i)->Actor == PC)
				{
					FoundExisting = 1;
					break;
				}
			}

			// Not found - create child connection
			if( !FoundExisting)
			{
				// create a split connection on the client
				UChildConnection* child = Connection->Driver->CreateChild(Connection); 
				child->HandleClientPlayer(PC);
				debugf( NAME_DevSplitScreen, TEXT("Client received PlayerController=%s. Num child connections=%d."), Actor->GetName(), Connection->Children.Num() );
			}
		}
	}
	else
	{
		debugf(NAME_Warning, TEXT("NULL Actor in ReceivedBunch(), class %s"), ActorClass ? ActorClass->GetName() : TEXT("None"));
	}

	unguardf(( TEXT("(Actor %s)"), Actor ? Actor->GetName() : TEXT("None")));
}

//
// Replicate this channel's actor differences.
//
void UActorChannel::ReplicateActor()
{
	guard(UActorChannel::ReplicateActor);
	checkSlow(Actor);
	checkSlow(!bClosing);

	// Create an outgoing bunch, and skip this actor if the channel is saturated.
	FOutBunch Bunch( this, 0 );
	if( Bunch.IsError() )
		return;

	// Send initial stuff.
	guard(SetupInitial);
	if( OpenPacketId != MAXWORD )
	{
		Actor->bNetInitial = 0;
		if( !bSpawnAcked && bOpenAcked )
		{
			// After receiving ack to the spawn, force refresh of all subsequent unreliable packets, which could
			// have been lost due to ordering problems. Note: We could avoid this by doing it in FActorChannel::ReceivedAck,
			// and avoid dirtying properties whose acks were received *after* the spawn-ack (tricky ordering issues though).
			bSpawnAcked = true;
			for( INT i = Retirement.Num() - 1; i >= 0; i-- )
			{
				if( Retirement(i).OutPacketId != MAXWORD && !Retirement(i).Reliable )
				{
					Dirty.AddUniqueItem(i);
				}
			}
		}
	}
	else
	{
		Actor->bNetInitial = 1;
		Bunch.SetbClose( Actor->bNetTemporary );
		Bunch.SetbReliable( !Actor->bNetTemporary );
	}
	unguard;

	// Get class network info cache.
	FClassNetCache* ClassCache = Connection->PackageMap->GetClassNetCache(Actor->GetClass());
	check(ClassCache);

	// Owned by connection's player?
	Actor->bNetOwner = 0;
    APlayerController* Top = Actor->GetTopPlayerController();
	UPlayer* Player = Top ? Top->Player : NULL;
	UBOOL bRecordingDemo = Connection->Driver->IsDemoDriver();
	UBOOL bClientDemo = bRecordingDemo && Actor->Level->NetMode == NM_Client;

	// Set quickie replication variables.
	UBOOL bDemoOwner = 0;
	if( bRecordingDemo )
	{
		Actor->bNetOwner = 1;
		bDemoOwner = bClientDemo ? (Player && Cast<UViewport>(Player)!=NULL) : Actor->bDemoOwner;
	}
	else	
	{
		Actor->bNetOwner = Connection->Driver->ServerConnection ? Cast<UViewport>(Player)!=NULL : Player==Connection;
		bDemoOwner = 0;

		// scion sz - use child connection's parent
		if( Connection->Driver->ServerConnection==NULL &&				// server side
			Player && Player->IsA(UChildConnection::StaticClass()) )	// player controller child connection
		{
			if( ((UChildConnection*)Player)->Parent==Connection )
				Actor->bNetOwner = 1;
		}
	}
	Actor->bRepClientDemo = Actor->bNetOwner && Top && Top->bClientDemo;

	// If initial, send init data.
	if( Actor->bNetInitial && bOpenedLocally )
	{
		guard(SendInitialActorData);
		if( Actor->bStatic || Actor->bNoDelete )
		{
			// Persistent actor.
			// Diagnostic, a persistent actor the level spawned at run time (no linker, e.g. the
			// DefaultPhysicsVolume when the map file has none) has no package-map index and goes out as None.
			if( Connection->PackageMap->ObjectToIndex(Actor)==INDEX_NONE )
				debugf( NAME_DevNet, TEXT("Channel %i: persistent actor %s has no package-map index"), ChIndex, Actor->GetFullName() );
			Bunch << Actor;
		}
		else
		{
			// Transient actor.
			// Diagnostic, a class the client cannot resolve goes out as None.
			if( Connection->PackageMap->ObjectToIndex(ActorClass)==INDEX_NONE )
				debugf( NAME_DevNet, TEXT("Channel %i: actor %s has unmapped class %s"), ChIndex, Actor->GetName(), ActorClass ? ActorClass->GetPathName() : TEXT("None") );
            Bunch << ActorClass;
            SerializeCompressedInitial( Bunch, Actor->Location, Actor->Rotation, Actor->bNetInitialRotation, this );
		}
		unguard;
	}

	// Save out the actor's RemoteRole, and downgrade it if necessary.
	BYTE ActualRemoteRole=Actor->RemoteRole;
	if( Actor->RemoteRole==ROLE_AutonomousProxy && ((Actor->Instigator && !Actor->Instigator->bNetOwner && !Actor->bNetOwner) || (bRecordingDemo && !bDemoOwner)) )
		Actor->RemoteRole=ROLE_SimulatedProxy;

	Actor->bNetDirty = bActorDirty || Actor->bNetInitial || Actor->bDemoRecording;
	Actor->bNetInitial = Actor->bNetInitial || bActorStillInitial; // for replication purposes, bNetInitial stays true until all properties sent
	bActorMustStayDirty = false;
	bActorStillInitial = false;
	//debugf(NAME_DevNetTraffic,TEXT("Replicate %s: NetDirty %d Still Initial %d"),ActorClass->GetName(), Actor->bNetDirty,Actor->bNetInitial);

	// Get memory for retirement list.
	FMemMark Mark(GMem);
	appMemzero( &RepEval(0), RepEval.Num() );
	INT* Reps = New<INT>( GMem, Retirement.Num() ), *LastRep;
	UBOOL		FilledUp = 0;

	// Figure out which properties to replicate.
	guard(FigureOutWhatNeedsReplicating);

	INT     iCount     = ClassCache->RepProperties.Num();
	LastRep            = Actor->GetOptimizedRepList( &ActorClass->Defaults(0), &Retirement(0), Reps, Connection->PackageMap, this );
	if( Actor->bNetDirty )
	{
		if ( Actor->DelayScriptReplication(LastFullUpdateTime) )
			bActorMustStayDirty = true;
		else
		{
		for( INT iField=0; iField<iCount; iField++  )
		{
			FFieldNetCache* FieldCache = ClassCache->RepProperties(iField);
            UProperty* It = (UProperty*)(FieldCache->Field);
			BYTE& Eval = RepEval(FieldCache->ConditionIndex);
			if( Eval!=2 )
			{
				UObjectProperty* Op = Cast<UObjectProperty>(It,CLASS_IsAUObjectProperty);
				for( INT Index=0; Index<It->ArrayDim; Index++ )
				{
					// Evaluate need to send the property.
					INT Offset = It->Offset + Index*It->ElementSize;
					BYTE* Src = (BYTE*)Actor + Offset;
					if( Op && !Connection->PackageMap->CanSerializeObject(*(UObject**)Src) )
					{
						//debugf(TEXT("%s MUST STAY DIRTY Because of %s (property %s)"), Actor->GetName(), (*(UObject**)Src)->GetName(), Op->GetName());
						//epic moelfke: don't make Actor stay dirty for properties referencing objects pending kill
						if (!(*(UObject**)Src)->IsPendingKill())
						{
							bActorMustStayDirty = true;
						}
						Src = NULL;
					}

					BYTE* Dest = GetMapValueAddr(Offset - Index * It->ElementSize, Actor->bNetTemporary) + Index * It->ElementSize;
					if (!Dest)
					{
						checkSlow(Actor->bNetTemporary);
						Dest = Actor->GetClass()->GetDefaultsSafe(Offset);
					}
					if( Dest && !It->Identical( Dest, Src ) )
					{
						if( !(Eval & 2) )
						{
							DWORD Val=0;
							FFrame( Actor, It->RepOwner->GetOwnerClass(), It->RepOwner->RepOffset, NULL ).Step( Actor, &Val );
							Eval = Val | 2;
						}
						if( Eval & 1 )
							*LastRep++ = It->RepIndex+Index;
					}
				}
			}
		}
	}
	}
	check(!Bunch.IsError());
	unguard;

	// Add dirty properties to list.
	guard(AddDirtyProperties);
	for( INT i=Dirty.Num()-1; i>=0; i-- )
	{
		INT D=Dirty(i);
		INT* R;
		for( R=Reps; R<LastRep; R++ )
			if( *R==D )
				break;
		if( R==LastRep )
			*LastRep++=D;
	}
	unguard;

	TArray<INT>  StillDirty;

	// Replicate those properties.
	guard(ReplicateThem);
	for( INT* iPtr=Reps; iPtr<LastRep; iPtr++ )
	{
		// Get info.
		FRepRecord* Rep    = &ActorClass->ClassReps(*iPtr);
		UProperty*	It     = Rep->Property;
		INT         Index  = Rep->Index;
		INT         Offset = It->Offset + Index*It->ElementSize;

		// Figure out field to replicate.
		FFieldNetCache* FieldCache
		=	It->GetFName()==NAME_Role
		?	ClassCache->GetFromField(Connection->Driver->RemoteRoleProperty)
		:	It->GetFName()==NAME_RemoteRole
		?	ClassCache->GetFromField(Connection->Driver->RoleProperty)
		:	ClassCache->GetFromField(It);
		checkSlow(FieldCache);

		// Send property name and optional array index.
		FBitWriterMark Mark( Bunch );
		Bunch.WriteInt( FieldCache->FieldNetIndex, ClassCache->GetMaxIndex() );
		if( It->ArrayDim != 1 )
		{
			BYTE Element = Index;
			Bunch << Element;
		}

		// Send property.
		UBOOL Mapped = It->NetSerializeItem( Bunch, Connection->PackageMap, (BYTE*)Actor + Offset );
		//debugf(NAME_DevNetTraffic,TEXT("   Send %s %i"),It->GetName(),Mapped);
		if( !Bunch.IsError() )
		{
			// Update recent value.
			if( RecentMap.Num() )
			{
				if( Mapped )
				{
					BYTE* Dest = GetMapValueAddr(Offset - Index * It->ElementSize);
					if( Dest )
					{
						It->CopySingleValue(Dest + Index * It->ElementSize, (BYTE*)Actor + Offset);
					}
				}
				else
				{
					StillDirty.AddUniqueItem( *iPtr );
				}
			}

			STAT_INC(GStats.DWORDStats(GEngineStats.STATS_Net_NumReps));
		}
		else
		{
			// Stop the changes because we overflowed.
			Mark.Pop( Bunch );
			LastRep  = iPtr;
			//debugf(NAME_DevNetTraffic,TEXT("FILLED UP"));
			FilledUp = 1;
			break;
		}
	}
	unguard;

	// If not empty, send and mark as updated.
	if( Bunch.GetNumBits() )
	{
		guard(DoSendBunch);
		INT PacketId = SendBunch( &Bunch, 1 );
		for( INT* Rep=Reps; Rep<LastRep; Rep++ )
		{
			Dirty.RemoveItem(*Rep);
			FPropertyRetirement& Retire = Retirement(*Rep);
			Retire.OutPacketId = PacketId;
			Retire.Reliable    = Bunch.GetbReliable();

			//debug
//			debugf( "Set Retire OutPacketId %d", Retire.OutPacketId );
		}
		if( Actor->bNetTemporary )
		{
			Connection->SentTemporaries.AddItem( Actor );
		}
		unguard;
	}
	for ( INT i=0; i<StillDirty.Num(); i++ )
		Dirty.AddUniqueItem(StillDirty(i));

	// If we evaluated everything, mark LastUpdateTime, even if nothing changed.
	if ( FilledUp )
	{
		debugf(NAME_DevNetTraffic,TEXT("Filled packet up before finishing %s still initial %d"),Actor->GetName(),bActorStillInitial);
	}
	else
	{
		LastUpdateTime = Connection->Driver->Time;
	}

	bActorStillInitial = Actor->bNetInitial && (FilledUp || (!Actor->bNetTemporary && bActorMustStayDirty));
	bActorDirty = bActorMustStayDirty || FilledUp;

	// Reset temporary net info.
	Actor->bNetOwner  = 0;
	Actor->RemoteRole = ActualRemoteRole;
	Actor->bNetDirty = 0;
	
	Mark.Pop();
	unguardf(( TEXT("(Actor %s)"), Actor ? Actor->GetName() : TEXT("None")));;
}

//
// Describe the actor channel.
//
FString UActorChannel::Describe()
{
	guard(UActorChannel::Describe);
	if( bClosing || !Actor )
		return FString::Printf( TEXT("Actor=None, ActorClass=%s, bOpenTemporary=%s, bOpenAcked=%s "),
								ActorClass ? ActorClass->GetPathName() : TEXT("None"),
								bOpenTemporary ? TEXT("True") : TEXT("False"), bOpenAcked ? TEXT("True") : TEXT("False") )
				+ UChannel::Describe();
	else
		return FString::Printf(TEXT("Actor=%s (Role=%i RemoteRole=%i) "), Actor->GetFullName(), Actor->Role, Actor->RemoteRole) + UChannel::Describe();
	unguard;
}

IMPLEMENT_CLASS(UActorChannel);

/*-----------------------------------------------------------------------------
	UFileChannel implementation.
-----------------------------------------------------------------------------*/

UFileChannel::UFileChannel()
{
	Download = NULL;
}
void UFileChannel::Init( UNetConnection* InConnection, INT InChannelIndex, INT InOpenedLocally )
{
	guard(UFileChannel::UFileChannel);
	Super::Init( InConnection, InChannelIndex, InOpenedLocally );
	unguard;
}
void UFileChannel::ReceivedBunch( FInBunch& Bunch )
{
	guard(UFileChannel::ReceivedBunch);
	check(!bClosing);
	if( bOpenedLocally )
	{
		// Receiving a file sent from the other side.
		checkSlow(Bunch.GetNumBytes());
		Download->ReceiveData( Bunch.GetData(), Bunch.GetNumBytes() );
	}
	else
	{
		if( !Connection->Driver->AllowDownloads )
		{
			debugf( NAME_DevNet, LocalizeError(TEXT("NetInvalid"),TEXT("Engine")) );
			FOutBunch Bunch( this, 1 );
			SendBunch( &Bunch, 0 );
			return;
		}
		if( SendFileAr )
		{
			FString Cmd;
			Bunch << Cmd;
			if( !Bunch.IsError() && Cmd==TEXT("SKIP") )
			{
				// User cancelled optional file download.
				// Remove it from the package map
				debugf( TEXT("User skipped download of '%s'"), SrcFilename );
				Connection->PackageMap->List->Remove( PackageIndex );
				return;
			}
		}
		else
		{
			// Request to send a file.
			FGuid Guid;
			Bunch << Guid;
			if( !Bunch.IsError() )
			{
				for (INT i = 0; i < Connection->PackageMap->List->Num(); i++)
				{
					FPackageInfo& Info = (*Connection->PackageMap->List)(i);
					if( Info.Guid==Guid && Info.URL!=TEXT("") )
					{
						if( Connection->Driver->MaxDownloadSize>0 &&
							GFileManager->FileSize(*Info.URL)>Connection->Driver->MaxDownloadSize )
							break;							
						appStrncpy( SrcFilename, *Info.URL, ARRAY_COUNT(SrcFilename) );
						if( Connection->Driver->Notify->NotifySendingFile( Connection, Guid ) )
						{
							check(Info.Linker);
							SendFileAr = GFileManager->CreateFileReader( SrcFilename );
							if( SendFileAr )
							{
								// Accepted! Now initiate file sending.
								debugf( NAME_DevNet, LocalizeProgress(TEXT("NetSend"),TEXT("Engine")), SrcFilename );
								PackageIndex = i;
								return;
							}
						}
					}
				}
			}
		}

		// Illegal request; refuse it by closing the channel.
		debugf( NAME_DevNet, LocalizeError(TEXT("NetInvalid"),TEXT("Engine")) );
		FOutBunch Bunch( this, 1 );
		SendBunch( &Bunch, 0 );
	}
	unguard;
}
void UFileChannel::Tick()
{
	guard(UFileChannel::Tick);
	UChannel::Tick();
	Connection->bTimeSensitive = true;
	INT Size;
	//TIM: IsNetReady(1) causes the client's bandwidth to be saturated. Good for clients, very bad
	// for bandwidth-limited servers. IsNetReady(0) caps the clients bandwidth.
	static UBOOL LanPlay = ParseParam(appCmdLine(),TEXT("lanplay"));
	while( !bOpenedLocally && SendFileAr && IsNetReady(LanPlay) && (Size=MaxSendBytes())!=0 )
	{
		// Sending.
		INT Remaining = (*Connection->PackageMap->List)(PackageIndex).FileSize - SentData;
		FOutBunch Bunch( this, Size>=Remaining );
		Size = Min( Size, Remaining );
		BYTE* Buffer = (BYTE*)appAlloca( Size );
		SendFileAr->Serialize( Buffer, Size );
		if( SendFileAr->IsError() )
		{
			//!!
		}
		SentData += Size;
		Bunch.Serialize( Buffer, Size );
		Bunch.SetbReliable( 1 );
		check(!Bunch.IsError());
		SendBunch( &Bunch, 0 );
		Connection->FlushNet();
		if( Bunch.GetbClose() )
		{
			// Finished.
			delete SendFileAr;
			SendFileAr = NULL;
		}
	}
	unguard;
}
void UFileChannel::Destroy()
{
	guard(UFileChannel::~UFileChannel);
	check(Connection);
	if( RouteDestroy() )
		return;
	check(Connection->Channels[ChIndex]==this);

	// Close the file.
	if( SendFileAr )
	{
		delete SendFileAr;
		SendFileAr = NULL;
	}

	// Notify that the receive succeeded or failed.
	if( bOpenedLocally && Download )
	{
		Download->DownloadDone();
		delete Download;
	}
	Super::Destroy();
	unguard;
}
FString UFileChannel::Describe()
{
	guard(UFileChannel::Describe);
	FPackageInfo& Info = (*Connection->PackageMap->List)(PackageIndex);
	return FString::Printf
	(
		TEXT("File='%s', %s=%i/%i "),
		bOpenedLocally ? (Download?Download->TempFilename:TEXT("")): SrcFilename,
		bOpenedLocally ? TEXT("Received") : TEXT("Sent"),
		bOpenedLocally ? (Download?Download->Transfered:0): SentData,
		Info.FileSize
	) + UChannel::Describe();
	unguard;
}
IMPLEMENT_CLASS(UFileChannel)

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

