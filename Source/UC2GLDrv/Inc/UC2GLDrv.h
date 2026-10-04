/*=============================================================================
	UC2GLDrv.h: UC2's OpenGL render device, public header.

	A three-way merge (docs/porting/analysis/gl-renderer.md): engine-ut2004's OpenGLDrv
	(structure, GL code) brought to UC2's render interface, with UC2's own D3DDrv as the
	specification of behaviour. M5 = renderer R1, fixed function; see the notebook for
	what is deferred to M6 (pixel-shader materials, vertex shaders) and M7 (render-target
	post-processing) and where the hooks are.
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
