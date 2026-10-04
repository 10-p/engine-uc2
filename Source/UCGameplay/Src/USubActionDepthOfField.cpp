#include "UCGameplay.h"

IMPLEMENT_CLASS(USubActionDepthOfField);

/* scion ======================================================================
 * USubActionDepthOfField::Update
 * Author: jg
 * 
 * Updates the various counters and sets the targets based upon time. Once there
 * is a transition in place, the target type switches from actor based to an
 * absolute offset from the camera position. This requires the camera location,
 * target 1's location, and target 2's location. It interpolates between the
 * target locations and figures out the distance to the camera and sets that as
 * the zone of chaos.
 *
 * input:	InScenePct	The percentage of completion for the overall scene
 *			InSM		The scene manager this object is contained within
 *
 * ============================================================================
 */
UBOOL USubActionDepthOfField::Update(FLOAT InScenePct,ASceneManager* InSM)
{
	guard(USubActionDepthOfField::Update);
	// Let UMatSubAction figure out start/stop
	UBOOL bRet = Super::Update(InScenePct,InSM);
	// Go ahead and start updating the depth of field
	if (Status == SASTATUS_Running && CameraEffect && FirstTarget)
	{
		// Verify our pointer
		UUCScreenDOF* pDOF = Cast<UUCScreenDOF>(CameraEffect);
		check(pDOF && "The camera effect must be a depth of field effect");
		// Figure out the percent complete
		FLOAT Pct = (InScenePct - PctStarting) / PctDuration;
		Pct = Min<FLOAT>( Max<FLOAT>( Pct, 0.0001f ), 1.f );
		// Calculate the amount of time that has passed for this effect
		FLOAT CurrentElapsedTime = Duration * Pct;
		FLOAT DeltaTime = CurrentElapsedTime - ElapsedTime;
		ElapsedTime = CurrentElapsedTime;
		// Check to see if we are interpolating now or not
		if (ElapsedFocusTime >= InitialFocusTime && SecondTarget != NULL)
		{
			if (ElapsedInterpTime < InterpolateTime)
			{
				// Interpolating between the two actors
				// Update how far along the interpolation time we are
				ElapsedInterpTime += DeltaTime;
				// Determine the new location
				const FVector& Location =
					(SecondTarget->Location - FirstTarget->Location) *
					Clamp<FLOAT>((ElapsedInterpTime / InterpolateTime),0.0001f,1.f);
				// Determine the vector from the camera to the target
				const FVector& CamToLoc =
					(Location - InSM->Viewer->Location).SafeNormal();
				// Calculate the distance along the view direction
				pDOF->FocusDistance = FDist(Location,InSM->Viewer->Location) *
					(InSM->Viewer->Rotation.Vector() | CamToLoc);
			}
		}
		else
		{
			// Determine the vector from the camera to the target
			const FVector& CamToLoc = (FirstTarget->Location -
				InSM->Viewer->Location).SafeNormal();
			// Calculate the distance along the view direction
			pDOF->FocusDistance = FDist(FirstTarget->Location,
				InSM->Viewer->Location) * (InSM->Viewer->Rotation.Vector() |
				CamToLoc);
			// Keep track of how long we've been focused on the other actor
			ElapsedFocusTime += DeltaTime;
		}
	}
	return bRet;
	unguard;
}
