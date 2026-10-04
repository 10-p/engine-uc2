//=============================================================================
//	scion sz -- 
//	(VS_Texcoord0_Source_Pass_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// output texture coordinate with no matrix transform
mov oT0, R_TEX_SOURCE
