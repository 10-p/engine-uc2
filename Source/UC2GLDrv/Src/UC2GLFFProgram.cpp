/*=============================================================================
	UC2GLFFProgram.cpp: the texture stages of fixed-function materials as GLSL (O-9).
	Not Epic's.

	Epic's material compiler (UC2GLMaterialState.cpp) describes every fixed-function pass
	as Direct3D 8 texture stages: per stage a colour and an alpha operation (D3DTOP_*) on
	up to three arguments (D3DTA_*: CURRENT, TEXTURE, DIFFUSE, TFACTOR, with COMPLEMENT and
	ALPHAREPLICATE). M5 translated each stage into GL's GL_COMBINE texture environment
	(FOpenGLRenderInterface::SetMaterialBlending / TranslateStageOp). That needs
	ATI_texture_env_combine3 (or NV_texture_env_combine4) for the three-operand operations
	and as many fixed-function texture units as stages, and the owner's NVIDIA GPU has
	neither combine3 nor more than 4 such units (docs/porting/analysis/o9-nvidia.md).

	Here a pass is turned into a small fragment shader instead, one per combination of
	operations, arguments and texture targets (cached), linked WITHOUT a vertex shader: the
	fixed-function vertex stage (transform, lighting, texgen, texture matrices, fog
	coordinate) stays what it was, and the shader does what GL's fixed-function fragment
	stage did after the combiners: the colour sum (secondary colour) and linear fog (on the
	fragment's eye-space |z|, recovered from gl_FragCoord.z and the projection since F-5; before
	that gl_FogFragCoord, which NVIDIA hands as the signed eye-space z, and Mesa as a value that
	fogs DM-Azure's sky zone far more than NVIDIA and GL4ES do). Alpha
	test, blending, depth and stencil are per-fragment operations after the shader, as
	before. The operations are Direct3D 8's definitions (d3d8types.h / the DirectX 8 SDK's
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
	SetMaterialBlending) applies to these lookups as it did to the combiners; units the
	fixed-function stage does not have (NVIDIA: 4 and up) get the stage's bias in the lookup.

	Desktop GL only: under GL4ES (WebGL) gl4es already generates a shader of the same kind
	from the fixed-function state, combine3 and combine4 included, with 8 units.
	Switch: [UC2GLDrv.UC2GLRenderDevice] FixedFunctionPrograms (default True),
	ini FixedFunctionPrograms, console FFPROGRAMS [0|1].
=============================================================================*/

#include "UC2GLDrv.h"

// What a pass's program depends on: per stage the two operations, their arguments (only
// those the operation reads; the rest zeroed so equal passes share a program) and the
// texture target; the colour sum and fog; which stages take their LOD bias in the lookup.
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
				BiasLoc;
	UBOOL		Reported;
};

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

static void FFMakeKey( UUC2GLRenderDevice* RenDev, FOpenGLMaterialState* MS, INT StagesUsed, UBOOL ColorSum, UBOOL Fog, FUC2GLFFKey& Key )
{
	appMemzero( &Key, sizeof(Key) );
	DWORD BiasMask = 0;
	for( INT s=0; s<StagesUsed; s++ )
	{
		FOpenGLMaterialStateStage& Stage = MS->Stages[s];
		DWORD	CUsed = FFOpArgs( Stage.ColorOp ), AUsed = Stage.ColorOp == D3DTOP_DISABLE ? 0 : FFOpArgs( Stage.AlphaOp );
		DWORD	Target = 0;
		if( FFStageSamples( Stage ) )
		{
			Target = (Stage.Texture && Stage.Texture->TextureID && Stage.Texture->IsCubemap) ? 2 : 1;
			if( s >= RenDev->GLFixedFunctionUnits )
				BiasMask |= 1 << s;
		}
		Key.Words[s*3+0] = (DWORD)Stage.ColorOp | ((DWORD)Stage.AlphaOp << 8) | (Target << 16);
		Key.Words[s*3+1] = ((CUsed & 1) ? (Stage.ColorArg0 & 0xff) : 0) | ((CUsed & 2) ? (Stage.ColorArg1 & 0xff) << 8 : 0) | ((CUsed & 4) ? (Stage.ColorArg2 & 0xff) << 16 : 0);
		Key.Words[s*3+2] = ((AUsed & 1) ? (Stage.AlphaArg0 & 0xff) : 0) | ((AUsed & 2) ? (Stage.AlphaArg1 & 0xff) << 8 : 0) | ((AUsed & 4) ? (Stage.AlphaArg2 & 0xff) << 16 : 0);
		if( Stage.ColorOp == D3DTOP_DISABLE )
			break;
	}
	Key.Words[MAX_STAGES*3] = (DWORD)StagesUsed | (ColorSum ? 0x10 : 0) | (Fog ? 0x20 : 0) | (BiasMask << 8);
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

static FString FFBuildSource( const FUC2GLFFKey& Key )
{
	INT		StagesUsed	= Key.Words[MAX_STAGES*3] & 0xf;
	UBOOL	ColorSum	= (Key.Words[MAX_STAGES*3] & 0x10) != 0,
			Fog			= (Key.Words[MAX_STAGES*3] & 0x20) != 0;
	DWORD	BiasMask	= Key.Words[MAX_STAGES*3] >> 8;
	FString	Decl, Body;

	Decl += TEXT("// UC2GLDrv: D3D texture stages (UC2GLFFProgram.cpp)\nuniform vec4 u_tfactor;\n");
	if( BiasMask )
		Decl += FString::Printf( TEXT("uniform float u_bias[%i];\n"), MAX_STAGES );
	Body += TEXT("void main()\n{\n\tvec4 cur = gl_Color;\n");
	for( INT s=0; s<StagesUsed; s++ )
	{
		D3DTEXTUREOP	ColorOp	= (D3DTEXTUREOP)(Key.Words[s*3] & 0xff),
						AlphaOp	= (D3DTEXTUREOP)((Key.Words[s*3] >> 8) & 0xff);
		DWORD			Target	= (Key.Words[s*3] >> 16) & 3;
		if( ColorOp == D3DTOP_DISABLE )
			break;
		if( Target )
		{
			Decl += FString::Printf( TEXT("uniform %s u_tex%i;\n"), Target == 2 ? TEXT("samplerCube") : TEXT("sampler2D"), s );
			FString Bias = (BiasMask & (1 << s)) ? FString::Printf( TEXT(", u_bias[%i]"), s ) : FString();
			if( Target == 2 )
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
		// F-5: the fog distance is the fragment's own eye-space depth |z|, recovered from its window
		// depth through the projection (clip z = A z + B, clip w = C z + D; perspective and
		// orthographic alike): what D3D's table fog uses (D3DRS_FOGTABLEMODE, per pixel, depth;
		// Epic's Fog.vsh: -camera-space z) and what GL4ES computes on the web. Not
		// gl_FogFragCoord: Mesa's fixed-function vertex stage hands a larger value for DM-Azure's
		// sky-zone draws than NVIDIA's and GL4ES' (render-f5.md step 6: the hazy sky on Mesa).
		Body += TEXT("\tfloat ndc = (2.0 * gl_FragCoord.z - gl_DepthRange.near - gl_DepthRange.far) / gl_DepthRange.diff;\n")
				TEXT("\tfloat eyeZ = (gl_ProjectionMatrix[3][2] - ndc * gl_ProjectionMatrix[3][3]) / (ndc * gl_ProjectionMatrix[2][3] - gl_ProjectionMatrix[2][2]);\n")
				TEXT("\tcur.rgb = mix( gl_Fog.color.rgb, cur.rgb, clamp( (gl_Fog.end - abs( eyeZ )) * gl_Fog.scale, 0.0, 1.0 ) );\n");
	}
	Body += TEXT("\tgl_FragColor = cur;\n}\n");
	return Decl + Body;
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
	if( Status )
	{
		P.Program = RenDev->glCreateProgram();
		RenDev->glAttachShader( P.Program, Shader );
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
	if( P.Program )
	{
		RenDev->FFProgramsLinked++;
		P.TFactorLoc	= RenDev->glGetUniformLocation( P.Program, "u_tfactor" );
		P.BiasLoc		= RenDev->glGetUniformLocation( P.Program, "u_bias" );
		RenDev->glUseProgram( P.Program );
		for( INT s=0; s<MAX_STAGES; s++ )
		{
			ANSICHAR Name[16];
			appSprintf( Name, "u_tex%i", s );
			GLint Loc = RenDev->glGetUniformLocation( P.Program, Name );
			if( Loc >= 0 )
				RenDev->glUniform1i( Loc, s );
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
	if( !RenDev->FFProgramsActive || MaterialState->UsePixelShaderProgram )
		return 0;
	INT StagesUsed = Min<INT>( MaterialState->StagesUsed, RenDev->NumTextureUnits );
	FUC2GLFFKey Key;
	FFMakeKey( RenDev, MaterialState, StagesUsed, ColorSum, Fog, Key );
	FUC2GLFFProgram* P = FFGetProgram( RenDev, Key );
	if( !P->Program )
	{
		RenDev->FFFallbackDraws++;
		return 0;
	}
	RenDev->glUseProgram( P->Program );
	FPlane TFactor = FColor(MaterialState->TFactorColor).Plane();
	if( P->TFactorLoc >= 0 )
		RenDev->glUniform4f( P->TFactorLoc, TFactor.X, TFactor.Y, TFactor.Z, TFactor.W );
	if( P->BiasLoc >= 0 )
	{
		FLOAT Bias[MAX_STAGES];
		for( INT s=0; s<MAX_STAGES; s++ )
			Bias[s] = (s < StagesUsed && !RenDev->NoLODBias) ? MaterialState->Stages[s].TextureMipLODBias : 0.f;
		RenDev->glUniform1fv( P->BiasLoc, MAX_STAGES, Bias );
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
	if( !RenDev->FFProgramsActive && !RenDev->FFProgramDraws )
		return;
	debugf( NAME_Init, TEXT("UC2GLDrv: fixed-function programs: %i linked, %i failed; %i program draws, %i draws kept the texture environment (program missing)"),
		RenDev->FFProgramsLinked, RenDev->FFProgramsFailed, RenDev->FFProgramDraws, RenDev->FFFallbackDraws );
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
