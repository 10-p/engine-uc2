/*=============================================================================
	UnBits.h: Unreal bitstream manipulation classes.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "CorePrivate.h" 

// Table.
static BYTE GShift[8]={0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80};
static BYTE GMask [8]={0x00,0x01,0x03,0x07,0x0f,0x1f,0x3f,0x7f};

// Optimized arbitrary bit range memory copy routine.
void appBitsCpy( BYTE* Dest, INT DestBit, BYTE* Src, INT SrcBit, INT BitCount )
{
	if( BitCount==0 ) return;
	
	// Special case - always at least one bit to copy,
	// a maximum of 2 bytes to read, 2 to write - only touch bytes that are actually used.
	if( BitCount <= 8 ) 
	{
		DWORD DestIndex	   = DestBit/8;
		DWORD SrcIndex	   = SrcBit /8;
		DWORD LastDest	   =( DestBit+BitCount-1 )/8;  
		DWORD LastSrc	   =( SrcBit +BitCount-1 )/8;  
		DWORD ShiftSrc     = SrcBit & 7; 
		DWORD ShiftDest    = DestBit & 7;
		DWORD FirstMask    = 0xFF << ShiftDest;  
		DWORD LastMask     = 0xFE << ((DestBit + BitCount-1) & 7) ; // Pre-shifted left by 1.	
		DWORD Accu;		

		if( SrcIndex == LastSrc )
			Accu = (Src[SrcIndex] >> ShiftSrc); 
		else
			Accu =( (Src[SrcIndex] >> ShiftSrc) | (Src[LastSrc ] << (8-ShiftSrc)) );			

		if( DestIndex == LastDest )
		{
			DWORD MultiMask = FirstMask & ~LastMask;
			Dest[DestIndex] = ( ( Dest[DestIndex] & ~MultiMask ) | ((Accu << ShiftDest) & MultiMask) );		
		}
		else
		{		
			Dest[DestIndex] = (BYTE)( ( Dest[DestIndex] & ~FirstMask ) | (( Accu << ShiftDest) & FirstMask) ) ;
			Dest[LastDest ] = (BYTE)( ( Dest[LastDest ] & LastMask  )  | (( Accu >> (8-ShiftDest)) & ~LastMask) ) ;
		}

		return;
	}

	// Main copier, uses byte sized shifting. Minimum size is 9 bits, so at least 2 reads and 2 writes.
	DWORD DestIndex		= DestBit/8;
	DWORD FirstSrcMask  = 0xFF << ( DestBit & 7);  
	DWORD LastDest		= ( DestBit+BitCount )/8; 
	DWORD LastSrcMask   = 0xFF << ((DestBit + BitCount) & 7); 
	DWORD SrcIndex		= SrcBit/8;
	DWORD LastSrc		= ( SrcBit+BitCount )/8;  
	INT   ShiftCount    = (DestBit & 7) - (SrcBit & 7); 
	INT   DestLoop      = LastDest-DestIndex; 
	INT   SrcLoop       = LastSrc -SrcIndex;  
	DWORD FullLoop;
	DWORD BitAccu;

	// Lead-in needs to read 1 or 2 source bytes depending on alignment.
	if( ShiftCount>=0 )
	{
		FullLoop  = Max(DestLoop, SrcLoop);  
		BitAccu   = Src[SrcIndex] << ShiftCount; 
		ShiftCount += 8; //prepare for the inner loop.
	}
	else
	{
		ShiftCount +=8; // turn shifts -7..-1 into +1..+7
		FullLoop  = Max(DestLoop, SrcLoop-1);  
		BitAccu   = Src[SrcIndex] << ShiftCount; 
		SrcIndex++;		
		ShiftCount += 8; // Prepare for inner loop.  
		BitAccu = ( ( (DWORD)Src[SrcIndex] << ShiftCount ) + (BitAccu)) >> 8; 
	}

	// Lead-in - first copy.
	Dest[DestIndex] = (BYTE) (( BitAccu & FirstSrcMask) | ( Dest[DestIndex] &  ~FirstSrcMask ) );
	SrcIndex++;
	DestIndex++;

	// Fast inner loop. 
	for(; FullLoop>1; FullLoop--) 
	{   // ShiftCount ranges from 8 to 15 - all reads are relevant.
		BitAccu = (( (DWORD)Src[SrcIndex] << ShiftCount ) + (BitAccu)) >> 8; // Copy in the new, discard the old.
		SrcIndex++;
		Dest[DestIndex] = (BYTE) BitAccu;  // Copy low 8 bits.
		DestIndex++;		
	}

	// Lead-out. 
	if( LastSrcMask != 0xFF) 
	{
		if ((DWORD)(SrcBit+BitCount-1)/8 == SrcIndex ) // Last legal byte ?
		{
			BitAccu = ( ( (DWORD)Src[SrcIndex] << ShiftCount ) + (BitAccu)) >> 8; 
		}
		else
		{
			BitAccu = BitAccu >> 8; 
		}		

		Dest[DestIndex] = (BYTE)( ( Dest[DestIndex] & LastSrcMask ) | (BitAccu & ~LastSrcMask) );  		
	}	
}

/*-----------------------------------------------------------------------------
	FBitWriter.
-----------------------------------------------------------------------------*/

/**
 * Constructor using known size the buffer needs to be
 */
FBitWriter::FBitWriter( INT InByteCount )
:	Num			( 0 )
,	Max			( InByteCount * 8 )
,	Buffer		( ((InByteCount * 8) + 7) >> 3 )
,	ByteCount(InByteCount)
{
	guard(FBitWriter::FBitWriter);
	appMemzero( &Buffer(0), Buffer.Num() );
	ArIsPersistent = ArIsSaving = 1;
	ArNetVer |= 0x80000000;
	unguard;
}

/**
 * Default constructor. Zeros everything.
 */
FBitWriter::FBitWriter(void) : Num(0), Max(0), ByteCount(0)
{
}

/**
 * Resets the bit writer back to its initial state
 */
void FBitWriter::Reset(void)
{
	FArchive::Reset();
	Num = 0;
	appMemzero( &Buffer(0), Buffer.Num() );
	ArIsPersistent = ArIsSaving = 1;
	ArNetVer |= 0x80000000;
}

//epic ===========================================================
//	Epic Function: SerializeBits
//	Author: someone (commented: superville)
//
//	Description: Writes bits from Src into Buffer array and 
//		does bookkeeping on bit write counts.  Checks for overflow
//
//	Input:	void*	Src			- Ptr to location of bits to write
//			INT		LengthBits	- Number of bits starting at the Src address
//
//	Notes:
//	Last Modified: 02/13/05 (superville)
// ================================================================
void FBitWriter::SerializeBits( void* Src, INT LengthBits )
{
	guardSlow(FBitWriter::SerializeBits);

	// If we have enough space to write into
	if( Num + LengthBits <= Max )
	{
		// If we are only writing 1 bit
		if( LengthBits == 1 )
		{
			// If the 1 spot of Src is marked
			// Mark the bit in the buffer
			// (otherwise, don't mark it, but Num will update and leave it set to zero)
			if( ((BYTE*)Src)[0] & 0x01 )
			{
				// Set the flag by shifting our current number of bits by 3 
				// (dividing by 8) to get the byte we are want to write to
				// Then OR that byte with the proper bit (stored in GShift)
				// GShift[8]={0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80};
				Buffer(Num>>3) |= GShift[Num&7];
			}

			// Update number of bits written
			Num++;
		}
		else
		{
			// Otherwise, if there is more than one bit to write
			// Copy the bits through optimized bit range copy routine
			appBitsCpy( &Buffer(0), Num, (BYTE*)Src, 0, LengthBits);
			// Update the number of bits written
			Num += LengthBits;
		}
	}
	else 
	{
		//debug
//		debugf( TEXT("FBitWriter::SerializeBits Error Src %x Len %i"), Src, LengthBits );

		// Otherwise, we overflowed... mark the error flag
		ArIsError = 1;
	}
	unguardSlow;
}
void FBitWriter::Serialize( void* Src, INT LengthBytes )
{
	guardSlow(FBitWriter::Serialize);
	//warning: Copied and pasted from FBitWriter::SerializeBits
	INT LengthBits = LengthBytes*8;
	if( Num+LengthBits<=Max )
	{
		appBitsCpy( &Buffer(0), Num, (BYTE*)Src, 0, LengthBits);
		Num += LengthBits;
	}
	else
	{
		//debug
//		debugf( TEXT("FBitWriter::Serialize Error Src %x Len %i"), Src, LengthBits );

		ArIsError = 1;
	}

	unguardSlow;
}
void FBitWriter::SerializeInt( DWORD& Value, DWORD ValueMax )
{
	guardSlow(FBitWriter::SerializeInt);
	//checkSlow(Value<ValueMax);

	if( Num+appCeilLogTwo(ValueMax)<=Max )
	{
		DWORD NewValue=0;
		for( DWORD Mask=1; NewValue+Mask<ValueMax && Mask; Mask*=2,Num++ )
		{
			if( Value&Mask )
			{
				Buffer(Num>>3) += GShift[Num&7];
				NewValue += Mask;
			}
		}
	} 
	else
	{
		//debug
//		debugf( TEXT("FBitWriter::SerializeInt Error Value %d Max %d"), Value, ValueMax );

		ArIsError = 1;
	}
	unguardSlow;
}

//epic ===========================================================
//	Epic Function: WriteInt
//	Author: someone (commented: superville)
//
//	Description: Writes Value into Buffer array and 
//		does bookkeeping on bit write counts.  Checks for overflow
//
//	Input:	DWORD	Value	 - (up to) 4 byte value to write into buffer
//								Will only use the bits it needs
//			DWORD	ValueMax - Maximum value to write (will drop anything after)
//								Used to compute number of bits to write
//
//	Notes:
//	Last Modified: 02/13/05 (superville)
// ================================================================
void FBitWriter::WriteInt( DWORD Value, DWORD ValueMax )
{
	guardSlow(FBitWriter::WriteInt);

	//warning: Copied and pasted from FBitWriter::SerializeInt
	// Make sure we have enough room in the buffer
	// appCeilLogTwo is used to compute the number of bits needed to store ValueMax
	if( Num + appCeilLogTwo( ValueMax ) <= Max )
	{
		// NewValue acts as a counter
		// Bits are written the lowests bit at a time and then shifted (?)
		DWORD NewValue = 0;

		// Starting with the first bit and moving to the next bit each loop
		// And update the number of bits written
		// Continue until the bit value considered is greater than ValueMax
		for( DWORD Mask = 1; NewValue + Mask < ValueMax && Mask; Mask *= 2, Num++ )
		{
			// See if the current bit in Value is marked
			// (Otherwise, leave the buffer bit as zero and update the Num counter)
			if( Value & Mask )
			{
				// If so,...
				// Set the flag by shifting our current number of bits by 3 
				// (dividing by 8) to get the byte we are want to write to
				// Then OR that byte with the proper bit (stored in GShift)
				// GShift[8]={0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80};
				Buffer(Num>>3) += GShift[Num&7];

				// Update our considered value (when this exceeds ValueMax, exit loop)
				NewValue += Mask;
			}
		}
	} 
	else 
	{
		//debug
//		debugf( TEXT("FBitWriter::WriteInt Error Value %d Max %d"), Value, ValueMax );

		// Otherwise, we overflowed... mark the error flag
		ArIsError = 1;
	}

	unguardSlow;
}
void FBitWriter::WriteBit( BYTE In )
{
	guardSlow(FBitWriter::WriteBit);
	if( Num+1<=Max )
	{
		if( In )
			Buffer(Num>>3) |= GShift[Num&7];
		Num++;
	}
	else
	{
		//debug
//		debugf( TEXT("FBitWriter::WriteBit Byte %d"), In );

		ArIsError = 1;
	}
	unguardSlow;
}

/*-----------------------------------------------------------------------------
	FBitWriterMark.
-----------------------------------------------------------------------------*/

void FBitWriterMark::Pop( FBitWriter& Writer )
{
	guardSlow(FBitWriterMark::Pop);
	checkSlow(Num<=Writer.Num);
	checkSlow(Num<=Writer.Max);

	if( Num&7 )
		Writer.Buffer(Num>>3) &= GMask[Num&7];
	INT Start = (Num       +7)>>3;
	INT End   = (Writer.Num+7)>>3;
	appMemzero( &Writer.Buffer(Start), End-Start );

	//debug
//	if( Overflowed )
//		debugf( TEXT("FBitWriterMark::Pop Overflowed") );

	Writer.ArIsError = Overflowed;
	Writer.Num       = Num;

	unguardSlow;
}

/*-----------------------------------------------------------------------------
	FBitReader.
-----------------------------------------------------------------------------*/

//
// Reads bitstreams.
//
FBitReader::FBitReader( BYTE* Src, INT CountBits )
:	Num			( CountBits )
,	Buffer		( (CountBits+7)>>3 )
,	Pos			( 0 )
{
	guard(FBitReader::FBitReader);
	ArIsPersistent = ArIsLoading = 1;
	ArNetVer |= 0x80000000;
	if( Src )
		appMemcpy( &Buffer(0), Src, (CountBits+7)>>3 );
	unguard;
}
void FBitReader::SetData( FBitReader& Src, INT CountBits )
{
	guard(FBitReader::SetData);
	Num        = CountBits;
	Pos        = 0;
	ArIsError  = 0;
	Buffer.Empty();
	Buffer.Add( (CountBits+7)>>3 );
	Src.SerializeBits( &Buffer(0), CountBits );
	unguard;
}
void FBitReader::SerializeBits( void* Dest, INT LengthBits )
{
	guardSlow(FBitReader::SerializeBits);
	appMemzero( Dest, (LengthBits+7)>>3 );
	if( Pos+LengthBits<=Num )
	{
		//for( INT i=0; i<LengthBits; i++,Pos++ )
		//	if( Buffer(Pos>>3) & GShift[Pos&7] )
		//		((BYTE*)Dest)[i>>3] |= GShift[i&7];
		if( LengthBits == 1 )
		{
			if( Buffer(Pos>>3) & GShift[Pos&7] )
				((BYTE*)Dest)[0] |= 0x01;
			Pos++;
		}
		else
		{
			appBitsCpy( (BYTE*)Dest, 0, &Buffer(0), Pos, LengthBits );
			Pos += LengthBits;
		}
	}
	else
	{
		SetOverflowed();
	}

	unguardSlow;
}
void FBitReader::SerializeInt( DWORD& Value, DWORD ValueMax )
{
	guardSlow(FBitReader::SerializeInt);
	Value=0;
	for( DWORD Mask=1; Value+Mask<ValueMax && Mask; Mask*=2,Pos++ )
	{
		if( Pos>=Num )
		{
			//debug
//			debugf( TEXT("FBitReader::SerializeInt Error Value %d Max %d"), Value, ValueMax );

			ArIsError = 1;
			break;
		}
		if( Buffer(Pos>>3) & GShift[Pos&7] )
		{
			Value |= Mask;
		}
	}
	unguardSlow;
}
DWORD FBitReader::ReadInt( DWORD Max )
{
	guardSlow(FBitReader::ReadInt);
	DWORD Value=0;
	SerializeInt( Value, Max );
	return Value;
	unguardSlow;
}
BYTE FBitReader::ReadBit()
{
	guardSlow(FBitReader::ReadBit);
	BYTE Bit=0;
	SerializeBits( &Bit, 1 );
	return Bit;
	unguardSlow;
}
void FBitReader::Serialize( void* Dest, INT LengthBytes )
{
	guardSlow(FBitReader::Serialize);
	SerializeBits( Dest, LengthBytes*8 );
	unguardSlow;
}
BYTE* FBitReader::GetData()
{
	guardSlow(FBitReader::GetData);
	return &Buffer(0);
	unguardSlow;
}
UBOOL FBitReader::AtEnd()
{
	return ArIsError || Pos==Num;
}
void FBitReader::SetOverflowed()
{
	//debug
//	debugf( TEXT("FBitReader::SetOverflowed") );

	ArIsError = 1;
}
INT FBitReader::GetNumBytes()
{
	return (Num+7)>>3;
}
INT FBitReader::GetNumBits()
{
	return Num;
}
INT FBitReader::GetPosBits()
{
	return Pos;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

