/*=============================================================================
	UC2GLPSMaterialState.cpp: the render state of pixel-shader materials.

	UC2's D3DDrv/Src/D3DPSMaterialState.cpp carried over unchanged in behaviour (types
	renamed; pixel shader constants go to FOpenGLRenderInterface::SetPixelShaderConstant,
	which records them for M6). PC's D3DDrv runs these handlers for every pixel-shader
	material, including the Xbox-only ones it draws as their texture stage 0, so they
	decide blending, depth and culling of those fallbacks; R1 does the same.

	D3DPSMaterialState.cpp: pixel shader material state handling
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Sam Zamani
=============================================================================*/

#include "UC2GLDrv.h"

// scion sz - 
// Function pointers corresponding to entries in EPixelShader enum
// If a new entry is added to EPixelShader then one must exist here as well
FOpenGLPSMaterialStateHandlers::PSMatFuncPtr FOpenGLPSMaterialStateHandlers::PSMatFuncPtrArray[] =
{
	NULL,
	NULL,
	NULL,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_Blur,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DepthOfField,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_Mask,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_MotionBlur,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_Refraction,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_BloomSelect,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_EmissiveSelect,
	NULL,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_LuminanceTexIndex,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_UseTexIndex,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_Distortion,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_Distortion,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_Distortion,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_Distortion,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_Distortion,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_SkinShader,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_SkinShader,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_TeamSkinShader,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_TeamSkinShader,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_TeamSkinShader,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_SkinShaderOpacity,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_SkinShaderOpacity,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_SkinShaderOpacity,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_SkinShaderOpacity,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_TeamShader,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_TeamShader,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_TeamShader,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_TeamShader,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_EmissiveSkinShader,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_EmissiveSkinShader,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_EmissiveSkinShader,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_EmissiveShader,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_EmissiveShader,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_EmissiveShader,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_EmissiveShader,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_EmissiveConstColor,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_MaskedLuminance,
	NULL,
	NULL,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_PlayerCam,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_PlayerCam,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_PlayerCam,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DiffuseOpacity,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_ColorRemap,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_ColorRemap,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_DepthSwizzleColor,
	&FOpenGLPSMaterialStateHandlers::SetMatState_PS_ColorRemapGrayScale
};

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_Blur( UPixelShaderMaterial* InShader, 
										  FOpenGLModifierInfo* InModifierInfo, 
										  FOpenGLMaterialState* InMaterialState,
										  FOpenGLRenderInterface* RI )
{
    guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_Blur);

	InMaterialState->AlphaBlending	= 0;
	InMaterialState->AlphaTest = 1;
	InMaterialState->AlphaRef = 0;
	InMaterialState->ZTest = 0;
	InMaterialState->ZWrite = 0;

	UPSBlurMaterial* BlurShader = (UPSBlurMaterial*)(InShader);

	// set filter weights
	RI->SetPixelShaderConstant( 0, BlurShader->Weights, 4 );

	// set alpha blending mode
	if( BlurShader->BlurBlendMode == BLUR_ADD )
	{
		InMaterialState->AlphaBlending	= 1;
		InMaterialState->SrcBlend = D3DBLEND_SRCCOLOR;
		InMaterialState->DestBlend = D3DBLEND_ONE;
	}
	else if( BlurShader->BlurBlendMode == BLUR_EMISSIVE )
	{
		// blurred emissive select
		InMaterialState->PixelShader = PS_EmissiveSelectBlur;
		InMaterialState->AlphaBlending	= 1;
		InMaterialState->AlphaTest = 0;
		InMaterialState->ZTest = 0;
		InMaterialState->ZWrite = 0;
		InMaterialState->SrcBlend = D3DBLEND_SRCALPHA;
		InMaterialState->DestBlend = D3DBLEND_INVSRCALPHA;			
	}
	//scion jg -- Darken and blur at the same time
	else if (BlurShader->BlurBlendMode == BLUR_SHADOW)
	{
		InMaterialState->PixelShader = PS_ShadowBlur;
	}

	unguardSlow;
}

// scion sz -
void FOpenGLPSMaterialStateHandlers::SetMatState_PS_DepthOfField( UPixelShaderMaterial* InShader, 
										  FOpenGLModifierInfo* InModifierInfo, 
										  FOpenGLMaterialState* InMaterialState,
										  FOpenGLRenderInterface* RI )
{
    guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_DepthOfField);

	InMaterialState->AlphaBlending	= 1;
	InMaterialState->AlphaTest = 1;
	InMaterialState->AlphaRef = ((UPSDepthOfFieldMaterial*)InShader)->AlphaRef;
	InMaterialState->ZTest = 0;
	InMaterialState->ZWrite = 0;		
	InMaterialState->SrcBlend = D3DBLEND_SRCALPHA;
	InMaterialState->DestBlend = D3DBLEND_INVSRCALPHA;

	for( INT i=0; i<4; i++)
	{
		InMaterialState->Stages[i].TextureAddressU = D3DTADDRESS_CLAMP;
		InMaterialState->Stages[i].TextureAddressV = D3DTADDRESS_CLAMP;			
	}

	unguardSlow
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_MotionBlur( UPixelShaderMaterial* InShader, 
										  FOpenGLModifierInfo* InModifierInfo, 
										  FOpenGLMaterialState* InMaterialState,
										  FOpenGLRenderInterface* RI )
{
    guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_MotionBlur);

	InMaterialState->AlphaBlending	= 1;
	InMaterialState->AlphaTest = 1;
	InMaterialState->AlphaRef = 0;
	InMaterialState->ZTest = 0;
	InMaterialState->ZWrite = 0;
	InMaterialState->SrcBlend = D3DBLEND_SRCALPHA;
	InMaterialState->DestBlend = D3DBLEND_INVSRCALPHA;

	InMaterialState->Stages[0].MinFilter = D3DTEXF_POINT;
	InMaterialState->Stages[0].MagFilter = D3DTEXF_POINT;
	InMaterialState->Stages[0].MipFilter = D3DTEXF_NONE;

	unguardSlow
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_Mask( UPixelShaderMaterial* InShader, 
										  FOpenGLModifierInfo* InModifierInfo, 
										  FOpenGLMaterialState* InMaterialState,
										  FOpenGLRenderInterface* RI )
{
    guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_Mask);

	InMaterialState->AlphaBlending	= 1;
	InMaterialState->AlphaTest = 1;
	InMaterialState->AlphaRef = 0;
	InMaterialState->ZTest = 0;
	InMaterialState->ZWrite = 0;
	InMaterialState->SrcBlend = D3DBLEND_SRCALPHA;
	InMaterialState->DestBlend = D3DBLEND_INVSRCALPHA;

	unguardSlow
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_Refraction( UPixelShaderMaterial* InShader, 
										  FOpenGLModifierInfo* InModifierInfo, 
										  FOpenGLMaterialState* InMaterialState,
										  FOpenGLRenderInterface* RI )
{
	guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_Refraction);

	InMaterialState->AlphaBlending	= 1;
	InMaterialState->AlphaTest = 1;
	InMaterialState->AlphaRef = 0;
	InMaterialState->ZTest = 1;
	InMaterialState->ZWrite = 1;
	
	unguardSlow
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_BloomSelect( UPixelShaderMaterial* InShader, 
										  FOpenGLModifierInfo* InModifierInfo, 
										  FOpenGLMaterialState* InMaterialState,
										  FOpenGLRenderInterface* RI )
{
	guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_BloomSelect);

	UPSBloomSelectMaterial* pMaterial = (UPSBloomSelectMaterial*)InShader;

	InMaterialState->AlphaBlending	= 0;
	InMaterialState->AlphaTest = 1;
	InMaterialState->AlphaRef = 0;
	InMaterialState->ZTest = 0;
	InMaterialState->ZWrite = 0;

	// store luminosity transform as well as the
	// luminosity threshold value in constant c0
	static FLOAT MinClamp = 0.2f;	// setting this value lower will pick up more pixels for bloom
	static FLOAT MaxClamp = 0.8f;	// setting this value higher will exclude more pixels for bloom
	static FLOAT Bias = 0.05f;		// setting this value higher will exclude more pixels for bloom
	FLOAT LuminosityThreshold = Clamp<FLOAT>( pMaterial->LuminosityThreshold+Bias, MinClamp, MaxClamp );
	FLOAT LuminosityThresholdInv = Min( 1.f/(1.f-LuminosityThreshold), 2.0f );
	FLOAT flColor[] = { 0.f, 0.f, 
		LuminosityThresholdInv,
		LuminosityThreshold
	};

	RI->SetPixelShaderConstant(0,flColor,1);

	unguardSlow
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_EmissiveSelect( UPixelShaderMaterial* InShader, 
										  FOpenGLModifierInfo* InModifierInfo, 
										  FOpenGLMaterialState* InMaterialState,
										  FOpenGLRenderInterface* RI )
{
	guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_EmissiveSelect);

	InMaterialState->AlphaBlending	= 1;
		InMaterialState->AlphaTest = 1;
		InMaterialState->AlphaRef = 0;
		InMaterialState->ZTest = 0;
		InMaterialState->ZWrite = 0;
		InMaterialState->SrcBlend = D3DBLEND_SRCALPHA;
		InMaterialState->DestBlend = D3DBLEND_INVSRCALPHA;

	unguardSlow
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_LuminanceTexIndex( UPixelShaderMaterial* InShader, 
										  FOpenGLModifierInfo* InModifierInfo, 
										  FOpenGLMaterialState* InMaterialState,
										  FOpenGLRenderInterface* RI )
{
    guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_LuminanceTexIndex);

	// Material is opaque
	InMaterialState->SrcBlend = D3DBLEND_ONE;
	InMaterialState->DestBlend = D3DBLEND_ZERO;
	InMaterialState->AlphaBlending = 0;

	InMaterialState->AlphaRef = 0;
	InMaterialState->ZTest = 1;
	InMaterialState->ZWrite = 1;
	// Get the color and pass that as the shader constant
	static FLOAT flColor[] = { 0.30f, 0.59f, 0.11f, 1.0f };
	// The shader expects c1 as having the color to modulate
	RI->SetPixelShaderConstant(0,flColor,1);

	unguardSlow
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_UseTexIndex( UPixelShaderMaterial* InShader, 
										  FOpenGLModifierInfo* InModifierInfo, 
										  FOpenGLMaterialState* InMaterialState,
										  FOpenGLRenderInterface* RI )
{
    guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_UseTexIndex);

	// Set the render states depending on whether we have a mask or not
	UPSUseTexIndexMaterial* pShader = (UPSUseTexIndexMaterial*)InShader;
	if (pShader->Mask != NULL)
	{
		InMaterialState->AlphaBlending = 1;
		InMaterialState->AlphaTest = 1;
	}
	else
	{
		InMaterialState->AlphaBlending = 0;
		InMaterialState->AlphaTest = 0;
	}
	InMaterialState->AlphaRef = 0;
	InMaterialState->ZTest = 0;
	InMaterialState->ZWrite = 0;

	unguardSlow
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_Distortion( UPixelShaderMaterial* InShader, 
										  FOpenGLModifierInfo* InModifierInfo, 
										  FOpenGLMaterialState* InMaterialState,
										  FOpenGLRenderInterface* RI )
{
    guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_Distortion);

	//scion jg -- Two sided support
	InMaterialState->TwoSided =
		((UPSDistortionBaseMaterial*)InShader)->bTwoSided;

	InMaterialState->AlphaBlending = 1;
	InMaterialState->AlphaTest = 1;
	InMaterialState->AlphaRef = 0;	
	InMaterialState->ZTest = 1;
	InMaterialState->ZWrite = 0;	

	// UC2GLDrv (M7): D3DDrv's "#ifdef _XBOX" is the device's post-process switch; the
	// "#else" branch below is PC's (and M6's) state.
	if( RI->RenDev->PostProcessActive() )
	{
	// scion sz -
	// current stage of post processing
	switch( RI->GetPostProcessStage() )
	{
	case FRenderInterface::PP_STAGE_ACCUMULATE:
		{
			// override pixel shader to use the accumulation distortion shader
			InMaterialState->PixelShader = PS_DistortionAccum;
			// add to values already in the accumulation render target
			InMaterialState->SrcBlend = D3DBLEND_ONE;
			InMaterialState->DestBlend = D3DBLEND_ONE;
			// bias towards higher mipmap surface level
			InMaterialState->Stages[0].TextureMipLODBias = -1.8f;
		}
		break;
	case FRenderInterface::PP_STAGE_NONE:
	case FRenderInterface::PP_STAGE_RENDER_DISTORTION:
		{
			// help fill-rate
			InMaterialState->ZTest = 0;
			InMaterialState->AlphaBlending = 0;
			// alpha blend
			InMaterialState->SrcBlend = D3DBLEND_SRCALPHA;		
			InMaterialState->DestBlend = D3DBLEND_INVSRCALPHA;
			// use the actual distortion pixel shader
			InMaterialState->PixelShader = PS_Distortion;
			// disable texture filtering modes for back buffer fetch
			// in tex stage 1
			InMaterialState->Stages[1].MinFilter = D3DTEXF_POINT;
			InMaterialState->Stages[1].MagFilter = D3DTEXF_POINT;
			InMaterialState->Stages[1].MipFilter = D3DTEXF_NONE;
			// disable texture filtering modes for back buffer fetch
			// in tex stage 2
			InMaterialState->Stages[2].MinFilter = D3DTEXF_POINT;
			InMaterialState->Stages[2].MagFilter = D3DTEXF_POINT;
			InMaterialState->Stages[2].MipFilter = D3DTEXF_NONE;
			// Make sure stage 3 has tex coords when no tex gen mode is set
			// stage 3 tex coordinates copied from stage 0
			if (InMaterialState->Stages[3].TexCoordIndex <= TCS_Stream3)
			{				
				InMaterialState->Stages[3].TexCoordIndex = TCS_Stream0;
			}
		}
		break;
	case FRenderInterface::PP_STAGE_RENDER_COLOR:
		{
			//scion jg -- Set the constant color for all distortion types
			// except the basic distortion
			if (InShader->PixelShaderType != PS_Distortion)
			{
				UPSBlendedDistortionMaterial* pMaterial =
					(UPSBlendedDistortionMaterial*)InShader;

				switch( pMaterial->DiffuseBlendMode )
				{
				case DIST_Add:
					InMaterialState->SrcBlend = D3DBLEND_ONE;
					InMaterialState->DestBlend = D3DBLEND_ONE;
					break;
				case DIST_Modulate:
					InMaterialState->SrcBlend = D3DBLEND_DESTCOLOR;
					InMaterialState->DestBlend = D3DBLEND_SRCCOLOR;
					break;
				case DIST_Subtract:
					InMaterialState->SrcBlend = D3DBLEND_ONE;
					InMaterialState->DestBlend = D3DBLEND_ONE;
					// destination - source
					InMaterialState->BlendOp = D3DBLENDOP_REVSUBTRACT;
					break;
				}

				// Get the color and pass that as the shader constant
				FLOAT flColor[] = { pMaterial->ConstantColor.R * ONE_OVER_255,
					pMaterial->ConstantColor.G * ONE_OVER_255,
					pMaterial->ConstantColor.B * ONE_OVER_255,
					pMaterial->ConstantColor.A * ONE_OVER_255 };
				// The shader expects c0 as having the color to modulate
				RI->SetPixelShaderConstant(0,flColor,1);
			}
			// shouldn't be rendering non-blended distortion objects
			// at this point, but just in case we do
			else
			{
				// don't render anything
				// UC2GLDrv: Epic also writes "InShader->PixelShaderType = NULL" here and in the
				// default case: that changes the material object for good (its PreRender,
				// vertex shader and state handler are chosen by this value on every later
				// use, so it would be drawn as a fixed-function material from then on).
				// Not carried over; the colour write mask alone draws nothing.
				InMaterialState->ColorWriteMask = CWM_COLORWRITEENABLE_NONE;
			}
		}
		break;	
	default:
		// don't render anything
		InMaterialState->ColorWriteMask = CWM_COLORWRITEENABLE_NONE;
	}
	}
	else
	{
	InMaterialState->SrcBlend = D3DBLEND_ONE;
	InMaterialState->DestBlend = D3DBLEND_ZERO;
	}

	unguardSlow
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_SkinShader( UPixelShaderMaterial* InShader, 
										  FOpenGLModifierInfo* InModifierInfo, 
										  FOpenGLMaterialState* InMaterialState,
										  FOpenGLRenderInterface* RI )
{
    guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_SkinShader);

	//scion jg -- Skin shaders for regular skins
	// Cast so we can get to the specific info
	UPSSkinShader* pMaterial = (UPSSkinShader*)InShader;
	// Get the color modifier from the correct place
	FColor& Color = (InModifierInfo->ModifyColor == 1) ?
		InModifierInfo->TFactorColor : pMaterial->Color;
	// Get the color and pass that as the shader constant
	FLOAT flColor[] = { Color.R * ONE_OVER_255,Color.G * ONE_OVER_255,
		Color.B * ONE_OVER_255,Color.A * ONE_OVER_255,
		16.f, 16.f, 16.f, 16.f,};
	// Set the render states
	if (Color.A == 255)
	{
		// Material is opaque
		InMaterialState->SrcBlend = D3DBLEND_ONE;
		InMaterialState->DestBlend = D3DBLEND_ZERO;
		InMaterialState->AlphaBlending = 0;
	}
	else
	{
		// Material is translucent so turn on alpha blending
		InMaterialState->SrcBlend = D3DBLEND_SRCALPHA;
		InMaterialState->DestBlend = D3DBLEND_INVSRCALPHA;
		InMaterialState->AlphaBlending = 1;
	}
	InMaterialState->AlphaTest = 0;
	InMaterialState->OverrideFogColor = 0;
	InMaterialState->ZWrite	= 1;
	InMaterialState->ZTest = 1;
	InMaterialState->TwoSided = InModifierInfo->TwoSided || pMaterial->bIsTwoSided;
	// Choose the correct shader
	pMaterial->PixelShaderType = pMaterial->bUseLightingForSpecular ?
		pMaterial->BaseShaderType + 1 : pMaterial->BaseShaderType;
	// The shader expects c0 as having the color to modulate
	RI->SetPixelShaderConstant(0,flColor,2);

	unguardSlow
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_TeamSkinShader( UPixelShaderMaterial* InShader, 
										  FOpenGLModifierInfo* InModifierInfo, 
										  FOpenGLMaterialState* InMaterialState,
										  FOpenGLRenderInterface* RI )
{
    guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_TeamSkinShader);

	//	if (InShader->PixelShaderType >= PS_TeamSkinShader &&
	//		InShader->PixelShaderType <= PS_TeamSkinShaderSpecLightAlpha)

	//scion jg -- Team emissive skin shaders
	// Cast so we can get to the specific info
	UPSTeamSkinShader* pMaterial = (UPSTeamSkinShader*)InShader;
	// Set the render states
	InMaterialState->AlphaBlending = 1;
	InMaterialState->AlphaTest = 0;
	InMaterialState->OverrideFogColor = 0;
	InMaterialState->ZWrite	= 1;
	InMaterialState->ZTest = 1;
	InMaterialState->TwoSided = pMaterial->bIsTwoSided || InModifierInfo->TwoSided;
	// Get the color modifier from the correct place
	FColor& Color = (InModifierInfo->ModifyColor == 1) ?
		InModifierInfo->TFactorColor : pMaterial->Color;
	// Don't change my render states on me damnit!
	InModifierInfo->ModifyFramebufferBlending = 0;
	// Get the color and pass that as the shader constant
	FLOAT flConst[] = { Color.R * ONE_OVER_255,Color.G * ONE_OVER_255,
		Color.B * ONE_OVER_255,Color.A * ONE_OVER_255,
		16.f, 16.f, 16.f, 16.f};
	// The shader expects c0 as having a crazy constant to multiply against
	RI->SetPixelShaderConstant(0,flConst,2);
	// Set the blend modes differently if being alphaed or not
	if (InModifierInfo->ModifyColor == 1 && Color.A == 255)
	{
		// Normal emissive writing state
		InMaterialState->SrcBlend = D3DBLEND_ONE;
		InMaterialState->DestBlend = D3DBLEND_ZERO;
		// Choose the correct pixel shader
		pMaterial->PixelShaderType = PS_TeamSkinShader + 
			pMaterial->bUseLightingForSpecular;
		InMaterialState->PixelShader =
			(EPixelShader)pMaterial->PixelShaderType;
	}
	else
	{
		// Alpha the character so no emissive writing
		InMaterialState->SrcBlend = D3DBLEND_SRCALPHA;
		InMaterialState->DestBlend = D3DBLEND_INVSRCALPHA;
		InMaterialState->ColorWriteMask = CWM_COLORWRITEENABLE_COLORONLY;
		// Choose the correct pixel shader
		pMaterial->PixelShaderType = PS_TeamSkinShaderSpecLightAlpha;
		InMaterialState->PixelShader =
			(EPixelShader)pMaterial->PixelShaderType;
	}

	unguardSlow
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_SkinShaderOpacity( UPixelShaderMaterial* InShader, 
										  FOpenGLModifierInfo* InModifierInfo, 
										  FOpenGLMaterialState* InMaterialState,
										  FOpenGLRenderInterface* RI )
{
    guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_SkinShaderOpacity);

	//	if( InShader->PixelShaderType >= PS_SkinShaderOpacity && 
	//		InShader->PixelShaderType <= PS_TeamSkinShaderSpecLightOpacity )

	// scion sz - skin shaders with opacity
	UPSSkinShaderOpacity* pMaterial = (UPSSkinShaderOpacity*)InShader;

	FColor& Color = (InModifierInfo->ModifyColor == 1) ?	InModifierInfo->TFactorColor : pMaterial->Color;

	FLOAT flColor[] = { 
		Color.R * ONE_OVER_255,
			Color.G * ONE_OVER_255,
			Color.B * ONE_OVER_255,
			Color.A * ONE_OVER_255,
			16.f, 16.f, 16.f, 16.f
	};

	InMaterialState->SrcBlend		= D3DBLEND_ONE;
	InMaterialState->DestBlend	= D3DBLEND_ZERO;
	InMaterialState->AlphaBlending = 0;
	InMaterialState->AlphaTest = 1;
	InMaterialState->AlphaRef = 127;
	InMaterialState->ZWrite	= 1;
	InMaterialState->ZTest = 1;
	InMaterialState->TwoSided = pMaterial->bIsTwoSided || InModifierInfo->TwoSided;;

	// use PS_SkinShaderSpecLightOpacity or PS_TeamSkinShaderSpecLightOpacity
	// bUseLightingForSpecular == 1
	InMaterialState->PixelShader = (EPixelShader)(pMaterial->PixelShaderType + pMaterial->bUseLightingForSpecular);

	// The shader expects c0 as having the color to modulate
	// The shader expects c1 as having a crazy constant to multiply against
	RI->SetPixelShaderConstant(0,flColor,2);

	unguardSlow
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_TeamShader( UPixelShaderMaterial* InShader, 
										  FOpenGLModifierInfo* InModifierInfo, 
										  FOpenGLMaterialState* InMaterialState,
										  FOpenGLRenderInterface* RI )
{
    guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_TeamShader);

	//	if (InShader->PixelShaderType >= PS_TeamShader &&
	//		InShader->PixelShaderType <= PS_TeamBSPShader)

	//scion jg -- Team shaders for world objects

	// Cast so we can get to the specific info
	UPSTeamShader* pMaterial = (UPSTeamShader*)InShader;
	// Handle late binding of the light map if needed
	if (pMaterial->bIsUsedForBSP == 1)
	{
		InMaterialState->Stages[2].TextureAddressU = D3DTADDRESS_CLAMP;
		InMaterialState->Stages[2].TextureAddressV = D3DTADDRESS_CLAMP;
		InMaterialState->Stages[2].TexCoordIndex = TCS_Stream1;
		InMaterialState->Stages[2].Texture = RI->CurrentState->Lightmap;
		InMaterialState->StagesUsed++;
	}
	// Get the color modifier from the correct place
	FColor& Color = (InModifierInfo->ModifyColor == 1) ?
		InModifierInfo->TFactorColor : pMaterial->Color;
	// Get the color and pass that as the shader constant
	FLOAT flColor[] = { Color.R * ONE_OVER_255,Color.G * ONE_OVER_255,
		Color.B * ONE_OVER_255,Color.A * ONE_OVER_255 };
	// Set the render states
	if (Color.A == 255 && pMaterial->IsTransparent() == 0)
	{
		// Material is opaque
		InMaterialState->SrcBlend = D3DBLEND_ONE;
		InMaterialState->DestBlend = D3DBLEND_ZERO;
		InMaterialState->AlphaBlending = 0;
	}
	else
	{
		// Material is translucent so turn on alpha blending
		InMaterialState->SrcBlend = D3DBLEND_SRCALPHA;
		InMaterialState->DestBlend = D3DBLEND_INVSRCALPHA;
		InMaterialState->AlphaBlending = 1;
	}
	InMaterialState->AlphaTest = 0;
	InMaterialState->OverrideFogColor = 0;
	InMaterialState->ZWrite	= 1;
	InMaterialState->ZTest = 1;
	InMaterialState->TwoSided = pMaterial->bIsTwoSided;
	// The shader expects c0 as having the color to modulate
	RI->SetPixelShaderConstant(0,flColor,1);	
	
	unguardSlow
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse( UPixelShaderMaterial* InShader, 
										  FOpenGLModifierInfo* InModifierInfo, 
										  FOpenGLMaterialState* InMaterialState,
										  FOpenGLRenderInterface* RI )
{
    guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_DualDiffuse);

	//	if( InShader->PixelShaderType >= PS_DualDiffuse_NM_Add_NSL &&
	//		InShader->PixelShaderType <= PS_ShenuSkin)

	//scion jg -- Dual diffuse shader
	// Cast so we can get to the specific info
	UPSBaseShader* pMaterial = (UPSBaseShader*)InShader;
	// Set the render states
	if (pMaterial->IsTransparent() == 0)
	{
		// Material is opaque
		InMaterialState->SrcBlend = D3DBLEND_ONE;
		InMaterialState->DestBlend = D3DBLEND_ZERO;
		InMaterialState->AlphaBlending = 0;
	}
	else
	{
		// Material is translucent so turn on alpha blending
		InMaterialState->SrcBlend = D3DBLEND_SRCALPHA;
		InMaterialState->DestBlend = D3DBLEND_INVSRCALPHA;
		InMaterialState->AlphaBlending = 1;
	}
	InMaterialState->AlphaTest = 0;
	InMaterialState->OverrideFogColor = 0;
	InMaterialState->ZWrite	= 1;
	InMaterialState->ZTest = 1;
	InMaterialState->TwoSided = pMaterial->bIsTwoSided;
	
	unguardSlow
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_EmissiveShader( UPixelShaderMaterial* InShader, 
										  FOpenGLModifierInfo* InModifierInfo, 
										  FOpenGLMaterialState* InMaterialState,
										  FOpenGLRenderInterface* RI )
{
    guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_EmissiveShader);

	//	if (InShader->PixelShaderType >= PS_EmissiveShaderDiffuseOnly &&
	//		InShader->PixelShaderType <= PS_EmissiveSpecular)

	// Cast so we can get to the specific info
	UPSEmissiveShader* pMaterial = (UPSEmissiveShader*)InShader;
	// Set blend modes as specified
	switch (pMaterial->EmissiveBlendMode)
	{
	case EBM_Normal:
		{
			// Overwrite the framebuffer
			InMaterialState->SrcBlend = D3DBLEND_ONE;
			InMaterialState->DestBlend = D3DBLEND_ZERO;
			break;
		}
	case EBM_Additive:
		{
			// Add to the framebuffer
			InMaterialState->SrcBlend = D3DBLEND_ONE;
			InMaterialState->DestBlend = D3DBLEND_INVSRCCOLOR;
			break;
		}
	case EBM_Subtractive:
		{
			// Subtract from the framebuffer
			InMaterialState->SrcBlend = D3DBLEND_ZERO;
			InMaterialState->DestBlend = D3DBLEND_INVSRCCOLOR;
			break;
		}
	case EBM_Modulate:
		{
			// Modulate with the framebuffer
			InMaterialState->SrcBlend = D3DBLEND_DESTCOLOR;
			InMaterialState->DestBlend = D3DBLEND_SRCCOLOR;
			break;
		}
	}
	// Set the render states
	InMaterialState->AlphaBlending = 1;
	InMaterialState->AlphaTest = 0;
	InMaterialState->OverrideFogColor = 0;
	InMaterialState->ZWrite	= 1;
	InMaterialState->ZTest = 1;
	InMaterialState->TwoSided = pMaterial->bIsTwoSided;
	// Get the color modifier from the shader or the 
	FColor& Color = (InModifierInfo->ModifyColor == 1) ?
		InModifierInfo->TFactorColor : pMaterial->EmissiveColor;
	FLOAT flConst[] = { 16.f, 16.f, 16.f, 16.f,
		Color.R * ONE_OVER_255, Color.G * ONE_OVER_255,
		Color.B * ONE_OVER_255, Color.A * ONE_OVER_255 };
	// The shader expects c0 as having a crazy constant to multiply against
	RI->SetPixelShaderConstant(0,flConst,2);

	unguardSlow
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_EmissiveConstColor( UPixelShaderMaterial* InShader, 
										  FOpenGLModifierInfo* InModifierInfo, 
										  FOpenGLMaterialState* InMaterialState,
										  FOpenGLRenderInterface* RI )
{
    guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_EmissiveConstColor);

	// scion sz - first emissive test
	UPSEmissiveConstColor* pMaterial = Cast<UPSEmissiveConstColor>( InShader );
	FLOAT flConst[] = 
	{ 
		pMaterial->Color.R * ONE_OVER_255,
			pMaterial->Color.G * ONE_OVER_255,
			pMaterial->Color.B * ONE_OVER_255,
			pMaterial->Color.A * ONE_OVER_255,
	};
	RI->SetPixelShaderConstant(0,flConst,1);

	InMaterialState->SrcBlend = D3DBLEND_ONE;
	InMaterialState->DestBlend = D3DBLEND_ZERO;
	InMaterialState->AlphaBlending = 1;
	InMaterialState->AlphaTest = 0;
	InMaterialState->OverrideFogColor = 0;
	InMaterialState->ZWrite	= 1;
	InMaterialState->ZTest = 1;	

	unguardSlow
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_EmissiveSkinShader( UPixelShaderMaterial* InShader, 
										  FOpenGLModifierInfo* InModifierInfo, 
										  FOpenGLMaterialState* InMaterialState,
										  FOpenGLRenderInterface* RI )
{
    guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_EmissiveSkinShader);

	//	if (InShader->PixelShaderType >= PS_EmissiveSkinShader &&
	//		InShader->PixelShaderType <= PS_EmissiveSkinShaderSpecLightAlpha)

	//scion jg -- Emissive skin shaders
	// Cast so we can get to the specific info
	UPSSkinShader* pMaterial = (UPSSkinShader*)InShader;
	// Set the render states
	InMaterialState->AlphaBlending = 1;
	InMaterialState->AlphaTest = 0;
	InMaterialState->OverrideFogColor = 0;
	InMaterialState->ZWrite	= 1;
	InMaterialState->ZTest = 1;
	InMaterialState->TwoSided = pMaterial->bIsTwoSided || InModifierInfo->TwoSided;
	// Don't change my render states on me damnit!
	InModifierInfo->ModifyFramebufferBlending = 0;
	// Get the color modifier from the correct place
	const FColor& Color = (InModifierInfo->ModifyColor == 1) ?
		InModifierInfo->TFactorColor : pMaterial->Color;
	// Get the color and pass that as the shader constant
	FLOAT flConst[] = { 16.f, 16.f, 16.f, 16.f,
		Color.R * ONE_OVER_255,Color.G * ONE_OVER_255,
		Color.B * ONE_OVER_255,Color.A * ONE_OVER_255};
	// The shader expects c0 as having a crazy constant to multiply against
	RI->SetPixelShaderConstant(0,flConst,2);
	// Set the blend modes differently if being alphaed or not
	if (InModifierInfo->ModifyColor == 0 ||
		(InModifierInfo->ModifyColor == 1 && Color.A == 255))
	{
		// Normal emissive writing state
		InMaterialState->SrcBlend = D3DBLEND_ONE;
		InMaterialState->DestBlend = D3DBLEND_ZERO;
		// Choose the correct pixel shader
		pMaterial->PixelShaderType = PS_EmissiveSkinShader + 
			pMaterial->bUseLightingForSpecular;
		InMaterialState->PixelShader =
			(EPixelShader)pMaterial->PixelShaderType;
	}
	else
	{
		// Alpha the character so no emissive writing
		InMaterialState->SrcBlend = D3DBLEND_SRCALPHA;
		InMaterialState->DestBlend = D3DBLEND_INVSRCALPHA;
		InMaterialState->ColorWriteMask = CWM_COLORWRITEENABLE_COLORONLY;
		// Choose the correct pixel shader
		pMaterial->PixelShaderType = PS_EmissiveSkinShaderSpecLightAlpha;
		InMaterialState->PixelShader =
			(EPixelShader)pMaterial->PixelShaderType;
	}

	unguardSlow
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_MaskedLuminance( UPixelShaderMaterial* InShader, 
										  FOpenGLModifierInfo* InModifierInfo, 
										  FOpenGLMaterialState* InMaterialState,
										  FOpenGLRenderInterface* RI )
{
    guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_MaskedLuminance);

	// Material is translucent so turn on alpha blending
	InMaterialState->SrcBlend = D3DBLEND_SRCALPHA;
	InMaterialState->DestBlend = D3DBLEND_INVSRCALPHA;
	InMaterialState->AlphaBlending = 1;
	InMaterialState->TwoSided = ((UPSBackBufferEffect*)InShader)->bIsTwoSided;

	InMaterialState->AlphaRef = 0;
	InMaterialState->ZTest = 1;
	InMaterialState->ZWrite = 1;
	// Get the color and pass that as the shader constant
	static FLOAT flColor[] = { 0.30f, 0.59f, 0.11f, 1.0f };
	// The shader expects c1 as having the color to modulate
	RI->SetPixelShaderConstant(0,flColor,1);

	unguardSlow
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_PlayerCam( UPixelShaderMaterial* InShader, 
										  FOpenGLModifierInfo* InModifierInfo, 
										  FOpenGLMaterialState* InMaterialState,
										  FOpenGLRenderInterface* RI )
{
    guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_PlayerCam);

	//	if (InShader->PixelShaderType >= PS_PlayerCam &&
	//		InShader->PixelShaderType <= PS_PlayerCamTwoNoise)

	InMaterialState->SrcBlend = D3DBLEND_ONE;
	InMaterialState->DestBlend = D3DBLEND_ZERO;
	InMaterialState->AlphaBlending = 0;
	InMaterialState->AlphaTest = 0;
	InMaterialState->AlphaRef = 0;
	InMaterialState->ZTest = 1;
	InMaterialState->ZWrite = 1;

	unguardSlow
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_DiffuseOpacity( UPixelShaderMaterial* InShader, 
										  FOpenGLModifierInfo* InModifierInfo, 
										  FOpenGLMaterialState* InMaterialState,
										  FOpenGLRenderInterface* RI )
{
	guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_DiffuseOpacity);

	// Material is translucent so turn on alpha blending
	InMaterialState->SrcBlend = D3DBLEND_SRCALPHA;
	InMaterialState->DestBlend = D3DBLEND_INVSRCALPHA;
	InMaterialState->AlphaBlending = 1;
	InMaterialState->AlphaTest = 1;
	InMaterialState->AlphaRef = 0;
	InMaterialState->ZTest = 1;
	InMaterialState->ZWrite = 1;
	InMaterialState->TwoSided = ((UPSDiffuseOpacity*)InShader)->bIsTwoSided;

	unguardSlow
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_ColorRemap( UPixelShaderMaterial* InShader, 
													 FOpenGLModifierInfo* InModifierInfo, 
													 FOpenGLMaterialState* InMaterialState,
													 FOpenGLRenderInterface* RI )
{
	guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_ColorRemap);

	//	if (InShader->PixelShaderType == PS_ColorRemap ||
	//		InShader->PixelShaderType == PS_ColorRemapMasked)

	UPSColorRemapMaterial* ColorRemap = (UPSColorRemapMaterial*)InShader;
	// Material is opaque
	InMaterialState->SrcBlend = D3DBLEND_ONE;
	InMaterialState->DestBlend = D3DBLEND_ZERO;
	InMaterialState->AlphaBlending = 0;
	InMaterialState->AlphaRef = 0;
	InMaterialState->ZTest = ColorRemap->bUseZ;
	InMaterialState->ZWrite = ColorRemap->bUseZ;
	// The amount to blend between the remap and backbuffer
	FLOAT Blend[] = { ColorRemap->BlendAmount, ColorRemap->BlendAmount,
		ColorRemap->BlendAmount, ColorRemap->BlendAmount };
	// The shader expects c1 as having the color to modulate
	RI->SetPixelShaderConstant(0,Blend,1);

	unguardSlow
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_DepthSwizzleColor( UPixelShaderMaterial* InShader, 
													 FOpenGLModifierInfo* InModifierInfo, 
													 FOpenGLMaterialState* InMaterialState,
													 FOpenGLRenderInterface* RI )
{
	guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_DepthSwizzleColor);

	// to improve fillrate a bit
	InMaterialState->AlphaBlending = 0;
	InMaterialState->AlphaTest = 0;
	InMaterialState->ZTest = 0;
	InMaterialState->ZWrite = 0;

	unguardSlow;
}

void FOpenGLPSMaterialStateHandlers::SetMatState_PS_ColorRemapGrayScale(
	UPixelShaderMaterial* InShader,FOpenGLModifierInfo* InModifierInfo, 
	FOpenGLMaterialState* InMaterialState,FOpenGLRenderInterface* RI)
{
	guardSlow(FOpenGLPSMaterialStateHandlers::SetMatState_PS_ColorRemapGrayScale);

	//	if (InShader->PixelShaderType == PS_ColorRemap ||
	//		InShader->PixelShaderType == PS_ColorRemapMasked)

	UPSColorRemapMaterial* ColorRemap = (UPSColorRemapMaterial*)InShader;
	// Material is opaque
	InMaterialState->SrcBlend = D3DBLEND_ONE;
	InMaterialState->DestBlend = D3DBLEND_ZERO;
	InMaterialState->AlphaBlending = 0;
	InMaterialState->AlphaRef = 0;
	InMaterialState->ZTest = ColorRemap->bUseZ;
	InMaterialState->ZWrite = ColorRemap->bUseZ;
	// The amount to blend between the remap and backbuffer
	FLOAT Blend[] = { ColorRemap->BlendAmount, ColorRemap->BlendAmount,
		ColorRemap->BlendAmount, ColorRemap->BlendAmount,
		0.30f, 0.59f, 0.11f, 1.0f };
	// The shader expects c1 as having the color to modulate
	RI->SetPixelShaderConstant(0,Blend,2);

	unguardSlow
}

