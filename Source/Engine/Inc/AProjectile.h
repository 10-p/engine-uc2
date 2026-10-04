	void BoundProjectileVelocity();
	virtual UBOOL ShrinkCollision(AActor *HitActor);
    virtual UBOOL IsAProjectile() { return true; }
	//epic moelfke: function to create the recent map for replication, so we can skip properties that will never be replicated
	virtual void CreateRecentMap(TMap<_WORD, TArray<BYTE>*>& RecentMap);
    virtual INT* GetOptimizedRepList( BYTE* InDefault, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel );
