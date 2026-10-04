/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * Simply sets the tex coords for doing the correct dot product for color
 * remapping. The dot product calculates lumnance and then is used to index into
 * another texture.
 *
 * ============================================================================
 */

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// Just fill our 3x2 matrix for the texture lookups
mov oT1, c[VSC_COLOR_REMAP]
mov oT2, c[VSC_COLOR_REMAP]
