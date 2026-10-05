/*=============================================================================
	UC2NetTest.cpp: test-only console commands and command file for the network
	tests (tools/dev/net-test.sh). uc2 port; not Epic's. Quiet unless used.

	Console commands (also from the SDL2 test channel's "exec ..." lines):
	  NETPROBE <tag>                     "NetProbe: <tag> ..." lines: one summary line, one per
	                                     pawn and one per PlayerReplicationInfo in this process's level
	  NETPLACE <name> <x> <y> <z> <yaw>  server/listen server: move that player's pawn there (the
	                                     client follows through the engine's own position correction)
	  NETAIM <name>                      a player's own process: turn the local player controller
	                                     to face that player's pawn as this process sees it

	Command file: -nettest=<file> (any process; the dedicated server has no other input).
	A harness appends lines; each tick runs every complete new line:
	  exec <console command>             through UGameEngine::Exec (e.g. "exec netprobe S1",
	                                     "exec servertravel DM-Recoil")
	  log <text>                         a marker line
=============================================================================*/

#include "EnginePrivate.h"
#include "UnNet.h"

static FName NetProbeLogName()
{
	// RF_Native keeps a run-time name out of the purge of unreferenced names (UnObj.cpp
	// RemoveNames), as for SDL2Drv's SDL2Input.
	static FName Name;
	if( Name==NAME_None )
	{
		Name = FName( TEXT("NetProbe") );
		Name.SetFlags( RF_Native );
	}
	return Name;
}
#define NETPROBE ((EName)NetProbeLogName().GetIndex())

static const TCHAR* NetModeName( BYTE Mode )
{
	switch( Mode )
	{
		case NM_Standalone:			return TEXT("Standalone");
		case NM_DedicatedServer:	return TEXT("DedicatedServer");
		case NM_ListenServer:		return TEXT("ListenServer");
		case NM_Client:				return TEXT("Client");
	}
	return TEXT("Unknown");
}

// A player name as one token: spaces become '_' (GameInfo.Login turns '_' into ' ').
static FString Token( const FString& S )
{
	FString R = S.Len() ? S : FString(TEXT("None"));
	for( INT i=0; i<R.Len(); i++ )
		if( (*R)[i]==' ' )
			((TCHAR*)*R)[i] = '_';
	return R;
}

static UBOOL SameName( APlayerReplicationInfo* PRI, const TCHAR* Name )
{
	return PRI && appStricmp( *Token(PRI->PlayerName), Name )==0;
}

static APlayerController* LocalPlayer( UGameEngine* Engine )
{
	if( Engine->Client && Engine->Client->Viewports.Num() )
		return Engine->Client->Viewports(0)->Actor;
	return NULL;
}

static void Probe( UGameEngine* Engine, const TCHAR* Tag )
{
	ULevel* Level = Engine->GLevel;
	if( !Level || !Level->GetLevelInfo() )
	{
		debugf( NETPROBE, TEXT("%s level=None"), Tag );
		return;
	}
	ALevelInfo* Info = Level->GetLevelInfo();
	APlayerController* PC = LocalPlayer( Engine );
	if( PC && PC->GetLevel()!=Level )
		PC = NULL;
	INT Conns = 0;
	if( Level->NetDriver )
		Conns = Level->NetDriver->ServerConnection ? 1 : Level->NetDriver->ClientConnections.Num();
	INT NumPawns=0, NumPRIs=0;
	for( INT i=0; i<Level->Actors.Num(); i++ )
	{
		AActor* A = Level->Actors(i);
		if( A && !A->bDeleteMe && A->IsA(APawn::StaticClass()) )
			NumPawns++;
		if( A && !A->bDeleteMe && A->IsA(APlayerReplicationInfo::StaticClass()) )
			NumPRIs++;
	}
	debugf( NETPROBE, TEXT("%s summary net=%s level=%s time=%.1f conns=%i local=%s localpawn=%i gri=%i pawns=%i pris=%i"),
		Tag, NetModeName(Info->NetMode), Level->GetOuter()->GetName(), Info->TimeSeconds, Conns,
		PC && PC->PlayerReplicationInfo ? *Token(PC->PlayerReplicationInfo->PlayerName) : TEXT("None"),
		PC && PC->Pawn ? 1 : 0, Info->GRI ? 1 : 0, NumPawns, NumPRIs );
	for( INT i=0; i<Level->Actors.Num(); i++ )
	{
		AActor* A = Level->Actors(i);
		if( !A || A->bDeleteMe )
			continue;
		if( A->IsA(APawn::StaticClass()) )
		{
			APawn* P = (APawn*)A;
			AWeapon* W = P->Weapon;
			debugf( NETPROBE, TEXT("%s pawn name=%s loc=(%.1f,%.1f,%.1f) health=%i local=%i role=%i class=%s weapon=%s shots=%i"),
				Tag, P->PlayerReplicationInfo ? *Token(P->PlayerReplicationInfo->PlayerName) : TEXT("None"),
				P->Location.X, P->Location.Y, P->Location.Z, P->Health,
				PC && PC->Pawn==P ? 1 : 0, (INT)P->Role, P->GetClass()->GetName(),
				W ? W->GetClass()->GetName() : TEXT("None"), W ? (INT)W->FlashCount : 0 );
		}
		else if( A->IsA(APlayerReplicationInfo::StaticClass()) )
		{
			APlayerReplicationInfo* R = (APlayerReplicationInfo*)A;
			debugf( NETPROBE, TEXT("%s pri name=%s id=%i score=%.0f deaths=%.0f kills=%i bot=%i spectator=%i pawnclass=%s energy=%s explosive=%s"),
				Tag, *Token(R->PlayerName), R->PlayerID, R->Score, R->Deaths, R->Kills, (INT)R->bBot,
				(INT)R->bIsSpectator, R->PawnClass ? R->PawnClass->GetName() : TEXT("None"),
				*Token(R->EnergyWeaponClassName), *Token(R->ExplosiveWeaponClassName) );
		}
	}
}

static void Place( UGameEngine* Engine, const TCHAR* Cmd )
{
	FString Name;
	ParseToken( Cmd, Name, 0 );
	FVector L;
	L.X = appAtof( *ParseToken(Cmd,0) );
	L.Y = appAtof( *ParseToken(Cmd,0) );
	L.Z = appAtof( *ParseToken(Cmd,0) );
	INT Yaw = appAtoi( *ParseToken(Cmd,0) );
	ULevel* Level = Engine->GLevel;
	if( !Level || !Level->GetLevelInfo() || Level->GetLevelInfo()->NetMode==NM_Client )
	{
		debugf( NETPROBE, TEXT("place %s failed: no server level here"), *Name );
		return;
	}
	for( AController* C = Level->GetLevelInfo()->ControllerList; C; C = C->nextController )
	{
		if( !SameName(C->PlayerReplicationInfo, *Name) )
			continue;
		if( !C->Pawn )
		{
			debugf( NETPROBE, TEXT("place %s failed: no pawn"), *Name );
			return;
		}
		APawn* P = C->Pawn;
		FRotator R( 0, Yaw, 0 );
		UBOOL Moved = Level->FarMoveActor( P, L, 0, 0 );
		P->Velocity = FVector(0,0,0);
		P->Acceleration = FVector(0,0,0);
		P->Rotation = R;
		C->Rotation = R;
		debugf( NETPROBE, TEXT("place %s %s loc=(%.1f,%.1f,%.1f) yaw=%i"), *Name, Moved ? TEXT("ok") : TEXT("failed"),
			P->Location.X, P->Location.Y, P->Location.Z, Yaw );
		return;
	}
	debugf( NETPROBE, TEXT("place %s failed: no such player"), *Name );
}

static void Aim( UGameEngine* Engine, const TCHAR* Cmd )
{
	FString Name;
	ParseToken( Cmd, Name, 0 );
	APlayerController* PC = LocalPlayer( Engine );
	if( !PC || !PC->Pawn )
	{
		debugf( NETPROBE, TEXT("aim %s failed: no local pawn"), *Name );
		return;
	}
	ULevel* Level = PC->GetLevel();
	for( INT i=0; i<Level->Actors.Num(); i++ )
	{
		APawn* T = Cast<APawn>( Level->Actors(i) );
		if( !T || T->bDeleteMe || T==PC->Pawn || !SameName(T->PlayerReplicationInfo, *Name) )
			continue;
		FVector Eye = PC->Pawn->Location + FVector( 0, 0, PC->Pawn->BaseEyeHeight );
		FRotator R = (T->Location - Eye).Rotation();
		R.Roll = 0;
		PC->Rotation = R;
		debugf( NETPROBE, TEXT("aim %s ok rot=(%i,%i,%i) dist=%.1f"), *Name, R.Pitch & 65535, R.Yaw & 65535, R.Roll,
			(T->Location - Eye).Size() );
		return;
	}
	debugf( NETPROBE, TEXT("aim %s failed: no such pawn here"), *Name );
}

ENGINE_API UBOOL UC2NetTestExec( UGameEngine* Engine, const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UC2NetTestExec);
	if( ParseCommand(&Cmd,TEXT("NETPROBE")) )
	{
		while( *Cmd==' ' )
			Cmd++;
		Probe( Engine, *Cmd ? Cmd : TEXT("-") );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("NETPLACE")) )
	{
		Place( Engine, Cmd );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("NETAIM")) )
	{
		Aim( Engine, Cmd );
		return 1;
	}
	return 0;
	unguard;
}

/*-----------------------------------------------------------------------------
	The -nettest=<file> command file.
-----------------------------------------------------------------------------*/

struct FUC2NetTestFile
{
	FILE*		File;
	ANSICHAR	Partial[1024];
	INT			PartialLen;
};

ENGINE_API void UC2NetTestTick( UGameEngine* Engine )
{
	guard(UC2NetTestTick);
	static UBOOL Checked = 0;
	static FUC2NetTestFile T;
	if( !Checked )
	{
		Checked = 1;
		T.File = NULL;
		T.PartialLen = 0;
		FString Path;
		if( Parse(appCmdLine(), TEXT("NETTEST="), Path) )
		{
			T.File = fopen( TCHAR_TO_ANSI(*Path), "r" );
			debugf( NETPROBE, TEXT("net test file %s: %s"), *Path, T.File ? TEXT("open") : TEXT("cannot open") );
		}
	}
	if( !T.File )
		return;
	for( ;; )
	{
		int C = fgetc( T.File );
		if( C==EOF )
		{
			// Lines are appended while we read (see SDL2InputTest.cpp for why the seek).
			clearerr( T.File );
			fseek( T.File, 0, SEEK_CUR );
			return;
		}
		if( C=='\r' )
			continue;
		if( C!='\n' )
		{
			if( T.PartialLen < (INT)sizeof(T.Partial)-1 )
				T.Partial[T.PartialLen++] = (ANSICHAR)C;
			continue;
		}
		T.Partial[T.PartialLen] = 0;
		T.PartialLen = 0;
		if( !T.Partial[0] || T.Partial[0]=='#' )
			continue;
		const TCHAR* Line = appFromAnsi( T.Partial );
		debugf( NETPROBE, TEXT("test: %s"), Line );
		const TCHAR* Cmd = Line;
		if( ParseCommand(&Cmd,TEXT("EXEC")) )
		{
			// A command may travel to another map: run at most one per tick.
			Engine->Exec( Cmd, *GLog );
			return;
		}
		else if( !ParseCommand(&Cmd,TEXT("LOG")) )
			debugf( NETPROBE, TEXT("test: unknown command") );
	}
	unguard;
}
