/*=============================================================================
	UC2AudioData.cpp: the cooked audio data - cue tables (.uc2cues), wave bank
	indexes (.uc2wb) and Ogg Vorbis decoding from memory.

	The files are written by tools/cook-audio.py; their format is specified in
	docs/porting/analysis/audio.md ("Cooked format"). Readers ignore keywords and
	keys they do not know, so the format can grow without breaking older builds.
=============================================================================*/

#include "UC2ALAudioPrivate.h"

#include "vorbis/codec.h"
#include "vorbis/vorbisfile.h"

/*-----------------------------------------------------------------------------
	Line records: "keyword key=value key=value", values %XX-escaped.
-----------------------------------------------------------------------------*/

struct FUC2Record
{
	FString				Keyword;
	TArray<FString>		Keys, Values;

	const TCHAR* Get( const TCHAR* Key, const TCHAR* Default=TEXT("") ) const
	{
		for( INT i=0; i<Keys.Num(); i++ )
			if( Keys(i)==Key )
				return *Values(i);
		return Default;
	}
	INT GetInt( const TCHAR* Key, INT Default=0 ) const
	{
		const TCHAR* V = Get( Key, NULL );
		return V ? appAtoi(V) : Default;
	}
	FLOAT GetFloat( const TCHAR* Key, FLOAT Default=0.f ) const
	{
		const TCHAR* V = Get( Key, NULL );
		return V ? appAtof(V) : Default;
	}
	// "a,b" -> A, B
	void GetPair( const TCHAR* Key, INT& A, INT& B ) const
	{
		FString V = Get( Key, TEXT("0,0") ), L, R;
		if( V.Split( TEXT(","), &L, &R ) ) { A = appAtoi(*L); B = appAtoi(*R); }
		else { A = B = appAtoi(*V); }
	}
};

static INT HexDigit( TCHAR C )
{
	if( C>='0' && C<='9' ) return C-'0';
	if( C>='a' && C<='f' ) return C-'a'+10;
	if( C>='A' && C<='F' ) return C-'A'+10;
	return -1;
}

static FString Unescape( const TCHAR* S, INT Len )
{
	FString Out;
	for( INT i=0; i<Len; i++ )
	{
		if( S[i]=='%' && i+2<Len+0 && HexDigit(S[i+1])>=0 && HexDigit(S[i+2])>=0 )
		{
			TCHAR C[2] = { (TCHAR)(HexDigit(S[i+1])*16 + HexDigit(S[i+2])), 0 };
			Out += C;
			i += 2;
		}
		else
		{
			TCHAR C[2] = { S[i], 0 };
			Out += C;
		}
	}
	return Out;
}

// Splits one line into a record. Returns 0 for blank lines and comments.
static UBOOL ParseRecord( const TCHAR* Line, INT Len, FUC2Record& R )
{
	R.Keyword = TEXT("");
	R.Keys.Empty();
	R.Values.Empty();
	INT i = 0;
	while( i<Len && (Line[i]==' ' || Line[i]=='\t') )
		i++;
	if( i>=Len || Line[i]=='#' )
		return 0;
	UBOOL bFirst = 1;
	while( i<Len )
	{
		INT Start = i;
		while( i<Len && Line[i]!=' ' && Line[i]!='\t' )
			i++;
		if( i>Start )
		{
			if( bFirst )
			{
				R.Keyword = Unescape( Line+Start, i-Start );
				bFirst = 0;
			}
			else
			{
				INT Eq = Start;
				while( Eq<i && Line[Eq]!='=' )
					Eq++;
				new(R.Keys) FString( Unescape( Line+Start, Eq-Start ) );
				new(R.Values) FString( Eq<i ? Unescape( Line+Eq+1, i-Eq-1 ) : FString() );
			}
		}
		while( i<Len && (Line[i]==' ' || Line[i]=='\t') )
			i++;
	}
	return !bFirst;
}

// Calls Visit for every record of a cooked text file. Returns 0 when the file cannot be read.
template<class F> static UBOOL ForEachRecord( const TCHAR* Path, F Visit, FString& Error )
{
	FString Text;
	if( !appLoadFileToString( Text, Path ) )
	{
		Error = FString::Printf( TEXT("cannot read %s"), Path );
		return 0;
	}
	const TCHAR* P = *Text;
	FUC2Record R;
	INT LineNo = 0;
	while( *P )
	{
		const TCHAR* E = P;
		while( *E && *E!='\n' )
			E++;
		INT Len = E-P;
		if( Len>0 && P[Len-1]=='\r' )
			Len--;
		LineNo++;
		if( ParseRecord( P, Len, R ) && !Visit( R, LineNo ) )
			return 0;
		P = *E ? E+1 : E;
	}
	return 1;
}

/*-----------------------------------------------------------------------------
	Cue tables.
-----------------------------------------------------------------------------*/

static INT CategoryIndex( const TCHAR* Name )
{
	if( !appStricmp(Name,TEXT("Music")) ) return UAudioSubsystem::VOLUME_Music;
	if( !appStricmp(Name,TEXT("FX")) )    return UAudioSubsystem::VOLUME_FX;
	if( !appStricmp(Name,TEXT("Voice")) ) return UAudioSubsystem::VOLUME_Voice;
	return -1;
}

static void ParseIntList( const FString& S, TArray<INT>& A, TArray<INT>& B )
{
	// "a:b,a:b,..." ('-' = empty)
	if( S==TEXT("-") || !S.Len() )
		return;
	FString Rest = S, Item;
	for( ;; )
	{
		FString Next;
		UBOOL bMore = Rest.Split( TEXT(","), &Item, &Next );
		if( !bMore )
			Item = Rest;
		FString L, R;
		if( Item.Split( TEXT(":"), &L, &R ) )
		{
			A.AddItem( appAtoi(*L) );
			B.AddItem( appAtoi(*R) );
		}
		if( !bMore )
			break;
		Rest = Next;
	}
}

UBOOL UC2LoadSoundBank( const TCHAR* Path, FUC2SoundBank& Bank, FString& Error )
{
	guard(UC2LoadSoundBank);
	TMap<FString,INT> GroupVolume;
	FUC2Sound* Sound = NULL;
	FUC2Track* Track = NULL;
	UBOOL bHeader = 0;
	UBOOL bOk = ForEachRecord( Path, [&]( const FUC2Record& R, INT LineNo ) -> UBOOL
	{
		const FString& K = R.Keyword;
		if( K==TEXT("uc2cues") )
		{
			bHeader = 1;
			INT Version = appAtoi( R.Keys.Num() ? *R.Keys(0) : TEXT("0") );
			if( Version!=1 )
			{
				Error = FString::Printf( TEXT("%s: cue table version %d, this build reads 1"), Path, Version );
				return 0;
			}
		}
		else if( !bHeader )
		{
			Error = FString::Printf( TEXT("%s: not a cue table"), Path );
			return 0;
		}
		else if( K==TEXT("bank") )
			Bank.Name = R.Get(TEXT("name"));
		else if( K==TEXT("group") )
			GroupVolume.Set( R.Get(TEXT("name")), R.GetInt(TEXT("volume")) );
		else if( K==TEXT("sound") )
		{
			Sound = new(Bank.Sounds) FUC2Sound;
			Track = NULL;
			Sound->Name				= R.Get(TEXT("name"));
			Sound->Priority			= R.GetInt(TEXT("priority"));
			Sound->Volume			= R.GetInt(TEXT("volume"));
			const INT* G			= GroupVolume.Find( R.Get(TEXT("group")) );
			if( G )
				Sound->Volume += *G;
			Sound->Pitch			= R.GetInt(TEXT("pitch"));
			Sound->Category			= CategoryIndex( R.Get(TEXT("category")) );
			Sound->bHeadroomBoost	= R.GetInt(TEXT("headroomboost"));
			Sound->I3DL2			= R.GetInt(TEXT("i3dl2"));
			Sound->bIs3D			= R.GetInt(TEXT("3d"));
			Sound->LengthMs			= R.GetInt(TEXT("length"));
			Sound->Loops			= R.GetInt(TEXT("loops"));
		}
		else if( !Sound )
			return 1;	// unknown record before the first sound: ignored (forward compatibility)
		else if( K==TEXT("3d") )
		{
			Sound->MinDistance	= Max( R.GetFloat(TEXT("min"), 1.f), 0.0001f );
			Sound->MaxDistance	= R.GetFloat(TEXT("max"), 1e9f);
			Sound->Rolloff		= R.GetFloat(TEXT("rolloff"), 1.f);
		}
		else if( K==TEXT("rpc") )
		{
			FUC2Rpc* Rpc = new(Sound->Rpcs) FUC2Rpc;
			Rpc->Name	= R.Get(TEXT("name"));
			Rpc->Track	= R.GetInt(TEXT("track"));
			ParseIntList( R.Get(TEXT("points")), Rpc->X, Rpc->Y );
		}
		else if( K==TEXT("track") )
			Track = new(Sound->Tracks) FUC2Track;
		else if( !Track )
			return 1;
		else if( K==TEXT("play") )
		{
			Track->bHasPlay		= 1;
			Track->PlayTime		= R.GetInt(TEXT("time"));
			Track->Delay		= R.GetInt(TEXT("delay"));
			Track->Loops		= R.GetInt(TEXT("loops"));
			Track->bNewVariation= R.GetInt(TEXT("newvar"));
			Track->Playlist		= R.GetInt(TEXT("playlist"));
			R.GetPair( TEXT("pitch"), Track->PitchLow, Track->PitchHigh );
			R.GetPair( TEXT("volume"), Track->VolLow, Track->VolHigh );
		}
		else if( K==TEXT("wave") )
		{
			FUC2Variation* V = new(Track->Variations) FUC2Variation;
			V->BankName	= R.Get(TEXT("bank"));
			V->Entry	= R.GetInt(TEXT("entry"));
			V->Weight	= R.GetInt(TEXT("weight"), -1);
		}
		else if( K==TEXT("setvol") || K==TEXT("setpitch") )
		{
			FUC2Event E;
			E.Kind		= K==TEXT("setvol") ? EV_SetVolume : EV_SetPitch;
			FString M	= R.Get(TEXT("mode"));
			E.Mode		= M==TEXT("fade") ? EM_Fade : M==TEXT("random") ? EM_Random : EM_Set;
			E.bRelative	= R.GetInt(TEXT("relative"), 1);
			E.Time		= R.GetInt(TEXT("time"));
			E.Steps		= R.GetInt(TEXT("steps"));
			E.Duration	= R.GetInt(TEXT("dur"));
			E.Low		= R.GetInt(TEXT("lo"));
			E.High		= R.GetInt(TEXT("hi"));
			Track->Events.AddItem( E );
		}
		else if( K==TEXT("lpf") || K==TEXT("lfo") || K==TEXT("pitchlfo") || K==TEXT("eg") || K==TEXT("mixbins")
			  || K==TEXT("loopstart") )
			Sound->IgnoredEvents++;
		if( K==TEXT("cue") )
		{
			FUC2Cue* Cue = new(Bank.Cues) FUC2Cue;
			Cue->Name = R.Get(TEXT("name"));
			ParseIntList( R.Get(TEXT("sounds")), Cue->Sounds, Cue->Weights );
			for( INT i=0; i<Cue->Sounds.Num(); i++ )
				if( Cue->Sounds(i)<0 || Cue->Sounds(i)>=Bank.Sounds.Num() )
				{
					Error = FString::Printf( TEXT("%s:%d: cue %s names sound %d of %d"), Path, LineNo, *Cue->Name,
						Cue->Sounds(i), Bank.Sounds.Num() );
					return 0;
				}
			Bank.CueIndex.Set( *Cue->Name, Bank.Cues.Num()-1 );
		}
		return 1;
	}, Error );
	return bOk && bHeader;
	unguard;
}

FLOAT FUC2Rpc::Evaluate( FLOAT Value01 ) const
{
	// XACT parameter: 0..65535; the curve is piecewise linear through its points (a vertical step where two
	// points share an X: the later one wins from there on).
	if( !X.Num() )
		return 0.f;
	FLOAT P = Clamp( Value01, 0.f, 1.f ) * 65535.f;
	FLOAT Out = Y(0);
	for( INT i=0; i+1<X.Num(); i++ )
	{
		if( P < X(i) )
			break;
		if( X(i+1)==X(i) || P >= X(i+1) )
			Out = Y(i+1);
		else
			Out = Y(i) + (Y(i+1)-Y(i)) * (P-X(i)) / (FLOAT)(X(i+1)-X(i));
	}
	return Out;
}

/*-----------------------------------------------------------------------------
	Wave bank indexes.
-----------------------------------------------------------------------------*/

UBOOL UC2LoadWaveBank( const TCHAR* Path, FUC2WaveBank& Bank, FString& Error )
{
	guard(UC2LoadWaveBank);
	UBOOL bHeader = 0;
	UBOOL bOk = ForEachRecord( Path, [&]( const FUC2Record& R, INT LineNo ) -> UBOOL
	{
		const FString& K = R.Keyword;
		if( K==TEXT("uc2wb") )
		{
			bHeader = 1;
			INT Version = appAtoi( R.Keys.Num() ? *R.Keys(0) : TEXT("0") );
			if( Version!=1 )
			{
				Error = FString::Printf( TEXT("%s: wave bank index version %d, this build reads 1"), Path, Version );
				return 0;
			}
		}
		else if( !bHeader )
		{
			Error = FString::Printf( TEXT("%s: not a wave bank index"), Path );
			return 0;
		}
		else if( K==TEXT("bank") )
		{
			Bank.Name		= R.Get(TEXT("name"));
			Bank.Lang		= R.Get(TEXT("lang"));
			Bank.DataFile	= R.Get(TEXT("data"));
			Bank.bStream	= R.GetInt(TEXT("stream"));
		}
		else if( K==TEXT("wave") )
		{
			INT Index = R.GetInt(TEXT("index"), -1);
			if( Index!=Bank.Waves.Num() )
			{
				Error = FString::Printf( TEXT("%s:%d: wave %d out of order"), Path, LineNo, Index );
				return 0;
			}
			FUC2Wave* W = new(Bank.Waves) FUC2Wave;
			W->Name		= R.Get(TEXT("name"));
			W->bMissing	= R.Get(TEXT("missing"),NULL)!=NULL;
			W->Offset	= R.GetInt(TEXT("offset"));
			W->Size		= R.GetInt(TEXT("size"));
			W->Rate		= R.GetInt(TEXT("rate"));
			W->Channels	= R.GetInt(TEXT("channels"));
			W->Frames	= R.GetInt(TEXT("frames"));
			R.GetPair( TEXT("loop"), W->LoopStart, W->LoopLength );
		}
		return 1;
	}, Error );
	return bOk && bHeader;
	unguard;
}

/*-----------------------------------------------------------------------------
	Vorbis from memory (vorbisfile with memory callbacks).
-----------------------------------------------------------------------------*/

struct FMemSource
{
	const BYTE*	Data;
	size_t		Size, Pos;
};

static size_t MemRead( void* Ptr, size_t Size, size_t Count, void* Src )
{
	FMemSource* M = (FMemSource*)Src;
	size_t Want = Size*Count;
	size_t Left = M->Size - M->Pos;
	if( Want>Left )
		Want = Left;
	appMemcpy( Ptr, M->Data + M->Pos, Want );
	M->Pos += Want;
	return Size ? Want/Size : 0;
}

static int MemSeek( void* Src, ogg_int64_t Offset, int Whence )
{
	FMemSource* M = (FMemSource*)Src;
	ogg_int64_t P = Whence==SEEK_SET ? Offset : Whence==SEEK_CUR ? (ogg_int64_t)M->Pos + Offset : (ogg_int64_t)M->Size + Offset;
	if( P<0 || P>(ogg_int64_t)M->Size )
		return -1;
	M->Pos = (size_t)P;
	return 0;
}

static int MemClose( void* )
{
	return 0;
}

static long MemTell( void* Src )
{
	return (long)((FMemSource*)Src)->Pos;
}

static ov_callbacks MemCallbacks()
{
	ov_callbacks C;
	C.read_func		= MemRead;
	C.seek_func		= MemSeek;
	C.close_func	= MemClose;
	C.tell_func		= MemTell;
	return C;
}

UBOOL UC2DecodeVorbis( const BYTE* Data, INT Size, TArray<SWORD>& Pcm, INT& Channels, INT& Rate, FString& Error,
	INT Frames, INT* Trimmed )
{
	guard(UC2DecodeVorbis);
	FMemSource Src = { Data, (size_t)Size, 0 };
	OggVorbis_File Vf;
	if( ov_open_callbacks( &Src, &Vf, NULL, 0, MemCallbacks() ) < 0 )
	{
		Error = TEXT("not an Ogg Vorbis stream");
		return 0;
	}
	vorbis_info* Info = ov_info( &Vf, -1 );
	Channels	= Info->channels;
	Rate		= Info->rate;
	ogg_int64_t Total = ov_pcm_total( &Vf, -1 );
	Pcm.Empty( Total>0 ? (INT)(Total*Channels) : 0 );
	char Buffer[16384];
	for( ;; )
	{
		int Section = 0;
		long Got = ov_read( &Vf, Buffer, sizeof(Buffer), 0, 2, 1, &Section );
		if( Got==0 )
			break;
		if( Got<0 )
		{
			ov_clear( &Vf );
			Error = FString::Printf( TEXT("decode error %d"), (INT)Got );
			return 0;
		}
		INT Index = Pcm.Add( Got/2 );
		appMemcpy( &Pcm(Index), Buffer, Got );
	}
	ov_clear( &Vf );
	if( Trimmed )
		*Trimmed = 0;
	if( Frames>0 && Channels>0 && Pcm.Num() > Frames*Channels )
	{
		if( Trimmed )
			*Trimmed = Pcm.Num()/Channels - Frames;
		Pcm.Remove( Frames*Channels, Pcm.Num() - Frames*Channels );
	}
	return 1;
	unguard;
}

struct FUC2VorbisImpl
{
	TArray<BYTE>	Data;
	FMemSource		Src;
	OggVorbis_File	Vf;
};

UBOOL FUC2VorbisStream::Open( TArray<BYTE>& InData, FString& Error )
{
	guard(FUC2VorbisStream::Open);
	Close();
	FUC2VorbisImpl* I = new FUC2VorbisImpl;
	ExchangeArray( I->Data, InData );
	I->Src.Data	= &I->Data(0);
	I->Src.Size	= I->Data.Num();
	I->Src.Pos	= 0;
	if( !I->Data.Num() || ov_open_callbacks( &I->Src, &I->Vf, NULL, 0, MemCallbacks() ) < 0 )
	{
		delete I;
		Error = TEXT("not an Ogg Vorbis stream");
		return 0;
	}
	vorbis_info* Info = ov_info( &I->Vf, -1 );
	Channels	= Info->channels;
	Rate		= Info->rate;
	Position	= 0;
	Impl		= I;
	return 1;
	unguard;
}

INT FUC2VorbisStream::Read( SWORD* Out, INT MaxFrames )
{
	if( !Impl )
		return 0;
	FUC2VorbisImpl* I = (FUC2VorbisImpl*)Impl;
	if( Frames>0 )
		MaxFrames = Clamp( Frames - Position, 0, MaxFrames );
	INT Bytes = 0, Want = MaxFrames*Channels*2;
	while( Bytes<Want )
	{
		int Section = 0;
		long Got = ov_read( &I->Vf, (char*)Out + Bytes, Want-Bytes, 0, 2, 1, &Section );
		if( Got<=0 )
			break;
		Bytes += Got;
	}
	Position += Bytes/(Channels*2);
	return Bytes/(Channels*2);
}

UBOOL FUC2VorbisStream::Rewind()
{
	if( !Impl || ov_raw_seek( &((FUC2VorbisImpl*)Impl)->Vf, 0 )!=0 )
		return 0;
	Position = 0;
	return 1;
}

void FUC2VorbisStream::Close()
{
	if( Impl )
	{
		FUC2VorbisImpl* I = (FUC2VorbisImpl*)Impl;
		ov_clear( &I->Vf );
		delete I;
		Impl = NULL;
	}
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
