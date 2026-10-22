/*=============================================================================
	UC2GL4ESDrv.cpp: the GL4ES binding of UC2GLDrv.

	Native (Linux, Android): SDL2Drv creates the window and, from the attributes set here, an
	OpenGL ES 3.0 context, ES 2.0 as the fallback (EGL or GLX, SDL's choice; EGL on Android); gl4es
	is handed SDL's lookup.
	Web: under PROXY_TO_PTHREAD the canvas belongs to the engine's worker
	(OFFSCREENCANVASES_TO_PTHREAD), and SDL's GL path is EGL, which Emscripten runs on the
	browser's main thread, where the canvas no longer is (Emscripten issue 24792). So SDL
	keeps the window and the input only, and the WebGL 2 (or 1) context is created here, directly
	on the worker; the
	frame is presented with emscripten_webgl_commit_frame (the link has
	-sOFFSCREEN_FRAMEBUFFER, without which that call does nothing).
=============================================================================*/

#if defined(__EMSCRIPTEN__)
// Before the engine headers: emscripten/html5.h pulls in <time.h>, and UnFile.h defines
// clock(Timer) as a macro.
#include <emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>
#endif

#if defined(__ANDROID__)
#include <string.h>
#include <sys/system_properties.h>	// __system_property_get: is this the emulator
#endif

#include "UC2GL4ESDrv.h"
#include "SDL.h"
#include <stdlib.h>

// gl4es (include/gl4esinit.h), declared by hand: its GL header must not meet the driver's.
extern "C"
{
	void set_getprocaddress( void* (*NewProcAddress)( const char* ) );
	void initialize_gl4es( void );
	void* gl4es_GetProcAddress( const char* Name );
	// include/gl4estranscode.h
	unsigned int gl4es_dxt_upload_format( unsigned int DXTFormat );
	int gl4es_dxt_upload_size( unsigned int Format, int Width, int Height );
	void* gl4es_dxt_transcode( unsigned int DXTFormat, unsigned int Format, int Width, int Height, const void* Data );
}

/*-----------------------------------------------------------------------------
	Package and class.
-----------------------------------------------------------------------------*/

IMPLEMENT_PACKAGE(UC2GL4ESDrv);
IMPLEMENT_CLASS(UUC2GL4ESRenderDevice);

#if defined(__EMSCRIPTEN__)
static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE GWebGLContext = 0;
#endif

UUC2GL4ESRenderDevice::UUC2GL4ESRenderDevice()
{
	IsGLES = 1;
}

void UUC2GL4ESRenderDevice::StaticConstructor()
{
	// UUC2GLRenderDevice::StaticConstructor declared the shared properties on its class and set
	// the compiled defaults, which this class's default object starts from. This class adds GLESVersion.
	new(GetClass(),TEXT("GLESVersion"),RF_Public)UIntProperty( CPP_PROPERTY(GLESVersion), TEXT("Options"), CPF_Config );
	GLESVersion = 3;
}

// The version asked for: 3 unless the ini says 2.
static INT RequestedGLES( INT GLESVersion )
{
	return GLESVersion == 2 ? 2 : 3;
}

const TCHAR* UUC2GL4ESRenderDevice::BindingName()
{
	INT Major = GLESMajor ? GLESMajor : RequestedGLES( GLESVersion );
#if defined(__EMSCRIPTEN__)
	return Major >= 3 ? TEXT("WebGL 2 through GL4ES") : TEXT("WebGL 1 through GL4ES");
#else
	return Major >= 3 ? TEXT("OpenGL ES 3 through GL4ES") : TEXT("OpenGL ES 2 through GL4ES");
#endif
}

/*-----------------------------------------------------------------------------
	Context.
-----------------------------------------------------------------------------*/

static void SetESContextAttributes( INT Major )
{
#if !defined(__EMSCRIPTEN__)
	// The window's own framebuffer only receives the present pass.
	SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
	SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 0 );
	SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 0 );
	SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, Major );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 0 );
#endif
}

void UUC2GL4ESRenderDevice::BindingSetContextAttributes()
{
	// Once a context exists its version stays (GL4ES is initialised for it once).
	SetESContextAttributes( GLESMajor ? GLESMajor : RequestedGLES( GLESVersion ) );
}

UBOOL UUC2GL4ESRenderDevice::BindingCreateContext( UViewport* Viewport, UBOOL Fullscreen, INT NewX, INT NewY, UBOOL bSaveSize )
{
#if defined(__EMSCRIPTEN__)
	// The engine draws at the canvas's size in DEVICE pixels, whatever the ini or the caller
	// asks for: the canvas's CSS box is the page's business, and a frame of another size would
	// be resampled by the browser (the ini's 640x480 stretched over a 1024x768 box).
	// Both calls are answered by the browser's main thread.
	{
		double CssW = 0.0, CssH = 0.0;
		if( emscripten_get_element_css_size( "#canvas", &CssW, &CssH ) == EMSCRIPTEN_RESULT_SUCCESS )
		{
			double	Ratio	= emscripten_get_device_pixel_ratio();
			// Odd widths too (the viewport does not round the width to even on the web): a 1023-CSS-pixel
			// box at 125 % is 1279 device pixels, not 1278.
			INT		W		= (INT)(CssW * Ratio + 0.5),
					H		= (INT)(CssH * Ratio + 0.5);
			if( W >= 320 && H >= 200 && (W != NewX || H != NewY) )
			{
				debugf( NAME_Init, TEXT("UC2GL4ESDrv: the canvas is %ix%i device pixels (CSS %.0fx%.0f, ratio %.2f): drawing at that size instead of %ix%i"), W, H, CssW, CssH, Ratio, NewX, NewY );
				NewX = W;
				NewY = H;
			}
		}
	}
	// SDL: the window (the canvas) and its size, no GL.
	if( !Viewport->ResizeViewport( Fullscreen ? BLIT_Fullscreen : 0, NewX, NewY, bSaveSize ) )
		return 0;
	if( GWebGLContext > 0 )
	{
		emscripten_webgl_make_context_current( GWebGLContext );
		return 1;
	}
	EmscriptenWebGLContextAttributes Attrs;
	emscripten_webgl_init_context_attributes( &Attrs );
	// WebGL 2 = OpenGL ES 3.0, through GL4ES' ES 3 back end (our fork); WebGL 1 = OpenGL ES 2
	// where the browser has no WebGL 2 or the ini asks for it (GLESVersion=2).
	INT Major = RequestedGLES( GLESVersion );
	Attrs.majorVersion					= Major >= 3 ? 2 : 1;
	Attrs.minorVersion					= 0;
	// The canvas only receives the present pass: the scene has its own framebuffer object
	// with depth and stencil, and the picture is opaque.
	Attrs.alpha							= 0;
	Attrs.depth							= 0;
	Attrs.stencil						= 0;
	Attrs.antialias						= 0;
	Attrs.premultipliedAlpha			= 0;
	Attrs.preserveDrawingBuffer			= 0;
	Attrs.powerPreference				= EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
	Attrs.enableExtensionsByDefault		= 1;
	// The frame is ours to present (emscripten_webgl_commit_frame in BindingSwap).
	Attrs.explicitSwapControl			= 1;
	Attrs.renderViaOffscreenBackBuffer	= 1;
	GWebGLContext = emscripten_webgl_create_context( "#canvas", &Attrs );
	if( GWebGLContext <= 0 && Major >= 3 )
	{
		debugf( NAME_Init, TEXT("UC2GL4ESDrv: no WebGL 2 context on the engine's thread (result %i): trying WebGL 1"), (INT)GWebGLContext );
		Major = 2;
		Attrs.majorVersion = 1;
		GWebGLContext = emscripten_webgl_create_context( "#canvas", &Attrs );
	}
	if( GWebGLContext <= 0 )
	{
		debugf( NAME_Init, TEXT("UC2GL4ESDrv: could not create a WebGL context on the engine's thread (result %i)"), (INT)GWebGLContext );
		GWebGLContext = 0;
		return 0;
	}
	GLESMajor = Major;
	if( emscripten_webgl_make_context_current( GWebGLContext ) != EMSCRIPTEN_RESULT_SUCCESS )
	{
		debugf( NAME_Init, TEXT("UC2GL4ESDrv: could not make the WebGL context current") );
		return 0;
	}
	debugf( NAME_Init, TEXT("UC2GL4ESDrv: WebGL %i context created on the engine's thread (handle %i; alpha 0, depth 0, stencil 0, antialias 0, explicit swap)"), Major >= 3 ? 2 : 1, (INT)GWebGLContext );
	return 1;
#else
	// SDL2Drv creates the GL window and context for BLIT_OpenGL, from the attributes above.
	if( Viewport->ResizeViewport( (Fullscreen ? BLIT_Fullscreen : 0) | BLIT_OpenGL, NewX, NewY, bSaveSize ) )
	{
		if( !GLESMajor )
		{
			GLESMajor = RequestedGLES( GLESVersion );
			debugf( NAME_Init, TEXT("UC2GL4ESDrv: OpenGL ES %i.0 context requested"), GLESMajor );
		}
		return 1;
	}
	if( GLESMajor || RequestedGLES( GLESVersion ) < 3 )
		return 0;
	debugf( NAME_Init, TEXT("UC2GL4ESDrv: no OpenGL ES 3.0 context: trying OpenGL ES 2.0") );
	SetESContextAttributes( 2 );
	if( !Viewport->ResizeViewport( (Fullscreen ? BLIT_Fullscreen : 0) | BLIT_OpenGL, NewX, NewY, bSaveSize ) )
		return 0;
	GLESMajor = 2;
	return 1;
#endif
}

UBOOL UUC2GL4ESRenderDevice::BindingContextAlive()
{
#if defined(__EMSCRIPTEN__)
	return GWebGLContext > 0 && emscripten_webgl_get_current_context() == GWebGLContext;
#else
	return SDL_GL_GetCurrentContext() != NULL;
#endif
}

void UUC2GL4ESRenderDevice::BindingGetDrawableSize( UViewport* Viewport, INT& Width, INT& Height )
{
#if defined(__EMSCRIPTEN__)
	int W = 0, H = 0;
	if( GWebGLContext > 0 && emscripten_webgl_get_drawing_buffer_size( GWebGLContext, &W, &H ) == EMSCRIPTEN_RESULT_SUCCESS && W > 0 && H > 0 )
	{
		Width	= W;
		Height	= H;
	}
#else
	SDL_Window* Window = (SDL_Window*)Viewport->GetWindow();
	if( Window )
		SDL_GL_GetDrawableSize( Window, &Width, &Height );
#endif
}

void UUC2GL4ESRenderDevice::BindingSetSwapInterval( INT Interval )
{
#if !defined(__EMSCRIPTEN__)
	SDL_GL_SetSwapInterval( Interval );
#endif
}

void UUC2GL4ESRenderDevice::BindingSwap( UViewport* Viewport )
{
#if defined(__EMSCRIPTEN__)
	emscripten_webgl_commit_frame();
#else
	SDL_GL_SwapWindow( (SDL_Window*)Viewport->GetWindow() );
#endif
}

/*-----------------------------------------------------------------------------
	gl4es.
-----------------------------------------------------------------------------*/

// The library under gl4es.
static void* UC2GL4ES_RawGetProcAddress( const char* Name )
{
#if defined(__EMSCRIPTEN__)
	return emscripten_webgl_get_proc_address( Name );
#else
	return SDL_GL_GetProcAddress( Name );
#endif
}

UBOOL UUC2GL4ESRenderDevice::BindingInit()
{
	static UBOOL Initialized = 0;
	if( Initialized )
		return 1;

	// gl4es is configured through its environment only; the ones that matter most:
	// USEVBO: without it gl4es hands GLES client-side pointers for data that is in buffer
	// objects, and WebGL has no client-side arrays (Emscripten then copies every vertex
	// range before every draw). BATCH 0: its draw-call merging costs more than it saves.
	// GL4ES' OpenGL ES 3 back end (the fork's) on an OpenGL ES 3 / WebGL 2 context.
	setenv( "LIBGL_ES",				GLESMajor >= 3 ? "3" : "2", 1 );
	// Fragment highp: GL4ES' own fixed-function shaders are
	// mediump by default and samplers lowp; with this every shader is highp where the hardware
	// has it (always on OpenGL ES 3 / WebGL 2).
	setenv( "LIBGL_FORCEHIGHP",		"1", 1 );
	// GL4ES' ES 2 shader conversion inserts int overloads of pow/max/min/clamp/mod for desktop GLSL
	// that mixes int and float; the driver's shaders are written in the GLSL ES subset and need none.
	// Some OpenGL ES drivers reject those overloads (NVIDIA's); GL4ES' ES 3 path does not insert
	// them anyway (GLSL ES 3.00 forbids it).
	setenv( "LIBGL_NOINTOVLHACK",	"1", 1 );
	setenv( "LIBGL_NOBANNER",		"1", 1 );
	setenv( "LIBGL_NOERROR",		"0", 1 );	// glGetError keeps working (CheckGLErrors)
	setenv( "LIBGL_USEVBO",			"1", 1 );
	setenv( "LIBGL_BATCH",			"0", 1 );
	setenv( "LIBGL_AVOID16BITS",	"1", 1 );	// no 16-bit downsampling of textures
	setenv( "LIBGL_MIPMAP",			"0", 1 );
	setenv( "LIBGL_FPS",			"0", 1 );
	setenv( "LIBGL_SILENTSTUB",		"1", 1 );
	setenv( "LIBGL_NOTEXRECT",		"1", 1 );
	// No "precompiled shader archive": where the hardware has program binaries gl4es keeps its
	// generated fixed-function programs in ~/.gl4es.psa. The device does not write into the
	// user's home directory, and natively it builds its programs the way the browser has to
	// (WebGL has no program binaries).
	setenv( "LIBGL_NOPSA",			"1", 1 );
	// No framebuffer unbind and rebind around a draw that samples a texture attached to the bound framebuffer: gl4es
	// switches that workaround on by itself for ARM and Imagination GPUs (Android's Mali), and its check indexes the
	// bound textures with the texture unit read back from the driver's program, out of range, a crash on the first
	// draw of a match. Off, every target runs gl4es the same way.
	setenv( "LIBGL_FBOUNBIND",		"0", 1 );
	// (gl4es' own LIBGL_DBGSHADERCONV=15 / LIBGL_LOGSHADERERROR=1 in the environment debug its shader conversion.)
#if defined(__ANDROID__)
	// The Android emulator's OpenGL ES translator advertises GL_EXT_texture_format_BGRA8888 and then uploads BGRA
	// textures with the channels and alpha wrong: there gl4es swizzles BGRA to RGBA itself. Devices' drivers do it right.
	{
		char Qemu[PROP_VALUE_MAX] = { 0 }, Hardware[PROP_VALUE_MAX] = { 0 };
		__system_property_get( "ro.kernel.qemu", Qemu );
		__system_property_get( "ro.hardware", Hardware );
		if( Qemu[0] == '1' || strstr( Hardware, "goldfish" ) || strstr( Hardware, "ranchu" ) )
		{
			setenv( "LIBGL_NOBGRA", "1", 1 );
			debugf( NAME_Init, TEXT("UC2GL4ESDrv: the Android emulator: gl4es converts BGRA textures itself (LIBGL_NOBGRA)") );
		}
	}
#endif

	set_getprocaddress( UC2GL4ES_RawGetProcAddress );
	initialize_gl4es();
	Initialized = 1;
	debugf( NAME_Init, TEXT("UC2GL4ESDrv: gl4es initialised (GLES%i back end, real buffer objects, no batching, highp everywhere)"), GLESMajor >= 3 ? 3 : 2 );
	return 1;
}

void* UUC2GL4ESRenderDevice::BindingGetProcAddress( const char* Name )
{
	return gl4es_GetProcAddress( Name );
}

void* UUC2GL4ESRenderDevice::BindingGetRawProcAddress( const char* Name )
{
	return UC2GL4ES_RawGetProcAddress( Name );
}

// gl4es' choice for DXT: as it is where the hardware has S3TC, else ASTC 4x4 or ETC2, else none.
GLenum UUC2GL4ESRenderDevice::BindingDXTUploadFormat( GLenum Format )
{
	return gl4es_dxt_upload_format( Format );
}

void* UUC2GL4ESRenderDevice::BindingTranscodeDXT( GLenum Format, GLenum UploadFormat, INT Width, INT Height, const BYTE* Data, INT& Size )
{
	Size = gl4es_dxt_upload_size( UploadFormat, Width, Height );
	return gl4es_dxt_transcode( Format, UploadFormat, Width, Height, Data );
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
