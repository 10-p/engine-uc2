/*=============================================================================
	UC2GLFFProgram.cpp: the texture stages of fixed-function materials as GLSL.

	Epic's material compiler (UC2GLMaterialState.cpp) describes every fixed-function pass
	as Direct3D 8 texture stages: per stage a colour and an alpha operation (D3DTOP_*) on
	up to three arguments (D3DTA_*: CURRENT, TEXTURE, DIFFUSE, TFACTOR, with COMPLEMENT and
	ALPHAREPLICATE). GL's GL_COMBINE texture environment can take each stage
	(FOpenGLRenderInterface::SetMaterialBlending / TranslateStageOp, the fallback), but that
	needs ATI_texture_env_combine3 (or NV_texture_env_combine4) for the three-operand
	operations and as many fixed-function texture units as stages, and some drivers (NVIDIA's)
	have neither combine3 nor more than 4 such units.

	Here a pass is turned into a small fragment shader instead, one per combination of
	operations, arguments and texture targets (cached), linked WITHOUT a vertex shader: the
	fixed-function vertex stage (transform, lighting, texgen, texture matrices, fog
	coordinate) stays as it is, and the shader does what GL's fixed-function fragment
	stage does after the combiners: the colour sum (secondary colour) and linear fog (on the
	fragment's eye-space |z|, recovered from gl_FragCoord.z and the projection; gl_FogFragCoord
	differs between drivers). Alpha test, blending, depth and stencil are per-fragment
	operations after the shader. The operations are Direct3D 8's definitions (d3d8types.h / the DirectX 8 SDK's
	"Texture Blending" pages), every stage result saturated to [0,1] as D3D does:

	  SELECTARG1 a1   SELECTARG2 a2   MODULATE a1*a2   MODULATE2X 2*a1*a2   MODULATE4X 4*a1*a2
	  ADD a1+a2   ADDSIGNED a1+a2-.5   ADDSIGNED2X 2*(a1+a2-.5)   SUBTRACT a1-a2
	  ADDSMOOTH a1+a2-a1*a2   BLEND{DIFFUSE,TEXTURE,FACTOR,CURRENT}ALPHA a1*A+a2*(1-A)
	  BLENDTEXTUREALPHAPM a1+a2*(1-Atex)   MODULATEALPHA_ADDCOLOR a1.rgb+a1.a*a2.rgb
	  MODULATECOLOR_ADDALPHA a1.rgb*a2.rgb+a1.a   MODULATEINVALPHA_ADDCOLOR (1-a1.a)*a2.rgb+a1.rgb
	  MODULATEINVCOLOR_ADDALPHA (1-a1.rgb)*a2.rgb+a1.a   DOTPRODUCT3 4*dot(a1-.5,a2-.5) into all
	  four channels   MULTIPLYADD a0+a1*a2   LERP a0*a1+(1-a0)*a2   DISABLE: no further stage

	Texture lookups are those of the fixed-function stage: 2D textures at (s/q, t/q)
	(texture2DProj), cube maps at (s,t,r). Stage 0's CURRENT is the diffuse colour, as
	D3D's. The texture-unit LOD bias (glTexEnv TEXTURE_FILTER_CONTROL, set per stage by
	SetMaterialBlending) applies to these lookups as it does to the combiners; units the
	fixed-function stage does not have (NVIDIA: 4 and up) get the stage's bias in the lookup.

	A stage that samples a BSP lightmap atlas reads it through a cubic B-spline of four
	bilinear reads (UseBicubicLightmaps; UC2GL_BicubicLookup) where the device has the filter on.

	Desktop GL: every fixed-function pass. Switch: [UC2GLDrv.UC2GLRenderDevice]
	FixedFunctionPrograms (default True), console FFPROGRAMS [0|1].
	GL ES (GL4ES): gl4es generates a shader of the same kind from the fixed-function state,
	combine3 and combine4 included, with 8 units, and draws every pass that way, except a pass
	with a lightmap stage while the bicubic filter is on: that pass takes its program here, with a
	vertex stage of its own (FFBuildVertexSource) that computes what gl4es' vertex stage computes
	for the pass; the position is the same product as in gl4es' shaders, so later passes keep
	their depth test, and a reflection is computed per fragment, as gl4es does. Without a
	texture-unit LOD bias on GL ES every sampled stage takes its bias in the lookup.
=============================================================================*/

#include "UC2GLDrv.h"

// What a pass's program depends on: per stage the two operations, their arguments (only
// those the operation reads; the rest zeroed so equal passes share a program), the
// texture target and whether it is a lightmap read through the bicubic filter; the colour
// sum and fog; which stages take their LOD bias in the lookup.
struct FUC2GLFFKey
{
	DWORD	Words[MAX_STAGES * 3 + 1];
};

struct FUC2GLFFProgram
{
	FUC2GLFFKey	Key;
	DWORD		Hash;
	GLuint		Program;
	GLint		TFactorLoc,
				BiasLoc[MAX_STAGES],
				TexSizeLoc[MAX_STAGES];
	UBOOL		Reported;
};

//
// The bicubic lightmap read of stage s: uc2_bicubic<s>( vec4 tc ) in place of texture2DProj( u_tex<s>, tc ),
// for a source that declares the sampler u_tex<s>; it declares u_texSize<s> (width, height, 1/width,
// 1/height). The cubic B-spline (GPU Gems 2, ch. 20): the 4x4 texel footprint as four bilinear reads, each
// placed between two texel centres by the weights; the reads are clamped to the texture's outer texel
// centres. The weights are never negative, so the result stays within the texels' range. Every read is of
// the first level (bias -16): the reads' own level would jump where their positions jump, at texel
// boundaries, and the B-spline is a low-pass filter of its own. It ends with an empty line: gl4es inserts
// declarations of its own (alpha test) above the line before main(), which must not be inside a function.
//
FString UC2GL_BicubicLookup( INT s )
{
	return FString::Printf(
		TEXT("uniform vec4 u_texSize%i;\n")
		TEXT("vec4 uc2_bicubic%i( vec4 tc )\n")
		TEXT("{\n")
		TEXT("\tvec2 st = tc.xy / tc.w * u_texSize%i.xy - 0.5;\n")
		TEXT("\tvec2 i = floor( st );\n")
		TEXT("\tvec2 f = st - i;\n")
		TEXT("\tvec2 w0 = (1.0 - f) * (1.0 - f) * (1.0 - f) / 6.0;\n")
		TEXT("\tvec2 w1 = (4.0 - 6.0 * f * f + 3.0 * f * f * f) / 6.0;\n")
		TEXT("\tvec2 w3 = f * f * f / 6.0;\n")
		TEXT("\tvec2 g0 = w0 + w1;\n")
		TEXT("\tvec2 g1 = 1.0 - g0;\n")
		TEXT("\tvec2 p0 = clamp( i - 0.5 + w1 / g0, vec2( 0.5 ), u_texSize%i.xy - 0.5 ) * u_texSize%i.zw;\n")
		TEXT("\tvec2 p1 = clamp( i + 1.5 + w3 / g1, vec2( 0.5 ), u_texSize%i.xy - 0.5 ) * u_texSize%i.zw;\n")
		TEXT("\treturn g0.y * (g0.x * texture2D( u_tex%i, p0, -16.0 ) + g1.x * texture2D( u_tex%i, vec2( p1.x, p0.y ), -16.0 ))\n")
		TEXT("\t\t+ g1.y * (g0.x * texture2D( u_tex%i, vec2( p0.x, p1.y ), -16.0 ) + g1.x * texture2D( u_tex%i, p1, -16.0 ));\n")
		TEXT("}\n")
		TEXT("\n"),
		s, s, s, s, s, s, s, s, s, s, s );
}

// Whether the stage reads a lightmap atlas the bicubic filter takes (filter on).
static UBOOL FFStageBicubic( UUC2GLRenderDevice* RenDev, const FOpenGLMaterialStateStage& Stage )
{
	return RenDev->BicubicLightmapsActive && Stage.Texture && Stage.Texture->TextureID && Stage.Texture->IsLightmap && !Stage.Texture->IsCubemap;
}

static TArray<FUC2GLFFProgram>	GFFPrograms;

// Which of Arg0/Arg1/Arg2 an operation reads (bit 0 = Arg0, 1 = Arg1, 2 = Arg2), and
// whether it reads the stage's texture alpha by itself.
static DWORD FFOpArgs( D3DTEXTUREOP Op )
{
	switch( Op )
	{
	case D3DTOP_DISABLE:				return 0;
	case D3DTOP_SELECTARG1:				return 2;
	case D3DTOP_SELECTARG2:				return 4;
	case D3DTOP_MULTIPLYADD:
	case D3DTOP_LERP:					return 7;
	case D3DTOP_BUMPENVMAP:
	case D3DTOP_BUMPENVMAPLUMINANCE:
	case D3DTOP_PREMODULATE:			return 0;
	default:							return 6;
	}
}

static UBOOL FFStageSamples( const FOpenGLMaterialStateStage& Stage )
{
	if( Stage.ColorOp == D3DTOP_BLENDTEXTUREALPHA || Stage.ColorOp == D3DTOP_BLENDTEXTUREALPHAPM
	||	Stage.AlphaOp == D3DTOP_BLENDTEXTUREALPHA || Stage.AlphaOp == D3DTOP_BLENDTEXTUREALPHAPM )
		return 1;
	DWORD	CArgs[3] = { Stage.ColorArg0, Stage.ColorArg1, Stage.ColorArg2 },
			AArgs[3] = { Stage.AlphaArg0, Stage.AlphaArg1, Stage.AlphaArg2 };
	DWORD	CUsed = FFOpArgs( Stage.ColorOp ), AUsed = Stage.ColorOp == D3DTOP_DISABLE ? 0 : FFOpArgs( Stage.AlphaOp );
	for( INT i=0; i<3; i++ )
		if( ((CUsed >> i) & 1 && (CArgs[i] & D3DTA_SELECTMASK) == D3DTA_TEXTURE)
		||	((AUsed >> i) & 1 && (AArgs[i] & D3DTA_SELECTMASK) == D3DTA_TEXTURE) )
			return 1;
	return 0;
}

// The texture coordinates of a stage in a program with a vertex stage of its own.
enum EFFGeneration
{
	FFG_Vertices	= 0,	// a vertex set (D3DTSS_TCI_PASSTHRU)
	FFG_Position	= 1,	// the camera-space position
	FFG_Normal		= 2,	// the camera-space normal
	FFG_Reflection	= 3,	// the camera-space reflection vector, per fragment
};

static DWORD FFGeneration( const FOpenGLMaterialStateStage& Stage )
{
	switch( Stage.TexCoordIndex & 0xFFFF0000 )
	{
	case D3DTSS_TCI_CAMERASPACEPOSITION:			return FFG_Position;
	case D3DTSS_TCI_CAMERASPACENORMAL:				return FFG_Normal;
	case D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR:	return FFG_Reflection;
	default:										return FFG_Vertices;
	}
}

// OwnVertex: the program has a vertex stage of its own (GL ES), which needs each stage's coordinate generation.
static void FFMakeKey( UUC2GLRenderDevice* RenDev, FOpenGLMaterialState* MS, INT StagesUsed, UBOOL ColorSum, UBOOL Fog, UBOOL OwnVertex, FUC2GLFFKey& Key )
{
	appMemzero( &Key, sizeof(Key) );
	DWORD BiasMask = 0;
	for( INT s=0; s<StagesUsed; s++ )
	{
		FOpenGLMaterialStateStage& Stage = MS->Stages[s];
		DWORD	CUsed = FFOpArgs( Stage.ColorOp ), AUsed = Stage.ColorOp == D3DTOP_DISABLE ? 0 : FFOpArgs( Stage.AlphaOp );
		DWORD	Target = 0, Bicubic = 0, Generation = 0;
		if( FFStageSamples( Stage ) )
		{
			Target = (Stage.Texture && Stage.Texture->TextureID && Stage.Texture->IsCubemap) ? 2 : 1;
			Bicubic = FFStageBicubic( RenDev, Stage );
			Generation = OwnVertex ? FFGeneration( Stage ) : FFG_Vertices;
			if( !Bicubic && (s >= RenDev->GLFixedFunctionUnits || RenDev->IsGLES) )
				BiasMask |= 1 << s;
		}
		Key.Words[s*3+0] = (DWORD)Stage.ColorOp | ((DWORD)Stage.AlphaOp << 8) | (Target << 16) | (Bicubic << 18) | (Generation << 19);
		Key.Words[s*3+1] = ((CUsed & 1) ? (Stage.ColorArg0 & 0xff) : 0) | ((CUsed & 2) ? (Stage.ColorArg1 & 0xff) << 8 : 0) | ((CUsed & 4) ? (Stage.ColorArg2 & 0xff) << 16 : 0);
		Key.Words[s*3+2] = ((AUsed & 1) ? (Stage.AlphaArg0 & 0xff) : 0) | ((AUsed & 2) ? (Stage.AlphaArg1 & 0xff) << 8 : 0) | ((AUsed & 4) ? (Stage.AlphaArg2 & 0xff) << 16 : 0);
		if( Stage.ColorOp == D3DTOP_DISABLE )
			break;
	}
	Key.Words[MAX_STAGES*3] = (DWORD)StagesUsed | (ColorSum ? 0x10 : 0) | (Fog ? 0x20 : 0) | (OwnVertex ? 0x40 : 0) | (BiasMask << 8);
}

static DWORD FFHash( const FUC2GLFFKey& Key )
{
	DWORD H = 2166136261u;
	for( INT i=0; i<ARRAY_COUNT(Key.Words); i++ )
	{
		H ^= Key.Words[i];
		H *= 16777619u;
		H ^= H >> 13;
	}
	return H;
}

// One argument as a vec4 expression, its modifiers applied (D3D applies them to the whole
// argument: an operation that reads an argument's alpha reads the modified alpha).
static FString FFArg( DWORD Arg, INT Stage )
{
	FString Base;
	switch( Arg & D3DTA_SELECTMASK )
	{
	case D3DTA_DIFFUSE:		Base = TEXT("gl_Color");						break;
	case D3DTA_CURRENT:		Base = TEXT("cur");								break;
	case D3DTA_TEXTURE:		Base = FString::Printf( TEXT("t%i"), Stage );	break;
	case D3DTA_TFACTOR:		Base = TEXT("u_tfactor");						break;
	case D3DTA_SPECULAR:	Base = TEXT("gl_SecondaryColor");				break;
	default:				Base = TEXT("vec4(0.0)");						break;	// D3DTA_TEMP: never set (no D3DPMISCCAPS_TSSARGTEMP)
	}
	if( Arg & D3DTA_ALPHAREPLICATE )
		Base = FString::Printf( TEXT("%s.aaaa"), *Base );
	if( Arg & D3DTA_COMPLEMENT )
		Base = FString::Printf( TEXT("(1.0 - %s)"), *Base );
	return Base;
}

// The result of one operation; C = ".rgb" for the colour half, ".a" for the alpha half.
static FString FFOp( D3DTEXTUREOP Op, const FString& A0, const FString& A1, const FString& A2, INT Stage, const TCHAR* C )
{
	FString a0 = A0 + C, a1 = A1 + C, a2 = A2 + C;
	switch( Op )
	{
	case D3DTOP_SELECTARG1:					return a1;
	case D3DTOP_SELECTARG2:					return a2;
	case D3DTOP_MODULATE:					return FString::Printf( TEXT("%s * %s"), *a1, *a2 );
	case D3DTOP_MODULATE2X:					return FString::Printf( TEXT("2.0 * %s * %s"), *a1, *a2 );
	case D3DTOP_MODULATE4X:					return FString::Printf( TEXT("4.0 * %s * %s"), *a1, *a2 );
	case D3DTOP_ADD:						return FString::Printf( TEXT("%s + %s"), *a1, *a2 );
	case D3DTOP_ADDSIGNED:					return FString::Printf( TEXT("%s + %s - 0.5"), *a1, *a2 );
	case D3DTOP_ADDSIGNED2X:				return FString::Printf( TEXT("2.0 * (%s + %s - 0.5)"), *a1, *a2 );
	case D3DTOP_SUBTRACT:					return FString::Printf( TEXT("%s - %s"), *a1, *a2 );
	case D3DTOP_ADDSMOOTH:					return FString::Printf( TEXT("%s + %s - %s * %s"), *a1, *a2, *a1, *a2 );
	case D3DTOP_BLENDDIFFUSEALPHA:			return FString::Printf( TEXT("mix( %s, %s, gl_Color.a )"), *a2, *a1 );
	case D3DTOP_BLENDTEXTUREALPHA:			return FString::Printf( TEXT("mix( %s, %s, t%i.a )"), *a2, *a1, Stage );
	case D3DTOP_BLENDFACTORALPHA:			return FString::Printf( TEXT("mix( %s, %s, u_tfactor.a )"), *a2, *a1 );
	case D3DTOP_BLENDCURRENTALPHA:			return FString::Printf( TEXT("mix( %s, %s, cur.a )"), *a2, *a1 );
	case D3DTOP_BLENDTEXTUREALPHAPM:		return FString::Printf( TEXT("%s + %s * (1.0 - t%i.a)"), *a1, *a2, Stage );
	case D3DTOP_MODULATEALPHA_ADDCOLOR:		return FString::Printf( TEXT("%s + %s.a * %s"), *a1, *A1, *a2 );
	case D3DTOP_MODULATECOLOR_ADDALPHA:		return FString::Printf( TEXT("%s * %s + %s.a"), *a1, *a2, *A1 );
	case D3DTOP_MODULATEINVALPHA_ADDCOLOR:	return FString::Printf( TEXT("(1.0 - %s.a) * %s + %s"), *A1, *a2, *a1 );
	case D3DTOP_MODULATEINVCOLOR_ADDALPHA:	return FString::Printf( TEXT("(1.0 - %s) * %s + %s.a"), *a1, *a2, *A1 );
	case D3DTOP_DOTPRODUCT3:				return FString::Printf( TEXT("4.0 * dot( %s.rgb - 0.5, %s.rgb - 0.5 )"), *A1, *A2 );
	case D3DTOP_MULTIPLYADD:				return FString::Printf( TEXT("%s + %s * %s"), *a0, *a1, *a2 );
	case D3DTOP_LERP:						return FString::Printf( TEXT("mix( %s, %s, %s )"), *a2, *a1, *a0 );
	default:								return FString::Printf( TEXT("cur%s"), C );	// BUMPENVMAP*, PREMODULATE: not reported by the device; what the GL path did
	}
}

// Whether a sampled stage of the key takes the reflection (per fragment).
static UBOOL FFHasReflection( const FUC2GLFFKey& Key )
{
	for( INT s=0; s<(INT)(Key.Words[MAX_STAGES*3] & 0xf) && (Key.Words[s*3] & 0xff) != D3DTOP_DISABLE; s++ )
		if( ((Key.Words[s*3] >> 16) & 3) && ((Key.Words[s*3] >> 19) & 3) == FFG_Reflection )
			return 1;
	return 0;
}

static FString FFBuildSource( const FUC2GLFFKey& Key )
{
	INT		StagesUsed	= Key.Words[MAX_STAGES*3] & 0xf;
	UBOOL	ColorSum	= (Key.Words[MAX_STAGES*3] & 0x10) != 0,
			Fog			= (Key.Words[MAX_STAGES*3] & 0x20) != 0;
	DWORD	BiasMask	= Key.Words[MAX_STAGES*3] >> 8;
	FString	Decl, Body;

	Decl += TEXT("// UC2GLDrv: D3D texture stages (UC2GLFFProgram.cpp)\nuniform vec4 u_tfactor;\n");
	if( FFHasReflection( Key ) )
		Decl += TEXT("varying vec3 v_reflEye;\nvarying vec3 v_reflNormal;\n");
	Body += TEXT("void main()\n{\n\tvec4 cur = gl_Color;\n");
	for( INT s=0; s<StagesUsed; s++ )
	{
		D3DTEXTUREOP	ColorOp	= (D3DTEXTUREOP)(Key.Words[s*3] & 0xff),
						AlphaOp	= (D3DTEXTUREOP)((Key.Words[s*3] >> 8) & 0xff);
		DWORD			Target	= (Key.Words[s*3] >> 16) & 3,
						Bicubic	= (Key.Words[s*3] >> 18) & 1;
		UBOOL			Reflect	= ((Key.Words[s*3] >> 19) & 3) == FFG_Reflection;
		if( ColorOp == D3DTOP_DISABLE )
			break;
		if( Target )
		{
			Decl += FString::Printf( TEXT("uniform %s u_tex%i;\n"), Target == 2 ? TEXT("samplerCube") : TEXT("sampler2D"), s );
			// One uniform per stage, not an array: drivers keep an array only up to its last element read, and
			// gl4es refuses an upload longer than that.
			if( BiasMask & (1 << s) )
				Decl += FString::Printf( TEXT("uniform float u_bias%i;\n"), s );
			FString Bias = (BiasMask & (1 << s)) ? FString::Printf( TEXT(", u_bias%i"), s ) : FString();
			if( Bicubic )
			{
				Decl += UC2GL_BicubicLookup( s );
				Body += FString::Printf( TEXT("\tvec4 t%i = uc2_bicubic%i( gl_TexCoord[%i] );\n"), s, s, s );
			}
			else if( Reflect )
			{
				// The reflection per fragment, then the stage's texture matrix, as gl4es' own shaders
				// reflect (a 2D map is read without a divide: a reflection is a direction).
				Body += FString::Printf( TEXT("\tvec4 r%i = gl_TextureMatrix[%i] * vec4( reflect( normalize( v_reflEye ), normalize( v_reflNormal ) ), 1.0 );\n"), s, s );
				if( Target == 2 )
					Body += FString::Printf( TEXT("\tvec4 t%i = textureCube( u_tex%i, r%i.stp%s );\n"), s, s, s, *Bias );
				else
					Body += FString::Printf( TEXT("\tvec4 t%i = texture2D( u_tex%i, r%i.st%s );\n"), s, s, s, *Bias );
			}
			else if( Target == 2 )
				Body += FString::Printf( TEXT("\tvec4 t%i = textureCube( u_tex%i, gl_TexCoord[%i].xyz%s );\n"), s, s, s, *Bias );
			else
				Body += FString::Printf( TEXT("\tvec4 t%i = texture2DProj( u_tex%i, gl_TexCoord[%i]%s );\n"), s, s, s, *Bias );
		}
		DWORD	CW = Key.Words[s*3+1], AW = Key.Words[s*3+2];
		FString	C = FFOp( ColorOp, FFArg( CW & 0xff, s ), FFArg( (CW >> 8) & 0xff, s ), FFArg( (CW >> 16) & 0xff, s ), s, TEXT(".rgb") );
		Body += FString::Printf( TEXT("\t// stage %i: colour op %i, alpha op %i\n"), s, (INT)ColorOp, (INT)AlphaOp );
		if( ColorOp == D3DTOP_DOTPRODUCT3 )
		{
			// D3D replicates the dot product into all four channels, alpha included.
			Body += FString::Printf( TEXT("\tcur = vec4( clamp( %s, 0.0, 1.0 ) );\n"), *C );
			continue;
		}
		FString A = AlphaOp == D3DTOP_DISABLE ? FString( TEXT("cur.a") ) : FFOp( AlphaOp, FFArg( AW & 0xff, s ), FFArg( (AW >> 8) & 0xff, s ), FFArg( (AW >> 16) & 0xff, s ), s, TEXT(".a") );
		Body += FString::Printf( TEXT("\tcur = clamp( vec4( %s, %s ), 0.0, 1.0 );\n"), *C, *A );
	}
	if( ColorSum )
		Body += TEXT("\tcur.rgb = clamp( cur.rgb + gl_SecondaryColor.rgb, 0.0, 1.0 );\n");
	if( Fog )
	{
		// The fog distance is the fragment's own eye-space depth |z|, recovered from its window
		// depth through the projection (clip z = A z + B, clip w = C z + D; perspective and
		// orthographic alike): what D3D's table fog uses (D3DRS_FOGTABLEMODE, per pixel, depth;
		// Epic's Fog.vsh: -camera-space z) and what GL4ES computes on the web. Not
		// gl_FogFragCoord: Mesa's fixed-function vertex stage hands a larger value for sky-zone draws
		// (DM-Azure's) than NVIDIA's and GL4ES'.
		Body += TEXT("\tfloat ndc = (2.0 * gl_FragCoord.z - gl_DepthRange.near - gl_DepthRange.far) / gl_DepthRange.diff;\n")
				TEXT("\tfloat eyeZ = (gl_ProjectionMatrix[3][2] - ndc * gl_ProjectionMatrix[3][3]) / (ndc * gl_ProjectionMatrix[2][3] - gl_ProjectionMatrix[2][2]);\n")
				TEXT("\tcur.rgb = mix( gl_Fog.color.rgb, cur.rgb, clamp( (gl_Fog.end - abs( eyeZ )) * gl_Fog.scale, 0.0, 1.0 ) );\n");
	}
	Body += TEXT("\tgl_FragColor = cur;\n}\n");
	return Decl + Body;
}

//
// The vertex stage of a program with OwnVertex: what gl4es' own vertex stage computes for the pass (unlit; the
// normals normalised, as GL_NORMALIZE asks; the texture generation's eye planes are the identity, and a coordinate
// it does not generate is the vertex's), each stage's coordinates through its texture matrix; for a reflection the
// eye-space position and normal. ftransform() is the product gl4es' own shaders take, so the depth is theirs. The
// empty line before main(): gl4es inserts declarations of its own (clip planes) above the line before it.
//
static FString FFBuildVertexSource( const FUC2GLFFKey& Key )
{
	INT		StagesUsed	= Key.Words[MAX_STAGES*3] & 0xf;
	UBOOL	ColorSum	= (Key.Words[MAX_STAGES*3] & 0x10) != 0,
			Reflection	= FFHasReflection( Key );
	FString	S = TEXT("// UC2GLDrv: the vertex stage of a fixed-function pass (UC2GLFFProgram.cpp)\n");
	if( Reflection )
		S += TEXT("varying vec3 v_reflEye;\nvarying vec3 v_reflNormal;\n");
	S +=	TEXT("\n")
			TEXT("void main()\n{\n")
			TEXT("\tgl_Position = ftransform();\n")
			TEXT("\tgl_FrontColor = gl_Color;\n")
			TEXT("\tvec4 eye = gl_ModelViewMatrix * gl_Vertex;\n")
			TEXT("\tvec3 normal = normalize( gl_NormalMatrix * gl_Normal );\n");
	if( ColorSum )
		S += TEXT("\tgl_FrontSecondaryColor = gl_SecondaryColor;\n");
	if( Reflection )
		S += TEXT("\tv_reflEye = eye.xyz;\n\tv_reflNormal = normal;\n");
	for( INT s=0; s<StagesUsed; s++ )
	{
		DWORD Word = Key.Words[s*3];
		if( (Word & 0xff) == D3DTOP_DISABLE )
			break;
		if( !((Word >> 16) & 3) )
			continue;
		switch( (Word >> 19) & 3 )
		{
		case FFG_Vertices:	S += FString::Printf( TEXT("\tgl_TexCoord[%i] = gl_TextureMatrix[%i] * gl_MultiTexCoord%i;\n"), s, s, s );						break;
		case FFG_Position:	S += FString::Printf( TEXT("\tgl_TexCoord[%i] = gl_TextureMatrix[%i] * vec4( eye.xyz, gl_MultiTexCoord%i.q );\n"), s, s, s );	break;
		case FFG_Normal:	S += FString::Printf( TEXT("\tgl_TexCoord[%i] = gl_TextureMatrix[%i] * vec4( normal, gl_MultiTexCoord%i.q );\n"), s, s, s );	break;
		default:			break;	// FFG_Reflection: per fragment
		}
	}
	S += TEXT("}\n");
	return S;
}

static FUC2GLFFProgram* FFGetProgram( UUC2GLRenderDevice* RenDev, const FUC2GLFFKey& Key )
{
	DWORD Hash = FFHash( Key );
	for( INT i=0; i<GFFPrograms.Num(); i++ )
		if( GFFPrograms(i).Hash == Hash && appMemcmp( &GFFPrograms(i).Key, &Key, sizeof(Key) ) == 0 )
			return &GFFPrograms(i);

	FUC2GLFFProgram& P = GFFPrograms( GFFPrograms.AddZeroed() );
	P.Key	= Key;
	P.Hash	= Hash;
	FString Source = FFBuildSource( Key );
	const GLchar* Sources[2] = { RenDev->ShaderPrologue( 1 ), TCHAR_TO_ANSI(*Source) };
	ANSICHAR Text[8192];
	appStrncpy( Text, Sources[1], ARRAY_COUNT(Text) );
	Sources[1] = Text;
	GLuint Shader = RenDev->glCreateShader( GL_FRAGMENT_SHADER );
	RenDev->glShaderSource( Shader, 2, Sources, NULL );
	RenDev->glCompileShader( Shader );
	GLint Status = 0;
	RenDev->glGetShaderiv( Shader, GL_COMPILE_STATUS, &Status );
	// Without OwnVertex (desktop GL) the program has no vertex shader: the fixed-function vertex stage feeds it.
	GLuint VertexShader = 0;
	if( Status && (Key.Words[MAX_STAGES*3] & 0x40) )
	{
		FString VertexSource = FFBuildVertexSource( Key );
		Source += TEXT("// vertex stage:\n") + VertexSource;
		ANSICHAR VertexText[4096];
		appStrncpy( VertexText, TCHAR_TO_ANSI(*VertexSource), ARRAY_COUNT(VertexText) );
		const GLchar* VertexSources[2] = { RenDev->ShaderPrologue( 0 ), VertexText };
		VertexShader = RenDev->glCreateShader( GL_VERTEX_SHADER );
		RenDev->glShaderSource( VertexShader, 2, VertexSources, NULL );
		RenDev->glCompileShader( VertexShader );
		RenDev->glGetShaderiv( VertexShader, GL_COMPILE_STATUS, &Status );
		if( !Status )
		{
			RenDev->glDeleteShader( Shader );
			Shader = VertexShader;
			VertexShader = 0;
		}
	}
	if( Status )
	{
		P.Program = RenDev->glCreateProgram();
		RenDev->glAttachShader( P.Program, Shader );
		if( VertexShader )
			RenDev->glAttachShader( P.Program, VertexShader );
		RenDev->glLinkProgram( P.Program );
		RenDev->glGetProgramiv( P.Program, GL_LINK_STATUS, &Status );
	}
	if( !Status )
	{
		ANSICHAR Log[1024] = "";
		if( P.Program )
			RenDev->glGetProgramInfoLog( P.Program, sizeof(Log)-1, NULL, Log );
		else
			RenDev->glGetShaderInfoLog( Shader, sizeof(Log)-1, NULL, Log );
		debugf( NAME_Warning, TEXT("UC2GLDrv: fixed-function program %i does not build (%s); its passes use the texture environment. Source:\n%s"), GFFPrograms.Num(), appFromAnsi(Log), *Source );
		if( P.Program )
			RenDev->glDeleteProgram( P.Program );
		P.Program = 0;
		RenDev->FFProgramsFailed++;
	}
	RenDev->glDeleteShader( Shader );
	if( VertexShader )
		RenDev->glDeleteShader( VertexShader );
	if( P.Program )
	{
		RenDev->FFProgramsLinked++;
		P.TFactorLoc	= RenDev->glGetUniformLocation( P.Program, "u_tfactor" );
		RenDev->glUseProgram( P.Program );
		for( INT s=0; s<MAX_STAGES; s++ )
		{
			ANSICHAR Name[16];
			appSprintf( Name, "u_tex%i", s );
			GLint Loc = RenDev->glGetUniformLocation( P.Program, Name );
			if( Loc >= 0 )
				RenDev->glUniform1i( Loc, s );
			appSprintf( Name, "u_bias%i", s );
			P.BiasLoc[s] = RenDev->glGetUniformLocation( P.Program, Name );
			appSprintf( Name, "u_texSize%i", s );
			P.TexSizeLoc[s] = RenDev->glGetUniformLocation( P.Program, Name );
		}
		RenDev->glUseProgram( 0 );
		if( RenDev->LogFFPrograms )
			debugf( NAME_Init, TEXT("UC2GLDrv: fixed-function program %i (hash %08x):\n%s"), GFFPrograms.Num(), Hash, *Source );
	}
	return &P;
}

//
// The program of a fixed-function pass, bound for the next draw; 0 if the pass keeps the
// texture environment (switch off, or the program does not build).
//
UBOOL FOpenGLRenderInterface::BeginFixedFunctionProgram( FOpenGLMaterialState* MaterialState, UBOOL ColorSum, UBOOL Fog )
{
	if( MaterialState->UsePixelShaderProgram )
		return 0;
	INT StagesUsed = Min<INT>( MaterialState->StagesUsed, RenDev->PassStages );
	UBOOL Lightmap = 0;
	for( INT s=0; s<StagesUsed && MaterialState->Stages[s].ColorOp != D3DTOP_DISABLE; s++ )
		if( FFStageSamples( MaterialState->Stages[s] ) )
			Lightmap |= FFStageBicubic( RenDev, MaterialState->Stages[s] );
	// GL ES: only an unlit pass with a lightmap for the filter (the lightmapped BSP), with a vertex stage of its
	// own (FFBuildVertexSource); every other pass keeps gl4es' shader.
	UBOOL OwnVertex = RenDev->IsGLES;
	UBOOL Program = RenDev->FFProgramsActive || (RenDev->IsGLES && Lightmap && !CurrentState->UseDynamicLighting);
	if( !Program )
	{
		if( Lightmap )
			RenDev->BilinearLightmapDraws++;
		return 0;
	}
	FUC2GLFFKey Key;
	FFMakeKey( RenDev, MaterialState, StagesUsed, ColorSum, Fog, OwnVertex, Key );
	FUC2GLFFProgram* P = FFGetProgram( RenDev, Key );
	if( !P->Program )
	{
		RenDev->FFFallbackDraws++;
		if( Lightmap )
			RenDev->BilinearLightmapDraws++;
		return 0;
	}
	if( Lightmap )
		RenDev->BicubicLightmapDraws++;
	RenDev->glUseProgram( P->Program );
	FPlane TFactor = FColor(MaterialState->TFactorColor).Plane();
	if( P->TFactorLoc >= 0 )
		RenDev->glUniform4f( P->TFactorLoc, TFactor.X, TFactor.Y, TFactor.Z, TFactor.W );
	for( INT s=0; s<StagesUsed; s++ )
	{
		if( P->BiasLoc[s] >= 0 )
			RenDev->glUniform1f( P->BiasLoc[s], RenDev->NoLODBias ? 0.f : MaterialState->Stages[s].TextureMipLODBias );
		if( P->TexSizeLoc[s] >= 0 )
		{
			FOpenGLTexture* Texture = MaterialState->Stages[s].Texture;
			FLOAT W = Max( Texture->CachedWidth, 1 ), H = Max( Texture->CachedHeight, 1 );
			RenDev->glUniform4f( P->TexSizeLoc[s], W, H, 1.f / W, 1.f / H );
		}
	}
	RenDev->FFProgramDraws++;
	return 1;
}

void FOpenGLRenderInterface::EndFixedFunctionProgram()
{
	RenDev->glUseProgram( 0 );
}

void UC2GL_FlushFixedFunctionPrograms( UUC2GLRenderDevice* RenDev )
{
	if( RenDev->ContextAlive() )
		for( INT i=0; i<GFFPrograms.Num(); i++ )
			if( GFFPrograms(i).Program )
				RenDev->glDeleteProgram( GFFPrograms(i).Program );
	GFFPrograms.Empty();
}

INT UC2GL_NumFixedFunctionPrograms()
{
	return GFFPrograms.Num();
}

void UC2GL_LogFFStats( UUC2GLRenderDevice* RenDev )
{
	if( RenDev->BicubicLightmapDraws || RenDev->BilinearLightmapDraws )
		debugf( NAME_Init, TEXT("UC2GLDrv: draws with a lightmap stage while the bicubic filter was on: %i filtered, %i bilinear (texture environment)"),
			RenDev->BicubicLightmapDraws, RenDev->BilinearLightmapDraws );
	if( !RenDev->FFProgramsActive && !RenDev->FFProgramDraws )
		return;
	debugf( NAME_Init, TEXT("UC2GLDrv: fixed-function programs: %i linked, %i failed; %i program draws, %i draws kept the texture environment (program missing)"),
		RenDev->FFProgramsLinked, RenDev->FFProgramsFailed, RenDev->FFProgramDraws, RenDev->FFFallbackDraws );
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
