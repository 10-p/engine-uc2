/*=============================================================================
	SDL2Client.cpp: USDL2Client, the SDL2 viewport manager.

	Behaviour follows Epic's UWindowsClient (Source/WinDrv/Src/WinClient.cpp); the SDL
	calls come from engine-ut2004's SDL2Drv. The Windows message pump of the PC build
	(CMainLoop in UnEngineWin.h + WWindowsViewportWindow::WndProc) becomes PumpEvents,
	which hands each SDL event to the viewport that owns its window.
=============================================================================*/

#include "SDL2Drv.h"

/*-----------------------------------------------------------------------------
	Class implementation.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(USDL2Client);

/*-----------------------------------------------------------------------------
	USDL2Client implementation.
-----------------------------------------------------------------------------*/

USDL2Client::USDL2Client()
{
	guard(USDL2Client::USDL2Client);
	InMenuLoop = 0;
	unguard;
}

//
// Static init: the configuration keys of [WinDrv.WindowsClient].
//
void USDL2Client::StaticConstructor()
{
	guard(USDL2Client::StaticConstructor);

	new(GetClass(),TEXT("UseJoystick"),       RF_Public)UBoolProperty(CPP_PROPERTY(UseJoystick),       TEXT("Display"),  CPF_Config );
	new(GetClass(),TEXT("StartupFullscreen"), RF_Public)UBoolProperty(CPP_PROPERTY(StartupFullscreen), TEXT("Display"),  CPF_Config );
	new(GetClass(),TEXT("MouseXMultiplier"),  RF_Public)UFloatProperty(CPP_PROPERTY(MouseXMultiplier),  TEXT("Display"),  CPF_Config );
	new(GetClass(),TEXT("MouseYMultiplier"),  RF_Public)UFloatProperty(CPP_PROPERTY(MouseYMultiplier),  TEXT("Display"),  CPF_Config );

	// Compiled defaults for the inherited UClient settings, equal to Default.ini's [SDL2Drv.SDL2Client]
	// (a copy of [WinDrv.WindowsClient]). UClient::StaticConstructor registers them without values, so
	// an ini that has no section for this client (Epic's, the retail one, an old Avaris.ini) would
	// otherwise get a 0x0 window, black gamma and the effects switched off (engine-ut2004 hit the same).
	WindowedViewportX	= 640;
	WindowedViewportY	= 480;
	FullscreenViewportX	= 640;
	FullscreenViewportY	= 480;
	Brightness			= 0.8f;
	Contrast			= 0.7f;
	Gamma				= 0.8f;
	CaptureMouse		= 1;
	ScreenFlashes		= 1;
	NoLighting			= 0;
	MinDesiredFrameRate	= 35.0f;
	Decals				= 1;
	Coronas				= 1;
	DecoLayers			= 1;
	Projectors			= 1;
	NoDynamicLights		= 0;
	ReportDynamicUploads= 0;
	NoFractalAnim		= 0;
	for( INT i=0; i<LODSET_MAX; i++ )
		TextureLODSet[i] = 4;					// "Normal" in UClient's ETextureDetail list
	TextureLODSet[LODSET_Lightmap] = 0;			// "UltraHigh"
	UseJoystick			= 0;
	StartupFullscreen	= 0;
	MouseXMultiplier	= 1.0f;
	MouseYMultiplier	= 1.0f;

	unguard;
}

//
// Initialize the platform-specific viewport manager subsystem.
// Must be called after the Unreal object manager has been initialized.
// Must be called before any viewports are created.
//
void USDL2Client::Init( UEngine* InEngine )
{
	guard(USDL2Client::Init);

	// Init base.
	UClient::Init( InEngine );

	// Video only. Pads (SDL_INIT_GAMECONTROLLER) join at M10, see PollGamepads.
	if( SDL_InitSubSystem( SDL_INIT_VIDEO ) != 0 )
		appErrorf( TEXT("Couldn't initialize SDL2 video: %s"), appFromAnsi(SDL_GetError()) );
	debugf( NAME_Init, TEXT("SDL2 video driver: %s"), appFromAnsi(SDL_GetCurrentVideoDriver()) );

	// Text input (WM_CHAR's role). SDL enables it on desktop by default; say so explicitly.
	SDL_StartTextInput();

	USDL2Viewport::InitKeyMap();

	// Note configuration.
	PostEditChange();

	// Default res option.
	if( ParseParam(appCmdLine(),TEXT("defaultres")) )
	{
		WindowedViewportX  = FullscreenViewportX  = 640;
		WindowedViewportY  = FullscreenViewportY  = 480;
	}

	// Success.
	debugf( NAME_Init, TEXT("Client initialized") );
	unguard;
}

//
// Shut down the platform-specific viewport manager subsystem.
//
void USDL2Client::Destroy()
{
	guard(USDL2Client::Destroy);

	// Make sure to shut down Viewports first.
	for( INT i=0; i<Viewports.Num(); i++ )
		Viewports(i)->ConditionalDestroy();

	// Shut down GRenDev.
	if( Engine && Engine->GRenDev )
		Engine->GRenDev->Exit(NULL);

	SDL_QuitSubSystem( SDL_INIT_VIDEO );

	debugf( NAME_Exit, TEXT("SDL2 client shut down") );
	Super::Destroy();
	unguard;
}

//
// Failsafe routine to shut down viewport manager subsystem
// after an error has occured. Not guarded.
//
void USDL2Client::ShutdownAfterError()
{
	debugf( NAME_Exit, TEXT("Executing USDL2Client::ShutdownAfterError") );
	SDL_SetRelativeMouseMode( SDL_FALSE );
	if( Engine && Engine->Audio )
		Engine->Audio->ConditionalShutdownAfterError();
	for( INT i=Viewports.Num()-1; i>=0; i-- )
	{
		USDL2Viewport* Viewport = (USDL2Viewport*)Viewports( i );
		Viewport->ConditionalShutdownAfterError();
	}
	Super::ShutdownAfterError();
}

void USDL2Client::NotifyDestroy( void* Src )
{
	guard(USDL2Client::NotifyDestroy);
	unguard;
}

//
// Command line.
//
UBOOL USDL2Client::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(USDL2Client::Exec);
	if( UClient::Exec( Cmd, Ar ) )
		return 1;
	return 0;
	unguard;
}

//
// The message pump: every pending SDL event goes to the viewport whose window it
// names (keyboard and text events name the focused window). Called by the launcher's
// main loop once a frame and by each viewport's UpdateInput.
//
void USDL2Client::PumpEvents()
{
	guard(USDL2Client::PumpEvents);
	SDL_Event Ev;
	while( SDL_PollEvent(&Ev) )
	{
		DWORD WindowID = 0;
		switch( Ev.type )
		{
			case SDL_QUIT:
				// The last window was closed (or SIGINT/SIGTERM): what WM_QUIT does on Windows.
				debugf( NAME_Log, TEXT("SDL_QUIT received") );
				appRequestExit( 0 );
				continue;
			case SDL_WINDOWEVENT:		WindowID = Ev.window.windowID;	break;
			case SDL_KEYDOWN:
			case SDL_KEYUP:				WindowID = Ev.key.windowID;		break;
			case SDL_TEXTINPUT:			WindowID = Ev.text.windowID;	break;
			case SDL_MOUSEMOTION:		WindowID = Ev.motion.windowID;	break;
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:		WindowID = Ev.button.windowID;	break;
			case SDL_MOUSEWHEEL:		WindowID = Ev.wheel.windowID;	break;
			default:
				continue;
		}
		USDL2Viewport* Viewport = FindViewport( WindowID );
		if( !Viewport && LastCurrent && Viewports.FindItemIndex(LastCurrent)!=INDEX_NONE )
			Viewport = (USDL2Viewport*)LastCurrent;	// e.g. relative mouse motion with no window id
		if( Viewport && Viewport->Actor )
			Viewport->HandleEvent( Ev );
	}
	unguard;
}

USDL2Viewport* USDL2Client::FindViewport( DWORD WindowID )
{
	guard(USDL2Client::FindViewport);
	if( WindowID )
	{
		for( INT i=0; i<Viewports.Num(); i++ )
		{
			USDL2Viewport* Viewport = (USDL2Viewport*)Viewports(i);
			if( Viewport->Window && SDL_GetWindowID(Viewport->Window)==WindowID )
				return Viewport;
		}
	}
	return NULL;
	unguard;
}

//
// M10 HOOK -- game pads. Deliberately empty at M4.
//
// What belongs here (spec: Source/XboxDrv/Src/XboxClient.cpp and XboxViewport.cpp):
//  - SDL_INIT_GAMECONTROLLER in Init, SDL_CONTROLLERDEVICEADDED/REMOVED in PumpEvents, mapped to
//    pad slots 0..3 and announced as IK_JoySlider1/IK_JoySlider2 (UXboxClient::DetectDevices).
//  - Pads bound to a viewport (UViewport::GamepadID) are read by that viewport's UpdateInput with
//    Engine->ActiveControllerId set to the pad, buttons through GamepadButtonMap, sticks as
//    IK_JoyX/Y/U/V axes (UXboxViewport::UpdateInput).
//  - Pads bound to no viewport go to InteractionMaster->MasterProcessKeyEvent / MasterProcessJoyInput
//    only, and Start on an unbound pad may join a split-screen player (UClient::BeginSplitScreen,
//    UXboxClient::UpdateGamepadUnboundInput).
//  - Rumble from APlayerController::WaveformManager (UXboxViewport::UpdateGamepadRumble).
// Keyboard and mouse never change Engine->ActiveControllerId: on PC they are pad 0, the id
// ULevel::SpawnPlayActor gives the first viewport ("fake the GamepadID", UnLevAct.cpp).
//
void USDL2Client::PollGamepads( FLOAT DeltaSeconds )
{
}

//
// Perform timer-tick processing on all visible viewports.  This causes
// all realtime viewports, and all non-realtime viewports which have been
// updated, to be blitted.
//
void USDL2Client::Tick( FLOAT DeltaSeconds )
{
	guard(USDL2Client::Tick);

	PollGamepads( DeltaSeconds );

	// Blit any viewports that need blitting.
	for( INT i=0; i<Viewports.Num(); i++ )
	{
		USDL2Viewport* Viewport = CastChecked<USDL2Viewport>(Viewports(i));
		if( (Viewport->IsRealtime() || (Viewport->DirtyViewport != 0)) && Viewport->SizeX && Viewport->SizeY )
			Viewport->Repaint( (Viewport->DirtyViewport == -1) ? 0 : 1, DeltaSeconds );
	}

	unguard;
}

//
// Create a new viewport.
//
UViewport* USDL2Client::NewViewport( const FName Name )
{
	guard(USDL2Client::NewViewport);
	return new( this, Name )USDL2Viewport();
	unguard;
}

//
// Configuration change.
//
void USDL2Client::PostEditChange()
{
	guard(USDL2Client::PostEditChange);
	Super::PostEditChange();
	unguard;
}

//
// Enable or disable all viewport windows that have ShowFlags set (or all if ShowFlags=0).
// Only the editor uses this; an SDL window has no disabled state.
//
void USDL2Client::EnableViewportWindows( DWORD ShowFlags, int DoEnable )
{
	guard(USDL2Client::EnableViewportWindows);
	unguard;
}

//
// Show or hide all viewport windows that have ShowFlags set (or all if ShowFlags=0).
//
void USDL2Client::ShowViewportWindows( DWORD ShowFlags, int DoShow )
{
	guard(USDL2Client::ShowViewportWindows);
	for( INT i=0; i<Viewports.Num(); i++ )
	{
		USDL2Viewport* Viewport = (USDL2Viewport*)Viewports(i);
		if( Viewport->Window && Viewport->Actor && (Viewport->Actor->ShowFlags & ShowFlags)==ShowFlags )
		{
			if( DoShow )
				SDL_ShowWindow( Viewport->Window );
			else
				SDL_HideWindow( Viewport->Window );
		}
	}
	unguard;
}

//
// Make this viewport the current one.
// If Viewport=0, makes no viewport the current one.
//
void USDL2Client::MakeCurrent( UViewport* InViewport )
{
	guard(USDL2Client::MakeCurrent);
	for( INT i=0; i<Viewports.Num(); i++ )
	{
		UViewport* OldViewport = Viewports(i);
		if( OldViewport->Current && OldViewport!=InViewport )
		{
			OldViewport->Current = 0;
			OldViewport->UpdateWindowFrame();
		}
	}
	if( InViewport )
	{
		LastCurrent = InViewport;
		InViewport->Current = 1;
		InViewport->UpdateWindowFrame();
	}
	unguard;
}

//
// Returns a pointer to the viewport that was last current.
//
UViewport* USDL2Client::GetLastCurrent()
{
	guard(USDL2Client::GetLastCurrent);
	return LastCurrent;
	unguard;
}

//
// Initial size of the screen: the window size when windowed, the fullscreen size otherwise
// (UWindowsClient::GetInitScreenSize).
//
void USDL2Client::GetInitScreenSize( INT& SizeX, INT& SizeY )
{
	guard(USDL2Client::GetInitScreenSize);

	if( Viewports.Num() && Viewports(0)->IsFullscreen() )
	{
		SizeX = FullscreenViewportX;
		SizeY = FullscreenViewportY;
	}
	else
	{
		SizeX = WindowedViewportX;
		SizeY = WindowedViewportY;
	}

	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
