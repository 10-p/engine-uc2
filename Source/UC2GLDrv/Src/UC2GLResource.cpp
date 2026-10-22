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
	UploadBindTarget= GL_TEXTURE_2D;
	StorageSeenID	= 0;
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
	StorageSeenID = 0;
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
// B,G,R,A bytes (what GL_BGRA uploads). Used where the hardware takes no S3TC (MaskCaps=s3tc, WebGL 1
// without WEBGL_compressed_texture_s3tc). It replaces UTexture::ConvertDXT, which only exists for
// textures that are UTextures: the BSP lightmaps (DXT1 FTextures of the engine) have none. Standard S3TC:
// DXT1 with its
// three-colour + transparent mode when c0 <= c1, DXT3 explicit 4-bit alpha, DXT5 interpolated alpha.
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
// as in D3D's freshly allocated memory; on GLES its mip chain is completed from those zeros.
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

		if( RenDev->UseHardwareS3TC() )
		{
			BYTE* Data = new BYTE[Size];
			appMemzero( Data, Size );
			if( Texture )
				Texture->GetTextureData( MipIndex, Data, RowBytes, ActualFormat, 0 );
			GLenum Format	= ActualFormat == TEXF_DXT1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT :
							  ActualFormat == TEXF_DXT3 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT :
														  GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
			RenDev->CompressedTexImage2D( UploadBindTarget, TextureID, Target, Level, Format, MipWidth, MipHeight, Size, Data, 0 );
			if( Level == 0 )
				CachedGLFormat = Format;
			delete [] Data;
		}
		else
		{
			// No S3TC: decompress (the driver's own decoder; see UC2GL_DecodeDXT).
			BYTE* Data = UC2GL_DecodeDXTMip( Texture, MipIndex, ActualFormat, MipWidth, MipHeight );
			RenDev->TexImage2D( UploadBindTarget, TextureID, Target, Level, GL_RGBA, MipWidth, MipHeight, GL_BGRA, GL_UNSIGNED_BYTE, Data );
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

	RenDev->TexImage2D( UploadBindTarget, TextureID, Target, Level, Internal, MipWidth, MipHeight, Format, Type, Data );
	if( Level == 0 )
		CachedGLFormat = Internal;
	delete [] Data;
}

//
// The levels the engine does not have, down to 1x1, for a context without
// GL_TEXTURE_MAX_LEVEL (OpenGL ES 2, WebGL 1: a mip chain that stops early makes the texture
// incomplete, i.e. black). The engine keeps log2 of the SMALLER side levels, so every
// non-square mipmapped texture is short by one or more. Each missing level is the box
// filter of the one before it, starting from the engine's last level.
//
void FOpenGLTexture::CompleteChain( GLenum Target, FTexture* Texture, ETextureFormat ActualFormat, INT LastMipIndex, INT FirstMip, INT Width, INT Height )
{
	if( RenDev->CapTextureMaxLevel )
		return;
	if( IsDXTC(ActualFormat) && RenDev->UseHardwareS3TC() )
	{
		CompleteCompressedChain( Target, Texture, ActualFormat, LastMipIndex, FirstMip, Width, Height );
		return;
	}
	INT	MipWidth	= Max( Width >> LastMipIndex, 1 ),
		MipHeight	= Max( Height >> LastMipIndex, 1 );
	if( MipWidth == 1 && MipHeight == 1 )
		return;

	// The last level as the device uploaded it.
	GLint	Internal	= GL_RGBA;
	GLenum	Format		= GL_BGRA, Type = GL_UNSIGNED_BYTE;
	INT		Channels	= 4;
	TArray<BYTE> Pixels;
	if( IsDXTC(ActualFormat) )
	{
		BYTE* Data = UC2GL_DecodeDXTMip( Texture, LastMipIndex, ActualFormat, MipWidth, MipHeight );
		Pixels.Add( MipWidth * MipHeight * 4 );
		appMemcpy( &Pixels(0), Data, MipWidth * MipHeight * 4 );
		delete [] Data;
	}
	else if( ActualFormat == TEXF_L8 || ActualFormat == TEXF_A8 )
	{
		Internal = Format = ActualFormat == TEXF_L8 ? GL_LUMINANCE : GL_ALPHA;
		Channels = 1;
		Pixels.AddZeroed( MipWidth * MipHeight );
		if( Texture ) Texture->GetTextureData( LastMipIndex, &Pixels(0), MipWidth, ActualFormat, 0 );
	}
	else if( ActualFormat == TEXF_RGBA8 )
	{
		Pixels.AddZeroed( MipWidth * MipHeight * 4 );
		if( Texture ) Texture->GetTextureData( LastMipIndex, &Pixels(0), MipWidth * 4, ActualFormat, 0 );
	}
	else if( ActualFormat == TEXF_R5G6B5 )
	{
		// Averaged as three 8-bit channels, stored again as 5-6-5.
		TArray<_WORD> Packed;
		Packed.AddZeroed( MipWidth * MipHeight );
		if( Texture ) Texture->GetTextureData( LastMipIndex, &Packed(0), MipWidth * 2, ActualFormat, 0 );
		Channels = 3;
		Pixels.Add( MipWidth * MipHeight * 3 );
		for( INT i=0; i<MipWidth*MipHeight; i++ )
		{
			Pixels(i*3)		= ((Packed(i) >> 11) & 31) * 255 / 31;
			Pixels(i*3+1)	= ((Packed(i) >> 5) & 63) * 255 / 63;
			Pixels(i*3+2)	= (Packed(i) & 31) * 255 / 31;
		}
		Internal = Format = GL_RGB;
		Type = GL_UNSIGNED_SHORT_5_6_5;
	}
	else
	{
		// The bump formats are uploaded as opaque black (UploadMip).
		Pixels.Add( MipWidth * MipHeight * 4 );
		for( INT i=0; i<MipWidth*MipHeight; i++ )
			*(DWORD*)&Pixels(i*4) = 0xFF000000;
	}

	INT Level = LastMipIndex - FirstMip;
	while( MipWidth > 1 || MipHeight > 1 )
	{
		INT	NewWidth	= Max( MipWidth >> 1, 1 ),
			NewHeight	= Max( MipHeight >> 1, 1 ),
			StepX		= MipWidth > 1 ? 2 : 1,
			StepY		= MipHeight > 1 ? 2 : 1;
		TArray<BYTE> Smaller;
		Smaller.Add( NewWidth * NewHeight * Channels );
		for( INT y=0; y<NewHeight; y++ )
			for( INT x=0; x<NewWidth; x++ )
				for( INT c=0; c<Channels; c++ )
				{
					INT Sum = 0;
					for( INT j=0; j<StepY; j++ )
						for( INT i=0; i<StepX; i++ )
							Sum += Pixels( ((y*StepY+j) * MipWidth + x*StepX+i) * Channels + c );
					Smaller( (y*NewWidth+x) * Channels + c ) = (Sum + StepX*StepY/2) / (StepX*StepY);
				}
		Pixels		= Smaller;
		MipWidth	= NewWidth;
		MipHeight	= NewHeight;
		Level++;
		if( Channels == 3 )
		{
			TArray<_WORD> Packed;
			Packed.Add( MipWidth * MipHeight );
			for( INT i=0; i<MipWidth*MipHeight; i++ )
				Packed(i) = ((Pixels(i*3) * 31 + 127) / 255 << 11) | ((Pixels(i*3+1) * 63 + 127) / 255 << 5) | ((Pixels(i*3+2) * 31 + 127) / 255);
			RenDev->TexImage2D( UploadBindTarget, TextureID, Target, Level, Internal, MipWidth, MipHeight, Format, Type, (BYTE*)&Packed(0) );
		}
		else
			RenDev->TexImage2D( UploadBindTarget, TextureID, Target, Level, Internal, MipWidth, MipHeight, Format, Type, &Pixels(0) );
	}
}

//
// A DXT mip chain that the engine ends before 1x1 (it has log2 of the smaller side
// levels, so every non-square texture), completed for a context without
// GL_TEXTURE_MAX_LEVEL when the data goes to the hardware compressed (CompleteChain does
// the same for uncompressed data). Each missing level is one 4x4 block of a
// single colour: the average of the level before it (the last real level is at most a few
// blocks, the rest are 2x1 or 1x1 texels of it).
//
static void UC2GL_DecodeDXTBlockAverage( const BYTE* Block, ETextureFormat Format, INT& R, INT& G, INT& B, INT& A, INT& Count )
{
	const BYTE* ColorBlock = Format == TEXF_DXT1 ? Block : Block + 8;
	_WORD	C0 = ColorBlock[0] | (ColorBlock[1] << 8),
			C1 = ColorBlock[2] | (ColorBlock[3] << 8);
	INT		Colors[4][3];
	for( INT i=0; i<2; i++ )
	{
		_WORD C = i ? C1 : C0;
		Colors[i][0] = ((C >> 11) & 31) * 255 / 31;
		Colors[i][1] = ((C >> 5) & 63) * 255 / 63;
		Colors[i][2] = (C & 31) * 255 / 31;
	}
	UBOOL ThreeColor = Format == TEXF_DXT1 && C0 <= C1;
	for( INT c=0; c<3; c++ )
	{
		Colors[2][c] = ThreeColor ? (Colors[0][c] + Colors[1][c]) / 2 : (2 * Colors[0][c] + Colors[1][c]) / 3;
		Colors[3][c] = ThreeColor ? 0 : (Colors[0][c] + 2 * Colors[1][c]) / 3;
	}
	INT Alphas[8];
	if( Format == TEXF_DXT5 )
	{
		Alphas[0] = Block[0];
		Alphas[1] = Block[1];
		for( INT i=2; i<8; i++ )
			Alphas[i] = Alphas[0] > Alphas[1] ? ((8-i) * Alphas[0] + (i-1) * Alphas[1]) / 7 : (i < 6 ? ((6-i) * Alphas[0] + (i-1) * Alphas[1]) / 5 : (i == 6 ? 0 : 255));
	}
	for( INT t=0; t<16; t++ )
	{
		INT Index = (ColorBlock[4 + t/4] >> ((t%4)*2)) & 3;
		INT Alpha = 255;
		if( Format == TEXF_DXT1 )
			Alpha = (ThreeColor && Index == 3) ? 0 : 255;
		else if( Format == TEXF_DXT3 )
			Alpha = ((Block[t/2] >> ((t%2)*4)) & 15) * 17;
		else
		{
			DWORD Low = Block[2] | (Block[3] << 8) | (Block[4] << 16), High = Block[5] | (Block[6] << 8) | (Block[7] << 16);
			INT Code = t < 8 ? (Low >> (t*3)) & 7 : (High >> ((t-8)*3)) & 7;
			Alpha = Alphas[Code];
		}
		R += Colors[Index][0]; G += Colors[Index][1]; B += Colors[Index][2]; A += Alpha;
		Count++;
	}
}

void FOpenGLTexture::CompleteCompressedChain( GLenum Target, FTexture* Texture, ETextureFormat ActualFormat, INT LastMipIndex, INT FirstMip, INT Width, INT Height )
{
	if( RenDev->CapTextureMaxLevel || !IsDXTC(ActualFormat) || !RenDev->UseHardwareS3TC() )
		return;
	INT	MipWidth	= Max( Width >> LastMipIndex, 1 ),
		MipHeight	= Max( Height >> LastMipIndex, 1 );
	if( MipWidth == 1 && MipHeight == 1 )
		return;

	INT	BlocksX		= (Max( MipWidth, 4 ) + 3) / 4,
		BlocksY		= (Max( MipHeight, 4 ) + 3) / 4,
		BlockBytes	= ActualFormat == TEXF_DXT1 ? 8 : 16,
		RowBytes	= BlocksX * BlockBytes;
	TArray<BYTE> Data;
	Data.AddZeroed( RowBytes * BlocksY );
	if( Texture )
		Texture->GetTextureData( LastMipIndex, &Data(0), RowBytes, ActualFormat, 0 );
	INT R = 0, G = 0, B = 0, A = 0, Count = 0;
	for( INT i=0; i<BlocksX*BlocksY; i++ )
		UC2GL_DecodeDXTBlockAverage( &Data(i*BlockBytes), ActualFormat, R, G, B, A, Count );
	R /= Count; G /= Count; B /= Count; A /= Count;

	BYTE	Block[16];
	appMemzero( Block, sizeof(Block) );
	BYTE*	ColorBlock	= ActualFormat == TEXF_DXT1 ? Block : Block + 8;
	_WORD	Color		= ((R * 31 + 127) / 255 << 11) | ((G * 63 + 127) / 255 << 5) | ((B * 31 + 127) / 255);
	ColorBlock[0] = ColorBlock[2] = Color & 255;
	ColorBlock[1] = ColorBlock[3] = Color >> 8;
	if( ActualFormat == TEXF_DXT1 && A < 128 )
		ColorBlock[4] = ColorBlock[5] = ColorBlock[6] = ColorBlock[7] = 0xff;	// c0 <= c1: index 3 is transparent
	else if( ActualFormat == TEXF_DXT3 )
		for( INT i=0; i<8; i++ )
			Block[i] = (A >> 4) * 17;
	else if( ActualFormat == TEXF_DXT5 )
		Block[0] = Block[1] = A;

	GLenum Format	= ActualFormat == TEXF_DXT1 ? GL_COMPRESSED_RGBA_S3TC_DXT1_EXT :
					  ActualFormat == TEXF_DXT3 ? GL_COMPRESSED_RGBA_S3TC_DXT3_EXT :
												  GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
	// Every completed level is that block repeated: a level of a non-square chain can be more than
	// one block in one direction (e.g. 1x16 = 1x4 blocks), and the size of the data must be exactly
	// its number of blocks (WebGL: GL_INVALID_VALUE otherwise; seen on the first browser match).
	INT		MaxBlocks	= ((Max( MipWidth >> 1, 1 ) + 3) / 4) * ((Max( MipHeight >> 1, 1 ) + 3) / 4);
	TArray<BYTE> Repeated;
	Repeated.Add( MaxBlocks * BlockBytes );
	for( INT i=0; i<MaxBlocks; i++ )
		appMemcpy( &Repeated(i * BlockBytes), Block, BlockBytes );
	INT Level = LastMipIndex - FirstMip;
	while( MipWidth > 1 || MipHeight > 1 )
	{
		MipWidth	= Max( MipWidth >> 1, 1 );
		MipHeight	= Max( MipHeight >> 1, 1 );
		Level++;
		INT Blocks = ((MipWidth + 3) / 4) * ((MipHeight + 3) / 4);
		RenDev->CompressedTexImage2D( UploadBindTarget, TextureID, Target, Level, Format, MipWidth, MipHeight, Blocks * BlockBytes, &Repeated(0), 0 );
	}
}

//
// The storage of an engine texture goes to GLES around gl4es (UUC2GLRenderDevice::
// TexImage2D, CompressedTexImage2D), but gl4es' fixed-function emulation only samples a
// texture whose storage it has seen itself, and gl4es generates mipmaps for a texture it
// believes to lack them. So a new texture is first defined through gl4es, once per texture
// name: one RGBA texel (per face), flagged GL_GENERATE_MIPMAP so that gl4es considers its
// mipmaps taken care of.
//
void FOpenGLTexture::PrepareStorage()
{
	if( !RenDev->IsGLES || StorageSeenID == TextureID )
		return;
	static const BYTE Texel[4] = { 255, 255, 255, 255 };
	RenDev->glTexParameteri( UploadBindTarget, GL_GENERATE_MIPMAP, GL_TRUE );
	if( UploadBindTarget == GL_TEXTURE_CUBE_MAP )
		for( INT Face=0; Face<6; Face++ )
			RenDev->glTexImage2D( GL_TEXTURE_CUBE_MAP_POSITIVE_X + Face, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, Texel );
	else
		RenDev->glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, Texel );
	StorageSeenID = TextureID;
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
			UploadBindTarget = GL_TEXTURE_2D;
			PrepareStorage();
			RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			if( !RenDev->IsGLES )
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

			UBOOL	Compressed	= IsDXTC(ActualFormat) && RenDev->UseHardwareS3TC();
			UploadBindTarget = GL_TEXTURE_2D;
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
				RenDev->CompressedTexImage2D( GL_TEXTURE_2D, TextureID, GL_TEXTURE_2D, 0, Format, USize, VSize, Size, Data, 0 );
				CachedGLFormat = Format;
			}
			else
			{
				RenDev->TexImage2D( GL_TEXTURE_2D, TextureID, GL_TEXTURE_2D, 0, GL_RGBA, USize, VSize, GL_BGRA, GL_UNSIGNED_BYTE, Data );
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
		UploadBindTarget = GL_TEXTURE_2D;
		PrepareStorage();
		// The last level the engine has (desktop GL; where there is no max level the chain is
		// completed below).
		if( !RenDev->IsGLES )
			RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, Max(0,NumMips-FirstMip-1) );

		INT Levels = 0;
		for( INT MipIndex=FirstMip; MipIndex<NumMips; MipIndex++ )
		{
			UploadMip( GL_TEXTURE_2D, Texture, ActualFormat, MipIndex, FirstMip, Width, Height );
			if( MipIndex == NumMips-1 && Levels > 0 )
				CompleteChain( GL_TEXTURE_2D, Texture, ActualFormat, MipIndex, FirstMip, Width, Height );
			Texture->UnloadRawTextureData( MipIndex );
			Levels++;
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
		UploadBindTarget = GL_TEXTURE_CUBE_MAP;
		PrepareStorage();
		if( !RenDev->IsGLES )
			RenDev->glTexParameteri( GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, Max(0,NumMips-FirstMip-1) );

		INT Levels = 0;
		for( INT MipIndex=FirstMip; MipIndex<NumMips; MipIndex++ )
		{
			for( INT FaceIndex=0; FaceIndex<6; FaceIndex++ )
			{
				// An empty face is uploaded too (zeroed, UploadMip), or GL's cube
				// map is incomplete and the stage samples nothing.
				FTexture* Face = Cubemap->GetFace( FaceIndex );
				UploadMip( GL_TEXTURE_CUBE_MAP_POSITIVE_X + FaceIndex, Face, ActualFormat, MipIndex, FirstMip, Width, Height );
				if( MipIndex == NumMips-1 && Levels > 0 )
					CompleteChain( GL_TEXTURE_CUBE_MAP_POSITIVE_X + FaceIndex, Face, ActualFormat, MipIndex, FirstMip, Width, Height );
				if( Face )
					Face->UnloadRawTextureData( MipIndex );
			}
			Levels++;
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

GLuint FOpenGLUploadPool::Next( UUC2GLRenderDevice* RenDev, GLenum Target )
{
	if( Frame != RenDev->FrameCounter )
	{
		Frame	= RenDev->FrameCounter;
		Used	= 0;
	}
	if( Used == Names.Num() )
		Names.AddItem( RenDev->NewBuffer( Target ) );
	return Names(Used++);
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
	Used = 0;
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

	BufferID	= Pool.Next( RenDev, GL_ARRAY_BUFFER );
	CachedSize	= Size;
	RenDev->glBindBuffer( GL_ARRAY_BUFFER, BufferID );
	RenDev->glBufferData( GL_ARRAY_BUFFER, Size, Swizzled, GL_STREAM_DRAW );
	RenDev->StatBufferUploads++; RenDev->StatBufferBytes += Size;
	// (G5: no InvalidateHardwareState: a buffer upload changes no material state; the streams and the index buffer are bound by every draw)

	return 0;

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

	BufferID	= Pool.Next( RenDev, GL_ELEMENT_ARRAY_BUFFER );
	CachedSize	= Size;
	RenDev->glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, BufferID );
	RenDev->glBufferData( GL_ELEMENT_ARRAY_BUFFER, Size, &RenDev->ScratchBuffer(0), GL_STREAM_DRAW );
	RenDev->StatBufferUploads++; RenDev->StatBufferBytes += Size;
	// (G5: no InvalidateHardwareState: a buffer upload changes no material state; the streams and the index buffer are bound by every draw)

	if( RenDev->IsGLES )
	{
		Shadow.Empty( Size );
		Shadow.Add( Size );
		appMemcpy( &Shadow(0), &RenDev->ScratchBuffer(0), Size );
		ShadowBase = 0;
	}
	return 0;

	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
