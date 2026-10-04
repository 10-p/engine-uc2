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
					ColorRemapLoc;			// M7: VSC_COLOR_REMAP
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
									MaxTextureUnits;
	FLOAT							DetailTexMipBias,
									DefaultTexMipBias;

	// Status.
	FRenderCaps						RenderCaps;
	FOpenGLDeviceCaps				DeviceCaps;
	UViewport*						LockedViewport;
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
									MainDepthStencil;
	INT								MainWidth,
									MainHeight;

	// Render-target post-processing (M7; UC2GLPostProcess.cpp, docs/porting/analysis/postprocess.md).
	// UsePostProcess=False is exactly M6: no capability reported to the engine, the M7 pixel
	// shaders stay on their fallback, the interface answers what PC's D3DDrv answers.
	UBOOL							UsePostProcess,
									DOFShippedBlurAlpha;	// UUCScreenDOF: the blur alpha byte the shipped binary read (65) instead of script's
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
	// Dynamic gamma (FD3DDynamicGamma's ramp): a 256x1 lookup the present pass applies.
	GLuint							GammaRampTexture;
	UBOOL							GammaRampActive;
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
