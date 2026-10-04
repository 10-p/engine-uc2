/*=============================================================================
	SDL2Viewport.cpp: USDL2Viewport, one SDL2 window.

	Behaviour follows Epic's UWindowsViewport (Source/WinDrv/Src/WinViewport.cpp): the
	window messages it handles (WM_KEYDOWN/UP, WM_CHAR, WM_SETFOCUS/KILLFOCUS, WM_SIZE,
	WM_DESTROY) and its DirectInput mouse become SDL events here (HandleEvent). SDL
	usage and the scancode table come from engine-ut2004's SDL2Drv.

	Input contract kept from the PC build:
	  - a key press/release is Engine->InputEvent(IK_*, IST_Press/IST_Release); auto-repeat
	    sends further IST_Press, as WM_KEYDOWN does;
	  - typed characters are Engine->Key(Key, Char) (WM_CHAR), including Backspace, Tab and
	    Escape, never Enter;
	  - mouse motion is relative IK_MouseX/IK_MouseY axis input at all times (DirectInput),
	    buttons and wheel likewise; the absolute cursor is read on demand through
	    UpdateMousePosition (GetCursorPos), which is what the GUI uses in a window;
	  - Shift/Ctrl/Alt are IK_Shift/IK_Ctrl/IK_Alt (Windows' VK_SHIFT/VK_CONTROL/VK_MENU).
	The null device needs nothing but the window. An OpenGL device (UC2GLDrv, M5) asks for
	BLIT_OpenGL in SetRes; ResizeViewport then makes the window an OpenGL window with a GL
	context (EnsureGLContext), and the device draws into it through GetWindow().
=============================================================================*/

#include "SDL2Drv.h"

/*-----------------------------------------------------------------------------
	Class implementation.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(USDL2Viewport);

/*-----------------------------------------------------------------------------
	Key map.
-----------------------------------------------------------------------------*/

BYTE USDL2Viewport::KeyMap[SDL2DRV_MAX_SCANCODES];

//
// SDL2 scancode (physical key) -> EInputKey (the Windows virtual-key code of that key on a
// US layout, which is what the PC build received).
//
void USDL2Viewport::InitKeyMap()
{
	#define INIT_KEY_RANGE( AStart, AEnd, BStart ) \
		for( DWORD Key = AStart; Key <= AEnd; ++Key ) KeyMap[Key] = BStart + ( Key - AStart )

	appMemzero( KeyMap, sizeof(KeyMap) );

	// Modifiers: one code for both sides, as WM_KEYDOWN reports them.
	KeyMap[SDL_SCANCODE_LSHIFT]			= IK_Shift;
	KeyMap[SDL_SCANCODE_RSHIFT]			= IK_Shift;
	KeyMap[SDL_SCANCODE_LCTRL]			= IK_Ctrl;
	KeyMap[SDL_SCANCODE_RCTRL]			= IK_Ctrl;
	KeyMap[SDL_SCANCODE_LALT]			= IK_Alt;
	KeyMap[SDL_SCANCODE_RALT]			= IK_Alt;

	// TTY and navigation.
	KeyMap[SDL_SCANCODE_ESCAPE]			= IK_Escape;
	KeyMap[SDL_SCANCODE_RETURN]			= IK_Enter;
	KeyMap[SDL_SCANCODE_TAB]			= IK_Tab;
	KeyMap[SDL_SCANCODE_BACKSPACE]		= IK_Backspace;
	KeyMap[SDL_SCANCODE_SPACE]			= IK_Space;
	KeyMap[SDL_SCANCODE_GRAVE]			= IK_Tilde;
	KeyMap[SDL_SCANCODE_LEFT]			= IK_Left;
	KeyMap[SDL_SCANCODE_RIGHT]			= IK_Right;
	KeyMap[SDL_SCANCODE_UP]				= IK_Up;
	KeyMap[SDL_SCANCODE_DOWN]			= IK_Down;
	KeyMap[SDL_SCANCODE_HOME]			= IK_Home;
	KeyMap[SDL_SCANCODE_END]			= IK_End;
	KeyMap[SDL_SCANCODE_PAGEUP]			= IK_PageUp;
	KeyMap[SDL_SCANCODE_PAGEDOWN]		= IK_PageDown;
	KeyMap[SDL_SCANCODE_INSERT]			= IK_Insert;
	KeyMap[SDL_SCANCODE_DELETE]			= IK_Delete;

	// Letters, digits, function keys.
	INIT_KEY_RANGE( SDL_SCANCODE_A,  SDL_SCANCODE_Z,   IK_A  );
	INIT_KEY_RANGE( SDL_SCANCODE_1,  SDL_SCANCODE_9,   IK_1  );
	KeyMap[SDL_SCANCODE_0]				= IK_0;
	INIT_KEY_RANGE( SDL_SCANCODE_F1, SDL_SCANCODE_F12, IK_F1 );
	INIT_KEY_RANGE( SDL_SCANCODE_F13, SDL_SCANCODE_F24, IK_F13 );

	// Punctuation.
	KeyMap[SDL_SCANCODE_SEMICOLON]		= IK_Semicolon;
	KeyMap[SDL_SCANCODE_EQUALS]			= IK_Equals;
	KeyMap[SDL_SCANCODE_COMMA]			= IK_Comma;
	KeyMap[SDL_SCANCODE_MINUS]			= IK_Minus;
	KeyMap[SDL_SCANCODE_PERIOD]			= IK_Period;
	KeyMap[SDL_SCANCODE_SLASH]			= IK_Slash;
	KeyMap[SDL_SCANCODE_BACKSLASH]		= IK_Backslash;
	KeyMap[SDL_SCANCODE_LEFTBRACKET]	= IK_LeftBracket;
	KeyMap[SDL_SCANCODE_RIGHTBRACKET]	= IK_RightBracket;
	KeyMap[SDL_SCANCODE_APOSTROPHE]		= IK_SingleQuote;

	// Numeric keypad.
	INIT_KEY_RANGE( SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_9, IK_NumPad1 );
	KeyMap[SDL_SCANCODE_KP_0]			= IK_NumPad0;
	KeyMap[SDL_SCANCODE_KP_PERIOD]		= IK_NumPadPeriod;
	KeyMap[SDL_SCANCODE_KP_ENTER]		= IK_Enter;
	KeyMap[SDL_SCANCODE_KP_MULTIPLY]	= IK_GreyStar;
	KeyMap[SDL_SCANCODE_KP_PLUS]		= IK_GreyPlus;
	KeyMap[SDL_SCANCODE_KP_MINUS]		= IK_GreyMinus;
	KeyMap[SDL_SCANCODE_KP_DIVIDE]		= IK_GreySlash;

	// Locks and the rest.
	KeyMap[SDL_SCANCODE_CAPSLOCK]		= IK_CapsLock;
	KeyMap[SDL_SCANCODE_NUMLOCKCLEAR]	= IK_NumLock;
	KeyMap[SDL_SCANCODE_SCROLLLOCK]		= IK_ScrollLock;
	KeyMap[SDL_SCANCODE_PAUSE]			= IK_Pause;
	KeyMap[SDL_SCANCODE_PRINTSCREEN]	= IK_PrintScrn;

	#undef INIT_KEY_RANGE
}

//
// SDL mouse button -> EInputKey.
//
static EInputKey MouseButtonKey( Uint8 Button )
{
	switch( Button )
	{
		case SDL_BUTTON_LEFT:	return IK_LeftMouse;
		case SDL_BUTTON_MIDDLE:	return IK_MiddleMouse;
		case SDL_BUTTON_RIGHT:	return IK_RightMouse;
		case SDL_BUTTON_X1:		return IK_Mouse4;
		case SDL_BUTTON_X2:		return IK_Mouse5;
		default:				return IK_None;
	}
}

/*-----------------------------------------------------------------------------
	USDL2Viewport Init/Exit.
-----------------------------------------------------------------------------*/

USDL2Viewport::USDL2Viewport()
:	UViewport()
{
	guard(USDL2Viewport::USDL2Viewport);
	Window				= NULL;
	GLContext			= NULL;
	BlitFlags			= 0;
	ViewportButtonFlags	= 0;
	Captured			= 0;
	HasFocus			= 0;

	// The engine only runs in 32-bit colour (UWindowsViewport sets 4 whatever the desktop depth).
	ColorBytes			= 4;
	FullscreenOnly		= 0;
	unguard;
}

void USDL2Viewport::Destroy()
{
	guard(USDL2Viewport::Destroy);
	Super::Destroy();
	if( BlitFlags & BLIT_Temporary )
	{
		appFree( ScreenPointer );
		ScreenPointer = NULL;
	}
	CloseWindow();
	unguard;
}

void USDL2Viewport::ShutdownAfterError()
{
	if( RenDev )
		RenDev->RestoreGamma();
	DestroyGLContext();
	if( Window )
	{
		SDL_SetRelativeMouseMode( SDL_FALSE );
		SDL_DestroyWindow( Window );
		Window = NULL;
	}
	Super::ShutdownAfterError();
}

/*-----------------------------------------------------------------------------
	Command line.
-----------------------------------------------------------------------------*/

static void ParseResolution( const TCHAR*& Cmd, INT& X, INT& Y, INT& BPP )
{
	X = appAtoi(Cmd);
	const TCHAR* CmdTemp = appStrchr(Cmd,'x') ? appStrchr(Cmd,'x')+1 : appStrchr(Cmd,'X') ? appStrchr(Cmd,'X')+1 : TEXT("");
	Y = appAtoi(CmdTemp);
	Cmd = CmdTemp;
	CmdTemp = appStrchr(Cmd,'x') ? appStrchr(Cmd,'x')+1 : appStrchr(Cmd,'X') ? appStrchr(Cmd,'X')+1 : TEXT("");
	BPP = appAtoi(CmdTemp);
	Cmd = CmdTemp;
}

UBOOL USDL2Viewport::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(USDL2Viewport::Exec);
	const TCHAR* OrigCmd = Cmd;
	while( *OrigCmd==' ' )
		OrigCmd++;
	if( UViewport::Exec( Cmd, Ar ) )
	{
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("GETSYSTEMLANGUAGE")) )
	{
		// Two letters, as GetLocaleInfo(LOCALE_SABBREVLANGNAME) gives on Windows.
		const ANSICHAR* Lang = getenv("LANG");
		if( Lang && appIsAlpha(Lang[0]) && appIsAlpha(Lang[1]) )
		{
			TCHAR Two[3] = { (TCHAR)toupper(Lang[0]), (TCHAR)toupper(Lang[1]), 0 };
			Ar.Log( Two );
		}
		else
			Ar.Log( TEXT("EN") );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("ENDFULLSCREEN")) )
	{
		if( BlitFlags & BLIT_Fullscreen )
			EndFullscreen();
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("TOGGLEFULLSCREEN")) )
	{
		ToggleFullscreen();
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("GETCURRENTRES")) )
	{
		Ar.Logf( TEXT("%ix%i"), SizeX, SizeY );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("GETCURRENTCOLORDEPTH")) )
	{
		Ar.Logf( TEXT("%i"), (ColorBytes?ColorBytes:2)*8 );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("GETCOLORDEPTHS")) )
	{
		Ar.Log( TEXT("32") );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("GETCURRENTRENDERDEVICE")) )
	{
		Ar.Log( RenDev ? RenDev->GetClass()->GetPathName() : TEXT("") );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("SETRES")) || ParseCommand(&Cmd,TEXT("TEMPSETRES")) )
	{
		// TEMPSETRES (WinDrv) changes the mode but puts the configured sizes back afterwards.
		UBOOL Temp = appStrfind( OrigCmd, TEXT("TEMPSETRES") )==OrigCmd;
		INT X, Y, BPP;
		ParseResolution( Cmd, X, Y, BPP );
		UBOOL Fullscreen = IsFullscreen() || FullscreenOnly;
		if( appStrchr(Cmd,'w') || appStrchr(Cmd,'W') )
			Fullscreen = 0;
		else if( appStrchr(Cmd,'f') || appStrchr(Cmd,'F') )
			Fullscreen = 1;
		USDL2Client* C = GetOuterUSDL2Client();
		INT SavedX = Fullscreen ? C->FullscreenViewportX : C->WindowedViewportX;
		INT SavedY = Fullscreen ? C->FullscreenViewportY : C->WindowedViewportY;
		if( X && Y && RenDev )
		{
			INT NewColorBytes = BPP==16 ? 2 : (BPP==24 || BPP==32) ? 4 : 0;
			if( !RenDev->SetRes( this, X, Y, Fullscreen || FullscreenOnly, NewColorBytes, !Temp ) )
				EndFullscreen();
		}
		if( Temp )
		{
			if( Fullscreen )
				C->FullscreenViewportX = SavedX, C->FullscreenViewportY = SavedY;
			else
				C->WindowedViewportX = SavedX, C->WindowedViewportY = SavedY;
			C->SaveConfig();
		}
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("SETMOUSE")) )
	{
		const TCHAR* CmdTemp = Cmd;
		INT X = appAtoi(CmdTemp);
		while( appIsDigit(*CmdTemp) )
			CmdTemp++;
		while( appIsSpace(*CmdTemp) )
			CmdTemp++;
		INT Y = appAtoi(CmdTemp);
		if( Window )
			SDL_WarpMouseInWindow( Window, X, Y );
		WindowsMouseX = X;
		WindowsMouseY = Y;
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("LOADWINDOWPOSITION")) )
	{
		// [WindowPositions] MainWindow=(X=,Y=,...) as UWindowsViewport saves it.
		TCHAR Pos[256];
		if( Window && !(BlitFlags & BLIT_Fullscreen) && GConfig->GetString( TEXT("WindowPositions"), TEXT("MainWindow"), Pos, ARRAY_COUNT(Pos) ) )
		{
			INT X=0, Y=0;
			SDL_GetWindowPosition( Window, &X, &Y );
			Parse( Pos, TEXT("X="), X );
			Parse( Pos, TEXT("Y="), Y );
			SDL_SetWindowPosition( Window, X, Y );
		}
		return 1;
	}
	else return 0;
	unguard;
}

/*-----------------------------------------------------------------------------
	Window opening and closing.
-----------------------------------------------------------------------------*/

//
// Open this viewport's window, then the render device (UWindowsViewport::OpenWindow).
//
void USDL2Viewport::OpenWindow( DWORD InParentWindow, UBOOL IsTemporary, INT NewX, INT NewY, INT OpenX, INT OpenY )
{
	guard(USDL2Viewport::OpenWindow);
	check(Actor);
	USDL2Client* C = GetOuterUSDL2Client();
	if( NewX!=INDEX_NONE )
		NewX = Align( NewX, 2 );

	if( IsTemporary )
	{
		// In-memory frame buffer, no window.
		BlitFlags     = BLIT_Temporary;
		ColorBytes    = 2;
		SizeX         = NewX;
		SizeY         = NewY;
		ScreenPointer = (BYTE*)appMalloc( 2 * NewX * NewY, TEXT("TemporaryViewportData") );
		debugf( NAME_Log, TEXT("Opened temporary viewport") );
	}
	else
	{
		BlitFlags = 0;
		INT W = NewX!=INDEX_NONE ? NewX : C->WindowedViewportX;
		INT H = NewY!=INDEX_NONE ? NewY : C->WindowedViewportY;
		if( W<=0 || H<=0 )
			W = 640, H = 480;
		if( !Window )
		{
			// A plain window. A GL render device (M5) needs SDL_WINDOW_OPENGL here; the null
			// device draws nothing and needs no context.
			Window = SDL_CreateWindow(
				TCHAR_TO_ANSI(LocalizeGeneral(TEXT("Product"),appPackage())),
				OpenX==INDEX_NONE ? SDL_WINDOWPOS_CENTERED : OpenX,
				OpenY==INDEX_NONE ? SDL_WINDOWPOS_CENTERED : OpenY,
				W, H, SDL_WINDOW_RESIZABLE );
			if( !Window )
				appErrorf( TEXT("SDL_CreateWindow failed: %s"), appFromAnsi(SDL_GetError()) );
			debugf( NAME_Log, TEXT("Opened viewport (SDL window %u, %ix%i)"), SDL_GetWindowID(Window), W, H );

			// WM_CREATE: make this viewport current; take the focus ("losing focus when starting
			// up fullscreen" fix in WinViewport.cpp).
			GetOuterUClient()->MakeCurrent( this );
			SDL_RaiseWindow( Window );
		}
		else
		{
			SDL_SetWindowSize( Window, W, H );
		}
	}

	UBOOL Windowed = GIsEditor || ParseParam(appCmdLine(),TEXT("windowed")) || !C->StartupFullscreen;

	// Create rendering device.
	if( !RenDev && !GIsEditor && ParseParam(appCmdLine(),TEXT("windowed")) )
		TryRenderDevice( TEXT("ini:Engine.Engine.RenderDevice"), NewX, NewY, 0 );
	if( !RenDev )
		TryRenderDevice( TEXT("ini:Engine.Engine.RenderDevice"), NewX, NewY, !Windowed );

	check(RenDev);
	RenDev->UpdateGamma( this );
	UpdateWindowFrame();

	unguard;
}

//
// Close a viewport window. Does not affect the viewport object.
//
void USDL2Viewport::CloseWindow()
{
	guard(USDL2Viewport::CloseWindow);
	if( Window )
	{
		if( !GIsEditor && !(BlitFlags & BLIT_Fullscreen) )
		{
			INT X=0, Y=0, W=0, H=0;
			SDL_GetWindowPosition( Window, &X, &Y );
			SDL_GetWindowSize( Window, &W, &H );
			GConfig->SetString( TEXT("WindowPositions"), TEXT("MainWindow"), *FString::Printf( TEXT("(X=%i,Y=%i,XL=%i,YL=%i)"), X, Y, W, H ) );
		}
		if( Captured )
			SDL_SetRelativeMouseMode( SDL_FALSE );
		DestroyGLContext();
		SDL_DestroyWindow( Window );
		Window = NULL;
		debugf( NAME_Log, TEXT("Closed viewport") );
	}
	unguard;
}

/*-----------------------------------------------------------------------------
	UViewport operations.
-----------------------------------------------------------------------------*/

UBOOL USDL2Viewport::Lock( BYTE* HitData, INT* HitSize )
{
	guard(USDL2Viewport::Lock);
	if( !SizeX || !SizeY || !RenDev )
		return 0;
	Stride = SizeX;
	return UViewport::Lock( HitData, HitSize );
	unguard;
}

void USDL2Viewport::Unlock()
{
	guard(USDL2Viewport::Unlock);
	UViewport::Unlock();
	unguard;
}

void USDL2Viewport::Repaint( UBOOL Blit, FLOAT DeltaSeconds )
{
	guard(USDL2Viewport::Repaint);
	GetOuterUSDL2Client()->Engine->Draw( this, DeltaSeconds, Blit );
	unguard;
}

UBOOL USDL2Viewport::IsFullscreen()
{
	guard(USDL2Viewport::IsFullscreen);
	return (BlitFlags & BLIT_Fullscreen)!=0;
	unguard;
}

//
// Editor cursor shapes; the game draws its own cursor.
//
void USDL2Viewport::SetModeCursor()
{
	guard(USDL2Viewport::SetModeCursor);
	unguard;
}

void USDL2Viewport::UpdateWindowFrame()
{
	guard(USDL2Viewport::UpdateWindowFrame);

	bFullScreen = (BlitFlags & BLIT_Fullscreen) ? 1 : 0;

	// Keep the capture state in step with the menu's cursor (UWindowsViewport does the same here).
	if( !GIsEditor && Window )
		SetMouseCapture( !bShowWindowsMouse, 0, 1 );

	if( !Window || (BlitFlags&BLIT_Fullscreen) || (BlitFlags&BLIT_Temporary) || !Actor )
		return;

	SDL_SetWindowTitle( Window, TCHAR_TO_ANSI(LocalizeGeneral(TEXT("Product"),appPackage())) );
	unguard;
}

void* USDL2Viewport::GetWindow()
{
	guard(USDL2Viewport::GetWindow);
	return (void*)Window;
	unguard;
}

/*-----------------------------------------------------------------------------
	Render device and modes.
-----------------------------------------------------------------------------*/

//
// Try switching to a new rendering device (UWindowsViewport::TryRenderDevice without the
// Win32 foreground juggling: an SDL window that exists can always go fullscreen).
//
void USDL2Viewport::TryRenderDevice( const TCHAR* ClassName, INT NewX, INT NewY, UBOOL Fullscreen )
{
	guard(USDL2Viewport::TryRenderDevice);
	USDL2Client* C = GetOuterUSDL2Client();

	// Shut down current rendering device.
	if( RenDev && RenDev != C->Engine->GRenDev )
	{
		RenDev->Exit(this);
		delete RenDev;
		RenDev = NULL;
	}

	// Use appropriate defaults.
	if( NewX==INDEX_NONE )
		NewX = (Fullscreen || FullscreenOnly) ? C->FullscreenViewportX : C->WindowedViewportX;
	if( NewY==INDEX_NONE )
		NewY = (Fullscreen || FullscreenOnly) ? C->FullscreenViewportY : C->WindowedViewportY;

	// Find device driver.
	UClass* RenderClass = UObject::StaticLoadClass( URenderDevice::StaticClass(), NULL, ClassName, NULL, 0, NULL );
	if( RenderClass )
	{
		if( C->Engine->GRenDev && RenderClass == C->Engine->GRenDev->GetClass() )
		{
			RenDev = C->Engine->GRenDev;
		}
		else
		{
			RenDev = ConstructObject<URenderDevice>( RenderClass, this );
			RenDev->Init();
		}
		FullscreenOnly = RenDev->Is3dfx;
		debugf( NAME_Init, TEXT("Render device: %s (%ix%i %s)"), RenDev->GetClass()->GetPathName(), NewX, NewY, (Fullscreen|FullscreenOnly) ? TEXT("fullscreen") : TEXT("windowed") );
		if( RenDev->SetRes( this, NewX, NewY, Fullscreen | FullscreenOnly ) )
		{
			if( GIsRunning && Actor->GetLevel() )
				Actor->GetLevel()->DetailChange(
					RenDev->SuperHighDetailActors ? DM_SuperHigh :
					RenDev->HighDetailActors ? DM_High :
					DM_Low
					);
		}
		else
		{
			debugf( NAME_Log, LocalizeError(TEXT("Failed3D"),TEXT("WinDrv")) );
			if( RenDev != C->Engine->GRenDev )
				delete RenDev;
			RenDev = NULL;
		}
	}

	if( !GIsEditor )
	{
		if( GUIController )
			GUIController->ResetInput();
		if( Input )
			Input->ResetInput();
	}
	unguard;
}

void USDL2Viewport::EndFullscreen()
{
	guard(USDL2Viewport::EndFullscreen);
	USDL2Client* Client = GetOuterUSDL2Client();
	debugf( TEXT("EndFullscreen") );
	if( RenDev && (BlitFlags & (BLIT_Direct3D|BLIT_OpenGL)) && !FullscreenOnly )
		RenDev->SetRes( this, Client->WindowedViewportX, Client->WindowedViewportY, 0 );
	else if( RenDev && (BlitFlags & BLIT_OpenGL) )
		RenDev->SetRes( this, INDEX_NONE, INDEX_NONE, FullscreenOnly );
	UpdateWindowFrame();
	if( GUIController )
		GUIController->ResetInput();
	if( Input )
		Input->ResetInput();
	unguard;
}

void USDL2Viewport::ToggleFullscreen()
{
	guard(USDL2Viewport::ToggleFullscreen);
	static UBOOL ToggleFullscreenMutex = 0;
	if( !ToggleFullscreenMutex )
	{
		ToggleFullscreenMutex = 1;
		if( BlitFlags & BLIT_Fullscreen )
		{
			EndFullscreen();
			if( Captured == bShowWindowsMouse )
				SetMouseCapture( !bShowWindowsMouse, 0, 1 );
		}
		else if( !(Actor->ShowFlags & SHOW_ChildWindow) )
		{
			debugf( TEXT("AttemptFullscreen") );
			TryRenderDevice( TEXT("ini:Engine.Engine.RenderDevice"), INDEX_NONE, INDEX_NONE, 1 );
		}
		ToggleFullscreenMutex = 0;
	}
	unguard;
}

//
// Resize the viewport. The render device calls this from SetRes (the null device always
// with BLIT_Direct3D).
//
UBOOL USDL2Viewport::ResizeViewport( DWORD NewBlitFlags, INT InNewX, INT InNewY, UBOOL bSaveSize )
{
	guard(USDL2Viewport::ResizeViewport);
	USDL2Client* Client = GetOuterUSDL2Client();

	// Keep BLIT_Temporary so the temporary buffer is not leaked (WinDrv).
	NewBlitFlags |= (BlitFlags & BLIT_Temporary);

	// Remember viewport.
	UViewport* SavedViewport = NULL;
	if( Client->Engine->Audio && !GIsEditor && !(GetFlags() & RF_Destroyed) )
		SavedViewport = Client->Engine->Audio->GetViewport();

	// Default resolution handling.
	INT NewX = InNewX!=INDEX_NONE ? InNewX : (NewBlitFlags&BLIT_Fullscreen) ? Client->FullscreenViewportX : Client->WindowedViewportX;
	INT NewY = InNewX!=INDEX_NONE ? InNewY : (NewBlitFlags&BLIT_Fullscreen) ? Client->FullscreenViewportY : Client->WindowedViewportY;
	check(NewX>=0);
	check(NewY>=0);
	NewX = Align(NewX,2);

	if( !(NewBlitFlags & BLIT_Temporary) )
		ScreenPointer = NULL;

	// Set new info.
	DWORD OldBlitFlags = BlitFlags;
	BlitFlags          = NewBlitFlags & ~BLIT_ParameterFlags;
	SizeX              = NewX;
	SizeY              = NewY;

	// Transitioning out of fullscreen.
	if( !(NewBlitFlags & BLIT_Fullscreen) && (OldBlitFlags & BLIT_Fullscreen) && Captured )
		SetMouseCapture( 0, 0, 0 );

	if( Window && !(NewBlitFlags & BLIT_Temporary) )
	{
		if( NewBlitFlags & BLIT_Fullscreen )
		{
			SDL_SetWindowSize( Window, NewX, NewY );
			SDL_SetWindowFullscreen( Window, SDL_WINDOW_FULLSCREEN );
			SetDrag( 1 );
			SetMouseCapture( 1, 1, 0 );
		}
		else
		{
			if( OldBlitFlags & BLIT_Fullscreen )
				SDL_SetWindowFullscreen( Window, 0 );
			if( !(NewBlitFlags & BLIT_NoWindowChange) )
				SDL_SetWindowSize( Window, NewX, NewY );
			SetDrag( 0 );
		}
	}

	// M5: an OpenGL render device needs a GL window and context.
	if( (NewBlitFlags & BLIT_OpenGL) && !(NewBlitFlags & BLIT_Temporary) && !EnsureGLContext() )
		return 0;

	// Update audio.
	if( SavedViewport && SavedViewport!=Client->Engine->Audio->GetViewport() )
		Client->Engine->Audio->SetViewport( SavedViewport );

	// Update the window.
	UpdateWindowFrame();
	bWindowsMouseAvailable = !(NewBlitFlags & BLIT_Fullscreen);

	// Save info.
	if( RenDev && !GIsEditor && bSaveSize && NewX && NewY )
	{
		if( NewBlitFlags & BLIT_Fullscreen )
		{
			Client->FullscreenViewportX = NewX;
			Client->FullscreenViewportY = NewY;
		}
		else
		{
			Client->WindowedViewportX = NewX;
			Client->WindowedViewportY = NewY;
		}
		Client->SaveConfig();
	}
	return 1;
	unguard;
}

/*-----------------------------------------------------------------------------
	OpenGL context (M5).
-----------------------------------------------------------------------------*/

//
// Make this viewport's window an OpenGL window with a current GL context. OpenWindow
// creates a plain window before the render device is known, so a GL device's first SetRes
// re-creates it (same title, position and size) with SDL_WINDOW_OPENGL. The pixel format
// attributes are the device's (SDL_GL_SetAttribute before ResizeViewport). Compatibility
// profile: the fixed-function pipeline is what R1 draws with.
//
UBOOL USDL2Viewport::EnsureGLContext()
{
	guard(USDL2Viewport::EnsureGLContext);
	if( GLContext )
	{
		SDL_GL_MakeCurrent( Window, (SDL_GLContext)GLContext );
		return 1;
	}
	if( !Window )
		return 0;
	if( !(SDL_GetWindowFlags(Window) & SDL_WINDOW_OPENGL) )
	{
		INT X=0, Y=0, W=0, H=0;
		SDL_GetWindowPosition( Window, &X, &Y );
		SDL_GetWindowSize( Window, &W, &H );
		ANSICHAR Title[256];
		strncpy( Title, SDL_GetWindowTitle(Window), ARRAY_COUNT(Title)-1 );
		Title[ARRAY_COUNT(Title)-1] = 0;
		Uint32 Flags = SDL_WINDOW_OPENGL | (SDL_GetWindowFlags(Window) & (SDL_WINDOW_RESIZABLE|SDL_WINDOW_FULLSCREEN|SDL_WINDOW_HIDDEN));
		if( Captured )
			SDL_SetRelativeMouseMode( SDL_FALSE );
		SDL_DestroyWindow( Window );
		Window = SDL_CreateWindow( Title, X, Y, W, H, Flags );
		if( !Window )
			appErrorf( TEXT("SDL_CreateWindow (OpenGL) failed: %s"), appFromAnsi(SDL_GetError()) );
		debugf( NAME_Log, TEXT("Re-created viewport window for OpenGL (SDL window %u, %ix%i)"), SDL_GetWindowID(Window), W, H );
		SDL_RaiseWindow( Window );
		if( Captured )
			SDL_SetRelativeMouseMode( SDL_TRUE );
	}
	GLContext = SDL_GL_CreateContext( Window );
	if( !GLContext )
	{
		debugf( NAME_Init, TEXT("SDL_GL_CreateContext failed: %s"), appFromAnsi(SDL_GetError()) );
		return 0;
	}
	SDL_GL_MakeCurrent( Window, (SDL_GLContext)GLContext );
	return 1;
	unguard;
}

void USDL2Viewport::DestroyGLContext()
{
	if( GLContext )
	{
		SDL_GL_DeleteContext( (SDL_GLContext)GLContext );
		GLContext = NULL;
	}
}

/*-----------------------------------------------------------------------------
	Input.
-----------------------------------------------------------------------------*/

TCHAR* USDL2Viewport::GetLocalizedKeyName( EInputKey Key )
{
	guard(USDL2Viewport::GetLocalizedKeyName);
	TCHAR* KeyName = appStaticString1024();
	appStrcpy( KeyName, TEXT("UNKNOWN") );
	for( INT i=0; i<SDL2DRV_MAX_SCANCODES; i++ )
	{
		if( KeyMap[i] && KeyMap[i]==(BYTE)Key )
		{
			const char* Name = SDL_GetKeyName( SDL_GetKeyFromScancode((SDL_Scancode)i) );
			if( Name && Name[0] )
				appStrncpy( KeyName, appFromAnsi(Name), 1024 );
			break;
		}
	}
	return KeyName;
	unguard;
}

UBOOL USDL2Viewport::CauseInputEvent( INT iKey, EInputAction Action, FLOAT Delta )
{
	guard(USDL2Viewport::CauseInputEvent);
	return GetOuterUSDL2Client()->Engine->InputEvent( this, (EInputKey)iKey, Action, Delta );
	unguard;
}

//
// Decode one UTF-8 sequence; returns the code point (0xFFFD if malformed) and advances P.
//
static DWORD DecodeUTF8( const char*& P )
{
	const BYTE* S = (const BYTE*)P;
	DWORD C = *S++;
	INT Extra = C>=0xF0 ? 3 : C>=0xE0 ? 2 : C>=0xC0 ? 1 : 0;
	if( C>=0x80 && C<0xC0 )
	{
		P = (const char*)S;
		return 0xFFFD;
	}
	C &= (0xFF >> (Extra+1+(Extra>0)));
	for( INT i=0; i<Extra; i++ )
	{
		if( (*S & 0xC0)!=0x80 )
		{
			P = (const char*)S;
			return 0xFFFD;
		}
		C = (C<<6) | (*S++ & 0x3F);
	}
	P = (const char*)S;
	return C;
}

//
// One SDL event for this viewport's window (the WndProc of the PC build).
//
void USDL2Viewport::HandleEvent( const SDL_Event& Ev )
{
	guard(USDL2Viewport::HandleEvent);
	USDL2Client* Client = GetOuterUSDL2Client();

	switch( Ev.type )
	{
		case SDL_WINDOWEVENT:
		{
			switch( Ev.window.event )
			{
				case SDL_WINDOWEVENT_FOCUS_GAINED:	FocusChanged( 1 ); break;
				case SDL_WINDOWEVENT_FOCUS_LOST:	FocusChanged( 0 ); break;
				case SDL_WINDOWEVENT_SIZE_CHANGED:
				{
					// WM_SIZE: the window manager resized us.
					INT NewX = Ev.window.data1, NewY = Ev.window.data2;
					if( !(BlitFlags & BLIT_Fullscreen) && NewX>0 && NewY>0 && (NewX!=SizeX || NewY!=SizeY) )
					{
						if( RenDev && (BlitFlags & (BLIT_OpenGL|BLIT_Direct3D)) )
							RenDev->SetRes( this, NewX, NewY, 0 );
						else
							ResizeViewport( BlitFlags|BLIT_NoWindowChange, NewX, NewY );
					}
					break;
				}
				case SDL_WINDOWEVENT_EXPOSED:
					DirtyViewport = 1;
					break;
				case SDL_WINDOWEVENT_CLOSE:
					// WM_DESTROY of the game window: close the menus and leave.
					if( !GIsEditor && GUIController )
						GUIController->eventCloseAll( 1 );
					appRequestExit( 0 );
					break;
			}
			break;
		}
		case SDL_KEYDOWN:
		{
			if( (INT)Ev.key.keysym.scancode >= SDL2DRV_MAX_SCANCODES )
				break;
			EInputKey Key = (EInputKey)KeyMap[Ev.key.keysym.scancode];
			if( Key==IK_None )
				break;
			if( Key==IK_Enter && (Ev.key.keysym.mod & KMOD_ALT) )
			{
				ToggleFullscreen();
				break;
			}
			CauseInputEvent( Key, IST_Press );

			// The control characters WM_CHAR delivers for these keys (never Enter).
			if( Key==IK_Backspace || Key==IK_Tab || Key==IK_Escape )
				Client->Engine->Key( this, Key, (TCHAR)Key );
			break;
		}
		case SDL_KEYUP:
		{
			if( (INT)Ev.key.keysym.scancode >= SDL2DRV_MAX_SCANCODES )
				break;
			EInputKey Key = (EInputKey)KeyMap[Ev.key.keysym.scancode];
			if( Key==IK_None )
				break;
			CauseInputEvent( Key, IST_Release );

			// Capture the mouse again if the menu went away while a key was held (WM_KEYUP).
			if( !GIsEditor && Captured == bShowWindowsMouse )
				SetMouseCapture( !bShowWindowsMouse, 0, 1 );
			break;
		}
		case SDL_TEXTINPUT:
		{
			// WM_CHAR: TCHAR is Latin-1, so code points above 255 cannot be typed.
			for( const char* P = Ev.text.text; *P; )
			{
				DWORD C = DecodeUTF8( P );
				if( C>=32 && C<256 && C!=127 )
					Client->Engine->Key( this, (EInputKey)C, (TCHAR)C );
			}
			break;
		}
		case SDL_MOUSEMOTION:
		{
			// DirectInput's relative axes.
			FLOAT MX = Client->MouseXMultiplier!=0.f ? Client->MouseXMultiplier : 1.f;
			FLOAT MY = Client->MouseYMultiplier!=0.f ? Client->MouseYMultiplier : 1.f;
			if( !Captured )
			{
				WindowsMouseX = Ev.motion.x;
				WindowsMouseY = Ev.motion.y;
			}
			if( Ev.motion.xrel )
				CauseInputEvent( IK_MouseX, IST_Axis, +MX * Ev.motion.xrel );
			if( Ev.motion.yrel )
				CauseInputEvent( IK_MouseY, IST_Axis, -MY * Ev.motion.yrel );
			break;
		}
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
		{
			EInputKey Key = MouseButtonKey( Ev.button.button );
			if( Key==IK_None )
				break;
			DWORD Flag = Key==IK_LeftMouse ? MOUSE_Left : Key==IK_RightMouse ? MOUSE_Right : Key==IK_MiddleMouse ? MOUSE_Middle : 0;
			if( Ev.type==SDL_MOUSEBUTTONDOWN )
			{
				ViewportButtonFlags |= Flag;
				CauseInputEvent( Key, IST_Press );
			}
			else
			{
				ViewportButtonFlags &= ~Flag;
				CauseInputEvent( Key, IST_Release );
				// WM_xBUTTONUP in the game.
				if( !GIsEditor )
				{
					if( Dragging )
						Dragging = 0;
					if( Captured == bShowWindowsMouse )
						SetMouseCapture( !bShowWindowsMouse, 0, 1 );
				}
			}
			break;
		}
		case SDL_MOUSEWHEEL:
		{
			INT Notches = Ev.wheel.y;
			if( Ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED )
				Notches = -Notches;
			if( Notches )
			{
				// DirectInput reports WHEEL_DELTA (120) per notch.
				CauseInputEvent( IK_MouseW, IST_Axis, Notches * 120 );
				EInputKey Key = Notches<0 ? IK_MouseWheelDown : IK_MouseWheelUp;
				CauseInputEvent( Key, IST_Press );
				CauseInputEvent( Key, IST_Release );
			}
			break;
		}
	}
	unguard;
}

//
// WM_SETFOCUS / WM_KILLFOCUS.
//
void USDL2Viewport::FocusChanged( UBOOL Gained )
{
	guard(USDL2Viewport::FocusChanged);
	HasFocus = Gained;
	if( Gained )
	{
		if( !GIsEditor && Captured == bShowWindowsMouse && !Dragging )
			SetMouseCapture( !bShowWindowsMouse, 0, 1 );
		if( GUIController )
			GUIController->ResetInput();
		if( Input )
			Input->ResetInput();
		GetOuterUClient()->MakeCurrent( this );
		if( RenDev )
			RenDev->UpdateGamma( this );
	}
	else
	{
		if( RenDev )
			RenDev->RestoreGamma();
		SetMouseCapture( 0, 0, 0 );
		SetDrag( 0 );
		if( !GIsEditor )
		{
			if( GUIController )
				GUIController->ResetInput();
			if( Input )
				Input->ResetInput();
		}
	}
	unguard;
}

//
// Capture: hide the cursor and take relative motion (SetCapture + ShowCursor(FALSE) + clipping).
// Clip without capture confines the cursor to the window.
//
void USDL2Viewport::SetMouseCapture( UBOOL Capture, UBOOL Clip, UBOOL OnlyFocus )
{
	guard(USDL2Viewport::SetMouseCapture);
	if( !Window )
		return;

	Clip |= Capture;
	if( Clip && OnlyFocus && !(SDL_GetWindowFlags(Window) & SDL_WINDOW_INPUT_FOCUS) )
		return;

	if( Capture != Captured )
	{
		if( Capture )
		{
			OrigCursor = FVector( WindowsMouseX, WindowsMouseY, 0 );
			SDL_SetRelativeMouseMode( SDL_TRUE );
		}
		else
		{
			SDL_SetRelativeMouseMode( SDL_FALSE );
		}
	}
	SDL_SetWindowGrab( Window, Clip ? SDL_TRUE : SDL_FALSE );
	Captured = Capture;
	unguard;
}

//
// The absolute cursor, read when the GUI asks (UWindowsViewport::UpdateMousePosition).
//
void USDL2Viewport::UpdateMousePosition()
{
	guard(USDL2Viewport::UpdateMousePosition);
	if( !Window || Captured )
		return;
	INT X=0, Y=0, WX=0, WY=0;
	SDL_GetMouseState( &X, &Y );
	SDL_GetWindowPosition( Window, &WX, &WY );
	MouseClientPos = FVector( X, Y, 0 );
	MouseScreenPos = FVector( X+WX, Y+WY, 0 );
	WindowsMouseX  = X;
	WindowsMouseY  = Y;
	unguard;
}

//
// Pump the event queue, then release every key the engine still holds that is up
// (and, on Reset, press every key that is down), as UWindowsViewport::UpdateInput does
// with GetKeyState.
//
void USDL2Viewport::UpdateInput( UBOOL Reset, FLOAT DeltaSeconds )
{
	guard(USDL2Viewport::UpdateInput);
	USDL2Client* Client = GetOuterUSDL2Client();
	Client->PumpEvents();

	if( !Input || !Window )
		return;

	BYTE Down[256];
	appMemzero( Down, sizeof(Down) );
	INT NumKeys = 0;
	const Uint8* State = SDL_GetKeyboardState( &NumKeys );
	UBOOL Focused = (SDL_GetWindowFlags(Window) & SDL_WINDOW_INPUT_FOCUS) != 0;
	for( INT i=0; i<NumKeys && i<SDL2DRV_MAX_SCANCODES; i++ )
		if( KeyMap[i] && State[i] && Focused )
			Down[KeyMap[i]] = 1;

	Reset = Reset && Focused;
	for( INT i=0; i<SDL2DRV_MAX_SCANCODES; i++ )
	{
		BYTE Key = KeyMap[i];
		if( !Key || Key==IK_LeftMouse || Key==IK_RightMouse || Key==IK_MiddleMouse )
			continue;
		if( !Input->KeyDown(Key) )
		{
			if( Reset && Down[Key] )
				CauseInputEvent( Key, IST_Press );
		}
		else if( !Down[Key] )
		{
			CauseInputEvent( Key, IST_Release );
		}
	}
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
