//=============================================================================
//	scion sz -- 
//	(VS_Texcoord2_Transform_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// output texture coordinate with matrix transform applied
// 

// note the swizzling of w to z 
// required since we are assuming a 2D texture coordinate
// this is a 3x2 transform

dp4 oT2.x, TEXCOORD2.xyww, c[VSC_TEX2_MATRIX0]
dp4 oT2.y, TEXCOORD2.xyww, c[VSC_TEX2_MATRIX1]

