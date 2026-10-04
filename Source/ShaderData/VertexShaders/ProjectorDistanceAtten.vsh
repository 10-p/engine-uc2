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

// Now clamp all the attenuations between 0 and 1
max R_LOCAL_DISTATTEN, R_LOCAL_DISTATTEN, VSC_ZERO
//min R_LOCAL_DISTATTEN, R_LOCAL_DISTATTEN, VSC_ONE
