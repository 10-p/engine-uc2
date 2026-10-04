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
static DOUBLE			GFirstFrameTime = 0.0;

/*-----------------------------------------------------------------------------
	UUC2GLRenderDevice.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(UUC2GLRenderDevice);

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
	FString Shots;
	if( Parse( appCmdLine(), TEXT("glshot="), Shots ) )
	{
		GShotTimes.Empty();
		TArray<FString> Parts;
		Shots.ParseIntoArray( TEXT(","), &Parts );
		for( INT i=0; i<Parts.Num(); i++ )
			GShotTimes.AddItem( appAtof(*Parts(i)) );
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

	glGenRenderbuffers( 1, &MainDepthStencil );
	glBindRenderbuffer( GL_RENDERBUFFER, MainDepthStencil );
	glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, Width, Height );

	glGenFramebuffers( 1, &MainFramebuffer );
	glBindFramebuffer( GL_FRAMEBUFFER, MainFramebuffer );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, MainColorTexture, 0 );
	glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, MainDepthStencil );
	glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, MainDepthStencil );

	GLenum Status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
	if( Status != GL_FRAMEBUFFER_COMPLETE )
	{
		debugf( NAME_Init, TEXT("UC2GLDrv: main framebuffer %ix%i incomplete (0x%04x)"), Width, Height, Status );
		return 0;
	}

	MainWidth	= Width;
	MainHeight	= Height;
	debugf( NAME_Init, TEXT("UC2GLDrv: main framebuffer %ix%i (RGBA8 texture, depth 24 + stencil 8)"), Width, Height );

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
		glDeleteRenderbuffers( 1, &MainDepthStencil );
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
static const char* GPresentFragmentShader =
	"uniform sampler2D uScene;\n"
	"uniform vec4 uGamma;\n"
	"varying vec2 vTexCoord;\n"
	"void main()\n"
	"{\n"
	"	vec3 Scene = texture2D( uScene, vTexCoord ).rgb;\n"
	"	vec3 Ramp = clamp( uGamma.y * pow( Scene, vec3( uGamma.x ) ) + vec3( uGamma.z ), 0.0, 1.0 );\n"
	"	gl_FragColor = vec4( mix( Scene, Ramp, uGamma.w ), 1.0 );\n"
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

	RenderInterface.Locked( Viewport, HitData, HitSize );

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
			debugf( NAME_Init, TEXT("UC2GLDrv: frame %i, %i GL errors so far"), FrameCounter, GLErrorCount );
	}

	SDL_GL_SwapWindow( (SDL_Window*)Viewport->GetWindow() );

	// -glshot: the engine's own SHOT exec (ReadPixels -> appCreateBitmap).
	if( GShotTimes.Num() )
	{
		if( GFirstFrameTime == 0.0 )
			GFirstFrameTime = appSeconds();
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
	// R1: fixed function only, so the engine takes its non-pixel-shader paths (one terrain
	// layer at a time). M6 raises PixelShaderVersion when the .psh translations exist.
	RenderCaps.HardwareTL					= 1;
	RenderCaps.PixelShaderVersion			= 0;
	RenderCaps.MaxSimultaneousTerrainLayers	= 1;
	return &RenderCaps;
}

void UUC2GLRenderDevice::LoadVertexShaderEnum( QWORD VertexShaderEnum )
{
	// M6: precompile the spliced GLSL vertex shader for this enum. Fixed function needs nothing.
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
