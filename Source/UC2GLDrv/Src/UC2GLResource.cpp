/*=============================================================================
	UC2GLResource.cpp: UC2GLDrv resources.

	GL code from engine-ut2004's OpenGLDrv (OpenGLResource.cpp: texture upload, render
	targets, buffer objects); what is cached and when from UC2's D3DDrv (D3DResource.cpp:
	formats, render target rules, the engine back link, the default render target).
=============================================================================*/

#include "UC2GLDrv.h"

/*-----------------------------------------------------------------------------
	FOpenGLResource.
-----------------------------------------------------------------------------*/

FOpenGLResource::FOpenGLResource(UUC2GLRenderDevice* InRenDev,FRenderResource* InRes)
{
	guard(FOpenGLResource::FOpenGLResource);

	RenDev			= InRenDev;
	CacheId			= INVALID_CACHEID;
	HashNext		= NULL;
	RenderResource	= InRes;
	CachedRevision	= 0;
	LastFrameUsed	= 0;

	// Add this resource to the device resource list.
	NextResource			= RenDev->ResourceList;
	RenDev->ResourceList	= this;

	unguard;
}

FOpenGLResource::~FOpenGLResource()
{
	guard(FOpenGLResource::~FOpenGLResource);

	// Break the link between the engine resource and this one (D3DResourceCleanup.cpp).
	if( RenderResource )
		RenderResource->SetCacheId(INVALID_CACHEID,NULL);
	RenderResource = NULL;

	// Remove this resource from the device resource list.
	if( RenDev->ResourceList != this )
	{
		FOpenGLResource* ResourcePtr = RenDev->ResourceList;
		while( ResourcePtr && ResourcePtr->NextResource != this )
			ResourcePtr = ResourcePtr->NextResource;
		if( ResourcePtr )
			ResourcePtr->NextResource = NextResource;
	}
	else
		RenDev->ResourceList = NextResource;
	NextResource = NULL;

	unguard;
}

/*-----------------------------------------------------------------------------
	FOpenGLTexture.
-----------------------------------------------------------------------------*/

FOpenGLTexture::FOpenGLTexture(UUC2GLRenderDevice* InRenDev,FRenderResource* InRes)
:	FOpenGLResource(InRenDev,InRes)
{
	TextureID		= 0;
	FramebufferID	= 0;
	DepthStencilID	= 0;
	OwnsTexture		= 1;
	IsCubemap		= 0;
	IsRenderTarget	= 0;
	IsDefaultTarget	= 0;
	DefaultUsage	= FRenderTarget::RT_MATERIAL_USE_BACKBUFFER;
	CachedWidth		= 0;
	CachedHeight	= 0;
	CachedFirstMip	= 0;
	CachedNumMips	= 0;
	CachedFormat	= TEXF_RGBA8;
	CachedGLFormat	= 0;
}

FOpenGLTexture::~FOpenGLTexture()
{
	guard(FOpenGLTexture::~FOpenGLTexture);
	DeleteGLObjects();
	unguard;
}

void FOpenGLTexture::DeleteGLObjects()
{
	if( !RenDev->ContextAlive() )
	{
		TextureID = FramebufferID = DepthStencilID = 0;
		return;
	}
	if( FramebufferID )
	{
		// Do not leave a deleted framebuffer bound: the device rebinds its current target.
		RenDev->glDeleteFramebuffers( 1, &FramebufferID );
		FramebufferID = 0;
	}
	if( DepthStencilID )
	{
		RenDev->glDeleteRenderbuffers( 1, &DepthStencilID );
		DepthStencilID = 0;
	}
	if( TextureID && OwnsTexture )
		RenDev->glDeleteTextures( 1, &TextureID );
	TextureID = 0;
}

INT FOpenGLTexture::CalculateFootprint()
{
	INT	Bytes = 0;
	for( INT MipIndex=0; MipIndex<Max(CachedNumMips-CachedFirstMip,1); MipIndex++ )
		Bytes += GetBytesPerPixel( CachedFormat, Max(CachedWidth >> MipIndex,1) * Max(CachedHeight >> MipIndex,1) );
	return IsCubemap ? Bytes * 6 : Bytes;
}

//
// Upload one mip level of an engine texture (FStaticTexture and friends) the way
// FD3DTexture::Cache does: always through FTexture::GetTextureData, which converts
// P8/G16/RGBA7 to RGBA8 and copies DXT blocks unchanged.
//
void FOpenGLTexture::UploadMip( GLenum Target, FTexture* Texture, ETextureFormat ActualFormat, INT MipIndex, INT FirstMip, INT Width, INT Height )
{
	INT		Level		= MipIndex - FirstMip,
			MipWidth	= Max( Width >> MipIndex, 1 ),
			MipHeight	= Max( Height >> MipIndex, 1 );

	if( IsDXTC(ActualFormat) )
	{
		INT		BlockWidth	= Max( MipWidth, 4 ),
				BlockHeight	= Max( MipHeight, 4 ),
				RowBytes	= GetBytesPerPixel( ActualFormat, BlockWidth ) * 4,
				Size		= RowBytes * (BlockHeight / 4);

		if( UUC2GLRenderDevice::SUPPORTS_GL_EXT_texture_compression_s3tc )
		{
			BYTE* Data = new BYTE[Size];
			appMemzero( Data, Size );
			Texture->GetTextureData( MipIndex, Data, RowBytes, ActualFormat, 0 );
			GLenum Format	= ActualFormat == TEXF_DXT1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT :
							  ActualFormat == TEXF_DXT3 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT :
														  GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
			RenDev->glCompressedTexImage2D( Target, Level, Format, MipWidth, MipHeight, 0, Size, Data );
			if( Level == 0 )
				CachedGLFormat = Format;
			delete [] Data;
		}
		else
		{
			// No S3TC: decompress (the engine's own decoder, as OpenGLDrv does).
			BYTE* Data = NULL;
			Texture->GetUTexture()->ConvertDXT( MipIndex, 1, 0, &Data );
			RenDev->glTexImage2D( Target, Level, GL_RGBA, MipWidth, MipHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, Data );
			if( Level == 0 )
				CachedGLFormat = GL_RGBA;
			delete [] Data;
		}
		return;
	}

	GLint	Internal;
	GLenum	Format, Type;
	INT		BytesPerPixel;
	switch( ActualFormat )
	{
	case TEXF_L8:		Internal = GL_LUMINANCE;	Format = GL_LUMINANCE;	Type = GL_UNSIGNED_BYTE;			BytesPerPixel = 1;	break;
	case TEXF_A8:		Internal = GL_ALPHA;		Format = GL_ALPHA;		Type = GL_UNSIGNED_BYTE;			BytesPerPixel = 1;	break;
	case TEXF_R5G6B5:	Internal = GL_RGB;			Format = GL_RGB;		Type = GL_UNSIGNED_SHORT_5_6_5;	BytesPerPixel = 2;	break;
	case TEXF_RGBA8:
	default:			Internal = GL_RGBA;			Format = GL_BGRA;		Type = GL_UNSIGNED_BYTE;			BytesPerPixel = 4;	break;
	}

	// Bump formats (V8U8, L6V5U5, V16U16) are only read by D3DTOP_BUMPENVMAP, which PC's
	// D3DDrv compiles out (BUMPMAP_ENABLE): upload them as opaque black RGBA8.
	UBOOL Unsupported = ActualFormat == TEXF_V8U8 || ActualFormat == TEXF_L6V5U5 || ActualFormat == TEXF_V16U16;

	INT		Pitch	= MipWidth * BytesPerPixel;
	BYTE*	Data	= new BYTE[Pitch * MipHeight];
	if( Unsupported )
	{
		for( INT i=0; i<MipWidth*MipHeight; i++ )
			((DWORD*)Data)[i] = 0xFF000000;
	}
	else
		Texture->GetTextureData( MipIndex, Data, Pitch, ActualFormat, 0 );

	RenDev->glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	RenDev->glTexImage2D( Target, Level, Internal, MipWidth, MipHeight, 0, Format, Type, Data );
	RenDev->glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
	if( Level == 0 )
		CachedGLFormat = Internal;
	delete [] Data;
}

UBOOL FOpenGLTexture::Cache(FBaseTexture* SourceTexture)
{
	guard(FOpenGLTexture::Cache);

	UBOOL	Failed = 0;
	INT		FirstMip = SourceTexture->GetFirstMip(),
			Width	 = SourceTexture->GetWidth(),
			Height	 = SourceTexture->GetHeight(),
			NumMips  = SourceTexture->GetNumMips();

	// Determine the source texture format and the one stored (FD3DTexture::Cache).
	ETextureFormat	SourceFormat = SourceTexture->GetFormat();
	ETextureFormat	ActualFormat =	SourceFormat == TEXF_P8		? TEXF_RGBA8 :
									SourceFormat == TEXF_G16	? TEXF_RGBA8 :
									SourceFormat == TEXF_RGBA7	? TEXF_RGBA8 :
									SourceFormat;

	FCompositeTexture*	CompositeTexture	= SourceTexture->GetCompositeTextureInterface();
	FCubemap*			Cubemap				= SourceTexture->GetCubemapInterface();
	FTexture*			Texture				= SourceTexture->GetTextureInterface();
	FRenderTarget*		RenderTarget		= SourceTexture->GetRenderTargetInterface();

	if( Texture != NULL && Texture->GetUTexture() != NULL )
		Texture->GetUTexture()->MemUsage = 0;

	// Verify NumMips.
	INT MinDimension = Min( Width, Height );
	INT LogDimension = 0;
	while( MinDimension > 0 )
	{
		MinDimension >>= 1;
		LogDimension++;
	}

	if( IsDXTC(SourceFormat) && (Width < 4 || Height < 4) )
	{
		if( Texture && Texture->GetUTexture() )
			warnf( TEXT("Texture [%s] is %ix%i and DXT formats require minimum UV dimensions of 4"), Texture->GetUTexture()->GetPathName(), Width, Height );
		return 0;
	}

	// Clamp number of miplevels to avoid crashes.
	NumMips = Min( NumMips, LogDimension );

	// Use first face if cubemaps aren't supported.
	if( Cubemap && !RenDev->SupportsCubemaps )
	{
		Texture = Cubemap->GetFace(0);
		Cubemap = NULL;
	}

	// Calculate the first mipmap to use.
	while( FirstMip < NumMips-1 && ((Width >> FirstMip) > RenDev->DeviceCaps.MaxTextureSize || (Height >> FirstMip) > RenDev->DeviceCaps.MaxTextureSize) )
		FirstMip++;

	INT	USize = Width  >> FirstMip,
		VSize = Height >> FirstMip;

	if( !USize || !VSize )
	{
		if( Texture && Texture->GetUTexture() )
			debugf( TEXT("ERROR! ZERO USIZE OR VSIZE ON TEXTURE: %s"), Texture->GetUTexture()->GetFullName() );
		DeleteGLObjects();
	}
	//
	//	CompositeTexture (lightmaps)
	//
	else if( CompositeTexture )
	{
		INT	NumChildren = CompositeTexture->GetNumChildren();

		if( !TextureID || CachedWidth != USize || CachedHeight != VSize || CachedFirstMip != 0 || CachedNumMips != 1 || CachedFormat != ActualFormat )
		{
			DeleteGLObjects();
			RenDev->glGenTextures( 1, &TextureID );
			RenDev->glBindTexture( GL_TEXTURE_2D, TextureID );
			RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0 );
			CachedChildRevision.Empty(NumChildren);
		}
		RenDev->glBindTexture( GL_TEXTURE_2D, TextureID );

		// Children are copied in the composite's own format (RGBA8 or DXT1 lightmaps),
		// then the whole image is uploaded when any child changed.
		UBOOL Changed = CachedChildRevision.Num() != NumChildren;
		if( !Changed )
			for( INT ChildIndex = 0; ChildIndex < NumChildren; ChildIndex++ )
			{
				INT ChildX = 0, ChildY = 0;
				if( CompositeTexture->GetChild(ChildIndex,&ChildX,&ChildY)->GetRevision() != CachedChildRevision(ChildIndex) )
					Changed = 1;
			}

		if( Changed )
		{
			CachedChildRevision.Empty(NumChildren);
			CachedChildRevision.AddZeroed(NumChildren);

			UBOOL	Compressed	= IsDXTC(ActualFormat) && RenDev->SUPPORTS_GL_EXT_texture_compression_s3tc;
			ETextureFormat ChildFormat = Compressed || !IsDXTC(ActualFormat) ? ActualFormat : TEXF_RGBA8;
			INT		Pitch		= IsDXTC(ChildFormat) ? GetBytesPerPixel(ChildFormat,USize) * 4 : GetBytesPerPixel(ChildFormat,USize),
					Rows		= IsDXTC(ChildFormat) ? VSize / 4 : VSize,
					Size		= Pitch * Rows;
			BYTE*	Data		= new BYTE[Size];
			appMemzero( Data, Size );

			for( INT ChildIndex = 0; ChildIndex < NumChildren; ChildIndex++ )
			{
				INT			ChildX = 0,
							ChildY = 0;
				FTexture*	Child = CompositeTexture->GetChild(ChildIndex,&ChildX,&ChildY);
				BYTE*		TileData = CalculateTexelPointer(Data,ChildFormat,Pitch,ChildX,ChildY);
				Child->GetTextureData( 0, TileData, Pitch, ChildFormat, 0 );
				CachedChildRevision(ChildIndex) = Child->GetRevision();
			}

			if( IsDXTC(ChildFormat) )
			{
				GLenum Format	= ChildFormat == TEXF_DXT1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT :
								  ChildFormat == TEXF_DXT3 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT :
															 GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
				RenDev->glCompressedTexImage2D( GL_TEXTURE_2D, 0, Format, USize, VSize, 0, Size, Data );
				CachedGLFormat = Format;
			}
			else
			{
				RenDev->glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, USize, VSize, 0, GL_BGRA, GL_UNSIGNED_BYTE, Data );
				CachedGLFormat = GL_RGBA;
			}
			delete [] Data;
		}
		NumMips		= 1;
		FirstMip	= 0;
	}
	//
	//	RenderTarget
	//
	else if( RenderTarget )
	{
		IsRenderTarget = 1;
		if( RenderTarget == RenDev->DefaultRenderTarget )
		{
			// The main framebuffer: not owned (UD3DRenderDevice's back buffer surfaces).
			DeleteGLObjects();
			IsDefaultTarget	= 1;
			// M7: which of the main framebuffer's buffers a material sees through this texture
			// (the default target has one cache id per usage). What is bound when a stage
			// samples it is decided in UUC2GLRenderDevice::GetSampledTexture.
			DefaultUsage	= RenderTarget->MaterialUsageFlag;
			OwnsTexture		= 0;
			TextureID		= RenDev->MainColorTexture;
			FramebufferID	= 0;
			USize			= RenDev->MainWidth;
			VSize			= RenDev->MainHeight;
		}
		else
		{
			if( !TextureID || !OwnsTexture || CachedWidth != USize || CachedHeight != VSize || CachedFormat != ActualFormat )
			{
				DeleteGLObjects();
				OwnsTexture = 1;
				// PC's D3DDrv cannot render to L8 and uses A8R8G8B8 instead; so do we, for
				// every format (a GL colour attachment must be renderable).
				RenDev->glGenTextures( 1, &TextureID );
				RenDev->glBindTexture( GL_TEXTURE_2D, TextureID );
				RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
				RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
				RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0 );
				RenDev->glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, USize, VSize, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL );
				CachedGLFormat = GL_RGBA;

				RenDev->glGenFramebuffers( 1, &FramebufferID );
				RenDev->glBindFramebuffer( GL_FRAMEBUFFER, FramebufferID );
				RenDev->glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, TextureID, 0 );

				if( RenderTarget->CreationFlags & FRenderTarget::RT_CREATE_DEPTH )
				{
					RenDev->glGenRenderbuffers( 1, &DepthStencilID );
					RenDev->glBindRenderbuffer( GL_RENDERBUFFER, DepthStencilID );
					RenDev->glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, USize, VSize );
					RenDev->glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, DepthStencilID );
					RenDev->glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, DepthStencilID );
				}
				else
				{
					// Without RT_CREATE_DEPTH, D3DDrv borrows the back buffer's depth surface
					// (a texture since M7: UUC2GLRenderDevice::CreateMainFramebuffer).
					RenDev->glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, RenDev->MainDepthStencil, 0 );
					RenDev->glFramebufferTexture2D( GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, RenDev->MainDepthStencil, 0 );
				}

				GLenum Status = RenDev->glCheckFramebufferStatus( GL_FRAMEBUFFER );
				if( Status != GL_FRAMEBUFFER_COMPLETE )
				{
					debugf( NAME_Warning, TEXT("UC2GLDrv: render target %ix%i incomplete (0x%04x)"), USize, VSize, Status );
					Failed = 1;
				}
				// The interface rebinds whatever target is current after a cache.
				RenDev->glBindFramebuffer( GL_FRAMEBUFFER, RenDev->RenderInterface.SavedRenderTargetState.GetCurrentState()->Framebuffer );
				if( Failed )
					DeleteGLObjects();
			}
		}
		NumMips		= 1;
		FirstMip	= 0;
	}
	//
	//	Texture
	//
	else if( Texture )
	{
		if( !TextureID || IsCubemap || CachedWidth != USize || CachedHeight != VSize || CachedFirstMip != FirstMip || CachedNumMips != NumMips || CachedFormat != ActualFormat )
		{
			DeleteGLObjects();
			IsCubemap = 0;
			RenDev->glGenTextures( 1, &TextureID );
		}
		RenDev->glBindTexture( GL_TEXTURE_2D, TextureID );

		INT Levels = 0;
		for( INT MipIndex=FirstMip; MipIndex<NumMips; MipIndex++ )
		{
			UploadMip( GL_TEXTURE_2D, Texture, ActualFormat, MipIndex, FirstMip, Width, Height );
			Texture->UnloadRawTextureData( MipIndex );
			Levels++;
		}
		RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, Max(0,Levels-1) );
	}
	//
	//	Cubemap
	//
	else if( Cubemap )
	{
		if( !TextureID || !IsCubemap || CachedWidth != USize || CachedHeight != VSize || CachedFirstMip != FirstMip || CachedNumMips != NumMips || CachedFormat != ActualFormat )
		{
			DeleteGLObjects();
			IsCubemap = 1;
			RenDev->glGenTextures( 1, &TextureID );
		}
		RenDev->glBindTexture( GL_TEXTURE_CUBE_MAP, TextureID );

		INT Levels = 0;
		for( INT MipIndex=FirstMip; MipIndex<NumMips; MipIndex++ )
		{
			for( INT FaceIndex=0; FaceIndex<6; FaceIndex++ )
			{
				FTexture* Face = Cubemap->GetFace( FaceIndex );
				if( Face )
				{
					UploadMip( GL_TEXTURE_CUBE_MAP_POSITIVE_X + FaceIndex, Face, ActualFormat, MipIndex, FirstMip, Width, Height );
					Face->UnloadRawTextureData( MipIndex );
				}
			}
			Levels++;
		}
		RenDev->glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, Max(0,Levels-1) );
		RenDev->glBindTexture( GL_TEXTURE_CUBE_MAP, 0 );
	}
	// Not supported. Instead of bombing out set the NULL texture.
	else
		DeleteGLObjects();

	// The interface tracks what is bound per unit; we just changed a binding.
	RenDev->RenderInterface.InvalidateHardwareState();

	// Update the cached info.
	CachedRevision	= SourceTexture->GetRevision();
	CachedWidth		= USize;
	CachedHeight	= VSize;
	CachedFirstMip	= FirstMip;
	CachedNumMips	= NumMips;
	CachedFormat	= ActualFormat;

	return Failed;

	unguard;
}

/*-----------------------------------------------------------------------------
	FOpenGLDefaultRenderTarget.
-----------------------------------------------------------------------------*/

FOpenGLDefaultRenderTarget::FOpenGLDefaultRenderTarget( UUC2GLRenderDevice* InRenDev )
{
	RenDev		= InRenDev;
	Width		= 0;
	Height		= 0;
	Format		= TEXF_RGBA8;
	Revision	= 1;
	ResetCacheIDs();
}

void FOpenGLDefaultRenderTarget::UpdateSurfaces( INT InWidth, INT InHeight )
{
	Width	= InWidth;
	Height	= InHeight;
	Revision++;
}

void FOpenGLDefaultRenderTarget::ResetCacheIDs()
{
	CacheIdBack = CacheIdFront = CacheIdDepth = INVALID_CACHEID;
}

_WORD FOpenGLDefaultRenderTarget::GetCacheId()
{
	if( MaterialUsageFlag == FRenderTarget::RT_MATERIAL_USE_DEPTHBUFFER )
		return CacheIdDepth;
	else if( MaterialUsageFlag == FRenderTarget::RT_MATERIAL_USE_FRONTBUFFER )
		return CacheIdFront;
	return CacheIdBack;
}

void FOpenGLDefaultRenderTarget::SetCacheId(_WORD NewCacheID,URenderDevice* pRenDev)
{
	if( MaterialUsageFlag == FRenderTarget::RT_MATERIAL_USE_DEPTHBUFFER )
		CacheIdDepth = NewCacheID;
	else if( MaterialUsageFlag == FRenderTarget::RT_MATERIAL_USE_FRONTBUFFER )
		CacheIdFront = NewCacheID;
	else
		CacheIdBack = NewCacheID;
	m_pRenDev = pRenDev;
}

/*-----------------------------------------------------------------------------
	Vertex data copy.
-----------------------------------------------------------------------------*/

static INT ComponentSize( BYTE Type )
{
	switch( Type )
	{
	case CT_Float4:	return 16;
	case CT_Float3:	return 12;
	case CT_Float2:	return 8;
	case CT_Float1:
	case CT_Color:	return 4;
	default:		return 0;	// the packed Xbox formats never reach a PC driver
	}
}

void UC2GL_CopyVertices( BYTE* Dest, const BYTE* Src, INT NumBytes, INT Stride, FVertexComponent* Components, INT NumComponents )
{
	appMemcpy( Dest, Src, NumBytes );

	// D3DCOLOR is B,G,R,A in memory; the GL colour arrays read R,G,B,A.
	INT Offset = 0;
	for( INT i=0; i<NumComponents; i++ )
	{
		if( Components[i].Type == CT_Color && Stride > 0 )
		{
			for( INT At = Offset; At + 4 <= NumBytes; At += Stride )
				Exchange( Dest[At+0], Dest[At+2] );
		}
		Offset += ComponentSize( Components[i].Type );
	}
}

/*-----------------------------------------------------------------------------
	FOpenGLVertexStream.
-----------------------------------------------------------------------------*/

FOpenGLVertexStream::FOpenGLVertexStream(UUC2GLRenderDevice* InRenDev,FRenderResource* InRes)
:	FOpenGLResource(InRenDev,InRes)
{
	BufferID	= 0;
	CachedSize	= 0;
}

FOpenGLVertexStream::~FOpenGLVertexStream()
{
	guard(FOpenGLVertexStream::~FOpenGLVertexStream);
	if( BufferID && RenDev->ContextAlive() )
	{
		RenDev->RenderInterface.InvalidateHardwareState();
		RenDev->glDeleteBuffers( 1, &BufferID );
	}
	BufferID = 0;
	unguard;
}

void FOpenGLVertexStream::Cache(FVertexStream* SourceStream)
{
	guard(FOpenGLVertexStream::Cache);

	INT	Size = SourceStream->GetSize();

	if( !BufferID )
		RenDev->glGenBuffers( 1, &BufferID );

	if( RenDev->ScratchBuffer.Num() < Size*2 )
		RenDev->ScratchBuffer.Add( Size*2 - RenDev->ScratchBuffer.Num() );

	BYTE* Raw		= &RenDev->ScratchBuffer(0);
	BYTE* Swizzled	= &RenDev->ScratchBuffer(Size);
	SourceStream->GetStreamData( Raw );

	FVertexComponent	Components[MAX_VERTEX_COMPONENTS];
	INT					NumComponents = SourceStream->GetComponents( Components );
	UC2GL_CopyVertices( Swizzled, Raw, Size, SourceStream->GetStride(), Components, NumComponents );

	RenDev->glBindBuffer( GL_ARRAY_BUFFER, BufferID );
	RenDev->glBufferData( GL_ARRAY_BUFFER, Size, Swizzled, GL_STATIC_DRAW );
	RenDev->RenderInterface.InvalidateHardwareState();

	CachedSize		= Size;
	CachedRevision	= SourceStream->GetRevision();

	unguard;
}

/*-----------------------------------------------------------------------------
	FOpenGLDynamicVertexStream.
-----------------------------------------------------------------------------*/

FOpenGLDynamicVertexStream::FOpenGLDynamicVertexStream(UUC2GLRenderDevice* InRenDev)
:	FOpenGLVertexStream(InRenDev,NULL)
{
	Tail = 0;
	Reallocate( INITIAL_DYNAMIC_VERTEXBUFFER_SIZE );
}

void FOpenGLDynamicVertexStream::Reallocate(INT NewSize)
{
	guard(FOpenGLDynamicVertexStream::Reallocate);
	if( !BufferID )
		RenDev->glGenBuffers( 1, &BufferID );
	RenDev->glBindBuffer( GL_ARRAY_BUFFER, BufferID );
	RenDev->glBufferData( GL_ARRAY_BUFFER, NewSize, NULL, GL_STREAM_DRAW );
	RenDev->RenderInterface.InvalidateHardwareState();
	CachedSize	= NewSize;
	Tail		= 0;
	unguard;
}

INT FOpenGLDynamicVertexStream::AddVertices(FVertexStream* Stream)
{
	guard(FOpenGLDynamicVertexStream::AddVertices);

	INT	Size	= Stream->GetSize(),
		Stride	= Stream->GetStride();

	if( Size > CachedSize )
		Reallocate( Size * 2 );

	// Align the tail to the stride so the first vertex has an index; orphan when full
	// (D3DLOCK_DISCARD in FD3DDynamicVertexStream).
	if( Stride > 0 && Tail % Stride )
		Tail += Stride - (Tail % Stride);
	if( Tail + Size > CachedSize )
	{
		RenDev->glBindBuffer( GL_ARRAY_BUFFER, BufferID );
		RenDev->glBufferData( GL_ARRAY_BUFFER, CachedSize, NULL, GL_STREAM_DRAW );
		Tail = 0;
	}

	if( RenDev->ScratchBuffer.Num() < Size*2 )
		RenDev->ScratchBuffer.Add( Size*2 - RenDev->ScratchBuffer.Num() );
	BYTE* Raw		= &RenDev->ScratchBuffer(0);
	BYTE* Swizzled	= &RenDev->ScratchBuffer(Size);
	Stream->GetStreamData( Raw );

	FVertexComponent	Components[MAX_VERTEX_COMPONENTS];
	INT					NumComponents = Stream->GetComponents( Components );
	UC2GL_CopyVertices( Swizzled, Raw, Size, Stride, Components, NumComponents );

	RenDev->glBindBuffer( GL_ARRAY_BUFFER, BufferID );
	RenDev->glBufferSubData( GL_ARRAY_BUFFER, Tail, Size, Swizzled );
	RenDev->RenderInterface.InvalidateHardwareState();

	INT	BaseVertexIndex = Stride ? Tail / Stride : 0;
	Tail += Size;
	return BaseVertexIndex;

	unguard;
}

/*-----------------------------------------------------------------------------
	FOpenGLVertexShader.
-----------------------------------------------------------------------------*/

FOpenGLVertexShader::FOpenGLVertexShader(UUC2GLRenderDevice* InRenDev,FShaderDeclaration& InDeclaration,QWORD InVertexShaderEnum)
{
	RenDev				= InRenDev;
	Declaration			= InDeclaration;
	VertexShaderEnum	= InVertexShaderEnum;

	NextVertexShader	= RenDev->VertexShaders;
	RenDev->VertexShaders = this;
}

FOpenGLVertexShader::~FOpenGLVertexShader()
{
	FOpenGLVertexShader* ShaderPtr;
	if( RenDev->VertexShaders != this )
	{
		ShaderPtr = RenDev->VertexShaders;
		while( ShaderPtr && ShaderPtr->NextVertexShader != this )
			ShaderPtr = ShaderPtr->NextVertexShader;
		if( ShaderPtr )
			ShaderPtr->NextVertexShader = NextVertexShader;
	}
	else
		RenDev->VertexShaders = NextVertexShader;
	NextVertexShader = NULL;
}

/*-----------------------------------------------------------------------------
	FOpenGLIndexBuffer.
-----------------------------------------------------------------------------*/

FOpenGLIndexBuffer::FOpenGLIndexBuffer(UUC2GLRenderDevice* InRenDev,FRenderResource* InRes)
:	FOpenGLResource(InRenDev,InRes)
{
	BufferID	= 0;
	CachedSize	= 0;
	IndexSize	= sizeof(_WORD);
}

FOpenGLIndexBuffer::~FOpenGLIndexBuffer()
{
	guard(FOpenGLIndexBuffer::~FOpenGLIndexBuffer);
	if( BufferID && RenDev->ContextAlive() )
	{
		RenDev->RenderInterface.InvalidateHardwareState();
		RenDev->glDeleteBuffers( 1, &BufferID );
	}
	BufferID = 0;
	unguard;
}

void FOpenGLIndexBuffer::Cache(FIndexBuffer* SourceIndexBuffer)
{
	guard(FOpenGLIndexBuffer::Cache);

	INT	Size = SourceIndexBuffer->GetSize();
	IndexSize = SourceIndexBuffer->GetIndexSize();

	if( !BufferID )
		RenDev->glGenBuffers( 1, &BufferID );

	if( RenDev->ScratchBuffer.Num() < Size )
		RenDev->ScratchBuffer.Add( Size - RenDev->ScratchBuffer.Num() );
	SourceIndexBuffer->GetContents( &RenDev->ScratchBuffer(0) );

	RenDev->glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, BufferID );
	RenDev->glBufferData( GL_ELEMENT_ARRAY_BUFFER, Size, &RenDev->ScratchBuffer(0), GL_STATIC_DRAW );
	RenDev->RenderInterface.InvalidateHardwareState();

	CachedSize		= Size;
	CachedRevision	= SourceIndexBuffer->GetRevision();

	unguard;
}

/*-----------------------------------------------------------------------------
	FOpenGLDynamicIndexBuffer.
-----------------------------------------------------------------------------*/

FOpenGLDynamicIndexBuffer::FOpenGLDynamicIndexBuffer(UUC2GLRenderDevice* InRenDev,INT InIndexSize)
:	FOpenGLIndexBuffer(InRenDev,NULL)
{
	IndexSize	= InIndexSize;
	Tail		= 0;
	Reallocate( INITIAL_DYNAMIC_INDEXBUFFER_SIZE );
}

void FOpenGLDynamicIndexBuffer::Reallocate(INT NewSize)
{
	guard(FOpenGLDynamicIndexBuffer::Reallocate);
	if( !BufferID )
		RenDev->glGenBuffers( 1, &BufferID );
	RenDev->glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, BufferID );
	RenDev->glBufferData( GL_ELEMENT_ARRAY_BUFFER, NewSize, NULL, GL_STREAM_DRAW );
	RenDev->RenderInterface.InvalidateHardwareState();
	CachedSize	= NewSize;
	Tail		= 0;
	unguard;
}

INT FOpenGLDynamicIndexBuffer::AddIndices(FIndexBuffer* Indices)
{
	guard(FOpenGLDynamicIndexBuffer::AddIndices);

	INT	Size = Indices->GetSize();
	check( Indices->GetIndexSize() == IndexSize );

	if( Size > CachedSize )
		Reallocate( Size * 2 );
	if( Tail + Size > CachedSize )
	{
		RenDev->glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, BufferID );
		RenDev->glBufferData( GL_ELEMENT_ARRAY_BUFFER, CachedSize, NULL, GL_STREAM_DRAW );
		Tail = 0;
	}

	if( RenDev->ScratchBuffer.Num() < Size )
		RenDev->ScratchBuffer.Add( Size - RenDev->ScratchBuffer.Num() );
	Indices->GetContents( &RenDev->ScratchBuffer(0) );

	RenDev->glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, BufferID );
	RenDev->glBufferSubData( GL_ELEMENT_ARRAY_BUFFER, Tail, Size, &RenDev->ScratchBuffer(0) );
	RenDev->RenderInterface.InvalidateHardwareState();

	INT	BaseIndex = Tail / IndexSize;
	Tail += Size;
	return BaseIndex;

	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
