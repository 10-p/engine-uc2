/*=============================================================================
	UnRender.h: Rendering functions and structures
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#ifndef _UNRENDER_H_
#define _UNRENDER_H_

//
//  Defines.
//
#define ORTHO_LOW_DETAIL 40000.0f
#define DEPTH_COMPLEXITY_BITS(Viewport)	((Viewport)->IsDepthComplexity() ? 4 : 0)
#define DEPTH_COMPLEXITY_MASK(Viewport)	((Viewport)->IsDepthComplexity() ? 0x0f : 0)

#define MAX_RECURSION_DEPTH		4

//
//	FSceneMemStack
//

class FSceneMemStack : public FMemStack
{
public:

	FSceneMemStack();
	virtual ~FSceneMemStack();
};

// A memory stack used for temporary rendering allocations.
extern FSceneMemStack	GSceneMem;	
// scion sz -
// this stack is just for lighting information since we want
// to persist it while rendering the scene
// Actually, just new'ing from heap instead
//extern FSceneMemStack	GSceneActorLightingMem;

//
//  FSceneNode
//
class ENGINE_API FSceneNode
{
public:
	
	// scion sz - 
	enum TFilterMode
	{
		Filter_All,			// renders any actor
		Filter_Solid,		// renders only solid actors
		Filter_Emissive,	// renders only emissive actors
		Filter_Translucent,	// renders only translucent actors
		Filter_PostProcess	// renders only postprocess actors
	};
	
	TFilterMode		FilterMode;

	UViewport*		Viewport;
	FRenderTarget*	RenderTarget;
	FSceneNode*		Parent;
	INT				Recursion;

	FMatrix			WorldToCamera,
					CameraToWorld,
					CameraToScreen,
					ScreenToCamera,
					WorldToScreen,
					ScreenToWorld;
	FVector			ViewOrigin,
					CameraX,
					CameraY;

	FLOAT			Determinant;		// WorldToScreen.Determinant();

	// Constructor/destructor
	FSceneNode(UViewport* InViewport,FRenderTarget* InRenderTarget);
	FSceneNode(FSceneNode* InParent);
	virtual ~FSceneNode();

	// Project - Projects a point from the scene node's coordinate system into screen space.
	FPlane Project(FVector V);

	// Deproject - Deprojects a point from screen space into the scene node's coordinate system.
	FVector Deproject(FPlane P);

	// GetLodSceneNode - Determines the scenenode to use for determining LOD.

	virtual FSceneNode* GetLodSceneNode() { return this; }

	// Render
	virtual void Render(FRenderInterface* RI) = 0;

	// Subinterfaces.
	virtual class FLevelSceneNode* GetLevelSceneNode() { return NULL; }
	virtual class FCameraSceneNode* GetCameraSceneNode() { return NULL; }
	virtual class FActorSceneNode* GetActorSceneNode() { return NULL; }
	virtual class FSkySceneNode* GetSkySceneNode() { return NULL; }
	virtual class FMirrorSceneNode* GetMirrorSceneNode() { return NULL; }

	//jg -- Only level scene nodes should use occlusion queries
	virtual UBOOL CanUseHardwareOcclusion(void) { return 0; }
	//jg -- Quick accessors for determining whether this node is a child/parent
	FORCEINLINE UBOOL IsParentNode(void) { return Parent == NULL; }
	FORCEINLINE UBOOL IsChildNode(void) { return Parent != NULL; }
};

//
//  FDynamicLight
//
class ENGINE_API FDynamicLight
{
public:

	AActor*				Actor;				// may be NULL
	INT					Revision;

	FPlane				Color;
	FVector				Position,
						Direction;
	FLOAT				Radius,
						LightCone;			// scion sz - for spot lights

	UBOOL				Dynamic,
						Changed;

	INT					SortKey;			// temp - only relevant to actor being rendered

    FLOAT               Alpha;

	// scion sz -
	// overrideable light type (defaults to Actor->LightEffect)
	BYTE				LightEffect;

	// scion sz -
	// keep track of lights marked as already used 
	// so that we don't have to do a slow search for duplicates
	static TArray<FDynamicLight*> MarkedUsed;
	UBOOL bAlreadyUsed;

	// Constructor
	FDynamicLight(AActor* InActor = NULL);

	// virtual destructor needed
	virtual ~FDynamicLight(){}

	// Update - Calculates render data for the light.
	void Update();

	// SampleIntensity
	FLOAT SampleIntensity(FVector SamplePosition,FVector SampleNormal);

	// SampleLight
	FColor SampleLight(FVector SamplePosition,FVector SampleNormal);

	// used by FVolatileDynamicLight
	virtual void AddActorRef( class FDynamicActor* pDynActor ){};
	virtual void RemoveActorRef( class FDynamicActor* pDynActor ){};

	/* scion ======================================================================
	* ::MarkUsed
	* Author: sz
	* 
	* Mark a light as in-use so that we don't have any duplicates in the consider
	* list. See RenderActor.
	* =============================================================================
	*/
	FORCEINLINE void MarkUsed()
	{
		bAlreadyUsed=1;
		MarkedUsed.AddItem(this);
	}
	FORCEINLINE UBOOL IsUsed()
	{
		return bAlreadyUsed;
	}

	/* scion ======================================================================
	* ::MarkUsed
	* Author: sz
	* 
	* Clear any flags for lights that were marked as already considered
	* =============================================================================
	*/
	static FORCEINLINE void UnMarkAllUsed()
	{
		for( INT Idx=0; Idx<MarkedUsed.Num(); Idx++ )
		{
			MarkedUsed(Idx)->bAlreadyUsed=0;
		}
		MarkedUsed.Reset();
	}

	/* scion ======================================================================
	* ::IsSunLight
	* Author: sz
	* 
	* return true if DynamicLight is a sun light
	* =============================================================================
	*/
	FORCEINLINE UBOOL IsSunLight()
	{
		return( LightEffect == LE_Sunlight );
	}

	/* scion ======================================================================
	* ::IsSpotLight
	* Author: sz
	* 
	* return true if DynamicLight is a spot light
	* =============================================================================
	*/
	FORCEINLINE UBOOL IsSpotLight()
	{
		return( LightEffect == LE_Spotlight || LightEffect == LE_StaticSpot );
	}

	/* scion ======================================================================
	* ::IsPointLight
	* Author: sz
	* 
	* return true if DynamicLight is a point light
	* =============================================================================
	*/
	FORCEINLINE UBOOL IsPointLight()
	{
		return( !IsSunLight() && !IsSpotLight() );
	}

	/* scion ======================================================================
	* ::IsDirectionalLight
	* Author: sz
	* 
	* return true if DynamicLight is a directional (!sun) light
	* =============================================================================
	*/
	FORCEINLINE UBOOL IsDirectionalLight()
	{
		return( LightEffect == LE_Directional );
	}

	/* scion ======================================================================
	* ::SampleLightAtten
	* Author: sz
	* 
	* samples light at reference point and returns the sampled color
	*
	* Inputs:
	*	LightA				= input point light
	*	RealBoundingSphere	= reference point to be lit
	*
	* =============================================================================
	*/
	FORCEINLINE FLOAT SampleLightAtten( FSphere& RealBoundingSphere )
	{
		guardSlow(FDynamicLight::SampleLightAtten);

		// dist sqrd to lightA
		FLOAT DistSqA = (Position - RealBoundingSphere).SizeSquared();
		// radius sqrd for lightA
		FLOAT RadiusSqA = Radius*Radius;

		if( DistSqA > RadiusSqA )
			return 0.f;			

		// get LightA's intensity at the sample point
		// see ::UnrealAttenuation
		FLOAT	SqA = DistSqA / RadiusSqA;
		FLOAT	A = appSqrt( SqA );						//A = Distance / Radius;
		FLOAT	AttenA = 2 * SqA * A - 3 * SqA + 1;		//B = (2 * A * A * A - 3 * A * A + 1);

		// scale light color by dist attenuation amount
		return( AttenA );

		unguardSlow;
	}

};

class FDynamicActor;

/* ============================================================================
 * Author: jg
 * ============================================================================
 *
 * Base class for hardware occlusion queries. It allows the FDynamicActor
 * to communicate with the query without knowing anything other than this one
 * method
 *
 * ============================================================================
 */
class FBaseOcclusionQuery
{
protected:
	// The dynamic actor this query relates to
	FDynamicActor* pDynActor;

public:
	/* ============================================================================
	 * FBaseOcclusionQuery::ClearDynamicActor
	 * Author: jg
	 * 
	 * Breaks the link between the query and the dynamic actor
	 *
	 * ============================================================================
	 */
	FORCEINLINE void ClearDynamicActor(void)
	{
		pDynActor = NULL;
	}

	/* ============================================================================
	 * FBaseOcclusionQuery::GetDynamicActor
	 * Author: jg
	 * 
	 * Returns the dynamic actor. This can be null if the actor died before the
	 * query finished.
	 *
	 * ============================================================================
	 */
	FORCEINLINE FDynamicActor* GetDynamicActor(void)
	{
		return pDynActor;
	}
};

// Expose this as a function so we don't have to include other details about
// the query manager
void ENGINE_API ClearQueriesForViewport(UViewport* pViewport);
void ENGINE_API FlushHWOcclusionQueryPool();

// scion sz 
// use pooled lists of light linked lists to
// minimize new allocations
#define FLightList TList<FDynamicLight*>
#define FLightListPool TPooledList< FLightList >
// Gets a light list from the pool
FORCEINLINE FLightList* CreateLightList(void)
{
	guard(CreateLightList);
	// Get a new particle from the pool
	FLightListPool* pPooled = FLightListPool::GetInstance(NULL);
	return &pPooled->Element;
	unguard;
}
// Returns a light list to the pool
FORCEINLINE void DestroyLightList(FLightList* LightList)
{
	guard(DestroyLightList);
	// Return the instance to the pool
	FLightListPool::ReturnInstance((FLightListPool*)LightList);
	unguard;
}
// add a new light entry to the beginning of a list
FORCEINLINE FLightList* AddLightListEntry( FDynamicLight* InEntry, FLightList* InNext=NULL )
{
	guard(AddLightListEntry);    
	FLightList* Result = CreateLightList();
	Result->Element = InEntry;
	Result->Next = InNext;
	return Result;
	unguard;
}

// scion sz 
// use pooled lists of projector linked lists to
// minimize new allocations
#define FProjectorList TList<FProjectorRenderInfo*>
#define FProjectorListPool TPooledList< FProjectorList >
// Gets a light list from the pool
FORCEINLINE FProjectorList* CreateProjectorList(void)
{
	guard(CreateProjectorList);
	// Get a new particle from the pool
	FProjectorListPool* pPooled = FProjectorListPool::GetInstance(NULL);
	return &pPooled->Element;
	unguard;
}
// Returns a Projector list to the pool
FORCEINLINE void DestroyProjectorList(FProjectorList* ProjectorList)
{
	guard(DestroyProjectorList);
	// Return the instance to the pool
	FProjectorListPool::ReturnInstance((FProjectorListPool*)ProjectorList);
	unguard;
}
// add a new Projector entry to the beginning of a list
FORCEINLINE FProjectorList* AddProjectorListEntry( FProjectorRenderInfo* InEntry, FProjectorList* InNext=NULL )
{
	guard(AddProjectorListEntry);    
	FProjectorList* Result = CreateProjectorList();
	Result->Element = InEntry;
	Result->Next = InNext;
	return Result;
	unguard;
}

//
//  FDynamicActor
//
class ENGINE_API FDynamicActor
{
public:
	AActor*	Actor;
	INT		Revision;

	FMatrix	LocalToWorld;
// scion sz - not used
//			WorldToLocal;
	FLOAT	Determinant;
	FBox	BoundingBox;
	FSphere	BoundingSphere;
	FColor	AmbientColor;
	DWORD	VisibilityTag;
    FBox	PredictedBox;
    FVector AmbientVector;

	// scion sz - distance ratio for LOD used for lighting calcs
	// 1.0=closest/highest LOD >0.0=furthest/lowest LOD
	FLOAT	DistanceLODFactor;
	// scion sz - these are needed when bUseLightingFromBase is used
	FLightList*	ActorLights;
	FProjectorList*	ActorProjectors;
	FLOAT	LastLightUpdateTime;

	// scion sz - deferred rendering flags used depending on current FSceneNode::TFilterMode state
	EPostProcessMaterialType IsPostProcess;
	BITFIELD IsTranslucent:1;
	BITFIELD IsEmissive:1;
	BITFIELD NeedsSortUpdate:1;
	// scion sz - some actors only need lighting updated once.  This flag tells us if a lighting update is needed.
	BITFIELD NeedsLightUpdate:1;

	//jg -- Hardware occlusion settings
	// The number of viewports supported. Change this to meet your number
	// 4 split screens + 1 overlay = 5 total
	#define MAX_SS_VIEWPORTS 5
	// The size of an object where it is not longer checked for occlusion
	// I picked this number out of the air so tune it for your objects
	#define MAX_BOX_SIZE (2048.f * 2048.f)
	// Whether to perform this test or not. Very large actors shouldn't be
	// tested, nor should skybox actors.
	BITFIELD bNeedsOcclusionTest:1;
	BITFIELD bCanUseDeferredOcclusionTest:1;
	// Bit mask of viewports this actor is viewable in
	BYTE bIsVisibleInViewport;
	// Bit mask of viewports that this actor needs deferred tests in
	BYTE bNeedsDeferredQueryInViewport;
	// Array of outstanding query objects which must be updated if this actor dies
	FBaseOcclusionQuery* Queries[MAX_SS_VIEWPORTS];

// Accessor methods for the hardware occlusion state
	// Places the viewport in the first available slot
	FORCEINLINE INT AddViewport(UViewport* pViewport)
	{
		INT Index = pViewport->GetViewportIndex();
		return (Index < MAX_SS_VIEWPORTS) ? Index : INDEX_NONE;
	}
	// Finds the index of the viewport. Adds it if not known
	FORCEINLINE INT GetViewportIndex(UViewport* pViewport)
	{
		INT Index = pViewport->GetViewportIndex();
		return (Index < MAX_SS_VIEWPORTS) ? Index : INDEX_NONE;
	}
	// Determines if the actor is visible in this viewport
	FORCEINLINE UBOOL IsVisible(UViewport* pViewport)
	{
		// Figure out which bit represents the viewport visibility setting
		// by using the index to determine it
		INT Index = GetViewportIndex(pViewport);
		checkSlow(Index < MAX_SS_VIEWPORTS);
		// Return whether or not it is visible
		return Index == -1 || bIsVisibleInViewport & (1 << Index);
	}
	// Determines if the actor needs a deferred query in this viewport
	FORCEINLINE UBOOL NeedsDeferredQuery(UViewport* pViewport)
	{
		// Figure out which bit represents the viewport visibility setting
		// by using the index to determine it
		INT Index = GetViewportIndex(pViewport);
		checkSlow(Index < MAX_SS_VIEWPORTS);
		// Return whether or not it is visible
		return Index == -1 || bNeedsDeferredQueryInViewport & (1 << Index);
	}
	// Marks an actor as visible or not in the specified viewport
	FORCEINLINE void SetVisible(UViewport* pViewport,UBOOL bVisible)
	{
		// Figure out which bit represents the viewport visibility setting
		// by using the index to determine it
		INT Index = GetViewportIndex(pViewport);
		if (Index >= 0 && Index < MAX_SS_VIEWPORTS)
		{
			// Enable or disable the specified bit
			bIsVisibleInViewport = ((bIsVisibleInViewport & ~(1 << Index)) |
				bVisible << Index);
		}
	}
	// Marks an actor as needing a deferred test or not
	FORCEINLINE void SetNeedsDeferredQuery(UViewport* pViewport,UBOOL bNeeds)
	{
		// Figure out which bit represents the viewport visibility setting
		// by using the index to determine it
		INT Index = GetViewportIndex(pViewport);
		if (Index >= 0 && Index < MAX_SS_VIEWPORTS)
		{
			// Enable or disable the specified bit
			bNeedsDeferredQueryInViewport =
				((bNeedsDeferredQueryInViewport & ~(1 << Index)) | bNeeds << Index);
		}
	}
	// Associates a query with a viewport
	FORCEINLINE void SetQuery(UViewport* pViewport,FBaseOcclusionQuery* pQuery)
	{
		guard(SetQuery);
		// Get the index for this viewport
		INT Index = GetViewportIndex(pViewport);
		check(Index >= 0 && Index < MAX_SS_VIEWPORTS);
		// Mark this as the query associated with this viewport
		Queries[Index] = pQuery;
		unguard;
	}
	// Whether this object should use occlusion testing or not
	FORCEINLINE UBOOL NeedsOcclusionTest(void)
	{ 
		return bNeedsOcclusionTest;
	}
	// Whether this object can use deferred occlusion testing or not
	FORCEINLINE UBOOL CanUseDeferredOcclusionTest(void)
	{ 
		return bCanUseDeferredOcclusionTest;
	}
	// Sets whether this object needs testing or not
	FORCEINLINE void SetNeedsOcclusionTest(UBOOL bIn) 
	{ 
		bNeedsOcclusionTest = bIn;
	}

	// Constructor
	FDynamicActor(AActor* InActor);

	// Clean up any outstanding query references. This is needed so any
	// outstanding queries don't try to change the status of a dead actor
	~FDynamicActor()
	{
		// Loop through our queries cleaning up any refs
		for (INT Index = 0; Index < MAX_SS_VIEWPORTS; Index++)
		{
			if (Queries[Index] != NULL)
			{
				Queries[Index]->ClearDynamicActor();
			}
		}
		EmptyLights();
		EmptyProjectors();		
	}

	// Update - Calculates render data for the actor.
	void Update();
	// scion sz - update deferred rendering flags for sorting
	void UpdateSorting();	
	// scion sz - add a new projector list entry
	// scion sz - delete light entries
	FORCEINLINE void EmptyLights()
	{
		FLightList*	pCur = ActorLights;
		FLightList*	pTemp = NULL;
		while( pCur )
		{	
			if( !GIsGarbageCollecting &&
				pCur->Element->Dynamic )
			{
				pCur->Element->RemoveActorRef(this);
			}

			pTemp = pCur;
			pCur = pCur->Next;
			//delete pTemp;
			DestroyLightList( pTemp );
		}
		ActorLights = NULL;
	}
	// scion sz - delete projector entries
	FORCEINLINE void EmptyProjectors()
	{
		FProjectorList* pCur = ActorProjectors;
		FProjectorList* pTemp = NULL;
		while( pCur )
		{
			pTemp = pCur;
			pCur = pCur->Next;
			//delete pTemp;
			DestroyProjectorList( pTemp );
		}
		ActorProjectors = NULL;
	}

	// scion sz - remove a deleted light from this dyn actor's light list
	FORCEINLINE void RemoveVolatileLight(class FVolatileDynamicLight* pDynLight );

	// Render
	void Render(FLevelSceneNode* SceneNode,TList<FDynamicLight*>* Lights,TList<FProjectorRenderInfo*>* Projectors,FRenderInterface* RI);
};

/* scion ======================================================================
* ::FVolatileDynamicLight
* Author: sz
* 
* A dynamic light that may be destroyed during gameplay.
* Keeps track of any actors currently using this light
* so that those references can be destroyed when this light
* is destroyed.
*
* =============================================================================
*/
class ENGINE_API FVolatileDynamicLight : public FDynamicLight
{
public:

	// actors that are referencing this dynamic light
	TArray<FDynamicActor*> ActorRefs;

	// constructor
	FVolatileDynamicLight( AActor* InActor = NULL ) :
		FDynamicLight(InActor)
		{
		}

	// destructor
	~FVolatileDynamicLight()
	{
		if( !GIsGarbageCollecting ) 
		{
			for( INT Idx=0; Idx < ActorRefs.Num(); Idx++ )
			{
				FDynamicActor* pDynActor = ActorRefs(Idx);
				check(pDynActor);
				pDynActor->RemoveVolatileLight(this);
			}
		}
	}

	void AddActorRef( class FDynamicActor* pDynActor )
	{
        ActorRefs.AddUniqueItem( pDynActor );
	}

	void RemoveActorRef( class FDynamicActor* pDynActor )
	{
		INT FoundIdx = ActorRefs.FindItemIndex( pDynActor );
		if( FoundIdx != INDEX_NONE )
		{
			ActorRefs.Remove( FoundIdx );
		}
	}
};

//
//  FLevelSceneNode
//
class ENGINE_API FLevelSceneNode : public FSceneNode
{
public:

	ULevel*		Level;
	UModel*		Model;
	AActor*		ViewActor;
	INT			ViewZone,
				InvisibleZone;
	DWORD		StencilMask;

	// Constructors/destructor.
	FLevelSceneNode(UViewport* InViewport,FRenderTarget* InRenderTarget);
	FLevelSceneNode(FLevelSceneNode* InParent,INT InViewZone,FMatrix LocalToParent);
	virtual ~FLevelSceneNode();

	// GetViewFrustum
	virtual FConvexVolume GetViewFrustum();

#ifdef __PSX2_EE__
	// GetOverflowFrustum - gets the guard band frustum, only needed for PS2
	virtual FConvexVolume GetOverflowFrustum();
#endif
	// FilterActor
	virtual UBOOL FilterActor(AActor* Actor, UBOOL DrawAttached=0 );
    virtual UBOOL FilterAttachment(AActor* AttachedActor);
	virtual UBOOL FilterProjector(AProjector* Actor);

	// FSceneNode interface.
	virtual void Render(FRenderInterface* RI);

	virtual FLevelSceneNode* GetLevelSceneNode() { return this; }

	//jg -- Only parent level scene nodes should use occlusion queries
	virtual UBOOL CanUseHardwareOcclusion(void) { return GIsEditor == 0 && IsParentNode(); }
};

//
//  FCameraSceneNode
//
class ENGINE_API FCameraSceneNode : public FLevelSceneNode
{
public:

	// Constructor/destructor.
	FCameraSceneNode(UViewport* InViewport,FRenderTarget* InRenderTarget,AActor* CameraActor,FVector CameraLocation,FRotator CameraRotation,FLOAT CameraFOV);
	virtual ~FCameraSceneNode();

	// FSceneNode interface.
	virtual void Render(FRenderInterface* RI);

	virtual FCameraSceneNode* GetCameraSceneNode() { return this; }

	// Allows changing parameters on the fly.
	virtual void UpdateMatrices();

	virtual UBOOL IsShadowSceneNode(){ return 0;}

	FRotator	ViewRotation;
	FLOAT		ViewFOV;
};

//
//	FPlayerSceneNode
//
class ENGINE_API FPlayerSceneNode : public FCameraSceneNode
{
public:

	// Constructor.
	FPlayerSceneNode(UViewport* InViewport,FRenderTarget* InRenderTarget,AActor* CameraActor,FVector CameraLocation,FRotator CameraRotation,FLOAT CameraFOV);

	// FSceneNode interface.
	virtual void Render(FRenderInterface* RI);
};

//
//  FActorSceneNode
//
class ENGINE_API FActorSceneNode : public FCameraSceneNode
{
public:

	AActor*	RenderActor;

	// Constructor.
	FActorSceneNode(UViewport* InViewport,FRenderTarget* InRenderTarget,AActor* InActor,AActor* CameraActor,FVector CameraLocation,FRotator CameraRotation,FLOAT CameraFOV);

	// FSceneNode interface.
	virtual void Render(FRenderInterface* RI);

	virtual FActorSceneNode* GetActorSceneNode() { return this; }
};

//
//  FSkySceneNode
//
class ENGINE_API FSkySceneNode : public FLevelSceneNode
{
public:

	// Constructor.
	FSkySceneNode(FLevelSceneNode* InParent,INT InViewZone);

	// FSceneNode interface.
	virtual FSkySceneNode* GetSkySceneNode() { return this; }
};

//
//  FMirrorSceneNode
//
class ENGINE_API FMirrorSceneNode : public FLevelSceneNode
{
public:

	INT	MirrorSurface;

	// Constructor.
	FMirrorSceneNode(FLevelSceneNode* InParent,FPlane MirrorPlane,INT InViewZone,INT InMirrorSurface);

	// FSceneNode interface.
	virtual FMirrorSceneNode* GetMirrorSceneNode() { return this; }
};

/* scion =====================================================================
* class FInteractionRenderSceneNode
* Author: sz
* ============================================================================
* Renders only the interactions (ie. the menus) as well as any camera effects
* ============================================================================
*/
class ENGINE_API FInteractionRenderSceneNode : public FCameraSceneNode
{
public:

	// Constructor
	FInteractionRenderSceneNode( UViewport* InViewport, FRenderTarget* InRenderTarget, AActor* CameraActor, FVector CameraLocation, FRotator CameraRotation, FLOAT CameraFOV );

	// FSceneNode interface
	virtual void Render( FRenderInterface* RI );	    
};

//
//  FVisibilityInterface
//
class FVisibilityInterface
{
public:

	// Destructor.
	virtual ~FVisibilityInterface() {}

	// Visible - Returns whether a bounding box or sphere is visible.
	virtual UBOOL Visible(const FBox& BoundingBox) = 0;
	virtual UBOOL Visible(const FSphere& BoundingSphere) = 0;
};


//
//	FSpriteVertexStream
//
class FSpriteVertexStream : public FVertexStream
{
public:

	//
	//	FSpriteVertex
	//
	struct FSpriteVertex
	{
		FVector	Position;
		FColor	Diffuse;
		FLOAT	U,
				V;
	};

	FSpriteVertex			Vertices[4];

	// Constructor.
	FSpriteVertexStream()
	{
	}

	// GetRevision
	virtual INT GetRevision()
	{
		return 1;
	}

	// GetSize
	virtual INT GetSize()
	{
		return 4 * sizeof(FSpriteVertex);
	}

	// GetStride
	virtual INT GetStride()
	{
		return sizeof(FSpriteVertex);
	}

	// GetComponents
	virtual INT GetComponents(FVertexComponent* OutComponents)
	{
		OutComponents[0].Type = CT_Float3;
		OutComponents[0].Function = FVF_Position;
		OutComponents[1].Type = CT_Color;
		OutComponents[1].Function = FVF_Diffuse;
		OutComponents[2].Type = CT_Float2;
		OutComponents[2].Function = FVF_TexCoord0;

		return 3;
	}

	// GetStreamData
	virtual void GetStreamData(void* Dest)
	{
		appMemcpy(Dest,&Vertices[0],4 * sizeof(FSpriteVertex));
	}

	// GetRawStreamData
	virtual void GetRawStreamData(void ** Dest, INT FirstVertex )
	{
		*Dest = &Vertices[FirstVertex];
	}
};

//
//	FLitSpriteVertexStream
//
class FLitSpriteVertexStream : public FVertexStream
{
public:

	//	FSpriteVertex
	struct FLitSpriteVertex
	{
		FVector	Position;
		FVector Normal;
		FColor	Diffuse;
		FLOAT	U,
				V;
	};

	FLitSpriteVertex		Vertices[4];

	// Constructor.
	FLitSpriteVertexStream()
	{
	}

	// GetRevision
	virtual INT GetRevision()
	{
		return 1;
	}

	// GetSize
	virtual INT GetSize()
	{
		return 4 * sizeof(FLitSpriteVertex);
	}

	// GetStride
	virtual INT GetStride()
	{
		return sizeof(FLitSpriteVertex);
	}

	// GetComponents
	virtual INT GetComponents(FVertexComponent* OutComponents)
	{
		OutComponents[0].Type = CT_Float3;
		OutComponents[0].Function = FVF_Position;
		OutComponents[1].Type = CT_Float3;
		OutComponents[1].Function = FVF_Normal;
		OutComponents[2].Type = CT_Color;
		OutComponents[2].Function = FVF_Diffuse;
		OutComponents[3].Type = CT_Float2;
		OutComponents[3].Function = FVF_TexCoord0;
		return 4;
	}

	// GetStreamData
	virtual void GetStreamData(void* Dest)
	{
		appMemcpy(Dest,&Vertices[0],4 * sizeof(FLitSpriteVertex));
	}

	// GetRawStreamData
	virtual void GetRawStreamData(void ** Dest, INT FirstVertex )
	{
		*Dest = &Vertices[FirstVertex];
	}
};


//
//	FCoronaRender
//
#define MAX_CORONA_LIGHTS 64
struct FCoronaRender
{
	struct FCoronaInfo
	{
		AActor* Actor;
		INT		iActor;
		FLOAT   Bright;
		DOUBLE	LastTrace;
		UBOOL	LastOccluded;

		FCoronaInfo()
		: Actor(NULL)
		, Bright(0)
		, iActor(0)
		, LastTrace(0)
		, LastOccluded(0)
		{}

		FCoronaInfo( AActor* InActor, FLOAT InBright, INT IniActor, DOUBLE InLastTrace, UBOOL InLastOccluded )
		: Actor(InActor)
		, Bright(InBright)
		, iActor(IniActor)
		, LastTrace(InLastTrace)
		, LastOccluded(InLastOccluded)
		{}
	};

	FCoronaInfo	CoronaLights[MAX_CORONA_LIGHTS];
	AActor*		VisibleLights[MAX_CORONA_LIGHTS];
	
	DOUBLE	LastTime;
	FLOAT	Delta;
	INT		iFree;
	INT		iVisible;

	FCoronaRender();

	virtual UBOOL IsOccluded( FLevelSceneNode* SceneNode, AActor* Light );
	virtual void AddVisibleLight( AActor* Light );
	virtual void AddCoronaLight( FLevelSceneNode* SceneNode, AActor* Light, FLOAT Delta );
	virtual void RenderCoronas( FLevelSceneNode* SceneNode, FRenderInterface* RI );
    void Reset();
};

// scion sz -
class FPostProcessRender
{
public:
    TList<FDynamicActor*>* PPDrawList;
	FLevelSceneNode* SceneNode;
	FRenderInterface* RI;

	FPostProcessRender( TList<FDynamicActor*>* InPPDrawList, FLevelSceneNode* InSceneNode, FRenderInterface* InRI ) :
		PPDrawList(InPPDrawList),
		SceneNode(InSceneNode),
		RI(InRI)
	{
		check( SceneNode && RI );
	}
	void Render();
};


/*-----------------------------------------------------------------------------
	Hit proxies.
-----------------------------------------------------------------------------*/

// Hit a Bsp surface.
struct ENGINE_API HBspSurf : public HHitProxy
{
	DECLARE_HIT_PROXY(HBspSurf,HHitProxy)
	INT iSurf;
	HBspSurf( INT iInSurf ) : iSurf( iInSurf ) {}
};

// Hit an actor.
struct ENGINE_API HActor : public HHitProxy
{
	DECLARE_HIT_PROXY(HActor,HHitProxy)
	AActor* Actor;
	HActor( AActor* InActor ) : Actor( InActor ) {}

	virtual AActor* GetActor()
	{
		return Actor;
	}
};

// Hit a brush vertex.
struct HBrushVertex : public HHitProxy
{
	DECLARE_HIT_PROXY(HBrushVertex,HHitProxy)
	ABrush* Brush;
	FVector Location;
	HBrushVertex( ABrush* InBrush, FVector InLocation ) : Brush(InBrush), Location(InLocation) {}
};

// Hit ray descriptor.
struct ENGINE_API HCoords : public HHitProxy
{
	DECLARE_HIT_PROXY(HCoords,HHitProxy)
	FVector	Origin,
			Direction;
	HCoords( FCameraSceneNode* InFrame )
	{
		FLOAT	X = (FLOAT) ((InFrame->Viewport->HitX+InFrame->Viewport->HitXL/2) - (InFrame->Viewport->SizeX / 2)) / (InFrame->Viewport->SizeX / 2.0f),
				Y = (FLOAT) ((InFrame->Viewport->HitY+InFrame->Viewport->HitYL/2) - (InFrame->Viewport->SizeY / 2)) / -(InFrame->Viewport->SizeY / 2.0f);

		Origin = InFrame->ViewOrigin;
		Direction = InFrame->Deproject(FPlane(X,Y,0.0f,InFrame->Viewport->IsOrtho() ? 1.0f : NEAR_CLIPPING_PLANE)) - InFrame->ViewOrigin;
	}
};

// Hit terrain.
struct ENGINE_API HTerrain : public HHitProxy
{
	DECLARE_HIT_PROXY(HTerrain,HHitProxy)
	ATerrainInfo* TerrainInfo;
	HTerrain( ATerrainInfo* InTerrainInfo ) : TerrainInfo( InTerrainInfo ) {}

	virtual AActor* GetActor()
	{
		return (AActor*)TerrainInfo;
	}
};

struct ENGINE_API HTerrainToolLayer : public HHitProxy
{
	DECLARE_HIT_PROXY(HTerrainToolLayer,HHitProxy)
	ATerrainInfo* TerrainInfo;
	UTexture* AlphaMap;			// The texture that will be painted on
	INT LayerNum;
	HTerrainToolLayer( ATerrainInfo* InTerrainInfo, INT InLayerNum, UTexture* InAlphaMap ) : TerrainInfo( InTerrainInfo ), LayerNum( InLayerNum ), AlphaMap(InAlphaMap) {}
};

struct ENGINE_API HMatineeTimePath : public HHitProxy
{
	ASceneManager* SceneManager;
	DECLARE_HIT_PROXY(HMatineeTimePath,HHitProxy)
	HMatineeTimePath( ASceneManager* InSceneManager ) : SceneManager(InSceneManager) {}
};

struct ENGINE_API HMatineeScene : public HHitProxy
{
	ASceneManager* SceneManager;
	DECLARE_HIT_PROXY(HMatineeScene,HHitProxy)
	HMatineeScene( ASceneManager* InSceneManager ) : SceneManager(InSceneManager) {}
};

struct ENGINE_API HMatineeAction : public HHitProxy
{
	ASceneManager* SM;
	UMatAction* MatAction;
	DECLARE_HIT_PROXY(HMatineeAction,HHitProxy)
	HMatineeAction( ASceneManager* InSM, UMatAction* InMatAction ) : SM(InSM), MatAction(InMatAction) {}
};

struct ENGINE_API HMatineeSubAction : public HHitProxy
{
	UMatAction* MatAction;
	UMatSubAction* MatSubAction;
	DECLARE_HIT_PROXY(HMatineeSubAction,HHitProxy)
	HMatineeSubAction( UMatSubAction* InMatSubAction, UMatAction* InMatAction ) : MatSubAction(InMatSubAction), MatAction(InMatAction) {}
};

struct ENGINE_API HMaterialTree : public HHitProxy
{
	UMaterial* Material;
	DWORD hWnd;				// The HWND of the texture properties dialog
	DECLARE_HIT_PROXY(HMaterialTree,HHitProxy);
	HMaterialTree( UMaterial* InMaterial, DWORD InHwnd ) : Material(InMaterial), hWnd(InHwnd) {}
};

//scion jg -- Added so I can determine how efficient a given vertex stream is
void ENGINE_API CalculateVertexCacheHits(UBOOL bIsTriStrip, DWORD dwVertexSize,
	_WORD* pIndices, DWORD dwNumIndices, DWORD* pdwNumDegenTris,
	DWORD* pdwNumCacheMisses, DWORD* pdwNumPagesCrossed);

/*-----------------------------------------------------------------------------
	D-8: AutoFOV (aspect-correct field of view), ported from engine-ut2004 (ufront step 2.16)
	and adapted to UC2. docs/porting/analysis/console-fov.md.
-----------------------------------------------------------------------------*/

//
// UE2's FovAngle is the HORIZONTAL field of view: FCameraSceneNode::UpdateMatrices gives
// FPerspectiveMatrix(FOV/2, FOV/2, 1, Aspect), whose row 1 divides by the aspect, so a wider window
// buys width out of height (90 deg at 4:3 is 73.74 deg vertically, at 16:9 only 58.72: the world
// looks zoomed vertically). UC2 was designed for a 4:3 picture (640x480). AutoFOV re-derives the
// horizontal FOV from the window's aspect so the VERTICAL one stays what it is at 4:3 (Hor+):
//
//   Off      (0) - stock: FovAngle used as it is.
//   HorPlus  (1) - 2*atan( tan(Fov/2) * Aspect/Ref ). The vertical FOV of Ref (4:3) at any aspect.
//   Adaptive (2) - max(Fov, HorPlus). The default: Hor+ on windows wider than Ref, stock on narrower
//                  ones (where Hor+ would cut both axes).
//
// Applied as a SCOPED override of the player's FovAngle around the frame (UGameEngine::Draw, after
// PlayerCalcView), so the world, the first-person weapon, the HUD's 3D markers, the LOD and shadow
// biases agree on one value, while everything outside the frame (script's ticks, the mouse:
// UCPlayerInput scales by DesiredFOV) keeps the player's real FOV. Config: [ufront.AutoFOV]
// AutoFOV / ReferenceAspect / WeaponScale (the template's section and keys); console AUTOFOV [0|1|2]
// for the session. At the reference aspect (640x480, 1024x768...) nothing changes, bit for bit.
//
enum EAutoFovMode
{
	AUTOFOV_Off      = 0,
	AUTOFOV_HorPlus  = 1,
	AUTOFOV_Adaptive = 2,
};

// The size a viewport's FOV is corrected for: its own SizeX x SizeY (G-6: a split-screen viewport too,
// AutoFOV per viewport aspect; D-8 had used the whole window's for it).
ENGINE_API void ufrontAutoFovSize( UViewport* Viewport, INT& SizeX, INT& SizeY );

// G-6: the ScaleFOVX/ScaleFOVY that make a split-screen viewport (ScaleX/ScaleY below 1) show exactly Fov
// horizontally with square pixels. FCameraSceneNode::UpdateMatrices scales camera space by ScaleX/ScaleY and
// the half-angle by ScaleFOVX/Y (Epic's Xbox maths: the SSDetailLevels' ReduceFOV, 0.5 for three and four
// players, a 79 degree quarter and a 45 degree top strip of three); the edge of the view is then
// tan(ScaleFOV*h)/ScaleX, so ScaleFOV = atan(ScaleX*tan(h))/h puts it at tan(h). Returns 0 (nothing to do)
// for a full-window viewport or with AutoFOV off (Epic's view, ReduceFOV and all).
ENGINE_API UBOOL ufrontSplitFovScale( UViewport* Viewport, FLOAT Fov, FLOAT& OutScale );

// Sets that scale for a scope (a frame, a projection, a canvas-drawn actor) and puts the viewport's back.
struct FScopedSplitFovScale
{
	UViewport*	Viewport;
	FLOAT		SavedX, SavedY;
	UBOOL		bSet;
	FScopedSplitFovScale( UViewport* InViewport, FLOAT Fov );
	~FScopedSplitFovScale();
};

// Effective horizontal FOV for a window of SizeX x SizeY. BaseFov unchanged when off, at the reference
// aspect (within 1e-4) or for a degenerate size. bLog: the one-line report (only the primary view).
ENGINE_API FLOAT ufrontEffectiveFov( FLOAT BaseFov, INT SizeX, INT SizeY, UBOOL bLog = 0 );

// The inverse: the base FOV whose effective FOV is EffectiveFov at this size.
ENGINE_API FLOAT ufrontBaseFov( FLOAT EffectiveFov, INT SizeX, INT SizeY );

// FOV for a canvas-drawn actor (the first-person weapon: UCWeapon.RenderOverlays draws it through
// Canvas.DrawActor with its own DisplayFOV, a separate scene node). WeaponScale blends in tan space
// between the stock FOV (0) and the corrected one (1, the default: the weapon keeps its 4:3 size and
// place at any aspect).
ENGINE_API FLOAT ufrontWeaponFov( FLOAT BaseFov, FLOAT EffectiveFov );

// One "[AutoFOV] weapon viewport=WxH base=B drawFov=D" line when the inputs change.
ENGINE_API void ufrontLogWeaponFov( INT SizeX, INT SizeY, FLOAT BaseFov, FLOAT DrawFov );

// The frame being drawn: the player whose FovAngle is overridden (NULL outside UGameEngine::Draw), its
// uncorrected FovAngle, and the effective value in use.
ENGINE_API extern APlayerController* GAutoFovScopeActor;
ENGINE_API extern FLOAT GAutoFovFrameBase;
ENGINE_API extern FLOAT GAutoFovFrameEffective;

// Console: AUTOFOV [Off|HorPlus|Adaptive|0|1|2] (session only; the ini keeps its value).
ENGINE_API UBOOL ufrontAutoFovExec( const TCHAR* Cmd, FOutputDevice& Ar );

#endif

/*------------------------------------------------------------------------------------
	The End.
------------------------------------------------------------------------------------*/

