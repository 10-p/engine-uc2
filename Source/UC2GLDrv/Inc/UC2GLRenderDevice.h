/*=============================================================================
	UC2GLRenderDevice.h: UC2GLDrv.UC2GLRenderDevice, UC2's OpenGL render device.

	engine-ut2004 OpenGLDrv's UOpenGLRenderDevice brought to UC2's URenderDevice
	(UnRenDev.h): 16-bit cache ids per ECacheHint, ResetRenderStates, LoadVertexShaderEnum,
	ReadPixels without the flip flag. Behaviour of the PC D3DDrv (UD3DRenderDevice) where
	the interface says what to do.

	The scene is drawn into the device's main framebuffer object (colour texture +
	depth/stencil); Present draws that texture to the window through a small shader that
	applies the client's Brightness/Contrast/Gamma (the formula of UD3DRenderDevice::
	UpdateGamma), so the desktop's gamma ramp is never touched.
=============================================================================*/

#ifndef _INC_UC2GLRENDERDEVICE
#define _INC_UC2GLRENDERDEVICE

#define AUTO_INITIALIZE_REGISTRANTS_UC2GLDRV UUC2GLRenderDevice::StaticClass();

// Texture-stage capabilities the material compiler asks about (FD3DRenderInterface reads
// these from D3DCAPS8; here they come from the GL context).
struct FOpenGLDeviceCaps
{
	INT		MaxSimultaneousTextures,	// texture units usable by the fixed-function combiner
			MaxTextureBlendStages;		// = MaxSimultaneousTextures: a GL combiner stage needs a unit
	DWORD	TextureOpCaps,
			PrimitiveMiscCaps;
	INT		MaxTextureSize;
};
#define D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR	0x00020000
#define D3DTEXOPCAPS_MULTIPLYADD			0x01000000
#define D3DPMISCCAPS_TSSARGTEMP				0x00000400

//
//	Programmable materials (M6).
//
enum EPixelShaderState
{
	PSS_None		= 0,	// no Xbox shader for the enum value, or pixel shaders are off
	PSS_Ready		= 1,	// translated, compiled, drawn with a program
	PSS_Failed		= 2,	// did not compile: its materials take the stage-0 fallback
	PSS_M7			= 3,	// needs a back-buffer copy, the depth buffer or a render target, and UsePostProcess is off
	PSS_EngineGated	= 4,	// translated, but the PC engine path never asks for it
};

struct FOpenGLProgram
{
	GLuint			Program;			// 0: could not be built
	QWORD			VertexShaderEnum;
	DWORD			PSKey;
	FOpenGLProgram*	Next;
	UBOOL			Reported;
	GLint			ClipLoc, ModelViewLoc, NormalMatrixLoc,
					AmbientLoc, LightDirLoc, LightColorLoc,
					FogParamsLoc, FogColorLoc,
					TexMatrixLoc[4],
					ConstantLoc[8],
					DistortionScaleLoc,		// M7: VSC_DISTORTION_SCALE
					DistortionScreenLoc,	// M7: clip space to back-buffer texture coordinates
					ColorRemapLoc,			// M7: VSC_COLOR_REMAP
					ProjectorLoc,			// M7b: VSC_PROJ_* (u_proj[13])
					SceneDepthRectLoc;		// M11: the scene depth test in the shader (PSV_SceneDepthTest)
};

//
//	UUC2GLRenderDevice
//
class UUC2GLRenderDevice : public URenderDevice
{
	DECLARE_CLASS(UUC2GLRenderDevice,URenderDevice,CLASS_Config,UC2GLDrv)
public:

	// Our render interface.
	FOpenGLRenderInterface			RenderInterface;

	// Resource management (UC2's caches).
	FOpenGLResource*							ResourceList;
	TOpenGLResourceCache<FOpenGLTexture,128>	TextureCache;
	TOpenGLResourceCache<FOpenGLVertexStream,256> VertexBufferCache;
	TOpenGLResourceCache<FOpenGLIndexBuffer,16>	IndexBufferCache;
	FOpenGLVertexShader*			VertexShaders;

	FOpenGLDynamicVertexStream*		DynamicVertexStream;
	FOpenGLDynamicIndexBuffer*		DynamicIndexBuffer16;
	FOpenGLDynamicIndexBuffer*		DynamicIndexBuffer32;

	FOpenGLDefaultRenderTarget*		DefaultRenderTarget;

	// Configuration ([UC2GLDrv.UC2GLRenderDevice]; the D3D driver's keys where they apply).
	UBOOL							UsePrecaching,
									UseTrilinear,
									UseMipmapping,
									UseVSync,
									UseCubemaps,
									CheckGLErrors;
	INT								LevelOfAnisotropy,
									MaxTextureUnits,
									MaterialStages;		// O-9: the stage count the material compiler is given (Epic's D3DDrv: exactly 4)
	FLOAT							DetailTexMipBias,
									DefaultTexMipBias;

	// Status.
	FRenderCaps						RenderCaps;
	FOpenGLDeviceCaps				DeviceCaps;
	UViewport*						LockedViewport;
	UViewport*						LastViewport;		// the viewport of the last frame (GLPROBE)
	INT								FrameCounter;
	INT								NumTextureUnits;
	FLOAT							MaxAnisotropy;
	UBOOL							ValidContext;
	INT								GLMajorVersion,
									GLMinorVersion;
	D3DTEXTUREADDRESS				CubemapTextureAddressing;
	UBOOL							IsGeForce;	// D3DDrv: GeForce1/2-class (2 textures); never true here

	// The main framebuffer: the default render target. Its depth/stencil attachment is a
	// texture (M7), so a shader can read the scene's depth (depth of field).
	GLuint							MainFramebuffer,
									MainColorTexture,
									MainDepthStencil,		// texture (CapDepthTexture) ...
									MainDepthRenderbuffer;	// ... or renderbuffer (M11: no depth textures)
	INT								MainWidth,
									MainHeight;
	// M11 / O-8, anti-aliasing. AntiAliasing (ini; -glaa=N, -glnoaa) is the number of samples.
	// Desktop GL: MainFramebuffer is then a multisampled framebuffer (renderbuffers), and
	// MainColorTexture / MainDepthStencil are its resolve, in MainResolveFramebuffer, brought up
	// to date (ResolveMain) before anything reads them: the copies the post-process samples,
	// the depth copy, the present pass, ReadPixels. Desktop GL and (F-3) OpenGL ES 3 / WebGL 2 through
	// GL4ES; OpenGL ES 2 / WebGL 1 have no multisampled framebuffers and get no anti-aliasing (the
	// FXAA that stood in for it there until F-3 was removed at the owner's word, 2026-10-09).
	INT								AntiAliasing;
	INT								MainSamples;			// 0: the main framebuffer is single-sampled
	GLuint							MainResolveFramebuffer,
									MainMSColorRenderbuffer,
									MainMSDepthRenderbuffer;
	INT								ResolvedColorVersion,
									ResolvedDepthVersion;
	void ResolveMain( UBOOL Color, UBOOL Depth );
	// The framebuffer that holds MainColorTexture / MainDepthStencil (the main one without MSAA).
	GLuint MainTextureFramebuffer() { return MainSamples ? MainResolveFramebuffer : MainFramebuffer; }

	// Render-target post-processing (M7; UC2GLPostProcess.cpp, docs/porting/analysis/postprocess.md).
	// UsePostProcess=False is exactly M6: no capability reported to the engine, the M7 pixel
	// shaders stay on their fallback, the interface answers what PC's D3DDrv answers.
	UBOOL							UsePostProcess,
									DOFShippedBlurAlpha;	// UUCScreenDOF: the blur alpha byte the shipped binary read (65) instead of script's
	// M7b (docs/porting/analysis/projectors.md): the Xbox shadow textures (FRenderCaps::XboxShadows:
	// caster in ShadowDarkness grey, PS_ShadowBlur). Reported only while the PS_Blur family is
	// drawn (post-processing on): PS_ShadowBlur shares its handler.
	UBOOL							UseXboxShadows;
	UBOOL XboxShadowsActive();
	// M7b: three terrain layers per pass through PS_Terrain3Layer (FRenderCaps::
	// MaxSimultaneousTerrainLayers = 3, the value the Xbox build hard-codes in UnTerrain.cpp).
	UBOOL							UseTerrain3Layer;
	UBOOL Terrain3LayerActive();
	// M7b: the Xbox's static projector batching (FRenderCaps::XboxProjectors: UGameEngine::LoadMap
	// re-packs level-static projectors, drawn with UProjectorMultiMaterial: SetProjectorMultiMaterial,
	// the 12 PS_Projector_*, ProjectorAtten.vsh). Read when a map loads.
	UBOOL							UseXboxProjectors;
	UBOOL XboxProjectorsActive();
	UBOOL ProjectorShadersReady();
	// M7b: InitPixelShaders has run since the context was made. The engine loads the command-line
	// map before the viewport (and so the GL context) exists and builds terrain passes and the
	// static projector batches from the caps then: until the shaders are known the caps say
	// what the switches ask for (both shaders compile everywhere: psh2glsl --gputest, GLSL ES too).
	UBOOL							ShaderStateKnown;
	void RefreshTerrainPasses();
	// What a shader gets when a material samples the default render target: GL cannot sample
	// the framebuffer it is drawing into, and the main framebuffer is stored bottom row first.
	// Snapshot = its colour, DepthColor = its depth as the Xbox saw it through D3DFMT_LIN_B8G8R8A8
	// (blue = most significant depth byte, green = middle, red = least); both top row first.
	GLuint							PostCopyProgram,
									PostDepthEncodeProgram,
									PostDepthCopyProgram;
	GLint							PostCopyRectLoc,
									PostDepthEncodeRectLoc,
									PostDepthCopyRectLoc;
	GLuint							SnapshotTexture,
									SnapshotFramebuffer,
									DepthColorTexture,
									DepthColorFramebuffer;
	INT								SnapshotVersion,		// MainColorVersion the snapshot holds
									DepthColorVersion,		// MainDepthVersion the depth colour holds
									MainColorVersion,		// bumped by every draw and clear into the main framebuffer
									MainDepthVersion;
	INT								SnapshotCopies,
									DepthColorCopies,
									DepthBufferCopies;
	// pp-scale (docs/porting/analysis/pp-scale.md): the post-processing works on a picture of
	// PostProcessReferenceHeight lines (ini, default 480 = the Xbox's; 0 = in back-buffer pixels,
	// as before; console PPREFERENCE). A pass that draws the back buffer into a smaller render
	// target (the first, downsampling pass of every blur chain) reads ReferenceTexture: the back
	// buffer brought down to that many lines by an area-averaging pass, so that the chains run
	// texel for texel as on the Xbox. Made only when the back buffer is taller than the reference.
	INT								PostProcessReferenceHeight;
	GLuint							PostReferenceProgram;	// built for PostReferenceTaps x PostReferenceTaps samples
	INT								PostReferenceTaps;
	GLint							PostReferenceRectLoc,
									PostReferenceStepLoc;
	GLuint							ReferenceTexture,
									ReferenceFramebuffer;
	INT								ReferenceWidth,
									ReferenceHeight,
									ReferenceVersion,		// MainColorVersion the reference copy holds
									ReferenceCopies;
	UBOOL							SampleReference;		// the pass being prepared reads the reference copy
	UBOOL ReferenceCopyActive();
	// Dynamic gamma (FD3DDynamicGamma's ramp): a 256x1 lookup the present pass applies.
	GLuint							GammaRampTexture;
	UBOOL							GammaRampActive,
									XboxClientDefaultsApplied;
	GLint							PresentRampLoc,
									PresentUseRampLoc;

	// Present pass.
	GLuint							PresentProgram;
	GLint							PresentTextureLoc,
									PresentGammaLoc;
	FLOAT							PresentGamma,
									PresentBrightness,
									PresentContrast;
	UBOOL							ApplyGamma;

	GLuint							NullTextureID;

	// Programmable materials (M6; UC2GLShaders.cpp).
	UBOOL							UsePixelShaders,
									PrecompileVertexShaders;
	BYTE							PixelShaderState[PS_MAX];	// EPixelShaderState
	TMap<QWORD,GLuint>				VertexShaderObjects;		// by VertexShaderEnum; 0 = cannot be built
	TMap<DWORD,GLuint>				FragmentShaderObjects;		// by EPixelShader | variant << 8
	TMap<QWORD,FOpenGLProgram*>		Programs;					// hash chains
	INT								ProgramsLinked, ProgramsFailed,
									VertexShadersCompiled, VertexShadersFailed, VertexShadersPrecompiled,
									FragmentShadersCompiled, FragmentShadersFailed,
									PrecompileSeen, PrecompileNotApplicable,
									ProgramDraws, ProgramFallbackDraws;

	// O-9 (UC2GLFFProgram.cpp): the texture stages of fixed-function passes as GLSL instead
	// of GL's texture environment. UseFixedFunctionPrograms is the ini switch
	// (FixedFunctionPrograms; -glffprograms / -glnoffprograms; console FFPROGRAMS [0|1]);
	// FFProgramsActive says whether they are in use (desktop GL only). The stage count and
	// the texture-op caps the material compiler sees follow from it (UpdateTextureCaps).
	UBOOL							UseFixedFunctionPrograms,
									FFProgramsActive,
									LogFFPrograms,			// -glffsource: log every generated program
									MaskCombine3,			// -glnocombine3: as if GL_ATI_texture_env_combine3 were missing
									MaskCombine4;			// -glnocombine4: as if GL_NV_texture_env_combine4 were missing
	INT								GLFixedFunctionUnits,	// GL_MAX_TEXTURE_UNITS: units of the texture environment
									GLProgramUnits;			// min(GL_MAX_TEXTURE_COORDS, GL_MAX_TEXTURE_IMAGE_UNITS): units a fragment shader can sample with fixed-function texture coordinates
	INT								FFProgramsLinked, FFProgramsFailed, FFProgramDraws, FFFallbackDraws;
	void UpdateTextureCaps();
	UBOOL HasCombine3() { return SUPPORTS_GL_ATI_texture_env_combine3 && !MaskCombine3; }
	UBOOL HasCombine4() { return SUPPORTS_GL_NV_texture_env_combine4 && !MaskCombine4; }

	// GL error checking (CheckGLErrors=True or -glerrors).
	INT								GLErrorFrames,
									GLErrorCount;

	TArray<BYTE>					ScratchBuffer;

	// GL entry points.
	#define GL_EXT(name) static UBOOL SUPPORTS##name;
	#define GL_PROC(ext,ret,func,parms) static ret (APIENTRY *func)parms;
	#include "UC2GLFuncs.h"
	#undef GL_EXT
	#undef GL_PROC
	// The library under the binding (UC2GLRawFuncs.h): the same functions on desktop GL,
	// OpenGL ES 2 / WebGL itself under GL4ES.
	#define GL_RAW(ret,func,parms) static ret (APIENTRY *Raw_##func)parms;
	#include "UC2GLRawFuncs.h"
	#undef GL_RAW

	// The GL binding (UC2GLBinding.h; M11). This class binds desktop OpenGL through SDL;
	// UC2GL4ESDrv.UC2GL4ESRenderDevice overrides the Binding* virtuals to bind OpenGL ES 2 /
	// WebGL 1 through GL4ES. Everything else is the same object code for both.
	UBOOL							IsGLES;				// the context is OpenGL ES (behind GL4ES)
	// F-3: the OpenGL ES major version behind GL4ES: 3 (OpenGL ES 3.0 / WebGL 2: depth textures,
	// multisampled framebuffers, gl_FragDepth and fragment highp in core; GL4ES emits GLSL ES 3.00)
	// or 2 (OpenGL ES 2 / WebGL 1: each of those an extension, with the fallbacks). 0 on desktop GL.
	INT								GLESMajor;
	// The depth-stencil image is attached to GL_DEPTH_STENCIL_ATTACHMENT (WebGL 1, OpenGL ES 3 /
	// WebGL 2) rather than to the depth and the stencil points (OpenGL ES 2 has no such point).
	UBOOL UseDepthStencilAttachmentPoint()
	{
#ifdef __EMSCRIPTEN__
		return 1;
#else
		return GLESMajor >= 3;
#endif
	}
	// What the context can do; all true on desktop GL. Each has its fallback, and a switch
	// that masks it so the fallback can be tested where the capability exists
	// (-glnodepthtexture, -glnofragdepth, -glnos3tc, -glnouintindex).
	UBOOL							CapDepthTexture,	// a depth-stencil texture can be a framebuffer attachment and be sampled
									CapFragDepth,		// a fragment shader can write gl_FragDepth
									CapUIntIndex,		// 32-bit index buffers
									CapTextureMaxLevel,	// GL_TEXTURE_MAX_LEVEL: a mip chain may stop before 1x1
									CapFragmentHighp,	// highp float in fragment shaders
									CapRawS3TC;			// GLES: the hardware takes DXT data (gl4es itself always decodes it in software)
	// A compressed image for a texture, handed to the library under the binding (GLES: gl4es
	// would decode it to RGBA8 on the CPU). BindTarget is GL_TEXTURE_2D or GL_TEXTURE_CUBE_MAP.
	void CompressedTexImage2D( GLenum BindTarget, GLuint Texture, GLenum Target, INT Level, GLenum Format, INT Width, INT Height, INT Size, const BYTE* Data, UBOOL FirstStorage );
	UBOOL UseHardwareS3TC() { return IsGLES ? CapRawS3TC : SUPPORTS_GL_EXT_texture_compression_s3tc; }
	// The same for uncompressed data of an engine texture (Format GL_BGRA, GL_LUMINANCE,
	// GL_ALPHA or GL_RGB with 5-6-5; tightly packed rows).
	void TexImage2D( GLenum BindTarget, GLuint Texture, GLenum Target, INT Level, GLint Internal, INT Width, INT Height, GLenum Format, GLenum Type, const BYTE* Data );
	virtual const TCHAR* BindingName();
	// SDL_GL_SetAttribute for the context this binding wants; before the window is (re)created.
	virtual void BindingSetContextAttributes();
	// The viewport's window with a current context of this binding.
	virtual UBOOL BindingCreateContext( UViewport* Viewport, UBOOL Fullscreen, INT NewX, INT NewY, UBOOL bSaveSize );
	// Once, with the context current, before the pointer table is filled.
	virtual UBOOL BindingInit();
	virtual void* BindingGetProcAddress( const char* Name );
	virtual void* BindingGetRawProcAddress( const char* Name );
	virtual UBOOL BindingContextAlive();
	virtual void BindingGetDrawableSize( UViewport* Viewport, INT& Width, INT& Height );
	virtual void BindingSetSwapInterval( INT Interval );
	virtual void BindingSwap( UViewport* Viewport );
	// The hardware's extension string (GLES: not gl4es' emulated one), space separated.
	UBOOL FindRawExt( const char* Name );
	// The first lines of every GLSL source this driver compiles.
	const ANSICHAR* ShaderPrologue( UBOOL Fragment );

	// Depth-stencil storage for framebuffers, and its attachment to the framebuffer bound to
	// GL_FRAMEBUFFER (UC2GLRenderDevice.cpp; around gl4es on GLES).
	GLuint CreateDepthStencilTexture( INT Width, INT Height );
	GLuint CreateDepthStencilRenderbuffer( INT Width, INT Height );
	void DeleteDepthStencilRenderbuffer( GLuint& Renderbuffer );
	void AttachDepthStencilTexture( GLuint Framebuffer, GLuint Texture );
	void AttachDepthStencilRenderbuffer( GLuint Framebuffer, GLuint Renderbuffer );
	void AttachMainDepthStencil( GLuint Framebuffer );
	GLint RawBindFramebuffer( GLuint Framebuffer );
	// A 2D texture as colour attachment 0 of the framebuffer bound to GL_FRAMEBUFFER.
	void AttachColorTexture( GLuint Texture );
	// Verification (UC2GLSelfTest.cpp): writes the main depth buffer from D24S8 words, bottom
	// row first. Returns 0 where the context cannot do that (no depth texture).
	UBOOL UploadMainDepth( const DWORD* Rows, INT Width, INT Height );
	void DetectCapabilities();
	void PrepareGL4ESTextureZero();
	// A static full-screen strip (-1..1) in a buffer object: WebGL has no client-side arrays.
	GLuint							QuadBuffer;
	// Buffer object names. On GLES a freed name is kept and handed out again instead of
	// being deleted (NewBuffer / FreeBuffer in UC2GLRenderDevice.cpp say why).
	TArray<GLuint>					FreeArrayBuffers, FreeElementBuffers;
	GLuint NewBuffer( GLenum Target );
	void FreeBuffer( GLenum Target, GLuint& Buffer );
	// -glnolodbias: every texture LOD bias is 0 (verification: until F-3 GL4ES ignored the LOD
	// bias of the fixed-function path, and this was what the desktop was compared with; our fork
	// applies it since, and the switch shows the difference it makes).
	UBOOL							NoLODBias;
	// -glerrorsdraw: glGetError after every draw call and texture upload, with what it was (slow; to find the call behind an error -glerrors reports).
	UBOOL							CheckGLErrorsPerDraw;
	// -glstats: one log line every 300 frames (M11, G5): frame time and what a frame costs
	// in calls the driver makes (per frame averages over the interval).
	UBOOL							LogStats;
	INT								StatDraws,			// draw calls (passes), all
									StatPrimitives,
									StatMaterials,		// passes whose full material state was (re)applied
									StatBufferUploads, StatBufferBytes,
									StatTextureUploads,
									StatPostQuads,		// the device's own full-target quads (snapshots, depth copies)
									StatSnapshots, StatDepthColors, StatDepthCopies,
									StatFrame0;
	DOUBLE							StatTime0;
	void LogFrameStats();
	void BindQuadBuffer();

	// Constructors.
	UUC2GLRenderDevice();
	void StaticConstructor();
	virtual void Destroy();

	// Helpers.
	UBOOL FindExt( const char* Name );
	void FindProc( void*& ProcAddress, const char* Name, const char* SupportName, UBOOL& Supports );
	void FindProcs();
	UBOOL CheckGLError( const TCHAR* Tag, UBOOL Log=1 );
	// The viewport owns the context and closes its window before UViewport::Destroy reaches
	// the render device: GL objects are deleted only while a context is current.
	UBOOL ContextAlive();

	// Resource caching (UC2's scheme).
	FOpenGLResource* GetCachedResource(_WORD CacheId,ECacheHint CacheHint);
	void CacheResource(FOpenGLResource* Resource,ECacheHint CacheHint);
	void FlushResource(FOpenGLResource* Resource);
	FOpenGLVertexShader* GetVertexShader(QWORD VertexShaderEnum,FShaderDeclaration& Declaration);

	// Main framebuffer and present pass.
	UBOOL CreateMainFramebuffer( INT Width, INT Height );
	void DestroyMainFramebuffer();
	UBOOL CreatePresentProgram();
	void DrawPresentPass( UViewport* Viewport );
	void InitGLState();

	// Post-processing (UC2GLPostProcess.cpp).
	// The one condition for everything M7 added: the ini switch, pixel shaders (the
	// post-process materials are pixel-shader materials) and the copy programs.
	FORCEINLINE UBOOL PostProcessActive() { return UsePostProcess && UsePixelShaders && PostCopyProgram; }
	UBOOL InitPostProcess();
	void ExitPostProcess();
	void DestroyPostProcessTargets();
	void SetPostProcess( UBOOL Enable );
	void DrawPostQuad( GLuint Program, GLint RectLoc, GLuint SourceTexture, GLuint DestFramebuffer, INT DestWidth, INT DestHeight, const FLOAT* Rect, UBOOL DepthOnly );
	GLuint GetSampledTexture( FOpenGLTexture* Texture );
	void UpdateSampledTexture( FOpenGLTexture* Texture );
	void SetGammaRamp( const BYTE* Ramp );

	// Programmable materials.
	void InitPixelShaders();
	void ExitPixelShaders();
	void LogPixelShaderStats();
	UBOOL PixelShaderFamilyReady( INT Type );
	FString BuildVertexShaderSource( QWORD VertexShaderEnum );
	GLuint GetVertexShaderObject( QWORD VertexShaderEnum );
	GLuint GetFragmentShaderObject( INT PixelShader, DWORD Variant );
	FOpenGLProgram* GetProgram( INT PixelShader, DWORD Variant, QWORD VertexShaderEnum );

	// URenderDevice interface.
	virtual UBOOL Init();
	virtual UBOOL SetRes( UViewport* Viewport, INT NewX, INT NewY, UBOOL Fullscreen, INT ColorBytes=0, UBOOL bSaveSize=true );
	virtual void Exit( UViewport* Viewport );
	virtual void Flush( UViewport* Viewport );
	virtual void FlushResource( _WORD CacheId, ECacheHint CacheHint );
	virtual UBOOL ResourceCached( _WORD CacheId, ECacheHint CacheHint );
	virtual void ResetRenderStates(void);
	virtual void UpdateGamma( UViewport* Viewport );
	virtual void RestoreGamma();
	virtual UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar );
	virtual FRenderInterface* Lock( UViewport* Viewport, BYTE* HitData, INT* HitSize );
	virtual void Unlock( FRenderInterface* RI );
	virtual void Present( UViewport* Viewport );
	virtual void ReadPixels( UViewport* Viewport, FColor* Pixels );
	virtual void SetEmulationMode( EHardwareEmulationMode Mode );
	virtual FRenderCaps* GetRenderCaps();
	virtual void LoadVertexShaderEnum( QWORD VertexShaderEnum );
};

#endif
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
