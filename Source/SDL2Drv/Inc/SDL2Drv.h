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
class FSDL2InputTest;

/*-----------------------------------------------------------------------------
	Game pads (M10).
-----------------------------------------------------------------------------*/

// The Xbox build has four pad ports, and a pad's button i is sent as IK_Joy1+i
// (XboxViewport.cpp GamepadButtonMap): 0-7 are the analog buttons A B X Y Black White
// LeftTrigger RightTrigger, 8-15 the digital ones DPadUp DPadDown DPadLeft DPadRight
// Start Back LeftThumb RightThumb. Sticks are IK_JoyX/JoyY (left) and IK_JoyU/JoyV (right).
enum
{
	SDL2DRV_MAX_PADS		= 4,	// NUM_GAMEPADS
	SDL2DRV_PAD_BUTTONS		= 16,	// XboxViewport.cpp polls 16 buttons
	SDL2DRV_PAD_STICKAXES	= 4,	// LeftX, LeftY, RightX, RightY
	SDL2DRV_PAD_SOURCES		= 64,	// SDL buttons (0..31) and SDL axes (32..63)
};

// One entry of the SDL control -> Xbox control table ([SDL2Drv.PadMap] in the ini).
struct FSDL2PadMapEntry
{
	INT		Target;		// -1 = unmapped; 0..15 = Xbox button index; 16..19 = stick axis 0..3
	UBOOL	Invert;		// axis sign flip (SDL's stick Y is positive down, the Xbox's up)
};

// One pad port.
struct FSDL2Pad
{
	void*	Controller;							// SDL_GameController*, NULL when the port is empty
	INT		InstanceID;							// SDL_JoystickID
	BYTE	Position[SDL2DRV_PAD_BUTTONS];		// last value per Xbox button, 0..255 (XInput's ButtonPosition)
	FLOAT	Axes[SDL2DRV_PAD_STICKAXES];		// -1..1, Xbox convention (up = +)
	INT		RepeatKey;							// IK_* being auto-repeated, IK_None if none
	DOUBLE	NextRepeatTime;
	_WORD	RumbleLeft, RumbleRight;			// last motor speeds sent
	DOUBLE	RumbleSentTime;
#ifdef __EMSCRIPTEN__
	// G-10: SDL's Emscripten joystick driver has no rumble; the browser's Gamepad haptics stand in
	// (SDL2Pads.cpp, SetPadRumble). -1 = no actuator for this pad (or not found).
	INT		WebIndex;							// navigator.getGamepads() index of this pad
	UBOOL	WebHaptics;							// it has a vibrationActuator with playEffect
	DOUBLE	WebHoldUntil;						// the last effect's level holds until then
#endif
	TCHAR	Name[64];
};

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

	// Pad configuration (M10; [SDL2Drv.SDL2Client], Epic's Xbox values as defaults).
	INT					PadButtonThreshold;		// XINPUT_GAMEPAD_MAX_CROSSTALK: pressed above this (0..255)
	FLOAT				PadStickDeadZone;		// radial, applied before the bindings' own DeadZone= (Xbox: 0)
	FLOAT				PadRepeatDelay,			// first IST_Hold after (Xbox: 0.2 s)
						PadRepeatRate;			// then every (Xbox: 0.1 s)
	BITFIELD			PadRumble;				// drive the pad motors from the player's WaveformManager
	FLOAT				PadRumbleScale;
	FLOAT				JoyDeadZone;			// stored by SETDEADZONE, as UXboxClient (nothing reads it there either)
	BITFIELD			LogInput;				// SDL2Input log lines for every pad event
	BITFIELD			SplitScreenJoinInMatch;	// G-6: Start on an unbound pad during a match adds a split-screen player
	BITFIELD			SplitScreenFullDetail;	// G-9: split-screen players keep the single player's detail (else the Xbox's)
	BITFIELD			KeyboardIsPlayer;		// G-9: keyboard+mouse are a player of their own (pad port 0); pads use ports 1-3

	// Variables.
	UBOOL				InMenuLoop;
	FSDL2Pad			Pads[SDL2DRV_MAX_PADS];
	FSDL2PadMapEntry	PadMap[SDL2DRV_PAD_SOURCES];
	UBOOL				PadsInitialized;
	FSDL2InputTest*		InputTest;				// test key InputTestFile=<file>: the M10 test channel

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
	UBOOL SplitScreenViewports() { return 1; }	// G-6: split-screen viewports are rectangles of the one window
	UBOOL SplitScreenKeepsFullDetail() { return SplitScreenFullDetail; }	// G-9

	// USDL2Client interface.
	void PumpEvents();
	USDL2Viewport* FindViewport( DWORD WindowID );
	void PollGamepads( FLOAT DeltaSeconds );

	// Pads (SDL2Pads.cpp).
	void InitPads();
	void ExitPads();
	void LoadPadMap();
	void PadAdded( INT DeviceIndex );
	void PadRemoved( INT InstanceID );
	INT FindPadSlot( INT InstanceID );
	UViewport* GetPadViewport( INT Slot );
	void ReadPad( INT Slot, BYTE* NewPosition );
	void UpdateBoundPad( USDL2Viewport* Viewport, FLOAT DeltaSeconds );
	void UpdateUnboundPads();
	void SendPadKey( INT Slot, UViewport* Viewport, EInputKey Key, EInputAction Action );
	void SetPadRumble( INT Slot, _WORD Left, _WORD Right, FLOAT HoldSeconds=0.f );
	BYTE GetPadAnalogButton( INT Slot, INT ButtonIndex );
	UBOOL LoggingInput();
	UBOOL PadExec( const TCHAR* Cmd, FOutputDevice& Ar );

	// Split screen (G-6, SDL2Client.cpp).
	UBOOL PadJoin( INT Slot );
	void UpdateSplitLayout();
	UBOOL CursorWanted();
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
	void*				GLContext;		// M5: SDL_GLContext, created for BLIT_OpenGL render devices
	DWORD				BlitFlags;
	DWORD				ViewportButtonFlags;
	UBOOL				Captured;
	UBOOL				HasFocus;
	// G-6: a split-screen viewport is a rectangle of viewport 0's window, as on the Xbox (one screen, the
	// viewports' OffsetX/OffsetY/SizeX/SizeY): it has no window of its own and borrows the render device.
	UBOOL				SubViewport;
	INT					WindowSizeX,	// the window's size, the frame the split-screen layouts divide
						WindowSizeY;	// (viewport 0's SizeX/SizeY shrink to its own rectangle when split)

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
	UBOOL EnsureGLContext();
	void DestroyGLContext();
	USDL2Viewport* GetWindowViewport();

	// UPlayer/UViewport pad hooks (M10).
	BYTE GetAnalogButtonAmount( INT ButtonIndex );
	void SetClientTravel( const TCHAR* NextURL, UBOOL bItems, ETravelType NextTravelType );
};

// SDL2Input: the log name of every M10 input line (quiet unless LogInput or the test channel).
extern FName NAME_SDL2Input;
#define SDL2INPUT ((EName)NAME_SDL2Input.GetIndex())

#define AUTO_INITIALIZE_REGISTRANTS_SDL2DRV \
	USDL2Viewport::StaticClass(); \
	USDL2Client::StaticClass();

#endif //_INC_SDL2DRV
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
