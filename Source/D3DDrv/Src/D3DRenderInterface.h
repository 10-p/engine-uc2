/*=============================================================================
	D3DRenderInterface.h: Unreal Direct3D rendering interface definition.
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/

#ifndef HEADER_D3DRENDERINTERFACE
#define HEADER_D3DRENDERINTERFACE

#if SUPPORTS_PRAGMA_PACK
#pragma pack (push,1)
#endif
///
//	FD3DModifierInfo
//
struct FD3DModifierInfo
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
	BITFIELD			ModifyVertexOpacity:1;			// scion sz - 
	// framebuffer blend modifier
    BITFIELD			ZWrite:1;
    BITFIELD			ZTest:1;
    BITFIELD			AlphaTest:1;
    BITFIELD			TwoSided:1;
	//scion jg -- For overriding an alpha setting
	BITFIELD			AlphaEnable:1;
	BITFIELD			OverrideAlpha:1;
	BITFIELD			AlphaBlend:1;
	BITFIELD			OpacityOverrideTexModifier:1;
	//jg -- For enabling/disabling alpha/color writes
	BITFIELD			ColorWriteEnable:1;
	BITFIELD			AlphaWriteEnable:1;

	// Constructor
	FD3DModifierInfo();

	// FD3DModifierInfo interface
	void SetDetailTextureScale( FLOAT Scale );
};
#if SUPPORTS_PRAGMA_PACK
#pragma pack (pop)
#endif

///
//	FD3DMaterialStateStage
//
struct FD3DMaterialStateStage
{
	FD3DTexture*		Texture;
	D3DTEXTUREADDRESS	TextureAddressU,
						TextureAddressV,
						TextureAddressW;
    FLOAT               TextureMipLODBias;
	
	D3DTEXTUREOP 		ColorOp,
						AlphaOp;
	DWORD				ColorArg0,
						ColorArg1,
						ColorArg2,
						AlphaArg0,
						AlphaArg1,
						AlphaArg2,
						ResultArg;

	BOOL				TextureFilterEnabled;
	DWORD				MinFilter,
						MagFilter,
						MipFilter;

	DWORD				TexCoordIndex;
	DWORD				TexCoordCount;
	UBOOL				TextureTransformsEnabled;
	D3DMATRIX			TextureTransformMatrix;

#ifdef BUMPMAP_ENABLE
	DWORD				ColorSign;
	BOOL				BumpMapEnabled;
	DWORD				BumpEnvMat00,
						BumpEnvMat01,
						BumpEnvMat10,
						BumpEnvMat11;
	DWORD				BumpEnvLScale;
	DWORD				BumpEnvLOffset;
#endif

	FD3DMaterialStateStage();
};

#ifdef _XBOX //scion jg -- Define the max number of stages supported
#define MAX_STAGES 8
#else
#define MAX_STAGES 8
#endif

///
//	FD3DMaterialState
//
class FD3DMaterialState
{
public:
	EPixelShader		PixelShader;
	BYTE				AlphaBlending;			// Alpha blending is enabled
	BYTE				AlphaTest;				// Alpha test is enabled?
	BYTE				ZTest;					// Test zbuffer
	BYTE				ZWrite;					// Write to zbuffer
	BYTE				TwoSided;
	BYTE				OverrideFogColor;
	BYTE				SpecularEnable;

	BYTE				AlphaRef;				// If alpha testing, the value to compare against
	EFillMode			FillMode;				// Wireframe, flatshaded or solid
	D3DCOLOR			TFactorColor;
	D3DBLEND			SrcBlend,
						DestBlend;
	D3DBLENDOP			BlendOp;
	FColor				OverriddenFogColor;
	DWORD				ColorWriteMask;			// scion sz -- per material color write mask
	QWORD				MaterialVertexShaderEnum;	// scion jg/sz -- per material vertex shader fragments

	BYTE					StagesUsed;
	FD3DMaterialStateStage	Stages[MAX_STAGES];

	INT	NumRefs;

	FD3DMaterialState();
};

//
//	FD3DMaterialStatePool
//

class FD3DMaterialStatePool
{
public:

	TArray<FD3DMaterialState*>	FreeStates;

	// Destructor.

	~FD3DMaterialStatePool()
	{
		for(INT StateIndex = 0;StateIndex < FreeStates.Num();StateIndex++)
			delete FreeStates(StateIndex);
	}

	// AllocateState

	FD3DMaterialState* AllocateState(FD3DMaterialState* DefaultState)
	{
		FD3DMaterialState*	Result;

		if(FreeStates.Num())
			Result = FreeStates.Pop();
		else
			Result = NEW FD3DMaterialState();

		if(DefaultState)
			appMemcpy(Result,DefaultState,sizeof(FD3DMaterialState));

		Result->NumRefs = 1;

		return Result;
	}

	// FreeState

	void FreeState(FD3DMaterialState* State)
	{
		FreeStates.AddItem(State);
	}
};

// scion sz -
// FD3DDynamicGamma
//
class FD3DDynamicGamma
{
public:
	struct FD3DHistogram 
	{
		FLOAT	Values[256];							// luminosity samples
		FLOAT	MinL,MaxL,AvgL;							// min/max/avg luminosity
	};

	FD3DHistogram			Histogram;

	FLOAT					CurMinL,					// Current interpolated min/max/avg luminosity 
							CurMaxL, 
							CurAvgL;

	D3DGAMMARAMP			GammaRamp;					// final gamma curve
	

	FRenderTarget*			LuminanceTarget;			// down sampled grayscale texture used to render back buffer into
	INT						LuminanceSampleWidth,		// width of LuminanceTarget
							LuminanceSampleHeight;		// height of LuminanceTarget

	DOUBLE					UpdateRate,					// rate to update LuminanceTarget and set gamma
							LastUpdateTime;				// 
	FLOAT					TransitionRate;				// rate to blend to new gamma ramp
	UBOOL					ForceUpdate;				//

	class FD3DRenderInterface*	RI;						// 

	FD3DDynamicGamma( FD3DRenderInterface* InRI );
	void UpdateLuminanceTarget();
	void UpdateHistogram();
	void UpdateDeviceGamma();
	void ResetDeviceGamma();
	void ShowDebug();
};

#ifdef _XBOX //scion jg -- Max allowed passes 3 is plenty
#define MAX_MATERIAL_PASSES 3
#else
#define MAX_MATERIAL_PASSES 8
#endif

#define MAX_STATESTACKDEPTH	32

//scion jg -- Base handler of state depth
class FD3DStateDepth
{
protected:
	// The index of the currently used state
	BYTE StateIndex;

public:
	FD3DStateDepth()
	{
		StateIndex = 0;
	}

	FORCEINLINE BYTE GetStateIndex() { return StateIndex; }
};

//scion jg -- Structure used to save rendertarget pointers
struct FD3DRenderTargetState
{
	FRenderTarget*		RenderTarget;
	IDirect3DSurface8*	RenderTargetSurface;
	IDirect3DSurface8*	DepthStencilSurface;

	// Default constructor to set the base size
	FD3DRenderTargetState()
	{
		RenderTarget = NULL;
		RenderTargetSurface = DepthStencilSurface = NULL;
	}
};

//scion jg -- Class to manage viewport state changes
class FD3DSavedRenderTargetState : public FD3DStateDepth
{
	// The stack of viewport size values being used
	FD3DRenderTargetState SavedStates[MAX_STATESTACKDEPTH];

public:
	// Push a new stencil state onto the stack
	FORCEINLINE void PushRenderTarget(FRenderTarget* pTarget,
		IDirect3DSurface8* pTargetSurf,IDirect3DSurface8* pDepth)
	{
		// Move to the next state position
		StateIndex++;
		checkSlow(StateIndex < MAX_STATESTACKDEPTH &&
			"Nested too deep in the render target stack depth");
		// Copy the passed in values
		SavedStates[StateIndex].RenderTarget = pTarget;
		SavedStates[StateIndex].RenderTargetSurface = pTargetSurf;
		SavedStates[StateIndex].DepthStencilSurface = pDepth;
	}

	// Restores the previous viewport size
	FORCEINLINE void PopRenderTarget()
	{
		checkSlow(StateIndex > 0 &&
			"Popped too many render target changes off of the stack");
		// Move to the previous state position
		StateIndex--;
	}

	// Returns a pointer to the current state
	FORCEINLINE FD3DRenderTargetState* GetCurrentState(void)
	{
		return &SavedStates[StateIndex];
	}
};

//scion jg -- Structure used to save viewport sizes
struct FD3DViewportState
{
	_WORD				ViewportX,
						ViewportY,
						ViewportWidth,
						ViewportHeight;

	// Default constructor to set the base size
	FD3DViewportState()
	{
		ViewportX = 0;
		ViewportY = 0;
		ViewportWidth = 640;
		ViewportHeight = 480;
	}
};

//scion jg -- Class to manage viewport state changes
class FD3DSavedViewportState : public FD3DStateDepth
{
	// The stack of viewport size values being used
	FD3DViewportState SavedStates[MAX_STATESTACKDEPTH];

public:
	// Push a new stencil state onto the stack
	FORCEINLINE void PushViewportSize(_WORD InX,_WORD InY,_WORD InWidth,
		_WORD InHeight)
	{
		// Move to the next state position
		StateIndex++;
		checkSlow(StateIndex < MAX_STATESTACKDEPTH &&
			"Nested too deep in the viewport size stack depth");
		// Copy the passed in values
		SavedStates[StateIndex].ViewportX = InX;
		SavedStates[StateIndex].ViewportY = InY;
		SavedStates[StateIndex].ViewportWidth = InWidth;
		SavedStates[StateIndex].ViewportHeight = InHeight;
	}

	// Restores the previous viewport size
	FORCEINLINE void PopViewportSize()
	{
		checkSlow(StateIndex > 0 &&
			"Popped too many viewport size changes off of the stack");
		// Move to the previous state position
		StateIndex--;
	}

	// Returns a pointer to the current state
	FORCEINLINE FD3DViewportState* GetCurrentState(void)
	{
		return &SavedStates[StateIndex];
	}
};

//scion jg -- Structure used to save stencil state
struct FD3DStencilState
{
	ECompareFunction	StencilTest;
	EStencilOp			StencilFailOp,
						StencilZFailOp,
						StencilPassOp;
	DWORD				StencilRef,
						StencilMask,
						StencilWriteMask;

	// Default constructor to put things into their default state
	FD3DStencilState()
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

//scion jg -- Class to manage stencil state changes
class FD3DSavedStencilState : public FD3DStateDepth
{
	// The stack of stencil state values being used
	FD3DStencilState SavedStencilStates[MAX_STATESTACKDEPTH];

public:
	// Push a new stencil state onto the stack
	FORCEINLINE void PushStencilState(ECompareFunction Test,DWORD Ref,
		DWORD Mask,EStencilOp FailOp,EStencilOp ZFailOp,EStencilOp PassOp,
		DWORD WriteMask)
	{
		// Move to the next state position
		StateIndex++;
		checkSlow(StateIndex < MAX_STATESTACKDEPTH &&
			"Nested too deep in the stencil state stack depth");
		// Use the common routine for setting the state
		SetCurrentState(Test,Ref,Mask,FailOp,ZFailOp,PassOp,WriteMask);
	}

	// Just sets the specified state in the stack
	FORCEINLINE void SetCurrentState(ECompareFunction Test,DWORD Ref,
		DWORD Mask,EStencilOp FailOp,EStencilOp ZFailOp,EStencilOp PassOp,
		DWORD WriteMask)
	{
		// Copy the passed in values
		SavedStencilStates[StateIndex].StencilTest = Test;
		SavedStencilStates[StateIndex].StencilFailOp = FailOp;
		SavedStencilStates[StateIndex].StencilZFailOp = ZFailOp;
		SavedStencilStates[StateIndex].StencilPassOp = PassOp;
		SavedStencilStates[StateIndex].StencilRef = Ref;
		SavedStencilStates[StateIndex].StencilMask = Mask;
		SavedStencilStates[StateIndex].StencilWriteMask	= WriteMask;
	}

	// Restore the stencil state to its former state
	FORCEINLINE void PopStencilState()
	{

		checkSlow(StateIndex > 0 &&
			"Popped too many stencil state changes off of the stack");

		// Move to the previous state position
		StateIndex--;
	}

	// Returns a pointer to the current state
	FORCEINLINE FD3DStencilState* GetCurrentState(void)
	{
		return &SavedStencilStates[StateIndex];
	}
};

#define MAX_D3D_LIGHT_TYPES D3DLIGHT_DIRECTIONAL

//
//	FD3DRenderInterface
//
class FD3DRenderInterface : public FRenderInterface
{
public:

	///
	//	FD3DSavedState
	//
	class FD3DSavedState
	{
	public:
		DWORD				ColorWriteMask;

		FMatrix				LocalToWorld,
							WorldToCamera,
							CameraToScreen;

		QWORD				VertexShaderEnum;	
		FD3DVertexShader*	VertexShader;
		
		FD3DVertexStream*	Streams[16];
		INT					StreamStrides[16];
		BYTE				NumStreams;

		BYTE				ForceDefSphereMap;

		FD3DIndexBuffer*	IndexBuffer;
		INT					IndexBufferBase;

		D3DCULL				CullMode;

		FD3DMaterialState*	MaterialPasses[MAX_MATERIAL_PASSES];
		FD3DMaterialState*	CurrentMaterialState;
		BYTE				NumMaterialPasses;

		BYTE				ZBias;
		BYTE				UseDetailTexturing;
		BYTE				UseDynamicLighting;
		BYTE				UseStaticLighting;
		BYTE				LightingModulate2X;
		BYTE				LightingOnly;
		BYTE				DistanceFogEnabled;

		FD3DTexture*		Lightmap;
		FSphere				LitSphere;
		FColor				AmbientLightColor;
		
		FLOAT				DistanceFogStart,
							DistanceFogEnd;
		FColor				DistanceFogColor;

		UMaterial*			LastMaterial;

		FD3DSavedState();
	};


	UD3DRenderDevice*		RenDev;
	UViewport*				Viewport;
	FD3DSavedState			SavedStates[MAX_STATESTACKDEPTH];
	FD3DSavedState*			CurrentState;
	FD3DMaterialState		DefaultPass;
	// scion sz - current # of lights for each type	
	INT						LightTypeTotals[MAX_D3D_LIGHT_TYPES];

	INT						SavedStateIndex;

	//scion jg -- Moved out of FD3DSavedState since this is infrequently changed
	// data so it shouldn't be pushed/popped each render call
	FD3DSavedStencilState SavedStencilState;
	FD3DSavedViewportState SavedViewportState;
	FD3DSavedRenderTargetState SavedRenderTargetState;
	// end new stacks of states

	FD3DMaterialStatePool	MaterialStatePool;

	EPrecacheMode			PrecacheMode;

#ifndef _XBOX //scion jg -- Unused on the xbox so save space
	// Hit stack.
	TArray<BYTE>			HitStack;
	BYTE*					HitData;
	INT*					HitSize;
	INT						HitCount;
	DWORD					HitPixels[HIT_SIZE][HIT_SIZE];
#endif

	FRenderTarget*			BackBufferCopyTarget;
	FD3DDynamicGamma*		DynamicGamma;

	// scion sz - reusable render targets
	FRenderTargetPool		RenderTargetPool;
	
	// scion sz - current step in rendering distortion objects
	EPostProcessStage		PostProcessStage;

	// Constructor.
	FD3DRenderInterface(UD3DRenderDevice* InRenDev);
	~FD3DRenderInterface();

	// scion sz - get current # of lights of a certain type	
	FORCEINLINE INT GetLightTypeCount( D3DLIGHTTYPE LightType )
	{
		return LightTypeTotals[(INT)LightType-1];
	}

	// scion sz - increment total for given light type
	FORCEINLINE void IncrementLightType( D3DLIGHTTYPE LightType )
	{
		LightTypeTotals[(INT)LightType-1]++;
	}

	// FRenderInterface interface.
	virtual void PushState();
	virtual void PopState();

	virtual UBOOL SetRenderTarget(FRenderTarget* InRenderTarget);
	//scion jg -- Added for restoring previous state without Push/PopState()
	virtual void ResetRenderTarget(void);
	virtual FRenderTarget* GetRenderTarget() { return SavedRenderTargetState.GetCurrentState()->RenderTarget; }
	//scion jg -- Used to determine if we are back buffer rendering or not
	virtual UBOOL IsDefaultRenderTarget(void);
	virtual FRenderTargetPool* GetRenderTargetPool() { return &RenderTargetPool; }
	virtual void SetViewport(INT X,INT Y,INT Width,INT Height);
	// scion sz -
	virtual void SetScreenSpaceOffset( FLOAT OffsetX, FLOAT OffsetY );
	//scion jg -- Added for resetting to previous state
	virtual void ResetViewport(void);
	virtual void GetViewport(INT* X,INT* Y,INT* Width,INT* Height);
	virtual UViewport* GetCurViewportPtr() { return Viewport; }
	virtual void Clear(UBOOL UseColor,FColor Color,UBOOL UseDepth,FLOAT Depth,UBOOL UseStencil,DWORD Stencil);
	// scion sz - added to control color write mask
	virtual void SetColorWriteMask( DWORD ColorMask );

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

	//scion jg -- Return the number of passes this material needs
	virtual INT SetMaterial(UMaterial* Material, FString* ErrorString, UMaterial** ErrorMaterial, INT* NumPasses);
	virtual void SetZBias(INT ZBias);
	virtual void SetStencilOp(ECompareFunction Test,DWORD Ref,DWORD Mask,EStencilOp FailOp,EStencilOp ZFailOp,EStencilOp PassOp,DWORD WriteMask);
	//scion jg -- Restores the previous stencil render states
	virtual void ResetStencilOp(void);
	//scion jg -- Added for shadows being able to disable stencil (needed for L8 rendering)
	virtual void EnableStencil(UBOOL bEnable);
	//scion jg -- Added for shadows being able to disable Z-buffer (needed for L8 rendering)
	virtual void EnableDepth(UBOOL bEnable);
	// scion sz - material in current state
	virtual UMaterial* GetLastMaterial();

	//jg -- So we can set vertex shader constants
	virtual void SetVertexShaderConstant(INT Register,const void* pConstantData,DWORD ConstantCount);

	virtual void SetPrecacheMode( EPrecacheMode PrecacheMode );
	//scion jg -- So we can query the mode
	virtual EPrecacheMode GetPrecacheMode(void);

	// scion sz - add vertex shader type to current deferred state VertexShaderEnum
	virtual void AddVertexShaderEnum( QWORD VertexShaderEnum );
	virtual INT  SetVertexStreams(QWORD VertexShaderEnum,FVertexStream** Streams,INT NumStreams);
	virtual INT  SetDynamicStream(QWORD VertexShaderEnum,FVertexStream* Stream);

	virtual INT  SetIndexBuffer(FIndexBuffer* IndexBuffer,INT BaseVertexIndex);
	virtual INT  SetDynamicIndexBuffer(FIndexBuffer* IndexBuffer,INT BaseVertexIndex);

	virtual void DrawPrimitive(EPrimitiveType PrimitiveType,INT FirstIndex,INT NumPrimitives,INT MinIndex,INT MaxIndex);

#ifdef _XBOX
	//scion jg -- For rendering small pre-built dynamic vertex buffers without stalling
	virtual void DrawPrimitiveUP(QWORD VertexShaderEnum,EPrimitiveType PrimitiveType,
		INT NumPrimitives,FVertexStream* pVBStream,FIndexBuffer* pIBStream = NULL);
	//scion jg -- Used to execute a push buffer. If the bUseIndexStream is false,
	// the index stream is not committed
	virtual void RunPushBuffer(FPushBuffer* pPushBuffer,UBOOL bUseIndexStream = 0,UBOOL bCommit = 1);
	//scion jg -- Used to get a pointer directly into the main push buffer
	virtual DWORD* BeginPush(DWORD dwSizeNeeded,UBOOL bCommitStates = 1,
		UBOOL bSetTexCoords = 1);
	//scion jg -- Used to get a pointer directly into the main push buffer
	virtual DWORD* BeginPush(DWORD dwSizeNeeded,QWORD VertexShaderEnum,
		FVertexStream* pVBStream,UBOOL bCommitStates = 1,
		UBOOL bSetTexCoords = 1);
	//scion jg -- Releases the push buffer grabbed with BeginPush
	virtual void EndPush(DWORD* pEndPointer);
	//scion jg -- Allows you to control the locking scheme of a D3D resource.
	// Insert a fence just after rendering a resource in order to have the
	// cpu get the resource faster. Returns the fence that was inserted.
	virtual DWORD InsertFence(void);
#else
	//scion jg -- So the editor can report how many passes a material is on BSP
	virtual DWORD GetStageCountForPass(INT Pass);
#endif

	// scion sz -- retrieve the vertex shader type for a material (not used)
	virtual DWORD GetMaterialVertexShaderType(UMaterial* Material);

	// scion sz -- back buffer blit copy used for post process effects
	virtual UMaterial* GetBackBufferCopyMaterial();
	virtual FRenderTarget* GetBackBufferCopyTarget();
	virtual void SetBackBufferCopyTarget(FRenderTarget* RenderTarget) { BackBufferCopyTarget = RenderTarget; }
	virtual void UpdateBackBufferCopyTarget(UViewport* InViewport=NULL);
	virtual UMaterial* GetBackBufferMaterial();
	// scion sz - copy depth buffer from one render target to another
	virtual void CopyDepthBuffer( FRenderTarget* DestTarget, FRenderTarget* SrcTarget );
	
	// scion sz - set the different steps for rendering distortion
	virtual void SetPostProcessStage( EPostProcessStage InStage ) {
		PostProcessStage = InStage;
	}
	virtual EPostProcessStage GetPostProcessStage() { 
		return PostProcessStage; 
	}
	// scion sz - force sphere mapping mode to default
	virtual void ForceDefSphereMappingMode( UBOOL bEnable )
	{
		CurrentState->ForceDefSphereMap = bEnable;		
	}
	
	// scion sz -- update the gamma curve based on the current scene luminosity
	virtual void UpdateDynamicGamma();
	virtual FLOAT GetAverageSceneLuminosity();

	// Skeletal skin shader support.	
	void SetSkinBones( INT FirstBoneDestIndex, FCoords* FirstBone, INT BoneCount );

	// scion sz - sets the terrain world transform
	virtual void SetTerrainTransform( FMatrix& WorldMat, INT HeightMapX, INT HeightMapY );
	
	// Helper function.
	virtual D3DSTENCILOP GetD3DStencilOp( EStencilOp StencilOp );
	virtual D3DCMPFUNC   GetD3DCompFunc( ECompareFunction CompFunc );

	// FD3DRenderInterface interface.
	void Locked( UViewport* InViewport, BYTE* InHitData,INT* InHitSize);
	void Unlocked();

	virtual void UpdateDOFRampLookupTexture( UMaterial* DOFShader, BYTE FocusType );
	//scion jg -- Allows the direct setting of a renderstate bypassing the deferred commit
	virtual void SetRenderStateDirect(BYTE State,DWORD Value);
	//jg -- Used for occlusion queries
	virtual UBOOL BeginOcclusionQuery(void);
	virtual UBOOL EndOcclusionQuery(DWORD QueryNum);
	virtual UBOOL GetOcclusionQueryResults(DWORD QueryNum,DWORD& dwPixels);

	// scion sz - for adjusting perspective matrix based on pixel size
	virtual FLOAT GetPixelAspectRatio();

private:
	FD3DTexture* CacheTexture(FBaseTexture* Texture);
	void SetMaterialBlending( FD3DMaterialState* NewMaterialState, D3DCULL CullMode );
	
	// Various material handlers

	// scion sz - added to handle UMultiPassMaterials
	UBOOL SetMultiPassMaterial( UMultiPassMaterial* MultiPassMaterial, FD3DModifierInfo BaseModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial );
	// scion sz - added to handle UPixelShaderMaterials
	UBOOL SetPixelShaderMaterial( UPixelShaderMaterial* InShader, FD3DModifierInfo InModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial, INT InPassesUsed=0 );
	UBOOL SetShaderMaterial( UShader* InShader, FD3DModifierInfo InModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial, INT InPassesUsed=0 );
	UBOOL SetSimpleMaterial( UMaterial* InMaterial, FD3DModifierInfo InModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial );
	UBOOL SetParticleMaterial( UParticleMaterial* InParticleMaterial, FD3DModifierInfo InModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial );
	UBOOL SetTerrainMaterial( UTerrainMaterial* InTerrainMaterial, FD3DModifierInfo InModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial );	
	UBOOL SetProjectorMaterial( UProjectorMaterial* ProjectorMaterial, FD3DModifierInfo InModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial );
	// scion sz - added to handle UProjectorMultiMaterial
	UBOOL SetProjectorMultiMaterial( UProjectorMultiMaterial* ProjectorMultiMaterial, FD3DModifierInfo InModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial );
	void  SetLightingOnlyMaterial();
	//scion jg -- Added for movie to texture
	UBOOL SetMovieMaterial( UMovieMaterial* MovieMaterial, FD3DModifierInfo BaseModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial );

	// Helpers
	FORCEINLINE UBOOL HandleCombinedMaterial( UMaterial* InMaterial, INT& PassesUsed, INT& StagesUsed, INT& FreeStagesUsed, FD3DModifierInfo ModifierInfo, UBOOL InvertOutputAlpha=0, FString* ErrorString=NULL, UMaterial** ErrorMaterial=NULL, UBOOL AddToCurrent=0 );
	FORCEINLINE void  SetShaderBitmap( FD3DMaterialStateStage& Stage, UBitmapMaterial* BitmapMaterial );
	FORCEINLINE void  HandleOpacityBitmap( FD3DMaterialStateStage& Stage, UBitmapMaterial* Bitmap, UBOOL ModulateAlpha=0, FD3DModifierInfo* ModifierInfo=NULL, INT PassesUsed=0 );
	FORCEINLINE void  HandleVertexOpacity( FD3DMaterialStateStage& Stage, UVertexColor* VertexColor );
	FORCEINLINE void  HandleSpecular_SP( FD3DMaterialStateStage& Stage, UBitmapMaterial* Bitmap, UBOOL UseSpecularity, UBOOL UseConstantSpecularity, UBOOL UseDiffuseModulate );
	FORCEINLINE void  HandleSelfIllumination_SP( FD3DMaterialStateStage& Stage, UBitmapMaterial* Bitmap );
	FORCEINLINE void  HandleLighting_MP( FD3DMaterialStateStage& Stage, FD3DTexture* Lightmap, UBOOL UseDiffuse );
	FORCEINLINE void  HandleLightmap_SP( FD3DMaterialStateStage& Stage, FD3DTexture* Lightmap );
	FORCEINLINE void  HandleDiffuse_Patch( FD3DMaterialStateStage& Stage, UBOOL Modulate2X = 0 );
	FORCEINLINE void  HandleDiffuse_SP( FD3DMaterialStateStage& Stage );
	FORCEINLINE void  HandleDiffuse_Stage( FD3DMaterialStateStage& Stage, UBOOL Modulate2X = 0 );
	FORCEINLINE void  HandleDiffuseTFactor_Stage( FD3DMaterialStateStage& Stage, UBOOL Modulate2X );
	FORCEINLINE void  HandleDetail( UBitmapMaterial* DetailBitmap, INT& PassesUsed, INT& StagesUsed, INT& FreeStagesUsed, FD3DModifierInfo InModifierInfo, UBOOL SinglePassOnly=0 );
	FORCEINLINE void  HandleTFactor_SP( FD3DMaterialStateStage& Stage );
	FORCEINLINE void  HandleBumpMaterial( FD3DMaterialStateStage& Stage, UBitmapMaterial* BumpMapBitmap, FD3DModifierInfo* ModifierInfo );
	FORCEINLINE void  ApplyTexModifier( FD3DMaterialStateStage& Stage, FD3DModifierInfo* ModifierInfo );
	FORCEINLINE void  ApplyFinalBlend( FD3DModifierInfo* InModifierInfo, INT PassesUsed=0 );

	// scion sz - Pixel Shader Helpers
	FORCEINLINE void HandlePSVertexShader( UMaterial* InMaterial, FD3DMaterialState* MaterialState );	

	FString DescribeStage(FD3DMaterialState* MaterialState,INT StageIndex,UBOOL Alpha);
	FString DescribeArg(FD3DMaterialState* MaterialState,INT StageIndex,DWORD Arg,UBOOL Alpha);
};

// scion sz -
// Allows for calling material state handler functions for pixel shader materials 
// based on the UPixelShaderMaterial's EPixelShader type.
class FD3DPSMaterialStateHandlers
{
private:
	// function pointer typedef for readability
	typedef void (*PSMatFuncPtr)( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	// array of function pointers to the handlers
	static PSMatFuncPtr PSMatFuncPtrArray[];

public:
	// entry point for setting the pixel shader material's state
	// by calling one of the function pointers in PSMatFuncPtrArray
	FORCEINLINE static void HandlePSMaterialState( UPixelShaderMaterial* InShader, 
		FD3DModifierInfo* InModifierInfo, FD3DMaterialState* InMaterialState, 
		FD3DRenderInterface* RI )
	{
		//checkSlow( InShader->PixelShaderType <= ARRAY_COUNT(PSMatFuncPtrArray) );
		if( PSMatFuncPtrArray[InShader->PixelShaderType] != NULL ) {
			PSMatFuncPtrArray[InShader->PixelShaderType]( InShader, InModifierInfo, InMaterialState, RI );
		}
	}

private:
	// material state handler functions for the different pixel shader types
	static void SetMatState_PS_Blur( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_DepthOfField( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_Mask( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_MotionBlur( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_Refraction( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_BloomSelect( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_EmissiveSelect( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_LuminanceTexIndex( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_UseTexIndex( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_Distortion( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_SkinShader( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_TeamSkinShader( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_SkinShaderOpacity( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_TeamShader( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_DualDiffuse( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_EmissiveSkinShader( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_EmissiveShader( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_EmissiveConstColor( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_MaskedLuminance( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_PlayerCam( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_DiffuseOpacity( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_ColorRemap( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_DepthSwizzleColor( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
	static void SetMatState_PS_ColorRemapGrayScale( UPixelShaderMaterial*, FD3DModifierInfo*, FD3DMaterialState*, FD3DRenderInterface* );
};

/*----------------------------------------------------------------------------
	CheckMaterial.
----------------------------------------------------------------------------*/

//
// Attempt to cast a material/modifier chain to the appropriate class.
// If the chain ends with a material of the specfied class, remember any 
// modifier info along the way.
//
template<class C,DWORD RenderHint> C* CheckMaterial(FD3DRenderInterface* RI,
	UMaterial* InMaterial, FD3DModifierInfo* ModifierInfo=NULL)
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
				FMatrix* TexMatrix = TexModifier->GetMatrix(RI->Viewport->Actor->Level->TimeSeconds);
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
				//scion jg -- Alpha override handling
				ModifierInfo->OverrideAlpha			= FinalBlend->OverrideAlpha;
				ModifierInfo->AlphaEnable			= FinalBlend->AlphaEnable;
				//jg -- Copy color/alpha write settings
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
