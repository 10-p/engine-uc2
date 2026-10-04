/*=============================================================================
	UnCameraEffects.cpp: Camera effect implemention
	Copyright 2001-2002 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/

#include "EnginePrivate.h"

IMPLEMENT_CLASS(USubActionCameraEffect);
IMPLEMENT_CLASS(UCameraEffect);
IMPLEMENT_CLASS(UMotionBlur);

//
//	USubActionCameraEffect::Update
//

UBOOL USubActionCameraEffect::Update(FLOAT Percent,ASceneManager* SceneManager)
{
	guard(USubActionCameraEffect::Update);

	if(!UMatSubAction::Update(Percent,SceneManager))
		return 0;

	if(IsRunning())
	{
		APlayerController*	Controller = Cast<APlayerController>(SceneManager->Viewer);

		if(Controller && CameraEffect)
		{
			if(PctDuration > 0.0f)
				CameraEffect->Alpha = Lerp(StartAlpha,EndAlpha,(Percent - PctStarting) / PctDuration);
			else
				CameraEffect->Alpha = EndAlpha;

			if(CameraEffect->Alpha <= 0.0f || (Status == SASTATUS_Ending && DisableAfterDuration))
				Controller->eventRemoveCameraEffect(CameraEffect);
			else
				Controller->eventAddCameraEffect(CameraEffect,AllowDuplicate?0:1);
		}
	}

	return 1;

	unguard;
}

//
//	USubActionCameraEffect::GetStatString
//

FString USubActionCameraEffect::GetStatString()
{
	guard(USubActionCameraEffect:GetStatString);
	//scion jg -- Return the name of the camera effect for ease of use
	return FString::Printf(TEXT("Camera Effect %s"),CameraEffect == NULL ?
		TEXT("None") : CameraEffect->GetName());
	unguard;
}

//
//	UMotionBlur::Destroy
//

void UMotionBlur::Destroy()
{
	guard(UMotionBlur::Destroy);

	Super::Destroy();

	for(INT TargetIndex = 0;TargetIndex < ARRAY_COUNT(RenderTargets);TargetIndex++)
		if(RenderTargets[TargetIndex])
		{
			delete RenderTargets[TargetIndex];
			RenderTargets[TargetIndex] = NULL;
		}

	unguard;
}

//
//	UMotionBlur::PreRender
//

void UMotionBlur::PreRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UMotionBlur::PreRender);

	// If necessary, create new render targets.

	for(INT TargetIndex = 0;TargetIndex < ARRAY_COUNT(RenderTargets);TargetIndex++)
	{
		UBOOL	Clear = 0;

		if(!RenderTargets[TargetIndex])
		{
			RenderTargets[TargetIndex] = NEW FAuxRenderTarget(Viewport->SizeX,Viewport->SizeY,TEXF_RGBA8);
			Clear = 1;
		}
		else if(RenderTargets[TargetIndex]->Width != Viewport->SizeX || RenderTargets[TargetIndex]->Height != Viewport->SizeY)
		{
			RenderTargets[TargetIndex]->Width = Viewport->SizeX;
			RenderTargets[TargetIndex]->Height = Viewport->SizeY;
			RenderTargets[TargetIndex]->Revision++;
			Clear = 1;
		}

		if(Clear)
		{
			RI->PushState();

			RI->SetRenderTarget(RenderTargets[TargetIndex]);
			RI->Clear();
			//scion jg -- Pop the render target change
			RI->ResetRenderTarget();
			RI->PopState();
		}
	}

	// Render the scene into the first render target.

	RI->PushState();

	RI->SetRenderTarget(RenderTargets[0]);
	RI->SetViewport(0,0,Viewport->SizeX,Viewport->SizeY);
	RI->Clear(1,FColor(0,0,0),1,1.0f,1,~DEPTH_COMPLEXITY_MASK(Viewport));

	unguard;
}

//
//	UMotionBlur::PostRender
//

void UMotionBlur::PostRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UMotionBlur::PostRender);
	//scion jg -- Pop the viewport size change && pop the render target change
	RI->ResetViewport();
	RI->ResetRenderTarget();

	// Blend the new frame into the last frame, using BlurAlpha as the blending factor.

	DECLARE_STATIC_UOBJECT(
		UProxyBitmapMaterial,
		RenderTargetTexture,
		{
			RenderTargetTexture->SetTextureInterface(RenderTargets[0]);
		}
		);

	DECLARE_STATIC_UOBJECT(
		UVertexColor,
		VertexColor,
		{
		}
		);

	DECLARE_STATIC_UOBJECT(
		UShader,
		BlurShader,
		{
			BlurShader->Diffuse = RenderTargetTexture;
			BlurShader->Opacity = VertexColor;
		}
		);

	RI->SetRenderTarget(RenderTargets[1]);
	RI->SetViewport(0,0,Viewport->SizeX,Viewport->SizeY);

	FLOAT	FrameAlpha = Clamp(Alpha * (1.0f - (Viewport->Actor->Level->TimeSeconds - LastFrameTime) / 0.1f),0.0f,1.0f);

	FCanvasUtil(&Viewport->RenderTarget,RI).DrawTile(
		0,0,
		Viewport->SizeX,Viewport->SizeY,
		0,0,
		Viewport->SizeX*RenderTargets[0]->GetWidth(), Viewport->SizeY*RenderTargets[0]->GetHeight(),
		0.0f,
		BlurShader,
		FColor(255,255,255,BlurAlpha * FrameAlpha + 255 * (1.0f - FrameAlpha))
		);

	//scion jg -- Pop the viewport size change && pop the render target change
	RI->ResetViewport();
	RI->ResetRenderTarget();
	RI->PopState();

	// Copy the result to the frame buffer.

	DECLARE_STATIC_UOBJECT(
		UProxyBitmapMaterial,
		BltTexture,
		{
			BltTexture->SetTextureInterface(RenderTargets[1]);
		}
		);

	FCanvasUtil(&Viewport->RenderTarget,RI).DrawTile(
		0,0,
		Viewport->SizeX,Viewport->SizeY,
		0,0,
		Viewport->SizeX*RenderTargets[1]->GetWidth(), Viewport->SizeY*RenderTargets[1]->GetHeight(),
		1.0f,
		BltTexture,
		FColor(255,255,255,255)
		);

	LastFrameTime = Viewport->Actor->Level->TimeSeconds;

	unguard;
}

IMPLEMENT_CLASS(UScreenFlash);
/* scion ======================================================================
 * UScreenFlash::PostRender
 * Author: jg
 * 
 * Renders any view flash as needed. Does nothing in third person
 *
 * input:	Viewport	The viewport we are drawing into
 *			RI			The render interface used for drawing
 *
 * ============================================================================
 */
void UScreenFlash::PostRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UScreenFlash::PreRender);
	// Don't flash in third person, unless specifically toggled on
	if (bIsThirdPerson == 0 || bAllowThirdPersonFlash == 1)
	{
		FlashScale.X = Clamp( FlashScale.X, 0.f, 1.f );
		FlashScale.Y = Clamp( FlashScale.Y, 0.f, 1.f );
		FlashScale.Z = Clamp( FlashScale.Z, 0.f, 1.f );
		FlashFog.X   = Clamp( FlashFog.X  , 0.f, 1.f );
		FlashFog.Y   = Clamp( FlashFog.Y  , 0.f, 1.f );
		FlashFog.Z   = Clamp( FlashFog.Z  , 0.f, 1.f );
		if(FlashScale.X < 1.0f)
		{
			static FSolidColorTexture WhiteTexture(FColor(255,255,255,255));

			DECLARE_STATIC_UOBJECT(UProxyBitmapMaterial,WhiteTextureMaterial,
			{
				WhiteTextureMaterial->SetTextureInterface(&WhiteTexture);
			});

			DECLARE_STATIC_UOBJECT(UFinalBlend,ScreenFlashBlend,
			{
				ScreenFlashBlend->FrameBufferBlending = FB_AlphaBlend;
				ScreenFlashBlend->Material = WhiteTextureMaterial;
				ScreenFlashBlend->ZWrite = 0;
			});

			FCanvasUtil(&Viewport->RenderTarget,RI).DrawTile(0,0,
				Viewport->SizeX,Viewport->SizeY,0,0,0,0,0,ScreenFlashBlend,
				FColor(FPlane(FlashFog.X,FlashFog.Y,FlashFog.Z,
				1.0f - FlashScale.X)));
		}
	}
	unguard;
}

IMPLEMENT_CLASS(UScreenFade);
/* scion ======================================================================
 * UScreenFade::PostRender
 * Author: jg
 * 
 * Renders the screen fade using the color and the time specified
 *
 * input:	Viewport	The viewport we are drawing into
 *			RI			The render interface used for drawing
 *
 * ============================================================================
 */
void UScreenFade::PostRender(UViewport* Viewport,FRenderInterface* RI)
{
	guard(UScreenFade::PreRender);
	// If we still need to fade
	if (ElapsedFadeTime <= FadeTime ||
		(bFadeOut == 0 && bDisableOnComplete == 0))
	{
		static FSolidColorTexture WhiteTexture(FColor(255,255,255,255));

		DECLARE_STATIC_UOBJECT(UProxyBitmapMaterial,WhiteTextureMaterial,
		{
			WhiteTextureMaterial->SetTextureInterface(&WhiteTexture);
		});

		DECLARE_STATIC_UOBJECT(UFinalBlend,ScreenFadeBlend,
		{
			ScreenFadeBlend->FrameBufferBlending = FB_AlphaBlend;
			ScreenFadeBlend->Material = WhiteTextureMaterial;
			ScreenFadeBlend->ZWrite = 0;
		});
		// If we are starting, make the last time match the current
		if (LastFadeTime == 0.f)
		{
			LastFadeTime = Viewport->Actor->Level->TimeSeconds;
		}
		// Update the time counters
		ElapsedFadeTime += Viewport->Actor->Level->TimeSeconds - LastFadeTime;
		FColor Color(FadeColor);
		// Figure out how to blend with the scene depending on whether we are
		// fading in or out. If fading in and holding the last color clamp at
		// full alpha if we are past the elapsed time
		FadeColor.A = (ElapsedFadeTime > FadeTime) ? 255 :
			Clamp<BYTE>(255 * (bFadeOut ? (1.f - (ElapsedFadeTime / FadeTime)) : 
			(ElapsedFadeTime / FadeTime)),0,255);
		// Draw a full screen quad
		FCanvasUtil(&Viewport->RenderTarget,RI).DrawTile(0,0,
			Viewport->SizeX,Viewport->SizeY,0,0,0,0,0,ScreenFadeBlend,FadeColor);
		// Update the last time value
		LastFadeTime = Viewport->Actor->Level->TimeSeconds;
	}
	else
	{
		// If the caller has requested the effect stay on after fade in or we
		// are fading out, disable the camera effect
		if (bFadeOut == 0 && bDisableOnComplete == 1 || bFadeOut == 1)
		{
			// Disable the camera effect now
			Enabled = 0;
		}
	}
	unguard;
}
