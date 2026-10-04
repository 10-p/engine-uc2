// TrailEmitter test -- Laurent
// Based on non finished code by Daniel Vogel

	virtual void Initialize( INT InMaxParticles );
	virtual void CleanUp();
	virtual void PostEditChange();

	virtual inline FColor SetInitialPointColor(FColor ParticleColor);

	virtual void UpdateParticle( FLOAT DeltaTime, INT Index );
	virtual INT  UpdateParticles( FLOAT DeltaTime );
	virtual void SpawnParticle( INT Index, FLOAT SpawnTime, INT Flags = 0, INT SpawnFlags = 0, const FVector& LocalLocationOffset = FVector(0,0,0) );
	virtual INT  RenderParticles( FDynamicActor* DynActor, class FLevelSceneNode* SceneNode, TList<FDynamicLight*>* Lights, class FRenderInterface* RI );

	// scion sz - disabled for trails
	void SetDynamicAtten( FLOAT AttenFactor ) {}

	// scion sz -
#ifdef _XBOX
	virtual INT FillVertexBuffer( struct FUCTrailVertex** pVertices, INT SheetIndex, INT ParticleIndex, FVector& ViewLocation );
#else
	virtual INT FillVertexBuffer( struct FUCTrailVertex** pVertices, FLevelSceneNode* SceneNode );
#endif

	// scion sz -
	void AttenMaxParticles( FLOAT AttenFactor );

	virtual void Serialize( FArchive& Ar );
