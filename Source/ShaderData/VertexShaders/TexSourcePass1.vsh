//=============================================================================
//	scion sz -- 
//	(VS_Texcoord1_Source_Pass_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// output texture coordinate with no matrix transform
mov oT1, R_TEX_SOURCE
