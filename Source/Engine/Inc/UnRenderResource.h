/*=============================================================================
	UnRenderResource.h: Rendering resources, vertex buffers, index buffers
	Copyright 1997-2000 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Jack Porter
=============================================================================*/

#ifndef _UNRENDERRESOURCE_H_
#define _UNRENDERRESOURCE_H_

//
// Forward declarations.
//
class UTexture;
class FCubemap;
class FTexture;
class FRenderTarget;
class FCompositeTexture;


#include "UnColor.h"

//scion jg -- This class handles packing UV coordinates into a smaller memory
// footprint. They are packed into shorts using half their normal space
class FPackedUVCoord
{
	// The 16bit representation of a FLOAT
	SWORD Coord;

	// Remaps the coordinate to a range between -1.0 and 1.0
	FORCEINLINE SetCoord(FLOAT fl)
	{
		// This will clamp the value between -32767 and 32767, which is
		// interpreted by D3DVSDT_NORMSHORT1 as -1.0 to 1.0
		Coord = (fl < -1.f || fl > 1.f) ? appFractional(fl) * 32767.f :
			fl * 32767.f;
	}

public:
	// Constructors
	FORCEINLINE FPackedUVCoord()
	{
		SetCoord(0.f);
	}

	FORCEINLINE FPackedUVCoord(FLOAT fl)
	{
		SetCoord(fl);
	}

	FORCEINLINE FPackedUVCoord operator=(FLOAT fl)
	{
		SetCoord(fl);
		return *this;
	}

	FORCEINLINE operator FLOAT ()
	{
		return Unpack();
	}

	FORCEINLINE FLOAT Unpack(void)
	{
		static FLOAT flMul = 1.f / 32767.f;
		return (FLOAT)Coord * flMul;
	}

	FORCEINLINE FPackedUVCoord operator-=(FLOAT fl)
	{
		SetCoord(Unpack() - fl);
		return *this;
	}

	FORCEINLINE FPackedUVCoord operator+=(FLOAT fl)
	{
		SetCoord(Unpack() + fl);
		return *this;
	}

	/* ============================================================================
	 * ::operator<<
	 * Author: jg
	 * 
	 * Serializer for FPackedUVCoord. Depending on the licensee package version, it
	 * either in place converts a FLOAT to a FPackedUVCoord or it serializes just
	 * the FPackedUVCoord amount of data.
	 *
	 * inputs:	Ar -- The archive that we are serializing from/to
	 *			Coord -- The FPackedUVCoord var that is being serialized
	 *
	 * ============================================================================
	 */
	friend FArchive& operator<<(FArchive& Ar,FPackedUVCoord& Coord)
	{
		// Figure out whether this is the xbox or normal package format
		if (Ar.LicenseeVer() == XBOX_PACKAGE_FORMAT)
		{
			// This is an Xbox format FPackedPosition so no conversion is needed
			Ar << Coord.Coord;
		}
		else
		{
			// Perform on the fly conversion
			if (Ar.IsLoading() == 1)
			{
				FLOAT fl;
				// Serialize the value to a float
				Ar << fl;
				// Now convert it to packed form
				Coord = fl;
			}
			// This should never happen, handle anyway
			else if (Ar.IsSaving() == 1)
			{
				check(PACKAGE_FILE_VERSION_LICENSEE == XBOX_PACKAGE_FORMAT);
				Ar << Coord.Coord;
			}
			else
			{
				Ar << Coord.Coord;
			}
		}
		return Ar;
	}
};

//scion jg -- Indicates an uncached item
#define INVALID_CACHEID	65535

//
//	FRenderResource
//	Abstract rendering resource interface.
//
class ENGINE_API FRenderResource
{
public:

	//scion jg -- For cleaning up resources.
	class URenderDevice* m_pRenDev;
#ifdef _XBOX
	//scion jg -- Whether this resource can be UMA loaded or not
	BYTE bIsUMA;
	FRenderResource() : bIsUMA(0), CacheId(INVALID_CACHEID), m_pRenDev(NULL) {}
#else
	//scion jg -- Changes for new caching scheme
	FRenderResource() : CacheId(INVALID_CACHEID), m_pRenDev(NULL) {}
#endif

	virtual ~FRenderResource();

	//scion jg -- Changes for new caching scheme
protected:
	_WORD CacheId;
public:
	virtual _WORD GetCacheId() { return CacheId; }
	virtual void SetCacheId(_WORD NewCacheID,URenderDevice* pRenDev)
	{
		CacheId = NewCacheID;
		m_pRenDev = pRenDev;
	}

	virtual INT GetRevision() = 0;

	/**
	 * Flushes the resource from the renderer
	 */
	void Flush(void);

	// scion sz - get subclass interfaces
	virtual class FBaseTexture* GetBaseTextureInterface() { return NULL; }
	virtual class FVertexStream* GetVertexStreamInterface() { return NULL; }
	virtual class FIndexBuffer* GetIndexBufferInterface() { return NULL; }
};

//
//	EComponentType
//
enum EComponentType
{
	CT_Float4,
	CT_Float3,
	CT_Float2,
	CT_Float1,
	CT_Color,
	CT_RGBA4,
	//scion jg -- For packed normals
	CT_NormPacked3,
	//scion jg -- For 16 bit representation of floats
	CT_NormShort1,
	CT_NormShort2,
	CT_NormShort3,
	CT_NormShort4,
	// scion sz - more 16 bit floats (these are not normalized to [0,1])
	CT_Short1,
	CT_Short2,
	CT_Short3,
	CT_Short4,
	// scion sz - packed byte formats
	CT_PByte1,
	CT_PByte2,
	CT_PByte3,
	CT_PByte4
};

//
//	EFixedVertexFunction
//
enum EFixedVertexFunction
{
	FVF_Position,
	FVF_Normal,
	FVF_Diffuse,
	FVF_Specular,
	FVF_TexCoord0,
	FVF_TexCoord1,
	FVF_TexCoord2,
	FVF_TexCoord3,
	FVF_TexCoord4,
	FVF_TexCoord5,
	FVF_TexCoord6,
	FVF_TexCoord7,
	FVF_BlendIndices,
	FVF_PSize,
	FVF_BlendWeights,
	FVF_Fog,
	FVF_BackDiffuse,
	FVF_BackSpecular,

	FVF_GPUReg0,
	FVF_GPUReg1,
	FVF_GPUReg2,
	FVF_GPUReg3,
	FVF_GPUReg4,
	FVF_GPUReg5,
	FVF_GPUReg6,
	FVF_GPUReg7,
	FVF_GPUReg8,
	FVF_GPUReg9,
	FVF_GPUReg10,
	FVF_GPUReg11,
	FVF_GPUReg12,
	FVF_GPUReg13,
	FVF_GPUReg14,
	FVF_GPUReg15,
};

//
//	FVertexComponent
//
struct FVertexComponent
{
	EComponentType			Type;
	EFixedVertexFunction	Function;

#if !defined(_XBOX)
	// MSM: eliminted constructors to keep them from getting called 1000's of times
	// per frame.
	FVertexComponent()
	{
	}

	FVertexComponent(EComponentType InType,EFixedVertexFunction InFunction)
	{
		Type = InType;
		Function = InFunction;
	}
#endif
};


enum { MAX_VERTEX_COMPONENTS = 8 };

//
//	FVertexStream
//	Abstract vertex stream interface.
//
class ENGINE_API FVertexStream : public FRenderResource
{
public:
	virtual ~FVertexStream();

	virtual INT GetStride() = 0;
	virtual INT GetSize() = 0;
	virtual UBOOL HintDynamic() { return 0; }
	virtual UBOOL UseNPatches() { return 0; }

	virtual INT GetComponents(FVertexComponent* Components) = 0;

	virtual void GetStreamData(void* Dest) = 0;
	virtual void GetRawStreamData(void ** Dest, INT FirstVertex ) = 0;

	virtual FVertexStream* GetVertexStreamInterface() { return this; }
};

//
//	FIndexBuffer
//	Abstract index buffer interface.
//
class ENGINE_API FIndexBuffer : public FRenderResource
{
public:
	virtual ~FIndexBuffer();

	virtual INT GetSize() = 0;

	virtual void GetContents(void* Data) = 0;

	virtual INT GetIndexSize() = 0;

	//scion jg -- Allows direct access to the index data array
	virtual void* GetContentsPtr(void) { return NULL; }

	virtual FIndexBuffer* GetIndexBufferInterface() { return this; }
};

//
//	ETextureFormat
//
enum ETextureFormat
{
	TEXF_P8			= 0x00,
	TEXF_RGBA7		= 0x01,
	TEXF_RGB16		= 0x02,
	TEXF_DXT1       = 0x03,
	TEXF_RGB8       = 0x04,
	TEXF_RGBA8      = 0x05,
	TEXF_NODATA		= 0x06,
	TEXF_DXT3		= 0x07,
	TEXF_DXT5		= 0x08,
	TEXF_L8			= 0x09,
	TEXF_G16		= 0x0a,
	TEXF_RRRGGGBBB	= 0x0b,
	TEXF_V8U8		= 0x0c,		// -- 16 bit dUdV for bump mapping
	TEXF_L6V5U5		= 0x0d,		// -- 16 bit dUdV for bump mapping + 6 bit luminance
	TEXF_V16U16		= 0x0e,		// -- 32 bit dUdV for bump mapping
	TEXF_A8			= 0x0f,
	//scion jg -- 16bit color support
	TEXF_R5G6B5		= 0x10,
	TEXF_MAX		= 0xff
};

//
//	ETexClampMode
//
enum ETexClampMode
{
	TC_Wrap				= 0x00,
	TC_Clamp			= 0x01,
};

// scion sz -
enum ETexFilterMode
{
	TF_NEAREST=0,
	TF_BILINEAR,
	TF_TRILINEAR,
	TF_MAX
}; 

//
//  IsDXTC
//
inline UBOOL IsDXTC( ETextureFormat Format )
{
	return ( (Format == TEXF_DXT1) || (Format == TEXF_DXT3) || (Format == TEXF_DXT5) );
}


//
//	GetBytesPerPixel
//
inline INT GetBytesPerPixel(ETextureFormat Format,INT NumPixels)
{
	switch(Format)
	{
	case TEXF_DXT1:
		return NumPixels / 2;
	case TEXF_RGBA8:
	case TEXF_RGBA7:
	case TEXF_V16U16:
		return NumPixels * 4;
	case TEXF_P8:
	case TEXF_DXT3:
	case TEXF_DXT5:
	case TEXF_L8:
	case TEXF_A8:
		return NumPixels;
	case TEXF_RGB16:
	case TEXF_G16:
	case TEXF_V8U8:
	case TEXF_L6V5U5:
		return NumPixels * 2;
	case TEXF_RGB8:
		return NumPixels * 3;
	};

	return 0;
}

//
//  CalculateTexelPointer
//
inline BYTE* CalculateTexelPointer(BYTE* Base,ETextureFormat Format,INT Stride,INT X,INT Y)
{
	switch(Format)
	{
	case TEXF_DXT1:
		return Base + Stride * (Y / 4) + (X * 2);
	case TEXF_DXT3:
	case TEXF_DXT5:
		return Base + Stride * (Y / 4) + (X * 4);
	case TEXF_RGBA8:
	case TEXF_RGBA7:
	case TEXF_V16U16:
		return Base + Stride * Y + X * 4;
	case TEXF_RGB8:
	case TEXF_RRRGGGBBB:
		return Base + Stride * Y + X * 3;
	case TEXF_RGB16:
	case TEXF_G16:
	case TEXF_V8U8:
	case TEXF_L6V5U5:
		return Base + Stride * Y + X * 2;
	case TEXF_P8:
	case TEXF_L8:
	case TEXF_A8:
		return Base + Stride * Y + X;
	};

	return Base;
}

//
//	Base texture interface.
//
class ENGINE_API FBaseTexture : public FRenderResource
{
public:
	virtual ~FBaseTexture();

	virtual FBaseTexture* GetBaseTextureInterface() { return this; }
	virtual FCubemap* GetCubemapInterface() { return NULL; } 
	virtual FTexture* GetTextureInterface() { return NULL; }
	virtual FCompositeTexture* GetCompositeTextureInterface() { return NULL; }
	virtual FRenderTarget* GetRenderTargetInterface() { return NULL; }

	virtual INT GetWidth() = 0;
	virtual INT GetHeight() = 0;
	virtual INT GetFirstMip() = 0;
	virtual INT GetNumMips() = 0;
	virtual ETextureFormat GetFormat() = 0;

	// temporary
	virtual ETexClampMode GetUClamp() = 0;
	virtual ETexClampMode GetVClamp() = 0;
};

//
// FTexture
//
class ENGINE_API FTexture : public FBaseTexture
{
public:

	virtual FTexture* GetTextureInterface() { return this; }

	virtual void* GetRawTextureData(INT MipIndex) = 0;
	virtual void GetTextureData(INT MipIndex,void* Dest,INT DestStride,ETextureFormat DestFormat,UBOOL ColoredMips=0) = 0;
	virtual void UnloadRawTextureData( INT MipIndex ) = 0;
	virtual UTexture* GetUTexture() = 0;
};

//
// FCompositeTexture
//
class ENGINE_API FCompositeTexture : public FBaseTexture
{
public:

	virtual FCompositeTexture* GetCompositeTextureInterface() { return this; }

	virtual INT GetNumChildren() = 0;
	virtual FTexture* GetChild(INT ChildIndex,INT* OutChildX,INT* OutChildY) = 0;
};

//
// FCubemap
//
class ENGINE_API FCubemap : public FBaseTexture
{
public:

	virtual FCubemap* GetCubemapInterface() { return this; }
	
	virtual class UCubemap* GetUCubemap() { return NULL; }

	virtual FTexture* GetFace( INT Face ) = 0;
};

//
// FRenderTarget
//
class ENGINE_API FRenderTarget : public FBaseTexture
{
public:
	// scion sz (can have both)
	enum 
	{
		RT_CREATE_COLOR=0x1,				// create color buffer
		RT_CREATE_DEPTH=0x2					// create depth buffer
	};

	// scion sz (only one mode is valid)
	enum 
	{		
		RT_MATERIAL_USE_BACKBUFFER=0,		// use back buffer as material
		RT_MATERIAL_USE_FRONTBUFFER=1,		// use front buffer as material
		RT_MATERIAL_USE_DEPTHBUFFER=2,		// use depth buffer as material
	};

	// scion sz - make sure these always use TC_Clamp
	virtual ETexClampMode GetUClamp()
	{
		return TC_Clamp;
	}
	virtual ETexClampMode GetVClamp()
	{
		return TC_Clamp;
	}

	INT	CreationFlags, MaterialUsageFlag;
	
	// If this render target was created by a render target pool
	// it will have a pointer to that pool and a PoolIndex that is valid. 	
	class FRenderTargetPool* RenderTargetPool;
	INT PoolIndex;

	FRenderTarget( INT InCreationFlags=RT_CREATE_COLOR|RT_CREATE_DEPTH ) : 
	CreationFlags(InCreationFlags),
	MaterialUsageFlag(RT_MATERIAL_USE_BACKBUFFER),
	PoolIndex(INDEX_NONE),
	RenderTargetPool(NULL)
	{}

	virtual FRenderTarget* GetRenderTargetInterface() { return this; }

	// Material usage
	virtual void SetMaterialUsage( INT InMaterialUse ) { MaterialUsageFlag = InMaterialUse; }

	// convenience function to remove a refrence of this render target from
	// the RenderTargetPool
	void RemovePoolRef();
};


//------------------------------------------------------------------------------
//	FRenderTargetPool
//
//	Interface for sharing render targets in order to save memory
//------------------------------------------------------------------------------
class ENGINE_API FRenderTargetPool
{
public:
	TArray<FRenderTarget*>	RenderTargets;
	TArray<INT>				RefCounts;

	FRenderTargetPool() {}
	~FRenderTargetPool() { ClearRenderTargets(); }

	FRenderTarget* ObtainRenderTarget( INT Width, INT Height, 
		ETextureFormat TexFormat=TEXF_RGBA8, 
		INT CreationFlags=FRenderTarget::RT_CREATE_COLOR|FRenderTarget::RT_CREATE_DEPTH );

	void ClearRenderTargets();

	void AddRef( FRenderTarget* RenderTarget );
	void RemoveRef( FRenderTarget* RenderTarget );
};


/**************************************************************************************
	Backward compatibility code.
**************************************************************************************/

/*------------------------------------------------------------------------------------
	Forward declarations.
------------------------------------------------------------------------------------*/

class		URenderResource;
class   	UVertexBuffer;
class   	UIndexBuffer;

/*------------------------------------------------------------------------------------
	URenderResource.
------------------------------------------------------------------------------------*/
class ENGINE_API URenderResource : public UObject
{
	DECLARE_ABSTRACT_CLASS(URenderResource,UObject,0,Engine)

	INT Revision;

	// UObject interface.
	void Serialize(FArchive& Ar);
};

/*------------------------------------------------------------------------------------
	UVertexStreamBase.
------------------------------------------------------------------------------------*/

enum EStreamType
{
	ST_MISC				= 0x0,
	ST_VECTOR3			= 0x1,
	ST_COLOR			= 0x2,
	ST_UV2				= 0x3,
	ST_FVF				= 0x4,
	ST_POSNORMTEX		= 0x5,
};

class ENGINE_API UVertexStreamBase: public URenderResource
{
	DECLARE_ABSTRACT_CLASS(UVertexStreamBase,URenderResource,0,Engine)

	INT		ItemSize;
	DWORD	PolyFlags;	// How we intend to render this VB.
	DWORD	StreamType;

	// UVertexStreamBase interface
	UVertexStreamBase( INT InItemSize, DWORD InPolyFlags, DWORD InStreamType ) 
		: ItemSize(InItemSize), PolyFlags(InPolyFlags), StreamType(InStreamType) {};
	virtual void* GetData()=0;
	virtual INT GetDataSize()=0;
	void SetPolyFlags( DWORD InPolyFlags )
	{
		if( PolyFlags != InPolyFlags )
			Revision++;
		PolyFlags = InPolyFlags;
	}

	// UObject interface
	void Serialize(FArchive& Ar)
	{
		Super::Serialize(Ar);
		if( Ar.Ver() >= 75 )
			Ar << ItemSize << PolyFlags << StreamType;
	}
};

/*------------------------------------------------------------------------------------
	UVertexBuffer.
------------------------------------------------------------------------------------*/
struct FUntransformedVertex
{
	FVector	Position,
			Normal;
	DWORD	Color;
	FLOAT	U,
			V;
	FLOAT	U2,
			V2;
	FUntransformedVertex()
	{}
	FUntransformedVertex( FVector InPosition )
	{
		Position = InPosition;
	}

	// Serialization.
	ENGINE_API friend FArchive& operator<<(FArchive& Ar,FUntransformedVertex& V)
	{
		return Ar
			<< V.Position
			<< V.Normal
			<< V.Color
			<< V.U
			<< V.V
			<< V.U2
			<< V.V2;
	}
};

class ENGINE_API UVertexBuffer : public UVertexStreamBase
{
	DECLARE_CLASS(UVertexBuffer,UVertexStreamBase,0,Engine)

	TArray<FUntransformedVertex>	Vertices;

	static URenderDevice* RenderDevice;

	UVertexBuffer() : UVertexStreamBase( sizeof(FUntransformedVertex), 0, ST_FVF )  {}
	UVertexBuffer( DWORD InPolyFlags ) : UVertexStreamBase( sizeof(FUntransformedVertex), InPolyFlags, ST_MISC) {}

	// UVertexStream Interface
	void* GetData() { return &Vertices(0); }
	INT GetDataSize() { return Vertices.Num() * sizeof(FUntransformedVertex); }

	// UObject Interface
	virtual void Serialize(FArchive& Ar)
	{
		guard(UVertexBuffer::Serialize);
		Super::Serialize(Ar);
		Ar << Vertices;
		if( Ar.Ver() >= 73 && Ar.Ver() <= 74 )
			Ar << PolyFlags;
		unguard;
	}
};

/*------------------------------------------------------------------------------------
	UVertexStreamVECTOR.
------------------------------------------------------------------------------------*/

class ENGINE_API UVertexStreamVECTOR : public UVertexStreamBase
{
	DECLARE_CLASS(UVertexStreamVECTOR,UVertexStreamBase,0,Engine)

	TArray<FVector>	VectorData;

	UVertexStreamVECTOR() : UVertexStreamBase( sizeof(FVector), 0, ST_VECTOR3 )  {}
	UVertexStreamVECTOR( DWORD InPolyFlags ) : UVertexStreamBase( sizeof(FVector), InPolyFlags, ST_VECTOR3) {}

	// UVertexStream Interface
	void* GetData() { return &VectorData(0); }
	INT GetDataSize() { return VectorData.Num() * sizeof(FVector); }

	// UObject Interface
	virtual void Serialize(FArchive& Ar)
	{
		guard(UVertexStreamVECTOR::Serialize);
		Super::Serialize(Ar);
		Ar << VectorData;
		unguard;
	}
};

/*------------------------------------------------------------------------------------
	UVertexStreamCOLOR.
------------------------------------------------------------------------------------*/

class FColorHack
{
public:

#if __INTEL_BYTE_ORDER__
#ifdef __PSX2_EE__
	BYTE R,G,B,A;
#else
	BYTE B,G,R,A;
#endif
#else
	BYTE A,R,G,B;
#endif

	DWORD& DWColor(void) {return *((DWORD*)this);}
	const DWORD& DWColor(void) const {return *((DWORD*)this);}

	FColorHack()
	{
	}

	FColorHack(BYTE InR,BYTE InG,BYTE InB,BYTE InA)
	{
		R = InR;
		G = InG;
		B = InB;
		A = InA;
	}

	FColorHack(BYTE InR,BYTE InG,BYTE InB)
	{
		R = InR;
		G = InG;
		B = InB;
		A = 255;
	}

	friend FArchive& operator<<(FArchive& Ar,FColorHack& Color)
	{
		return Ar << Color.R << Color.G << Color.B << Color.A;
	}
};

class ENGINE_API UVertexStreamCOLOR : public UVertexStreamBase
{
	DECLARE_CLASS(UVertexStreamCOLOR,UVertexStreamBase,0,Engine)

	TArray<FColorHack>	ColorData;

	UVertexStreamCOLOR() : UVertexStreamBase( sizeof(FColorHack), 0, ST_COLOR )  {}
	UVertexStreamCOLOR( DWORD InPolyFlags ) : UVertexStreamBase( sizeof(FColorHack), InPolyFlags, ST_COLOR) {}

	// UVertexStream Interface
	void* GetData() { return &ColorData(0); }
	INT GetDataSize() { return ColorData.Num() * sizeof(FColorHack); }

	// UObject Interface
	virtual void Serialize(FArchive& Ar)
	{
		guard(UVertexStreamCOLOR::Serialize);
		Super::Serialize(Ar);
		Ar << ColorData;
		unguard;
	}
};

/*------------------------------------------------------------------------------------
	UVertexStreamUV.
------------------------------------------------------------------------------------*/

struct FUV2Data
{
	FLOAT U;
	FLOAT V;
	ENGINE_API friend FArchive& operator<<(FArchive& Ar,FUV2Data& UVData)
	{
		return Ar << UVData.U << UVData.V;
	}
};

class ENGINE_API UVertexStreamUV : public UVertexStreamBase
{
	DECLARE_CLASS(UVertexStreamUV,UVertexStreamBase,0,Engine)

	TArray<FUV2Data>	UVData;

	UVertexStreamUV() : UVertexStreamBase( sizeof(FUV2Data), 0, ST_UV2 )  {}
	UVertexStreamUV( DWORD InPolyFlags ) : UVertexStreamBase( sizeof(FUV2Data), InPolyFlags, ST_UV2) {}

	// UVertexStream Interface
	void* GetData() { return &UVData(0); }
	INT GetDataSize() { return UVData.Num() * sizeof(FUV2Data); }

	// UObject Interface
	virtual void Serialize(FArchive& Ar)
	{
		guard(UVertexStreamUV::Serialize);
		Super::Serialize(Ar);
		Ar << UVData;
		unguard;
	}
};

/*------------------------------------------------------------------------------------
	UVertexStreamPosNormTex
------------------------------------------------------------------------------------*/

struct FPosNormTexData
{
	FVector	Position,
			Normal;
	FLOAT	U,
			V;
	FLOAT	U2,
			V2;

	ENGINE_API friend FArchive& operator<<(FArchive& Ar,FPosNormTexData& V)
	{
		return Ar
			<< V.Position
			<< V.Normal
			<< V.U
			<< V.V
			<< V.U2
			<< V.V2;
	}
};

class ENGINE_API UVertexStreamPosNormTex : public UVertexStreamBase
{
	DECLARE_CLASS(UVertexStreamPosNormTex,UVertexStreamBase,0,Engine)

	TArray<FPosNormTexData>	Data;

	UVertexStreamPosNormTex() : UVertexStreamBase( sizeof(FPosNormTexData), 0, ST_POSNORMTEX )  {}
	UVertexStreamPosNormTex( DWORD InPolyFlags ) : UVertexStreamBase( sizeof(FPosNormTexData), InPolyFlags, ST_POSNORMTEX) {}

	// UVertexStream Interface
	void* GetData() { return &Data(0); }
	INT GetDataSize() { return Data.Num() * sizeof(FPosNormTexData); }

	// UObject Interface
	virtual void Serialize(FArchive& Ar)
	{
		guard(UVertexStreamPosNormTex::Serialize);
		Super::Serialize(Ar);
		Ar << Data;
		unguard;
	}
};



/*------------------------------------------------------------------------------------
	UIndexBuffer.
------------------------------------------------------------------------------------*/
class ENGINE_API UIndexBuffer : public URenderResource
{
	DECLARE_CLASS(UIndexBuffer,URenderResource,0,Engine)

	TArray<_WORD>	Indices;

	static URenderDevice* RenderDevice;

	// UObject interface.
	virtual void Serialize(FArchive& Ar);
};

#endif

/*------------------------------------------------------------------------------------
	The End.
------------------------------------------------------------------------------------*/

