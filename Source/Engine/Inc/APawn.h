/*=============================================================================
	APawn.h: Class functions residing in the APawn class.
	Copyright 1997-2001 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

// declare type for node evaluation functions
typedef FLOAT ( *NodeEvaluator ) (ANavigationPoint*, APawn*, FLOAT);

	//epic moelfke: changed a whole bunch of the FVectors being passed by value to be const ref so there's no copy

	// Constructors.
	APawn() {}

	// AActor interface.
	APawn* GetPlayerPawn() const;
	FLOAT GetNetPriority( FVector& ViewPos, FVector& ViewDir, AActor* Sent, FLOAT Time, FLOAT Lag );
	virtual UBOOL DelayScriptReplication(FLOAT LastFullUpdateTime);
	INT* GetOptimizedRepList( BYTE* InDefault, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel );
	void NotifyAnimEnd( int Channel );
	virtual UBOOL IsNoRender( const UPlayer* Viewport ) const;

#ifdef WITH_LIPSINC
	void NotifyLIPSincAnimEnd();
#endif

	void NotifyBump(AActor *Other);
	UBOOL CheckOwnerUpdated();
	void TickSimulated( FLOAT DeltaSeconds );
	void TickSpecial( FLOAT DeltaSeconds );
	UBOOL PlayerControlled();
	void SetBase(AActor *NewBase, const FVector& NewFloor = FVector(0,0,1), int bNotifyActor = 1);
	void CheckForErrors();
	UBOOL IsNetRelevantFor(APlayerController* RealViewer, AActor* Viewer, const FVector& SrcLocation);
	UBOOL CacheNetRelevancy(UBOOL bIsRelevant, APlayerController* RealViewer, AActor* Viewer);
	UBOOL ShouldTrace(AActor *SourceActor, DWORD TraceFlags);
	void PreNetReceive();
	void PostNetReceive();
	void PostNetReceiveLocation();
	void PostNetReceiveRotation();
	void RenderEditorSelected(FLevelSceneNode* SceneNode,FRenderInterface* RI, FDynamicActor* FDA);

	// Level functions
	void SetZone( UBOOL bTest, UBOOL bForceRefresh );

	// Latent movement
	void setMoveTimer(FLOAT MoveSize);
	virtual FLOAT GetMaxSpeed();
	virtual UBOOL moveToward(const FVector &Dest, AActor *GoalActor);
	virtual void rotateToward(AActor *Focus, FVector FocalPoint);
	UBOOL PickWallAdjust(const FVector& WallHitNormal);
	void StartNewSerpentine(const FVector& Dir, const FVector& Start);
	void ClearSerpentine();

	// reach tests
	UBOOL ReachedDestination(FVector Dir, AActor* GoalActor);
	virtual int pointReachable(FVector aPoint, int bKnowVisible = 0);
	virtual int actorReachable(AActor *Other, UBOOL bKnowVisible = 0, UBOOL bNoAnchorCheck = 0);
	int Reachable(const FVector& aPoint, AActor* GoalActor);
	int walkReachable(const FVector& Dest, int reachFlags, AActor* GoalActor);
	int jumpReachable(const FVector& Dest, int reachFlags, AActor* GoalActor);
	int flyReachable(const FVector& Dest, int reachFlags, AActor* GoalActor);

	// movement component tests (used by reach tests)
	ETestMoveResult jumpLanding(FVector testvel, int moveActor);
	ETestMoveResult walkMove(FVector Delta, FCheckResult& Hit, AActor* GoalActor, FLOAT threshold);
	ETestMoveResult flyMove(FVector Delta, AActor* GoalActor, FLOAT threshold);
	ETestMoveResult FindBestJump(const FVector& Dest);
	ETestMoveResult FindJumpUp(const FVector& Direction);
	ETestMoveResult HitGoal(AActor *GoalActor);
	FVector SuggestJumpVelocity(const FVector& Dest, FLOAT XYSpeed, FLOAT BaseZ);
	virtual UBOOL HurtByVolume(AActor *A);

	// Path finding
	UBOOL ValidAnchor();
	FLOAT findPathToward(AActor *goal, FVector GoalLocation, NodeEvaluator NodeEval, FLOAT BestWeight, UBOOL bWeightDetours);
	ANavigationPoint* breadthPathTo(NodeEvaluator NodeEval, ANavigationPoint *start, int moveFlags, FLOAT *Weight, UBOOL bWeightDetours);
	ANavigationPoint* CheckDetour(ANavigationPoint* BestDest, ANavigationPoint* Start, UBOOL bWeightDetours);
	int calcMoveFlags();
	void SetAnchor(ANavigationPoint *NewAnchor);

	// Pawn physics modes
	//epic moelfke: added APawn version of setPhysics() to record when pawn starts falling for stats
	virtual void setPhysics(BYTE NewPhysics, AActor* NewFloor = NULL, const FVector& NewFloorV = FVector(0,0,1));
	void performPhysics(FLOAT DeltaSeconds);
	FVector CheckForLedges(const FVector& AccelDir, const FVector& Delta, const FVector& GravDir, int &bCheckedFall, int &bMustJump);
	void physWalking(FLOAT deltaTime, INT Iterations);
	void physFlying(FLOAT deltaTime, INT Iterations);
	void physFalling(FLOAT deltaTime, INT Iterations);
	void startNewPhysics(FLOAT deltaTime, INT Iterations);
	void physicsRotation(FLOAT deltaTime, const FVector& OldVelocity);
	void processLanded(const FVector& HitNormal, AActor *HitActor, FLOAT remainingTime, INT Iterations);
	void processHitWall(FVector HitNormal, AActor *HitActor);
	void SmoothHitWall(const FVector& HitNormal, AActor *HitActor);
	FVector NewFallVelocity(const FVector& OldVelocity, const FVector& OldAcceleration, FLOAT timeTick);
	void stepUp(const FVector& GravDir, const FVector& DesiredDir, FVector Delta, FCheckResult &Hit);

	// animation functions - temp FIXME - create animation objects with this functionality
	void UpdateMovementAnimation(FLOAT DeltaSeconds);
	void ZeroMovementAlpha(INT iStart, INT iEnd, FLOAT StepSize);
	virtual void UpdatePawnSpinePitch( FLOAT DeltaTime );
	UBOOL IsSpectatorTarget();

	// Controller interface
	inline UBOOL APawn::IsPlayer()
	{
		return (Controller && Controller->bIsPlayer);
	}
	inline UBOOL APawn::IsHumanControlled()
	{
		return (Controller && Controller->IsAPlayerController());
	}
	inline UBOOL APawn::IsLocallyControlled()
	{
		return (Controller && Controller->LocalPlayerController());
	}

	virtual UBOOL IsAPawn() { return true; }
	
	//scion superville: Retrieve Extended Cylinder Extent
	FVector	GetMeleeCylinder();
	FVector GetWeaponExtent( FVector *out_AdjustLocation );
	FLOAT	CalcMaxSpeed( FLOAT maxSpeed );

	//epic superville: Get fire location from current weapon's avgfireoffset
	// Use so we don't have to call a script event from aim calculations
	FORCEINLINE FVector GetFireStart()
	{
		FRotator Rot;
		FVector	 Result;

		if( !Weapon )
		{
			return Location;
		}

		if( Controller )
		{
			Rot = (Controller->IsAPlayerController() ? Cast<APlayerController>(Controller)->CurrentAimRotation : Controller->Rotation);
		}
		else
		{
			Rot = Rotation;
		}

		FCoords Coords = GMath.UnitCoords / Rot;

		Result  = Location + FVector(0,0,EyeHeight) + WalkBob +
				  Weapon->FireOffsetAvg.X * Coords.XAxis +
				  Weapon->FireOffsetAvg.Y * Coords.YAxis +
				  Weapon->FireOffsetAvg.Z * Coords.ZAxis;

		return Result;
	}

private:
	UBOOL Pick3DWallAdjust(const FVector& WallHitNormal);
	FVector findWaterLine(const FVector& Start, const FVector& End);
	void calcVelocity(const FVector& AccelDir, FLOAT deltaTime, FLOAT maxSpeed, FLOAT friction, INT bFluid, INT bBrake );

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/


