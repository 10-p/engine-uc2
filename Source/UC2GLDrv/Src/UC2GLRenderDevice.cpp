/*=============================================================================
	UC2GLRenderDevice.cpp: UC2GLDrv.UC2GLRenderDevice.

	From engine-ut2004's OpenGLDrv (OpenGLRenderDevice.cpp): the GL entry point table bound
	through SDL, extension discovery, SetRes through the viewport, resource lists. From UC2's
	D3DDrv (D3DRenderDevice.cpp): the resource caches with 16-bit ids, Flush/FlushResource,
	ResetRenderStates, the default render target, the gamma formula, render caps, the
	hardware workarounds' defaults (CubemapTextureAddressing, IsGeForce, DUMMY_MODULATE2X).
	New: the main framebuffer object and the present pass, GL error checking per frame, and
	a screenshot switch for verification (-glshot=<seconds>,...).
=============================================================================*/

#include "UC2GLDrv.h"
#include "SDL.h"

/*-----------------------------------------------------------------------------
	Globals.
-----------------------------------------------------------------------------*/

// D3DDrv's Voodoo 3 hack: MODULATE2X everywhere else.
D3DTEXTUREOP DUMMY_MODULATE2X = D3DTOP_MODULATE2X;

// OpenGL function pointers.
#define GL_EXT(name) UBOOL UUC2GLRenderDevice::SUPPORTS##name = 0;
#define GL_PROC(ext,ret,func,parms) ret (APIENTRY *UUC2GLRenderDevice::func)parms = NULL;
#include "UC2GLFuncs.h"
#undef GL_EXT
#undef GL_PROC

#define GL_RAW(ret,func,parms) ret (APIENTRY *UUC2GLRenderDevice::Raw_##func)parms = NULL;
#include "UC2GLRawFuncs.h"
#undef GL_RAW

// -glshot=<seconds>[,<seconds>...]: run the engine's SHOT exec at those times after the
// first frame (verification; the files are System/ShotNNNNN.bmp).
static TArray<FLOAT>	GShotTimes;
static TArray<FLOAT>	GPSToggleTimes;
static TArray<FLOAT>	GPPToggleTimes;
static TArray<FLOAT>	GFFToggleTimes;		// O-9: -glfftoggle=<s>,... flips FixedFunctionPrograms
// O-9: COMBINERTEST / -glcombinertest: the numeric test of the texture stages
// (UC2GLCombinerTest.cpp), like GSelfTestPending.
static INT				GCombinerTestPending = 0;
static INT				GMatCensusPending = 0;	// O-9: GLMATCENSUS (UC2GLPixelTrace.cpp)
// -glexec=<seconds>:<command>[@<seconds>:<command>...]: console commands run through the
// viewport at those times after the first frame; '+' stands for a space in a command
// (verification: "-glexec=40:BlurMe@50:set+UCScreenBerserk+Enabled+1"). Times ascending.
static TArray<FLOAT>	GExecTimes;
static TArray<FString>	GExecCommands;
// -glshotramp: screenshots show the dynamic gamma ramp the present pass applies (they are
// read before it otherwise, as D3D's are).
// POSTSELFTEST / -glselftest: the numeric checks of UC2GLSelfTest.cpp, run at the start of a
// frame (0 = no, 1 = at the next frame with a player, n = n frames from now).
static INT				GSelfTestPending = 0;
static UBOOL			GShotRamp = 0;
// -glshotpresented (O-8): -glshot pictures are what the present pass put on the screen (the gamma and
// FXAA applied), read from the default framebuffer before the swap, instead of the main framebuffer.
static UBOOL			GShotPresented = 0;
static TArray<FColor>	GPresentedPixels;
static INT				GPresentedWidth = 0, GPresentedHeight = 0;
static BYTE				GLastGammaRamp[256];
void UC2GL_RememberGammaRamp( const BYTE* Ramp ) { appMemcpy( GLastGammaRamp, Ramp, 256 ); }
static DOUBLE			GFirstFrameTime = 0.0;

/*-----------------------------------------------------------------------------
	UUC2GLRenderDevice.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(UUC2GLRenderDevice);

#define UC2GL_LOG_ONCE_INIT(...) { static UBOOL Logged = 0; if( !Logged ) { Logged = 1; debugf( NAME_Init, __VA_ARGS__ ); } }


// -name=<seconds>,<seconds>,... on the command line.
static void ParseTimeList( const TCHAR* Key, TArray<FLOAT>& Times )
{
	Times.Empty();
	const TCHAR* Found = appStrfind( appCmdLine(), Key );
	if( !Found )
		return;
	Found += appStrlen( Key );
	while( *Found && *Found != ' ' && *Found != '\t' )
	{
		Times.AddItem( appAtof( Found ) );
		while( *Found && *Found != ',' && *Found != ' ' && *Found != '\t' )
			Found++;
		if( *Found == ',' )
			Found++;
	}
}

UUC2GLRenderDevice::UUC2GLRenderDevice()
:	RenderInterface(this)
{
	guard(UUC2GLRenderDevice::UUC2GLRenderDevice);

	ResourceList			= NULL;
	VertexShaders			= NULL;
	DynamicVertexStream		= NULL;
	DynamicIndexBuffer16	= NULL;
	DynamicIndexBuffer32	= NULL;
	DefaultRenderTarget		= NULL;
	LockedViewport			= NULL;
	LastViewport			= NULL;
	FrameCounter			= 0;
	NumTextureUnits			= 0;
	GLFixedFunctionUnits	= 2;
	GLProgramUnits			= 2;
	FFProgramsActive		= 0;
	LogFFPrograms			= 0;
	MaskCombine3			= 0;
	MaskCombine4			= 0;
	FFProgramsLinked = FFProgramsFailed = FFProgramDraws = FFFallbackDraws = 0;
	MaxAnisotropy			= 1.f;
	ValidContext			= 0;
	GLMajorVersion			= 0;
	GLMinorVersion			= 0;
	CubemapTextureAddressing= D3DTADDRESS_CLAMP;
	IsGeForce				= 0;
	MainFramebuffer			= 0;
	MainColorTexture		= 0;
	MainDepthStencil		= 0;
	MainSamples				= 0;
	MainResolveFramebuffer	= MainMSColorRenderbuffer = MainMSDepthRenderbuffer = 0;
	ResolvedColorVersion	= ResolvedDepthVersion = -1;
	PresentFXAA				= 0;
	PresentFXAALoc			= -1;
	MainWidth				= 0;
	MainHeight				= 0;
	PresentProgram			= 0;
	PresentTextureLoc		= -1;
	PresentGammaLoc			= -1;
	PresentGamma			= 1.f;
	PresentBrightness		= 0.5f;
	PresentContrast			= 0.5f;
	ApplyGamma				= 1;
	NullTextureID			= 0;
	PostCopyProgram = PostDepthEncodeProgram = PostDepthCopyProgram = 0;
	PostCopyRectLoc = PostDepthEncodeRectLoc = PostDepthCopyRectLoc = -1;
	SnapshotTexture = SnapshotFramebuffer = DepthColorTexture = DepthColorFramebuffer = 0;
	SnapshotVersion = DepthColorVersion = -1;
	MainColorVersion = MainDepthVersion = 0;
	SnapshotCopies = DepthColorCopies = DepthBufferCopies = 0;
	GammaRampTexture		= 0;
	GammaRampActive			= 0;
	XboxClientDefaultsApplied = 0;
	PresentRampLoc			= -1;
	PresentUseRampLoc		= -1;
	// Epic sets these in XboxViewport.cpp; the engine's render-target helpers multiply by them.
	FSAAScale[0] = FSAAScale[1] = 1.f;
	ProgramsLinked = ProgramsFailed = 0;
	VertexShadersCompiled = VertexShadersFailed = VertexShadersPrecompiled = 0;
	FragmentShadersCompiled = FragmentShadersFailed = 0;
	PrecompileSeen = PrecompileNotApplicable = 0;
	ProgramDraws = ProgramFallbackDraws = 0;
	appMemzero( PixelShaderState, sizeof(PixelShaderState) );
	GLErrorFrames			= 0;
	GLErrorCount			= 0;
	appMemzero( &DeviceCaps, sizeof(DeviceCaps) );
	IsGLES					= 0;
	CapDepthTexture = CapFragDepth = CapUIntIndex = CapTextureMaxLevel = CapFragmentHighp = 1;
	MainDepthRenderbuffer	= 0;
	QuadBuffer				= 0;

	unguard;
}

void UUC2GLRenderDevice::StaticConstructor()
{
	guard(UUC2GLRenderDevice::StaticConstructor);

	new(GetClass(),TEXT("UsePrecaching"),		RF_Public)UBoolProperty	( CPP_PROPERTY(UsePrecaching		), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("UseTrilinear"),		RF_Public)UBoolProperty	( CPP_PROPERTY(UseTrilinear			), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("UseMipmapping"),		RF_Public)UBoolProperty	( CPP_PROPERTY(UseMipmapping		), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("UseVSync"),			RF_Public)UBoolProperty	( CPP_PROPERTY(UseVSync				), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("UseCubemaps"),			RF_Public)UBoolProperty	( CPP_PROPERTY(UseCubemaps			), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("CheckGLErrors"),		RF_Public)UBoolProperty	( CPP_PROPERTY(CheckGLErrors		), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("UsePixelShaders"),		RF_Public)UBoolProperty	( CPP_PROPERTY(UsePixelShaders		), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("FixedFunctionPrograms"),RF_Public)UBoolProperty( CPP_PROPERTY(UseFixedFunctionPrograms), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("PrecompileVertexShaders"),RF_Public)UBoolProperty( CPP_PROPERTY(PrecompileVertexShaders), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("UsePostProcess"),		RF_Public)UBoolProperty	( CPP_PROPERTY(UsePostProcess		), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("DOFShippedBlurAlpha"),	RF_Public)UBoolProperty	( CPP_PROPERTY(DOFShippedBlurAlpha	), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("LevelOfAnisotropy"),	RF_Public)UIntProperty	( CPP_PROPERTY(LevelOfAnisotropy	), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("AntiAliasing"),		RF_Public)UIntProperty	( CPP_PROPERTY(AntiAliasing			), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("MaxTextureUnits"),		RF_Public)UIntProperty	( CPP_PROPERTY(MaxTextureUnits		), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("MaterialStages"),		RF_Public)UIntProperty	( CPP_PROPERTY(MaterialStages		), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("DetailTexMipBias"),	RF_Public)UFloatProperty( CPP_PROPERTY(DetailTexMipBias		), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("DefaultTexMipBias"),	RF_Public)UFloatProperty( CPP_PROPERTY(DefaultTexMipBias	), TEXT("Options"), CPF_Config );

	// Compiled defaults = the PC D3DDrv's Default.ini values, so a missing ini section
	// still gives the PC look (SDL2Drv's lesson: unset config properties are zero).
	UsePrecaching			= 1;
	UseTrilinear			= 1;
	UseMipmapping			= 1;
	UseVSync				= 0;
	UseCubemaps				= 1;
	CheckGLErrors			= 0;
	UsePixelShaders			= 1;
	// O-9: fixed-function texture stages as GLSL (UC2GLFFProgram.cpp).
	UseFixedFunctionPrograms= 1;
	PrecompileVertexShaders	= 1;
	UsePostProcess			= 1;
	DOFShippedBlurAlpha		= 0;
	// O-8 (owner, 2026-10-06): 16x anisotropic filtering and 4x anti-aliasing by default, also
	// for an ini that predates the keys.
	LevelOfAnisotropy		= 16;
	AntiAliasing			= 4;
	MaxTextureUnits			= 8;
	// O-9: UC2's D3DDrv clamps MaxSimultaneousTextures and MaxTextureBlendStages to 4 and
	// refuses any card that has fewer (D3DRenderDevice.cpp, CheckDeviceCaps; Xbox and PC), so
	// its material compiler only ever split materials into passes for 4 stages.
	MaterialStages			= 4;
	DetailTexMipBias		= 0.8f;
	DefaultTexMipBias		= -0.5f;
	DetailTextures			= 1;
	HighDetailActors		= 1;
	SuperHighDetailActors	= 1;
	UseCompressedLightmaps	= 1;
	UseStencil				= 1;
	SupportsCubemaps		= 1;
	SupportsZBIAS			= 1;

	unguard;
}

void UUC2GLRenderDevice::Destroy()
{
	guard(UUC2GLRenderDevice::Destroy);
	Super::Destroy();
	Flush( NULL );

	// M7: the render-target pool dies with this object, and FRenderTargetPool's destructor
	// asserts that no reference is left. The camera effects release theirs when they are
	// destroyed, which at exit can be after the device (on Xbox the game never exits). The
	// targets still held are handed to their holders: no longer the pool's, so their
	// RemovePoolRef does nothing (they are leaked, at exit).
	FRenderTargetPool& Pool = RenderInterface.RenderTargetPool;
	for( INT i=0; i<Pool.RenderTargets.Num(); i++ )
	{
		FRenderTarget* Held = Pool.RenderTargets(i);
		if( Held && Pool.RefCounts(i) > 0 )
		{
			Held->RenderTargetPool	= NULL;
			Held->PoolIndex			= INDEX_NONE;
			Pool.RenderTargets(i)	= NULL;
		}
		Pool.RefCounts(i) = 0;
	}

	if( DefaultRenderTarget )
	{
		delete DefaultRenderTarget;
		DefaultRenderTarget = NULL;
	}
	unguard;
}

/*-----------------------------------------------------------------------------
	GL binding.
-----------------------------------------------------------------------------*/

UBOOL UUC2GLRenderDevice::FindExt( const char* Name )
{
	const char* Extensions = (const char*)glGetString( GL_EXTENSIONS );
	if( !Extensions )
		return 0;
	INT Len = strlen( Name );
	for( const char* At = strstr( Extensions, Name ); At; At = strstr( At + 1, Name ) )
		if( (At == Extensions || At[-1] == ' ') && (At[Len] == ' ' || At[Len] == 0) )
			return 1;
	return 0;
}

void UUC2GLRenderDevice::FindProc( void*& ProcAddress, const char* Name, const char* SupportName, UBOOL& Supports )
{
	ProcAddress = BindingGetProcAddress( Name );
	if( !ProcAddress && Supports )
	{
		debugf( NAME_Init, TEXT("UC2GLDrv: missing function '%s' for '%s'"), appFromAnsi(Name), appFromAnsi(SupportName) );
		Supports = 0;
	}
}

//
// O-9: the stage count and texture-op caps the material compiler (UC2GLMaterialState.cpp)
// is given. Epic's D3DDrv gave it the hardware's MaxSimultaneousTextures /
// MaxTextureBlendStages (clamped to 4, D3DRenderDevice.cpp) and its TextureOpCaps; here:
// - fragment programs (desktop GL, FixedFunctionPrograms): every D3D operation is exact and
//   a pass may sample as many textures as a fragment shader can with fixed-function texture
//   coordinates, up to MaxTextureUnits (8);
// - the texture environment: as many stages as GL_MAX_TEXTURE_UNITS, and the three-operand
//   operations only with combine3 or combine4.
//
void UUC2GLRenderDevice::UpdateTextureCaps()
{
	if( MaxTextureUnits < 1 )
		MaxTextureUnits = MAX_STAGES;
	if( MaterialStages < 1 )
		MaterialStages = 4;
	FFProgramsActive = UseFixedFunctionPrograms && !IsGLES && SUPPORTS_GL2;
	INT Units = FFProgramsActive ? GLProgramUnits : GLFixedFunctionUnits;
	NumTextureUnits = Clamp<INT>( Min<INT>( Min<INT>( Units, MaxTextureUnits ), MaterialStages ), 2, MAX_STAGES );
	DeviceCaps.MaxSimultaneousTextures	= NumTextureUnits;
	DeviceCaps.MaxTextureBlendStages	= NumTextureUnits;
	DeviceCaps.TextureOpCaps			= (FFProgramsActive || HasCombine3() || HasCombine4()) ? (D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR | D3DTEXOPCAPS_MULTIPLYADD) : 0;
	DeviceCaps.PrimitiveMiscCaps		= 0;	// no D3DPMISCCAPS_TSSARGTEMP: GL combiners have no temp register (the terrain path that wants it is not reached)
	debugf( NAME_Init, TEXT("UC2GLDrv: fixed-function materials: %s; %i texture stages (MaterialStages %i, %i on this path), three-operand operations %s"),
		FFProgramsActive ? TEXT("fragment programs (FixedFunctionPrograms=True)") : IsGLES ? TEXT("texture environment (GL4ES generates its shaders)") : TEXT("texture environment (FixedFunctionPrograms=False)"),
		NumTextureUnits, MaterialStages, Min<INT>( Units, MaxTextureUnits ),
		FFProgramsActive ? TEXT("exact") : HasCombine3() ? TEXT("through combine3") : HasCombine4() ? TEXT("through combine4") : TEXT("NOT available (approximated)") );
}

void UUC2GLRenderDevice::FindProcs()
{
	guard(UUC2GLRenderDevice::FindProcs);

	// glGetString first: version and extensions decide what the rest is.
	SUPPORTS_GL = 1;
	FindProc( *(void**)&glGetString, "glGetString", "_GL", SUPPORTS_GL );
	if( !glGetString )
		appErrorf( TEXT("UC2GLDrv: no glGetString") );

	const char* Version = (const char*)glGetString( GL_VERSION );
	GLMajorVersion = GLMinorVersion = 0;
	if( Version )
		sscanf( Version, "%d.%d", &GLMajorVersion, &GLMinorVersion );
	INT VersionNumber = GLMajorVersion * 10 + GLMinorVersion;

	SUPPORTS_GL2	= VersionNumber >= 20;
	SUPPORTS_FBO	= VersionNumber >= 30 || FindExt("GL_ARB_framebuffer_object");
	SUPPORTS_GL_EXT_texture_compression_s3tc	= FindExt("GL_EXT_texture_compression_s3tc");
	SUPPORTS_GL_EXT_texture_filter_anisotropic	= FindExt("GL_EXT_texture_filter_anisotropic") || FindExt("GL_ARB_texture_filter_anisotropic");
	SUPPORTS_GL_ATI_texture_env_combine3		= FindExt("GL_ATI_texture_env_combine3");
	SUPPORTS_GL_NV_texture_env_combine4			= FindExt("GL_NV_texture_env_combine4");
	SUPPORTS_GL_ARB_texture_env_crossbar		= VersionNumber >= 14 || FindExt("GL_ARB_texture_env_crossbar");
	SUPPORTS_GL_ARB_texture_non_power_of_two	= VersionNumber >= 20 || FindExt("GL_ARB_texture_non_power_of_two");
	SUPPORTS_GL_EXT_packed_depth_stencil		= VersionNumber >= 30 || FindExt("GL_EXT_packed_depth_stencil");
	SUPPORTS_MSAA								= VersionNumber >= 30 || FindExt("GL_ARB_framebuffer_object");

	#define GL_EXT(name)
	#define GL_PROC(ext,ret,func,parms) FindProc( *(void**)&func, #func, #ext, SUPPORTS##ext );
	#include "UC2GLFuncs.h"
	#undef GL_EXT
	#undef GL_PROC

	// The library under the binding.
	#define GL_RAW(ret,func,parms) *(void**)&Raw_##func = BindingGetRawProcAddress( #func );
	#include "UC2GLRawFuncs.h"
	#undef GL_RAW

	unguard;
}

//
// The hardware's own extension list. WebGL names its extensions without the GL_ prefix and
// with its own vendor part (WEBGL_depth_texture for OES_depth_texture): callers ask for each
// spelling they accept.
//
UBOOL UUC2GLRenderDevice::FindRawExt( const char* Name )
{
	const char* Extensions = Raw_glGetString ? (const char*)Raw_glGetString( GL_EXTENSIONS ) : NULL;
	if( !Extensions )
		return 0;
	INT Len = strlen( Name );
	for( const char* At = strstr( Extensions, Name ); At; At = strstr( At + 1, Name ) )
		if( (At == Extensions || At[-1] == ' ') && (At[Len] == ' ' || At[Len] == 0) )
			return 1;
	return 0;
}

/*-----------------------------------------------------------------------------
	The desktop binding: OpenGL through SDL (UC2GL4ESDrv overrides all of these).
-----------------------------------------------------------------------------*/

const TCHAR* UUC2GLRenderDevice::BindingName()
{
	return TEXT("desktop GL through SDL");
}

void UUC2GLRenderDevice::BindingSetContextAttributes()
{
#if UC2GL_HAS_DESKTOP_BINDING
	// The window's own framebuffer only receives the present pass.
	SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 0 );
	SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 0 );
	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
	// The platform's default profile (compatibility): the fixed-function pipeline is what R1
	// draws with. Said explicitly because the attributes are process-wide and the GL4ES
	// binding asks for an ES profile.
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, 0 );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 2 );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 1 );
#endif
}

UBOOL UUC2GLRenderDevice::BindingCreateContext( UViewport* Viewport, UBOOL Fullscreen, INT NewX, INT NewY, UBOOL bSaveSize )
{
#if UC2GL_HAS_DESKTOP_BINDING
	// SDL2Drv creates the GL window and context for BLIT_OpenGL.
	return Viewport->ResizeViewport( (Fullscreen ? BLIT_Fullscreen : 0) | BLIT_OpenGL, NewX, NewY, bSaveSize );
#else
	debugf( NAME_Init, TEXT("UC2GLDrv: this platform has no desktop OpenGL; use UC2GL4ESDrv.UC2GL4ESRenderDevice") );
	return 0;
#endif
}

UBOOL UUC2GLRenderDevice::BindingInit()
{
	return 1;
}

void* UUC2GLRenderDevice::BindingGetProcAddress( const char* Name )
{
#if UC2GL_HAS_DESKTOP_BINDING
	return SDL_GL_GetProcAddress( Name );
#else
	return NULL;
#endif
}

void* UUC2GLRenderDevice::BindingGetRawProcAddress( const char* Name )
{
	return BindingGetProcAddress( Name );
}

UBOOL UUC2GLRenderDevice::BindingContextAlive()
{
	return SDL_GL_GetCurrentContext() != NULL;
}

void UUC2GLRenderDevice::BindingGetDrawableSize( UViewport* Viewport, INT& Width, INT& Height )
{
	SDL_Window* Window = (SDL_Window*)Viewport->GetWindow();
	if( Window )
		SDL_GL_GetDrawableSize( Window, &Width, &Height );
}

void UUC2GLRenderDevice::BindingSetSwapInterval( INT Interval )
{
	SDL_GL_SetSwapInterval( Interval );
}

void UUC2GLRenderDevice::BindingSwap( UViewport* Viewport )
{
	SDL_GL_SwapWindow( (SDL_Window*)Viewport->GetWindow() );
}

//
// The first lines of every GLSL source the driver compiles. All of them are written in the
// common subset of GLSL 1.20 and GLSL ES 1.00 (explicit attributes, varyings and uniforms, no
// fixed-function built-ins). On GLES the default precision is highp where the hardware has
// it in fragment shaders, because the post-process shaders rebuild 24-bit depth values and
// address 1280-texel targets; gl4es replaces these lines by the same choice of its own.
//
const ANSICHAR* UUC2GLRenderDevice::ShaderPrologue( UBOOL Fragment )
{
	// UC2_HIGHP: for the samplers that read depth. A sampler's default precision in GLSL ES
	// is lowp, which Mesa computes in 16-bit floats (a 24-bit depth came back with 11 bits:
	// self-test "depth-color" on the twin) and which mobile hardware may honour as well.
	if( !IsGLES )
		return "#version 120\n#define UC2_HIGHP\n";
	if( Fragment && !CapFragmentHighp )
		return "#version 100\nprecision mediump float;\n#define UC2_HIGHP mediump\n";
	return "#version 100\nprecision highp float;\n#define UC2_HIGHP highp\n";
}

UBOOL UUC2GLRenderDevice::ContextAlive()
{
	return ValidContext && BindingContextAlive();
}

//
// Drain glGetError; returns whether there was any. Used per frame with CheckGLErrors.
//
UBOOL UUC2GLRenderDevice::CheckGLError( const TCHAR* Tag, UBOOL Log )
{
	UBOOL Any = 0;
	GLenum Error;
	// M11: on GLES the library under gl4es is asked as well. gl4es answers glGetError from
	// its own record of what IT rejected; an error OpenGL ES raised for a call gl4es passed
	// on stays in the hardware's error flag and would never be seen.
	while( (Error = glGetError()) != GL_NO_ERROR || (IsGLES && Raw_glGetError && (Error = Raw_glGetError()) != GL_NO_ERROR) )
	{
		Any = 1;
		GLErrorCount++;
		if( Log && GLErrorCount <= 200 )
		{
			const TCHAR* Name =	Error == GL_INVALID_ENUM		? TEXT("GL_INVALID_ENUM") :
								Error == GL_INVALID_VALUE		? TEXT("GL_INVALID_VALUE") :
								Error == GL_INVALID_OPERATION	? TEXT("GL_INVALID_OPERATION") :
								Error == GL_OUT_OF_MEMORY		? TEXT("GL_OUT_OF_MEMORY") :
								Error == GL_INVALID_FRAMEBUFFER_OPERATION ? TEXT("GL_INVALID_FRAMEBUFFER_OPERATION") :
								Error == GL_STACK_OVERFLOW		? TEXT("GL_STACK_OVERFLOW") :
								Error == GL_STACK_UNDERFLOW		? TEXT("GL_STACK_UNDERFLOW") :
																  TEXT("unknown");
			debugf( NAME_Warning, TEXT("UC2GLDrv: GL error 0x%04x (%s) in frame %i [%s]"), (INT)Error, Name, FrameCounter, Tag );
		}
		if( GLErrorCount > 100000 )
			break;
	}
	return Any;
}

/*-----------------------------------------------------------------------------
	Init/Exit/SetRes.
-----------------------------------------------------------------------------*/

UBOOL UUC2GLRenderDevice::Init()
{
	guard(UUC2GLRenderDevice::Init);

	debugf( NAME_Init, TEXT("Initializing UC2GLDrv (class %s; binding: %s)"), GetClass()->GetPathName(), BindingName() );

	// The engine's conventions stay D3D's (BGRA colours, D3D canvas pixel offset); the
	// driver converts (see UC2GLRenderInterface.cpp).
	GIsOpenGL		= 0;

	SupportsCubemaps	= UseCubemaps;
	SupportsZBIAS		= 1;

	if( !DefaultRenderTarget )
		DefaultRenderTarget = new FOpenGLDefaultRenderTarget(this);
	RenderInterface.SavedRenderTargetState.GetCurrentState()->RenderTarget = DefaultRenderTarget;

	ScratchBuffer.Empty();
	ScratchBuffer.Add( 65536 );

	// Verification switches.
	CheckGLErrors |= ParseParam( appCmdLine(), TEXT("glerrors") );
	// (Parse() stops at a comma, so a list of times is read by hand.)
	ParseTimeList( TEXT("glshot="), GShotTimes );

	// A/B switches for the programmable materials (M6): the command line wins over the ini,
	// and -glpstoggle=<s>,... flips UsePixelShaders at those times (seconds after the first
	// frame, like -glshot), so one run can take both pictures from the same camera.
	if( ParseParam( appCmdLine(), TEXT("nopixelshaders") ) )
		UsePixelShaders = 0;
	if( ParseParam( appCmdLine(), TEXT("pixelshaders") ) )
		UsePixelShaders = 1;
	ParseTimeList( TEXT("glpstoggle="), GPSToggleTimes );
	// O-9: -glffprograms / -glnoffprograms (FixedFunctionPrograms), -glunits=N (MaxTextureUnits),
	// -glnocombine3 / -glnocombine4 (masks: the texture environment as on a GL without them,
	// e.g. Mesa behaving like the NVIDIA driver), -glffsource (log every generated program),
	// -glcombinertest (the numeric test of the texture stages, UC2GLCombinerTest.cpp).
	if( ParseParam( appCmdLine(), TEXT("glnoffprograms") ) )
		UseFixedFunctionPrograms = 0;
	if( ParseParam( appCmdLine(), TEXT("glffprograms") ) )
		UseFixedFunctionPrograms = 1;
	Parse( appCmdLine(), TEXT("glunits="), MaxTextureUnits );
	Parse( appCmdLine(), TEXT("glstages="), MaterialStages );
	MaskCombine3	= ParseParam( appCmdLine(), TEXT("glnocombine3") );
	MaskCombine4	= ParseParam( appCmdLine(), TEXT("glnocombine4") );
	LogFFPrograms	= ParseParam( appCmdLine(), TEXT("glffsource") );
	if( ParseParam( appCmdLine(), TEXT("glcombinertest") ) )
		GCombinerTestPending = 10;
	ParseTimeList( TEXT("glfftoggle="), GFFToggleTimes );

	// The same for render-target post-processing (M7): -nopostprocess / -postprocess, and
	// -glpptoggle=<s>,... flips UsePostProcess at those times (console: POSTPROCESS [0|1]).
	if( ParseParam( appCmdLine(), TEXT("nopostprocess") ) )
		UsePostProcess = 0;
	if( ParseParam( appCmdLine(), TEXT("postprocess") ) )
		UsePostProcess = 1;
	ParseTimeList( TEXT("glpptoggle="), GPPToggleTimes );
	GShotRamp = ParseParam( appCmdLine(), TEXT("glshotramp") );
	GShotPresented = ParseParam( appCmdLine(), TEXT("glshotpresented") );
	NoLODBias = ParseParam( appCmdLine(), TEXT("glnolodbias") );
	// O-8: -glaa=N (samples; 0 = off), -glnoaa. Tests that compare pictures run with -glnoaa.
	Parse( appCmdLine(), TEXT("glaa="), AntiAliasing );
	if( ParseParam( appCmdLine(), TEXT("glnoaa") ) )
		AntiAliasing = 0;
	LogStats = ParseParam( appCmdLine(), TEXT("glstats") );
	StatDraws = StatPrimitives = StatMaterials = StatBufferUploads = StatBufferBytes = StatTextureUploads = StatPostQuads = 0;
	StatSnapshots = StatDepthColors = StatDepthCopies = StatFrame0 = 0;
	StatTime0 = 0.0;
	CheckGLErrorsPerDraw = ParseParam( appCmdLine(), TEXT("glerrorsdraw") );
	CheckGLErrors |= CheckGLErrorsPerDraw;
	if( ParseParam( appCmdLine(), TEXT("glselftest") ) )
		GSelfTestPending = 10;
	GExecTimes.Empty();
	GExecCommands.Empty();
	const TCHAR* ExecList = appStrfind( appCmdLine(), TEXT("glexec=") );
	if( ExecList )
	{
		ExecList += 7;
		while( *ExecList && *ExecList != ' ' && *ExecList != '\t' )
		{
			FLOAT Time = appAtof( ExecList );
			while( *ExecList && *ExecList != ':' && *ExecList != ' ' )
				ExecList++;
			if( *ExecList != ':' )
				break;
			ExecList++;
			FString Command;
			while( *ExecList && *ExecList != '@' && *ExecList != ' ' && *ExecList != '\t' )
			{
				Command += FString::Printf( TEXT("%c"), *ExecList == '+' ? ' ' : *ExecList );
				ExecList++;
			}
			GExecTimes.AddItem( Time );
			new(GExecCommands) FString( Command );
			if( *ExecList == '@' )
				ExecList++;
		}
	}


	return 1;

	unguard;
}

void UUC2GLRenderDevice::Exit( UViewport* Viewport )
{
	guard(UUC2GLRenderDevice::Exit);

	if( CheckGLErrors )
		debugf( NAME_Init, TEXT("UC2GLDrv: %i GL errors in %i frames (%i frames with errors)"), GLErrorCount, FrameCounter, GLErrorFrames );

	LastViewport = NULL;
	Flush( Viewport );
	if( ContextAlive() )
	{
		DestroyMainFramebuffer();
		if( PresentProgram )
			glDeleteProgram( PresentProgram );
		PresentProgram = 0;
		LogPixelShaderStats();
		// M7: what the interface holds of the post-process path goes while the device can
		// still flush the render targets.
		extern void UC2GL_DeleteDynamicGamma( FOpenGLRenderInterface* RI );
		UC2GL_DeleteDynamicGamma( &RenderInterface );
		if( RenderInterface.BackBufferCopyTarget && RenderInterface.BackBufferCopyTarget != DefaultRenderTarget )
			RenderInterface.BackBufferCopyTarget->RemovePoolRef();
		RenderInterface.BackBufferCopyTarget = NULL;
		ExitPostProcess();
		ExitPixelShaders();
		extern void UC2GL_LogFFStats( UUC2GLRenderDevice* );
		UC2GL_LogFFStats( this );
		extern void UC2GL_FlushFixedFunctionPrograms( UUC2GLRenderDevice* );
		UC2GL_FlushFixedFunctionPrograms( this );
		if( NullTextureID )
			glDeleteTextures( 1, &NullTextureID );
		NullTextureID = 0;
		if( QuadBuffer )
			glDeleteBuffers( 1, &QuadBuffer );
		QuadBuffer = 0;
	}
	ValidContext = 0;

	unguard;
}

UBOOL UUC2GLRenderDevice::SetRes( UViewport* Viewport, INT NewX, INT NewY, UBOOL Fullscreen, INT ColorBytes, UBOOL bSaveSize )
{
	guard(UUC2GLRenderDevice::SetRes);

	if( LockedViewport )
		appErrorf( TEXT("Can't change resolution while render device is locked!") );

	debugf( NAME_Init, TEXT("UC2GLDrv: SetRes %ix%i %s"), NewX, NewY, Fullscreen ? TEXT("fullscreen") : TEXT("windowed") );

	BindingSetContextAttributes();
	if( !BindingCreateContext( Viewport, Fullscreen, NewX, NewY, bSaveSize ) )
	{
		debugf( NAME_Init, TEXT("UC2GLDrv: the viewport could not provide an OpenGL context (%s)"), BindingName() );
		return 0;
	}

	if( !ValidContext )
	{
		if( !BindingInit() )
			return 0;
		FindProcs();

		debugf( NAME_Init, TEXT("GL_VENDOR     : %s"), appFromAnsi((ANSICHAR*)glGetString(GL_VENDOR)) );
		debugf( NAME_Init, TEXT("GL_RENDERER   : %s"), appFromAnsi((ANSICHAR*)glGetString(GL_RENDERER)) );
		debugf( NAME_Init, TEXT("GL_VERSION    : %s"), appFromAnsi((ANSICHAR*)glGetString(GL_VERSION)) );
		if( glGetString(GL_SHADING_LANGUAGE_VERSION) )
			debugf( NAME_Init, TEXT("GL_SHADING_LANGUAGE_VERSION : %s"), appFromAnsi((ANSICHAR*)glGetString(GL_SHADING_LANGUAGE_VERSION)) );

		if( !SUPPORTS_GL || !SUPPORTS_GL2 || !SUPPORTS_FBO )
		{
			debugf( NAME_Init, TEXT("UC2GLDrv: needs OpenGL 2.0 with framebuffer objects (GL %s, GLSL %s, FBO %s)"), SUPPORTS_GL ? TEXT("yes") : TEXT("no"), SUPPORTS_GL2 ? TEXT("yes") : TEXT("no"), SUPPORTS_FBO ? TEXT("yes") : TEXT("no") );
			return 0;
		}
		ValidContext = 1;

		appSprintf( GMachineVideo, TEXT("%s"), appFromAnsi((ANSICHAR*)glGetString(GL_RENDERER)) );

		DetectCapabilities();
		if( IsGLES )
			PrepareGL4ESTextureZero();
		if( CheckGLErrors ) CheckGLError( TEXT("SetRes: binding and capabilities") );

		// Capabilities.
		GLint Units = 0, MaxSize = 0, Coords = 0, ImageUnits = 0;
		glGetIntegerv( GL_MAX_TEXTURE_UNITS, &Units );
		glGetIntegerv( GL_MAX_TEXTURE_SIZE, &MaxSize );
		if( !IsGLES )
		{
			glGetIntegerv( GL_MAX_TEXTURE_COORDS, &Coords );
			glGetIntegerv( GL_MAX_TEXTURE_IMAGE_UNITS, &ImageUnits );
		}
		GLFixedFunctionUnits	= Clamp<INT>( Units, 2, MAX_STAGES );
		GLProgramUnits			= IsGLES ? GLFixedFunctionUnits : Clamp<INT>( Min<INT>( Coords, ImageUnits ), GLFixedFunctionUnits, MAX_STAGES );
		UpdateTextureCaps();
		debugf( NAME_Init, TEXT("UC2GLDrv: texture units: %i in the texture environment (GL_MAX_TEXTURE_UNITS), %i for fragment programs (GL_MAX_TEXTURE_COORDS %i, GL_MAX_TEXTURE_IMAGE_UNITS %i); combine3 %s, combine4 %s"),
			Units, GLProgramUnits, Coords, ImageUnits,
			SUPPORTS_GL_ATI_texture_env_combine3 ? (MaskCombine3 ? TEXT("masked") : TEXT("yes")) : TEXT("no"),
			SUPPORTS_GL_NV_texture_env_combine4 ? (MaskCombine4 ? TEXT("masked") : TEXT("yes")) : TEXT("no") );
		DeviceCaps.MaxTextureSize			= MaxSize > 0 ? MaxSize : 2048;
		if( SUPPORTS_GL_EXT_texture_filter_anisotropic )
			glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &MaxAnisotropy );
		LevelOfAnisotropy = Clamp<INT>( LevelOfAnisotropy, 1, Max<INT>(1,(INT)MaxAnisotropy) );

		debugf( NAME_Init, TEXT("UC2GLDrv: %i texture units (GL reports %i), max texture %i, S3TC %s, combine3 %s, anisotropy %i of %i"),
			NumTextureUnits, Units, DeviceCaps.MaxTextureSize,
			SUPPORTS_GL_EXT_texture_compression_s3tc ? TEXT("yes") : TEXT("no"),
			HasCombine3() ? TEXT("yes") : TEXT("no"),
			LevelOfAnisotropy, (INT)MaxAnisotropy );
		if( !SUPPORTS_GL_EXT_texture_compression_s3tc )
			debugf( NAME_Init, TEXT("UC2GLDrv: no S3TC; DXT textures are decompressed on upload") );
		// O-8: multisampling where the main framebuffer can be multisampled (desktop GL),
		// otherwise FXAA in the present pass (OpenGL ES 2 / WebGL 1 have no multisampled
		// framebuffer objects; the context's own antialias attribute only reaches the default
		// framebuffer, which receives nothing but the present quad).
		PresentFXAA = AntiAliasing > 0 && (IsGLES || !SUPPORTS_MSAA);
		debugf( NAME_Init, TEXT("UC2GLDrv: anti-aliasing: %s"),
			AntiAliasing <= 0 ? TEXT("off") : PresentFXAA ? TEXT("FXAA in the present pass (no multisampled framebuffers on this context)") : *FString::Printf( TEXT("%ix multisampled main framebuffer, resolved before it is read"), AntiAliasing ) );

		SupportsCubemaps = UseCubemaps;

		if( !CreatePresentProgram() )
			return 0;
		if( CheckGLErrors ) CheckGLError( TEXT("SetRes: present program") );

		// A black opaque texture for stages that have no texture.
		FColor Black(0,0,0,255);
		glGenTextures( 1, &NullTextureID );
		glBindTexture( GL_TEXTURE_2D, NullTextureID );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_BGRA, GL_UNSIGNED_BYTE, &Black );

		// The full-screen strip of the present and copy passes.
		static const FLOAT Quad[8] = { -1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f };
		glGenBuffers( 1, &QuadBuffer );
		glBindBuffer( GL_ARRAY_BUFFER, QuadBuffer );
		glBufferData( GL_ARRAY_BUFFER, sizeof(Quad), Quad, GL_STATIC_DRAW );
		glBindBuffer( GL_ARRAY_BUFFER, 0 );

		if( CheckGLErrors ) CheckGLError( TEXT("SetRes: device textures and buffers") );

		// Render-target post-processing (M7): the copy programs. Before the pixel shaders:
		// which of those are compiled depends on it.
		InitPostProcess();
		if( CheckGLErrors ) CheckGLError( TEXT("SetRes: post-process programs") );

		// Programmable materials (M6): fragment shaders of every integrated pixel shader,
		// vertex shaders of the VertexShaderConfigs.ini list.
		InitPixelShaders();
		if( CheckGLErrors ) CheckGLError( TEXT("SetRes: pixel and vertex shaders") );
	}

	if( !CreateMainFramebuffer( Viewport->SizeX, Viewport->SizeY ) )
		return 0;
	if( CheckGLErrors ) CheckGLError( TEXT("SetRes: main framebuffer") );


	BindingSetSwapInterval( UseVSync ? 1 : 0 );

	ResetRenderStates();
	UpdateGamma( Viewport );

	Viewport->PendingFrame = 0;

	CheckGLError( TEXT("SetRes") );

	return 1;

	unguard;
}

/*-----------------------------------------------------------------------------
	Main framebuffer and present pass.
-----------------------------------------------------------------------------*/

UBOOL UUC2GLRenderDevice::CreateMainFramebuffer( INT Width, INT Height )
{
	guard(UUC2GLRenderDevice::CreateMainFramebuffer);

	Width	= Max( Width, 1 );
	Height	= Max( Height, 1 );
	if( MainFramebuffer && Width == MainWidth && Height == MainHeight )
		return 1;

	DestroyMainFramebuffer();

	glGenTextures( 1, &MainColorTexture );
	glBindTexture( GL_TEXTURE_2D, MainColorTexture );
	// (FXAA samples between texels; every other reader samples texel centres, the same either way.)
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, PresentFXAA ? GL_LINEAR : GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, PresentFXAA ? GL_LINEAR : GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0 );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, Width, Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
	if( CheckGLErrors ) CheckGLError( TEXT("main framebuffer: colour texture") );

	// M7: depth and stencil live in a texture, not a renderbuffer, so the depth can be read
	// by a shader (UUC2GLRenderDevice::UpdateSampledTexture, depth of field). GL 3.0 /
	// EXT_packed_depth_stencil + ARB_depth_texture; on WebGL this needs WEBGL_depth_texture
	// (M11: without it the depth colour has to come from a pass that writes depth as colour).
	// M11: where the context has no depth textures (CapDepthTexture) it is a renderbuffer,
	// and the two readers of depth take their fallbacks (UC2GLPostProcess.cpp).
	if( CapDepthTexture )
		MainDepthStencil = CreateDepthStencilTexture( Width, Height );
	else
		MainDepthRenderbuffer = CreateDepthStencilRenderbuffer( Width, Height );
	if( CheckGLErrors ) CheckGLError( TEXT("main framebuffer: depth-stencil storage") );

	glGenFramebuffers( 1, &MainFramebuffer );
	if( CheckGLErrors ) CheckGLError( TEXT("main framebuffer: glGenFramebuffers") );
	glBindFramebuffer( GL_FRAMEBUFFER, MainFramebuffer );
	if( CheckGLErrors ) CheckGLError( TEXT("main framebuffer: glBindFramebuffer") );
	AttachColorTexture( MainColorTexture );
	if( CheckGLErrors ) CheckGLError( TEXT("main framebuffer: colour attachment") );
	AttachMainDepthStencil( MainFramebuffer );
	if( CheckGLErrors ) CheckGLError( TEXT("main framebuffer: attachments") );

	GLenum Status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
	if( Status != GL_FRAMEBUFFER_COMPLETE )
	{
		debugf( NAME_Init, TEXT("UC2GLDrv: main framebuffer %ix%i incomplete (0x%04x)"), Width, Height, Status );
		return 0;
	}

	// O-8: the multisampled main framebuffer; the one built above becomes its resolve.
	MainSamples = 0;
	if( !IsGLES && !PresentFXAA && AntiAliasing > 1 && SUPPORTS_MSAA && MainDepthStencil )
	{
		GLint MaxSamples = 0;
		glGetIntegerv( GL_MAX_SAMPLES, &MaxSamples );
		INT Samples = Min<INT>( AntiAliasing, MaxSamples );
		if( Samples > 1 )
		{
			MainResolveFramebuffer = MainFramebuffer;
			glGenRenderbuffers( 1, &MainMSColorRenderbuffer );
			glBindRenderbuffer( GL_RENDERBUFFER, MainMSColorRenderbuffer );
			glRenderbufferStorageMultisample( GL_RENDERBUFFER, Samples, GL_RGBA8, Width, Height );
			glGenRenderbuffers( 1, &MainMSDepthRenderbuffer );
			glBindRenderbuffer( GL_RENDERBUFFER, MainMSDepthRenderbuffer );
			glRenderbufferStorageMultisample( GL_RENDERBUFFER, Samples, GL_DEPTH24_STENCIL8, Width, Height );
			glBindRenderbuffer( GL_RENDERBUFFER, 0 );
			glGenFramebuffers( 1, &MainFramebuffer );
			glBindFramebuffer( GL_FRAMEBUFFER, MainFramebuffer );
			glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, MainMSColorRenderbuffer );
			glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, MainMSDepthRenderbuffer );
			glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, MainMSDepthRenderbuffer );
			GLenum MSStatus = glCheckFramebufferStatus( GL_FRAMEBUFFER );
			if( MSStatus == GL_FRAMEBUFFER_COMPLETE )
				MainSamples = Samples;
			else
			{
				debugf( NAME_Init, TEXT("UC2GLDrv: %ix multisampled main framebuffer incomplete (0x%04x): no anti-aliasing"), Samples, MSStatus );
				glBindFramebuffer( GL_FRAMEBUFFER, MainResolveFramebuffer );
				glDeleteFramebuffers( 1, &MainFramebuffer );
				glDeleteRenderbuffers( 1, &MainMSColorRenderbuffer );
				glDeleteRenderbuffers( 1, &MainMSDepthRenderbuffer );
				MainFramebuffer = MainResolveFramebuffer;
				MainResolveFramebuffer = MainMSColorRenderbuffer = MainMSDepthRenderbuffer = 0;
			}
		}
	}
	ResolvedColorVersion = ResolvedDepthVersion = -1;

	MainWidth	= Width;
	MainHeight	= Height;
	debugf( NAME_Init, TEXT("UC2GLDrv: main framebuffer %ix%i (RGBA8 texture, depth 24 + stencil 8 %s%s)"), Width, Height, MainDepthStencil ? TEXT("texture") : TEXT("renderbuffer"),
		MainSamples ? *FString::Printf( TEXT("; drawn %ix multisampled, resolved into them"), MainSamples ) : TEXT("") );
	MainColorVersion++;
	MainDepthVersion++;

	// The engine's back buffer target now stands for this framebuffer.
	if( DefaultRenderTarget )
		DefaultRenderTarget->UpdateSurfaces( Width, Height );

	glClearColor( 0.f, 0.f, 0.f, 1.f );
	glClearDepth( 1.0 );
	glClearStencil( 0 );
	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	glDepthMask( GL_TRUE );
	glStencilMask( 0xff );
	glDisable( GL_SCISSOR_TEST );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
	glEnable( GL_SCISSOR_TEST );

	RenderInterface.InvalidateHardwareState();
	return 1;

	unguard;
}

//
// O-8: the multisampled main framebuffer's colour and/or depth-stencil into MainColorTexture /
// MainDepthStencil, if anything was drawn since the last resolve. Leaves GL_FRAMEBUFFER bound
// to the interface's current target. (Scissor off for the blit: it applies to blits.)
//
void UUC2GLRenderDevice::ResolveMain( UBOOL Color, UBOOL Depth )
{
	if( !MainSamples )
		return;
	GLbitfield Mask = 0;
	if( Color && ResolvedColorVersion != MainColorVersion )
		Mask |= GL_COLOR_BUFFER_BIT;
	if( Depth && ResolvedDepthVersion != MainDepthVersion )
		Mask |= GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
	if( !Mask )
		return;
	glBindFramebuffer( GL_READ_FRAMEBUFFER, MainFramebuffer );
	glBindFramebuffer( GL_DRAW_FRAMEBUFFER, MainResolveFramebuffer );
	glDisable( GL_SCISSOR_TEST );
	glBlitFramebuffer( 0, 0, MainWidth, MainHeight, 0, 0, MainWidth, MainHeight, Mask, GL_NEAREST );
	glEnable( GL_SCISSOR_TEST );
	GLuint Current = RenderInterface.SavedRenderTargetState.GetCurrentState()->Framebuffer;
	glBindFramebuffer( GL_FRAMEBUFFER, Current ? Current : MainFramebuffer );
	if( Mask & GL_COLOR_BUFFER_BIT )
		ResolvedColorVersion = MainColorVersion;
	if( Mask & GL_DEPTH_BUFFER_BIT )
		ResolvedDepthVersion = MainDepthVersion;
}

void UUC2GLRenderDevice::DestroyMainFramebuffer()
{
	// The snapshot and the depth colour have the main framebuffer's size.
	DestroyPostProcessTargets();
	if( !ContextAlive() )
	{
		MainFramebuffer = MainDepthStencil = MainDepthRenderbuffer = MainColorTexture = 0;
		MainResolveFramebuffer = MainMSColorRenderbuffer = MainMSDepthRenderbuffer = 0;
		MainSamples = 0;
		MainWidth = MainHeight = 0;
		return;
	}
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	if( MainFramebuffer )
		glDeleteFramebuffers( 1, &MainFramebuffer );
	if( MainResolveFramebuffer )
		glDeleteFramebuffers( 1, &MainResolveFramebuffer );
	if( MainMSColorRenderbuffer )
		glDeleteRenderbuffers( 1, &MainMSColorRenderbuffer );
	if( MainMSDepthRenderbuffer )
		glDeleteRenderbuffers( 1, &MainMSDepthRenderbuffer );
	MainResolveFramebuffer = MainMSColorRenderbuffer = MainMSDepthRenderbuffer = 0;
	MainSamples = 0;
	if( MainDepthStencil )
		glDeleteTextures( 1, &MainDepthStencil );
	DeleteDepthStencilRenderbuffer( MainDepthRenderbuffer );
	if( MainColorTexture )
		glDeleteTextures( 1, &MainColorTexture );
	MainFramebuffer = MainDepthStencil = MainColorTexture = 0;
	MainWidth = MainHeight = 0;
}

/*-----------------------------------------------------------------------------
	Depth-stencil storage (M11).

	Desktop GL: a GL_DEPTH24_STENCIL8 texture or renderbuffer on the depth and the stencil
	attachment points, as since M7.

	GLES2 behind gl4es: done with the raw library, around gl4es, because gl4es decides from
	the OpenGL ES extension names (OES_depth_texture, OES_packed_depth_stencil, OES_depth24)
	which WebGL 1 does not have under those names, and then gives a depth attachment a 16-bit
	renderbuffer plus a separate stencil renderbuffer, a combination WebGL 1 rejects. What
	WebGL 1 has: the DEPTH_STENCIL renderbuffer format in core, and with WEBGL_depth_texture a
	DEPTH_STENCIL / UNSIGNED_INT_24_8 texture; either goes on the one DEPTH_STENCIL_ATTACHMENT
	point. OpenGL ES 2 proper (the native twin) has the same storage through
	OES_packed_depth_stencil (+ OES_depth_texture) but no combined attachment point: the image
	goes on both points. That attachment call is the one place where the twin and the browser
	run different lines.

	gl4es caches texture and renderbuffer bindings, so the raw calls put back what was bound.
	The texture NAME is gl4es' (it tracks names, and the texture is later bound for sampling
	through it); renderbuffers are raw throughout (the driver uses no other renderbuffers).
-----------------------------------------------------------------------------*/

GLuint UUC2GLRenderDevice::CreateDepthStencilTexture( INT Width, INT Height )
{
	GLuint Texture = 0;
	glGenTextures( 1, &Texture );
	if( !IsGLES )
	{
		glBindTexture( GL_TEXTURE_2D, Texture );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0 );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, Width, Height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL );
		return Texture;
	}
	// gl4es' own record of the texture first: a one-texel storage (it only binds a texture
	// whose storage it has seen) with the sampling state the copy passes want, so that it has
	// nothing to "correct" when the texture is bound for one.
	static const BYTE Texel[4] = { 255, 255, 255, 255 };
	glBindTexture( GL_TEXTURE_2D, Texture );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, Texel );
	// Then the real storage.
	GLint Bound = 0;
	Raw_glGetIntegerv( GL_TEXTURE_BINDING_2D, &Bound );
	Raw_glBindTexture( GL_TEXTURE_2D, Texture );
	Raw_glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	Raw_glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	Raw_glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	Raw_glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	Raw_glTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH_STENCIL_OES, Width, Height, 0, GL_DEPTH_STENCIL_OES, GL_UNSIGNED_INT_24_8_OES, NULL );
	Raw_glBindTexture( GL_TEXTURE_2D, Bound );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return Texture;
}

GLuint UUC2GLRenderDevice::CreateDepthStencilRenderbuffer( INT Width, INT Height )
{
	GLuint Renderbuffer = 0;
	if( !IsGLES )
	{
		glGenRenderbuffers( 1, &Renderbuffer );
		glBindRenderbuffer( GL_RENDERBUFFER, Renderbuffer );
		glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, Width, Height );
		glBindRenderbuffer( GL_RENDERBUFFER, 0 );
		return Renderbuffer;
	}
	Raw_glGenRenderbuffers( 1, &Renderbuffer );
	Raw_glBindRenderbuffer( GL_RENDERBUFFER, Renderbuffer );
#ifdef __EMSCRIPTEN__
	Raw_glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_STENCIL_OES, Width, Height );		// WebGL 1: DEPTH_STENCIL, core
#else
	Raw_glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8_OES, Width, Height );	// OES_packed_depth_stencil
#endif
	Raw_glBindRenderbuffer( GL_RENDERBUFFER, 0 );
	return Renderbuffer;
}

void UUC2GLRenderDevice::DeleteDepthStencilRenderbuffer( GLuint& Renderbuffer )
{
	if( Renderbuffer )
	{
		if( IsGLES )
			Raw_glDeleteRenderbuffers( 1, &Renderbuffer );
		else
			glDeleteRenderbuffers( 1, &Renderbuffer );
	}
	Renderbuffer = 0;
}

//
// GLES: makes Framebuffer the one OpenGL ES itself has bound and returns what was. gl4es
// keeps its own idea of the bound framebuffer and makes it real when it needs it, which is
// not necessarily when the driver's glBindFramebuffer returns (first browser run with
// frames: the raw attachment call answered GL_INVALID_OPERATION, i.e. no framebuffer bound).
//
GLint UUC2GLRenderDevice::RawBindFramebuffer( GLuint Framebuffer )
{
	GLint Bound = 0;
	Raw_glGetIntegerv( GL_FRAMEBUFFER_BINDING, &Bound );
	if( (GLuint)Bound != Framebuffer )
	{
		UC2GL_LOG_ONCE_INIT( TEXT("UC2GLDrv: (GLES) OpenGL ES had framebuffer %i bound where gl4es was told %i: raw calls bind what they need themselves"), Bound, Framebuffer );
		Raw_glBindFramebuffer( GL_FRAMEBUFFER, Framebuffer );
	}
	return Bound;
}

//
// The colour attachment goes through gl4es (it tracks colour attachments). While attaching,
// gl4es sets sampling parameters of the texture on whatever GLES has bound at that moment,
// so on GLES the texture is bound for the duration of the call. (The one GL error of the
// browser runs up to G4 run 12, reported here, was gl4es realising its "texture 0" inside
// this call: PrepareGL4ESTextureZero.)
//
void UUC2GLRenderDevice::AttachColorTexture( GLuint Texture )
{
	if( !IsGLES )
	{
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, Texture, 0 );
		return;
	}
	GLint Bound = 0;
	Raw_glGetIntegerv( GL_TEXTURE_BINDING_2D, &Bound );
	Raw_glBindTexture( GL_TEXTURE_2D, Texture );
	if( CheckGLErrors ) CheckGLError( TEXT("colour attachment: raw texture bind") );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, Texture, 0 );
	if( CheckGLErrors ) CheckGLError( TEXT("colour attachment: glFramebufferTexture2D through gl4es") );
	Raw_glBindTexture( GL_TEXTURE_2D, Bound );
}

// Framebuffer is the one bound through glBindFramebuffer by the caller.
void UUC2GLRenderDevice::AttachDepthStencilTexture( GLuint Framebuffer, GLuint Texture )
{
	if( !IsGLES )
	{
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, Texture, 0 );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, Texture, 0 );
		return;
	}
	GLint Bound = RawBindFramebuffer( Framebuffer );
#ifdef __EMSCRIPTEN__
	Raw_glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, Texture, 0 );
#else
	Raw_glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, Texture, 0 );
	Raw_glFramebufferTexture2D( GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, Texture, 0 );
#endif
	if( (GLuint)Bound != Framebuffer )
		Raw_glBindFramebuffer( GL_FRAMEBUFFER, Bound );
}

void UUC2GLRenderDevice::AttachDepthStencilRenderbuffer( GLuint Framebuffer, GLuint Renderbuffer )
{
	if( !IsGLES )
	{
		glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, Renderbuffer );
		glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, Renderbuffer );
		return;
	}
	GLint Bound = RawBindFramebuffer( Framebuffer );
#ifdef __EMSCRIPTEN__
	Raw_glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, Renderbuffer );
#else
	Raw_glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, Renderbuffer );
	Raw_glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, Renderbuffer );
#endif
	if( (GLuint)Bound != Framebuffer )
		Raw_glBindFramebuffer( GL_FRAMEBUFFER, Bound );
}

// The main framebuffer's depth-stencil image on Framebuffer (render targets without a depth
// buffer of their own borrow it, as D3DDrv's do).
void UUC2GLRenderDevice::AttachMainDepthStencil( GLuint Framebuffer )
{
	if( MainDepthStencil )
		AttachDepthStencilTexture( Framebuffer, MainDepthStencil );
	else
		AttachDepthStencilRenderbuffer( Framebuffer, MainDepthRenderbuffer );
}

UBOOL UUC2GLRenderDevice::UploadMainDepth( const DWORD* Rows, INT Width, INT Height )
{
	if( !MainDepthStencil )
		return 0;
	if( !IsGLES )
	{
		glBindTexture( GL_TEXTURE_2D, MainDepthStencil );
		glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, Width, Height, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, Rows );
		return 1;
	}
#ifdef __EMSCRIPTEN__
	// WEBGL_depth_texture forbids texSubImage2D on a depth texture.
	return 0;
#else
	GLint Bound = 0;
	Raw_glGetIntegerv( GL_TEXTURE_BINDING_2D, &Bound );
	Raw_glBindTexture( GL_TEXTURE_2D, MainDepthStencil );
	Raw_glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
	Raw_glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, Width, Height, GL_DEPTH_STENCIL_OES, GL_UNSIGNED_INT_24_8_OES, Rows );
	Raw_glBindTexture( GL_TEXTURE_2D, Bound );
	// gl4es sets the unpack alignment itself before each of its own uploads.
	return Raw_glGetError() == GL_NO_ERROR;
#endif
}

void UUC2GLRenderDevice::CompressedTexImage2D( GLenum BindTarget, GLuint Texture, GLenum Target, INT Level, GLenum Format, INT Width, INT Height, INT Size, const BYTE* Data, UBOOL FirstStorage )
{
	if( !IsGLES )
	{
		glCompressedTexImage2D( Target, Level, Format, Width, Height, 0, Size, Data );
		return;
	}
	if( !CapRawS3TC )
	{
		// gl4es decodes to RGBA8 (and makes the smaller mip levels itself).
		glCompressedTexImage2D( Target, Level, Format, Width, Height, 0, Size, Data );
		return;
	}
	// Around gl4es, restoring the raw binding it believes in (see the depth-stencil helpers).
	// (gl4es has seen a one-texel storage of this texture: FOpenGLTexture::PrepareStorage.)
	GLenum	Query = BindTarget == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_BINDING_CUBE_MAP : GL_TEXTURE_BINDING_2D;
	GLint	Bound = 0;
	Raw_glGetIntegerv( Query, &Bound );
	Raw_glBindTexture( BindTarget, Texture );
	Raw_glCompressedTexImage2D( Target, Level, Format, Width, Height, 0, Size, Data );
	if( CheckGLErrors )
	{
		GLenum Error = Raw_glGetError();
		if( Error != GL_NO_ERROR )
		{
			GLErrorCount++;
			debugf( NAME_Warning, TEXT("UC2GLDrv: GL error 0x%04x from a compressed upload: texture %i target 0x%04x level %i format 0x%04x %ix%i, %i bytes (frame %i)"), (INT)Error, Texture, Target, Level, Format, Width, Height, Size, FrameCounter );
		}
	}
	Raw_glBindTexture( BindTarget, Bound );
}

//
// M11. Storage of ENGINE textures on GLES goes to the library under gl4es, compressed or
// not. gl4es keeps its own idea of a texture's mip chain: at the first draw with a mipmap
// filter it calls glGenerateMipmap on any texture whose levels it has not seen arrive down
// to 1x1, which would replace the engine's mip levels (and fails on compressed ones). What
// gl4es is given instead is a one-texel "automatic mipmap" storage per texture
// (FOpenGLTexture::PrepareStorage): valid for its fixed-function emulation, nothing for it
// to generate or convert. Render targets, the movie texture and the small device textures
// stay with gl4es.
//
void UUC2GLRenderDevice::TexImage2D( GLenum BindTarget, GLuint Texture, GLenum Target, INT Level, GLint Internal, INT Width, INT Height, GLenum Format, GLenum Type, const BYTE* Data )
{
	if( !IsGLES )
	{
		glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
		glTexImage2D( Target, Level, Internal, Width, Height, 0, Format, Type, Data );
		glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
		return;
	}
	// OpenGL ES 2: the internal format is the format; BGRA only as an extension WebGL does
	// not have, so the D3DCOLOR bytes are reordered here (one path for the twin and the web).
	if( Format == GL_BGRA )
	{
		INT Bytes = Width * Height * 4;
		if( ScratchBuffer.Num() < Bytes )
			ScratchBuffer.Add( Bytes - ScratchBuffer.Num() );
		BYTE* Dest = &ScratchBuffer(0);
		for( INT i=0; i<Bytes; i+=4 )
		{
			Dest[i]		= Data[i+2];
			Dest[i+1]	= Data[i+1];
			Dest[i+2]	= Data[i];
			Dest[i+3]	= Data[i+3];
		}
		Data	= Dest;
		Format	= GL_RGBA;
	}
	GLenum	Query = BindTarget == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_BINDING_CUBE_MAP : GL_TEXTURE_BINDING_2D;
	GLint	Bound = 0, Alignment = 4;
	Raw_glGetIntegerv( Query, &Bound );
	Raw_glGetIntegerv( GL_UNPACK_ALIGNMENT, &Alignment );
	Raw_glBindTexture( BindTarget, Texture );
	Raw_glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
	Raw_glTexImage2D( Target, Level, Format, Width, Height, 0, Format, Type, Data );
	Raw_glPixelStorei( GL_UNPACK_ALIGNMENT, Alignment );
	Raw_glBindTexture( BindTarget, Bound );
}

//
// M11. gl4es remembers, per vertex attribute, which buffer and offset it last sent to
// OpenGL ES and skips the call when the next draw asks for the same. It does not forget that
// when the buffer is deleted: a new buffer that gets the old one's name (GLES hands names
// out again at once) with the same layout is taken for "already sent", while GLES has reset
// the attribute to no buffer at the deletion. The draw then reads nothing (found by the
// self-test's depth probe: its second draw from a freshly created buffer drew no pixel).
// So on GLES buffer names are never deleted while the device lives: a freed one is emptied
// and reused, by target (a WebGL buffer keeps the target it was first bound to).
//
GLuint UUC2GLRenderDevice::NewBuffer( GLenum Target )
{
	TArray<GLuint>& Free = Target == GL_ELEMENT_ARRAY_BUFFER ? FreeElementBuffers : FreeArrayBuffers;
	GLuint Buffer = 0;
	if( IsGLES && Free.Num() )
		Buffer = Free.Pop();
	else
		glGenBuffers( 1, &Buffer );
	return Buffer;
}

void UUC2GLRenderDevice::FreeBuffer( GLenum Target, GLuint& Buffer )
{
	if( !Buffer )
		return;
	if( IsGLES )
	{
		glBindBuffer( Target, Buffer );
		glBufferData( Target, 4, NULL, GL_STATIC_DRAW );
		glBindBuffer( Target, 0 );
		(Target == GL_ELEMENT_ARRAY_BUFFER ? FreeElementBuffers : FreeArrayBuffers).AddItem( Buffer );
	}
	else
		glDeleteBuffers( 1, &Buffer );
	Buffer = 0;
}

void UUC2GLRenderDevice::LogFrameStats()
{
	DOUBLE	Now		= appSeconds();
	INT		Frames	= FrameCounter - StatFrame0;
	if( StatTime0 > 0.0 && Frames > 0 )
	{
		FLOAT N = Frames;
		debugf( NAME_Init, TEXT("UC2GLDrv: stats: frames %i-%i: %.2f ms/frame (%.1f frames/s); per frame: %.1f draw calls, %.0f primitives, %.1f material state applications, %.1f buffer uploads (%.1f KB), %.2f texture uploads, %.2f device quads (%.2f back-buffer snapshots, %.2f depth-as-colour, %.2f depth copies)"),
			StatFrame0 + 1, FrameCounter, (FLOAT)((Now - StatTime0) * 1000.0 / Frames), (FLOAT)(Frames / (Now - StatTime0)),
			StatDraws / N, StatPrimitives / N, StatMaterials / N, StatBufferUploads / N, StatBufferBytes / N / 1024.f, StatTextureUploads / N, StatPostQuads / N,
			(SnapshotCopies - StatSnapshots) / N, (DepthColorCopies - StatDepthColors) / N, (DepthBufferCopies - StatDepthCopies) / N );
	}
	StatTime0	= Now;
	StatFrame0	= FrameCounter;
	StatDraws = StatPrimitives = StatMaterials = StatBufferUploads = StatBufferBytes = StatTextureUploads = StatPostQuads = 0;
	StatSnapshots	= SnapshotCopies;
	StatDepthColors	= DepthColorCopies;
	StatDepthCopies	= DepthBufferCopies;
}

void UUC2GLRenderDevice::BindQuadBuffer()
{
	glBindBuffer( GL_ARRAY_BUFFER, QuadBuffer );
}

//
// What the context can do (see the Cap* members). Desktop GL 2.0 + FBO has all of it.
//
//
// GLES: gl4es' "texture 0" (M11, G4).
//
// gl4es treats texture 0 as an object with parameters, as desktop GL and OpenGL ES 2 do (they
// have a default texture); WebGL has none. Two consequences, both seen:
// - The first time gl4es realises a unit with texture 0 bound, it sets that texture's filter
//   and wrap parameters with nothing bound under it, and WebGL answers GL_INVALID_OPERATION
//   (the one GL error of every browser run up to G4 run 12, inside the first
//   glFramebufferTexture2D: gl4es realize_textures -> realize_1texture).
// - Its texture 0 has the default minifying filter NEAREST_MIPMAP_LINEAR and no mipmaps, so at
//   a draw gl4es calls glGenerateMipmap for it (realize_textures, drawing) without making that
//   unit active: on whatever texture the ACTIVE unit has, which can be an engine texture with
//   DXT storage (Mesa with an ES 2.0 context: "GL_INVALID_OPERATION in generate mipmaps on
//   compressed texture"; gl4es' own glGetError calls hide it from -glerrors).
// So: every unit starts with a texture of our own under gl4es' "texture 0" (1x1 opaque black,
// which is what WebGL samples for no texture; gl4es binds a real 0 once it has bound anything
// else on a unit, and by then has nothing left to set), and texture 0 gets a filter that needs
// no mipmaps.
//
void UUC2GLRenderDevice::PrepareGL4ESTextureZero()
{
	if( Raw_glGenTextures )
	{
		static const BYTE Black[4] = { 0, 0, 0, 255 };
		GLint RawUnits = 0;
		Raw_glGetIntegerv( GL_MAX_TEXTURE_IMAGE_UNITS, &RawUnits );
		GLuint StandIn = 0;
		Raw_glGenTextures( 1, &StandIn );
		for( INT Unit = Min<INT>( RawUnits, 32 ) - 1; Unit >= 0; Unit-- )
		{
			Raw_glActiveTexture( GL_TEXTURE0 + Unit );
			Raw_glBindTexture( GL_TEXTURE_2D, StandIn );
			if( Unit == 0 )
				Raw_glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, Black );
		}
	}
	// gl4es keeps sampler parameters and applies them when it next realises the unit. That has
	// to happen NOW, while no texture of gl4es' own was ever bound for real (so its "texture 0"
	// is our stand-in): 2D binds are deferred, so bind another name and 0 again to mark unit 0
	// changed, and glFlush realises it (gl4es realize_textures). From then on gl4es' record of
	// texture 0 matches what it wants and it never sets texture 0's parameters again.
	GLuint Scratch = 0;
	glActiveTexture( GL_TEXTURE0 );
	glGenTextures( 1, &Scratch );
	glBindTexture( GL_TEXTURE_2D, Scratch );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glFlush();
	glDeleteTextures( 1, &Scratch );
}

void UUC2GLRenderDevice::DetectCapabilities()
{
	CapDepthTexture = CapFragDepth = CapUIntIndex = CapTextureMaxLevel = CapFragmentHighp = 1;
	CapRawS3TC = 0;
	if( IsGLES )
	{
		// (EXT_texture_compression_s3tc alone may mean DXT1 only on OpenGL ES; the ANGLE pair or the WebGL extension has all three.)
		CapRawS3TC			= Raw_glCompressedTexImage2D != NULL &&
							(  FindRawExt("WEBGL_compressed_texture_s3tc") || FindRawExt("GL_WEBGL_compressed_texture_s3tc")
							|| (FindRawExt("GL_EXT_texture_compression_s3tc") && FindRawExt("GL_ANGLE_texture_compression_dxt3") && FindRawExt("GL_ANGLE_texture_compression_dxt5")) );
		CapDepthTexture		= ( FindRawExt("GL_OES_depth_texture") || FindRawExt("WEBGL_depth_texture") || FindRawExt("GL_WEBGL_depth_texture") )
#ifndef __EMSCRIPTEN__
							&& FindRawExt("GL_OES_packed_depth_stencil")
#endif
							;
		CapFragDepth		= FindRawExt("GL_EXT_frag_depth") || FindRawExt("EXT_frag_depth");
		CapUIntIndex		= FindRawExt("GL_OES_element_index_uint") || FindRawExt("OES_element_index_uint");
		CapTextureMaxLevel	= 0;
		GLint Range[2] = { 0, 0 }, Precision = 0;
		if( Raw_glGetShaderPrecisionFormat )
			Raw_glGetShaderPrecisionFormat( GL_FRAGMENT_SHADER, GL_HIGH_FLOAT, Range, &Precision );
		CapFragmentHighp	= Precision >= 23;
		debugf( NAME_Init, TEXT("UC2GLDrv: GLES hardware: %s / %s"), appFromAnsi((ANSICHAR*)Raw_glGetString(GL_RENDERER)), appFromAnsi((ANSICHAR*)Raw_glGetString(GL_VERSION)) );
		debugf( NAME_Init, TEXT("UC2GLDrv: GLES extensions: %s"), appFromAnsi((ANSICHAR*)Raw_glGetString(GL_EXTENSIONS)) );
		debugf( NAME_Init, TEXT("UC2GLDrv: GLES fragment highp float: %i bits of precision (range 2^-%i..2^%i)"), Precision, Range[0], Range[1] );
	}
	// Masks, to exercise each fallback where the capability exists.
	if( ParseParam( appCmdLine(), TEXT("glnodepthtexture") ) )	CapDepthTexture = 0;
	if( ParseParam( appCmdLine(), TEXT("glnofragdepth") ) )		CapFragDepth = 0;
	if( ParseParam( appCmdLine(), TEXT("glnouintindex") ) )		CapUIntIndex = 0;
	if( ParseParam( appCmdLine(), TEXT("glnomaxlevel") ) )		CapTextureMaxLevel = 0;
	if( ParseParam( appCmdLine(), TEXT("glnos3tc") ) )
	{
		// GLES: gl4es decodes. Desktop: the engine's decoder.
		CapRawS3TC = 0;
		if( !IsGLES )
			SUPPORTS_GL_EXT_texture_compression_s3tc = 0;
	}
	debugf( NAME_Init, TEXT("UC2GLDrv: capabilities: S3TC %s, depth texture %s, gl_FragDepth %s, 32-bit indices %s, texture max level %s, fragment highp %s"),
		UseHardwareS3TC() ? TEXT("yes") : TEXT("NO (fallback: the driver decodes DXT to RGBA8)"),
		CapDepthTexture ? TEXT("yes") : TEXT("NO (no fallback: the depth of field and the hiding of distortion behind nearer geometry are lost)"),
		CapFragDepth ? TEXT("yes") : TEXT("NO (fallback: the distortion passes test the scene's depth in the shader)"),
		CapUIntIndex ? TEXT("yes") : TEXT("NO (fallback: 32-bit index data stored as 16-bit)"),
		CapTextureMaxLevel ? TEXT("yes") : TEXT("no (mip chains are completed to 1x1)"),
		CapFragmentHighp ? TEXT("yes") : TEXT("NO (mediump)") );
}

//
// The present shader: GLSL 1.20 written so it is also valid GLSL ES 1.00 (no fixed-function
// built-ins, explicit attribute and uniforms); the version line is the binding's.
//
static const char* GPresentVertexShader =
	"attribute vec2 aPosition;\n"
	"varying vec2 vTexCoord;\n"
	"void main()\n"
	"{\n"
	"	vTexCoord = aPosition * 0.5 + 0.5;\n"
	"	gl_Position = vec4( aPosition, 0.0, 1.0 );\n"
	"}\n";

// UD3DRenderDevice::UpdateGamma's ramp, normalised:
//   out = (Contrast+0.5) * in^(1/Gamma) + (Brightness-0.5)*0.5 - Contrast*0.5 + 0.25
// uGamma = ( 1/Gamma, Contrast+0.5, (Brightness-0.5)*0.5 - Contrast*0.5 + 0.25, enabled ).
// M7: with dynamic gamma the ramp is the 256-entry table FD3DDynamicGamma::UpdateDeviceGamma
// computes (uRamp, one channel; uUseRamp 1), looked up per colour channel at texel centres.
// O-8: uFXAA = ( 1/width, 1/height of the scene, enabled ). FXAA where the main framebuffer
// cannot be multisampled (OpenGL ES 2 / WebGL 1): Timothy Lottes' "FXAA PC simple" filter (the
// public reduced form: four diagonal neighbours give the edge direction, two or four taps along
// it, the wider blend kept only if its luma stays within the neighbourhood's range). It runs on
// the scene before the gamma, i.e. on what the game drew; the HUD and menus are in that picture
// too (the engine draws them into the same buffer), so their edges are smoothed as well.
static const char* GPresentFragmentShader =
	"uniform sampler2D uScene;\n"
	"uniform sampler2D uRamp;\n"
	"uniform float uUseRamp;\n"
	"uniform vec4 uGamma;\n"
	"uniform vec4 uFXAA;\n"
	"varying vec2 vTexCoord;\n"
	"vec3 FXAA( vec2 UV )\n"
	"{\n"
	"	vec2 Rcp = uFXAA.xy;\n"
	"	vec3 NW = texture2D( uScene, UV + vec2( -1.0, -1.0 ) * Rcp ).rgb;\n"
	"	vec3 NE = texture2D( uScene, UV + vec2( 1.0, -1.0 ) * Rcp ).rgb;\n"
	"	vec3 SW = texture2D( uScene, UV + vec2( -1.0, 1.0 ) * Rcp ).rgb;\n"
	"	vec3 SE = texture2D( uScene, UV + vec2( 1.0, 1.0 ) * Rcp ).rgb;\n"
	"	vec3 M = texture2D( uScene, UV ).rgb;\n"
	"	vec3 Luma = vec3( 0.299, 0.587, 0.114 );\n"
	"	float LNW = dot( NW, Luma ), LNE = dot( NE, Luma ), LSW = dot( SW, Luma ), LSE = dot( SE, Luma ), LM = dot( M, Luma );\n"
	"	float LMin = min( LM, min( min( LNW, LNE ), min( LSW, LSE ) ) );\n"
	"	float LMax = max( LM, max( max( LNW, LNE ), max( LSW, LSE ) ) );\n"
	"	vec2 Dir = vec2( -((LNW + LNE) - (LSW + LSE)), (LNW + LSW) - (LNE + LSE) );\n"
	"	float Reduce = max( (LNW + LNE + LSW + LSE) * (0.25 * 0.125), 1.0 / 128.0 );\n"
	"	float RcpDirMin = 1.0 / (min( abs( Dir.x ), abs( Dir.y ) ) + Reduce);\n"
	"	Dir = min( vec2( 8.0 ), max( vec2( -8.0 ), Dir * RcpDirMin ) ) * Rcp;\n"
	"	vec3 A = 0.5 * (texture2D( uScene, UV + Dir * (1.0 / 3.0 - 0.5) ).rgb + texture2D( uScene, UV + Dir * (2.0 / 3.0 - 0.5) ).rgb);\n"
	"	vec3 B = A * 0.5 + 0.25 * (texture2D( uScene, UV + Dir * -0.5 ).rgb + texture2D( uScene, UV + Dir * 0.5 ).rgb);\n"
	"	float LB = dot( B, Luma );\n"
	"	return (LB < LMin || LB > LMax) ? A : B;\n"
	"}\n"
	"void main()\n"
	"{\n"
	"	vec3 Scene = uFXAA.z > 0.5 ? FXAA( vTexCoord ) : texture2D( uScene, vTexCoord ).rgb;\n"
	"	vec3 Ramp = clamp( uGamma.y * pow( Scene, vec3( uGamma.x ) ) + vec3( uGamma.z ), 0.0, 1.0 );\n"
	"	vec3 Result = mix( Scene, Ramp, uGamma.w );\n"
	"	if( uUseRamp > 0.5 )\n"
	"	{\n"
	"		vec3 Index = Scene * (255.0 / 256.0) + vec3( 0.5 / 256.0 );\n"
	"		Result = vec3( texture2D( uRamp, vec2( Index.r, 0.5 ) ).r, texture2D( uRamp, vec2( Index.g, 0.5 ) ).r, texture2D( uRamp, vec2( Index.b, 0.5 ) ).r );\n"
	"	}\n"
	"	gl_FragColor = vec4( Result, 1.0 );\n"
	"}\n";

static GLuint CompileShader( UUC2GLRenderDevice* RenDev, GLenum Type, const char* Body )
{
	const char* Sources[2] = { RenDev->ShaderPrologue( Type == GL_FRAGMENT_SHADER ), Body };
	GLuint Shader = RenDev->glCreateShader( Type );
	RenDev->glShaderSource( Shader, 2, Sources, NULL );
	RenDev->glCompileShader( Shader );
	GLint Status = 0;
	RenDev->glGetShaderiv( Shader, GL_COMPILE_STATUS, &Status );
	if( !Status )
	{
		char Log[2048] = "";
		RenDev->glGetShaderInfoLog( Shader, sizeof(Log), NULL, Log );
		debugf( NAME_Init, TEXT("UC2GLDrv: present shader does not compile: %s"), appFromAnsi(Log) );
		RenDev->glDeleteShader( Shader );
		return 0;
	}
	return Shader;
}

UBOOL UUC2GLRenderDevice::CreatePresentProgram()
{
	guard(UUC2GLRenderDevice::CreatePresentProgram);

	GLuint VS = CompileShader( this, GL_VERTEX_SHADER, GPresentVertexShader ),
		   FS = CompileShader( this, GL_FRAGMENT_SHADER, GPresentFragmentShader );
	if( !VS || !FS )
		return 0;

	PresentProgram = glCreateProgram();
	glAttachShader( PresentProgram, VS );
	glAttachShader( PresentProgram, FS );
	glBindAttribLocation( PresentProgram, 0, "aPosition" );
	glLinkProgram( PresentProgram );
	glDeleteShader( VS );
	glDeleteShader( FS );

	GLint Status = 0;
	glGetProgramiv( PresentProgram, GL_LINK_STATUS, &Status );
	if( !Status )
	{
		char Log[2048] = "";
		glGetProgramInfoLog( PresentProgram, sizeof(Log), NULL, Log );
		debugf( NAME_Init, TEXT("UC2GLDrv: present program does not link: %s"), appFromAnsi(Log) );
		return 0;
	}
	PresentTextureLoc	= glGetUniformLocation( PresentProgram, "uScene" );
	PresentGammaLoc		= glGetUniformLocation( PresentProgram, "uGamma" );
	PresentRampLoc		= glGetUniformLocation( PresentProgram, "uRamp" );
	PresentUseRampLoc	= glGetUniformLocation( PresentProgram, "uUseRamp" );
	PresentFXAALoc		= glGetUniformLocation( PresentProgram, "uFXAA" );
	return 1;

	unguard;
}

void UUC2GLRenderDevice::DrawPresentPass( UViewport* Viewport )
{
	guard(UUC2GLRenderDevice::DrawPresentPass);

	INT DrawableX = MainWidth, DrawableY = MainHeight;
	BindingGetDrawableSize( Viewport, DrawableX, DrawableY );

	ResolveMain( 1, 0 );
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	glViewport( 0, 0, DrawableX, DrawableY );
	glDisable( GL_SCISSOR_TEST );
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_STENCIL_TEST );
	glDisable( GL_BLEND );
	glDisable( GL_ALPHA_TEST );
	glDisable( GL_CULL_FACE );
	glDisable( GL_FOG );
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );

	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, MainColorTexture );

	glUseProgram( PresentProgram );
	glUniform1i( PresentTextureLoc, 0 );
	FLOAT Gamma = Max( PresentGamma, 0.01f );
	glUniform4f( PresentGammaLoc, 1.f / Gamma, PresentContrast + 0.5f, (PresentBrightness - 0.5f) * 0.5f - PresentContrast * 0.5f + 0.25f, ApplyGamma ? 1.f : 0.f );
	UBOOL UseRamp = GammaRampActive && GammaRampTexture && ApplyGamma && PostProcessActive();
	glUniform1f( PresentUseRampLoc, UseRamp ? 1.f : 0.f );
	glUniform4f( PresentFXAALoc, 1.f / Max( MainWidth, 1 ), 1.f / Max( MainHeight, 1 ), PresentFXAA ? 1.f : 0.f, 0.f );
	glUniform1i( PresentRampLoc, 1 );
	if( UseRamp )
	{
		glActiveTexture( GL_TEXTURE1 );
		glBindTexture( GL_TEXTURE_2D, GammaRampTexture );
		glActiveTexture( GL_TEXTURE0 );
	}

	BindQuadBuffer();
	glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, 0, NULL );
	glEnableVertexAttribArray( 0 );
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	glDisableVertexAttribArray( 0 );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glUseProgram( 0 );

	// Back to the engine's state.
	glEnable( GL_SCISSOR_TEST );
	if( RenderInterface.DepthEnabled )
		glEnable( GL_DEPTH_TEST );
	if( RenderInterface.StencilEnabled )
		glEnable( GL_STENCIL_TEST );
	glBindFramebuffer( GL_FRAMEBUFFER, MainFramebuffer );
	RenderInterface.InvalidateHardwareState();

	unguard;
}

//
// The fixed-function defaults D3DDrv sets in SetRes and FD3DDeferredState::Init.
//
void UUC2GLRenderDevice::InitGLState()
{
	guard(UUC2GLRenderDevice::InitGLState);

	glEnable( GL_DEPTH_TEST );
	glDepthFunc( GL_LEQUAL );
	glDepthMask( GL_TRUE );
	glShadeModel( GL_SMOOTH );
	glEnable( GL_NORMALIZE );							// D3DRS_NORMALIZENORMALS
	glEnable( GL_SCISSOR_TEST );						// the scissor is always the viewport
	glEnable( GL_POLYGON_OFFSET_FILL );
	glPolygonOffset( 0.f, 0.f );
	glDisable( GL_CULL_FACE );
	glFrontFace( GL_CCW );
	glDisable( GL_BLEND );
	glBlendFunc( GL_ONE, GL_ZERO );
	glBlendEquation( GL_FUNC_ADD );
	glAlphaFunc( GL_GREATER, 0.f );
	glDisable( GL_ALPHA_TEST );
	glDisable( GL_LIGHTING );
	glDisable( GL_FOG );
	glFogi( GL_FOG_MODE, GL_LINEAR );					// D3DFOG_LINEAR
	glHint( GL_FOG_HINT, GL_NICEST );					// per pixel, like table fog
	glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );

	// D3D material: ambient and diffuse white, specular 0, power 0; emissive from COLOR1.
	static const FLOAT One[4]	= { 1.f, 1.f, 1.f, 1.f };
	static const FLOAT Zero[4]	= { 0.f, 0.f, 0.f, 1.f };
	glMaterialfv( GL_FRONT_AND_BACK, GL_AMBIENT, One );
	glMaterialfv( GL_FRONT_AND_BACK, GL_DIFFUSE, One );
	glMaterialfv( GL_FRONT_AND_BACK, GL_SPECULAR, Zero );
	glMaterialfv( GL_FRONT_AND_BACK, GL_EMISSION, Zero );
	glColorMaterial( GL_FRONT_AND_BACK, GL_EMISSION );
	glDisable( GL_COLOR_MATERIAL );
	glLightModeli( GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE );
	glLightModeli( GL_LIGHT_MODEL_TWO_SIDE, GL_FALSE );
	glLightModeli( GL_LIGHT_MODEL_COLOR_CONTROL, GL_SINGLE_COLOR );
	for( INT i=0; i<8; i++ )
	{
		glLightfv( GL_LIGHT0 + i, GL_AMBIENT, Zero );
		glLightfv( GL_LIGHT0 + i, GL_SPECULAR, Zero );
		glDisable( GL_LIGHT0 + i );
	}

	// Texture units: combiners on, camera-space position texgen planes (eye planes are taken
	// through the modelview at the time they are set, so set them with identity).
	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();
	static const FLOAT PlaneS[4] = { 1.f, 0.f, 0.f, 0.f },
					   PlaneT[4] = { 0.f, 1.f, 0.f, 0.f },
					   PlaneR[4] = { 0.f, 0.f, 1.f, 0.f },
					   PlaneQ[4] = { 0.f, 0.f, 0.f, 1.f };
	for( INT s=0; s<Min<INT>(Max<INT>(GLFixedFunctionUnits,GLProgramUnits),MAX_STAGES); s++ )
	{
		// (O-9: every unit fragment programs may use; texture environment state only on its own units.)
		glActiveTexture( GL_TEXTURE0 + s );
		glClientActiveTexture( GL_TEXTURE0 + s );
		if( s < GLFixedFunctionUnits )
			glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE );
		glTexGenfv( GL_S, GL_EYE_PLANE, PlaneS );
		glTexGenfv( GL_T, GL_EYE_PLANE, PlaneT );
		glTexGenfv( GL_R, GL_EYE_PLANE, PlaneR );
		glTexGenfv( GL_Q, GL_EYE_PLANE, PlaneQ );
		glDisable( GL_TEXTURE_GEN_S );
		glDisable( GL_TEXTURE_GEN_T );
		glDisable( GL_TEXTURE_GEN_R );
		glDisable( GL_TEXTURE_GEN_Q );
		if( s < GLFixedFunctionUnits )
		{
			glDisable( GL_TEXTURE_2D );
			glDisable( GL_TEXTURE_CUBE_MAP );
		}
		glDisableClientState( GL_TEXTURE_COORD_ARRAY );
		glMatrixMode( GL_TEXTURE );
		glLoadIdentity();
		glMatrixMode( GL_MODELVIEW );
	}
	glActiveTexture( GL_TEXTURE0 );
	glClientActiveTexture( GL_TEXTURE0 );

	// Stencil: D3DRS_STENCILENABLE = UseStencil, ALWAYS/KEEP.
	RenderInterface.StencilEnabled	= UseStencil;
	RenderInterface.DepthEnabled	= 1;
	if( UseStencil )
		glEnable( GL_STENCIL_TEST );
	else
		glDisable( GL_STENCIL_TEST );
	glStencilFunc( GL_ALWAYS, 0, 0xff );
	glStencilOp( GL_KEEP, GL_KEEP, GL_KEEP );
	glStencilMask( 0xff );

	unguard;
}

/*-----------------------------------------------------------------------------
	Resource caching (UC2's scheme).
-----------------------------------------------------------------------------*/

FOpenGLResource* UUC2GLRenderDevice::GetCachedResource(_WORD CacheId,ECacheHint CacheHint)
{
	switch( CacheHint )
	{
	case CH_Texture:		return TextureCache.GetCachedResource(CacheId);
	case CH_VertexBuffer:	return VertexBufferCache.GetCachedResource(CacheId);
	case CH_IndexBuffer:	return IndexBufferCache.GetCachedResource(CacheId);
	}
	return NULL;
}

void UUC2GLRenderDevice::CacheResource(FOpenGLResource* Resource,ECacheHint CacheHint)
{
	if( Resource->CacheId != INVALID_CACHEID )
		return;
	switch( CacheHint )
	{
	case CH_Texture:		TextureCache.Cache((FOpenGLTexture*)Resource);				break;
	case CH_VertexBuffer:	VertexBufferCache.Cache((FOpenGLVertexStream*)Resource);	break;
	case CH_IndexBuffer:	IndexBufferCache.Cache((FOpenGLIndexBuffer*)Resource);		break;
	}
}

void UUC2GLRenderDevice::FlushResource( _WORD CacheId, ECacheHint CacheHint )
{
	guard(UUC2GLRenderDevice::FlushResource);

	FOpenGLResource* CachedResource = GetCachedResource(CacheId,CacheHint);
	if( CachedResource )
	{
		switch( CacheHint )
		{
		case CH_Texture:		TextureCache.Flush(CacheId);		break;
		case CH_VertexBuffer:	VertexBufferCache.Flush(CacheId);	break;
		case CH_IndexBuffer:	IndexBufferCache.Flush(CacheId);	break;
		}
		delete CachedResource;
	}

	unguard;
}

void UUC2GLRenderDevice::FlushResource(FOpenGLResource* Resource)
{
	guard(UUC2GLRenderDevice::FlushResource);
	check(Resource);
	ECacheHint CacheHint = Resource->GetVertexStream() ? CH_VertexBuffer : Resource->GetIndexBuffer() ? CH_IndexBuffer : CH_Texture;
	if( Resource->CacheId != INVALID_CACHEID && GetCachedResource(Resource->CacheId,CacheHint) == Resource )
		FlushResource( Resource->CacheId, CacheHint );
	else
		delete Resource;
	unguard;
}

UBOOL UUC2GLRenderDevice::ResourceCached( _WORD CacheId, ECacheHint CacheHint )
{
	return GetCachedResource(CacheId,CacheHint) != NULL;
}

FOpenGLVertexShader* UUC2GLRenderDevice::GetVertexShader(QWORD VertexShaderEnum,FShaderDeclaration& Declaration)
{
	// Fixed function: one shader per vertex declaration; the enum is recorded, not used.
	for( FOpenGLVertexShader* ShaderPtr = VertexShaders; ShaderPtr; ShaderPtr = ShaderPtr->NextVertexShader )
		if( ShaderPtr->Declaration == Declaration )
			return ShaderPtr;
	return new FOpenGLVertexShader(this,Declaration,VertexShaderEnum);
}

void UUC2GLRenderDevice::Flush( UViewport* Viewport )
{
	guard(UUC2GLRenderDevice::Flush);

	if( ContextAlive() )
		ResetRenderStates();

	DynamicVertexStream		= NULL;
	DynamicIndexBuffer16	= NULL;
	DynamicIndexBuffer32	= NULL;

	while( ResourceList )
		FlushResource( ResourceList );

	if( DefaultRenderTarget )
		DefaultRenderTarget->ResetCacheIDs();

	TextureCache.Reset();
	VertexBufferCache.Reset();
	IndexBufferCache.Reset();

	if( !Viewport || Viewport->GetOuterUClient()->Viewports.Num() == 0 )
		while( VertexShaders )
			delete VertexShaders;

	PrecacheOnFlip = UsePrecaching;

	unguard;
}

void UUC2GLRenderDevice::ResetRenderStates(void)
{
	guard(UUC2GLRenderDevice::ResetRenderStates);
	if( ValidContext )
	{
		InitGLState();
		RenderInterface.InvalidateHardwareState();
		RenderInterface.LightsDirty		= 1;
		RenderInterface.MatricesDirty	= 1;
	}
	unguard;
}

/*-----------------------------------------------------------------------------
	Gamma: applied by the present pass, never to the display.
-----------------------------------------------------------------------------*/

void UUC2GLRenderDevice::UpdateGamma( UViewport* Viewport )
{
	guard(UUC2GLRenderDevice::UpdateGamma);
	PresentGamma		= Viewport->GetOuterUClient()->Gamma;
	PresentBrightness	= Viewport->GetOuterUClient()->Brightness;
	PresentContrast		= Viewport->GetOuterUClient()->Contrast;
	ApplyGamma			= !ParseParam( appCmdLine(), TEXT("NOGAMMA") );
	// D3D's SetGammaRamp replaces whatever ramp the dynamic gamma had set.
	GammaRampActive		= 0;
	unguard;
}

void UUC2GLRenderDevice::RestoreGamma()
{
	// Nothing to restore: the display's ramp is never changed.
}

/*-----------------------------------------------------------------------------
	Exec.
-----------------------------------------------------------------------------*/

UBOOL UUC2GLRenderDevice::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UUC2GLRenderDevice::Exec);

	if( ParseCommand(&Cmd,TEXT("GLINFO")) )
	{
		Ar.Logf( TEXT("UC2GLDrv: %s / %s / %s; %i units; %i frames; %i GL errors"),
			ValidContext ? appFromAnsi((ANSICHAR*)glGetString(GL_VENDOR)) : TEXT("-"),
			ValidContext ? appFromAnsi((ANSICHAR*)glGetString(GL_RENDERER)) : TEXT("-"),
			ValidContext ? appFromAnsi((ANSICHAR*)glGetString(GL_VERSION)) : TEXT("-"),
			NumTextureUnits, FrameCounter, GLErrorCount );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("RESOURCES")) )
	{
		INT Bytes[3] = {0,0,0}, Counts[3] = {0,0,0};
		for( FOpenGLResource* Resource = ResourceList; Resource; Resource = Resource->NextResource )
		{
			INT Kind = Resource->GetTexture() ? 0 : Resource->GetVertexStream() ? 1 : 2;
			Bytes[Kind] += Resource->CalculateFootprint();
			Counts[Kind]++;
		}
		Ar.Logf( TEXT("Textures %i (%i bytes), vertex streams %i (%i bytes), index buffers %i (%i bytes)"), Counts[0], Bytes[0], Counts[1], Bytes[1], Counts[2], Bytes[2] );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("PIXELSHADERS")) )
	{
		// PIXELSHADERS [0|1]: switch the programmable materials (no argument: toggle).
		UBOOL Wanted = appStrlen(Cmd) ? (appAtoi(Cmd) != 0) : !UsePixelShaders;
		if( Wanted != UsePixelShaders && ValidContext && !LockedViewport )
		{
			LogPixelShaderStats();
			UsePixelShaders = Wanted;
			InitPixelShaders();
		}
		Ar.Logf( TEXT("UC2GLDrv: pixel shaders %s (frame %i)"), UsePixelShaders ? TEXT("on") : TEXT("off"), FrameCounter );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("POSTPROCESS")) )
	{
		// POSTPROCESS [0|1]: switch render-target post-processing (no argument: toggle).
		UBOOL Wanted = appStrlen(Cmd) ? (appAtoi(Cmd) != 0) : !UsePostProcess;
		if( Wanted != UsePostProcess && ValidContext && !LockedViewport )
			SetPostProcess( Wanted );
		Ar.Logf( TEXT("UC2GLDrv: post-processing %s (frame %i)"), UsePostProcess ? TEXT("on") : TEXT("off"), FrameCounter );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("GLTEXELS")) )
	{
		// GLTEXELS <Package.Group.Texture>: size, format and nine texels (corners, edge
		// midpoints, centre) of a texture, as R,G,B,A (verification: colour ramps).
		UTexture* Texture = Cast<UTexture>( UObject::StaticLoadObject( UTexture::StaticClass(), NULL, Cmd, NULL, LOAD_NoWarn, NULL ) );
		if( !Texture )
			Ar.Logf( TEXT("UC2GLDrv: GLTEXELS: no texture '%s'"), Cmd );
		else
		{
			FBaseTexture*	Base	= Texture->Get( 0, NULL )->GetRenderInterface();
			FTexture*		Tex		= Base ? Base->GetTextureInterface() : NULL;
			INT				W		= Texture->USize, H = Texture->VSize;
			if( Tex && W > 0 && H > 0 )
			{
				TArray<FColor> Texels( W * H );
				Tex->GetTextureData( 0, &Texels(0), W * sizeof(FColor), TEXF_RGBA8, 0 );
				FString Line;
				for( INT j=0; j<3; j++ )
					for( INT i=0; i<3; i++ )
					{
						FColor C = Texels( (j*(H-1)/2) * W + i*(W-1)/2 );
						Line += FString::Printf( TEXT(" (%i,%i)=%i,%i,%i,%i"), i*(W-1)/2, j*(H-1)/2, C.R, C.G, C.B, C.A );
					}
				Ar.Logf( TEXT("UC2GLDrv: GLTEXELS %s: %ix%i format %i:%s"), Texture->GetFullName(), W, H, (INT)Texture->Format, *Line );
				// The mip chain as the device will upload it: first texel and middle texel of each level.
				FString MipLine;
				for( INT m=Tex->GetFirstMip(); m<Tex->GetNumMips(); m++ )
				{
					INT MW = Max( W >> m, 1 ), MH = Max( H >> m, 1 );
					TArray<FColor> MipTexels( MW * MH );
					appMemzero( &MipTexels(0), MW * MH * sizeof(FColor) );
					Tex->GetTextureData( m, &MipTexels(0), MW * sizeof(FColor), TEXF_RGBA8, 0 );
					FColor A = MipTexels(0), B = MipTexels( MW/2 );
					MipLine += FString::Printf( TEXT(" mip %i %ix%i: %i,%i,%i / %i,%i,%i;"), m, MW, MH, A.R, A.G, A.B, B.R, B.G, B.B );
				}
				// And what the device holds for it: level 0 read back from GL.
				FOpenGLTexture* GLTexture = RenderInterface.CacheTexture( Base );
				if( GLTexture && GLTexture->TextureID && !GLTexture->IsCubemap )
				{
					GLuint Framebuffer = 0;
					FColor Read[2];
					appMemzero( Read, sizeof(Read) );
					glGenFramebuffers( 1, &Framebuffer );
					glBindFramebuffer( GL_FRAMEBUFFER, Framebuffer );
					glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, GLTexture->TextureID, 0 );
					GLenum Status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
					glPixelStorei( GL_PACK_ALIGNMENT, 1 );
					glReadPixels( 0, 0, 1, 1, GL_BGRA, GL_UNSIGNED_BYTE, &Read[0] );
					glReadPixels( GLTexture->CachedWidth/2, 0, 1, 1, GL_BGRA, GL_UNSIGNED_BYTE, &Read[1] );
					glBindFramebuffer( GL_FRAMEBUFFER, RenderInterface.SavedRenderTargetState.GetCurrentState()->Framebuffer ? RenderInterface.SavedRenderTargetState.GetCurrentState()->Framebuffer : MainFramebuffer );
					glDeleteFramebuffers( 1, &Framebuffer );
					Ar.Logf( TEXT("UC2GLDrv: GLTEXELS %s: GL texture %i, %ix%i, mips %i..%i, internal format 0x%04x, framebuffer status 0x%04x, level 0 first / middle texel: %i,%i,%i,%i / %i,%i,%i,%i"),
						Texture->GetName(), GLTexture->TextureID, GLTexture->CachedWidth, GLTexture->CachedHeight, GLTexture->CachedFirstMip, GLTexture->CachedNumMips, GLTexture->CachedGLFormat, Status,
						Read[0].R, Read[0].G, Read[0].B, Read[0].A, Read[1].R, Read[1].G, Read[1].B, Read[1].A );
				}
				else
					Ar.Logf( TEXT("UC2GLDrv: GLTEXELS %s: the device has no GL texture for it"), Texture->GetName() );
				Ar.Logf( TEXT("UC2GLDrv: GLTEXELS %s: first mip %i of %i, LODSet %i, clamp %i/%i:%s"), Texture->GetName(), Tex->GetFirstMip(), Tex->GetNumMips(), (INT)Texture->LODSet, (INT)Texture->UClampMode, (INT)Texture->VClampMode, *MipLine );
			}
		}
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("GLPROBE")) )
	{
		// GLPROBE <tag>: one log line with where the player is (verification of input in the
		// browser, where the native test channel of SDL2Drv does not exist: M11).
		while( *Cmd == ' ' )
			Cmd++;
		APlayerController*	Player	= LastViewport ? LastViewport->Actor : NULL;
		APawn*				Pawn	= Player ? Player->Pawn : NULL;
		AActor*				Where	= Pawn ? (AActor*)Pawn : (AActor*)Player;
		FString Line = FString::Printf( TEXT("UC2GLDrv: probe %s frame=%i level=%s pawn=%i loc=(%.1f,%.1f,%.1f) rot=(%i,%i,%i) size=%ix%i"),
			Cmd, FrameCounter,
			(Player && Player->GetLevel()) ? Player->GetLevel()->GetOuter()->GetName() : TEXT("None"),
			Pawn ? 1 : 0,
			Where ? Where->Location.X : 0.f, Where ? Where->Location.Y : 0.f, Where ? Where->Location.Z : 0.f,
			Player ? Player->Rotation.Pitch : 0, Player ? Player->Rotation.Yaw : 0, Player ? Player->Rotation.Roll : 0,
			MainWidth, MainHeight );
		debugf( NAME_Init, TEXT("%s"), *Line );
		Ar.Log( *Line );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("DISTORTIONCULLDISTANCE")) )
	{
		// DISTORTIONCULLDISTANCE [<units>]: the distance over which distortion fades out (O-7,
		// UC2GLMaterialState.cpp); 5000 is the Xbox's. No argument: show it. (fx-fixes)
		extern FLOAT GUC2GLDistortionCullDistance;
		if( appStrlen(Cmd) && appAtof(Cmd) > 0.f )
			GUC2GLDistortionCullDistance = appAtof(Cmd);
		Ar.Logf( TEXT("UC2GLDrv: distortion fades out over %.0f units (frame %i)"), GUC2GLDistortionCullDistance, FrameCounter );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("GLPIXTRACE")) )
	{
		// GLPIXTRACE <x> <y> [all]: log what the next frame draws over that pixel (UC2GLPixelTrace.cpp; fx-fixes).
		extern void UC2GL_PixelTraceArm( UUC2GLRenderDevice*, INT, INT, UBOOL );
		INT X = appAtoi( *ParseToken(Cmd,0) );
		INT Y = appAtoi( *ParseToken(Cmd,0) );
		UC2GL_PixelTraceArm( this, X, Y, ParseCommand(&Cmd,TEXT("ALL")) );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("FFPROGRAMS")) )
	{
		// FFPROGRAMS [0|1]: fixed-function texture stages as fragment programs or as GL's texture
		// environment (O-9; no argument: toggle). The next material set is compiled for it.
		UBOOL Wanted = appStrlen(Cmd) ? (appAtoi(Cmd) != 0) : !UseFixedFunctionPrograms;
		if( Wanted != UseFixedFunctionPrograms && ValidContext && !LockedViewport )
		{
			UseFixedFunctionPrograms = Wanted;
			UpdateTextureCaps();
			RenderInterface.InvalidateHardwareState();
		}
		Ar.Logf( TEXT("UC2GLDrv: fixed-function programs %s (frame %i)"), FFProgramsActive ? TEXT("on") : TEXT("off"), FrameCounter );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("GLMATCENSUS")) )
	{
		GMatCensusPending = 1;
		Ar.Logf( TEXT("UC2GLDrv: the material census runs at the start of the next frame; results in the log") );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("COMBINERTEST")) )
	{
		GCombinerTestPending = 1;
		Ar.Logf( TEXT("UC2GLDrv: the texture-stage test runs at the start of the next frame; results in the log") );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("POSTSELFTEST")) )
	{
		GSelfTestPending = 1;
		Ar.Logf( TEXT("UC2GLDrv: the post-process self-test runs at the start of the next frame; results in the log") );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("DOFSHIPPEDALPHA")) )
	{
		// DOFSHIPPEDALPHA [0|1]: UUCScreenDOF's blur alpha as the shipped binary read it
		// (docs/porting/analysis/runtime-port.md, "Deviation"); no argument: toggle.
		DOFShippedBlurAlpha = appStrlen(Cmd) ? (appAtoi(Cmd) != 0) : !DOFShippedBlurAlpha;
		Ar.Logf( TEXT("UC2GLDrv: depth-of-field blur alpha %s (frame %i)"), DOFShippedBlurAlpha ? TEXT("as the shipped binary read it") : TEXT("as script sets it"), FrameCounter );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("SUPPORTEDRESOLUTION")) )
	{
		Ar.Logf( TEXT("1") );
		return 1;
	}
	return 0;

	unguard;
}

/*-----------------------------------------------------------------------------
	Frames.
-----------------------------------------------------------------------------*/

FRenderInterface* UUC2GLRenderDevice::Lock( UViewport* Viewport, BYTE* HitData, INT* HitSize )
{
	guard(UUC2GLRenderDevice::Lock);

	check(Viewport);
	FrameCounter++;
	LockedViewport = Viewport;
	LastViewport = Viewport;

	// The window may have been resized without SetRes (SDL2Drv calls SetRes for that, but
	// keep the framebuffer honest either way).
	if( Viewport->SizeX != MainWidth || Viewport->SizeY != MainHeight )
		CreateMainFramebuffer( Viewport->SizeX, Viewport->SizeY );

	// M7: the client settings the Xbox build has and PC's ini and UClient::Init do not give,
	// applied the first time a frame is drawn with post-processing on.
	// - UClient::MaxDistortion (pixels of screen-space distortion; DefaultXbox.ini: 35) is what
	//   the engine gives its full-screen distortion quad. An ini without the key reads 0,
	//   which would leave every distortion without displacement.
	// - UClient::Init sets DynamicGamma = 1 and LuminosityBias = 1 under _XBOX. (DynamicGamma
	//   already reads True on this build, see the notebook; this makes it deliberate.)
	//   DYNAMICGAMMA 0 / LUMINOSITYBIAS on the console change them afterwards.
	if( PostProcessActive() && !XboxClientDefaultsApplied )
	{
		XboxClientDefaultsApplied = 1;
		UClient* Client = Viewport->GetOuterUClient();
		if( Client->MaxDistortion <= 0.f )
		{
			Client->MaxDistortion = 35.f;
			debugf( NAME_Init, TEXT("UC2GLDrv: MaxDistortion is not set in the client's ini section; using the Xbox value, 35") );
		}
		Client->DynamicGamma	= 1;
		Client->LuminosityBias	= 1.0f;
	}

	RenderInterface.Locked( Viewport, HitData, HitSize );

	if( GSelfTestPending && Viewport->Actor && Viewport->Actor->Level && --GSelfTestPending == 0 )
	{
		extern void UC2GL_RunPostProcessSelfTest( UUC2GLRenderDevice* RenDev, UViewport* Viewport );
		UC2GL_RunPostProcessSelfTest( this, Viewport );
	}
	if( GCombinerTestPending && Viewport->Actor && Viewport->Actor->Level && --GCombinerTestPending == 0 )
	{
		extern void UC2GL_RunCombinerTest( UUC2GLRenderDevice* RenDev, UViewport* Viewport );
		UC2GL_RunCombinerTest( this, Viewport );
	}
	if( GMatCensusPending && Viewport->Actor && Viewport->Actor->Level && --GMatCensusPending == 0 )
	{
		extern void UC2GL_RunMaterialCensus( UUC2GLRenderDevice* RenDev, UViewport* Viewport );
		UC2GL_RunMaterialCensus( this, Viewport );
	}

	return &RenderInterface;

	unguard;
}

void UUC2GLRenderDevice::Unlock( FRenderInterface* RI )
{
	guard(UUC2GLRenderDevice::Unlock);
	RenderInterface.Unlocked();
	LockedViewport = NULL;
	unguard;
}

void UUC2GLRenderDevice::Present( UViewport* Viewport )
{
	guard(UUC2GLRenderDevice::Present);

	if( !ValidContext )
		return;

	DrawPresentPass( Viewport );

	if( GShotPresented && GShotTimes.Num() && GFirstFrameTime != 0.0 && appSeconds() - GFirstFrameTime >= GShotTimes(0) )
	{
		INT W = MainWidth, H = MainHeight;
		BindingGetDrawableSize( Viewport, W, H );
		GPresentedPixels.Empty( W * H );
		GPresentedPixels.Add( W * H );
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, W, H, GL_BGRA, GL_UNSIGNED_BYTE, &GPresentedPixels(0) );
		glBindFramebuffer( GL_FRAMEBUFFER, MainFramebuffer );
		GPresentedWidth = W;
		GPresentedHeight = H;
	}

	if( CheckGLErrors )
	{
		if( CheckGLError( TEXT("frame") ) )
			GLErrorFrames++;
		if( FrameCounter % 600 == 0 )
		{
			debugf( NAME_Init, TEXT("UC2GLDrv: frame %i, %i GL errors so far"), FrameCounter, GLErrorCount );
			LogPixelShaderStats();
			extern void UC2GL_LogFFStats( UUC2GLRenderDevice* );
			UC2GL_LogFFStats( this );
		}
	}

	BindingSwap( Viewport );

	if( LogStats && (FrameCounter - StatFrame0 >= 300 || StatTime0 == 0.0) )
		LogFrameStats();

	// -glshot: the engine's own SHOT exec (ReadPixels -> appCreateBitmap).
	if( GFirstFrameTime == 0.0 )
		GFirstFrameTime = appSeconds();
	if( GPSToggleTimes.Num() && appSeconds() - GFirstFrameTime >= GPSToggleTimes(0) )
	{
		GPSToggleTimes.Remove(0);
		Exec( TEXT("PIXELSHADERS"), *GLog );
	}
	if( GPPToggleTimes.Num() && appSeconds() - GFirstFrameTime >= GPPToggleTimes(0) )
	{
		GPPToggleTimes.Remove(0);
		Exec( TEXT("POSTPROCESS"), *GLog );
	}
	if( GFFToggleTimes.Num() && appSeconds() - GFirstFrameTime >= GFFToggleTimes(0) )
	{
		GFFToggleTimes.Remove(0);
		Exec( TEXT("FFPROGRAMS"), *GLog );
	}
	while( GExecTimes.Num() && appSeconds() - GFirstFrameTime >= GExecTimes(0) )
	{
		FString Command = GExecCommands(0);
		debugf( NAME_Init, TEXT("UC2GLDrv: -glexec at %.1f s (frame %i): %s"), GExecTimes(0), FrameCounter, *Command );
		GExecTimes.Remove(0);
		GExecCommands.Remove(0);
		Viewport->Exec( *Command, *GLog );
	}
	if( GShotTimes.Num() )
	{
		if( appSeconds() - GFirstFrameTime >= GShotTimes(0) )
		{
			debugf( NAME_Init, TEXT("UC2GLDrv: -glshot at %.1f s (frame %i)"), GShotTimes(0), FrameCounter );
			// The counters at the moment of the picture (the periodic line may not come in a short run).
			if( CheckGLErrors )
			{
				debugf( NAME_Init, TEXT("UC2GLDrv: frame %i, %i GL errors so far"), FrameCounter, GLErrorCount );
				LogPixelShaderStats();
			}
			GShotTimes.Remove(0);
			Viewport->Exec( TEXT("SHOT"), *GLog );
		}
	}

	unguard;
}

//
// The main framebuffer as D3D's back buffer reads: top row first, B,G,R,A bytes (FColor).
// (Before the present pass: the ramp is the display's business on PC too.)
//
void UUC2GLRenderDevice::ReadPixels( UViewport* Viewport, FColor* Pixels )
{
	guard(UUC2GLRenderDevice::ReadPixels);

	if( !Viewport || !Pixels || !ValidContext )
		return;

	INT Width = Min( Viewport->SizeX, MainWidth ), Height = Min( Viewport->SizeY, MainHeight );
	appMemzero( Pixels, sizeof(FColor) * Viewport->SizeX * Viewport->SizeY );

	if( GPresentedWidth )
	{
		// -glshotpresented: the picture read after the present pass of this frame.
		INT W = Min( Width, GPresentedWidth ), H = Min( Height, GPresentedHeight );
		for( INT y=0; y<H; y++ )
			for( INT x=0; x<W; x++ )
			{
				FColor C = GPresentedPixels( (GPresentedHeight - 1 - y) * GPresentedWidth + x );
				C.A = 255;
				Pixels[ y * Viewport->SizeX + x ] = C;
			}
		GPresentedWidth = GPresentedHeight = 0;
		return;
	}

	ResolveMain( 1, 0 );
	glBindFramebuffer( GL_FRAMEBUFFER, MainTextureFramebuffer() );
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	TArray<FColor> Rows( Width * Height );
	glReadPixels( 0, 0, Width, Height, GL_BGRA, GL_UNSIGNED_BYTE, &Rows(0) );
	for( INT y=0; y<Height; y++ )
		for( INT x=0; x<Width; x++ )
		{
			FColor C = Rows( (Height - 1 - y) * Width + x );
			C.A = 255;
			if( GShotRamp && GammaRampActive && PostProcessActive() )
			{
				C.R = GLastGammaRamp[C.R];
				C.G = GLastGammaRamp[C.G];
				C.B = GLastGammaRamp[C.B];
			}
			Pixels[ y * Viewport->SizeX + x ] = C;
		}
	glBindFramebuffer( GL_FRAMEBUFFER, RenderInterface.SavedRenderTargetState.GetCurrentState()->Framebuffer ? RenderInterface.SavedRenderTargetState.GetCurrentState()->Framebuffer : MainFramebuffer );

	unguard;
}

void UUC2GLRenderDevice::SetEmulationMode( EHardwareEmulationMode Mode )
{
}

FRenderCaps* UUC2GLRenderDevice::GetRenderCaps()
{
	// The caps stay those of M5: the engine's terrain code picks its render method from them
	// (one layer per pass, no pixel shader). PS_Terrain3Layer is translated; asking for it
	// means MaxSimultaneousTerrainLayers = 3 here and the Xbox mask constants in
	// SetTerrainMaterial (see docs/porting/analysis/shaders.md, deferred).
	RenderCaps.HardwareTL					= 1;
	RenderCaps.PixelShaderVersion			= 0;
	RenderCaps.MaxSimultaneousTerrainLayers	= 1;
	// M7: the one capability that lifts the engine's and the game's Xbox-only post-process
	// paths (docs/porting/analysis/postprocess.md, "Engine gates").
	RenderCaps.XboxPostProcess				= PostProcessActive() ? 1 : 0;
	RenderCaps.ShippedDOFBlurAlpha			= DOFShippedBlurAlpha ? 1 : 0;
	return &RenderCaps;
}


/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
