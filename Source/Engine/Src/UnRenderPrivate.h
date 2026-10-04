/*=============================================================================
	UnRenderPrivate.h: Private rendering definitions.
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/

#define UPDATE_CHANGED_LIGHTMAPS	GIsEditor
#define UPDATE_DYNAMIC_LIGHTMAPS	0

//
//	FRenderBatchChunk
//

enum { BATCH_CHUNK_SIZE = 32 };

template<class PrimitiveType> struct FRenderBatchChunk
{
	INT					BaseIndex;
	PrimitiveType*		Primitives[BATCH_CHUNK_SIZE];
	FRenderBatchChunk*	NextChunk;
};

//
//	FRenderBatch
//

template<class PrimitiveType> class FRenderBatch
{
public:

	FRenderBatchChunk<PrimitiveType>*	FirstChunk;
	FRenderBatchChunk<PrimitiveType>*	LastChunk;
	INT									NumPrimitives;

	// Constructor.
	FRenderBatch()
	{
		FirstChunk = LastChunk = NULL;
		NumPrimitives = 0;
	}

	// AddPrimitive

	void AddPrimitive(PrimitiveType* Primitive)
	{
		if(!LastChunk || (NumPrimitives - LastChunk->BaseIndex) == BATCH_CHUNK_SIZE)
		{
			FRenderBatchChunk<PrimitiveType>*	NewChunk = new(GSceneMem) FRenderBatchChunk<PrimitiveType>;

			NewChunk->NextChunk = NULL;

			if(LastChunk)
			{
				NewChunk->BaseIndex = LastChunk->BaseIndex + BATCH_CHUNK_SIZE;
				LastChunk->NextChunk = NewChunk;
				LastChunk = NewChunk;
			}
			else
			{
				NewChunk->BaseIndex = 0;
				FirstChunk = LastChunk = NewChunk;
			}
		}

		LastChunk->Primitives[NumPrimitives - LastChunk->BaseIndex] = Primitive;
		NumPrimitives++;
	}
};

//
//	FProjectorBatchVertex
//

struct FProjectorBatchVertex
{
	FVector	WorldPosition;
#ifdef _XBOX
	// scion sz - we need normals to attenuate vertices
	FPackedNormal Normal;
	// ProjectedPosition, Attenuation not needed since they are dynamically
	// generated on the GPU
#else
	FVector	Normal;	
#endif

#if DYNAMIC_PROJ_STREAM
	FColor	Attenuation;
	FVector	ProjectedPosition;
#endif

};

//
//	FProjectorRenderPrimitive
//

class FProjectorRenderPrimitive
{
public:	
	INT	NumIndices,
		NumVertices;

	virtual void GetIndices(_WORD* DestIndices,_WORD BaseVertexIndex) = 0;
	virtual void GetVertices(BYTE* DestVertices) = 0;
	// scion sz - DrawPrimitiveUP support
	virtual void GetRawVertices( void** DestVertices, INT FirstVertex ) =0;
	virtual void GetRawIndices( void** DestIndices ) =0;
	// scion sz - needed for vertex shader rendering of projector
	virtual FProjectorRenderInfo* GetProjRenderInfo() =0;
};

//
//	FStaticProjectorPrimitive
//

class FStaticProjectorPrimitive : public FProjectorRenderPrimitive
{
public:

	FStaticProjectorInfo*	ProjectorInfo;

	// Constructor.

	FStaticProjectorPrimitive(FStaticProjectorInfo* InProjectorInfo)
	{
		ProjectorInfo = InProjectorInfo;

		NumVertices = ProjectorInfo->Vertices.Num();
		NumIndices = ProjectorInfo->Indices.Num();
	}

	// GetIndices

	virtual void GetIndices(_WORD* DestIndices,_WORD BaseVertexIndex)
	{
		for(INT Index = 0;Index < ProjectorInfo->Indices.Num();Index++)
			*DestIndices++ = ProjectorInfo->Indices(Index) + BaseVertexIndex;
	}

	// GetVertices

	virtual void GetVertices(BYTE* DestVertices)
	{
// scion sz - vertex data updated on GPU
#if DYNAMIC_PROJ_STREAM
		for(INT VertexIndex = 0;VertexIndex < ProjectorInfo->Vertices.Num();VertexIndex++)
		{
			FProjectorBatchVertex*	DestVertex = (FProjectorBatchVertex*)DestVertices;
			FStaticProjectorVertex*	SrcVertex = &ProjectorInfo->Vertices(VertexIndex);

			DestVertex->WorldPosition = SrcVertex->WorldPosition;
			//scion jg -- Use the precalculated position, instead of calculating again
			FVector ProjectedPosition = SrcVertex->ProjectedPosition;

			if(!(ProjectorInfo->RenderInfo->ProjectorFlags & PRF_Projected))
				ProjectedPosition.Z = 1.0f;

			DestVertex->ProjectedPosition = ProjectedPosition;

			FLOAT	LifeSpan = ProjectorInfo->RenderInfo->Expires == 0.0 ? 1.0 : ProjectorInfo->RenderInfo->Expires - ProjectorInfo->RenderInfo->LastRenderTime;

			BYTE	AttenuationByte = Clamp<INT>(appFloor(SrcVertex->Attenuation * Clamp(LifeSpan,0.0f,1.0f) * 255.0f),0,255);
			DestVertex->Attenuation = FColor(AttenuationByte,AttenuationByte,AttenuationByte,AttenuationByte);

			DestVertices += sizeof(FProjectorBatchVertex);

			if(ProjectorInfo->BaseMaterial)
			{
				FLOAT*	DestUV = (FLOAT*)DestVertices;
				DestUV[0] = ProjectorInfo->BaseUVs(VertexIndex).U;
				DestUV[1] = ProjectorInfo->BaseUVs(VertexIndex).V;
				DestVertices += sizeof(FLOAT) * 2;
			}
		}
#endif
	}

	// scion sz -
	virtual void GetRawVertices( void** DestVertices, INT FirstVertex )
	{
		checkSlow( ProjectorInfo );
		*DestVertices = &ProjectorInfo->Vertices( FirstVertex );
	}

	// scion sz -
	virtual void GetRawIndices( void** DestIndices )
	{
		checkSlow( ProjectorInfo );
        *DestIndices = &ProjectorInfo->Indices(0);
	}

	// scion sz -
	virtual FProjectorRenderInfo* GetProjRenderInfo()
	{
		checkSlow( ProjectorInfo );
		return ProjectorInfo->RenderInfo;
	}
};


/* scion ======================================================================
* class FProjectorRenderBatch
* Author: sz
* 
* abstract base for any batched projectors
* ============================================================================
*/
class FProjectorRenderBatch
{
public:

	enum EBatchType {
		BATCH_Default,
		BATCH_DynamicBSP,
		BATCH_DynamicStaticMesh,
		BATCH_MAX
	};

	virtual ~FProjectorRenderBatch(){}
	virtual void Render( FSceneNode* SceneNode,FRenderInterface* RI )=0;
	virtual void AddPrimitive( FProjectorRenderPrimitive* Primitive )=0;
	virtual UBOOL MatchProjectorInfo( FProjectorRenderInfo* InRenderInfo, 
		UMaterial* InBaseMaterial, UBOOL InTwoSided, EBatchType BatchType=BATCH_Default )=0;
	virtual EBatchType GetBatchType()=0;

};

#if DYNAMIC_PROJ_STREAM
/* scion ======================================================================
* class FProjectorRenderBatchDef
* Author: 
* 
* PC version of projector batcher
* ============================================================================
*/
class FProjectorRenderBatchDef : public FProjectorRenderBatch, public FRenderBatch<FProjectorRenderPrimitive>
{
public:

	UMaterial*			BaseMaterial;
	UMaterial*			ProjectedMaterial;
	EProjectorBlending	BaseMaterialBlending;
	EProjectorBlending	FramebufferBlending;
	UBOOL				TwoSided;

	INT					VertexStride,
						VertexBufferSize,
						IndexBufferSize;

	// Constructor.
	FProjectorRenderBatchDef(UMaterial* InBaseMaterial,
		UMaterial* InProjectedMaterial,
		EProjectorBlending InBaseMaterialBlending,
		EProjectorBlending InFramebufferBlending,
		UBOOL InTwoSided)
	{
		BaseMaterial = InBaseMaterial;
		ProjectedMaterial = InProjectedMaterial;
		BaseMaterialBlending = InBaseMaterialBlending;
		FramebufferBlending = InFramebufferBlending;
		TwoSided = InTwoSided;

		VertexBufferSize = 0;
		IndexBufferSize = 0;

		VertexStride = sizeof(FProjectorBatchVertex);
		if(BaseMaterial)
			VertexStride += sizeof(FLOAT) * 2;
	}

	virtual EBatchType GetBatchType() { return BATCH_Default; }

	// Render
	virtual void Render(FSceneNode* SceneNode,FRenderInterface* RI);

	// AddPrimitive
	virtual void AddPrimitive(FProjectorRenderPrimitive* Primitive)
	{
		FRenderBatch<FProjectorRenderPrimitive>::AddPrimitive(Primitive);

		VertexBufferSize += Primitive->NumVertices * VertexStride;
		IndexBufferSize += Primitive->NumIndices * sizeof(_WORD);
	}

	virtual UBOOL MatchProjectorInfo( FProjectorRenderInfo* InRenderInfo, 
		UMaterial* InBaseMaterial, 
		UBOOL InTwoSided, 
		EBatchType InBatchType=BATCH_Default )
	{
		guardSlow( FProjectorRenderBatchDef::MatchProjectorInfo );
		checkSlow( InRenderInfo );

		// Return true if the static projector belongs in this batch; false otherwise.

		if( GetBatchType() != InBatchType )
			return 0;

		if( ProjectedMaterial != InRenderInfo->Material )
			return 0;

		if( FramebufferBlending != InRenderInfo->FrameBufferBlendingOp )
			return 0;

		if( BaseMaterialBlending != InRenderInfo->MaterialBlendingOp )
			return 0;

		if( BaseMaterial != InBaseMaterial )
			return 0;

		if( TwoSided != InTwoSided )
			return 0;

		if( VertexBufferSize >= 100000 )
			return 0;

		return 1;

		unguardSlow;
	}
};

#else
/* scion ======================================================================
* class FProjectorRenderBatch
* Author: sz
* 
* XBox version of projector batcher.
* Static projector primitives are batched only based on their projector.
* This is the only batching that can be done since the GPU (vertex shader) 
* state will be unique to each projector.
* ============================================================================
*/
class FProjectorRenderBatchDef : public FProjectorRenderBatch
{
public:
	// only a single projector's geometry can be batched
	// this is the renderinfo corresponding to that projector
	FProjectorRenderInfo*				RenderInfo;
	// list of static projector primitives that use the projector
	TList<FProjectorRenderPrimitive*>*	RenderPrimList;

	FProjectorRenderBatchDef()
	{
		RenderInfo = NULL;
		RenderPrimList = NULL;
	}

	FProjectorRenderBatchDef( FProjectorRenderInfo* InRenderInfo )
	{
		RenderInfo = InRenderInfo;
		RenderPrimList = NULL;
	}

	virtual EBatchType GetBatchType() { return BATCH_Default; }

	virtual void Render( FSceneNode* SceneNode, FRenderInterface* RI );

	virtual void AddPrimitive( FProjectorRenderPrimitive* Primitive )
	{
		checkSlow( Primitive );
		RenderPrimList = new(GSceneMem) TList<FProjectorRenderPrimitive*>( Primitive, RenderPrimList );		
	}

	virtual UBOOL MatchProjectorInfo( FProjectorRenderInfo* InRenderInfo, 
		UMaterial* InBaseMaterial, 
		UBOOL InTwoSided,
		EBatchType InBatchType=BATCH_Default )
	{
		guardSlow( FProjectorRenderBatchDef::MatchProjectorInfo );
		checkSlow( InRenderInfo );

		// Return true if the static projector belongs in this batch; false otherwise.	

		if( RenderInfo != InRenderInfo )
			return 0;

		if( GetBatchType() != InBatchType )
			return 0;

		/*
		if( BaseMaterial != InBaseMaterial )
			return 0;
		*/

		return 1;

		unguardSlow;
	}
};

/* scion ======================================================================
* class FProjectorRenderPrimitiveBSPNodes
* Author: sz
* 
* Keep track of a list of BSP nodes that are needed to render this projector
* ============================================================================
*/
class FProjectorRenderPrimitiveBSPNodes : public FProjectorRenderPrimitive
{
public:
	INT						SectionIdx,NodeIdx;
	UModel*					Model;

	FProjectorRenderPrimitiveBSPNodes( UModel* InModel, INT InSectionIdx, INT InNodeIdx ) :
		Model(InModel),
		SectionIdx(InSectionIdx),
		NodeIdx(InNodeIdx)
	{
		checkSlow(Model && 
			Model->Sections.IsValidIndex(InSectionIdx) && 
			Model->Nodes.IsValidIndex(InNodeIdx) );
		NumVertices = Model->Nodes(NodeIdx).NumVertices;
    }
	void GetIndices(_WORD* DestIndices,_WORD BaseVertexIndex)
	{ check(0 && "not implemented" ); }
	void GetVertices(BYTE* DestVertices)
	{ check(0 && "not implemented" ); }
	void GetRawVertices( void** DestVertices, INT FirstVertex )
	{ check(0 && "not implemented" ); }
	void GetRawIndices( void** DestIndices )
	{ check(0 && "not implemented" ); }
	FProjectorRenderInfo* GetProjRenderInfo()
	{ return NULL; }
};

/* scion ======================================================================
* class FProjectorRenderBatchBSPNodes
* Author: sz
* 
* Keep track of a list of BSP nodes that are needed to render this projector
* ============================================================================
*/
class FProjectorRenderBatchBSPNodes : public FProjectorRenderBatchDef
{
public:
	INT NumPrims;
	FProjectorRenderBatchBSPNodes() : 
		FProjectorRenderBatchDef(),
		NumPrims(0)
	{
	}
	FProjectorRenderBatchBSPNodes( FProjectorRenderInfo* InRenderInfo ) : 
		FProjectorRenderBatchDef( InRenderInfo ),
		NumPrims(0)
	{
	}
	EBatchType GetBatchType() { return BATCH_DynamicBSP; }
	void Render( FSceneNode* SceneNode,FRenderInterface* RI );
	void AddPrimitive( FProjectorRenderPrimitive* Primitive )
	{
		checkSlow( Primitive );
		RenderPrimList = new(GSceneMem) TList<FProjectorRenderPrimitive*>( Primitive, RenderPrimList );
		NumPrims++;
	}
};

/* scion ======================================================================
* class FProjectorRenderPrimStaticMeshClippedNodes
* Author: sz
* 
* Contains the static mesh and the kDOP nodes that clipped against the
* projector.
* ============================================================================
*/
class FProjectorRenderPrimStaticMeshClippedNodes : public FProjectorRenderPrimitive
{
public:
	// static mesh were clipped geomtery came from
	AActor* StaticMeshActor;
	// indices for kdop nodes that need to be rendered
	_WORD* NodeIndices;	
	INT NumNodeIndices;
	//UMaterial* BaseMaterial;	

	FProjectorRenderPrimStaticMeshClippedNodes( AActor* InStaticMeshActor, TArray<_WORD>& InNodeIndices, UBOOL InbRenderStaticMesh=0  ) :
		StaticMeshActor( InStaticMeshActor ),
		NumNodeIndices( InNodeIndices.Num() ),
		NodeIndices( NULL )
	{
		guard(FProjectorRenderPrimStaticMeshClippedNodes::FProjectorRenderPrimStaticMeshClippedNodes);
		checkSlow( StaticMeshActor );
		checkSlow( !StaticMeshActor->bDeleteMe );

		if( NumNodeIndices )
		{
			// copy kdop node indices
			NodeIndices = New<_WORD>( GSceneMem, NumNodeIndices );
			appMemcpy( NodeIndices, &InNodeIndices(0), NumNodeIndices*sizeof(_WORD) );
		}

		unguard;
	}

	void GetIndices(_WORD* DestIndices,_WORD BaseVertexIndex)
	{ check(0 && "not implemented" ); }
	void GetVertices(BYTE* DestVertices)
	{ check(0 && "not implemented" ); }
	void GetRawVertices( void** DestVertices, INT FirstVertex )
	{ check(0 && "not implemented" ); }
	void GetRawIndices( void** DestIndices )
	{ check(0 && "not implemented" ); }
	FProjectorRenderInfo* GetProjRenderInfo()
	{ return NULL; }
};

/* scion ======================================================================
* class FProjectorRenderBatchStaticMesh
* Author: sz
* 
* Keeps track of a list of static meshes and their kDOP nodes that were 
* interesected by the projector's frustum planes.  This batched list is
* then rendered.  This batched list of primitives corresponds to a single
* projector.
* ============================================================================
*/
class FProjectorRenderBatchStaticMesh : public FProjectorRenderBatchDef
{
public:
	INT NumPrims;
	FProjectorRenderBatchStaticMesh() : 
		FProjectorRenderBatchDef(),
		NumPrims(0)
	{
	}
	FProjectorRenderBatchStaticMesh( FProjectorRenderInfo* InRenderInfo ) : 
		FProjectorRenderBatchDef( InRenderInfo ),
		NumPrims(0)
	{
	}
	EBatchType GetBatchType() { return BATCH_DynamicStaticMesh; }
	void Render( FSceneNode* SceneNode,FRenderInterface* RI );
	void AddClippedStaticMeshPrims( AActor* StaticMeshActor );
private:
	void AddPrimitive( FProjectorRenderPrimitive* Primitive )
	{
		checkSlow( Primitive );
		RenderPrimList = new(GSceneMem) TList<FProjectorRenderPrimitive*>( Primitive, RenderPrimList );
		NumPrims++;
	}
};

#endif //!DYNAMIC_PROJ_STREAM

//
//	FVertexPool
//

class FVertexPool : public FVertexStream
{
private:

	INT				Size,
					BaseClientRevision,
					Revision;
	FVertexStream*	Client;

public:

	FVertexPool*	NextPool;
	INT				LifeTimeFrames;

	// Constructor.

	FVertexPool(INT InSize,FVertexPool* InNext)
	{
		Size = InSize;
		Revision = 0;
		Client = NULL;
		NextPool = InNext;
		LifeTimeFrames = 0;
	}

	// Destructor.

	~FVertexPool()
	{
		if(NextPool)
			delete NextPool;
	}

	// GetClient

	FVertexStream* GetClient() { return Client; }

	// SetClient

	void SetClient(FVertexStream* NewClient);

	// FRenderResource interface.

	virtual INT GetRevision();

	// FVertexStream interface.

	virtual INT GetStride() { check(Client); return Client->GetStride(); }
	virtual INT GetSize() { return Size; }
	virtual UBOOL HintDynamic() { return 1; }
	virtual UBOOL UseNPatches() { return 1; }

	virtual INT GetComponents(FVertexComponent* Components) { check(Client); return Client->GetComponents(Components); }
	virtual void GetStreamData(void* Dest) { check(Client); Client->GetStreamData(Dest); }
	virtual void GetRawStreamData(void ** Dest,INT FirstVertex) { check(Client); Client->GetRawStreamData(Dest,FirstVertex); }
};

//
//	GetVertexPool
//

extern FVertexPool* GetVertexPool(FVertexStream* Client);

//
//	UpdateVertexPools
//

extern void UpdateVertexPools(UViewport* Viewport);

//
// FBspNodeList
//

class FBspNodeList
{
public:

	UModel*	Model;
	INT		SectionIndex,
			NumNodes,
			NumTriangles;
	INT*	Nodes;

	// Constructors.

	FBspNodeList(UModel* InModel,INT InSectionIndex)
	{
		Model = InModel;
		SectionIndex = InSectionIndex;

		NumNodes = 0;
		NumTriangles = 0;
		Nodes = New<INT>(GSceneMem,Model->Sections(SectionIndex).NumNodes);
	}

	FBspNodeList(UModel* InModel)
	{
		Model = InModel;
		SectionIndex = INDEX_NONE;

		NumNodes = 0;
		NumTriangles = 0;
		Nodes = NULL;
	}

	// AddNode

	void AddNode(INT NodeIndex);
};

//
// FBspDrawList
//

class FBspDrawList : public FBspNodeList
{
public:

	TList<class FBspLightDrawList*>*		DynamicLights;
	TList<class FBspProjectorDrawList*>*	Projectors;

	// Constructors.

	FBspDrawList(UModel* InModel,INT InSectionIndex) : FBspNodeList(InModel,InSectionIndex)
	{
		DynamicLights = NULL;
		Projectors = NULL;
	}

	FBspDrawList(UModel* InModel) :
		FBspNodeList(InModel)
	{
		DynamicLights = NULL;
		Projectors = NULL;
	}

	// AddNode

	void AddNode(INT NodeIndex,FDynamicLight** InDynamicLights,INT NumDynamicLights,FProjectorRenderInfo** InDynamicProjectors,INT NumDynamicProjectors,FLevelSceneNode* SceneNode);

	// Render

	void Render(FLevelSceneNode* SceneNode,FRenderInterface* RI,FProjectorRenderInfo** InDynamicProjectors,INT NumDynamicProjectors);
};

//
// FBspLightDrawList
//

class FBspLightDrawList : public FBspNodeList
{
public:

	FDynamicLight*	Light;

	// Constructor.

	FBspLightDrawList(FBspDrawList* Parent,FDynamicLight* InLight) :
		FBspNodeList(Parent->Model,Parent->SectionIndex)
	{
		Light = InLight;
	}

	// Render

	void Render(FLevelSceneNode* SceneNode,FRenderInterface* RI);
};

//
//	FBspStencilDrawList
//

class FBspStencilDrawList : public FBspNodeList
{
public:

	DWORD	StencilMask;

	FBspStencilDrawList(UModel* InModel,INT InSectionIndex,DWORD InStencilMask) : FBspNodeList(InModel,InSectionIndex)
	{
		StencilMask = InStencilMask;
	}

	// Render

	void Render(FLevelSceneNode* SceneNode,FRenderInterface* RI);
};

//
//	GetRelevantLights
//

extern TList<FDynamicLight*>* GetRelevantLights(FSceneNode* SceneNode,FDynamicActor* DynamicActor,FSphere& RealBoundingSphere,FDynamicLight** Consider,INT NumConsider);

//
//	RenderLevel
//

extern void RenderLevel(FLevelSceneNode* SceneNode,FRenderInterface* RI);

//
//	CalculateStaticMeshLighting
//

extern void CalculateStaticMeshLighting(UStaticMesh* StaticMesh,UStaticMeshInstance* StaticMeshInstance,FDynamicActor* Owner);

//
//	RenderStaticMesh
//

extern void RenderStaticMesh(FDynamicActor* Owner,FLevelSceneNode* SceneNode,TList<FDynamicLight*>* Lights,TList<FProjectorRenderInfo*>* Projectors,FRenderInterface* RI);

//
//	FStaticMeshBatchList
//

struct FStaticMeshBatchList
{
	ULevel*	Level;
	INT*	Batches;
	INT**	VisibleBatchElements;
	INT*	NumVisibleBatchElements;
	INT		NumBatches;

	// Render

	void Render(FSceneNode* SceneNode,FRenderInterface* RI);
};

//
//	RenderBatchedStaticMesh
//

extern TList<FProjectorRenderBatch*>*	GProjectorBatchList;

extern void RenderBatchedStaticMesh(FStaticMeshBatchList& BatchList,FDynamicActor* Owner,FLevelSceneNode* SceneNode,FRenderInterface* RI);

//
//	FStaticBspProjectorPrimitive
//
class FStaticBspProjectorPrimitive : public FStaticProjectorPrimitive
{
public:
	// Constructor.
	FStaticBspProjectorPrimitive(FStaticProjectorInfo* InProjectorInfo);
	// GetIndices
	virtual void GetIndices(_WORD* DestIndices,_WORD BaseVertexIndex);
};

//
//  FDynamicBspProjectorPrimitive
//
class FDynamicBspProjectorPrimitive : public FProjectorRenderPrimitive
{
public:

	UMaterial*				BaseMaterial;
	FProjectorRenderInfo*	RenderInfo;
	FProjectorBatchVertex*  Vertices;
	_WORD*					Indices;

	// Constructor.
	FDynamicBspProjectorPrimitive(UMaterial* InBaseMaterial,FProjectorRenderInfo* InRenderInfo,FProjectorBatchVertex* InVertices,INT InNumVertices);
	// GetIndices
	virtual void GetIndices(_WORD* DestIndices,_WORD BaseVertexIndex);
	// GetVertices
	virtual void GetVertices(BYTE* DestVertices);
	// scion sz -
	virtual void GetRawVertices( void** DestVertices, INT FirstVertex );
	// scion sz -
	virtual void GetRawIndices( void** DestIndices );
	// scion sz -
	FORCEINLINE FProjectorRenderInfo* GetProjRenderInfo()
	{
		return RenderInfo;
	}

};
