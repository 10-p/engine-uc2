#pragma once

#include <XAct.h>

// FBankInfo
// - maintains references to both the wave bank and sound bank matching a given name
// - stored in UXACTAudioSubsystem
// - filled in and indexed by UXACTAudioSubsytem::GetBankIdx()
struct FBankInfo
{
	FName						BankName;				// name of this bank, is associated with the sound package
    IXACTWaveBank 				*pWaveBank;				// ref to the xact wave bank, may be NULL
	IXACTSoundBank				*pSoundBank;			// ref to the xact sound bank
	BYTE						*pbBankData;			// chunk of memory allocated for the soundbank
	UBOOL						bPersist;				// scion sz - if true, don't remove this bank during level change	
	HANDLE hFile;
};

// FSourceInfo
// - stores a reference to a sound source
// - matches actor with XACTSoundSource
struct FSourceInfo
{
	AActor						*Actor;					// our nifty actor reference
	FVector						LastLocation;
	UBOOL						bValid;					// is this entry valid? (can't just check SoundSource because 2d sources will always have NULL there)
	UBOOL						bIs2D;					// is this a 2d or 3d sound?
	UBOOL						bDoppler;				// should this source set it's velocity?
	UBOOL						bLoopAmbient;			// should the ambient cue play again manually? only true if the xact sound doesn't loop
	BYTE						ActiveCueCnt;			// how many cues are currently playing on this source
	BYTE						ListenerViewportIndex;  // index of closest listener's viewport in UEngine::GEngine->Client->Viewports array
	IXACTSoundSource			*SoundSource;			// ref to the xact sound source
	IXACTSoundCue				*AmbientCue;			// cue instance of any ambients playing on this source
	IXACTSoundCue				*AmbientCuePrep;		// used to prime the ambient cue when bLoopAmbient=true
};

// FCueInstanceInfo
// - matches cue instances with soundbank/source
struct FCueInstanceInfo
{
	IXACTSoundCue				*Cue;					// ref to the xact cue instance
	DWORD						CueIdx;					// ref to the xact cue index, used by ::StopSound()				
	INT							BankIdx;				// ref to the soundbank this cue is played from
	INT							SourceIdx;				// ref to the source this cue is played on
};

// FSoundCueInfo
// - stores the friendly index to a SoundCue, along with the source bank index
struct FSoundCueInfo
{
	INT							CueIdx;
	INT							BankIdx;
};

// FMusicInfo
// stores the two cues needed for playing music
struct FMusicCueInfo
{
	DWORD						CueIdx;					// sound cue index used by play/stop
    IXACTSoundCue				*MusicCue;				// xact cue instance that plays music
	IXACTSoundCue				*MusicCuePrep;			// used to prep the music so there is no pause when it loops	
};

#define MIN_UPDATE_DELTA 	32		// min amount an actor must move before we update it's position for audio

// scion sz - packet size in milliseconds for streaming sounds
#define STREAMING_PACKET_SIZE_MS				510		// assuming 22.05 kHz PCM mono format for sounds (streamed from HD)
#define STREAMING_PACKET_SIZE_MS_PRIME			5		// same as above but for primed buffers used for looping/zero-latency sounds
#define MUSIC_STREAMING_PACKET_SIZE_MS			510		// assuming 44.1 kHz PCM stereo format for music (streamed from DVD)
#define MUSIC_STREAMING_PACKET_SIZE_MS_PRIME	5		// music doesn't really need much priming as the latency is not an issue

class XBOXAUDIO_API UXACTAudioSubsystem : public UAudioSubsystem
{
	DECLARE_CLASS(UXACTAudioSubsystem,UAudioSubsystem,CLASS_Config,XBoxAudio)

	TArray<FSoundCueInfo>		SoundCues;			// list of all registered sound cues
	TMap<FName,INT>				CueMap;				// matches the cue name to an index into SoundCues
	TArray<FBankInfo>			Banks;				// list of wave/sound banks currently loaded
	TArray<FSourceInfo>			Sources;			// list of all current sound sources
	TArray<FCueInstanceInfo>	Cues;				// list of all sound cues currenlty being played
	FBankInfo					MusicBank;
	FMusicCueInfo				MusicCueInfo;

	UBOOL						bInitialized;
	FLOAT						LastTime;
	FLOAT						LastAmbientUpdate;

	UBOOL						bLoopMusic;			// manually loop all music (uses a play and a prime cue)
	UBOOL						bLoopAmbients;		// manually loop ambient sounds (uses a play and a prime cue for each)

	FLOAT						CategoryVolumes[UAudioSubsystem::VOLUME_Max];

	UViewport*					Viewport;

	LPDIRECTSOUND8 				DirectSound8;
	LPDSEFFECTIMAGEDESC			DSPImage;
	IXACTEngine*				XACTEngine;

	/** Array holding sources to be released. We defer relasing them for a bit to avoid having to busy wait for the MCP to finish */
	TArray<IXACTSoundSource*>	SourceReleaseList;

	// max number of static ambients allowed
	DWORD MaxAmbients;

	// transient list of all static ambient actors
	TArray<class AAmbientSound*> StaticAmbientList;

	struct FAmbientPriority
	{
		// index into StaticAmbientList
		INT StaticIdx;
		// current priority
		FLOAT Priority;
	};

	// transient list of all active ambient sounds
	// as indices into StaticAmbientList
	TArray<INT> ActiveAmbientList;

#ifndef FINAL_RELEASE
	// Stats.
	class FXACTStats
	{
	public:

		INT     STATS_FirstEntry,
				STATS_3DSources,
				STATS_2DSources,
				STATS_3DUpdates,
				STATS_UpdateCycles,
				STATS_RegisterCycles,
				STATS_Notifications,
				STATS_Ambients,
				STATS_LastEntry;
		INT		SrcCnt_3D,
				SrcCnt_2D,
				SrcCnt_Ambient;
		FXACTStats();
		void Init();
	} XACTStats;
#endif
	
	UXACTAudioSubsystem();
	void StaticConstructor();

	void Destroy();

	// UAudioSubsystem interface.
	UBOOL Init();
	void SetViewport( UViewport* Viewport );
	UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar=*GLog );
	void NoteDestroy( AActor* Actor );
	UViewport* GetViewport();
	void ProcessNotifications();
	void Update();
	void UpdateAmbientCues();
	//scion jg - Called during level switching to flush all sounds
	void CleanUp();

#ifdef WITH_LIPSINC	
	DOUBLE OffsetInSound( AActor* Actor, USound* Sound );
	UBOOL PauseSound( AActor* Actor, USound* Sound );
	UBOOL ResumeSound( AActor* Actor, USound* Sound );
#endif

	INT PlayMusic( FString Song, FLOAT FadeInTime );
	UBOOL StopMusic( INT SongHandle, FLOAT FadeOutTime );
	INT StopAllMusic( FLOAT FadeOutTime );
	void ProcessMusicNotifications( const XACT_NOTIFICATION & Notify );

	inline FVector MakeRelativeToViewport(const FVector& Location, APlayerController* Listener);
	INT GetBankIdx(FName BankName, UBOOL bPersist=0 );
	INT GetSourceIdx(AActor *Actor, UBOOL bCanCreateSource, UBOOL bIs2D, UBOOL bAmbient = 0);
	INT GetSourceIdx(AActor *Actor);
	INT GetCueIdx(IXACTSoundCue *Cue);
	INT GetNamedCueIdx( const FName& soundCue );
	void RemoveSource(INT SrcIdx);
	IXACTSoundBank* CreateSoundBank(FName BankName,BYTE *&pbBankData);
	// scion sz -
	void RemoveSoundBanks();
	void RemoveSoundCues();

	void NotifyLevelChange();
	void NotifyZoneChange(AZoneInfo *newZone);
	void NotifyPauseChange(UBOOL bNowPaused);

	void PlaySoundCue(AActor *srcActor, FName soundCue, UBOOL bAmbient = 0, FLOAT Volume = 0.f);
	void StopSoundCue(AActor *srcActor, FName soundCue);
	void StopSoundCues(AActor *srcActor);
	void StopAmbientSound(AActor *srcActor);
	INT RegisterSoundCue(FName soundCue);
    FLOAT GetSoundCueDuration(FName soundCue);
	FLOAT GetSoundCueRadius(FName soundCue);
	// scion sz - 
	UBOOL IsSoundCueLooping(FName soundCue);

	FLOAT GetCategoryVolume(EVolumeCategory category);
	void  SetCategoryVolume(EVolumeCategory category, FLOAT newVolume);

	const DSI3DL2LISTENER* GetAudioEffectFromEnum(EZoneAudioEffect newEffect);
};
