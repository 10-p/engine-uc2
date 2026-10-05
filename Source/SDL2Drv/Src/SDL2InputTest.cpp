/*=============================================================================
	SDL2InputTest.cpp: the M10 input test channel (see SDL2InputTest.h).

	Only active with -inputtest=<file>. Nothing here runs in a normal game.
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
,	Virtual( NULL )
,	VirtualID( -1 )
{
	File = fopen( TCHAR_TO_ANSI(InPath), "r" );
	debugf( SDL2INPUT, TEXT("input test channel %s: %s"), InPath, File ? TEXT("open") : TEXT("cannot open") );
}

FSDL2InputTest::~FSDL2InputTest()
{
	Detach();
	if( File )
		fclose( File );
}

void FSDL2InputTest::Detach()
{
	if( !Virtual )
		return;
	SDL_JoystickClose( Virtual );
	Virtual = NULL;
	for( INT i=0; i<SDL_NumJoysticks(); i++ )
		if( SDL_JoystickGetDeviceInstanceID(i)==VirtualID )
		{
			SDL_JoystickDetachVirtual( i );
			break;
		}
	VirtualID = -1;
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
			Desc.name        = "UC2 M10 virtual pad";
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
			debugf( SDL2INPUT, TEXT("virtual pad: attached as joystick %i, game controller %i"), Index, SDL_IsGameController(Index) );
			return 1;
		}
		else if( ParseCommand(&Cmd,TEXT("DETACH")) )
		{
			Detach();
			debugf( SDL2INPUT, TEXT("virtual pad: detached") );
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

	debugf( SDL2INPUT,
		TEXT("probe %s level=%s menu=%s focus=%s text=\"%s\" item=\"%s\" paused=%i pawn=%i loc=(%.1f,%.1f,%.1f) rot=(%i,%i,%i) weapon=%s ammo=%.1f shots=%i mouse=(%.0f,%.0f) pads=%i"),
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
		(INT)Client->PadsInitialized ? SDL_NumJoysticks() : 0 );
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
