/*=============================================================================
	UC2GLResource.cpp: UC2GLDrv resources.

	Texture upload, render targets, buffer objects; what is cached and when from UC2's D3DDrv (D3DResource.cpp:
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
	RenDev->DeleteDepthStencilRenderbuffer( DepthStencilID );
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
//
// DXT1/3/5 decoded by the driver, from the texture's compressed data (GetTextureData), into
// B,G,R,A bytes (what GL_BGRA uploads). Used where the context has no S3TC (desktop GL without
// EXT_texture_compression_s3tc, or MaskCaps=s3tc; gl4es takes DXT on every GLES context). It replaces
// UTexture::ConvertDXT, which only exists for textures that are UTextures: the BSP lightmaps (DXT1 FTextures of
// the engine) have none. Standard S3TC: DXT1 with its three-colour + transparent mode when c0 <= c1, DXT3 explicit
// 4-bit alpha, DXT5 interpolated alpha.
//
static void UC2GL_Expand565( _WORD C, INT* RGB )
{
	INT R = (C >> 11) & 31, G = (C >> 5) & 63, B = C & 31;
	RGB[0] = (R << 3) | (R >> 2);
	RGB[1] = (G << 2) | (G >> 4);
	RGB[2] = (B << 3) | (B >> 2);
}

static void UC2GL_DecodeDXT( const BYTE* Source, ETextureFormat Format, INT Width, INT Height, BYTE* Dest )
{
	INT BlockBytes	= Format == TEXF_DXT1 ? 8 : 16,
		BlocksX		= (Width + 3) / 4,
		BlocksY		= (Height + 3) / 4;
	for( INT by=0; by<BlocksY; by++ )
		for( INT bx=0; bx<BlocksX; bx++ )
		{
			const BYTE* Block		= Source + (by * BlocksX + bx) * BlockBytes;
			const BYTE* ColorBlock	= Format == TEXF_DXT1 ? Block : Block + 8;
			_WORD C0 = ColorBlock[0] | (ColorBlock[1] << 8), C1 = ColorBlock[2] | (ColorBlock[3] << 8);
			INT Colors[4][4];
			UC2GL_Expand565( C0, Colors[0] );
			UC2GL_Expand565( C1, Colors[1] );
			Colors[0][3] = Colors[1][3] = Colors[2][3] = Colors[3][3] = 255;
			if( Format != TEXF_DXT1 || C0 > C1 )
				for( INT k=0; k<3; k++ )
				{
					Colors[2][k] = (2 * Colors[0][k] + Colors[1][k]) / 3;
					Colors[3][k] = (Colors[0][k] + 2 * Colors[1][k]) / 3;
				}
			else
			{
				for( INT k=0; k<3; k++ )
				{
					Colors[2][k] = (Colors[0][k] + Colors[1][k]) / 2;
					Colors[3][k] = 0;
				}
				Colors[3][3] = 0;
			}
			DWORD Indices = ColorBlock[4] | (ColorBlock[5] << 8) | (ColorBlock[6] << 16) | ((DWORD)ColorBlock[7] << 24);
			// Alpha of DXT5: two end points and eight (or six + 0 + 255) values, 3-bit indices.
			INT Alphas[8];
			QWORD AlphaBits = 0;
			if( Format == TEXF_DXT5 )
			{
				Alphas[0] = Block[0];
				Alphas[1] = Block[1];
				if( Alphas[0] > Alphas[1] )
					for( INT k=1; k<7; k++ )
						Alphas[k+1] = ((7 - k) * Alphas[0] + k * Alphas[1]) / 7;
				else
				{
					for( INT k=1; k<5; k++ )
						Alphas[k+1] = ((5 - k) * Alphas[0] + k * Alphas[1]) / 5;
					Alphas[6] = 0;
					Alphas[7] = 255;
				}
				for( INT k=0; k<6; k++ )
					AlphaBits |= (QWORD)Block[2 + k] << (8 * k);
			}
			for( INT y=0; y<4; y++ )
				for( INT x=0; x<4; x++ )
				{
					INT PX = bx * 4 + x, PY = by * 4 + y;
					if( PX >= Width || PY >= Height )
						continue;
					INT		Texel	= y * 4 + x;
					INT*	C		= Colors[ (Indices >> (2 * Texel)) & 3 ];
					INT		A		= C[3];
					if( Format == TEXF_DXT3 )
						A = ((Block[Texel / 2] >> (4 * (Texel & 1))) & 15) * 17;
					else if( Format == TEXF_DXT5 )
						A = Alphas[ (INT)((AlphaBits >> (3 * Texel)) & 7) ];
					BYTE* Out = Dest + (PY * Width + PX) * 4;
					Out[0] = (BYTE)C[2];
					Out[1] = (BYTE)C[1];
					Out[2] = (BYTE)C[0];
					Out[3] = (BYTE)A;
				}
		}
}

// One mip level of a DXT texture as B,G,R,A bytes, Width x Height (the caller frees it).
static BYTE* UC2GL_DecodeDXTMip( FTexture* Texture, INT MipIndex, ETextureFormat Format, INT Width, INT Height )
{
	INT		BlocksX	= (Width + 3) / 4,
			BlocksY	= (Height + 3) / 4,
			RowBytes= BlocksX * (Format == TEXF_DXT1 ? 8 : 16);
	BYTE* Pixels = new BYTE[ Width * Height * 4 ];
	if( !Texture )
	{
		// A cube map face the cube map leaves empty (UploadMip): zeros.
		appMemzero( Pixels, Width * Height * 4 );
		return Pixels;
	}
	TArray<BYTE> Compressed;
	Compressed.AddZeroed( RowBytes * BlocksY );
	Texture->GetTextureData( MipIndex, &Compressed(0), RowBytes, Format, 0 );
	UC2GL_DecodeDXT( &Compressed(0), Format, Width, Height, Pixels );
	return Pixels;
}

//
// UploadMip with Texture NULL: a cube map face the
// cubemap leaves empty. D3D allocates all six faces of a cube texture and fills those that
// exist, so the cube stays usable; GL needs every face for a complete cube map (an incomplete
// one disables texturing: DM-Azure's pool drew its vertex colour instead of
// T_Liquid_Decay_Water_C_CM, which has no -Z face). The missing face gets zeroed blocks/texels,
// as in D3D's freshly allocated memory.
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

		if( RenDev->SUPPORTS_GL_EXT_texture_compression_s3tc )
		{
			// (GLES: gl4es hands the image to the hardware as it is, transcoded to ASTC or ETC2, or decoded.)
			BYTE* Data = new BYTE[Size];
			appMemzero( Data, Size );
			if( Texture )
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
			// No S3TC: decompress (the driver's own decoder; see UC2GL_DecodeDXT).
			BYTE* Data = UC2GL_DecodeDXTMip( Texture, MipIndex, ActualFormat, MipWidth, MipHeight );
			RenDev->TexImage2D( Target, Level, GL_RGBA, MipWidth, MipHeight, GL_BGRA, GL_UNSIGNED_BYTE, Data );
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
	else if( Texture )
		Texture->GetTextureData( MipIndex, Data, Pitch, ActualFormat, 0 );
	else
		appMemzero( Data, Pitch * MipHeight );

	RenDev->TexImage2D( Target, Level, Internal, MipWidth, MipHeight, Format, Type, Data );
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
				RenDev->TexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, USize, VSize, GL_BGRA, GL_UNSIGNED_BYTE, Data );
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
			// Which of the main framebuffer's buffers a material sees through this texture
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
				RenDev->AttachColorTexture( TextureID );

				if( RenderTarget->CreationFlags & FRenderTarget::RT_CREATE_DEPTH )
				{
					// (Storage and attachment through the device's helpers, which go
					// around gl4es on GLES.)
					DepthStencilID = RenDev->CreateDepthStencilRenderbuffer( USize, VSize );
					RenDev->AttachDepthStencilRenderbuffer( FramebufferID, DepthStencilID );
				}
				else
				{
					// Without RT_CREATE_DEPTH, D3DDrv borrows the back buffer's depth surface
					// (a texture: UUC2GLRenderDevice::CreateMainFramebuffer).
					// OpenGL ES 2 and WebGL 1 want every attachment of a framebuffer to
					// have the same size (WebGL: FRAMEBUFFER_INCOMPLETE_DIMENSIONS for the 16x16
					// luminance target with the 640x480 depth), where D3D and desktop GL use the
					// corner of a larger depth buffer. A smaller target gets no depth buffer
					// there: what the engine draws into such targets (filter and copy tiles)
					// does not depth-test, and without a depth buffer the test passes anyway.
					if( !RenDev->IsGLES || (USize == RenDev->MainWidth && VSize == RenDev->MainHeight) )
						RenDev->AttachMainDepthStencil( FramebufferID );
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
		// The last level the engine has. OpenGL ES has no maximum level: gl4es derives the levels below it, down to 1x1.
		RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, Max(0,NumMips-FirstMip-1) );

		for( INT MipIndex=FirstMip; MipIndex<NumMips; MipIndex++ )
		{
			UploadMip( GL_TEXTURE_2D, Texture, ActualFormat, MipIndex, FirstMip, Width, Height );
			Texture->UnloadRawTextureData( MipIndex );
		}
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
		RenDev->glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, Max(0,NumMips-FirstMip-1) );

		for( INT MipIndex=FirstMip; MipIndex<NumMips; MipIndex++ )
		{
			for( INT FaceIndex=0; FaceIndex<6; FaceIndex++ )
			{
				// An empty face is uploaded too (zeroed, UploadMip), or GL's cube
				// map is incomplete and the stage samples nothing.
				FTexture* Face = Cubemap->GetFace( FaceIndex );
				UploadMip( GL_TEXTURE_CUBE_MAP_POSITIVE_X + FaceIndex, Face, ActualFormat, MipIndex, FirstMip, Width, Height );
				if( Face )
					Face->UnloadRawTextureData( MipIndex );
			}
		}
		RenDev->glBindTexture( GL_TEXTURE_CUBE_MAP, 0 );
	}
	// Not supported. Instead of bombing out set the NULL texture.
	else
		DeleteGLObjects();

	// The interface tracks what is bound per unit; we just changed a binding.
	RenDev->RenderInterface.InvalidateHardwareState();
	if( RenDev->CheckGLErrorsPerDraw )
		RenDev->CheckGLError( *FString::Printf( TEXT("texture upload: %ix%i format %i, %i mips from %i, composite %i cubemap %i render target %i"), Width, Height, (INT)ActualFormat, NumMips, FirstMip, CompositeTexture != NULL, Cubemap != NULL, RenderTarget != NULL ) );

	RenDev->StatTextureUploads++;

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
		RenDev->FreeBuffer( GL_ARRAY_BUFFER, BufferID );
	}
	BufferID = 0;
	unguard;
}

void FOpenGLVertexStream::Cache(FVertexStream* SourceStream)
{
	guard(FOpenGLVertexStream::Cache);

	INT	Size = SourceStream->GetSize();

	if( !BufferID )
		BufferID = RenDev->NewBuffer( GL_ARRAY_BUFFER );

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
	RenDev->StatBufferUploads++; RenDev->StatBufferBytes += Size;
	// (G5: no InvalidateHardwareState: a buffer upload changes no material state; the streams and the index buffer are bound by every draw)

	CachedSize		= Size;
	CachedRevision	= SourceStream->GetRevision();

	unguard;
}

/*-----------------------------------------------------------------------------
	FOpenGLDynamicVertexStream.
-----------------------------------------------------------------------------*/

GLuint FOpenGLUploadPool::Upload( UUC2GLRenderDevice* RenDev, GLenum Target, const void* Data, INT Size, INT Align, INT InitialSize, INT& Offset )
{
	if( RenDev->IsGLES )
	{
		if( Frame != RenDev->FrameCounter )
		{
			Frame	= RenDev->FrameCounter;
			Used	= 0;
		}
		if( Used == Names.Num() )
			Names.AddItem( RenDev->NewBuffer( Target ) );
		GLuint Name = Names(Used++);
		RenDev->glBindBuffer( Target, Name );
		RenDev->glBufferData( Target, Size, Data, GL_STREAM_DRAW );
		Offset = 0;
		return Name;
	}
	if( !Names.Num() )
		Names.AddItem( RenDev->NewBuffer( Target ) );
	GLuint Name = Names(0);
	RenDev->glBindBuffer( Target, Name );
	if( Size > RingSize )
	{
		RingSize	= Max( InitialSize, Size * 2 );
		Tail		= RingSize;
	}
	if( Align > 0 && Tail % Align )
		Tail += Align - (Tail % Align);
	if( Tail + Size > RingSize )
	{
		RenDev->glBufferData( Target, RingSize, NULL, GL_STREAM_DRAW );
		Tail = 0;
	}
	RenDev->glBufferSubData( Target, Tail, Size, Data );
	Offset	= Tail;
	Tail	+= Size;
	return Name;
}

void FOpenGLUploadPool::Free( UUC2GLRenderDevice* RenDev, GLenum Target )
{
	if( RenDev->ContextAlive() )
	{
		RenDev->RenderInterface.InvalidateHardwareState();
		for( INT i=0; i<Names.Num(); i++ )
			RenDev->FreeBuffer( Target, Names(i) );
	}
	Names.Empty();
	Used = Tail = RingSize = 0;
}

FOpenGLDynamicVertexStream::FOpenGLDynamicVertexStream(UUC2GLRenderDevice* InRenDev)
:	FOpenGLVertexStream(InRenDev,NULL)
{
}

FOpenGLDynamicVertexStream::~FOpenGLDynamicVertexStream()
{
	guard(FOpenGLDynamicVertexStream::~FOpenGLDynamicVertexStream);
	Pool.Free( RenDev, GL_ARRAY_BUFFER );
	BufferID = 0;
	unguard;
}

INT FOpenGLDynamicVertexStream::AddVertices(FVertexStream* Stream)
{
	guard(FOpenGLDynamicVertexStream::AddVertices);

	INT	Size	= Stream->GetSize(),
		Stride	= Stream->GetStride();

	if( RenDev->ScratchBuffer.Num() < Size*2 )
		RenDev->ScratchBuffer.Add( Size*2 - RenDev->ScratchBuffer.Num() );
	BYTE* Raw		= &RenDev->ScratchBuffer(0);
	BYTE* Swizzled	= &RenDev->ScratchBuffer(Size);
	Stream->GetStreamData( Raw );

	FVertexComponent	Components[MAX_VERTEX_COMPONENTS];
	INT					NumComponents = Stream->GetComponents( Components );
	UC2GL_CopyVertices( Swizzled, Raw, Size, Stride, Components, NumComponents );

	INT Offset	= 0;
	BufferID	= Pool.Upload( RenDev, GL_ARRAY_BUFFER, Swizzled, Size, Stride, INITIAL_DYNAMIC_VERTEXBUFFER_SIZE, Offset );
	CachedSize	= Size;
	RenDev->StatBufferUploads++; RenDev->StatBufferBytes += Size;
	// (G5: no InvalidateHardwareState: a buffer upload changes no material state; the streams and the index buffer are bound by every draw)

	return Stride ? Offset / Stride : 0;

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
		RenDev->FreeBuffer( GL_ELEMENT_ARRAY_BUFFER, BufferID );
	}
	BufferID = 0;
	unguard;
}

//
// A context without 32-bit indices (WebGL 1 without OES_element_index_uint; MaskCaps=uintindex
// masks them anywhere). 32-bit index data is narrowed to 16 bits in place (the WORDs land at or
// below the DWORDs they come from, so a forward loop is safe). Exact when no index is above
// 65535, which holds for every index buffer this engine makes (it makes no 32-bit ones: only
// FRaw32BitIndexBuffer would, and nothing uses it). Returns 0 if an index does not fit; the
// buffer is then left as zeros (every primitive degenerate: nothing drawn) and that is logged.
//
static UBOOL UC2GL_NarrowIndices( BYTE* Data, INT Count )
{
	const DWORD*	Source	= (const DWORD*)Data;
	_WORD*			Dest	= (_WORD*)Data;
	DWORD			Largest	= 0;
	for( INT i=0; i<Count; i++ )
		Largest = Max( Largest, Source[i] );
	if( Largest > 65535 )
	{
		static UBOOL Logged = 0;
		if( !Logged )
		{
			Logged = 1;
			debugf( NAME_Warning, TEXT("UC2GLDrv: a 32-bit index buffer with index %u, above 65535, on a context without 32-bit indices: not drawn"), Largest );
		}
		appMemzero( Data, Count * sizeof(_WORD) );
		return 0;
	}
	for( INT i=0; i<Count; i++ )
		Dest[i] = (_WORD)Source[i];
	static UBOOL Logged = 0;
	if( !Logged )
	{
		Logged = 1;
		debugf( NAME_Init, TEXT("UC2GLDrv: no 32-bit indices on this context: 32-bit index data is stored as 16-bit (first buffer: %i indices, largest %u)"), Count, Largest );
	}
	return 1;
}

void FOpenGLIndexBuffer::Cache(FIndexBuffer* SourceIndexBuffer)
{
	guard(FOpenGLIndexBuffer::Cache);

	INT	Size = SourceIndexBuffer->GetSize();
	IndexSize = SourceIndexBuffer->GetIndexSize();

	if( !BufferID )
		BufferID = RenDev->NewBuffer( GL_ELEMENT_ARRAY_BUFFER );

	if( RenDev->ScratchBuffer.Num() < Size )
		RenDev->ScratchBuffer.Add( Size - RenDev->ScratchBuffer.Num() );
	SourceIndexBuffer->GetContents( &RenDev->ScratchBuffer(0) );
	INT StoredSize = Size;
	if( IndexSize == sizeof(DWORD) && !RenDev->CapUIntIndex )
	{
		UC2GL_NarrowIndices( &RenDev->ScratchBuffer(0), Size / sizeof(DWORD) );
		IndexSize	= sizeof(_WORD);
		StoredSize	= Size / 2;
	}

	RenDev->glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, BufferID );
	RenDev->glBufferData( GL_ELEMENT_ARRAY_BUFFER, StoredSize, &RenDev->ScratchBuffer(0), GL_STATIC_DRAW );
	RenDev->StatBufferUploads++; RenDev->StatBufferBytes += StoredSize;
	// (G5: no InvalidateHardwareState: a buffer upload changes no material state; the streams and the index buffer are bound by every draw)

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
	ShadowBase	= 0;
}

FOpenGLDynamicIndexBuffer::~FOpenGLDynamicIndexBuffer()
{
	guard(FOpenGLDynamicIndexBuffer::~FOpenGLDynamicIndexBuffer);
	Pool.Free( RenDev, GL_ELEMENT_ARRAY_BUFFER );
	BufferID = 0;
	unguard;
}

INT FOpenGLDynamicIndexBuffer::AddIndices(FIndexBuffer* Indices)
{
	guard(FOpenGLDynamicIndexBuffer::AddIndices);

	INT		SourceSize	= Indices->GetSize();
	// The 32-bit dynamic buffer of a context without 32-bit indices stores 16-bit ones.
	UBOOL	Narrow		= Indices->GetIndexSize() == sizeof(DWORD) && IndexSize == sizeof(_WORD);
	INT		Size		= Narrow ? SourceSize / 2 : SourceSize;
	check( Narrow || Indices->GetIndexSize() == IndexSize );

	if( RenDev->ScratchBuffer.Num() < SourceSize )
		RenDev->ScratchBuffer.Add( SourceSize - RenDev->ScratchBuffer.Num() );
	Indices->GetContents( &RenDev->ScratchBuffer(0) );
	if( Narrow )
		UC2GL_NarrowIndices( &RenDev->ScratchBuffer(0), SourceSize / sizeof(DWORD) );

	INT Offset	= 0;
	BufferID	= Pool.Upload( RenDev, GL_ELEMENT_ARRAY_BUFFER, &RenDev->ScratchBuffer(0), Size, IndexSize, INITIAL_DYNAMIC_INDEXBUFFER_SIZE, Offset );
	CachedSize	= Size;
	RenDev->StatBufferUploads++; RenDev->StatBufferBytes += Size;
	// (G5: no InvalidateHardwareState: a buffer upload changes no material state; the streams and the index buffer are bound by every draw)

	INT	BaseIndex = Offset / IndexSize;
	if( RenDev->IsGLES )
	{
		Shadow.Empty( Size );
		Shadow.Add( Size );
		appMemcpy( &Shadow(0), &RenDev->ScratchBuffer(0), Size );
		ShadowBase = BaseIndex;
	}
	return BaseIndex;

	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
