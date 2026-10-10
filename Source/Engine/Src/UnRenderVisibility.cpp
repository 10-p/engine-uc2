/*=============================================================================
	UnRenderVisibility.cpp: Rendering visibility determination code.
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/

#include "EnginePrivate.h"
#include "UnRenderPrivate.h"

//
//	Current global visibility tag.  Incremented for every scene node.
//

static DWORD	CurrentVisibilityTag = 0;

FORCEINLINE float BoxDistanceSqr( FDynamicActor* DynamicActor, FVector& Origin ) 
{
    FBox& box = DynamicActor->BoundingBox;
	float s, d = 0.0f; 
	//find the square of the distance
	//from the sphere to the box
	for( long i=0 ; i<3 ; i++ ) 
	{ 
		if( Origin[i] < box.Min[i] )
		{	
			s = Origin[i] - box.Min[i];
			d += s*s; 	
		}
		else if( Origin[i] > box.Max[i] )
		{ 
			s = Origin[i] - box.Max[i];
			d += s*s; 
		}
    }
    return d;
}

FORCEINLINE UBOOL CheckCullDistance( FVector& ViewOrigin, FDynamicActor* DynamicActor, UViewport* pVP ) 
{
    if( DynamicActor->Actor->CullDistance == 0.0f || pVP->Precaching == 1)
        return 1;

	static FLOAT flAtanSq = Square(appTan(Clamp(pVP->Actor->FovAngle,10.f,85.f) * (PI/360.f)));

    float effectiveDistSqr = BoxDistanceSqr(DynamicActor, ViewOrigin) * flAtanSq;
    return (Square(DynamicActor->Actor->CullDistance) >= effectiveDistSqr);
}

//
//  ENodePass
//

enum ENodePass
{
	PASS_Front = 0,
	PASS_Plane = 1,
};

//
//  FNodeStack
//

struct FNodeStack
{
	FNodeStack*	Prev;
	INT			iNode,
				Outside,
				iFarNode,
				FarOutside,
				iFarLeaf;
	ENodePass	Pass;

	FProjectorRenderInfo**	DynamicProjectors;
	FDynamicLight**			DynamicLights;
	INT						NumDynamicLights,
							NumDynamicProjectors;

	static FNodeStack* Allocate(INT NumDynamicLights,INT NumDynamicProjectors)
	{
		FNodeStack*	NodeStack = (FNodeStack*) New<BYTE>(
				GSceneMem,
				sizeof(FNodeStack) +
				sizeof(FDynamicLight*) * NumDynamicLights +
				sizeof(FProjectorRenderInfo*) * NumDynamicProjectors
				);

		NodeStack->DynamicLights = (FDynamicLight**)&NodeStack[1];
		NodeStack->NumDynamicLights = NumDynamicLights;
		NodeStack->DynamicProjectors = (FProjectorRenderInfo**)(NodeStack->DynamicLights + NumDynamicLights);
		NodeStack->NumDynamicProjectors = NumDynamicProjectors;

		return NodeStack;
	}
};

//
//  FZoneInfo
//

class FZoneInfo : public FVisibilityInterface
{
public:

	TList<FConvexVolume*>*	Portals;
	TList<FConvexVolume*>*	AntiPortals;

	// Constructor/destructor.

	FZoneInfo() { Portals = AntiPortals = NULL; }
	~FZoneInfo() {}

	// FVisibilityInterface implementation.

	UBOOL Visible(const FBox& BoundingBox)
	{
		FVector	Origin = BoundingBox.GetCenter(),
				Extent = BoundingBox.Max - Origin;
		UBOOL	Visible = 0;

		for(TList<FConvexVolume*>* Portal = Portals;Portal;Portal = Portal->Next)
		{
			if(Portal->Element->BoxCheck(Origin,Extent) & CF_Inside)
			{
				Visible = 1;
				break;
			}
		}

		if(Visible)
		{
			for(TList<FConvexVolume*>* AntiPortal = AntiPortals;AntiPortal;AntiPortal = AntiPortal->Next)
			{
				if(!(AntiPortal->Element->BoxCheck(Origin,Extent) & CF_Outside))
				{
					Visible = 0;
					break;
				}
			}
		}

		return Visible;
	}

	UBOOL Visible(const FSphere& BoundingSphere)
	{
		UBOOL	Visible = 0;

		for(TList<FConvexVolume*>* Portal = Portals;Portal;Portal = Portal->Next)
		{
			if(Portal->Element->SphereCheck(BoundingSphere) & CF_Inside)
			{
				Visible = 1;
				break;
			}
		}

		if(Visible)
		{
			for(TList<FConvexVolume*>* AntiPortal = AntiPortals;AntiPortal;AntiPortal = AntiPortal->Next)
			{
				if(!(AntiPortal->Element->SphereCheck(BoundingSphere) & CF_Outside))
				{
					Visible = 0;
					break;
				}
			}
		}

		return Visible;
	}
};

//
//	FTranslucentDrawItem
//

struct FTranslucentDrawItem
{
	UBOOL					BSP;	
	INT						iNode;
	FDynamicLight**			DynamicLights;
	INT						NumDynamicLights;
	FProjectorRenderInfo**	DynamicProjectors;
	INT						NumDynamicProjectors;	
	FDynamicActor*			DynamicActor;
};

#if SUPPORTS_PRAGMA_PACK
#pragma pack (push,1)
#endif
// Each query uses 4 bytes of data in D3D on a 256 query block basis. I've
// set aside 16K for queries. Tune this as needed. Note that too few and
// the Xbox libs will assert in debug mode.
#define MAX_QUERY_INDEX 4096

/* ============================================================================
 * Author: jg
 * ============================================================================
 *
 * This class holds all of the data for a given hardware occlusion query
 *
 * ============================================================================
 */
class FHWOcclusionQuery : public FBaseOcclusionQuery
{
	// The index of the query
	_WORD QueryIndex;

public:
	/* ============================================================================
	 * FHWOcclusionQuery::FHWOcclusionQuery
	 * Author: jg
	 * 
	 * Initializes with values
	 *
	 * inputs:	pDyn -- The dynamic actor to associate with this query
	 *			Index -- The index to use for the query
	 *
	 * ============================================================================
	 */
	FHWOcclusionQuery(FDynamicActor* pDyn,_WORD Index)
	{
		pDynActor = pDyn;
		QueryIndex = Index;
	}

	/* ============================================================================
	 * FHWOcclusionQuery::FHWOcclusionQuery
	 * Author: jg
	 * 
	 * Default ctor
	 *
	 * ============================================================================
	 */
	FHWOcclusionQuery()
	{
		pDynActor = NULL;
		QueryIndex = (_WORD)-1;
	}

	/* ============================================================================
	 * FHWOcclusionQuery::operator=
	 * Author: jg
	 * 
	 * Safely handle assignments
	 *
	 * inputs:	In -- The query to copy
	 *
	 * ============================================================================
	 */
	FHWOcclusionQuery& operator=(const FHWOcclusionQuery& In)
	{
		check(this != &In);
		pDynActor = In.pDynActor;
		QueryIndex = In.QueryIndex;
		return *this;
	}

// Accessors

	/* ============================================================================
	 * FHWOcclusionQuery::GetQueryIndex
	 * Author: jg
	 * 
	 * Returns the index for this query
	 *
	 * ============================================================================
	 */
	FORCEINLINE _WORD GetQueryIndex(void)
	{
		guard(FHWOcclusionQuery::GetQueryIndex)
		return QueryIndex;
		unguard;
	}

	/* ============================================================================
	 * FHWOcclusionQuery::SetQueryIndex
	 * Author: jg
	 * 
	 * Sets the query index for this query
	 *
	 * ============================================================================
	 */
	FORCEINLINE void SetQueryIndex(_WORD Query)
	{
		guard(FHWOcclusionQuery::SetQueryIndex)
		QueryIndex = Query;
		unguard;
	}
};

/* ============================================================================
 * Author: jg
 * ============================================================================
 *
 * This class handles all hardware occlusion culling. The basic procedure is:
 *
 *	Determine occlusion query results from last frame
 *	Render terrain
 *	Render BSP
 *	Render solid actors
 *	Render occlusion queries for this frame
 *	Render translucent actors
 *
 * Two lists (per viewport) are maintained to do this. The first list manages
 * all pending queries and is the primary list for all activity. The second
 * list manages any queries that took too long to complete. Since they must be
 * cleared before the query index can be used again we must attempt to read
 * their results even though they no longer matter. Any query that makes it
 * onto the waiting list is considered to have been visible.
 *
 * Both lists are the templated pooled lists to reduce the malloc/free overhead
 * Also, the GSceneMem can't be used as these objects live across frames
 *
 * ============================================================================
 */
class FOcclusionQueryManager
{
	// For easier typing
	#define FHWOcclusionNode TPooledList<FHWOcclusionQuery>
	// This is the primary list of pending queries. One for each viewport
	FHWOcclusionNode* PendingList[MAX_SS_VIEWPORTS];
	// These took too long to process so keep trying to clear them
	FHWOcclusionNode* WaitingList[MAX_SS_VIEWPORTS];
	// The mesh to use for hardware occlusion rendering
	UStaticMesh* pOcclusionMesh;
	// The next index to use as a query
	_WORD NextQueryIndex;

	/* ============================================================================
	 * FOcclusionQueryManager::AddViewport
	 * Author: jg
	 * 
	 * Places the viewport in the first available slot
	 *
	 * input:	pViewport -- The viewport to add
	 *
	 * returns:	The index the viewport was added at
	 *
	 * ============================================================================
	 */
	FORCEINLINE INT AddViewport(UViewport* pViewport)
	{
		INT Index = pViewport->GetViewportIndex();
		return (Index < MAX_SS_VIEWPORTS) ? Index : INDEX_NONE;
	}

	/* ============================================================================
	 * FOcclusionQueryManager::GetViewportIndex
	 * Author: jg
	 * 
	 * Finds the index of the viewport. Adds it if not known
	 *
	 * input:	pViewport -- The viewport to add
	 *
	 * returns:	The index of the viewport
	 *
	 * ============================================================================
	 */
	FORCEINLINE INT GetViewportIndex(UViewport* pViewport)
	{
		INT Index = pViewport->GetViewportIndex();
		return (Index < MAX_SS_VIEWPORTS) ? Index : INDEX_NONE;
	}

	/* ============================================================================
	 * FOcclusionQueryManager::GetOcclusionMesh
	 * Author: jg
	 * 
	 * If the mesh has been previously loaded, it returns a pointer to it. Otherwise
	 * it loads the mesh. It must be in Entry and must be named CubeOfDeath
	 *
	 * returns:	The static mesh to use as our bounding box test
	 *
	 * ============================================================================
	 */
	FORCEINLINE UStaticMesh* GetOcclusionMesh(void)
	{
		guard(FOcclusionQueryManager::GetOcclusionMesh);
		// Find it if we haven't a ref to it
		if (pOcclusionMesh == NULL)
		{
			pOcclusionMesh = FindObjectChecked<UStaticMesh>(ANY_PACKAGE,
				TEXT("CubeOfDeath"));
		}
		return pOcclusionMesh;
		unguard;
	}

	/* ============================================================================
	 * FOcclusionQueryManager::GetNextQueryIndex
	 * Author: jg
	 * 
	 * Figures out what the next query to use is
	 *
	 * returns	The query value to use
	 * ============================================================================
	 */
	FORCEINLINE _WORD GetNextQueryIndex(void)
	{
		guard(FOcclusionQueryManager::GetNextQueryID);
		_WORD QueryID = NextQueryIndex;
		// Increment the query index
		NextQueryIndex = ++NextQueryIndex % MAX_QUERY_INDEX;
		return QueryID;
		unguard;
	}

	/* ============================================================================
	 * FOcclusionQueryManager::RenderQuery
	 * Author: jg
	 * 
	 * Renders an individual query. It uses the bounding box of the dynamic actor
	 * to generate the values placed in the vertex shader constants. The vertex
	 * shader constants will act like a local to world transform. On the PC, it
	 * modifies the L2W in order to do the same thing.
	 *
	 * inputs:	RI -- The render interface to render with
	 *			Query -- The query to render
	 *			Section -- The static mesh section to render
	 *			bCommit -- Whether to commit states or not. Only needed once
	 *
	 * ============================================================================
	 */
	FORCEINLINE void RenderQuery(FRenderInterface* RI,FHWOcclusionQuery& Query,
		const FStaticMeshSection& Section,UBOOL bCommit)
	{
		guard(FOcclusionQueryManager::RenderQuery);
		// Get the bounding box information
		const FVector& Extent = Query.GetDynamicActor()->BoundingBox.GetExtent();
		const FVector& Origin = Query.GetDynamicActor()->BoundingBox.GetCenter();
		// Don't do the rest if this fails
		if (RI->BeginOcclusionQuery())
		{
#ifdef _XBOX
			// Build the vertex shader constants
			FLOAT Constants[] =
			{
				Extent.X,Extent.Y,Extent.Z,1.f,
				Origin.X,Origin.Y,Origin.Z,1.f
			};
			// Send this through the pipeline
			RI->SetVertexShaderConstant(VSC_COMPRESSION_SCALE,Constants,2);
			// Now render our static mesh
			RI->RunPushBuffer(Section.pPushBuffer,1,bCommit);
#else
			// Set the transform that will make act the same as the compression shader
			RI->SetTransform(TT_LocalToWorld,
				FScaleMatrix(Extent) * FTranslationMatrix(Origin));
			// Now render it
			RI->DrawPrimitive(Section.IsStrip ? PT_TriangleStrip : PT_TriangleList,
				Section.FirstIndex,Section.NumPrimitives,Section.MinVertexIndex,
				Section.MaxVertexIndex);
#endif
			// Tell the query what its index is
			Query.SetQueryIndex(GetNextQueryIndex());
			// Now mark it as done
			RI->EndOcclusionQuery(Query.GetQueryIndex());
		}
		unguard;
	}

	/* ============================================================================
	 * FOcclusionQueryManager::ClearWaitingQueries
	 * Author: jg
	 * 
	 * Reads the visibility results for any queries on the waiting list. The
	 * results are discarded. If the query is still not done, it is reinserted into
	 * the waiting list.
	 *
	 * inputs:	pViewport -- The viewport that needs to be cleaned up
	 *			Index -- The viewport index to use
	 *
	 * ============================================================================
	 */
	void ClearWaitingQueries(UViewport* pViewport,INT Index)
	{
		guard(FOcclusionQueryManager::ClearWaitingQueries);
		// Start at the beginning of the list
		FHWOcclusionNode* pQuery = WaitingList[Index];
		// Terminate this list so we can reinsert if needed
		WaitingList[Index] = NULL;
		// Now work through each query and see if it is done
		while (pQuery != NULL)
		{
			// Get the next node to process
			FHWOcclusionNode* pNext = pQuery->Next;
			DWORD dwPixels;
			UBOOL bCompleted = 1;
			// Check for an unrendered query
			_WORD QueryIndex = pQuery->Element.GetQueryIndex();
			// If this query never rendered, just treat it as done
			if (QueryIndex < MAX_QUERY_INDEX)
			{
				// Try to fetch the results of the query
				bCompleted = pViewport->RI->GetOcclusionQueryResults(QueryIndex,
					dwPixels);
			}
			if (bCompleted)
			{
				// We can return this node to the pool since it is done
				FHWOcclusionNode::ReturnInstance(pQuery);
			}
			else
			{
				// The query still isn't done so reinsert it at the begining of
				// the list
				pQuery->Next = WaitingList[Index];
				WaitingList[Index] = pQuery;
			}
			// Move forward to the next query to test
			pQuery = pNext;
		}
		unguard;
	}

public:
	/* ============================================================================
	 * FOcclusionQueryManager::FOcclusionQueryManager
	 * Author: jg
	 * 
	 * Initializes all of the vars for this manager
	 *
	 * ============================================================================
	 */
	FOcclusionQueryManager()
	{
		for (INT Index = 0; Index < MAX_SS_VIEWPORTS; Index++)
		{
			WaitingList[Index] = PendingList[Index] = NULL;
		}
		NextQueryIndex = 0;
		pOcclusionMesh = NULL;
	}

	/* ============================================================================
	 * FOcclusionQueryManager::AddQuery
	 * Author: jg
	 * 
	 * Adds a dynamic actor to the query list. It maintains a different list for
	 * each viewport so that actor visible in one viewport can be occluded in
	 * another.
	 *
	 * inputs:	pSceneNode -- The scene node currently being rendered
	 *			pDynActor -- the actor to test
	 *
	 * ============================================================================
	 */
	FORCEINLINE void AddQuery(FSceneNode* pSceneNode,FDynamicActor* pDynActor)
	{
		guard(FOcclusionQueryManager::AddQuery);
		FUNC_CLOCK(C,GStats.DWORDStats(GEngineStats.STATS_HWOcclusion_OcclusionTestCycles));
		// Only do these checks for valid scene node types. Skip skyboxes & shadows
		if (pSceneNode->CanUseHardwareOcclusion())
		{
			// Skip any actors not needing testing and make any actor that is
			// really close to the camera as visible
			if (pDynActor->NeedsOcclusionTest() &&
				pDynActor->BoundingBox.PointInBox(pSceneNode->ViewOrigin,64.f) == 0)
			{
				UViewport* pViewport = pSceneNode->Viewport;
				// If it is visible use deferred testing
				if (pDynActor->IsVisible(pViewport) &&
					pDynActor->CanUseDeferredOcclusionTest())
				{
					// We need deferred query support for this
					pDynActor->SetNeedsDeferredQuery(pViewport,1);
				}
				// Otherwise use bounding box testing
				else
				{
					// Figure out which list we are using
					INT Index = GetViewportIndex(pViewport);
					// check for invalid viewport
					if( Index == INDEX_NONE )
						return;
					// We need immediate query instead
					pDynActor->SetNeedsDeferredQuery(pViewport,0);
					// Build a new item with an invalid query index
					FHWOcclusionQuery Query(pDynActor,MAX_QUERY_INDEX + 1);
					// Add the item to the list
					PendingList[Index] = FHWOcclusionNode::GetInstance(Query,
						PendingList[Index]);
					// Make the link between the viewport, dynamic actor, and query
					pDynActor->SetQuery(pViewport,&PendingList[Index]->Element);
				}
			}
			else
			{
				// We don't need any query types
				pDynActor->SetNeedsDeferredQuery(pSceneNode->Viewport,0);
				// Just mark these as visible
				pDynActor->SetVisible(pSceneNode->Viewport,1);
			}
		}
		// We don't want to check for occlusion in child nodes
		else if (pSceneNode->IsChildNode())
		{
			pDynActor->SetNeedsOcclusionTest(0);
		}
		unguard;
	}

	/* ============================================================================
	 * FOcclusionQueryManager::SubmitQueries
	 * Author: jg
	 * 
	 * Sets all of the rendering states for doing the queries. Note it sets all
	 * state once and never changes it. The only thing that changes are the vertex
	 * shader constants for the bounding boxes. This results in very low overhead
	 * being placed into the push buffer (a few dozen bytes). It is extremely
	 * important that all state commiting is handled optimally. If not this function
	 * will hurt more than help. If things are done optimally, UE2X can render 1500
	 * bounding boxes in ~3.4ms (CPU) on an Xbox.
	 *
	 * inputs:	pSceneNode -- The scene node currently being rendered
	 *
	 * ============================================================================
	 */
	void SubmitQueries(FSceneNode* pSceneNode)
	{
		guard(FOcclusionQueryManager::SubmitQueries);
		FUNC_CLOCK(C,GStats.DWORDStats(GEngineStats.STATS_HWOcclusion_OcclusionTestCycles));
		checkSlow(pSceneNode->CanUseHardwareOcclusion());
		UViewport* pViewport = pSceneNode->Viewport;
		// Figure out which list we are using
		INT Index = GetViewportIndex(pViewport);
		// check for invalid viewport
		if( Index == INDEX_NONE )
			return;
		// Clean up any waiting queries
		ClearWaitingQueries(pViewport,Index);
		// Get the render interface
		FRenderInterface* RI = pViewport->RI;
		// Store the current state
		RI->PushState();
		// Get the static mesh used for rendering
		UStaticMesh* pMesh = GetOcclusionMesh();
		// Set the vertex streams
		FVertexStream* VertexStreams[] = { &pMesh->VertexStream };
#ifdef _XBOX
		// Use the compression shader on the Xbox
		RI->SetVertexStreams(VS_CompressedMesh,VertexStreams,1);
		// We want no index buffers on the xbox
		RI->SetIndexBuffer(NULL,0);
		// Use identity for local to world since all boxes are in world space
		RI->SetTransform(TT_LocalToWorld,FMatrix::Identity);
#else
		// No compression on the PC
		RI->SetVertexStreams(VS_FixedFunction,VertexStreams,1);
		// We do use index buffers on the PC
		RI->SetIndexBuffer(&pMesh->IndexBuffer,0);
#endif
		// Disable stencil
		RI->SetStencilOp(CF_Always,1,0xFF,SO_Keep,SO_Keep,SO_Keep,0xFF);
		// Disable all writes to the back buffer
		RI->SetColorWriteMask(CWM_COLORWRITEENABLE_NONE);
		// Build the wire frame material
		DECLARE_STATIC_UOBJECT(UShader,OcclusionShader,
		{
			OcclusionShader->Wireframe = 1;
		});
		// Use the final blend to help turn off color/alpha writes. Annoying,
		// but you have to do it with the state and the material. ZTest but
		// don't ZWrite, since we only want rejections
		DECLARE_STATIC_UOBJECT(UFinalBlend,OcclusionFB,
		{
			OcclusionFB->Material = OcclusionShader;
			OcclusionFB->AlphaTest = 1;
			OcclusionFB->ZTest = 1;
			OcclusionFB->ZWrite = 0;
			OcclusionFB->AlphaWriteEnable = 0;
			OcclusionFB->ColorWriteEnable = 0;
			OcclusionFB->FrameBufferBlending = FB_Overwrite;
		});
		// Set this material for all rendering
		RI->SetMaterial(OcclusionFB);
		// Grab the only section on this mesh
		FStaticMeshSection& Section = GetOcclusionMesh()->Sections(0);
		UBOOL bCommit = 1;
		// Loop through the queries rendering each query in turn
		for (FHWOcclusionNode* pQuery = PendingList[Index]; pQuery != NULL;
			pQuery = pQuery->Next)
		{
			// Only render queries that have not already been submitted. Those
			// will all have invalid query index values. Valid values means they
			// were rendered as the actor and not the bounding box
			if (pQuery->Element.GetQueryIndex() > MAX_QUERY_INDEX)
			{
				// Do the per query work in here. Only commit the first time
				RenderQuery(RI,pQuery->Element,Section,bCommit);
				bCommit = 0;
			}
		}
		// Restore the previous stencil mode
		RI->ResetStencilOp();
		// Restore the old state
		RI->PopState();
		unguard;
	}

	/* ============================================================================
	 * FOcclusionQueryManager::UpdateQueryResults
	 * Author: jg
	 * 
	 * Reads the visibility results for any queries on the pending list. Each actor
	 * is marked as visible or not depending on the results. If the test has not
	 * completed in time, it treats the actor as visible and places the test on the
	 * waiting list. It's best to treat indeterminate results as visible otherwise
	 * you can have an actor flash in/out over time.
	 *
	 * inputs:	pSceneNode -- The scene node currently being rendered
	 *
	 * ============================================================================
	 */
	void UpdateQueryResults(FSceneNode* pSceneNode)
	{
		guard(FOcclusionQueryManager::UpdateQueryResults);
		FUNC_CLOCK(C,GStats.DWORDStats(GEngineStats.STATS_HWOcclusion_OcclusionTestCycles));
		checkSlow(pSceneNode->CanUseHardwareOcclusion());
		UViewport* pViewport = pSceneNode->Viewport;
		// Figure out which list we are using
		INT Index = GetViewportIndex(pViewport);
		// check for invalid viewport
		if( Index == INDEX_NONE )
			return;
		// Clean up any waiting queries
		ClearWaitingQueries(pViewport,Index);
		// Start at the beginning of the list
		FHWOcclusionNode* pQuery = PendingList[Index];
		// Terminate this list so we can reinsert if needed
		PendingList[Index] = NULL;
		// Now work through each query and see if it is done
		while (pQuery != NULL)
		{
			// Get the next node to process
			FHWOcclusionNode* pNext = pQuery->Next;
			// Default to being visible
			DWORD dwPixels = 1;
			// Try to fetch the results of the query
			UBOOL bCompleted = pViewport->RI->GetOcclusionQueryResults(
				pQuery->Element.GetQueryIndex(),dwPixels);
			UBOOL bVisible = dwPixels > 0;
			// If we have a valid actor
			if (pQuery->Element.GetDynamicActor() != NULL)
			{
				// Update the visibility status for this actor
				pQuery->Element.GetDynamicActor()->SetVisible(pViewport,bVisible);
				// Sever the link between these two
				pQuery->Element.GetDynamicActor()->SetQuery(pViewport,NULL);
				// Check to see if we are GPU bound
				if (bCompleted == FALSE)
				{
					// Because we were GPU bound last time, defer these for future checks
					pQuery->Element.GetDynamicActor()->SetNeedsDeferredQuery(pViewport,Index);
				}
				// Zero the dynamic actor as it is no longer needed
				pQuery->Element.ClearDynamicActor();
			}
			// Figure out whether to delete this query or put on the waiting list
			if (bCompleted)
			{
				// We can return this node to the pool since it is done
				FHWOcclusionNode::ReturnInstance(pQuery);
			}
			else
			{
				// The query isn't done so insert it at the begining of
				// the waiting list
				pQuery->Next = WaitingList[Index];
				WaitingList[Index] = pQuery;
			}
			// Move forward to the next query to test
			pQuery = pNext;
			// Update our stats
			STAT_INC(GStats.DWORDStats(GEngineStats.STATS_HWOcclusion_OcclusionTests));
			STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_HWOcclusion_OccludedObjects),
				bVisible == 0);
			STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_HWOcclusion_WaitingTests),
				bCompleted == 0);
		}
		unguard;
	}

	/* ============================================================================
	 * FOcclusionQueryManager::ClearViewportQueries
	 * Author: jg
	 * 
	 * When a viewport is destroyed (splitscreen) we need to clear out any queries
	 * that were remaining or D3D will get confused. It moves any remaining queries
	 * to the waiting list and continually calls clear on those until there are no
	 * more outstanding queries. This should only happen on level change or a
	 * splitscreen player leaving the game.
	 *
	 * inputs:	pViewport -- The viewport to clean out the entries for
	 *
	 * ============================================================================
	 */
	void ClearViewportQueries(UViewport* pViewport)
	{
		guard(FOcclusionQueryManager::ClearViewportQueries);
		// Figure out which list we are using
		INT Index = GetViewportIndex(pViewport);
		// check for invalid viewport
		if( Index == INDEX_NONE )
			return;
		// We have to insert them one at a time
		for (FHWOcclusionNode* pQuery = PendingList[Index]; pQuery != NULL;)
		{
			FHWOcclusionNode* pNext = pQuery->Next;
			// Insert the query onto the waiting list
			pQuery->Next = WaitingList[Index];
			WaitingList[Index] = pQuery;
			if( pQuery->Element.GetDynamicActor() != NULL )
			{
				// Sever the link between these two
				pQuery->Element.GetDynamicActor()->SetQuery(pViewport,NULL);
				// Zero the dynamic actor as it is no longer needed
				pQuery->Element.ClearDynamicActor();
			}			
			// Now move to the next item
			pQuery = pNext;
		}
		PendingList[Index] = NULL;
		// All queries must be cleared before we can continue
		while (WaitingList[Index] != NULL)
		{
			// Clean up any waiting queries
			ClearWaitingQueries(pViewport,Index);
		}
		// Remove the specified viewport from the list of valid ones
		unguard;
	}

	/* ============================================================================
	 * FOcclusionQueryManager::BeginDeferredQuery
	 * Author: jg
	 * 
	 * Starts query tracking for a deferred occlusion test.
	 *
	 * inputs:	pSceneNode -- The scene node currently being rendered
	 *			pDynActor -- the actor to test
	 *
	 * ============================================================================
	 */
	FORCEINLINE void BeginDeferredQuery(FSceneNode* pSceneNode,FDynamicActor* pDynActor)
	{
		guard(FOcclusionQueryManager::BeginDeferredQuery);
		FUNC_CLOCK(C,GStats.DWORDStats(GEngineStats.STATS_HWOcclusion_OcclusionTestCycles));
		// Only do this if the actor needs deferred querying
		if (pSceneNode->CanUseHardwareOcclusion() &&
			pDynActor->NeedsDeferredQuery(pSceneNode->Viewport))
		{
			// Start the deferred query 
			pSceneNode->Viewport->RI->BeginOcclusionQuery();
		}
		unguard;
	}

	/* ============================================================================
	 * FOcclusionQueryManager::EndDeferredQuery
	 * Author: jg
	 * 
	 * This version of adding a query is used when the object was visible last
	 * frame, so you assume it's visible this frame and use the object rendering as
	 * the test instead of rendering the bounding box.
	 *
	 * inputs:	pSceneNode -- The scene node currently being rendered
	 *			pDynActor -- the actor to test
	 *
	 * ============================================================================
	 */
	FORCEINLINE void EndDeferredQuery(FSceneNode* pSceneNode,FDynamicActor* pDynActor)
	{
		guard(FOcclusionQueryManager::EndDeferredQuery);
		FUNC_CLOCK(C,GStats.DWORDStats(GEngineStats.STATS_HWOcclusion_OcclusionTestCycles));
		UViewport* pViewport = pSceneNode->Viewport;
		// Only do this if the actor needs deferred querying
		if (pSceneNode->CanUseHardwareOcclusion() &&
			pDynActor->NeedsDeferredQuery(pViewport))
		{
			// Figure out which list we are using
			INT Index = GetViewportIndex(pViewport);
			// check for invalid viewport
			if( Index == INDEX_NONE )
				return;
			// Build a new item with a valid query index
			FHWOcclusionQuery Query(pDynActor,GetNextQueryIndex());
			// Add the item to the list
			PendingList[Index] = FHWOcclusionNode::GetInstance(Query,
				PendingList[Index]);
			// Make the link between the viewport, dynamic actor, and query
			pDynActor->SetQuery(pViewport,&PendingList[Index]->Element);
			// End the query 
			pViewport->RI->EndOcclusionQuery(PendingList[Index]->Element.GetQueryIndex());
		}
		else
		{
			// Just mark these as visible
			pDynActor->SetVisible(pSceneNode->Viewport,1);
		}
		unguard;
	}
};

// The global occlusion query manager
FOcclusionQueryManager HWOcclusionManager;
// The singleton pooled list manager
#if defined(_MSC_VER)
TPooledList<FHWOcclusionQuery>::TPoolInstance<FHWOcclusionQuery> TPooledList<FHWOcclusionQuery>::PoolInstance;
#else
template<> TPooledList<FHWOcclusionQuery>::TPoolInstance<FHWOcclusionQuery> TPooledList<FHWOcclusionQuery>::PoolInstance{};
#endif

// scion sz - needed for splitscreen and flushing queries in unloadmap
// Simple function accessible everywhere
void ENGINE_API ClearQueriesForViewport(UViewport* pViewport)
{
	HWOcclusionManager.ClearViewportQueries(pViewport);
}

// free instances in global pool of FHWOcclusionQuery items
void ENGINE_API FlushHWOcclusionQueryPool()
{
	FHWOcclusionNode::FreePool();
}

#if SUPPORTS_PRAGMA_PACK
#pragma pack (pop)
#endif

//
//  FRenderState
//

struct FRenderState
{
	FLevelSceneNode*				SceneNode;				// The scene node that's rendering the level.
	FRenderInterface*				RI;						// The renderer.
	ULevel*							Level;					// The level being rendered.
	UModel*							Model;					// The level's BSP tree.
	UClient*						Client;					// UClient for detail options.

	TList<FDynamicActor*>**			LeafActors;				// Maps a leaf index to a list of actors in the leaf.
	TList<FDynamicActor*>*			OutsideActors;			// A list of actors that are entirely outside of the level.

	FZoneInfo						Zones[64];				// The visibility volumes for each zone.
	TList<INT>*						ActiveZones;			// The zones that are visible.
	QWORD							ActiveZoneMask;			// A visible zone mask.
	BYTE*							RenderedPortals;		// Contains a bitmask for [anti]portal surfaces that have been rendered.

	TList<FLevelSceneNode*>*		ChildSceneNodes;		// Child scene nodes(mirrors, skyboxes, etc)

	FDynamicLight***				LeafLights;				// A list of dynamic lights in each leaf.
	FProjectorRenderInfo***			LeafProjectors;			// A list of dynamic projectors in each leaf.

	TList<FDynamicActor*>*			ActorDrawList;			// A list of actors to render.
	//scion jg -- The list of actors that have a single section. These actors
	// are sorted so that they render together (fewer state changes)
	TList<FDynamicActor*>*			SortedActorDrawList;
	// scion sz - special case list for the nali pawns
	TList<FDynamicActor*>*			NaliPawnActorDrawList;
	// A draw list for translucent surfaces and actors.
	TList<FTranslucentDrawItem>*	TranslucentDrawList;	
	// scion sz - A list of actors to render that have post processing effects materials.
	TList<FDynamicActor*>*			PostProcessDrawList;	
	// scion sz - A list of actors to render that have emissive materials.
	TList<FDynamicActor*>*			EmissiveDrawList;		
	// scion sz - so we can render all static projector geometry after the level is rendered
	TList<FDynamicActor*>*			StaticProjectorBatches;	

// scion sz - 
#ifndef _XBOX
	FStaticMeshBatchList			StaticMeshBatchList;	// A list of visible static mesh batches.
#endif

	FBspDrawList**					BspDrawLists;			// An array of BSP draw list pointers.
	TList<INT>*						SectionDrawList;		// A list of BSP sections to draw.

	TList<FBspStencilDrawList*>*	StencilDrawLists;		// A list of BSP stencil draw lists.

	DWORD							SkyStencilMask;			// The stencil mask to initialize untouched pixels to before drawing the sky.	
};

TList<FProjectorRenderBatch*>*	GProjectorBatchList;

//
// CoronaRender - actually not global.
//

FCoronaRender GCoronaRender;

// uc2 port (G-9): one corona state per split-screen view (UnRender.h).
#define UC2_SPLIT_CORONA_STATES 4
static FCoronaRender	GSplitCoronaRender[UC2_SPLIT_CORONA_STATES];
static UViewport*		GSplitCoronaViewport[UC2_SPLIT_CORONA_STATES];

ENGINE_API FCoronaRender& UC2CoronaRenderFor( UViewport* Viewport )
{
	UClient* Client = Viewport ? Viewport->GetOuterUClient() : NULL;
	if( !Client || !Client->Viewports.Num() || Client->Viewports(0)==Viewport )
		return GCoronaRender;
	for( INT i=0; i<UC2_SPLIT_CORONA_STATES; i++ )
		if( GSplitCoronaViewport[i]==Viewport )
			return GSplitCoronaRender[i];
	for( INT i=0; i<UC2_SPLIT_CORONA_STATES; i++ )
		if( !GSplitCoronaViewport[i] || Client->Viewports.FindItemIndex(GSplitCoronaViewport[i])==INDEX_NONE )
		{
			GSplitCoronaViewport[i] = Viewport;
			GSplitCoronaRender[i].Reset();
			return GSplitCoronaRender[i];
		}
	return GCoronaRender;	// more views than states: Epic's shared one
}

ENGINE_API void UC2ResetCoronas()
{
	GCoronaRender.Reset();
	for( INT i=0; i<UC2_SPLIT_CORONA_STATES; i++ )
		GSplitCoronaRender[i].Reset();
}

//
//	ExtrudeAntiPortal
//

FConvexVolume* ExtrudeAntiPortal(FVector ViewOrigin,FDynamicActor* Actor)
{
	UConvexVolume*	ConvexVolume = Actor->Actor->AntiPortal;
	FConvexVolume*	Result = new(GSceneMem) FConvexVolume;
	FLOAT*			DotProducts = New<FLOAT>(GSceneMem,ConvexVolume->Faces.Num());

	for(INT FaceIndex = 0;FaceIndex < ConvexVolume->Faces.Num() && Result->NumPlanes < FConvexVolume::MAX_VOLUME_PLANES;FaceIndex++)
	{
		FConvexVolumeFace*	Face = &ConvexVolume->Faces(FaceIndex);
		FPlane				Plane = Face->Plane.TransformBy(Actor->LocalToWorld);
		FLOAT				Dot = Plane.PlaneDot(ViewOrigin);

		if(Dot > 0.0f)
			Result->BoundingPlanes[Result->NumPlanes++] = Plane;

		DotProducts[FaceIndex] = Dot;
	}

	for(INT EdgeIndex = 0;EdgeIndex < ConvexVolume->Edges.Num() && Result->NumPlanes < FConvexVolume::MAX_VOLUME_PLANES;EdgeIndex++)
	{
		FConvexVolumeEdge*	Edge = &ConvexVolume->Edges(EdgeIndex);

		if(DotProducts[Edge->Faces[0]] * DotProducts[Edge->Faces[1]] < 0.0f)
		{
			INT	FrontFace = DotProducts[Edge->Faces[1]] > 0.0f;

			Result->BoundingPlanes[Result->NumPlanes++] = FPlane(
				ViewOrigin,
				Actor->LocalToWorld.TransformFVector(ConvexVolume->Faces(Edge->Faces[FrontFace]).Vertices(Edge->Vertices[FrontFace])),
				Actor->LocalToWorld.TransformFVector(ConvexVolume->Faces(Edge->Faces[1 - FrontFace]).Vertices(Edge->Vertices[1 - FrontFace]))
				);
		}
	}

	return Result;
}

//
//	BuildAntiPortals
//

static void BuildAntiPortals(FRenderState& RenderState,INT ZoneIndex)
{
	if(!RenderState.SceneNode->Viewport->Precaching && ZoneIndex < RenderState.Level->ZoneRenderInfo.Num())
	{
		for(INT ActorIndex = 0;ActorIndex < RenderState.Level->ZoneRenderInfo(ZoneIndex).AntiPortals.Num();ActorIndex++)
		{
			FDynamicActor*	DynamicActor = RenderState.Level->ZoneRenderInfo(ZoneIndex).AntiPortals(ActorIndex)->GetActorRenderData();

			//scion jg -- Check cull distance before visibility
			if (DynamicActor->VisibilityTag != CurrentVisibilityTag &&
				CheckCullDistance(RenderState.SceneNode->ViewOrigin,DynamicActor,RenderState.SceneNode->Viewport) && RenderState.Zones[ZoneIndex].Visible(DynamicActor->BoundingBox))
			{
				FConvexVolume*	AntiPortalVolume = ExtrudeAntiPortal(RenderState.SceneNode->ViewOrigin,DynamicActor);

				RenderState.Zones[ZoneIndex].AntiPortals = new(GSceneMem) TList<FConvexVolume*>(AntiPortalVolume,RenderState.Zones[ZoneIndex].AntiPortals);

				if(RenderState.SceneNode->Viewport->Actor->ShowFlags & SHOW_Volumes)
					RenderState.ActorDrawList = new(GSceneMem) TList<FDynamicActor*>(DynamicActor,RenderState.ActorDrawList);

				DynamicActor->VisibilityTag = CurrentVisibilityTag;
			}
		}
	}
}

//
//  RenderActor
//

static void RenderActor(FRenderState& RenderState,FDynamicActor* DynamicActor,UBOOL bSkipPushPop=0)
{
	guardSlow(RenderActor);
	
	AActor*	RenderActor = DynamicActor->Actor;	
	checkSlow(RenderActor);
	
	// scion sz - get base for lighting
	AActor* BaseLitActor = RenderActor->GetAmbientLightingActor();
	FDynamicActor* BaseLitDynActor = DynamicActor;
	if( BaseLitActor != RenderActor )
	{	
		BaseLitDynActor = BaseLitActor->GetActorRenderData();
	}
	checkSlow(BaseLitActor);

	// scion sz - 
	// Don't do lighting/projector calcs when in post processing mode
	// Also always use the BaseLitActor for lighting and only update its lighting once per frame
	if(	( BaseLitDynActor->LastLightUpdateTime != BaseLitActor->Level->TimeSeconds
		||	BaseLitDynActor->LastProjectorGeneration != GProjectorRenderInfoGeneration ) &&	// uc2 port (G-9): UnProjector.h
		RenderState.SceneNode->FilterMode != FSceneNode::Filter_PostProcess )
	{
		// need to update these		
		BaseLitDynActor->EmptyProjectors();	
		// set cur time since it is updating
		BaseLitDynActor->LastLightUpdateTime = BaseLitActor->Level->TimeSeconds;
		BaseLitDynActor->LastProjectorGeneration = GProjectorRenderInfoGeneration;

		if( !BaseLitActor->bUnlit && 
			!RenderActor->bUnlit && 
			BaseLitDynActor->NeedsLightUpdate )
		{			

			// need to update these		
			BaseLitDynActor->EmptyLights();

			// Offset the bounding sphere to account for root motion.
			FSphere	RealBoundingSphere( BaseLitDynActor->BoundingSphere );

			//scion jg -- Check for skeletal mesh so we can modify lighting by type
			UBOOL bSkeletalMesh = BaseLitActor->IsDTMesh() &&
				BaseLitActor->Mesh && BaseLitActor->Mesh->IsA(USkeletalMesh::StaticClass());

			if(bSkeletalMesh)
			{
				USkeletalMeshInstance*	MeshInstance = (USkeletalMeshInstance*)(BaseLitActor->Mesh->MeshGetInstance(BaseLitActor));
				checkSlow(MeshInstance);
				RealBoundingSphere += (MeshInstance->GetBoneCoords(0).Origin - BaseLitActor->Location);
			}
			else if(BaseLitActor->DrawType == DT_StaticMesh && BaseLitActor->bSpecialLit )
			{
				RealBoundingSphere.W *= 10.5f;
			}

			static FDynamicLight*	Consider[256];
			INT						NumConsider = 0;

			// scion sz - only do lighting calculations if this actor needs a lighting update
			if( BaseLitDynActor->NeedsLightUpdate )
			{
				FUNC_CLOCK(MeshLightCyc,GStats.DWORDStats(GEngineStats.STATS_Visibility_MeshLightCycles));

				UBOOL NoStaticLights = (BaseLitActor->DrawType == DT_StaticMesh) && 
					((BaseLitActor->bStatic && !BaseLitActor->bLightChanged) || 
					(Cast<AMover>(BaseLitActor) && !Cast<AMover>(BaseLitActor)->bDynamicLightMover));
				
				AActor*			LightActor=NULL;
				FDynamicLight*	DynamicLight=NULL;

				for(INT LeafIndex = 0;LeafIndex < BaseLitActor->Leaves.Num();LeafIndex++)
				{
					INT		iLeaf = BaseLitActor->Leaves(LeafIndex);			
					FLeaf&	Leaf = RenderState.Model->Leaves(iLeaf);

					// Find lights that affect this actor.					
					AActor**		StaticLightActors = Leaf.iPermeating >= 0 ? &RenderState.Model->Lights(Leaf.iPermeating) : NULL;
					FDynamicLight**	DynamicLightActors = RenderState.LeafLights[iLeaf];

					// Pass=0 add static lights 
					// Pass=1 add dynamic lights
					for(INT Pass = NoStaticLights;Pass < 2;Pass++)
					{
						if(	(Pass == 0 && Leaf.iPermeating < 0) || 
							(Pass == 1 && (!DynamicLightActors || !BaseLitActor->bUseDynamicLights || BaseLitActor->bSinglePassLighting)))
							continue;

						while( NumConsider < 256 )
						{
							if(Pass == 0)
							{
								LightActor = *StaticLightActors++;

								// scion sz - skip invalidated lights
								if( LightActor == (AActor*)FLeaf::INVALID_LIGHT )
									continue;

								if( LightActor && 
									!LightActor->bDeleteMe) 
								{
									DynamicLight = LightActor->GetLightRenderData();
								}
								else
									break;
							}
							else
							{
								DynamicLight = *DynamicLightActors++;

								if( DynamicLight && 
									!DynamicLight->Actor->bDeleteMe )
								{
									LightActor = DynamicLight->Actor;
								}
								else
									break;
							}

							if( DynamicLight && 
								!DynamicLight->IsUsed() )
							{
								// scion sz - flag with bAlreadyUsed instead of searching in Consider[]
								DynamicLight->MarkUsed();							

								//scion jg -- Skip this light if it is not set to apply to pawns or
								// skip it if it only affects skeletal and this is not skeletal
								if(bSkeletalMesh == 1 && LightActor->AffectType < AT_AffectSkeletal ||
									bSkeletalMesh == 0 && LightActor->AffectType == AT_AffectSkeletal)
									continue;

								if(LightActor->bSpecialLit != BaseLitActor->bSpecialLit)
									continue;

								FLOAT	DistanceSquared = (DynamicLight->Position - RealBoundingSphere).SizeSquared();

								if(LightActor->LightEffect != LE_Sunlight && DistanceSquared > Square(DynamicLight->Radius + RealBoundingSphere.W))
									continue;

								Consider[NumConsider++] = DynamicLight;							
							}
						}
					}
				}
				// scion sz - clear used flags
				FDynamicLight::UnMarkAllUsed();
			}

			if( NumConsider || 
				BaseLitActor->bSinglePassLighting )
			{			
				// get list of lights to use for rendering actor
				BaseLitDynActor->ActorLights = GetRelevantLights(RenderState.SceneNode,BaseLitDynActor,RealBoundingSphere,Consider,NumConsider);				
			}
		}

		// Find dynamic projectors that affect this actor.
		if( RenderActor->bAcceptsProjectors &&
			BaseLitActor->bAcceptsProjectors )
		{
			FUNC_CLOCK(ProjCyc,GStats.DWORDStats(GEngineStats.STATS_Projector_RenderCycles));

			for(INT LeafIndex = 0;LeafIndex < BaseLitActor->Leaves.Num();LeafIndex++)
			{
				INT		iLeaf = BaseLitActor->Leaves(LeafIndex);

				for(FProjectorRenderInfo** ProjectorList = RenderState.LeafProjectors[iLeaf];ProjectorList && *ProjectorList;ProjectorList++)
				{
					FProjectorRenderInfo*	ProjectorInfo = *ProjectorList;

					if(!ProjectorInfo->Projector->bProjectActor && BaseLitActor->Mesh)
						continue;

					if(!ProjectorInfo->Projector->bProjectStaticMesh && BaseLitActor->StaticMesh)
						continue;

					TList<FProjectorRenderInfo*>* ExistingProjectorList;
					for(ExistingProjectorList = BaseLitDynActor->ActorProjectors;ExistingProjectorList;ExistingProjectorList = ExistingProjectorList->Next)
						if(ExistingProjectorList->Element == ProjectorInfo)
							break;

					if(ExistingProjectorList)
						continue;

					UBOOL	Inside = 1;
					FVector	ActorCenter(BaseLitDynActor->BoundingBox.GetCenter());
					FVector ActorExtent(BaseLitDynActor->BoundingBox.GetExtent());

					for(INT PlaneIndex = 0;PlaneIndex < 6;PlaneIndex++)
					{
						FLOAT	PushOut = FBoxPushOut(ProjectorInfo->FrustumPlanes[PlaneIndex],ActorExtent),
							Dist = ProjectorInfo->FrustumPlanes[PlaneIndex].PlaneDot(ActorCenter);

						if(Dist < -PushOut)
						{
							Inside = 0;
							break;
						}
					}

					if(Inside) 
					{
						BaseLitDynActor->ActorProjectors = AddProjectorListEntry( ProjectorInfo, BaseLitDynActor->ActorProjectors );
					}
				}
			}
		}
	}

	// render actor
	if( !bSkipPushPop ) {
		RenderState.RI->PushState();
	}	
	RenderState.RI->SetCullMode(DynamicActor->Determinant * RenderState.SceneNode->Determinant < 0.0f ? CM_CCW : CM_CW);
	DynamicActor->Render( 
		RenderState.SceneNode,
		BaseLitDynActor->ActorLights,
		BaseLitDynActor->ActorProjectors,
		RenderState.RI
		);
	if( !bSkipPushPop ) {
		RenderState.RI->PopState();
	}

	unguardSlow;
}

/* scion ======================================================================
 * AddActorToDrawList
 * Author: jg
 * 
 * If the specified actor is a static mesh and it has more than one section or
 * the actor in questions is not a static mesh, it is placed into the
 * ActorDrawList like the old code did. If it has only one section, it is
 * sorted by material and mesh for fewer state changes.
 *
 * input:	DynamicActor -- The actor that is being added to the draw list
 *			RenderState -- The render state structure to add it to
 *
 * ============================================================================
 */
FORCEINLINE void AddActorToDrawList(FDynamicActor* DynamicActor,
	FRenderState& RenderState)
{
	// Check to see if this is a candidate for sorting
	if (DynamicActor->Actor->DrawType == DT_StaticMesh &&
		DynamicActor->Actor->StaticMesh &&
		DynamicActor->Actor->StaticMesh->Sections.Num() == 1)
	{
		// Get the material to sort by
		UMaterial* pMaterial = DynamicActor->Actor->StaticMesh->GetSkin(
			DynamicActor->Actor,0);
		UBOOL bFound = 0;
		// Set where to start searching from
		TList<FDynamicActor*>* DrawList = RenderState.SortedActorDrawList;
		// Search through the list until at the end or until a previous
		// actor with the same material and mesh are present
		while (bFound == 0 && DrawList != NULL)
		{
			// Move to the next in the list if this doesn't match
			if (DrawList->Element->Actor->StaticMesh->GetSkin(
				DrawList->Element->Actor,0) != pMaterial)
			{
				// Not a match so keep checking
				DrawList = DrawList->Next;
			}
			else
			{
				// We have a match
				bFound = 1;
				// Cache the static mesh pointer
				UStaticMesh* pMesh = DynamicActor->Actor->StaticMesh;
				// Now sort by mesh within the list of actors that have the
				// same material
				while (DrawList != NULL && DrawList->Next != NULL &&
					DrawList->Next->Element->Actor->StaticMesh->GetSkin(
					DrawList->Next->Element->Actor,0) == pMaterial)
				{
					// Make sure the mesh is the same
					if (DrawList->Element->Actor->StaticMesh != pMesh)
					{
						// Not a match so keep checking
						DrawList = DrawList->Next;
					}
					else
					{
						// Insert the item here so quit checking
						break;
					}
				}
			}
		}
		// See if there was a place to insert it at
		if (bFound)
		{
			// Insert between the two items
			DrawList->Next = new(GSceneMem)TList<FDynamicActor*>(DynamicActor,
				DrawList->Next);
		}
		else
		{
			// There was no corresponding actors with this material so insert
			// at the head of the list
			RenderState.SortedActorDrawList = new(GSceneMem)TList<FDynamicActor*>(
				DynamicActor,RenderState.SortedActorDrawList);
		}
	}
	else
	{
		// Add it to the list at the head
		RenderState.ActorDrawList = new(GSceneMem)TList<FDynamicActor*>(
			DynamicActor,RenderState.ActorDrawList);
	}
}

/* scion ======================================================================
 * AddNaliPawnToDrawList
 * Author: sz/jg
 * 
 * Batch all nali pawns to the same draw list sorted by material
 *
 * input:	DynamicActor -- The actor that is being added to the draw list
 *			RenderState -- The render state structure to add it to
 *
 * ============================================================================
 */
FORCEINLINE void AddNaliPawnToDrawList(FDynamicActor* DynamicActor,
	FRenderState& RenderState)
{
	checkSlow(DynamicActor->Actor->DrawType == DT_NaliPawn);
	// scion sz - special case for the Nali pawns
	if(  /*Cast<ULodMesh*>(DynamicActor->Actor->Mesh)*/
		DynamicActor->Actor->Mesh &&
		(DynamicActor->Actor->Skins.Num()==1 ||
		((ULodMesh*)DynamicActor->Actor->Mesh)->Materials.Num()==1) )
	{
		// Get the material to sort by
		UMaterial* pMaterial = ((ULodMesh*)DynamicActor->Actor->Mesh)->GetSkin(
			DynamicActor->Actor,0);
		UBOOL bFound = 0;
		// Set where to start searching from
		TList<FDynamicActor*>* DrawList = RenderState.NaliPawnActorDrawList;
		// Search through the list until at the end or until a previous
		// actor with the same material and mesh are present
		while (bFound == 0 && DrawList != NULL)
		{
			// Move to the next in the list if this doesn't match
			if (((ULodMesh*)DrawList->Element->Actor->Mesh)->GetSkin(
				DrawList->Element->Actor,0) != pMaterial)
			{
				// Not a match so keep checking
				DrawList = DrawList->Next;
			}
			else
			{
				// We have a match
				bFound = 1;				
			}
		}
		// See if there was a place to insert it at
		if (bFound)
		{
			// Insert between the two items
			DrawList->Next = new(GSceneMem)TList<FDynamicActor*>(DynamicActor,
				DrawList->Next);
		}
		else
		{
			// There was no corresponding actors with this material so insert
			// at the head of the list
			RenderState.NaliPawnActorDrawList = new(GSceneMem)TList<FDynamicActor*>(
				DynamicActor,RenderState.NaliPawnActorDrawList);
		}
	}
	else
	{
		// Add it to the list at the head
		RenderState.NaliPawnActorDrawList = new(GSceneMem)TList<FDynamicActor*>(
			DynamicActor,RenderState.NaliPawnActorDrawList);
	}
}

void UpdateAndAddAttachedActors( FDynamicActor* DynamicActor, 
	FRenderState& RenderState, UBOOL bDynamic );

/* scion ======================================================================
 * AddActorToDeferredDrawLists
 * Author: sz
 *
 * Add this actor and all of its attachments to deferred draw lists
 * Draw order should always be Solid,Emissives,Translucents(sorted),PostProcess
 * ============================================================================
 */
FORCEINLINE void AddActorToDeferredDrawLists(FDynamicActor* DynamicActor, 
	FRenderState& RenderState, UBOOL bDynamic )
{
	guardSlow(AddActorToDeferredDrawLists);

	// refresh sorting flags
	DynamicActor->UpdateSorting();

	// update any mesh attachments and add them to their draw lists 
	if( DynamicActor->Actor->IsDTMesh() &&
		DynamicActor->Actor->Mesh != NULL &&
		DynamicActor->Actor->Attached.Num() )
	{
		UpdateAndAddAttachedActors( DynamicActor, RenderState, bDynamic );
	}

	// Add to list of Emissive items. If Emissive then assume !PostPRocess && !Translucent
	if( DynamicActor->IsEmissive )
	{
		RenderState.EmissiveDrawList = new(GSceneMem) TList<FDynamicActor*>( DynamicActor, RenderState.EmissiveDrawList );
	}
	// Add to list of Translucent items.  These are sorted. Translucent BSP nodes are also in this list.
	else if( DynamicActor->IsTranslucent )
	{
		TList<FTranslucentDrawItem>**	DrawTail = &RenderState.TranslucentDrawList;
		FLOAT							DistanceSquared = (DynamicActor->BoundingSphere - RenderState.SceneNode->ViewOrigin).SizeSquared();

		while(*DrawTail)
		{
			if(!(*DrawTail)->Element.BSP && ((*DrawTail)->Element.DynamicActor->BoundingSphere - RenderState.SceneNode->ViewOrigin).SizeSquared() > DistanceSquared)
				DrawTail = &(*DrawTail)->Next;
			else if((*DrawTail)->Element.BSP)
			{
				FBspNode&	Node = RenderState.Model->Nodes((*DrawTail)->Element.iNode);

				if(Node.Plane.PlaneDot(DynamicActor->BoundingSphere) * Node.Plane.PlaneDot(RenderState.SceneNode->ViewOrigin) > 0.0f)
					DrawTail = &(*DrawTail)->Next;
				else
					break;
			}
			else
				break;
		};

		FTranslucentDrawItem	TranslucentActorItem;
		TranslucentActorItem.BSP = 0;
		TranslucentActorItem.DynamicActor = DynamicActor;

		*DrawTail = new(GSceneMem) TList<FTranslucentDrawItem>(TranslucentActorItem,*DrawTail);

		// if IsPostProcess && IsTranslucent then it is a blended distortion material
		if( DynamicActor->IsPostProcess > POSTPROCESS_NONE )
		{
			RenderState.PostProcessDrawList = new(GSceneMem) TList<FDynamicActor*>( DynamicActor, RenderState.PostProcessDrawList );
		}
	}
	 // Add to list of PostProcess items. Note that only emitters can be both PostProcess && Translucent
	else if( DynamicActor->IsPostProcess > POSTPROCESS_NONE)
	{
		RenderState.PostProcessDrawList = new(GSceneMem) TList<FDynamicActor*>( DynamicActor, RenderState.PostProcessDrawList );
	}
	else if( DynamicActor->Actor->DrawType == DT_StaticProjectorBatch )
	{	
		// scion sz - group static projector prims together
		RenderState.StaticProjectorBatches = new(GSceneMem) TList<FDynamicActor*>( DynamicActor, RenderState.StaticProjectorBatches );
	}
	else if( !bDynamic )
	{
		//scion jg -- Add the actor to the sorted list
		// or to the normal list
		AddActorToDrawList( DynamicActor, RenderState );
	}
	else if( DynamicActor->Actor->DrawType == DT_NaliPawn )
	{
		// scion sz - batch these together as a special case
        AddNaliPawnToDrawList( DynamicActor, RenderState );
	}
	else
	{
		// default list for all solids
		RenderState.ActorDrawList = new(GSceneMem) TList<FDynamicActor*>( DynamicActor, RenderState.ActorDrawList );				
	}

	unguardSlow;
}

/* scion ======================================================================
 * UpdateAndAddAttachedActor
 * Author: sz
 * 
 * Iterate over each skeletal mesh attachment and add them to the 
 * global deferred draw lists.
 * ============================================================================
 */
 void UpdateAndAddAttachedActors( FDynamicActor* DynamicActor, 
	 FRenderState& RenderState, UBOOL bDynamic )
 {
	 guardSlow(UpdateAndAddAttachedActors);
	 checkSlow(DynamicActor && DynamicActor->Actor);

	 USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(DynamicActor->Actor->Mesh);
	 if( SkelMesh )
	 {
		 for( INT Idx=0; Idx < DynamicActor->Actor->Attached.Num(); Idx++ )
		 {
			 AActor* AttachedActor = DynamicActor->Actor->Attached(Idx);
			 if ( AttachedActor && !AttachedActor->bDeleteMe )
			 {
				 // check for bHidden,etc
				 UBOOL bShouldDrawAttachment = RenderState.SceneNode->FilterAttachment( AttachedActor );
				 if( bShouldDrawAttachment ) 
				 {
					 bShouldDrawAttachment = RenderState.SceneNode->FilterActor( AttachedActor, 1 );
				 }
				 // if attached to a bone and not already updated
				 if( bShouldDrawAttachment &&
					 AttachedActor->AttachmentBone != NAME_None &&
					 AttachedActor->bForceUpdateWhenAttached == 0 )
				 {
					 // update location
					 bShouldDrawAttachment = SkelMesh->SetAttachmentLocation( DynamicActor->Actor, AttachedActor );					 
				 }
				 // if valid bone attachment or just a valid attachment
				 if( bShouldDrawAttachment )	
				 {
					 // refresh 
					 AttachedActor->ClearRenderData();
					 // get render data (may update if necessary)
					 FDynamicActor* AttachedDynActor = AttachedActor->GetActorRenderData();
					 // recurse on attached actor to add it to one of the deferred lists
					 AddActorToDeferredDrawLists( AttachedDynActor, RenderState, bDynamic );
				 }
			 }
		 }
	 }

	 unguardSlow;
 }

//
//  ProcessLeaf - Process the contents of a leaf.
//
static void ProcessLeaf(FRenderState& RenderState,INT iLeaf)
{
	guard(ProcessLeaf);

	INT		iZone = RenderState.SceneNode->ViewZone ? RenderState.Model->Leaves(iLeaf).iZone : 0;

	if(iZone != RenderState.SceneNode->InvisibleZone)
	{
		if (GIsEditor)
		{
			// Add anti-portals in this leaf to the list of visibility volumes.

			for(TList<FDynamicActor*>* ActorList = RenderState.LeafActors[iLeaf];ActorList;ActorList = ActorList->Next)
			{
				FDynamicActor*	DynamicActor = ActorList->Element;

				if(DynamicActor->Actor->DrawType == DT_AntiPortal && DynamicActor->Actor->AntiPortal && (RenderState.SceneNode->Viewport->Actor->ShowFlags & SHOW_PlayerCtrl))
				{
					if(DynamicActor->VisibilityTag != CurrentVisibilityTag && 
						RenderState.Zones[iZone].Visible(DynamicActor->BoundingBox) && 
						CheckCullDistance(RenderState.SceneNode->ViewOrigin,DynamicActor,RenderState.SceneNode->Viewport))
					{
						FConvexVolume*	AntiPortalVolume = ExtrudeAntiPortal(RenderState.SceneNode->ViewOrigin,DynamicActor);
						INT				Zones[4] = { -1, -1, -1, -1 },
										NumZones = 0;

						for(INT LeafIndex = 0;LeafIndex < DynamicActor->Actor->Leaves.Num();LeafIndex++)
						{
							INT	ZoneIndex = RenderState.Model->Leaves(DynamicActor->Actor->Leaves(LeafIndex)).iZone,
								OtherZoneIndex = 0;

							for(OtherZoneIndex = 0;OtherZoneIndex < NumZones;OtherZoneIndex++)
								if(Zones[OtherZoneIndex] == ZoneIndex)
									break;

							if(OtherZoneIndex < 4 && Zones[OtherZoneIndex] != ZoneIndex)
							{
								RenderState.Zones[ZoneIndex].AntiPortals = new(GSceneMem) TList<FConvexVolume*>(AntiPortalVolume,RenderState.Zones[ZoneIndex].AntiPortals);
								Zones[OtherZoneIndex] = ZoneIndex;
							}
						}

						DynamicActor->VisibilityTag = CurrentVisibilityTag;
					}
				}
			}
		}
		else
		{
			// Render static actors in this leaf.

			FLeafRenderInfo&	LeafRenderInfo = RenderState.Level->LeafRenderInfo(iLeaf);

			for(INT ActorIndex = 0;ActorIndex < LeafRenderInfo.RenderActors.Num();ActorIndex++)
			{
				AActor*	Actor = LeafRenderInfo.RenderActors(ActorIndex);

				// Update the cached filter state for the static actor.

				if(Actor->StaticFilterState == FS_Maybe)
				{
					if(RenderState.SceneNode->FilterActor(Actor))
						Actor->StaticFilterState = FS_Yes;
					else
						Actor->StaticFilterState = FS_No;
				}

				if(Actor->StaticFilterState == FS_Yes)
				{
					FDynamicActor*	DynamicActor = Actor->GetActorRenderData();
					if (DynamicActor->VisibilityTag != CurrentVisibilityTag &&
						(RenderState.Zones[iZone].Visible(DynamicActor->BoundingBox) &&
						CheckCullDistance(RenderState.SceneNode->ViewOrigin,DynamicActor,RenderState.SceneNode->Viewport)) ||
						RenderState.SceneNode->Viewport->Precaching)
					{
						//jg -- Check for a hardware occluded actor
						if (RenderState.SceneNode->CanUseHardwareOcclusion() == 0 ||
							DynamicActor->IsVisible(RenderState.SceneNode->Viewport))
						{
							// scion sz - add actors to their corresponding draw lists
							AddActorToDeferredDrawLists( DynamicActor, RenderState, 0 );

							if( RenderState.Client->Coronas
							&&	DynamicActor->Actor->bCorona 
							&&  (FDistSquared(RenderState.SceneNode->ViewOrigin, DynamicActor->Actor->Location) < Square(DynamicActor->Actor->WorldLightRadius()) || DynamicActor->Actor->LightRadius == 0 )
							&&  RenderState.Zones[iZone].Visible(FBox(DynamicActor->Actor->Location,DynamicActor->Actor->Location)) 
							&&  CheckCullDistance(RenderState.SceneNode->ViewOrigin,DynamicActor,RenderState.SceneNode->Viewport)   
							)
							{
								UC2CoronaRenderFor( RenderState.SceneNode->Viewport ).AddVisibleLight( DynamicActor->Actor );
							}
						}
						DynamicActor->VisibilityTag = CurrentVisibilityTag;
						//jg -- Add to the hardware occlusion list
						HWOcclusionManager.AddQuery(RenderState.SceneNode,DynamicActor);
					}
				}
	 			else
	 				LeafRenderInfo.RenderActors.Remove(ActorIndex);
			}
		}

		// Render dynamic actors in this leaf.

		for(TList<FDynamicActor*>* ActorList = RenderState.LeafActors[iLeaf];ActorList;ActorList = ActorList->Next)
		{
			FDynamicActor*	DynamicActor = ActorList->Element;
  			if (DynamicActor->VisibilityTag != CurrentVisibilityTag &&
				(RenderState.Zones[iZone].Visible(DynamicActor->BoundingBox) && (CheckCullDistance(RenderState.SceneNode->ViewOrigin,DynamicActor,RenderState.SceneNode->Viewport)) || RenderState.SceneNode->Viewport->Precaching) &&
				(DynamicActor->Actor->DrawType != DT_AntiPortal || !(RenderState.SceneNode->Viewport->Actor->ShowFlags & SHOW_PlayerCtrl)))
			{
				//jg -- Check for a hardware occluded actor
				if (RenderState.SceneNode->CanUseHardwareOcclusion() == 0 ||
					DynamicActor->IsVisible(RenderState.SceneNode->Viewport))
				{
					// scion sz - add actors to their corresponding draw lists
					AddActorToDeferredDrawLists( DynamicActor, RenderState, 1 );					

					if( RenderState.Client->Coronas
					&&	DynamicActor->Actor->bCorona 
					&&  (FDistSquared(RenderState.SceneNode->ViewOrigin, DynamicActor->Actor->Location) < Square(DynamicActor->Actor->WorldLightRadius()) || DynamicActor->Actor->LightRadius == 0 )
					&&  RenderState.Zones[iZone].Visible(FBox(DynamicActor->Actor->Location,DynamicActor->Actor->Location)) 
					&&  CheckCullDistance(RenderState.SceneNode->ViewOrigin,DynamicActor,RenderState.SceneNode->Viewport)   
					)
					{
						UC2CoronaRenderFor( RenderState.SceneNode->Viewport ).AddVisibleLight( DynamicActor->Actor );
					}
				}
				DynamicActor->VisibilityTag = CurrentVisibilityTag;
				//jg -- Add to the hardware occlusion list
				HWOcclusionManager.AddQuery(RenderState.SceneNode,DynamicActor);
			}
		}
	}

	unguard;
}

//
//  ExtrudePolygon - Extrudes a polygon into a convex volume.
//

static FConvexVolume* ExtrudePolygon(FRenderState& RenderState,FPoly& Polygon,UBOOL FarClip = 1)
{
	FConvexVolume*	Volume = new(GSceneMem) FConvexVolume;
	FPlane			PolygonPlane = FPlane(Polygon.Vertex[0],Polygon.Normal);

	Volume->BoundingPlanes[Volume->NumPlanes++] = PolygonPlane;

	for(INT EdgeIndex = 0;EdgeIndex < Polygon.NumVertices && Volume->NumPlanes < FConvexVolume::MAX_VOLUME_PLANES - 1;EdgeIndex++)
	{
		FPlane	SidePlane = FPlane(RenderState.SceneNode->ViewOrigin,Polygon.Vertex[EdgeIndex],Polygon.Vertex[(EdgeIndex + 1) % Polygon.NumVertices]);

		Volume->BoundingPlanes[Volume->NumPlanes++] = SidePlane;
	}

	if(FarClip)
	{
		AZoneInfo*	ViewZoneInfo = RenderState.Level->GetZoneActor(RenderState.SceneNode->ViewZone);

		if(ViewZoneInfo->bDistanceFog && (RenderState.SceneNode->Viewport->Actor->ShowFlags & SHOW_DistanceFog))
		{
			FLOAT		FarClip = ViewZoneInfo->DistanceFogEnd;
			FVector		Z = (RenderState.SceneNode->Deproject(FPlane(0,0,0,NEAR_CLIPPING_PLANE)) - RenderState.SceneNode->ViewOrigin).SafeNormal();

			Volume->BoundingPlanes[Volume->NumPlanes++] = FPlane(RenderState.SceneNode->ViewOrigin + Z * FarClip,Z);
		}
	}

	return Volume;
}

//
//  ProcessNode - Render a node and it's coplanars.
//

static void ProcessNode(FRenderState& RenderState,INT iNode,FDynamicLight** DynamicLights,INT NumDynamicLights,FProjectorRenderInfo** DynamicProjectors,INT NumDynamicProjectors)
{
	guard(ProcessNode);

	FBspNode&	Node = RenderState.Model->Nodes(iNode);
	FBspSurf&	Surf = RenderState.Model->Surfs(Node.iSurf);
	FLOAT		PlaneDot = Node.Plane.PlaneDot(RenderState.SceneNode->ViewOrigin);
	INT			IsFront = PlaneDot > 0.0f;

	// Backface cull.

	if(IsFront || (Surf.PolyFlags & (PF_TwoSided | PF_Portal)) || !RenderState.SceneNode->ViewZone)
	{
		// Test for visibility with view frustum/portal volumes.

		if(RenderState.Zones[RenderState.SceneNode->ViewZone ? Node.iZone[IsFront] : 0].Visible(Node.ExclusiveSphereBound))
		{
			DWORD	StencilMask = 0;
			UBOOL	RenderNode = 0;

			if(Surf.PolyFlags & PF_FakeBackdrop)
			{
				if(!(Surf.PolyFlags & PF_Invisible))
					RenderNode = 1;

				if((RenderState.SceneNode->Viewport->Actor->ShowFlags & SHOW_Backdrop) && RenderState.SceneNode->Recursion < MAX_RECURSION_DEPTH - 1 && RenderState.SceneNode->Viewport->NextStencilMask)
				{
					// Find the corresponding sky zone.

					ASkyZoneInfo*	SkyZoneInfo = NULL;

					if(GIsEditor && RenderState.SceneNode->Viewport->IsPerspective())
					{
						for(INT ZoneIndex = 0;ZoneIndex < RenderState.Model->NumZones;ZoneIndex++)
							if(Cast<ASkyZoneInfo>(RenderState.Level->GetZoneActor(ZoneIndex)))
								SkyZoneInfo = Cast<ASkyZoneInfo>(RenderState.Level->GetZoneActor(ZoneIndex));
					}
					else
						SkyZoneInfo = RenderState.Level->GetZoneActor(Node.iZone[IsFront])->SkyZone;

					AZoneInfo* TestZone = RenderState.Level->GetZoneActor(RenderState.SceneNode->ViewZone);
					if( (SkyZoneInfo) && (!TestZone->bLonelyZone) )
					{
						SkyZoneInfo->LastRenderTime = SkyZoneInfo->Level->TimeSeconds;

						// Create a child scene node for the sky box.
						FSkySceneNode*	SkySceneNode = NULL;

						for(TList<FLevelSceneNode*>* Child = RenderState.ChildSceneNodes;Child;Child = Child->Next)
						{
							if(Child->Element->ViewZone == SkyZoneInfo->Region.ZoneNumber)
								SkySceneNode = (FSkySceneNode*) Child->Element;
						}

						if(!SkySceneNode)
						{
							SkySceneNode = NEW FSkySceneNode(RenderState.SceneNode,SkyZoneInfo->Region.ZoneNumber);
							RenderState.ChildSceneNodes = new(GSceneMem) TList<FLevelSceneNode*>(SkySceneNode,RenderState.ChildSceneNodes);
						}

						RenderState.SkyStencilMask = SkySceneNode->StencilMask;
						RenderNode = 0;
					}
				}
			}
			else if(Surf.PolyFlags & PF_Mirrored)
			{
				RenderNode = 1;

				if((!GIsEditor || (RenderState.SceneNode->Viewport->Actor->ShowFlags & SHOW_PlayerCtrl)) && RenderState.SceneNode->Recursion < MAX_RECURSION_DEPTH - 1 && RenderState.SceneNode->Viewport->NextStencilMask)
				{
					FMirrorSceneNode*	MirrorSceneNode = NULL;

					for(TList<FLevelSceneNode*>* Child = RenderState.ChildSceneNodes;Child;Child = Child->Next)
					{
						FMirrorSceneNode*	OtherMirrorSceneNode = Child->Element->GetMirrorSceneNode();

						if(OtherMirrorSceneNode && OtherMirrorSceneNode->MirrorSurface == Node.iSurf && OtherMirrorSceneNode->ViewZone == Node.iZone[IsFront])
							MirrorSceneNode = OtherMirrorSceneNode;
					}

					if(!MirrorSceneNode)
					{
						MirrorSceneNode = NEW FMirrorSceneNode(RenderState.SceneNode,Node.Plane,Node.iZone[IsFront],Node.iSurf);
						RenderState.ChildSceneNodes = new(GSceneMem) TList<FLevelSceneNode*>(MirrorSceneNode,RenderState.ChildSceneNodes);
					}

					StencilMask = MirrorSceneNode->StencilMask;
				}
			}
			else if(RenderState.SceneNode->ViewZone && (Surf.PolyFlags & PF_Portal))
			{
				RenderNode = 1;

				if(!GIsEditor || (RenderState.SceneNode->Viewport->Actor->ShowFlags & SHOW_PlayerCtrl))
				{
					if(Surf.Actor && Surf.Actor->Brush && Surf.Actor->Brush->Polys)
					{
						if(!(RenderState.RenderedPortals[Node.iSurf >> 3] & (1 << (Node.iSurf & 7))) && Node.iZone[0] != Node.iZone[1])
						{
							// Build a FPoly containing the portal's vertices.

							FPoly	Polygon = Surf.Actor->Brush->Polys->Element(Surf.iBrushPoly);
							INT		iOppositeZone = Node.iZone[1 - IsFront];

							FMatrix&	LocalToWorld = Surf.Actor->GetActorRenderData()->LocalToWorld;

							Polygon.Normal = LocalToWorld.TransformNormal(Polygon.Normal);

							for(INT VertexIndex = 0;VertexIndex < Polygon.NumVertices;VertexIndex++)
								Polygon.Vertex[VertexIndex] = LocalToWorld.TransformFVector(Polygon.Vertex[VertexIndex]);

							if(!IsFront)
								Polygon.Reverse();

							// Quick cull out for Lonely Zones

							AZoneInfo* TestZone = RenderState.Level->GetZoneActor(iOppositeZone);
							AZoneInfo* ViewZone = RenderState.Level->GetZoneActor(RenderState.SceneNode->ViewZone);

							if ( (!TestZone->bLonelyZone) && (!ViewZone->bLonelyZone) )
							{

								UBOOL	Clipped = 0;

								// Test for manual excludes

								for (int indx=0;indx<ViewZone->ManualExcludes.Num();indx++)
								{
										if (TestZone == ViewZone->ManualExcludes(indx) )
										{
											Clipped = 1;
											break;
										}
								}

								if (!Clipped)
								{
								    // Add the portal to the visibility volumes for the opposite zone.
    
								    FLOAT	Area = Polygon.Area();
    
								    for(TList<FConvexVolume*>* AntiPortal = RenderState.Zones[Node.iZone[IsFront]].AntiPortals;AntiPortal;AntiPortal = AntiPortal->Next)
								    {
									    FPoly	ClippedPolygon = AntiPortal->Element->ClipPolygon(Polygon);
									    FLOAT	ClippedArea = ClippedPolygon.Area();
    
									    if(ClippedArea >= Area)
									    {
										    Clipped = 1;
										    break;
									    }
									    else if(ClippedArea > 0.0f)
										    RenderState.Zones[iOppositeZone].AntiPortals = new(GSceneMem) TList<FConvexVolume*>(AntiPortal->Element,RenderState.Zones[iOppositeZone].AntiPortals);
								    }
    
								    if(!Clipped)
								    {
									    for(TList<FConvexVolume*>* Portal = RenderState.Zones[Node.iZone[IsFront]].Portals;Portal;Portal = Portal->Next)
									    {
										    if(PlaneDot > 1.0f || PlaneDot < -1.0f)
										    {
										        FPoly	ClippedPolygon = Portal->Element->ClipPolygon(Polygon);
        
										        if(ClippedPolygon.NumVertices)
										        {
											        FConvexVolume*	PortalVolume = ExtrudePolygon(RenderState,ClippedPolygon);
        
											        RenderState.Zones[iOppositeZone].Portals = new(GSceneMem) TList<FConvexVolume*>(PortalVolume,RenderState.Zones[iOppositeZone].Portals);
										        }
										    }
										    else
										    {
											    // The viewer is very close to the portal, simply use the view frustums for the current zone as the visibility volume.
    
											    FConvexVolume*	PortalVolume = Portal->Element;
    
											    RenderState.Zones[iOppositeZone].Portals = new(GSceneMem) TList<FConvexVolume*>(PortalVolume,RenderState.Zones[iOppositeZone].Portals);
										    }
									    }
    
									    QWORD	OppositeZoneMask = (QWORD) 1 << Node.iZone[1 - IsFront];
    
									    if(!(RenderState.ActiveZoneMask & OppositeZoneMask))
									    {
										    RenderState.ActiveZones = new(GSceneMem) TList<INT>(iOppositeZone,RenderState.ActiveZones);
										    RenderState.ActiveZoneMask |= OppositeZoneMask;
    
										    if(!GIsEditor)
											    BuildAntiPortals(RenderState,iOppositeZone);
									    }
								    }
								}
							}

							RenderNode = 0;

							RenderState.RenderedPortals[Node.iSurf >> 3] |= (1 << (Node.iSurf & 7));
						}
						else
							RenderNode = 0;
					}
				}

				if(!(Surf.PolyFlags & PF_Invisible))
					RenderNode = 1;
			}
			else if(Surf.PolyFlags & PF_AntiPortal)
			{
				if(!GIsEditor || (RenderState.SceneNode->Viewport->Actor->ShowFlags & SHOW_PlayerCtrl))
				{
					// Add the antiportal to the visibility volumes for the antiportal's zone.

					if(Surf.Actor && Surf.Actor->Brush && Surf.Actor->Brush->Polys)
					{
						if(!(RenderState.RenderedPortals[Node.iSurf >> 3] & (1 << (Node.iSurf & 7))))
						{
							FPoly	Polygon = Surf.Actor->Brush->Polys->Element(Surf.iBrushPoly);
							FMatrix&	LocalToWorld = Surf.Actor->GetActorRenderData()->LocalToWorld;

							Polygon.Normal = LocalToWorld.TransformNormal(Polygon.Normal);

							for(INT VertexIndex = 0;VertexIndex < Polygon.NumVertices;VertexIndex++)
								Polygon.Vertex[VertexIndex] = LocalToWorld.TransformFVector(Polygon.Vertex[VertexIndex]);

							if(((RenderState.SceneNode->ViewOrigin - Polygon.Vertex[0]) | Polygon.Normal) < 0.0f)
								Polygon.Reverse();

							FConvexVolume*	AntiPortalVolume = ExtrudePolygon(RenderState,Polygon,0);

							RenderState.Zones[Node.iZone[IsFront]].AntiPortals = new(GSceneMem) TList<FConvexVolume*>(AntiPortalVolume,RenderState.Zones[Node.iZone[IsFront]].AntiPortals);

							RenderState.RenderedPortals[Node.iSurf >> 3] |= (1 << (Node.iSurf & 7));
						}
					}

					if(!(Surf.PolyFlags & PF_Invisible))
						RenderNode = 1;
				}
				else
					RenderNode = 1;
			}
			else if(!(Surf.PolyFlags & PF_Invisible) || !(RenderState.SceneNode->Viewport->Actor->ShowFlags & SHOW_PlayerCtrl))
				RenderNode = 1;

			if(RenderNode && Node.iZone[IsFront] != RenderState.SceneNode->InvisibleZone && Node.iSection != INDEX_NONE && (RenderState.SceneNode->Viewport->Actor->ShowFlags & SHOW_BSP))
			{
				if(StencilMask)
				{
					// Add the node to a stencil draw list.

					FBspStencilDrawList*	StencilDrawList = NULL;

					for(TList<FBspStencilDrawList*>* List = RenderState.StencilDrawLists;List;List = List->Next)
					{
						if(List->Element->StencilMask == StencilMask && List->Element->SectionIndex == Node.iSection)
						{
							StencilDrawList = List->Element;
							break;
						}
					}

					if(!StencilDrawList)
					{
						StencilDrawList = new(GSceneMem) FBspStencilDrawList(RenderState.Model,Node.iSection,StencilMask);
						RenderState.StencilDrawLists = new(GSceneMem) TList<FBspStencilDrawList*>(StencilDrawList,RenderState.StencilDrawLists);
					}

					StencilDrawList->AddNode(iNode);
				}

				if(!(Surf.PolyFlags & PF_Mirrored) || !(Surf.PolyFlags & PF_Invisible))
				{
					if(Surf.Material && Surf.Material->RequiresSorting())
					{
						// Add the node to the translucent draw list.

						FTranslucentDrawItem	TranslucentNodeItem;
						TranslucentNodeItem.BSP = 1;
						TranslucentNodeItem.iNode = iNode;
						TranslucentNodeItem.NumDynamicLights = NumDynamicLights;
						TranslucentNodeItem.NumDynamicProjectors = NumDynamicProjectors;

						if(NumDynamicLights)
						{
							TranslucentNodeItem.DynamicLights = New<FDynamicLight*>(GSceneMem,NumDynamicLights);
							appMemcpy(TranslucentNodeItem.DynamicLights,DynamicLights,NumDynamicLights * sizeof(FDynamicLight*));
						}
						else
							TranslucentNodeItem.DynamicLights = NULL;

						if(NumDynamicProjectors)
						{
							TranslucentNodeItem.DynamicProjectors = New<FProjectorRenderInfo*>(GSceneMem,NumDynamicProjectors);
							appMemcpy(TranslucentNodeItem.DynamicProjectors,DynamicProjectors,NumDynamicProjectors * sizeof(FProjectorRenderInfo*));
						}
						else
							TranslucentNodeItem.DynamicProjectors = NULL;

						RenderState.TranslucentDrawList = new(GSceneMem) TList<FTranslucentDrawItem>(TranslucentNodeItem,RenderState.TranslucentDrawList);
					}
					else
					{
						// Add the node to it's section's draw list.

						if(!RenderState.BspDrawLists[Node.iSection])
						{
							RenderState.BspDrawLists[Node.iSection] = new(GSceneMem) FBspDrawList(RenderState.Model,Node.iSection);
							RenderState.SectionDrawList = new(GSceneMem) TList<INT>(Node.iSection,RenderState.SectionDrawList);
						}

						RenderState.BspDrawLists[Node.iSection]->AddNode(iNode,DynamicLights,NumDynamicLights,DynamicProjectors,NumDynamicProjectors,RenderState.SceneNode);

						if(Node.iLightMap != INDEX_NONE && (UPDATE_CHANGED_LIGHTMAPS || UPDATE_DYNAMIC_LIGHTMAPS))
						{
							FLightMap*	LightMap = &RenderState.Model->LightMaps(Node.iLightMap);

							for(INT LightIndex = 0;LightIndex < NumDynamicLights;LightIndex++)
							{
								FDynamicLight*	DynamicLight = DynamicLights[LightIndex];

								if((DynamicLight->Changed && UPDATE_CHANGED_LIGHTMAPS) || (DynamicLight->Dynamic && UPDATE_DYNAMIC_LIGHTMAPS))
								{
									UBOOL	Update = 1;

									for(INT LightIndex = 0;LightIndex < LightMap->DynamicLights.Num();LightIndex++)
										if(LightMap->DynamicLights(LightIndex) == DynamicLight->Actor)
										{
											Update = 0;
											break;
										}

									if(Update)
									{
										if(!LightMap->DynamicLights.Num())
											RenderState.Model->DynamicLightMaps.AddItem(Node.iLightMap);

										LightMap->DynamicLights.AddItem(DynamicLight->Actor);

										LightMap->Revision++;
										RenderState.Model->LightMapTextures(LightMap->iTexture).Revision++;
									}
								}
							}
						}
					}
				}
			}
		}
	}

	if(Node.iPlane != INDEX_NONE)
		ProcessNode(RenderState,Node.iPlane,DynamicLights,NumDynamicLights,DynamicProjectors,NumDynamicProjectors);

	unguard;
}

//
//  RenderLevel
//

static FRenderState*			RootRenderState = NULL;
static FDynamicLight**			RootDynamicLights = NULL;
static FProjectorRenderInfo**	RootDynamicProjectors = NULL;
static INT						RootNumDynamicLights,
								RootNumDynamicProjectors;

#ifdef WITH_LIPSINC
	// The 'Talker' list.
	extern TLIPSincTalkerList GLIPSincTalkerList;
#endif

void RenderLevel(FLevelSceneNode* SceneNode,FRenderInterface* RI)
{
	guard(RenderLevel);	

	ULevel*	Level = SceneNode->Level;
	UModel*	Model = SceneNode->Model;

#ifndef FINAL_RELEASE
	INT	InitialByteCount = GSceneMem.GetByteCount();
#endif

	LOCAL_CLOCK(SetupTime);
	// Setup the render state.

	FRenderState	RenderState;

	RenderState.SceneNode = SceneNode;
	RenderState.RI = RI;

	RenderState.Level = Level;
	RenderState.Model = Model;
	RenderState.Client		= SceneNode->Viewport->GetOuterUClient();

	SceneNode->FilterMode = FSceneNode::Filter_All;

	CurrentVisibilityTag++;

	if(!CurrentVisibilityTag)
	{
		// The visibility tag wrapped around, reset all actor's visibility tags.

		CurrentVisibilityTag++;

		for(INT ActorIndex = 0;ActorIndex < Level->Actors.Num();ActorIndex++)
		{
			AActor*	Actor = Level->Actors(ActorIndex);

			if(Actor && Actor->ActorRenderData)
				Actor->ActorRenderData->VisibilityTag = 0;
		}
	}

	// Corona initialization.

	if(!SceneNode->Parent)
	{
		FCoronaRender& CoronaRender = UC2CoronaRenderFor( SceneNode->Viewport );	// uc2 port (G-9)
		CoronaRender.iVisible	= 0;
		appMemzero(CoronaRender.VisibleLights, sizeof(CoronaRender.VisibleLights));
	}

	// Initialize temporary memory stack.

	FMemMark	MemMark(GSceneMem);

	// Set the view and projection matrices.
	if( SceneNode->Viewport->Actor->UseFixedVisibility )
		RI->SetTransform(TT_WorldToCamera,SceneNode->Viewport->Actor->RenderWorldToCamera);
	else
		RI->SetTransform(TT_WorldToCamera,SceneNode->WorldToCamera);
	RI->SetTransform(TT_CameraToScreen,SceneNode->CameraToScreen);

	// Set the cull mode.		

	RI->SetCullMode(SceneNode->Determinant < 0.0f ? CM_CCW : CM_CW);

	// Setup actors for rendering.

	FDynamicLight*			DynamicLights[256];
	FProjectorRenderInfo*	DynamicProjectors[256];
	INT						NumDynamicLights = 0,
							NumDynamicProjectors = 0;
	GProjectorBatchList = NULL;

#ifdef WITH_LIPSINC	
	// Force an update on all currently 'talking' skeletal mesh instances, even if they are not
	// currently visible.
	GLIPSincTalkerList.ForceUpdate();
#endif

	if(!RootRenderState)
	{
		// Initialize lists.

		RenderState.OutsideActors	= NULL;
		RenderState.LeafActors		= NewZeroed<TList<FDynamicActor*>*>(GSceneMem,Model->Leaves.Num());

		// Iterate through all actors, except for the level info and builder brush.
		UBOOL	IsLit = SceneNode->Viewport->IsLit(),
				IsDynamicLit = !SceneNode->Viewport->GetOuterUClient()->NoDynamicLights;

		INT ActorIndex;

		if(GIsEditor)
			ActorIndex = 2;
		else
			ActorIndex = Level->iFirstDynamicActor;

		for(;ActorIndex < Level->Actors.Num();ActorIndex++)
		{
			AActor*	Actor = Level->Actors(ActorIndex);

			if(Actor && (!Actor->bStatic || GIsEditor))
			{
	//WD: added "#if __INTEL__" preprocessor definition check
	//rcg: changed to "#if __HAS_SSE__"
	//vogel: actually is slower on XBox.
#ifndef XBOX
#if __HAS_SSE__
				//WD: added prefetching (the prefetch distance is based on the 128-byte L2 cache lines of the Intel(r) Pentium(r) 4 Processor).
				//WD: **NOTE**: the order of prefetching is directly dependent on the data layout in EngineClasses.h (defined by Actor.uc).
				if( GIsSSE ) //!!vogel: GIsPentium4ProcessorBased)
				{
					_mm_prefetch((char*)&Actor->LightBrightness, _MM_HINT_NTA);       // pull the first cache-line of Actor into cache hierarchy
	//				_mm_prefetch((char*)(&Actor->LightBrightness+128), _MM_HINT_NTA); // pull the second cache-line of Actor into cache hierarchy
				}
#endif
#endif
				if(SceneNode->FilterActor(Actor))
				{
					FDynamicActor* DynamicActor = Actor->GetActorRenderData();

					// Add the actor to LeafActors for leaves it is in.
					if(SceneNode->ViewZone && Actor->Leaves.Num())
					{
						for(INT LeafIndex = 0;LeafIndex < Actor->Leaves.Num();LeafIndex++)
							RenderState.LeafActors[Actor->Leaves(LeafIndex)] = new(GSceneMem) TList<FDynamicActor*>(DynamicActor,RenderState.LeafActors[Actor->Leaves(LeafIndex)]);
					}
					else if(!SceneNode->ViewZone)
						RenderState.OutsideActors = new(GSceneMem) TList<FDynamicActor*>(DynamicActor,RenderState.OutsideActors);
				}

				if(IsLit && Actor->LightType != LT_None)
				{
					// Ensure the cached FDynamicLight is up to date.
					FDynamicLight*	CachedLight = Actor->GetLightRenderData();

					if(!SceneNode->Parent && (((Actor->bDynamicLight || CachedLight->Dynamic) || ((Actor->bLightChanged || Actor->bDeleteMe) || CachedLight->Changed))))
						CachedLight->Update();

					if((CachedLight->Dynamic || CachedLight->Changed) && NumDynamicLights < 256 && IsDynamicLit)
						DynamicLights[NumDynamicLights++] = CachedLight;
				}
			}
		}

		// Find dynamic projectors we need to render.

		for(INT ProjectorIndex = 0;ProjectorIndex < Level->DynamicProjectors.Num();ProjectorIndex++)
		{
			FProjectorRenderInfo*	ProjectorInfo = Level->DynamicProjectors(ProjectorIndex);

			if(!ProjectorInfo->Render( Level->GetLevelInfo()->TimeSeconds ))
			{
				// Replace this projector with the last dynamic projector, and remove an item from the end of the array.
				Level->DynamicProjectors(ProjectorIndex--) = Level->DynamicProjectors(Level->DynamicProjectors.Num() - 1);
				Level->DynamicProjectors.Remove(Level->DynamicProjectors.Num() - 1);
				continue;
			}

			check(ProjectorInfo->Projector); // There should never be a dynamic projector without an attached actor!

			if(SceneNode->FilterProjector(ProjectorInfo->Projector))
			{
				DynamicProjectors[NumDynamicProjectors++] = ProjectorInfo;

				if(NumDynamicProjectors == 256)
					break;
			}
		}

		// Clear dynamic light lists.

		if(!SceneNode->Parent && IsLit)
		{
			for(INT LightMapIndex = 0;LightMapIndex < Model->DynamicLightMaps.Num();LightMapIndex++)
			{
				FLightMap*	LightMap = &Model->LightMaps(Model->DynamicLightMaps(LightMapIndex));

				LightMap->DynamicLights.Empty();

				LightMap->Revision++;
				Model->LightMapTextures(LightMap->iTexture).Revision++;
			}

			Model->DynamicLightMaps.Empty();
		}

		RootRenderState = &RenderState;
		RootDynamicLights = DynamicLights;
		RootDynamicProjectors = DynamicProjectors;
		RootNumDynamicLights = NumDynamicLights;
		RootNumDynamicProjectors = NumDynamicProjectors;
	}
	else
	{
		RenderState.LeafActors = RootRenderState->LeafActors;
		RenderState.OutsideActors = RootRenderState->OutsideActors;
		appMemcpy(DynamicLights,RootDynamicLights,sizeof(FDynamicLight*) * RootNumDynamicLights);
		appMemcpy(DynamicProjectors,RootDynamicProjectors,sizeof(FProjectorRenderInfo*) * RootNumDynamicProjectors);
		NumDynamicLights = RootNumDynamicLights;
		NumDynamicProjectors = RootNumDynamicProjectors;
	}

	// Initialize rendering variables.

	RenderState.LeafLights		= NewZeroed<FDynamicLight**>(GSceneMem,Model->Leaves.Num());
	RenderState.LeafProjectors	= NewZeroed<FProjectorRenderInfo**>(GSceneMem,Model->Leaves.Num());

	RenderState.RenderedPortals		= NewZeroed<BYTE>(GSceneMem,(Model->Surfs.Num() + 7) / 8);

	RenderState.ActorDrawList		= NULL;
	//scion jg -- Added for sorting of single section static meshes
	RenderState.SortedActorDrawList	= NULL;
	// scion sz - null special case nali pawn list
	RenderState.NaliPawnActorDrawList = NULL;
	// translucent list
	RenderState.TranslucentDrawList = NULL;
	// scion sz - null deferred distortion actor list
	RenderState.PostProcessDrawList = NULL;
	// scion sz - null deferred emissive actor list
	RenderState.EmissiveDrawList	= NULL;
	// scion sz - null deferred static projector actor list
	RenderState.StaticProjectorBatches = NULL;

// scion sz - SM batching not used on xbox
#ifndef _XBOX
	RenderState.StaticMeshBatchList.Level = Level;
	RenderState.StaticMeshBatchList.Batches = New<INT>(GSceneMem,Level->StaticMeshBatches.Num());
	RenderState.StaticMeshBatchList.VisibleBatchElements = NewZeroed<INT*>(GSceneMem,Level->StaticMeshBatches.Num());
	RenderState.StaticMeshBatchList.NumVisibleBatchElements = New<INT>(GSceneMem,Level->StaticMeshBatches.Num());
	RenderState.StaticMeshBatchList.NumBatches = 0;
#endif

	TList<FProjectorRenderBatch*>*	SavedGProjectorBatchList = GProjectorBatchList;
	GProjectorBatchList = NULL;

	RenderState.BspDrawLists = NewZeroed<FBspDrawList*>(GSceneMem,Model->Sections.Num());
	RenderState.SectionDrawList = NULL;
	RenderState.StencilDrawLists = NULL;

	RenderState.SkyStencilMask		= 0;
	RenderState.ChildSceneNodes		= NULL;

	// Setup the view frustum.

	if(SceneNode->Viewport->Precaching)
	{
		RenderState.ActiveZones = NULL;
		for(INT ZoneIndex = 1;ZoneIndex < Model->NumZones;ZoneIndex++)
			RenderState.ActiveZones = new(GSceneMem) TList<INT>(ZoneIndex,RenderState.ActiveZones);

		RenderState.ActiveZoneMask = ((QWORD) 1 << Model->NumZones) - 1;

		for(INT ZoneIndex = 0;ZoneIndex < Model->NumZones;ZoneIndex++)
			RenderState.Zones[ZoneIndex].Portals = new(GSceneMem) TList<FConvexVolume*>(new(GSceneMem) FConvexVolume());
	}
	else
	{
		RenderState.Zones[SceneNode->ViewZone].Portals = new(GSceneMem) TList<FConvexVolume*>(new(GSceneMem) FConvexVolume(SceneNode->GetViewFrustum()));
		RenderState.ActiveZones = new(GSceneMem) TList<INT>(SceneNode->ViewZone);
		RenderState.ActiveZoneMask = (QWORD) 1 << SceneNode->ViewZone;
	}

	if(!GIsEditor)
		BuildAntiPortals(RenderState,SceneNode->ViewZone);

	AZoneInfo* ViewZoneInfo = SceneNode->Level->GetZoneActor(SceneNode->ViewZone);	
	if( !GIsEditor && !SceneNode->Parent && ViewZoneInfo->bClearToFogColor && ViewZoneInfo->bDistanceFog )
	{
		// Clear the color buffer to the distance fog color if wanted.
		FColor DistanceFogColor( ViewZoneInfo->DistanceFogColor );
		DistanceFogColor.A = 0;
		RI->Clear( 1, DistanceFogColor, 0, 0, 0 );
	}
	else if( !GIsEditor && !SceneNode->Parent )
	{
		// Clear the color buffer if outside the level.
		RI->Clear( 1, FColor(0,0,0,0), 0, 0, 0 );
	}	

	if (SceneNode->IsParentNode())
	{
		//scion jg -- Fill the stencil with ones to mask out for skybox
		RI->SetStencilOp(CF_Always,1,0xFF,SO_Keep,SO_Keep,SO_Replace,0xFF);
	}	

	STAT_INC_BY(GStats.DWORDStats( GEngineStats.STATS_Visibility_SetupCycles ),LOCAL_UNCLOCK(SetupTime));

	if (SceneNode->CanUseHardwareOcclusion())
	{
		//jg -- Update the last frame's queries
		HWOcclusionManager.UpdateQueryResults(SceneNode);
	}

	// Traverse the BSP tree.

	LOCAL_CLOCK(TravTime);

	// Push the root BSP node.

	FNodeStack*	NodeStack = NULL;

	if(!(SceneNode->Viewport->IsWire() && GIsEditor) && Model->Nodes.Num())
	{
		FNodeStack*	NodeStackBase = FNodeStack::Allocate(NumDynamicLights,NumDynamicProjectors);

		appMemcpy(NodeStackBase->DynamicLights,DynamicLights,NumDynamicLights * sizeof(FDynamicLight*));
		appMemcpy(NodeStackBase->DynamicProjectors,DynamicProjectors,NumDynamicProjectors * sizeof(FProjectorRenderInfo*));
		NodeStackBase->Pass = PASS_Front;
		NodeStackBase->iNode = 0;
		NodeStackBase->Outside = Model->RootOutside;
		NodeStackBase->Prev = NULL;

		NodeStack = NodeStackBase;
	}

	// Handle outside antiportals.

	for(TList<FDynamicActor*>* ActorList = RenderState.OutsideActors;ActorList;ActorList = ActorList->Next)
	{
		FDynamicActor*	DynamicActor = ActorList->Element;

		if(DynamicActor->Actor->DrawType == DT_AntiPortal && DynamicActor->Actor->AntiPortal && (RenderState.SceneNode->Viewport->Actor->ShowFlags & SHOW_PlayerCtrl))
		{
			//scion jg -- Check cull distance before visibility
			if(CheckCullDistance(RenderState.SceneNode->ViewOrigin,DynamicActor,RenderState.SceneNode->Viewport) && RenderState.Zones[0].Visible(DynamicActor->BoundingBox)) 
			{
				FConvexVolume*	AntiPortalVolume = ExtrudeAntiPortal(RenderState.SceneNode->ViewOrigin,DynamicActor);
				RenderState.Zones[0].AntiPortals = new(GSceneMem) TList<FConvexVolume*>(AntiPortalVolume,RenderState.Zones[0].AntiPortals);
			}
		}	
	}

	// Process the BSP tree.

	while(NodeStack)
	{
		FBspNode&	Node = Model->Nodes(NodeStack->iNode);

		if(NodeStack->Pass == PASS_Front)
		{
			// Zone mask rejection.

			STAT_INC(GStats.DWORDStats( GEngineStats.STATS_Visibility_MaskTests ));

			if(SceneNode->ViewZone && !(Node.ZoneMask & RenderState.ActiveZoneMask))
			{
				STAT_INC(GStats.DWORDStats( GEngineStats.STATS_Visibility_MaskRejects ));
				NodeStack = NodeStack->Prev;
				continue;
			}

			// Bound rejection.

			if(Node.iRenderBound != INDEX_NONE)
			{
				FBox&	BoundingBox = Model->Bounds(Node.iRenderBound);
				UBOOL	Visible = 0;

				STAT_INC(GStats.DWORDStats( GEngineStats.STATS_Visibility_BoxTests ));

				if(SceneNode->ViewZone)
				{
					for(TList<INT>* Zone = RenderState.ActiveZones;Zone;Zone = Zone->Next)
					{
						if((Node.ZoneMask & ((QWORD) 1 << Zone->Element)) && RenderState.Zones[Zone->Element].Visible(BoundingBox))
						{
							Visible = 1;
							break;
						}
					}
				}
				else
					Visible = RenderState.Zones[0].Visible(BoundingBox);

				if(!Visible)
				{
					STAT_INC(GStats.DWORDStats( GEngineStats.STATS_Visibility_BoxRejects ));
					NodeStack = NodeStack->Prev;
					continue;
				}
			}

			// Filter dynamic lights through the BSP tree.

			FDynamicLight*			ChildLights[2][256];
			INT						NumChildLights[2] = {0, 0};

			for(INT LightIndex = 0;LightIndex < NodeStack->NumDynamicLights;LightIndex++)
			{
				FDynamicLight*	DynamicLight = NodeStack->DynamicLights[LightIndex];

				if(DynamicLight->Actor->LightEffect == LE_Sunlight)
				{
					ChildLights[1][NumChildLights[1]++] = DynamicLight;
					ChildLights[0][NumChildLights[0]++] = DynamicLight;
				}
				else
				{
					FLOAT	Dist = Node.Plane.PlaneDot(DynamicLight->Position);

					if(Dist > -DynamicLight->Radius)
						ChildLights[1][NumChildLights[1]++] = DynamicLight;

					if(Dist < DynamicLight->Radius)
						ChildLights[0][NumChildLights[0]++] = DynamicLight;
				}
			}

			// Filter dynamic projectors through the BSP tree.

			FProjectorRenderInfo*	ChildProjectors[2][256];
			INT						NumChildProjectors[2] = {0, 0};

			for(INT ProjectorIndex = 0;ProjectorIndex < NodeStack->NumDynamicProjectors;ProjectorIndex++)
			{
				FProjectorRenderInfo*	ProjectorInfo = NodeStack->DynamicProjectors[ProjectorIndex];
				FLOAT					PushOut = FBoxPushOut(Node.Plane,ProjectorInfo->BoundingBoxExtent),
										Dist = Node.Plane.PlaneDot(ProjectorInfo->BoundingBoxCenter);

				if(Dist > -PushOut)
					ChildProjectors[1][NumChildProjectors[1]++] = ProjectorInfo;

				if(Dist < PushOut)
					ChildProjectors[0][NumChildProjectors[0]++] = ProjectorInfo;
			}

			// Push the current node onto the stack for the second pass.

			INT	IsFront = Node.Plane.PlaneDot(SceneNode->ViewOrigin) > 0.0f;

			NodeStack->Pass = PASS_Plane;
			NodeStack->iFarNode = Node.iChild[1 - IsFront];
			NodeStack->FarOutside = Node.ChildOutside(1 - IsFront,NodeStack->Outside);
			NodeStack->iFarLeaf = Node.iLeaf[1 - IsFront];

			appMemcpy(NodeStack->DynamicLights,ChildLights[1 - IsFront],NumChildLights[1 - IsFront] * sizeof(FDynamicLight*));
			NodeStack->NumDynamicLights = NumChildLights[1 - IsFront];

			appMemcpy(NodeStack->DynamicProjectors,ChildProjectors[1 - IsFront],NumChildProjectors[1 - IsFront] * sizeof(FProjectorRenderInfo*));
			NodeStack->NumDynamicProjectors = NumChildProjectors[1 - IsFront];

			if(Node.iChild[IsFront] != INDEX_NONE)
			{
				// Push the front child onto the stack for the first pass.

				FNodeStack*	FrontNodeStack = FNodeStack::Allocate(NumChildLights[IsFront],NumChildProjectors[IsFront]);

				appMemcpy(FrontNodeStack->DynamicLights,ChildLights[IsFront],NumChildLights[IsFront] * sizeof(FDynamicLight*));
				appMemcpy(FrontNodeStack->DynamicProjectors,ChildProjectors[IsFront],NumChildProjectors[IsFront] * sizeof(FProjectorRenderInfo*));
				FrontNodeStack->iNode = Node.iChild[IsFront];
				FrontNodeStack->Outside = Node.ChildOutside(IsFront,NodeStack->Outside);
				FrontNodeStack->Pass = PASS_Front;
				FrontNodeStack->Prev = NodeStack;

				NodeStack = FrontNodeStack;
			}
			else if(Node.iLeaf[IsFront] != INDEX_NONE)
			{
				if(NumChildLights[IsFront])
				{
					ChildLights[IsFront][NumChildLights[IsFront]] = NULL;
					RenderState.LeafLights[Node.iLeaf[IsFront]] = New<FDynamicLight*>(GSceneMem,NumChildLights[IsFront] + 1);
					appMemcpy(RenderState.LeafLights[Node.iLeaf[IsFront]],ChildLights[IsFront],sizeof(FDynamicLight*) * (NumChildLights[IsFront] + 1));
				}

				if(NumChildProjectors[IsFront])
				{
					ChildProjectors[IsFront][NumChildProjectors[IsFront]] = NULL;
					RenderState.LeafProjectors[Node.iLeaf[IsFront]] = New<FProjectorRenderInfo*>(GSceneMem,NumChildProjectors[IsFront] + 1);
					appMemcpy(RenderState.LeafProjectors[Node.iLeaf[IsFront]],ChildProjectors[IsFront],sizeof(FProjectorRenderInfo*) * (NumChildProjectors[IsFront] + 1));
				}

				ProcessLeaf(RenderState,Node.iLeaf[IsFront]);
			}

			continue;
		}
		else
		{
			// Process the node and it's coplanars.

			ProcessNode(RenderState,NodeStack->iNode,NodeStack->DynamicLights,NodeStack->NumDynamicLights,NodeStack->DynamicProjectors,NodeStack->NumDynamicProjectors);

			// Push the back child onto the stack for the first pass.

			if(NodeStack->iFarNode != INDEX_NONE)
			{
				NodeStack->iNode = NodeStack->iFarNode;
				NodeStack->Outside = NodeStack->FarOutside;
				NodeStack->Pass = PASS_Front;
				continue;
			}
			else if(NodeStack->iFarLeaf != INDEX_NONE)
			{
				if(NodeStack->NumDynamicLights)
				{
					RenderState.LeafLights[NodeStack->iFarLeaf] = New<FDynamicLight*>(GSceneMem,NodeStack->NumDynamicLights + 1);
					appMemcpy(RenderState.LeafLights[NodeStack->iFarLeaf],NodeStack->DynamicLights,sizeof(FDynamicLight*) * NodeStack->NumDynamicLights);
					RenderState.LeafLights[NodeStack->iFarLeaf][NodeStack->NumDynamicLights] = NULL;
				}

				if(NodeStack->NumDynamicProjectors)
				{
					RenderState.LeafProjectors[NodeStack->iFarLeaf] = New<FProjectorRenderInfo*>(GSceneMem,NodeStack->NumDynamicProjectors + 1);
					appMemcpy(RenderState.LeafProjectors[NodeStack->iFarLeaf],NodeStack->DynamicProjectors,sizeof(FProjectorRenderInfo*) * NodeStack->NumDynamicProjectors);
					RenderState.LeafProjectors[NodeStack->iFarLeaf][NodeStack->NumDynamicProjectors] = NULL;
				}

				ProcessLeaf(RenderState,NodeStack->iFarLeaf);
			}
			
			NodeStack = NodeStack->Prev;
		}
	};
	
	// If the camera is outside the world, render actors that are outside.
	for(TList<FDynamicActor*>* ActorList = RenderState.OutsideActors;ActorList;ActorList = ActorList->Next)
	{
		FDynamicActor*	DynamicActor = ActorList->Element;
		
		if (RenderState.Zones[0].Visible(DynamicActor->BoundingBox) &&
			CheckCullDistance(RenderState.SceneNode->ViewOrigin,DynamicActor,RenderState.SceneNode->Viewport)) 
		{
			//jg -- Check for a hardware occluded actor
			if (RenderState.SceneNode->CanUseHardwareOcclusion() == 0 ||
				DynamicActor->IsVisible(RenderState.SceneNode->Viewport))
			{
				// scion sz - add to deferred actor draw lists
				AddActorToDeferredDrawLists( DynamicActor, RenderState, 1 );				
			}
			//jg -- Add to the hardware occlusion list
			HWOcclusionManager.AddQuery(RenderState.SceneNode,DynamicActor);
		}
	}

	STAT_INC_BY(GStats.DWORDStats( GEngineStats.STATS_Visibility_TraverseCycles ),LOCAL_UNCLOCK(TravTime));


	// Render child scene nodes first if not using stencil.
	if (!GIsEditor && !SceneNode->Viewport->RenDev->UseStencil)
	{
		UBOOL RequiresClear = 0;
		for(TList<FLevelSceneNode*>* Child = RenderState.ChildSceneNodes;Child;Child = Child->Next)
		{
			RequiresClear = 1;
			Child->Element->Render(RI);
			delete Child->Element;
		}
		if( RequiresClear )
			RI->Clear(0,FColor(0,0,0),1,1.0f,0,0);
	}

	// Render terrain.
	if(SceneNode->Viewport->Actor->ShowFlags & SHOW_Terrain)
	{
		FUNC_CLOCK(TerCyc,GStats.DWORDStats( GEngineStats.STATS_Terrain_RenderCycles ));

		for(INT ZoneIndex = 0;ZoneIndex < Model->NumZones;ZoneIndex++)
		{
			AZoneInfo*	ZoneActor = Level->GetZoneActor(ZoneIndex);

			if(ZoneActor->bTerrainZone && (!SceneNode->ViewZone || (RenderState.ActiveZoneMask & ((QWORD) 1 << ZoneIndex))) && ZoneIndex != SceneNode->InvisibleZone)
			{
				for(INT TerrainIndex = 0;TerrainIndex < ZoneActor->Terrains.Num();TerrainIndex++) 
				{
					ATerrainInfo*	TerrainInfo = ZoneActor->Terrains(TerrainIndex);

					RI->PushState();

					if( TerrainInfo->Inverted )
						RI->SetCullMode(SceneNode->Determinant < 0.0f ? CM_CW : CM_CCW);

					TerrainInfo->Render(
						SceneNode,
						RI,
						SceneNode->ViewZone ? &RenderState.Zones[ZoneIndex] : &RenderState.Zones[0],
						DynamicLights,
						NumDynamicLights,
						DynamicProjectors,
						NumDynamicProjectors
						); 

					RI->PopState();

					STAT_INC(GStats.DWORDStats( GEngineStats.STATS_Visibility_VisibleObjects ));
				}
			}
		}
	}

	// Render solid BSP sufaces.
	RI->PushState();
	RI->SetTransform(TT_LocalToWorld,FMatrix::Identity);

	for(TList<INT>* SectionList = RenderState.SectionDrawList;SectionList;SectionList = SectionList->Next)
	{
		RenderState.BspDrawLists[SectionList->Element]->Render(RenderState.SceneNode,RI,DynamicProjectors,NumDynamicProjectors);
		STAT_INC(GStats.DWORDStats( GEngineStats.STATS_Visibility_VisibleObjects ));
	}

	// Render solid actors.
	SceneNode->FilterMode = FSceneNode::Filter_Solid;
	for(TList<FDynamicActor*>* ActorDrawList = RenderState.ActorDrawList;ActorDrawList;ActorDrawList = ActorDrawList->Next)
	{
		//jg -- Handle deferred query tests
		HWOcclusionManager.BeginDeferredQuery(RenderState.SceneNode,ActorDrawList->Element);
		RenderActor(RenderState,ActorDrawList->Element);
		//jg -- Handle deferred query tests
		HWOcclusionManager.EndDeferredQuery(RenderState.SceneNode,ActorDrawList->Element);
		STAT_INC(GStats.DWORDStats( GEngineStats.STATS_Visibility_VisibleObjects ));
	}

#if 0 //scion jg -- Debug code for static mesh sorting
	static UBOOL bLog = 0;
	if (bLog)
	{
		INT nCount = 0;
		for (TList<FDynamicActor*>* DrawList = RenderState.SortedActorDrawList;
			DrawList != NULL; DrawList = DrawList->Next)
		{
			const TCHAR* szActorName = DrawList->Element->Actor->GetName();
			const TCHAR* szMeshName = DrawList->Element->Actor->StaticMesh->GetName();
			UMaterial* pMaterial = DrawList->Element->Actor->StaticMesh->GetSkin(
				DrawList->Element->Actor,0);
			debugf(TEXT("%d : %s (%s) (%s)"),++nCount,szActorName,szMeshName,
				pMaterial->GetName());
		}
		bLog = 0;
	}
#endif

	//scion jg -- Render actors that have been sorted by material and mesh
	for (TList<FDynamicActor*>* DrawList = RenderState.SortedActorDrawList;
		DrawList != NULL; DrawList = DrawList->Next)
	{
		//jg -- Handle deferred query tests
		HWOcclusionManager.BeginDeferredQuery(RenderState.SceneNode,DrawList->Element);
		RenderActor(RenderState,DrawList->Element);
		//jg -- Handle deferred query tests
		HWOcclusionManager.EndDeferredQuery(RenderState.SceneNode,DrawList->Element);
		STAT_INC(GStats.DWORDStats( GEngineStats.STATS_Visibility_VisibleObjects ));
	}

	//scion sz -- Render special case nali pawn actors 
	for (TList<FDynamicActor*>* DrawList = RenderState.NaliPawnActorDrawList;
		DrawList != NULL; DrawList = DrawList->Next)
	{
		// these must all be DT_NaliPawn actors and have meshes
		FDynamicActor* DynActor = DrawList->Element;
		checkSlow(DynActor->Actor->DrawType == DT_NaliPawn);
		checkSlow(DynActor->Actor->Mesh != NULL);
        // start deferred query
		HWOcclusionManager.BeginDeferredQuery(RenderState.SceneNode,DynActor);	
		
        // get lights/projectors etc and render it
		RenderActor(RenderState,DynActor,1);

		// end deferred query
		HWOcclusionManager.EndDeferredQuery(RenderState.SceneNode,DynActor);
		STAT_INC(GStats.DWORDStats( GEngineStats.STATS_Visibility_VisibleObjects ));
	}

#ifndef _XBOX
	RenderState.StaticMeshBatchList.Render(SceneNode,RI);
#endif

	if( GProjectorBatchList )
	{
		// scion sz - l2w must be reset here as it is needed for projector frustum clipping calcs
		RI->SetTransform( TT_LocalToWorld, FMatrix::Identity );	
		RI->EnableLighting(0,1,0,NULL,0);
		RI->SetZBias(1);	
		// Disable stencil
		RI->SetStencilOp(CF_Always,1,0xFF,SO_Keep,SO_Keep,SO_Keep,0xFF);
		if(!SceneNode->Viewport->HitTesting) 
		{
			for(TList<FProjectorRenderBatch*>* ProjectorBatchList = GProjectorBatchList;ProjectorBatchList;ProjectorBatchList = ProjectorBatchList->Next) {
				ProjectorBatchList->Element->Render(SceneNode,RI);
			}
		}
		RI->ResetStencilOp();
		GProjectorBatchList = NULL;
	}	

	RI->PopState();

	// scion sz -
	// Render Emissive actors
	SceneNode->FilterMode = FSceneNode::Filter_Emissive;
	for(TList<FDynamicActor*>* EmissiveDrawList = RenderState.EmissiveDrawList; EmissiveDrawList; EmissiveDrawList = EmissiveDrawList->Next)
	{
		//jg -- Handle deferred query tests
		HWOcclusionManager.BeginDeferredQuery(RenderState.SceneNode,EmissiveDrawList->Element);
		RenderActor(RenderState,EmissiveDrawList->Element);
		//jg -- Handle deferred query tests
		HWOcclusionManager.EndDeferredQuery(RenderState.SceneNode,EmissiveDrawList->Element);
		STAT_INC(GStats.DWORDStats( GEngineStats.STATS_Visibility_VisibleObjects ));
	}

	// scion sz -
	// projectors for above
	if( !SceneNode->Viewport->HitTesting && 
		GProjectorBatchList )
	{
		RI->PushState();
		RI->SetTransform( TT_LocalToWorld, FMatrix::Identity );	
		RI->EnableLighting(0,1,0,NULL,0);
		RI->SetZBias(1);			
		// Disable stencil
		RI->SetStencilOp(CF_Always,1,0xFF,SO_Keep,SO_Keep,SO_Keep,0xFF);
		for(TList<FProjectorRenderBatch*>* ProjectorBatchList = GProjectorBatchList;ProjectorBatchList;ProjectorBatchList = ProjectorBatchList->Next) 
		{
			ProjectorBatchList->Element->Render(SceneNode,RI);
		}
        RI->ResetStencilOp();
		RI->PopState();
		GProjectorBatchList = NULL;
	}

	if( GIsEditor || SceneNode->Viewport->RenDev->UseStencil )
	{
		// Render stencil surfaces.
		for(TList<FBspStencilDrawList*>* StencilDrawList = RenderState.StencilDrawLists;StencilDrawList;StencilDrawList = StencilDrawList->Next)
			StencilDrawList->Element->Render(SceneNode,RI);

#ifndef _XBOX
		if (GIsEditor == 1)
		{
			// Reset the stencil
			DECLARE_STATIC_UOBJECT(UProxyBitmapMaterial,crap,
			{
				static FSolidColorTexture	crapColor(FColor(255,255,0));
				crap->SetTextureInterface(&crapColor);
			});
			DECLARE_STATIC_UOBJECT(UFinalBlend,SkyStencilClear,
			{
				SkyStencilClear->Material = crap;
				SkyStencilClear->FrameBufferBlending = FB_Invisible;
				SkyStencilClear->ZWrite = 0;
				SkyStencilClear->ZTest = 0;
				SkyStencilClear->TwoSided = 1;
			});
			// clear non-1 stencil
			RI->SetStencilOp(CF_Equal,1,0xFF,SO_Zero,SO_Keep,SO_Keep,0xFF);
			FCanvasUtil(&SceneNode->Viewport->RenderTarget,RI).DrawTile(0,0,
				SceneNode->Viewport->SizeX,SceneNode->Viewport->SizeY,
				0,0,0,0,1,SkyStencilClear,FColor(0,0,0));
			RI->ResetStencilOp();
		}
#endif

		//scion jg -- Only render the skybox where there are 0s in the stencil
		RI->SetStencilOp(CF_Equal,0,0xFF,SO_Keep,SO_Keep,SO_Replace,0xFF);
		// Render child scene nodes.
		for(TList<FLevelSceneNode*>* Child = RenderState.ChildSceneNodes;Child;Child = Child->Next)
		{
			Child->Element->Render(RI);
			delete Child->Element;
		}
		// Set depth buffer back to 1 wherever there is sky
		DECLARE_STATIC_UOBJECT(UProxyBitmapMaterial,HackMaterial,
		{
			static FSolidColorTexture	BlackTexture(FColor(255,255,0));
			HackMaterial->SetTextureInterface(&BlackTexture);
		});
		DECLARE_STATIC_UOBJECT(UFinalBlend,SkyStencilFinalBlend,
		{
			SkyStencilFinalBlend->Material = HackMaterial;
			SkyStencilFinalBlend->FrameBufferBlending = FB_Invisible;
			SkyStencilFinalBlend->ZWrite = 1;
			SkyStencilFinalBlend->ZTest = 0;
			SkyStencilFinalBlend->TwoSided = 1;
		});
		FCanvasUtil(&SceneNode->Viewport->RenderTarget,RI).DrawTile(0,0,
			SceneNode->Viewport->SizeX,SceneNode->Viewport->SizeY,
			0,0,0,0,1,SkyStencilFinalBlend,FColor(0,0,0));
		//scion jg -- Reset stencil operations
		RI->ResetStencilOp();
	}

	//scion jg -- Reset stencil op for the skybox mask and set everything ignore
	// stencil from here on out
	if (SceneNode->IsParentNode())
	{
		RI->ResetStencilOp();
		// Ignore all stencil
		RI->SetStencilOp(CF_Always,0,0xFF,SO_Keep,SO_Keep,SO_Keep,0xFF);
	}

#ifndef FINAL_RELEASE
	// scion sz -
	// Render Projector volumes
	if( SceneNode->Viewport->bShowProjectorHulls )
	{
		for( INT Idx=0; Idx < Level->Actors.Num(); Idx++ )
		{
			AProjector* Projector = Cast<AProjector>( Level->Actors(Idx) );
			if( Projector )
			{
				Projector->RenderWireframe( RI );
			}
		}
	}
#endif		

	// scion sz -
	// render static projector batched geometry
	for( TList<FDynamicActor*>* StaticProjectorBatches = RenderState.StaticProjectorBatches; 
		StaticProjectorBatches; StaticProjectorBatches = StaticProjectorBatches->Next )
	{
		FDynamicActor* DynActor = StaticProjectorBatches->Element;
		//jg -- Handle deferred query tests
		HWOcclusionManager.BeginDeferredQuery(RenderState.SceneNode,DynActor);
		DynActor->Render( SceneNode, NULL, NULL, RI );
		//jg -- Handle deferred query tests
		HWOcclusionManager.EndDeferredQuery(RenderState.SceneNode,DynActor);
		STAT_INC(GStats.DWORDStats( GEngineStats.STATS_Visibility_VisibleObjects ));
	}

	//jg -- Process any pending hardware occlusion queries
	if (SceneNode->CanUseHardwareOcclusion())
	{
		HWOcclusionManager.SubmitQueries(SceneNode);
	}

	// scion sz -
	// moved terrain rendering to before Translucent items
	// Render terrain decorations.
	if( SceneNode->Viewport->GetOuterUClient()->DecoLayers && (SceneNode->Viewport->Actor->ShowFlags & SHOW_Terrain) )
	{
		FUNC_CLOCK(DecoCyc,GStats.DWORDStats( GEngineStats.STATS_DecoLayer_RenderCycles ));

		RI->PushState();

		for(INT ZoneIndex = 0;ZoneIndex < Model->NumZones;ZoneIndex++)
		{
			AZoneInfo*	ZoneActor = Level->GetZoneActor(ZoneIndex);

			if(ZoneActor->bTerrainZone && (!SceneNode->ViewZone || (RenderState.ActiveZoneMask & ((QWORD) 1 << ZoneIndex))) && ZoneIndex != SceneNode->InvisibleZone)
			{
				for(INT TerrainIndex = 0;TerrainIndex < ZoneActor->Terrains.Num();TerrainIndex++) 
				{
					ATerrainInfo*	TerrainInfo = ZoneActor->Terrains(TerrainIndex);

					if( TerrainInfo->Inverted )
						RI->SetCullMode(SceneNode->Determinant < 0.0f ? CM_CW : CM_CCW);

					TerrainInfo->RenderDecorations(SceneNode,RI,SceneNode->ViewZone ? &RenderState.Zones[ZoneIndex] : &RenderState.Zones[0]);

					if( TerrainInfo->Inverted )
						RI->SetCullMode(SceneNode->Determinant < 0.0f ? CM_CCW : CM_CW);
				}
			}
		}

		RI->PopState();
	}

	// scion sz -
	// Render translucent actors.	
	SceneNode->FilterMode = FSceneNode::Filter_Translucent;
	for(TList<FTranslucentDrawItem>* TranslucentDrawList = RenderState.TranslucentDrawList;TranslucentDrawList;TranslucentDrawList = TranslucentDrawList->Next)
	{
		FDynamicActor* DynamicActor = TranslucentDrawList->Element.DynamicActor;

		if( TranslucentDrawList->Element.BSP )
		{
			RI->PushState();

			FBspDrawList			ScratchList(Model);
			FTranslucentDrawItem&	Item = TranslucentDrawList->Element;
			FBspNode&				Node = Model->Nodes(Item.iNode);

			ScratchList.SectionIndex = Node.iSection;
			ScratchList.Nodes = &Item.iNode;
			ScratchList.AddNode(Item.iNode,Item.DynamicLights,Item.NumDynamicLights,Item.DynamicProjectors,Item.NumDynamicProjectors,SceneNode);

			ScratchList.Render(SceneNode,RI,Item.DynamicProjectors,Item.NumDynamicProjectors);

			RI->PopState();
			STAT_INC(GStats.DWORDStats( GEngineStats.STATS_Visibility_VisibleObjects ));
		}
		else 
		{
			//jg -- Handle deferred query tests
			HWOcclusionManager.BeginDeferredQuery(RenderState.SceneNode,DynamicActor);
			RenderActor( RenderState, DynamicActor );
			//jg -- Handle deferred query tests
			HWOcclusionManager.EndDeferredQuery(RenderState.SceneNode,DynamicActor);
			STAT_INC(GStats.DWORDStats( GEngineStats.STATS_Visibility_VisibleObjects ));
		}

		if( !SceneNode->Viewport->HitTesting && 
			GProjectorBatchList )
		{
			// scion sz - 
			RI->PushState();
			RI->SetTransform( TT_LocalToWorld, FMatrix::Identity );	
			RI->EnableLighting(0,1,0,NULL,0);
			RI->SetZBias(1);	
			// Disable stencil
			RI->SetStencilOp(CF_Always,1,0xFF,SO_Keep,SO_Keep,SO_Keep,0xFF);
			for(TList<FProjectorRenderBatch*>* ProjectorBatchList = GProjectorBatchList;ProjectorBatchList;ProjectorBatchList = ProjectorBatchList->Next)
			{
				ProjectorBatchList->Element->Render(SceneNode,RI);
			}
            RI->ResetStencilOp();
			RI->PopState();
			GProjectorBatchList = NULL;
		}		
	}

	// restore
	GProjectorBatchList = SavedGProjectorBatchList;
	
	// Render coronas. 
	if( !SceneNode->Parent && (SceneNode->Viewport->Actor->ShowFlags & SHOW_Coronas) )
	{
		RI->PushState();
		UC2CoronaRenderFor( SceneNode->Viewport ).RenderCoronas( SceneNode, RI );	// uc2 port (G-9)
		RI->PopState();
	}

    // scion sz - render distortion items
	if( RenderState.PostProcessDrawList )
	{
		FPostProcessRender PostProcessDraw( RenderState.PostProcessDrawList, SceneNode, RI );
		PostProcessDraw.Render();
	}

	// Render the builder brush.
	if( GIsEditor && SceneNode->FilterActor(Level->Brush()) )
	{
		FDynamicActor*	DynamicActor = Level->Brush()->GetActorRenderData();
		FConvexVolume	Frustum(SceneNode->GetViewFrustum());
		if(Frustum.BoxCheck(DynamicActor->BoundingBox.GetCenter(), DynamicActor->BoundingBox.GetExtent()) & CF_Inside)
			DynamicActor->Render(SceneNode,NULL,NULL,RI);
	}

#if 0
	// Render antiportal volumes.

	RI->SetTransform(TT_LocalToWorld,FMatrix::Identity);
	for(INT ZoneIndex = 0;ZoneIndex < Model->NumZones;ZoneIndex++)
		for(TList<FConvexVolume*>* AntiPortalList = RenderState.Zones[ZoneIndex].AntiPortals;AntiPortalList;AntiPortalList = AntiPortalList->Next)
			FLineBatcher(RI,0).DrawConvexVolume(*AntiPortalList->Element,FColor(255,0,0));
#endif

	//scion jg -- Reset all stencil changes
	if (!SceneNode->Parent)
	{
		RI->ResetStencilOp();
	}
	// Cleanup.
	STAT_INC_BY(GStats.DWORDStats( GEngineStats.STATS_Visibility_ScratchBytes ),(GSceneMem.GetByteCount() - InitialByteCount));

	if(RootRenderState == &RenderState)
		RootRenderState = NULL;

	MemMark.Pop();

	unguard;
}



