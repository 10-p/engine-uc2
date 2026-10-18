/*=============================================================================
	UnEngine.h: Unreal engine definition.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/
#if SUPPORTS_PRAGMA_PACK
#pragma pack (push,4)	// script mirrors are laid out with /Zp4 (as the generated headers); 64-bit pointers sit on 4
#endif

/*-----------------------------------------------------------------------------
	Unreal engine.
-----------------------------------------------------------------------------*/

class ENGINE_API UEngine : public USubsystem
{
	DECLARE_ABSTRACT_CLASS(UEngine,USubsystem,CLASS_Config|CLASS_Transient,Engine)

	// Subsystems.
	UClass*					AudioDeviceClass;
	UClass*					ConsoleClass;
	UClass*					DefaultMenuClass;
	UClass*					DefaultPlayerMenuClass;
	UClass*					NetworkDriverClass;
	UClass*					LanguageClass;

	// Variables.
	class UPrimitive*		Cylinder;
	class UClient*			Client;
	class UAudioSubsystem*	Audio;
	class URenderDevice*	GRenDev;
	class UVoiceSubsystem*	Voice;

	// Stats.
	UBOOL					bShowFrameRate;
	UBOOL					bShowRenderStats;
	UBOOL					bShowHardwareStats;
	UBOOL					bShowGameStats;
	UBOOL					bShowCanvasStats;
	UBOOL					bShowNetStats;
	UBOOL					bShowAnimStats;		 // Show animation statistics.
	UBOOL					bShowSlowestStats;	 // scion sz - show all stats that ran slower than threshold
	FLOAT					MinStatMSec;		 // scion sz - threshold to use for bShowSlowestStats
	
	// ifdef WITH_LIPSinc not here since this needs to be in Engine.uc as well.
	UBOOL					bShowLIPSincStats;   // Show LIPSinc statistics.
	// endif

	UBOOL					bShowHistograph;
	UBOOL					bShowXboxMemStats;
	UBOOL					bShowMatineeStats;	// Show Matinee specific information
	UBOOL					bShowAudioStats;
	UBOOL					bShowLightStats;    // Show dynamic lighting statistics.
	UBOOL					bShowStatsSummary;

	INT						TickCycles GCC_PACK(4);
	INT						GameCycles, ClientCycles;	
	FLOAT					CacheSizeMegs;		// scion sz - changed this to a float

	BITFIELD				UseSound:1 GCC_PACK(4);
	BITFIELD				UseStaticMeshBatching:1;
	//scion jg -- Displays meshes that have bad/slow collision options on them
	BITFIELD				bShowSlowCollisionMeshes:1;
	//scion rwd - toggle server object ref checking on cleanupdestroyed
	BITFIELD				bServerRefChecking:1;
	FLOAT					CurrentTickRate GCC_PACK(4);

	INT						ActiveControllerId;

	//scion rwd - static engine instance ptr, allow objects to reach engine
	static 	UEngine*		GEngine;

	// Color preferences.
	FColor
		C_WorldBox,
		C_GroundPlane,
		C_GroundHighlight,
		C_BrushWire,
		C_Pivot,
		C_Select,
		C_Current,
		C_AddWire,
		C_SubtractWire,
		C_GreyWire,
		C_BrushVertex,
		C_BrushSnap,
		C_Invalid,
		C_ActorWire,
		C_ActorHiWire,
		C_Black,
		C_White,
		C_Mask,
		C_SemiSolidWire,
		C_NonSolidWire,
		C_WireBackground,
		C_WireGridAxis,
		C_ActorArrow,
		C_ScaleBox,
		C_ScaleBoxHi,
		C_ZoneWire,
		C_Mover,
		C_OrthoBackground,
		C_StaticMesh,
		C_Volume,
		C_ConstraintLine,
		C_AnimMesh,
		C_TerrainWire;

	// Constructors.
	UEngine();
	void StaticConstructor();

	// UObject interface.
	void Serialize( FArchive& Ar );
	void Destroy();

	// UEngine interface.
	virtual void Init();
	virtual UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Out=*GLog );
	virtual void Flush( UBOOL AllowPrecache );
	virtual void UpdateGamma();
	virtual void RestoreGamma();
	virtual UBOOL Key( UViewport* Viewport, EInputKey Key, TCHAR Unicode );
	virtual UBOOL InputEvent( UViewport* Viewport, EInputKey iKey, EInputAction State, FLOAT Delta=0.f);
	virtual void Tick( FLOAT DeltaSeconds )=0;
	virtual void Draw( UViewport* Viewport, FLOAT DeltaSeconds, UBOOL Blit=1, BYTE* HitData=NULL, INT* HitSize=NULL )=0;
	virtual void MouseDelta( UViewport* Viewport, DWORD Buttons, FLOAT DX, FLOAT DY )=0;
	virtual void MousePosition( UViewport* Viewport, DWORD Buttons, FLOAT X, FLOAT Y )=0;
	virtual void MouseWheel( UViewport* Viewport, DWORD Buttons, INT Delta )=0;
	virtual void Click( UViewport* Viewport, DWORD Buttons, FLOAT X, FLOAT Y )=0;
	virtual void UnClick( UViewport* Viewport, DWORD Buttons, INT MouseX, INT MouseY )=0;
	virtual void SetClientTravel( UPlayer* Viewport, const TCHAR* NextURL, UBOOL bItems, ETravelType TravelType )=0;
	virtual INT ChallengeResponse( INT Challenge );
	virtual FLOAT GetMaxTickRate();
	virtual void SetProgress( const TCHAR* Str1, const TCHAR* Str2, FLOAT Seconds );
	void InitAudio();

	virtual void edSetClickLocation( FVector& InLocation ) {};
	virtual void edDrawAxisIndicator( class FSceneNode* SceneNode ) {};
	virtual int edcamMode( UViewport* Viewport ) {return 0;}
	virtual int edcamTerrainBrush() {return 0;}
	virtual int edcamMouseControl( UViewport* InViewport ) {return 0;}
	virtual void EdCallback( DWORD Code, UBOOL Send, DWORD lParam ) {};
};

/*-----------------------------------------------------------------------------
	UServerCommandlet.
-----------------------------------------------------------------------------*/

class UServerCommandlet : public UCommandlet
{
	DECLARE_CLASS(UServerCommandlet,UCommandlet,CLASS_Transient,Engine);
	void StaticConstructor();
	INT Main( const TCHAR* Parms );
};

/*-----------------------------------------------------------------------------
	UGlobalTempObjects.
-----------------------------------------------------------------------------*/

class UGlobalTempObjects : public UObject
{
	DECLARE_CLASS(UGlobalTempObjects,UObject,CLASS_Transient,Engine);
	TArray<UObject**> GlobalObjectPtrs;

	// Constructor
	UGlobalTempObjects()
	{
		AddToRoot();
	}

	// UGlobalTempObjects interface
	void AddGlobalObject( UObject** InObjectPtr )
	{
		guard(UGlobalTempObjects::AddGlobalObject);
		GlobalObjectPtrs.AddItem(InObjectPtr);		
		unguard;
	}
	
	// UObject interface
	void Serialize( FArchive& Ar )
	{
		guard(UGlobalTempObjects::Serialize);
		Super::Serialize(Ar);
		// scion sz - only serialize this during
		// the process mark pass that happens during GC
		if( GIsProcessMarkPass &&
			!(Ar.IsLoading() || Ar.IsSaving()) )
		{
			// scion sz - allow temp objects to actually be deleted
			for( INT i=0;i<GlobalObjectPtrs.Num();i++ )
			{
				if( (*GlobalObjectPtrs(i)) )
					(*GlobalObjectPtrs(i))->ClearFlags( RF_TagPersist );
			}
			EmptyAll();
		}
		unguard;
	}
	void Destroy()
	{
		guard(UGlobalTempObjects::Destroy);
		EmptyAll();
		Super::Destroy();		
		unguard;
	}

	void EmptyAll()
	{
		for( INT i=0;i<GlobalObjectPtrs.Num();i++ )
			*(GlobalObjectPtrs(i)) = NULL;
		GlobalObjectPtrs.Empty();
	}

	// scion sz - 
	// remove entries for objects matching certain flags
	void RemoveEntries( DWORD RemoveFlags, DWORD KeepFlags )
	{
		guard(UGlobalTempObjects::RemoveObjects);

		TArray<UObject**> ValidObjectPtrs;

		for( INT i=0; i < GlobalObjectPtrs.Num(); i++ )
		{
			UObject* Obj = *(GlobalObjectPtrs(i));
			if( Obj )
			{
				// check if the object needs to be removed
				if( (Obj->GetFlags()&RemoveFlags) &&
					!(Obj->GetFlags()&KeepFlags) )
				{
					// set the obj ptr to NULL
					*(GlobalObjectPtrs(i)) = NULL;
				}
				else
				{
					// keep this obj ptr. put it in another array 
                    ValidObjectPtrs.AddItem( GlobalObjectPtrs(i) );
				}
			}
		}
        // copy over the old list of obj ptrs with the new clean list
		GlobalObjectPtrs = ValidObjectPtrs;        

		unguard;
	}
};


/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

#if SUPPORTS_PRAGMA_PACK
#pragma pack (pop)
#endif
