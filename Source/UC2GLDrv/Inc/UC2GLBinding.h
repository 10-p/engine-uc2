/*=============================================================================
	UC2GLBinding.h: which OpenGL this build of UC2GLDrv talks to.

	One driver source, two bindings chosen at build time:

	  UC2GL_BINDING_DESKTOP (default)  desktop OpenGL (2.0+ compatibility profile with
	                                   framebuffer objects). The context comes from SDL2Drv
	                                   (SDL_GL_CreateContext); every entry point is looked up
	                                   with SDL_GL_GetProcAddress into the device's pointer
	                                   table (UC2GLFuncs.h). libGL is never linked.
	  UC2GL_BINDING_GL4ES              (not built yet) GL4ES translating the same fixed-
	                                   function calls to GLES2/WebGL. The pointer table is
	                                   filled from gl4es' own lookup instead of SDL's, and the
	                                   present shader is compiled as GLSL ES 1.00. Everything
	                                   that differs is behind UC2GL_GLES.

	Nothing outside this header and UC2GLRenderDevice.cpp (FindProcs, shader prologue)
	depends on the binding.
=============================================================================*/

#ifndef _INC_UC2GLBINDING
#define _INC_UC2GLBINDING

#if !defined(UC2GL_BINDING_DESKTOP) && !defined(UC2GL_BINDING_GL4ES)
	#define UC2GL_BINDING_DESKTOP 1
#endif

#ifdef UC2GL_BINDING_GL4ES
	#define UC2GL_GLES 1
	#error "UC2GL_BINDING_GL4ES: not built at M5 (see docs/porting/analysis/gl-renderer.md)"
#else
	#define UC2GL_GLES 0
#endif

#ifdef WIN32
	#pragma pack(push,8)
	#include <windows.h>	// GL/gl.h needs WINGDIAPI/APIENTRY on Windows
	#pragma pack(pop)
#endif

// Prototypes are deliberately NOT requested: every call goes through the device's pointer
// table, so a stray direct call to a GL function fails to link instead of silently binding
// to a libGL nobody loaded.
#ifdef GL_GLEXT_PROTOTYPES
	#undef GL_GLEXT_PROTOTYPES
#endif
#include <GL/gl.h>
#include <GL/glext.h>

#ifndef APIENTRY
	#define APIENTRY
#endif

// Look up one entry point of the bound GL.
void* UC2GL_GetProcAddress( const char* Name );

#endif
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
