//=============================================================================
//	scion sz -- 
//	(VS_SkelThreeBoned)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// using compressed indices stored in byte format [0,1] which must be remapped to [0,255]
mul r1, BLENDINDEX, VSC_255_HALF

// retrieve bone index 0. Must be * 3 !
mov a0.x, r1.x

// transform POSITION/NORMAL with first bone
m4x3 r2, R_LOCAL_POSITION, c[a0.x + VSC_BONEBASE]
m3x3 R_LOCAL_NORMAL, NORMAL, c[a0.x + VSC_BONEBASE]

// retrieve bone index 1
mov a0.x, r1.y

// transform POSITION/NORMAL with second bone
m4x3 r0, R_LOCAL_POSITION, c[a0.x + VSC_BONEBASE]
m3x3 r5, NORMAL, c[a0.x + VSC_BONEBASE]

// retrieve bone index 2
mov a0.x, r1.z

// transform POSITION/NORMAL with third bone
m4x3 r1, R_LOCAL_POSITION, c[a0.x + VSC_BONEBASE]
m3x3 r6, NORMAL, c[a0.x + VSC_BONEBASE]

// weights - final vertex determination
//
// R_LOCAL_POSITION =   (BLENDWEIGHT.x * R_LOCAL_POSITION ) + (BLENDWEIGHT.y * R_LOCAL_POSITION) + (BLENDWEIGHT.z * R_LOCAL_POSITION)
// Sum transformed vertex positions using blend weights
//
mul R_LOCAL_POSITION.xyzw, BLENDWEIGHT.x, r2.xyz
mad R_LOCAL_POSITION.xyzw, BLENDWEIGHT.y, r0.xyzw, R_LOCAL_POSITION.xyzw
mad R_LOCAL_POSITION.xyzw, BLENDWEIGHT.z, r1.xyzw, R_LOCAL_POSITION.xyzw

// Light-normal determination R_LOCAL_NORMAL = ...
mul R_LOCAL_NORMAL.xyzw, BLENDWEIGHT.x, R_LOCAL_NORMAL.xyz
mad R_LOCAL_NORMAL.xyzw, BLENDWEIGHT.y,  r5.xyzw, R_LOCAL_NORMAL.xyzw
mad R_LOCAL_NORMAL.xyzw, BLENDWEIGHT.z, r6.xyzw, R_LOCAL_NORMAL.xyzw

// Necessary. Avoid by smarter use of automatic expansion
mov R_LOCAL_POSITION.w, VSC_ONE

// project world into clip space
m4x4 oPos, R_LOCAL_POSITION, c[VSC_CLIP_MATRIX0]

// Copy vertex position/normal for lighting
// R_LOCAL_POSITION
// R_LOCAL_NORMAL

