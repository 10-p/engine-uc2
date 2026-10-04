/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * Writes the attenuation values out from the temporary register into oD0 & oD1
 *
 * ============================================================================
 */

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// Writes the x and z values into zw (b & a) channels
mov oD0, R_OUT_ATTEN0AND1
// Writes the y and w values into zw (b & a) channels
mov oD1, R_OUT_ATTEN2AND3
