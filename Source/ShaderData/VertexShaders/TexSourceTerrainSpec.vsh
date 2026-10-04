//=============================================================================
//	scion sz -- 
//	(VS_Texcoord_Source_TerrainSpecial_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// GPUReg15 contains the x,y offsets for the vertex
// these can ve used as our uv stream directly
// note that we multiply by 255 to go from [0,1] to [0,255]
// there is also a +0.5 bias
mul R_TEX_SOURCE, GPUReg15, VSC_255
add R_TEX_SOURCE, R_TEX_SOURCE, VSC_ONEHALF
mul R_TEX_SOURCE.xy, R_TEX_SOURCE.xy, c[VSC_TERRAIN_MAP_ONEOVER_SIZE].xy
// make sure z=0 and w=1
mov R_TEX_SOURCE.zw, c[VSC_CONSTANTS0].xxxy




