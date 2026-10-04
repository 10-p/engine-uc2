/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * Performs linear interpolation of fog for the given vertex
 *
 * ============================================================================
 */

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

mad oFog.x, -R_CAMERASPACE_POSITION.z, VSC_FOG_START, VSC_FOG_END
