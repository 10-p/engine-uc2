//=============================================================================
//	scion sz -- 
//	(VS_Texcoord3_Source_Texkill_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

//
// Each of the 4 frustum planes for the projector are stored in
// shader constants (VSC_PROJ_CLIP_PLANE0 thru VSC_PROJ_CLIP_PLANE3)
//
// Plane equation is represented by Ax + By + Cz + D = 0
// Therefore we have:
// [x,y,z] = [A,B,C] = normal for the plane
// [w] = - dot(normal,origin) = D
//
// All these values are in local (model) space so that we can work
// with R_LOCAL_POSITION
//

// assuming R_LOCAL_POSITION.w = 1.0
//

m4x4 oT3, R_LOCAL_POSITION, c[VSC_PROJ_CLIP_MATRIX]