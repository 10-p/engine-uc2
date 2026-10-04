/*=============================================================================
	UnLevel.h: ULevel definition.
	Copyright 1997-2002 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

/*-----------------------------------------------------------------------------
	Network notification sink.
-----------------------------------------------------------------------------*/

#define STANDBYCHEAT_TIMEOUT		1.0f
#define STANDBYCHEAT_FRAMERATECAP	0.5f	// Ignore any delta time over this amount to prevent false positives (should be about half the timeout)

//
// Accepting connection responses.
//
enum EAcceptConnection
{
	ACCEPTC_Reject,	// Reject the connection.
	ACCEPTC_Accept, // Accept the connection.
	ACCEPTC_Ignore, // Ignore it, sending no reply, while server travelling.
};

//
// The net code uses this to send notifications.
//
class ENGINE_API FNetworkNotify
{
public:
	virtual EAcceptConnection NotifyAcceptingConnection()=0;
	virtual void NotifyAcceptedConnection( class UNetConnection* Connection )=0;
	virtual UBOOL NotifyAcceptingChannel( class UChannel* Channel )=0;
	virtual ULevel* NotifyGetLevel()=0;
	virtual void NotifyReceivedText( UNetConnection* Connection, const TCHAR* Text )=0;
	virtual UBOOL NotifySendingFile( UNetConnection* Connection, FGuid GUID )=0;
	virtual void NotifyReceivedFile( UNetConnection* Connection, INT PackageIndex, const TCHAR* Error, UBOOL Skipped )=0;
	virtual void NotifyProgress( const TCHAR* Str1, const TCHAR* Str2, FLOAT Seconds )=0;
	virtual void NotifyStandbyHack( UBOOL bStandby )=0;
};

/*-----------------------------------------------------------------------------
	FCollisionHashBase.
-----------------------------------------------------------------------------*/

class FCollisionHashBase
{
public:
	// FCollisionHashBase interface.
	virtual ~FCollisionHashBase() {};
	virtual void Tick()=0;
	virtual void AddActor( AActor *Actor )=0;
	virtual void RemoveActor( AActor *Actor )=0;
	virtual FCheckResult* ActorLineCheck( FMemStack& Mem, const FVector& End,const FVector& Start, const FVector& Extent, DWORD TraceFlags, DWORD ExtraNodeFlags, AActor *SourceActor )=0;
	virtual FCheckResult* ActorPointCheck( FMemStack& Mem, const FVector& Location, const FVector& Extent, DWORD TraceFlags, DWORD ExtraNodeFlags, UBOOL bSingleResult=0 )=0;
	virtual FCheckResult* ActorRadiusCheck( FMemStack& Mem, const FVector& Location, FLOAT Radius, DWORD ExtraNodeFlags )=0;
	virtual FCheckResult* ActorEncroachmentCheck( FMemStack& Mem, AActor* Actor, FVector Location, FRotator Rotation, DWORD TraceFlags, DWORD ExtraNodeFlags )=0;
	virtual FCheckResult* ActorOverlapCheck( FMemStack& Mem, AActor* Actor, FBox* Box, UBOOL bBlockKarmaOnly)=0;
	virtual void CheckActorNotReferenced( AActor* Actor )=0;
	virtual void CheckIsEmpty( )=0;
	virtual void CheckActorLocations( ULevel *level )=0;
};

ENGINE_API FCollisionHashBase* GNewCollisionHash();

/*-----------------------------------------------------------------------------
	ULevel base.
-----------------------------------------------------------------------------*/

//
// A game level.
//
class ENGINE_API ULevelBase : public UObject, public FNetworkNotify
{
	DECLARE_ABSTRACT_CLASS(ULevelBase,UObject,0,Engine)

	// Database.
	TTransArray<AActor*> Actors;

	// Variables.
	class UNetDriver*	NetDriver;
	class UEngine*		Engine;
	FURL				URL;
	class UDemoRecDriver*	DemoRecDriver;

	// Constructors.
	ULevelBase( UEngine* InOwner, const FURL& InURL=FURL(NULL) );

	// UObject interface.
	void Serialize( FArchive& Ar );
	void Destroy();

	// FNetworkNotify interface.
	void NotifyProgress( const TCHAR* Str1, const TCHAR* Str2, FLOAT Seconds );
	void NotifyStandbyHack(UBOOL bStandby) {}

protected:
	ULevelBase()
	: Actors( this )
	{}
};

/*-----------------------------------------------------------------------------
	ULevel class.
-----------------------------------------------------------------------------*/

//
// Trace options.
//
enum ETraceFlags
{
	// Bitflags.
	TRACE_Pawns				= 0x00000001, // Check collision with pawns.
	TRACE_Movers			= 0x00000002, // Check collision with movers.
	TRACE_Level				= 0x00000004, // Check collision with BSP level geometry.
	TRACE_Volumes			= 0x00000008, // Check collision with soft volume boundaries.
	TRACE_Others			= 0x00000010, // Check collision with all other kinds of actors.
	TRACE_OnlyProjActor		= 0x00000020, // Check collision with other actors only if they are projectile targets
	TRACE_Blocking			= 0x00000040, // Check collision with other actors only if they block the check actor
	TRACE_LevelGeometry		= 0x00000080, // Check collision with other actors which are static level geometry
	TRACE_ShadowCast		= 0x00000100, // Check collision with shadow casting actors
	TRACE_StopAtFirstHit	= 0x00000200, // Stop when find any collision (for visibility checks)
	TRACE_SingleResult		= 0x00000400, // Stop when find guaranteed first nearest collision (for SingleLineCheck)
	TRACE_Debug				= 0x00000800, // used for debugging specific traces
	TRACE_Material			= 0x00001000, // Request that Hit.Material return the material the trace hit.
	TRACE_Corona			= 0x00002000, // Special trace flag when checking coronas. Lime not included,
	TRACE_Projectors		= 0x00004000, // Check collision with projectors
	TRACE_AcceptProjectors	= 0x00008000, // Check collision with Actors with bAcceptsProjectors == true
	
	TRACE_BulletTrace		= 0x00010000, // If hit extended cylinder, play bullet whip
	TRACE_IgnoreExtent		= 0x00020000, // Ignore bBlock*Extent flags
	TRACE_Camera			= 0x00040000, // Trace bBlockCamera only
	TRACE_Melee				= 0x00080000, // Trace against melee cylinder
	TRACE_Weapon			= 0x00100000, // Trace for any weapon (will use anim defined cylinder)

	// Combinations.
	TRACE_Actors		= TRACE_Pawns | TRACE_Movers | TRACE_Others | TRACE_LevelGeometry,
	TRACE_AllColliding  = TRACE_Level | TRACE_Actors | TRACE_Volumes,
	TRACE_ProjTargets	= TRACE_OnlyProjActor | TRACE_AllColliding,
	TRACE_AllBlocking	= TRACE_Blocking | TRACE_AllColliding,
	TRACE_World			= TRACE_Level | TRACE_Movers | TRACE_LevelGeometry,
	TRACE_Hash			= TRACE_Pawns | TRACE_Movers | TRACE_Volumes | TRACE_Others | TRACE_LevelGeometry,
};

//
// Level updating.
//
#if _MSC_VER
enum ELevelTick
{
	LEVELTICK_TimeOnly		= 0,	// Update the level time only.
	LEVELTICK_ViewportsOnly	= 1,	// Update time and viewports.
	LEVELTICK_All			= 2,	// Update all.
};
#endif

//
//	FLeafRenderInfo
//

struct ENGINE_API FLeafRenderInfo
{
	TArray<AActor*>	RenderActors;
};

// scion sz - static mesh batching not used on xbox
#ifndef _XBOX

//
//	FStaticMeshBatchVertex
//

struct ENGINE_API FStaticMeshBatchVertex
{
	FVector	Position;
	FColor	Lighting;
	FLOAT	UVs[0];
};

//
//	FStaticMeshBatchNormalVertex
//

struct ENGINE_API FStaticMeshBatchNormalVertex
{
	FVector	Position,
			Normal;
	FColor	Lighting;
	FLOAT	UVs[0];
};

//
//	FStaticMeshBatchVertexStream
//

class ENGINE_API FStaticMeshBatchVertexStream : public FVertexStream
{
public:

	class FStaticMeshBatch*	Batch;
	INT						Size,
							NumUVs,
							Stride;
	UBOOL					Normal;

	// Constructor.

	FStaticMeshBatchVertexStream(UMaterial* Material);

	// FRenderResource interface.

	INT GetRevision() { return 1; }

	// FVertexStream interface.

	virtual INT GetSize() { return Size; }
	virtual INT GetStride() { return Stride; }
	virtual INT GetComponents(FVertexComponent* Components);
	virtual void GetStreamData(void* Dest);
	virtual void GetRawStreamData(void** Dest,INT FirstVertex) {}
};

//
//	FStaticMeshBatchIndexBuffer
//

class ENGINE_API FStaticMeshBatchIndexBuffer : public FIndexBuffer
{
public:

	class FStaticMeshBatch*	Batch;
	INT						Size;

	// Constructor.

	FStaticMeshBatchIndexBuffer();

	// FRenderResource interface.

	INT GetRevision() { return 1; }

	// FIndexBuffer interface.

	virtual INT GetSize() { return Size; }
	virtual INT GetIndexSize() { return sizeof(_WORD); }
	virtual void GetContents(void* Dest);
};

//
//	FStaticMeshBatch
//

class ENGINE_API FStaticMeshBatch
{
public:

	struct FBatchElement
	{
		AActor*	Actor;
		INT		SectionIndex,
				FirstIndex,
				NumPrimitives;
		_WORD	MinVertexIndex,
				MaxVertexIndex;
	};

	UMaterial*						Material;
	UBOOL							Sorted;
	TArray<FBatchElement>			Elements;
	FStaticMeshBatchVertexStream	Vertices;
	FStaticMeshBatchIndexBuffer		Indices;

	// Constructor.

	FStaticMeshBatch(UMaterial* InMaterial,UBOOL DisableSorting);

	// AddElement

	INT AddElement(AActor* Actor,INT SectionIndex);
};

#endif //!_XBOX

//
//	FZoneRenderInfo
//

struct ENGINE_API FZoneRenderInfo
{
	TArray<AActor*>	AntiPortals;
};

// scion sz -
// flags to skip certain checks in spawn actor
enum ENGINE_API EIgnoreSpawnActorFlags
{
	IGNORE_SPAWN_None			= 0,
	IGNORE_SPAWN_Collision		= 0x1 << 0,
	IGNORE_SPAWN_DetailLevel	= 0x1 << 1
};

//
// The level object.  Contains the level's actor list, Bsp information, and brush list.
//
class ENGINE_API ULevel : public ULevelBase
{
	DECLARE_CLASS(ULevel,ULevelBase,0,Engine)
	NO_DEFAULT_CONSTRUCTOR(ULevel)

	// Number of blocks of descriptive text to allocate with levels.
	enum {NUM_LEVEL_TEXT_BLOCKS=16};

	// Main variables, always valid.
	UModel*					Model;
	UTextBuffer*			TextBlocks[NUM_LEVEL_TEXT_BLOCKS];
	DOUBLE                  TimeSeconds;
	TMap<FString,FString>	TravelInfo;

	// Only valid in memory.
	FCollisionHashBase* Hash;
	AActor* FirstDeleted;
	struct FActorLink* NewlySpawned;
	UBOOL InTick, Ticked, FinishedPrecaching;
	INT iFirstDynamicActor, iFirstNetRelevantActor, NetTag;
#ifndef _XBOX//jg -- We don't use this on the xbox
	BYTE ZoneDist[64][64];
	// scion sz - this either
	TArray<FStaticMeshBatch>		StaticMeshBatches;	// Only valid in game.
#endif

	TArray<FLeafRenderInfo>			LeafRenderInfo;		// Only valid in game.
	TArray<FZoneRenderInfo>			ZoneRenderInfo;		// Only valid in game.
	TArray<FProjectorRenderInfo*>	DynamicProjectors;
	TArray<AActor*>					AntiPortals;		// Only valid in game, for placed antiportals
	UBOOL							BuiltRenderData;

	UBOOL bShowLineChecks;
	UBOOL bShowExtentLineChecks;
	UBOOL bShowPointChecks;

	// Temporary stats.
//	INT NetTickCycles, ActorTickCycles, AudioTickCycles, FindPathCycles;
//	INT	MoveCycles, NumMoves, NumReps, NumPV, NumRPC, SeePlayer, CanvasCycles;
//	INT Spawning, ParticleTickTime, Unused;

#ifdef WITH_KARMA
    MdtWorldID      KWorld; /* Dyanmics */
    MstBridgeID     KBridge; /* Glue (callbacks etc.) */
	MeAssetFactory*	KAssetFactory; /* Used for instancing ragdolls etc. */

    McdModelID      KLevelModel; /* Level collision model (usually tri-list) */

	TArray<AActor*> Ragdolls; // Currently active ragdolls. Oldest first.
	TArray<KarmaTriListData*> TriListPool; // 'Spare' tri-lists to be used by ragdolls etc.

	// This is the list that is iterated over when generating contacts.
	// Things in this list should have Physics of PHYS_Karma or PHYS_KarmaRagDoll, and bBlockKarma == true.
	// This list will still contain actors even if their geometry is NULL though.
	TArray<AActor*> KContactGenActors;

	TMap<QWORD, McdModelPairID> OverlapPairs;

	// Contains pairs of models that have no collision between them.
	// This does NOT include intra-skeletal disabled models. Those are held in USkeletalMeshInstance KSkelDisableTable.
	TMap<QWORD, UBOOL>			KDisableTable;
#endif
	
	// Constructor.
	ULevel( UEngine* InEngine, UBOOL RootOutside );

	// UObject interface.
	void Serialize( FArchive& Ar );
	void Destroy();
	void PostLoad();

	// ULevel interface.
	//epic moelfke: removed virtual from all the functions since ULevel has no subclasses
	void Modify( UBOOL DoTransArrays=0 );
	void SetActorCollision( UBOOL bCollision, UBOOL bFastClear = 0 );
	void Tick( ELevelTick TickType, FLOAT DeltaSeconds );
	void TickNetClient( FLOAT DeltaSeconds );
	void TickNetServer( FLOAT DeltaSeconds );
	INT ServerTickClients( FLOAT DeltaSeconds );
	void ReconcileActors();
	void RememberActors();
	UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar=*GLog );
	void ShrinkLevel();
	void CompactActors();
	UBOOL Listen( FString& Error );
	UBOOL IsServer();
	//scion jg -- Changed to pass by ref instead of value
	UBOOL MoveActor( AActor *Actor,const FVector& Delta,const FRotator& NewRotation, FCheckResult &Hit, UBOOL Test=0, UBOOL IgnorePawns=0, UBOOL bIgnoreBases=0, UBOOL bNoFail=0 );
	//scion jg -- Changed to pass by ref instead of value
	UBOOL FarMoveActor( AActor* Actor,const FVector& DestLocation, UBOOL Test=0, UBOOL bNoCheck=0, UBOOL bAttachedMove=0 );
	UBOOL EditorDestroyActor( AActor* Actor );
	// scion sz - added bForceStatic,bForceNoDelete to force removal of actors with bStatic,bNoDelete=true
	UBOOL DestroyActor( AActor* Actor, UBOOL bNetForce=0, UBOOL bForceStatic=0, UBOOL bForceNoDelete=0 );
	void CleanupDestroyed( UBOOL bForce );
	AActor* SpawnActor( UClass* Class, FName InName=NAME_None, FVector Location=FVector(0,0,0), FRotator Rotation=FRotator(0,0,0), AActor* Template=NULL, DWORD IgnoreFlags=0, UBOOL bRemoteOwned=0, AActor* Owner=NULL, APawn* Instigator=NULL );
	ABrush*	SpawnBrush();
	void SpawnViewActor( UViewport* Viewport );
	APlayerController* SpawnPlayActor( UPlayer* Viewport, ENetRole RemoteRole, const FURL& URL, FString& Error );
	// scion sz - for spawning controllers in chase camera viewports
	APlayerController* SpawnChaseCameraActor( AActor* TargetActor, UViewport* Viewport, ENetRole RemoteRole );
	// scion sz - for spawning the controller for the overlay viewport (splitscreen)
	APlayerController* SpawnOverlayCameraActor( UViewport* Viewport, ENetRole RemoteRole );
	UBOOL FindSpot( FVector Extent, FVector& Location );
	UBOOL CheckSlice( FVector& Location, FVector Extent, INT& bKeepTrying );
	UBOOL CheckEncroachment( AActor* Actor, FVector TestLocation, FRotator TestRotation, UBOOL bTouchNotify );
	UBOOL SinglePointCheck( FCheckResult& Hit, const FVector& Location, const FVector& Extent, DWORD ExtraNodeFlags, ALevelInfo* Level, UBOOL bActors );
	UBOOL EncroachingWorldGeometry( FCheckResult& Hit, const FVector& Location, const FVector& Extent, DWORD ExtraNodeFlags, ALevelInfo* Level);
	UBOOL SingleLineCheck( FCheckResult& Hit, AActor* SourceActor, const FVector& End, const FVector& Start, DWORD TraceFlags, FVector Extent=FVector(0,0,0) );
	FCheckResult* MultiPointCheck( FMemStack& Mem, const FVector& Location, const FVector& Extent, DWORD ExtraNodeFlags, ALevelInfo* Level, UBOOL bActors, UBOOL bOnlyWorldGeometry=0, UBOOL bSingleResult=0 );
	FCheckResult* MultiLineCheck( FMemStack& Mem, const FVector& End, const FVector& Start, const FVector& Size, ALevelInfo* LevelInfo, DWORD TraceFlags, AActor* SourceActor );
	void DetailChange( EDetailMode NewDetailMode );
	void ForceDetailLevel( EDetailMode NewDetailMode );
	INT TickDemoRecord( FLOAT DeltaSeconds );
	INT TickDemoPlayback( FLOAT DeltaSeconds );
	void UpdateTime( ALevelInfo* Info );
	UBOOL IsPaused();
	// !! PSX2 has a problem with the TCHAR typedef.
	#if UNICODE
	void WelcomePlayer( UNetConnection* Connection, TCHAR* Optional=TEXT("") );
	#else
	void WelcomePlayer( UNetConnection* Connection, char* Optional = "" );
	#endif
	void BuildRenderData();

	// FNetworkNotify interface.
	EAcceptConnection NotifyAcceptingConnection();
	void NotifyAcceptedConnection( class UNetConnection* Connection );
	UBOOL NotifyAcceptingChannel( class UChannel* Channel );
	ULevel* NotifyGetLevel() {return this;}
	void NotifyReceivedText( UNetConnection* Connection, const TCHAR* Text );
	void NotifyReceivedFile( UNetConnection* Connection, INT PackageIndex, const TCHAR* Error, UBOOL Skipped );
	UBOOL NotifySendingFile( UNetConnection* Connection, FGuid GUID );
	void NotifyStandbyHack(UBOOL bStandby);

	// Accessors.
	ABrush* Brush()
	{
		guardSlow(ULevel::Brush);
		check(Actors.Num()>=2);
		check(Actors(1)!=NULL);
		check(Actors(1)->Brush!=NULL);
		return (ABrush*)Actors(1);
		unguardSlow;
	}
	INT GetActorIndex( AActor* Actor )
	{
		guard(ULevel::GetActorIndex);
		for( INT i=0; i<Actors.Num(); i++ )
			if( Actors(i) == Actor )
				return i;
		appErrorf( TEXT("Actor not found: %s"), Actor->GetFullName() );
		return INDEX_NONE;
		unguard;
	}
	ALevelInfo* GetLevelInfo()
	{
		guardSlow(ULevel::GetLevelInfo);
		checkSlow(Actors(0));
		checkSlow(Actors(0)->IsA(ALevelInfo::StaticClass()));
		return (ALevelInfo*)Actors(0);
		unguardSlow;
	}
	AZoneInfo* GetZoneActor( INT iZone )
	{
		guardSlow(ULevel::GetZoneActor);
		return Model->Zones[iZone].ZoneActor ? Model->Zones[iZone].ZoneActor : GetLevelInfo();
		unguardSlow;
	}
	void UpdateTerrainArrays();
	UBOOL ToFloor( AActor* InActor, UBOOL InAlign, AActor* InIgnoreActor );
};

/*-----------------------------------------------------------------------------
	Iterators.
-----------------------------------------------------------------------------*/

//
// Iterate through all static brushes in a level.
//
class FStaticBrushIterator
{
public:
	// Public constructor.
	FStaticBrushIterator( ULevel *InLevel )
	:	Level   ( InLevel   )
	,	Index   ( -1        )
	{
		checkSlow(Level!=NULL);
		++*this;
	}
	void operator++()
	{
		do
		{
			if( ++Index >= Level->Actors.Num() )
			{
				Level = NULL;
				break;
			}
		} while
		(	!Level->Actors(Index)
		||	!Level->Actors(Index)->IsStaticBrush() );
	}
	ABrush* operator* ()
	{
		checkSlow(Level);
		checkSlow(Index<Level->Actors.Num());
		checkSlow(Level->Actors(Index));
		checkSlow(Level->Actors(Index)->IsStaticBrush());
		return (ABrush*)Level->Actors(Index);
	}
	ABrush* operator-> ()
	{
		checkSlow(Level);
		checkSlow(Index<Level->Actors.Num());
		checkSlow(Level->Actors(Index));
		checkSlow(Level->Actors(Index)->IsStaticBrush());
		return (ABrush*)Level->Actors(Index);
	}
	operator UBOOL()
	{
		return Level != NULL;
	}
protected:
	ULevel*		Level;
	INT		    Index;
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

