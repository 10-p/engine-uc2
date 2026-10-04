//=============================================================================
//	scion sz -- 
//	(VS_Texcoord_CAMERASPACENORMAL_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// store camera space NORMAL for use as a texture coordinate source
// 

// transform by inverse transpose of the model view matrix
m3x3 R_CAMERASPACE_NORMAL, R_LOCAL_NORMAL, c[VSC_INVT_MVIEW_MATRIX]
// it would be nice to be able to assume orthonormal transforms
//m3x3 R_CAMERASPACE_NORMAL, R_LOCAL_NORMAL, c[VSC_MVIEW_MATRIX]

// Make sure the normal is normalized
// NOTESZ - if we did lighting in camera space instead of object space (currently used)
// then this normal would only have to be normalized once.

dp3	r0.w, R_CAMERASPACE_NORMAL, R_CAMERASPACE_NORMAL				// r0.w = x^2 + y^2 + z^2
rsq r0.w, r0.w														// r0.w = 1 / sqrt(x^2 + y^2 + z^2)
mul R_CAMERASPACE_NORMAL.xyz, R_CAMERASPACE_NORMAL.xyz, r0.w

mov R_CAMERASPACE_NORMAL.w, VSC_ONE


