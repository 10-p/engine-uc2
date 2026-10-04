//=============================================================================
//	scion sz -- 
//	(VS_Next_Light_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// Set a0.x to next light
mov r0.x, c[a0.x + VSC_LIGHT_DIR_OFFSET + VSC_LIGHT_SIZE].w
mov a0.x, r0.x

