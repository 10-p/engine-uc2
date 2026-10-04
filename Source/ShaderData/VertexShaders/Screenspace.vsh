//=============================================================================
//	scion sz -- 
//	(VS_Screenspace_Code)
//
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

