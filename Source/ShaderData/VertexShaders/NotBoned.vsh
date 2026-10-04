//=============================================================================
//	scion sz -- 
//	(VS_NotBoned_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// Transform vertex position
m4x4 oPos, R_LOCAL_POSITION, c[VSC_CLIP_MATRIX0]
