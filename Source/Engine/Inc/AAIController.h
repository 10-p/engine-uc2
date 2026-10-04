	INT AcceptNearbyPath(AActor *goal);
	//epic moelfke: changed the FVectors being passed by value to be const ref so there's no copy
	void AdjustFromWall(const FVector& HitNormal, AActor* HitActor);
	void SetAdjustLocation(const FVector& NewLoc);
	DECLARE_FUNCTION(execPollWaitToSeeEnemy)

	//scion rwd - virtual GotoState (PushState/PopState)
	DECLARE_FUNCTION(execGotoState)
	//scion rwd - virtual IsInState (PushState/PopState)
	DECLARE_FUNCTION(execIsInState)

