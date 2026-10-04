/*=============================================================================
	UParticleEmitter.h.
	Copyright 2001 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

	virtual	void PostLoad();
	virtual void Destroy();
	virtual void PostEditChange();

	virtual void Initialize( INT InMaxParticles );
	virtual void CleanUp();
	virtual void Reset();
	virtual void Die();

	virtual void Scale( FLOAT ScaleFactor );
	
	virtual FLOAT SpawnParticles( FLOAT OldLeftover, FLOAT Rate, FLOAT DeltaTime );
	// scion sz - no need to construct FVector(0,0,0) each call
	virtual void SpawnParticle( INT Index, FLOAT SpawnTime, INT Flags = 0, INT SpawnFlags = 0, const FVector& LocalLocationOffset = GMath.ZeroVector );
	virtual INT UpdateParticles( FLOAT DeltaTime );
	virtual void UpdateParticle( FLOAT DeltaTime, INT Index ) {};
	virtual INT RenderParticles( FDynamicActor* DynActor, class FLevelSceneNode* SceneNode, TList<FDynamicLight*>* Lights, FRenderInterface* RI );

	// scion sz -
	virtual void AttenMaxParticles( FLOAT AttenFactor );
	// scion sz -
	virtual void SetDynamicAtten( FLOAT AttenFactor );
	// scion sz -
	FORCEINLINE void UpdateRelativeRot( UBOOL bForce );

	virtual void Serialize( FArchive& Ar );

//jg -- Changes needed for changing from a preallocated array of particles
// to a dynamically growing list
	#define FPooledParticle TPooledList<FParticle>
	// Gets a particle from the pool
	FORCEINLINE FParticle* CreateParticle(void)
	{
		guard(CreateParticle);
		// Get a new particle from the pool
		FPooledParticle* pPooled = FPooledParticle::GetInstance(NULL);
		return &pPooled->Element;
		unguard;
	}
	// Returns a particle to the pool
	FORCEINLINE void DestroyParticle(FParticle* Particle)
	{
		guard(DestroyParticle);
		// Return the instance to the pool
		FPooledParticle::ReturnInstance((FPooledParticle*)Particle);
		unguard;
	}
//jg -- End list of particle creation changes

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

