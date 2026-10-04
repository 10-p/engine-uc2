//=============================================================================
//	scion sz -- 
//	Vertex Shader Registers
//
//=============================================================================

/*=============================================================================
// cut-n-paste from D3d8Types-Xbox.h
// assignments of vertex input registers for fixed function vertex shader
//
#define D3DVSDE_VERTEX          -1  // Xbox extension, used only in Begin/End bracket
#define D3DVSDE_POSITION        0
#define D3DVSDE_BLENDWEIGHT     1
#define D3DVSDE_NORMAL          2
#define D3DVSDE_DIFFUSE         3
#define D3DVSDE_SPECULAR        4
#define D3DVSDE_FOG             5   // Xbox extension
#define D3DVSDE_BACKDIFFUSE     7   // Xbox extension
#define D3DVSDE_BACKSPECULAR    8   // Xbox extension
#define D3DVSDE_TEXCOORD0       9
#define D3DVSDE_TEXCOORD1       10
#define D3DVSDE_TEXCOORD2       11
#define D3DVSDE_TEXCOORD3       12
=============================================================================*/

#define POSITION        v0
#define BLENDWEIGHT     v1
#define BLENDINDEX		v13		// added for gpu skinning
#define NORMAL          v2
#define DIFFUSE         v3
#define SPECULAR        v4
#define FOG             v5
#define BACKDIFFUSE     v7
#define BACKSPECULAR    v8
#define TEXCOORD0       v9
#define TEXCOORD1       v10
#define TEXCOORD2       v11
#define TEXCOORD3       v12

// direct gpu register usage corresponding to [FVF_GPUReg0 .. FVF_GPUReg15]
#define GPUReg0			v0
#define GPUReg1			v1
#define GPUReg2			v2
#define GPUReg3			v3
#define GPUReg4			v4
#define GPUReg5			v5
#define GPUReg6			v6
#define GPUReg7			v7
#define GPUReg8			v8
#define GPUReg9			v9
#define GPUReg10		v10
#define GPUReg11		v11
#define GPUReg12		v12
#define GPUReg13		v13
#define GPUReg14		v14
#define GPUReg15		v15

#define R_FINAL_COLOR			r11		// running sum of RGB from vertex color and lighting
#define R_LOCAL_POSITION		r10		// local space position (after bone transforms)
#define R_LOCAL_NORMAL			r9		// local space normal (after bone transforms)
#define R_TEX_SOURCE			r8		// temp register to store the texture coordinate source (from stream or from texgen modes) 
#define R_CAMERASPACE_POSITION	r7		// temp register to store vertex position in camera space as tex coordinate source  
#define R_CAMERASPACE_NORMAL	r6		// temp register to store vertex normal in camera space as tex coordinate source
#define R_CAMERASPACE_REFLECT	r5		// temp register to store vertex reflection vector in camera space as tex coordinate source


// Projector temporary registers. Assumes lighting is off

// These are the distance attenuation calculations. They are stored in one
// register so that I can clamp then between 0 & 1 in one instruction
#define R_LOCAL_DISTATTEN	r4
#define R_LOCAL_DISTATTEN0	r4.x
#define R_LOCAL_DISTATTEN1	r4.y
#define R_LOCAL_DISTATTEN2	r4.z

// These are the directional attenuation registers. Same as above, but
// with names that are separate in case we want to change them later
#define R_LOCAL_ATTENUATION	r11
#define R_LOCAL_ATTEN0		r11.x
#define R_LOCAL_ATTEN1		r11.y
#define R_LOCAL_ATTEN2		r11.z

#define R_LOCAL_ATTEN0AND1		r11.xz
#define R_LOCAL_ATTEN2AND3		r11.yw
#define R_LOCAL_DIST_ATTEN0AND1	r4.xz
#define R_LOCAL_DIST_ATTEN2AND3	r4.yw

// Helpers for copying the attenuation into oD0 & oD1
#define R_OUT_ATTEN0AND1		r11.xxzx
#define R_OUT_ATTEN2AND3		r11.xxwy

// -- End
