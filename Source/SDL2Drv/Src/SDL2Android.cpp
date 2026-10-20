/*=============================================================================
	SDL2Android.cpp: the on-screen pad of the Android app.

	GameActivity (Android/app/src/main/java/com/uc2/android/GameActivity.java) draws a pad over
	SDL's surface and reports its buttons and sticks through the JNI functions below, from
	Android's UI thread. The client gives SDL a virtual game controller
	(SDL_JoystickAttachVirtualEx) and copies that state into it before every event poll, so the
	on-screen pad reaches the game as any SDL pad does (SDL2Pads.cpp: its port, [SDL2Drv.PadMap],
	the menus' pad navigation, the bindings). The overlay asks which layout to draw (menus or
	match) and whether the pad is in use through nativeTouchPadState.

	Back on the menus' first page (in the menu level, one page on the GUI's stack) asks the
	activity to confirm exiting the game (GameActivity.showExitDialog); everywhere else Back is
	Escape (SDL2Viewport.cpp).

	Off Android the client's functions here do nothing.
=============================================================================*/

// The standard headers before the engine's, which declare their own global operator new and delete.
#if defined(__ANDROID__)
#include <jni.h>
#include <atomic>
#endif

#include "SDL2Drv.h"

#if defined(__ANDROID__)

// What the overlay holds: SDL_GameControllerButton bits and SDL_GameControllerAxis values (sticks -1..1 with
// y positive down, as SDL's; triggers 0..1). Written by the UI thread, read by the engine's.
static std::atomic<DWORD>	GTouchButtons( 0 );
static std::atomic<FLOAT>	GTouchAxes[SDL_CONTROLLER_AXIS_MAX];

// For the overlay, written once a tick by the engine's thread.
enum
{
	TOUCHPAD_Attached	= 1,	// the virtual controller exists
	TOUCHPAD_InUse		= 2,	// it has a pad port (no real pad had port 0 before it)
	TOUCHPAD_Menu		= 4,	// the menus are up
};
static std::atomic<INT>		GTouchState( 0 );

extern "C"
{

JNIEXPORT void JNICALL Java_com_uc2_android_GameActivity_nativeTouchPadButton( JNIEnv*, jclass, jint Button, jboolean Down )
{
	if( Button < 0 || Button >= SDL_CONTROLLER_BUTTON_MAX )
		return;
	if( Down )
		GTouchButtons.fetch_or( 1u << Button );
	else
		GTouchButtons.fetch_and( ~(1u << Button) );
}

JNIEXPORT void JNICALL Java_com_uc2_android_GameActivity_nativeTouchPadAxis( JNIEnv*, jclass, jint Axis, jfloat Value )
{
	if( Axis < 0 || Axis >= SDL_CONTROLLER_AXIS_MAX )
		return;
	GTouchAxes[Axis].store( Value );
}

JNIEXPORT jint JNICALL Java_com_uc2_android_GameActivity_nativeTouchPadState( JNIEnv*, jclass )
{
	return GTouchState.load();
}

} // extern "C"

//
// The virtual controller: every SDL controller button and axis, in SDL's own order (SDL maps a virtual
// controller's buttons and axes to the controller's one for one), so the on-screen pad is read like any pad.
//
void USDL2Client::InitTouchPad()
{
	guard(USDL2Client::InitTouchPad);
	if( !TouchPad )
	{
		debugf( NAME_Init, TEXT("SDL2 pads: no on-screen pad (TouchPad=False)") );
		return;
	}
	if( !PadsInitialized )
		return;
	SDL_VirtualJoystickDesc Desc;
	SDL_zero( Desc );
	Desc.version	= SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
	Desc.type		= SDL_JOYSTICK_TYPE_GAMECONTROLLER;
	Desc.naxes		= SDL_CONTROLLER_AXIS_MAX;
	Desc.nbuttons	= SDL_CONTROLLER_BUTTON_MAX;
	Desc.name		= "UC2 on-screen pad";
	INT Index = SDL_JoystickAttachVirtualEx( &Desc );
	if( Index < 0 )
	{
		debugf( NAME_Init, TEXT("SDL2 pads: no on-screen pad: SDL_JoystickAttachVirtualEx failed: %s"), appFromAnsi(SDL_GetError()) );
		return;
	}
	TouchPadJoystick = SDL_JoystickOpen( Index );
	if( !TouchPadJoystick )
	{
		debugf( NAME_Init, TEXT("SDL2 pads: no on-screen pad: SDL_JoystickOpen failed: %s"), appFromAnsi(SDL_GetError()) );
		SDL_JoystickDetachVirtual( Index );
		return;
	}
	FeedTouchPad();
	// The controller arrives as an SDL_CONTROLLERDEVICEADDED event and takes its port there (PadAdded).
	debugf( NAME_Init, TEXT("SDL2 pads: on-screen pad attached (virtual SDL controller, device %i)"), Index );
	unguard;
}

void USDL2Client::ExitTouchPad()
{
	guard(USDL2Client::ExitTouchPad);
	if( !TouchPadJoystick )
		return;
	SDL_Joystick* Joystick = (SDL_Joystick*)TouchPadJoystick;
	SDL_JoystickID ID = SDL_JoystickInstanceID( Joystick );
	SDL_JoystickClose( Joystick );
	TouchPadJoystick = NULL;
	for( INT i=0; i<SDL_NumJoysticks(); i++ )
		if( SDL_JoystickGetDeviceInstanceID(i)==ID && SDL_JoystickIsVirtual(i) )
		{
			SDL_JoystickDetachVirtual( i );
			break;
		}
	GTouchState.store( 0 );
	unguard;
}

//
// The overlay's state into the virtual controller. SDL applies it when it next updates its joysticks (every
// event poll), so this runs right before PumpEvents polls.
//
void USDL2Client::FeedTouchPad()
{
	if( !TouchPadJoystick )
		return;
	SDL_Joystick* Joystick = (SDL_Joystick*)TouchPadJoystick;
	DWORD Buttons = GTouchButtons.load();
	for( INT b=0; b<SDL_CONTROLLER_BUTTON_MAX; b++ )
		SDL_JoystickSetVirtualButton( Joystick, b, (Buttons >> b) & 1 );
	for( INT a=0; a<SDL_CONTROLLER_AXIS_MAX; a++ )
	{
		FLOAT Value = GTouchAxes[a].load();
		INT Raw;
		if( a==SDL_CONTROLLER_AXIS_TRIGGERLEFT || a==SDL_CONTROLLER_AXIS_TRIGGERRIGHT )
			Raw = appRound( -32768.f + Clamp( Value, 0.f, 1.f ) * 65535.f );	// a trigger's whole range is released..pressed
		else
			Raw = appRound( Clamp( Value, -1.f, 1.f ) * 32767.f );
		SDL_JoystickSetVirtualAxis( Joystick, a, (Sint16)Clamp( Raw, -32768, 32767 ) );
	}
}

UBOOL USDL2Client::IsTouchPad( INT InstanceID )
{
	return TouchPadJoystick && SDL_JoystickInstanceID( (SDL_Joystick*)TouchPadJoystick )==InstanceID;
}

void USDL2Client::PublishTouchPadState()
{
	INT State = 0;
	if( TouchPadJoystick )
	{
		State |= TOUCHPAD_Attached;
		if( FindPadSlot( SDL_JoystickInstanceID( (SDL_Joystick*)TouchPadJoystick ) )!=INDEX_NONE )
			State |= TOUCHPAD_InUse;
	}
	if( MenuActive() )
		State |= TOUCHPAD_Menu;
	GTouchState.store( State );
}

//
// Whether Back has nothing left to go back to: the menu level, with the GUI on its first page (its MenuStack, read
// through the class's property: the GUI package is not this one's).
//
UBOOL USDL2Client::BackLeavesGame()
{
	guard(USDL2Client::BackLeavesGame);
	UGameEngine* GameEngine = Cast<UGameEngine>( Engine );
	ULevel* Level = GameEngine ? GameEngine->GLevel : NULL;
	if( !Level || appStricmp( Level->GetOuter()->GetName(), TEXT("MENU") )!=0 )
		return 0;
	UBaseGUIController* GUI = Viewports.Num() ? Viewports(0)->GUIController : NULL;
	UArrayProperty* Stack = GUI ? FindField<UArrayProperty>( GUI->GetClass(), TEXT("MenuStack") ) : NULL;
	INT Pages = Stack ? ((FArray*)((BYTE*)GUI + Stack->Offset))->Num() : 0;
	debugf( NAME_Log, TEXT("SDL2Drv: Back in the menus with %i page(s) open%s"), Pages, Pages<=1 ? TEXT(": asking to exit") : TEXT("") );
	return Pages <= 1;
	unguard;
}

//
// GameActivity.showExitDialog on the activity: its dialog quits through SDL (SDL_QUIT, then the engine's own exit).
//
void USDL2Client::AskToExit()
{
	guard(USDL2Client::AskToExit);
	JNIEnv* Env = (JNIEnv*)SDL_AndroidGetJNIEnv();
	jobject Activity = (jobject)SDL_AndroidGetActivity();
	if( !Env || !Activity )
	{
		debugf( NAME_Warning, TEXT("SDL2Drv: no activity to ask whether to exit") );
		return;
	}
	jclass Class = Env->GetObjectClass( Activity );
	jmethodID Show = Env->GetMethodID( Class, "showExitDialog", "()V" );
	if( Show )
		Env->CallVoidMethod( Activity, Show );
	if( Env->ExceptionCheck() )
	{
		Env->ExceptionClear();
		debugf( NAME_Warning, TEXT("SDL2Drv: GameActivity.showExitDialog failed") );
	}
	Env->DeleteLocalRef( Class );
	Env->DeleteLocalRef( Activity );
	unguard;
}

#else

void USDL2Client::InitTouchPad() {}
void USDL2Client::ExitTouchPad() {}
void USDL2Client::FeedTouchPad() {}
UBOOL USDL2Client::IsTouchPad( INT InstanceID ) { return 0; }
void USDL2Client::PublishTouchPadState() {}
UBOOL USDL2Client::BackLeavesGame() { return 0; }
void USDL2Client::AskToExit() {}

#endif

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
