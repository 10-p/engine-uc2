/*=============================================================================
	D3DResource.h: Unreal Direct3D resource definition.
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/

#ifndef HEADER_D3DRESOURCE
#define HEADER_D3DRESOURCE

// scion sz -- see PixelShaderMaterial.uc
// enum EPixelShader

//
//	GetResourceHashIndex
//
INT GetResourceHashIndex(QWORD CacheId);

//
//	FD3DResource
//
class FD3DResource
{
public:

	UD3DRenderDevice*	RenDev;
	//scion jg -- Changed the following during the rewrite of the resource
	// caching system. A whole lot faster and uses less memory.
	_WORD				CacheId;
	FD3DResource*		HashNext;
	FRenderResource*	RenderResource;

	INT					CachedRevision;

	FD3DResource*		NextResource;

	INT					LastFrameUsed;
#ifdef _XBOX
	//scion jg -- Whether this resource is UMA loaded or not
	UBOOL bIsUMA;
#endif

	// Constructor/destructor.
	FD3DResource(UD3DRenderDevice* InRenDev,FRenderResource* InRes);
	virtual ~FD3DResource();

	// GetTexture
	virtual class FD3DTexture* GetTexture() { return NULL; }

	// GetVertexStream
	virtual class FD3DVertexStream* GetVertexStream() { return NULL; }

	// GetIndexBuffer
	virtual class FD3DIndexBuffer* GetIndexBuffer() { return NULL; }

	// CalculateFootprint
	virtual INT CalculateFootprint() { return 0; }
};

//
//	FD3DTexture
//
class FD3DTexture : public FD3DResource
{
	//jg -- Clean up routine for UMA texture resources
	void DeleteTexture(void);
	void DeleteCubeMap(void);

#if defined(_XBOX) && defined(XBOX_ASSET_FORMAT)
	//jg -- Mark as coming from an XPR so we can handle cleanup
	UBOOL bCameFromXPR;
#endif

public:

	IDirect3DSurface8*		RenderTargetSurface;
	IDirect3DSurface8*		DepthStencilSurface;

	IDirect3DTexture8*		Direct3DTexture8;
	IDirect3DCubeTexture8*	Direct3DCubeTexture8;
	INT						CachedWidth,
							CachedHeight,
							CachedFirstMip,
							CachedNumMips;
	D3DFORMAT				CachedFormat;

	TArray<INT>				CachedChildRevision;

	// Constructor/destructor.
	FD3DTexture(UD3DRenderDevice* InRenDev,FRenderResource* InRes);
	virtual ~FD3DTexture();

	// GetTexture
	virtual FD3DTexture* GetTexture() { return this; }

	// CalculateFootprint
	virtual INT CalculateFootprint();

	// Cache - Ensure that the cached texture is up to date.
	UBOOL Cache(FBaseTexture* SourceTexture);
};

/* scion ======================================================================
* FDefaultRenderTarget
* Author: sz
* 
* ============================================================================
*/
class FDefaultRenderTarget : public FRenderTarget
{
public:
	IDirect3DSurface8*	BackBufferSurface;
	IDirect3DSurface8*	FrontBufferSurface;
	IDirect3DSurface8*	DepthBufferSurface;

	IDirect3DTexture8*	BackBufferTexture;
	IDirect3DTexture8*	FrontBufferTexture;
	IDirect3DTexture8*	DepthBufferTexture;

	_WORD				CacheIdBack, CacheIdFront, CacheIdDepth;
	INT					Revision, Width, Height;
	ETextureFormat		Format;

	UD3DRenderDevice*	RenDev;

	FDefaultRenderTarget( UD3DRenderDevice* InRenDev );
	~FDefaultRenderTarget();

	void UpdateSurfaces();

	// FRenderResource interface.

	virtual INT GetRevision() { return Revision; }	
	//scion jg -- Special case render target cache functions
	virtual _WORD GetCacheId(void);
	virtual void SetCacheId(_WORD NewCacheID,URenderDevice*);
	//scion jg -- Special handler for flushing this resource
	void ResetCacheIDs(void);

	// FBaseTexture interface.

	virtual INT GetWidth() { return Width;	}
	virtual INT GetHeight() { return Height; }
	virtual INT GetFirstMip() { return 0; }
	virtual INT GetNumMips() { return 1; }
	virtual ETextureFormat GetFormat() { return Format; }
	virtual ETexClampMode GetUClamp() { return TC_Clamp; }
	virtual ETexClampMode GetVClamp() { return TC_Clamp; }
};

//
//	FStreamDeclaration
//
struct FStreamDeclaration
{
	FVertexComponent	Components[MAX_VERTEX_COMPONENTS];
	INT					NumComponents;

	FORCEINLINE FStreamDeclaration()
	{
		NumComponents = 0;
	}

	FORCEINLINE FStreamDeclaration(FVertexStream* VertexStream)
	{
		NumComponents = VertexStream->GetComponents(Components);
	}

	FORCEINLINE UBOOL operator==(FStreamDeclaration& Other)
	{
		if(NumComponents != Other.NumComponents)
			return 0;

		// scion sz - a bitmask for these components would be much faster

		for(INT ComponentIndex = 0;ComponentIndex < NumComponents;ComponentIndex++)
		{
			if(Components[ComponentIndex].Type != Other.Components[ComponentIndex].Type ||
				Components[ComponentIndex].Function != Other.Components[ComponentIndex].Function)
				return 0;
		}

		return 1;
	}
};

//
//	FD3DVertexStream
//
class FD3DVertexStream : public FD3DResource
{
public:

	IDirect3DVertexBuffer8*	Direct3DVertexBuffer8;
	INT						CachedSize;

	//scion jg -- This is a single function that contains all cleanup logic
	FORCEINLINE void InternalRelease(void);

	// Constructor/destructor.
	FD3DVertexStream(UD3DRenderDevice* InRenDev,FRenderResource* InRes);
	virtual ~FD3DVertexStream();

	// GetVertexStream
	virtual FD3DVertexStream* GetVertexStream() { return this; }

	// CalculateFootprint
	virtual INT CalculateFootprint();

	// Cache - Ensure that the cached vertex stream is up to date.
	void Cache(FVertexStream* SourceStream);
};

// SL MODIFY BEGIN
#ifdef _XBOX
#define NUM_INTERNAL_BUFFERS 1
#else
#define NUM_INTERNAL_BUFFERS 1
#endif
// SL MODIFY END

//
//	FD3DDynamicVertexStream
//
class FD3DDynamicVertexStream : public FD3DVertexStream
{
public:

	INT	Tail;

	// SL MODIFY BEGIN
	INT	Tails[NUM_INTERNAL_BUFFERS];
	IDirect3DVertexBuffer8* Direct3DVertexBuffer8s[NUM_INTERNAL_BUFFERS];
	INT CachedSizes[NUM_INTERNAL_BUFFERS];
	INT CurrentBuffer;
	// SL MODIFY END

	// Constructor/destructor.
	FD3DDynamicVertexStream(UD3DRenderDevice* InRenDev);

	// Reallocate - Reallocates the dynamic vertex buffer.
	void Reallocate(INT NewSize);

	// AddVertices - Adds the vertices from the given stream to the end of the stream.  
	// Returns the index of the first vertex in the stream.
	INT AddVertices(FVertexStream* Vertices);
};

//
//	FShaderDeclaration
//
struct FShaderDeclaration
{
	FStreamDeclaration	Streams[16];
	INT					NumStreams;

	FORCEINLINE FShaderDeclaration()
	{
		NumStreams = 0;
	}

	FORCEINLINE UBOOL operator==(FShaderDeclaration& Other)
	{
		if(NumStreams != Other.NumStreams)
			return 0;

		for(INT StreamIndex = 0;StreamIndex < NumStreams;StreamIndex++)
		{
			if(!(Streams[StreamIndex] == Other.Streams[StreamIndex]))
				return 0;
		}

		return 1;
	}
};

//	scion sz -
//	FD3DVertexShaderFragments
//
class FD3DVertexShaderFragments
{
public:
	TArray<FString>			FileNames;
	UD3DRenderDevice*		RenDev;

	TArray<DWORD*>			ShaderMicrocodeArray;
	TArray<DWORD>			ShaderMicrocodeSizes;

	FD3DVertexShaderFragments( UD3DRenderDevice* InRenDev );
	~FD3DVertexShaderFragments();

	DWORD* LoadVertexShaderFragment( EVertexShaderCodeFragment FragmentID, DWORD* OutFragmentSize=NULL );
	DWORD* GetVertexShaderFragmentTokenData( EVertexShaderCodeFragment FragmentID );
};

// scion sz - XBOX ONLY 
// FD3DCombinedVertexShader
//
class FD3DCombinedVertexShader
{
public:
	enum { MAX_SHADER_FRAGMENTS=1024 };

	QWORD				VertexShaderEnum;
	DWORD				Handle;
	
	DWORD*				CombinedMicrocode;
	DWORD				CombinedMicrocodeSize;
	DWORD				CombinedMicrocodeInstructions;

	UD3DRenderDevice*	RenDev;

	

#if defined(_DEBUG)
	EVertexShaderCodeFragment	ShaderFragmentEnums[MAX_SHADER_FRAGMENTS];
#endif

	FD3DCombinedVertexShader( UD3DRenderDevice* InRenDev, QWORD InVertexShaderEnum );
	~FD3DCombinedVertexShader();

	//jg -- This ctor takes the data from a package rather than building it
	FD3DCombinedVertexShader(UD3DRenderDevice* InRenDev,QWORD InVertexShaderEnum,
		void* pByteCode,DWORD SizeInBytes);

	INT CalculateFootprint() { return CombinedMicrocodeSize; }

private:
	DWORD AddFragmentToArray( DWORD Index, EVertexShaderCodeFragment FragmentType );
	static DWORD* ShaderTokenArray[MAX_SHADER_FRAGMENTS];
};

//
//	FD3DVertexShader
//
class FD3DVertexShader
{
public:

	QWORD				VertexShaderEnum;
	FShaderDeclaration	Declaration;
	TArray<DWORD>		D3DDeclaration;

	DWORD				Handle;
	DWORD				HandleVS;

	UD3DRenderDevice*	RenDev;
	FD3DVertexShader*	NextVertexShader;

	ANSICHAR*			Source;

	// Constructor/destructor.
	FD3DVertexShader( UD3DRenderDevice* InRenDev, FShaderDeclaration& InDeclaration, QWORD InVertexShaderEnum, ANSICHAR* InSource=NULL );
	~FD3DVertexShader();

private:
	void SetD3DShaderDeclaration( FShaderDeclaration& InDeclaration );
};

//
//	FD3DIndexBuffer
//
class FD3DIndexBuffer : public FD3DResource
{
public:

	IDirect3DIndexBuffer8*	Direct3DIndexBuffer8;
	INT						CachedSize;

	// Constructor/destructor.
	FD3DIndexBuffer(UD3DRenderDevice* InRenDev,FRenderResource* InRes);
	~FD3DIndexBuffer();

	//scion jg -- This is a single function that contains all cleanup logic
	FORCEINLINE void InternalRelease(void);

	// GetIndexBuffer
	virtual FD3DIndexBuffer* GetIndexBuffer() { return this; }

	// CalculateFootprint
	virtual INT CalculateFootprint();

	// Cache - Ensure the cached index buffer is up to date.
	void Cache(FIndexBuffer* SourceIndexBuffer);
};

//
//	FD3DDynamicIndexBuffer
//
class FD3DDynamicIndexBuffer : public FD3DIndexBuffer
{
public:

	INT	Tail;
	INT IndexSize;

#ifdef _XBOX
	//scion jg -- Index buffers do not need to be in contiguous RAM so
	// manage this rather than having D3D do it
	struct FCachedIndexBuffer
	{
		// The D3D structure we'll fix up with the right data
		D3DIndexBuffer IndexBuffer;
		// The dynamic indices for this buffer
		TArray<_WORD> Indices;
	};
	// Contains a group of currently cached buffers
	TArray<FCachedIndexBuffer*> CachedBuffers;
	// Clean up our array in the destructor so we don't leak
	~FD3DDynamicIndexBuffer(); 
#endif

	// Constructor/destructor.
	FD3DDynamicIndexBuffer(UD3DRenderDevice* InRenDev, INT InIndexSize);

	// Reallocate - Reallocates the dynamic index buffer.
	void Reallocate(INT NewSize);

	// AddIndices - Adds the indices from the given buffer to the end of this buffer.  Returns the old tail.
	INT AddIndices(FIndexBuffer* Indices);
};

//
//	FD3DPixelShader
//
class FD3DPixelShader
{
public:
	ANSICHAR*			Source;
	ANSICHAR*			Filename;
	DWORD*				Data;
	DWORD				Handle;
	EPixelShader		Type;

	UD3DRenderDevice*	RenDev;
	FD3DPixelShader*	NextPixelShader;

	// Constructor/destructor.
	FD3DPixelShader(UD3DRenderDevice* InRenDev, EPixelShader InType, 
		ANSICHAR* InSource=NULL, ANSICHAR* InFilename=NULL, DWORD* InData=NULL );
	~FD3DPixelShader();
};


#endif