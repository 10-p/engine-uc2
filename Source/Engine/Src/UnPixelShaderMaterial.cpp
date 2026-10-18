/* scion ======================================================================
* Author: sz
* ============================================================================
* UnPixelShaderMaterial.cpp
*
* This file contains pixel shader material implementations
*
* ============================================================================
*/

#include "EnginePrivate.h"

IMPLEMENT_CLASS(UPixelShaderMaterial);
IMPLEMENT_CLASS(UPSBlurMaterial);
IMPLEMENT_CLASS(UPSDepthOfFieldMaterial);
IMPLEMENT_CLASS(UPSMotionBlurMaterial);
IMPLEMENT_CLASS(UPSRefractionMaterial);
IMPLEMENT_CLASS(UPSDistortionBaseMaterial);
IMPLEMENT_CLASS(UPSDistortionMaterial);
IMPLEMENT_CLASS(UPSModulatedDistortionMaterial);
IMPLEMENT_CLASS(UPSAdditiveDistortion);
IMPLEMENT_CLASS(UPSSubtractiveDistortion);
IMPLEMENT_CLASS(UPSBlendedDistortionMaterial);
IMPLEMENT_CLASS(UPSBloomSelectMaterial);
IMPLEMENT_CLASS(UPSLuminanceTexIndexMaterial);
IMPLEMENT_CLASS(UPSUseTexIndexMaterial);
IMPLEMENT_CLASS(UPSSkinShader);
IMPLEMENT_CLASS(UPSTeamSkinShader);
IMPLEMENT_CLASS(UPSBaseShader);
IMPLEMENT_CLASS(UPSDualDiffuse);
IMPLEMENT_CLASS(UPSDiffuseOpacitySpecularMask);
IMPLEMENT_CLASS(UPSShenuSkin);
IMPLEMENT_CLASS(UPSEmissiveSkinShader);
IMPLEMENT_CLASS(UPSEmissiveConstColor);
IMPLEMENT_CLASS(UPSEmissiveShader);
IMPLEMENT_CLASS(UPSEmissiveSpecular);
IMPLEMENT_CLASS(UPSTeamShader);
IMPLEMENT_CLASS(UPSSkinShaderOpacity);
IMPLEMENT_CLASS(UPSTeamSkinShaderOpacity);
IMPLEMENT_CLASS(UPSBackBufferEffect);
IMPLEMENT_CLASS(UPSPlayerCamMaterial);
IMPLEMENT_CLASS(UPSDiffuseOpacity);
IMPLEMENT_CLASS(UPSColorRemapMaterial);

//
// UPixelShaderMaterial::PreRender
//
void UPixelShaderMaterial::PreRender(FRenderInterface* RI)
{
}

//
// UPSBlurMaterial::PreRender
//
void UPSBlurMaterial::PreRender(FRenderInterface* RI)
{
	guard(UPSBlurMaterial::PreRender);
	Super::PreRender(RI);

	if( BlurSourceOffset0 )
	{
		BlurSourceOffset0->Material = BlurSourceMaterial;
		TextureStage0 = BlurSourceOffset0;
	}

	if( BlurSourceOffset1 )
	{
		BlurSourceOffset1->Material = BlurSourceMaterial;
		TextureStage1 = BlurSourceOffset1;
	}

	if( BlurSourceOffset2 )
	{
		BlurSourceOffset2->Material = BlurSourceMaterial;
		TextureStage2 = BlurSourceOffset2;
	}

	if( BlurSourceOffset3 )
	{
		BlurSourceOffset3->Material = BlurSourceMaterial;
		TextureStage3 = BlurSourceOffset3;
	}

	unguard;
}

//
// UPSDepthOfFieldMaterial Constructor
//
UPSDepthOfFieldMaterial::UPSDepthOfFieldMaterial()
{
	guard(UPSDepthOfFieldMaterial::UPSDepthOfFieldMaterial);

	DynamicRampLookup = ConstructObject<UTexture>( UTexture::StaticClass() );
	DynamicRampLookup->Format = TEXF_A8;
	DynamicRampLookup->Init( 64, 512 );
	//jg -- Don't try to fetch this texture from a bundle since it is
	// runtime generated
	DynamicRampLookup->bParametric = TRUE;

	unguard;
}

void UPSDepthOfFieldMaterial::Destroy()
{
	guard(UPSDepthOfFieldMaterial::Destroy);

	if( DynamicRampLookup )
	{
		delete DynamicRampLookup;
		DynamicRampLookup=NULL;
	}

	Super::Destroy();

	unguard;

}

//
// UPSDepthOfFieldMaterial::PreRender
//
void UPSDepthOfFieldMaterial::PreRender(FRenderInterface* RI)
{
	guard(UPSDepthOfFieldMaterial::PreRender);
	Super::PreRender(RI);

	TextureStage0 = BlurMaterial;
	TextureStage1 = DepthBufferMaterial;
	// this texture is swizzled so it doesn't need a tex matrix for u,v -> s,t conversion
	TextureStage2 = DynamicRampLookup;
//	TextureStage3 = BlurMoreMaterial;
	
	unguard;
}

//
// UPSMotionBlurMaterial Constructor
//
UPSMotionBlurMaterial::UPSMotionBlurMaterial()
{
	guard(UPSMotionBlurMaterial::UPSMotionBlurMaterial);	
	unguard;
}

//
// UPSMotionBlurMaterial::Destroy
//
void UPSMotionBlurMaterial::Destroy()
{
	guard(UPSMotionBlurMaterial::Destroy);
	Super::Destroy();
	unguard;
}

//
// UPSMotionBlurMaterial::PreRender
//
void UPSMotionBlurMaterial::PreRender(FRenderInterface* RI)
{
	guard(UPSMotionBlurMaterial::PreRender);
	Super::PreRender(RI);

	TextureStage0 = BackBufferModifier;
	TextureStage1 = SourceMask;

	unguard;
}

//
// UPSRefractionMaterial::UPSRefractionMaterial
//
UPSRefractionMaterial::UPSRefractionMaterial()
{
	guard(UPSRefractionMaterial::UPSRefractionMaterial);

	if( !GIsEditor )
	{
		BackBufferMaterial = ConstructObject<UTexMatrix>(UTexMatrix::StaticClass());
	}

	// -- this matrix transforms [-1,1] screen space coordinates to texture space [0,1]
	MatTextureSpace.SetIdentity();
	MatTextureSpace.M[0][0] = 0.5f;	MatTextureSpace.M[0][1] = 0.0f;
	MatTextureSpace.M[1][0] = 0.0f;	MatTextureSpace.M[1][1] = -0.5f;
	MatTextureSpace.M[3][0] = 0.5f;	MatTextureSpace.M[3][1] = 0.5f;

	unguard;
}

//
// UPSRefractionMaterial::PreRender
//
void UPSRefractionMaterial::PreRender(FRenderInterface* RI)
{
	guard(UPSRefractionMaterial::PreRender);
	Super::PreRender(RI);

	if( !BackBufferMaterial )
		return;

	BackBufferMaterial->Material = RI->GetBackBufferCopyMaterial();

	MatTextureSpace.M[0][0] = 0.5f*BackBufferMaterial->MaterialUSize();
	MatTextureSpace.M[1][1] = -0.5f*BackBufferMaterial->MaterialVSize();
	MatTextureSpace.M[3][0] = 0.5f*BackBufferMaterial->MaterialUSize();
	MatTextureSpace.M[3][1] = 0.5f*BackBufferMaterial->MaterialVSize();

    MatTextureSpace.M[3][0] += IndexOfRefraction;
	MatTextureSpace.M[3][1] += IndexOfRefraction;	

	BackBufferMaterial->Matrix = MatTextureSpace;
	BackBufferMaterial->TexCoordSource = TCS_CameraProjCoords;

	TextureStage0 = BackBufferMaterial;	

	unguard;
}

//
// UPSRefractionMaterial::Destroy
//
void UPSRefractionMaterial::Destroy()
{
	guard(UPSRefractionMaterial::Destroy);
	Super::Destroy();

	if( BackBufferMaterial )
	{
		delete BackBufferMaterial;
		BackBufferMaterial=NULL;
	}	unguard;
}

/* scion ======================================================================
 * UPSDistortionMaterial::PreRender
 * Author: jg/sz
 * 
 * Sets the mask for the distortion effect. Moved from the original code.
 *
 * ============================================================================
 */
void UPSDistortionMaterial::PreRender(FRenderInterface* RI)
{
	guard(UPSDistortionBaseMaterial::PreRender);

	// Epic's "#ifdef _XBOX" / "#else": the Xbox branch runs when the render device
	// has the post-process capability (FRenderCaps::XboxPostProcess), PC's otherwise.
	if( RenDevHasXboxPostProcess() )
	{
	Super::PreRender(RI);

	switch( RI->GetPostProcessStage() )
	{
	case FRenderInterface::PP_STAGE_ACCUMULATE:
		{
            TextureStage1 = Mask;
		}
		break;
	case FRenderInterface::PP_STAGE_NONE:
	case FRenderInterface::PP_STAGE_RENDER_DISTORTION:
		{
			TextureStage3 = Mask;
		}	
		break;
	case FRenderInterface::PP_STAGE_RENDER_COLOR:	
	default:
		{
			TextureStage0 = NULL;
		}
		break;
	}
	}
	else
	{

	TextureStage1 = Mask;

	}

	unguard;
}

/* scion ======================================================================
 * UPSBlendedDistortionMaterial::PreRender
 * Author: jg/sz
 * 
 * Sets the diffuse texture into the 4th stage so it can be blended with the
 * distortion.
 *
 * ============================================================================
 */
void UPSBlendedDistortionMaterial::PreRender(FRenderInterface* RI)
{
	guard(UPSBlendedDistortionMaterial::PreRender);
	Super::PreRender(RI);

	// Epic's "#ifdef _XBOX" / "#else": the Xbox branch runs when the render device
	// has the post-process capability (FRenderCaps::XboxPostProcess), PC's otherwise.
	if( RenDevHasXboxPostProcess() )
	{
	switch( RI->GetPostProcessStage() )
	{
	case FRenderInterface::PP_STAGE_ACCUMULATE:
		{
			TextureStage1 = Diffuse;
		}
		break;
	case FRenderInterface::PP_STAGE_NONE:		
	case FRenderInterface::PP_STAGE_RENDER_DISTORTION:
		{
			TextureStage3 = Diffuse;
		}	
		break;	
	case FRenderInterface::PP_STAGE_RENDER_COLOR:	
	default:
		{
			TextureStage0 = Diffuse;
		}
		break;
	}
	}
	else
	{

	TextureStage1 = Diffuse;

	}

	unguard;
}

/* scion ======================================================================
 * UPSDistortionBaseMaterial::PreRender
 * Author: sz
 * 
 * Sets texture stages for the distortion material based on the current
 * post process pass.
 *
 * ============================================================================
 */
void UPSDistortionBaseMaterial::PreRender(FRenderInterface* RI)
{
	guard(UPSDistortionBaseMaterial::PreRender);
	Super::PreRender(RI);

	// Epic's "#ifdef _XBOX" / "#else": the Xbox branch runs when the render device
	// has the post-process capability (FRenderCaps::XboxPostProcess), PC's otherwise.
	if( RenDevHasXboxPostProcess() )
	{
	switch( RI->GetPostProcessStage() )
	{
	case FRenderInterface::PP_STAGE_ACCUMULATE:
		{
			// accumulation pass just needs the distortion material
			TextureStage0 = DistortionMaterial;
			//TextureStage1 = set by subclass
			TextureStage2 = NULL; // reserved for texcoord usage			
			TextureStage3 = NULL;
		}
		break;
	case FRenderInterface::PP_STAGE_NONE:
	case FRenderInterface::PP_STAGE_RENDER_DISTORTION:
		{			
#if OLD_DISTORT_METHOD 
			// copy of back buffer 
			UMaterial* BBMat = RI->GetBackBufferCopyMaterial();	
#else
			// use back buffer directly
			UMaterial* BBMat = RI->GetBackBufferMaterial();
#endif
			// distortion pass needs a distortion material
			// as well as the back buffer 
			TextureStage0 = DistortionMaterial;
			TextureStage1 = BBMat;
			TextureStage2 = BBMat;
			//TextureStage3 - set by subclass
		}
		break;			
	case FRenderInterface::PP_STAGE_RENDER_COLOR:	
	default:
		//TextureStage0 - set by subclass
		TextureStage1 = NULL;
		TextureStage2 = NULL;
		TextureStage3 = NULL;
		break;
		
	}
	}
	else
	{

	TextureStage0 = DistortionMaterial;
	//TextureStage1 - set by subclass

	}

	unguard;
}

//
// PSDistortionBaseMaterial's cpptext declares these two outside _XBOX; they
// give PC's answers ("so that something would be drawn on the PC": no post-process list,
// always sorted as translucent) unless the render device has the post-process capability,
// and then the Xbox ones (the inline body in the _XBOX branch of the class).
//
#ifndef _XBOX
EPostProcessMaterialType UPSDistortionBaseMaterial::IsPostProcess()
{
	if( !RenDevHasXboxPostProcess() )
		return POSTPROCESS_NONE;
	return RequiresSorting() ? POSTPROCESS_BLEND : POSTPROCESS_NOBLEND; 
}

UBOOL UPSDistortionBaseMaterial::RequiresSorting()
{
	if( !RenDevHasXboxPostProcess() )
		return 1;
	return Super::RequiresSorting();
}
#endif

//
// UPSDistortionMaterial::Destroy
//
void UPSDistortionBaseMaterial::Destroy()
{
	guard(UPSDistortionBaseMaterial::Destroy);	
	Super::Destroy();
	unguard;
}

//
// UPSBloomSelectMaterial::PreRender
//
void UPSBloomSelectMaterial::PreRender(FRenderInterface* RI)
{
	guard(UPSBloomSelectMaterial::PreRender);
	Super::PreRender(RI);

	TextureStage0 = SourceMaterial;

	unguard;
}

/* scion ======================================================================
 * UPSSkinShader::PreRender
 * Author: jg
 * 
 * Sets the texture stages for the pixel shader
 *
 * ============================================================================
 */
void UPSSkinShader::PreRender(FRenderInterface* RI)
{
	guard(UPSSkinShader::PreRender);
	TextureStage0 = Diffuse;
	TextureStage1 = Specular;
	TextureStage2 = RimLight;
	unguard;
}

/* scion ======================================================================
 * UPSTeamSkinShader::PreRender
 * Author: jg
 * 
 * Sets the texture stages for the pixel shader
 *
 * ============================================================================
 */
void UPSTeamSkinShader::PreRender(FRenderInterface* RI)
{
	guard(UPSTeamSkinShader::PreRender);
	TextureStage0 = Diffuse;
	TextureStage1 = Specular;
	TextureStage2 = RimLight;
	TextureStage3 = SpecularMask;
	unguard;
}

/* scion ======================================================================
 * UPSTeamSkinShader::PostLoad
 * Author: jg
 * 
 * Auto converts the team mask into a spec mask
 *
 * ============================================================================
 */
void UPSTeamSkinShader::PostLoad(void)
{
	guard(UPSTeamSkinShader::PostLoad);
	Super::PostLoad();
	// The team mask property is deprecated so move any material there into
	// the specular mask
	if (TeamMask != NULL)
	{
		SpecularMask = TeamMask;
		TeamMask = NULL;
	}
	unguard;
}

/* scion ======================================================================
* UPSSkinShaderOpacity::PreRender
* Author: sz
* 
* Sets the texture stages and chooses the correct pixel shader
*
* ============================================================================
*/
void UPSSkinShaderOpacity::PreRender(FRenderInterface* RI)
{
	guard(UPSTeamSkinShader::PreRender);
	TextureStage0 = Diffuse;
	TextureStage1 = Specular;
	TextureStage2 = Opacity;
	TextureStage3 = RimLight;
	unguard;
}

/* scion ======================================================================
* UPSTeamSkinShaderOpacity::PreRender
* Author: sz
* 
* Sets the texture stages and chooses the correct pixel shader
*
* ============================================================================
*/
void UPSTeamSkinShaderOpacity::PreRender(FRenderInterface* RI)
{
	guard(UPSTeamSkinShader::PreRender);
	TextureStage0 = Diffuse;
	TextureStage1 = Specular;
	TextureStage2 = Opacity;
	TextureStage3 = SpecularMask;
	unguard;
}

/* scion ======================================================================
 * UPSTeamSkinShader::PostLoad
 * Author: jg
 * 
 * Auto converts the team mask into a spec mask
 *
 * ============================================================================
 */
void UPSTeamSkinShaderOpacity::PostLoad(void)
{
	guard(UPSTeamSkinShaderOpacity::PostLoad);
	Super::PostLoad();
	// The team mask property is deprecated so move any material there into
	// the specular mask
	if (TeamMask != NULL)
	{
		SpecularMask = TeamMask;
		TeamMask = NULL;
	}
	unguard;
}

/* scion ======================================================================
 * UPSDualDiffuse::PreRender
 * Author: jg
 * 
 * Sets the texture stages and chooses the correct pixel shader
 *
 * ============================================================================
 */
void UPSDualDiffuse::PreRender(FRenderInterface* RI)
{
	guard(UPSDualDiffuse::PreRender);
	// Don't do this unless it changes
	if (bRebuild == 1)
	{
		// Set the texture stages
		TextureStage0 = Diffuse;
		TextureStage1 = Diffuse2;
		TextureStage2 = Specular;
		TextureStage3 = SpecularMask;
		// Determine if we are using a spec mask or not and pick a different
		// pixel shader based upon whether we are or not
		if (SpecularMask != NULL)
		{
			// Now pick the correct shader based upon the settings
			switch (MaskMode)
			{
				case BMO_None:
				{
					// Select between add or mod
					PixelShaderType = (BlendMode == BBO_Add) ?
						PS_DualDiffuse_NM_Add_NSL_SM : PS_DualDiffuse_NM_Mod_NSL_SM;
					break;
				}
				case BMO_Mask1:
				{
					// Select between add or mod or blend
					PixelShaderType = (BlendMode == BBO_Add) ?
						PS_DualDiffuse_M1_Add_NSL_SM : ((BlendMode == BBO_Modulate) ?
						PS_DualDiffuse_M1_Mod_NSL_SM : PS_DualDiffuse_M1_Blend_NSL_SM);
					break;
				}
				case BMO_Mask2:
				{
					// Select between add or mod or blend
					PixelShaderType = (BlendMode == BBO_Add) ?
						PS_DualDiffuse_M2_Add_NSL_SM : ((BlendMode == BBO_Modulate) ?
						PS_DualDiffuse_M2_Mod_NSL_SM : PS_DualDiffuse_M2_Blend_NSL_SM);
					break;
				}
			}
		}
		else
		{
			// Now pick the correct shader based upon the settings
			switch (MaskMode)
			{
				case BMO_None:
				{
					// Select between add or mod
					PixelShaderType = (BlendMode == BBO_Add) ?
						PS_DualDiffuse_NM_Add_NSL : PS_DualDiffuse_NM_Mod_NSL;
					break;
				}
				case BMO_Mask1:
				{
					// Select between add or mod or blend
					PixelShaderType = (BlendMode == BBO_Add) ?
						PS_DualDiffuse_M1_Add_NSL : ((BlendMode == BBO_Modulate) ?
						PS_DualDiffuse_M1_Mod_NSL : PS_DualDiffuse_M1_Blend_NSL);
					break;
				}
				case BMO_Mask2:
				{
					// Select between add or mod or blend
					PixelShaderType = (BlendMode == BBO_Add) ?
						PS_DualDiffuse_M2_Add_NSL : ((BlendMode == BBO_Modulate) ?
						PS_DualDiffuse_M2_Mod_NSL : PS_DualDiffuse_M2_Blend_NSL);
					break;
				}
			}
		}
		// Check for specular having lighting
		if (bUseLightingForSpecular == 1)
		{
			// Specular lighting is always one greater than non-lighting
			PixelShaderType++;
		}
	}
	bRebuild = 0;
	unguard;
}

/* scion ======================================================================
 * UPSDiffuseOpacitySpecularMask::PreRender
 * Author: jg
 * 
 * Sets the texture stages for the pixel shader
 *
 * ============================================================================
 */
void UPSDiffuseOpacitySpecularMask::PreRender(FRenderInterface* RI)
{
	guard(UPSDiffuseOpacitySpecularMask::PreRender);
	TextureStage0 = Diffuse;
	TextureStage1 = Specular;
	TextureStage2 = SpecMask;
	// Choose between the shader that uses the diffuse alpha for both opacity
	// and specular or the shader that uses the diffuse alpha for opacity and
	// a separate mask for specular
	PixelShaderType = (SpecMask == NULL) ?
		PS_DiffuseOpacitySpecularMask : PS_DiffuseOpacitySpecularMask2;
	// Account for spec lighting
	PixelShaderType += bUseLightingForSpecular;
	unguard;
}

/* scion ======================================================================
 * UPSShenuSkin::PreRender
 * Author: jg
 * 
 * Sets the texture stages for the pixel shader
 *
 * ============================================================================
 */
void UPSShenuSkin::PreRender(FRenderInterface* RI)
{
	guard(UPSShenuSkin::PreRender);
	TextureStage0 = Diffuse;
	TextureStage1 = Specular;
	TextureStage2 = SelfIlluminationMask;
	unguard;
}

/* scion ======================================================================
 * UPSEmissiveSkinShader::PreRender
 * Author: jg
 * 
 * Sets the texture stages for the pixel shader
 *
 * ============================================================================
 */
void UPSEmissiveSkinShader::PreRender(FRenderInterface* RI)
{
	guard(UPSEmissiveSkinShader::PreRender);
	TextureStage0 = Diffuse;
	TextureStage1 = Specular;
	TextureStage2 = RimLight;
	TextureStage3 = EmissiveMask;
	// Is it modulating specular by the lighting?
	if (bUseLightingForSpecular == 1)
	{
		PixelShaderType = PS_EmissiveSkinShaderSpecLight;
	}
	unguard;
}

/* scion ======================================================================
 * UPSEmissiveShader::PreRender
 * Author: jg
 * 
 * Sets the texture stages for the pixel shader and chooses the correct shader
 *
 * ============================================================================
 */
void UPSEmissiveShader::PreRender(FRenderInterface* RI)
{
	guard(UPSEmissiveShader::PreRender);
	TextureStage0 = Diffuse;
	TextureStage1 = Specular;
	TextureStage2 = SpecularMask;
	TextureStage3 = EmissiveMask;
	// Is this material diffuse only?
	if (Specular == NULL)
	{
		PixelShaderType = PS_EmissiveShaderDiffuseOnly;
	}
	else
	{
		// Choose the specular shader or the spec light shader
		PixelShaderType = PS_EmissiveShaderSpec + bUseLightingForSpecular;
	}
	unguard;
}

/* scion ======================================================================
 * UPSEmissiveSpecular::PreRender
 * Author: jg
 * 
 * Sets the texture stages for the pixel shader and chooses the correct shader
 *
 * ============================================================================
 */
void UPSEmissiveSpecular::PreRender(FRenderInterface* RI)
{
	guard(UPSEmissiveSpecular::PreRender);
	TextureStage0 = Diffuse;
	TextureStage1 = Specular;
	TextureStage2 = EmissiveMask;
	unguard;
}

/* scion ======================================================================
 * UPSTeamShader::PreRender
 * Author: jg
 * 
 * Sets the texture stages for the pixel shader and chooses the correct shader
 *
 * ============================================================================
 */
void UPSTeamShader::PreRender(FRenderInterface* RI)
{
	guard(UPSTeamShader::PreRender);
	// All of these use the diffuse
	TextureStage0 = Diffuse;
	// If not BSP, use the normal route
	if (bIsUsedForBSP == 0)
	{
		if (Specular != NULL)
		{
			TextureStage1 = Specular;
			TextureStage2 = SpecularMask;
			TextureStage3 = TeamMask;
			// Figure out which shader to use
			PixelShaderType = PS_TeamShader + bUseLightingForSpecular;
		}
		else
		{
			// Diffuse only shader so set mask in t1
			TextureStage1 = TeamMask;
			PixelShaderType = PS_TeamShaderNoSpec;
		}
	}
	// Otherwise clear any specular settings and put the light map in t1
	else
	{
		// Place the team mask in t2
		TextureStage1 = TeamMask;
		Specular = NULL;
		SpecularMask = NULL;
		// t2 will be replaced with the light map later
		TextureStage2 = TextureStage3 = NULL;
		// BSP shader is team masking with additive light map blending
		PixelShaderType = PS_TeamBSPShader;
	}
	unguard;
}

/* scion ======================================================================
 * ::ApplyViewportXForm
 * Author: sz
 * 
 * Only use the current viewport's portion of the back buffer
 * for the back buffer materials
 *
 * ============================================================================
 */
FORCEINLINE void ApplyViewportXForm( FMatrix& M, UViewport* Viewport )
{	
	if( !Viewport )
		return;

	M.M[0][0] = Viewport->ScaleX;
	M.M[1][1] = Viewport->ScaleY;
	M.M[3][0] = (FLOAT)Viewport->OffsetX / ((FLOAT)Viewport->SizeX / Viewport->ScaleX);
	M.M[3][1] = (FLOAT)Viewport->OffsetY / ((FLOAT)Viewport->SizeY / Viewport->ScaleY);
}

/* scion ======================================================================
 * UPSBackBufferEffect::PreRender
 * Author: jg
 * 
 * Sets the texture stages for the pixel shader and chooses the correct shader
 *
 * ============================================================================
 */
void UPSBackBufferEffect::PreRender(FRenderInterface* RI)
{
	guard(UPSBackBufferEffect::PreRender);
	// Epic's "#ifdef _XBOX" / "#else": the Xbox branch runs when the render device
	// has the post-process capability (FRenderCaps::XboxPostProcess), PC's otherwise.
	if( RenDevHasXboxPostProcess() )
	{
	// This allows the back buffer to use standard UV coords
	DECLARE_STATIC_UOBJECT(UTexMatrix,BackBufferTexMatrix, 
	{
		BackBufferTexMatrix->Matrix.SetIdentity();
	});
	// use only viewport portion of back buffer tex
	ApplyViewportXForm( BackBufferTexMatrix->Matrix, RI->GetCurViewportPtr() );
	// Get the back buffer copy
	BackBufferTexMatrix->Material = RI->GetBackBufferCopyMaterial();
	// Now use screen projected coords
	DECLARE_STATIC_UOBJECT(UTexEnvMap,BackBufferEnvMap,
	{
		BackBufferEnvMap->EnvMapType = EM_ProjectSpace;
	});
	BackBufferEnvMap->Material = BackBufferTexMatrix;
	// Set the stages
	TextureStage0 = BackBufferEnvMap;
	}
	else
	{
	// Set the stages
	TextureStage0 = DefaultMaterial;
	}
	TextureStage1 = Mask;
	// Figure out which shader to use
	PixelShaderType = Mask ? PS_MaskedLuminance : PS_LuminanceTexIndex;
	unguard;
}

/* scion ======================================================================
 * UPSPlayerCamMaterial::PreRender
 * Author: jg
 * 
 * Sets up the back buffer texture
 *
 * ============================================================================
 */
void UPSPlayerCamMaterial::PreRender(FRenderInterface* RI)
{
	guard(UPSPlayerCamMaterial::PreRender);
	// Epic's "#ifdef _XBOX" / "#else": the Xbox branch runs when the render device
	// has the post-process capability (FRenderCaps::XboxPostProcess), PC's otherwise.
	if( RenDevHasXboxPostProcess() )
	{
	// This allows the back buffer to use standard UV coords
	DECLARE_STATIC_UOBJECT(UTexMatrix,PlayerCamMatrix, 
	{
		PlayerCamMatrix->Matrix.SetIdentity();
	});	
	// use only viewport portion of back buffer tex
	ApplyViewportXForm( PlayerCamMatrix->Matrix, RI->GetCurViewportPtr() );
	// Get the back buffer copy
	PlayerCamMatrix->Material = RI->GetBackBufferCopyMaterial();
	// Set the stages
	TextureStage0 = PlayerCamMatrix;
	TextureStage1 = NoiseMask;
	TextureStage2 = Noise1;
	TextureStage3 = Noise2;
	// Figure out which pixel shader to use
	PixelShaderType = (NoiseMask == NULL) ? PS_PlayerCam :
		(Noise2 == NULL) ? PS_PlayerCamNoise : PS_PlayerCamTwoNoise;
	}
	else
	{
	// Set the stages
	TextureStage0 = DefaultMaterial;
	}
	unguard;
}

/* scion ======================================================================
 * UPSDiffuseOpacity::PreRender
 * Author: jg
 * 
 * Sets the texture stages for the pixel shader and chooses the correct shader
 *
 * ============================================================================
 */
void UPSDiffuseOpacity::PreRender(FRenderInterface* RI)
{
	guard(UPSDiffuseOpacity::PreRender);
	TextureStage0 = Diffuse;
	TextureStage1 = Opacity;
	unguard;
}

/* scion ======================================================================
 * UPSColorRemapMaterial::PreRender
 * Author: jg
 * 
 * Sets the texture stages for the pixel shader and chooses the correct shader
 *
 * ============================================================================
 */
void UPSColorRemapMaterial::PreRender(FRenderInterface* RI)
{
	guard(UPSColorRemapMaterial::PreRender);
	// Epic's "#ifdef _XBOX" / "#else": the Xbox branch runs when the render device
	// has the post-process capability (FRenderCaps::XboxPostProcess), PC's otherwise.
	if( RenDevHasXboxPostProcess() )
	{
	// For effects that need to control the tex matrix
	if (bSetBackbufferManually == 0)
	{
		DECLARE_STATIC_UOBJECT(UTexMatrix,BackBufferTexMatrix, 
		{
			BackBufferTexMatrix->Matrix.SetIdentity();
		});
		// use only viewport portion of back buffer tex
		ApplyViewportXForm( BackBufferTexMatrix->Matrix, RI->GetCurViewportPtr() );
		// use blurred copy or direct back buffer
		BackBufferTexMatrix->Material = bUseBlurredCopy == 0 ?
			RI->GetBackBufferMaterial() :
			RI->GetBackBufferCopyMaterial();
		// The backbuffer to sample
		TextureStage0 = BackBufferTexMatrix;
	}
	}
	else
	{
	// Just use the default texture
	TextureStage0 = DefaultMaterial;
	}
	if (bGrayScaleOnly == FALSE)
	{
		// These are put in both stages since we are doing a texm3x2
		TextureStage1 = RemapTexture;
		TextureStage2 = RemapTexture;
		if (Mask != NULL)
		{
			// The mask texture to use
			TextureStage3 = Mask;
			// Use the masked version of the shader
			PixelShaderType = PS_ColorRemapMasked;
		}
	}
	else
	{
		// We manually set the textures in the camera effect
		// Hack but we're almost done so who cares
		PixelShaderType = PS_ColorRemapGrayScale;
	}
	unguard;
}
/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/
