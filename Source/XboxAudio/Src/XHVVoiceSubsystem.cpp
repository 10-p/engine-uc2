#include "XboxAudioPrivate.h"
#include "UnIpDrv.h"
#include "UnTcpNetDriverXbox.h"
#include "UnUIXInteraction.h"
#include "AvarisPlayerItem.h"
#include "XboxLive.h"
IMPLEMENT_CLASS(UXHVVoiceSubsystem);

#include "XboxDrv.h"
#include <dsstdfx.h>

extern FXboxGamepadState	Gamepads[NUM_GAMEPADS];

// voice masking
enum EVoiceMasks
{
	VM_Default		= 0,
	VM_Anonymous,
	VM_Cartoon,
	VM_BigGuy,
	VM_Child,
	VM_Robot,
	VM_DarkMaster,
	VM_Whisper,
	VM_Max
};

XHV_VOICE_MASK Masks[VM_Max] =
{
	// None
	{ XHV_VOICE_MASK_PARAM_DISABLED, XHV_VOICE_MASK_PARAM_DISABLED, XHV_VOICE_MASK_PARAM_DISABLED, XHV_VOICE_MASK_PARAM_DISABLED },
	// Anonymous
	{ 0.95f, 0.50f, 0.35f, XHV_VOICE_MASK_PARAM_DISABLED },
	// Cartoon
	{ 0.10f, XHV_VOICE_MASK_PARAM_DISABLED, XHV_VOICE_MASK_PARAM_DISABLED, XHV_VOICE_MASK_PARAM_DISABLED },
	// Big Guy
	{ 0.90f, 0.05f, XHV_VOICE_MASK_PARAM_DISABLED, XHV_VOICE_MASK_PARAM_DISABLED },
	// Child
	{ 0.10f, 0.70f, XHV_VOICE_MASK_PARAM_DISABLED, XHV_VOICE_MASK_PARAM_DISABLED },
	// Robot
	{ 0.50f, XHV_VOICE_MASK_PARAM_DISABLED, XHV_VOICE_MASK_PARAM_DISABLED, 0.05f                      },
	// Dark Master
	{ 1.00f, 0.00f, XHV_VOICE_MASK_PARAM_DISABLED, XHV_VOICE_MASK_PARAM_DISABLED },
	// Whisper
	{ 0.50f, XHV_VOICE_MASK_PARAM_DISABLED, 0.01f, XHV_VOICE_MASK_PARAM_DISABLED }
};

#ifdef FINAL_RELEASE
#define LOG_VOICE_NOTIFY_FAILURE(func,msg,type,src,dst)
#define LOG_VOICE_FAILURE(func,msg,xuid)
#define LOG_VOICE_BROADCASTNOTIFY_FAILURE(func,msg,type,src)
#else
#define SHOWCASE(x) case x: return TEXT(#x); break;

const TCHAR* GetNotifyTypeDescription( ENotifyType Type )
{
	switch (Type)
	{
		SHOWCASE(Notify_Register);
		SHOWCASE(Notify_UnRegister);
		SHOWCASE(Notify_CommStatus);
		SHOWCASE(Notify_Mute);
		SHOWCASE(Notify_UnMute);
	}

	return TEXT("");
}
#define LOG_VOICE_FAILURE(func,msg,xuid) debugf(NAME_DevVoice, TEXT("%s failed: %s (0x%016I64X)"), func, msg, xuid.qwUserID)
#define LOG_VOICE_NOTIFY_FAILURE(func,msg,type,src,dst) debugf(NAME_DevVoice, TEXT("%s failed: %s Type '%s' Source '0x%016I64X' Dest '0x%016I64X'"), func, GetNotifyTypeDescription(type), src.qwUserID, dst.qwUserID)
#define LOG_VOICE_BROADCASTNOTIFY_FAILURE(func,msg,type,src) debugf(NAME_DevVoice, TEXT("%s failed: %s Type '%s' Source '0x%016I64X'"), func, GetNotifyTypeDescription(type), src.qwUserID)
#endif

/* epic ===============================================
* GetLocalXUID
* Author: rayd
*
* Temporary function, uses the local IP as a fake XUID
* for purposes of identify talkers.	 Needs to check for
* XOnline registration and get the real XUID from UIX
* so as to properly support voice bans, mail, etc.
*
* =====================================================
*/
XUID GetLocalXUID()
{

	guard(GetLocalXUID);

	XUID newXUID;

	UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
	if ( UIX && UIX->SignedIn() )
	{
		newXUID = UIX->GetXUID();
	}
	else
	{
		XNADDR xna;
		XNetGetTitleXnAddr( &xna );
		DWORD ip;
		IpGetInt(xna.ina,ip);
		newXUID.qwUserID = ip;
		newXUID.dwUserFlags = 0;
	}
	return newXUID;

	unguard;
}

/* epic ===============================================
* ::IsServer
* Author: rayd
*
* Are we the central voice server?
*
* =====================================================
*/
UBOOL UXHVVoiceSubsystem::IsServer()
{
	guard(UXHVVoiceSubsystem::IsServer);
	UGameEngine* Game = Cast<UGameEngine>(UEngine::GEngine);
	if ( Game && Game->GLevel )
	{
		ALevelInfo* Info = Game->GLevel->GetLevelInfo();
		if ( Info )
			return Info->NetMode == NM_ListenServer;
	}

	return false;
	unguard;
}

/* epic ===============================================
* ::IsClient
* Author: rayd
*
* Are we a potential voice client?
*
* =====================================================
*/
UBOOL UXHVVoiceSubsystem::IsClient()
{
	guard(UXHVVoiceSubsystem::IsClient);
	UGameEngine* Game = Cast<UGameEngine>(UEngine::GEngine);
	if ( Game && Game->GLevel )
	{
		ALevelInfo* Info = Game->GLevel->GetLevelInfo();
		if ( Info )
			return Info->NetMode == NM_Client;
	}

	return false;
	unguard;
}

/* epic ====================================================================================================
* Function:	UXHVVoiceSubsystem::IsXboxLiveMatch
* Author:	Per Vognsen
*
* Checks whether the current game is an Xbox Live match.
* ==========================================================================================================
*/
UBOOL UXHVVoiceSubsystem::IsXboxLiveMatch()
{
	if (Viewport != NULL && Viewport->Actor != NULL && Viewport->Actor->GetLevel() != NULL)
	{
		UTcpNetDriverXbox* Driver = Cast<UTcpNetDriverXbox>(Viewport->Actor->GetLevel()->NetDriver);
		if (Driver != NULL)
		{
			return Driver->bUsingXboxLive;
		}
		else
		{
			return FALSE;
		}
	}
	else
	{
		return FALSE;
	}
}

UXHVVoiceSubsystem* UXHVVoiceSubsystem::theUXHVVoiceSubsystem = NULL;

UXHVVoiceSubsystem::UXHVVoiceSubsystem()
{
	guard(UXHVVoiceSubsystem::UXHVVoiceSubsystem);
	// scion sz - obj persists across level loads
	SetFlags( RF_TagPersist );
	theUXHVVoiceSubsystem = this;
	unguard;
}

void UXHVVoiceSubsystem::StaticConstructor()
{
	Viewport = NULL;
}

void UXHVVoiceSubsystem::Destroy()
{
	guard(UXHVVoiceSubsystem::Destroy);
	debugf(NAME_DevVoice, TEXT("UXHVVoiceSubsystem::Destroy()"));
	Super::Destroy();
	Shutdown();
	theUXHVVoiceSubsystem = NULL;
	unguard;
}



/* epic ===============================================
* ::Exec
* Author: rayd
*
* XHV specific execs:
*
* - REGISTER: debug registration
* - MUTE <name>: attempts to mute matching player
* - UNMUTE <name>: same as mute, but different!
* - SPEAKER: sets all voices through the speaker
* - HEADPHONE: sets all voices through the headphones
* - MASK <type>: sets the voice mask
* - SETVOLUME <volume>: sets the voice volume (must be between 0 and 1).
* =====================================================
*/
UBOOL UXHVVoiceSubsystem::Exec(const TCHAR* Cmd, FOutputDevice& Ar)
{
	guard(UXHVVoiceSubsystem::Exec);

	if (ParseCommand(&Cmd,TEXT("XHV")))
	{
		if (ParseCommand(&Cmd,TEXT("REGISTER")))
		{
			RegisterSelf();
		}
		else
		if (ParseCommand(&Cmd,TEXT("MUTE")))
		{
			// figure out the player being muted
			FString playerName;
			if (Parse(Cmd,TEXT("NAME="),playerName))
			{
				// grab the PRI for this player
				APlayerReplicationInfo *pri = MatchPRIToName(playerName);
				if (pri != NULL)
				{
					MuteTalker( *((XUID*)&pri->XUID) );
				}
			}
		}
		else
		if (ParseCommand(&Cmd,TEXT("UNMUTE")))
		{
			// figure out the player being muted
			FString playerName;
			if (Parse(Cmd,TEXT("NAME="),playerName))
			{
				// grab the PRI for this player
				APlayerReplicationInfo *pri = MatchPRIToName(playerName);
				if (pri != NULL)
				{
					// generate a fake xuid using the pri's qwUserID
					UnMuteTalker( *((XUID*)&pri->XUID) );
				}
			}
		}
		else
		if (ParseCommand(&Cmd, TEXT("MUTELIST")))
		{
			if (IsServer())
			{
				debugf(NAME_Log, TEXT("Mutelist:"));

				debugf(NAME_Log, TEXT("  0x%016I64X:"), GetLocalXUID().qwUserID);
				for (int MuteIdx = 0; MuteIdx < ServerMuteList.Num(); MuteIdx++)
				{
					debugf(NAME_Log, TEXT("    0x%016I64X"), ServerMuteList(MuteIdx));
				}

				UNetDriver* Driver = Viewport->Actor->GetLevel()->NetDriver;
				if (Driver != NULL)
				{
					for (INT ConnIdx = 0; ConnIdx < Driver->ClientConnections.Num(); ConnIdx++)
					{
						UTcpipConnectionXbox* Conn = Cast<UTcpipConnectionXbox>(Driver->ClientConnections(ConnIdx));

						XUID ConnXUID;
						appMemcpy(&ConnXUID, &Conn->Actor->PlayerReplicationInfo->XUID, sizeof(XUID));

						debugf(NAME_Log, TEXT("  0x%016I64X:"), ConnXUID.qwUserID);
						for (int MuteIdx = 0; MuteIdx < Conn->MuteList.Num(); MuteIdx++)
						{
							debugf(NAME_Log, TEXT("    0x%016I64X"), Conn->MuteList(MuteIdx));
						}
					}
				}
			}
			else
			{ 
				debugf(NAME_Log, TEXT("MUTELIST is only valid on the server."));
			}
		}
		else
		if (ParseCommand(&Cmd,TEXT("SPEAKER")))
		{
			SetOutputMode( COMM_Speaker );
		}
		else
		if (ParseCommand(&Cmd,TEXT("HEADPHONE")))
		{
			SetOutputMode(COMM_Headphone);
		}
		else
		if (ParseCommand(&Cmd,TEXT("MASK")) && XHVEngine)
		{
			// figure out what type of mask
			LocalTalkerPort = GetMicrophonePort();
			if (LocalTalkerPort != -1)
			{
				FString maskName;
				if (Parse(Cmd,TEXT("TYPE="),maskName))
				{
					XHV_VOICE_MASK mask = Masks[VM_Default];
					if (maskName == TEXT("anonymous"))
					{
						mask = Masks[VM_Anonymous];
					}
					else if (maskName == TEXT("cartoon"))
					{
						mask = Masks[VM_Cartoon];
					}
					else
					if (maskName == TEXT("bigguy"))
					{
						mask = Masks[VM_BigGuy];
					}
					else
					if (maskName == TEXT("child"))
					{
						mask = Masks[VM_Child];
					}
					else
					if (maskName == TEXT("robot"))
					{
						mask = Masks[VM_Robot];
					}
					else
					if (maskName == TEXT("darkmaster"))
					{
						mask = Masks[VM_DarkMaster];
					}
					else
					if (maskName == TEXT("whisper"))
					{
						mask = Masks[VM_Whisper];
					}
					XHVEngine->SetVoiceMask(LocalTalkerPort,&mask);
				}
			}
		}
		else if (ParseCommand(&Cmd, TEXT("SETVOLUME")))
		{
			FLOAT Volume = appAtof(Cmd);
			SetVolume(Volume);
		}

		return 1;
	}
	return 0;
	unguard;
}

/* epic ===============================================
* ::MatchPRIToName
* Author: rayd
*
* Finds the PRI that matches the given name.
*
* =====================================================
*/
APlayerReplicationInfo* UXHVVoiceSubsystem::MatchPRIToName(FString &playerName)
{
	guard(UXHVVoiceSubsystem::MatchPRIToXUID);

	// iterate through the pri array in the gameinfo
	UGameEngine* Game = Cast<UGameEngine>(UEngine::GEngine);
	if ( Game && Game->GLevel )
	{
		ALevelInfo* Info = Game->GLevel->GetLevelInfo();
		if (Info )
		{
			AGameReplicationInfo* gri = Info->GRI;
			if (gri != NULL)
			{
				for (INT idx = 0; idx < gri->PRIArray.Num(); idx++)
				{
					if (gri->PRIArray(idx)->PlayerName == playerName)
					{
						return gri->PRIArray(idx);
					}
				}
			}
		}
	}
	return NULL;
	unguard;
}

/* epic ===============================================
* ::MatchPRIToXUID
* Author: rayd
*
* Returns the playerreplicationinfo that matches the
* given XUID.
*
* =====================================================
*/
APlayerReplicationInfo* UXHVVoiceSubsystem::MatchPRIToXUID(const XUID& inPlayerID)
{
	guard(UXHVVoiceSubsystem::MatchPRIToXUID);
	UGameEngine* Game = Cast<UGameEngine>(UEngine::GEngine);
	if ( Game && Game->GLevel )
	{
		ALevelInfo* Info = Game->GLevel->GetLevelInfo();
		if (Info )
		{
			AGameReplicationInfo* gri = Info->GRI;
			if (gri != NULL)
			{
				for (INT idx = 0; idx < gri->PRIArray.Num(); idx++)
				{
					if ( !appMemcmp(&gri->PRIArray(idx)->XUID, &inPlayerID, sizeof(XUID)) )
					{
						return gri->PRIArray(idx);
					}
				}
			}
		}
	}
	return NULL;
	unguard;
}

/* epic ===============================================
* ::GetMicrophonePort
* Author: rayd
*
* Figures out which port has a microphone connected.
*
* =====================================================
*/
DWORD UXHVVoiceSubsystem::GetMicrophonePort()
{
	guard(UXHVVoiceSubsystem::GetMicrophonePort);
	DWORD dwMicrophoneState = XGetDevices( XDEVICE_TYPE_VOICE_MICROPHONE );
	DWORD dwHeadphoneState = XGetDevices( XDEVICE_TYPE_VOICE_HEADPHONE );

	// if our associated viewport has a valid gamepad ID, we only care about the
	// communicator state of the gamepad for viewport.
	if ( Viewport != NULL )
	{
		if ( Viewport->GamepadID != UViewport::INVALID_GAMEPAD_ID )
		{
			// Check whether the microphone is plugged in the active gamepad.
			if ( (dwMicrophoneState & (1 << Viewport->GamepadID)) &&
				(dwHeadphoneState & (1 << Viewport->GamepadID)) )
				return Viewport->GamepadID;
		}
	}

	// no port has a microphone, return invalid index
	return -1;
	unguard;
}

/* epic ====================================================================================================
* Function:	UXHVVoiceSubsystem::IsActivePort
* Author:	Per Vognsen
*
* Checks whether a given port corresponds to the active gamepad.
* ==========================================================================================================
*/
UBOOL UXHVVoiceSubsystem::IsActivePort(INT Port)
{
	guard(UXHVVoiceSubsystem::IsActivePort);

	if (Viewport != NULL)
	{
		return (Viewport->GetGamepadID() == Port);
	}
	else
	{
		// There is no viewport and hence no active gamepad, so return false.
		return FALSE;
	}

	unguard;
}

/* epic ===============================================
* ::SetViewport
* Author: rayd
*
* Called once we receive a new viewport, use to register
* talkers in a mp game.
*
* =====================================================
*/
void UXHVVoiceSubsystem::SetViewport(UViewport *inViewport)
{
	guard(UXHVVoiceSubsystem::SetViewport);
	Viewport = inViewport;

	// if we are a server/client, auto-register
	if (Viewport != NULL)
	{
		LocalTalkerPort = GetMicrophonePort();
		if (IsServer() || IsClient())
		{
			debugf(NAME_DevVoice,TEXT("Received new client/server viewport, requesting registration"));
			bPendingRequest = 1;
		}
	}
	else LocalTalkerPort = -1;

	unguard;
}

/* epic ===============================================
* ::Init
* Author: rayd
*
* Initializes XHV, returns 1 for success, 0 for failure.
*
* =====================================================
*/
UBOOL UXHVVoiceSubsystem::Init(UAudioSubsystem *audio)
{
	guard(UXHVVoiceSubsystem::Init);

	// scion sz - don't initialize if invalid audio device
	UXACTAudioSubsystem* pXboxAudio = Cast<UXACTAudioSubsystem>( audio );
	if( !pXboxAudio )
		return 0;

	UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
	UBOOL bSignedIn = UIX != NULL ? UIX->SignedIn() : false;

	if (!bInitialized && bSignedIn)
	{
		bInitialized = 0;
		XHVEngine = NULL;
		check(pXboxAudio != NULL && "Audio must be initialized before voice!");
		// create the XHV engine
		XHV_RUNTIME_PARAMS xhvParms;
		xhvParms.dwMaxRemoteTalkers = 7;		// max players == 8
		xhvParms.dwMaxLocalTalkers = 4;			// max split screen players == 2, max online == 1
		xhvParms.dwMaxCompressedBuffers = 10;	// this caps at 8
		xhvParms.pEffectImageDesc = pXboxAudio->DSPImage;
		xhvParms.dwEffectsStartIndex = GraphVoice_Voice_0;
		xhvParms.dwFlags = 0;					// must be zero
		xhvParms.bCustomVADProvided = 0;		// allow default VAD support
		xhvParms.bHeadphoneAlwaysOn = 0;		// no speech recognition, so this option isn't needed
		xhvParms.dwOutOfSyncThreshold = 20;		
		if (SUCCEEDED(XHVEngineCreate(&xhvParms,&XHVEngine)))
		{
			bInitialized = 1;
			// enable processing modes
			//XHVEngine->EnableProcessingMode(XHV_LOOPBACK_MODE);		// for debugging, loopback locally, eats 4 voices (EVIL)
			XHVEngine->EnableProcessingMode(XHV_VOICECHAT_MODE);	// normal voice chat
			XHVEngine->EnableProcessingMode(XHV_VOICEMAIL_MODE); // voice mail support
			// set the callback interface to this
			XHVEngine->SetCallbackInterface(this);
			// set the max # of playback streams (7 remote talkers)
			XHVEngine->SetMaxPlaybackStreamsCount(7);

			debugf(NAME_DevVoice,TEXT("XHV subsystem initialized"));

			// Notify UIX so that support for voice attachments can be enabled.
			if (UIX != NULL)
			{
				INT ActiveGamepadPort = UIX->getGamepadPort();
				if (ActiveGamepadPort != -1)
				{
					if (SUCCEEDED(XHVEngine->RegisterLocalTalker(ActiveGamepadPort)))
					{
						// default to being inactive as to not waste cpu cycles
						XHVEngine->SetProcessingMode(ActiveGamepadPort, XHV_INACTIVE_MODE);
					}
				}

				UIX->InitUIXVoiceSupport();
			}
		}
		else
		{
			debugf(NAME_DevVoice,TEXT("XHV subsystem initialization skipped -- could not create XHV engine."));
		}
	}
	else
	{
		if (!bInitialized)
		{
			debugf(NAME_DevVoice,TEXT("XHV subsystem initialization skipped -- not signed into Live."));
		}
		else
		{
			debugf(NAME_DevVoice,TEXT("XHV subsystem initialization skipped -- already initialized."));
		}
	}
	return bInitialized;
	unguard;
}

/*==========================================================================================================
function	UXHVVoiceSubsystem::Shutdown
Desc:		Called when the user logs off.  Cleans up and releases the XHVEngine.

return:		Only returns false if we're initialized, but don't have a valid XHVEngine reference.

last modified by:	Ron Prestenback
==========================================================================================================*/
UBOOL UXHVVoiceSubsystem::Shutdown()
{
	guard(UXHVVoiceSubsystem::Shutdown);

	// if we weren't initialized to begin with, just return success
	if ( !bInitialized )
		return true;

	// if we don't have a valid handle to an instance of the XHV engine, return false
	if ( XHVEngine == NULL )
		return false;

	// no longer initialized
	bInitialized = false;

	// iterate through each talker,
	for ( INT idx = Voices.Num() - 1; idx >= 0; idx-- )
	{
		// and unregister them
		UnRegisterTalker(Voices(idx).PlayerID);
		// UnRegisterTalker removes from Voices array
	}

	XHVEngine->Release();
	XHVEngine = NULL;
	return true;
	unguard;
}

/* epic ===============================================
* ::NotifyLevelChange
* Author: rayd
*
* Called on level switch, force unregister of all talkers.
*
* =====================================================
*/
void UXHVVoiceSubsystem::NotifyLevelChange()
{
	guard(UXHVVoiceSubsystem::NotifyLevelChange);

	// iterate through each talker,
	for ( INT idx = Voices.Num() - 1; idx >= 0; idx-- )
	{
		// and unregister them
		UnRegisterTalker(Voices(idx).PlayerID);
		// UnRegisterTalker removes from Voices array
	}

	Voices.Empty();
	unguard;
}

/* epic ===============================================
* ::Update
* Author: rayd
*
* Handles processing of voice packets.
*
* =====================================================
*/
void UXHVVoiceSubsystem::Update()
{
	guard(UXHVVoiceSubsystem::Update);
	if (bInitialized && Viewport != NULL)
	{
		APlayerController* PC = Viewport->Actor;
		if (PC != NULL)
		{
			UBOOL bLoading = PC->Level->Role < ROLE_Authority && PC->Role == ROLE_Authority;	
			if (!bLoading)
			{
				// let the XHV engine process
				XHVEngine->DoWork();

				// check if we're waiting to request registration
				if (bPendingRequest && IsXboxLiveMatch())
				{
					XUID PlayerXUID = GetLocalXUID();
					UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();

					// don't perform voice registration until we've been properly registered with UIX
					if ( UIX && UIX->FindPlayerItem(&PlayerXUID) != NULL )
					{
						if ( RegisterSelf() )
						{
							bPendingRequest = 0;
						}
					}
				}
			}
		}
	}
	unguard;
}

/* epic ===============================================
* ::CommunicatorStatusUpdate
* Author: rayd
*
* Called when a microphone/headset status changes.
*
* note: does not support multiple signed-in users
* =====================================================
*/
HRESULT UXHVVoiceSubsystem::CommunicatorStatusUpdate(DWORD dwPort, XHV_VOICE_COMMUNICATOR_STATUS status)
{
	guard(UXHVVoiceSubsystem::CommunicatorStatusUpdate);

	// Only process status updates concerning the active gamepad.
	if (!IsActivePort(dwPort))
	{
		return S_OK;
	}

	UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
	if (bInitialized && UIX != NULL && Viewport != NULL && Viewport->Profile != NULL)
	{
		bVoiceThroughSpeakers = Viewport->Profile->bVoiceOverSpeakers;
		if (bVoiceThroughSpeakers || status == XHV_VOICE_COMMUNICATOR_STATUS_REMOVED)
		{
			UIX->SetVoiceMailMode(TRUE);
		}
		else
		{
			UIX->SetVoiceMailMode(FALSE);
		}
	}

	if (bInitialized &&
		Viewport != NULL &&
		IsXboxLiveMatch() &&
		(IsServer() || IsClient()))
	{
		debugf(NAME_DevVoice,TEXT("Communicator status updated for port %u  Voices:%i"),dwPort, Voices.Num());
		// If a communicator was inserted, register ourself as a talker.
		if (status == XHV_VOICE_COMMUNICATOR_STATUS_INSERTED)
		{
			debugf(NAME_DevVoice,TEXT("- Communicator inserted on port %d"), dwPort);

			// Invariant: The communicator was plugged into the active gamepad, so LocalTalkerPort should be -1.
			if (LocalTalkerPort == -1)
			{
				// Set the local talker port to the port of the active gamepad.
				LocalTalkerPort = GetMicrophonePort();

				debugf(NAME_DevVoice,TEXT("- enabled voice chat on port %d"),LocalTalkerPort);

				INT maskIdx = 0;
				if ( Viewport && Viewport->Profile )
				{
					maskIdx = Viewport->Profile->VoiceMaskingType;
					bVoiceThroughSpeakers = Viewport->Profile->bVoiceOverSpeakers;
				}

				XHVEngine->SetVoiceMask(LocalTalkerPort,&Masks[maskIdx]);
				XHVEngine->SetProcessingMode(LocalTalkerPort,XHV_VOICECHAT_MODE);

				if (!RegisterSelf())
				{
					SetOutputMode(bVoiceThroughSpeakers);
				}
			}
		}
		// If the communicator was removed, unregister ourself as talker.
		else
		{
			debugf(NAME_DevVoice,TEXT("- Communicator removed"));

			// Invariant: Since the communicator was removed, it must have been previously plugged in.
			// Since (i) LocalTalkerPort should always either be -1 or the port of the active gamepad
			// and (ii) we previously checked via IsActivePort() that the status update concerned the
			// active gamepad, we must have LocalTalkerPort == dwPort.
			if (LocalTalkerPort == dwPort)
			{
				// set the talker to inactive
				XHVEngine->SetProcessingMode(LocalTalkerPort,XHV_INACTIVE_MODE);

				// Set the LocalTalkerPort to -1 to indicate that no communicator is plugged into the active gamepad.
				LocalTalkerPort = -1;
				UnregisterSelf();
			}
		}
	}
	return S_OK;
	unguard;
}

/* epic ===============================================
* ::LocalChatDataReady
* Author: rayd
*					
* Called when we have a packet ready from the microphone.
*
* =====================================================
*/
HRESULT UXHVVoiceSubsystem::LocalChatDataReady(DWORD dwLocalPort, DWORD dwSize, void *pData)
{
	guard(UXHVVoiceSubsystem::LocalChatDataReady);
	if (pData != NULL &&
		dwSize > 0)
	{
		APlayerController* PC = Viewport->Actor;
		if (PC != NULL)
		{
			UBOOL bLoading = PC->Level->Role < ROLE_Authority && PC->Role == ROLE_Authority;	
			if (!bLoading)
			{
				if (bInitialized &&
					Viewport != NULL && Viewport->Actor != NULL && Viewport->Actor->GetLevel() != NULL &&
					IsXboxLiveMatch())
				{
					// Check if user is voice banned.
					if (!XOnlineIsUserVoiceAllowed(GetLocalXUID().dwUserFlags))
					{
						return S_OK;
					}

					// if we are the server, broadcast to all clients
					if (IsServer())
					{
						UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();

						UNetDriver* Driver = Viewport->Actor->GetLevel()->NetDriver;
						if (Driver != NULL)
						{
							for (INT ConnIdx = 0; ConnIdx < Driver->ClientConnections.Num(); ConnIdx++)
							{
								UTcpipConnectionXbox* Conn = Cast<UTcpipConnectionXbox>(Driver->ClientConnections(ConnIdx));
								AvarisPlayerItem* PlayerItem = UIX->FindPlayerItem(Conn);
								if (PlayerItem != NULL)
								{
									const XUID* ConnXUID = PlayerItem->GetXUID();
									if (Conn != NULL && !IsMuted(GetLocalXUID(), *ConnXUID) && UIX->ShouldSendTo(GetLocalXUID(), *ConnXUID))
									{
										INT PacketIdx = Conn->VoicePackets.AddZeroed();
										Conn->VoicePackets(PacketIdx).InitPacket(GetLocalXUID(),pData,dwSize);
									}
								}
							}
						}
					}
					else if (IsClient())
					{
						// package the voice and transmit to the server
						UTcpipConnectionXbox *server = Cast<UTcpipConnectionXbox>(Viewport->GetServerConnection());
						if (server != NULL)
						{
							// build directly into the voice packet queue for our server connection
							INT idx = server->VoicePackets.AddZeroed();
							server->VoicePackets(idx).InitPacket(GetLocalXUID(),pData,dwSize);
						}
					}
				}
			}
		}
	}
	return S_OK;
	unguard;
}

/* epic ===============================================
* ::RegisterTalker
* Author: rayd
*
* Registers a talker for processing voice.
*
* NOTE: this should only be called from the server, all
*       registrations should be requested
* =====================================================
*/
UBOOL UXHVVoiceSubsystem::RegisterTalker(const XUID& inPlayerID,UNetConnection *inPlayer)
{
	guard(UXHVVoiceSubsystem::RegisterTalker);
	if (Viewport != NULL)
	{
		debugf(NAME_DevVoice,TEXT("Registering talker with XUID (0x%016I64X - 0x%08X)"),inPlayerID.qwUserID, inPlayerID.dwUserFlags);

		// only register a player once
		INT idx = PlayerVoiceInfoNdx(inPlayerID);
		if ( idx == -1 )
		{
			// register remote talker with XHV
			XHVEngine->RegisterRemoteTalker(inPlayerID);

			// set the playback mode for this user
			ToggleVoiceOutput(inPlayerID,bVoiceThroughSpeakers);

			// add to voices array
			idx = Voices.AddZeroed();
			Voices(idx).Player = inPlayer;
			Voices(idx).PlayerID = inPlayerID;

			return 1;
		}
		else
		{
			debugf(NAME_DevVoice,TEXT("- already registered"));
		}
	}
	return 0;
	unguard;
}

/* epic ===============================================
* ::UnRegisterTalker
* Author: rayd
*
* Unregisters a talker, remote or not.
*
* =====================================================
*/
void UXHVVoiceSubsystem::UnRegisterTalker(const XUID& inPlayerID)
{
	guard(UXHVVoiceSubsystem::UnRegisterTalker);
	if (Viewport != NULL)
	{
		debugf(NAME_DevVoice,TEXT("Unregistering talker with XUID %u"),inPlayerID.qwUserID);

		// find matching entry in Voices array
		INT idx = PlayerVoiceInfoNdx(inPlayerID);
		if ( idx != -1 )
		{
			// notify XHV
			if (XHVEngine != NULL)
			{
				XHVEngine->UnregisterRemoteTalker(inPlayerID);
			}

			// remove the entry
			Voices.Remove(idx,1);
		}
	}
	unguard;
}

/*==========================================================================================================
function	UXHVVoiceSubsystem::RegisterSelf
Desc:		Attempts to register ourselves as a talker.  If we're a client, send a registration request to
the server and wait for the response packet in order to be registered.  If we're the server,
register locally, then call BroadcastRegisterTalker to propagate the registration to all clients.

notes:		The new talker also receives registration of all the existing clients to ensure late joins are
in synch.
last modified by:	Ron Prestenback
==========================================================================================================*/
UBOOL UXHVVoiceSubsystem::RegisterSelf()
{
	guard(UXHVVoiceSubsystem::RegisterSelf);

	if ( bInitialized && Viewport != NULL)
	{
		// we must have a communicator inserted in order to register ourselves
		if ( LocalTalkerPort != -1 )
		{
			XUID inPlayerID = GetLocalXUID();

			if ( IsServer() )
			{
				// if we're not already registered, register locally
				if ( RegisterTalker(inPlayerID) )
				{
					XHVEngine->SetProcessingMode(LocalTalkerPort,XHV_VOICECHAT_MODE);

					// then broadcast the registration to all connected clients
					BroadcastRegisterTalker(inPlayerID);
				}
				else
				{
					return 0;
				}
			}
			else if (IsClient())
			{
				// otherwise form the registration packet and send to server
				UTcpipConnectionXbox *server = Cast<UTcpipConnectionXbox>(Viewport->GetServerConnection());

				// if we're connected to the server and we aren't registered already
				if ( server != NULL && RegisterTalker(inPlayerID) )
				{
					XHVEngine->SetProcessingMode(LocalTalkerPort,XHV_VOICECHAT_MODE);

					BYTE EmptyData[MAX_VDATA_SIZE] = {0};
					BroadcastVoiceNotification(Notify_Register, inPlayerID, EmptyData);
				}
				else
				{
					return 0;
				}
			}

			XboxLiveManager* Manager = XboxLiveManager::getXboxLiveManager();
			if (Manager != NULL)
			{
				Manager->StartMuteListQuery();
			}
		}

		SetOutputMode(bVoiceThroughSpeakers);
		return 1;
	}
	return 0;

	unguard;
}

/*==========================================================================================================
function	UXHVVoiceSubsystem::UnregisterSelf
Desc:		Unregisters the local player as a talker.  If we're the server, also broadcast the
unregistration to all clients.

notes:		Unlike RegisterSelf(), clients locally unregister themselves immediately rather than wait for
a confirmation packet from the server.
last modified by:	Ron Prestenback
==========================================================================================================*/
void UXHVVoiceSubsystem::UnregisterSelf()
{
	guard(UXHVVoiceSubsystem::UnregisterSelf);

	if (Viewport != NULL)
	{
		XUID inPlayerID = GetLocalXUID();

		if ( LocalTalkerPort != -1 )
			XHVEngine->SetProcessingMode(LocalTalkerPort, XHV_INACTIVE_MODE);

		// when our commicator is unplugged, remove the speaker/headphone icon
		// from everyone else's player list
		SetOutputMode(COMM_None);

		// unregister locally, jic
		UnRegisterTalker(inPlayerID);

		// if we are the server, unregister locally
		if (IsServer())
		{
			// broadcast the unregistration
			BroadcastUnRegisterTalker(inPlayerID);
		}
		else if (IsClient())
		{
			BYTE EmptyData[MAX_VDATA_SIZE] = {0};
			BroadcastVoiceNotification(Notify_UnRegister, inPlayerID, EmptyData);
		}
	}

	unguard;
}

/*==========================================================================================================
function	UXHVVoiceSubsystem::ReceivedNewPlayer
Desc:		Called by UUIXInteraction when a new client registration is received.  Sends the communicator
status of all existing players to the new player.

input:		inXUID
XUID for the player that has just connected.

notes:		This function should only ever be called on the server.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UXHVVoiceSubsystem::ReceivedNewPlayer( const XUID& inXUID ) 
{
	guard(UXHVVoiceSubsystem::ReceivedNewPlayer);

	verify(IsServer() && "ReceivedNewPlayer should never be called from clients!");

	// first, find the PRI associated with this xuid
	APlayerReplicationInfo* PRI = MatchPRIToXUID(inXUID);
	if ( PRI == NULL )
	{
		debugf(NAME_DevVoice, TEXT("UXHVVoiceSubsystem::ReceivedNewPlayer - no matching PRI found for player 0x%016I64X"), inXUID.qwUserID);
		return;
	}

	// next, find the net connection associated with this PRI
	UTcpipConnectionXbox* NewConnection = NULL;
	if ( PRI->Owner && PRI->Owner->IsAPlayerController() )
	{
		APlayerController* PC = (APlayerController*)PRI->Owner;
		NewConnection = Cast<UTcpipConnectionXbox>(PC->Player);
	}

	if ( NewConnection == NULL )
	{
		debugf(NAME_DevVoice, TEXT("UXHVVoiceSubsystem::ReceivedNewPlayer - no matching net connection found for player 0x%016I64X"), inXUID.qwUserID);
		return;
	}

	if ( bInitialized && Viewport != NULL && Viewport->Actor != NULL && IsXboxLiveMatch())
	{
		ULevel* Level = Viewport->Actor->GetLevel();
		if (Level != NULL)
		{
			check(Level->NetDriver != NULL && "Invalid net driver for server?");
			UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();

			BYTE EmptyData[MAX_VDATA_SIZE] = {0};

			// Send this new player the list of currently registered talkers.
			for (INT VoiceIdx = 0; VoiceIdx < Voices.Num(); VoiceIdx++)
			{
				FVoiceInfo& Voice = Voices(VoiceIdx);
				if (Voice.Player != NewConnection)
				{
					SendVoiceNotification(Notify_Register, Voice.PlayerID, inXUID, EmptyData);
				}
			}

			// first, send the communicator status for the server to this new client
			AvarisPlayerItem* Player = UIX->FindPlayerItem(Viewport->Actor->PlayerReplicationInfo);
			if ( Player )
			{
				BYTE Data[MAX_VDATA_SIZE] = {0};
				Data[0] = Player->GetVoiceStatus();
				SendVoiceNotification(Notify_CommStatus, GetLocalXUID(), inXUID, Data);
			}

			Player = UIX->FindPlayerItem(NewConnection);
			BYTE NewData[MAX_VDATA_SIZE] = {0};
			if ( Player )
			{
				NewData[0] = Player->GetVoiceStatus();
				if (NewData[0] == 255)
				{
					LOG_VOICE_FAILURE(TEXT("ReceivedNewPlayer"), TEXT("Voice status for NewConnection is still 255."), inXUID);
				}
			}
			else
			{
				LOG_VOICE_FAILURE(TEXT("ReceivedNewPlayer"), TEXT("NewConnection not found in Player List."), inXUID);
			}

			// go through the list of connected players,
			for ( INT i = 0; i < Level->NetDriver->ClientConnections.Num(); i++ )
			{
				UTcpipConnectionXbox* Connection = Cast<UTcpipConnectionXbox>(Level->NetDriver->ClientConnections(i));

				// if this connection isn't the same connection as the new player
				if ( Connection != NULL && Connection != NewConnection )
				{
					// find the registered UIX player list item associated with this connection
					Player = UIX->FindPlayerItem(Connection);
					if ( Player )
					{
						BYTE Data[MAX_VDATA_SIZE] = {0};
						// get the current communicator status for this existing player
						Data[0] = Player->GetVoiceStatus();
						if (Data[0] != 255)
						{

							// whew...finally....
							// send the communicator status for this existing player to the newly connected player
							const XUID* PlayerXUID = Player->GetXUID();
							SendVoiceNotification(Notify_CommStatus, *PlayerXUID, inXUID, Data);
							SendVoiceNotification(Notify_CommStatus, inXUID, *PlayerXUID, NewData);
						}
						else
						{

							debugf(NAME_DevVoice, TEXT("UXHVVoiceSubsystem::ReceivedNewPlayer - existing player connection isn't yet registered with UIX: 0x%016I64X"), Player->GetXUID()->qwUserID);
						}
					}
				}
			}
		}
	}

	unguard;
}

/* epic ===============================================
* ::BroadcastRegisterTalker
* Author: rayd
*
* Server function to form and send registration packet to
* all clients.
*
* =====================================================
*/
void UXHVVoiceSubsystem::BroadcastRegisterTalker(const XUID& inPlayerID,UNetConnection *srcConn)
{
	guard(UXHVVoiceSubsystem::BroadcastRegisterTalker);
	check(IsServer() && "Broadcasts are restricted to the server");
	if (Viewport != NULL && Viewport->Actor != NULL)
	{
		debugf(NAME_DevVoice,TEXT("Server broadcast talker registration for (0x%016I64X - 0x%08X)"),inPlayerID.qwUserID, inPlayerID.dwUserFlags);
		// for each connection,
		if ( Viewport->Actor->GetLevel() )
		{
			UNetDriver *driver = Viewport->Actor->GetLevel()->NetDriver;
			check(driver != NULL && "Invalid net driver for server?");

			BYTE EmptyData[MAX_VDATA_SIZE] = {0};
			BroadcastVoiceNotification(Notify_Register, inPlayerID, EmptyData);
		}
	}
	unguard;
}

/* epic ===============================================
* ::BroadcastUnRegisterTalker
* Author: rayd
*
* Server function to form and send unregistration packet
* to all clients.
*
* =====================================================
*/
void UXHVVoiceSubsystem::BroadcastUnRegisterTalker(const XUID& inPlayerID)
{
	guard(UXHVVoiceSubsystem::BroadcastUnRegisterTalker);
	if (Viewport != NULL && Viewport->Actor != NULL && Viewport->Actor->GetLevel() != NULL)
	{
		debugf(NAME_DevVoice,TEXT("Server broadcast talker unregistration for (0x%016I64X - 0x%08X)"),inPlayerID.qwUserID, inPlayerID.dwUserFlags);
		// for each connection,
		UNetDriver* Driver = Viewport->Actor->GetLevel()->NetDriver;
		if (Driver != NULL)
		{
			BYTE EmptyData[MAX_VDATA_SIZE] = {0};
			BroadcastVoiceNotification(Notify_UnRegister, inPlayerID, EmptyData);
		}
	}
	unguard;
}

/* epic ===============================================
* ::ReceiveVoicePacket
* Author: rayd
*
* Pipes an incoming voice packet to the XHV engine for
* playback.  If we are the server it routes it back to
* all other clients.
*
* =====================================================
*/
void UXHVVoiceSubsystem::ReceiveVoicePacket(const XUID& inXUID,void *Data, _WORD DataSize)
{
	guard(UXHVVoiceSubsystem::ReceiveVoicePacket);
	if (bInitialized)
	{
		// Check if user is voice banned.
		if (!XOnlineIsUserVoiceAllowed(GetLocalXUID().dwUserFlags))
		{
			return;
		}

		// submit it locally
		INT VoiceIdx = PlayerVoiceInfoNdx(inXUID);
		if (Voices.IsValidIndex(VoiceIdx))
		{
			XHVEngine->SubmitIncomingVoicePacket(inXUID,Data,DataSize);
		}
	}
	unguard;
}

/* epic ===============================================
* ::HandleIncomingVoicePacket
* Author: rayd
*
* Called from net connection when we receive a new
* voice packet, to be parsed and dealt with.
*
* =====================================================
*/
void UXHVVoiceSubsystem::HandleIncomingVoicePacket(UNetConnection *inPlayer,FVoicePacket &inPkt)
{
	guard(UXHVVoiceSubsystem::HandleIncomingVoicePacket);
	if (inPlayer != NULL && Viewport != NULL && bInitialized && IsXboxLiveMatch())
	{
		UNetDriver *driver = Viewport->Actor->GetLevel()->NetDriver;
		// pass it to XHV
		if (inPkt.DataSize > 0)
		{
			UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();

			// if we are the server, then broadcast to all other clients
			if (IsServer())
			{
				for (INT ConnIdx = 0; ConnIdx < driver->ClientConnections.Num(); ConnIdx++)
				{
					UTcpipConnectionXbox* Conn = Cast<UTcpipConnectionXbox>(driver->ClientConnections(ConnIdx));
					AvarisPlayerItem* PlayerItem = UIX->FindPlayerItem(Conn);
					if (PlayerItem != NULL)
					{
						const XUID* ConnXUID = PlayerItem->GetXUID();
						if (Conn != NULL && inPlayer != Conn && !IsMuted(inPkt.SrcID, *ConnXUID) && UIX->ShouldSendTo(inPkt.SrcID, *ConnXUID))
						{
							INT PacketIdx = Conn->VoicePackets.AddZeroed();
							Conn->VoicePackets(PacketIdx).InitPacket(inPkt.SrcID,inPkt.Data,inPkt.DataSize);
						}
					}
				}
			}
			// play the packet locally
			if (!IsServer() || (UIX->ShouldSendTo(inPkt.SrcID, GetLocalXUID()) && !IsMuted(inPkt.SrcID, GetLocalXUID())))
			{
				ReceiveVoicePacket(inPkt.SrcID,inPkt.Data,inPkt.DataSize);
			}
		}
	}
	unguard;
}

/* epic ====================================================================================================
* Function:	UXHVVoiceSubsystem::HandleVoiceNotification
* Author:	Per Vognsen
*
* Handles voice notifications, including registrations, unregistrations and communication status updates.
*
* Parameters:
* NotifyType		The kind of notification, indicating whether it's e.g. a registration.
* SrcConnection		The network connection of the sender. Only valid on server.
* SrcXUID			The XUID of the sender.
* Data				Data specific to the type of notification.
* ==========================================================================================================
*/
void UXHVVoiceSubsystem::HandleVoiceNotification(ENotifyType NotifyType, UTcpipConnectionXbox* SrcConnection, XUID SrcXUID, BYTE Data[MAX_VDATA_SIZE])
{
	guard(UXHVVoiceSubsystem::HandleVoiceNotification);

	// Voice chatting is only available in Xbox Live matches.
	if (!IsXboxLiveMatch())
	{
		return;
	}

	switch(NotifyType)
	{
	case Notify_Register:
		if (IsServer())
		{
			// if this is a new registration
			if (RegisterTalker(SrcXUID, SrcConnection))
			{
				// let all existing clients about this new registration
				BroadcastRegisterTalker(SrcXUID, SrcConnection);
			}
		}
		else if (IsClient())
		{
			if (SrcXUID.qwUserID != GetLocalXUID().qwUserID)
			{
				// register the new talker
				RegisterTalker(SrcXUID);
			}
		}
		break;
	case Notify_UnRegister:
		// unregister is same as register essentially
		if (SrcXUID.qwUserID != GetLocalXUID().qwUserID)
		{
			UnRegisterTalker(SrcXUID);
		}
		// if we are the server then we need to broadcast
		if (IsServer())
		{
			BroadcastUnRegisterTalker(SrcXUID);
		}
		break;

	case Notify_CommStatus:
		// if we are the server, then broadcast to all other clients
		if (IsServer())
		{
			BroadcastVoiceNotification(Notify_CommStatus, SrcXUID, Data);
		}

		if (SrcXUID.qwUserID != GetLocalXUID().qwUserID)
		{
			UpdateCommStatus(SrcXUID, Data[0]);
		}
		break;

	case Notify_Mute:
		if (IsServer())
		{
			XUID MuterXUID = SrcXUID;
			XUID MuteeXUID = *(XUID*) Data;

			ServerMuteTalker(SrcConnection, MuterXUID, MuteeXUID);
		}
		else if (IsClient())
		{
			debugf(NAME_DevLive, TEXT("Error: VPT_Mute packet received on client. Should only ever be received on server."));
		}
		break;

	case Notify_UnMute:
		if (IsServer())
		{
			XUID UnMuterXUID = SrcXUID;
			XUID UnMuteeXUID = *(XUID*) Data;

			ServerUnMuteTalker(SrcConnection, UnMuterXUID, UnMuteeXUID);
		}
		else if (IsClient())
		{
			debugf(NAME_DevLive, TEXT("Error: VPT_UnMute packet received on client. Should only ever be received on server."));
		}
		break;

	default:
		debugf(NAME_Error,TEXT("XHV: unknown voice packet type %u"), NotifyType);
		break;
	}

	unguard;
}

/* epic ====================================================================================================
* Function:	UXHVVoiceSubsystem::SendVoiceNotification
* Author:	Per Vognsen
*
* Sends a voice notification to a specific player.
*
* Parameters:
* NotifyType		The kind of notification, indicating whether it's e.g. a registration.
* SrcXUID			The XUID of the sender.
* DestXUID			The XUID of the desired destination.
* Data				Data specific to the type of notification.
* ==========================================================================================================
*/
void UXHVVoiceSubsystem::SendVoiceNotification(ENotifyType NotifyType, const XUID& SrcXUID, const XUID& DestXUID, const BYTE Data[MAX_VDATA_SIZE])
{
	guard(UXHVVoiceSubsystem::SendVoiceNotification);

	if (Viewport == NULL || Viewport->Actor == NULL)
	{
		if ( Viewport == NULL )
			LOG_VOICE_NOTIFY_FAILURE(__FUNC_NAME__, TEXT("NULL Viewport"), NotifyType, SrcXUID, DestXUID);
		else LOG_VOICE_NOTIFY_FAILURE(__FUNC_NAME__, TEXT("NULL Viewport->Actor"), NotifyType, SrcXUID, DestXUID);
		return;
	}
	if (Viewport->Actor->GetLevel() == NULL || Viewport->Actor->GetLevel()->GetLevelInfo() == NULL)
	{
		if ( Viewport->Actor->GetLevel() == NULL )
            LOG_VOICE_NOTIFY_FAILURE(__FUNC_NAME__, TEXT("NULL Level"), NotifyType, SrcXUID, DestXUID);
		else LOG_VOICE_NOTIFY_FAILURE(__FUNC_NAME__, TEXT("NULL LevelInfo"), NotifyType, SrcXUID, DestXUID);

		return;
	}

	if (DestXUID.qwUserID == 0)
	{
		LOG_VOICE_NOTIFY_FAILURE(__FUNC_NAME__, TEXT("NULL Dest"), NotifyType, SrcXUID, DestXUID);
		return;
	}

	// Unpack the bytes of the source XUID for replication.
	BYTE* SrcXUIDBytes = (BYTE*) &SrcXUID;
	FString SrcXUIDStr;
	BytesToString(SrcXUIDBytes, 12, SrcXUIDStr);

	// Unpack the data into individual bytes for replication.
	FString DataStr;
	BytesToString(Data, 12, DataStr);

	if (IsServer())
	{
		// Send the notification to the client with DestXUID.
		AController* ControllerList = Viewport->Actor->GetLevel()->GetLevelInfo()->ControllerList;
		if (ControllerList != NULL)
		{
			for (AController* Controller = ControllerList; Controller != NULL; Controller = Controller->nextController)
			{
				if ( Controller->IsAPlayerController() )
				{
					APlayerController* PlayerController = (APlayerController*)Controller;

					if (PlayerController != NULL && PlayerController->PlayerReplicationInfo != NULL)
					{
						if ( XOnlineAreUsersIdentical((XUID*)PlayerController->PlayerReplicationInfo->XUID, &DestXUID) )
						{
							PlayerController->eventClientNotifyVoiceSystem(NotifyType, SrcXUIDStr, DataStr);
							return;
						}
					}
				}
			}
			
			LOG_VOICE_NOTIFY_FAILURE(__FUNC_NAME__, TEXT("Dest XUID wasn't found"), NotifyType, SrcXUID, DestXUID);
		}
	}
	else if (IsClient())
	{
		// Send the notification to the server.
		Viewport->Actor->eventServerNotifyVoiceSystem(NotifyType, SrcXUIDStr, DataStr);
	}

	unguard;
}

/* epic ====================================================================================================
* Function:	UXHVVoiceSubsystem::BroadcastVoiceNotification
* Author:	Per Vognsen
*
* Broadcasts a voice notification to all connected players (if on server) or to the server (if on client).
*
* Parameters:
* NotifyType		The kind of notification, indicating whether it's e.g. a registration.
* SrcXUID			The XUID of the sender.
* Data				Data specific to the type of notification.
* ==========================================================================================================
*/
void UXHVVoiceSubsystem::BroadcastVoiceNotification(ENotifyType NotifyType, const XUID& SrcXUID, const BYTE Data[MAX_VDATA_SIZE])
{
	guard(UXHVVoiceSubsystem::BroadcastVoiceNotification);

	if (Viewport == NULL || Viewport->Actor == NULL)
	{
		if ( Viewport == NULL )
			LOG_VOICE_BROADCASTNOTIFY_FAILURE(__FUNC_NAME__, TEXT("NULL Viewport"), NotifyType, SrcXUID);
		else LOG_VOICE_BROADCASTNOTIFY_FAILURE(__FUNC_NAME__, TEXT("NULL Viewport->Actor"), NotifyType, SrcXUID);
		return;
	}
	if (Viewport->Actor->GetLevel() == NULL || Viewport->Actor->GetLevel()->GetLevelInfo() == NULL)
	{
		if ( Viewport->Actor->GetLevel() == NULL )
			LOG_VOICE_BROADCASTNOTIFY_FAILURE(__FUNC_NAME__, TEXT("NULL Level"), NotifyType, SrcXUID);
		else LOG_VOICE_BROADCASTNOTIFY_FAILURE(__FUNC_NAME__, TEXT("NULL LevelInfo"), NotifyType, SrcXUID);
		return;
	}

	// Unpack the bytes of the source XUID for replication.
	BYTE* SrcXUIDBytes = (BYTE*) &SrcXUID;
	FString SrcXUIDStr;
	BytesToString(SrcXUIDBytes, 12, SrcXUIDStr);

	// Unpack the data into individual bytes for replication.
	FString DataStr;
	BytesToString(Data, 12, DataStr);

	if (IsServer())
	{
		// Broadcast the notification to all clients except SrcXUID.
		AController* ControllerList = Viewport->Actor->GetLevel()->GetLevelInfo()->ControllerList;
		if (ControllerList != NULL)
		{
			XUID MyXUID = GetLocalXUID();
			for (AController* Controller = ControllerList; Controller != NULL; Controller = Controller->nextController)
			{
				if ( Controller->IsAPlayerController() )
				{
					APlayerController* PlayerController = (APlayerController*)Controller;

					if (PlayerController != NULL && PlayerController->PlayerReplicationInfo != NULL)
					{
						APlayerReplicationInfo* PRI = PlayerController->PlayerReplicationInfo;

						if ( !XOnlineAreUsersIdentical((XUID*)PRI->XUID, &SrcXUID) &&
							!XOnlineAreUsersIdentical((XUID*)PRI->XUID, &MyXUID) )
						{
							PlayerController->eventClientNotifyVoiceSystem(NotifyType, SrcXUIDStr, DataStr);
						}
					}
				}
			}
		}
	}
	else if (IsClient())
	{
		// Send the notification to the server.
		Viewport->Actor->eventServerNotifyVoiceSystem(NotifyType, SrcXUIDStr, DataStr);
	}

	unguard;
}

/* epic ====================================================================================================
* Function:	UXHVVoiceSubsystem::SendVoiceNotification
* Author:	Per Vognsen
*
* Mutes a player. If on a client, this notifies the server that we wish to mute the player.
*
* Parameters:
* MuteeXUID		The XUID of the player to be muted.
* ==========================================================================================================
*/
void UXHVVoiceSubsystem::MuteTalker(const XUID& MuteeXUID)
{
	guard(UXHVVoiceSubsystem::MuteTalker);
	debugf(NAME_DevVoice, TEXT("MuteTalker - (0x%016I64X - 0x%08X)"), MuteeXUID.qwUserID, MuteeXUID.dwUserFlags);

	XUID MyXUID = GetLocalXUID();

	if (IsServer())
	{
		ServerMuteTalker(NULL, MyXUID, MuteeXUID);
	}
	else if (IsClient())
	{
		if (Viewport == NULL)
		{
			return;
		}

		// Send 'mute' packet.
		BYTE Data[MAX_VDATA_SIZE] = {0};
		*(XUID*) &Data[0] = MuteeXUID;
		BroadcastVoiceNotification(Notify_Mute, MyXUID, Data);
	}

	unguard;
}

/* epic ====================================================================================================
* Function:	UXHVVoiceSubsystem::UnMuteTalker
* Author:	Per Vognsen
*
* Mutes a player. If on a client, this notifies the server that we wish to unmute the player.
*
* Parameters:
* UnMuteeXUID	The XUID of the player to be unmuted.
* ==========================================================================================================
*/
void UXHVVoiceSubsystem::UnMuteTalker(const XUID& UnMuteeXUID)
{
	guard(UXHVVoiceSubsystem::UnMuteTalker);
	debugf(NAME_DevVoice, TEXT("UnMuteTalker - (0x%016I64X - 0x%08X)"), UnMuteeXUID.qwUserID, UnMuteeXUID.dwUserFlags);

	XUID MyXUID = GetLocalXUID();

	if (IsServer())
	{
		ServerUnMuteTalker(NULL, MyXUID, UnMuteeXUID);
	}
	else if (IsClient())
	{
		if (Viewport == NULL)
		{
			return;
		}

		// Send 'unmute' packet.
		BYTE Data[MAX_VDATA_SIZE] = {0};
		*(XUID*) &Data[0] = UnMuteeXUID;
		BroadcastVoiceNotification(Notify_UnMute, MyXUID, Data);
	}

	unguard;
}

/* epic ====================================================================================================
* Function:	UXHVVoiceSubsystem::ServerMuteTalker
* Author:	Per Vognsen
*
* Makes one player mute another player. Only valid on server.
*
* Parameters:
* MuterConnection	The network connection of the player wanting to mute.
* MuterXUID			The XUID of the player wanting to mute.
* MuteeXUID			The XUID of the player to be muted.
* ==========================================================================================================
*/
void UXHVVoiceSubsystem::ServerMuteTalker(UTcpipConnectionXbox* MuterConnection, const XUID& MuterXUID, const XUID& TargetXUID)
{
	guard(UXHVVoiceSubsystem::ServerMuteTalker);

	if (MuterXUID.qwUserID == GetLocalXUID().qwUserID)
	{
		// Add the mutee to the mutelist if it is not already there.
		ServerMuteList.AddUniqueItem(TargetXUID.qwUserID);
	}
	else
	{
		if ( MuterConnection == NULL )
		{
			LOG_VOICE_FAILURE(__FUNC_NAME__,TEXT("Null MuterConnection specified!"), MuterXUID);
			return;
		}

		// Add the mutee to the mutelist if it is not already there.
		MuterConnection->MuteList.AddUniqueItem(TargetXUID.qwUserID);
	}

	unguard;
}

/* epic ====================================================================================================
* Function:	UXHVVoiceSubsystem::ServerUnMuteTalker
* Author:	Per Vognsen
*
* Makes one player unmute another player. Only valid on server.
*
* Parameters:
* UnMuterConnection		The network connection of the player wanting to unmute.
* UnMuterXUID			The XUID of the player wanting to unmute.
* UnMuteeXUID			The XUID of the player to be unmuted.
* ==========================================================================================================
*/
void UXHVVoiceSubsystem::ServerUnMuteTalker(UTcpipConnectionXbox* UnMuterConnection, const XUID& UnMuterXUID, const XUID& UnMuteeXUID)
{
	guard(UXHVVoiceSubsystem::ServerUnMuteTalker);

	if (UnMuterXUID.qwUserID == GetLocalXUID().qwUserID)
	{
		// If the unmutee is in the mutelist, remove it.
		if (ServerMuteList.FindItemIndex(UnMuteeXUID.qwUserID) != -1)
		{
			ServerMuteList.RemoveItem(UnMuteeXUID.qwUserID);
		}
		else
		{
			debugf(NAME_DevLive, TEXT("Error: Trying to unmute player that is not muted."));
		}		
	}
	else
	{
		if (UnMuterConnection != NULL)
		{
			TArray<ULONGLONG>& MuteList = UnMuterConnection->MuteList;
			// If the unmutee is in the mutelist, remove it.
			if (MuteList.FindItemIndex(UnMuteeXUID.qwUserID) != -1)
			{
				MuteList.RemoveItem(UnMuteeXUID.qwUserID);
			}
			else
			{
				debugf(NAME_DevLive, TEXT("Error: Trying to unmute player that is not muted."));
			}
		}
	}

	unguard;
}

/*==========================================================================================================
function	UXHVVoiceSubsystem::SetOutputMode
Desc:		Configures whether other player's voices are heard through speakers or headphones.

input:		Mode
			valid values are
			0 - headphones	(COMM_Headphones or UIX_VOICE_STATUS_COMMUNICATOR)
			1 - speakers	(COMM_Speakers or UIX_VOICE_STATUS_SPEAKERS)
			2 - none		(COMM_None or UIX_VOICE_STATUS_NONE)


notes:		XHV supports having per-player settings for whether the voice is played
			from headphones or speakers (i.e. PlayerA's voice could be played through
			speakers, while PlayerB's voice could be played through speakers).
			This is necessary to support split-screen voice chat support, but [for now, at least]
			we aren't going to support this - just use the global setting for all players.

****		Here is where we convert a communicator status of COMM_None (which would mean no voice-chat
			icon is displayed in the player/friends list) into a communicator status of COMM_Speaker (which
			means that the voice-over-speaker icon would be displayed).

last modified by:	Ron Prestenback
==========================================================================================================*/
void UXHVVoiceSubsystem::SetOutputMode( BYTE Mode )
{
	guard(UXHVVoiceSubsystem::SetOutputMode);

	if (!IsXboxLiveMatch())
	{
		return;
	}

	if ( Mode == COMM_None )
		Mode = COMM_Speaker;

	// use speakers if that's the current setting, or if we don't have a communicator plugged in
	bVoiceThroughSpeakers = Mode == COMM_Speaker || LocalTalkerPort == -1;
	UIX_VOICE_STATUS_TYPE LocalCommStatus = bVoiceThroughSpeakers
		? UIX_VOICE_STATUS_SPEAKERS
		: UIX_VOICE_STATUS_COMMUNICATOR;

	for (INT idx = 0; idx < Voices.Num(); idx++)
	{
		ToggleVoiceOutput(Voices(idx).PlayerID,bVoiceThroughSpeakers);
	}

	ServerUpdateCommStatus( LocalCommStatus );

	unguard;
}

/*==========================================================================================================
function	UXHVVoiceSubsystem::ServerUpdateCommStatus
Desc:		Local machine calls this function to update its communicator status to the rest of the game.
			If we're the server, propagate this status update to all clients, otherwise send a comm status
			packet to the server. In either case, we then update comm status locally.


input:		Mode
			valid values are
				0 - headphones	(COMM_Headphones or UIX_VOICE_STATUS_COMMUNICATOR)
				1 - speakers	(COMM_Speakers or UIX_VOICE_STATUS_SPEAKERS)
				2 - none		(COMM_None or UIX_VOICE_STATUS_NONE)

last modified by:	Ron Prestenback
==========================================================================================================*/
void UXHVVoiceSubsystem::ServerUpdateCommStatus( BYTE Mode )
{
	guard(UXHVVoiceSubsystem::ServerUpdateCommStatus);

	if ( bInitialized && Viewport != NULL )
	{
		XUID PlayerXUID = GetLocalXUID();
		BYTE Data[MAX_VDATA_SIZE] = {0};
		Data[0] = Mode;

		// if we are the server, then broadcast to all other clients
		// (we're updating the voicechat status for the server)
		BroadcastVoiceNotification(Notify_CommStatus, PlayerXUID, Data);

		// then update the status locally
		UpdateCommStatus( PlayerXUID, Mode );
	}

	unguard;
}

/*==========================================================================================================
function	UXHVVoiceSubsystem::UpdateCommStatus
Desc:		Notifies UIX about the communicator status of the player associated with the specified XUID.  This
			function should only be called by ServerUpdateCommStatus (when updating our own communicator status)
			or from HandleIncomingVoicePacket (when updating other player's communicator status).

input:		Mode
			valid values are
				0 - headphones	(COMM_Headphones or UIX_VOICE_STATUS_COMMUNICATOR)
				1 - speakers	(COMM_Speakers or UIX_VOICE_STATUS_SPEAKERS)
				2 - none		(COMM_None or UIX_VOICE_STATUS_NONE)

effects:	This updates the voice icon that will appear on the scoreboards and UIX player list.  If the
specified player is the local player, we'll also update the player's online state (which affects
how the player appears in other player's "friends" lists).

last modified by:	Ron Prestenback
==========================================================================================================*/
void UXHVVoiceSubsystem::UpdateCommStatus( const XUID& inPlayerID, BYTE Mode)
{
	guard(UXHVVoiceSubsystem::UpdateCommStatus);

	debugf(NAME_DevVoice, TEXT("UpdateCommStatus for 0x%016I64X to %i"), inPlayerID.qwUserID, Mode);
	UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
	if ( UIX )
	{
		// update the voice icon that will appear on the scoreboard and UIX player lists
		// (this is called for both local and remote players)
		UIX->UpdateRemoteCommunicator(inPlayerID, Mode);

		XUID LocalXUID = GetLocalXUID();
		if ( XOnlineAreUsersIdentical(&inPlayerID, &LocalXUID) )
		{
			// if this is the local player, notify UIX that it should update the player's
			// online state (for presence)
			debugf(NAME_DevVoice, TEXT("Calling VoiceCommunicatorUpdate..."));
			UIX->VoiceCommunicatorUpdate(LocalTalkerPort);
		}
	}
	unguard;
}

/*==========================================================================================================
function	UXHVVoiceSubsystem::ToggleVoiceOutput
Desc:		Sets the voice output mode in the XHVEngine for the specified player.

input:		inPlayerID
				XUID of a player which should already be registered in our local Voices array.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UXHVVoiceSubsystem::ToggleVoiceOutput( const XUID& inPlayerID, UBOOL bUseSpeakers)
{
	guard(UXHVVoiceSubsystem::ToggleVoiceOutput);

	XUID LocalXUID = GetLocalXUID();
	if (bInitialized && !XOnlineAreUsersIdentical(&inPlayerID, &LocalXUID))
	{
		// if we have no headphones, then force speaker output
		if ( LocalTalkerPort == -1)
			bUseSpeakers = 1;

		debugf(NAME_DevVoice,TEXT("Setting %s for xuid (0x%016I64X - 0x%08X)"),bUseSpeakers?TEXT("speakers"):TEXT("headphones"),inPlayerID.qwUserID,inPlayerID.dwUserFlags);
		if (bUseSpeakers)
		{
			// setup the volume max for tv output
			const DSMIXBINVOLUMEPAIR dsmbvp = { DSMIXBIN_FXSEND_15, DSBVOLUME_MAX };
			const DSMIXBINS dsmb = { 1, &dsmbvp };
			XHVEngine->SetMixBinMapping(inPlayerID,XHV_PLAYBACK_TO_SPEAKERS,&dsmb);
			// enable speakers
			XHVEngine->SetPlaybackPriority(inPlayerID,XHV_PLAYBACK_TO_SPEAKERS,XHV_PLAYBACK_PRIORITY_MAX);
			// disable headphones
			if (LocalTalkerPort != -1)
			{
				XHVEngine->SetPlaybackPriority(inPlayerID,LocalTalkerPort,XHV_PLAYBACK_PRIORITY_NEVER);
			}
		}
		else
		{
			// enable headphones
			XHVEngine->SetPlaybackPriority(inPlayerID,LocalTalkerPort,XHV_PLAYBACK_PRIORITY_MAX);
			// disable speakers
			XHVEngine->SetPlaybackPriority(inPlayerID,XHV_PLAYBACK_TO_SPEAKERS,XHV_PLAYBACK_PRIORITY_NEVER);
		}
	}
	unguard;
}

/* epic ====================================================================================================
* Function:	UXHVVoiceSubsystem::IsMuted
* Author:	Per Vognsen
*
* Checks whether one player is muting another. Only valid on server.
*
* Parameters:
* SrcXUID		The XUID of the player doing the muting.
* DestXUID		The XUID of the player being muted.
* ==========================================================================================================
*/
UBOOL UXHVVoiceSubsystem::IsMuted(const XUID& SrcXUID, const XUID& DestXUID)
{
	guard(UXHVVoiceSubsystem::IsMuted);

	// Check whether the server has DestXUID muted
	if (SrcXUID.qwUserID == GetLocalXUID().qwUserID)
	{
		if ( ServerMuteList.FindItemIndex(DestXUID.qwUserID) != -1 )
			return TRUE;
	}

	// Check whether the server is muted by SrcXUID
	if (DestXUID.qwUserID == GetLocalXUID().qwUserID)
	{
		if ( ServerMuteList.FindItemIndex(SrcXUID.qwUserID) != -1 )
			return TRUE;
	}

	// Otherwise, client-to-client mute
	// Check whether dest is in src's mutelist if src is a client
	if (GetLocalXUID().qwUserID != SrcXUID.qwUserID)
	{
		UTcpipConnectionXbox* SrcConn = GetConnectionByXUID(SrcXUID);
		if (SrcConn != NULL)
		{
			if ( SrcConn->MuteList.FindItemIndex(DestXUID.qwUserID) != -1 )
				return TRUE;
		}
	}

	// Check whether src is in dest's mutelist if dest is a client
	if (GetLocalXUID().qwUserID != DestXUID.qwUserID)
	{
		UTcpipConnectionXbox* DestConn = GetConnectionByXUID(DestXUID);
		if (DestConn != NULL)
		{
			if ( DestConn->MuteList.FindItemIndex(SrcXUID.qwUserID) != -1 )
				return TRUE;
		}
	}

	return FALSE;
	unguard;
}

/* epic =============================================
* ::IsTalking
* Author: bcl
* Takes a remote XUID and a local port.
* returns true if the two are talking.
* ==================================================
*/
UBOOL UXHVVoiceSubsystem::IsTalking( const XUID& xuidRemoteTalker, DWORD dwPort )
{
	guard(UXHVVoiceSubsystem::IsTalking);
	if ((dwPort != LocalTalkerPort) || (!XHVEngine))
		return 0;

	UBOOL bIsTalking = XHVEngine->IsTalking(xuidRemoteTalker);
	if (bIsTalking)
	{
		// if this player isn't registered with us, they can't be talking
		INT Ndx = PlayerVoiceInfoNdx(xuidRemoteTalker);
		if (Ndx < 0)
			bIsTalking = false;
	}
	return bIsTalking;
	unguard;
}

/* epic =============================================
* Finds the given XUID in the list of voices, returning
* the index of the matching record in the Voices array.
* Returns the index of the matching record or -1 if there is no match.
* ==================================================
*/
INT UXHVVoiceSubsystem::PlayerVoiceInfoNdx( const XUID& inXUID)
{
	guard(UXHVVoiceSubsystem::PlayerVoiceInfoNdx);
	INT retval = -1;
	for (INT i = 0; i < Voices.Num(); ++i)
	{
		if (XOnlineAreUsersIdentical(&Voices(i).PlayerID, &inXUID))
		{
			retval = i;
			break;
		}
	}

	return retval;
	unguard;

}

/* epic ====================================================================================================
* Function:	UXHVVoiceSubsystem::GetConnectionByXUID
* Author:	Per Vognsen
*
* Finds the network connection corresponding to an XUID.
*
* Parameters:
* PlayerXUID	The XUID of the desired connection.
* ==========================================================================================================
*/
//LOG_VOICE_FAILURE(__FUNC_NAME__, TEXT(""), PlayerXUID);
UTcpipConnectionXbox* UXHVVoiceSubsystem::GetConnectionByXUID(const XUID& PlayerXUID)
{
	guard(UXHVVoiceSubsystem::GetConnectionByXUID);

	if (Viewport == NULL || Viewport->Actor == NULL || Viewport->Actor->GetLevel() == NULL)
	{
		if ( Viewport == NULL )
			LOG_VOICE_FAILURE(__FUNC_NAME__, TEXT("NULL Viewport."), PlayerXUID);
		else if ( Viewport->Actor == NULL )
			LOG_VOICE_FAILURE(__FUNC_NAME__, TEXT("NULL Viewport->Actor."), PlayerXUID);
		else LOG_VOICE_FAILURE(__FUNC_NAME__, TEXT("NULL XLevel."), PlayerXUID);

		return NULL;
	}

	UNetDriver* Driver = Viewport->Actor->GetLevel()->NetDriver;
	if (Driver != NULL)
	{
		BYTE Empty[12];
		appMemzero(Empty, ARRAY_COUNT(Empty));

		if ( !appMemcmp(Empty, &PlayerXUID, ARRAY_COUNT(Empty)) )
		{
			LOG_VOICE_FAILURE(__FUNC_NAME__, TEXT("Empty PlayerXUID specified!"), PlayerXUID);
			return NULL;
		}

		for (INT ConnIdx = 0; ConnIdx < Driver->ClientConnections.Num(); ConnIdx++)
		{
			UTcpipConnectionXbox* Conn = Cast<UTcpipConnectionXbox>(Driver->ClientConnections(ConnIdx));

			if ( Conn == NULL || Conn->Actor == NULL || Conn->Actor->PlayerReplicationInfo == NULL )
			{
				continue;
			}

			if ( !appMemcmp(Conn->Actor->PlayerReplicationInfo->XUID, Empty, ARRAY_COUNT(Empty)) )
			{
				debugf(NAME_DevVoice, TEXT("GetConnectionByXUID - Player has blank XUID: %s (%s)"), Conn->Actor->GetName(), *Conn->Actor->PlayerReplicationInfo->PlayerName);
				continue;
			}

			if ( XOnlineAreUsersIdentical((XUID*)Conn->Actor->PlayerReplicationInfo->XUID, &PlayerXUID) )
				return Conn;
		}

		LOG_VOICE_FAILURE(__FUNC_NAME__, TEXT("No matching connections found."), PlayerXUID);
		return NULL;
	}
	else
	{
		LOG_VOICE_FAILURE(__FUNC_NAME__, TEXT("Invalid net driver."), PlayerXUID);
		return NULL;
	}

	unguard;
}

/* epic ====================================================================================================
* Function:	UXHVVoiceSubsystem::GetXUIDByConnection
* Author:	Per Vognsen
*
* Finds the XUID corresponding to a connection.
*
* Parameters:
* SearchConnection	The connection corresponding to the desired XUID.
* ==========================================================================================================
*/
XUID UXHVVoiceSubsystem::GetXUIDByConnection(UTcpipConnectionXbox* SearchConnection)
{
	guard(UXHVVoiceSubsystem::GetXUIDByConnection);

	XUID Result = {0};

	if ( SearchConnection == NULL )
	{
		debugf(NAME_DevVoice, TEXT("GetXUIDByConnection failed: NULL SearchConnection specified!"));
		return Result;
	}

	if ( SearchConnection->Actor == NULL )
	{
		debugf(NAME_DevVoice, TEXT("GetXUIDByConnection failed: SearchConnection has no PlayerController!"));
		return Result;
	}

	if ( SearchConnection->Actor->PlayerReplicationInfo == NULL )
	{
		debugf(NAME_DevVoice, TEXT("GetXUIDByConnection failed: SearchConnection's PlayerController has no PRI!"));
		return Result;
	}

	appMemcpy(&Result, SearchConnection->Actor->PlayerReplicationInfo->XUID, sizeof(XUID));
	return Result;

	unguard;
}

/* epic ====================================================================================================
* Function:	UXHVVoiceSubsystem::SetVolume
* Author:	Per Vognsen
*
* Sets the voice chat volume (between 0 and 1).
*
* Parameters:
* Volume	The new volume (between 0 and 1).
* ==========================================================================================================
*/
void UXHVVoiceSubsystem::SetVolume(FLOAT Volume)
{
	guard(UXHVVoiceSubSystem::SetVolume);

	extern IDirectSound8* GDirectSound;

	// Convert to DSP data format.
	DWORD GainParam = DWORD(Volume * 0x7FFFFF);
	GDirectSound->SetEffectData(UserVoiceRateConverter_VoiceRateConverter, 32, &GainParam, sizeof(DWORD), DSFX_IMMEDIATE);

	unguard;
}