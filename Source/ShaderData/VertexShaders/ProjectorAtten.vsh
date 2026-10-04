/* scion ======================================================================
 * Author: jg/sz
 * ============================================================================
 *
 * Attenuates the projector(s) based upon their distance from the vertex and
 * the MaxTraceDistance value of the projector. The formula for determining
 * this is:
 *
 * 1 - (((VertPos - ProjOrig) dot ProjDir) * (1 / MaxTraceDistance)
 *
 * This is done for each projector and is passed on to the back face
 * attenuation fragment.
 *
 * The back face projector fragment calculates the dot between the vertex
 * normal and the projected direction. It then chooses the max between the dot
 * and the back face culling flags (0.f == back face cull, 1.f == no cull) which
 * results in the projector either being attenuated or not.
 *
 * Finally, it writes the clamped values out to oD0 and oD1
 *
 * ============================================================================
 */

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

 
// Calculate the projector to vertex vector for projector 1
add r0, R_LOCAL_POSITION, -c[VSC_PROJ_ORIGIN0]
// Dot that against the projector direction
// store in R_LOCAL_DISTATTEN.x
dp3 R_LOCAL_DISTATTEN0, r0, -c[VSC_PROJ_DIRECTION0]

// Calculate the projector to vertex vector for projector 2
add r0, R_LOCAL_POSITION, -c[VSC_PROJ_ORIGIN1]
// Dot that against the projector direction
dp3 R_LOCAL_DISTATTEN1, r0, -c[VSC_PROJ_DIRECTION1]

// Calculate the projector to vertex vector for projector 3
add r0, R_LOCAL_POSITION, -c[VSC_PROJ_ORIGIN2]
// Dot that against the projector direction
dp3 R_LOCAL_DISTATTEN2, r0, -c[VSC_PROJ_DIRECTION2]

// Now scale all the attenuations by the inverse max trace distance
// and subtract the result from 1 (R_LOCAL_DISTATTEN is negative)
mul R_LOCAL_DISTATTEN, R_LOCAL_DISTATTEN, c[VSC_INVMAXTRACEDISTS]
add R_LOCAL_DISTATTEN, VSC_ONE, R_LOCAL_DISTATTEN

// We need to clip vertices that fall behind the projector near plane
// This will only happen if the resulting dist atten >= 1. 
// R_LOCAL_DISTATTEN = (R_LOCAL_DISTATTEN < 1) ? R_LOCAL_DISTATTEN : 0;
slt r1, R_LOCAL_DISTATTEN, VSC_ONE
mul R_LOCAL_DISTATTEN, R_LOCAL_DISTATTEN, r1

// Clamp negative values to zero
max R_LOCAL_DISTATTEN, R_LOCAL_DISTATTEN, VSC_ZERO

// Make sure the normal is normalized
dp3	r0.w, R_LOCAL_NORMAL, R_LOCAL_NORMAL				// r0.w = x^2 + y^2 + z^2
rsq r0.w, r0.w											// r0.w = 1 / (x^2 + y^2 + z^2)
mul R_LOCAL_NORMAL.xyz, R_LOCAL_NORMAL.xyz, r0.w

// each projector direction is stored in VSC_PROJ_DIRECTIONS
// 
dp3 R_LOCAL_ATTEN0, R_LOCAL_NORMAL.xyz, -c[VSC_PROJ_DIRECTION0]
dp3 R_LOCAL_ATTEN1, R_LOCAL_NORMAL.xyz, -c[VSC_PROJ_DIRECTION1]
dp3 R_LOCAL_ATTEN2, R_LOCAL_NORMAL.xyz, -c[VSC_PROJ_DIRECTION2]

// Handle the project on backfaces flag correctly. This will choose the max
// between the flag being on (1.f) and the Dir dot Normal.
max R_LOCAL_ATTENUATION, R_LOCAL_ATTENUATION, c[VSC_PROJ_BACKFACES]

// apply another attenuation based on lifespan (assuming values area already [0,1] clamped)
mul R_LOCAL_ATTENUATION, R_LOCAL_ATTENUATION, c[VSC_PROJ_LIFESPAN_ATTEN]

// Modulate the two attenuation factors together
mul R_LOCAL_ATTEN0AND1, R_LOCAL_ATTEN0AND1, R_LOCAL_DIST_ATTEN0AND1
mul R_LOCAL_ATTEN2AND3, R_LOCAL_ATTEN2AND3, R_LOCAL_DIST_ATTEN2AND3

// Now clamp between 0..1 for all values in one instruction
max R_LOCAL_ATTENUATION, R_LOCAL_ATTENUATION, VSC_ZERO

// Writes the x and z values into zw (b & a) channels (Diffuse color)
mov oD0, R_OUT_ATTEN0AND1
// Writes the y and w values into zw (b & a) channels (Specular color)
mov oD1, R_OUT_ATTEN2AND3
