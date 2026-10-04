/*=============================================================================
	UnStaticMesh.h: Static mesh class definition.
	Copyright 1997-2000 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/

//scion jg -- Version 11 contains kDOP tree support for collision
//jg -- Version 12 includes the compression scale support
//jg -- Version 13 includes the compression offset (learned this from skeletal)
//jg -- Version 14 includes correct wireframe for tristripped meshes
#define STATICMESH_VERSION 14

//
//	FStaticMeshVertex
//
#if SUPPORTS_PRAGMA_PACK
#pragma pack (push,1)
#endif
struct ENGINE_API FStaticMeshVertex
{
#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
	//jg -- Use compressed versions for both types
	FPackedPosition Position;
	FPackedNormal Normal;
#else
	FVector Position;
	FVector Normal;
#endif

	// Serializer.

	ENGINE_API friend FArchive& operator<<(FArchive& Ar,FStaticMeshVertex& V)
	{
		return Ar << V.Position << V.Normal;
	}
};
#if SUPPORTS_PRAGMA_PACK
#pragma pack (pop)
#endif


//
//	FStaticMeshVertexStream
//

class ENGINE_API FStaticMeshVertexStream : public FVertexStream
{
public:
#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
	//scion jg -- Load static meshes directly into D3D ram
	TUmaArray<FStaticMeshVertex> Vertices;
#else
	TArray<FStaticMeshVertex>	Vertices;
#endif
	INT Revision;

	// Constructor.
	FStaticMeshVertexStream()
	{
		Revision = 0;
#ifdef _XBOX
		bIsUMA = 1;
#endif
	}

// FVertexStream interface.
	virtual INT GetSize()
	{
		return Vertices.Num() * sizeof(FStaticMeshVertex);
	}
	virtual INT GetStride()
	{
		return sizeof(FStaticMeshVertex);
	}
	virtual void GetStreamData(void* Dest)
	{
		appMemcpy(Dest,&Vertices(0),Vertices.Num() * sizeof(FStaticMeshVertex));
	}
	virtual void GetRawStreamData(void ** Dest, INT FirstVertex )
	{
		*Dest = &Vertices(FirstVertex);
	}
	virtual INT GetComponents(FVertexComponent* OutComponents)
	{
#ifdef _XBOX//jg -- Use compressed components
		OutComponents[0].Type = CT_NormShort3;
		OutComponents[0].Function = FVF_Position;
		OutComponents[1].Type = CT_NormPacked3;
		OutComponents[1].Function = FVF_Normal;
#else
		OutComponents[0].Type = CT_Float3;
		OutComponents[0].Function = FVF_Position;
		OutComponents[1].Type = CT_Float3;
		OutComponents[1].Function = FVF_Normal;
#endif
		return 2;
	}

// FRenderResource interface.
	virtual INT GetRevision()
	{
		return Revision;
	}

	// Serializer.
	ENGINE_API friend FArchive& operator<<(FArchive& Ar,
		FStaticMeshVertexStream& S)
	{
		return Ar << S.Vertices << S.Revision;
	}
};

//
//	FStaticMeshColorStream
//

class ENGINE_API FStaticMeshColorStream : public FRawColorStream
{
public:

	// FVertexStream interface.

	virtual INT GetComponents(FVertexComponent* OutComponents)
	{
		OutComponents[0].Type = CT_Color;
		OutComponents[0].Function = FVF_Specular;

		return 1;
	}
};

//
//	FStaticMeshUV
//

struct ENGINE_API FStaticMeshUV
{
#ifdef JG_XBOX //scion jg -- Use 16 bit floats on xbox for space savings
	FPackedUVCoord U;
	FPackedUVCoord V;
#else
	FLOAT U;
	FLOAT V;
#endif

	// Serializer.

	ENGINE_API friend FArchive& operator<<(FArchive& Ar,FStaticMeshUV& UV)
	{
		return Ar << UV.U << UV.V;
	}
};

//
//	FStaticMeshUVStream
//

class ENGINE_API FStaticMeshUVStream : public FVertexStream
{
public:

#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
	//scion jg -- UMA support for static mesh UV streams
	TUmaArray<FStaticMeshUV> UVs;
#else
	TArray<FStaticMeshUV>	UVs;
#endif
	INT						CoordinateIndex;

	INT						Revision;

	// Constructor.

	FStaticMeshUVStream()
	{
		CoordinateIndex = 0;
		Revision = 0;
#ifdef _XBOX
	//scion jg -- UMA support for static mesh UV streams
	bIsUMA = 1;
#endif
	}

	// FVertexStream interface.

	virtual INT GetSize() { return UVs.Num() * sizeof(FStaticMeshUV); }
	virtual INT GetStride() { return sizeof(FStaticMeshUV); }
	virtual INT GetComponents(FVertexComponent* OutComponents)
	{
#ifdef JG_XBOX //scion jg -- Use 16 bit floats on xbox for space savings
		OutComponents[0].Type = CT_NormShort2;
#else
		OutComponents[0].Type = CT_Float2;
#endif
		OutComponents[0].Function = (EFixedVertexFunction) (FVF_TexCoord0 + CoordinateIndex);

		return 1;
	}
	virtual void GetStreamData(void* Dest)
	{
		appMemcpy(Dest,&UVs(0),UVs.Num() * sizeof(FStaticMeshUV));
	}
	virtual void GetRawStreamData(void ** Dest, INT FirstVertex )
	{
		*Dest = &UVs(FirstVertex);
	}

	// FRenderResource interface.

	virtual INT GetRevision() { return Revision; }

	// Serializer.

	ENGINE_API friend FArchive& operator<<(FArchive& Ar,FStaticMeshUVStream& S)
	{
		return Ar << S.UVs << S.CoordinateIndex << S.Revision;
	}
};

//
//	FStaticMeshSection
//	A set of triangles in a static mesh.
//

class ENGINE_API FStaticMeshSection
{
public:

	UMaterial*	LegacyMaterial;	// Moved to the UStaticMesh::Materials array.  Stored for converting old static meshes.

	UBOOL		IsStrip;		// Whether this section is stripped.
	_WORD		FirstIndex,		// The first index in the index buffer used by this section.
				MinVertexIndex,	// The smallest vertex index used by this section.
				MaxVertexIndex,	// The largest vertex used by this section.
				NumTriangles,	// The number of raw triangles in this section.
				NumPrimitives;	// The number of primitives to render, including degenerate triangles used in stripping.

	// Legacy variables.
	_WORD	LegacyFirstIndex;

#ifdef _XBOX
	//scion jg -- If the size of the indices are large enough then this is
	// the precompiled push buffer to use when rendering
	FPushBuffer* pPushBuffer;
#endif
	// Constructor.
	FStaticMeshSection()
	{
		LegacyMaterial = NULL;
		IsStrip = 0;
		FirstIndex = NumTriangles = NumPrimitives = 0;
		MinVertexIndex = MaxVertexIndex = INDEX_NONE;
#ifdef _XBOX
		pPushBuffer = NULL;
#endif
	}

	// Serialization.
	ENGINE_API friend FArchive& operator<<(FArchive& Ar,FStaticMeshSection& S)
	{
		Ar	<< S.IsStrip
			<< S.FirstIndex
			<< S.MinVertexIndex
			<< S.MaxVertexIndex
			<< S.NumTriangles
			<< S.NumPrimitives;

#ifdef _XBOX
		// scion sz - count push buffer bytes
		if( !Ar.IsSaving() && !Ar.IsLoading() )
		{
			if( S.pPushBuffer )
				S.pPushBuffer->Serialize( Ar );
		}
#endif

		return Ar;
	}
};

//
//	FStaticMeshTriangle
//

struct ENGINE_API FStaticMeshTriangle
{
public:

	FVector			Vertices[3];
	FStaticMeshUV	UVs[3][8];
	FColor			Colors[3];
	INT				MaterialIndex;
	DWORD			SmoothingMask;
	INT				NumUVs;

	UMaterial*		LegacyMaterial;
	DWORD			LegacyPolyFlags;

	// Serializer.

	friend FArchive& operator<<(FArchive& Ar,FStaticMeshTriangle& T);
};

//
//	FStaticMeshMaterial
//

class ENGINE_API FStaticMeshMaterial
{
public:

	UMaterial*	Material;
	UBOOL		EnableCollision,
				OldEnableCollision;	// Used to detect when the user has changed EnableCollision. :)

	// Constructor.

	FStaticMeshMaterial(UMaterial* InMaterial)
	{
		Material = InMaterial;
		EnableCollision = OldEnableCollision = 1;
	}

	// Serializer.

	ENGINE_API friend FArchive& operator<<(FArchive& Ar,FStaticMeshMaterial& M)
	{
		return Ar	<< M.Material
					<< AR_INDEX(M.EnableCollision)
					<< AR_INDEX(M.OldEnableCollision);
	}
};

//scion jg -- Include the kDOP code here
#include "UnkDOP.h"


// Bulk data loading printf strings
#define COLORSTREAM_PF	TEXT("%s_CS")
#define ALPHASTREAM_PF	TEXT("%s_AS")
#define VERTEXSTREAM_PF	TEXT("%s_VS")
#define UVSTREAM_PF	TEXT("%s_UV%d")

//
//	UStaticMesh
//

class ENGINE_API UStaticMesh : public UPrimitive
{
	//scion rwd - added auto-precache of static meshes
	DECLARE_CLASS(UStaticMesh,UPrimitive,CLASS_SafeReplace|CLASS_CollapseCategories|CLASS_AutoPrecache,Engine);

public:

	// Rendering data.

	TArray<FStaticMeshSection>				Sections;

	FStaticMeshVertexStream					VertexStream;

	FStaticMeshColorStream					ColorStream,
											AlphaStream;
	TArray<FStaticMeshUVStream>				UVStreams;

	FRawIndexBuffer							IndexBuffer,
											WireframeIndexBuffer;

	TArray<FStaticMeshMaterial>				Materials;

	UModel*									CollisionModel;
	//scion jg -- New kDOP collision code
	FkDOPTree kDOPTree;
	//jg -- Compression scaling vectors
	FVector CompressionScale;
	FVector CompressionInvScale;
	FVector CompressionOffset;
	// For calculating the compression scales & offset for this mesh
	void ComputeCompressionScales(void);

	// Artist-accessible options.
	UBOOL									UseSimpleLineCollision,
											UseSimpleBoxCollision,
											UseVertexColor,
											UseSimpleKarmaCollision,
											//jg -- Tristripping vars
											UseTriStrips,
											OldUseTriStrips,
											bCacheOptimize,
											OldbCacheOptimize;

	// Source data.

	TLazyArray<FStaticMeshTriangle>			RawTriangles;
	INT										InternalVersion;

	// Content authentication key.

	DWORD                                   AuthenticationKey;

	// Legacy variables.

	UVertexBuffer*							LegacyVertexBuffer;
	UIndexBuffer*							LegacyIndexBuffer;
	UIndexBuffer*							LegacyWireframeIndexBuffer;

	// Karma properties
#ifdef WITH_KARMA
	UKMeshProps*							KPhysicsProps;
	TArray<McdGeometryID>					KCollisionGeom;
	TArray<FVector>							KCollisionGeomScale3D;
#endif

	//scion rwd - use a second staticmesh as collision
	UStaticMesh*							CollisionStaticMesh;

	// Constructor.

	UStaticMesh();

	// UObject interface.

	void StaticConstructor();
	virtual void PostEditChange();
	virtual void Serialize(FArchive& Ar);
	virtual void PostLoad();
	virtual void Destroy();
	virtual void Rename( const TCHAR* NewName=NULL, UObject* NewOuter=NULL );

	// UPrimitive interface.

	virtual FBox GetRenderBoundingBox(const AActor* Owner);
	virtual FSphere GetRenderBoundingSphere(const AActor* Owner);
	virtual FBox GetCollisionBoundingBox(const AActor* Owner) const;
	virtual UBOOL UseCylinderCollision( const AActor* Owner );

	virtual void Illuminate(AActor* Owner,UBOOL ChangedOnly);

	//scion jg -- Changed to pass by ref
	virtual UBOOL PointCheck(FCheckResult& Result,AActor* Owner,const FVector& Location,const FVector& Extent,DWORD ExtraNodeFlags);
	//scion jg -- Changed to pass by ref
	virtual UBOOL LineCheck(FCheckResult &Result,AActor* Owner,AActor* Source,const FVector& End,const FVector& Start,const FVector& Extent,DWORD ExtraNodeFlags, DWORD TraceFlags);

	virtual FVector GetEncroachExtent(AActor* Owner);
	virtual FVector GetEncroachCenter(AActor* Owner);

	// UStaticMesh interface.

	FORCEINLINE UMaterial* GetSkin(AActor* Owner,INT Index)
	{
		UMaterial*	Material = Owner->GetSkin(Index);

		if(!Material)
		{
			Material = Materials(Index).Material;

			if(!Material)
				Material = UMaterial::StaticClass()->GetDefaultObject<UMaterial>()->DefaultMaterial;
		}

		return Material;
	}

	void Build();

	//scion rwd - custom precache for meshes
	virtual void Precache(UObject *src = NULL,TMap<UObject*,BYTE> *masterPrecacheList = NULL);

	//jg -- For clearing all pushbuffer references, flushing meshes
	void ClearAllMeshData(void);
	//jg -- For setting all pushbuffer references
	void SetAllMeshData(UBOOL bCheckResult = FALSE);
};

//
//	FStaticMeshLightInfo
//

class FStaticMeshLightInfo
{
public:

	AActor*			LightActor;
	TArray<BYTE>	VisibilityBits;
	UBOOL			Applied;

	// Constructors.

	FStaticMeshLightInfo() {}
	FStaticMeshLightInfo(AActor* InActor)
	{
		LightActor = InActor;
		Applied = 0;
	}

	// Serializer.

	ENGINE_API friend FArchive& operator<<(FArchive& Ar,FStaticMeshLightInfo& L)
	{
		return Ar	<< L.LightActor
					<< L.VisibilityBits
					<< L.Applied;
	}
};

//
//	UStaticMeshInstance
//

class ENGINE_API UStaticMeshInstance : public UObject
{
	DECLARE_CLASS(UStaticMeshInstance,UObject,0,Engine);

public:
	TArray<FStaticMeshLightInfo>	Lights;
	FRawColorStream					ColorStream;

	// Constructor.
	UStaticMeshInstance() {}

	//jg -- PostLoad() is needed to wire up TUmaArray data
	void PostLoad(void);

	// Serialize
	virtual void Serialize(FArchive& Ar);
};
