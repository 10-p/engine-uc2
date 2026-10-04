/*=============================================================================
	UC2ALAudio.cpp: package, and the cooked-data check commandlet.

	ucc-bin UC2ALAudio.UC2AudioCheck   (from System/, with the cooked Sounds/ and Music/
	beside it) loads every cue table, resolves every cue of every bank to its waves in
	every language the cook produced, and decodes every wave of every wave bank with the
	same code the device uses. No audio device is involved.
=============================================================================*/

#include "UC2ALAudioPrivate.h"

IMPLEMENT_PACKAGE(UC2ALAudio);
IMPLEMENT_CLASS(UUC2AudioCheckCommandlet);

void UUC2AudioCheckCommandlet::StaticConstructor()
{
	guard(UUC2AudioCheckCommandlet::StaticConstructor);
	LogToStdout		= 1;
	IsClient		= 0;
	IsEditor		= 0;
	IsServer		= 0;
	LazyLoad		= 1;
	ShowErrorCount	= 0;
	unguard;
}

struct FCheckBank
{
	FUC2WaveBank	Bank;
	TArray<BYTE>	Data;
	TArray<INT>		Decoded;	// per wave: 0 not tried, 1 decodes to its frames, -1 fails or differs
};

static const TCHAR* GCheckLangs[] = { TEXT(""), TEXT("deu"), TEXT("fra"), TEXT("ita"), TEXT("spa") };

INT UUC2AudioCheckCommandlet::Main( const TCHAR* Parms )
{
	guard(UUC2AudioCheckCommandlet::Main);
	const FString Dir = TEXT("../Sounds/");
	INT Failures = 0;
	TArray<FString> Messages;

	// Every wave bank file: load the index and the data, decode every wave.
	TArray<FString> WbFiles = GFileManager->FindFiles( *(Dir + TEXT("*.uc2wb")), 1, 0 );
	TMap<FString,INT> ByFile;
	TArray<FCheckBank*> Banks;
	INT Waves = 0, WavesOk = 0, WavesMissing = 0, Streams = 0, StreamsOk = 0, TrimmedWaves = 0, MaxTrim = 0;
	for( INT i=0; i<WbFiles.Num(); i++ )
	{
		FCheckBank* B = new FCheckBank;
		FString Error;
		if( !UC2LoadWaveBank( *(Dir + WbFiles(i)), B->Bank, Error ) )
		{
			GWarn->Logf( TEXT("FAIL %s: %s"), *WbFiles(i), *Error );
			Failures++;
			delete B;
			continue;
		}
		B->Bank.File = WbFiles(i);
		ByFile.Set( *WbFiles(i), Banks.AddItem( B ) );
		B->Decoded.AddZeroed( B->Bank.Waves.Num() );
		if( !appLoadFileToArray( B->Data, *(Dir + B->Bank.DataFile) ) )
		{
			GWarn->Logf( TEXT("FAIL %s: cannot read its data file %s"), *WbFiles(i), *B->Bank.DataFile );
			Failures++;
		}
		for( INT w=0; w<B->Bank.Waves.Num(); w++ )
		{
			FUC2Wave& W = B->Bank.Waves(w);
			if( W.bMissing )
			{
				WavesMissing++;
				B->Decoded(w) = -1;
				continue;
			}
			if( B->Bank.bStream )
			{
				// Music: stream the whole file, as the device does, and count its frames.
				Streams++;
				FUC2VorbisStream Stream;
				Stream.Frames = W.Frames;
				TArray<BYTE> Copy = B->Data;
				FString Err;
				INT Frames = 0;
				if( Stream.Open( Copy, Err ) )
				{
					TArray<SWORD> Tmp;
					Tmp.Add( 8192*Stream.Channels );
					INT Got;
					while( (Got = Stream.Read( &Tmp(0), 8192 ))>0 )
						Frames += Got;
				}
				if( Frames==W.Frames && Stream.Rate==W.Rate && Stream.Channels==W.Channels )
				{
					StreamsOk++;
					B->Decoded(w) = 1;
				}
				else
				{
					GWarn->Logf( TEXT("FAIL %s: stream %s: %d frames %d Hz %d ch, index says %d frames %d Hz %d ch %s"), *WbFiles(i),
						*B->Bank.DataFile, Frames, Stream.Rate, Stream.Channels, W.Frames, W.Rate, W.Channels, *Err );
					Failures++;
					B->Decoded(w) = -1;
				}
				continue;
			}
			Waves++;
			TArray<SWORD> Pcm;
			INT Channels = 0, Rate = 0, Trimmed = 0;
			FString Err;
			UBOOL bOk = W.Offset>=0 && W.Size>0 && W.Offset+W.Size<=B->Data.Num()
				&& UC2DecodeVorbis( &B->Data(W.Offset), W.Size, Pcm, Channels, Rate, Err, W.Frames, &Trimmed )
				&& Channels==W.Channels && Rate==W.Rate && Pcm.Num()==W.Frames*W.Channels;
			if( bOk && Trimmed )
			{
				TrimmedWaves++;
				MaxTrim = Max( MaxTrim, Trimmed );
			}
			if( bOk )
			{
				WavesOk++;
				B->Decoded(w) = 1;
			}
			else
			{
				GWarn->Logf( TEXT("FAIL %s wave %d %s: decoded %d frames %d Hz %d ch, index says %d frames %d Hz %d ch %s"),
					*WbFiles(i), w, *W.Name, Channels ? Pcm.Num()/Channels : 0, Rate, Channels, W.Frames, W.Rate, W.Channels, *Err );
				Failures++;
				B->Decoded(w) = -1;
			}
		}
	}
	GWarn->Logf( TEXT("wave banks: %d index files; waves: %d decoded to their exact length of %d cooked (%d entries have no cooked wave); music streams: %d of %d"),
		Banks.Num(), WavesOk, Waves, WavesMissing, StreamsOk, Streams );
	GWarn->Logf( TEXT("decoder end trim: %d waves decoded past their end-of-stream granule position (at most %d frames) and were cut to the source length"),
		TrimmedWaves, MaxTrim );

	// Every cue of every cue table, in every language: at least one variation decodes.
	TArray<FString> CueFiles = GFileManager->FindFiles( *(Dir + TEXT("*.uc2cues")), 1, 0 );
	INT TotalCues = 0;
	for( INT l=0; l<ARRAY_COUNT(GCheckLangs); l++ )
	{
		INT Cues = 0, Full = 0, Partial = 0, None = 0, Localised = 0;
		for( INT f=0; f<CueFiles.Num(); f++ )
		{
			FUC2SoundBank SB;
			FString Error;
			if( !UC2LoadSoundBank( *(Dir + CueFiles(f)), SB, Error ) )
			{
				if( l==0 )
				{
					GWarn->Logf( TEXT("FAIL %s: %s"), *CueFiles(f), *Error );
					Failures++;
				}
				continue;
			}
			for( INT c=0; c<SB.Cues.Num(); c++ )
			{
				Cues++;
				INT Good = 0, Total = 0;
				UBOOL bLocal = 0;
				for( INT s=0; s<SB.Cues(c).Sounds.Num(); s++ )
				{
					FUC2Sound& Snd = SB.Sounds( SB.Cues(c).Sounds(s) );
					for( INT t=0; t<Snd.Tracks.Num(); t++ )
						for( INT v=0; v<Snd.Tracks(t).Variations.Num(); v++ )
						{
							FUC2Variation& V = Snd.Tracks(t).Variations(v);
							Total++;
							const INT* Bi = NULL;
							if( *GCheckLangs[l] )
							{
								Bi = ByFile.Find( V.BankName + TEXT("_") + GCheckLangs[l] + TEXT(".uc2wb") );
								bLocal |= Bi!=NULL;
							}
							if( !Bi )
								Bi = ByFile.Find( V.BankName + TEXT(".uc2wb") );
							if( Bi && V.Entry>=0 && V.Entry<Banks(*Bi)->Decoded.Num() && Banks(*Bi)->Decoded(V.Entry)==1 )
								Good++;
						}
				}
				Localised += bLocal;
				if( Good && Good==Total )
					Full++;
				else if( Good )
				{
					Partial++;
					if( l==0 || bLocal )
						new(Messages) FString( FString::Printf( TEXT("partial [%s] %s: %d of %d variations decode"),
							*GCheckLangs[l] ? GCheckLangs[l] : TEXT("int"), *SB.Cues(c).Name, Good, Total ) );
				}
				else
				{
					None++;
					if( l==0 || bLocal )
						new(Messages) FString( FString::Printf( TEXT("silent  [%s] %s: none of %d variations decodes"),
							*GCheckLangs[l] ? GCheckLangs[l] : TEXT("int"), *SB.Cues(c).Name, Total ) );
				}
			}
		}
		if( l==0 )
			TotalCues = Cues;
		GWarn->Logf( TEXT("cues [%s]: %d in %d cue tables: %d resolve fully, %d partly, %d to no decodable wave (%d use a localised bank)"),
			*GCheckLangs[l] ? GCheckLangs[l] : TEXT("int"), Cues, CueFiles.Num(), Full, Partial, None, Localised );
	}
	for( INT i=0; i<Messages.Num(); i++ )
		GWarn->Logf( TEXT("  %s"), *Messages(i) );
	for( INT i=0; i<Banks.Num(); i++ )
		delete Banks(i);
	GWarn->Logf( TEXT("UC2AudioCheck: %d cue tables, %d cues, %d failures"), CueFiles.Num(), TotalCues, Failures );
	return Failures ? 1 : 0;
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
