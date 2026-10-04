//=============================================================================
//	scion sz -- 
//	(VS_Light_Directional_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

/*
 * Temp register usage:
 * r2	= L Dot N
 * r3	= L Dot N clamped to [0,1] .. also multiplied by Attenuation factor
 */

// L/|L| vector - given by light direction (assuming normalized)

// L Dot N
dp3 r2.xyz, R_LOCAL_NORMAL.xyz, -c[a0.x + VSC_LIGHT_DIR_OFFSET]

// Clamp L Dot N to [0,1]
max r3.xyz, r2.xyz, VSC_ZERO

// Multiply by light color
// and 
// Add to running final color
mad R_FINAL_COLOR.xyz, c[a0.x + VSC_LIGHT_DIFFUSE_OFFSET].xyz, r3.xyz, R_FINAL_COLOR.xyz



