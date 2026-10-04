/*=============================================================================
	UUCRibbonEmitter.h.
=============================================================================*/

	virtual	void PostLoad();
	virtual void Destroy();
	virtual void PostEditChange();

	virtual void Initialize( INT InMaxParticles );
	virtual void CleanUp();
	//virtual void UpdateActorHitList();

	virtual void Scale( FLOAT ScaleFactor );

	virtual void SpawnParticle( INT Index, FLOAT SpawnTime, INT Flags = 0, INT SpawnFlags = 0, const FVector& LocalLocationOffset = FVector(0,0,0) );
	virtual INT  UpdateParticles( FLOAT DeltaTime );
	virtual INT  RenderParticles( FDynamicActor* DynActor, FLevelSceneNode* SceneNode, TList<FDynamicLight*>* Lights, FRenderInterface* RI );

	// scion sz - disabled for ribbons
	void SetDynamicAtten( FLOAT AttenFactor ) {}

	virtual void GetNewRibbonPoint(FVector *PointLocation, FVector *PointAxis, FLOAT *PointWidth);

	virtual FLOAT GetGlobalScale(FLOAT RelativeTime);
	virtual FLOAT GetPointScale(FLOAT RelativeTime, INT PointNum);

	void* FillVertexBuffer( class FUCRibbonVertex** pVertices, FLevelSceneNode* SceneNode, INT Sheet );
