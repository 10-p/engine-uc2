/*=============================================================================
	UC2GLSelfTest.cpp: numeric checks of the post-process primitives.

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
	  luminance    PS_LuminanceTexIndex into the 16x16 target the dynamic gamma reads
	  fog          fixed-function linear fog
	  pp-sizes     the camera effects' pooled blur targets are the fractions of the
	               reference picture (PostProcessReferenceHeight) Epic's formulas give
	  ref-copy     the 480-line reference copy of the back buffer, against the CPU
	  ref-taps     FilterCopyRenderTarget from the back buffer: taps in reference
	               pixels, read from the reference copy
	  bb-copy-ref  UpdateBackBufferCopyTarget with the reference picture
	               (the tests above pp-sizes run with the reference height 0, as Epic's code;
	               the three ref tests need a back buffer taller than 480 lines)
	  index-narrow only without 32-bit indices (the fallback): 32-bit index data stored as
	               16-bit and drawn

	Console: POSTSELFTEST (runs at the start of the next frame). Test key: SelfTest=True
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
	UBOOL					DepthUploadFailed;

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
		if( !RenDev->UploadMainDepth( &Rows(0), W, H ) )
			DepthUploadFailed = 1;
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
		Depth through points: the way to check a depth buffer where it
		cannot be read back (OpenGL ES 2, WebGL).
	-------------------------------------------------------------------------*/

	GLuint PointProgram;
	GLint  PointColorLoc;
	// The same, with the scene depth test of PSV_SceneDepthTest (UC2GLShaders.cpp) around it.
	GLuint DepthTestProgram;
	GLint  DepthTestColorLoc, DepthTestRectLoc;
	INT    LastProbeRed, LastProbeGreen;	// pixels whose upper / lower bound held in the last probe
	UBOOL  ProbeWithSceneDepthTest;			// DrawDepthPoints draws with DepthTestProgram
	GLuint BuildProbeProgram( UBOOL SceneDepthTest, GLint& ColorLoc )
	{
		static const char* VS = "attribute vec3 aPosition;\nvoid main()\n{\n\tgl_Position = vec4( aPosition, 1.0 );\n}\n";
		static const char* FS = "uniform vec4 uColor;\nvoid main()\n{\n\tgl_FragColor = uColor;\n}\n";
		extern FString UC2GL_WithSceneDepthTest( const ANSICHAR* Body );
		extern FString UC2GL_WithSceneDepthTestVertex( const FString& Source );
		FString FSWithTest = UC2GL_WithSceneDepthTest( FS ), VSWithTest = UC2GL_WithSceneDepthTestVertex( FString( VS ) );
		ANSICHAR FSText[2048], VSText[2048];
		appStrncpy( FSText, TCHAR_TO_ANSI(*FSWithTest), ARRAY_COUNT(FSText) );
		appStrncpy( VSText, TCHAR_TO_ANSI(*VSWithTest), ARRAY_COUNT(VSText) );
		GLuint Shaders[2];
		for( INT i=0; i<2; i++ )
		{
			const char* Sources[2] = { RenDev->ShaderPrologue( i == 1 ), i == 0 ? (SceneDepthTest ? VSText : VS) : (SceneDepthTest ? FSText : FS) };
			Shaders[i] = RenDev->glCreateShader( i == 0 ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER );
			RenDev->glShaderSource( Shaders[i], 2, Sources, NULL );
			RenDev->glCompileShader( Shaders[i] );
		}
		GLuint Program = RenDev->glCreateProgram();
		RenDev->glAttachShader( Program, Shaders[0] );
		RenDev->glAttachShader( Program, Shaders[1] );
		RenDev->glBindAttribLocation( Program, 0, "aPosition" );
		RenDev->glLinkProgram( Program );
		RenDev->glDeleteShader( Shaders[0] );
		RenDev->glDeleteShader( Shaders[1] );
		GLint Status = 0;
		RenDev->glGetProgramiv( Program, GL_LINK_STATUS, &Status );
		if( !Status )
		{
			RenDev->glDeleteProgram( Program );
			return 0;
		}
		ColorLoc = RenDev->glGetUniformLocation( Program, "uColor" );
		return Program;
	}
	UBOOL BuildPointProgram()
	{
		if( !PointProgram )
			PointProgram = BuildProbeProgram( 0, PointColorLoc );
		return PointProgram != 0;
	}
	UBOOL BuildDepthTestProgram()
	{
		if( !DepthTestProgram )
		{
			DepthTestProgram = BuildProbeProgram( 1, DepthTestColorLoc );
			if( DepthTestProgram )
			{
				DepthTestRectLoc = RenDev->glGetUniformLocation( DepthTestProgram, "u_sceneDepthRect" );
				RenDev->glUseProgram( DepthTestProgram );
				RenDev->glUniform1i( RenDev->glGetUniformLocation( DepthTestProgram, "u_sceneDepth" ), 7 );
				RenDev->glUseProgram( 0 );
			}
		}
		return DepthTestProgram != 0;
	}
	// One point per pixel of a W x H framebuffer, at 24-bit depth Z + Offset. Z is the picture
	// top row first; BottomUp says the framebuffer stores it the GL way (the main framebuffer),
	// otherwise row 0 of the picture is row 0 of the framebuffer (render targets).
	void DrawDepthPoints( INT W, INT H, const TArray<DWORD>& Z, INT Offset, const FLOAT* Color, UBOOL BottomUp )
	{
		// (One small triangle around each pixel centre rather than a point: point rasterisation
		// through gl4es drew a single pixel.)
		TArray<FLOAT> Vertices;
		Vertices.Add( W * H * 9 );
		static const FLOAT Corner[3][2] = { { 0.1f, 0.1f }, { 0.9f, 0.1f }, { 0.5f, 0.9f } };
		for( INT y=0; y<H; y++ )
			for( INT x=0; x<W; x++ )
			{
				DOUBLE	D	= Clamp<DOUBLE>( ((DOUBLE)Z(y*W+x) + Offset) / 16777215.0, 0.0, 1.0 );
				INT		Row	= BottomUp ? H-1-y : y;
				for( INT c=0; c<3; c++ )
				{
					FLOAT* V = &Vertices( ((y*W+x) * 3 + c) * 3 );
					V[0] = (x + Corner[c][0]) / W * 2.f - 1.f;
					V[1] = (Row + Corner[c][1]) / H * 2.f - 1.f;
					V[2] = (FLOAT)( D * 2.0 - 1.0 );
				}
			}
		GLuint Buffer = RenDev->NewBuffer( GL_ARRAY_BUFFER );
		RenDev->glBindBuffer( GL_ARRAY_BUFFER, Buffer );
		RenDev->glBufferData( GL_ARRAY_BUFFER, Vertices.Num() * sizeof(FLOAT), &Vertices(0), GL_STATIC_DRAW );
		RenDev->glUseProgram( ProbeWithSceneDepthTest ? DepthTestProgram : PointProgram );
		RenDev->glUniform4fv( ProbeWithSceneDepthTest ? DepthTestColorLoc : PointColorLoc, 1, Color );
		RenDev->glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, 0, NULL );
		RenDev->glEnableVertexAttribArray( 0 );
		RenDev->glDrawArrays( GL_TRIANGLES, 0, W * H * 3 );
		RenDev->glDisableVertexAttribArray( 0 );
		RenDev->glUseProgram( 0 );
		RenDev->glBindBuffer( GL_ARRAY_BUFFER, 0 );
		RenDev->FreeBuffer( GL_ARRAY_BUFFER, Buffer );
	}
	// How many pixels of a framebuffer's depth buffer are not within Bound units of Expected:
	// a point at Expected+Bound must pass "greater or equal", one at Expected-Bound "less or
	// equal". Nothing is written to depth; the framebuffer's colour is used as the tally.
	INT ProbeDepth( GLuint Framebuffer, INT W, INT H, const TArray<DWORD>& Expected, INT Bound, UBOOL BottomUp )
	{
		if( !BuildPointProgram() )
			return W * H;
		RenDev->glBindFramebuffer( GL_FRAMEBUFFER, Framebuffer );
		RenDev->glViewport( 0, 0, W, H );
		RenDev->glDisable( GL_SCISSOR_TEST );
		RenDev->glDisable( GL_STENCIL_TEST );
		RenDev->glDisable( GL_BLEND );
		RenDev->glDisable( GL_ALPHA_TEST );
		RenDev->glDisable( GL_CULL_FACE );
		RenDev->glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
		RenDev->glClearColor( 0.f, 0.f, 0.f, 0.f );
		RenDev->glClear( GL_COLOR_BUFFER_BIT );
		RenDev->glEnable( GL_DEPTH_TEST );
		RenDev->glDepthMask( GL_FALSE );
		static const FLOAT Red[4] = { 1.f, 0.f, 0.f, 0.f }, Green[4] = { 0.f, 1.f, 0.f, 0.f };
		RenDev->glColorMask( GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE );
		RenDev->glDepthFunc( GL_GEQUAL );
		DrawDepthPoints( W, H, Expected, Bound, Red, BottomUp );
		RenDev->glColorMask( GL_FALSE, GL_TRUE, GL_FALSE, GL_FALSE );
		RenDev->glDepthFunc( GL_LEQUAL );
		DrawDepthPoints( W, H, Expected, -Bound, Green, BottomUp );
		RenDev->glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
		RenDev->glDepthFunc( GL_LEQUAL );
		RenDev->glEnable( GL_SCISSOR_TEST );
		FTestImage Tally( W, H );
		RenDev->glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		RenDev->glReadPixels( 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, &Tally.Data(0) );
		INT Wrong = 0;
		for( INT i=0; i<W*H; i++ )
			if( Tally.Data(i*4) != 255 || Tally.Data(i*4+1) != 255 )
				Wrong++;
		LastProbeRed = LastProbeGreen = 0;
		for( INT i=0; i<W*H; i++ )
		{
			LastProbeRed	+= Tally.Data(i*4) == 255;
			LastProbeGreen	+= Tally.Data(i*4+1) == 255;
		}
		RI->RestoreAfterDeviceDraw();
		return Wrong;
	}
	// No gl_FragDepth: CopyDepthBuffer copies nothing and the passes drawn into the target
	// test the scene's depth in the shader (PSV_SceneDepthTest). The same two bounds as
	// ProbeDepth, through that shader test and with GL's depth test off: a triangle at
	// Expected+Bound must be dropped (GL_LEQUAL against the scene), one at Expected-Bound kept.
	// Returns the pixels where either bound fails.
	INT ProbeSceneDepthTest( GLuint Framebuffer, INT W, INT H, const TArray<DWORD>& Expected, INT Bound )
	{
		if( !BuildDepthTestProgram() || RI->SceneDepthTestFramebuffer != Framebuffer )
			return W * H;
		RenDev->glBindFramebuffer( GL_FRAMEBUFFER, Framebuffer );
		RenDev->glViewport( 0, 0, W, H );
		RenDev->glDisable( GL_SCISSOR_TEST );
		RenDev->glDisable( GL_STENCIL_TEST );
		RenDev->glDisable( GL_BLEND );
		RenDev->glDisable( GL_ALPHA_TEST );
		RenDev->glDisable( GL_CULL_FACE );
		RenDev->glDisable( GL_DEPTH_TEST );
		RenDev->glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
		RenDev->glClearColor( 0.f, 0.f, 0.f, 0.f );
		RenDev->glClear( GL_COLOR_BUFFER_BIT );
		RenDev->glActiveTexture( GL_TEXTURE0 + 7 );
		RenDev->glBindTexture( GL_TEXTURE_2D, RenDev->MainDepthStencil );
		RenDev->glActiveTexture( GL_TEXTURE0 );
		RenDev->glUseProgram( DepthTestProgram );
		RenDev->glUniform4fv( DepthTestRectLoc, 1, RI->SceneDepthTestRect );
		ProbeWithSceneDepthTest = 1;
		static const FLOAT Red[4] = { 1.f, 0.f, 0.f, 0.f }, Green[4] = { 0.f, 1.f, 0.f, 0.f };
		RenDev->glColorMask( GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE );
		DrawDepthPoints( W, H, Expected, Bound, Red, 0 );
		RenDev->glColorMask( GL_FALSE, GL_TRUE, GL_FALSE, GL_FALSE );
		DrawDepthPoints( W, H, Expected, -Bound, Green, 0 );
		ProbeWithSceneDepthTest = 0;
		RenDev->glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
		RenDev->glEnable( GL_DEPTH_TEST );
		RenDev->glEnable( GL_SCISSOR_TEST );
		FTestImage Tally( W, H );
		RenDev->glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		RenDev->glReadPixels( 0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, &Tally.Data(0) );
		INT Wrong = 0;
		LastProbeRed = LastProbeGreen = 0;
		for( INT i=0; i<W*H; i++ )
		{
			// Where Expected + Bound is beyond the largest depth the triangle is clamped to it,
			// equal to the scene's depth, and GL_LEQUAL keeps it: no upper bound to test there.
			UBOOL Upper = Tally.Data(i*4) == 0 || Expected(i) + Bound > 0xffffff, Lower = Tally.Data(i*4+1) == 255;
			LastProbeRed	+= Upper;
			LastProbeGreen	+= Lower;
			if( !Upper || !Lower )
				Wrong++;
		}
		RI->RestoreAfterDeviceDraw();
		return Wrong;
	}

	/*-------------------------------------------------------------------------
		Fixed-function distance fog (gl4es emulates it in a shader).
	-------------------------------------------------------------------------*/

	// A black, untextured quad at camera-space depth Z (D3D camera space: z forward) through
	// the fixed-function path with the device's fog set-up; returns the red byte it comes out as.
	INT FogProbe( FLOAT Start, FLOAT End, FLOAT Z, FLOAT Tilt, FLOAT Scale=1.f, FLOAT OffAxis=0.f )
	{
		static const FLOAT White[4] = { 1.f, 1.f, 1.f, 1.f };
		// Camera-space positions: a quad around the view axis, its top edge Tilt times farther than its bottom edge.
		FLOAT V[16] = { -Z, -Z, Z, 1.f,   Z, -Z, Z, 1.f,   -Z*Tilt, Z*Tilt, Z*Tilt, 1.f,   Z*Tilt, Z*Tilt, Z*Tilt, 1.f };
		// A model-view matrix with a scale (the vertices are divided by it) and a sideways shift
		// of the quad (OffAxis times its depth: the centre pixel then looks at it off the view axis).
		for( INT i=0; i<4; i++ )
		{
			V[i*4] += OffAxis * V[i*4+2];
			for( INT k=0; k<3; k++ )
				V[i*4+k] /= Scale;
		}
		FLOAT M[16] = { Scale,0,0,0,  0,Scale,0,0,  0,0,Scale,0,  0,0,0,1 };
		M[12] = -OffAxis * Z;
		FLOAT P[16] = { 1,0,0,0,  0,1,0,0,  0,0,0,1,  0,0,0,0 };	// x/z, y/z; depth 0
		GLuint Buffer = RenDev->NewBuffer( GL_ARRAY_BUFFER );
		RenDev->glBindBuffer( GL_ARRAY_BUFFER, Buffer );
		RenDev->glBufferData( GL_ARRAY_BUFFER, sizeof(V), V, GL_STATIC_DRAW );
		RenDev->glBindFramebuffer( GL_FRAMEBUFFER, RenDev->MainFramebuffer );
		RenDev->glViewport( 0, 0, 64, 64 );
		RenDev->glDisable( GL_SCISSOR_TEST );
		RenDev->glDisable( GL_STENCIL_TEST );
		RenDev->glDisable( GL_DEPTH_TEST );
		RenDev->glDisable( GL_BLEND );
		RenDev->glDisable( GL_ALPHA_TEST );
		RenDev->glDisable( GL_CULL_FACE );
		RenDev->glDisable( GL_LIGHTING );
		RenDev->glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
		for( INT s=0; s<RenDev->NumTextureUnits; s++ )
		{
			RenDev->glActiveTexture( GL_TEXTURE0 + s );
			RenDev->glClientActiveTexture( GL_TEXTURE0 + s );
			if( s < RenDev->GLFixedFunctionUnits )
			{
				RenDev->glDisable( GL_TEXTURE_2D );
				RenDev->glDisable( GL_TEXTURE_CUBE_MAP );
			}
			RenDev->glDisableClientState( GL_TEXTURE_COORD_ARRAY );
		}
		RenDev->glActiveTexture( GL_TEXTURE0 );
		RenDev->glClientActiveTexture( GL_TEXTURE0 );
		RenDev->glMatrixMode( GL_PROJECTION );
		RenDev->glLoadMatrixf( P );
		RenDev->glMatrixMode( GL_MODELVIEW );
		RenDev->glLoadMatrixf( M );
		RenDev->glEnable( GL_FOG );
		RenDev->glFogf( GL_FOG_START, Start );
		RenDev->glFogf( GL_FOG_END, End );
		RenDev->glFogfv( GL_FOG_COLOR, White );
		RenDev->glDisableClientState( GL_NORMAL_ARRAY );
		RenDev->glDisableClientState( GL_COLOR_ARRAY );
		RenDev->glDisableClientState( GL_SECONDARY_COLOR_ARRAY );
		RenDev->glDisable( GL_COLOR_SUM );
		RenDev->glColor4f( 0.f, 0.f, 0.f, 1.f );
		RenDev->glVertexPointer( 4, GL_FLOAT, 0, NULL );
		RenDev->glEnableClientState( GL_VERTEX_ARRAY );
		RenDev->glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
		RenDev->glDisable( GL_FOG );
		RenDev->glBindBuffer( GL_ARRAY_BUFFER, 0 );
		RenDev->FreeBuffer( GL_ARRAY_BUFFER, Buffer );
		BYTE Pixel[4] = { 0, 0, 0, 0 };
		RenDev->glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		RenDev->glReadPixels( 32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, Pixel );
		RenDev->glEnable( GL_SCISSOR_TEST );
		RI->LightsDirty = RI->MatricesDirty = 1;
		RI->RestoreAfterDeviceDraw();
		return Pixel[0];
	}
	void TestFog()
	{
		// The fog factor is (End - z) / (End - Start) on the camera-space depth z; the quad is
		// black and the fog white, so the red byte is 255 * (1 - factor).
		static const FLOAT Cases[][6] = { { 0.f, 1000.f, 250.f, 1.f, 1.f, 0.f }, { 0.f, 1000.f, 750.f, 1.f, 1.f, 0.f }, { 230.f, 1600.f, 900.f, 1.f, 1.f, 0.f }, { 0.f, 120000.f, 60000.f, 1.f, 1.f, 0.f }, { 0.f, 120000.f, 30000.f, 3.f, 1.f, 0.f }, { 230.f, 1600.f, 5000.f, 1.f, 1.f, 0.f },
			{ 0.f, 120000.f, 60000.f, 1.f, 1.65f, 0.f }, { 0.f, 1000.f, 500.f, 1.f, 0.25f, 0.f }, { 0.f, 120000.f, 90000.f, 1.f, 1.f, 0.f } };
		FLOAT	MaxError = 0.f;
		FString	Detail;
		for( INT c=0; c<ARRAY_COUNT(Cases); c++ )
		{
			// At the centre of a tilted quad z is the perspective interpolation of its edges: 2ab/(a+b).
			FLOAT	Near		= Cases[c][2], Far = Cases[c][2] * Cases[c][3],
					Z			= 2.f * Near * Far / (Near + Far),
					Expected	= 255.f * Clamp( 1.f - (Cases[c][1] - Z) / (Cases[c][1] - Cases[c][0]), 0.f, 1.f );
			INT		Got			= FogProbe( Cases[c][0], Cases[c][1], Cases[c][2], Cases[c][3], Cases[c][4], Cases[c][5] );
			MaxError = Max( MaxError, Abs( Got - Expected ) );
			Detail += FString::Printf( TEXT(" [%g..%g at z %g, model scale %g: %i, expected %.1f]"), Cases[c][0], Cases[c][1], Z, Cases[c][4], Got, Expected );
		}
		Report( TEXT("fog"), ARRAY_COUNT(Cases), MaxError, 2.f, *FString::Printf( TEXT("fixed-function linear fog on camera-space depth, white fog over a black quad:%s"), *Detail ) );
	}

	/*-------------------------------------------------------------------------
		32-bit indices on a context without them (OES_element_index_uint
		missing, or MaskCaps=uintindex). Only run then.
	-------------------------------------------------------------------------*/

	// A 32-bit index buffer through the engine's path (SetDynamicIndexBuffer), then drawn by
	// GL from what the driver stored: two triangles over a 16x16 viewport, every pixel must be
	// covered. Also checks what was stored: 16-bit indices equal to the 32-bit ones.
	void TestIndexNarrow()
	{
		if( RenDev->CapUIntIndex || !BuildPointProgram() )
			return;
		FRaw32BitIndexBuffer Indices;
		static const DWORD Quad[6] = { 0, 1, 2, 2, 1, 3 };
		for( INT i=0; i<6; i++ )
			Indices.Indices.AddItem( Quad[i] + 40000 );		// above 32767: a sign or width mistake shows
		INT First = RI->SetDynamicIndexBuffer( &Indices, 0 );
		FOpenGLDynamicIndexBuffer* Stored = RenDev->DynamicIndexBuffer32;
		INT StoredWrong = 0;
		if( !Stored || Stored->IndexSize != sizeof(_WORD) )
			StoredWrong = 6;
		else if( RenDev->IsGLES && Stored->Shadow.Num() == 6 * sizeof(_WORD) )
			for( INT i=0; i<6; i++ )
				StoredWrong += ((_WORD*)&Stored->Shadow(0))[i] != Quad[i] + 40000;
		// 40004 vertices, the four used at 40000..40003: a full-viewport quad.
		TArray<FLOAT> Vertices;
		Vertices.AddZeroed( 40004 * 3 );
		static const FLOAT Corner[4][2] = { { -1.f, -1.f }, { 1.f, -1.f }, { -1.f, 1.f }, { 1.f, 1.f } };
		for( INT c=0; c<4; c++ )
		{
			Vertices( (40000 + c) * 3 + 0 ) = Corner[c][0];
			Vertices( (40000 + c) * 3 + 1 ) = Corner[c][1];
		}
		GLuint Buffer = RenDev->NewBuffer( GL_ARRAY_BUFFER );
		RenDev->glBindBuffer( GL_ARRAY_BUFFER, Buffer );
		RenDev->glBufferData( GL_ARRAY_BUFFER, Vertices.Num() * sizeof(FLOAT), &Vertices(0), GL_STATIC_DRAW );
		RenDev->glBindFramebuffer( GL_FRAMEBUFFER, RenDev->MainFramebuffer );
		RenDev->glViewport( 0, 0, 16, 16 );
		RenDev->glDisable( GL_SCISSOR_TEST );
		RenDev->glDisable( GL_STENCIL_TEST );
		RenDev->glDisable( GL_DEPTH_TEST );
		RenDev->glDisable( GL_BLEND );
		RenDev->glDisable( GL_CULL_FACE );
		RenDev->glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
		RenDev->glClearColor( 0.f, 0.f, 0.f, 0.f );
		RenDev->glClear( GL_COLOR_BUFFER_BIT );
		static const FLOAT White[4] = { 1.f, 1.f, 1.f, 1.f };
		RenDev->glUseProgram( PointProgram );
		RenDev->glUniform4fv( PointColorLoc, 1, White );
		RenDev->glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, 0, NULL );
		RenDev->glEnableVertexAttribArray( 0 );
		if( Stored )
		{
			RenDev->glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, Stored->BufferID );
			RenDev->glDrawElements( GL_TRIANGLES, 6, Stored->IndexSize == 4 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, (const BYTE*)(PTRINT)(First * Stored->IndexSize) );
			RenDev->glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
		}
		RenDev->glDisableVertexAttribArray( 0 );
		RenDev->glUseProgram( 0 );
		RenDev->glBindBuffer( GL_ARRAY_BUFFER, 0 );
		RenDev->FreeBuffer( GL_ARRAY_BUFFER, Buffer );
		FTestImage Got( 16, 16 );
		RenDev->glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		RenDev->glReadPixels( 0, 0, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, &Got.Data(0) );
		RenDev->glEnable( GL_SCISSOR_TEST );
		RI->RestoreAfterDeviceDraw();
		INT Uncovered = 0;
		for( INT i=0; i<16*16; i++ )
			Uncovered += Got.Data(i*4) != 255;
		Report( TEXT("index-narrow"), 16*16 + 6, Uncovered + StoredWrong, 0.f, *FString::Printf( TEXT("FALLBACK (no 32-bit indices): six 32-bit indices 40000..40003 through SetDynamicIndexBuffer are stored as %i-bit (%i stored values wrong) and draw a 16x16 quad (%i pixels not covered)"), Stored ? Stored->IndexSize * 8 : 0, StoredWrong, Uncovered ) );
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
		if( !RenDev->CapFragDepth )
		{
			// No gl_FragDepth: nothing is copied; what stands for the copy is the depth test
			// in the shaders of the passes drawn into the target. Checked with the same expected
			// depths and bounds as the copy.
			TArray<DWORD> Expected;
			Expected.Add( HW * HH );
			for( INT y=0; y<HH; y++ )
				for( INT x=0; x<HW; x++ )
					Expected(y*HW+x) = Z( (2*y+1)*W + 2*x+1 );
			INT Bound = 0, WrongAtBound = 0, WrongAt1 = 0;
			FString PerBound;
			static const INT Bounds[] = { 1, 2, 3, 4, 8, 16, 256, 65536 };
			for( INT b=0; b<ARRAY_COUNT(Bounds); b++ )
			{
				Bound			= Bounds[b];
				WrongAtBound	= ProbeSceneDepthTest( RI->CacheTexture( Half )->FramebufferID, HW, HH, Expected, Bound );
				PerBound		+= FString::Printf( TEXT(" [%i: %i wrong, upper held %i, lower held %i]"), Bound, WrongAtBound, LastProbeRed, LastProbeGreen );
				if( b == 0 )
					WrongAt1 = WrongAtBound;
				if( WrongAtBound == 0 )
					break;
			}
			debugf( NAME_Init, TEXT("UC2GLDrv: selftest depth-copy (fallback) per bound:%s"), *PerBound );
			// Tolerance 3 units of the 24-bit depth (the copy's is 1): the depth reaches the shader
			// as a 32-bit float from the texture and is compared with an interpolated 32-bit
			// float, where GL's own test compares two integers (1 or 2 units are too tight).
			Report( TEXT("depth-copy"), HW*HH, WrongAtBound ? 16777215.f : (FLOAT)Bound, 3.f, *FString::Printf( TEXT("FALLBACK (no gl_FragDepth): the scene depth test in the shader of a pass drawn into a %ix%i target after CopyDepthBuffer: target pixel (x,y) is tested against the depth of back-buffer pixel (2x+1,2y+1); probed with two triangles at the expected depth +/- N units through that test: every pixel within %i units (%i pixels not within 1); last probe: upper bound held for %i pixels, lower bound for %i"), HW, HH, Bound, WrongAt1, LastProbeRed, LastProbeGreen ) );
			delete Half;
			return;
		}
		if( RenDev->IsGLES )
		{
			// OpenGL ES cannot read a depth buffer back. The copy is probed instead: see ProbeDepth.
			TArray<DWORD> Expected;
			Expected.Add( HW * HH );
			for( INT y=0; y<HH; y++ )
				for( INT x=0; x<HW; x++ )
					Expected(y*HW+x) = Z( (2*y+1)*W + 2*x+1 );
			INT Bound = 0, WrongAtBound = 0, WrongAt1 = 0;
			static const INT Bounds[] = { 1, 2, 3, 4, 8, 16, 256, 65536 };
			for( INT b=0; b<ARRAY_COUNT(Bounds); b++ )
			{
				Bound			= Bounds[b];
				WrongAtBound	= ProbeDepth( RI->CacheTexture( Half )->FramebufferID, HW, HH, Expected, Bound, 0 );
				if( b == 0 )
					WrongAt1 = WrongAtBound;
				if( WrongAtBound == 0 )
					break;
			}
			Report( TEXT("depth-copy"), HW*HH, WrongAtBound ? 16777215.f : (FLOAT)Bound, 1.f, *FString::Printf( TEXT("CopyDepthBuffer into %ix%i: target pixel (x,y) holds the depth of back-buffer pixel (2x+1,2y+1); OpenGL ES cannot read depth back, so each pixel is probed with two depth-tested triangles at the expected depth +/- N units: every pixel is within %i units (%i pixels are not within 1); the same probe on desktop GL agrees with the read-back there; last probe: upper bound held for %i pixels, lower bound for %i"), HW, HH, Bound, WrongAt1, LastProbeRed, LastProbeGreen ) );
			delete Half;
			return;
		}
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
		// The point probe that stands in for the read-back on OpenGL ES, run here too, where the truth is known.
		TArray<DWORD> Expected;
		Expected.Add( HW * HH );
		for( INT y=0; y<HH; y++ )
			for( INT x=0; x<HW; x++ )
				Expected(y*HW+x) = Z( (2*y+1)*W + 2*x+1 );
		INT ProbeBound = 0, ProbeWrong = 0;
		static const INT Bounds[] = { 1, 2, 3, 4, 8, 16, 256, 65536 };
		for( INT b=0; b<ARRAY_COUNT(Bounds); b++ )
		{
			ProbeBound = Bounds[b];
			ProbeWrong = ProbeDepth( RI->CacheTexture( Half )->FramebufferID, HW, HH, Expected, ProbeBound, 0 );
			if( !ProbeWrong )
				break;
		}
		Report( TEXT("depth-copy"), HW*HH, MaxZError, 1.f, *FString::Printf( TEXT("CopyDepthBuffer into %ix%i: target pixel (x,y) holds the depth of back-buffer pixel (2x+1,2y+1); error in units of the 24-bit depth; %i pixels wrong (of those, matching texel (2x,2y) %i, (2x+1,2y) %i, (2x,2y+1) %i); the triangle probe used on OpenGL ES says: %s within %i units (last probe: upper bound held for %i pixels, lower bound for %i)"), HW, HH, Wrong, Others[0], Others[1], Others[2], ProbeWrong ? TEXT("NOT every pixel") : TEXT("every pixel"), ProbeBound, LastProbeRed, LastProbeGreen ) );
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

		// PostProcessTargetScale 2: the same pass with TapScale 2: the
		// taps at twice the offsets, each the mean of two bilinear reads 1 texel either side along each axis the
		// filter reaches (PS_Blur's fill variant). The mean signed error shows a pass that loses or gains (a
		// rounding bias: an 8-bit fill pass whose exact halves round down shows -0.1 to -0.2 here).
		FilterCopyRenderTarget( Dest, Source, NULL, Sampler, RI, NULL, NULL, 0, 2 );
		ReadTarget( Dest, Got );
		UBOOL Reach[2] = { 0, 0 };
		for( INT t=0; t<4; t++ )
		{
			Reach[0] |= F.SamplePoints[t].OffsetU != 0.f;
			Reach[1] |= F.SamplePoints[t].OffsetV != 0.f;
		}
		FLOAT MaxError2 = 0.f, MeanError2 = 0.f;
		for( INT y=0; y<DH; y++ )
			for( INT x=0; x<DW; x++ )
				for( INT c=0; c<3; c++ )
				{
					FLOAT Sum = 0.f;
					for( INT t=0; t<4; t++ )
						for( INT fy=-1; fy<=1; fy+=2 )
							for( INT fx=-1; fx<=1; fx+=2 )
							{
								if( (fx > 0 && !Reach[0]) || (fy > 0 && !Reach[1]) )
									continue;
								FLOAT Share = (Reach[0] ? 0.5f : 1.f) * (Reach[1] ? 0.5f : 1.f);
								Sum += Share * F.SamplePoints[t].Weight * Pattern.Bilinear(
									(x + 0.5f) / DW + (2.f * F.SamplePoints[t].OffsetU + (Reach[0] ? fx : 0)) / SW,
									(y + 0.5f) / DH + (2.f * F.SamplePoints[t].OffsetV + (Reach[1] ? fy : 0)) / SH, c );
							}
					MaxError2 = Max( MaxError2, Abs( Sum - (FLOAT)Got.At(x,y,c) ) );
					MeanError2 += (Got.At(x,y,c) - Sum) / (DW * DH * 3);
				}
		// One line per kernel: the worse of the two errors against the tolerance, and the mean of the scaled
		// pass must stay within 0.1 of a step.
		FLOAT Worst = Max( MaxError, MaxError2 );
		if( Abs( MeanError2 ) > 0.1f )
			Worst = Max( Worst, 255.f );
		Report( Name, DW*DH*3*2, Worst, 2.f, *FString::Printf( TEXT("%ix%i -> %ix%i, taps (%g,%g) (%g,%g) (%g,%g) (%g,%g) texels, weights %.3f %.3f %.3f %.3f, bilinear, D3D sample positions: largest error %.2f; with TapScale 2 (taps x2, each the mean of 2 reads per axis): largest error %.2f, mean %+.3f"),
			SW, SH, DW, DH, F.SamplePoints[0].OffsetU, F.SamplePoints[0].OffsetV, F.SamplePoints[1].OffsetU, F.SamplePoints[1].OffsetV,
			F.SamplePoints[2].OffsetU, F.SamplePoints[2].OffsetV, F.SamplePoints[3].OffsetU, F.SamplePoints[3].OffsetV,
			F.SamplePoints[0].Weight, F.SamplePoints[1].Weight, F.SamplePoints[2].Weight, F.SamplePoints[3].Weight, MaxError, MaxError2, MeanError2 ) );
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
		The reference picture.
	-------------------------------------------------------------------------*/

	// pp-sizes: the blur targets the camera effects hold in the render-target pool are the
	// fractions of the reference picture Epic's formulas give (UCCameraEffects.cpp, the shipped
	// script's scales: depth of field 0.5, bloom and emissive 0.25; each level halves one side),
	// and the back-buffer copy is half the back buffer. Run first, before the other tests touch
	// the pool, with the reference height the frames before were drawn with.
	void TestTargetSizes()
	{
		FPostProcessReference Ref = GetPostProcessReference( RenDev->MainWidth, RenDev->MainHeight );
		// PostProcessTargetScale multiplies every blur target's sides before Epic's truncation (the
		// back-buffer copy is not a blur target and keeps its size).
		INT S = RenDev->GetRenderCaps()->PostProcessTargetScale;
		struct FSize { INT W, H, Flags; };
		TArray<FSize> Expected;
		#define UCPP_EXPECT(w,h,f) { FSize Sz; Sz.W = Max((INT)(w),1); Sz.H = Max((INT)(h),1); Sz.Flags = (f); Expected.AddItem( Sz ); }
		const INT C = FRenderTarget::RT_CREATE_COLOR, CD = FRenderTarget::RT_CREATE_COLOR | FRenderTarget::RT_CREATE_DEPTH;
		// UUCScreenDOF: (W/2, H/4), (W/4, H/4).
		UCPP_EXPECT( Ref.Width * 0.5f * S, Ref.Height * 0.25f * S, C );
		UCPP_EXPECT( Ref.Width * 0.25f * S, Ref.Height * 0.25f * S, C );
		// UUCScreenBloom: select (W/4, H/4) with depth, (W/8, H/4), (W/8, H/8).
		UCPP_EXPECT( Ref.Width * 0.25f * S, Ref.Height * 0.25f * S, CD );
		UCPP_EXPECT( Ref.Width * 0.125f * S, Ref.Height * 0.25f * S, C );
		UCPP_EXPECT( Ref.Width * 0.125f * S, Ref.Height * 0.125f * S, C );
		// UUCScreenEmissive (its select target is not pooled): (W/4, H/4), (W/4, H/8), (W/8, H/8).
		INT EW = Max( (INT)(Ref.Width * 0.25f * S), 1 ), EH = Max( (INT)(Ref.Height * 0.25f * S), 1 );
		UCPP_EXPECT( EW, EH, C );
		UCPP_EXPECT( EW, EH * 0.5f, C );
		UCPP_EXPECT( EW * 0.5f, EH * 0.5f, C );
		// The back-buffer copy (and distortion accumulation target): half the back buffer.
		UCPP_EXPECT( RenDev->MainWidth * 0.5f, RenDev->MainHeight * 0.5f, CD );
		#undef UCPP_EXPECT

		FRenderTargetPool* Pool = RI->GetRenderTargetPool();
		FString Live, Unexpected, Missing;
		INT Compared = 0, Wrong = 0;
		for( INT i=0; i<Pool->RenderTargets.Num(); i++ )
		{
			FRenderTarget* T = Pool->RenderTargets(i);
			if( !T || Pool->RefCounts(i) <= 0 )
				continue;
			Compared++;
			Live += FString::Printf( TEXT(" %ix%i%s"), T->GetWidth(), T->GetHeight(), (T->CreationFlags & FRenderTarget::RT_CREATE_DEPTH) ? TEXT("+depth") : TEXT("") );
			UBOOL Found = 0;
			for( INT e=0; e<Expected.Num(); e++ )
				Found |= Expected(e).W == T->GetWidth() && Expected(e).H == T->GetHeight() && Expected(e).Flags == T->CreationFlags;
			if( !Found )
			{
				Wrong++;
				Unexpected += FString::Printf( TEXT(" %ix%i"), T->GetWidth(), T->GetHeight() );
			}
		}
		for( INT e=0; e<Expected.Num(); e++ )
		{
			UBOOL Found = 0;
			for( INT i=0; i<Pool->RenderTargets.Num(); i++ )
				if( Pool->RenderTargets(i) && Pool->RefCounts(i) > 0 )
					Found |= Expected(e).W == Pool->RenderTargets(i)->GetWidth() && Expected(e).H == Pool->RenderTargets(i)->GetHeight() && Expected(e).Flags == Pool->RenderTargets(i)->CreationFlags;
			if( !Found )
			{
				Wrong++;
				Missing += FString::Printf( TEXT(" %ix%i"), Expected(e).W, Expected(e).H );
			}
		}
		Report( TEXT("pp-sizes"), Compared, Wrong ? 255.f : 0.f, 0.f, *FString::Printf( TEXT("back buffer %ix%i, reference height %i, target scale %i: reference picture %.1fx%.1f (%.3f back-buffer pixels each); live pooled targets:%s; not expected:%s; expected but missing:%s"),
			RenDev->MainWidth, RenDev->MainHeight, RenDev->PostProcessReferenceHeight, S, Ref.Width, Ref.Height, Ref.PixelScale, *Live, Unexpected.Len() ? *Unexpected : TEXT(" none"), Missing.Len() ? *Missing : TEXT(" none") ) );
	}

	// The reference copy as the device computes it, on the CPU: N x N bilinear samples evenly over
	// each reference pixel (GPostReferenceFragmentShader, UC2GL_UpdateReferenceCopy).
	void ReferenceOf( FTestImage& Source, FTestImage& Out )
	{
		INT N = Clamp( appCeil( (FLOAT)Source.H / Out.H - 0.01f ), 1, 8 );
		FLOAT SU = 1.f / (Out.W * N), SV = 1.f / (Out.H * N);
		for( INT y=0; y<Out.H; y++ )
			for( INT x=0; x<Out.W; x++ )
				for( INT c=0; c<4; c++ )
				{
					FLOAT Sum = 0.f;
					for( INT j=0; j<N; j++ )
						for( INT i=0; i<N; i++ )
							Sum += Source.Bilinear( (x + 0.5f) / Out.W + (i - (N - 1) * 0.5f) * SU, (y + 0.5f) / Out.H + (j - (N - 1) * 0.5f) * SV, c );
					Out.At(x,y,c) = (BYTE)Clamp( appRound( Sum / (N * N) ), 0, 255 );
				}
	}

	// ref-copy, ref-taps, bb-copy-ref: with a 480-line reference (whatever the ini says), a random
	// back buffer. ref-copy: the copy against the CPU. ref-taps: FilterCopyRenderTarget from the
	// back buffer into the first target of the depth of field (Epic's vertical 1331 kernel), whose
	// taps must sit +-1 and +-3 REFERENCE pixels apart and read the reference copy; expected values
	// from the copy as read back. bb-copy-ref: UpdateBackBufferCopyTarget, taps (+-1,+-1) reference
	// pixels into the half-size target, read from the copy.
	void TestReference()
	{
		const INT RefH = 480;
		INT W = RenDev->MainWidth, H = RenDev->MainHeight;
		if( H <= RefH || !RenDev->PostReferenceProgram )
		{
			debugf( NAME_Init, TEXT("UC2GLDrv: selftest ref-copy, ref-taps, bb-copy-ref: not run (back buffer %ix%i: %s)"), W, H,
				H <= RefH ? TEXT("480 lines or fewer, no reference copy is made") : TEXT("the reference copy program did not build") );
			return;
		}
		RenDev->PostProcessReferenceHeight = RefH;
		RenDev->SampleReference = 0;
		FPostProcessReference Ref = GetPostProcessReference( W, H );

		FTestImage Pattern( W, H );
		Pattern.Randomize( 7, 3 );
		SetMainColor( Pattern );

		// ref-copy
		RenDev->DefaultRenderTarget->SetMaterialUsage( FRenderTarget::RT_MATERIAL_USE_BACKBUFFER );
		RenDev->SampleReference = 1;
		RenDev->UpdateSampledTexture( RI->CacheTexture( RenDev->DefaultRenderTarget ) );
		RenDev->SampleReference = 0;
		RI->InvalidateHardwareState();
		INT RW = RenDev->ReferenceWidth, RH = RenDev->ReferenceHeight;
		FTestImage Copy( RW, RH ), Expected( RW, RH );
		ReadFramebuffer( RenDev->ReferenceFramebuffer, Copy );
		ReferenceOf( Pattern, Expected );
		FLOAT MaxError = 0.f;
		for( INT y=0; y<RH; y++ )
			for( INT x=0; x<RW; x++ )
				for( INT c=0; c<3; c++ )
					MaxError = Max( MaxError, Abs( (FLOAT)Copy.At(x,y,c) - Expected.At(x,y,c) ) );
		INT N = Clamp( appCeil( (FLOAT)H / RH - 0.01f ), 1, 8 );
		Report( TEXT("ref-copy"), RW*RH*3, MaxError, 2.f, *FString::Printf( TEXT("back buffer %ix%i -> reference copy %ix%i, %ix%i bilinear samples per pixel"), W, H, RW, RH, N, N ) );

		// ref-taps
		if( RenDev->PixelShaderFamilyReady( PS_Blur ) )
		{
			INT DW = Max( (INT)(Ref.Width * 0.5f), 1 ), DH = Max( (INT)(Ref.Height * 0.25f), 1 );
			FAuxRenderTarget* Dest = new FAuxRenderTarget( DW, DH, TEXF_RGBA8, FRenderTarget::RT_CREATE_COLOR );
			FMultiSampleFilter Sampler( FMultiSampleFilter::MULTI_SAMPLE_VERT_1331 );
			INT CopiesBefore = RenDev->ReferenceCopies, SnapshotsBefore = RenDev->SnapshotCopies;
			FilterCopyRenderTarget( Dest, RenDev->DefaultRenderTarget, Viewport, Sampler, RI );
			FTestImage Got( DW, DH );
			ReadTarget( Dest, Got );
			// Taps in reference pixels: the offsets of the filter times PixelScale back-buffer pixels.
			FFourSampleFilter F = Sampler.FilterList(0);
			for( INT t=0; t<4; t++ )
			{
				F.SamplePoints[t].OffsetU *= Ref.PixelScale * RW / W;
				F.SamplePoints[t].OffsetV *= Ref.PixelScale * RH / H;
			}
			FLOAT TapError = CompareFilter( Copy, Got, 0.f, 0.f, 1.f, 1.f, F );
			Report( TEXT("ref-taps"), DW*DH*3, TapError, 2.f, *FString::Printf( TEXT("FilterCopyRenderTarget(back buffer, viewport, vertical 1331) -> %ix%i: taps (0,+-1) (0,+-3) reference pixels = %.3f back-buffer pixels each, read from the reference copy (%i reference copies, %i snapshots during the pass)"),
				DW, DH, Ref.PixelScale, RenDev->ReferenceCopies - CopiesBefore, RenDev->SnapshotCopies - SnapshotsBefore ) );
			delete Dest;
		}
		else
			Report( TEXT("ref-taps"), 0, 255.f, 0.f, TEXT("PS_Blur is not available") );

		// bb-copy-ref
		RI->UpdateBackBufferCopyTarget( Viewport );
		FRenderTarget* BBCopy = RI->BackBufferCopyTarget;
		if( BBCopy && BBCopy != RenDev->DefaultRenderTarget )
		{
			FTestImage Got( BBCopy->GetWidth(), BBCopy->GetHeight() );
			ReadTarget( BBCopy, Got );
			FFourSampleFilter F;
			static const FLOAT Off[4][2] = { {-1,-1}, {1,-1}, {-1,1}, {1,1} };
			for( INT t=0; t<4; t++ )
			{
				F.SamplePoints[t].OffsetU	= Off[t][0] * Ref.PixelScale * RW / W;
				F.SamplePoints[t].OffsetV	= Off[t][1] * Ref.PixelScale * RH / H;
				F.SamplePoints[t].Weight	= 0.25f;
			}
			FLOAT BBError = CompareFilter( Copy, Got, 0.f, 0.f, 1.f, 1.f, F );
			Report( TEXT("bb-copy-ref"), Got.W*Got.H*3, BBError, 2.f, *FString::Printf( TEXT("UpdateBackBufferCopyTarget %ix%i -> %ix%i (half, as before), taps (+-1,+-1) reference pixels, read from the reference copy"), W, H, Got.W, Got.H ) );
		}
		else
			Report( TEXT("bb-copy-ref"), 0, 255.f, 0.f, TEXT("no back-buffer copy target") );
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
		static const INT LookupHeight = 256;
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

	/*-------------------------------------------------------------------------
		luminance: the pass the dynamic gamma's histogram is made from.
	-------------------------------------------------------------------------*/

	void TestLuminance()
	{
		if( !RenDev->PixelShaderFamilyReady( PS_LuminanceTexIndex ) )
		{
			Report( TEXT("luminance"), 0, 255.f, 0.f, TEXT("PS_LuminanceTexIndex is not available") );
			return;
		}
		INT W = RenDev->MainWidth, H = RenDev->MainHeight;
		FTestImage Pattern( W, H );
		Pattern.Randomize( 9, 4 );
		SetMainColor( Pattern );

		extern FRenderTarget* UC2GL_TestLuminancePass( FOpenGLRenderInterface* RI, FLOAT& MinL, FLOAT& MaxL, FLOAT& AvgL );
		FLOAT MinL = 0.f, MaxL = 0.f, AvgL = 0.f;
		FRenderTarget* Target = UC2GL_TestLuminancePass( RI, MinL, MaxL, AvgL );
		FTestImage Got( Target->GetWidth(), Target->GetHeight() );
		ReadTarget( Target, Got );
		// One bilinear sample of the back buffer per target pixel (the 16x16 target does not
		// average the frame: Epic's pass samples it sparsely), then dp3 with (0.30, 0.59, 0.11).
		FLOAT MaxError = 0.f, Sum = 0.f;
		for( INT y=0; y<Got.H; y++ )
			for( INT x=0; x<Got.W; x++ )
			{
				FLOAT U = (x + 0.5f) / Got.W, V = (y + 0.5f) / Got.H;
				FLOAT Expected = 0.30f * Pattern.Bilinear(U,V,0) + 0.59f * Pattern.Bilinear(U,V,1) + 0.11f * Pattern.Bilinear(U,V,2);
				MaxError = Max( MaxError, Abs( Expected - (FLOAT)Got.At(x,y,2) ) );	// the histogram reads the blue byte
				Sum += Expected;
			}
		FLOAT ExpectedAvg = Sum / (Got.W * Got.H) / 255.f;
		Report( TEXT("luminance"), Got.W*Got.H, MaxError, 2.f, *FString::Printf( TEXT("back buffer -> %ix%i luminance target (0.30, 0.59, 0.11), one bilinear sample per pixel; histogram of it: min %.3f max %.3f average %.3f (average of the expected values %.3f)"), Got.W, Got.H, MinL, MaxL, AvgL, ExpectedAvg ) );
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
		if( RenDev->MainSamples )
		{
			// The checks write and read the main framebuffer's textures directly and compare
			// exactly: they need the single-sampled main framebuffer.
			debugf( NAME_Init, TEXT("UC2GLDrv: selftest: anti-aliasing is on (multisampled main framebuffer); the self-test needs it off: NumAASamples=0") );
			Failed = 1;
			debugf( NAME_Init, TEXT("UC2GLDrv: selftest summary: %i passed, %i failed"), Passed, Failed );
			return;
		}
		RI->PushState();
		// The targets the frames before this one were drawn with; then the primitives
		// as Epic's code specifies them, in back-buffer pixels (no reference picture: the tests
		// below were written for that and stay unchanged); then the reference picture.
		INT ConfiguredReference = RenDev->PostProcessReferenceHeight;
		TestTargetSizes();
		RenDev->PostProcessReferenceHeight = 0;
		RenDev->SampleReference = 0;
		TestSnapshot();
		TestDepth();
		TestBlur( TEXT("blur-h"),   FMultiSampleFilter::MULTI_SAMPLE_HORIZ_1221, 64, 32, 32, 32, 21 );
		TestBlur( TEXT("blur-v"),   FMultiSampleFilter::MULTI_SAMPLE_VERT_1221,  32, 32, 32, 16, 22 );
		TestBlur( TEXT("blur-box"), FMultiSampleFilter::MULTI_SAMPLE_BOX,        32, 16, 32, 16, 23 );
		TestBackBufferCopy();
		TestDOFRamp();
		TestRemap();
		TestLuminance();
		TestFog();
		TestIndexNarrow();
		TestReference();
		RenDev->PostProcessReferenceHeight = ConfiguredReference;
		RenDev->SampleReference = 0;
		RI->PopState();

		// Leave a defined frame behind.
		RenDev->glBindFramebuffer( GL_FRAMEBUFFER, RenDev->MainFramebuffer );
		RI->Clear( 1, FColor(0,0,0,0), 1, 1.f, 1, 0 );
		debugf( NAME_Init, TEXT("UC2GLDrv: selftest summary: %i passed, %i failed"), Passed, Failed );

		// Complete programs, to be compared between the bindings (UC2GLShaders.cpp).
		extern void UC2GL_RunProgramTest( UUC2GLRenderDevice* RenDev );
		UC2GL_RunProgramTest( RenDev );
		RenDev->glBindFramebuffer( GL_FRAMEBUFFER, RenDev->MainFramebuffer );
		RI->Clear( 1, FColor(0,0,0,0), 1, 1.f, 1, 0 );
	}
};

void UC2GL_RunPostProcessSelfTest( UUC2GLRenderDevice* RenDev, UViewport* Viewport )
{
	FSelfTest Test;
	Test.RenDev		= RenDev;
	Test.RI			= &RenDev->RenderInterface;
	Test.Viewport	= Viewport;
	Test.PointProgram		= 0;
	Test.PointColorLoc		= -1;
	Test.DepthTestProgram	= 0;
	Test.DepthTestColorLoc	= Test.DepthTestRectLoc = -1;
	Test.ProbeWithSceneDepthTest = 0;
	Test.DepthUploadFailed	= 0;
	Test.Run();
	if( Test.PointProgram )
		RenDev->glDeleteProgram( Test.PointProgram );
	if( Test.DepthTestProgram )
		RenDev->glDeleteProgram( Test.DepthTestProgram );
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
