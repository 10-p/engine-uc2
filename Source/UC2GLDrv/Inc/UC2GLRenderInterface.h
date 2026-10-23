/*=============================================================================
	UC2GLRenderInterface.h: UC2GLDrv's FRenderInterface.

	Structure: saved-state stack, material state pool, deferred lights/matrices, commit in
	DrawPrimitive. Interface and behaviour: UC2's D3DDrv/D3DRenderInterface.h (UC2's
	FRenderInterface, separate viewport/render-target/stencil stacks, per-material colour
	write mask, blend op, filters, PostProcessStage, light type counting).
=============================================================================*/

#ifndef _INC_UC2GLRENDERINTERFACE
#define _INC_UC2GLRENDERINTERFACE

class UUC2GLRenderDevice;

// Test key MaterialTime=<seconds> (read in UUC2GLRenderDevice::Init): the time the driver hands to
// materials (texture panners, rotators and oscillators, constant colours, PreSetMaterial) instead of the
// level's TimeSeconds, so that a picture of a moving material is the same in every run (the Liandri
// overlay's panned noise). Below 0 (the default): the level's time.
extern FLOAT GTestMaterialTime;
inline FLOAT UC2GLMaterialTime( const ALevelInfo* Level )
{
	return GTestMaterialTime >= 0.f ? GTestMaterialTime : Level->TimeSeconds;
}

//
//	FOpenGLModifierInfo: UC2's FD3DModifierInfo.
//
struct FOpenGLModifierInfo
{
	// color modifier
	FColor				TFactorColor;
	// opacity modifier
	UMaterial*			Opacity;
	// texture modifier
	FMatrix				Matrix;
	BYTE				TexCoordSource;
	BYTE				TexCoordCount;
	BYTE				TexCoordProjected;
	BYTE				AlphaRef;
	BYTE				FrameBufferBlending;
	BITFIELD			ModifyTextureTransforms:1;
	BITFIELD			ModifyFramebufferBlending:1;
	BITFIELD			ModifyColor:1;
	BITFIELD			ModifyOpacity:1;
	BITFIELD			ModifyVertexOpacity:1;
	// framebuffer blend modifier
	BITFIELD			ZWrite:1;
	BITFIELD			ZTest:1;
	BITFIELD			AlphaTest:1;
	BITFIELD			TwoSided:1;
	BITFIELD			AlphaEnable:1;
	BITFIELD			OverrideAlpha:1;
	BITFIELD			AlphaBlend:1;
	BITFIELD			OpacityOverrideTexModifier:1;
	BITFIELD			ColorWriteEnable:1;
	BITFIELD			AlphaWriteEnable:1;

	FOpenGLModifierInfo();

	void SetDetailTextureScale( FLOAT Scale );
};

//
//	FOpenGLMaterialStateStage: UC2's FD3DMaterialStateStage. One texture stage in D3D8
//	vocabulary (UC2GLD3DTypes.h); translated to GL when the pass is committed.
//
struct FOpenGLMaterialStateStage
{
	FOpenGLTexture*		Texture;
	D3DTEXTUREADDRESS	TextureAddressU,
						TextureAddressV,
						TextureAddressW;
	FLOAT				TextureMipLODBias;

	D3DTEXTUREOP		ColorOp,
						AlphaOp;
	DWORD				ColorArg0,
						ColorArg1,
						ColorArg2,
						AlphaArg0,
						AlphaArg1,
						AlphaArg2,
						ResultArg;

	DWORD				MinFilter,
						MagFilter,
						MipFilter;

	DWORD				TexCoordIndex;
	DWORD				TexCoordCount;
	UBOOL				TextureTransformsEnabled;
	D3DMATRIX			TextureTransformMatrix;

	FOpenGLMaterialStateStage();
};

#define MAX_STAGES 8

//
//	FOpenGLMaterialState: UC2's FD3DMaterialState.
//
class FOpenGLMaterialState
{
public:
	EPixelShader		PixelShader;
	BYTE				AlphaBlending;
	BYTE				AlphaTest;
	BYTE				ZTest;
	BYTE				ZWrite;
	BYTE				TwoSided;
	BYTE				OverrideFogColor;
	BYTE				SpecularEnable;
	// PS_Blur's taps widened in the shader (UC2_BLUR_FILL_* of UPSBlurMaterial::BlurBlendMode, set by
	// SetMatState_PS_Blur; 0 otherwise): the fill variant of the program (UC2GLShaders.cpp).
	BYTE				BlurFill;

	BYTE				AlphaRef;
	EFillMode			FillMode;
	D3DCOLOR			TFactorColor;
	D3DBLEND			SrcBlend,
						DestBlend;
	D3DBLENDOP			BlendOp;
	FColor				OverriddenFogColor;
	DWORD				ColorWriteMask;
	QWORD				MaterialVertexShaderEnum;	// the unique vertex shader a material asks for
	BYTE				UsePixelShaderProgram;		// UC2GLDrv: the pass was set up for its pixel shader (SetPixelShaderMaterial's real branch)

	BYTE					StagesUsed;
	FOpenGLMaterialStateStage	Stages[MAX_STAGES];

	INT	NumRefs;

	FOpenGLMaterialState();
};

//
//	FOpenGLMaterialStatePool
//
class FOpenGLMaterialStatePool
{
public:
	TArray<FOpenGLMaterialState*>	FreeStates;

	~FOpenGLMaterialStatePool()
	{
		for(INT StateIndex = 0;StateIndex < FreeStates.Num();StateIndex++)
			delete FreeStates(StateIndex);
	}

	FOpenGLMaterialState* AllocateState(FOpenGLMaterialState* DefaultState)
	{
		FOpenGLMaterialState* Result;
		if(FreeStates.Num())
			Result = FreeStates.Pop();
		else
			Result = new FOpenGLMaterialState();
		if(DefaultState)
			appMemcpy(Result,DefaultState,sizeof(FOpenGLMaterialState));
		Result->NumRefs = 1;
		return Result;
	}

	void FreeState(FOpenGLMaterialState* State)
	{
		FreeStates.AddItem(State);
	}
};

#define MAX_MATERIAL_PASSES	8
#define MAX_STATESTACKDEPTH	32

//
//	The stacks UC2 keeps outside the saved state (D3DRenderInterface.h): render target,
//	viewport and stencil state are pushed/popped by their own Set/Reset calls.
//
struct FOpenGLRenderTargetState
{
	FRenderTarget*	RenderTarget;		// NULL means the default render target
	GLuint			Framebuffer;
	INT				Width,
					Height;
	UBOOL			Flipped;			// rendering upside down so the texture reads top-down, as D3D's

	FOpenGLRenderTargetState() : RenderTarget(NULL), Framebuffer(0), Width(0), Height(0), Flipped(0) {}
};

struct FOpenGLViewportState
{
	INT		ViewportX,
			ViewportY,
			ViewportWidth,
			ViewportHeight;

	FOpenGLViewportState() : ViewportX(0), ViewportY(0), ViewportWidth(640), ViewportHeight(480) {}
};

struct FOpenGLStencilState
{
	ECompareFunction	StencilTest;
	EStencilOp			StencilFailOp,
						StencilZFailOp,
						StencilPassOp;
	DWORD				StencilRef,
						StencilMask,
						StencilWriteMask;

	FOpenGLStencilState()
	{
		StencilTest			= CF_Always;
		StencilFailOp		= SO_Keep;
		StencilZFailOp		= SO_Keep;
		StencilPassOp		= SO_Keep;
		StencilRef			= 0;
		StencilMask			= 0xffffffff;
		StencilWriteMask	= 0xffffffff;
	}
};

template<class T> class TOpenGLStateStack
{
	T		States[MAX_STATESTACKDEPTH];
	INT		StateIndex;
public:
	TOpenGLStateStack() : StateIndex(0) {}
	FORCEINLINE T* GetCurrentState() { return &States[StateIndex]; }
	FORCEINLINE INT GetStateIndex() { return StateIndex; }
	FORCEINLINE T* Push()
	{
		check(StateIndex+1 < MAX_STATESTACKDEPTH);
		States[StateIndex+1] = States[StateIndex];
		return &States[++StateIndex];
	}
	FORCEINLINE void Pop()
	{
		check(StateIndex > 0);
		StateIndex--;
	}
	FORCEINLINE void Reset() { StateIndex = 0; }
};

//
//	A fixed-function light as the D3D driver describes it (D3DLIGHT8), committed to GL.
//
struct FOpenGLLight
{
	UBOOL			Enabled;
	D3DLIGHTTYPE	Type;
	FVector			Position,
					Direction;
	FPlane			Diffuse;
	FLOAT			Attenuation0,
					Attenuation1,
					Attenuation2,
					Range,
					Phi,
					Theta,
					Falloff;
};

//
//	FOpenGLRenderInterface
//
class FOpenGLRenderInterface : public FRenderInterface
{
public:

	//
	//	FOpenGLSavedState: UC2's FD3DSavedState plus what the GL commit needs.
	//
	class FOpenGLSavedState
	{
	public:
		DWORD					ColorWriteMask;

		FMatrix					LocalToWorld,
								WorldToCamera,
								CameraToScreen;

		QWORD					VertexShaderEnum;
		FOpenGLVertexShader*	VertexShader;

		FOpenGLVertexStream*	Streams[16];
		INT						StreamStrides[16];
		INT						NumStreams;
		// The buffer names the streams and the index buffer had when set: a dynamic buffer
		// moves to another name with every upload.
		GLuint					StreamBuffers[16];

		BYTE					ForceDefSphereMap;

		FOpenGLIndexBuffer*		IndexBuffer;
		INT						IndexBufferBase;
		GLuint					IndexBufferID;

		ECullMode				CullMode;

		FOpenGLMaterialState*	MaterialPasses[MAX_MATERIAL_PASSES];
		FOpenGLMaterialState*	CurrentMaterialState;
		INT						NumMaterialPasses;

		INT						ZBias;
		BYTE					UseDetailTexturing;
		BYTE					UseDynamicLighting;
		BYTE					UseStaticLighting;
		BYTE					LightingModulate2X;
		BYTE					LightingOnly;
		BYTE					DistanceFogEnabled;

		FOpenGLTexture*			Lightmap;
		FSphere					LitSphere;
		FColor					AmbientLightColor;

		FLOAT					DistanceFogStart,
								DistanceFogEnd;
		FColor					DistanceFogColor;

		UMaterial*				LastMaterial;

		FOpenGLSavedState();
	};

	UUC2GLRenderDevice*		RenDev;
	UViewport*				Viewport;

	FOpenGLSavedState		SavedStates[MAX_STATESTACKDEPTH];
	FOpenGLSavedState*		CurrentState;
	FOpenGLMaterialState	DefaultPass;
	INT						SavedStateIndex;

	// scion sz - current # of lights of each type (UC2 counts them to stay within its limits)
	INT						LightTypeTotals[D3DLIGHT_DIRECTIONAL];

	TOpenGLStateStack<FOpenGLStencilState>		SavedStencilState;
	TOpenGLStateStack<FOpenGLViewportState>		SavedViewportState;
	TOpenGLStateStack<FOpenGLRenderTargetState>	SavedRenderTargetState;

	FOpenGLMaterialStatePool	MaterialStatePool;
	EPrecacheMode			PrecacheMode;

	// Deferred state (the D3D driver's FD3DDeferredState role).
	FOpenGLLight			Lights[8];
	FColor					AmbientColor;		// RS_AMBIENT: the saved ambient plus LE_Ambient lights
	UBOOL					LightsDirty,
							MatricesDirty;
	UBOOL					StencilEnabled,
							DepthEnabled;

	FRenderTarget*			BackBufferCopyTarget;
	FRenderTargetPool		RenderTargetPool;
	EPostProcessStage		PostProcessStage;

	// Render-target post-processing (UC2GLPostProcess.cpp).
	class FOpenGLDynamicGamma*	DynamicGamma;			// FD3DDynamicGamma (the window's viewport)
	TArray<class FOpenGLDynamicGamma*> ViewportDynamicGammas;	// One per split-screen viewport
	class FOpenGLDynamicGamma*	CurrentDynamicGamma;	// The one of the viewport being drawn
	FLOAT					DistortionScale[4];			// VSC_DISTORTION_SCALE (HandlePSVertexShader)
	FLOAT					ColorRemapConstant[4];		// VSC_COLOR_REMAP
	// VSC_PROJ_DIRECTIONS .. VSC_PROJ_LIFESPAN_ATTEN, 13 registers (SetProjectorMultiMaterial;
	// ProjectorAtten.vsh, TexSourceTexKill3.vsh): directions 0-2, origins 3-5, back faces 6,
	// inverse trace distances 7, clip planes 8-11, lifespan 12, all in the mesh's local space.
	FLOAT					ProjectorShaderConstants[13][4];
	UBOOL					PointSampleNudge;			// see GetGLMatrices
	// A context without gl_FragDepth (EXT_frag_depth): CopyDepthBuffer cannot write the
	// scene's depth into the distortion accumulation target, so the program passes drawn into
	// that target this frame test the scene's depth in their fragment shader instead
	// (PSV_SceneDepthTest, UC2GLShaders.cpp). The framebuffer is that target's; the rectangle
	// maps gl_FragCoord.xy to the main depth texture: uv = xy * Rect.xy + Rect.zw.
	GLuint					SceneDepthTestFramebuffer;
	FLOAT					SceneDepthTestRect[4];
	// Between BeginOcclusionQuery and EndOcclusionQuery nothing is drawn: the engine's
	// query geometry writes neither colour nor depth nor stencil, and there is no query to count for.
	UBOOL					InOcclusionQuery;
	GLuint					WireIndexBuffer;			// GLES: wireframe passes drawn as lines (DrawWireframe)
	UBOOL DrawWireframe( GLenum Type, INT FirstIndex, INT Count );

	// Hit testing (the editor's clicks: a frame locked with hit data, never presented). As D3DDrv does it: PushHit
	// fills the viewport's hit rectangle with a colour no material draws, PopHit reads the rectangle back and
	// the proxy was hit when something else is there. A nested proxy's pixels do not count for its parent, so
	// PopHit fills the rectangle again and the parent remembers what it had drawn before the nested push.
	struct FHitFrame
	{
		INT		Count;			// bytes of the proxy on HitStack
		UBOOL	DrewBefore;		// the proxy drew in the rectangle before a nested proxy was pushed
		FColor	DrewColor;
	};
	BYTE*					HitData;
	INT*					HitSize;
	INT						HitCount;
	TArray<BYTE>			HitStack;
	TArray<FHitFrame>		HitFrames;
	// The hit rectangle in the current render target's GL window coordinates; 0 when it is empty.
	UBOOL GetHitRect( INT& X, INT& Y, INT& W, INT& H );
	// The first pixel of the rectangle that is not the fill colour, if any.
	UBOOL ReadHitRect( INT X, INT Y, INT W, INT H, FColor& Found );
	void FillHitRect( INT X, INT Y, INT W, INT H );

	FOpenGLRenderInterface(UUC2GLRenderDevice* InRenDev);
	~FOpenGLRenderInterface();

	FORCEINLINE INT GetLightTypeCount( D3DLIGHTTYPE LightType ) { return LightTypeTotals[(INT)LightType-1]; }
	FORCEINLINE void IncrementLightType( D3DLIGHTTYPE LightType ) { LightTypeTotals[(INT)LightType-1]++; }

	// FRenderInterface interface.
	virtual void PushState();
	virtual void PopState();

	virtual UBOOL SetRenderTarget(FRenderTarget* InRenderTarget);
	virtual void ResetRenderTarget(void);
	virtual FRenderTarget* GetRenderTarget();
	virtual UBOOL IsDefaultRenderTarget(void);
	virtual FRenderTargetPool* GetRenderTargetPool() { return &RenderTargetPool; }
	virtual void SetViewport(INT X,INT Y,INT Width,INT Height);
	virtual void SetScreenSpaceOffset( FLOAT OffsetX, FLOAT OffsetY );
	virtual void ResetViewport(void);
	virtual void GetViewport(INT* X,INT* Y,INT* Width,INT* Height);
	virtual UViewport* GetCurViewportPtr() { return Viewport; }
	virtual void Clear(UBOOL UseColor,FColor Color,UBOOL UseDepth,FLOAT Depth,UBOOL UseStencil,DWORD Stencil);
	virtual void SetColorWriteMask( DWORD ColorMask );
	virtual void SetRenderStateDirect(BYTE State,DWORD Value);
	virtual void SetVertexShaderConstant(INT Register,const void* pConstantData,DWORD ConstantCount);

	virtual void PushHit(const BYTE* Data,INT Count);
	virtual void PopHit(INT Count,UBOOL Force);

	virtual void SetCullMode(ECullMode CullMode);
	virtual ECullMode GetCullMode();

	virtual void SetAmbientLight(FColor Color);
	virtual void EnableLighting(UBOOL UseDynamic, UBOOL UseStatic, UBOOL Modulate2X, FBaseTexture* LightmapTexture, UBOOL LightingOnly, const FSphere& LitSphere );
	virtual void SetLight(INT LightIndex,FDynamicLight* Light,FLOAT Scale);

	virtual void SetNPatchTesselation( FLOAT Tesselation );
	virtual void SetDistanceFog(UBOOL Enable,FLOAT FogStart,FLOAT FogEnd,FColor Color);
	virtual void SetGlobalColor(FColor Color);
	virtual void SetTransform(ETransformType Type,const FMatrix& Matrix);

	virtual INT SetMaterial(UMaterial* Material, FString* ErrorString, UMaterial** ErrorMaterial, INT* NumPasses);
	virtual UMaterial* GetLastMaterial();
	virtual DWORD GetMaterialVertexShaderType(UMaterial* Material);

	virtual void SetStencilOp(ECompareFunction Test,DWORD Ref,DWORD Mask,EStencilOp FailOp,EStencilOp ZFailOp,EStencilOp PassOp,DWORD WriteMask);
	virtual void ResetStencilOp(void);
	virtual void EnableStencil(UBOOL bEnable);
	virtual void EnableDepth(UBOOL bEnable);

	virtual void SetPrecacheMode( EPrecacheMode PrecacheMode );
	virtual EPrecacheMode GetPrecacheMode(void);
	virtual void SetZBias(INT ZBias);
	void ApplyZBias( INT ZBias );		// The engine's ZBias (0..16) as glPolygonOffset, the Xbox's slope scale

	virtual void AddVertexShaderEnum( QWORD VertexShaderEnum );
	virtual INT SetVertexStreams(QWORD VertexShaderEnum,FVertexStream** Streams,INT NumStreams);
	virtual INT SetDynamicStream(QWORD VertexShaderEnum,FVertexStream* Stream);
	virtual INT SetIndexBuffer(FIndexBuffer* IndexBuffer,INT BaseVertexIndex);
	virtual INT SetDynamicIndexBuffer(FIndexBuffer* IndexBuffer,INT BaseVertexIndex);

	virtual void DrawPrimitive(EPrimitiveType PrimitiveType,INT FirstIndex,INT NumPrimitives,INT MinIndex,INT MaxIndex);

	virtual DWORD GetStageCountForPass(INT Pass);

	// Post-processing (UC2GLPostProcess.cpp): the Xbox bodies of FD3DRenderInterface when the
	// device has UsePostProcess, what PC's D3DDrv returns (nothing) when it has not.
	virtual UMaterial* GetBackBufferCopyMaterial();
	virtual FRenderTarget* GetBackBufferCopyTarget();
	virtual void SetBackBufferCopyTarget(FRenderTarget* RenderTarget) { BackBufferCopyTarget = RenderTarget; }
	virtual void UpdateBackBufferCopyTarget(UViewport* InViewport=NULL);
	virtual UMaterial* GetBackBufferMaterial();
	virtual void CopyDepthBuffer( FRenderTarget* DestTarget, FRenderTarget* SrcTarget );
	virtual void SetPostProcessStage( EPostProcessStage InStage ) { PostProcessStage = InStage; }
	virtual EPostProcessStage GetPostProcessStage() { return PostProcessStage; }
	virtual void ForceDefSphereMappingMode( UBOOL bEnable ) { CurrentState->ForceDefSphereMap = bEnable; }
	virtual void UpdateDynamicGamma();
	virtual FLOAT GetAverageSceneLuminosity();
	virtual void SetSkinBones( INT FirstBoneDestIndex, FCoords* FirstBone, INT BoneCount );
	virtual void PrecacheMaterial(UMaterial* Material);
	virtual void UpdateDOFRampLookupTexture( UMaterial* DOFShader, BYTE FocusType );
	virtual void SetTerrainTransform( FMatrix& WorldMat, INT HeightMapX, INT HeightMapY );

	virtual UBOOL BeginOcclusionQuery(void);
	virtual UBOOL EndOcclusionQuery(DWORD QueryNum);
	virtual UBOOL GetOcclusionQueryResults(DWORD QueryNum,DWORD& dwPixels);
	virtual FLOAT GetPixelAspectRatio();

	// FOpenGLRenderInterface interface.
	void Locked( UViewport* InViewport, BYTE* InHitData, INT* InHitSize );
	void Unlocked();
	void InvalidateHardwareState();
	// After the device drew something of its own (a copy pass) while the interface is locked:
	// framebuffer, viewport and the enables the passes do not set come back.
	void RestoreAfterDeviceDraw();
	// Before a pass is committed: bring the copies up to date that stand in for the default
	// render target when a stage samples it (UUC2GLRenderDevice::UpdateSampledTexture).
	void PrepareSampledTargets( FOpenGLMaterialState* MaterialState );

	FOpenGLTexture* CacheTexture(FBaseTexture* Texture);

	// Pixel shader constants the PS material handlers set (D3D's SetPixelShaderConstant).
	// Uniforms u_c0..7 of the GLSL translations of the .psh shaders (UC2GLShaders.cpp).
	FLOAT					PixelShaderConstants[8][4];
	void SetPixelShaderConstant( INT Register, const void* Data, INT Count );

private:
	friend struct FUC2GLCombinerTest;	// The numeric test of the texture stages (UC2GLCombinerTest.cpp)
	void SetMaterialBlending( FOpenGLMaterialState* NewMaterialState, ECullMode CullMode );
	void ApplyCullMode( ECullMode CullMode );
	void ApplyViewport();
	void ApplyStencilState();
	void CommitLights();
	void CommitMatrices();
	void CommitStreams( INT FirstIndex );
	void CommitTexCoords( INT Pass );

	// Programmable materials (UC2GLShaders.cpp).
	void GetGLMatrices( FMatrix& ModelView, FMatrix& Projection );
	QWORD GetStateVertexShaderEnum( FOpenGLMaterialState* MaterialState, UBOOL HasDiffuse, UBOOL HasSpecular );
	UBOOL BeginProgramPass( INT Pass, INT FirstIndex );
	void EndProgramPass();
	// A fixed-function pass's texture stages as a fragment program (UC2GLFFProgram.cpp).
	UBOOL BeginFixedFunctionProgram( FOpenGLMaterialState* MaterialState, UBOOL ColorSum, UBOOL Fog );
	void EndFixedFunctionProgram();
	UBOOL					ColorSumEnabled;			// CommitStreams turned GL_COLOR_SUM on for this pass

	// Various material handlers (UC2GLMaterialState.cpp; UC2's D3DMaterialState.cpp).
	UBOOL SetMultiPassMaterial( UMultiPassMaterial* MultiPassMaterial, FOpenGLModifierInfo BaseModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial );
	UBOOL SetPixelShaderMaterial( UPixelShaderMaterial* InShader, FOpenGLModifierInfo InModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial, INT InPassesUsed=0 );
	UBOOL SetShaderMaterial( UShader* InShader, FOpenGLModifierInfo InModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial, INT InPassesUsed=0 );
	UBOOL SetSimpleMaterial( UMaterial* InMaterial, FOpenGLModifierInfo InModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial );
	UBOOL SetParticleMaterial( UParticleMaterial* InParticleMaterial, FOpenGLModifierInfo InModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial );
	UBOOL SetTerrainMaterial( UTerrainMaterial* InTerrainMaterial, FOpenGLModifierInfo InModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial );
	UBOOL SetProjectorMaterial( UProjectorMaterial* ProjectorMaterial, FOpenGLModifierInfo InModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial );
	UBOOL SetProjectorMultiMaterial( UProjectorMultiMaterial* ProjectorMultiMaterial, FOpenGLModifierInfo InModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial );
	void  SetLightingOnlyMaterial();
	UBOOL SetMovieMaterial( UMovieMaterial* MovieMaterial, FOpenGLModifierInfo BaseModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial );

	// Helpers
	UBOOL HandleCombinedMaterial( UMaterial* InMaterial, INT& PassesUsed, INT& StagesUsed, INT& FreeStagesUsed, FOpenGLModifierInfo ModifierInfo, UBOOL InvertOutputAlpha=0, FString* ErrorString=NULL, UMaterial** ErrorMaterial=NULL, UBOOL AddToCurrent=0 );
	void  SetShaderBitmap( FOpenGLMaterialStateStage& Stage, UBitmapMaterial* BitmapMaterial );
	void  HandleOpacityBitmap( FOpenGLMaterialStateStage& Stage, UBitmapMaterial* Bitmap, UBOOL ModulateAlpha=0, FOpenGLModifierInfo* ModifierInfo=NULL, INT PassesUsed=0 );
	void  HandleVertexOpacity( FOpenGLMaterialStateStage& Stage, UVertexColor* VertexColor );
	void  HandleSpecular_SP( FOpenGLMaterialStateStage& Stage, UBitmapMaterial* Bitmap, UBOOL UseSpecularity, UBOOL UseConstantSpecularity, UBOOL UseDiffuseModulate );
	void  HandleSelfIllumination_SP( FOpenGLMaterialStateStage& Stage, UBitmapMaterial* Bitmap );
	void  HandleLighting_MP( FOpenGLMaterialStateStage& Stage, FOpenGLTexture* Lightmap, UBOOL UseDiffuse );
	void  HandleLightmap_SP( FOpenGLMaterialStateStage& Stage, FOpenGLTexture* Lightmap );
	void  HandleDiffuse_Patch( FOpenGLMaterialStateStage& Stage, UBOOL Modulate2X = 0 );
	void  HandleDiffuse_SP( FOpenGLMaterialStateStage& Stage );
	void  HandleDiffuse_Stage( FOpenGLMaterialStateStage& Stage, UBOOL Modulate2X = 0 );
	void  HandleDiffuseTFactor_Stage( FOpenGLMaterialStateStage& Stage, UBOOL Modulate2X );
	void  HandleDetail( UBitmapMaterial* DetailBitmap, INT& PassesUsed, INT& StagesUsed, INT& FreeStagesUsed, FOpenGLModifierInfo InModifierInfo, UBOOL SinglePassOnly=0 );
	void  HandleTFactor_SP( FOpenGLMaterialStateStage& Stage );
	void  HandleBumpMaterial( FOpenGLMaterialStateStage& Stage, UBitmapMaterial* BumpMapBitmap, FOpenGLModifierInfo* ModifierInfo );
	void  ApplyTexModifier( FOpenGLMaterialStateStage& Stage, FOpenGLModifierInfo* ModifierInfo );
	void  ApplyFinalBlend( FOpenGLModifierInfo* InModifierInfo, INT PassesUsed=0 );
	void  HandlePSVertexShader( UMaterial* InMaterial, FOpenGLMaterialState* MaterialState );
};

//
//	FOpenGLPSMaterialStateHandlers: UC2's FD3DPSMaterialStateHandlers (render state per
//	pixel shader type; UC2GLPSMaterialState.cpp).
//
class FOpenGLPSMaterialStateHandlers
{
private:
	typedef void (*PSMatFuncPtr)( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static PSMatFuncPtr PSMatFuncPtrArray[];

public:
	// UC2GLDrv: two pixel shader types belong to one family when one state handler serves both.
	static UBOOL SameHandler( INT A, INT B ) { return PSMatFuncPtrArray[A] != NULL && PSMatFuncPtrArray[A] == PSMatFuncPtrArray[B]; }

	static void HandlePSMaterialState( UPixelShaderMaterial* InShader, FOpenGLModifierInfo* InModifierInfo, FOpenGLMaterialState* InMaterialState, FOpenGLRenderInterface* RI )
	{
		if( PSMatFuncPtrArray[InShader->PixelShaderType] != NULL )
			PSMatFuncPtrArray[InShader->PixelShaderType]( InShader, InModifierInfo, InMaterialState, RI );
	}

private:
	static void SetMatState_PS_Blur( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_DepthOfField( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_Mask( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_MotionBlur( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_Refraction( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_BloomSelect( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_EmissiveSelect( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_LuminanceTexIndex( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_UseTexIndex( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_Distortion( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_SkinShader( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_TeamSkinShader( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_SkinShaderOpacity( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_TeamShader( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_DualDiffuse( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_EmissiveSkinShader( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_EmissiveShader( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_EmissiveConstColor( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_MaskedLuminance( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_PlayerCam( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_DiffuseOpacity( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_ColorRemap( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_DepthSwizzleColor( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
	static void SetMatState_PS_ColorRemapGrayScale( UPixelShaderMaterial*, FOpenGLModifierInfo*, FOpenGLMaterialState*, FOpenGLRenderInterface* );
};

/*----------------------------------------------------------------------------
	CheckMaterial (UC2's, D3DRenderInterface.h).
----------------------------------------------------------------------------*/

//
// Attempt to cast a material/modifier chain to the appropriate class.
// If the chain ends with a material of the specfied class, remember any
// modifier info along the way.
//
template<class C,DWORD RenderHint> C* CheckMaterial(FOpenGLRenderInterface* RI, UMaterial* InMaterial, FOpenGLModifierInfo* ModifierInfo=NULL)
{
	C* Material = MaterialCast<C,RenderHint>(InMaterial);
	if( Material )
		return Material;

	// See if we have a chain of Modifiers eventually pointing to material of class C
	UModifier* Modifier = MaterialCast<UModifier,MRH_Modifier>(InMaterial);
	while( Modifier )
	{
		Material = MaterialCast<C,RenderHint>(Modifier->Material);
		Modifier = MaterialCast<UModifier,MRH_Modifier>(Modifier->Material);
	}

	// If we have a C, go through the Modifier list and combine the matrices for any TexModifiers.
	if( Material && ModifierInfo )
	{
		UBOOL NeedSource = 1;
		Modifier = MaterialCast<UModifier,MRH_Modifier>(InMaterial);
		while( Modifier )
		{
			// Check for TexModifier.
			UTexModifier* TexModifier = MaterialCast<UTexModifier,MRH_TexModifier>(Modifier);
			if( TexModifier )
			{
				FMatrix* TexMatrix = TexModifier->GetMatrix(UC2GLMaterialTime(RI->Viewport->Actor->Level));
				if( TexMatrix )
				{
					ModifierInfo->ModifyTextureTransforms = 1;
					ModifierInfo->Matrix *= *TexMatrix;
				}
				// Locate the first non-passthrough texture coordinate source.
				if( NeedSource && TexModifier->TexCoordSource!=TCS_NoChange )
				{
					ModifierInfo->ModifyTextureTransforms = 1;
					ModifierInfo->TexCoordSource	= TexModifier->TexCoordSource;
					ModifierInfo->TexCoordCount		= TexModifier->TexCoordCount;
					ModifierInfo->TexCoordProjected	= TexModifier->TexCoordProjected;
					NeedSource = 0;
				}
			}

			// Check for FinalBlend modifier.
			UFinalBlend* FinalBlend = MaterialCast<UFinalBlend,MRH_FinalBlend>(Modifier);
			if( FinalBlend )
			{
				ModifierInfo->ModifyFramebufferBlending = 1;

				ModifierInfo->FrameBufferBlending	= FinalBlend->FrameBufferBlending;
				ModifierInfo->ZWrite				= FinalBlend->ZWrite;
				ModifierInfo->ZTest					= FinalBlend->ZTest;
				ModifierInfo->AlphaTest				= FinalBlend->AlphaTest;
				ModifierInfo->TwoSided				= FinalBlend->TwoSided;
				ModifierInfo->AlphaRef				= FinalBlend->AlphaRef;
				ModifierInfo->OverrideAlpha			= FinalBlend->OverrideAlpha;
				ModifierInfo->AlphaEnable			= FinalBlend->AlphaEnable;
				ModifierInfo->AlphaWriteEnable		= FinalBlend->AlphaWriteEnable;
				ModifierInfo->ColorWriteEnable		= FinalBlend->ColorWriteEnable;
			}

			// Check for ColorModifier modifier.
			UColorModifier* ColorModifier = MaterialCast<UColorModifier,MRH_ColorModifier>(Modifier);
			if( ColorModifier )
			{
				ModifierInfo->ModifyColor	= 1;

				ModifierInfo->AlphaBlend	|= ColorModifier->AlphaBlend;
				ModifierInfo->TwoSided		|= ColorModifier->RenderTwoSided;
				ModifierInfo->TFactorColor	= ColorModifier->Color;
			}

			// Check for an OpacityModifier modifier.
			UOpacityModifier* OpacityModifier = MaterialCast<UOpacityModifier,MRH_OpacityModifier>(Modifier);
			if( OpacityModifier )
			{
				ModifierInfo->ModifyOpacity = 1;
				ModifierInfo->Opacity = OpacityModifier->Opacity;
				ModifierInfo->OpacityOverrideTexModifier = OpacityModifier->bOverrideTexModifier;
			}

			// Move to the next modifier in the chain
			Modifier = MaterialCast<UModifier,MRH_Modifier>(Modifier->Material);
		}
	}

	return Material;
}

#endif
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
