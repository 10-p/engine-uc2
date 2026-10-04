/*=============================================================================
	SDLDrv.h: Simple Directmedia Layer cross-platform driver.
	Copyright 2002 Epic Games, Inc. All Rights Reserved.

        SDL website: http://www.libsdl.org/

Revision history:
	* Created by Ryan C. Gordon, based on WinDrv.
      This is an updated rewrite of the original SDLDrv.
=============================================================================*/

#ifndef _INC_SDLDRV
#define _INC_SDLDRV

/*----------------------------------------------------------------------------
	API.
----------------------------------------------------------------------------*/

#ifndef SDLDRV_API
	#define SDLDRV_API DLL_IMPORT
#endif

/*----------------------------------------------------------------------------
	Dependencies.
----------------------------------------------------------------------------*/

#include "SDL.h"

// Unreal includes.
#include "Engine.h"
#include "UnRender.h"

/*-----------------------------------------------------------------------------
	Declarations.
-----------------------------------------------------------------------------*/

// Classes.
class USDLViewport;
class USDLClient;

/*-----------------------------------------------------------------------------
	USDLClient.
-----------------------------------------------------------------------------*/

//
// SDL implementation of the client.
//
class USDLClient : public UClient, public FNotifyHook
{
	DECLARE_CLASS(USDLClient,UClient,CLASS_Transient|CLASS_Config,SDLDrv)

	// Configuration.
	BITFIELD			UseJoystick;
	BITFIELD			StartupFullscreen;
	INT                 JoystickNumber;
	INT                 JoystickHatNumber;
	FLOAT               ScaleJBX;
	FLOAT               ScaleJBY;
	UBOOL               IgnoreHat;

	// Variables.
	UBOOL				InMenuLoop;
	UBOOL				ConfigReturnFullscreen;
	INT					NormalMouseInfo[3];
	INT					CaptureMouseInfo[3];

    static SDL_Joystick *Joystick;
    static int JoystickAxes;
    static int JoystickButtons;
    static int JoystickHats;
    static int JoystickBalls;

	// Constructors.
	USDLClient();
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
	void Tick();
	void MakeCurrent( UViewport* InViewport );
	UViewport* GetLastCurrent();
	class UViewport* NewViewport( const FName Name );
};

/*-----------------------------------------------------------------------------
	USDLViewport.
-----------------------------------------------------------------------------*/

//
// An SDL viewport.
//
class USDLViewport : public UViewport
{
	DECLARE_CLASS(USDLViewport,UViewport,CLASS_Transient,SDLDrv)
	DECLARE_WITHIN(USDLClient)

	// Variables.
    SDL_Surface *		ScreenSurface;
	DWORD				BlitFlags;

    UBOOL				LostGrab;
    UBOOL				LostFullscreen;

    INT					SavedCursorX;
    INT					SavedCursorY;

	// SDL Keysym to EInputKey map.
	BYTE				KeysymMap[512];

	// KeyRepeatKey.
    INT					KeyRepeatKey;
    TCHAR				KeyRepeatUnicode;
	FTime				RepeatTimer;

	// Joystick Hat Hack
	EInputKey 			LastJoyHat;
	UBOOL				LastKey;

	// Constructor.
	USDLViewport();

	// UObject interface.
	void Destroy();
	void ShutdownAfterError();

	// UViewport interface.
	UBOOL Lock( BYTE* HitData=NULL, INT* HitSize=0 );
	UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar );
	UBOOL ResizeViewport( DWORD BlitFlags, INT NewX=INDEX_NONE, INT NewY=INDEX_NONE, UBOOL bSaveSize=true );
	UBOOL IsFullscreen();
	void Unlock();
	void Repaint( UBOOL Blit );
	void SetModeCursor();
	void UpdateWindowFrame();
	void OpenWindow( DWORD ParentWindow, UBOOL Temporary, INT NewX, INT NewY, INT OpenX, INT OpenY );
	void CloseWindow();
	void UpdateInput( UBOOL Reset, FLOAT DeltaSeconds );
	void* GetWindow();
	void SetMouseCapture( UBOOL Capture, UBOOL Clip, UBOOL FocusOnly );

	// USDLViewport interface.
	void ToggleFullscreen();
	void EndFullscreen();
	void SetTopness();
	DWORD GetViewportButtonFlags( DWORD wParam );

	UBOOL CauseInputEvent( INT iKey, EInputAction Action, FLOAT Delta=0.0 );

	virtual void TryRenderDevice( const TCHAR* ClassName, INT NewX, INT NewY, UBOOL Fullscreen );
    void SetTitleBar();

	TCHAR* GetLocalizedKeyName( EInputKey Key );
};


#define AUTO_INITIALIZE_REGISTRANTS_SDLDRV \
	USDLViewport::StaticClass(); \
	USDLClient::StaticClass();

#endif //_INC_SDLDRV
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

