/*=============================================================================
	AController.h: AI or player.
	Copyright 2000 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

	//epic moelfke: changed a whole bunch of the FVectors being passed by value to be const ref so there's no copy

	INT* GetOptimizedRepList( BYTE* InDefault, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel );
	UBOOL Tick( FLOAT DeltaTime, enum ELevelTick TickType );

	// Seeing and hearing checks
	int CanHear(const FVector& NoiseLoc, FLOAT Loudness, AActor *Other);
	void ShowSelf();
	DWORD SeeActor(AActor *Other, UBOOL bMaySkipChecks=true);
	DWORD LineOfSightTo(AActor *Other, INT bUseLOSFlag=0);
	void CheckEnemyVisible();
	void StartAnimPoll();
	UBOOL CheckAnimFinished(INT Channel);
	virtual AActor* GetViewTarget();

	AActor* HandleSpecial(AActor *bestPath);
	virtual INT AcceptNearbyPath(AActor* goal);
	virtual void AdjustFromWall(const FVector& HitNormal, AActor* HitActor);
	void SetRouteCache(ANavigationPoint *EndPath, FLOAT StartDist, FLOAT EndDist);
	AActor* FindPath(const FVector& point, AActor* goal, UBOOL bWeightDetours);
	AActor* SetPath(INT bInitialPath=1);
	virtual void SetAdjustLocation(const FVector& NewLoc);
	virtual UBOOL LocalPlayerController();
	virtual UBOOL WantsLedgeCheck();
	virtual UBOOL StopAtLedge();
	void CheckFears();

	//scion superville: Get dot products to target Returns dist to target
	FLOAT GetDotToTarget(AActor *Target, const FVector& Start, FVector Direction, FLOAT &out_LatDot, FLOAT &out_VertDot);

	//epic moelfke: Returns true if a superpickup desireable to this Controller is ready to be picked up
	UBOOL IsSuperPickupAvailable();

	// Natives.
	DECLARE_FUNCTION(execPollWaitForLanding)
	DECLARE_FUNCTION(execPollMoveTo)
	DECLARE_FUNCTION(execPollMoveToward)
	DECLARE_FUNCTION(execPollFinishRotation)

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

