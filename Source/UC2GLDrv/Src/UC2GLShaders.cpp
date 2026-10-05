/*=============================================================================
	UC2GLShaders.cpp: programmable materials (M6, renderer R2).

	The Xbox build draws everything through vertex shaders spliced from the fragments in
	Source/ShaderData/VertexShaders and, for pixel-shader materials, the register-combiner
	shaders in Source/ShaderData/PixelShaders. The PC build never got most of that. Here:

	- Fragment shaders: generated from Epic's .psh sources by tools/psh2glsl.py
	  (Shaders/*.glsl, embedded through Shaders/UC2GLPixelShaders.inc).
	- Vertex shaders: composed from GLSL snippets hand-ported from the .vsh fragments, in the
	  order FD3DCombinedVertexShader (D3DResource.cpp) splices them, keyed by Epic's 64-bit
	  VertexShaderEnum. The enum of a draw is derived from the render state exactly as the
	  Xbox FD3DDeferredState::Commit does (lighting, vertex colours, fog, per-stage texture
	  coordinate source and transform); the bits the Xbox engine code passes in and PC's does
	  not (static-mesh vertex colours) are read off the vertex streams.
	- Programs: one per (pixel shader, variant, vertex shader enum), linked on first use.

	Every snippet names the .vsh file it stands for. docs/porting/analysis/shaders.md has the
	design notes and the list of what waits for M7.
=============================================================================*/

#include "UC2GLDrv.h"

#include "UC2GLPixelShaders.inc"

// Vertex attribute locations (the roles of ShaderRegisters.vsh: POSITION, NORMAL, DIFFUSE,
// SPECULAR, TEXCOORD0-3).
enum
{
	VA_Position		= 0,
	VA_Normal		= 1,
	VA_Diffuse		= 2,
	VA_Specular		= 3,
	VA_TexCoord0	= 4,
	VA_MAX			= 8
};

// Fragment shader variants.
enum
{
	PSV_CubeMask		= 0x0f,	// stage n samples a cube map (PS_GLOBALFLAGS_TEXMODE_ADJUST)
	PSV_SpecularEnable	= 0x10,	// D3DRS_SPECULARENABLE for shaders without xfc
};


#define UC2GL_LOG_ONCE(...) { static UBOOL Logged = 0; if( !Logged ) { Logged = 1; debugf( NAME_Init, __VA_ARGS__ ); } }

/*-----------------------------------------------------------------------------
	Which shaders are drawn for real at this milestone.
-----------------------------------------------------------------------------*/

//
// These read a copy of the back buffer, the depth buffer or another render target, or belong
// to the post-process chain the engine runs only for a device with the XboxPostProcess
// capability. With UsePostProcess (M7) they are drawn like every other shader; without it
// they stay on the stage-0 fallback (SetPixelShaderMaterial), which is the M6 behaviour.
//
static UBOOL UC2GL_IsM7PixelShader( INT Type )
{
	switch( Type )
	{
	// M7: post-process chain (FPostProcessRender, UCCameraEffects, shadow blur): render targets as input.
	case PS_Blur:
	case PS_DepthOfField:
	case PS_Mask:
	case PS_MotionBlur:
	case PS_BloomSelect:
	case PS_EmissiveSelect:
	case PS_EmissiveSelectBlur:
	case PS_ShadowBlur:
	case PS_DepthSwizzleColor:
	// M7: the back-buffer copy as a texture.
	case PS_Refraction:
	case PS_LuminanceTexIndex:
	case PS_UseTexIndex:
	case PS_UseTexIndexMasked:
	case PS_MaskedLuminance:
	case PS_PlayerCam:
	case PS_PlayerCamNoise:
	case PS_PlayerCamTwoNoise:
	case PS_ColorRemap:
	case PS_ColorRemapMasked:
	case PS_ColorRemapGrayScale:
	// M7: distortion (back buffer through a dependent read, accumulation target).
	case PS_Distortion:
	case PS_ModulatedDistortion:
	case PS_AdditiveDistortion:
	case PS_SubtractiveDistortion:
	case PS_DistortionAccum:
		return 1;
	default:
		return 0;
	}
}

//
// Translated, but nothing in the PC code path asks for them: the engine batches projectors
// into UProjectorMultiMaterial only under _XBOX (UnRenderBSP.cpp, UnRenderStaticMesh.cpp,
// UnRenderBatch.cpp) and SetProjectorMultiMaterial is an _XBOX body; terrain asks for its
// pixel shader only when the caps report three simultaneous layers.
//
static UBOOL UC2GL_IsEngineGatedPixelShader( INT Type )
{
	return (Type >= PS_Projector_Add_1 && Type <= PS_Projector_Modulate_Clip) || Type == PS_Terrain3Layer;
}

/*-----------------------------------------------------------------------------
	Shader objects.
-----------------------------------------------------------------------------*/

static GLuint UC2GL_CompileShader( UUC2GLRenderDevice* RenDev, GLenum Type, const ANSICHAR* Header, const ANSICHAR* Body, const TCHAR* What )
{
	const GLchar* Sources[2] = { Header, Body };
	GLuint Shader = RenDev->glCreateShader( Type );
	RenDev->glShaderSource( Shader, 2, Sources, NULL );
	RenDev->glCompileShader( Shader );
	GLint Status = 0;
	RenDev->glGetShaderiv( Shader, GL_COMPILE_STATUS, &Status );
	if( !Status )
	{
		ANSICHAR Log[1024] = "";
		RenDev->glGetShaderInfoLog( Shader, sizeof(Log)-1, NULL, Log );
		debugf( NAME_Warning, TEXT("UC2GLDrv: %s does not compile: %s"), What, Log );
		RenDev->glDeleteShader( Shader );
		return 0;
	}
	return Shader;
}

//
// The GLSL vertex shader of one VertexShaderEnum: Epic's fragments in the order
// FD3DCombinedVertexShader::FD3DCombinedVertexShader splices them (D3DResource.cpp 2230-2620).
// Returns an empty string for combinations that need the Xbox asset format.
//
FString UUC2GLRenderDevice::BuildVertexShaderSource( QWORD Enum )
{
	// GPU skinning and the packed terrain vertices exist only with XBOX_ASSET_FORMAT; of the
	// unique shaders the projector attenuation ones are engine-gated (projector batching).
	// M7: VS_Distortion, VS_DistortionAccum and VS_ColorRemap (Distortion.vsh,
	// DistortionAccum.vsh, ColorRemap.vsh) are composed below, with what
	// FD3DCombinedVertexShader skips for each of them.
	DWORD Unique = VS_GETUNIQUESHADER(Enum);
	if( (Enum & (VS_SkeletalSkin | VS_TerrainSpecial)) || (Unique && Unique != VS_Distortion && Unique != VS_DistortionAccum && Unique != VS_ColorRemap) )
		return FString();
	UBOOL	SkipTexStage[4]	= { 0, 0, 0, 0 },
			SkipLighting	= 0,
			SkipOutputColor	= 0;
	if( Unique == VS_Distortion )
	{
		// stage 1 and 2 used for 3x2 matrix multiply in pixel shader when doing distortion, so skip
		SkipTexStage[1] = SkipTexStage[2] = 1;
		SkipLighting = 1;
	}
	else if( Unique == VS_DistortionAccum )
	{
		// stage 2 used for texcoord that represents distoriton amount
		SkipTexStage[2] = 1;
		SkipLighting = 1;
	}
	else if( Unique == VS_ColorRemap )
	{
		SkipTexStage[1] = SkipTexStage[2] = 1;
		SkipLighting = 1;
		SkipOutputColor = 1;
	}

	FString S;
	S += TEXT("// VertexShaderEnum ");
	S += appI64toa( (SQWORD)Enum );
	S += TEXT("\n")
		TEXT("attribute vec4 a_position;\n")		// POSITION
		TEXT("attribute vec3 a_normal;\n")			// NORMAL
		TEXT("attribute vec4 a_diffuse;\n")			// DIFFUSE
		TEXT("attribute vec4 a_specular;\n")		// SPECULAR
		TEXT("attribute vec4 a_tex0;\n")			// TEXCOORD0..3
		TEXT("attribute vec4 a_tex1;\n")
		TEXT("attribute vec4 a_tex2;\n")
		TEXT("attribute vec4 a_tex3;\n")
		TEXT("uniform mat4 u_clip;\n")				// VSC_CLIP_MATRIX
		TEXT("uniform mat4 u_modelView;\n")			// VSC_MVIEW_MATRIX
		TEXT("uniform mat3 u_normalMatrix;\n")		// VSC_INVT_MVIEW_MATRIX
		TEXT("uniform vec4 u_ambient;\n")			// VSC_AMBIENT_COLOR
		TEXT("uniform vec3 u_lightDir[5];\n")		// VSC_HARD_LIGHT_BASE_DIRS (local space)
		TEXT("uniform vec3 u_lightColor[5];\n")		// VSC_HARD_LIGHT_BASE_COLORS
		TEXT("uniform vec4 u_fogParams;\n")			// VSC_FOG
		TEXT("uniform mat4 u_texMatrix0;\n")		// VSC_TEXn_MATRIX
		TEXT("uniform mat4 u_texMatrix1;\n")
		TEXT("uniform mat4 u_texMatrix2;\n")
		TEXT("uniform mat4 u_texMatrix3;\n")
		TEXT("uniform vec4 u_distortionScale;\n")	// VSC_DISTORTION_SCALE
		TEXT("uniform vec4 u_distortionScreen;\n")	// clip space xy to back-buffer texture coordinates (scale, offset)
		TEXT("uniform vec4 u_colorRemap;\n")		// VSC_COLOR_REMAP
		TEXT("varying vec4 v_color0;\n")			// oD0
		TEXT("varying vec4 v_color1;\n")			// oD1
		TEXT("varying float v_fog;\n")				// oFog
		TEXT("varying vec4 v_tex0;\n")				// oT0..3
		TEXT("varying vec4 v_tex1;\n")
		TEXT("varying vec4 v_tex2;\n")
		TEXT("varying vec4 v_tex3;\n")
		TEXT("void main()\n")
		TEXT("{\n")
		TEXT("\tvec4 localPosition;\n")				// R_LOCAL_POSITION
		TEXT("\tvec3 localNormal;\n")				// R_LOCAL_NORMAL
		TEXT("\tvec4 finalColor;\n")				// R_FINAL_COLOR
		TEXT("\tvec4 texSource;\n")					// R_TEX_SOURCE
		TEXT("\tvec4 camPosition;\n")				// R_CAMERASPACE_POSITION
		TEXT("\tvec4 camNormal;\n")					// R_CAMERASPACE_NORMAL
		TEXT("\tvec4 camReflect;\n")				// R_CAMERASPACE_REFLECT
		TEXT("\tvec4 t;\n")
		// what the registers a shader does not write hold
		TEXT("\tv_color1 = vec4(0.0, 0.0, 0.0, 1.0);\n")
		TEXT("\tv_fog = 1.0;\n")
		TEXT("\tv_tex0 = vec4(0.0, 0.0, 0.0, 1.0);\n")
		TEXT("\tv_tex1 = vec4(0.0, 0.0, 0.0, 1.0);\n")
		TEXT("\tv_tex2 = vec4(0.0, 0.0, 0.0, 1.0);\n")
		TEXT("\tv_tex3 = vec4(0.0, 0.0, 0.0, 1.0);\n");

	// VS_UnpackPosition (UnpackPosition.vsh) unpacks FPackedPosition, which only exists with
	// XBOX_ASSET_FORMAT: PC streams carry plain positions, so DefaultPosition.vsh always.
	S +=	TEXT("\t// DefaultPosition.vsh\n")
			TEXT("\tlocalPosition = a_position;\n")
			TEXT("\tlocalNormal = a_normal;\n")
			TEXT("\t// NotBoned.vsh (Screenspace.vsh is GL's own viewport transform)\n")
			TEXT("\tgl_Position = u_clip * localPosition;\n")
			TEXT("\t// InitRGB.vsh\n")
			TEXT("\tfinalColor = vec4(0.0);\n")
			TEXT("\t// LightAmbient.vsh\n")
			TEXT("\tfinalColor += u_ambient;\n");

	if( Unique == VS_ColorRemap )
		S +=	TEXT("\t// ColorRemap.vsh\n")
				TEXT("\tv_tex1 = u_colorRemap;\n")
				TEXT("\tv_tex2 = u_colorRemap;\n");

	if( !SkipLighting && (Enum & VS_Lit) )
	{
		S +=	TEXT("\t// LightHardDirectional5.vsh\n")
				TEXT("\tlocalNormal = localNormal * inversesqrt(dot(localNormal, localNormal));\n");
		for( INT i=0; i<VS_Lights_Directional_MAX; i++ )
			S += FString::Printf( TEXT("\tfinalColor.xyz += u_lightColor[%i] * max(dot(localNormal, -u_lightDir[%i]), 0.0);\n"), i, i );
	}

	if( SkipOutputColor )
		S +=	TEXT("\t// (no colour output fragment: oD0 is not written; nothing reads it)\n")
				TEXT("\tfinalColor = vec4(0.0, 0.0, 0.0, 1.0);\n");
	else if( Enum & VS_VertexColored )
	{
		if( Enum & VS_StaticMeshVertexColor )
		{
			S +=	TEXT("\t// MulVertexSpec.vsh\n")
					TEXT("\tfinalColor *= a_specular;\n");
			if( Enum & VS_StaticMeshVertexColorDiffuse )
				S +=	TEXT("\t// AddVertexRGB.vsh\n")
						TEXT("\tfinalColor += a_diffuse;\n");
		}
		else
			S +=	TEXT("\t// AddVertexRGB.vsh\n")
					TEXT("\tfinalColor += a_diffuse;\n");
	}
	S +=	TEXT("\t// OutLightRGB.vsh (output colours are clamped to [0,1])\n")
			TEXT("\tv_color0 = clamp(finalColor, 0.0, 1.0);\n");

	UBOOL CameraSpacePositionInit = 0, CameraSpaceNormalInit = 0, CameraSpaceReflectInit = 0;

	// since the distortion amount is modulated by vertex alpha color
	// then we have to add the fragments here AFTER vertex color calcs
	if( Unique == VS_Distortion )
	{
		// Distortion.vsh. The Xbox fragment replaces Screenspace.vsh: it computes the screen
		// position itself, sets oPos.w to +-1 (so everything interpolates linearly on screen)
		// and writes the rows of the 3x2 matrix Distortion.psh multiplies the distortion
		// texel with: ( scale * vertex alpha, 0, back-buffer x ) and ( 0, scale * alpha,
		// back-buffer y ). There the back buffer is a linear texture addressed in pixels;
		// here its copy is addressed 0..1, so the pixel amounts are divided by its size and
		// the position comes from the clip-space position through u_distortionScreen (GL's
		// own window transform: exact texel centres whatever the half-pixel conventions).
		S +=	TEXT("\t// Distortion.vsh\n")
				TEXT("\tgl_Position = gl_Position / max(abs(gl_Position.w), 0.000001);\n")
				TEXT("\tt.xy = u_distortionScale.xy * finalColor.w;\n")
				TEXT("\tv_tex1 = vec4(t.x, 0.0, gl_Position.x * gl_Position.w * u_distortionScreen.x + u_distortionScreen.z, 1.0);\n")
				TEXT("\tv_tex2 = vec4(0.0, t.y, gl_Position.y * gl_Position.w * u_distortionScreen.y + u_distortionScreen.w, 1.0);\n");
	}
	else if( Unique == VS_DistortionAccum )
	{
		S +=	TEXT("\t// TexCamPosition.vsh\n")
				TEXT("\tcamPosition = u_modelView * localPosition;\n");
		CameraSpacePositionInit = 1;
		S +=	TEXT("\t// DistortionAccum.vsh\n")
				TEXT("\tt.x = u_distortionScale.x * finalColor.w;\n")						// scale by distortion amount and vertex alpha
				TEXT("\tt.y = clamp(u_distortionScale.z * camPosition.z, 0.0, 1.0);\n")	// distance attenuate distortion
				TEXT("\tt.y = 1.0 - t.y * t.y;\n")											// exponential falloff, 1 - result
				TEXT("\tv_tex2 = vec4(t.y * t.x, t.y * t.x, 0.0, 1.0);\n");
	}

	for( INT StageIndex=0; StageIndex<4; StageIndex++ )
	{
		// skip flagged texture stages (some special case unique shaders require this)
		if( SkipTexStage[StageIndex] )
			continue;

		DWORD TexSource = VS_GETTEXSOURCE(StageIndex,Enum);
		if( TexSource == VS_Tex_Source_None )
			continue;

		DWORD	Transform	= VS_GETTEXTRANSFORM(StageIndex,Enum);
		UBOOL	Direct		= 0;	// the VS_TexcoordN_Pass/Transform fragments: no copy through R_TEX_SOURCE

		if( TexSource < 4 )
		{
			if( (DWORD)StageIndex == TexSource && (Transform == VS_Tex_Transform_None || Transform == VS_Tex_Transform_m4x2) )
				Direct = 1;
			else
				S += FString::Printf( TEXT("\t// TexSource%i.vsh\n\ttexSource = a_tex%i;\n"), TexSource, TexSource );
		}
		else
		{
			if( !CameraSpacePositionInit && (TexSource == VS_Tex_Source_CAMERASPACEPOSITION || TexSource == VS_Tex_Source_CAMERASPACEREFLECTIONVECTOR) )
			{
				S +=	TEXT("\t// TexCamPosition.vsh\n")
						TEXT("\tcamPosition = u_modelView * localPosition;\n");
				CameraSpacePositionInit = 1;
			}
			if( !CameraSpaceNormalInit && (TexSource == VS_Tex_Source_CAMERASPACENORMAL || TexSource == VS_Tex_Source_CAMERASPACEREFLECTIONVECTOR) )
			{
				S +=	TEXT("\t// TexCamNormal.vsh\n")
						TEXT("\tcamNormal.xyz = u_normalMatrix * localNormal;\n")
						TEXT("\tcamNormal.xyz *= inversesqrt(dot(camNormal.xyz, camNormal.xyz));\n")
						TEXT("\tcamNormal.w = 1.0;\n");
				CameraSpaceNormalInit = 1;
			}
			if( TexSource == VS_Tex_Source_CAMERASPACEPOSITION )
				S +=	TEXT("\t// TexSourceCamPos.vsh\n\ttexSource = camPosition;\n");
			else if( TexSource == VS_Tex_Source_CAMERASPACENORMAL )
				S +=	TEXT("\t// TexSourceCamNorm.vsh\n\ttexSource = camNormal;\n");
			else
			{
				if( !CameraSpaceReflectInit )
				{
					S +=	TEXT("\t// TexCamReflect.vsh\n")
							TEXT("\tt.xyz = camPosition.xyz * inversesqrt(dot(camPosition.xyz, camPosition.xyz));\n")
							TEXT("\tcamReflect.xyz = (-2.0 * dot(t.xyz, camNormal.xyz)) * camNormal.xyz + t.xyz;\n")
							TEXT("\tcamReflect.w = 1.0;\n");
					CameraSpaceReflectInit = 1;
				}
				S +=	TEXT("\t// TexSourceCamRef.vsh\n\ttexSource = camReflect;\n");
			}
		}

		FString SrcName = Direct ? FString::Printf(TEXT("a_tex%i"),StageIndex) : FString(TEXT("texSource"));
		switch( Transform )
		{
		case VS_Tex_Transform_None:
			S += FString::Printf( TEXT("\t// %s%i.vsh\n\tv_tex%i = %s;\n"), Direct ? TEXT("TexPass") : TEXT("TexSourcePass"), StageIndex, StageIndex, *SrcName );
			break;
		case VS_Tex_Transform_m4x2:
			// dp4 oTn.x, SRC.xyww, c[VSC_TEXn_MATRIX0] / dp4 oTn.y, SRC.xyww, c[VSC_TEXn_MATRIX1]
			S += FString::Printf( TEXT("\t// %s%i%s.vsh\n\tt = u_texMatrix%i * %s.xyww;\n\tv_tex%i.xy = t.xy;\n"),
				Direct ? TEXT("TexTransform") : TEXT("TexSourceTransform"), StageIndex, Direct ? TEXT("") : TEXT("_m4x2"), StageIndex, *SrcName, StageIndex );
			break;
		case VS_Tex_Transform_m4x3:
			S += FString::Printf( TEXT("\t// TexSourceTransform%i_m4x3z.vsh\n\tt = u_texMatrix%i * %s;\n\tv_tex%i.xyz = t.xyz;\n"), StageIndex, StageIndex, *SrcName, StageIndex );
			break;
		case VS_Tex_Transform_m4x3_Projected:
			S += FString::Printf( TEXT("\t// TexSourceTransform%i_m4x3w.vsh\n\tt = u_texMatrix%i * %s;\n\tv_tex%i.xy = t.xy;\n\tv_tex%i.w = t.z;\n"), StageIndex, StageIndex, *SrcName, StageIndex, StageIndex );
			break;
		}
	}

	if( Enum & VS_Fog )
	{
		if( !CameraSpacePositionInit )
			S +=	TEXT("\t// TexCamPosition.vsh\n")
					TEXT("\tcamPosition = u_modelView * localPosition;\n");
		S +=	TEXT("\t// Fog.vsh (mad oFog.x, -R_CAMERASPACE_POSITION.z, VSC_FOG_START, VSC_FOG_END; the output is clamped)\n")
				TEXT("\tv_fog = clamp(-camPosition.z * u_fogParams.x + u_fogParams.y, 0.0, 1.0);\n");
	}

	S += TEXT("}\n");
	return S;
}

GLuint UUC2GLRenderDevice::GetVertexShaderObject( QWORD Enum )
{
	GLuint* Found = VertexShaderObjects.Find( Enum );
	if( Found )
		return *Found;

	GLuint	Shader = 0;
	FString	Source = BuildVertexShaderSource( Enum );
	if( Source.Len() )
	{
		Shader = UC2GL_CompileShader( this, GL_VERTEX_SHADER, ShaderPrologue(0), TCHAR_TO_ANSI(*Source), *FString::Printf(TEXT("vertex shader %s"),appI64toa((SQWORD)Enum)) );
		if( Shader )
			VertexShadersCompiled++;
		else
			VertexShadersFailed++;
	}
	VertexShaderObjects.Set( Enum, Shader );
	return Shader;
}

GLuint UUC2GLRenderDevice::GetFragmentShaderObject( INT PixelShader, DWORD Variant )
{
	DWORD	Key = (DWORD)PixelShader | (Variant << 8);
	GLuint*	Found = FragmentShaderObjects.Find( Key );
	if( Found )
		return *Found;

	GLuint Shader = 0;
	const FUC2GLPixelShaderSource& Source = GUC2GLPixelShaders[PixelShader];
	if( Source.Source )
	{
		ANSICHAR Header[256];
		appStrcpy( Header, ShaderPrologue(1) );
		for( INT s=0; s<4; s++ )
			if( Variant & (1<<s) )
				appSprintf( Header + appStrlen(Header), "#define UC2_TEX%i_CUBE 1\n", s );
		if( Variant & PSV_SpecularEnable )
			appStrcat( Header, "#define UC2_SPECULARENABLE 1\n" );
		Shader = UC2GL_CompileShader( this, GL_FRAGMENT_SHADER, Header, Source.Source, *FString::Printf(TEXT("pixel shader %i (%s.psh, variant %i)"),PixelShader,Source.Name,Variant) );
		if( Shader )
			FragmentShadersCompiled++;
		else
			FragmentShadersFailed++;
	}
	FragmentShaderObjects.Set( Key, Shader );
	return Shader;
}

FOpenGLProgram* UUC2GLRenderDevice::GetProgram( INT PixelShader, DWORD Variant, QWORD VertexShaderEnum )
{
	DWORD	PSKey	= (DWORD)PixelShader | (Variant << 8);
	QWORD	Hash	= VertexShaderEnum ^ ((QWORD)PSKey * 0x9E3779B97F4A7C15ULL);

	FOpenGLProgram* First = Programs.FindRef( Hash );
	for( FOpenGLProgram* P=First; P; P=P->Next )
		if( P->VertexShaderEnum == VertexShaderEnum && P->PSKey == PSKey )
			return P;

	FOpenGLProgram* P = new FOpenGLProgram;
	appMemzero( P, sizeof(FOpenGLProgram) );
	P->VertexShaderEnum	= VertexShaderEnum;
	P->PSKey			= PSKey;
	P->Next				= First;
	Programs.Set( Hash, P );

	GLuint	VS = GetVertexShaderObject( VertexShaderEnum ),
			FS = GetFragmentShaderObject( PixelShader, Variant );
	if( VS && FS )
	{
		GLuint Program = glCreateProgram();
		glAttachShader( Program, VS );
		glAttachShader( Program, FS );
		glBindAttribLocation( Program, VA_Position,		"a_position" );
		glBindAttribLocation( Program, VA_Normal,		"a_normal" );
		glBindAttribLocation( Program, VA_Diffuse,		"a_diffuse" );
		glBindAttribLocation( Program, VA_Specular,		"a_specular" );
		glBindAttribLocation( Program, VA_TexCoord0+0,	"a_tex0" );
		glBindAttribLocation( Program, VA_TexCoord0+1,	"a_tex1" );
		glBindAttribLocation( Program, VA_TexCoord0+2,	"a_tex2" );
		glBindAttribLocation( Program, VA_TexCoord0+3,	"a_tex3" );
		glLinkProgram( Program );
		GLint Status = 0;
		glGetProgramiv( Program, GL_LINK_STATUS, &Status );
		if( Status )
		{
			P->Program			= Program;
			P->ClipLoc			= glGetUniformLocation( Program, "u_clip" );
			P->ModelViewLoc		= glGetUniformLocation( Program, "u_modelView" );
			P->NormalMatrixLoc	= glGetUniformLocation( Program, "u_normalMatrix" );
			P->AmbientLoc		= glGetUniformLocation( Program, "u_ambient" );
			P->LightDirLoc		= glGetUniformLocation( Program, "u_lightDir" );
			P->LightColorLoc	= glGetUniformLocation( Program, "u_lightColor" );
			P->FogParamsLoc		= glGetUniformLocation( Program, "u_fogParams" );
			P->FogColorLoc		= glGetUniformLocation( Program, "u_fogColor" );
			ANSICHAR Name[32];
			for( INT i=0; i<4; i++ )
			{
				appSprintf( Name, "u_texMatrix%i", i );
				P->TexMatrixLoc[i] = glGetUniformLocation( Program, Name );
			}
			for( INT i=0; i<8; i++ )
			{
				appSprintf( Name, "u_c%i", i );
				P->ConstantLoc[i] = glGetUniformLocation( Program, Name );
			}
			P->DistortionScaleLoc	= glGetUniformLocation( Program, "u_distortionScale" );
			P->DistortionScreenLoc	= glGetUniformLocation( Program, "u_distortionScreen" );
			P->ColorRemapLoc		= glGetUniformLocation( Program, "u_colorRemap" );
			// Samplers never change: stage n is texture unit n.
			glUseProgram( Program );
			for( INT i=0; i<4; i++ )
			{
				appSprintf( Name, "u_tex%i", i );
				GLint Loc = glGetUniformLocation( Program, Name );
				if( Loc >= 0 )
					glUniform1i( Loc, i );
			}
			glUseProgram( 0 );
			ProgramsLinked++;
			debugf( NAME_Init, TEXT("UC2GLDrv: program %i: pixel shader %i (%s.psh) variant %i, vertex shader %s"), ProgramsLinked, PixelShader, GUC2GLPixelShaders[PixelShader].Name, Variant, appI64toa((SQWORD)VertexShaderEnum) );
		}
		else
		{
			ANSICHAR Log[1024] = "";
			glGetProgramInfoLog( Program, sizeof(Log)-1, NULL, Log );
			debugf( NAME_Warning, TEXT("UC2GLDrv: program for pixel shader %i (%s.psh), vertex shader %s does not link: %s"), PixelShader, GUC2GLPixelShaders[PixelShader].Name, appI64toa((SQWORD)VertexShaderEnum), Log );
			glDeleteProgram( Program );
			ProgramsFailed++;
		}
	}
	else
		ProgramsFailed++;

	return P;
}

/*-----------------------------------------------------------------------------
	Device set-up.
-----------------------------------------------------------------------------*/

void UUC2GLRenderDevice::InitPixelShaders()
{
	guard(UUC2GLRenderDevice::InitPixelShaders);

	ExitPixelShaders();
	appMemzero( PixelShaderState, sizeof(PixelShaderState) );

	INT Ready = 0, Failed = 0, M7 = 0, Gated = 0;
	for( INT Type=0; Type<PS_MAX; Type++ )
	{
		if( !GUC2GLPixelShaders[Type].Source )
			PixelShaderState[Type] = PSS_None;
		else if( UC2GL_IsM7PixelShader(Type) && !(UsePostProcess && PostCopyProgram) )
		{
			PixelShaderState[Type] = PSS_M7;
			M7++;
		}
		else if( UC2GL_IsEngineGatedPixelShader(Type) )
		{
			PixelShaderState[Type] = PSS_EngineGated;
			Gated++;
		}
		else if( !UsePixelShaders )
			PixelShaderState[Type] = PSS_None;
		else if( GetFragmentShaderObject( Type, 0 ) )
		{
			PixelShaderState[Type] = PSS_Ready;
			Ready++;
		}
		else
		{
			// One line per failing shader; its materials take the M5 stage-0 fallback.
			debugf( NAME_Warning, TEXT("UC2GLDrv: pixel shader %i (%s.psh) is not usable; its materials are drawn with their first texture stage"), Type, GUC2GLPixelShaders[Type].Name );
			PixelShaderState[Type] = PSS_Failed;
			Failed++;
		}
	}

	if( UsePixelShaders )
		debugf( NAME_Init, TEXT("UC2GLDrv: pixel shaders on: %i fragment shaders compiled, %i failed; %i need post-processing, which is off (UsePostProcess), %i are not reachable on PC (engine gates)"), Ready, Failed, M7, Gated );
	else
		debugf( NAME_Init, TEXT("UC2GLDrv: pixel shaders off (UsePixelShaders=False): every pixel-shader material is drawn with its first texture stage, as at M5") );

	// The Xbox list of vertex shader combinations as a precompile list.
	if( UsePixelShaders && PrecompileVertexShaders )
	{
		PrecompileSeen = PrecompileNotApplicable = 0;
		UVertexShaderConfigs* Configs = ConstructObject<UVertexShaderConfigs>( UVertexShaderConfigs::StaticClass(), (UObject*)-1, NAME_None, RF_Keep );
		DOUBLE Start = appSeconds();
		Configs->PrecacheVertexShaderEnums( this );
		debugf( NAME_Init, TEXT("UC2GLDrv: VertexShaderConfigs.ini: %i combinations, %i vertex shaders compiled (%i failed), %i need the Xbox asset format (GPU skinning, packed terrain) or an M7 shader, %.2f s"),
			PrecompileSeen, VertexShadersCompiled, VertexShadersFailed, PrecompileNotApplicable, (FLOAT)(appSeconds() - Start) );
		Configs->VertexShaderEnums.Empty();
		VertexShadersPrecompiled = VertexShaderObjects.Num();
	}

	unguard;
}

void UUC2GLRenderDevice::ExitPixelShaders()
{
	guard(UUC2GLRenderDevice::ExitPixelShaders);

	UBOOL Alive = ContextAlive();
	for( TMap<QWORD,FOpenGLProgram*>::TIterator It(Programs); It; ++It )
	{
		FOpenGLProgram* P = It.Value();
		while( P )
		{
			FOpenGLProgram* Next = P->Next;
			if( P->Program && Alive )
				glDeleteProgram( P->Program );
			delete P;
			P = Next;
		}
	}
	Programs.Empty();
	if( Alive )
	{
		for( TMap<QWORD,GLuint>::TIterator It(VertexShaderObjects); It; ++It )
			if( It.Value() )
				glDeleteShader( It.Value() );
		for( TMap<DWORD,GLuint>::TIterator It(FragmentShaderObjects); It; ++It )
			if( It.Value() )
				glDeleteShader( It.Value() );
	}
	VertexShaderObjects.Empty();
	FragmentShaderObjects.Empty();
	ProgramsLinked = ProgramsFailed = 0;
	VertexShadersCompiled = VertexShadersFailed = 0;
	FragmentShadersCompiled = FragmentShadersFailed = 0;
	VertexShadersPrecompiled = 0;

	unguard;
}

void UUC2GLRenderDevice::LogPixelShaderStats()
{
	if( !UsePixelShaders )
		return;
	debugf( NAME_Init, TEXT("UC2GLDrv: programs: %i linked, %i failed; fragment shaders %i compiled, %i failed; vertex shaders %i compiled (%i from VertexShaderConfigs.ini), %i failed; %i program draws, %i fixed-function draws of pixel-shader passes"),
		ProgramsLinked, ProgramsFailed, FragmentShadersCompiled, FragmentShadersFailed, VertexShadersCompiled, VertexShadersPrecompiled, VertexShadersFailed, ProgramDraws, ProgramFallbackDraws );
}

//
// The family of a pixel-shader material: every type its state handler may switch to shares
// the handler (D3DPSMaterialState.cpp's table). A material is drawn with programs only if
// the whole family is ready, so the choice the handler makes later cannot land on a shader
// that is not there.
//
UBOOL UUC2GLRenderDevice::PixelShaderFamilyReady( INT Type )
{
	if( !UsePixelShaders || Type <= PS_None || Type >= PS_MAX )
		return 0;
	if( PixelShaderState[Type] != PSS_Ready )
		return 0;
	for( INT Other=0; Other<PS_MAX; Other++ )
		if( FOpenGLPSMaterialStateHandlers::SameHandler( Type, Other ) && PixelShaderState[Other] != PSS_Ready )
			return 0;
	return 1;
}

void UUC2GLRenderDevice::LoadVertexShaderEnum( QWORD VertexShaderEnum )
{
	// Epic's Xbox list, used as a precompile list. PC streams carry unpacked positions, so
	// VS_UnpackPosition does not apply (see BuildVertexShaderSource).
	PrecompileSeen++;
	QWORD Enum = VertexShaderEnum & ~VS_UnpackPosition;
	if( !UsePixelShaders || !ValidContext )
		return;
	if( (Enum & (VS_SkeletalSkin | VS_TerrainSpecial)) || VS_GETUNIQUESHADER(Enum) )
	{
		// (The unique shaders, distortion and colour remap included, are compiled on first use.)
		PrecompileNotApplicable++;
		return;
	}
	GetVertexShaderObject( Enum );
}

/*-----------------------------------------------------------------------------
	Drawing a pass with a program.
-----------------------------------------------------------------------------*/

//
// The VertexShaderEnum of the current state: FD3DDeferredState::Commit's Xbox branch
// (D3DRenderState.cpp 470-600) and GetTexStageVertexShader.
//
QWORD FOpenGLRenderInterface::GetStateVertexShaderEnum( FOpenGLMaterialState* MaterialState, UBOOL HasDiffuse, UBOOL HasSpecular )
{
	QWORD Enum = VS_Normal | MaterialState->MaterialVertexShaderEnum;

	// HandleVertexShaderLights (HARDCODED_VS_LIGHTS)
	if( CurrentState->UseDynamicLighting )
		Enum |= VS_Lit;

	// RS_COLORVERTEX
	if( CurrentState->UseStaticLighting )
	{
		Enum |= VS_VertexColored;
		// The Xbox static mesh renderer passes these two bits (UnRenderStaticMesh.cpp, _XBOX
		// branch); PC's passes VS_FixedFunction and lets D3D's material sources do the same
		// job (diffuse and ambient from COLOR2, emissive from COLOR1). The streams tell:
		// only static meshes carry FVF_Specular (their colour or alpha stream), and the
		// instance's colour stream is FVF_Diffuse.
		if( HasSpecular )
		{
			Enum |= VS_StaticMeshVertexColor;
			if( HasDiffuse )
				Enum |= VS_StaticMeshVertexColorDiffuse;
		}
	}

	// GetTexStageVertexShader
	for( INT StageIndex=0; StageIndex<4; StageIndex++ )
	{
		FOpenGLMaterialStateStage& Stage = MaterialState->Stages[StageIndex];
		if( StageIndex < MaterialState->StagesUsed && Stage.Texture )
		{
			DWORD Gen = Stage.TexCoordIndex & 0xFFFF0000;
			if( Gen == D3DTSS_TCI_CAMERASPACEPOSITION )
				VS_SETTEXSOURCE( StageIndex, VS_Tex_Source_CAMERASPACEPOSITION, Enum )
			else if( Gen == D3DTSS_TCI_CAMERASPACENORMAL )
				VS_SETTEXSOURCE( StageIndex, VS_Tex_Source_CAMERASPACENORMAL, Enum )
			else if( Gen == D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR )
				VS_SETTEXSOURCE( StageIndex, VS_Tex_Source_CAMERASPACEREFLECTIONVECTOR, Enum )
			else
				VS_SETTEXSOURCE( StageIndex, Stage.TexCoordIndex, Enum )

			DWORD Flags = Stage.TextureTransformsEnabled ? Stage.TexCoordCount : D3DTTFF_DISABLE;
			if( Flags > 0 )
			{
				if( Flags & D3DTTFF_PROJECTED )
					VS_SETTEXTRANSFORM( StageIndex, VS_Tex_Transform_m4x3_Projected, Enum )
				else if( VS_GETTEXSOURCE( StageIndex, Enum ) >= VS_Tex_Source_CAMERASPACEPOSITION )
					VS_SETTEXTRANSFORM( StageIndex, VS_Tex_Transform_m4x3, Enum )
				else
					VS_SETTEXTRANSFORM( StageIndex, VS_Tex_Transform_m4x2, Enum )
			}
		}
		else
			VS_SETTEXSOURCE( StageIndex, VS_Tex_Source_None, Enum )
	}

	if( CurrentState->DistanceFogEnabled )
		Enum |= VS_Fog;

	return Enum;
}

//
// Sets up program, uniforms and vertex attributes for a pass of a pixel-shader material.
// Returns 0 when the pass has to be drawn with the fixed-function path.
//
UBOOL FOpenGLRenderInterface::BeginProgramPass( INT Pass, INT FirstIndex )
{
	guard(FOpenGLRenderInterface::BeginProgramPass);

	FOpenGLMaterialState* MaterialState = CurrentState->MaterialPasses[Pass];
	if( !MaterialState->UsePixelShaderProgram || MaterialState->PixelShader == PS_None )
		return 0;

	INT PixelShader = MaterialState->PixelShader;
	if( PixelShader >= PS_MAX || RenDev->PixelShaderState[PixelShader] != PSS_Ready )
	{
		RenDev->ProgramFallbackDraws++;
		return 0;
	}
	const FUC2GLPixelShaderSource& Source = GUC2GLPixelShaders[PixelShader];

	// Texture coordinate sets beyond TEXCOORD3 have no register in Epic's vertex shaders.
	for( INT s=0; s<Min<INT>(MaterialState->StagesUsed,4); s++ )
		if( (MaterialState->Stages[s].TexCoordIndex & 0xFFFF0000) == D3DTSS_TCI_PASSTHRU && (MaterialState->Stages[s].TexCoordIndex & 0xFFFF) > 3 )
		{
			UC2GL_LOG_ONCE( TEXT("UC2GLDrv: a pixel-shader pass uses texture coordinate set %i; drawn with fixed function"), MaterialState->Stages[s].TexCoordIndex & 0xFFFF );
			RenDev->ProgramFallbackDraws++;
			return 0;
		}

	// Vertex streams as generic attributes.
	UBOOL	NoIndexBuffer	= CurrentState->IndexBuffer == NULL;
	INT		BaseVertex		= NoIndexBuffer ? FirstIndex : CurrentState->IndexBufferBase;
	UBOOL	Has[VA_MAX];
	appMemzero( Has, sizeof(Has) );

	FOpenGLVertexShader* Shader = CurrentState->VertexShader;
	for( INT StreamIndex=0; Shader && StreamIndex<CurrentState->NumStreams && StreamIndex<Shader->Declaration.NumStreams; StreamIndex++ )
	{
		FOpenGLVertexStream* Stream = CurrentState->Streams[StreamIndex];
		if( !Stream )
			continue;
		INT	Stride	= CurrentState->StreamStrides[StreamIndex],
			Offset	= Stride * BaseVertex;
		RenDev->glBindBuffer( GL_ARRAY_BUFFER, Stream->BufferID );

		FStreamDeclaration& Decl = Shader->Declaration.Streams[StreamIndex];
		for( INT ComponentIndex=0; ComponentIndex<Decl.NumComponents; ComponentIndex++ )
		{
			FVertexComponent&	Component = Decl.Components[ComponentIndex];
			INT					NumCoords = 0;
			switch( Component.Type )
			{
			case CT_Float4:	NumCoords = 4; break;
			case CT_Float3:	NumCoords = 3; break;
			case CT_Float2:	NumCoords = 2; break;
			case CT_Float1:	NumCoords = 1; break;
			case CT_Color:	NumCoords = 1; break;
			default:		break;
			}
			const BYTE*	Pointer	= (const BYTE*)(PTRINT)Offset;
			INT			Attrib	= -1;
			switch( Component.Function )
			{
			case FVF_Position:	Attrib = VA_Position;	break;
			case FVF_Normal:	Attrib = VA_Normal;		break;
			case FVF_Diffuse:	Attrib = VA_Diffuse;	break;
			case FVF_Specular:	Attrib = VA_Specular;	break;
			case FVF_TexCoord0:
			case FVF_TexCoord1:
			case FVF_TexCoord2:
			case FVF_TexCoord3:	Attrib = VA_TexCoord0 + (Component.Function - FVF_TexCoord0); break;
			default:			break;
			}
			if( Attrib >= 0 && NumCoords )
			{
				if( Component.Type == CT_Color )
					RenDev->glVertexAttribPointer( Attrib, 4, GL_UNSIGNED_BYTE, GL_TRUE, Stride, Pointer );
				else
					RenDev->glVertexAttribPointer( Attrib, NumCoords, GL_FLOAT, GL_FALSE, Stride, Pointer );
				Has[Attrib] = 1;
			}
			Offset += NumCoords * 4;
		}
	}
	if( !Has[VA_Position] )
	{
		RenDev->ProgramFallbackDraws++;
		return 0;
	}

	// Program.
	QWORD	Enum	= GetStateVertexShaderEnum( MaterialState, Has[VA_Diffuse], Has[VA_Specular] );
	DWORD	Variant	= MaterialState->SpecularEnable ? PSV_SpecularEnable : 0;
	for( INT s=0; s<4; s++ )
		if( Source.TexOp[s] == 1 && s < MaterialState->StagesUsed && MaterialState->Stages[s].Texture && MaterialState->Stages[s].Texture->TextureID && MaterialState->Stages[s].Texture->IsCubemap )
			Variant |= 1 << s;

	FOpenGLProgram* Program = RenDev->GetProgram( PixelShader, Variant, Enum );
	if( !Program->Program )
	{
		if( !Program->Reported )
		{
			Program->Reported = 1;
			debugf( NAME_Warning, TEXT("UC2GLDrv: no program for pixel shader %i (%s.psh) with vertex shader %s; drawn with fixed function"), PixelShader, Source.Name, appI64toa((SQWORD)Enum) );
		}
		RenDev->ProgramFallbackDraws++;
		return 0;
	}

	// The fixed-function arrays stay off while a program draws: their pointers may belong to
	// other streams.
	RenDev->glDisableClientState( GL_VERTEX_ARRAY );
	RenDev->glDisableClientState( GL_NORMAL_ARRAY );
	RenDev->glDisableClientState( GL_COLOR_ARRAY );
	RenDev->glDisableClientState( GL_SECONDARY_COLOR_ARRAY );
	for( INT s=0; s<RenDev->NumTextureUnits; s++ )
	{
		RenDev->glClientActiveTexture( GL_TEXTURE0 + s );
		RenDev->glDisableClientState( GL_TEXTURE_COORD_ARRAY );
	}
	RenDev->glClientActiveTexture( GL_TEXTURE0 );

	for( INT Attrib=0; Attrib<VA_MAX; Attrib++ )
	{
		if( Has[Attrib] )
			RenDev->glEnableVertexAttribArray( Attrib );
		else
			RenDev->glDisableVertexAttribArray( Attrib );
	}
	// Registers no stream feeds. A missing DIFFUSE adds nothing and keeps alpha (D3D's
	// material source rule: without the colour the material's emissive, black, is used).
	if( !Has[VA_Normal] )	RenDev->glVertexAttrib4f( VA_Normal,	0.f, 0.f, 1.f, 0.f );
	if( !Has[VA_Diffuse] )	RenDev->glVertexAttrib4f( VA_Diffuse,	0.f, 0.f, 0.f, 1.f );
	if( !Has[VA_Specular] )	RenDev->glVertexAttrib4f( VA_Specular,	1.f, 1.f, 1.f, 1.f );
	for( INT s=0; s<4; s++ )
		if( !Has[VA_TexCoord0+s] )
			RenDev->glVertexAttrib4f( VA_TexCoord0+s, 0.f, 0.f, 0.f, 1.f );

	RenDev->glUseProgram( Program->Program );

	// Transforms.
	FMatrix ModelView, Projection;
	GetGLMatrices( ModelView, Projection );
	FMatrix Clip = ModelView * Projection;
	RenDev->glUniformMatrix4fv( Program->ClipLoc, 1, GL_FALSE, (GLfloat*)&Clip );
	if( Program->ModelViewLoc >= 0 )
		RenDev->glUniformMatrix4fv( Program->ModelViewLoc, 1, GL_FALSE, (GLfloat*)&ModelView );
	if( Program->NormalMatrixLoc >= 0 )
	{
		// VSC_INVT_MVIEW_MATRIX holds the rows of Inverse(World*View); the fragment takes
		// dp3 of the normal with each row.
		FMatrix	Inv = ModelView.Inverse();
		FLOAT	M3[9];
		for( INT i=0; i<3; i++ )
			for( INT j=0; j<3; j++ )
				M3[j*3+i] = Inv.M[i][j];
		RenDev->glUniformMatrix3fv( Program->NormalMatrixLoc, 1, GL_FALSE, M3 );
	}

	// Lighting constants: FD3DDeferredState::Commit (ambient) and HandleVertexShaderLights.
	if( Program->AmbientLoc >= 0 )
	{
		FLOAT W = CurrentState->UseStaticLighting ? 0.f : 1.f;
		if( Enum & VS_StaticMeshVertexColor )
			W = 1.f;
		RenDev->glUniform4f( Program->AmbientLoc, AmbientColor.R / 255.f, AmbientColor.G / 255.f, AmbientColor.B / 255.f, W );
	}
	if( Program->LightDirLoc >= 0 || Program->LightColorLoc >= 0 )
	{
		FLOAT Dirs[VS_Lights_Directional_MAX*3], Colors[VS_Lights_Directional_MAX*3];
		appMemzero( Dirs, sizeof(Dirs) );
		appMemzero( Colors, sizeof(Colors) );
		for( INT LightIndex=0; LightIndex<VS_Lights_Directional_MAX; LightIndex++ )
		{
			FOpenGLLight& Light = Lights[LightIndex];
			if( !Light.Enabled || Light.Type != D3DLIGHT_DIRECTIONAL )
				continue;
			// D3DXVec3TransformNormal( Direction, Transpose(World) ), normalised.
			FVector Local;
			const FMatrix& W = CurrentState->LocalToWorld;
			Local.X = Light.Direction.X * W.M[0][0] + Light.Direction.Y * W.M[0][1] + Light.Direction.Z * W.M[0][2];
			Local.Y = Light.Direction.X * W.M[1][0] + Light.Direction.Y * W.M[1][1] + Light.Direction.Z * W.M[1][2];
			Local.Z = Light.Direction.X * W.M[2][0] + Light.Direction.Y * W.M[2][1] + Light.Direction.Z * W.M[2][2];
			Local = Local.SafeNormal();
			Dirs[LightIndex*3+0] = Local.X;
			Dirs[LightIndex*3+1] = Local.Y;
			Dirs[LightIndex*3+2] = Local.Z;
			Colors[LightIndex*3+0] = Light.Diffuse.X;
			Colors[LightIndex*3+1] = Light.Diffuse.Y;
			Colors[LightIndex*3+2] = Light.Diffuse.Z;
		}
		if( Program->LightDirLoc >= 0 )
			RenDev->glUniform3fv( Program->LightDirLoc, VS_Lights_Directional_MAX, Dirs );
		if( Program->LightColorLoc >= 0 )
			RenDev->glUniform3fv( Program->LightColorLoc, VS_Lights_Directional_MAX, Colors );
	}

	// Fog: VSC_FOG = ( 1/(End-Start), End/(End-Start) ). Without fog the factor stays 1.
	if( Program->FogParamsLoc >= 0 )
	{
		FLOAT Scale = 1.f / (CurrentState->DistanceFogEnd - CurrentState->DistanceFogStart);
		RenDev->glUniform4f( Program->FogParamsLoc, Scale, CurrentState->DistanceFogEnd * Scale, 0.f, 0.f );
	}
	if( Program->FogColorLoc >= 0 )
	{
		FPlane FogColor = (CurrentState->DistanceFogEnabled && MaterialState->OverrideFogColor) ? MaterialState->OverriddenFogColor.Plane() : CurrentState->DistanceFogColor.Plane();
		RenDev->glUniform4f( Program->FogColorLoc, FogColor.X, FogColor.Y, FogColor.Z, FogColor.W );
	}

	// Texture stages: transforms, and opaque black where the shader samples a stage the
	// material left empty.
	for( INT s=0; s<4; s++ )
	{
		UBOOL Bound = s < MaterialState->StagesUsed && MaterialState->Stages[s].Texture && MaterialState->Stages[s].Texture->TextureID;
		if( Bound && Program->TexMatrixLoc[s] >= 0 )
			RenDev->glUniformMatrix4fv( Program->TexMatrixLoc[s], 1, GL_FALSE, (GLfloat*)&MaterialState->Stages[s].TextureTransformMatrix );
		if( !Bound && (Source.TexOp[s] == 1 || Source.TexOp[s] >= 5) )
		{
			RenDev->glActiveTexture( GL_TEXTURE0 + s );
			RenDev->glBindTexture( GL_TEXTURE_2D, RenDev->NullTextureID );
		}
	}
	RenDev->glActiveTexture( GL_TEXTURE0 );

	// M7: the unique vertex shaders' constants (HandlePSVertexShader).
	if( Program->DistortionScaleLoc >= 0 || Program->DistortionScreenLoc >= 0 )
	{
		FOpenGLRenderTargetState*	RTState = SavedRenderTargetState.GetCurrentState();
		FOpenGLViewportState*		VPState = SavedViewportState.GetCurrentState();
		FLOAT	MainW	= Max<INT>( RenDev->MainWidth, 1 ),
				MainH	= Max<INT>( RenDev->MainHeight, 1 );
		if( VS_GETUNIQUESHADER(Enum) == VS_Distortion )
		{
			// Xbox: x and y are pixels of a 640x480 back buffer, z and w the ratio of the
			// back-buffer texture to the back buffer (1). Here: the same fraction of the
			// picture, in texture coordinates of the back-buffer copy.
			FLOAT PixelScale = MainH / 480.f;
			RenDev->glUniform4f( Program->DistortionScaleLoc, DistortionScale[0] * PixelScale / MainW, DistortionScale[1] * PixelScale / MainH, 0.f, 0.f );
			// GL window coordinates of the current viewport, as coordinates of the copy (top row first).
			FLOAT	VPX	= VPState->ViewportX,
					VPW	= VPState->ViewportWidth,
					VPH	= VPState->ViewportHeight,
					VPY	= RTState->Flipped ? VPState->ViewportY : RTState->Height - VPState->ViewportY - VPH;
			if( RTState->Flipped )
				RenDev->glUniform4f( Program->DistortionScreenLoc, 0.5f * VPW / MainW, 0.5f * VPH / MainH, (VPX + 0.5f * VPW) / MainW, (VPY + 0.5f * VPH) / MainH );
			else
				RenDev->glUniform4f( Program->DistortionScreenLoc, 0.5f * VPW / MainW, -0.5f * VPH / MainH, (VPX + 0.5f * VPW) / MainW, 1.f - (VPY + 0.5f * VPH) / MainH );
		}
		else
			RenDev->glUniform4fv( Program->DistortionScaleLoc, 1, DistortionScale );
	}
	if( Program->ColorRemapLoc >= 0 )
		RenDev->glUniform4fv( Program->ColorRemapLoc, 1, ColorRemapConstant );

	// Pixel shader constants (D3D's SetPixelShaderConstant; the shader clamps them to [0,1]).
	for( INT i=0; i<8; i++ )
		if( Program->ConstantLoc[i] >= 0 )
			RenDev->glUniform4fv( Program->ConstantLoc[i], 1, PixelShaderConstants[i] );

	RenDev->ProgramDraws++;
	return 1;

	unguard;
}

void FOpenGLRenderInterface::EndProgramPass()
{
	RenDev->glUseProgram( 0 );
	for( INT Attrib=0; Attrib<VA_MAX; Attrib++ )
		RenDev->glDisableVertexAttribArray( Attrib );
}

/*-----------------------------------------------------------------------------
	Program test (M11): complete programs, vertex side included, drawn and read back.

	For every usable pixel shader and five vertex shader enums (texture coordinates passed
	through; + vertex colours; + five directional lights; + fog; + 4x2 texture transforms on
	stages 0 and 1) the real program of the driver is linked and a quad is drawn with four
	sets of pseudo-random constants, lights, matrices, colours and coordinates, over four
	small bilinear textures; the centre pixel is logged. The numbers mean nothing by
	themselves: tools/dev/m11-twin.sh compares the lines of the desktop binding (whose
	fragment shaders tools/psh2glsl.py --gputest ties to the reference interpreter) with the
	lines of the GL4ES binding, i.e. the same programs after gl4es' conversion to GLSL ES
	1.00 on OpenGL ES. Part of POSTSELFTEST / -glselftest.
-----------------------------------------------------------------------------*/

static DWORD UC2GL_ProgTestHash( DWORD A, DWORD B, DWORD C )
{
	DWORD H = A * 0x9E3779B1u ^ (B + 0x7F4A7C15u) * 0x85EBCA6Bu ^ (C + 0x165667B1u) * 0xC2B2AE35u;
	H ^= H >> 15; H *= 0x2C1B3C6Du; H ^= H >> 12; H *= 0x297A2D39u; H ^= H >> 15;
	return H;
}
// A value on the 8-bit grid in [0,1].
static FLOAT UC2GL_ProgTestUnit( DWORD A, DWORD B, DWORD C )
{
	return (UC2GL_ProgTestHash( A, B, C ) & 255) / 255.f;
}

void UC2GL_RunProgramTest( UUC2GLRenderDevice* RenDev )
{
	guard(UC2GL_RunProgramTest);

	enum { Size = 8, Cases = 4, Enums = 5 };
	QWORD EnumList[Enums];
	for( INT e=0; e<Enums; e++ )
	{
		QWORD Enum = VS_Normal;
		for( INT s=0; s<4; s++ )
			VS_SETTEXSOURCE( s, s, Enum )
		if( e == 1 )	Enum |= VS_VertexColored;
		if( e == 2 )	Enum |= VS_Lit;
		if( e == 3 )	Enum |= VS_Fog;
		if( e == 4 )	{ VS_SETTEXTRANSFORM( 0, VS_Tex_Transform_m4x2, Enum ) VS_SETTEXTRANSFORM( 1, VS_Tex_Transform_m4x2, Enum ) }
		EnumList[e] = Enum;
	}

	// Target and textures (through the binding's ordinary texture path; bilinear, repeating).
	GLuint Target = 0, Framebuffer = 0, Textures[4];
	RenDev->glGenTextures( 1, &Target );
	RenDev->glBindTexture( GL_TEXTURE_2D, Target );
	RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	RenDev->glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, Size, Size, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
	RenDev->glGenFramebuffers( 1, &Framebuffer );
	RenDev->glBindFramebuffer( GL_FRAMEBUFFER, Framebuffer );
	RenDev->glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, Target, 0 );
	RenDev->glGenTextures( 4, Textures );
	for( INT t=0; t<4; t++ )
	{
		BYTE Texels[4*4*4];
		for( INT i=0; i<4*4*4; i++ )
			Texels[i] = UC2GL_ProgTestHash( t, i, 99 ) & 255;
		RenDev->glActiveTexture( GL_TEXTURE0 + t );
		RenDev->glBindTexture( GL_TEXTURE_2D, Textures[t] );
		RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
		RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
		RenDev->glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
		RenDev->glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, Texels );
	}
	RenDev->glActiveTexture( GL_TEXTURE0 );

	RenDev->glViewport( 0, 0, Size, Size );
	RenDev->glDisable( GL_SCISSOR_TEST );
	RenDev->glDisable( GL_STENCIL_TEST );
	RenDev->glDisable( GL_DEPTH_TEST );
	RenDev->glDisable( GL_BLEND );
	RenDev->glDisable( GL_ALPHA_TEST );
	RenDev->glDisable( GL_CULL_FACE );
	RenDev->glDepthMask( GL_FALSE );
	RenDev->glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );

	INT Shaders = 0, Lines = 0, NoProgram = 0;
	for( INT Type=0; Type<PS_MAX; Type++ )
	{
		if( RenDev->PixelShaderState[Type] != PSS_Ready )
			continue;
		Shaders++;
		for( INT e=0; e<Enums; e++ )
		{
			FOpenGLProgram* Program = RenDev->GetProgram( Type, 0, EnumList[e] );
			if( !Program->Program )
			{
				NoProgram++;
				debugf( NAME_Init, TEXT("UC2GLDrv: progtest %i %s v%i: NO PROGRAM"), Type, appFromAnsi(GUC2GLPixelShaders[Type].Name), e );
				continue;
			}
			FString Line;
			for( INT c=0; c<Cases; c++ )
			{
				DWORD Seed = Type * 1000 + e * 10 + c;
				#define U(n) UC2GL_ProgTestUnit( Seed, n, 7 )
				RenDev->glClearColor( 128/255.f, 64/255.f, 192/255.f, 32/255.f );	// what a discarding shader leaves
				RenDev->glClear( GL_COLOR_BUFFER_BIT );
				RenDev->glUseProgram( Program->Program );

				FMatrix Identity = FMatrix::Identity;
				RenDev->glUniformMatrix4fv( Program->ClipLoc, 1, GL_FALSE, (GLfloat*)&Identity );
				if( Program->ModelViewLoc >= 0 )
				{
					// Camera-space z between -100 and -1100 for the fog.
					FMatrix ModelView = FMatrix::Identity;
					ModelView.M[3][2] = -100.f - 1000.f * U(1);
					RenDev->glUniformMatrix4fv( Program->ModelViewLoc, 1, GL_FALSE, (GLfloat*)&ModelView );
				}
				if( Program->NormalMatrixLoc >= 0 )
				{
					static const FLOAT M3[9] = { 1,0,0, 0,1,0, 0,0,1 };
					RenDev->glUniformMatrix3fv( Program->NormalMatrixLoc, 1, GL_FALSE, M3 );
				}
				if( Program->AmbientLoc >= 0 )
					RenDev->glUniform4f( Program->AmbientLoc, U(2) * 0.5f, U(3) * 0.5f, U(4) * 0.5f, U(5) );
				if( Program->LightDirLoc >= 0 || Program->LightColorLoc >= 0 )
				{
					FLOAT Dirs[15], Colors[15];
					for( INT i=0; i<5; i++ )
					{
						FVector D = FVector( U(10+i*3) - 0.5f, U(11+i*3) - 0.5f, -0.25f - U(12+i*3) ).SafeNormal();
						Dirs[i*3] = D.X; Dirs[i*3+1] = D.Y; Dirs[i*3+2] = D.Z;
						for( INT k=0; k<3; k++ )
							Colors[i*3+k] = U(30+i*3+k) * 0.25f;
					}
					if( Program->LightDirLoc >= 0 )
						RenDev->glUniform3fv( Program->LightDirLoc, 5, Dirs );
					if( Program->LightColorLoc >= 0 )
						RenDev->glUniform3fv( Program->LightColorLoc, 5, Colors );
				}
				if( Program->FogParamsLoc >= 0 )
					RenDev->glUniform4f( Program->FogParamsLoc, 1.f / 1000.f, 1200.f / 1000.f, 0.f, 0.f );
				if( Program->FogColorLoc >= 0 )
					RenDev->glUniform4f( Program->FogColorLoc, U(50), U(51), U(52), 1.f );
				for( INT s=0; s<4; s++ )
					if( Program->TexMatrixLoc[s] >= 0 )
					{
						FMatrix M = FMatrix::Identity;
						M.M[0][0] = 0.5f + U(60+s);
						M.M[1][1] = 0.5f + U(64+s);
						M.M[3][0] = U(68+s) * 0.25f;
						M.M[3][1] = U(72+s) * 0.25f;
						RenDev->glUniformMatrix4fv( Program->TexMatrixLoc[s], 1, GL_FALSE, (GLfloat*)&M );
					}
				for( INT i=0; i<8; i++ )
					if( Program->ConstantLoc[i] >= 0 )
						RenDev->glUniform4f( Program->ConstantLoc[i], U(80+i*4), U(81+i*4), U(82+i*4), U(83+i*4) );
				if( Program->DistortionScaleLoc >= 0 )
					RenDev->glUniform4f( Program->DistortionScaleLoc, 0.01f, 0.01f, 0.f, 0.f );
				if( Program->DistortionScreenLoc >= 0 )
					RenDev->glUniform4f( Program->DistortionScreenLoc, 0.5f, 0.5f, 0.5f, 0.5f );
				if( Program->ColorRemapLoc >= 0 )
					RenDev->glUniform4f( Program->ColorRemapLoc, 0.30f, 0.59f, 0.11f, 1.f );

				for( INT t=0; t<4; t++ )
				{
					RenDev->glActiveTexture( GL_TEXTURE0 + t );
					RenDev->glBindTexture( GL_TEXTURE_2D, Textures[t] );
				}
				RenDev->glActiveTexture( GL_TEXTURE0 );

				RenDev->BindQuadBuffer();
				RenDev->glVertexAttribPointer( VA_Position, 2, GL_FLOAT, GL_FALSE, 0, NULL );
				RenDev->glEnableVertexAttribArray( VA_Position );
				for( INT a=1; a<VA_MAX; a++ )
					RenDev->glDisableVertexAttribArray( a );
				RenDev->glVertexAttrib4f( VA_Normal, 0.f, 0.f, 1.f, 0.f );
				RenDev->glVertexAttrib4f( VA_Diffuse, U(110) * 0.5f, U(111) * 0.5f, U(112) * 0.5f, U(113) );
				RenDev->glVertexAttrib4f( VA_Specular, U(114), U(115), U(116), U(117) );
				for( INT s=0; s<4; s++ )
					RenDev->glVertexAttrib4f( VA_TexCoord0+s, U(120+s*4), U(121+s*4), U(122+s*4), 1.f );
				RenDev->glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
				RenDev->glDisableVertexAttribArray( VA_Position );
				RenDev->glBindBuffer( GL_ARRAY_BUFFER, 0 );
				RenDev->glUseProgram( 0 );
				#undef U

				BYTE Pixel[4] = { 0, 0, 0, 0 };
				RenDev->glPixelStorei( GL_PACK_ALIGNMENT, 1 );
				RenDev->glReadPixels( Size/2, Size/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, Pixel );
				Line += FString::Printf( TEXT(" %02x%02x%02x%02x"), Pixel[0], Pixel[1], Pixel[2], Pixel[3] );
			}
			debugf( NAME_Init, TEXT("UC2GLDrv: progtest %i %s v%i:%s"), Type, appFromAnsi(GUC2GLPixelShaders[Type].Name), e, *Line );
			Lines++;
		}
	}
	debugf( NAME_Init, TEXT("UC2GLDrv: progtest summary: %i pixel shaders x %i vertex shader enums, %i programs drawn (%i cases each), %i could not be built"), Shaders, Enums, Lines, Cases, NoProgram );

	RenDev->glBindFramebuffer( GL_FRAMEBUFFER, RenDev->MainFramebuffer );
	RenDev->glDeleteFramebuffers( 1, &Framebuffer );
	RenDev->glDeleteTextures( 1, &Target );
	RenDev->glDeleteTextures( 4, Textures );
	RenDev->glEnable( GL_SCISSOR_TEST );
	RenDev->RenderInterface.RestoreAfterDeviceDraw();

	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
