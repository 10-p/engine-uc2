/*=============================================================================
	UCComboSceneManager.cpp:
	Copyright 2003 Scion Studios. All Rights Reserved.
=============================================================================*/

#include "EnginePrivate.h"

IMPLEMENT_CLASS(AUCComboSceneManager);
IMPLEMENT_CLASS(AUCCameraActor);

/* scion ======================================================================
 * AUCComboSceneManager::execReleaseObjects
 * Author: jg
 * 
 * Native implementation of ReleaseObjects. Cleans up the spawned interpolation
 * points and delete the sub-objects.
 *
 * ============================================================================
 */
void AUCComboSceneManager::execReleaseObjects(FFrame& Stack,RESULT_DECL)
{
	guardSlow(AUCComboSceneManager::execReleaseObjects);
	P_FINISH;
	// Loop through all the actions and delete them
	for (INT nIndex = 0; nIndex < Actions.Num(); nIndex++)
	{
		// Delete all the sub actions for this action first
		for (INT nSubIndex = 0; nSubIndex < Actions(nIndex)->SubActions.Num();
			nSubIndex++)
		{
			delete Actions(nIndex)->SubActions(nSubIndex);
		}
		// Empty the subaction array and then delete the current action
		Actions(nIndex)->SubActions.Empty();
		delete Actions(nIndex);
	}
	Actions.Empty();
	unguardSlow;
}

/* scion ======================================================================
 * AUCComboSceneManager::execBuildInterpolationPointSamples
 * Author: jg
 * 
 * Native implementation of BuildInterpolationPointSamples. Re/builds the
 * interpolation point path(s).
 *
 * ============================================================================
 */
void AUCComboSceneManager::execBuildInterpolationPointSamples(FFrame& Stack,
	RESULT_DECL)
{
	guardSlow(AUCComboSceneManager::execBuildInterpolationPointSamples);
	P_FINISH;
	PreparePath();
	unguardSlow;
}

/* scion ======================================================================
 * AUCComboSceneManager::Tick
 * Author: jg
 * 
 * Moves the scene manager actor to keep current position with the attached
 * actor. This allows the artist to specify the scene manager as a look at and
 * it will follow the player as they move.
 *
 * ============================================================================
 */
UBOOL AUCComboSceneManager::Tick(FLOAT DeltaTime,enum ELevelTick TickType)
{
	guard(AUCComboSceneManager::Tick);
	
	// Call the super so that the matinee sequence is updated
	UBOOL bRet = Super::Tick(DeltaTime,TickType);
	// If we are attached to an actor, move this actor to their position
	// Don't update the position if either of these are dead or we aren't
	// interpolating the scene
	if (bIsRunning && MoveRelativeTo != NULL &&
		MoveRelativeTo->bDeleteMe == 0 && bDeleteMe == 0)
	{
		GetLevel()->FarMoveActor(this,MoveRelativeTo->Location,0,1,1);
	}
	return bRet;
	unguard;
}

/* scion ======================================================================
 * AUCComboSceneManager::UpdateViewerLocation
 * Author: jg
 * 
 * Updates the viewer's location based upon scene percentage and any changes in
 * the attached actor's location. It also checks that the viewer is placed into
 * a valid location and adjust that if it collides with something.
 *
 * ============================================================================
 */
void AUCComboSceneManager::UpdateViewerLocation(FLOAT InScenePct)
{
	guardSlow(AUCComboSceneManager::UpdateViewerLocation);

	// Get the location adjusted by the action
	FVector NewLoc = GetLocation(&CurrentAction->SampleLocations,InScenePct);
	// Now add in the accumulated change in pawn position if applicable
	if (MoveRelativeTo != NULL && Affect == AFFECT_ViewportCamera)
	{
		// Update the amount of change thus far
		TotalPositionChange += LastPosition - MoveRelativeTo->Location;
		// Track for delta calculations
		LastPosition = MoveRelativeTo->Location;
		// And now adjust the returned location by this amount
		NewLoc += TotalPositionChange;
	}
	FCheckResult Hit(1.f);
	// Now move the camera to the new location
	if( InterpActor && 
		GetLevel()->MoveActor(InterpActor,NewLoc - InterpActor->Location,FRotator(0,0,0),Hit) == 0 )
	{
		// It collided so move to where it hit
		GetLevel()->MoveActor(InterpActor,Hit.Location - InterpActor->Location,
			FRotator(0,0,0),Hit);
	}

	//epic superville: If camera is inside a pawn, don't render it
	if( InterpActor && InterpActor->Owner && InterpActor->Owner->IsAPlayerController() )
	{
		APlayerController* PC = Cast<APlayerController>(InterpActor->Owner);
		if( PC->LocalPlayerController() )
		{
			for( APawn* P = GetLevel()->GetLevelInfo()->PawnList; P; P = P->NextPawn )
			{
				// Check to see if should toggle rendering if camera clips collision
				FVector Delta	= InterpActor->Location - P->Location;
				if( Square(Delta.Z) < Square( P->CollisionHeight * P->CameraNoRenderScale ) &&
					Delta.SizeSquared2D() < Square( P->CollisionRadius * P->CameraNoRenderScale ) ) 
				{
					P->bNoRender = true;
				}
				else {
					P->bNoRender = false;
				}
			}
		}
	}
	
	unguardSlow;
}

/* scion ======================================================================
 * AUCComboSceneManager::CopySceneManager
 * Author: jg
 * 
 * Copies the properties from the passed in scene manager into this object so
 * that it can be exported into class form. It also calculates the relative
 * position/rotation for each interpolation point that is used by the matinee.
 * The interpolation points aren't exported. They are respawned using their
 * calculated position/rotation when the combo manager is spawned.
 *
 * ============================================================================
 */
void AUCComboSceneManager::CopySceneManager(ASceneManager* pSceneManager)
{
	guard(AUCComboSceneManager::CopySceneManager);

	// Copy the scenemanager properties
	Affect			= pSceneManager->Affect;
	bLooping		= pSceneManager->bLooping;
	bCinematicView	= pSceneManager->bCinematicView;
	PlayerScriptTag = pSceneManager->PlayerScriptTag;
	NextSceneTag	= pSceneManager->NextSceneTag;

	// Walk through the list of actions and copy the action pointers
	for( INT nIndex = 0; nIndex < pSceneManager->Actions.Num(); nIndex++ )
	{
		// Get a pointer to the action we are copying
		UMatAction* pToCopy = pSceneManager->Actions(nIndex);
		// Both the action and the interpolation point need to be valid
		if( pToCopy )
		{
			if( pToCopy->IntPoint )
			{
				// Make a copy of the matinee action object so we can modify it
				// and not affect the original
				UMatAction* pCopy = (UMatAction*)StaticConstructObject(
					pToCopy->GetClass(),GetOuter(),NAME_None,0,pToCopy);
				// Copy the action pointer
				Actions.AddItem(pCopy);
				INT nSpawnIndex = IPSpawnPositions.AddZeroed();
				// Add the interpolation point's relative location/rotation
				IPSpawnPositions(nSpawnIndex).RelativeLocation =
					pCopy->IntPoint->Location - Location;
				IPSpawnPositions(nSpawnIndex).RelativeRotation =
					pCopy->IntPoint->Rotation - Rotation;
				// Zero out the interpolation point
				pCopy->IntPoint = NULL;
			}

			// Go through each sub action and save out relative info
			for( INT nSubIndex = 0; nSubIndex < pToCopy->SubActions.Num(); nSubIndex++ )
			{
				// If it is an orientation sub action
				USubActionOrientation* pSubAction = Cast<USubActionOrientation>(pToCopy->SubActions(nSubIndex));
				if( pSubAction )
				{
					// If the orientation is going to look at an actor by tag and the tag is valid
					if( pSubAction->CamOrientation.bLookAtByTag && 
						pSubAction->CamOrientation.LookAt &&
						pSubAction->CamOrientation.LookAt->Tag != NAME_None )
					{
						// Search through current actor spawn list and see if we already have an actor with this tag
						UBOOL bAdd = 1;
						for( INT ActorIdx = 0; ActorIdx < ActorSpawnPositions.Num(); ActorIdx++ )
						{
							if( ActorSpawnPositions(ActorIdx).ActorTag == pSubAction->CamOrientation.LookAt->Tag )
							{
								bAdd = 0;
								break;
							}
						}

						if( bAdd )
						{
							// Add this actor to be spawned w/ relative location/rotation
							INT nSpawnIndex = ActorSpawnPositions.AddZeroed();

							ActorSpawnPositions(nSpawnIndex).ActorClass = 
								pSubAction->CamOrientation.LookAt->GetClass();
							ActorSpawnPositions(nSpawnIndex).ActorTag = 
								pSubAction->CamOrientation.LookAt->Tag;
							ActorSpawnPositions(nSpawnIndex).RelativeLocation =
								pSubAction->CamOrientation.LookAt->Location - Location;
							ActorSpawnPositions(nSpawnIndex).RelativeRotation =
								pSubAction->CamOrientation.LookAt->Rotation - Rotation;
						}
					}
				}
			}
		}
	}

	// These two arrays must match in size
	check(Actions.Num() && IPSpawnPositions.Num());
	unguard;
}
