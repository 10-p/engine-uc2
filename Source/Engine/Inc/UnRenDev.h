/*=============================================================================
	UnRenDev.h: 3D rendering device class.

	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
	Compiled with Visual C++ 4.0. Best viewed with Tabs=4.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#ifndef _UNRENDEV_H_
#define _UNRENDEV_H_

#ifdef _XBOX
#include "ShaderConstants.vsh"
#endif

/*
	FRenderInterface
*/

// Color write modes
// these correspond to D3D's
// D3DCOLORWRITEENABLE_RED,D3DCOLORWRITEENABLE_GREEN,D3DCOLORWRITEENABLE_BLUE,D3DCOLORWRITEENABLE_ALPHA
#ifdef _XBOX
enum EColorWriteMask
{	
	CWM_COLORWRITEENABLE_NONE		=0x0,
	CWM_COLORWRITEENABLE_RED		=1L<<16,
	CWM_COLORWRITEENABLE_GREEN		=1L<<8,
	CWM_COLORWRITEENABLE_BLUE		=1L<<0,
	CWM_COLORWRITEENABLE_ALPHA		=1L<<24,
	CWM_COLORWRITEENABLE_COLORONLY	=CWM_COLORWRITEENABLE_RED|CWM_COLORWRITEENABLE_GREEN|CWM_COLORWRITEENABLE_BLUE,
	CWM_COLORWRITEENABLE_ALL		=0x01010101
};
#else
enum EColorWriteMask
{	
	CWM_COLORWRITEENABLE_NONE		=0x0,
	CWM_COLORWRITEENABLE_RED		=1L<<0,
	CWM_COLORWRITEENABLE_GREEN		=1L<<1,
	CWM_COLORWRITEENABLE_BLUE		=1L<<2,
	CWM_COLORWRITEENABLE_ALPHA		=1L<<3,
	CWM_COLORWRITEENABLE_COLORONLY	=CWM_COLORWRITEENABLE_RED|CWM_COLORWRITEENABLE_GREEN|CWM_COLORWRITEENABLE_BLUE,
	CWM_COLORWRITEENABLE_ALL		=0xF
};
#endif

// Cull modes.

enum ECullMode
{
	CM_CW,
	CM_CCW,
	CM_None
};

// Transform types.

enum ETransformType
{
	TT_LocalToWorld,
	TT_WorldToCamera,
	TT_CameraToScreen
};

// Texture coordinate sources.
enum ETexCoordSource
{
	TC_UVStream,
	TC_CameraPosition,
	TC_CameraNormal,
	TC_CameraReflectionVector
};

// Primitive types for DrawPrimitive.

//scion jg -- Changed this so that the types match one for one
// with the platform they are being used on. Much faster.
#ifdef _XBOX
enum EPrimitiveType
{
	PT_PointList		= 1,
	PT_LineList			= 2,
	PT_LineLoop			= 3,
	PT_LineStrip		= 4,
	PT_TriangleList		= 5,
	PT_TriangleStrip	= 6,
	PT_TriangleFan		= 7,
	PT_QuadList			= 8,
	PT_QuadStrip		= 9,
	PT_Polygon			= 10
};
#else
enum EPrimitiveType
{
	PT_PointList		= 1,
	PT_LineList			= 2,
	PT_LineStrip		= 3,
	PT_TriangleList		= 4,
	PT_TriangleStrip	= 5,
	PT_TriangleFan		= 6,
	PT_QuadList
};
#endif



// scion sz -
// Final enumeration is used to map to a vertex shader that has already been created.
// Vertex shaders for SetVertexStreams
//

//enum EVertexShader
//{	
	const QWORD VS_FixedFunction					= 0;					// If no vertex shader enumerations we use the fixed function pipeline

	/* 7-0 */
	const QWORD VS_NotBoned							= 0x1 << 0;				
	const QWORD VS_SkelOneBoned						= 0x1 << 1;			// GPU skinning with 1 bone influence per vertex
	const QWORD VS_SkelTwoBoned						= 0x1 << 2;			// GPU skinning with 2 bone influence per vertex
	const QWORD VS_SkelThreeBoned					= 0x1 << 3;			// GPU skinning with 3 bone influence per vertex
	const QWORD VS_SkelFourBoned					= 0x1 << 4;			// GPU skinning with 4 bone influence per vertex
	const QWORD VS_VertexColored					= 0x1 << 5;			// Use Vertex color
	const QWORD VS_Lit								= 0x1 << 6;			// Lit
	const QWORD VS_Fog								= 0x1 << 7;			// Fragment for calculating fog

	/* 23-8 used for light types */
	const QWORD VS_Light0							= 0x3 << 8;			// Light type for light 0 .. see EVertexShaderLightType (4 possible values)
	const QWORD VS_Light1							= 0x3 << 10;		// " light 1
	const QWORD VS_Light2							= 0x3 << 12;		// " light 2
	const QWORD VS_Light3							= 0x3 << 14;		// " light 3
	const QWORD VS_Light4							= 0x3 << 16;		// " light 4
	const QWORD VS_Light5							= 0x3 << 18;		// " light 5
	const QWORD VS_Light6							= 0x3 << 20;		// " light 6
	const QWORD VS_Light7							= 0x3 << 22;		// " light 7
	
	/* 35-24 texcoord source types */
	const QWORD VS_Tex0_Source						= 0x7 << 24;			// Source texture coordinates for Tex 0 .. see EVertexShaderTexSource (8 possible values)
	const QWORD VS_Tex1_Source						= 0x7 << 27;			// " Tex 1
	const QWORD VS_Tex2_Source						= 0x7 << 30;			// " Tex 2
	const QWORD VS_Tex3_Source						= ((QWORD)0x7) << 33;	// " Tex 3

	/* 43-36 tex transform types */	
	const QWORD VS_Tex0_Transform					= ((QWORD)0x3) << 36;	// Enables texture transform for Tex 0 .. see EVertexShaderTexTransformType (4 possible values)
	const QWORD VS_Tex1_Transform					= ((QWORD)0x3) << 38;	// " Tex 1
	const QWORD VS_Tex2_Transform					= ((QWORD)0x3) << 40;	// " Tex 2
	const QWORD VS_Tex3_Transform					= ((QWORD)0x3) << 42;	// " Tex 3

	//jg -- Changed how positions are calculated	
	// 44 - Unpack position (position * backspecular)
	const QWORD VS_UnpackPosition					= (((QWORD)1) << 44);
	// Helper defines to make things easier
	#define VS_Normal (VS_NotBoned)
	#define VS_CompressedMesh (VS_UnpackPosition | VS_NotBoned)	

	// 45 - uses special vertex coloring needed for static meshes
	const QWORD VS_StaticMeshVertexColor			= (((QWORD)1) << 45);

	// 46 - used to derive x,y,u,v coords of terrain vertices implicitly
	const QWORD VS_TerrainSpecial					= (((QWORD)1) << 46);

	// 45 - uses special vertex coloring needed for static meshes 
	// since there can be more than one color stream
	const QWORD VS_StaticMeshVertexColorDiffuse		= (((QWORD)1) << 47);

	/* no free slots left!*/

	/* 63-48 reserved slots for unique shaders (such as distortion,etc..)
	 * This range is used as 16 bit number instead of bitmask since the unique
	 * shaders will NOT be combined in any way.  Note that a unique shader
	 * CAN be combined with any of the above enumerations however.
	 * See EUniqueVertexShader.
	 */

//};

// Force hardcoding of directional lights
// so that there are always VS_Lights_Directional_MAX enabled
// This was done to reduce vertex shader combinations
#define HARDCODED_VS_LIGHTS

#define VS_Lights_Directional_MAX 5
#define VS_Lights_Point_MAX 0
#define VS_Lights_Spot_MAX 0
#define VS_Lights_MAX 8
#define VS_SkeletalSkin (VS_SkelOneBoned|VS_SkelTwoBoned|VS_SkelThreeBoned|VS_SkelFourBoned)

// scion sz -
// upper 16 bit of QWORD used for vertex shader enumerations can be
// any of these unique vertex shaders.  2^16 (65,535) max value.
enum EUniqueVertexShader
{
	// use with PS_Distortion pixel shader
	VS_Distortion =1,	
	// Projector code for distance attenuation and back face culling
	VS_Projector_Atten,
	// same as VS_Projector_Atten except it adds a fragment to frustum clip pixels
	VS_Projector_Atten_Clip,
	// Used to push a 3x2 matrix into the PS_ColorRemap shader via t1, t2
	VS_ColorRemap,
	// used with PS_DistortionAccum pixel shader for accumulating distoriton vectors
	VS_DistortionAccum
	// Max = 65,535
};

#define VS_GETUNIQUESHADER( VertexShaderEnum ) ( (VertexShaderEnum>>48)&0xffff )
#define VS_SETUNIQUESHADER( UniqueShader, VertexShaderEnum ) { VertexShaderEnum |= ((QWORD)UniqueShader)<<48; }
#define VS_GETUNIQUESHADER_QWORD( UniqueShader ) ( ((QWORD)UniqueShader)<<48 )

// scion sz -
// Each VS_Light# entry can be one of these
// !These correspond to D3DLIGHTTYPE!
enum EVertexShaderLightType
{
	VS_Light_Type_None =0,
	VS_Light_Type_Point,
	VS_Light_Type_Spot,
	VS_Light_Type_Directional
};

#define VS_GETLIGHTTYPE( LightIdx, VertexShaderEnum ) ((VertexShaderEnum >> (22-(2*LightIdx))) & 0x03)
#define VS_SETLIGHTTYPE( LightIdx, LightType, VertexShaderEnum ) { VertexShaderEnum |= ((QWORD)(LightType&0x3)) << (22-(2*LightIdx)); }


// scion sz -
// Each texture coordinate can have a source mapping
// source mappings can be one of 8 modes:
// the first 4 are just coordinate remapping and then there are 3 texgen modes
enum EVertexShaderTexSource
{
	VS_Tex_Source0 =0,
	VS_Tex_Source1,
	VS_Tex_Source2,
	VS_Tex_Source3,
	VS_Tex_Source_CAMERASPACEPOSITION,
	VS_Tex_Source_CAMERASPACENORMAL,
	VS_Tex_Source_CAMERASPACEREFLECTIONVECTOR,
	VS_Tex_Source_None
};

// scion sz -
// texture transform to apply to a given texture coordinate
enum EVertexShaderTexTransformType
{
	VS_Tex_Transform_None =0,
	VS_Tex_Transform_m4x2,				// typical transform
	VS_Tex_Transform_m4x3,				// cubemaps or any 3 coordinate transform (without projection)
	VS_Tex_Transform_m4x3_Projected,	// projected textures
};

#define VS_GETTEXSOURCE( TexIdx, VertexShaderEnum ) ((VertexShaderEnum >> (33-(3*TexIdx))) & 0x07)
#define VS_SETTEXSOURCE( TexIdx, TexSource, VertexShaderEnum ) { VertexShaderEnum |= ((QWORD)(TexSource&0x07)) << (33-(3*TexIdx)); }

#define VS_GETTEXTRANSFORM( TexIdx, VertexShaderEnum) ((VertexShaderEnum >> (42-(2*TexIdx))) & 0x03)
#define VS_SETTEXTRANSFORM( TexIdx, TexTransform, VertexShaderEnum) { VertexShaderEnum |= ((QWORD)(TexTransform&0x3)) << (42-(2*TexIdx)); }

// scion sz -
// Identifies Vertex shader code fragments to combine together.
// Each enumeration corresponds to a vertex shader file (.vsh)
enum EVertexShaderCodeFragment
{
	VS_Screenspace_Code =0,											// final transformation from homogenous clip space to screen space
	VS_NotBoned_Code,												// applies the World*View*Proj	
	
	VS_SkelOneBoned_Code,											// applies World*(Bone1*Weight1)*View*Proj
	VS_SkelTwoBoned_Code,											// applies World*(Bone1*Weight1)*(Bone2*Weight2)*View*Proj
	VS_SkelThreeBoned_Code,											// applies World*(Bone1*Weight1)*(Bone2*Weight2)*(Bone3*Weight3)*View*Proj
	VS_SkelFourBoned_Code,											// applies World*(Bone1*Weight1)*(Bone2*Weight2)*(Bone3*Weight3)*(Bone4*Weight4*)*View*Proj
	
	VS_InitLighting_Code,											// intialize lighting
	VS_Light_Ambient_Code,											// global ambient light
	VS_Light_Directional_Code,										// directional light
	VS_Light_Point_Code,											// point light	
	VS_Light_Spot_Code,												// spot light
	VS_Next_Light_Code,												// selects the next light

	VS_Light_HardDirectional5_Code,									// hard coded 5 directional lights :(

	VS_InitRGB_Code,												// Initialize final color to 0
	VS_Output_LightRGB_Code,										// Output result from lighting calculations including ambient to output color (oD0)
	VS_Output_VertexRGB_Code,										// Output just vertex diffuse color to output color (oD0)
	VS_Add_VertexRGB_Code,											// adds vertex diffuse color to output color (oD0)
	VS_Add_VertexSpecular_Code,										// adds vertex specular color to output color (oD0)
	VS_Mul_VertexSpecular_Code,										// modulate current output light value with specular component (used by static mesh static/dynamic lighting)
 	
	VS_Texcoord0_Pass_Code,											// just copies default texture coordinate (TEXCOORD0) directly into oT0
	VS_Texcoord1_Pass_Code,											// just copies default texture coordinate (TEXCOORD1) directly into oT1
	VS_Texcoord2_Pass_Code,											// just copies default texture coordinate (TEXCOORD2) directly into oT2
	VS_Texcoord3_Pass_Code,											// just copies default texture coordinate (TEXCOORD3) directly into oT3
	
	VS_Texcoord0_Transform_Code,									// applies texture transform matrix to default source and copies it to oT0
	VS_Texcoord1_Transform_Code,									// " oT1
	VS_Texcoord2_Transform_Code,									// " oT2
	VS_Texcoord3_Transform_Code,									// " oT3
	
	VS_Texcoord_Source0_Code,										// maps TEXCOORD0 as input texture coordinates for any stage
	VS_Texcoord_Source1_Code,										// " TEXCOORD1
	VS_Texcoord_Source2_Code,										// " TEXCOORD2
	VS_Texcoord_Source3_Code,										// " TEXCOORD3
	VS_Texcoord_Source_CAMERASPACEPOSITION_Code,					//
	VS_Texcoord_Source_CAMERASPACEREFLECTIONVECTOR_Code,			//
	VS_Texcoord_Source_CAMERASPACENORMAL_Code,						//
	VS_Texcoord_Source_TerrainSpecial_Code,							// generate uv coordinate based on terrain x,y offsets and store them in the R_TEX_SOURCE register for use
	VS_Texcoord_CAMERASPACEPOSITION_Code,							// emulates fixed function's D3DTSS_TCI_CAMERASPACEPOSITION and maps it as the input texture coordinates for any stage
	VS_Texcoord_CAMERASPACEREFLECTIONVECTOR_Code,					// emulates fixed function's D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR and maps it as the input texture coordinates for any stage
	VS_Texcoord_CAMERASPACENORMAL_Code,								// emulates fixed funciton's D3DTSS_TCI_CAMERASPACENORMAL and maps it as the input texture coordinates for any stage
	
	VS_Texcoord0_Source_Pass_Code,									// Just copies R_TEX_SOURCE to oT0
	VS_Texcoord1_Source_Pass_Code,									// " oT1
	VS_Texcoord2_Source_Pass_Code,									// " oT2
	VS_Texcoord3_Source_Pass_Code,									// " oT3

	VS_Texcoord0_Source_Transform_m4x2_Code,						// applies 4x2 texture transform matrix to R_TEX_SOURCE and copies to oT0
	VS_Texcoord1_Source_Transform_m4x2_Code,						// " oT1
	VS_Texcoord2_Source_Transform_m4x2_Code,						// " oT2
	VS_Texcoord3_Source_Transform_m4x2_Code,						// " oT3

	VS_Texcoord0_Source_Transform_m4x3z_Code,						// applies 4x3 (with z) texture transform matrix to R_TEX_SOURCE and copies to oT0
	VS_Texcoord1_Source_Transform_m4x3z_Code,						// " oT1
	VS_Texcoord2_Source_Transform_m4x3z_Code,						// " oT2
	VS_Texcoord3_Source_Transform_m4x3z_Code,						// " oT3

	VS_Texcoord0_Source_Transform_m4x3w_Code,						// applies 4x3 (with w) texture transform matrix to R_TEX_SOURCE and copies to oT0
	VS_Texcoord1_Source_Transform_m4x3w_Code,						// " oT1
	VS_Texcoord2_Source_Transform_m4x3w_Code,						// " oT2
	VS_Texcoord3_Source_Transform_m4x3w_Code,						// " oT3

	VS_Texcoord3_Source_Texkill_Code,								// use for passing frustum clip planes to pixel shader 
																	// via oT3 in order to texkill (clip/cull) pixels

	VS_Distortion_Code,												// Distortion
	VS_DistortionAccum_Code,										// Distortion Accumulation
	VS_TerrainSpecial_Code,											// optimized terrain
	
	VS_Projector_Atten_Code,										// Projector attenuation by face normal & distance
	VS_UnpackPosition_Code,
	VS_DefaultPosition_Code,

	VS_Fog_Code,

	VS_ColorRemap_Code,												// Used for PS_ColorRemap

	VS_MAX_Code
};

// Stencil buffer operations.

#ifdef _XBOX
//scion jg -- Use the same values as the xbox so that we can
// do a cheaper cast instead of function call to map these
enum EStencilOp
{
	SO_Keep			= 0x1e00,
	SO_Zero			= 0,
	SO_Replace		= 0x1e01,
	SO_IncrementSat	= 0x1e02,
	SO_DecrementSat	= 0x1e03,
	SO_Invert		= 0x150a,
	SO_Increment	= 0x8507,
	SO_Decrement	= 0x8508
};
#else
enum EStencilOp
{
	SO_Keep			= 1,
	SO_Zero			= 2,
	SO_Replace		= 3,
	SO_IncrementSat	= 4,
	SO_DecrementSat	= 5,
	SO_Invert		= 6,
	SO_Increment	= 7,
	SO_Decrement	= 8
};
#endif

// Comparison functions.
#ifdef _XBOX
//scion jg -- Use the same values as the xbox so that we can
// do a cheaper cast instead of function call to map these
enum ECompareFunction
{
	CF_Never		= 0x200,
	CF_Less			= 0x201,
	CF_Equal		= 0x202,
	CF_LessEqual	= 0x203,
	CF_Greater		= 0x204,
	CF_NotEqual		= 0x205,
	CF_GreaterEqual	= 0x206,
	CF_Always		= 0x207
};
#else
enum ECompareFunction
{
	CF_Never		= 1,
	CF_Less			= 2,
	CF_Equal		= 3,
	CF_LessEqual	= 4,
	CF_Greater		= 5,
	CF_NotEqual		= 6,
	CF_GreaterEqual	= 7,
	CF_Always		= 8
};
#endif

enum EFogState
{
	FS_Store		= 0,
	FS_Restore		= 1,
	FS_Ignore		= 2
};

enum EHardwareEmulationMode
{
	HEM_None		= 0,
	HEM_GeForce1	= 1,
	HEM_XBox		= 2,
};

// Precaching options.
enum EPrecacheMode
{
	//scion jg -- Added so we have something useful to compare against
	PRECACHE_None,
	PRECACHE_VertexBuffers,
	PRECACHE_VertexShaders,		// scion sz - precaching vertex shader microcode
	PRECACHE_All
};

// Class definition
#include "PushBuffer.h"

//scion jg -- Common routine for allocating a push buffer
FORCEINLINE FPushBuffer* appAllocPushBuffer(DWORD dwSize);

//scion jg -- For compiling indices into runnable push buffers
FPushBuffer* appCompileIndicesToPushBuffer(EPrimitiveType Prim,
	DWORD dwVertCount,const _WORD* pIndices,UBOOL bRejectRunAsCopy = 1);

#define STATICMESH_PB_PF TEXT("%s_%d_pb")
#define PROJECTOR_PB_PF TEXT("%s_pb")
#define SKELMESH_PB_PF TEXT("%s_%d_%d_pb")

#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
/*---------------------------------------------------*/
/*  These defines describe the binary format of the  */
/*  push-buffer.  They may be used in conjunction    */
/*  with BeginState/EndState, BeginPush/EndPush and  */
/*  RunPushBuffer.                                   */
/*---------------------------------------------------*/

/* Encoding support */

#define D3DPUSH_ENCODE(Method, Count)   (((Count) << 18) + (Method))
#define D3DPUSH_NOINCREMENT_FLAG        0x40000000
#define D3DPUSH_MAX_COUNT               2047

/* Methods */

#define D3DPUSH_NO_OPERATION                0x00000100 // Parameter must be zero
#define D3DPUSH_SET_BEGIN_END               0x000017fc // Parameter is D3DPRIMITIVETYPE or 0 to end
#define D3DPUSH_INLINE_ARRAY                0x00001818 // Use NOINCREMENT_FLAG
#define D3DPUSH_SET_TRANSFORM_CONSTANT_LOAD 0x00001ea4 // Add 96 to constant index parameter
#define D3DPUSH_SET_TRANSFORM_CONSTANT      0x00000b80 // Can't use NOINCREMENT_FLAG, maximum of 32 writes

// D3D push buffer commands that aren't in D3D8Types.h

#define D3DPUSH_DRAW_INDEX_BATCH 0x1800
// Use the one below when you have an odd number of vertices, since they are
// normally encoded in pairs.
#define D3DPUSH_DRAW_INDEX_LAST 0x1808 // single vertex
// Remember indices are encoded in pairs, hence their 16bit-ness
#define D3DPUSH_ENCODE_INDEX(index1,index2) (((index2) << 16) + (index1))

// Defines for managing the push buffer
#define MAX_DWORDS_IN_INDEX_PUSH 2047
#define MAX_INDICES_IN_PUSH (MAX_DWORDS_IN_INDEX_PUSH * 2)

// This is the minimum size for a runnable push buffer. Any smaller and it
// is more efficient to use a RunAsCopy push buffer.
#define MIN_RUNNABLE_SIZE 2540
#define MIN_ALLOC_SIZE 660

#endif

#ifdef _XBOX

//scion jg -- For creating D3DPerf events for viewing in pix
void appD3DBeginEvent(const FColor& Color,char* szText);
void appD3DEndEvent(void);

#ifndef FINAL_RELEASE//scion jg -- PIX logging support

INT appLogva(TCHAR* Dest,INT Count,const TCHAR* Fmt,va_list Args);

// Standard colors
#define PIX_SKEL_MESH FColor(255,0,0,255)
#define PIX_STATIC_MESH FColor(0,128,255,255)
#define PIX_CORONA FColor(0,255,255,255)
#define PIX_CANVAS FColor(128,255,255,255)
#define PIX_SCREEN_PRIM FColor(128,128,255,255)
#define PIX_TERRAIN FColor(0,128,0,255)
#define PIX_SHADOW FColor(128,128,128,255)
#define PIX_BSP FColor(255,128,0,255)
#define PIX_FLUID_SURF FColor(0,0,128,255)
#define PIX_PARTICLE FColor(128,0,128,255)
#define PIX_PROJECTOR FColor(0,128,128,255)
#define PIX_DECO FColor(64,128,128,255)

/* scion ======================================================================
 * Author: jg
 *
 * This struct is used to create scoped D3D/PIX events. It logs the event and
 * automatically closes the event upon destruction.
 *
 * ============================================================================
 */
struct FD3DEvent
{
	FD3DEvent() {}
	// Terminate the event based upon scope
	~FD3DEvent()
	{
		appD3DEndEvent();
	}
	// Operator for logging a PIX event with var args
	void __cdecl operator()(const FColor& Color,const TCHAR* pszFmt, ...) const
	{
		va_list ptr;
		va_start(ptr, pszFmt);
		TCHAR TempStr[256];
		// Build the string in the temp buffer
		appLogva(TempStr,ARRAY_COUNT(TempStr),pszFmt,ptr);
		va_end(ptr);
		// Now trigger the D3D event
		appD3DBeginEvent(Color,TCHAR_TO_ANSI(TempStr));
	}
};

// Macros to allow for scoping of D3D draw events. Let's us log
// rendering calls to PIX for later inspection
#define BEGIN_D3D_EVENT(x) { FD3DEvent Event_##x; Event_##x
#define END_D3D_EVENT }
#else
	// Compile these out in final builds
	#define PIX_SKEL_MESH 1
	#define PIX_STATIC_MESH 1
	#define PIX_CORONA 1
	#define PIX_CANVAS 1
	#define PIX_SCREEN_PRIM 1
	#define PIX_TERRAIN 1
	#define PIX_SHADOW 1
	#define PIX_BSP 1
	#define PIX_FLUID_SURF 1
	#define PIX_PARTICLE 1
	#define PIX_PROJECTOR 1
	#define PIX_DECO 1
#if defined(_MSC_VER)
	#define BEGIN_D3D_EVENT(x) { __noop
#else
	/* Used as BEGIN_D3D_EVENT(name)(colour,format,...): swallow the second argument list unevaluated,
	   which is what the MSVC __noop intrinsic does. */
	#define BEGIN_D3D_EVENT(x) { UC2_D3D_EVENT_NOOP
	#define UC2_D3D_EVENT_NOOP(...) ((void)0)
#endif
	#define END_D3D_EVENT }
#endif
#else
	// Compile these out in non-xbox builds
	#define PIX_SKEL_MESH 1
	#define PIX_STATIC_MESH 1
	#define PIX_CORONA 1
	#define PIX_CANVAS 1
	#define PIX_SCREEN_PRIM 1
	#define PIX_TERRAIN 1
	#define PIX_SHADOW 1
	#define PIX_BSP 1
	#define PIX_FLUID_SURF 1
	#define PIX_PARTICLE 1
	#define PIX_PROJECTOR 1
	#define PIX_DECO 1
#if defined(_MSC_VER)
	#define BEGIN_D3D_EVENT(x) { __noop
#else
	/* Used as BEGIN_D3D_EVENT(name)(colour,format,...): swallow the second argument list unevaluated,
	   which is what the MSVC __noop intrinsic does. */
	#define BEGIN_D3D_EVENT(x) { UC2_D3D_EVENT_NOOP
	#define UC2_D3D_EVENT_NOOP(...) ((void)0)
#endif
	#define END_D3D_EVENT }
#endif

class ENGINE_API FRenderInterface
{
public:

	virtual void PushState() = 0;
	virtual void PopState() = 0;

	virtual UBOOL SetRenderTarget(FRenderTarget* RenderTarget) = 0;
	//scion jg -- Added for restoring previous state without Push/PopState()
	virtual void ResetRenderTarget(void) = 0;
	virtual FRenderTarget* GetRenderTarget() = 0;
	virtual FRenderTargetPool* GetRenderTargetPool() { return NULL; }
	virtual void SetViewport(INT X,INT Y,INT Width,INT Height) = 0;
	// scion sz -
	virtual void SetScreenSpaceOffset( FLOAT OffsetX, FLOAT OffsetY ) =0;
	//scion jg -- Added for resetting to previous state
	virtual void ResetViewport(void)=0;
	virtual void GetViewport(INT* X,INT* Y,INT* Width,INT* Height) = 0;
	virtual UViewport* GetCurViewportPtr() { return NULL; }
	virtual void Clear(UBOOL UseColor = 1,FColor Color = FColor(0,0,0),UBOOL UseDepth = 1,FLOAT Depth = 1.0f,UBOOL UseStencil = 1,DWORD Stencil = 0) = 0;
	virtual void SetColorWriteMask( DWORD ColorMask ) {}
	//scion jg -- Allows the direct setting of a renderstate bypassing the deferred commit
	virtual void SetRenderStateDirect(BYTE State,DWORD Value) {}
	//jg -- So we can set vertex shader constants
	virtual void SetVertexShaderConstant(INT Register,const void* pConstantData,DWORD ConstantCount) = 0;

	virtual void PushHit(const BYTE* Data,INT Count) = 0;
	virtual void PopHit(INT Count,UBOOL Force) = 0;

	virtual void SetCullMode(ECullMode CullMode) = 0;
	virtual ECullMode GetCullMode() = 0;

	virtual void SetAmbientLight(FColor Color) = 0;
	// scion sz - changed FSphere(FVector(0,0,0),0) to GMath.ZeroSphere
	virtual void EnableLighting(UBOOL UseDynamic, UBOOL UseStatic=1, UBOOL Modulate2X=0, FBaseTexture* UseLightmap=NULL, UBOOL LightingOnly=0,const FSphere& LitSphere=GMath.ZeroSphere) = 0;
	virtual void SetLight(INT LightIndex,class FDynamicLight* Light, FLOAT Scale=1.0f) = 0;
	
	virtual void SetNPatchTesselation( FLOAT Tesselation ) = 0;
	virtual void SetDistanceFog(UBOOL Enable,FLOAT FogStart,FLOAT FogEnd,FColor Color) = 0;
	virtual void SetGlobalColor(FColor Color) = 0;
	virtual void SetTransform(ETransformType Type,const FMatrix& Matrix) = 0;
	virtual INT SetMaterial(UMaterial* Material,FString* ErrorString=NULL, UMaterial** ErrorMaterial=NULL, INT* NumPasses=NULL) = 0;
	// scion sz - material in current state
	virtual UMaterial* GetLastMaterial() { return NULL; }

	virtual DWORD GetMaterialVertexShaderType(UMaterial* Material) = 0;

	virtual void SetStencilOp(ECompareFunction Test,DWORD Ref,DWORD Mask,EStencilOp FailOp,EStencilOp ZFailOp,EStencilOp PassOp,DWORD WriteMask) = 0;
	//scion jg -- Added to cut down on stencil state changes
	virtual void ResetStencilOp(void) = 0;
	//scion jg -- Used to determine if we are back buffer rendering or not
	virtual UBOOL IsDefaultRenderTarget(void) = 0;
	//scion jg -- Added for shadows being able to disable stencil (needed for L8 rendering)
	virtual void EnableStencil(UBOOL bEnable) = 0;
	//scion jg -- Added for shadows being able to disable Z-buffer (needed for L8 rendering)
	virtual void EnableDepth(UBOOL bEnable) = 0;

	virtual void SetPrecacheMode( EPrecacheMode PrecacheMode ) = 0;
	//scion jg -- So we can query the mode. Always return precache all to find errors faster
	virtual EPrecacheMode GetPrecacheMode(void) { return PRECACHE_All; }
	// Soon to disappear
		virtual void SetZBias(INT ZBias) = 0;

	virtual void AddVertexShaderEnum( QWORD VertexShaderEnum ) = 0;
	virtual INT SetVertexStreams(QWORD VertexShaderEnum,FVertexStream** Streams,INT NumStreams) = 0;
	virtual INT SetDynamicStream(QWORD VertexShaderEnum,FVertexStream* Stream) = 0;

	virtual INT SetIndexBuffer(FIndexBuffer* IndexBuffer,INT BaseIndex) = 0;
	virtual INT SetDynamicIndexBuffer(FIndexBuffer* IndexBuffer,INT BaseIndex) = 0;

	virtual void DrawPrimitive(EPrimitiveType PrimitiveType,INT FirstIndex,INT NumPrimitives,INT MinIndex = INDEX_NONE,INT MaxIndex = INDEX_NONE) = 0;
#ifdef _XBOX
	//scion jg -- For rendering small pre-built dynamic vertex buffers without stalling
	virtual void DrawPrimitiveUP(QWORD VertexShaderEnum,EPrimitiveType PrimitiveType,
		INT NumPrimitives,FVertexStream* pVBStream,FIndexBuffer* pIBStream = NULL) {}
	//scion jg -- Used to execute a push buffer. If the bUseIndexStream is false,
	// the index stream is not committed. No render states are committed if
	// bCommit is zero
	virtual void RunPushBuffer(FPushBuffer* pPushBuffer,
		UBOOL bUseIndexStream = 0, UBOOL bCommit = 1) {}
	//scion jg -- Used to get a pointer directly into the main push buffer
	virtual DWORD* BeginPush(DWORD dwSizeNeeded,UBOOL bCommitStates = 1,
		UBOOL bSetTexCoords = 1) { return NULL; }
	//scion jg -- Used to get a pointer directly into the main push buffer
	virtual DWORD* BeginPush(DWORD dwSizeNeeded,QWORD VertexShaderEnum,
		FVertexStream* pVBStream,UBOOL bCommitStates = 1,
		UBOOL bSetTexCoords = 1) { return NULL; }
	//scion jg -- Releases the push buffer grabbed with BeginPush
	virtual void EndPush(DWORD* pEndPointer) {}
	//scion jg -- Allows you to control the locking scheme of a D3D resource.
	// Insert a fence just after rendering a resource in order to have the
	// cpu get the resource faster. Returns the fence that was inserted.
	virtual DWORD InsertFence(void) { return (DWORD)-1; }
#else
	//scion jg -- So the editor can report how many passes a material is on BSP
	virtual DWORD GetStageCountForPass(INT Pass) { return 0; }
#endif

	// scion sz - back buffer blit copy used for post process effects
	virtual UMaterial* GetBackBufferCopyMaterial() { return NULL; }
	virtual FRenderTarget* GetBackBufferCopyTarget() { return NULL;	}
	virtual void SetBackBufferCopyTarget(FRenderTarget* RenderTarget) {}
	virtual void UpdateBackBufferCopyTarget(UViewport* InViewport=NULL) {}
	virtual UMaterial* GetBackBufferMaterial() { return NULL; }
	// scion sz - copy depth buffer from one render target to another
	virtual void CopyDepthBuffer( FRenderTarget* DestTarget, FRenderTarget* SrcTarget ) {}
	
	
	// scion sz - the different steps for rendering distortion
	enum EPostProcessStage
	{
		PP_STAGE_NONE,
		PP_STAGE_ACCUMULATE,
		PP_STAGE_RENDER_COLOR,
		PP_STAGE_RENDER_DISTORTION
	};
	virtual void SetPostProcessStage( EPostProcessStage InStage ) {}
	virtual EPostProcessStage GetPostProcessStage() { return PP_STAGE_NONE; }

	virtual void ForceDefSphereMappingMode( UBOOL bEnable ){}

	// scion sz -- update the gamma curve based on the current scene luminosity
	virtual void UpdateDynamicGamma() {}
	virtual FLOAT GetAverageSceneLuminosity() { return 0.5f;	}

	// Skeletal GPU shader support.	
	virtual void SetSkinBones( INT FirstBoneDestIndex, FCoords* FirstBone, INT BoneCount ) {}
	
	//scion rwd - added better material precaching
	virtual void PrecacheMaterial(UMaterial *material) {}

	// scion sz - updates the lookup table for depth of field
	virtual void UpdateDOFRampLookupTexture( UMaterial* InDOFShader, BYTE FocusType ) {}

	// scion sz - sets the terrain world transform
	virtual void SetTerrainTransform( FMatrix& WorldMat, INT HeightMapX, INT HeightMapY ) {};

	//jg -- Used for occlusion queries
	virtual UBOOL BeginOcclusionQuery(void) = 0;
	virtual UBOOL EndOcclusionQuery(DWORD QueryNum) = 0;
	virtual UBOOL GetOcclusionQueryResults(DWORD QueryNum,DWORD& dwPixels) = 0;	

	// scion sz - for adjusting perspective matrix based on pixel size
	virtual FLOAT GetPixelAspectRatio()
	{
		return 1.f;
	}
};

/*
	URenderDevice
*/

enum EDescriptionFlags
{
	RDDESCF_Certified       = 1,
	RDDESCF_Incompatible    = 2,
	RDDESCF_LowDetailWorld  = 4,
	RDDESCF_LowDetailSkins  = 8,
	RDDESCF_LowDetailActors = 16,
};

// Usage types (determine hardware/software shader and buffer choices)
enum EUsageType
{
	UT_Terrain,
	UT_Static,
	UT_Skin,	
	UT_Morphed,
	UT_World
};

//
// FRenderCaps - render device capabilities exposed to the engine
//
struct FRenderCaps
{
	INT MaxSimultaneousTerrainLayers;
	INT		PixelShaderVersion;	//!!powervr_aaron: Terrain optimisation code needs to know whether pixel shaders are being used
	UBOOL	HardwareTL;			//!!powervr_aaron: Terrain optimisation code needs to know whether SW TnL is being used

	// set default render caps in constructor
	FRenderCaps()
	:	MaxSimultaneousTerrainLayers(1),
		PixelShaderVersion(0),
		HardwareTL(0)
	{}
};

//scion jg -- Changes for faster caching scheme
enum ECacheHint
{
	CH_Texture,
	CH_VertexBuffer,
	CH_IndexBuffer
};

//
// A low-level 3D rendering device.
//
class ENGINE_API URenderDevice : public USubsystem
{
	DECLARE_ABSTRACT_CLASS(URenderDevice,USubsystem,CLASS_Config,Engine)

	// Variables.
	BYTE			DecompFormat;
	INT				RecommendedLOD;
	DWORD			TerrainLOD;
	BITFIELD		HighDetailActors;
	BITFIELD		SuperHighDetailActors;
	BITFIELD		DetailTextures;
	BITFIELD		PrecacheOnFlip;
	BITFIELD		SupportsCubemaps;
	BITFIELD		SupportsZBIAS;
	BITFIELD		UseCompressedLightmaps;
	BITFIELD		UseStencil;
	BITFIELD		Use16bit;
	BITFIELD		Use16bitTextures;
	BITFIELD		IsVoodoo3;
	BITFIELD		Is3dfx;
	BITFIELD		LowQualityTerrain;
	BITFIELD		SkyboxHack;
	BITFIELD		Pad1[8];
	DWORD			Pad0[8];
	FLOAT           FSAAScale[2];

	// Constructors.
	void StaticConstructor();

	// URenderDevice low-level functions that drivers must implement.
	virtual UBOOL Init()=0;
	virtual UBOOL SetRes( UViewport* Viewport, INT NewX, INT NewY, UBOOL Fullscreen, INT ColorBytes=0,UBOOL bSaveSize=true)=0;
	virtual void Exit( UViewport* Viewport )=0;

	virtual void Flush( UViewport* Viewport )=0;
	//scion jg -- Changes for faster caching scheme
	virtual void FlushResource( _WORD CacheId, ECacheHint CacheHint ) = 0;
	virtual UBOOL ResourceCached( _WORD CacheId, ECacheHint CacheHint ) { return 0; }
	//scion jg -- For resetting render states without flushing
	virtual void ResetRenderStates(void) {}

	virtual void UpdateGamma( UViewport* Viewport )=0;
	virtual void RestoreGamma()=0;

	virtual UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar ) = 0;

	virtual FRenderInterface* Lock( UViewport* Viewport, BYTE* HitData, INT* HitSize )=0;
	virtual void Unlock( FRenderInterface* RI ) = 0;
	virtual void Present( UViewport* Viewport ) = 0;

	virtual void ReadPixels( UViewport* Viewport, FColor* Pixels )=0;
	virtual void SetEmulationMode(EHardwareEmulationMode Mode) = 0;
	virtual FRenderCaps* GetRenderCaps() = 0;

	// scion sz -
	// load a vertex shader configuration and store it in this render device
	virtual void LoadVertexShaderEnum( QWORD VertexShaderEnum ) = 0;
};

#endif
/*------------------------------------------------------------------------------------
	The End.
------------------------------------------------------------------------------------*/

