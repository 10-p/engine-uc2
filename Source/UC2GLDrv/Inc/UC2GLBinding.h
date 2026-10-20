/*=============================================================================
	UC2GLBinding.h: which OpenGL UC2GLDrv talks to.

	One driver source, compiled once. The binding is a property of the render device
	object, chosen by its class:

	  UC2GLDrv.UC2GLRenderDevice         desktop OpenGL (2.0+ compatibility profile with
	                                     framebuffer objects). The context comes from SDL2Drv
	                                     (SDL_GL_CreateContext); every entry point is looked
	                                     up with SDL_GL_GetProcAddress into the device's
	                                     pointer table (UC2GLFuncs.h). libGL is never linked.
	  UC2GL4ESDrv.UC2GL4ESRenderDevice   a subclass in its own small package
	                                     (Source/UC2GL4ESDrv): an OpenGL ES 3 context, ES 2 as
	                                     the fallback (SDL/EGL on Linux, WebGL 2 or 1 on the
	                                     web), behind GL4ES, which translates the same
	                                     fixed-function and GLSL calls. The pointer table is
	                                     filled from gl4es' lookup; the calls that must reach
	                                     OpenGL ES itself (depth-stencil storage and attachment,
	                                     engine texture uploads) go through a second, "raw"
	                                     table filled from the library under gl4es.

	The device's Binding* virtuals are the whole interface between the two; everything else
	in the driver asks UUC2GLRenderDevice::IsGLES (and the Cap* flags) at run time, so both
	bindings run the same object code and a renderer fix reaches the web by itself.

	UC2GL_HAS_DESKTOP_BINDING is 0 where no desktop OpenGL exists (Emscripten, Android): the
	base class then cannot create a context and says so.
=============================================================================*/

#ifndef _INC_UC2GLBINDING
#define _INC_UC2GLBINDING

#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
	#define UC2GL_HAS_DESKTOP_BINDING 0
#else
	#define UC2GL_HAS_DESKTOP_BINDING 1
#endif

#ifdef WIN32
	#pragma pack(push,8)
	#include <windows.h>	// GL/gl.h needs WINGDIAPI/APIENTRY on Windows
	#pragma pack(pop)
#endif

// Prototypes are deliberately not requested: every call goes through the device's pointer
// table, so a stray direct call to a GL function fails to link instead of silently binding
// to a libGL nobody loaded (or, in a build with gl4es, to gl4es behind the device's back).
#ifdef GL_GLEXT_PROTOTYPES
	#undef GL_GLEXT_PROTOTYPES
#endif
#include <GL/gl.h>
#include <GL/glext.h>

#ifndef APIENTRY
	#define APIENTRY
#endif

// OpenGL ES 2 / WebGL 1 names the desktop headers may not have.
#ifndef GL_DEPTH_STENCIL_OES
	#define GL_DEPTH_STENCIL_OES			0x84F9
#endif
#ifndef GL_UNSIGNED_INT_24_8_OES
	#define GL_UNSIGNED_INT_24_8_OES		0x84FA
#endif
#ifndef GL_DEPTH24_STENCIL8_OES
	#define GL_DEPTH24_STENCIL8_OES			0x88F0
#endif
#ifndef GL_DEPTH_STENCIL_ATTACHMENT
	#define GL_DEPTH_STENCIL_ATTACHMENT		0x821A
#endif
#ifndef GL_HIGH_FLOAT
	#define GL_LOW_FLOAT					0x8DF0
	#define GL_MEDIUM_FLOAT					0x8DF1
	#define GL_HIGH_FLOAT					0x8DF2
#endif

#endif
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
