//=============================================================================
//	scion sz -- 
//	(VS_InitLighting_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// Make sure the normal is normalized
dp3	r0.w, R_LOCAL_NORMAL, R_LOCAL_NORMAL				// r0.w = x^2 + y^2 + z^2
rsq r0.w, r0.w											// r0.w = 1 / (x^2 + y^2 + z^2)
mul R_LOCAL_NORMAL.xyz, R_LOCAL_NORMAL.xyz, r0.w

// Set a0.x to first light's data block.
mov r0.x, c[VSC_LIGHT_BASE + VSC_LIGHT_DIR_OFFSET].w
mov a0.x, r0.x


