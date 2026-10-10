/*=============================================================================
	UC2XboxBanks.cpp: M16 - the shipped Xbox XACT banks read at run time.

	A retail disc has no cooked data: only Sounds/<Bank>.xsb (sound banks: cues,
	sounds, tracks, events, 3D parameters) and Sounds/<Bank>[_<lang>].xwb (wave
	banks: Xbox ADPCM, PCM, one WMA). These readers build the same in-memory tables
	the cooked .uc2cues/.uc2wb give (UC2AudioData.cpp), so the device plays either
	source with the same code. The offline readers tools/uc2xwb.py and
	tools/uc2xsb.py (read_xsb_full) are the reference; the layout and how it was
	decoded are in docs/porting/analysis/xbox-audio-runtime.md.

	Portable: every field is read little-endian byte by byte with bounds checks;
	no Xbox SDK, no structure casts, offsets fit in INT (the largest bank is 10 MB).
=============================================================================*/

#include "UC2ALAudioPrivate.h"

/*-----------------------------------------------------------------------------
	Little-endian reads with bounds.
-----------------------------------------------------------------------------*/

struct FUC2Bytes
{
	const BYTE*	Data;
	INT			Size;
	UBOOL		bBad;		// a read fell outside the data (the result is 0)
	FUC2Bytes( const BYTE* InData, INT InSize ) : Data(InData), Size(InSize), bBad(0) {}
	UBOOL Has( INT At, INT Len )
	{
		if( At<0 || Len<0 || At>Size-Len )
		{
			bBad = 1;
			return 0;
		}
		return 1;
	}
	DWORD U8( INT At )	{ return Has(At,1) ? (DWORD)Data[At] : 0; }
	DWORD U16( INT At )	{ return Has(At,2) ? (DWORD)Data[At] | ((DWORD)Data[At+1]<<8) : 0; }
	DWORD U32( INT At )	{ return Has(At,4) ? (DWORD)Data[At] | ((DWORD)Data[At+1]<<8) | ((DWORD)Data[At+2]<<16) | ((DWORD)Data[At+3]<<24) : 0; }
	INT S16( INT At )	{ DWORD V = U16(At); return V>=0x8000 ? (INT)V - 0x10000 : (INT)V; }
	INT S32( INT At )	{ return (INT)U32(At); }
	FLOAT F32( INT At )
	{
		DWORD V = U32(At);
		FLOAT F;
		appMemcpy( &F, &V, sizeof(F) );		// IEEE single on every target we build for
		return F;
	}
	// A NUL-terminated name of at most Max bytes.
	FString Str( INT At, INT Max )
	{
		FString Out;
		for( INT i=0; i<Max && Has(At+i,1) && Data[At+i]; i++ )
		{
			TCHAR C[2] = { (TCHAR)Data[At+i], 0 };
			Out += C;
		}
		return Out;
	}
};

// Reads Len bytes at At of an open file into Out.
static UBOOL ReadAt( FArchive* Ar, INT At, INT Len, TArray<BYTE>& Out )
{
	if( !Ar || At<0 || Len<0 || At>Ar->TotalSize()-Len )
		return 0;
	Out.Empty( Len );
	Out.Add( Len );
	Ar->Seek( At );
	if( Len )
		Ar->Serialize( &Out(0), Len );
	return !Ar->IsError();
}

// Reads Len bytes at At of a file into Out.
static UBOOL ReadFileRange( const TCHAR* Path, INT At, INT Len, TArray<BYTE>& Out, FString& Error )
{
	FArchive* Ar = GFileManager->CreateFileReader( Path );
	UBOOL bOk = ReadAt( Ar, At, Len, Out );
	if( Ar )
		delete Ar;
	if( !bOk )
		Error = Ar ? FString::Printf( TEXT("%s: cannot read %d bytes at %d"), Path, Len, At ) : FString::Printf( TEXT("cannot open %s"), Path );
	return bOk;
}

/*-----------------------------------------------------------------------------
	Wave banks (.xwb, XDK xactwb.h version 3).
-----------------------------------------------------------------------------*/

enum
{
	XWB_FLAG_COMPACT	= 0x20000,
	XWB_TAG_PCM			= 0,
	XWB_TAG_ADPCM		= 1,
	XWB_TAG_WMA			= 2,
	ADPCM_BLOCK			= 36,	// bytes per channel per block
	ADPCM_FRAMES		= 64,	// sample frames per block
};

// Bytes of a format -> sample frames (ADPCM: whole blocks; WMA: unknown, 0).
static INT XwbFrames( INT Format, INT Channels, INT Bytes )
{
	INT Ch = Max( Channels, 1 );
	switch( Format )
	{
		case WF_XboxADPCM:	return Bytes / (ADPCM_BLOCK*Ch) * ADPCM_FRAMES;
		case WF_PCM16:		return Bytes / (2*Ch);
		case WF_PCM8:		return Bytes / Ch;
		default:			return 0;
	}
}

UBOOL UC2LoadXwbWaveBank( const TCHAR* Path, FUC2WaveBank& Bank, FString& Error )
{
	guard(UC2LoadXwbWaveBank);
	FArchive* Ar = GFileManager->CreateFileReader( Path );
	if( !Ar )
	{
		Error = FString::Printf( TEXT("cannot read %s"), Path );
		return 0;
	}
	INT FileSize = Ar->TotalSize();
	TArray<BYTE> Head, BankData, Meta;
	// The header: 'WBND', version, 4 segment regions {offset, length}: bank data, entry meta-data, entry names,
	// wave data. Then the bank data (40 bytes) and the entry table; the waves stay in the file.
	UBOOL bRead = ReadAt( Ar, 0, 40, Head );
	FUC2Bytes H( bRead ? &Head(0) : NULL, bRead ? Head.Num() : 0 );
	INT SegOff[4], SegLen[4];
	for( INT i=0; i<4; i++ )
	{
		SegOff[i] = H.S32( 8+8*i );
		SegLen[i] = H.S32( 12+8*i );
	}
	if( bRead && H.U32(0)==0x444E4257 && H.U32(4)==3 && SegLen[0]>=40 && SegOff[0]>=0 && SegLen[1]>=0 && SegOff[1]>=0
	 && SegOff[1]<=FileSize-SegLen[1] && SegOff[3]>=0 && SegLen[3]>=0 && SegOff[3]<=FileSize-SegLen[3] )
		bRead = ReadAt( Ar, SegOff[0], 40, BankData ) && ReadAt( Ar, SegOff[1], SegLen[1], Meta );
	else if( bRead )
	{
		Error = H.U32(0)!=0x444E4257 ? FString::Printf( TEXT("%s: not a WBND wave bank"), Path )
			: H.U32(4)!=3 ? FString::Printf( TEXT("%s: wave bank version %d (3 is read)"), Path, H.U32(4) )
			: FString::Printf( TEXT("%s: a segment lies beyond the file"), Path );
		delete Ar;
		return 0;
	}
	delete Ar;
	if( !bRead )
	{
		Error = FString::Printf( TEXT("%s: too short for a wave bank"), Path );
		return 0;
	}
	FUC2Bytes B( &BankData(0), BankData.Num() );
	DWORD Flags		= B.U32(0);
	INT Count		= B.S32(4);
	FString Name	= B.Str( 8, 16 );
	INT MetaSize	= B.S32(24);
	INT Align		= B.S32(32);
	DWORD Compact	= B.U32(36);
	FUC2Bytes M( Meta.Num() ? &Meta(0) : NULL, Meta.Num() );
	INT Wave0 = SegOff[3], WaveLen = SegLen[3];
	if( Count<0 || Count>65535 || (Flags & XWB_FLAG_COMPACT ? MetaSize!=4 : MetaSize<24) || Count*MetaSize>Meta.Num() )
	{
		Error = FString::Printf( TEXT("%s: %d entries of %d bytes do not fit the entry table (%d bytes)"), Path, Count, MetaSize, Meta.Num() );
		return 0;
	}
	Bank.Name		= Name;
	Bank.DataFile	= FString(Path).Mid( FString(Path).InStr( TEXT("/"), 1 ) + 1 );
	Bank.bXbox		= 1;
	Bank.bStream	= 0;	// every UC2 bank says "streaming" (flag 1); the device streams what PlayMusic plays
	Bank.Waves.Empty( Count );
	for( INT i=0; i<Count; i++ )
	{
		FUC2Wave* W = new(Bank.Waves) FUC2Wave;
		DWORD Fmt;
		if( Flags & XWB_FLAG_COMPACT )
		{
			// offset:21 in alignment units, length deviation:11; the length runs to the next entry (or the end).
			DWORD V = M.U32( 4*i );
			INT Off = (INT)(V & 0x1FFFFF) * Align;
			INT Next = i+1<Count ? (INT)(M.U32( 4*(i+1) ) & 0x1FFFFF) * Align : WaveLen;
			W->Offset		= Wave0 + Off;
			W->Size			= Next - Off - (INT)(V >> 21);
			Fmt				= Compact;
			W->LoopStart	= W->LoopLength = 0;		// no loop region of its own: a loop plays the whole wave
		}
		else
		{
			INT At = MetaSize*i;
			Fmt				= M.U32( At+4 );
			W->Offset		= Wave0 + M.S32( At+8 );
			W->Size			= M.S32( At+12 );
			W->LoopStart	= M.S32( At+16 );			// bytes for now; frames below
			W->LoopLength	= M.S32( At+20 );
		}
		INT Tag			= Fmt & 3;
		W->Channels		= (Fmt >> 2) & 7;
		W->Rate			= (Fmt >> 5) & ((1<<26)-1);
		W->Format		= Tag==XWB_TAG_ADPCM ? WF_XboxADPCM : Tag==XWB_TAG_WMA ? WF_WMA : (Fmt>>31) ? WF_PCM16 : WF_PCM8;
		W->Frames		= XwbFrames( W->Format, W->Channels, W->Size );
		W->LoopStart	= XwbFrames( W->Format, W->Channels, W->LoopStart );
		W->LoopLength	= XwbFrames( W->Format, W->Channels, W->LoopLength );
		W->Name			= FString::Printf( TEXT("%s#%d"), *Name, i );
		if( W->Size<0 || W->Offset<Wave0 || W->Offset>FileSize-W->Size || W->Channels<1 || W->Rate<1 )
		{
			Error = FString::Printf( TEXT("%s: entry %d (%d+%d, %d ch, %d Hz) is not a wave inside the file"), Path, i, W->Offset, W->Size, W->Channels, W->Rate );
			return 0;
		}
		if( W->Format==WF_WMA )
			W->bMissing = 1;	// no WMA decoder: the entry is there, it cannot play
	}
	if( M.bBad )
	{
		Error = FString::Printf( TEXT("%s: entry table truncated"), Path );
		return 0;
	}
	return 1;
	unguard;
}

INT UC2DecodeXboxAdpcm( const BYTE* Data, INT Size, INT Channels, SWORD* Out )
{
	// IMA ADPCM, the reading tools/uc2xwb.py proved against 643 source waves (audio-f6.md): per channel a 4-byte
	// header (s16 sample, u8 step index, u8 0) and 32 bytes of codes, low nibble first; channels interleaved in
	// 4-byte words. A block plays the header sample and the first 63 codes (the 64th predicts the next header).
	static const INT IndexTab[16] = { -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8 };
	static const INT StepTab[89] =
	{
		7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
		107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
		876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871,
		5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623,
		27086, 29794, 32767
	};
	if( Channels<1 )
		return 0;
	const INT Blk = ADPCM_BLOCK*Channels;
	const INT Blocks = Size / Blk;
	for( INT b=0; b<Blocks; b++ )
	{
		const BYTE* Base = Data + b*Blk;
		for( INT c=0; c<Channels; c++ )
		{
			const BYTE* Hd = Base + 4*c;
			INT Pred = (INT)(SWORD)(Hd[0] | (Hd[1]<<8));
			INT Index = Hd[2];
			if( Index>88 )
				Index = 88;
			SWORD* O = Out + (b*ADPCM_FRAMES)*Channels + c;
			O[0] = (SWORD)Pred;
			INT n = 1;
			for( INT w=0; w<8 && n<ADPCM_FRAMES; w++ )
			{
				const BYTE* Word = Base + 4*Channels + (w*Channels + c)*4;
				for( INT k=0; k<8 && n<ADPCM_FRAMES; k++ )
				{
					INT Code = (k & 1) ? (Word[k>>1] >> 4) : (Word[k>>1] & 15);
					INT Step = StepTab[Index];
					INT Diff = Step >> 3;
					if( Code & 1 ) Diff += Step >> 2;
					if( Code & 2 ) Diff += Step >> 1;
					if( Code & 4 ) Diff += Step;
					if( Code & 8 )
					{
						Pred -= Diff;
						if( Pred < -32768 ) Pred = -32768;
					}
					else
					{
						Pred += Diff;
						if( Pred > 32767 ) Pred = 32767;
					}
					Index += IndexTab[Code];
					if( Index<0 ) Index = 0;
					else if( Index>88 ) Index = 88;
					O[n*Channels] = (SWORD)Pred;
					n++;
				}
			}
		}
	}
	return Blocks*ADPCM_FRAMES;
}

// Raw bytes of one Xbox entry -> interleaved PCM.
static UBOOL DecodeXwbBytes( const BYTE* Raw, INT Size, INT Format, INT Channels, TArray<SWORD>& Pcm, FString& Error )
{
	Pcm.Empty();
	switch( Format )
	{
		case WF_XboxADPCM:
		{
			INT Frames = XwbFrames( Format, Channels, Size );
			Pcm.Add( Frames*Channels );
			if( Frames )
				UC2DecodeXboxAdpcm( Raw, Size, Channels, &Pcm(0) );
			return 1;
		}
		case WF_PCM16:
		{
			INT N = Size/2;
			Pcm.Add( N );
			for( INT i=0; i<N; i++ )
				Pcm(i) = (SWORD)(Raw[2*i] | (Raw[2*i+1]<<8));
			return 1;
		}
		case WF_PCM8:
		{
			Pcm.Add( Size );
			for( INT i=0; i<Size; i++ )
				Pcm(i) = (SWORD)(((INT)Raw[i] - 128) << 8);
			return 1;
		}
		case WF_WMA:
			Error = TEXT("WMA (no decoder in this build)");
			return 0;
		default:
			Error = FString::Printf( TEXT("unknown format %d"), Format );
			return 0;
	}
}

UBOOL UC2DecodeWave( const FUC2WaveBank& Bank, const FUC2Wave& W, const TCHAR* SoundsDir, TArray<SWORD>& Pcm,
	INT& Channels, INT& Rate, FString& Error, INT* Trimmed )
{
	guard(UC2DecodeWave);
	if( Trimmed )
		*Trimmed = 0;
	if( W.Format==WF_Vorbis )
	{
		if( W.Offset<0 || W.Size<=0 || W.Offset+W.Size > Bank.Data.Num() )
		{
			Error = TEXT("outside its data file");
			return 0;
		}
		return UC2DecodeVorbis( &Bank.Data(W.Offset), W.Size, Pcm, Channels, Rate, Error, W.Frames, Trimmed );
	}
	TArray<BYTE> Raw;
	if( !ReadFileRange( *(FString(SoundsDir) + Bank.DataFile), W.Offset, W.Size, Raw, Error ) )
		return 0;
	Channels	= W.Channels;
	Rate		= W.Rate;
	return DecodeXwbBytes( Raw.Num() ? &Raw(0) : NULL, Raw.Num(), W.Format, W.Channels, Pcm, Error );
	unguard;
}

/*-----------------------------------------------------------------------------
	Streamed music from an .xwb entry.
-----------------------------------------------------------------------------*/

UBOOL FUC2XwbStream::Open( const TCHAR* Path, const FUC2Wave& W, FString& Error )
{
	guard(FUC2XwbStream::Open);
	Close();
	if( W.Format!=WF_XboxADPCM && W.Format!=WF_PCM16 && W.Format!=WF_PCM8 )
	{
		Error = W.Format==WF_WMA ? TEXT("the bank's wave is WMA (no decoder in this build)") : TEXT("unknown wave format");
		return 0;
	}
	Ar = GFileManager->CreateFileReader( Path );
	if( !Ar || W.Offset<0 || W.Size<0 || W.Offset > Ar->TotalSize()-W.Size )
	{
		Close();
		Error = FString::Printf( TEXT("cannot read %s"), Path );
		return 0;
	}
	Format		= W.Format;
	Channels	= W.Channels;
	Rate		= W.Rate;
	Start		= W.Offset;
	Size		= W.Size;
	Frames		= W.Frames;
	return Rewind();
	unguard;
}

INT FUC2XwbStream::Read( SWORD* Out, INT MaxFrames )
{
	if( !Ar || Channels<1 )
		return 0;
	// Whole blocks (ADPCM) or 4096 frames (PCM) at a time; what is left over waits in Pending.
	const INT Unit = Format==WF_XboxADPCM ? ADPCM_BLOCK*Channels : (Format==WF_PCM16 ? 2 : 1)*Channels;
	const INT UnitsPerRead = Format==WF_XboxADPCM ? 64 : 4096;		// 64 ADPCM blocks = 4096 frames
	INT Done = 0;
	TArray<BYTE> Raw;
	while( Done<MaxFrames )
	{
		INT Have = (Pending.Num() - PendingAt) / Channels;
		if( Have>0 )
		{
			INT Take = Min( Have, MaxFrames-Done );
			appMemcpy( Out + Done*Channels, &Pending(PendingAt), Take*Channels*sizeof(SWORD) );
			PendingAt += Take*Channels;
			Done += Take;
			continue;
		}
		INT Units = Min( UnitsPerRead, (Size-Pos)/Unit );
		if( Units<=0 )
			break;
		Raw.Empty( Units*Unit );
		Raw.Add( Units*Unit );
		Ar->Seek( Start+Pos );
		Ar->Serialize( &Raw(0), Units*Unit );
		if( Ar->IsError() )
			break;
		Pos += Units*Unit;
		FString Error;
		PendingAt = 0;
		if( !DecodeXwbBytes( &Raw(0), Raw.Num(), Format, Channels, Pending, Error ) )
			break;
	}
	Position += Done;
	return Done;
}

UBOOL FUC2XwbStream::Rewind()
{
	if( !Ar )
		return 0;
	Pos = 0;
	Position = 0;
	Pending.Empty();
	PendingAt = 0;
	return 1;
}

void FUC2XwbStream::Close()
{
	if( Ar )
		delete Ar;
	Ar = NULL;
	Pending.Empty();
	PendingAt = 0;
}

/*-----------------------------------------------------------------------------
	Sound banks (.xsb, format version 11).
-----------------------------------------------------------------------------*/

enum
{
	XSB_SOUND_3D			= 0x01,
	XSB_SOUND_HEADROOM		= 0x02,
	XSB_SOUND_ONEWAVE		= 0x08,		// +0 = entry | bank << 16
	XSB_SOUND_VARIATIONS	= 0x10,		// +0 = a variation table
	XSB_PLAY_VARTABLE		= 0x0400,
	XSB_PLAY_NEWVARIATION	= 0x4000,
};

// The size of an event of a type, 0 = unknown (mix bins carry their own size).
static INT XsbEventSize( INT Type )
{
	switch( Type )
	{
		case 0:		return 12;	// play
		case 1:		return 24;	// play with pitch/volume variation
		case 4:		return 16;	// set pitch
		case 5:		return 16;	// set volume
		case 7:		return 20;	// low-pass filter
		case 8:		return 12;	// pitch LFO
		case 9:		return 16;	// multi-function LFO
		case 11:	return 24;	// pitch and filter envelope
		case 12:	return 8;	// loop start
		case 15:	return 8;	// play without a wave
		default:	return 0;
	}
}

// Pitch in the bank's runtime unit (semitone * 4096/12) -> cents, as tools/uc2xsb.py (round half away from 0).
static INT XsbCents( INT Stored )
{
	DOUBLE C = Stored * 1200.0 / 4096.0;
	return C>=0.0 ? (INT)(C + 0.5) : -(INT)(-C + 0.5);
}

// The stored volume keeps 16 mB steps: the round value the designer most likely typed inside the step (a multiple
// of 100, else 50, else 10, else the top of the step), as tools/uc2xsb.py volume_from_stored.
static INT XsbVolume( INT Attenuation16 )
{
	INT Hi = -16*Attenuation16, Lo = Hi-16;
	static const INT Steps[3] = { 100, 50, 10 };
	for( INT i=0; i<3; i++ )
	{
		INT Mod = Hi % Steps[i];
		if( Mod<0 )
			Mod += Steps[i];
		INT V = Hi - Mod;
		if( V>Lo && V<=Hi )
			return V;
	}
	return Hi;
}

// Reads a variation table into the play event of a track.
static UBOOL XsbVariationTable( FUC2Bytes& D, INT At, const TArray<FString>& WaveBanks, FUC2Track& T, FString& Error )
{
	INT Head = D.U16( At );
	INT Count = Head & 0x1FFF;
	T.Playlist = Head >> 13;
	TArray<INT> Widths;
	for( INT j=0; j<Count; j++ )
	{
		INT E = At+4+8*j;
		INT Bank = D.U16(E+2), Lo = D.U16(E+4), Hi = D.U16(E+6);
		if( Bank>=WaveBanks.Num() )
		{
			Error = FString::Printf( TEXT("variation table at 0x%x: wave bank %d of %d"), At, Bank, WaveBanks.Num() );
			return 0;
		}
		FUC2Variation* V = new(T.Variations) FUC2Variation;
		V->BankName	= WaveBanks(Bank);
		V->Entry	= D.U16(E);
		Widths.AddItem( Hi - Lo + (j==Count-1 ? 0 : 1) );
	}
	// The bank keeps cumulative ranges over 0..10000. Equal ranges (the last takes the rounding remainder) are
	// equal weights: written as "unset", as the project has them; otherwise the widths (the same ratios).
	INT MinW = 0, MaxW = 0;
	for( INT j=0; j<Widths.Num(); j++ )
	{
		MinW = j ? Min( MinW, Widths(j) ) : Widths(j);
		MaxW = j ? Max( MaxW, Widths(j) ) : Widths(j);
	}
	UBOOL bUniform = Count<=1 || MaxW-MinW<=Count;
	for( INT j=0; j<Count; j++ )
		T.Variations(j).Weight = bUniform ? -1 : Widths(j);
	return !D.bBad;
}

// The entry table of a wave bank, for cue lengths: frames and rate per entry.
struct FXsbWaveLengths
{
	FString			Name;
	TArray<INT>		Frames, Rates;
};

UBOOL UC2LoadXsbSoundBank( const TCHAR* Path, FUC2SoundBank& Bank, FString& Error )
{
	guard(UC2LoadXsbSoundBank);
	TArray<BYTE> File;
	if( !appLoadFileToArray( File, Path ) || File.Num()<0x38 )
	{
		Error = FString::Printf( TEXT("cannot read %s"), Path );
		return 0;
	}
	FUC2Bytes D( &File(0), File.Num() );
	if( D.U32(0)!=0x4B424453 )	// "SDBK"
	{
		Error = FString::Printf( TEXT("%s: not an SDBK sound bank"), Path );
		return 0;
	}
	if( D.U16(4)!=11 )
	{
		Error = FString::Printf( TEXT("%s: sound bank version %d (11 is read)"), Path, D.U16(4) );
		return 0;
	}
	INT WbTab = D.S32(0x08), T3D = D.S32(0x10), RpcArea = D.S32(0x14);
	INT NumSounds = D.U16(0x1C), NumCues = D.U16(0x1E), NumRpc = D.U16(0x20), NumWb = D.U16(0x22);
	Bank.Name = D.Str( 0x28, 16 );
	TArray<FString> WaveBanks;
	for( INT i=0; i<NumWb; i++ )
		new(WaveBanks) FString( D.Str( WbTab+16*i, 16 ) );

	// Cues: 20 bytes from 0x38: u16 flags (1 queue), u16 sound (0xFFFF none), u32 name, u32 0xFFFFFFFF (no sound
	// variation table: none in UC2), ...
	for( INT i=0; i<NumCues; i++ )
	{
		INT At = 0x38 + 20*i;
		INT SoundIndex = D.U16(At+2);
		if( D.U32(At+8)!=0xFFFFFFFF )
		{
			Error = FString::Printf( TEXT("%s: cue %d has a sound variation table (not read)"), Path, i );
			return 0;
		}
		FUC2Cue* Cue = new(Bank.Cues) FUC2Cue;
		Cue->Name = D.Str( D.S32(At+4), 256 );
		if( SoundIndex!=0xFFFF )
		{
			if( SoundIndex>=NumSounds )
			{
				Error = FString::Printf( TEXT("%s: cue %s names sound %d of %d"), Path, *Cue->Name, SoundIndex, NumSounds );
				return 0;
			}
			Cue->Sounds.AddItem( SoundIndex );
			Cue->Weights.AddItem( -1 );
		}
		Bank.CueIndex.Set( *Cue->Name, Bank.Cues.Num()-1 );
	}

	// Cue lengths (XACT_SOUNDCUE_PROPERTIES, computed per sound below as tools/cook-audio.py does): the wave lengths of
	// the base-language wave banks beside the .xsb (Xbox ADPCM: whole 64-frame blocks).
	FString Dir = Path;
	INT Slash = Max( Dir.InStr( TEXT("/"), 1 ), Dir.InStr( TEXT("\\"), 1 ) );
	Dir = Slash>=0 ? Dir.Left( Slash+1 ) : FString();
	TArray<FXsbWaveLengths> Lengths;
	for( INT w=0; w<WaveBanks.Num(); w++ )
	{
		FXsbWaveLengths* L = new(Lengths) FXsbWaveLengths;
		L->Name = WaveBanks(w);
		FUC2WaveBank WB;
		FString Ignored;
		if( UC2LoadXwbWaveBank( *(Dir + WaveBanks(w) + TEXT(".xwb")), WB, Ignored ) )
			for( INT e=0; e<WB.Waves.Num(); e++ )
			{
				L->Frames.AddItem( WB.Waves(e).Frames );
				L->Rates.AddItem( WB.Waves(e).Rate );
			}
	}

	// Sounds: 20 bytes each after the cues.
	INT SoundBase = 0x38 + 20*NumCues;
	for( INT i=0; i<NumSounds; i++ )
	{
		INT At = SoundBase + 20*i;
		FUC2Sound* S = new(Bank.Sounds) FUC2Sound;
		DWORD W0	= D.U32(At);
		DWORD VRaw	= D.U16(At+4);
		INT Tracks	= D.U8(At+8);
		INT Cat		= D.U8(At+10);
		INT Flags	= D.U8(At+11);
		INT Set3D	= D.U16(At+12);
		TArray<DOUBLE> TrackEnd;		// per track: the end of its last non-play event (time + duration)
		UBOOL bForever = 0;
		S->Priority			= D.U8(At+14);
		S->Volume			= XsbVolume( VRaw & 0x1FF );		// the group volume is already in it (the bank bakes it)
		S->Pitch			= XsbCents( D.S16(At+6) );
		S->Category			= Cat==0 ? UAudioSubsystem::VOLUME_Music : Cat==1 ? UAudioSubsystem::VOLUME_FX : Cat==2 ? UAudioSubsystem::VOLUME_Voice : -1;
		S->bHeadroomBoost	= (Flags & XSB_SOUND_HEADROOM) ? 1 : 0;
		S->I3DL2			= D.U8(At+15) ? -300 : 0;			// only "sends to I3DL2" is stored: the project's level
		S->bIs3D			= (Flags & XSB_SOUND_3D) ? 1 : 0;
		if( S->bIs3D )
		{
			INT R = T3D + 40*Set3D;
			S->MinDistance	= Max( D.F32(R+8), 0.0001f );
			S->MaxDistance	= D.F32(R+12);
			S->Rolloff		= D.F32(R+20);
		}
		if( Flags & XSB_SOUND_ONEWAVE )
		{
			FUC2Track* T = new(S->Tracks) FUC2Track;
			T->bHasPlay = 1;
			FUC2Variation* V = new(T->Variations) FUC2Variation;
			if( (INT)(W0>>16)>=WaveBanks.Num() )
			{
				Error = FString::Printf( TEXT("%s: sound %d: wave bank %d of %d"), Path, i, W0>>16, WaveBanks.Num() );
				return 0;
			}
			V->BankName = WaveBanks(W0>>16);
			V->Entry = W0 & 0xFFFF;
		}
		else if( Flags & XSB_SOUND_VARIATIONS )
		{
			FUC2Track* T = new(S->Tracks) FUC2Track;
			T->bHasPlay = 1;
			if( !XsbVariationTable( D, (INT)W0, WaveBanks, *T, Error ) )
			{
				Error = FString::Printf( TEXT("%s: sound %d: %s"), Path, i, *Error );
				return 0;
			}
		}
		else for( INT t=0; t<Tracks; t++ )
		{
			// Track table: u32 per track = event count | event list offset << 8.
			DWORD TT = D.U32( (INT)W0 + 4*t );
			INT Events = TT & 0xFF, E = (INT)(TT >> 8);
			FUC2Track* T = new(S->Tracks) FUC2Track;
			while( TrackEnd.Num()<S->Tracks.Num() )
				TrackEnd.AddItem( 0.0 );
			for( INT k=0; k<Events; k++ )
			{
				DWORD Head = D.U32(E);
				INT Type = Head & 0xFF, Time = (INT)(Head >> 8);
				INT Size = Type==16 ? 8 + D.S32(E+4) : XsbEventSize( Type );
				if( Size<=0 || D.bBad )
				{
					Error = FString::Printf( TEXT("%s: sound %d track %d: event type %d at 0x%x (not known)"), Path, i, t, Type, E );
					return 0;
				}
				if( Type==0 || Type==1 || Type==15 )
				{
					// Play: as the cooked loader, the fields of the track's play event (one per track in UC2).
					DWORD PF = D.U16(E+4);
					T->bHasPlay		= 1;
					T->PlayTime		= Time;
					T->Loops		= D.U16(E+6);
					T->bNewVariation= (PF & XSB_PLAY_NEWVARIATION) ? 1 : 0;
					if( Type==15 )
						;	// a play event without a wave (A_Octane Water_Dripping01): silent, as on the Xbox
					else if( PF & XSB_PLAY_VARTABLE )
					{
						if( !XsbVariationTable( D, D.S32(E+8), WaveBanks, *T, Error ) )
						{
							Error = FString::Printf( TEXT("%s: sound %d: %s"), Path, i, *Error );
							return 0;
						}
					}
					else
					{
						DWORD WV = D.U32(E+8);
						if( (INT)(WV>>16)>=WaveBanks.Num() )
						{
							Error = FString::Printf( TEXT("%s: sound %d: wave bank %d of %d"), Path, i, WV>>16, WaveBanks.Num() );
							return 0;
						}
						FUC2Variation* V = new(T->Variations) FUC2Variation;
						V->BankName = WaveBanks(WV>>16);
						V->Entry = WV & 0xFFFF;
					}
					if( Type==1 )
					{
						T->PitchLow	= XsbCents( D.S16(E+12) );
						T->PitchHigh= XsbCents( D.S16(E+14) );
						T->VolLow	= D.S16(E+16);
						T->VolHigh	= D.S16(E+18);
						T->Delay	= D.U16(E+20);
					}
				}
				else if( Type==4 || Type==5 )
				{
					FUC2Event Ev;
					INT Mode	= D.U8(E+5);
					Ev.Kind		= Type==4 ? EV_SetPitch : EV_SetVolume;
					Ev.Mode		= (Mode & 0x04) ? EM_Random : (Mode & 0x20) ? EM_Fade : EM_Set;
					Ev.bRelative= D.U8(E+12) ? 1 : 0;
					Ev.Time		= Time;
					Ev.Steps	= D.U16(E+6);
					Ev.Duration	= D.U16(E+13);
					Ev.Low		= Type==4 ? XsbCents( D.S16(E+8) ) : D.S16(E+8);
					Ev.High		= Type==4 ? XsbCents( D.S16(E+10) ) : D.S16(E+10);
					T->Events.AddItem( Ev );
					TrackEnd(t) = Max( TrackEnd(t), (DOUBLE)(Time + Ev.Duration) );
				}
				else
				{
					// Low-pass, LFOs, envelope, mix bins, loop start: counted, not played (as the cooked loader).
					S->IgnoredEvents++;
					TrackEnd(t) = Max( TrackEnd(t), (DOUBLE)(Time + (Type==7 ? (INT)D.U16(E+13) : 0)) );
					if( Type==12 )
					{
						S->Loops = Max( S->Loops, (INT)D.U16(E+6) );
						bForever |= D.U16(E+6)==UC2_LOOP_INFINITE;
					}
				}
				E += Size;
			}
		}
		if( D.bBad )
		{
			Error = FString::Printf( TEXT("%s: sound %d reaches outside the file"), Path, i );
			return 0;
		}
		// XACT_SOUNDCUE_PROPERTIES: dwLoopCount = the highest loop count, dwLength = the longest track (timestamp +
		// delay + the longest variation x passes, or the end of an event), -1 for a sound that loops forever.
		DOUBLE Longest = 0.0;
		for( INT t=0; t<S->Tracks.Num(); t++ )
		{
			FUC2Track& T = S->Tracks(t);
			DOUBLE TrackLen = t<TrackEnd.Num() ? TrackEnd(t) : 0.0;
			if( T.bHasPlay )
			{
				S->Loops = Max( S->Loops, T.Loops );
				bForever |= T.Loops==UC2_LOOP_INFINITE;
				DOUBLE WaveLen = 0.0;
				for( INT v=0; v<T.Variations.Num(); v++ )
					for( INT w=0; w<Lengths.Num(); w++ )
					{
						const FXsbWaveLengths& L = Lengths(w);
						INT Ent = T.Variations(v).Entry;
						if( L.Name==T.Variations(v).BankName && Ent>=0 && Ent<L.Frames.Num() && L.Rates(Ent)>0 )
							WaveLen = Max( WaveLen, 1000.0 * L.Frames(Ent) / L.Rates(Ent) );
					}
				TrackLen = Max( TrackLen, T.PlayTime + T.Delay + WaveLen*(T.Loops+1) );
			}
			Longest = Max( Longest, TrackLen );
		}
		S->LengthMs = bForever ? -1 : (INT)appFloor( Longest + 0.5 );
	}

	// RPC curves: 24-byte records at the RPC area (u16 sound, u8 track, u8 1, u32 curve, char name[16]); a curve is
	// 4 bytes, 4 points (u16 x, s16 y mB), 4 bytes.
	for( INT k=0; k<NumRpc; k++ )
	{
		INT At = RpcArea + 24*k;
		INT SoundIndex = D.U16(At);
		if( SoundIndex>=NumSounds )
		{
			Error = FString::Printf( TEXT("%s: RPC %d names sound %d of %d"), Path, k, SoundIndex, NumSounds );
			return 0;
		}
		FUC2Rpc* R = new(Bank.Sounds(SoundIndex).Rpcs) FUC2Rpc;
		R->Name = D.Str( At+8, 16 );
		R->Track = D.U8(At+2);
		INT Curve = D.S32(At+4);
		for( INT j=0; j<4; j++ )
		{
			R->X.AddItem( D.U16(Curve+4+4*j) );
			R->Y.AddItem( D.S16(Curve+6+4*j) );
		}
	}
	if( D.bBad )
	{
		Error = FString::Printf( TEXT("%s: an RPC curve reaches outside the file"), Path );
		return 0;
	}

	// Sound names are not in the bank: a sound is named after its first cue.
	for( INT c=0; c<Bank.Cues.Num(); c++ )
		for( INT s=0; s<Bank.Cues(c).Sounds.Num(); s++ )
			if( !Bank.Sounds(Bank.Cues(c).Sounds(s)).Name.Len() )
				Bank.Sounds(Bank.Cues(c).Sounds(s)).Name = Bank.Cues(c).Name;
	for( INT s=0; s<Bank.Sounds.Num(); s++ )
		if( !Bank.Sounds(s).Name.Len() )
			Bank.Sounds(s).Name = FString::Printf( TEXT("%s\\Sound%d"), *Bank.Name, s );

	return 1;
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
