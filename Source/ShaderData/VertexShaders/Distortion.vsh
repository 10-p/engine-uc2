//=============================================================================
//	scion sz -- 
//	Distortion Vertex Shader - (VS_Distortion)
//
//  This shader is used along with the Distortion.psh pixel shader to create
//  the distortion effect.
//
//  Implementation Notes:
//  Calculate screenspace position of vertex.  Then offset this position by
//  the distortion scale and vertex alpha.  Vertex alpha is used so that when
//  an object alpha's out (like a particle fading), its distortion will also
//  dissolve away.
//
//  The end result is then a 3x2 matrix as follows:
//  [ (distortion scale)*(vertex alpha), 0, screenspace X+0.5 ]
//  [ (distortion scale)*(vertex alpha), 0, screenspace Y+0.5 ]
//
//  See distortion.psh to see how the above is used.
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// Screenspace instructions from c-37 and c-38
// From 'The oPos to Screen Space Transformation'
// in Vertex Shader Assembler Reference
mul oPos.xyz, r12, c-38						// scale
	+rcc r1.x, r12.w						// compute 1/w
mad oPos.xyz, r12, r1.x, c-37				// scale by 1/w, add offset

// distortion amount modulated by diffuse alpha
mul r1.xy, c[VSC_DISTORTION_SCALE].xy, R_FINAL_COLOR.w

// oT1 = (scale*alpha, 0, screenspace x)
mov oT1.x, r1.x

// oT2 = (scale*alpha, 0, screenspace y)
mov oT2.y, r1.y

// clamp w to -1 or +1
// -1 represents a clipped vertex
slt r1, r1, r1								// r1 gets 0
sge r1, r1, r12.w							// r1 gets 1 if oPos.w is negative
mul r1, r1, VSC_NEG_TWO						// r1 gets -2 if oPos.w is negative
add oPos.w, r1, VSC_ONE						// oPos.w gets -1 or +1 (based on its sign)

// divide by w
mul r2.xy, r12.xy, r12.w
// scale to match back buffer copy size
mul r2.x, r2.x, c[VSC_DISTORTION_SCALE].z
mul r2.y, r2.y, c[VSC_DISTORTION_SCALE].w
// add -0.5 (note, can't combine mul+add to mad with above since 2 different constants are used)
add oT1.z, r2.x, -VSC_ONEHALF
add oT2.z, r2.y, -VSC_ONEHALF

// assuming diffuse color output fragment 
// has been added as well
//mov oD0, DIFFUSE





