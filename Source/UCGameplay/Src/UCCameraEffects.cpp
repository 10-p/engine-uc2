/*=============================================================================
	UCCameraEffects.cpp:
	Copyright 2003 Scion Studios. All Rights Reserved.
=============================================================================*/

#include "UCGameplay.h"

IMPLEMENT_CLASS(UUCScreenBlur);
IMPLEMENT_CLASS(UUCScreenDOF);
IMPLEMENT_CLASS(UUCScreenMotionBlur);
IMPLEMENT_CLASS(UUCScreenHeatShimmer);
IMPLEMENT_CLASS(UUCScreenRadialBlur);
IMPLEMENT_CLASS(UUCScreenBloom);
IMPLEMENT_CLASS(UUCScreenEmissive);
IMPLEMENT_CLASS(UUCScreenEmissiveWithBloom);

// jg -- Global UVs. Declare once, reuse many.
FLOAT U1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
FLOAT V1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
FLOAT U2[4] = {1.0f, 1.0f, 1.0f, 1.0f};
FLOAT V2[4] = {1.0f, 1.0f, 1.0f, 1.0f};

// jg -- Global FScreenPrimUtil. Prevents updating of the CanvasToScreen
// matrix often for effects that are always the same size
FScreenPrimUtil GScreenPrimUtil;
// scion sz - used a lot
FColor WhiteOpaque(255,255,255,255);


/* scion ======================================================================
* ::ViewportTransformTexMatrix
* Author: sz
* 
* Applies the viewport scale and offset to a material's texture matrix in 
* order to only use the portion of the material that the viewport overlaps. 
* ============================================================================
*/
FORCEINLINE void ViewportTransformTexMatrix( UTexMatrix* TexMatrix, UMaterial* Material, UViewport* Viewport )
{
	checkSlow( TexMatrix && Material && Viewport );

	TexMatrix->Matrix.M[0][0] = (FLOAT)Viewport->SizeX / Material->MaterialUSize() * Viewport->RenDev->FSAAScale[0];
	TexMatrix->Matrix.M[1][1] = (FLOAT)Viewport->SizeY / Material->MaterialVSize() * Viewport->RenDev->FSAAScale[1];
	TexMatrix->Matrix.M[3][0] = (FLOAT)Viewport->OffsetX / Material->MaterialUSize();
	TexMatrix->Matrix.M[3][1] = (FLOAT)Viewport->OffsetY / Material->MaterialVSize();
}

/* UC2 port (pp-scale) =======================================================
* The blur targets of the depth of field, the emissive glow and the bloom are
* sized for the reference picture (GetPostProcessReference, UnRenderUtil.h;
* docs/porting/analysis/pp-scale.md). The sizes are FLOAT products, which Epic
* passed to ObtainRenderTarget's INT parameters (truncated) but compared with
* the targets' INT sizes as FLOATs: wherever a product is not a whole number
* (1366x768 x 0.25 = 341.5, and most sizes of a 16:9 reference picture) the
* targets were released and obtained again every frame, and the emissive
* accumulation target (deleted and created again) lost its streaks. Both now
* use the truncated whole number. At 640x480 every product is whole: as before.
* ============================================================================
*/
static FORCEINLINE INT UCPP_TargetSize( FLOAT Size )
{
	return Max( (INT)Size, 1 );
}

// One log line whenever a blur target is (re)created: the evidence that the sizes follow the
// reference picture (tools/dev/pp-scale.sh reads it).
static void UCPP_LogTarget( const TCHAR* Effect, INT Index, FRenderTarget* Target, const FPostProcessReference& Reference, FRenderInterface* RI )
{
	if( !Target )
		return;
	debugf( NAME_Init, TEXT("UCCameraEffects: %s target %i: %ix%i (reference picture %.1fx%.1f, back buffer %ix%i)"),
		Effect, Index, Target->GetWidth(), Target->GetHeight(), Reference.Width, Reference.Height,
		RI->GetRenderTarget()->GetWidth(), RI->GetRenderTarget()->GetHeight() );
}


/* scion ======================================================================
 * UUCScreenBlur::PostRender
 * Author: jg/sz
 * 
 * Does a full screen blur with the blur material for the blur time. The time
 * is decremented each frame. On zero it disables the effect.
 *
 * input:	Viewport -- The viewport we are rendering in
 *			RI -- The render interface to render with
 *
 * ============================================================================
 */
void UUCScreenBlur::PostRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenBlur::PostRender);
#ifndef _XBOX
	// UC2 port (M7): Epic's "return" for every platform but Xbox; the rest of the function
	// runs when the render device can post-process (FRenderCaps::XboxPostProcess).
	if( !RenDevHasXboxPostProcess() )
		return;
#endif
	// Bail if we are not rendering any more. Disable to prevent further calls
	if (BlurMaterial == NULL || BlurTime <= 0.f)
	{
		Enabled = 0;
		return;
	}
	// -- render blurred screen back to the back buffer	
	DECLARE_STATIC_UOBJECT( UProxyBitmapMaterial, BlurSourceMaterial, {} );
	BlurSourceMaterial->SetTextureInterface( RI->GetRenderTarget() );
	DECLARE_STATIC_UOBJECT(	UTexMatrix,	BlurSourceMaterialTexMatrix, 
	{
		BlurSourceMaterialTexMatrix->Matrix.SetIdentity();
	} );
	BlurSourceMaterialTexMatrix->Material = BlurSourceMaterial;
	// scion sz - only use the portion of the backbuffer that overlaps the viewport
	ViewportTransformTexMatrix( BlurSourceMaterialTexMatrix, BlurSourceMaterial, Viewport );
	// Save this so we don't affect other materials
	UMaterial* pOld = BlurMaterial->BlurSourceMaterial;
	// Set the back buffer as our material
	BlurMaterial->BlurSourceMaterial = BlurSourceMaterialTexMatrix;
	// jg -- Use the global screen prim so we don't have to recalc the
	// canvas to screen matrix unless the size changed
	GScreenPrimUtil.Update(Viewport->SizeX,Viewport->SizeY,RI);
	GScreenPrimUtil.DrawTile(0.0f, 0.0f, Viewport->SizeX, Viewport->SizeY,
		U1, V1,	U2, V2,0.0f,BlurMaterial,WhiteOpaque);
	GScreenPrimUtil.Flush();
	// Restore the previous material
	BlurMaterial->BlurSourceMaterial = pOld;
	// See if this is the first frame of this effect
	if (LastBlurTime == 0.f)
	{
		LastBlurTime = Viewport->Actor->Level->TimeSeconds;
	}
	// Update the time counters
	FLOAT flDelta = Viewport->Actor->Level->TimeSeconds - LastBlurTime;
	// Subtract out the time change
	BlurTime -= flDelta;
	// Update the last time for delta calculation
	LastBlurTime = Viewport->Actor->Level->TimeSeconds;
	unguard;    
}


//jg -- Intialize the DOF statics
UPSDepthOfFieldMaterial* UUCScreenDOF::SharedPixelShaderDOFMaterial = NULL;
// Ref count for allocating/freeing the above resources
DWORD UUCScreenDOF::DOFResourceRefCount = 0;

//
//	UUCScreenDOF::UUCScreenDOF
//
UUCScreenDOF::UUCScreenDOF()
{
	guard(UUCScreenDOF::UUCScreenDOF);
#ifdef _XBOX
	InitSharedDOFResources();
#else
	// UC2 port (M7): Epic's Xbox-only construction (the shared material and its reference
	// count). The render device may not exist yet when an effect object is constructed, and
	// its capability can be switched while the game runs, so PreRender does it on first use
	// instead (InitSharedDOFResources below; PixelShaderDOFMaterial says whether this object
	// holds a reference). Without the capability nothing is allocated, as before.
	PixelShaderDOFMaterial = NULL;
	if( RenDevHasXboxPostProcess() )
		InitSharedDOFResources();
#endif
	unguard;
}

//
//	UUCScreenDOF::InitSharedDOFResources: the body of Epic's Xbox constructor.
//
void UUCScreenDOF::InitSharedDOFResources()
{
	guard(UUCScreenDOF::InitSharedDOFResources);
	//jg -- Only initialize these once
	if (DOFResourceRefCount == 0)
	{
		check(SharedPixelShaderDOFMaterial == NULL);
		SharedPixelShaderDOFMaterial = ConstructObject<UPSDepthOfFieldMaterial>( UPSDepthOfFieldMaterial::StaticClass() );		
	}
	if( BlurTargets.Num() < 2 )
		BlurTargets.AddZeroed( 2 - BlurTargets.Num() );
	// Keep this update to date with the number of instances
	DOFResourceRefCount++;
	// Get a pointer to the one and only material
	PixelShaderDOFMaterial = SharedPixelShaderDOFMaterial;
	PixelShaderDOFMaterial->AlphaRef = AlwaysOnAlphaRef;
	unguard;
}

//
//	UUCScreenDOF::Destroy
//
void UUCScreenDOF::Destroy()
{
	guard(UUCScreenDOF::Destroy);
	Super::Destroy();
#ifndef _XBOX
	// UC2 port (M7): only an object that took its reference (see the constructor).
	if( PixelShaderDOFMaterial != NULL )
#endif
	{
	//jg -- Keep this update to date with the number of instances
	check(DOFResourceRefCount > 0);
	DOFResourceRefCount--;
	// Clean up if there are no more DOF objects
	if (DOFResourceRefCount == 0)
	{
		if( SharedPixelShaderDOFMaterial )
		{
			delete SharedPixelShaderDOFMaterial;
			SharedPixelShaderDOFMaterial = NULL;
		}		
	}
	PixelShaderDOFMaterial = NULL;
	DestroyRenderTargets();
	}
	unguard;
}

//
//	UUCScreenDOF::DestroyRenderTargets
//
void UUCScreenDOF::DestroyRenderTargets()
{
	for( INT i=0; i < BlurTargets.Num(); i++ )
	{
		if( BlurTargets(i) )
		{
			BlurTargets(i)->RemovePoolRef();
			BlurTargets(i) = NULL;
		}
	}	
}

//
//	UUCScreenDOF::PreRender
//
void UUCScreenDOF::PreRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenDOF::PreRender);

#ifndef _XBOX
	// UC2 port (M7): Epic's "return" for every platform but Xbox; the rest of the function
	// runs when the render device can post-process (FRenderCaps::XboxPostProcess).
	if( !RenDevHasXboxPostProcess() )
		return;
#endif

#ifndef _XBOX
	// UC2 port (M7): see the constructor.
	if( PixelShaderDOFMaterial == NULL )
		InitSharedDOFResources();
#endif

	// use default render target for source
	BackBufferTarget = RI->GetRenderTarget();
	
	checkSlow( BlurTargets.Num() >= 2 );

	// UC2 port (pp-scale): the sizes are fractions of the reference picture (480 lines, the back
	// buffer's aspect) instead of the back buffer, so the blur covers the same share of the picture
	// as on the Xbox at any resolution; Epic's: RI->GetRenderTarget()->GetWidth() * BlurTargetScaleX
	// (GetPostProcessReference gives exactly that without a reference height).
	FPostProcessReference Reference = GetPostProcessReference( RI->GetRenderTarget()->GetWidth(), RI->GetRenderTarget()->GetHeight() );
	FLOAT BlurSizeX = Reference.Width * BlurTargetScaleX * Viewport->ScaleX * RenDevPostProcessTargetScale();	// UC2 port (H-1): x PostProcessTargetScale
	FLOAT BlurSizeY = Reference.Height * BlurTargetScaleY * Viewport->ScaleY * RenDevPostProcessTargetScale();

	if( BlurTargets.Num() < 2 )
	{
		BlurTargets.AddZeroed( 2-BlurTargets.Num() );
	}

	for( INT i=0; i < BlurTargets.Num(); i++ )
	{	
		// even targets used for vertical blur
		// so we halve the vertical size
		if( i%2 == 0 )
		{
            BlurSizeY *= 0.5f;
		}
		// odd targets used for horizontal blur
		// so we halve the horizontal size
		else
		{
			BlurSizeX *= 0.5f;
		}

		// UC2 port (pp-scale): compared as the whole numbers the target is created with (see
		// UCPP_TargetSize); Epic compared the INT size with the FLOAT product.
		if( !BlurTargets(i) ||
			BlurTargets(i)->GetWidth() != UCPP_TargetSize(BlurSizeX) ||
			BlurTargets(i)->GetHeight() != UCPP_TargetSize(BlurSizeY) )
		{
			if( BlurTargets(i) )
			{
				BlurTargets(i)->RemovePoolRef();
				BlurTargets(i) = NULL;
			}

			// obtain/create the render target
			BlurTargets(i) = RI->GetRenderTargetPool()->ObtainRenderTarget(UCPP_TargetSize(BlurSizeX), UCPP_TargetSize(BlurSizeY), TEXF_RGBA8,
				FRenderTarget::RT_CREATE_COLOR );
			UCPP_LogTarget( TEXT("UCScreenDOF"), i, BlurTargets(i), Reference, RI );
		}
	}

	unguard;
}

//
//	UUCScreenDOF::PostRender
//
void UUCScreenDOF::PostRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenDOF::PostRender);

#ifndef _XBOX
	// UC2 port (M7): Epic's "return" for every platform but Xbox; the rest of the function
	// runs when the render device can post-process (FRenderCaps::XboxPostProcess).
	if( !RenDevHasXboxPostProcess() )
		return;
#endif

	checkSlow( PixelShaderDOFMaterial );

	switch( FocusType )
	{
	case DOF_FocusActor:
		HandleFocusActor( Viewport, RI );
		break;
	case DOF_FocusDistance:
		HandleFocusDistance( Viewport, RI );
		break;
	case DOF_FocusAbsoluteZDepth:
	{
		// Only update this once since has no dynamic component
		if (bNeedsAbsoluteUpdate)
		{
			PixelShaderDOFMaterial->bUpdateRampLookup = 1;
			PixelShaderDOFMaterial->FocusNear = StartZ;
			bNeedsAbsoluteUpdate = 0;
			ModulateBlurColor.A = AbsoluteBlurAlpha;
		}
#ifndef _XBOX
		// UC2 port (M7), for the owner's review (docs/porting/analysis/runtime-port.md,
		// "Deviation"): script puts ThirdPersonAlpha (100) or FirstPersonAlpha (65) into
		// AbsoluteBlurAlpha per camera mode. Epic's header had fallen behind the script, so
		// the shipped binary read the byte at FirstPersonAlpha's place here: always 65. We
		// read what script intends; a device can ask for the shipped value
		// (FRenderCaps::ShippedDOFBlurAlpha; UC2GLDrv: DOFShippedBlurAlpha / DOFSHIPPEDALPHA).
		// Set on every frame so that the switch takes effect at once.
		ModulateBlurColor.A = Viewport->RenDev->GetRenderCaps()->ShippedDOFBlurAlpha ? FirstPersonAlpha : AbsoluteBlurAlpha;
#endif
		break;
	}
	case DOF_Manual:
	default:
		break;
	}

	checkSlow( BlurTargets.Num() >= 2 );

	static FMultiSampleFilter SamplerVertBlur( FMultiSampleFilter::MULTI_SAMPLE_VERT_1331 );
	static FMultiSampleFilter SamplerHorizBlur( FMultiSampleFilter::MULTI_SAMPLE_HORIZ_1331 );
	FRenderTarget* SrcTarget = BackBufferTarget;
	for( INT i=0; i < BlurTargets.Num(); i++ )
	{
		// even targets used for vertical blur
		if( i%2 == 0 )
		{
			// down sample in the vertical direction and use a vertical blur filter
			FilterCopyRenderTarget( BlurTargets(i), SrcTarget, Viewport, SamplerVertBlur, RI );
		}
		// odd targets used for horizontal blur
		else
		{
			// down sample in the horizontal direction and use a horizontal blur filter
			FilterCopyRenderTarget( BlurTargets(i), SrcTarget, NULL, SamplerHorizBlur, RI, NULL, NULL, 0, RenDevPostProcessTargetScale() );	// UC2 port (H-1): TapScale
		}

		SrcTarget = BlurTargets(i);
	}

	// setup depth buffer materail
	DECLARE_STATIC_UOBJECT(	UProxyBitmapMaterial, DepthBufferMaterial, {});
	DepthBufferMaterial->SetTextureInterface( BackBufferTarget );
	DECLARE_STATIC_UOBJECT(	UTexMatrix,	DepthBufferTexMatrix, 
	{
		DepthBufferTexMatrix->Matrix.SetIdentity();
		DepthBufferTexMatrix->Material = DepthBufferMaterial;
	}
	);
	// scion sz - only use the portion of the backbuffer that overlaps the viewport
	ViewportTransformTexMatrix( DepthBufferTexMatrix, DepthBufferMaterial, Viewport );

	// setup blurred back buffer material
	DECLARE_STATIC_UOBJECT(	UProxyBitmapMaterial, BlurTargetMaterial, {});
	BlurTargetMaterial->SetTextureInterface( BlurTargets(1) );
	DECLARE_STATIC_UOBJECT(	UTexMatrix,	BlurTargetTexMatrix, 
	{
		BlurTargetTexMatrix->Matrix.SetIdentity();
		BlurTargetTexMatrix->Material = BlurTargetMaterial;
	}
	);	

//	// setup twice blurred back buffer material
//	DECLARE_STATIC_UOBJECT(	UProxyBitmapMaterial, BlurMoreTargetMaterial, {});
//	BlurMoreTargetMaterial->SetTextureInterface( BlurTargets(BlurTargets.Num()-1) );
//	DECLARE_STATIC_UOBJECT(	UTexMatrix,	BlurMoreTargetTexMatrix, 
//	{
//		BlurMoreTargetTexMatrix->Matrix.SetIdentity();
//		BlurMoreTargetTexMatrix->Material = BlurMoreTargetMaterial;
//	}
//	);	

#ifndef FINAL_RELEASE
	static UBOOL bSamDebugDOF=0;
	if( bDebugFocusRange || bSamDebugDOF )
		RI->Clear(1, FColor(0,255,0), 0, 0.0f, 0, 0 );		// sz - for debug
#endif

	// use depth from render target as a color texture
	BackBufferTarget->SetMaterialUsage( FRenderTarget::RT_MATERIAL_USE_DEPTHBUFFER );

	// setup the pixel shader used to apply the actual DOF effect based on z-buffer values
	PixelShaderDOFMaterial->BlurMaterial = BlurTargetTexMatrix;
	PixelShaderDOFMaterial->DepthBufferMaterial = DepthBufferTexMatrix;
// 	PixelShaderDOFMaterial->BlurMoreMaterial = BlurMoreTargetTexMatrix;

	RI->UpdateDOFRampLookupTexture( PixelShaderDOFMaterial, FocusType );

	//scion jg -- Only use the mask when specified. This lets us turn off the
	// effect for matinee sequences
	if (bUseStencilMask == 1)
	{
		RI->SetStencilOp(CF_NotEqual,0xBE,0xFF,SO_Keep,SO_Keep,SO_Keep,
			0xFF);
	}

	FColor BlurColor(ModulateBlurColor);
	BlurColor.A = (BYTE)(BlurColor.A*Alpha);
	// jg -- Use the global screen prim so we don't have to recalc the
	// canvas to screen matrix unless the size changed
	GScreenPrimUtil.Update(Viewport->SizeX,Viewport->SizeY,RI);
	GScreenPrimUtil.DrawTile(
		0, 0, Viewport->SizeX, Viewport->SizeY,
		U1, V1,	U2, V2,
		0.0f,
		PixelShaderDOFMaterial,
		BlurColor
		);
	GScreenPrimUtil.Flush();
	
	if (bUseStencilMask == 1)
	{
		RI->ResetStencilOp();
	}

#ifdef _XBOX
	//scion jg -- Make the GPU release its hold on the DOFRampTexture sooner
	RI->InsertFence();
#endif

	// restore render target usage to color mode
	BackBufferTarget->SetMaterialUsage( FRenderTarget::RT_MATERIAL_USE_BACKBUFFER );

	unguard;    
}

//
//	UUCScreenDOF::HandleFocusActor
//
void UUCScreenDOF::HandleFocusActor(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenDOF::HandleFocusActor);

	if( !FocusActor )
		return;

	FCoords Coords = GMath.UnitCoords / Viewport->Actor->Rotation;
	PixelShaderDOFMaterial->FocusWorldNear = FocusActor->Location - (Coords.XAxis * FocusRadius);
	PixelShaderDOFMaterial->FocusWorldFar = FocusActor->Location + (Coords.XAxis * FocusRadius);
	PixelShaderDOFMaterial->bUpdateRampLookup = true;

	unguard;
}

//
//	UUCScreenDOF::HandleFocusDistance
//
void UUCScreenDOF::HandleFocusDistance(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenDOF::HandleFocusDistance);
	
	const FCoords& Coords = GMath.UnitCoords / Viewport->Actor->Rotation;
	//scion jg -- Figure out if this will end up behind the camera
	FLOAT flFront = FocusDistance - FocusRadius;
	if (flFront > 0.f)
	{
		PixelShaderDOFMaterial->FocusWorldNear = Viewport->Actor->Location +
			(Coords.XAxis * flFront);
		PixelShaderDOFMaterial->FocusWorldFar = Viewport->Actor->Location +
			(Coords.XAxis * (FocusDistance+FocusRadius));
	}
	else
	{
		//scion jg -- The near value is behind the camera so adjust the near
		// and far so that the midpoint is still our focus zone and add a
		// little extra to keep it from being behind the near clip plane
		PixelShaderDOFMaterial->FocusWorldNear = Viewport->Actor->Location +
			(Coords.XAxis * 100.f);
		PixelShaderDOFMaterial->FocusWorldFar = Viewport->Actor->Location +
			(Coords.XAxis * (FocusDistance + FocusRadius - flFront + 100.f));
	}
	PixelShaderDOFMaterial->bUpdateRampLookup = true;

	unguard;
}

//
//	UUCScreenMotionBlur::UUCScreenMotionBlur
//
UUCScreenMotionBlur::UUCScreenMotionBlur()
{
	guard(UUCScreenMotionBlur::UUCScreenMotionBlur);

	AccumulationTarget = NULL;

	unguard;
}

//
//	UUCScreenMotionBlur::Destroy
//
void UUCScreenMotionBlur::Destroy()
{
	guard(UUCScreenMotionBlur::Destroy);
	Super::Destroy();

	DestroyRenderTargets();

	unguard;
}

//
//	UUCScreenMotionBlur::DestroyRenderTargets
//
void UUCScreenMotionBlur::DestroyRenderTargets()
{
	if( AccumulationTarget )
	{
		delete AccumulationTarget;
		AccumulationTarget = NULL;
	}
}

//
//	UUCScreenMotionBlur::PreRender
//
void UUCScreenMotionBlur::PreRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenMotionBlur::PreRender);
#ifndef _XBOX
	// UC2 port (M7): Epic's "return" for every platform but Xbox; the rest of the function
	// runs when the render device can post-process (FRenderCaps::XboxPostProcess).
	if( !RenDevHasXboxPostProcess() )
		return;
#endif

	// -- use default render target for source
	BackBufferTarget = RI->GetRenderTarget();

	if( !AccumulationTarget )
	{
		AccumulationTarget = NEW FAuxRenderTarget( 
			RI->GetRenderTarget()->GetWidth() * Viewport->ScaleX, 
			RI->GetRenderTarget()->GetHeight() * Viewport->ScaleY, 
			TEXF_RGBA8, 
			FRenderTarget::RT_CREATE_COLOR );
	}

	unguard;
}

//
//	UUCScreenMotionBlur::PostRender
//
void UUCScreenMotionBlur::PostRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenMotionBlur::PostRender);
#ifndef _XBOX
	// UC2 port (M7): Epic's "return" for every platform but Xbox; the rest of the function
	// runs when the render device can post-process (FRenderCaps::XboxPostProcess).
	if( !RenDevHasXboxPostProcess() )
		return;
#endif

	if( !PixelShaderMotionBlurMaterial )
		return;

	DECLARE_STATIC_UOBJECT( UProxyBitmapMaterial, SourceMaterial, {} );
	DECLARE_STATIC_UOBJECT( UProxyBitmapMaterial, TargetMaterial, {} );

	SourceMaterial->SetTextureInterface( BackBufferTarget );
	TargetMaterial->SetTextureInterface( AccumulationTarget );

	// -- basic blur comes from just blending a faded quad of the current back buffer into
	// an offscreen accumulation buffer each frame.  Never clearing the accumulation buffer.
	// Then just render the accumulation buffer to the screen.

	DECLARE_STATIC_UOBJECT(	UTexMatrix,	SourceMaterialTexMatrix, 
	{
		SourceMaterialTexMatrix->Matrix.SetIdentity();
		SourceMaterialTexMatrix->Material = SourceMaterial;
	}
	);
	// scion sz - only use the portion of the backbuffer that overlaps the viewport
	ViewportTransformTexMatrix( SourceMaterialTexMatrix, SourceMaterial, Viewport );

	DECLARE_STATIC_UOBJECT(	UTexMatrix,	TargetMaterialTexMatrix, 
	{
		TargetMaterialTexMatrix->Matrix.SetIdentity();
		TargetMaterialTexMatrix->Material = TargetMaterial;
	} 
	);	

	if( !PixelShaderMotionBlurMaterial->BackBufferModifier )
	{
		PixelShaderMotionBlurMaterial->BackBufferModifier = SourceMaterialTexMatrix;
	}
	else
	{
		PixelShaderMotionBlurMaterial->BackBufferModifier->Material = SourceMaterial;
	}

	RI->SetRenderTarget( AccumulationTarget );    

    BYTE FallofColor = PixelShaderMotionBlurMaterial->FalloffRate*255;

	if( Reset )
	{
        FallofColor=255;
		Reset=0;
	}

	// -- draw full screen quad into Accumulation Target
	FScreenPrimUtil( AccumulationTarget, RI ).DrawTile(
		0, 0, AccumulationTarget->GetWidth(), AccumulationTarget->GetHeight(),
		U1, V1,	U2, V2,
		0.0f,
		PixelShaderMotionBlurMaterial,
		FColor(255,255,255,FallofColor*Alpha)
		);

	//scion jg -- Pop the render target change
	RI->ResetRenderTarget();

	DECLARE_STATIC_UOBJECT( UPixelShaderMaterial, FinalMaskBlend,
	{
		FinalMaskBlend->PixelShaderType = PS_Mask;
		FinalMaskBlend->TextureStage0 = TargetMaterialTexMatrix;
		FinalMaskBlend->TextureStage1 = PixelShaderMotionBlurMaterial->FinalMask;
	}
	);

	UMaterial* FinalMaterial = TargetMaterialTexMatrix;
	if( PixelShaderMotionBlurMaterial->FinalMask )
	{
		FinalMaterial = FinalMaskBlend;
	}	

	// -- draw full screen quad into Back Buffer Target
	// jg -- Use the global screen prim so we don't have to recalc the
	// canvas to screen matrix unless the size changed
	GScreenPrimUtil.Update(Viewport->SizeX,Viewport->SizeY,RI);
	GScreenPrimUtil.DrawTile(
		0, 0, Viewport->SizeX, Viewport->SizeY,
		U1, V1,	U2, V2,
		0.0f,
		FinalMaterial,
		WhiteOpaque
		);
	GScreenPrimUtil.Flush();

	unguard;
}

//
//	UUCScreenHeatShimmer::UUCScreenHeatShimmer
//
UUCScreenHeatShimmer::UUCScreenHeatShimmer()
{
	guard(UUCScreenHeatShimmer::UUCScreenHeatShimmer);
	unguard;
}

//
//	UUCScreenHeatShimmer::Destroy
//
void UUCScreenHeatShimmer::Destroy()
{
	guard(UUCScreenHeatShimmer::Destroy);
	Super::Destroy();
	unguard;
}

//
//	UUCScreenHeatShimmer::PreRender
//
void UUCScreenHeatShimmer::PreRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenHeatShimmer::PreRender);
#ifndef _XBOX
	// UC2 port (M7): Epic's "return" for every platform but Xbox; the rest of the function
	// runs when the render device can post-process (FRenderCaps::XboxPostProcess).
	if( !RenDevHasXboxPostProcess() )
		return;
#endif

	unguard;
}

//
//	UUCScreenHeatShimmer::PostRender
//
void UUCScreenHeatShimmer::PostRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenHeatShimmer::PostRender);
#ifndef _XBOX
	// UC2 port (M7): Epic's "return" for every platform but Xbox; the rest of the function
	// runs when the render device can post-process (FRenderCaps::XboxPostProcess).
	if( !RenDevHasXboxPostProcess() )
		return;
#endif

	if( !PixelShaderDistortionMaterial )
		return;

	// jg -- Use the global screen prim so we don't have to recalc the
	// canvas to screen matrix unless the size changed
	GScreenPrimUtil.Update(Viewport->SizeX,Viewport->SizeY,RI);
	GScreenPrimUtil.DrawTile(
		0, 0, Viewport->SizeX, Viewport->SizeY,
		U1, V1,	U2, V2,
		0.0f,
		PixelShaderDistortionMaterial,
		FColor(255,255,255,255*Alpha)
		);
	GScreenPrimUtil.Flush();

	unguard;
}

//
//	UUCScreenRadialBlur::UUCScreenRadialBlur
//
UUCScreenRadialBlur::UUCScreenRadialBlur()
{
	guard(UUCScreenRadialBlur::UUCScreenRadialBlur);
	unguard;
}

//
//	UUCScreenRadialBlur::Destroy
//
void UUCScreenRadialBlur::Destroy()
{
	guard(UUCScreenRadialBlur::Destroy);
	Super::Destroy();
	unguard;
}

//
//	UUCScreenRadialBlur::PreRender
//
void UUCScreenRadialBlur::PreRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenRadialBlur::PreRender);
#ifndef _XBOX
	// UC2 port (M7): Epic's "return" for every platform but Xbox; the rest of the function
	// runs when the render device can post-process (FRenderCaps::XboxPostProcess).
	if( !RenDevHasXboxPostProcess() )
		return;
#endif
	unguard;
}

//	
//	UUCScreenRadialBlur::PostRender
//
void UUCScreenRadialBlur::PostRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenRadialBlur::PostRender);
#ifndef _XBOX
	// UC2 port (M7): Epic's "return" for every platform but Xbox; the rest of the function
	// runs when the render device can post-process (FRenderCaps::XboxPostProcess).
	if( !RenDevHasXboxPostProcess() )
		return;
#endif

	if( !PixelShaderMotionBlurMaterial )
		return;

	UMaterial* SourceMaterial = RI->GetBackBufferMaterial();

	// -- basic blur comes from just blending a faded quad of the current back buffer into
	// an offscreen accumulation buffer each frame.  Never clearing the accumulation buffer.
	// Then just render the accumulation buffer to the screen.

	DECLARE_STATIC_UOBJECT(	UTexMatrix,	SourceMaterialTexMatrix, 
	{
		SourceMaterialTexMatrix->Matrix.SetIdentity();
		SourceMaterialTexMatrix->Material = SourceMaterial;
	}
	);
	// scion sz - only use the portion of the backbuffer that overlaps the viewport
	ViewportTransformTexMatrix( SourceMaterialTexMatrix, SourceMaterial, Viewport );

	if( !PixelShaderMotionBlurMaterial->BackBufferModifier )
	{
		PixelShaderMotionBlurMaterial->BackBufferModifier = SourceMaterialTexMatrix;
	}
	else
	{
		PixelShaderMotionBlurMaterial->BackBufferModifier->Material = SourceMaterial;
	}

	BYTE FallofColor = PixelShaderMotionBlurMaterial->FalloffRate*255;

	if( Reset )
	{
		FallofColor=255;
		Reset=0;
	}

	// 
	FLOAT RadialBlurDistanceX=RadialBlurDistance;
	FLOAT RadialBlurDistanceY=RadialBlurDistance;

	// UC2 port (pp-scale): RadialBlurDistance is the inset, in pixels of the Xbox's 640x480 screen,
	// of the zoomed copies drawn below: a zoom of 1 - 2d/640 across and 1 - 2d/480 down. The insets
	// are scaled so that both zoom factors stay the Xbox's at any resolution and aspect (across: the
	// screen's width over 4/3 of the reference height; down: its height over the reference height),
	// which puts every point of the picture's 4:3 middle exactly where the Xbox put it. 1 and 1 at
	// 640x480 and without a reference height (Epic's pixels). docs/porting/analysis/pp-scale.md.
	FLOAT InsetScaleX = 1.f, InsetScaleY = 1.f;
	INT ReferenceHeight = RenDevPostProcessReferenceHeight();
	if( ReferenceHeight > 0 )
	{
		InsetScaleX = RI->GetRenderTarget()->GetWidth() / (ReferenceHeight * 4.f / 3.f);
		InsetScaleY = RI->GetRenderTarget()->GetHeight() / (FLOAT)ReferenceHeight;
	}

	GScreenPrimUtil.Update( Viewport->SizeX, Viewport->SizeY, RI );

	// draw quad multiple times 
	for( INT i=1; i<=NumRadialPolys; i++ )
	{
		RadialBlurDistanceX = RadialBlurDistance/(FLOAT)i;
		RadialBlurDistanceY = RadialBlurDistance/(FLOAT)i;
		// scale by viewport size
		RadialBlurDistanceX *= Viewport->ScaleX * InsetScaleX;
		RadialBlurDistanceY *= Viewport->ScaleY * InsetScaleY;
		
		GScreenPrimUtil.DrawTile(
			RadialBlurDistanceX, RadialBlurDistanceY, Viewport->SizeX-RadialBlurDistanceX, Viewport->SizeY-RadialBlurDistanceY,
			U1, V1,	U2, V2,
			0.0f,
			PixelShaderMotionBlurMaterial,
			FColor(255,255,255,FallofColor*Alpha)
			);
	}

	GScreenPrimUtil.Flush();

	unguard;
}


//
//	UUCScreenBloom::UUCScreenBloom
//
UUCScreenBloom::UUCScreenBloom()
{
	guard(UUCScreenBloom::UUCScreenBloom);

	PixelShaderBloomMaterial = ConstructObject<UPSBloomSelectMaterial>(UPSBloomSelectMaterial::StaticClass());
	BlurTargets.AddZeroed( 2 );
	SelectTarget = NULL;
	
	unguard;
}

//
//	UUCScreenBloom::Destroy
//
void UUCScreenBloom::Destroy()
{
	guard(UUCScreenBloom::Destroy);
	Super::Destroy();

	if( PixelShaderBloomMaterial )
	{
		delete PixelShaderBloomMaterial;
		PixelShaderBloomMaterial = NULL;
	}

	DestroyRenderTargets();

	unguard;
}

//
//	UUCScreenBloom::DestroyRenderTargets
//
void UUCScreenBloom::DestroyRenderTargets()
{
	for( INT i=0; i < BlurTargets.Num(); i++ )
	{
		if( BlurTargets(i) )
			BlurTargets(i)->RemovePoolRef();
		BlurTargets(i) = NULL;
	}

	if( SelectTarget )
	{
		SelectTarget->RemovePoolRef();
		SelectTarget = NULL;
	}
}

//
//	UUCScreenBloom::PreRender
//
void UUCScreenBloom::PreRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenBloom::PreRender);
#ifndef _XBOX
	// UC2 port (M7): Epic's "return" for every platform but Xbox; the rest of the function
	// runs when the render device can post-process (FRenderCaps::XboxPostProcess).
	if( !RenDevHasXboxPostProcess() )
		return;
#endif

	// use default render target for source
	BackBufferTarget = RI->GetRenderTarget();

	checkSlow( BlurTargets.Num() == 2 );

	// UC2 port (pp-scale): fractions of the reference picture, compared as whole numbers (see
	// UCPP_TargetSize and UUCScreenDOF::PreRender).
	FPostProcessReference Reference = GetPostProcessReference( RI->GetRenderTarget()->GetWidth(), RI->GetRenderTarget()->GetHeight() );
	FLOAT BlurSizeX = Reference.Width * BlurTargetScaleX * Viewport->ScaleX * RenDevPostProcessTargetScale();	// UC2 port (H-1): x PostProcessTargetScale
	FLOAT BlurSizeY = Reference.Height * BlurTargetScaleY * Viewport->ScaleY * RenDevPostProcessTargetScale();

	// create a render target for selecting hot pixels
	// this render target is always 1/2 the size of the back buffer
	if( !SelectTarget ||
		SelectTarget->GetWidth() != UCPP_TargetSize(BlurSizeX) ||
		SelectTarget->GetHeight() != UCPP_TargetSize(BlurSizeY) )
	{
		if( SelectTarget )
		{
			SelectTarget->RemovePoolRef();
			SelectTarget = NULL;
		}
		// NOTE: this render target is also used by the BackBufferCopyTarget since they are both 320x240
		// purposefully creating a depth buffer here as well
		SelectTarget = RI->GetRenderTargetPool()->ObtainRenderTarget( UCPP_TargetSize(BlurSizeX), UCPP_TargetSize(BlurSizeY), TEXF_RGBA8 /*,FRenderTarget::RT_CREATE_COLOR*/ );
		UCPP_LogTarget( TEXT("UCScreenBloom select"), 0, SelectTarget, Reference, RI );
	}

	// 1/2 horizontal width for first render target (horizontal blur)
	BlurSizeX *= 0.5f;

	// 1st render target is used for vertical blur
	if( !BlurTargets(0) ||
		BlurTargets(0)->GetWidth() != UCPP_TargetSize(BlurSizeX) ||
		BlurTargets(0)->GetHeight() != UCPP_TargetSize(BlurSizeY) )
	{
		if( BlurTargets(0) )
		{
			BlurTargets(0)->RemovePoolRef();
			BlurTargets(0) = NULL;
		}

		BlurTargets(0) = RI->GetRenderTargetPool()->ObtainRenderTarget( UCPP_TargetSize(BlurSizeX), UCPP_TargetSize(BlurSizeY), TEXF_RGBA8, FRenderTarget::RT_CREATE_COLOR );
		UCPP_LogTarget( TEXT("UCScreenBloom"), 0, BlurTargets(0), Reference, RI );
	}

	// 1/2 vertical height for second render target (vertical blur)
	BlurSizeY *= 0.5f;

	// 2nd render target is used for horizontal blur
	if( !BlurTargets(1) ||
		BlurTargets(1)->GetWidth() != UCPP_TargetSize(BlurSizeX) ||
		BlurTargets(1)->GetHeight() != UCPP_TargetSize(BlurSizeY) )
	{
		if( BlurTargets(1) )
		{
			BlurTargets(1)->RemovePoolRef();
			BlurTargets(1) = NULL;
		}

		BlurTargets(1) = RI->GetRenderTargetPool()->ObtainRenderTarget( UCPP_TargetSize(BlurSizeX), UCPP_TargetSize(BlurSizeY), TEXF_RGBA8, FRenderTarget::RT_CREATE_COLOR );
		UCPP_LogTarget( TEXT("UCScreenBloom"), 1, BlurTargets(1), Reference, RI );
	}

	unguard;
}

//
//	UUCScreenBloom::PostRender
//
void UUCScreenBloom::PostRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenBloom::PostRender);
#ifndef _XBOX
	// UC2 port (M7): Epic's "return" for every platform but Xbox; the rest of the function
	// runs when the render device can post-process (FRenderCaps::XboxPostProcess).
	if( !RenDevHasXboxPostProcess() )
		return;
#endif

	checkSlow( PixelShaderBloomMaterial );
	checkSlow( SelectTarget && BlurTargets(0) && BlurTargets(1) );
	
	// force stencil off
	RI->EnableStencil( 0 );

	// Draw the hot portions of the back buffer into the SelectTarget.
	// Hot portions are found by using PixelShaderBloomMaterial to draw.

	DECLARE_STATIC_UOBJECT(	UProxyBitmapMaterial, BackBufferMaterial, {});
	BackBufferMaterial->SetTextureInterface( BackBufferTarget );

	DECLARE_STATIC_UOBJECT(	UTexMatrix,	SourceMaterialTexMatrix, 
	{
		SourceMaterialTexMatrix->Matrix.SetIdentity();
		SourceMaterialTexMatrix->Material = BackBufferMaterial;
	});
	// scion sz - only use the portion of the backbuffer that overlaps the viewport
	ViewportTransformTexMatrix( SourceMaterialTexMatrix, BackBufferMaterial, Viewport );

	// set render target to bloom selection target
	RI->SetRenderTarget( SelectTarget );

	// set threshold for selecting hot pixels
	PixelShaderBloomMaterial->LuminosityThreshold = RI->GetAverageSceneLuminosity();

	// draw hot portion into accumulation target using PixelShaderBloomMaterial
	PixelShaderBloomMaterial->SourceMaterial = SourceMaterialTexMatrix;	
	FScreenPrimUtil( SelectTarget, RI ).DrawTile(
		0, 0, SelectTarget->GetWidth(), SelectTarget->GetHeight(),
		U1, V1,	U2, V2,
		0.0f,
		PixelShaderBloomMaterial,
		WhiteOpaque
		);

	// add results from emissive pass if it exists
	if( EmissiveResultTarget )
	{
		DECLARE_STATIC_UOBJECT(	UProxyBitmapMaterial, EmissiveResultMaterial, {});
		EmissiveResultMaterial->SetTextureInterface( EmissiveResultTarget );
		DECLARE_STATIC_UOBJECT(	UTexMatrix,	EmissiveResultTexMatrix, {
			EmissiveResultTexMatrix->Matrix.SetIdentity();
			EmissiveResultTexMatrix->Material = EmissiveResultMaterial;
		});
		DECLARE_STATIC_UOBJECT(	UFinalBlend, EmissiveAccumBlend,
		{
			EmissiveAccumBlend->FrameBufferBlending = FB_Brighten;
			EmissiveAccumBlend->Material = EmissiveResultTexMatrix;
			EmissiveAccumBlend->ZWrite = 0;
			EmissiveAccumBlend->ZTest = 0;
		});
		FScreenPrimUtil( SelectTarget, RI ).DrawTile(
			0, 0, SelectTarget->GetWidth(), SelectTarget->GetHeight(),
			U1, V1,	U2, V2,
			0.0f,
			EmissiveAccumBlend,
			WhiteOpaque
			);
	}

	//scion jg -- Pop the render target change
	RI->ResetRenderTarget();

	// if the vertical and horizontal blurs are swapped (ordering) then the 160x240 RT from DOF
	// can be reused.  Saves memory.

	// 1st pass (horizontal blur)
	static FMultiSampleFilter SamplerHorizBlur( FMultiSampleFilter::MULTI_SAMPLE_HORIZ_1221 );
	FilterCopyRenderTarget( BlurTargets(0), SelectTarget, NULL, SamplerHorizBlur, RI, NULL, NULL, 0, RenDevPostProcessTargetScale() );	// UC2 port (H-1): TapScale

	// 1st pass (vertical blur)
	static FMultiSampleFilter SamplerVertBlur( FMultiSampleFilter::MULTI_SAMPLE_VERT_1221 );
	FilterCopyRenderTarget( BlurTargets(1), BlurTargets(0), NULL, SamplerVertBlur, RI, NULL, NULL, 0, RenDevPostProcessTargetScale() );	// UC2 port (H-1): TapScale

	//
	// add results from blur to frame buffer
	DECLARE_STATIC_UOBJECT(	UProxyBitmapMaterial, BlurTargetMaterial, {});
	BlurTargetMaterial->SetTextureInterface( BlurTargets(1) );

	DECLARE_STATIC_UOBJECT(	UTexMatrix,	FinalBlendTexMatrix, 
	{
		FinalBlendTexMatrix->Matrix.SetIdentity();
		FinalBlendTexMatrix->Material = BlurTargetMaterial;
	} );

	DECLARE_STATIC_UOBJECT(	UFinalBlend, BlurBlend,
	{
		BlurBlend->FrameBufferBlending = FB_Brighten;
		BlurBlend->Material = FinalBlendTexMatrix;
		BlurBlend->ZWrite = 0;
		BlurBlend->ZTest = 0;
	}
	);

#ifndef FINAL_RELEASE
	static UBOOL bDebugClearBack=0;
	if( bDebugClearBack )
		RI->Clear(1, FColor(0,0,0), 0, 0.0f, 0, 0 );		// sz - for debug
#endif

	// jg -- Use the global screen prim so we don't have to recalc the
	// canvas to screen matrix unless the size changed
	GScreenPrimUtil.Update(Viewport->SizeX,Viewport->SizeY,RI);
	GScreenPrimUtil.DrawTile(
		0, 0, Viewport->SizeX, Viewport->SizeY,
		U1, V1,	U2, V2,
		0.0f,
		BlurBlend,
		FColor(255,255,255,128*Alpha)
		);
	GScreenPrimUtil.Flush();

	// force stencil back on
	RI->EnableStencil( 1 );

	unguard;
}

/* scion ======================================================================
* UUCScreenEmissive::UUCScreenEmissive
* Author: sz
* 
* =============================================================================
*/
UUCScreenEmissive::UUCScreenEmissive()
{
	guard(UUCScreenEmissive::UUCScreenEmissive);

	SelectTarget = NULL;

	unguard;
}

/* scion ======================================================================
* UUCScreenEmissive::Destroy
* Author: sz
* 
* =============================================================================
*/
void UUCScreenEmissive::Destroy()
{
	guard(UUCScreenEmissive::Destroy);

	Super::Destroy();
	DestroyRenderTargets();

	unguard;
}

/* scion ======================================================================
* UUCScreenEmissive::DestroyRenderTargets
* Author: sz
* 
* Clean up render targets we created and the ones we referenced from the 
* render target pool.
*
* =============================================================================
*/
void UUCScreenEmissive::DestroyRenderTargets()
{
	// remove blur targets from render target pool
	for( INT i=0; i < BlurTargets.Num(); i++ )
	{
		if( BlurTargets(i) )
			BlurTargets(i)->RemovePoolRef();
		BlurTargets(i) = NULL;
	}

	// remove the accumulation target we created
	if( SelectTarget )
	{
		delete SelectTarget;
		SelectTarget = NULL;
	}
}

/* scion ======================================================================
* UUCScreenEmissive::PreRender
* Author: sz
* 
* Convenient place to make sure the blur targets and the accumulation target
* have been created.
*
* =============================================================================
*/
void UUCScreenEmissive::PreRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenEmissive::PreRender);
#ifndef _XBOX
	// UC2 port (M7): Epic's "return" for every platform but Xbox; the rest of the function
	// runs when the render device can post-process (FRenderCaps::XboxPostProcess).
	if( !RenDevHasXboxPostProcess() )
		return;
#endif

	// use default render target for source
	BackBufferTarget = RI->GetRenderTarget();

	// get pointer to render target pool
	FRenderTargetPool* RenderTargetPool = RI->GetRenderTargetPool();

	// UC2 port (pp-scale): fractions of the reference picture, compared as whole numbers (see
	// UCPP_TargetSize and UUCScreenDOF::PreRender).
	FPostProcessReference Reference = GetPostProcessReference( BackBufferTarget->GetWidth(), BackBufferTarget->GetHeight() );
	FLOAT BlurSizeX = Reference.Width * BlurTargetScaleX * Viewport->ScaleX * RenDevPostProcessTargetScale();	// UC2 port (H-1): x PostProcessTargetScale
	FLOAT BlurSizeY = Reference.Height * BlurTargetScaleY * Viewport->ScaleY * RenDevPostProcessTargetScale();

	// Accumulation target (check for sizeX/sizeY changes)
	if( !SelectTarget ||
		SelectTarget->GetWidth() != UCPP_TargetSize(BlurSizeX) ||
		SelectTarget->GetHeight() != UCPP_TargetSize(BlurSizeY) )
	{
		if( SelectTarget )
			delete SelectTarget;

		// create a unique target since we want to accumulate streaks across frames
		SelectTarget = NEW FAuxRenderTarget( UCPP_TargetSize(BlurSizeX), UCPP_TargetSize(BlurSizeY), TEXF_RGBA8, FRenderTarget::RT_CREATE_COLOR );
		UCPP_LogTarget( TEXT("UCScreenEmissive select"), 0, SelectTarget, Reference, RI );
	}

	// initialize blur render targets (each one is half the size of the previous)
	if( BlurTargets.Num() != NumBlurTargets )
	{
		BlurTargets.Empty();
		BlurTargets.AddZeroed( NumBlurTargets );
	}	
	for( INT i=0; i < BlurTargets.Num(); i++ )
	{	
		// first blur target is just a copy of the accumulation target
		if( i==0 )		
		{
			BlurSizeX = SelectTarget->GetWidth();
			BlurSizeY = SelectTarget->GetHeight();
		}
		// odd targets used for vertical blur
		// so we halve the vertical size
		else if( i%2 == 1 )
		{
            BlurSizeY *= 0.5f;
		}
		// even targets used for horizontal blur
		// so we halve the horizontal size
		else
		{
			BlurSizeX *= 0.5f;
		}
		
		if( !BlurTargets(i) ||
			BlurTargets(i)->GetWidth() != UCPP_TargetSize(BlurSizeX) ||
			BlurTargets(i)->GetHeight() != UCPP_TargetSize(BlurSizeY) )
		{
			if( BlurTargets(i) )
			{
				BlurTargets(i)->RemovePoolRef();
				BlurTargets(i) = NULL;
			}

			// obtain/create the render target
			BlurTargets(i) = RenderTargetPool->ObtainRenderTarget(UCPP_TargetSize(BlurSizeX), UCPP_TargetSize(BlurSizeY), TEXF_RGBA8, FRenderTarget::RT_CREATE_COLOR );
			UCPP_LogTarget( TEXT("UCScreenEmissive"), i, BlurTargets(i), Reference, RI );
		}
	}

	unguard;
}

/* scion ======================================================================
* UUCScreenEmissive::PostRender
* Author: sz
*
* Return render target containing final result from effect
* =============================================================================
*/
FRenderTarget* UUCScreenEmissive::GetEmissiveResultTarget()
{
	return ( BlurTargets.Num() >= 1 ) ? BlurTargets(0) : SelectTarget;
}

/* scion ======================================================================
* UUCScreenEmissive::PostRender
* Author: sz
* 
* Select emissive pixels from back buffer and render them into the 
* accumulation target.  Do a separable horizontal and vertical blur using
* consecutively smaller blur render targets.  Accumulate results back
* into the accumulation target.
*
*
* =============================================================================
*/
void UUCScreenEmissive::PostRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenEmissive::PostRender);
#ifndef _XBOX
	// UC2 port (M7): Epic's "return" for every platform but Xbox; the rest of the function
	// runs when the render device can post-process (FRenderCaps::XboxPostProcess).
	if( !RenDevHasXboxPostProcess() )
		return;
#endif

	checkSlow( SelectTarget && BackBufferTarget && NumBlurTargets == BlurTargets.Num() );
	
	// force stencil to off
	RI->EnableStencil( 0 );

	// get streak falloff rate
	BYTE FalloffColor = 255*FalloffRate;
	if( Reset )
	{
		FalloffColor=255;
		Reset=0;
	}

// #if 0 because,
// first method does not blur the initial downsampling that happens
// when the back buffer emissive pixels are selected and copied to
// the much smaller sized selection target
#if 0
	// make sure the back buffer material has a tex matrix
	// required for the u,v -> s,t transform required on Xbox for linear textures
	DECLARE_STATIC_UOBJECT(	UProxyBitmapMaterial, BackBufferMaterial, {});
	BackBufferMaterial->SetTextureInterface( BackBufferTarget );
	DECLARE_STATIC_UOBJECT(	UTexMatrix,	SourceMaterialTexMatrix, 
	{
		SourceMaterialTexMatrix->Matrix.SetIdentity();
		SourceMaterialTexMatrix->Material = BackBufferMaterial;

	});
	// scion sz - only use the portion of the backbuffer that overlaps the viewport
	ViewportTransformTexMatrix( SourceMaterialTexMatrix, BackBufferMaterial, Viewport );

	// Emissive pixel shader to render emissive pixels from the back buffer.
	// Emissive pixels in the back buffer are selected (modulated) based 
	// by their alpha values.  Ie. a pixel with alpha=255 is fully emissive
	// and a pixel with alpha=0 is not emissive.
	DECLARE_STATIC_UOBJECT( UPixelShaderMaterial, PSEmissiveSelect,
	{
		PSEmissiveSelect->TextureStage0 = SourceMaterialTexMatrix;
		PSEmissiveSelect->PixelShaderType = PS_EmissiveSelect;
		PSEmissiveSelect->RenderHint = MRH_PixelShaderMaterial;
	});

	// draw emissive pixels into accumulation target (black elsewhere)
	RI->SetRenderTarget( SelectTarget );
	FScreenPrimUtil( SelectTarget, RI ).DrawTile(
		0, 0, 
		SelectTarget->GetWidth(), SelectTarget->GetHeight(),
		U1, V1,	U2, V2,
		0.0f,
		PSEmissiveSelect,
		FColor(255,255,255,FalloffColor)
		);
	//scion jg -- Pop the render target change
	RI->ResetRenderTarget();
#else

	FColor DrawColor(255,255,255,FalloffColor);
	FMultiSampleFilter SamplerBoxBlur2X( FMultiSampleFilter::MULTI_SAMPLE_BOX, 2.0f );
	FilterCopyRenderTarget( SelectTarget, BackBufferTarget, Viewport, SamplerBoxBlur2X, RI, &DrawColor, NULL, BLUR_EMISSIVE );

#endif //#if 0

	// create a proxy material and a tex matrix for the (uv->st conversion) 
	// to render with the slect target 
	DECLARE_STATIC_UOBJECT(	UProxyBitmapMaterial, SelectTargetMaterial, {});
	SelectTargetMaterial->SetTextureInterface( SelectTarget );
	DECLARE_STATIC_UOBJECT(	UTexMatrix,	SelectTargetMaterialTexMatrix, 
	{
		SelectTargetMaterialTexMatrix->Matrix.SetIdentity();
		SelectTargetMaterialTexMatrix->Material = SelectTargetMaterial;

	});
	// final blend is needed so that the alpha in the emissive select target
	// can be ignored and so that no z values are written
	DECLARE_STATIC_UOBJECT(	UFinalBlend, SelectCopyBlend,
	{
		SelectCopyBlend->FrameBufferBlending = FB_Overwrite;
		SelectCopyBlend->Material = SelectTargetMaterialTexMatrix;
		SelectCopyBlend->ZWrite = 0;
		SelectCopyBlend->ZTest = 0;
		SelectCopyBlend->AlphaTest = 0;
	});

	// get the first blur target
	// this render target will be used for feedback blur based on
	// the number of passes
	checkSlow(BlurTargets.Num());
	FRenderTarget* FeedbackBlurTarget = BlurTargets(0);

	// copy the select target contents to the first blur target
	// without any blur operation.  
	RI->SetRenderTarget( FeedbackBlurTarget );
	FScreenPrimUtil( FeedbackBlurTarget, RI ).DrawTile(
		0, 0, 
		FeedbackBlurTarget->GetWidth(), FeedbackBlurTarget->GetHeight(),
		U1, V1,	U2, V2,
		0.0f,
		SelectCopyBlend,
		WhiteOpaque
		);
	RI->ResetRenderTarget();

	for( INT pass=0; pass < NumFeedbackPasses; pass++ )
	{
		// generate different filter kernels for blurring
		static FMultiSampleFilter SamplerHorizBlur( FMultiSampleFilter::MULTI_SAMPLE_HORIZ_1221 );
		static FMultiSampleFilter SamplerVertBlur( FMultiSampleFilter::MULTI_SAMPLE_VERT_1221 );
		static FMultiSampleFilter SamplerBoxBlur( FMultiSampleFilter::MULTI_SAMPLE_BOX );

		FRenderTarget* SrcTarget = FeedbackBlurTarget;
		
		// start at BlurTargets(1) since BlurTargets(0) is the FeedbackBlurTarget
		for( INT i=1; i < BlurTargets.Num(); i++ )
		{
			// odd targets used for vertical blur
			if( i%2 == 1 )
			{
				// down sample in the vertical direction and use a vertical blur filter
				FilterCopyRenderTarget( BlurTargets(i), SrcTarget, NULL, SamplerVertBlur, RI, NULL, NULL, 0, RenDevPostProcessTargetScale() );	// UC2 port (H-1): TapScale
			}
			// even targets used for horizontal blur
			else
			{
				// down sample in the horizontal direction and use a horizontal blur filter
				FilterCopyRenderTarget( BlurTargets(i), SrcTarget, NULL, SamplerHorizBlur, RI, NULL, NULL, 0, RenDevPostProcessTargetScale() );	// UC2 port (H-1): TapScale
			}

			SrcTarget = BlurTargets(i);
		}

		DECLARE_STATIC_UOBJECT(	UFinalBlend, FeedbackBlend,
		{
			// FB_AlphaBlend is more correct, but the blur is not as bright
			// so it doesn't show up as well
			//FeedbackBlend->FrameBufferBlending = FB_AlphaBlend;	
			FeedbackBlend->FrameBufferBlending = FB_Brighten;			
			FeedbackBlend->Material = NULL;
			FeedbackBlend->ZWrite = 0;
			FeedbackBlend->ZTest = 0;
		});

		// up sample and use a box blur filter to blend the result from this blur
		// pass back into the FeedbackBlurTarget
		if( pass == 0 )
			FilterCopyRenderTarget( FeedbackBlurTarget, SrcTarget, NULL, SamplerBoxBlur, RI, NULL, NULL, 0, RenDevPostProcessTargetScale() );	// UC2 port (H-1): TapScale
		else
		{
			static FColor FeedBackAlpha(255,255,255,255/*127 for FB_AlphaBlend*/);
			FilterCopyRenderTarget( FeedbackBlurTarget, SrcTarget, NULL, SamplerBoxBlur, RI, &FeedBackAlpha, FeedbackBlend, 0, RenDevPostProcessTargetScale() );	// UC2 port (H-1): TapScale
		}

	}

	// add results from blur to frame buffer
	// using an additive blend 
	if( DisplayOutput )
	{
		FRenderTarget* EmissiveResultTarget = GetEmissiveResultTarget();
		DECLARE_STATIC_UOBJECT(	UProxyBitmapMaterial, BlurTargetMaterial, {});
		BlurTargetMaterial->SetTextureInterface( EmissiveResultTarget );

		DECLARE_STATIC_UOBJECT(	UTexMatrix,	FinalBlendTexMatrix, 
		{
			FinalBlendTexMatrix->Matrix.SetIdentity();
			FinalBlendTexMatrix->Material = BlurTargetMaterial;
		});	

		DECLARE_STATIC_UOBJECT( UFinalBlend, BlurBlend,
		{
			BlurBlend->FrameBufferBlending = FB_Brighten;
			BlurBlend->Material = FinalBlendTexMatrix;
			BlurBlend->ZWrite = 0;
			BlurBlend->ZTest = 0;
		}
		);

		static UBOOL bDebugClearBack=0;
		static FColor ClearColor(0,0,0);
		if( bDebugClearBack )
			RI->Clear(1, ClearColor, 0, 0.0f, 0, 0 );			// -- DEBUGSZ

		// jg -- Use the global screen prim so we don't have to recalc the
		// canvas to screen matrix unless the size changed
		GScreenPrimUtil.Update(Viewport->SizeX,Viewport->SizeY,RI);
		GScreenPrimUtil.DrawTile(
			0, 0, Viewport->SizeX, Viewport->SizeY,
			U1, V1,	U2, V2,
			0.0f,
			BlurBlend,
			FColor(255,255,255,128*Alpha)						// alpha comes from the camera effect in order to fade out the effect
			);
		GScreenPrimUtil.Flush();
	}

	// force stencil back on
	RI->EnableStencil( 1 );

	unguard;
}

/* scion ======================================================================
* UUCScreenEmissiveWithBloom::UCScreenEmissiveWithBloom
* Author: sz
* 
* Create both a UUCScreenEmissive and a UUCScreenBloom camera effect
* when this class is instantiated.
* ============================================================================
*/
UUCScreenEmissiveWithBloom::UUCScreenEmissiveWithBloom()
{
	EmissiveCamEffect = ConstructObject<UUCScreenEmissive>(UUCScreenEmissive::StaticClass());
	BloomCamEffect = ConstructObject<UUCScreenBloom>(UUCScreenBloom::StaticClass());
}

/* scion ======================================================================
* UUCScreenEmissiveWithBloom::Destroy
* Author: sz
* 
* Clean up sub camera effects.
* ============================================================================
*/
void UUCScreenEmissiveWithBloom::Destroy()
{
	guard(UUCScreenEmissiveWithBloom::Destroy);
	Super::Destroy();

	if( EmissiveCamEffect )
	{
		delete EmissiveCamEffect;
		EmissiveCamEffect = NULL;
	}

	if( BloomCamEffect )
	{
		delete BloomCamEffect;
		BloomCamEffect = NULL;
	}
	
	unguard;
}

/* scion ======================================================================
* UUCScreenEmissiveWithBloom::DestroyRenderTargets
* Author: sz
* 
* Clean up render targets created by the effects
* ============================================================================
*/
void UUCScreenEmissiveWithBloom::DestroyRenderTargets()
{
	if( EmissiveCamEffect )
	{
		EmissiveCamEffect->DestroyRenderTargets();
	}

	if( BloomCamEffect )
	{
		BloomCamEffect->DestroyRenderTargets();
	}

}

/* scion ======================================================================
* UUCScreenEmissiveWithBloom::PreRender
* Author: sz
*
* Run Pre render for each sub camera effect. This is where render targets
* are created or updated.
* ============================================================================
*/
void UUCScreenEmissiveWithBloom::PreRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenEmissiveWithBloom::PreRender);

	checkSlow( EmissiveCamEffect && BloomCamEffect );

	EmissiveCamEffect->PreRender( Viewport, RI );
	BloomCamEffect->PreRender( Viewport, RI );

	unguard;
}

/* scion ======================================================================
* UUCScreenEmissiveWithBloom::PostRender
* Author: sz
* 
* Run Post render for each sub camera effect and accumulate results.
* By doing this we save the extra full screen pass from adding the emissive 
* result and we also get an extra 2 pass blur on the emissive for free.
* ============================================================================
*/
void UUCScreenEmissiveWithBloom::PostRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenEmissiveWithBloom::PostRender);

	checkSlow( EmissiveCamEffect && BloomCamEffect );

	// make sure that the emissive post render does not draw its
	// results onto the back buffer
	EmissiveCamEffect->DisplayOutput = 0;
	EmissiveCamEffect->PostRender( Viewport, RI );

	// set the result from the emissive camera effect for the
	// bloom camera effect to use 
	BloomCamEffect->EmissiveResultTarget = EmissiveCamEffect->GetEmissiveResultTarget();
	BloomCamEffect->PostRender( Viewport, RI );

	unguard;
}

IMPLEMENT_CLASS(UUCScreenBerserk);
/* scion ======================================================================
 * UUCScreenBerserk::PreRender
 * Author: jg
 * 
 * Updates the various counters in order to calculate the blends correctly
 *
 * input:	Viewport	The viewport we are drawing into
 *			RI			The render interface used for drawing
 *
 * ============================================================================
 */
void UUCScreenBerserk::PreRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenBerserk::PreRender);
	// Check to see if this effect needs to be reset. If so, zero all the
	// transients, forcing them to be recalculated
	if (Reset == 1)
	{
		LastFrameTime = 0.f;
		ElapsedBerserkFadeTime	= BerserkFadeTime;
		BerserkFadeDirection	= -1.f;
		bFading					= 0;
		Reset					= 0;
	}
	// See if this is the first time we were run
	if (LastFrameTime == 0.f)
	{
		LastFrameTime = Viewport->Actor->Level->TimeSeconds;
	}
	// Figure out how much time has elapsed this frame
	FLOAT flDelta = Viewport->Actor->Level->TimeSeconds - LastFrameTime;
	// If we are fading out berserk with no direction, start fading
	if( bFading && BerserkFadeDirection == 0.f ) {
		BerserkFadeDirection	= 1.f;
		ElapsedBerserkFadeTime	= 0.f;
	}
	// Check to see if we are blending the back buffer copy with the berserk
	// effect
	if (BerserkFadeDirection != 0.f)
	{
		ElapsedBerserkFadeTime += flDelta * BerserkFadeDirection;
		// Check for end of effect in either direction
		if (ElapsedBerserkFadeTime > BerserkFadeTime ||
			ElapsedBerserkFadeTime < 0.f)
		{
			if( BerserkFadeDirection >= 1.f )
			{
				// Disable if done fading out
				Enabled	= 0;
			}
			BerserkFadeDirection = 0.f;
		}
	}
	// Grab the last frame time
	LastFrameTime = Viewport->Actor->Level->TimeSeconds;
	unguard;
}

/* scion ======================================================================
 * UUCScreenBerserk::PostRender
 * Author: jg
 * 
 * Draws the full screen effect and blends in the back buffer copy.
 *
 * input:	Viewport	The viewport we are drawing into
 *			RI			The render interface used for drawing
 *
 * ============================================================================
 */
void UUCScreenBerserk::PostRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenBerserk::PostRender);
#ifndef _XBOX
	// UC2 port (M7): Epic's "return" for every platform but Xbox; the rest of the function
	// runs when the render device can post-process (FRenderCaps::XboxPostProcess).
	if( !RenDevHasXboxPostProcess() )
		return;
#endif
	// jg -- Use the global screen prim so we don't have to recalc the
	// canvas to screen matrix unless there was a size change
	GScreenPrimUtil.Update(Viewport->SizeX,Viewport->SizeY,RI);
	RenderBerserkEffect(Viewport,RI);
	unguard;
}

/* scion ======================================================================
 * UUCScreenBerserk::RenderBerserkEffect
 * Author: jg
 * 
 * Draws the full screen effect, using the values updated in PreRender. It does
 * two passes: the first converts the back buffer into look up values to use in
 * the second pass.
 *
 * input:	Viewport	The viewport we are drawing into
 *			RI			The render interface used for drawing
 *
 * ============================================================================
 */
void UUCScreenBerserk::RenderBerserkEffect(UViewport* Viewport,
	FRenderInterface* RI)
{
	guard(UUCScreenBerserk::RenderBerserkEffect);
	// Render the effect everywhere Pawn's aren't
	RI->SetStencilOp(CF_NotEqual,0xBE,0xFF,SO_Keep,SO_Keep,SO_Keep,
		0xFF);
	// We are using the backbuffer as our source & dest
	FRenderTarget* BackBufferTarget = RI->GetRenderTarget();
	DECLARE_STATIC_UOBJECT(UProxyBitmapMaterial,PBMBackBuffer,{});
	// This is the proxy to treat the back buffer as a material
	PBMBackBuffer->SetTextureInterface(BackBufferTarget);
	// This allows the back buffer to use standard UV coords
	DECLARE_STATIC_UOBJECT(UTexMatrix,BackBufferTexMatrix, 
	{
		BackBufferTexMatrix->Matrix.SetIdentity();
		BackBufferTexMatrix->Material = PBMBackBuffer;
	}
	);
	// scion sz - only use the portion of the backbuffer that overlaps the viewport
	ViewportTransformTexMatrix( BackBufferTexMatrix, PBMBackBuffer, Viewport );
	DECLARE_STATIC_UOBJECT(UPSColorRemapMaterial,ColorRemap,
	{
		ColorRemap->TextureStage0 = BackBufferTexMatrix;
		// Don't ZTest or write
		ColorRemap->bUseZ = 0;
		// Don't auto set the backbuffer or it kills the viewport handling
		ColorRemap->bSetBackbufferManually = 1;
	});
	if (BerserkFadeDirection != 0.f)
	{
		// Handle fading
		ColorRemap->BlendAmount = 1.f - (ElapsedBerserkFadeTime / BerserkFadeTime);
	}
	else
	{
		// No fade, constant blend
		ColorRemap->BlendAmount = 1.f;
	}
	// Set our remap effect texture
	ColorRemap->RemapTexture = RemapTexture;
	// Now draw the back buffer full screen
	GScreenPrimUtil.DrawTile(0,0,Viewport->SizeX,Viewport->SizeY,U1,V1,U2,V2,
		0.f,ColorRemap,WhiteOpaque);
	GScreenPrimUtil.Flush();
	// Restore the pushed stencil state
	RI->ResetStencilOp();
	unguard;
}

IMPLEMENT_CLASS(UUCScreenMenuPause);
// Hack to work around the fact that things aren't ticked when paused
DOUBLE MenuPauseLastFrameTime = 0.0;

/* scion ======================================================================
 * UUCScreenMenuPause::PreRender
 * Author: jg
 * 
 * Updates the various counters in order to calculate the blends correctly
 *
 * input:	Viewport	The viewport we are drawing into
 *			RI			The render interface used for drawing
 *
 * ============================================================================
 */
void UUCScreenMenuPause::PreRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenMenuPause::PreRender);
	// Figure out if we need to auto reset or not
	if (Reset == 0)
	{
		// Figure out if the fade direction has changed
		Reset = (FadeMode == MPFM_FadeIn && LastFadeMode == MPFM_NoFade) ||
			(FadeMode == MPFM_FadeOut && LastFadeMode == MPFM_NoFade);
	}
	LastFadeMode = FadeMode;
	// Can't get time from the level since it is paused
	DOUBLE dblCurrentTime = appSeconds();
	// Check to see if this effect needs to be reset. If so, zero all the
	// transients, forcing them to be recalculated
	if (Reset == 1)
	{
		MenuPauseLastFrameTime = dblCurrentTime;
		// Start from 1 or 0 depending on the fade direction
		ElapsedFadeTime = (FadeMode == MPFM_FadeOut) ? FadeTime : 0.f;
		Reset = 0;
	}
	// Figure out how much time has elapsed this frame
	FLOAT flDelta = dblCurrentTime - MenuPauseLastFrameTime;
	// Add to how much time has elapsed
	if (FadeMode == MPFM_FadeIn)
	{
		ElapsedFadeTime = Clamp<FLOAT>(ElapsedFadeTime + flDelta,0.f,FadeTime);
	}
	else if (FadeMode == MPFM_FadeOut)
	{
		ElapsedFadeTime = Clamp<FLOAT>(ElapsedFadeTime - flDelta,0.f,FadeTime);
	}
	// Grab the last frame time
	MenuPauseLastFrameTime = dblCurrentTime;
	unguard;
}

/* scion ======================================================================
 * UUCScreenMenuPause::PostRender
 * Author: jg
 * 
 * Draws the full screen effect, blends in the back buffer copy, and renders
 * the masking particles.
 *
 * input:	Viewport	The viewport we are drawing into
 *			RI			The render interface used for drawing
 *
 * ============================================================================
 */
void UUCScreenMenuPause::PostRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenMenuPause::PostRender);
#ifndef _XBOX
	// UC2 port (M7): Epic's "return" for every platform but Xbox; the rest of the function
	// runs when the render device can post-process (FRenderCaps::XboxPostProcess).
	if( !RenDevHasXboxPostProcess() )
		return;
#endif
	// jg -- Use the global screen prim so we don't have to recalc the
	// canvas to screen matrix unless there was a size change
	GScreenPrimUtil.Update(Viewport->SizeX,Viewport->SizeY,RI);
	// Do the full screen effect
	RenderEffect(Viewport,RI);
	// Figure out whether we are done drawing or fading
	if (FadeMode == MPFM_FadeOut)
	{
		Enabled = ElapsedFadeTime > 0.f;
	}
	// Turn off the fading if it has reached it's max
	else if (FadeMode == MPFM_FadeIn && ElapsedFadeTime >= FadeTime)
	{
		FadeMode = MPFM_NoFade;
	}
	unguard;
}

/* scion ======================================================================
 * UUCScreenMenuPause::RenderEffect
 * Author: jg
 * 
 * Draws the full screen effect, using the values updated in PreRender. It does
 * two passes: the first converts the back buffer into look up values to use in
 * the second pass.
 *
 * input:	Viewport	The viewport we are drawing into
 *			RI			The render interface used for drawing
 *
 * ============================================================================
 */
void UUCScreenMenuPause::RenderEffect(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenMenuPause::RenderEffect);
	// We are using the backbuffer as our source & dest
	FRenderTarget* BackBufferTarget = RI->GetRenderTarget();
	DECLARE_STATIC_UOBJECT(UProxyBitmapMaterial,PBMBackBuffer,{});
	// This is the proxy to treat the back buffer as a material
	PBMBackBuffer->SetTextureInterface(BackBufferTarget);
	// This allows the back buffer to use standard UV coords
	DECLARE_STATIC_UOBJECT(UTexMatrix,BackBufferTexMatrix, 
	{
		BackBufferTexMatrix->Matrix.SetIdentity();
		BackBufferTexMatrix->Material = PBMBackBuffer;
	}
	);
	// scion sz - only use the portion of the backbuffer that overlaps the viewport
	ViewportTransformTexMatrix( BackBufferTexMatrix, PBMBackBuffer, Viewport );
	DECLARE_STATIC_UOBJECT(UPSColorRemapMaterial,ColorRemap,
	{
		ColorRemap->TextureStage0 = BackBufferTexMatrix;
		// Don't ZTest or write
		ColorRemap->bUseZ = 0;
		// Don't auto set the backbuffer or it kills the viewport handling
		ColorRemap->bSetBackbufferManually = 1;
	});
	// Set our remap effect texture
	ColorRemap->RemapTexture = RemapTexture;
	// Calculate the alpha
	if (bUseAlphaWhileFading)
	{
		//ss: Scale real alpha by camera effect alpha so we can control it better
		ColorRemap->BlendAmount = Alpha;
	}
	else
	{
		// Figure out how much of the effect to blend in
		if (FadeMode == MPFM_NoFade)
		{
			ColorRemap->BlendAmount = 1.f;
		}
		else
		{
			ColorRemap->BlendAmount = (ElapsedFadeTime / FadeTime);
		}
	}
	// Now draw the back buffer full screen
	GScreenPrimUtil.DrawTile(0,0,Viewport->SizeX,Viewport->SizeY,U1,V1,U2,V2,
		0.f,ColorRemap,WhiteOpaque);
	GScreenPrimUtil.Flush();
	unguard;
}

IMPLEMENT_CLASS(UUCScreenSniperOverlay);
/* scion ======================================================================
 * UUCScreenSniperOverlay::PreRender
 * Author: jg
 * 
 * Creates the render effects if needed. Otherwise it passes the PreRender()
 * call on to the aggregated effects.
 *
 * input:	Viewport	The viewport we are drawing into
 *			RI			The render interface used for drawing
 *
 * ============================================================================
 */
void UUCScreenSniperOverlay::PreRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenSniperOverlay::PreRender);
#ifndef _XBOX
	// UC2 port (M7): Xbox-only until the render device can post-process (FRenderCaps::XboxPostProcess).
	if( RenDevHasXboxPostProcess() )
#endif
	{
	// Create our depth of field effect if not already created
	if (DOFOverlay == NULL)
	{
		DOFOverlay = ConstructObject<UUCScreenDOF>(UUCScreenDOF::StaticClass());
	}
	// Create our radial blur effect if not already created
	if (RadialBlurOverlay == NULL)
	{
		RadialBlurOverlay = ConstructObject<UUCScreenRadialBlur>(
			UUCScreenRadialBlur::StaticClass());
		// Init the blur distance
		RadialBlurOverlay->RadialBlurDistance = RadialBlurAmount;
		RadialBlurOverlay->NumRadialPolys = 4;
		// Swap out the blur mask
		RadialBlurOverlay->PixelShaderMotionBlurMaterial = SniperRadialMaterial;
	}
	checkSlow(DOFOverlay != NULL && RadialBlurOverlay != NULL);
	// Forward the call on to our aggregated effects
	DOFOverlay->PreRender(Viewport,RI);
	RadialBlurOverlay->PreRender(Viewport,RI);
	}
	unguard;
}

/* scion ======================================================================
 * UUCScreenSniperOverlay::PostRender
 * Author: jg
 * 
 * Forwards the PostRender() call on to the aggregated effects. Then it calls
 * the RenderEffect() method to render the colorization effect.
 *
 * input:	Viewport	The viewport we are drawing into
 *			RI			The render interface used for drawing
 *
 * ============================================================================
 */
void UUCScreenSniperOverlay::PostRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenSniperOverlay::PostRender);
#ifndef _XBOX
	// UC2 port (M7): Xbox-only until the render device can post-process; the overlays exist
	// once PreRender ran with the capability.
	if( RenDevHasXboxPostProcess() && DOFOverlay != NULL && RadialBlurOverlay != NULL )
#endif
	{
	checkSlow(DOFOverlay != NULL && RadialBlurOverlay != NULL);
	// Update the global screen prim util
	GScreenPrimUtil.Update(Viewport->SizeX,Viewport->SizeY,RI);
	// Forward the call on to our DOF effect
	DOFOverlay->PixelShaderDOFMaterial->AlphaRef = DOFOverlay->DefaultAlphaRef;
	DOFOverlay->PostRender(Viewport,RI);
	DOFOverlay->PixelShaderDOFMaterial->AlphaRef = DOFOverlay->AlwaysOnAlphaRef;
	// Now do the blur
	RadialBlurOverlay->PostRender(Viewport,RI);
	// Now render our color remapping
	RenderEffect(Viewport,RI);
	}
	unguard;
}

/* scion ======================================================================
 * UUCScreenSniperOverlay::RenderEffect
 * Author: jg
 * 
 * Uses the single pass method of color remapping
 *
 * input:	Viewport	The viewport we are drawing into
 *			RI			The render interface used for drawing
 *
 * ============================================================================
 */
void UUCScreenSniperOverlay::RenderEffect(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenSniperOverlay::RenderEffect);
	// We are using the backbuffer as our source & dest
	FRenderTarget* BackBufferTarget = RI->GetRenderTarget();
	DECLARE_STATIC_UOBJECT(UProxyBitmapMaterial,PBMBackBuffer,{});
	// This is the proxy to treat the back buffer as a material
	PBMBackBuffer->SetTextureInterface(BackBufferTarget);
	// This allows the back buffer to use standard UV coords
	DECLARE_STATIC_UOBJECT(UTexMatrix,BackBufferTexMatrix, 
	{
		BackBufferTexMatrix->Matrix.SetIdentity();
		BackBufferTexMatrix->Material = PBMBackBuffer;
	}
	);
	// scion sz - only use the portion of the backbuffer that overlaps the viewport
	ViewportTransformTexMatrix( BackBufferTexMatrix, PBMBackBuffer, Viewport );
	DECLARE_STATIC_UOBJECT(UPSColorRemapMaterial,ColorRemap,
	{
		ColorRemap->TextureStage0 = BackBufferTexMatrix;
		// Don't ZTest or write
		ColorRemap->bUseZ = 0;
		// Don't auto set the backbuffer or it kills the viewport handling
		ColorRemap->bSetBackbufferManually = 1;
		ColorRemap->BlendAmount = 1.f;
		// Set our remap effect texture
		ColorRemap->Mask = RemapMask;
	});
	// Set our remap effect texture
	ColorRemap->RemapTexture = RemapTexture;
	// Now draw the back buffer full screen
	GScreenPrimUtil.DrawTile(0,0,Viewport->SizeX,Viewport->SizeY,U1,V1,U2,V2,
		0.f,ColorRemap,WhiteOpaque);
	GScreenPrimUtil.Flush();
	unguard;
}

/* scion ======================================================================
 * UUCScreenSniperOverlay::Destroy
 * Author: jg
 * 
 * Cleans up any allocated memory/objects
 *
 * ============================================================================
 */
void UUCScreenSniperOverlay::Destroy(void)
{
	guard(UUCScreenSniperOverlay::Destroy);
	// Avoid the failed to route destroy messages
	Super::Destroy();
	// Clean up the aggregated effects if allocated
	if (DOFOverlay != NULL)
	{
		delete DOFOverlay;
		DOFOverlay = NULL;
	}
	if (RadialBlurOverlay != NULL)
	{
		delete RadialBlurOverlay;
		RadialBlurOverlay = NULL;
	}
	unguard;
}

IMPLEMENT_CLASS(UUCScreenTeleportOverlay);
/* scion ======================================================================
 * UUCScreenTeleportOverlay::UpdateFOVAndBlur
 * Author: jg
 * 
 * Interpolates the FOV & blur amounts based upon whether the player is
 * teleporting in or out. Returns whether or not we are done with the effect.
 *
 * input:	pPC -- The player controller to update FOV on
 *
 * ============================================================================
 */
UBOOL UUCScreenTeleportOverlay::UpdateFOVAndBlur(APlayerController* pPC)
{
	guard(UUCScreenTeleportOverlay::UpdateFOVAndBlur);
	check(pPC);
	checkSlow(pPC->IsValid());
	UBOOL bDone = 0;
	// Twiddle the FOV depending on the direction the effect is going
	switch (TeleportType)
	{
		case TELET_In:
		{
			// If we are still active
			if (ElapsedTime <= TeleportInTime)
			{
				// Figure out how far along in the interpolation we are
				Alpha = Clamp<FLOAT>(ElapsedTime / TeleportInTime,0.f,1.f);
				// Interpolate between the original FOV and our max FOV
				CurrentFOV = Clamp<FLOAT>(MaxFOV -
					(Alpha * (MaxFOV - OriginalFOV)),OriginalFOV,MaxFOV);
				// Calculate how much blur to apply
				CurrentBlurAmount = Clamp<FLOAT>(MaxRadialBlurAmount -
					(Alpha * (MaxRadialBlurAmount - MinRadialBlurAmount)),
					MinRadialBlurAmount,MaxRadialBlurAmount);
			}
			else
			{
				bDone = 1;
			}
			break;
		}
		case TELET_Out:
		case TELET_Both:
		{
			// Figure out how far along in the interpolation we are
			Alpha = Clamp<FLOAT>(ElapsedTime / TeleportOutTime,0.f,1.f);
			// Interpolate between the original FOV and our max FOV
			CurrentFOV = Clamp<FLOAT>(OriginalFOV +
				(Alpha * (MaxFOV - OriginalFOV)),OriginalFOV,MaxFOV);
			// Calculate how much blur to apply
			CurrentBlurAmount = Clamp<FLOAT>(MinRadialBlurAmount +
				(Alpha * (MaxRadialBlurAmount - MinRadialBlurAmount)),
				MinRadialBlurAmount,MaxRadialBlurAmount);
			// Figure out if we need to switch to teleporting in
			if (ElapsedTime >= TeleportOutTime)
			{
				if (TeleportType == TELET_Out)
				{
					bDone = 1;
				}
				else
				{
					TeleportType = TELET_In;
				}
			}
			break;
		}
		case TELET_None:
		{
			bDone = 1;
			break;
		}
	}
	// Apply the current fov value to the player
	pPC->FovAngle = bDone ? OriginalFOV : CurrentFOV;
	// Set the blur amount to apply
	RadialBlurOverlay->RadialBlurDistance = CurrentBlurAmount;
	return bDone;
	unguard;
}

/* scion ======================================================================
 * UUCScreenTeleportOverlay::PreRender
 * Author: jg
 * 
 * Interpolates the FOV based upon whether the player is teleporting in or out.
 * Creates the render effects if needed. Then it passes the PreRender() call on
 * to the aggregated effects.
 *
 * input:	Viewport	The viewport we are drawing into
 *			RI			The render interface used for drawing
 *
 * ============================================================================
 */
void UUCScreenTeleportOverlay::PreRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenTeleportOverlay::PreRender);
#ifndef _XBOX
	// UC2 port (M7): Xbox-only until the render device can post-process (FRenderCaps::XboxPostProcess).
	if( RenDevHasXboxPostProcess() )
#endif
	{
	// Create our radial blur effect if not already created
	if (RadialBlurOverlay == NULL)
	{
		RadialBlurOverlay = ConstructObject<UUCScreenRadialBlur>(
			UUCScreenRadialBlur::StaticClass());
		// Swap out the blur mask
		RadialBlurOverlay->PixelShaderMotionBlurMaterial = RadialBlurMaterial;
	}
	checkSlow(RadialBlurOverlay != NULL);
	if (Reset == 1)
	{
		// Grab the original FOV for restoring later if this is just starting
		OriginalFOV = ((APlayerController*)Viewport->Actor)->FovAngle;
		// Reset the time
		LastTime = Viewport->Actor->Level->TimeSeconds;
		Reset = 0;
	}
	// Update the elapsed time
	ElapsedTime += Viewport->Actor->Level->TimeSeconds - LastTime;
	// Update the FOV and disable the effect if needed
	if (UpdateFOVAndBlur((APlayerController*)Viewport->Actor))
	{
		Enabled = 0;
	}
	LastTime = Viewport->Actor->Level->TimeSeconds;
	// Forward the call on to our aggregated effects
	RadialBlurOverlay->PreRender(Viewport,RI);
	}
	unguard;
}

/* scion ======================================================================
 * UUCScreenTeleportOverlay::PostRender
 * Author: jg
 * 
 * Forwards the PostRender() call on to the aggregated effects. Then it calls
 * the RenderEffect() method to render the colorization effect.
 *
 * input:	Viewport	The viewport we are drawing into
 *			RI			The render interface used for drawing
 *
 * ============================================================================
 */
void UUCScreenTeleportOverlay::PostRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenTeleportOverlay::PostRender);
#ifndef _XBOX
	// UC2 port (M7): Xbox-only until the render device can post-process; the overlay exists
	// once PreRender ran with the capability.
	if( RenDevHasXboxPostProcess() && RadialBlurOverlay != NULL )
#endif
	{
	checkSlow(RadialBlurOverlay != NULL);
	// Update the global screen prim util
	GScreenPrimUtil.Update(Viewport->SizeX,Viewport->SizeY,RI);
	// Do the radial blur
	RadialBlurOverlay->PostRender(Viewport,RI);
	// Now render our color remapping
	RenderEffect(Viewport,RI);
	}
	unguard;
}

/* scion ======================================================================
 * UUCScreenTeleportOverlay::RenderEffect
 * Author: jg
 * 
 * Uses the single pass method of color remapping
 *
 * input:	Viewport	The viewport we are drawing into
 *			RI			The render interface used for drawing
 *
 * ============================================================================
 */
void UUCScreenTeleportOverlay::RenderEffect(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenTeleportOverlay::RenderEffect);
	// We are using the backbuffer as our source & dest
	FRenderTarget* BackBufferTarget = RI->GetRenderTarget();
	DECLARE_STATIC_UOBJECT(UProxyBitmapMaterial,PBMBackBuffer,{});
	// This is the proxy to treat the back buffer as a material
	PBMBackBuffer->SetTextureInterface(BackBufferTarget);
	// This allows the back buffer to use standard UV coords
	DECLARE_STATIC_UOBJECT(UTexMatrix,BackBufferTexMatrix, 
	{
		BackBufferTexMatrix->Matrix.SetIdentity();
		BackBufferTexMatrix->Material = PBMBackBuffer;
	}
	);
	// scion sz - only use the portion of the backbuffer that overlaps the viewport
	ViewportTransformTexMatrix( BackBufferTexMatrix, PBMBackBuffer, Viewport );
	DECLARE_STATIC_UOBJECT(UPSColorRemapMaterial,ColorRemap,
	{
		ColorRemap->TextureStage0 = BackBufferTexMatrix;
		// Set our remap effect texture
		ColorRemap->RemapTexture = RemapTexture;
		// Don't ZTest or write
		ColorRemap->bUseZ = 0;
		// Don't auto set the backbuffer or it kills the viewport handling
		ColorRemap->bSetBackbufferManually = 1;
	});
	// Blend based upon the alpha
	ColorRemap->BlendAmount = 1.f - Alpha;
	// Now draw the back buffer full screen
	GScreenPrimUtil.DrawTile(0,0,Viewport->SizeX,Viewport->SizeY,U1,V1,U2,V2,
		0.f,ColorRemap,WhiteOpaque);
	GScreenPrimUtil.Flush();
	unguard;
}

/* scion ======================================================================
 * UUCScreenTeleportOverlay::Destroy
 * Author: jg
 * 
 * Cleans up any allocated memory/objects
 *
 * ============================================================================
 */
void UUCScreenTeleportOverlay::Destroy(void)
{
	guard(UUCScreenTeleportOverlay::Destroy);
	// Avoid the failed to route destroy messages
	Super::Destroy();
	// Clean up the aggregated effects if allocated
	if (RadialBlurOverlay != NULL)
	{
		delete RadialBlurOverlay;
		RadialBlurOverlay = NULL;
	}
	unguard;
}

IMPLEMENT_CLASS(UUCScreenLiandri);
/* scion ======================================================================
 * UUCScreenLiandri::PreRender
 * Author: jg
 * 
 * Updates the various counters in order to calculate the blends correctly
 *
 * input:	Viewport	The viewport we are drawing into
 *			RI			The render interface used for drawing
 *
 * ============================================================================
 */
void UUCScreenLiandri::PreRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenLiandri::PreRender);
	// Check to see if this effect needs to be reset. If so, zero all the
	// transients, forcing them to be recalculated
	if (Reset == 1)
	{
		LastFrameTime = 0.f;
		ElapsedTime = 0.f;
		Reset = FALSE;
	}
	// See if this is the first time we were run
	if (LastFrameTime == 0.f)
	{
		LastFrameTime = Viewport->Actor->Level->TimeSeconds;
	}
	// Figure out how much time has elapsed this frame
	FLOAT flDelta = Viewport->Actor->Level->TimeSeconds - LastFrameTime;
	ElapsedTime += flDelta;
	// Check to see if we are blending the back buffer copy with the Liandri
	// effect
	if (bIsFading == TRUE)
	{
		if (bFadeIn == TRUE)
		{
			// Stop fading in if we are done
			if (ElapsedTime > FadeInTime)
			{
				bIsFading = FALSE;
				BlendAmount = 1.f;
			}
			else
			{
				BlendAmount = ElapsedTime / FadeInTime;
			}
		}
		else
		{
			// If we are done fading out, disable the effect
			if (ElapsedTime > FadeOutTime)
			{
				bIsFading = FALSE;
				BlendAmount = 0.f;
				Enabled = 0;
			}
			else
			{
				BlendAmount = 1.f - (ElapsedTime / FadeOutTime);
			}
		}
	}
	// Grab the last frame time
	LastFrameTime = Viewport->Actor->Level->TimeSeconds;
	unguard;
}

/* scion ======================================================================
 * UUCScreenLiandri::PostRender
 * Author: jg
 * 
 * Draws the full screen effect and blends in the back buffer copy.
 *
 * input:	Viewport	The viewport we are drawing into
 *			RI			The render interface used for drawing
 *
 * ============================================================================
 */
void UUCScreenLiandri::PostRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UUCScreenLiandri::PostRender);
#ifndef _XBOX
	// UC2 port (M7): Epic's "return" for every platform but Xbox; the rest of the function
	// runs when the render device can post-process (FRenderCaps::XboxPostProcess).
	if( !RenDevHasXboxPostProcess() )
		return;
#endif
	// Render the effect everywhere Pawn's aren't
	RI->SetStencilOp(CF_NotEqual,0xBE,0xFF,SO_Keep,SO_Keep,SO_Keep,
		0xFF);
	// We are using the backbuffer as our source & dest
	FRenderTarget* BackBufferTarget = RI->GetRenderTarget();
	DECLARE_STATIC_UOBJECT(UProxyBitmapMaterial,PBMBackBuffer,{});
	// This is the proxy to treat the back buffer as a material
	PBMBackBuffer->SetTextureInterface(BackBufferTarget);
	// This allows the back buffer to use standard UV coords
	DECLARE_STATIC_UOBJECT(UTexMatrix,BackBufferTexMatrix, 
	{
		BackBufferTexMatrix->Matrix.SetIdentity();
		BackBufferTexMatrix->Material = PBMBackBuffer;
	}
	);
	// scion sz - only use the portion of the backbuffer that overlaps the viewport
	ViewportTransformTexMatrix( BackBufferTexMatrix, PBMBackBuffer, Viewport );
	DECLARE_STATIC_UOBJECT(UPSColorRemapMaterial,ColorRemap,
	{
		ColorRemap->TextureStage0 = BackBufferTexMatrix;
		// Don't ZTest or write
		ColorRemap->bUseZ = 0;
		// Don't auto set the backbuffer or it kills the viewport handling
		ColorRemap->bSetBackbufferManually = 1;
		ColorRemap->bGrayScaleOnly = 1;
	});
	// Handle fading
	ColorRemap->BlendAmount = BlendAmount;
	// Set our noise and mask textures
	ColorRemap->TextureStage1 = Noise1;
	ColorRemap->TextureStage2 = Noise2;
	ColorRemap->TextureStage3 = Noise3;
	// Now draw the back buffer full screen
	GScreenPrimUtil.DrawTile(0,0,Viewport->SizeX,Viewport->SizeY,U1,V1,U2,V2,
		0.f,ColorRemap,WhiteOpaque);
	GScreenPrimUtil.Flush();
	// Restore the pushed stencil state
	RI->ResetStencilOp();
	unguard;
}
