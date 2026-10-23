/*=============================================================================
	UC2WGLDrv.cpp: the WGL binding of UC2GLDrv (Windows).

	One OpenGL context for every viewport. It is created on a hidden window of the binding's own,
	so it outlives any viewport window, and every viewport window gets the same pixel format; it is
	made current on a viewport's window before that viewport is drawn, read or presented
	(WinDrv's viewport windows are CS_OWNDC: each keeps its device context). Entry points come
	from wglGetProcAddress, the OpenGL 1.1 ones from opengl32.dll's exports.
=============================================================================*/

#include "UC2WGLDrv.h"

/*-----------------------------------------------------------------------------
	Package and class.
-----------------------------------------------------------------------------*/

IMPLEMENT_PACKAGE(UC2WGLDrv);
IMPLEMENT_CLASS(UUC2WGLRenderDevice);

UUC2WGLRenderDevice::UUC2WGLRenderDevice()
{
	FramebufferViewport = NULL;
	FramebufferWindow = NULL;
}

// Wine's ntdll exports its version; Windows' does not.
static UBOOL RunningUnderWine()
{
	HMODULE Ntdll = GetModuleHandleA( "ntdll.dll" );
	return Ntdll && GetProcAddress( Ntdll, "wine_get_version" ) != NULL;
}

void UUC2WGLRenderDevice::StaticConstructor()
{
	new(GetClass(),TEXT("GDIPresent"),RF_Public)UBoolProperty( CPP_PROPERTY(GDIPresent), TEXT("Options"), CPF_Config );
	GDIPresent		= RunningUnderWine();

	// UUC2GLRenderDevice::StaticConstructor declared the properties on its class and set the game's compiled
	// defaults, which this class's default object starts from. The editor's differ: its viewports show the
	// level as PC's D3DDrv drew it (no Xbox post-processing), and one context draws many windows, so no
	// multisampled framebuffer per viewport.
	UsePostProcess	= 0;
	NumAASamples	= 0;
}

/*-----------------------------------------------------------------------------
	Context.
-----------------------------------------------------------------------------*/

static HGLRC	GContext		= NULL;
static HWND		GContextWindow	= NULL;		// hidden: holds the context while no viewport window does
static HDC		GContextDC		= NULL;
static INT		GPixelFormat	= 0;
typedef BOOL (WINAPI* UC2WGLSwapIntervalFunc)( int Interval );
static UC2WGLSwapIntervalFunc GSwapIntervalEXT = NULL;

static void DescribePixelFormat( PIXELFORMATDESCRIPTOR& Pfd )
{
	appMemzero( &Pfd, sizeof(Pfd) );
	Pfd.nSize		= sizeof(Pfd);
	Pfd.nVersion	= 1;
	Pfd.dwFlags		= PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	Pfd.iPixelType	= PFD_TYPE_RGBA;
	Pfd.cColorBits	= 24;
	Pfd.cAlphaBits	= 8;
	// The window's own framebuffer only receives the present pass: no depth, no stencil.
	Pfd.cDepthBits	= 0;
	Pfd.cStencilBits= 0;
	Pfd.iLayerType	= PFD_MAIN_PLANE;
}

//
// The device context of a window, with the binding's pixel format (a window's pixel format can be set once).
//
static HDC WindowDC( HWND Window )
{
	if( !Window || !IsWindow( Window ) )
		return NULL;
	HDC DC = GetDC( Window );
	if( !DC )
		return NULL;
	if( GetPixelFormat( DC ) == 0 )
	{
		PIXELFORMATDESCRIPTOR Pfd;
		DescribePixelFormat( Pfd );
		if( !GPixelFormat )
			GPixelFormat = ChoosePixelFormat( DC, &Pfd );
		if( !GPixelFormat || !SetPixelFormat( DC, GPixelFormat, &Pfd ) )
		{
			debugf( NAME_Init, TEXT("UC2WGLDrv: could not set pixel format %i on window %08x (error %i)"), GPixelFormat, (INT)(PTRINT)Window, (INT)GetLastError() );
			return NULL;
		}
	}
	return DC;
}

static UBOOL CreateContextWindow()
{
	if( GContextDC )
		return 1;
	WNDCLASSA Class;
	appMemzero( &Class, sizeof(Class) );
	Class.style			= CS_OWNDC;
	Class.lpfnWndProc	= DefWindowProcA;
	Class.hInstance		= hInstance;
	Class.lpszClassName	= "UC2WGLContextWindow";
	RegisterClassA( &Class );
	GContextWindow = CreateWindowExA( 0, "UC2WGLContextWindow", "UC2WGLDrv", WS_POPUP, 0, 0, 16, 16, NULL, NULL, hInstance, NULL );
	GContextDC = WindowDC( GContextWindow );
	if( !GContextDC )
	{
		debugf( NAME_Init, TEXT("UC2WGLDrv: could not create the context's window (error %i)"), (INT)GetLastError() );
		return 0;
	}
	return 1;
}

//
// WinDrv makes a viewport window it creates as a child of an editor window a WS_POPUP window (the style only; the
// parent stays). Windows keeps treating it as that window's child; wine does not, and a browser page hidden with
// its viewport inside leaves the viewport shown over the page that replaced it. Under wine it is made a plain
// child again.
//
static void NormalizeViewportWindow( HWND Window )
{
	static UBOOL UnderWine = RunningUnderWine();
	if( !UnderWine || !Window )
		return;
	LONG Style = GetWindowLongA( Window, GWL_STYLE );
	HWND Parent = GetAncestor( Window, GA_PARENT );
	if( !(Style & WS_POPUP) || !Parent || Parent == GetDesktopWindow() )
		return;
	SetWindowLongA( Window, GWL_STYLE, (Style & ~WS_POPUP) | WS_CHILD | WS_CLIPSIBLINGS );
	SetWindowPos( Window, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED );
}

UBOOL UUC2WGLRenderDevice::MakeCurrent( UViewport* Viewport )
{
	if( !GContext )
		return 0;
	// GDIPresent draws every viewport in framebuffer objects and never renders into a viewport window: the context
	// stays on its own window.
	HDC DC = (Viewport && !GDIPresent) ? WindowDC( (HWND)Viewport->GetWindow() ) : NULL;
	if( !DC )
		DC = GContextDC;
	if( wglGetCurrentContext() == GContext && wglGetCurrentDC() == DC )
		return 1;
	if( wglMakeCurrent( DC, GContext ) )
		return 1;
	debugf( NAME_Warning, TEXT("UC2WGLDrv: wglMakeCurrent failed (error %i)"), (INT)GetLastError() );
	return 0;
}

const TCHAR* UUC2WGLRenderDevice::BindingName()
{
	return TEXT("desktop GL through WGL");
}

void UUC2WGLRenderDevice::BindingSetContextAttributes()
{
	// The pixel format says it all (DescribePixelFormat).
}

UBOOL UUC2WGLRenderDevice::BindingCreateContext( UViewport* Viewport, UBOOL Fullscreen, INT NewX, INT NewY, UBOOL bSaveSize )
{
	// WinDrv sizes the window (and, for the game, makes it fullscreen); the drawing is the context's.
	if( !Viewport->ResizeViewport( (Fullscreen ? BLIT_Fullscreen : 0) | BLIT_OpenGL, NewX, NewY, bSaveSize ) )
		return 0;
	NormalizeViewportWindow( (HWND)Viewport->GetWindow() );
	if( !GContext )
	{
		if( !CreateContextWindow() )
			return 0;
		GContext = wglCreateContext( GContextDC );
		if( !GContext )
		{
			debugf( NAME_Init, TEXT("UC2WGLDrv: wglCreateContext failed (pixel format %i, error %i)"), GPixelFormat, (INT)GetLastError() );
			return 0;
		}
		debugf( NAME_Init, TEXT("UC2WGLDrv: OpenGL context created (pixel format %i), shared by every viewport window; frames presented %s"), GPixelFormat,
			GDIPresent ? TEXT("through GDI (GDIPresent=True)") : TEXT("with SwapBuffers (GDIPresent=False)") );
	}
	if( !MakeCurrent( Viewport ) )
		return 0;
	// SetRes sizes the viewport's own main framebuffer next.
	UseWindowFramebuffer( Viewport );
	return 1;
}

void* UUC2WGLRenderDevice::BindingGetProcAddress( const char* Name )
{
	void* Proc = (void*)wglGetProcAddress( Name );
	// Some drivers answer 1, 2, 3 or -1 for a function they do not hand out this way.
	if( (PTRINT)Proc <= 3 || (PTRINT)Proc == (PTRINT)-1 )
		Proc = NULL;
	if( !Proc )
	{
		static HMODULE OpenGL32 = NULL;
		if( !OpenGL32 )
			OpenGL32 = GetModuleHandleA( "opengl32.dll" );
		if( OpenGL32 )
			Proc = (void*)GetProcAddress( OpenGL32, Name );
	}
	return Proc;
}

UBOOL UUC2WGLRenderDevice::BindingContextAlive()
{
	return GContext && wglGetCurrentContext() == GContext;
}

void UUC2WGLRenderDevice::BindingGetDrawableSize( UViewport* Viewport, INT& Width, INT& Height )
{
	HWND Window = (HWND)Viewport->GetWindow();
	RECT Rect;
	if( Window && GetClientRect( Window, &Rect ) && Rect.right > Rect.left && Rect.bottom > Rect.top )
	{
		Width	= Rect.right - Rect.left;
		Height	= Rect.bottom - Rect.top;
	}
}

void UUC2WGLRenderDevice::BindingSetSwapInterval( INT Interval )
{
	if( !GSwapIntervalEXT )
		GSwapIntervalEXT = (UC2WGLSwapIntervalFunc)BindingGetProcAddress( "wglSwapIntervalEXT" );
	if( GSwapIntervalEXT )
		GSwapIntervalEXT( Interval );
}

void UUC2WGLRenderDevice::BindingSwap( UViewport* Viewport )
{
	// A hidden viewport (a browser that is not shown) is not presented.
	HWND Window = (HWND)Viewport->GetWindow();
	if( !IsWindowVisible( Window ) )
		return;
	if( !GDIPresent )
	{
		HDC DC = WindowDC( Window );
		if( DC )
			SwapBuffers( DC );
		return;
	}
	// The viewport's frame, read from its main framebuffer and drawn into the window by GDI. The present pass
	// (the game's gamma, brightness and contrast) is left out, as D3DDrv's gamma ramp is in a window.
	INT Width = MainWidth, Height = MainHeight;
	if( Width <= 0 || Height <= 0 )
		return;
	PresentPixels.Empty( Width * Height * 4 );
	PresentPixels.Add( Width * Height * 4 );
	ResolveMain( 1, 0 );
	glBindFramebuffer( GL_FRAMEBUFFER, MainTextureFramebuffer() );
	glPixelStorei( GL_PACK_ALIGNMENT, 4 );
	glReadPixels( 0, 0, Width, Height, GL_BGRA, GL_UNSIGNED_BYTE, &PresentPixels(0) );
	glBindFramebuffer( GL_FRAMEBUFFER, MainFramebuffer );
	BITMAPINFO Info;
	appMemzero( &Info, sizeof(Info) );
	Info.bmiHeader.biSize			= sizeof(BITMAPINFOHEADER);
	Info.bmiHeader.biWidth			= Width;
	Info.bmiHeader.biHeight			= Height;	// bottom-up, GL's row order
	Info.bmiHeader.biPlanes			= 1;
	Info.bmiHeader.biBitCount		= 32;
	Info.bmiHeader.biCompression	= BI_RGB;
	HDC DC = GetDCEx( Window, NULL, DCX_CACHE );
	if( !DC )
		return;
	SetDIBitsToDevice( DC, 0, 0, Width, Height, 0, 0, 0, Height, &PresentPixels(0), &Info, DIB_RGB_COLORS );
	ReleaseDC( Window, DC );
}

/*-----------------------------------------------------------------------------
	A main framebuffer per viewport window.
-----------------------------------------------------------------------------*/

void UUC2WGLRenderDevice::DeleteWindowFramebuffer( const FWindowFramebuffer& F )
{
	if( F.Framebuffer )			glDeleteFramebuffers( 1, &F.Framebuffer );
	if( F.ResolveFramebuffer )	glDeleteFramebuffers( 1, &F.ResolveFramebuffer );
	if( F.MSColorRenderbuffer )	glDeleteRenderbuffers( 1, &F.MSColorRenderbuffer );
	if( F.MSDepthRenderbuffer )	glDeleteRenderbuffers( 1, &F.MSDepthRenderbuffer );
	if( F.DepthRenderbuffer )	glDeleteRenderbuffers( 1, &F.DepthRenderbuffer );
	if( F.DepthStencil )		glDeleteTextures( 1, &F.DepthStencil );
	if( F.ColorTexture )		glDeleteTextures( 1, &F.ColorTexture );
}

void UUC2WGLRenderDevice::UseWindowFramebuffer( UViewport* Viewport, UBOOL ClearIfSwitched )
{
	HWND Window = Viewport ? (HWND)Viewport->GetWindow() : NULL;
	if( !Window || (Window == FramebufferWindow && Viewport == FramebufferViewport) || !ValidContext )
		return;

	// The current window's framebuffer goes back to the list.
	if( FramebufferWindow && MainFramebuffer )
	{
		FWindowFramebuffer& F = WindowFramebuffers( WindowFramebuffers.Add() );
		F.Viewport				= FramebufferViewport;
		F.Window				= FramebufferWindow;
		F.Framebuffer			= MainFramebuffer;
		F.ColorTexture			= MainColorTexture;
		F.DepthStencil			= MainDepthStencil;
		F.DepthRenderbuffer		= MainDepthRenderbuffer;
		F.ResolveFramebuffer	= MainResolveFramebuffer;
		F.MSColorRenderbuffer	= MainMSColorRenderbuffer;
		F.MSDepthRenderbuffer	= MainMSDepthRenderbuffer;
		F.Samples				= MainSamples;
		F.Width					= MainWidth;
		F.Height				= MainHeight;
	}

	// Closed windows' framebuffers are deleted; this window's is taken out of the list.
	FWindowFramebuffer Found;
	appMemzero( &Found, sizeof(Found) );
	for( INT i=WindowFramebuffers.Num()-1; i>=0; i-- )
	{
		FWindowFramebuffer F = WindowFramebuffers(i);
		if( F.Window == Window && F.Viewport == Viewport )
			Found = F;
		else if( IsWindow( F.Window ) && F.Window != Window )
			continue;
		else
			DeleteWindowFramebuffer( F );
		WindowFramebuffers.Remove( i );
	}

	if( !Found.Framebuffer )
	{
		RECT Rect;
		GetWindowRect( Window, &Rect );
		debugf( NAME_Init, TEXT("UC2WGLDrv: viewport %s: window %08x at %i,%i %ix%i, %s, in %08x"), Viewport->GetName(), (INT)(PTRINT)Window,
			Rect.left, Rect.top, Rect.right - Rect.left, Rect.bottom - Rect.top, IsWindowVisible( Window ) ? TEXT("visible") : TEXT("hidden"), (INT)(PTRINT)GetAncestor( Window, GA_ROOT ) );
	}
	// The device draws into it from now on (zero: the device makes a new one for the window's size).
	FramebufferViewport		= Viewport;
	FramebufferWindow		= Window;
	MainFramebuffer			= Found.Framebuffer;
	MainColorTexture		= Found.ColorTexture;
	MainDepthStencil		= Found.DepthStencil;
	MainDepthRenderbuffer	= Found.DepthRenderbuffer;
	MainResolveFramebuffer	= Found.ResolveFramebuffer;
	MainMSColorRenderbuffer	= Found.MSColorRenderbuffer;
	MainMSDepthRenderbuffer	= Found.MSDepthRenderbuffer;
	MainSamples				= Found.Samples;
	MainWidth				= Found.Width;
	MainHeight				= Found.Height;
	// Copies of the previous window's frame are not this one's.
	MainColorVersion++;
	MainDepthVersion++;
	ResolvedColorVersion = ResolvedDepthVersion = -1;
	if( DefaultRenderTarget && MainFramebuffer )
		DefaultRenderTarget->UpdateSurfaces( MainWidth, MainHeight );
	if( ClearIfSwitched && MainFramebuffer )
	{
		glBindFramebuffer( GL_FRAMEBUFFER, MainFramebuffer );
		glClearColor( 0.f, 0.f, 0.f, 1.f );
		glClearDepth( 1.0 );
		glClearStencil( 0 );
		glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
		glDepthMask( GL_TRUE );
		glStencilMask( 0xff );
		glDisable( GL_SCISSOR_TEST );
		glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT );
		glEnable( GL_SCISSOR_TEST );
	}
	RenderInterface.InvalidateHardwareState();
}

/*-----------------------------------------------------------------------------
	The device's entry points that touch GL outside SetRes.
-----------------------------------------------------------------------------*/

FRenderInterface* UUC2WGLRenderDevice::Lock( UViewport* Viewport, BYTE* HitData, INT* HitSize )
{
	MakeCurrent( Viewport );
	UseWindowFramebuffer( Viewport, 1 );
	return UUC2GLRenderDevice::Lock( Viewport, HitData, HitSize );
}

void UUC2WGLRenderDevice::Present( UViewport* Viewport )
{
	MakeCurrent( Viewport );
	UseWindowFramebuffer( Viewport );
	UUC2GLRenderDevice::Present( Viewport );
}

void UUC2WGLRenderDevice::ReadPixels( UViewport* Viewport, FColor* Pixels )
{
	MakeCurrent( Viewport );
	UseWindowFramebuffer( Viewport );
	UUC2GLRenderDevice::ReadPixels( Viewport, Pixels );
}

void UUC2WGLRenderDevice::Flush( UViewport* Viewport )
{
	MakeCurrent( Viewport );
	UUC2GLRenderDevice::Flush( Viewport );
}

void UUC2WGLRenderDevice::Exit( UViewport* Viewport )
{
	// The viewport's window may be gone already: then the context's own window holds the context.
	MakeCurrent( Viewport );
	UUC2GLRenderDevice::Exit( Viewport );
	if( BindingContextAlive() )
		for( INT i=0; i<WindowFramebuffers.Num(); i++ )
			DeleteWindowFramebuffer( WindowFramebuffers(i) );
	WindowFramebuffers.Empty();
	FramebufferViewport = NULL;
	FramebufferWindow = NULL;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
