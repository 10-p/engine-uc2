/*=============================================================================
	UnBits.h: Unreal bitstream manipulation classes.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

/*-----------------------------------------------------------------------------
	FBitWriter.
-----------------------------------------------------------------------------*/

//
// Writes bitstreams.
//
struct CORE_API FBitWriter : public FArchive
{
	friend struct FBitWriterMark;
public:
	/**
	 * Default constructor. Zeros everything.
	 */
	FBitWriter(void);
	/**
	 * Constructor using known size the buffer needs to be
	 */
	FBitWriter( INT InByteCount );
	void SerializeBits( void* Src, INT LengthBits );
	void SerializeInt( DWORD& Value, DWORD Max );
	void WriteInt( DWORD Result, DWORD Max );
	void WriteBit( BYTE In );
	void Serialize( void* Src, INT LengthBytes );
	/**
	 * Holds the originally requested byte count for fast comparisons
	 */
	FORCEINLINE INT GetByteCount(void)
	{
		return ByteCount;
	}

	/**
	 * Returns a pointer to our internal buffer
	 */
	FORCEINLINE BYTE* GetData(void)
	{
		return &Buffer(0);
	}

	/**
	 * Returns the number of bytes written
	 */
	FORCEINLINE INT GetNumBytes(void)
	{
		return (Num+7)>>3;
	}

	/**
	 * Returns the number of bits written
	 */
	FORCEINLINE INT GetNumBits(void)
	{
		return Num;
	}

	/**
	 * Returns the number of bits the buffer supports
	 */
	FORCEINLINE INT GetMaxBits(void)
	{
		return Max;
	}

	/**
	 * Marks this bit writer as overflowed
	 */
	FORCEINLINE void SetOverflowed(void)
	{
		ArIsError = 1;
	}

	/**
	 * jg -- This is a faster assignment operator than what the compiler
	 * generates. Also, the tarray copy was very slow given that it was
	 * an array of bytes.
	 */
	FORCEINLINE FBitWriter& operator=(const FBitWriter& In)
	{
		// Simple copies
		Num = In.Num;
		Max = In.Max;
		ByteCount = In.ByteCount;
		// Use optimized tarray copy
		Buffer.FastCopy(In.Buffer);
		return *this;
	}

	/**
	 * Resets the bit writer back to its initial state
	 */
	void Reset(void);

private:
	TArray<BYTE>	Buffer; // Array of bytes large enough to hold "Max" bits
	INT				Num;	// Number of bits written already
	INT				Max;	// Number of bits we can hold (set through constructor InMaxBits)
	INT				ByteCount;
};

//
// For pushing and popping FBitWriter positions.
//
struct CORE_API FBitWriterMark
{
public:
	FBitWriterMark()
	:	Num         ( 0 )
	{}
	FBitWriterMark( FBitWriter& Writer )
	:	Overflowed	( Writer.ArIsError )
	,	Num			( Writer.Num )
	{}
	INT GetNumBits()
	{
		return Num;
	}
	void Pop( FBitWriter& Writer );
private:
	UBOOL			Overflowed;
	INT				Num;
};

/*-----------------------------------------------------------------------------
	FBitReader.
-----------------------------------------------------------------------------*/

//
// Reads bitstreams.
//
struct CORE_API FBitReader : public FArchive
{
public:
	FBitReader( BYTE* Src=NULL, INT CountBits=0 );
	void SetData( FBitReader& Src, INT CountBits );
	void SerializeBits( void* Dest, INT LengthBits );
	void SerializeInt( DWORD& Value, DWORD Max );
	DWORD ReadInt( DWORD Max );
	BYTE ReadBit();
	void Serialize( void* Dest, INT LengthBytes );
	BYTE* GetData();
	UBOOL AtEnd();
	void SetOverflowed();
	INT GetNumBytes();
	INT GetNumBits();
	INT GetPosBits();
private:
	TArray<BYTE> Buffer;
	INT   Num;
	INT   Pos;
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

