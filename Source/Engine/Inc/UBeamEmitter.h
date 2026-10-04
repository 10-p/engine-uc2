/*=============================================================================
	UBeamEmitter.h.
	Copyright 2001 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

	virtual void Initialize( INT InMaxParticles );
	virtual void CleanUp();
	virtual void PostEditChange();
	virtual void UpdateActorHitList();

	virtual void Scale( FLOAT ScaleFactor );

	virtual void SpawnParticle( INT Index, FLOAT SpawnTime, INT Flags = 0, INT SpawnFlags = 0, const FVector& LocalLocationOffset = FVector(0,0,0) );
	virtual INT  UpdateParticles( FLOAT DeltaTime );
	virtual INT  RenderParticles( FDynamicActor* DynActor, FLevelSceneNode* SceneNode, TList<FDynamicLight*>* Lights, FRenderInterface* RI );

	void SetHighLowFrequencyPoints( INT Index, FLOAT SpawnTime, FParticle& Particle, FVector &Direction, FVector &EndPoint, UBOOL SetEndPoint );

	// scion sz - disabled for beams
	void SetDynamicAtten( FLOAT AttenFactor ) {}
	// scion sz -
	void AttenMaxParticles( FLOAT AttenFactor );

	// scion sz -
#ifdef _XBOX
	virtual INT FillVertexBuffer( class FBeamVertex** pVertices, INT SheetIndex, INT ParticleIndex, FVector& ViewLocation );
#else
	virtual INT FillVertexBuffer( class FBeamVertex** pVertices, INT NumParticles, FLevelSceneNode* SceneNode );
#endif

	virtual void Serialize( FArchive& Ar );

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

