/*=============================================================================
	XboxDrv.h: Unreal Xbox viewport and platform driver.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Tim Sweeney.
=============================================================================*/

#ifndef _INC_XBOXDRV
#define _INC_XBOXDRV

/*----------------------------------------------------------------------------
	Dependencies.
----------------------------------------------------------------------------*/

// Unreal includes.

#include "D3DDrv.h"
#include "Engine.h"
#include "UnRender.h"
#include "XboxLoadSaveManager.h"

/*-----------------------------------------------------------------------------
	Declarations.
-----------------------------------------------------------------------------*/

// Classes.
class UXboxViewport;
class UXboxClient;

/*-----------------------------------------------------------------------------
	UXboxClient.
-----------------------------------------------------------------------------*/

//
// Xbox implementation of the client.
//
class DLL_EXPORT UXboxClient : public UClient
{
	DECLARE_CLASS(UXboxClient,UClient,CLASS_Transient|CLASS_Config,XboxDrv)

	FLOAT JoyDeadZone;

	// Constructors.
	UXboxClient();
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
	class UViewport* NewViewport( const FName Name );
	UViewport* GetLastCurrent();
	//scion jg -- Added for gamepad availability checking
	FORCEINLINE UBOOL IsGamepadInUse(INT nID);
	// scion sz - same as IsGamepadInUse but returns the viewport
	FORCEINLINE UViewport* GetGamepadViewport(INT nID);
	// scion sz - default xbox fullscreen size
	void GetInitScreenSize( INT& SizeX, INT& SizeY );
	// scion sz - handle device insertion/removal
	void DetectDevices();
	// scion sz - handle keyboard input
	void UpdateKeyboardInput();
	// scion sz - handle input from gamepads that are not bound to viewports
	void UpdateGamepadUnboundInput();
	// scion sz - only update gamma for viewport(0)
	void UpdateGamma();
	void RestoreGamma();
};

/*-----------------------------------------------------------------------------
	UXboxViewport.
-----------------------------------------------------------------------------*/

#define NUM_GAMEPADS 4

enum EXboxGamepadButton
{
	XGB_A =				0,
	XGB_B =				1,
	XGB_X =				2,
	XGB_Y =				3,
	XGB_Black =			4,
	XGB_White =			5,
	XGB_LeftTrigger =	6,
	XGB_RightTrigger =	7,
	XGB_DigitalUp =		8,
	XGB_DigitalDown =	9,
	XGB_DigitalLeft =	10,
	XGB_DigitalRight =	11,
	XGB_Start =			12,
	XGB_Back =			13,
	XGB_LeftThumb =		14,
	XGB_RightThumb =	15
};

struct FXboxGamepadState
{
	HANDLE		Handle;

	//scion bcl -- promoted from viewport functions to make it easier to pass to UIX
	XINPUT_STATE	State;

	FVector		LeftJoystick,
				RightJoystick;
	BYTE		ButtonPosition[16];

	EInputKey	RepeatKey;
	FLOAT		NextRepeatTime;
	// Rumble state info
	XINPUT_FEEDBACK Feedback;

	FXboxGamepadState::FXboxGamepadState() { 
		Handle = NULL;
	}
};

struct FXboxKeyboard
{
	HANDLE		Handle;

	FXboxKeyboard::FXboxKeyboard() { 
		Handle = NULL;
	}
};

//
// A Windows viewport.
//
class DLL_EXPORT UXboxViewport : public UViewport
{
	DECLARE_CLASS(UXboxViewport,UViewport,CLASS_Transient,XboxDrv)
	DECLARE_WITHIN(UXboxClient)

	INT	Width,
		Height;

	// Constructor.
	UXboxViewport();

	// UObject interface.
	void Destroy();
	void ShutdownAfterError();

	// UViewport interface.
	UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar );
	UBOOL ResizeViewport( DWORD BlitFlags, INT NewX=INDEX_NONE, INT NewY=INDEX_NONE, UBOOL bSaveSize=true );
	UBOOL IsFullscreen();
	void Repaint( UBOOL Blit, FLOAT DeltaSeconds = 0.f );
	void SetModeCursor();
	void OpenWindow( DWORD ParentWindow, UBOOL Temporary, INT NewX, INT NewY, INT OpenX, INT OpenY );
	void CloseWindow();
	void UpdateInput( UBOOL Reset, FLOAT DeltaSeconds );
	void* GetWindow();
	void SetMouseCapture( UBOOL Capture, UBOOL Clip, UBOOL FocusOnly );
	void UpdateWindowFrame();
	TCHAR* GetLocalizedKeyName( EInputKey Key );
	//scion jg -- Rumbles the joystick as needed
	void UpdateGamepadRumble(FLOAT flDelta);
	//scion jg -- Our overload that queries the viewport for the button state
	virtual BYTE GetAnalogButtonAmount(INT ButtonIndex);
	//epic moelfke: added accessor for setting travel URL
	virtual void SetClientTravel(const TCHAR* NextURL, UBOOL bItems, ETravelType NextTravelType);
};

#ifdef __STATIC_LINK
	#define AUTO_INITIALIZE_REGISTRANTS_XBOXDRV \
		UXboxClient::StaticClass(); \
		UXboxLoadSaveManager::StaticClass(); \
		UXboxViewport::StaticClass(); \

#endif

#endif //_INC_XBOXDRV

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
