/*=============================================================================
	UnConn.h: Unreal network connection base class.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

class UNetDriver;

/*-----------------------------------------------------------------------------
	UNetConnection.
-----------------------------------------------------------------------------*/

//
// Whether to support net lag and packet loss testing.
//
#define DO_ENABLE_NET_TEST 1

//
// State of a connection.
//
enum EConnectionState
{
	USOCK_Invalid   = 0, // Connection is invalid, possibly uninitialized.
	USOCK_Closed    = 1, // Connection permanently closed.
	USOCK_Pending	= 2, // Connection is awaiting connection.
	USOCK_Open      = 3, // Connection is open.
};

#if DO_ENABLE_NET_TEST
//
// A lagged packet
//
struct DelayedPacket
{
	TArray<BYTE> Data;
	DOUBLE SendTime;
};
#endif

struct FDownloadInfo
{
	UClass* Class;
	FString ClassName;
	FString Params;
	UBOOL Compression;
};

//
// A network connection.
//
class ENGINE_API UNetConnection : public UPlayer
{
	DECLARE_ABSTRACT_CLASS(UNetConnection,UPlayer,CLASS_Transient|CLASS_Config,Engine)

	// Constants.
	enum{ MAX_PROTOCOL_VERSION = 1     };	// Maximum protocol version supported.
	enum{ MIN_PROTOCOL_VERSION = 1     };	// Minimum protocol version supported.
	enum{ MAX_CHANNELS         = 255   };	// Maximum channels.
	//epic bcl -- Timeout for retrying a follow on XboxLive
	enum{ MAX_DEAD_AIR_BEFORE_RETRY = 15};	// Time (in s) after which an attempt to retry a pending level will begin

	// Connection information.
	UNetDriver*		Driver;					// Owning driver.
	EConnectionState State;					// State this connection is in.
	FURL			URL;					// URL of the other side.
	UPackageMap*	PackageMap;				// Package map between local and remote.

	// Negotiated parameters.
	BYTE			ProtocolVersion;		// Protocol version we're communicating with (<=PROTOCOL_VERSION).
	_WORD			MaxPacket;				// Maximum packet size.
	BYTE			PacketOverhead;			// Bytes overhead per packet sent.
	INT				Challenge;				// Server-generated challenge.
	_WORD			NegotiatedVer;			// Negotiated version for new channels.
	INT				UserFlags;				// User-specified flags.
	FStringNoInit	RequestURL;				// URL requested by client

	//scion bcl -- Promoted from static variable so more than one match can
	// be followed.
	BITFIELD			bTryLastMatch:1;		// Have we retried getting the current match? Useful for Xboxen
	//From "Negotiated parameters" section
	BITFIELD			bInternalAck:1;			// Internally ack all packets, for 100% reliable connections.
	//From "Merge info" section
	BITFIELD			bAllowMerge:1;			// Whether to allow merging.
	BITFIELD			bTimeSensitive:1;		// Whether contents are time-sensitive.
	//From "Internal" section
	BITFIELD			bPassedChallenge:1;		// Whether client has passed the challenge/response mechanism.
	BITFIELD			bForceTimeout:1;		// reduce timeout to zero and begin reconnect sequence (used for speeding up server travels on Xbox)
	//From "Cheat Protection" section
	BITFIELD			bValidated:1;			// Has this connection been validated

	// CD key authentication
    FString			CDKeyHash;				// Hash of client's CD key
	FString			CDKeyResponse;			// Client's response to CD key challenge

	// Internal.
	FLOAT			LastReceiveTime;		// Last time a packet was received, for timeout checking.
	FLOAT			LastSendTime;			// Last time a packet was sent, for keepalives.
	FLOAT			LastTickTime;			// Last time of polling.
	FLOAT			LastRepTime;			// Time of last replication.
	INT				QueuedBytes;			// Bytes assumed to be queued up.
	INT				TickCount;				// Count of ticks.

	// Merge info.
	FBitWriterMark  LastStart;				// Most recently sent bunch start.
	FBitWriterMark  LastEnd;				// Most recently sent bunch end.
	FOutBunch*		LastOutBunch;			// Most recent outgoing bunch.
	FOutBunch		LastOut;

	// Stat display.
	FLOAT			StatUpdateTime;			// Time of last stat update.
	FLOAT			StatPeriod;				// Interval between gathering stats.
	FLOAT			InRate,    OutRate;		// Rate for last interval.
	FLOAT			InPackets, OutPackets;	// Packet counts.
	FLOAT			InBunches, OutBunches;	// Bunch counts.
	FLOAT			InLoss,    OutLoss;		// Packet loss percent.
	FLOAT			InOrder,   OutOrder;	// Out of order incoming packets.
	FLOAT			BestLag,   AvgLag;		// Lag.

	// Stat accumulators.
	FLOAT			LagAcc, BestLagAcc;		// Previous msec lag.
	INT				InLossAcc, OutLossAcc;	// Packet loss accumulator.
	INT				InPktAcc,  OutPktAcc;	// Packet accumulator.
	INT				InBunAcc,  OutBunAcc;	// Bunch accumulator.
	INT				InByteAcc, OutByteAcc;	// Byte accumulator.
	INT				InOrdAcc,  OutOrdAcc;	// Out of order accumulator.
	INT				LagCount;				// Counter for lag measurement.
	INT				HighLossCount;			// Counts high packet loss.
	FLOAT			LastTime, FrameTime;	// Monitors frame time.
	FLOAT			CumulativeTime, AverageFrameTime;
	INT				CountedFrames;

	// Packet.
	FBitWriter		Out;					// Outgoing packet.
	FLOAT			OutLagTime[256];		// For lag measuring.
	//superville: Converted from INTs to _WORDs since MAX_PACKETID is 16384
	// Zero reserved for invalid id
	_WORD			OutLagPacketId[256];	// For lag measuring.
	_WORD			InPacketId;				// Full incoming packet index.
	_WORD			OutPacketId;			// Most recently sent packet.
	_WORD 			OutAckPacketId;			// Most recently acked outgoing packet.

	// Channel table.
	UChannel*	Channels     [ MAX_CHANNELS ];
	INT			OutReliable  [ MAX_CHANNELS ];
	INT			InReliable   [ MAX_CHANNELS ];
	INT			PendingOutRec[ MAX_CHANNELS ];	// Outgoing reliable unacked data from previous (now destroyed) channel in this slot.  This contains the first chsequence not acked
	TArray<INT> QueuedAcks, ResendAcks;
	TArray<UChannel*> OpenChannels;
	TArray<AActor*> SentTemporaries;
	TMap<AActor*,UActorChannel*> ActorChannels;

	// File Download
	UDownload*				Download;
	TArray<FDownloadInfo>	DownloadInfo;

	// Cheat Protection
	INT		CurrentPackageCount;	// How many packages have been validated
	INT		ExpectedPackageCount;	// # of packages expected from the server

	AActor*  Viewer;
	AActor **OwnedConsiderList;
	INT OwnedConsiderListSize;

	// scion sz - ripped from UC1 for child splitscreen client connections
	TArray<UChildConnection*>   Children;

#if DO_ENABLE_NET_TEST
	// For development.
	INT				PktLoss;
	INT				PktOrder;
	INT				PktDup;
	INT				PktLag;
	TArray<DelayedPacket> Delayed;
#endif

	// Constructors and destructors.
	UNetConnection();
	UNetConnection( UNetDriver* Driver, const FURL& InURL );

	// UObject interface.
	void Serialize( FArchive& Ar );
	virtual void Destroy();

	// UPlayer interface.
	void ReadInput( FLOAT DeltaSeconds );

	// FArchive interface.
	void Serialize( const TCHAR* Data, EName MsgType );

	// FExec interface.
	UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar=*GLog );

	// UNetConnection interface.
	virtual FString LowLevelGetRemoteAddress()=0;
	virtual FString LowLevelDescribe()=0;
	virtual void LowLevelSend( void* Data, INT Count )=0; //!! "Looks like an FArchive"
	virtual void InitOut();
	virtual void AssertValid();
	virtual void SendAck( INT PacketId, UBOOL FirstTime=1 );
	virtual void FlushNet();
	virtual void Tick();
	virtual INT IsNetReady( UBOOL Saturate );
	virtual void HandleClientPlayer( APlayerController* PC );
	void SetActorDirty( AActor* DirtyActor );

	UBOOL	IsLoadingAssets() const;

	// Functions.
	void PurgeAcks();
	void SendPackageMap();
	void PreSend( INT SizeBits );
	void PostSend();
	virtual void ReceivedRawPacket( void* Data, INT Count );//!! "looks like an FArchive"
	INT SendRawBunch( FOutBunch& Bunch, UBOOL InAllowMerge );
	UNetDriver* GetDriver() {return Driver;}
	class UControlChannel* GetControlChannel();
	UChannel* CreateChannel( enum EChannelType Type, UBOOL bOpenedLocally, INT ChannelIndex=INDEX_NONE );
	void ReceivedPacket( FBitReader& Reader );
	void ReceivedNak( INT NakPacketId );
	void ReceiveFile( INT PackageIndex );
	void SlowAssertValid()
	{
#if DO_GUARD_SLOW
		AssertValid();
#endif
	}
	//epic rwd - added for voice packet support
	virtual UBOOL HasPendingData();

#ifdef _XBOX
	//epic rwd - voice support
	UNetConnection* GetServerConnection() { return (Driver != NULL ? Driver->ServerConnection : NULL); }
#endif

	virtual INT CalcPacketOverhead(INT PacketSize);
};

// scion sz -
// ripped from UC1.
// Client connections that join a game on the server create
// child connections based on the existing client connection.
class ENGINE_API UChildConnection : public UNetConnection
{
public:
	DECLARE_CLASS(UChildConnection,UNetConnection,CLASS_Transient|CLASS_Config,Engine)

	UNetConnection*     Parent;

	// UObject Interface
	virtual void Destroy()
	{
		check( OpenChannels.Num() == 0 );
		return Super::Destroy();
	}

	// UNetConnection interface.
	virtual FString LowLevelGetRemoteAddress()
	{
		return Parent->LowLevelGetRemoteAddress();
	}
	virtual FString LowLevelDescribe()
	{
		return Parent->LowLevelDescribe();
	}
	virtual void LowLevelSend( void* Data, INT Count )
	{
		//Parent->LowLevelSend(Data, Count);
	}
	virtual void InitOut()
	{
		Parent->InitOut();
	}
	virtual void AssertValid()
	{
		Parent->AssertValid();
	}
	virtual void SendAck( INT PacketId, UBOOL FirstTime=1 )
	{
		//Parent->SendAck( PacketId, FirstTime );
	}
	virtual void FlushNet()
	{
		Parent->FlushNet();
	}
	virtual INT IsNetReady( UBOOL Saturate )
	{
		return Parent->IsNetReady(Saturate);
	}
	void SetActorDirty( AActor* DirtyActor )
	{
		Parent->SetActorDirty(DirtyActor);
	}
	virtual void Tick();
	virtual void HandleClientPlayer( APlayerController* PC );
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

