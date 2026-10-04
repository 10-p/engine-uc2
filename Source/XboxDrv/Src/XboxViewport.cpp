/*=============================================================================
	XboxViewport.cpp: UXboxViewport code.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "XboxDrv.h"
#include "d3d8perf.h"

//
//	Input remapping tables.
//

EInputKey	GamepadButtonMap[] =
{
	IK_Joy1,
	IK_Joy2,
	IK_Joy3,
	IK_Joy4,
	IK_Joy5,
	IK_Joy6,
	IK_Joy7,
	IK_Joy8,
	IK_Joy9,
	IK_Joy10,
	IK_Joy11,
	IK_Joy12,
	IK_Joy13,
	IK_Joy14,
	IK_Joy15,
	IK_Joy16,
	//scion jg -- These two events aren't real. They are to signify the
	// the controller being plugged in or removed
	IK_JoySlider1, // == Gamepad Added
	IK_JoySlider2,  // == Gamepad Removed

	// neither is this - used to signal that we are shutting down and connections
	// to xbox live should be closed
	IK_JoyR,
};

//scion jg -- Moved these here so they could be shared across threads during
// movie playback. These aren't synchronized because only one thread at a
// time can access them anyway. If this assumption changes, then thread
// synchronization needs to be added
FXboxGamepadState	Gamepads[NUM_GAMEPADS];

FXboxKeyboard		Keyboards[4];

/*-----------------------------------------------------------------------------
	Class implementation.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(UXboxViewport);

/*-----------------------------------------------------------------------------
	UXboxViewport Init/Exit.
-----------------------------------------------------------------------------*/

//
// Constructor.
//
UXboxViewport::UXboxViewport()
: UViewport()
{
}

//
// Destroy.
//
void UXboxViewport::Destroy()
{
	guard(UXboxViewport::Destroy);

	Super::Destroy();

	unguard;
}

//
// Error shutdown.
//
void UXboxViewport::ShutdownAfterError()
{
	Super::ShutdownAfterError();
}

/*-----------------------------------------------------------------------------
	Command line.
-----------------------------------------------------------------------------*/

//
// Command line.
//
UBOOL UXboxViewport::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UXboxViewport::Exec);

	if (UViewport::Exec(Cmd, Ar))
	{
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("GETCURRENTRES")) )
	{
		Ar.Logf( TEXT("%ix%i"), Width, Height );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("GETCURRENTCOLORDEPTH")) )
	{
		Ar.Logf( TEXT("32") );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("GETCOLORDEPTHS")) )
	{
		Ar.Log( TEXT("32") );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("GETCURRENTRENDERDEVICE")) )
	{
		Ar.Log( RenDev->GetClass()->GetPathName() );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("SETRES")) )
	{
		INT X=appAtoi(Cmd);
		TCHAR* CmdTemp = appStrchr(Cmd,'x') ? appStrchr(Cmd,'x')+1 : appStrchr(Cmd,'X') ? appStrchr(Cmd,'X')+1 : TEXT("");
		INT Y=appAtoi(CmdTemp);
		Cmd = CmdTemp;
		CmdTemp = appStrchr(Cmd,'x') ? appStrchr(Cmd,'x')+1 : appStrchr(Cmd,'X') ? appStrchr(Cmd,'X')+1 : TEXT("");
		if( X && Y )
		{
			UBOOL Result = RenDev->SetRes( this, X, Y, 1 );
			if( !Result )
			{
				debugf(TEXT("SetRes failed"));
			}
		}
		return 1;
	}
#if defined(_DEBUG) || defined(PROFILE)
	else if( ParseCommand(&Cmd,TEXT("PROFILING")) )
	{
		if( ParseCommand(&Cmd,TEXT("START")) )
		{
			D3DPERF_StartPerfProfile();
			return 1;
		}
		if( ParseCommand(&Cmd,TEXT("STOP")) )
		{
			D3DPERF_StopPerfProfile();
			return 1;
		}
		if( ParseCommand(&Cmd,TEXT("RESET")) )
		{
			D3DPERF_Reset();
			return 1;
		}
		if( ParseCommand(&Cmd,TEXT("DUMP")) )
		{
			D3DPERF_Dump();
			return 1;
		}
		return 0;
	}
	else if( ParseCommand(&Cmd, TEXT("D3DPERF")) )
	{
		INT MSecs=appAtoi(Cmd);
		D3DPERF_GetStatistics()->m_DumpWaitCycleTimesThreshold = MSecs;
		return 1;
	}
#endif
	else if( ParseCommand(&Cmd,TEXT("XBOXMEM")) )
	{
		// Print some memory stats.
		MEMORYSTATUS MemStatus;
		GlobalMemoryStatus( &MemStatus );
#define DETAILED_MEMSTATS 1
#if DETAILED_MEMSTATS
		debugf(TEXT("Memory Statistics\n"));
		debugf(TEXT("%8d total KByte of virtual memory"		),	MemStatus.dwTotalVirtual	/ 1024 );
		debugf(TEXT("%8d  free KByte of virtual memory."	),	MemStatus.dwAvailVirtual	/ 1024 );
		debugf(TEXT("%8d total KByte of physical memory."	),	MemStatus.dwTotalPhys		/ 1024 );
		debugf(TEXT("%8d  free KByte of physical memory."	),	MemStatus.dwAvailPhys		/ 1024 );
		debugf(TEXT("%8d total KByte of paging file."		),	MemStatus.dwTotalPageFile	/ 1024 );
		debugf(TEXT("%8d  free KByte of paging file."		),	MemStatus.dwAvailPageFile	/ 1024 );
		debugf(TEXT("%8d percent of memory is in use."		),	MemStatus.dwMemoryLoad );
		debugf(TEXT("\n"));
#else
		debugf (TEXT("%d KByte of physical memory in use"), (MemStatus.dwTotalPhys - MemStatus.dwAvailPhys) / 1024 );
		Ar.Logf(TEXT("%d KByte of physical memory in use"), (MemStatus.dwTotalPhys - MemStatus.dwAvailPhys) / 1024 );
#endif
		return 1;
	}
	//scion jg -- HDTV mode switches. Annoying but needs to be here though it
	// would be better in UD3DRenderDevice
	else if (ParseCommand(&Cmd,TEXT("HDTV")))
	{
		UD3DRenderDevice* pRenDev = (UD3DRenderDevice*)RenDev;
		// Figure out which modes are possible
		DWORD dwFlags = XGetVideoFlags();
		UBOOL bReset = 0;
		// Figure out which mode they want
		if (ParseCommand(&Cmd,TEXT("480")))
		{
			bReset = 1;
			// Turns off HDTV
			pRenDev->PresentParms.BackBufferWidth = 640;
			pRenDev->PresentParms.BackBufferHeight = 480;
			pRenDev->PresentParms.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER |
				D3DPRESENTFLAG_10X11PIXELASPECTRATIO | D3DPRESENTFLAG_INTERLACED;
		}
		else if (ParseCommand(&Cmd,TEXT("480p")) &&
			dwFlags & XC_VIDEO_FLAGS_HDTV_480p)
		{
			bReset = 1;
			pRenDev->PresentParms.BackBufferWidth = 640;
			pRenDev->PresentParms.BackBufferHeight = 480;
			pRenDev->PresentParms.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER |
				D3DPRESENTFLAG_10X11PIXELASPECTRATIO | D3DPRESENTFLAG_PROGRESSIVE;
		}
		else if (ParseCommand(&Cmd,TEXT("480p_big")) &&
			dwFlags & XC_VIDEO_FLAGS_HDTV_480p)
		{
			bReset = 1;
			pRenDev->PresentParms.BackBufferWidth = 720;
			pRenDev->PresentParms.BackBufferHeight = 480;
			pRenDev->PresentParms.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER |
				D3DPRESENTFLAG_PROGRESSIVE;
		}
		else if (ParseCommand(&Cmd,TEXT("720p")) &&
			dwFlags & XC_VIDEO_FLAGS_HDTV_720p)
		{
			bReset = 1;
			pRenDev->PresentParms.BackBufferWidth = 1280;
			pRenDev->PresentParms.BackBufferHeight = 720;
			pRenDev->PresentParms.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER |
				D3DPRESENTFLAG_PROGRESSIVE | D3DPRESENTFLAG_WIDESCREEN;
		}
		else if (ParseCommand(&Cmd,TEXT("1080i")) &&
			dwFlags & XC_VIDEO_FLAGS_HDTV_1080i)
		{
			bReset = 1;
			pRenDev->PresentParms.BackBufferWidth = 1920;
			pRenDev->PresentParms.BackBufferHeight = 1080;
			pRenDev->PresentParms.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER |
				D3DPRESENTFLAG_INTERLACED | D3DPRESENTFLAG_WIDESCREEN;
		}
		if (bReset == 1)
		{
			// Flush everything so that backbuffer textures are released
			pRenDev->Flush(this);
			// Reset the device
			D3DDevice_Reset(&pRenDev->PresentParms);
			// Now set the viewport boundaries since they've changed
			SizeX = pRenDev->PresentParms.BackBufferWidth;
			SizeY = pRenDev->PresentParms.BackBufferHeight;
			// update the DefaultRenderTarget with the new frame buffer surfaces
			pRenDev->DefaultRenderTarget->UpdateSurfaces();
			// all camera effects must update their render target surfaces
			check(Actor);
			for( INT i = 0; i < Actor->CameraEffects.Num(); i++ )
			{
				check( Actor->CameraEffects(i) );
				Actor->CameraEffects(i)->DestroyRenderTargets();
			}
		}
		return 1;
	}
	//scion jg -- FSAA mode switches
	else if (ParseCommand(&Cmd,TEXT("FSAA")))
	{
		UD3DRenderDevice* pRenDev = (UD3DRenderDevice*)RenDev;
		UBOOL bReset = 0;
		// Figure out which mode they want
		if (ParseCommand(&Cmd,TEXT("OFF")))
		{
			bReset = 1;
			// Turns off FSAA
			pRenDev->PresentParms.MultiSampleType = 0;
		}
		else if (ParseCommand(&Cmd,TEXT("ON1")))
		{
			bReset = 1;
			pRenDev->PresentParms.MultiSampleType = D3DMULTISAMPLE_2_SAMPLES;
		}
		else if (ParseCommand(&Cmd,TEXT("ON2")))
		{
			bReset = 1;
			pRenDev->PresentParms.MultiSampleType = D3DMULTISAMPLE_4_SAMPLES;
		}
		else if (ParseCommand(&Cmd,TEXT("ON3")))
		{
			bReset = 1;
			pRenDev->PresentParms.MultiSampleType = D3DMULTISAMPLE_4_SAMPLES_SUPERSAMPLE_GAUSSIAN;
		}
		if (bReset == 1)
		{
			guard(FSAA::Reset);
			// Flush everything so that backbuffer textures are released
			pRenDev->Flush(this);
			// Reset the device
			D3DDevice_Reset(&pRenDev->PresentParms);
			// update the DefaultRenderTarget with the new frame buffer surfaces
			pRenDev->DefaultRenderTarget->UpdateSurfaces();
			// set FSAA scale values for new render targets
			pRenDev->FSAAScale[0] = (FLOAT)pRenDev->DefaultRenderTarget->GetWidth() / (FLOAT) pRenDev->FullScreenWidth;
			pRenDev->FSAAScale[1] = (FLOAT)pRenDev->DefaultRenderTarget->GetHeight() / (FLOAT) pRenDev->FullScreenHeight;
			// all camera effects must update their render target surfaces
			check(Actor);
			for( INT i = 0; i < Actor->CameraEffects.Num(); i++ )
			{
				check( Actor->CameraEffects(i) );
				Actor->CameraEffects(i)->DestroyRenderTargets();
			}
			unguard;

		}
		return 1;
	}
	else
	{
		return 0;
	}

	unguard;
}

void UXboxViewport::SetClientTravel(const TCHAR* NextURL, UBOOL bItems, ETravelType NextTravelType)
{
	guard(UXboxViewport::SetClientTravel);

	Super::SetClientTravel(NextURL, bItems, NextTravelType);

	// make sure there's no rumble going on so it doesn't persist while the map is loaded
	if (GamepadID != INVALID_GAMEPAD_ID)
	{
		Gamepads[GamepadID].Feedback.Rumble.wLeftMotorSpeed = 0;
		Gamepads[GamepadID].Feedback.Rumble.wRightMotorSpeed = 0;

		if ( Gamepads[GamepadID].Handle )
			XInputSetState(Gamepads[GamepadID].Handle, &Gamepads[GamepadID].Feedback);
	}
	else
	{
		debugf(NAME_Warning, TEXT("Invalid GamepadID in SetClientTravel()"));
	}

	unguard;
}

/* scion ======================================================================
 * UXboxClient::UpdateGamepadRumble
 * Author: jg
 * 
 * Updates the motor speeds for the gamepad
 *
 * input:	flDelta FLOAT The amount of time that has elapsed since the last
 *			update
 *
 * ============================================================================
 */
void UXboxViewport::UpdateGamepadRumble(FLOAT flDelta)
{
	guard(UXboxViewport::UpdateGamepadRumble);

	// Get a pointer to the waveform manager
	UWaveformManager* pManager = ((APlayerController*)Actor)->WaveformManager;
	// If we have a waveform manager and we have a valid gamepad, then
	// go ahead and update the rumble data
	if (GamepadID != INVALID_GAMEPAD_ID)
	{
		if (pManager != NULL)
		{
			// Update the rumble data
			pManager->GetRumbleState(flDelta,
				Gamepads[GamepadID].Feedback.Rumble.wLeftMotorSpeed,
				Gamepads[GamepadID].Feedback.Rumble.wRightMotorSpeed);
		}
		else
		{
			// zero rumble so we're not rumbling forever if the UWaveformManager gets deleted while we were rumbling
			Gamepads[GamepadID].Feedback.Rumble.wLeftMotorSpeed = 0;
			Gamepads[GamepadID].Feedback.Rumble.wRightMotorSpeed = 0;
		} 
		// Update the motor with the data
		XInputSetState(Gamepads[GamepadID].Handle,
			&Gamepads[GamepadID].Feedback);
	}
	unguard;
}

//
//	Input handling
//
void UXboxViewport::UpdateInput( UBOOL Reset, FLOAT DeltaSeconds ) 
{
	guardSlow(UXboxViewport::UpdateInput);

	UXboxClient*	Client = GetOuterUXboxClient();
	
	//scion jg -- Check if there is a gamepad available. Upon level switch
	// the player controller is destroyed so it won't have a gamepad
	if (Actor->GamepadID == INVALID_GAMEPAD_ID &&
		GamepadID != INVALID_GAMEPAD_ID)
	{
		// The controller is new but the viewport already is linked to a gamepad
		SetGamepadID(GamepadID);
	}
	// scion sz -
	// handle gamepad change when player controller updates its gamepad id
	else if( Actor->GamepadID != GamepadID )
	{		
		SetGamepadID( Actor->GamepadID );
	}

#if 0
	// scion sz -
	// temp hack to initialize controller input for a viewport
	else if( GamepadID == INVALID_GAMEPAD_ID )
	{
		// find a controller that is not bound to a viewport
		for( INT i=0; i<NUM_GAMEPADS; i++ )
		{
			UViewport* GamepadViewport = Client->GetGamepadViewport(i);			
			
			if( !GamepadViewport || 
				GamepadViewport == this )
			{
				SetGamepadID(i);
				break;
			}
		}
	}
#endif

	// scion sz -
	// only handle input for gamepad bound to this viewport/playercontroller
	INT Index = GamepadID;
	if( Index < NUM_GAMEPADS && 
		Gamepads[Index].Handle && 
		IsOurGamepad(Index) )
	{
		Client->Engine->ActiveControllerId = Index;

		// Update device state.
		XINPUT_STATE & State = Gamepads[Index].State;

		if(XInputGetState(Gamepads[Index].Handle,&State) == ERROR_SUCCESS)
		{
			Gamepads[Index].LeftJoystick  = FVector(State.Gamepad.sThumbLX / 32768.0f, State.Gamepad.sThumbLY / 32768.0f,0.0f);
			Gamepads[Index].RightJoystick = FVector(State.Gamepad.sThumbRX / 32768.0f, State.Gamepad.sThumbRY / 32768.0f,0.0f);

			for(INT ButtonIndex = 0;ButtonIndex < 16;ButtonIndex++)
			{
				BYTE OldPosition = Gamepads[Index].ButtonPosition[ButtonIndex];

				if(ButtonIndex < 8)
					Gamepads[Index].ButtonPosition[ButtonIndex] = State.Gamepad.bAnalogButtons[ButtonIndex];
				else
					Gamepads[Index].ButtonPosition[ButtonIndex] = (State.Gamepad.wButtons & (1 << (ButtonIndex-8))) ? 255 : 0;

				if(OldPosition <= XINPUT_GAMEPAD_MAX_CROSSTALK &&
					Gamepads[Index].ButtonPosition[ButtonIndex] > XINPUT_GAMEPAD_MAX_CROSSTALK )
				{
					Client->Engine->InputEvent(this,GamepadButtonMap[ButtonIndex],IST_Press);
					Gamepads[Index].RepeatKey = GamepadButtonMap[ButtonIndex];
					Gamepads[Index].NextRepeatTime = appSeconds() + 0.2f;
				}
				else if(OldPosition > XINPUT_GAMEPAD_MAX_CROSSTALK &&
					Gamepads[Index].ButtonPosition[ButtonIndex] <= XINPUT_GAMEPAD_MAX_CROSSTALK )
				{
					Client->Engine->InputEvent(this,GamepadButtonMap[ButtonIndex],IST_Release);
					Gamepads[Index].RepeatKey = IK_None;
				}
			}

			if (!InteractionMaster->MasterProcessJoyInput(Index,IK_JoyX,Gamepads[Index].LeftJoystick.X) )
				Input->DirectAxis(IK_JoyX,Gamepads[Index].LeftJoystick.X,DeltaSeconds);

			if (!InteractionMaster->MasterProcessJoyInput(Index,IK_JoyY,Gamepads[Index].LeftJoystick.Y) )
				Input->DirectAxis(IK_JoyY,Gamepads[Index].LeftJoystick.Y,DeltaSeconds);

			if (!InteractionMaster->MasterProcessJoyInput(Index,IK_JoyU,Gamepads[Index].RightJoystick.X) )
				Input->DirectAxis(IK_JoyU,Gamepads[Index].RightJoystick.X,DeltaSeconds);

			if (!InteractionMaster->MasterProcessJoyInput(Index,IK_JoyV,Gamepads[Index].RightJoystick.Y) )
				Input->DirectAxis(IK_JoyV,Gamepads[Index].RightJoystick.Y,DeltaSeconds);			
		}

		if(Gamepads[Index].RepeatKey != IK_None)
		{
			if(CurrentTime >= Gamepads[Index].NextRepeatTime )
			{
				Client->Engine->InputEvent(this,Gamepads[Index].RepeatKey,IST_Hold);
				Gamepads[Index].NextRepeatTime = CurrentTime + 0.1f;
			}
		}

		// Update the waveform that is currently playing on our gamepad
		UpdateGamepadRumble(DeltaSeconds);
	}

	unguardSlow;
}

//
// Get localized key name.
//
TCHAR* UXboxViewport::GetLocalizedKeyName( EInputKey Key )
{
	guard(UXboxViewport::GetLocalizedKeyName);

	TCHAR* KeyName = appStaticString1024();
	appStrncpy( KeyName, TEXT("UNKNOWN XBOX KEY"), 1024 );

	return KeyName;

	unguard;
}

/*-----------------------------------------------------------------------------
	Window openining and closing.
-----------------------------------------------------------------------------*/

//
// Open this viewport's window.
//
void UXboxViewport::OpenWindow( DWORD InParentWindow, UBOOL IsTemporary, INT NewX, INT NewY, INT OpenX, INT OpenY )
{
	guard(UXboxViewport::OpenWindow);

	// Create the render device.
	RenDev = GetOuterUXboxClient()->Engine->GRenDev;	
	check(RenDev);

	// scion sz
	// don't initialize render device if we already have a parent window
	if( InParentWindow )
		return;

	// Initialize the render device.
	// scion sz - don't reset detail levels here since it is already done in UGameEngine::Init
	if( !RenDev->Init() || !RenDev->SetRes(this,640,480,1) )
		appErrorf(TEXT("Couldn't create render device!"));

	unguard;
}

//
// Close a viewport window.  Assumes that the viewport has been openened with
// OpenViewportWindow.  Does not affect the viewport's object, only the
// platform-specific information associated with it.
//
void UXboxViewport::CloseWindow()
{
	guard(UXboxViewport::CloseWindow);

	// scion sz
	// only for viewport(0)
	if( this != GetOuterUClient()->Viewports(0) )
		return;

	// close gamepad devices
	for(int Index = 0;Index < 4;Index++)
	{
		if(Gamepads[Index].Handle)
			XInputClose(Gamepads[Index].Handle);
	}

	// scion sz
	// also close keyboard devices
	for(int Index = 0;Index < 4;Index++)
	{
		if(Keyboards[Index].Handle)
			XInputClose(Keyboards[Index].Handle);
	}

	unguard;
}

//
// Resize the viewport.
//
UBOOL UXboxViewport::ResizeViewport( DWORD NewBlitFlags, INT InNewX, INT InNewY, UBOOL bSaveSize )
{
	guard(UXboxViewport::ResizeViewport);

	SizeX = InNewX;
	SizeY = InNewY;

	return 1;
	unguard;
}

//
// Repaint the viewport.
//
void UXboxViewport::Repaint( UBOOL Blit, FLOAT DeltaSeconds )
{
	guard(UXboxViewport::Repaint);
	GetOuterUXboxClient()->Engine->Draw( this, DeltaSeconds, Blit );
	unguard;
}

//
// Functions that do nothing.
//
void UXboxViewport::SetMouseCapture( UBOOL Capture, UBOOL Clip, UBOOL FocusOnly )
{
}

void* UXboxViewport::GetWindow()
{
	return NULL;
}

void UXboxViewport::SetModeCursor()
{
}

void UXboxViewport::UpdateWindowFrame()
{
}

UBOOL UXboxViewport::IsFullscreen()
{
	guard(UXboxViewport::IsFullscreen);
	
	UBOOL bFullScreen = 1;
	
	// scion sz -
	// viewport is fullscreen iff it matches the initial screen size
	UClient* Client = GetOuterUClient();
	INT InitScreenX, InitScreenY;
	Client->GetInitScreenSize( InitScreenX, InitScreenY );
	
	if( SizeX != InitScreenX || 
		SizeY != InitScreenY )
	{
        bFullScreen = 0;
	}

	return bFullScreen;

	unguard;
}

/* scion ======================================================================
 * UXboxClient::GetAnalogButtonAmount
 * Author: jg
 * 
 * Figures out how much the user has depressed the specified analog button
 *
 * input:	ButtonIndex The button to check the state of
 *
 * ============================================================================
 */
BYTE UXboxViewport::GetAnalogButtonAmount(INT ButtonIndex)
{
	guard(UXboxViewport::GetAnalogButtonAmount);
	check(ButtonIndex < 8);
	// Default to unpressed
	BYTE byDepressed = 0;
	if( ((APlayerController*)Actor)->GamepadID < NUM_GAMEPADS )
	{
		//epic superville: Make sure there is a valid game pad pointer at this index
		if( Gamepads[((APlayerController*)Actor)->GamepadID].Handle )
		{
			XINPUT_STATE & State = Gamepads[((APlayerController*)Actor)->GamepadID].State;
			// Read the input for the gamepad associated with this viewport
			if(XInputGetState(Gamepads[((APlayerController*)Actor)->GamepadID].Handle,&State) == ERROR_SUCCESS)
			{
				// Return the amount it is being depressed
				byDepressed = State.Gamepad.bAnalogButtons[ButtonIndex];
			}
		}
	}	
	return byDepressed;
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
