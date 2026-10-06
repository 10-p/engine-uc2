/*=============================================================================
	UC2ALAudio.h: the portable audio device of the UC2 port (OpenAL Soft).

	Plays the cooked XACT data (tools/cook-audio.py: Sounds/*.uc2cues cue tables,
	Sounds/*.uc2wb + *.ogp Ogg Vorbis wave banks, Music/*.ogg) the way Epic's Xbox
	device, XboxAudio/Src/XACTAudioSubsystem.cpp, played the XACT banks. The design,
	the cooked format and every known difference from the Xbox are in
	docs/porting/analysis/audio.md.

	The launcher includes this header only to register the classes: nothing of
	OpenAL or Vorbis is visible here (the state lives behind FUC2AudioState).
=============================================================================*/

#ifndef _INC_UC2ALAUDIO
#define _INC_UC2ALAUDIO

#include "Engine.h"

struct FUC2AudioState;

/*-----------------------------------------------------------------------------
	UUC2ALAudioSubsystem.
-----------------------------------------------------------------------------*/

class UUC2ALAudioSubsystem : public UAudioSubsystem
{
	DECLARE_CLASS(UUC2ALAudioSubsystem,UAudioSubsystem,CLASS_Config,UC2ALAudio)

	// Configuration ([UC2ALAudio.UC2ALAudioSubsystem]).
	FStringNoInit	OutputDevice;		// alcOpenDevice name; empty = the default device
	FStringNoInit	Backends;			// OpenAL Soft backends to try, in order (its ALSOFT_DRIVERS, unless the environment sets that)
	FStringNoInit	WaveFile;			// render to this .wav with OpenAL Soft's wave writer (tests); -AUDIOWAVE=
	INT				MaxVoices;			// sources in the voice pool (the Xbox had 188 2D + 64 3D)
	UBOOL			ZoneReverb;			// zone -> I3DL2 preset (the shipped Xbox code had this in #if 0)
	UBOOL			UseEFX;					// environmental reverb through EFX when the device has it
	UBOOL			MusicFades;			// honour PlayMusic/StopMusic fade times (the Xbox ignored them)
	UBOOL			CueLog;					// one DevSound line per cue request (DevSound is suppressed by default)

	// Not configuration: set by UC2AudioSelfTest before Init() to mix into memory (OpenAL Soft's loopback
	// device, a simulated clock, a fixed listener) instead of opening an output device.
	UBOOL			LoopbackTest;

	// Everything else (OpenAL objects, cooked data, cue instances).
	FUC2AudioState*	State;

	// Constructors.
	UUC2ALAudioSubsystem();
	void StaticConstructor();

	// UObject interface.
	void Destroy();
	void ShutdownAfterError();

	// UAudioSubsystem interface.
	UBOOL Init();
	void SetViewport( UViewport* Viewport );
	UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar=*GLog );
	void Update();
	void PlaySoundCue( AActor* srcActor, FName soundCue, UBOOL bAmbient=0, FLOAT Volume=0.f );
	void StopSoundCue( AActor* srcActor, FName soundCue );
	void StopSoundCues( AActor* srcActor );
	FLOAT GetSoundCueDuration( FName soundCue );
	FLOAT GetSoundCueRadius( FName soundCue );
	FLOAT GetCategoryVolume( EVolumeCategory Category );
	void SetCategoryVolume( EVolumeCategory Category, FLOAT NewVolume );
	INT PlayMusic( FString Song, FLOAT FadeInTime );
	UBOOL StopMusic( INT SongHandle, FLOAT FadeOutTime );
	INT StopAllMusic( FLOAT FadeOutTime );
	void NoteDestroy( AActor* Actor );
	UViewport* GetViewport();
	void CleanUp();
	void NotifyLevelChange();
	void NotifyZoneChange( AZoneInfo* NewZone );
	void NotifyPauseChange( UBOOL bNowPaused );
	class FMovieAudioStream* OpenMovieAudio( INT Rate, INT Channels, UBOOL bExclusive );	// M9
};

/*-----------------------------------------------------------------------------
	UUC2AudioCheckCommandlet: `ucc-bin UC2ALAudio.UC2AudioCheck` resolves every cue of
	every cooked cue table to its waves and decodes each wave (no audio device needed).
-----------------------------------------------------------------------------*/

class UUC2AudioCheckCommandlet : public UCommandlet
{
	DECLARE_CLASS(UUC2AudioCheckCommandlet,UCommandlet,CLASS_Transient,UC2ALAudio)
	void StaticConstructor();
	INT Main( const TCHAR* Parms );
};

/*-----------------------------------------------------------------------------
	UUC2AudioSelfTestCommandlet: `ucc-bin UC2ALAudio.UC2AudioSelfTest` plays known cues
	alone through the device's own code into memory (OpenAL Soft loopback) and compares
	what comes out with what the XACT parameters say: gain (volume, group, headroom,
	category, distance, the Volume parameter), pan direction, pitch, loop counts and the
	variation playlists. Prints the error per case.
-----------------------------------------------------------------------------*/

class UUC2AudioSelfTestCommandlet : public UCommandlet
{
	DECLARE_CLASS(UUC2AudioSelfTestCommandlet,UCommandlet,CLASS_Transient,UC2ALAudio)
	void StaticConstructor();
	INT Main( const TCHAR* Parms );
};

#define AUTO_INITIALIZE_REGISTRANTS_UC2ALAUDIO \
	UUC2ALAudioSubsystem::StaticClass(); \
	UUC2AudioCheckCommandlet::StaticClass(); \
	UUC2AudioSelfTestCommandlet::StaticClass();

#endif // _INC_UC2ALAUDIO

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
