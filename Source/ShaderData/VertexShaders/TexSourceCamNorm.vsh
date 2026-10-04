//=============================================================================
//	scion sz -- 
//	(VS_Texcoord_Source_CAMERASPACENORMAL_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// use temp register R_CAMERASPACE_NORMAL for source
mov R_TEX_SOURCE, R_CAMERASPACE_NORMAL
