
#include "XBoxAudioPrivate.h"

#include "XBUtil.h"

#ifndef FINAL_RELEASE
#include <xactwb.h>
#endif

IMPLEMENT_CLASS(UXACTAudioSubsystem);

//UBOOL GbXACTEnabled = 1;
static UBOOL HandleNotificationRegistration(IXACTEngine *XACTEngine, UBOOL bRegister, DWORD soundCueIdx, IXACTSoundBank *pSoundBank, XACT_NOTIFICATION_TYPE notification);

//jg -- Dirty disc routine
void ShowDirtyDiscError(void);

/**
 * Closes the specified file handle after canceling any pending i/o and
 * waiting for it to be complete.
 *
 * @param hFile the file to close
 */
void CloseFileHandle(HANDLE hFile)
{
	guard(CloseFileHandle);
	// This will only work if XACT queued requests from this thread
	if (CancelIo(hFile) == FALSE)
	{
		debugf(TEXT("XACT.CloseFileHandle.CancelIo() failed with 0x%X"),
			GetLastError());
	}
	OVERLAPPED Overlapped;
	ZeroMemory(&Overlapped,sizeof(OVERLAPPED));
	DWORD dwIgnored;
	// Now wait for this handle to be done with any asynch i/o
	if (GetOverlappedResult(hFile,&Overlapped,&dwIgnored,TRUE) == FALSE)
	{
		debugf(TEXT("XACT.CloseFileHandle.GetOverlappedResult() failed with 0x%X"),
			GetLastError());
	}
	// Now close the handle
	if (CloseHandle(hFile) == FALSE)
	{
		debugf(TEXT("XACT.CloseFileHandle.CloseHandle() failed with 0x%X"),
			GetLastError());
		check(0);
	}
	unguard;
}

#ifndef FINAL_RELEASE //jg -- This code is a hack to find out if we are going to clobber memory or not
/**
 * This code checks the wave bank header to see if it is properly set up to
 * stream from the specified volume
 *
 * @param FileName the name of the wavebank to inspect
 * @param hStreamingWaveBank the handle to the wave bank in quesion
 */
void ValidateStreamingSettings(const FString& FileName,
	HANDLE hStreamingWaveBank)
{
	guard(ValidateStreamingSettings);
	// This buffer is 2K because you can only do aligned reads with
	// overlapped i/o
	BYTE Buffer[2048];
	DWORD NumRead = 0;
	OVERLAPPED Overlapped;
	WAVEBANKHEADER* WaveBankHeader;
	WAVEBANKDATA* WaveBankData;
	// Zero the overlapped
	appMemzero(&Overlapped,sizeof(OVERLAPPED));
	// Check to see if this bank was meant to stream off of the
	// specified volume
	ReadFile(hStreamingWaveBank,Buffer,2048,&NumRead,&Overlapped);
	// Wait for the read to happen
	if (GetOverlappedResult(hStreamingWaveBank,&Overlapped,&NumRead,TRUE))
	{
		// Cast the buffer directly to a header
		WaveBankHeader = (WAVEBANKHEADER*)Buffer;
		// Index into the buffer where the wavebank data is
		WaveBankData = (WAVEBANKDATA*)(Buffer +
			WaveBankHeader->Segments[WAVEBANK_SEGIDX_BANKDATA].dwOffset);
		// Now get the pointer for the W
		// Now check for valid drive/optimization settings
		if (FileName[0] == TEXT('D') &&
			WaveBankData->dwAlignment == WAVEBANK_ALIGNMENT_HARDDRIVE)
		{
			debugf(TEXT("Attempt to play HD optimized wave bank (%s) from DVD"),
				*FileName);
			check(0);
		}
		else if (FileName[0] == TEXT('Z') &&
			WaveBankData->dwAlignment == WAVEBANK_ALIGNMENT_DVD)
		{
			debugf(TEXT("Wasting memory by playing a DVD optimized wavebank (%s) off of HD"),
				*FileName);
		}
	}
	else
	{
		debugf(TEXT("Couldn't determine wavebank type for (%s)"),
			*FileName);
	}
	unguard;
}
#endif


UXACTAudioSubsystem::UXACTAudioSubsystem()
{
	// scion sz - obj persists across level loads
	SetFlags( RF_TagPersist );
}

void UXACTAudioSubsystem::StaticConstructor()
{
}

void UXACTAudioSubsystem::Destroy()
{
	guard(UXACTAudioSubsystem::Destroy);
	// if initialized, do some cleaning
	if (bInitialized)
	{
		bInitialized = 0;
		// remove any audio references to this object
		USound::Audio = NULL;
		//TODO: implement cleanup? not sure if it's ever needed on xbox
		debugf(NAME_Exit,TEXT("XACT subsystem shut down"));
	}
	Super::Destroy();
	unguard;
}

extern UBOOL InitSockets( FString& Error );

#ifdef _DEBUG
extern "C" DWORD g_dwXactDebugLevel, g_dwXactDebugBreakLevel;
extern "C" PFNXACTDEBUGOUTPUTCALLBACK g_pXACTEngineDebugOutputCallback;
extern "C" DWORD g_dwDirectSoundDebugLevel;
#endif

// scion sz - for routing xact debug printfs
VOID CALLBACK DebugPrint(DWORD dwLevel, PCSTR pszString)
{
	debugf( NAME_DevSound, TEXT("XACTDebug: %s"), ANSI_TO_TCHAR(pszString) );	
}

UBOOL UXACTAudioSubsystem::Init()
{
	guard(UXACTAudioSubsystem::Init);
	// if not already initialized
	if (!bInitialized)
	{
#ifdef _DEBUG
		g_dwXactDebugLevel = 2;
		g_dwXactDebugBreakLevel = 0;
		g_dwDirectSoundDebugLevel = 1; //errors only
		g_pXACTEngineDebugOutputCallback = DebugPrint;
#endif
		HRESULT hr;
		// create the DirectSound device
		extern IDirectSound8* GDirectSound;
		check(GDirectSound && TEXT("ERROR: Failed to create direct sound device"));
		DirectSound8 = GDirectSound;
		// Initialize the XACT runtime parameters
		XACT_RUNTIME_PARAMETERS xrParams = {0};
		xrParams.dwMax2DHwVoices         = 188; // Maximum number of 2D hardware voices/streams XACT engine will use
		xrParams.dwMax3DHwVoices         = 64;  // Maximum number of 3D hardware voices/streams XACT engine will use, between 0 and 64
		xrParams.dwMaxConcurrentStreams  = 96;  // Maximum number of 2D voices XACT may use for streaming from the hard drive/DVD
		xrParams.dwMaxNotifications      = 384;   // Maximum number of notifications, 0 = default
		// Create the XACT runtime engine
		if (FAILED(hr = XACTEngineCreate(&xrParams,&XACTEngine)))
		{
			debugf(NAME_Init,TEXT("ERROR: Failed to create XACT engine"));
			return hr;
		}
		// load the DirectSound effects image
		DSEFFECTIMAGELOC dsImageLoc;
		dsImageLoc.dwI3DL2ReverbIndex = GraphI3DL2_I3DL2Reverb;
		dsImageLoc.dwCrosstalkIndex = GraphXTalk_XTalk;
		//jg -- Handle path remapping for demos
		FString FileName = GFileManager->ResolvePath(TEXT("..\\xbox.bin"));
		hr = XAudioDownloadEffectsImage(TCHAR_TO_ANSI(*FileName), &dsImageLoc, XAUDIO_DOWNLOADFX_EXTERNFILE, &DSPImage);
		if (FAILED(hr))
		{
			debugf(NAME_Init,TEXT("ERROR: Failed to load effects image"));
			//jg -- Show the dirty/damaged disc error
			ShowDirtyDiscError();
			return 0;
		}
		// 2 units == 1" == 2.54 cm  -- needed for doppler effect & HRTF
		DirectSound8->SetDistanceFactor(0.0254f/2.f,DS3D_IMMEDIATE);
		// Full vs light is ~ 70kb.
		DirectSoundUseFullHRTF();
		// Initialize variables
		LastAmbientUpdate = -1.f;
		bInitialized	= 1;
		USound::Audio = this;

		// set all the default volumes
		UPlayerProfile* DefaultProfile = UPlayerProfile::StaticClass()->GetDefaultObject<UPlayerProfile>();
		SetCategoryVolume(VOLUME_Music, DefaultProfile->MusicVolume);
		SetCategoryVolume(VOLUME_FX, DefaultProfile->SoundFXVolume);
		SetCategoryVolume(VOLUME_Voice, DefaultProfile->VoiceVolume);

		debugf(NAME_Init,TEXT("XACT subsystem initialized"));

#ifndef FINAL_RELEASE
		XACTStats.Init();
#endif

		// register common wave/soundbanks
		GetBankIdx(FName(TEXT("A_Character")), 1);
		GetBankIdx(FName(TEXT("A_Combo")), 1);
		GetBankIdx(FName(TEXT("A_Gameplay")), 1);
		GetBankIdx(FName(TEXT("A_Pickup")), 1);
		GetBankIdx(FName(TEXT("A_Weapon")), 1);
		GetBankIdx(FName(TEXT("A_WeaponMelee")), 1);
		GetBankIdx(FName(TEXT("A_WeaponRace")), 1);
		GetBankIdx(FName(TEXT("A_Announcer")), 1);
		GetBankIdx(FName(TEXT("A_Announcer_MK")), 1);
		GetBankIdx(FName(TEXT("A_Ann_Female")), 1);
		GetBankIdx(FName(TEXT("A_TauntAnubis")), 1);
		GetBankIdx(FName(TEXT("A_TauntApophis")), 1);
		GetBankIdx(FName(TEXT("A_TauntArclite")), 1);
		GetBankIdx(FName(TEXT("A_TauntBrock")), 1);
		GetBankIdx(FName(TEXT("A_TauntLauren")), 1);
		GetBankIdx(FName(TEXT("A_TauntMalcolm")), 1);
		GetBankIdx(FName(TEXT("A_TauntProto")), 1);
		GetBankIdx(FName(TEXT("A_TauntRaiden")), 1);
		GetBankIdx(FName(TEXT("A_TauntRaptor")), 1);
		GetBankIdx(FName(TEXT("A_TauntSapphire")), 1);
		GetBankIdx(FName(TEXT("A_TauntSelket")), 1);
		GetBankIdx(FName(TEXT("A_TauntSobek")), 1);
		GetBankIdx(FName(TEXT("A_TauntSzalor")), 1);

		// scion sz - init music cue info
		MusicCueInfo.MusicCue = NULL;
		MusicCueInfo.MusicCuePrep = NULL;
		MusicCueInfo.CueIdx = INDEX_NONE;

		// scion sz - manually loop music
		// not need to loop music manually since
		// each music entry exists in its own wavebank file
		bLoopMusic = 0;
		// scion sz - manually loop ambients
		// this is done since we don't want XACT to create extra buffers 
		// for wavebank entries that we never use
		bLoopAmbients = 0;
	}
	return 1;
	unguard;
}

/* scion ===============================================
* ::NotifyLevelChange
* Author: rayd
*
* Used to clear out all our currently playing sounds.
*
* =====================================================
*/
void UXACTAudioSubsystem::NotifyLevelChange()
{
	if (XACTEngine != NULL)
	{
		debugf(NAME_DevSound,TEXT("Notified of level change, releasing sources"));
		LastAmbientUpdate = -1.f;
		// empty the static ambient list
		StaticAmbientList.Empty();
		ActiveAmbientList.Empty();
		// stop any music tracks
		StopAllMusic(0.f);
		// clear out all sources
		for (INT i = 0; i < Sources.Num(); i++)
		{
			// if the sound source is valid, stop any sounds and release it
			if (Sources(i).SoundSource != NULL)
			{
				// stop any cues playing on this source
				Sources(i).SoundSource->StopSoundCues();
				// nuke this source
				Sources(i).SoundSource->Release();
			}
			// stop ambient cue, if any
			if (Sources(i).AmbientCue != NULL)
			{
				INT cueIdx = GetCueIdx(Sources(i).AmbientCue);
				if (cueIdx >= 0 && cueIdx < Cues.Num())
				{
					// grab the bank this cue is playing in
					INT bankIdx = Cues(cueIdx).BankIdx;
					// stop the actual cue
					Banks(bankIdx).pSoundBank->Stop(XACT_SOUNDCUE_INDEX_UNUSED,0,Sources(i).AmbientCue);
					// scionsz - stop the prep cue if it exists
					if( Sources(i).AmbientCuePrep && 
						Sources(i).AmbientCuePrep != Sources(i).AmbientCue)
					{
						Banks(bankIdx).pSoundBank->Stop(XACT_SOUNDCUE_INDEX_UNUSED,0,Sources(i).AmbientCuePrep);
					}
				}
			}
		}
		Sources.Empty();
		// empty our cue list
		Cues.Empty();
		// tick the xact engine so that it can clean up
		XACTEngineDoWork();
		// and process any notifications that pop up
		ProcessNotifications();
		// scion sz - remove all sound cue entries
		RemoveSoundCues();
		// scion sz - remove any sound banks that were loaded
		RemoveSoundBanks();
		// clear Viewport so we don't try to play any more sounds until we are reinitialized
		Viewport = NULL;

#ifndef FINAL_RELEASE
		XACTStats.SrcCnt_2D = 0;
		XACTStats.SrcCnt_3D = 0;
		XACTStats.SrcCnt_Ambient = 0;
#endif
	}
}

/* epic ===============================================
* ::NotifyPauseChange
* Author: moelfke
*
* Called when the game becomes paused/unpaused. Tells
* XACT to pause/unpause all active cues (except the
* level music)
*
* =====================================================
*/
void UXACTAudioSubsystem::NotifyPauseChange(UBOOL bNowPaused)
{
	guard(UXACTAudioSubsystem::NotifyPauseChange);

	for (INT i = 0; i < Cues.Num(); i++)
	{
		if (Cues(i).Cue != NULL && Banks(Cues(i).BankIdx).pSoundBank != NULL)
		{
			Banks(Cues(i).BankIdx).pSoundBank->PauseSoundCue(Cues(i).Cue, bNowPaused);
		}
	}

	unguard;
}

/* epic ===============================================
* ::MakeRelativeToViewport
* Author: moelfke
*
* Helper function for splitscreen sound. Since we can
* only have one 'real' listener (the primary viewport in
* our Viewport variable), this function transforms the
* given sound source Location vector to be positioned
* relative to the 'real' listener exactly as it
* currently is to the Listener passed in.
*
* =====================================================
*/
inline FVector UXACTAudioSubsystem::MakeRelativeToViewport(const FVector& Location, APlayerController* Listener)
{
	guardSlow(UXACTAudioSubsystem::MakeRelativeToViewport);

	// get the world space delta between the sound source and the viewer
	FVector Delta = (Location - Listener->ViewTarget->Location);
	// transform Delta to the viewer's local space
	FVector ViewActorLocal = Delta.TransformVectorBy(GMath.UnitCoords / Listener->Rotation);
	// transform again to a world space delta to the 'real' listener
	FVector ListenerWorld = ViewActorLocal.TransformVectorBy(GMath.UnitCoords * Viewport->Actor->Rotation);
	// add 'real' listener's location to get the final position to use
	FVector FakePosition = ListenerWorld + Viewport->Actor->ViewTarget->Location;

	return FakePosition;

	unguardSlow;
}

/* scion ===============================================
* ::GetSourceIdx
* Author: jg
*
* Searches for a matching sound source for the given
* Actor.
* Returns -1 if unable to find a sound source.
*
* =====================================================
*/
INT UXACTAudioSubsystem::GetSourceIdx(AActor *Actor)
{
	guard(UXACTAudioSubsystem::GetSourceIdx);
	INT nFound = -1;
	if (Actor != NULL)
	{
		// first search through our current list
		for (INT Idx = 0; Idx < Sources.Num() && nFound == -1; Idx++)
		{
			if (Sources(Idx).Actor == Actor)
			{
				// found a match, return the index
				nFound = Idx;
			}
		}
	}
	return nFound;
	unguard;
}

/* scion ===============================================
* ::GetSourceIdx
* Author: rayd
*
* Searches for a matching sound source for the given
* Actor, optionally creating one if it isn't found.
* Returns -1 if unable to find/create a sound source.
*
* =====================================================
*/
INT UXACTAudioSubsystem::GetSourceIdx(AActor *Actor, UBOOL bCanCreateSource, UBOOL bIs2D, UBOOL bAmbient)
{
	guard(UXACTAudioSubsystem::GetSourceIdx);
	INT Idx = -1;
	if (Actor != NULL)
	{
		// first search through our current list
		for (Idx = 0; Idx < Sources.Num(); Idx++)
		{
			if ( Sources(Idx).Actor == Actor &&
				((bAmbient && !bCanCreateSource) ? (Sources(Idx).AmbientCue != NULL) : (Sources(Idx).bIs2D == bIs2D)) )
			{
				// found a match, return the index
				return Idx;
			}
		}
		Idx = -1;
		// create a new source if we can
		if (bCanCreateSource)
		{
#ifndef FINAL_RELEASE
			if( bIs2D ) {
				XACTStats.SrcCnt_2D++;
			}
#endif
			// attempt the create the source
			IXACTSoundSource* Src = NULL;
			if (!bIs2D)
			{
				// only create a sound source if it's a 3d sound
				HRESULT hr = XACTEngine->CreateSoundSource(XACT_FLAG_SOUNDSOURCE_3D,&Src);
				if (SUCCEEDED(hr))
				{
					//debugf(NAME_DevSound,TEXT("Created 3D sound source for actor %s"),Actor->GetName());
					Src->SetPosition(Actor->Location.X, Actor->Location.Y, Actor->Location.Z, DS3D_DEFERRED);
					Src->SetVelocity(0.f,0.f,0.f,DS3D_DEFERRED);
#ifndef FINAL_RELEASE
					XACTStats.SrcCnt_3D++; 
#endif
				}
				else
				{
					// this is bad
					debugf(NAME_DevSound,TEXT("Failed to create new 3d sound source!"));
					return -1;
				}
			}
			// look for an empty entry in the list first
			for (Idx = 0; Idx < Sources.Num(); Idx++)
			{
				if (Sources(Idx).Actor == NULL &&
					Sources(Idx).SoundSource == NULL)
				{
					// use this entry
					break;
				}
			}

			//debugf(NAME_DevSound,TEXT("Creating new sound source for %s at index(%d), 2d? %s"),Actor!=NULL?Actor->GetName():TEXT("NULL"),Idx,bIs2D?TEXT("true"):TEXT("false"));
			// if we didn't find a valid entry, make a new one
			if (Idx >= Sources.Num())
			{
				Idx = Sources.Num();
				Sources.AddZeroed();
			}
			Sources(Idx).Actor = Actor;
			Sources(Idx).SoundSource = Src;
			Sources(Idx).bIs2D = bIs2D;
			Sources(Idx).bLoopAmbient = Actor->bLoopAmbientSound;
			Sources(Idx).ActiveCueCnt = 0;
			Sources(Idx).AmbientCue = NULL;
			Sources(Idx).AmbientCuePrep = NULL;
			Sources(Idx).LastLocation = Actor->Location;
			// check to see if this is a doppler source
			//TEMP: only allow projectiles to doppler
			//TEMP: don't allow anything to doppler :)
			Sources(Idx).bDoppler = 0; //(bIs2D || !Actor->IsA(AProjectile::StaticClass())) ? 0 : 1;
			Sources(Idx).bValid = true;
		}
	}
	// return our index
	return Idx;
	unguard;
}

/* scion ===============================================
* ::GetBankIdx
* Author: rayd
*
* Returns the index to Banks[] of the given WaveBank/SoundBank,
* attempting to load it if it doesn't exist.  Returns -1
* if no entry was found, and we failed to load the actual
* wave bank and/or sound bank.
*
* NOTE: It is assumed that all WaveBank/SoundBanks will
* match in name to the sound's package.
*
* bPersist - If true, prevents the sound bank and its 
* wavebank from being removed during level change.
*
* =====================================================
*/
INT UXACTAudioSubsystem::GetBankIdx(FName BankName, UBOOL bPersist )
{
	guard(UXACTAudioSubsystem::GetBankIdx);

	// if it's a valid name,
	if (BankName != NAME_None)
	{
		INT Idx = 0;
		// iterate through our current list looking for a match
		for (Idx = 0; Idx < Banks.Num(); Idx++)
		{
			// if it's a matching name
			if (Banks(Idx).BankName == BankName)
			{
				// return the index
				return Idx;
			}
		}
		// attempt to load the wave bank file
		// first try localized version
		FString FileName = FString::Printf(TEXT("..\\Sounds\\%s_%s.xwb"), *BankName, GetLanguage());
		//jg -- Handle path remapping for demos
		FileName = GFileManager->ResolvePath(*FileName);
		HANDLE hStreamingWaveBank = CreateFile((LPCSTR)TCHAR_TO_ANSI(*FileName),
											   GENERIC_READ,FILE_SHARE_READ,NULL,
											   OPEN_EXISTING,FILE_FLAG_OVERLAPPED|FILE_FLAG_NO_BUFFERING,NULL);
		if (hStreamingWaveBank == INVALID_HANDLE_VALUE)
		{
			debugf(NAME_DevSound, TEXT("Failed to load localized wavebank %s"), *FileName);
			// fallback to generic english version
			FileName = GFileManager->ResolvePath(*FString::Printf(TEXT("..\\Sounds\\%s.xwb"), *BankName));
			hStreamingWaveBank = CreateFile( (LPCSTR)TCHAR_TO_ANSI(*FileName),
													GENERIC_READ, FILE_SHARE_READ, NULL,
													OPEN_EXISTING, FILE_FLAG_OVERLAPPED|FILE_FLAG_NO_BUFFERING, NULL );
		}
		if (hStreamingWaveBank != INVALID_HANDLE_VALUE)
		{
#if LIN_LOAD && !FINAL_RELEASE
			//jg -- Hack function for preventing XACT stompage
			ValidateStreamingSettings(FileName,hStreamingWaveBank);
#endif
			// Initialize the streaming wave bank parameters
			XACT_WAVEBANK_STREAMING_PARAMETERS wbParams = {0};
			wbParams.hFile                        = hStreamingWaveBank;
			wbParams.dwOffset                     = 0;
			wbParams.dwPacketSizeInMilliSecs      = STREAMING_PACKET_SIZE_MS;
			wbParams.dwPrimePacketSizeInMilliSecs = STREAMING_PACKET_SIZE_MS_PRIME;
			IXACTWaveBank *pWaveBank = NULL;
			HRESULT hr;
			if (SUCCEEDED(hr = XACTEngine->RegisterStreamedWaveBank(&wbParams,&pWaveBank)))
			{
				if( bPersist )
				{
					// mark the name as keep so it's not lost on level gc
					BankName.SetFlags(RF_Keep);
				}				
				debugf(NAME_DevSound,TEXT("Registered streaming wavebank %s"), *FileName);
				// loaded the wave bank, now try to load the sound bank
				BYTE* pbBankData = NULL;
				IXACTSoundBank *pSoundBank = CreateSoundBank(BankName,pbBankData);
				if (pSoundBank != NULL)
				{
					INT Idx = Banks.Num();
					Banks.AddZeroed();
					Banks(Idx).BankName = BankName;
					Banks(Idx).pSoundBank = pSoundBank;
					Banks(Idx).pWaveBank = pWaveBank;
					Banks(Idx).pbBankData = pbBankData;
					Banks(Idx).bPersist = bPersist;
					Banks(Idx).hFile = hStreamingWaveBank;
					// return the idx
					return Idx;
				}
			}
			else
			{
				//jg -- Use the common safer routine
				CloseFileHandle(hStreamingWaveBank);
				// couldn't register the wavebank
				debugf(NAME_Error,TEXT("Failed to register wavebank %s"),*BankName);
			}
		}
		else
		{
			// couldn't load the actual wavebank file
			debugf(NAME_Error,TEXT("Failed to load wavebank %s"),FileName);
			//jg -- Show the dirty/damaged disc error
			ShowDirtyDiscError();
		}
	}
	// failed to find an entry, and failed to create a new one, return an invalid index
	return -1;
	unguard;
}

/* scion ===============================================
* ::CreateSoundBank
* Author: rayd
*
* Creates a soundbank, loading the matching .xsb.
*
* =====================================================
*/
IXACTSoundBank* UXACTAudioSubsystem::CreateSoundBank(FName BankName,BYTE *&pbBankData)
{
	guard(UXACTAudioSubsystem::CreateSoundBank);
	debugf( NAME_DevSound, TEXT("CreateSoundBank(%s)"), *BankName );
	// load the actual sound bank into a byte array
	IXACTSoundBank *pSoundBank = NULL;
	DWORD dwFileSize  = 0;
	FString FileName = FString::Printf(TEXT("..\\Sounds\\%s.xsb"),*BankName);
	//jg -- Handle path remapping for demos
	FileName = GFileManager->ResolvePath(*FileName);
	HRESULT hr;
	if (SUCCEEDED(hr = XBUtil_LoadFile((LPCSTR)TCHAR_TO_ANSI(*FileName),(VOID **)&pbBankData,&dwFileSize)))
	{
		// and now attempt to register the sound bank with xact
		if (FAILED(hr = XACTEngine->CreateSoundBank(pbBankData,dwFileSize,&pSoundBank)))
		{
			debugf(NAME_Error,TEXT("Failed to create soundbank %s"),*BankName);
			// free up the sound bank buffer
			free(pbBankData);
			pbBankData = NULL;
		}
	}
	else
	{
		// couldn't find the file apparently
		debugf(NAME_Error,TEXT("Failed to load soundbank %s [%s]"),*BankName,FileName);
		//jg -- Show the dirty/damaged disc error
		ShowDirtyDiscError();
	}
	return pSoundBank;
	unguard;
}

/* scion ===============================================
* ::RemoveSoundBanks
* Author: sz
*
* Remove any soundbanks not marked to persist.
* Also unregister its corresponding wavebank.
*
* =====================================================
*/
void UXACTAudioSubsystem::RemoveSoundBanks()
{
	guard(UXACTAudioSubsystem::RemoveSoundBanks);
	check(XACTEngine);
	check( Cues.Num()==0 && TEXT("remove cues first") );

	// list of banks to keep
	TArray<FBankInfo> ValidBanks;
	// iterate through all banks
	for( INT BankIdx=0; BankIdx < Banks.Num(); BankIdx++ )
	{
        FBankInfo& BankInfo = Banks(BankIdx);
		if( BankInfo.bPersist )
		{
            ValidBanks.AddItem( BankInfo );
		}
		// remove any banks not marked to persist
		else
		{
			debugf( TEXT("Removing sound bank [%s]"), *BankInfo.BankName );
			if( BankInfo.pWaveBank )
			{
				HRESULT hr;
				if( FAILED(hr = XACTEngine->UnRegisterWaveBank( BankInfo.pWaveBank )) )
				{
					debugf( NAME_Warning,
						TEXT("Failed to unregister wavebank for soundbank=%s"), 
						*BankInfo.BankName );
				}
			}
			if( BankInfo.pSoundBank )
			{
				BankInfo.pSoundBank->Release();
				BankInfo.pSoundBank = NULL;
			}
            if( BankInfo.pbBankData )
			{
				//delete[] BankInfo.pbBankData;
				free( BankInfo.pbBankData );
				BankInfo.pbBankData = NULL;
			}					
			if (BankInfo.hFile != INVALID_HANDLE_VALUE)
			{
				//jg -- Use the common safer routine
				CloseFileHandle(BankInfo.hFile);
			}
		}
	}
	// restore valid banks
	Banks.Empty( ValidBanks.Num() );
	Banks.Add( ValidBanks.Num() );
	for( INT Idx=0; Idx < ValidBanks.Num(); Idx++ )
	{
        Banks(Idx) = ValidBanks(Idx);
	}

	unguard;
}

/* scion ===============================================
* ::RemoveSoundCues
* Author: sz
*
* Unregister notifications for all sound cues.
* Remove all the sound cue entries and the cue map entries.
*
* =====================================================
*/
void UXACTAudioSubsystem::RemoveSoundCues()
{
	guard(UXACTAudioSubsystem::RemoveSoundCues);

	// unregister all the stop notifications
	for( INT Idx=0; Idx < SoundCues.Num(); Idx++ )
	{
        FSoundCueInfo& SoundCueInfo = SoundCues(Idx);
		HandleNotificationRegistration(
			XACTEngine,
			0,
			SoundCueInfo.CueIdx,
			Banks(SoundCueInfo.BankIdx).pSoundBank,
			eXACTNotification_Stop
			);
	}
    // remove the soundcues
	SoundCues.Empty();
	// remove the FName to cue index mappings
	CueMap.Empty();

	unguard;
}

/* scion ===============================================
* ::GetCueIdx
* Author: rayd
*
* Tries to match a cue instance to one in the Cues[]
* list. Returns -1 if no entry was found.
*
* =====================================================
*/
INT UXACTAudioSubsystem::GetCueIdx(IXACTSoundCue *Cue)
{
	guard(UXACTAudioSubsystem::GetCueIdx);
	INT Idx = -1;
	// iterate through all our current cues
	for (Idx = 0; Idx < Cues.Num(); Idx++)
	{
		if (Cues(Idx).Cue == Cue)
		{
			return Idx;
		}
	}
	// invalid cue, not in our list (shouldn't ever happen)
	Idx = -1;
	return Idx;
	unguard;
}
									
/* epic ===============================================
* ::GetNamedCueIdx
* Author: sz
*
* Get the sound cue info associated with the soundCue
* name. If the soundCue has not already been registered
* then register it.
*
* =====================================================
*/
INT UXACTAudioSubsystem::GetNamedCueIdx( const FName& soundCue )
{
	guard(UXACTAudioSubsystem::GetNamedCueIdx);
	// first get the index for the sound
	INT cueInfoIdx = INDEX_NONE;
	// first see if this soundCue already is registered
	INT *Idx = CueMap.Find(soundCue);
	if( !Idx )
	{
		FString bankName;
		FString cueName;
		FString srcString = *soundCue;

		// there has to be at least one "."
		// to the left of it is the bankName
		if( srcString.Split( FString(TEXT(".")), &bankName, NULL ) )
		{
			// if there are any more "." then only use the right portion
			// as the cue name. This skips all group names.
			srcString.Split( FString(TEXT(".")), NULL, &cueName, 1 );
			// create the proper bankName.cueName name
			FString RealCueStr = FString::Printf(TEXT("%s.%s"), *bankName, *cueName );
			FName RealCueName( *RealCueStr );
			// try to find this again
			Idx = CueMap.Find( RealCueName );
			// only register the sound if it doesn't exist
			if( !Idx )
			{
				// no entry yet, so register this sound
				cueInfoIdx = RegisterSoundCue(RealCueName);
				// if we were able to register the sound,
				if (cueInfoIdx != -1)
				{
					// then add it to the map for future lookups
					CueMap.Add(RealCueName,cueInfoIdx);
				}
			}
		}
	}
	
	if( Idx )
	{
		// set the cue info index to the found value
		cueInfoIdx = *Idx;
	}

    return cueInfoIdx;
	unguard;
}
									
/* epic ===============================================
* ::SetViewport
*
* Called when we receive a new viewport, caches it
* locally.
*
* =====================================================
*/
void UXACTAudioSubsystem::SetViewport(UViewport* InViewport)
{
	Viewport = InViewport;
}

//NOTE: graciously yanked from UC
inline LONG CalcVolume( float Volume )
{
    if (Volume < KINDA_SMALL_NUMBER)
	{
        return DSBVOLUME_MIN;
	}

    Volume = log10(Clamp(Volume, 0.1f, 1.f)); // 0 to -1.f
    return LONG(Volume * 1000.f);
}

#ifndef FINAL_RELEASE
/* epic ===============================================
* ::TestSoundsFor
* Author: moelfke
*
* Debugging function that iterates over all the properties
* of the given UStruct and attempts to play all referenced
* sound cues. For easily finding missing/broken cues.
* All cues tested are added to the AllSoundCues array
* to avoid testing sounds multiple times
*
* NOTE: Assumes that any name property with a period in
* the name is a sound cue reference. A hack, of course;
* ideally we'd have a special data type that tells us that
* it's a sound cue. But we don't and it's too late now.
* Nothing's perfect.
*
* =====================================================
*/
static void TestSoundsFor(UStruct* Struct, BYTE* Data, UXACTAudioSubsystem* Audio, TArray<FName>& AllSoundCues)
{
	// iterate through all the properties
	for (TFieldIterator<UProperty, CLASS_IsAUProperty> It(Struct); It; ++It)
	{
		// for structs, we need to go through all of the properties inside of it
		if (It->IsA(UStructProperty::StaticClass()))
		{
			TestSoundsFor(((UStructProperty*)*It)->Struct, Data + It->Offset, Audio, AllSoundCues);
		}
		// for arrays, iterate through all the elements
		else if (It->IsA(UArrayProperty::StaticClass()))
		{
			UArrayProperty* ArrayProp = (UArrayProperty*)*It;
			FArray* Array = (FArray*)(Data + It->Offset);
			if (ArrayProp->Inner->IsA(UNameProperty::StaticClass()))
			{
				for (INT i = 0; i < Array->Num(); i++)
				{
					// get the name stored at this element in the array
					FName Name = *((FName*)Array->GetData() + i);
					// if it's a valid name containing a period
					if (Name != NAME_None && appStrstr(*Name, TEXT(".")))
					{
						// it must be a sound cue reference, try to play it
						if (AllSoundCues.FindItemIndex(Name) == INDEX_NONE)
						{
							Audio->PlaySoundCue(Audio->Viewport->Actor, Name, 0);
							Audio->StopSoundCue(Audio->Viewport->Actor, Name);
							AllSoundCues.AddItem(Name);
						}
					}
				}
			}
			// for structs, we need to go through all of the properties inside of it
			else if (ArrayProp->Inner->IsA(UStructProperty::StaticClass()))
			{
				for (INT i = 0; i < Array->Num(); i++)
				{
					TestSoundsFor(((UStructProperty*)ArrayProp->Inner)->Struct, (BYTE*)Array->GetData() + i * ArrayProp->Inner->ElementSize, Audio, AllSoundCues);
				}
			}
		}
		else if (It->IsA(UNameProperty::StaticClass()))
		{
			for (INT i = 0; i < It->ArrayDim; i++)
			{
				// get the name stored in this property
				FName Name = *((FName*)(Data + It->Offset + i * It->ElementSize));
				// if it's a valid name containing a period
				if (Name != NAME_None && appStrstr(*Name, TEXT(".")))
				{
					// it must be a sound cue reference, try to play it
					if (AllSoundCues.FindItemIndex(Name) == INDEX_NONE)
					{
						Audio->PlaySoundCue(Audio->Viewport->Actor, Name, 0);
						Audio->StopSoundCue(Audio->Viewport->Actor, Name);
						AllSoundCues.AddItem(Name);
					}
				}
			}
		}
	}
}

// function to sort names, used when listing all sound cues (see "XACT TESTALLSOUNDCUES" exec below)
static FORCEINLINE int Compare(FName A, FName B)
{
	return appStrcmp(*A, *B);
}

#endif

/* epic ===============================================
* ::Exec
*
* Expose some useful debugging commands:
*
* "XACT" +
" - "MEM"			dump xact memory usage
* - "SOURCES"		list all the current 2d & 3d sources
* - "MUSIC"			set the new music track
* - "FILTER"		applies a reverb filter for testing
* - "TESTALLSOUNDS"	test plays all sound cue references
*					in all loaded classes
*
* =====================================================
*/
UBOOL UXACTAudioSubsystem::Exec(const TCHAR* Cmd, FOutputDevice& Ar)
{
	guard(UXACTAudioSubsystem::Exec);
#ifndef FINAL_RELEASE
	if (ParseCommand(&Cmd,TEXT("XACT")))
	{
		if (ParseCommand(&Cmd,TEXT("MEM")))
		{
			// dump xact mem usage
			if (XACTEngine != NULL)
			{
				XACT_REALTIME_AUDIO_DATA AudioData;
				if (XACTEngine->GetRealtimeData(&AudioData) == DS_OK)
				{
					debugf(TEXT("XACT memory usage: %2.3fmb"),AudioData.dwXactMemoryUsage/(1024.f*1024.f));
					DWORD memCnt = 0;
					memCnt += sizeof(FBankInfo) * Banks.Num();
					for (INT i = 0; i < Banks.Num(); i++)
					{
						memCnt += sizeof(*Banks(i).pbBankData);
					}
					memCnt += sizeof(FSourceInfo) * Sources.Num();
					memCnt += sizeof(FCueInstanceInfo) * Cues.Num();
					memCnt += sizeof(FSoundCueInfo) * SoundCues.Num();
					memCnt += sizeof(UXACTAudioSubsystem);
					debugf(TEXT("~ internal usage: %2.1fkb [%d sources, %d banks, %d cues, %d active cues]"),
						   memCnt/1024.f,
						   Sources.Num(),
						   Banks.Num(),
						   SoundCues.Num(),
						   Cues.Num());
				}
			}
			return 1;
		}
		else
		if (ParseCommand(&Cmd,TEXT("SOURCES")))
		{
			// dump current sources
			debugf(TEXT("2D Sources:"));
			INT Cnt = 0;
			for (INT i = 0; i < Sources.Num(); i++)
			{
				if (Sources(i).bIs2D)
				{
					Cnt++;
					debugf(TEXT("%d - Actor: %s, Cues: %d%s"),
						   Cnt,
						   Sources(i).Actor!=NULL?Sources(i).Actor->GetName():TEXT("NULL"),
						   Sources(i).ActiveCueCnt,
						   Sources(i).AmbientCue != NULL?TEXT("Ambient"):TEXT(""));
				}
			}
			debugf(TEXT("3D Sources:"));
			Cnt = 0;
			for (INT i = 0; i < Sources.Num(); i++)
			{
				if (!Sources(i).bIs2D)
				{
					Cnt++;
					debugf(TEXT("%d - Actor: %s, Cues: %d%s"),
						   Cnt,
						   Sources(i).Actor!=NULL?Sources(i).Actor->GetName():TEXT("NULL"),
						   Sources(i).ActiveCueCnt,
						   Sources(i).AmbientCue != NULL?TEXT("Ambient"):TEXT(""));
				}
			}
			return 1;
		}
		else
		if (ParseCommand(&Cmd,TEXT("MUSIC")))
		{
			FString SongName;
			if (Parse(Cmd,TEXT("SONG="),SongName))
			{
				debugf(TEXT("Playing song %s"),*SongName);
				PlayMusic(SongName,0.f);
			}
			else
			{
				debugf(TEXT("Usage: XACT MUSIC SONG=<songname>"));
			}
			return 1;
		}
		else
		if (ParseCommand(&Cmd,TEXT("FILTER")))
		{
			//test of reverb
			if (XACTEngine->SetI3dl2Listener(&DirectSoundI3DL2ListenerPreset_Underwater,DS3D_IMMEDIATE) != DS_OK)
			{
				debugf(NAME_Init,TEXT("Failed to apply listener parameters"));
			}
			else
			{
				debugf(NAME_Init,TEXT("Applied I3dl2 filter (underwater)"));
			}
			return 1;
		}
		// scion sz - for testing sounds
		else if( ParseCommand(&Cmd,TEXT("SOUNDTEST")) )
		{
            FString SoundStr = ParseToken( Cmd , 0 );
			if( SoundStr.InStr(TEXT(".")) == INDEX_NONE )
			{
				debugf( TEXT("Usage: XACT SOUNDTEST BankName.CueName") );
			}
			else
			{
				UBOOL bLoop =0;
				Parse(Cmd,TEXT("LOOP="),bLoop);
				Viewport->Actor->bLoopAmbientSound = bLoop;
				debugf( TEXT("SOUNDTEST [%s] loop=%d"), SoundStr, bLoop );
                PlaySoundCue( Viewport->Actor, FName(*SoundStr), 1 );
			}
			return 1;
		}
		// scion sz - disable playing of sounds
		else if( ParseCommand(&Cmd,TEXT("DISABLE")) )
		{
			// cleanup
			NotifyLevelChange();
			// disable further updates
			//GbXACTEnabled = 0;
			return 1;
		}
		else if( ParseCommand(&Cmd,TEXT("ENABLE")) )
		{
			// re-enable
			//GbXACTEnabled = 1;
			return 1;
		}
		//epic moelfke: test all sound cues being referenced
		else if (ParseCommand(&Cmd, TEXT("TESTALLSOUNDS")))
		{
			if (Viewport && Viewport->Actor && Viewport->Actor->GetLevel())
			{
				UGameEngine* GameEngine = Cast<UGameEngine>(Viewport->Actor->GetLevel()->Engine);
				if (GameEngine)
				{
					// load everything we have
					for (INT i = 0; i < GameEngine->ServerPackages.Num(); i++)
					{
						debugf(TEXT("Loading %s..."), *GameEngine->ServerPackages(i));
						LoadPackage(NULL, *GameEngine->ServerPackages(i), LOAD_NoWarn);
					}
					// test all sound references in the game
					debugf(TEXT("Testing all sounds..."));
					TArray<FName> AllSoundCues;
					for (TObjectIterator<UClass> It; It; ++It)
					{
						TestSoundsFor(*It, (BYTE*)It->GetDefaultsSafe(0), this, AllSoundCues);
		}
					debugf(TEXT("Testing complete."));
					if (ParseCommand(&Cmd, TEXT("LIST")))
					{
						// now list everything we tested
						debugf(TEXT("List of tested cues:"));
						// sort them all alphabetically first
						Sort<FName>(&AllSoundCues(0), AllSoundCues.Num());
						for (INT i = 0; i < AllSoundCues.Num(); i++)
						{
							debugf(*AllSoundCues(i));
						}
					}
					// make sure all sounds that were played get cleaned up
					NotifyLevelChange();
					// do garbage collection to unload everything we don't need anymore
					CollectGarbage(RF_Native);

					return 1;
				}
	}
		}
	}
#endif
	return 0;
	unguard;
}

/* epic ===============================================
* ::GetAudioEffectFromEnum
*
* Returns the static listener preset that matches the
* supplied enum.
*
* =====================================================
*/
const DSI3DL2LISTENER* UXACTAudioSubsystem::GetAudioEffectFromEnum(EZoneAudioEffect newEffect)
{
	const DSI3DL2LISTENER* effect = NULL;
	switch (newEffect)
	{
	case EZA_Default:
		effect = &DirectSoundI3DL2ListenerPreset_Default;
		break;
	case EZA_Default2:
		effect = &DirectSoundI3DL2ListenerPreset_Default2;
		break;
	case EZA_Generic:
		effect = &DirectSoundI3DL2ListenerPreset_Generic;
		break;
	case EZA_PaddedCell:
		effect = &DirectSoundI3DL2ListenerPreset_PaddedCell;
		break;
	case EZA_Room:
		effect = &DirectSoundI3DL2ListenerPreset_Room;
		break;
	case EZA_Bathroom:
		effect = &DirectSoundI3DL2ListenerPreset_Bathroom;
		break;
	case EZA_LivingRoom:
		effect = &DirectSoundI3DL2ListenerPreset_LivingRoom;
		break;
	case EZA_StoneRoom:
		effect = &DirectSoundI3DL2ListenerPreset_StoneRoom;
		break;
	case EZA_Auditorium:
		effect = &DirectSoundI3DL2ListenerPreset_Auditorium;
		break;
	case EZA_ConcertHall:
		effect = &DirectSoundI3DL2ListenerPreset_ConcertHall;
		break;
	case EZA_Cave:
		effect = &DirectSoundI3DL2ListenerPreset_Cave;
		break;
	case EZA_Arena:
		effect = &DirectSoundI3DL2ListenerPreset_Arena;
		break;
	case EZA_Hangar:
		effect = &DirectSoundI3DL2ListenerPreset_Hangar;
		break;
	case EZA_CarpetedHallway:
		effect = &DirectSoundI3DL2ListenerPreset_CarpetedHallway;
		break;
	case EZA_Hallway:
		effect = &DirectSoundI3DL2ListenerPreset_Hallway;
		break;
	case EZA_StoneCorridor:
		effect = &DirectSoundI3DL2ListenerPreset_StoneCorridor;
		break;
	case EZA_Alley:
		effect = &DirectSoundI3DL2ListenerPreset_Alley;
		break;
	case EZA_Forest:
		effect = &DirectSoundI3DL2ListenerPreset_Forest;
		break;
	case EZA_City:
		effect = &DirectSoundI3DL2ListenerPreset_City;
		break;
	case EZA_Mountains:
		effect = &DirectSoundI3DL2ListenerPreset_Mountains;
		break;
	case EZA_Quarry:
		effect = &DirectSoundI3DL2ListenerPreset_Quarry;
		break;
	case EZA_Plain:
		effect = &DirectSoundI3DL2ListenerPreset_Plain;
		break;
	case EZA_ParkingLot:
		effect = &DirectSoundI3DL2ListenerPreset_ParkingLot;
		break;
	case EZA_SewerPipe:
		effect = &DirectSoundI3DL2ListenerPreset_SewerPipe;
		break;
	case EZA_Underwater:
		effect = &DirectSoundI3DL2ListenerPreset_Underwater;
		break;
	case EZA_NoReverb:
		effect = &DirectSoundI3DL2ListenerPreset_NoReverb;
		break;
	default:
		break;
	}
	return effect;
}

/* scion ===============================================
* ::NotifyZoneChange
* Author: rayd
*
* Called when our listener has changed zones, used to
* update listener properties.
*
* =====================================================
*/
void UXACTAudioSubsystem::NotifyZoneChange(AZoneInfo *newZone)
{
	guard(NotifyZoneChange);
#if 0
	//debugf(NAME_DevSound,TEXT("notify zone change: %s %d"),newZone!=NULL?newZone->GetName():TEXT("NULL"),newZone!=NULL?newZone->ZoneAudioEffect:-1);
	const DSI3DL2LISTENER *newListenerProp = NULL;
	// use the zone audio effects
	if (newZone != NULL)
	{
		 newListenerProp = GetAudioEffectFromEnum((EZoneAudioEffect)newZone->ZoneAudioEffect);
	}
	// check to use default
	if (newListenerProp == NULL)
	{
		newListenerProp = GetAudioEffectFromEnum(EZA_Default);
	}
	if (SUCCEEDED(XACTEngine->SetI3dl2Listener(newListenerProp,DS3D_DEFERRED)))
	{
		//debugf(NAME_DevSound,TEXT("- applied new listener props"));
	}
#endif
	unguard;
}

/* epic ===============================================
* ::NoteDestroy
*
* Called when an actor is about to be destroyed, used
* to clean up all source references to the actor.
*
* =====================================================
*/
void UXACTAudioSubsystem::NoteDestroy(AActor* Actor)
{
	guard(UXACTAudioSubsystem::NoteDestroy);
	INT Idx;
	// Loop through removing all sources for this actor
	while ((Idx = GetSourceIdx(Actor)) != -1)
	{
		//debugf(NAME_DevSound,TEXT("Received destroy notification for source %s at index(%d)"),Actor->GetName(),Idx);
		// stop any ambient sounds associated with the actor
		StopAmbientSound(Actor);
		// if there are no cues on this source just remove it
		if (Sources(Idx).ActiveCueCnt == 0)
		{
			RemoveSource(Idx);
		}
		else
		{
			//debugf(NAME_DevSound,TEXT("Keeping source alive for %s at index(%d)"),Actor->GetName(),Idx);
			// null the actor ref and let it die out
			Sources(Idx).Actor = NULL;
		}
	}
	unguard;
}

/* scion ======================================================================
 * UXACTAudioSubsystem::CleanUp
 * Author: jg
 *
 * Flushes any left over objects in the sources array. Somehow during level
 * transitions a Pawn is created that is not destroyed using DestroyActor() so
 * NoteDestroy() is never called on the audio system. Then that Pawn is cleaned
 * up in garbage collection, so it is no longer valid, but still has a sound
 * source. This crashes later in Update() because the Actor pointer is non-null
 * but not valid. This seems to be a problem in our two GC calls in LoadMap()
 *
 * ============================================================================
 */
void UXACTAudioSubsystem::CleanUp(void)
{
	guard(UXACTAudioSubsystem::CleanUp);
	NotifyLevelChange();
	unguard;
}

UViewport* UXACTAudioSubsystem::GetViewport()
{
	return Viewport;
}

// Required for StoppedCues TMap.
FORCEINLINE DWORD GetTypeHash( const IXACTSoundCue* A )
{
	return (DWORD) A;
}

/* epic ===============================================
* ::ProcessNotifications
*
* Iterates through all notifications generated by XACT
* and looks for stopped cues.  Cleans up all references
* to the stopped cue, releases the instance if necessary
* and checks if the cue's source is now dead.
*
* =====================================================
*/
void UXACTAudioSubsystem::ProcessNotifications()
{
	guard(UXACTAudioSubsysstem::ProcessNotifications);

	// XACT tends to double stop sources from time to time which can cause a lot of trouble with our
	// manual looping code if we don't discard those.
	TMap<IXACTSoundCue*,INT> 	StoppedCues;
	XACT_NOTIFICATION 			XACTNotify;
	appMemzero( &XACTNotify, sizeof(XACTNotify) );

	// Retrieve all outstanding notifications...
	HRESULT hr;
	while( (hr=XACTEngine->GetNotification(NULL,&XACTNotify)) == DS_OK )
	{
		STAT_INC(GStats.DWORDStats(XACTStats.STATS_Notifications));
		XACT_NOTIFICATION Notify = XACTNotify;
		appMemzero( &XACTNotify, sizeof(XACTNotify) );

		if( Notify.Header.pSoundCue == MusicCueInfo.MusicCue &&
			MusicCueInfo.CueIdx != INDEX_NONE )
		{
			ProcessMusicNotifications( Notify );
		}
		else
		// check to see if this is a stop notification
		if( Notify.Header.wType == eXACTNotification_Stop )
		{
			UBOOL AlreadyStopped = StoppedCues.Find( Notify.Header.pSoundCue ) != NULL;
			if( AlreadyStopped )
				continue;
			StoppedCues.Set( Notify.Header.pSoundCue, 1 );

			INT CueIdx = GetCueIdx(Notify.Header.pSoundCue);
			if (CueIdx >= 0 &&
				CueIdx < Cues.Num())
			{
				UBOOL bIsAmbient = 0;

				INT srcIdx = Cues(CueIdx).SourceIdx;
				if (srcIdx >= 0 &&
					srcIdx < Sources.Num())
				{
					FSourceInfo& SourceInfo = Sources(srcIdx);

					// check to see if it's an ambient cue
					if( SourceInfo.AmbientCue == Notify.Header.pSoundCue )
					{
						// if we're not looping this ambient, we shouldn't be getting a stop notification on it
						if ((!bLoopAmbients || !SourceInfo.bLoopAmbient))
						{
							/* All of our static ambients are currently set not to loop in XACT and trigger this warning so disable it for now so there's no logspam
							if (SourceInfo.Actor)
							{
								debugf(NAME_Warning, TEXT("Received stop notification for ambient sound on actor %s with AmbientSoundCue property set to %s"), SourceInfo.Actor->GetName(), *SourceInfo.Actor->AmbientSoundCue);
							}
							else
							{
								debugf(NAME_Warning, TEXT("Received stop notification for ambient sound on source with NULL Actor!"));
							}
							*/

							continue;
						}
						bIsAmbient = 1;
						//debugf(NAME_DevSound,TEXT("- was ambient sound, clearing"));
						SourceInfo.AmbientCue = NULL;
#ifndef FINAL_RELEASE
						XACTStats.SrcCnt_Ambient -= 1;
#endif
					}
					else
					if (SourceInfo.ActiveCueCnt > 0)
					{
						// decrement the source's active cue count
						SourceInfo.ActiveCueCnt--;
					}
					//debugf(NAME_DevSound,TEXT("Received stop notification for cue %d, source %d: %s"), CueIdx, srcIdx, SourceInfo.Actor != NULL && SourceInfo.Actor->IsValid() ? SourceInfo.Actor->GetName():TEXT("NULL"));
				}
				else
				{
					debugf(NAME_DevSound,TEXT("SrcIdx = %d, Sources.Num() = %d"),srcIdx,Sources.Num());
				}
				UBOOL bRemoveCue=1;
				// if it hasn't been destroyed, stop it to release the cue
				if (!(Notify.Header.wFlags & XACT_FLAG_NOTIFICATION_SOUNDCUE_DESTROYED))
				{
					// scion sz - handle manual looping for ambient sounds
					if( bLoopAmbients &&
						bIsAmbient &&
						srcIdx >= 0 &&
						srcIdx < Sources.Num() )
					{
						FSourceInfo& SourceInfo = Sources(srcIdx);						

						// determine if the ambient sound should be manually looped
						if( SourceInfo.bLoopAmbient && SourceInfo.AmbientCuePrep )
						{
							FBankInfo& BankInfo = Banks(Cues(CueIdx).BankIdx);
							// start playing with the primed cue
							//debugf( NAME_DevSound, TEXT("- was ambient sound, looping") );
							// stop the previous ambient sound immediately
							Banks(Cues(CueIdx).BankIdx).pSoundBank->Stop(XACT_SOUNDCUE_INDEX_UNUSED,
								XACT_FLAG_SOUNDCUE_IMMEDIATE,
								Notify.Header.pSoundCue );
							// don't remove this cue instance since we are still playing it
							SourceInfo.AmbientCue = SourceInfo.AmbientCuePrep;
							Cues(CueIdx).Cue = SourceInfo.AmbientCue;
							bRemoveCue=0;
							// play the previously primed cue so that there is no hitch when looping the sound
							if( FAILED(hr = BankInfo.pSoundBank->Play( XACT_SOUNDCUE_INDEX_UNUSED, SourceInfo.SoundSource, 
								XACT_FLAG_SOUNDCUE_PREPARED, 
								&SourceInfo.AmbientCue )) )
							{
								SourceInfo.AmbientCue = SourceInfo.AmbientCuePrep = NULL;
								debugf( NAME_DevSound, TEXT("Error could not play looped ambient sound") );
							}							
							else 
							{
#ifndef FINAL_RELEASE
								XACTStats.SrcCnt_Ambient += 1;
#endif
								// prime the cue again for the next time we get a stop notification
								if( FAILED(hr = BankInfo.pSoundBank->Prepare( Notify.Header.dwSoundCueIndex, 
									XACT_FLAG_SOUNDCUE_PRIME, 
									&SourceInfo.AmbientCuePrep )) )
								{
									Banks(Cues(CueIdx).BankIdx).pSoundBank->Stop(XACT_SOUNDCUE_INDEX_UNUSED,
									XACT_FLAG_SOUNDCUE_IMMEDIATE,
									SourceInfo.AmbientCue );
									SourceInfo.AmbientCue = NULL;
									SourceInfo.AmbientCuePrep = NULL;
									debugf( NAME_DevSound, TEXT("Error could not prime looped ambient sound") );
								}	
							}
						}
					}
					if( bRemoveCue )
					{
						//debugf(NAME_DevSound,TEXT("- Stopping cue to release, wasn't auto release"));
						Banks(Cues(CueIdx).BankIdx).pSoundBank->Stop(XACT_SOUNDCUE_INDEX_UNUSED,
																	 0,
																	 Notify.Header.pSoundCue);
					}
				}
				// remove the cue from our list
				if( bRemoveCue )
				{
					Cues.Remove(CueIdx,1);
				}
			}
			else
			{
				debugf(NAME_DevSound,TEXT("CueIdx = %d, Cues.Num() = %d"),CueIdx,Cues.Num());
			}
		}
	}
	unguard;
}

/* scion ==============================================
* ::ProcessMusicNotifications
* Author: sz
*
* The music should loop continuously. When a stop 
* notification is received for the current cue, we
* switch playback to the primed cue.
*
* =====================================================
*/
void UXACTAudioSubsystem::ProcessMusicNotifications( const XACT_NOTIFICATION & Notify )
{
	guard(UXACTAudioSubsystem::ProcessMusicNotifications);
	check( Notify.Header.pSoundCue == MusicCueInfo.MusicCue );

	if( Notify.Header.wType == eXACTNotification_Stop )
	{
		if (!(Notify.Header.wFlags & XACT_FLAG_NOTIFICATION_SOUNDCUE_DESTROYED))
		{
			// stop main music cue
			if( MusicCueInfo.MusicCue )
			{
				MusicBank.pSoundBank->Stop(XACT_SOUNDCUE_INDEX_UNUSED,XACT_FLAG_SOUNDCUE_IMMEDIATE,MusicCueInfo.MusicCue);
				MusicCueInfo.MusicCue = NULL;
			}
			// start playing with the primed cue
			if( MusicCueInfo.MusicCuePrep )
			{
				debugf( NAME_DevSound, TEXT("Restarting music") );
				MusicBank.pSoundBank->Play( XACT_SOUNDCUE_INDEX_UNUSED, NULL, XACT_FLAG_SOUNDCUE_PREPARED, &MusicCueInfo.MusicCuePrep );
				MusicCueInfo.MusicCue = MusicCueInfo.MusicCuePrep;
				MusicCueInfo.MusicCuePrep = NULL;
				// prime the cue again for the next time we get a stop notification
				MusicBank.pSoundBank->Prepare( Notify.Header.dwSoundCueIndex, XACT_FLAG_SOUNDCUE_PRIME, &MusicCueInfo.MusicCuePrep );
			}
		}
		else
		{
			MusicCueInfo.MusicCue = NULL;
			MusicCueInfo.MusicCuePrep = NULL;
		}        
	}

	unguard;
}

/* scion ===============================================
* ::Update
* Author: rayd
*
* Update first processes any stop notifications, and
* handles cleaning up the cue instances of any sounds that
* have finished playing.  It then iterates through all
* 3D sound sources and updates their position/velocity,
* and then does the same for the listener position/velocity.
* Finally we call XACTEngineDoWork(), which lets XACT
* do all it's processing and actually play the audio.
*
* =====================================================
*/
void UXACTAudioSubsystem::Update()
{
	guard(UXACTAudioSubsystem::Update);

//	if( !GbXACTEnabled )
//		return;

	if (Viewport && Viewport->Actor && Viewport->Actor->ViewTarget && XACTEngine)
	{
		FUNC_CLOCK(AudioCyc,GStats.DWORDStats(GEngineStats.STATS_Game_AudioTickCycles));
		FUNC_CLOCK(UpdateCyc,GStats.DWORDStats(XACTStats.STATS_UpdateCycles));
		// figure out delta since last update
		FLOAT CurrentTime = Viewport->Actor->Level->TimeSeconds;
		FLOAT  DeltaTime = CurrentTime - LastTime;
		LastTime  = CurrentTime;
		DeltaTime = Clamp(DeltaTime,0.0001f,1.f);
		
		// Process notifications.
		ProcessNotifications();
		
		TArray<APlayerController*> ViewActors; // viewer for each viewport
		ViewActors.AddItem(Viewport->Actor);
		// warning: Assuming our Viewport variable points to UEngine::GEngine->Client->Viewports(0)
		for (INT i = 1; i < UEngine::GEngine->Client->Viewports.Num(); i++)
		{
			if ( UEngine::GEngine->Client->Viewports(i) && UEngine::GEngine->Client->Viewports(i)->Actor &&
				UEngine::GEngine->Client->Viewports(i)->Actor->ViewTarget )
			{
				ViewActors.AddItem(UEngine::GEngine->Client->Viewports(i)->Actor);
			}
			else
			{
				ViewActors.AddItem(NULL);
			}
		}

		INT PosUpdates = 0;
		guard(UpdateSources);
		// update all our sources and update 3d positions
		for (INT i = 0; i < Sources.Num(); i++)
		{
			// Get a ref to this so we can see what is happening
			FSourceInfo& Source = Sources(i);
			if (Source.ActiveCueCnt == 0 &&
				Source.AmbientCue == NULL &&
				Source.bValid)
			{
				RemoveSource(i);
			}
			else
			// if a 3d valid source, with active cues,
			if (!Source.bIs2D &&
				Source.SoundSource != NULL &&
				Source.Actor != NULL &&
				!Source.Actor->bStatic &&
				(Source.LastLocation.IsZero() || (Source.Actor->Location - Source.LastLocation).SizeSquared() > MIN_UPDATE_DELTA) &&
				(Source.AmbientCue != NULL || Source.ActiveCueCnt != 0))
			{
				// update the position
				if ( Source.ListenerViewportIndex == 0 || Source.ListenerViewportIndex >= ViewActors.Num() ||
					!ViewActors(Source.ListenerViewportIndex) )
				{
					Source.SoundSource->SetPosition(Source.Actor->Location.X, Source.Actor->Location.Y, Source.Actor->Location.Z, DS3D_DEFERRED);
				}
				else
				{
					// since we only have one 'real' listener, transform the source's location to be the same relative
					// location to the 'real' listener as it is to the listener that corresponds with the given viewport
					FVector FakePosition = MakeRelativeToViewport(Source.Actor->Location, ViewActors(Source.ListenerViewportIndex));
					Source.SoundSource->SetPosition(FakePosition.X, FakePosition.Y, FakePosition.Z, DS3D_DEFERRED);
				}
				Source.LastLocation = Source.Actor->Location;
				PosUpdates++;
				STAT_INC(GStats.DWORDStats(XACTStats.STATS_3DUpdates));
				// and velocity if it's a doppler source
				if (Source.bDoppler)
				{
					FVector Vel(Source.Actor->Velocity/DeltaTime);
					Source.SoundSource->SetVelocity(Vel.X,Vel.Y,Vel.Z,DS3D_DEFERRED);
				}
			}
		}
		// remove dead sources from the end of the list
		while (Sources.Num() > 0)
		{
			INT i = Sources.Num() - 1;
			if (Sources(i).Actor == NULL &&
				Sources(i).SoundSource == NULL &&
				!Sources(i).bValid)
			{
				Sources.Remove(i,1);
			}
			else
			{
				// no more to clean
				break;
			}
		}
		// Update ambient cues.
		if (Viewport->Actor->Level->TimeSeconds - LastAmbientUpdate > 1.f)
		{
			LastAmbientUpdate = Viewport->Actor->Level->TimeSeconds;
			UpdateAmbientCues();
		}
		unguard;

		// update our listening position, which is the position of our ViewTarget
		XACTEngine->SetListenerPosition( Viewport->Actor->ViewTarget->Location.X, Viewport->Actor->ViewTarget->Location.Y,
										Viewport->Actor->ViewTarget->Location.Z, DS3D_DEFERRED );
		// and orientation, which is the orientation of the Viewer (NOT ViewTarget!)
		FCoords ViewCoords = GMath.UnitCoords / Viewport->Actor->Rotation;
		XACTEngine->SetListenerOrientation( ViewCoords.XAxis.X, ViewCoords.XAxis.Y, ViewCoords.XAxis.Z,
											ViewCoords.ZAxis.X, ViewCoords.ZAxis.Y, ViewCoords.ZAxis.Z, DS3D_DEFERRED );
		// commit all of our changes
		XACTEngine->CommitDeferredSettings();
		// let xact do it's thing...
		XACTEngineDoWork();

		// Defer releasing the XACT source for a bit to avoid releasing a source that was just stopped.
		if( SourceReleaseList.Num() > 10 )
		{
			for( INT SourceIndex=0; SourceIndex<SourceReleaseList.Num(); SourceIndex++ )
			{
				SourceReleaseList(SourceIndex)->Release();
			}
			SourceReleaseList.Empty();
		}

#ifndef FINAL_RELEASE
		GStats.DWORDStats(XACTStats.STATS_2DSources) = XACTStats.SrcCnt_2D;
		GStats.DWORDStats(XACTStats.STATS_3DSources) = XACTStats.SrcCnt_3D + SourceReleaseList.Num();
		GStats.DWORDStats(XACTStats.STATS_Ambients) = XACTStats.SrcCnt_Ambient;
#endif
	}
	unguard;
}

/* scion ===============================================
* ::RemoveSource
* Author: rayd
*
* Cleans an entry in the Sources[] list for recycling.
*
* =====================================================
*/
void UXACTAudioSubsystem::RemoveSource(INT SrcIdx)
{
	guard(UXACTAudioSubsystem::RemoveSource);
	if (SrcIdx >= 0 &&
		SrcIdx < Sources.Num())
	{
#ifndef FINAL_RELEASE
		if (Sources(SrcIdx).bIs2D)
		{
			XACTStats.SrcCnt_2D--;
		}
		else
		{
			XACTStats.SrcCnt_3D--;
		}
#endif
		//debugf(NAME_DevSound,TEXT("Cleaning up source entry for actor %s, %d"), Sources(SrcIdx).Actor!=NULL?Sources(SrcIdx).Actor->GetName():TEXT("NULL"), SrcIdx);
		if (Sources(SrcIdx).SoundSource != NULL)
		{
			Sources(SrcIdx).SoundSource->StopSoundCues();
			// Defer releasing the source for a bit to avoid releasing a source that was just stopped.
			SourceReleaseList.AddItem( Sources(SrcIdx).SoundSource );
			Sources(SrcIdx).SoundSource = NULL;
		}
		Sources(SrcIdx).Actor = NULL;
		Sources(SrcIdx).ActiveCueCnt = NULL;
		Sources(SrcIdx).AmbientCue = NULL;
		Sources(SrcIdx).AmbientCuePrep = NULL;
		Sources(SrcIdx).bValid = false;
	}
	unguard;
}

/* scion ===============================================
* ::PlayMusic
* Author: rayd
*
* Registers the wave bank associated with the song and
* creates the A_Music sound bank if necessary.  The reason
* for breaking the wave/sound bank same name paradigm was
* because our music tracks are huge, and wanted to avoid
* unnecessary copy times for minor changes.
*
* NOTE: FadeInTime is ignored for XACT, change the actual
* song cue if you want to have it fade in.
*
* =====================================================
*/
INT UXACTAudioSubsystem::PlayMusic(FString Song, FLOAT FadeInTime)
{
	guard(UXACTAudioSubsystem::PlayMusic);
	// if there is a previous song playing
	if (MusicBank.pWaveBank != NULL)
	{
		StopMusic(0,0.f);
	}
	// load the music wave bank
	// attempt to load the wave bank file
	FString FileName = FString::Printf(TEXT("..\\Sounds\\M_%s.xwb"),*Song);
	//jg -- Handle path remapping for demos
	FileName = GFileManager->ResolvePath(*FileName);
	HANDLE hStreamingWaveBank = CreateFile((LPCSTR)TCHAR_TO_ANSI(*FileName),
										   GENERIC_READ,FILE_SHARE_READ,NULL,
										   OPEN_EXISTING,FILE_FLAG_OVERLAPPED|FILE_FLAG_NO_BUFFERING,NULL);
	if (hStreamingWaveBank != INVALID_HANDLE_VALUE)
	{
		// Initialize the streaming wave bank parameters
		XACT_WAVEBANK_STREAMING_PARAMETERS wbParams = {0};
		wbParams.hFile                        = hStreamingWaveBank;
		wbParams.dwOffset                     = 0;
		wbParams.dwPacketSizeInMilliSecs      = MUSIC_STREAMING_PACKET_SIZE_MS;
		wbParams.dwPrimePacketSizeInMilliSecs = MUSIC_STREAMING_PACKET_SIZE_MS_PRIME;
		IXACTWaveBank *pWaveBank = NULL;
		HRESULT hr;
		if (SUCCEEDED(hr = XACTEngine->RegisterStreamedWaveBank(&wbParams,&pWaveBank)))
		{
			MusicBank.pWaveBank = pWaveBank;
			MusicBank.hFile = hStreamingWaveBank;
			// if the sound bank hasn't been created yet, load it
			if (MusicBank.pSoundBank == NULL)
			{
				IXACTSoundBank *pSoundBank = CreateSoundBank(FName(TEXT("A_Music")),MusicBank.pbBankData);
				if (pSoundBank != NULL)
				{
					MusicBank.pSoundBank = pSoundBank;
				}
			}
		}
		else
		{
			//jg -- Use the common safer routine
			CloseFileHandle(hStreamingWaveBank);
			debugf(NAME_Error, TEXT("Failed to register wave bank M_%s"), *Song);
		}
	}
	else
	{
		debugf(NAME_Error, TEXT("Failed to load wave bank M_%s"), *FileName);
	}
	if (MusicBank.pWaveBank != NULL &&
		MusicBank.pSoundBank != NULL)
	{
		// attempt to register the sound cue
		HRESULT hr;
		if (SUCCEEDED(hr = MusicBank.pSoundBank->GetSoundCueIndexFromFriendlyName((LPCSTR)TCHAR_TO_ANSI(*Song),&MusicCueInfo.CueIdx)))
		{
			// scion sz - determine if the sound is already set for automatic looping
			UBOOL bLooping = 0;
			XACT_SOUNDCUE_PROPERTIES cueProps;
			HRESULT hr;
			hr = MusicBank.pSoundBank->GetSoundCueProperties( MusicCueInfo.CueIdx, &cueProps );
			if( SUCCEEDED(hr) )
			{
				if( cueProps.dwLoopCount > 0 || 
					cueProps.dwLoopCount == XACT_LOOP_INFINITE )
		{
					bLooping = 1;
				}
			}

			// and finally play the actual cue
			if (FAILED(hr = MusicBank.pSoundBank->Play(MusicCueInfo.CueIdx, NULL, 0, &MusicCueInfo.MusicCue)))
			{
				debugf(NAME_Error, TEXT("Wave bank M_%s is missing, corrupt, or not a streaming wave bank!\nWARNING: Future attempts to register wave banks may now fail!\n"), *Song);
				checkSlow(false && "This level is using a broken wave bank for music!");
			}
			// scion sz - if we want to loop music manually then we need to prime a secondary cue
			// as soon as the primary cue starts playing
			else if( bLoopMusic && !bLooping )
			{
				// register stop notification for music
				HandleNotificationRegistration(XACTEngine,1,MusicCueInfo.CueIdx,MusicBank.pSoundBank,eXACTNotification_Stop);
				// prime the secondary cue to be ready to loop the music when it ends
                MusicBank.pSoundBank->Prepare( MusicCueInfo.CueIdx, XACT_FLAG_SOUNDCUE_PRIME, &MusicCueInfo.MusicCuePrep );			
			}
		}
		else
		{
			debugf(NAME_Error, TEXT("Failed to find sound cue A_Music.%s"), *Song);
		}
	}
	return -1;
	unguard;
}

/* scion ===============================================
* ::StopMusic
* Author: rayd
*
* Stops the current music track and unregisters the
* wave bank.
*
* NOTE: FadeOutTime is ignored for XACT, change the
* actual song cue to have it fade out.
*
* =====================================================
*/
UBOOL UXACTAudioSubsystem::StopMusic(INT SongHandle, FLOAT FadeOutTime)
{
	guard(UXACTAudioSubsystem::StopMusic);
	if (MusicBank.pWaveBank != NULL)
	{
		// stop main music cue
		if( MusicCueInfo.MusicCue )
		{
			MusicBank.pSoundBank->Stop(XACT_SOUNDCUE_INDEX_UNUSED,XACT_FLAG_SOUNDCUE_IMMEDIATE,MusicCueInfo.MusicCue);
			MusicCueInfo.MusicCue = NULL;
		}
		// release prep music cue
		if( MusicCueInfo.MusicCuePrep )
		{
			MusicBank.pSoundBank->Stop(XACT_SOUNDCUE_INDEX_UNUSED,XACT_FLAG_SOUNDCUE_IMMEDIATE,MusicCueInfo.MusicCuePrep);
			MusicCueInfo.MusicCuePrep = NULL;
		}		
		// unregister the wave bank
		XACTEngine->UnRegisterWaveBank(MusicBank.pWaveBank);
		// and null the wave bank
		MusicBank.pWaveBank = NULL;
		//jg -- Use the common safer routine
		CloseFileHandle(MusicBank.hFile);
	}
	return 0;
	unguard;
}

/* scion ===============================================
* ::StopAllMusic
* Author: rayd
*
* See StopMusic().
*
* =====================================================
*/
INT UXACTAudioSubsystem::StopAllMusic(FLOAT FadeOutTime)
{
	return StopMusic(0,0.f);
}

#ifdef WITH_LIPSINC
DOUBLE UXACTAudioSubsystem::OffsetInSound(AActor *Actor, USound* Sound)
{
	return 0.f;
}

UBOOL UXACTAudioSubsystem::PauseSound(AActor *Actor, USound* Sound)
{
	return 0;
}

UBOOL UXACTAudioSubsystem::ResumeSound(AActor *Actor, USound* Sound)
{
	return 0;
}
#endif

/* scion ===============================================
* ::StopAmbientSound
* Author: rayd
*
* Gets the source for the specified actor and stops any
* ambient sound previously played on said source.
*
* =====================================================
*/
void UXACTAudioSubsystem::StopAmbientSound(AActor *srcActor)
{
	guard(UXACTAudioSubsystem::StopAmbientSound);
	if (srcActor != NULL)
	{
		// find a matching source for the actor
		INT srcIdx = GetSourceIdx(srcActor, 0, 0, 1);
		// if we have a valid source and an ambient cue ref,
		if (srcIdx >= 0 &&
			srcIdx < Sources.Num() &&
			Sources(srcIdx).AmbientCue != NULL)
		{
			// stop the previous ambient cue instance
			INT cueIdx = GetCueIdx(Sources(srcIdx).AmbientCue);
			if (cueIdx >= 0 &&
				cueIdx < Cues.Num())
			{
				//debugf(NAME_DevSound,TEXT("Stopping previous ambient sound on source %s"),Sources(srcIdx).Actor->GetName());
				// grab the bank this cue is playing in
				INT bankIdx = Cues(cueIdx).BankIdx;
				// stop the actual cue
				Banks(bankIdx).pSoundBank->Stop(XACT_SOUNDCUE_INDEX_UNUSED, XACT_FLAG_SOUNDCUE_IMMEDIATE, Sources(srcIdx).AmbientCue);
				// scionsz - stop the prep cue if it exists
				if( Sources(srcIdx).AmbientCuePrep && 
					Sources(srcIdx).AmbientCuePrep != Sources(srcIdx).AmbientCue )
				{
					Banks(bankIdx).pSoundBank->Stop(XACT_SOUNDCUE_INDEX_UNUSED, XACT_FLAG_SOUNDCUE_IMMEDIATE, Sources(srcIdx).AmbientCuePrep);
					Sources(srcIdx).AmbientCuePrep = NULL;
				}
				// remove the cue entry
				Cues.Remove(cueIdx,1);
				// invalidate the ambient cue ref
				Sources(srcIdx).AmbientCue = NULL;
#ifndef FINAL_RELEASE
				XACTStats.SrcCnt_Ambient -= 1;
#endif
			}
#ifndef FINAL_RELEASE
			else if (srcActor->AmbientSoundCue != NAME_None && !srcActor->bStatic)
			{
				debugf(NAME_DevSoundError, TEXT("Failed to find cue instance to stop ambient sound %s playing on actor %s"), *(srcActor->AmbientSoundCue), srcActor->GetName());
			}
#endif
		}
#ifndef FINAL_RELEASE
		else if (srcActor->AmbientSoundCue != NAME_None && !srcActor->bStatic)
		{
			debugf(NAME_DevSoundError, TEXT("Failed to find source to stop ambient sound %s playing on actor %s"), *(srcActor->AmbientSoundCue), srcActor->GetName());
		}
#endif
	}
	unguard;
}

// max number of active ambients to allow
#define MAX_AMBIENT				5

void UXACTAudioSubsystem::UpdateAmbientCues()
{
	guard(UXACTAudioSubsystem::UpdateAmbientCues);

	// check to see if we have a list of static ambients
	if (StaticAmbientList.Num() == 0)
	{
		// if not, then build it now
		debugf(NAME_DevSound,TEXT("Building static ambient sound list"));
		ULevel *level = Viewport->Actor->GetLevel();
		for (INT idx = 0; idx < level->Actors.Num(); idx++)
		{
			if (level->Actors(idx) != NULL &&
				level->Actors(idx)->bStatic)
			{
				AAmbientSound *amb = Cast<AAmbientSound>(level->Actors(idx));
				if (amb != NULL)
				{
					// add to the list
					debugf(NAME_DevSound,TEXT("- found static ambient %s"),amb->GetName());
					StaticAmbientList.AddItem(amb);
				}
			}
		}
	}
	// if we have ambients to deal with
	if (StaticAmbientList.Num() > 0)
	{
		// then start building the prioritized (sorted) list, up to our max static ambient count
		TArray<FAmbientPriority> priorityList;
		AActor *listener = Viewport->Actor;
		// use pawn if available
		if (Viewport->Actor->Pawn != NULL)
		{
			listener = Viewport->Actor->Pawn;
		}
		FLOAT maxPriority = 1.f;
		for (INT idx = 0; idx < StaticAmbientList.Num(); idx++)
		{
			AAmbientSound *amb = StaticAmbientList(idx);
			// initial priority based on distance
			FLOAT dist = (amb->Location - listener->Location).Size();
			FLOAT priority = dist/GetSoundCueRadius(amb->AmbientSoundCue);
			if (priority <= 1.f &&
				(priority < maxPriority || priorityList.Num() < MAX_AMBIENT))
			{
				// insert the new ambient in the list
				UBOOL bInserted = 0;
				for (INT pIdx = 0; pIdx < priorityList.Num() && !bInserted; pIdx++)
				{
					if (priority < priorityList(pIdx).Priority)
					{
						bInserted = 1;
						priorityList.Insert(pIdx,1);
						priorityList(pIdx).StaticIdx = idx;
						priorityList(pIdx).Priority = priority;
					}
				}
				// if no insert, append
				if (!bInserted)
				{
					maxPriority = priority;
					INT pIdx = priorityList.AddZeroed(1);
					priorityList(pIdx).StaticIdx = idx;
					priorityList(pIdx).Priority = priority;
				}
				// kill off the end of the list
				while (priorityList.Num() > MAX_AMBIENT)
				{
					priorityList.Pop();
					maxPriority = priorityList(priorityList.Num()-1).Priority;
				}
			}
		}
		// compare against the active ambient list
		for (INT idx = 0; idx < ActiveAmbientList.Num(); idx++)
		{
			UBOOL bInList = 0;
			INT pIdx;
			for (pIdx = 0; pIdx < priorityList.Num(); pIdx++)
			{
				if (ActiveAmbientList(idx) == priorityList(pIdx).StaticIdx)
				{
					bInList = 1;
					break;
				}
			}
			// if not in the prioritized list, then stop and remove it
			if (!bInList)
			{
				StopAmbientSound(StaticAmbientList(ActiveAmbientList(idx)));
				ActiveAmbientList.Remove(idx--,1);
			}
			// otherwise, remove from prioritized list (so that it isn't played twice)
			else
			{
				priorityList.Remove(pIdx,1);
			}
		}
		// play all the remaining ambients in the cue
		for (INT idx = 0; idx < priorityList.Num(); idx++)
		{
			AActor *amb = StaticAmbientList(priorityList(idx).StaticIdx);
			PlaySoundCue(amb,amb->AmbientSoundCue,1);
			// and add to active ambient list
			ActiveAmbientList.AddItem(priorityList(idx).StaticIdx);
		}
	}

	unguard;
}

/* scion ===============================================
* ::PlaySoundCue
* Author: rayd
*
* Plays the given sound on the given actor.  Automatically
* handles registration of soundCue with the proper source
* banks and cue friendly name, as well creating a new
* sound source for srcActor if one doesn't already exist.
*
* =====================================================
*/
void UXACTAudioSubsystem::PlaySoundCue(AActor *srcActor, FName soundCue, UBOOL bAmbient, FLOAT Volume)
{
	guard(UXACTAudioSubsystem::PlaySoundCue);
	FUNC_CLOCK(UpdateCyc,GStats.DWORDStats(XACTStats.STATS_UpdateCycles));

//	if( !GbXACTEnabled )
//		return;

#ifdef _DEBUG
	if (Cast<AWeapon>(srcActor))
	{
		debugf(NAME_Warning, TEXT("Playing sound on weapon %s not supported (use the Weapon's ThirdPersonActor instead)"), srcActor->GetName());
	}
#endif

	// first, if ambient, stop the previous ambient
	if (bAmbient)
	{
		StopAmbientSound(srcActor);
	}
	// if we have a valid sound cue name, we're initialized, and we're not trying to play a sound on a destroyed actor
	if (soundCue != NAME_None && Viewport != NULL && bInitialized && (srcActor == NULL || !srcActor->bDeleteMe))
	{
		// try to find this named cue
		INT cueInfoIdx = GetNamedCueIdx( soundCue );
		// if we have a valid cueInfoIdx,
		if (cueInfoIdx >= 0 &&
			cueInfoIdx < SoundCues.Num())
		{
			FSoundCueInfo &cueInfo = SoundCues(cueInfoIdx);
			// if not given a srcActor,
			if (srcActor == NULL)
			{
				//debugf(NAME_DevSound,TEXT("Playing sound %s on default source"),*soundCue);
				// assume this is a fire and forget, 2D sound
				Banks(cueInfo.BankIdx).pSoundBank->Play(cueInfo.CueIdx,
														NULL,
														XACT_FLAG_SOUNDCUE_AUTORELEASE,
														NULL);
			}
			else
			{
				// check to see if this needs a 2d source
				UBOOL bIs2D = false;
				// if it should be 2d for any viewport, then play it 2d
				// if not, find closest viewer and the distance to it
				INT ClosestViewport = 0;
				FLOAT ClosestDistance = 1000000.f;
				for (INT i = 0; i < UEngine::GEngine->Client->Viewports.Num() && !bIs2D; i++)
				{
					if (UEngine::GEngine->Client->Viewports(i))
					{
						APlayerController* ViewActor = UEngine::GEngine->Client->Viewports(i)->Actor;
						if (ViewActor && ViewActor->bIsPlayer)
						{
							if (srcActor == ViewActor || srcActor->IsOwnedBy(ViewActor->ViewTarget))
							{
								bIs2D = true;
							}
							else if (ViewActor->ViewTarget)
							{
								FLOAT Distance = (srcActor->Location - ViewActor->ViewTarget->Location).Size();
								if (Distance < ClosestDistance)
								{
									ClosestViewport = i;
									ClosestDistance = Distance;
								}
							}
						}
					}
				}
				
				UBOOL bSkipSound = false;
				// Skip sounds that are far away. The more sounds being played, the more aggressive the culling.
				if (!bIs2D && !bAmbient && Volume <= 0.f)
				{
					FLOAT FudgedDistance = ClosestDistance * (1 + ::Max<FLOAT>(0.f, FLOAT(Cues.Num()) / 15.f));
					if (GetSoundCueRadius(soundCue) < FudgedDistance)
					{
						bSkipSound = true;
					}
				}
				
				if (!bSkipSound)
				{
					// find a matching source for the actor
					INT srcIdx = GetSourceIdx(srcActor, 1, bIs2D, bAmbient);
					// if we have a valid source,
					if (srcIdx >= 0 &&
						srcIdx < Sources.Num())
					{
						FSourceInfo& SourceInfo = Sources(srcIdx);

						// grab the matching sound source
						//NOTE: this will be NULL for 2d sounds
						IXACTSoundSource *soundSrc = SourceInfo.SoundSource;

						// mark closest viewport; the sound is played as heard by this player
						if (bIs2D)
						{
							SourceInfo.ListenerViewportIndex = 0;
						}
						else
						{
							SourceInfo.ListenerViewportIndex = ClosestViewport;
							// update sound source's position
							// necessary in case srcActor is destroyed later in this frame
							if (ClosestViewport == 0 || !Viewport->Actor || !Viewport->Actor->ViewTarget)
							{
								soundSrc->SetPosition(srcActor->Location.X, srcActor->Location.Y, srcActor->Location.Z, DS3D_DEFERRED);
							}
							else
							{
								// this source is closest to some other listener than the primary viewport
								// so need to move it to be relative to the 'real' listener
								FVector FakePosition = MakeRelativeToViewport(srcActor->Location, UEngine::GEngine->Client->Viewports(ClosestViewport)->Actor);
								soundSrc->SetPosition(FakePosition.X, FakePosition.Y, FakePosition.Z, DS3D_DEFERRED);
							}
							SourceInfo.LastLocation = srcActor->Location;
						}

						IXACTSoundCue *cue = NULL;
						HRESULT hr;
						// scion sz - play cue using cue index and retrieve the cue
						hr = Banks(cueInfo.BankIdx).pSoundBank->Play(cueInfo.CueIdx,
																	 soundSrc,
																	 0,
																	 &cue);

						if (SUCCEEDED(hr))						 	
						{
							//debugf(NAME_DevSound,TEXT("Playing sound %s on %s source %s%s"),
							//	   *soundCue,
							//	   SourceInfo.bIs2D?TEXT("2D"):TEXT("3D"),
							//	   SourceInfo.Actor!=NULL?SourceInfo.Actor->GetName():TEXT("NULL"),
							//	   bAmbient?TEXT(" [Ambient]"):TEXT(""));
							// add this cue instance to our list for potential stopping later
							INT Idx = Cues.Num();
							Cues.AddZeroed();
							Cues(Idx).Cue = cue;
							Cues(Idx).BankIdx = cueInfo.BankIdx;
							Cues(Idx).SourceIdx = srcIdx;
							// if this was an ambient cue save the instance on the source
							if (bAmbient)
							{
								Sources(srcIdx).AmbientCue = cue;
#ifndef FINAL_RELEASE
								XACTStats.SrcCnt_Ambient += 1;
#endif
								// scion sz - only loop manually if the sound is not setup for automatic looping in XACT
								if( bLoopAmbients ) 
								{
									SourceInfo.bLoopAmbient = (bLoopAmbients && SourceInfo.bLoopAmbient && !IsSoundCueLooping(soundCue)) ? 1:0;
									// scion sz - if the ambient should loop prime the next cue
									// so that it will be ready to play when the sound stops
									if( bAmbient && SourceInfo.bLoopAmbient && !SourceInfo.AmbientCuePrep )
									{
										hr = Banks(cueInfo.BankIdx).pSoundBank->Prepare( cueInfo.CueIdx,										
											XACT_FLAG_SOUNDCUE_PRIME,
											&SourceInfo.AmbientCuePrep );                                    
									}
								}
								
							}
							else
							{
								// and increment the active cue count for this source
								Sources(srcIdx).ActiveCueCnt++;
							}
							// set volume parameter control, if a value was specified for it
							if (Volume > 0.f)
							{
								XACT_PARAMETER_ENTRY ParamEntry;
								ParamEntry.wScope = eXACTParameterControlScope_SoundCueInstance;
								ParamEntry.wFlags = 0;
								ParamEntry.wCategoryIndex = 0;
								ParamEntry.wStepCount = 0;
								ParamEntry.flStepSize = 0.f;
								ParamEntry.flStartValue = Volume;
								ParamEntry.u.pSoundCue = cue;
								ParamEntry.pszFriendlyName = "Volume";
								XACT_PARAMETER_CONTROL_DESC Params;
								Params.dwEntryCount = 1;
								Params.paEntries = &ParamEntry;
								XACTEngine->SetParameterControl(&Params);
							}
						}
						else
						{
							debugf(NAME_DevSoundError, TEXT("Failed to play sound %s on %s source %s%s"),
								   *soundCue,
								   Sources(srcIdx).bIs2D?TEXT("2D"):TEXT("3D"),
								   Sources(srcIdx).Actor!=NULL?Sources(srcIdx).Actor->GetName():TEXT("NULL"),
								   bAmbient?TEXT(" [Ambient]"):TEXT(""));
						}
					}
				}
				else
				{
					//debugf(NAME_DevSound,TEXT("Skipped playing 3d sound %s due to distance from listener"), *soundCue);
				}
			}
		}
		else
		{
			debugf(NAME_Error,TEXT("Failed to play sound %s on actor %s%s"),
				*soundCue,
				srcActor!=NULL?srcActor->GetName():TEXT("NULL"),
				bAmbient?TEXT(" (ambient)"):TEXT(""));
		}
	}
	unguardf((TEXT("SoundCue: %s"), *soundCue));
}

/* epic ===============================================
* static HandleNotificationRegistration
*
* Utility function that registers (and unregisters) all
* notifications we care about for a given sound cue.
*
* =====================================================
*/
static UBOOL HandleNotificationRegistration(IXACTEngine *XACTEngine, UBOOL bRegister, DWORD soundCueIdx, IXACTSoundBank *pSoundBank, XACT_NOTIFICATION_TYPE notification)
{
	// register a notification for sound cue
	XACT_NOTIFICATION_DESCRIPTION desc;
	desc.wType = notification;
	desc.wFlags = XACT_FLAG_NOTIFICATION_USE_SOUNDCUE_INDEX;
	if (bRegister)
	{
		desc.wFlags |= XACT_FLAG_NOTIFICATION_PERSIST;
	}
	desc.u.pSoundBank = pSoundBank;
	desc.dwSoundCueIndex = soundCueIdx;
	desc.pSoundCue = NULL;
	desc.pvContext = NULL;
	desc.hEvent = NULL;
	if (bRegister)
	{
		return SUCCEEDED(XACTEngine->RegisterNotification(&desc));
	}
	else
	{
		return SUCCEEDED(XACTEngine->UnRegisterNotification(&desc));
	}
}

/* scion ===============================================
* ::RegisterSoundCue
* Author: rayd
*
* Splits apart soundCue and attempts to find a matching
* wave and sound bank, as well as register the actual
* cue.  If successful, a new entry is added to SoundCues
* and the index is returned, otherwise
*
* =====================================================
*/
INT UXACTAudioSubsystem::RegisterSoundCue(FName soundCue)
{
	guard(UXACTAudioSubsystem::RegisterSoundCue);
	//debugf(NAME_DevSound,TEXT("RegisterSoundCue(%s)"),*soundCue);
	INT cueIdx = -1;
	FString srcString = *soundCue;
	TArray<FString> splitStrings;
	// split the soundCue into <bankname>.<group>.<cuename>
	if (srcString.ParseIntoArray(TEXT("."),&splitStrings) >= 2)
	{
		// the first string will always be the bank,
		FString bankName = splitStrings(0);
		// always skip the group entry if it exists
		FString cueName = FString::Printf(TEXT("%s\\%s"),*bankName,splitStrings.Num() > 2 ? *splitStrings(2) : *splitStrings(1));
		// now register the wave/sound bank
		INT bankIdx = GetBankIdx(FName(*bankName));
		if (bankIdx >= 0 &&
			bankIdx < Banks.Num())
		{
			// registered the bank, now register the sound cue
			DWORD soundCueIdx;
			HRESULT hr;
			if (SUCCEEDED(hr = Banks(bankIdx).pSoundBank->GetSoundCueIndexFromFriendlyName((LPCSTR)TCHAR_TO_ANSI(*cueName),&soundCueIdx)))
			{
				guard(RegisterNotification);

				//epic moelfke: we don't handle stop notifications for ambient/looping sounds so tell XACT not to send them
				UBOOL bLooping = 0;
				XACT_SOUNDCUE_PROPERTIES cueProps;
				hr = Banks(bankIdx).pSoundBank->GetSoundCueProperties(soundCueIdx,&cueProps);
				if (SUCCEEDED(hr))
				{
					if( cueProps.dwLoopCount > 0 || 
						cueProps.dwLoopCount == XACT_LOOP_INFINITE )
					{
						bLooping = 1;
					}				
				}				
				if( bLooping )
				{
					HandleNotificationRegistration(XACTEngine,0,soundCueIdx,Banks(bankIdx).pSoundBank,eXACTNotification_Stop);					
				}
				else
				{
					// register a notification for sound cue
					HandleNotificationRegistration(XACTEngine,1,soundCueIdx,Banks(bankIdx).pSoundBank,eXACTNotification_Stop);
				}				
				// unregister notifications that we don't care about
				HandleNotificationRegistration(XACTEngine,0,soundCueIdx,Banks(bankIdx).pSoundBank,eXACTNotification_Start);
				HandleNotificationRegistration(XACTEngine,0,soundCueIdx,Banks(bankIdx).pSoundBank,eXACTNotification_StreamDataAvailable);
				HandleNotificationRegistration(XACTEngine,0,soundCueIdx,Banks(bankIdx).pSoundBank,eXACTNotification_SoundCueProcessing);
				unguard;
				// create a new entry for this info
				cueIdx = SoundCues.Num();
				SoundCues.AddZeroed();
				SoundCues(cueIdx).CueIdx = soundCueIdx;
				SoundCues(cueIdx).BankIdx = bankIdx;
				// mark it as keep jic
				// scion sz - removed since we want the names to be removed during level change
				// soundCue.SetFlags(RF_Keep);
			}
			else
			{
				debugf(NAME_DevSound, TEXT("Failed to find sound cue %s"), *soundCue);
			}
		}
		else
		{
			debugf(NAME_DevSound,TEXT("Failed to register sound cue %s"),*soundCue);
		}
	}
	// return the resulting index
	return cueIdx;
	unguard;
}

/* epic ===============================================
* ::StopSoundCue
*
* Searches through all the active cue instances on the
* given source and stops them.
*
* =====================================================
*/
void UXACTAudioSubsystem::StopSoundCue(AActor *srcActor, FName soundCue)
{
	guard(UXACTAudioSubsystem::StopSoundCue);
	FUNC_CLOCK(UpdateCyc,GStats.DWORDStats(XACTStats.STATS_UpdateCycles));
	if (soundCue != NAME_None &&
		Viewport != NULL &&
		srcActor != NULL &&
		bInitialized)
	{
		// try to find this named cue
		INT cueInfoIdx = GetNamedCueIdx( soundCue );
		// if we have a valid cueInfoIdx,
		if (cueInfoIdx >= 0 &&
			cueInfoIdx < SoundCues.Num())
		{
			FSoundCueInfo &cueInfo = SoundCues(cueInfoIdx);
			// check to see if this needs a 2d source (ambients are always 3d)
			UBOOL bIs2D = (srcActor->IsA(AController::StaticClass()) ||
						   (Viewport->Actor != NULL &&
							srcActor->IsOwnedBy(Viewport->Actor->ViewTarget)));
			// find a matching source for the actor
			INT srcIdx = GetSourceIdx(srcActor,0,bIs2D);
			if (srcIdx >= 0)
			{
			// find the cue instances and remove them
			for (INT i = 0; i < Cues.Num(); i++)
			{
				// if matching cue index, and matching source,
				if (Cues(i).CueIdx == cueInfo.CueIdx &&
					Cues(i).SourceIdx == srcIdx)
				{
					// stop the cue
					Banks(cueInfo.BankIdx).pSoundBank->Stop(XACT_SOUNDCUE_INDEX_UNUSED,
															0,
															Cues(i).Cue);
					// remove this cue from the source
					if (Sources(srcIdx).ActiveCueCnt > 0)
					{
						Sources(srcIdx).ActiveCueCnt--;
					}
					Cues(i).Cue = NULL;
					Cues(i).CueIdx = -1;
					Cues(i).BankIdx = -1;
					Cues(i).SourceIdx = -1;
					// remove from the list as well
					Cues.Remove(i--,1);
				}
			}
		}
	}
	}
	unguard;
}

void UXACTAudioSubsystem::StopSoundCues(AActor *srcActor)
{
	guard(UXACTAudioSubystem::StopSoundCues);
	/*
	if (srcActor != NULL &&
		Viewport != NULL &&
		Viewport->Actor != NULL &&
		Viewport->Actor->ViewTarget != NULL)
	{
		// find the matching source for this sound
		INT srcIdx = GetSourceIdx(srcActor,0,srcActor->IsOwnedBy(Viewport->Actor->ViewTarget));
		if (srcIdx >= 0 &&
			srcIdx < Sources.Num())
		{
			// iterate through the cue list and stop all cues on this source
			for (INT i = 0; i < Cues.Num(); i++)
			{
				if (Cues(i).SourceIdx == srcIdx)
				{
					if (Cues(i).Cue != NULL)
					{
						// found a match, call stop on the sound bank for this cue
						Banks(Cues(i).BankIdx).pSoundBank->Stop(XACT_SOUNDCUE_INDEX_UNUSED,0,Cues(i).Cue);
						// decrement the source's active cue count
						Sources(srcIdx).ActiveCueCnt--;
					}
					// and remove this cue instance from our list
					Cues.Remove(i--,1);
				}
			}
		}
	}
	*/
	unguard;
}

/* epic ===============================================
* ::GetSoundCueDuration
*
* Queries XACT for the duration of the given cue and
* returns the result.
*
* =====================================================
*/
FLOAT UXACTAudioSubsystem::GetSoundCueDuration(FName soundCue)
{
	guard(UXACTAudioSubsystem::GetSoundCueDuration);
	FLOAT duration = 0.f;
	if (soundCue != NAME_None)
	{
		// try to find this named cue
		INT cueInfoIdx = GetNamedCueIdx( soundCue );
		// if we have a valid cueInfoIdx,
		if (cueInfoIdx >= 0 &&
			cueInfoIdx < SoundCues.Num())
		{
			FSoundCueInfo &cueInfo = SoundCues(cueInfoIdx);
			// query the bank for the cue info
			XACT_SOUNDCUE_PROPERTIES cueProps;
			HRESULT hr;
			hr = Banks(cueInfo.BankIdx).pSoundBank->GetSoundCueProperties(cueInfo.CueIdx,&cueProps);
			if (SUCCEEDED(hr))
			{
				//debugf(NAME_DevSound,TEXT("Sound cue %s duration %d"),*soundCue,cueProps.dwLength);
				duration = cueProps.dwLength/1000.f;
			}
		}
	}
	return duration;
	unguard;
}

/* epic ===============================================
* ::GetSoundCueRadius
*
* Attempts to figure out the radius for the given sound
* cue.
*
* =====================================================
*/
FLOAT UXACTAudioSubsystem::GetSoundCueRadius(FName soundCue)
{
	guard(UXACTAudioSubsystem::GetSoundCueDuration);
	FLOAT radius = 0.f;
	if (soundCue != NAME_None)
	{
		// try to find this named cue
		INT cueInfoIdx = GetNamedCueIdx( soundCue );
		// if we have a valid cueInfoIdx,
		if (cueInfoIdx >= 0 &&
			cueInfoIdx < SoundCues.Num())
		{
			FSoundCueInfo &cueInfo = SoundCues(cueInfoIdx);
			// query the bank for the cue info
			XACT_SOUNDCUE_PROPERTIES cueProps;
			HRESULT hr;
			hr = Banks(cueInfo.BankIdx).pSoundBank->GetSoundCueProperties(cueInfo.CueIdx,&cueProps);
			if (SUCCEEDED(hr))
			{
				//db = -20*log10(1+(distance - min distance)*rolloff/mindist)
				//TODO: calculate the real rolloff distance?
				radius = (cueProps.flMinDistance*2.f) + (cueProps.flMinDistance * (2.f/cueProps.flRolloffFactor));
			}
		}
			}
	return radius;
	unguard;
		}

/* scion ==============================================
* ::GetSoundCueRadius
* Author: sz
*
* Returns true if the sound cue is marked as looping in
* XACT.
*
* =====================================================
*/
UBOOL UXACTAudioSubsystem::IsSoundCueLooping(FName soundCue)
{
	guard(UXACTAudioSubsystem::IsSoundCueLooping);
	UBOOL bLooping = 0;
	if (soundCue != NAME_None)
		{
		// try to find this named cue
		INT cueInfoIdx = GetNamedCueIdx( soundCue );
		// if we have a valid cueInfoIdx,
		if (cueInfoIdx >= 0 &&
			cueInfoIdx < SoundCues.Num())
		{
			FSoundCueInfo &cueInfo = SoundCues(cueInfoIdx);
			// query the bank for the cue info
			XACT_SOUNDCUE_PROPERTIES cueProps;
			HRESULT hr;
			hr = Banks(cueInfo.BankIdx).pSoundBank->GetSoundCueProperties(cueInfo.CueIdx,&cueProps);
			if (SUCCEEDED(hr))
			{
				if( cueProps.dwLoopCount > 0 || 
					cueProps.dwLoopCount == XACT_LOOP_INFINITE )
				{
					bLooping = 1;
				}				
			}
		}
	}
    return bLooping;
	unguard;
}

/* epic ===============================================
* ::GetCategoryVolume
*
* Returns the cached volume setting for the given
* category.
*
* =====================================================
*/
FLOAT UXACTAudioSubsystem::GetCategoryVolume(EVolumeCategory category)
{
	guard(UXACTAudioSubsystem::GetCategoryVolume);
	return CategoryVolumes[category];
	unguard;
}

/* epic ===============================================
* ::SetCategoryVolume
*
* Sets the volume setting for the given category and
* caches it locally.
*
* =====================================================
*/
void UXACTAudioSubsystem::SetCategoryVolume(EVolumeCategory category, FLOAT newVolume)
{
	guard(UXACTAudioSubsystem::SetCategoryVolume);
	newVolume = Clamp(newVolume,0.f,1.f);
	CategoryVolumes[category] = newVolume;
	if ( category != VOLUME_VoiceChat )
	{
		XACTEngine->SetMasterVolume((WORD)category,CalcVolume(newVolume));
	}
	else
	{
		UXHVVoiceSubsystem* XHV = UXHVVoiceSubsystem::getUXHVVoiceSubsystem();
		if ( XHV && XHV->Initialized() )
			XHV->SetVolume(newVolume);
	}
	unguard;
}

#ifndef FINAL_RELEASE
UXACTAudioSubsystem::FXACTStats::FXACTStats()
{
	guard(FXACTStats::FXACTStats)
	appMemset( &STATS_FirstEntry, 0xFF, (DWORD) &STATS_LastEntry - (DWORD) &STATS_FirstEntry );
	unguard;
}

void UXACTAudioSubsystem::FXACTStats::Init()
{
	guard(FXACTStats::Init);

	// If already initialized retrieve indices from GStats.
	if( GStats.Registered[STATSTYPE_Audio] )
	{
		INT* Dummy = &STATS_3DSources;
		for( INT i=0; i<GStats.Stats[STATSTYPE_Audio].Num(); i++ )
			*(Dummy++) = GStats.Stats[STATSTYPE_Audio](i).Index;
		return;
	}

	// Register stats with GStat.
	STATS_3DSources				= GStats.RegisterStats( STATSTYPE_Audio, STATSDATATYPE_DWORD, TEXT("3D Sources"		), TEXT("Audio"		), STATSUNIT_Default	);
	STATS_2DSources				= GStats.RegisterStats( STATSTYPE_Audio, STATSDATATYPE_DWORD, TEXT("2D Sources"		), TEXT("Audio"		), STATSUNIT_Default	);
	STATS_Ambients				= GStats.RegisterStats( STATSTYPE_Audio, STATSDATATYPE_DWORD, TEXT("Active Ambients"), TEXT("Audio"		), STATSUNIT_Default	);
	STATS_3DUpdates				= GStats.RegisterStats( STATSTYPE_Audio, STATSDATATYPE_DWORD, TEXT("3D Updates"		), TEXT("Audio"		), STATSUNIT_Default	);
	STATS_Notifications			= GStats.RegisterStats( STATSTYPE_Audio, STATSDATATYPE_DWORD, TEXT("Notificatons"	), TEXT("Audio"		), STATSUNIT_Default	);
	STATS_UpdateCycles			= GStats.RegisterStats( STATSTYPE_Audio, STATSDATATYPE_DWORD, TEXT("Update"			), TEXT("Audio"		), STATSUNIT_MSec		);
	STATS_RegisterCycles		= GStats.RegisterStats( STATSTYPE_Audio, STATSDATATYPE_DWORD, TEXT("Register"		), TEXT("Audio"		), STATSUNIT_MSec		);
	
	// Initialized.
	GStats.Registered[STATSTYPE_Audio] = 1;

	unguard;
}
#endif
