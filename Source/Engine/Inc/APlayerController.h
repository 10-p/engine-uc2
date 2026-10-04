/*=============================================================================
	APlayerController.h: A player pawn.
	Copyright 2000 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

	void Destroy();

	//  Player Pawn interface.
	void SetPlayer( UPlayer* Player );

	// AActor interface.
	INT* GetOptimizedRepList( BYTE* InDefault, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel );
	UBOOL Tick( FLOAT DeltaTime, enum ELevelTick TickType );
	UBOOL IsNetRelevantFor(APlayerController* RealViewer, AActor* Viewer, const FVector& SrcLocation);
	AActor* GetViewTarget();
	virtual UBOOL LocalPlayerController();
	virtual UBOOL WantsLedgeCheck();
	virtual UBOOL StopAtLedge();

    virtual UBOOL IsAPlayerController() { return true; }
	virtual void PostScriptDestroyed();
	void PostNetReceive();

	virtual UBOOL GetPlayerURLOption( const FString& OptionName, FString& OptionValue );
	virtual void UpdatePlayerURL( const FString& Option, const FString& Value );
	virtual void ClearPlayerURL( FString& Option );


/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

