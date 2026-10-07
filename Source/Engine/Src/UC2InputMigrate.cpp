/*=============================================================================
	UC2InputMigrate.cpp: User.ini values from before D-6 moved to D-6's.
	uc2 port (D-6); not Epic's.

	D-6 separated the mouse from the game pad (docs/porting/analysis/mouse.md): the mouse
	is bound to aTurn/aLookUp and added linearly by UCPlayerInput (rebuilt UCGameplay.u),
	the right stick stays on aMouseX/aMouseY with the Xbox's response curve, acceleration
	and dead zone. User.ini is derived from DefUser.ini once and then kept: in a native run
	tree that a player keeps, and in the browser, where the frontend stores it with the
	player's files. A player who ran an earlier build would keep the old bindings, and the
	mouse would go on feeding the stick's curve (capped, accelerated) or a linear stick.

	So at start-up, before the first viewport reads its bindings, a key whose value is
	EXACTLY one an earlier build wrote (Epic's PC DefUser.ini, which the web build carried
	until 2026-10-07; M10's) is set to D-6's value, with one log line each. A value a player
	chose is left alone. The change is in the config cache: it reaches the file when the
	engine writes User.ini (at exit; on the web at `flushconfig`).
=============================================================================*/

#include "EnginePrivate.h"

struct FUC2IniMigration
{
	const TCHAR* Section;
	const TCHAR* Key;
	const TCHAR* Old;
	const TCHAR* New;
};

static const FUC2IniMigration GUC2InputMigrations[] =
{
	// The mouse: Epic's PC file (Speed 6.0) and M10 (1.0) sent it into the stick's variables.
	{ TEXT("Engine.Input"), TEXT("MouseX"),			TEXT("Count bXAxis | Axis aMouseX Speed=6.0"),	TEXT("Count bXAxis | Axis aTurn Speed=1.0") },
	{ TEXT("Engine.Input"), TEXT("MouseX"),			TEXT("Count bXAxis | Axis aMouseX Speed=1.0"),	TEXT("Count bXAxis | Axis aTurn Speed=1.0") },
	{ TEXT("Engine.Input"), TEXT("MouseY"),			TEXT("Count bYAxis | Axis aMouseY Speed=6.0"),	TEXT("Count bYAxis | Axis aLookUp Speed=1.0") },
	{ TEXT("Engine.Input"), TEXT("MouseY"),			TEXT("Count bYAxis | Axis aMouseY Speed=1.0"),	TEXT("Count bYAxis | Axis aLookUp Speed=1.0") },
	// M10's look keys (on the stick's variables).
	{ TEXT("Engine.Input"), TEXT("Left"),			TEXT("Axis aMouseX Speed=-2.5"),				TEXT("Axis aTurn Speed=-16.0") },
	{ TEXT("Engine.Input"), TEXT("Right"),			TEXT("Axis aMouseX Speed=+2.5"),				TEXT("Axis aTurn Speed=+16.0") },
	{ TEXT("Engine.Input"), TEXT("PageUp"),			TEXT("Axis aMouseY Speed=+2.5"),				TEXT("Axis aLookUp Speed=+16.0") },
	{ TEXT("Engine.Input"), TEXT("PageDown"),		TEXT("Axis aMouseY Speed=-2.5"),				TEXT("Axis aLookUp Speed=-16.0") },
	// M10's sticks (the move stick pre-scaled for PlayerMove; dead zone 0.25): the Xbox's bindings.
	{ TEXT("Engine.Input"), TEXT("JoyX"),			TEXT("Axis aStrafe SpeedBase=2400000.0 DeadZone=0.25"),			TEXT("Axis aStrafe SpeedBase=100.0 DeadZone=0.3") },
	{ TEXT("Engine.Input"), TEXT("JoyY"),			TEXT("Axis aForward SpeedBase=2400000.0 DeadZone=0.25"),		TEXT("Axis aForward SpeedBase=100.0 DeadZone=0.3") },
	{ TEXT("Engine.Input"), TEXT("JoyU"),			TEXT("Axis aMouseX SpeedBase=100.0 DeadZone=0.25 | Count bXAxis"),	TEXT("Axis aMouseX SpeedBase=100.0 DeadZone=0.3 | Count bXAxis") },
	{ TEXT("Engine.Input"), TEXT("JoyV"),			TEXT("Axis aMouseY SpeedBase=100.0 DeadZone=0.25 | Count bYAxis"),	TEXT("Axis aMouseY SpeedBase=100.0 DeadZone=0.3 | Count bYAxis") },
	// The wheel's commands exist nowhere.
	{ TEXT("Engine.Input"), TEXT("MouseWheelUp"),	TEXT("NextSkill"),								TEXT("NextWeapon") },
	{ TEXT("Engine.Input"), TEXT("MouseWheelDown"),	TEXT("PrevSkill"),								TEXT("NextWeapon") },
	// The pad's friction: Epic's PC values were tuned for a mouse that went through the stick's path.
	{ TEXT("UCGameplay.UCPlayerInput"), TEXT("aTurn_FrictionRange"),	TEXT("(Min=1.0,Max=0.75)"),		TEXT("(Min=1.0,Max=0.95)") },
	{ TEXT("UCGameplay.UCPlayerInput"), TEXT("aLookUp_FrictionRange"),	TEXT("(Min=1.0,Max=0.65)"),		TEXT("(Min=1.0,Max=0.9)") },
};

// M10 made the stick's section linear for the mouse; recognised by its limits, put back to the Xbox's as a whole.
static const FUC2IniMigration GUC2LinearPadSection[] =
{
	{ TEXT("UCGameplay.UCPlayerInput"), TEXT("bNonlinearAccel"),	TEXT("false"),		TEXT("true") },
	{ TEXT("UCGameplay.UCPlayerInput"), TEXT("aMouseX_Limit"),		TEXT("1000000"),	TEXT("100") },
	{ TEXT("UCGameplay.UCPlayerInput"), TEXT("aTurn_Limit"),		TEXT("10240000"),	TEXT("1024") },
	{ TEXT("UCGameplay.UCPlayerInput"), TEXT("aMouseY_Limit"),		TEXT("1000000"),	TEXT("100") },
	{ TEXT("UCGameplay.UCPlayerInput"), TEXT("aLookUp_Limit"),		TEXT("10240000"),	TEXT("1024") },
	{ TEXT("UCGameplay.UCPlayerInput"), TEXT("bAccelMultipliers"),	TEXT("false"),		TEXT("true") },
};

static FString UC2IniValue( const TCHAR* Section, const TCHAR* Key, UBOOL& bFound )
{
	FString Value;
	bFound = GConfig->GetString( Section, Key, Value, GUserIni );
	// Trailing blanks are not part of a binding (Epic's file has a few).
	while( Value.Len() && (Value.Right(1)==TEXT(" ") || Value.Right(1)==TEXT("\t")) )
		Value = Value.Left( Value.Len()-1 );
	return Value;
}

static void UC2ApplyMigration( const FUC2IniMigration& M )
{
	UBOOL bFound = 0;
	FString Value = UC2IniValue( M.Section, M.Key, bFound );
	if( bFound && appStricmp( *Value, M.Old )==0 )
	{
		GConfig->SetString( M.Section, M.Key, M.New, GUserIni );
		debugf( NAME_Init, TEXT("Input: User.ini [%s] %s=%s is a value from before D-6; now %s"), M.Section, M.Key, M.Old, M.New );
		// A class whose package is already loaded has read its config already (the static-link build loads
		// UCGameplay before the game engine starts): read it again, so the objects made later get the new value.
		UClass* Class = FindObject<UClass>( NULL, M.Section );
		if( Class )
			Class->GetDefaultObject()->LoadConfig();
	}
}

ENGINE_API void UC2MigrateUserIni()
{
	guard(UC2MigrateUserIni);
	for( INT i=0; i<ARRAY_COUNT(GUC2InputMigrations); i++ )
		UC2ApplyMigration( GUC2InputMigrations[i] );

	UBOOL bFoundX = 0, bFoundT = 0;
	FString LimitX = UC2IniValue( TEXT("UCGameplay.UCPlayerInput"), TEXT("aMouseX_Limit"), bFoundX );
	FString LimitT = UC2IniValue( TEXT("UCGameplay.UCPlayerInput"), TEXT("aTurn_Limit"), bFoundT );
	if( bFoundX && bFoundT && LimitX==TEXT("1000000") && LimitT==TEXT("10240000") )
		for( INT i=0; i<ARRAY_COUNT(GUC2LinearPadSection); i++ )
			UC2ApplyMigration( GUC2LinearPadSection[i] );

	// The D-6 bindings need the D-6 UCPlayerInput (rebuilt UCGameplay.u): the shipped one overwrites aTurn/aLookUp
	// from the stick, and the mouse would not turn the view. Say so once if that is the combination.
	UClass* PlayerInputClass = FindObject<UClass>( NULL, TEXT("UCGameplay.UCPlayerInput") );
	UBOOL bFound = 0;
	FString MouseX = UC2IniValue( TEXT("Engine.Input"), TEXT("MouseX"), bFound );
	if( PlayerInputClass && !FindField<UProperty>( PlayerInputClass, TEXT("MouseLookSensitivity") ) && appStrfind( *MouseX, TEXT("aTurn") ) )
		debugf( NAME_Warning, TEXT("Input: the mouse is bound to aTurn/aLookUp (D-6) but this UCGameplay.u has no D-6 mouse look (the shipped package?): the mouse will not turn the view; install the rebuilt UCGameplay.u (docs/porting/analysis/mouse.md)") );
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
