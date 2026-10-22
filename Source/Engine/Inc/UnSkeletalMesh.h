/*=============================================================================
	UnSkeletalMesh.h: Unreal mesh objects.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	UMeshAnimation: Unreal Animation object
	Objects containing skeletal or hierarchical animation keys.
	(Classic vertex animation is stored inside UMesh object.)

	Copyright 1999,200,2001,2002 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* ULodMesh, USkeletalMesh subclassing - Erik
		* Updated to use Chris Hargrove's mesh instancing concepts.
		* Optimized and uses discrete LOD levels, feb 2002 - Erik

=============================================================================*/

#define MESHANIM_VERSION 4

// History:
//  3: added the flextracks (compressed) animation data.
//  4: removes the (unused) RawTracks.
//

// GPU skinning

// scion sz - should be (44) but there is a bug in the bone chunker
#define GPUBONES_MAX  (43)			// 132 constant registers reserved for bones on XBOX .. (132/3)=44
//#define GPUBONES_MAX  (79)		// 24  (GF2+)  => PC: Radeon 9700 has 256 ->  =   (256-18)/3 = 79  max bones..


#ifdef WITH_LIPSINC
	#include "LIPSincSupport.h"
#endif

// Forward decl.
class USkeletalMesh;
class USkeletalMeshInstance;
class UMeshAnimation;
class MotionChunk;

/* scion ======================================================================
* FRawMotionDataBlock
* Author: sz
* 
* Stores all all raw animation data (quats,positions,time values) for all 
* keys of the motion chunks.  The data is stored in a packed form as a
* single block of memory. Serializing animation data in this way should
* speed up load times and reduce the total animation footprint, since there
* are far fewer allocations during serialization.
* ============================================================================
*/
class FRawMotionDataBlock
{
public:
	DWORD dwSize;
	void* pData;
	// Whether this block came from a XPR file or not
	UBOOL bIsInXPR;

	FRawMotionDataBlock() :
	dwSize(0), pData(NULL), bIsInXPR(0)
	{
	}
	~FRawMotionDataBlock()
	{
#if defined(XBOX_ASSET_FORMAT) && defined(_XBOX)
		// If this was from an XPR don't delete since it will get freed
		// when the block is unloaded
		if (pData && bIsInXPR == 0)
#else
		if (pData != NULL)
#endif
		{
			appFree( pData );
		}
	}
	void CopyMoves( TArray<MotionChunk>& Moves );
	void Empty()
	{
#if defined(XBOX_ASSET_FORMAT) && defined(_XBOX)
		// If this was from an XPR don't delete since it will get freed
		// when the block is unloaded
		if (pData && bIsInXPR == 0)
#else
		if (pData != NULL)
#endif
		{
			appFree( pData );
		}

		pData = NULL;
		dwSize = 0;
	}

	//jg -- Used to fix up the pointer and size var from the XPR file
	FORCEINLINE void FixUpData(DWORD Size,void* Data)
	{
		guard(FixUpData);
		check(bIsInXPR);
		// Just copy over the members
		dwSize = Size;
		pData = Data;
		unguard;
	}

	friend FArchive& operator<<( FArchive& Ar, FRawMotionDataBlock& A )
	{
		Ar.CountBytes( A.dwSize, A.dwSize );
		Ar << A.dwSize;
		if( Ar.IsLoading() )
		{
#if defined(XBOX_ASSET_FORMAT)
			A.bIsInXPR = 0;
			// Only load this if we have the converted format
			if (Ar.LicenseeVer() == XBOX_PACKAGE_FORMAT)
			{
				Ar << A.bIsInXPR;
			}
			// Only allocate and serialize if it is NOT in a XPR
			if (A.bIsInXPR == 0)
#endif
			{
				check( !A.pData );
				if( A.dwSize > 0 )
				{
					A.pData = appMalloc( A.dwSize );
					Ar.Serialize( A.pData, A.dwSize );
				}
			}
		}
		else if (Ar.IsSaving())
		{
#if defined(XBOX_ASSET_FORMAT)
			// Only load this if we have the converted format
			if (Ar.LicenseeVer() == XBOX_PACKAGE_FORMAT)
			{
				Ar << A.bIsInXPR;
			}
			// Only serialize if it is NOT in a XPR
			if (A.bIsInXPR == 0)
#endif
			{
				Ar.Serialize( A.pData, A.dwSize );
			}
		}
		return Ar;
	}
};

/* scion ======================================================================
* FArchiveRawAnimBlock
* Author: sz
* 
* Archive used to only traverse elements that are serialized from MotionChunks
* into the raw animation memory block.
* ============================================================================
*/
class FArchiveRawAnimBlock : public FArchive
{
public:
	enum EMode { MODE_MEMCOUNT, MODE_COPY };
	FArchiveRawAnimBlock( class FRawMotionDataBlock* InDataBlock, EMode InMode ) :
		Offset(0), Mode(InMode), DataBlock(InDataBlock), pAr(NULL)
	{
		check(DataBlock);
	}
	FArchiveRawAnimBlock( class FRawMotionDataBlock* InDataBlock, FArchive* InpAr ) :
		Offset(0), DataBlock(InDataBlock), pAr(InpAr)
	{
        check(DataBlock);
		check(pAr);

		ArIsLoading = pAr->IsLoading();
		ArIsSaving = pAr->IsSaving();
	}

	template< class T > void SerializeArray( TArray<T>& A )
	{
		DWORD Size = 0;
		void* pData = NULL;
		
		if( IsLoading() )
		{
			// serialize data and offset from packed animation block						
			Serialize( pData, Size );
			A.SetData( pData, Size );
		}
		else
		{
			// use data and offset from existing arrays
			pData = A.GetData();
			Size = A.Num() * sizeof(T);
			Serialize( pData, Size );
		}
	}

	template< class T > void SerializeItem( T& Item )
	{
		DWORD Size = 0;
		void* pData = NULL;

		if( IsLoading() )
		{
			// serialize data and offset from packed animation block						
			Serialize( pData, Size );
			Item = *((T*)pData);
		}
		else
		{
			// use data and offset from existing arrays
			pData = (void*)&Item;
			Size = sizeof(T);
			Serialize( pData, Size );
		}
	}

	void Serialize( void*& Data, DWORD& Length )
	{
		if( pAr != NULL )
		{
			FArchive& Ar = *pAr;
			// only care about size since the current offset is implicit
            Ar << Length;
			// fill in array values directly when loading
			if( Ar.IsLoading() )
			{
				if( Length > 0 )
				{
#ifdef _XBOX
					Data = (void*)((BYTE*)DataBlock->pData + GetOffset());
#else
					// on PC we want arrays to be able to get modified, resized, etc
					// so the data from the animation block can not be used in-place
					Data = appMalloc( Length );
					appMemcpy( Data, (void*)((BYTE*)DataBlock->pData + GetOffset()), Length );
#endif
				}
			}
		}
		else if( Mode == MODE_COPY )
		{
			// assumes that a block as already been
			// allocated by FRawMotionDataBlock and
			// it has enough room to copy data to
			check( DataBlock->pData );
			check( (GetOffset()+Length) <= DataBlock->dwSize );

            appMemcpy( (BYTE*)DataBlock->pData + GetOffset(), Data, Length );
		}
		// always increment offset
		AddOffset( Length );
	}
	FORCEINLINE DWORD GetOffset()
	{
		return Offset;
	}
	FORCEINLINE void AddOffset( DWORD Amt )
	{
		Offset += Amt;
	}
protected:
	// current offset from start of memory block
	DWORD Offset;
	// determines if the motion data is just counted or actually loaded
	EMode Mode;
	// ptr to the raw motion data (used when copying motion data)
	class FRawMotionDataBlock* DataBlock;
	// another archive that we are acting as a proxy for
	FArchive* pAr;
private:
	FArchiveRawAnimBlock(){}
};

//
// Skeletal Mesh stream for smoothly skinned sections, with compute-skin-at-GetStreamData-callback time.
//
class ENGINE_API FSkinVertexStream : public FVertexStream
{
public:

	UMesh*						AnimMesh;
	UMeshInstance*				MeshInstance;

	INT							Revision;
	UBOOL                       bPartial;

	UBOOL                       bStreamCallback;
	TArray<FAnimMeshVertex>		Vertices;

	// Constructor.
	FSkinVertexStream()
	{
		AnimMesh = NULL;
		MeshInstance=NULL;
		Revision = 1;
		bPartial = 0;
		bStreamCallback = 0;
	}

	// Non-inlines:
	virtual void GetStreamData(void* Dest);
	virtual void GetRawStreamData(void ** Dest, INT FirstVertex );
	virtual INT GetSize();
	virtual INT GetStride();

	// GetRevision
	virtual INT GetRevision()
	{
		return Revision;
	}
	
	// GetComponents
	virtual INT GetComponents(FVertexComponent* OutComponents)
	{
		OutComponents[0].Type = CT_Float3;
		OutComponents[0].Function = FVF_Position;
		OutComponents[1].Type = CT_Float3;
		OutComponents[1].Function = FVF_Normal;
		OutComponents[2].Type = CT_Float2;
		OutComponents[2].Function = FVF_TexCoord0;
		return 3;
	}
	
	// Serialization.
	ENGINE_API friend FArchive& operator<<(FArchive& Ar,FSkinVertexStream& V)
	{
		if( !Ar.IsPersistent() )
		{
			Ar << V.AnimMesh;
			Ar << V.MeshInstance;
		}
		Ar << V.Revision;
		Ar << V.bPartial;
		Ar << V.bStreamCallback;
		Ar << V.Vertices;
		return Ar;
	}
};


/*------------------------------------------------------------------------------------
	FSkinGPUVertexStream.
------------------------------------------------------------------------------------*/

#if SUPPORTS_PRAGMA_PACK
#pragma pack(push,1)
#endif
struct FSkinGPUVertex
{	
#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
	//jg -- Use quantized positions on the xbox
	FPackedPosition Position;
	FPackedNormal	Normal;		// scion sz - DWORD packed normals on xbox
	FPackedUVCoord	U;			// 16 bit floats for UVs
	FPackedUVCoord	V;			//
	FColor			Index;		// scion sz - DWORD packed color used instead of array of floats
	FColor			Weight;		// scion sz - DWORD packed color used instead of array of floats
#else
	FVector			Position;
	FVector			Normal;
	FLOAT			U,V;
	FLOAT			Index[4];
	FLOAT			Weight[4];
#endif
				
	FSkinGPUVertex()
	{}
	FSkinGPUVertex( FVector InPosition )
	{
		Position = InPosition;
	}
	// Serialization.
	ENGINE_API friend FArchive& operator<<(FArchive& Ar,FSkinGPUVertex& V)
	{
		Ar  << V.Position;

// scion sz - 
// normalize unpacked normals when loading 
#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
		if(Ar.LicenseeVer() != XBOX_PACKAGE_FORMAT)
		{
			if( Ar.IsLoading() )
			{
				FVector TempNormal;
				Ar << TempNormal;
				TempNormal = TempNormal.SafeNormal();
				V.Normal = TempNormal;
			}
			else
			{
				Ar << V.Normal;
			}
		}
		else
		{
			Ar << V.Normal;
		}
#else
		Ar << V.Normal;
#endif
		Ar  << V.U
			<< V.V;

#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
		if(Ar.LicenseeVer() != XBOX_PACKAGE_FORMAT)
		{
			if (Ar.IsLoading())
			{
				FLOAT		fIndex[4];
				FLOAT		fWeight[4];

				Ar	<< fIndex[0]
					<< fIndex[1]
					<< fIndex[2]
					<< fIndex[3]
					<< fWeight[0]
					<< fWeight[1]
					<< fWeight[2]
					<< fWeight[3];

				V.Index = FColor( FPlane(fIndex[0]/255.f, fIndex[1]/255.f, fIndex[2]/255.f, fIndex[3]/255.f) );
				V.Weight = FColor( FPlane(fWeight[0], fWeight[1], fWeight[2], fWeight[3]) );				
			}
			else if( Ar.IsSaving() )
			{
				FLOAT		fIndex[4];
				FLOAT		fWeight[4];

				FPlane IndexPlane = FPlane(V.Index);
				FPlane WeightPlane = FPlane(V.Weight);

				fIndex[0] = IndexPlane.X * 255.f;
				fIndex[1] = IndexPlane.Y * 255.f;
				fIndex[2] = IndexPlane.Z * 255.f;
				fIndex[3] = IndexPlane.W * 255.f;

				// normalize weights so they add up to 1.0
				FLOAT fWeightTotal = WeightPlane.X + WeightPlane.Y + WeightPlane.Z + WeightPlane.W;
				if( fWeightTotal > 0 )
				{
					WeightPlane /= fWeightTotal;
				}

				fWeight[0] = WeightPlane.X;
				fWeight[1] = WeightPlane.Y;
				fWeight[2] = WeightPlane.Z;
				fWeight[3] = WeightPlane.W;
				
				Ar	<< fIndex[0]
					<< fIndex[1]
					<< fIndex[2]
					<< fIndex[3]
					<< fWeight[0]
					<< fWeight[1]
					<< fWeight[2]
					<< fWeight[3];
			}
		}
		//jg -- Simply serialize the compact values
		else
		{
			Ar << V.Index << V.Weight;
		}
#else
		// scion sz -
		// PC must still support the old FLOAT arrays as they are needed
		// by UnSkeletalTools::AddNewGPUStreamSection()
		Ar	<< V.Index[0]
			<< V.Index[1]
			<< V.Index[2]
			<< V.Index[3]
			<< V.Weight[0]
			<< V.Weight[1]
			<< V.Weight[2]
			<< V.Weight[3];
#endif

		return Ar;
	}
};

// Optimized vertex for single-bone-influence GPU rendered streams.
struct FSkinGPUMonoVertex
{
	FVector	Position;  // 3d  12
	FVector Normal;    // 3d  12
 	FLOAT	U,V;			 // 2d   8	
	FLOAT   BoneIndex; //	
				
	FSkinGPUMonoVertex()
	{}
	FSkinGPUMonoVertex( FVector InPosition )
	{
		Position = InPosition;
	}
	// Serialization.
	ENGINE_API friend FArchive& operator<<(FArchive& Ar,FSkinGPUMonoVertex& V)
	{
		return Ar
			<< V.Position
			<< V.Normal
			<< V.U
			<< V.V
			<< V.BoneIndex;			
	}
};

#if SUPPORTS_PRAGMA_PACK
#pragma pack(pop)
#endif

// Array specifying bones needed for each GPU-rendered skeletal skin chunk.
// Outside the pack(1) block: packed, the TArray sits at offset 2, where 32-bit ARM faults on its ldrd/strd.
// It serializes field by field, never as raw memory.
struct FGPUBoneChunk
{
	_WORD FirstBoneSlot; // First destination bone slot.
	TArray<_WORD> ChunkBoneIndices; // If zero, all bones can be uploaded for this section.

	ENGINE_API friend FArchive& operator<<(FArchive& Ar,FGPUBoneChunk& F)
	{
		return Ar
			<< F.FirstBoneSlot
			<< F.ChunkBoneIndices;				
	}
};

//jg -- Format of export/fixup string for skeletal mesh vertex streams
#define SKELMESH_VS_PF TEXT("%s_vs_%d")
#define ANIM_SET_PF TEXT("%s_anim")

class ENGINE_API FSkinGPUVertexStream : public FVertexStream
{
public:

	UMesh*						AnimMesh;
	UMeshInstance*				MeshInstance;

	INT							Revision;
	UBOOL                       bPartial;

	UBOOL                       bStreamCallback;

#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
	//jg -- UMA arrays must be enabled for asset conversion now
	// scion sz - use contiguous memory (only one copy instead of 2 and a bit faster during load)
	TUmaArray<FSkinGPUVertex>	Vertices;
#else
	TArray<FSkinGPUVertex>		Vertices;
#endif

	// Constructor.
	FSkinGPUVertexStream()
	{
		AnimMesh = NULL;
		MeshInstance = NULL;
		Revision = 1;
		bPartial = 0;
		bStreamCallback = 0;
#ifdef _XBOX
		bIsUMA = 1;
#endif
	}

	// Non-inlines:
	virtual void GetStreamData(void* Dest);
	virtual void GetRawStreamData(void ** Dest, INT FirstVertex );
	virtual INT GetSize();
	virtual INT GetStride();

	// GetRevision
	virtual INT GetRevision()
	{
		return Revision;
	}
	
	// GetComponents
	virtual INT GetComponents(FVertexComponent* OutComponents)
	{
#ifdef _XBOX
		//jg -- quantized positions use normalized shorts
		OutComponents[0].Type = CT_NormShort3;
#else
		OutComponents[0].Type = CT_Float3;
#endif
		OutComponents[0].Function = FVF_Position;
#ifdef _XBOX
		OutComponents[1].Type = CT_NormPacked3;		// scion sz - DWORD packed normals on xbox		
#else
		OutComponents[1].Type = CT_Float3;
#endif
		OutComponents[1].Function = FVF_Normal;

#ifdef _XBOX
		OutComponents[2].Type = CT_NormShort2;			// scion sz - 16 bit floats for UVs
#else
		OutComponents[2].Type = CT_Float2;
#endif
		OutComponents[2].Function = FVF_TexCoord0;

//		OutComponents[3].Type = CT_Float4;
		OutComponents[3].Type = CT_Color;				// scion sz - DWORD color for indices
		OutComponents[3].Function = FVF_GPUReg13;

//		OutComponents[4].Type = CT_Float4;
		OutComponents[4].Type = CT_Color;				// scion sz - DWORD color for weights
		OutComponents[4].Function = FVF_BlendWeights;

		return 5;
	}

	// Serialization.
	ENGINE_API friend FArchive& operator<<(FArchive& Ar,FSkinGPUVertexStream& V)
	{
		if( !Ar.IsPersistent() )
		{
			Ar << V.AnimMesh;
			Ar << V.MeshInstance;
		}
		Ar << V.Revision;
		Ar << V.bPartial;
		Ar << V.bStreamCallback;
		Ar << V.Vertices;
		return Ar;
	}
};


class ENGINE_API FSkinGPUSingleBoneVertexStream : public FVertexStream
{
public:

	UMesh*						AnimMesh;
	UMeshInstance*				MeshInstance;

	INT							Revision;
	UBOOL                       bPartial;

	UBOOL                       bStreamCallback;
	TArray<FSkinGPUMonoVertex>		Vertices;

	// Constructor.
	FSkinGPUSingleBoneVertexStream()
	{
		AnimMesh = NULL;
		MeshInstance = NULL;
		Revision = 1;
		bPartial = 0;
		bStreamCallback = 0;
	}

	// Non-inlines:
	virtual void GetStreamData(void* Dest);
	virtual void GetRawStreamData(void ** Dest, INT FirstVertex );
	virtual INT GetSize();
	virtual INT GetStride();


	// GetRevision
	virtual INT GetRevision()
	{
		return Revision;
	}
	
	// GetComponents
	virtual INT GetComponents(FVertexComponent* OutComponents)
	{
		OutComponents[0].Type = CT_Float3;
		OutComponents[0].Function = FVF_Position;
		OutComponents[1].Type = CT_Float3;
		OutComponents[1].Function = FVF_Normal;
		OutComponents[2].Type = CT_Float2;
		OutComponents[2].Function = FVF_TexCoord0;
		OutComponents[3].Type = CT_Float1;
		OutComponents[3].Function = FVF_GPUReg13;  		
		return 4;
	}

	// Serialization.
	ENGINE_API friend FArchive& operator<<(FArchive& Ar,FSkinGPUSingleBoneVertexStream& V)
	{
		if( !Ar.IsPersistent() )
		{
			Ar << V.AnimMesh;
			Ar << V.MeshInstance;
		}
		Ar << V.Revision;
		Ar << V.bPartial;
		Ar << V.bStreamCallback;
		Ar << V.Vertices;
		return Ar;
	}
};



// 16-byte FSkinVert with 3d vertex and packed normal.
struct FSkinPoint
{
	FVector   Point;
	FMeshNorm Normal;

	// Serialization.
	friend FArchive& operator<<(FArchive& Ar,FSkinPoint& S)
	{
		Ar		<< S.Point
				<< S.Normal;				
		return Ar;
	}	
};


// Defines
#define MAXSKELANIMCHANNELS (256)    // Arbitrary maxima.
#define MAXSKELBONESCALERS  (256)    //
#define MAXSKELBONEMOVERS   (256)    //


// Mesh section structure - for both animating and static parts.
struct FSkelMeshSection
{
	_WORD       MaterialIndex,  // Material (texture) used for this section. Usually an actor's multi-skins.
	     		FirstIndex,		// The offset of the first index in the index buffer.
				MinIndex,		// The smallest vertex index used by this section.
				MaxIndex,		// The largest vertex index used by this section.
				TotalVerts,     // The total number of vertices
				TotalWedges,    //
				MaxInfluences,  // Largest number of per-vertex bone influences
				ShaderIndex,    // This section might need a custom shader.
				FirstFace,      // First face for this section.
				TotalFaces;     // Faces - at full LOD - for this section

#ifdef _XBOX
	//scion jg -- For compiling indices to a push buffer
	FPushBuffer* pPushBuffer;
	FSkelMeshSection()
	{
		pPushBuffer = NULL;
	}
#endif

	// Serialization.
	friend FArchive& operator<<(FArchive& Ar,FSkelMeshSection& S)
	{		
		Ar		<< S.MaterialIndex
				<< S.FirstIndex
				<< S.MinIndex
				<< S.MaxIndex
				<< S.TotalVerts
				<< S.MaxInfluences
				<< S.ShaderIndex
				<< S.FirstFace
				<< S.TotalFaces;

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


/*-----------------------------------------------------------------------------
	Skeletal Mesh animation controllers
-----------------------------------------------------------------------------*/

struct MeshAnimLinkup
{
	UMeshAnimation* Anim;  // Animation as was linked.
	UMesh*          Mesh;  // Mesh as was linked.
	UMesh*          RefMesh;     // Optional mesh to get refpose adjustments from - to help make non-alike skeletons' animations work.
	TArray <INT>    CachedLinks; // The named-bone links between the animation and mesh.
	//TArray <FCoords>RefBoneAdjusts; // If a reference mesh was set, these adjusts may have to be applied.  At some cost, but only O(bones).

	friend FArchive &operator<<( FArchive& Ar, MeshAnimLinkup& M )
	{
		if( !Ar.IsPersistent() )
		{
			//Ar << M.Anim;
			Ar << M.Mesh;
			Ar << M.RefMesh;
			//Ar << M.CachedLinks;
			//Ar << RefBoneAdjusts;
		}
		return Ar;
	}

};

struct MeshAnimChannel
{	
	FName ChannelID;
	INT   MeshAnimIndex;    // Index into the instance's linked animations in AnimLinkups.

	// Classic variables	
	FName AnimSequence;     //
	FLOAT AnimRate;         //
	FLOAT AnimFrame;        //
	FLOAT AnimLast;         //
	FLOAT TweenRate;        //
	FLOAT OldAnimRate;		// Animation rate of previous animation (= AnimRate until animation completes).
	FLOAT OrigRate;         // 'Native' sequence rate.
	FLOAT AnimRangeStart;   // Optional non-zero Start and non-1.0f End within a sequence - to allows sub-ranges to
	FLOAT AnimRangeEnd;     // be treated as a 'new' animation sequence, and even implied reversed playing.

	INT   bAnimFinished;    //
	INT   bAnimLoop;        //
	INT   bAnimNotify;      // Whether a notify is applied to the current sequence
	INT   bTimedBlend;      // Blend to a destination alpha over a certain time.
	INT   bTweening;        //
	INT   bAltRange;        // Play animation from AnimRangeStart to AnimRangeEnd if set - otherwise default 0-1 range.
	INT   bNotifyIgnore;    // Ignore notifies
	INT   bGlobalPose;      // Evaluate entire skeleton even for partial blends, then use the global rotations..
	
	// Blending
	FLOAT BlendAlpha;       // Alpha for entire blend
	FLOAT BlendInTime;      // Lead-in time (relative to entire animation duration)	
	FLOAT BlendOutTime;     // lead-out rate (relative to entire animation )

	FLOAT BlendTargetInterval; // Remaining interval to blend from current to target alpha (starts whenver this was set.)
	FLOAT BlendTargetAlpha;    //
	FLOAT TweenProgress;    // Value tweens along with animation to create a linear alpha.

    INT   StartBone;        // start bone number

	FLOAT CachedFrame;      // AnimFrame of the last stored skeletal pose..
	FName CachedSeq;	    // Cached animation sequence

	// Ctor defaults: rely on AddZeroed.
	MeshAnimChannel()
	{
	}
	~MeshAnimChannel()
	{
	}	

	friend FArchive &operator<<( FArchive& Ar, MeshAnimChannel& M )
	{
		if( !Ar.IsPersistent() )
		{
			Ar << M.ChannelID;
			Ar << M.MeshAnimIndex;
			Ar << M.AnimSequence;
			Ar << M.AnimRate;
			Ar << M.AnimFrame;
			Ar << M.AnimLast;
			Ar << M.TweenRate;
			Ar << M.OldAnimRate;
			Ar << M.OrigRate;
			Ar << M.AnimRangeStart;
			Ar << M.AnimRangeEnd;
			Ar << M.bAnimFinished;
			Ar << M.bAnimLoop;
			Ar << M.bAnimNotify;
			Ar << M.bTimedBlend;
			Ar << M.bTweening;
			Ar << M.bAltRange;
			Ar << M.bNotifyIgnore;
			Ar << M.bGlobalPose;
			Ar << M.BlendAlpha;
			Ar << M.BlendInTime;
			Ar << M.BlendOutTime;
			Ar << M.BlendTargetAlpha;
			Ar << M.BlendTargetInterval;
			Ar << M.TweenProgress;
			Ar << M.StartBone;
			Ar << M.CachedFrame;
			Ar << M.CachedSeq;
		}
		return Ar;
	}
};

struct MeshBoneScaler
{
	INT		Bone;
	FName	BoneName;
	FLOAT   ScaleUniform;
	FCoords	ScalerTrafo;

	// Serialization.
	friend FArchive& operator<<(FArchive& Ar,MeshBoneScaler& S)
	{
		return Ar
				<< S.Bone
				<< S.BoneName
				<< S.ScaleUniform
				<< S.ScalerTrafo;				
	}	
};

struct MeshBoneDirector
{
	INT		Bone;
	FName	BoneName;
	FRotator	Turn;
	FVector		Trans;
	DWORD       Flags;
	FLOAT       TurnAlpha;
	FLOAT       TransAlpha;
	// #debug add movement restrictors?

	friend FArchive& operator<<(FArchive& Ar, MeshBoneDirector& S)
	{
		return Ar
			<< S.Bone
			<< S.BoneName
			<< S.Turn
			<< S.Trans
			<< S.TurnAlpha
			<< S.TransAlpha;
	}
};



/*-----------------------------------------------------------------------------
	USkeletalMesh.
-----------------------------------------------------------------------------*/

// Note: uses old-style resource loading, then saves it as an USkeletalMesh in the
// .u building phase.  That's why some of these structs are serializable and others aren't.

// A bone: an orientation, and a position, all relative to their parent.
struct VJointPos
{
	FQuat   	Orientation;  //
	FVector		Position;     //  needed or not ?

	FLOAT       Length;       //  For collision testing / debugging drawing...
	FLOAT       XSize;
	FLOAT       YSize;
	FLOAT       ZSize;

	friend FArchive &operator<<( FArchive& Ar, VJointPos& V )
	{
		return Ar << V.Orientation << V.Position << V.Length << V.XSize << V.XSize << V.ZSize;
	}
};

// 16-byte helper structure (PSX2) VBoneInfluence + Bone index, up to 4, for each vertex.
struct VertInfIndex
{
	_WORD InfIndex[4];
	_WORD BoneIndex[4];

	// For garbage collection / objlist size only
	friend FArchive &operator<<( FArchive& Ar, VertInfIndex& V )
	{
		return Ar << V.InfIndex[0] << V.InfIndex[1] << V.InfIndex[2] << V.InfIndex[3]
			      << V.BoneIndex[0]<< V.BoneIndex[1]<< V.BoneIndex[2]<< V.BoneIndex[3];
	}
};

// Reference-skeleton bone, the package-serializable version.
struct FMeshBone
{
	FName 		Name;		  // Bone's name.
	DWORD		Flags;        // reserved
	VJointPos	BonePos;      // reference position
	INT         ParentIndex;  // 0/NULL if this is the root bone.
	INT 		NumChildren;  // children  // only needed in animation ?
	INT         Depth;        // Number of steps to root in the skeletal hierarcy; root=0.
	friend FArchive &operator<<( FArchive& Ar, FMeshBone& F)
	{
		return Ar << F.Name << F.Flags << F.BonePos << F.NumChildren << F.ParentIndex;
	}
};

// Named bone for the animating skeleton data.
struct FNamedBone
{
	FName	   Name;  // Bone's fname (== single 32-bit index to name)
	DWORD      Flags; // reserved
	INT        ParentIndex;  // 0/NULL if this is the root bone.	

#ifndef _XBOX	
	FVector		Position;			// scion sz - ref pose position of this bone
	BYTE		TranslationMethod;	// scion sz - used to force bone to use bone lengths or keyframe translation
#endif
	
	friend FArchive &operator<<( FArchive& Ar, FNamedBone& F)
	{
		Ar << F.Name;
		Ar << F.Flags;
		Ar << F.ParentIndex;		

#ifndef _XBOX
		// scion sz - store ref pose position
		if( Ar.IsLoading() && Ar.Ver() < 130 )
		{
			F.Position = FVector(0,0,0);
		}
		else
		{
			Ar << F.Position;			
		}

		// scion sz - store translation method for each bone
		if( Ar.IsLoading() && Ar.Ver() < 132 )
		{
			F.TranslationMethod = BTM_Automatic;
		}
		else
		{
			Ar << F.TranslationMethod;			
		}
#else
		// scion sz - Position doesn't exist on the xbox so just serialize and throw away
		if( Ar.Ver() >= 130 )
		{
			FVector TempPosition;
			Ar << TempPosition;
		}

		// scion sz - TranslationMethod doesn't exist on the xbox so just serialize and throw away
		if( Ar.Ver() >= 132 )
		{
			BYTE TempTranslationMethod;
			Ar << TempTranslationMethod;
		}
#endif
		return Ar;
	}
};

// Binary bone format to deal with raw animations as generated by various exporters.
struct FNamedBoneBinary
{
	ANSICHAR   Name[64];	// Bone's name
	DWORD      Flags;		// reserved
	INT        NumChildren; //
	INT		   ParentIndex;	// 0/NULL if this is the root bone.
	VJointPos  BonePos;	    //
};


// Binary animation info format - used to organize raw animation keys into FAnimSeqs on rebuild
// Similar to MotionChunkDigestInfo..
struct AnimInfoBinary
{
	ANSICHAR Name[64];     // Animation's name
	ANSICHAR Group[64];    // Animation's group name	

	INT TotalBones;           // TotalBones * NumRawFrames is number of animation keys to digest.

	INT RootInclude;          // 0 none 1 included 		
	INT KeyCompressionStyle;  // Reserved: variants in tradeoffs for compression.
	INT KeyQuotum;            // Max key quotum for compression	
	FLOAT KeyReduction;       // desired
	FLOAT TrackTime;          // explicit - can be overridden by the animation rate
	FLOAT AnimRate;           // frames per second.
	INT StartBone;            // - Reserved: for partial animations.
	INT FirstRawFrame;        //
	INT NumRawFrames;         // NumRawFrames and AnimRate dictate tracktime...
};

// File header structure.
struct VChunkHeader
{
	ANSICHAR	ChunkID[20];  // string ID of up to 19 chars (usually zero-terminated)
	INT			TypeFlag;     // Flags/reserved
    INT         DataSize;     // size per struct following;
	INT         DataCount;    // number of structs/
};

// Raw data material.
struct VMaterial
{
	ANSICHAR            MaterialName[64];
	INT					TextureIndex;  // texture index ('multiskin index')
	DWORD				PolyFlags;     // ALL poly's with THIS material will have this flag.
	INT				    AuxMaterial;   // reserved: index into another material, eg. detailtexture/shininess/whatever.
	DWORD				AuxFlags;      // reserved: auxiliary flags
	INT					LodBias;       // material-specific lod bias
	INT					LodStyle;      // material-specific lod style
};

// Raw data bone.
struct VBone
{
	ANSICHAR    Name[64];     //
	DWORD		Flags;        // reserved / 0x02 = bone where skin is to be attached...	
	INT 		NumChildren;  // children  // only needed in animation ?
	INT         ParentIndex;  // 0/NULL if this is the root bone.
	VJointPos	BonePos;      // reference position
};

// Bone influence blending
struct VBoneInfIndex // ,, ,, contains Index, number of influences per bone (+ N detail level sizers! ..)
{
	_WORD WeightIndex;
	_WORD Number;  // how many to process
	_WORD DetailA;  // how many to process if we're up to 2 max influences
	_WORD DetailB;  // how many to process if we're up to full 3 max influences

	friend FArchive &operator<<( FArchive& Ar, VBoneInfIndex& V )
	{
		return Ar << V.WeightIndex << V.Number << V.DetailA << V.DetailB;
	}
};

struct VBoneInfluence // Weight and bone number
{
	_WORD BoneWeight;
	_WORD BoneIndex;
	friend FArchive &operator<<( FArchive& Ar, VBoneInfluence& V )
	{
		return Ar << V.BoneWeight << V.BoneIndex;
	}
};

// 3d vertex point index into a VBoneBlend array.
struct VWeightIndex
{
	TArray<_WORD> PointIndices;
	INT  WeightBase;
	friend FArchive &operator<<( FArchive& Ar, VWeightIndex& V )
	{
		return Ar << V.PointIndices << V.WeightBase;
	}
};

// Raw data bone influence.
struct VRawBoneInfluence // just weight, vertex, and Bone, sorted later....
{
	FLOAT Weight;
	INT   PointIndex;
	INT   BoneIndex;
};

// An animation key.
struct VQuatAnimKey
{
	FVector		Position;           // relative to parent.
	FQuat       Orientation;        // relative to parent.
	FLOAT       Time;				// The duration until the next key (end key wraps to first...)

	friend FArchive &operator<<( FArchive& Ar, VQuatAnimKey& V )
	{
		return Ar << V.Position << V.Orientation << V.Time;
	}
};

// 'Analog' animation key track (for single bone/element.)
// Either KeyPos or KeyQuat can be single/empty? entries to signify no movement at all;
// for N>1 entries there's always N keytimers available.
struct AnalogTrack
{	
	DWORD Flags;       // reserved
	TArray <FQuat>   KeyQuat;   // Orientation key track
	TArray <FVector> KeyPos;    // Position key track
	TArray <FLOAT>   KeyTime;  // For each key, time when next key takes effect (measured from start of track.)
	friend FArchive &operator<<( FArchive& Ar, AnalogTrack& A )
	{
		Ar << A.Flags;
		// scion sz - 
		// ver 147 serializes arrays from the raw anim block
		if( Ar.Ver() < 147 )
		{
			Ar << A.KeyQuat;
			Ar << A.KeyPos;
			Ar << A.KeyTime;
		}		
		return Ar;
	}

	/* scion ======================================================================
	* operator<< FArchiveRawAnimBlock
	* Author: sz
	* 
	* Only serialize the key track arrays
	* ============================================================================
	*/
	friend FArchiveRawAnimBlock &operator<<( FArchiveRawAnimBlock& Ar, AnalogTrack& A )
	{
		Ar.SerializeArray<FQuat>( A.KeyQuat );
		Ar.SerializeArray<FVector>( A.KeyPos );
		Ar.SerializeArray<FLOAT>( A.KeyTime );	
		return Ar;
	}

	AnalogTrack& operator=( const AnalogTrack& Other)
	{
		guard(AnalogTrack::operator=);
		this->Flags   = Other.Flags;	
		this->KeyQuat = Other.KeyQuat;
		this->KeyPos  = Other.KeyPos;
		this->KeyTime = Other.KeyTime;		
		return *this;
		unguard;
	}

	void Erase()
	{
		guard(AnalogTrack::Erase);
#ifdef _XBOX
		// arrays are in-place so don't try to free
		KeyQuat.SetData( NULL, 0 );
		KeyPos.SetData( NULL, 0 );
		KeyTime.SetData( NULL, 0 );
#else
		KeyQuat.Empty();
		KeyPos.Empty();
		KeyTime.Empty();
#endif
		unguard;
	}	
};

enum FlexAnimDataTypes
{
	ADT_VirtualType			= -2,
	ADT_OldUTCompatible		= -1,
	ADT_Empty				=  0,
	ADT_Static				=  1,
	ADT_Raw     			=  2,	// Lossless standard format.
	ADT_48Bit               =  3,	// quantized rotations
	ADT_48BitRotOnly        =  4,	// quantized rotations, single position entry 
	// scion sz -
	ADT_48BitRotOnlyNoPos   =  5,	// quantized rotations, no position (uses ref pose position)
	ADT_48BitStatic			=  6,	// single quantized rotation, single position entry
	ADT_48BitStaticRotOnly	=  7,	// single quantized rotation, no position (uses ref pose position)
	/*
	// not used
	ADT_32Bit				=  5,
	ADT_32BitRotOnly     	=  6,
	ADT_24Bit				=  7,		
	ADT_Splines				=  8,
	ADT_SplinesRotOnly   	=  9,
	*/
};



// Quantized vector class.
class FVector16
{
public:
	_WORD X,Y,Z;
	
	friend FArchive& operator<<(FArchive& Ar,FVector16& V)
	{
		Ar << V.X;
		Ar << V.Y;
		Ar << V.Z;
		return Ar;
	}	
};

#define Quant16BitDiv     (32767.f)
#define Quant16BitFactor  (32767.f)
#define Quant16BitOffs    (32767)


// Quantized, packed Quaternion - 16 bits per element, W implied.
class FQuat16
{
public:
	 _WORD X,Y,Z;

	 FORCEINLINE FQuat GetQuat()
	 {
		 FLOAT FX = ((INT)X - (INT)Quant16BitOffs) / Quant16BitDiv;
		 FLOAT FY = ((INT)Y - (INT)Quant16BitOffs) / Quant16BitDiv;
		 FLOAT FZ = ((INT)Z - (INT)Quant16BitOffs) / Quant16BitDiv;
		 FLOAT FW = appSqrt( 1.0-FX*FX-FY*FY-FZ*FZ );
		 return FQuat( FX,FY,FZ,FW );
	 }

	 // Constructor from fully normalized quat.
	 FORCEINLINE FQuat16( const FQuat InQuat )
	 {
		 X = (INT)(InQuat.X * Quant16BitFactor) + Quant16BitOffs;
		 Y = (INT)(InQuat.Y * Quant16BitFactor) + Quant16BitOffs;
		 Z = (INT)(InQuat.Z * Quant16BitFactor) + Quant16BitOffs;
	 }

	 friend FArchive& operator<<( FArchive& Ar, FQuat16& Q)
	 {
		 Ar << Q.X;
		 Ar << Q.Y;
		 Ar << Q.Z;
		 return Ar;
	 }

	 FQuat16(){};
};

// Packed Quaternion - full floats, W implied.
class FQuatPacked
{
	public:
	 FLOAT X,Y,Z;

	 FORCEINLINE FQuat GetQuat()
	 {	
		 FLOAT ImpliedWSquared =  1.0f-X*X-Y*Y-Z*Z;
		 FLOAT ImpliedW = ImpliedWSquared > 0.0f  ? appSqrt( ImpliedWSquared ) : 0.0f;
		 return FQuat( X,Y,Z,ImpliedW );
	 }

	 // Constructor from fully normalized quat.
	 FQuatPacked( const FQuat InQuat )
	 {
		 X = InQuat.X;
		 Y = InQuat.Y;
		 Z = InQuat.Z;
	 }

	 // From any explicit quat.
	 FQuatPacked( FLOAT X, FLOAT Y, FLOAT Z, FLOAT W )
	 {
		 FQuat TempQuat(X,Y,Z,W);		
		 if( W<0.f)
		 {
			 TempQuat.W = -TempQuat.W;
			 TempQuat.X = -TempQuat.X;
			 TempQuat.Y = -TempQuat.Y;
			 TempQuat.Z = -TempQuat.Z;			
		 }
		 TempQuat.Normalize();
		 *this = FQuatPacked(TempQuat);
	 }

	 friend FArchive& operator<<( FArchive& Ar, FQuatPacked& Q)
	 {
		 Ar << Q.X;
		 Ar << Q.Y;
		 Ar << Q.Z;
		 return Ar;
	 }

	 FQuatPacked(){};
};

//
// BASE class of all flexible anim data storage types.
// Most subclasses are 'sparse array'-ish implementations to hold
// rotation and translation keys ( nothing fancy, low memory usage above all..)
//
class ENGINE_API FlexTrackBase
{
public:			
	// Constructor.
	FlexTrackBase(){}

	// Type checking. Somewhat kludgy but we want serialization of arrays with a mix of classes without UOBject overhead.
	virtual INT GetClassType()
	{
		return ADT_VirtualType;
	}

	// Deallocate any data contained.
	virtual void Erase() = 0;
	
	// Return an exact byte count of the data contained.
	virtual INT FootPrint() = 0;
	
	virtual void GetRotPos( FLOAT Time, FQuat& OutRot, FVector& OutPos , USkeletalMeshInstance* Inst, INT bRootTrafoOnly, INT SkelBone ){}
	virtual void CopyTrackFrom( const FlexTrackBase* Other ){}
	
	// Type specific serialization.
	virtual void Serialize( FArchive& Ar ) = 0;
	virtual void Serialize( FArchiveRawAnimBlock& Ar ) = 0;
	
	~FlexTrackBase(){}	
};


//
// Completely static track - wastes no space with TArrays.
//
class ENGINE_API FlexTrackStatic : public FlexTrackBase
{
public:

	FQuatPacked Orientation;
	TArray<FVector> Positions;		// scion sz - Num()==0 or Num()==1

	// Constructor.
	FlexTrackStatic(){}

	virtual INT GetClassType()
	{
		return ADT_Static;
	}

	void Erase()
	{
		Orientation = FQuatPacked( 0.f, 0.f, 0.f, 1.f );
#ifdef _XBOX
		// arrays are in-place so don't try to free
		Positions.SetData( NULL, 0 );
#else
		Positions.Empty();
#endif
	}

	virtual INT FootPrint()
	{
		return( 4 + sizeof(FQuatPacked) + sizeof(FVector)*Positions.Num() );
	}

	virtual void GetRotPos( FLOAT Time, FQuat& OutRot, FVector& OutPos , USkeletalMeshInstance* Inst, INT bRootTrafoOnly, INT SkelBone );

	virtual void CopyTrackFrom( const FlexTrackBase* Other )
	{
		Orientation = ((FlexTrackStatic*)Other)->Orientation;
		Positions    = ((FlexTrackStatic*)Other)->Positions;
	}

	friend FArchive &operator<<( FArchive& Ar, FlexTrackStatic& F)
	{
		// scion sz - 
		// ver 147 serializes arrays from the raw anim block
		if( Ar.Ver() < 147 )
		{
			Ar << F.Orientation;
			// scion sz - versions < 130 stored just a single static position
			// versions >= 130 store a list of positions (or empty list)
			if( Ar.IsLoading() && Ar.Ver() < 130 )
			{
				FVector Position;
				Ar << Position;
				F.Positions.Empty();
				F.Positions.AddItem( Position );
			}
			else
			{
				Ar << F.Positions;
			}
		}
		return Ar;
	}

	/* scion ======================================================================
	* operator<< FArchiveRawAnimBlock
	* Author: sz
	* 
	* Only serialize the key track arrays
	* ============================================================================
	*/
	friend FArchiveRawAnimBlock &operator<<( FArchiveRawAnimBlock& Ar, FlexTrackStatic& F )
	{
		Ar.SerializeItem<FQuatPacked>( F.Orientation );
		Ar.SerializeArray<FVector>( F.Positions );
		return Ar;
	}
	virtual void Serialize( FArchiveRawAnimBlock& Ar )
	{
		Ar << *this;
	}

	virtual void Serialize( FArchive& Ar )
	{
		Ar << *this;
	}

	FlexTrackStatic& operator=( const FlexTrackStatic& Other)
	{
		Orientation = Other.Orientation;
		Positions = Other.Positions;	
		return *this;
	}

	~FlexTrackStatic()
	{
		Erase();
	}	

};

/* scion ======================================================================
* FlexTrack48StaticRotOnly
* Author: sz
* 
* Track w/ only a single rotation 
* ============================================================================
*/
class ENGINE_API FlexTrack48StaticRotOnly : public FlexTrackBase
{
public:

	FQuat16 Orientation;

	// Constructor.
	FlexTrack48StaticRotOnly(){}

	virtual INT GetClassType()
	{
		return ADT_48BitStaticRotOnly;
	}

	void Erase()
	{
		Orientation = FQuat16( FQuat(0.f, 0.f, 0.f, 1.f) );
	}

	virtual INT FootPrint()
	{
		return( 4 + sizeof(FlexTrack48StaticRotOnly) );
	}

	virtual void GetRotPos( FLOAT Time, FQuat& OutRot, FVector& OutPos , USkeletalMeshInstance* Inst, INT bRootTrafoOnly, INT SkelBone );

	virtual void CopyTrackFrom( const FlexTrackBase* Other )
	{
		*this = *((FlexTrack48StaticRotOnly*)Other);
	}

	friend FArchive &operator<<( FArchive& Ar, FlexTrack48StaticRotOnly& F)
	{		
		return Ar;
	}

	friend FArchiveRawAnimBlock &operator<<( FArchiveRawAnimBlock& Ar, FlexTrack48StaticRotOnly& F )
	{
		Ar.SerializeItem<FQuat16>( F.Orientation );
		return Ar;
	}
	virtual void Serialize( FArchiveRawAnimBlock& Ar )
	{
		Ar << *this;
	}

	virtual void Serialize( FArchive& Ar )
	{
		Ar << *this;
	}

	FlexTrack48StaticRotOnly& operator=( const FlexTrack48StaticRotOnly& Other)
	{
		Orientation = Other.Orientation;
		return *this;
	}

	~FlexTrack48StaticRotOnly()
	{
		Erase();
	}	

};

/* scion ======================================================================
* FlexTrack48Static
* Author: sz
* 
* Track w/ only a single rotation and position
* ============================================================================
*/
class ENGINE_API FlexTrack48Static : public FlexTrackBase
{
public:

	FQuat16 Orientation;
	FVector Position;

	// Constructor.
	FlexTrack48Static(){}

	virtual INT GetClassType()
	{
		return ADT_48BitStatic;
	}

	void Erase()
	{
		Orientation = FQuat16( FQuat(0.f, 0.f, 0.f, 1.f) );
		Position = GMath.ZeroVector;
	}

	virtual INT FootPrint()
	{
		return( 4 + sizeof(FlexTrack48Static) );
	}

	virtual void GetRotPos( FLOAT Time, FQuat& OutRot, FVector& OutPos , USkeletalMeshInstance* Inst, INT bRootTrafoOnly, INT SkelBone );

	virtual void CopyTrackFrom( const FlexTrackBase* Other )
	{
		*this = *((FlexTrack48Static*)Other);
	}

	friend FArchive &operator<<( FArchive& Ar, FlexTrack48Static& F)
	{		
		return Ar;
	}

	friend FArchiveRawAnimBlock &operator<<( FArchiveRawAnimBlock& Ar, FlexTrack48Static& F )
	{
		Ar.SerializeItem<FQuat16>( F.Orientation );
		Ar.SerializeItem<FVector>( F.Position );
		return Ar;
	}
	virtual void Serialize( FArchiveRawAnimBlock& Ar )
	{
		Ar << *this;
	}

	virtual void Serialize( FArchive& Ar )
	{
		Ar << *this;
	}

	FlexTrack48Static& operator=( const FlexTrack48Static& Other)
	{
		Orientation = Other.Orientation;
		Position = Other.Position;	
		return *this;
	}

	~FlexTrack48Static()
	{
		Erase();
	}	

};

/* scion ======================================================================
* FlexTrack48RotOnlyNoPos
* Author: sz
* 
* Same track information as FlexTrack48RotOnly but no position
* ref pose position data is used
* ============================================================================
*/
class ENGINE_API FlexTrack48RotOnlyNoPos : public FlexTrackBase
{
public:
	// Orientations. If static, then just 1 element.
	TArray<FQuat16> Orientations;
	// TimeKeys: if 0, non-culled (each Quat sits 1 units from its neighbors.
	TArray<_WORD> TimeKeys;
	
	// Constructor
	FlexTrack48RotOnlyNoPos()
	{		
	}			

	virtual INT GetClassType()
	{
		return ADT_48BitRotOnlyNoPos;
	}

	virtual void Erase()
	{
#ifdef _XBOX
		// arrays are in-place so don't try to free
		Orientations.SetData( NULL, 0 );
		TimeKeys.SetData( NULL, 0 );
#else
		Orientations.Empty();		
		TimeKeys.Empty();
#endif
	}		

	virtual INT FootPrint()
	{
		return( 4 + sizeof(FlexTrack48RotOnlyNoPos) + Orientations.Num()*sizeof(FQuat16) + TimeKeys.Num()*sizeof(_WORD) );
	}
	
	virtual void GetRotPos( FLOAT Time, FQuat& OutRot, FVector& OutPos , USkeletalMeshInstance* Inst, INT bRootTrafoOnly, INT SkelBone );

	virtual void CopyTrackFrom( const FlexTrackBase* Other )
	{				
		*this = *((FlexTrack48RotOnlyNoPos*)Other);
	}

	friend FArchive &operator<<( FArchive& Ar, FlexTrack48RotOnlyNoPos& F)
	{
		return Ar;
	}

	friend FArchiveRawAnimBlock &operator<<( FArchiveRawAnimBlock& Ar, FlexTrack48RotOnlyNoPos& F )
	{
		Ar.SerializeArray<FQuat16>( F.Orientations );
		Ar.SerializeArray<_WORD>( F.TimeKeys );	
		return Ar;
	}
	virtual void Serialize( FArchiveRawAnimBlock& Ar )
	{
		Ar << *this;
	}

	virtual void Serialize( FArchive& Ar )
	{
		Ar << *this;
	}

	FlexTrack48RotOnlyNoPos& operator=( const FlexTrack48RotOnlyNoPos& Other)
	{
		Orientations = Other.Orientations;		
		TimeKeys = Other.TimeKeys;
		return *this;
	}

	~FlexTrack48RotOnlyNoPos()
	{
		Erase();		
	}
};

//
// Raw track.
//
class FlexTrackRaw : public FlexTrackBase
{
public:
	// Orientations. If static, then just 1 element.
	TArray<FQuatPacked> Orientations;	
	// Raw If static, then just 1 element.
	TArray<FVector> Positions;	
	TArray<_WORD>   TimeKeys;  // None signifies each Quat sits 1.0 time unit from its neighbors.	

	// Constructor.
	FlexTrackRaw()
	{
	}

	virtual INT GetClassType()
	{
		return ADT_Raw;
	}
		
	virtual void Erase()
	{
#ifdef _XBOX
		// arrays are in-place so don't try to free
		Orientations.SetData( NULL, 0 );
		Positions.SetData( NULL, 0 );
		TimeKeys.SetData( NULL, 0 );
#else
		Orientations.Empty();
		Positions.Empty();
		TimeKeys.Empty();
#endif
	}		

	virtual INT FootPrint()
	{
		return 0;
	}
	// return(1+ 12+12+12+4+ Orientations.Num()*2 + Positions.Num()*12 + TimeKeys.Num()*2 );
	virtual void GetRotPos( FLOAT Time, FQuat& OutRot, FVector& OutPos , USkeletalMeshInstance* Inst, INT bRootTrafoOnly, INT SkelBone )
	{
	}

	virtual void CopyTrackFrom( const FlexTrackBase* Other )
	{		
		Positions    = ((FlexTrackRaw*)Other)->Positions;
		Orientations = ((FlexTrackRaw*)Other)->Orientations;
		TimeKeys     = ((FlexTrackRaw*)Other)->TimeKeys;
	}

	friend FArchive &operator<<( FArchive& Ar, FlexTrackRaw& F)
	{
		// scion sz - 
		// ver 147 serializes arrays from the raw anim block
		if( Ar.Ver() < 147 )
		{
			Ar << F.Orientations;
			Ar << F.Positions;
			Ar << F.TimeKeys;
		}
		return Ar;
	}

	/* scion ======================================================================
	* operator<< FArchiveRawAnimBlock
	* Author: sz
	* 
	* Only serialize the key track arrays
	* ============================================================================
	*/
	friend FArchiveRawAnimBlock &operator<<( FArchiveRawAnimBlock& Ar, FlexTrackRaw& F )
	{
		Ar.SerializeArray<FQuatPacked>( F.Orientations );
		Ar.SerializeArray<FVector>( F.Positions );
		Ar.SerializeArray<_WORD>( F.TimeKeys );
		return Ar;
	}
	virtual void Serialize( FArchiveRawAnimBlock& Ar )
	{
		Ar << *this;
	}

	virtual void Serialize( FArchive& Ar )
	{
		Ar << *this;
	}

	FlexTrackRaw& operator=( const FlexTrackRaw& Other)
	{
		Orientations = Other.Orientations;
		Positions = Other.Positions;
		TimeKeys = Other.TimeKeys;
		return *this;
	}

	~FlexTrackRaw()
	{
		Erase();		
	}
};

//
// Quantized track - animated rotations and positions.
//
class ENGINE_API FlexTrack48 : public FlexTrackBase
{
public:
	// Orientations. If static, then just 1 element.
	TArray<FQuat16> Orientations;
	// TimeKeys: if 0, non-culled (each Quat sits 1 units from its neighbors.
	TArray<_WORD>   TimeKeys;
	// Positions. If static, then just 1 element (ideally should be encoded as a FlexTrack48RotOnly..)
	TArray<FVector> Positions;		
	
	// Constructor
	FlexTrack48()
	{		
	}			

	virtual INT GetClassType()
	{
		return ADT_48Bit;
	}

	virtual void Erase()
	{
#ifdef _XBOX
		// arrays are in-place so don't try to free
		Orientations.SetData( NULL, 0 );
		Positions.SetData( NULL, 0 );
		TimeKeys.SetData( NULL, 0 );
#else
		Orientations.Empty();
		Positions.Empty();
		TimeKeys.Empty();
#endif
	}		

	virtual INT FootPrint()
	{
		return( 4 + sizeof(FlexTrack48) + Orientations.Num() * sizeof(FQuat16) + Positions.Num() * sizeof(FVector) + TimeKeys.Num()*sizeof(_WORD) );
	}
	
	virtual void GetRotPos( FLOAT Time, FQuat& OutRot, FVector& OutPos , USkeletalMeshInstance* Inst, INT bRootTrafoOnly, INT SkelBone );		

	virtual void CopyTrackFrom( const FlexTrackBase* Other )
	{		
		Positions    = ((FlexTrack48*)Other)->Positions;
		Orientations = ((FlexTrack48*)Other)->Orientations;
		TimeKeys     = ((FlexTrack48*)Other)->TimeKeys;
	}

	friend FArchive &operator<<( FArchive& Ar, FlexTrack48& F)
	{
		// scion sz - 
		// ver 147 serializes arrays from the raw anim block
		if( Ar.Ver() < 147 )
		{
			Ar << F.Orientations;
			Ar << F.Positions;
			Ar << F.TimeKeys;
		}
		return Ar;
	}

	/* scion ======================================================================
	* operator<< FArchiveRawAnimBlock
	* Author: sz
	* 
	* Only serialize the key track arrays
	* ============================================================================
	*/
	friend FArchiveRawAnimBlock &operator<<( FArchiveRawAnimBlock& Ar, FlexTrack48& F )
	{
		Ar.SerializeArray<FQuat16>( F.Orientations );
		Ar.SerializeArray<FVector>( F.Positions );
		Ar.SerializeArray<_WORD>( F.TimeKeys );	
		return Ar;
	}
	virtual void Serialize( FArchiveRawAnimBlock& Ar )
	{
		Ar << *this;
	}

	virtual void Serialize( FArchive& Ar )
	{		
		Ar << *this;
	}

	FlexTrack48& operator=( const FlexTrack48& Other)
	{
		Orientations = Other.Orientations;
		Positions = Other.Positions;
		TimeKeys = Other.TimeKeys;
		return *this;
	}

	~FlexTrack48()
	{
		Erase();		
	}
};


//
// Quantized track - animated rotations and positions.
//
class ENGINE_API FlexTrack48RotOnly : public FlexTrackBase
{
public:
	// Orientations. If static, then just 1 element.
	TArray<FQuat16> Orientations;
	// TimeKeys: if 0, non-culled (each Quat sits 1 units from its neighbors.
	TArray<_WORD> TimeKeys;
	// Positions. If static, then just 1 element.
	// scion sz - can be 0 elements
	TArray<FVector> Positions;
	
	// Constructor
	FlexTrack48RotOnly()
	{		
	}			

	virtual INT GetClassType()
	{
		return ADT_48BitRotOnly;
	}

	virtual void Erase()
	{
#ifdef _XBOX
		// arrays are in-place so don't try to free
		Orientations.SetData( NULL, 0 );
		TimeKeys.SetData( NULL, 0 );
		Positions.SetData( NULL, 0 );
#else
		Orientations.Empty();		
		TimeKeys.Empty();
		Positions.Empty();
#endif
	}		

	virtual INT FootPrint()
	{
		return( 4 + sizeof(FlexTrack48RotOnly) + Orientations.Num()*sizeof(FQuat16) + TimeKeys.Num()*sizeof(_WORD) + Positions.Num()*sizeof(FVector) );
	}
	
	virtual void GetRotPos( FLOAT Time, FQuat& OutRot, FVector& OutPos , USkeletalMeshInstance* Inst, INT bRootTrafoOnly, INT SkelBone );

	virtual void CopyTrackFrom( const FlexTrackBase* Other )
	{				
		*this = *((FlexTrack48RotOnly*)Other);
	}

	friend FArchive &operator<<( FArchive& Ar, FlexTrack48RotOnly& F)
	{
		// scion sz - 
		// ver 147 serializes arrays from the raw anim block
		if( Ar.Ver() < 147 )
		{
			Ar << F.Orientations;		
			Ar << F.TimeKeys;
			// scion sz - versions < 130 stored just a single static position
			// versions >= 130 store a list of positions (or empty list)
			if( Ar.IsLoading() && Ar.Ver() < 130 )
			{
				FVector Position;
				Ar << Position;
				F.Positions.Empty();
				F.Positions.AddItem( Position );
			}
			else
			{
				Ar << F.Positions;
			}
		}
		return Ar;
	}

	/* scion ======================================================================
	* operator<< FArchiveRawAnimBlock
	* Author: sz
	* 
	* Only serialize the key track arrays
	* ============================================================================
	*/
	friend FArchiveRawAnimBlock &operator<<( FArchiveRawAnimBlock& Ar, FlexTrack48RotOnly& F )
	{
		Ar.SerializeArray<FQuat16>( F.Orientations );
		Ar.SerializeArray<FVector>( F.Positions );
		Ar.SerializeArray<_WORD>( F.TimeKeys );	
		return Ar;
	}
	virtual void Serialize( FArchiveRawAnimBlock& Ar )
	{
		Ar << *this;
	}

	virtual void Serialize( FArchive& Ar )
	{
		Ar << *this;
	}

	FlexTrack48RotOnly& operator=( const FlexTrack48RotOnly& Other)
	{
		Orientations = Other.Orientations;		
		TimeKeys = Other.TimeKeys;
		Positions = Other.Positions;
		return *this;
	}

	~FlexTrack48RotOnly()
	{
		Erase();		
	}
};



//
//
// Enables us to put multiple track types into a single TArray by using custom allocation for all.
// All that's needed for a new type is the explicit type checking & allocation in the
// serialization's IsLoading() block below.
//
//class ENGINE_API FlexTrackSlot
class FlexTrackSlot
{
public:
	FlexTrackBase* FlexTrackPtr;

	FlexTrackSlot()
	{		
		FlexTrackPtr = NULL;
	}

	void Erase()
	{
		if( FlexTrackPtr )
		{
			FlexTrackPtr->Erase();
			delete FlexTrackPtr;
			FlexTrackPtr = NULL;
		}
	}	

	void AllocateTrack( INT ClassTypeNum )
	{
		if( FlexTrackPtr )
			delete FlexTrackPtr;
		FlexTrackPtr = NULL;

		// All subclasses define their own track types. NOTE - this table must be expanded whenever you add a new track type.
		switch( ClassTypeNum )
		{
			case ADT_48Bit:				
				FlexTrackPtr = (FlexTrackBase*) NEW FlexTrack48;
			break;
			case ADT_48BitRotOnly:
				FlexTrackPtr = (FlexTrackBase*) NEW FlexTrack48RotOnly;
			break;
			// scion sz
			case ADT_48BitRotOnlyNoPos:
				FlexTrackPtr = (FlexTrackBase*) NEW FlexTrack48RotOnlyNoPos;
				break;
			// scion sz
			case ADT_48BitStatic:
				FlexTrackPtr = (FlexTrackBase*) NEW FlexTrack48Static;
				break;
			// scion sz
			case ADT_48BitStaticRotOnly:
				FlexTrackPtr = (FlexTrackBase*) NEW FlexTrack48StaticRotOnly;
				break;
			case ADT_Static:
				FlexTrackPtr = (FlexTrackBase*) NEW FlexTrackStatic;
			break;
			case ADT_Raw:
				FlexTrackPtr = (FlexTrackBase*) NEW FlexTrackRaw;
			break;
			case ADT_Empty:				
			break;
		}				
	}
	
	// Serialization: class-type sensitive; serializes object from the pointer which can be different classes/sizes.
	friend FArchive &operator<<( FArchive& Ar, FlexTrackSlot& F )
	{ 		
		INT ClassTypeNum = ADT_Empty;

		// Ensure properly marked as empty when saving without available track data.
		if( !Ar.IsLoading() && F.FlexTrackPtr )
			ClassTypeNum = F.FlexTrackPtr->GetClassType();

		// 'Explicit' class ID saving/loading - only done during serialization - doesn't take up RAM.
		if( Ar.IsLoading() || Ar.IsSaving() )
			Ar << ClassTypeNum; 			

		// ClassTypeNum Serves as explicit class 'vtable pointer' precursor.
		if( Ar.IsLoading() )
			F.AllocateTrack( ClassTypeNum );	
					
		if( F.FlexTrackPtr && ( ClassTypeNum != ADT_Empty ) )
		{
			F.FlexTrackPtr->Serialize( Ar );						
		}		

		return Ar;
	}

	/* scion ======================================================================
	* operator<< FArchiveRawAnimBlock
	* Author: sz
	* 
	* Only serialize the key track arrays
	* ============================================================================
	*/
	friend FArchiveRawAnimBlock &operator<<( FArchiveRawAnimBlock& Ar, FlexTrackSlot& F )
	{
		if( F.FlexTrackPtr )
		{
			F.FlexTrackPtr->Serialize( Ar );						
		}
		return Ar;
	}

	FlexTrackSlot& operator=( const FlexTrackSlot& Other)
	{			
		Erase();
		INT ClassTypeNum = ADT_Empty;

		if( Other.FlexTrackPtr )
			ClassTypeNum = Other.FlexTrackPtr->GetClassType();

		AllocateTrack( ClassTypeNum );

		// Invoke class-specific copy operator.
		// Already guaranteed that they're (binary) compatible.
		if( FlexTrackPtr && Other.FlexTrackPtr )
			FlexTrackPtr->CopyTrackFrom( Other.FlexTrackPtr );

		return *this;
	}

	~FlexTrackSlot()
	{		
		Erase();		
	}
	
};


//
// Motion chunks as defined by Animsequences in script and/or exported raw data.
// Used during digestion phase only.
//
struct MotionChunkDigestInfo
{	
	FName	Name;		 // Sequence's name.
	FName	Group;		 // Group.	
	INT     RootInclude; // 0=none, 1=include, 2=store only root motion/
	INT     KeyCompressionStyle;
	INT     KeyQuotum;
	FLOAT   KeyReduction;
	FLOAT   TrackTime;
	FLOAT   AnimRate;
	INT     StartBone;
	INT     FirstRawFrame;
	INT     NumRawFrames;

};

// Individual animation;  subgroup of bones with compressed animation.
class ENGINE_API MotionChunk
{
public:
	FVector RootSpeed3D;  // Net 3d speed.
	FLOAT   TrackTime;    // Total time (Same for each track.)
	INT     StartBone;    // If we're a partial-hierarchy-movement, this is the lowest bone.
	INT     InternalVersion; // Used to be DWORD Flags

	// scion sz - not used
	// TArray<INT>           BoneIndices;    // Refbones number of Bone indices (-1 or valid one) to fast-find tracks for a particular bone.

	// Frame-less, compressed animation tracks. NumBones times NumAnims tracks in total
	TArray<AnalogTrack>   AnimTracks;     // Compressed key tracks (one for each bone)
	AnalogTrack           RootTrack;      // Unused: Root tracks for use with cutscenes / special physics modes, in addition to the regular skeletal root track.

	// Flexible multi-format animation data tracks. One for each bone.
	TArray<FlexTrackSlot> FlexTracks;

	// Raw storage - optional, lazy array.
	TLazyArray<FlexTrackRaw> RawTracks;

	friend FArchive &operator<<( FArchive& Ar, MotionChunk& M)
	{
		if( Ar.IsSaving() )
			M.InternalVersion = MESHANIM_VERSION;

		Ar << M.RootSpeed3D;
		Ar << M.TrackTime;
		Ar << M.StartBone;
		Ar << M.InternalVersion;
		// scion sz - removed bone indices
		if( Ar.Ver() < 149 && Ar.IsLoading() )
		{
			TArray<INT> TempBoneIndices;
			Ar << TempBoneIndices;
		}
		Ar << M.AnimTracks;
		Ar << M.RootTrack;

		if( M.InternalVersion >= 3) // 3+ signifies quantized quaternions (flextracks)/
		{						
			Ar << M.FlexTracks;

			if( Ar.IsLoading() && (M.InternalVersion == 3) )
			{				
				Ar << M.RawTracks;				
			}				
		}

		return Ar;
	}

	/* scion ======================================================================
	* operator<< FArchiveRawAnimBlock
	* Author: sz
	* 
	* Serialize all members of the motion chunck that will be stored in the memory
	* block.  Only animation tracks are saved in the raw anim block.
	* ============================================================================
	*/
	friend FArchiveRawAnimBlock &operator<<( FArchiveRawAnimBlock& Ar, MotionChunk& M )
	{
		for( INT i=0; i < M.AnimTracks.Num(); i++ )
		{
			Ar << M.AnimTracks(i);
		}
		Ar << M.RootTrack;		
		for( INT i=0; i < M.FlexTracks.Num(); i++ )
		{
            Ar << M.FlexTracks(i);
		}
		for( INT i=0; i < M.RawTracks.Num(); i++ )
		{
			Ar << M.RawTracks(i);
		}
		return Ar;
	}

	MotionChunk& operator=( const MotionChunk& Other )
	{
		guard(MotionChunk::operator =);
		this->RootSpeed3D = Other.RootSpeed3D;
		this->TrackTime   = Other.TrackTime;
		this->StartBone   = Other.StartBone;
		this->InternalVersion = Other.InternalVersion;
		this->RootTrack   = Other.RootTrack;
		// scion sz - not used
//		this->BoneIndices = Other.BoneIndices;	
		this->AnimTracks  = Other.AnimTracks;

		// Copy FlexTracks per element as it is a non-uniform TArray.		
		this->FlexTracks.Empty();
		this->FlexTracks.AddZeroed( Other.FlexTracks.Num() );
		for( INT t=0; t< Other.FlexTracks.Num(); t++)
		{
			this->FlexTracks(t) = Other.FlexTracks(t);
		}		

		//Other.RawTracks.Load();
		//this->RawTracks.Load();		
/*
		this->RawTracks.Empty();
		this->RawTracks.AddZeroed( Other.RawTracks.Num() );
		for( INT t=0; t< Other.RawTracks.Num(); t++)
		{
			this->RawTracks(t) = Other.RawTracks(t);
		}
*/
		
		return *this;
		unguard;
	}

	void Erase()
	{
		guard(MotionChunk::Erase);
		// scion sz - not used
//		BoneIndices.Empty();
		
		for( INT i=0; i<AnimTracks.Num(); i++)
			AnimTracks(i).Erase();
		AnimTracks.Empty();

		for( INT i=0; i<FlexTracks.Num(); i++)
			FlexTracks(i).Erase();
		FlexTracks.Empty();		

		//RawTracks.Load();
		//for( INT i=0; i<RawTracks.Num(); i++)
		//	RawTracks(i).Erase();
		//RawTracks.Empty();

		unguard;
	}

	INT CalculateMemFootprint( UBOOL RenderDataOnly=0 );

	~MotionChunk()
	{
		Erase();
	}
};


// Temp error structure for compression of animation key tracks.
struct TrackDiffs
{
	FLOAT  BoneBias;         // Bias in error tolerance for this bone (may depend on size, place in hierarchy)
	TArray <FLOAT> QuatErr;  // Error in orientation between this key and next
	TArray <FLOAT> PosErr;   // Error in position between this key and next
};

// Vertex with texturing info, akin to Hoppe's 'Wedge' concept - import only.
struct VVertex
{
	_WORD	PointIndex;	 // Index to a point.
	FLOAT   U,V;         // Scaled to BYTES, rather...-> Done in digestion phase, on-disk size doesn't matter here.
	BYTE    MatIndex;    // At runtime, this one will be implied by the face that's pointing to us.
	BYTE    Reserved;    // Top secret.
};

// Points: regular FVectors (for now..)
struct VPoint
{	
	FVector			Point; // Change into packed integer later IF necessary, for 3x size reduction...
};

// Textured triangle.
struct VTriangle
{
	_WORD   WedgeIndex[3];	 // Point to three vertices in the vertex list.
	BYTE    MatIndex;	     // Materials can be anything.
	BYTE    AuxMatIndex;     // Second material from exporter (unused)
	DWORD   SmoothingGroups; // 32-bit flag for smoothing groups.

	friend FArchive &operator<<( FArchive& Ar, VTriangle& V )
	{
		Ar << V.WedgeIndex[0] << V.WedgeIndex[1] << V.WedgeIndex[2];
		Ar << V.MatIndex << V.AuxMatIndex;
		Ar << V.SmoothingGroups;
		return Ar;
	}

	VTriangle& operator=( const VTriangle& Other)
	{
		guard(VTriangle::operator=);
		this->AuxMatIndex = Other.AuxMatIndex;
		this->MatIndex        =  Other.MatIndex;
		this->SmoothingGroups =  Other.SmoothingGroups;
		this->WedgeIndex[0]   =  Other.WedgeIndex[0];
		this->WedgeIndex[1]   =  Other.WedgeIndex[1];
		this->WedgeIndex[2]   =  Other.WedgeIndex[2];
		return *this;
		unguard;
	}
};

struct FVertInfluence
{
	FLOAT Weight;
	_WORD VertIndex;
	_WORD BoneIndex;
	friend FArchive &operator<<( FArchive& Ar, FVertInfluence& F )
	{
		return Ar << F.Weight << F.VertIndex << F.BoneIndex;
	}
};

// Bundle of raw data - read from binary .PSK file skin/reference-skeleton data.
struct USkelImport
{
	TArray <VMaterial>   Materials; // Materials
	TArray <FVector>	 Points;    // 3D Points
	TArray <VVertex>     Wedges;    // Wedges
	TArray <VTriangle>   Faces;     // Faces
	TArray <VBone>       RefBonesBinary;   // Reference skeleton
	TArray <VRawBoneInfluence> Influences; //	
};

// Skeletal (render-ready) wedge.
struct FSkelWedge
{
	FLOAT  			TexU;			// Texture UV coordinates.
	FLOAT           TexV;			//
	_WORD           VertexIndex;    // Index into 3D vector array.
	
	friend FArchive &operator<<( FArchive& Ar, FSkelWedge& T )
	{
		Ar << T.TexU << T.TexV << T.VertexIndex;
		return Ar;
	}

	FSkelWedge& operator=( const FSkelWedge& Other )
	{		
		this->TexU = Other.TexU;
		this->TexV = Other.TexV;
		this->VertexIndex = Other.VertexIndex;
		return *this;
	}
};

// Colored normals for debug display as set by ComputeSkinVerts.
struct FVisNormal
{
	FVector Vertex;
	DWORD   InfluenceCount;
	FVector Normal;
	DWORD   MainBoneIndex;
	
	friend FArchive &operator<<( FArchive& Ar, FVisNormal& V )
	{
		Ar << V.Vertex << V.InfluenceCount << V.Normal << V.MainBoneIndex;
		return Ar;
	}
};

//
// Skeletal Bone Collision Primitive - Sphere
//
struct FSkelBoneSphere
{
	FName		BoneName;
	FVector		Offset;
	FLOAT		Radius;

	friend FArchive &operator<<( FArchive& Ar, FSkelBoneSphere& V )
	{
		Ar << V.BoneName << V.Offset << V.Radius;
		return Ar;
	}
};

//
// Skeletal Bone Collision Primitive - Sphere
//
struct FSkelBoneBox
{
	FName		BoneName;
	FVector		Offset;
	FVector		Radii;

	friend FArchive &operator<<( FArchive& Ar, FSkelBoneBox& V )
	{
		Ar << V.BoneName << V.Offset << V.Radii;
		return Ar;
	}
};

#ifdef WITH_KARMA
class FKBoneLifter
{
public:
	FInterpCurveInit		LiftVel;
	FInterpCurveInit		Softness;

	DWORD				BoneIndex;
	FLOAT				LateralFriction;
	FLOAT				CurrentTime;
	MdtContactGroupID	LiftContact;

	FKBoneLifter(DWORD InBoneIndex , FInterpCurveInit* InLiftVel, FLOAT InLateralFriction, FInterpCurveInit* InSoftness)
	:	BoneIndex(InBoneIndex)
	,	LateralFriction(InLateralFriction)
	{
		LiftVel = *InLiftVel;
		Softness = *InSoftness;

		CurrentTime = 0.f;
		LiftContact = 0;
	}	
};
#endif

// All data to define a certain LOD model for a mesh.
class ENGINE_API FStaticLODModel
{
public:
	//
	// All necessary data to render smooth-parts is in SkinningStream, SmoothVerts, SmoothSections and SmoothIndexbuffer.
	// For rigid parts: RigidVertexStream, RigidIndexBuffer, and RigidSections.
	//

	// Essential direct-render data.	
	TArray<DWORD>        SkinningStream; // UV's, bone indices, weights.
	TArray<FSkinPoint>   SmoothVerts;        // Vertices and normals.
	INT					 SmoothStreamWedges; // # of wedges the SkinningStream will produce.

	// Sections.
	TArray<FSkelMeshSection> SmoothSections;
	TArray<FSkelMeshSection> RigidSections;

	// Software/rigid-section rendering buffers.
	FRawIndexBuffer      SmoothIndexBuffer;
	FRawIndexBuffer      RigidIndexBuffer;
	FSkinVertexStream    RigidVertexStream;	

	//  GPU rendering buffers.
	TArray<FSkelMeshSection>	GPUMultiBoneSections;		
	FRawIndexBuffer						GPUIndexBuffer;
	FSkinGPUVertexStream			GPUVertexStream;	
    TArray<FGPUBoneChunk>    GPUBoneChunks;

	// TArray<FSkelMeshSection>	GPUSingleBoneSections;
	// FSkinGPUSingleBoneVertexStream		SkinGPUMonoVertexStream;

	// Bone hierarchy subset active for this chunk.
	TArray<_WORD> ActiveBoneIndices;
		
	// Raw data. Never loaded unless converting / updating.
	TLazyArray<FVertInfluence> Influences;
	TLazyArray<FMeshWedge>     Wedges;
	TLazyArray<FMeshFace>      Faces;
	TLazyArray<FVector>        Points;	

	// Rendering characteristics.
	FLOAT DisplayFactor; // Lod factor at which this LOD (or a lower one) kicks in.
	FLOAT LODHysteresis; // Flexible DisplayFactor range delta allowed to prevent flickering between lods.
	INT   DupVertCount;  // Required duplicate vertices buffer size during skinning.	
	INT   MaxInfluences; // For the smooth sections; if 0, they can use simpler rendering scheme.
	UBOOL bUniqueSubset; // Whether this LOD originated from a standalone, artist-defined mesh.
	UBOOL bUseSmoothing; // Flag: construct/redigest itself with smoothing groups.	

	//jg -- Compression scaling vectors
	FVector CompressionScale;
	FVector CompressionInvScale;
	// Used to move the verts around the origin
	FVector CompressionOffset;

	// Functions
	void ConstructGPUStream( UMeshInstance* MInst, INT MaxBonesPerChunk ); //Construct the skinning stream for this LOD model.	
		
	friend FArchive &operator<<( FArchive& Ar, FStaticLODModel& F )
	{
		Ar << F.SkinningStream;
		Ar << F.SmoothVerts;
		Ar << F.SmoothStreamWedges;

		Ar << F.SmoothSections;
		Ar << F.RigidSections;		

		Ar << F.SmoothIndexBuffer;				
		Ar << F.RigidIndexBuffer;			
		Ar << F.RigidVertexStream;			

		Ar << F.Influences;
		Ar << F.Wedges;
		Ar << F.Faces;
		Ar << F.Points;		

		Ar << F.DisplayFactor;
		Ar << F.LODHysteresis;
		Ar << F.DupVertCount;
		Ar << F.MaxInfluences;
		Ar << F.bUniqueSubset;
		Ar << F.bUseSmoothing;								
		
		if( !Ar.IsPersistent() )
		{					
			Ar << F.ActiveBoneIndices;
		}

		if(  Ar.Ver() >= 127 )
		{
			//jg -- Serialize compression scales if the right version
			if (Ar.Ver() >= 136)
			{
				Ar << F.CompressionScale << F.CompressionInvScale
					<< F.CompressionOffset;
			}
			else
			{
				F.CompressionScale = FVector(1.f,1.f,1.f);
				F.CompressionInvScale = FVector(1.f,1.f,1.f);
				F.CompressionOffset  = FVector(0.f,0.f,0.f);;
			}
			// Set the compression scale & inverse scale on the compressor class
			FPackedPosition::SetCompressionVectors(&F.CompressionScale,
				&F.CompressionInvScale,&F.CompressionOffset);

			Ar << F.GPUMultiBoneSections;
			Ar << F.GPUIndexBuffer;
			Ar << F.GPUVertexStream;
			if( Ar.Ver() >= 128 )
			{
				Ar << F.GPUBoneChunks;
			}
#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
			// scion sz -
			// keep smooth sections when loading on the PC
			// clear the data when loaded on Xbox or during xbox asset conversion (XAC)
			// since it is never used on Xbox.
			if( Ar.IsLoading() &&
				F.GPUMultiBoneSections.Num() )
			{
				F.SkinningStream.Empty();
				F.SmoothVerts.Empty();
				F.SmoothSections.Empty();
				F.SmoothIndexBuffer.Indices.Empty();
			}
#endif
			//jg -- Wipe the compression scale & offset on the compressor class
			// so that we can find errors faster
			FPackedPosition::ClearCompressionVectors();
		}

		return Ar;
	}
};


//
// Skeletal mesh instance.
//

class ENGINE_API USkeletalMeshInstance : public ULodMeshInstance
{
	DECLARE_CLASS(USkeletalMeshInstance, ULodMeshInstance, CLASS_Transient, Engine)


	// 'Direct-write-callback' vertex buffer for smooth-skinned parts ( doesn't contain a TArray. )	
	FSkinVertexStream     SkinStream;		// All other buffers - index- and rigid-parts vertex buffers - don't change so can reside with the mesh.
	
	TArray<MeshAnimLinkup>  AnimLinkups;	// Animations along with bone-to-bone linkup arrays.		
	
	// State of bone bases - set up in local mesh space.
	TArray<FCoords>		SpaceBases;
	
	// scion sz -
	// stores final local mesh space transforms relative to ref pose
	// these can be reused by owner driven meshes and for same-frame rendering of mesh
	TArray<FCoords>		CachedFinalBases;		// store final bone transforms in ref pose space (local space relative to refrence pose)
	SQWORD				CachedFinalLastTick;    // Determines freshness of the CachedFinalBases (so it is updated once per frame per instance)
	
	// Temporary storage of intermediate FCoords.
	FCoords            CachedMeshTrafo;		// Transforms SpaceBases into world space

	// LOD	
	INT CurrentLODLevel;     // Index into LODModels.
	FLOAT LastLodFactor;     // last computed LOD factor ( using which, LOD model was chosen )
	
	// Animation blending.
	TArray <MeshAnimChannel>  Blends;       // Animation blending.
	TArray <MeshBoneScaler>   Scalers;      // Non-uniform bone scaling
	TArray <MeshBoneDirector> Directors;    // rotation/translation controllers - local space
	TArray <MeshBoneDirector> WorldSpacers; // ,, - in world space.

	INT LastNotifyStage;  // Remembers channel for notify events.
	INT LastAnimEndStage; //
	
	// Cached vertex frame for tweening
	TArray <FQuat>    CachedOrientations;
	TArray <FVector>  CachedPositions;
	TArray <INT>      CachedLinks;
	
	FLOAT			  CachedFrame;
	FName			  CachedSeq;	
	USkeletalMesh*    CachedMesh;	
	FLOAT		      TweenIndicator;
	UBOOL             CacheActive;
	FLOAT             PrevFrame; // Loop detection
	FName             PrevSeq;   // Loop detection

	// For rendering bone/influence debugging graphics only.
	TArray <FVector> DebugPivots;
	TArray <INT>	 DebugParents;
	TArray <FVisNormal> DebugInfluences;

	// Editor/debugging rendering mode flags.
	INT              ForcedLodModel;  // Force drawing of a specific lodmodel -1    if > 0.
	UBOOL            bForceRefpose;	  // Force reference pose.
	UBOOL            bPrintBoneNames; // Force printing of bone names.
	UBOOL            bDisplayNormals; // Force drawing of influences-count-colored normals.
	
    FDynamicLight	FillLight;			// complements the primary light source for better rim highlights
	FDynamicLight	HiLightLight;		// light opposite camera direction to provide more contrast in the scene

#ifdef WITH_LIPSINC
	INT m_nActiveLIPSincAnim;
	TArray<DOUBLE> CachedLIPSincValues;
	TArray<TLIPSincIndexedBone> CachedLIPSincBones;

	TLIPSincLookAtController LookAtController;

	TLIPSincBlendInfo LIPSincBlendInfo;
	
	UBOOL HasLIPSincAnim( FName LIPSincAnimName );
	void PlayLIPSincAnim( FName LIPSincAnimName, FLOAT Volume, FLOAT Radius, FLOAT Pitch );
	UBOOL StopLIPSincAnim( void );
	UBOOL PauseLIPSincAnim( void );
	UBOOL ResumeLIPSincAnim( void );
	UBOOL IsPlayingLIPSincAnim( void );

	// NOTE:
	// For now, just expose an FString that is the name of the currently playing LIPSinc animation.
	// When we do LIPSinc anim properties, we might want to return a TLIPSincAnimation* here and
	// expose that to script, etc.

	FString CurrentLIPSincAnim( void );
#endif

	// Force an update for this mesh, whether it is visible or not.
	void ForceUpdate( void );

#ifdef WITH_KARMA
    /*
    This means the 'Reference' relative transform (with BBONE_RefPose flag)
    for this bone is nothing, and that ALL relative transforms come from a Director.
    */
    TArray <UBOOL>              NoRefPose;

    /* Physics for rag-doll. */
    TArray <McdModelID>         KSkelModels;
    TArray <MdtConstraintID>    KSkelJoints;

    UBOOL                       KSkelIsInitialised;

    //  BIG FAT HACK! - theres no easy way to tell from a Trace which Bone got hit,
    //  so we just remember which bone hit last time a linetrace was run...
    INT                         KLastTraceHit;

	// This is the world-space AABB to use when the skel mesh is in rag-doll.
	FBox						KSkelBox;

	// Used for 'freeze-ing' ragdoll
	UBOOL						KFrozen;

	// Used for contacts that lift the skeleton up.
	TArray <FKBoneLifter>		KBoneLifters;

	// First and last bones in the array which have physics.
	INT							KPhysRootIndex;
	INT							KPhysLastIndex;

	// Contains pairs of models that have no collision between them.
	TMap<QWORD, UBOOL>			KSkelDisableTable;

	// How long this ragdoll has been physical for.
	FLOAT						KRagdollAge;

	// Time (KRagdollAge) at which we need to relax our limits again.
	FLOAT						KRelaxLimitTime;
	UBOOL						KLimitsAreReduced;

	// Time that the next convulsion will start.
	FLOAT						KNextConvulsionTime;
#endif

	// Object interface.
	void Serialize( FArchive& Ar );
	// Constructor.
	USkeletalMeshInstance()
	{
		CachedMesh = NULL;
		
		HiLightLight.Color.X = 0.8f;
		HiLightLight.Color.Y = 0.8f;
		HiLightLight.Color.Z = 1.0f;
		HiLightLight.Alpha = 1.0f;
		FillLight.Alpha = 1.0f;

#ifdef WITH_LIPSINC
		m_nActiveLIPSincAnim = -1;
#endif

#ifdef WITH_KARMA
        KSkelIsInitialised = 0;
        KLastTraceHit = -1;
#endif
	}

#ifdef WITH_KARMA
	virtual void Destroy();
#endif

	UBOOL UpdateAnimation(FLOAT DeltaSeconds);
	UBOOL PlayAnim(INT Channel,FName SequenceName, FLOAT InRate, FLOAT InTweenTime, UBOOL InLooping);
	UBOOL SetBlendParams(INT Channel, FLOAT BlendAlpha, FLOAT InTime, FLOAT OutTime, FName StartBoneName, UBOOL bGlobalPose );
	UBOOL SetSkelAnim( UMeshAnimation* NewAnimation, USkeletalMesh* NewMesh ); // Link in a new animation object to the meshinstance.	
	void  ClearSkelAnims(); // Clear linked animation objects & their precached bone match lists.

	// Internal blending playanim.
	UBOOL BlendAnim(HMeshAnim InAnim, FLOAT InRate, FLOAT InTweenTime, UBOOL InLooping);

	virtual void  ActualizeAnimLinkups();

	void  SetMesh(UMesh* InMesh );

	// Return number of animations supported by the mesh instance.
	INT GetAnimCount();
	// Return animation for a given index.
	HMeshAnim GetAnimIndexed(INT InIndex);
	// Return animation for a given name
	HMeshAnim GetAnimNamed(FName InName);
	// Get the name of a given animation
	FName AnimGetName(HMeshAnim InAnim);
	// Get the group of a given animation
	FName AnimGetGroup(HMeshAnim InAnim);
	// See if an animation has this particular group tag.
	UBOOL AnimIsInGroup(HMeshAnim InAnim, FName Group);
	// Get the number of frames in an animation
	FLOAT AnimGetFrameCount(HMeshAnim InAnim);
	// Get the play rate of the animation in frames per second
	FLOAT AnimGetRate(HMeshAnim InAnim);
	// Get the number of notifications associated with this animation.
	INT AnimGetNotifyCount(HMeshAnim InAnim);
	// Get the time of a particular notification.
	FLOAT AnimGetNotifyTime(HMeshAnim InAnim, INT InNotifyIndex);
	// Get text associated with a given notify.
	const TCHAR* AnimGetNotifyText(HMeshAnim InAnim, INT InNotifyIndex);
	// Get function associated with given notify.
	UAnimNotify* AnimGetNotifyObject(HMeshAnim InAnim, INT InNotifyIndex);
	// change to void* => AnimGetNotifyObject() ? #debug

	virtual UMeshAnimation* FindAnimObjectForSequence( FName SeqName );
	virtual UBOOL AnimForcePose( FName SeqName, FLOAT AnimFrame, FLOAT Delta, INT Channel = 0 );
	virtual void ForceBoneRefresh();

	virtual UMeshAnimation* CurrentSkelAnim( INT channel );

	UBOOL IsAnimTweening( INT Channel=0);	
	UBOOL IsAnimLooping( INT Channel=0);
	UBOOL IsAnimPastLastFrame( INT Channel=0);
	UBOOL AnimStopLooping( INT Channel=0);

	FName GetActiveAnimSequence(INT Channel=0);
	FLOAT GetActiveAnimRate(INT Channel=0);
	FLOAT GetActiveAnimFrame(INT Channel=0);
	
	// TODO -clean up texture retrieval logic - some sources obsolete?
	UMaterial* GetMaterial( INT Count, AActor* Owner )
	{
		guardSlow(UMesh::GetMaterial);
		if( Owner && Owner->GetSkin( Count ) )
			return Owner->GetSkin( Count );
		else if( Count!=0 && ((ULodMesh*)GetMesh())->Materials.Num() > Count )
			return ((ULodMesh*)GetMesh())->Materials(Count);
		else if( Owner && Owner->Skins.Num() )
			return Owner->Skins(0);
		else if( Count>=0 &&((ULodMesh*)GetMesh())->Materials.Num() > Count )
			return ((ULodMesh*)GetMesh())->Materials(Count);
		else if( Count >= ((ULodMesh*)GetMesh())->Materials.Num() && ((ULodMesh*)GetMesh())->Materials.Num()>0 )
			return ((ULodMesh*)GetMesh())->Materials(0);
		else
			return NULL;
		unguardSlow;
	}

	void GetFrame( AActor* Owner, FLevelSceneNode* SceneNode, FVector*	ResultVerts, INT Size, INT& LODRequest, DWORD TaskFlag);
	void GetMeshVerts( AActor* Owner, FVector*	ResultVerts, INT Size, INT& LODRequest);
	FLOAT GetAnimRateOnChannel(INT Channel);

	// Get Verts & Normals or Bones & Normals for the current pose (interleaved.)
	INT GetMeshVertsAndNormals( AActor* Owner, TArray<FVector>* Result );
	INT GetMeshJointsAndNormals( AActor* Owner, TArray<FVector>* Result, INT BoneDepth=0 );

	// Render support.
	FMatrix MeshToWorld();
	void Render(FDynamicActor* Owner,FLevelSceneNode* SceneNode,TList<FDynamicLight*>* Lights,TList<FProjectorRenderInfo*>* Projectors,FRenderInterface* RI);
	
	virtual void MeshSkinVertsCallback( void* Dest );
	virtual INT ActiveVertStreamSize();
	
	
	void MeshBuildBounds();
	void SetScale( FVector NewScale );

	FBox GetRenderBoundingBox( const AActor* Owner );
	FSphere GetRenderBoundingSphere( const AActor* Owner );
	
	// Additional skeletal interface functions.

	FCoords  GetBoneCoords(   DWORD BoneIdx );
	FCoords  GetTagCoords( FName TagAlias );
	FRotator GetBoneRotation( FName BoneName, INT Space );
	FRotator GetBoneRotation( DWORD BoneIdx, INT Space );
	FRotator GetRootRotation();
	FVector  GetRootLocation();
	FRotator GetRootRotationDelta();
	FVector  GetRootLocationDelta();

	UBOOL    EnableChannelNotify( INT Channel, INT Switch );

	INT		MatchRefBone( FName StartBoneName);
	UBOOL	ValidateAnimChannel( INT TestChannel );
	INT		GetAnimChannelCount();
	UBOOL   IsAnimating(INT Channel);
	UBOOL	StopAnimating( UBOOL ClearAll = false );
	UBOOL   FreezeAnimAt( FLOAT Time, INT Channel );

	UBOOL	SetBoneScale( INT Slot, FLOAT BoneScale, FName BoneName );
	
	UBOOL   SetBoneLocation( FName BoneName, FVector  BoneTrans, FLOAT Alpha );
	UBOOL   SetBoneRotation( FName BoneName, FRotator BoneTurn, INT Space, FLOAT Alpha );
	UBOOL   SetBoneDirection( FName BoneName, FRotator BoneTurn, FVector BoneTrans, FLOAT Alpha, INT Space );

	void ScaleAnimRate( INT Channel, FLOAT Scale );
	void SetAnimRate(INT Channel, FLOAT NewRate);
	void ForceAnimRate(INT Channel, FLOAT NewRate);
	FLOAT GetBlendAlpha(INT Channel);
	void SetBlendAlpha(INT Channel, FLOAT NewAlpha);
	FLOAT GetAnimFrame(INT Channel);
	void SetAnimFrame(INT Channel, FLOAT NewFrame, INT UnitFlag=0 );
	void CopyAnimation(INT SrcChannel, INT DestChannel, AActor* DestActor = NULL );
	void UpdateBlendAlpha(INT Channel, FLOAT GoalAlpha, FLOAT StepSize);
	FName GetAnimSequence(INT Channel);
	void SetAnimSequence(INT Channel, FName NewSeq);
	void BlendToAlpha( INT Channel, FLOAT DestAlpha, FLOAT TimeInterval );
	void SetTweenRate( INT Channel, FLOAT NewRate );
	void SetOrigRate( INT Channel, FLOAT NewRate );
	void SetAnimFinished( INT Channel, UBOOL B );
	void SetAnimLoop( INT Channel, UBOOL B );
	void SetAnimLast( INT Channel, FLOAT Frame );
	void SetAnimNotify( INT Channel, UBOOL B );
	void SetOldAnimRate(INT Channel, FLOAT NewRate);
	void SetBlendInTime( INT Channel, FLOAT Time );
	void SetBlendOutTime( INT Channel, FLOAT Time );
	void SetBlendTargetInterval( INT Channel, FLOAT Time );
	void SetBlendTargetAlpha( INT Channel, FLOAT Time );
	void SetTweenProgress( INT Channel, FLOAT Time );
	void SetTimedBlend( INT Channel, UBOOL B );
	void SetGlobalPose( INT Channel, UBOOL B );
	void SetCachedSeq( INT Channel, FName NewSeq );
	void SetCachedFrame( INT Channel, FLOAT NewFrame );
	void SetStartBone( INT Channel, INT NewBone );
	void SetAnimRangeStart(INT Channel, FLOAT NewRate);
	void SetAnimRangeEnd(INT Channel, FLOAT NewRate);
	void SetNotifyIgnore( INT Channel, UBOOL B );
	void SetAltRange( INT Channel, UBOOL B );
	void SetTweening( INT Channel, UBOOL B );
	void SetMeshAnimIndex(  INT Channel, INT Index );

	//scion rwd
	void GetRootBoneOffset(AActor *Owner, INT Channel, FLOAT FrameTime, FVector &RootLocation, FRotator &RootRotation);

	// scion sz
	void GetCurAnimTrackRotPos( INT Channel, FLOAT FrameTime, FQuat& OutRot, FVector& OutPos, INT BoneIndex );
};
	

//
// Skeletal mesh.
//

class ENGINE_API USkeletalMesh : public ULodMesh
{
	//scion rwd - added auto-precache of skeletal meshes
	DECLARE_CLASS(USkeletalMesh, ULodMesh, CLASS_SafeReplace | CLASS_AutoPrecache, Engine)

	// Full lazy-loadable mesh data + wedge-collapses ( Verts are implicitly LOD-sorted. )
	TLazyArray<FVector>				RawVerts;
	TLazyArray<FMeshNorm>		RawNormals;
	TLazyArray<FMeshWedge>		RawWedges;
	TLazyArray<VTriangle>				RawFaces;
	TLazyArray<FVertInfluence>	RawInfluences;
	TLazyArray<_WORD>					RawCollapseWedges;
	TLazyArray<_WORD>					RawFaceLevel;

	// Skeletal specific data
	TArray<FMeshBone>					RefSkeleton;//Reference skeleton.
	INT SkeletalDepth;  // The max hierarchy depth.

	// Static LOD models
	TArray<FStaticLODModel> LODModels;

	// Legacy-skeletal data structures:
	TArray<FVector>         Points;        // Floating point vectors directly from our skin vertex 3d points.	

	// OBSOLETE:    Single, double, etc indices - MultiWeights(0).PointIndex(n) = pointindex, weights implied from _WORD array
	TArray<VWeightIndex>	MultiBlends;  // Nested TArray.
	TArray<VBoneInfluence>  Weights;        // Weight/Boneindex array.

	UMeshAnimation* DefaultAnim; // Link this up when no other animation is available - for backwards compatibility.
	USkeletalMesh*  DefaultRefMesh; // Optional non-similar mesh for animation adjustment.

	// All LOD/Vertex buffers that aren't unique per instance.
	FMeshLODChunk LODChunks[4]; // For non-smooth-lod hardware rendering schemes (PSX2)
	
	// Reference skeleton precomputed bases #DEBUG wasteful ?!
	TArray <FCoords> RefBasesInverse;
	// scion sz -
	// if we are using owner driven animation then we can reuse the
	// ref bases from the owning mesh 
	UBOOL bRefBasesMatchOwner;

	// Runtime influence index helper array.
	TArray <VertInfIndex> InfluenceIndex; // pre-processed index of bone influences per vertex.

	// Attachments.
	TArray <FName>   TagAliases; // Attachment bone aliases
	TArray <FName>   TagNames;	 // real bone indices into reference skeleton
	TArray <FCoords> TagCoords;	 // reserved

	// Bone collision primitives.
	// This info is BEFORE ULodMesh::DrawScale is applied.
	TArray <FSkelBoneSphere>	BoneCollisionSpheres;
	TArray <FSkelBoneBox>		BoneCollisionBoxes;

	// Set of UModels that correspond to the BoneCollisionBoxes array.
	// These have ULodMesh::DrawScale baked in.
	TArray <UModel*>			BoneCollisionBoxModels;

#ifdef WITH_KARMA
	// Karma version of collision data (taken from reference pose).
	// Used for PHYS_Karma with skeletal meshes (not PHYS_KarmaRagDoll!)
	// This has the ULodMesh::DrawScale baked in.
	UKMeshProps*				KPhysicsProps;	
#endif

	// To avoid quantization errors build until valid
	void ComputeCompressionScales(void);
	// Whether this mesh has been tristripped or not
	UBOOL bIsTristripped;
	// Function to tristrip this mesh
	void ConvertMeshToStrips(void);

	// Object interface.
	void Serialize( FArchive& Ar );
	virtual void PostLoad();
	virtual void Destroy();

	// Constructor.
	USkeletalMesh()
	{
#ifdef WITH_LIPSINC
	#ifdef _XBOX
		bAlreadyLookedForController = 0;
	#endif
#endif
		DefaultAnim = NULL;
		bRefBasesMatchOwner = 0;
	}

	UClass* MeshGetInstanceClass() { return USkeletalMeshInstance::StaticClass(); }

	// Setup-only routines - not concerned with the instance.
	void ReconstructRawMesh();
	void GenerateLodModel( INT LodLevel, FLOAT ReductionFactor, FLOAT DistanceFactor, INT MaxInfluences, UBOOL bSlowTaskAllowed );
	// Use the raw NewModel data as a lod level for this mesh.	
	void InsertLodModel( INT LodLevel, USkeletalMesh* RawMesh, FLOAT DistanceFactor, UBOOL bSlowTaskAllowed );	

	// Convert -existing- smooth skin to rigid parts + smooth parts.	
	void ExtractRigidParts( INT LODModelIndex, INT PartMinVerts, INT MaxParts, UBOOL bDuplicateVerts );
	
	void CalculateNormals(TArray <FVector>& Normals, UBOOL Displace );
	void NormalizeInfluences( INT LodLevel );
	INT  RenderPreProcess();

	// Defined in both, but points to meshinstance implementation.
	FBox GetRenderBoundingBox( const AActor* Owner );
	FSphere GetRenderBoundingSphere( const AActor* Owner );

	// Attachment aliases.
	UBOOL SetAttachAlias( FName TagName, FName BoneName, FCoords& AdjustCoords );

	UBOOL SetAttachmentLocation(AActor *Actor, AActor *AttachedActor);

	// Forced handedness fix.
	void FlipFaces();
	UBOOL ConformSkeletonTo( USkeletalMesh* SrcMesh );

	// Reports memory footprint in bytes, for digested data.
	virtual INT MemFootprint(UBOOL RenderDataOnly=0);	
	INT LODFootprint( INT LODIndex, UBOOL RenderDataOnly );

	INT		MatchRefBone( FName StartBoneName);
	
	FSkelBoneSphere* FindBoneCollisionSphere(FName BoneName);

	//scion jg -- Changed to pass by ref
	virtual UBOOL LineCheck(FCheckResult &Result,AActor* Owner,AActor* Source,const FVector& End,const FVector& Start,const FVector& Extent,DWORD ExtraNodeFlags, DWORD TraceFlags);
	virtual FBox GetCollisionBoundingBox( const AActor* Owner ) const;
	virtual UBOOL UseCylinderCollision( const AActor* Owner );	
	//scion jg -- Changed to pass by ref
	virtual UBOOL PointCheck(FCheckResult&	Result, AActor*	Owner,const FVector& Location,const FVector& Extent, DWORD ExtraNodeFlags);

#ifdef WITH_LIPSINC
	#ifdef _XBOX
		UBOOL	bAlreadyLookedForController;
	#endif
	TLIPSincController *LIPSincController;
	UBOOL LinkupLIPSincBones( void );
#endif

	//scion rwd - custom precache for meshes
	void Precache(UObject *src = NULL,TMap<UObject*,BYTE> *masterPrecacheList = NULL);

	//jg -- For clearing all pushbuffer references
	void ClearAllMeshData(void);
	//jg -- For setting all pushbuffer references
	void SetAllMeshData(void);
};


/*-----------------------------------------------------------------------------
	UMeshAnimation definition.
-----------------------------------------------------------------------------*/
// STUB LEGACY CLASS
class ENGINE_API UAnimation : public UObject
{
	DECLARE_CLASS(UAnimation,UObject, CLASS_SafeReplace, Engine)
	void Serialize( FArchive& Ar );
};


//
//  Per-Animation-Object (re-)digest data and info.
//
struct  AnimImportHelpObject
{
	TArray<MotionChunkDigestInfo>  MovesInfo;		  // Moves info from file or script, instructions to build the AnimSeqs.
	INT                     RawNumFrames;			  // Raw number of frames.
	TArray<VQuatAnimKey>    RawAnimKeys;			  // Raw keys (bones * frames), ordered by frames.
	TArray<AnimInfoBinary>  RawAnimSeqInfo;	          // Moves info from file (optional)
	FLOAT                   CompFactor;               // Default global compression factor when digesting animations.

	// Serialization - incomplete, but never used in-game.
	friend FArchive& operator<<(FArchive& Ar,AnimImportHelpObject& A)
	{	
		// Serialize for memory tally only.
		if( !Ar.IsPersistent() )
		{
			Ar
			//<< A.MovesInfo
			<< A.RawNumFrames
			<< A.RawAnimKeys
			//<< A.RawAnimSeqInfo
			<< A.CompFactor;
		}
		return	Ar;
			
	}	
};

//
// UnAnimation, the base class of animating skeletal bones which are linked up by name dynamically to
// USkeletalMesh skins/reference skeletons.
//
class ENGINE_API UMeshAnimation : public UObject
{
	DECLARE_CLASS(UMeshAnimation,UObject, CLASS_SafeReplace, Engine)

	// Internal version number. Important now that we allow management of
	// backward compatibility of meshes/animations inside .ukx packages.
	INT 	InternalVersion;

	// Skeletal animation data. Linked up to a reference skeleton at runtime.
	TArray<FNamedBone>		RefBones;		// Name.

	// scion sz - 
	// All of the motion chunk track data (quats,positions,key times)
	// are packed together in this block
	FRawMotionDataBlock		RawMotionDataBlock;

	TArray<MotionChunk>		Moves;			// One for every animation - has hierarchy starting point,
											// speed, flags, compression, and actual animation.

	// MeshAnimSeq information; per-animation names/data/notifies.
	TArray<FMeshAnimSeq>  AnimSeqs;        // Classic AnimSeqs.

	// Helper object containing raw uncompressed animation keys, etc - not to be serialized.
	AnimImportHelpObject*  DigestHelper;

	// Object interface.
	void Serialize( FArchive& Ar );
	void PostLoad();
	// Constructor.
	UMeshAnimation();		

	virtual FMeshAnimSeq* GetAnimSeq( FName SeqName )
	{
		guardSlow(UMeshAnimation::GetAnimSeq);
		for( INT i=0; i<AnimSeqs.Num(); i++ )
		{
			if( SeqName == AnimSeqs(i).Name )
				return &AnimSeqs(i);
		}
		return NULL;
		unguardSlow;
	}

	// Initialize import/digestion helper object.
	virtual void InitForDigestion()
	{	
		if( !DigestHelper )
		{
			DigestHelper = NEW AnimImportHelpObject;
			appMemzero( DigestHelper,sizeof(AnimImportHelpObject));
			DigestHelper->CompFactor = 1.0f;
		}
	}

	// Get the motion tracks for a sequence name, if present.
	virtual MotionChunk* GetMovement( FName SeqName )
	{
		guardSlow(UMeshAnimation::GetMovement);
		for( INT i=0; i<AnimSeqs.Num(); i++ )
		{
			if( SeqName == AnimSeqs(i).Name )
				return &Moves(i);
		}
		return NULL;
		unguardSlow;
	}

	// Modify the root orientation/translation for a certain sequence in this UMeshAnimation
	virtual UBOOL AdjustMovement( FName SeqName, FCoords AdjustCoords );	

    // Conform bones from non-matching skeletons when merging PSA animation data in the editor.
	virtual void ConformBones( UMeshAnimation* DestAnimObject, USkeletalMesh* ReferenceMesh );

	// scion sz -
	// Updates an animation sequence's moves data so that there is a unique track for each bone entry in RefBones
	virtual void ConformMoves( UMeshAnimation* NewAnimObject, USkeletalMesh* ReferenceMesh );

	// Estimates memory footprint in bytes, for digested data.
	virtual INT MemFootprint();
	virtual INT SequenceMemFootprint( FName SeqName);

	// Sequence list order management.
	virtual INT SortSequencesByName();
	virtual INT MoveSequenceByIndex( INT SourceIdx, INT DestIdx );

};




/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/


