//=============================================================================
//	scion sz -- 
//	
//  This file is included D3DShaderConstants.h and also by all the vertex
//  shader assembly files.
//
//=============================================================================

/*=============================================================================
Positive registers
[0..+95]	

Negative registers 
[-1,-96]	Reserved when using the fixed function pipeline - 
			Careful, these get cleared.
-37			Screen Space Calculation [D3D Reserved]
-38			Screen Space Calculation [D3D Reserved]
=============================================================================*/

// Bone Matrices
// [-36..+95]
#define VSC_BONEBASE		-36

// Constants
// [-96,-95]
#define VSC_CONSTANTS0		-96		// [0,1,-2.0,0.5]
#define VSC_CONSTANTS1		-95		// [255.5,255.0,1/255,?]
#define VSC_ZERO			c[-96].xxxx
#define VSC_ONE				c[-96].yyyy
#define VSC_NEG_TWO			c[-96].zzzz
#define VSC_ONEHALF			c[-96].wwww
#define VSC_255_HALF		c[-95].xxxx
#define VSC_255				c[-95].yyyy
#define VSC_ONE_OVER_255	c[-95].zzzz

// Clip Matrix = LocalToWorld * WorldToCamera * CameraToScreen (WVP matrix)
// [-94,-91]
#define VSC_CLIP_MATRIX0	-94
#define VSC_CLIP_MATRIX1	-93
#define VSC_CLIP_MATRIX2	-92
#define VSC_CLIP_MATRIX3	-91
#define VSC_CLIP_MATRIX		VSC_CLIP_MATRIX0

// Texture Stage 0 Transform Matrix - 
// this is the only stage that can have a projected transform so it gets a full 4x4 matrix
// [-90,-88]
#define VSC_TEX0_MATRIX0	-90
#define VSC_TEX0_MATRIX1	-89
#define VSC_TEX0_MATRIX2	-88
#define VSC_TEX0_MATRIX		VSC_TEX0_MATRIX0

// Texture Stage 1 Transform Matrix
// [-87,-85]
#define VSC_TEX1_MATRIX0	-87
#define VSC_TEX1_MATRIX1	-86
#define VSC_TEX1_MATRIX2	-85
#define VSC_TEX1_MATRIX		VSC_TEX1_MATRIX0

// Texture Stage 2 Transform Matrix
// [-84,-82]
#define VSC_TEX2_MATRIX0	-84
#define VSC_TEX2_MATRIX1	-83
#define VSC_TEX2_MATRIX2	-82
#define VSC_TEX2_MATRIX		VSC_TEX2_MATRIX0

// Texture Stage 3 Transform Matrix
// [-81,-79]
#define VSC_TEX3_MATRIX0	-81
#define VSC_TEX3_MATRIX1	-80
#define VSC_TEX3_MATRIX2	-79
#define VSC_TEX3_MATRIX		VSC_TEX3_MATRIX0

// Inverse Transpose Model View Matrix = ((LocalToWorld * WorldToCamera)^-1)^T
// Used for transforming the normal during tex coordinate generation (env mapping)
// [-79,-75]
#define VSC_INVT_MVIEW_MATRIX0	-78
#define VSC_INVT_MVIEW_MATRIX1	-77
#define VSC_INVT_MVIEW_MATRIX2	-76
#define VSC_INVT_MVIEW_MATRIX3	-75
#define VSC_INVT_MVIEW_MATRIX	VSC_INVT_MVIEW_MATRIX0

// Ambient color
// [-74]
#define VSC_AMBIENT_COLOR	-74

// LIGHTS - assuming MAX of 8 lights
// [-73,-50] = [-73, -73 + VSC_LIGHT_SIZE*VS_MAX_Lights - 1]
#define VSC_LIGHT_BASE				-73
#define VSC_LIGHT_DIR_OFFSET		0
#define VSC_LIGHT_POS_OFFSET		1
#define VSC_LIGHT_DIFFUSE_OFFSET	2
#define VSC_LIGHT_SIZE				3
// for hardcoded lights (5 directionals)
// [-73,-69]
#define VSC_HARD_LIGHT_BASE_DIRS	-73
// [-68,-64]
#define VSC_HARD_LIGHT_BASE_COLORS	-68
#define VSC_HARD_LIGHT_BASE			VSC_HARD_LIGHT_BASE_DIRS

// Fog X = Start, Y = End
// [-49]
#define VSC_FOG	-49
#define VSC_FOG_START c[VSC_FOG].x
#define VSC_FOG_END c[VSC_FOG].y

//jg -- For decompressing packed verts
#define VSC_COMPRESSION_SCALE		-48
#define VSC_COMPRESSION_SCALE_REG	c[VSC_COMPRESSION_SCALE]
#define VSC_COMPRESSION_OFFSET		-47
#define VSC_COMPRESSION_OFFSET_REG	c[VSC_COMPRESSION_OFFSET].xyz

// UNUSED SLOTS
// [-46,-44]

// Model View Matrix = LocalToWorld * WorldToCamera (WV matrix)
// Used for tex coordinate generation (env mapping)
// [-43,-40]
#define VSC_MVIEW_MATRIX0	-43
#define VSC_MVIEW_MATRIX1	-42
#define VSC_MVIEW_MATRIX2	-41
#define VSC_MVIEW_MATRIX3	-40
#define VSC_MVIEW_MATRIX	VSC_MVIEW_MATRIX0

// scale x,y for distortion
// [-73] (assuming lighting is off)
#define VSC_DISTORTION_SCALE	-73

// local space projector direction vectors
// [-73,-71] (assuming lighting is off)
#define VSC_PROJ_DIRECTION0		-73
#define VSC_PROJ_DIRECTION1		-72
#define VSC_PROJ_DIRECTION2		-71
#define VSC_PROJ_DIRECTIONS		VSC_PROJ_DIRECTION0

// Local space projector origin vectors
// [-70,-68] (assuming lighting is off)
#define VSC_PROJ_ORIGIN0		-70
#define VSC_PROJ_ORIGIN1		-69
#define VSC_PROJ_ORIGIN2		-68
#define VSC_PROJ_ORIGINS		VSC_PROJ_ORIGIN0

// Project on back face constants for projectors
#define VSC_PROJ_BACKFACES		-67

// Projector attenution distances (1.f / MaxTraceDistance)
// [-66] (assuming lighting is off)
#define VSC_INVMAXTRACEDISTS	-66
#define VSC_INVMAXTRACEDIST0	c[VSC_INVMAXTRACEDISTS].x
#define VSC_INVMAXTRACEDIST1	c[VSC_INVMAXTRACEDISTS].y
#define VSC_INVMAXTRACEDIST2	c[VSC_INVMAXTRACEDISTS].z

// Projector frustum planes.
// [-65,-62] (assuming lighting is off)
#define VSC_PROJ_CLIP_PLANE0	-65
#define VSC_PROJ_CLIP_PLANE1	-64
#define VSC_PROJ_CLIP_PLANE2	-63
#define VSC_PROJ_CLIP_PLANE3	-62
#define VSC_PROJ_CLIP_MATRIX	VSC_PROJ_CLIP_PLANE0

// Projector lifespan attenuation
#define VSC_PROJ_LIFESPAN_ATTEN	-61

// terrain world matrix
// used for the l2w transform for terrain
// using registers from the bone matrices' range
// [-36,-33] (assuming no skinning bone matrices)
#define VSC_TERRAIN_WORLD_MATRIX0		-36
#define VSC_TERRAIN_WORLD_MATRIX1		-35
#define VSC_TERRAIN_WORLD_MATRIX2		-34
#define VSC_TERRAIN_WORLD_MATRIX3		-33
#define VSC_TERRAIN_WORLD_MATRIX		VSC_TERRAIN_WORLD_MATRIX0
// terrain map size (1/width,1/height,?,?)
// [-32].xy
#define VSC_TERRAIN_MAP_ONEOVER_SIZE	-32

// Constants for creating the correct dot product for color remapping
// [-73] (assuming lighting is off)
#define VSC_COLOR_REMAP	-73

// -- End
