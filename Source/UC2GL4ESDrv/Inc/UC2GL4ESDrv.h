/*=============================================================================
	UC2GL4ESDrv.h: UC2GL4ESDrv.UC2GL4ESRenderDevice, the GL4ES binding of UC2GLDrv (M11).

	UUC2GLRenderDevice is the renderer; this subclass only changes what it is bound to
	(UC2GLBinding.h): an OpenGL ES 2 context (SDL/EGL natively, WebGL 1 in the browser) and
	GL4ES in front of it. Native Linux carries it as the twin of the web renderer.
=============================================================================*/

#ifndef _INC_UC2GL4ESDRV
#define _INC_UC2GL4ESDRV

#include "UC2GLDrv.h"

#define AUTO_INITIALIZE_REGISTRANTS_UC2GL4ESDRV UUC2GL4ESRenderDevice::StaticClass();

class UUC2GL4ESRenderDevice : public UUC2GLRenderDevice
{
	DECLARE_CLASS(UUC2GL4ESRenderDevice,UUC2GLRenderDevice,CLASS_Config,UC2GL4ESDrv)
public:
	UUC2GL4ESRenderDevice();
	// The config properties are the base class's (declared there once); the values come from
	// this class's own ini section, [UC2GL4ESDrv.UC2GL4ESRenderDevice].
	void StaticConstructor();

	// UUC2GLRenderDevice binding.
	virtual const TCHAR* BindingName();
	virtual void BindingSetContextAttributes();
	virtual UBOOL BindingCreateContext( UViewport* Viewport, UBOOL Fullscreen, INT NewX, INT NewY, UBOOL bSaveSize );
	virtual UBOOL BindingInit();
	virtual void* BindingGetProcAddress( const char* Name );
	virtual void* BindingGetRawProcAddress( const char* Name );
	virtual UBOOL BindingContextAlive();
	virtual void BindingGetDrawableSize( UViewport* Viewport, INT& Width, INT& Height );
	virtual void BindingSetSwapInterval( INT Interval );
	virtual void BindingSwap( UViewport* Viewport );
};

#endif
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
