//=============================================================================
//	scion sz -- 
//	(VS_Light_Point_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

/*
 * Temp register usage:
 * r0	= L (vertex to light vector) .. normalized
 * r1.x	= |L|*|L|
 * r1.z	= 1 / |L|
 * r1.y = 1/(atten0 + D*atten2 + D^2*atten3) .. D3D Attenuation factor
 * r2	= L Dot N
 * r3	= L Dot N clamped to [0,1] .. also multiplied by Attenuation factor
 */

// L vector
sub r0.xyz, c[a0.x + VSC_LIGHT_POS_OFFSET].xyz, R_LOCAL_POSITION.xyz

// |L| = L magnitude
dp3	r1.x, r0.xyz, r0.xyz					// r1.x = x^2 + y^2 + z^2
rsq r1.z, r1.x								// r1.z = 1 / sqrt(x^2 + y^2 + z^2)

// Normalize L = L/|L|
mul r0.xyz, r0.xyz, r1.z

// L Dot N
dp3 r2.xyz, R_LOCAL_NORMAL.xyz, r0.xyz

// Clamp L Dot N to > 0
max r3.xyz, r2.xyz, VSC_ZERO

// check light's range and set r3.xyz to 1 if this vertex is within the range
// set to 0 otherwise
slt r0.w, r1.x, c[a0.x + VSC_LIGHT_POS_OFFSET].w
mul r3.xyz, r3.xyz, r0.w

// use D3D light attenuation factors s.t. A = 1/(atten0 + D*atten2 + D^2*atten3)
//
// r1.x = d*d
// r1.z = 1/d
// r4 = { 1, d, d*d, 1/d }
//
dst r4, r1.x, r1.z
dp3 r0.x, r4.xyz, c[a0.x + VSC_LIGHT_DIR_OFFSET].xyz
rcp r1.y, r0.x

// multiply attenuation from above with the dot product result
mul r3.xyz, r3.xyz, r1.y

// Multiply by light color 
// and
// Add to running final color
//
mad R_FINAL_COLOR.xyz, c[a0.x + VSC_LIGHT_DIFFUSE_OFFSET].xyz, r3.xyz, R_FINAL_COLOR.xyz

