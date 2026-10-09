/*=============================================================================
	UC2Cheats.cpp: the position cheats TELEPORT and WHEREAMI, and the one guard
	every position/inspection command of the port goes through. uc2 port; not
	Epic's. docs/CHEAT-COMMANDS.md is the reference (names and format shared with
	the other engines of the frontend).

	Console commands (the console, a key binding, the SDL2 test channel's "exec ...",
	the web's ue_exec_command):
	  WHEREAMI                         the local player's position and view:
	                                   "<x> <y> <z> <yaw> <pitch>" (x y z: world units, one
	                                   decimal; yaw 0..65535, pitch -32768..32767: Unreal
	                                   rotation units), on the console, on the screen
	                                   (ClientMessage) and in the log as
	                                   "Cheat: WHEREAMI <x> <y> <z> <yaw> <pitch>"
	  TELEPORT <x> <y> <z> [<yaw> [<pitch>]]
	                                   puts the local player's pawn there (WHEREAMI's output
	                                   pasted back gives the same place and view); without yaw
	                                   and pitch the view is kept. TELEPORT with no arguments is
	                                   Epic's CheatManager.Teleport (to the surface looked at),
	                                   unchanged.

	The guard (UC2CheatsAllowed): cheats exist where UC2 creates a CheatManager, i.e. for
	the local player of a standalone game (PlayerController.AddCheats: NetMode ==
	NM_Standalone). A listen server, a network client and a dedicated server refuse:
	"Cheat: <COMMAND> refused: ..." in the log and on the console. Test harnesses that
	need these commands in network games (tools/dev/net-test.py) set
	[Engine.GameEngine] AllowNetCheats=True in that process's ini (-ini:System:...).
	Every command that runs logs one "Cheat: <COMMAND> <arguments>" line. The test
	commands NETPROBE, NETPLACE, NETPLACENEAR and NETAIM (UC2NetTest.cpp) use the same
	guard and log the same way.
=============================================================================*/

#include "EnginePrivate.h"

// The log carries no log-name prefix in this engine, so "Cheat: " is part of the text, on NAME_Log (never
// suppressed). The console (Ar) gets the short form, unless it is the log itself (the test channel's "exec").
static void CheatLog( FOutputDevice& Ar, const TCHAR* Text )
{
	debugf( NAME_Log, TEXT("Cheat: %s"), Text );
	if( &Ar != GLog )
		Ar.Log( Text );
}

static APlayerController* FirstLocalPlayer( UEngine* Engine )
{
	if( Engine && Engine->Client && Engine->Client->Viewports.Num() )
		return Engine->Client->Viewports(0)->Actor;
	return NULL;
}

//
// May cheats run in this process now? Logs the refusal. PC: the local player the command came
// from (NULL: the first viewport's, if any).
//
ENGINE_API UBOOL UC2CheatsAllowed( UEngine* Engine, APlayerController* PC, const TCHAR* Command, const TCHAR* Args, FOutputDevice& Ar )
{
	guard(UC2CheatsAllowed);
	if( !PC )
		PC = FirstLocalPlayer( Engine );
	UBOOL Allowed = PC && PC->CheatManager;	// UC2's own rule (PlayerController.AddCheats)
	const TCHAR* Why = TEXT("");
	if( !Allowed )
	{
		UBOOL NetCheats = 0;
		GConfig->GetBool( TEXT("Engine.GameEngine"), TEXT("AllowNetCheats"), NetCheats, GIni );
		if( NetCheats )
			Allowed = 1, Why = TEXT(" (AllowNetCheats: a test setting)");
	}
	if( !Allowed )
	{
		CheatLog( Ar, *FString::Printf( TEXT("%s refused: cheats are only for a standalone game (UC2's CheatManager rule)"), Command ) );
		return 0;
	}
	while( Args && *Args==' ' )
		Args++;
	debugf( NAME_Log, TEXT("Cheat: %s %s%s"), Command, Args ? Args : TEXT(""), Why );
	return 1;
	unguard;
}

//
// WHEREAMI and TELEPORT <x> <y> <z> [<yaw> [<pitch>]]. Called first by UPlayer::Exec (so that
// TELEPORT with coordinates is not taken by CheatManager.Teleport, which has no parameters)
// and by UGameEngine::Exec (the -nettest file, a process without a player).
//
ENGINE_API UBOOL UC2CheatExec( UEngine* Engine, APlayerController* PC, const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UC2CheatExec);
	const TCHAR* Str = Cmd;
	if( ParseCommand( &Str, TEXT("WHEREAMI") ) )
	{
		if( !PC )
			PC = FirstLocalPlayer( Engine );
		if( !PC || !PC->Pawn )
		{
			if( UC2CheatsAllowed( Engine, PC, TEXT("WHEREAMI"), TEXT(""), Ar ) )
				CheatLog( Ar, TEXT("WHEREAMI failed: no local player with a pawn") );
			return 1;
		}
		FVector L = PC->Pawn->Location;
		INT Yaw = PC->Rotation.Yaw & 65535;
		INT Pitch = PC->Rotation.Pitch & 65535;
		if( Pitch >= 32768 )
			Pitch -= 65536;
		FString Pos = FString::Printf( TEXT("%.1f %.1f %.1f %i %i"), L.X, L.Y, L.Z, Yaw, Pitch );
		if( !UC2CheatsAllowed( Engine, PC, TEXT("WHEREAMI"), *Pos, Ar ) )
			return 1;
		if( &Ar != GLog )
			Ar.Log( *Pos );
		PC->eventClientMessage( Pos, NAME_None );
		return 1;
	}
	if( ParseCommand( &Str, TEXT("TELEPORT") ) )
	{
		// TELEPORT alone: Epic's CheatManager.Teleport (UPlayer::Exec goes on to the script)
		FString Tok[5];
		INT N = 0;
		const TCHAR* Args = Str;
		while( N<5 && ParseToken( Str, Tok[N], 0 ) )
			N++;
		if( N==0 )
			return 0;
		if( !PC )
			PC = FirstLocalPlayer( Engine );
		if( !UC2CheatsAllowed( Engine, PC, TEXT("TELEPORT"), Args, Ar ) )
			return 1;
		if( N<3 )
		{
			CheatLog( Ar, TEXT("TELEPORT failed: usage TELEPORT <x> <y> <z> [<yaw> [<pitch>]]") );
			return 1;
		}
		if( !PC || !PC->Pawn || !PC->GetLevel() || PC->Level->NetMode==NM_Client )
		{
			CheatLog( Ar, TEXT("TELEPORT failed: no local pawn this process moves (a network client cannot)") );
			return 1;
		}
		FVector To( appAtof(*Tok[0]), appAtof(*Tok[1]), appAtof(*Tok[2]) );
		APawn* P = PC->Pawn;
		UBOOL Moved = PC->GetLevel()->FarMoveActor( P, To, 0, 0 );
		P->Velocity = FVector(0,0,0);
		P->Acceleration = FVector(0,0,0);
		if( N>=4 )
		{
			INT Yaw = appAtoi( *Tok[3] );
			INT Pitch = N>=5 ? appAtoi( *Tok[4] ) : 0;
			P->Rotation = FRotator( 0, Yaw, 0 );
			PC->Rotation = FRotator( Pitch, Yaw, 0 );
		}
		// UC2's ServerMove returns a pawn to the last ServerMove's spot (UnController.cpp, "epic moelfke"):
		// record the new one, as UC2NetTest.cpp's NETPLACE does
		if( !PC->LastServerMoveLocation.IsZero() )
		{
			PC->LastServerMoveLocation = P->Location;
			PC->LastServerMoveBase = P->Base;
			PC->PredictedVelocity = FVector(0,0,0);
		}
		FString Pos = FString::Printf( TEXT("%.1f %.1f %.1f %i %i"), P->Location.X, P->Location.Y, P->Location.Z,
			PC->Rotation.Yaw & 65535, (PC->Rotation.Pitch & 65535) >= 32768 ? (PC->Rotation.Pitch & 65535) - 65536 : (PC->Rotation.Pitch & 65535) );
		CheatLog( Ar, *FString::Printf( TEXT("TELEPORT %s: %s"), Moved ? TEXT("ok") : TEXT("failed (no room there)"), *Pos ) );
		return 1;
	}
	return 0;
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
