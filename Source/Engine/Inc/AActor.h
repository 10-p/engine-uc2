/*=============================================================================
	AActor.h.
	Copyright 1997-2002 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

	// Constructors.
	AActor() {}
	void Destroy();

	// UObject interface.
	void ProcessEvent( UFunction* Function, void* Parms, void* Result=NULL );
	void ProcessState( FLOAT DeltaSeconds );
	UBOOL ProcessRemoteFunction( UFunction* Function, void* Parms, FFrame* Stack );
	void ProcessDemoRecFunction( UFunction* Function, void* Parms, FFrame* Stack );
	void Serialize( FArchive& Ar );
	void InitExecution();
	void PostEditChange();
	virtual void PreEditUndo();
	virtual void PostEditUndo();
	void PostLoad();
	void NetDirty(UProperty* property);

	// AActor interface.
	//epic moelfke: added functions to create the recent map for replication, so we can skip properties that will never be replicated
	virtual void CreateRecentMap(TMap<_WORD, TArray<BYTE>*>& RecentMap);
	void AddToRecentMap(UProperty* It, BYTE* Default, TMap<_WORD, TArray<BYTE>*>& RecentMap);
	virtual INT* GetOptimizedRepList( BYTE* InDefault, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel );
	class ULevel* GetLevel() const;
	virtual APawn* GetPlayerPawn() const {return NULL;}
	virtual UBOOL IsPlayer() {return false;}
	UBOOL IsOwnedBy( const AActor *TestOwner ) const;
	UBOOL IsBlockedBy( const AActor* Other, DWORD TraceFlags = 0 ) const;
	UBOOL IsInZone( const AZoneInfo* Other ) const;
	UBOOL IsBasedOn( const AActor *Other ) const;
	UBOOL IsJoinedTo( const AActor *Other) const;
	// scion sz - returns true if the actor should not be rendered
	virtual UBOOL IsNoRender( const UPlayer* Viewport ) const
	{
		return bNoRender;
	}
	
	// Editor specific
	UBOOL IsHiddenEd() { return (bHiddenEd || bHiddenEdGroup); }
	
	virtual FLOAT GetNetPriority( FVector& ViewPos, FVector& ViewDir, AActor* Sent, FLOAT Time, FLOAT Lag );
	virtual FLOAT WorldLightRadius() const {return 25.f * (LightRadius+1.0f);}
	virtual UBOOL Tick( FLOAT DeltaTime, enum ELevelTick TickType );
	virtual void PostEditMove() {}
	virtual void PostEditLoad() {}
	virtual void PreRaytrace() {}
	virtual void PostRaytrace() {}
	virtual void Spawned() {}
	virtual void PreNetReceive();
	virtual void PostNetReceive();
	virtual void PostNetReceiveLocation();
	virtual void PostNetReceiveRotation();
	virtual UMaterial* GetSkin( INT Index );
	virtual UBOOL ShouldTickInEntry() { return false; }

	// Rendering info.

	virtual FCoords ToLocal() const
	{
		return GMath.UnitCoords / Rotation / Location;
	}
	virtual FCoords ToWorld() const
	{
		return GMath.UnitCoords * Location * Rotation;
	}
	FORCEINLINE FMatrix LocalToWorld() const
	{
#if 0
		FTranslationMatrix LToW( -PrePivot );
		FScaleMatrix TempScale( DrawScale3D * DrawScale );
		FRotationMatrix TempRot( Rotation );
		FTranslationMatrix TempTrans( Location );
		LToW *= TempScale;
		LToW *= TempRot;
		LToW *= TempTrans;
		return LToW;
#else
		FMatrix Result;

		FLOAT	SR	= GMath.SinTab(Rotation.Roll),
				SP	= GMath.SinTab(Rotation.Pitch),
				SY	= GMath.SinTab(Rotation.Yaw),
				CR	= GMath.CosTab(Rotation.Roll),
				CP	= GMath.CosTab(Rotation.Pitch),
				CY	= GMath.CosTab(Rotation.Yaw);
		
		FLOAT	LX	= Location.X,
				LY	= Location.Y,
				LZ	= Location.Z,
				PX	= PrePivot.X,
				PY	= PrePivot.Y,
				PZ	= PrePivot.Z;
				
		FLOAT	DX	= DrawScale3D.X * DrawScale,
				DY	= DrawScale3D.Y * DrawScale,
				DZ	= DrawScale3D.Z * DrawScale;
/*
		//epic superville & zamani: 
		// If attachment should use base's prepivot, take it into account 
		// (only when attached to cylinder - if attached to bone, prepivot of mesh is already included)
		if( bAttachWithPrepivot &&
			Base && 
			AttachmentBone == NAME_None )
		{
			PX += Base->PrePivot.X;
			PY += Base->PrePivot.Y;
			PZ += Base->PrePivot.Z;
		}
*/				
		Result.M[0][0] = CP * CY * DX;
		Result.M[0][1] = CP * DX * SY;
		Result.M[0][2] = DX * SP;
		Result.M[0][3] = 0.f;

		Result.M[1][0] = DY * ( CY * SP * SR - CR * SY );
		Result.M[1][1] = DY * ( CR * CY + SP * SR * SY );
		Result.M[1][2] = -CP * DY * SR;
		Result.M[1][3] = 0.f;

		Result.M[2][0] = -DZ * ( CR * CY * SP + SR * SY );
		Result.M[2][1] =  DZ * ( CY * SR - CR * SP * SY );
		Result.M[2][2] = CP * CR * DZ;
		Result.M[2][3] = 0.f;

		Result.M[3][0] = LX - CP * CY * DX * PX + CR * CY * DZ * PZ * SP - CY * DY * PY * SP * SR + CR * DY * PY * SY + DZ * PZ * SR * SY;
		Result.M[3][1] = LY - (CR * CY * DY * PY + CY * DZ * PZ * SR + CP * DX * PX * SY - CR * DZ * PZ * SP * SY + DY * PY * SP * SR * SY);
		Result.M[3][2] = LZ - (CP * CR * DZ * PZ + DX * PX * SP - CP * DY * PY * SR);
		Result.M[3][3] = 1.f;

		return Result;
#endif
	}
	virtual FMatrix WorldToLocal() const
	{
		return	FTranslationMatrix(-Location) *
				FInverseRotationMatrix(Rotation) *
				FScaleMatrix(FVector( 1.f / DrawScale3D.X, 1.f / DrawScale3D.Y, 1.f / DrawScale3D.Z) / DrawScale) *
				FTranslationMatrix(PrePivot);
	}
	class FDynamicActor* GetActorRenderData();
	class FDynamicLight* GetLightRenderData();
	void ClearRenderData();
	void UpdateRenderData();

	FLOAT LifeFraction()
	{
		return Clamp( 1.f - LifeSpan / GetClass()->GetDefaultActor()->LifeSpan, 0.f, 1.f );
	}
	FVector GetCylinderExtent() const {return FVector(CollisionRadius,CollisionRadius,CollisionHeight);}

	AActor* GetTopOwner();
	UBOOL IsPendingKill() {return bDeleteMe;}
	virtual void PostScriptDestroyed() {} // C++ notification that the script Destroyed() function has been called.
	
	// AActor collision functions.
	virtual UBOOL ShouldTrace(AActor *SourceActor, DWORD TraceFlags);
	virtual UPrimitive* GetPrimitive();
	UBOOL IsOverlapping( AActor *Other, FCheckResult* Hit=NULL );

	void ClearOctreeData();
	UBOOL IsInOctree()
	{
		return OctreeNodes.Num() > 0;
	}

	// AActor general functions.
	void BeginTouch(AActor *Other);
	void EndTouch(AActor *Other, UBOOL NoNotifySelf);
	void SetOwner( AActor *Owner );
	UBOOL IsBrush()       const;
	UBOOL IsStaticBrush() const;
	UBOOL IsMovingBrush() const;
	UBOOL IsVolumeBrush() const;
	UBOOL IsEncroacher() const;
	UBOOL IsAnimating(int Channel=0) const;

	virtual void NotifyBump(AActor *Other);
	void SetCollision( UBOOL NewCollideActors, UBOOL NewBlockActors, UBOOL NewBlockPlayers);
	void SetCollisionSize( FLOAT NewRadius, FLOAT NewHeight );
	void SetDrawScale( FLOAT NewScale);
	void SetDrawScale3D( FVector NewScale3D);
	void SetStaticMesh( UStaticMesh* NewStaticMesh );
	void SetDrawType( EDrawType NewDrawType );
	virtual void SetBase(AActor *NewBase, const FVector& NewFloor = FVector(0,0,1), int bNotifyActor = 1);
	FRotator GetViewRotation();
	virtual void NotifyAnimEnd( int Channel );

#ifdef WITH_LIPSINC
	virtual void NotifyLIPSincAnimEnd();
#endif

	virtual void UpdateAnimation(FLOAT DeltaSeconds);
	virtual void StartAnimPoll();
	virtual UBOOL CheckAnimFinished(int Channel);
	void UpdateTimers(FLOAT DeltaSeconds);
	virtual UBOOL CheckOwnerUpdated();
	virtual void TickAuthoritative( FLOAT DeltaSeconds );
	virtual void TickSimulated( FLOAT DeltaSeconds );
	virtual void TickSpecial( FLOAT DeltaSeconds );
    	virtual UBOOL PlayerControlled();
	virtual UBOOL IsNetRelevantFor(APlayerController* RealViewer, AActor* Viewer, const FVector& SrcLocation);
	virtual UBOOL DelayScriptReplication(FLOAT LastFullUpdateTime) { return false; }
	UBOOL AttachToBone( AActor* Attachment, FName BoneName );
	UBOOL DetachFromBone( AActor* Attachment );
	inline AActor* GetAmbientLightingActor() { return bUseLightingFromBase && Base ? Base->GetAmbientLightingActor() : this; }

	// AActor Rendering in editor
	virtual void RenderEditorInfo(FLevelSceneNode* SceneNode,FRenderInterface* RI, FDynamicActor* FDA);
	virtual void RenderEditorSelected(FLevelSceneNode* SceneNode,FRenderInterface* RI, FDynamicActor* FDA);

	// Level functions
	virtual void SetZone( UBOOL bTest, UBOOL bForceRefresh );
	virtual void SetVolumes();
 	virtual void SetVolumes(const TArray<class AVolume*>& Volumes);
	virtual void PostBeginPlay();

	// Physics functions.
	//epic moelfke: changed NewFloorV to be const ref
	virtual void setPhysics(BYTE NewPhysics, AActor* NewFloor = NULL, const FVector& NewFloorV = FVector(0,0,1));
	void FindBase();
	virtual void performPhysics(FLOAT DeltaSeconds);
	void physProjectile(FLOAT deltaTime, INT Iterations);
	virtual void BoundProjectileVelocity();
	//jg -- made a ref so there is no copy
	virtual void processHitWall(const FVector& HitNormal, AActor *HitActor);
	//jg -- made a ref so there is no copy
	virtual void processLanded(const FVector& HitNormal, AActor *HitActor, FLOAT remainingTime, INT Iterations);
	virtual void physFalling(FLOAT deltaTime, INT Iterations);
	void physicsRotation(FLOAT deltaTime);
	int fixedTurn(int current, int desired, int deltaRate);
	//jg -- Made DesiredDir const since it isn't modified
	void TwoWallAdjust(const FVector &DesiredDir, FVector &Delta, FVector &HitNormal, FVector &OldHitNormal, FLOAT HitTime);
	void physRootMotion(FLOAT DeltaTime);
	//jg -- made a ref so there is no copy
	UBOOL moveSmooth(const FVector& Delta);
	//jg -- made a ref so there is no copy
	virtual FRotator FindSlopeRotation(const FVector& FloorNormal, FRotator NewRotation);
	void UpdateRelativeRotation();
	//jg -- made a ref so there is no copy
	virtual void SmoothHitWall(const FVector& HitNormal, AActor *HitActor);
	//jg -- made a ref so there is no copy
	virtual void stepUp(const FVector& GravDir, const FVector& DesiredDir, FVector Delta, FCheckResult &Hit);
	virtual UBOOL ShrinkCollision(AActor *HitActor);
	virtual FLOAT GetMaxSpeed();

	//scion superville: Gravity accessor function
	FVector GetGravity();
	FLOAT GetLandingTime( FVector BaseLocation, FVector LandLocation, FVector Vel, UBOOL bTrace = 0 );

	//scion superville: Force attachment updates
	void UpdateAttachments();

	//scion superville: Timer management
	void	SetFuncTimer( FName FuncName, FLOAT Rate, UBOOL bLoop = 0, UBOOL bAddToTimer = 0, UBOOL bAllowMultiple = 0, FLOAT Delay = 0.f );
	void	DisableFuncTimer( FName FuncName, UBOOL bAll = 0 );
	UBOOL	FuncTimerIsActive( FName FuncName );
	FLOAT	GetFuncTimerCount( FName FuncName );
	FLOAT	GetFuncTimerRate( FName FuncName );

#ifdef WITH_KARMA
	virtual McdModelID getKModel() const;
	
	void KWake();
	void KGetRigidBodyState(FKRigidBodyState* state);
	UBOOL KIsAwake();
	void KDrawRigidBodyState(FKRigidBodyState* state, UBOOL AltColour);

	void KAddForces(FVector force, FVector torque);

	virtual void physKarma(FLOAT DeltaTime);
	void physKarma_internal(FLOAT DeltaTime);

	void preKarmaStep_skeletal(FLOAT DeltaTime);
	void postKarmaStep_skeletal();

	virtual void preKarmaStep(FLOAT DeltaTime);
	virtual void postKarmaStep();

	void physKarmaRagDoll(FLOAT DeltaTime);
	void physKarmaRagDoll_internal(FLOAT DeltaTime);
	void KFreezeRagdoll();

	virtual void preContactUpdate();
	virtual UBOOL KRepulsorsShouldHit(AActor *Actor) {return true;}
#endif

	// AI functions.
	void CheckNoiseHearing(FLOAT Loudness);
	int TestCanSeeMe(class APlayerController *Viewer);
	virtual INT AddMyMarker(AActor *S) { return 0; };
	virtual void ClearMarker() {};
	virtual AActor* AssociatedLevelGeometry();
	virtual UBOOL HasAssociatedLevelGeometry(AActor *Other);
	//epic moelfke: Added optional GravScale parameter to check against any gravity scale, not just the scale this actor is using
	//epic moelfke: changed FVectors to be const ref so there's no copy
	FVector SuggestFallVelocity(const FVector& Dest, const FVector& Start, FLOAT XYSpeed, FLOAT BaseZ, FLOAT JumpZ, FLOAT MaxXYSpeed, FLOAT GravScale = 0.f);

	// Animation functions
	virtual void	PlayAnim(INT Channel,FName SequenceName, FLOAT PlayAnimRate, FLOAT TweenTime, INT Loop);
	//scion superville: Accessor functions for Animation mirroring to attachments
	virtual void	SetAnimFrame( INT Channel, FLOAT NewFrame, INT UnitFlag = 0 );
	virtual void	FreezeAnimAt( FLOAT Time, INT Channel );
	virtual void	StopAnimating( UBOOL ClearAllButBase );
	virtual UBOOL	EnableChannelNotify( INT Channel, INT Switch );
	virtual UBOOL	SetBlendParams( INT Channel, FLOAT BlendAlpha, FLOAT InTime, FLOAT OutTime, FName StartBoneName, UBOOL bGlobalPose );
	virtual void	SetBlendAlpha( INT Channel, FLOAT NewAlpha );
	virtual void	BlendToAlpha( INT Channel, FLOAT TargetAlpha, FLOAT TimeInterval );
	virtual void	UpdateBlendAlpha( INT Channel, FLOAT GoalAlpha, FLOAT StepSize );
	virtual void	SetAnimSequence( INT Channel, FName NewSequence );
	virtual void	ScaleAnimRate( INT Channel, FLOAT Scale );
	virtual void	SetAnimRate( INT Channel, FLOAT NewRate );
	virtual void	SetOrigRate( INT Channel, FLOAT NewRate );
	virtual void	CopyAnimation( INT SrcChannel, INT DestChannel, AActor* DestActor = NULL );
	virtual void	SetTweenRate( INT Channel, FLOAT NewRate );
	virtual void	SetAnimFinished( INT Channel, UBOOL B );
	virtual void	SetAnimLoop( INT Channel, UBOOL B );
	virtual void	SetAnimLast( INT Channel, FLOAT Frame );
	virtual void	SetAnimNotify( INT Channel, UBOOL B );
	virtual void	SetOldAnimRate(INT Channel, FLOAT NewRate);
	virtual void	SetBlendInTime( INT Channel, FLOAT Time );
	virtual void	SetBlendOutTime( INT Channel, FLOAT Time );
	virtual void	SetBlendTargetInterval( INT Channel, FLOAT Time );
	virtual void	SetBlendTargetAlpha( INT Channel, FLOAT Time );
	virtual void	SetTweenProgress( INT Channel, FLOAT Time );
	virtual void	SetTimedBlend( INT Channel, UBOOL B );
	virtual void	SetGlobalPose( INT Channel, UBOOL B );
	virtual void	SetCachedSeq( INT Channel, FName NewSeq );
	virtual void	SetCachedFrame( INT Channel, FLOAT NewFrame );
	virtual void	SetStartBone( INT Channel, INT NewBone );
	virtual void	SetAnimRangeStart(INT Channel, FLOAT NewRate);
	virtual void	SetAnimRangeEnd(INT Channel, FLOAT NewRate);
	virtual void	SetNotifyIgnore( INT Channel, UBOOL B );
	virtual void	SetAltRange( INT Channel, UBOOL B );
	virtual void	SetTweening( INT Channel, UBOOL B );
	virtual void	SetMeshAnimIndex(  INT Channel, INT Index );

	//epic superville: Animation helper functions
	FLOAT GetAnimDuration( FName Seq, FLOAT Rate, FLOAT CurrentFrame );

	// Special editor behavior
	AActor* GetHitActor();
	virtual void CheckForErrors();

	// path creation
	virtual void PrePath() {};
	virtual void PostPath() {};

	// Projectors
	void AttachProjector( AProjector* Projector );
	void DetachProjector( AProjector* Projector );
	virtual AActor* GetProjectorBase();

	virtual UBOOL IsABrush()  {return false;}
	virtual UBOOL IsAMover() {return false;}
	virtual UBOOL IsAVolume() {return false;}
    virtual UBOOL IsAPlayerController() { return false; }
    virtual UBOOL IsAPawn() { return false; }
    virtual UBOOL IsAProjectile() { return false; }
	virtual UBOOL IsAWeaponAttachment() { return false; }

    virtual APlayerController* GetTopPlayerController()
    {
        AActor* TopActor = GetTopOwner();
        return (TopActor && TopActor->IsAPlayerController()) ? ((APlayerController*)TopActor) : NULL;
    }

	// Natives.
	DECLARE_FUNCTION(execPollSleep)
	DECLARE_FUNCTION(execPollFinishAnim)
	DECLARE_FUNCTION(execPollFinishInterpolation)

	//scion rwd - ambient sound registration
	void PlayAmbientSoundCue(FName cue = NAME_None);

	void KAddImpulse(FVector Impulse, FVector Position, FName BoneName);
	void KAddAngularImpulse(FVector AngImpulse);

	void CopyMaterialsToSkins(UBOOL bSetDefault = 0);

	void MoveVectorTo(FVector& out_Result, const FVector& Old, const FVector& Desired, const FVector& InterpSpeed, FLOAT DeltaTime, UBOOL bSkipNormalize);
	
	virtual FVector GetExtendedCylinder()
	{
		return FVector(CollisionRadius,CollisionRadius,CollisionHeight);
	}

	// scion sz - 
	FORCEINLINE UBOOL IsDTMesh() const
	{
		return( DrawType == DT_Mesh || DrawType == DT_NaliPawn );
	}


/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

