/* scion ======================================================================
 * Author: Epic/jg
 * ============================================================================
 *
 * This file contains the common elements shared by Unreal's original collision
 * code and the new kDOP collision system.
 *
 * ============================================================================
 */

#ifndef _COLLISION_H
#define _COLLISION_H

//
//	FCollisionCheck
//

class FCollisionCheck
{
public:

	FCheckResult*			Result;
	AActor*					Owner;
	UStaticMesh*			StaticMesh;

	// Constructor.

	FCollisionCheck(FCheckResult* InResult,AActor* InOwner,UStaticMesh* InStaticMesh)
	{
		Result = InResult;
		Owner = InOwner;
		StaticMesh = InStaticMesh;
	}
};

#endif