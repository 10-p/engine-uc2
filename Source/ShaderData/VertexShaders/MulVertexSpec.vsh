//=============================================================================
//	scion sz -- 
//	(VS_Mul_VertexSpecular_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

mul R_FINAL_COLOR, R_FINAL_COLOR, SPECULAR

