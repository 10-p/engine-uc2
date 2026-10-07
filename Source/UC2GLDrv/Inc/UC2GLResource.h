/*=============================================================================
	UC2GLResource.h: UC2GLDrv resources (textures, render targets, vertex and index
	buffers).

	Structure from engine-ut2004's OpenGLDrv (OpenGLResource.h); caching scheme from UC2's
	D3DDrv (D3DResource.h, D3DRenderDevice.h): the engine's FRenderResource carries a
	16-bit cache id that the render device hands out per cache (textures, vertex buffers,
	index buffers), and every driver resource points back at its engine resource so the
	link can be broken from either side.
=============================================================================*/

#ifndef _INC_UC2GLRESOURCE
#define _INC_UC2GLRESOURCE

class UUC2GLRenderDevice;

#define INITIAL_DYNAMIC_VERTEXBUFFER_SIZE	(1024*1024)	// Initial size of the dynamic vertex buffer, in bytes.
#define INITIAL_DYNAMIC_INDEXBUFFER_SIZE	(256*1024)	// Initial size of the dynamic index buffers, in bytes.

//
//	TOpenGLResourceCache: UC2's TResourceCache (D3DRenderDevice.h), unchanged in behaviour.
//	Hands out the 16-bit cache ids, reusing ids of flushed resources.
//
template<class TYPE,_WORD wNumSlots = 1024> class TOpenGLResourceCache
{
	TArray<_WORD>	SlotsAvailable;
	TYPE*			ResourceHash[wNumSlots];
	_WORD			NextCacheID;

public:
	TOpenGLResourceCache()
	{
		Reset();
	}

	FORCEINLINE TYPE* GetCachedResource(_WORD CacheID)
	{
		TYPE* pResource = ResourceHash[CacheID % wNumSlots];
		while( pResource && pResource->CacheId != CacheID )
			pResource = (TYPE*)pResource->HashNext;
		return pResource;
	}

	void Cache(TYPE* pResource)
	{
		_WORD NewCacheID = INVALID_CACHEID;
		if( SlotsAvailable.Num() == 0 )
		{
			NewCacheID = NextCacheID++;
		}
		else
		{
			INT Index = SlotsAvailable.Num() - 1;
			NewCacheID = SlotsAvailable(Index);
			SlotsAvailable.Remove(Index);
		}
		check(NewCacheID < INVALID_CACHEID);
		pResource->CacheId = NewCacheID;
		pResource->HashNext = NULL;
		TYPE* pChain = ResourceHash[NewCacheID % wNumSlots];
		if( pChain == NULL )
			ResourceHash[NewCacheID % wNumSlots] = pResource;
		else
		{
			while( pChain->HashNext != NULL )
				pChain = (TYPE*)pChain->HashNext;
			pChain->HashNext = pResource;
		}
	}

	// Removes an item from the hash and makes its id available again.
	// (UC2's TResourceCache::Flush only freed the id when the slot's chain became empty and
	// could unlink the wrong entry when the flushed resource headed a chain; this version
	// unlinks exactly the resource with that id and always recycles the id.)
	void Flush(_WORD CacheID)
	{
		TYPE** Link = &ResourceHash[CacheID % wNumSlots];
		while( *Link && (*Link)->CacheId != CacheID )
			Link = (TYPE**)&(*Link)->HashNext;
		if( *Link )
		{
			*Link = (TYPE*)(*Link)->HashNext;
			SlotsAvailable.AddItem(CacheID);
		}
	}

	void Reset()
	{
		NextCacheID = 0;
		appMemzero(ResourceHash,sizeof(ResourceHash));
		SlotsAvailable.Empty();
	}
};

//
//	FOpenGLResource
//
class FOpenGLResource
{
public:
	UUC2GLRenderDevice*	RenDev;
	_WORD				CacheId;
	FOpenGLResource*	HashNext;
	FRenderResource*	RenderResource;	// back link to the engine resource (may be NULL)
	INT					CachedRevision;
	FOpenGLResource*	NextResource;
	INT					LastFrameUsed;

	// Constructor/destructor.
	FOpenGLResource(UUC2GLRenderDevice* InRenDev,FRenderResource* InRes);
	virtual ~FOpenGLResource();

	virtual class FOpenGLTexture* GetTexture() { return NULL; }
	virtual class FOpenGLVertexStream* GetVertexStream() { return NULL; }
	virtual class FOpenGLIndexBuffer* GetIndexBuffer() { return NULL; }
	virtual INT CalculateFootprint() { return 0; }
};

//
//	FOpenGLTexture: a 2D texture, a cube map, or a render target (texture + framebuffer).
//
class FOpenGLTexture : public FOpenGLResource
{
public:
	GLuint					TextureID;			// GL texture (not owned when it is the main framebuffer's)
	GLuint					FramebufferID;		// render targets only
	GLuint					DepthStencilID;		// render targets with their own depth buffer only
	GLenum					UploadBindTarget;	// GL_TEXTURE_2D or GL_TEXTURE_CUBE_MAP while Cache uploads
	GLuint					StorageSeenID;		// the texture name gl4es has seen a storage of (PrepareStorage)
	void PrepareStorage();
	// M11: OpenGL ES has no GL_TEXTURE_MAX_LEVEL, so a mip chain must go down to 1x1.
	void CompleteCompressedChain( GLenum Target, FTexture* Texture, ETextureFormat ActualFormat, INT LastMipIndex, INT FirstMip, INT Width, INT Height );
	void CompleteChain( GLenum Target, FTexture* Texture, ETextureFormat ActualFormat, INT LastMipIndex, INT FirstMip, INT Width, INT Height );
	UBOOL					OwnsTexture,
							IsCubemap,
							IsRenderTarget,
							IsDefaultTarget;	// wraps the device's main framebuffer
	INT						DefaultUsage;		// IsDefaultTarget: the FRenderTarget::RT_MATERIAL_USE_* it was cached for

	INT						CachedWidth,
							CachedHeight,
							CachedFirstMip,
							CachedNumMips;
	ETextureFormat			CachedFormat;
	GLenum					CachedGLFormat;		// internal format of level 0, for the footprint
	TArray<INT>				CachedChildRevision;

	FOpenGLTexture(UUC2GLRenderDevice* InRenDev,FRenderResource* InRes);
	virtual ~FOpenGLTexture();

	virtual FOpenGLTexture* GetTexture() { return this; }
	virtual INT CalculateFootprint();

	// Cache - Ensure that the cached texture is up to date. Returns 1 on failure (as FD3DTexture::Cache).
	UBOOL Cache(FBaseTexture* SourceTexture);

private:
	void DeleteGLObjects();
	void UploadMip( GLenum Target, FTexture* Texture, ETextureFormat SourceFormat, INT MipIndex, INT FirstMip, INT Width, INT Height );
};

//
//	FOpenGLDefaultRenderTarget: UC2's FDefaultRenderTarget (D3DResource.h). The engine's
//	"back buffer" render target; here it stands for the device's main framebuffer object.
//
class FOpenGLDefaultRenderTarget : public FRenderTarget
{
public:
	_WORD				CacheIdBack, CacheIdFront, CacheIdDepth;
	INT					Revision, Width, Height;
	ETextureFormat		Format;
	UUC2GLRenderDevice*	RenDev;

	FOpenGLDefaultRenderTarget( UUC2GLRenderDevice* InRenDev );

	void UpdateSurfaces( INT InWidth, INT InHeight );
	void ResetCacheIDs();

	// FRenderResource interface.
	virtual INT GetRevision() { return Revision; }
	virtual _WORD GetCacheId();
	virtual void SetCacheId(_WORD NewCacheID,URenderDevice* pRenDev);

	// FBaseTexture interface.
	virtual INT GetWidth() { return Width; }
	virtual INT GetHeight() { return Height; }
	virtual INT GetFirstMip() { return 0; }
	virtual INT GetNumMips() { return 1; }
	virtual ETextureFormat GetFormat() { return Format; }
	virtual ETexClampMode GetUClamp() { return TC_Clamp; }
	virtual ETexClampMode GetVClamp() { return TC_Clamp; }
};

//
//	FStreamDeclaration / FShaderDeclaration: as in both drivers.
//
struct FStreamDeclaration
{
	FVertexComponent	Components[MAX_VERTEX_COMPONENTS];
	INT					NumComponents;

	FStreamDeclaration()
	{
		NumComponents = 0;
	}
	FStreamDeclaration(FVertexStream* VertexStream)
	{
		NumComponents = VertexStream->GetComponents(Components);
	}
	UBOOL operator==(FStreamDeclaration& Other)
	{
		if(NumComponents != Other.NumComponents)
			return 0;
		for(INT ComponentIndex = 0;ComponentIndex < NumComponents;ComponentIndex++)
		{
			if(Components[ComponentIndex].Type != Other.Components[ComponentIndex].Type ||
				Components[ComponentIndex].Function != Other.Components[ComponentIndex].Function)
				return 0;
		}
		return 1;
	}
};

struct FShaderDeclaration
{
	FStreamDeclaration	Streams[16];
	INT					NumStreams;

	FShaderDeclaration()
	{
		NumStreams = 0;
	}
	UBOOL operator==(FShaderDeclaration& Other)
	{
		if(NumStreams != Other.NumStreams)
			return 0;
		for(INT StreamIndex = 0;StreamIndex < NumStreams;StreamIndex++)
			if(!(Streams[StreamIndex] == Other.Streams[StreamIndex]))
				return 0;
		return 1;
	}
};

//
//	FOpenGLVertexShader: a fixed-function vertex declaration (D3D's FVF shader).
//	The QWORD VertexShaderEnum is recorded and otherwise ignored, as on PC (fixed-function
//	T&L, CPU skinning). M6: GLSL vertex shaders spliced from the .vsh fragments key on it.
//
class FOpenGLVertexShader
{
public:
	QWORD					VertexShaderEnum;
	FShaderDeclaration		Declaration;
	UUC2GLRenderDevice*		RenDev;
	FOpenGLVertexShader*	NextVertexShader;

	FOpenGLVertexShader(UUC2GLRenderDevice* InRenDev,FShaderDeclaration& InDeclaration,QWORD InVertexShaderEnum);
	~FOpenGLVertexShader();
};

//
//	FOpenGLVertexStream: a vertex buffer object (static) or the rolling dynamic buffer.
//	Colours are stored as D3DCOLOR (B,G,R,A bytes) by the engine; they are swizzled to
//	R,G,B,A while uploading so the fixed-function colour arrays read them correctly.
//
class FOpenGLVertexStream : public FOpenGLResource
{
public:
	GLuint		BufferID;
	INT			CachedSize;

	FOpenGLVertexStream(UUC2GLRenderDevice* InRenDev,FRenderResource* InRes);
	virtual ~FOpenGLVertexStream();

	virtual FOpenGLVertexStream* GetVertexStream() { return this; }
	virtual INT CalculateFootprint() { return CachedSize; }

	// Cache - Ensure that the cached vertex stream is up to date.
	void Cache(FVertexStream* SourceStream);
};

//
//	FOpenGLDynamicVertexStream
//
class FOpenGLDynamicVertexStream : public FOpenGLVertexStream
{
public:
	INT		Tail;

	FOpenGLDynamicVertexStream(UUC2GLRenderDevice* InRenDev);

	void Reallocate(INT NewSize);
	// Adds the vertices of Stream at the end of the buffer; returns the index of the first one.
	INT AddVertices(FVertexStream* Stream);
};

//
//	FOpenGLIndexBuffer
//
class FOpenGLIndexBuffer : public FOpenGLResource
{
public:
	GLuint		BufferID;
	INT			CachedSize,
				IndexSize;

	FOpenGLIndexBuffer(UUC2GLRenderDevice* InRenDev,FRenderResource* InRes);
	virtual ~FOpenGLIndexBuffer();

	virtual FOpenGLIndexBuffer* GetIndexBuffer() { return this; }
	virtual INT CalculateFootprint() { return CachedSize; }

	void Cache(FIndexBuffer* SourceIndexBuffer);
};

//
//	FOpenGLDynamicIndexBuffer
//
class FOpenGLDynamicIndexBuffer : public FOpenGLIndexBuffer
{
public:
	INT		Tail;
	// M11: the indices added last, kept on the CPU where wireframe is drawn as lines by the
	// driver (GLES has no polygon mode); ShadowBase is the buffer index of its first element.
	TArray<BYTE>	Shadow;
	INT				ShadowBase;

	FOpenGLDynamicIndexBuffer(UUC2GLRenderDevice* InRenDev,INT InIndexSize);

	void Reallocate(INT NewSize);
	// Adds the indices at the end of the buffer; returns the first index.
	INT AddIndices(FIndexBuffer* Indices);
};

// Copies vertex data, swizzling every CT_Color component from D3DCOLOR to RGBA byte order.
void UC2GL_CopyVertices( BYTE* Dest, const BYTE* Src, INT NumBytes, INT Stride, FVertexComponent* Components, INT NumComponents );

#endif
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
