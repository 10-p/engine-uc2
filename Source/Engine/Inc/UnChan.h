/*=============================================================================
	UnChan.h: Unreal datachannel class.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

//jg -- Constant for all buffers that are reading from the network
#define MAX_STRING_SERIALIZE_SIZE	2048

/**
 * This class provides the common routines for sending/recieving a string
 * on the network connection. First, it does NO memory allocations. Second it
 * provides a simple interface that mimics FString enough that it can be easily
 * used as a FString replacement in the network code. It uses a static buffer
 * for reading and uses the source buffer for sending (no copy).
 */
class ENGINE_API FNetString
{
	/**
	 * The length of the string being read/sent
	 */
	INT Length;
	/**
	 * Pointer to the buffer that holds the data or will get the data
	 */
	const TCHAR* String;

	/** Hide from callers */
	FNetString(void);

public:
	/**
	 * Constructor that sets the members
	 */
	FNetString(INT InLength,const TCHAR* InString) :
		Length(InLength), String(InString)
	{
		check(String);
	}

	/**
	 * Constructor that sets the members for a given string
	 */
	FNetString(const TCHAR* InString) :
		String(InString)
	{
		check(String);
		Length = appStrlen(String);
	}

	/**
	 * Constructor that sets the members for a given FString
	 */
	FNetString(const FString& InString)
	{
		check(InString.Len());
		String = *InString;
		Length = InString.Len();
	}

	/**
	 * Mimic the FString interface for getting access to the data
	 */
	FORCEINLINE const TCHAR* operator*(void)
	{
		return String;
	}

	/**
	 * Allow non-const access
	 */
	FORCEINLINE TCHAR* GetRaw(void)
	{
		return (TCHAR*)String;
	}

	/**
	 * Return the length same as FString
	 */
	FORCEINLINE INT Len(void)
	{
		return Length;
	}

	/**
	 * Method for serializing a FNetString. Note it is more efficient to
	 * send UNICODE data than to convert each character to ANSI and send/read.
	 */
	friend FArchive& operator<<(FArchive& Ar,FNetString& NetString);
};

/**
 * This class just provides a temporary data holder and relies on the parent
 * for everything else
 */
class ENGINE_API FNetStringRead : public FNetString
{
	/**
	 * Temporary buffer that will have the string read into it
	 */
	TCHAR ReadBuffer[MAX_STRING_SERIALIZE_SIZE];

public:
	/**
	 * Default constructor. Just tells the base class where to read into
	 */
	FNetStringRead(void) :
		FNetString(MAX_STRING_SERIALIZE_SIZE,ReadBuffer)
	{
	}
};

/*-----------------------------------------------------------------------------
	UChannel base class.
-----------------------------------------------------------------------------*/
//
// Base class of communication channels.
//
class ENGINE_API UChannel : public UObject
{
	DECLARE_ABSTRACT_CLASS(UChannel,UObject,CLASS_Transient,Engine);

	// Variables.
	UNetConnection*	Connection;			// Owner connection.

	BITFIELD		bOpenAcked:1;		// Whether open has been acknowledged.
	BITFIELD		bClosing:1;			// State of the channel.
	BITFIELD		bOpenTemporary:1;	// Opened temporarily.
	BITFIELD		bBroken:1;			// Has encountered errors and is ignoring subsequent packets.
	BITFIELD		bTornOff:1;			// Actor associated with this channel was torn off
	BITFIELD		bOpenedLocally:1;	// Whether channel was opened locally or by remote.

	_WORD			OpenPacketId;		// Packet the spawn message was sent in.

	EChannelType	ChType;			// Type of this channel.

	BYTE            ChIndex;		// Index of this channel.
	BYTE			NumInRec;		// Number of packets in InRec.
	BYTE			NumOutRec;		// Number of packets in OutRec.

	FInBunch*		InRec;			// Incoming data with queued dependencies.
	FOutBunch*		OutRec;			// Outgoing reliable unacked data.
	
	// Statics.
	static UClass* ChannelClasses[CHTYPE_MAX];
	static UBOOL IsKnownChannelType( INT Type );

	// Constructor.
	UChannel();
	void Destroy();

	// UChannel interface.
	virtual void Init( UNetConnection* InConnection, INT InChIndex, UBOOL InOpenedLocally );
	virtual void SetClosingFlag();
	virtual void Close();
	virtual FString Describe();
	virtual void ReceivedBunch( FInBunch& Bunch )=0;
	virtual void ReceivedNak( INT NakPacketId );
	virtual void Tick();

	// General channel functions.
	INT RouteDestroy();
	void ReceivedAcks();
	UBOOL ReceivedSequencedBunch( FInBunch& Bunch );
	void ReceivedRawBunch( FInBunch& Bunch );
	INT SendBunch( FOutBunch* Bunch, UBOOL Merge );
	INT IsNetReady( UBOOL Saturate );
	void AssertInSequenced();
	INT MaxSendBytes();
};

/*-----------------------------------------------------------------------------
	UControlChannel base class.
-----------------------------------------------------------------------------*/

//
// A channel for exchanging text.
//
class ENGINE_API UControlChannel : public UChannel, public FOutputDevice
{
	DECLARE_CLASS(UControlChannel,UChannel,CLASS_Transient,Engine);

	// Constructor.
	void StaticConstructor()
	{
		guard(UControlChannel::StaticConstructor);
		ChannelClasses[CHTYPE_Control]        = GetClass();
		GetDefault<UControlChannel>()->ChType = CHTYPE_Control;
		unguard;
	}
	UControlChannel();
	void Init( UNetConnection* InConnection, INT InChIndex, UBOOL InOpenedLocally );
	void Destroy();

	// UChannel interface.
	void ReceivedBunch( FInBunch& Bunch );

	// FArchive interface.
	void Serialize( const TCHAR* Data, EName Event );

	// UControlChannel interface.
	FString Describe();
};

/*-----------------------------------------------------------------------------
	UActorChannel.
-----------------------------------------------------------------------------*/

//
// A channel for exchanging actor properties.
//
class ENGINE_API UActorChannel : public UChannel
{
	DECLARE_CLASS(UActorChannel,UChannel,CLASS_Transient,Engine);

	// Variables.
	ULevel*	Level;				// Level this actor channel is associated with.
	AActor* Actor;				// Actor this corresponds to.
	UClass* ActorClass;			// Class of the actor.
	FLOAT	RelevantTime;		// Last time this actor was relevant to client.
	FLOAT	LastUpdateTime;		// Last time this actor was replicated.
	FLOAT	LastFullUpdateTime;	// Last time this actor was fully replicated.

	//epic superville: Made 4 UBOOLs into BITFIELDs
	BITFIELD bSpawnAcked:1;				// Whether spawn has been acknowledged.
    BITFIELD bActorDirty:1;				// Whether actor is dirty
    BITFIELD bActorMustStayDirty:1;		// ActorDirty may not be cleared at end of this tick
    BITFIELD bActorStillInitial:1;		// Not all properties sent while bNetInitial, so still bNetInitial next tick

	TArray<BYTE> RepEval;					// Evaluated replication conditions.
	TArray<INT>  Dirty;						// Properties that are dirty and need resending.
	TArray<FPropertyRetirement> Retirement; // Property retransmission.

	//epic superville:
	// RecentMap stores the offset of a property (DWORD) and the value of the property (Array of bytes)
	TMap< _WORD, TArray<BYTE>* >	RecentMap;

	// Constructor.
	void StaticConstructor()
	{
		guard(UActorChannel::StaticConstructor);
		ChannelClasses[CHTYPE_Actor]        = GetClass();
		GetDefault<UActorChannel>()->ChType = CHTYPE_Actor;
		unguard;
	}
	UActorChannel();
	void Init( UNetConnection* InConnection, INT InChIndex, UBOOL InOpenedLocally );
	void Destroy();

	// UChannel interface.
	void SetClosingFlag();
	void ReceivedBunch( FInBunch& Bunch );
	void ReceivedNak( INT NakPacketId );
	void Close();
	void Tick();

	// UActorChannel interface and accessors.
	AActor* GetActor() {return Actor;}
	FString Describe();
	void ReplicateActor();
	void SetChannelActor( AActor* InActor );

	//epic superville:
	// UActorChannel RecentMap helper functions
	// Retrieve the address of the value stored in RecentMap with the key Offset
	// Returns NULL if can't find it and, if bMayFail is false, logs a warning
	FORCEINLINE BYTE* GetMapValueAddr(INT Offset, UBOOL bMayFail = 0)
	{
		// Get the pointer to the pointer to the array
		TArray<BYTE>** ppArray = RecentMap.Find( Offset );
		// If pointer is valid
		if( ppArray )
		{
			// Return the address of the first BYTE in the array
			return &((**ppArray)(0));
		}
#ifndef FINAL_RELEASE
		else if (!bMayFail)
		{
			const TCHAR* Var = NULL;
			for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(ActorClass); It; ++It )
			{
				if( It->Offset == Offset )
				{
					Var = It->GetName();
				}						
			}

			debugf(TEXT("UActorChannel::GetMapValueAddr - Failed to find Key %d Var %s Actor %s"), Offset, Var ? Var : TEXT("Unfound??"), Actor ? Actor->GetName() : TEXT("NULL"));
			debugf(TEXT("Variable is either - A) Not in a script replication statement or B) Not listed in native CreateRecentMap for that actor class (only for specific classes)"));
		}
#endif // !FINAL_RELEASE


		// Otherwise, couldn't find anything
		return NULL;
	}
};

/*-----------------------------------------------------------------------------
	File transfer channel.
-----------------------------------------------------------------------------*/

//
// A channel for exchanging binary files.
//
class ENGINE_API UFileChannel : public UChannel
{
	DECLARE_CLASS(UFileChannel,UChannel,CLASS_Transient,Engine);

	// Receive Variables.
	UChannelDownload*	Download;		 // UDownload when receiving.

	// Send Variables.
	FArchive*			SendFileAr;		 // File being sent.
	TCHAR				SrcFilename[256];// Filename being sent.
	INT					PackageIndex;	 // Index of package in map.
	INT					SentData;		 // Number of bytes sent.

	// Constructor.
	void StaticConstructor()
	{
		guard(UFileChannel::StaticConstructor);
		ChannelClasses[CHTYPE_File]        = GetClass();
		GetDefault<UFileChannel>()->ChType = CHTYPE_File;
		unguard;
	}
	UFileChannel();
	void Init( UNetConnection* InConnection, INT InChIndex, UBOOL InOpenedLocally );
	void Destroy();

	// UChannel interface.
	void ReceivedBunch( FInBunch& Bunch );

	// UFileChannel interface.
	FString Describe();
	void Tick();
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

