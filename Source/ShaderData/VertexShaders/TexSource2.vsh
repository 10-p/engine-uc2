//=============================================================================
//	scion sz -- 
//	(VS_Texcoord_Source2_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// use TEXCOORD2 stream input for source
mov R_TEX_SOURCE, TEXCOORD2
