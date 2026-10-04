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

void* UC2GL_GetProcAddress( const char* Name )
{
	return SDL_GL_GetProcAddress( Name );
}

// -glshot=<seconds>[,<seconds>...]: run the engine's SHOT exec at those times after the
// first frame (verification; the files are System/ShotNNNNN.bmp).
static TArray<FLOAT>	GShotTimes;
static TArray<FLOAT>	GPSToggleTimes;
static TArray<FLOAT>	GPPToggleTimes;
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
static BYTE				GLastGammaRamp[256];
void UC2GL_RememberGammaRamp( const BYTE* Ramp ) { appMemcpy( GLastGammaRamp, Ramp, 256 ); }
static DOUBLE			GFirstFrameTime = 0.0;

/*-----------------------------------------------------------------------------
	UUC2GLRenderDevice.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(UUC2GLRenderDevice);


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
	FrameCounter			= 0;
	NumTextureUnits			= 0;
	MaxAnisotropy			= 1.f;
	ValidContext			= 0;
	GLMajorVersion			= 0;
	GLMinorVersion			= 0;
	CubemapTextureAddressing= D3DTADDRESS_CLAMP;
	IsGeForce				= 0;
	MainFramebuffer			= 0;
	MainColorTexture		= 0;
	MainDepthStencil		= 0;
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
	new(GetClass(),TEXT("PrecompileVertexShaders"),RF_Public)UBoolProperty( CPP_PROPERTY(PrecompileVertexShaders), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("UsePostProcess"),		RF_Public)UBoolProperty	( CPP_PROPERTY(UsePostProcess		), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("DOFShippedBlurAlpha"),	RF_Public)UBoolProperty	( CPP_PROPERTY(DOFShippedBlurAlpha	), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("LevelOfAnisotropy"),	RF_Public)UIntProperty	( CPP_PROPERTY(LevelOfAnisotropy	), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("MaxTextureUnits"),		RF_Public)UIntProperty	( CPP_PROPERTY(MaxTextureUnits		), TEXT("Options"), CPF_Config );
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
	PrecompileVertexShaders	= 1;
	UsePostProcess			= 1;
	DOFShippedBlurAlpha		= 0;
	LevelOfAnisotropy		= 1;
	MaxTextureUnits			= 8;
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
	ProcAddress = UC2GL_GetProcAddress( Name );
	if( !ProcAddress && Supports )
	{
		debugf( NAME_Init, TEXT("UC2GLDrv: missing function '%s' for '%s'"), appFromAnsi(Name), appFromAnsi(SupportName) );
		Supports = 0;
	}
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

	#define GL_EXT(name)
	#define GL_PROC(ext,ret,func,parms) FindProc( *(void**)&func, #func, #ext, SUPPORTS##ext );
	#include "UC2GLFuncs.h"
	#undef GL_EXT
	#undef GL_PROC

	unguard;
}

UBOOL UUC2GLRenderDevice::ContextAlive()
{
	return ValidContext && SDL_GL_GetCurrentContext() != NULL;
}

//
// Drain glGetError; returns whether there was any. Used per frame with CheckGLErrors.
//
UBOOL UUC2GLRenderDevice::CheckGLError( const TCHAR* Tag, UBOOL Log )
{
	UBOOL Any = 0;
	GLenum Error;
	while( (Error = glGetError()) != GL_NO_ERROR )
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

	debugf( NAME_Init, TEXT("Initializing UC2GLDrv (OpenGL, fixed function; binding: desktop GL through SDL)") );

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

	// The same for render-target post-processing (M7): -nopostprocess / -postprocess, and
	// -glpptoggle=<s>,... flips UsePostProcess at those times (console: POSTPROCESS [0|1]).
	if( ParseParam( appCmdLine(), TEXT("nopostprocess") ) )
		UsePostProcess = 0;
	if( ParseParam( appCmdLine(), TEXT("postprocess") ) )
		UsePostProcess = 1;
	ParseTimeList( TEXT("glpptoggle="), GPPToggleTimes );
	GShotRamp = ParseParam( appCmdLine(), TEXT("glshotramp") );
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
		if( NullTextureID )
			glDeleteTextures( 1, &NullTextureID );
		NullTextureID = 0;
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

	// The window's own framebuffer only receives the present pass.
	SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 0 );
	SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 0 );
	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

	// SDL2Drv creates the GL window and context for BLIT_OpenGL.
	if( !Viewport->ResizeViewport( (Fullscreen ? BLIT_Fullscreen : 0) | BLIT_OpenGL, NewX, NewY, bSaveSize ) )
	{
		debugf( NAME_Init, TEXT("UC2GLDrv: the viewport could not provide an OpenGL context") );
		return 0;
	}

	if( !ValidContext )
	{
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

		// Capabilities.
		GLint Units = 0, MaxSize = 0;
		glGetIntegerv( GL_MAX_TEXTURE_UNITS, &Units );
		glGetIntegerv( GL_MAX_TEXTURE_SIZE, &MaxSize );
		if( MaxTextureUnits < 1 )
			MaxTextureUnits = MAX_STAGES;
		NumTextureUnits = Clamp<INT>( Min<INT>( Units, MaxTextureUnits ), 2, MAX_STAGES );
		DeviceCaps.MaxSimultaneousTextures	= NumTextureUnits;
		DeviceCaps.MaxTextureBlendStages	= NumTextureUnits;
		DeviceCaps.TextureOpCaps			= SUPPORTS_GL_ATI_texture_env_combine3 ? (D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR | D3DTEXOPCAPS_MULTIPLYADD) : 0;
		DeviceCaps.PrimitiveMiscCaps		= 0;	// no D3DPMISCCAPS_TSSARGTEMP: GL combiners have no temp register
		DeviceCaps.MaxTextureSize			= MaxSize > 0 ? MaxSize : 2048;
		if( SUPPORTS_GL_EXT_texture_filter_anisotropic )
			glGetFloatv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &MaxAnisotropy );
		LevelOfAnisotropy = Clamp<INT>( LevelOfAnisotropy, 1, Max<INT>(1,(INT)MaxAnisotropy) );

		debugf( NAME_Init, TEXT("UC2GLDrv: %i texture units (GL reports %i), max texture %i, S3TC %s, combine3 %s, anisotropy %i of %i"),
			NumTextureUnits, Units, DeviceCaps.MaxTextureSize,
			SUPPORTS_GL_EXT_texture_compression_s3tc ? TEXT("yes") : TEXT("no"),
			SUPPORTS_GL_ATI_texture_env_combine3 ? TEXT("yes") : TEXT("no"),
			LevelOfAnisotropy, (INT)MaxAnisotropy );
		if( !SUPPORTS_GL_EXT_texture_compression_s3tc )
			debugf( NAME_Init, TEXT("UC2GLDrv: no S3TC; DXT textures are decompressed on upload") );

		SupportsCubemaps = UseCubemaps;

		if( !CreatePresentProgram() )
			return 0;

		// A black opaque texture for stages that have no texture.
		FColor Black(0,0,0,255);
		glGenTextures( 1, &NullTextureID );
		glBindTexture( GL_TEXTURE_2D, NullTextureID );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_BGRA, GL_UNSIGNED_BYTE, &Black );

		// Render-target post-processing (M7): the copy programs. Before the pixel shaders:
		// which of those are compiled depends on it.
		InitPostProcess();

		// Programmable materials (M6): fragment shaders of every integrated pixel shader,
		// vertex shaders of the VertexShaderConfigs.ini list.
		InitPixelShaders();
	}

	if( !CreateMainFramebuffer( Viewport->SizeX, Viewport->SizeY ) )
		return 0;


	SDL_GL_SetSwapInterval( UseVSync ? 1 : 0 );

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
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0 );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, Width, Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );

	// M7: depth and stencil live in a texture, not a renderbuffer, so the depth can be read
	// by a shader (UUC2GLRenderDevice::UpdateSampledTexture, depth of field). GL 3.0 /
	// EXT_packed_depth_stencil + ARB_depth_texture; on WebGL this needs WEBGL_depth_texture
	// (M11: without it the depth colour has to come from a pass that writes depth as colour).
	glGenTextures( 1, &MainDepthStencil );
	glBindTexture( GL_TEXTURE_2D, MainDepthStencil );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0 );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, Width, Height, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL );

	glGenFramebuffers( 1, &MainFramebuffer );
	glBindFramebuffer( GL_FRAMEBUFFER, MainFramebuffer );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, MainColorTexture, 0 );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, MainDepthStencil, 0 );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, MainDepthStencil, 0 );

	GLenum Status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
	if( Status != GL_FRAMEBUFFER_COMPLETE )
	{
		debugf( NAME_Init, TEXT("UC2GLDrv: main framebuffer %ix%i incomplete (0x%04x)"), Width, Height, Status );
		return 0;
	}

	MainWidth	= Width;
	MainHeight	= Height;
	debugf( NAME_Init, TEXT("UC2GLDrv: main framebuffer %ix%i (RGBA8 texture, depth 24 + stencil 8 texture)"), Width, Height );
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

void UUC2GLRenderDevice::DestroyMainFramebuffer()
{
	// The snapshot and the depth colour have the main framebuffer's size.
	DestroyPostProcessTargets();
	if( !ContextAlive() )
	{
		MainFramebuffer = MainDepthStencil = MainColorTexture = 0;
		MainWidth = MainHeight = 0;
		return;
	}
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	if( MainFramebuffer )
		glDeleteFramebuffers( 1, &MainFramebuffer );
	if( MainDepthStencil )
		glDeleteTextures( 1, &MainDepthStencil );
	if( MainColorTexture )
		glDeleteTextures( 1, &MainColorTexture );
	MainFramebuffer = MainDepthStencil = MainColorTexture = 0;
	MainWidth = MainHeight = 0;
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
static const char* GPresentFragmentShader =
	"uniform sampler2D uScene;\n"
	"uniform sampler2D uRamp;\n"
	"uniform float uUseRamp;\n"
	"uniform vec4 uGamma;\n"
	"varying vec2 vTexCoord;\n"
	"void main()\n"
	"{\n"
	"	vec3 Scene = texture2D( uScene, vTexCoord ).rgb;\n"
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
#if UC2GL_GLES
	const char* Prologue = "#version 100\nprecision mediump float;\n";
#else
	const char* Prologue = "#version 120\n";
#endif
	const char* Sources[2] = { Prologue, Body };
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
	return 1;

	unguard;
}

void UUC2GLRenderDevice::DrawPresentPass( UViewport* Viewport )
{
	guard(UUC2GLRenderDevice::DrawPresentPass);

	SDL_Window* Window = (SDL_Window*)Viewport->GetWindow();
	INT DrawableX = MainWidth, DrawableY = MainHeight;
	if( Window )
		SDL_GL_GetDrawableSize( Window, &DrawableX, &DrawableY );

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
	glUniform1i( PresentRampLoc, 1 );
	if( UseRamp )
	{
		glActiveTexture( GL_TEXTURE1 );
		glBindTexture( GL_TEXTURE_2D, GammaRampTexture );
		glActiveTexture( GL_TEXTURE0 );
	}

	static const FLOAT Quad[8] = { -1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f };
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, 0, Quad );
	glEnableVertexAttribArray( 0 );
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	glDisableVertexAttribArray( 0 );
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
	for( INT s=0; s<NumTextureUnits; s++ )
	{
		glActiveTexture( GL_TEXTURE0 + s );
		glClientActiveTexture( GL_TEXTURE0 + s );
		glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE );
		glTexGenfv( GL_S, GL_EYE_PLANE, PlaneS );
		glTexGenfv( GL_T, GL_EYE_PLANE, PlaneT );
		glTexGenfv( GL_R, GL_EYE_PLANE, PlaneR );
		glTexGenfv( GL_Q, GL_EYE_PLANE, PlaneQ );
		glDisable( GL_TEXTURE_GEN_S );
		glDisable( GL_TEXTURE_GEN_T );
		glDisable( GL_TEXTURE_GEN_R );
		glDisable( GL_TEXTURE_GEN_Q );
		glDisable( GL_TEXTURE_2D );
		glDisable( GL_TEXTURE_CUBE_MAP );
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

	if( CheckGLErrors )
	{
		if( CheckGLError( TEXT("frame") ) )
			GLErrorFrames++;
		if( FrameCounter % 600 == 0 )
		{
			debugf( NAME_Init, TEXT("UC2GLDrv: frame %i, %i GL errors so far"), FrameCounter, GLErrorCount );
			LogPixelShaderStats();
		}
	}

	SDL_GL_SwapWindow( (SDL_Window*)Viewport->GetWindow() );

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

	glBindFramebuffer( GL_FRAMEBUFFER, MainFramebuffer );
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
