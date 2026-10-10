/*=============================================================================
	SDL2Pads.cpp: game pads through SDL's GameController API (M10).

	The specification is Epic's Xbox driver, which this reproduces on SDL:
	  - XboxViewport.cpp:16-42, 397-506  the button table, the 30/255 press threshold
	    (XINPUT_GAMEPAD_MAX_CROSSTALK), raw sticks, key repeat, the per-viewport read
	    with Engine->ActiveControllerId set to the pad, sticks through
	    InteractionMaster->MasterProcessJoyInput then Input->DirectAxis;
	  - XboxClient.cpp:450-482, 678-786, 834-944  hot-plug as IK_JoySlider1/2, pads
	    bound to no viewport going to the InteractionMaster only, the active controller
	    id restored after the pads were read;
	  - XboxViewport.cpp:330-392  rumble from the player's WaveformManager, zeroed on travel.
	    In the browser SDL has no rumble; G-10 drives the Gamepad API's haptics instead (below).
	What is mapping or tuning is ini data: [SDL2Drv.PadMap] (SDL control -> Xbox control) and
	the Pad* settings of [SDL2Drv.SDL2Client]. docs/porting/analysis/controls.md has the
	table with the evidence for every row.

	Split screen (G-6): Start on an unbound pad joins a player as UXboxClient::
	UpdateGamepadUnboundInput does (USDL2Client::PadJoin, SDL2Client.cpp); each bound pad is
	read by its own viewport only.
=============================================================================*/

#include "SDL2Drv.h"
#include "SDL2InputTest.h"
#ifdef __EMSCRIPTEN__
#include <emscripten/em_asm.h>
#endif

FName NAME_SDL2Input;

#ifdef __EMSCRIPTEN__
/*-----------------------------------------------------------------------------
	G-10: rumble in the browser (owner, 2026-10-10: "Web rumble now").

	SDL's Emscripten joystick driver has no rumble (EMSCRIPTEN_JoystickRumble returns SDL_Unsupported(),
	SDL_GameControllerHasRumble is false; G-5 measured every request refused). The browser's Gamepad API has
	haptics on Chrome and Edge: navigator.getGamepads()[i].vibrationActuator.playEffect('dual-rumble',
	{startDelay, duration, weakMagnitude, strongMagnitude}). Both calls below run on the page's MAIN thread (the
	engine runs on a worker under PROXY_TO_PTHREAD, where there is no navigator.getGamepads): the probe at
	connect is a synchronous proxied call, the effects are asynchronous ones (the engine never waits for the
	page). No actuator (Firefox, Safari, a pad without motors): logged once when the pad connects, then nothing.
	Notebook: docs/porting/analysis/web-g10.md.
-----------------------------------------------------------------------------*/

//
// The browser's index of a pad SDL opened. SDL's Emscripten driver keeps the Gamepad API index to itself; it
// names the joystick by the browser's Gamepad.id, so this takes the Ordinal-th listed pad with that id (two pads
// of one make: SDL opens them in the order the browser connected them). Returns (index << 1) | has-actuator, or
// -1 when no listed pad has that id.
// (In the EM_ASM blocks: no top-level commas, the macro would split its arguments there, and no // comments,
// the block becomes one line of JavaScript.)
//
static INT WebPadProbe( const char* Id, INT Ordinal )
{
	return MAIN_THREAD_EM_ASM_INT({
		if (typeof navigator === 'undefined' || !navigator.getGamepads) return -1;
		var id = UTF8ToString($0);
		var k = $1;
		var pads = navigator.getGamepads();
		for (var i = 0; i < pads.length; i++) {
			var g = pads[i];
			if (!g || g.id !== id || k-- > 0) continue;
			var a = g.vibrationActuator;
			var dual = !!a && typeof a.playEffect === 'function' &&
				(!a.effects || a.effects.indexOf('dual-rumble') >= 0) && (!a.type || a.type === 'dual-rumble');
			return (g.index << 1) | (dual ? 1 : 0);
		}
		return -1;
	}, Id, Ordinal );
}

//
// One effect on pad Index: the Xbox's left (low-frequency, large) motor is the strong one, the right (high-
// frequency, small) motor the weak one; magnitudes 0..1. DurationMs 0 = stop (reset()).
//
static void WebPadEffect( INT Index, DOUBLE Strong, DOUBLE Weak, INT DurationMs )
{
	MAIN_THREAD_ASYNC_EM_ASM({
		var pads = navigator.getGamepads ? navigator.getGamepads() : [];
		var g = pads[$0];
		var a = g && g.vibrationActuator;
		if (!a || typeof a.playEffect !== 'function') return;
		var p = $3 > 0 || typeof a.reset !== 'function'
			? a.playEffect('dual-rumble', { startDelay: 0, duration: $3, weakMagnitude: $2, strongMagnitude: $1 })
			: a.reset();
		if (p && typeof p.catch === 'function') p.catch(function () {});
	}, Index, Strong, Weak, DurationMs );
}
#endif

/*-----------------------------------------------------------------------------
	The SDL control names used by [SDL2Drv.PadMap].
-----------------------------------------------------------------------------*/

// Sources 0..31 are SDL_GameControllerButton values, 32..63 SDL_GameControllerAxis values.
#define PAD_AXIS_SOURCE(a)	(32 + (a))
#define PAD_TARGET_STICK	16	// targets 16..19 = stick axes LeftX, LeftY, RightX, RightY

static const TCHAR* GStickAxisKeyNames[SDL2DRV_PAD_STICKAXES] = { TEXT("JoyX"), TEXT("JoyY"), TEXT("JoyU"), TEXT("JoyV") };
static const EInputKey GStickAxisKeys[SDL2DRV_PAD_STICKAXES] = { IK_JoyX, IK_JoyY, IK_JoyU, IK_JoyV };

//
// The compiled default of [SDL2Drv.PadMap], the table in controls.md: used for every
// control the ini section does not name (an ini from the PC or Xbox release has no such section).
//
static void DefaultPadMap( FSDL2PadMapEntry* Map )
{
	for( INT i=0; i<SDL2DRV_PAD_SOURCES; i++ )
		Map[i].Target = -1, Map[i].Invert = 0;
	#define BTN(b,x)	Map[SDL_CONTROLLER_BUTTON_##b].Target = (x)
	BTN(A,0); BTN(B,1); BTN(X,2); BTN(Y,3);
	BTN(RIGHTSHOULDER,4);	// Black
	BTN(LEFTSHOULDER,5);	// White
	BTN(DPAD_UP,8); BTN(DPAD_DOWN,9); BTN(DPAD_LEFT,10); BTN(DPAD_RIGHT,11);
	BTN(START,12); BTN(BACK,13); BTN(LEFTSTICK,14); BTN(RIGHTSTICK,15);
	#undef BTN
	Map[PAD_AXIS_SOURCE(SDL_CONTROLLER_AXIS_TRIGGERLEFT)].Target  = 6;
	Map[PAD_AXIS_SOURCE(SDL_CONTROLLER_AXIS_TRIGGERRIGHT)].Target = 7;
	Map[PAD_AXIS_SOURCE(SDL_CONTROLLER_AXIS_LEFTX)].Target  = PAD_TARGET_STICK+0;
	Map[PAD_AXIS_SOURCE(SDL_CONTROLLER_AXIS_LEFTY)].Target  = PAD_TARGET_STICK+1;
	Map[PAD_AXIS_SOURCE(SDL_CONTROLLER_AXIS_LEFTY)].Invert  = 1;
	Map[PAD_AXIS_SOURCE(SDL_CONTROLLER_AXIS_RIGHTX)].Target = PAD_TARGET_STICK+2;
	Map[PAD_AXIS_SOURCE(SDL_CONTROLLER_AXIS_RIGHTY)].Target = PAD_TARGET_STICK+3;
	Map[PAD_AXIS_SOURCE(SDL_CONTROLLER_AXIS_RIGHTY)].Invert = 1;
}

//
// "Joy1".."Joy16" -> 0..15, "JoyX/JoyY/JoyU/JoyV" -> stick targets, anything else -> -1.
//
static INT ParsePadTarget( const TCHAR* Name )
{
	for( INT i=0; i<SDL2DRV_PAD_STICKAXES; i++ )
		if( appStricmp(Name, GStickAxisKeyNames[i])==0 )
			return PAD_TARGET_STICK + i;
	if( appStrnicmp(Name, TEXT("Joy"), 3)==0 && appIsDigit(Name[3]) )
	{
		INT N = appAtoi( Name+3 );
		if( N>=1 && N<=SDL2DRV_PAD_BUTTONS )
			return N-1;
	}
	if( appStricmp(Name, TEXT("None"))==0 )
		return -2;	// explicitly unmapped
	return -1;
}

//
// [SDL2Drv.PadMap]: <sdl control>=<engine key> [Invert]. Controls are SDL's own names
// (SDL_GameControllerGetStringForButton/Axis: a b x y back guide start leftstick rightstick
// leftshoulder rightshoulder dpup dpdown dpleft dpright misc1 paddle1..4 touchpad, and
// leftx lefty rightx righty lefttrigger righttrigger); keys are Joy1..Joy16 (Xbox button
// index + 1, so they keep the Xbox threshold and analog semantics) or JoyX JoyY JoyU JoyV.
//
void USDL2Client::LoadPadMap()
{
	guard(USDL2Client::LoadPadMap);
	DefaultPadMap( PadMap );

	TMultiMap<FName,FString>* Section = GConfig->GetSectionPrivate( TEXT("SDL2Drv.PadMap"), 0, 1 );
	if( !Section )
		return;
	INT Count = 0;
	for( TMultiMap<FName,FString>::TIterator It(*Section); It; ++It )
	{
		const ANSICHAR* Control = TCHAR_TO_ANSI( *It.Key() );
		INT Source = -1;
		SDL_GameControllerButton Button = SDL_GameControllerGetButtonFromString( Control );
		if( Button != SDL_CONTROLLER_BUTTON_INVALID )
			Source = Button;
		else
		{
			SDL_GameControllerAxis Axis = SDL_GameControllerGetAxisFromString( Control );
			if( Axis != SDL_CONTROLLER_AXIS_INVALID )
				Source = PAD_AXIS_SOURCE(Axis);
		}
		FString Value = It.Value(), TargetName, Rest;
		if( !Value.Split(TEXT(" "), &TargetName, &Rest) )
			TargetName = Value, Rest = TEXT("");
		INT Target = ParsePadTarget( *TargetName );
		if( Source<0 || Source>=SDL2DRV_PAD_SOURCES || Target==-1 )
		{
			debugf( NAME_Warning, TEXT("[SDL2Drv.PadMap] %s=%s: unknown control or key, ignored"), *It.Key(), *Value );
			continue;
		}
		PadMap[Source].Target = Target==-2 ? -1 : Target;
		PadMap[Source].Invert = appStrfind(*Rest, TEXT("Invert"))!=NULL;
		Count++;
	}
	debugf( NAME_Init, TEXT("SDL2 pads: %i entries from [SDL2Drv.PadMap]"), Count );
	unguard;
}

/*-----------------------------------------------------------------------------
	Setup.
-----------------------------------------------------------------------------*/

UBOOL USDL2Client::LoggingInput()
{
	return LogInput || InputTest!=NULL;
}

void USDL2Client::InitPads()
{
	guard(USDL2Client::InitPads);
	for( INT i=0; i<SDL2DRV_MAX_PADS; i++ )
	{
		appMemzero( &Pads[i], sizeof(Pads[i]) );
		Pads[i].InstanceID = -1;
		Pads[i].RepeatKey  = IK_None;
	}
	if( !UseJoystick )
	{
		debugf( NAME_Init, TEXT("SDL2 pads: off (UseJoystick=False)") );
		return;
	}
	if( SDL_InitSubSystem( SDL_INIT_GAMECONTROLLER ) != 0 )
	{
		debugf( NAME_Init, TEXT("SDL2 pads: SDL_INIT_GAMECONTROLLER failed: %s"), appFromAnsi(SDL_GetError()) );
		return;
	}
	LoadPadMap();
	PadsInitialized = 1;
	// Pads already connected arrive as SDL_CONTROLLERDEVICEADDED events on the next pump.
	debugf( NAME_Init, TEXT("SDL2 pads: GameController API up, %i joystick(s) present, threshold %i/255, stick dead zone %.2f"),
		SDL_NumJoysticks(), PadButtonThreshold, PadStickDeadZone );
	unguard;
}

void USDL2Client::ExitPads()
{
	guard(USDL2Client::ExitPads);
	for( INT i=0; i<SDL2DRV_MAX_PADS; i++ )
	{
		if( Pads[i].Controller )
		{
			SetPadRumble( i, 0, 0 );
			SDL_GameControllerClose( (SDL_GameController*)Pads[i].Controller );
			Pads[i].Controller = NULL;
			Pads[i].InstanceID = -1;
		}
	}
	if( PadsInitialized )
		SDL_QuitSubSystem( SDL_INIT_GAMECONTROLLER );
	PadsInitialized = 0;
	unguard;
}

INT USDL2Client::FindPadSlot( INT InstanceID )
{
	for( INT i=0; i<SDL2DRV_MAX_PADS; i++ )
		if( Pads[i].Controller && Pads[i].InstanceID==InstanceID )
			return i;
	return INDEX_NONE;
}

//
// The viewport a pad port belongs to (UXboxClient::GetGamepadViewport): the one whose
// GamepadID is the port. On PC, ULevel::SpawnPlayActor gives the first viewport GamepadID 0,
// so the first pad and the keyboard both drive player one.
//
UViewport* USDL2Client::GetPadViewport( INT Slot )
{
	for( INT i=0; i<Viewports.Num(); i++ )
		if( Viewports(i)->GetGamepadID()==Slot && Viewports(i)->Actor )
			return Viewports(i);
	return NULL;
}

//
// A key event from a pad port: to its viewport through the engine, or, for a pad bound to
// no viewport, to the interaction master only (Xbox: "we should never send input from
// unbound gamepads to the engine's input system"). The caller sets ActiveControllerId.
//
void USDL2Client::SendPadKey( INT Slot, UViewport* Viewport, EInputKey Key, EInputAction Action )
{
	guard(USDL2Client::SendPadKey);
	if( LoggingInput() && Action!=IST_Hold )
		debugf( SDL2INPUT, TEXT("pad %i %s %s (%s)"), Slot,
			Viewport && Viewport->Input ? Viewport->Input->GetKeyName(Key) : TEXT("?"),
			Action==IST_Press ? TEXT("press") : Action==IST_Release ? TEXT("release") : TEXT("?"),
			Viewport ? TEXT("bound") : TEXT("unbound") );
	if( Viewport )
		Engine->InputEvent( Viewport, Key, Action );
	else if( InteractionMaster )
		InteractionMaster->MasterProcessKeyEvent( Key, Action, 0.f );
	unguard;
}

//
// A pad was connected: first free port, as the Xbox's four fixed ports.
//
void USDL2Client::PadAdded( INT DeviceIndex )
{
	guard(USDL2Client::PadAdded);
	if( !PadsInitialized || !SDL_IsGameController(DeviceIndex) )
		return;
	SDL_JoystickID ID = SDL_JoystickGetDeviceInstanceID( DeviceIndex );
	if( FindPadSlot(ID)!=INDEX_NONE )
		return;
	INT Slot = INDEX_NONE;
	for( INT i=0; i<SDL2DRV_MAX_PADS && Slot==INDEX_NONE; i++ )
		if( !Pads[i].Controller )
			Slot = i;
	if( Slot==INDEX_NONE )
	{
		debugf( NAME_Log, TEXT("SDL2 pads: all %i ports in use, pad ignored"), (INT)SDL2DRV_MAX_PADS );
		return;
	}
	SDL_GameController* GC = SDL_GameControllerOpen( DeviceIndex );
	if( !GC )
	{
		debugf( NAME_Log, TEXT("SDL2 pads: SDL_GameControllerOpen failed: %s"), appFromAnsi(SDL_GetError()) );
		return;
	}
	FSDL2Pad& Pad = Pads[Slot];
	appMemzero( &Pad, sizeof(Pad) );
	Pad.Controller = GC;
	Pad.InstanceID = ID;
	Pad.RepeatKey  = IK_None;
	const char* Name = SDL_GameControllerName( GC );
	appStrncpy( Pad.Name, Name ? appFromAnsi(Name) : TEXT("pad"), ARRAY_COUNT(Pad.Name) );
	debugf( NAME_Log, TEXT("SDL2 pads: pad %i connected: %s%s"), Slot, Pad.Name,
		SDL_GameControllerHasRumble(GC) ? TEXT(" (rumble)") : TEXT("") );
#ifdef __EMSCRIPTEN__
	// G-10: SDL has no rumble here; look for the browser's haptics on this pad (once, at connect).
	Pad.WebIndex = -1;
	if( !SDL_GameControllerHasRumble(GC) )
	{
		const char* JoyName = SDL_JoystickName( SDL_GameControllerGetJoystick(GC) );
		INT Ordinal = 0;
		for( INT i=0; i<SDL2DRV_MAX_PADS; i++ )
			if( i!=Slot && Pads[i].Controller && Pads[i].InstanceID<ID && JoyName )
			{
				const char* Other = SDL_JoystickName( SDL_GameControllerGetJoystick((SDL_GameController*)Pads[i].Controller) );
				if( Other && !SDL_strcmp(Other, JoyName) )
					Ordinal++;
			}
		INT Probe = JoyName ? WebPadProbe( JoyName, Ordinal ) : -1;
		Pad.WebIndex   = Probe>=0 ? (Probe>>1) : -1;
		Pad.WebHaptics = Probe>=0 && (Probe&1);
		if( Pad.WebHaptics )
			debugf( NAME_Log, TEXT("SDL2 pads: pad %i rumble through the browser: Gamepad %i vibrationActuator, dual-rumble"), Slot, Pad.WebIndex );
		else
			debugf( NAME_Log, TEXT("SDL2 pads: pad %i has no rumble: SDL has none in the browser and %s"), Slot,
				Probe<0 ? TEXT("the browser lists no pad with its id") : TEXT("this browser gives the pad no vibrationActuator (dual-rumble: Chrome, Edge)") );
	}
#endif

	// UXboxClient::DetectDevices: insertion is IK_JoySlider1 to the pad's viewport, or to the
	// interaction master when no viewport has this pad.
	INT OldId = Engine->ActiveControllerId;
	Engine->ActiveControllerId = Slot;
	UViewport* Viewport = GetPadViewport( Slot );
	SendPadKey( Slot, Viewport, IK_JoySlider1, IST_Press );
	SendPadKey( Slot, Viewport, IK_JoySlider1, IST_Release );
	Engine->ActiveControllerId = OldId;
	unguard;
}

void USDL2Client::PadRemoved( INT InstanceID )
{
	guard(USDL2Client::PadRemoved);
	INT Slot = FindPadSlot( InstanceID );
	if( Slot==INDEX_NONE )
		return;
	FSDL2Pad& Pad = Pads[Slot];
	INT OldId = Engine->ActiveControllerId;
	Engine->ActiveControllerId = Slot;
	UViewport* Viewport = GetPadViewport( Slot );

	// Release what was held (the Xbox only clears its table; a key left down in UInput would
	// keep firing or moving until the next ResetInput).
	for( INT b=0; b<SDL2DRV_PAD_BUTTONS; b++ )
		if( Pad.Position[b] > PadButtonThreshold )
			SendPadKey( Slot, Viewport, (EInputKey)(IK_Joy1+b), IST_Release );

	SendPadKey( Slot, Viewport, IK_JoySlider2, IST_Press );
	SendPadKey( Slot, Viewport, IK_JoySlider2, IST_Release );
	Engine->ActiveControllerId = OldId;

	debugf( NAME_Log, TEXT("SDL2 pads: pad %i disconnected: %s"), Slot, Pad.Name );
	SDL_GameControllerClose( (SDL_GameController*)Pad.Controller );
	appMemzero( &Pad, sizeof(Pad) );
	Pad.InstanceID = -1;
	Pad.RepeatKey  = IK_None;
	unguard;
}

/*-----------------------------------------------------------------------------
	Reading.
-----------------------------------------------------------------------------*/

//
// The pad's state in Xbox terms: 16 button positions 0..255 (XInput's bAnalogButtons and
// wButtons, XboxViewport.cpp:455-463) and the four stick axes in -1..1 with up positive
// (sThumb / 32768).
//
void USDL2Client::ReadPad( INT Slot, BYTE* NewPosition )
{
	FSDL2Pad& Pad = Pads[Slot];
	SDL_GameController* GC = (SDL_GameController*)Pad.Controller;
	appMemzero( NewPosition, SDL2DRV_PAD_BUTTONS );
	FLOAT Axes[SDL2DRV_PAD_STICKAXES] = { 0.f, 0.f, 0.f, 0.f };

	for( INT Source=0; Source<SDL2DRV_PAD_SOURCES; Source++ )
	{
		const FSDL2PadMapEntry& Entry = PadMap[Source];
		if( Entry.Target < 0 )
			continue;
		if( Source < 32 )
		{
			if( Source >= SDL_CONTROLLER_BUTTON_MAX )
				continue;
			UBOOL Down = SDL_GameControllerGetButton( GC, (SDL_GameControllerButton)Source ) != 0;
			if( Entry.Target < SDL2DRV_PAD_BUTTONS )
				NewPosition[Entry.Target] = Max<BYTE>( NewPosition[Entry.Target], Down ? 255 : 0 );
			else
				Axes[Entry.Target-PAD_TARGET_STICK] += (Down ? 1.f : 0.f) * (Entry.Invert ? -1.f : 1.f);
		}
		else
		{
			INT Axis = Source - 32;
			if( Axis >= SDL_CONTROLLER_AXIS_MAX )
				continue;
			INT Raw = SDL_GameControllerGetAxis( GC, (SDL_GameControllerAxis)Axis );
			if( Entry.Target < SDL2DRV_PAD_BUTTONS )
			{
				// An axis as an analog button (the triggers): 0..32767 -> 0..255.
				INT Value = Entry.Invert ? -Raw : Raw;
				BYTE Analog = (BYTE)Clamp( (Value * 255 + 16383) / 32767, 0, 255 );
				NewPosition[Entry.Target] = Max<BYTE>( NewPosition[Entry.Target], Analog );
			}
			else
				Axes[Entry.Target-PAD_TARGET_STICK] += (Raw / 32768.f) * (Entry.Invert ? -1.f : 1.f);
		}
	}

	// Optional radial dead zone per stick (the Xbox driver has none: the bindings' DeadZone= does it).
	for( INT Stick=0; Stick<2; Stick++ )
	{
		FLOAT& X = Axes[Stick*2+0];
		FLOAT& Y = Axes[Stick*2+1];
		X = Clamp( X, -1.f, 1.f );
		Y = Clamp( Y, -1.f, 1.f );
		if( PadStickDeadZone > 0.f )
		{
			FLOAT Len = appSqrt( X*X + Y*Y );
			if( Len <= PadStickDeadZone )
				X = Y = 0.f;
			else if( PadStickDeadZone < 1.f )
			{
				FLOAT Scale = Min( 1.f, (Len - PadStickDeadZone) / (1.f - PadStickDeadZone) ) / Len;
				X *= Scale;
				Y *= Scale;
			}
		}
	}
	appMemcpy( Pad.Axes, Axes, sizeof(Axes) );
}

//
// Compare the new positions with the old ones and send the edges, as XboxViewport.cpp:455-477:
// pressed when the value rises above the threshold, released when it falls to it or below.
//
static void SendButtonEdges( USDL2Client* Client, INT Slot, UViewport* Viewport, const BYTE* NewPosition )
{
	FSDL2Pad& Pad = Client->Pads[Slot];
	const INT T = Clamp( Client->PadButtonThreshold, 0, 254 );
	for( INT b=0; b<SDL2DRV_PAD_BUTTONS; b++ )
	{
		BYTE Old = Pad.Position[b];
		BYTE New = NewPosition[b];
		Pad.Position[b] = New;
		EInputKey Key = (EInputKey)(IK_Joy1 + b);
		if( Old <= T && New > T )
		{
			if( Client->LoggingInput() )
				debugf( SDL2INPUT, TEXT("pad %i button %i value %i > %i"), Slot, b, New, T );
			// G-6: Start on a pad bound to no viewport may join a player (XboxClient.cpp:881); a press
			// that made a player is used up there.
			if( !Viewport && b==12 && Client->PadJoin( Slot ) )
				continue;
			Client->SendPadKey( Slot, Viewport, Key, IST_Press );
			Pad.RepeatKey      = Key;
			Pad.NextRepeatTime = appSeconds() + Client->PadRepeatDelay;
		}
		else if( Old > T && New <= T )
		{
			if( Client->LoggingInput() )
				debugf( SDL2INPUT, TEXT("pad %i button %i value %i <= %i"), Slot, b, New, T );
			Client->SendPadKey( Slot, Viewport, Key, IST_Release );
			if( Pad.RepeatKey==Key )
				Pad.RepeatKey = IK_None;
		}
	}
}

//
// A pad bound to this viewport (UXboxViewport::UpdateInput): buttons to the engine, sticks
// to the interaction master first (the GUI turns them into D-pad moves) and otherwise to the
// bindings through DirectAxis, then key repeat and rumble. Runs from the viewport's
// UpdateInput, i.e. once per player tick with that tick's DeltaSeconds.
//
void USDL2Client::UpdateBoundPad( USDL2Viewport* Viewport, FLOAT DeltaSeconds )
{
	guard(USDL2Client::UpdateBoundPad);
	INT Slot = Viewport->GetGamepadID();
	if( !PadsInitialized || Slot<0 || Slot>=SDL2DRV_MAX_PADS || !Pads[Slot].Controller || !Viewport->Actor || !Viewport->Input )
		return;
	FSDL2Pad& Pad = Pads[Slot];
	INT OldId = Engine->ActiveControllerId;
	Engine->ActiveControllerId = Slot;

	BYTE NewPosition[SDL2DRV_PAD_BUTTONS];
	ReadPad( Slot, NewPosition );
	SendButtonEdges( this, Slot, Viewport, NewPosition );

	for( INT a=0; a<SDL2DRV_PAD_STICKAXES && Viewport->Input; a++ )
	{
		if( !InteractionMaster || !InteractionMaster->MasterProcessJoyInput( Slot, GStickAxisKeys[a], Pad.Axes[a] ) )
			if( Viewport->Input )
				Viewport->Input->DirectAxis( GStickAxisKeys[a], Pad.Axes[a], DeltaSeconds );
	}

	if( Pad.RepeatKey!=IK_None && appSeconds() >= Pad.NextRepeatTime )
	{
		Engine->InputEvent( Viewport, (EInputKey)Pad.RepeatKey, IST_Hold );
		Pad.NextRepeatTime = appSeconds() + PadRepeatRate;
	}

	// Rumble (UXboxViewport::UpdateGamepadRumble).
	_WORD Left = 0, Right = 0;
	FLOAT Hold = 0.f;
	UWaveformManager* Manager = Viewport->Actor ? Viewport->Actor->WaveformManager : NULL;
	if( Manager )
	{
		Manager->GetRumbleState( DeltaSeconds, Left, Right );
		// How long these levels hold: the rest of the waveform's current sample (G-10, the browser's effects
		// need a duration; the Xbox motors and SDL's refreshed request do not use it).
		UForceFeedbackWaveform* W = Manager->FFWaveform;
		if( W && !Manager->bIsPaused && Manager->CurrentSample>=0 && Manager->CurrentSample<W->Samples.Num() )
			Hold = Max( W->Samples(Manager->CurrentSample).Duration - Manager->ElapsedTime, 0.f );
	}
	SetPadRumble( Slot, Left, Right, Hold );

	Engine->ActiveControllerId = OldId;
	unguard;
}

//
// Pads bound to no viewport (UXboxClient::UpdateGamepadUnboundInput): their buttons reach
// the interaction master only, with ActiveControllerId set to the port so the GUI can
// filter them by its ControllerMask.
//
void USDL2Client::UpdateUnboundPads()
{
	guard(USDL2Client::UpdateUnboundPads);
	if( !PadsInitialized )
		return;
	INT OldId = Engine->ActiveControllerId;
	for( INT Slot=0; Slot<SDL2DRV_MAX_PADS; Slot++ )
	{
		if( !Pads[Slot].Controller || GetPadViewport(Slot) )
			continue;
		Engine->ActiveControllerId = Slot;
		BYTE NewPosition[SDL2DRV_PAD_BUTTONS];
		ReadPad( Slot, NewPosition );
		SendButtonEdges( this, Slot, NULL, NewPosition );
		Pads[Slot].RepeatKey = IK_None;		// the Xbox repeats bound pads only
		SetPadRumble( Slot, 0, 0 );
	}
	Engine->ActiveControllerId = OldId;
	unguard;
}

//
// Motor speeds to SDL. Sent when they change, and refreshed while they run (SDL needs a
// duration; the Xbox motors run until told otherwise).
// G-10, in the browser: to the Gamepad API's haptics instead (SDL has no rumble there): an effect
// per change of level, lasting HoldSeconds (the rest of the waveform sample, from UpdateBoundPad)
// and 50 ms more, sent again if the level still holds when it runs out; a stop when it drops to 0.
//
void USDL2Client::SetPadRumble( INT Slot, _WORD Left, _WORD Right, FLOAT HoldSeconds )
{
	FSDL2Pad& Pad = Pads[Slot];
	if( !Pad.Controller )
		return;
	if( !PadRumble )
		Left = Right = 0;
	else if( PadRumbleScale != 1.f )
	{
		Left  = (_WORD)Clamp( appRound(Left  * PadRumbleScale), 0, 65535 );
		Right = (_WORD)Clamp( appRound(Right * PadRumbleScale), 0, 65535 );
	}
	DOUBLE Now = appSeconds();
	UBOOL Changed = Left!=Pad.RumbleLeft || Right!=Pad.RumbleRight;
#ifdef __EMSCRIPTEN__
	if( Pad.WebHaptics )
	{
		if( !Changed && (!(Left|Right) || Now < Pad.WebHoldUntil) )
			return;
		// A level with no known hold (a caller other than UpdateBoundPad) is refreshed every 0.5 s, as for SDL.
		FLOAT Hold = (Left|Right) ? (HoldSeconds>0.f ? HoldSeconds : 0.5f) : 0.f;
		INT DurationMs = (Left|Right) ? Clamp( appRound(1000.f*Hold) + 50, 50, 5000 ) : 0;
		WebPadEffect( Pad.WebIndex, Left/65535.0, Right/65535.0, DurationMs );
		if( Changed && LoggingInput() )
			debugf( SDL2INPUT, TEXT("pad %i rumble %u %u -> browser dual-rumble strong %.3f weak %.3f, %i ms"), Slot,
				(DWORD)Left, (DWORD)Right, Left/65535.0, Right/65535.0, DurationMs );
		Pad.RumbleLeft     = Left;
		Pad.RumbleRight    = Right;
		Pad.RumbleSentTime = Now;
		Pad.WebHoldUntil   = Now + Hold;
		return;
	}
#endif
	if( !Changed && (!(Left|Right) || Now - Pad.RumbleSentTime < 0.5) )
		return;
	// The Xbox's left motor is the low-frequency (large) one, as SDL's first argument.
	INT Result = SDL_GameControllerRumble( (SDL_GameController*)Pad.Controller, Left, Right, (Left|Right) ? 1000 : 0 );
	if( Changed && LoggingInput() )
		debugf( SDL2INPUT, TEXT("pad %i rumble %u %u -> SDL_GameControllerRumble %i"), Slot, (DWORD)Left, (DWORD)Right, Result );
	Pad.RumbleLeft     = Left;
	Pad.RumbleRight    = Right;
	Pad.RumbleSentTime = Now;
}

BYTE USDL2Client::GetPadAnalogButton( INT Slot, INT ButtonIndex )
{
	if( Slot<0 || Slot>=SDL2DRV_MAX_PADS || !Pads[Slot].Controller || ButtonIndex<0 || ButtonIndex>=SDL2DRV_PAD_BUTTONS )
		return 0;
	return Pads[Slot].Position[ButtonIndex];
}

//
// Hot-plug and unbound pads, once per client tick.
//
void USDL2Client::PollGamepads( FLOAT DeltaSeconds )
{
	guard(USDL2Client::PollGamepads);
	if( InputTest )
		InputTest->Tick( this );
	UpdateUnboundPads();
	unguard;
}

/*-----------------------------------------------------------------------------
	Console commands.
-----------------------------------------------------------------------------*/

UBOOL USDL2Client::PadExec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(USDL2Client::PadExec);
	if( ParseCommand(&Cmd,TEXT("SETDEADZONE")) )
	{
		// UXboxClient::Exec: stores the value; the GUI and UCPlayerController call it.
		FString Value;
		if( ParseToken(Cmd, Value, 0) )
			JoyDeadZone = Value==TEXT("Reset") ? GetDefault<USDL2Client>()->JoyDeadZone : appAtof(*Value);
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("PADS")) )
	{
		// List the ports.
		for( INT i=0; i<SDL2DRV_MAX_PADS; i++ )
		{
			FSDL2Pad& P = Pads[i];
			if( P.Controller )
				Ar.Logf( TEXT("pad %i: %s, viewport %s, sticks %.2f %.2f %.2f %.2f"), i, P.Name,
					GetPadViewport(i) ? GetPadViewport(i)->GetName() : TEXT("none"), P.Axes[0], P.Axes[1], P.Axes[2], P.Axes[3] );
			else
				Ar.Logf( TEXT("pad %i: empty"), i );
		}
		return 1;
	}
	return 0;
	unguard;
}

/*-----------------------------------------------------------------------------
	The viewport's side.
-----------------------------------------------------------------------------*/

//
// UPlayer::GetAnalogButtonAmount returns 255 on every non-Xbox viewport, so UCPlayerInput's
// timed input sequences (UCPlayerController.uc InputRequests: triggers, A, X) saw those
// buttons held all the time. Here: the bound pad's analog value (XGB_* index), or 255 while
// a keyboard or mouse key is down whose binding is the same command as that pad button's
// (the key that does what the button does counts as the button).
//
BYTE USDL2Viewport::GetAnalogButtonAmount( INT ButtonIndex )
{
	guard(USDL2Viewport::GetAnalogButtonAmount);
	USDL2Client* Client = GetOuterUSDL2Client();
	BYTE Amount = Client->GetPadAnalogButton( GamepadID, ButtonIndex );
	if( Amount < 255 && Input && ButtonIndex>=0 && ButtonIndex<SDL2DRV_PAD_BUTTONS )
	{
		const FString& PadBinding = Input->Bindings[IK_Joy1+ButtonIndex];
		if( PadBinding.Len() )
			for( INT k=1; k<IK_Joy1; k++ )
				if( Input->KeyDown(k) && Input->Bindings[k].Len() && appStricmp(*Input->Bindings[k], *PadBinding)==0 )
					return 255;
	}
	return Amount;
	unguard;
}

//
// UXboxViewport::SetClientTravel: no rumble left running while the next map loads.
//
void USDL2Viewport::SetClientTravel( const TCHAR* NextURL, UBOOL bItems, ETravelType NextTravelType )
{
	guard(USDL2Viewport::SetClientTravel);
	Super::SetClientTravel( NextURL, bItems, NextTravelType );
	USDL2Client* Client = GetOuterUSDL2Client();
	if( GamepadID < SDL2DRV_MAX_PADS )
		Client->SetPadRumble( GamepadID, 0, 0 );
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
