/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * Copies the position & normal into their local registers
 *
 * ============================================================================
 */

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// Copy vertex position/normal for lighting
mov R_LOCAL_POSITION, POSITION
mov R_LOCAL_NORMAL, NORMAL
