/*=============================================================================
	UC2ALAudioSubsystem.cpp: XACT behaviour on OpenAL Soft.

	The specification is Epic's Xbox device, XboxAudio/Src/XACTAudioSubsystem.cpp,
	and the XACT documentation of the Xbox SDK; references in the comments are to
	that file ("XACT :line"). docs/porting/analysis/audio.md has the mapping and
	every known difference.

	Units: volumes in hundredths of a dB (mB), pitch in cents, times in ms in the
	cooked data, seconds at run time. Distance attenuation is computed here with
	DirectSound's rolloff formula and the sum of all volumes is clamped to 0 dB as
	XACT/DirectSound did (they cannot amplify), so OpenAL's own distance model is
	off (AL_NONE) and positions only steer panning.
=============================================================================*/

#include "UC2ALAudioPrivate.h"
#include "UnMovie.h"		// M9: FMovieAudioStream

#ifndef __EMSCRIPTEN__
#include <stdlib.h>		// setenv for the wave writer
#endif

/*-----------------------------------------------------------------------------
	Run-time state.
-----------------------------------------------------------------------------*/

// Music is refilled from the game thread once a frame: 4 x 32768 frames (3 s at 44.1 kHz) ride out the long frames
// of a first-time load (8192-frame buffers left audible gaps under software GL: logs/m8/deadbolt-2).
enum { UC2_MUSIC_SLOTS = 2, UC2_MUSIC_BUFFERS = 4, UC2_MUSIC_BUFFER_FRAMES = 32768 };

// XACT :64 - an actor must move this far (squared) before its sound sources are moved.
#define UC2_MIN_UPDATE_DELTA	32.f
// XACT :2008
#define UC2_MAX_AMBIENT			5

// The banks XACT :215-237 registers at start-up and keeps across levels.
static const TCHAR* GPersistentBanks[] =
{
	TEXT("A_Character"), TEXT("A_Combo"), TEXT("A_Gameplay"), TEXT("A_Pickup"), TEXT("A_Weapon"),
	TEXT("A_WeaponMelee"), TEXT("A_WeaponRace"), TEXT("A_Announcer"), TEXT("A_Announcer_MK"),
	TEXT("A_Ann_Female"), TEXT("A_TauntAnubis"), TEXT("A_TauntApophis"), TEXT("A_TauntArclite"),
	TEXT("A_TauntBrock"), TEXT("A_TauntLauren"), TEXT("A_TauntMalcolm"), TEXT("A_TauntProto"),
	TEXT("A_TauntRaiden"), TEXT("A_TauntRaptor"), TEXT("A_TauntSapphire"), TEXT("A_TauntSelket"),
	TEXT("A_TauntSobek"), TEXT("A_TauntSzalor"),
};

// One per (actor, 2D/3D): XACT's FSourceInfo.
struct FUC2Emitter
{
	AActor*		Actor;				// NULL once the actor is destroyed (its cues play out)
	UBOOL		b2D;
	INT			AmbientInst;		// cue instance id of the actor's ambient cue, or 0
	INT			ActiveCues;
	FVector		Location;
	INT			ListenerViewport;
	UBOOL		bValid;
};

struct FUC2TrackPlay
{
	INT				Voice;			// -1: none
	DOUBLE			StartAt;		// when the play event fires
	INT				LoopsLeft;		// UC2_LOOP_INFINITE = forever
	UBOOL			bStarted, bDone, bQueueMode;
	INT				Variation;
	FLOAT			VarVolume, VarPitch;
	TArray<FLOAT>	EventRandom;	// per event: the value a "random" event picked
};

struct FUC2CueInst
{
	INT						Id;
	INT						Bank, Sound, Cue;
	INT						Emitter;	// -1: fire and forget (2D, no actor)
	UBOOL					bAmbient, b2D;
	DOUBLE					StartTime;
	FLOAT					Rpc;		// the "Volume" parameter, 0..1 (0 = not set)
	FString					Name;		// as requested, for the log
	TArray<FUC2TrackPlay>	Tracks;
};

struct FUC2Voice
{
	ALuint		Source;
	INT			Inst;			// cue instance id, 0 = free
	INT			Track;
	INT			Priority;
	DOUBLE		StartTime;
	ALuint		Buffer;			// the buffer it plays (queue mode: the last one queued)
	INT			BufferChannels, BufferRate;
};

struct FUC2MusicSlot
{
	INT					Handle;		// 0 = free
	FString				Song;
	FUC2VorbisStream	Stream;
	ALuint				Source;
	ALuint				Buffers[UC2_MUSIC_BUFFERS];
	UBOOL				bLoop, bEnded, bStopAfterFade;
	INT					Volume;		// the A_Music sound's volume, mB
	FLOAT				Fade, FadeTarget, FadeRate;
	FUC2MusicSlot() : Handle(0), Source(0), bLoop(0), bEnded(0), bStopAfterFade(0), Volume(0), Fade(1.f),
		FadeTarget(1.f), FadeRate(0.f) { appMemzero( Buffers, sizeof(Buffers) ); }
};

struct FUC2CueStats
{
	INT Requested, Played, Culled, Failed, NoVoice;
	FUC2CueStats() : Requested(0), Played(0), Culled(0), Failed(0), NoVoice(0) {}
};

struct FUC2AudioState
{
	// OpenAL
	UBOOL					bDevice;
	ALCdevice*				Device;
	ALCcontext*				Context;
	UBOOL					bEFX, bEAXReverb, bLoopPoints;
	ALuint					Slot, Effect;
	INT						ZonePreset;
	TMap<INT,ALuint>		SendFilters;	// I3DL2 send level (mB) -> low-pass filter used as a send gain
	TArray<FUC2Voice>		Voices;
	FUC2MusicSlot			Music[UC2_MUSIC_SLOTS];
	INT						NextMusicHandle;

	// Cooked data
	FString					SoundsDir;
	FString					Lang;
	TArray<FUC2SoundBank*>	SoundBanks;
	TArray<FUC2WaveBank*>	WaveBanks;
	TMap<FName,INT>			CueMap;			// requested name -> (bank << 16) | cue, or -1
	TMap<FString,INT>		LoggedOnce;

	DOUBLE					T0;				// appSeconds() when the device opened (capture time 0)
	UBOOL					bTest;			// loopback self-test: simulated clock, listener at the origin
	DOUBLE					TestClock;
	LPALCRENDERSAMPLESSOFT	RenderSamples;

	// Play state
	UViewport*				Viewport;
	TArray<FUC2Emitter>		Emitters;
	TArray<FUC2CueInst>		Insts;
	INT						NextInstId;
	UBOOL					bPaused;
	DOUBLE					PauseStart;
	FLOAT					CategoryVolumes[UAudioSubsystem::VOLUME_Max];
	FLOAT					LastAmbientUpdate;
	TArray<AAmbientSound*>	StaticAmbients;
	UBOOL					bAmbientsListed;
	TArray<INT>				ActiveAmbients;
	TMap<FString,FUC2CueStats> Stats;
	TArray<class FUC2ALMovieStream*> MovieStreams;	// M9: movie sound streams alive (detached on Destroy)
	FString					Notice;			// O-1: why sound is off or incomplete, shown once on the console
	FString					Backend;		// the OpenAL Soft backend playing ("" = unknown or none)

	FUC2AudioState() : bDevice(0), Device(NULL), Context(NULL), bEFX(0), bEAXReverb(0), bLoopPoints(0), Slot(0),
		Effect(0), ZonePreset(-1), NextMusicHandle(1), Viewport(NULL), NextInstId(1), bPaused(0), PauseStart(0),
		LastAmbientUpdate(-1.f), bAmbientsListed(0), T0(0.0), bTest(0), TestClock(0.0), RenderSamples(NULL)
	{
		for( INT i=0; i<UAudioSubsystem::VOLUME_Max; i++ )
			CategoryVolumes[i] = 1.f;
	}
};

/*-----------------------------------------------------------------------------
	Helpers.
-----------------------------------------------------------------------------*/

// The device's clock: wall time, or the self-test's simulated time.
static DOUBLE NowOf( FUC2AudioState* S )
{
	return S->bTest ? S->TestClock : appSeconds();
}

static void CueLogf( UBOOL bOn, FUC2AudioState* S, const TCHAR* Fmt, ... )
{
	if( !bOn || FName::SafeSuppressed(NAME_DevSound) )
		return;
	TCHAR Buf[1024];
	GET_VARARGS( Buf, ARRAY_COUNT(Buf), Fmt, Fmt );
	debugf( NAME_DevSound, TEXT("UC2Audio: [%7.2f] %s"), (FLOAT)(NowOf(S) - S->T0), Buf );
}

static FLOAT MBToGain( FLOAT MB )
{
	return MB<=-10000.f ? 0.f : appPow( 10.f, MB/2000.f );
}

// XACT :864-873 ("graciously yanked from UC"): 1000*log10, i.e. half the dB a true amplitude ratio gives.
static FLOAT CategoryMB( FLOAT Volume )
{
	if( Volume < KINDA_SMALL_NUMBER )
		return -10000.f;
	return 1000.f * appLoge( Clamp( Volume, 0.1f, 1.f ) ) / appLoge( 10.f );
}

// DirectSound's distance rolloff (XACT computed it itself for every sound on a 3D source):
// gain = Min / (Min + Rolloff*(d - Min)) with d clamped to [Min, Max]; in mB.
static FLOAT DistanceMB( FLOAT Distance, FLOAT Min, FLOAT Max, FLOAT Rolloff )
{
	FLOAT D = Clamp( Distance, Min, Max );
	FLOAT Ratio = 1.f + Rolloff * (D - Min) / Min;
	return Ratio<=1.f ? 0.f : -2000.f * appLoge( Ratio ) / appLoge( 10.f );
}

static FLOAT RandRange( INT Low, INT High )
{
	return Low==High ? (FLOAT)Low : Low + appFrand() * (High - Low);
}

// The cue log (DevSound, quiet by default): one line per cue request and outcome, stamped with the seconds
// since the device opened, which is also the time in a capture written by the wave writer.
struct FUC2AudioState;
static void CueLogf( UBOOL bOn, FUC2AudioState* S, const TCHAR* Fmt, ... );

/*-----------------------------------------------------------------------------
	Banks.
-----------------------------------------------------------------------------*/

static void LogOnce( FUC2AudioState* S, const FString& Key, const FString& Message )
{
	if( S->LoggedOnce.Find( Key ) )
		return;
	S->LoggedOnce.Set( *Key, 1 );
	debugf( NAME_DevSound, TEXT("UC2Audio: %s"), *Message );
}

static INT FindSoundBank( FUC2AudioState* S, const FString& Name )
{
	for( INT i=0; i<S->SoundBanks.Num(); i++ )
		if( S->SoundBanks(i)->Name==Name )
			return i;
	return -1;
}

// XACT GetBankIdx :526 (the sound bank half): load <Bank>.uc2cues once; keep it across levels if persistent.
static INT GetSoundBank( FUC2AudioState* S, const FString& Name, UBOOL bPersist )
{
	guard(GetSoundBank);
	INT Index = FindSoundBank( S, Name );
	if( Index>=0 )
	{
		S->SoundBanks(Index)->bPersist |= bPersist;
		return Index;
	}
	FString Error;
	FUC2SoundBank* Bank = new FUC2SoundBank;
	if( !UC2LoadSoundBank( *(S->SoundsDir + Name + TEXT(".uc2cues")), *Bank, Error ) )
	{
		delete Bank;
		LogOnce( S, FString(TEXT("bank:")) + Name, FString::Printf( TEXT("sound bank %s not loaded (%s)"), *Name, *Error ) );
		return -1;
	}
	Bank->Name = Name;	// as requested (the file names it the same)
	Bank->bPersist = bPersist;
	// Variations without a cooked wave are left out at play time; say so once per bank, at load.
	debugf( NAME_DevSound, TEXT("UC2Audio: loaded sound bank %s (%d sounds, %d cues)%s"), *Name, Bank->Sounds.Num(),
		Bank->Cues.Num(), bPersist ? TEXT(", persistent") : TEXT("") );
	return S->SoundBanks.AddItem( Bank );
	unguard;
}

// The wave bank half of GetBankIdx: <Bank>_<lang> first, then <Bank> (XACT :546-560).
static INT GetWaveBank( FUC2AudioState* S, const FString& Name )
{
	guard(GetWaveBank);
	for( INT i=0; i<S->WaveBanks.Num(); i++ )
		if( S->WaveBanks(i)->Name==Name )
			return i;
	FString Error;
	FUC2WaveBank* Bank = new FUC2WaveBank;
	UBOOL bOk = 0;
	if( S->Lang.Len() )
	{
		Bank->File = Name + TEXT("_") + S->Lang + TEXT(".uc2wb");
		bOk = UC2LoadWaveBank( *(S->SoundsDir + Bank->File), *Bank, Error );
	}
	if( !bOk )
	{
		Bank->Waves.Empty();
		Bank->File = Name + TEXT(".uc2wb");
		bOk = UC2LoadWaveBank( *(S->SoundsDir + Bank->File), *Bank, Error );
	}
	if( !bOk )
	{
		delete Bank;
		LogOnce( S, FString(TEXT("wb:")) + Name, FString::Printf( TEXT("wave bank %s not loaded (%s)"), *Name, *Error ) );
		return -1;
	}
	INT Missing = 0;
	for( INT i=0; i<Bank->Waves.Num(); i++ )
		if( Bank->Waves(i).bMissing )
		{
			Missing++;
			debugf( NAME_DevSound, TEXT("UC2Audio: %s entry %d (%s) has no cooked wave: variations using it are skipped"),
				*Bank->File, i, *Bank->Waves(i).Name );
		}
	Bank->Name = Name;
	debugf( NAME_DevSound, TEXT("UC2Audio: loaded wave bank %s (%d waves%s)"), *Bank->File, Bank->Waves.Num(),
		Missing ? *FString::Printf( TEXT(", %d missing"), Missing ) : TEXT("") );
	return S->WaveBanks.AddItem( Bank );
	unguard;
}

static FUC2Wave* ResolveVariation( FUC2AudioState* S, FUC2Variation& V, FUC2WaveBank** OutBank=NULL )
{
	if( V.Bank<0 || V.Bank>=S->WaveBanks.Num() || S->WaveBanks(V.Bank)->Name!=V.BankName )
		V.Bank = GetWaveBank( S, V.BankName );
	if( V.Bank<0 )
		return NULL;
	FUC2WaveBank* Bank = S->WaveBanks(V.Bank);
	if( V.Entry<0 || V.Entry>=Bank->Waves.Num() || Bank->Waves(V.Entry).bMissing )
		return NULL;
	if( OutBank )
		*OutBank = Bank;
	return &Bank->Waves(V.Entry);
}

// Decodes a wave into an OpenAL buffer on first use (mono downmix for 3D voices of stereo waves).
static ALuint EnsureBuffer( FUC2AudioState* S, FUC2WaveBank* Bank, FUC2Wave* W, UBOOL bMono )
{
	guard(EnsureBuffer);
	if( !S->bDevice || W->bDecodeFailed )
		return 0;
	UBOOL bDownmix = bMono && W->Channels==2;
	ALuint& Target = bDownmix ? W->MonoBuffer : W->Buffer;
	if( Target )
		return Target;
	if( Bank->bStream )
	{
		LogOnce( S, FString(TEXT("stream:")) + Bank->Name, FString::Printf( TEXT("%s is a streamed (music) bank; play it with PlayMusic"), *Bank->Name ) );
		return 0;
	}
	if( !Bank->bDataTried )
	{
		Bank->bDataTried = 1;
		DOUBLE ReadStart = appSeconds();
		if( !appLoadFileToArray( Bank->Data, *(S->SoundsDir + Bank->DataFile) ) )
			LogOnce( S, FString(TEXT("data:")) + Bank->DataFile, FString::Printf( TEXT("cannot read %s"), *(S->SoundsDir + Bank->DataFile) ) );
		else
			// One line per wave pack and run: what loading it cost (on the web: the fetch of the whole file on first use).
			debugf( NAME_Log, TEXT("UC2Audio: [%7.2f] read wave pack %s (%i bytes) in %.0f ms"), (FLOAT)(NowOf(S) - S->T0), *Bank->DataFile,
				Bank->Data.Num(), 1000.0 * (appSeconds() - ReadStart) );
	}
	if( W->Offset<0 || W->Size<=0 || W->Offset+W->Size > Bank->Data.Num() )
	{
		W->bDecodeFailed = 1;
		LogOnce( S, FString(TEXT("range:")) + W->Name, FString::Printf( TEXT("%s: wave %s outside its data file"), *Bank->File, *W->Name ) );
		return 0;
	}
	TArray<SWORD> Pcm;
	INT Channels = 0, Rate = 0;
	FString Error;
	if( !UC2DecodeVorbis( &Bank->Data(W->Offset), W->Size, Pcm, Channels, Rate, Error, W->Frames ) || !Channels )
	{
		W->bDecodeFailed = 1;
		LogOnce( S, FString(TEXT("dec:")) + W->Name, FString::Printf( TEXT("%s: %s does not decode (%s)"), *Bank->File, *W->Name, *Error ) );
		return 0;
	}
	INT Frames = Pcm.Num() / Channels;
	if( Frames!=W->Frames )
		LogOnce( S, FString(TEXT("len:")) + W->Name, FString::Printf( TEXT("%s: %s decodes to %d frames, the index says %d"), *Bank->File, *W->Name, Frames, W->Frames ) );
	if( bDownmix )
	{
		for( INT i=0; i<Frames; i++ )
			Pcm(i) = (SWORD)(((INT)Pcm(2*i) + (INT)Pcm(2*i+1)) / 2);
		Pcm.Remove( Frames, Pcm.Num()-Frames );
		Channels = 1;
	}
	GAL.alGetError();
	GAL.alGenBuffers( 1, &Target );
	GAL.alBufferData( Target, UC2ALFormat(Channels), Pcm.Num() ? &Pcm(0) : NULL, Pcm.Num()*sizeof(SWORD), Rate );
	// A loop region shorter than the wave (XACT loops it, the tail plays only when the loop is released).
	if( S->bLoopPoints && W->LoopLength>0 && (W->LoopStart>0 || W->LoopStart+W->LoopLength<Frames) )
	{
		ALint Points[2] = { W->LoopStart, Min( W->LoopStart+W->LoopLength, Frames ) };
		GAL.alBufferiv( Target, AL_LOOP_POINTS_SOFT, Points );
	}
	if( GAL.alGetError()!=AL_NO_ERROR )
	{
		LogOnce( S, FString(TEXT("buf:")) + W->Name, FString::Printf( TEXT("%s: OpenAL refused the buffer of %s"), *Bank->File, *W->Name ) );
		if( Target )
			GAL.alDeleteBuffers( 1, &Target );
		Target = 0;
		W->bDecodeFailed = 1;
	}
	return Target;
	unguard;
}

static void FreeWaveBankBuffers( FUC2WaveBank* Bank )
{
	for( INT i=0; i<Bank->Waves.Num(); i++ )
	{
		FUC2Wave& W = Bank->Waves(i);
		if( W.Buffer )		GAL.alDeleteBuffers( 1, &W.Buffer );
		if( W.MonoBuffer )	GAL.alDeleteBuffers( 1, &W.MonoBuffer );
		W.Buffer = W.MonoBuffer = 0;
	}
}

// XACT GetNamedCueIdx/RegisterSoundCue :800-856, 2391-2465: "Bank.Group.Cue" -> bank "Bank", friendly name
// "Bank\Cue" (the first and the last dot-separated parts).
static INT ResolveCue( FUC2AudioState* S, FName CueName )
{
	guard(ResolveCue);
	const INT* Found = S->CueMap.Find( CueName );
	if( Found )
		return *Found;
	INT Result = -1;
	FString Src = *CueName;
	TArray<FString> Parts;
	if( Src.ParseIntoArray( TEXT("."), &Parts ) >= 2 )
	{
		FString BankName = Parts(0);
		FString Friendly = BankName + TEXT("\\") + Parts(Parts.Num()>2 ? 2 : 1);
		INT Bank = GetSoundBank( S, BankName, 0 );
		if( Bank>=0 )
		{
			const INT* Cue = S->SoundBanks(Bank)->CueIndex.Find( Friendly );
			if( Cue )
				Result = (Bank<<16) | *Cue;
		}
	}
	S->CueMap.Set( CueName, Result );
	return Result;
	unguard;
}

/*-----------------------------------------------------------------------------
	UUC2ALAudioSubsystem.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(UUC2ALAudioSubsystem);

UUC2ALAudioSubsystem::UUC2ALAudioSubsystem()
{
	// Survives level changes, as the Xbox device (XACT :113).
	SetFlags( RF_TagPersist );
	State = NULL;
}

void UUC2ALAudioSubsystem::StaticConstructor()
{
	guard(UUC2ALAudioSubsystem::StaticConstructor);
	new(GetClass(),TEXT("OutputDevice"), RF_Public)UStrProperty  (CPP_PROPERTY(OutputDevice), TEXT("Audio"), CPF_Config );
	new(GetClass(),TEXT("Backends"),     RF_Public)UStrProperty  (CPP_PROPERTY(Backends),     TEXT("Audio"), CPF_Config );
	new(GetClass(),TEXT("WaveFile"),     RF_Public)UStrProperty  (CPP_PROPERTY(WaveFile),     TEXT("Audio"), CPF_Config );
	new(GetClass(),TEXT("MaxVoices"),    RF_Public)UIntProperty  (CPP_PROPERTY(MaxVoices),    TEXT("Audio"), CPF_Config );
	new(GetClass(),TEXT("ZoneReverb"),   RF_Public)UBoolProperty (CPP_PROPERTY(ZoneReverb),   TEXT("Audio"), CPF_Config );
	new(GetClass(),TEXT("UseEFX"),       RF_Public)UBoolProperty (CPP_PROPERTY(UseEFX),       TEXT("Audio"), CPF_Config );
	new(GetClass(),TEXT("MusicFades"),   RF_Public)UBoolProperty (CPP_PROPERTY(MusicFades),   TEXT("Audio"), CPF_Config );
	new(GetClass(),TEXT("CueLog"),       RF_Public)UBoolProperty (CPP_PROPERTY(CueLog),       TEXT("Audio"), CPF_Config );
	// Compiled defaults = Default.ini's [UC2ALAudio.UC2ALAudioSubsystem].
	MaxVoices	= 64;
#if !defined(__EMSCRIPTEN__) && !defined(_WIN32)
	// openal-soft's own Linux order, made explicit (O-1): PipeWire, then PulseAudio (also PipeWire's pulse
	// server), then ALSA, then OSS. The null and wave backends are never chosen unless asked for.
	Backends	= TEXT("pipewire,pulse,alsa,oss");
#endif
	ZoneReverb	= 1;
	UseEFX		= 1;
	MusicFades	= 1;
	CueLog		= 1;
	unguard;
}

UBOOL UUC2ALAudioSubsystem::Init()
{
	guard(UUC2ALAudioSubsystem::Init);
	if( State )
		return 1;
	State = new FUC2AudioState;
	FUC2AudioState* S = State;

	// The cooked data sits beside the game's content: <root>/Sounds, <root>/Music (System/ is the working
	// directory, as for the Xbox's "..\Sounds\").
	S->SoundsDir = TEXT("../Sounds/");
	FString Lang = UObject::GetLanguage();
	S->Lang = Lang.Locs()==TEXT("int") ? FString() : Lang.Locs();

	// Category volumes from the default profile (XACT :203-206).
	UPlayerProfile* DefaultProfile = UPlayerProfile::StaticClass()->GetDefaultObject<UPlayerProfile>();
	S->CategoryVolumes[VOLUME_Music]	= Clamp( DefaultProfile->MusicVolume, 0.f, 1.f );
	S->CategoryVolumes[VOLUME_FX]		= Clamp( DefaultProfile->SoundFXVolume, 0.f, 1.f );
	S->CategoryVolumes[VOLUME_Voice]	= Clamp( DefaultProfile->VoiceVolume, 0.f, 1.f );
	S->CategoryVolumes[VOLUME_VoiceChat]= Clamp( DefaultProfile->VoiceChatVolume, 0.f, 1.f );

	// Render to a file instead of a device (tests): OpenAL Soft's wave writer, chosen through its
	// configuration before the library starts. Set with -ini:System:[UC2ALAudio.UC2ALAudioSubsystem]:WaveFile=<file>
	// (F-4e: the -audiowave=<file> switch is gone; the key always existed).
	FString Wave = WaveFile;
#ifndef __EMSCRIPTEN__
	if( Wave.Len() )
	{
		FString Conf = FString(appBaseDir()) + TEXT("UC2ALAudio-wave.conf");
		FString Text = FString::Printf( TEXT("[general]\ndrivers=wave\n[wave]\nfile=%s\n"), *Wave );
		if( appSaveStringToFile( Text, *Conf ) )
		{
#ifdef _WIN32
			// Windows (M12): the CRT's _putenv copies the string and updates the process environment, which
			// OpenAL32.dll reads with _wgetenv.
			_putenv( appToAnsi(*(FString(TEXT("ALSOFT_CONF=")) + Conf)) );
			_putenv( "ALSOFT_DRIVERS=wave" );
#else
			setenv( "ALSOFT_CONF", appToAnsi(*Conf), 1 );
			setenv( "ALSOFT_DRIVERS", "wave", 1 );
#endif
		}
	}
#endif

	// The cooked data (O-1): say so when it is not there, here and once on the console, instead of failing
	// every cue in the (normally suppressed) cue log.
	UBOOL bSoundData = GFileManager->FileSize( *(S->SoundsDir + TEXT("A_Gameplay.uc2cues")) ) > 0;
	UBOOL bMusicData = GFileManager->FindFiles( TEXT("../Music/*.ogg"), 1, 0 ).Num() > 0;
	if( !bSoundData )
		S->Notice = TEXT("Game sound is off (effects, speech, music): no cooked sound data in ../Sounds (tools/cook-audio.py makes it; tools/dev/mkrun-uc2.sh links it into a run tree; a web build needs it in its file manifest).");
	else if( !bMusicData )
		S->Notice = TEXT("Music is off: no cooked music in ../Music (tools/cook-audio.py makes it; tools/dev/mkrun-uc2.sh links it into a run tree; a web build needs it in its file manifest).");
	if( S->Notice.Len() )
		debugf( NAME_Init, TEXT("UC2ALAudio: %s"), *S->Notice );

	FString Why;
	if( !UC2ALBind( Why ) )
	{
		debugf( NAME_Init, TEXT("UC2ALAudio: %s: sound is off"), *Why );
		S->Notice = FString::Printf( TEXT("Sound is off: %s."), *Why );
		return 1;
	}

	// The backend order (O-1): explicit, and what the log reports against. OpenAL Soft reads ALSOFT_DRIVERS
	// when it first opens a device; a value already in the environment (the test harness's "null", the
	// wave writer's "wave") or an empty Backends= leaves the choice to it and its alsoft.conf.
	FString Order;
#ifndef __EMSCRIPTEN__
	const ANSICHAR* EnvDrivers = getenv( "ALSOFT_DRIVERS" );
	if( EnvDrivers && *EnvDrivers )
		Order = FString(appFromAnsi(EnvDrivers)) + TEXT(" (ALSOFT_DRIVERS)");
	else if( Backends.Len() )
	{
#ifdef _WIN32
		_putenv( appToAnsi(*(FString(TEXT("ALSOFT_DRIVERS=")) + Backends)) );
#else
		setenv( "ALSOFT_DRIVERS", appToAnsi(*Backends), 1 );
#endif
		Order = Backends;
	}
#else
	Order = TEXT("web (the only backend of the web build)");
#endif
	UC2ALCaptureLog( 1 );
	ALCint Attribs[] = { ALC_MAX_AUXILIARY_SENDS, 1, 0, 0, 0, 0, 0, 0, 0 };
	if( LoopbackTest )
	{
		// Self-test: mix into memory, 48 kHz stereo float, at a simulated time (ALC_SOFT_loopback).
		LPALCLOOPBACKOPENDEVICESOFT LoopbackOpen = (LPALCLOOPBACKOPENDEVICESOFT)GAL.alcGetProcAddress( NULL, "alcLoopbackOpenDeviceSOFT" );
		S->RenderSamples = (LPALCRENDERSAMPLESSOFT)GAL.alcGetProcAddress( NULL, "alcRenderSamplesSOFT" );
		S->Device = LoopbackOpen && S->RenderSamples ? LoopbackOpen( NULL ) : NULL;
		ALCint Loop[] = { ALC_MAX_AUXILIARY_SENDS, 1, ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT, ALC_FORMAT_TYPE_SOFT,
			ALC_FLOAT_SOFT, ALC_FREQUENCY, UC2_TEST_RATE, 0 };
		appMemcpy( Attribs, Loop, sizeof(Loop) );
		S->bTest = 1;
	}
	else
		S->Device = GAL.alcOpenDevice( OutputDevice.Len() ? appToAnsi(*OutputDevice) : NULL );
	// What openal-soft did while opening it (the backends are initialised by the first open).
	TArray<FString> Problems;
	FString BackendLine = UC2ALBackendReport( *Order, S->Backend, Problems );
	UC2ALCaptureLog( 0 );
	if( LoopbackTest )
		S->Backend = TEXT("loopback");
	debugf( NAME_Init, TEXT("UC2ALAudio: OpenAL Soft backends: %s"), *BackendLine );
	for( INT i=0; i<Problems.Num(); i++ )
		debugf( NAME_Init, TEXT("UC2ALAudio: openal-soft said: %s"), *Problems(i) );
	if( !S->Device )
	{
		debugf( NAME_Init, TEXT("UC2ALAudio: OpenAL could not open %s: sound is off"),
			OutputDevice.Len() ? *FString::Printf(TEXT("device \"%s\""), *OutputDevice) : TEXT("an output device") );
		S->Notice = FString::Printf( TEXT("Sound is off: no audio output device could be opened (OpenAL Soft backends: %s)."), *BackendLine );
		// The names OutputDevice= can take (ALC_ENUMERATE_ALL_EXT: a list of strings ending with an empty one).
		const ALCchar* List = GAL.alcIsExtensionPresent( NULL, "ALC_ENUMERATE_ALL_EXT" ) ? GAL.alcGetString( NULL, ALC_ALL_DEVICES_SPECIFIER )
			: GAL.alcGetString( NULL, ALC_DEVICE_SPECIFIER );
		FString Names;
		for( const ALCchar* P = List; P && *P; P += strlen(P) + 1 )
			Names += FString::Printf( TEXT("%s\"%s\""), Names.Len() ? TEXT(", ") : TEXT(""), appFromAnsi(P) );
		debugf( NAME_Init, TEXT("UC2ALAudio: output devices of that backend: %s"), Names.Len() ? *Names : TEXT("none") );
		return 1;
	}
	S->Context = GAL.alcCreateContext( S->Device, Attribs );
	if( !S->Context || !GAL.alcMakeContextCurrent( S->Context ) )
	{
		debugf( NAME_Init, TEXT("UC2ALAudio: OpenAL could not create a context: sound is off") );
		if( S->Context )
			GAL.alcDestroyContext( S->Context );
		GAL.alcCloseDevice( S->Device );
		S->Context = NULL;
		S->Device = NULL;
		return 1;
	}
	S->bDevice = 1;
	S->T0 = NowOf( S );
	GAL.alDistanceModel( AL_NONE );		// distance attenuation is XACT's, computed in UpdateVoice
	GAL.alDopplerFactor( 0.f );			// XACT :500: no source ever dopplers
	S->bLoopPoints = GAL.alIsExtensionPresent( "AL_SOFT_loop_points" );

	// The voice pool, plus two dedicated music sources.
	INT Want = Clamp( MaxVoices, 8, 255 );
	for( INT i=0; i<Want; i++ )
	{
		ALuint Src = 0;
		GAL.alGetError();
		GAL.alGenSources( 1, &Src );
		if( GAL.alGetError()!=AL_NO_ERROR )
			break;
		FUC2Voice* V = new(S->Voices) FUC2Voice;
		appMemzero( V, sizeof(FUC2Voice) );
		V->Source = Src;
		GAL.alSourcei( Src, AL_SOURCE_RELATIVE, AL_TRUE );
	}
	for( INT i=0; i<UC2_MUSIC_SLOTS; i++ )
	{
		GAL.alGenSources( 1, &S->Music[i].Source );
		GAL.alGenBuffers( UC2_MUSIC_BUFFERS, S->Music[i].Buffers );
		GAL.alSourcei( S->Music[i].Source, AL_SOURCE_RELATIVE, AL_TRUE );
		GAL.alSource3f( S->Music[i].Source, AL_POSITION, 0.f, 0.f, 0.f );
	}

	// Environmental reverb (the Xbox's I3DL2 listener) through EFX, if the device has it.
	// (UseEFX=False, e.g. -ini:System:[UC2ALAudio.UC2ALAudioSubsystem]:UseEFX=False: dry sound for comparison, as on a
	// device without EFX. F-4e: the -NOEFX switch is gone.)
	UBOOL bWantEFX = UseEFX;
	if( bWantEFX && GAL.alcIsExtensionPresent( S->Device, "ALC_EXT_EFX" ) && UC2ALBindEFX() )
	{
		GAL.alGetError();
		GAL.alGenAuxiliaryEffectSlots( 1, &S->Slot );
		GAL.alGenEffects( 1, &S->Effect );
		GAL.alEffecti( S->Effect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB );
		S->bEAXReverb = GAL.alGetError()==AL_NO_ERROR;
		UBOOL bReverb = S->bEAXReverb;
		if( !bReverb )
		{
			GAL.alEffecti( S->Effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB );
			bReverb = GAL.alGetError()==AL_NO_ERROR;
		}
		S->bEFX = S->Slot && S->Effect && bReverb;
		if( S->bEFX )
		{
			UC2ALLoadReverb( S->Effect, S->bEAXReverb, GUC2ZonePresets[0] );
			GAL.alAuxiliaryEffectSloti( S->Slot, AL_EFFECTSLOT_EFFECT, S->Effect );
			S->ZonePreset = 0;
		}
	}
	const ALchar* DeviceName = GAL.alcGetString( S->Device, ALC_ALL_DEVICES_SPECIFIER );
	if( !DeviceName )
		DeviceName = GAL.alcGetString( S->Device, ALC_DEVICE_SPECIFIER );
	ALCint OutRate = 0;
	GAL.alcGetIntegerv( S->Device, ALC_FREQUENCY, 1, &OutRate );
	debugf( NAME_Init, TEXT("UC2ALAudio: %s on \"%s\" (backend %s) at %d Hz, %d voices, reverb %s, loop points %s%s"),
		appFromAnsi((const ANSICHAR*)GAL.alGetString( AL_VERSION )), appFromAnsi(DeviceName ? DeviceName : ""),
		S->Backend.Len() ? *S->Backend : TEXT("unknown"), OutRate,
		S->Voices.Num(), !S->bEFX ? TEXT("off (no EFX: dry sound)") : S->bEAXReverb ? TEXT("EFX (EAX reverb)") : TEXT("EFX (standard reverb)"),
		S->bLoopPoints ? TEXT("yes") : TEXT("no"),
		Wave.Len() ? *FString::Printf( TEXT(", writing %s"), *Wave ) : TEXT("") );
	debugf( NAME_Init, TEXT("UC2ALAudio: category volumes (default profile): Music %.2f, FX %.2f, Voice %.2f"),
		S->CategoryVolumes[VOLUME_Music], S->CategoryVolumes[VOLUME_FX], S->CategoryVolumes[VOLUME_Voice] );
	if( !S->bEFX )
		debugf( NAME_Init, TEXT("UC2ALAudio: %s: zone reverb is off, sound is dry"),
			bWantEFX ? TEXT("the device has no EFX reverb") : TEXT("EFX switched off (UseEFX=False)") );

	// The persistent banks (XACT :215-237). Only their cue tables load now; wave data loads on first play.
	DOUBLE BanksStart = appSeconds();
	INT BanksLoaded = 0;
	for( INT i=0; i<ARRAY_COUNT(GPersistentBanks); i++ )
		BanksLoaded += GetSoundBank( S, GPersistentBanks[i], 1 ) >= 0;
	debugf( NAME_Init, TEXT("UC2ALAudio: %i of %i persistent cue tables read in %.0f ms"), BanksLoaded, ARRAY_COUNT(GPersistentBanks),
		1000.0 * (appSeconds() - BanksStart) );
	return 1;
	unguard;
}

static void DetachMovieStreams( FUC2AudioState* S );

void UUC2ALAudioSubsystem::Destroy()
{
	guard(UUC2ALAudioSubsystem::Destroy);
	if( State )
	{
		FUC2AudioState* S = State;
		DetachMovieStreams( S );
		if( S->bDevice )
		{
			for( INT i=0; i<S->Voices.Num(); i++ )
			{
				GAL.alSourceStop( S->Voices(i).Source );
				GAL.alSourcei( S->Voices(i).Source, AL_BUFFER, 0 );
				GAL.alDeleteSources( 1, &S->Voices(i).Source );
			}
			for( INT i=0; i<UC2_MUSIC_SLOTS; i++ )
			{
				GAL.alSourceStop( S->Music[i].Source );
				GAL.alSourcei( S->Music[i].Source, AL_BUFFER, 0 );
				GAL.alDeleteSources( 1, &S->Music[i].Source );
				GAL.alDeleteBuffers( UC2_MUSIC_BUFFERS, S->Music[i].Buffers );
				S->Music[i].Stream.Close();
			}
			for( INT i=0; i<S->WaveBanks.Num(); i++ )
				FreeWaveBankBuffers( S->WaveBanks(i) );
			if( S->bEFX )
			{
				for( TMap<INT,ALuint>::TIterator It(S->SendFilters); It; ++It )
					GAL.alDeleteFilters( 1, &It.Value() );
				GAL.alDeleteAuxiliaryEffectSlots( 1, &S->Slot );
				GAL.alDeleteEffects( 1, &S->Effect );
			}
			GAL.alcMakeContextCurrent( NULL );
			GAL.alcDestroyContext( S->Context );
			GAL.alcCloseDevice( S->Device );
			UC2ALUnbind();
		}
		// The cue summary: what was asked for and what became of it (DevSound).
		if( CueLog && !FName::SafeSuppressed(NAME_DevSound) )
			Exec( TEXT("UC2AUDIO STATS") );
		for( INT i=0; i<S->SoundBanks.Num(); i++ )
			delete S->SoundBanks(i);
		for( INT i=0; i<S->WaveBanks.Num(); i++ )
			delete S->WaveBanks(i);
		delete S;
		State = NULL;
		debugf( NAME_Exit, TEXT("UC2ALAudio shut down") );
	}
	Super::Destroy();
	unguard;
}

void UUC2ALAudioSubsystem::ShutdownAfterError()
{
	// Stop the mixer quickly; the rest dies with the process.
	if( State && State->bDevice )
	{
		GAL.alcMakeContextCurrent( NULL );
		GAL.alcDestroyContext( State->Context );
		GAL.alcCloseDevice( State->Device );
		State->bDevice = 0;
	}
	Super::ShutdownAfterError();
}

void UUC2ALAudioSubsystem::SetViewport( UViewport* InViewport )
{
	if( State )
		State->Viewport = InViewport;
}

UViewport* UUC2ALAudioSubsystem::GetViewport()
{
	return State ? State->Viewport : NULL;
}

/*-----------------------------------------------------------------------------
	Voices.
-----------------------------------------------------------------------------*/

static FUC2CueInst* FindInst( FUC2AudioState* S, INT Id )
{
	for( INT i=0; i<S->Insts.Num(); i++ )
		if( S->Insts(i).Id==Id )
			return &S->Insts(i);
	return NULL;
}

static void ReleaseVoice( FUC2AudioState* S, INT VoiceIndex )
{
	FUC2Voice& V = S->Voices(VoiceIndex);
	GAL.alSourceStop( V.Source );
	GAL.alSourcei( V.Source, AL_BUFFER, 0 );		// also empties a queue
	GAL.alSourcei( V.Source, AL_LOOPING, AL_FALSE );
	if( S->bEFX )
		GAL.alSource3i( V.Source, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL );
	V.Inst = 0;
	V.Track = 0;
	V.Buffer = 0;
}

// A free voice, or the least important one when all are busy: XACT steals a lower-priority sound for a
// higher one and, at equal priority, the oldest (Xbox SDK, "Audio best practices" ch. 10). Priority
// values: lower = more important (see audio.md). -1 when every voice is more important.
static INT AllocVoice( FUC2AudioState* S, INT Priority, FString& Stolen )
{
	INT Victim = -1;
	for( INT i=0; i<S->Voices.Num(); i++ )
	{
		FUC2Voice& V = S->Voices(i);
		if( !V.Inst )
			return i;
		if( Victim<0 || V.Priority > S->Voices(Victim).Priority
		 || (V.Priority==S->Voices(Victim).Priority && V.StartTime < S->Voices(Victim).StartTime) )
			Victim = i;
	}
	if( Victim<0 || S->Voices(Victim).Priority < Priority )
		return -1;
	FUC2Voice& V = S->Voices(Victim);
	FUC2CueInst* Inst = FindInst( S, V.Inst );
	if( Inst && V.Track<Inst->Tracks.Num() )
	{
		Inst->Tracks(V.Track).Voice = -1;
		Inst->Tracks(V.Track).bDone = 1;
		Stolen = Inst->Name;
	}
	ReleaseVoice( S, Victim );
	return Victim;
}

static ALuint SendFilter( FUC2AudioState* S, INT MB )
{
	const ALuint* F = S->SendFilters.Find( MB );
	if( F )
		return *F;
	ALuint Filter = 0;
	GAL.alGenFilters( 1, &Filter );
	GAL.alFilteri( Filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS );
	GAL.alFilterf( Filter, AL_LOWPASS_GAIN, Clamp( MBToGain( (FLOAT)MB ), 0.f, 1.f ) );
	GAL.alFilterf( Filter, AL_LOWPASS_GAINHF, 1.f );
	S->SendFilters.Set( MB, Filter );
	return Filter;
}

// Weighted choice among the playable variations of a play event, by the event's playlist type (XACT
// authoring guide, "Variation playlist type"). The playlist state lives in the track (shared by every play).
static INT ChooseVariation( FUC2AudioState* S, FUC2Track& T )
{
	TArray<INT> Cand;
	UBOOL bAnyWeight = 0;
	for( INT i=0; i<T.Variations.Num(); i++ )
		if( ResolveVariation( S, T.Variations(i) ) )
		{
			Cand.AddItem( i );
			bAnyWeight |= T.Variations(i).Weight>=0;
		}
	if( !Cand.Num() )
		return -1;
	if( Cand.Num()==1 )
		return T.LastVariation = Cand(0);
	// An unset weight counts as XACT_VARIATION_TABLE_WEIGHT_MAX when others are set (one event mixes them).
	#define WEIGHT(i) (bAnyWeight ? (T.Variations(i).Weight>=0 ? T.Variations(i).Weight : 10000) : 1)
	INT Pick = -1;
	switch( T.Playlist )
	{
		case PL_Ordered:
		case PL_OrderedFromRandom:
		{
			// heaviest first (stable for equal weights), then round and round
			TArray<INT> Order = Cand;
			for( INT a=1; a<Order.Num(); a++ )
				for( INT b=a; b>0 && WEIGHT(Order(b)) > WEIGHT(Order(b-1)); b-- )
					Exchange( Order(b), Order(b-1) );
			INT Pos = Order.FindItemIndex( T.LastVariation );
			if( Pos==INDEX_NONE )
				Pick = T.Playlist==PL_OrderedFromRandom ? Order( appRand() % Order.Num() ) : Order(0);
			else
				Pick = Order( (Pos+1) % Order.Num() );
			break;
		}
		case PL_Shuffle:
		{
			for( INT i=T.Bag.Num()-1; i>=0; i-- )
				if( Cand.FindItemIndex( T.Bag(i) )==INDEX_NONE )
					T.Bag.Remove( i );
			if( !T.Bag.Num() )
				T.Bag = Cand;
			INT k = appRand() % T.Bag.Num();
			Pick = T.Bag(k);
			T.Bag.Remove( k );
			break;
		}
		case PL_ParameterControlled:
			Pick = Cand(0);		// nothing in UC2 sets "=WaveVariation"
			break;
		default:	// PL_Random, PL_RandomNoRepeat
		{
			INT Total = 0;
			for( INT i=0; i<Cand.Num(); i++ )
				if( !(T.Playlist==PL_RandomNoRepeat && Cand(i)==T.LastVariation) )
					Total += WEIGHT(Cand(i));
			INT R = Total>0 ? appRand() % Total : 0;
			for( INT i=0; i<Cand.Num() && Pick<0; i++ )
			{
				if( T.Playlist==PL_RandomNoRepeat && Cand(i)==T.LastVariation )
					continue;
				R -= WEIGHT(Cand(i));
				if( R<0 )
					Pick = Cand(i);
			}
			if( Pick<0 )
				Pick = Cand(0);
			break;
		}
	}
	#undef WEIGHT
	return T.LastVariation = Pick;
}

/*-----------------------------------------------------------------------------
	Cue instances.
-----------------------------------------------------------------------------*/

// The value of a track's Set Volume / Set Pitch events at Elapsed seconds since the cue started.
static FLOAT EventsValue( const FUC2Track& T, const FUC2TrackPlay& P, BYTE Kind, FLOAT Elapsed, FLOAT Base )
{
	FLOAT Value = Base;
	for( INT i=0; i<T.Events.Num(); i++ )
	{
		const FUC2Event& E = T.Events(i);
		if( E.Kind!=Kind || Elapsed*1000.f < E.Time )
			continue;
		FLOAT V;
		if( E.Mode==EM_Random )
			V = P.EventRandom(i);
		else if( E.Mode==EM_Fade && E.Duration>0 )
		{
			FLOAT F = Clamp( (Elapsed*1000.f - E.Time) / E.Duration, 0.f, 1.f );
			if( E.Steps>0 )
				F = (FLOAT)appFloor( F*E.Steps ) / E.Steps;	// XACT steps the fade ("Fade step count")
			V = E.Low + (E.High-E.Low)*F;
		}
		else
			V = E.Low;
		Value = E.bRelative ? Value + V : V;
	}
	return Value - Base;
}

static UBOOL FindViewer( UViewport* Viewport, INT Index, APlayerController*& Out )
{
	Out = NULL;
	if( !UEngine::GEngine || !UEngine::GEngine->Client )
		return 0;
	if( Index<0 || Index>=UEngine::GEngine->Client->Viewports.Num() )
		Index = 0;
	UViewport* V = Index==0 ? Viewport : UEngine::GEngine->Client->Viewports(Index);
	if( !V || !V->Actor || !V->Actor->ViewTarget )
		V = Viewport;
	if( !V || !V->Actor || !V->Actor->ViewTarget )
		return 0;
	Out = V->Actor;
	return 1;
}

// Gain, pitch and position of one voice. Everything XACT summed into a voice's volume is summed here and
// clamped at 0 dB (DirectSound volumes only attenuate).
static void UpdateVoice( UUC2ALAudioSubsystem* Audio, FUC2AudioState* S, FUC2CueInst& Inst, INT TrackIndex, DOUBLE Now )
{
	FUC2TrackPlay& P = Inst.Tracks(TrackIndex);
	if( P.Voice<0 )
		return;
	FUC2Voice& V = S->Voices(P.Voice);
	FUC2Sound& Snd = S->SoundBanks(Inst.Bank)->Sounds(Inst.Sound);
	FUC2Track& T = Snd.Tracks(TrackIndex);
	FLOAT Elapsed = (FLOAT)(Now - Inst.StartTime);

	FLOAT MB = Snd.Volume + P.VarVolume + EventsValue( T, P, EV_SetVolume, Elapsed, 0.f );
	if( !Snd.bHeadroomBoost )
		MB -= 600.f;	// XACT's per-voice headroom; "Headroom Boost" sounds play 6 dB louder
	if( Snd.Category>=0 )
		MB += CategoryMB( S->CategoryVolumes[Snd.Category] );
	if( Inst.Rpc > 0.f )
		for( INT i=0; i<Snd.Rpcs.Num(); i++ )
			if( Snd.Rpcs(i).Name==TEXT("Volume") && Snd.Rpcs(i).Track==TrackIndex )
				MB += Snd.Rpcs(i).Evaluate( Inst.Rpc );

	FVector Pos(0,0,0);
	if( !Inst.b2D && Inst.Emitter>=0 )
	{
		FUC2Emitter& E = S->Emitters(Inst.Emitter);
		APlayerController* Viewer = NULL;
		if( S->bTest || FindViewer( S->Viewport, E.ListenerViewport, Viewer ) )
		{
			// The listener: the view target's location, the controller's rotation (XACT :1691-1696). The
			// source is placed in the listener's frame, so split-screen viewers each hear their own sources
			// (what MakeRelativeToViewport, XACT :366, achieves with its one real listener). Self-test: the
			// listener is at the origin with no rotation.
			FVector Delta = S->bTest ? E.Location : E.Location - Viewer->ViewTarget->Location;
			FVector Local = S->bTest ? Delta : Delta.TransformVectorBy( GMath.UnitCoords / Viewer->Rotation );
			Pos = FVector( Local.Y, Local.Z, -Local.X );	// Unreal (X fwd, Y right, Z up) -> OpenAL
			// A sound without 3D settings on a 3D source gets DirectSound's defaults (min distance 1, rolloff
			// 1: XACT white paper "Mixing and Matching 2D/3D Sounds").
			FLOAT Min = Snd.bIs3D ? Snd.MinDistance : 1.f;
			FLOAT Max = Snd.bIs3D ? Snd.MaxDistance : 1e9f;
			FLOAT Rolloff = Snd.bIs3D ? Snd.Rolloff : 1.f;
			MB += DistanceMB( Delta.Size(), Min, Max, Rolloff );
		}
	}
	MB = Clamp( MB, -10000.f, 0.f );

	FLOAT Cents = Snd.Pitch + P.VarPitch + EventsValue( T, P, EV_SetPitch, Elapsed, 0.f );
	GAL.alSourcef( V.Source, AL_GAIN, MBToGain( MB ) );
	GAL.alSourcef( V.Source, AL_PITCH, Clamp( appPow( 2.f, Cents/1200.f ), 0.01f, 16.f ) );
	GAL.alSource3f( V.Source, AL_POSITION, Pos.X, Pos.Y, Pos.Z );
}

// Starts the play event of a track: choose a variation, find a voice, set it going.
static void StartTrack( UUC2ALAudioSubsystem* Audio, FUC2AudioState* S, FUC2CueInst& Inst, INT TrackIndex, DOUBLE Now )
{
	guard(StartTrack);
	FUC2TrackPlay& P = Inst.Tracks(TrackIndex);
	FUC2Sound& Snd = S->SoundBanks(Inst.Bank)->Sounds(Inst.Sound);
	FUC2Track& T = Snd.Tracks(TrackIndex);
	P.bStarted = 1;
	P.Variation = ChooseVariation( S, T );
	if( P.Variation<0 )
	{
		P.bDone = 1;
		return;
	}
	FUC2WaveBank* WB = NULL;
	FUC2Wave* W = ResolveVariation( S, T.Variations(P.Variation), &WB );
	UBOOL b3DVoice = !Inst.b2D && Inst.Emitter>=0;
	ALuint Buffer = W ? EnsureBuffer( S, WB, W, b3DVoice ) : 0;
	if( !Buffer )
	{
		P.bDone = 1;
		return;
	}
	FString Stolen;
	INT VoiceIndex = AllocVoice( S, Snd.Priority, Stolen );
	if( VoiceIndex<0 )
	{
		P.bDone = 1;
		FUC2CueStats* St = S->Stats.Find( Inst.Name );
		if( St ) St->NoVoice++;
		UBOOL CueLog = Audio->CueLog;
		CueLogf( CueLog, S, TEXT("%s: no voice (all %d busy with more important sounds)"), *Inst.Name, S->Voices.Num() );
		return;
	}
	if( Stolen.Len() )
	{
		UBOOL CueLog = Audio->CueLog;
		CueLogf( CueLog, S, TEXT("%s takes the voice of %s"), *Inst.Name, *Stolen );
	}
	FUC2Voice& V = S->Voices(VoiceIndex);
	if( !S->bTest )
	{
		// The first sound of the run (O-2: the time from the device's start to something audible).
		static UBOOL bFirst = 1;
		if( bFirst )
			debugf( NAME_Log, TEXT("UC2Audio: [%7.2f] first sound: %s"), (FLOAT)(Now - S->T0), *Inst.Name );
		bFirst = 0;
	}
	V.Inst		= Inst.Id;
	V.Track		= TrackIndex;
	V.Priority	= Snd.Priority;
	V.StartTime	= Now;
	V.Buffer	= Buffer;
	V.BufferChannels = (W->Channels==2 && !b3DVoice) ? 2 : 1;
	V.BufferRate = W->Rate;
	P.Voice = VoiceIndex;
	P.VarVolume = RandRange( T.VolLow, T.VolHigh );
	P.VarPitch = RandRange( T.PitchLow, T.PitchHigh );

	// Looping: infinite on one wave = the source loops it (the wave's loop region via AL_SOFT_loop_points);
	// a counted loop, or a new variation on every loop, queues one wave per pass.
	P.bQueueMode = T.Loops!=0 && (T.Loops!=UC2_LOOP_INFINITE || T.bNewVariation);
	GAL.alSourcei( V.Source, AL_BUFFER, 0 );
	if( P.bQueueMode )
	{
		GAL.alSourcei( V.Source, AL_LOOPING, AL_FALSE );
		GAL.alSourceQueueBuffers( V.Source, 1, &Buffer );
		if( P.LoopsLeft>0 )
		{
			ALuint Next = Buffer;
			if( T.bNewVariation )
			{
				INT NV = ChooseVariation( S, T );
				FUC2WaveBank* NB = NULL;
				FUC2Wave* NW = NV>=0 ? ResolveVariation( S, T.Variations(NV), &NB ) : NULL;
				ALuint NBuf = NW ? EnsureBuffer( S, NB, NW, b3DVoice ) : 0;
				if( NBuf && NW->Rate==W->Rate && NW->Channels==W->Channels )
					Next = NBuf;
			}
			GAL.alSourceQueueBuffers( V.Source, 1, &Next );
			if( P.LoopsLeft!=UC2_LOOP_INFINITE )
				P.LoopsLeft--;
		}
	}
	else
	{
		GAL.alSourcei( V.Source, AL_BUFFER, Buffer );
		GAL.alSourcei( V.Source, AL_LOOPING, T.Loops==UC2_LOOP_INFINITE ? AL_TRUE : AL_FALSE );
	}
	// I3DL2 send: 3D voices only (a 3D sound on the default source ignores its 3D settings, the I3DL2 level
	// included; a sound without 3D settings on a 3D source sends at full level - XACT white paper).
	if( S->bEFX )
	{
		if( b3DVoice )
			GAL.alSource3i( V.Source, AL_AUXILIARY_SEND_FILTER, S->Slot, 0, SendFilter( S, Snd.bIs3D ? Snd.I3DL2 : 0 ) );
		else
			GAL.alSource3i( V.Source, AL_AUXILIARY_SEND_FILTER, AL_EFFECTSLOT_NULL, 0, AL_FILTER_NULL );
	}
	UpdateVoice( Audio, S, Inst, TrackIndex, Now );
	GAL.alSourcePlay( V.Source );
	unguard;
}

// Advances a cue instance; returns 0 when it has finished.
static UBOOL UpdateInst( UUC2ALAudioSubsystem* Audio, FUC2AudioState* S, FUC2CueInst& Inst, DOUBLE Now )
{
	guard(UpdateInst);
	UBOOL bAlive = 0;
	FUC2Sound& Snd = S->SoundBanks(Inst.Bank)->Sounds(Inst.Sound);
	for( INT t=0; t<Inst.Tracks.Num(); t++ )
	{
		FUC2TrackPlay& P = Inst.Tracks(t);
		if( P.bDone )
			continue;
		if( !P.bStarted )
		{
			if( Now >= P.StartAt )
				StartTrack( Audio, S, Inst, t, Now );
			if( !P.bDone )
				bAlive = 1;
			continue;
		}
		FUC2Voice& V = S->Voices(P.Voice);
		FUC2Track& T = Snd.Tracks(t);
		if( P.bQueueMode )
		{
			ALint Processed = 0;
			GAL.alGetSourcei( V.Source, AL_BUFFERS_PROCESSED, &Processed );
			while( Processed-- > 0 )
			{
				ALuint Done = 0;
				GAL.alSourceUnqueueBuffers( V.Source, 1, &Done );
				if( P.LoopsLeft>0 )
				{
					ALuint Next = V.Buffer;
					if( T.bNewVariation )
					{
						INT NV = ChooseVariation( S, T );
						FUC2WaveBank* NB = NULL;
						FUC2Wave* NW = NV>=0 ? ResolveVariation( S, T.Variations(NV), &NB ) : NULL;
						ALuint NBuf = NW ? EnsureBuffer( S, NB, NW, V.BufferChannels==1 && NW->Channels==2 ) : 0;
						if( NBuf && NW->Rate==V.BufferRate && (NW->Channels==V.BufferChannels || V.BufferChannels==1) )
							Next = NBuf;
					}
					GAL.alSourceQueueBuffers( V.Source, 1, &Next );
					if( P.LoopsLeft!=UC2_LOOP_INFINITE )
						P.LoopsLeft--;
				}
			}
		}
		ALint State = AL_STOPPED;
		GAL.alGetSourcei( V.Source, AL_SOURCE_STATE, &State );
		if( State==AL_STOPPED )
		{
			ReleaseVoice( S, P.Voice );
			P.Voice = -1;
			P.bDone = 1;
			continue;
		}
		UpdateVoice( Audio, S, Inst, t, Now );
		bAlive = 1;
	}
	return bAlive;
	unguard;
}

static void StopInst( FUC2AudioState* S, FUC2CueInst& Inst )
{
	for( INT t=0; t<Inst.Tracks.Num(); t++ )
	{
		FUC2TrackPlay& P = Inst.Tracks(t);
		if( P.Voice>=0 && S->bDevice )
			ReleaseVoice( S, P.Voice );
		P.Voice = -1;
		P.bDone = 1;
	}
}

// Removes a cue instance and settles its emitter's bookkeeping (XACT ProcessNotifications :1397-1513).
static void RemoveInst( FUC2AudioState* S, INT Index )
{
	FUC2CueInst& Inst = S->Insts(Index);
	StopInst( S, Inst );
	if( Inst.Emitter>=0 && Inst.Emitter<S->Emitters.Num() )
	{
		FUC2Emitter& E = S->Emitters(Inst.Emitter);
		if( E.AmbientInst==Inst.Id )
			E.AmbientInst = 0;
		else if( E.ActiveCues>0 )
			E.ActiveCues--;
	}
	S->Insts.Remove( Index );
}

static INT StartInst( UUC2ALAudioSubsystem* Audio, FUC2AudioState* S, INT Packed, const FString& Name, INT Emitter,
	UBOOL b2D, UBOOL bAmbient, FLOAT Volume, DOUBLE Now )
{
	guard(StartInst);
	INT BankIndex = Packed>>16, CueIndex = Packed & 0xffff;
	FUC2SoundBank* Bank = S->SoundBanks(BankIndex);
	FUC2Cue& Cue = Bank->Cues(CueIndex);
	if( !Cue.Sounds.Num() )
		return 0;
	// A cue picks one of its sounds (weighted); every cue of UC2 has one.
	INT SoundIndex = Cue.Sounds(0);
	if( Cue.Sounds.Num()>1 )
	{
		INT Total = 0;
		for( INT i=0; i<Cue.Sounds.Num(); i++ )
			Total += Cue.Weights(i)>=0 ? Cue.Weights(i) : 1;
		INT R = Total>0 ? appRand() % Total : 0;
		for( INT i=0; i<Cue.Sounds.Num(); i++ )
		{
			R -= Cue.Weights(i)>=0 ? Cue.Weights(i) : 1;
			if( R<0 ) { SoundIndex = Cue.Sounds(i); break; }
		}
	}
	FUC2Sound& Snd = Bank->Sounds(SoundIndex);
	FUC2CueInst* Inst = new(S->Insts) FUC2CueInst;
	Inst->Id		= S->NextInstId++;
	Inst->Bank		= BankIndex;
	Inst->Sound		= SoundIndex;
	Inst->Cue		= CueIndex;
	Inst->Emitter	= Emitter;
	Inst->b2D		= b2D;
	Inst->bAmbient	= bAmbient;
	Inst->StartTime	= Now;
	Inst->Rpc		= Volume;
	Inst->Name		= Name;
	for( INT t=0; t<Snd.Tracks.Num(); t++ )
	{
		FUC2Track& T = Snd.Tracks(t);
		FUC2TrackPlay* P = new(Inst->Tracks) FUC2TrackPlay;
		P->Voice		= -1;
		P->bStarted		= 0;
		P->bDone		= !T.bHasPlay;
		P->bQueueMode	= 0;
		P->Variation	= -1;
		P->VarVolume	= P->VarPitch = 0.f;
		P->LoopsLeft	= T.Loops;
		P->StartAt		= Now + (T.PlayTime + (T.Delay>0 ? appFrand()*T.Delay : 0.f)) / 1000.0;
		for( INT e=0; e<T.Events.Num(); e++ )
			P->EventRandom.AddItem( RandRange( T.Events(e).Low, T.Events(e).High ) );
	}
	INT Id = Inst->Id;
	INT Index = S->Insts.Num()-1;
	if( !UpdateInst( Audio, S, S->Insts(Index), Now ) )
	{
		// Nothing to play (no playable variation, no voice): gone before the caller counts it on its emitter.
		S->Insts(Index).Emitter = -1;
		RemoveInst( S, Index );
		return 0;
	}
	return Id;
	unguard;
}

/*-----------------------------------------------------------------------------
	Emitters (XACT sources).
-----------------------------------------------------------------------------*/

static INT FindEmitter( FUC2AudioState* S, AActor* Actor )
{
	for( INT i=0; i<S->Emitters.Num(); i++ )
		if( S->Emitters(i).bValid && S->Emitters(i).Actor==Actor )
			return i;
	return -1;
}

// XACT GetSourceIdx :424-507.
static INT GetEmitter( FUC2AudioState* S, AActor* Actor, UBOOL bCreate, UBOOL b2D, UBOOL bAmbient )
{
	if( !Actor )
		return -1;
	for( INT i=0; i<S->Emitters.Num(); i++ )
	{
		FUC2Emitter& E = S->Emitters(i);
		if( E.bValid && E.Actor==Actor && ((bAmbient && !bCreate) ? E.AmbientInst!=0 : E.b2D==b2D) )
			return i;
	}
	if( !bCreate )
		return -1;
	INT Index = -1;
	for( INT i=0; i<S->Emitters.Num() && Index<0; i++ )
		if( !S->Emitters(i).bValid )
			Index = i;
	if( Index<0 )
	{
		Index = S->Emitters.Num();
		S->Emitters.AddZeroed();
	}
	FUC2Emitter& E = S->Emitters(Index);
	E.Actor				= Actor;
	E.b2D				= b2D;
	E.AmbientInst		= 0;
	E.ActiveCues		= 0;
	E.Location			= Actor->Location;
	E.ListenerViewport	= 0;
	E.bValid			= 1;
	return Index;
}

static void StopAmbient( FUC2AudioState* S, AActor* Actor )
{
	INT E = GetEmitter( S, Actor, 0, 0, 1 );
	if( E<0 || !S->Emitters(E).AmbientInst )
		return;
	INT Id = S->Emitters(E).AmbientInst;
	for( INT i=0; i<S->Insts.Num(); i++ )
		if( S->Insts(i).Id==Id )
		{
			RemoveInst( S, i );
			break;
		}
	S->Emitters(E).AmbientInst = 0;
}

/*-----------------------------------------------------------------------------
	Cues.
-----------------------------------------------------------------------------*/

void UUC2ALAudioSubsystem::PlaySoundCue( AActor* srcActor, FName soundCue, UBOOL bAmbient, FLOAT Volume )
{
	guard(UUC2ALAudioSubsystem::PlaySoundCue);
	FUC2AudioState* S = State;
	if( !S )
		return;
	if( bAmbient )
		StopAmbient( S, srcActor );
	if( soundCue==NAME_None || !S->Viewport || (srcActor && srcActor->bDeleteMe) )
		return;
	FString Name = *soundCue;
	FUC2CueStats* St = S->Stats.Find( Name );
	if( !St )
	{
		S->Stats.Set( *Name, FUC2CueStats() );
		St = S->Stats.Find( Name );
	}
	St->Requested++;
	INT Packed = ResolveCue( S, soundCue );
	if( Packed<0 )
	{
		St->Failed++;
		CueLogf( CueLog, S, TEXT("%s on %s%s: failed (no such cue)"), *Name,
			srcActor ? srcActor->GetName() : TEXT("none"), bAmbient ? TEXT(" (ambient)") : TEXT("") );
		return;
	}
	if( !S->bDevice )
		return;
	DOUBLE Now = NowOf( S );

	// Fire and forget (XACT :2164-2172).
	if( !srcActor )
	{
		if( StartInst( this, S, Packed, Name, -1, 1, 0, Volume, Now ) )
		{
			St->Played++;
			CueLogf( CueLog, S, TEXT("%s: played 2D (no actor)"), *Name );
		}
		else
		{
			St->Failed++;
			CueLogf( CueLog, S, TEXT("%s: failed (nothing playable)"), *Name );
		}
		return;
	}

	// 2D when the actor is a viewer's controller or belongs to its view target, else 3D for the closest
	// viewer (XACT :2176-2203).
	UBOOL b2D = 0;
	INT Closest = 0;
	FLOAT ClosestDistance = 1000000.f;
	UClient* Client = UEngine::GEngine ? UEngine::GEngine->Client : NULL;
	for( INT i=0; Client && i<Client->Viewports.Num() && !b2D; i++ )
	{
		UViewport* VP = Client->Viewports(i);
		APlayerController* Viewer = VP ? VP->Actor : NULL;
		if( !Viewer || !Viewer->bIsPlayer )
			continue;
		if( srcActor==Viewer || srcActor->IsOwnedBy( Viewer->ViewTarget ) )
			b2D = 1;
		else if( Viewer->ViewTarget )
		{
			FLOAT D = (srcActor->Location - Viewer->ViewTarget->Location).Size();
			if( D<ClosestDistance )
			{
				Closest = i;
				ClosestDistance = D;
			}
		}
	}
	// Far sounds are skipped, more eagerly the more cues play (XACT :2205-2214).
	if( !b2D && !bAmbient && Volume<=0.f )
	{
		FLOAT Fudged = ClosestDistance * (1.f + ::Max( 0.f, S->Insts.Num()/15.f ));
		FLOAT Radius = GetSoundCueRadius( soundCue );
		if( Radius < Fudged )
		{
			St->Culled++;
			CueLogf( CueLog, S, TEXT("%s on %s: culled (distance %.0f, x%.2f for %d cues, radius %.0f)"),
				*Name, srcActor->GetName(), ClosestDistance, 1.f + S->Insts.Num()/15.f, S->Insts.Num(), Radius );
			return;
		}
	}
	INT E = GetEmitter( S, srcActor, 1, b2D, bAmbient );
	if( E<0 )
		return;
	S->Emitters(E).ListenerViewport = b2D ? 0 : Closest;
	S->Emitters(E).Location = srcActor->Location;
	INT Id = StartInst( this, S, Packed, Name, E, b2D, bAmbient, Volume, Now );
	if( !Id )
	{
		St->Failed++;
		CueLogf( CueLog, S, TEXT("%s on %s: failed (nothing playable)"), *Name, srcActor->GetName() );
		return;
	}
	if( bAmbient )
		S->Emitters(E).AmbientInst = Id;
	else
		S->Emitters(E).ActiveCues++;
	St->Played++;
	CueLogf( CueLog, S, TEXT("%s on %s: played %s%s%s"), *Name, srcActor->GetName(),
		b2D ? TEXT("2D") : *FString::Printf( TEXT("3D (distance %.0f)"), ClosestDistance ),
		bAmbient ? TEXT(", ambient") : TEXT(""),
		Volume>0.f ? *FString::Printf( TEXT(", Volume %.2f"), Volume ) : TEXT("") );
	unguardf(( TEXT("SoundCue: %s"), *soundCue ));
}

// XACT :2475-2527.
void UUC2ALAudioSubsystem::StopSoundCue( AActor* srcActor, FName soundCue )
{
	guard(UUC2ALAudioSubsystem::StopSoundCue);
	FUC2AudioState* S = State;
	if( !S || soundCue==NAME_None || !S->Viewport || !srcActor )
		return;
	INT Packed = ResolveCue( S, soundCue );
	if( Packed<0 )
		return;
	UBOOL b2D = srcActor->IsA( AController::StaticClass() )
		|| (S->Viewport->Actor && srcActor->IsOwnedBy( S->Viewport->Actor->ViewTarget ));
	INT E = GetEmitter( S, srcActor, 0, b2D, 0 );
	if( E<0 )
		return;
	for( INT i=S->Insts.Num()-1; i>=0; i-- )
	{
		FUC2CueInst& Inst = S->Insts(i);
		if( Inst.Emitter==E && Inst.Bank==(Packed>>16) && Inst.Cue==(Packed & 0xffff) )
		{
			CueLogf( CueLog, S, TEXT("%s on %s: stopped"), *Inst.Name, srcActor->GetName() );
			RemoveInst( S, i );
		}
	}
	unguard;
}

// The Xbox device's StopSoundCues is commented out entirely (XACT :2529-2563), and nothing calls it.
void UUC2ALAudioSubsystem::StopSoundCues( AActor* srcActor )
{
}

FLOAT UUC2ALAudioSubsystem::GetSoundCueDuration( FName soundCue )
{
	guard(UUC2ALAudioSubsystem::GetSoundCueDuration);
	if( !State || soundCue==NAME_None )
		return 0.f;
	INT Packed = ResolveCue( State, soundCue );
	if( Packed<0 )
		return 0.f;
	FUC2SoundBank* Bank = State->SoundBanks(Packed>>16);
	FUC2Cue& Cue = Bank->Cues(Packed & 0xffff);
	if( !Cue.Sounds.Num() )
		return 0.f;
	INT Ms = Bank->Sounds(Cue.Sounds(0)).LengthMs;
	// XACT_SOUNDCUE_PROPERTIES.dwLength: 0xFFFFFFFF for a cue that loops forever (XACT :2593).
	return Ms<0 ? 4294967295.f/1000.f : Ms/1000.f;
	unguard;
}

// XACT :2609-2636: 2*MinDistance + MinDistance*(2/Rolloff). A sound without 3D settings reports
// DirectSound's defaults (min 1, rolloff 1): radius 4.
FLOAT UUC2ALAudioSubsystem::GetSoundCueRadius( FName soundCue )
{
	guard(UUC2ALAudioSubsystem::GetSoundCueRadius);
	if( !State || soundCue==NAME_None )
		return 0.f;
	INT Packed = ResolveCue( State, soundCue );
	if( Packed<0 )
		return 0.f;
	FUC2SoundBank* Bank = State->SoundBanks(Packed>>16);
	FUC2Cue& Cue = Bank->Cues(Packed & 0xffff);
	if( !Cue.Sounds.Num() )
		return 0.f;
	FUC2Sound& Snd = Bank->Sounds(Cue.Sounds(0));
	FLOAT Min = Snd.bIs3D ? Snd.MinDistance : 1.f;
	FLOAT Rolloff = Snd.bIs3D ? Snd.Rolloff : 1.f;
	return Min*2.f + (Rolloff>0.f ? Min*(2.f/Rolloff) : 1e9f);
	unguard;
}

FLOAT UUC2ALAudioSubsystem::GetCategoryVolume( EVolumeCategory Category )
{
	return State && Category>=0 && Category<VOLUME_Max ? State->CategoryVolumes[Category] : 0.f;
}

void UUC2ALAudioSubsystem::SetCategoryVolume( EVolumeCategory Category, FLOAT NewVolume )
{
	if( State && Category>=0 && Category<VOLUME_Max )
		State->CategoryVolumes[Category] = Clamp( NewVolume, 0.f, 1.f );	// applied by the next Update
}

void UUC2ALAudioSubsystem::NoteDestroy( AActor* Actor )
{
	guard(UUC2ALAudioSubsystem::NoteDestroy);
	FUC2AudioState* S = State;
	if( !S || !Actor )
		return;
	INT E;
	while( (E = FindEmitter( S, Actor ))>=0 )
	{
		StopAmbient( S, Actor );
		FUC2Emitter& Em = S->Emitters(E);
		if( Em.ActiveCues==0 )
			Em.bValid = 0;
		Em.Actor = NULL;		// its cues play out where it was
	}
	unguard;
}

/*-----------------------------------------------------------------------------
	Music: one streamed file per song (XACT PlayMusic :1777, M_<Song> + cue <Song> of A_Music).
-----------------------------------------------------------------------------*/

static UBOOL FillMusicBuffer( FUC2MusicSlot& M, ALuint Buffer )
{
	static TArray<SWORD> Tmp;
	Tmp.Empty( UC2_MUSIC_BUFFER_FRAMES*2 );
	Tmp.Add( UC2_MUSIC_BUFFER_FRAMES*Max(M.Stream.Channels,1) );
	INT Got = M.Stream.Read( &Tmp(0), UC2_MUSIC_BUFFER_FRAMES );
	if( Got<UC2_MUSIC_BUFFER_FRAMES && M.bLoop && M.Stream.Rewind() )
		Got += M.Stream.Read( &Tmp(Got*M.Stream.Channels), UC2_MUSIC_BUFFER_FRAMES-Got );
	if( Got<=0 )
		return 0;
	GAL.alBufferData( Buffer, UC2ALFormat(M.Stream.Channels), &Tmp(0), Got*M.Stream.Channels*sizeof(SWORD), M.Stream.Rate );
	return 1;
}

static void StopMusicSlot( FUC2MusicSlot& M )
{
	GAL.alSourceStop( M.Source );
	GAL.alSourcei( M.Source, AL_BUFFER, 0 );
	M.Stream.Close();
	M.Handle = 0;
	M.Song = TEXT("");
}

INT UUC2ALAudioSubsystem::PlayMusic( FString Song, FLOAT FadeInTime )
{
	guard(UUC2ALAudioSubsystem::PlayMusic);
	FUC2AudioState* S = State;
	if( !S || !S->bDevice || !Song.Len() )
		return 0;
	// One song at a time: the previous one stops (XACT :1781-1784).
	for( INT i=0; i<UC2_MUSIC_SLOTS; i++ )
		if( S->Music[i].Handle )
			StopMusicSlot( S->Music[i] );
	INT BankIndex = GetSoundBank( S, TEXT("A_Music"), 1 );
	const INT* CueIndex = BankIndex>=0 ? S->SoundBanks(BankIndex)->CueIndex.Find( Song ) : NULL;
	if( !CueIndex || !S->SoundBanks(BankIndex)->Cues(*CueIndex).Sounds.Num() )
	{
		CueLogf( CueLog, S, TEXT("music %s: no such cue in A_Music"), *Song );
		return 0;
	}
	FUC2SoundBank* Bank = S->SoundBanks(BankIndex);
	FUC2Sound& Snd = Bank->Sounds( Bank->Cues(*CueIndex).Sounds(0) );
	FUC2WaveBank* WB = NULL;
	FUC2Wave* W = NULL;
	if( Snd.Tracks.Num() && Snd.Tracks(0).Variations.Num() )
		W = ResolveVariation( S, Snd.Tracks(0).Variations(0), &WB );
	if( !W )
	{
		CueLogf( CueLog, S, TEXT("music %s: its wave bank is not cooked"), *Song );
		return 0;
	}
	TArray<BYTE> Data;
	FString File = S->SoundsDir + WB->DataFile, Error;
	FUC2MusicSlot& M = S->Music[0];
	M.Stream.Frames = W->Frames;
	DOUBLE ReadStart = appSeconds();
	UBOOL bRead = appLoadFileToArray( Data, *File );
	DOUBLE ReadSeconds = appSeconds() - ReadStart;
	if( !bRead || !M.Stream.Open( Data, Error ) )
	{
		CueLogf( CueLog, S, TEXT("music %s: cannot play %s (%s)"), *Song, *File, Error.Len() ? *Error : TEXT("unreadable") );
		return 0;
	}
	M.Handle		= S->NextMusicHandle++;
	M.Song			= Song;
	M.bLoop			= Snd.Loops!=0;
	M.bEnded		= 0;
	M.bStopAfterFade= 0;
	M.Volume		= Snd.Volume + (Snd.bHeadroomBoost ? 0 : -600);	// a cue like any other: the 6 dB voice headroom too
	UBOOL bFade		= MusicFades && FadeInTime>0.f;
	M.Fade			= bFade ? 0.f : 1.f;
	M.FadeTarget	= 1.f;
	M.FadeRate		= bFade ? 1.f/FadeInTime : 0.f;
	GAL.alSourcei( M.Source, AL_BUFFER, 0 );
	INT Queued = 0;
	for( INT i=0; i<UC2_MUSIC_BUFFERS; i++ )
		if( FillMusicBuffer( M, M.Buffers[i] ) )
		{
			GAL.alSourceQueueBuffers( M.Source, 1, &M.Buffers[i] );
			Queued++;
		}
	FLOAT MB = M.Volume + CategoryMB( S->CategoryVolumes[VOLUME_Music] );
	GAL.alSourcef( M.Source, AL_GAIN, MBToGain( Clamp( MB, -10000.f, 0.f ) ) * M.Fade );
	GAL.alSourcePlay( M.Source );
	// (Not only in the cue log: one line per song, and what a web run checks that the music started.)
	debugf( NAME_Log, TEXT("UC2Audio: [%7.2f] music %s: playing %s (%d Hz, %d ch, %s, volume %d mB, fade in %.1f s; file read in %.0f ms)"),
		(FLOAT)(NowOf(S) - S->T0), *Song, *File, M.Stream.Rate, M.Stream.Channels, M.bLoop ? TEXT("looping") : TEXT("once"), M.Volume,
		bFade ? FadeInTime : 0.f, 1000.0 * ReadSeconds );
	return M.Handle;
	unguard;
}

UBOOL UUC2ALAudioSubsystem::StopMusic( INT SongHandle, FLOAT FadeOutTime )
{
	guard(UUC2ALAudioSubsystem::StopMusic);
	FUC2AudioState* S = State;
	if( !S || !S->bDevice )
		return 0;
	// The Xbox ignored the handle (one song at a time); a handle of ours stops that song only.
	for( INT i=0; i<UC2_MUSIC_SLOTS; i++ )
	{
		FUC2MusicSlot& M = S->Music[i];
		if( !M.Handle || (SongHandle>0 && SongHandle!=M.Handle) )
			continue;
		CueLogf( CueLog, S, TEXT("music %s: stop (fade out %.1f s)"), *M.Song, MusicFades ? FadeOutTime : 0.f );
		if( MusicFades && FadeOutTime>0.f )
		{
			M.FadeTarget = 0.f;
			M.FadeRate = 1.f/FadeOutTime;
			M.bStopAfterFade = 1;
		}
		else
			StopMusicSlot( M );
	}
	return 0;
	unguard;
}

INT UUC2ALAudioSubsystem::StopAllMusic( FLOAT FadeOutTime )
{
	return StopMusic( 0, FadeOutTime );
}

static void UpdateMusic( UUC2ALAudioSubsystem* Audio, FUC2AudioState* S, FLOAT DeltaTime )
{
	// What a refill costs the frame it happens in (O-2: decoding 32768 frames of Vorbis on the game thread),
	// reported once after the first 8 refills of the run.
	static INT RefillCount = 0;
	static DOUBLE RefillTotal = 0.0, RefillMax = 0.0;
	for( INT i=0; i<UC2_MUSIC_SLOTS; i++ )
	{
		FUC2MusicSlot& M = S->Music[i];
		if( !M.Handle )
			continue;
		ALint Processed = 0;
		GAL.alGetSourcei( M.Source, AL_BUFFERS_PROCESSED, &Processed );
		while( Processed-- > 0 )
		{
			ALuint B = 0;
			GAL.alSourceUnqueueBuffers( M.Source, 1, &B );
			DOUBLE T0 = appSeconds();
			UBOOL bFilled = !M.bEnded && FillMusicBuffer( M, B );
			if( bFilled && !S->bTest && RefillCount < 8 )
			{
				DOUBLE T = appSeconds() - T0;
				RefillTotal += T;
				RefillMax = ::Max( RefillMax, T );
				if( ++RefillCount == 8 )
					debugf( NAME_Log, TEXT("UC2Audio: music refill cost (first 8 buffers of %i frames, decoded on the game thread): %.2f ms on average, %.2f ms at most"),
						(INT)UC2_MUSIC_BUFFER_FRAMES, 1000.0 * RefillTotal / 8, 1000.0 * RefillMax );
			}
			if( bFilled )
				GAL.alSourceQueueBuffers( M.Source, 1, &B );
			else
				M.bEnded = 1;
		}
		if( M.Fade!=M.FadeTarget )
		{
			M.Fade = M.Fade<M.FadeTarget ? ::Min( M.Fade + M.FadeRate*DeltaTime, M.FadeTarget ) : ::Max( M.Fade - M.FadeRate*DeltaTime, M.FadeTarget );
			if( M.Fade<=0.f && M.bStopAfterFade )
			{
				StopMusicSlot( M );
				continue;
			}
		}
		FLOAT MB = M.Volume + CategoryMB( S->CategoryVolumes[UAudioSubsystem::VOLUME_Music] );
		GAL.alSourcef( M.Source, AL_GAIN, MBToGain( Clamp( MB, -10000.f, 0.f ) ) * M.Fade );
		ALint St = AL_STOPPED, Queued = 0;
		GAL.alGetSourcei( M.Source, AL_SOURCE_STATE, &St );
		GAL.alGetSourcei( M.Source, AL_BUFFERS_QUEUED, &Queued );
		if( St!=AL_PLAYING )
		{
			if( Queued>0 )
				GAL.alSourcePlay( M.Source );	// starved (a long frame): carry on
			else if( M.bEnded )
				StopMusicSlot( M );
		}
	}
}

/*-----------------------------------------------------------------------------
	Update.
-----------------------------------------------------------------------------*/

// XACT UpdateAmbientCues :2010-2120: every second, the 5 nearest static AmbientSounds within their cue's
// radius play, the others stop.
static void UpdateAmbientCues( UUC2ALAudioSubsystem* Audio, FUC2AudioState* S )
{
	guard(UpdateAmbientCues);
	APlayerController* PC = S->Viewport->Actor;
	if( !S->bAmbientsListed )
	{
		S->bAmbientsListed = 1;
		ULevel* Level = PC->GetLevel();
		INT ZonePresets[UC2_NUM_ZONE_PRESETS];
		appMemzero( ZonePresets, sizeof(ZonePresets) );
		for( INT i=0; i<Level->Actors.Num(); i++ )
		{
			AAmbientSound* Amb = Cast<AAmbientSound>( Level->Actors(i) );
			if( Amb && Amb->bStatic )
				S->StaticAmbients.AddItem( Amb );
			AZoneInfo* Zone = Cast<AZoneInfo>( Level->Actors(i) );
			if( Zone && Zone->ZoneAudioEffect < UC2_NUM_ZONE_PRESETS )
				ZonePresets[Zone->ZoneAudioEffect]++;
		}
		FString Zones;
		for( INT i=0; i<UC2_NUM_ZONE_PRESETS; i++ )
			if( ZonePresets[i] )
				Zones += FString::Printf( TEXT(" %s x%d"), GUC2ZonePresets[i].Name, ZonePresets[i] );
		debugf( NAME_DevSound, TEXT("UC2Audio: %d static ambient sounds; zone reverb presets in this level:%s"),
			S->StaticAmbients.Num(), *Zones );
	}
	if( !S->StaticAmbients.Num() )
		return;
	AActor* Listener = PC->Pawn ? (AActor*)PC->Pawn : (AActor*)PC;
	TArray<INT> Order;
	TArray<FLOAT> Prio;
	for( INT i=0; i<S->StaticAmbients.Num(); i++ )
	{
		AAmbientSound* Amb = S->StaticAmbients(i);
		FLOAT Radius = Audio->GetSoundCueRadius( Amb->AmbientSoundCue );
		FLOAT P = Radius>0.f ? (Amb->Location - Listener->Location).Size() / Radius : 2.f;
		if( P>1.f )
			continue;
		INT At = 0;
		while( At<Prio.Num() && Prio(At)<=P )
			At++;
		if( At>=UC2_MAX_AMBIENT )
			continue;
		Order.Insert( At );
		Prio.Insert( At );
		Order(At) = i;
		Prio(At) = P;
		if( Order.Num()>UC2_MAX_AMBIENT )
		{
			Order.Pop();
			Prio.Pop();
		}
	}
	for( INT i=0; i<S->ActiveAmbients.Num(); i++ )
	{
		INT Pos = Order.FindItemIndex( S->ActiveAmbients(i) );
		if( Pos==INDEX_NONE )
		{
			StopAmbient( S, S->StaticAmbients(S->ActiveAmbients(i)) );
			S->ActiveAmbients.Remove( i-- );
		}
		else
			Order.Remove( Pos );
	}
	for( INT i=0; i<Order.Num(); i++ )
	{
		AAmbientSound* Amb = S->StaticAmbients(Order(i));
		Audio->PlaySoundCue( Amb, Amb->AmbientSoundCue, 1 );
		S->ActiveAmbients.AddItem( Order(i) );
	}
	unguard;
}

void UUC2ALAudioSubsystem::Update()
{
	guard(UUC2ALAudioSubsystem::Update);
	FUC2AudioState* S = State;
	if( !S )
		return;
	// O-1: why sound or movies are off, once on the console (here: outside rendering's draw calls, with a
	// player controller to tell). The log said it at Init already.
	if( S->Viewport && S->Viewport->Actor )
	{
		FString MovieNotice;
		if( S->Notice.Len() )
		{
			debugf( NAME_Log, TEXT("UC2ALAudio: on the console: %s"), *S->Notice );
			S->Viewport->Actor->eventClientMessage( S->Notice, NAME_None );
			S->Notice = TEXT("");
		}
		if( appTakeMovieNotice( MovieNotice ) )
		{
			debugf( NAME_Log, TEXT("UC2ALAudio: on the console: %s"), *MovieNotice );
			S->Viewport->Actor->eventClientMessage( MovieNotice, NAME_None );
		}
	}
	if( !S->bDevice )
		return;
	static DOUBLE LastTime = 0.0;
	DOUBLE Now = NowOf( S );
	FLOAT DeltaTime = LastTime>0.0 ? Clamp( (FLOAT)(Now-LastTime), 0.f, 1.f ) : 0.f;
	LastTime = Now;

	// Music streams whatever the viewport is doing.
	UpdateMusic( this, S, DeltaTime );

	if( !S->Viewport || !S->Viewport->Actor || !S->Viewport->Actor->ViewTarget )
		return;

	// Emitters: drop the empty ones, follow the moving ones (XACT :1621-1681).
	for( INT i=0; i<S->Emitters.Num(); i++ )
	{
		FUC2Emitter& E = S->Emitters(i);
		if( !E.bValid )
			continue;
		if( E.ActiveCues==0 && !E.AmbientInst )
		{
			E.bValid = 0;
			E.Actor = NULL;
			continue;
		}
		if( E.Actor && !E.Actor->bStatic && (E.Actor->Location - E.Location).SizeSquared() > UC2_MIN_UPDATE_DELTA )
			E.Location = E.Actor->Location;
	}
	while( S->Emitters.Num() && !S->Emitters.Last().bValid )
		S->Emitters.Pop();

	if( !S->bPaused )
		for( INT i=0; i<S->Insts.Num(); i++ )
			if( !UpdateInst( this, S, S->Insts(i), Now ) )
				RemoveInst( S, i-- );

	FLOAT LevelTime = S->Viewport->Actor->Level->TimeSeconds;
	if( LevelTime - S->LastAmbientUpdate > 1.f || LevelTime < S->LastAmbientUpdate )
	{
		S->LastAmbientUpdate = LevelTime;
		UpdateAmbientCues( this, S );
	}
	unguard;
}

/*-----------------------------------------------------------------------------
	Level, zone, pause.
-----------------------------------------------------------------------------*/

// XACT :265-326: stop everything (music too), forget the sources, unload the banks that are not persistent.
void UUC2ALAudioSubsystem::NotifyLevelChange()
{
	guard(UUC2ALAudioSubsystem::NotifyLevelChange);
	FUC2AudioState* S = State;
	if( !S )
		return;
	debugf( NAME_DevSound, TEXT("UC2Audio: level change: stopping %d cues, releasing %d emitters"), S->Insts.Num(), S->Emitters.Num() );
	S->LastAmbientUpdate = -1.f;
	S->StaticAmbients.Empty();
	S->bAmbientsListed = 0;
	S->ActiveAmbients.Empty();
	StopAllMusic( 0.f );
	while( S->Insts.Num() )
		RemoveInst( S, S->Insts.Num()-1 );
	S->Emitters.Empty();
	S->CueMap.Empty();
	for( INT i=S->SoundBanks.Num()-1; i>=0; i-- )
		if( !S->SoundBanks(i)->bPersist )
		{
			delete S->SoundBanks(i);
			S->SoundBanks.Remove( i );
		}
	// A wave bank stays while a persistent sound bank uses it.
	TArray<FString> Keep;
	for( INT i=0; i<S->SoundBanks.Num(); i++ )
		for( INT s=0; s<S->SoundBanks(i)->Sounds.Num(); s++ )
			for( INT t=0; t<S->SoundBanks(i)->Sounds(s).Tracks.Num(); t++ )
				for( INT v=0; v<S->SoundBanks(i)->Sounds(s).Tracks(t).Variations.Num(); v++ )
				{
					// (not AddUniqueItem: TArray::AddItem assigns into unconstructed memory, wrong for FString)
					const FString& Name = S->SoundBanks(i)->Sounds(s).Tracks(t).Variations(v).BankName;
					if( Keep.FindItemIndex( Name )==INDEX_NONE )
						new(Keep) FString( Name );
				}
	for( INT i=S->WaveBanks.Num()-1; i>=0; i-- )
		if( Keep.FindItemIndex( S->WaveBanks(i)->Name )==INDEX_NONE )
		{
			if( S->bDevice )
				FreeWaveBankBuffers( S->WaveBanks(i) );
			delete S->WaveBanks(i);
			S->WaveBanks.Remove( i );
		}
	S->Viewport = NULL;
	unguard;
}

void UUC2ALAudioSubsystem::CleanUp()
{
	// XACT :1337: the same as a level change.
	NotifyLevelChange();
}

void UUC2ALAudioSubsystem::NotifyZoneChange( AZoneInfo* NewZone )
{
	guard(UUC2ALAudioSubsystem::NotifyZoneChange);
	FUC2AudioState* S = State;
	if( !S )
		return;
	if( !S->bEFX )
	{
		CueLogf( CueLog, S, TEXT("zone %s (ZoneAudioEffect %d): no EFX, sound stays dry"), NewZone ? NewZone->GetName() : TEXT("none"),
			NewZone ? NewZone->ZoneAudioEffect : -1 );
		return;
	}
	INT Preset = 0;
	if( ZoneReverb && NewZone && NewZone->ZoneAudioEffect < UC2_NUM_ZONE_PRESETS )
		Preset = NewZone->ZoneAudioEffect;
	if( Preset==S->ZonePreset )
	{
		CueLogf( CueLog, S, TEXT("zone %s (ZoneAudioEffect %d): reverb preset %s unchanged"), NewZone ? NewZone->GetName() : TEXT("none"),
			NewZone ? NewZone->ZoneAudioEffect : -1, GUC2ZonePresets[Preset].Name );
		return;
	}
	S->ZonePreset = Preset;
	UC2ALLoadReverb( S->Effect, S->bEAXReverb, GUC2ZonePresets[Preset] );
	GAL.alAuxiliaryEffectSloti( S->Slot, AL_EFFECTSLOT_EFFECT, S->Effect );
	CueLogf( 1, S, TEXT("zone %s: reverb preset %s"), NewZone ? NewZone->GetName() : TEXT("none"), GUC2ZonePresets[Preset].Name );
	unguard;
}

// XACT :338-351: every cue instance pauses; music plays on.
void UUC2ALAudioSubsystem::NotifyPauseChange( UBOOL bNowPaused )
{
	guard(UUC2ALAudioSubsystem::NotifyPauseChange);
	FUC2AudioState* S = State;
	if( !S || !S->bDevice || !!S->bPaused==!!bNowPaused )
		return;
	S->bPaused = bNowPaused;
	DOUBLE Now = NowOf( S );
	if( bNowPaused )
		S->PauseStart = Now;
	FLOAT Shift = bNowPaused ? 0.f : (FLOAT)(Now - S->PauseStart);
	for( INT i=0; i<S->Insts.Num(); i++ )
	{
		FUC2CueInst& Inst = S->Insts(i);
		Inst.StartTime += Shift;
		for( INT t=0; t<Inst.Tracks.Num(); t++ )
		{
			FUC2TrackPlay& P = Inst.Tracks(t);
			P.StartAt += Shift;
			if( P.Voice>=0 )
			{
				if( bNowPaused )
					GAL.alSourcePause( S->Voices(P.Voice).Source );
				else
					GAL.alSourcePlay( S->Voices(P.Voice).Source );
			}
		}
	}
	debugf( NAME_DevSound, TEXT("UC2Audio: %s %d cues"), bNowPaused ? TEXT("paused") : TEXT("resumed"), S->Insts.Num() );
	unguard;
}

/*-----------------------------------------------------------------------------
	Exec.
-----------------------------------------------------------------------------*/

UBOOL UUC2ALAudioSubsystem::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UUC2ALAudioSubsystem::Exec);
	FUC2AudioState* S = State;
	if( !S )
		return 0;
	const TCHAR* Str = Cmd;
	if( ParseCommand( &Str, TEXT("UC2AUDIO") ) )
	{
		if( ParseCommand( &Str, TEXT("STATS") ) )
		{
			// One line per cue requested: what was asked and what became of it.
			Ar.Logf( TEXT("UC2Audio: cue summary (requested/played/culled/failed/no voice), %d cues:"), S->Stats.Num() );
			for( TMap<FString,FUC2CueStats>::TIterator It(S->Stats); It; ++It )
				Ar.Logf( TEXT("UC2Audio:   %s %d/%d/%d/%d/%d"), *It.Key(), It.Value().Requested, It.Value().Played,
					It.Value().Culled, It.Value().Failed, It.Value().NoVoice );
			INT Busy = 0;
			for( INT i=0; i<S->Voices.Num(); i++ )
				Busy += S->Voices(i).Inst ? 1 : 0;
			Ar.Logf( TEXT("UC2Audio: %d cue instances, %d of %d voices busy, %d emitters, %d sound banks, %d wave banks"),
				S->Insts.Num(), Busy, S->Voices.Num(), S->Emitters.Num(), S->SoundBanks.Num(), S->WaveBanks.Num() );
			return 1;
		}
		if( ParseCommand( &Str, TEXT("ZONE") ) )
		{
			// UC2AUDIO ZONE <n>: force a reverb preset (testing)
			INT Preset = Clamp( appAtoi(Str), 0, UC2_NUM_ZONE_PRESETS-1 );
			if( S->bEFX )
			{
				S->ZonePreset = Preset;
				UC2ALLoadReverb( S->Effect, S->bEAXReverb, GUC2ZonePresets[Preset] );
				GAL.alAuxiliaryEffectSloti( S->Slot, AL_EFFECTSLOT_EFFECT, S->Effect );
			}
			Ar.Logf( TEXT("UC2Audio: reverb preset %s%s"), GUC2ZonePresets[Preset].Name, S->bEFX ? TEXT("") : TEXT(" (no EFX)") );
			return 1;
		}
		if( ParseCommand( &Str, TEXT("PLAY") ) )
		{
			// UC2AUDIO PLAY Bank.Cue: on the viewer, as XACT SOUNDTEST did
			FString CueStr = ParseToken( Str, 0 );
			if( S->Viewport && S->Viewport->Actor && CueStr.Len() )
				PlaySoundCue( S->Viewport->Actor, FName(*CueStr) );
			return 1;
		}
		return 0;
	}
	if( ParseCommand( &Str, TEXT("XACT") ) )
	{
		// The Xbox device's developer commands that make sense here (XACT :982).
		if( ParseCommand( &Str, TEXT("MUSIC") ) )
		{
			FString Song;
			if( Parse( Str, TEXT("SONG="), Song ) )
				PlayMusic( Song, 0.f );
			else
				Ar.Logf( TEXT("Usage: XACT MUSIC SONG=<songname>") );
			return 1;
		}
		if( ParseCommand( &Str, TEXT("SOUNDTEST") ) )
		{
			FString CueStr = ParseToken( Str, 0 );
			if( S->Viewport && S->Viewport->Actor && CueStr.InStr(TEXT("."))!=INDEX_NONE )
				PlaySoundCue( S->Viewport->Actor, FName(*CueStr), 1 );
			else
				Ar.Logf( TEXT("Usage: XACT SOUNDTEST BankName.CueName") );
			return 1;
		}
	}
	return 0;
	unguard;
}

/*-----------------------------------------------------------------------------
	Offline test hooks (UC2AudioSelfTest). The cue goes through PlaySoundCue's own path
	from StartInst on; only the actor is replaced by a location.
-----------------------------------------------------------------------------*/

UBOOL UC2TestPlay( UUC2ALAudioSubsystem* Audio, const TCHAR* Cue, FVector Location, UBOOL b2D, FLOAT Volume, FUC2TestPlay& Out )
{
	guard(UC2TestPlay);
	FUC2AudioState* S = Audio->State;
	appMemzero( &Out, sizeof(Out) );
	if( !S || !S->bTest )
		return 0;
	INT Packed = ResolveCue( S, FName(Cue) );
	if( Packed<0 )
		return 0;
	INT E = -1;
	if( !b2D )
	{
		E = S->Emitters.Num();
		S->Emitters.AddZeroed();
		FUC2Emitter& Em	= S->Emitters(E);
		Em.Location		= Location;
		Em.bValid		= 1;
	}
	INT Id = StartInst( Audio, S, Packed, Cue, E, b2D, 0, Volume, NowOf(S) );
	if( !Id )
		return 0;
	if( E>=0 )
		S->Emitters(E).ActiveCues++;
	FUC2CueInst* Inst = FindInst( S, Id );
	Out.Inst	= Id;
	Out.Sound	= &S->SoundBanks(Inst->Bank)->Sounds(Inst->Sound);
	if( Inst->Tracks.Num() )
	{
		FUC2TrackPlay& P = Inst->Tracks(0);
		Out.Variation	= P.Variation;
		Out.VarVolume	= P.VarVolume;
		Out.VarPitch	= P.VarPitch;
		if( P.Variation>=0 )
			Out.Wave = ResolveVariation( S, Out.Sound->Tracks(0).Variations(P.Variation), &Out.WaveBank );
	}
	Out.b3DVoice = !b2D;
	return 1;
	unguard;
}

INT UC2TestRender( UUC2ALAudioSubsystem* Audio, FLOAT MaxSeconds, TArray<FLOAT>& Out, UBOOL bWhileSounding )
{
	guard(UC2TestRender);
	FUC2AudioState* S = Audio->State;
	if( !S || !S->bTest || !S->RenderSamples )
		return 0;
	const INT Step = UC2_TEST_RATE/100;		// 10 ms: the device updates its cues between steps, as once a frame
	FLOAT Tmp[UC2_TEST_RATE/100*2];
	INT Frames = 0, Tail = -1;
	while( Frames < MaxSeconds*UC2_TEST_RATE )
	{
		for( INT i=0; i<S->Insts.Num(); i++ )
			if( !UpdateInst( Audio, S, S->Insts(i), S->TestClock ) )
				RemoveInst( S, i-- );
		if( bWhileSounding && !S->Insts.Num() && Tail<0 )
			Tail = 5;						// everything stopped: 50 ms more, then done
		S->RenderSamples( S->Device, Tmp, Step );
		INT At = Out.Add( Step*2 );
		appMemcpy( &Out(At), Tmp, sizeof(Tmp) );
		Frames += Step;
		S->TestClock += (DOUBLE)Step/UC2_TEST_RATE;
		if( Tail>=0 && Tail-- == 0 )
			break;
	}
	return Frames;
	unguard;
}

void UC2TestStopAll( UUC2ALAudioSubsystem* Audio )
{
	FUC2AudioState* S = Audio->State;
	if( !S )
		return;
	while( S->Insts.Num() )
		RemoveInst( S, S->Insts.Num()-1 );
	S->Emitters.Empty();
}

/*-----------------------------------------------------------------------------
	M9: a movie's sound (Engine/Inc/UnMovie.h, FMovieAudioStream).

	One 2D source with a queue of small buffers the movie player fills from its own Advance(); the played
	frames (processed buffers + AL_SAMPLE_OFFSET) are the clock the movie's pictures follow. Gain 0 dB in no
	volume category: the Xbox played XMV sound through DirectSound, outside XACT's categories and headroom.
	A full-screen movie is exclusive: every cue and the music pause while it plays, as nothing else ran
	beside a full-screen movie on the Xbox.
-----------------------------------------------------------------------------*/

enum { UC2_MOVIE_BUFFER_FRAMES = 4096, UC2_MOVIE_BUFFERS = 12 };	// 12 x 4096 = 1.1 s at 44.1 kHz

class FUC2ALMovieStream : public FMovieAudioStream
{
public:
	UUC2ALAudioSubsystem*	Audio;
	FUC2AudioState*			S;			// NULL once the device is gone
	ALuint					Source;
	ALuint					Buffers[UC2_MOVIE_BUFFERS];
	TArray<ALuint>			Free;
	TArray<INT>				QueuedSizes;	// frames of each queued buffer, oldest first
	INT						Rate, Channels;
	DOUBLE					PlayedBase, LastPlayed;
	INT						Queued;
	UBOOL					bPaused, bExclusive, bPausedCues;
	TArray<INT>				PausedMusic;

	FUC2ALMovieStream( UUC2ALAudioSubsystem* InAudio, INT InRate, INT InChannels, UBOOL bInExclusive )
	:	Audio(InAudio), S(InAudio->State), Source(0), Rate(InRate), Channels(InChannels), PlayedBase(0.0),
		LastPlayed(0.0), Queued(0), bPaused(0), bExclusive(bInExclusive), bPausedCues(0)
	{
		GAL.alGenSources( 1, &Source );
		GAL.alGenBuffers( UC2_MOVIE_BUFFERS, Buffers );
		for( INT i=0; i<UC2_MOVIE_BUFFERS; i++ )
			Free.AddItem( Buffers[i] );
		GAL.alSourcei( Source, AL_SOURCE_RELATIVE, AL_TRUE );
		GAL.alSource3f( Source, AL_POSITION, 0.f, 0.f, 0.f );
		GAL.alSourcef( Source, AL_GAIN, 1.f );
		if( bExclusive )
		{
			if( !S->bPaused )
			{
				Audio->NotifyPauseChange( 1 );
				bPausedCues = 1;
			}
			for( INT i=0; i<UC2_MUSIC_SLOTS; i++ )
			{
				ALint St = AL_STOPPED;
				GAL.alGetSourcei( S->Music[i].Source, AL_SOURCE_STATE, &St );
				if( S->Music[i].Handle && St == AL_PLAYING )
				{
					GAL.alSourcePause( S->Music[i].Source );
					PausedMusic.AddItem( i );
				}
			}
		}
		S->MovieStreams.AddItem( this );
		// Log, not DevSound: the time is where the movie's sound starts in a WaveFile= capture.
		debugf( NAME_Log, TEXT("UC2Audio: [%7.2f] movie sound stream opened (%i Hz, %i ch%s)"), (FLOAT)(NowOf(S) - S->T0),
			Rate, Channels, bExclusive ? TEXT(", exclusive: other sounds paused") : TEXT("") );
	}
	void Detach()
	{
		if( !S )
			return;
		GAL.alSourceStop( Source );
		GAL.alSourcei( Source, AL_BUFFER, 0 );
		GAL.alDeleteSources( 1, &Source );
		GAL.alDeleteBuffers( UC2_MOVIE_BUFFERS, Buffers );
		S->MovieStreams.RemoveItem( this );
		S = NULL;
	}
	~FUC2ALMovieStream()
	{
		if( !S )
			return;
		FUC2AudioState* St = S;
		Detach();
		if( bExclusive )
		{
			for( INT i=0; i<PausedMusic.Num(); i++ )
				if( St->Music[PausedMusic(i)].Handle )
					GAL.alSourcePlay( St->Music[PausedMusic(i)].Source );
			if( bPausedCues )
				Audio->NotifyPauseChange( 0 );
		}
		debugf( NAME_Log, TEXT("UC2Audio: movie sound stream closed after %.3f s played"), LastPlayed / Rate );
	}
	// Take back the buffers that have played.
	void Service()
	{
		if( !S )
			return;
		ALint Processed = 0;
		GAL.alGetSourcei( Source, AL_BUFFERS_PROCESSED, &Processed );
		while( Processed-- > 0 && QueuedSizes.Num() )
		{
			ALuint B = 0;
			GAL.alSourceUnqueueBuffers( Source, 1, &B );
			PlayedBase += QueuedSizes(0);
			Queued -= QueuedSizes(0);
			QueuedSizes.Remove( 0 );
			Free.AddItem( B );
		}
	}
	INT FreeFrames()
	{
		Service();
		return S ? Free.Num() * UC2_MOVIE_BUFFER_FRAMES : 0;
	}
	void Write( const SWORD* Pcm, INT Frames )
	{
		Service();
		while( S && Frames > 0 && Free.Num() )
		{
			INT N = Min<INT>( Frames, UC2_MOVIE_BUFFER_FRAMES );
			ALuint B = Free.Pop();
			GAL.alBufferData( B, UC2ALFormat(Channels), Pcm, N * Channels * sizeof(SWORD), Rate );
			GAL.alSourceQueueBuffers( Source, 1, &B );
			QueuedSizes.AddItem( N );
			Queued += N;
			Pcm += N * Channels;
			Frames -= N;
		}
		Kick();
	}
	void Kick()
	{
		if( !S || bPaused )
			return;
		ALint St = AL_STOPPED;
		GAL.alGetSourcei( Source, AL_SOURCE_STATE, &St );
		if( St != AL_PLAYING && Queued > 0 )
			GAL.alSourcePlay( Source );		// first data, or starved by a long frame: carry on
	}
	DOUBLE PlayedFrames()
	{
		Service();
		if( !S )
			return LastPlayed;
		ALint Offset = 0;
		GAL.alGetSourcei( Source, AL_SAMPLE_OFFSET, &Offset );
		DOUBLE P = PlayedBase + (QueuedSizes.Num() ? Min<INT>( Offset, QueuedSizes(0) ) : 0);
		// Never backwards (a buffer can finish between the two queries above).
		LastPlayed = Max( LastPlayed, P );
		Kick();
		return LastPlayed;
	}
	INT QueuedFrames()
	{
		Service();
		return Queued;
	}
	void Pause( UBOOL bPause )
	{
		bPaused = bPause;
		if( !S )
			return;
		if( bPause )
			GAL.alSourcePause( Source );
		else
			Kick();
	}
};

static void DetachMovieStreams( FUC2AudioState* S )
{
	while( S->MovieStreams.Num() )
		S->MovieStreams.Last()->Detach();
}

FMovieAudioStream* UUC2ALAudioSubsystem::OpenMovieAudio( INT Rate, INT Channels, UBOOL bExclusive )
{
	guard(UUC2ALAudioSubsystem::OpenMovieAudio);
	if( !State || !State->bDevice || Rate <= 0 || Channels < 1 || Channels > 2 )
		return NULL;
	return new FUC2ALMovieStream( this, Rate, Channels, bExclusive );
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
