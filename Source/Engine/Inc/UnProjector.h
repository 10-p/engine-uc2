/*=============================================================================
	UnProjector.h: Projected textures & Decals
	Copyright 2000 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

#define DECALREMOVETIME 1.f

// scion sz -
// used to remove unused stream components for projectors on Xbox
#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
#define DYNAMIC_PROJ_STREAM 0
#else
#define DYNAMIC_PROJ_STREAM	1
#endif

class AProjector;

// Bumped whenever a dynamic projector's render info is detached, abandoned or deleted.
// FDynamicActor caches the projectors that hit it (ActorProjectors, raw pointers, no reference) once per value of
// Level->TimeSeconds (Scion's per-tick cache, UnRender.cpp FActorSceneNode::Render and UnRenderVisibility.cpp
// RenderActor). When TimeSeconds does not move from one tick to the next (a TimeDilation so small that the tick is
// below the float's resolution: the split-screen tests' frozen frames), a projector detached in a tick (a pawn's
// shadow re-attaches as it animates) would stay in the cached lists, and RenderStaticMesh would call Render() on an
// info it holds no reference to (removing someone else's), or on a deleted one. The caches are rebuilt when this
// changes.
ENGINE_API extern DWORD GProjectorRenderInfoGeneration;

enum EProjectorFlags
{
	PRF_Projected		= 0x01,
	PRF_ProjectOnUnlit	= 0x02,
	PRF_Gradient		= 0x04,
	PRF_ProjectOnAlpha  = 0x08,
	PRF_ProjectOnBackfaces	= 0x10
};

struct ENGINE_API FProjectorRenderInfo
{
	AProjector*			Projector;			// May not be valid.
	INT					ReferenceCount;		// Number of surfaces, actors etc referencing this data structure
	DOUBLE				LastRenderTime;		// Time of last render
	DOUBLE				Expires;			// Time the projector can be removed, 0 = never
	UMaterial*			Material;
	UTexture*			GradientTexture;

	BYTE				MaterialBlendingOp,
						FrameBufferBlendingOp;
	FMatrix				Matrix,
						InverseMatrix;
	FVector				BoundingBoxCenter,
						BoundingBoxExtent;
	FPlane				FrustumPlanes[6];
	FMatrix				GradientMatrix;
	DWORD				ProjectorFlags;
	// scions sz - projected texture simple bitmap material (if one exists)
	UBitmapMaterial*	ProjectedBitmap;

	FProjectorRenderInfo( AProjector* InProjector, FLOAT InExpires = 0 );
	~FProjectorRenderInfo()
	{
		check(ReferenceCount==0);
		GProjectorRenderInfoGeneration++;
	}
	FORCEINLINE FProjectorRenderInfo* AddReference()
	{
		ReferenceCount++;
		return this;
	}
	FORCEINLINE void RemoveReference()
	{
		if( --ReferenceCount == 0 )
			delete this;
	}
	FORCEINLINE UBOOL Render( DOUBLE Now )
	{
        if( 
			Expires!=0 
		&&  (		UTexture::__Client->Decals==0 
				||	UTexture::__Client->Projectors==0 
				||	UTexture::__Client->Engine->GRenDev->SupportsZBIAS==0
			) 
		)
        {
            RemoveReference();
			return 0;
        }

		// See if this decal has expired.  If so, remove it.
		if( Expires!=0 && (Expires<=Now || (LastRenderTime!=0.0f && (Now-LastRenderTime>DECALREMOVETIME))) )
		{
			RemoveReference();
			return 0;
		}

		LastRenderTime = Now;
		return 1;
	}
	UMaterial* GetMaterial(class FSceneNode* SceneNode,UMaterial* BaseMaterial);

	// scion sz - 
	FORCEINLINE UBitmapMaterial* GetProjectedBitmap()
	{
		return ProjectedBitmap;
	}
	
	ENGINE_API friend FArchive& operator<<( FArchive& Ar, FProjectorRenderInfo* Info )
	{
		return Ar << Info->Material << Info->GradientTexture;
	}
};

#if SUPPORTS_PRAGMA_PACK
#pragma pack (push,1)
#endif
struct ENGINE_API FStaticProjectorVertex
{
	FVector	WorldPosition;
#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
	// scion sz - we need normals to attenuate vertices
	FPackedNormal Normal;
	// ProjectedPosition, Attenuation not needed since they are dynamically
	// generated on the GPU
#else
	FVector	Normal;
	//scion jg -- Calculate this once not every frame it is rendered
	// (thanks ckline of Irrational)	
#endif

#if DYNAMIC_PROJ_STREAM
	FLOAT	Attenuation;
	FVector	ProjectedPosition;
#endif

	// scion sz - serialize these
	ENGINE_API friend FArchive& operator<<( FArchive& Ar, FStaticProjectorVertex& V )
	{
		Ar << V.WorldPosition << V.Normal;
#if DYNAMIC_PROJ_STREAM
		Ar << V.Attenuation << V.ProjectedPosition;
#endif
		return Ar;
	}
	
};
#if SUPPORTS_PRAGMA_PACK
#pragma pack (pop)
#endif

struct ENGINE_API FStaticProjectorUV
{
	FLOAT	U,
			V;

	// scion sz - serialize these
	ENGINE_API friend FArchive& operator<<( FArchive& Ar, FStaticProjectorUV& UV )
	{
		return Ar << UV.U << UV.V;
	}
};

struct ENGINE_API FStaticProjectorInfo
{
	FProjectorRenderInfo*			RenderInfo;
	UMaterial*						BaseMaterial;
	TArray<FStaticProjectorVertex>	Vertices;
	TArray<FStaticProjectorUV>		BaseUVs;	// Only if RenderInfo->BaseMaterialBlendingOp != PB_None or base material has opacity.
	TArray<_WORD>					Indices;
	UBOOL							TwoSided;

	// scion sz - reference count these
	INT								RefCount;
	FStaticProjectorInfo() :
		RefCount(1)
	{
	}
	~FStaticProjectorInfo()
	{
		check( RefCount==0 );
	}
	FORCEINLINE FStaticProjectorInfo* AddRef()
	{
		++RefCount;
		return this;
	}
    FORCEINLINE FStaticProjectorInfo* RemoveRef()
	{
		check( RefCount>0 );
		if( --RefCount == 0 ) {
			delete this;
			return NULL;
		}
		return this;
	}

	INT CalculateFootPrint()
	{
		INT TotalBytes =	Vertices.Num() * sizeof(FStaticProjectorVertex) +
							BaseUVs.Num() * sizeof(FStaticProjectorUV) +
							Indices.Num() * sizeof(_WORD);

		return TotalBytes;
	}

	ENGINE_API friend FArchive& operator<<(FArchive& Ar,FStaticProjectorInfo* ProjectorInfo)
	{
		return Ar << ProjectorInfo->RenderInfo << ProjectorInfo->BaseMaterial;
	}
};

class ENGINE_API UProjectorPrimitive : public UPrimitive
{
	DECLARE_CLASS(UProjectorPrimitive,UPrimitive,0,Engine);

	// scion sz - its singleton
	static UProjectorPrimitive* GProjectorPrimitive;
	static UProjectorPrimitive* GetStaticInstance();

	// UPrimitive interface
	virtual UBOOL UseCylinderCollision( const AActor* Owner );
	virtual FBox GetCollisionBoundingBox( const AActor* Owner ) const;
	//scion jg -- Changed to pass by ref
	virtual UBOOL PointCheck
	(
		FCheckResult	&Result,
		AActor			*Actor,
		const FVector&	Location,
		const FVector&	Extent,
		DWORD           ExtraNodeFlags
	);
	//scion jg -- Changed to pass by ref
	virtual UBOOL LineCheck
	(
		FCheckResult&	Result,
		AActor*			Owner,
		AActor*			Source,
		const FVector&	End,
		const FVector&	Start,
		const FVector&	Extent,
		DWORD           ExtraNodeFlags,
		DWORD			TraceFlags
	);
	virtual void Destroy();
	
	virtual FVector GetEncroachExtent(AActor* Owner) { return GetCollisionBoundingBox(Owner).GetExtent(); }
	virtual FVector GetEncroachCenter(AActor* Owner) { return GetCollisionBoundingBox(Owner).GetCenter(); }
};

//jg -- Used for export/fixup of projector data
#define PROJECTOR_VS_PF TEXT("%s_pvs")

/* scion =====================================================================
* class FStaticProjectorVertexStream
* Author: sz
* ============================================================================
* Vertex stream used by UStaticProjectorClippedPrim
* ============================================================================
*/
class ENGINE_API FStaticProjectorVertexStream : public FVertexStream
{
public:
//jg -- During asset conversion we need to load the PC version and copy
// that data to our TUmaArray so it gets exported correctly
#if !defined(_XBOX) && defined(XBOX_ASSET_FORMAT)
	// Holds PC version
	TArray<FStaticProjectorVertex> Vertices;
	// Holds the converted xbox version
	TUmaArray<FStaticProjectorVertex> XboxVertices;
#elif defined(_XBOX) && defined(XBOX_ASSET_FORMAT)
	TUmaArray<FStaticProjectorVertex> Vertices;
#else
	TArray<FStaticProjectorVertex> Vertices;
#endif
	INT Revision;

	// Constructor.
	FStaticProjectorVertexStream()
	{
		Revision = 0;
#if defined(_XBOX) && defined(XBOX_ASSET_FORMAT)
		bIsUMA = 1;
#endif
	}

	// FVertexStream interface.
	INT GetSize()
	{
		return Vertices.Num() * sizeof(FStaticProjectorVertex);
	}
	INT GetStride()
	{
		return sizeof(FStaticProjectorVertex);
	}
	void GetStreamData(void* Dest)
	{
		appMemcpy(Dest,&Vertices(0),Vertices.Num() * sizeof(FStaticProjectorVertex));
	}
	void GetRawStreamData(void ** Dest, INT FirstVertex )
	{
		*Dest = &Vertices(FirstVertex);
	}
	INT GetComponents(FVertexComponent* OutComponents)
	{
		INT	NumComponents = 0;

		OutComponents[NumComponents].Type = CT_Float3;
		OutComponents[NumComponents].Function = FVF_Position;
		NumComponents++;

#ifdef _XBOX
		// use packed normals on xbox
		OutComponents[NumComponents].Type = CT_NormPacked3;
		OutComponents[NumComponents].Function = FVF_Normal;
		NumComponents++;		
#else
		OutComponents[NumComponents].Type = CT_Float3;
		OutComponents[NumComponents].Function = FVF_Normal;
		NumComponents++;		
#endif
		return NumComponents;
	}

	// FRenderResource interface.
	INT GetRevision()
	{
		return Revision;
	}

	// Serializer.
	ENGINE_API friend FArchive& operator<<(FArchive& Ar, FStaticProjectorVertexStream& S)
	{
//jg -- During asset conversion we need to load the PC version and copy
// that data to our TUmaArray so it gets exported correctly
#if !defined(_XBOX) && defined(XBOX_ASSET_FORMAT)
		// Load via the PC format
		if (Ar.IsSaving() == 0)
		{
			Ar << S.Vertices << S.Revision;
		}
		// Only save out the xbox version
		else
		{
			check(Ar.LicenseeVer() == XBOX_PACKAGE_FORMAT);
			Ar << S.XboxVertices << S.Revision;
		}
		return Ar;
#else
		return Ar << S.Vertices << S.Revision;
#endif
	}
};

/* scion =====================================================================
* class FStaticProjectorUVStream
* Author: sz
* ============================================================================
* UV stream used by UStaticProjectorClippedPrim
* ============================================================================
*/
class ENGINE_API FStaticProjectorUVStream : public FVertexStream
{
public:

	TArray<FStaticProjectorUV>	UVs;
	INT	Revision;

	FStaticProjectorUVStream()
	{
		Revision = 0;
	}

	// FVertexStream interface.
	INT GetSize() 
	{ 
		return UVs.Num() * sizeof(FStaticProjectorUV); 
	}
	INT GetStride() 
	{ 
		return sizeof(FStaticProjectorUV); 
	}
	INT GetComponents( FVertexComponent* OutComponents )
	{
		OutComponents[0].Type = CT_Float2;
		OutComponents[0].Function = FVF_TexCoord0;
		return 1;
	}
	void GetStreamData(void* Dest)
	{
		appMemcpy(Dest,&UVs(0),UVs.Num() * sizeof(FStaticProjectorUV));
	}
	void GetRawStreamData(void ** Dest, INT FirstVertex )
	{
		*Dest = &UVs(FirstVertex);
	}

	// FRenderResource interface.
	INT GetRevision() { return Revision; }

	// Serializer.
	ENGINE_API friend FArchive& operator<<( FArchive& Ar, FStaticProjectorUVStream& S )
	{
		return Ar << S.UVs << S.Revision;
	}
};

/* scion =====================================================================
* class FStaticProjectorIndexBuffer
* Author: sz
* ============================================================================
* Index buffer stream used by UStaticProjectorClippedPrim
* ============================================================================
*/
class FStaticProjectorIndexBuffer : public FRawIndexBuffer
{
public:
	// Constructor.
	FStaticProjectorIndexBuffer()
	{
#ifdef _XBOX
		bIsUMA = 0;
#endif
		Revision = 1;
	}
};

/* scion =====================================================================
* class UStaticProjectorClippedPrim
* Author: sz
* ============================================================================
* Stores vertex stream and indices (or compiled push buffer) for the clipped
* geometry of a single projector. Serialized with AProjector.
* ============================================================================
*/
class ENGINE_API UStaticProjectorClippedPrim : public UPrimitive
{
	DECLARE_CLASS(UStaticProjectorClippedPrim,UPrimitive,0,Engine); 

public:

	// vertex stream for clipped prims
	FStaticProjectorVertexStream VertexStream;
	// uv tex coord stream for base material (not projected material)
	FStaticProjectorUVStream UVStream;
	// index buffer for tri lists
    FStaticProjectorIndexBuffer IndexBuffer;
	// compiled push buffer version of the indices
	FPushBuffer* pPushBuffer;
	// base material if any
	UMaterial* BaseMaterial;

	// Constructor
	UStaticProjectorClippedPrim() :
		pPushBuffer(NULL),
		BaseMaterial(NULL)
	{}

	// UObject interface.
	void StaticConstructor();
	void Serialize( FArchive& Ar );
	void PostLoad();
	void Destroy();	

	// UPrimitive interface
	FBox GetRenderBoundingBox( const AActor* Owner )
	{
		return BoundingBox;
	}
	FSphere GetRenderBoundingSphere( const AActor* Owner )
	{
		return BoundingSphere;
	}

	UBOOL UseCylinderCollision( const AActor* Owner )
	{
		return UProjectorPrimitive::GetStaticInstance()->UseCylinderCollision( Owner );
	}
	FBox GetCollisionBoundingBox( const AActor* Owner ) const
	{
		return UProjectorPrimitive::GetStaticInstance()->GetCollisionBoundingBox( Owner );
	}
	UBOOL PointCheck
	(
		FCheckResult	&Result,
		AActor			*Actor,
		const FVector&	Location,
		const FVector&	Extent,
		DWORD           ExtraNodeFlags
	)
	{
		return UProjectorPrimitive::GetStaticInstance()->PointCheck(
			Result,
			Actor,
			Location,
			Extent,
			ExtraNodeFlags
			);
	}
	UBOOL LineCheck
	(
		FCheckResult&	Result,
		AActor*			Owner,
		AActor*			Source,
		const FVector&	End,
		const FVector&	Start,
		const FVector&	Extent,
		DWORD           ExtraNodeFlags,
		DWORD			TraceFlags
	)
	{
		return UProjectorPrimitive::GetStaticInstance()->LineCheck(
			Result,
			Owner,
			Source,
			End,
			Start,
			Extent,
			ExtraNodeFlags,
			TraceFlags
			);
	}	
	FVector GetEncroachExtent(AActor* Owner) 
	{ 
		return UProjectorPrimitive::GetStaticInstance()->GetCollisionBoundingBox( Owner ).GetExtent(); 
	}
	FVector GetEncroachCenter(AActor* Owner) 
	{ 
		return UProjectorPrimitive::GetStaticInstance()->GetCollisionBoundingBox( Owner ).GetCenter();
	}
};

/* scion =====================================================================
* class FMultiMaterialProjectorInfo
* Author: sz
* ============================================================================
* Manages static array of UProjectorMultiMaterials (one for each blend mode)
* ============================================================================
*/
class FMultiMaterialProjectorInfo
{
public:
	FMultiMaterialProjectorInfo( UMaterial* BaseMaterial, UBOOL bEnableFrustumClip );
	UProjectorMultiMaterial* GetMaterial( EProjectorBlending Type );
	void AddProjectorEntry( FProjectorRenderInfo* ProjRendInfo );
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

