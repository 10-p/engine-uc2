/*=============================================================================
	UC2GL4ESDrv.cpp: the GL4ES binding of UC2GLDrv (M11).

	Native (Linux): SDL2Drv creates the window and, from the attributes set here, an OpenGL
	ES 2.0 context (EGL or GLX, SDL's choice); gl4es is handed SDL's lookup.
	Web: under PROXY_TO_PTHREAD the canvas belongs to the engine's worker
	(OFFSCREENCANVASES_TO_PTHREAD), and SDL's GL path is EGL, which Emscripten runs on the
	browser's main thread, where the canvas no longer is (Emscripten issue 24792). So SDL
	keeps the window and the input only, and the WebGL 1 context is created here, directly
	on the worker (engine-ut2004's arrangement, there inside its SDL2Viewport.cpp); the
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

#include "UC2GL4ESDrv.h"
#include "SDL.h"
#include <stdlib.h>

// gl4es (include/gl4esinit.h), declared by hand: its GL header must not meet the driver's.
extern "C"
{
	void set_getprocaddress( void* (*NewProcAddress)( const char* ) );
	void initialize_gl4es( void );
	void* gl4es_GetProcAddress( const char* Name );
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
	// Nothing: UUC2GLRenderDevice::StaticConstructor declared the properties on its class and
	// set the compiled defaults, which this class's default object starts from.
}

const TCHAR* UUC2GL4ESRenderDevice::BindingName()
{
#if defined(__EMSCRIPTEN__)
	return TEXT("WebGL 1 through GL4ES");
#else
	return TEXT("OpenGL ES 2 through GL4ES");
#endif
}

/*-----------------------------------------------------------------------------
	Context.
-----------------------------------------------------------------------------*/

void UUC2GL4ESRenderDevice::BindingSetContextAttributes()
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
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 2 );
	SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 0 );
#endif
}

UBOOL UUC2GL4ESRenderDevice::BindingCreateContext( UViewport* Viewport, UBOOL Fullscreen, INT NewX, INT NewY, UBOOL bSaveSize )
{
#if defined(__EMSCRIPTEN__)
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
	// WebGL 1 = OpenGL ES 2, what gl4es targets (docs/porting/analysis/web-renderer.md, decision 3).
	Attrs.majorVersion					= 1;
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
	if( GWebGLContext <= 0 )
	{
		debugf( NAME_Init, TEXT("UC2GL4ESDrv: could not create a WebGL 1 context on the engine's thread (result %i)"), (INT)GWebGLContext );
		GWebGLContext = 0;
		return 0;
	}
	if( emscripten_webgl_make_context_current( GWebGLContext ) != EMSCRIPTEN_RESULT_SUCCESS )
	{
		debugf( NAME_Init, TEXT("UC2GL4ESDrv: could not make the WebGL context current") );
		return 0;
	}
	debugf( NAME_Init, TEXT("UC2GL4ESDrv: WebGL 1 context created on the engine's thread (handle %i; alpha 0, depth 0, stencil 0, antialias 0, explicit swap)"), (INT)GWebGLContext );
	return 1;
#else
	// SDL2Drv creates the GL window and context for BLIT_OpenGL, from the attributes above.
	return Viewport->ResizeViewport( (Fullscreen ? BLIT_Fullscreen : 0) | BLIT_OpenGL, NewX, NewY, bSaveSize );
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

	// gl4es is configured through its environment only. The values are engine-ut2004's,
	// each for the reason given there (its GL4ESRenderDevice.cpp); the ones that matter most:
	// USEVBO: without it gl4es hands GLES client-side pointers for data that IS in buffer
	// objects, and WebGL has no client-side arrays (Emscripten then copies every vertex
	// range before every draw). BATCH 0: its draw-call merging was measured as a loss there.
	setenv( "LIBGL_ES",				"2", 1 );
	setenv( "LIBGL_NOBANNER",		"1", 1 );
	setenv( "LIBGL_NOERROR",		"0", 1 );	// glGetError keeps working (-glerrors)
	setenv( "LIBGL_USEVBO",			"1", 1 );
	setenv( "LIBGL_BATCH",			"0", 1 );
	setenv( "LIBGL_AVOID16BITS",	"1", 1 );	// no 16-bit downsampling of textures
	setenv( "LIBGL_MIPMAP",			"0", 1 );
	setenv( "LIBGL_FPS",			"0", 1 );
	setenv( "LIBGL_SILENTSTUB",		"1", 1 );
	setenv( "LIBGL_NOTEXRECT",		"1", 1 );
	// -gl4esdebug: gl4es logs every shader it converts (before and after).
	if( ParseParam( appCmdLine(), TEXT("gl4esdebug") ) )
	{
		setenv( "LIBGL_DBGSHADERCONV", "15", 1 );
		setenv( "LIBGL_LOGSHADERERROR", "1", 1 );
	}

	set_getprocaddress( UC2GL4ES_RawGetProcAddress );
	initialize_gl4es();
	Initialized = 1;
	debugf( NAME_Init, TEXT("UC2GL4ESDrv: gl4es initialised (GLES2 back end, real buffer objects, no batching)") );
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

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
