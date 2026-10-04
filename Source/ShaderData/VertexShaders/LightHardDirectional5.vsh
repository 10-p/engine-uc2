//=============================================================================
//	scion sz -- 
//	(VS_Light_HardDirectional5_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

 // Make sure the vertex normal is normalized
// r0.w = x^2 + y^2 + z^2
dp3	r0.w, R_LOCAL_NORMAL, R_LOCAL_NORMAL
// r0.w = 1 / (x^2 + y^2 + z^2)
rsq r0.w, r0.w								
mul R_LOCAL_NORMAL.xyz, R_LOCAL_NORMAL.xyz, r0.w
 
// ================= 
// LIGHT 0
// ================= 
// L Dot N - L/|L| vector given by light direction (assuming normalized)
dp3 r2.xyz, R_LOCAL_NORMAL.xyz, -c[0 + VSC_HARD_LIGHT_BASE_DIRS]
// Clamp L Dot N to [0,1]
max r3.xyz, r2.xyz, VSC_ZERO
// Multiply by light color and Add to running final color
mad R_FINAL_COLOR.xyz, c[0 + VSC_HARD_LIGHT_BASE_COLORS].xyz, r3.xyz, R_FINAL_COLOR.xyz

// ================= 
// LIGHT 1
// ================= 
// L Dot N - L/|L| vector given by light direction (assuming normalized)
dp3 r2.xyz, R_LOCAL_NORMAL.xyz, -c[1 + VSC_HARD_LIGHT_BASE_DIRS]
// Clamp L Dot N to [0,1]
max r3.xyz, r2.xyz, VSC_ZERO
// Multiply by light color and Add to running final color
mad R_FINAL_COLOR.xyz, c[1 + VSC_HARD_LIGHT_BASE_COLORS].xyz, r3.xyz, R_FINAL_COLOR.xyz

// ================= 
// LIGHT 2
// ================= 
// L Dot N - L/|L| vector given by light direction (assuming normalized)
dp3 r2.xyz, R_LOCAL_NORMAL.xyz, -c[2 + VSC_HARD_LIGHT_BASE_DIRS]
// Clamp L Dot N to [0,1]
max r3.xyz, r2.xyz, VSC_ZERO
// Multiply by light color and Add to running final color
mad R_FINAL_COLOR.xyz, c[2 + VSC_HARD_LIGHT_BASE_COLORS].xyz, r3.xyz, R_FINAL_COLOR.xyz

// ================= 
// LIGHT 3
// ================= 
// L Dot N - L/|L| vector given by light direction (assuming normalized)
dp3 r2.xyz, R_LOCAL_NORMAL.xyz, -c[3 + VSC_HARD_LIGHT_BASE_DIRS]
// Clamp L Dot N to [0,1]
max r3.xyz, r2.xyz, VSC_ZERO
// Multiply by light color and Add to running final color
mad R_FINAL_COLOR.xyz, c[3 + VSC_HARD_LIGHT_BASE_COLORS].xyz, r3.xyz, R_FINAL_COLOR.xyz

// ================= 
// LIGHT 4
// ================= 
// L Dot N - L/|L| vector given by light direction (assuming normalized)
dp3 r2.xyz, R_LOCAL_NORMAL.xyz, -c[4 + VSC_HARD_LIGHT_BASE_DIRS]
// Clamp L Dot N to [0,1]
max r3.xyz, r2.xyz, VSC_ZERO
// Multiply by light color and Add to running final color
mad R_FINAL_COLOR.xyz, c[4 + VSC_HARD_LIGHT_BASE_COLORS].xyz, r3.xyz, R_FINAL_COLOR.xyz




