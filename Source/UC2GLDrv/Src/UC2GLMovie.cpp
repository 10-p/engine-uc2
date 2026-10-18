/*=============================================================================
	UC2GLMovie.cpp: MovieMaterial on the OpenGL device.

	The PC branch of SetMovieMaterial. UC2's D3DDrv drew movie materials only on the Xbox
	(D3DMaterialState.cpp, carried over unchanged in UC2GLMaterialState.cpp under #ifdef _XBOX) and
	returned 0 on PC. This file is the portable equivalent, kept out of UC2GLMaterialState.cpp so that file
	stays diffable against Epic's (its PC stub is the only line range removed there):
	  - the movie logic (bReset, open on first use, advance once per level time step, OnDonePlaying,
	    LoadingFallback) is the engine's appUpdateMovieMaterial (Engine/Src/UnMovie.cpp), shared by every device;
	  - the texture: the engine's FMovieTexture never changes revision, so the generic cache never uploads
	    anything into it; the frame is uploaded here into that cache entry's GL texture, only when the material
	    is drawn and the player's picture revision differs from the one uploaded (kept in the material's
	    transient LastD3DTexture, which held the last D3D texture on the Xbox);
	  - the material state is the Xbox branch's: texture SELECTARG1 for colour and alpha, SRCALPHA/INVSRCALPHA
	    blending (ONE/INVSRCCOLOR when bAdditive), Z write and test, the Mask's alpha on stage 1. No linear
	    texture matrix: GL textures take normalised coordinates (the Xbox's LIN_ textures did not).
=============================================================================*/

#include "UC2GLDrv.h"
#include "UnMovie.h"

#if !defined(_XBOX)

UBOOL FOpenGLRenderInterface::SetMovieMaterial(UMovieMaterial* pMovieMaterial,
	FOpenGLModifierInfo BaseModifierInfo, FString* ErrorString,
	UMaterial** ErrorMaterial)
{
	guard(FOpenGLRenderInterface::SetMovieMaterial);
	// (The Xbox counted STATS_MovieMaterial_Cycles/_Num; those stats exist only in the Xbox build of UnStats.h.)

	UBOOL bFallback = 0;
	FPortableMoviePlayer* Player = appUpdateMovieMaterial( pMovieMaterial, bFallback );
	if( !Player )
	{
		// No picture yet and the material has a LoadingFallback: draw that. (appUpdateMovieMaterial returns
		// NULL only then; without a fallback it hands a black picture, so the DefaultMaterial choice and the
		// return below are not reached.)
		if( bFallback )
			return SetMaterial( pMovieMaterial->LoadingFallback == NULL ? pMovieMaterial->DefaultMaterial :
				pMovieMaterial->LoadingFallback, ErrorString, ErrorMaterial, NULL );
		return 0;
	}

	// The cache entry of the material's FMovieTexture, with the current picture in it.
	FOpenGLMaterialStateStage& Stage = CurrentState->MaterialPasses[0]->Stages[0];
	SetShaderBitmap( Stage, pMovieMaterial );
	FOpenGLTexture* Texture = Stage.Texture;
	INT W = Player->GetWidth(), H = Player->GetHeight();
	INT Uploaded = pMovieMaterial->LastD3DTexture;	// revision + 1 of the picture in the GL texture, 0 = none
	if( Texture && Player->GetBGRA() && (!Texture->TextureID || Uploaded != Player->GetRevision() + 1) )
	{
		UBOOL bNew = !Texture->TextureID || Texture->CachedWidth != W || Texture->CachedHeight != H;
		if( !Texture->TextureID )
			RenDev->glGenTextures( 1, &Texture->TextureID );
		RenDev->glBindTexture( GL_TEXTURE_2D, Texture->TextureID );
		RenDev->glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
		if( bNew )
		{
			RenDev->glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0 );
			RenDev->glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_BGRA, GL_UNSIGNED_BYTE, Player->GetBGRA() );
		}
		else
			RenDev->glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, W, H, GL_BGRA, GL_UNSIGNED_BYTE, Player->GetBGRA() );
		Texture->CachedWidth	= W;
		Texture->CachedHeight	= H;
		Texture->CachedFirstMip	= 0;
		Texture->CachedNumMips	= 1;
		Texture->CachedFormat	= TEXF_RGBA8;
		Texture->CachedGLFormat	= GL_RGBA;
		pMovieMaterial->LastD3DTexture = Player->GetRevision() + 1;
		// The interface tracks what is bound per unit; we just changed a binding.
		RenDev->RenderInterface.InvalidateHardwareState();
	}

	// The Xbox branch's states (UC2GLMaterialState.cpp, SetMovieMaterial under _XBOX).
	ApplyTexModifier( Stage, &BaseModifierInfo );
	Stage.AlphaArg1 = D3DTA_TEXTURE;
	Stage.ColorArg1 = D3DTA_TEXTURE;
	Stage.AlphaOp = D3DTOP_SELECTARG1;
	Stage.ColorOp = D3DTOP_SELECTARG1;
	CurrentState->MaterialPasses[0]->StagesUsed = 1;
	if( pMovieMaterial->bAdditive == 0 )
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
	if( pMovieMaterial->Mask != NULL )
	{
		FOpenGLMaterialStateStage& MaskStage = CurrentState->MaterialPasses[0]->Stages[1];
		MaskStage.AlphaArg1 = D3DTA_TEXTURE;
		MaskStage.ColorArg1 = D3DTA_CURRENT;
		MaskStage.AlphaOp = D3DTOP_SELECTARG1;
		MaskStage.ColorOp = D3DTOP_SELECTARG1;
		FOpenGLModifierInfo MaskModifier;
		UBitmapMaterial* Mask = CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this,pMovieMaterial->Mask,&MaskModifier);
		SetShaderBitmap( MaskStage, Mask );
		ApplyTexModifier( MaskStage, &MaskModifier );
		CurrentState->MaterialPasses[0]->StagesUsed = 2;
	}
	return 1;
	unguard;
}

#endif // !_XBOX

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
