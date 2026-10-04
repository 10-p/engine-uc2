//=============================================================================
//	scion sz -- 
//	(VS_SkelOneBoned)
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
m4x3 R_LOCAL_POSITION, POSITION, c[a0.x + VSC_BONEBASE]
m3x3 R_LOCAL_NORMAL, NORMAL, c[a0.x + VSC_BONEBASE]

// weights - final vertex determination
// not needed since just one bone

// Necessary. Avoid by smarter use of automatic expansion
mov R_LOCAL_POSITION.w, VSC_ONE

// project world into clip space
m4x4 oPos, R_LOCAL_POSITION, c[VSC_CLIP_MATRIX0]

// Copy vertex position/normal for lighting
// R_LOCAL_POSITION
// R_LOCAL_NORMAL
