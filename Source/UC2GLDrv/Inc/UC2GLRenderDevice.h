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

	// The main framebuffer: the default render target.
	GLuint							MainFramebuffer,
									MainColorTexture,
									MainDepthStencil;
	INT								MainWidth,
									MainHeight;

	// Present pass.
	GLuint							PresentProgram;
	GLint							PresentTextureLoc,
									PresentGammaLoc;
	FLOAT							PresentGamma,
									PresentBrightness,
									PresentContrast;
	UBOOL							ApplyGamma;

	GLuint							NullTextureID;

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
