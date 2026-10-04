//=============================================================================
//	scion sz -- 
//	Distortion Accumulation Vertex Shader - (VS_Distortion_Accum)
//	
//  Outputs oT2 for use as a texcoord to interpolate the amount of distortion
//  across the tri. 
//
//  See DistortionAccum.psh to see how the above is used.
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// scale by distortion amount and vertex alpha
mul r1.x, c[VSC_DISTORTION_SCALE].xy, R_FINAL_COLOR.w

// assuming R_CAMERASPACE_POSITION is valid from TexCamPosition fragment
// distance attenuate distortion
mul r0.x, c[VSC_DISTORTION_SCALE].z, R_CAMERASPACE_POSITION.z
// clamp to [0,1]
min r0.x, r0.x, VSC_ONE
max r0.x, r0.x, VSC_ZERO
// exponential falloff
mul r0.x, r0.x, r0.x
// 1- result
sub r0.x, VSC_ONE, r0.x

// distortion amount = distortion scale * vertex alpha * distance
mul oT2.xy, r0.xx, r1.xx

// z=0, w=1
mov oT2.z, VSC_ZERO
mov oT2.w, VSC_ONE

// assuming diffuse color output fragment 
// has been added as well
//mov oD0, DIFFUSE


