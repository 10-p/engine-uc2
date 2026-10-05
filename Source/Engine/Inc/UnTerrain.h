/*=============================================================================
	UnTerrain.h: Unreal terrain objects.
	Copyright 1997-2000 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Jack Porter
=============================================================================*/
#if SUPPORTS_PRAGMA_PACK
#pragma pack (push,4)	// M13: script mirrors are laid out with /Zp4 (as the generated headers); 64-bit pointers sit on 4
#endif

/*------------------------------------------------------------------------------
    Misc stuff.
------------------------------------------------------------------------------*/

struct FTerrainVertex
{

#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
	// scion sz - replaced float[3]=position with Word[1]=Height and Byte[2]={X,Y} 
	SWORD Height;
	BYTE OffsetXY[2];	
	//scion jg -- Use packed normals on the xbox
	FPackedNormal Normal;
	FColor	Color;
#else
	FVector	Position;
	FVector	Normal;
	FColor	Color;
	FLOAT U;
	FLOAT V;
#endif 
	
	ENGINE_API friend FArchive& operator<<(FArchive& Ar,FTerrainVertex& V)
	{
#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
		if(Ar.LicenseeVer() == XBOX_PACKAGE_FORMAT)
		{
			// scion sz - updated. no need to check versions since we don't load/save streams
			Ar << V.Height << V.OffsetXY[0] << V.OffsetXY[1] << V.Normal << V.Color;
		}
		return Ar;
#else
		return Ar << V.Position << V.Normal << V.Color << V.U << V.V;
#endif

	}
};

class FTerrainVertexStream : public FVertexStream
{
public:

	TArray<FTerrainVertex>	Vertices;
	INT						Revision;

	// Constructor.

	FTerrainVertexStream()
	{
		Revision = 0;
	}

	// GetRevision

	virtual INT GetRevision()
	{
		return Revision;
	}

	// GetSize

	virtual INT GetSize()
	{
		return Vertices.Num() * sizeof(FTerrainVertex);
	}

	// GetStride

	virtual INT GetStride()
	{
		return sizeof(FTerrainVertex);
	}

	// GetComponents

	virtual INT GetComponents(FVertexComponent* OutComponents)
	{
#ifndef _XBOX		
		OutComponents[0].Type = CT_Float3;
		OutComponents[0].Function = FVF_Position;
		
		OutComponents[1].Type = CT_Float3;
		OutComponents[1].Function = FVF_Normal;
		
		OutComponents[2].Type = CT_Color;
		OutComponents[2].Function = FVF_Diffuse;
		
		OutComponents[3].Type = CT_Float2;
		OutComponents[3].Function = FVF_TexCoord0;
		
		return 4;
#else
		INT n=0;
		OutComponents[n].Type = CT_Short1;
		OutComponents[n++].Function = FVF_GPUReg14;

		OutComponents[n].Type = CT_PByte2;
		OutComponents[n++].Function = FVF_GPUReg15;
		
		OutComponents[n].Type = CT_NormPacked3;
		OutComponents[n++].Function = FVF_Normal;
		
		OutComponents[n].Type = CT_Color;
		OutComponents[n++].Function = FVF_Diffuse;
		return n;
#endif
	}

	// GetStreamData

	virtual void GetStreamData(void* Dest)
	{
		appMemcpy(Dest,&Vertices(0),Vertices.Num() * sizeof(FTerrainVertex));
	}

	// GetRawStreamData

	virtual void GetRawStreamData(void ** Dest, INT FirstVertex )
	{
		*Dest = &Vertices(FirstVertex);
	}

	// Serializer.

	friend ENGINE_API FArchive& operator<<(FArchive& Ar,FTerrainVertexStream& VertexStream)
	{
		return Ar	<< VertexStream.Vertices
					<< VertexStream.Revision;
	}
};

struct FTerrainNormalPair
{
	FVector Normal1;
	FVector Normal2;

	friend FArchive& operator<<( FArchive& Ar, FTerrainNormalPair& N )
	{
		return Ar << N.Normal1 << N.Normal2;
	}
};

enum
{
	TEXMAPAXIS_XY	= 0,
	TEXMAPAXIS_XZ	= 1,
	TEXMAPAXIS_YZ	= 2,
	TEXMAPAXIS_MAX	= 3,
};


enum ESortOrder
{
	SORT_NoSort		= 0,
	SORT_BackToFront= 1,
	SORT_FrontToBack= 2,
};

struct FTerrainLayer
{
	UMaterial*		Texture;
	UTexture*		AlphaMap;		// unloaded
	FLOAT			UScale;
	FLOAT			VScale;
	FLOAT			UPan;
	FLOAT			VPan;
	BYTE			TextureMapAxis;
	FLOAT			TextureRotation;
	FRotator		LayerRotation;
	FMatrix			TextureMatrix;
	FLOAT			KFriction;
	FLOAT			KRestitution;
	UTexture*		LayerWeightMap;	// layer weight map (precomputed from alphamap)
};

struct FDecorationLayer
{
	UBOOL			ShowOnTerrain;
	UTexture*		ScaleMap;
	UTexture*		DensityMap;
	UTexture*		ColorMap;
	UStaticMesh*	StaticMesh;
	FRangeVector	ScaleMultiplier;
	FRange			FadeoutRadius;
	FRange			DensityMultiplier;
	INT				MaxPerQuad;
	INT				Seed;
	UBOOL			AlignToTerrain;
	BYTE			DrawOrder;
	UBOOL			ShowOnInvisibleTerrain;
	UBOOL			LitDirectional;
	UBOOL			DisregardTerrainLighting;
	UBOOL			RandomYaw;
};

struct FDecoInfo
{
	FVector		Location;
	FRotator	Rotation;
	FVector		Scale;
	FVector		TempScale;
	FColor		Color;
	INT			Distance;

	friend FArchive& operator<<( FArchive& Ar, FDecoInfo& DI )
	{
		return Ar << DI.Location << DI.Rotation << DI.Scale << DI.TempScale << DI.Color << DI.Distance;
	}
}; 

struct FDecoSectorInfo
{
	TArray<FDecoInfo>			DecoInfo;
	FVector						Location;
	FLOAT						Radius;

	friend FArchive& operator<<( FArchive& Ar, FDecoSectorInfo& DSI )
	{
		return Ar << DSI.DecoInfo << DSI.Location << DSI.Radius;
	}
};

struct FDecorationLayerData
{
	TArray<FDecoSectorInfo>		Sectors;

	friend FArchive& operator<<( FArchive& Ar, FDecorationLayerData& DLD )
	{
		return Ar << DLD.Sectors;
	}
};

enum ETerrainRenderMethod
{
	RM_WeightMap			= 0,
	RM_CombinedWeightMap	= 1,
	RM_AlphaMap				= 2,
};

//
// Combination of layers, etc.
//
struct FTerrainRenderCombination
{
	TArray<INT> Layers;
	ETerrainRenderMethod RenderMethod;
	UTexture* CombinedWeightMaps;

	friend FArchive& operator<<( FArchive& Ar, FTerrainRenderCombination& C )
	{
		// Must init since we'll read a byte but it's really an INT
		if (Ar.IsLoading())
		{
			C.RenderMethod = RM_WeightMap;
		}
		return Ar << C.Layers << *(BYTE*)(&C.RenderMethod) << C.CombinedWeightMaps;
	}
};


//
// Sector info for terrain layers rendered in a single pass.
//
struct FTerrainSectorRenderPass
{
	ATerrainInfo* TerrainInfo;
	INT RenderCombinationNum;

	TArray<_WORD> Indices;
	INT NumTriangles;
	INT NumIndices;
	DWORD MinIndex;
	DWORD MaxIndex;

	FTerrainSectorRenderPass()
	:	RenderCombinationNum(-1)
	,	NumTriangles		(0)
	,	NumIndices			(0)
	,	MinIndex			(0)
	,	MaxIndex			(0)
	{}

	inline FTerrainRenderCombination* GetRenderCombination(ATerrainInfo* InTerrainInfo);

	friend FArchive& operator<<( FArchive& Ar, FTerrainSectorRenderPass& P )
	{
		return Ar	<< P.RenderCombinationNum
					<< P.Indices << P.NumTriangles << P.NumIndices
					<< P.MinIndex << P.MaxIndex;
	}
};

struct FTerrainSectorLightInfo
{
	AActor*			LightActor;
	TArray<BYTE>	VisibilityBitmap;

	FTerrainSectorLightInfo() {}

	FTerrainSectorLightInfo( AActor* InActor )
		: LightActor(InActor) {}

	friend FArchive& operator<<( FArchive& Ar, FTerrainSectorLightInfo& I )
	{
		return Ar << I.LightActor << I.VisibilityBitmap;
	}
};

/*------------------------------------------------------------------------------
    UTerrainSector.
------------------------------------------------------------------------------*/

class ENGINE_API UTerrainSector : public UObject
{
	DECLARE_CLASS(UTerrainSector,UObject,0,Engine);
	NO_DEFAULT_CONSTRUCTOR(UTerrainSector)

	TArray<FTerrainSectorRenderPass> RenderPasses;
	TArray<FTerrainSectorLightInfo> LightInfos;
	ATerrainInfo* Info;
	FBox BoundingBox;			// Bounding box
	INT QuadsX, QuadsY;			// Dimensions
	INT OffsetX, OffsetY;
	FVector Location;			// Center
	FLOAT	Radius;				// Radius
	TArray<FStaticProjectorInfo*> Projectors;

	INT BlockIndex;
	_WORD VertexStreamOffset;

	FRawIndexBuffer CompleteIndexBuffer;
	INT CompleteNumTriangles;
	INT CompleteNumIndices;
	DWORD CompleteMinIndex;
	DWORD CompleteMaxIndex;

	// Constructor.
	UTerrainSector( ATerrainInfo* InInfo, INT InOffsetX, INT InOffsetY, INT QuadsX, INT QuadsY );

	// UObject interface.
	virtual void Serialize(FArchive& Ar);
	virtual void PostLoad();
	virtual void Destroy();

	// UTerrainSector interface
	void GenerateTriangles();
	void StaticLight( UBOOL Force );
	//scion jg -- Inlined for speed
	FORCEINLINE INT GetGlobalVertex( INT x, INT y );
	FORCEINLINE INT GetLocalVertex( INT x, INT y ) { return x + y*(QuadsX+1); }
	inline UBOOL IsTriangleAll( INT Layer, INT X, INT Y, INT Tri, UBOOL Turned, BYTE AlphaValue );
	UBOOL IsSectorAll( INT Layer, BYTE AlphaValue );
	UBOOL PassShouldRenderTriangle( INT Pass, INT X, INT Y, INT Tri, UBOOL Turned );

	// scion sz - 
	FORCEINLINE FTerrainVertexStream& GetVertexStream();
	// scion sz - 
	INT AddToSectorBlock();
	// scion sz - get world position of vertex at xy offsets relative to sector origin
	FORCEINLINE FVector GetVertexPos( INT x, INT y );
	// scion sz - get vertex normal at xy offsets relative to sector origin
	FORCEINLINE FVector GetVertexNormal( INT x, INT y );

	// Projectors
	void AttachProjector( AProjector* InProjector, FProjectorRenderInfo* InRenderInfo, INT MinQuadX, INT MinQuadY, INT MaxQuadX, INT MaxQuadY );
};

/* scion ======================================================================
* Class: UTerrainSectorBlock
* Author: sz
* 
* Container for a patch of terrain sectors.  Since each block is of a fixed size,
* we can derive each quad's x,y coordinates implicitly from its index.  This
* allows us to reduce the memory overhead of the terrain vertex stream.
* =============================================================================
*/
class ENGINE_API UTerrainSectorBlock : public UObject
{
	DECLARE_CLASS(UTerrainSectorBlock,UObject,0,Engine);
	NO_DEFAULT_CONSTRUCTOR(UTerrainSectorBlock)

	enum { MAX_SPAN_QUADS = 256 };				// total quads that can fit in
												// this block in either direction
												// Always < 256 since we want to use
												// a byte to index 

	INT	OffsetX, OffsetY;						// lower left corner of this group	
	INT SizeX, SizeY;							// 
	FMatrix WorldMat;							// offset concatenated 
	TArray<UTerrainSector*>	Sectors;			// sectors contained by this block
	ATerrainInfo*	TerrainInfo;				// terrain info

	FTerrainVertexStream	VertexStream;		// vertices needed for all sectors
												// in this block

	// UTerrainSectorBlock interface
	UBOOL AddSector( UTerrainSector* Sector );
	UBOOL IsSectorInBlock( UTerrainSector* Sector );
	void CalcWorldMatrix();

	// Constructor
	UTerrainSectorBlock( ATerrainInfo* InTerrainInfo, INT InOffsetX, INT InOffsetY, INT InSizeX, INT InSizeY );	

	// UObject interface.
	virtual void Serialize(FArchive& Ar);
	virtual void PostLoad();
	virtual void Destroy();

};

/*------------------------------------------------------------------------------
    UTerrainPrimitive.
------------------------------------------------------------------------------*/

class ENGINE_API UTerrainPrimitive : public UPrimitive
{
	DECLARE_CLASS(UTerrainPrimitive,UPrimitive,0,Engine);
	NO_DEFAULT_CONSTRUCTOR(UTerrainPrimitive)

	ATerrainInfo* Info;

	// Constructor
	UTerrainPrimitive( ATerrainInfo* InInfo )
		: Info( InInfo ) {}

	// UPrimitive interface
	UBOOL LineCheck(FCheckResult &Result,AActor* Owner,AActor*Source,const FVector& End,const FVector& Start,const FVector& Extent,DWORD ExtraNodeFlags,DWORD TraceFlags);
	UBOOL PointCheck(FCheckResult& Result,AActor* Owner,const FVector& Point,const FVector& Extent,DWORD ExtraNodeFlags);
	FBox GetRenderBoundingBox( const AActor* Owner, UBOOL Exact );
	void Illuminate(AActor* Owner,UBOOL ChangedOnly);

	// UObject interface.
	virtual void Serialize(FArchive& Ar);
};

/*------------------------------------------------------------------------------
    ATerrainInfo.
------------------------------------------------------------------------------*/

struct FSelectedTerrainVertex
{
	INT X, Y;
	INT OldHeight;
	FLOAT Weight, Delta;

	friend FArchive& operator<<( FArchive& Ar, FSelectedTerrainVertex& V )
	{
		return Ar << V.X << V.Y << V.Weight << V.OldHeight << V.Delta;
	}
};

class ENGINE_API ATerrainInfo : public AInfo
{
    DECLARE_CLASS(ATerrainInfo,AInfo,0,Engine)

	// Editor Properties.
	INT							TerrainSectorSize;
	class UTexture*				TerrainMap;		// empty after load
    FVector						TerrainScale;
	FTerrainLayer				Layers[32];
	TArray<FDecorationLayer>	DecoLayers;
	FLOAT						DecoLayerOffset;
    BITFIELD					Inverted:1		GCC_PACK(4);
    BITFIELD					bKCollisionHalfRes:1;
	BYTE						DecoDetailLevel	GCC_PACK(4);

	// Variables.
	UBOOL						JustLoaded		GCC_PACK(4);
	TArray<FDecorationLayerData>DecoLayerData;
	TArray<UTerrainSector*>		Sectors;	
	TArray<FVector>				Vertices;		// empty after load
	INT							HeightmapX;
	INT							HeightmapY;
	INT							SectorsX;
	INT							SectorsY;
	UTerrainPrimitive*			Primitive;
	
	TArray<FTerrainNormalPair>	FaceNormals;	// empty after load
	FCoords						ToWorld;
	FCoords						ToHeightmap;
	TArray<FSelectedTerrainVertex>		SelectedVertices;
	INT							ShowGrid;
	TArray<DWORD>				QuadVisibilityBitmap;
	TArray<DWORD>				EdgeTurnBitmap;
	// scion sz - use array of BYTE indices
	TArray<BYTE>				QuadDomMaterialBitmap; // Dominant material of each quad.	
	TArray<FTerrainRenderCombination> RenderCombinations;	
	TArray<FColor>				VertexColors;
	TArray<FColor>				PaintedColor;		// editor only
	class UTexture*				CollapsedLayers;

	// scion sz - for grouping sectors into optimal patches
	TArray<UTerrainSectorBlock*>	SectorBlocks;

	// Deprecated.
	class UTexture*				OldTerrainMap;
	TArray<_WORD>				OldHeightmap;

	// Constructors.
	ATerrainInfo(); 

	// UObject Interface
	void PostEditChange();
	virtual void Serialize(FArchive& Ar);
	void PostLoad();
	virtual void Destroy();

	// AActor Interface.
	UPrimitive* GetPrimitive() { return Sectors.Num() ? (Primitive ? Primitive : (Primitive = new(this) UTerrainPrimitive(this))): AActor::GetPrimitive(); }
	void CheckForErrors();

	// ATerrainInfo Interface.
	void SetupSectors();

	// scion sz - sector blocks 
	void SetupSectorBlocks();
	FORCEINLINE INT GetSectorBlockSize();
	FORCEINLINE INT GetSectorBlockSpanX();
	FORCEINLINE INT GetSectorBlockSpanY();
	FORCEINLINE INT GetSectorBlockIndex( INT x, INT y );
	FORCEINLINE INT GetSectorIndex( INT x, INT y );
	
	void Update( FLOAT Time, INT StartX=0, INT StartY=0, INT EndX=0, INT EndY=0, UBOOL UpdateLighting=0 );
	//scion jg -- Made inline for faster lookups
	FORCEINLINE INT GetGlobalVertex( INT x, INT y )
	{
		return x + y * HeightmapX;
	}
	// scion sz - get world position of vertex at xy offsets relative to terrain origin
	FORCEINLINE FVector GetVertexPos( INT x, INT y );

	void Render(
		FLevelSceneNode* SceneNode,
		FRenderInterface* RI,
		FVisibilityInterface* VI,
		FDynamicLight** DynamicLights,
		INT NumDynamicLights,
		FProjectorRenderInfo** DynamicProjectors,
		INT NumDynamicProjectors
		);

	// DecoLayers.
	void UpdateDecorations( INT SectorIndex );
	void RenderDecorations(FLevelSceneNode* SceneNode,FRenderInterface* RI,FVisibilityInterface* VI);	

	// Collision
	UBOOL LineCheck( FCheckResult &Result,const FVector& End,const FVector& Start,const FVector& Extent, DWORD TraceFlags, UBOOL CheckInvisibleQuads );
	//scion jg -- Changed to pass FVectors by ref instead of value
	UBOOL LineCheckWithQuad( INT X, INT Y, FCheckResult &Result, const FVector& End, const FVector& Start, const FVector& Extent, DWORD TraceFlags, UBOOL CheckInvisibleQuads );

	UBOOL PointCheck( FCheckResult &Result, const FVector& Point,const FVector& Extent, UBOOL CheckInvisibleQuads=0 );

	// Editor
	UBOOL GetClosestVertex( FVector& InLocation, FVector* InOutput, INT* InX, INT* InY );
	UBOOL SelectVertexX( INT InX, INT InY );
	UBOOL SelectVertex( FVector Location );
	void ConvertHeightmapFormat();
	void SoftSelect( FLOAT InnerRadius, FLOAT OuterRadius );
	void SoftDeselect();
	void SelectVerticesInBox( FBox& InRange );
	void MoveVertices( FLOAT Delta );
	void ResetMove();
	FBox GetSelectedVerticesBounds();
	void UpdateFromSelectedVertices();
    void SmoothColors( void );

	// internal
	void CheckComputeDataOnLoad();
	INT GetRenderCombinationNum( TArray<INT>& Layers, ETerrainRenderMethod RenderMethod );
	void UpdateVertices( FLOAT Time, INT StartX, INT StartY, INT EndX, INT EndY );
	void UpdateTriangles( INT StartX, INT StartY, INT EndX, INT EndY, UBOOL UpdateLighting=0 );
	void CalcCoords();
	void CalcLayerTexCoords();
	FVector GetVertexNormal( INT x, INT y ); 
	void PrecomputeLayerWeights();
	void CombineLayerWeights();



#if __STATIC_LINK || __LINUX__
	BYTE GetLayerAlpha( INT x, INT y, INT Layer, UTexture* InAlphaMap = NULL );
	void SetLayerAlpha( FLOAT x, FLOAT y, INT Layer, BYTE Alpha, UTexture* InAlphaMap = NULL );
	_WORD GetHeightmap( INT x, INT y );
	void SetHeightmap( INT x, INT y, _WORD w );
	FColor GetTextureColor( INT x, INT y, UTexture* InTextureMap );
	void SetTextureColor( INT x, INT y, UTexture* InTextureMap, FColor& Color );
#else
	inline BYTE GetLayerAlpha( INT x, INT y, INT Layer, UTexture* InAlphaMap = NULL );
	inline void SetLayerAlpha( FLOAT x, FLOAT y, INT Layer, BYTE Alpha, UTexture* InAlphaMap = NULL );
	inline _WORD GetHeightmap( INT x, INT y );
	inline void SetHeightmap( INT x, INT y, _WORD w );
	inline FColor GetTextureColor( INT x, INT y, UTexture* Texture );
	inline void SetTextureColor( INT x, INT y, UTexture* Texture, FColor& Color );
#endif
	FORCEINLINE UBOOL GetQuadVisibilityBitmap( INT x, INT y )
	{
		INT BitIndex = x + y * HeightmapX;
		return (QuadVisibilityBitmap(BitIndex>>5) & (1<<(BitIndex&0x1f))) ? 1 : 0;
	}
	void SetQuadVisibilityBitmap( INT x, INT y, UBOOL Visible )
	{
		INT BitIndex = x + y * HeightmapX;
		INT Index = BitIndex>>5;
		DWORD Bitmask = 1 << (BitIndex&0x1f);
		if( Visible )
			QuadVisibilityBitmap(Index) |= Bitmask;
		else
			QuadVisibilityBitmap(Index) &= ~Bitmask;
	}
	FORCEINLINE UBOOL GetEdgeTurnBitmap( INT x, INT y )
	{
		INT BitIndex = x + y * HeightmapX;
		return (EdgeTurnBitmap(BitIndex>>5) & (1<<(BitIndex&0x1f))) ? 1 : 0;
	}
	void SetEdgeTurnBitmap( INT x, INT y, UBOOL Turned )
	{
		INT BitIndex = x + y * HeightmapX;
		INT Index = BitIndex>>5;
		DWORD Bitmask = 1 << (BitIndex&0x1f);
		if( Turned )
			EdgeTurnBitmap(Index) |= Bitmask;
		else
			EdgeTurnBitmap(Index) &= ~Bitmask;
	}	
	FORCEINLINE UMaterial* GetQuadDomMaterialBitmap( INT x, INT y )
	{
		if(QuadDomMaterialBitmap.Num() > 0)
		{
			// scion sz - use dominant material index to return texture from Layers[]
			return (UMaterial*)Layers[ QuadDomMaterialBitmap( x + (y * HeightmapX) ) ].Texture;
		}
		else
			return NULL;
	}
	FORCEINLINE FVector HeightmapToWorld( FVector H )
	{
		return H.TransformPointBy( ToWorld );
	}
	FVector WorldToHeightmap( FVector W )
	{
		return W.TransformPointBy( ToHeightmap );
	}
};

FORCEINLINE INT UTerrainSector::GetGlobalVertex( INT x, INT y )
{
	return (x+OffsetX)+(y+OffsetY)*Info->HeightmapX;
}

/* scion ======================================================================
* UTerrainSector::GetVertexPos
* Author: sz
* 
* Get world space vertex position given an x,y offset relative to the
* origin of this sector
* =============================================================================
*/
FORCEINLINE FVector UTerrainSector::GetVertexPos( INT x, INT y )
{
#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
	// get vertex stream index
	INT Index = GetLocalVertex( x, y ) + VertexStreamOffset;
	// stream vertices just store offsets relative to the
	// origin of the block containing this sector
	// so, we must transform it to world space
	UTerrainSectorBlock* Block = Info->SectorBlocks( BlockIndex );
	FTerrainVertex* Vertex = &GetVertexStream().Vertices(Index);
	FPlane Pos( Vertex->OffsetXY[0], Vertex->OffsetXY[1], Vertex->Height, 1.0f );

#if 0
// scion sz - debug
	FVector XboxVert = Block->WorldMat.TransformFPlane( Pos );
	FVector PCVert = Info->Vertices( GetGlobalVertex(x, y) );
	static UBOOL DEBUGSZ_DoPrint=0;
	if( DEBUGSZ_DoPrint )
	{
		debugf( TEXT("BlockIndex=%d GetGlobalVertex(x=%d,y=%d)=%d Index=%d RawVertex=(%d,%d,%d) XboxVert=%.2f,%.2f,%.2f PCVert=%.2f,%.2f,%.2f"),
			BlockIndex, 
			x,y,GetGlobalVertex(x,y),
			Index,
			Vertex->OffsetXY[0], Vertex->OffsetXY[1], Vertex->Height, 
			XboxVert.X, XboxVert.Y, XboxVert.Z,
			PCVert.X, PCVert.Y, PCVert.Z 
			);
	}
#endif

	return Block->WorldMat.TransformFPlane( Pos );
#else
	// already transformed to world space
	return( Info->Vertices( GetGlobalVertex(x, y) ) );
#endif	
}

/* scion ======================================================================
* UTerrainSector::GetVertexNormal
* Author: sz
* 
* get vertex normal at xy offsets relative to sector origin
* =============================================================================
*/
FORCEINLINE FVector UTerrainSector::GetVertexNormal( INT x, INT y )
{
	// get vertex stream index
	INT Index = GetLocalVertex( x, y ) + VertexStreamOffset;
#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
	return( GetVertexStream().Vertices(Index).Normal.Unpack() );
#else
	return( GetVertexStream().Vertices(Index).Normal );	
#endif
}

/* scion ======================================================================
* UTerrainSector::GetVertexStream
* Author: sz
* 
* return vertex stream associated with this sector
* =============================================================================
*/
FORCEINLINE FTerrainVertexStream& UTerrainSector::GetVertexStream()
{
	checkSlow(Info);
	checkSlow( BlockIndex > -1 && BlockIndex < Info->SectorBlocks.Num() );

	return( Info->SectorBlocks(BlockIndex)->VertexStream );
}

/* scion ======================================================================
* ATerrainInfo::GetVertexPos
* Author: sz
* 
* return max size of a sector block
* =============================================================================
*/
FORCEINLINE INT ATerrainInfo::GetSectorBlockSize() 
{ 
	return UTerrainSectorBlock::MAX_SPAN_QUADS - TerrainSectorSize;
}

/* scion ======================================================================
* ATerrainInfo::GetSectorBlockSpanX
* Author: sz
* 
* num blocks in X direction
* =============================================================================
*/
FORCEINLINE INT ATerrainInfo::GetSectorBlockSpanX()
{
	return appCeil( (FLOAT)HeightmapX / (FLOAT)GetSectorBlockSize() );
}

/* scion ======================================================================
* ATerrainInfo::GetSectorBlockSpanX
* Author: sz
* 
* num blocks in Y direction
* =============================================================================
*/
FORCEINLINE INT ATerrainInfo::GetSectorBlockSpanY()
{
	return appCeil( (FLOAT)HeightmapY / (FLOAT)GetSectorBlockSize() );
}

/* scion ======================================================================
* ATerrainInfo::GetSectorBlockIndex
* Author: sz
* 
* return sector block that contains the coordinate
* =============================================================================
*/
FORCEINLINE INT ATerrainInfo::GetSectorBlockIndex( INT x, INT y )
{
	checkSlow( x < HeightmapX && y < HeightmapY );
	return (x / GetSectorBlockSize()) + (y / GetSectorBlockSize())*GetSectorBlockSpanX();
}

/* scion ======================================================================
* ATerrainInfo::GetSectorIndex
* Author: sz
* 
* calc index into Info->Sectors[] array based x,y coords
* =============================================================================
*/
FORCEINLINE INT ATerrainInfo::GetSectorIndex( INT x, INT y )
{
	checkSlow( x < HeightmapX && y < HeightmapY );
	return( (x / TerrainSectorSize) + (y / TerrainSectorSize) * SectorsX );
}

/* scion ======================================================================
* ATerrainInfo::GetVertexPos
* Author: sz
* 
* Get world space vertex position given an x,y offset relative to the
* origin of this terrain
* =============================================================================
*/
FORCEINLINE FVector ATerrainInfo::GetVertexPos( INT x, INT y )
{
	INT SectorIdx = GetSectorIndex( x, y );
	checkSlow( SectorIdx < Sectors.Num() );
	UTerrainSector* Sector = Sectors( SectorIdx );
	return Sector->GetVertexPos( x - Sector->OffsetX, y - Sector->OffsetY );
}

/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/

#if SUPPORTS_PRAGMA_PACK
#pragma pack (pop)
#endif
