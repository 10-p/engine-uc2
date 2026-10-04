/*=============================================================================
	UC2GLSelfTest.cpp: numeric checks of the post-process primitives (M7).

	There is no Xbox to compare pictures with, so the operations the effects are built
	from are checked against a small CPU computation of what Epic's Xbox code specifies:
	known patterns go into the main framebuffer or a render target, the operation runs
	through the same engine and driver path the game uses, the pixels are read back.
	One test per primitive operation, not per material:

	  snapshot     the copy that stands for the back buffer when a stage samples it:
	               every pixel, orientation included
	  depth-color  the depth buffer as the Xbox's B8G8R8A8 view of D24S8
	  depth-copy   CopyDepthBuffer into a half-size target: which source texel, which way up
	  blur-h/-v/-box  FilterCopyRenderTarget (PS_Blur) with Epic's 1-2-2-1 and box kernels:
	               tap offsets, weights, D3D's half-texel sample positions
	  bb-copy      UpdateBackBufferCopyTarget: the half-size four-tap copy of the back buffer
	  dof-ramp     PS_DepthOfField: depth -> ramp texture -> blend alpha (absolute mode)
	  remap        PS_ColorRemap + ColorRemap.vsh: luminance -> lookup coordinate

	Console: POSTSELFTEST (runs at the start of the next frame). Command line: -glselftest
	(runs once, at the tenth frame with a player). Results: one log line per test,
	"UC2GLDrv: selftest <name>: PASS|FAIL ...", and a summary line.
	The frame in which the tests run is overdrawn by them; the next one is normal.
=============================================================================*/

#include "UC2GLDrv.h"

static DWORD TestHash( DWORD X, DWORD Y, DWORD C )
{
	DWORD H = X * 73856093u ^ Y * 19349663u ^ C * 83492791u;
	H ^= H >> 13; H *= 0x5bd1e995u; H ^= H >> 15;
	return H;
}

// An RGBA8 picture, top row first (as D3D stores textures), 4 bytes per pixel: R,G,B,A.
struct FTestImage
{
	INT				W, H;
	TArray<BYTE>	Data;
	FTestImage( INT InW, INT InH ) : W(InW), H(InH) { Data.AddZeroed( W * H * 4 ); }
	FORCEINLINE BYTE& At( INT X, INT Y, INT C ) { return Data( (Clamp(Y,0,H-1) * W + Clamp(X,0,W-1)) * 4 + C ); }
	// What a bilinear, clamped read at texture coordinates (U,V) returns, in 0..255.
	FLOAT Bilinear( FLOAT U, FLOAT V, INT C )
	{
		FLOAT	X = U * W - 0.5f, Y = V * H - 0.5f;
		INT		X0 = appFloor(X), Y0 = appFloor(Y);
		FLOAT	FX = X - X0, FY = Y - Y0;
		return	(At(X0,Y0,C) * (1.f-FX) + At(X0+1,Y0,C) * FX) * (1.f-FY)
			+	(At(X0,Y0+1,C) * (1.f-FX) + At(X0+1,Y0+1,C) * FX) * FY;
	}
	void Randomize( DWORD Seed, INT Block=1 )
	{
		for( INT y=0; y<H; y++ )
			for( INT x=0; x<W; x++ )
				for( INT c=0; c<4; c++ )
					At(x,y,c) = c == 3 ? 255 : (BYTE)(TestHash( x/Block + Seed*977, y/Block, c ) & 255);
	}
};

struct FSelfTest
{
	UUC2GLRenderDevice*		RenDev;
	FOpenGLRenderInterface*	RI;
	UViewport*				Viewport;
	INT						Passed, Failed;

	void Report( const TCHAR* Name, INT Compared, FLOAT MaxError, FLOAT Tolerance, const TCHAR* Detail )
	{
		UBOOL Ok = Compared > 0 && MaxError <= Tolerance;
		if( Ok ) Passed++; else Failed++;
		debugf( NAME_Init, TEXT("UC2GLDrv: selftest %s: %s, %i values compared, largest error %.2f of 255 (tolerance %.2f)%s%s"),
			Name, Ok ? TEXT("PASS") : TEXT("FAIL"), Compared, MaxError, Tolerance, *Detail ? TEXT("; ") : TEXT(""), Detail );
	}

	// The main framebuffer's colour, from a top-row-first picture.
	void SetMainColor( FTestImage& Image )
	{
		TArray<BYTE> Rows;
		Rows.Add( Image.W * Image.H * 4 );
		for( INT y=0; y<Image.H; y++ )
			appMemcpy( &Rows((Image.H-1-y)*Image.W*4), &Image.Data(y*Image.W*4), Image.W*4 );
		RenDev->glBindTexture( GL_TEXTURE_2D, RenDev->MainColorTexture );
		RenDev->glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
		RenDev->glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, Image.W, Image.H, GL_RGBA, GL_UNSIGNED_BYTE, &Rows(0) );
		RenDev->MainColorVersion++;
		RI->InvalidateHardwareState();
	}
	// The main framebuffer's depth, from 24-bit values, top row first.
	void SetMainDepth( TArray<DWORD>& Z, INT W, INT H )
	{
		TArray<DWORD> Rows;
		Rows.Add( W * H );
		for( INT y=0; y<H; y++ )
			for( INT x=0; x<W; x++ )
				Rows((H-1-y)*W+x) = Z(y*W+x) << 8;
		RenDev->glBindTexture( GL_TEXTURE_2D, RenDev->MainDepthStencil );
		RenDev->glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
		RenDev->glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, W, H, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, &Rows(0) );
		RenDev->MainDepthVersion++;
		RI->InvalidateHardwareState();
	}
	// A render target's colour, from and to a top-row-first picture (its texture is stored that way).
	void SetTarget( FRenderTarget* Target, FTestImage& Image )
	{
		FOpenGLTexture* Texture = RI->CacheTexture( Target );
		RenDev->glBindTexture( GL_TEXTURE_2D, Texture->TextureID );
		RenDev->glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
		RenDev->glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, Image.W, Image.H, GL_RGBA, GL_UNSIGNED_BYTE, &Image.Data(0) );
		RI->InvalidateHardwareState();
	}
	void ReadFramebuffer( GLuint Framebuffer, FTestImage& Image )
	{
		RenDev->glBindFramebuffer( GL_FRAMEBUFFER, Framebuffer );
		RenDev->glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		RenDev->glReadPixels( 0, 0, Image.W, Image.H, GL_RGBA, GL_UNSIGNED_BYTE, &Image.Data(0) );
		RenDev->glBindFramebuffer( GL_FRAMEBUFFER, RI->SavedRenderTargetState.GetCurrentState()->Framebuffer );
	}
	void ReadTarget( FRenderTarget* Target, FTestImage& Image )
	{
		ReadFramebuffer( RI->CacheTexture( Target )->FramebufferID, Image );
	}

	/*-------------------------------------------------------------------------
		snapshot, depth-color, depth-copy: the driver's own copies.
	-------------------------------------------------------------------------*/

	void TestSnapshot()
	{
		INT W = RenDev->MainWidth, H = RenDev->MainHeight;
		FTestImage Pattern( W, H );
		Pattern.Randomize( 1 );
		SetMainColor( Pattern );

		RenDev->DefaultRenderTarget->SetMaterialUsage( FRenderTarget::RT_MATERIAL_USE_BACKBUFFER );
		FOpenGLTexture* Texture = RI->CacheTexture( RenDev->DefaultRenderTarget );
		RenDev->UpdateSampledTexture( Texture );

		FTestImage Got( W, H );
		ReadFramebuffer( RenDev->SnapshotFramebuffer, Got );
		INT MaxError = 0, UpsideDown = 0;
		for( INT y=0; y<H; y++ )
			for( INT x=0; x<W; x++ )
				for( INT c=0; c<3; c++ )
				{
					MaxError	= Max( MaxError, Abs( (INT)Got.At(x,y,c) - (INT)Pattern.At(x,y,c) ) );
					UpsideDown	= Max( UpsideDown, Abs( (INT)Got.At(x,y,c) - (INT)Pattern.At(x,H-1-y,c) ) );
				}
		Report( TEXT("snapshot"), W*H*3, MaxError, 0.f, *FString::Printf( TEXT("back buffer -> the texture a stage samples, %ix%i, top row first (against the same picture upside down the error would be %i)"), W, H, UpsideDown ) );
	}

	void TestDepth()
	{
		INT W = RenDev->MainWidth, H = RenDev->MainHeight;
		TArray<DWORD> Z;
		Z.Add( W * H );
		for( INT y=0; y<H; y++ )
			for( INT x=0; x<W; x++ )
				Z(y*W+x) = TestHash( x, y, 7 ) & 0xffffff;
		SetMainDepth( Z, W, H );

		// depth-color
		RenDev->DefaultRenderTarget->SetMaterialUsage( FRenderTarget::RT_MATERIAL_USE_DEPTHBUFFER );
		FOpenGLTexture* Texture = RI->CacheTexture( RenDev->DefaultRenderTarget );
		RenDev->DefaultRenderTarget->SetMaterialUsage( FRenderTarget::RT_MATERIAL_USE_BACKBUFFER );
		RenDev->UpdateSampledTexture( Texture );
		FTestImage Got( W, H );
		ReadFramebuffer( RenDev->DepthColorFramebuffer, Got );
		// blue = most significant byte, green = middle, red = least significant. Compared as
		// the 24-bit number: one unit of it is one step of the red byte.
		INT MaxError = 0, Inexact = 0;
		for( INT y=0; y<H; y++ )
			for( INT x=0; x<W; x++ )
			{
				INT GotZ	= (Got.At(x,y,2) << 16) | (Got.At(x,y,1) << 8) | Got.At(x,y,0);
				INT Error	= Abs( GotZ - (INT)Z(y*W+x) );
				MaxError	= Max( MaxError, Error );
				if( Error )
					Inexact++;
			}
		Report( TEXT("depth-color"), W*H, MaxError, 2.f, *FString::Printf( TEXT("24-bit depth -> blue/green/red bytes as D3DFMT_LIN_B8G8R8A8 over D24S8 shows them, top row first; error in units of the 24-bit value, %i of %i values not exact (the depth passes through a 32-bit float)"), Inexact, W*H ) );

		// depth-copy into a half-size target with its own depth buffer (the distortion accumulation target's shape)
		INT HW = W/2, HH = H/2;
		FAuxRenderTarget* Half = new FAuxRenderTarget( HW, HH, TEXF_RGBA8, FRenderTarget::RT_CREATE_COLOR|FRenderTarget::RT_CREATE_DEPTH );
		RI->CopyDepthBuffer( Half, RenDev->DefaultRenderTarget );
		TArray<DWORD> GotZ;
		GotZ.Add( HW * HH );
		RenDev->glBindFramebuffer( GL_FRAMEBUFFER, RI->CacheTexture( Half )->FramebufferID );
		RenDev->glPixelStorei( GL_PACK_ALIGNMENT, 4 );
		RenDev->glReadPixels( 0, 0, HW, HH, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, &GotZ(0) );
		RenDev->glBindFramebuffer( GL_FRAMEBUFFER, RI->SavedRenderTargetState.GetCurrentState()->Framebuffer );
		// The Xbox quad samples the source, point filtered, at (x+0.5, y+0.5) / target size: the
		// corner shared by source texels (2x..2x+1, 2y..2y+1). A tie; the texel right and below.
		INT Wrong = 0, MaxZError = 0, Others[4] = {0,0,0,0};
		for( INT y=0; y<HH; y++ )
			for( INT x=0; x<HW; x++ )
			{
				INT G = GotZ(y*HW+x) >> 8;
				INT Error = Abs( G - (INT)Z( (2*y+1)*W + 2*x+1 ) );
				MaxZError = Max( MaxZError, Error );
				if( Error > 1 )
				{
					Wrong++;
					for( INT k=0; k<4; k++ )
						if( Abs( G - (INT)Z( (2*y+(k>>1))*W + 2*x+(k&1) ) ) <= 1 )
							Others[k]++;
				}
			}
		Report( TEXT("depth-copy"), HW*HH, MaxZError, 1.f, *FString::Printf( TEXT("CopyDepthBuffer into %ix%i: target pixel (x,y) holds the depth of back-buffer pixel (2x+1,2y+1); error in units of the 24-bit depth; %i pixels wrong (of those, matching texel (2x,2y) %i, (2x+1,2y) %i, (2x,2y+1) %i)"), HW, HH, Wrong, Others[0], Others[1], Others[2] ) );
		delete Half;
	}

	/*-------------------------------------------------------------------------
		blur: FilterCopyRenderTarget through PS_Blur.
	-------------------------------------------------------------------------*/

	// Dest pixel (x,y) of a full-target tile with coordinates 0..1 samples at ((x+0.5)/W,
	// (y+0.5)/H): D3D puts pixel centres on integer coordinates, and FScreenPrimUtil's canvas
	// matrix moves the tile half a pixel up and left to make up for it. Each tap adds its
	// offset in source texels.
	FLOAT CompareFilter( FTestImage& Source, FTestImage& Got, FLOAT U0, FLOAT V0, FLOAT USize, FLOAT VSize, FFourSampleFilter& Filter )
	{
		FLOAT MaxError = 0.f;
		for( INT y=0; y<Got.H; y++ )
			for( INT x=0; x<Got.W; x++ )
				for( INT c=0; c<3; c++ )
				{
					FLOAT Sum = 0.f;
					for( INT t=0; t<4; t++ )
						Sum += Filter.SamplePoints[t].Weight * Source.Bilinear(
							U0 + USize * (x + 0.5f) / Got.W + Filter.SamplePoints[t].OffsetU / Source.W,
							V0 + VSize * (y + 0.5f) / Got.H + Filter.SamplePoints[t].OffsetV / Source.H, c );
					MaxError = Max( MaxError, Abs( Sum - (FLOAT)Got.At(x,y,c) ) );
				}
		return MaxError;
	}

	void TestBlur( const TCHAR* Name, FMultiSampleFilter::EMultiSampleType Type, INT SW, INT SH, INT DW, INT DH, DWORD Seed )
	{
		if( !RenDev->PixelShaderFamilyReady( PS_Blur ) )
		{
			Report( Name, 0, 255.f, 0.f, TEXT("PS_Blur is not available") );
			return;
		}
		FAuxRenderTarget* Source	= new FAuxRenderTarget( SW, SH, TEXF_RGBA8, FRenderTarget::RT_CREATE_COLOR );
		FAuxRenderTarget* Dest		= new FAuxRenderTarget( DW, DH, TEXF_RGBA8, FRenderTarget::RT_CREATE_COLOR );
		FTestImage Pattern( SW, SH );
		Pattern.Randomize( Seed );
		SetTarget( Source, Pattern );

		FMultiSampleFilter Sampler( Type );
		FilterCopyRenderTarget( Dest, Source, NULL, Sampler, RI );

		FTestImage Got( DW, DH );
		ReadTarget( Dest, Got );
		FFourSampleFilter& F = Sampler.FilterList(0);
		FLOAT MaxError = CompareFilter( Pattern, Got, 0.f, 0.f, 1.f, 1.f, F );
		Report( Name, DW*DH*3, MaxError, 2.f, *FString::Printf( TEXT("%ix%i -> %ix%i, taps (%g,%g) (%g,%g) (%g,%g) (%g,%g) texels, weights %.3f %.3f %.3f %.3f, bilinear, D3D sample positions"),
			SW, SH, DW, DH, F.SamplePoints[0].OffsetU, F.SamplePoints[0].OffsetV, F.SamplePoints[1].OffsetU, F.SamplePoints[1].OffsetV,
			F.SamplePoints[2].OffsetU, F.SamplePoints[2].OffsetV, F.SamplePoints[3].OffsetU, F.SamplePoints[3].OffsetV,
			F.SamplePoints[0].Weight, F.SamplePoints[1].Weight, F.SamplePoints[2].Weight, F.SamplePoints[3].Weight ) );
		delete Source;
		delete Dest;
	}

	void TestBackBufferCopy()
	{
		INT W = RenDev->MainWidth, H = RenDev->MainHeight;
		FTestImage Pattern( W, H );
		Pattern.Randomize( 3 );
		SetMainColor( Pattern );

		RI->UpdateBackBufferCopyTarget( Viewport );
		FRenderTarget* Copy = RI->BackBufferCopyTarget;
		if( !Copy || Copy == RenDev->DefaultRenderTarget )
		{
			Report( TEXT("bb-copy"), 0, 255.f, 0.f, TEXT("no back-buffer copy target") );
			return;
		}
		FTestImage Got( Copy->GetWidth(), Copy->GetHeight() );
		ReadTarget( Copy, Got );
		// Epic's four taps: (+-1,+-1) back-buffer texels, weights 0.25 (UPSBlurMaterial's defaults).
		FFourSampleFilter F;
		static const FLOAT Off[4][2] = { {-1,-1}, {1,-1}, {-1,1}, {1,1} };
		for( INT t=0; t<4; t++ )
		{
			F.SamplePoints[t].OffsetU	= Off[t][0];
			F.SamplePoints[t].OffsetV	= Off[t][1];
			F.SamplePoints[t].Weight	= 0.25f;
		}
		FLOAT MaxError = CompareFilter( Pattern, Got, 0.f, 0.f, 1.f, 1.f, F );
		Report( TEXT("bb-copy"), Got.W*Got.H*3, MaxError, 2.f, *FString::Printf( TEXT("UpdateBackBufferCopyTarget %ix%i -> %ix%i, four taps at (+-1,+-1) texels, weight 0.25 each"), W, H, Got.W, Got.H ) );
	}

	/*-------------------------------------------------------------------------
		dof-ramp: depth -> ramp -> blend alpha, as UUCScreenDOF::PostRender draws it.
	-------------------------------------------------------------------------*/

	void TestDOFRamp()
	{
		if( !RenDev->PixelShaderFamilyReady( PS_DepthOfField ) )
		{
			Report( TEXT("dof-ramp"), 0, 255.f, 0.f, TEXT("PS_DepthOfField is not available") );
			return;
		}
		INT W = RenDev->MainWidth, H = RenDev->MainHeight;
		const INT	Block	= 16;
		const FLOAT	StartZ	= 0.98f;	// UCScreenDOF's default

		// Depth constant per 16x16 block, from just before StartZ to 1.
		TArray<DWORD> Z;
		Z.Add( W * H );
		for( INT y=0; y<H; y++ )
			for( INT x=0; x<W; x++ )
			{
				DOUBLE T = (TestHash( x/Block, y/Block, 11 ) & 0xffff) / 65535.0;
				Z(y*W+x) = (DWORD)( (0.975 + 0.025 * T) * 16777215.0 );
			}
		SetMainDepth( Z, W, H );

		static FSolidColorTexture White( FColor(255,255,255,255) );
		DECLARE_STATIC_UOBJECT( UProxyBitmapMaterial, WhiteMaterial, { WhiteMaterial->SetTextureInterface( &White ); } );
		DECLARE_STATIC_UOBJECT( UProxyBitmapMaterial, DepthMaterial, {} );
		DepthMaterial->SetTextureInterface( RenDev->DefaultRenderTarget );
		DECLARE_STATIC_UOBJECT( UTexMatrix, DepthTexMatrix, { DepthTexMatrix->Matrix.SetIdentity(); DepthTexMatrix->Material = DepthMaterial; } );
		DECLARE_STATIC_UOBJECT( UTexMatrix, BlurTexMatrix, { BlurTexMatrix->Matrix.SetIdentity(); BlurTexMatrix->Material = WhiteMaterial; } );
		DECLARE_STATIC_UOBJECT( UPSDepthOfFieldMaterial, DOFMaterial, {} );
		DOFMaterial->BlurMaterial			= BlurTexMatrix;
		DOFMaterial->DepthBufferMaterial	= DepthTexMatrix;
		DOFMaterial->AlphaRef				= 0;
		DOFMaterial->FocusNear				= StartZ;
		DOFMaterial->bUpdateRampLookup		= 1;
		RI->UpdateDOFRampLookupTexture( DOFMaterial, 3 /* DOF_FocusAbsoluteZDepth */ );

		FAuxRenderTarget* Dest = new FAuxRenderTarget( W, H, TEXF_RGBA8, FRenderTarget::RT_CREATE_COLOR );
		static FLOAT U1[4] = {0,0,0,0}, V1[4] = {0,0,0,0}, U2[4] = {1,1,1,1}, V2[4] = {1,1,1,1};
		RenDev->DefaultRenderTarget->SetMaterialUsage( FRenderTarget::RT_MATERIAL_USE_DEPTHBUFFER );
		RI->SetRenderTarget( Dest );
		RI->Clear( 1, FColor(0,0,0,0), 0, 0.f, 0, 0 );
		{
			FScreenPrimUtil Tile( Dest, RI );
			Tile.DrawTile( 0, 0, W, H, U1, V1, U2, V2, 0.f, DOFMaterial, FColor(255,255,255,255) );
			Tile.Flush();
		}
		RI->ResetRenderTarget();
		RenDev->DefaultRenderTarget->SetMaterialUsage( FRenderTarget::RT_MATERIAL_USE_BACKBUFFER );

		// The material blends white with the ramp's alpha over black: the red channel is the alpha.
		FTestImage Got( W, H );
		ReadTarget( Dest, Got );
		UTexture* Ramp = DOFMaterial->DynamicRampLookup;
		INT		RW = Ramp->USize, RH = Ramp->VSize, Compared = 0;
		BYTE*	Bits = &Ramp->Mips(0).DataArray(0);
		FLOAT	MaxLookupError = 0.f, MaxFormulaError = 0.f;
		FString	Samples;
		for( INT y=Block/2; y<H; y+=Block )
			for( INT x=Block/2; x<W; x+=Block )
			{
				DWORD V = Z(y*W+x);
				// texreg2gb: u = green (middle byte), v = blue (most significant byte), bilinear in the ramp.
				FLOAT	U = ((V>>8)&255) / 255.f * RW - 0.5f, Vc = ((V>>16)&255) / 255.f * RH - 0.5f;
				INT		U0 = appFloor(U), V0 = appFloor(Vc);
				FLOAT	FU = U - U0, FV = Vc - V0;
				#define RAMP(a,b) ((FLOAT)Bits[ Clamp(b,0,RH-1) * RW + Clamp(a,0,RW-1) ])
				FLOAT Expected = (RAMP(U0,V0)*(1-FU) + RAMP(U0+1,V0)*FU)*(1-FV) + (RAMP(U0,V0+1)*(1-FU) + RAMP(U0+1,V0+1)*FU)*FV;
				#undef RAMP
				if( Abs( Expected - (FLOAT)Got.At(x,y,0) ) > 3.f && Samples.Len() < 400 )
					Samples += FString::Printf( TEXT(" [z=%.5f (bytes %i,%i,%i) expected %.1f got %i,%i,%i,%i]"), V / 16777215.f, (V>>16)&255, (V>>8)&255, V&255, Expected, Got.At(x,y,0), Got.At(x,y,1), Got.At(x,y,2), Got.At(x,y,3) );
				MaxLookupError = Max( MaxLookupError, Abs( Expected - (FLOAT)Got.At(x,y,0) ) );
				// What the effect means: alpha = (z - StartZ) / (1 - StartZ), 0 before StartZ.
				FLOAT Depth = V / 16777215.f;
				FLOAT Meant = Depth < StartZ ? 0.f : 255.f * (Depth - StartZ) / (1.f - StartZ);
				MaxFormulaError = Max( MaxFormulaError, Abs( Meant - (FLOAT)Got.At(x,y,0) ) );
				Compared++;
			}
		Report( TEXT("dof-ramp"), Compared, MaxLookupError, 3.f, *FString::Printf( TEXT("depth -> ramp lookup (u = middle byte, v = high byte) -> blend alpha, one value per 16x16 block of constant depth; against alpha = (z - %.2f)/(1 - %.2f) the largest difference is %.1f (the ramp has 64 columns for 256 values)%s"), StartZ, StartZ, MaxFormulaError, *Samples ) );
		delete Dest;
	}

	/*-------------------------------------------------------------------------
		remap: PS_ColorRemap.
	-------------------------------------------------------------------------*/

	void TestRemap()
	{
		if( !RenDev->PixelShaderFamilyReady( PS_ColorRemap ) )
		{
			Report( TEXT("remap"), 0, 255.f, 0.f, TEXT("PS_ColorRemap is not available") );
			return;
		}
		INT W = RenDev->MainWidth, H = RenDev->MainHeight;
		const INT Block = 8;
		FTestImage Pattern( W, H );
		Pattern.Randomize( 5, Block );
		SetMainColor( Pattern );

		// A lookup texture that answers with its own coordinates: texel (i,j) = (red i, green j).
		// The game's colour ramps are 256x1 (T_FX.ColorRamps): -glselftest1d uses that shape.
		static INT LookupHeight = ParseParam( appCmdLine(), TEXT("glselftest1d") ) ? 1 : 256;
		static UTexture* Lookup = NULL;
		if( !Lookup )
		{
			Lookup = ConstructObject<UTexture>( UTexture::StaticClass(), UObject::GetTransientPackage(), NAME_None, RF_Keep );
			Lookup->Format		= TEXF_RGBA8;
			Lookup->Init( 256, LookupHeight );
			Lookup->UClampMode	= TC_Clamp;
			Lookup->VClampMode	= TC_Clamp;
			FColor* Texels = (FColor*)&Lookup->Mips(0).DataArray(0);
			for( INT j=0; j<LookupHeight; j++ )
				for( INT i=0; i<256; i++ )
					Texels[j*256+i] = FColor( i, LookupHeight > 1 ? j : i, 0, 255 );
			Lookup->bRealtimeChanged = 1;
		}

		DECLARE_STATIC_UOBJECT( UProxyBitmapMaterial, BackBuffer, {} );
		BackBuffer->SetTextureInterface( RenDev->DefaultRenderTarget );
		DECLARE_STATIC_UOBJECT( UTexMatrix, BackBufferTexMatrix, { BackBufferTexMatrix->Matrix.SetIdentity(); BackBufferTexMatrix->Material = BackBuffer; } );
		DECLARE_STATIC_UOBJECT( UPSColorRemapMaterial, ColorRemap,
		{
			ColorRemap->TextureStage0			= BackBufferTexMatrix;
			ColorRemap->bUseZ					= 0;
			ColorRemap->bSetBackbufferManually	= 1;
		});
		ColorRemap->BlendAmount		= 1.f;
		ColorRemap->RemapTexture	= Lookup;

		FAuxRenderTarget* Dest = new FAuxRenderTarget( W, H, TEXF_RGBA8, FRenderTarget::RT_CREATE_COLOR );
		static FLOAT U1[4] = {0,0,0,0}, V1[4] = {0,0,0,0}, U2[4] = {1,1,1,1}, V2[4] = {1,1,1,1};
		RI->SetRenderTarget( Dest );
		RI->Clear( 1, FColor(0,0,0,0), 0, 0.f, 0, 0 );
		{
			FScreenPrimUtil Tile( Dest, RI );
			Tile.DrawTile( 0, 0, W, H, U1, V1, U2, V2, 0.f, ColorRemap, FColor(255,255,255,255) );
			Tile.Flush();
		}
		RI->ResetRenderTarget();

		FTestImage Got( W, H );
		ReadTarget( Dest, Got );
		FLOAT	MaxError = 0.f;
		INT		Compared = 0;
		for( INT y=Block/2; y<H; y+=Block )
			for( INT x=Block/2; x<W; x+=Block )
			{
				// ColorRemap.vsh: both rows of the 3x2 matrix are (0.30, 0.59, 0.11): u = v = luminance.
				FLOAT Luminance	= (0.30f * Pattern.At(x,y,0) + 0.59f * Pattern.At(x,y,1) + 0.11f * Pattern.At(x,y,2)) / 255.f;
				FLOAT Expected	= Clamp( Luminance * 256.f - 0.5f, 0.f, 255.f );
				MaxError = Max( MaxError, Abs( Expected - (FLOAT)Got.At(x,y,0) ) );
				MaxError = Max( MaxError, Abs( Expected - (FLOAT)Got.At(x,y,1) ) );
				Compared += 2;
			}
		Report( TEXT("remap"), Compared, MaxError, 2.f, TEXT("back buffer -> luminance (0.30, 0.59, 0.11) -> lookup at (luminance, luminance), blend amount 1; one value per 8x8 block of constant colour") );
		delete Dest;
	}

	void Run()
	{
		Passed = Failed = 0;
		debugf( NAME_Init, TEXT("UC2GLDrv: selftest of the post-process primitives (frame %i, %ix%i)"), RenDev->FrameCounter, RenDev->MainWidth, RenDev->MainHeight );
		if( !RenDev->PostProcessActive() )
		{
			debugf( NAME_Init, TEXT("UC2GLDrv: selftest: post-processing is off, nothing to test") );
			return;
		}
		RI->PushState();
		TestSnapshot();
		TestDepth();
		TestBlur( TEXT("blur-h"),   FMultiSampleFilter::MULTI_SAMPLE_HORIZ_1221, 64, 32, 32, 32, 21 );
		TestBlur( TEXT("blur-v"),   FMultiSampleFilter::MULTI_SAMPLE_VERT_1221,  32, 32, 32, 16, 22 );
		TestBlur( TEXT("blur-box"), FMultiSampleFilter::MULTI_SAMPLE_BOX,        32, 16, 32, 16, 23 );
		TestBackBufferCopy();
		TestDOFRamp();
		TestRemap();
		RI->PopState();

		// Leave a defined frame behind.
		RenDev->glBindFramebuffer( GL_FRAMEBUFFER, RenDev->MainFramebuffer );
		RI->Clear( 1, FColor(0,0,0,0), 1, 1.f, 1, 0 );
		debugf( NAME_Init, TEXT("UC2GLDrv: selftest summary: %i passed, %i failed"), Passed, Failed );
	}
};

void UC2GL_RunPostProcessSelfTest( UUC2GLRenderDevice* RenDev, UViewport* Viewport )
{
	FSelfTest Test;
	Test.RenDev		= RenDev;
	Test.RI			= &RenDev->RenderInterface;
	Test.Viewport	= Viewport;
	Test.Run();
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
