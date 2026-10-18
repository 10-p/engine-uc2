/*=============================================================================
	UnNetDrv.h: Unreal network driver base class.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#ifndef _UNNETDRV_H_
#define _UNNETDRV_H_

/*-----------------------------------------------------------------------------
	UPackageMapLevel.
-----------------------------------------------------------------------------*/

class ENGINE_API UPackageMapLevel : public UPackageMap
{
	DECLARE_CLASS(UPackageMapLevel,UPackageMap,CLASS_Transient,Engine);
	UNetConnection* Connection;
	UBOOL CanSerializeObject( UObject* Obj );
	UBOOL SerializeObject( FArchive& Ar, UClass* Class, UObject*& Obj );
	UPackageMapLevel()
	: UPackageMap()
	{}
	UPackageMapLevel( UNetConnection* InConnection )
	: UPackageMap(), Connection( InConnection )
	{}
};

/*-----------------------------------------------------------------------------
	UNetDriver.
-----------------------------------------------------------------------------*/

//
// Base class of a network driver attached to an active or pending level.
//
class ENGINE_API UNetDriver : public USubsystem
{
	DECLARE_ABSTRACT_CLASS(UNetDriver,USubsystem,CLASS_Transient|CLASS_Config,Engine)

	// Variables.
	TArray<UNetConnection*>		ClientConnections;
	UNetConnection*				ServerConnection;
	FNetworkNotify*				Notify;
	UPackageMap*				MasterMap;
	FLOAT						Time;
	FLOAT						ConnectionTimeout;
	FLOAT						InitialConnectTimeout;
	FLOAT						KeepAliveTime;
	FLOAT						RelevantTimeout;
	FLOAT						SpawnPrioritySeconds;
	FLOAT						ServerTravelPause;
	INT							MaxClientRate;
	INT							MaxInternetClientRate;
	INT							NetServerMaxTickRate;
	INT							LanServerMaxTickRate;
	UBOOL						AllowDownloads;
	UBOOL						ProfileStats;
	UProperty*					RoleProperty;
	UProperty*					RemoteRoleProperty;
	INT							SendCycles, RecvCycles;
	INT							MaxDownloadSize;
	TArray<FString>				DownloadManagers;
	//epic superville: Used to protect against "Standby" cheat
	FLOAT						LastReceiveTime;
	UBOOL						bStandByCheatFound;

	// Constructors.
	UNetDriver();
	void StaticConstructor();

	// UObject interface.
	void Destroy();
	void Serialize( FArchive& Ar );

	// UNetDriver interface.
	virtual void LowLevelDestroy()=0;
	virtual FString LowLevelGetNetworkNumber()=0;
	virtual void AssertValid();
	virtual UBOOL InitConnect( FNetworkNotify* InNotify, FURL& ConnectURL, FString& Error );
	virtual UBOOL InitListen( FNetworkNotify* InNotify, FURL& ListenURL, FString& Error );
	virtual void TickFlush();
	virtual void TickDispatch( FLOAT DeltaTime );
	virtual UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar=*GLog );
	virtual void NotifyActorDestroyed( AActor* Actor );
    virtual UBOOL IsDemoDriver() { return 0; } 
	virtual void PreServerTravel();

	// scion sz - ripped from UC1 for creating child splitscreen client connections
	virtual class UChildConnection* CreateChild(UNetConnection* Parent);

	// determines whether this connection should be considered when detecting standby hacks
	virtual UBOOL ShouldIncludeInStandbyCheck( const UNetConnection* Connection ) const;
	// Whether this driver runs the standby check at all (both halves: the server's and the
	// client's). 1 = Epic's behaviour; the browser's WebRTC driver returns 0.
	virtual UBOOL StandbyCheckEnabled() const { return 1; }

	void SetStandbyCheatStatus( UBOOL bStandbyEnabled );
	void UpdateLastReceiveTime();
};

#endif
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

