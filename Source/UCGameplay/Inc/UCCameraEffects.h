
/* scion ======================================================================
* Class: UUCScreenDOF
* Author: sz
* 
* see UCScreenDOF.uc 
* =============================================================================
*/
class UCGAMEPLAY_API UUCScreenDOF : public UCameraEffect
{
	DECLARE_CLASS(UUCScreenDOF,UCameraEffect,0,UCGameplay);

public:

	FRenderTarget*					BackBufferTarget;			// source for blurring
	TArray<FRenderTarget*>			BlurTargets;

	//jg -- Changed to use on set of these resources for all instances. Uses
	// a reference counting scheme to handle all instances being destroyed.	
	static UPSDepthOfFieldMaterial*		SharedPixelShaderDOFMaterial;	
	// Ref count for allocating/freeing the above resources
	static DWORD DOFResourceRefCount;

	// Per instance pointer so script can manipulate it
	UPSDepthOfFieldMaterial*		PixelShaderDOFMaterial;	
	FLOAT					BlurTargetScaleX;
	FLOAT					BlurTargetScaleY;

	enum EDOFFocusType
	{
		DOF_Manual,
		DOF_FocusActor,
		DOF_FocusDistance,
		// Blur effect starts at an aboslute distance in the Z-buffer
		DOF_FocusAbsoluteZDepth
	};

	EDOFFocusType		FocusType;
	AActor*				FocusActor;
	FLOAT				FocusDistance;
	FLOAT				FocusRadius;

	//TEMP jg -- Added this to help debug DOF w/matinee
    BITFIELD bDebugFocusRange:1 GCC_PACK(4);
	
	//scion jg -- Used by matinee to toggle off the stencil masking
    BITFIELD bUseStencilMask:1;
	
	FColor	ModulateBlurColor GCC_PACK(4);

	//scion jg -- For all the time on DOF
	// Start Z is where in the depth buffer the effect starts blending in
	FLOAT StartZ;
	// UCScreenDOF.uc declares these two before AbsoluteBlurAlpha and picks one of them into it per camera mode.
	// This header had fallen behind the script: without them AbsoluteBlurAlpha sat at FirstPersonAlpha's offset
	// (the class size still matched under MSVC, so the engine's size check never noticed).
	BYTE FirstPersonAlpha;
	BYTE ThirdPersonAlpha;
	// The max amount to blend the blur in at a Z-depth of 1
	BYTE AbsoluteBlurAlpha;
	BITFIELD bNeedsAbsoluteUpdate:1 GCC_PACK(4);
	// min alpha ref for drawing to save fill
	BYTE AlwaysOnAlphaRef GCC_PACK(4);
	BYTE DefaultAlphaRef;

	UUCScreenDOF();

	// UObject interface.
	void Destroy();

	// UCameraEffect interface.
	virtual void PreRender(UViewport* Viewport,FRenderInterface* RI);
	virtual void PostRender(UViewport* Viewport,FRenderInterface* RI);
	void HandleFocusActor(UViewport* Viewport,FRenderInterface* RI);
	void HandleFocusDistance(UViewport* Viewport,FRenderInterface* RI);
	virtual void DestroyRenderTargets();
};

/* scion ======================================================================
* Class: UUCScreenMotionBlur
* Author: sz
* 
* see UCScreenMotionBlur.uc 
* =============================================================================
*/
class UCGAMEPLAY_API UUCScreenMotionBlur : public UCameraEffect
{
	DECLARE_CLASS(UUCScreenMotionBlur,UCameraEffect,0,UCGameplay);

public:

	FRenderTarget*			BackBufferTarget;
	FRenderTarget*			AccumulationTarget;

	UPSMotionBlurMaterial*	PixelShaderMotionBlurMaterial;

	UUCScreenMotionBlur();

	// UObject interface.
	void Destroy();

	// UCameraEffect interface.
	virtual void PreRender(UViewport* Viewport,FRenderInterface* RI);
	virtual void PostRender(UViewport* Viewport,FRenderInterface* RI);
	virtual void DestroyRenderTargets();
};

/* scion ======================================================================
* Class: UUCScreenHeatShimmer
* Author: sz
* 
* see UCScreenHeatShimmer.uc 
* =============================================================================
*/
class UCGAMEPLAY_API UUCScreenHeatShimmer : public UCameraEffect
{
	DECLARE_CLASS(UUCScreenHeatShimmer,UCameraEffect,0,UCGameplay);

public:
	FRenderTarget*				BackBufferTarget;
	FRenderTarget*				OffscreenTarget;

	UProxyBitmapMaterial*		BackBufferMaterial;
	UProxyBitmapMaterial*		OffscreenMaterial;

	UPSDistortionMaterial*		PixelShaderDistortionMaterial;

	UUCScreenHeatShimmer();

	// UObject interface.
	void Destroy();

	// UCameraEffect interface.
	virtual void PreRender(UViewport* Viewport,FRenderInterface* RI);
	virtual void PostRender(UViewport* Viewport,FRenderInterface* RI);
};

/* scion ======================================================================
* Class: UUCScreenRadialBlur
* Author: sz
* 
* see UCScreenRadialBlur.uc 
* =============================================================================
*/
class UCGAMEPLAY_API UUCScreenRadialBlur : public UCameraEffect
{
	DECLARE_CLASS(UUCScreenRadialBlur,UCameraEffect,0,UCGameplay);

public:

	FRenderTarget*			BackBufferTarget;
	FRenderTarget*			AccumulationTarget;

	UPSMotionBlurMaterial*	PixelShaderMotionBlurMaterial;

	FLOAT					RadialBlurDistance;
	INT						NumRadialPolys;

	UUCScreenRadialBlur();	

	// UObject interface.
	void Destroy();

	// UCameraEffect interface.
	virtual void PreRender(UViewport* Viewport,FRenderInterface* RI);
	virtual void PostRender(UViewport* Viewport,FRenderInterface* RI);
};

/* scion ======================================================================
* Class: UUCScreenEmissive
* Author: sz
* 
* see UCScreenEmissive.uc 
* =============================================================================
*/
class UCGAMEPLAY_API UUCScreenEmissive : public UCameraEffect
{
	DECLARE_CLASS(UUCScreenEmissive,UCameraEffect,0,UCGameplay);

public:

	FRenderTarget*					BackBufferTarget;
	FRenderTarget*					SelectTarget;
	TArrayNoInit<FRenderTarget*>	BlurTargets;

	FLOAT BlurTargetScaleX, BlurTargetScaleY;

	INT	NumBlurTargets;

	FLOAT FalloffRate;

	BITFIELD DisplayOutput:1 GCC_PACK(4);

	INT NumFeedbackPasses;

	UUCScreenEmissive();
	FRenderTarget* GetEmissiveResultTarget();

	// UObject interface.
	void Destroy();

	// UCameraEffect interface.
	virtual void PreRender(UViewport* Viewport,FRenderInterface* RI);
	virtual void PostRender(UViewport* Viewport,FRenderInterface* RI);
	virtual void DestroyRenderTargets();
};

/* scion ======================================================================
* Class: UUCScreenBloom
* Author: sz
* 
* see UCScreenBloom.uc 
* =============================================================================
*/
class UCGAMEPLAY_API UUCScreenBloom : public UUCScreenEmissive
{
	DECLARE_CLASS(UUCScreenBloom,UUCScreenEmissive,0,UCGameplay);
public:

	UPSBloomSelectMaterial*	PixelShaderBloomMaterial;
	FRenderTarget*	EmissiveResultTarget;

	UUCScreenBloom();

	// UObject interface.
	void Destroy();

	// UCameraEffect interface.
	virtual void PreRender(UViewport* Viewport,FRenderInterface* RI);
	virtual void PostRender(UViewport* Viewport,FRenderInterface* RI);
	virtual void DestroyRenderTargets();
};

/* scion ======================================================================
* Class: UUCScreenEmissiveWithBloom
* Author: sz
* 
* see UCScreenEmissiveWithBloom.uc 
* =============================================================================
*/
class UCGAMEPLAY_API UUCScreenEmissiveWithBloom : public UCameraEffect
{
	DECLARE_CLASS(UUCScreenEmissiveWithBloom,UCameraEffect,0,UCGameplay);
public:

	UUCScreenEmissive*				EmissiveCamEffect;
	UUCScreenBloom*					BloomCamEffect;

	UUCScreenEmissiveWithBloom();

	// UObject interface.
	void Destroy();

	// UCameraEffect interface.
	virtual void PreRender(UViewport* Viewport,FRenderInterface* RI);
	virtual void PostRender(UViewport* Viewport,FRenderInterface* RI);
	virtual void DestroyRenderTargets();
};


