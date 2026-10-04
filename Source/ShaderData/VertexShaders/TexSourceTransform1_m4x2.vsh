//=============================================================================
//	scion sz -- 
//	(VS_Texcoord1_Source_Transform_m4x2_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// output texture coordinate with matrix transform applied
// 

dp4 oT1.x, R_TEX_SOURCE.xyww, c[VSC_TEX1_MATRIX0]
dp4 oT1.y, R_TEX_SOURCE.xyww, c[VSC_TEX1_MATRIX1]
