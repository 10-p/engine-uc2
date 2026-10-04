#include "UCGameplay.h"
#include "UnPath.h"

IMPLEMENT_PACKAGE(UCGameplay);

// Register things.
#define NAMES_ONLY
#define AUTOGENERATE_NAME(name) UCGAMEPLAY_API FName UCGAMEPLAY_##name;
#define AUTOGENERATE_FUNCTION(cls,idx,name) IMPLEMENT_FUNCTION(cls,idx,name)
#include "UCGameplayClasses.h"
#undef AUTOGENERATE_FUNCTION
#undef AUTOGENERATE_NAME
#undef NAMES_ONLY

// Import natives
#define NATIVES_ONLY
#define NAMES_ONLY
#define AUTOGENERATE_NAME(name)
#define AUTOGENERATE_FUNCTION(cls,idx,name)
#include "UCGameplayClasses.h"
#undef AUTOGENERATE_FUNCTION
#undef AUTOGENERATE_NAME
#undef NATIVES_ONLY
#undef NAMES_ONLY

void RegisterNamesUCGameplay()
{
	#define NAMES_ONLY
	#define AUTOGENERATE_NAME(name) extern UCGAMEPLAY_API FName UCGAMEPLAY_##name; UCGAMEPLAY_##name=FName(TEXT(#name),FNAME_Intrinsic);
	#define AUTOGENERATE_FUNCTION(cls,idx,name)
	#include "UCGameplayClasses.h"
	#undef DECLARE_NAME
	#undef NAMES_ONLY
}

#if !__STATIC_LINK
	struct FUCGameplayInitManager
    {
        FUCGameplayInitManager()
        {
			RegisterNamesUCGameplay();
        }
    } UCGameplayInitManager;
#endif

IMPLEMENT_CLASS(AUCAIController);
IMPLEMENT_CLASS(AUCBot);
IMPLEMENT_CLASS(AUCPawn);
IMPLEMENT_CLASS(AUCPlayerController);
IMPLEMENT_CLASS(AUCProjectile);
IMPLEMENT_CLASS(AUCWeapon);
IMPLEMENT_CLASS(AUCWeap_Melee);
IMPLEMENT_CLASS(APickupNotifier);
IMPLEMENT_CLASS(AUCCombo);
IMPLEMENT_CLASS(AUCShield);
IMPLEMENT_CLASS(AUCHead);
IMPLEMENT_CLASS(AUCAmbient_Creature);
IMPLEMENT_CLASS(AUCManualJumpPad);
IMPLEMENT_CLASS(AUCWeaponAttachment);
IMPLEMENT_CLASS(UCharacterMemoryUsageCommandlet);

void AUCWeapon::execGetAnimInfo( FFrame& Stack, RESULT_DECL )
{
	guard(execGetAnimInfo);

	P_GET_NAME(Seq);
	P_GET_FLOAT_OPTX(Rate,1.f);
	P_GET_FLOAT_OPTX(CurFrame,0.f);
	P_FINISH;

	*(FLOAT*)Result = 0.f;

	// Brute force for now - ideally would like to use a TMap but script no liky tmaps
	for( INT i = 0; i < AnimList.Num(); i++ )
	{
		if( Seq == AnimList(i).Anim )
		{
			*(FAnimInfo*)Result = AnimList(i);
			return;
		}
	}	


/*		// If anim map uninitialized
		if( AnimMap.Num() == 0 )
		{
			// Loop the anim list
			for( INT i = 0; i < AnimList.Num(); i++ )
			{
				// Add each entry to hash
				AnimMap.Add( AnimList(i).Anim, &AnimList(i) );
			}	
		}

		FAnimInfo** Info = AnimMap.Find( Seq );

		// If info found, retrieve duration from it
		if( Info )
		{
			*(FLOAT*)Result = (*Info)->Duration * Rate;
		}
		else
		{
			debugf( TEXT("execGetAnimLength %s: Can't find anim %s"), GetName(), *Seq );
		}
		*/

	unguard;
}

//scion ==========================================================
//	Scion Function: execCalculateAimInfo
//	Author: superville
//	
//	Desription: Computes aiming info based on camera type, camera location,
//	and camera rotation.  Third person camera traces along camera path
//	to a world location (camera focus point) and then traces from the
//	weapon fire position to the camera focus point to get the position
//	that the player is aiming at.  It then assigns all the values
//	needed to ensure the weapon fires to that spot (and the information
//	about that spot.
//
//	Input:	 float DeltaTime - change in time
//	Network: Called both client and server
//	Notes:	 PlayerCalcView must be called so the server can compute camera postion
//			 Otherwise aiming will be inconsistent
//	Last Modified: 11/02/04 (moelfke)
// ===============================================================
void AUCPlayerController::execCalculateAimInfo( FFrame& Stack, RESULT_DECL )
{
	P_GET_FLOAT(DeltaTime);
	P_GET_UBOOL_OPTX(bUpdateOnly,0);
	P_FINISH;

	if( !Pawn || !Pawn->Weapon ) {
		CurrentAimFocus		= Location;
		CurrentAimLocation	= Location;
		CurrentAimRotation	= Rotation;
		CurrentAimNormal	= FVector(0,0,0);
		CurrentAimActor		= NULL;
		return;
	}

	FVector		 X, Y, Z, Start, End;
	FRotator	 AimRot;
	FLOAT		 Dist;
	FVector		 FireStart;
	FCheckResult Hit(1.0f);

	Dist = Pawn->Weapon->TraceDist;

	//debug
	INT			Idx = 0;

	// If only updating extra info (happens only on server)
	if( bUpdateOnly )
	{
		// Use CurrentAimRotation (which was sent by client) to get rest of info
		FCoords Coords = GMath.UnitCoords / CurrentAimRotation;
		Start	= Pawn->GetFireStart();
		End		= Start + Coords.XAxis * Dist;
		
		GetLevel()->SingleLineCheck(Hit, Pawn, End, Start, TRACE_World | TRACE_Camera, FVector(0,0,0));

		if( !Hit.Actor  ) {
			Hit.Location = End;
		}

		CurrentAimFocus		= Hit.Location;
		CurrentAimLocation	= Hit.Location;
		CurrentAimNormal	= Hit.Normal;
		CurrentAimActor		= Hit.Actor;
		return;
	}

	if( bThirdPerson && !Pawn->Weapon->bIsZoomed ) {
    	// Calculate view info on client and server
		eventPlayerCalcView( DeltaTime, Hit.Actor, Hit.Location, AimRot, 0 );

		// Trace from camera location along camera rotation to world space coord
		// HitLocation == Worldspace coord of camera focus
		FCoords Coords = GMath.UnitCoords / Rotation;
		FireStart = Pawn->GetFireStart();

		// Calcuate the ProjectedStart -- that is, the point along the view direction
		// that is directly above the pawn (this prevents objects between the pawn and
		// the camera from giving strange fire directions)
		// To get the projected start:
		// Project the CameraToPawn vector onto the camera view direction and add it to the camera location
		Start	 = (FireStart - CurrentCameraLocation).ProjectOnTo( Coords.XAxis ) + CurrentCameraLocation;
		End		 = Start + Coords.XAxis * Dist;

		GetLevel()->SingleLineCheck(Hit, Pawn, End, Start, TRACE_ProjTargets | TRACE_Camera, FVector(0,0,0));
		if( !Hit.Actor ) {
			Hit.Location = End;
		}
		else if (Hit.Actor->IsAPawn())
		{
			Hit.Location += (Hit.Actor->CollisionRadius * Pawn->Rotation.Vector());

			// Make sure we didn't hit the flat edge (or corner) of the cylinder (always adjust in a little)
			if( Hit.Location.Z > Hit.Actor->Location.Z )
			{
				Hit.Location.Z -= Hit.Actor->CollisionHeight * 0.3f;
			}
			else if( Hit.Location.Z < Hit.Actor->Location.Z )
			{
				Hit.Location.Z += Hit.Actor->CollisionHeight * 0.3f;
			}			
		}
		
		// Set current aim focus to be hit location of camera trace
		CurrentAimFocus = Hit.Location;

		//debug
		// Collect camera/aim info
		if( bGetCameraInfo ) {
			Idx = CameraInfoList.AddZeroed() - 1;
			CameraInfoList(Idx).DesiredCamLoc = DesiredCameraLocation;
			CameraInfoList(Idx).CurrentCamLoc = CurrentCameraLocation;
			CameraInfoList(Idx).CurrentCamRot = CurrentCameraRotation;
			CameraInfoList(Idx).CamTraceStart = Start;
			CameraInfoList(Idx).CamTraceHit	  = Hit.Location;
		}

// Skip trace from gun to world, assume camera trace is fine (ie doesn't go over wall)
/*
		// Trace from weapon fire start to camera Focus Point
		// HitLocation == Worldspace coord weapon will hit if fired
		// End is moved a bit farther away to make sure we hit whatever the camera hit
		// (Otherwise HitActor will be None and set CurrentAimActor to None)
		Start		 = FireStart;
		End			 = CurrentAimFocus + (CurrentAimFocus - FireStart).SafeNormal() * 10;
			
		GetLevel()->SingleLineCheck( Hit, Pawn, End, Start, TRACE_World | TRACE_Camera | TRACE_Material, FVector(0,0,0) );
		if( !Hit.Actor ) {
			Hit.Location = End;
		}
*/

		// Aim along fire line - smooth interp aim location
		CurrentAimLocation  = Hit.Location;
		CurrentAimRotation	= (CurrentAimLocation - FireStart).Rotation();
		CurrentAimNormal	= Hit.Normal;
		CurrentAimActor		= Hit.Actor;

		//debug
		// Collect camera/aim info
		if( bGetCameraInfo ) {
			CameraInfoList(Idx).WpnTraceStart = Start;
			CameraInfoList(Idx).WpnTraceHit	  = Hit.Location;
			CameraInfoList(Idx).AimLoc		  = CurrentAimLocation;
			CameraInfoList(Idx).AimRot		  = CurrentAimRotation;
		}
	}
	else {
		FCoords Coords = GMath.UnitCoords / Rotation;
		Start	= Pawn->GetFireStart();
		End		= Start + Coords.XAxis * Dist;
		
		GetLevel()->SingleLineCheck(Hit, Pawn, End, Start, TRACE_World | TRACE_Camera, FVector(0,0,0));

		if( !Hit.Actor  ) {
			Hit.Location = End;
		}

		CurrentAimRotation	= Rotation;
		CurrentAimFocus		= Hit.Location;
		CurrentAimLocation	= Hit.Location;
		CurrentAimNormal	= Hit.Normal;
		CurrentAimActor		= Hit.Actor;
	}
}

//scion ===========================================================
//	SCION Function: CalcThirdPersonView
//	Author: superville
//
//	Description: Calculates the third person camera location/rotation
//		and manages all of the current camera info
//
//	Input:	out Vector	CameraLocation - Location of view actor (and set to be camera location)
//			out Rotator CameraRotation - Set to be view direction of camera
//				float	Dist		   - Distance camera should be from pawn (ignored for 3rd person)
//	Output: Camera location / Camera rotation through out params
//	Network: Both server and client
//	Last Modified: 03/06/04 (superville)
// ================================================================
void AUCPlayerController::execCalcThirdPersonView( FFrame& Stack, RESULT_DECL )
{
	guard(execCalcThirdPersonView);
	P_GET_VECTOR_REF( CameraLocation );
	P_GET_ROTATOR_REF( CameraRotation );
	P_GET_FLOAT(Dist);
	P_GET_FLOAT(DeltaTime);
	P_FINISH;

	if( CameraActor ) {
		*CameraLocation = CameraActor->Location;
		*CameraRotation = CameraActor->Rotation;
		return;
	}
  
	APawn *viewPawn = Pawn;
	if (ViewTarget && ViewTarget->IsAPawn())
	{
		viewPawn = (APawn*)ViewTarget;
	}
	
	if( viewPawn )
	{
		FCheckResult Hit(1.0);
		FVector CameraExtent = GetCylinderExtent();
		FLOAT DistFromDesired = 0.f;
		UBOOL bComputeDesiredLocation = 1;
		FLOAT InterpSpeedScale = 1.f;
		
		//FLOAT OrigDeltaTime = DeltaTime;
		//DeltaTime = LastCameraDeltaTime + (OrigDeltaTime - LastCameraDeltaTime) * 0.2;
		
		//debug
		/*
		if( bDebugCameraTrace )
		{
			debugf( TEXT("Compute New DeltaTime: Last %f Passed %f New %f"), LastCameraDeltaTime, OrigDeltaTime, DeltaTime );
		}
		*/

		LastCameraDeltaTime = DeltaTime;
				
		// If viewPawn is standing still and camera hasn't rotated
		if (!bAlwaysUpdateCamera && viewPawn->Location == OldPawnLocation && Rotation == OldCameraRot)
		{
			// No need to compute desired location
			bComputeDesiredLocation = 0;
		}

		// Store view target location
		OldPawnLocation = viewPawn->Location;

		// === MANAGE CAMERA FLAGS === //
		// If camera was zoomed but it's not now, set camera location perfectly
		UBOOL	bSetLocation		= 0,
				bDirectToDesired	= 0;

		if( viewPawn->bJustZoomed ) {
			bSetLocation			= 1;
			viewPawn->bJustZoomed	= 0;
		}
		else if( bSetCameraLocation || 
				(GameReplicationInfo && GameReplicationInfo->bGameEnded) || 
				bViewBot ) 
		{
			// If the viewPawn just teleported -- Set camera location
			// Or if game is ended (or if cheating to view a bot)
			bSetLocation			= 1;
			static FName FuncName = FName(TEXT("ClearSetCameraFlag"), FNAME_Find);
			if( !FuncTimerIsActive( FuncName ) ) {
				SetFuncTimer( FuncName, 0.25, 0 );
			}
		}
		else if (Pawn && Pawn != ViewTarget)
		{
			bDirectToDesired = 1;
			DistFromDesired = -1.0;
		}
		else if( viewPawn->Base && viewPawn->Base->IsAMover() && !viewPawn->Base->Velocity.IsZero() ) {
			// If viewPawn is on mover
			// If camera already knew about this mover
			if( bCameraOnMover ) {
				if( DistFromDesired < 0.0 ) {
					// If was already on this mover and camera go to min desired location
					bSetLocation	 = 1;
				}
				else {
					// Otherwise, continue the direction interpolation
					bDirectToDesired = 1;
				}
			}
			else {
				// Otherwise, interp directly to desired location (no lazy camera)
				DistFromDesired		= -1.0;
				bDirectToDesired	= 1;
				bCameraOnMover		= 1;
			}
		}
		else if( bCameraOnMover ) 
		{
			// If not on a mover but the flag is set, unmark
			bCameraOnMover = 0;
		}

		// Get the camera yaw to translate the camera
		FRotator	ViewRotation = Rotation.Normalize();
		FRotator	RotAdjust(0, ViewRotation.Yaw, 0);

		// If we should compute desired location
		if( bComputeDesiredLocation )
		{
			// === COMPUTE CAMERA OFFSET === //
			// If looking down, move the camera between the mid and high position
			// by percentage of possible pitch
			if( ViewRotation.Pitch <= 0 ) 
			{
				DesiredCameraLocation = viewPawn->IdealCameraOffset_Mid +
								Abs(ViewRotation.Pitch / float(UCONST_CAMERA_MINPITCH_NORM)) *
									(viewPawn->IdealCameraOffset_High - viewPawn->IdealCameraOffset_Mid);
			}
			else 
			{
				// If looking up, move between mid and low position instead
				DesiredCameraLocation = viewPawn->IdealCameraOffset_Mid +
								Abs(ViewRotation.Pitch / float(UCONST_CAMERA_MAXPITCH_NORM)) *
									(viewPawn->IdealCameraOffset_Low - viewPawn->IdealCameraOffset_Mid);
			}

			// Worst camera location is the 1st person eye point
			FVector WorstLoc  = FVector(0, 0, viewPawn->BaseEyeHeight) + viewPawn->WalkBob + ShakeOffset + *CameraLocation;

			// Best location is the offset added to the worst location (rotated by view yaw of course)
			DesiredCameraLocation = DesiredCameraLocation.TransformVectorBy(GMath.UnitCoords * RotAdjust) + WorstLoc;

			// If desired location is too far away, accelerate the camera interp speed
			if( MaxCameraTravelDist > 0.f ) 
			{
				FLOAT DistSq = (DesiredCameraLocation - OldCameraLoc).SizeSquared();
				if( DistSq > Square(MaxCameraTravelDist) ) 
				{
					CameraInterpScale += DeltaTime;
				}
				else if( CameraInterpScale > 1.f ) 
				{
					CameraInterpScale = Max( 1.f, CameraInterpScale - DeltaTime );
				}				
			}

			// Figure out the best camera extent (Height cannot go over cylinder top from the worst location)
			CameraExtent.Z = Min<FLOAT>( CollisionHeight, (viewPawn->Location.Z + viewPawn->CollisionHeight) - WorstLoc.Z );

			FVector PawnDir = viewPawn->Rotation.Vector();

			DesiredCameraLocation = GetBestDesiredLocation( viewPawn, 
															DesiredCameraLocation, 
															WorstLoc, 
															CameraExtent, 
															bSetLocation || bDirectToDesired, 
															0 );

			// If not setting location - do further adjustments
			if( !bSetLocation ) 
			{
				FLOAT OldToWorst_DistSq = (WorstLoc - OldCameraLoc).ProjectOnTo( PawnDir ).SizeSquared();
				FLOAT DesToWorst_DistSq = (WorstLoc - DesiredCameraLocation).ProjectOnTo( PawnDir ).SizeSquared();

				// Do one more trace from the current desired location to the camera location 
				GetLevel()->SingleLineCheck( Hit, viewPawn, DesiredCameraLocation, OldCameraLoc, TRACE_World | TRACE_Camera, CameraExtent );
                
				// If something between the two slide the camera along that surface
				//	until it is no longer blocked (Ignore if camera started IN world geometry)
				if( Hit.Actor && Hit.Location != OldCameraLoc )
				{
					if( OldToWorst_DistSq >= DesToWorst_DistSq )
					{
						DesiredCameraLocation = OldCameraLoc + PawnDir * ((DesiredCameraLocation - OldCameraLoc) | PawnDir);
					}
					else
					{
						DesiredCameraLocation = DesiredCameraLocation + PawnDir * ((WorstLoc - DesiredCameraLocation) | PawnDir);
					}

					DesiredCameraLocation = GetBestDesiredLocation( viewPawn, 
															DesiredCameraLocation, 
															WorstLoc, 
															CameraExtent, 
															0, 
															1 );


					// Speed up the camera
					InterpSpeedScale = 2.f;
				}
			}
		}

		// If old position and desired are almost right on top of eachother
		if( (OldCameraLoc - DesiredCameraLocation).SizeSquared() < KINDA_SMALL_NUMBER ) 
		{
			// Just set camera location
			*CameraLocation = DesiredCameraLocation;
		}
		else 
		{
			// Use best location if just setting
			if( bSetLocation ) 
			{
				*CameraLocation	= DesiredCameraLocation;
			}
			else if( bDirectToDesired ) 
			{
				// If interpolating directly to desired location
				// Get vector between desired and old location
				FVector Delta = OldCameraLoc - DesiredCameraLocation;

				// If our current dist is less than zero, we just started to interp
				if( DistFromDesired < 0.0 ) {
					// So grab current distance from delta
					DistFromDesired	= Delta.Size();
				}

				// Get delta direction and compute new distance by moving shrinking distance by transition speed
				Delta			= Delta.SafeNormal();
				DistFromDesired	= DistFromDesired - (DeltaTime * CameraTransitionInterpSpeed);

				// If inside the min distance just set the location
				if( DistFromDesired <= 0.0 ) 
				{
					DistFromDesired	= -1.0;
					*CameraLocation	= DesiredCameraLocation;
				}
				else 
				{
					// Otherwise move camera directly toward desired location
					*CameraLocation	= DesiredCameraLocation + (Delta * DistFromDesired);
				}
			}
			else 
			{
				// Otherwise, interp to desired location
				// Get view axes and camera movement direction
				FCoords Coords = GMath.UnitCoords / Rotation;
				FVector OldToDesired   = (DesiredCameraLocation - OldCameraLoc).SafeNormal();

				// Figure out interp speed based on movement directions
				FVector CameraSpeed(0,0,0);
				FLOAT	ViewDotMove = 0.f;
				if( viewPawn->Weapon && viewPawn->Weapon->bMeleeWeapon ) {
					ViewDotMove		= Coords.XAxis | OldToDesired;
					CameraSpeed.X	= CameraMeleeInterpSpeed.X.GetRangeValue( (ViewDotMove + 1) * 0.5 );
					ViewDotMove		= Coords.YAxis | OldToDesired;
					CameraSpeed.Y	= CameraMeleeInterpSpeed.Y.GetRangeValue( (ViewDotMove + 1) * 0.5 );
					ViewDotMove		= Coords.ZAxis | OldToDesired;
					CameraSpeed.Z	= CameraMeleeInterpSpeed.Z.GetRangeValue( (ViewDotMove + 1) * 0.5 );
				}
				else {
					ViewDotMove		= Coords.XAxis | OldToDesired;
					CameraSpeed.X	= CameraInterpSpeed.X.GetRangeValue( (ViewDotMove + 1) * 0.5 );
					ViewDotMove		= Coords.YAxis | OldToDesired;
					CameraSpeed.Y	= CameraInterpSpeed.Y.GetRangeValue( (ViewDotMove + 1) * 0.5 );
					ViewDotMove		= Coords.ZAxis | OldToDesired;
					CameraSpeed.Z	= CameraInterpSpeed.Z.GetRangeValue( (ViewDotMove + 1) * 0.5 );
				}


				CameraSpeed *= viewPawn->CameraInterpScale;
				CameraSpeed *= CameraInterpScale;
				CameraSpeed *= InterpSpeedScale;

				// hack: if DeltaTime increased by too much, move the camera exactly the same as the previous frame
				// avoids hitches
				if (DeltaTime < LastCameraDeltaTime * 1.5f)
				{
					LastCameraDirSize = (DesiredCameraLocation - OldCameraLoc).Size();
				}

				MoveVectorTo(*CameraLocation, OldCameraLoc, DesiredCameraLocation, CameraSpeed * LastCameraDirSize, DeltaTime, false);

				//debug
				if( bDebugCameraTrace )
				{
//					debugf( TEXT("Delta %f Speed %f,%f,%f Time %f %d"), (*CameraLocation-OldCameraLoc).Size(), CameraSpeed.X, CameraSpeed.Y, CameraSpeed.Z, DeltaTime, DeltaTime == 0.f ? 1 : 0 );
				}
			}
		}

	/*
	//scion superville: Code that attempted to fix the problem of the camera readjusting your aim as
	//	it translates (interpolation) while the rotation stays fixed.
	//	Kept around so I can try to readdress the problem later.
			// Camera rotation faces the aiming focus
			// If we are over the roll threshold, allow coordinate space to roll to avoid gimbal lock
			if( Abs(ViewRotation.Pitch) > CAMERA_ROLL_THRESHOLD ) {
				// Use OrthoRotation to avoid gimbal lock when looking down/back toward viewPawn
				// X Axis is along direction to focus location from camera
				// Y Axis is kept from the controller
				// Z Axis is recomputed using cross product of X and Y axes
				GetAxes( Rotation, X, Y, Z );
				CameraRotation = OrthoRotation( Normal(CurrentAimFocus - CameraLocation), Y, Normal(X CROSS Y) );
			}
			else {
				// Otherwise, take direct rotation and only impose a roll if the controller has one
				CameraRotation		= Rotator(CurrentAimFocus - CameraLocation);
				CameraRotation.Roll = Rotation.Roll;
			}

			CameraRotation = LerpRotator( OldCameraRot, CameraRotation, 0.75 );
	*/

		*CameraRotation	 = Rotation;

		OldCameraLoc	 = *CameraLocation;
		OldCameraRot	 = *CameraRotation;

		// Check to see if viewPawn should toggle rendering if camera clips collision
		if( viewPawn->IsLocallyControlled() && viewPawn == Pawn )
		{
			FVector Delta	= *CameraLocation - viewPawn->Location;
			if( Square(Delta.Z) < Square( viewPawn->CollisionHeight * viewPawn->CameraNoRenderScale ) &&
				Delta.SizeSquared2D() < Square( viewPawn->CollisionRadius * viewPawn->CameraNoRenderScale ) ) 
			{
				viewPawn->bNoRender = true;
			}
			else 
			{
				viewPawn->bNoRender = false;
			}
		}
	}
	unguard;
}

FVector AUCPlayerController::GetBestDesiredLocation
( 
	APawn		*viewPawn, 
	FVector		CurrentLoc, 
	const FVector& WorstLoc, 
	const FVector& CameraExtent, 
	UBOOL		bKeepHit, 
	UBOOL		bFullProject
)
{
#define	MAX_TRACE_COUNT	 3

	FCheckResult Hit(1.0);
	INT TraceCount = 0;
	do 
	{
		// Trace from the worst location to desired to make sure nothing is in the way
		GetLevel()->SingleLineCheck( Hit, viewPawn, CurrentLoc, WorstLoc, TRACE_World | TRACE_Camera | TRACE_IgnoreExtent, CameraExtent );

		if( Hit.Actor )
		{
			// If locked and should be applying the lock
			// Interupt apply lock for a short time
			if( bIsLocked && bApplyLock )
			{
				static FName NAME_ReapplyLockClamp = FName(TEXT("ReapplyLockClamp"), FNAME_Find);
				bApplyLockClamp = false;
                SetFuncTimer( NAME_ReapplyLockClamp, 0.2f, 0 ); 
			}

			// If we are just setting camera location, don't shift Z value
			if( bKeepHit ) {
				CurrentLoc = Hit.Location;
			}
			else {
				// Otherwise, if we hit something on the way out
				FVector Vect;
				if( bFullProject )
				{
					Vect = CurrentLoc - WorstLoc;
				}
				else
				{
					Vect = CurrentLoc - Hit.Location;
				}

				// If hit ceiling
				if( Abs(Hit.Normal.Z) >= UCONST_MINFLOORZ )

				{
					// Figure out the point along the surface hit that is furthest away from the worst location 
					// while still being in line with the ideal
					CurrentLoc  = CurrentLoc - Hit.Normal * (Vect | Hit.Normal);
				}
				else
				{
					// Otherwise, collided with a wall.. just adjust Z along it
					CurrentLoc.X = Hit.Location.X;
					CurrentLoc.Y = Hit.Location.Y;
				}
			}

			// Move desired location away from hit location
			CurrentLoc += Hit.Normal * (CameraExtent * 1.1f);
		}

	} while( Hit.Actor && ++TraceCount < MAX_TRACE_COUNT );

	return CurrentLoc;
}

void AUCPawn::PostScriptDestroyed()
{
	guard(AUCPawn::PostScriptDestroyed);
	// clean up any alpha actor refs
	for (INT idx = 0; idx < AlphaActors.Num(); idx++)
	{
		// clear the actor and it's skins
		if (AlphaActors(idx).AlphaActor != NULL)
		{
			AlphaActors(idx).AlphaActor->Skins.Empty();
			AlphaActors(idx).AlphaActor = NULL;
		}
		// free unique shaders
		SavedRimLight = NULL;
		SavedHeadRimLight = NULL;
		if (UniqueSkinShader != NULL)
		{
			UniqueSkinShader->RimLight = NULL;
			delete UniqueSkinShader;
			UniqueSkinShader = NULL;
		}
		if (UniqueHeadSkinShader != NULL)
		{
			UniqueHeadSkinShader->RimLight = NULL;
			delete UniqueHeadSkinShader;
			UniqueHeadSkinShader = NULL;
		}
		// clear the base skins
		AlphaActors(idx).BaseSkins.Empty();
		// delete the final blends
		for (INT fbIdx = 0; fbIdx < AlphaActors(idx).FinalBlends.Num(); fbIdx++)
		{
			UFinalBlend *deleteMe = AlphaActors(idx).FinalBlends(fbIdx);
			if (deleteMe != NULL)
			{
				deleteMe->Material = NULL;
				delete deleteMe;
			}
		}
		AlphaActors(idx).FinalBlends.Empty();
		// delete the color modifiers
		for (INT cmIdx = 0; cmIdx < AlphaActors(idx).Modifiers.Num(); cmIdx++)
		{
			UColorModifier *deleteMe = AlphaActors(idx).Modifiers(cmIdx);
			if (deleteMe != NULL)
			{
				deleteMe->Material = NULL;
				delete deleteMe;
			}
		}
		AlphaActors(idx).Modifiers.Empty();
	}
	// and empty the array
	AlphaActors.Empty();
	// clear our skins jic
	Skins.Empty();
	Super::PostScriptDestroyed();
	unguard;
}

INT AUCPawn::GetAlphaActorIdx(AActor *actor)
{
	if (actor != NULL)
	{
		for (INT idx = 0; idx < AlphaActors.Num(); idx++)
		{
			if (AlphaActors(idx).AlphaActor == actor)
			{
				return idx;
			}
		}
	}
	return -1;
}

void AUCPawn::execGetAlphaActorIdx(FFrame &Stack, RESULT_DECL)
{
	P_GET_OBJECT(AActor,actor);
	P_FINISH;
	*(INT*)Result = GetAlphaActorIdx(actor);
}

void AUCPawn::execAddAlphaActor(FFrame &Stack, RESULT_DECL)
{
	guard(AUCPawn::execAddAlphaActor);
	P_GET_OBJECT(AActor,actor);
	P_GET_UBOOL_OPTX(bUseTeamColor,0);
	P_GET_UBOOL_OPTX(bCopySkins,1);
	P_FINISH;

	INT Idx = -1;
	if (!bPendingDelete)
	{
		if (actor != NULL &&
			!actor->bDeleteMe &&
			(actor->IsDTMesh() ||
			 actor->DrawType == DT_StaticMesh))
		{
			// check to see if it's already in the list
			Idx = GetAlphaActorIdx(actor);
			if (Idx == -1)
			{
				// if they haven't already setup the skins, copy them to the actor
				if( bCopySkins ) {
					actor->Skins.Empty();
					actor->CopyMaterialsToSkins(0);
				}

				// if this actor actually has skins to manipulate
				if (actor->Skins.Num() > 0 && actor->Skins(0) != NULL &&
					Cast<UPSSkinShader>(actor->Skins(0)))
				{
					// if this is our body,
					if (actor == this)
					{
						// check if we need to create the uniqueskinshader
						if (UniqueSkinShader == NULL)
						{
							UniqueSkinShader = (UPSSkinShader*)StaticConstructObject(actor->Skins(0)->GetClass(),
								GetTransientPackage(),NAME_None,RF_ScriptMask,actor->Skins(0),&Stack);
						}
						// scion sz - make sure we have a proper skin shader
						UPSSkinShader* ActorSkinShader = Cast<UPSSkinShader>( actor->Skins(0) );
						if( ActorSkinShader )
						{
							SavedRimLight = ActorSkinShader->RimLight;
							bBodyUseLightingForSpecular = ActorSkinShader->bUseLightingForSpecular;
						}
						else
						{
							SavedRimLight = NULL;
						}						
					}
					else
					if (bFlashHead &&
						actor == Head)
					{
						// check if we need to create the uniqueskinshader
						if (UniqueHeadSkinShader == NULL)
						{
							UniqueHeadSkinShader = (UPSSkinShader*)StaticConstructObject(actor->Skins(0)->GetClass(),
								GetTransientPackage(),NAME_None,RF_ScriptMask,actor->Skins(0),&Stack);
						}
						SavedHeadRimLight = UniqueHeadSkinShader->RimLight;
						bHeadUseLightingForSpecular = UniqueHeadSkinShader->bUseLightingForSpecular;
					}
				}

				// go ahead and add it to the list
				Idx = AlphaActors.AddZeroed();
				AlphaActors(Idx).AlphaActor = actor;
				AlphaActors(Idx).bUseTeamColor = bUseTeamColor;

				// copy the current skins
				AlphaActors(Idx).BaseSkins.AddZeroed(actor->Skins.Num());
				AlphaActors(Idx).FinalBlends.AddZeroed(actor->Skins.Num());
				AlphaActors(Idx).Modifiers.AddZeroed(actor->Skins.Num());

				// create the final blends and modifiers for this actor as necessary
				for (INT i = 0; i < actor->Skins.Num(); i++)
				{
					if (actor == this &&
						i == 0)
					{
						AlphaActors(Idx).BaseSkins(i) = UniqueSkinShader;
					}
					else
					if (bFlashHead &&
						actor == Head &&
						i == 0)
					{
						AlphaActors(Idx).BaseSkins(i) = UniqueHeadSkinShader;
					}
					else
					{
						AlphaActors(Idx).BaseSkins(i) = actor->Skins(i);

						if( actor->IsAWeaponAttachment() )
						{
							// create a new final blend
							AlphaActors(Idx).FinalBlends(i) = (UFinalBlend*)StaticConstructObject(UFinalBlend::StaticClass(),GetTransientPackage(),NAME_None,RF_ScriptMask,NULL,&Stack);
							AlphaActors(Idx).FinalBlends(i)->FrameBufferBlending = FB_AlphaBlend;
							AlphaActors(Idx).FinalBlends(i)->ZWrite = true;
							AlphaActors(Idx).FinalBlends(i)->AlphaTest = false;
						}
					}

					// create a new modifier
					AlphaActors(Idx).Modifiers(i) = (UColorModifier*)StaticConstructObject(UColorModifier::StaticClass(),GetTransientPackage(),NAME_None,RF_ScriptMask,NULL,&Stack);
				}
			}
			else
			{
				// make sure to update entry
				AlphaActors(Idx).bUseTeamColor = bUseTeamColor;
			}
		}
	}

	*(INT*)Result = Idx;
	unguard;
}

void AUCPawn::UpdatePawnSpinePitch( FLOAT DeltaTime )
{
	INT		Pitch		= 0;
	FLOAT	ViewAlpha	= 0.f;
	FLOAT	TargetAlpha = 1.f;
	UBOOL	bLerpSpine	= 0;

	// If on locally controlled machine
	if( Controller ) {
		// Zero pitch if dead
		if( Health <= 0 ) {
            SpinePitch = 0;
		}
		else {
			if( IsHumanControlled() )
			{
				// If human compute pitch from controller rotation
			// Normalize the rotation
			Pitch = Controller->Rotation.Pitch & 65535;
			}
			else
			{
				// Otherwise, if a bot...
				// Always lerp SpinePitch to avoid pop
//				bLerpSpine = 1;

				// Compute pitch from direction to enemy
				if( Controller->Enemy )
				{
					FVector V = (Controller->Enemy->Location - Location);
					Pitch = V.Rotation().Pitch & 65535;
				}
				else
				{
					// Or zero the pitch
					Pitch = 0;
				}
			}

			// If pitch is looking upward
			if( Pitch <= UCONST_CAMERA_MAXPITCH ) {	
				// Set value on [0,128] range -> (level,max up)
				BYTE Target = Pitch / 128;

				if( bLerpSpine )
				{
					Target = Lerp<BYTE>( SpinePitch, Target, 0.2f );
				}
				
				SpinePitch = Target;
				ViewAlpha  = SpinePitch / 128.f;
			}
			else if( Pitch >= UCONST_CAMERA_MINPITCH ) {
				// Otherwise, if looking downward
				// Set value on [128,255] range -> (level, max down)
				BYTE Target = (Pitch - UCONST_CAMERA_MINPITCH) / 128;
				Target = 128 + ((1.f - (Target / 127.f)) * 127);
				
				if( bLerpSpine )
				{
					Target = Lerp<BYTE>( SpinePitch, Target, 0.2f );
				}

				SpinePitch = Target;
				ViewAlpha  = (SpinePitch - 128) / 128.f;
			}
		}
	}
	else {
		// Otherwise, remote client... compute pitch
		// If byte value is on lower half, looking up
		if( SpinePitch <= 128 ) {
			ViewAlpha	= SpinePitch / 128.f;
			Pitch		= UCONST_CAMERA_MAXPITCH_NORM * ViewAlpha;
		}
		else {
			// Otherwise, looking down
			ViewAlpha	= (SpinePitch - 128) / 128.f;
			Pitch		= UCONST_CAMERA_MINPITCH_NORM * ViewAlpha;
		}
	}

	// Normalize the pitch
	Pitch = Pitch & 0xFFFF; if( Pitch > 32767 ) Pitch -= 0x10000;

	// Get mesh instance
	USkeletalMeshInstance *MeshInst = NULL;
	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()) ) {
		MeshInst = (USkeletalMeshInstance*)Mesh->MeshGetInstance(this);
	}
	if( !MeshInst )
		return;

	//debug
	if( DebugSpinePitchAlpha != 0 )
	{
		ViewAlpha	= Clamp<FLOAT>( Abs(DebugSpinePitchAlpha), 0.0, 1.0 );
		Pitch		= 10;
		if( DebugSpinePitchAlpha < 0 )
		{
			Pitch = -10;
		}
	}

	// Target alpha is zero during a full body anim or 
	//	when have a melee weapon
	if( FullBodyID != 0 || WeaponAnimType == WAT_Melee )
	{
		TargetAlpha = 0.f;
	}

	// Loop through each bone to update
	for( INT i = 0; i < RotateBoneByView.Num(); i++ )
	{
		FRotateBoneByViewItem &Item = RotateBoneByView(i);

		// Make sure name is valid and weapon type matches (if in melee, always allow it to lerp to zero)
		if(  Item.BoneName == NAME_None || 
			(Item.WAT != WeaponAnimType && WeaponAnimType != WAT_Melee) )
		{
			continue;
		}

		// If target alpha is zero and we are already there - skip
		if( TargetAlpha == 0.f && Item.Alpha == 0.f )
		{
			continue;
		}

		// Calculate rotation based on alpha
		FRotator Rot(0,0,0);
		if( Pitch > 0 )
		{
			// If looking up, use Center -> Up range
			Rot += Item.Center + (Item.Up	- Item.Center) * ViewAlpha;
		}
		else 
		{
			// If looking down, use Center -> Down range
			Rot += Item.Center + (Item.Down - Item.Center) * ViewAlpha;		
		}
		
		// Update current alpha for this bone
		Item.Alpha = Lerp<FLOAT>( Item.Alpha, TargetAlpha, 0.1f );

		// Adjust bone rotation in local space
		MeshInst->SetBoneRotation( Item.BoneName, Rot, 0, Item.Alpha );
	}
}


/*-----------------------------------------------------------------------------
	Replication.
-----------------------------------------------------------------------------*/

#include "UnNet.h"

#if !FORCE_EXTREME_PACKING   // with this #define, BITFIELD is BYTE. --ryan.
static inline UBOOL NEQ(BITFIELD A,BITFIELD B,UPackageMap* Map,UActorChannel* Channel) {return A!=B;}
#endif

static inline UBOOL NEQ(BYTE A,BYTE B,UPackageMap* Map,UActorChannel* Channel) {return A!=B;}
static inline UBOOL NEQ(INT A,INT B,UPackageMap* Map,UActorChannel* Channel) {return A!=B;}
static inline UBOOL NEQ(FLOAT& A,FLOAT& B,UPackageMap* Map,UActorChannel* Channel) {return *(INT*)&A!=*(INT*)&B;}
static inline UBOOL NEQ(FVector& A,FVector& B,UPackageMap* Map,UActorChannel* Channel) {return ((INT*)&A)[0]!=((INT*)&B)[0] || ((INT*)&A)[1]!=((INT*)&B)[1] || ((INT*)&A)[2]!=((INT*)&B)[2];}
static inline UBOOL NEQ(FRotator& A,FRotator& B,UPackageMap* Map,UActorChannel* Channel) {return A.Pitch!=B.Pitch || A.Yaw!=B.Yaw || A.Roll!=B.Roll;}
static inline UBOOL NEQ(UObject* A,UObject* B,UPackageMap* Map,UActorChannel* Channel) {if( Map->CanSerializeObject(A) )return A!=B; Channel->bActorMustStayDirty = true;
//debugf(TEXT("%s Must stay dirty because of %s"),Channel->Actor->GetName(),A->GetName());
return (B!=NULL);}
static inline UBOOL NEQ(FName& A,FName B,UPackageMap* Map,UActorChannel* Channel) {return *(INT*)&A!=*(INT*)&B;}
static inline UBOOL NEQ(FColor& A,FColor& B,UPackageMap* Map,UActorChannel* Channel) {return *(INT*)&A!=*(INT*)&B;}
static inline UBOOL NEQ(FPlane& A,FPlane& B,UPackageMap* Map,UActorChannel* Channel) {return
((INT*)&A)[0]!=((INT*)&B)[0] || ((INT*)&A)[1]!=((INT*)&B)[1] ||
((INT*)&A)[2]!=((INT*)&B)[2] || ((INT*)&A)[3]!=((INT*)&B)[3];}

//jg -- Replaced the copy construction of both A & B with const ref
static inline UBOOL NEQ(const FString& A,const FString& B,UPackageMap* Map,UActorChannel* Channel)
{
	return A!=B;
}

//jg -- Replaced the copy construction of both A & B with const ref
static inline UBOOL NEQ(const FCompressedPosition& A, const FCompressedPosition& B,UPackageMap* Map,UActorChannel* Channel)
{
	return 1; // only try to replicate in compressed form if already know location has changed
}

static inline UBOOL NEQ(BYTE A[8], BYTE B[8], UPackageMap* Map,UActorChannel *Channel)
{
	for (INT idx = 0; idx < 8; idx++)
	{
		if (A[idx] != B[idx])
		{
			return 1;
		}
	}
	return 0;
}

INT* AUCPlayerController::GetOptimizedRepList( BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel )
{
	Ptr = Super::GetOptimizedRepList(Defaults,Retire,Ptr,Map,Channel);

	if( bNetDirty )
	{
		DOREPBOOL(UCPlayerController,bIsLocked);
		DOREP(UCPlayerController,MasterHijackKey,INT);
	}

	if( bNetOwner )
	{
		DOREPBOOL(UCPlayerController,bForceFaceEnemy);
	}

	return Ptr;
}

INT* AUCPawn::GetOptimizedRepList( BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel )
{
	Ptr = Super::GetOptimizedRepList(Defaults,Retire,Ptr,Map,Channel);

	if( bTearOff )
	{
		DOREPBOOL(UCPawn,bWasWalking);
	}

	if( bNetInitial || bNetDirty )
	{
		DOREP(UCPawn,FullBodyID,INT);
		DOREP(UCPawn,UpperBodyID,INT);
		DOREP(UCPawn,FullBodyCnt,INT);
		DOREP(UCPawn,UpperBodyCnt,INT);
		DOREP(UCPawn,WeaponAnimType,BYTE);
	}

	if( bNetDirty )
	{
		DOREP(UCPawn,DamageFlashCount,BYTE);
		DOREPBOOL(UCPawn,bInstantDeRes);

		if( bNetOwner )
		{
			DOREP(UCPawn,MeleeWeapon,AUCWeap_Melee*);
			DOREP(UCPawn,MaxMultiJump,INT);
			DOREP(UCPawn,MultiJumpBoost,FLOAT);
			DOREP(UCPawn,MultiJumpRemaining,INT);
			DOREP(UCPawn,PickupNotifier,APickupNotifier*);
			DOREP(UCPawn,CurrentCombo,AUCCombo*);
			DOREP(UCPawn,ExtraMultiJumpAdrenCost,FLOAT);
		}
		DOREP(UCPawn,Shield,AUCShield*);
		DOREPBOOL(UCPawn,bIronSpirit);
		DOREPBOOL(UCPawn,bInvis);
		DOREPBOOL(UCPawn,bComboShield);
		DOREPBOOL(UCPawn,bUDamage);
		DOREPBOOL(UCPawn,bQuakeRun);
		DOREPBOOL(UCPawn,bWraith);
		DOREPBOOL(UCPawn,bSilent);
		DOREPBOOL(UCPawn,bDisplacement);
		DOREPBOOL(UCPawn,bNimble);
		DOREPBOOL(UCPawn,bMeleeSetPhysicsNone);
		DOREPBOOL(UCPawn,bSpawnProtection);
		DOREPBOOL(UCPawn,bAllowMeleeJumpAttack);
	}

	return Ptr;
}

INT* AUCProjectile::GetOptimizedRepList(BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel)
{
	guard(AUCProjectile::GetOptimizedRepList);
	
	Ptr = Super::GetOptimizedRepList(Defaults,Retire,Ptr,Map,Channel);
	
	if (bNetDirty)
	{
		DOREPBOOL(UCProjectile,bExploded);
		DOREPBOOL(UCProjectile,bHomeToTarget);
		DOREPBOOL(UCProjectile,bHomeToLocation);
		DOREPBOOL(UCProjectile,bCanHitInstigator);
		if (bExploded)
		{
			DOREPBOOL(UCProjectile,bEMPExplode);
		}
		if (bHomeToTarget)
		{
			DOREP(UCProjectile,HomeTarget,AActor*);
		}
		if (bHomeToLocation)
		{
			DOREP(UCProjectile,HomeLocation,FVector);
		}
		if (bHomeToTarget || bHomeToLocation)
		{
			DOREP(UCProjectile,HomeAccelScale,FLOAT);
		}
	}

	return Ptr;

	unguard;
}

void AUCCombo::CreateRecentMap(TMap<_WORD, TArray<BYTE>*>& RecentMap)
{
	guard(AUCCombo::CreateRecentMap);

	UObject* Default = StaticConstructObject(GetClass());

	// add Owner and Instigator, the only Actor properties that will be replicated
	static UProperty* ActorReplicatedProps[2] = {	FindField<UProperty>(GetClass(), TEXT("Owner")),
													FindField<UProperty>(GetClass(), TEXT("Instigator"))	};
	for (INT i = 0; i < ARRAY_COUNT(ActorReplicatedProps); i++)
	{
		checkSlow(ActorReplicatedProps[i]);
		AddToRecentMap(ActorReplicatedProps[i], (BYTE*)Default, RecentMap);
	}

	// iterate through all our properties, stopping when we get to Actor's because we already added the Actor properties we care about
	for (UProperty* It = GetClass()->PropertyLink; It && It->GetOuter() != AActor::StaticClass(); It = It->PropertyLinkNext)
	{
		AddToRecentMap(It, (BYTE*)Default, RecentMap);
	}

	delete Default;

	unguard;
}

INT* AUCCombo::GetOptimizedRepList(BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel)
{
	guard(AUCCombo::GetOptimizedRepList);

	if( bNetDirty && bNetOwner )
	{
		if (bNetInitial)
		{
			DOREP(Actor,Owner,AActor*);
			DOREP(Actor,Instigator,APawn*);
		}

		DOREP(UCCombo,NextCombo,AUCCombo*);
		DOREP(UCCombo,Duration,FLOAT);
		DOREP(UCCombo,TotalDuration,FLOAT);
		DOREPBOOL(UCCombo,bHasFlag);
	}

	return Ptr;

	unguard;
}

INT* AUCWeapon::GetOptimizedRepList(BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel)
{
	guard(AUCWeapon::GetOptimizedRepList);

	// note: intentionally skipping AWeapon::GetOptimizedRepList()
	Ptr = AInventory::GetOptimizedRepList(Defaults, Retire, Ptr, Map, Channel);
	if( bNetDirty && bNetOwner )
	{
		DOREPBOOL(UCWeapon,bDidComboFlag);
		DOREP(UCWeapon,KnockBackDirX,FLOAT);
		DOREP(UCWeapon,KnockBackDirY,FLOAT);
		DOREPBOOL(UCWeapon,bComboAvailable);
		DOREPBOOL(UCWeapon,bReadyToFire);
		DOREPBOOL(UCWeapon,bAllowAltFire);
	}

	return Ptr;

	unguard;
}

INT* AUCWeaponAttachment::GetOptimizedRepList(BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel)
{
	guard(AUCWeaponAttachment::GetOptimizedRepList);

	Ptr = Super::GetOptimizedRepList(Defaults, Retire, Ptr, Map, Channel);
	if (bReplicateFXVars && bNetDirty && (!bNetOwner || bReplicateToAll))
	{
		DOREP(UCWeaponAttachment,FX_Actor,AActor*);
		DOREP(UCWeaponAttachment,FX_Location,FVector);
		DOREP(UCWeaponAttachment,FX_Normal,FVector);
	}

	return Ptr;

	unguard;
}

/* scion ======================================================================
 * AUCAmbient_Creature::TickSpecial
 * Author: jg
 * 
 * Forces a GetFrame() to update the skeleton for this ambient creature. This
 * fixes instanced ambient creatures not being updated when the primary instance
 * is not in view. Update attached actor locations, if needed
 *
 * input:	DeltaSeconds - The time delta since the last tick
 *
 * ============================================================================
 */
void AUCAmbient_Creature::TickSpecial(FLOAT DeltaSeconds)
{
	guard(AUCAmbient_Creature::TickSpecial);
	// Only force an update if we are the main instance
	if (AnimationInstance == NULL)
	{
		USkeletalMesh* SkelMesh = (USkeletalMesh*)Mesh;
		// Don't bother if we don't have a skeletal mesh
		if (SkelMesh != NULL)
		{
			USkeletalMeshInstance* MInst = ((USkeletalMeshInstance*)MeshInstance);
			// Force an update of the skeleton
			MInst->ForceUpdate();
		}
	}
	// If we have any attached actors, update their positions
	Super::TickSpecial(DeltaSeconds);
	unguard;
}

// scion sz - returns true if the head should not be rendered
UBOOL AUCHead::IsNoRender( const UPlayer* Viewport ) const
{
	APawn* OwningPawn = Cast<APawn>( Owner );

	// pawn heads should only be allowed to not render in their own viewports
	// so, see if the current active viewport's controller matches the pawn's
	if( Viewport && 
		OwningPawn && 
		Viewport->Actor == OwningPawn->Controller )
	{        
		return bNoRender;
	}

	return 0;
}

static UBOOL bOldExploded;

void AUCProjectile::PreNetReceive()
{
	bOldExploded = bExploded;

	Super::PreNetReceive();
}

void AUCProjectile::PostNetReceive()
{
	Super::PostNetReceive();

	if (bExploded && !bOldExploded)
	{
		eventExplode(Location, Velocity.SafeNormal(), NULL);
	}
}

UBOOL AUCProjectile::Tick(FLOAT DeltaSeconds, enum ELevelTick TickType)
{
	guard(AUCProjectile::Tick);

	UBOOL bResult = Super::Tick(DeltaSeconds, TickType);

	if (bResult && !bDeleteMe && !bExploded) 
	{
		//debug
		//debugf(TEXT("%s TICK %s %s"), GetName(), bHomeToTarget ? TEXT("True") : TEXT("False"), HomeTarget ? HomeTarget->GetName() : TEXT("None"));

		if (bHomeToTarget && HomeTarget)
		{
			//debug
			//debugf(TEXT("%s Targ %s %f"), GetName(), HomeTarget->GetName(), HomeAccelScale);

			APawn* PTarget = HomeTarget->IsAPawn() ? (APawn*)HomeTarget : NULL;
			if (PTarget && PTarget->Health <= 0)
			{
				bHomeToTarget = false;
				bNetDirty = true;
			}
			else if (bPerfectHome)
			{
				Velocity = (HomeTarget->Location - Location).SafeNormal() * Speed;
				if (bUpdateSimulatedPosition)
				{
					bNetDirty = true;
				}
			}
			else if (!Velocity.IsZero())
			{
				// Accelerate toward target but ignore component along velocity direction
				FVector TargetLocation = HomeTarget->Location + (HomeTarget->Velocity * HomeLeadPct);
				Acceleration = (TargetLocation - Location).SafeNormal() * HomeAccelScale;
				Acceleration -= Acceleration.ProjectOnTo(Velocity);
			}
		}
		if (bHomeToLocation)
		{
			if (bPerfectHome)
			{
				Velocity = (HomeLocation - Location).SafeNormal() * Speed;
				if (bUpdateSimulatedPosition)
				{
					bNetDirty = true;
				}
			}
			else if (!Velocity.IsZero())
			{
				// accelerate to location
				Acceleration = (HomeLocation - Location).SafeNormal() * HomeAccelScale;
				Acceleration -= Acceleration.ProjectOnTo(Velocity);
			}
		}

		if (Physics != PHYS_None && bRotationByVelocity)
		{
			FCheckResult Hit(1.0f);
			GetLevel()->MoveActor(	this, 
									FVector(0,0,0), 
									bRotationInverted ? Velocity.Rotation() + FRotator(0,0,Rotation.Roll) : 
										(-Velocity).Rotation() + FRotator(0,0,Rotation.Roll), 
									Hit );
		}

		if (bTrailRotationByVelocity)
		{
			for (INT i = 0; i < TrailEmitter.Num(); i++)
			{
				if (TrailEmitter(i))
				{
					FCheckResult Hit(1.0f);
					GetLevel()->MoveActor(TrailEmitter(i), FVector(0,0,0), Velocity.Rotation(), Hit);
				}
			}
		}

		if (Level->NetMode != NM_DedicatedServer)
		{
			// If a reflection has occurred since the last tick
			if (ReflectCount != OldReflectCount && !bTrailsDelayed)
			{
				// Reset old reflect counter so this only happens once per reflect
				OldReflectCount = ReflectCount;

				AUCPawn* P = Cast<AUCPawn>(Instigator);
				if (P)
				{
					// If instigator has UDamage on, attach the UDam FX
					if (P->bUDamage)
					{
						eventSpawnExtraFX(UDamageTrailEmitterClass);
					}
					else if (P->bVampire)
					{
						eventSpawnExtraFX(VampireTrailEmitterClass);
					}
				}
			}
		}
	}

	return bResult;

	unguard;
}

/* scion ===============================================
* ::ApplyAlphaSkins
* Author: rayd
*
* Iterates through all alpha actors and builds the proper
* skin arrangement and places it in the Skins[] list.
*
* This function is a bit of a mess because of the wierd
* dependencies we have in our character skins.
*
* =====================================================
*/
void AUCPawn::ApplyAlphaSkins()
{
	guard(AUCPawn::ApplyAlphaSkins);

	if (!bDisableAlphaUpdates)
	{
		for (INT i = 0; i < AlphaActors.Num(); i++)
		{
			AActor* A = AlphaActors(i).AlphaActor;
			// scion sz - skip if bDeleteMe is true
			if (A && !A->bDeleteMe)
			{
				// iterate through each skin assignment
				for (INT j = 0; j < A->Skins.Num(); j++)
				{
					// invis doesn't need to be alpha'd, so set that as the only skin
					if (bInvis)
					{
						A->Skins(j) = InvisMaterial;
					}
					else if (bWraith)
					{
						A->Skins(j) = WraithMaterial;
					}
					else if (bSilent || bDisplacement)
					{
						A->Skins(j) = SilentMaterial;
					}
					// if we have team colors, or we want to force the alpha, we always have to apply the colormodifier
					else if( (AlphaActors(i).bUseTeamColor || AlphaActors(i).bForceAlpha) &&
							 (AlphaActors(i).Modifiers(j) || AlphaActors(i).FinalBlends(j)) )
					{
						// base skin, which should be the team shader
						AlphaActors(i).Modifiers(j)->Material = AlphaActors(i).BaseSkins(j);

						// setup the modifier color,
						if (AlphaActors(i).bUseTeamColor && PlayerReplicationInfo && PlayerReplicationInfo->Team)
						{
							// apply the team color
							AlphaActors(i).Modifiers(j)->Color = PlayerReplicationInfo->Team->TeamColors(PlayerReplicationInfo->Team->TeamIndex).TeamColor;
						}
						else
						{
							// default color
							AlphaActors(i).Modifiers(j)->Color.R = 255;
							AlphaActors(i).Modifiers(j)->Color.G = 255;
							AlphaActors(i).Modifiers(j)->Color.B = 255;
						}

						// setup alpha for the modifier,
						if( AlphaActors(i).bForceAlpha )
						{
							// force given alpha
							AlphaActors(i).Modifiers(j)->Color.A = AlphaActors(i).ForceAlpha;
							AlphaActors(i).Modifiers(j)->AlphaBlend = true;
							
							// setup the final blend, using modifier as source
							AlphaActors(i).FinalBlends(j)->Material = AlphaActors(i).Modifiers(j);
							// and set the final blend as the skin
							A->Skins(j) = AlphaActors(i).FinalBlends(j);
						}
						else
						{
							// disable alpha on the modifier
							AlphaActors(i).Modifiers(j)->Color.A = 255;
							AlphaActors(i).Modifiers(j)->AlphaBlend = false;
							
							// and set the final blend as the skin
							A->Skins(j) = AlphaActors(i).Modifiers(j);
						}						
					}
					// otherwise stick with the base skin assignment
					else
					{
						A->Skins(j) = AlphaActors(i).BaseSkins(j);
					}
				}
                // scion sz - force sorting update
				FDynamicActor* DynA = A->GetActorRenderData(); check(DynA);				
				DynA->NeedsSortUpdate = 1;                
			}
			else
			{
				// otherwise remove this empty entry
				eventRemoveAlphaActor(i--, 0);
			}
		}
	}

	unguard;
}

void AUCPawn::execApplyAlphaSkins(FFrame& Stack, RESULT_DECL)
{
	P_FINISH;

	ApplyAlphaSkins();
}

void AUCPawn::ScalePawn()
{
	guard(AUCPawn::ScalePawn);

	// can only scale if turned on and there's only one viewport (not splitscreen)
	if (Level->bUseDynamicDrawScale && UEngine::GEngine->Client && UEngine::GEngine->Client->Viewports.Num() == 1)
	{
		FLOAT Scale = GetClass()->GetDefaultActor()->DrawScale;
		// get local player
		APlayerController* PC = UEngine::GEngine->Client->Viewports(0)->Actor;
		if (PC && PC->ViewTarget && PC->ViewTarget != this)
		{
			// scale ourselves by the distance between the player and us
			FLOAT Distance = (Location - PC->ViewTarget->Location).Size();
			// Only scale if it is beyond the start threshhold
			if (Distance >= StartScaleDistance)
			{
				// Figure out the Alpha of the distance lerping
				FLOAT Alpha = (Distance - StartScaleDistance) /	(EndScaleDistance - StartScaleDistance);
				Alpha *= PC->FovAngle / PC->DefaultFOV;	// simple linear scale for FOV changes

				if (Alpha > 1.0)
				{
					Alpha = 1.0;
				}
				// Lerp between the min and max scaling values
				FLOAT MaxScale = Scale * MaxDistanceScaling;
				Scale = Lerp<FLOAT>(Scale, MaxScale, Alpha);
			}
		}
		// if we have different scales, apply the new one
		if (DrawScale != Scale)
		{
			SetDrawScale(Scale);
			if (Head)
			{
				Head->SetDrawScale(Scale);
			}
		}
	}

	unguard;
}

void AUCPawn::TickSpecial( FLOAT DeltaSeconds )
{
	if (bUseDesiredPrePivot && DesiredPrePivot != PrePivot)
	{
		PrePivot = Lerp(PrePivot, DesiredPrePivot, 0.5f);
		for (INT i = 0; i < 3; i++)
		{
			if (Abs(PrePivot[i] - DesiredPrePivot[i]) < 0.5f)
			{
				PrePivot[i] = DesiredPrePivot[i];
			}
		}
	}

	// If this pawn is being simulated and 
	// it is no locally controlled
	if( IsSpectatorTarget() )
	{
		// Try lerping to the desired rotation
		// Default the new rotation to the desired
		FRotator NewRotation = DesiredRotation;

		// Normalize the desired and old
		INT Desired = DesiredRotation.Yaw;
		INT Old		= Rotation.Yaw & 0xFFFF;
		if( Old > 32767 )
		{
			Old -= 0x10000;
		}

		// If not already there
		if( Desired != Old )
		{
			// Get the delta yaw
			INT Dir = Desired - Old;

			// If not changing too little or too much we should interp
			if( Abs(Dir) > 5 && Abs(Dir) < MaxRotationInterp )
			{
				// Interp from old to desired (interps faster the larger the delta is)
				NewRotation.Yaw = Old + Dir * DeltaSeconds * 10.f;
				
				// Check to see if we have exceeded our target
				// If so just set the value
				INT OldToDesired = Desired - Old;
				INT OldToNew	 = NewRotation.Yaw - Old;
				if( Square(OldToDesired) <= Square(OldToNew) )
				{
					NewRotation.Yaw = Desired;
				}
			}

			// Update the pawn's rotation
			FCheckResult Hit(1.f);
			GetLevel()->MoveActor( this, FVector(0,0,0), NewRotation, Hit );
		}
	}


	Super::TickSpecial(DeltaSeconds);
}

UBOOL AUCPawn::Tick(FLOAT DeltaSeconds, enum ELevelTick TickType)
{
	guard(AUCPawn::Tick);

	UBOOL bResult = Super::Tick(DeltaSeconds, TickType);
	if (bResult && !bDeleteMe && !GIsEditor)
	{
#ifndef FINAL_RELEASE
		if (bDebugHeadShots && Mesh && Mesh->IsA(USkeletalMesh::StaticClass()))
		{
			// draw head sphere
			Mesh->MeshGetInstance(this);
			INT BoneIdx = ((USkeletalMeshInstance*)MeshInstance)->MatchRefBone(HeadBone);
			const FCoords& C = ((USkeletalMeshInstance*)MeshInstance)->GetBoneCoords(BoneIdx);
			FVector HeadLoc = C.Origin + (HeadOffset.X * C.XAxis + HeadOffset.Y * C.YAxis + HeadOffset.Z * C.ZAxis);
			GTempLineBatcher->AddSphere(HeadLoc, HeadRadius, FColor(255, 0, 0), 32);
		}
#endif
		bStepped = 0;

		if (Controller)
		{
			OldController = Controller;
		}
		// Reset damage controller if walking
		if (Physics == PHYS_Walking)
		{
			DamageByController = NULL;
		}
		// handle replicated adrenaline
		if (Level->NetMode == NM_Client)
		{
			// round value up to handle precision bug where on the server player has 50 adrenaline but it converts to 49.something
			Adrenaline = ceil(FLOAT(RepAdrenaline) / 255.f * AdrenalineMax);
		}
		else
		{
			FLOAT AdrenalinePercent = Adrenaline / AdrenalineMax;
			BYTE newRepAdrenaline;
			if (AdrenalinePercent > 0.98f)
			{
				newRepAdrenaline = 255;
			}
			else if (AdrenalinePercent < 0.02f)
			{
				newRepAdrenaline = 0;
			}
			else
			{
				newRepAdrenaline = BYTE(AdrenalinePercent * 255.f);
			}
			// only set if different, to avoid an unnecessary bNetDirty
			if (newRepAdrenaline != RepAdrenaline)
			{
				RepAdrenaline = newRepAdrenaline;
				bNetDirty = 1;
			}
		}
		// if this isn't a locally controlled pawn,
		// note: IsLocallyControlled() in C++ corresponds to script (IsLocallyControlled() && IsHumanControlled())
		if (!IsLocallyControlled())
		{
			// scale it based on the distance to the camera
			ScalePawn();
		}
		// Tick Adrenaline Info (slow increase over time when no voluntary combos on)
		if (bAdrenalineEnabled && Adrenaline < AdrenalineMax && Level->Game && !Level->Game->bGameEnded)
		{
			UBOOL bHasVoluntaryCombo = 0;
			for (AUCCombo* Combo = CurrentCombo; Combo && !bHasVoluntaryCombo; Combo = Combo->NextCombo)
			{
				bHasVoluntaryCombo = !(Combo->bNegativeCombo || Combo->bForced);
			}
			if (!bHasVoluntaryCombo)
			{
				Adrenaline += DeltaSeconds * fAdrenalineReturnRate;
				if (Adrenaline >= AdrenalineMax)
				{
					// adrenaline is full
					Adrenaline = AdrenalineMax;
					// if controlled by a human
					if (Role == ROLE_Authority && IsHumanControlled())
					{
						// play adrenaline full announcement
						Controller->eventClientPlaySoundCue(Controller->AdrenalineFullCue, 0);
						// set timer to play again if adrenaline remains full
						static FName NAME_PlayAdrenalineFullCue = FName(TEXT("PlayAdrenalineFullCue"));
						Controller->SetFuncTimer(NAME_PlayAdrenalineFullCue, 30.f, false);
					}
				}
			}
		}
		// we need the GRI to properly handle skins, otherwise we don't know if we need team skins or not
		if (!Level->GRI)
		{
			return bResult;
		}
		if (!bSetupMeshesAndSkins)
		{
#if LIN_LOAD
			//jg -- Super evil hack for verifying that a set of XPRs are loaded
			if (Level != NULL)
			{
#ifndef FINAL_RELEASE
				FLOAT Start = appSeconds();
#endif
				Level->eventDynamicLoadAssetsFor(GetClass());
				Level->WaitForDynamicLoadingComplete();
#ifndef FINAL_RELEASE
				debugf(TEXT("Evil XPR hack took %f seconds"),(FLOAT)appSeconds() - Start);
#endif
			}
#endif
			eventSetupMeshesAndSkins();
			bSetupMeshesAndSkins = 1;
		}
		// check to see if we need to update our alpha actors
		UBOOL bForceAlphaSkinUpdate = 0;
		if (bUpdateAlphaActors && Level->NetMode != NM_DedicatedServer)
		{
			eventClientUpdateAlphaActors();
			bUpdateAlphaActors = 0;
			bForceAlphaSkinUpdate = 1;
		}
		// == ALPHAACTORS UPDATE == //
		// check to see if we need to update our skins
		if ( bForceAlphaSkinUpdate || bInvis != bOldInvis || bUDamage != bOldUDamage || bComboShield != bOldComboShield ||
			bWraith != bOldWraith || bSilent != bOldSilent || bDisplacement != bOldDisplacement || 
			bSpawnProtection != bOldSpawnProtection || DamageFlashCount != OldDamageFlashCount )
		{
			UBOOL bRefreshSkin = 0;
			// check for an update of the damage skin rimlight
			if( DamageFlashCount != OldDamageFlashCount || 
				bSpawnProtection != bOldSpawnProtection )
			{
				if( (!bOldSpawnProtection && OldDamageFlashCount == 0) &&	
					AlphaActors.Num() > 0 && 
					Health > 0 )
				{
					// apply the damage skin
					if (UniqueSkinShader)
					{
						// save the previous rimlight material
						if (Health > MaxHealthAmount)
						{
							UniqueSkinShader->RimLight = DamageFlashSuperSkin;
						}
						else
						{
							UniqueSkinShader->RimLight = DamageFlashSkin;
						}

						UniqueSkinShader->bUseLightingForSpecular = 0;
						if (UniqueHeadSkinShader)
						{
							if (Health > MaxHealthAmount)
							{
								UniqueHeadSkinShader->RimLight = DamageFlashSuperSkin;
							}
							else
							{
								UniqueHeadSkinShader->RimLight = DamageFlashSkin;
							}
							UniqueHeadSkinShader->bUseLightingForSpecular = 0;
						}
					}
				}
				else if( Health <= 0 || (DamageFlashCount == 0 && !bSpawnProtection) )
				{
					bRefreshSkin = 1;
					if (UniqueSkinShader)
					{
						// reapply saved rimlight material
						UniqueSkinShader->RimLight = SavedRimLight;
						UniqueSkinShader->bUseLightingForSpecular = bBodyUseLightingForSpecular;

						if (UniqueHeadSkinShader)
						{
							UniqueHeadSkinShader->RimLight = SavedHeadRimLight;
							UniqueHeadSkinShader->bUseLightingForSpecular = bHeadUseLightingForSpecular;
						}
					}
				}
			}

			if (bRefreshSkin || bUDamage != bOldUDamage)
			{
				if (bUDamage)
				{
					// apply the UDamage skin
					if (UniqueSkinShader)
					{
						UniqueSkinShader->RimLight = UDamageMaterial;

						UniqueSkinShader->bUseLightingForSpecular = 0;
						if (UniqueHeadSkinShader)
						{
							UniqueHeadSkinShader->RimLight = UDamageMaterial;
							UniqueHeadSkinShader->bUseLightingForSpecular = 0;
						}
					}
				}
				else
				{
					if (UniqueSkinShader)
					{
						// reapply saved rimlight material
						UniqueSkinShader->RimLight = SavedRimLight;
						UniqueSkinShader->bUseLightingForSpecular = bBodyUseLightingForSpecular;
						if (UniqueHeadSkinShader)
						{
							UniqueHeadSkinShader->RimLight = SavedHeadRimLight;
							UniqueHeadSkinShader->bUseLightingForSpecular = bHeadUseLightingForSpecular;
						}
					}
				}
			}

			// update the skins
			ApplyAlphaSkins();
			// if we're now invis,
			if( bInvis || bDisplacement )
			{
				// disable our player shadow
				if (PlayerShadow)
				{
					PlayerShadow->bShadowActive = 0;
				}
				// and turn off projectors
				Projectors.Empty();
				bAcceptsProjectors = 0;
			}
			// otherwise,
			else
			{
				// enable our player shadow
				if (PlayerShadow)
				{
					PlayerShadow->bShadowActive = 1;
				}
				// and turn on projectors
				bAcceptsProjectors = 1;
			}

			// save our current state
			bOldInvis = bInvis;
			bOldUDamage = bUDamage;
			bOldComboShield = bComboShield;
			bOldWraith = bWraith;
			bOldSilent = bSilent;
			bOldDisplacement = bDisplacement;
			bOldSpawnProtection = bSpawnProtection;
			OldDamageFlashCount = DamageFlashCount;
		}
		
		// if our shield status has changed,
		if (bOldIsShielded != bIsShielded)
		{
			if (Level->NetMode == NM_Client)
			{
				// check to see if we should activate/deactivate
				if (bIsShielded)
				{
					eventShieldActive();
				}
				else
				{
					eventShieldInactive();
				}
			}
			// save our new shield status
			bOldIsShielded = bIsShielded;
		}

		//debug
		//debugf(TEXT("%s FBID %d %s %d"), GetName(), FullBodyID, (OldFullBodyCnt != FullBodyCnt) ? TEXT("True") : TEXT("False"), FullBodyCnt);

		if (Level->NetMode != NM_DedicatedServer && Level->NetMode != NM_Standalone)
		{
			if (OldFullBodyCnt != FullBodyCnt)
			{
				//debugf(TEXT("%s TICK FULLBODYANIM CHANGED %d TO %d ID %d"), GetName(), OldFullBodyCnt, FullBodyCnt, FullBodyID);
				UBOOL bUpdateAnims = 0;
				if (!IsLocallyControlled() && (!Controller || !Controller->IsA(AAIController::StaticClass())))
				{
					switch (FullBodyID)
					{
						case UCONST_FBANIM_DODGE_Fwd:
						case UCONST_FBANIM_DODGE_Bwd:
						case UCONST_FBANIM_DODGE_Lft:
						case UCONST_FBANIM_DODGE_Rt:
							eventPlayDodge();
							bUpdateAnims = 1;
							break;
						case UCONST_FBANIM_FLIP_Fwd:
						case UCONST_FBANIM_FLIP_Bwd:
						case UCONST_FBANIM_FLIP_Lft:
						case UCONST_FBANIM_FLIP_Rt:
							eventPlayDoubleDodge( 1 );
							bUpdateAnims = 1;
							break;
						case UCONST_FBANIM_WALLJUMP_Fwd:
						case UCONST_FBANIM_WALLJUMP_Bwd:
						case UCONST_FBANIM_WALLJUMP_Lft:
						case UCONST_FBANIM_WALLJUMP_Rt:
							eventPlayDoubleDodge( 0 );
							bUpdateAnims = 1;
							break;
						case UCONST_FBANIM_DBLJUMP_Fwd:
						case UCONST_FBANIM_DBLJUMP_Bwd:
						case UCONST_FBANIM_DBLJUMP_Lft:
						case UCONST_FBANIM_DBLJUMP_Rt:
						case UCONST_FBANIM_DBLJUMP_Up:
							eventPlayDoubleJump();
							bUpdateAnims = 1;
							break;
						case UCONST_FBANIM_JUMP_Fwd:
						case UCONST_FBANIM_JUMP_Bwd:
						case UCONST_FBANIM_JUMP_Lft:
						case UCONST_FBANIM_JUMP_Rt:
						case UCONST_FBANIM_JUMP_Up:
							if ( !bDisableActorSounds && (!Owner || !Owner->bDisableActorSounds) &&
								(!Instigator || !Instigator->bDisableActorSounds) && GetLevel()->Engine->Audio )
							{
								GetLevel()->Engine->Audio->PlaySoundCue(this, JumpSound);
							}
							break;
						case UCONST_FBANIM_Death_HeadShot:
							// handled in playdying event!
							break;
						case UCONST_FBANIM_None:
						default:
							bUpdateAnims = 1;
							break;
					}
				}
				else
				{
					// Owning client - if Attack is higher than simulated actions
					// play the animation
					if (FullBodyID == UCONST_FBANIM_None || FullBodyID > UCONST_FBANIM_MELEE_ATTACK)
					{
						bUpdateAnims = 1;
					}
				}

				// only update if this is a remote player
				// since all our animations will be simulated already
				if (bUpdateAnims)
				{
					//debug
					//debugf(TEXT("%s UPDATEANIMS %d"), GetName(), FullBodyID);
					eventSetFullBodyAnim(FullBodyID);
				}
				OldFullBodyCnt = FullBodyCnt;
			}


			if (OldUpperBodyCnt != UpperBodyCnt)
			{
				//debug
#ifdef _DEBUG
				if (bDebugAnim)
				{
					debugf(TEXT("%s TICK UPPERBODYANIM CHANGED %d TO %d ID %d"), GetName(), OldUpperBodyCnt, UpperBodyCnt, UpperBodyID);
				}
#endif
				UBOOL bUpdateAnims = 1;

				// If on a local client (where there is a controller)
				if( Controller )
				{
					// Don't replay the reflect animation (it is handled by the melee weapon)
					switch (UpperBodyID)
					{
						case UCONST_UBANIM_REFLECT_Active:
							bUpdateAnims = 0;
							break;
					}
				}

				if (bUpdateAnims)
				{
					eventSetUpperBodyAnim(UpperBodyID);
				}
			}

			if (OldWeaponAnimType != WeaponAnimType)
			{
				//debug
#ifdef _DEBUG
				if (bDebugAnim)
				{
					debugf(TEXT("%s TICK WEAPONANIMTYPE CHANGED %u TO %u"), GetName(), OldWeaponAnimType, WeaponAnimType);
				}
#endif
				eventSetWeaponAnimType(WeaponAnimType);
			}
		}		
	}

	return bResult;

	unguard;
}

UBOOL AUCWeaponAttachment::Tick(FLOAT DeltaSeconds, enum ELevelTick TickType)
{
	guard(AUCWeaponAttachment::Tick);

	UBOOL bResult = Super::Tick(DeltaSeconds, TickType);
	if (bResult && (AlphaTimer > 0.f || !Instigator || !Instigator->IsLocallyControlled()))
	{
		AUCPawn* P = Cast<AUCPawn>(Instigator);
		if (P)
		{
			AUCWeaponAttachment* Default = GetClass()->GetDefaultObject<AUCWeaponAttachment>();
			// if dynamic drawscale is enabled and it's not a splitscreen game
			if (Level->bUseDynamicDrawScale && UEngine::GEngine->Client && UEngine::GEngine->Client->Viewports.Num() == 1)
			{
				// Base scale is default modified by character
				FLOAT Scale = Default->DrawScale * P->WeaponAttachmentScale;

				// Get the local player and verify that it's not the same as this weapon's
				// instigator, and check that local player isn't zoomed
				// Don't scale when interpolating
				APlayerController* PC = UEngine::GEngine->Client->Viewports(0)->Actor;

				if (PC && !PC->bInterpolating && PC->ViewTarget != Instigator && PC->ViewTarget)
				{
					// They are not the same so scale the third person actor by the
					// distance between them
					FLOAT Distance = (Instigator->Location - PC->ViewTarget->Location).Size();

					// Only scale if it is beyond the start threshhold
					if (Distance >= StartScaleDistance)
					{
						// Figure out the Alpha of the distance lerping
						FLOAT Alpha = (Distance - StartScaleDistance) / (EndScaleDistance - StartScaleDistance);

						Alpha *= PC->FovAngle / PC->DefaultFOV;	// simple linear scale for FOV changes

						if (Alpha > 1.0f)
						{
							Alpha = 1.0f;
						}

						// Lerp between the min and max scaling values
						FLOAT MaxScale = Default->DrawScale * MaxDistanceScaling;
						Scale = Lerp<FLOAT>(Default->DrawScale, MaxScale, Alpha);
						// Adjust to be character specific
						Scale *= P->WeaponAttachmentScale;
					}
				}

				if (Scale != DrawScale)
				{
					// Now set the third person weapon scales
					SetDrawScale(Scale);

					// Scale up effects if there are any
					if (FX_Emitter)
					{
						FX_Emitter->SetDrawScale(Scale);

						// Update relative offset when scaling as well
						if (FX_Emitter->Base)
						{
							FVector NewLocation = Default->FX_RelativeLoc * (Scale / Instigator->DrawScale);
							FX_Emitter->RelativeLocation = NewLocation;
							FCoords Coords = GMath.UnitCoords / FX_Emitter->Base->Rotation;
							NewLocation = FX_Emitter->Base->Location + NewLocation.X * Coords.XAxis + NewLocation.Y * Coords.YAxis + NewLocation.Z * Coords.ZAxis;
							GetLevel()->FarMoveActor(FX_Emitter, NewLocation, false, false, true);
						}
					}
					if (ExtraEffect)
					{
						ExtraEffect->SetDrawScale(Scale);
					}
				}
			}

			// If alpha timer is running
			if (AlphaTimer > 0.0)
			{
				FLOAT MaxAlpha = 255.f;

				// Decrement timer and compute new alpha value
				FLOAT Alpha;
				AlphaTimer -= DeltaSeconds;
				if (AlphaTimer >= 0.f)
				{
					Alpha = AlphaTimer / Default->AlphaTimer;
				}
				else
				{
					Alpha = 1.0f;
				}

				// Alpha in (flip alpha) if not being destroyed
				if (!bPendingDestroy)
				{
					Alpha = 1.0f - Alpha;
				}

				// If we don't have an info pointer yet, find it
				if ( AlphaActorIndex < 0 || AlphaActorIndex >= P->AlphaActors.Num() ||
					P->AlphaActors(AlphaActorIndex).AlphaActor != this )
				{
					AlphaActorIndex = P->GetAlphaActorIdx(this);
				}

				if (AlphaActorIndex >= 0)
				{
					// If alpha timer still running after this tick
					if (AlphaTimer > 0.0)
					{
						// Force the alpha value on the attached actor
						P->AlphaActors(AlphaActorIndex).bForceAlpha = true;
						P->AlphaActors(AlphaActorIndex).ForceAlpha = Clamp<INT>(Alpha * MaxAlpha, 1, MaxAlpha);
						P->ApplyAlphaSkins();
					}
					else
					{
						// Otherwise, timer just ended, turn off force flag
						P->AlphaActors(AlphaActorIndex).bForceAlpha = false;
						P->AlphaActors(AlphaActorIndex).ForceAlpha = MaxAlpha;
						P->ApplyAlphaSkins();
					}
				}
			}
		}
	}

	return bResult;

	unguard;
}

void AUCManualJumpPad::RenderEditorSelected(FLevelSceneNode* SceneNode, FRenderInterface* RI, FDynamicActor* FDA)
{
#ifndef _XBOX
	guard(AUCManualJumpPad::RenderEditorSelected);

	if (myMarker)
	{
		RI->SetTransform(TT_LocalToWorld, FMatrix::Identity);
		FLineBatcher LineBatcher(RI, 0);

		for (INT i = 0; i < myMarker->PathList.Num(); i++)
		{
			// draw lines to all forced paths
			if (myMarker->PathList(i) && myMarker->PathList(i)->End && (myMarker->PathList(i)->reachFlags & R_FORCED))
			{
				FVector Vel = JumpVelocity + FVector(0, 0, TESTJUMPZ);
				FVector Loc = Location;
				FVector Dest = myMarker->PathList(i)->End->Location;
				FVector AirControlDir = (myMarker->PathList(i)->End->Location - Location).SafeNormal();
				AirControlDir.Z = 0.f;
				while (Vel.Z > 0.f || Loc.Z > Dest.Z)
				{
					FVector OldLoc = Loc;
					// apply jump velocity
					Loc += 0.03f * Vel;
					// apply predicted air control
					FVector AirControl = 0.03f * AirControlDir * APawn::StaticClass()->GetDefaultObject<APawn>()->AirSpeed;
					if (Loc.X != Dest.X)
					{
						Loc.X += AirControl.X;
					}
					if (Loc.Y != Dest.Y)
					{
						Loc.Y += AirControl.Y;
					}
					// if we went past the destination, set to it
					if ((OldLoc.X < Dest.X) == (Loc.X >= Dest.X))
					{
						Loc.X = Dest.X;
					}
					if ((OldLoc.Y < Dest.Y) == (Loc.Y >= Dest.Y))
					{
						Loc.Y = Dest.Y;
					}
					// apply gravity to velocity
					Vel += 0.03f * GetGravity();
					// draw the line
					LineBatcher.DrawLine(Loc, OldLoc, FColor(255, 64, 255));
				}
			}
		}
	}

	unguard;
#endif
}

INT AUCManualJumpPad::AddMyMarker(AActor *S)
{
#ifndef _XBOX
	guard(AUCManualJumpPad::AddMyMarker);

	AScout* Scout = Cast<AScout>(S);
	UPrimitive* Primitive = GetPrimitive();
	INT Result = 0;
	if (Scout && Primitive)
	{
		FCheckResult Hit(1.0f);
		UBOOL bNoHit = Primitive->LineCheck(Hit, this, this, Location - FVector(0, 0, 1000), Location + FVector(0, 0, 1000), FVector(0, 0, 0), 0, 0);
		if (bNoHit)
		{
			GWarn->MapCheck_Add(MCTYPE_ERROR, this, TEXT("Failed to find spot for NavigationPoint marker - was collision turned off?"));
		}
		else
		{
			if (!Scout->findStart(Hit.Location) || (Abs(Scout->Location.Z - Hit.Location.Z) > Scout->CollisionHeight))
			{
				GetLevel()->FarMoveActor(Scout, Hit.Location + FVector(0,0,Scout->CollisionHeight), 1, 1);
			}
			if (myMarker && !myMarker->bDeleteMe)
			{
				GetLevel()->FarMoveActor(myMarker, Scout->Location, 1, 1);
			}
			else
			{
				myMarker = (ANavigationPoint*)GetLevel()->SpawnActor(ANavigationPoint::StaticClass(), NAME_None, Scout->Location);
				if (myMarker)
				{
					myMarker->bHiddenEd = 1;
				}
				else
				{
					GWarn->MapCheck_Add(MCTYPE_ERROR, this, TEXT("Failed to place NavigationPoint marker - is something in the way?"));
				}
			}

			if (myMarker)
			{
				// add forced paths from MyMarker to all our JumpTargets
				for (INT i = 0; i < JumpTargets.Num(); i++)
				{
					if (JumpTargets(i) == NAME_None)
					{
						JumpTargets.Remove(i, 1);
						i--;
					}
					else
					{
						ANavigationPoint* Nav = (ANavigationPoint*)StaticFindObjectFast(ANavigationPoint::StaticClass(), GetLevel()->GetOuter(), JumpTargets(i));
						if (Nav)
						{
							UReachSpec *newSpec = ConstructObject<UReachSpec>(UReachSpec::StaticClass(), GetLevel()->GetOuter(), NAME_None, RF_Public);
							newSpec->defineFor(myMarker, Nav, Scout);
							newSpec->CollisionRadius = Max(newSpec->CollisionRadius, COMMONRADIUS);
							newSpec->CollisionHeight = Max(newSpec->CollisionHeight, COMMONHEIGHT);
							newSpec->reachFlags = R_FORCED | R_JUMP;
							newSpec->Distance = (INT)((myMarker->Location - Nav->Location).Size());
							myMarker->PathList.AddItem(newSpec);
							Nav->SetupForcedPath(Scout, newSpec);
						}
						else
						{
							GWarn->MapCheck_Add(MCTYPE_ERROR, this, *FString::Printf(TEXT("Failed to find JumpTarget %s!"), *JumpTargets(i)));
						}
					}
				}

				// automatically calculate appropriate JumpVelocity to reach the first JumpTarget, if requested
				if (bAutomaticJumpVelocity)
				{
					Scout->JumpZ = 10000.f;
					Scout->SetCollisionSize(HUMANRADIUS,HUMANHEIGHT);
					if (myMarker->PathList.Num() > 0 && GetLevel()->FarMoveActor(Scout, myMarker->Location))
					{
						FVector XYDir = myMarker->PathList(0)->End->Location - myMarker->Location;
						FLOAT ZDiff = XYDir.Z;
						FLOAT Time = 2.5f * appSqrt(Abs(ZDiff / GetGravity().Z));
						JumpVelocity = XYDir / Time; 
						JumpVelocity.Z = ZDiff / Time - 0.5f * GetGravity().Z * Time;
					}
					else 
					{
						JumpVelocity = FVector(0.f, 0.f, 3.f*TESTJUMPZ);
					}
					Scout->JumpZ = TESTJUMPZ;
				}
			
				Result = 1;
			}
		}
	}
	
	return Result;

	unguard;
#else
	return 0;
#endif
}

void UCharacterMemoryUsageCommandlet::execGetXPRSize(FFrame& Stack, RESULT_DECL)
{
#ifndef _XBOX
	guard(UCharacterMemoryUsageCommandlet::execGetXPRSize);

	P_GET_TARRAY_REF(XPRGroupNames, FString);
	P_FINISH;

	INT TotalSize = 0;

	for (INT i = 0; i < XPRGroupNames->Num(); i++)
	{
		TotalSize += GFileManager->FileSize(*FString::Printf(TEXT("..\\XboxTextures\\%s.xpr"), *(*XPRGroupNames)(i)));
		TotalSize += GFileManager->FileSize(*FString::Printf(TEXT("..\\XboxTextures\\%s_uma.xpr"), *(*XPRGroupNames)(i)));
		TotalSize += GFileManager->FileSize(*FString::Printf(TEXT("..\\XboxTextures\\%s_pb.xpr"), *(*XPRGroupNames)(i)));
	}

	*(INT*)Result = TotalSize;

	unguardexec;
#endif
}
