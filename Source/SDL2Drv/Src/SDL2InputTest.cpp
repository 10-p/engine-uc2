/*=============================================================================
	SDL2InputTest.cpp: the M10 input test channel (see SDL2InputTest.h).

	Only active with the test key [SDL2Drv.SDL2Client] InputTestFile=<file> (-ini:). Nothing here runs in a normal game.
=============================================================================*/

#include "SDL2Drv.h"
#include "SDL2InputTest.h"
#include <stdio.h>

/*-----------------------------------------------------------------------------
	Virtual pad.
-----------------------------------------------------------------------------*/

// SDL_GameController buttons A..DPAD_RIGHT and all six axes, in SDL's enum order: with these
// masks SDL's own mapping for virtual pads makes joystick button/axis i controller button/axis i.
enum
{
	VPAD_BUTTONS	= SDL_CONTROLLER_BUTTON_DPAD_RIGHT + 1,
	VPAD_AXES		= SDL_CONTROLLER_AXIS_MAX,
};

static int SDLCALL VirtualPadRumble( void* UserData, Uint16 Low, Uint16 High )
{
	debugf( SDL2INPUT, TEXT("virtual pad: rumble request reached the device: low %u high %u"), (DWORD)Low, (DWORD)High );
	return 0;
}

/*-----------------------------------------------------------------------------
	Reflection helpers for the probe (the GUI's classes are script classes of
	another package; reading them by property name keeps SDL2Drv independent of GUI).
-----------------------------------------------------------------------------*/

static UObject* GetObjectProp( UObject* Obj, const TCHAR* Name )
{
	if( !Obj )
		return NULL;
	UObjectProperty* P = FindField<UObjectProperty>( Obj->GetClass(), Name );
	return P ? *(UObject**)((BYTE*)Obj + P->Offset) : NULL;
}

static FString GetPropText( UObject* Obj, const TCHAR* Name )
{
	if( !Obj )
		return TEXT("");
	UProperty* P = FindField<UProperty>( Obj->GetClass(), Name );
	if( !P )
		return TEXT("");
	TCHAR Buffer[1024] = TEXT("");
	P->ExportText( 0, Buffer, (BYTE*)Obj, (BYTE*)Obj, 0 );
	return Buffer;
}

//
// D-6: a struct property's bytes (a vector or rotator of a script class), NULL if there is none.
//
static BYTE* GetStructPropData( UObject* Obj, const TCHAR* Name, const TCHAR* StructName )
{
	if( !Obj )
		return NULL;
	UStructProperty* P = FindField<UStructProperty>( Obj->GetClass(), Name );
	if( !P || !P->Struct || appStricmp(P->Struct->GetName(), StructName)!=0 )
		return NULL;
	return (BYTE*)Obj + P->Offset;
}

static FVector GetVectorProp( UObject* Obj, const TCHAR* Name )
{
	BYTE* Data = GetStructPropData( Obj, Name, TEXT("Vector") );
	return Data ? *(FVector*)Data : FVector(0,0,0);
}

static FRotator GetRotatorProp( UObject* Obj, const TCHAR* Name )
{
	BYTE* Data = GetStructPropData( Obj, Name, TEXT("Rotator") );
	return Data ? *(FRotator*)Data : FRotator(0,0,0);
}

//
// D-6: the i-th pawn of another controller (bots and other players), in the level's controller list order.
//
static APawn* OtherPawn( APlayerController* PC, INT Index )
{
	if( !PC || !PC->Level )
		return NULL;
	INT i = 0;
	for( AController* C = PC->Level->ControllerList; C; C = C->nextController )
	{
		if( C==PC || !C->Pawn || C->Pawn==PC->Pawn )
			continue;
		if( i++ == Index )
			return C->Pawn;
	}
	return NULL;
}

//
// The selected row of a GUI list (GUIList: Elements[Index].Item), "" for anything else.
//
static FString GetListItem( UObject* Obj )
{
	if( !Obj )
		return TEXT("");
	UArrayProperty* AP = FindField<UArrayProperty>( Obj->GetClass(), TEXT("Elements") );
	UIntProperty* IP = FindField<UIntProperty>( Obj->GetClass(), TEXT("Index") );
	UStructProperty* SP = AP ? Cast<UStructProperty>( AP->Inner ) : NULL;
	UStrProperty* ItemP = SP ? FindField<UStrProperty>( SP->Struct, TEXT("Item") ) : NULL;
	if( !ItemP || !IP )
		return TEXT("");
	FArray* Elements = (FArray*)((BYTE*)Obj + AP->Offset);
	INT Index = *(INT*)((BYTE*)Obj + IP->Offset);
	if( Index<0 || Index>=Elements->Num() )
		return FString::Printf( TEXT("#%i/%i"), Index, Elements->Num() );
	BYTE* Elem = (BYTE*)Elements->GetData() + Index*AP->Inner->ElementSize;
	FString Result = *(FString*)(Elem + ItemP->Offset);
	UStrProperty* ExtraP = FindField<UStrProperty>( SP->Struct, TEXT("ExtraStrData") );
	if( ExtraP && ((FString*)(Elem + ExtraP->Offset))->Len() )
		Result = Result + TEXT("|") + *(FString*)(Elem + ExtraP->Offset);
	return Result;
}

//
// The text of the first GUIEditBox in a component tree (an on-screen keyboard page's field
// never has the focus), "" if there is none.
//
static UBOOL IsAClassNamed( UObject* Obj, const TCHAR* Name )
{
	for( UClass* C = Obj ? Obj->GetClass() : NULL; C; C = C->GetSuperClass() )
		if( appStricmp(C->GetName(), Name)==0 )
			return 1;
	return 0;
}

static UObject* FindEditBoxByName( UObject* Obj, INT Depth )
{
	if( !Obj || Depth > 32 )
		return NULL;
	if( IsAClassNamed(Obj, TEXT("GUIEditBox")) )
		return Obj;
	UArrayProperty* AP = FindField<UArrayProperty>( Obj->GetClass(), TEXT("Controls") );
	if( !AP || !Cast<UObjectProperty>(AP->Inner) )
		return NULL;
	FArray* Controls = (FArray*)((BYTE*)Obj + AP->Offset);
	for( INT i=0; i<Controls->Num(); i++ )
		if( UObject* Found = FindEditBoxByName( ((UObject**)Controls->GetData())[i], Depth+1 ) )
			return Found;
	return NULL;
}

/*-----------------------------------------------------------------------------
	FSDL2InputTest.
-----------------------------------------------------------------------------*/

FSDL2InputTest::FSDL2InputTest( const TCHAR* InPath )
:	File( NULL )
,	PartialLen( 0 )
,	WatchLeft( 0 )
,	WatchIndex( 0 )
{
	for( INT i=0; i<MAX_VIRTUAL; i++ )
		Virtual[i] = NULL, VirtualID[i] = -1;
	File = fopen( TCHAR_TO_ANSI(InPath), "r" );
	debugf( SDL2INPUT, TEXT("input test channel %s: %s"), InPath, File ? TEXT("open") : TEXT("cannot open") );
}

FSDL2InputTest::~FSDL2InputTest()
{
	for( INT i=0; i<MAX_VIRTUAL; i++ )
		Detach( i );
	if( File )
		fclose( File );
}

void FSDL2InputTest::Detach( INT Pad )
{
	if( !Virtual[Pad] )
		return;
	SDL_JoystickClose( Virtual[Pad] );
	Virtual[Pad] = NULL;
	for( INT i=0; i<SDL_NumJoysticks(); i++ )
		if( SDL_JoystickGetDeviceInstanceID(i)==VirtualID[Pad] )
		{
			SDL_JoystickDetachVirtual( i );
			break;
		}
	VirtualID[Pad] = -1;
}

//
// Run every complete line that has arrived, stopping after a command that changes the
// virtual pad, so each pad change is seen by at least one poll (the driver polls like the
// Xbox did; a press and release in the same frame would be lost there too).
//
void FSDL2InputTest::Tick( USDL2Client* Client )
{
	if( !File )
		return;
	if( WatchLeft > 0 )
		Watch( Client );
	for( ;; )
	{
		int C = fgetc( File );
		if( C==EOF )
		{
			// Lines are appended while we read. clearerr alone is enough for glibc; Microsoft's CRT
			// (as wine's msvcrt implements it, at least) also remembers end-of-file at the descriptor
			// and keeps returning EOF until the next seek. A seek to where we are is the portable form.
			clearerr( File );
			fseek( File, 0, SEEK_CUR );
			return;
		}
		if( C=='\r' )
			continue;
		if( C!='\n' )
		{
			if( PartialLen < (INT)sizeof(Partial)-1 )
				Partial[PartialLen++] = (ANSICHAR)C;
			continue;
		}
		Partial[PartialLen] = 0;
		PartialLen = 0;
		if( !Partial[0] || Partial[0]=='#' )
			continue;
		if( Execute( Client, appFromAnsi(Partial) ) )
			return;
	}
}

UBOOL FSDL2InputTest::Execute( USDL2Client* Client, const TCHAR* Line )
{
	guard(FSDL2InputTest::Execute);
	debugf( SDL2INPUT, TEXT("test: %s"), Line );
	const TCHAR* Cmd = Line;
	if( ParseCommand(&Cmd,TEXT("PAD")) )
	{
		// G-6: an optional virtual pad index first ("pad 2 button start 1"); without one, pad 0.
		INT Pad = 0;
		while( *Cmd==' ' )
			Cmd++;
		if( appIsDigit(*Cmd) )
		{
			Pad = Clamp( appAtoi(Cmd), 0, (INT)MAX_VIRTUAL-1 );
			while( appIsDigit(*Cmd) )
				Cmd++;
		}
		SDL_Joystick*& Virtual = this->Virtual[Pad];
		SDL_JoystickID& VirtualID = this->VirtualID[Pad];
		if( ParseCommand(&Cmd,TEXT("ATTACH")) )
		{
			if( Virtual )
				return 0;
			SDL_VirtualJoystickDesc Desc;
			SDL_zero( Desc );
			Desc.version     = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
			Desc.type        = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
			Desc.naxes       = VPAD_AXES;
			Desc.nbuttons    = VPAD_BUTTONS;
			Desc.button_mask = (1u << VPAD_BUTTONS) - 1;
			Desc.axis_mask   = (1u << VPAD_AXES) - 1;
			static const char* Names[MAX_VIRTUAL] = { "UC2 M10 virtual pad", "UC2 M10 virtual pad 2", "UC2 M10 virtual pad 3", "UC2 M10 virtual pad 4" };
			Desc.name        = Names[Pad];
			Desc.Rumble      = VirtualPadRumble;
			INT Index = SDL_JoystickAttachVirtualEx( &Desc );
			if( Index < 0 )
			{
				debugf( SDL2INPUT, TEXT("virtual pad: attach failed: %s"), appFromAnsi(SDL_GetError()) );
				return 0;
			}
			Virtual   = SDL_JoystickOpen( Index );
			VirtualID = Virtual ? SDL_JoystickInstanceID( Virtual ) : -1;
			// Triggers rest at the bottom of their range (raw -32768 = controller 0).
			if( Virtual )
			{
				SDL_JoystickSetVirtualAxis( Virtual, SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768 );
				SDL_JoystickSetVirtualAxis( Virtual, SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768 );
			}
			debugf( SDL2INPUT, TEXT("virtual pad %i: attached as joystick %i, game controller %i"), Pad, Index, SDL_IsGameController(Index) );
			return 1;
		}
		else if( ParseCommand(&Cmd,TEXT("DETACH")) )
		{
			Detach( Pad );
			debugf( SDL2INPUT, TEXT("virtual pad %i: detached"), Pad );
			return 1;
		}
		else if( ParseCommand(&Cmd,TEXT("BUTTON")) )
		{
			FString Name;
			ParseToken( Cmd, Name, 0 );
			INT Value = appAtoi( Cmd );
			SDL_GameControllerButton B = SDL_GameControllerGetButtonFromString( TCHAR_TO_ANSI(*Name) );
			if( !Virtual || B==SDL_CONTROLLER_BUTTON_INVALID || (INT)B>=(INT)VPAD_BUTTONS )
			{
				debugf( SDL2INPUT, TEXT("virtual pad: bad button command") );
				return 0;
			}
			SDL_JoystickSetVirtualButton( Virtual, B, Value ? SDL_PRESSED : SDL_RELEASED );
			return 1;
		}
		else if( ParseCommand(&Cmd,TEXT("AXIS")) )
		{
			FString Name;
			ParseToken( Cmd, Name, 0 );
			INT Value = appAtoi( Cmd );
			SDL_GameControllerAxis A = SDL_GameControllerGetAxisFromString( TCHAR_TO_ANSI(*Name) );
			if( !Virtual || A==SDL_CONTROLLER_AXIS_INVALID )
			{
				debugf( SDL2INPUT, TEXT("virtual pad: bad axis command") );
				return 0;
			}
			if( A==SDL_CONTROLLER_AXIS_TRIGGERLEFT || A==SDL_CONTROLLER_AXIS_TRIGGERRIGHT )
			{
				// SDL maps a trigger's whole raw range onto the controller's 0..32767.
				Value = Clamp( Value, 0, 32767 );
				Value = Clamp( appRound(-32768.f + Value * (65535.f / 32767.f)), -32768, 32767 );
			}
			SDL_JoystickSetVirtualAxis( Virtual, A, (Sint16)Clamp(Value, -32768, 32767) );
			return 1;
		}
		debugf( SDL2INPUT, TEXT("virtual pad: unknown command") );
		return 0;
	}
	else if( ParseCommand(&Cmd,TEXT("PROBE")) )
	{
		Probe( Client, Cmd );
		return 0;
	}
	else if( ParseCommand(&Cmd,TEXT("VIEWPORTS")) )
	{
		ProbeViewports( Client, Cmd );
		return 0;
	}
	else if( ParseCommand(&Cmd,TEXT("EXEC")) )
	{
		if( Client->Viewports.Num() )
			Client->Viewports(0)->Exec( Cmd, *GLog );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("LOG")) )
	{
		return 0;
	}
	else if( ParseCommand(&Cmd,TEXT("KEYS")) )
	{
		// D-9: the first viewport's bindings of the named keys, one line: keys <tag> <Key>="<binding>" ...
		FString Tag, Name, Out;
		ParseToken( Cmd, Tag, 0 );
		UInput* Input = Client->Viewports.Num() ? Client->Viewports(0)->Input : NULL;
		while( Input && ParseToken( Cmd, Name, 0 ) )
		{
			EInputKey Key;
			FString Value = Input->FindKeyName( *Name, Key ) ? FString(Input->Bindings[Key]) : FString(TEXT("?"));
			Out += FString::Printf( TEXT(" %s=\"%s\""), *Name, *Value.Replace( TEXT("\""), TEXT("'") ) );
		}
		debugf( SDL2INPUT, TEXT("keys %s%s"), *Tag, *Out );
		return 0;
	}
	else if( ParseCommand(&Cmd,TEXT("MOUSE")) )
	{
		// D-6: exact relative motion, as SDL delivers a mouse's motion in relative mode (xrel/yrel in counts,
		// y down = +). Pushed into SDL's queue, so it reaches USDL2Viewport::HandleEvent through PumpEvents like
		// a real event. One per tick: the harness paces a path by writing one line per step.
		FString SX, SY;
		ParseToken( Cmd, SX, 0 );
		ParseToken( Cmd, SY, 0 );
		USDL2Viewport* Viewport = Client->Viewports.Num() ? (USDL2Viewport*)Client->Viewports(0) : NULL;
		if( !Viewport || !Viewport->Window )
		{
			debugf( SDL2INPUT, TEXT("test: mouse: no viewport window") );
			return 0;
		}
		SDL_Event Ev;
		SDL_zero( Ev );
		Ev.type				= SDL_MOUSEMOTION;
		Ev.motion.timestamp	= SDL_GetTicks();
		Ev.motion.windowID	= SDL_GetWindowID( Viewport->Window );
		Ev.motion.which		= 0;
		Ev.motion.xrel		= appAtoi( *SX );
		Ev.motion.yrel		= appAtoi( *SY );
		if( SDL_PushEvent( &Ev ) < 0 )
			debugf( SDL2INPUT, TEXT("test: mouse: SDL_PushEvent failed: %s"), appFromAnsi(SDL_GetError()) );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("PLACE")) || ParseCommand(&Cmd,TEXT("AIM")) )
	{
		// D-6 lock-on set-up (test only): put another pawn where the test wants it, or face it.
		UBOOL bPlace = appStrnicmp( Line, TEXT("PLACE"), 5 )==0;
		FString SI, SD, SA;
		ParseToken( Cmd, SI, 0 );
		APlayerController* PC = Client->Viewports.Num() ? Client->Viewports(0)->Actor : NULL;
		APawn* Other = OtherPawn( PC, appAtoi(*SI) );
		if( !PC || !PC->Pawn || !Other )
		{
			debugf( SDL2INPUT, TEXT("test: %s: no such pawn"), bPlace ? TEXT("place") : TEXT("aim") );
			return 0;
		}
		if( bPlace )
		{
			ParseToken( Cmd, SD, 0 );
			ParseToken( Cmd, SA, 0 );
			FLOAT Yaw = PC->Rotation.Yaw * (2.f*PI/65536.f) + appAtof(*SA) * (PI/180.f);
			FVector Dest = PC->Pawn->Location + appAtof(*SD) * FVector( appCos(Yaw), appSin(Yaw), 0.f );
			UBOOL Moved = PC->GetLevel()->FarMoveActor( Other, Dest );
			Other->Velocity = FVector(0,0,0);
			Other->Rotation = (PC->Pawn->Location - Other->Location).Rotation();
			Other->Rotation.Pitch = Other->Rotation.Roll = 0;
			debugf( SDL2INPUT, TEXT("test: place %s at (%.1f,%.1f,%.1f): %s, los %i"), Other->GetName(),
				Other->Location.X, Other->Location.Y, Other->Location.Z, Moved ? TEXT("moved") : TEXT("blocked"),
				(INT)PC->LineOfSightTo( Other ) );
		}
		else
		{
			FVector Eye = GetVectorProp( PC, TEXT("CurrentCameraLocation") );
			if( Eye.IsZero() )
				Eye = PC->Pawn->Location;
			FRotator R = (Other->Location - Eye).Rotation();
			R.Roll = 0;
			PC->Rotation = R;
			debugf( SDL2INPUT, TEXT("test: aim at %s: rot (%i,%i)"), Other->GetName(), R.Pitch & 65535, R.Yaw & 65535 );
		}
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("WATCH")) )
	{
		// D-6: a state line every client tick for the next <ticks> ticks.
		FString Ticks, Tag;
		ParseToken( Cmd, Ticks, 0 );
		if( !ParseToken( Cmd, Tag, 0 ) )
			Tag = TEXT("w");
		WatchLeft  = Max( 0, appAtoi(*Ticks) );
		WatchIndex = 0;
		WatchTag   = Tag;
		return 0;
	}
	debugf( SDL2INPUT, TEXT("test: unknown command") );
	return 0;
	unguard;
}

//
// One line with the state a test asserts on.
//
void FSDL2InputTest::Probe( USDL2Client* Client, const TCHAR* Tag )
{
	guard(FSDL2InputTest::Probe);
	while( *Tag==' ' )
		Tag++;
	if( !Client->Viewports.Num() || !Client->Viewports(0)->Actor )
	{
		debugf( SDL2INPUT, TEXT("probe %s level=None"), Tag );
		return;
	}
	UViewport* Viewport = Client->Viewports(0);
	APlayerController* PC = Viewport->Actor;
	ULevel* Level = PC->GetLevel();
	APawn* Pawn = PC->Pawn;
	AWeapon* Weapon = Pawn ? Pawn->Weapon : NULL;

	UObject* GUI = Viewport->GUIController;
	UBOOL MenuActive = Viewport->GUIController && Viewport->GUIController->bActive;
	UObject* Page = MenuActive ? GetObjectProp( GUI, TEXT("ActivePage") ) : NULL;
	// Deepest focused control, and its text if it is a text field.
	UObject* Focus = Page;
	for( INT Depth=0; Depth<16; Depth++ )
	{
		UObject* Next = GetObjectProp( Focus, TEXT("FocusedControl") );
		if( !Next || Next==Focus )
			break;
		Focus = Next;
	}
	FString Text = GetPropText( Focus, TEXT("TextStr") );
	if( !Text.Len() && Page )
		Text = GetPropText( FindEditBoxByName( Page, 0 ), TEXT("TextStr") );
	FString Item = GetListItem( Focus );
	// D-7/D-8: the console's typing line, the scoreboard and the player's FOV (base and desired).
	UConsole* Console = Cast<UConsole>( Viewport->Console );
	FString Typed = Console ? Console->TypedStr.Replace( TEXT("\""), TEXT("'") ) : FString();

	debugf( SDL2INPUT,
		TEXT("probe %s level=%s menu=%s focus=%s text=\"%s\" item=\"%s\" paused=%i pawn=%i loc=(%.1f,%.1f,%.1f) rot=(%i,%i,%i) weapon=%s ammo=%.1f shots=%i mouse=(%.0f,%.0f) pads=%i console=%i typed=\"%s\" scores=%i fov=%.3f desiredfov=%.3f view=%ix%i"),
		Tag,
		Level && Level->GetOuter() ? Level->GetOuter()->GetName() : TEXT("None"),
		Page ? Page->GetClass()->GetName() : TEXT("None"),
		Focus && Focus!=Page ? *(FString(Focus->GetName())+TEXT(".")+Focus->GetClass()->GetName()) : TEXT("None"),
		*Text,
		*Item,
		PC->Level && PC->Level->Pauser ? 1 : 0,
		Pawn ? 1 : 0,
		Pawn ? Pawn->Location.X : 0.f, Pawn ? Pawn->Location.Y : 0.f, Pawn ? Pawn->Location.Z : 0.f,
		PC->Rotation.Pitch, PC->Rotation.Yaw, PC->Rotation.Roll,
		Weapon ? Weapon->GetClass()->GetName() : TEXT("None"),
		Weapon ? Weapon->AmmoAmount : 0.f,
		Weapon ? (INT)Weapon->FlashCount : 0,
		GUI ? appAtof(*GetPropText(GUI,TEXT("MouseX"))) : 0.f,
		GUI ? appAtof(*GetPropText(GUI,TEXT("MouseY"))) : 0.f,
		(INT)Client->PadsInitialized ? SDL_NumJoysticks() : 0,
		Console ? (INT)Console->bTyping : -1,
		*Typed,
		PC->myHUD ? (INT)PC->myHUD->bShowScores : -1,
		PC->FovAngle, PC->DesiredFOV,
		Viewport->SizeX, Viewport->SizeY );
	// D-6: the camera the player sees (UCPlayerController's per-frame view), lock-on state and which device owns
	// the look (UCPlayerInput), on a line of their own so older parsers of the line above are unaffected.
	FVector  Cam    = GetVectorProp( PC, TEXT("CurrentCameraLocation") );
	FRotator CamRot = GetRotatorProp( PC, TEXT("CurrentCameraRotation") );
	UObject* Enemy  = GetObjectProp( PC, TEXT("Enemy") );
	debugf( SDL2INPUT,
		TEXT("probe2 %s cam=(%.2f,%.2f,%.2f) camrot=(%i,%i,%i) locked=%s enemy=%s mouselook=%s sens=%s invert=%s padinvert=%s autocenter=%s"),
		Tag, Cam.X, Cam.Y, Cam.Z, CamRot.Pitch, CamRot.Yaw, CamRot.Roll,
		*GetPropText( PC, TEXT("bIsLocked") ),
		Enemy ? Enemy->GetName() : TEXT("None"),
		*GetPropText( PC->PlayerInput, TEXT("bMouseOwnsLook") ),
		*GetPropText( PC->PlayerInput, TEXT("MouseLookSensitivity") ),
		*GetPropText( PC->PlayerInput, TEXT("bInvertMouseLook") ),
		*GetPropText( PC->PlayerInput, TEXT("bInvertMouse") ),
		*GetPropText( PC, TEXT("bAutoCenterPitch") ) );
	// D-9: the caption of the focused control, or of the nearest control above it in the focus chain that has
	// one (a GUILabelButton; the slider of a menu option has none, the option has), on a line of its own.
	FString Caption;
	{
		UObject* Chain[17];
		INT Count = 0;
		for( UObject* C = Page; C && Count<17; )
		{
			Chain[Count++] = C;
			UObject* Next = GetObjectProp( C, TEXT("FocusedControl") );
			if( !Next || Next==C )
				break;
			C = Next;
		}
		for( INT i=Count-1; i>0 && !Caption.Len(); i-- )
			Caption = GetPropText( Chain[i], TEXT("Caption") );
	}
	debugf( SDL2INPUT, TEXT("probe4 %s caption=\"%s\""), Tag, *Caption.Replace( TEXT("\""), TEXT("'") ) );
	// D-6: the other pawns (lock-on targets), with line of sight and the view rotation that faces them
	for( INT i=0; ; i++ )
	{
		APawn* Other = OtherPawn( PC, i );
		if( !Other || !Pawn )
			break;
		FVector Eye = Cam.IsZero() ? Pawn->Location : Cam;
		FRotator To = (Other->Location - Eye).Rotation();
		debugf( SDL2INPUT, TEXT("probe3 %s other %i %s loc=(%.1f,%.1f,%.1f) dist=%.1f los=%i face=(%i,%i)"),
			Tag, i, Other->GetName(), Other->Location.X, Other->Location.Y, Other->Location.Z,
			(Other->Location - Pawn->Location).Size(), (INT)PC->LineOfSightTo( Other ), To.Pitch & 65535, To.Yaw & 65535 );
	}
	unguard;
}

//
// G-6: every viewport of the client: its rectangle of the window, scales, the split-screen flags, its pad and
// its player (pawn location and view rotation, HUD class), for tools/dev/g6-splitscreen.sh.
//
void FSDL2InputTest::ProbeViewports( USDL2Client* Client, const TCHAR* Tag )
{
	guard(FSDL2InputTest::ProbeViewports);
	while( *Tag==' ' )
		Tag++;
	USplitScreenMgr* Mgr = Client->GetSplitScreenMgr();
	INT ScreenX = 0, ScreenY = 0;
	Client->GetInitScreenSize( ScreenX, ScreenY );
	debugf( SDL2INPUT, TEXT("vps %s n=%i split=%i screen=%ix%i"), Tag, Client->Viewports.Num(), Mgr ? Mgr->NumActiveSplitViews() : 0, ScreenX, ScreenY );
	for( INT i=0; i<Client->Viewports.Num(); i++ )
	{
		USDL2Viewport* V = (USDL2Viewport*)Client->Viewports(i);
		APlayerController* PC = V->Actor;
		APawn* Pawn = PC ? PC->Pawn : NULL;
		INT SplitIndex = -1;
		if( Mgr )
			for( INT s=0; s<Mgr->NumActiveSplitViews(); s++ )
				if( Mgr->GetSplitView(s)==V )
					SplitIndex = s;
		debugf( SDL2INPUT,
			TEXT("vp %s %i name=%s pad=%i rect=(%i,%i,%i,%i) scale=(%.3f,%.3f) fovscale=(%.3f,%.3f) menu=%i scene=%i disabled=%i split=%i overlay=%i window=%i actor=%s pawn=%i loc=(%.1f,%.1f,%.1f) rot=(%i,%i,%i) hud=%s fov=%.3f player=%s gui=%i shots=%i acc=(%.0f,%.0f)"),
			Tag, i, V->GetName(), (INT)V->GamepadID, V->OffsetX, V->OffsetY, V->SizeX, V->SizeY, V->ScaleX, V->ScaleY,
			V->ScaleFOVX, V->ScaleFOVY, (INT)V->bShowMenu, (INT)V->bShowScene, (INT)V->bDisableDraw, SplitIndex,
			Mgr && Mgr->IsOverlayViewport(V) ? 1 : 0, V->Window ? 1 : 0,
			PC ? PC->GetClass()->GetName() : TEXT("None"), Pawn ? 1 : 0,
			Pawn ? Pawn->Location.X : 0.f, Pawn ? Pawn->Location.Y : 0.f, Pawn ? Pawn->Location.Z : 0.f,
			PC ? PC->Rotation.Pitch : 0, PC ? PC->Rotation.Yaw : 0, PC ? PC->Rotation.Roll : 0,
			PC && PC->myHUD ? PC->myHUD->GetClass()->GetName() : TEXT("None"), PC ? PC->FovAngle : 0.f,
			PC && PC->PlayerReplicationInfo ? *PC->PlayerReplicationInfo->PlayerName.Replace(TEXT(" "),TEXT("_")) : TEXT("None"),
			V->GUIController ? (INT)V->GUIController->bActive : -1,
			Pawn && Pawn->Weapon ? (INT)Pawn->Weapon->FlashCount : -1,
			Pawn ? Pawn->Acceleration.X : 0.f, Pawn ? Pawn->Acceleration.Y : 0.f );
	}
	unguard;
}

//
// D-6: one compact line per client tick while a watch runs (time, view rotation, camera, pawn).
//
void FSDL2InputTest::Watch( USDL2Client* Client )
{
	guard(FSDL2InputTest::Watch);
	WatchLeft--;
	if( !Client->Viewports.Num() || !Client->Viewports(0)->Actor )
		return;
	APlayerController* PC = Client->Viewports(0)->Actor;
	APawn* Pawn = PC->Pawn;
	FVector  Cam = GetVectorProp( PC, TEXT("CurrentCameraLocation") );
	FRotator CamRot = GetRotatorProp( PC, TEXT("CurrentCameraRotation") );
	debugf( SDL2INPUT,
		TEXT("watch %s %i t=%.4f rot=(%i,%i) cam=(%.2f,%.2f,%.2f) camrot=(%i,%i) loc=(%.2f,%.2f,%.2f) locked=%s mouselook=%s"),
		*WatchTag, WatchIndex++,
		PC->Level ? PC->Level->TimeSeconds : 0.f,
		PC->Rotation.Pitch, PC->Rotation.Yaw,
		Cam.X, Cam.Y, Cam.Z, CamRot.Pitch, CamRot.Yaw,
		Pawn ? Pawn->Location.X : 0.f, Pawn ? Pawn->Location.Y : 0.f, Pawn ? Pawn->Location.Z : 0.f,
		*GetPropText( PC, TEXT("bIsLocked") ),
		*GetPropText( PC->PlayerInput, TEXT("bMouseOwnsLook") ) );
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
