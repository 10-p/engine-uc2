/*=============================================================================
	SDL2Drv.h: SDL2 client and viewport for the portable UC2 build.

	Adapted from engine-ut2004's SDL2Drv (the sibling port) to UC2's UClient/UViewport
	interface; Epic's WinDrv (PC) is the behavioural spec, XboxDrv the spec for the
	pad routing that M10 will add (see USDL2Client::PollGamepads). Epic's stale SDL 1.2
	driver for this codebase (Source/SDLDrv) stays in the tree as reference.
=============================================================================*/

#ifndef _INC_SDL2DRV
#define _INC_SDL2DRV

/*----------------------------------------------------------------------------
	Dependencies.
----------------------------------------------------------------------------*/

// The launcher includes this header only to register the classes; it does not need
// the SDL headers for that.
#ifdef SDL2DRV_REGISTRATION_ONLY
	struct SDL_Window;
	enum { SDL2DRV_MAX_SCANCODES = 512 };
#else
	#include "SDL.h"
	enum { SDL2DRV_MAX_SCANCODES = SDL_NUM_SCANCODES };
#endif

#include "Engine.h"
#include "UnRender.h"

/*-----------------------------------------------------------------------------
	Declarations.
-----------------------------------------------------------------------------*/

class USDL2Viewport;
class USDL2Client;

/*-----------------------------------------------------------------------------
	USDL2Client.
-----------------------------------------------------------------------------*/

//
// SDL2 implementation of the client (UWindowsClient's role).
//
class USDL2Client : public UClient, public FNotifyHook
{
	DECLARE_CLASS(USDL2Client,UClient,CLASS_Transient|CLASS_Config,SDL2Drv)

	// Configuration (the same keys as [WinDrv.WindowsClient]).
	BITFIELD			UseJoystick;
	BITFIELD			StartupFullscreen;
	FLOAT				MouseXMultiplier,
						MouseYMultiplier;

	// Variables.
	UBOOL				InMenuLoop;

	// Constructors.
	USDL2Client();
	void StaticConstructor();

	// FNotifyHook interface.
	void NotifyDestroy( void* Src );

	// UObject interface.
	void Destroy();
	void PostEditChange();
	void ShutdownAfterError();

	// UClient interface.
	void Init( UEngine* InEngine );
	void ShowViewportWindows( DWORD ShowFlags, INT DoShow );
	void EnableViewportWindows( DWORD ShowFlags, INT DoEnable );
	UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar=*GLog );
	void Tick( FLOAT DeltaSeconds );
	void MakeCurrent( UViewport* InViewport );
	UViewport* GetLastCurrent();
	class UViewport* NewViewport( const FName Name );
	void GetInitScreenSize( INT& SizeX, INT& SizeY );

	// USDL2Client interface.
	void PumpEvents();
	USDL2Viewport* FindViewport( DWORD WindowID );
	void PollGamepads( FLOAT DeltaSeconds );
};

/*-----------------------------------------------------------------------------
	USDL2Viewport.
-----------------------------------------------------------------------------*/

//
// An SDL2 viewport: one SDL window, no rendering context of its own (the render
// device creates whatever it needs on GetWindow()).
//
class USDL2Viewport : public UViewport
{
	DECLARE_CLASS(USDL2Viewport,UViewport,CLASS_Transient,SDL2Drv)
	DECLARE_WITHIN(USDL2Client)

	// Variables.
	SDL_Window*			Window;
	DWORD				BlitFlags;
	DWORD				ViewportButtonFlags;
	UBOOL				Captured;
	UBOOL				HasFocus;

	// SDL2 scancode -> EInputKey (shared by all viewports).
	static BYTE			KeyMap[SDL2DRV_MAX_SCANCODES];
	static void			InitKeyMap();

	// Constructor.
	USDL2Viewport();

	// UObject interface.
	void Destroy();
	void ShutdownAfterError();

	// UViewport interface.
	UBOOL Lock( BYTE* HitData=NULL, INT* HitSize=0 );
	void Unlock();
	UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar );
	UBOOL ResizeViewport( DWORD BlitFlags, INT NewX=INDEX_NONE, INT NewY=INDEX_NONE, UBOOL bSaveSize=true );
	UBOOL IsFullscreen();
	void Repaint( UBOOL Blit, FLOAT DeltaSeconds = 0.f );
	void SetModeCursor();
	void UpdateWindowFrame();
	void OpenWindow( DWORD ParentWindow, UBOOL Temporary, INT NewX, INT NewY, INT OpenX, INT OpenY );
	void CloseWindow();
	void UpdateMousePosition();
	void UpdateInput( UBOOL Reset, FLOAT DeltaSeconds );
	void* GetWindow();
	void SetMouseCapture( UBOOL Capture, UBOOL Clip, UBOOL FocusOnly );
	void TryRenderDevice( const TCHAR* ClassName, INT NewX, INT NewY, UBOOL Fullscreen );
	TCHAR* GetLocalizedKeyName( EInputKey Key );

	// USDL2Viewport interface.
	void ToggleFullscreen();
	void EndFullscreen();
	UBOOL CauseInputEvent( INT iKey, EInputAction Action, FLOAT Delta=0.0 );
#ifndef SDL2DRV_REGISTRATION_ONLY
	void HandleEvent( const SDL_Event& Ev );
#endif
	void FocusChanged( UBOOL Gained );
};

#define AUTO_INITIALIZE_REGISTRANTS_SDL2DRV \
	USDL2Viewport::StaticClass(); \
	USDL2Client::StaticClass();

#endif //_INC_SDL2DRV
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
