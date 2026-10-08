/*=============================================================================
	UC2GLPostProcess.cpp: render targets as shader inputs and the post-process
	interface (M7, renderer R3).

	The Xbox build of UC2 reads its own back buffer and depth buffer as textures
	(distortion, depth of field, bloom, blurs, colour remap) and keeps a blurred half-size
	copy of the back buffer. Epic implemented none of it on PC: FD3DRenderInterface's
	functions return early outside _XBOX. Here are the bodies for OpenGL. The specification
	is the _XBOX code of Source/D3DDrv/Src/D3DRenderInterface.cpp (CopyDepthBuffer,
	GetBackBufferCopyTarget, UpdateBackBufferCopyTarget, GetBackBufferMaterial,
	FD3DDynamicGamma) and D3DMaterialState.cpp (UpdateDOFRampLookupTexture); functions
	ported from there keep Epic's text and name what was changed with "UC2GLDrv:".

	THE COORDINATE CONVENTIONS, in one place (docs/porting/analysis/postprocess.md has the
	long version):

	1. Every texture a material can sample is stored top row first, as Direct3D stores
	   it: v = 0 is the top of the picture. Render-target textures get that way because
	   the interface draws into them upside down in GL's window space (M5: the "Flipped"
	   targets of FOpenGLRenderInterface). The engine's texture coordinates therefore need
	   no change anywhere.
	2. The one exception is the main framebuffer (the default render target): it is drawn
	   the GL way, bottom row first, so the present pass can show it straight. It is never
	   bound as a texture. When a stage samples the default render target, what is bound is
	   a copy: UUC2GLRenderDevice::GetSampledTexture / UpdateSampledTexture below. The copy
	   pass flips, so the copy is top row first like everything else. The copy is also what
	   makes "sample the buffer you are drawing into" defined: the Xbox reads its back buffer
	   in place, GL may not.
	3. Depth as a texture exists only for the default render target, as on Xbox
	   (FDefaultRenderTarget::DepthBufferTexture, a D3DFMT_LIN_B8G8R8A8 view of the D24S8
	   buffer). The copy pass writes the 24-bit depth into the bytes that view showed:
	   blue = bits 23..16, green = 15..8, red = 7..0. GL's window depth equals D3D's z here
	   (M5's clip fix-up maps z to 2z-w and the depth range is 0..1).
	4. Half-texel offset. Direct3D 8 puts pixel centres on integer coordinates, so a
	   full-target quad drawn at 0..size with texture coordinates 0..1 would read a
	   same-size texture on texel boundaries. Epic's screen tiles do not do that:
	   FScreenPrimUtil's canvas matrix moves every tile half a pixel up and left
	   (UnRenderUtil.h, "- 0.5f"), so destination pixel (x,y) samples at ((x+0.5)/w,
	   (y+0.5)/h): texel centres for a same-size copy, the shared corner of 2x2 texels for
	   a half-size one. M5's clip fix-up makes GL rasterise exactly as D3D does, so the same
	   holds here with no further correction; the self-test (UC2GLSelfTest.cpp: blur-h,
	   blur-v, blur-box, bb-copy) checks it against a CPU computation, and fails by 70 to
	   120 of 255 if the half pixel is left out. A point-filtered read that lands exactly on
	   a texel boundary is a tie; hardware takes the texel to the right and below, GL leaves
	   it to rounding noise. For a pass with a point-filtered render-target stage the
	   interface therefore samples 1/32 pixel further right and down
	   (FOpenGLRenderInterface::GetGLMatrices, PointSampleNudge), and CopyDepthBuffer adds a
	   quarter texel for the same reason.
	5. The distortion vertex shader computes the coordinate of "this pixel in the back
	   buffer" itself (Distortion.vsh); its GLSL port takes it from GL's own window
	   coordinates, so no offset convention is involved (UC2GLShaders.cpp).
=============================================================================*/

#include "UC2GLDrv.h"
#include "UC2GLD3DXCompat.h"

#define UC2GL_LOG_ONCE(...) { static UBOOL Logged = 0; if( !Logged ) { Logged = 1; debugf( NAME_Init, __VA_ARGS__ ); } }

/*-----------------------------------------------------------------------------
	The copy programs.
-----------------------------------------------------------------------------*/

// uRect = ( scale u, scale v, offset u, offset v ) applied to the quad's 0..1 coordinates.
static const char* GPostVertexShader =
	"attribute vec2 aPosition;\n"
	"uniform vec4 uRect;\n"
	"varying vec2 vTexCoord;\n"
	"void main()\n"
	"{\n"
	"	vTexCoord = (aPosition * 0.5 + 0.5) * uRect.xy + uRect.zw;\n"
	"	gl_Position = vec4( aPosition, 0.0, 1.0 );\n"
	"}\n";

static const char* GPostCopyFragmentShader =
	"uniform sampler2D uSource;\n"
	"varying vec2 vTexCoord;\n"
	"void main()\n"
	"{\n"
	"	gl_FragColor = texture2D( uSource, vTexCoord );\n"
	"}\n";

// Depth as the Xbox's D3DFMT_LIN_B8G8R8A8 view of a D24S8 buffer shows it. Alpha was the
// stencil byte there; no shader of the game reads it (DOF.psh uses green and blue only).
// The depth arrives as a 32-bit float: the 24-bit value is reproduced to within two units
// (self-test "depth-color"), and without the min() a depth of exactly 1 (the cleared buffer,
// the sky) rounds up to 2^24, whose high byte does not exist: found by self-test "dof-ramp".
// M11: 2^24-1 is written as a product because gl4es reprints every float literal of a shader
// with six significant digits (16777215.0 became 1.67772e+07 = 16777200; found by self-test
// "depth-color" on the twin). No literal in any shader of the driver may need more than six.
static const char* GPostDepthEncodeFragmentShader =
	"uniform UC2_HIGHP sampler2D uSource;\n"
	"varying vec2 vTexCoord;\n"
	"void main()\n"
	"{\n"
	"	float ZMax = 4096.0 * 4096.0 - 1.0;\n"
	"	float Z = min( floor( texture2D( uSource, vTexCoord ).r * ZMax + 0.5 ), ZMax );\n"
	"	float High = floor( Z / 65536.0 );\n"
	"	float Middle = floor( (Z - High * 65536.0) / 256.0 );\n"
	"	float Low = Z - High * 65536.0 - Middle * 256.0;\n"
	"	gl_FragColor = vec4( Low, Middle, High, 0.0 ) / 255.0;\n"
	"}\n";

static const char* GPostDepthCopyFragmentShader =
	"uniform UC2_HIGHP sampler2D uSource;\n"
	"varying vec2 vTexCoord;\n"
	"void main()\n"
	"{\n"
	"	gl_FragDepth = texture2D( uSource, vTexCoord ).r;\n"
	"	gl_FragColor = vec4( 0.0 );\n"
	"}\n";

// pp-scale: the reference copy (docs/porting/analysis/pp-scale.md). One pixel of it is the average
// of the k x k back-buffer pixels under it (k = back-buffer lines per reference line), read as
// N x N bilinear samples spread evenly over them: uStep = the distance between two samples in source
// texture coordinates (a reference pixel divided by N). N = ceil(k), at most 8, is a constant of the
// program (UC2_TAPS, put in front of this text; the program is built again when N changes): a loop
// with a uniform bound and a break ran about 4 ms slower per frame at 3840x2160 on the owner's
// NVIDIA card than the snapshot it replaces (the 25 reads of 5x5 could not be issued together).
static const char* GPostReferenceFragmentShader =
	"uniform sampler2D uSource;\n"
	"uniform vec4 uStep;\n"
	"varying vec2 vTexCoord;\n"
	"void main()\n"
	"{\n"
	"	vec2 Origin = vTexCoord - uStep.xy * (float(UC2_TAPS - 1) * 0.5);\n"
	"	vec4 Sum = vec4( 0.0 );\n"
	"	for( int j = 0; j < UC2_TAPS; j++ )\n"
	"		for( int i = 0; i < UC2_TAPS; i++ )\n"
	"			Sum += texture2D( uSource, Origin + uStep.xy * vec2( float(i), float(j) ) );\n"
	"	gl_FragColor = Sum * (1.0 / float(UC2_TAPS * UC2_TAPS));\n"
	"}\n";

static GLuint UC2GL_BuildPostProgram( UUC2GLRenderDevice* RenDev, const char* FragmentBody, const TCHAR* What, GLint& RectLoc );

// The reference copy's program for N x N samples (0 if it does not build).
static GLuint UC2GL_BuildReferenceProgram( UUC2GLRenderDevice* RenDev, INT N )
{
	if( RenDev->PostReferenceProgram && RenDev->PostReferenceTaps == N )
		return RenDev->PostReferenceProgram;
	if( RenDev->PostReferenceProgram )
		RenDev->glDeleteProgram( RenDev->PostReferenceProgram );
	ANSICHAR Body[2048];
	appSprintf( Body, "#define UC2_TAPS %i\n%s", N, GPostReferenceFragmentShader );
	RenDev->PostReferenceProgram = UC2GL_BuildPostProgram( RenDev, Body, TEXT("reference copy"), RenDev->PostReferenceRectLoc );
	RenDev->PostReferenceTaps = RenDev->PostReferenceProgram ? N : 0;
	if( RenDev->PostReferenceProgram )
		RenDev->PostReferenceStepLoc = RenDev->glGetUniformLocation( RenDev->PostReferenceProgram, "uStep" );
	return RenDev->PostReferenceProgram;
}

static GLuint UC2GL_BuildPostProgram( UUC2GLRenderDevice* RenDev, const char* FragmentBody, const TCHAR* What, GLint& RectLoc )
{
	GLuint Shaders[2] = { 0, 0 };
	for( INT i=0; i<2; i++ )
	{
		const char* Sources[2] = { RenDev->ShaderPrologue( i == 1 ), i == 0 ? GPostVertexShader : FragmentBody };
		Shaders[i] = RenDev->glCreateShader( i == 0 ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER );
		RenDev->glShaderSource( Shaders[i], 2, Sources, NULL );
		RenDev->glCompileShader( Shaders[i] );
		GLint Status = 0;
		RenDev->glGetShaderiv( Shaders[i], GL_COMPILE_STATUS, &Status );
		if( !Status )
		{
			char Log[1024] = "";
			RenDev->glGetShaderInfoLog( Shaders[i], sizeof(Log)-1, NULL, Log );
			debugf( NAME_Warning, TEXT("UC2GLDrv: post-process %s shader does not compile: %s"), What, appFromAnsi(Log) );
			RenDev->glDeleteShader( Shaders[0] );
			if( i )
				RenDev->glDeleteShader( Shaders[1] );
			return 0;
		}
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
		debugf( NAME_Warning, TEXT("UC2GLDrv: post-process %s program does not link"), What );
		RenDev->glDeleteProgram( Program );
		return 0;
	}
	RectLoc = RenDev->glGetUniformLocation( Program, "uRect" );
	RenDev->glUseProgram( Program );
	RenDev->glUniform1i( RenDev->glGetUniformLocation( Program, "uSource" ), 0 );
	RenDev->glUseProgram( 0 );
	return Program;
}

UBOOL UUC2GLRenderDevice::InitPostProcess()
{
	guard(UUC2GLRenderDevice::InitPostProcess);

	ExitPostProcess();
	PostCopyProgram			= UC2GL_BuildPostProgram( this, GPostCopyFragmentShader, TEXT("copy"), PostCopyRectLoc );
	PostDepthEncodeProgram	= UC2GL_BuildPostProgram( this, GPostDepthEncodeFragmentShader, TEXT("depth encode"), PostDepthEncodeRectLoc );
	PostDepthCopyProgram	= UC2GL_BuildPostProgram( this, GPostDepthCopyFragmentShader, TEXT("depth copy"), PostDepthCopyRectLoc );
	if( !PostCopyProgram || !PostDepthEncodeProgram || !PostDepthCopyProgram )
	{
		// Without the copies nothing of the post-process interface can work: no capability.
		debugf( NAME_Warning, TEXT("UC2GLDrv: post-processing is off: its copy programs could not be built") );
		ExitPostProcess();
		return 0;
	}
	// pp-scale: without it the downsampling passes read the full-size snapshot (sparser, but the
	// same positions): not a reason to switch post-processing off.
	// (Built here for 2 x 2 samples to know it builds; UC2GL_UpdateReferenceCopy builds it again for the
	// back buffer's N.)
	UC2GL_BuildReferenceProgram( this, 2 );
	debugf( NAME_Init, TEXT("UC2GLDrv: post-processing %s (UsePostProcess=%s%s)"),
		(UsePostProcess && UsePixelShaders) ? TEXT("on: render targets, back-buffer and depth copies, the Xbox post-process paths of the engine run") : TEXT("off: as at M6"),
		UsePostProcess ? TEXT("True") : TEXT("False"), UsePixelShaders ? TEXT("") : TEXT(", but pixel shaders are off") );
	debugf( NAME_Init, TEXT("UC2GLDrv: post-process reference height %i (%s)"), PostProcessReferenceHeight,
		!PostProcessReferenceHeight ? TEXT("blurs in back-buffer pixels, as before") :
		PostReferenceProgram ? TEXT("blurs sized for a picture of that many lines, read from a copy of the back buffer that size") :
		TEXT("blurs sized for a picture of that many lines; the reference copy program failed, the full-size snapshot is read instead") );
	return 1;

	unguard;
}

void UUC2GLRenderDevice::DestroyPostProcessTargets()
{
	if( ContextAlive() )
	{
		if( SnapshotFramebuffer )	glDeleteFramebuffers( 1, &SnapshotFramebuffer );
		if( SnapshotTexture )		glDeleteTextures( 1, &SnapshotTexture );
		if( DepthColorFramebuffer )	glDeleteFramebuffers( 1, &DepthColorFramebuffer );
		if( DepthColorTexture )		glDeleteTextures( 1, &DepthColorTexture );
		if( ReferenceFramebuffer )	glDeleteFramebuffers( 1, &ReferenceFramebuffer );
		if( ReferenceTexture )		glDeleteTextures( 1, &ReferenceTexture );
	}
	SnapshotFramebuffer = SnapshotTexture = DepthColorFramebuffer = DepthColorTexture = 0;
	ReferenceFramebuffer = ReferenceTexture = 0;
	ReferenceWidth = ReferenceHeight = 0;
	SnapshotVersion = DepthColorVersion = ReferenceVersion = -1;
}

void UUC2GLRenderDevice::ExitPostProcess()
{
	guard(UUC2GLRenderDevice::ExitPostProcess);

	if( SnapshotCopies || DepthColorCopies || DepthBufferCopies || ReferenceCopies )
		debugf( NAME_Init, TEXT("UC2GLDrv: post-processing: %i back-buffer snapshots, %i reference copies, %i depth-as-colour copies, %i depth buffer copies in %i frames"), SnapshotCopies, ReferenceCopies, DepthColorCopies, DepthBufferCopies, FrameCounter );
	SnapshotCopies = DepthColorCopies = DepthBufferCopies = ReferenceCopies = 0;

	DestroyPostProcessTargets();
	if( ContextAlive() )
	{
		if( PostCopyProgram )			glDeleteProgram( PostCopyProgram );
		if( PostDepthEncodeProgram )	glDeleteProgram( PostDepthEncodeProgram );
		if( PostDepthCopyProgram )		glDeleteProgram( PostDepthCopyProgram );
		if( PostReferenceProgram )		glDeleteProgram( PostReferenceProgram );
		if( GammaRampTexture )			glDeleteTextures( 1, &GammaRampTexture );
	}
	PostCopyProgram = PostDepthEncodeProgram = PostDepthCopyProgram = PostReferenceProgram = 0;
	PostReferenceTaps = 0;
	SampleReference = 0;
	GammaRampTexture	= 0;
	GammaRampActive		= 0;

	unguard;
}

//
// POSTPROCESS [0|1]. Which path an actor is drawn on is cached per actor by the engine
// (FDynamicActor::UpdateSorting, UParticleEmitter::HasPostProcessMaterial) from
// UMaterial::IsPostProcess, which follows the capability: make the engine look again.
//
void UUC2GLRenderDevice::SetPostProcess( UBOOL Enable )
{
	guard(UUC2GLRenderDevice::SetPostProcess);

	UsePostProcess	= Enable;
	GammaRampActive	= 0;
	InitPixelShaders();

	for( TObjectIterator<AActor> It; It; ++It )
	{
		It->bSkinsChanged = 1;
		AEmitter* Emitter = Cast<AEmitter>( *It );
		if( Emitter )
			for( INT i=0; i<Emitter->Emitters.Num(); i++ )
				if( Emitter->Emitters(i) )
					Emitter->Emitters(i)->HasPostProcessMaterial = 0;
	}
	// The static materials the Xbox paths build keep their stages from one mode to the other
	// only through PreRender, which every SetMaterial runs: nothing else to reset.

	unguard;
}

//
// One quad of Source into a framebuffer through one of the copy programs. Leaves the GL
// state for the interface to restore (RestoreAfterDeviceDraw).
//
void UUC2GLRenderDevice::DrawPostQuad( GLuint Program, GLint RectLoc, GLuint SourceTexture, GLuint DestFramebuffer, INT DestWidth, INT DestHeight, const FLOAT* Rect, UBOOL DepthOnly )
{
	StatPostQuads++;
	glBindFramebuffer( GL_FRAMEBUFFER, DestFramebuffer );
	glViewport( 0, 0, DestWidth, DestHeight );
	glDisable( GL_SCISSOR_TEST );
	glDisable( GL_STENCIL_TEST );
	glDisable( GL_BLEND );
	glDisable( GL_ALPHA_TEST );
	glDisable( GL_CULL_FACE );
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	if( DepthOnly )
	{
		glEnable( GL_DEPTH_TEST );
		glDepthFunc( GL_ALWAYS );
		glDepthMask( GL_TRUE );
		glColorMask( GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE );
	}
	else
	{
		glDisable( GL_DEPTH_TEST );
		glDepthMask( GL_FALSE );
		glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	}

	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, SourceTexture );
	glUseProgram( Program );
	glUniform4f( RectLoc, Rect[0], Rect[1], Rect[2], Rect[3] );

	BindQuadBuffer();
	glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, 0, NULL );
	glEnableVertexAttribArray( 0 );
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	glDisableVertexAttribArray( 0 );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glUseProgram( 0 );

	glEnable( GL_SCISSOR_TEST );
	RenderInterface.RestoreAfterDeviceDraw();
}

static void UC2GL_CreateColorTarget( UUC2GLRenderDevice* RenDev, GLuint& Texture, GLuint& Framebuffer, INT Width, INT Height )
{
	RenDev->glGenTextures( 1, &Texture );
	RenDev->glBindTexture( GL_TEXTURE_2D, Texture );
	RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0 );
	RenDev->glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, Width, Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
	RenDev->glGenFramebuffers( 1, &Framebuffer );
	RenDev->glBindFramebuffer( GL_FRAMEBUFFER, Framebuffer );
	RenDev->AttachColorTexture( Texture );
	GLenum Status = RenDev->glCheckFramebufferStatus( GL_FRAMEBUFFER );
	if( Status != GL_FRAMEBUFFER_COMPLETE )
		debugf( NAME_Warning, TEXT("UC2GLDrv: post-process copy target %ix%i incomplete (0x%04x)"), Width, Height, Status );
	RenDev->glBindFramebuffer( GL_FRAMEBUFFER, RenDev->RenderInterface.SavedRenderTargetState.GetCurrentState()->Framebuffer );
}

//
// The GL texture a stage binds for an engine texture: the texture itself, except for the
// default render target (convention 2 at the top of this file).
//
GLuint UUC2GLRenderDevice::GetSampledTexture( FOpenGLTexture* Texture )
{
	if( Texture->IsDefaultTarget && PostProcessActive() )
	{
		GLuint Copy =	Texture->DefaultUsage == FRenderTarget::RT_MATERIAL_USE_DEPTHBUFFER ? DepthColorTexture :
						SampleReference ? ReferenceTexture : SnapshotTexture;
		return Copy ? Copy : NullTextureID;
	}
	return Texture->TextureID;
}

//
// pp-scale: is there a reference copy to make? Post-processing on, a reference height, its
// program, and a back buffer taller than the reference picture (at 480 lines or fewer the
// downsampling passes read the snapshot exactly as before).
//
UBOOL UUC2GLRenderDevice::ReferenceCopyActive()
{
	return PostProcessActive() && PostReferenceProgram && PostProcessReferenceHeight > 0 && MainHeight > PostProcessReferenceHeight;
}

//
// pp-scale: the reference copy, brought up to date (see the header and GPostReferenceFragmentShader).
//
static void UC2GL_UpdateReferenceCopy( UUC2GLRenderDevice* RenDev )
{
	INT H = RenDev->PostProcessReferenceHeight;
	INT W = Max( appRound( (FLOAT)RenDev->MainWidth * H / RenDev->MainHeight ), 1 );
	if( !RenDev->ReferenceTexture || RenDev->ReferenceWidth != W || RenDev->ReferenceHeight != H )
	{
		if( RenDev->ReferenceFramebuffer )	RenDev->glDeleteFramebuffers( 1, &RenDev->ReferenceFramebuffer );
		if( RenDev->ReferenceTexture )		RenDev->glDeleteTextures( 1, &RenDev->ReferenceTexture );
		UC2GL_CreateColorTarget( RenDev, RenDev->ReferenceTexture, RenDev->ReferenceFramebuffer, W, H );
		RenDev->ReferenceWidth	= W;
		RenDev->ReferenceHeight	= H;
		RenDev->ReferenceVersion = -1;
		debugf( NAME_Init, TEXT("UC2GLDrv: reference copy %ix%i of the %ix%i back buffer (%ix%i samples per pixel)"), W, H, RenDev->MainWidth, RenDev->MainHeight,
			Clamp( appCeil( (FLOAT)RenDev->MainHeight / H - 0.01f ), 1, 8 ), Clamp( appCeil( (FLOAT)RenDev->MainHeight / H - 0.01f ), 1, 8 ) );
	}
	if( RenDev->ReferenceVersion == RenDev->MainColorVersion )
		return;

	// The main framebuffer is bottom row first, the copy top row first (convention 2).
	static const FLOAT FlipRect[4] = { 1.f, -1.f, 0.f, 1.f };
	INT N = Clamp( appCeil( (FLOAT)RenDev->MainHeight / H - 0.01f ), 1, 8 );
	RenDev->ResolveMain( 1, 0 );
	if( !UC2GL_BuildReferenceProgram( RenDev, N ) )
	{
		UC2GL_LOG_ONCE( TEXT("UC2GLDrv: the reference copy program for %ix%i samples does not build; the reference copy stays as it is"), N, N );
		return;
	}
	RenDev->glUseProgram( RenDev->PostReferenceProgram );
	RenDev->glUniform4f( RenDev->PostReferenceStepLoc, 1.f / (W * N), 1.f / (H * N), 0.f, 0.f );
	// The samples fall between texels: bilinear for this pass (every other reader of the main
	// colour samples texel centres; FXAA wants it bilinear anyway).
	RenDev->glActiveTexture( GL_TEXTURE0 );
	RenDev->glBindTexture( GL_TEXTURE_2D, RenDev->MainColorTexture );
	RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	RenDev->DrawPostQuad( RenDev->PostReferenceProgram, RenDev->PostReferenceRectLoc, RenDev->MainColorTexture, RenDev->ReferenceFramebuffer, W, H, FlipRect, 0 );
	RenDev->glActiveTexture( GL_TEXTURE0 );
	RenDev->glBindTexture( GL_TEXTURE_2D, RenDev->MainColorTexture );
	RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, RenDev->PresentFXAA ? GL_LINEAR : GL_NEAREST );
	RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, RenDev->PresentFXAA ? GL_LINEAR : GL_NEAREST );
	RenDev->ReferenceVersion = RenDev->MainColorVersion;
	RenDev->ReferenceCopies++;
}

//
// Brings the copy that stands for the default render target up to date, if anything was
// drawn into the main framebuffer since it was made.
//
void UUC2GLRenderDevice::UpdateSampledTexture( FOpenGLTexture* Texture )
{
	guard(UUC2GLRenderDevice::UpdateSampledTexture);

	if( !Texture->IsDefaultTarget || !PostProcessActive() )
		return;

	// The main framebuffer is bottom row first, the copy top row first.
	static const FLOAT FlipRect[4] = { 1.f, -1.f, 0.f, 1.f };

	if( Texture->DefaultUsage == FRenderTarget::RT_MATERIAL_USE_DEPTHBUFFER )
	{
		// Desktop GL: the main depth buffer is a texture and one quad re-encodes it.
		// M11 (WebGL without WEBGL_depth_texture): this is the place where the depth colour
		// would instead come from a cheap pass that draws the scene's depth as colour.
		if( !DepthColorTexture )
		{
			UC2GL_CreateColorTarget( this, DepthColorTexture, DepthColorFramebuffer, MainWidth, MainHeight );
			DepthColorVersion = -1;
		}
		if( !MainDepthStencil )
		{
			// M11, NOT IMPLEMENTED YET: a context without depth textures (CapDepthTexture; WebGL 1
			// without WEBGL_depth_texture). The plan is a second draw of every depth-writing
			// primitive into this target (docs/porting/analysis/web-renderer.md, "Fallbacks").
			// Until then the depth colour says "near" everywhere: the depth of field, its only
			// reader, then leaves the picture sharp instead of blurring all of it.
			UC2GL_LOG_ONCE( TEXT("UC2GLDrv: no depth textures on this context: the depth of field has no depth to read and stays off (fallback not implemented)") );
			if( DepthColorVersion == -1 )
			{
				glBindFramebuffer( GL_FRAMEBUFFER, DepthColorFramebuffer );
				glDisable( GL_SCISSOR_TEST );
				glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
				glClearColor( 0.f, 0.f, 0.f, 0.f );
				glClear( GL_COLOR_BUFFER_BIT );
				glEnable( GL_SCISSOR_TEST );
				RenderInterface.RestoreAfterDeviceDraw();
				DepthColorVersion = 0;
			}
		}
		else if( DepthColorVersion != MainDepthVersion )
		{
			ResolveMain( 0, 1 );
			DrawPostQuad( PostDepthEncodeProgram, PostDepthEncodeRectLoc, MainDepthStencil, DepthColorFramebuffer, MainWidth, MainHeight, FlipRect, 0 );
			DepthColorVersion = MainDepthVersion;
			DepthColorCopies++;
		}
	}
	else if( SampleReference )
	{
		// pp-scale: a downsampling pass reads the back buffer as the reference picture.
		UC2GL_UpdateReferenceCopy( this );
	}
	else
	{
		// Back buffer (and "front buffer": the Xbox's previous frame; nothing in the game asks for it).
		if( !SnapshotTexture )
		{
			UC2GL_CreateColorTarget( this, SnapshotTexture, SnapshotFramebuffer, MainWidth, MainHeight );
			SnapshotVersion = -1;
		}
		if( SnapshotVersion != MainColorVersion )
		{
			// -glsnapshotlog (G5): what asks for each back-buffer snapshot, for one frame (the 600th).
			if( FrameCounter == 600 && ParseParam( appCmdLine(), TEXT("glsnapshotlog") ) )
				debugf( NAME_Init, TEXT("UC2GLDrv: snapshot %i of frame %i, for material %s (main colour version %i, the snapshot had %i)"), SnapshotCopies + 1, FrameCounter,
					RenderInterface.GetLastMaterial() ? RenderInterface.GetLastMaterial()->GetPathName() : TEXT("None"), MainColorVersion, SnapshotVersion );
			ResolveMain( 1, 0 );
			DrawPostQuad( PostCopyProgram, PostCopyRectLoc, MainColorTexture, SnapshotFramebuffer, MainWidth, MainHeight, FlipRect, 0 );
			SnapshotVersion = MainColorVersion;
			SnapshotCopies++;
		}
	}

	unguard;
}

//
// The 256-entry ramp of the dynamic gamma (D3D's SetGammaRamp, applied by the present pass).
//
void UUC2GLRenderDevice::SetGammaRamp( const BYTE* Ramp )
{
	if( !GammaRampTexture )
	{
		glGenTextures( 1, &GammaRampTexture );
		glBindTexture( GL_TEXTURE_2D, GammaRampTexture );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0 );
	}
	else
		glBindTexture( GL_TEXTURE_2D, GammaRampTexture );
	glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_LUMINANCE, 256, 1, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, Ramp );
	extern void UC2GL_RememberGammaRamp( const BYTE* Ramp );
	UC2GL_RememberGammaRamp( Ramp );
	GammaRampActive = 1;
	RenderInterface.InvalidateHardwareState();
}

/*-----------------------------------------------------------------------------
	FOpenGLRenderInterface: state around the device's own draws.
-----------------------------------------------------------------------------*/

void FOpenGLRenderInterface::RestoreAfterDeviceDraw()
{
	RenDev->glBindFramebuffer( GL_FRAMEBUFFER, SavedRenderTargetState.GetCurrentState()->Framebuffer );
	if( !Viewport )
		return;
	ApplyViewport();
	if( DepthEnabled )
		RenDev->glEnable( GL_DEPTH_TEST );
	else
		RenDev->glDisable( GL_DEPTH_TEST );
	if( StencilEnabled )
		RenDev->glEnable( GL_STENCIL_TEST );
	else
		RenDev->glDisable( GL_STENCIL_TEST );
	ApplyStencilState();
	// Blend, alpha test, depth function and mask, culling, colour mask, polygon mode and the
	// texture units come back with the next pass.
	InvalidateHardwareState();
}

void FOpenGLRenderInterface::PrepareSampledTargets( FOpenGLMaterialState* MaterialState )
{
	UBOOL Nudge = 0;
	// pp-scale: a pass that draws the back buffer into a render target of at most three quarters
	// of its height is a downsampling pass of the post-processing (the first pass of the depth of
	// field, the emissive and bloom selections, the back-buffer copy, the luminance target; the
	// targets are at most half the back buffer). It reads the reference copy: the back buffer as a
	// picture of PostProcessReferenceHeight lines, which is what the Xbox's taps were made for.
	// Every other reader (full-screen passes into the back buffer, distortion) reads the snapshot.
	const FOpenGLRenderTargetState* Target = SavedRenderTargetState.GetCurrentState();
	UBOOL Reference = RenDev->ReferenceCopyActive() && Target->RenderTarget && Target->Framebuffer != RenDev->MainFramebuffer
		&& Target->Height * 4 <= RenDev->MainHeight * 3;
	if( Reference != RenDev->SampleReference )
	{
		// The stages bind another texture for the default render target: rebind them.
		RenDev->SampleReference = Reference;
		InvalidateHardwareState();
	}
	if( RenDev->PostProcessActive() )
	{
		INT StagesUsed = Min<INT>( MaterialState->StagesUsed, RenDev->NumTextureUnits );
		for( INT s=0; s<StagesUsed; s++ )
		{
			FOpenGLTexture* Texture = MaterialState->Stages[s].Texture;
			if( !Texture || !Texture->IsRenderTarget )
				continue;
			if( Texture->IsDefaultTarget )
				RenDev->UpdateSampledTexture( Texture );
			else if( Texture->FramebufferID && Texture->FramebufferID == SavedRenderTargetState.GetCurrentState()->Framebuffer )
				UC2GL_LOG_ONCE( TEXT("UC2GLDrv: a pass samples the %ix%i render target it draws into (undefined in GL; not expected from the game)"), Texture->CachedWidth, Texture->CachedHeight );
			// Convention 4: a point-filtered read of a render target.
			if( MaterialState->Stages[s].MagFilter == D3DTEXF_POINT )
				Nudge = 1;
		}
	}
	if( Nudge != PointSampleNudge )
	{
		PointSampleNudge	= Nudge;
		MatricesDirty		= 1;
	}
}

/*-----------------------------------------------------------------------------
	Back buffer as a material, the back-buffer copy target.
-----------------------------------------------------------------------------*/

//
//	FD3DRenderInterface::GetBackBufferMaterial
//
UMaterial* FOpenGLRenderInterface::GetBackBufferMaterial()
{
	guard(FOpenGLRenderInterface::GetBackBufferMaterial);

	// UC2GLDrv: D3DDrv's "#ifndef _XBOX return NULL;" is the device's switch.
	if( !RenDev->PostProcessActive() )
		return NULL;

	DECLARE_STATIC_UOBJECT(	UProxyBitmapMaterial, BackBufferMaterial, {});
	BackBufferMaterial->SetTextureInterface( RenDev->DefaultRenderTarget );

	return BackBufferMaterial;

	unguard;
}

/* scion ======================================================================
* FD3DRenderInterface::GetBackBufferCopyTarget
* Author: sz
*
* creates the back buffer copy render target if it doesn't exist and return it
*
* ============================================================================
*/
FRenderTarget* FOpenGLRenderInterface::GetBackBufferCopyTarget()
{
	guard(FOpenGLRenderInterface::GetBackBufferCopyTarget);

	// UC2GLDrv: PC's D3DDrv would dereference GetBackBufferMaterial()'s NULL here; nothing
	// on PC calls it. Without post-processing: what M5 returned.
	if( !RenDev->PostProcessActive() )
		return BackBufferCopyTarget;

	// get a back buffer material
	UMaterial* SourceMaterial = GetBackBufferMaterial();
	// scale to apply to back buffer size
	const FLOAT TestScale =  0.5f;
	// get the full screen size of the back buffer
	INT FullScreenSizeX = SourceMaterial->MaterialUSize();
	INT FullScreenSizeY = SourceMaterial->MaterialVSize();
	// calc size of the back buffer copy
	INT CopyTargetSizeX = FullScreenSizeX * TestScale;
	INT CopyTargetSizeY = FullScreenSizeY * TestScale;

	// create the back buffer copy target if it doesn't exist
	// or if needs to be resized
	if( !BackBufferCopyTarget ||
		BackBufferCopyTarget->GetWidth() != CopyTargetSizeX ||
		BackBufferCopyTarget->GetHeight() != CopyTargetSizeY )
	{
		// remove the old one
		if( BackBufferCopyTarget )
		{
			BackBufferCopyTarget->RemovePoolRef();
			BackBufferCopyTarget = NULL;
		}
        // this should reuse the same target as the bloom back buffer (320x240)
		BackBufferCopyTarget = RenderTargetPool.ObtainRenderTarget( CopyTargetSizeX, CopyTargetSizeY,
			TEXF_RGBA8, FRenderTarget::RT_CREATE_COLOR|FRenderTarget::RT_CREATE_DEPTH );
	}

	return BackBufferCopyTarget;

	unguard;
}

/* scion ======================================================================
* FD3DRenderInterface::UpdateBackBufferCopyTarget
* Author: sz
*
* blit portion of the back buffer set by the current viewport extents to
* the back buffer copy target
*
* ============================================================================
*/
void FOpenGLRenderInterface::UpdateBackBufferCopyTarget( UViewport* InViewport )
{
	guard(FOpenGLRenderInterface::UpdateBackBufferCopyTarget);

	// UC2GLDrv: D3DDrv's "#ifndef _XBOX return;".
	if( !RenDev->PostProcessActive() || !RenDev->PixelShaderFamilyReady( PS_Blur ) )
		return;

	if( !InViewport )
	{
		InViewport = Viewport;
		check( InViewport );
	}

	// make sure it exists
    BackBufferCopyTarget = GetBackBufferCopyTarget();

	// switch to the back buffer target so we can blit the
	// original back buffer to it
	SetRenderTarget( BackBufferCopyTarget );

	// get a back buffer material
	UMaterial* SourceMaterial = GetBackBufferMaterial();
	// get the full screen size of the back buffer
	INT FullScreenSizeX = SourceMaterial->MaterialUSize();
	INT FullScreenSizeY = SourceMaterial->MaterialVSize();
	// scale to apply to back buffer size
	FLOAT TestScale = (FLOAT)BackBufferCopyTarget->GetWidth() / (FLOAT)FullScreenSizeX;

	// offset matrices used for 4 sample blur
	DECLARE_STATIC_UOBJECT_ARRAY( UTexMatrix, SourceTexMatrix, 4,
	{
		for( INT i=0; i<4; i++ )
		{
			SourceTexMatrix[i]->Matrix.SetIdentity();
			SourceTexMatrix[i]->Material = SourceMaterial;
		}
	} );
	// create a 4 sample blur pixel shader
	DECLARE_STATIC_UOBJECT( UPSBlurMaterial, BlurPixelShader,
	{
		BlurPixelShader->BlurSourceMaterial = SourceMaterial;
		BlurPixelShader->BlurSourceOffset0 = SourceTexMatrix[0];
		BlurPixelShader->BlurSourceOffset1 = SourceTexMatrix[1];
		BlurPixelShader->BlurSourceOffset2 = SourceTexMatrix[2];
		BlurPixelShader->BlurSourceOffset3 = SourceTexMatrix[3];
	});

	// setup the sample ofsets
	// weights should default to 0.25f
	FLOAT OneTexelU = 1.0f / (FLOAT)FullScreenSizeX;
	FLOAT OneTexelV = 1.0f / (FLOAT)FullScreenSizeY;
	// UC2GLDrv (pp-scale): the taps are pixels of the reference picture (k back-buffer pixels;
	// GetPostProcessReference, 1 at 480 lines), read from the reference copy (PrepareSampledTargets:
	// the target is half the back buffer), so the blurred copy is as blurred, for its share of the
	// picture, as the Xbox's. The target itself stays half the back buffer: it is also the
	// distortion accumulation target and its depth copy (UnRender.cpp, FPostProcessRender), whose
	// edges stay sharper with more texels; the distortion amounts are scaled separately
	// (UC2GLShaders.cpp, PixelScale).
	FLOAT PixelScale = GetPostProcessReference( FullScreenSizeX, FullScreenSizeY ).PixelScale;
	OneTexelU *= PixelScale;
	OneTexelV *= PixelScale;

	// tex0 offset
	SourceTexMatrix[0]->Matrix.M[2][0] = OneTexelU * -1.0f;
	SourceTexMatrix[0]->Matrix.M[2][1] = OneTexelV * -1.0f;

	// tex1 offset
	SourceTexMatrix[1]->Matrix.M[2][0] = OneTexelU * 1.0f;
	SourceTexMatrix[1]->Matrix.M[2][1] = OneTexelV * -1.0f;

	// tex2 offset
	SourceTexMatrix[2]->Matrix.M[2][0] = OneTexelU * -1.0f;
	SourceTexMatrix[2]->Matrix.M[2][1] = OneTexelV * 1.0f;

	// tex3 offset
	SourceTexMatrix[3]->Matrix.M[2][0] = OneTexelU * 1.0f;
	SourceTexMatrix[3]->Matrix.M[2][1] = OneTexelV * 1.0f;

    // create a final blend to disable z-buffer writes
	DECLARE_STATIC_UOBJECT(	UFinalBlend, BackBufferFinalBlend,
	{
		BackBufferFinalBlend->ZWrite = 0;
		BackBufferFinalBlend->Material = BlurPixelShader;
	} );
	// adjust UVs based on viewport size
	// so that we only blit the desired portion of the back buffer
	FLOAT OffsetX = (FLOAT)InViewport->OffsetX / (FLOAT)FullScreenSizeX;
	FLOAT OffsetY = (FLOAT)InViewport->OffsetY / (FLOAT)FullScreenSizeY;
	FLOAT SizeX = (FLOAT)InViewport->SizeX / (FLOAT)FullScreenSizeX * InViewport->RenDev->FSAAScale[0];
	FLOAT SizeY = (FLOAT)InViewport->SizeY / (FLOAT)FullScreenSizeY * InViewport->RenDev->FSAAScale[1];
	FLOAT U1[4] = { OffsetX, OffsetX, OffsetX, OffsetX };
	FLOAT V1[4] = { OffsetY, OffsetY, OffsetY, OffsetY };
	FLOAT U2[4] = { OffsetX+SizeX, OffsetX+SizeX, OffsetX+SizeX, OffsetX+SizeX };
	FLOAT V2[4] = { OffsetY+SizeY, OffsetY+SizeY, OffsetY+SizeY, OffsetY+SizeY };
	static FColor WhiteOpaque(255,255,255,255);
	// draw full screen quad into BackBufferCopyTarget
	FScreenPrimUtil( BackBufferCopyTarget, this ).DrawTile(
		(InViewport->OffsetX)*TestScale,
		(InViewport->OffsetY)*TestScale,
		(InViewport->OffsetX+InViewport->SizeX)*TestScale,
		(InViewport->OffsetY+InViewport->SizeY)*TestScale,
		U1, V1,	U2, V2,
		0.0f,
		BlurPixelShader,
		WhiteOpaque
		);

	// set render target back to default
	ResetRenderTarget();

	unguard;
}

//
//	FD3DRenderInterface::GetBackBufferCopyMaterial
//
UMaterial* FOpenGLRenderInterface::GetBackBufferCopyMaterial()
{
	guard(FOpenGLRenderInterface::GetBackBufferCopyMaterial);

	// UC2GLDrv: D3DDrv's "#ifndef _XBOX return NULL;".
	if( !RenDev->PostProcessActive() )
		return NULL;

	if( !BackBufferCopyTarget )
	{
		BackBufferCopyTarget = GetBackBufferCopyTarget();
	}

	DECLARE_STATIC_UOBJECT(	UProxyBitmapMaterial, BackBufferCopyMaterial, {});
	BackBufferCopyMaterial->SetTextureInterface( BackBufferCopyTarget );

	return BackBufferCopyMaterial;

	unguard;
}

/* scion ======================================================================
* FD3DRenderInterface::CopyDepthBuffer
* Author: sz
*
* copy depth buffer from one render target to another
*
* UC2GLDrv: the Xbox draws the source's depth buffer, re-typed as a colour texture, into
* the destination's depth buffer, re-typed as a colour surface: a point-sampled quad whose
* coordinates select the viewport's part of the source. GL cannot re-type buffers; the
* same quad is drawn with a shader that writes the sampled depth as the fragment's depth.
* Stencil is not copied (the accumulation pass this serves does not use it). The source
* has to be the default render target: only its depth buffer is a texture, as on Xbox.
* ============================================================================
*/
void FOpenGLRenderInterface::CopyDepthBuffer( FRenderTarget* DestTarget, FRenderTarget* SrcTarget )
{
	guard(FOpenGLRenderInterface::CopyDepthBuffer);

	if( !RenDev->PostProcessActive() || !RenDev->PostDepthCopyProgram || !DestTarget )
		return;
	if( SrcTarget && SrcTarget != RenDev->DefaultRenderTarget )
	{
		UC2GL_LOG_ONCE( TEXT("UC2GLDrv: CopyDepthBuffer from a render target other than the back buffer is not supported") );
		return;
	}

	// make sure destination target texture is cached
	FOpenGLTexture* DestTexture = CacheTexture( DestTarget );
	if( !DestTexture || !DestTexture->FramebufferID || !DestTexture->DepthStencilID )
	{
		UC2GL_LOG_ONCE( TEXT("UC2GLDrv: CopyDepthBuffer into a render target without its own depth buffer is not supported") );
		return;
	}

	if( !RenDev->MainDepthStencil )
	{
		// M11, NOT IMPLEMENTED: a context without depth textures (WebGL 1 without
		// WEBGL_depth_texture) has no scene depth a shader can read, so neither the copy nor
		// the in-shader test below can be done (web-renderer.md, "Fallbacks"). The target's
		// depth is cleared: what is drawn into it (the distortion accumulation) is not hidden
		// by nearer scene geometry.
		UC2GL_LOG_ONCE( TEXT("UC2GLDrv: no depth textures on this context: the scene's depth cannot reach the distortion accumulation target, distortion shows through nearer geometry (fallback not implemented)") );
		RenDev->glBindFramebuffer( GL_FRAMEBUFFER, DestTexture->FramebufferID );
		RenDev->glDisable( GL_SCISSOR_TEST );
		RenDev->glDepthMask( GL_TRUE );
		RenDev->glClearDepth( 1.0 );
		RenDev->glClear( GL_DEPTH_BUFFER_BIT );
		RenDev->glEnable( GL_SCISSOR_TEST );
		RestoreAfterDeviceDraw();
		return;
	}

	// adjust UVs based on viewport size
	// so that we only blit the desired portion of the back buffer
	FLOAT OffsetX = (FLOAT)Viewport->OffsetX / (FLOAT)RenDev->MainWidth;
	FLOAT OffsetY = (FLOAT)Viewport->OffsetY / (FLOAT)RenDev->MainHeight;
	// The Xbox quad is an FScreenPrimUtil tile over the whole destination with texture
	// coordinates (Offset, Offset + Scale), point filtered. That canvas puts pixel centres on
	// texel centres, so destination pixel (x,y) samples at Offset + Scale * (x+0.5, y+0.5) /
	// size, exactly as this quad does. For a half-size destination that is the corner shared
	// by four source texels: a tie, which hardware resolves to the texel right and below
	// ((2x+1, 2y+1)) and GL leaves to rounding. A quarter source texel is added to make it
	// that texel always. The source is stored bottom row first, the destination top row
	// first: v is mirrored.
	FLOAT	U0		= OffsetX + 0.25f / RenDev->MainWidth,
			V0		= OffsetY + 0.25f / RenDev->MainHeight;
	FLOAT Rect[4] =
	{
		Viewport->ScaleX,
		-Viewport->ScaleY,
		U0,
		1.f - V0
	};
	// O-8: the multisampled main depth into the texture both paths below read.
	RenDev->ResolveMain( 0, 1 );
	if( !RenDev->CapFragDepth )
	{
		// M11, a context without gl_FragDepth (WebGL 1 without EXT_frag_depth): the depth
		// cannot be written by a shader, so it is not copied. The target's depth is cleared
		// instead (the accumulation passes still test against each other there), and every
		// program pass drawn into this target until the next frame compares its fragment's
		// depth with the scene's depth in the shader, through the same mapping the copy quad
		// would use: target pixel centre (gl_FragCoord.xy) / target size -> this Rect.
		UC2GL_LOG_ONCE( TEXT("UC2GLDrv: no gl_FragDepth on this context: the distortion accumulation passes test the scene's depth in their fragment shaders instead of a depth copy") );
		RenDev->glBindFramebuffer( GL_FRAMEBUFFER, DestTexture->FramebufferID );
		RenDev->glDisable( GL_SCISSOR_TEST );
		RenDev->glDepthMask( GL_TRUE );
		RenDev->glClearDepth( 1.0 );
		RenDev->glClear( GL_DEPTH_BUFFER_BIT );
		RenDev->glEnable( GL_SCISSOR_TEST );
		RestoreAfterDeviceDraw();
		SceneDepthTestFramebuffer	= DestTexture->FramebufferID;
		SceneDepthTestRect[0]		= Rect[0] / Max<INT>( DestTexture->CachedWidth, 1 );
		SceneDepthTestRect[1]		= Rect[1] / Max<INT>( DestTexture->CachedHeight, 1 );
		SceneDepthTestRect[2]		= Rect[2];
		SceneDepthTestRect[3]		= Rect[3];
		return;
	}
	RenDev->DrawPostQuad( RenDev->PostDepthCopyProgram, RenDev->PostDepthCopyRectLoc, RenDev->MainDepthStencil, DestTexture->FramebufferID, DestTexture->CachedWidth, DestTexture->CachedHeight, Rect, 1 );
	RenDev->DepthBufferCopies++;

	unguard;
}

/*-----------------------------------------------------------------------------
	Depth of field: the ramp lookup texture.
-----------------------------------------------------------------------------*/

//
// FD3DRenderInterface::UpdateDOFRampLookupTexture's Xbox body. UC2GLDrv: the Xbox writes
// the swizzled texture memory in place; here the bytes go into the texture's mip and the
// texture is marked changed, so the device uploads it again. Everything else is Epic's.
//
void UC2GL_UpdateDOFRampLookupTexture( FOpenGLRenderInterface* RI, UMaterial* DOFShader, BYTE FocusType )
{
	guard(UC2GL_UpdateDOFRampLookupTexture);

	UViewport* Viewport = RI->Viewport;
	UPSDepthOfFieldMaterial* InDOFShader = (UPSDepthOfFieldMaterial*)DOFShader;

	if( !InDOFShader )
		return;

	if( !InDOFShader->bUpdateRampLookup )
		return;

	UTexture* Ramp = InDOFShader->DynamicRampLookup;
	if( !Ramp || Ramp->Format != TEXF_A8 || Ramp->Mips.Num() < 1 )
		return;
	Ramp->Mips(0).DataArray.Load();

	DWORD Width = Ramp->USize;
	DWORD Height = Ramp->VSize;
	if( (DWORD)Ramp->Mips(0).DataArray.Num() < Width * Height )
		return;
	BYTE* Bits = &Ramp->Mips(0).DataArray(0);

	// -- Using D3DFMT_LIN_B8G8R8A8 format for the depth buffer. So, B8G8 represent the most significant 16bits
	//    of Z.  The pixel shader for DOF uses a texreg2gb dependent texture read on these depth values.
	//    This means that U coordinates come from Green and V coordinates come from Blue.  So our U axis (width)
	//    represents the least significant 8 bits (green) and the V axis (Height) represent the most significant
	//    8 bits (blue) of our 16 bit z value.

	BYTE* p;
	FLOAT fU,fV,fUV,Result;
	DWORD dwU,dwV,dwUV;

	// Only do the midpoint function for dynamically adjusting depths
	// DOF_FocusAbsoluteZDepth == 3
	if (FocusType < 3)
	{
		// -- Convert world coordinate values for near/far to homogeneous space (WVP & 1/w) z values
		//    These near/far values will then be used to bias and scale the lookup ramp so that only areas
		//	  within [0.0,Near] and [Far,1.0] appear blurry.  That's the theory anyway.

		FPlane vNear( InDOFShader->FocusWorldNear.X, InDOFShader->FocusWorldNear.Y, InDOFShader->FocusWorldNear.Z, 1.0f );
		vNear = Viewport->LodSceneNode->WorldToCamera.TransformFPlane( vNear );

		// -- clamp DOF near to be bigger than the NEAR_CLIPPING_PLANE plus some silly fudge factor
		FLOAT NearDiff = 0.0f;
		static FLOAT MinZNear = (NEAR_CLIPPING_PLANE+40.f);
		if( vNear.Z < MinZNear )
		{
			NearDiff = MinZNear - vNear.Z;
			vNear.Z = MinZNear;
		}
		vNear = Viewport->LodSceneNode->CameraToScreen.TransformFPlane( vNear );
		InDOFShader->FocusNear = vNear.Z / vNear.W;

		FPlane vFar( InDOFShader->FocusWorldFar.X, InDOFShader->FocusWorldFar.Y, InDOFShader->FocusWorldFar.Z, 1.0f );
		vFar = Viewport->LodSceneNode->WorldToCamera.TransformFPlane( vFar );
		// -- add silly fudge factor to DOF far so that we maintain the midpoint over the focus distance
		vFar.Z += NearDiff;
		vFar = Viewport->LodSceneNode->CameraToScreen.TransformFPlane( vFar );
		InDOFShader->FocusFar = vFar.Z / vFar.W;

		if( InDOFShader->FocusFar < InDOFShader->FocusNear )
		{
			// -- shouldn't happen.. but swap if near>far
			FLOAT FocusTemp = InDOFShader->FocusFar;
			InDOFShader->FocusFar = InDOFShader->FocusNear;
			InDOFShader->FocusNear = FocusTemp;
		}

		// -- set values
		FLOAT FocusRange = InDOFShader->FocusFar - InDOFShader->FocusNear;				// positive distance between Near+Far
		FLOAT FocusMidpoint = (InDOFShader->FocusFar + InDOFShader->FocusNear)*0.5f;	// (Near+Far)/2

		for( DWORD v=0; v<Height; v++ )		// blue channel (most significant 8 bits of z)
		{
			fV = v * 1.f/(Height-1);
			dwV = appTrunc(fV*255.0f);
			for( DWORD u=0; u<Width; u++ )			// green channel (least significant 8 bits of z)
			{
				p = Bits + v*Width + u;
				fU = u * 1.f/(Width-1);
				dwU = appTrunc(fU*255.0f);
				dwUV = ((dwV<<16)&0xff0000) | ((dwU<<8)&0x00ff00);
				fUV = (dwUV/(float)(D3DZ_MAX_D24S8));
				Result = (2.0f/FocusRange) * (fUV-FocusMidpoint);		// y = (2*(x-focusMidpoint)/FocusRange)^4
				Result = Result*Result;
				Result = (Result>1.0f) ? 1.0f : Result;
				*p = (BYTE)(255 * Result);
			}
		}
	}
	else
	{
		// Calculate an alpha based upon absolute Z-Depth
		for( DWORD v=0; v<Height; v++ )
		{
			fV = v * 1.f/(Height-1);
			dwV = appTrunc(fV*255.0f);
			for( DWORD u=0; u<Width; u++ )
			{
				p = Bits + v*Width + u;
				fU = u * 1.f/(Width-1);
				dwU = appTrunc(fU*255.0f);
				dwUV = ((dwV<<16)&0xff0000) | ((dwU<<8)&0x00ff00);
				fUV = (dwUV/(float)(D3DZ_MAX_D24S8));
				// Use zero alpha until we cross StartZ (FocusNear)
				if (fUV < InDOFShader->FocusNear)
				{
					*p = 0;
				}
				else
				{
					// Calculate the blur based upon the distance between start
					// and end depths
					Result = (fUV - InDOFShader->FocusNear) / (1.f - InDOFShader->FocusNear);
					*p = (BYTE)(255.f * Result);
				}
			}
		}
	}

	// UC2GLDrv: upload on the next use.
	Ramp->bRealtimeChanged = 1;

	InDOFShader->bUpdateRampLookup = false;

	unguard;
}

/*-----------------------------------------------------------------------------
	Dynamic gamma: FD3DDynamicGamma (D3DRenderInterface.cpp 3150-3450).
-----------------------------------------------------------------------------*/

class FOpenGLDynamicGamma
{
public:
	struct FHistogram
	{
		FLOAT	Values[256];
		FLOAT	MinL, MaxL, AvgL;
	};

	FOpenGLRenderInterface*	RI;
	FLOAT					UpdateRate,
							TransitionRate,
							LastUpdateTime;
	INT						LuminanceSampleWidth,
							LuminanceSampleHeight;
	FAuxRenderTarget*		LuminanceTarget;
	UBOOL					ForceUpdate;
	FHistogram				Histogram;
	FLOAT					CurMinL, CurMaxL, CurAvgL;
	BYTE					GammaRamp[256];

	FOpenGLDynamicGamma( FOpenGLRenderInterface* InRI ) :
		RI(InRI),
		UpdateRate(0.05f),
		TransitionRate(0.05f),
		LastUpdateTime(0),
		LuminanceSampleWidth(16),
		LuminanceSampleHeight(16),
		LuminanceTarget(NULL),
		ForceUpdate(0)
	{
		appMemzero( &Histogram, sizeof(Histogram) );
		CurMinL = 0.0f;
		CurMaxL = 1.0f;
		CurAvgL = 0.5f;
	}
	~FOpenGLDynamicGamma()
	{
		if( LuminanceTarget )
			delete LuminanceTarget;
	}

	void UpdateLuminanceTarget();
	void UpdateHistogram();
	void UpdateDeviceGamma();
};

// scion sz --
//
void FOpenGLDynamicGamma::UpdateLuminanceTarget()
{
	guard(FOpenGLDynamicGamma::UpdateLuminanceTarget);

	// create a new render target if it doesn't exist and make sure it's the right size
	if( !LuminanceTarget ||
		LuminanceTarget->GetWidth() != LuminanceSampleWidth ||
		LuminanceTarget->GetHeight() != LuminanceSampleHeight )
	{
		if( LuminanceTarget )
			delete LuminanceTarget;
		LuminanceTarget = NEW FAuxRenderTarget( LuminanceSampleWidth, LuminanceSampleHeight, TEXF_RGBA8, FRenderTarget::RT_CREATE_COLOR );
	}

	check(LuminanceTarget);

	// UC2GLDrv: also when the clock went back (a new level).
	if( LastUpdateTime+UpdateRate > RI->Viewport->CurrentTime && LastUpdateTime <= RI->Viewport->CurrentTime )
		return;

	LastUpdateTime = RI->Viewport->CurrentTime;

	RI->SetRenderTarget( LuminanceTarget );

	DECLARE_STATIC_UOBJECT(UPixelShaderMaterial,PSLuminanceTransform,
	{
		PSLuminanceTransform->PixelShaderType = PS_LuminanceTexIndex;
		PSLuminanceTransform->RenderHint = MRH_PixelShaderMaterial;
	});
	DECLARE_STATIC_UOBJECT(	UTexMatrix,	BackBufferTexMatrix,
	{
		BackBufferTexMatrix->Matrix.SetIdentity();
	});
	BackBufferTexMatrix->Material = RI->GetBackBufferMaterial();
	PSLuminanceTransform->TextureStage0 = BackBufferTexMatrix;

	static FLOAT U1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	static FLOAT V1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	static FLOAT U2[4] = {1.0f,	1.0f, 1.0f,	1.0f};
	static FLOAT V2[4] = {1.0f,	1.0f, 1.0f,	1.0f};

	FScreenPrimUtil(LuminanceTarget,RI).DrawTile(
		0,0,LuminanceSampleWidth,LuminanceSampleHeight,
		U1,V1,U2,V2,
		0.f,
		PSLuminanceTransform,
		FColor(255,255,255,255)
		);
	//scion jg -- Pop the render target change
	RI->ResetRenderTarget();

	// refresh histogram values
	UpdateHistogram();

	unguard;
}

// scion sz --
//
void FOpenGLDynamicGamma::UpdateHistogram()
{
	guard(FOpenGLDynamicGamma::UpdateHistogram);
	check(LuminanceTarget);

	appMemzero( &Histogram, sizeof(Histogram) );

	UUC2GLRenderDevice* RenDev = RI->RenDev;
	FOpenGLTexture* LuminanceTex = (FOpenGLTexture*) RenDev->GetCachedResource( LuminanceTarget->GetCacheId(),CH_Texture );
	if( !LuminanceTex || !LuminanceTex->FramebufferID )
		return;

	DWORD Width = LuminanceTex->CachedWidth;
	DWORD Height = LuminanceTex->CachedHeight;

	// UC2GLDrv: the Xbox reads the texture's memory, PC's D3D would lock it; GL reads the
	// target back (16x16 pixels). The byte Epic's loop takes from each A8R8G8B8 pixel is the
	// first in memory: blue. Row order does not matter to a histogram.
	TArray<BYTE> Pixels;
	Pixels.Add( Width * Height * 4 );
	RenDev->glBindFramebuffer( GL_FRAMEBUFFER, LuminanceTex->FramebufferID );
	RenDev->glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	RenDev->glReadPixels( 0, 0, Width, Height, GL_BGRA, GL_UNSIGNED_BYTE, &Pixels(0) );
	RenDev->glBindFramebuffer( GL_FRAMEBUFFER, RI->SavedRenderTargetState.GetCurrentState()->Framebuffer );
	BYTE* Bits = &Pixels(0);

	FLOAT LuminosityBias = RI->Viewport->GetOuterUClient()->LuminosityBias;
	// UC2GLDrv: UClient::Init sets LuminosityBias to 1 under _XBOX only and it is not a config
	// property, so on PC it is 0 until the LUMINOSITYBIAS command is used; 0 would put every
	// sample in the first histogram bucket. Unset means the Xbox value.
	if( LuminosityBias <= 0.f )
		LuminosityBias = 1.f;

	// fill in the histogram with values from the Luminosity texture
	Histogram.AvgL = 0.f;
	for( DWORD v=0; v<Height; v++ )	{
		for( DWORD u=0; u<Width; u++ ) {
			BYTE index = *(Bits + v*Width*sizeof(DWORD) + u*sizeof(DWORD));

			// apply luminosity scale to make the scene seem brighter
			index = (BYTE)Clamp<INT>( appTrunc( LuminosityBias * (FLOAT)index ), 0, 255 );

			Histogram.Values[index] += 1.0f;
			Histogram.AvgL += (FLOAT)index;
		}
	}

	// normalize histogram values
	FLOAT TotalSamples = Width*Height;
	FLOAT TotalSamplesInv = (TotalSamples > 0.f) ? 1.f/TotalSamples : 0.f;
	for( INT i=0; i<256; i++ ) {
		Histogram.Values[i] *= TotalSamplesInv;
	}

	static FLOAT AreaThreshold = 0.1f;
	FLOAT Area = 0.f;

    // calc minimum luminosity value
	// clamp to MinLThreshold
	Area = 0.f;
	Histogram.MinL = 0.f;
	for( INT i=0; i<256; i++ ) {
		Area += Histogram.Values[i];
		if( Area >= AreaThreshold ) {
			Histogram.MinL = (FLOAT)i/255.f;
			break;
		}
	}

	// calc maximum luminosity value
	// clamp to MaxLThreshold
	Area = 0.f;
	Histogram.MaxL = 1.f;
	for( INT i=255; i>=0; i-- ) {
		Area += Histogram.Values[i];
		if( Area >= AreaThreshold ) {
			Histogram.MaxL = (FLOAT)i/255.f;
			break;
		}
	}

	// calc average luminosity value
	Histogram.AvgL = Clamp<FLOAT>( Histogram.AvgL * TotalSamplesInv * 1.f/255.f, Histogram.MinL, Histogram.MaxL );

	// clamp MinL/MaxL using current contrast setting
	//FLOAT Contrast = RI->Viewport->GetOuterUClient()->Contrast;
	FLOAT Contrast = RI->Viewport->Actor->Level->LevelContrast;
	FLOAT MidPoint = Histogram.MinL + (Histogram.MaxL - Histogram.MinL)*0.5f;
	FLOAT ClampMinL = Max(0.0f, MidPoint - (1.0f - Contrast));
	FLOAT ClampMaxL = Min(1.0f, MidPoint + (1.0f - Contrast));
	Histogram.MinL = Clamp<FLOAT>( Histogram.MinL, 0.0f, ClampMinL );
	Histogram.MaxL = Clamp<FLOAT>( Histogram.MaxL, ClampMaxL, 1.0f );

	ForceUpdate = 1;

	unguard;
}

// scion sz --
//
void FOpenGLDynamicGamma::UpdateDeviceGamma()
{
	guard(FOpenGLDynamicGamma::UpdateDeviceGamma);

	static const FLOAT UpdateThreshold=0.01f;
	UBOOL DeviceUpdateRequired=0;

	// update current Min/Max/Avg luminosity values
	// only update if the values change by more than UpdateThreshold
	if( Abs(CurMinL-Histogram.MinL) > UpdateThreshold )
	{
		// interpolate between the current min luminosity and
		// the new desired luminosity stored in the histogram
		// this is done at a rate of TransitionRate
		CurMinL = Lerp( CurMinL, Histogram.MinL, TransitionRate );
        DeviceUpdateRequired=1;
	}
	if( Abs(CurMaxL-Histogram.MaxL) > UpdateThreshold )
	{
		CurMaxL = Lerp( CurMaxL, Histogram.MaxL, TransitionRate );
		DeviceUpdateRequired=1;
	}
	if( Abs(CurAvgL-Histogram.AvgL) > UpdateThreshold )
	{
		CurAvgL = Lerp( CurAvgL, Histogram.AvgL, TransitionRate );
		DeviceUpdateRequired=1;
	}

	// UC2GLDrv: the present pass has no ramp yet after UpdateGamma replaced it.
	if( !DeviceUpdateRequired && !ForceUpdate && RI->RenDev->GammaRampActive )
		return;

	// distance between current max and min luminosity
	FLOAT Dist = (CurMaxL - CurMinL);
	// current average luminosity is not (max-min)/2
	// calculate ratio of average luminosity (of all historgram values)
	// versus the distance between the max and min luminosity values
	// assumes CurMinL <= CurAvgL <= CurMaxL
	FLOAT AvgRatio = (CurAvgL - CurMinL) / Dist;
	//FLOAT Gamma = RI->Viewport->GetOuterUClient()->Gamma;
	FLOAT Gamma = RI->Viewport->Actor->Level->LevelGamma;
	// typcially the exponent would just be 1/gamma but we want to shift
	// the gamma curve in the direction of the average luminosity
	// this is done by simply adding the ratio of average luminosity to 1/gamma
	static const FLOAT AvgRatioScale = 0.3f;
	FLOAT Power = 1.f/Gamma + AvgRatio*AvgRatioScale;
	FLOAT x=0.f;

	for( INT i=0; i<256; i++ ) {
		x = i/255.f;
		if( x <= CurMinL )
		{
			GammaRamp[i] = 0;
		}
		else if( x >= CurMaxL )
		{
			GammaRamp[i] = 255;
		}
		else
		{
			GammaRamp[i] = (BYTE)Clamp<INT>( appTrunc( 255.f * appPow( (x-CurMinL)/Dist, Power ) + 0.5f ), 0, 255 );
		}
	}

	RI->RenDev->SetGammaRamp( GammaRamp );
	ForceUpdate = 0;

	// UC2GLDrv: evidence for the log, every 200th ramp.
	static INT RampCount = 0;
	if( (RampCount++ % 200) == 0 )
		debugf( NAME_Init, TEXT("UC2GLDrv: dynamic gamma ramp %i: luminance min %.3f max %.3f average %.3f (histogram %.3f %.3f %.3f), exponent %.3f, ramp[64,128,192] = %i %i %i"),
			RampCount, CurMinL, CurMaxL, CurAvgL, Histogram.MinL, Histogram.MaxL, Histogram.AvgL, Power, GammaRamp[64], GammaRamp[128], GammaRamp[192] );

	unguard;
}

// scion sz --
//
void FOpenGLRenderInterface::UpdateDynamicGamma()
{
	guard(FOpenGLRenderInterface::UpdateDynamicGamma);

	// UC2GLDrv: D3DDrv's "#ifndef _XBOX return;".
	if( !RenDev->PostProcessActive() || !RenDev->PixelShaderFamilyReady( PS_LuminanceTexIndex ) )
		return;

	if( !DynamicGamma )
		DynamicGamma = new FOpenGLDynamicGamma(this);

	if( Viewport->GetOuterUClient()->DynamicGamma )
	{
		DynamicGamma->UpdateLuminanceTarget();
		DynamicGamma->UpdateDeviceGamma();
	}
	else if( RenDev->GammaRampActive )
	{
		// UC2GLDrv: DYNAMICGAMMA 0 calls UEngine::UpdateGamma on Xbox, which sets the static
		// ramp again; make sure of it here.
		RenDev->GammaRampActive = 0;
	}

	// Viewport->bDebugShowGamma (FD3DDynamicGamma::ShowDebug, a histogram overlay) is not ported.

	unguard;
}

// scion sz --
//
FLOAT FOpenGLRenderInterface::GetAverageSceneLuminosity()
{
	FLOAT AverageSceneLuminosity = 0.5f;
	if( DynamicGamma )
	{
		AverageSceneLuminosity = (DynamicGamma->CurMaxL - DynamicGamma->CurMinL) * 0.5f ;
	}
	return AverageSceneLuminosity;
}

//
// For the self-test (UC2GLSelfTest.cpp): run the luminance pass now, whatever the update
// rate, and hand out the target it drew and the histogram's result.
//
FRenderTarget* UC2GL_TestLuminancePass( FOpenGLRenderInterface* RI, FLOAT& MinL, FLOAT& MaxL, FLOAT& AvgL )
{
	if( !RI->DynamicGamma )
		RI->DynamicGamma = new FOpenGLDynamicGamma(RI);
	RI->DynamicGamma->LastUpdateTime = -1000.f;
	RI->DynamicGamma->UpdateLuminanceTarget();
	MinL = RI->DynamicGamma->Histogram.MinL;
	MaxL = RI->DynamicGamma->Histogram.MaxL;
	AvgL = RI->DynamicGamma->Histogram.AvgL;
	return RI->DynamicGamma->LuminanceTarget;
}

//
// Called by the device when the context goes away or post-processing is switched.
//
void UC2GL_DeleteDynamicGamma( FOpenGLRenderInterface* RI )
{
	if( RI->DynamicGamma )
		delete RI->DynamicGamma;
	RI->DynamicGamma = NULL;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
