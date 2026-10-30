/*=============================================================================
	UC2WGLDrv.h: UC2WGLDrv.UC2WGLRenderDevice, the WGL binding of UC2GLDrv.

	UUC2GLRenderDevice is the renderer; this subclass only changes what it is bound to
	(UC2GLBinding.h): desktop OpenGL on the Win32 windows of WinDrv's viewports, through WGL.
	It is the editor's device: UnrealEd draws every viewport (the level views, the browsers,
	the property previews) through one device object, so one GL context serves every window
	and is made current on the viewport's window before that viewport is drawn.
=============================================================================*/

#ifndef _INC_UC2WGLDRV
#define _INC_UC2WGLDRV

#include "UC2GLDrv.h"

#define AUTO_INITIALIZE_REGISTRANTS_UC2WGLDRV UUC2WGLRenderDevice::StaticClass();

class UUC2WGLRenderDevice : public UUC2GLRenderDevice
{
	DECLARE_CLASS(UUC2WGLRenderDevice,UUC2GLRenderDevice,CLASS_Config,UC2WGLDrv)
public:
	// How a finished frame reaches its window (ini GDIPresent): False, SwapBuffers on the window; True, every
	// viewport drawn in framebuffer objects only, its frame read back and drawn into the window with GDI
	// (SetDIBitsToDevice). Compiled default: True under wine, where SwapBuffers on WinDrv's viewport windows
	// leaves parts of them unpainted (the window's GL surface keeps the size and clip it had when it was made).
	UBOOL GDIPresent;

	UUC2WGLRenderDevice();
	// The config properties are the base class's; the values come from this class's own ini
	// section, [UC2WGLDrv.UC2WGLRenderDevice], with the editor's compiled defaults.
	void StaticConstructor();

	// URenderDevice: the context is made current on the viewport's window first.
	virtual FRenderInterface* Lock( UViewport* Viewport, BYTE* HitData, INT* HitSize );
	virtual void Present( UViewport* Viewport );
	virtual void ReadPixels( UViewport* Viewport, FColor* Pixels );
	virtual void Flush( UViewport* Viewport );
	virtual void Exit( UViewport* Viewport );

	// UUC2GLRenderDevice binding.
	virtual const TCHAR* BindingName();
	virtual void BindingSetContextAttributes();
	virtual UBOOL BindingCreateContext( UViewport* Viewport, UBOOL Fullscreen, INT NewX, INT NewY, UBOOL bSaveSize );
	virtual void* BindingGetProcAddress( const char* Name );
	virtual UBOOL BindingContextAlive();
	virtual void BindingGetDrawableSize( UViewport* Viewport, INT& Width, INT& Height );
	virtual void BindingSetSwapInterval( INT Interval );
	virtual void BindingSwap( UViewport* Viewport );
	virtual UBOOL BindingShowsPresentPass() { return !GDIPresent; }

private:
	// The context current on the viewport's window (or, without one, on the binding's own hidden window).
	UBOOL MakeCurrent( UViewport* Viewport );

	// The device's main framebuffer is the frame of one viewport. The editor draws many viewports of different
	// sizes, one after the other, through this one device: each viewport window keeps a main framebuffer of its
	// own, swapped into the device before that viewport is drawn, presented or read. A frame starts cleared when
	// the device switches to it for drawing (D3DDrv's viewports shared one back buffer).
	struct FWindowFramebuffer
	{
		UViewport*	Viewport;	// compared, never used: a window handle can come back for another viewport's window
		HWND	Window;
		GLuint	Framebuffer, ColorTexture, DepthStencil, DepthRenderbuffer;
		GLuint	ResolveFramebuffer, MSColorRenderbuffer, MSDepthRenderbuffer;
		INT		Samples, Width, Height;
	};
	TArray<BYTE>				PresentPixels;		// GDIPresent: the frame read back
	TArray<FWindowFramebuffer>	WindowFramebuffers;	// every window's but the current one's
	UViewport*					FramebufferViewport;	// whose framebuffer the device's Main* members hold
	HWND						FramebufferWindow;
	void UseWindowFramebuffer( UViewport* Viewport, UBOOL ClearIfSwitched=0 );
	void DeleteWindowFramebuffer( const FWindowFramebuffer& F );
};

#endif
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
