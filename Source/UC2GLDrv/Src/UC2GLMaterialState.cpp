/*=============================================================================
	UC2GLMaterialState.cpp: the material compiler of UC2GLDrv.

	UC2's D3DDrv/Src/D3DMaterialState.cpp carried over function for function (types
	renamed FD3D* -> FOpenGL*, D3DCAPS8 -> FOpenGLDeviceCaps, D3DX matrix helpers on
	FMatrix). Stages are still described in D3D8 vocabulary (UC2GLD3DTypes.h) and
	translated to GL in FOpenGLRenderInterface::SetMaterialBlending/CommitTexCoords, so
	this file stays diffable against the specification: `diff` it against UC2's
	D3DMaterialState.cpp to see every deliberate change; each one is marked "UC2GLDrv:".
	Code under #ifdef _XBOX is kept as it is (never compiled; reference for M6/M7/M9).

	D3DMaterialState.cpp: Unreal Direct3D rendering interface implementation.
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Daniel Vogel
=============================================================================*/

#include "UC2GLDrv.h"
#include "UC2GLD3DXCompat.h"

//
// Voodoo 3 hack.
//
extern D3DTEXTUREOP DUMMY_MODULATE2X;

// scion sz
FORCEINLINE UBOOL IsTextureLinear( FOpenGLTexture* Texture )
{
#ifndef _XBOX
	return 0;
#else
	return( Texture && 
			Texture->CachedFormat >= D3DFMT_LIN_A1R5G5B5 && 
			Texture->CachedFormat <= D3DFMT_LIN_F16 );

#endif
}

//
// FOpenGLModifierInfo::FOpenGLModifierInfo
//
FOpenGLModifierInfo::FOpenGLModifierInfo()
{
	ModifyTextureTransforms		= 0;
	ModifyFramebufferBlending	= 0;
	ModifyColor					= 0;
	ModifyOpacity				= 0;
	ModifyVertexOpacity			= 0;
	
	Matrix				= FMatrix::Identity;
	TexCoordSource		= TCS_Stream0;
	TexCoordCount		= TCN_2DCoords;
	TexCoordProjected	= 0;

	FrameBufferBlending	= FB_Overwrite;
	ZWrite				= 1;
	ZTest				= 1;
	AlphaTest			= 0;
	TwoSided			= 0;
	AlphaRef			= 0;

	TFactorColor		= 0;
	AlphaBlend			= 0;

	Opacity				= NULL;
	OpacityOverrideTexModifier = 0;

	// scion sz - these need to be initialized
	ColorWriteEnable	= 1;
	AlphaWriteEnable	= 0;
	OverrideAlpha		= 0;
	AlphaEnable			= 0;
}

//
// FOpenGLModifierInfo::SetDetailTextureScale - apply a texture scaling transform for a detail texture.
//
void FOpenGLModifierInfo::SetDetailTextureScale( FLOAT Scale )
{
	ModifyTextureTransforms = 1;
	Matrix *= FMatrix (
					FPlane(Scale,0,0,0),
					FPlane(0,Scale,0,0),
					FPlane(0,0,1,0),
					FPlane(0,0,0,1)
				);
}

//
// FOpenGLMaterialStateStage::FOpenGLMaterialStateStage
//
FOpenGLMaterialStateStage::FOpenGLMaterialStateStage()
{
	appMemcpy( &TextureTransformMatrix, &FMatrix::Identity, sizeof(D3DMATRIX));

	TextureTransformsEnabled	= 0;
	TexCoordIndex				= D3DTSS_TCI_PASSTHRU;
	TexCoordCount				= D3DTTFF_DISABLE;
	TextureAddressU				= D3DTADDRESS_CLAMP;
	TextureAddressV				= D3DTADDRESS_CLAMP;
	TextureAddressW				= D3DTADDRESS_CLAMP;
    TextureMipLODBias           = -1000.f; //!! will be set to DefaultTexMipBias
	Texture		= NULL;
	
	ResultArg	= D3DTA_CURRENT;
	ColorArg0	= D3DTA_CURRENT;
	ColorArg1	= D3DTA_CURRENT;
	ColorArg2	= D3DTA_CURRENT;
	ColorOp		= D3DTOP_SELECTARG1;
	AlphaArg0	= D3DTA_CURRENT;
	AlphaArg1	= D3DTA_CURRENT;
	AlphaArg2	= D3DTA_CURRENT;
	AlphaOp		= D3DTOP_SELECTARG1;

	MinFilter = D3DTEXF_LINEAR;
	MagFilter = D3DTEXF_LINEAR;
	MipFilter = D3DTEXF_LINEAR;

#ifdef BUMPMAP_ENABLE
	BumpMapEnabled=0;
	ColorSign=0;
#endif
}

//
// FOpenGLMaterialState::FOpenGLMaterialState
//
FOpenGLMaterialState::FOpenGLMaterialState()
{
	PixelShader			= PS_None;
	AlphaBlending		= 0;
	AlphaTest			= 0;
	AlphaRef			= 0;
	ZTest				= 1;
	ZWrite				= 1;
	TwoSided			= 0;
	FillMode			= FM_Solid;
	SrcBlend			= D3DBLEND_ONE;
	DestBlend			= D3DBLEND_ZERO;
	BlendOp				= D3DBLENDOP_ADD;
	TFactorColor		= 0;
	StagesUsed			= 0;
	OverrideFogColor	= 0;

	// scion sz -
	// only enable RGB color writes (not Alpha).  Frame buffer alpha gives us pixel's emissive amount.
	ColorWriteMask		= CWM_COLORWRITEENABLE_COLORONLY;

	// scion sz -
	SpecularEnable		= 0;

	// scion sz/jg - per material vertex shader fragment
	MaterialVertexShaderEnum = 0;

	// UC2GLDrv
	UsePixelShaderProgram = 0;
}

/*----------------------------------------------------------------------------
	SetMaterialShader.
----------------------------------------------------------------------------*/

#define NEWPASS()															\
		CurrentState->MaterialPasses[PassesUsed]->StagesUsed = StagesUsed;					\
		PassesUsed++;														\
		StagesUsed = 0;														\
		CurrentState->MaterialPasses[PassesUsed] = MaterialStatePool.AllocateState(&DefaultPass); \
		CurrentState->MaterialPasses[PassesUsed]->ZWrite = 0;

//
//
//

void FOpenGLRenderInterface::HandleDetail( UBitmapMaterial* DetailBitmap, INT& PassesUsed, INT& StagesUsed, INT& FreeStagesUsed, FOpenGLModifierInfo InModifierInfo, UBOOL SinglePassOnly )
{
	INT MaxTextureStages = RenDev->DeviceCaps.MaxSimultaneousTextures;
	if( StagesUsed >= MaxTextureStages+FreeStagesUsed )
	{
#if 1
		// No room for detail texture in single pass, start a new render pass if possible
		if( (!InModifierInfo.ModifyFramebufferBlending || InModifierInfo.FrameBufferBlending == FB_Overwrite) && !SinglePassOnly )
		{
			NEWPASS();
			FOpenGLMaterialStateStage& Stage = CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed];
			SetShaderBitmap( Stage, DetailBitmap );
			Stage.ColorArg1 = D3DTA_TEXTURE;
			Stage.AlphaArg1 = D3DTA_DIFFUSE;
			Stage.ColorOp   = D3DTOP_SELECTARG1;
			Stage.AlphaOp   = D3DTOP_SELECTARG1;
            Stage.TextureMipLODBias = RenDev->DetailTexMipBias;
			ApplyTexModifier( Stage, &InModifierInfo );
			StagesUsed++;
		
			// modulate with framebuffer.
			CurrentState->MaterialPasses[PassesUsed]->SrcBlend			= D3DBLEND_DESTCOLOR;
			CurrentState->MaterialPasses[PassesUsed]->DestBlend			= D3DBLEND_SRCCOLOR;
			CurrentState->MaterialPasses[PassesUsed]->AlphaBlending		= 1;
			CurrentState->MaterialPasses[PassesUsed]->ZWrite			= 0;
			CurrentState->MaterialPasses[PassesUsed]->ZTest				= 1;
			CurrentState->MaterialPasses[PassesUsed]->OverrideFogColor	= 1;
			CurrentState->MaterialPasses[PassesUsed]->OverriddenFogColor= FColor( 127, 127, 127, 0 );
			CurrentState->MaterialPasses[PassesUsed]->TwoSided			= InModifierInfo.TwoSided;

            //Direct3DDevice8->SetTextureStageState( Stage, D3DTSS_MIPMAPLODBIAS, *(DWORD*)&LodBias );
		}
#endif
	}
	else 
	{
		// Add detail texture as a single pass
		FOpenGLMaterialStateStage& Stage = CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed];
		SetShaderBitmap( Stage, DetailBitmap );
		Stage.ColorArg1 = D3DTA_TEXTURE;
		Stage.ColorArg2 = D3DTA_CURRENT;
		Stage.AlphaArg1 = D3DTA_CURRENT;
		Stage.ColorOp   = DUMMY_MODULATE2X;
		Stage.AlphaOp   = D3DTOP_SELECTARG1;
        Stage.TextureMipLODBias = RenDev->DetailTexMipBias;
		ApplyTexModifier( Stage, &InModifierInfo );
		StagesUsed++;
	}
}

//
// Handle lightmap in single pass
//
void FOpenGLRenderInterface::HandleLightmap_SP( FOpenGLMaterialStateStage& Stage, FOpenGLTexture* Lightmap )
{
	Stage.AlphaArg1	= D3DTA_CURRENT;
	Stage.ColorArg1	= D3DTA_TEXTURE;
	Stage.ColorArg2	= D3DTA_CURRENT;
	Stage.AlphaOp	= D3DTOP_SELECTARG1;
	Stage.ColorOp	= (CurrentState->LightingModulate2X && !CurrentState->UseStaticLighting) ? DUMMY_MODULATE2X : D3DTOP_MODULATE;
	Stage.Texture	= Lightmap;
	Stage.TextureAddressU			= D3DTADDRESS_CLAMP; 
	Stage.TextureAddressV			= D3DTADDRESS_CLAMP; 
	Stage.TexCoordIndex				= TCS_Stream1;
	Stage.TextureTransformsEnabled	= 0;
}

//
// Patch in the diffuse lighting into the first texture stage
//
void FOpenGLRenderInterface::HandleDiffuse_Patch( FOpenGLMaterialStateStage& Stage, UBOOL Modulate2X )
{
	Stage.ColorArg2	= D3DTA_DIFFUSE;
	Stage.AlphaArg2	= D3DTA_DIFFUSE;
	Stage.ColorOp	= CurrentState->LightingModulate2X ? DUMMY_MODULATE2X : D3DTOP_MODULATE;
	Stage.AlphaOp	= D3DTOP_MODULATE;
}

void FOpenGLRenderInterface::HandleDiffuse_SP( FOpenGLMaterialStateStage& Stage )
{
	Stage.ColorArg1	= D3DTA_DIFFUSE;
	Stage.ColorArg2 = CurrentState->LightingModulate2X ? D3DTA_DIFFUSE : Stage.ColorArg2;
	Stage.ColorOp	= CurrentState->LightingModulate2X ? D3DTOP_ADD : D3DTOP_SELECTARG1;
	Stage.AlphaArg1	= D3DTA_DIFFUSE;
	Stage.AlphaOp	= D3DTOP_SELECTARG1;
}

void FOpenGLRenderInterface::HandleDiffuse_Stage( FOpenGLMaterialStateStage& Stage, UBOOL Modulate2X )
{
	Stage.ColorArg1	= D3DTA_DIFFUSE;
	Stage.ColorArg2	= D3DTA_CURRENT;
	Stage.ColorOp	= CurrentState->LightingModulate2X ? DUMMY_MODULATE2X : D3DTOP_MODULATE;
	Stage.AlphaArg2	= D3DTA_CURRENT;
	Stage.AlphaOp	= D3DTOP_SELECTARG2;
}

void FOpenGLRenderInterface::HandleDiffuseTFactor_Stage( FOpenGLMaterialStateStage& Stage, UBOOL Modulate2X )
{
	Stage.ColorArg1	= D3DTA_DIFFUSE;
	Stage.ColorArg2	= D3DTA_CURRENT;
	Stage.ColorOp	= CurrentState->LightingModulate2X ? DUMMY_MODULATE2X : D3DTOP_MODULATE;
	Stage.AlphaArg1	= D3DTA_CURRENT;
	Stage.AlphaArg2	= D3DTA_TFACTOR;
	Stage.AlphaOp	= D3DTOP_MODULATE;
}

//
// Blend using TFactorColor
//
void FOpenGLRenderInterface::HandleTFactor_SP( FOpenGLMaterialStateStage& Stage )
{
	Stage.AlphaArg1	= D3DTA_CURRENT;
	Stage.AlphaArg2 = D3DTA_TFACTOR;
	Stage.ColorArg1	= D3DTA_CURRENT;
	Stage.ColorArg2	= D3DTA_TFACTOR;
	Stage.AlphaOp	= D3DTOP_MODULATE;
	Stage.ColorOp	= CurrentState->LightingModulate2X ? DUMMY_MODULATE2X : D3DTOP_MODULATE;
	Stage.Texture	= NULL;
}

//
// Handle lightmap is it own pass
//
void FOpenGLRenderInterface::HandleLighting_MP( FOpenGLMaterialStateStage& Stage, FOpenGLTexture* Lightmap, UBOOL UseDiffuse )
{
	Stage.AlphaArg1 = D3DTA_DIFFUSE;
	Stage.ColorArg1 = D3DTA_TEXTURE;
	Stage.ColorArg2 = D3DTA_DIFFUSE;
	Stage.AlphaOp	= D3DTOP_SELECTARG1;

	if( !Lightmap )
		Stage.ColorOp = D3DTOP_SELECTARG2;
	else
	if( !UseDiffuse )
		Stage.ColorOp = D3DTOP_SELECTARG1;
	else
		Stage.ColorOp = CurrentState->LightingModulate2X ? DUMMY_MODULATE2X : D3DTOP_MODULATE;

	Stage.Texture	= Lightmap;
	Stage.TextureAddressU = D3DTADDRESS_WRAP; 
	Stage.TextureAddressV = D3DTADDRESS_WRAP; 
	
	Stage.TexCoordIndex = TCS_Stream1;
	Stage.TextureTransformsEnabled = 0;
}

void FOpenGLRenderInterface::SetShaderBitmap( FOpenGLMaterialStateStage& Stage, UBitmapMaterial* BitmapMaterial )
{
	if( BitmapMaterial )
	{
		Stage.Texture = CacheTexture( BitmapMaterial->Get(Viewport->CurrentTime,Viewport)->GetRenderInterface() );
		if( Stage.Texture->IsCubemap )
		{
			// setup cubemap clamping
			Stage.TextureAddressU = RenDev->CubemapTextureAddressing;
			Stage.TextureAddressV = RenDev->CubemapTextureAddressing;
		}
		else
		{
			switch( BitmapMaterial->UClampMode )
			{
			case TC_Wrap:	Stage.TextureAddressU = D3DTADDRESS_WRAP;  break;
			case TC_Clamp:	Stage.TextureAddressU = D3DTADDRESS_CLAMP; break;
			}
			switch( BitmapMaterial->VClampMode )
			{
			case TC_Wrap:	Stage.TextureAddressV = D3DTADDRESS_WRAP;  break;
			case TC_Clamp:	Stage.TextureAddressV = D3DTADDRESS_CLAMP; break;
			}

			// scion sz -
			switch( BitmapMaterial->FilterMode )
			{				
			case TF_NEAREST:	
				Stage.MagFilter = D3DTEXF_POINT;
				Stage.MinFilter = D3DTEXF_POINT;
				Stage.MipFilter = D3DTEXF_NONE;
				break;
			case TF_TRILINEAR:
				Stage.MipFilter = D3DTEXF_LINEAR;
				break;
			case TF_BILINEAR:
			default:
				break;
			}
		}
		Stage.TextureAddressW = RenDev->CubemapTextureAddressing;
	}
	else
		Stage.Texture = NULL;
}

//
// ApplyFinalBlend
//
void FOpenGLRenderInterface::ApplyFinalBlend( FOpenGLModifierInfo* InModifierInfo, INT PassesUsed )
{
	guard(FOpenGLRenderInterface::ApplyFinalBlend);

	FColor	FogColor = FColor(0,0,0,1);

	switch( InModifierInfo->FrameBufferBlending )
	{
	case FB_Overwrite :
		CurrentState->MaterialPasses[PassesUsed]->SrcBlend			= D3DBLEND_ONE;
		CurrentState->MaterialPasses[PassesUsed]->DestBlend			= D3DBLEND_ZERO;
		CurrentState->MaterialPasses[PassesUsed]->AlphaBlending		= 0;
		break;
	case FB_AlphaBlend:
		CurrentState->MaterialPasses[PassesUsed]->SrcBlend			= D3DBLEND_SRCALPHA;
		CurrentState->MaterialPasses[PassesUsed]->DestBlend			= D3DBLEND_INVSRCALPHA;
		CurrentState->MaterialPasses[PassesUsed]->AlphaBlending		= 1;
		break;
	case FB_AlphaModulate_MightNotFogCorrectly:
		CurrentState->MaterialPasses[PassesUsed]->SrcBlend			= D3DBLEND_ONE;
		CurrentState->MaterialPasses[PassesUsed]->DestBlend			= D3DBLEND_INVSRCALPHA;
		CurrentState->MaterialPasses[PassesUsed]->AlphaBlending		= 1;
		CurrentState->MaterialPasses[PassesUsed]->OverrideFogColor	= 1;
		CurrentState->MaterialPasses[PassesUsed]->OverriddenFogColor= FColor( 0, 0, 0, 0 );
		break;
	case FB_Modulate:
		CurrentState->MaterialPasses[PassesUsed]->SrcBlend			= D3DBLEND_DESTCOLOR;
		CurrentState->MaterialPasses[PassesUsed]->DestBlend			= D3DBLEND_SRCCOLOR;
		CurrentState->MaterialPasses[PassesUsed]->AlphaBlending		= 1;
		CurrentState->MaterialPasses[PassesUsed]->OverrideFogColor	= 1;
		CurrentState->MaterialPasses[PassesUsed]->OverriddenFogColor= FColor( 127, 127, 127, 0 );
		break;
	case FB_Translucent:
		CurrentState->MaterialPasses[PassesUsed]->SrcBlend			= D3DBLEND_ONE;
		CurrentState->MaterialPasses[PassesUsed]->DestBlend			= D3DBLEND_ONE;
		CurrentState->MaterialPasses[PassesUsed]->AlphaBlending		= 1;
		CurrentState->MaterialPasses[PassesUsed]->OverrideFogColor	= 1;
		CurrentState->MaterialPasses[PassesUsed]->OverriddenFogColor= FColor( 0, 0, 0, 0 );
		break;
	case FB_Darken:
		CurrentState->MaterialPasses[PassesUsed]->SrcBlend			= D3DBLEND_ZERO;
		CurrentState->MaterialPasses[PassesUsed]->DestBlend			= D3DBLEND_INVSRCCOLOR;
		CurrentState->MaterialPasses[PassesUsed]->AlphaBlending		= 1;
		CurrentState->MaterialPasses[PassesUsed]->OverrideFogColor	= 1;
		CurrentState->MaterialPasses[PassesUsed]->OverriddenFogColor= FColor( 0, 0, 0, 0 );
		break;
	case FB_Brighten:
		CurrentState->MaterialPasses[PassesUsed]->SrcBlend			= D3DBLEND_ONE;
		CurrentState->MaterialPasses[PassesUsed]->DestBlend			= D3DBLEND_INVSRCCOLOR;
		CurrentState->MaterialPasses[PassesUsed]->AlphaBlending		= 1;
		CurrentState->MaterialPasses[PassesUsed]->OverrideFogColor	= 1;
		CurrentState->MaterialPasses[PassesUsed]->OverriddenFogColor= FColor( 0, 0, 0, 0 );
		break;
	case FB_Invisible:
		CurrentState->MaterialPasses[PassesUsed]->SrcBlend			= D3DBLEND_ZERO;
		CurrentState->MaterialPasses[PassesUsed]->DestBlend			= D3DBLEND_ONE;
		CurrentState->MaterialPasses[PassesUsed]->AlphaBlending		= 1;
		CurrentState->MaterialPasses[PassesUsed]->OverrideFogColor	= 1;
		CurrentState->MaterialPasses[PassesUsed]->OverriddenFogColor= FColor( 0, 0, 0, 0 );
		break;
	case FB_ShadowBlend:
		CurrentState->MaterialPasses[PassesUsed]->SrcBlend			= D3DBLEND_SRCALPHA;
		CurrentState->MaterialPasses[PassesUsed]->DestBlend			= D3DBLEND_ONE;
		CurrentState->MaterialPasses[PassesUsed]->AlphaBlending		= 1;
		CurrentState->MaterialPasses[PassesUsed]->OverrideFogColor	= 1;
		CurrentState->MaterialPasses[PassesUsed]->OverriddenFogColor= FColor( 0, 0, 0, 0 );
	}

	CurrentState->MaterialPasses[PassesUsed]->ZWrite	= InModifierInfo->ZWrite;
	CurrentState->MaterialPasses[PassesUsed]->ZTest		= InModifierInfo->ZTest;
	CurrentState->MaterialPasses[PassesUsed]->TwoSided	= InModifierInfo->TwoSided;
	CurrentState->MaterialPasses[PassesUsed]->AlphaTest	= InModifierInfo->AlphaTest;
	CurrentState->MaterialPasses[PassesUsed]->AlphaRef	= InModifierInfo->AlphaRef;
	//jg -- Check for color/alpha write enable being set
	CurrentState->MaterialPasses[PassesUsed]->ColorWriteMask =
		InModifierInfo->ColorWriteEnable ? CWM_COLORWRITEENABLE_COLORONLY : 0;
	CurrentState->MaterialPasses[PassesUsed]->ColorWriteMask |=
		InModifierInfo->AlphaWriteEnable ? CWM_COLORWRITEENABLE_ALPHA : 0;
	//scion jg -- Check to see if we need to override the alpha settings
	if (InModifierInfo->OverrideAlpha)
	{
		CurrentState->MaterialPasses[PassesUsed]->AlphaBlending =
			InModifierInfo->AlphaEnable;
	}

	unguard;
}

//
// ApplyTexModifier
//
void FOpenGLRenderInterface::ApplyTexModifier( FOpenGLMaterialStateStage& Stage, FOpenGLModifierInfo* ModifierInfo )
{
	guard(FOpenGLRenderInterface::ApplyTexModifier);

	if( ModifierInfo->ModifyTextureTransforms )
	{
		// scion sz 
		// projected textures only work if the tex coord count > 2
		checkSlow( !(ModifierInfo->TexCoordProjected && ModifierInfo->TexCoordCount <= TCN_2DCoords) );

		// Apply texture coordinate transforms
		switch( ModifierInfo->TexCoordCount )
		{
		case TCN_2DCoords:
			Stage.TexCoordCount = D3DTTFF_COUNT2;
			break;
		case TCN_3DCoords:
			Stage.TexCoordCount = D3DTTFF_COUNT3;
			break;
		case TCN_4DCoords:
			Stage.TexCoordCount = D3DTTFF_COUNT4;
			break;
		default:
			appErrorf(TEXT("Unknown TexCoordCount %d"), ModifierInfo->TexCoordCount );
		}		
		if( ModifierInfo->TexCoordProjected )
			Stage.TexCoordCount |= D3DTTFF_PROJECTED;

		switch( ModifierInfo->TexCoordSource )
		{
		case TCS_NoChange:
			Stage.TexCoordIndex = D3DTSS_TCI_PASSTHRU;
			Stage.TextureTransformsEnabled = 0;
			break;
		case TCS_Stream0:
		case TCS_Stream1:
		case TCS_Stream2:
		case TCS_Stream3:
		case TCS_Stream4:
		case TCS_Stream5:
		case TCS_Stream6:
		case TCS_Stream7:
			{
				Stage.TexCoordIndex = ModifierInfo->TexCoordSource;
				if( ModifierInfo->TexCoordProjected || ModifierInfo->Matrix != FMatrix::Identity )
				{
					Stage.TextureTransformMatrix = *(D3DMATRIX*)&ModifierInfo->Matrix;
					Stage.TextureTransformsEnabled = 1;
				}
				else
					Stage.TextureTransformsEnabled = 0;
			}
			break;
		case TCS_WorldCoords:
			{
				D3DXMatrixInverse((D3DXMATRIX*)&Stage.TextureTransformMatrix,NULL,
					(D3DXMATRIX*) &CurrentState->WorldToCamera);
				if( ModifierInfo->Matrix != FMatrix::Identity )
				{
					D3DXMatrixMultiply((D3DXMATRIX*)&Stage.TextureTransformMatrix,
						(D3DXMATRIX*)&Stage.TextureTransformMatrix,(D3DXMATRIX*)&ModifierInfo->Matrix);		
				}
				Stage.TextureTransformsEnabled = 1;
				Stage.TexCoordIndex = D3DTSS_TCI_CAMERASPACEPOSITION;				
			}
			break;
		case TCS_CameraCoords:
			{
				if( ModifierInfo->Matrix != FMatrix::Identity )
				{
					Stage.TextureTransformMatrix = *(D3DMATRIX*)&ModifierInfo->Matrix;		
					Stage.TextureTransformsEnabled = 1;
				}
				else
					Stage.TextureTransformsEnabled = 0;
				Stage.TexCoordIndex = D3DTSS_TCI_CAMERASPACEPOSITION;
			}
			break;
		case TCS_WorldEnvMapCoords:
			{
				D3DXMATRIX WorldToTexture;
				if( ModifierInfo->Matrix != FMatrix::Identity )
				{
					D3DXMatrixMultiply(&WorldToTexture, (D3DXMATRIX*)&CurrentState->WorldToCamera, (D3DXMATRIX*)&ModifierInfo->Matrix);
					D3DXMatrixTranspose(&WorldToTexture, &WorldToTexture);
				}
				else
					D3DXMatrixTranspose(&WorldToTexture, (D3DXMATRIX*)&CurrentState->WorldToCamera);
				Stage.TextureTransformMatrix = WorldToTexture;
				Stage.TextureTransformsEnabled = 1;
				Stage.TexCoordIndex = D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR;				
			}
			break;
		case TCS_CameraEnvMapCoords:
			{
				if( ModifierInfo->Matrix != FMatrix::Identity )
				{
					Stage.TextureTransformMatrix = *(D3DMATRIX*)&ModifierInfo->Matrix;		
					Stage.TextureTransformsEnabled = 1;
				}
				else
					Stage.TextureTransformsEnabled = 0;
				Stage.TexCoordIndex = D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR;				
			}
			break;
		// scion sz -
		case TCS_CameraEnvMapNormals:
			{
				Stage.TextureTransformMatrix = *(D3DMATRIX*)&ModifierInfo->Matrix;		
				Stage.TextureTransformsEnabled = 1;
                Stage.TexCoordIndex = D3DTSS_TCI_CAMERASPACENORMAL;
			}
			break;
		// scion sz -
		case TCS_CameraToActorEnvMapNormals:
			{
				if( CurrentState->ForceDefSphereMap )
				{
					Stage.TextureTransformMatrix = *(D3DMATRIX*)&ModifierInfo->Matrix;		
					Stage.TextureTransformsEnabled = 1;
					Stage.TexCoordIndex = D3DTSS_TCI_CAMERASPACENORMAL;
					break;
				}

				// get the camera scene node from the viewport
				if( Viewport->LodSceneNode && Viewport->LodSceneNode->GetCameraSceneNode() )
				{
					FCameraSceneNode* CamSceneNode = Viewport->LodSceneNode->GetCameraSceneNode();

					// current object's position
					FVector ObjectPos( CurrentState->LocalToWorld.M[3][0], CurrentState->LocalToWorld.M[3][1], CurrentState->LocalToWorld.M[3][2] );
					if( CurrentState->LitSphere != GMath.ZeroSphere )
					{
						// use position adjusted for rootmotion
						ObjectPos = CurrentState->LitSphere;
					}					
					// current camera's position
					FVector CameraPos( CamSceneNode->CameraToWorld.M[3][0], CamSceneNode->CameraToWorld.M[3][1], CamSceneNode->CameraToWorld.M[3][2] );

					// direction vector to object from camera's postion				
					FVector ViewObjDir = ObjectPos - CameraPos;
					ViewObjDir.Normalize();

					// Get the world space coords representing the view rotation from camera's position to the object's position
					FCoords ViewObjDirCoords = GMath.UnitCoords / ViewObjDir.Rotation();
					ViewObjDirCoords.ZAxis = -ViewObjDirCoords.ZAxis;

					// create the new camera matrix
					D3DXMATRIX ViewObjectMatrix;
					ViewObjectMatrix._11 = ViewObjDirCoords.YAxis.X;
					ViewObjectMatrix._21 = ViewObjDirCoords.YAxis.Y;
					ViewObjectMatrix._31 = ViewObjDirCoords.YAxis.Z;
					ViewObjectMatrix._41 = -(ViewObjDirCoords.YAxis | CameraPos);

					ViewObjectMatrix._12 = ViewObjDirCoords.ZAxis.X;
					ViewObjectMatrix._22 = ViewObjDirCoords.ZAxis.Y;
					ViewObjectMatrix._32 = ViewObjDirCoords.ZAxis.Z;
					ViewObjectMatrix._42 = -(ViewObjDirCoords.ZAxis | CameraPos);

					ViewObjectMatrix._13 = ViewObjDirCoords.XAxis.X;
					ViewObjectMatrix._23 = ViewObjDirCoords.XAxis.Y;
					ViewObjectMatrix._33 = ViewObjDirCoords.XAxis.Z;
					ViewObjectMatrix._43 = -(ViewObjDirCoords.XAxis | CameraPos);

					ViewObjectMatrix._14 = 0.0f;
					ViewObjectMatrix._24 = 0.0f;
					ViewObjectMatrix._34 = 0.0f;
					ViewObjectMatrix._44 = 1.0f;

					// apply new camera matrix after the inverse of the original view transform
					D3DXMatrixMultiply( (D3DXMATRIX*)&Stage.TextureTransformMatrix, (D3DXMATRIX*)&CamSceneNode->CameraToWorld, (D3DXMATRIX*)&ViewObjectMatrix );
				}
				else
				{
					// reset if we don't have a scene node
					D3DXMatrixIdentity( (D3DXMATRIX*)&Stage.TextureTransformMatrix );
				}

				if( ModifierInfo->Matrix != FMatrix::Identity )
				{
					// apply the ModifierInfo->Matrix transform
					D3DXMatrixMultiply( (D3DXMATRIX*)&Stage.TextureTransformMatrix, (D3DXMATRIX*)&Stage.TextureTransformMatrix, (D3DXMATRIX*)&ModifierInfo->Matrix );
				}

				Stage.TextureTransformsEnabled = 1;
				Stage.TexCoordIndex = D3DTSS_TCI_CAMERASPACENORMAL;
			}
			break;
		// scion sz -
		case TCS_CameraProjCoords:
			{
				// -- transform screen space texture coordinates to projected space 
				// NOTE: these coordinates probably need you to do a viewport transform as well for them to be useful

				D3DXMatrixMultiply( (D3DXMATRIX*)&Stage.TextureTransformMatrix,  (D3DXMATRIX*)&CurrentState->CameraToScreen, (D3DXMATRIX*)&ModifierInfo->Matrix );
				Stage.TextureTransformsEnabled = 1;
				Stage.TexCoordCount = D3DTTFF_COUNT4|D3DTTFF_PROJECTED;
				Stage.TexCoordIndex = D3DTSS_TCI_CAMERASPACEPOSITION;
			}
			break;
		default:
			appErrorf( TEXT("Unknown TexCoordSource %d"), ModifierInfo->TexCoordSource );
		}

		// scion sz --
		// xbox requires linear format texture UV values to go from [0,TexWidth] and [0,TexHeight] not [0,1]
		if( IsTextureLinear(Stage.Texture) )
		{
			D3DXMATRIX LinearConvert;
			D3DXMatrixIdentity( &LinearConvert );
			LinearConvert._11 = Stage.Texture->CachedWidth;
			LinearConvert._22 = Stage.Texture->CachedHeight;

			D3DXMatrixMultiply((D3DXMATRIX*)&Stage.TextureTransformMatrix,
				(D3DXMATRIX*)&Stage.TextureTransformMatrix,
				(D3DXMATRIX*)&LinearConvert);

			Stage.TextureTransformsEnabled = 1;
		}
		
	}

	unguard;
}

//
// Handle opacity bitmap
//
void FOpenGLRenderInterface::HandleOpacityBitmap( FOpenGLMaterialStateStage& Stage, UBitmapMaterial* Bitmap, UBOOL ModulateAlpha, FOpenGLModifierInfo* ModifierInfo, INT PassesUsed )
{
	SetShaderBitmap( Stage, Bitmap );

	if( ModifierInfo && ModifierInfo->ModifyColor )
	{
		// scion sz -- modulate opacity with a constant color
		FColor ShiftedAlphaColor = ModifierInfo->TFactorColor;

		// takes [0,255] alpha range and uses it as [-127,+127] range
		// assumes that the alpha ref test is == 127
		if( ShiftedAlphaColor.A > 127 )
		{
			// 128,255 added
			ShiftedAlphaColor.A = ShiftedAlphaColor.A - 127;
			Stage.AlphaOp = D3DTOP_ADD;
		}
		else
		{
			// 0,127 subtracted
			ShiftedAlphaColor.A = 127 - ShiftedAlphaColor.A;
			Stage.AlphaOp = D3DTOP_SUBTRACT;
		}
		
		CurrentState->MaterialPasses[PassesUsed]->TFactorColor		 = ShiftedAlphaColor;
		CurrentState->MaterialPasses[PassesUsed]->AlphaBlending		|= ModifierInfo->AlphaBlend;
		CurrentState->MaterialPasses[PassesUsed]->TwoSided			|= ModifierInfo->TwoSided;

		Stage.AlphaArg1 = D3DTA_TEXTURE;
		Stage.AlphaArg2 = D3DTA_TFACTOR;		
		Stage.ColorArg2 = D3DTA_CURRENT;
		Stage.ColorOp   = D3DTOP_SELECTARG2;
	}
	else
	{
		Stage.AlphaArg1 = D3DTA_TEXTURE;
		Stage.AlphaArg2 = D3DTA_CURRENT;
		Stage.AlphaOp   = ModulateAlpha ? D3DTOP_MODULATE : D3DTOP_SELECTARG1;
		Stage.ColorArg2 = D3DTA_CURRENT;
		Stage.ColorOp   = D3DTOP_SELECTARG2;
	}	
}

//
// Handle vertex opacity
//
void FOpenGLRenderInterface::HandleVertexOpacity( FOpenGLMaterialStateStage& Stage, UVertexColor* VertexColor )
{
	Stage.AlphaArg1 = D3DTA_DIFFUSE;
	Stage.AlphaOp = D3DTOP_SELECTARG1;
	Stage.ColorArg2 = D3DTA_CURRENT;
	Stage.ColorOp = D3DTOP_SELECTARG2;
}

//
// Handle self-illumination in a single pass
//
void FOpenGLRenderInterface::HandleSelfIllumination_SP( FOpenGLMaterialStateStage& Stage, UBitmapMaterial* Bitmap )
{
	SetShaderBitmap( Stage, Bitmap );
	Stage.AlphaArg1 = D3DTA_CURRENT;
	Stage.AlphaOp   = D3DTOP_SELECTARG1;
	Stage.ColorArg1	= D3DTA_TEXTURE;
	Stage.ColorArg2	= D3DTA_CURRENT;
	Stage.ColorOp   = D3DTOP_BLENDCURRENTALPHA;
}

//
// Handle specular in a single pass
//
void FOpenGLRenderInterface::HandleSpecular_SP( FOpenGLMaterialStateStage& Stage, UBitmapMaterial* Bitmap, UBOOL UseSpecularity, UBOOL UseConstantSpecularity, UBOOL UseDiffuseModulate )
{
	SetShaderBitmap( Stage, Bitmap );

	Stage.AlphaArg1 = D3DTA_CURRENT;
	Stage.AlphaOp   = D3DTOP_SELECTARG1;

	if( UseDiffuseModulate )
	{
		Stage.ColorArg1 = D3DTA_DIFFUSE;
		Stage.ColorArg2 = D3DTA_TEXTURE;
		Stage.ColorArg0 = D3DTA_CURRENT;
		Stage.ColorOp   = D3DTOP_MULTIPLYADD;
	}
	else if( !UseSpecularity )
	{
		Stage.ColorArg1 = D3DTA_CURRENT;
		Stage.ColorArg2 = D3DTA_TEXTURE;
		Stage.ColorOp   = D3DTOP_ADD;
	}
	else
	if( UseConstantSpecularity )
	{
		Stage.ColorArg1 = D3DTA_TFACTOR;
		Stage.ColorArg2 = D3DTA_TEXTURE;
		Stage.ColorArg0 = D3DTA_CURRENT;
		Stage.ColorOp   = D3DTOP_MULTIPLYADD;
	}
	else
	{
		Stage.ColorArg1 = D3DTA_CURRENT;
		Stage.ColorArg2 = D3DTA_TEXTURE;
		if( RenDev->DeviceCaps.TextureOpCaps & D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR )
			Stage.ColorOp = D3DTOP_MODULATEALPHA_ADDCOLOR;
		else
			Stage.ColorOp = DUMMY_MODULATE2X;
	}
}

//
// Handle complex material
//
UBOOL FOpenGLRenderInterface::HandleCombinedMaterial( UMaterial* InMaterial, INT& PassesUsed, INT& StagesUsed, INT& FreeStagesUsed, FOpenGLModifierInfo ModifierInfo, UBOOL InvertOutputAlpha, FString* ErrorString, UMaterial** ErrorMaterial, UBOOL AddToCurrent )
{
	guard(FOpenGLRenderInterface::HandleCombinedMaterial);

	UVertexColor* VertexColor;
	UConstantMaterial* ConstantMaterial;
	UBitmapMaterial* BitmapMaterial;
	UCombiner* Combiner;

	INT MaxTextureStages = RenDev->DeviceCaps.MaxSimultaneousTextures;
	INT MaxBlendStages   = RenDev->DeviceCaps.MaxTextureBlendStages;
	INT MaxFreeStages = MaxBlendStages - MaxTextureStages;

	if( !InMaterial )
	{
		return 1;
	}
	else
	if( (VertexColor=CheckMaterial<UVertexColor,MRH_VertexColor>(this, InMaterial, &ModifierInfo)) != NULL )
	{
		if( StagesUsed >= MaxTextureStages+MaxFreeStages )
		{
			if(ErrorString ) *ErrorString = TEXT("No stages left for vertex color");
			if(ErrorMaterial) *ErrorMaterial = InMaterial;
			return 0;
		}
		CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg2 = D3DTA_CURRENT;
		CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG2;
		CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg1 = D3DTA_DIFFUSE;
		CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorOp = D3DTOP_SELECTARG1;
		StagesUsed++;
		if( MaxFreeStages )
			FreeStagesUsed++;
		return 1;
	}
	else
	if( (ConstantMaterial=CheckMaterial<UConstantMaterial,MRH_ConstantMaterial>(this, InMaterial, &ModifierInfo)) != NULL )
	{
		FColor	Color = ConstantMaterial->GetColor(Viewport->Actor->Level->TimeSeconds);

		if(CurrentState->MaterialPasses[PassesUsed]->TFactorColor == 0 || CurrentState->MaterialPasses[PassesUsed]->TFactorColor == Color.DWColor())
		{
			if( StagesUsed >= MaxTextureStages+MaxFreeStages )
			{
				if(ErrorString ) *ErrorString = TEXT("No stages left for constant color");
				if(ErrorMaterial) *ErrorMaterial = InMaterial;
				return 0;
			}
			CurrentState->MaterialPasses[PassesUsed]->TFactorColor = Color.DWColor();
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg1 = D3DTA_TFACTOR;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG1;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg1 = D3DTA_TFACTOR;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorOp = D3DTOP_SELECTARG1;
			if( MaxFreeStages )
				FreeStagesUsed++;
			StagesUsed++;
			return 1;
		}
		else
		{
			if(ErrorString) *ErrorString = TEXT("Only one ConstantMaterial may be used per material.");
			if(ErrorMaterial) *ErrorMaterial = InMaterial;
			return 0;
		}
	}
	else
	if( (BitmapMaterial=CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, InMaterial, &ModifierInfo)) != NULL )
	{
		// handle simple case
		if( StagesUsed >= MaxTextureStages+FreeStagesUsed )
		{
			if(ErrorString ) *ErrorString = TEXT("No stages left for BitmapMaterial");
			if(ErrorMaterial) *ErrorMaterial = InMaterial;
			return 0;
		}
		SetShaderBitmap( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], BitmapMaterial );
		CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg1 = InvertOutputAlpha ? (D3DTA_TEXTURE | D3DTA_COMPLEMENT) : D3DTA_TEXTURE;
		CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp   = D3DTOP_SELECTARG1;
		if( AddToCurrent )
		{
			// scion sz - add to current color
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg1 = D3DTA_CURRENT;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg2 = D3DTA_TEXTURE;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorOp   = D3DTOP_MODULATEALPHA_ADDCOLOR;
		}
		else
		{
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg1 = D3DTA_TEXTURE;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorOp   = D3DTOP_SELECTARG1;
		}		
		ApplyTexModifier( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], &ModifierInfo );
		StagesUsed++;
		return 1;
	}
	else
	if( (Combiner=CheckMaterial<UCombiner,MRH_Combiner>(this, InMaterial, &ModifierInfo)) != NULL )
	{
		UBitmapMaterial* BitmapMaterial1 = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, Combiner->Material1);
		UBitmapMaterial* BitmapMaterial2 = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, Combiner->Material2);

		if( !Combiner->Material1 )
		{
			if(ErrorString ) *ErrorString = TEXT("Combiner must specify Material1");
			if(ErrorMaterial) *ErrorMaterial = InMaterial;
			return 0;
		}
		else
		if( !BitmapMaterial1 && !BitmapMaterial2 )
		{
			if(ErrorString ) *ErrorString = TEXT("Either Material1 or Material2 must be a simple bitmap material.");
			if(ErrorMaterial) *ErrorMaterial = InMaterial;
			return 0;
		}


		// Swap Material1/2 if it makes our life easier.
		UMaterial* Material1;
		UMaterial* Material2;
		UBOOL Swapped;

		// Rearrange arguments to ensure simple bitmap is the second operand.
		if( !BitmapMaterial2 )
		{
			Material1 = Combiner->Material2;
			Material2 = Combiner->Material1;
			Swapped = 1;
		}
		else
		{
			if( Combiner->InvertMask && BitmapMaterial1 && Combiner->Mask==Combiner->Material2 )
			{
				// Swap the arguments if they're both bitmaps, and we're also using arg2 as an inverted mask
				Material1 = Combiner->Material2;
				Material2 = Combiner->Material1;
				Swapped = 1;
			}
			else
			{
				// No need to swap
				Material1 = Combiner->Material1;
				Material2 = Combiner->Material2;
				Swapped = 0;
			}
		}

		// Process the complex first operand.
		if( !HandleCombinedMaterial( Material1, PassesUsed, StagesUsed, FreeStagesUsed, ModifierInfo, Combiner->Mask==Material1 && Combiner->InvertMask, ErrorString, ErrorMaterial, AddToCurrent ) )
			return 0;

		UBOOL	UseTextureAlpha = 0,
				UseFactorAlpha = 0,
				UseDiffuseAlpha = 0;
		if( Combiner->Mask )
		{
			if( Combiner->Mask == Material2 )
			{
				if( Combiner->InvertMask )
				{
					if( StagesUsed >= MaxTextureStages+FreeStagesUsed )
					{
						if(ErrorString ) *ErrorString = Swapped ? TEXT("No stages left for inverted Mask which is the same as Material1") : TEXT("No stages left for inverted Mask which is the same as Material2");
						if(ErrorMaterial) *ErrorMaterial = InMaterial;
						return 0;
					}

					// Unfortunately we need to load the texture again to support an inverted mask for argument 2, when argument1 is complex
					FOpenGLModifierInfo MaskModifier = ModifierInfo;
					SetShaderBitmap( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, Combiner->Mask, &MaskModifier) );
					ApplyTexModifier( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], &MaskModifier );
					CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg1 = D3DTA_COMPLEMENT | D3DTA_TEXTURE;
					CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp   = D3DTOP_SELECTARG1;
					CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg1 = D3DTA_CURRENT;
					CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorOp   = D3DTOP_SELECTARG1;
					StagesUsed++;

					CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg1 = D3DTA_CURRENT | (InvertOutputAlpha ? D3DTA_COMPLEMENT : 0);
					CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp   = D3DTOP_SELECTARG1;
				}
				else
				{
					CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE | (InvertOutputAlpha ? D3DTA_COMPLEMENT : 0);
					CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp   = D3DTOP_SELECTARG1;
					UseTextureAlpha = 1;
			}
			}
			else
			if( Combiner->Mask != Material1 )
			{
				UBitmapMaterial*	MaskBitmap;
				UVertexColor*		VertexColor;
				UConstantMaterial*	ConstantMaterial;

				if( (MaskBitmap=CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, Combiner->Mask)) != NULL )
				{
					// Process alpha channel from Mask Bitmap.
					if( StagesUsed >= MaxTextureStages+FreeStagesUsed )
					{
						if(ErrorString ) *ErrorString = TEXT("No stages left for Mask bitmap");
					if( ErrorMaterial ) *ErrorMaterial = InMaterial;
					return 0;
				}
				FOpenGLModifierInfo MaskModifier = ModifierInfo;
					SetShaderBitmap( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, Combiner->Mask, &MaskModifier) );
					ApplyTexModifier( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], &MaskModifier );
					CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg1 = Combiner->InvertMask ? (D3DTA_COMPLEMENT | D3DTA_TEXTURE) : D3DTA_TEXTURE;
					CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp   = D3DTOP_SELECTARG1;
					CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
					CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorOp   = D3DTOP_SELECTARG2;
				StagesUsed++;

					CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg1 = D3DTA_CURRENT | (InvertOutputAlpha ? D3DTA_COMPLEMENT : 0);
					CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp   = D3DTOP_SELECTARG1;
				}
				else if( (VertexColor=CheckMaterial<UVertexColor,MRH_VertexColor>(this, Combiner->Mask)) != NULL )
				{
					CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg1 = D3DTA_DIFFUSE;
					CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG1;
					UseDiffuseAlpha = 1;
				}
				else if( (ConstantMaterial=CheckMaterial<UConstantMaterial,MRH_ConstantMaterial>(this, Combiner->Mask)) != NULL)
				{
					FColor	Color = ConstantMaterial->GetColor(Viewport->Actor->Level->TimeSeconds);

					if(CurrentState->MaterialPasses[PassesUsed]->TFactorColor == 0 || CurrentState->MaterialPasses[PassesUsed]->TFactorColor == Color.DWColor())
					{
						CurrentState->MaterialPasses[PassesUsed]->TFactorColor = Color.DWColor();
						CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg1 = D3DTA_TFACTOR;
						CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG1;
						UseFactorAlpha = 1;
					}
					else
					{
						if(ErrorString) *ErrorString = TEXT("Only one ConstantMaterial may be used per material.");
						if(ErrorMaterial) *ErrorMaterial = InMaterial;
						return 0;
					}
				}
				else
				{
					if( ErrorString ) *ErrorString = TEXT("Combiner Mask must be a bitmap material, a vertex color, a constant color, Material1 or Material2.");
					if( ErrorMaterial ) *ErrorMaterial = InMaterial;
					return 0;
				}
			}
		}

		if( StagesUsed >= MaxTextureStages+FreeStagesUsed )
		{
			if(ErrorString ) *ErrorString = Swapped ? TEXT("No stages left for Bitmap Material1") : TEXT("No stages left for Bitmap Material2");
			if(ErrorMaterial) *ErrorMaterial = InMaterial;
			return 0;
		}

		// Process Material2
		FOpenGLModifierInfo Material2Modifier = ModifierInfo;
		SetShaderBitmap( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, Material2, &Material2Modifier) );
		ApplyTexModifier( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], &Material2Modifier );

		switch( Combiner->CombineOperation )
		{
		case CO_Use_Color_From_Material1:
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg1 = D3DTA_TEXTURE;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorOp   = Swapped ? D3DTOP_SELECTARG1 : D3DTOP_SELECTARG2;
			break;
		case CO_Use_Color_From_Material2:
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg1 = D3DTA_TEXTURE;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorOp   = Swapped ? D3DTOP_SELECTARG2 : D3DTOP_SELECTARG1;
			break;
		case CO_Multiply:
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg1 = D3DTA_TEXTURE;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorOp   = Combiner->Modulate4X ? (D3DTOP_MODULATE4X) : (Combiner->Modulate2X ? DUMMY_MODULATE2X : D3DTOP_MODULATE);
			break;
		case CO_Add:
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg1 = D3DTA_TEXTURE;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorOp   = D3DTOP_ADD;
			break;
		case CO_Subtract:
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg1 = Swapped ? D3DTA_CURRENT : D3DTA_TEXTURE;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg2 = Swapped ? D3DTA_TEXTURE : D3DTA_CURRENT;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorOp   = D3DTOP_SUBTRACT;
			break;
		case CO_AlphaBlend_With_Mask:
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg1 = Swapped ? D3DTA_CURRENT : D3DTA_TEXTURE;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg2 = Swapped ? D3DTA_TEXTURE : D3DTA_CURRENT;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorOp   = UseTextureAlpha ? D3DTOP_BLENDTEXTUREALPHA : UseFactorAlpha ? D3DTOP_BLENDFACTORALPHA : UseDiffuseAlpha ? D3DTOP_BLENDDIFFUSEALPHA : D3DTOP_BLENDCURRENTALPHA;
			break;
		case CO_Add_With_Mask_Modulation:
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg1 = Swapped ? D3DTA_CURRENT : D3DTA_TEXTURE;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg2 = Swapped ? D3DTA_TEXTURE : D3DTA_CURRENT;
			if( RenDev->DeviceCaps.TextureOpCaps & D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR )
				CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorOp   = D3DTOP_MODULATEALPHA_ADDCOLOR;
			else
				CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorOp   = D3DTOP_ADD;
			break;
		}
		
		switch( Combiner->AlphaOperation )
		{
		case AO_Use_Mask:
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg2 = D3DTA_CURRENT;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp   = D3DTOP_SELECTARG2;			
			break;
		case AO_Multiply:
			if( Combiner->Mask && Combiner->Mask != Combiner->Material1 && Combiner->Mask != Combiner->Material2 )
			{
				if( ErrorString ) *ErrorString = TEXT("Combiner Mask must be Material1, or Material2 or None when using Alpha Operation AO_Multiply.");
				if( ErrorMaterial ) *ErrorMaterial = InMaterial;
				return 0;
			}
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg2 = D3DTA_CURRENT;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp   = D3DTOP_MODULATE;
			if( InvertOutputAlpha )
			{
				StagesUsed++;
				CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg1 = D3DTA_CURRENT;
				CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorOp   = D3DTOP_SELECTARG1;
				CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg1 = D3DTA_CURRENT | D3DTA_COMPLEMENT;
				CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp   = D3DTOP_SELECTARG1;
			}
			break;
		case AO_Add:
			if( Combiner->Mask && Combiner->Mask != Combiner->Material1 && Combiner->Mask != Combiner->Material2 )
			{
				if( ErrorString ) *ErrorString = TEXT("Combiner Mask must be Material1, or Material2 or None when using Alpha Operation AO_Add.");
				if( ErrorMaterial ) *ErrorMaterial = InMaterial;
				return 0;
			}
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg2 = D3DTA_CURRENT;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp   = D3DTOP_ADD;
			if( InvertOutputAlpha )
			{
				StagesUsed++;
				CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg1 = D3DTA_CURRENT;
				CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorOp   = D3DTOP_SELECTARG1;
				CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg1 = D3DTA_CURRENT | D3DTA_COMPLEMENT;
				CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp   = D3DTOP_SELECTARG1;
			}
			break;
		case AO_Use_Alpha_From_Material1:
			if( Combiner->Mask && Combiner->Mask != Combiner->Material1 && Combiner->Mask != Combiner->Material2 )
			{
				if( ErrorString ) *ErrorString = TEXT("Combiner Mask must be Material1, or Material2 or None when using Alpha Operation AO_Use_Alpha_From_Material1.");
				if( ErrorMaterial ) *ErrorMaterial = InMaterial;
				return 0;
			}
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg2 = D3DTA_CURRENT;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp   = Swapped ? D3DTOP_SELECTARG1 : D3DTOP_SELECTARG2;
			if( InvertOutputAlpha )
			{
				StagesUsed++;
				CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg1 = D3DTA_CURRENT;
				CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorOp   = D3DTOP_SELECTARG1;
				CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg1 = D3DTA_CURRENT | D3DTA_COMPLEMENT;
				CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp   = D3DTOP_SELECTARG1;
			}
			break;
		case AO_Use_Alpha_From_Material2:
			if( Combiner->Mask && Combiner->Mask != Combiner->Material1 && Combiner->Mask != Combiner->Material2 )
			{
				if( ErrorString ) *ErrorString = TEXT("Combiner Mask must be Material1, or Material2 or None when using Alpha Operation AO_Use_Alpha_From_Material2.");
				if( ErrorMaterial ) *ErrorMaterial = InMaterial;
				return 0;
			}
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg2 = D3DTA_CURRENT;
			CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp   = Swapped ? D3DTOP_SELECTARG2 : D3DTOP_SELECTARG1;
			if( InvertOutputAlpha )
			{
				StagesUsed++;
				CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg1 = D3DTA_CURRENT;
				CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorOp   = D3DTOP_SELECTARG1;
				CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg1 = D3DTA_CURRENT | D3DTA_COMPLEMENT;
				CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp   = D3DTOP_SELECTARG1;
			}
			break;
		}
		StagesUsed++;
	}

	return 1;
	unguard;
}

inline DWORD F2DW( FLOAT f ) { return *((DWORD*)&f);}
// scion sz -
// FOpenGLRenderInterface::HandleBumpMaterial
//
void FOpenGLRenderInterface::HandleBumpMaterial( FOpenGLMaterialStateStage& Stage, UBitmapMaterial* BumpMapBitmap,  FOpenGLModifierInfo* ModifierInfo)
{
	guard(FOpenGLRenderInterface::HandleBumpMaterial);
#ifdef BUMPMAP_ENABLE

	SetShaderBitmap( Stage, BumpMapBitmap );

	if( BumpMapBitmap->Format == TEXF_L6V5U5	)
	{
		Stage.ColorOp = D3DTOP_BUMPENVMAPLUMINANCE;
	}
	else
	{
		Stage.ColorOp = D3DTOP_BUMPENVMAP;
	}

	Stage.ColorArg1 = D3DTA_TEXTURE;
	Stage.ColorArg2 = D3DTA_CURRENT;
#ifdef _XBOX
	Stage.ColorSign = D3DTSIGN_RSIGNED|D3DTSIGN_GSIGNED|D3DTSIGN_BSIGNED;
#else
	Stage.ColorSign = 0;
#endif

	Stage.BumpMapEnabled = true;
	Stage.BumpEnvMat00 = F2DW(0.5f); 
	Stage.BumpEnvMat01 = F2DW(0.0f); 
	Stage.BumpEnvMat10 = F2DW(0.0f); 
	Stage.BumpEnvMat11 = F2DW(0.5f);
	Stage.BumpEnvLScale = F2DW(1.0f);
	Stage.BumpEnvLOffset = F2DW(0.0f);

	ApplyTexModifier( Stage, ModifierInfo );
#endif
	unguard;
}

// scion sz -
// FOpenGLRenderInterface::UpdateDOFRampLookupTexture
//
void FOpenGLRenderInterface::UpdateDOFRampLookupTexture( UMaterial* DOFShader, BYTE FocusType )
{
#ifdef _XBOX
	guard(FOpenGLRenderInterface::UpdateDOFRampLookupTexture);

	UPSDepthOfFieldMaterial* InDOFShader = (UPSDepthOfFieldMaterial*)DOFShader;

	if( !InDOFShader )
		return;

	if( !InDOFShader->bUpdateRampLookup )
		return;

	// -- make sure the texture is cached
	FOpenGLTexture* tex = CacheTexture( InDOFShader->DynamicRampLookup->Get(Viewport->CurrentTime,Viewport)->GetRenderInterface() );
	if( !tex || !tex->Direct3DTexture8 )
		return;

	DWORD Width = tex->CachedWidth;
	DWORD Height = tex->CachedHeight;

	// -- Using D3DFMT_LIN_B8G8R8A8 format for the depth buffer. So, B8G8 represent the most significant 16bits
	//    of Z.  The pixel shader for DOF uses a texreg2gb dependent texture read on these depth values.
	//    This means that U coordinates come from Green and V coordinates come from Blue.  So our U axis (width) 
	//    represents the least significant 8 bits (green) and the V axis (Height) represent the most significant 
	//    8 bits (blue) of our 16 bit z value.

	BYTE* p;
	FLOAT fU,fV,fUV,Result;
	DWORD dwU,dwV,dwUV;

	Swizzler s(Width, Height, 0);			// This texture is swizzled so we need a swizzler to manipulate/traverse it
	s.SetV(s.SwizzleV(0));
	s.SetU(s.SwizzleU(0));
	//scion jg -- Don't lock the texture just write to it so we don't stall
	BYTE* Bits = (BYTE*)(tex->Direct3DTexture8->Data | 0x80000000);

	// Only do the midpoint function for dynamically adjusting depths
	// DOF_FocusAbsoluteZDepth == 3
	if (FocusType < 3)
	{
		// -- Convert world coordinate values for near/far to homogeneous space (WVP & 1/w) z values
		//    These near/far values will then be used to bias and scale the lookup ramp so that only areas
		//	  within [0.0,Near] and [Far,1.0] appear blurry.  That's the theory anyway.

		D3DXVECTOR4 vNear( InDOFShader->FocusWorldNear.X, InDOFShader->FocusWorldNear.Y, InDOFShader->FocusWorldNear.Z, 1.0f );
		D3DXVec4Transform( &vNear, &vNear, (D3DXMATRIX*)&Viewport->LodSceneNode->WorldToCamera );

		// -- clamp DOF near to be bigger than the NEAR_CLIPPING_PLANE plus some silly fudge factor
		FLOAT NearDiff = 0.0f;
		static FLOAT MinZNear = (NEAR_CLIPPING_PLANE+40.f);
		if( vNear.z < MinZNear )
		{
			NearDiff = MinZNear - vNear.z;
			vNear.z = MinZNear;
		}
		D3DXVec4Transform( &vNear, &vNear, (D3DXMATRIX*)&Viewport->LodSceneNode->CameraToScreen );
		InDOFShader->FocusNear = vNear.z / vNear.w;	

		D3DXVECTOR4 vFar( InDOFShader->FocusWorldFar.X, InDOFShader->FocusWorldFar.Y, InDOFShader->FocusWorldFar.Z, 1.0f );
		D3DXVec4Transform( &vFar, &vFar, (D3DXMATRIX*)&Viewport->LodSceneNode->WorldToCamera );
		// -- add silly fudge factor to DOF far so that we maintain the midpoint over the focus distance
		vFar.z += NearDiff;
		D3DXVec4Transform( &vFar, &vFar, (D3DXMATRIX*)&Viewport->LodSceneNode->CameraToScreen );
		InDOFShader->FocusFar = vFar.z / vFar.w;

		if( InDOFShader->FocusFar < InDOFShader->FocusNear )
		{
			// -- shouldn't happen.. but swap if near>far
			FLOAT FocusTemp = InDOFShader->FocusFar;
			InDOFShader->FocusFar = InDOFShader->FocusNear;
			InDOFShader->FocusNear = FocusTemp;
		}

		// -- set values
		FLOAT FocusRange = InDOFShader->FocusFar - InDOFShader->FocusNear;				// positive distance between Near+Far
		FLOAT FocusMidpoint = (InDOFShader->FocusFar + InDOFShader->FocusNear)*0.5f;	// (Near+Far)/2

		for( DWORD v=0; v<Height; v++ )		// blue channel (most significant 8 bits of z)	
		{
			fV = v * 1.f/(Height-1);
			dwV = appTrunc(fV*255.0f);
			for( DWORD u=0; u<Width; u++ )			// green channel (least significant 8 bits of z)
			{
				p = Bits + s.Get2D();
				fU = u * 1.f/(Width-1);
				dwU = appTrunc(fU*255.0f);			
				dwUV = ((dwV<<16)&0xff0000) | ((dwU<<8)&0x00ff00);
				fUV = (dwUV/(float)(D3DZ_MAX_D24S8));
				Result = (2.0f/FocusRange) * (fUV-FocusMidpoint);		// y = (2*(x-focusMidpoint)/FocusRange)^4
				Result = Result*Result;
				Result = (Result>1.0f) ? 1.0f : Result;
				*p = (BYTE)(255 * Result);
				s.IncU();
			}
			s.IncV();
		}
	}
	else
	{
		// Calculate an alpha based upon absolute Z-Depth
		for( DWORD v=0; v<Height; v++ )
		{
			fV = v * 1.f/(Height-1);
			dwV = appTrunc(fV*255.0f);
			for( DWORD u=0; u<Width; u++ )
			{
				p = Bits + s.Get2D();
				fU = u * 1.f/(Width-1);
				dwU = appTrunc(fU*255.0f);			
				dwUV = ((dwV<<16)&0xff0000) | ((dwU<<8)&0x00ff00);
				fUV = (dwUV/(float)(D3DZ_MAX_D24S8));
				// Use zero alpha until we cross StartZ (FocusNear)
				if (fUV < InDOFShader->FocusNear)
				{
					*p = 0;
				}
				else
				{
					// Calculate the blur based upon the distance between start
					// and end depths
					Result = (fUV - InDOFShader->FocusNear) / (1.f - InDOFShader->FocusNear);
					*p = (BYTE)(255.f * Result);
				}
				s.IncU();
			}
			s.IncV();
		}
	}

	InDOFShader->bUpdateRampLookup = false;

	unguard
#endif //_XBOX
}

// scion sz -
// FOpenGLRenderInterface::HandlePSVertexShader
//
void FOpenGLRenderInterface::HandlePSVertexShader( UMaterial* InMaterial, FOpenGLMaterialState* MaterialState )
{
	guard(FOpenGLRenderInterface::HandlePSVertexShader);

#ifndef _XBOX 
	return;
#endif

	// UC2GLDrv: the rest is the Xbox vertex shader setup (VSC_* constants exist only there).
	// M6: the spliced GLSL vertex shaders take it over.
#ifdef _XBOX
	UPixelShaderMaterial* PixelShaderMaterial = MaterialCast<UPixelShaderMaterial,MRH_PixelShaderMaterial>(InMaterial);
	if( !PixelShaderMaterial )
		return;

	//scion jg -- Handle the range of distortion effects
	if( PixelShaderMaterial->PixelShaderType >= PS_Distortion &&
		PixelShaderMaterial->PixelShaderType <= PS_DistortionAccum)
	{
		static FLOAT MAX_DISTORTION_AMT_CLAMP = (UTexture::__Client) ? UTexture::__Client->MaxDistortion : 20.f;
		static FLOAT DISTORTION_CULL_DISTANCE = 5000.f;

		switch( PostProcessStage )
		{
		case PP_STAGE_ACCUMULATE:
			{
				// get distortion amount from shader
				UPSDistortionBaseMaterial* DistortionShader = (UPSDistortionBaseMaterial*)( PixelShaderMaterial );				
				FLOAT fDistortAmt = Min<FLOAT>( DistortionShader->DistortionAmount, MAX_DISTORTION_AMT_CLAMP*3.0f );
                // scale the distortion amount since it will be clamped to [0,1] as a texcoord output
				fDistortAmt /= MAX_DISTORTION_AMT_CLAMP;
				FLOAT DistortionScale[4] = 
				{ 
					fDistortAmt, 
					fDistortAmt, 
					1.0f/DISTORTION_CULL_DISTANCE,
					0.f
				};
				// scion sz - set distortion scale constant
				RenDev->SetVertexShaderConstant( VSC_DISTORTION_SCALE, DistortionScale, 1 );
				// scion sz - set vertex shader needed for distortion accumulation
				MaterialState->MaterialVertexShaderEnum = VS_GETUNIQUESHADER_QWORD(VS_DistortionAccum);
			}
			break;
		case PP_STAGE_NONE:
		case PP_STAGE_RENDER_DISTORTION:
			{				
#if OLD_DISTORT_METHOD 
				// copy of back buffer 
				UMaterial* BBMat = RI->GetBackBufferCopyMaterial();	
#else
				// use back buffer directly
				UMaterial* BBMat = GetBackBufferMaterial();
#endif
				// get distortion amount from shader
				UPSDistortionBaseMaterial* DistortionShader = (UPSDistortionBaseMaterial*)( PixelShaderMaterial );		
				FLOAT fDistortAmt = Min<FLOAT>( DistortionShader->DistortionAmount, MAX_DISTORTION_AMT_CLAMP );
				FLOAT DistortionScale[4] = 
				{ 
					fDistortAmt, 
					fDistortAmt, 
					(FLOAT)BBMat->MaterialUSize() / (FLOAT)RenDev->DefaultRenderTarget->GetWidth(), 
					(FLOAT)BBMat->MaterialVSize() / (FLOAT)RenDev->DefaultRenderTarget->GetHeight()
				};
				// scion sz - set distortion scale constant
				RenDev->SetVertexShaderConstant( VSC_DISTORTION_SCALE, DistortionScale, 1 );				
				//scion jg -- Set this separate from the vertex shader so that SetMaterial
				// can be called before SetVertexStreams. 
				// scion sz - set vertex shader needed for distortion
				MaterialState->MaterialVertexShaderEnum = VS_GETUNIQUESHADER_QWORD(VS_Distortion);		
			}
			break;
		case PP_STAGE_RENDER_COLOR:	
		default:
			{
				// scion sz -
				// don't need any special vertex shader for just rendering color
			}
			break;
		}		
	}
	//jg -- Handle color remapping VS
	else if (PixelShaderMaterial->PixelShaderType >= PS_ColorRemap &&
		PixelShaderMaterial->PixelShaderType <= PS_ColorRemapMasked)
	{
		// Luminance 3x1 matrix
		static FLOAT flColor[] = { 0.30f, 0.59f, 0.11f, 1.0f };
		RenDev->SetVertexShaderConstant(VSC_COLOR_REMAP,flColor,1);
		//scion jg -- Set this separate from the vertex shader so that SetMaterial
		// can be called before SetVertexStreams. 
		MaterialState->MaterialVertexShaderEnum = VS_GETUNIQUESHADER_QWORD(VS_ColorRemap);
	}
#endif

	unguard;
}

// scion sz - 
// FOpenGLRenderInterface::SetShaderMaterial
//
UBOOL FOpenGLRenderInterface::SetPixelShaderMaterial( UPixelShaderMaterial* InShader, FOpenGLModifierInfo InModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial, INT InPassesUsed )
{
	guard(FOpenGLRenderInterface::SetPixelShaderMaterial);

	INT StagesUsed = 0;
	INT PassesUsed = InPassesUsed;
	FOpenGLMaterialState*	MaterialState = CurrentState->MaterialPasses[PassesUsed];
	FOpenGLModifierInfo TexModifiers[4];

#ifndef _XBOX
	// UC2GLDrv test switch, off unless -gldualdiffuse=<n> is on the command line. No package of
	// the game contains a PSDualDiffuse material (tools/dev/ps-census.py), so the 32 DualDiffuse
	// shaders and their handler cannot be seen on shipped content. With the switch, every
	// PSEmissiveShader material that has a diffuse and a specular texture is drawn as a
	// PSDualDiffuse built from those two textures: n = BlendMode (0 add, 1 modulate, 2 blend)
	// + 10 * MaskMode (0 none, 1 first texture's alpha, 2 second texture's alpha).
	static INT TestDualDiffuse = -2;
	if( TestDualDiffuse == -2 && !Parse( appCmdLine(), TEXT("gldualdiffuse="), TestDualDiffuse ) )
		TestDualDiffuse = -1;
	if( TestDualDiffuse >= 0 && InShader->PixelShaderType >= PS_EmissiveShaderDiffuseOnly && InShader->PixelShaderType <= PS_EmissiveShaderSpecLight )
	{
		UPSEmissiveShader* Emissive = (UPSEmissiveShader*)InShader;
		if( Emissive->Diffuse && Emissive->Specular )
		{
			DECLARE_STATIC_UOBJECT( UPSDualDiffuse, TestDualDiffuseMaterial, {} );
			TestDualDiffuseMaterial->Diffuse					= Emissive->Diffuse;
			TestDualDiffuseMaterial->Diffuse2					= Emissive->Specular;
			TestDualDiffuseMaterial->Specular					= NULL;
			TestDualDiffuseMaterial->SpecularMask				= NULL;
			TestDualDiffuseMaterial->BlendMode					= TestDualDiffuse % 10;
			TestDualDiffuseMaterial->MaskMode					= (TestDualDiffuse / 10) % 10;
			TestDualDiffuseMaterial->bUseLightingForSpecular	= 0;
			TestDualDiffuseMaterial->bIsTwoSided				= Emissive->bIsTwoSided;
			TestDualDiffuseMaterial->bRebuild					= 1;
			InShader = TestDualDiffuseMaterial;
		}
	}
#endif

	InShader->PreRender( this );
	HandlePSVertexShader( InShader, MaterialState );

#ifndef _XBOX
	// UC2GLDrv (M6): PC's test here is InShader->IsXboxOnly() == 0, because PC had hand-written
	// DX8 shaders for some types only. Ours is whether the material's whole shader family is
	// translated, compiled and has its inputs at this milestone (UUC2GLRenderDevice::
	// PixelShaderFamilyReady): then the Xbox branch below is taken, Xbox-only or not.
	// Otherwise, and always with UsePixelShaders=False, PC's Xbox-only fallback: texture
	// stage 0 through SetSimpleMaterial, then the state handler.
	// M7: the families that stay on the fallback are those whose input is the back buffer, the
	// depth buffer or another render target (distortion, blur, depth of field, bloom and emissive
	// select, motion blur, luminance, player cam, colour remap): UC2GL_IsM7PixelShader.
	if( RenDev->PixelShaderFamilyReady( InShader->PixelShaderType ) )
#endif
	{
		MaterialState->UsePixelShaderProgram = 1;

		// -- set the desired pixel shader 
		MaterialState->PixelShader = (EPixelShader)InShader->PixelShaderType;	

		if (InShader->TextureStage0)
		{			
			UBitmapMaterial* TexBitmap0=CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, InShader->TextureStage0, &TexModifiers[0]);
			SetShaderBitmap( MaterialState->Stages[StagesUsed], TexBitmap0 );
			ApplyTexModifier( MaterialState->Stages[StagesUsed], &TexModifiers[0] );
			StagesUsed++;
		}

		if (InShader->TextureStage1)
		{
			UBitmapMaterial* TexBitmap1=CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, InShader->TextureStage1, &TexModifiers[1]);
			SetShaderBitmap( MaterialState->Stages[StagesUsed], TexBitmap1 );
			ApplyTexModifier( MaterialState->Stages[StagesUsed], &TexModifiers[1] );
			StagesUsed++;
		}

		if (InShader->TextureStage2)
		{
			UBitmapMaterial* TexBitmap2=CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, InShader->TextureStage2, &TexModifiers[2]);
			SetShaderBitmap( MaterialState->Stages[StagesUsed], TexBitmap2 );
			ApplyTexModifier( MaterialState->Stages[StagesUsed], &TexModifiers[2] );
			StagesUsed++;
		}

		if (InShader->TextureStage3)
		{
			UBitmapMaterial* TexBitmap3=CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, InShader->TextureStage3, &TexModifiers[3]);
			SetShaderBitmap( MaterialState->Stages[StagesUsed], TexBitmap3 );
			ApplyTexModifier( MaterialState->Stages[StagesUsed], &TexModifiers[3] );
			StagesUsed++;
		}
	}
#ifndef _XBOX
	else
	{
		FOpenGLModifierInfo TexModifier0;
		SetSimpleMaterial( InShader->TextureStage0, TexModifier0, NULL, NULL );
		StagesUsed++;
	}
#endif

	// (hack) opacity skin shaders may modify color (alpha) on their opacity tex stage
	// this needs to be added to the current color modifier for the material
	if( InShader->PixelShaderType >= PS_SkinShaderOpacity && 
		InShader->PixelShaderType <= PS_TeamSkinShaderSpecLightOpacity &&
		TexModifiers[2].ModifyColor )
	{
        UPSSkinShaderOpacity* pMaterial = (UPSSkinShaderOpacity*)InShader;
		FColor& Color = (InModifierInfo.ModifyColor == 1) ? InModifierInfo.TFactorColor : pMaterial->Color;
		InModifierInfo.TFactorColor.R = appTrunc( Color.R * ONE_OVER_255 * (FLOAT)TexModifiers[2].TFactorColor.R );
		InModifierInfo.TFactorColor.G = appTrunc( Color.G * ONE_OVER_255 * (FLOAT)TexModifiers[2].TFactorColor.G );
		InModifierInfo.TFactorColor.B = appTrunc( Color.B * ONE_OVER_255 * (FLOAT)TexModifiers[2].TFactorColor.B );
		InModifierInfo.TFactorColor.A = appTrunc( Color.A * ONE_OVER_255 * (FLOAT)TexModifiers[2].TFactorColor.A );
		InModifierInfo.ModifyColor = 1;
	}

	MaterialState->StagesUsed = StagesUsed;

	// scion sz - enable alpha writes only for emissive materials and only if
	// they are not being alpha blended
	if( InShader->IsEmissive() )
	{
		MaterialState->ColorWriteMask = CWM_COLORWRITEENABLE_ALL;
	}

	// set the material state for this pixel shader material
	FOpenGLPSMaterialStateHandlers::HandlePSMaterialState( InShader, &InModifierInfo, MaterialState, this );

	if( InModifierInfo.ModifyFramebufferBlending )
	{
		ApplyFinalBlend( &InModifierInfo );
	}

	CurrentState->NumMaterialPasses = ++PassesUsed;

	return 1;

	unguard
}

// scion sz -
// FOpenGLRenderInterface::SetMultiPassMaterial
//
UBOOL FOpenGLRenderInterface::SetMultiPassMaterial( UMultiPassMaterial* MultiPassMaterial, FOpenGLModifierInfo BaseModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial )
{
	guard(FOpenGLRenderInterface::SetMultiPassMaterial);

	checkSlow(MultiPassMaterial);

	CurrentState->NumMaterialPasses = 0;
	INT MaxTextureStages = RenDev->DeviceCaps.MaxSimultaneousTextures;

	for( INT PassIndex=0; PassIndex<MultiPassMaterial->MaterialPasses.Num(); PassIndex++ )
	{
		INT StagesUsed = 0;
		INT FreeStagesUsed = 0;
		INT PassesUsed = CurrentState->NumMaterialPasses;

		UMaterial*		Material = MultiPassMaterial->MaterialPasses(PassIndex).Material;
		UFinalBlend*	FinalBlend = MultiPassMaterial->MaterialPasses(PassIndex).FinalBlend;
		UMaterial*		Opacity = MultiPassMaterial->MaterialPasses(PassIndex).Opacity;
		UShader*		Shader = MaterialCast<UShader,MRH_Shader>(Material);

		FOpenGLModifierInfo PSModifierInfo;
		UPixelShaderMaterial* PSMaterial = CheckMaterial<UPixelShaderMaterial,MRH_PixelShaderMaterial>(this, Material, &PSModifierInfo );

		if( PSMaterial )
		{
			DECLARE_STATIC_UOBJECT( UPSSkinShaderOpacity, SkinShaderOpacity, {} );
			DECLARE_STATIC_UOBJECT( UPSTeamSkinShaderOpacity, TeamSkinShaderOpacity, {} );

            UPSSkinShader* SkinShader = Cast<UPSSkinShader>( PSMaterial );
			UPSTeamSkinShader* TeamSkinShader = Cast<UPSTeamSkinShader>( PSMaterial );
			if( TeamSkinShader )
			{
				TeamSkinShaderOpacity->Diffuse = TeamSkinShader->Diffuse;
				TeamSkinShaderOpacity->Specular = TeamSkinShader->Specular;
				TeamSkinShaderOpacity->Opacity = Opacity;
				TeamSkinShaderOpacity->TeamMask = TeamSkinShader->TeamMask;
				TeamSkinShaderOpacity->SpecularMask = TeamSkinShader->SpecularMask;
				TeamSkinShaderOpacity->bUseLightingForSpecular = TeamSkinShader->bUseLightingForSpecular;
				TeamSkinShaderOpacity->Color = TeamSkinShader->Color;

				SetPixelShaderMaterial( TeamSkinShaderOpacity, PSModifierInfo, ErrorString, ErrorMaterial, PassesUsed );
			}
			else if( SkinShader )
			{
				SkinShaderOpacity->Diffuse = SkinShader->Diffuse;
				SkinShaderOpacity->Specular = SkinShader->Specular;
				SkinShaderOpacity->Opacity = Opacity;
				SkinShaderOpacity->RimLight = SkinShader->RimLight;
				SkinShaderOpacity->bUseLightingForSpecular = SkinShader->bUseLightingForSpecular;
				SkinShaderOpacity->Color = SkinShader->Color;

				SetPixelShaderMaterial( SkinShaderOpacity, PSModifierInfo, ErrorString, ErrorMaterial, PassesUsed );
			}
			else
			{
				SetPixelShaderMaterial( PSMaterial, PSModifierInfo, ErrorString, ErrorMaterial, PassesUsed );
			}
		}
		else if( Shader )
		{
			UMaterial*	SavedShaderSpecular = Shader->Specular;
			UMaterial*	SavedShaderOpacity = Shader->Opacity;
			BYTE		SavedShaderOutputBlend = Shader->OutputBlending;
			
			Shader->Specular = NULL;		// shader specular not handled properly
			Shader->Opacity = Opacity;
			Shader->OutputBlending = MultiPassMaterial->MaterialPasses(PassIndex).ShaderOutputBlending;

			SetShaderMaterial( Shader, BaseModifierInfo, ErrorString, ErrorMaterial, PassesUsed );

			Shader->Specular = SavedShaderSpecular;
			Shader->Opacity = SavedShaderOpacity;
			Shader->OutputBlending = SavedShaderOutputBlend;

		}
		else
		{
			UBOOL NeedsDiffuseLight = CurrentState->UseStaticLighting || CurrentState->UseDynamicLighting;	
			HandleCombinedMaterial( Material, PassesUsed, StagesUsed, FreeStagesUsed, BaseModifierInfo, 0, ErrorString, ErrorMaterial );

			if( NeedsDiffuseLight )
			{
				if( StagesUsed == 1 )
				{
					// patch in diffuse to stage[0]
					HandleDiffuse_Patch( CurrentState->MaterialPasses[PassesUsed]->Stages[0] );
				}
				else
				{
					HandleDiffuse_Stage( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], 1 );
					StagesUsed++;
				}
			}

			if( Opacity )
			{
				FOpenGLModifierInfo OpacityModifierInfo;
				UBitmapMaterial* OpacityBitmap;
				UConstantMaterial* ConstantMaterial;
				if( (OpacityBitmap=CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, Opacity, &OpacityModifierInfo )) != NULL )
				{
					// The opacity is a simple texture:
					if( StagesUsed < MaxTextureStages+FreeStagesUsed )
					{
						HandleOpacityBitmap( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], OpacityBitmap, 1, &OpacityModifierInfo, PassesUsed );
						ApplyTexModifier( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], &OpacityModifierInfo );
						StagesUsed++;
					}
				}
				else if( (ConstantMaterial=CheckMaterial<UConstantMaterial,MRH_ConstantMaterial>(this, Opacity, &OpacityModifierInfo )) != NULL )
				{
					if( StagesUsed < MaxTextureStages+FreeStagesUsed )
					{
						FColor	Color = ConstantMaterial->GetColor(Viewport->Actor->Level->TimeSeconds);
						if(CurrentState->MaterialPasses[PassesUsed]->TFactorColor == 0 || CurrentState->MaterialPasses[PassesUsed]->TFactorColor == Color.DWColor())
						{
							CurrentState->MaterialPasses[PassesUsed]->TFactorColor = Color.DWColor();
							CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg1 = D3DTA_TFACTOR;
							CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG1;
							CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg1 = D3DTA_CURRENT;
							CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorOp = D3DTOP_SELECTARG1;
							StagesUsed++;
						}
					}
				}
			} // if(Opacity)

			CurrentState->MaterialPasses[PassesUsed]->SrcBlend		= D3DBLEND_ONE;
			CurrentState->MaterialPasses[PassesUsed]->DestBlend		= D3DBLEND_ZERO;
			CurrentState->MaterialPasses[PassesUsed]->AlphaBlending	= 0;
			CurrentState->MaterialPasses[PassesUsed]->AlphaTest		= 0;
			if( PassIndex) 
				CurrentState->MaterialPasses[PassesUsed]->ZTest		= 0;

			CurrentState->MaterialPasses[PassesUsed]->StagesUsed	= StagesUsed;
			CurrentState->NumMaterialPasses++;
		}		

		if( FinalBlend )
		{
			FOpenGLModifierInfo ModifierInfo;

			ModifierInfo.ModifyFramebufferBlending = 1;

			ModifierInfo.FrameBufferBlending	= FinalBlend->FrameBufferBlending;
			ModifierInfo.ZWrite					= FinalBlend->ZWrite;
			ModifierInfo.ZTest					= FinalBlend->ZTest;
			ModifierInfo.AlphaTest				= FinalBlend->AlphaTest;
			ModifierInfo.TwoSided				= FinalBlend->TwoSided;
			ModifierInfo.AlphaRef				= FinalBlend->AlphaRef;

			ApplyFinalBlend( &ModifierInfo, PassesUsed );          
		}
		else if( PassIndex )
		{
			CurrentState->MaterialPasses[PassesUsed]->ZTest			= 0;
		}

		// setup for a new pass
		if( PassIndex < (MultiPassMaterial->MaterialPasses.Num()-1) )
			CurrentState->MaterialPasses[CurrentState->NumMaterialPasses] = MaterialStatePool.AllocateState(&DefaultPass);		
	}
	
	return 1;

	unguard;
}

//
// FOpenGLRenderInterface::SetShaderMaterial
//
UBOOL FOpenGLRenderInterface::SetShaderMaterial( UShader* InShader, FOpenGLModifierInfo BaseModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial, INT InPassesUsed )
{
	guard(FOpenGLRenderInterface::SetShaderMaterial);

	INT MaxTextureStages = RenDev->DeviceCaps.MaxSimultaneousTextures;
	INT MaxBlendStages   = RenDev->DeviceCaps.MaxTextureBlendStages;
	INT MaxFreeStages = MaxBlendStages - MaxTextureStages;

	INT StagesUsed = 0;
	INT PassesUsed = InPassesUsed;
	INT FreeStagesUsed = 0;

	//
	// 1. Process diffuse, opacity and lightmap
	//
	UMaterial* InDiffuse = InShader->Diffuse;

	UBOOL Unlit = 0;
	UBOOL NeedsLightmap	= CurrentState->Lightmap ? 1 : 0;
	UBOOL NeedsDiffuseLight = CurrentState->UseStaticLighting || CurrentState->UseDynamicLighting;
	
	// Check for an unlit material
	if( InShader->SelfIllumination && !InShader->SelfIlluminationMask )
	{
		InDiffuse = InShader->SelfIllumination;
		Unlit = 1;
		NeedsDiffuseLight = 0;
		NeedsLightmap = 0;
	}

	// Check for an opacity override
	UMaterial* InOpacity = InShader->Opacity;
	UBOOL OpacityUseBaseModifier = 1;
	
	if( BaseModifierInfo.ModifyOpacity )
	{
		InOpacity = BaseModifierInfo.Opacity;
		OpacityUseBaseModifier = !BaseModifierInfo.OpacityOverrideTexModifier;
	}

	// Only simple shaders for those cards.
	if( RenDev->Is3dfx )
	{
		InShader->Specular			= NULL;
		InShader->SpecularityMask	= NULL;
	}

	// Simple case: no opacity!
	if( !InOpacity )
	{
		// Process diffuse channel
		if( !HandleCombinedMaterial( InDiffuse, PassesUsed, StagesUsed, FreeStagesUsed, BaseModifierInfo, 0, ErrorString, ErrorMaterial ) )
			return 0;		

		if( StagesUsed == 1 )
		{
			// patch in diffuse to stage[0]
			if( NeedsDiffuseLight )
			{
				HandleDiffuse_Patch( CurrentState->MaterialPasses[PassesUsed]->Stages[0] );
				NeedsDiffuseLight=0;
			}

			// add a lightmap
			if( NeedsLightmap )
			{
				HandleLightmap_SP( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], CurrentState->Lightmap );
				StagesUsed++;
			}
		}
		else
		if( StagesUsed<MaxTextureStages && NeedsLightmap && !NeedsDiffuseLight )
		{
			HandleLightmap_SP( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], CurrentState->Lightmap );
			StagesUsed++;
		}
		else
		if( StagesUsed<MaxTextureStages && !NeedsLightmap && NeedsDiffuseLight )
		{
			if( StagesUsed && !BaseModifierInfo.ModifyColor )
			{
				HandleDiffuse_Stage( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], InShader->ModulateStaticLighting2X );
				NeedsDiffuseLight=0;
				StagesUsed++;
			}
		}
		else
		{
			// Perform lighting in a seperate pass
			if( NeedsDiffuseLight || NeedsLightmap )
			{
				NEWPASS();
				HandleLighting_MP( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], CurrentState->Lightmap, NeedsDiffuseLight );
				StagesUsed++;

				// Set lighting framebuffer blending
				if( NeedsDiffuseLight && !NeedsLightmap && !InShader->ModulateStaticLighting2X )
				{
					CurrentState->MaterialPasses[PassesUsed]->SrcBlend				= D3DBLEND_DESTCOLOR;
					CurrentState->MaterialPasses[PassesUsed]->DestBlend				= D3DBLEND_ZERO;
				}
				else
				{
					CurrentState->MaterialPasses[PassesUsed]->SrcBlend				= D3DBLEND_DESTCOLOR;
					CurrentState->MaterialPasses[PassesUsed]->DestBlend				= D3DBLEND_SRCCOLOR;
					CurrentState->MaterialPasses[PassesUsed]->OverriddenFogColor	= FColor( 127, 127, 127, 0 );
					CurrentState->MaterialPasses[PassesUsed]->OverrideFogColor		= 1;
				}

				CurrentState->MaterialPasses[PassesUsed]->TwoSided		= InShader->TwoSided;
				CurrentState->MaterialPasses[PassesUsed]->AlphaBlending	= 1;
				NeedsDiffuseLight=0;
			}
		}	

		//
		// 2. Self-illumination
		//
		UBOOL SinglePassSelfIllum=0;
		if( !Unlit && InShader->SelfIllumination )
		{
			FOpenGLModifierInfo SelfIlluminationModifierInfo = BaseModifierInfo;
			FOpenGLModifierInfo SelfIlluminationMaskModifierInfo = BaseModifierInfo;

			UBitmapMaterial* SelfIlluminationBitmap = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, InShader->SelfIllumination, &SelfIlluminationModifierInfo );
			UBitmapMaterial* SelfIlluminationMaskBitmap = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, InShader->SelfIlluminationMask, &SelfIlluminationMaskModifierInfo );

			if( PassesUsed==0 && StagesUsed<MaxTextureStages-1 && SelfIlluminationBitmap && SelfIlluminationMaskBitmap )
			{
				// we have room to add the self-illumination in the single pass
				HandleOpacityBitmap( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], SelfIlluminationMaskBitmap );
				ApplyTexModifier( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed],&SelfIlluminationMaskModifierInfo );
				StagesUsed++;

				HandleSelfIllumination_SP( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], SelfIlluminationBitmap);
				ApplyTexModifier( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed],&SelfIlluminationModifierInfo );
				StagesUsed++;
				SinglePassSelfIllum=1;
			}
			else
			{
				// we need to do the self-illumination in its own pass
				NEWPASS();
				CurrentState->MaterialPasses[PassesUsed]->TwoSided				= InShader->TwoSided;

				// Load SelfIllumination
				if( !HandleCombinedMaterial( InShader->SelfIllumination, PassesUsed, StagesUsed, FreeStagesUsed, BaseModifierInfo, 0, ErrorString, ErrorMaterial ) )
					return 0;
	
				if( InShader->SelfIlluminationMask == InShader->SelfIllumination )
				{
					// Self-illumination alpha channel is in SelfIllumination.  Just blend it to the framebuffer.
					CurrentState->MaterialPasses[PassesUsed]->SrcBlend			= D3DBLEND_SRCALPHA;
					CurrentState->MaterialPasses[PassesUsed]->DestBlend			= D3DBLEND_INVSRCALPHA;
					CurrentState->MaterialPasses[PassesUsed]->AlphaBlending		= 1;
				}
				else
				if( SelfIlluminationMaskBitmap )
				{
					if( StagesUsed < MaxTextureStages )
					{
						HandleOpacityBitmap( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], SelfIlluminationMaskBitmap, 0,0, PassesUsed );
						ApplyTexModifier( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed],&SelfIlluminationMaskModifierInfo );
						StagesUsed++;
					}
					else
					{
						if( ErrorString ) *ErrorString = FString::Printf(TEXT("No room for SelfIllimunationMask in multipass SelfIllumination (StagesUsed was %d)"), StagesUsed);
						if( ErrorMaterial ) *ErrorMaterial = InShader;
						return 0;
					}

					// Add SelfIllumination to the framebuffer
					CurrentState->MaterialPasses[PassesUsed]->SrcBlend			= D3DBLEND_SRCALPHA;
					CurrentState->MaterialPasses[PassesUsed]->DestBlend			= D3DBLEND_INVSRCALPHA;
					CurrentState->MaterialPasses[PassesUsed]->AlphaBlending		= 1;
				}
				else
				{
					if( ErrorString ) *ErrorString = TEXT("SelfIllimunationMask must be a simple bitmap");
					if( ErrorMaterial ) *ErrorMaterial = InShader;
					return 0;
				}				
			}
		}

		//
		// 3. Detail Texture
		//
		if( RenDev->DetailTextures && CurrentState->UseDetailTexturing )
		{
			UMaterial* DetailMaterial = NULL;
			FLOAT DetailScale = 8.f;
			if( InShader->Detail )
			{
				DetailMaterial = InShader->Detail;
				DetailScale = InShader->DetailScale;
			}
			else
			{
				UTexture* DiffuseTex = CheckMaterial<UTexture,MRH_Texture>(this, InDiffuse);
				if( DiffuseTex )
				{
					DetailMaterial = DiffuseTex->Detail;
					DetailScale = DiffuseTex->DetailScale;
				}
			}

			if( DetailMaterial )
			{
				FOpenGLModifierInfo DetailModifierInfo = BaseModifierInfo;
				DetailModifierInfo.SetDetailTextureScale( DetailScale );
				UBitmapMaterial* DetailBitmap = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, DetailMaterial, &DetailModifierInfo);
				if( DetailBitmap )
					HandleDetail( DetailBitmap, PassesUsed, StagesUsed, FreeStagesUsed, DetailModifierInfo );
			}
		}

		//
		// 4. Process specular and mask
		//
		if( InShader->Specular )
		{
			FOpenGLModifierInfo SpecularModifierInfo = BaseModifierInfo;
			FOpenGLModifierInfo SpecularityMaskModifierInfo = BaseModifierInfo;
			FOpenGLModifierInfo BumpModifierInfo = BaseModifierInfo;

			UBitmapMaterial* SpecularBitmap = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, InShader->Specular, &SpecularModifierInfo );
			UBitmapMaterial* SpecularityMaskBitmap = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, InShader->SpecularityMask, &SpecularityMaskModifierInfo );
			UConstantMaterial* SpecularityMaskConstant = CheckMaterial<UConstantMaterial,MRH_ConstantMaterial>(this, InShader->SpecularityMask, &SpecularityMaskModifierInfo );			
			UBitmapMaterial* BumpMapBitmap = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, InShader->Bump, &BumpModifierInfo );
			UCombiner*	SpecularCombiner = CheckMaterial<UCombiner,MRH_Combiner>(this, InShader->Specular, &SpecularModifierInfo );

			if( PassesUsed==0 && StagesUsed<MaxTextureStages-2 && SpecularBitmap && BumpMapBitmap )
			{
#ifdef BUMPMAP_ENABLE
				HandleBumpMaterial( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], BumpMapBitmap, &BumpModifierInfo );
				StagesUsed++;
#endif

				HandleSpecular_SP( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], SpecularBitmap, 1, 0, InShader->PerformLightingOnSpecularPass && !NeedsDiffuseLight );
				ApplyTexModifier( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], &SpecularModifierInfo );
				StagesUsed++;

				HandleOpacityBitmap( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], SpecularityMaskBitmap );
				ApplyTexModifier( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], &SpecularityMaskModifierInfo );
				StagesUsed++;
			}
			else if( PassesUsed==0 && StagesUsed<MaxTextureStages && SpecularBitmap && ((!InShader->SpecularityMask)||(InShader->SpecularityMask==InShader->Diffuse&&!SinglePassSelfIllum)||SpecularityMaskConstant) )
			{
				// We can fit single-pass specular
				if( SpecularityMaskConstant )
				{
					BYTE SpecularAlpha = SpecularityMaskConstant->GetColor(Viewport->Actor->Level->TimeSeconds).A;
					//!!MAT check for TFactor use
					CurrentState->MaterialPasses[PassesUsed]->TFactorColor = (D3DCOLOR)(FColor(SpecularAlpha,SpecularAlpha,SpecularAlpha,SpecularAlpha));
				}
				HandleSpecular_SP( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], SpecularBitmap, InShader->SpecularityMask != NULL, SpecularityMaskConstant != NULL, InShader->PerformLightingOnSpecularPass && !NeedsDiffuseLight );
				ApplyTexModifier( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], &SpecularModifierInfo );
				StagesUsed++;
			}
			// scion sz - handle combiner in specular channel
			else if( PassesUsed==0 && StagesUsed<MaxTextureStages && SpecularCombiner && !SpecularityMaskBitmap )
			{
				if( !HandleCombinedMaterial( SpecularCombiner, PassesUsed, StagesUsed, FreeStagesUsed, BaseModifierInfo, 0, ErrorString, ErrorMaterial, 1 ) )
					return 0;                
			}
			else
			if(	PassesUsed==0 && StagesUsed<MaxTextureStages-1 && SpecularBitmap && SpecularityMaskBitmap )
			{
				// We have room for the specular and mask in the same pass pass as diffuse
				HandleSpecular_SP( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], SpecularBitmap, 1, 0, InShader->PerformLightingOnSpecularPass && !NeedsDiffuseLight );
				ApplyTexModifier( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], &SpecularModifierInfo );
				StagesUsed++;
				HandleOpacityBitmap( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], SpecularityMaskBitmap );
				ApplyTexModifier( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], &SpecularityMaskModifierInfo );
				StagesUsed++;
			}
			else
			{
				// We need to perform specular in its own pass
				NEWPASS();
				CurrentState->MaterialPasses[PassesUsed]->TwoSided = InShader->TwoSided;

				// Load specular
				if( !HandleCombinedMaterial( InShader->Specular, PassesUsed, StagesUsed, FreeStagesUsed, BaseModifierInfo, 0, ErrorString, ErrorMaterial ) )
					return 0;
	
				if( !InShader->SpecularityMask )
				{
					// Add specular to the framebuffer
					CurrentState->MaterialPasses[PassesUsed]->SrcBlend				= D3DBLEND_ONE;
					CurrentState->MaterialPasses[PassesUsed]->DestBlend				= D3DBLEND_ONE;
					CurrentState->MaterialPasses[PassesUsed]->AlphaBlending			= 1;
					CurrentState->MaterialPasses[PassesUsed]->OverrideFogColor		= 1;
					CurrentState->MaterialPasses[PassesUsed]->OverriddenFogColor	= FColor( 0, 0, 0, 0 );
				}
				else
				if( SpecularityMaskConstant )
				{
					//!!MAT
					if( ErrorString ) *ErrorString = TEXT("TODO: constant SpecularityMask with multipass specular");
					if( ErrorMaterial ) *ErrorMaterial = InShader;
					return 0;
				}
				else
				if( SpecularityMaskBitmap )
				{
					if( StagesUsed < MaxTextureStages )
					{
						if( InShader->PerformLightingOnSpecularPass && !NeedsDiffuseLight )
						{
							CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE;
							CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp   = D3DTOP_SELECTARG1;
							CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
							CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg1 = D3DTA_DIFFUSE;
							CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorOp   = D3DTOP_MODULATE;
						}
						else
						{
							HandleOpacityBitmap( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], SpecularityMaskBitmap, 0,0, PassesUsed );
						}						
						ApplyTexModifier( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], &SpecularityMaskModifierInfo );
						StagesUsed++;
					}
					else
					{
						if( ErrorString ) *ErrorString = FString::Printf(TEXT("No room for SpecularityMask in multipass specular (StagesUsed was %d)"), StagesUsed);
						if( ErrorMaterial ) *ErrorMaterial = InShader;
						return 0;
					}

					// Add specular to the framebuffer
					CurrentState->MaterialPasses[PassesUsed]->SrcBlend				= D3DBLEND_SRCALPHA;
					CurrentState->MaterialPasses[PassesUsed]->DestBlend			= D3DBLEND_ONE;
					CurrentState->MaterialPasses[PassesUsed]->AlphaBlending		= 1;
					CurrentState->MaterialPasses[PassesUsed]->OverrideFogColor		= 1;
					CurrentState->MaterialPasses[PassesUsed]->OverriddenFogColor	= FColor( 0, 0, 0, 0 );
				}
				else
				{
					if( ErrorString ) *ErrorString = TEXT("SpecularityMask must be a simple bitmap");
					if( ErrorMaterial ) *ErrorMaterial = InShader;
					return 0;
				}				
			}
		}

	} // no opacity
	else
	{	
		//
		// 1. Handle diffuse
		//
		if( !HandleCombinedMaterial( InDiffuse, PassesUsed, StagesUsed, FreeStagesUsed, BaseModifierInfo, 0, ErrorString, ErrorMaterial ) )
			return 0;

		// Handle the vertex color
		if( NeedsDiffuseLight )
		{
			if( StagesUsed==1 )
			{
				HandleDiffuse_Patch( CurrentState->MaterialPasses[PassesUsed]->Stages[0] );
				NeedsDiffuseLight=0;
			}
			else
			{
				if( StagesUsed < MaxTextureStages+MaxFreeStages )
				{
					if( !BaseModifierInfo.ModifyColor )
					{
						HandleDiffuse_Stage( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], InShader->ModulateStaticLighting2X );
						NeedsDiffuseLight=0;
						StagesUsed++;
						if( MaxFreeStages )
							FreeStagesUsed++;
					}					
				}
				else
				{
					if( ErrorString ) *ErrorString = TEXT("No stages left for vertex lighting stage");
					if( ErrorMaterial ) *ErrorMaterial = InShader;
					return 0;
				}
			}
		}

		// Handle the lightmap
		if( NeedsLightmap )
		{
			if( StagesUsed < MaxTextureStages+FreeStagesUsed )
			{
				HandleLightmap_SP( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], CurrentState->Lightmap );
				StagesUsed++;
			}
			else
			{
				if( ErrorString ) *ErrorString = TEXT("No stages left for lightmap");
				if( ErrorMaterial ) *ErrorMaterial = InShader;
				return 0;
			}
		}

		// Load the opacity if needed
		if( InOpacity != InDiffuse )
		{
			FOpenGLModifierInfo OpacityModifierInfo = OpacityUseBaseModifier ? BaseModifierInfo : FOpenGLModifierInfo();
			UBitmapMaterial* OpacityBitmap;
			UConstantMaterial* ConstantMaterial;
			UVertexColor* VertexColor;
			if( (OpacityBitmap=CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, InOpacity, &OpacityModifierInfo )) != NULL )
			{
				if( StagesUsed < MaxTextureStages+FreeStagesUsed )
				{
					// The opacity is a simple texture:
					HandleOpacityBitmap( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], OpacityBitmap, 1, &OpacityModifierInfo, PassesUsed );
					ApplyTexModifier( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], &OpacityModifierInfo );
					StagesUsed++;
				}
				else
				{
					if( ErrorString ) *ErrorString = TEXT("No stages left for opacity bitmap");
					if( ErrorMaterial ) *ErrorMaterial = InShader;
					return 0;
				}
			}
			else if( (ConstantMaterial=CheckMaterial<UConstantMaterial,MRH_ConstantMaterial>(this, InOpacity, &OpacityModifierInfo )) != NULL )
			{
				if( StagesUsed < MaxTextureStages+FreeStagesUsed )
				{
					FColor	Color = ConstantMaterial->GetColor(Viewport->Actor->Level->TimeSeconds);

					if(CurrentState->MaterialPasses[PassesUsed]->TFactorColor == 0 || CurrentState->MaterialPasses[PassesUsed]->TFactorColor == Color.DWColor())
					{
						CurrentState->MaterialPasses[PassesUsed]->TFactorColor = Color.DWColor();
						CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaArg1 = D3DTA_TFACTOR;
						CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG1;
						CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorArg1 = D3DTA_CURRENT;
						CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed].ColorOp = D3DTOP_SELECTARG1;
						StagesUsed++;
					}
					else
					{
						if(ErrorString) *ErrorString = TEXT("Only one ConstantMaterial may be used per material.");
						if(ErrorMaterial) *ErrorMaterial = InShader;
						return 0;
					}
				}
				else
				{
					if( ErrorString ) *ErrorString = TEXT("No stages left for constant opacity");
					if( ErrorMaterial ) *ErrorMaterial = InShader;
					return 0;
				}
			}
			else if( (VertexColor=CheckMaterial<UVertexColor,MRH_VertexColor>(this, InOpacity, &OpacityModifierInfo )) != NULL )
			{
				if( StagesUsed < MaxTextureStages+FreeStagesUsed )
				{
					HandleVertexOpacity( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], VertexColor );
					StagesUsed++;
				}
				else
				{
					if( ErrorString ) *ErrorString = TEXT("No stages left for vertex opacity");
					if( ErrorMaterial ) *ErrorMaterial = InShader;
					return 0;
				}
			}
			else
			{
				// don't handle this case yet!
				if( ErrorString ) *ErrorString = TEXT("Cannot handle complex opacity which is not the same as diffuse material");
				if( ErrorMaterial ) *ErrorMaterial = InOpacity;
				return 0;
			}
		}

		//
		// 2. Self-illumination
		//
		if( !Unlit && InShader->SelfIllumination )
		{
			// we need to do the self-illumination in its own pass
			NEWPASS();
			CurrentState->MaterialPasses[PassesUsed]->TwoSided = InShader->TwoSided;

			FOpenGLModifierInfo OpacityModifierInfo = OpacityUseBaseModifier ? BaseModifierInfo : FOpenGLModifierInfo();
			UBitmapMaterial* OpacityBitmap = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, InOpacity, &OpacityModifierInfo );

			if( !OpacityBitmap )
			{
				if( ErrorString ) *ErrorString = TEXT("Opacity must be a simple bitmap when using SelfIllumination");
				if( ErrorMaterial ) *ErrorMaterial = InOpacity;
				return 0;
			}

			FOpenGLModifierInfo SelfIlluminationModifierInfo = BaseModifierInfo;
			FOpenGLModifierInfo SelfIlluminationMaskModifierInfo = BaseModifierInfo;

			UBitmapMaterial* SelfIlluminationMaskBitmap = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, InShader->SelfIlluminationMask, &SelfIlluminationMaskModifierInfo );

			// Load SelfIllumination
			if( !HandleCombinedMaterial( InShader->SelfIllumination, PassesUsed, StagesUsed, FreeStagesUsed, BaseModifierInfo, 0, ErrorString, ErrorMaterial ) )
				return 0;

			if( SelfIlluminationMaskBitmap )
			{
				if( StagesUsed < MaxTextureStages-1 )
				{
					HandleOpacityBitmap( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], SelfIlluminationMaskBitmap, 0,0, PassesUsed );
					ApplyTexModifier( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed],&SelfIlluminationMaskModifierInfo );
					StagesUsed++;

					HandleOpacityBitmap( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], OpacityBitmap, 1,0, PassesUsed );
					ApplyTexModifier( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], &OpacityModifierInfo );
					StagesUsed++;
				}
				else
				{
					if( ErrorString ) *ErrorString = FString::Printf(TEXT("No room for SelfIllimunationMask in multipass SelfIllumination (StagesUsed was %d)"), StagesUsed);
					if( ErrorMaterial ) *ErrorMaterial = InShader;
					return 0;
				}

				// Add SelfIllumination to the framebuffer
				CurrentState->MaterialPasses[PassesUsed]->SrcBlend				= D3DBLEND_SRCALPHA;
				CurrentState->MaterialPasses[PassesUsed]->DestBlend			= D3DBLEND_INVSRCALPHA;
				CurrentState->MaterialPasses[PassesUsed]->AlphaBlending		= 1;
			}
			else
			{
				if( ErrorString ) *ErrorString = TEXT("SelfIllimunationMask must be a simple bitmap");
				if( ErrorMaterial ) *ErrorMaterial = InShader;
				return 0;
			}				
		}

		//
		// 3. Specular
		//
		if( InShader->Specular )
		{
			FOpenGLModifierInfo SpecularModifierInfo = BaseModifierInfo;
			FOpenGLModifierInfo SpecularityMaskModifierInfo = BaseModifierInfo;

			UBitmapMaterial* SpecularityMaskBitmap = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, InShader->SpecularityMask, &SpecularityMaskModifierInfo );
			UConstantMaterial* SpecularityMaskConstant = CheckMaterial<UConstantMaterial,MRH_ConstantMaterial>(this, InShader->SpecularityMask, &SpecularityMaskModifierInfo );

			// We need to perform specular in its own pass
			NEWPASS();
			CurrentState->MaterialPasses[PassesUsed]->TwoSided = InShader->TwoSided;

			// Load specular
			if( !HandleCombinedMaterial( InShader->Specular, PassesUsed, StagesUsed, FreeStagesUsed, BaseModifierInfo, 0, ErrorString, ErrorMaterial ) )
				return 0;			

			if( !InShader->SpecularityMask )
			{
				// No specularity mask, do nothing.
			}
			else
			if( SpecularityMaskConstant )
			{
				//!!MAT
				if( ErrorString ) *ErrorString = TEXT("TODO: constant SpecularityMask with multipass specular");
				if( ErrorMaterial ) *ErrorMaterial = InShader;
				return 0;
			}
			else
			if( SpecularityMaskBitmap )
			{
				if( StagesUsed < MaxTextureStages )
				{
					HandleOpacityBitmap( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], SpecularityMaskBitmap, 0,0, PassesUsed );
					ApplyTexModifier( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], &SpecularityMaskModifierInfo );
					StagesUsed++;
				}
				else
				{
					if( ErrorString ) *ErrorString = FString::Printf(TEXT("No room for SpecularityMask in multipass specular (StagesUsed was %d)"), StagesUsed);
					if( ErrorMaterial ) *ErrorMaterial = InShader;
					return 0;
				}
			}
			else
			{
				if( ErrorString ) *ErrorString = TEXT("SpecularityMask must be a simple bitmap");
				if( ErrorMaterial ) *ErrorMaterial = InShader;
				return 0;
			}				

			// Add specular to the framebuffer
			CurrentState->MaterialPasses[PassesUsed]->SrcBlend			= InShader->SpecularityMask ? D3DBLEND_SRCALPHA : D3DBLEND_ONE;
			CurrentState->MaterialPasses[PassesUsed]->SrcBlend			= CurrentState->MaterialPasses[PassesUsed]->SrcBlend;
			CurrentState->MaterialPasses[PassesUsed]->DestBlend			= D3DBLEND_ONE;
			CurrentState->MaterialPasses[PassesUsed]->AlphaBlending		= 1;
			CurrentState->MaterialPasses[PassesUsed]->OverrideFogColor		= 1;
			CurrentState->MaterialPasses[PassesUsed]->OverriddenFogColor	= FColor( 0, 0, 0, 0 );
		}
	}

	//
	// Framebuffer blending
	//
	if( BaseModifierInfo.ModifyFramebufferBlending )
	{
		ApplyFinalBlend( &BaseModifierInfo );
	}
	else
	{
		switch( InShader->OutputBlending )
		{
		case OB_Normal :
			if( InOpacity )
			{
				CurrentState->MaterialPasses[InPassesUsed]->SrcBlend		= D3DBLEND_SRCALPHA;
				CurrentState->MaterialPasses[InPassesUsed]->DestBlend		= D3DBLEND_INVSRCALPHA;
				CurrentState->MaterialPasses[InPassesUsed]->AlphaBlending	= 1;
				CurrentState->MaterialPasses[InPassesUsed]->AlphaTest		= 1;
				CurrentState->MaterialPasses[InPassesUsed]->ZWrite		= 0;
			}
			else
			{
				CurrentState->MaterialPasses[InPassesUsed]->SrcBlend		= D3DBLEND_ONE;
				CurrentState->MaterialPasses[InPassesUsed]->DestBlend		= D3DBLEND_ZERO;
				CurrentState->MaterialPasses[InPassesUsed]->AlphaBlending	= 0;
				CurrentState->MaterialPasses[InPassesUsed]->AlphaTest		= 0;
				CurrentState->MaterialPasses[InPassesUsed]->ZWrite		= 1;
			}
			break;
		case OB_Masked:
			CurrentState->MaterialPasses[InPassesUsed]->SrcBlend			= D3DBLEND_ONE;
			CurrentState->MaterialPasses[InPassesUsed]->DestBlend			= D3DBLEND_ZERO;
			CurrentState->MaterialPasses[InPassesUsed]->AlphaBlending		= 0;
			CurrentState->MaterialPasses[InPassesUsed]->AlphaTest			= 1;
			CurrentState->MaterialPasses[InPassesUsed]->AlphaRef			= 127;
			CurrentState->MaterialPasses[InPassesUsed]->ZWrite			= 1;
			break;
		case OB_Modulate:
			CurrentState->MaterialPasses[InPassesUsed]->SrcBlend			= D3DBLEND_DESTCOLOR;
			CurrentState->MaterialPasses[InPassesUsed]->DestBlend			= D3DBLEND_SRCCOLOR;
			CurrentState->MaterialPasses[InPassesUsed]->AlphaBlending		= 1;
			CurrentState->MaterialPasses[InPassesUsed]->ZWrite			= 0;
			CurrentState->MaterialPasses[InPassesUsed]->OverrideFogColor	= 1;
			CurrentState->MaterialPasses[InPassesUsed]->OverriddenFogColor= FColor( 127, 127, 127, 0 );
			break;
		case OB_Translucent:
			CurrentState->MaterialPasses[InPassesUsed]->SrcBlend			= D3DBLEND_ONE;
			CurrentState->MaterialPasses[InPassesUsed]->DestBlend			= D3DBLEND_ONE;
			CurrentState->MaterialPasses[InPassesUsed]->AlphaBlending		= 1;
			CurrentState->MaterialPasses[InPassesUsed]->ZWrite			= 0;
			CurrentState->MaterialPasses[InPassesUsed]->OverrideFogColor	= 1;
			CurrentState->MaterialPasses[InPassesUsed]->OverriddenFogColor= FColor( 0, 0, 0, 0 );
			break;
		case OB_Brighten:
			CurrentState->MaterialPasses[InPassesUsed]->SrcBlend			= D3DBLEND_ONE;
			CurrentState->MaterialPasses[InPassesUsed]->DestBlend			= D3DBLEND_INVSRCCOLOR;
			CurrentState->MaterialPasses[InPassesUsed]->AlphaBlending		= 1;
			CurrentState->MaterialPasses[InPassesUsed]->ZWrite			= 0;
			CurrentState->MaterialPasses[InPassesUsed]->OverrideFogColor	= 1;
			CurrentState->MaterialPasses[InPassesUsed]->OverriddenFogColor= FColor( 0, 0, 0, 0 );
			break;
		case OB_Darken:
			CurrentState->MaterialPasses[InPassesUsed]->SrcBlend			= D3DBLEND_ZERO;
			CurrentState->MaterialPasses[InPassesUsed]->DestBlend			= D3DBLEND_INVSRCCOLOR;
			CurrentState->MaterialPasses[InPassesUsed]->AlphaBlending		= 1;
			CurrentState->MaterialPasses[InPassesUsed]->ZWrite			= 0;
			CurrentState->MaterialPasses[InPassesUsed]->OverrideFogColor	= 1;
			CurrentState->MaterialPasses[InPassesUsed]->OverriddenFogColor= FColor( 0, 0, 0, 0 );
			break;
		case OB_Invisible:
			CurrentState->MaterialPasses[InPassesUsed]->SrcBlend			= D3DBLEND_ZERO;
			CurrentState->MaterialPasses[InPassesUsed]->DestBlend			= D3DBLEND_ONE;
			CurrentState->MaterialPasses[InPassesUsed]->AlphaBlending		= 1;
			CurrentState->MaterialPasses[InPassesUsed]->ZWrite			= 0;
			break;
		}
		CurrentState->MaterialPasses[InPassesUsed]->TwoSided |= InShader->TwoSided;
	}

	// Fillmode.
	CurrentState->MaterialPasses[InPassesUsed]->FillMode = InShader->Wireframe ? FM_Wireframe : FM_Solid;

	// Color modifier
	if( BaseModifierInfo.ModifyColor )
	{
		if(	BaseModifierInfo.AlphaBlend									&&
			(CurrentState->MaterialPasses[InPassesUsed]->SrcBlend  == D3DBLEND_ONE)	&&
			(CurrentState->MaterialPasses[InPassesUsed]->DestBlend == D3DBLEND_ZERO)
		)
		{
				CurrentState->MaterialPasses[InPassesUsed]->SrcBlend	= D3DBLEND_SRCALPHA;
				CurrentState->MaterialPasses[InPassesUsed]->DestBlend	= D3DBLEND_INVSRCALPHA;
		}

		if( BaseModifierInfo.AlphaBlend && 
			NeedsDiffuseLight )
		{
			HandleDiffuseTFactor_Stage( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], InShader->ModulateStaticLighting2X );
		}
		else
		{
			HandleTFactor_SP(CurrentState->MaterialPasses[InPassesUsed]->Stages[StagesUsed]);
		}		

		CurrentState->MaterialPasses[InPassesUsed]->TFactorColor		 = BaseModifierInfo.TFactorColor;
		CurrentState->MaterialPasses[InPassesUsed]->AlphaBlending		|= BaseModifierInfo.AlphaBlend;
		CurrentState->MaterialPasses[InPassesUsed]->TwoSided			|= BaseModifierInfo.TwoSided;
		StagesUsed++;
	}

	// Done.
	CurrentState->MaterialPasses[PassesUsed++]->StagesUsed	= StagesUsed;
	CurrentState->NumMaterialPasses							= PassesUsed;
	
	return 1;
	unguard;
}

/*----------------------------------------------------------------------------
	SetSimpleMaterial.
----------------------------------------------------------------------------*/

UBOOL FOpenGLRenderInterface::SetSimpleMaterial( UMaterial* InMaterial, FOpenGLModifierInfo InModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial )
{
	guard(FOpenGLRenderInterface::SetSimpleMaterial);

	// Handle masked/alpha texture UTexture
	UTexture* Texture;
	UMaterial* DetailMaterial = NULL;
	FLOAT DetailScale = 8.f;
	if( !InModifierInfo.ModifyFramebufferBlending && (Texture=Cast<UTexture>(InMaterial))!=NULL )
	{
		if( Texture->bMasked )
		{
			InModifierInfo.ModifyFramebufferBlending	= 1;
			if( InModifierInfo.ModifyColor && InModifierInfo.AlphaBlend )
				InModifierInfo.FrameBufferBlending		= FB_AlphaBlend;
			else
				InModifierInfo.FrameBufferBlending		= FB_Overwrite;
			InModifierInfo.ZWrite		= 1;
			InModifierInfo.ZTest		= 1;
			InModifierInfo.AlphaTest	= 1;
			InModifierInfo.AlphaRef		= 127;
			if( !InModifierInfo.ModifyColor )
				InModifierInfo.TwoSided	= Texture->bTwoSided;
		}
		else
		if( Texture->bAlphaTexture )
		{
			InModifierInfo.ModifyFramebufferBlending	= 1;
			InModifierInfo.FrameBufferBlending			= FB_AlphaBlend;
			InModifierInfo.ZWrite		= 1;
			InModifierInfo.ZTest		= 1;
			InModifierInfo.AlphaTest	= 1;
			InModifierInfo.AlphaRef		= 0;
			if( !InModifierInfo.ModifyColor )
				InModifierInfo.TwoSided	= Texture->bTwoSided;
		}
		else
		{
			if( !InModifierInfo.ModifyColor )
				InModifierInfo.TwoSided	= Texture->bTwoSided;
		}

		DetailMaterial = Texture->Detail;
		DetailScale = Texture->DetailScale;
	}

	INT StagesUsed = 0;
	INT PassesUsed = 0;
	INT FreeStagesUsed = 0;

	INT MaxTextureStages = RenDev->DeviceCaps.MaxSimultaneousTextures;
	INT MaxBlendStages   = RenDev->DeviceCaps.MaxTextureBlendStages;
	INT MaxFreeStages	= MaxBlendStages - MaxTextureStages;

	if( !HandleCombinedMaterial( InMaterial, PassesUsed, StagesUsed, FreeStagesUsed, InModifierInfo, 0, ErrorString, ErrorMaterial ) )
		return 0;

	if( CurrentState->UseStaticLighting || CurrentState->UseDynamicLighting )
		HandleDiffuse_Patch( CurrentState->MaterialPasses[PassesUsed]->Stages[0] );

	if( CurrentState->Lightmap )
	{
		if( StagesUsed < MaxTextureStages )
			HandleLightmap_SP( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed++], CurrentState->Lightmap );
		else
		{
			NEWPASS();
			HandleLighting_MP( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed], CurrentState->Lightmap, CurrentState->UseStaticLighting );
			StagesUsed++;

			CurrentState->MaterialPasses[PassesUsed]->SrcBlend				= D3DBLEND_DESTCOLOR;
			CurrentState->MaterialPasses[PassesUsed]->DestBlend				= D3DBLEND_SRCCOLOR;
			CurrentState->MaterialPasses[PassesUsed]->OverriddenFogColor	= FColor( 127, 127, 127, 0 );
			CurrentState->MaterialPasses[PassesUsed]->OverrideFogColor		= 1;
			
			CurrentState->MaterialPasses[PassesUsed]->TwoSided				= InModifierInfo.TwoSided;
			CurrentState->MaterialPasses[PassesUsed]->AlphaBlending			= 1;
		}
	}

	// Set framebuffer blending
	if( InModifierInfo.ModifyFramebufferBlending )
	{
		ApplyFinalBlend( &InModifierInfo );
	}

	// Color modifier
	if( InModifierInfo.ModifyColor )
	{
		if( StagesUsed < MaxTextureStages+MaxFreeStages )
		{
			if(	InModifierInfo.AlphaBlend											&&
			    (CurrentState->MaterialPasses[PassesUsed]->SrcBlend  == D3DBLEND_ONE)	&&
			    (CurrentState->MaterialPasses[PassesUsed]->DestBlend == D3DBLEND_ZERO)
		    )
		    {
			    CurrentState->MaterialPasses[PassesUsed]->SrcBlend		= D3DBLEND_SRCALPHA;
			    CurrentState->MaterialPasses[PassesUsed]->DestBlend	= D3DBLEND_INVSRCALPHA;
		    }
		    HandleTFactor_SP(CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed]);
		    CurrentState->MaterialPasses[PassesUsed]->TFactorColor		= InModifierInfo.TFactorColor;
		    CurrentState->MaterialPasses[PassesUsed]->AlphaBlending   |= InModifierInfo.AlphaBlend;
		    CurrentState->MaterialPasses[PassesUsed]->TwoSided		   |= InModifierInfo.TwoSided;
		    StagesUsed++;
			if( MaxFreeStages )
				FreeStagesUsed++;
		}
		else
		{
			if( ErrorString ) *ErrorString = TEXT("No stages left for constant color modifier.");
			if( ErrorMaterial ) *ErrorMaterial = InMaterial;
			return 0;				
		}
	}

	// Detail textures.
	if( RenDev->DetailTextures && CurrentState->UseDetailTexturing && DetailMaterial )
	{
		FOpenGLModifierInfo DetailModifierInfo = InModifierInfo;
		DetailModifierInfo.SetDetailTextureScale( DetailScale );
		UBitmapMaterial* DetailBitmap = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, DetailMaterial, &DetailModifierInfo);
		if( DetailBitmap )
			HandleDetail( DetailBitmap, PassesUsed, StagesUsed, FreeStagesUsed, DetailModifierInfo );
	}

	CurrentState->MaterialPasses[PassesUsed]->StagesUsed = StagesUsed;
	StagesUsed = 0;
	PassesUsed++;
	CurrentState->NumMaterialPasses = PassesUsed;

	return 1;
	unguard;
}

/*----------------------------------------------------------------------------
	SetParticleMaterial.
----------------------------------------------------------------------------*/

UBOOL FOpenGLRenderInterface::SetParticleMaterial( UParticleMaterial* ParticleMaterial, FOpenGLModifierInfo InModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial )
{
	guard(FOpenGLRenderInterface::SetParticleMaterial);

	INT StagesUsed			= 0;
	INT PassesUsed			= 0;
	INT NumProjectors		= ParticleMaterial->AcceptsProjectors ? ParticleMaterial->NumProjectors : 0;
	UBOOL UseSpecialBlend	= 0;

	// Projectors on particles override blending between subdivisions for now.
	if( ParticleMaterial->BlendBetweenSubdivisions && NumProjectors && (RenDev->DeviceCaps.MaxSimultaneousTextures == 2) )
		ParticleMaterial->BlendBetweenSubdivisions = 0;

	switch ( ParticleMaterial->ParticleBlending )
	{
	case PTDS_Regular :
		CurrentState->MaterialPasses[0]->SrcBlend			= D3DBLEND_ONE;
		CurrentState->MaterialPasses[0]->DestBlend			= D3DBLEND_ZERO;
		CurrentState->MaterialPasses[0]->AlphaBlending		= 0;
		break;
	case PTDS_AlphaBlend:
		CurrentState->MaterialPasses[0]->SrcBlend			= D3DBLEND_SRCALPHA;
		CurrentState->MaterialPasses[0]->DestBlend			= D3DBLEND_INVSRCALPHA;
		CurrentState->MaterialPasses[0]->AlphaBlending		= 1;
		break;
	case PTDS_AlphaModulate_MightNotFogCorrectly:
		CurrentState->MaterialPasses[0]->SrcBlend			= D3DBLEND_ONE;
		CurrentState->MaterialPasses[0]->DestBlend			= D3DBLEND_INVSRCALPHA;
		CurrentState->MaterialPasses[0]->AlphaBlending		= 1;
		CurrentState->MaterialPasses[0]->OverrideFogColor	= 1;
		CurrentState->MaterialPasses[0]->OverriddenFogColor= FColor( 0, 0, 0, 0 );
		break;
	case PTDS_Modulated:
		CurrentState->MaterialPasses[0]->SrcBlend			= D3DBLEND_DESTCOLOR;
		CurrentState->MaterialPasses[0]->DestBlend			= D3DBLEND_SRCCOLOR;
		CurrentState->MaterialPasses[0]->AlphaBlending		= 1;
		UseSpecialBlend										= 1;
		CurrentState->MaterialPasses[0]->OverrideFogColor	= 1;
		CurrentState->MaterialPasses[0]->OverriddenFogColor= FColor( 127, 127, 127, 0 );
		break;
	case PTDS_Translucent:
		CurrentState->MaterialPasses[0]->SrcBlend			= D3DBLEND_ONE;
		CurrentState->MaterialPasses[0]->DestBlend			= D3DBLEND_ONE;
		CurrentState->MaterialPasses[0]->AlphaBlending		= 1;
		CurrentState->MaterialPasses[0]->OverrideFogColor	= 1;
		CurrentState->MaterialPasses[0]->OverriddenFogColor= FColor( 0, 0, 0, 0 );
		break;
	case PTDS_Darken:
		CurrentState->MaterialPasses[0]->SrcBlend			= D3DBLEND_ZERO;
		CurrentState->MaterialPasses[0]->DestBlend			= D3DBLEND_INVSRCCOLOR;
		CurrentState->MaterialPasses[0]->AlphaBlending		= 1;
		CurrentState->MaterialPasses[0]->OverrideFogColor	= 1;
		CurrentState->MaterialPasses[0]->OverriddenFogColor= FColor( 0, 0, 0, 0 );
		break;
	case PTDS_Brighten:
		CurrentState->MaterialPasses[0]->SrcBlend			= D3DBLEND_ONE;
		CurrentState->MaterialPasses[0]->DestBlend			= D3DBLEND_INVSRCCOLOR;
		CurrentState->MaterialPasses[0]->AlphaBlending		= 1;
		CurrentState->MaterialPasses[0]->OverrideFogColor	= 1;
		CurrentState->MaterialPasses[0]->OverriddenFogColor= FColor( 0, 0, 0, 0 );
		break;
	case PTDS_SourceDarken:
		CurrentState->MaterialPasses[0]->SrcBlend			= D3DBLEND_INVDESTCOLOR;
		CurrentState->MaterialPasses[0]->DestBlend			= D3DBLEND_INVSRCCOLOR;
		CurrentState->MaterialPasses[0]->AlphaBlending		= 1;
		CurrentState->MaterialPasses[0]->OverrideFogColor	= 1;
		CurrentState->MaterialPasses[0]->OverriddenFogColor= FColor( 0, 0, 0, 0 );
		break;
	}
	
	FOpenGLModifierInfo TexModifier;
	UBitmapMaterial* BitmapMaterial = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, ParticleMaterial->BitmapMaterial, &TexModifier);
	UPixelShaderMaterial* PixelShaderMaterial = CheckMaterial<UPixelShaderMaterial,MRH_PixelShaderMaterial>(this, ParticleMaterial->BitmapMaterial, &TexModifier);

	if( PixelShaderMaterial )
	{
		SetPixelShaderMaterial( PixelShaderMaterial, TexModifier, NULL, NULL );
		CurrentState->MaterialPasses[0]->ZWrite				= ParticleMaterial->ZWrite;
		CurrentState->MaterialPasses[0]->ZTest				= ParticleMaterial->ZTest;
		CurrentState->MaterialPasses[0]->TwoSided			= ParticleMaterial->RenderTwoSided;
		CurrentState->NumMaterialPasses = 1;
		return 1;
	}
	else if( BitmapMaterial )
	{
		SetShaderBitmap( CurrentState->MaterialPasses[0]->Stages[0], BitmapMaterial );
		ApplyTexModifier( CurrentState->MaterialPasses[0]->Stages[0], &TexModifier );
	}

	if( !ParticleMaterial->BlendBetweenSubdivisions )
	{
		CurrentState->MaterialPasses[0]->Stages[0].ColorArg1	= D3DTA_TEXTURE;
		CurrentState->MaterialPasses[0]->Stages[0].AlphaArg1	= D3DTA_TEXTURE;
		if( ParticleMaterial->UseTFactor )
		{
			CurrentState->MaterialPasses[0]->Stages[0].ColorArg2	= D3DTA_TFACTOR;
			CurrentState->MaterialPasses[0]->Stages[0].AlphaArg2	= D3DTA_TFACTOR;		
		}
		else
		{
			CurrentState->MaterialPasses[0]->Stages[0].ColorArg2	= D3DTA_DIFFUSE;
			CurrentState->MaterialPasses[0]->Stages[0].AlphaArg2	= D3DTA_DIFFUSE;
		}

		if( !UseSpecialBlend )
		{
			CurrentState->MaterialPasses[0]->Stages[0].ColorOp = D3DTOP_MODULATE;
			CurrentState->MaterialPasses[0]->Stages[0].AlphaOp = D3DTOP_MODULATE;
		}
		else
		{
			CurrentState->MaterialPasses[0]->Stages[0].ColorOp = D3DTOP_BLENDDIFFUSEALPHA;
			CurrentState->MaterialPasses[0]->Stages[0].AlphaOp = D3DTOP_BLENDDIFFUSEALPHA;
		}

		StagesUsed = 1;
	}
	else
	{	
		// Color = C * ( (1-A) * T1 + A * T2 )
		// Alpha = (1-A) * T1 + A * T2
		CurrentState->MaterialPasses[0]->Stages[0].ColorArg1	= D3DTA_TEXTURE;
		CurrentState->MaterialPasses[0]->Stages[0].AlphaArg1	= D3DTA_TEXTURE;
		CurrentState->MaterialPasses[0]->Stages[0].ColorOp		= D3DTOP_SELECTARG1;
		CurrentState->MaterialPasses[0]->Stages[0].AlphaOp		= D3DTOP_SELECTARG1;

		CurrentState->MaterialPasses[0]->Stages[1].ColorArg1	= D3DTA_TEXTURE;
		CurrentState->MaterialPasses[0]->Stages[1].ColorArg2	= D3DTA_CURRENT;
		CurrentState->MaterialPasses[0]->Stages[1].AlphaArg1	= D3DTA_TEXTURE;
		CurrentState->MaterialPasses[0]->Stages[1].AlphaArg2	= D3DTA_CURRENT;
		CurrentState->MaterialPasses[0]->Stages[1].ColorOp		= D3DTOP_BLENDDIFFUSEALPHA;
		CurrentState->MaterialPasses[0]->Stages[1].AlphaOp		= D3DTOP_BLENDDIFFUSEALPHA;
		
		CurrentState->MaterialPasses[0]->Stages[2].ColorArg1	= D3DTA_DIFFUSE;
		CurrentState->MaterialPasses[0]->Stages[2].ColorArg2	= D3DTA_CURRENT;
		CurrentState->MaterialPasses[0]->Stages[2].ColorOp		= D3DTOP_MODULATE;
		CurrentState->MaterialPasses[0]->Stages[2].AlphaArg2	= D3DTA_CURRENT;
		CurrentState->MaterialPasses[0]->Stages[2].AlphaOp		= D3DTOP_SELECTARG2;

		if( BitmapMaterial )
		{
			SetShaderBitmap( CurrentState->MaterialPasses[0]->Stages[1], BitmapMaterial );
			ApplyTexModifier( CurrentState->MaterialPasses[0]->Stages[1], &TexModifier );
			CurrentState->MaterialPasses[0]->Stages[1].TexCoordIndex	= TCS_Stream1;
		}		
		
		StagesUsed = 3;
	}

	// Figure out how many projectors we can use.
	if( StagesUsed == 1 )
	{
		NumProjectors = Clamp<INT>(	NumProjectors, 
									0, 
									RenDev->DeviceCaps.MaxSimultaneousTextures - 1 
								);
	}
	else
	{
		NumProjectors = Clamp<INT>(	NumProjectors, 
									0, 
									Min(	RenDev->DeviceCaps.MaxTextureBlendStages - 3, 
											RenDev->DeviceCaps.MaxSimultaneousTextures - 2 
										)
								);
	}
	
	// Projectors.
	for( INT ProjIndex=0; ProjIndex<NumProjectors; ProjIndex++ )
	{
		UBitmapMaterial* ProjMaterial = ParticleMaterial->Projectors[ProjIndex].BitmapMaterial;
		FOpenGLTexture*	 ProjTexture  = ProjMaterial ? CacheTexture(ProjMaterial->Get(Viewport->CurrentTime,Viewport)->GetRenderInterface()) : NULL;

		if( ProjMaterial )
		{
			switch( ProjMaterial->UClampMode )
			{
			case TC_Wrap:	CurrentState->MaterialPasses[0]->Stages[1].TextureAddressU = D3DTADDRESS_WRAP;  break;
			case TC_Clamp:	CurrentState->MaterialPasses[0]->Stages[1].TextureAddressU = D3DTADDRESS_CLAMP; break;
			}
			switch( ProjMaterial->VClampMode )
			{
			case TC_Wrap:	CurrentState->MaterialPasses[0]->Stages[1].TextureAddressV = D3DTADDRESS_WRAP;  break;
			case TC_Clamp:	CurrentState->MaterialPasses[0]->Stages[1].TextureAddressV = D3DTADDRESS_CLAMP; break;
			}
		}

		CurrentState->MaterialPasses[0]->Stages[StagesUsed].ColorArg1	= D3DTA_TEXTURE;
		CurrentState->MaterialPasses[0]->Stages[StagesUsed].AlphaArg1	= D3DTA_TEXTURE;
		CurrentState->MaterialPasses[0]->Stages[StagesUsed].ColorArg2	= D3DTA_CURRENT;
		CurrentState->MaterialPasses[0]->Stages[StagesUsed].AlphaArg2	= D3DTA_CURRENT;
		CurrentState->MaterialPasses[0]->Stages[StagesUsed].AlphaOp	= D3DTOP_SELECTARG2;
		CurrentState->MaterialPasses[0]->Stages[StagesUsed].Texture	= ProjTexture;
		
		switch( ParticleMaterial->Projectors[ProjIndex].BlendMode )
		{
		case PB_AlphaBlend:
			CurrentState->MaterialPasses[0]->Stages[StagesUsed].ColorOp = D3DTOP_BLENDTEXTUREALPHA;
			break;
		case PB_Add:
			// There is not really a good way to handle this case.
			CurrentState->MaterialPasses[0]->Stages[StagesUsed].ColorOp = DUMMY_MODULATE2X;
			break;
		case PB_None:
		case PB_Modulate:
		default:
			CurrentState->MaterialPasses[0]->Stages[StagesUsed].ColorOp = D3DTOP_MODULATE;
		}

		//!!vogel: cut & pasted from cubemap code. Why are we relying on D3DX?!
		D3DXMATRIX	CameraToTexture, InvViewMatrix;
		D3DXMatrixInverse(&InvViewMatrix,NULL,(D3DXMATRIX*) &CurrentState->WorldToCamera);
		D3DXMatrixMultiply(&CameraToTexture,&InvViewMatrix,(D3DXMATRIX*)&ParticleMaterial->Projectors[ProjIndex].Matrix);		
		
		CurrentState->MaterialPasses[0]->Stages[StagesUsed].TextureTransformMatrix		= CameraToTexture;		
		CurrentState->MaterialPasses[0]->Stages[StagesUsed].TextureTransformsEnabled	= 1;
		CurrentState->MaterialPasses[0]->Stages[StagesUsed].TexCoordIndex				= D3DTSS_TCI_CAMERASPACEPOSITION;				
		CurrentState->MaterialPasses[0]->Stages[StagesUsed].TexCoordCount				= D3DTTFF_COUNT3;

		if( ParticleMaterial->Projectors[ProjIndex].Projected )
			CurrentState->MaterialPasses[0]->Stages[StagesUsed].TexCoordCount |= D3DTTFF_PROJECTED;

		StagesUsed++;
	}

	CurrentState->MaterialPasses[0]->ZWrite			= ParticleMaterial->ZWrite;
	CurrentState->MaterialPasses[0]->ZTest				= ParticleMaterial->ZTest;
	CurrentState->MaterialPasses[0]->TwoSided			= ParticleMaterial->RenderTwoSided;
	CurrentState->MaterialPasses[0]->AlphaTest			= ParticleMaterial->AlphaTest;
	CurrentState->MaterialPasses[0]->AlphaRef			= ParticleMaterial->AlphaRef;
	CurrentState->MaterialPasses[0]->FillMode			= ParticleMaterial->Wireframe ? FM_Wireframe : FM_Solid;

	
	CurrentState->MaterialPasses[PassesUsed]->StagesUsed	= StagesUsed;
	CurrentState->NumMaterialPasses			= ++PassesUsed;

	return 1;
	unguard;
}

/*----------------------------------------------------------------------------
	SetLightingOnlyMaterial.
----------------------------------------------------------------------------*/

void FOpenGLRenderInterface::SetLightingOnlyMaterial()
{
	guard(FOpenGLRenderInterface::SetLightingOnlyMaterial);
	INT StagesUsed = 0;
	INT PassesUsed = 0;

	HandleDiffuse_SP( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed++] );

	if( CurrentState->Lightmap )
		HandleLightmap_SP( CurrentState->MaterialPasses[PassesUsed]->Stages[StagesUsed++], CurrentState->Lightmap );

	CurrentState->MaterialPasses[PassesUsed]->StagesUsed = StagesUsed;
	StagesUsed = 0;
	PassesUsed++;
	CurrentState->NumMaterialPasses = PassesUsed;

	unguard;
}

/*----------------------------------------------------------------------------
	SetTerrainMaterial.
----------------------------------------------------------------------------*/

UBOOL FOpenGLRenderInterface::SetTerrainMaterial( UTerrainMaterial* TerrainMaterial, FOpenGLModifierInfo InModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial )
{
	guard(FOpenGLRenderInterface::SetTerrainMaterial);

	if( TerrainMaterial->RenderMethod == RM_AlphaMap )
	{
		UBitmapMaterial* BitmapMaterial;
		FOpenGLModifierInfo TexModifier;
		TexModifier.ModifyTextureTransforms = 1;
		TexModifier.Matrix = TerrainMaterial->Layers(0).TextureMatrix;
		TexModifier.TexCoordSource = TCS_WorldCoords;
		if( (BitmapMaterial = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, TerrainMaterial->Layers(0).Texture, &TexModifier )) != NULL )
		{
		// This is a regular bitmap material.
			FOpenGLMaterialStateStage* Stage = &CurrentState->MaterialPasses[0]->Stages[0];

		// Alpha-blend with the framebuffer, using AlphaWeight as an alphamap
		if( TerrainMaterial->FirstPass )
		{
				CurrentState->MaterialPasses[0]->SrcBlend			= D3DBLEND_ONE;
				CurrentState->MaterialPasses[0]->DestBlend			= D3DBLEND_ZERO;
				CurrentState->MaterialPasses[0]->AlphaBlending		= 0;
				CurrentState->MaterialPasses[0]->AlphaTest			= 0;
		}
		else
		{
				CurrentState->MaterialPasses[0]->SrcBlend			= D3DBLEND_SRCALPHA;
				CurrentState->MaterialPasses[0]->DestBlend			= D3DBLEND_INVSRCALPHA;
				CurrentState->MaterialPasses[0]->AlphaBlending		= 1;
				CurrentState->MaterialPasses[0]->AlphaTest			= 1;
		}
			CurrentState->MaterialPasses[0]->OverrideFogColor	= 0;
			CurrentState->MaterialPasses[0]->ZWrite			= 1;
			CurrentState->MaterialPasses[0]->ZTest				= 1;
			CurrentState->MaterialPasses[0]->TwoSided			= 0;
			CurrentState->MaterialPasses[0]->AlphaRef			= 0;


		// load the texture for the layer
		SetShaderBitmap( *Stage, BitmapMaterial );
		Stage->ColorArg1 = D3DTA_TEXTURE;
		Stage->ColorArg2 = D3DTA_DIFFUSE;
		Stage->AlphaArg1 = D3DTA_TEXTURE;
		Stage->ColorOp   = (CurrentState->UseStaticLighting || CurrentState->UseDynamicLighting) ? DUMMY_MODULATE2X : D3DTOP_SELECTARG1;
		Stage->AlphaOp   = D3DTOP_SELECTARG1;
		ApplyTexModifier( *Stage, &TexModifier );
		Stage++;

		// use the alphamap's alpha for the layer
		SetShaderBitmap( *Stage, TerrainMaterial->Layers(0).AlphaWeight );
		Stage->ColorArg1 = D3DTA_CURRENT;
		Stage->ColorOp   = D3DTOP_SELECTARG1;
		Stage->AlphaArg1 = D3DTA_TEXTURE;
			Stage->AlphaOp   = BitmapMaterial->IsTransparent() ? D3DTOP_MODULATE : D3DTOP_SELECTARG1 ;
			FOpenGLModifierInfo AlphaModifier;
		AlphaModifier.ModifyTextureTransforms = 1;
		AlphaModifier.TexCoordSource = TCS_Stream0;
		ApplyTexModifier( *Stage, &AlphaModifier );
		Stage++;

			CurrentState->MaterialPasses[0]->StagesUsed = Stage - &CurrentState->MaterialPasses[0]->Stages[0];
		}
		else
		{
			// This is a shader.  
			DECLARE_STATIC_UOBJECT( UTexMatrix, LayerTexCoordModifier, {} );	
			LayerTexCoordModifier->Material = TerrainMaterial->Layers(0).Texture;
			LayerTexCoordModifier->Matrix = TerrainMaterial->Layers(0).TextureMatrix;
			LayerTexCoordModifier->TexCoordSource = TCS_WorldCoords;

			DECLARE_STATIC_UOBJECT( UTexMatrix, AlphaTexCoordModifier, {} );
			AlphaTexCoordModifier->Material = TerrainMaterial->Layers(0).AlphaWeight;
			AlphaTexCoordModifier->Matrix = FMatrix::Identity;
			AlphaTexCoordModifier->TexCoordSource = TCS_Stream0;

			DECLARE_STATIC_UOBJECT( UOpacityModifier, OpacityModifier, { OpacityModifier->bOverrideTexModifier = 1; } );
			OpacityModifier->Material = LayerTexCoordModifier;
			OpacityModifier->Opacity = AlphaTexCoordModifier;

			UMaterial* InMaterial;
			if( TerrainMaterial->FirstPass )
				InMaterial = LayerTexCoordModifier;
			else
				InMaterial = OpacityModifier;

			FOpenGLModifierInfo ShaderModifierInfo;
			UShader* Shader;
			if( (Shader = CheckMaterial<UShader,MRH_Shader>(this, InMaterial, &ShaderModifierInfo )) != NULL )
			{
				if( !SetShaderMaterial( Shader, ShaderModifierInfo, ErrorString, ErrorMaterial ) )
					return 0;
			}
			else
			{
				if( ErrorString ) *ErrorString = TEXT("Terrain layers must be Shaders or a BitmapMaterials.");
				if( ErrorMaterial ) *ErrorMaterial = TerrainMaterial->Layers(0).Texture;
				return 0;				
			}
		}
	}
	else
	{
		FOpenGLMaterialStateStage* Stage = &CurrentState->MaterialPasses[0]->Stages[0];

		// Add to the framebuffer, using AlphaWeight as a weightmap

		// Set framebuffer blending.
		CurrentState->MaterialPasses[0]->SrcBlend		= D3DBLEND_ONE;
		if( TerrainMaterial->FirstPass )
		{
			CurrentState->MaterialPasses[0]->DestBlend			= D3DBLEND_ZERO;
			CurrentState->MaterialPasses[0]->OverrideFogColor	= 0;
			CurrentState->MaterialPasses[0]->ZWrite			= 1;
			CurrentState->MaterialPasses[0]->AlphaBlending		= 0;
		}
		else
		{
			CurrentState->MaterialPasses[0]->DestBlend			= D3DBLEND_ONE;
			CurrentState->MaterialPasses[0]->OverrideFogColor	= 1;
			CurrentState->MaterialPasses[0]->OverriddenFogColor = FColor( 0, 0, 0, 0 );
			CurrentState->MaterialPasses[0]->ZWrite			= 0;
			CurrentState->MaterialPasses[0]->AlphaBlending		= 1;
		}
		CurrentState->MaterialPasses[0]->AlphaTest		= 0;
		CurrentState->MaterialPasses[0]->ZTest			= 1;
		CurrentState->MaterialPasses[0]->TwoSided		= 0;
		CurrentState->MaterialPasses[0]->AlphaRef		= 0;

		// We're rendering with a combined weightmap and a pixelshader
		if( TerrainMaterial->RenderMethod == RM_CombinedWeightMap )
		{
			FOpenGLModifierInfo TexModifier, WeightMapModifier;

			SetShaderBitmap( *Stage, TerrainMaterial->Layers(0).AlphaWeight );
			WeightMapModifier.ModifyTextureTransforms = 1;
			WeightMapModifier.TexCoordSource = TCS_Stream0;
			ApplyTexModifier( *Stage, &WeightMapModifier );
			Stage++;

			TexModifier.ModifyTextureTransforms = 1;
			TexModifier.Matrix = TerrainMaterial->Layers(0).TextureMatrix;
			TexModifier.TexCoordSource = TCS_WorldCoords;
			SetShaderBitmap( *Stage, CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, TerrainMaterial->Layers(0).Texture, &TexModifier ) );
			ApplyTexModifier( *Stage, &TexModifier );
			Stage++;
			
			if( TerrainMaterial->Layers.Num() > 1 )
			{
				TexModifier = FOpenGLModifierInfo();
				TexModifier.ModifyTextureTransforms = 1;
				TexModifier.Matrix = TerrainMaterial->Layers(1).TextureMatrix;
				TexModifier.TexCoordSource = TCS_WorldCoords;
				SetShaderBitmap( *Stage, CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, TerrainMaterial->Layers(1).Texture, &TexModifier ) );
				ApplyTexModifier( *Stage, &TexModifier );
				Stage++;

				if( TerrainMaterial->Layers.Num() > 2 )
				{
					TexModifier = FOpenGLModifierInfo();
					TexModifier.ModifyTextureTransforms = 1;
					TexModifier.Matrix = TerrainMaterial->Layers(2).TextureMatrix;
					TexModifier.TexCoordSource = TCS_WorldCoords;
					SetShaderBitmap( *Stage, CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, TerrainMaterial->Layers(2).Texture, &TexModifier ) );
					ApplyTexModifier( *Stage, &TexModifier );
					Stage++;

					if( TerrainMaterial->Layers.Num() > 3 )
					{
						TexModifier = FOpenGLModifierInfo();
						TexModifier.ModifyTextureTransforms = 1;
						TexModifier.Matrix = TerrainMaterial->Layers(3).TextureMatrix;
						TexModifier.TexCoordSource = TCS_WorldCoords;
						SetShaderBitmap( *Stage, CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, TerrainMaterial->Layers(3).Texture, &TexModifier ) );
						ApplyTexModifier( *Stage, &TexModifier );
						Stage++;
					}
				}
			}

			switch( TerrainMaterial->Layers.Num() ) 
			{
			case 3:
				{
				CurrentState->MaterialPasses[0]->PixelShader = PS_Terrain3Layer;
#ifdef _XBOX
				static FLOAT MaskCoefficients[3*4] = {	1.f, 0.f, 0.f, 0.f,	
															0.f, 1.f, 0.f, 0.f,	
															0.f, 0.f, 1.f, 0.f };
				RenDev->DeferredState.SetPixelShaderConstant( 0, MaskCoefficients, 3 );
#endif
				}
				break;
			case 4:
				CurrentState->MaterialPasses[0]->PixelShader = PS_Terrain4Layer;
				break;
			default:
				appErrorf(TEXT("too many layers at once"));
			}
		}
		else
		{
			FOpenGLModifierInfo TexModifier, WeightMapModifier;
			
			//!!powervr_aaron: KYRO does not support D3DTA_TEMP, and it takes an extra stage to blend in diffuse without it.
			if( !(RenDev->DeviceCaps.PrimitiveMiscCaps & D3DPMISCCAPS_TSSARGTEMP) )
			{
				// load the texture for the first layer
				Stage->ColorArg1 = D3DTA_TEXTURE;
				Stage->ColorArg2 = D3DTA_DIFFUSE;
				Stage->ColorOp	 = TerrainMaterial->Layers.Num() == 1 ? DUMMY_MODULATE2X : D3DTOP_SELECTARG1;
				Stage->AlphaArg1 = D3DTA_DIFFUSE;
				Stage->AlphaOp   = D3DTOP_SELECTARG1;
				TexModifier.ModifyTextureTransforms = 1;
				TexModifier.Matrix = TerrainMaterial->Layers(0).TextureMatrix;
				TexModifier.TexCoordSource = TCS_WorldCoords;
				SetShaderBitmap( *Stage, CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, TerrainMaterial->Layers(0).Texture, &TexModifier ) );
				ApplyTexModifier( *Stage, &TexModifier );
				Stage++;

				// multiply by the weightmap
				SetShaderBitmap( *Stage, TerrainMaterial->Layers(0).AlphaWeight );
				Stage->ColorArg1 = D3DTA_CURRENT;
				Stage->ColorArg2 = D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE;
				Stage->ColorOp   = D3DTOP_MODULATE;
				Stage->AlphaArg1 = D3DTA_CURRENT;
				Stage->AlphaOp   = D3DTOP_SELECTARG1;
				WeightMapModifier.ModifyTextureTransforms = 1;
				WeightMapModifier.TexCoordSource = TCS_Stream0;
				ApplyTexModifier( *Stage, &WeightMapModifier );
				Stage++;

				if( TerrainMaterial->Layers.Num() > 1 )
				{
					// Render each subsequent layer.
					for( INT l=1;l<TerrainMaterial->Layers.Num();l++ )
					{
						// load the weightmap to alpha
						Stage->ColorArg1 = D3DTA_CURRENT;
						Stage->ColorOp   = D3DTOP_SELECTARG1;
						Stage->AlphaArg1 = D3DTA_TEXTURE;
						Stage->AlphaOp   = D3DTOP_SELECTARG1;
						WeightMapModifier.ModifyTextureTransforms = 1;
						WeightMapModifier.TexCoordSource = TCS_Stream0;
						SetShaderBitmap( *Stage, TerrainMaterial->Layers(l).AlphaWeight );
						ApplyTexModifier( *Stage, &WeightMapModifier );
						Stage++;

						// current += alpha * texture
						SetShaderBitmap( *Stage, CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, TerrainMaterial->Layers(l).Texture, &TexModifier ) );
						Stage->ColorArg1 = D3DTA_CURRENT;
						Stage->ColorArg2 = D3DTA_TEXTURE;
						Stage->ColorOp	 = D3DTOP_MODULATEALPHA_ADDCOLOR;
						Stage->AlphaArg1 = D3DTA_CURRENT;
						Stage->AlphaOp   = D3DTOP_SELECTARG1;
						TexModifier = FOpenGLModifierInfo();
						TexModifier.ModifyTextureTransforms = 1;
						TexModifier.Matrix = TerrainMaterial->Layers(l).TextureMatrix;
						TexModifier.TexCoordSource = TCS_WorldCoords;
						ApplyTexModifier( *Stage, &TexModifier );
						Stage++;
					}

					// modulate landscape by vertex diffuse colours
					Stage->ColorArg1 = D3DTA_DIFFUSE;
					Stage->ColorArg2 = D3DTA_CURRENT;
					Stage->ColorOp	 = DUMMY_MODULATE2X;
					Stage->AlphaArg1 = D3DTA_CURRENT;
					Stage->AlphaOp   = D3DTOP_SELECTARG1;
					Stage->TexCoordIndex = 0;
					Stage->TextureTransformsEnabled = 0;
					Stage++;
				}
			}
			else
			{
			// load the texture for the first layer
			Stage->ColorArg1 = D3DTA_TEXTURE;
			Stage->ColorArg2 = D3DTA_DIFFUSE;
			Stage->ColorOp   = DUMMY_MODULATE2X;
			TexModifier.ModifyTextureTransforms = 1;
			TexModifier.Matrix = TerrainMaterial->Layers(0).TextureMatrix;
			TexModifier.TexCoordSource = TCS_WorldCoords;
			SetShaderBitmap( *Stage, CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, TerrainMaterial->Layers(0).Texture, &TexModifier ) );
			ApplyTexModifier( *Stage, &TexModifier );
			Stage++;

			// multiply the weightmap and save it in the temp register
			SetShaderBitmap( *Stage, TerrainMaterial->Layers(0).AlphaWeight );
			Stage->ColorArg1 = D3DTA_CURRENT;
			Stage->ColorArg2 = D3DTA_TEXTURE;
			Stage->ColorOp   = D3DTOP_MODULATE;
			Stage->ResultArg = TerrainMaterial->Layers.Num() == 1 ? D3DTA_CURRENT : D3DTA_TEMP;
			WeightMapModifier.ModifyTextureTransforms = 1;
			WeightMapModifier.TexCoordSource = TCS_Stream0;
			ApplyTexModifier( *Stage, &WeightMapModifier );
			Stage++;

			// Render each subsequent layer.
			for( INT l=1;l<TerrainMaterial->Layers.Num();l++ )
			{
				// load the texture
				Stage->ColorArg1 = D3DTA_TEXTURE;
				Stage->ColorArg2 = D3DTA_DIFFUSE;
				Stage->ColorOp   = DUMMY_MODULATE2X;
				TexModifier = FOpenGLModifierInfo();
				TexModifier.ModifyTextureTransforms = 1;
				TexModifier.Matrix = TerrainMaterial->Layers(l).TextureMatrix;
				TexModifier.TexCoordSource = TCS_WorldCoords;
				SetShaderBitmap( *Stage, CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, TerrainMaterial->Layers(l).Texture, &TexModifier ) );
				ApplyTexModifier( *Stage, &TexModifier );
				Stage++;

				// multiply the weightmap and add to the temporary register
				SetShaderBitmap( *Stage, TerrainMaterial->Layers(l).AlphaWeight );
				Stage->ColorArg1 = D3DTA_TEXTURE;
				Stage->ColorArg2 = D3DTA_CURRENT;
				Stage->ColorArg0 = D3DTA_TEMP;
				Stage->ColorOp	 = D3DTOP_MULTIPLYADD;
				Stage->ResultArg = l == TerrainMaterial->Layers.Num()-1 ? D3DTA_CURRENT : D3DTA_TEMP;
				WeightMapModifier.ModifyTextureTransforms = 1;
				WeightMapModifier.TexCoordSource = TCS_Stream0;
				ApplyTexModifier( *Stage, &WeightMapModifier );
				Stage++;
			}
			}
		}
		CurrentState->MaterialPasses[0]->StagesUsed = Stage - &CurrentState->MaterialPasses[0]->Stages[0];
	}
	CurrentState->NumMaterialPasses	= 1;
	return 1;
	unguard;
}

/*----------------------------------------------------------------------------
	SetProjectorMaterial.
----------------------------------------------------------------------------*/

//
//	FProjectorCubeMapFace
//

class FProjectorCubeMapFace : public FTexture
{
public:

	FVector	Base,
			FaceX,
			FaceY;
	INT		Size;

	// Constructor.

	FProjectorCubeMapFace(FVector InBase,FVector InFaceX,FVector InFaceY,INT InSize)
	{
		Base = InBase;
		FaceX = InFaceX;
		FaceY = InFaceY;
		Size = InSize;
	}

	// FRenderResource interface.

	virtual INT GetRevision() { return 1; }

	// FBaseTexture interface.

	virtual INT GetWidth() { return Size; }
	virtual INT GetHeight() { return Size; }
	virtual INT GetFirstMip() { return 0; }
	virtual INT GetNumMips() { return 2; }
	virtual ETextureFormat GetFormat() { return TEXF_RGBA8; }

	virtual ETexClampMode GetUClamp() { return TC_Clamp; }
	virtual ETexClampMode GetVClamp() { return TC_Clamp; }

	// FTexture interface

	virtual void* GetRawTextureData(INT MipIndex) { return NULL; }
	virtual void UnloadRawTextureData(INT MipIndex) {}

	void GetTextureData(INT MipIndex,void* Dest,INT DestStride,ETextureFormat DestFormat,UBOOL ColoredMips)
	{
		guard(FSpecularCubeMapFace::GetTextureData);

		check(DestFormat == TEXF_RGBA8);

		INT		MipSize = Size >> MipIndex;
		FVector	DiffX = FaceX / (MipSize - 1),
				DiffY = FaceY / (MipSize - 1);

		for(INT Y = 0;Y < MipSize;Y++)
		{
			FColor*	DestPtr = (FColor*) ((BYTE*)Dest + Y * DestStride);
			FVector	Vector = Base + DiffY * (Y + 0.5f) + DiffX * 0.5f;

			for(INT X = 0;X < MipSize;X++)
			{
				FVector	Normal = Vector.SafeNormal();

				*DestPtr++ = FColor(
								Clamp(appFloor(Normal.Z * 255.0f),0,255),
								Clamp(appFloor(Normal.Z * 255.0f),0,255),
								Clamp(appFloor(Normal.Z * 255.0f),0,255),
								Clamp(appFloor(Normal.Z * 255.0f),0,255)
								);

				Vector += DiffX;
			}
		}

		unguard;
	}

	virtual UTexture* GetUTexture() { return NULL; }
};

//
//	FProjectorCubeMap
//

class FProjectorCubeMap : public FCubemap
{
public:

	FProjectorCubeMapFace	PositiveX,
							NegativeX,
							PositiveY,
							NegativeY,
							PositiveZ,
							NegativeZ;

	INT		Size;
	INT		Revision;

	// Constructor.

	FProjectorCubeMap(INT InSize) :
		PositiveX(FVector(+1,+1,+1),FVector(+0,+0,-2),FVector(+0,-2,+0),InSize),
		NegativeX(FVector(-1,+1,-1),FVector(+0,+0,+2),FVector(+0,-2,+0),InSize),
		PositiveY(FVector(-1,+1,-1),FVector(+2,+0,+0),FVector(+0,+0,+2),InSize),
		NegativeY(FVector(-1,-1,+1),FVector(+2,+0,+0),FVector(+0,+0,-2),InSize),
		PositiveZ(FVector(-1,+1,+1),FVector(+2,+0,+0),FVector(+0,-2,+0),InSize),
		NegativeZ(FVector(+1,+1,-1),FVector(-2,+0,+0),FVector(+0,-2,+0),InSize)
	{
		Size = InSize;
		Revision = 1;
	}

	// FRenderResource interface.

	virtual INT GetRevision() { return Revision; }

	// FBaseTexture interface.

	virtual INT GetWidth() { return Size; }
	virtual INT GetHeight() { return Size; }
	virtual INT GetFirstMip() { return 0; }
	virtual INT GetNumMips() { return 2; }
	virtual ETextureFormat GetFormat() { return TEXF_RGBA8; }

	virtual ETexClampMode GetUClamp() { return TC_Clamp; }
	virtual ETexClampMode GetVClamp() { return TC_Clamp; }

	// FCubeMap interface.

	virtual FTexture* GetFace(INT FaceIndex)
	{
		switch(FaceIndex)
		{
		case 0:
			return &PositiveX;
		case 1:
			return &NegativeX;
		case 2:
			return &PositiveY;
		case 3:
			return &NegativeY;
		case 4:
			return &PositiveZ;
		case 5:
			return &NegativeZ;
		default:	
			return NULL;
		};
	}
};

/* scion ======================================================================
* FOpenGLRenderInterface::SetProjectorMultiMaterial
* Author: sz
*
* Takes a list of projectors and renders groups of 3 in a single pass while
* trying to mimic the single pass (SetProjectorMaterial) functionality as
* closely as possible.
*
* ============================================================================
*/
UBOOL FOpenGLRenderInterface::SetProjectorMultiMaterial( UProjectorMultiMaterial* ProjectorMultiMaterial, FOpenGLModifierInfo InModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial )
{
	guard(FOpenGLRenderInterface::SetProjectorMultiMaterial);
#ifdef _XBOX

	if( !ProjectorMultiMaterial->RenderEntries.Num() )
		return 0;

	// use pixel shader texkill to clip projected area
	UBOOL bUseClipPlanes = ProjectorMultiMaterial->bFrustumClipPixels;

	// extract the opacity from the base material
	// this can be both a texture and/or a color modifier	
	UMaterial*			BaseMaterial = ProjectorMultiMaterial->BaseMaterial;
	UTexture*			BaseOpacityTexture=NULL;
	UBOOL				BaseTwoSided=0;
	FOpenGLModifierInfo	BaseModifierInfo;
	FLOAT				PSConstModulateColor[4] = {1.f, 1.f, 1.f, 1.f };

	if( BaseMaterial )
	{
		UShader*			Shader;
		UTexture*			BitmapMaterial;

		// get opacity texture from shader
		if( (Shader=CheckMaterial<UShader,MRH_Shader>(this, BaseMaterial, &BaseModifierInfo)) != NULL )
		{
			BaseOpacityTexture = MaterialCast<UTexture,MRH_Texture>( Shader->Opacity );
			BaseTwoSided = Shader->TwoSided;
		}
		// get opacity texture from base texture
		else if( (BitmapMaterial=CheckMaterial<UTexture,MRH_Texture>(this, BaseMaterial, &BaseModifierInfo)) != NULL )
		{
			if( BitmapMaterial->bMasked || BitmapMaterial->bAlphaTexture )
				BaseOpacityTexture = BitmapMaterial;
		}		
		// see if any ColorModifiers were used
		BaseTwoSided |= BaseModifierInfo.TwoSided;
		if( BaseModifierInfo.ModifyColor )
		{
			PSConstModulateColor[0] = BaseModifierInfo.TFactorColor.R * ONE_OVER_255;
			PSConstModulateColor[1] = BaseModifierInfo.TFactorColor.G * ONE_OVER_255;
			PSConstModulateColor[2] = BaseModifierInfo.TFactorColor.B * ONE_OVER_255;
			PSConstModulateColor[3] = BaseModifierInfo.TFactorColor.A * ONE_OVER_255;
		}
	}

	D3DXMATRIX	InvViewMatrix,
				CameraToTexture,
				LinearConvert;

	D3DXMatrixIdentity( &LinearConvert );

	// Calculate the inverse of the current WorldToCamera (View) matrix.
	D3DXMatrixInverse( &InvViewMatrix, NULL, (D3DXMATRIX*)&CurrentState->WorldToCamera );

	D3DXVECTOR4 ProjectorConstants[] = 
	{	
		// Projector directions in local space
		D3DXVECTOR4(0.f,0.f,0.f,0.f),
		D3DXVECTOR4(0.f,0.f,0.f,0.f),
		D3DXVECTOR4(0.f,0.f,0.f,0.f),
		// The array of projector origins in local space
		D3DXVECTOR4(0.f,0.f,0.f,1.f),
		D3DXVECTOR4(0.f,0.f,0.f,1.f),
		D3DXVECTOR4(0.f,0.f,0.f,1.f),
		// Project on back faces constants
		D3DXVECTOR4(0.f,0.f,0.f,0.f),
		// The projector inverse max trace distance
		D3DXVECTOR4(0.f,0.f,0.f,1.f),
		// frustum planes [0..4]
		D3DXVECTOR4(0.f,0.f,0.f,1.f),
		D3DXVECTOR4(0.f,0.f,0.f,1.f),
		D3DXVECTOR4(0.f,0.f,0.f,1.f),
		D3DXVECTOR4(0.f,0.f,0.f,1.f),
		// lifetime attenuations
		D3DXVECTOR4(1.f,1.f,1.f,1.f)
	};
	const INT NumConstants = 13;
	// Get a pointer to where we are going to update projector directions
    D3DXVECTOR4* LocalProjDirs = &ProjectorConstants[0];
	// Get a pointer to where we are going to update origins
	D3DXVECTOR4* LocalProjOrigins = &ProjectorConstants[3];
	// Get a pointer to where we can update the back face culling flags
	FLOAT* ProjBackfaces = (FLOAT*)&ProjectorConstants[6];
	// Get a pointer to where we are going to update invmaxtracedist
	FLOAT* ProjTraceDists = (FLOAT*)&ProjectorConstants[7];
	// Get a pointer to where we are going to update frustum planes
	D3DXVECTOR4* LocalFrustumPlanes = &ProjectorConstants[8];
	// lifespan attenuation
	FLOAT* ProjLifespanAttens = (FLOAT*)&ProjectorConstants[12];

	// last stage is reserved for clip rejection tex coords if using clip planes
	// also we only want to support a single projector if bUseClipPlanes 
	// so MaxStages=2 (one for opacity texture, one for projector texture)
	INT MaxStages = (bUseClipPlanes) ? 2 : 4;

	checkSlow( ProjectorMultiMaterial->RenderEntries.Num() );
	// number of passes required to render all the projectors in this ProjectorMultiMAterial
	INT NumProjPasses = (ProjectorMultiMaterial->RenderEntries.Num()-1) / (MaxStages-1) + 1;
	// keep track of the last projector that was used for rendering
	INT LastProjectorUsed = -1;

	// since the vertex shader constants are NOT a part of the current material
	// state, we can not have multiple passes since vertex shader constants would 
	// not be restored properly
	NumProjPasses = 1;

	for( INT Pass=0; Pass < NumProjPasses; Pass++ )
	{
		FOpenGLMaterialState*	MaterialState = CurrentState->MaterialPasses[Pass];
		INT					StagesUsed = 0;		
		QWORD				VSEnum = VS_NotBoned;

		// always disable zwrites for projectors
		// since they shouldn't be occluding anything
		MaterialState->ZWrite = 0;

		if( BaseOpacityTexture )
		{
			// set the base material opacity texture
			SetShaderBitmap( MaterialState->Stages[StagesUsed], BaseOpacityTexture );
			ApplyTexModifier( MaterialState->Stages[StagesUsed], &BaseModifierInfo );
			StagesUsed++;
		}
		else
		{
			// set default texture if the base material had no opacity
			// (possibly create more pixel shaders to handle this case so we don't waste a stage)
			SetShaderBitmap( MaterialState->Stages[StagesUsed], ProjectorMultiMaterial->DefaultOpacity );
			StagesUsed++;		
		}

		D3DMATRIX* pWorldInvMat = RenDev->DeferredState.GetTransform(TS_WORLDINV);
		D3DMATRIX WorldInvTransposeMat;
		D3DXMatrixTranspose( (D3DXMATRIX*)&WorldInvTransposeMat, (D3DXMATRIX*)RenDev->DeferredState.GetTransform(TS_WORLD) );
 
		// iterate through projectors and set their texture stages
		for( INT ProjIndex=LastProjectorUsed+1; ProjIndex < ProjectorMultiMaterial->RenderEntries.Num() && StagesUsed < MaxStages; ProjIndex++ )
		{
			FProjectorRenderInfo* ProjectorInfo = ProjectorMultiMaterial->RenderEntries( ProjIndex );
			checkSlow( ProjectorInfo );
			AProjector* Projector = ProjectorInfo->Projector;
			checkSlow( Projector );

			// See if the projected texture is simple.  
			// If it's not then skip it.
			FOpenGLModifierInfo	ProjectedModifier;
			UBitmapMaterial*	ProjectedBitmap = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>( this, ProjectorInfo->Material, &ProjectedModifier );
			if( !ProjectedBitmap )
			{
				ProjectedBitmap = ProjectorInfo->GetProjectedBitmap();
			}
			if( ProjectedBitmap )
			{
				FOpenGLMaterialStateStage& StageState = MaterialState->Stages[StagesUsed];
				
				// set texture
				SetShaderBitmap( StageState, ProjectedBitmap );				

				// projection direction vector in world space
				FVector WorldProjDir(ProjectorInfo->FrustumPlanes[0]);
				// should already be normalized
				//WorldProjDir.Normalize();

				// set the projector direction vector (in local object space)
				// the direction vector is the same as the normal for the near 
				// frustum plane of the projector
				D3DXVec3TransformNormal( (D3DXVECTOR3*)&LocalProjDirs[StagesUsed-1], 
					(D3DXVECTOR3*)&WorldProjDir, 
					(D3DXMATRIX*)&WorldInvTransposeMat );
				D3DXVec3Normalize( (D3DXVECTOR3*)&LocalProjDirs[StagesUsed-1], (D3DXVECTOR3*)&LocalProjDirs[StagesUsed-1] );

				// Transform the projector's origin to local space of the mesh
				D3DXVec3Transform((D3DXVECTOR4*)&LocalProjOrigins[StagesUsed-1], 
					(D3DXVECTOR3*)&Projector->Location, 
					(D3DXMATRIX*)pWorldInvMat );

				// Set the back face flag
				ProjBackfaces[StagesUsed-1] = Projector->bProjectOnBackfaces ? 1.f : 0.f;

				// the max trace distance must also be transformed so that it makes sense in local space				
				FVector WorldProjMaxLoc = (WorldProjDir*Projector->MaxTraceDistance*Projector->DistanceAttenScale) + Projector->Location;
				D3DXVECTOR4 LocalProjMaxLoc;
				D3DXVec3Transform((D3DXVECTOR4*)&LocalProjMaxLoc, 
					(D3DXVECTOR3*)&WorldProjMaxLoc, 
					(D3DXMATRIX*)pWorldInvMat );
				D3DXVec3Subtract( (D3DXVECTOR3*)&LocalProjMaxLoc, 
					(D3DXVECTOR3*)&LocalProjMaxLoc,
					(D3DXVECTOR3*)&LocalProjOrigins[StagesUsed-1] );
                FLOAT LocalMaxTraceDist = Max<FLOAT>( 0.1f, D3DXVec3Length( (D3DXVECTOR3*)&LocalProjMaxLoc ) );

				// Set the inv max trace distance
				ProjTraceDists[StagesUsed-1] = 1.f / LocalMaxTraceDist;

				// attenuation based on lifespan (only fades during last second)
				if( ProjectorInfo->Expires > 0.0 )
				{
					ProjLifespanAttens[StagesUsed-1] = Clamp<FLOAT>(ProjectorInfo->Expires - ProjectorInfo->LastRenderTime,0.0f,1.0f);
				}

				// Calculate the camera to projected texture matrix
				// Premultiply modifier transform with the camera to projected matrix
				// use camera space position texgen mode
				// make sure our coordinates are set to projected
				D3DXMatrixMultiply( &CameraToTexture, &InvViewMatrix, (D3DXMATRIX*)&ProjectorInfo->Matrix );
				D3DXMatrixMultiply( (D3DXMATRIX*)&StageState.TextureTransformMatrix, &CameraToTexture, (D3DXMATRIX*)&ProjectedModifier.Matrix );
				StageState.TexCoordCount = D3DTTFF_COUNT3|D3DTTFF_PROJECTED;
				StageState.TexCoordIndex = D3DTSS_TCI_CAMERASPACEPOSITION;
				StageState.TextureTransformsEnabled = 1;

				// xbox linear texture uv translation (same as in FOpenGLRenderInterface::ApplyTexModifier)
				if( IsTextureLinear(StageState.Texture) )
				{
					LinearConvert._11 = StageState.Texture->CachedWidth;
					LinearConvert._22 = StageState.Texture->CachedHeight;

					D3DXMatrixMultiply((D3DXMATRIX*)&StageState.TextureTransformMatrix,
						(D3DXMATRIX*)&StageState.TextureTransformMatrix,
						(D3DXMATRIX*)&LinearConvert);
				}
				StagesUsed++;		
			}
			else
			{
				debugf( NAME_Error, TEXT("Invalid Material [%s] on projector [%s]") );
			}
			LastProjectorUsed++;
		}

		// number of stages used including base opacity
		MaterialState->StagesUsed = StagesUsed;

		// Use proper vertex shader fragment for attenuation
		if( bUseClipPlanes )
		{
			// scion sz -

			// if we are using clip planes we need the vertex shader that passes the frustum clip
			// results thru stage 3
			VS_SETUNIQUESHADER( VS_Projector_Atten_Clip, MaterialState->MaterialVertexShaderEnum );

			// we can only support clip planes from one projector at a time
			// so we use the first one.
			FProjectorRenderInfo* ProjectorInfo = ProjectorMultiMaterial->RenderEntries( 0 );
			checkSlow( ProjectorInfo );

			// set the frustum planes (in local-space)
			for( INT Plane=0; Plane < 4; Plane++ )
			{
				// transform each plane with the inverse world matrix
				D3DXVec3TransformNormal( (D3DXVECTOR3*)&LocalFrustumPlanes[Plane], 
					(D3DXVECTOR3*)&ProjectorInfo->FrustumPlanes[Plane+1], 
					(D3DXMATRIX*)&WorldInvTransposeMat );

				// transform frustum plane origin to local-space
				D3DXVECTOR4 LocalPlaneOrigin( 0.f,0.f,0.f,1.f );
				D3DXVec3Transform( (D3DXVECTOR4*)&LocalPlaneOrigin, 
					(D3DXVECTOR3*)&ProjectorInfo->Projector->FrustumVertices[Plane], 
					(D3DXMATRIX*)pWorldInvMat );

				// recompute the -D for this plane since we went from world-space to local-space
				LocalFrustumPlanes[Plane].w = -D3DXVec3Dot( 
					(D3DXVECTOR3*)&LocalFrustumPlanes[Plane], 
					(D3DXVECTOR3*)&LocalPlaneOrigin );
	
			}
		}
		else
		{
			VS_SETUNIQUESHADER( VS_Projector_Atten, MaterialState->MaterialVertexShaderEnum );
		}

		// Set vertex shader constants (directions, origins, trace dists, backfaces in one copy)
		RenDev->SetVertexShaderConstant( VSC_PROJ_DIRECTIONS, ProjectorConstants, NumConstants );

		// make sure specular channel is enabled since we pass attenuation factors
		// using both the diffuse and the specular output colors
		MaterialState->SpecularEnable = 1;

		// other states
		MaterialState->TwoSided = BaseTwoSided;
		MaterialState->AlphaBlending = 1;
		MaterialState->ZWrite = 0;
		MaterialState->AlphaTest = 1;
		MaterialState->AlphaRef = 0;

		switch( ProjectorMultiMaterial->FrameBufferBlending )
		{
		case PB_Add:
			{
				// set alpha blending mode
				MaterialState->SrcBlend = D3DBLEND_ONE;
				MaterialState->DestBlend = D3DBLEND_ONE;

				// set pixel shader based on number of stages used
				checkSlow( StagesUsed <= MaxStages && StagesUsed >= 2 );
				if( bUseClipPlanes )
				{
					MaterialState->PixelShader = PS_Projector_Add_Clip;
				}
				else
				{
					MaterialState->PixelShader = (EPixelShader)(PS_Projector_Add_1 + (StagesUsed-2));
				}
			}
			break;
		case PB_AlphaBlend:
			{
				// set alpha blending mode
				MaterialState->SrcBlend = D3DBLEND_SRCALPHA;
				MaterialState->DestBlend = D3DBLEND_INVSRCALPHA;

				// set pixel shader based on number of stages used
				checkSlow( StagesUsed <= MaxStages && StagesUsed >= 2 );
				if( bUseClipPlanes )
				{
					MaterialState->PixelShader = PS_Projector_AlphaBlend_Clip;
				}
				else
				{
					MaterialState->PixelShader = (EPixelShader)(PS_Projector_AlphaBlend_1 + (StagesUsed-2));
				}
			}
			break;
		case PB_None:
		case PB_Modulate:
			{
				// set alpha blending mode
				MaterialState->SrcBlend = D3DBLEND_DESTCOLOR;
				MaterialState->DestBlend = D3DBLEND_SRCCOLOR;

				// set pixel shader based on number of stages used
				checkSlow( StagesUsed <= MaxStages && StagesUsed >= 2 );
				if( bUseClipPlanes )
				{
					MaterialState->PixelShader = PS_Projector_Modulate_Clip;
				}
				else
				{
					MaterialState->PixelShader = (EPixelShader)(PS_Projector_Modulate_1 + (StagesUsed-2));
				}

				// set pixel shader constant[0]
				static FLOAT flColor[] = { 127.f * ONE_OVER_255, 127.f * ONE_OVER_255, 127.f * ONE_OVER_255, 0.f };
				RenDev->DeferredState.SetPixelShaderConstant( 0, flColor, 1 );

			}
			break;
		default:
			{
				if(ErrorString)
					*ErrorString = TEXT("Invalid FrameBufferBlending mode");
				return 0;
			}
		};

		// setup for a new pass
		if( Pass < (NumProjPasses-1) )
			CurrentState->MaterialPasses[Pass+1] = MaterialStatePool.AllocateState( &DefaultPass );
	}
	
	CurrentState->NumMaterialPasses = NumProjPasses;

#endif
	return 1;
	unguard;
}

UBOOL FOpenGLRenderInterface::SetProjectorMaterial( UProjectorMaterial* ProjectorMaterial, FOpenGLModifierInfo InModifierInfo, FString* ErrorString, UMaterial** ErrorMaterial )
{
	guard(FOpenGLRenderInterface::SetProjectorMaterial);

	FOpenGLMaterialState*	MaterialState = CurrentState->MaterialPasses[0];
	INT					PassesUsed = 0,
						StagesUsed = 0,
						FreeStagesUsed = 0,
						MaxTextureStages = RenDev->DeviceCaps.MaxSimultaneousTextures,
						MaxBlendStages   = RenDev->DeviceCaps.MaxTextureBlendStages;

	// See if we have an alpha channel in the BaseMaterial.

	UShader*	BaseShader = Cast<UShader>(ProjectorMaterial->BaseMaterial);
	UTexture*	BaseTexture = Cast<UTexture>(ProjectorMaterial->BaseMaterial);	
	UMaterial*	BaseDiffuse = ProjectorMaterial->BaseMaterial;
	UMaterial*	BaseOpacity = NULL;

	if(BaseTexture && (BaseTexture->bMasked || BaseTexture->bAlphaTexture))
		BaseOpacity = BaseTexture;
	else if(BaseShader)
	{
		BaseDiffuse = BaseShader->Diffuse;
		BaseOpacity = BaseShader->Opacity;
		MaterialState->TwoSided = BaseShader->TwoSided;
	}

	if(ProjectorMaterial->bStaticProjector)
	{
		// See if the projected texture is simple.

		FOpenGLModifierInfo	ProjectedModifier;
		UBitmapMaterial*	ProjectedBitmap = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this,ProjectorMaterial->Projected,&ProjectedModifier);

		if(ProjectedBitmap)
		{
			// Handle the base material.

			if(ProjectorMaterial->BaseMaterialBlending != PB_None && BaseDiffuse)
			{
				FOpenGLModifierInfo BaseModifier			= InModifierInfo;
				BaseModifier.TexCoordSource				= TCS_Stream1;
				BaseModifier.ModifyTextureTransforms	= 1;
				if(!HandleCombinedMaterial(BaseDiffuse,PassesUsed,StagesUsed,FreeStagesUsed,BaseModifier,0,ErrorString,ErrorMaterial))
					return 0;
			}

			if(StagesUsed < MaxTextureStages + FreeStagesUsed)
			{
				SetShaderBitmap(MaterialState->Stages[StagesUsed],ProjectedBitmap);
				ProjectedModifier.ModifyTextureTransforms	= 1;
				ProjectedModifier.TexCoordCount				= TCN_3DCoords;
				ProjectedModifier.TexCoordProjected			= 1;
				ProjectedModifier.TexCoordSource			= TCS_Stream0;
				ApplyTexModifier(MaterialState->Stages[StagesUsed],&ProjectedModifier);

				switch(ProjectorMaterial->BaseMaterialBlending)
				{
					case PB_AlphaBlend:
					{
						MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_BLENDTEXTUREALPHA;
						MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_TEXTURE;
						MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
						MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG1;
						MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE;
						break;
					}
					case PB_Modulate:
					{
						MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_MODULATE2X;
						MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_TEXTURE;
						MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
						MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG1;
						MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE;
						break;
					}
					case PB_None:
					default:
					{
						switch(ProjectorMaterial->FrameBufferBlending)
						{
							case PB_Add:
							{
								MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_MODULATE;
								MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_DIFFUSE | D3DTA_ALPHAREPLICATE;
								MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_TEXTURE;
								MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG2;
								MaterialState->Stages[StagesUsed].AlphaArg2 = D3DTA_TEXTURE;
								break;
							}
							case PB_AlphaBlend:
							{
								MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_SELECTARG2;
								MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_TEXTURE;
								MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_MODULATE;
								MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_DIFFUSE;
								MaterialState->Stages[StagesUsed].AlphaArg2 = D3DTA_TEXTURE;
								break;
							}
							case PB_Modulate:
							default:
							{
								MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_BLENDDIFFUSEALPHA;
								MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_TEXTURE;
								MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_TFACTOR;
								MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG1;
								MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE;
								MaterialState->TFactorColor = FColor(127,127,127,0);
								break;
							}
						};
						break;
					}
				};

				StagesUsed++;
			}
			else
			{
				if(ErrorString)
					*ErrorString = TEXT("No stages left for projected texture");

				if(ErrorMaterial)
					*ErrorMaterial = ProjectorMaterial;

				return 0;
			}
		}
		else
		{
			// Handle the projected texture.

			ProjectedModifier.ModifyTextureTransforms = 1;
			ProjectedModifier.TexCoordCount = TCN_3DCoords;
			ProjectedModifier.TexCoordProjected = 1;
			ProjectedModifier.TexCoordSource = TCS_Stream0;

			if(!HandleCombinedMaterial(ProjectorMaterial->Projected,PassesUsed,StagesUsed,FreeStagesUsed,ProjectedModifier,0,ErrorString,ErrorMaterial))
				return 0;

			if(ProjectorMaterial->BaseMaterialBlending != PB_None && BaseDiffuse)
			{
				// Handle the base material.

				FOpenGLModifierInfo	BaseModifier = InModifierInfo;
				UBitmapMaterial*	BaseBitmap = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this,BaseDiffuse,&BaseModifier);

				if(!BaseBitmap)
				{
					if(ErrorString)
						*ErrorString = TEXT("Either projected texture or base material must be simple.");

					if(ErrorMaterial)
						*ErrorMaterial = ProjectorMaterial;

					return 0;
				}

				if(StagesUsed < MaxTextureStages + FreeStagesUsed)
				{
					SetShaderBitmap(MaterialState->Stages[StagesUsed],BaseBitmap);
					ApplyTexModifier(MaterialState->Stages[StagesUsed],&BaseModifier);
					MaterialState->Stages[StagesUsed].TexCoordIndex = 1;

					switch(ProjectorMaterial->BaseMaterialBlending)
					{
						case PB_AlphaBlend:
						{
							MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_BLENDTEXTUREALPHA;
							MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_TEXTURE;
							MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
							MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_BLENDTEXTUREALPHA;
							MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE;
							MaterialState->Stages[StagesUsed].AlphaArg2 = D3DTA_CURRENT;
							break;
						}
						case PB_Modulate:
						{
							MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_MODULATE2X;
							MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_TEXTURE;
							MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
							MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_MODULATE2X;
							MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE;
							MaterialState->Stages[StagesUsed].AlphaArg2 = D3DTA_CURRENT;
							break;
						}
						case PB_None:
						default:
						{
							MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_SELECTARG1;
							MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_TEXTURE;
							MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG1;
							MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE;
							break;
						}		
					};

					StagesUsed++;
				}
				else
				{
					if(ErrorString)
						*ErrorString = TEXT("No stages left for base material");

					if(ErrorMaterial)
						*ErrorMaterial = BaseDiffuse;

					return 0;
				}
			}
		}

		if(BaseOpacity)
		{
			// Handle the base material opacity.

			if(StagesUsed < MaxTextureStages + FreeStagesUsed)
			{
				FOpenGLModifierInfo	BaseOpacityModifier = InModifierInfo;
				UBitmapMaterial*	BaseOpacityBitmap = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this,BaseOpacity);

				if(BaseOpacityBitmap)
				{
					SetShaderBitmap(MaterialState->Stages[StagesUsed],BaseOpacityBitmap);
					ApplyTexModifier(MaterialState->Stages[StagesUsed],&BaseOpacityModifier);
					MaterialState->Stages[StagesUsed].TexCoordIndex = 1;

					switch(ProjectorMaterial->FrameBufferBlending)
					{
						case PB_Add:
						{
							MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_MODULATE;
							MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE;
							MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
							MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG2;
							MaterialState->Stages[StagesUsed].AlphaArg2 = D3DTA_CURRENT;
							break;
						}
						case PB_AlphaBlend:
						{
							MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_SELECTARG2;
							MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
							MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_MODULATE;
							MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE;
							MaterialState->Stages[StagesUsed].AlphaArg2 = D3DTA_CURRENT;
							break;
						}
						case PB_Modulate:
						default:
						{
							MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_BLENDTEXTUREALPHA;
							MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_CURRENT;
							MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_TFACTOR;
							MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG1;
							MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_CURRENT;
							MaterialState->TFactorColor = FColor(127,127,127,0);
							break;
						}
					};

					StagesUsed++;
				}
				else
				{
					if(ErrorString)
						*ErrorString = TEXT("Base opacity isn't bitmap");

					if(ErrorMaterial)
						*ErrorMaterial = BaseOpacity;

					return 0;
				}
			}
			else
			{
				if(ErrorString)
					*ErrorString = TEXT("No stages left for base opacity");

				if(ErrorMaterial)
					*ErrorMaterial = ProjectorMaterial;

				return 0;
			}
		}

		if( !RenDev->IsGeForce && (ProjectorMaterial->BaseMaterialBlending != PB_None || !ProjectedBitmap) && (ProjectorMaterial->bGradient || !ProjectorMaterial->bProjectOnBackfaces))
		{
			// Handle the vertex attenuation.

			if(StagesUsed < MaxBlendStages)
			{
				switch(ProjectorMaterial->FrameBufferBlending)
				{
					case PB_Add:
					{
						MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_MODULATE;
						MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_DIFFUSE | D3DTA_ALPHAREPLICATE;
						MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
						MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG2;
						MaterialState->Stages[StagesUsed].AlphaArg2 = D3DTA_CURRENT;
						break;
					}
					case PB_AlphaBlend:
					{
						MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_SELECTARG2;
						MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
						MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_MODULATE;
						MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_DIFFUSE;
						MaterialState->Stages[StagesUsed].AlphaArg2 = D3DTA_CURRENT;
						break;
					}
					case PB_Modulate:
					default:
					{
						MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_BLENDDIFFUSEALPHA;
						MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_CURRENT;
						MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_TFACTOR;
						MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG1;
						MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_CURRENT;
						MaterialState->TFactorColor = FColor(127,127,127,0);
						break;
					}
				};

				StagesUsed++;

				if(MaxBlendStages > MaxTextureStages)
					FreeStagesUsed++;
			}
			else
			{
				if(ErrorString)
					*ErrorString = TEXT("No stages left for attenuation");

				if(ErrorMaterial)
					*ErrorMaterial = ProjectorMaterial;

				return 0;
			}
		}
	}
	else
	{
		// Calculate the inverse view matrix.

		D3DXMATRIX	InvViewMatrix;

		D3DXMatrixInverse(&InvViewMatrix,NULL,(D3DXMATRIX*) &CurrentState->WorldToCamera);

		// Calculate the camera to projected texture matrix.

		D3DXMATRIX	CameraToTexture;

		D3DXMatrixMultiply(&CameraToTexture,&InvViewMatrix,(D3DXMATRIX*) &ProjectorMaterial->Matrix);

		// See if the projected texture is simple.

		FOpenGLModifierInfo	ProjectedModifier;
		UBitmapMaterial*	ProjectedBitmap = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this,ProjectorMaterial->Projected,&ProjectedModifier);

		if(ProjectedBitmap)
		{
			// Handle the base material.

			if(ProjectorMaterial->BaseMaterialBlending != PB_None && BaseDiffuse)
			{
				if(!HandleCombinedMaterial(BaseDiffuse,PassesUsed,StagesUsed,FreeStagesUsed,InModifierInfo,0,ErrorString,ErrorMaterial))
					return 0;
			}

			// Handle the projected texture.

			if(StagesUsed < MaxTextureStages + FreeStagesUsed)
			{
				SetShaderBitmap(MaterialState->Stages[StagesUsed],ProjectedBitmap);
				D3DXMatrixMultiply((D3DXMATRIX*)&MaterialState->Stages[StagesUsed].TextureTransformMatrix,&CameraToTexture,(D3DXMATRIX*)&ProjectedModifier.Matrix);
				MaterialState->Stages[StagesUsed].TexCoordCount = D3DTTFF_COUNT3;
				MaterialState->Stages[StagesUsed].TexCoordIndex = D3DTSS_TCI_CAMERASPACEPOSITION;
				MaterialState->Stages[StagesUsed].TextureTransformsEnabled = 1;

				// xbox linear texture uv translation (same as in ApplyTexModifier()
				if( IsTextureLinear(MaterialState->Stages[StagesUsed].Texture) )
				{
					D3DXMATRIX LinearConvert;
					D3DXMatrixIdentity( &LinearConvert );
					LinearConvert._11 = MaterialState->Stages[StagesUsed].Texture->CachedWidth;
					LinearConvert._22 = MaterialState->Stages[StagesUsed].Texture->CachedHeight;

					D3DXMatrixMultiply((D3DXMATRIX*)&MaterialState->Stages[StagesUsed].TextureTransformMatrix,
						(D3DXMATRIX*)&MaterialState->Stages[StagesUsed].TextureTransformMatrix,
						(D3DXMATRIX*)&LinearConvert);
				}

				if(ProjectorMaterial->bProjected)
					MaterialState->Stages[StagesUsed].TexCoordCount |= D3DTTFF_PROJECTED;

				switch(ProjectorMaterial->BaseMaterialBlending)
				{
					case PB_AlphaBlend:
					{
						MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_BLENDTEXTUREALPHA;
						MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_TEXTURE;
						MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
						MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG1;
						MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE;
						break;
					}
					case PB_Modulate:
					{
						MaterialState->Stages[StagesUsed].ColorOp = DUMMY_MODULATE2X;
						MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_TEXTURE;
						MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
						MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG1;
						MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE;
						break;
					}
					case PB_None:
					default:
					{
						MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_SELECTARG1;
						MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_TEXTURE;
						MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG1;
						MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE;
						break;
					}
				};

				StagesUsed++;
			}
			else
			{
				if(ErrorString)
					*ErrorString = TEXT("No stages left for projected texture");

				if(ErrorMaterial)
					*ErrorMaterial = ProjectorMaterial;

				return 0;
			}
		}
		else
		{
			// Handle the projected texture.

			ProjectedModifier.ModifyTextureTransforms = 1;
			ProjectedModifier.TexCoordSource = TCS_WorldCoords;
			ProjectedModifier.TexCoordCount = TCN_3DCoords;
			ProjectedModifier.TexCoordProjected = ProjectorMaterial->bProjected;
			ProjectedModifier.Matrix = ProjectorMaterial->Matrix;

			if(!HandleCombinedMaterial(ProjectorMaterial->Projected,PassesUsed,StagesUsed,FreeStagesUsed,ProjectedModifier,0,ErrorString,ErrorMaterial))
				return 0;

			if(ProjectorMaterial->BaseMaterialBlending != PB_None && BaseDiffuse)
			{
				// Handle the base material.

				FOpenGLModifierInfo	BaseModifier = InModifierInfo;
				UBitmapMaterial*	BaseBitmap = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this,BaseDiffuse,&BaseModifier);

				if(!BaseBitmap)
				{
					if(ErrorString)
						*ErrorString = TEXT("Either projected texture or base material must be simple.");

					if(ErrorMaterial)
						*ErrorMaterial = ProjectorMaterial;

					return 0;
				}

				if(StagesUsed < MaxTextureStages + FreeStagesUsed)
				{
					SetShaderBitmap(MaterialState->Stages[StagesUsed],BaseBitmap);
					ApplyTexModifier(MaterialState->Stages[StagesUsed],&BaseModifier);

					switch(ProjectorMaterial->BaseMaterialBlending)
					{
						case PB_AlphaBlend:
						{
							MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_BLENDTEXTUREALPHA;
							MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_TEXTURE;
							MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
							MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_BLENDTEXTUREALPHA;
							MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE;
							MaterialState->Stages[StagesUsed].AlphaArg2 = D3DTA_CURRENT;
							break;
						}
						case PB_Modulate:
						{
							MaterialState->Stages[StagesUsed].ColorOp = DUMMY_MODULATE2X;
							MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_TEXTURE;
							MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
							MaterialState->Stages[StagesUsed].AlphaOp = DUMMY_MODULATE2X;
							MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE;
							MaterialState->Stages[StagesUsed].AlphaArg2 = D3DTA_CURRENT;
							break;
						}
						case PB_None:
						default:
						{
							MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_SELECTARG1;
							MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_TEXTURE;
							MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG1;
							MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE;
							break;
						}		
					};

					StagesUsed++;
				}
				else
				{
					if(ErrorString)
						*ErrorString = TEXT("No stages left for base material");

					if(ErrorMaterial)
						*ErrorMaterial = BaseDiffuse;

					return 0;
				}
			}
		}

		if(ProjectorMaterial->bGradient)
		{
			// Setup camera to gradient transform.

			D3DXMATRIX	CameraToGradient;

			D3DXMatrixMultiply(&CameraToGradient,&InvViewMatrix,(D3DXMATRIX*) &ProjectorMaterial->GradientMatrix);

			// Handle the gradient texture.

			if(StagesUsed < MaxTextureStages + FreeStagesUsed)
			{
				SetShaderBitmap(MaterialState->Stages[StagesUsed],ProjectorMaterial->Gradient);

				MaterialState->Stages[StagesUsed].TextureTransformsEnabled = 1;
				MaterialState->Stages[StagesUsed].TexCoordIndex = D3DTSS_TCI_CAMERASPACEPOSITION;
				MaterialState->Stages[StagesUsed].TexCoordCount = D3DTTFF_COUNT3;
				MaterialState->Stages[StagesUsed].TextureTransformMatrix = CameraToGradient;

				switch(ProjectorMaterial->FrameBufferBlending)
				{
					case PB_Add:
					{
						MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_MODULATE;
						MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE;
						MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
						MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG2;
						MaterialState->Stages[StagesUsed].AlphaArg2 = D3DTA_CURRENT;
						break;
					}
					case PB_AlphaBlend:
					{
						MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_SELECTARG2;
						MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
						MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_MODULATE;
						MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE;
						MaterialState->Stages[StagesUsed].AlphaArg2 = D3DTA_CURRENT;
						break;
					}
					case PB_Modulate:
					default:
					{
						MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_BLENDTEXTUREALPHA;
						MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_CURRENT;
						MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_TFACTOR;
						MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG1;
						MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_CURRENT;
						MaterialState->TFactorColor = FColor(127,127,127,0);
						break;
					}
				};

				StagesUsed++;
			}
			else
			{
				if(ErrorString)
					*ErrorString = TEXT("No stages left for gradient texture");

				if(ErrorMaterial)
					*ErrorMaterial = ProjectorMaterial;

				return 0;
			}
		}

		if(ProjectorMaterial->bProjectOnAlpha && BaseOpacity)
		{
			// Handle the gradient texture.

			if(StagesUsed < MaxTextureStages + FreeStagesUsed)
			{
				FOpenGLModifierInfo	BaseOpacityModifier = InModifierInfo;
				UBitmapMaterial*	BaseOpacityBitmap = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this,BaseOpacity);

				if(BaseOpacityBitmap)
				{
					SetShaderBitmap(MaterialState->Stages[StagesUsed],BaseOpacityBitmap);
					ApplyTexModifier(MaterialState->Stages[StagesUsed],&BaseOpacityModifier);

					switch(ProjectorMaterial->FrameBufferBlending)
					{
						case PB_Add:
						{
							MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_MODULATE;
							MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_TEXTURE | D3DTA_ALPHAREPLICATE;
							MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
							MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG2;
							MaterialState->Stages[StagesUsed].AlphaArg2 = D3DTA_CURRENT;
							break;
						}
						case PB_AlphaBlend:
						{
							MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_SELECTARG2;
							MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
							MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_MODULATE;
							MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE;
							MaterialState->Stages[StagesUsed].AlphaArg2 = D3DTA_CURRENT;
							break;
						}
						case PB_Modulate:
						default:
						{
							MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_BLENDTEXTUREALPHA;
							MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_CURRENT;
							MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_TFACTOR;
							MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG1;
							MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_CURRENT;
							MaterialState->TFactorColor = FColor(127,127,127,0);
							break;
						}
					};

					StagesUsed++;
				}
				else
				{
					if(ErrorString)
						*ErrorString = TEXT("Base opacity isn't bitmap");

					if(ErrorMaterial)
						*ErrorMaterial = BaseOpacity;

					return 0;
				}
			}
			else
			{
				if(ErrorString)
						*ErrorString = TEXT("No stages left for base opacity");

				if(ErrorMaterial)
					*ErrorMaterial = ProjectorMaterial;

				return 0;
			}
		}

		if(!ProjectorMaterial->bProjectOnBackfaces)
		{
			static FProjectorCubeMap	ProjectorCubeMap(32);

			// Handle the lighting modulation.

			if(StagesUsed < MaxTextureStages + FreeStagesUsed)
			{
				MaterialState->Stages[StagesUsed].Texture = CacheTexture(&ProjectorCubeMap);
				MaterialState->Stages[StagesUsed].TextureTransformsEnabled = 1;
				MaterialState->Stages[StagesUsed].TexCoordIndex = D3DTSS_TCI_CAMERASPACENORMAL;
				MaterialState->Stages[StagesUsed].TexCoordCount = D3DTTFF_COUNT3;
				MaterialState->Stages[StagesUsed].TextureAddressU = RenDev->CubemapTextureAddressing;
				MaterialState->Stages[StagesUsed].TextureAddressV = RenDev->CubemapTextureAddressing;
				MaterialState->Stages[StagesUsed].TextureAddressW = RenDev->CubemapTextureAddressing;

				FCoords	NormalCameraToTexture = ((FMatrix*)&CameraToTexture)->Coords();

				NormalCameraToTexture.XAxis.Normalize();
				NormalCameraToTexture.YAxis.Normalize();
				NormalCameraToTexture.ZAxis.Normalize();

				MaterialState->Stages[StagesUsed].TextureTransformMatrix._11 = NormalCameraToTexture.XAxis.X;
				MaterialState->Stages[StagesUsed].TextureTransformMatrix._21 = NormalCameraToTexture.XAxis.Y;
				MaterialState->Stages[StagesUsed].TextureTransformMatrix._31 = NormalCameraToTexture.XAxis.Z;
				MaterialState->Stages[StagesUsed].TextureTransformMatrix._41 = 0.0f;
				MaterialState->Stages[StagesUsed].TextureTransformMatrix._12 = NormalCameraToTexture.YAxis.X;
				MaterialState->Stages[StagesUsed].TextureTransformMatrix._22 = NormalCameraToTexture.YAxis.Y;
				MaterialState->Stages[StagesUsed].TextureTransformMatrix._32 = NormalCameraToTexture.YAxis.Z;
				MaterialState->Stages[StagesUsed].TextureTransformMatrix._42 = 0.0f;
				MaterialState->Stages[StagesUsed].TextureTransformMatrix._13 = -NormalCameraToTexture.ZAxis.X;
				MaterialState->Stages[StagesUsed].TextureTransformMatrix._23 = -NormalCameraToTexture.ZAxis.Y;
				MaterialState->Stages[StagesUsed].TextureTransformMatrix._33 = -NormalCameraToTexture.ZAxis.Z;
				MaterialState->Stages[StagesUsed].TextureTransformMatrix._43 = 0.0f;
				MaterialState->Stages[StagesUsed].TextureTransformMatrix._34 = 0.0f;
				MaterialState->Stages[StagesUsed].TextureTransformMatrix._44 = 1.0f;

				switch(ProjectorMaterial->FrameBufferBlending)
				{
					case PB_Add:
					{
						MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_MODULATE;
						MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_TEXTURE;
						MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
						MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG2;
						MaterialState->Stages[StagesUsed].AlphaArg2 = D3DTA_CURRENT;
						break;
					}
					case PB_AlphaBlend:
					{
						MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_SELECTARG2;
						MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_CURRENT;
						MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_MODULATE;
						MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_TEXTURE;
						MaterialState->Stages[StagesUsed].AlphaArg2 = D3DTA_CURRENT;
						break;
					}
					case PB_Modulate:
					default:
					{
						MaterialState->Stages[StagesUsed].ColorOp = D3DTOP_BLENDTEXTUREALPHA;
						MaterialState->Stages[StagesUsed].ColorArg1 = D3DTA_CURRENT;
						MaterialState->Stages[StagesUsed].ColorArg2 = D3DTA_TFACTOR;
						MaterialState->Stages[StagesUsed].AlphaOp = D3DTOP_SELECTARG1;
						MaterialState->Stages[StagesUsed].AlphaArg1 = D3DTA_CURRENT;
						MaterialState->TFactorColor = FColor(127,127,127,0);
						break;
					}
				};

				StagesUsed++;
			}
			else
			{
				if(ErrorString)
					*ErrorString = TEXT("No stages left for directional attenuation");

				if(ErrorMaterial)
					*ErrorMaterial = ProjectorMaterial;

				return 0;
			}
		}
	}

	// Handle frame buffer blending.

	MaterialState->TwoSided = ProjectorMaterial->bTwoSided;
	MaterialState->AlphaBlending = 1;
	MaterialState->ZWrite = 0;

	switch(ProjectorMaterial->FrameBufferBlending)
	{
		case PB_Add:
		{
			MaterialState->SrcBlend = D3DBLEND_ONE;
			MaterialState->DestBlend = D3DBLEND_ONE;
			MaterialState->OverrideFogColor	= 1;
			MaterialState->OverriddenFogColor = FColor(0,0,0,0);
			break;
		}
		case PB_AlphaBlend:
		{
			MaterialState->SrcBlend = D3DBLEND_SRCALPHA;
			MaterialState->DestBlend = D3DBLEND_INVSRCALPHA;
			break;
		}
		case PB_Modulate:
		default:
		{
			MaterialState->SrcBlend = D3DBLEND_DESTCOLOR;
			MaterialState->DestBlend = D3DBLEND_SRCCOLOR;
			MaterialState->OverrideFogColor	= 1;
			MaterialState->OverriddenFogColor = FColor(127,127,127,0);
			break;
		}
	};

	CurrentState->MaterialPasses[PassesUsed]->StagesUsed = StagesUsed;
	CurrentState->NumMaterialPasses = ++PassesUsed;

	if(PassesUsed == 1)
		return 1;
	else
		return 0;

	unguard;
}


#ifdef _XBOX

#include "xmvhelper.h"

/* scion ======================================================================
 * FOpenGLRenderInterface::SetMovieMaterial
 * Author: jg
 * 
 * Gets the next frame of the movie and sets that into the render states. Note
 * since the UMovieMaterial just wraps a CXMVHelper object, we must manually
 * handle the caching of the FTexture data. That means the cached information
 * must be built by hand out of the texture that is returned by CXMVHelper. 
 * Yes this is a bit of a hack and but there is no other way. I hate to put
 * the code to handle the creating/loading/reading in here, but it is better
 * than breaking the separation of Engine versus D3DDrv.
 *
 * input:	pMovieMaterial The movie material to set the render states for
 *			BaseModifierInfo The base modifier for this texture
 *			ErrorString The associated error string
 *			ErrorMaterial The material to use on error
 *
 * ============================================================================
 */
UBOOL FOpenGLRenderInterface::SetMovieMaterial(UMovieMaterial* pMovieMaterial,
	FOpenGLModifierInfo BaseModifierInfo, FString* ErrorString,
	UMaterial** ErrorMaterial)
{
	guard(FOpenGLRenderInterface::SetMovieMaterial);
	FUNC_CLOCK(SMCyc,GStats.DWORDStats(GEngineStats.STATS_MovieMaterial_Cycles));
	UBOOL bHandled = 0;
	// See if this needs to be reset or not
	if (pMovieMaterial->bReset == 1)
	{
		// Only try to delete it if it is valid
		if (pMovieMaterial->MovieDecoder != NULL)
		{
			CXMVPlayer* pDecoder = (CXMVPlayer*)pMovieMaterial->MovieDecoder;
			pDecoder->Reset();
		}
		// No longer in reset mode
		pMovieMaterial->bHasError = pMovieMaterial->bReset = 0;
	}
	// If we haven't set up the movie reader yet, do so
	if (pMovieMaterial->MovieDecoder == NULL && pMovieMaterial->bHasError == 0)
	{
		// Check for streaming being needed
		if (pMovieMaterial->bDecodeFromMemory == 1)
		{
			if (pMovieMaterial->bDoneBackgroundLoading == 1)
			{
				CXMVPlayer* pDecoder = (CXMVPlayer*)NEW CXMVPlayer();
				check(pDecoder && pMovieMaterial->InMemoryDecodingBlock);
				// Attempt to have it open the movie
				HRESULT hr = pDecoder->OpenMovieFromMemory(D3DFMT_LIN_A8R8G8B8,
					RenDev->Direct3DDevice8,1,pMovieMaterial->bEnableAudio,
					(BYTE*)pMovieMaterial->InMemoryDecodingBlock,
					pMovieMaterial->InMemoryBlockSize,
					pMovieMaterial->bPlayLooping,
					pMovieMaterial->bManualFramerateSynch);
				if (SUCCEEDED(hr))
				{
					pMovieMaterial->MovieDecoder = (INT)pDecoder;
				}
				else
				{
					// So we don't try this material again
					pMovieMaterial->bHasError = 1;
					// Log the failure so the artist can fix
					debugf(NAME_DevD3D,TEXT("Failed to open movie (%s)"),
						*pMovieMaterial->MovieFileName);
					delete pDecoder;
				}
			}
			else
			{
				// Start the background loading
				pMovieMaterial->LoadInMemoryMovie();
				// Use the fallback material if valid
				return SetMaterial(pMovieMaterial->LoadingFallback == NULL ?
						pMovieMaterial->DefaultMaterial :
						pMovieMaterial->LoadingFallback,ErrorString,ErrorMaterial,
						NULL);
			}
		}
		else
		{
			CXMVPlayer* pDecoder = (CXMVPlayer*)NEW CXMVPlayer();
			check(pDecoder);
			// Attempt to have it open the movie
			HRESULT hr = pDecoder->OpenFile(pMovieMaterial->MovieFileName,
				D3DFMT_LIN_A8R8G8B8,RenDev->Direct3DDevice8,1,
				pMovieMaterial->bEnableAudio,pMovieMaterial->bPlayLooping,
				pMovieMaterial->bManualFramerateSynch);
			if (SUCCEEDED(hr))
			{
				pMovieMaterial->MovieDecoder = (INT)pDecoder;
			}
			else
			{
				// So we don't try this material again
				pMovieMaterial->bHasError = 1;
				// Log the failure so the artist can fix
				debugf(NAME_DevD3D,TEXT("Failed to open movie (%s)"),
					*pMovieMaterial->MovieFileName);
				delete pDecoder;
			}
		}
	}
	LPDIRECT3DTEXTURE8 pDisplay = NULL;
	// Check to see if it failed to open
	if (pMovieMaterial->bHasError == 0)
	{
		// Only get the next frame if it is needed
		if (pMovieMaterial->bNeedsFrameUpdate == 1)
		{
			DWORD XmvResult = 0;
			// Get the next frame of the movie
			pDisplay = ((CXMVPlayer*)pMovieMaterial->MovieDecoder)->
				AdvanceFrameForTexturing(RenDev->Direct3DDevice8,XmvResult);
			// Fire off the delegate letting the interested parties know the
			// movie is done playing
			if (XmvResult == XMV_ENDOFFILE &&
				OBJ_DELEGATE_IS_SET(pMovieMaterial,OnDonePlaying))
			{
				pMovieMaterial->delegateOnDonePlaying();
			}
			// If there wasn't a frame ready or there was an error, show the
			// backup material
			if (pDisplay == NULL)
			{
				// Use the fallback material if valid
				return SetMaterial(pMovieMaterial->LoadingFallback == NULL ?
						pMovieMaterial->DefaultMaterial :
						pMovieMaterial->LoadingFallback,ErrorString,ErrorMaterial,
						NULL);
			}
			// Store the last used texture oi
			pMovieMaterial->LastD3DTexture = (INT)pDisplay;
		}
		else
		{
			// Use the last gotten frame
			pDisplay = (LPDIRECT3DTEXTURE8)pMovieMaterial->LastD3DTexture;
		}
	}
	// If we have a valid texture display it, otherwise use the calcium texture
	if (pDisplay != NULL)
	{
		D3DSURFACE_DESC desc;
		// Figure out the width and height and stick those in the movie material
		pDisplay->GetLevelDesc(0,&desc);
		// Now place in the material
		pMovieMaterial->USize = desc.Width;
		pMovieMaterial->VSize = desc.Height;
		DECLARE_STATIC_UOBJECT(UTexMatrix,LinearTexMatrix, 
		{
			LinearTexMatrix->Matrix.SetIdentity();
		}
		);
		// Stick our movie in the material so it can be scaled correctly
		LinearTexMatrix->Material = pMovieMaterial;
		// Cast to a bitmap so we can set it and cache it
		UBitmapMaterial* Bitmap = CheckMaterial<UBitmapMaterial,
			MRH_BitmapMaterial>(this,LinearTexMatrix,&BaseModifierInfo);
		FBaseTexture* Resource = Bitmap->Get(Viewport->CurrentTime,Viewport)->GetRenderInterface();
		// Flush it so it's not in the cache multiple times
		Resource->Flush();
		// Cache the texture
		FOpenGLTexture* pTexture = CacheTexture(Resource);
		pTexture->bIsUMA = 0;
		// Now add ref it so we can safely release it later
		pDisplay->AddRef();
		// If we have set a previous display texture, release it
		if (pTexture->Direct3DTexture8 != NULL)
		{
			pTexture->Direct3DTexture8->Release();
		}
		// Set the new display texture to use
		pTexture->Direct3DTexture8 = pDisplay;
		// Sets basic modes
		SetShaderBitmap(CurrentState->MaterialPasses[0]->Stages[0],Bitmap);
		// Manually fix up the pointer for the texture
		CurrentState->MaterialPasses[0]->Stages[0].Texture->Direct3DTexture8 =
			pDisplay;
		// Manually fix up the cached item
		CurrentState->MaterialPasses[0]->Stages[0].Texture->CachedFormat =
			D3DFMT_LIN_A8R8G8B8;
		CurrentState->MaterialPasses[0]->Stages[0].Texture->CachedWidth =
			desc.Width;
		CurrentState->MaterialPasses[0]->Stages[0].Texture->CachedHeight =
			desc.Height;
		// Apply any modifier that might be affecting the movie output
		ApplyTexModifier(CurrentState->MaterialPasses[0]->Stages[0],
			&BaseModifierInfo);
		CurrentState->MaterialPasses[0]->Stages[0].AlphaArg1 = D3DTA_TEXTURE;
		CurrentState->MaterialPasses[0]->Stages[0].ColorArg1 = D3DTA_TEXTURE;
		CurrentState->MaterialPasses[0]->Stages[0].AlphaOp = D3DTOP_SELECTARG1;
		CurrentState->MaterialPasses[0]->Stages[0].ColorOp = D3DTOP_SELECTARG1;
		// Manually fix up the pointer for the texture
		CurrentState->MaterialPasses[0]->Stages[0].Texture->Direct3DTexture8 =
			pDisplay;
		// Set blending states
		CurrentState->MaterialPasses[0]->StagesUsed = 1;
		// Figure out the back buffer blending
		if (pMovieMaterial->bAdditive == 0)
		{
			CurrentState->MaterialPasses[0]->SrcBlend = D3DBLEND_SRCALPHA;
			CurrentState->MaterialPasses[0]->DestBlend = D3DBLEND_INVSRCALPHA;
		}
		else
		{
			CurrentState->MaterialPasses[0]->SrcBlend = D3DBLEND_ONE;
			CurrentState->MaterialPasses[0]->DestBlend = D3DBLEND_INVSRCCOLOR;
		}
		CurrentState->MaterialPasses[0]->AlphaBlending = 1;
		CurrentState->MaterialPasses[0]->AlphaRef = 0;
		CurrentState->MaterialPasses[0]->ZWrite = 1;
		CurrentState->MaterialPasses[0]->ZTest = 1;
		CurrentState->MaterialPasses[0]->TwoSided = 0;
		// Handle masking if requested
		if (pMovieMaterial->Mask != NULL)
		{
			CurrentState->MaterialPasses[0]->Stages[1].AlphaArg1 = D3DTA_TEXTURE;
			CurrentState->MaterialPasses[0]->Stages[1].ColorArg1 = D3DTA_CURRENT;
			CurrentState->MaterialPasses[0]->Stages[1].AlphaOp = D3DTOP_SELECTARG1;
			CurrentState->MaterialPasses[0]->Stages[1].ColorOp = D3DTOP_SELECTARG1;
			FOpenGLModifierInfo MaskModifier;
			// Cast to a bitmap so we can set it and cache it
			UBitmapMaterial* Mask = CheckMaterial<UBitmapMaterial,
				MRH_BitmapMaterial>(this,pMovieMaterial->Mask,&MaskModifier);
			// Performs caching and sets basic modes
			SetShaderBitmap(CurrentState->MaterialPasses[0]->Stages[1],Mask);
			// Apply any modifier that might be affecting the movie output
			ApplyTexModifier(CurrentState->MaterialPasses[0]->Stages[1],
				&MaskModifier);
			CurrentState->MaterialPasses[0]->StagesUsed = 2;
		}
		bHandled = 1;
	}
	STAT_INC(GStats.DWORDStats(GEngineStats.STATS_MovieMaterial_Num));
	return bHandled;
	unguard;
}

#else

/* scion ======================================================================
 * FOpenGLRenderInterface::SetMovieMaterial
 * Author: jg
 * 
 * Always returns not found on the PC
 *
 * input:	ignored
 *			ignored
 *			ignored
 *			ignored
 *
 * ============================================================================
 */
UBOOL FOpenGLRenderInterface::SetMovieMaterial(UMovieMaterial*,FOpenGLModifierInfo,
	FString*,UMaterial**)
{
	guard(FOpenGLRenderInterface::SetMovieMaterial);
	return 0;
	unguard;
}

#endif
/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/
