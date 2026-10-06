/*=============================================================================
	UC2NetTest.cpp: test-only console commands and command file for the network
	tests (tools/dev/net-test.sh). uc2 port; not Epic's. Quiet unless used.

	Console commands (also from the SDL2 test channel's "exec ..." lines):
	  NETPROBE <tag>                     "NetProbe: <tag> ..." lines: one summary line, one per
	                                     pawn and one per PlayerReplicationInfo in this process's level
	  NETPLACE <name> <x> <y> <z> <yaw>  server/listen server: move that player's pawn there (the
	                                     client follows through the engine's own position correction)
	  NETPLACENEAR <name> <other> <dist> [<first>] server: put that player's pawn <dist> units from the
	                                     other's, at the first of 16 headings with a clear path for
	                                     a pawn-sized box between them, facing the other
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

static void KeepServerMoveSpot( AController* C );

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
		KeepServerMoveSpot( C );
		debugf( NETPROBE, TEXT("place %s %s loc=(%.1f,%.1f,%.1f) yaw=%i"), *Name, Moved ? TEXT("ok") : TEXT("failed"),
			P->Location.X, P->Location.Y, P->Location.Z, Yaw );
		return;
	}
	debugf( NETPROBE, TEXT("place %s failed: no such player"), *Name );
}

// After a server-side move of a player's pawn: UC2's ServerMove first returns the pawn to where the last
// ServerMove left it (UnController.cpp:413, "epic moelfke"; also ULevel tick, UnLevTic.cpp:401), which would
// undo the move at once. Record the new spot there, as a move by the client would have.
static void KeepServerMoveSpot( AController* C )
{
	APlayerController* PC = Cast<APlayerController>( C );
	if( PC && C->Pawn && !PC->LastServerMoveLocation.IsZero() )
	{
		PC->LastServerMoveLocation = C->Pawn->Location;
		PC->LastServerMoveBase = C->Pawn->Base;
		PC->PredictedVelocity = FVector(0,0,0);
	}
}

static AController* FindPlayer( ULevel* Level, const TCHAR* Name )
{
	for( AController* C = Level->GetLevelInfo()->ControllerList; C; C = C->nextController )
		if( SameName(C->PlayerReplicationInfo, Name) )
			return C;
	return NULL;
}

static void PlaceNear( UGameEngine* Engine, const TCHAR* Cmd )
{
	FString Name, Other;
	ParseToken( Cmd, Name, 0 );
	ParseToken( Cmd, Other, 0 );
	FLOAT Dist = appAtof( *ParseToken(Cmd,0) );
	INT First = appAtoi( *ParseToken(Cmd,0) );	// optional: the first of the 16 headings to try
	ULevel* Level = Engine->GLevel;
	if( !Level || !Level->GetLevelInfo() || Level->GetLevelInfo()->NetMode==NM_Client )
	{
		debugf( NETPROBE, TEXT("placenear %s failed: no server level here"), *Name );
		return;
	}
	AController* C = FindPlayer( Level, *Name );
	AController* O = FindPlayer( Level, *Other );
	if( !C || !C->Pawn || !O || !O->Pawn )
	{
		debugf( NETPROBE, TEXT("placenear %s failed: both players need a pawn"), *Name );
		return;
	}
	APawn* P = C->Pawn;
	FVector From = O->Pawn->Location;
	FVector Extent( P->CollisionRadius, P->CollisionRadius, P->CollisionHeight );
	for( INT k=Clamp(First,0,16); k<16; k++ )
	{
		FVector Dir = FRotator( 0, k*4096, 0 ).Vector();
		FVector To = From + Dir * Dist;
		FCheckResult Hit( 1.f );
		Level->SingleLineCheck( Hit, NULL, To, From, TRACE_World, Extent );
		if( Hit.Time < 1.f )
			continue;
		// And ground below the spot (no ledge): the floor must be within 1.5 pawn heights.
		FCheckResult Floor( 1.f );
		Level->SingleLineCheck( Floor, NULL, To - FVector(0,0,P->CollisionHeight*1.5f), To, TRACE_World, Extent );
		if( Floor.Time >= 1.f )
			continue;
		if( !Level->FarMoveActor( P, To, 0, 0 ) )
			continue;
		FRotator R = (From - To).Rotation();
		R.Pitch = R.Roll = 0;
		P->Velocity = P->Acceleration = FVector(0,0,0);
		P->Rotation = R;
		C->Rotation = R;
		KeepServerMoveSpot( C );
		debugf( NETPROBE, TEXT("placenear %s ok loc=(%.1f,%.1f,%.1f) heading=%i index=%i"), *Name, P->Location.X, P->Location.Y, P->Location.Z, k*4096, k );
		return;
	}
	debugf( NETPROBE, TEXT("placenear %s failed: no clear spot at %.0f"), *Name, Dist );
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
	else if( ParseCommand(&Cmd,TEXT("NETPLACENEAR")) )
	{
		PlaceNear( Engine, Cmd );
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

/*-----------------------------------------------------------------------------
	NETDROP=<spec>[,<spec>...]: deterministic packet loss for the network tests
	(tools/dev/net-test.py handshake-loss). Read once from the command line; inert without it.
	  <TEXT>[#n][+k]    when this process SENDS a control message starting with TEXT for the n-th time
	                    on a connection (default 1), the packet carrying it is lost, and k more after it
	  <<TEXT>[#n][+k]   when this process RECEIVES it for the n-th time, its next k outgoing packets on
	                    that connection are lost (default k 1)
	Hooks: UNetConnection::Serialize (sent text), UControlChannel::ReceivedBunch (received text),
	UNetConnection::FlushNet (the drop, DO_ENABLE_NET_TEST like Epic's PktLoss/PktLag).
-----------------------------------------------------------------------------*/

struct FUC2DropSpec
{
	FString Text;
	UBOOL	bReceived;
	INT		Nth;
	INT		Extra;
};
#define UC2_MAX_DROP_SPECS 16
struct FUC2DropState
{
	INT		Seen[UC2_MAX_DROP_SPECS];	// per spec: occurrences on this connection (plain data: TMap copies values)
	INT		Armed;						// packets still to drop
	INT		Dropped;
};
static TArray<FUC2DropSpec>* GDropSpecs = NULL;
static TMap<UNetConnection*,FUC2DropState>* GDropState = NULL;

static UBOOL DropInit()
{
	static UBOOL Done = 0;
	if( !Done )
	{
		Done = 1;
		FString All;
		if( Parse(appCmdLine(), TEXT("NETDROP="), All) && All.Len() )
		{
			GDropSpecs = new TArray<FUC2DropSpec>;
			GDropState = new TMap<UNetConnection*,FUC2DropState>;
			while( All.Len() )
			{
				FString One;
				INT Comma = All.InStr( TEXT(",") );
				if( Comma>=0 ) { One = All.Left(Comma); All = All.Mid(Comma+1); }
				else { One = All; All = TEXT(""); }
				FUC2DropSpec S;
				S.bReceived = One.Left(1)==TEXT("<");
				if( S.bReceived )
					One = One.Mid(1);
				S.Nth = 1;
				S.Extra = S.bReceived ? 1 : 0;
				INT Plus = One.InStr( TEXT("+") );
				if( Plus>=0 ) { S.Extra = appAtoi( *One.Mid(Plus+1) ); One = One.Left(Plus); }
				INT Hash = One.InStr( TEXT("#") );
				if( Hash>=0 ) { S.Nth = appAtoi( *One.Mid(Hash+1) ); One = One.Left(Hash); }
				S.Text = One;
				if( GDropSpecs->Num() < UC2_MAX_DROP_SPECS )
					new(*GDropSpecs) FUC2DropSpec( S );	// placement copy: AddItem assigns into raw memory
				debugf( NETPROBE, TEXT("netdrop: %s %s #%i +%i"), S.bReceived ? TEXT("on receiving") : TEXT("on sending"), *S.Text, S.Nth, S.Extra );
			}
		}
	}
	return GDropSpecs!=NULL;
}

ENGINE_API void UC2NetTestNoteText( UNetConnection* Connection, const TCHAR* Text, UBOOL bSending )
{
	if( !DropInit() || !Connection )
		return;
	FUC2DropState* St = GDropState->Find( Connection );
	if( !St )
	{
		FUC2DropState New;
		appMemzero( &New, sizeof(New) );
		St = &GDropState->Set( Connection, New );
	}
	for( INT i=0; i<GDropSpecs->Num(); i++ )
	{
		FUC2DropSpec& S = (*GDropSpecs)(i);
		if( S.bReceived==bSending || appStrnicmp(Text, *S.Text, S.Text.Len())!=0 )
			continue;
		if( ++St->Seen[i]==S.Nth )
		{
			// Sent text: its own packet (the next flush) plus Extra; received text: the next Extra packets.
			St->Armed += bSending ? 1 + S.Extra : S.Extra;
			debugf( NETPROBE, TEXT("netdrop: %s '%s' #%i on %s: dropping the next %i outgoing packet(s)"),
				bSending ? TEXT("sending") : TEXT("received"), *S.Text, S.Nth, *Connection->LowLevelGetRemoteAddress(), St->Armed );
		}
	}
}

ENGINE_API UBOOL UC2NetTestDropPacket( UNetConnection* Connection )
{
	if( !GDropSpecs || !Connection )
		return 0;
	FUC2DropState* St = GDropState->Find( Connection );
	if( !St || St->Armed<=0 )
		return 0;
	St->Armed--;
	St->Dropped++;
	debugf( NETPROBE, TEXT("netdrop: lost packet %i (%i bytes) to %s"), Connection->OutPacketId, Connection->Out.GetNumBytes(), *Connection->LowLevelGetRemoteAddress() );
	return 1;
}
