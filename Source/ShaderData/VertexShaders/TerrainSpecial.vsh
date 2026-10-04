//=============================================================================
//	scion sz -- 
//	(VS_TerrainSpecial_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// local space position comes from 2 registers
// GPUReg14 contains the height
mov R_LOCAL_POSITION.z, GPUReg14.x
// GPUReg15 contains the offsets
mul R_LOCAL_POSITION.xy, GPUReg15.xy, VSC_255
// w=1.0
mov R_LOCAL_POSITION.w, VSC_ONE

// translate local position with the sector offset
m4x4 R_LOCAL_POSITION, R_LOCAL_POSITION, c[VSC_TERRAIN_WORLD_MATRIX]

// Transform vertex position
m4x4 oPos, R_LOCAL_POSITION, c[VSC_CLIP_MATRIX0]

// copy the local space normal
mov R_LOCAL_NORMAL, NORMAL



