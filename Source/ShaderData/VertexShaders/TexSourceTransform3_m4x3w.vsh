//=============================================================================
//	scion sz -- 
//	(VS_Texcoord3_Source_Transform_m4x3w_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// output texture coordinate with matrix transform applied
// 

dp4 oT3.x, R_TEX_SOURCE.xyzw, c[VSC_TEX3_MATRIX0]
dp4 oT3.y, R_TEX_SOURCE.xyzw, c[VSC_TEX3_MATRIX1]
dp4 oT3.w, R_TEX_SOURCE.xyzw, c[VSC_TEX3_MATRIX2]
