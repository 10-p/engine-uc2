/*=============================================================================
	WinClient.cpp: UWindowsClient code.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "XboxDrv.h"
#ifndef FINAL_RELEASE
#include "FOutputDeviceFile.h"
#include "XboxLevelMemStat.h"
#endif
#include "Engine.h"
#include "UnIpDrv.h"
#include "GUI.h"

//scion jg -- Added for PIX timing & pushbuffer support
#if defined(_DEBUG) || defined(PROFILE)
// For PIX integration
#include <D3d8perf.h>
// For gathering of performance statistics
#include <XbDm.h>
#endif

// epic bcl -- include the UIX interaction so that we can cast the created one and insert it in the InteractionMaster

#include "UnUIXInteraction.h"

/*-----------------------------------------------------------------------------
	Class implementation.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(UXboxClient);

/*-----------------------------------------------------------------------------
	UXboxClient implementation.
-----------------------------------------------------------------------------*/

UBOOL GShowSubtitles = TRUE;

//
// UXboxClient constructor.
//
UXboxClient::UXboxClient()
{
}

//
// Static init.
//
void UXboxClient::StaticConstructor()
{
	guard(UXboxClient::StaticConstructor);

	new(GetClass(),TEXT("JoyDeadZone"),			RF_Public)UFloatProperty(CPP_PROPERTY(JoyDeadZone				), TEXT(""), CPF_Config );

	unguard;
}

//
// Initialize the platform-specific viewport manager subsystem.
// Must be called after the Unreal object manager has been initialized.
// Must be called before any viewports are created.
//
void UXboxClient::Init( UEngine* InEngine )
{
	guard(UXboxClient::Init);

	// Init base.
	UClient::Init( InEngine );

	// Note configuration.
	PostEditChange();

#ifndef FINAL_RELEASE
	XINPUT_DEBUG_KEYQUEUE_PARAMETERS KeyQueueParams =
	{
		XINPUT_DEBUG_KEYQUEUE_FLAG_KEYDOWN		|
		XINPUT_DEBUG_KEYQUEUE_FLAG_KEYUP		|
		XINPUT_DEBUG_KEYQUEUE_FLAG_KEYREPEAT,
		50,
		500,
		70
	};

	XInputDebugInitKeyboardQueue( &KeyQueueParams );	
#endif
	// Success.
	debugf( NAME_Init, TEXT("Client initialized") );
	unguard;
}


//
// Shut down the platform-specific viewport manager subsystem.
//
void UXboxClient::Destroy()
{
	guard(UXboxClient::Destroy);

	debugf( NAME_Exit, TEXT("Xbox client shut down") );
	
	Super::Destroy();

	unguard;
}

//
// Failsafe routine to shut down viewport manager subsystem
// after an error has occured. Not guarded.
//
void UXboxClient::ShutdownAfterError()
{
	debugf( NAME_Exit, TEXT("Executing UXboxClient::ShutdownAfterError") );

	if( Engine && Engine->Audio )
		Engine->Audio->ConditionalShutdownAfterError();

	for( INT i=Viewports.Num()-1; i>=0; i-- )
	{
		UXboxViewport* Viewport = (UXboxViewport*)Viewports( i );
		Viewport->ConditionalShutdownAfterError();
	}

	Super::ShutdownAfterError();
}

//
// Update Gamma.
//
void UXboxClient::UpdateGamma()
{
	guard(UXboxClient::UpdateGamma);
	
	if( Viewports.Num() &&
		Viewports(0)->RenDev )
		Viewports(0)->RenDev->UpdateGamma( Viewports(0) );

	unguard;
}

//
// Restore Gamma.
//
void UXboxClient::RestoreGamma()
{
	guard(UXboxClient::RestoreGamma);

	if( Viewports.Num() &&
		Viewports(0)->RenDev )
		Viewports(0)->RenDev->RestoreGamma();

	unguard;
}

//
// Command line.
//
UBOOL UXboxClient::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UXboxClient::Exec);

	if( UClient::Exec( Cmd, Ar ) )
	{
		return 1;
	}
	// Enable/disable subtitles
	else if (ParseCommand(&Cmd,TEXT("SUBTITLES")))
	{
		if (ParseCommand(&Cmd,TEXT("TRUE")) || ParseCommand(&Cmd,GTrue))
		{
			GShowSubtitles = TRUE;
		}
		else
		{
			GShowSubtitles = FALSE;
		}
		return 1;
	}
	//jg -- Used to hack level rendering
	else if (ParseCommand(&Cmd,TEXT("TOGGLERENDERLEVEL")))
	{
		extern UBOOL GRenderLevel;
		GRenderLevel ^= TRUE;
		return 1;
	}
#ifndef FINAL_RELEASE
	//jg -- Used for viewing the loaded set of XPRs
	else if (ParseCommand(&Cmd,TEXT("DUMPXPRS")))
	{
		extern void DumpLoadedXPRInfo(void);
		DumpLoadedXPRInfo();
		return 1;
	}
	//jg -- Used for viewing whether any XPR contains items in another
	else if (ParseCommand(&Cmd,TEXT("DUMPXPRDUPES")))
	{
		extern void DumpXPRDuplicates(void);
		DumpXPRDuplicates();
		return 1;
	}
	//jg -- Used for finding meshes not loaded in XPRs
	else if (ParseCommand(&Cmd,TEXT("DUMPNONXPRMESHES")))
	{
		// Check all loaded static meshes
		for (TObjectIterator<UStaticMesh> It; It; ++It)
		{
			if (It->VertexStream.Vertices.IsInXPR() == FALSE)
			{
				Ar.Logf(TEXT("StaticMesh '%s' is not in an XPR"),It->GetName());
			}
		}
		// Check all loaded skeletal meshes
		for (TObjectIterator<USkeletalMesh> It; It; ++It)
		{
			if (It->LODModels(0).GPUVertexStream.Vertices.IsInXPR() == FALSE)
			{
				Ar.Logf(TEXT("SkeletalMesh '%s' is not in an XPR"),It->GetName());
			}
		}
		return 1;
	}
	// scion sz -
	else if( ParseCommand(&Cmd, TEXT("MEMUSED")) )
	{
		UBOOL bShowDetail = 0;
		if( ParseCommand(&Cmd, TEXT("DETAIL")) )
		{
            bShowDetail = 1;            
		}

		FXboxLevelMemStat MemStat( Ar, (UD3DRenderDevice*)Viewports(0)->RenDev, bShowDetail );

		if( ParseCommand(&Cmd, TEXT("RESOURCE")) )
		{
			MemStat.ShowCachedResource();
		}
		else if( ParseCommand(&Cmd, TEXT("TEXTURE")) )
		{
			MemStat.ShowTextures();
		}		
		else if( ParseCommand(&Cmd, TEXT("STATICMESH")) )
		{
			MemStat.ShowStaticMeshes();
		}
		else if( ParseCommand(&Cmd, TEXT("STATICMESHINSTANCE")) )
		{
			MemStat.ShowStaticMeshInstances();
		}
		else if( ParseCommand(&Cmd, TEXT("TERRAIN")) )
		{
			MemStat.ShowTerrain();
		}
		else if( ParseCommand(&Cmd, TEXT("BSP")) )
		{
			MemStat.ShowBSP();
		}
		else if( ParseCommand(&Cmd, TEXT("MESH")) )
		{
			MemStat.ShowSkeletalMesh();
		}
		else if( ParseCommand(&Cmd, TEXT("ANIMATION")) )
		{
			MemStat.ShowAnimation();
		}
		else if( ParseCommand(&Cmd, TEXT("STATICPROJECTOR")) )
		{
			MemStat.ShowStaticProjectors();
		}
		else if( ParseCommand(&Cmd, TEXT("ACTOR")) )
		{
			MemStat.ShowActors();
		}
		else
		{
			MemStat.ShowTextures();
			MemStat.ShowStaticMeshes();
			MemStat.ShowStaticMeshInstances();
			MemStat.ShowTerrain();
			MemStat.ShowBSP();
			MemStat.ShowSkeletalMesh();
			MemStat.ShowAnimation();
			MemStat.ShowStaticProjectors();
			MemStat.ShowActors();
		}
	}
	else if (ParseCommand(&Cmd,TEXT("MOVIESAFEZONE")))
	{
		// Global for tracking whether to show the safezone or not
		extern UBOOL GShowMovieSafezone;
		GShowMovieSafezone ^= 1;
		return 1;
	}

#endif
//scion jg -- Added to allow for console command perf gathering
#if defined(PROFILE) || defined(_DEBUG)
	else if (ParseCommand(&Cmd,TEXT("XBPERF")))
	{
		if (ParseCommand(&Cmd,TEXT("START")))
		{
			// Stop any current stats gathering
			DmStopProfiling();
			TCHAR szTemp[MAX_PATH] = { '\0' };
			// Create a default output file name in case one isn't specified
			FString strFile(TEXT("Z:\\XBPerf.stats"));
			// Parse out the use specified file name
			if (Parse(Cmd,TEXT("FILE="),szTemp,MAX_PATH))
			{
				strFile = szTemp;
			}
			// Now tell the perf API to start logging to that file
			HRESULT hr = DmStartProfiling(TCHAR_TO_ANSI(*strFile),0);
			// Log whether it worked or not
			Ar.Logf(SUCCEEDED(hr) ?
				TEXT("Gathering performance stats to file %s") :
				TEXT("Failed to gather stats to file %s"),*strFile);
		}
		else if (ParseCommand(&Cmd,TEXT("STOP")))
		{
			// Stop any current stats gathering
			DmStopProfiling();
			Ar.Log(TEXT("Performance statistics gathering stopped"));
		}

		return 1;
	}
#endif
//scion sz -
#if 0	
	else if( ParseCommand(&Cmd,TEXT("TOGGLEPROFILE")) )
	{
        extern FLOAT GDumpProfileDeltaTime;
		FLOAT ProfileDeltaTime = -1.0f;
		ProfileDeltaTime = appAtof( *ParseToken(Cmd,0) );
		if( ProfileDeltaTime >= 0.0f )
		{
			GDumpProfileDeltaTime = ProfileDeltaTime;
			debugf( TEXT("TOGGLEPROFILE: Profiling=%.4f"), GDumpProfileDeltaTime );
		}		
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("PRINTSLOWSTAT")) )
	{
		extern FLOAT GDumpStatsDeltaTime;
		FLOAT DeltaTimeThreshold = -1.0f;
		DeltaTimeThreshold = appAtof( *ParseToken(Cmd,0) );
		if( DeltaTimeThreshold >= 0.0f )
		{
			GDumpStatsDeltaTime = DeltaTimeThreshold;
			debugf( TEXT("PRINTSLOWSTAT: DeltaTime Threshold=%.4f"), GDumpStatsDeltaTime );
		}
	}
#endif
	else if (ParseCommand(&Cmd,TEXT("GPUSKIN")))
	{
		if( ParseCommand(&Cmd,TEXT("1")) || ParseCommand(&Cmd,TEXT("ON")) )
		{
			GHardwareSkinning = 1;
		}
		else if( ParseCommand(&Cmd,TEXT("0")) || ParseCommand(&Cmd,TEXT("OFF")) )
		{
			GHardwareSkinning = 0;
		}
	}
	//scion jg -- Turn movie capture on/off
	else if (ParseCommand(&Cmd,TEXT("RECORDMOVIE")))
	{
		extern UBOOL GIsRecordingMovie;
		extern FLOAT GMovieFPS;
		extern FLOAT GMovieTimeStep;
		// Enable movie recording if not on
		if (GIsRecordingMovie == 0)
		{
			GIsRecordingMovie = 1;
			// See if the frame rate was specified
			if (Parse(Cmd,TEXT("FPS="),GMovieFPS))
			{
				// Update the time step to match
				GMovieTimeStep = 1.f / GMovieFPS;
			}
		}
		// Otherwise stop recording
		else
		{
			GIsRecordingMovie = 0;
		}
	}	
	//scion jg -- Reboot the xbox to the title
	else if (ParseCommand(&Cmd,TEXT("RESTARTTITLE")))
	{
		XLaunchNewImage(XBE_NAME,NULL);
	}
	// scion sz - unload config
	else if( ParseCommand(&Cmd,TEXT("EMPTYCONFIG")) )
	{
		FString FileName;
		FString ClassName;

		if (ParseCommand(&Cmd, TEXT("All")))
		{
			GConfig->UnloadAll();
		}
		else
		{
			if (Parse(Cmd,TEXT("FILE="), FileName))
			{
				GConfig->UnloadFile(*FileName);
			}
			if (Parse(Cmd,TEXT("CLASS="), ClassName))
			{
				for (TObjectIterator<UClass> It; It; ++It)
				{
					UClass* Class = *It;
					if (appStricmp(Class->GetName(), *ClassName) == 0)
					{
						GConfig->UnloadFile(*Class->ClassConfigName);
					}
				}			
			}
		}
	}
	else if ( ParseCommand(&Cmd,TEXT("SETDEADZONE")) )
	{
		FString Result;
		if ( ParseToken(Cmd, Result, 0) )
		{
			if ( Result == TEXT("Reset") )
			{
				JoyDeadZone = GetDefault<UXboxClient>()->JoyDeadZone;
			}
			else
			{
				JoyDeadZone = appAtof(*Result);
			}
		}

		return 1;
	}
	return 0;
	unguard;
}

//
// Perform timer-tick processing on all visible viewports.  This causes
// all realtime viewports, and all non-realtime viewports which have been
// updated, to be blitted.
//
void UXboxClient::Tick( FLOAT DeltaSeconds )
{
	guard(UXboxClient::Tick);

	check(InteractionMaster);
	check(InteractionMaster->UIX);

	static UBOOL UIXInstalled = false;
	if ( !UIXInstalled )
	{
		UIXInstalled = true;
		InteractionMaster->UIX->InitUIX();
	}

	// store the current active controller id
	BYTE PreviousControllerId = Engine->ActiveControllerId;

#ifndef FINAL_RELEASE
	// scion sz -
	// get keyboard input
	UpdateKeyboardInput();
#endif //FINAL_RELEASE

	// scion sz - 
	// detect active devices
	DetectDevices();

	// scion sz -
	// update gamepad input for gamepads not used by viewports
	UpdateGamepadUnboundInput();

	// now restore the previously active ID in case it now points to an unbound gamepad.
	Engine->ActiveControllerId = PreviousControllerId;

//scion jg -- Added for PIX pushbuffer timing and capture support
#if defined(PROFILE) || defined(_DEBUG)
	do
	{
#endif

	// Repaint all viewports.
	// scion sz - only do blit (present) on last viewport
	UBOOL bBlitViewport = 1; 
	DOUBLE CurrentTime = appSeconds();
  	for( INT i=0; i<Viewports.Num(); i++ )
	{
		UViewport* Viewport = Viewports(i);

		if( !Viewport->bDisableDraw )
		{
			Viewport->Repaint( bBlitViewport, DeltaSeconds );
			bBlitViewport = 0;
		}
		else
		{
			// otherwise, update the viewport's current time
			// (since CurrentTime is only updated in UViewport::Lock)
			Viewport->CurrentTime = CurrentTime;
		}
	}

//scion jg -- Added for PIX pushbuffer timing and capture support
#if defined(PROFILE) || defined(_DEBUG)
	}
	// This should only occur twice for each PIX grab. Once for timing
	// and once for grabbing the pushbuffer contents.
	while (D3DPERF_QueryRepeatFrame());
#endif
	unguard;
}

//
// Create a new viewport.
//
UViewport* UXboxClient::NewViewport( const FName Name )
{
	guard(UXboxClient::NewViewport);
	return new( this, Name )UXboxViewport();
	unguard;
}

//
// Configuration change.
//
void UXboxClient::PostEditChange()
{
	guard(UXboxClient::PostEditChange);

	Super::PostEditChange();

	unguard;
}

//
// Enable or disable all viewport windows that have ShowFlags set (or all if ShowFlags=0).
//
void UXboxClient::EnableViewportWindows( DWORD ShowFlags, int DoEnable )
{
}

//
// Show or hide all viewport windows that have ShowFlags set (or all if ShowFlags=0).
//
void UXboxClient::ShowViewportWindows( DWORD ShowFlags, int DoShow )
{
}

//
// Make this viewport the current one.
// If Viewport=0, makes no viewport the current one.
//
void UXboxClient::MakeCurrent( UViewport* InViewport )
{
}

// Returns a pointer to the viewport that was last current.
UViewport* UXboxClient::GetLastCurrent()
{
	return NULL;
}

/* scion ======================================================================
 * UXboxClient::IsGamepadInUse
 * Author: jg
 *
 * Determines if the passed in gamepad ID is being used by a viewport. This
 * is used when detecting dynamically inserted and removed gamepads. It is meant
 * to tell a viewport if it can have the specified gamepad or not.
 *
 * input:	nID INT The ID of the gamepad in question
 *
 * ============================================================================
 */
UBOOL UXboxClient::IsGamepadInUse(INT nID)
{
	guardSlow(UXboxClient::IsGamepadInUse);
	UBOOL bUsed = 0;
	// Check each viewport that we have active
	for (INT nViewport = 0; nViewport < Viewports.Num() && bUsed == 0;
		nViewport++)
	{
		bUsed = Viewports(nViewport)->GetGamepadID() == nID;
/*
ron --	once gameplay has begun (i.e. that is, game is no longer at the initial interactive screen,
		viewports are locked to whichever gamepad port activated that viewport.  So, even if the gamepad
		is removed, the viewport will retain its GamepadID.  As a result, we should match the gamepadID
		in question against the viewport's GamepadID, as the viewport's playercontroller's GamepadID might
		be a tick behind.

		// Check the player controller for the gamepad id
		APlayerController* pController = Cast<APlayerController>(
			Viewports(nViewport)->Actor);
		if (pController)
		{
			bUsed = pController->GamepadID == nID;
		}
*/
	}
	return bUsed;
	unguardSlow;
}

/* scion ======================================================================
* UXboxClient::GetGamepadViewport
* Author: sz
* 
* same as IsGamepadInUse() but returns the viewport that is using the gamepad
*
* ============================================================================
*/
UViewport* UXboxClient::GetGamepadViewport(INT nID)
{
	guardSlow(UXboxClient::GetGamepadViewport);
	UViewport* FoundViewport = NULL;
	// Check each viewport that we have active
	for (INT nViewport = 0; nViewport < Viewports.Num() && !FoundViewport;	nViewport++)
	{
		UViewport* curViewport = Viewports(nViewport);
		if ( curViewport->GetGamepadID() == nID )
			FoundViewport = curViewport;
/*
ron --	once gameplay has begun (i.e. that is, game is no longer at the initial interactive screen,
		viewports are locked to whichever gamepad port activated that viewport.  So, even if the gamepad
		is removed, the viewport will retain its GamepadID.  As a result, we should match the gamepadID
		in question against the viewport's GamepadID, as the viewport's playercontroller's GamepadID might
		be a tick behind.

		// Check the player controller for the gamepad id
		APlayerController* pController = Cast<APlayerController>(Viewports(nViewport)->Actor);
		if (pController && 
			pController->GamepadID == nID )
		{
			FoundViewport = Viewports(nViewport);
		}
*/
	}
	return FoundViewport;
	unguardSlow;
}

/* scion ======================================================================
* UXboxClient::GetInitialScreenSize
* Author: sz
*
* Returns intial size of the screen.  This can just be the back buffer size.
*
* output:	SizeX,SizeY
*
* ============================================================================
*/
void UXboxClient::GetInitScreenSize( INT& SizeX, INT& SizeY )
{
	guardSlow(UXboxClient::GetInitialScreenSize);

	SizeX = FullscreenViewportX;
	SizeY = FullscreenViewportY;

	unguardSlow;
}

/* scion ======================================================================
* UXboxClient::DetectDevices
* Author: sz
*
* Checks all xbox devices (gamepads, keyboards) for insertions and removals
*
* ============================================================================
*/
void UXboxClient::DetectDevices()
{
	guardSlow(UXboxClient::DetectDevices);

	extern FXboxGamepadState	Gamepads[4];
	extern FXboxKeyboard		Keyboards[4];
	//jg -- For removal/insertion during movie playback
	extern UBOOL GamepadRemovedDuringMovie[4];
	extern UBOOL GamepadAddedDuringMovie[4];

	// gamepad device insertions/removals bit masks
	DWORD	GamepadInsertionMask,
			GamepadRemovalMask;			

	// get gamepads' device state
	XGetDeviceChanges(XDEVICE_TYPE_GAMEPAD,&GamepadInsertionMask,&GamepadRemovalMask);	

	for (INT Index = 0;Index < NUM_GAMEPADS; Index++)
	{
		Engine->ActiveControllerId = Index; // ron

		// Handle gamepad removal.
		if ((Gamepads[Index].Handle && GamepadRemovalMask & (1 << Index)) ||
			GamepadRemovedDuringMovie[Index])
		{
			// find viewport that is using this gamepad
			UViewport* GamepadViewport = GetGamepadViewport(Index);
			if( GamepadViewport )
			{
				//scion jg -- Send the removal of the joy stick as a keypress
				Engine->InputEvent(GamepadViewport,IK_JoySlider2,IST_Press);
				Engine->InputEvent(GamepadViewport,IK_JoySlider2,IST_Release);
			}
			else
			{	
				// if no viewport was mapped to the gamepad then just send an event
				// for the gamepad removal
				checkSlow(InteractionMaster);
				InteractionMaster->MasterProcessKeyEvent(IK_JoySlider2, IST_Press, 0.0f );
				InteractionMaster->MasterProcessKeyEvent(IK_JoySlider2, IST_Release, 0.0f );
			}
			// It's possible that the movie thread destroyed the handle
			if (Gamepads[Index].Handle != NULL)
			{
				XInputClose(Gamepads[Index].Handle);
				Gamepads[Index].Handle = NULL;
			}

			// clear any buttons that were being held down
			appMemzero(Gamepads[Index].ButtonPosition, ARRAY_COUNT(Gamepads[Index].ButtonPosition));
			GamepadRemovedDuringMovie[Index] = 0;
		}

		// Handle gamepad insertion.
		if ((!Gamepads[Index].Handle && GamepadInsertionMask & (1 << Index)) ||
			GamepadAddedDuringMovie[Index])
		{
			// It's possible that the movie thread created the handle
			if (Gamepads[Index].Handle == NULL)
			{
				Gamepads[Index].Handle = XInputOpen(XDEVICE_TYPE_GAMEPAD,Index,
					XDEVICE_NO_SLOT,NULL);
			}

			// find viewport that was using this gamepad
			UViewport* GamepadViewport = GetGamepadViewport(Index);
			if( GamepadViewport )
			{
				//scion jg -- Wire this gamepad to the playercontroller
				//scion jg -- Send the addition of the gamepad as a keypress
				Engine->InputEvent(GamepadViewport,IK_JoySlider1,IST_Press);
				Engine->InputEvent(GamepadViewport,IK_JoySlider1,IST_Release);
			}
			else
			{	
				// send an event for the gamepad insertion
				checkSlow(InteractionMaster);
				InteractionMaster->MasterProcessKeyEvent(IK_JoySlider1, IST_Press, 0.0f );
				InteractionMaster->MasterProcessKeyEvent(IK_JoySlider1, IST_Release, 0.0f );
			}			
			GamepadAddedDuringMovie[Index] = 0;
		}
	}

#ifndef FINAL_RELEASE
	// keyboard device insertions/removals bit masks
	DWORD	KeyboardInsertionMask,
			KeyboardRemovalMask;

	// get keyboards' device state
	XGetDeviceChanges(XDEVICE_TYPE_DEBUG_KEYBOARD,&KeyboardInsertionMask,&KeyboardRemovalMask);

	for (INT Index = 0;Index < NUM_GAMEPADS; Index++)
	{
		// Handle keyboard insertion
		if(Keyboards[Index].Handle && (KeyboardRemovalMask & (1 << Index)))
		{
			XInputClose(Keyboards[Index].Handle);
			Keyboards[Index].Handle = NULL;			
		}

		// Handle keyboard removal
		if(!Keyboards[Index].Handle && (KeyboardInsertionMask & (1 << Index)))
			Keyboards[Index].Handle = XInputOpen(XDEVICE_TYPE_DEBUG_KEYBOARD,Index,XDEVICE_NO_SLOT,NULL);
	}
#endif //FINAL_RELEASE

	unguardSlow;
}

/* scion ======================================================================
* UXboxClient::UpdateKeyboardInput
* Author: sz
*
* poll keyboard(s) for input and send key input events
*
* ============================================================================
*/
void UXboxClient::UpdateKeyboardInput()
{
	guardSlow(UXboxClient::UpdateKeyboardInput);
#ifndef FINAL_RELEASE
	extern FXboxKeyboard		Keyboards[4];

	for (INT Index = 0;Index < NUM_GAMEPADS; Index++)
	{
		if(Keyboards[Index].Handle)
		{
			XINPUT_DEBUG_KEYSTROKE Key;
			while( XInputDebugGetKeystroke( &Key ) == ERROR_SUCCESS )
			{ 
				if( Key.Flags & XINPUT_DEBUG_KEYSTROKE_FLAG_KEYUP )
					Engine->InputEvent( Viewports(0), (EInputKey) Key.VirtualKey, IST_Release, 0 );
				else
				{
					Engine->InputEvent( Viewports(0), (EInputKey) Key.VirtualKey, IST_Press, 0 );
					if( Key.Ascii!=IK_Enter )
						Engine->Key( Viewports(0), (EInputKey) Key.Ascii, TCHAR(Key.Ascii) );
				}
			}
		}
	}
#endif
	unguardSlow;
}

/* scion ======================================================================
* UXboxClient::UpdateGamepadUnboundInput
* Author: sz
*
* Only gamepads which are not bound to viewports are updated.
* Gamepads that are bound to viewports/playercontrollers are updated by
* UXboxViewport::UpdateInput when the player controller that uses them is ticked.
*
* ============================================================================
*/
void UXboxClient::UpdateGamepadUnboundInput()
{
	guardSlow(UXboxClient::UpdateGamepadUnboundInput);

	extern FXboxGamepadState	Gamepads[4];
	extern EInputKey			GamepadButtonMap[];

/* ron --	
	We need to query all buttons so that attract mode ends when input is received from any gamepad.
	It is not required that thumbstick movements interrupt attract mode, so we don't need to check for 
	joystick input.

	// list of buttons to query
	static const ValidButtons[] = { XGB_Start };
	static const NumValidButtons = ARRAY_COUNT(ValidButtons);	
*/	
	for (INT Index = 0;Index < NUM_GAMEPADS; Index++)
	{
		// see if this gamepad is already in use
		if( Gamepads[Index].Handle && !IsGamepadInUse(Index) )
		{
			// if it isn't then check for a start button press
			XINPUT_STATE & State = Gamepads[Index].State;
			if( XInputGetState(Gamepads[Index].Handle,&State) == ERROR_SUCCESS )
			{
				Engine->ActiveControllerId = Index;

/*				for( INT ValidButton=0; ValidButton < NumValidButtons; ValidButton++ )
				{
					INT ButtonIndex = ValidButtons[ValidButton];
*/
				for ( INT ButtonIndex = 0; ButtonIndex < ARRAY_COUNT(Gamepads[Index].ButtonPosition); ButtonIndex++ )
				{
					BYTE OldPosition = Gamepads[Index].ButtonPosition[ButtonIndex];

					if(ButtonIndex < 8)
						Gamepads[Index].ButtonPosition[ButtonIndex] = State.Gamepad.bAnalogButtons[ButtonIndex];
					else
						Gamepads[Index].ButtonPosition[ButtonIndex] = (State.Gamepad.wButtons & (1 << (ButtonIndex-8))) ? 255 : 0;

					if(OldPosition <= XINPUT_GAMEPAD_MAX_CROSSTALK &&
						Gamepads[Index].ButtonPosition[ButtonIndex] > XINPUT_GAMEPAD_MAX_CROSSTALK )
					{
						// button was pressed
						UBOOL bProcessed=false;

						// the button was the start button, and we're currently allowed to add new players
						if( ButtonIndex == XGB_Start && AllowControllerJoin() )
						{
							// first check all existing viewport's GamepadID
							for ( INT ViewportIndex = 0; ViewportIndex < Viewports.Num(); ViewportIndex++ )
							{
								UViewport* Viewport = Viewports(ViewportIndex);
								if ( Viewport->GetGamepadID() == UViewport::INVALID_GAMEPAD_ID )
								{
									// if we find a viewport with an invalid gamepadID, link up
									// this gamepad to that viewport
									Viewport->SetGamepadID(Index);
									bProcessed = true;
									break;
								}
							}

							// if all existing viewports already have valid gamepad IDs
							// request a new split-screen viewport for this unbound gamepad
							// and capture the input event if a split-screen was successfully created
							if ( !bProcessed )
								bProcessed = BeginSplitScreen( this, Index );
							else
							{
								// if we're doing the initial gamepad-to-viewport binding (at startup), we don't want
								// to swallow this keypress - unset bProcessed so that the GUI also receives the event
								// and progresses past the title menu...otherwise, the user would need to press start twice
								bProcessed = false;

								// note: 
								// this is not the absolute correct way to do it (ideally, we would pass the input
								// to the GUI regardless, and allow the GUI to determine whether to further process it)
								// but we can cheat since we know that this is how we want this to work (at least for now)
							}
						}

						// pass along unprocessed key events to the interaction system...
						// it will determine whether to further process input from unbound gamepads
						// note:only the interaction system will ever see input from unbound gamepads
						//		we should never send input from unbound gamepads to the engine's input system
						if ( !bProcessed )
						{
							checkSlow(InteractionMaster);
							InteractionMaster->MasterProcessKeyEvent(GamepadButtonMap[ButtonIndex], IST_Press, 0.0f );
						}
					}
					else if(OldPosition > XINPUT_GAMEPAD_MAX_CROSSTALK &&
						Gamepads[Index].ButtonPosition[ButtonIndex] <= XINPUT_GAMEPAD_MAX_CROSSTALK )
					{
						// button released
						// pass along unprocessed key events to the interaction system...
						// it will determine whether to further process input from unbound gamepads
						// note:only the interaction system will ever see input from unbound gamepads
						//		we should never send input from unbound gamepads to the engine's input system
						checkSlow(InteractionMaster);
						InteractionMaster->MasterProcessKeyEvent(GamepadButtonMap[ButtonIndex], IST_Release, 0.0f );
					}

				}
			}
		}
	}

	unguardSlow;
}

