/*=============================================================================
	UnNavigationPoint.cpp: 

  NavigationPoint and subclass functions

	Copyright 2001-2002 Epic Games, Inc. All Rights Reserved.

  	Revision history:
		* Created by Steven Polge 
=============================================================================*/

#include "EnginePrivate.h"
#include "UnPath.h"

/* epic ===============================================
* ::execSetObjectiveDistance
* Author: moelfke
*
* Cache the path distance from each NavigationPoint to this one
* and store it in their ObjectiveDist array at the specified
* index. Useful for use on game objectives so you always
* know how far a bot has to go or can find destinations within
* a certain path distance without having to do expensive pathfinding.
*
* Use of this function is necessary for use of the FindHidingSpot()
* function. The results of this function can also be used to
* optimize FindPathToward() when pathfinding towards frequently
* requested destinations. See those functions in Controller.uc
* for more information.
*
* =====================================================
*/
void ANavigationPoint::execSetObjectiveDistance(FFrame& Stack, RESULT_DECL)
{
	guard(ANavigationPoint::execSetObjectiveDistance);

	P_GET_INT(Index);
	P_FINISH;

	SetObjectiveDistance(Index, 0.f);

	unguardexec;
}

/* epic ===============================================
* ::SetObjectiveDistance
* Author: moelfke
*
* See execSetObjectiveDistance()
*
* =====================================================
*/
void ANavigationPoint::SetObjectiveDistance(INT Index, FLOAT CurrentDist)
{
	guard(ANavigationPoint::SetObjectiveDistance);

	//increase the size of the array if necessary
	while (ObjectiveDist.Num() <= Index)
	{
		// initialize elements to really high number so they always pass the check below
		ObjectiveDist.AddItem(1000000.f);
	}

	// if the currently stored dist is larger than the new one
	if (ObjectiveDist(Index) > CurrentDist)
	{
		// set the stored dist to the current dist
		ObjectiveDist(Index) = CurrentDist;
		// then continue along to the nodes connected to this one
		for (INT i = 0; i < PathList.Num(); i++)
			if ((PathList(i)->reachFlags & R_PROSCRIBED) == 0 && PathList(i)->Distance >= 0.f)
				PathList(i)->End->SetObjectiveDistance(Index, CurrentDist + PathList(i)->Distance);
	}

	unguard;
}

UBOOL ANavigationPoint::CanReach(ANavigationPoint *Dest, FLOAT Dist)
{
	guard(ANavigationPoint::CanReach)

	if ( this == Dest )
		return true;
	if ( visitedWeight >= Dist )
		return false;

	visitedWeight = Dist;
	if ( Dist <= 0.f )
		return false;
	
	for ( INT i=0; i<PathList.Num(); i++ )
		if ( (PathList(i)->reachFlags & R_PROSCRIBED) == 0 )
		{
			if( PathList(i)->Distance <= 1.f )
			{
				debugf(TEXT("%s negative or zero distance to %s!"), GetName(),PathList(i)->End->GetName());
				GWarn->MapCheck_Add( MCTYPE_ERROR, this, *FString::Printf(TEXT("negative or zero distance to %s!"), PathList(i)->End->GetName()));
			}
			else if ( PathList(i)->End->CanReach(Dest, Dist - PathList(i)->Distance) )
			return true;
		}

	return false;
	unguard;
}

UBOOL ANavigationPoint::ReviewPath(APawn* Scout)
{
	guard(ANavigationPoint::ReviewPath)

	if ( bMustBeReachable )
	{
		// check that all other paths can reach me
		for ( ANavigationPoint* N=Level->NavigationPointList; N!=NULL; N=N->nextNavigationPoint )
		{
			for ( ANavigationPoint* M=Level->NavigationPointList; M!=NULL; M=M->nextNavigationPoint )
				M->visitedWeight = 0.f;
			if ( !N->CanReach(this, 10000000.f) )
				GWarn->MapCheck_Add( MCTYPE_ERROR, N, *FString::Printf(TEXT("Cannot reach %s from this node!"), GetName()));
		}
	}

	// check that there aren't any really long paths from here
	for ( INT i=0; i<PathList.Num(); i++ )
		if ( ((PathList(i)->reachFlags & (R_PROSCRIBED | R_FORCED | R_JUMP)) == 0) && (PathList(i)->Distance > 1000.f) 
			&& (Abs(PathList(i)->Start->Location.Z - PathList(i)->End->Location.Z) < 500.f) )
		{
			// check if alternate route
			UBOOL bFoundRoute = false;

			for ( INT j=0; j<PathList.Num(); j++ )
				if ( (PathList(j)->Distance < 700.f)
					&& PathList(j)->End->CanReach(PathList(i)->End,::Max((PathList(i)->Distance - PathList(j)->Distance) * 1.25f,700.f)) )
				{
					bFoundRoute = true;
					break;
				}
			if ( !bFoundRoute )
				GWarn->MapCheck_Add( MCTYPE_WARNING, this, *FString::Printf(TEXT("Path to %s is very long - add a pathnode in between."), PathList(i)->End->GetName()));
		}
	// do walks from this path, check that when lose this path, find another
	// FIXME implement

	return true;
	unguard;
}

UBOOL AJumpDest::ReviewPath(APawn* Scout)
{
	guard(AJumpDest::ReviewPath)

	// check there is a forced path to me
	if ( !bOptionalJumpDest )
	{
		UBOOL bHasForcedPath = false;
		for ( ANavigationPoint* N=Level->NavigationPointList; N!=NULL; N=N->nextNavigationPoint )
			if ( N != this )
				for ( INT i=0; i<N->PathList.Num(); i++ )
					if ( (N->PathList(i)->End == this) && ((N->PathList(i)->reachFlags & R_FORCED) != 0) )
					{
						bHasForcedPath = true;
						break;
					}
		if ( !bHasForcedPath )
			GWarn->MapCheck_Add( MCTYPE_WARNING, this, TEXT("JumpDest has no forced paths to it."));
	}
	Super::ReviewPath(Scout);
	return true;
	unguard;
}

UBOOL APathNode::ReviewPath(APawn* Scout)
{
	guard(APathNode::ReviewPath)

	// check if should be JumpDest
	for ( INT i=0; i<PathList.Num(); i++ )
		if ( (PathList(i)->reachFlags & R_PROSCRIBED) == 0 )
		PathList(i)->End->CheckSymmetry(this);

	Super::ReviewPath(Scout);
	return true;
	unguard;
}

void APathNode::CheckSymmetry(ANavigationPoint* Other)
{
	guard(APathNode::CheckSymmetry)

	for ( INT i=0; i<PathList.Num(); i++ )
		if ( (PathList(i)->End == Other) && ((PathList(i)->reachFlags & R_PROSCRIBED) == 0) )
			return;

	// check for short enough alternate path to warrant no symmetry
	FLOAT Dist = (Location - Other->Location).Size();
	for ( ANavigationPoint* N=Level->NavigationPointList; N!=NULL; N=N->nextNavigationPoint )
		N->visitedWeight = 0.f;
	if ( CanReach(Other,Dist * 1.5f * PATHPRUNING) )
		return;

	GWarn->MapCheck_Add( MCTYPE_ERROR, Other, *FString::Printf(TEXT("Should be JumpDest for %s!"), GetName()));
	unguard;
}

UReachSpec* ANavigationPoint::GetReachSpecTo(ANavigationPoint *Nav)
{
	guard(ANavigationPoint::GetReachSpecTo)

	for (INT i=0; i<PathList.Num(); i++ )
		if ( PathList(i)->End == Nav ) 
			return PathList(i);
	return NULL;
	unguard;
}

AActor* AActor::AssociatedLevelGeometry()
{
	guard(AActor::AssociatedLevelGeometry);

	if ( bWorldGeometry )
		return this;

	return NULL;
	unguard;
}

AActor* ADoor::AssociatedLevelGeometry()
{
	guard(ADoor::AssociatedLevelGeometry);

	return MyDoor;
	unguard;
}

UBOOL AActor::HasAssociatedLevelGeometry(AActor *Other)
{
	guard(AActor::HasAssociatedLevelGeometry);

	return ( bWorldGeometry && (Other == this) );
	unguard;
}

UBOOL ADoor::HasAssociatedLevelGeometry(AActor *Other)
{
	guard(ADoor::HasAssociatedLevelGeometry);

	if ( !Other )
		return false;

	for ( AMover *Mine=MyDoor; Mine!=NULL; Mine=Mine->Follower )
		if ( Mine == Other )
			return true;

	return false;
	unguard;
}

/* if navigationpoint is moved, paths are invalid
*/
void ANavigationPoint::PostEditMove()
{
	guard(ANavigationPoint::PostEditMove);
	Level->bPathsRebuilt = 0;
	bPathsChanged = true;
	unguard;
}

/* if navigationpoint is spawned, paths are invalid
*/
void ANavigationPoint::Spawned()
{
	guard(ANavigationPoint::Spawned);
	Level->bPathsRebuilt = 0;
	bPathsChanged = true;
	unguard;
}

/* if navigationpoint is spawned, paths are invalid
NOTE THAT NavigationPoints should not be destroyed during gameplay!!!
*/
void ANavigationPoint::Destroy()
{
	guard(ANavigationPoint::Destroy);

	AActor::Destroy();

	if (GIsEditor && GetLevel())
	{
		Level->bPathsRebuilt = 0;

		// clean up reachspecs referring to this NavigationPoint
		for ( INT i=0; i<PathList.Num(); i++ )
			if ( PathList(i) )
				PathList(i)->Start = NULL;

		for ( INT i=0; i<GetLevel()->Actors.Num(); i++ )
		{
			ANavigationPoint *Nav = Cast<ANavigationPoint>(GetLevel()->Actors(i));
			if ( Nav )
			{
				for ( INT j=0; j<Nav->PathList.Num(); j++ )
					if ( Nav->PathList(j) && Nav->PathList(j)->End == this )
					{
						Nav->PathList(j)->reachFlags |= R_PRUNED;
						Nav->PathList(j)->End = NULL;
					}
				Nav->CleanUpPruned();
			}
		}
	}

	unguard;
}

void ANavigationPoint::CleanUpPruned()
{
	guard(ANavigationPoint::CleanUpPruned);

	for ( INT i=PathList.Num()-1; i>=0; i-- )
		if ( PathList(i) && (PathList(i)->reachFlags & R_PRUNED) )
			PathList.Remove(i);
	PathList.Shrink();
	unguard;
}

/* ProscribedPathTo()
returns 2 if no path is permited because L.D. proscribed it
returns 1 if no path from this to dest is permited for other reasons
*/
INT ANavigationPoint::ProscribedPathTo(ANavigationPoint *Nav)
{
	guard(ANavigationPoint::ProscribedPathTo);

	if ( (bOneWayPath && (((Nav->Location - Location) | Rotation.Vector()) <= 0))
		|| ((Location - Nav->Location).SizeSquared() > MAXPATHDISTSQ) )
		return 1;
					 
	// see if destination is in list of proscribed paths
	for (INT j=0; j<4; j++ )
		if ( Nav->IsIdentifiedAs(ProscribedPaths[j]) )
			return 2;

	return 0;
	unguard;
}

UBOOL ANavigationPoint::IsIdentifiedAs(FName ActorName)
{
	guard(ANavigationPoint::IsIdentifiedAs);

	return ( ActorName == GetFName() );
	unguard;
}

UBOOL AInventorySpot::IsIdentifiedAs(FName ActorName)
{
	guard(AInventorySpot::IsIdentifiedAs);

	if ( ActorName == GetFName() )
		return true;
	return ( markedItem && (ActorName == markedItem->GetFName()) ); 
	unguard;
}

UBOOL ADoor::IsIdentifiedAs(FName ActorName)
{
	guard(ADoor::IsIdentifiedAs);

	if ( ActorName == GetFName() )
		return true;
	return ( MyDoor && (ActorName == MyDoor->GetFName()) ); 
	unguard;
}

UBOOL AAIMarker::IsIdentifiedAs(FName ActorName)
{
	guard(AAIMarker::IsIdentifiedAs);

	if ( ActorName == GetFName() )
		return true;
	return ( markedScript && (ActorName == markedScript->GetFName()) ); 
	unguard;
}

UBOOL ANavigationPoint::ShouldBeBased()
{
	return ( !PhysicsVolume->bWaterVolume && !bNotBased );
}

/* addReachSpecs()
Virtual function - adds reachspecs to path for every path reachable from it. 
*/
void ANavigationPoint::addReachSpecs(APawn * Scout, UBOOL bOnlyChanged)
{
	guard(ANavigationPoint::addReachspecs);

	bPathsChanged = bPathsChanged || !bOnlyChanged;
	UReachSpec *newSpec = ConstructObject<UReachSpec>(UReachSpec::StaticClass(),GetLevel()->GetOuter(),NAME_None,RF_Public);

	// warn if no base
	if ( !Base && ShouldBeBased() && (GetClass()->ClassFlags & CLASS_Placeable) )
		GWarn->MapCheck_Add( MCTYPE_ERROR, this, TEXT("Navigation point not on valid base, or too close to steep slope"));

	// warn if bad base
	if ( Base && Base->bPathColliding )
		GWarn->MapCheck_Add( MCTYPE_ERROR, this, *FString::Printf(TEXT("This type of NavigationPoint cannot be based on %s"), Base->GetName()) );

	// warn if imbedded in wall
	if ( Region.ZoneNumber == 0 )
		GWarn->MapCheck_Add( MCTYPE_ERROR, this, TEXT("Navigation point imbedded in level geometry"));

	for ( INT i=0; i<GetLevel()->Actors.Num(); i++)
	{
		ANavigationPoint *Nav = Cast<ANavigationPoint>(GetLevel()->Actors(i));
		if (Nav && !Nav->bDeleteMe && !Nav->bNoAutoConnect && !Nav->bSourceOnly
				&& (Nav != this) && (bPathsChanged || Nav->bPathsChanged) )
		{
			// check if paths are too close together
			if ( ((Nav->Location - Location).SizeSquared() < 2.f * HUMANRADIUS) && (Nav->GetClass()->ClassFlags & CLASS_Placeable) )
				GWarn->MapCheck_Add( MCTYPE_WARNING, this, TEXT("May be too close to other navigation points"));

			INT Proscribed = ProscribedPathTo(Nav);
			newSpec->Init();

			if ( Proscribed == 2 )
			{
				// no path allowed because LD marked it - mark it with a reachspec so LDs will know there is a proscribed path here
				newSpec->reachFlags = R_PROSCRIBED;
				newSpec->Start = this;
				newSpec->End = Nav;
				PathList.AddItem(newSpec);
				newSpec = ConstructObject<UReachSpec>(UReachSpec::StaticClass(),GetLevel()->GetOuter(),NAME_None,RF_Public);
			}
			else
			{
				INT bForced = 0;
				// check if forced path
				for (INT j=0; j<4; j++ )
				{
					if ( Nav->GetFName() == ForcedPaths[j] )
						bForced = 1;
				}

				if ( bForced )
				{
					// LD wants a path to Nav
					//epic moelfke: use defineFor() to try to get CollisionRadius/CollisionHeight as large as possible
					newSpec->defineFor(this, Nav, Scout);
					newSpec->CollisionRadius = Max(newSpec->CollisionRadius, COMMONRADIUS);
					newSpec->CollisionHeight = Max(newSpec->CollisionHeight, COMMONHEIGHT);
					newSpec->reachFlags = R_FORCED;
					newSpec->Distance = (INT)((Location - Nav->Location).Size());
					PathList.AddItem(newSpec);
					Nav->SetupForcedPath(Scout,newSpec);
					newSpec = ConstructObject<UReachSpec>(UReachSpec::StaticClass(),GetLevel()->GetOuter(),NAME_None,RF_Public);
				}
				else if ( !bDestinationOnly && !Proscribed && newSpec->defineFor(this, Nav, Scout) )
				{
					debugf(TEXT("***********added new spec from %s to %s"),GetName(),Nav->GetName());
					PathList.AddItem(newSpec);
					newSpec = ConstructObject<UReachSpec>(UReachSpec::StaticClass(),GetLevel()->GetOuter(),NAME_None,RF_Public);
				}
			}
		}
	}
	unguard;
}

void ALiftCenter::addReachSpecs(APawn * Scout, UBOOL bOnlyChanged)
{
	guard(ALiftCenter::addReachspecs);

	bPathsChanged = bPathsChanged || !bOnlyChanged;
	// find associated mover
		FindBase();
		MyLift = Cast<AMover>(Base);
		// Warn if there is no lift
		if ( !MyLift )
			GWarn->MapCheck_Add( MCTYPE_WARNING, this, TEXT("No Lift found for this LiftCenter"));

	if ( MyLift && (LiftTag != MyLift->Tag) )
		GWarn->MapCheck_Add( MCTYPE_WARNING, this, TEXT("LiftTag does not match the lift this LiftCenter is on"));
	// Warn if lift is bump open timed (the default initial state for AMover)
	if ( MyLift && (MyLift->InitialState == AMover::StaticClass()->GetDefaultActor()->InitialState) )
		GWarn->MapCheck_Add( MCTYPE_ERROR, this, TEXT("Mover associated with a LiftCenter must not be BumpOpenTimed"));

	UReachSpec *newSpec = ConstructObject<UReachSpec>(UReachSpec::StaticClass(),GetLevel()->GetOuter(),NAME_None,RF_Public);
	//debugf("Add Reachspecs for LiftCenter at (%f, %f, %f)", Location.X,Location.Y,Location.Z);
	INT NumExits = 0;

	for ( INT i=0; i<GetLevel()->Actors.Num(); i++ )
	{
		ALiftExit *Nav = Cast<ALiftExit>(GetLevel()->Actors(i)); 
		if ( Nav && (Nav->LiftTag == LiftTag) && (bPathsChanged || Nav->bPathsChanged) ) 
		{
			NumExits++;

			// calculate the mover keyframe to associate with this liftexit
			// unless level designer has already specified it
			if ( Nav->SuggestedKeyFrame == 255 )
			{
				if ( MyLift )
				{
					INT Best = -1;
					FLOAT BestDist = 1000000.f;
					FCheckResult Hit(1.f);

					for ( INT j=0; j<MyLift->NumKeys; j++ )
					{
						FVector Pos = MyLift->Location + MyLift->KeyPos[j] + LiftOffset;
						if ( Abs(Pos.Z - Nav->Location.Z) < UCONST_MAXSTEPHEIGHT )
						{
							FLOAT Dist2D = (Pos - Nav->Location).Size2D();
							if ( Dist2D < BestDist )
							{
								GetLevel()->SingleLineCheck( Hit, Nav, Pos, Nav->Location, TRACE_World, FVector(CollisionRadius,CollisionRadius, 1.f) );
								if ( Hit.Time == 1.f )
								{
									Best = j;
									BestDist = Dist2D;
								}
							}
						}
					}
					if ( Best >= 0 )
						Nav->KeyFrame = Best;
					else
						GWarn->MapCheck_Add( MCTYPE_WARNING, Nav, TEXT("No suitable keyframe found for this LiftExit"));
				}
			}
			else
				Nav->KeyFrame = Nav->SuggestedKeyFrame;

			// add reachspec from LiftCenter to LiftExit
			Nav->MyLift = MyLift;
			newSpec->Init();
			//epic moelfke: use defineFor() to try to get CollisionRadius/CollisionHeight as large as possible
			// move lift to the location it will be at when it is at the KeyFrame necessary to use Nav
			if (MyLift)
			{
				GetLevel()->FarMoveActor(MyLift, MyLift->Location + MyLift->KeyPos[Nav->KeyFrame], 0, 1, 1);
			}
			newSpec->defineFor(this, Nav, Scout);
			if (MyLift)
			{
				GetLevel()->FarMoveActor(MyLift, MyLift->Location - MyLift->KeyPos[Nav->KeyFrame], 0, 1, 1);
			}
			newSpec->CollisionRadius = Max(newSpec->CollisionRadius, COMMONRADIUS);
			newSpec->CollisionHeight = Max(newSpec->CollisionHeight, COMMONHEIGHT);
			newSpec->reachFlags = R_SPECIAL;
			newSpec->Distance = 500;
			PathList.AddItem(newSpec);
			newSpec = ConstructObject<UReachSpec>(UReachSpec::StaticClass(),GetLevel()->GetOuter(),NAME_None,RF_Public);

			// add reachspec from LiftExit to LiftCenter
			newSpec->Init();
			//epic moelfke: use defineFor() to try to get CollisionRadius/CollisionHeight as large as possible
			// move lift to the location it will be at when it is at the KeyFrame necessary to use Nav
			if (MyLift)
			{
				GetLevel()->FarMoveActor(MyLift, MyLift->Location + MyLift->KeyPos[Nav->KeyFrame], 0, 1, 1);
			}
			int Result = newSpec->defineFor(Nav, this, Scout);
			if (MyLift)
			{
				GetLevel()->FarMoveActor(MyLift, MyLift->Location - MyLift->KeyPos[Nav->KeyFrame], 0, 1, 1);
			}
			//epic moelfke: require reachability for ReachSpec from lift jump LiftExits to LiftCenter (don't force it)
			if (Result || !Nav->bLiftJumpExit)
			{
				newSpec->CollisionRadius = Max(newSpec->CollisionRadius, COMMONRADIUS);
				newSpec->CollisionHeight = Max(newSpec->CollisionHeight, COMMONHEIGHT);
				newSpec->reachFlags = R_SPECIAL;
				newSpec->Distance = 500;
				Nav->PathList.AddItem(newSpec);
				newSpec = ConstructObject<UReachSpec>(UReachSpec::StaticClass(),GetLevel()->GetOuter(),NAME_None,RF_Public);
			}
		}
	}
	
	// Warn if no lift exits
	if ( NumExits == 0 )
		GWarn->MapCheck_Add( MCTYPE_ERROR, this, TEXT("No LiftExits for this LiftCenter"));
	unguard;
}

void ATeleporter::addReachSpecs(APawn * Scout, UBOOL bOnlyChanged)
{
	guard(ATeleporter::addReachspecs);

	UReachSpec *newSpec = ConstructObject<UReachSpec>(UReachSpec::StaticClass(),GetLevel()->GetOuter(),NAME_None,RF_Public);
	//debugf("Add Reachspecs for node at (%f, %f, %f)", Location.X,Location.Y,Location.Z);
	bPathsChanged = bPathsChanged || !bOnlyChanged;

	for (INT i=0; i<GetLevel()->Actors.Num(); i++)
	{
		ATeleporter *Nav = Cast<ATeleporter>(GetLevel()->Actors(i)); 
		if ( Nav && (Nav != this) && (Nav->Tag != NAME_None) && (URL==*Nav->Tag) && (bPathsChanged || Nav->bPathsChanged) )
		{
			newSpec->Init();
			newSpec->CollisionRadius = MAXCOMMONRADIUS;
			newSpec->CollisionHeight = MAXCOMMONRADIUS;
			newSpec->reachFlags = R_SPECIAL;
			newSpec->Start = this;
			newSpec->End = Nav;
			newSpec->Distance = 100;
			PathList.AddItem(newSpec);
			newSpec = ConstructObject<UReachSpec>(UReachSpec::StaticClass(),GetLevel()->GetOuter(),NAME_None,RF_Public);
			break;
		}
	}

	ANavigationPoint::addReachSpecs(Scout, bOnlyChanged);
	unguard;
}

void APlayerStart::addReachSpecs(APawn * Scout, UBOOL bOnlyChanged)
{
	guard(APlayerStart::addReachspecs);
	ANavigationPoint::addReachSpecs(Scout, bOnlyChanged);

	// check that playerstart is useable
	Scout->SetCollisionSize( HUMANRADIUS, HUMANHEIGHT );
	if ( !GetLevel()->FarMoveActor(Scout,Location,1) )
		GWarn->MapCheck_Add( MCTYPE_ERROR, this, TEXT("PlayerStart is not useable"));

	unguard;
}

/* InitForPathFinding()
find door movers associated with ADoor actor, so the collision for these movers can be temporarily turned off
when check paths with ADoor as an end point
*/
void ADoor::InitForPathFinding()
{
	guard(ADoor::InitForPathFinding());

	// find associated mover
	if ( DoorTag != NAME_None )
	{
		MyDoor = NULL;
		for (INT i=0; i<GetLevel()->Actors.Num(); i++)
		{
			AMover * NewDoor = Cast<AMover>(GetLevel()->Actors(i));
			if ( NewDoor && ((NewDoor->Tag == DoorTag) 
					|| (MyDoor && (MyDoor->ReturnGroup != NAME_None) && (NewDoor->ReturnGroup == MyDoor->ReturnGroup))) )
			{
				if ( MyDoor && ((MyDoor->ReturnGroup == NAME_None) || (NewDoor->ReturnGroup != MyDoor->ReturnGroup)) )
				{
					GWarn->MapCheck_Add( MCTYPE_ERROR, MyDoor, TEXT("Door has same tag as another door"));
					GWarn->MapCheck_Add( MCTYPE_ERROR, NewDoor, TEXT("Door has same tag as another door"));
				}

				NewDoor->myMarker = this;
				if ( MyDoor )
				{
					NewDoor->Leader = MyDoor;
					NewDoor->Follower = MyDoor->Follower;
					MyDoor->Follower = NewDoor;
				}
				else
				{
					MyDoor = NewDoor;
					MyDoor->Leader = MyDoor;
					MyDoor->Follower = NULL;
				}
			}
		}
		if ( !MyDoor )
		{
			// Warn if there is no door
			GWarn->MapCheck_Add( MCTYPE_WARNING, this, TEXT("No Mover found for this Door"));
		}
	}
	unguard;
}

/* PrePath()
Turn off ADoor associated mover collision temporarily
*/
void ADoor::PrePath()
{
	guard(ADoor::PrePath);

	for ( AMover *D=MyDoor; D!=NULL; D=D->Follower )
		if ( D->bBlockActors && D->bCollideActors )
		{
			D->SetCollision(0, D->bBlockActors, D->bBlockPlayers);
			bTempNoCollide = 1;
		}
	unguard;
}

/* PostPath()
Turn ADoor associated mover collision back on
*/
void ADoor::PostPath()
{
	guard(ADoor::PostPath);

	if ( bTempNoCollide )
		for ( AMover *D=MyDoor; D!=NULL; D=D->Follower )
			D->SetCollision(1, D->bBlockActors, D->bBlockPlayers);

	unguard;
}

void ADoor::PostaddReachSpecs(APawn * Scout)
{
	guard(ADoor::addReachspecs);

	// add R_DOOR requirement to all reachspecs going through this node
	for (INT i=0; i<PathList.Num(); i++ )
		PathList(i)->reachFlags = PathList(i)->reachFlags | R_DOOR;

	for ( ANavigationPoint* Nav=Level->NavigationPointList; Nav!=NULL; Nav=Nav->nextNavigationPoint )
		for (INT i=0; i<Nav->PathList.Num(); i++ )
			if ( Nav->PathList(i)->End == this )
				Nav->PathList(i)->reachFlags = Nav->PathList(i)->reachFlags | R_DOOR;
	unguard;
}

void AJumpDest::SetupForcedPath(APawn* Scout, UReachSpec* Path)
{
	guard(AJumpDest::SetupForcedPath);
	
	if ( NumUpstreamPaths > 7 )
	{
		GWarn->MapCheck_Add( MCTYPE_ERROR, this, TEXT("More than 8 paths to this JumpDest!"));
		return;
	}
	// calculate NeededJumpZ
	NeededJump[NumUpstreamPaths] = FVector(0.f,0.f,1.1f*TESTJUMPZ);
	Scout->JumpZ = 10000.f;
	Scout->GroundSpeed = TESTGROUNDSPEED;
	Scout->SetCollisionSize(HUMANRADIUS,HUMANHEIGHT);
	if ( GetLevel()->FarMoveActor(Scout, Path->Start->Location) )
	{
		CalculatedGravityZ[NumUpstreamPaths] = Scout->GetGravity().Z;
		NeededJump[NumUpstreamPaths] = Scout->SuggestJumpVelocity(Location,TESTGROUNDSPEED,0.f);
	}
	Scout->JumpZ = TESTJUMPZ;
	UpstreamPaths[NumUpstreamPaths] = Path;
	NumUpstreamPaths++;
	if ( Path->Start->Location.Z > Location.Z )
	{
		// set the path's MaxLandingVelocity
		Path->MaxLandingVelocity = -1 * GetGravity().Z * appSqrt(2.f * (Location.Z - Path->Start->Location.Z)/GetGravity().Z);
	}
	unguard;
}

void AJumpPad::addReachSpecs(APawn * Scout, UBOOL bOnlyChanged)
{
	guard(AJumpPad::addReachspecs);

	ANavigationPoint::addReachSpecs(Scout,bOnlyChanged);
	Scout->JumpZ = 10000.f;
	Scout->SetCollisionSize(HUMANRADIUS,HUMANHEIGHT);
	if ( (PathList.Num() > 0) && GetLevel()->FarMoveActor(Scout, Location) )
	{
		JumpTarget = PathList(0)->End;
		FVector XYDir = JumpTarget->Location - Location;
		FLOAT ZDiff = XYDir.Z;
		FLOAT Time = 2.5f * JumpZModifier * appSqrt(Abs(ZDiff/GetGravity().Z));
		JumpVelocity = XYDir/Time; 
		JumpVelocity.Z = ZDiff/Time - 0.5f * GetGravity().Z * Time;

		//epic moelfke: if during the jump from this pad the pawn will hit any actors on the navigation network, force paths to them
		FMemMark Mark(GMem);
		FVector Vel = JumpVelocity;
		FVector Loc = Location;
		FVector Dest = PathList(0)->End->Location;
		// figure out how often we need to check to find everything along the jump
		FLOAT TickDelta;
		FLOAT XYSpeed = appSqrt(Vel.X * Vel.X + Vel.Y * Vel.Y);
		if (Abs(Vel.Z) > XYSpeed)
		{
			// fastest speed on Z axis, compare against pawn CollisionHeight
			TickDelta = Scout->CollisionHeight * 2.0f / Abs(Vel.Z);
		}
		else
		{
			// fastest speed on XY plane, compare against pawn CollisionRadius
			TickDelta = Scout->CollisionRadius * 2.0f / XYSpeed;
		}
		while ((Vel.Z > 0.f || Loc.Z > Dest.Z))
		{
			Loc += TickDelta * Vel;
			Vel += TickDelta * GetGravity();

			for ( FCheckResult* Hits = GetLevel()->Hash->ActorPointCheck(GMem, Loc, Scout->GetCylinderExtent(), TRACE_AllColliding, 0, false);
					Hits; Hits = Hits->GetNext() )
			{
				if (Hits->Actor && Hits->Actor != this && !Hits->Actor->bBlockActors)
				{
					ANavigationPoint* Nav = Cast<ANavigationPoint>(Hits->Actor);
					if (!Nav && Cast<APickup>(Hits->Actor))
					{
						Nav = ((APickup*)Hits->Actor)->myMarker;
					}
					if (Nav && !GetReachSpecTo(Nav))
					{
						// force path to the marker
						debugf(TEXT("JumpPad jump test found path from %s to %s"), GetName(), Nav->GetName());
						UReachSpec *newSpec = ConstructObject<UReachSpec>(UReachSpec::StaticClass(), GetLevel()->GetOuter(), NAME_None, RF_Public);
						newSpec->Init();
						newSpec->CollisionRadius = COMMONRADIUS;
						newSpec->CollisionHeight = COMMONHEIGHT;
						newSpec->reachFlags = R_FORCED;
						newSpec->Start = this;
						newSpec->End = Nav;
						newSpec->Distance = (INT)((Location - Nav->Location).Size());
						PathList.AddItem(newSpec);
						Nav->SetupForcedPath(Scout, newSpec);
					}
				}
			}
		}
		Mark.Pop();
	}
	else 
	{
		GWarn->MapCheck_Add( MCTYPE_ERROR, this, TEXT("No forced destination for this jumppad!"));
		JumpVelocity = FVector(0.f,0.f,3.f*TESTJUMPZ);
	}
	Scout->JumpZ = TESTJUMPZ;
	unguard;
}

/* ClearForPathFinding()
clear transient path finding properties right before a navigation network search
*/
void ANavigationPoint::ClearForPathFinding()
{
	guard(ANavigationPoint::ClearForPathFinding);

	visitedWeight = UCONST_INFINITE_PATH_COST;
	nextOrdered = NULL;
	prevOrdered = NULL;
	previousPath = NULL;
	bEndPoint = bTransientEndPoint;
	bTransientEndPoint = false;
	cost = ExtraCost + FearCost;
	if( !ExtraCostRange.IsZero() )
	{
		cost += ExtraCostRange.GetRand();
	}
	unguard;
}

/* ClearPaths()
remove all path information from a navigation point. (typically before generating a new version of this
information
*/
void ANavigationPoint::ClearPaths()
{
	guard(ANavigationPoint::ClearPaths);

	nextNavigationPoint = NULL;
	nextOrdered = NULL;
	prevOrdered = NULL;
	previousPath = NULL;
	PathList.Empty();
	unguard;
}

void AJumpDest::ClearPaths()
{
	guard(ANavigationPoint::ClearPaths);

	Super::ClearPaths();
	NumUpstreamPaths = 0;
	unguard;
}

void ALiftCenter::FindBase()
{
	guard(ALiftCenter::FindBase);

	if ( !GIsEditor )
		return;

	// find associated mover
	if ( LiftTag != NAME_None )
	{
		MyLift =  NULL;
		for (INT i=0; i<GetLevel()->Actors.Num(); i++)
		{
			if ( GetLevel()->Actors(i) && (GetLevel()->Actors(i)->Tag == LiftTag) )
			{
				AMover* FoundLift = Cast<AMover>(GetLevel()->Actors(i));
				if ( FoundLift )
				{
				if ( MyLift )
				{
						GWarn->MapCheck_Add( MCTYPE_ERROR, MyLift, TEXT("Lift has same tag as another lift"));
						GWarn->MapCheck_Add( MCTYPE_ERROR, FoundLift, TEXT("Lift has same tag as another lift"));
						return;
					}
					MyLift = FoundLift;
					MyLift->myMarker = this;
					SetBase(MyLift);
					LiftOffset = Location - MyLift->Location;
				}
			}
		}
	}
	unguard;
}

void ANavigationPoint::FindBase()
{
	guard(ANavigationPoint::FindBase);

	if ( !GIsEditor )
		return;

	SetZone(1,1);
	if (  ShouldBeBased() && (Region.ZoneNumber != 0) )
	{
		// not using find base, because don't want to fail if LD has navigationpoint slightly interpenetrating floor
		FCheckResult Hit(1.f);
		FVector CollisionSlice = FVector(HUMANRADIUS, HUMANRADIUS, 1.f);
		if ( CollisionRadius < HUMANRADIUS )
			CollisionSlice = FVector(CollisionRadius, CollisionRadius, 1.f);
		GetLevel()->SingleLineCheck( Hit, this, Location - FVector(0,0, 2.f * CollisionHeight), Location, TRACE_AllBlocking, CollisionSlice );
		if ( Hit.Actor )
		{
			if ( Hit.Normal.Z > UCONST_MINFLOORZ )
				GetLevel()->FarMoveActor(this, Hit.Location + FVector(0.f,0.f,CollisionHeight-1.f),0,1,0);
			else 
				Hit.Actor = NULL;
		}
		SetBase(Hit.Actor, Hit.Normal);
	}
	unguard;
}

void ADoor::FindBase()
{
	guard(ADoor::FindBase);
	if ( !GIsEditor )
		return;

	PrePath();
	ANavigationPoint::FindBase();
	PostPath();
	unguard;
}

/* Prune paths when an acceptable route using an intermediate path exists
*/
INT ANavigationPoint::PrunePaths()
{
	guard(ANavigationPoint::PrunePaths);

	INT pruned = 0;
	debugf(TEXT("Prune paths from %s"),GetName());

	for ( INT i=0; i<PathList.Num(); i++ )
	{
		for ( INT j=0; j<PathList.Num(); j++ )
			if ( (i!=j) && !(PathList(j)->reachFlags & R_PRUNED) && (*PathList(j) <= *PathList(i)) 
				&& PathList(j)->End->FindAlternatePath(PathList(i), PathList(j)->Distance) )
			{
				PathList(i)->reachFlags |= R_PRUNED;
				//debugf(TEXT("Pruned path to %s because of path through %s"),PathList(i)->End->GetName(),PathList(j)->End->GetName());
				j = PathList.Num();
				pruned++;
			}
	}

	CleanUpPruned();

	return pruned;
	unguard;
}

UBOOL ANavigationPoint::FindAlternatePath(UReachSpec* StraightPath, INT AccumulatedDistance)
{
	guard(ANavigationPoint::FindAlternatePath);

	FVector StraightDir = StraightPath->End->Location - StraightPath->Start->Location;

	// check if the endpoint is directly reachable
	INT i;
	for ( i=0; i<PathList.Num(); i++ )
		if ( !(PathList(i)->reachFlags & R_PRUNED) && (PathList(i)->End == StraightPath->End) 
				&& ((StraightDir | (StraightPath->End->Location - Location)) >= 0.f) )
		{
			if ( (AccumulatedDistance + PathList(i)->Distance < PATHPRUNING * StraightPath->Distance)
					&& (*PathList(i) <= *StraightPath) )
			{
				//debugf(TEXT("Direct path from %s to %s"),GetName(), PathList(i)->End->GetName());
				return true;
			}
			else 
				return false;
		}

	// now continue looking for path
	for ( INT i=0; i<PathList.Num(); i++ )
		if ( !(PathList(i)->reachFlags & R_PRUNED) 
			&& (PathList(i)->Distance > 0.f)
			&& (AccumulatedDistance + PathList(i)->Distance < PATHPRUNING * StraightPath->Distance)
			&& (*PathList(i) <= *StraightPath)
			&& (PathList(i)->End != StraightPath->Start)
			&& ((StraightDir | (PathList(i)->End->Location - Location)) > 0.f)
			&& PathList(i)->End->FindAlternatePath(StraightPath, AccumulatedDistance + PathList(i)->Distance) )
		{
			//debugf(TEXT("Partial path from %s to %s"),GetName(), PathList(i)->End->GetName());
			return true;
		}

	return false;
	unguard;
}

void ANavigationPoint::execIsForcedPathTo(FFrame &Stack,RESULT_DECL)
{
	P_GET_OBJECT(ANavigationPoint,Dest);
	P_FINISH;
	UBOOL bForced = 0;
	if (Dest != NULL)
	{
		for (INT Idx = 0; Idx < PathList.Num(); Idx++)
		{
			if (PathList(Idx)->End == Dest)
			{
				bForced = PathList(Idx)->reachFlags & R_FORCED;
				break;
			}
		}
	}
	*(UBOOL*)Result = bForced;
}
