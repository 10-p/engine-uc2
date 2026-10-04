/*=============================================================================
	ALevelInfo.h.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

	// Constructors.
	ALevelInfo() {}

	// AActor interface.
	INT* GetOptimizedRepList( BYTE* InDefault, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel );
	void CheckForErrors();
	void PreNetReceive();
	void PostNetReceive();

	// Level functions
	void SetZone( UBOOL bTest, UBOOL bForceRefresh );
	void SetVolumes();
	APhysicsVolume* GetDefaultPhysicsVolume();
	APhysicsVolume* GetPhysicsVolume(FVector Loc, AActor *A, UBOOL bUseTouch);

	//epic moelfke: called when game is paused or unpaused. Notifies any subsystems that need to know about it.
	FORCEINLINE void NotifyPauseChange(UBOOL bNowPaused);

	//jg -- Hack for XPRs not being loaded in time
	void WaitForDynamicLoadingComplete(void);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

