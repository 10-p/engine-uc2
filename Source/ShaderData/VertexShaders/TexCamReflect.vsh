//=============================================================================
//	scion sz -- 
//	(VS_Texcoord_CAMERASPACEREFLECTIONVECTOR_Code)
//
//=============================================================================

xvs.1.1

#include "ShaderConstants.vsh"
#include "ShaderRegisters.vsh"
#pragma fragment
#pragma screenspace

// Assumes that R_CAMERASPACE_POSITION and R_CAMERASPACE_NORMAL have already
// been computed and are normalized.
//
// outputs the reflection of the eye vector in camera space about the vertex normal

// normalize the camera space position first
dp3	r0.w, R_CAMERASPACE_POSITION, R_CAMERASPACE_POSITION						// r0.w = x^2 + y^2 + z^2
rsq r0.w, r0.w																	// r0.w = 1 / sqrt(x^2 + y^2 + z^2)
mul r1.xyz, R_CAMERASPACE_POSITION.xyz, r0.w									// r1 = normalized(R_CAMERASPACE_POSITION)
																				
dp3 r0, r1, R_CAMERASPACE_NORMAL												// Dot product Cam*Normal
mul r2, VSC_NEG_TWO, r0															// 
mad R_CAMERASPACE_REFLECT, r2, R_CAMERASPACE_NORMAL, r1							// Compute reflection vector

mov R_CAMERASPACE_REFLECT.w, VSC_ONE