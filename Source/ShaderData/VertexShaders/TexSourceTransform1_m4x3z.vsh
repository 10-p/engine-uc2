//=============================================================================
//	scion sz -- 
//	(VS_Texcoord1_Source_Transform_m4x3z_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// output texture coordinate with matrix transform applied
// 

dp4 oT1.x, R_TEX_SOURCE.xyzw, c[VSC_TEX1_MATRIX0]
dp4 oT1.y, R_TEX_SOURCE.xyzw, c[VSC_TEX1_MATRIX1]
dp4 oT1.z, R_TEX_SOURCE.xyzw, c[VSC_TEX1_MATRIX2]
