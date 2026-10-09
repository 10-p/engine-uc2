/*=============================================================================
	UnActor.cpp: Actor list functions.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "EnginePrivate.h"

IMPLEMENT_CLASS(UPlayer);

/*-----------------------------------------------------------------------------
	UPlayer object implementation.
-----------------------------------------------------------------------------*/

UPlayer::UPlayer()
{}
void UPlayer::Serialize( FArchive& Ar )
{
	guard(UPlayer::Serialize);
	Super::Serialize( Ar );
	unguard;
}
void UPlayer::Destroy()
{
	guard(UPlayer::Destroy);
	APlayerController *oldActor = Actor;
	Actor = NULL;
	if( GIsRunning && oldActor )
	{
		ULevel* Level = oldActor->GetLevel();
		oldActor->Player = NULL;
		if( oldActor->RendMap != REN_Prefab && oldActor->RendMap != REN_PrefabCompiled )		// !!hack - I don't know why this crashes when it's a REN_Prefab
			Level->DestroyActor( oldActor, 1 );
	}
	Console = NULL;
	Super::Destroy();
	unguard;
}
// uc2 port: TELEPORT <x> <y> <z> / WHEREAMI, cheats (UC2Cheats.cpp)
ENGINE_API UBOOL UC2CheatExec( UEngine* Engine, APlayerController* PC, const TCHAR* Cmd, FOutputDevice& Ar );

UBOOL UPlayer::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UPlayer::Exec);
	if ( !Actor )
		return 0;
	// uc2 port: before the scripts, so that TELEPORT with coordinates is ours (CheatManager.Teleport has none)
	if( UC2CheatExec( Actor->GetLevel()->Engine, Actor, Cmd, Ar ) )
		return 1;
	if( Actor->GetLevel()->Exec(Cmd,Ar) )
	{
		return 1;
	}
	if( Actor->Level && Actor->Level->Game && Actor->Level->Game->ScriptConsoleExec(Cmd,Ar,Actor->Pawn) )
	{
		return 1;
	}
	if( Actor->myHUD && Actor->myHUD->ScriptConsoleExec(Cmd,Ar,Actor->Pawn) )
	{
		return 1;
	}
	if( Actor->CheatManager && Actor->CheatManager->ScriptConsoleExec(Cmd,Ar,Actor->Pawn) )
	{
		return 1;
	}
	if( Actor->ScriptConsoleExec(Cmd,Ar,Actor->Pawn) )
	{
		return 1;
	}
	if( Actor->PlayerInput && Actor->PlayerInput->ScriptConsoleExec(Cmd,Ar,Actor->Pawn) )
	{
		return 1;
	}
	// rjp -- allow the GUIController to process exec functions
	if ( GUIController && GUIController->ScriptConsoleExec(Cmd,Ar,Actor->Pawn) )
	{
		return 1;
	}
	if( Actor->Pawn )
	{
		if( Actor->Pawn->ScriptConsoleExec(Cmd,Ar,Actor->Pawn) )
			return 1;
		if( Actor->Pawn->Weapon && Actor->Pawn->Weapon->ScriptConsoleExec(Cmd,Ar,Actor->Pawn) )
			return 1;
	}
	if( Actor->GetLevel()->Engine->Exec(Cmd,Ar) )
	{
		return 1;
	}
	else return 0;
	unguard;
}

/* scion ======================================================================
 * UPlayer::execGetAnalogButtonAmount
 * Author: jg
 *
 * Updates the motor speeds for the gamepad
 *
 * input:	flDelta FLOAT The amount of time that has elapsed since the last
 *			update
 *
 * ============================================================================
 */
void UPlayer::execGetAnalogButtonAmount(FFrame& Stack,RESULT_DECL)
{
	guardSlow(AActor::execTrace);
	P_GET_INT(ButtonIndex);
	P_FINISH;
	// Call the virtual for figuring this out so that the UXboxViewport can
	// overload it with a good version
	*(BYTE*)Result = GetAnalogButtonAmount(ButtonIndex);
	unguardSlow;
}
IMPLEMENT_FUNCTION(UPlayer,INDEX_NONE,execGetAnalogButtonAmount);


void UPlayer::execGetGamepadID( FFrame& Stack, RESULT_DECL )
{
	guard(UPlayer::execGetGamepadID);

	P_FINISH;

	*(BYTE*)Result = GetGamepadID();
	unguardexec;
}
IMPLEMENT_FUNCTION(UPlayer,INDEX_NONE,execGetGamepadID);
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

