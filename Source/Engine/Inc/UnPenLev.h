/*=============================================================================
	UnPenLev.h: Unreal pending level definition.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

/*-----------------------------------------------------------------------------
	UPendingLevel.
-----------------------------------------------------------------------------*/

//
// Class controlling a pending game level.
//
class UPendingLevel : public ULevelBase
{
	DECLARE_ABSTRACT_CLASS(UPendingLevel,UObject,0,Engine)
	NO_DEFAULT_CONSTRUCTOR(UPendingLevel)

	// Variables.
	UBOOL		Success;
	UBOOL		SentJoin;
	UBOOL		LonePlayer;
	INT			FilesNeeded;
	FString		Error;

	// Constructors.
	UPendingLevel( UEngine* InEngine, const FURL& InURL );

	// UPendingLevel interface.
	virtual void Tick( FLOAT DeltaTime )=0;
	virtual UNetDriver* GetDriver()=0;
	virtual void SendJoin()=0;
	virtual UBOOL TrySkipFile()=0;
};

/*-----------------------------------------------------------------------------
	UNetPendingLevel.
-----------------------------------------------------------------------------*/

class UNetPendingLevel : public UPendingLevel
{
	DECLARE_CLASS(UNetPendingLevel,UPendingLevel,CLASS_Transient,Engine)
	NO_DEFAULT_CONSTRUCTOR(UNetPendingLevel)

	//epic moelfke: Indicates level in URL has already been loaded in advance of handshaking
	UBOOL bPreloadedLevel;
	//epic moelfke: Indicates we are connecting to a new level on the same host
	UBOOL bSameHostAsLast;
	//epic moelfke: list of classes (fully qualified) that we should fully load the assets for before sending a join message
	TArray<FString> LoadClassNames;
	//epic moelfke: temp PlayerControllers so players can spectate the level while waiting for the above classes' assets to load
	TArray<APlayerController*> TempPCs;

	// Constructors.
	UNetPendingLevel( UEngine* InEngine, const FURL& InURL );

	//epic moelfke: needed when bPreloadedLevel is true to make sure everything gets reset properly if the connection fails
	virtual void Destroy();

	// FNetworkNotify interface.
	EAcceptConnection NotifyAcceptingConnection();
	void NotifyAcceptedConnection( class UNetConnection* Connection );
	UBOOL NotifyAcceptingChannel( class UChannel* Channel );
	ULevel* NotifyGetLevel();
	void NotifyReceivedText( UNetConnection* Connection, const TCHAR* Text );
	void NotifyReceivedFile( UNetConnection* Connection, INT PackageIndex, const TCHAR* Error, UBOOL Skipped );
	UBOOL NotifySendingFile( UNetConnection* Connection, FGuid Guid );

	// UPendingLevel interface.
	void Tick( FLOAT DeltaTime );
	UNetDriver* GetDriver() { return NetDriver; }
	void SendJoin();
	UBOOL TrySkipFile();

	// UNetPendingLevel interface
	void ReceiveNextFile( UNetConnection* Connection );
	//epic moelfke: added function to attempt loading a level before handshaking with the server
	void TryPreloadingLevel(UGameEngine* GameEngine);
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

