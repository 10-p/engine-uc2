/*=============================================================================
	SDL2Client.cpp: USDL2Client, the SDL2 viewport manager.

	Behaviour follows Epic's UWindowsClient (Source/WinDrv/Src/WinClient.cpp); the SDL
	calls come from engine-ut2004's SDL2Drv. The Windows message pump of the PC build
	(CMainLoop in UnEngineWin.h + WWindowsViewportWindow::WndProc) becomes PumpEvents,
	which hands each SDL event to the viewport that owns its window.
=============================================================================*/

#include "SDL2Drv.h"
#include "SDL2InputTest.h"

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
	PadsInitialized = 0;
	InputTest = NULL;
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

	// M10 pads: what the Xbox driver hard-codes, as settings (SDL2Pads.cpp, controls.md).
	new(GetClass(),TEXT("PadButtonThreshold"),RF_Public)UIntProperty  (CPP_PROPERTY(PadButtonThreshold),TEXT("Input"),    CPF_Config );
	new(GetClass(),TEXT("PadStickDeadZone"),  RF_Public)UFloatProperty(CPP_PROPERTY(PadStickDeadZone),  TEXT("Input"),    CPF_Config );
	new(GetClass(),TEXT("PadRepeatDelay"),    RF_Public)UFloatProperty(CPP_PROPERTY(PadRepeatDelay),    TEXT("Input"),    CPF_Config );
	new(GetClass(),TEXT("PadRepeatRate"),     RF_Public)UFloatProperty(CPP_PROPERTY(PadRepeatRate),     TEXT("Input"),    CPF_Config );
	new(GetClass(),TEXT("PadRumble"),         RF_Public)UBoolProperty (CPP_PROPERTY(PadRumble),         TEXT("Input"),    CPF_Config );
	new(GetClass(),TEXT("PadRumbleScale"),    RF_Public)UFloatProperty(CPP_PROPERTY(PadRumbleScale),    TEXT("Input"),    CPF_Config );
	new(GetClass(),TEXT("JoyDeadZone"),       RF_Public)UFloatProperty(CPP_PROPERTY(JoyDeadZone),       TEXT("Input"),    CPF_Config );
	new(GetClass(),TEXT("LogInput"),          RF_Public)UBoolProperty (CPP_PROPERTY(LogInput),          TEXT("Input"),    CPF_Config );
	new(GetClass(),TEXT("SplitScreenJoinInMatch"),RF_Public)UBoolProperty(CPP_PROPERTY(SplitScreenJoinInMatch),TEXT("Input"),CPF_Config );
	new(GetClass(),TEXT("SplitScreenFullDetail"),RF_Public)UBoolProperty(CPP_PROPERTY(SplitScreenFullDetail),TEXT("Display"),CPF_Config );
	new(GetClass(),TEXT("PadsStartAtPlayerTwo"),RF_Public)UBoolProperty(CPP_PROPERTY(PadsStartAtPlayerTwo),TEXT("Input"),CPF_Config );

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
	UseJoystick			= 1;		// M10: pads on (WinDrv's DirectInput joystick was off by default)
	StartupFullscreen	= 0;
	MouseXMultiplier	= 1.0f;
	MouseYMultiplier	= 1.0f;
	PadButtonThreshold	= 30;		// XINPUT_GAMEPAD_MAX_CROSSTALK (XDK Xbox.h)
	PadStickDeadZone	= 0.0f;		// the Xbox driver passes sticks raw; the bindings have DeadZone=
	PadRepeatDelay		= 0.2f;		// XboxViewport.cpp:469
	PadRepeatRate		= 0.1f;		// XboxViewport.cpp:497
	PadRumble			= 1;
	PadRumbleScale		= 1.0f;
	JoyDeadZone			= 0.35f;	// [XboxDrv.XboxClient] JoyDeadZone in Epic's inis
	LogInput			= 0;
	SplitScreenJoinInMatch	= 1;	// G-6: players 3 and 4 (UC2's menus stop at two; splitscreen.md)
	SplitScreenFullDetail	= 0;	// G-9/2026-10-10: the owner keeps Epic's split-screen detail reductions (the key gives full detail; splitscreen.md)
	PadsStartAtPlayerTwo	= 0;	// G-11 (owner, 2026-10-10): the first pad drives player one with keyboard+mouse; True = pads join from player two
	UIScaleMode			= UISCALE_Fit43;	// D-2: the UI in a centred 4:3 box (Native640 = as before)
	UIMenuBackground	= UIMENUBG_Fill;	// D-2: menu backgrounds fill the window (ui-scale.md)
	UISharpText			= 1;				// D-2: glyphs from bigger sizes of the font when the UI is scaled up

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

	// Our log name. RF_Native keeps it out of the purge of unreferenced names (UnObj.cpp RemoveNames),
	// which would otherwise hand its index to another name and the lines to that name's suppression.
	NAME_SDL2Input = FName( TEXT("SDL2Input") );
	NAME_SDL2Input.SetFlags( RF_Native );

	// F-1: relative mouse counts exactly as the device sends them (no acceleration, no scaling), what DirectInput
	// gave WinDrv. These are SDL 2's defaults on every desktop backend (raw input on Windows, XInput2 raw motion on
	// X11, the unaccelerated relative-pointer deltas on Wayland; MODE_WARP=1 would warp the system cursor and read its
	// accelerated motion); engine-ut2004's SDL2Drv relies on the same defaults without naming them. Said here so
	// that a backend or SDL version change cannot bring acceleration in unnoticed. Normal priority: an SDL_MOUSE_*
	// environment variable still wins (SDL's rule), e.g. for a player who wants warping.
	// The browser's pointer lock is the frontend's (unadjustedMovement where the browser allows it).
	SDL_SetHint( SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "0" );
	SDL_SetHint( SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "0" );
	SDL_SetHint( SDL_HINT_MOUSE_RELATIVE_SPEED_SCALE, "1.0" );

	if( SDL_InitSubSystem( SDL_INIT_VIDEO ) != 0 )
		appErrorf( TEXT("Couldn't initialize SDL2 video: %s"), appFromAnsi(SDL_GetError()) );
	debugf( NAME_Init, TEXT("SDL2 video driver: %s"), appFromAnsi(SDL_GetCurrentVideoDriver()) );

	// Pads (M10, SDL2Pads.cpp), and the test channel that drives a virtual one (SDL2InputTest.h).
	InitPads();
	// The channel's file: test key [SDL2Drv.SDL2Client] InputTestFile=<file>, set with -ini: (F-4 follow-up; was
	// -inputtest=<file>), read once and never kept in the ini.
	FString TestFile;
	if( appReadTestKey( TEXT("InputTestFile"), TestFile, TEXT("SDL2Drv.SDL2Client") ) && TestFile.Len() && PadsInitialized )
		InputTest = new FSDL2InputTest( *TestFile );

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

	// Make sure to shut down Viewports first. G-6: last first, so the split-screen viewports go before
	// viewport 0, whose window, device and console they borrow (a viewport's Destroy also takes it out of
	// the list, which made the old forward loop skip every second one).
	for( INT i=Viewports.Num()-1; i>=0; i-- )
		if( i < Viewports.Num() )
			Viewports(i)->ConditionalDestroy();

	// Shut down GRenDev.
	if( Engine && Engine->GRenDev )
		Engine->GRenDev->Exit(NULL);

	if( InputTest )
	{
		delete InputTest;
		InputTest = NULL;
	}
	ExitPads();
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
	if( PadExec( Cmd, Ar ) )
		return 1;
	// UXboxClient's "TOGGLERENDERLEVEL" (XboxClient.cpp: "Used to hack level rendering"): the level is not
	// drawn, the HUD and the menus are. D-2's checks measure the UI on a black frame with it (ui-scale.md).
	if( ParseCommand(&Cmd,TEXT("TOGGLERENDERLEVEL")) )
	{
		extern UBOOL GRenderLevel;
		GRenderLevel ^= TRUE;
		Ar.Logf( TEXT("TOGGLERENDERLEVEL: the level is %s"), GRenderLevel ? TEXT("drawn") : TEXT("not drawn") );
		return 1;
	}
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
			case SDL_CONTROLLERDEVICEADDED:
				PadAdded( Ev.cdevice.which );		// a device index
				continue;
			case SDL_CONTROLLERDEVICEREMOVED:
				PadRemoved( Ev.cdevice.which );		// an instance id
				continue;
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

// Game pads: SDL2Pads.cpp (USDL2Client::PollGamepads and the rest). Keyboard and mouse never
// change Engine->ActiveControllerId: on PC they are pad 0, the id ULevel::SpawnPlayActor gives the
// first viewport ("fake the GamepadID", UnLevAct.cpp). G-11 (owner's rule, 2026-10-10): by default
// (PadsStartAtPlayerTwo=False) the first pad takes port 0 and drives player one with them, the two exchangeable at
// any moment (G-6's model); with PadsStartAtPlayerTwo port 0 is theirs alone, the pads take ports 1-3 and a pad's
// Start joins as player two (G-9's model, then called KeyboardIsPlayer).

//
// Perform timer-tick processing on all visible viewports.  This causes
// all realtime viewports, and all non-realtime viewports which have been
// updated, to be blitted.
//
void USDL2Client::Tick( FLOAT DeltaSeconds )
{
	guard(USDL2Client::Tick);

	// The active controller id, restored after the unbound pads were read (UXboxClient::Tick).
	BYTE PreviousControllerId = Engine->ActiveControllerId;
	PollGamepads( DeltaSeconds );
	Engine->ActiveControllerId = PreviousControllerId;

	// H-3: UGameEngine::Tick takes the mouse back when the menus close ("If game is paused, release the cursor") only
	// while there is ONE viewport; in split screen nothing did until the next key or button release. The window's
	// capture follows the menus' cursor here instead, the same rule (captured again only with CaptureMouse).
	if( Viewports.Num()>1 && !GIsEditor )
	{
		USDL2Viewport* Main = (USDL2Viewport*)Viewports(0);
		UBOOL Want = CursorWanted();
		if( Main->Window && Main->Captured==Want && (Want || CaptureMouse) )
			Main->SetMouseCapture( !Want, 0, 1 );
	}

	// Blit any viewports that need blitting. G-6: split-screen viewports are rectangles of one window, so
	// one frame is presented, as UXboxClient::Tick does: only the first viewport drawn is given Blit (it
	// presents the whole frame at the start of its next draw, UGameEngine::Draw), and a viewport the
	// split-screen manager disabled (bDisableDraw: the split views while the menus are up) is not drawn.
	// With one viewport this is what it always was.
	UBOOL BlitViewport = 1;
	DOUBLE CurrentTime = appSeconds();
	for( INT i=0; i<Viewports.Num(); i++ )
	{
		USDL2Viewport* Viewport = CastChecked<USDL2Viewport>(Viewports(i));
		if( !(Viewport->IsRealtime() || (Viewport->DirtyViewport != 0)) || !Viewport->SizeX || !Viewport->SizeY )
			continue;
		if( Viewport->bDisableDraw )
		{
			Viewport->CurrentTime = CurrentTime;
			continue;
		}
		Viewport->Repaint( (BlitViewport && Viewport->DirtyViewport != -1) ? 1 : 0, DeltaSeconds );
		BlitViewport = 0;
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

	// G-6: the window's size once it has one: the screen the split-screen layouts divide
	// (UXboxClient::GetInitScreenSize: the back buffer's size). Viewport 0's SizeX/SizeY are its own
	// rectangle while split.
	USDL2Viewport* Main = Viewports.Num() ? (USDL2Viewport*)Viewports(0) : NULL;
	if( Main && Main->WindowSizeX>0 && Main->WindowSizeY>0 )
	{
		SizeX = Main->WindowSizeX;
		SizeY = Main->WindowSizeY;
	}
	else if( Main && (Main->BlitFlags & BLIT_Fullscreen) )
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
	Split screen (G-6, docs/porting/analysis/splitscreen.md).
-----------------------------------------------------------------------------*/

//
// Start on a pad that drives no viewport (UXboxClient::UpdateGamepadUnboundInput, XboxClient.cpp:878-917):
// when the menus allow a player to join, the pad takes a viewport that has no pad, or a new split-screen
// player is made for it (UClient::BeginSplitScreen). Returns whether the press was used up (it then does
// not reach the menus). PC addition (SplitScreenJoinInMatch): in a running match, where UC2's menus never
// allow a join and stop at two players anyway (GUIController.AllowControllerJoin, MAX_VIEWPORTS=2), the
// press adds a split-screen player directly, up to the manager's MaxSplitScreens (4).
//
UBOOL USDL2Client::PadJoin( INT Slot )
{
	guard(USDL2Client::PadJoin);
	if( !Viewports.Num() )
		return 0;
	if( AllowControllerJoin() )
	{
		// G-9/G-11: with PadsStartAtPlayerTwo the window's viewport is the keyboard's player (port 0) even before
		// its controller was given that port: a pad never takes it, it joins as a player of its own.
		for( INT i=PadsStartAtPlayerTwo ? 1 : 0; i<Viewports.Num(); i++ )
			if( Viewports(i)->GetGamepadID()==UViewport::INVALID_GAMEPAD_ID && Viewports(i)->Actor )
			{
				// The first binding at start-up: the press also goes on to the menus (Xbox).
				Viewports(i)->SetGamepadID( Slot );
				debugf( NAME_Log, TEXT("Split screen: pad %i takes viewport %s"), Slot, Viewports(i)->GetName() );
				return 0;
			}
		UBOOL Joined = BeginSplitScreen( this, Slot );
		debugf( NAME_Log, TEXT("Split screen: pad %i Start in the menus: %s"), Slot, Joined ? TEXT("joined") : TEXT("no player made") );
		return Joined;
	}
	USplitScreenMgr* Mgr = GetSplitScreenMgr();
	UGameEngine* GameEngine = Cast<UGameEngine>( Engine );
	ULevel* Level = GameEngine ? GameEngine->GLevel : NULL;
	ALevelInfo* Info = Level ? Level->GetLevelInfo() : NULL;
	UBaseGUIController* GUI = Viewports(0)->GUIController;
	if( SplitScreenJoinInMatch && Mgr && Info && Level!=GameEngine->GEntry
	&&	appStricmp( Level->GetOuter()->GetName(), TEXT("MENU") )!=0
	&&	(Info->NetMode==NM_Standalone || Info->NetMode==NM_ListenServer)
	&&	!(GUI && GUI->bActive)
	&&	Mgr->NumActiveSplitViews() < Mgr->MaxSplitScreens )
	{
		UBOOL Joined = Mgr->CreateSplitPlayer( NULL, Slot ) != NULL;
		debugf( NAME_Log, TEXT("Split screen: pad %i Start in the match: %s (%i split views)"), Slot, Joined ? TEXT("joined") : TEXT("no player made"), Mgr->NumActiveSplitViews() );
		if( Joined )
			Mgr->RefreshPlayerHUDs();
		return Joined;
	}
	return 0;
	unguard;
}

//
// The window changed size: the split-screen rectangles follow (USplitScreenMgr::UpdateSizes, Scion's layouts).
//
void USDL2Client::UpdateSplitLayout()
{
	guard(USDL2Client::UpdateSplitLayout);
	USplitScreenMgr* Mgr = GetSplitScreenMgr();
	UGameEngine* GameEngine = Cast<UGameEngine>( Engine );
	if( Mgr && (Mgr->NumActiveSplitViews() || Mgr->OverlayView) && GameEngine && GameEngine->GLevel )
		Mgr->UpdateSizes();
	unguard;
}

//
// Whether the window shows the cursor: a menu's cursor of the window's viewport, or, while split, of the
// overlay viewport that draws the menus (it has no window of its own).
//
// H-3: only the viewport that OWNS the GUIController counts, and only while the GUI is up. bShowWindowsMouse is set
// on the GUI's ViewportOwner every menu frame (GUIController::NativePreRender) and cleared on it when the last menu
// closes (GUIController.SetControllerStatus); when a player joins in the menus, USplitScreenMgr hands the GUI to the
// overlay viewport (ViewportOwner = OverlayView), so viewport 0 kept the 1 its menus had left and nobody cleared it:
// the cursor stayed wanted through the split-screen match and the mouse was never captured again (on the web: no
// pointer lock and the browser's pointer over the game; h3-pads.md). With one viewport the GUI is always viewport 0's
// and bShowWindowsMouse is only ever set while it is active, so this is what it was.
//
UBOOL USDL2Client::CursorWanted()
{
	for( INT i=0; i<Viewports.Num(); i++ )
	{
		UViewport* V = Viewports(i);
		if( V->bShowWindowsMouse && (i==0 || V->bShowMenu)
		&&	V->GUIController && V->GUIController->ViewportOwner==V && V->GUIController->bActive )
			return 1;
	}
	return 0;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
