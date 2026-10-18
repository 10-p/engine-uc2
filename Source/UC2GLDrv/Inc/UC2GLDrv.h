/*=============================================================================
	UC2GLDrv.h: UC2's OpenGL render device, public header.

	UC2's render interface on OpenGL, with UC2's own D3DDrv as the specification of
	behaviour: fixed-function texture stages (as GLSL programs or GL's texture environment),
	pixel-shader materials from the translated Xbox shaders, vertex shaders composed from
	the .vsh fragments, render-target post-processing. The binding (desktop GL or GL4ES)
	is chosen at run time by the device class (UC2GLBinding.h).
=============================================================================*/

#ifndef _INC_UC2GLDRV
#define _INC_UC2GLDRV

#include "UC2GLBinding.h"

#include "Engine.h"
#include "UnRender.h"

#include "UC2GLD3DTypes.h"
#include "UC2GLResource.h"
#include "UC2GLRenderInterface.h"
#include "UC2GLRenderDevice.h"

#endif
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
