//=============================================================================
//	scion sz -- 
//	(VS_Light_Ambient_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

add R_FINAL_COLOR, R_FINAL_COLOR, c[VSC_AMBIENT_COLOR]

