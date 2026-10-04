/*=============================================================================
	UnController.cpp: AI implementation

  This contains both C++ methods (movement and reachability), as well as some
  AI related natives

	Copyright 2000-2002 Epic MegaGames, Inc. This software is a trade secret.

	Revision history:
		* Created by Steven Polge 4/00
=============================================================================*/
#include "EnginePrivate.h"
#include "UnNet.h"
#include "FConfigCacheIni.h"
#include "UnPath.h"
#include "ULoadSaveManager.h"

#ifdef _XBOX
#include "UnTcpNetDriverXbox.h"
#include "UnUIXInteraction.h"
#include "XboxLive.h"
#endif

IMPLEMENT_CLASS(UCheatManager);
IMPLEMENT_CLASS(UPlayerInput);
IMPLEMENT_CLASS(APlayerController);

enum EAIFunctions
{
	AI_PollMoveTo = 501,
	AI_PollMoveToward = 503,
	AI_PollStrafeTo = 505,
	AI_PollStrafeFacing = 507,
	AI_PollFinishRotation = 509,
	AI_PollWaitForLanding = 528,
	AI_PollWaitToSeeEnemy = 511
};

UBOOL AController::LocalPlayerController()
{
	guard(AController::LocalPlayerController);
	return false;
	unguard;
}

UBOOL APlayerController::LocalPlayerController()
{
	guard(APlayerController::LocalPlayerController);

	return ( Player && Player->IsA(UViewport::StaticClass()) );
	unguard;
}

UBOOL AController::WantsLedgeCheck()
{
	guard(AController::WantsLedgeCheck);

	if ( !Pawn || !Pawn->bCheckLedges )
		return false;
	if ( Pawn->bCanJump )
	{
		// check if moving toward a navigation point, and not messed with
		if ( MoveTarget && (GetStateFrame()->LatentAction == AI_PollMoveToward) )
		{
			// check if still on path
			// epic moelfke: rewrote check so it can actually succeed
			if (CurrentPath && CurrentPath->End == MoveTarget)
			{
				FVector PawnDir = Pawn->Location - CurrentPath->Start->Location;
				PawnDir.Z = 0.f;
				const FLOAT PawnDist = PawnDir.SizeSquared();
				// if the pawn's cylinder is touching the starting point, then it is on the path
				if (PawnDist < Pawn->CollisionRadius * Pawn->CollisionRadius)
				{
					//debugf(TEXT("%s skip ledge check because at path start"), Pawn->GetName());
					return false;
				}
				else
				{
					const FLOAT PathDist = CurrentPath->Distance * CurrentPath->Distance;
					// if the pawn is between the two points on the path
					if (PawnDist < PathDist && (FVector(Pawn->Location.X, Pawn->Location.Y, CurrentPath->End->Location.Z) - CurrentPath->End->Location).SizeSquared() < PathDist)
					{
						// find the distance from the pawn to the line segment of the path by solving a right triangle,
						// where the segment from the path start to the pawn location is the hypotenuse and the segment from the
						// pawn to the path forms a right angle with the path
						const DOUBLE Angle = appAcos(FVector(CurrentPathDir.X, CurrentPathDir.Y, 0.f) | PawnDir.SafeNormal());
						const FLOAT Dist = appSin(Angle) * PawnDir.Size();
						// if the distance is less than the size of the pawn's cylinder, the pawn is on the path
						if (Dist < Pawn->CollisionRadius)
						{
							//debugf(TEXT("%s skip ledge check because on path"), Pawn->GetName());
							return false;
						}
					}
				}
			}

			// check if could reach by jumping
			if ( MoveTarget->Physics != PHYS_Falling || MoveTarget->Velocity.Z <= 0.f ) // epic moelfke: also allow if MoveTarget is falling downwards
			{
				const FVector& NeededJump = Pawn->SuggestFallVelocity(MoveTarget->Location, Pawn->Location, Pawn->GroundSpeed, 0.f, 2 * Pawn->JumpZ, Pawn->GroundSpeed);
				// epic moelfke: if pawn can double jump, give a little more leeway
				if ( NeededJump.Z < (Pawn->bCanDoubleJump ? Pawn->JumpZ * 1.5f : Pawn->JumpZ) )
				{
					//debugf(TEXT("%s skip ledge check because jump reachable"), Pawn->GetName());
					return false;
				}
			}
		}
	}
	//debugf(TEXT("%s do ledge check"), Pawn->GetName());
	return ( !Pawn->bCanFly );
	unguard;
}

UBOOL APlayerController::WantsLedgeCheck()
{
	guard(APlayerController::WantsLedgeCheck);

	return false;
//	return ( Pawn && (Pawn->bIsCrouched || Pawn->bIsWalking) );
	unguard;
}

UBOOL APlayerController::StopAtLedge()
{
	guard(APlayerController::StopAtLedge);

	return false;
	unguard;
}

UBOOL AController::StopAtLedge()
{
	guard(AController::StopAtLedge);

	if ( !Pawn->bCanJump || Pawn->bStopAtLedges )
	{
		MoveTimer = -1.f;
		return true;
	}
	return false;
	unguard;
}
//-------------------------------------------------------------------------------------------------
/*
Node Evaluation functions, used with APawn::BreadthPathTo()
*/

// declare type for node evaluation functions
typedef FLOAT ( *NodeEvaluator ) (ANavigationPoint*, APawn*, FLOAT);

FLOAT FindBestInventory( ANavigationPoint* CurrentNode, APawn* seeker, FLOAT bestWeight )
{
	FLOAT CacheWeight = 0;
	if ( CurrentNode->InventoryCache && (CurrentNode->visitedWeight < (8.f - CurrentNode->InventoryCache->TimerCounter) * seeker->GroundSpeed) )
	{
		FLOAT CacheDist = ::Max(1.f,((CurrentNode->Location - CurrentNode->InventoryCache->Location).Size()) + CurrentNode->visitedWeight);
		if ( CurrentNode->InventoryCache->bDeleteMe )
			CurrentNode->InventoryCache = NULL;
		else if ( CurrentNode->InventoryCache->MaxDesireability/CacheDist > bestWeight )
			CacheWeight = seeker->Controller->eventDesireability(CurrentNode->InventoryCache)/CacheDist;
		bestWeight = ::Max(bestWeight,CacheWeight);
	}
	
	if ( !CurrentNode->GetAInventorySpot() || !seeker->Controller )
		return CacheWeight;

	APickup* item = ((AInventorySpot *)CurrentNode)->markedItem;
	FLOAT AdjustedWeight = ::Max(1,CurrentNode->visitedWeight);
	if ( item && !item->bDeleteMe && (item->IsProbing(NAME_Touch) || (item->bPredictRespawns && (item->LatentFloat < seeker->Controller->RespawnPredictionTime)))
			&& (item->MaxDesireability/AdjustedWeight > bestWeight) )
	{
		if ( !item->IsProbing(NAME_Touch) )
			AdjustedWeight += seeker->GroundSpeed * item->LatentFloat;
		return ::Max(CacheWeight,seeker->Controller->eventDesireability(item)/AdjustedWeight);
	}
	return CacheWeight;
}

FLOAT FindRandomPath( ANavigationPoint* CurrentNode, APawn* seeker, FLOAT bestWeight )
{
	//epic superville: If this node is claimed already, don't select it (or if can't teleport to it)
	if( !CurrentNode->bAcceptRaidenTeleport || 
		 CurrentNode->ManagerFlags & UCONST_PATHMANAGER_CLAIMED )
	{
		return -1.f;
	}

	if ( CurrentNode->bEndPoint )
		return (1000.f + appFrand());
	return appFrand();
}

/* epic ===============================================
* ::FindBestMarkedInventory
* Author: moelfke
*
* Calls FindBestInventory() node evaluation function
* only on nodes marked beforehand by setting bTransientEndPoint
* to true on them.
* Specifically used by execFindBestPickupInRangeOf()
*
* =====================================================
*/
FLOAT FindBestMarkedInventory(ANavigationPoint* CurrentNode, APawn* seeker, FLOAT bestWeight)
{
	//if this node has been marked, evaluate its inventory using the standard function
	if (CurrentNode->bEndPoint)
	{
		return FindBestInventory(CurrentNode, seeker, bestWeight);
	}
	else
	{
		return 0.f; //otherwise, we don't care
	}
}

/* epic ===============================================
* ::FindBestSuperPickup
* Author: moelfke
*
* Returns 1.f (thereby successfully bailing out of
* pathfinding) if the given node marks a superpickup
* that is desireable to seeker.
* Specifically used by execFindBestSuperPickupPath()
*
* =====================================================
*/
FLOAT FindBestSuperPickup(ANavigationPoint* CurrentNode, APawn* seeker, FLOAT bestWeight)
{
	AInventorySpot* IS = CurrentNode->GetAInventorySpot();
	if ( IS && IS->markedItem && IS->markedItem->bSuperPickup && seeker->Controller &&
			(IS->markedItem->IsProbing(NAME_Touch) || IS->markedItem->LatentFloat < seeker->Controller->RespawnPredictionTime) &&
			 seeker->Controller->eventDesireability(IS->markedItem) > 0.f )
	{
		return 1.f;
	}
	else
	{
		return 0.f;
	}
}

//----------------------------------------------------------------------------------
//
// Set the player.
//
void APlayerController::SetPlayer( UPlayer* InPlayer )
{
	guard(APlayerController::SetPlayer);
	check(InPlayer!=NULL);

	debugf(TEXT("%s got player %s"),GetName(), InPlayer->GetName());

	// Detach old player.
	if( InPlayer->Actor )
	{
		InPlayer->Actor->Player = NULL;

		//epic moelfke: if the new Actor is in the same level as the old,
		//set its to the location of the old one so the player isn't disoriented by the switch
		if (GetLevel() == InPlayer->Actor->GetLevel())
		{
			GetLevel()->FarMoveActor(this, InPlayer->Actor->Location);
			FCheckResult Hit;
			GetLevel()->MoveActor(this, FVector(0,0,0), InPlayer->Actor->Rotation, Hit);
			// destroy the old Actor
			GetLevel()->DestroyActor(InPlayer->Actor);
		}
	}

	// Set the viewport.
	Player = InPlayer;
	InPlayer->Actor = this;

	UNetDriver* Driver = GetLevel()->NetDriver;
	if( (ClientCap>1800) && Driver && Driver->ServerConnection )
		Player->CurrentNetSpeed = Driver->ServerConnection->CurrentNetSpeed = Clamp( ClientCap, 1800, Driver->MaxClientRate );

	// initialize the input system only if local player
	UViewport* Viewport = Cast<UViewport>(InPlayer);
	if ( Viewport ) 
	{
		eventInitInputSystem();

		//scion superville: Notify local player controller of new viewport
		eventReceivedViewport();
	}

	unguard;
}

// scion sz -
void APlayerController::Destroy()
{
	guard(APlayerController::Destroy);

	//epic moelfke: stats
	// only save to profile if playing a standalone match
	if ( Level && Level->GetOuter()->GetFName() != FName(TEXT("Entry")) &&
		Level->GetOuter()->GetFName() != FName(TEXT("Menu")) )
	{
		UViewport* Viewport = Cast<UViewport>(Player);
		if ( Viewport )
		{
			if ( Level->NetMode == NM_Standalone )
			{
				if (Viewport->Profile)
				{
					if (Stats)
					{
						eventPrepareStatWrite();
						Viewport->Profile->MergeStatData(Stats,false);
					}

					if (!Viewport->Profile->SaveProfile())
					{
						debugf(TEXT("Failed to save PlayerStats config data for profile '%s'"), *Viewport->Profile->PlayerName);
					}
				}
			}
			else if ( Level->NetMode == NM_Client || Level->NetMode == NM_ListenServer )
			{
				FString Options;
				if ( GetPlayerURLOption(TEXT("MenuOptions"), Options) &&
					Options.InStr(TEXT("disconnect")) )
				{
					eventCheckDisconnectFlag(1);
				}
			}
		}
	}

	if ( Stats != NULL )
		delete Stats;

	Stats = NULL;
	Super::Destroy();

	unguard;
}

void APlayerController::PostScriptDestroyed()
{
	guard(APlayerController::PostScriptDestroyed);

	// cheatmanager, adminmanager, and playerinput cleaned up in C++ PostScriptDestroyed()
	if ( PlayerInput )
		delete PlayerInput;
	PlayerInput = NULL;
	if ( CheatManager )
		delete CheatManager;
	CheatManager = NULL;

	if ( WaveformManager )
		delete WaveformManager;

	WaveformManager = NULL;

	Super::PostScriptDestroyed();
	unguard;
}




void APlayerController::execServerMove(FFrame& Stack, RESULT_DECL)
{
	guard(APlayerController::execServerMove);

	P_GET_FLOAT(TimeStamp);
	P_GET_VECTOR(InAccel);
	P_GET_VECTOR(ClientLoc);
	P_GET_UBOOL(NewbJumpStatus);
	P_GET_UBOOL(NewbDoubleJump);
	P_GET_BYTE(DoubleClickMove);
	P_GET_BYTE(ClientRoll);
	P_GET_INT(View);
	P_GET_INT(CompressedAimRotation);
	P_GET_BYTE_OPTX(OldTimeDelta,0);
	P_GET_INT_OPTX(OldAccel,0);
	P_GET_BYTE_OPTX(XAxis,0);
	P_GET_BYTE_OPTX(YAxis,0);
	P_FINISH;

	INT			maxPitch		= 0, 
				ViewPitch		= 0, 
				ViewYaw			= 0;
    UBOOL		NewbPressedJump = 0, 
				OldbDoubleJump	= 0;
	FLOAT		DeltaTime		= 0.f,
				ClientErr		= 0.f,
				OldTimeStamp	= 0.f;
	FRotator	DeltaRot(0,0,0), 
				Rot(0,0,0), 
				ViewRot(0,0,0), 
				AimRot(0,0,0);
	FVector		Accel(0.f,0.f,0.f), 
				LocDiff(0.f,0.f,0.f);

	EDoubleClickDir OldDoubleClickMove = DCLICK_None;
	ALevelInfo*		LevelInfo = GetLevel()->GetLevelInfo();

    // If this move is outdated, discard it.
    if( CurrentTimeStamp >= TimeStamp )
	{
        return;
	}

	//epic moelfke: return pawn to position from last servermove (reversing any physics that happened between updates)
	if( Pawn && !LastServerMoveLocation.IsZero() && (!LastServerMoveBase || LastServerMoveBase->bStatic) )
	{
		FCheckResult Hit(1.0f);
		GetLevel()->MoveActor( Pawn, LastServerMoveLocation - Pawn->Location, Pawn->Rotation, Hit );
	}

	// if client hasn't acknowledged its pawn yet, send it again
	if( AcknowledgedPawn != Pawn )
	{
		InAccel = FVector(0.f,0.f,0.f);
		eventGivePawn( Pawn );
	}
	// if OldTimeDelta corresponds to a lost packet, process it first
	else if( OldTimeDelta != 0 )
	{
		OldTimeStamp = TimeStamp - FLOAT(OldTimeDelta) / 500.f - 0.001f;
		if( CurrentTimeStamp < (OldTimeStamp - 0.001f) )
		{
			// split out components of lost move (approx)
			Accel.X = ((DWORD)OldAccel) >> 23;
			if( Accel.X > 127 )
			{
				Accel.X = -1 * (Accel.X - 128);
			}
			Accel.Y = (((DWORD)OldAccel) >> 15) & 255;
			if( Accel.Y > 127 )
			{
				Accel.Y = -1 * (Accel.Y - 128);
			}
			Accel.Z = (((DWORD)OldAccel) >> 7) & 255;
			if( Accel.Z > 127 )
			{
				Accel.Z = -1 * (Accel.Z - 128);
			}
			Accel *= 20;

			OldbDoubleJump	= ((OldAccel & 32) != 0);
			NewbPressedJump = ((OldAccel & 16) != 0);
			if( NewbPressedJump )
			{
				bJumpStatus = NewbJumpStatus;
			}
			switch( OldAccel & 7 )
			{
				case 0:
					OldDoubleClickMove = DCLICK_None;
					break;
				case 1:
					OldDoubleClickMove = DCLICK_Left;
					break;
				case 2:
					OldDoubleClickMove = DCLICK_Right;
					break;
				case 3:
					OldDoubleClickMove = DCLICK_Forward;
					break;
				case 4:
					OldDoubleClickMove = DCLICK_Back;
					break;
			}
			//log("Recovered move from "$OldTimeStamp$" acceleration "$Accel$" from "$OldAccel);
			OldTimeStamp = Min<FLOAT>(OldTimeStamp, CurrentTimeStamp + MaxResponseTime);
			eventMoveAutonomous(OldTimeStamp - CurrentTimeStamp, NewbPressedJump, OldbDoubleJump, OldDoubleClickMove, Accel, FRotator(0,0,0), CurrentAimRotation);
			CurrentTimeStamp = OldTimeStamp;
		}
	}

	//scion rwd - unpack bytes into analog controls
	// Convert ByteToFloat (taken from Object.uc - signed)
	Ctrl_Move_Fwd_Pct = ((FLOAT(XAxis)/128.f)-1.f);
	Ctrl_Move_Stf_Pct = ((FLOAT(YAxis)/128.f)-1.f);

	// View components
	//epic superville: Cleaner compression with no roundoff error
	ViewPitch	= (View & 65535);
	ViewYaw		= (View >> 16);

	//epic superville: Uncompress the current aim rotation
	AimRot.Pitch	= (CompressedAimRotation & 65535);
	AimRot.Yaw		= (CompressedAimRotation >> 16);

	// Make acceleration.
    Accel = InAccel * 0.1f;

    NewbPressedJump = (bJumpStatus != NewbJumpStatus);
    bJumpStatus		= NewbJumpStatus;

    // Save move parameters.
    DeltaTime = Min<FLOAT>( MaxResponseTime, TimeStamp - CurrentTimeStamp );

    CurrentTimeStamp	= TimeStamp;
    ServerTimeStamp		= LevelInfo->TimeSeconds;
    ViewRot.Pitch		= ViewPitch;
    ViewRot.Yaw			= ViewYaw;
    ViewRot.Roll		= 0;

	if( !Pawn || Pawn->bServerMoveSetPawnRot )
	{
		FCheckResult Hit(1.0f);
		GetLevel()->MoveActor( this, FVector(0,0,0), ViewRot, Hit );
	}

	// only process the move if client has the correct pawn
	if( AcknowledgedPawn == Pawn )
	{
	    if( Pawn && Pawn->bServerMoveSetPawnRot )
	    {
	        Rot.Roll	= 256 * ClientRoll;
	        Rot.Yaw		= ViewYaw;
	        if( Pawn->Physics == PHYS_Flying )
			{
	            maxPitch = 2;
			}
	        else
			{
	            maxPitch = 0;
			}

	        if( (ViewPitch > maxPitch * RotationRate.Pitch) && 
				(ViewPitch < 65536 - maxPitch * RotationRate.Pitch) )
	        {
	            if( ViewPitch < 32768 )
				{
	                Rot.Pitch = maxPitch * RotationRate.Pitch;
				}
	            else
				{
	                Rot.Pitch = 65536 - maxPitch * RotationRate.Pitch;
				}
	        }
	        else
			{
	            Rot.Pitch = ViewPitch;
			}

	        DeltaRot = (Rotation - Rot);
			
			FCheckResult Hit(1.0f);
			GetLevel()->MoveActor( Pawn, FVector(0,0,0), Rot, Hit );
	    }

	    // Perform actual movement
	    if( !LevelInfo->Pauser && DeltaTime > 0.f )
		{
	        eventMoveAutonomous( DeltaTime, NewbPressedJump, NewbDoubleJump, DoubleClickMove, Accel, DeltaRot, AimRot );
		}

		//epic moelfke: remember location/velocity after last servermove
		if( Pawn )
		{
			LastServerMoveLocation	= Pawn->Location;
			LastServerMoveBase		= Pawn->Base;
			PredictedVelocity		= Pawn->Velocity;
		}

	    // Accumulate movement error.
	    if( ClientLoc.IsZero() )
		{
			return;		// first part of double servermove
		}
	    else if( LevelInfo->TimeSeconds - LastUpdateTime > 0.5f )
		{
	        ClientErr = 10000;
		}
	    else if( LevelInfo->TimeSeconds - LastUpdateTime > (180.f / Max<INT>( 1800, Player->CurrentNetSpeed )) )
	    {
	        if( !Pawn )
			{
	            LocDiff = Location - ClientLoc;
			}
	        else
			{
	            LocDiff = Pawn->Location - ClientLoc;
			}

			// client error is the size squared of server/client location delta
	        ClientErr = LocDiff | LocDiff;
	    }

	    // If client has accumulated a noticeable positional error, correct him.
	    if( ClientErr > 5 )
	    {
	        if( !Pawn )
	        {
	            PendingAdjustment.newPhysics	= Physics;
	            PendingAdjustment.NewLoc		= Location;
	            PendingAdjustment.NewVel		= Velocity;
	        }
	        else
	        {
	            PendingAdjustment.newPhysics	= Pawn->Physics;
	            PendingAdjustment.NewVel		= Pawn->Velocity;
	            PendingAdjustment.NewBase		= Pawn->Base;
	            if( Pawn->Base && Pawn->Base->IsAMover() )
				{
	                PendingAdjustment.NewLoc = Pawn->Location - Pawn->Base->Location;
				}
	            else
				{
	                PendingAdjustment.NewLoc = Pawn->Location;
				}

	            PendingAdjustment.NewFloor = Pawn->Floor;
	        }

			//log(" Client Error at "$TimeStamp$" is "$ClientErr$" with accel "$Accel$" LocDiff "$LocDiff$" Physics "$Pawn.Physics);
	        LastUpdateTime = LevelInfo->TimeSeconds;

			PendingAdjustment.TimeStamp = TimeStamp;
			PendingAdjustment.NewState	= (GetStateFrame() && GetStateFrame()->StateNode) ? GetStateFrame()->StateNode->GetFName() : FName(NAME_None);
	    }

		//log("Server moved stamp"@TimeStamp$" loc: "$Pawn.Location$" accel: "$Pawn.Acceleration$" vel: "$Pawn.Velocity);
	}

	unguard;
}



void APlayerController::execEndGame(FFrame& Stack, RESULT_DECL)
{
	guard(APlayerController::execEndGame);

	P_GET_NAME(RungName);
	P_GET_INT(LadderIndex);
	P_GET_INT(CharacterIndex);
	P_GET_UBOOL(bWonRung);
	P_FINISH;

	if (Level->NetMode == NM_Standalone && bWonRung)
	{
		UViewport* Viewport = Cast<UViewport>(Player);
		if (Viewport && Viewport->Profile && Stats)
		{
			if (Level && Level->GetOuter()->GetFName() != FName(TEXT("Entry")) && Level->GetOuter()->GetFName() != FName(TEXT("Menu")))
			{
				// Only update rung-related stats if we won the game.
				Viewport->Profile->MergeStatData(Stats, true);

#ifdef _XBOX
				UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
				if (UIX != NULL && UIX->SignedIn())
				{
					if (Level->Game->eventIsLadderMatch())
					{
						UGUIController* GUI = Cast<UGUIController>(Player->GUIController);
						if (GUI != NULL)
						{
							GUI->eventHashLadderIndex(CharacterIndex, LadderIndex);
							FString RungNameString(*RungName);
							INT UnderscoreIndex = RungNameString.InStr(TEXT("_"), TRUE);
							if (UnderscoreIndex != -1)
							{
								INT RungIndex = appAtoi(*RungNameString.Mid(UnderscoreIndex + 1));
								XboxLiveManager* Manager = XboxLiveManager::getXboxLiveManager();
								if (Manager != NULL)
								{
									debugf(TEXT("LadderIndex: %d, RungIndex: %d"), LadderIndex, RungIndex);
									Manager->StartStatsLadderWrite(LadderIndex, RungIndex, Stats->GetFloatStatByRung(RungName, RUNG_STAT_FLOAT_FASTEST_WIN));
								}
							}
						}
					}
				}
#endif
			}
		}
	}

	unguard;
}

void APlayerController::execGetSplitscreenLayout( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerController::execGetSplitscreenLayout);

	P_GET_INT_REF(out_ViewportIndex);
	P_GET_INT_REF(out_TotalViewports);
	P_FINISH;

	INT& ViewportIndex = *out_ViewportIndex = 0;
	INT& TotalViewports = *out_TotalViewports = 0;
	*(UBOOL*)Result = 0;

	UViewport* Viewport = Cast<UViewport>(Player);
	if ( Viewport == NULL )
		return;

	ULevel* Lvl = GetLevel();
	if ( Lvl && Lvl->Engine && Lvl->Engine->Client )
	{
		UClient* Client = Lvl->Engine->Client;
		TotalViewports = Client->Viewports.Num();

		if ( TotalViewports > 0 )
		{
			ViewportIndex = Client->Viewports.FindItemIndex(Viewport);
			*(UBOOL*)Result = 1;
		}
	}
	unguardexec;
}

/* epic ===============================================
* ::IsSplitScreen
* Author: sz
*
* Returns true if this player controller is playing in
* a split screen viewport
*
* =====================================================
*/
void APlayerController::execIsSplitScreen( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerController::execIsSplitScreen);

	P_FINISH;
	
	*(UBOOL*)Result = 0;

	UViewport* Viewport = Cast<UViewport>(Player);
	if ( Viewport == NULL )
		return;

	ULevel* Lvl = GetLevel();
	if ( Lvl && Lvl->Engine && Lvl->Engine->Client )
	{
		UClient* Client = Lvl->Engine->Client;
		USplitScreenMgr* SplitScreenMgr = Client->GetSplitScreenMgr();
		if( SplitScreenMgr )
		{
			*(UBOOL*)Result = SplitScreenMgr->IsSplitScreenViewport( Viewport );
		}		
	}
	unguardexec;
}

void APlayerController::execGetRawInputObject( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerController::execGetRawInputObject);

	P_FINISH;

	UViewport* Viewport = Cast<UViewport>(Player);
	if ( Viewport )
		*(UInput**)Result = Viewport->Input;

	unguardexec;
}

void APlayerController::execServerNotifyVoiceSystem(FFrame& Stack, RESULT_DECL)
{
	guard(APlayerController::execServerNotifyVoiceSystem);

	P_GET_BYTE(NotifyType);
	P_GET_STR(SrcXUIDStr);
	P_GET_STR(DataStr);
	P_FINISH;

#ifdef _XBOX
	if (UEngine::GEngine->Voice == NULL || PlayerReplicationInfo == NULL) { return; }

	BYTE SrcXUIDBytes[12];
	StringToBytes(SrcXUIDBytes, 12, SrcXUIDStr);
	XUID SrcXUID = *(XUID*) SrcXUIDBytes;

	BYTE Data[12];
	StringToBytes(Data, 12, DataStr);

	UTcpipConnectionXbox* SrcConnection = Cast<UTcpipConnectionXbox>(Player);

	UEngine::GEngine->Voice->HandleVoiceNotification((ENotifyType) NotifyType, SrcConnection, SrcXUID, Data);
#endif // _XBOX

	unguardexec;
}

void APlayerController::execClientNotifyVoiceSystem(FFrame& Stack, RESULT_DECL)
{
	guard(APlayerController::execClientNotifyVoiceSystem);

	P_GET_BYTE(NotifyType);
	P_GET_STR(SrcXUIDStr);
	P_GET_STR(DataStr);
	P_FINISH;

#ifdef _XBOX
	if (UEngine::GEngine->Voice == NULL || PlayerReplicationInfo == NULL) { return; }

	BYTE SrcXUIDBytes[12];
	StringToBytes(SrcXUIDBytes, 12, SrcXUIDStr);
	XUID SrcXUID = *(XUID*) SrcXUIDBytes;

	BYTE Data[12];
	StringToBytes(Data, 12, DataStr);

	UEngine::GEngine->Voice->HandleVoiceNotification((ENotifyType) NotifyType, NULL, SrcXUID, Data);
#endif // _XBOX

	unguardexec;
}

void APlayerController::execUpdateLocalStatsCache( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerController::execUpdateLocalStatsCache);

	P_FINISH;

	if ( Stats && PlayerReplicationInfo && PlayerReplicationInfo->StatCache )
	{
		PlayerReplicationInfo->StatCache->UpdateStats(*Stats);
	}

	unguardexec;
}

void APlayerController::execConsoleCommand( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerController::execConsoleCommand);

	P_GET_STR(Command);
	P_GET_UBOOL_OPTX(bWriteToLog, false);
	P_FINISH;

	*(FString*)Result = TEXT("");
	FStringOutputDevice StrOut;
	if( Player )
	{
		if (bWriteToLog)
		{
			Player->Exec(*Command, *GLog);
		}
		else
		{
			Player->Exec(*Command, StrOut);
		}
	}
	else
	{
		if (bWriteToLog)
		{
			GetLevel()->Engine->Exec(*Command, *GLog);
		}
		else
		{
			GetLevel()->Engine->Exec(*Command, StrOut);
		}
	}

	*(FString*)Result = *StrOut;

	unguardexec;
}

void APlayerController::execSetNetSpeed( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerController::execSetNetSpeed);

	P_GET_INT(NewSpeed);
	P_FINISH;

	//epic moelfke: changed this so it also works to change server to client netspeed
	UNetDriver* Driver = GetLevel()->NetDriver;
#ifdef _XBOX
	extern UBOOL GIsMidwayServer;
	// Don't throttle Midway's servers
	if (Player && NewSpeed > 1800 && Driver && GIsMidwayServer == FALSE)
#else
	if (Player && NewSpeed > 1800 && Driver)
#endif
	{
		Player->CurrentNetSpeed = Clamp(NewSpeed, 1800, Driver->MaxClientRate);
		if (Driver->ServerConnection)
		{
			Driver->ServerConnection->CurrentNetSpeed = Player->CurrentNetSpeed;
		}
	}
	unguardexec;
}

void APlayerController::execUpdateURL( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerController::execUpdateURL);

	P_GET_STR(NewOption);
	P_GET_STR(NewValue);
	P_GET_UBOOL_OPTX(bUpdateOnly,false);
	P_FINISH;

	if ( bUpdateOnly )
	{
		FString Value;
		if ( !GetPlayerURLOption(NewOption, Value) )
			return;
	}

	UpdatePlayerURL(NewOption, NewValue);

	unguardexec;
}

void APlayerController::execClearURL( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerController::execClearURL);

	P_GET_STR_OPTX(Option,TEXT(""));
	P_FINISH;

	ClearPlayerURL(Option);

	unguardexec;
}

void APlayerController::execGetURLOption( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerController::execGetURLOption);
	
	P_GET_STR(OptionName);
	P_GET_STR_REF(out_OptionValue);
	P_FINISH;

	*(UBOOL*)Result = GetPlayerURLOption(OptionName, *out_OptionValue);
	unguardexec;
}

void APlayerController::execClientTravel( FFrame& Stack, RESULT_DECL )
{
	guardSlow(APlayerController::execClientTravel);

	P_GET_STR(URL);
	P_GET_BYTE(TravelType);
	P_GET_UBOOL(bItems);
	P_FINISH;

	*(UBOOL*)Result = 0;
	if( Player )
	{
		// Warn the client.
		if (eventPreClientTravel(URL, TravelType, bItems))
		{
			// Do the travel.
			GetLevel()->Engine->SetClientTravel( Player, *URL, bItems, (ETravelType)TravelType );
			*(UBOOL*)Result = 1;
		}
	}

	unguardexecSlow;
}

UBOOL APlayerController::GetPlayerURLOption(const FString& OptionName, FString& OptionValue )
{
	guard(APlayerController::GetPlayerURLOption);

	UViewport* Viewport = Cast<UViewport>(Player);
	FURL& URL = Viewport
		? Viewport->PlayerURL
		: GetLevel()->URL;

	FString Value = URL.GetOption(*OptionName,TEXT("NOT_FOUND"));
	if ( Value == TEXT("NOT_FOUND") )
	{
		OptionValue = TEXT("");
		return false;
	}
	else
	{
		INT pos = 0;
		if ( Value.Len() && Value.Left(1) == TEXT("=") )
			pos = 1;

		OptionValue = Value.Mid(pos);
		return true;
	}

	unguard;
}

void APlayerController::UpdatePlayerURL( const FString& Option, const FString& Value )
{
	guard(APlayerController::UpdatePlayerURL);

	FString NewValue = Option;
	if ( Value != TEXT("") )
		NewValue += FString::Printf(TEXT("=%s"), *Value);

	UViewport* Viewport = Cast<UViewport>(Player);
	FURL& URL = Viewport
		? Viewport->PlayerURL
		: GetLevel()->URL;

	debugf(NAME_DevRon, TEXT("UpdatePlayerURL  NewValue '%s'  OldValue '%s'"), *NewValue, URL.GetOption(*Option,TEXT("")));
	URL.AddOption(*NewValue);

	unguard;
}

static void ClearURLOption( FURL& URL, FString& Option )
{
	guard(ClearURLOption);

	// If no option was specified, clear the entire URL
	if ( Option.Len() == 0 )
	{
		URL = FURL(*URL.Map);
	}
	else
	{
		INT pos = Option.InStr(TEXT("="));
		if ( pos != INDEX_NONE )
			Option = Option.Left(pos);

		for ( INT i = URL.Op.Num() - 1; i >= 0; i-- )
		{
			FString& Op = URL.Op(i);
			pos = Op.InStr(TEXT("="));
			if ( Option == (pos == INDEX_NONE ? Op : Op.Left(pos)) )
			{
				debugf(NAME_DevUI,TEXT("Removing commandline option %i '%s'"), i, *Op);
				URL.Op.Remove(i,1);
			}
		}
	}

	unguard;
}

void APlayerController::ClearPlayerURL( FString& Option )
{
	guard(APlayerController::ClearPlayerURL);

	UViewport* Viewport = Cast<UViewport>(Player);
	if ( Viewport )
		ClearURLOption(Viewport->PlayerURL, Option);

	ClearURLOption(GetLevel()->URL, Option);
	ClearURLOption(((UGameEngine*)UEngine::GEngine)->LastURL, Option);

	unguard;
}

/*---------------------------------------------------------------------------------------
Player Profiles
---------------------------------------------------------------------------------------*/
void APlayerController::execGetProfile( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerController::execGetProfile);

	P_FINISH;

	UViewport* Viewport = Cast<UViewport>(Player);
	if ( Viewport )
		*(UObject**)Result = Viewport->Profile;

	unguardexec;
}

void APlayerController::execLoadProfile( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerController::execLoadProfile);

	P_GET_STR(ProfileName);
	P_GET_UBOOL_OPTX(bRebindProfile,0);
	P_FINISH;

	*(UBOOL*)Result = 0;

	UViewport* Viewport = Cast<UViewport>(Player);
	if (Viewport && Viewport->Profile)
	{
		if (Viewport->Profile->LoadProfile(ProfileName))
		{
			if (bRebindProfile)
			{
				Viewport->Profile->eventSetProfileName(ProfileName);
				if (!Viewport->Profile->eventSetPlayerOwner(this))
				{
					Viewport->Profile->eventInitialize();
				}
			}

			*(UBOOL*)Result = 1;
		}
	}

	unguard;
}

void APlayerController::execSetDisconnectFlagValue( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerController::execSetDisconnectFlagValue);

	P_GET_UBOOL(bValue);
	P_GET_UBOOL(bTeamGame);
	P_FINISH;

	ULoadSaveManager* Manager = CastChecked<ULoadSaveManager>(GLoadSaveManager);
	Manager->SetDisconnectFlag(bValue,bTeamGame);

	unguardexec;
}

void APlayerController::execGetDisconnectFlagValue( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerController::execGetDisconnectFlagValue);

	P_GET_BYTE_REF(bTeamGame);
	P_FINISH;

	ULoadSaveManager* Manager = CastChecked<ULoadSaveManager>(GLoadSaveManager);
	*(UBOOL*)Result = Manager->GetDisconnectFlag(*bTeamGame);

	unguardexec;
}

#if _XBOX
#include "UnUIXInteraction.h"
#endif

void APlayerController::PostNetReceive()
{
	guard(APlayerController::PostNetReceive);

	Super::PostNetReceive();

	if ( Player && PlayerReplicationInfo && !PlayerReplicationInfo->bRegisteredUIX )
	{
#ifdef _XBOX
		UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
		if ( UIX && UIX->SignedIn() && 
			(PlayerReplicationInfo->bBot == false) )
		{
			UIX->RegisterPlayer(this);
			eventInitPlayerStats();
		}
#endif
	}

	unguard;
}
/*
FIXME - demo mode disabled
void APlayerController::PostNetReceive()
{
	guard(APlayerController::PostNetReceive);

	AActor::PostNetReceive();

	if( GetLevel()->DemoRecDriver && GetLevel()->DemoRecDriver->ServerConnection )
	{
		Rotation.Pitch = DemoViewPitch;
		Rotation.Yaw = DemoViewYaw;
	}
	unguard;
}
*/

void APlayerController::execRegisterUIX( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerController::execRegisterUIX);

	P_FINISH;

#ifdef _XBOX
	UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
	if ( UIX )
		UIX->RegisterPlayer(this);
#endif

	unguardexec;
}

void APlayerController::execSetViewTarget( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerController::execResetKeyboard);

	P_GET_ACTOR(NewViewTarget);
	P_FINISH;

	ViewTarget = NewViewTarget;
	if ( ViewTarget == this )
		RealViewTarget = NULL;
	else
		RealViewTarget = Cast<AController>(ViewTarget);
	GetViewTarget();	
	if ( ViewTarget == this )
		RealViewTarget = NULL;
	unguard;
}

void APickup::execAddToNavigation( FFrame& Stack, RESULT_DECL )
{
	guard(APickup::execAddToNavigation);

	P_FINISH;

	if ( PickupCache )
	{
		if ( PickupCache->InventoryCache == this )
			PickupCache->InventoryCache = NULL;
		PickupCache = NULL;
	}

	// find searcher
	APawn *Searcher = NULL;
	for ( AController *C=Level->ControllerList; C!=NULL; C=C->nextController )
		if ( C->bIsPlayer && C->Pawn )
		{
			Searcher = C->Pawn;
			break;
		}
	if ( !Searcher )
		return;

	// find nearest path
	FSortedPathList EndPoints;
	for ( ANavigationPoint *N=Level->NavigationPointList; N!=NULL; N=N->nextNavigationPoint )
	{
		INT dist = appTrunc((Location - N->Location).SizeSquared());
		if ( (dist < MAXPATHDISTSQ) && (Location.Z - N->Location.Z < UCONST_MAXSTEPHEIGHT + MAXJUMPHEIGHT)
			&& (!N->InventoryCache || N->InventoryCache->bDeleteMe || (N->InventoryCache->MaxDesireability <= MaxDesireability)) )
		{
			EndPoints.addPath(N, dist);
		}
	}

	if ( EndPoints.numPoints > 0 )
		PickupCache = EndPoints.findEndAnchor(Searcher,this,Location,false,false);

	if ( PickupCache )
	{
		PickupCache->InventoryCache = this;
	}
	unguard;
}

void APickup::execRemoveFromNavigation( FFrame& Stack, RESULT_DECL )
{
	guard(APickup::execRemoveFromNavigation);

	P_FINISH;

	if ( !PickupCache )
		return;
	if ( PickupCache->InventoryCache == this )
		PickupCache->InventoryCache = NULL;
	unguard;
}

void AController::execFindBestInventoryPath( FFrame& Stack, RESULT_DECL )
{
	guard(AController::execFindBestInventoryPath);

	P_GET_FLOAT_REF(Weight);
	P_FINISH;

	if ( !Pawn )
	{
		*(AActor**)Result = NULL;
		return;
	}
	GScriptCycles += appCycles();
	CLOCK(FindPathCyc,GStats.DWORDStats(GEngineStats.STATS_Game_FindPathCycles));
	AActor * bestPath = NULL;
	PendingMover = NULL;
	bPreparingMove = false;

	// first, look for nearby dropped inventory
	if ( Pawn->ValidAnchor() )
	{
		if ( Pawn->Anchor->InventoryCache )
		{
			if ( Pawn->Anchor->InventoryCache->bDeleteMe )
				Pawn->Anchor->InventoryCache = NULL;
			else if (eventDesireability(Pawn->Anchor->InventoryCache) > 0.f)
			{
				if ( Pawn->actorReachable(Pawn->Anchor->InventoryCache) )
				{
					RouteGoal = Pawn->Anchor->InventoryCache;
					*(AActor**)Result = Pawn->Anchor->InventoryCache;
					return;
				}
				else
				{
					Pawn->Anchor->InventoryCache = NULL;
				}
			}
		}
	}

	*Weight = Pawn->findPathToward(NULL,FVector(0,0,0),&FindBestInventory, *Weight,false);
	if ( *Weight > 0.f )
		bestPath = SetPath();

	GScriptCycles -= appCycles();

	*(AActor**)Result = bestPath;
	UNCLOCK();
	unguard;
}

/* epic ===============================================
* ::execFindBestPickupInRangeOf
* Author: moelfke
*
* Finds the best pickup within the specified range of
* Point, and sets a path to it, returning the first
* actor in that path
*
* =====================================================
*/
void AController::execFindBestPickupInRangeOf(FFrame& Stack, RESULT_DECL)
{
	guard(AController::execFindBestPickupInRangeOf);

	P_GET_VECTOR(Point);
	P_GET_STRUCT(FRange, DesiredDist);
	P_FINISH;

	// if we don't have a pawn, we can't do anything
	if (!Pawn)
	{
		*(AActor**)Result = NULL;
	}
	else
	{
		GScriptCycles += appCycles();
		CLOCK(FindPathCyc,GStats.DWORDStats(GEngineStats.STATS_Game_FindPathCycles));

		// clear previous path navigation properties
		PendingMover	= NULL;
		bPreparingMove	= false;

		// square the desired dist so we can use the faster SizeSquared() when comparing distances
		FRange DesiredDistSquared(DesiredDist.Min * DesiredDist.Min, DesiredDist.Max * DesiredDist.Max);

		for (ANavigationPoint* N = Level->NavigationPointList; N; N = N->nextNavigationPoint)
		{
			AInventorySpot* IS = N->GetAInventorySpot();
			// if this is an inventory spot that marks a pickup we can get
			if (IS && IS->markedItem && IS->markedItem->IsProbing(NAME_Touch))
			{
				FLOAT DistSquared = (IS->Location - Point).SizeSquared();
				// and it's in the specified range
				if (DistSquared >= DesiredDistSquared.Min && DistSquared <= DesiredDistSquared.Max)
				{
					// mark this inventory spot as an end point
					IS->bTransientEndPoint = true;
				}
			}
		}

		// find the best path
		// FindBestMarkedInventory() will only evaluate the nodes we've preselected
		if (Pawn->findPathToward(NULL, FVector(0,0,0), &FindBestMarkedInventory, 0.f, false) > 0.f)
		{
			*(AActor**)Result = SetPath();
		}
		else
		{
			// we failed to find anything
			*(AActor**)Result = NULL;
		}

		GScriptCycles -= appCycles();
		UNCLOCK();
	}

	unguardexec;
}

/* epic ===============================================
* ::IsSuperPickupAvailable
* Author: moelfke
*
* Returns true if there is a super (bSuperPickup == true)
* pickup that is ready to pickup or will be in the
* Controller's RespawnPredictionTime or less
*
* NOTE: Only inventory marked by an InventorySpot is
* checked; dropped pickups are not
*
* =====================================================
*/
UBOOL AController::IsSuperPickupAvailable()
{
	guard(AController::IsSuperPickupAvailable);

	UBOOL bResult = 0;
	for (ANavigationPoint* N = Level->NavigationPointList; N && !bResult; N = N->nextNavigationPoint)
	{
		AInventorySpot* IS = N->GetAInventorySpot();
		if ( IS && IS->markedItem && IS->markedItem->bSuperPickup &&
			(IS->markedItem->IsProbing(NAME_Touch) || IS->markedItem->LatentFloat < RespawnPredictionTime) &&
			eventDesireability(IS->markedItem) > 0.f )
		{
			bResult = 1;
		}
	}

	return bResult;

	unguard;
}

void AController::execIsSuperPickupAvailable(FFrame& Stack, RESULT_DECL)
{
	guard(AController::execIsSuperPickupAvailable);

	P_FINISH;

	*(UBOOL*)Result = IsSuperPickupAvailable();

	unguard;
}

/* epic ===============================================
* ::execFindBestSuperPickupPath
* Author: moelfke
*
* Pathfinds to the nearest available super pickup and puts
* the path in the RouteCache array, then returns the
* first Actor along that path
*
* NOTE: Only inventory marked by an InventorySpot is
* checked; dropped pickups are not
*
* =====================================================
*/
void AController::execFindBestSuperPickupPath(FFrame& Stack, RESULT_DECL)
{
	guard(AController::execFindBestSuperPickupPath);

	P_GET_UBOOL_OPTX(bWeightDetours, 0);
	P_FINISH;

	// if we don't have a pawn, or there are no superpickups available, we can't do anything
	if (!Pawn || !IsSuperPickupAvailable())
	{
		*(AActor**)Result = NULL;
	}
	else
	{
		GScriptCycles += appCycles();
		CLOCK(FindPathCyc,GStats.DWORDStats(GEngineStats.STATS_Game_FindPathCycles));

		// clear previous path navigation properties
		PendingMover	= NULL;
		bPreparingMove	= false;

		// find the best path, using FindBestSuperPickup as the node evaluation function
		if (Pawn->findPathToward(NULL, FVector(0,0,0), &FindBestSuperPickup, 0.f, false) > 0.f)
		{
			*(AActor**)Result = SetPath();
		}
		else
		{
			// we failed to find anything
			*(AActor**)Result = NULL;
		}

		GScriptCycles -= appCycles();
		UNCLOCK();
	}

	unguard;
}

/* epic ===============================================
* ::execCheckDetour
* Author: moelfke
*
* Checks if there is a useful detour the AI can make
* that is reachable from its Pawn's current Anchor and
* if so adds the best one to the beginning of its current
* path in the RouteCache array
*
* NOTE: Entries at the end of the RouteCache array may
* be removed to make room for the detour
*
* =====================================================
*/
void AController::execCheckDetour(FFrame& Stack, RESULT_DECL)
{
	guard(AController::execCheckDetour);

	P_FINISH;

	// if we have a pawn with a valid Anchor
	if (Pawn && Pawn->ValidAnchor())
	{
		AActor* DetourDest = NULL;
		FLOAT DetourWeight = 0.f;
		UBOOL bNavDetour = 1;
		// get Pawn info
		INT iRadius = appTrunc(Pawn->CollisionRadius);
		INT iHeight = appTrunc(Pawn->CollisionHeight);
		INT iMaxFallSpeed = appTrunc(Pawn->MaxFallSpeed);
		int MoveFlags = Pawn->calcMoveFlags();

		for (INT i = 0; i < Pawn->Anchor->PathList.Num(); i++)
		{
			UReachSpec *spec = Pawn->Anchor->PathList(i);
			// if the path supports our Pawn and is not blocked
			if (spec->End && !spec->End->bBlocked && spec->supports(iRadius, iHeight, MoveFlags, iMaxFallSpeed))
			{
				UReachSpec *Return = spec->End->GetReachSpecTo(Pawn->Anchor);
				// and it has an unforced return path
				if (Return && !(Return->reachFlags & R_FORCED))
				{
					// get its path distance
					INT PathWeight;
					if ((spec->reachFlags & R_FORCED) && spec->End->bSpecialForced)
					{
						PathWeight = spec->Distance + spec->End->cost + spec->End->eventSpecialCost(Pawn, spec);
					}
					else
					{
						PathWeight = spec->Distance + spec->End->cost;
					}
					// if it's relatively close
					if (PathWeight < 2 * MAXPATHDIST)
					{
						// rate the detour
						FLOAT NewDetourWeight = spec->End->eventDetourWeight(Pawn, PathWeight);
						if (NewDetourWeight > DetourWeight)
						{
							// this is our new best detour
							DetourDest = spec->End;
							DetourWeight = NewDetourWeight;
						}
					}
				}
			}
		}

		// now check for a dropped pickup
		// we check this last because we have to do expensive actorReachable() to know if we can get it
		if (Pawn->Anchor->InventoryCache)
		{
			if (Pawn->Anchor->InventoryCache->bDeleteMe)
			{
				Pawn->Anchor->InventoryCache = NULL;
			}
			// if it's better than our current detour (if any)
			else if (Pawn->Anchor->InventoryCache->eventDetourWeight(Pawn, (Pawn->Anchor->InventoryCache->Location - Pawn->Location).Size()) > DetourWeight)
			{
				// and it's reachable
				if (Pawn->actorReachable(Pawn->Anchor->InventoryCache))
				{
					DetourDest = Pawn->Anchor->InventoryCache;
					bNavDetour = 0;
				}
				else
				{
					// if it isn't reachable, remove the InventoryCache reference so we don't waste time trying this again
					Pawn->Anchor->InventoryCache = NULL;
				}
			}
		}

		// if DetourDest is already in our route, we don't need to do anything
		for (INT i = 0; i < ARRAY_COUNT(RouteCache) && RouteCache[i] && DetourDest; i++)
		{
			if (RouteCache[i] == DetourDest)
			{
				DetourDest = NULL;
			}
		}

		// if we have a detour, add it to the beginning of our current path in the RouteCache array
		if (DetourDest)
		{
			//debugf(TEXT("Adding detour to %s to route"), DetourDest->GetName());
			// if we're detouring to a NavigationPoint, see if it can directly reach our original path
			if ( bNavDetour && Cast<ANavigationPoint>(RouteCache[0])
					&& ((ANavigationPoint*)DetourDest)->GetReachSpecTo((ANavigationPoint*)RouteCache[0]) )
			{
				// add detour to beginning of path and move from the detour point to our original path
				for (INT i = ARRAY_COUNT(RouteCache) - 1; i > 0; i--)
				{
					if (RouteCache[i - 1])
					{
						RouteCache[i] = RouteCache[i - 1];
					}
				}
				RouteCache[0] = DetourDest;
			}
			else
			{
				// after going to the detour we must backtrack to our current anchor
				for (INT i = ARRAY_COUNT(RouteCache) - 1; i > 1; i--)
				{
					if (RouteCache[i - 2])
					{
						RouteCache[i] = RouteCache[i - 2];
					}
				}
				RouteCache[0] = DetourDest;
				RouteCache[1] = Pawn->Anchor;
			}
		}
	}

	unguard;
}

void AController::execStopWaiting( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execStopWaiting);

	P_FINISH;

	if( GetStateFrame()->LatentAction == EPOLL_Sleep )
		LatentFloat = -1.f;

	unguardSlow;
}

/* CanSee()
returns true if LineOfSightto object and it is within creature's
peripheral vision
*/

void AController::execCanSee( FFrame& Stack, RESULT_DECL )
{
	guardSlow(APawn::execCanSee);

	P_GET_ACTOR(Other);
	P_FINISH;

	*(DWORD*)Result = SeeActor( Other, false );
	unguardSlow;
}

/**
 * GetDotToTarget
 * Calculates horizontal and vertical dot between a Target actor and the Direction vertor.
 * Dots are set to 1.f if direction points to the Target's collision cylinder.
 * Otherwise the dots are calculated on an adjusted Target's location, using the closest point to the Target's collision cylinder.
 * This is used, for example, to determine the angular distance between the player's aim and a target for adhesion or friction.
 *
 * @param	Target			Target Actor
 * @param	Start			Point of origin.
 * @param	Direction		Direction
 * @param	out_LatDot		Lateral dot		[0.f,1.f]
 * @param	out_VertDot		Vertical dot 	[0.f,1.f]
 *
 * @return	adjusted distance to target.
 */
FLOAT AController::GetDotToTarget
(
	AActor	*Target,
	const FVector& Start,
	FVector Direction,
	FLOAT	&out_LatDot,
	FLOAT	&out_VertDot
)
{
	FVector		VectToTarget	= Target->Location - Start;
	VectToTarget.Normalize();
	Direction.Normalize();

	FCoords		Targ_Coords		= GMath.UnitCoords / VectToTarget.Rotation();
	FVector		Targ_Y			= Targ_Coords.YAxis;
	FVector		Targ_Z			= Targ_Coords.ZAxis;
	FCoords		View_Coords		= GMath.UnitCoords / Direction.Rotation();
	FVector		View_Y			= View_Coords.YAxis;
	FVector		View_Z			= View_Coords.ZAxis;

	// Adjusted Target Location used for final dotting
	FVector	AdjustedTargetLocation = Target->Location;

	// Figure out collision cylinder's "edge"
	// First find out on which side we should extend the radius
	FLOAT	ViewDotRight	= Direction | Targ_Y;
	INT		YDirMod			= (ViewDotRight < 0) ? -1 : 1;
	
	// and the height...
	FLOAT	ViewDotUp		= Direction | Targ_Z;
	INT		ZDirMod			= (ViewDotUp < 0) ? -1 : 1;

	// define extent from cylinder's center
	FVector	HorizEdgeOffset = Targ_Y * YDirMod * Target->CollisionRadius;
	FVector	VertEdgeOffset	= Targ_Z * ZDirMod * Target->CollisionHeight;

	// that's the closest collision cylinder edge to the Direction vector
	FVector	ClosestColEdge	= Target->Location + HorizEdgeOffset + VertEdgeOffset;
	FVector ClosestEdgeDir	= ClosestColEdge - Start;
	ClosestEdgeDir.Normalize();

	// Find out if we're aiming at the collision cylinder
	FLOAT	ViewRightDotTarg	= View_Y | VectToTarget;
	FLOAT	ViewRightDotOffset	= View_Y | ClosestEdgeDir;
	FLOAT	ViewUpDotTarg		= View_Z | VectToTarget;
	FLOAT	ViewUpDotOffset		= View_Z | ClosestEdgeDir;

	if( ((ViewRightDotTarg > 0.f && ViewRightDotOffset < 0.f) || (ViewRightDotTarg < 0.f && ViewRightDotOffset > 0.f)) && 
		((ViewUpDotTarg > 0.f && ViewUpDotOffset < 0.f) || (ViewUpDotTarg < 0.f && ViewUpDotOffset > 0.f)) )
	{
		// if we aim at the target's collision cylinder, assume we're aligned to it.
		out_LatDot	= 1.f;
		out_VertDot = 1.f;
	}
	else // otherwise we pick the closest between the cylinder's center or the cylinder's edge
	{
		// dot to target
		VectToTarget.Dot2D( Direction, out_LatDot, out_VertDot );

		// dot to closest collision cylinder edge
		FLOAT	LatDot_Edge, VertDot_Edge;
		ClosestEdgeDir.Dot2D( Direction, LatDot_Edge, VertDot_Edge );

		// figure out of if we're closer to target's location or edge's location
		if( LatDot_Edge > out_LatDot )
		{
			out_LatDot = LatDot_Edge;
			AdjustedTargetLocation += HorizEdgeOffset;
		}

		// figure out of if we're closer to target's location or edge's location
		if( VertDot_Edge > out_VertDot )
		{
			out_VertDot = VertDot_Edge;
			AdjustedTargetLocation += VertEdgeOffset;
		}
	}

	/*
	// debug lines
	// line to adjusted target location
	GTempLineBatcher->AddLine(Pawn->Location, AdjustedTargetLocation, FColor(255, 0, 255));

	// View Axes
	GTempLineBatcher->AddLine(Pawn->Location, Pawn->Location + Direction	* 500.f, FColor(255, 000, 000));
	GTempLineBatcher->AddLine(Pawn->Location, Pawn->Location + View_Y		* 100.f, FColor(000, 255, 000));
	GTempLineBatcher->AddLine(Pawn->Location, Pawn->Location + View_Z		* 100.f, FColor(000, 000, 255));

	// Target Axes
	GTempLineBatcher->AddLine(Target->Location, Target->Location - VectToTarget	* 100.f, FColor(255, 000, 000));
	GTempLineBatcher->AddLine(Target->Location, Target->Location + Targ_Y		* 100.f, FColor(000, 255, 000));
	GTempLineBatcher->AddLine(Target->Location, Target->Location + Targ_Z		* 100.f, FColor(000, 000, 255));
	*/

	// return distance to adjusted location we used for dotting
	FLOAT DistToTarget = (AdjustedTargetLocation-Start).SizeSquared();
	return DistToTarget;
}

void AController::execGetTargetByDot( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execGetTargetByDot);

	P_GET_FLOAT_REF(bestAim_Lat);
	P_GET_FLOAT_REF(bestAim_Vert);
	P_GET_FLOAT_REF(bestDist);
	P_GET_VECTOR(ViewDir);
	P_GET_VECTOR(projStart);
	P_GET_FLOAT(MaxRange);
	//scion rwd - added support for specifying a diff target class
	P_GET_OBJECT_OPTX(UClass,extraTargetClass,NULL);
	P_FINISH;

	FLOAT	BestMetric		= 0;
	FLOAT	OrigAim_Lat		= *bestAim_Lat,
			OrigAim_Vert	= *bestAim_Vert;

	AActor		*Pick = NULL;
	FCheckResult Hit(1.f);

	MaxRange *= MaxRange;

	TArray<AActor*>	possibleTargets;		// list of targets to evaluate

	if( GetLevel()->GetLevelInfo()->NetMode == NM_Client ) {
		// FIXME use Level->PawnList
		INT iActor = 0;
		while( iActor < GetLevel()->Actors.Num() )
		{
			AActor* TestActor = GetLevel()->Actors(iActor++);
			if (TestActor && TestActor->IsAPawn())
			{
				APawn *P = (APawn*)TestActor;
				if( P != Pawn		&&
					P->Health > 0	&&
					P->bProjTarget	&&
						(!PlayerReplicationInfo			||
						 !P->PlayerReplicationInfo		||
						 !Level->GRI					||
						 !Level->GRI->bTeamGame			||
						  PlayerReplicationInfo->Team != P->PlayerReplicationInfo->Team) )
				{
					possibleTargets.AddItem(P);
				}
			}
		}
	}
	else {
		// first add any opposing pawns
		for( AController *next = GetLevel()->GetLevelInfo()->ControllerList; next != NULL; next = next->nextController ) {
			// look for best controlled pawn target which is on same team
			if( next != this				&&
				next->Pawn					&&
				next->Pawn->Health > 0		&&
				next->Pawn->bProjTarget		&&
					(!PlayerReplicationInfo			||
					!next->PlayerReplicationInfo	||
					!Level->GRI					||
					!Level->GRI->bTeamGame			||
					PlayerReplicationInfo->Team != next->PlayerReplicationInfo->Team) )
			{
				possibleTargets.AddItem(next->Pawn);
			}
		}
	}

	// next add any extra actors that were requested
	if (extraTargetClass != NULL)
	{
		for (INT i = 0; i < GetLevel()->Actors.Num(); i++)
		{
			AActor *actor = GetLevel()->Actors(i);
			if (actor != NULL &&
				!actor->bDeleteMe &&
				actor->IsA(extraTargetClass))
			{
				possibleTargets.AddItem(actor);
			}
		}
	}
	// now evaluate each target and pick the best one
	for (INT i = 0; i < possibleTargets.Num(); i++)
	{
		AActor	*actor = possibleTargets(i);
		FLOAT	newAim_Lat	= -1.f,
				newAim_Vert = -1.f;

		FLOAT	DistToTarget = GetDotToTarget( actor, projStart, ViewDir, newAim_Lat, newAim_Vert );
		
		if( newAim_Lat   >= 0.f				&&
			newAim_Lat   >= OrigAim_Lat		&&
			newAim_Vert  >= 0.f				&&
			newAim_Vert  >= OrigAim_Vert	&&
			DistToTarget <= MaxRange		)
		{
			DistToTarget = appSqrt(DistToTarget);

			FLOAT NewMetric		= newAim_Lat + newAim_Vert;
			if( NewMetric > BestMetric ) {
				// target is more in line than current best - see if target is visible
				GetLevel()->SingleLineCheck( Hit, this, actor->Location, Pawn->Location + FVector(0,0,Pawn->EyeHeight), TRACE_World|TRACE_StopAtFirstHit );
				/* what was the point of this second trace? requiring eye to eye sight?  if it's important,
				   then we'll have to add an extra cast for pawns in here to check for eyeheight
				if( Hit.Actor ) {
					GetLevel()->SingleLineCheck( Hit, this, actor->Location + FVector(0,0,actor->EyeHeight), Pawn->Location + FVector(0,0,Pawn->EyeHeight), TRACE_World|TRACE_StopAtFirstHit );
				}
				*/
				if( !Hit.Actor ) {
					Pick = actor;
					*bestAim_Lat  = newAim_Lat;
					*bestAim_Vert = newAim_Vert;
					*bestDist	  = DistToTarget;

					BestMetric	 = NewMetric;
				}
			}
		}
	}

	*(AActor**)Result = Pick;
	unguardSlow;
}

//scion superville: Modified PickTarget to use Lateral/Vertical angles seperately
//epic rwd - simplified, only use single dot, hopefully produce better results
void AController::execAquireTarget( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execAquireTarget);

	P_GET_FLOAT_REF(bestAim_Lat);
	P_GET_FLOAT_REF(bestAim_Vert);
	P_GET_FLOAT_REF(bestDist);
	P_GET_VECTOR(ViewDir);
	P_GET_VECTOR(projStart);
	P_GET_FLOAT(MaxRange);
	P_FINISH;

	FLOAT BestMetric	= 0.f;
	APawn *Pick = NULL;
	FCheckResult Hit(1.f);
    MaxRange *= MaxRange;

	// Use input best lat as the min FOV
	FLOAT MinFOV = *bestAim_Lat;

	for ( APawn *next=GetLevel()->GetLevelInfo()->PawnList; next!=NULL; next=next->NextPawn )
	{
		// look for best controlled pawn target which is on same team
		if( next != Pawn			&&
			next->Health > 0		&&
			next->bProjTarget		&&
				(!PlayerReplicationInfo			||
				 !next->PlayerReplicationInfo	||
				 !Level->GRI					||
				 !Level->GRI->bTeamGame			||
				(PlayerReplicationInfo->Team != next->PlayerReplicationInfo->Team)) )
		{
			// calculate the distance to the target and
			// calculate the dot from our viewdir
			FLOAT targetDist = 0.f;
			FLOAT targetDot	 = 0.f;
			FLOAT Junk		 = 0.f;
			targetDist = GetDotToTarget( next, projStart, ViewDir, targetDot, Junk );

			// if within the max range and an acceptable fov,
			if (targetDist < MaxRange &&
				targetDot > MinFOV)
			{
				// calculate the overall metric for this target
				FLOAT DistMetric = appPow( (MaxRange - targetDist) / MaxRange, TargetDistExp ) * TargetDistWeight;
				FLOAT DotPMetric = appPow( targetDot, TargetDotPExp ) * TargetDotPWeight;
				FLOAT NewMetric	 = DistMetric + DotPMetric;

				if( next->PlayerReplicationInfo && 
					next->PlayerReplicationInfo->bHasFlag )
				{
					NewMetric += FlagLockMetric;
				}
/*
				debugf( TEXT("%s Dist %f DistMetric %f DotP %f DotPMetric %f Total %f"),
					next->GetName(),
					targetDist, DistMetric, targetDot, DotPMetric,
					NewMetric );
*/
				// if better than our current selection
				if( NewMetric > BestMetric ) {
					// target is more in line than current best - see if target is visible
					GetLevel()->SingleLineCheck( Hit, this, next->Location, Pawn->Location + FVector(0,0,Pawn->EyeHeight), TRACE_World|TRACE_StopAtFirstHit );
					if( Hit.Actor ) {
						GetLevel()->SingleLineCheck( Hit, this, next->Location + FVector(0,0,next->EyeHeight), Pawn->Location + FVector(0,0,Pawn->EyeHeight), TRACE_World|TRACE_StopAtFirstHit );
					}

					if( !Hit.Actor ) {
						Pick = next;
						*bestAim_Lat  = targetDot;
						*bestAim_Vert = targetDot;
						*bestDist	  = targetDist;

						BestMetric	 = NewMetric;
					}
				}
			}
		}
	}

	*(APawn**)Result = Pick;
	unguardSlow;
}

/* PickTarget()
Find the best pawn target for this controller to aim at.  Used for autoaiming.
*/
void AController::execPickTarget( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execPickTarget);

	P_GET_FLOAT_REF(bestAim);
	P_GET_FLOAT_REF(bestDist);
	P_GET_VECTOR(FireDir);
	P_GET_VECTOR(projStart);
	P_GET_FLOAT(MaxRange);
	P_FINISH;
	APawn *pick = NULL;
	FLOAT VerticalAim = *bestAim * 3.f - 2.f;
	FCheckResult Hit(1.f);
    MaxRange *= MaxRange;

	for ( AController *next=GetLevel()->GetLevelInfo()->ControllerList; next!=NULL; next=next->nextController )
	{
		// look for best controlled pawn target which is on same team
		if ( (next != this) && next->Pawn && (next->Pawn->Health > 0) && next->Pawn->bProjTarget
			&& (!PlayerReplicationInfo || !next->PlayerReplicationInfo
				|| !Level->Game->bTeamGame
				|| (PlayerReplicationInfo->Team != next->PlayerReplicationInfo->Team)) )
		{
			FVector AimDir = next->Pawn->Location - projStart;
			FLOAT newAim = FireDir | AimDir;
			FVector FireDir2D = FireDir;
			FireDir2D.Z = 0;
			FireDir2D.Normalize();
			FLOAT newAim2D = FireDir2D | AimDir;
			if ( newAim > 0 )
			{
				FLOAT FireDist = AimDir.SizeSquared();
				// only find targets which are < MaxRange units away
				if ( ((FireDist < MaxRange)
					|| (IsAPlayerController() && (FovAngle != ((APlayerController*)this)->DefaultFOV))) )
				{
					FireDist = appSqrt(FireDist);
					newAim = newAim/FireDist;
					if ( newAim > *bestAim )
					{
						// target is more in line than current best - see if target is visible
						GetLevel()->SingleLineCheck( Hit, this, next->Pawn->Location, Pawn->Location + FVector(0,0,Pawn->EyeHeight), TRACE_World|TRACE_StopAtFirstHit );
						if( Hit.Actor )
							GetLevel()->SingleLineCheck( Hit, this, next->Pawn->Location + FVector(0,0,next->Pawn->EyeHeight), Pawn->Location + FVector(0,0,Pawn->EyeHeight), TRACE_World|TRACE_StopAtFirstHit );
						if ( !Hit.Actor )
						{
							pick = next->Pawn;
							*bestAim = newAim;
							*bestDist = FireDist;
						}
					}
					else if ( !pick )
					{
						// no target yet, so be more liberal about up/down error (more vertical autoaim help)
						newAim2D = newAim2D/FireDist;
						if ( (newAim2D > *bestAim) && (newAim > VerticalAim) )
						{
							GetLevel()->SingleLineCheck( Hit, this, next->Pawn->Location, Pawn->Location + FVector(0,0,Pawn->EyeHeight), TRACE_World|TRACE_StopAtFirstHit );
							if( Hit.Actor )
								GetLevel()->SingleLineCheck( Hit, this, next->Pawn->Location + FVector(0,0,next->Pawn->EyeHeight), Pawn->Location + FVector(0,0,Pawn->EyeHeight), TRACE_World|TRACE_StopAtFirstHit );
							if ( !Hit.Actor )
							{
								pick = next->Pawn;
								*bestDist = FireDist;
							}
						}
					}
				}
			}
		}
	}

	*(APawn**)Result = pick;
	unguardSlow;
}

/* PickAnyTarget()
Find the best non-pawn target for this controller to aim at.  Used for autoaiming.
*/
void AController::execPickAnyTarget( FFrame& Stack, RESULT_DECL )
{
	guardSlow(APawn::execPickAnyTarget);

	P_GET_FLOAT_REF(bestAim);
	P_GET_FLOAT_REF(bestDist);
	P_GET_VECTOR(FireDir);
	P_GET_VECTOR(projStart);
	P_FINISH;
	AActor *pick = NULL;

	for( INT iActor=0; iActor<GetLevel()->Actors.Num(); iActor++ )
		if( GetLevel()->Actors(iActor) )
		{
			AActor* next = GetLevel()->Actors(iActor);
			if (next->bProjTarget && !next->IsAPawn())
			{
				FLOAT newAim = FireDir | (next->Location - projStart);
				if ( newAim > 0 )
				{
					FLOAT FireDist = (next->Location - projStart).SizeSquared();
					// only allow targets <2000 units away
					if ( FireDist < 4000000.f )
					{
						FireDist = appSqrt(FireDist);
						newAim = newAim/FireDist;
						// check if target is more in line and visible
						if ( (newAim > *bestAim) && LineOfSightTo(next) )
						{
							pick = next;
							*bestAim = newAim;
							*bestDist = FireDist;
						}
					}
				}
			}
		}

	*(AActor**)Result = pick;
	unguardSlow;
}

/* AddController()
Add a controller to the controller list.  Called when controller is spawned.
*/
void AController::execAddController( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execAddController);

	P_FINISH;

	nextController = Level->ControllerList;
	Level->ControllerList = this;
	unguardSlow;
}

/* RemoveController()
Remove a controller from the controller list.  Called when controller is destroyed
*/
void AController::execRemoveController( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execRemoveController);

	P_FINISH;

	AController *next = Level->ControllerList;
	if ( next == this )
		Level->ControllerList = next->nextController;
	else
	{
		while ( next )
		{
			if ( next->nextController == this )
			{
				next->nextController = nextController;
				break;
			}
			next = next->nextController;
		}
	}

	unguardSlow;
}

/* execWaitForLanding()
wait until physics is not PHYS_Falling
*/
void AController::execWaitForLanding( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execWaitForLanding);

	P_FINISH;

	LatentFloat = 4.f;
	if ( Pawn && (Pawn->Physics == PHYS_Falling) )
		GetStateFrame()->LatentAction = AI_PollWaitForLanding;
	unguardSlow;
}

void AController::execPollWaitForLanding( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execPollWaitForLanding);
	if( Pawn && 
	   (Pawn->Physics != PHYS_Falling &&
	    Pawn->Physics != PHYS_None) )
	{
		GetStateFrame()->LatentAction = 0;
	}
	else
	{
		//epic moelfke: if our pawn has an Anchor that's below, air control towards it
		//helps avoid bot falling to its death when it breaks out of a move while in the air
		if (bPhysicsAirControl && Pawn && Pawn->Anchor && Pawn->Anchor->Location.Z < Pawn->Location.Z)
		{
			Pawn->moveToward(Pawn->Anchor->Location, Pawn->Anchor);
		}
		FLOAT DeltaSeconds = *(FLOAT*)Result;
		LatentFloat -= DeltaSeconds;
		if ( LatentFloat < 0 )
		{
			eventLongFall();
			//epic moelfke: call the event again if we remain falling
			LatentFloat = 1.0f;
		}
	}
	unguardSlow;
}
IMPLEMENT_FUNCTION( AController, AI_PollWaitForLanding, execPollWaitForLanding);

void AController::execPickWallAdjust( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execPickWallAdjust);

	P_GET_VECTOR(HitNormal);
	P_FINISH;
	if ( !Pawn )
		return;
	//debugf(TEXT("Start walladjust at %d"),GetLevel()->FindPathCycles);
	GScriptCycles += appCycles();

	FUNC_CLOCK(FindPathCyc,GStats.DWORDStats(GEngineStats.STATS_Game_FindPathCycles));

	*(DWORD*)Result = Pawn->PickWallAdjust(HitNormal);

	GScriptCycles -= appCycles();
	unguardSlow;
}

/* FindStairRotation()
returns an integer to use as a pitch to orient player view along current ground (flat, up, or down)
*/
void APlayerController::execFindStairRotation( FFrame& Stack, RESULT_DECL )
{
	guardSlow(APlayerController::execFindStairRotation);

	P_GET_FLOAT(deltaTime);
	P_FINISH;

	// only recommend pitch if controller has a pawn, and frame rate isn't ridiculously low

#ifdef __PSX2_EE__
	// On the PS2 the frame rate is never high enough for this.
	*(DWORD*)Result = Rotation.Pitch;
	return;
#else
	if ( !Pawn || (deltaTime > 0.33) )
	{
		*(DWORD*)Result = Rotation.Pitch;
		return;
	}
#endif //__PSX2_EE__

	if (Rotation.Pitch > 32768)
		Rotation.Pitch = (Rotation.Pitch & 65535) - 65536;
	
	FCheckResult Hit(1.f);
	FRotator LookRot = Rotation;
	LookRot.Pitch = 0;
	FVector Dir = LookRot.Vector();
	FVector EyeSpot = Pawn->Location + FVector(0,0,Pawn->BaseEyeHeight);
	FLOAT height = Pawn->CollisionHeight + Pawn->BaseEyeHeight;
	FVector CollisionSlice(Pawn->CollisionRadius,Pawn->CollisionRadius,1.f);

	GetLevel()->SingleLineCheck(Hit, this, EyeSpot + 2 * height * Dir, EyeSpot, TRACE_World, CollisionSlice);
	FLOAT Dist = 2 * height * Hit.Time;
	INT stairRot = 0;
	if (Dist > 0.8 * height)
	{
		FVector Spot = EyeSpot + 0.5 * Dist * Dir;
		FLOAT Down = 3 * height;
		GetLevel()->SingleLineCheck(Hit, this, Spot - FVector(0,0,Down), Spot, TRACE_World, CollisionSlice);
		if (Hit.Time < 1.f)
		{
			FLOAT firstDown = Down * Hit.Time;
			if (firstDown < 0.7f * height - 6.f) // then up or level
			{
				Spot = EyeSpot + Dist * Dir;
				GetLevel()->SingleLineCheck(Hit, this, Spot - FVector(0,0,Down), Spot, TRACE_World, CollisionSlice);
				stairRot = ::Max(0, Rotation.Pitch);
				if ( Down * Hit.Time < firstDown - 10 )
					stairRot = 3600;
			}
			else if  (firstDown > 0.7f * height + 6.f) // then down or level
			{
				GetLevel()->SingleLineCheck(Hit, this, Pawn->Location + 0.9*Dist*Dir, Pawn->Location, TRACE_World|TRACE_StopAtFirstHit);
				if (Hit.Time == 1.f)
				{
					Spot = EyeSpot + Dist * Dir;
					GetLevel()->SingleLineCheck(Hit, this, Spot - FVector(0,0,Down), Spot, TRACE_World, CollisionSlice);
					if (Down * Hit.Time > firstDown + 10)
						stairRot = -4000;
				}
			}
		}
	}
	INT Diff = Abs(Rotation.Pitch - stairRot);
	if( (Diff > 0) && (Level->TimeSeconds - GroundPitchTime > 0.25) )
	{
		FLOAT RotRate = 4;
		if( Diff < 1000 )
			RotRate = 4000/Diff;

		RotRate = ::Min(1.f, RotRate * deltaTime);
		stairRot = appRound(FLOAT(Rotation.Pitch) * (1 - RotRate) + FLOAT(stairRot) * RotRate);
	}
	else
	{
		if ( (Diff < 10) && (Abs(stairRot) < 10) )
			GroundPitchTime = Level->TimeSeconds;
		stairRot = Rotation.Pitch;
	}
	*(DWORD*)Result = stairRot;
	unguardSlow;
}

void AController::execEAdjustJump( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execEAdjustJump);

	P_GET_FLOAT(BaseZ);
	P_GET_FLOAT(XYSpeed);
	P_FINISH;

	if ( Pawn )
		*(FVector*)Result = Pawn->SuggestJumpVelocity(Destination, XYSpeed,BaseZ);
	else
		*(FVector*)Result = FVector(0.f,0.f,0.f);
	unguardSlow;
}

void AController::execactorReachable( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execActorReachable);

	P_GET_ACTOR(actor);
	P_FINISH;

	if ( !actor || !Pawn )
	{
		debugf(NAME_DevPath, TEXT("Warning: No pawn or goal for ActorReachable by %s in %s, %s"), GetName(), GetStateFrame()->Describe(), Stack.Node->GetFullName());
		*(DWORD*)Result = 0;
		return;
	}

	GScriptCycles += appCycles();
	FUNC_CLOCK(FPCyc,GStats.DWORDStats(GEngineStats.STATS_Game_FindPathCycles));

	// check if cached failed reach
	if ( (LastFailedReach == actor) && (FailedReachTime == Level->TimeSeconds)
		&& (FailedReachLocation == Pawn->Location) )
	{
		*(DWORD*)Result = 0;
	}
	else
	{
		INT Reach = Pawn->actorReachable(actor);
		if ( !Reach )
		{
			LastFailedReach = actor;
			FailedReachTime = Level->TimeSeconds;
			FailedReachLocation = Pawn->Location;
		}
		*(DWORD*)Result = Reach;
	}
	//debugf( TEXT("ActorReachable Path time %f for %s to %s physics %d goal physics %d"),GStats.DWORDStats(GEngineStats.STATS_Game_FindPathCycles) * GSecondsPerCycle * 1000.0f,  *PlayerReplicationInfo->PlayerName, actor->GetName(), Pawn->Physics, actor->Physics );
	GScriptCycles -= appCycles();
	unguardSlow;
}

void AController::execpointReachable( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execPointReachable);

	P_GET_VECTOR(point);
	P_FINISH;

	if ( !Pawn )
	{
		debugf(NAME_DevPath,TEXT("Warning: No pawn for pointReachable by %s in %s"),GetName(), GetStateFrame()->Describe() );
		*(DWORD*)Result = 0;
		return;
	}

	GScriptCycles += appCycles();
	FUNC_CLOCK(FPCyc,GStats.DWORDStats(GEngineStats.STATS_Game_FindPathCycles));
	*(DWORD*)Result = Pawn->pointReachable(point);
	GScriptCycles -= appCycles();
	unguardSlow;
}

/* FindPathTo() and FindPathToward()
returns the best pathnode toward a point or actor - even if it is directly reachable
If there is no path, returns None
By default clears paths.  If script wants to preset some path weighting, etc., then
it can explicitly clear paths using execClearPaths before presetting the values and
calling FindPathTo with clearpath = 0
*/
AActor* AController::FindPath(const FVector& point, AActor* goal, UBOOL bWeightDetours)
{
	guard(AController::FindPath);
	
	if ( !Pawn )
	{
		debugf(NAME_DevPath,TEXT("Warning: No pawn for FindPath by %s in %s"),GetName(), GetStateFrame()->Describe() );
		return NULL;
	}
	//debugf(TEXT("%s FindPath"),GetName());
	//debugf(TEXT("Start find path at %d"),GetLevel()->FindPathCycles);
	GScriptCycles += appCycles();
	FUNC_CLOCK(FPCyc,GStats.DWORDStats(GEngineStats.STATS_Game_FindPathCycles));
	LastRouteFind = Level->TimeSeconds;
	AActor * bestPath = NULL;
	PendingMover = NULL;
	bPreparingMove = false;
	if ( Pawn->findPathToward(goal,point,NULL,0.f, bWeightDetours) > 0.f )
		bestPath = SetPath();
/*
	if ( goal )
		debugf( TEXT("At %f FindPath Path time %f for %s to %s"),Level->TimeSeconds,GStats.DWORDStats(GEngineStats.STATS_Game_FindPathCycles) * GSecondsPerCycle * 1000.0f,  *PlayerReplicationInfo->PlayerName,goal->GetName() );
	else
		debugf( TEXT("At %f FindPath Path time %f for %s"),Level->TimeSeconds,GStats.DWORDStats(GEngineStats.STATS_Game_FindPathCycles) * GSecondsPerCycle * 1000.0f,  *PlayerReplicationInfo->PlayerName );
*/
	GScriptCycles -= appCycles();
	//debugf(TEXT("Find path to time was %d"), GetLevel()->FindPathCycles);
	return bestPath;
	unguard;
}

void AController::execFindPathTo( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execFindPathTo);

	P_GET_VECTOR(point);
	P_GET_UBOOL_OPTX(bWeightDetours, false);
	P_FINISH;

	*(AActor**)Result = FindPath(point, NULL, bWeightDetours);
	unguardSlow;
}

void AController::execFindPathToward( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execFindPathToward);

	P_GET_ACTOR(goal);
	P_GET_UBOOL_OPTX(bWeightDetours, false);
	P_FINISH;

	if ( !goal )
	{
		debugf(NAME_DevPath,TEXT("Warning: No goal for FindPathToward by %s in %s"),GetName(), GetStateFrame()->Describe() );
		*(AActor**)Result = NULL;
		return;
	}
	*(AActor**)Result = FindPath(FVector(0,0,0), goal, bWeightDetours);
	unguardSlow;
}

void AController::execFindPathToIntercept( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execFindPathToIntercept);

	P_GET_ACTOR(goal);
	P_GET_ACTOR(OtherRouteGoal);
	P_GET_UBOOL_OPTX(bWeightDetours, false);
	P_FINISH;

    APawn *goalPawn = (goal && goal->IsAPawn()) ? (APawn*)goal : NULL;
	if ( !goalPawn || !Pawn )
	{
		debugf(NAME_DevPath,TEXT("Warning: No goal for FindPathToIntercept by %s in %s"),GetName(), GetStateFrame()->Describe() );
		*(AActor**)Result = NULL;
		return;
	}
	// debugf(TEXT("%s Find path to intercept %s going to %s"),Pawn->GetName(),goal->GetName(),OtherRouteGoal->GetName());
	if ( !Pawn->ValidAnchor() || !goalPawn->Controller || !OtherRouteGoal )
	{
		AActor *ResultPath = FindPath(FVector(0,0,0), goalPawn, bWeightDetours);
		*(AActor**)Result = ResultPath;
		return;
	}
	UBOOL bFindDirectPath = true;
	UBOOL bHumanPathed = false;
	if ( goalPawn->IsHumanControlled() )
	{
		APlayerController *GoalPC = (APlayerController*)goalPawn->Controller;
		if ( GoalPC && (goalPawn->Location != GoalPC->FailedPathStart) )
		{	
			bHumanPathed = (goalPawn->Controller->FindPath(FVector(0.f,0.f,0.f), OtherRouteGoal, false)!= NULL);
			if ( !bHumanPathed )
				GoalPC->FailedPathStart = goalPawn->Location;
		}
	}

	if ( ((goalPawn->Controller->GetStateFrame()->LatentAction == AI_PollMoveToward) || (Level->TimeSeconds - goalPawn->Controller->LastRouteFind < 0.75f))
		|| bHumanPathed )
	{
		// if already on path, movetoward goalPawn
		for (INT i=0; i<16; i++ )
		{
			if ( !goalPawn->Controller->RouteCache[i] )
				break;
			else
			{	
				bFindDirectPath = false;
				if ( goalPawn->Controller->RouteCache[i] == Pawn->Anchor )
				{
//						debugf(TEXT("Already on path"));
					bFindDirectPath = true;
					break;
				}
			}
		}
	}
	AActor *ResultActor = NULL;

	if ( bFindDirectPath )
	{
		ResultActor = FindPath(FVector(0.f,0.f,0.f), goalPawn, bWeightDetours);
	}
	else
	{
		//epic moelfke: only check MoveTarget if the pawn is currently in MoveToward()
		if (goalPawn->Controller->GetStateFrame()->LatentAction == AI_PollMoveToward)
		{
			ANavigationPoint* Nav = Cast<ANavigationPoint>(goalPawn->Controller->MoveTarget);
			if (Nav)
			{
				Nav->bTransientEndPoint = true;
			}
		}
		for (INT i=0; i<16; i++ )
		{
			ANavigationPoint* Nav = Cast<ANavigationPoint>(goalPawn->Controller->RouteCache[i]);
			if ( Nav )
			{
				Nav->bTransientEndPoint = true;
//					debugf(TEXT("Mark %s"),Nav->GetName());
			}
			else if ( !goalPawn->Controller->RouteCache[i] )
				break;
		}
		ResultActor = FindPath(FVector(0.f,0.f,0.f), goalPawn, bWeightDetours);
	}
	*(AActor**)Result = ResultActor;
	unguardSlow;
}

void AController::execFindPathTowardNearest( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execFindPathTowardNearest);

	P_GET_OBJECT(UClass,GoalClass);
	P_GET_UBOOL_OPTX(bWeightDetours, false);
	P_FINISH;

	if ( !GoalClass || !Pawn )
	{
		debugf(NAME_DevPath,TEXT("Warning: No goal for FindPathTowardNearest by %s in %s"),GetName(), GetStateFrame()->Describe() );
		*(AActor**)Result = NULL;
		return;
	}
	ANavigationPoint* Found = NULL;

	// mark appropriate Navigation points
	for ( ANavigationPoint* Nav=Level->NavigationPointList; Nav; Nav=Nav->nextNavigationPoint )
		if ( Nav->GetClass() == GoalClass )
		{
			Nav->bTransientEndPoint = true;
			Found = Nav;
		}
	if ( Found )
		*(AActor**)Result = FindPath(FVector(0,0,0), Found, bWeightDetours);
	else
		*(AActor**)Result = NULL;
	unguardSlow;
}

/* FindRandomDest()
returns a random pathnode which is reachable from the creature's location.  Note that the path to
this destination is in the RouteCache.
*/
void AController::execFindRandomDest( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execFindPathTo);

	P_FINISH;

	if ( !Pawn )
		return;

	GScriptCycles += appCycles();
	FUNC_CLOCK(FPCyc,GStats.DWORDStats(GEngineStats.STATS_Game_FindPathCycles));
	ANavigationPoint * bestPath = NULL;
	PendingMover = NULL;
	bPreparingMove = false;
	if ( Pawn->findPathToward(NULL,FVector(0,0,0),&FindRandomPath,0.f,false) > 0 )
		bestPath = Cast<ANavigationPoint>(RouteGoal);

	//debugf( TEXT("FindRandomDest Path time %f for %s"),GStats.DWORDStats(GEngineStats.STATS_Game_FindPathCycles) * GSecondsPerCycle * 1000.0f,  *PlayerReplicationInfo->PlayerName );
	GScriptCycles -= appCycles();

	*(ANavigationPoint**)Result = bestPath;
	unguardSlow;
}

/* epic ===============================================
* ::execFindLeastVisiblePointNear
* Author: moelfke
*
* Find the NavigationPoint with the lowest value for its
* Visibility property within MaxDist path distance
* of the NavigationPoint specified by ObjectiveIndex, which
* is an index into all NavigationPoints' ObjectiveDist array,
* that must have been set previously by a call to that point's
* SetObjectiveDistance() function. See its declaration
* in NavigationPoint.uc for more information.
*
* NOTE: if there are multiple points within range
* that share the lowest Visibility value, the one closest
* to the objective will be selected.
*
* NOTE: This function does not find a path toward the
* chosen point; use FindPathToward() for that.
*
* =====================================================
*/
void AController::execFindLeastVisiblePointNear(FFrame& Stack, RESULT_DECL)
{
	guard(AController::execFindLeastVisiblePointNear);

	P_GET_INT(ObjectiveIndex);
	P_GET_FLOAT(MaxDist);
	P_FINISH;

	GScriptCycles += appCycles();
	CLOCK(FindPathCyc,GStats.DWORDStats(GEngineStats.STATS_Game_FindPathCycles));

	ANavigationPoint* Best = NULL;
	for (ANavigationPoint* N = Level->NavigationPointList; N; N = N->nextNavigationPoint)
	{
		// if we have no best, or this node is less visible than the best, or half the time if it has the same visibility as the best
		if (!Best || N->Visibility < Best->Visibility || (N->Visibility == Best->Visibility && appFrand() < 0.5f))
		{
			// and it's inside the max distance
			if (N->ObjectiveDist.Num() > ObjectiveIndex && N->ObjectiveDist(ObjectiveIndex) <= MaxDist)
			{
				// then it is our new best
				Best = N;
			}
		}
	}

	*(ANavigationPoint**)Result = Best;

	GScriptCycles -= appCycles();
	UNCLOCK();

	unguardexec;
}

/* epic ===============================================
* ::execRouteActors
* Author: moelfke
*
* Returns all actors in the RouteCache array that are of
* the specified class or a subclass
*
* =====================================================
*/
void AController::execRouteActors(FFrame& Stack, RESULT_DECL)
{
	guard(AController::execRouteActors);

	P_GET_OBJECT(UClass, BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_FINISH;

	// make sure we have a valid class
	if (!BaseClass)
	{
		BaseClass = AActor::StaticClass();
	}
	
	INT Index = 0;
	PRE_ITERATOR;
		// get the next actor in the iteration
		*OutActor = NULL;
		while (Index < ARRAY_COUNT(RouteCache) && RouteCache[Index] && *OutActor == NULL)
		{
			if (RouteCache[Index]->IsA(BaseClass))
			{
				*OutActor = RouteCache[Index];
			}
			Index++;
		}
		if (*OutActor == NULL)
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

	unguardexec;
}

void AController::execLineOfSightTo( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execLineOfSightTo);

	P_GET_ACTOR(Other);
	P_FINISH;

	*(DWORD*)Result = LineOfSightTo(Other);
	unguardSlow;
}

void AAIController::execWaitToSeeEnemy( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AAIController::execWaitToSeeEnemy);

	P_GET_VECTOR(dest);
	P_GET_ACTOR_OPTX(viewfocus, NULL);
	P_GET_FLOAT_OPTX(speed, 1.f);
	P_FINISH;

	if ( !Pawn || !Enemy )
		return;
	Focus = Enemy;
	GetStateFrame()->LatentAction = AI_PollWaitToSeeEnemy;
	unguardSlow;
}

void AAIController::execPollWaitToSeeEnemy( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AAIController::execPollWaitToSeeEnemy);
	if( !Pawn || !Enemy )
	{
		GetStateFrame()->LatentAction = 0;
		return;
	}
	if ( Level->TimeSeconds - LastSeenTime > 0.1f )
		return;
	//check if facing enemy
	UBOOL success = (Abs(Pawn->DesiredRotation.Yaw - (Pawn->Rotation.Yaw & 65535)) < 2000);
	if (!success) //check if on opposite sides of zero
		success = (Abs(Pawn->DesiredRotation.Yaw - (Pawn->Rotation.Yaw & 65535)) > 63535);
	if ( !success )
		return;
	GetStateFrame()->LatentAction = 0;
	unguardSlow;
}
IMPLEMENT_FUNCTION( AAIController, AI_PollWaitToSeeEnemy, execPollWaitToSeeEnemy);

/* execMoveTo()
start moving to a point -does not use routing
Destination is set to a point
*/
void AController::execMoveTo( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execMoveTo);

	P_GET_VECTOR(dest);
	P_GET_ACTOR_OPTX(viewfocus, NULL);
	P_GET_UBOOL_OPTX(bShouldWalk, false);
	P_FINISH;

	if ( !Pawn )
		return;

	FVector MoveDir = dest - Pawn->Location;
	FLOAT MoveSize = MoveDir.Size();
	MoveTarget = NULL;
	Pawn->bReducedSpeed = false;
	Pawn->DesiredSpeed = Pawn->MaxDesiredSpeed;
	Pawn->DestinationOffset = 0.f;
	Pawn->NextPathRadius = 0.f;
	Focus = viewfocus;
	Pawn->setMoveTimer(MoveSize);
	GetStateFrame()->LatentAction = AI_PollMoveTo;
	Destination = dest;
	if ( !Focus )
		FocalPoint = Destination;
	bAdjusting = false;
	CurrentPath = NULL;
	Pawn->ClearSerpentine();
	AdjustLoc = Destination;
	bAdvancedTactics = false;
	Pawn->moveToward(Destination, NULL);
	unguardSlow;
}

void AController::execPollMoveTo( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execPollMoveTo);
	if( !Pawn || ((MoveTimer < 0.f) && (Pawn->Physics != PHYS_Falling)) )
	{
		GetStateFrame()->LatentAction = 0;
		return;
	}
	if ( bAdjusting )
		bAdjusting = !Pawn->moveToward(AdjustLoc, NULL);
	if( !bAdjusting && (!Pawn || Pawn->moveToward(Destination, NULL)) )
		GetStateFrame()->LatentAction = 0;
	else
		CheckFears();
	unguardSlow;
}
IMPLEMENT_FUNCTION( AController, AI_PollMoveTo, execPollMoveTo);

/* CheckFears()
Adjust pawn movement to avoid active FearSpots
*/
void AController::CheckFears()
{
	guard(AController::CheckFears);

	if ( Pawn->Acceleration.IsZero() )
		return;

	FVector FearAdjust(0.f,0.f,0.f);

	// For each fear slot
	for( INT i = 0; i < 2; i++ )
	{
		// If the slot is filled
		if( FearSpots[i] )
		{
			// If spot has been deleted - Empty slot
			if ( FearSpots[i]->bDeleteMe )
			{
				FearSpots[i] = NULL;
			}
			else if( (Square(Pawn->Location.Z - FearSpots[i]->Location.Z) > Square(Pawn->CollisionHeight + FearSpots[i]->CollisionHeight))
					||	(Square(Pawn->Location.X - FearSpots[i]->Location.X) + Square(Pawn->Location.Y - FearSpots[i]->Location.Y)
						> Square(Pawn->CollisionRadius + FearSpots[i]->CollisionRadius)) )
			{
				// Otherwise, if pawn is no longer in contact with fear spot - empty slot
				FearSpots[i] = NULL;
			}
			else
			{
				// Adjust away from fear spot (roughly normalized)
				FearAdjust += (Pawn->Location - FearSpots[i]->Location)/FearSpots[i]->CollisionRadius;
			}
		}
	}

	// If no adjustments to make - exit
	if ( FearAdjust.IsZero() )
	{
		return;
	}

	// Get pawn accel direction and speed (preserved after changing direction)
	FearAdjust.Normalize();
	FLOAT	PawnAccelRate	= Pawn->Acceleration.Size();
	FVector PawnDir			= Pawn->Acceleration/PawnAccelRate;

	// If pawn is already moving in the direction of the adjustment (away from fear locations) - exit
	if ( (FearAdjust | PawnDir) > 0.7f )
	{
		return;
	}

	// If pawn is moving into the fear spot
	if ( (FearAdjust | PawnDir) < -0.7f )
	{
		// Get lateral direction
		FVector LeftDir = PawnDir ^ FVector(0.f,0.f,1.f);	
		LeftDir = LeftDir.SafeNormal();

		// Update adjustment along this line
		FearAdjust = 2.f * LeftDir;

		// Flip to right direction if on right side
		if ( (LeftDir | FearAdjust) < 0.f )
			FearAdjust *= -1.f;
	}

	// Adjust pawn acceleartion
	Pawn->Acceleration = (PawnDir + FearAdjust).SafeNormal();
	Pawn->Acceleration *= PawnAccelRate;

	unguard;
}

/* execInLatentExecution()
returns true if controller currently performing latent execution with
passed in LatentAction value
*/
void AController::execInLatentExecution( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execInLatentExecution);
	P_GET_INT(LatentActionNumber);
	P_FINISH;

	*(DWORD*)Result = ( GetStateFrame()->LatentAction == LatentActionNumber );
	unguardSlow;
}

/* execMoveToward()
start moving toward a goal actor -does not use routing
MoveTarget is set to goal
*/
void AController::execMoveToward( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execMoveToward);

	P_GET_ACTOR(goal);
	P_GET_ACTOR_OPTX(viewfocus, goal);
	P_GET_FLOAT_OPTX(DesiredOffset,0.f);
	P_GET_UBOOL_OPTX(bStrafe, false);
	P_GET_UBOOL_OPTX(bShouldWalk, false);
	P_FINISH;

	if ( !goal || !Pawn )
	{
//		debugf( TEXT("MoveToward with no goal - goal: %s pawn: %s"), goal->GetFullName(), Pawn->GetFullName() );
		return;
	}

	//epic superville: Tell path manager that want to claim this goal
	if( GetLevel()->GetLevelInfo()->Game->PathManager )
	{
		// PathManager will return proper goal (either the one given, or a better one)
		goal = GetLevel()->GetLevelInfo()->Game->PathManager->eventClaimTarget( goal, this );
	}

	FVector Move = goal->Location - Pawn->Location;	
	Pawn->bReducedSpeed = false;
	Pawn->DesiredSpeed = Pawn->MaxDesiredSpeed;
	MoveTarget = goal;
	Focus = viewfocus;
	if (goal->IsAPawn())
	{
		MoveTimer = 1.2f; //max before re-assess movetoward
	}
	else
	{
		FLOAT MoveSize = Move.Size();
		Pawn->setMoveTimer(MoveSize);
	}
	Destination = MoveTarget->Location;
	GetStateFrame()->LatentAction = AI_PollMoveToward;
	bAdjusting = false;
	AdjustLoc = Destination;
	Pawn->ClearSerpentine();
	bAdvancedTactics = bStrafe && ( (Level->NetMode != NM_Standalone) || (Level->TimeSeconds - Pawn->LastRenderTime < 5.f) || bSoaking );

	// if necessary, allow the pawn to prepare for this move
	// give pawn the opportunity if its a navigation network move,
	// based on the reachspec
	ANavigationPoint *NavGoal = Cast<ANavigationPoint>(goal);

	FLOAT NewDestinationOffset = 0.f;
	CurrentPath = NULL;
	if ( NavGoal )
	{
		if ( NavGoal->bSpecialMove )
			NavGoal->eventSuggestMovePreparation(Pawn);
		//epic moelfke: always use Anchor if it was found this tick (i.e., by APawn::findPathToward())
		//this fixes the case where findPathToward() selects an Anchor that Pawn has not completely reached,
		//but the path from Anchor to the next point on the path requires special movement (bSpecialForced is true)
		//without this, CurrentPath isn't found and the bot incorrectly tries to run straight there
		if ((Pawn->Anchor == Pawn->LastAnchor && Pawn->LastValidAnchorTime == Level->TimeSeconds) || Pawn->ValidAnchor())
		{
			// if the reachspec isn't currently supported by the pawn
			// then give the pawn an opportunity to perform some latent preparation
			// (Controller will set its bPreparingMove=true if it needs latent preparation)
			CurrentPath = Pawn->Anchor->GetReachSpecTo(NavGoal);
			if ( CurrentPath )
			{
				if ( (CurrentPath->reachFlags & R_FORCED) && NavGoal->bSpecialForced )
					NavGoal->eventSuggestMovePreparation(Pawn);
				else if ( !CurrentPath->supports(Pawn->CollisionRadius,Pawn->CollisionHeight,Pawn->calcMoveFlags(),Pawn->MaxFallSpeed) )
					eventPrepareForMove(NavGoal, CurrentPath);
			    CurrentPathDir = CurrentPath->End->Location - CurrentPath->Start->Location;
			    CurrentPathDir = CurrentPathDir.SafeNormal();
			}
		}

		if ( !NavGoal->bNeverUseStrafing && !NavGoal->bForceNoStrafing )
		{
			if ( CurrentPath )
			{
				// round corners smoothly
				// start serpentine dir in current direction
				Pawn->SerpentineTime = 0.f;
				Pawn->SerpentineDir = Pawn->Velocity.SafeNormal();
				Pawn->SerpentineDist = Clamp(CurrentPath->CollisionRadius - Pawn->CollisionRadius,0.f,4.f * Pawn->CollisionRadius)
										* (0.5f + 1.f * appFrand());
			    FLOAT DP = CurrentPathDir | Pawn->SerpentineDir;
				FLOAT DistModifier = 1.f - DP*DP*DP*DP;
				if ( (DP < 0) && (DistModifier < 0.5f) )
					Pawn->SerpentineTime = 0.8f;
				else
					Pawn->SerpentineDist *= DistModifier;
			}
			if ( NavGoal != RouteGoal )
				NewDestinationOffset = (0.7f + 0.3f * appFrand()) * ::Max(0.f, Pawn->NextPathRadius - Pawn->CollisionRadius);
		}
	}
	Pawn->DestinationOffset = (DesiredOffset == 0.f) ? NewDestinationOffset : DesiredOffset;
	Pawn->NextPathRadius = 0.f;
	if ( !bPreparingMove )
		Pawn->moveToward(Destination, MoveTarget);
	unguardSlow;
}

void AController::execPollMoveToward( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execPollMoveToward);

	if( !MoveTarget || !Pawn || ((MoveTimer < 0.f) && (Pawn->Physics != PHYS_Falling)) )
	{
/*		if ( (MoveTimer < 0.f) && MoveTarget && PlayerReplicationInfo )
		{
			debugf(TEXT("%s movetimer %f moving toward %s"),*(PlayerReplicationInfo->PlayerName),MoveTimer,MoveTarget->GetName());
			if ( Pawn )
				debugf(TEXT("distance remaining %f"),(Pawn->Location - MoveTarget->Location).Size());
		}
*/
		//Stack.Log("MoveTarget cleared during movetoward");

		//epic superville: Tell path manager that we no longer claim this node
		if( GetLevel()->GetLevelInfo()->Game->PathManager )
		{
			GetLevel()->GetLevelInfo()->Game->PathManager->eventReleaseTarget( MoveTarget, this );
		}

		GetStateFrame()->LatentAction = 0;
		return;
	}
	// check that pawn is ready to go
	if ( bPreparingMove )
		return;
	// check if adjusting around an obstacle
	if ( bAdjusting )
		bAdjusting = !Pawn->moveToward(AdjustLoc, MoveTarget);
	if ( !MoveTarget || !Pawn )
	{
		//epic superville: Tell path manager that we no longer claim this node
		if( GetLevel()->GetLevelInfo()->Game->PathManager )
		{
			GetLevel()->GetLevelInfo()->Game->PathManager->eventReleaseTarget( MoveTarget, this );
		}

		GetStateFrame()->LatentAction = 0;
		return;
	}
	if ( !bAdjusting )
	{
		// set destination to current movetarget location
		Destination = MoveTarget->Location;
		if( Pawn->Physics==PHYS_Flying )
		{
			if (MoveTarget->IsAPawn())
			{
				Destination.Z += 0.7 * MoveTarget->CollisionHeight;
			}
			else if (MoveTarget->IsA(ANavigationPoint::StaticClass()) )
			{
				if ( MoveTarget->IsA(ALiftExit::StaticClass()) && CurrentPath && CurrentPath->Start->IsA(ALiftCenter::StaticClass()) )
				{
					Destination = Pawn->Location;
					Destination.Z = MoveTarget->Location.Z;
				}
				else if ( Pawn->Location.Z < Destination.Z )
					Destination.Z = Destination.Z + 500.f;
			}
		}

		FLOAT oldDesiredSpeed = Pawn->DesiredSpeed;
		FVector CurrentDest = Destination;

		// move to movetarget
		if( Pawn->moveToward(CurrentDest, MoveTarget) )
		{
			//epic superville: Tell path manager that we no longer claim this node
			if( GetLevel()->GetLevelInfo()->Game->PathManager )
			{
				GetLevel()->GetLevelInfo()->Game->PathManager->eventReleaseTarget( MoveTarget, this );
			}

			GetStateFrame()->LatentAction = 0;
		}
		else if ( MoveTarget && Pawn && (Pawn->Physics == PHYS_Walking) )
		{
			FVector Diff = Pawn->Location - Destination;
			FLOAT DiffZ = Diff.Z;
			Diff.Z = 0.f;
			// reduce timer if seem to be stuck above or below
			if ( Diff.SizeSquared() < Pawn->CollisionRadius * Pawn->CollisionRadius )
			{
				MoveTimer -= Pawn->AvgPhysicsTime;
				if ( DiffZ > Pawn->CollisionRadius + 2 * UCONST_MAXSTEPHEIGHT )
				{
					// check if visible below
					FCheckResult Hit(1.f);
					GetLevel()->SingleLineCheck(Hit, Pawn, Destination, Pawn->Location, TRACE_World|TRACE_StopAtFirstHit);
					if ( (Hit.Time < 1.f) && (Hit.Actor != MoveTarget) )
					{
						//epic superville: Tell path manager that we no longer claim this node
						if( MoveTarget && GetLevel()->GetLevelInfo()->Game->PathManager )
						{
							GetLevel()->GetLevelInfo()->Game->PathManager->eventReleaseTarget( MoveTarget, this );
						}

						GetStateFrame()->LatentAction = 0;
					}
				}
			}
		}
		if ( !MoveTarget || !Pawn )
		{
			//epic superville: Tell path manager that we no longer claim this node
			if( MoveTarget && GetLevel()->GetLevelInfo()->Game->PathManager )
			{
				GetLevel()->GetLevelInfo()->Game->PathManager->eventReleaseTarget( MoveTarget, this );
			}

			GetStateFrame()->LatentAction = 0;
			return;
		}
		if ( GetStateFrame()->LatentAction != 0 )
			CheckFears();

		Destination = MoveTarget->Location;
		if (MoveTarget->IsAPawn())
		{
			Pawn->DesiredSpeed = oldDesiredSpeed; //don't slow down when moving toward a pawn
			if ( MoveTarget->PhysicsVolume->bWaterVolume )
				MoveTimer = -1.f; //give up
		}
	}
	unguardSlow;
}
IMPLEMENT_FUNCTION( AController, AI_PollMoveToward, execPollMoveToward);

/* execTurnToward()
turn toward Focus
*/
void AController::execFinishRotation( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execFinishRotation);

	P_FINISH;

	GetStateFrame()->LatentAction = AI_PollFinishRotation;
	unguardSlow;
}

void AController::execPollFinishRotation( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AController::execPollFinishRotation);

	if( !Pawn )
	{
		GetStateFrame()->LatentAction = 0;
		return;
	}

	//only base success on Yaw
	int success = (Abs(Pawn->DesiredRotation.Yaw - (Pawn->Rotation.Yaw & 65535)) < 2000);
	if (!success) //check if on opposite sides of zero
		success = (Abs(Pawn->DesiredRotation.Yaw - (Pawn->Rotation.Yaw & 65535)) > 63535);	
	if( success )
		GetStateFrame()->LatentAction = 0;

	unguardSlow;
}
IMPLEMENT_FUNCTION( AController, AI_PollFinishRotation, execPollFinishRotation);

/*
SeePawn()

returns true if Other was seen by this controller's pawn.  Chance of seeing other pawn decreases with increasing
distance or angle in peripheral vision
*/
DWORD AController::SeeActor( AActor* Other, UBOOL bMaySkipChecks)
{
	guard(AController::SeeActor);
	if ( !Other || !Pawn )
		return 0;

	if( Other != Enemy )
		bLOSflag = !bLOSflag;

	FLOAT maxdist = Pawn->SightRadius * Min(1.f, Other->IsAPawn() ? ((FLOAT)((APawn*)Other)->Visibility * 0.0078125f) : 1.f); // * 1/128

	// fixed max sight distance
	if ( (Other->Location - Pawn->Location).SizeSquared() > maxdist * maxdist )
		return 0;

	FLOAT dist = (Other->Location - Pawn->Location).Size();

	// may skip if more than 1/5 of maxdist away (longer time to acquire)
	if ( bMaySkipChecks && (appFrand() * dist > 0.1f * maxdist) )
			return 0;

	// check field of view
	FVector SightDir = (Other->Location - Pawn->Location).SafeNormal();
	FVector LookDir = Rotation.Vector();
	Stimulus = (SightDir | LookDir);
	if ( Stimulus < Pawn->PeripheralVision )
		return 0;

	// need to make this only have effect at edge of vision
	//if ( bMaySkipChecks && (appFrand() * (1.f - Pawn->PeripheralVision) < 1.f - Stimulus) )
	//	return 0;
	if ( bMaySkipChecks && (appFrand() * dist > 0.1f * maxdist) )
	{
		// lower FOV vertically
		SightDir.Z *= 2.f;
		SightDir.Normalize();
		if ( (SightDir | LookDir) < Pawn->PeripheralVision )
			return 0;

		// notice other pawns at very different heights more slowly
		FLOAT heightMod = Abs(Other->Location.Z - Pawn->Location.Z);
		if ( appFrand() * dist < heightMod )
			return 0;
	}

	Stimulus = 1;
	return LineOfSightTo(Other, bMaySkipChecks);

	unguard;
}

AActor* AController::GetViewTarget()
{
	guard(AController::GetViewTarget);

	if ( Pawn )
		return Pawn;
	return this;
	unguard;
}

AActor* APlayerController::GetViewTarget()
{
	guard(APlayerController::GetViewTarget);

	if ( RealViewTarget )
	{
		AActor *OldViewTarget = ViewTarget;
		if ( RealViewTarget->Pawn )
		{
			if ( RealViewTarget->Pawn->bDeleteMe )
				ViewTarget = RealViewTarget;
			else
				ViewTarget = RealViewTarget->Pawn;
		}
		else if (RealViewTarget->IsAPlayerController() && ((APlayerController*)RealViewTarget)->ViewTarget)
			ViewTarget = ((APlayerController*)RealViewTarget)->ViewTarget;
		else if ( !ViewTarget )
			ViewTarget = RealViewTarget;
		if ( (OldViewTarget != ViewTarget)
			&& !LocalPlayerController() )
		{
			eventClientSetViewTarget(ViewTarget);
		}
	}
	else if ( !ViewTarget )
	{
		if ( Pawn && !Pawn->bDeleteMe && !Pawn->bPendingDelete )
			ViewTarget = Pawn;
		else
			ViewTarget = this;
	}
	return ViewTarget;
	unguard;
}

// Converts a world location in to Screen X/Y coordinates

void APlayerController::execWorldToScreen( FFrame& Stack, RESULT_DECL )
{ 
	guard(APlayerController::execWorldToScreen);

	P_GET_VECTOR(InLocation);							// Location holds the vector to project
	//scion rwd - adjust the matrix
	P_GET_VECTOR_OPTX(newScale,FVector(1,1,1));
	P_GET_ROTATOR_OPTX(newRotation,FRotator(0,0,0));
	P_FINISH;

	UViewport* View = Cast<UViewport>( Player );
	if (!View)
	{
		check( XLevel && XLevel->Engine && XLevel->Engine->Client );
		View = XLevel->Engine->Client->Viewports(0);
	}

	APlayerController*	CameraActor = this;
	AActor*		ViewActor = NULL;
	CameraActor->eventPlayerCalcView( 0.f, ViewActor, CameraActor->Location, CameraActor->Rotation, 0 );

	FCameraSceneNode	SceneNode(View,&View->RenderTarget,ViewActor, CameraActor->Location, CameraActor->Rotation,CameraActor->FovAngle);
	FCanvasUtil			CanvasUtil(&View->RenderTarget,View->RI);
	
#if 0
	INT iSizeX,iSizeY;
	XLevel->Engine->Client->GetInitScreenSize( iSizeX, iSizeY );
	FLOAT	SizeX = iSizeX,
			SizeY = iSizeY;
#else
	FLOAT	SizeX = View->SizeX,
			SizeY = View->SizeY;
#endif

	//scion rwd - adjust the matrix
	CanvasUtil.CanvasToScreen = (FRotationMatrix(newRotation) *
		FTranslationMatrix(FVector(-SizeX / 2.0f - 0.5f,-SizeY / 2.0f - 0.5f,0.0f)) *
		FScaleMatrix(FVector(2.0f / SizeX,-2.0f / SizeY,1.0f) * newScale));
	CanvasUtil.ScreenToCanvas = CanvasUtil.CanvasToScreen.Inverse();

	*(FVector*)Result = CanvasUtil.ScreenToCanvas.TransformFVector(SceneNode.Project(InLocation));


	unguard;
}

// Translate X/Y screen coordinates in to a world vector

void APlayerController::execScreenToWorld( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerController::execWorldToScreen);

	P_GET_VECTOR(InLocation);	// Location holds the X/Y coords to Deporject
	P_FINISH;

	UViewport* View = Cast<UViewport>( Player );
	if (!View)
	{
		check( XLevel && XLevel->Engine && XLevel->Engine->Client );
		View = XLevel->Engine->Client->Viewports(0);
	}

	APlayerController*	CameraActor = this;
	AActor*				ViewActor = NULL;

	CameraActor->eventPlayerCalcView( 0.f, ViewActor, CameraActor->Location, CameraActor->Rotation, 0 );

	FCameraSceneNode	SceneNode(View,&View->RenderTarget,ViewActor, CameraActor->Location, CameraActor->Rotation, CameraActor->FovAngle);
	FCanvasUtil			CanvasUtil(&View->RenderTarget,View->RI);

	FPlane	P(CanvasUtil.CanvasToScreen.TransformFVector(InLocation),NEAR_CLIPPING_PLANE);
	FVector Direction = SceneNode.Deproject(P);
	Direction = Direction - CameraActor->Location;
	Direction.SafeNormal();
	*(FVector*)Result = Direction;

	unguard;
}

/*
LineOfSightTo()
returns true if controller's pawn can see Other actor.
Checks line to center of other actor, and possibly to head or box edges depending on distance
*/
DWORD AController::LineOfSightTo(AActor *Other, INT bUseLOSFlag)
{
	guard(AController::LineOfSightTo);
	if ( !Other )
		return 0;

	AActor* ViewTarg = GetViewTarget();

	FVector ViewPoint = ViewTarg->Location;
	if ( ViewTarg == Pawn )
		ViewPoint.Z += Pawn->BaseEyeHeight; //look from eyes

	FCheckResult Hit(1.f);
	if (Other == Enemy)
	{
		GetLevel()->SingleLineCheck( Hit, this, Other->Location, ViewPoint, TRACE_World|TRACE_StopAtFirstHit );
		if ( Hit.Actor && (Hit.Actor != Other) )
			GetLevel()->SingleLineCheck( Hit, this, Enemy->Location + FVector(0,0,Enemy->BaseEyeHeight), ViewPoint, TRACE_World|TRACE_StopAtFirstHit );

		if ( !Hit.Actor || (Hit.Actor == Other) )
		{
			// update enemy info
			// NOTE that controllers update this info even if the enemy is behind them
			LastSeenTime = Level->TimeSeconds;
			LastSeeingPos = GetViewTarget()->Location;
			LastSeenPos = Enemy->Location;
			bEnemyInfoValid = true;
			return 1;
		}
		// only check sides if width of other is significant compared to distance
		if ( Other->CollisionRadius * Other->CollisionRadius/(Other->Location - ViewTarg->Location).SizeSquared() < 0.0001f )
			return 0;
	}
	else
	{
		GetLevel()->SingleLineCheck( Hit, this, Other->Location, ViewPoint, TRACE_World|TRACE_StopAtFirstHit );
		if ( !Hit.Actor || (Hit.Actor == Other) )
			return 1;

		FLOAT distSq = (Other->Location - ViewTarg->Location).SizeSquared();
		if ( distSq > 64000000.f )
			return 0;
		if ( (!bIsPlayer || !Other->IsAPawn()) && (distSq > 4000000.f) )
			return 0;
		
		//try viewpoint to head
		if ( !bUseLOSFlag || !bLOSflag )
		{
			GetLevel()->SingleLineCheck( Hit, this, Other->Location + FVector(0,0,Other->CollisionHeight * 0.8), ViewPoint, TRACE_World|TRACE_StopAtFirstHit );
			if ( !Hit.Actor || (Hit.Actor == Other) )
				return 1;
		}

		// bLOSFlag used by SeeActor to reduce visibility checks
		if ( bUseLOSFlag && !bLOSflag )
			return 0;
		// only check sides if width of other is significant compared to distance
		if ( Other->CollisionRadius * Other->CollisionRadius/distSq < 0.00015f )
			return 0;
	}

	//try checking sides - look at dist to four side points, and cull furthest and closest
	FVector Points[4];
	Points[0] = Other->Location - FVector(Other->CollisionRadius, -1 * Other->CollisionRadius, 0);
	Points[1] = Other->Location + FVector(Other->CollisionRadius, Other->CollisionRadius, 0);
	Points[2] = Other->Location - FVector(Other->CollisionRadius, Other->CollisionRadius, 0);
	Points[3] = Other->Location + FVector(Other->CollisionRadius, -1 * Other->CollisionRadius, 0);
	int imin = 0;
	int imax = 0;
	FLOAT currentmin = Points[0].SizeSquared();
	FLOAT currentmax = currentmin;
	for ( INT i=1; i<4; i++ )
	{
		FLOAT nextsize = Points[i].SizeSquared();
		if (nextsize > currentmax)
		{
			currentmax = nextsize;
			imax = i;
		}
		else if (nextsize < currentmin)
		{
			currentmin = nextsize;
			imin = i;
		}
	}

	for ( INT i=0; i<3; i++ )
		if	( (i != imin) && (i != imax) )
		{
			GetLevel()->SingleLineCheck( Hit, this, Points[i], ViewPoint, TRACE_World|TRACE_StopAtFirstHit );
			if ( !Hit.Actor || (Hit.Actor == Other) )
				return 1;
		}
	return 0;
	unguard;
}

/* CanHear()

Returns 1 if controller can hear this noise
Several types of hearing are supported

Noises must be perceptible (based on distance, loudness, and the alerntess of the controller

  Options for hearing are: (assuming the noise is perceptible

  bSameZoneHearing = Hear any perceptible noise made in the same zone
  bAdjacentZoneHearing = Hear any perceptible noise made in the same or an adjacent zone
  bLOSHearing = Hear any perceptible noise which is not blocked by geometry
  bAroundCornerHearing = Hear any noise around one corner (bLOSHearing must also be true)

*/
INT AController::CanHear(const FVector& NoiseLoc, FLOAT Loudness, AActor *Other)
{
	guard(AController::CanHear);

	if ( bUsePlayerHearing || !Other->Instigator || !Other->Instigator->Controller || !Pawn )
		return 0; //ignore sounds from uncontrolled (dead) pawns, or if don't have a pawn to control

	FLOAT DistSq = (Pawn->Location - NoiseLoc).SizeSquared();
	FLOAT Perceived = Loudness * Pawn->HearingThreshold * Pawn->HearingThreshold;

	// take pawn alertness into account (it ranges from -1 to 1 normally)
	Perceived *= ::Max(0.f,(Pawn->Alertness + 1.f));

	// check if sound is too quiet to hear
	if ( Perceived < DistSq )
		return 0;

	// check if in same zone
	if ( (Pawn->bSameZoneHearing || Pawn->bAdjacentZoneHearing) && (Pawn->Region.Zone == Other->Region.Zone) )
		return 1;

	if ( !Pawn->bLOSHearing )
		return 0;

	// check if Line of Sight
	// NOTE - still using FastTrace, so terrain and static meshes won't block
	FVector ViewLoc = Pawn->Location + FVector(0,0,Pawn->BaseEyeHeight);
	FCheckResult Hit(1.f);
	GetLevel()->SingleLineCheck(Hit, this, NoiseLoc, ViewLoc, TRACE_Level);
	if ( Hit.Time == 1.f )
		return 1;

	if ( Pawn->bMuffledHearing )
	{
		// sound distance increased to double plus 4x the distance through BSP walls
		if ( Perceived > 4 * DistSq )
		{
			// check dist inside of walls
			FVector FirstHit = Hit.Location;
			GetLevel()->SingleLineCheck(Hit, this, ViewLoc, NoiseLoc, TRACE_Level);
			FLOAT WallDistSq = (FirstHit - Hit.Location).SizeSquared();

			if ( Perceived > 4 * DistSq + WallDistSq * WallDistSq )
				return 1;
		}
	}

	if ( !Pawn->bAroundCornerHearing )
		return 0;

	// check if around corner
	// using navigation network
	Perceived *= 0.125f; // distance to corner must be < 0.7 * max distance
	FSortedPathList SoundPoints;

	// find potential waypoints for sound propagation
	for ( ANavigationPoint *Nav=Level->NavigationPointList; Nav; Nav=Nav->nextNavigationPoint )
		if ( Nav->bPropagatesSound )
		{
			FLOAT D1 = (Nav->Location - Pawn->Location).SizeSquared();
			FLOAT D2 = (Nav->Location - Other->Location).SizeSquared();
			if ( (D1 < Perceived) && (D2 < Perceived) )
				SoundPoints.addPath(Nav, D1+D2);
		}

	if ( SoundPoints.numPoints == 0 )
		return 0;

	for ( INT i=0; i<SoundPoints.numPoints; i++ )
		if ( GetLevel()->Model->FastLineCheck(SoundPoints.Path[i]->Location, NoiseLoc)
			&& GetLevel()->Model->FastLineCheck(SoundPoints.Path[i]->Location, ViewLoc) )
			return 1;
	return 0;
	unguard;
}

/* Send a HearNoise() message to all Controllers which could possibly hear this noise
*/
void AActor::CheckNoiseHearing(FLOAT Loudness)
{
	guard(AActor::CheckNoiseHearing);

	if ( !Instigator || !Instigator->Controller )
		return;

	FLOAT CurrentTime = GetLevel()->TimeSeconds;

	// allow only one noise per 0.2 seconds from a given instigator & area (within 50 units) unless much louder
	// check the two sound slots
	if ( (Instigator->noise1time > CurrentTime - 0.2f)
		 && ((Instigator->noise1spot - Location).SizeSquared() < 2500.f)
		 && (Instigator->noise1loudness >= 0.9f * Loudness) )
	{
		return;
	}

	if ( (Instigator->noise2time > CurrentTime - 0.2f)
		 && ((Instigator->noise2spot - Location).SizeSquared() < 2500.f)
		 && (Instigator->noise2loudness >= 0.9f * Loudness) )
	{
		return;
	}

	// put this noise in a slot
	if ( Instigator->noise1time < CurrentTime - 0.18f )
	{
		Instigator->noise1time = CurrentTime;
		Instigator->noise1spot = Location;
		Instigator->noise1loudness = Loudness;
	}
	else if ( Instigator->noise2time < CurrentTime - 0.18f )
	{
		Instigator->noise2time = CurrentTime;
		Instigator->noise2spot = Location;
		Instigator->noise2loudness = Loudness;
	}
	else if ( ((Instigator->noise1spot - Location).SizeSquared() < 2500)
			  && (Instigator->noise1loudness <= Loudness) )
	{
		Instigator->noise1time = CurrentTime;
		Instigator->noise1spot = Location;
		Instigator->noise1loudness = Loudness;
	}
	else if ( Instigator->noise2loudness <= Loudness )
	{
		Instigator->noise1time = CurrentTime;
		Instigator->noise1spot = Location;
		Instigator->noise1loudness = Loudness;
	}

	// if the noise is not made by a player or an AI with a player as an enemy, then only send it to
	// other AIs with the same tag
	if ( !Instigator->IsPlayer()
		&& (!Instigator->Controller->Enemy || !Instigator->Controller->Enemy->IsPlayer()) )
	{
		for ( AController *next=Level->ControllerList; next!=NULL; next=next->nextController )
			if ( (next->Pawn != Instigator) && next->IsProbing(NAME_HearNoise)
				&& (next->Tag == Tag)
				&& next->CanHear(Location, Loudness, this) )
				next->eventHearNoise(Loudness, this);
		return;
	}

	// all pawns can hear this noise
	for ( AController *P=Level->ControllerList; P!=NULL; P=P->nextController )
		if ( (P->Pawn != Instigator) && P->IsProbing(NAME_HearNoise)
			 && P->CanHear(Location, Loudness, this) )
			 P->eventHearNoise(Loudness, this);

	unguard;
}

void AController::CheckEnemyVisible()
{
	guard(AController::CheckEnemyVisible);

	FUNC_CLOCK(SPCyc,GStats.DWORDStats(GEngineStats.STATS_Game_SeePlayerCycles));
	if ( Enemy )
	{
		checkSlow(Enemy->IsValid());
		if ( !LineOfSightTo(Enemy) )
			eventEnemyNotVisible();
	}

	unguard;
}

/* Player shows self to pawns that are ready
*/
void AController::ShowSelf()
{
	guard(AController::ShowSelf);

	if ( !Pawn )
		return;
	FUNC_CLOCK(SPCyc,GStats.DWORDStats(GEngineStats.STATS_Game_SeePlayerCycles));
	for ( AController *C=Level->ControllerList; C!=NULL; C=C->nextController )
		if( C!=this  && (bIsPlayer || C->bIsPlayer) && C->SightCounter<0.f )
		{
			//check visibility
			if ( (bIsPlayer ? C->IsProbing(NAME_SeePlayer) : C->IsProbing(NAME_SeeMonster))
				&& C->SeeActor( Pawn ) )
			{
				if ( bIsPlayer )
					C->eventSeePlayer(Pawn);
				else
					C->eventSeeMonster(Pawn);
			}
		}

	unguard;
}

/*
SetPath()
Based on the results of the navigation network (which are stored in RouteCache[],
return the desired path.  Check if there are any intermediate goals (such as hitting a
switch) which must be completed before continuing toward the main goal
*/
AActor* AController::SetPath(INT bInitialPath)
{
	guard(AController::SetPath);

	static AActor* ChosenPaths[4];

	AActor * bestPath = RouteCache[0];

	if ( !Pawn->ValidAnchor() )
		return bestPath;	// make sure on network before trying to find complex routes

	if ( bInitialPath )
	{
		for ( INT i=0; i<4; i++ )
			ChosenPaths[i] = NULL;
		// if this is setting the path toward the main (final) goal
		// make sure still same goal as before
		if ( RouteGoal == GoalList[0] )
		{
			// check for existing intermediate goals
			if ( GoalList[1] )
			{
				INT i = 1;
				while ( GoalList[i] )
					i++;
				AActor* RealGoal = GoalList[i-1];
				if ( Pawn->actorReachable(RealGoal) )
				{
					// I can reach the intermediate goal, so
					GoalList[i-1] = NULL;
					PendingMover = NULL;
					bPreparingMove = false;
					return RealGoal;
				}
				// find path to new goal
				AMover * OldPend = PendingMover;
				UBOOL bOldPrep = bPreparingMove;
				PendingMover = NULL;
				bPreparingMove = false;
				if ( Pawn->findPathToward(RealGoal,RealGoal->Location,NULL, 0.f,false) > 0.f )
				{
					bestPath = SetPath(0);
				}
				else
				{
					PendingMover = OldPend;
					bPreparingMove = bOldPrep;
				}
			}
		}
		else
		{
			GoalList[0] = RouteGoal;
			for ( INT i=1; i<4; i++ )
				GoalList[i] = NULL;
		}
	}
	else
	{
		// add new goal to goal list
		for ( INT i=0; i<4; i++ )
		{
			if ( GoalList[i] == RouteGoal )
				break;
			if ( !GoalList[i] )
			{
				GoalList[i] = RouteGoal;
				break;
			}
		}
	}
	for ( INT i=0; i<4; i++ )
	{
		if ( ChosenPaths[i] == NULL )
		{
			ChosenPaths[i] = bestPath;
			break;
		}
		else if ( ChosenPaths[i] == bestPath )
			return bestPath;
	}
	if ( bestPath && bestPath->IsProbing(NAME_SpecialHandling) )
		bestPath = HandleSpecial(bestPath);
	return bestPath;
	unguard;
}


AActor* AController::HandleSpecial(AActor *bestPath)
{
	guard(AController::HandleSpecial);

	if ( !bCanDoSpecial || GoalList[3] )
		return bestPath;	//limit AI intermediate goal depth to 4

	AActor * newGoal = bestPath->eventSpecialHandling(Pawn);

	if ( newGoal && (newGoal != bestPath) )
	{
		AMover * OldPend = PendingMover;
		UBOOL bOldPrep = bPreparingMove;
		PendingMover = NULL;
		bPreparingMove = false;
		// if can reach intermediate goal directly, return it
		if ( Pawn->actorReachable(newGoal) )
			return newGoal;

		// find path to new goal
		if ( Pawn->findPathToward(newGoal,newGoal->Location,NULL, 0.f,false) > 0.f )
		{
			bestPath = SetPath(0);
		}
		else
		{
			PendingMover = OldPend;
			bPreparingMove = bOldPrep;
		}
	}
	return bestPath;

	unguard;
}

/* AcceptNearbyPath() returns true if the controller will accept a path which gets close to
and withing sight of the destination if no reachable path can be found.
*/
INT AController::AcceptNearbyPath(AActor *goal)
{
	return 0;
}

INT AAIController::AcceptNearbyPath(AActor *goal)
{
	return (goal && (goal->IsAPawn() || goal->Physics == PHYS_Falling));
}

/* AdjustFromWall()
Gives controller a chance to adjust around an obstacle and keep moving
*/

void AController::AdjustFromWall(const FVector& HitNormal, AActor* HitActor)
{
}

void AAIController::AdjustFromWall(const FVector& HitNormal, AActor* HitActor)
{
	guard(AAIController::AdjustFromWall);

	if ( bAdjustFromWalls
		&& ((GetStateFrame()->LatentAction == AI_PollMoveTo)
			|| (GetStateFrame()->LatentAction == AI_PollMoveToward)) )
	{
		if ( Pawn && MoveTarget && HitActor->IsAMover() )
		{
            AMover *HitMover = Cast<AMover>(HitActor);
			if ( HitMover && MoveTarget->HasAssociatedLevelGeometry(HitMover) )
			{
				ANavigationPoint *Nav = Cast<ANavigationPoint>(MoveTarget);
				if ( !Nav || !Nav->bSpecialMove || !Nav->eventSuggestMovePreparation(Pawn) )
					eventNotifyHitMover(HitNormal,HitMover);
				return;
			}
		}
		if ( bAdjusting )
		{
			MoveTimer = -1.f;
		}
		else
		{
			Pawn->SerpentineDir *= -1.f;
			if ( !Pawn->PickWallAdjust(HitNormal) )
				MoveTimer = -1.f;
		}
	}
	unguard;
}

void AController::SetAdjustLocation(const FVector& NewLoc)
{
}

void AAIController::SetAdjustLocation(const FVector& NewLoc)
{
	guard(AAIController::SetAdjustLocation);

	bAdjusting = true;
	AdjustLoc = NewLoc;

	unguard;
}

/*-----------------------------------------------------------------------------
AZoneInfo.
-----------------------------------------------------------------------------*/

void AZoneInfo::PostEditChange()
{
	guard(AZoneInfo::PostEditChange);
	Super::PostEditChange();
	if( GIsEditor )
	{
		//		XLevel->Engine->Flush(0);
		AmbientVector = FGetHSV(AmbientHue,AmbientSaturation,AmbientBrightness);

		for(INT ActorIndex = 0;ActorIndex < XLevel->Actors.Num();ActorIndex++)
			if(XLevel->Actors(ActorIndex))
				XLevel->Actors(ActorIndex)->ClearRenderData();
	}
	unguard;
}

