/*=============================================================================
	UC2ALAudio.cpp: package, and the cooked-data check commandlet.

	ucc-bin UC2ALAudio.UC2AudioCheck   (from System/, with the cooked Sounds/ and Music/
	beside it) loads every cue table, resolves every cue of every bank to its waves in
	every language the cook produced, and decodes every wave of every wave bank with the
	same code the device uses. No audio device is involved.

	M16: the shipped Xbox banks are checked the same way when ../Sounds has them
	(every .xwb entry decodes to its length, every M_<Song>.xwb streams to its end,
	every .xsb loads); cues resolve through the device's own choice per bank (the
	cooked file where there is one, else the .xsb/.xwb). G-10: a WMA music bank
	(M_Adoratrice.xwb) streams from the cooked ../Music/<Song>.ogg when that file is
	there, as the device plays it. Parameters (all optional):
	  DUMP=<dir>        write every sound bank's tables as the device holds them, from
	                    each source present: <dir>/<Bank>.cooked.txt, <dir>/<Bank>.xsb.txt
	                    (tools/dev/audio-xsb-compare.sh compares them)
	  PCM=<dir> PCMBANKS=<a+b+c>   write the decoded PCM of every wave of those .xwb files
	                    (16-bit little-endian, <dir>/<file>.<entry>.pcm) for the byte
	                    comparison with tools/uc2xwb.py (tools/dev/audio-xwb-pcm-compare.sh)
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

// M16: one sound bank's tables, every field the device plays, one line per record (DUMP=).
static void DumpSoundBank( const FUC2SoundBank& B, const FString& File )
{
	FString T = FString::Printf( TEXT("bank %s sounds=%d cues=%d\n"), *B.Name, B.Sounds.Num(), B.Cues.Num() );
	for( INT i=0; i<B.Sounds.Num(); i++ )
	{
		const FUC2Sound& S = B.Sounds(i);
		T += FString::Printf( TEXT("sound %d name=%s priority=%d volume=%d pitch=%d category=%d headroomboost=%d i3dl2=%d 3d=%d min=%g max=%g rolloff=%g length=%d loops=%d ignored=%d\n"),
			i, *S.Name, S.Priority, S.Volume, S.Pitch, S.Category, S.bHeadroomBoost, S.I3DL2, S.bIs3D, (DOUBLE)S.MinDistance,
			(DOUBLE)S.MaxDistance, (DOUBLE)S.Rolloff, S.LengthMs, S.Loops, S.IgnoredEvents );
		for( INT r=0; r<S.Rpcs.Num(); r++ )
		{
			FString Pts;
			for( INT k=0; k<S.Rpcs(r).X.Num(); k++ )
				Pts += FString::Printf( TEXT("%s%d:%d"), k ? TEXT(",") : TEXT(""), S.Rpcs(r).X(k), S.Rpcs(r).Y(k) );
			T += FString::Printf( TEXT(" rpc name=%s track=%d points=%s\n"), *S.Rpcs(r).Name, S.Rpcs(r).Track, *Pts );
		}
		for( INT t=0; t<S.Tracks.Num(); t++ )
		{
			const FUC2Track& K = S.Tracks(t);
			T += FString::Printf( TEXT(" track %d play=%d time=%d delay=%d loops=%d newvar=%d playlist=%d pitch=%d,%d volume=%d,%d\n"),
				t, K.bHasPlay, K.PlayTime, K.Delay, K.Loops, K.bNewVariation, K.Playlist, K.PitchLow, K.PitchHigh, K.VolLow, K.VolHigh );
			for( INT v=0; v<K.Variations.Num(); v++ )
				T += FString::Printf( TEXT("  wave bank=%s entry=%d weight=%d\n"), *K.Variations(v).BankName, K.Variations(v).Entry, K.Variations(v).Weight );
			for( INT e=0; e<K.Events.Num(); e++ )
			{
				const FUC2Event& E = K.Events(e);
				T += FString::Printf( TEXT("  event kind=%s mode=%s relative=%d time=%d steps=%d dur=%d lo=%d hi=%d\n"),
					E.Kind==EV_SetVolume ? TEXT("setvol") : TEXT("setpitch"), E.Mode==EM_Fade ? TEXT("fade") : E.Mode==EM_Random ? TEXT("random") : TEXT("set"),
					E.bRelative, E.Time, E.Steps, E.Duration, E.Low, E.High );
			}
		}
	}
	for( INT c=0; c<B.Cues.Num(); c++ )
	{
		FString Snds;
		for( INT k=0; k<B.Cues(c).Sounds.Num(); k++ )
			Snds += FString::Printf( TEXT("%s%d:%d"), k ? TEXT(",") : TEXT(""), B.Cues(c).Sounds(k), B.Cues(c).Weights(k) );
		T += FString::Printf( TEXT("cue %d name=%s sounds=%s\n"), c, *B.Cues(c).Name, Snds.Len() ? *Snds : TEXT("-") );
	}
	appSaveStringToFile( T, *File );
}

static const TCHAR* GCheckLangs[] = { TEXT(""), TEXT("deu"), TEXT("fra"), TEXT("ita"), TEXT("spa") };

INT UUC2AudioCheckCommandlet::Main( const TCHAR* Parms )
{
	guard(UUC2AudioCheckCommandlet::Main);
	const FString Dir = TEXT("../Sounds/");
	INT Failures = 0;
	TArray<FString> Messages;
	FString DumpDir, PcmDir, PcmBanks;
	Parse( Parms, TEXT("DUMP="), DumpDir );
	Parse( Parms, TEXT("PCM="), PcmDir );
	Parse( Parms, TEXT("PCMBANKS="), PcmBanks );
	if( DumpDir.Len() )
		GFileManager->MakeDirectory( *DumpDir, 1 );
	if( PcmDir.Len() )
		GFileManager->MakeDirectory( *PcmDir, 1 );

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
	if( WbFiles.Num() )
		GWarn->Logf( TEXT("wave banks: %d index files; waves: %d decoded to their exact length of %d cooked (%d entries have no cooked wave); music streams: %d of %d"),
		Banks.Num(), WavesOk, Waves, WavesMissing, StreamsOk, Streams );
	if( WbFiles.Num() )
		GWarn->Logf( TEXT("decoder end trim: %d waves decoded past their end-of-stream granule position (at most %d frames) and were cut to the source length"),
			TrimmedWaves, MaxTrim );

	// M16: every shipped wave bank (.xwb): the index, then every entry decoded as the device decodes it (read from
	// the file; ADPCM/PCM to 16-bit PCM) and its length checked; music banks streamed to their end.
	TArray<FString> XwbFiles = GFileManager->FindFiles( *(Dir + TEXT("*.xwb")), 1, 0 );
	INT XWaves = 0, XWavesOk = 0, XWma = 0, XStreams = 0, XStreamsOk = 0, XStreamsCooked = 0, PcmWritten = 0;
	TArray<FString> PcmList;
	PcmBanks.ParseIntoArray( TEXT("+"), &PcmList );	// (Parse stops an unquoted value at a comma)
	for( INT i=0; i<XwbFiles.Num(); i++ )
	{
		FCheckBank* B = new FCheckBank;
		FString Error;
		if( !UC2LoadXwbWaveBank( *(Dir + XwbFiles(i)), B->Bank, Error ) )
		{
			GWarn->Logf( TEXT("FAIL %s: %s"), *XwbFiles(i), *Error );
			Failures++;
			delete B;
			continue;
		}
		B->Bank.File = XwbFiles(i);
		ByFile.Set( *XwbFiles(i), Banks.AddItem( B ) );
		B->Decoded.AddZeroed( B->Bank.Waves.Num() );
		FString Stem = XwbFiles(i).Left( XwbFiles(i).Len()-4 );
		UBOOL bMusic = Stem.Left(2)==TEXT("M_");		// the 21 streamed music banks (A_Music plays them)
		UBOOL bPcm = PcmDir.Len() && PcmList.FindItemIndex( Stem )!=INDEX_NONE;
		for( INT w=0; w<B->Bank.Waves.Num(); w++ )
		{
			FUC2Wave& W = B->Bank.Waves(w);
			if( W.Format==WF_WMA )
			{
				XWma++;
				B->Decoded(w) = -1;
				// G-10 (owner: "first cooked, then wma, then silence"; no WMA decoder yet): the device plays a WMA
				// music bank from the cooked song beside the Xbox banks when there is one (PlayMusic).
				FString Cooked = Dir + TEXT("../Music/") + Stem.Mid(2) + TEXT(".ogg");
				if( bMusic && GFileManager->FileSize( *Cooked )>0 )
				{
					XStreams++;
					TArray<BYTE> Data;
					FUC2VorbisStream Stream;
					INT Frames = 0;
					if( appLoadFileToArray( Data, *Cooked ) && Stream.Open( Data, Error ) )
					{
						TArray<SWORD> Tmp;
						Tmp.Add( 32768*Stream.Channels );
						INT Got;
						while( (Got = Stream.Read( &Tmp(0), 32768 ))>0 )
							Frames += Got;
					}
					if( Frames>0 )
					{
						XStreamsOk++;
						XStreamsCooked++;
						B->Decoded(w) = 1;
						new(Messages) FString( FString::Printf( TEXT("WMA     %s entry %d: %d bytes of WMA (ASF), not decoded: the song streams from the cooked %s instead (%d frames, %d Hz, %d ch)"),
							*XwbFiles(i), w, W.Size, *Cooked, Frames, Stream.Rate, Stream.Channels ) );
					}
					else
					{
						GWarn->Logf( TEXT("FAIL %s: entry %d is WMA and the cooked %s does not stream %s"), *XwbFiles(i), w, *Cooked, *Error );
						Failures++;
					}
					continue;
				}
				new(Messages) FString( FString::Printf( TEXT("WMA     %s entry %d: %d bytes of WMA (ASF), not decoded: this wave cannot play"), *XwbFiles(i), w, W.Size ) );
				continue;
			}
			if( bMusic )
			{
				XStreams++;
				FUC2XwbStream Stream;
				INT Frames = 0;
				if( Stream.Open( *(Dir + XwbFiles(i)), W, Error ) )
				{
					TArray<SWORD> Tmp;
					Tmp.Add( 32768*Stream.Channels );
					INT Got;
					while( (Got = Stream.Read( &Tmp(0), 32768 ))>0 )
						Frames += Got;
				}
				if( Frames==W.Frames && Frames>0 )
				{
					XStreamsOk++;
					B->Decoded(w) = 1;
				}
				else
				{
					GWarn->Logf( TEXT("FAIL %s: stream entry %d: %d frames, the entry has %d %s"), *XwbFiles(i), w, Frames, W.Frames, *Error );
					Failures++;
					B->Decoded(w) = -1;
				}
				continue;
			}
			XWaves++;
			TArray<SWORD> Pcm;
			INT Channels = 0, Rate = 0;
			FString Err;
			UBOOL bOk = UC2DecodeWave( B->Bank, W, *Dir, Pcm, Channels, Rate, Err ) && Channels==W.Channels && Rate==W.Rate
				&& Pcm.Num()==W.Frames*W.Channels && W.Frames>0;
			if( bOk )
			{
				XWavesOk++;
				B->Decoded(w) = 1;
				if( bPcm )
				{
					// Raw 16-bit little-endian, byte by byte (the same bytes on any host).
					TArray<BYTE> Raw;
					Raw.Add( Pcm.Num()*2 );
					for( INT k=0; k<Pcm.Num(); k++ )
					{
						Raw(2*k)	= (BYTE)(Pcm(k) & 0xFF);
						Raw(2*k+1)	= (BYTE)((Pcm(k) >> 8) & 0xFF);
					}
					PcmWritten += appSaveArrayToFile( Raw, *FString::Printf( TEXT("%s/%s.%d.pcm"), *PcmDir, *Stem, w ) ) ? 1 : 0;
				}
			}
			else
			{
				GWarn->Logf( TEXT("FAIL %s entry %d: decoded %d frames %d Hz %d ch, the entry has %d frames %d Hz %d ch %s"),
					*XwbFiles(i), w, Channels ? Pcm.Num()/Channels : 0, Rate, Channels, W.Frames, W.Rate, W.Channels, *Err );
				Failures++;
				B->Decoded(w) = -1;
			}
		}
	}
	if( XwbFiles.Num() )
		GWarn->Logf( TEXT("Xbox wave banks: %d .xwb files; waves: %d decoded to their entry's length of %d (%d WMA entries not decoded); music streams: %d of %d%s"),
			XwbFiles.Num(), XWavesOk, XWaves, XWma, XStreamsOk, XStreams,
			XStreamsCooked ? *FString::Printf( TEXT(" (%d of a WMA bank from the cooked ../Music)"), XStreamsCooked ) : TEXT("") );
	if( PcmDir.Len() )
		GWarn->Logf( TEXT("PCM: %d waves of %s written to %s"), PcmWritten, *PcmBanks, *PcmDir );

	// Every cue of every cue table, in every language: at least one variation decodes. M16: the sound banks are the
	// cooked cue tables and the shipped .xsb files; the device takes the cooked one where both exist.
	TArray<FString> CueFiles = GFileManager->FindFiles( *(Dir + TEXT("*.uc2cues")), 1, 0 );
	TArray<FString> XsbFiles = GFileManager->FindFiles( *(Dir + TEXT("*.xsb")), 1, 0 );
	INT XsbOnly = 0, XsbLoaded = 0;
	for( INT i=0; i<XsbFiles.Num(); i++ )
	{
		FString Stem = XsbFiles(i).Left( XsbFiles(i).Len()-4 );
		FUC2SoundBank XB;
		FString Error;
		if( !UC2LoadXsbSoundBank( *(Dir + XsbFiles(i)), XB, Error ) )
		{
			GWarn->Logf( TEXT("FAIL %s: %s"), *XsbFiles(i), *Error );
			Failures++;
		}
		else
		{
			XsbLoaded++;
			if( DumpDir.Len() )
				DumpSoundBank( XB, DumpDir + TEXT("/") + Stem + TEXT(".xsb.txt") );
		}
		if( CueFiles.FindItemIndex( Stem + TEXT(".uc2cues") )==INDEX_NONE )
		{
			new(CueFiles) FString( XsbFiles(i) );
			XsbOnly++;
		}
	}
	if( XsbFiles.Num() )
		GWarn->Logf( TEXT("Xbox sound banks: %d .xsb files, %d load; %d have no cooked cue table (the device reads them)"),
			XsbFiles.Num(), XsbLoaded, XsbOnly );
	INT TotalCues = 0;
	for( INT l=0; l<ARRAY_COUNT(GCheckLangs); l++ )
	{
		INT Cues = 0, Full = 0, Partial = 0, None = 0, Localised = 0;
		for( INT f=0; f<CueFiles.Num(); f++ )
		{
			FUC2SoundBank SB;
			FString Error;
			UBOOL bXsb = CueFiles(f).Right(4)==TEXT(".xsb");
			if( bXsb ? !UC2LoadXsbSoundBank( *(Dir + CueFiles(f)), SB, Error ) : !UC2LoadSoundBank( *(Dir + CueFiles(f)), SB, Error ) )
			{
				if( l==0 )
				{
					GWarn->Logf( TEXT("FAIL %s: %s"), *CueFiles(f), *Error );
					Failures++;
				}
				continue;
			}
			if( l==0 && DumpDir.Len() && !bXsb )
				DumpSoundBank( SB, DumpDir + TEXT("/") + CueFiles(f).Left( CueFiles(f).Len()-8 ) + TEXT(".cooked.txt") );
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
							// The device's lookup (GetWaveBank/LoadWaveBankFile): <Bank>_<lang> then <Bank>, each cooked
							// (.uc2wb) first, else the shipped .xwb.
							const INT* Bi = NULL;
							if( *GCheckLangs[l] )
							{
								FString LangStem = V.BankName + TEXT("_") + GCheckLangs[l];
								Bi = ByFile.Find( LangStem + TEXT(".uc2wb") );
								if( !Bi )
									Bi = ByFile.Find( LangStem + TEXT(".xwb") );
								bLocal |= Bi!=NULL;
							}
							if( !Bi )
								Bi = ByFile.Find( V.BankName + TEXT(".uc2wb") );
							if( !Bi )
								Bi = ByFile.Find( V.BankName + TEXT(".xwb") );
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
