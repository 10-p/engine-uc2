/*=============================================================================
	UnBunch.h: Unreal bunch class.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

//
// A bunch of data to send.
//
class ENGINE_API FOutBunch : public FBitWriter
{
public:
	// Variables.
	FOutBunch*		Next;
	UChannel*		Channel;
	FLOAT			Time;

private:
	//epic jg/superville:
	// Top _WORD is the PacketID bottom word contains five bytes
	// (bOpen/bClose/bReliable/bReceivedAck/ChannelIndex)
	DWORD			BunchInfo;	

public:
	_WORD			ChSequence;
	BYTE			ChType;

	//epic jg/superville:
	// All accessor functions for packed properties

	FORCEINLINE void SetPacketID(_WORD PacketID)
	{
		BunchInfo = (((DWORD)PacketID) << 16) | (BunchInfo & 0x0000FFFF);
	}
	FORCEINLINE _WORD GetPacketID(void)
	{
		return (BunchInfo >> 16) & 0x0000FFFF;
	}
	FORCEINLINE void SetChannelIndex(BYTE ChannelID)
	{
		BunchInfo = (BunchInfo & 0xFFFFFF00) | ChannelID;
	}
	FORCEINLINE BYTE GetChannelIndex(void)
	{
		return 	BunchInfo & 0x000000FF;
	}
	FORCEINLINE void SetbOpen(UBOOL bOpen)
	{
		BunchInfo = ((BunchInfo & (~(1 << 9))) | (bOpen << 9));
	}
	FORCEINLINE UBOOL GetbOpen(void)
	{
		return (BunchInfo & (1 << 9)) ? 1 : 0;
	}
	FORCEINLINE void SetbClose(UBOOL bClose)
	{
		BunchInfo = ((BunchInfo & (~(1 << 10))) | (bClose << 10));
	}
	FORCEINLINE UBOOL GetbClose(void)
	{
		return (BunchInfo & (1 << 10)) ? 1 : 0;
	}
	FORCEINLINE void SetbReliable(UBOOL bReliable)
	{
		BunchInfo = ((BunchInfo & (~(1 << 11))) | (bReliable << 11));
	}
	FORCEINLINE UBOOL GetbReliable(void)
	{
		return (BunchInfo & (1 << 11)) ? 1 : 0;
	}
	FORCEINLINE void SetbReceivedAck(UBOOL bReceivedAck)
	{
		BunchInfo = ((BunchInfo & (~(1 << 12))) | (bReceivedAck << 12));
	}
	FORCEINLINE UBOOL GetbReceivedAck(void)
	{
		return (BunchInfo & (1 << 12)) ? 1 : 0;
	}

	// Functions.
	FOutBunch();
	FOutBunch( UChannel* InChannel, UBOOL bClose );
	FArchive& operator<<( FName& Name );
	FArchive& operator<<( UObject*& Object );

	/**
	 * jg -- Provide a better assignment operator because 3.3% of our CPU
	 * going to this is silly
	 */
	FORCEINLINE FOutBunch& operator=(const FOutBunch& In)
	{
		guard(FOutBunch::operator=);
		// Skip if we are the same
		if (&In != this)
		{
			FBitWriter::operator=(In);
			// Now memcpy our fields for speed
			appMemcpy(&Next,&In.Next,sizeof(FOutBunch) - sizeof(FBitWriter));
		}
		return *this;
		unguard;
	}
};

//
// A bunch of data received from a channel.
//
class ENGINE_API FInBunch : public FBitReader
{
public:
	// Variables.
	FInBunch*		Next;
	UNetConnection*	Connection;
	_WORD			ChType;
	_WORD			ChSequence;

private:
	//epic jg/superville:
	// Top _WORD is the PacketID bottom word contains all four byte
	// (bOpen/bClose/bReliable/ChannelIndex)
	DWORD BunchInfo;

public:
	//epic jg/superville:
	// All accessor functions for packed properties
	FORCEINLINE void SetPacketID(_WORD PacketID)
	{
		BunchInfo = (((DWORD)PacketID) << 16) | (BunchInfo & 0x0000FFFF);
	}
	FORCEINLINE _WORD GetPacketID(void)
	{
		return (BunchInfo >> 16) & 0x0000FFFF;
	}
	FORCEINLINE void SetChannelIndex(BYTE ChannelID)
	{
		BunchInfo = (BunchInfo & 0xFFFFFF00) | ChannelID;
	}
	FORCEINLINE BYTE GetChannelIndex(void)
	{
		return 	BunchInfo & 0x000000FF;
	}
	FORCEINLINE void SetbOpen(UBOOL bOpen)
	{
		BunchInfo = ((BunchInfo & (~(1 << 9))) | (bOpen << 9));
	}
	FORCEINLINE UBOOL GetbOpen(void)
	{
		return (BunchInfo & (1 << 9)) ? 1 : 0;
	}
	FORCEINLINE void SetbClose(UBOOL bClose)
	{
		BunchInfo = ((BunchInfo & (~(1 << 10))) | (bClose << 10));
	}
	FORCEINLINE UBOOL GetbClose(void)
	{
		return (BunchInfo & (1 << 10)) ? 1 : 0;
	}
	FORCEINLINE void SetbReliable(UBOOL bReliable)
	{
		BunchInfo = ((BunchInfo & (~(1 << 11))) | (bReliable << 11));
	}
	FORCEINLINE UBOOL GetbReliable(void)
	{
		return (BunchInfo & (1 << 11)) ? 1 : 0;
	}

	// Functions.
	FInBunch( UNetConnection* InConnection );
	FArchive& operator<<( FName& Name );
	FArchive& operator<<( UObject*& Object );
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

