/*=============================================================================
	UnPhysic.cpp: Actor physics implementation

	Copyright 1997-2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Steven Polge 3/97
=============================================================================*/

#include "EnginePrivate.h"

void AActor::execSetPhysics( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetPhysics);

	P_GET_BYTE(NewPhysics);
	P_FINISH;
	setPhysics(NewPhysics);
	unguardSlow;
}

void AActor::execAutonomousPhysics( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execAutonomousPhysics);

	P_GET_FLOAT(DeltaSeconds);
	P_FINISH;

	//scion jg -- Debug. Make sure there are no divide by zeros going on
	check(Location == Location);
	check(Acceleration == Acceleration);
	check(Velocity == Velocity);

	check(PhysicsVolume);

	// round acceleration to be consistent with replicated acceleration
	Acceleration.X = 0.1f * int(10 * Acceleration.X);
	Acceleration.Y = 0.1f * int(10 * Acceleration.Y);
	Acceleration.Z = 0.1f * int(10 * Acceleration.Z);

	// Perform physics.
	if( Physics!=PHYS_None )
		performPhysics( DeltaSeconds );
	unguardSlow;
}

//======================================================================================
// smooth movement (no real physics)

void AActor::execMoveSmooth( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execMoveSmooth);

	P_GET_VECTOR(Delta);
	P_FINISH;

	bJustTeleported = 0;
	int didHit = moveSmooth(Delta);

	*(DWORD*)Result = didHit;
	unguardexecSlow;
}

UBOOL AActor::moveSmooth(const FVector& Delta)
{
	guard(AActor::moveSmooth);

	FUNC_CLOCK(PhysCyc,GStats.DWORDStats(GEngineStats.STATS_Game_PhysicsCycles));
	FCheckResult Hit(1.f);
	UBOOL didHit = GetLevel()->MoveActor( this, Delta, Rotation, Hit );
	if (Hit.Time < 1.f)
	{
		FVector GravDir(0,0,-1);
		if (GetGravity().Z > 0)
			GravDir.Z = 1;
		FVector DesiredDir(Delta.SafeNormal());

		FLOAT UpDown = GravDir | DesiredDir;
		if ( (Abs(Hit.Normal.Z) < 0.2f) && (UpDown < 0.5f) && (UpDown > -0.2f) )
		{
			stepUp(GravDir, DesiredDir, Delta * (1.f - Hit.Time), Hit);
		}
		else
		{
			FVector Adjusted = (Delta - Hit.Normal * (Delta | Hit.Normal)) * (1.f - Hit.Time);
			if( (Delta | Adjusted) >= 0 )
			{
				FVector OldHitNormal = Hit.Normal;
				FVector DesiredDir(Delta.SafeNormal());
				GetLevel()->MoveActor(this, Adjusted, Rotation, Hit);
				if (Hit.Time < 1.f)
				{
					SmoothHitWall(Hit.Normal, Hit.Actor);
					TwoWallAdjust(DesiredDir, Adjusted, Hit.Normal, OldHitNormal, Hit.Time);
					GetLevel()->MoveActor(this, Adjusted, Rotation, Hit);
				}
			}
		}
	}
	return didHit;
	unguard;
}

//scion superville: Expose Max speed to Karma (use default Karma max for now)
FLOAT AActor::GetMaxSpeed()
{
	return 0.f;
}

//scion superville: Gravity accessor function
FVector AActor::GetGravity()
{
	return PhysicsVolume->Gravity * GravityScale;
}

//scion superville:
FLOAT AActor::GetLandingTime( FVector BaseLocation, FVector LandLocation, FVector Vel, UBOOL bTrace )
{
	if( bTrace ) {
		FCheckResult HitLanding(1.f);

		UBOOL bFoundGround = !GetLevel()->SingleLineCheck(  HitLanding,
															this,
															LandLocation,
															BaseLocation,
															TRACE_World | TRACE_StopAtFirstHit,
															GetCylinderExtent() );

		if( bFoundGround ) {
			// Will be landing on something
			LandLocation.Z = HitLanding.Location.Z;
		}
		else {
			// Not landing on anything
			return -1.f;
		}
	}

	FLOAT TimeToPeak = 0.f;
	FLOAT TimeToLand = 0.f;

	FLOAT GravZ		 = GetGravity().Z;
	FLOAT VelZ		 = Vel.Z;
	
	// If actor is rising
	// Compute time to peak of jump and the new z at that peak so
	// the landing time is accurate
	if( VelZ > 0 ) {
		// Time to peak of jump is pulled from
		// Vn = Vo + A(T) where Vn == 0
		TimeToPeak = -VelZ / GravZ;

		// Peak location is computed from TimeToPeak
		// Xn = Xo + Vo(T) + 0.5A(T^2)
		BaseLocation.Z = BaseLocation.Z + (VelZ * TimeToPeak) + (0.5 * GravZ * TimeToPeak * TimeToPeak);

		// Now set VelZ to zero so we can figure out the time to come back down
		VelZ = 0.f;
	}

	// When actor is falling
	// Use quadratic to solve for time in Xn = Xo + Vo(t) + 0.5A(t^2)
	// 0 = (0.5 * A)t^2 + (Vo)t + (Xo - Xn)
	//		|- A -|        |B|     |- C -|
	// t = -B +/- Sqrt( B^2 - 4AC))
	//				2A
	FLOAT DeltaPos	 = BaseLocation.Z - LandLocation.Z;
	FLOAT SqrtPart	 = (VelZ * VelZ) - (2.f * GravZ * DeltaPos);
	if( SqrtPart >= 0.f ) {
		TimeToLand = (-VelZ - appSqrt(SqrtPart)) / GravZ;
	}

	return  TimeToPeak + TimeToLand;
}

//jg -- made a ref so there is no copy
void AActor::SmoothHitWall(const FVector& HitNormal, AActor *HitActor)
{
	guardSlow(AActor::SmoothHitWall);

	eventHitWall(HitNormal, HitActor);
	unguardSlow;
}

//jg -- made a ref so there is no copy. This version does need to copy though
void APawn::SmoothHitWall(const FVector& HitNormal, AActor *HitActor)
{
	guardSlow(APawn::SmoothHitWall);
	FVector NewHitNormal(HitNormal);
	if ( Controller )
	{
		FVector Dir = (Controller->Destination - Location).SafeNormal();
		if ( Physics == PHYS_Walking )
		{
			NewHitNormal.Z = 0;
			Dir.Z = 0;
		}
		if ( Controller->eventNotifyHitWall(NewHitNormal, HitActor) )
			return;
	}
	eventHitWall(NewHitNormal, HitActor);
	unguardSlow;
}

//======================================================================================

void AActor::FindBase()
{
	guard(AActor::findBase);

	FCheckResult Hit(1.f);
	GetLevel()->SingleLineCheck( Hit, this, Location + FVector(0,0,-8), Location, TRACE_AllBlocking, GetCylinderExtent() );
	if (Base != Hit.Actor)
		SetBase(Hit.Actor,Hit.Normal);
	unguard;
}

void AActor::setPhysics(BYTE NewPhysics, AActor *NewFloor, const FVector& NewFloorV)
{
	guard(AActor::setPhysics);
	
	if (Physics == NewPhysics)
		return;
	FUNC_CLOCK(PhysCyc,GStats.DWORDStats(GEngineStats.STATS_Game_PhysicsCycles));

#ifdef WITH_KARMA
    BYTE OldPhysics = Physics;
#endif

	Physics = NewPhysics;

#ifdef WITH_KARMA
    // If the new physics isn't using Karma, but the old one was, shut it down.
    if(OldPhysics == PHYS_Karma)
    {
		AKConstraint* con = Cast<AKConstraint>(this);
        if(con)
            KTermConstraintKarma(con);
        else
            KTermActorDynamics(this);
    }
	else if(OldPhysics == PHYS_KarmaRagDoll)
		KTermActorKarma(this);

	// if we are turning on Karma, do it here.
	if(Physics == PHYS_Karma || Physics == PHYS_KarmaRagDoll)
    {
		KInitActorKarma(this);
    }
#endif

	if( (Physics == PHYS_Walking)		|| 
		(Physics == PHYS_None)			|| 
		(Physics == PHYS_RootMotion)	|| 
		(Physics == PHYS_Rotating)		)
	{	
		if ( NewFloor == NULL )
			FindBase();
		else if ( Base != NewFloor )
			SetBase(NewFloor,NewFloorV);
	}
	else if (Base != NULL)
		SetBase(NULL);
	if ( (Physics == PHYS_None) || (Physics == PHYS_Rotating) )
	{
		Velocity = FVector(0,0,0);
		Acceleration = FVector(0,0,0);
	}
	
	if ( PhysicsVolume )
		PhysicsVolume->eventPhysicsChangedFor(this);

	unguard;
}

void APawn::setPhysics(BYTE NewPhysics, AActor* NewFloor, const FVector& NewFloorV)
{
	if (NewPhysics == PHYS_Falling)
	{
		//epic moelfke: record when pawn starts falling for stats
		if (Physics != PHYS_Falling)
		{
			FallStartTime = Level->TimeSeconds;
		}
	}
	else
	{
		//epic moelfke: no longer falling, clear prelanded flag
		bPreLanded = 0;
	}

	Super::setPhysics(NewPhysics, NewFloor, NewFloorV);
}

void AMover::performPhysics(FLOAT DeltaSeconds)
{
	guard(AMover::performPhysics);

	//FLOAT OldClock = GStats.DWORDStats(GEngineStats.STATS_Game_SpawningCycles);
	FUNC_CLOCK(PhysCyc,GStats.DWORDStats(GEngineStats.STATS_Game_PhysicsCycles));
	FVector OldVelocity = Velocity;

	// change position
	switch (Physics)
	{
		case PHYS_Projectile: physProjectile(DeltaSeconds, 0); break;
		case PHYS_Falling: physFalling(DeltaSeconds, 0); break;
		case PHYS_Rotating: break;
		case PHYS_MovingBrush: physMovingBrush(DeltaSeconds); break;
#ifdef WITH_KARMA
        case PHYS_Karma: physKarma(DeltaSeconds); break;
        case PHYS_KarmaRagDoll: physKarmaRagDoll(DeltaSeconds); break;
#endif
	}
	if ( bOnlyDirtyReplication && (Physics != PHYS_Rotating) && (Physics != PHYS_MovingBrush) && (Physics != PHYS_None) )
		bNetDirty = true;

	// rotate
	if ( !RotationRate.IsZero() && !bInterpolating && ((Physics != PHYS_Rotating) || !bNoAIRelevance || (Level->TimeSeconds - LastRenderTime < 2.f)) )
		physicsRotation(DeltaSeconds);

	// allow touched actors to impact physics
	if ( PendingTouch )
	{
		PendingTouch->eventPostTouch(this);
		AActor *OldTouch = PendingTouch;
		PendingTouch = PendingTouch->PendingTouch;
		OldTouch->PendingTouch = NULL;
	}
	unguard;
}

void AActor::performPhysics(FLOAT DeltaSeconds)
{
	guard(AActor::performPhysics);

	if ( (Physics == PHYS_Rotating) && !bInterpolating && !bAlwaysRotate && (Level->TimeSeconds - LastRenderTime > 1.f) )
		return;
	//FLOAT OldClock = GStats.DWORDStats(GEngineStats.STATS_Game_PhysicsCycles);
	FUNC_CLOCK(PhysCyc,GStats.DWORDStats(GEngineStats.STATS_Game_PhysicsCycles));

	FVector OldVelocity = Velocity;

	// change position
	switch (Physics)
	{
		case PHYS_Projectile: physProjectile(DeltaSeconds, 0); break;
		case PHYS_Falling: physFalling(DeltaSeconds, 0); break;
		case PHYS_Rotating: break;
		case PHYS_RootMotion: physRootMotion(DeltaSeconds); break;
#ifdef WITH_KARMA
        case PHYS_Karma: physKarma(DeltaSeconds); break;
        case PHYS_KarmaRagDoll: physKarmaRagDoll(DeltaSeconds); break;
#endif
	}

	// rotate
	if ( !RotationRate.IsZero() && !bInterpolating )
		physicsRotation(DeltaSeconds);

	// allow touched actors to impact physics
	if ( PendingTouch )
	{
		PendingTouch->eventPostTouch(this);
		AActor *OldTouch = PendingTouch;
		PendingTouch = PendingTouch->PendingTouch;
		OldTouch->PendingTouch = NULL;
	}
	//debugf(TEXT("%s physics took %f"),GetName(), (GStats.DWORDStats(GEngineStats.STATS_Game_PhysicsCycles) - OldClock) * GSecondsPerCycle * 1000.0f);
	unguard;
}

void APawn::performPhysics(FLOAT DeltaSeconds)
{
	guard(APawn::performPhysics);
	FUNC_CLOCK(PhysCyc,GStats.DWORDStats(GEngineStats.STATS_Game_PhysicsCycles));

	if ( (Location.Z < ((Region.Zone->bSoftKillZ && (Physics != PHYS_Walking)) ? Region.Zone->KillZ - 2500.f : Region.Zone->KillZ)) && (Controller || (Region.Zone->KillZType != KILLZ_Suicide)) )
	{
		eventFellOutOfWorld(Region.Zone->KillZType);
		if ( bDeleteMe )
			return;
	}
	if ( bCollideWorld && (Region.ZoneNumber == 0) && !bIgnoreOutOfWorld )
	{
		// not in valid spot
		debugf( TEXT("%s fell out of the world!"), GetName());
		eventFellOutOfWorld(KILLZ_None);
		return;
	}

	FVector OldVelocity = Velocity;
	OldZ = Location.Z;	// used for eyeheight smoothing

	// change position
	startNewPhysics(DeltaSeconds,0);
	bSimulateGravity = ( (Physics == PHYS_Falling) || (Physics == PHYS_Walking) );

	if( Controller )
	{
		Controller->MoveTimer -= DeltaSeconds;
		if ( !bInterpolating && Physics != PHYS_Karma && Physics != PHYS_KarmaRagDoll)
		{
			// rotate
			if ( (Rotation != DesiredRotation) || (RotationRate.Roll > 0) || IsHumanControlled() )
				physicsRotation(DeltaSeconds, OldVelocity);
		}
	}

	OldAcceleration = Acceleration;
	AvgPhysicsTime = 0.8f * AvgPhysicsTime + 0.2f * DeltaSeconds;

	if ( PendingTouch )
	{
		PendingTouch->eventPostTouch(this);
		if ( PendingTouch )
		{
			AActor *OldTouch = PendingTouch;
			PendingTouch = PendingTouch->PendingTouch;
			OldTouch->PendingTouch = NULL;
		}
	}
	unguard;
}

void APawn::startNewPhysics(FLOAT deltaTime, INT Iterations)
{
	if ( (deltaTime < 0.0003f) || (Iterations > 7) )
		return;
	switch (Physics)
	{
		case PHYS_Walking: physWalking(deltaTime, Iterations); break;
		case PHYS_Falling: physFalling(deltaTime, Iterations); break;
		case PHYS_Projectile: physProjectile( deltaTime, Iterations ); break;
		case PHYS_Flying: physFlying(deltaTime, Iterations); break;
		case PHYS_RootMotion: physRootMotion(deltaTime); break;
#ifdef WITH_KARMA
        case PHYS_Karma: physKarma(deltaTime); break;
        case PHYS_KarmaRagDoll: physKarmaRagDoll(deltaTime); break;
#endif
	}
}

int AActor::fixedTurn(int current, int desired, int deltaRate)
{
	guard(AActor::fixedTurn);

	if (deltaRate == 0)
		return (current & 65535);

	int result = current & 65535;
	current = result;
	desired = desired & 65535;

	if (bFixedRotationDir)
	{
		if (bRotateToDesired)
		{
			if (deltaRate > 0)
			{
				if (current > desired)
					desired += 65536;
				result += Min(deltaRate, desired - current);
			}
			else
			{
				if (current < desired)
					current += 65536;
				result += ::Max(deltaRate, desired - current);
			}
		}
		else
			result += deltaRate;
	}
	else if (bRotateToDesired)
	{
		if (current > desired)
		{
			if (current - desired < 32768)
				result -= Min((current - desired), Abs(deltaRate));
			else
				result += Min((desired + 65536 - current), Abs(deltaRate));
		}
		else
		{
			if (desired - current < 32768)
				result += Min((desired - current), Abs(deltaRate));
			else
				result -= Min((current + 65536 - desired), Abs(deltaRate));
		}
	}
	return (result & 65535);
	unguard;
}

/* FindSlopeRotation()
return a rotation that will leave actor pointed in desired direction, and placed snugly against its floor
*/
FRotator AActor::FindSlopeRotation(const FVector& FloorNormal, FRotator NewRotation)
{
	guard(AActor::FindSlopeRotation);

	if ( (FloorNormal.Z < 0.95) && !FloorNormal.IsNearlyZero() )
	{
		FRotator TempRot = NewRotation;
		// allow yawing, but pitch and roll fixed based on wall
		TempRot.Pitch = 0;
		FVector YawDir = TempRot.Vector();
		FVector PitchDir = YawDir - FloorNormal * (YawDir | FloorNormal);
		TempRot = PitchDir.Rotation();
		NewRotation.Pitch = TempRot.Pitch;
		FVector RollDir = PitchDir ^ FloorNormal;
		TempRot = RollDir.Rotation();
		NewRotation.Roll = TempRot.Pitch;
	}
	else
		return FRotator(0,NewRotation.Yaw, 0);

	return NewRotation;
	unguard;
}

void APawn::physicsRotation(FLOAT deltaTime, const FVector& OldVelocity)
{
	guard(APawn::physicsRotation);

	// Accumulate a desired new rotation.
	FRotator NewRotation = Rotation;	

	INT bPhysOnFloor = (Physics == PHYS_Walking) || (Physics == PHYS_RootMotion);
	bFixedRotationDir = 0;
	INT deltaYaw	= 0;
	INT deltaPitch	= 0;
	if ( IsHumanControlled() )
	{
		bRotateToDesired = Controller->bRotateToDesired;

		if ( bRotateToDesired )
		{
			// auto-rotate player to DesiredRotation (for auto-aiming)
			APlayerController *C = (APlayerController*)Controller;
			deltaYaw	= appRound(2 * C->EnemyTurnSpeed.Yaw	* deltaTime);
			deltaPitch	= appRound(2 * C->EnemyTurnSpeed.Pitch	* deltaTime);
			DesiredRotation.Pitch	= Controller->DesiredRotation.Pitch;
			DesiredRotation.Yaw		= Controller->DesiredRotation.Yaw;
		}
	}
	else
	{
		bRotateToDesired = (Controller != NULL); //Pawns always have a "desired" rotation
		if ( bRotateToDesired && Controller->Enemy && (Controller->Focus == Controller->Enemy) )
		{
			if ( Controller->bEnemyAcquired && (Controller->Enemy->Visibility > 1) )
				deltaYaw = appRound(::Max(RotationRate.Yaw, Controller->RotationRate.Yaw) * deltaTime);
			else
				deltaYaw = appRound(Controller->AcquisitionYawRate * deltaTime);
			INT YawDiff = Abs((Rotation.Yaw & 65535) - DesiredRotation.Yaw);
			if ( (YawDiff < 2048) || (YawDiff > 63287) )
				Controller->bEnemyAcquired = true;
		}
		else
		{
			INT YawDiff = Abs((Rotation.Yaw & 65535) - DesiredRotation.Yaw);
			if ( YawDiff > 32768 )
				YawDiff -= 32768;
			deltaYaw = Clamp(2*YawDiff, RotationRate.Yaw, 2*RotationRate.Yaw);
			deltaYaw = appRound(deltaYaw * deltaTime);
		}
	}

	if ( bRotateToDesired )
	{
		//YAW
		if ( DesiredRotation.Yaw != NewRotation.Yaw )
			NewRotation.Yaw = fixedTurn(NewRotation.Yaw, DesiredRotation.Yaw, deltaYaw);

		// PITCH
		if ( !bRollToDesired && (bPhysOnFloor || (Physics == PHYS_Falling)) )
			DesiredRotation.Pitch = 0;
		if ( DesiredRotation.Pitch != NewRotation.Pitch )
			NewRotation.Pitch = fixedTurn(NewRotation.Pitch, DesiredRotation.Pitch, deltaYaw);

		if ( Controller->bRotateToDesired )
		{
			Controller->Rotation.Yaw = NewRotation.Yaw;
			Controller->Rotation.Pitch = fixedTurn(Controller->Rotation.Pitch, Controller->DesiredRotation.Pitch,deltaPitch);
		}
	}

	//ROLL
	if ( bRollToDesired )
	{
		if ( DesiredRotation.Roll != NewRotation.Roll )
			NewRotation.Roll = fixedTurn(NewRotation.Roll, DesiredRotation.Roll, deltaYaw);
	}
	else if ( RotationRate.Roll > 0 )
	{
		NewRotation.Roll = NewRotation.Roll & 65535;
		if ( NewRotation.Roll < 32768 )
		{
			if ( NewRotation.Roll > RotationRate.Roll )
				NewRotation.Roll = RotationRate.Roll;
		}
		else if ( NewRotation.Roll < 65536 - RotationRate.Roll )
			NewRotation.Roll = 65536 - RotationRate.Roll;
		//pawns roll based on physics
		if ( (Physics == PHYS_Walking) && (Velocity.SizeSquared() < 40000.f) )
		{
			FLOAT SmoothRoll = Min(1.f, 8.f * deltaTime);
			if (NewRotation.Roll < 32768)
				NewRotation.Roll = (INT) (NewRotation.Roll * (1 - SmoothRoll));
			else
				NewRotation.Roll = (INT) (NewRotation.Roll + (65536 - NewRotation.Roll) * SmoothRoll);
		}
		else
		{
			FVector RealAcceleration = (Velocity - OldVelocity)/deltaTime;
			if (RealAcceleration.SizeSquared() > 10000.f)
			{
				FLOAT MaxRoll = 28000.f;
				if ( Physics == PHYS_Walking )
					MaxRoll = 4096.f;
				NewRotation.Roll = 0;

				RealAcceleration = RealAcceleration.TransformVectorBy(GMath.UnitCoords/NewRotation); //y component will affect roll

				if (RealAcceleration.Y > 0)
					NewRotation.Roll = Min(RotationRate.Roll, (int)(RealAcceleration.Y * MaxRoll/AccelRate));
				else
					NewRotation.Roll = ::Max(65536 - RotationRate.Roll, (int)(65536.f + RealAcceleration.Y * MaxRoll/AccelRate));

				//smoothly change rotation
				Rotation.Roll = Rotation.Roll & 65535;
				if (NewRotation.Roll > 32768)
				{
					if (Rotation.Roll < 32768)
						Rotation.Roll += 65536;
				}
				else if (Rotation.Roll > 32768)
					Rotation.Roll -= 65536;
	
				FLOAT SmoothRoll = Min(1.f, 5.f * deltaTime);
				NewRotation.Roll = (INT) (NewRotation.Roll * SmoothRoll + Rotation.Roll * (1 - SmoothRoll));
			}
			else
			{
				FLOAT SmoothRoll = Min(1.f, 8.f * deltaTime);
				if (NewRotation.Roll < 32768)
					NewRotation.Roll = (INT) (NewRotation.Roll * (1 - SmoothRoll));
				else
					NewRotation.Roll = (INT) (NewRotation.Roll + (65536 - NewRotation.Roll) * SmoothRoll);
			}
		}
	}
	else
		NewRotation.Roll = 0;

	// Set the new rotation.
	if( NewRotation != Rotation )
	{
		FCheckResult Hit(1.f);
		GetLevel()->MoveActor( this, FVector(0,0,0), NewRotation, Hit );
	}
	unguard;
}

void AActor::physicsRotation(FLOAT deltaTime)
{
	guard(AActor::physicsRotation);
	
	if ( (!bRotateToDesired && !bFixedRotationDir)
		|| (bRotateToDesired && (Rotation == DesiredRotation)) )
		return;

	// Accumulate a desired new rotation.
	FRotator NewRotation = Rotation;	
	FRotator deltaRotation = RotationRate * deltaTime;

	//YAW
	if ( (deltaRotation.Yaw != 0) && (!bRotateToDesired || (DesiredRotation.Yaw != NewRotation.Yaw)) )
		NewRotation.Yaw = fixedTurn(NewRotation.Yaw, DesiredRotation.Yaw, deltaRotation.Yaw);
	//PITCH
	if ( (deltaRotation.Pitch != 0) && (!bRotateToDesired || (DesiredRotation.Pitch != NewRotation.Pitch)) )
		NewRotation.Pitch = fixedTurn(NewRotation.Pitch, DesiredRotation.Pitch, deltaRotation.Pitch);
	//ROLL
	if ( (deltaRotation.Roll != 0) && (!bRotateToDesired || (DesiredRotation.Roll != NewRotation.Roll)) )
		NewRotation.Roll = fixedTurn(NewRotation.Roll, DesiredRotation.Roll, deltaRotation.Roll);	

	// Set the new rotation.
	if( NewRotation != Rotation )
	{
		FCheckResult Hit(1.f);
		GetLevel()->MoveActor( this, FVector(0,0,0), NewRotation, Hit );
	}

	if ( bRotateToDesired && (Rotation == DesiredRotation) )
		eventEndedRotation(); //tell thing rotation ended

	unguard;
}

/*====================================================================================
physWalking()
*/
// if AI controlled, check for fall by doing trace forward
// try to find reasonable walk along ledge

FVector APawn::CheckForLedges(const FVector& AccelDir, const FVector& Delta, const FVector& GravDir, int &bCheckedFall, int &bMustJump )
{
	guard(APawn::CheckForLedges)

	//const DWORD TraceFlags = TRACE_AllBlocking | TRACE_StopAtFirstHit;
	const DWORD TraceFlags = TRACE_World | TRACE_StopAtFirstHit;
	FCheckResult Hit(1.f);
	if ( !Base )
	{
		GetLevel()->SingleLineCheck(Hit, this, Location - FVector(0.f,0.f,4.f), Location, TraceFlags, GetCylinderExtent());
		if ( Hit.Time == 1.f )
		{
			bMustJump = true;
			return Delta;
		}
	}

	const FVector ForwardCheck = AccelDir*CollisionRadius;

	// check if clear in front
	FVector Destn = Location + Delta + ForwardCheck;
	GetLevel()->SingleLineCheck(Hit, this, Destn, Location, TraceFlags);
	if (Hit.Time != 1.f)
		return Delta;

	// clear in front - see if there is footing at walk destination
	const FLOAT DesiredDist = Delta.Size();
	// check down enough to catch either step or slope
	const FLOAT TestDown = 4.f + CollisionHeight + ::Max( (float)UCONST_MAXSTEPHEIGHT, CollisionRadius + DesiredDist);
	// try a point trace
	GetLevel()->SingleLineCheck(Hit, this, Destn + TestDown * GravDir, Destn , TraceFlags);
	// if point trace hit nothing, or hit a steep slope, or below a normal step down, do a trace with extent
	if ( !bAvoidLedges )
		Destn = Location + Delta;
	if ( (Hit.Time == 1.f) || (Hit.Normal.Z < UCONST_MINFLOORZ)
		/*|| ((Hit.Time * TestDown > CollisionHeight + 4.f + Min<FLOAT>(UCONST_MAXSTEPHEIGHT,appSqrt(1.f - Hit.Normal.Z * Hit.Normal.Z) * (CollisionRadius + DesiredDist)/Hit.Normal.Z)))*/ )
	{
		GetLevel()->SingleLineCheck(Hit, this, Destn, Location, TraceFlags, GetCylinderExtent());
		if ( Hit.Time != 1.f )
			return Delta;
		GetLevel()->SingleLineCheck(Hit, this, Destn + GravDir * (UCONST_MAXSTEPHEIGHT + 4.f), Destn , TraceFlags, GetCylinderExtent());
	}
	if ( (Hit.Time == 1.f) || (Hit.Normal.Z < UCONST_MINFLOORZ) )
	{
		// We have a ledge!
		if ( Controller && Controller->StopAtLedge() )
			return FVector(0.f,0.f,0.f);

		// check which direction ledge goes
		const FVector DesiredDir = (Destn - Location).SafeNormal();
		const FVector SideDir(DesiredDir.Y, -1.f * DesiredDir.X, 0.f);
		
		// try left
		const FVector LeftSide = Destn + DesiredDist * SideDir;
		GetLevel()->SingleLineCheck(Hit, this, LeftSide, Destn, TraceFlags, GetCylinderExtent());
		if (Hit.Time == 1.f)
		{
			GetLevel()->SingleLineCheck(Hit, this, LeftSide + GravDir * (UCONST_MAXSTEPHEIGHT + 4.f), LeftSide , TraceFlags, GetCylinderExtent());
			if ( (Hit.Time < 1.f) && (Hit.Normal.Z >= UCONST_MINFLOORZ) )
			{
				// go left
				return (LeftSide - Location).SafeNormal() * DesiredDist;
				}
			}

		// try right
		const FVector RightSide = Destn - DesiredDist * SideDir;
		GetLevel()->SingleLineCheck(Hit, this, RightSide, Destn, TraceFlags, GetCylinderExtent());
		if ( Hit.Time == 1.f )
		{
			GetLevel()->SingleLineCheck(Hit, this, RightSide + GravDir * (UCONST_MAXSTEPHEIGHT + 4.f), RightSide , TraceFlags, GetCylinderExtent());
			if ( (Hit.Time < 1.f) && (Hit.Normal.Z >= UCONST_MINFLOORZ) )
			{
				// go right
				return (RightSide - Location).SafeNormal() * DesiredDist;
			}
		}

		// no available direction, so try to jump
		if ( !bCheckedFall && Controller && Controller->IsProbing(NAME_MayFall) )
		{
			bCheckedFall = 1;
			Controller->eventMayFall();
			bMustJump = bCanJump;
		}
	}
	return Delta;
	unguard;
}

void APawn::physWalking(FLOAT deltaTime, INT Iterations)
{
	guard(APawn::physWalking);

	if ( !Controller )
		return;
	
	//bound acceleration
	Velocity.Z = 0.f;
	Acceleration.Z = 0.f;
	FVector AccelDir = Acceleration.IsZero() ? Acceleration : Acceleration.SafeNormal();
	calcVelocity(AccelDir, deltaTime, GroundSpeed, PhysicsVolume->GroundFriction, 0, 1 );
	
	FVector DesiredMove = Velocity;
	// Add effect of velocity zone
	// Rather than constant velocity, hacked to make sure that velocity being clamped when walking doesn't
	// cause the zone velocity to have too much of an effect at fast frame rates
	if ( (PhysicsVolume->ZoneVelocity.SizeSquared() > 0)
		&& (IsHumanControlled() || (PhysicsVolume->ZoneVelocity.SizeSquared() > 10000.f)) )
		DesiredMove = DesiredMove + PhysicsVolume->ZoneVelocity * 25 * deltaTime;

	DesiredMove.Z = 0.f;

	//Perform the move
	FVector GravDir = (GetGravity().Z > 0) ? FVector(0.f,0.f,1.f) : FVector(0.f,0.f,-1.f);
	FVector Down = GravDir * (UCONST_MAXSTEPHEIGHT + 2.f);
	FCheckResult Hit(1.f);
	FVector OldLocation = Location;
	FVector OldFloor = Floor;
	AActor *OldBase = Base;
	bJustTeleported = 0;
	INT bCheckedFall = 0;
	INT bMustJump = 0;
	FLOAT remainingTime = deltaTime;

	while ( (remainingTime > 0.f) && (Iterations < 8) && Controller )
	{
		Iterations++;
		// subdivide moves to be no longer than 0.05 seconds
		FLOAT timeTick = (remainingTime > 0.05f) ? Min(0.05f, remainingTime * 0.5f) : remainingTime;
		remainingTime -= timeTick;
		FVector Delta = timeTick * DesiredMove;
		FVector subLoc = Location;

		if ( Delta.IsNearlyZero() )
			remainingTime = 0.f;
		else
		{
			// if AI controlled or walking player, avoid falls
			if ( Controller && Controller->WantsLedgeCheck() )
			{
				FVector subMove = Delta;
				Delta = CheckForLedges(AccelDir, Delta, GravDir, bCheckedFall, bMustJump);
				if ( Controller->MoveTimer == -1.f )
					remainingTime = 0.f;
			}

			// try to move forward
			GetLevel()->MoveActor(this, Delta, Rotation, Hit);

			if (Hit.Time < 1.f)
			{
				// hit a barrier, try to step up
				FLOAT DesiredDist = Delta.Size();
				FVector DesiredDir = Delta/DesiredDist;
				stepUp(GravDir, DesiredDir, Delta * (1.f - Hit.Time), Hit);
				if ( Physics == PHYS_Falling ) // pawn decided to jump up
				{
					FLOAT ActualDist = (Location - subLoc).Size2D();
					remainingTime += timeTick * (1 - Min(1.f,ActualDist/DesiredDist));
					eventFalling();
					if ( Physics == PHYS_Flying )
					{
						Velocity = FVector(0,0, AirSpeed);
						Acceleration = FVector(0,0,AccelRate);
					}

					//scion superville: Clear preland flag
					bPreLanded = 0;

					startNewPhysics(remainingTime,Iterations);
					return;
				}
			}
		}

		// check if imbedded in terrain
		GetLevel()->SingleLineCheck( Hit, this, Location - FVector(0.f,0.f,CollisionHeight), Location, TRACE_Level | TRACE_StopAtFirstHit );
		if ( Hit.Time < 1.f )
		{
			// raise player up
			GetLevel()->SingleLineCheck( Hit, this, Location - FVector(0.f,0.f,CollisionHeight), Location, TRACE_AllBlocking, FVector(CollisionRadius,CollisionRadius,2.f) );
			GetLevel()->MoveActor(this, FVector(0.f,0.f,2.f + CollisionHeight * (1.f - Hit.Time)),Rotation, Hit);
		}
		//drop to floor
		GetLevel()->SingleLineCheck( Hit, this, Location + Down, Location, TRACE_AllBlocking, GetCylinderExtent() );
		FLOAT FloorDist = Hit.Time * (UCONST_MAXSTEPHEIGHT + 2.f);
		Floor = Hit.Normal;
		if ( (Hit.Normal.Z < UCONST_MINFLOORZ) && !Delta.IsNearlyZero() && ((Delta | Hit.Normal) < 0) )
		{
			// slide down slope
			FVector Slide = FVector(0.f,0.f,UCONST_MAXSTEPHEIGHT) - Hit.Normal * (FVector(0.f,0.f,UCONST_MAXSTEPHEIGHT) | Hit.Normal);
			GetLevel()->MoveActor(this, -1 * Slide, Rotation, Hit);
			if ( (Hit.Actor != Base) && (Physics == PHYS_Walking) )
				SetBase(Hit.Actor, Hit.Normal);
		}
		else if( Hit.Time< 1.f && (Hit.Actor!=Base || FloorDist>MAXFLOORDIST) )
		{
			// move down to correct position
			GetLevel()->MoveActor(this, Down, Rotation, Hit);
			if ( (Hit.Actor != Base) && (Physics == PHYS_Walking) )
				SetBase(Hit.Actor, Hit.Normal);
		}
		else if ( FloorDist < MINFLOORDIST )
		{
			// move up to correct position (average of MAXFLOORDIST and MINFLOORDIST above floor)
			FVector realNorm = Hit.Normal;
			GetLevel()->MoveActor(this, FVector(0.f,0.f,0.5f*(MINFLOORDIST+MAXFLOORDIST) - FloorDist), Rotation, Hit);
			Hit.Time = 0.f;
			Hit.Normal = realNorm;
		}
		
		if( !bMustJump && Hit.Time<1.f && Hit.Normal.Z>=UCONST_MINFLOORZ )
		{
			if( (Hit.Normal.Z < 1.f) && ((Hit.Normal.Z * PhysicsVolume->GroundFriction) < 3.3f) )
			{
				// slide down slope, depending on friction and gravity
				FVector Slide = (deltaTime * GetGravity()/(2 * ::Max(0.5f, PhysicsVolume->GroundFriction))) * deltaTime;
				Delta = Slide - Hit.Normal * (Slide | Hit.Normal);
				if( (Delta | Slide) >= 0.f )
					GetLevel()->MoveActor(this, Delta, Rotation, Hit);
			}				
		}
		else
		{
			if ( !bMustJump && bCanJump && !bCheckedFall && Controller && Controller->IsProbing(NAME_MayFall) )
			{
				// give this pawn a chance to abort its fall
				bCheckedFall = 1;
				Controller->eventMayFall();
			}
			if ( !bJustTeleported && !bMustJump && !bCanJump )
			{
				// this pawn shouldn't fall, so undo its move
				Velocity = FVector(0.f,0.f,0.f);
				Acceleration = FVector(0.f,0.f,0.f);
				GetLevel()->FarMoveActor(this,OldLocation,false,false);
				if ( OldBase && (OldBase->bStatic || OldBase->bWorldGeometry || !OldBase->bMovable) )
				{
					SetBase(OldBase,OldFloor);
				}
				if ( Controller )
					Controller->MoveTimer = -1.f;
				return;
			}
			else if( Hit.Time > KINDA_SMALL_NUMBER )
			{
				// falling
				FLOAT DesiredDist = Delta.Size();
				FLOAT ActualDist = (Location - subLoc).Size2D();
				if (DesiredDist == 0.f)
					remainingTime = 0.f;
				else
					remainingTime += timeTick * (1.f - Min(1.f,ActualDist/DesiredDist));
				Velocity.Z = 0.f;
				eventFalling();
				if (Physics == PHYS_Walking)
					setPhysics(PHYS_Falling); //default if script didn't change physics

				//scion superville: Clear preland flag
				bPreLanded = 0;

				startNewPhysics(remainingTime,Iterations);
				return;
			}
		}
	}

	//scion superville: Allow touch events and such to change physics state and velocity
	if( Physics == PHYS_Walking ) 
	{
		if( bUseDesiredPrePivot )
		{
			// Only do Z prepivot lerp for now
			FVector FinalDelta = Location - OldLocation;
			PrePivot.Z -= FinalDelta.Z;
		}

		// make velocity reflect actual move
		if ( !bJustTeleported && !bNoVelocityUpdate )
			Velocity = (Location - OldLocation) / deltaTime;
		bNoVelocityUpdate = 0;
		Velocity.Z = 0.f;
	}

 	unguard;
}

/* calcVelocity()
Calculates new velocity and acceleration for pawn for this tick
bounds acceleration and velocity, adds effects of friction and momentum
// bBrake only for walking?
*/
void APawn::calcVelocity(const FVector& AccelDir, FLOAT deltaTime, FLOAT maxSpeed, FLOAT friction, INT bFluid, INT bBrake)
{
	guard(APawn::calcVelocity);

	FVector OldVelocity = Velocity;

	if ( bBrake && Acceleration.IsZero() )
	{
		FVector OldVel = Velocity;
		FVector SumVel = FVector(0,0,0);

		FLOAT RemainingTime = deltaTime;
		// subdivide braking to get reasonably consistent results at lower frame rates
		// (important for packet loss situations w/ networking)
		while( RemainingTime > 0.03f )
		{
			Velocity = Velocity - (2 * Velocity) * 0.03f * friction; //don't drift to a stop, brake
			if ( (Velocity | OldVel) > 0.f )
				SumVel += 0.03f * Velocity/deltaTime;
			RemainingTime -= 0.03f;
		}
		Velocity = Velocity - (2 * Velocity) * RemainingTime * friction; //don't drift to a stop, brake
		if ( (Velocity | OldVel) > 0.f )
			SumVel += RemainingTime * Velocity/deltaTime;
		Velocity = SumVel;
		if ( ((OldVel | Velocity) < 0.f)
			|| (Velocity.SizeSquared() < 100) )//brake to a stop, not backwards
			Velocity = FVector(0,0,0);
	}
	else
	{
		//scion superville: Get max speed based on current movement
		maxSpeed = CalcMaxSpeed( maxSpeed );

		FLOAT VelSize = Velocity.Size();
		if (Acceleration.SizeSquared() > AccelRate * AccelRate)
			Acceleration = AccelDir * AccelRate;
		Velocity = Velocity - (Velocity - AccelDir * VelSize) * deltaTime * friction;
	}

	Velocity = Velocity * (1 - bFluid * friction * deltaTime) + Acceleration * deltaTime;

	if ( !IsHumanControlled() )
		maxSpeed *= DesiredSpeed;

	if ( IsProbing(NAME_ModifyVelocity) )
		eventModifyVelocity(deltaTime,OldVelocity);
	if (Velocity.SizeSquared() > maxSpeed * maxSpeed)
	{
		Velocity = Velocity.SafeNormal();
		Velocity *= maxSpeed;
	}
	unguard;
}

void APawn::stepUp(const FVector& GravDir, const FVector& DesiredDir, FVector Delta, FCheckResult &Hit)
{
	guard(APawn::stepUp);

	FVector Down = GravDir * UCONST_MAXSTEPHEIGHT;

	if ( (Abs(Hit.Normal.Z) < MAXSTEPSIDEZ) || (Hit.Normal.Z >= UCONST_MINFLOORZ) )
	{
		// step up - treat as vertical wall
		GetLevel()->MoveActor(this, -1 * Down, Rotation, Hit);
		GetLevel()->MoveActor(this, Delta, Rotation, Hit);
	}
	else if ( Physics != PHYS_Walking )
	{
		 // slide up slope
		FLOAT Dist = Delta.Size();
		GetLevel()->MoveActor(this, Delta + FVector(0,0,Dist*Hit.Normal.Z), Rotation, Hit);
	}

	if (Hit.Time < 1.f)
	{
		ADecoration *PushDecor = NULL;
		if ( IsHumanControlled() )
			PushDecor = Cast<ADecoration>(Hit.Actor);
		if ( PushDecor && PushDecor->bPushable && ((Hit.Normal | DesiredDir) < -0.9) )
		{
			bNoVelocityUpdate = true;
			Velocity *= Mass/(Mass + PushDecor->Mass);
			processHitWall(Hit.Normal, PushDecor);
			if ( Physics == PHYS_Falling )
				return;
		}
		else if ( (Abs(Hit.Normal.Z) < MAXSTEPSIDEZ) && (Hit.Time * Delta.SizeSquared() > 144.f) )
		{
			// try another step
			GetLevel()->MoveActor(this, Down, Rotation, Hit);
			stepUp(GravDir, DesiredDir, Delta * (1 - Hit.Time), Hit);
			return;
		}

		// notify script that pawn ran into a wall
		processHitWall(Hit.Normal, Hit.Actor);
		if ( Physics == PHYS_Falling )
			return;

		//adjust and try again
		Hit.Normal.Z = 0;	// treat barrier as vertical;
		Hit.Normal = Hit.Normal.SafeNormal();
		FVector OriginalDelta = Delta;
		FVector OldHitNormal = Hit.Normal;
		Delta = (Delta - Hit.Normal * (Delta | Hit.Normal)) * (1.f - Hit.Time);
		if( (Delta | OriginalDelta) >= 0 )
		{
			GetLevel()->MoveActor(this, Delta, Rotation, Hit);
			if (Hit.Time < 1.f)
			{
				processHitWall(Hit.Normal, Hit.Actor);
				if ( Physics == PHYS_Falling )
					return;
				TwoWallAdjust(DesiredDir, Delta, Hit.Normal, OldHitNormal, Hit.Time);
				GetLevel()->MoveActor(this, Delta, Rotation, Hit);
			}
		}
	}
	GetLevel()->MoveActor(this, Down, Rotation, Hit);

	unguardf(( TEXT("(controller %d)"), Controller!=NULL ));
}

/* AActor::stepUp() used by MoveSmooth() to move smoothly up steps

*/
void AActor::stepUp(const FVector& GravDir, const FVector& DesiredDir, FVector Delta, FCheckResult &Hit)
{
	guard(AActor::stepUp);

	FVector Down = GravDir * UCONST_MAXSTEPHEIGHT;

	if ( Abs(Hit.Normal.Z) < MAXSTEPSIDEZ )
	{
		// step up - treat as vertical wall
		GetLevel()->MoveActor(this, -1 * Down, Rotation, Hit);
		GetLevel()->MoveActor(this, Delta, Rotation, Hit);
	}
	else
	{
		 // slide up slope
		FLOAT Dist = Delta.Size();
		GetLevel()->MoveActor(this, Delta + FVector(0,0,Dist*Hit.Normal.Z), Rotation, Hit);
	}

	if (Hit.Time < 1.f)
	{
		if ( (Abs(Hit.Normal.Z) < MAXSTEPSIDEZ) && (Hit.Time * Delta.SizeSquared() > 144.f) )
		{
			// try another step
			GetLevel()->MoveActor(this, Down, Rotation, Hit);
			stepUp(GravDir, DesiredDir, Delta * (1 - Hit.Time), Hit);
			return;
		}

		// notify script that actor ran into a wall
		processHitWall(Hit.Normal, Hit.Actor);
		if ( Physics == PHYS_Falling )
			return;

		//adjust and try again
		Hit.Normal.Z = 0;	// treat barrier as vertical;
		Hit.Normal = Hit.Normal.SafeNormal();
		FVector OriginalDelta = Delta;
		FVector OldHitNormal = Hit.Normal;
		Delta = (Delta - Hit.Normal * (Delta | Hit.Normal)) * (1.f - Hit.Time);
		if( (Delta | OriginalDelta) >= 0 )
		{
			GetLevel()->MoveActor(this, Delta, Rotation, Hit);
			if (Hit.Time < 1.f)
			{
				processHitWall(Hit.Normal, Hit.Actor);
				if ( Physics == PHYS_Falling )
					return;
				TwoWallAdjust(DesiredDir, Delta, Hit.Normal, OldHitNormal, Hit.Time);
				GetLevel()->MoveActor(this, Delta, Rotation, Hit);
			}
		}
	}
	GetLevel()->MoveActor(this, Down, Rotation, Hit);
	unguard;
}

void AActor::processHitWall(const FVector& HitNormal, AActor *HitActor)
{
	guard(AActor::processHitWall);

	if (!HitActor->IsAPawn())
	{
		eventHitWall(HitNormal, HitActor);
	}
	unguard;
}

void APawn::processHitWall(FVector HitNormal, AActor *HitActor)
{
	guard(APawn::processHitWall);

	if (!HitActor || HitActor->IsAPawn())
		return;
	if ( !bDirectHitWall && Controller )
	{
		//scion rwd - check to see if our controller cares about acceleration
		if ( Controller->bHitWallAccel && Acceleration.IsZero() )
		{
			//debugf(TEXT("no accel for hitwall for %s"),Controller->GetName());
			return;
		}
		FVector Dir = (Controller->Destination - Location).SafeNormal();
		if ( Physics == PHYS_Walking )
		{
			HitNormal.Z = 0;
			Dir.Z = 0;
			HitNormal = HitNormal.SafeNormal();
			Dir = Dir.SafeNormal();
		}
		if ( Controller->MinHitWall < (Dir | HitNormal) )
		{
			//debugf(TEXT("minhitwall for %s is %2.3f"),Controller->GetName(),(Dir|HitNormal));
			return;
		}
		// give controller the opportunity to handle the hitwall event instead of the controlled pawn
		if ( Controller->eventNotifyHitWall(HitNormal, HitActor) )
			return;
		if ( Physics != PHYS_Falling )
		{
			FCheckResult Hit(1.f);
			GetLevel()->MoveActor(this,FVector(0,0,-1.f * UCONST_MAXSTEPHEIGHT), Rotation, Hit);
			if ( Controller )
			Controller->AdjustFromWall(HitNormal, HitActor);
		}
	}
	eventHitWall(HitNormal, HitActor);
	unguard;
}

UBOOL AActor::ShrinkCollision(AActor *HitActor)
{
	return false;
}

UBOOL AProjectile::ShrinkCollision(AActor *HitActor)
{
	guardSlow(AProjectile::ShrinkCollision);

	if ( bSwitchToZeroCollision
		&& ((CollisionHeight != 0.f) || (CollisionRadius != 0.f))
		&& (!HitActor->bBlockZeroExtentTraces || (HitActor->IsA(AStaticMeshActor::StaticClass()) && ((AStaticMeshActor*)HitActor)->bExactProjectileCollision)) )
	{
		SetCollisionSize(0.f,0.f);
		ZeroCollider = HitActor;
		return true;
	}
	return false;
	unguardSlow;
}

void AActor::processLanded(const FVector& HitNormal, AActor *HitActor, FLOAT remainingTime, INT Iterations)
{
	guard(AActor::processLanded);

	if ( Location.Z < Region.Zone->KillZ )
		eventFellOutOfWorld(Region.Zone->KillZType);
	if ( bDeleteMe )
		return;
	if ( PhysicsVolume->bBounceVelocity && (PhysicsVolume->ZoneVelocity != FVector(0,0,0)) )
	{
		Velocity = PhysicsVolume->ZoneVelocity + FVector(0,0,80);
		return;
	}

	eventLanded(HitNormal);
	if (Physics == PHYS_Falling)
	{
		setPhysics(PHYS_None, HitActor, HitNormal);
		Velocity = FVector(0,0,0);
	}
	if ( bOrientOnSlope && (Physics == PHYS_None) )
	{
		// rotate properly onto slope
		FCheckResult Hit(1.f);
		FRotator NewRotation = FindSlopeRotation(HitNormal,Rotation);
		GetLevel()->MoveActor(this, FVector(0,0,0), NewRotation, Hit);
	}
	unguard;
}

void ADecoration::processLanded(const FVector& HitNormal, AActor *HitActor, FLOAT remainingTime, INT Iterations)
{
	guard(ADecoration::processLanded);

	if ( Location.Z < Region.Zone->KillZ )
		eventFellOutOfWorld(Region.Zone->KillZType);
	if ( bDeleteMe )
		return;
	if ( PhysicsVolume->bBounceVelocity && (PhysicsVolume->ZoneVelocity != FVector(0,0,0)) )
	{
		Velocity = PhysicsVolume->ZoneVelocity + FVector(0,0,80);
		return;
	}
	if ( numLandings < 5 ) // make sure its on a valid landing
	{
		FCheckResult Hit(1.f);
		GetLevel()->SingleLineCheck(Hit, this, Location -  FVector(0,0,(CollisionHeight + CollisionRadius + 8)),
			Location - FVector(0,0,(0.8 * CollisionHeight)) , TRACE_ProjTargets|TRACE_StopAtFirstHit);
		if ( !Hit.Actor )
		{
			FVector partExtent = 0.5 * GetCylinderExtent();
			partExtent.Z *= 2;
			int bQuad1 = GetLevel()->SingleLineCheck(Hit, this, Location + FVector(0.5 * CollisionRadius, 0.5 * CollisionRadius, -8),
				Location + FVector(0.5 * CollisionRadius, 0.5 * CollisionRadius, 0), TRACE_AllBlocking|TRACE_StopAtFirstHit, partExtent);
			int bQuad2 = GetLevel()->SingleLineCheck(Hit, this, Location + FVector(-0.5 * CollisionRadius, 0.5 * CollisionRadius, -8),
				Location + FVector(-0.5 * CollisionRadius, 0.5 * CollisionRadius, 0), TRACE_AllBlocking|TRACE_StopAtFirstHit, partExtent);
			int bQuad3 = GetLevel()->SingleLineCheck(Hit, this, Location + FVector(-0.5 * CollisionRadius, -0.5 * CollisionRadius, -8),
				Location + FVector(-0.5 * CollisionRadius, -0.5 * CollisionRadius, 0), TRACE_AllBlocking|TRACE_StopAtFirstHit, partExtent);
			int bQuad4 = GetLevel()->SingleLineCheck(Hit, this, Location + FVector(0.5 * CollisionRadius, -0.5 * CollisionRadius, -8),
				Location + FVector(0.5 * CollisionRadius, -0.5 * CollisionRadius, 0), TRACE_AllBlocking|TRACE_StopAtFirstHit, partExtent);
			
			if ( (bQuad1 + bQuad2 + bQuad3 + bQuad4 > 1) && !(bQuad1 + bQuad3 == 0) && !(bQuad2 + bQuad4 == 0) )
			{
				numLandings++;
				Velocity = 2 * Clamp( -1.f * Velocity.Z, 30.f, 30.f + CollisionRadius) *
							FVector((FLOAT)(bQuad1 + bQuad4 - bQuad2 - bQuad3), (FLOAT)(bQuad1 + bQuad2 - bQuad3 - bQuad4) , 0.5);
				return;
			}
		}
		numLandings = 0;
	}
	else
		numLandings = 0;

	Super::processLanded(HitNormal,HitActor,remainingTime,Iterations);
	unguard;
}

void APawn::processLanded(const FVector& HitNormal, AActor *HitActor, FLOAT remainingTime, INT Iterations)
{
	guard(APawn::processLanded);

	//Check that it is a valid landing (not a BSP cut)
	FCheckResult Hit(1.f);
	GetLevel()->SingleLineCheck(Hit, this, Location -  FVector(0,0,0.2f * CollisionHeight + 8),
		Location, TRACE_Actors|TRACE_World|TRACE_StopAtFirstHit, 0.9f * GetCylinderExtent());

	if ( Hit.Time == 1.f ) //Not a valid landing
	{
		FVector Adjusted = Location;
		if ( GetLevel()->FindSpot(1.1f * GetCylinderExtent(), Adjusted) && (Adjusted != Location) )
		{
			GetLevel()->FarMoveActor(this, Adjusted, 0, 0);
			Velocity.X += appFrand() * 60 - 30;
			Velocity.Y += appFrand() * 60 - 30;
			return;
		}
	}

	Floor = HitNormal;
	if ( !Controller || !Controller->eventNotifyLanded(HitNormal) )
		eventLanded(HitNormal);
	if ( Physics == PHYS_Falling )
	{
		if ( Health > 0 )
			setPhysics(PHYS_Walking, HitActor, HitNormal);
		else
			setPhysics(PHYS_None, HitActor, HitNormal);
	}
	if ( Physics == PHYS_Walking )
		Acceleration = Acceleration.SafeNormal();
	startNewPhysics(remainingTime, Iterations);

	if ( !Controller && (Physics == PHYS_None) )
	{
		// rotate properly onto slope
		FCheckResult Hit(1.f);
		FRotator NewRotation = FindSlopeRotation(HitNormal,Rotation);
		GetLevel()->MoveActor(this, FVector(0,0,0), NewRotation, Hit);
	}
	
	//scion superville: Clear preland flag
	bPreLanded = 0;

	unguard;
}

FVector APawn::NewFallVelocity(const FVector& OldVelocity, const FVector& OldAcceleration, FLOAT timeTick)
{
	guard(APawn::NewFallVelocity);

  	FLOAT NetFluidFriction = 0.f;

	// FIXME THIS IS A BUG LEFT IN TO KEEP FALLING BEHAVIOUR CONSISTENT WITH PREVIOUS VERSIONS
	if( GetGravity().Z != Level->GetDefaultPhysicsVolume()->Gravity.Z ) {
  		NetFluidFriction = 0.5f * PhysicsVolume->FluidFriction;
  	}

	FVector NewVelocity = OldVelocity * (1.f - (NetFluidFriction * timeTick)) 
			+ 0.5f * OldAcceleration * timeTick;
	return NewVelocity;
	unguard;
}

void AActor::TwoWallAdjust(const FVector &DesiredDir, FVector &Delta, FVector &HitNormal, FVector &OldHitNormal, FLOAT HitTime)
{
	guard(AActor::TwoWallAdjust);

	if ((OldHitNormal | HitNormal) <= 0) //90 or less corner, so use cross product for dir
	{
		FVector NewDir = (HitNormal ^ OldHitNormal);
		NewDir = NewDir.SafeNormal();
		Delta = (Delta | NewDir) * (1.f - HitTime) * NewDir;
		if ((DesiredDir | Delta) < 0)
			Delta = -1 * Delta;
	}
	else //adjust to new wall
	{
		Delta = (Delta - HitNormal * (Delta | HitNormal)) * (1.f - HitTime);
		if ((Delta | DesiredDir) <= 0)
			Delta = FVector(0,0,0);
	}
	unguard;
}

void APawn::physFalling(FLOAT deltaTime, INT Iterations)
{
	guard(APawn::physFalling);

	//bound acceleration, falling object has minimal ability to impact acceleration
	FLOAT BoundSpeed = 0; //Bound final 2d portion of velocity to this if non-zero
	FVector RealAcceleration = Acceleration;
	FCheckResult Hit(1.f);

	// test for slope to avoid using air control to climb walls
	FLOAT TickAirControl = AirControl;
	Acceleration.Z = 0.f;
	if( TickAirControl > 0.05f )
	{
		FVector TestWalk = ( TickAirControl * AccelRate * Acceleration.SafeNormal() + Velocity ) * deltaTime;
		TestWalk.Z = 0;
		GetLevel()->SingleLineCheck( Hit, this, Location + TestWalk, Location, TRACE_World|TRACE_StopAtFirstHit, FVector( CollisionRadius, CollisionRadius, CollisionHeight ) );
		if( Hit.Actor )
			TickAirControl = 0.f;
	}

	// boost maxAccel to increase player's control when falling
	FLOAT maxAccel = AccelRate * TickAirControl;
	FVector Velocity2D = Velocity;
	Velocity2D.Z = 0;
	FLOAT speed2d = Velocity2D.Size2D();
	if ( (speed2d < 10.f) && (TickAirControl > 0.f) ) //allow initial burst
		maxAccel = maxAccel + (10 - speed2d)/deltaTime;
	else if ( speed2d >= GroundSpeed )
	{
		if ( TickAirControl <= 0.05f )
			maxAccel = 1.f;
		else
			BoundSpeed = speed2d;
	}

	if ( Acceleration.SizeSquared() > maxAccel * maxAccel )
	{
		Acceleration = Acceleration.SafeNormal();
		Acceleration *= maxAccel;
	}

	FLOAT remainingTime = deltaTime;
	FLOAT timeTick = 0.1f;
	FVector OldLocation = Location;

	while ( (remainingTime > 0.f) && (Iterations < 8) )
	{
		Iterations++;
		if (remainingTime > 0.05f)
			timeTick = Min(0.05f, remainingTime * 0.5f);
		else timeTick = remainingTime;

		remainingTime -= timeTick;
		OldLocation = Location;
		bJustTeleported = 0;

		FVector OldVelocity = Velocity;
		Velocity = NewFallVelocity(OldVelocity,Acceleration + GetGravity() + ConstantAcceleration,timeTick);
		if ( IsProbing(NAME_ModifyVelocity) )
			eventModifyVelocity(timeTick,OldVelocity);
		//epic moelfke: added OldVelocity.Z check so we don't call eventNotifyJumpApex() when it's not really the apex of a jump
		if (Controller && Controller->bNotifyApex && Velocity.Z <= 0.f && OldVelocity.Z > 0.f)
		{
			bJustTeleported = true;
			Controller->eventNotifyJumpApex();
			Controller->bJumpOverWall = false;
		}
		if (BoundSpeed != 0.f)
		{
			// using air control, so make sure not exceeding acceptable speed
			FVector Vel2D = Velocity;
			Vel2D.Z = 0;
			if ( Vel2D.SizeSquared() > BoundSpeed * BoundSpeed )
			{
				Vel2D = Vel2D.SafeNormal();
				Vel2D = Vel2D * BoundSpeed;
				Vel2D.Z = Velocity.Z;
				Velocity = Vel2D;
			}
		}
		
		UBOOL	bHitWall = 0;
		FVector Adjusted = (Velocity + PhysicsVolume->ZoneVelocity) * timeTick;

		GetLevel()->MoveActor(this, Adjusted, Rotation, Hit);
		if ( bDeleteMe )
			return;
		else if ( Hit.Time < 1.f )
		{
			//epic superville: If we hit a floor or we hit something immediately and weren't moving before
			// call landed event.
			if( Hit.Normal.Z >= UCONST_MINFLOORZ || 
				(Hit.Time > KINDA_SMALL_NUMBER && OldVelocity.IsNearlyZero()) )
			{
				remainingTime += timeTick * (1.f - Hit.Time);
				if (!bJustTeleported && (Hit.Time > 0.1f) && (Hit.Time * timeTick > 0.003f) )
					Velocity = (Location - OldLocation)/(timeTick * Hit.Time);
				processLanded(Hit.Normal, Hit.Actor, remainingTime, Iterations);
				return;
			}
			else
			{
				bHitWall = 1;

				//check(!Hit.Normal.IsZero());
				//scion rwd - added bPhysicsAirControl check
				if ( Controller != NULL &&
					 Controller->bPhysicsAirControl &&
					 RealAcceleration.IsZero() && Cast<AAIController>(Controller) )
				{
					// try aircontrol push
					Acceleration = Velocity;
					Acceleration.Z = 0.f;
					Acceleration = Acceleration.SafeNormal();
					Acceleration *= AccelRate;
					RealAcceleration = Acceleration;
				}
				else {
					processHitWall(Hit.Normal, Hit.Actor);
				}

				FVector OldHitNormal = Hit.Normal;

				FVector Dir   = (Hit.Normal * (Adjusted | Hit.Normal));
				FVector Delta = (Adjusted - Dir) * (1.f - Hit.Time);
				if( (Delta | Adjusted) >= 0.f )
				{
					//if ( Delta.Z > 0 ) // friction slows sliding up slopes
					//	Delta *= 0.5f;	// FIXME should this be gone forever?
					GetLevel()->MoveActor(this, Delta, Rotation, Hit);
					if (Hit.Time < 1.f) //hit second wall
					{
						// FIXME REMOVE
						if ( Hit.Normal.IsZero() )
							debugf(TEXT("Zero normal from hit actor %s"),Hit.Actor->GetName());
						//check(!Hit.Normal.IsZero());
						if ( Hit.Normal.Z >= UCONST_MINFLOORZ )
						{
							remainingTime = 0.f;
							processLanded(Hit.Normal, Hit.Actor, remainingTime, Iterations);
							return;
						}
						processHitWall(Hit.Normal, Hit.Actor);
						FVector DesiredDir = Adjusted.SafeNormal();
						TwoWallAdjust(DesiredDir, Delta, Hit.Normal, OldHitNormal, Hit.Time);
						int bDitch = ( (OldHitNormal.Z > 0.f) && (Hit.Normal.Z > 0.f) && (Delta.Z == 0.f) && ((Hit.Normal | OldHitNormal) < 0.f) );
						GetLevel()->MoveActor(this, Delta, Rotation, Hit);
						if ( bDitch || (Hit.Normal.Z >= UCONST_MINFLOORZ) )
						{
							remainingTime = 0.f;
							processLanded(Hit.Normal, Hit.Actor, remainingTime, Iterations);
							return;
						}
					}
				}
				FLOAT OldZ = OldVelocity.Z;
				OldVelocity = (Location - OldLocation)/timeTick;
				OldVelocity.Z = OldZ;
			}
		}

		if ( !bJustTeleported )
		{
			// refine the velocity by figuring out the average actual velocity over the tick, and then the final velocity.
			// This particularly corrects for situations where level geometry affected the fall.
			Velocity = (Location - OldLocation)/timeTick - PhysicsVolume->ZoneVelocity; //actual average velocity

			if ( (Velocity.Z < OldVelocity.Z) || (OldVelocity.Z >= 0.f) )
				Velocity = 2 * Velocity - OldVelocity; //end velocity has 2* accel of avg
			if (Velocity.SizeSquared() > PhysicsVolume->TerminalVelocity * PhysicsVolume->TerminalVelocity)
			{
				Velocity  = Velocity.SafeNormal();
				Velocity *= PhysicsVolume->TerminalVelocity;
			}

			//epic moelfke: check for jump apex again in case the above Velocity changes set Velocity.Z to less than 0
			if (Controller && Controller->bNotifyApex && Velocity.Z <= 0.f && OldVelocity.Z > 0.f)
			{
				bJustTeleported = true;
				Controller->eventNotifyJumpApex();
				Controller->bJumpOverWall = false;
			}

			//epic superville: Clamp velocity if hit wall
			if( bHitWall					&& 
				MaxHitWallVelocity > 0.f	&& 
				Velocity.SizeSquared() > MaxHitWallVelocity * MaxHitWallVelocity ) 
			{
				Velocity  = Velocity.SafeNormal();
				Velocity *= MaxHitWallVelocity;
			}
		}

		//scion superville: Predict Landing
		if( Velocity.Z < 0.f && Physics == PHYS_Falling ) {
			FCheckResult HitLanding(1.f);
			FVector Start = Location;	// Start trace from location b/c extent will take the edge of trace cyl to feet
			FVector End	  = Location - FVector(0,0,PreLandTraceDist);

			UBOOL bFoundGround = !GetLevel()->SingleLineCheck(  HitLanding,
																this,
																End,
																Start,
																TRACE_Level | TRACE_StopAtFirstHit,
																GetCylinderExtent() );
			
			if( !bPreLanded ) {
				// Haven't gotten preland event
				if( bFoundGround ) {
					// about to land on something - drop event
					FLOAT TimeToLand = GetLandingTime( Start, HitLanding.Location, Velocity );
					if( eventPreLand( TimeToLand ) ) {
						bPreLanded = 1;
					}						
				}
			}
			else {
				// Have gotten preland event already
				if( !bFoundGround ) {
					// But now no floor below us - end the preland anim
					bPreLanded = 0;
					eventPreLandEnd();					
				}
			}
		}
		else {
			bPreLanded = 0;
		}
	}

	Acceleration = RealAcceleration;
	unguard;
}

void AActor::physFalling(FLOAT deltaTime, INT Iterations)
{
	guard(AActor::physFalling);

	if ( Location.Z < (Region.Zone->bSoftKillZ ? Region.Zone->KillZ - 2500.f : Region.Zone->KillZ) )
	{
		eventFellOutOfWorld(Region.Zone->KillZType);
		return;
	}

	if ( (Region.ZoneNumber == 0) && !bIgnoreOutOfWorld )
	{
		// not in valid spot
		if ( (Role == ROLE_Authority)
			&& (IsA(APickup::StaticClass()) || IsA(ADecoration::StaticClass())) )
			debugf( TEXT("%s fell out of the world at %f %f %f!"), GetFullName(), Location.X, Location.Y, Location.Z );
		eventFellOutOfWorld(KILLZ_None);
		return;
	}

	//bound acceleration, falling object has minimal ability to impact acceleration
	FVector RealAcceleration = Acceleration;
	FCheckResult Hit(1.f);
	FLOAT remainingTime = deltaTime;
	FLOAT timeTick = 0.1f;
	int numBounces = 0;
	FVector OldLocation = Location;

	while ( (remainingTime > 0.f) && (Iterations < 8) )
	{
		Iterations++;
		if (remainingTime > 0.05f)
			timeTick = Min(0.05f, remainingTime * 0.5f);
		else timeTick = remainingTime;

		remainingTime -= timeTick;
		OldLocation = Location;
		bJustTeleported = 0;

		FVector OldVelocity = Velocity;

		Velocity = OldVelocity * (1.f - (0.5f * PhysicsVolume->FluidFriction * timeTick)) + 0.5f * (Acceleration + GetGravity()) * timeTick;

		FVector Adjusted = (Velocity + PhysicsVolume->ZoneVelocity) * timeTick;

		GetLevel()->MoveActor(this, Adjusted, Rotation, Hit);
		if ( bDeleteMe )
			return;
		if ( (Hit.Time < 1.f) && ShrinkCollision(Hit.Actor) )
		{
			timeTick = timeTick * (1.f - Hit.Time);
			Adjusted = (Velocity + PhysicsVolume->ZoneVelocity) * timeTick;
			GetLevel()->MoveActor(this, Adjusted, Rotation, Hit);
		}
		if ( Hit.Time < 1.f )
		{
			ADecoration *D = Cast<ADecoration>(this);
			if (D && Hit.Actor->IsAPawn())
			{
				D->numLandings = ::Max(0, D->numLandings - 1);
			}
			if (bBounce)
			{
				eventHitWall(Hit.Normal, Hit.Actor);
				if ( Physics == PHYS_None )
					return;
				else if ( numBounces < 2 )
					remainingTime += timeTick * (1.f - Hit.Time);
				numBounces++;
			}
			else
			{
				if (Hit.Normal.Z >= UCONST_MINFLOORZ)
				{
					remainingTime += timeTick * (1.f - Hit.Time);
					if (!bJustTeleported && (Hit.Time > 0.1f) && (Hit.Time * timeTick > 0.003f) )
						Velocity = (Location - OldLocation)/(timeTick * Hit.Time);
					processLanded(Hit.Normal, Hit.Actor, remainingTime, Iterations);
					return;
				}
				else
				{
					processHitWall(Hit.Normal, Hit.Actor);
					FVector OldHitNormal = Hit.Normal;
					FVector Delta = (Adjusted - Hit.Normal * (Adjusted | Hit.Normal)) * (1.f - Hit.Time);
					if( (Delta | Adjusted) >= 0 )
					{
						if ( Delta.Z > 0 ) // friction slows sliding up slopes
							Delta *= 0.5;
						GetLevel()->MoveActor(this, Delta, Rotation, Hit);
						if (Hit.Time < 1.f) //hit second wall
						{
							if ( Hit.Normal.Z >= UCONST_MINFLOORZ )
							{
								remainingTime = 0.f;
								processLanded(Hit.Normal, Hit.Actor, remainingTime, Iterations);
								return;
							}
							else
								processHitWall(Hit.Normal, Hit.Actor);
		
							FVector DesiredDir = Adjusted.SafeNormal();
							TwoWallAdjust(DesiredDir, Delta, Hit.Normal, OldHitNormal, Hit.Time);
							int bDitch = ( (OldHitNormal.Z > 0) && (Hit.Normal.Z > 0) && (Delta.Z == 0) && ((Hit.Normal | OldHitNormal) < 0) );
							GetLevel()->MoveActor(this, Delta, Rotation, Hit);
							if ( bDitch || (Hit.Normal.Z >= UCONST_MINFLOORZ) )
							{
								remainingTime = 0.f;
								processLanded(Hit.Normal, Hit.Actor, remainingTime, Iterations);
								return;
							}
						}
					}
					FLOAT OldZ = OldVelocity.Z;
					OldVelocity = (Location - OldLocation)/timeTick;
					OldVelocity.Z = OldZ;
				}
			}
		}

		if (!bBounce && !bJustTeleported)
		{
			// refine the velocity by figuring out the average actual velocity over the tick, and then the final velocity.
			// This particularly corrects for situations where level geometry affected the fall.
			Velocity = (Location - OldLocation)/timeTick - PhysicsVolume->ZoneVelocity; //actual average velocity
			if ( (Velocity.Z < OldVelocity.Z) || (OldVelocity.Z >= 0) )
				Velocity = 2 * Velocity - OldVelocity; //end velocity has 2* accel of avg
			if (Velocity.SizeSquared() > PhysicsVolume->TerminalVelocity * PhysicsVolume->TerminalVelocity)
			{
				Velocity = Velocity.SafeNormal();
				Velocity *= PhysicsVolume->TerminalVelocity;
			}
		}
	}

	Acceleration = RealAcceleration;
	unguard;
}

void APawn::physFlying(FLOAT deltaTime, INT Iterations)
{
	guard(APawn::physFlying);

	FVector AccelDir;

	if ( bCollideWorld && (Region.ZoneNumber == 0) && !bIgnoreOutOfWorld )
	{
		// not in valid spot
		if ( !Controller || !Controller->bIsPlayer )
		{
			debugf( TEXT("%s flew out of the world!"), GetName());
			GetLevel()->DestroyActor( this );
		}
		return;
	}

	if ( Acceleration.IsZero() )
		AccelDir = Acceleration;
	else
		AccelDir = Acceleration.SafeNormal();

	calcVelocity(AccelDir, deltaTime, AirSpeed, 0.5f * PhysicsVolume->FluidFriction, 1, 0 );

	Iterations++;
	FVector OldLocation = Location;
	bJustTeleported = 0;
	FVector ZoneVel;
	if ( IsHumanControlled() || (PhysicsVolume->ZoneVelocity.SizeSquared() > 90000) )
		ZoneVel = PhysicsVolume->ZoneVelocity;
	else
		ZoneVel = FVector(0,0,0);
	FVector Adjusted = (Velocity + ZoneVel) * deltaTime;
	FCheckResult Hit(1.f);
	GetLevel()->MoveActor(this, Adjusted, Rotation, Hit);
	if (Hit.Time < 1.f)
	{
		Floor = Hit.Normal;
		FVector GravDir = FVector(0,0,-1);
		if (GetGravity().Z > 0)
			GravDir.Z = 1;
		FVector DesiredDir = Adjusted.SafeNormal();
		FVector VelDir = Velocity.SafeNormal();
		FLOAT UpDown = GravDir | VelDir;
		if ( (Abs(Hit.Normal.Z) < 0.2f) && (UpDown < 0.5f) && (UpDown > -0.2f) )
		{
			FLOAT stepZ = Location.Z;
			stepUp(GravDir, DesiredDir, Adjusted * (1.f - Hit.Time), Hit);
			OldLocation.Z = Location.Z + (OldLocation.Z - stepZ);
		}
		else
		{
			processHitWall(Hit.Normal, Hit.Actor);
			//adjust and try again
			FVector OldHitNormal = Hit.Normal;
			FVector Delta = (Adjusted - Hit.Normal * (Adjusted | Hit.Normal)) * (1.f - Hit.Time);
			if( (Delta | Adjusted) >= 0 )
			{
				GetLevel()->MoveActor(this, Delta, Rotation, Hit);
				if (Hit.Time < 1.f) //hit second wall
				{
					processHitWall(Hit.Normal, Hit.Actor);
					TwoWallAdjust(DesiredDir, Delta, Hit.Normal, OldHitNormal, Hit.Time);
					GetLevel()->MoveActor(this, Delta, Rotation, Hit);
				}
			}
		}
	}
	else
		Floor = FVector(0.f,0.f,1.f);

	if ( !bJustTeleported && !bNoVelocityUpdate )
		Velocity = (Location - OldLocation) / deltaTime;
	bNoVelocityUpdate = 0;
	unguard;
}

//get as close to waterline as possible, staying on same side as currently
FVector APawn::findWaterLine(const FVector& InWater, const FVector& OutofWater)
{
	guard(APawn::findWaterLine);

	FCheckResult Hit(1.f);
	FMemMark Mark(GMem);
	FCheckResult* FirstHit = GetLevel()->MultiLineCheck
	(
		GMem,
		InWater,
		OutofWater,
		FVector(0,0,0),
		Level,
		TRACE_Volumes | TRACE_World,
		this
	);

	// Skip owned actors and return the one nearest actor.
	for( FCheckResult* Check = FirstHit; Check!=NULL; Check=Check->GetNext() )
	{
		if( !IsOwnedBy( Check->Actor ) )
		{
			if( Check->Actor->bWorldGeometry )
				return OutofWater;		// never hit a water volume
			else
			{
				APhysicsVolume *W = Cast<APhysicsVolume>(Check->Actor);
				if ( W && W->bWaterVolume )
				{
					FVector Dir = InWater - OutofWater;
					Dir = Dir.SafeNormal();
					if ( W == PhysicsVolume )
						return Check->Location + 0.1f * Dir;
					else
						return Check->Location - 0.1f * Dir;
				}
			}
		}
	}
	Mark.Pop();
	return OutofWater;
	unguard;
}

/*
Encompasses()
returns true if point is within the volume
*/
INT AVolume::Encompasses(FVector point)
{
	guard(AVolume::Encompasses);

	if ( !Brush )
		return 0;
	FCheckResult Hit(1.f);
//	debugf(TEXT("%s brush pointcheck %d at %f %f %f"),GetName(),!Brush->PointCheck(Hit,this,	point, FVector(0.f,0.f,0.f), 0), point.X, point.Y,point.Z);
	return !Brush->PointCheck(Hit,this,	point, FVector(0.f,0.f,0.f), 0);
	unguard;
}

/* PhysProjectile is tailored for projectiles
*/
void AActor::physProjectile(FLOAT deltaTime, INT Iterations)
{
	guard(AActor::physProjectile);

	if ( Location.Z < (Region.Zone->bSoftKillZ ? Region.Zone->KillZ - 2500.f : Region.Zone->KillZ) )
	{
		eventFellOutOfWorld(Region.Zone->KillZType);
		return;
	}

	FLOAT remainingTime = deltaTime;
	INT numBounces = 0;

	if ( (Region.ZoneNumber == 0) && !bIgnoreOutOfWorld )
	{
		GetLevel()->DestroyActor( this );
		return;
	}

	FVector OldLocation = Location;
	bJustTeleported = 0;
	FCheckResult Hit(1.f);
	if ( bCollideActors )
	{
		AProjectile *Proj = IsAProjectile() ? ((AProjectile*)this) : NULL;
		if ( Proj && Proj->ZeroCollider )
		{
			UBOOL bStillTouching = false;
			for ( INT i=0; i<Touching.Num(); i++ )
				if ( Touching(i) == Proj->ZeroCollider )
				{
					bStillTouching = true;
					break;
				}
			if ( !bStillTouching )
			{
				Proj->ZeroCollider = NULL;
				SetCollisionSize(GetClass()->GetDefaultActor()->CollisionRadius,GetClass()->GetDefaultActor()->CollisionHeight);
			}

		}
	}

	while ( remainingTime > 0.f )
	{
		Iterations++;
		if ( !Acceleration.IsZero() )
		{
			//debugf(TEXT("%s has acceleration!"),GetName());
			Velocity = Velocity	+ Acceleration * remainingTime;
			BoundProjectileVelocity();
		}

		FLOAT timeTick = remainingTime;
		remainingTime = 0.f;
		FVector Adjusted = Velocity * deltaTime;
		Hit.Time = 1.f;
		GetLevel()->MoveActor(this, Adjusted, Rotation, Hit);
		
		if( Hit.Time<1.f && !bDeleteMe && !bJustTeleported )
		{
			if ( ShrinkCollision(Hit.Actor) )
				remainingTime = timeTick * (1.f - Hit.Time);
			else
			{
				eventHitWall(Hit.Normal, Hit.Actor);
				if (bBounce)
				{
					if (numBounces < 2)
						remainingTime = timeTick * (1.f - Hit.Time);
					numBounces++;
					if (Physics == PHYS_Falling)
						physFalling(remainingTime, Iterations);
				}
			}
		}
	}
	unguard;
}

void AActor::BoundProjectileVelocity()
{
	guard(AActor::BoundProjectileVelocity);

	if ( !Acceleration.IsZero() && (Velocity.SizeSquared() > Acceleration.SizeSquared()) )
	{
		Velocity = Velocity.SafeNormal();
		Velocity *= Acceleration.Size();
	}
	unguard;
}
void AProjectile::BoundProjectileVelocity()
{
	guard(AProjectile::BoundProjectileVelocity);

	if ( Velocity.SizeSquared() > MaxSpeed * MaxSpeed )
	{
		Velocity = Velocity.SafeNormal();
		Velocity *= MaxSpeed;
	}
	unguard;
}

void AActor::physRootMotion(FLOAT deltaTime)
{
	guard(AActor::physRootMotion);

	USkeletalMesh *SkM = Cast<USkeletalMesh>(Mesh);
	if ( !SkM )
	{
		Velocity = FVector(0,0,0);
		Acceleration = Velocity;
		return;
	}
	SkM->MeshGetInstance(this);
	USkeletalMeshInstance *SkMInstance = Cast<USkeletalMeshInstance>(MeshInstance);

	if ( !SkMInstance )
	{
		Velocity = FVector(0,0,0);
		Acceleration = Velocity;
		return;
	}
	FVector OldLocation = Location;
	INT OldbCollideWorld = bCollideWorld;
	bCollideWorld = 0;
	FCheckResult Hit(1.f);

	//GetLevel()->MoveActor(this, SkMInstance->GetRootLocationDelta(), Rotation + SkMInstance->GetRootRotationDelta(), Hit);

	if (!bJustTeleported)
		Velocity = (Location - OldLocation) / deltaTime;
	bCollideWorld = OldbCollideWorld;
	
	unguard;
}

