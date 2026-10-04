/*=============================================================================
	UnGame.h: Unreal game class.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

/*-----------------------------------------------------------------------------
	Unreal game engine.
-----------------------------------------------------------------------------*/

//
// The Unreal game engine.
//
class ENGINE_API UGameEngine : public UEngine
{
	DECLARE_CLASS(UGameEngine,UEngine,CLASS_Config|CLASS_Transient,Engine)

	// Variables.
	ULevel*			GLevel;
	ULevel*			GEntry;
	UPendingLevel*	GPendingLevel;
	FURL			LastURL;
	TArrayNoInit<FString> ServerActors;
	TArrayNoInit<FString> ServerPackages;

    FStringNoInit   MainMenuClass;
    FStringNoInit   ConnectingMenuClass;
    FStringNoInit   DisconnectMenuClass;
	FStringNoInit	LoadingClass;

	// Constructors.
	UGameEngine();

	// UObject interface.
	void Serialize( FArchive& Ar );
	void Destroy();

	// UEngine interface.
	void Init();
	void Tick( FLOAT DeltaSeconds );
	void Draw( UViewport* Viewport, FLOAT DeltaSeconds, UBOOL Blit=1, BYTE* HitData=NULL, INT* HitSize=NULL );
	UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar=*GLog );
	void MouseDelta( UViewport*, DWORD, FLOAT, FLOAT );
	void MousePosition( class UViewport*, DWORD, FLOAT, FLOAT );
	void MouseWheel( UViewport* Viewport, DWORD Buttons, INT Delta );
	void Click( UViewport*, DWORD, FLOAT, FLOAT );
	void UnClick( UViewport*, DWORD, INT, INT );
	void SetClientTravel( UPlayer* Viewport, const TCHAR* NextURL, UBOOL bItems, ETravelType TravelType );
	FLOAT GetMaxTickRate();
	INT ChallengeResponse( INT Challenge );
	void SetProgress( const TCHAR* Str1, const TCHAR* Str2, FLOAT Seconds );

	// UGameEngine interface.
	virtual UBOOL Browse( FURL URL, const TMap<FString,FString>* TravelInfo, FString& Error );
	//epic moelfke: added bQuiet, if set don't call SetProgress() on failure
	virtual ULevel* LoadMap( const FURL& URL, UPendingLevel* Pending, const TMap<FString,FString>* TravelInfo, FString& Error, UBOOL bQuiet = 0 );
	//scion sz - 
	virtual void UnloadMap();
	virtual void SaveGame( INT Position );
	virtual void CancelPending();
	virtual void PaintProgress();
	virtual void UpdateConnectingMessage();
	virtual void BuildServerMasterMap( UNetDriver* NetDriver, ULevel* InLevel );
	virtual void NotifyLevelChange();
	void FixUpLevel();
	void PostFixupLevel( TCHAR* GameClassName );

	//scion rwd - various precaches functions
	void PrecacheStaticMeshes(UViewport *Viewport, BYTE* HitData, INT* HitSize, AActor *CameraActor, FVector &CameraLocation, FRotator &CameraRotation);
	void PrecacheMeshes(UViewport *Viewport, BYTE* HitData, INT* HitSize, AActor *CameraActor, FVector &CameraLocation, FRotator &CameraRotation);
	void PrecacheMaterials(UViewport *Viewport, BYTE* HitData, INT* HitSize, AActor *CameraActor, FVector &CameraLocation, FRotator &CameraRotation);

private:
	// scion sz -
	void DissociateViewports();
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/


