/*=============================================================================
	APickup.h.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

	// Constructors.
	APickup() {}

	// AActor interface.
	//epic moelfke: function to create the recent map for replication, so we can skip properties that will never be replicated
	virtual void CreateRecentMap(TMap<_WORD, TArray<BYTE>*>& RecentMap);
	INT* GetOptimizedRepList( BYTE* InDefault, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel );
	virtual INT AddMyMarker(AActor *S);
	virtual void ClearMarker();
	virtual void CheckForErrors();
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

