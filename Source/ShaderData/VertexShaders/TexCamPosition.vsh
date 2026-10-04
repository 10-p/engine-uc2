//=============================================================================
//	scion sz -- 
//	(VS_Texcoord_CAMERASPACEPOSITION_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// store camera space position for use as a texture coordinate source
// 

m4x4 R_CAMERASPACE_POSITION, R_LOCAL_POSITION, c[VSC_MVIEW_MATRIX]

// NOTE: the vertex's position is the same as the eye vector when in camera space
