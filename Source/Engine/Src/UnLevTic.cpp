/*=============================================================================
	UnLevTic.cpp: Level timer tick function
	Copyright 1997-2002 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "EnginePrivate.h"
#include "UnNet.h"
#include "UnPath.h"


/*-----------------------------------------------------------------------------
	Helper classes.
-----------------------------------------------------------------------------*/

/* scion ======================================================================
* FConnectionRelevance
* Author: sz. based on UC1 code
* 
* Convenience class for storing relevance info for a connection.
*
* ============================================================================
*/
class FConnectionRelevance
{
public:
	APlayerController*	InViewer;
	AActor*				Viewer;
	FVector				Location;
	FRotator			Rotation;

	FConnectionRelevance( UNetConnection* InConnection, FLOAT DeltaSeconds )
	{
		guard(FConnectionRelevance::FConnectionRelevance);

        InViewer = InConnection->Actor;
		Viewer = InViewer->GetViewTarget();
		Location = Viewer->Location;
		Rotation = InViewer->Rotation;
		// calculate current view location and rotation for this connection's player controller
		InViewer->eventPlayerCalcView( DeltaSeconds, Viewer, Location, Rotation, 0 );

		// Compute ahead-vectors for prediction.
		FVector Ahead(GMath.ZeroVector);
		if( InConnection->TickCount & 1 )
		{
			FLOAT PredictSeconds = (InConnection->TickCount&2) ? 0.4f : 0.9f;
			Ahead = PredictSeconds * Viewer->Velocity;
			if( Viewer->Base )
				Ahead += PredictSeconds * Viewer->Base->Velocity;
			// scion sz - no need for a line check if zero velocity
			if( !Ahead.IsZero() )
			{
				FCheckResult Hit(1.0f);
				Hit.Location = Location + Ahead;
				Viewer->GetLevel()->Model->LineCheck(Hit,NULL,NULL,Hit.Location,Location,GMath.ZeroVector,NF_NotVisBlocking,0);
				Location = Hit.Location;
			}			
		}

		unguard;
	}

};

//
// Priority sortable list.
//
struct FActorPriority
{
	INT			    Priority;	// Update priority, higher = more important.
	AActor*			Actor;		// Actor.
	UActorChannel*	Channel;	// Actor channel.

	FActorPriority()
	{}
	
	FActorPriority( const FVector& ViewPos, const FVector& ViewDir, UNetConnection* InConnection, UActorChannel* InChannel, AActor* InActor )
	{
		guard(FActorPriority::FActorPriority);

		Actor       = InActor;
		Channel     = InChannel;
		Priority	= CalculatePriority( ViewPos, ViewDir, InConnection );
		unguard;
	}
	
	/* scion ======================================================================
	* FActorPriority::FActorPriority
	* Author: sz
	* 
	* Calc max priority relative to a list of relevant connections.  
	*
	* ============================================================================
	*/
	FActorPriority( UNetConnection* InConnection, AActor* InActor, const TArray<FConnectionRelevance*>& InRelevants )
	{
		guard(FActorPriority::FActorPriority);
		check( InConnection && InActor );
		check( !InConnection->IsA(UChildConnection::StaticClass()) );

		Actor		= InActor;
		Channel		= InConnection->ActorChannels.FindRef(Actor);		
		Priority	= 0;

		// iterate through all relevant connections
		for( INT i=0; i < InRelevants.Num(); i++ )
		{
			FConnectionRelevance* Relevant = InRelevants(i);
			FVector ViewDir( Relevant->Rotation.Vector() );

			INT CalcPriority  = CalculatePriority( Relevant->Location, ViewDir, InConnection );

			// store the maximum priority for this actor relative to each connection
			Priority = Max<INT>( Priority, CalcPriority );
		}

		unguard;
	}

	//epic superville: Calculate the priority for this actor
	inline INT CalculatePriority( const FVector& ViewPos, const FVector& ViewDir, UNetConnection* InConnection )
	{
		FLOAT Time  = Channel ? 
						(InConnection->Driver->Time - Channel->LastUpdateTime) : 
						InConnection->Driver->SpawnPrioritySeconds;
		
		if( !Actor->bHidden )
		{
			FVector Dir = Actor->Location - ViewPos;
			if ( (ViewDir | Dir) < 0.f )
				Time *= 0.5f;
		}

		return appRound(65536.0f * Actor->NetPriority * Time);
	}

	friend INT Compare( const FActorPriority* A, const FActorPriority* B )
	{
		return B->Priority - A->Priority;
	}
};

/*-----------------------------------------------------------------------------
	Tick a single actor.
-----------------------------------------------------------------------------*/
UBOOL AActor::CheckOwnerUpdated()
{
	guardSlow(AActor::CheckOwnerUpdated);
	if( Owner && (INT)Owner->bTicked!=GetLevel()->Ticked && !Owner->bStatic && !Owner->bDeleteMe )
	{
		GetLevel()->NewlySpawned = new(GEngineMem)FActorLink(this,GetLevel()->NewlySpawned);
		return 0;
	}
	return 1;
	unguardSlow;
}

UBOOL APawn::CheckOwnerUpdated()
{
	guardSlow(APawn::CheckOwnerUpdated);
	if( Owner && (INT)Owner->bTicked!=GetLevel()->Ticked && !Owner->bStatic && !Owner->bDeleteMe )
	{
		GetLevel()->NewlySpawned = new(GEngineMem)FActorLink(this,GetLevel()->NewlySpawned);
		return 0;
	}
	// Handle controller-first updating.
	if( Controller && !Controller->bDeleteMe && (INT)Controller->bTicked!=GetLevel()->Ticked )
	{
		GetLevel()->NewlySpawned = new(GEngineMem)FActorLink(this,GetLevel()->NewlySpawned);
		return 0;
	}
	return 1;
	unguardSlow;
}

void ALineOfSightTrigger::TickAuthoritative( FLOAT DeltaSeconds )
{
	guardSlow(ALineOfSightTrigger::TickAuthoritative);

	AActor::TickAuthoritative(DeltaSeconds);

	if ( bEnabled && !bTriggered && SeenActor
		&& (SeenActor->LastRenderTime >= OldTickTime) )
	{
		// find player pawn, and check if close enough
		for ( AController *C=Level->ControllerList; C; C=C->nextController )
		{
			if ( C->LocalPlayerController() )
			{
			    APlayerController *PC = Cast<APlayerController>(C);
				if ( (PC->GetViewTarget()->Location - Location).SizeSquared() < MaxViewDist*MaxViewDist )
				{
					// check if looking directly enough at target
					FVector Dir = (SeenActor->Location - PC->ViewTarget->Location).SafeNormal();
					if ( (PC->Rotation.Vector() | Dir) > RequiredViewDir )
					{
						FCheckResult Hit(1.f);
						GetLevel()->SingleLineCheck( Hit, this, SeenActor->Location, PC->ViewTarget->Location, TRACE_World|TRACE_StopAtFirstHit );
						if ( !Hit.Actor )
							eventPlayerSeesMe(PC);
					}
					break;
				}
			}
		}
	}
	OldTickTime = Level->TimeSeconds;

	unguardSlow;
}

void AActor::TickAuthoritative( FLOAT DeltaSeconds )
{
	guard(AActor::TickAuthoritative);

	// Tick the nonplayer.
	//clockSlow(GStats.DWORDStats(GEngineStats.STATS_Game_ScriptTickCycles));
	eventTick(DeltaSeconds);
	//unclockSlow(GStats.DWORDStats(GEngineStats.STATS_Game_ScriptTickCycles));

	// Update the actor's script state code.
	ProcessState( DeltaSeconds );

	UpdateTimers(DeltaSeconds );

	// Update LifeSpan.
	if( LifeSpan!=0.f )
	{
		LifeSpan -= DeltaSeconds;
		if( LifeSpan <= 0.0001f )
		{
			// Actor's LifeSpan expired.
			GetLevel()->DestroyActor( this );
			return;
		}
	}

	// Perform physics.
	if ( !bDeleteMe && (Physics!=PHYS_None) && (Role!=ROLE_AutonomousProxy) )
		performPhysics( DeltaSeconds );
	unguard;
}

void AActor::TickSimulated( FLOAT DeltaSeconds )
{
	guardSlow(AActor::TickSimulated);

	TickAuthoritative(DeltaSeconds);
	unguardSlow;
}

void APawn::TickSimulated( FLOAT DeltaSeconds )
{
	guardSlow(APawn::TickSimulated);

	//scion jg -- Debug. Make sure there are no divide by zeros going on
	checkSlow(Location == Location && Acceleration == Acceleration &&
		Velocity == Velocity);

	//scion superville: Don't predict physics if physics is none
	//		Do we even need this? Now that we are replicating physics all the time
	//		not just at bNetInitial like epic did?
	if( Physics != PHYS_None ) {
		// Simulated Physics for pawns
		// simulate gravity
		Acceleration = Velocity.SafeNormal();
		if ( bSimulateGravity )
			Physics = PHYS_Walking;	// set physics mode guess for use by animation
		else
			Physics = PHYS_Flying;

		//simulated pawns just predict location, no script execution
		moveSmooth(Velocity * DeltaSeconds);

		// if simulated gravity, check if falling
		if ( bSimulateGravity && !bSimGravityDisabled )
		{
			FCheckResult Hit(1.f);
			if ( Velocity.Z == 0.f )
				GetLevel()->SingleLineCheck(Hit, this, Location - FVector(0.f,0.f,1.5f * CollisionHeight), Location, TRACE_AllBlocking, FVector(CollisionRadius,CollisionRadius,4.f));
			else if ( Velocity.Z < 0.f )
				GetLevel()->SingleLineCheck(Hit, this, Location - FVector(0.f,0.f,8.f), Location, TRACE_AllBlocking, GetCylinderExtent());

			if ( (Hit.Time == 1.f) || (Hit.Normal.Z < UCONST_MINFLOORZ) )
			{
				if ( Velocity.Z == 0.f )
					Velocity.Z = 0.15f * GetGravity().Z;
				Velocity += 0.5f * GetGravity() * DeltaSeconds;
				Physics = PHYS_Falling;
			}
			else
				Velocity.Z = 0.f;
		}
	}

//scion superville: prevent call o UpdateMovementAnimation twice (already called in TickSpecial)
//	if ( bPhysicsAnimUpdate && Mesh )
//		UpdateMovementAnimation(DeltaSeconds);

	//scion superville: Update timers on client machines
	UpdateTimers( DeltaSeconds );

	// Tick the nonplayer.
	//clockSlow(GStats.DWORDStats(GEngineStats.STATS_Game_ScriptTickCycles));
	eventTick(DeltaSeconds);
	//unclockSlow(GStats.DWORDStats(GEngineStats.STATS_Game_ScriptTickCycles));

	unguardSlow;
}

//scion ===========================================================
//	SCION Function: TickSpecial
//	Author: superville (jg code moved to UpdateAttachments)
//
//	Description: Tells attachments to update
//
//	Input: DletaSeconds - change in time since last tick
//	Last Modified: 03/01/04 (superville)
// ================================================================
void AActor::TickSpecial( FLOAT DeltaSeconds )
{
	//Force attachment updates if necessary
	UpdateAttachments();
}

void APawn::TickSpecial( FLOAT DeltaSeconds )
{
	guardSlow(APawn::TickSpecial);

	if ( !bInterpolating && bPhysicsAnimUpdate && Mesh )
		UpdateMovementAnimation(DeltaSeconds);

	// update eyeheight if someone is viewing through this pawn's eyes
	if ( bUpdateEyeheight || IsHumanControlled() )
		eventUpdateEyeHeight(DeltaSeconds);

	// Call our parent
	Super::TickSpecial(DeltaSeconds);
	unguardSlow;
}

UBOOL AActor::PlayerControlled()
{
	return bPlayerControlled;
}

UBOOL APawn::PlayerControlled()
{
	return ( IsLocallyControlled() );
}

UBOOL AActor::Tick( FLOAT DeltaSeconds, ELevelTick TickType )
{
	guard(AActor::Tick);

	// Ignore actors in stasis
	if
	(	bStasis
	&&	((Physics==PHYS_None) || (Physics == PHYS_Rotating))
	&&	(GetLevel()->TimeSeconds - LastRenderTime > 5)
	&&	(Level->NetMode == NM_Standalone) )
	{
		bTicked = GetLevel()->Ticked;
		return 1;
	}

	// Handle owner-first updating.
	if ( !CheckOwnerUpdated() )
		return 0;
	bTicked = GetLevel()->Ticked;

	// Non-player update.
	if( (TickType==LEVELTICK_ViewportsOnly) && !PlayerControlled() )
		return 1;
		
	// This actor is tickable.
	if( RemoteRole == ROLE_AutonomousProxy )
	{
        APlayerController *PC = GetTopPlayerController();
		if ( (PC && PC->LocalPlayerController()) || Physics == PHYS_Karma )
			TickAuthoritative(DeltaSeconds);
		else
		{
			eventTick(DeltaSeconds);

			// Update the actor's script state code.
			ProcessState( DeltaSeconds );
			// Server handles timers for autonomous proxy.
			UpdateTimers( DeltaSeconds );

			if (!bDeleteMe && Physics != PHYS_None && PC && PC->ServerTimeStamp != 0.f)
			{
				FLOAT ServerMoveDelta = Level->TimeSeconds - PC->ServerTimeStamp;
				if (ServerMoveDelta > 0.5f * PC->MaxResponseTime)
				{
					// force position update
					//epic moelfke: return to previous servermove location first
					if (!PC->LastServerMoveLocation.IsZero() && (!PC->LastServerMoveBase || PC->LastServerMoveBase->bStatic))
					{
						FCheckResult Hit(1.0f);
						GetLevel()->MoveActor(this, PC->LastServerMoveLocation - Location, Rotation, Hit);
					}
					if (!Velocity.IsZero())
					{
						performPhysics(Level->TimeSeconds - PC->ServerTimeStamp);
					}
					PC->ServerTimeStamp = Level->TimeSeconds;
					//epic moelfke: remember new location/velocity
					PC->LastServerMoveLocation = Location;
					PC->PredictedVelocity = Velocity;
				}
				else if ( ServerMoveDelta > 0.f && LastRenderTime > Level->TimeSeconds - 1.f &&
							(!PC->LastServerMoveBase || PC->LastServerMoveBase->bStatic) &&
							!PC->PredictedVelocity.IsZero() )
				{
					//epic moelfke: do basic simulated physics on listen server for non-local player pawns between servermoves
					//the pawn will be returned to its original position (PC->LastServerMoveLocation) before
					//the next servermove is processed
					//this smooths out player movement on listen servers
					//PredictedVelocity is used to avoid clobbering Velocity as it may change due to game events (hit momentum, etc)
					
					// simulated pawns just predict location, no script execution
					moveSmooth(PC->PredictedVelocity * DeltaSeconds);
					// check if falling
					FCheckResult Hit(1.f);
					if (PC->PredictedVelocity.Z == 0.f)
					{
						GetLevel()->SingleLineCheck(Hit, this, Location - FVector(0.f,0.f,1.5f * CollisionHeight), Location, TRACE_AllBlocking, FVector(CollisionRadius,CollisionRadius,4.f));
					}
					else if (PC->PredictedVelocity.Z < 0.f)
					{
						GetLevel()->SingleLineCheck(Hit, this, Location - FVector(0.f,0.f,8.f), Location, TRACE_AllBlocking, GetCylinderExtent());
					}

					if (Hit.Time == 1.f || Hit.Normal.Z < UCONST_MINFLOORZ)
					{
						if (PC->PredictedVelocity.Z == 0.f)
						{
							PC->PredictedVelocity.Z = 0.15f * GetGravity().Z;
						}
						PC->PredictedVelocity += 0.5f * GetGravity() * DeltaSeconds;
					}
					else
					{
						PC->PredictedVelocity.Z = 0.f;
					}
				}
			}
		}
	}
	else if ( Role>ROLE_SimulatedProxy )
		TickAuthoritative(DeltaSeconds);
	else if ( Role == ROLE_SimulatedProxy )
		TickSimulated(DeltaSeconds);
	else if ( !bDeleteMe && ((Physics == PHYS_Falling) || (Physics == PHYS_Rotating)) ) // dumbproxies simulate falling if client side physics set
		performPhysics( DeltaSeconds );

	if ( bDeleteMe )
		return 1;

	// Update all animation, including multiple passes if necessary.
	// Meshes are allowed autonomous updating.	
	UpdateAnimation(DeltaSeconds);

	TickSpecial(DeltaSeconds);	// perform any tick functions unique to an actor subclass
	return 1;
	unguard;
}


/* Controller Tick
Controllers are never animated, and do not look for an owner to be ticked before them
Non-player controllers don't support being an autonomous proxy
*/
UBOOL AController::Tick( FLOAT DeltaSeconds, ELevelTick TickType )
{
	guard(AController::Tick);

	// Ignore controllers with no pawn, or pawn is in stasis
	if
	(	bStasis
	|| (TickType==LEVELTICK_ViewportsOnly)
	|| (Pawn && Pawn->bStasis
	&&	((Pawn->Physics==PHYS_None) || (Pawn->Physics == PHYS_Rotating))
	&&	(GetLevel()->TimeSeconds - Pawn->LastRenderTime > 5)
	&&	(Level->NetMode == NM_Standalone)) )
	{
		bTicked = GetLevel()->Ticked;
		return 1;
	}

	bTicked = GetLevel()->Ticked;

	if( Role>=ROLE_SimulatedProxy )
		TickAuthoritative(DeltaSeconds);
	
	// Update eyeheight and send visibility updates
	// with PVS, monsters look for other monsters, rather than sending msgs

	if( Role==ROLE_Authority && TickType==LEVELTICK_All )
	{
		if( SightCounter < 0.0f )
		{
			if( IsProbing(NAME_EnemyNotVisible) )
			{
				CheckEnemyVisible();
				SightCounter = 0.05f + 0.1f * appFrand();
			}
			else
				SightCounter += 0.15f + 0.1f * appFrand();
		}

		SightCounter = SightCounter - DeltaSeconds;
		// for best performance, players show themselves to players and non-players (e.g. monsters),
		// and monsters show themselves to players
		// but monsters don't show themselves to each other
		// also

		if( Pawn && !Pawn->bHidden && !Pawn->bAmbientCreature )
			ShowSelf();
	}
	if ( Pawn )
	{
		// rotate pawn toward focus
		if ( Pawn->bRotateToDesired )
			Pawn->rotateToward(Focus, FocalPoint);

		// face same direction as pawn
		Rotation = Pawn->Rotation;
	}
	if ( MonitoredPawn )
	{
		if ( !Pawn || MonitoredPawn->bDeleteMe || !MonitoredPawn->Controller )
			eventMonitoredPawnAlert();
		else
		{
			// quit if further than max dist, moving away fast, or has moved far enough
			if ( ((MonitoredPawn->Location - Pawn->Location).SizeSquared() > MonitorMaxDistSq)
				|| ((MonitoredPawn->Location - MonitorStartLoc).SizeSquared() > 0.25f * MonitorMaxDistSq) )
				eventMonitoredPawnAlert();
			else if ( (MonitoredPawn->Velocity.SizeSquared() > 0.6f * MonitoredPawn->GroundSpeed)
				&& ((MonitoredPawn->Velocity | (MonitorStartLoc - Pawn->Location)) > 0.f)
				&& ((MonitoredPawn->Location - Pawn->Location).SizeSquared() > 0.25f * MonitorMaxDistSq) )
				eventMonitoredPawnAlert();
		}
	}
	return 1;
	unguard;
}



/*
PlayerControllers
Controllers are never animated, and do not look for an owner to be ticked before them
*/
UBOOL APlayerController::Tick( FLOAT DeltaSeconds, ELevelTick TickType )
{
	guard(APlayerController::Tick);

	bTicked = GetLevel()->Ticked;

	TimeSinceLastFogChange += DeltaSeconds;

	GetViewTarget();
	if( (RemoteRole == ROLE_AutonomousProxy) && !LocalPlayerController() )
	{
		// Update the actor's script state code.
		ProcessState( DeltaSeconds );
		// Server handles timers for autonomous proxy.
		UpdateTimers( DeltaSeconds );
		if (PendingAdjustment.TimeStamp > 0.f)
		{
			eventSendClientAdjustment();
		}
	}
	else if( Role>=ROLE_SimulatedProxy )
	{
		// Player update.
		if( IsA(ACamera::StaticClass()) && !(ShowFlags & SHOW_PlayerCtrl) )
			return 1;

		// Process PlayerTick with input.
		if ( Player )
		{
			if ( !PlayerInput )
				eventInitInputSystem();
			if ( PlayerInput )
			{
				Player->ReadInput( DeltaSeconds );
				eventPlayerTick( DeltaSeconds );
				Player->ReadInput( -1.0f );
			}
		}

		// Update the actor's script state code.
		ProcessState( DeltaSeconds );

		UpdateTimers( DeltaSeconds );

		if ( bDeleteMe )
			return 1;

		// Perform physics.
		if( Physics!=PHYS_None && Role!=ROLE_AutonomousProxy )
			performPhysics( DeltaSeconds );
	}

	// Update eyeheight and send visibility updates
	// with PVS, monsters look for other monsters, rather than sending msgs
	if (Role == ROLE_Authority)
	{
		if (TickType==LEVELTICK_All)
		{
			if (SightCounter < 0.0f)
				SightCounter += 0.2f;

			SightCounter = SightCounter - DeltaSeconds;
			if (Pawn && !Pawn->bHidden)
				ShowSelf();
		}

		//epic moelfke: stats
		if (Pawn && Pawn->Weapon && Pawn->Health > 0 && Stats)
		{
			Stats->SetFloatStatByWeapon( Pawn->Weapon->StatsName != NAME_None ? Pawn->Weapon->StatsName
																				: Pawn->Weapon->GetClass()->GetFName(),
										WEAPON_STAT_FLOAT_TIME, DeltaSeconds / Level->TimeDilation, true );
		}
	}

	return 1;
	unguard;
}

void AActor::UpdateTimers(FLOAT DeltaSeconds)
{
	guardSlow(AActor::UpdateTimers);

	// Update timers.
	if( TimerRate>0.0f && (TimerCounter+=DeltaSeconds)>=TimerRate )
	{
		// Normalize the timer count.
		INT TimerTicksPassed = 1;
		if( TimerRate > 0.0f )
		{
			TimerTicksPassed     = (int)(TimerCounter/TimerRate);
			TimerCounter -= TimerRate * TimerTicksPassed;
			if( TimerTicksPassed && !bTimerLoop )
			{
				// Only want a one-shot timer message.
				TimerTicksPassed = 1;
				TimerRate = 0.0f;
			}
		}

		// Call timer routine with count of timer events that have passed.
		eventTimer();
	}
	//scion rwd - call a function like a normal timer
	// split into two loops to avoid infinite loop where
	// the function called, calls setfunctimer, with a rate less than our current delta
	for (INT i = 0; i < FuncTimers.Num(); i++)
	{
		if( FuncTimers(i).Delay <= 0.f )
		{
			FuncTimers(i).Count += DeltaSeconds;
		}
		else
		{
			FuncTimers(i).Delay -= DeltaSeconds;

			// If delay has expired, call the function
			if( FuncTimers(i).Delay <= 0.f )
			{
				UFunction  *Func;
				FName		FuncName = FuncTimers(i).FuncName;

				// If function was cached, just grab the pointer from the entry
				if( FuncTimers(i).FuncCache )
				{
					Func = FuncTimers(i).FuncCache;
				}
				else
				{
					// Otherwise, find the functio by searching then cache it in case
					// we need it again
					Func = FindFunction(FuncName);
					if( Func && (Func->FunctionFlags & FUNC_Final) )
					{
						FuncTimers(i).FuncCache = Func;
					}
				}

				// now call the function
				if (Func != NULL)
				{
					ProcessEvent(Func,NULL);
				}
				else
				{
					debugf(TEXT("Failed to find function %s for timer"), *FuncName);
				}
			}
		}
	}

	for (INT i = 0; i < FuncTimers.Num(); i++)
	{
		if (FuncTimers(i).Rate <= FuncTimers(i).Count)
		{
			UFunction  *Func;
			FName		FuncName = FuncTimers(i).FuncName;

			// If function was cached, just grab the pointer from the entry
			if( FuncTimers(i).FuncCache )
			{
				Func = FuncTimers(i).FuncCache;
			}
			else
			{
				// Otherwise, find the functio by searching then cache it in case
				// we need it again
				Func = FindFunction(FuncName);

				if( Func && (Func->FunctionFlags & FUNC_Final) )
				{
					FuncTimers(i).FuncCache = Func;
				}
			}

			// if we didn't find the function, or it's not looping
			if (Func == NULL ||
				!FuncTimers(i).bLoop)
			{
				// remove defunct timer
				FuncTimers.Remove(i--,1);
			}
			else
			{
				// otherwise reset for loop (conserve overlap time)
				FuncTimers(i).Count = FuncTimers(i).Count - FuncTimers(i).Rate;
			}
			// now call the function
			if (Func != NULL)
			{
				ProcessEvent(Func,NULL);
			}
			else
			{
				debugf(TEXT("Failed to find function %s for timer"), *FuncName);
			}
		}
	}
	unguardSlow;
}

void AActor::SetFuncTimer
( 
	FName FuncName, 
	FLOAT Rate, 
	UBOOL bLoop, 
	UBOOL bAddToTimer, 
	UBOOL bAllowMultiple, 
	FLOAT Delay 
)
{
	guard(SetFuncTimer);
	if (FuncName != NAME_None)
	{
#ifndef FINAL_RELEASE
		// make sure this function at least exists in our current scope
		UFunction *Func = FindFunction(FuncName);
		if (!Func)
		{
			debugf(NAME_Error, TEXT("SetFuncTimer(): Failed to find function %s in Actor %s!"), *FuncName, GetName());
		}
		// verify that it's a valid function timer
		else if (Func->NumParms != 0)
		{
			debugf(NAME_Error, TEXT("SetFuncTimer(): Attempt to set timer for function %s in Actor %s with parameters or return value!"), *FuncName, GetName());
		}
		else
		{
#endif // FINAL_RELEASE
			if( !bAllowMultiple || bAddToTimer )
			{
				// check to see if we already have this in our list
				for (INT i = 0; i < FuncTimers.Num(); i++)
				{
					if (FuncTimers(i).FuncName == FuncName)
					{
						if (Rate == 0.f)
						{
							// remove the timer
							FuncTimers.Remove(i,1);
						}
						else
						{
							if( bAddToTimer ) {
								FuncTimers(i).Rate += Rate;
								FuncTimers(i).bLoop = bLoop;
							}
							else {
								// update the timer
								FuncTimers(i).Count = 0;
								FuncTimers(i).Rate = Rate;
								FuncTimers(i).bLoop = bLoop;
							}
						}

						if( !bAllowMultiple )
						{
							return;
						}
					}
				}
			}

			if( Rate > 0.f || bAllowMultiple )
			{
				// add a new timer
				FFuncTimerInfo NewFuncTimer;
				NewFuncTimer.FuncName	= FuncName;
				NewFuncTimer.FuncCache	= NULL;
				NewFuncTimer.Delay		= Delay;
				NewFuncTimer.Count		= 0;
				NewFuncTimer.Rate		= Rate;
				NewFuncTimer.bLoop		= bLoop;
				FuncTimers.AddItem(NewFuncTimer);
			}
#ifndef FINAL_RELEASE
		}
#endif // FINAL_RELEASE
	}
	unguard;
}

//scion ===========================================================
//	SCION Function: DisableFuncTimer
//	Author: superville
//
//	Description: Turns off a function timer
//
//	Input: Name FuncName - Name of function timer to disable
//	Network: Both server and client
//	Last Modified: 03/06/04 (superville)
// ================================================================
void AActor::DisableFuncTimer( FName FuncName, UBOOL bAll )
{
	guard(DisableFuncTimer);
	if( FuncName != NAME_None ) 
	{
		// Find function in the list
		for( INT i = 0; i < FuncTimers.Num(); i++ ) 
		{
			// If function names match
			if( FuncTimers(i).FuncName == FuncName ) 
			{
				// Remove the timer
				FuncTimers.Remove( i--, 1 );

				// If we aren't looking to disable all that match
				if( !bAll )
				{
					return;
				}
			}
		}
	}
	unguard;
}

//scion ===========================================================
//	SCION Function: FuncTimerIsActive
//	Author: superville
//
//	Description: Checks if a function timer is active
//
//	Input: Name FuncName - Name of function timer to check
//	Output: TRUE if timer is active, FALSE otherwise
//	Network: Both server and client
//	Last Modified: 03/06/04 (superville)
// ================================================================
UBOOL AActor::FuncTimerIsActive( FName FuncName )
{
	guard(FuncTimerIsActive);
	if( FuncName != NAME_None ) {
		// Find function in the list
		for( INT i = 0; i < FuncTimers.Num(); i++ ) {
			// If function names match
			if( FuncTimers(i).FuncName == FuncName ) {
				// Return that timer is active
				return 1;
			}
		}
	}
	return 0;
	unguard;
}

FLOAT AActor::GetFuncTimerCount( FName FuncName )
{
	guard(GetFuncTimerCount);

	FLOAT Count = -1.f;
	if( FuncName != NAME_None ) {
		// Find function in the list
		for( INT i = 0; i < FuncTimers.Num(); i++ ) {
			// If function names match
			if( FuncTimers(i).FuncName == FuncName ) {
				// Get this timer's rate and bail out
				Count = FuncTimers(i).Count;
				break;
			}
		}
	}

	return Count;

	unguard;
}

FLOAT AActor::GetFuncTimerRate( FName FuncName )
{
	guard(GetFuncTimerRate);

	FLOAT Rate = 0.f;
	if( FuncName != NAME_None ) {
		// Find function in the list
		for( INT i = 0; i < FuncTimers.Num(); i++ ) {
			// If function names match
			if( FuncTimers(i).FuncName == FuncName ) {
				// Get this timer's rate and bail out
				Rate = FuncTimers(i).Rate;
				break;
			}
		}
	}

	return Rate;

	unguard;
}


/*-----------------------------------------------------------------------------
	Network client tick.
-----------------------------------------------------------------------------*/

void ULevel::TickNetClient( FLOAT DeltaSeconds )
{
	guard(ULevel::TickNetClient);
	FUNC_CLOCK(NetTick,GStats.DWORDStats(GEngineStats.STATS_Game_NetTickCycles));
	if( NetDriver->ServerConnection->State==USOCK_Open )
	{
		/* Don't replicate any properties from client to server
		for( TMap<AActor*,UActorChannel*>::TIterator ItC(NetDriver->ServerConnection->ActorChannels); ItC; ++ItC )
		{
			guard(UpdateLocalActors);
			UActorChannel* It = ItC.Value();
			APawn* PlayerPawn = It->GetActor() ? Cast<APawn>(It->GetActor()) : NULL;
			if( PlayerPawn && PlayerPawn->Controller )
			{
				APlayerController* PC = Cast<APlayerController>(PlayerPawn->Controller);
				if ( PC && PC->Player )
					It->ReplicateActor();
			}
			unguard;
		}
		*/
	}
	else if( NetDriver->ServerConnection->State==USOCK_Closed )
	{
		// Server disconnected.

		// Determine if it's a map change or disconnect
		UGameEngine* GE = Cast<UGameEngine>(Engine);

		if (!(GE && GE->GPendingLevel))
		{
			// server's gone - send the client back home unless we already have a pending destination
			// (i.e, server went ahead and loaded the next level while the client was waiting for something)
			if (!Engine->Client->Viewports(0)->Actor || Engine->Client->Viewports(0)->Actor->PendingTravelURL == TEXT(""))
			{
				Engine->SetClientTravel(Engine->Client->Viewports(0), TEXT("?failed"), 0, TRAVEL_Absolute);
			}
		}
		else
		{
			checkSlow(Engine->Client->Viewports.Num());
			//scion bcl -- the ?closed url seems to relaunch menu.unr in our code. Instead we will just wait for the server
			//             to get back to us.
			//Engine->SetClientTravel( Engine->Client->Viewports(0), TEXT("?closed"), 0, TRAVEL_Absolute );
		}
	}
	unguard;
}

/*-----------------------------------------------------------------------------
	Network server ticking individual client.
-----------------------------------------------------------------------------*/
UBOOL AActor::IsNetRelevantFor(APlayerController* RealViewer, AActor* Viewer, const FVector& SrcLocation)
{
	guard(AActor::IsNetRelevantFor);
	if( bAlwaysRelevant || IsOwnedBy(Viewer) || IsOwnedBy(RealViewer) || this==Viewer || Viewer==Instigator || RealViewer->bAllActorsRelevant )
		return 1;
	else if ( (Owner && (Base == Owner) && !bOnlyOwnerSee)
			|| (Base && (AttachmentBone != NAME_None) && Cast<USkeletalMesh>(Base->Mesh)) )
		return Base->IsNetRelevantFor( RealViewer, Viewer, SrcLocation );
	else if( (bHidden || bOnlyOwnerSee) && !bBlockPlayers && AmbientSoundCue == NAME_None )
		return 0;
	else
	{
		// check distance fog
		if ( Region.Zone->bDistanceFog && ((Location - SrcLocation).SizeSquared() > Region.Zone->DistanceFogEnd * Region.Zone->DistanceFogEnd) )
			return 0;
		if ( GetLevel()->Model->FastLineCheck(Location,SrcLocation) )
		{
			/*
			// check against terrain
			// 
			AZoneInfo* Z = Viewer->Region.Zone;
			if( Z && Z->bTerrainZone )
			{
				FVector End = Location;
				End.Z += 2.f * CollisionHeight;
				FCheckResult Hit(1.f);
				for( INT t=0;t<Z->Terrains.Num();t++ )
				{
					if( Z->Terrains(t)->LineCheck( Hit, End, SrcLocation, FVector(0.f,0.f,0.f) )==0 )
					{
						FPointRegion HitRegion = GetLevel()->Model->PointRegion( GetLevel()->GetLevelInfo(), Hit.Location );
						if( HitRegion.Zone == Z )
							return 0;
					}
				}
			} */
			/*
			// check against antiportal volumes
			FCheckResult Hit(1.f);
			for ( INT i=0; i<GetLevel()->AntiPortals.Num(); i++ )
				if ( GetLevel()->AntiPortals(i) && GetLevel()->AntiPortals(i)->GetPrimitive()->LineCheck( Hit, GetLevel()->AntiPortals(i), Location, SrcLocation, FVector(0.f,0.f,0.f), 0, TRACE_StopAtFirstHit )==0 )
					return 0;
			*/
			return 1;
		}
		return 0;
	}
	unguard;
}

UBOOL APlayerController::IsNetRelevantFor(APlayerController* RealViewer, AActor* Viewer, const FVector& SrcLocation)
{
	guardSlow(APlayerController::IsNetRelevantFor);
	return ( this==RealViewer );
	unguardSlow;
}

UBOOL APawn::CacheNetRelevancy(UBOOL bIsRelevant, APlayerController* RealViewer, AActor* Viewer)
{
	guardSlow(APawn::CacheNetRelevancy);

	bCachedRelevant = bIsRelevant;
	NetRelevancyTime = Level->TimeSeconds;
	LastRealViewer = RealViewer;
	LastViewer = Viewer;
	return bIsRelevant;
	unguardSlow;
}

UBOOL APawn::IsNetRelevantFor(APlayerController* RealViewer, AActor* Viewer, const FVector& SrcLocation)
{
	guardSlow(APawn::IsNetRelevantFor);

	if ( (NetRelevancyTime == Level->TimeSeconds) && (RealViewer == LastRealViewer) && (Viewer == LastViewer) )
		return bCachedRelevant;
	if( bAlwaysRelevant || IsOwnedBy(Viewer) || IsOwnedBy(RealViewer) || this==Viewer || Viewer==Instigator || RealViewer->bAllActorsRelevant )
		return CacheNetRelevancy(true,RealViewer,Viewer);
	else if( (bHidden || bOnlyOwnerSee) && !bBlockPlayers && AmbientSoundCue == NAME_None )
		return CacheNetRelevancy(false,RealViewer,Viewer);
	else
	{
		// check distance fog
		if ( Region.Zone->bDistanceFog && ((Location - SrcLocation).SizeSquared() > Region.Zone->DistanceFogEnd * Region.Zone->DistanceFogEnd) )
			return CacheNetRelevancy(false,RealViewer,Viewer);

		// check against BSP - check head and center
		//debugf(TEXT("Check relevance of %s"),*(PlayerReplicationInfo->PlayerName));
		if ( !GetLevel()->Model->FastLineCheck(Location + FVector(0.f,0.f,CollisionHeight),SrcLocation)
			 && !GetLevel()->Model->FastLineCheck(Location,SrcLocation)  )
			return CacheNetRelevancy(false,RealViewer,Viewer);
		/*
		if ( CollisionRadius <= COMMONRADIUS )
			return 0;

		// if large collision volume, check edges
		if ( GetLevel()->Model->FastLineCheck(Location + FVector(0.f,CollisionRadius,0.f),SrcLocation) )
			return CacheNetRelevancy(true,RealViewer,Viewer);
		if ( GetLevel()->Model->FastLineCheck(Location - FVector(0.f,CollisionRadius,0.f),SrcLocation) )
			return CacheNetRelevancy(true,RealViewer,Viewer);
		if ( GetLevel()->Model->FastLineCheck(Location + FVector(CollisionRadius,0.f,0.f),SrcLocation) )
			return CacheNetRelevancy(true,RealViewer,Viewer);
		if ( GetLevel()->Model->FastLineCheck(Location - FVector(CollisionRadius,0.f,0.f),SrcLocation) )
			return CacheNetRelevancy(true,RealViewer,Viewer);
		return 0;
		*/
		/*
		// check against terrain
		// 
		AZoneInfo* Z = Viewer->Region.Zone;
		if( Z && Z->bTerrainZone )
		{
			FVector End = Location;
			End.Z += 2.f * CollisionHeight;
			FCheckResult Hit(1.f);
			for( INT t=0;t<Z->Terrains.Num();t++ )
			{
				if( Z->Terrains(t)->LineCheck( Hit, End, SrcLocation, FVector(0.f,0.f,0.f) )==0 )
				{
					FPointRegion HitRegion = GetLevel()->Model->PointRegion( GetLevel()->GetLevelInfo(), Hit.Location );
					if( HitRegion.Zone == Z )
					return CacheNetRelevancy(false,RealViewer,Viewer);
				}
			}
		}
		*/
		/*
		// check against antiportal volumes
		FCheckResult Hit(1.f);
		for ( INT i=0; i<GetLevel()->AntiPortals.Num(); i++ )
			if ( GetLevel()->AntiPortals(i)
				&& GetLevel()->AntiPortals(i)->GetPrimitive()->LineCheck( Hit, GetLevel()->AntiPortals(i), Location + FVector(0.f,0.f,CollisionHeight), SrcLocation, FVector(0.f,0.f,0.f), 0, TRACE_StopAtFirstHit )==0 )
				return CacheNetRelevancy(false,RealViewer,Viewer);
		*/
		return CacheNetRelevancy(true,RealViewer,Viewer);
	}
	unguardSlow;
}

INT ULevel::ServerTickClients( FLOAT DeltaSeconds )
{
	guard(ULevel::ServerTickClients);

	FUNC_CLOCK(ServerTickClients,GStats.DWORDStats(GEngineStats.STATS_Net_ServetTickClientCyles));

	if ( NetDriver->ClientConnections.Num() == 0 )
		return 0;
	INT Updated=0;

	FMemMark Mark(GMem);
	// initialize connections
	for( INT i=NetDriver->ClientConnections.Num()-1; i>=0; i-- )
	{
		UNetConnection* ClientConnection = NetDriver->ClientConnections(i);

		check(ClientConnection);
		check(ClientConnection->State==USOCK_Pending || ClientConnection->State==USOCK_Open || ClientConnection->State==USOCK_Closed);
		// scion sz - child connections should not be ticked since they 
		// are handled by their parent connections
		check(!ClientConnection->IsA( UChildConnection::StaticClass() ));

		UNetConnection* Connection = ClientConnection;
		INT ChildIdx = 0;
		while( Connection )
		{
			// Handle not ready channels.
			if( Connection->Actor && 
				Connection->IsNetReady(0) && 
				Connection->State==USOCK_Open &&				
				((Connection->Driver->Time - Connection->LastReceiveTime < 1.5f) || Connection != ClientConnection))
			{
				Connection->Viewer = Connection->Actor->GetViewTarget();
				Connection->OwnedConsiderList = new(GMem,Actors.Num()-iFirstNetRelevantActor+2)AActor*;
				Connection->OwnedConsiderListSize = 0;
			}
			else
			{
				Connection->Viewer = NULL;
			}

			// scion sz - iterate through child connections
			if( ChildIdx < ClientConnection->Children.Num() )
			{				
                Connection = ClientConnection->Children( ChildIdx++ );
			}
			else
			{
                Connection = NULL;
			}
		}		
	}

	// make list of actors to consider
	AActor **ConsiderList = new(GMem,Actors.Num()-iFirstNetRelevantActor+2)AActor*;
	INT ConsiderListSize = 0;

	// Add LevelInfo to considerlist
	if( Actors(0) && (Actors(0)->RemoteRole!=ROLE_None) )
	{
		ConsiderList[0] = Actors(0);
		ConsiderListSize++;
	}
	FLOAT ServerTickTime = Engine->GetMaxTickRate();
	if ( ServerTickTime == 0.f )
		ServerTickTime = DeltaSeconds;
	else
		ServerTickTime = 1.f/ServerTickTime;

	// For each net relevant actor
	for( INT i=iFirstNetRelevantActor; i<Actors.Num(); i++ )
	{
		AActor* Actor = Actors(i);

		// If actor is valid and should be replicated and 
		// the timer for next replicate has expired
		if( Actor
			&& (Actor->RemoteRole!=ROLE_None)
			&& (TimeSeconds > Actor->NetUpdateTime) )
		{
			// Determine when to replicate this actor again
			Actor->NetUpdateTime = TimeSeconds + appFrand() * ServerTickTime + 1.f / Actor->NetUpdateFrequency;

			// If actor is always relevant or is relevant to people other than the owner
			if( Actor->bAlwaysRelevant || !Actor->bOnlyRelevantToOwner )
			{
				// Always consider for everybody
				ConsiderList[ConsiderListSize] = Actor;
				ConsiderListSize++;
			}
			else
			{
				// Otherwise, owner is the only one who cares

				// Get the owner of the actor (Playercontrollers and pawns won themselves)
				AActor* ActorOwner = Actor->Owner;
				if (!ActorOwner && (Actor->IsAPlayerController() || Actor->IsAPawn()))
				{
					ActorOwner = Actor;
				}

				// If there is a valid owner
				if (ActorOwner)
				{
					// Look through each network client
					for ( INT j=0; j<NetDriver->ClientConnections.Num(); j++ )
					{
						UNetConnection* ClientConnection = NetDriver->ClientConnections(j);

						UNetConnection* Connection = ClientConnection;
						INT ChildIdx = 0;

						// While there is a valid connection
						while( Connection )
						{
							// Add owner relevant actors to their owning controllers
							if (Connection->Viewer)
							{
								// If the actor owner is the connection viewer (viewtarget, ie pawn)
								// or the connection actor (controller)
								if (ActorOwner == Connection->Viewer || ActorOwner == Connection->Actor)
								{
									// Add it to the consider list for this connetion
									Connection->OwnedConsiderList[Connection->OwnedConsiderListSize] = Actor;
									Connection->OwnedConsiderListSize++;
								}
							}
							else if (Actor->IsOwnedBy(Connection->Actor))
							{
								//epic moelfke: this is necessary so that if the Connection is temporarily unavailable
								//(e.g, connection saturated or no reply in a while, see above where Connection->Viewer is set)
								//and then it becomes available again, owned actors are immediately replicated
								//without this, if the actor has a low NetUpdateFrequency, it could be some time before it
								//is received, which, especially if the actor hasn't even been spawned clientside yet
								//can cause some nasty issues
								Actor->NetUpdateTime = TimeSeconds - 1.f;
							}

							// scion sz - iterate through child connections
							if( ChildIdx < ClientConnection->Children.Num() )
							{
								Connection = ClientConnection->Children( ChildIdx++ );
							}
							else
							{
								Connection = NULL;
							}

						}
					}
				}
			}
		}
	}

	// For each client connection
	// reverse order every frame so if the upload bandwidth is saturated nobody gets completely starved
	static UBOOL bReverseOrder = 0;
	bReverseOrder = !bReverseOrder;
	for (INT i = bReverseOrder ? (NetDriver->ClientConnections.Num() - 1) : 0; bReverseOrder ? (i >= 0) : (i < NetDriver->ClientConnections.Num()); bReverseOrder ? i-- : i++)
	{
		UNetConnection* Connection = NetDriver->ClientConnections(i);

		// If the connection has a view target (ie pawn)
		if( Connection->Viewer )
		{
			// Get list of visible/relevant actors.
			FMemMark Mark(GMem);
			NetTag++;
			Connection->TickCount++;

			// scion sz -
			// keep track of relevancy information for child connections as well as the 
			// parent connection. Each relevant connection has a location and rotation 
			// that is used to check if an actor is visible and relevant for that connection
            TArray<FConnectionRelevance*> Relevants;
			Relevants.AddItem( new(GMem)FConnectionRelevance( Connection, DeltaSeconds ) );
			for( INT ChildIdx = 0; ChildIdx < Connection->Children.Num(); ChildIdx++ )
			{
                Relevants.AddItem( new(GMem)FConnectionRelevance( Connection->Children(ChildIdx), DeltaSeconds ) );
			}
			
			// scion sz - these are not used anywhere and don't
			// really make sense when child connections exist
			//InViewer->Level->ReplicationViewer = InViewer;
			//InViewer->Level->ReplicationViewTarget = Viewer;			

			// Set up to skip all sent temporary actors.
			guard(SkipSentTemporaries);
			for( INT i=0; i<Connection->SentTemporaries.Num(); i++ )
				Connection->SentTemporaries(i)->NetTag = NetTag;
			unguard;

			// Make list of all actors to consider.
			INT              ConsiderCount  = 0;
			FActorPriority** PriorityActors = new(GMem,Actors.Num()-iFirstNetRelevantActor+2)FActorPriority*;			

			guard(MakeConsiderList);

			// For each actor in the consider list
			for( INT i=0; i<ConsiderListSize; i++ )
			{
				AActor* Actor = ConsiderList[i];

				if( Actor->NetTag!=NetTag )
				{
					UActorChannel* Channel = Connection->ActorChannels.FindRef(Actor);

					// If the actor is only replicated when dirty 
					// and the channel that is created for it does not have any dirty actors
					if ( Actor->bOnlyDirtyReplication
						&& Channel
						&& !Channel->bActorDirty
						&& Channel->RecentMap.Num()
						&& Channel->Dirty.Num() == 0 )
					{
						Channel->RelevantTime = NetDriver->Time;
					}
					else
					{
						Actor->NetTag                 = NetTag;						
						// scion sz - get the priority for this actor.  
						// If multiple relevant connections exist then the maximum priority is used
						PriorityActors[ConsiderCount++] = new(GMem)FActorPriority( Connection, Actor, Relevants );
					}
				}
			}

			UNetConnection* NextConnection = Connection;
			INT ChildIdx = 0;
			while( NextConnection )
			{
				for( INT i=0; i<NextConnection->OwnedConsiderListSize; i++ )
				{
					AActor* Actor = NextConnection->OwnedConsiderList[i];
					if( Actor->NetTag!=NetTag )
					{
						// using parent connection here on purpose
						UActorChannel* Channel = Connection->ActorChannels.FindRef(Actor);
						if ( Actor->bOnlyDirtyReplication
							&& Channel
							&& !Channel->bActorDirty
							&& Channel->RecentMap.Num()
							&& Channel->Dirty.Num() == 0 )
						{
							Channel->RelevantTime = NetDriver->Time;
						}
						else
						{
							Actor->NetTag                 = NetTag;
							// scion sz - get the priority for this actor.  
							// If multiple relevant connections exist then the maximum priority is used.
							// Using parent connection here on purpose
							PriorityActors[ConsiderCount++] = new(GMem)FActorPriority( Connection, Actor, Relevants );
						}
					}
				}				
				
				// scion sz - iterate through child connections
				if( ChildIdx < Connection->Children.Num() )
				{
					NextConnection = Connection->Children( ChildIdx++ );
				}
				else
				{
					NextConnection = NULL;
				}
			}
			
			Connection->OwnedConsiderList = NULL;
			Connection->OwnedConsiderListSize = 0;
			Connection->LastRepTime = Connection->Driver->Time;
			unguard;

			// Sort by priority.
			guard(SortConsiderList);
			Sort( PriorityActors, ConsiderCount );
			unguard;

			// Update all relevant actors in sorted order.
			guard(UpdateRelevant);
			INT j;
			for (j = 0; j < ConsiderCount; j++)
			{
				UActorChannel* Channel     = PriorityActors[j]->Channel;
				if ( !Channel || Channel->Actor ) //make sure didn't just close this channel
				{
					AActor*        Actor       = PriorityActors[j]->Actor;
					UBOOL          CanSee      = 0;
					// only check visibility on already visible actors every 1.0 + 0.5R seconds
					// bTearOff actors should never be checked
					if( !Actor->bTearOff && (!Channel || NetDriver->Time - Channel->RelevantTime > 1.f) )
					{
						// scion sz - handle multiple relevant player controllers
						// the actor is relevant if it is relevant to the current connection or
						// any of its child connections
						for( INT RelevantIdx=0; RelevantIdx < Relevants.Num(); RelevantIdx++ )
						{
							FConnectionRelevance* Relevant = Relevants( RelevantIdx );
							if( Actor->IsNetRelevantFor( Relevant->InViewer, Relevant->Viewer, Relevant->Location ) )
							{
								CanSee = 1;
								break;
							}
						}						
					}

					if( CanSee || (Channel && NetDriver->Time - Channel->RelevantTime < NetDriver->RelevantTimeout) )
					{
						// Find or create the channel for this actor.
						STAT_INC(GStats.DWORDStats(GEngineStats.STATS_Net_NumPV));
						if( !Channel && Connection->PackageMap->ObjectToIndex(Actor->GetClass())!=INDEX_NONE )
						{
							// Create a new channel for this actor.
							Channel = (UActorChannel*)Connection->CreateChannel( CHTYPE_Actor, 1 );
							if( Channel )
								Channel->SetChannelActor( Actor );
						}
						if( Channel )
						{
							if ( !Connection->IsNetReady(0) ) // here also???
								break;
							if( CanSee )
								Channel->RelevantTime = NetDriver->Time + 0.5f * appFrand();
							if( Channel->IsNetReady(0) )
							{
								//debugf(TEXT("Replicate %s"),Actor->GetName());
								Channel->ReplicateActor();
								if (Channel->bActorDirty)
								{
									// didn't replicate everything, need to update again next frame
									Actor->NetUpdateTime = TimeSeconds - 1.f;
								}
								Updated++;
							}
							else
							{
								Actor->NetUpdateTime = TimeSeconds - 1.f;
							}
							if ( !Connection->IsNetReady(0) )
								break;
						}
					}
					else if( Channel )
					{
						Channel->Close();
						// skeletal attachments aren't relevant anymore either
						if ( Cast<USkeletalMesh>(Actor->Mesh) )
						{
							for ( INT k=0; k<Actor->Attached.Num(); k++ )
								if ( Actor->Attached(k) && !Actor->Attached(k)->bAlwaysRelevant
									&& (Actor->Attached(k)->AttachmentBone != NAME_None) )
								{
									UChannel *AttachedChannel = Connection->ActorChannels.FindRef(Actor->Attached(k));
									if ( AttachedChannel )
										AttachedChannel->Close();
								}
						}
					}
				}
			}
			for (INT k = j; k < ConsiderCount; k++)
			{
				AActor* Actor = PriorityActors[k]->Actor;
				for (INT RelevantIdx = 0; RelevantIdx < Relevants.Num(); RelevantIdx++)
				{
					FConnectionRelevance* Relevant = Relevants(RelevantIdx);
					if (Actor->IsNetRelevantFor(Relevant->InViewer, Relevant->Viewer, Relevant->Location))
					{
						Actor->NetUpdateTime = TimeSeconds - 1.f;
						break;
					}
				}
			}
			unguard;
			Mark.Pop();
		}

	}
	Mark.Pop();
	return Updated;
	unguard;
}

/*-----------------------------------------------------------------------------
	Network server tick.
-----------------------------------------------------------------------------*/

void UNetConnection::SetActorDirty(AActor* DirtyActor )
{
	guardSlow(UNetConnection::SetActorDirty);
	if( Actor && State==USOCK_Open )
	{
		UActorChannel* Channel = ActorChannels.FindRef(DirtyActor);
		if ( Channel )
			Channel->bActorDirty = true;
	}
	unguardSlow;
}

void ULevel::TickNetServer( FLOAT DeltaSeconds )
{
	guard(ULevel::TickNetServer);

	// Update all clients.
	INT Updated=0;

	// first, set which channels have dirty actors (need replication)
	AActor* Actor = Actors(0);
	if( Actor && Actor->bNetDirty )
	{
		for( INT j=NetDriver->ClientConnections.Num()-1; j>=0; j-- )
			NetDriver->ClientConnections(j)->SetActorDirty(Actor);
		Actor->bNetDirty = 0;
	}
	for( INT i=iFirstNetRelevantActor; i<Actors.Num(); i++ )
	{
		AActor* Actor = Actors(i);
		if( Actor && Actor->bNetDirty )
		{
			for( INT j=NetDriver->ClientConnections.Num()-1; j>=0; j-- )
				NetDriver->ClientConnections(j)->SetActorDirty(Actor);
			Actor->bNetDirty = 0;
		}
	}
	CLOCK(NetTickCyc,GStats.DWORDStats(GEngineStats.STATS_Game_NetTickCycles));
	Updated = ServerTickClients( DeltaSeconds );
	UNCLOCK();

#ifndef FINAL_RELEASE
	// Log message.
	if( (INT)(TimeSeconds-DeltaSeconds)!=(INT)(TimeSeconds) )
		debugf( NAME_Title, LocalizeProgress(TEXT("RunningNet"),TEXT("Engine")), *GetLevelInfo()->Title, *URL.Map, NetDriver->ClientConnections.Num() );

	// Stats.
	if( Updated )
	{
		for( INT i=0; i<NetDriver->ClientConnections.Num(); i++ )
		{
			UNetConnection* Connection = NetDriver->ClientConnections(i);
			if( Connection->Actor && Connection->State==USOCK_Open )
			{
				if( Connection->UserFlags&1 )
				{
					// Send stats.
					INT NumActors=0;
					for( INT i=0; i<Actors.Num(); i++ )
						NumActors += Actors(i)!=NULL;

					FString Stats = FString::Printf
					(
						TEXT("r=%i cli=%i act=%03.1f (%i) net=%03.1f pv/c=%i rep/c=%i rpc/c=%i"),
						appRound(Engine->GetMaxTickRate()),
						NetDriver->ClientConnections.Num(),
						GSecondsPerCycle*1000*GStats.DWORDStats(GEngineStats.STATS_Game_ActorTickCycles),
						NumActors,
						GSecondsPerCycle*1000*GStats.DWORDStats(GEngineStats.STATS_Game_NetTickCycles),
						GStats.DWORDStats(GEngineStats.STATS_Net_NumPV)   / NetDriver->ClientConnections.Num(),
						GStats.DWORDStats(GEngineStats.STATS_Net_NumReps) / NetDriver->ClientConnections.Num(),
						GStats.DWORDStats(GEngineStats.STATS_Net_NumRPC)  / NetDriver->ClientConnections.Num()
					);
					Connection->Actor->eventClientMessage( *Stats, NAME_None );
				}
				if( Connection->UserFlags&2 )
				{
					FString Stats = FString::Printf
					(
						TEXT("snd=%02.1f recv=%02.1f"),
						GSecondsPerCycle*1000*Connection->Driver->SendCycles,
						GSecondsPerCycle*1000*Connection->Driver->RecvCycles
					);
					Connection->Actor->eventClientMessage( *Stats, NAME_None );
				}
			}
		}
	}
#endif
	unguard;
}

/*-----------------------------------------------------------------------------
	Demo Recording tick.
-----------------------------------------------------------------------------*/

INT ULevel::TickDemoRecord( FLOAT DeltaSeconds )
{
	guard(ULevel::TickDemoRecord);

	// All replicatable actors are assumed to be relevant for demo recording.
	UNetConnection* Connection = DemoRecDriver->ClientConnections(0);
	for( INT i=iFirstNetRelevantActor; i<Actors.Num(); i++ )
	{
		AActor* Actor = Actors(i);
		UBOOL IsNetClient = (GetLevelInfo()->NetMode == NM_Client);
		if
		(	Actor
		&&	( (IsNetClient && Actor->bTearOff) || Actor->RemoteRole!=ROLE_None || (IsNetClient && Actor->Role!=ROLE_None && Actor->Role != ROLE_Authority))
		&&  (!Actor->bNetTemporary || Connection->SentTemporaries.FindItemIndex(Actor)==INDEX_NONE)
		&&  (Actor->bStatic || !Actor->GetClass()->GetDefaultActor()->bStatic))
		{
			// Create a new channel for this actor.
			UActorChannel* Channel = Connection->ActorChannels.FindRef( Actor );
			if( !Channel && Connection->PackageMap->ObjectToIndex(Actor->GetClass())!=INDEX_NONE )
			{
				// Check we haven't run out of actor channels.
				Channel = (UActorChannel*)Connection->CreateChannel( CHTYPE_Actor, 1 );
				check(Channel);
				Channel->SetChannelActor( Actor );
			}
			if( Channel )
			{
				// Send it out!
				check(!Channel->bClosing);
				if( Channel->IsNetReady(0) )
				{
					Actor->bDemoRecording = 1;
					Actor->bNetDirty = 1;
					Actor->bClientDemoRecording = IsNetClient;
					UBOOL TornOff = 0;
					if(IsNetClient)
					{
						if( Actor->bTearOff && Actor->Role==ROLE_Authority && Actor->RemoteRole==ROLE_None )
						{
							TornOff = 1;
							Actor->RemoteRole = ROLE_SimulatedProxy;
						}
						else
						Exchange(Actor->RemoteRole, Actor->Role);
					}
					Channel->ReplicateActor();
					if(IsNetClient)
					{
						if( TornOff )
							Actor->RemoteRole = ROLE_None;
						else
						Exchange(Actor->RemoteRole, Actor->Role);
					}
					Actor->bDemoRecording = 0;
					Actor->bClientDemoRecording = 0;
				}
			}
		}
	}
	return 1;
	unguard;
}
INT ULevel::TickDemoPlayback( FLOAT DeltaSeconds )
{
	guard(ULevel::TickDemoPlayback);
	if
	(	GetLevelInfo()->LevelAction==LEVACT_Connecting
	&&	DemoRecDriver->ServerConnection->State!=USOCK_Pending )
	{
		GetLevelInfo()->LevelAction = LEVACT_None;
		Engine->SetProgress( TEXT(""), TEXT(""), 0.0f );
	}
	if( DemoRecDriver->ServerConnection->State==USOCK_Closed && !DemoRecDriver->Loop )
	{
		// Demo stopped playing
		check(Engine->Client->Viewports.Num());

		//todo rjp -- this is currently broken (Browse no longer checks for ?entry)
		Engine->SetClientTravel( Engine->Client->Viewports(0), TEXT("?entry"), 0, TRAVEL_Absolute );
	}
	return 1;
	unguard;
}

/*-----------------------------------------------------------------------------
	Main level timer tick handler.
-----------------------------------------------------------------------------*/
UBOOL ULevel::IsPaused()
{
	guardSlow(ULevel::IsPaused);
	
	return (GetLevelInfo()->Pauser != NULL);
	unguardSlow;
}
//
// Update the level after a variable amount of time, DeltaSeconds, has passed.
// All child actors are ticked after their owners have been ticked.
//
void ULevel::Tick( ELevelTick TickType, FLOAT DeltaSeconds )
{
	guard(ULevel::Tick);
	ALevelInfo* Info = GetLevelInfo();

	FMemMark Mark(GMem);
	FMemMark EngineMark(GEngineMem);
	GInitRunaway();
	InTick=1;

	UBOOL RecordDemoFrame = 0;
	guard(UpdateDemo);
	if( DemoRecDriver )
	{
		RecordDemoFrame = DemoRecDriver->UpdateDemoTime( &DeltaSeconds );
		DemoRecDriver->TickDispatch( DeltaSeconds );

		// Fetch demo playback packets from demo file.
		if( DemoRecDriver->ServerConnection )
			TickDemoPlayback( DeltaSeconds );
	}
	unguard;

	// Update the net code and fetch all incoming packets.
	guard(UpdatePreNet);
	if( NetDriver )
	{
		NetDriver->TickDispatch( DeltaSeconds );
		if( NetDriver->ServerConnection )
			TickNetClient( DeltaSeconds );
	}
	unguard;

	// Update collision.
	guard(UpdateCollision);
	if( Hash )
		Hash->Tick();
	unguard;

	// Update time.
	guard(UpdateTime);
	if( !GIsBenchmarking )
	DeltaSeconds *= Info->TimeDilation;
	if ( !IsPaused() )
		TimeSeconds += DeltaSeconds;
	Info->TimeSeconds = TimeSeconds;
/* GERMAN VERSION
	AGameInfo *DefaultGameActor = Cast<AGameInfo>((AGameInfo::StaticClass())->GetDefaultActor());
	if ( DefaultGameActor )
		DefaultGameActor->bAlternateMode = true;
	if ( Info->Game )
		Info->Game->bAlternateMode = true;
*/
	UpdateTime(Info);
	if( Info->bPlayersOnly )
		TickType = LEVELTICK_ViewportsOnly;
	unguard;

	// Clamp time between 2000 fps and 2.5 fps.
	DeltaSeconds = Clamp(DeltaSeconds,0.0005f,0.40f);

	//epic moelfke: always tick the LevelInfo
	Info->Tick(DeltaSeconds, TickType);

	// If caller wants time update only, or we are paused, skip the rest.
	CLOCK(ActorTickCyc,GStats.DWORDStats(GEngineStats.STATS_Game_ActorTickCycles));
	if (TickType != LEVELTICK_TimeOnly && !IsPaused())
	{
		if (!NetDriver || !NetDriver->ServerConnection || NetDriver->ServerConnection->State == USOCK_Open)
		{
			// Tick all actors, owners before owned.
			guard(TickAllActors);
			NewlySpawned = NULL;
			INT Updated  = 1;

#ifdef WITH_KARMA
			/* KTODO: Should we do this _after_ actor physics? */
			KTickLevelKarma(this, DeltaSeconds);
#endif

			for( INT iActor=iFirstDynamicActor; iActor<Actors.Num(); iActor++ )
				if( Actors( iActor ) && !Actors(iActor)->bDeleteMe )
					Updated += Actors( iActor )->Tick(DeltaSeconds,TickType);
			while( NewlySpawned && Updated )
			{
				FActorLink* Link = NewlySpawned;
				NewlySpawned     = NULL;
				Updated          = 0;
				for( Link; Link; Link=Link->Next )
					if( Link->Actor && Link->Actor->bTicked!=(DWORD)Ticked && !Link->Actor->bDeleteMe )
						Updated += Link->Actor->Tick( DeltaSeconds, TickType );
			}
#ifdef WITH_KARMA
			if(KGData)
				KGData->bDoTick = 0;
#endif
			unguard;
		}
		//epic moelfke: when the connection has been closed, still tick any local PlayerControllers
		else if (NetDriver->ServerConnection->State == USOCK_Closed && Engine && Engine->Client)
		{
			for (INT i = 0; i < Engine->Client->Viewports.Num(); i++)
			{
				if (Engine->Client->Viewports(i) && Engine->Client->Viewports(i)->Actor)
				{
					Engine->Client->Viewports(i)->Actor->Tick(DeltaSeconds, TickType);
				}
			}
		}
	}
	else if( IsPaused() )
	{
		// Absorb input if paused.
		guard(AbsorbedPaused);
		NewlySpawned = NULL;
		INT Updated  = 1;
		for( INT iActor=iFirstDynamicActor; iActor<Actors.Num(); iActor++ )
		{
			APlayerController* PC=Cast<APlayerController>(Actors(iActor));
			if( PC && Cast<UViewport>(PC->Player) )
			{
				PC->Player->ReadInput( DeltaSeconds );
				PC->PlayerInput->eventPlayerInput( DeltaSeconds );
				for( TFieldIterator<UFloatProperty> It(PC->GetClass()); It; ++It )
					if( It->PropertyFlags & CPF_Input )
						*(FLOAT*)((BYTE*)PC + It->Offset) = 0.f;
				//epic moelfke: added special tick function for PlayerController while paused
				PC->eventPausedTick(DeltaSeconds);
				PC->bTicked = (DWORD)Ticked;
			}
			else if( Actors(iActor) )
			{
				if ( Actors(iActor)->bAlwaysTick && !Actors(iActor)->bDeleteMe )
					Actors(iActor)->Tick(DeltaSeconds,TickType);
				else
					Actors(iActor)->bTicked = (DWORD)Ticked;
			}
		}
		while( NewlySpawned && Updated )
		{
			FActorLink* Link = NewlySpawned;
			NewlySpawned     = NULL;
			Updated          = 0;
			for( Link; Link; Link=Link->Next )
				if( Link->Actor->bTicked!=(DWORD)Ticked && !Link->Actor->bDeleteMe )
				{
					if ( Link->Actor->bAlwaysTick )
						Updated += Link->Actor->Tick( DeltaSeconds, TickType );
					else
						Link->Actor->bTicked = (DWORD)Ticked;
				}
		}
		unguard;
	}
	UNCLOCK();
/*
	if ( (GStats.DWORDStats(GEngineStats.STATS_Game_ActorTickCycles) * GSecondsPerCycle * 1000.0f >= 30.f)
		|| (GScriptCycles * GSecondsPerCycle * 1000.0f > 10.f) )
 		debugf(TEXT("********** ActorTick Cycles %f at %f pathtime %f script time %f"),
					GStats.DWORDStats(GEngineStats.STATS_Game_ActorTickCycles) * GSecondsPerCycle * 1000.0f,
					TimeSeconds,
					GStats.DWORDStats(GEngineStats.STATS_Game_FindPathCycles) * GSecondsPerCycle * 1000.0f,
					GScriptCycles * GSecondsPerCycle * 1000.0f
					);
*/

	// Update net server and flush networking.
	guard(UpdateNetServer);
	if( NetDriver )
	{
		if( !NetDriver->ServerConnection )
			TickNetServer( DeltaSeconds );
		NetDriver->TickFlush();
	}
	unguard;

#ifndef FINAL_RELEASE
	// Demo Recording.
	guard(UpdatePostDemoRec);
	if( DemoRecDriver )
	{
		if( !DemoRecDriver->ServerConnection && RecordDemoFrame )
			TickDemoRecord( DeltaSeconds );
		DemoRecDriver->TickFlush();
	}
	unguard;
#endif

	// Finish up.
	Ticked = !Ticked;
	InTick = 0;
	Mark.Pop();
	EngineMark.Pop();

	CLOCK(CleanUp,GStats.DWORDStats(GEngineStats.STATS_Game_CleanupDestroyedCycles));
	CleanupDestroyed( 0 );
	UNCLOCK();
	
	unguardf(( TEXT("(NetMode=%i)"), GetLevelInfo()->NetMode ));
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

