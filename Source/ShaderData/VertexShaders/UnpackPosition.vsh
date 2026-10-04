/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * Unpacks a FPackedPosition into a full vector in object space. The direction
 * of the vector is a packed normal in the position register. The distance
 * along that direction is stored in the back specular register, since no mesh
 * uses that register.
 *
 * ============================================================================
 */

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace


// Muliply the vert (-1 to 1) by the compression scale to recover its position
// around the origin and then add in the origin of the bounding box to move it
// to its final position (this is purposefully not a mad, don't ask why)
mul R_LOCAL_POSITION, POSITION, VSC_COMPRESSION_SCALE_REG
add R_LOCAL_POSITION.xyz, R_LOCAL_POSITION.xyz, VSC_COMPRESSION_OFFSET_REG

// Copy vertex position/normal for lighting
mov R_LOCAL_NORMAL, NORMAL
