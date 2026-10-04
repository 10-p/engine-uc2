/*=============================================================================
	D3DRenderInterface.cpp: Unreal Direct3D rendering interface implementation.
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/

#include "D3DDrv.h"

//
//	FD3DRenderInterface::FD3DSavedState::FD3DSavedState
//
FD3DRenderInterface::FD3DSavedState::FD3DSavedState()
{
	guard(FD3DRenderInterface::FD3DSavedState::FD3DSavedState);

	ZBias				= 0;

	// scion sz -
	ColorWriteMask		= CWM_COLORWRITEENABLE_COLORONLY;
	
	LocalToWorld		= FMatrix::Identity;
	WorldToCamera		= FMatrix::Identity;
	CameraToScreen		= FMatrix::Identity;

	VertexShader		= NULL;
	appMemzero(Streams,sizeof(Streams));
	appMemzero(StreamStrides,sizeof(StreamStrides));

	NumStreams			= 0;
	
	IndexBuffer			= NULL;
	IndexBufferBase		= 0;

	CullMode			= D3DCULL_NONE;

	UseDetailTexturing  = 1;
	UseDynamicLighting	= 0;
	UseStaticLighting	= 1;
	LightingModulate2X	= 0;
	LightingOnly		= 0;
	Lightmap			= NULL;
	AmbientLightColor	= FColor(0,0,0,0);

	DistanceFogEnabled	= 0;
	DistanceFogStart	= 0.0f;
	DistanceFogEnd		= 0.0f;
	DistanceFogColor	= FColor(0,0,0,0);

	appMemzero(MaterialPasses,sizeof(MaterialPasses));
	NumMaterialPasses	= 0;
	CurrentMaterialState= NULL;
	LastMaterial=NULL;

	//scion jg -- Zero out the vertex shaders
	VertexShaderEnum = 0;

	ForceDefSphereMap=0;
	

	unguard;
}

//
//	FD3DRenderInterface::FD3DRenderInterface
//
FD3DRenderInterface::FD3DRenderInterface(UD3DRenderDevice* InRenDev)
{
	guard(FD3DRenderInterface::FD3DRenderInterface);

	RenDev		= InRenDev;
	Viewport	= NULL;

	PrecacheMode	= PRECACHE_None;

#ifndef _XBOX //scion jg -- Not used on xbox
	HitCount	= 0;
	HitData		= NULL;
	HitSize		= NULL;
#endif

	SavedStateIndex	= 0;
	CurrentState	= &SavedStates[SavedStateIndex];

	BackBufferCopyTarget = InRenDev->DefaultRenderTarget;

	DynamicGamma = NEW FD3DDynamicGamma(this);

	// scion sz - zero light type totals
	appMemzero(LightTypeTotals,sizeof(LightTypeTotals));

	// scion sz - default pass none
	PostProcessStage = PP_STAGE_NONE;

	unguard;
}

FD3DRenderInterface::~FD3DRenderInterface()
{
	guard(FD3DRenderInterface::~FD3DRenderInterface);

	if( DynamicGamma )
		delete DynamicGamma;

	unguard;
}


//
//	FD3DRenderInterface::Locked
//
void FD3DRenderInterface::Locked( UViewport* InViewport, BYTE* InHitData,INT* InHitSize)
{
	guard(FD3DRenderInterface::Locked)

	Viewport = InViewport;
#ifndef _XBOX //scion jg -- Not used on xbox
	HitCount = 0;
	HitData = InHitData;
	HitSize = InHitSize;
#endif

	// Determine the current render target and depth stencil surfaces.

	IDirect3DSurface8*	RenderTargetSurface = NULL;
	IDirect3DSurface8*	DepthStencilSurface = NULL;

	verify(!FAILED(RenDev->Direct3DDevice8->GetRenderTarget(&RenderTargetSurface)));
	verify(!FAILED(RenDev->Direct3DDevice8->GetDepthStencilSurface(&DepthStencilSurface)));

	// Setup the initial state
	// scion sz - added offsetX/Y
	SetViewport(Viewport->OffsetX,Viewport->OffsetY,Viewport->SizeX,Viewport->SizeY);

	SavedRenderTargetState.GetCurrentState()->DepthStencilSurface	= DepthStencilSurface;
	SavedRenderTargetState.GetCurrentState()->RenderTargetSurface	= RenderTargetSurface;

	// Save the render state.
	SavedStateIndex = 0;
	PushState();

	unguard;
}

//
//	FD3DRenderInterface::Unlocked
//
void FD3DRenderInterface::Unlocked()
{
	guard(FD3DRenderInterface::Unlocked);

	//scion jg -- Restore viewport state
	ResetViewport();
	// Verify that we reset ok
	checkSlow(SavedViewportState.GetStateIndex() == 0);
	checkSlow(SavedRenderTargetState.GetStateIndex() == 0);
	checkSlow(SavedStencilState.GetStateIndex() == 0);

	// Restore the initial state.
	PopState();
	checkSlow(SavedStateIndex == 0);

	//scion jg -- Release the surface pointers
	FD3DRenderTargetState* pState = SavedRenderTargetState.GetCurrentState();
	// Release the render target pointers.
	pState->RenderTargetSurface->Release();
	pState->RenderTargetSurface = NULL;
	pState->DepthStencilSurface->Release();
	pState->DepthStencilSurface = NULL;

	unguard;
}

//
//	FD3DRenderInterface::PushState
//
void FD3DRenderInterface::PushState()
{
	guard(F3DRenderInterface::PushState);

	FUNC_CLOCK(PushCyc,GStats.DWORDStats(RenDev->D3DStats.STATS_PushStateCycles));
	checkSlow(SavedStateIndex+1 < MAX_STATESTACKDEPTH);

	CurrentState = &SavedStates[SavedStateIndex+1];
	appMemcpy( CurrentState, &SavedStates[SavedStateIndex], sizeof(FD3DSavedState) );
	SavedStateIndex++;

	for(INT PassIndex = 0;PassIndex < CurrentState->NumMaterialPasses;PassIndex++)
		CurrentState->MaterialPasses[PassIndex]->NumRefs++;

#ifndef FINAL_RELEASE
	GStats.DWORDStats(RenDev->D3DStats.STATS_PushStateCalls)++;
#endif
	unguard;
}

//
//	FD3DRenderInterface::PopState
//
void FD3DRenderInterface::PopState()
{
	guard(F3DRenderInterface::PopState);

	FUNC_CLOCK(PopCyc,GStats.DWORDStats(RenDev->D3DStats.STATS_PopStateCycles));

	// Pop a saved state off the saved state stack.

	//scion jg -- Moved this to a debug only check rather than if statement
	checkSlow( SavedStateIndex > 0 && "PopState stack underflow");
	
	FD3DSavedState&	OldState	= SavedStates[SavedStateIndex--];
	FD3DSavedState& NewState	= SavedStates[SavedStateIndex];

	CurrentState = &SavedStates[SavedStateIndex];

	// Restore lighting.
	RenDev->DeferredState.SetRenderState( RS_LIGHTING, NewState.UseDynamicLighting ? TRUE : FALSE );
	RenDev->DeferredState.SetRenderState( RS_COLORVERTEX, NewState.UseStaticLighting ? TRUE : FALSE );

	RenDev->DeferredState.SetRenderState( RS_ZBIAS, NewState.ZBias );

	RenDev->DeferredState.SetTransform( TS_WORLD, (D3DMATRIX*) &NewState.LocalToWorld);
	RenDev->DeferredState.SetTransform( TS_VIEW, (D3DMATRIX*) &NewState.WorldToCamera);
	RenDev->DeferredState.SetTransform( TS_PROJECTION, (D3DMATRIX*) &NewState.CameraToScreen);

	if(NewState.VertexShader)
		RenDev->DeferredState.SetVertexShader(NewState.VertexShader);
	else
		RenDev->DeferredState.SetVertexShader(NULL);

	RenDev->DeferredState.SetVertexShaderEnum( NewState.VertexShaderEnum );
#if defined(_XBOX) && !defined(VERTEXSHADERS_ALWAYS_ON)
	//scion jg -- Reworked this code to not query d3d or use a static
	if (NewState.VertexShaderEnum == 0 && OldState.VertexShaderEnum > 0)
	{
		RenDev->Direct3DDevice8->SetShaderConstantMode( D3DSCM_96CONSTANTS );
	}
#endif

	CurrentState->VertexShader = NewState.VertexShader;
	//scion jg -- Correctly handle pushing/popping of vertex shaders
	CurrentState->VertexShaderEnum = NewState.VertexShaderEnum;

	for(INT Index = 0;Index < NewState.NumStreams;Index++)
		RenDev->DeferredState.SetStreamSource(Index,NewState.Streams[Index]->Direct3DVertexBuffer8,NewState.StreamStrides[Index]);

	for(INT Index = NewState.NumStreams;Index < OldState.NumStreams;Index++)
		RenDev->DeferredState.SetStreamSource(Index,NULL,0);

	CurrentState->NumStreams = NewState.NumStreams;

	if(NewState.IndexBuffer)
		RenDev->DeferredState.SetIndices(NewState.IndexBuffer->Direct3DIndexBuffer8,NewState.IndexBufferBase);
	else
		RenDev->DeferredState.SetIndices(NULL,0);

	RenDev->DeferredState.SetRenderState( RS_CULLMODE, NewState.CullMode );
	RenDev->DeferredState.SetRenderState( RS_AMBIENT, NewState.AmbientLightColor );

	//scion jg -- Clear any lights that are set
	RenDev->DeferredState.ResetLights();	

	//scion jg -- Removed the conditional since it would just pass through
	RenDev->DeferredState.SetRenderState( RS_FOGENABLE, NewState.DistanceFogEnabled);
	RenDev->DeferredState.SetRenderState( RS_FOGCOLOR, NewState.DistanceFogColor );
	RenDev->DeferredState.SetRenderState( RS_FOGSTART, *(DWORD*)(&NewState.DistanceFogStart) );
	RenDev->DeferredState.SetRenderState( RS_FOGEND, *(DWORD*)(&NewState.DistanceFogEnd) );

	// Restore material state.

	for(INT PassIndex = 0;PassIndex < OldState.NumMaterialPasses;PassIndex++)
	{
		if(--OldState.MaterialPasses[PassIndex]->NumRefs == 0)
			MaterialStatePool.FreeState(OldState.MaterialPasses[PassIndex]);

		OldState.MaterialPasses[PassIndex] = NULL;
	}

	CurrentState->CurrentMaterialState = NULL;

	STAT_INC(GStats.DWORDStats(RenDev->D3DStats.STATS_PopStateCalls));

	unguard;
}

/* scion ======================================================================
 * SetRenderTargetFast
 * Author: jg
 * 
 * Tries to use the optimized render target switching. If not it just sets the
 * render target using the normal method
 *
 * input:	pRenderTarget -- New color buffer to write to
 *			pNewZStencil -- new depth buffer to use
 *			pOldTarget -- The old render target to compare with
 *			RenDev -- The render device to use
 *
 * ============================================================================
 */
FORCEINLINE SetRenderTargetFast(IDirect3DSurface8* pRenderTarget,
	IDirect3DSurface8* pNewZStencil,IDirect3DSurface8* pOldTarget,
	UD3DRenderDevice* RenDev)
{
	guard(SetRenderTargetFast);
	HRESULT hr = E_FAIL;
#ifdef _XBOX
	// Compare the format settings for each render target
	UBOOL bSameColorFormat = pRenderTarget && pOldTarget &&
		pRenderTarget->Format == pOldTarget->Format;
	// Compare the size of each render target
	UBOOL bSameColorSize = pRenderTarget && pOldTarget &&
		pRenderTarget->Size == pOldTarget->Size;
	// If they match, use the fast setrendertarget call
	if (bSameColorFormat && bSameColorSize)
	{
		RenDev->Direct3DDevice8->SetRenderTargetFast(pRenderTarget,pNewZStencil,0);
		hr = S_OK;
	}
	else
#endif
	{
		hr = RenDev->Direct3DDevice8->SetRenderTarget(pRenderTarget,pNewZStencil);
	}
	return hr;
	unguard;
}


//
//	FD3DRenderInterface::SetRenderTarget
//
UBOOL FD3DRenderInterface::SetRenderTarget(FRenderTarget* InRenderTarget)
{
	guard(FD3DRenderInterface::SetRenderTarget);

	STAT_INC(GStats.DWORDStats(RenDev->D3DStats.STATS_SetRenderTargetCalls));
	FUNC_CLOCK(SetRT,GStats.DWORDStats(RenDev->D3DStats.STATS_SetRenderTargetCycles));

	FRenderTarget* UseRenderTarget = InRenderTarget;
	if( !UseRenderTarget )
	{
		UseRenderTarget = RenDev->DefaultRenderTarget;        
	}	

	//scion jg -- Completely rewrote the caching scheme for speed
	_WORD CacheId = UseRenderTarget->GetCacheId();
	FD3DTexture* D3DTexture = NULL;
	// Check to see if this item has been cached before
	if (CacheId != INVALID_CACHEID)
	{
		// It's been cached so find the item
		D3DTexture = (FD3DTexture*)RenDev->GetCachedResource(CacheId,CH_Texture);
//		checkSlow(D3DTexture);
	}
	// It's possible that the item was cached, then flushed so cache again
	// if needed
	if (D3DTexture == NULL)
	{
		// Allocate a new texture
		D3DTexture = NEW FD3DTexture(RenDev,UseRenderTarget);
		// Add it to the render device's cache
		RenDev->CacheResource(D3DTexture,CH_Texture);
		// Now propagate back the new cache ID
		UseRenderTarget->SetCacheId(D3DTexture->CacheId,RenDev);
	}
	checkSlow(D3DTexture);

	if(D3DTexture->CachedRevision != UseRenderTarget->GetRevision())
	{
		if(D3DTexture->Cache(UseRenderTarget))
		{
			return 0;
		}
	}

	// scion sz - if we are not using the default render target make sure to enable
	// color writes for everything including alpha
	if( UseRenderTarget == RenDev->DefaultRenderTarget )
		SetColorWriteMask( CWM_COLORWRITEENABLE_COLORONLY );
	else
		SetColorWriteMask( CWM_COLORWRITEENABLE_ALL );
	
	HRESULT	Result = E_FAIL;
	//scion jg -- Get the current state so we can determine if d3d needs updating
	FD3DRenderTargetState* pState = SavedRenderTargetState.GetCurrentState();
	// Push the new render target state on
	SavedRenderTargetState.PushRenderTarget(InRenderTarget,
		D3DTexture->RenderTargetSurface,D3DTexture->DepthStencilSurface);
	// Push the new viewport state too
	SetViewport(0,0,D3DTexture->CachedWidth,D3DTexture->CachedHeight);
	// Only update d3d if valid and there was a change. A valid depth buffer is
	// not needed, as L8 render targets do not have depth buffers.
	if (D3DTexture->RenderTargetSurface && 
		(pState->RenderTargetSurface != D3DTexture->RenderTargetSurface ||
		pState->DepthStencilSurface != D3DTexture->DepthStencilSurface))
	{
		Result = SetRenderTargetFast(D3DTexture->RenderTargetSurface,
			D3DTexture->DepthStencilSurface,pState->RenderTargetSurface,
			RenDev);
		//scion jg -- Attempt to catch nasty GHE errors better
		SINGLESTEPPUSHER;

		if( FAILED(Result) )
			appErrorf(TEXT("SetRenderTarget failed(%s)."),*D3DError(Result));

	}
	return SUCCEEDED(Result);
	unguard;
}

/* scion ======================================================================
 * FD3DRenderInterface::ResetRenderTarget
 * Author: jg
 * 
 * Restores the previous render target state
 *
 * ============================================================================
 */
void FD3DRenderInterface::ResetRenderTarget(void)
{
	guard(FD3DRenderInterface::ResetRenderTarget);
	STAT_INC(GStats.DWORDStats(RenDev->D3DStats.STATS_SetRenderTargetCalls));
	FUNC_CLOCK(SetRT,GStats.DWORDStats(RenDev->D3DStats.STATS_SetRenderTargetCycles));
	// Now get the old render target state
	FD3DRenderTargetState* pOldState = SavedRenderTargetState.GetCurrentState();
	// Pop the last state off
	SavedRenderTargetState.PopRenderTarget();
	// Now get the new render target state
	FD3DRenderTargetState* pNewState = SavedRenderTargetState.GetCurrentState();
	// Restore the color write mask as needed
	SetColorWriteMask(pNewState->RenderTarget == RenDev->DefaultRenderTarget ?
		CWM_COLORWRITEENABLE_COLORONLY : CWM_COLORWRITEENABLE_ALL);
	// Only update d3d if valid and there was a change
	if (pNewState->RenderTargetSurface != pOldState->RenderTargetSurface ||
		pNewState->DepthStencilSurface != pOldState->DepthStencilSurface)
	{
		HRESULT Result = SetRenderTargetFast(pNewState->RenderTargetSurface,
			pNewState->DepthStencilSurface,pOldState->RenderTargetSurface,
			RenDev);
		//scion jg -- Attempt to catch nasty GHE errors better
		SINGLESTEPPUSHER;

		if( FAILED(Result) )
			appErrorf(TEXT("SetRenderTarget failed(%s)."),*D3DError(Result));

	}
	// Pop the new viewport state too
	ResetViewport();
	unguard;
}

/* scion ======================================================================
 * FD3DRenderInterface::IsDefaultRenderTarget
 * Author: jg
 * 
 * Used to determine if we are back buffer rendering or not
 *
 * ============================================================================
 */
UBOOL FD3DRenderInterface::IsDefaultRenderTarget(void)
{
	return SavedRenderTargetState.GetCurrentState()->RenderTarget ==
		RenDev->DefaultRenderTarget;
}


//
//  FD3DRenderInterface::SetViewport
//
void FD3DRenderInterface::SetViewport(INT X,INT Y,INT Width,INT Height)
{
	guard(FD3DRenderInterface::SetViewport);
	//scion jg -- Only update the viewport size if it is different
	// So get the current state
	FD3DViewportState* pOldState = SavedViewportState.GetCurrentState();
	// And compare that to the requested values
	if (pOldState->ViewportX != X || pOldState->ViewportY != Y ||
		pOldState->ViewportWidth != Width || pOldState->ViewportHeight != Height)
	{
		D3DVIEWPORT8	Viewport8;

		Viewport8.X		= X;
		Viewport8.Y		= Y;
		Viewport8.Width = Width;
		Viewport8.Height= Height;
		Viewport8.MinZ	= 0.0f;
		Viewport8.MaxZ	= 1.0f;

		RenDev->Direct3DDevice8->SetViewport(&Viewport8);
	}
	//Now push the new viewport size
	SavedViewportState.PushViewportSize(X,Y,Width,Height);

	unguard;
}

// scion sz -
// simple Xbox only interface for screen space offset
void FD3DRenderInterface::SetScreenSpaceOffset( FLOAT OffsetX, FLOAT OffsetY )
{
	guard(FD3DRenderInterface::SetScreenSpaceOffset);
#ifdef _XBOX
	RenDev->Direct3DDevice8->SetScreenSpaceOffset( OffsetX, OffsetY );
#endif
	unguard;
}

/* scion ======================================================================
 * FD3DRenderInterface::ResetViewport
 * Author: jg
 * 
 * Reset the pushed viewport changes
 *
 * ============================================================================
 */
void FD3DRenderInterface::ResetViewport(void)
{
	guard(FD3DRenderInterface::ResetViewport);
	// Get the old state that is being popped off
	FD3DViewportState* pOldState = SavedViewportState.GetCurrentState();
	// Pop the old viewport size
	SavedViewportState.PopViewportSize();
	// Get the current state after popping
	FD3DViewportState* pNewState = SavedViewportState.GetCurrentState();
	// If the two states differ, update to the new one
	if (pOldState->ViewportX != pNewState->ViewportX ||
		pOldState->ViewportY != pNewState->ViewportY ||
		pOldState->ViewportWidth != pNewState->ViewportWidth ||
		pOldState->ViewportHeight != pNewState->ViewportHeight)
	{
		D3DVIEWPORT8	Viewport8;

		Viewport8.X		= pNewState->ViewportX;
		Viewport8.Y		= pNewState->ViewportY;
		Viewport8.Width = pNewState->ViewportWidth;
		Viewport8.Height= pNewState->ViewportHeight;
		Viewport8.MinZ	= 0.0f;
		Viewport8.MaxZ	= 1.0f;

		RenDev->Direct3DDevice8->SetViewport(&Viewport8);
	}
	unguard;
}

//
//  FD3DRenderInterface::GetViewport
//
void FD3DRenderInterface::GetViewport(INT* X,INT* Y,INT* Width,INT* Height)
{
	guard(FD3DRenderInterface::GetViewport);
	// Get the current state
	FD3DViewportState* pState = SavedViewportState.GetCurrentState();

	if( X )	*X = pState->ViewportX;
	if( Y ) *Y = pState->ViewportY;
	if( Width ) *Width = pState->ViewportWidth;
	if( Height ) *Height = pState->ViewportHeight;
	
	unguard;
}

//
// FD3DRenderInterface::SetMaterialBlending
//
void FD3DRenderInterface::SetMaterialBlending( FD3DMaterialState* NewMaterialState, D3DCULL CullMode )
{
	guard(FD3DRenderInterface::SetMaterialBlending);

	FUNC_CLOCK(SetMB,GStats.DWORDStats(RenDev->D3DStats.STATS_SetMaterialBlendingCycles));

	if(CurrentState->CurrentMaterialState == NewMaterialState)
		return;

#ifdef _XBOX
	//jg -- Avoid the copying of the pixel shader microcode by using our
	// program directly
	D3DPIXELSHADERDEF* pPixelShader = RenDev->GetPixelShaderProgram(NewMaterialState->PixelShader);
	RenDev->DeferredState.SetPixelShader(pPixelShader);
#else
	FD3DPixelShader* pPixelShader = RenDev->GetPixelShader(NewMaterialState->PixelShader);
	RenDev->DeferredState.SetPixelShader( pPixelShader ? pPixelShader->Handle : NULL );
#endif

	RenDev->DeferredState.SetRenderState( RS_ALPHABLENDENABLE, NewMaterialState->AlphaBlending ? TRUE : FALSE );
	RenDev->DeferredState.SetRenderState( RS_ALPHAREF, NewMaterialState->AlphaRef );
// scion sz - no need to set this over and over
//	RenDev->DeferredState.SetRenderState( RS_ALPHAFUNC, D3DCMP_GREATER );
	RenDev->DeferredState.SetRenderState( RS_ALPHATESTENABLE, NewMaterialState->AlphaTest ? TRUE : FALSE );

	switch(NewMaterialState->FillMode)
	{
	case FM_Wireframe:
	case FM_Solid:
		RenDev->DeferredState.SetRenderState( RS_FILLMODE, NewMaterialState->FillMode==FM_Wireframe ? D3DFILL_WIREFRAME : D3DFILL_SOLID );
		RenDev->DeferredState.SetTextureStageState( 0, TSS_COLOROP, D3DTOP_MODULATE );
		break;
	case FM_FlatShaded:
		RenDev->DeferredState.SetTextureStageState( 0, TSS_COLOROP, D3DTOP_SELECTARG2 );
		RenDev->DeferredState.SetRenderState( RS_FILLMODE, D3DFILL_SOLID );
		break;
	}

	RenDev->DeferredState.SetRenderState( RS_ZFUNC,NewMaterialState->ZTest ? D3DCMP_LESSEQUAL : D3DCMP_ALWAYS );
	RenDev->DeferredState.SetRenderState( RS_ZWRITEENABLE, NewMaterialState->ZWrite ? TRUE : FALSE );
	RenDev->DeferredState.SetRenderState( RS_CULLMODE,NewMaterialState->TwoSided ? D3DCULL_NONE : CullMode );

	// Source/destination blending
	RenDev->DeferredState.SetRenderState( RS_SRCBLEND, NewMaterialState->SrcBlend );
	RenDev->DeferredState.SetRenderState( RS_DESTBLEND, NewMaterialState->DestBlend );
	// scion sz - blend operation
	RenDev->DeferredState.SetRenderState( RS_BLENDOP, NewMaterialState->BlendOp );


	// Texture Factor
	RenDev->DeferredState.SetRenderState( RS_TEXTUREFACTOR, NewMaterialState->TFactorColor );

	// Texture stages and blending
	for( INT s=0;s<NewMaterialState->StagesUsed;s++ )
	{
		if( NewMaterialState->Stages[s].Texture )
		{
			if ( NewMaterialState->Stages[s].Texture->Direct3DCubeTexture8 )
			{
				RenDev->DeferredState.SetTexture(s,NewMaterialState->Stages[s].Texture->Direct3DCubeTexture8);
				// set texture clamping
				RenDev->DeferredState.SetTextureStageState( s, TSS_ADDRESSU, NewMaterialState->Stages[s].TextureAddressU );
				RenDev->DeferredState.SetTextureStageState( s, TSS_ADDRESSV, NewMaterialState->Stages[s].TextureAddressV );
				RenDev->DeferredState.SetTextureStageState( s, TSS_ADDRESSW, NewMaterialState->Stages[s].TextureAddressW );
			}
			else
			{
				RenDev->DeferredState.SetTexture(s,NewMaterialState->Stages[s].Texture->Direct3DTexture8);
				// set texture clamping
				RenDev->DeferredState.SetTextureStageState( s, TSS_ADDRESSU, NewMaterialState->Stages[s].TextureAddressU );
				RenDev->DeferredState.SetTextureStageState( s, TSS_ADDRESSV, NewMaterialState->Stages[s].TextureAddressV );
			}
		}
		else
			RenDev->DeferredState.SetTexture(s,NULL);


		if( NewMaterialState->Stages[s].TextureMipLODBias < -100.f )
			NewMaterialState->Stages[s].TextureMipLODBias = RenDev->DefaultTexMipBias;
        RenDev->DeferredState.SetTextureStageState( s, TSS_MIPMAPLODBIAS, *(DWORD*)&NewMaterialState->Stages[s].TextureMipLODBias );

		// scion sz - all stage color/alpah ops can be ignored when using a pixel shader
		if( !pPixelShader )
		{
			// alpha/color ops
			RenDev->DeferredState.SetTextureStageState( s, TSS_COLOROP,NewMaterialState->Stages[s].ColorOp );
			RenDev->DeferredState.SetTextureStageState( s, TSS_ALPHAOP,NewMaterialState->Stages[s].AlphaOp );

			// alpha/color args
			RenDev->DeferredState.SetTextureStageState( s, TSS_COLORARG0, NewMaterialState->Stages[s].ColorArg0 );
			RenDev->DeferredState.SetTextureStageState( s, TSS_COLORARG1, NewMaterialState->Stages[s].ColorArg1 );
			RenDev->DeferredState.SetTextureStageState( s, TSS_COLORARG2, NewMaterialState->Stages[s].ColorArg2 );
			RenDev->DeferredState.SetTextureStageState( s, TSS_ALPHAARG0, NewMaterialState->Stages[s].AlphaArg0 );
			RenDev->DeferredState.SetTextureStageState( s, TSS_ALPHAARG1, NewMaterialState->Stages[s].AlphaArg1 );
			RenDev->DeferredState.SetTextureStageState( s, TSS_ALPHAARG2, NewMaterialState->Stages[s].AlphaArg2 );
			RenDev->DeferredState.SetTextureStageState( s, TSS_RESULTARG, NewMaterialState->Stages[s].ResultArg );
		}
		
		RenDev->DeferredState.SetTextureStageState( s, TSS_TEXCOORDINDEX, NewMaterialState->Stages[s].TexCoordIndex );
	
		if( NewMaterialState->Stages[s].TextureTransformsEnabled )
		{
			RenDev->DeferredState.SetTextureStageState( s, TSS_TEXTURETRANSFORMFLAGS, NewMaterialState->Stages[s].TexCoordCount );
			//scion jg -- Moved here so there is only one conditional
			RenDev->DeferredState.SetTransform( (ED3DTransformState)(TS_TEXTURE0 + s), &NewMaterialState->Stages[s].TextureTransformMatrix );
		}
		else
			RenDev->DeferredState.SetTextureStageState( s, TSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE );

#ifdef BUMPMAP_ENABLE
		if( NewMaterialState->Stages[s].BumpMapEnabled )
		{
			RenDev->DeferredState.SetTextureStageState( s, TSS_COLORSIGN, NewMaterialState->Stages[s].ColorSign );
			RenDev->DeferredState.SetTextureStageState( s, TSS_BUMPENVMAT00, NewMaterialState->Stages[s].BumpEnvMat00 );
			RenDev->DeferredState.SetTextureStageState( s, TSS_BUMPENVMAT01, NewMaterialState->Stages[s].BumpEnvMat01 );
			RenDev->DeferredState.SetTextureStageState( s, TSS_BUMPENVMAT10, NewMaterialState->Stages[s].BumpEnvMat10 );
			RenDev->DeferredState.SetTextureStageState( s, TSS_BUMPENVMAT11, NewMaterialState->Stages[s].BumpEnvMat11 );
			RenDev->DeferredState.SetTextureStageState( s, TSS_BUMPENVLSCALE, NewMaterialState->Stages[s].BumpEnvLScale );
			RenDev->DeferredState.SetTextureStageState( s, TSS_BUMPENVLOFFSET, NewMaterialState->Stages[s].BumpEnvLOffset );
		}
		else
		{
			RenDev->DeferredState.SetTextureStageState( s, TSS_COLORSIGN, 0 );
		}
#endif

		// scion sz -
		// texture filter settings min/mag/mip
		RenDev->DeferredState.SetTextureStageState( s, TSS_MINFILTER, NewMaterialState->Stages[s].MinFilter );
		RenDev->DeferredState.SetTextureStageState( s, TSS_MAGFILTER, NewMaterialState->Stages[s].MagFilter );
		RenDev->DeferredState.SetTextureStageState( s, TSS_MIPFILTER, NewMaterialState->Stages[s].MipFilter );
	}

	// scion sz - all stage color/alpah ops can be ignored when using a pixel shader
	if( !pPixelShader )
	{
		// disable all unused tex stages
		for( INT s=NewMaterialState->StagesUsed; s<(INT)RenDev->DeviceCaps8.MaxTextureBlendStages; s++ )
		{
			NewMaterialState->Stages[s].ColorOp	= D3DTOP_DISABLE;
			NewMaterialState->Stages[s].AlphaOp	= D3DTOP_DISABLE;
			NewMaterialState->Stages[s].Texture	= NULL;

			RenDev->DeferredState.SetTextureStageState( s, TSS_COLOROP, D3DTOP_DISABLE );
			RenDev->DeferredState.SetTextureStageState( s, TSS_ALPHAOP, D3DTOP_DISABLE );
			RenDev->DeferredState.SetTexture( s, NULL );
		}
	}
	else
	{
		// disable all unused tex stages
		for( INT s=NewMaterialState->StagesUsed; s<(INT)RenDev->DeviceCaps8.MaxTextureBlendStages; s++ )
		{
			RenDev->DeferredState.SetTexture( s, NULL );
		}
	}

	// scion sz -
	// set color write mask (used for emissive,distortion,etc)
	if( NewMaterialState->ColorWriteMask == CWM_COLORWRITEENABLE_NONE )
	{
		RenDev->DeferredState.SetRenderState( RS_COLORWRITEENABLE, CWM_COLORWRITEENABLE_NONE );
	}
	else
	{
		RenDev->DeferredState.SetRenderState( RS_COLORWRITEENABLE, CurrentState->ColorWriteMask | NewMaterialState->ColorWriteMask );
	}
	

	// scion sz -
	// enable/disable specular color output (used by projectors)
	RenDev->DeferredState.SetRenderState( RS_SPECULARENABLE, NewMaterialState->SpecularEnable );

	CurrentState->CurrentMaterialState = NewMaterialState;

#ifndef FINAL_RELEASE
	GStats.DWORDStats(RenDev->D3DStats.STATS_SetMaterialBlendingCalls)++;
#endif
	unguard;
}



//
//	FD3DRenderInterface::Clear
//
void FD3DRenderInterface::Clear(UBOOL UseColor,FColor Color,UBOOL UseDepth,FLOAT Depth,UBOOL UseStencil,DWORD Stencil)
{
	guard(FD3DRenderInterface::Clear);

	FUNC_CLOCK(ClearCyc,GStats.DWORDStats(RenDev->D3DStats.STATS_ClearCycles));

	// device clear
	RenDev->Direct3DDevice8->Clear(
		0,
		NULL,
		  (UseColor ? D3DCLEAR_TARGET : 0) 
		| (UseDepth ? D3DCLEAR_ZBUFFER : 0) 
#ifdef _XBOX
		// XBox doesn't expose a non stencil depth buffer format.
		| (((!RenDev->UseStencil && UseDepth) || (RenDev->UseStencil && UseStencil)) ? D3DCLEAR_STENCIL : 0),
#else
		| (((RenDev->UseStencil || GIsEditor) && UseStencil) ? D3DCLEAR_STENCIL : 0),
#endif
		Color,
		Depth,
		Stencil & 0xff
		);

#ifndef FINAL_RELEASE
	GStats.DWORDStats(RenDev->D3DStats.STATS_ClearCalls)++;
#endif
	unguard;
}

/* scion ======================================================================
* FD3DRenderInterface::SetColorWriteMask
* Author: sz
* see D3DRS_COLORWRITEENABLE
* ============================================================================
*/
void FD3DRenderInterface::SetColorWriteMask( DWORD ColorMask )
{
    guard(FD3DRenderInterface::SetColorWriteMask);
	CurrentState->ColorWriteMask = ColorMask;
	unguard;
}

//
//	FD3DRenderInterface::PushHit
//
void FD3DRenderInterface::PushHit(const BYTE* Data,INT Count)
{
	guard(UD3DRenderDevice::PushHit);

#ifndef _XBOX //scion jg -- Not used on xbox
	UViewport*	LockedViewport = RenDev->LockedViewport;
	HRESULT		Result;

	check(LockedViewport->HitYL<=HIT_SIZE);
	check(LockedViewport->HitXL<=HIT_SIZE);

	// Get the current render target surface.
	IDirect3DSurface8*	RenderTarget;

	Result = RenDev->Direct3DDevice8->GetRenderTarget(&RenderTarget);

	if( FAILED(Result) )
	{
		debugf(NAME_DevD3D,TEXT("D3D Driver: GetRenderTarget failed (%s)"),*D3DError(Result));
		return;
	}

	// Lock the render target.
	D3DLOCKED_RECT	LockedRect;

	Result = RenderTarget->LockRect(&LockedRect,NULL,0);

	if( FAILED(Result) )
	{
		debugf(NAME_DevD3D,TEXT("D3D Driver: LockRect failed (%s)"),*D3DError(Result));
		return;
	}

	// Save the passed info on the working stack.
	INT	Index = HitStack.Add(Count);

	appMemcpy(&HitStack(Index),Data,Count);

	// Cleanup under cursor.
	switch( LockedViewport->ColorBytes )
	{
		case 2:
		{
			_WORD* src = (_WORD*) LockedRect.pBits;
			src = (_WORD*) ((BYTE*)src + LockedViewport->HitX * 2 + LockedViewport->HitY * LockedRect.Pitch);
			for( INT Y=0; Y<LockedViewport->HitYL; Y++, src=(_WORD*)((BYTE*)src + LockedRect.Pitch) )
			{
				for( INT X=0; X<LockedViewport->HitXL; X++ )
				{
					HitPixels[X][Y] = src[X];
					src[X] = IGNOREPIX;
				}
			}
			break;
		}
		case 3:
		{
			BYTE* src = (BYTE*) LockedRect.pBits;
			src = src + LockedViewport->HitX*3  + LockedViewport->HitY * LockedRect.Pitch;
			for( INT Y=0; Y<LockedViewport->HitYL; Y++, src+=LockedRect.Pitch )
			{
				for( INT X=0; X<LockedViewport->HitXL; X++ )
				{
					HitPixels[X][Y] = *((DWORD*)&src[X*3]);
					*((DWORD*)&src[X*3]) = IGNOREPIX;
				}
			}			
			break;
		}
		case 4:
		{
			DWORD* src = (DWORD*) LockedRect.pBits;
			src = (DWORD*)((BYTE*)src + LockedViewport->HitX * 4 + LockedViewport->HitY * LockedRect.Pitch);
			for( INT Y=0; Y<LockedViewport->HitYL; Y++, src=(DWORD*)((BYTE*)src + LockedRect.Pitch) )
			{
				for( INT X=0; X<LockedViewport->HitXL; X++ )
				{
					HitPixels[X][Y] = src[X];
					src[X] = IGNOREPIX;
				}
			}
			break;
		}
	
	}

	// Unlock the render target, and release our reference to it.
	RenderTarget->UnlockRect();
	RenderTarget->Release();
#endif
	unguard;
}

//
//	FD3DRenderInterface::PopHit
//
void FD3DRenderInterface::PopHit(INT Count,UBOOL Force)
{
	guard(UD3DRenderDevice::PopHit);

#ifndef _XBOX //scion jg -- Not used on xbox
	UViewport*	LockedViewport = RenDev->LockedViewport;
	HRESULT		Result;

	//debugf(NAME_DevD3D,TEXT("POPHIT stacknum   %i  Count %i "),HitStack.Num(),Count);
	check(Count <= HitStack.Num());
	UBOOL Hit=0;
	FColor HitColor = FColor(0,0,0);

	// Get the current render target surface.
	IDirect3DSurface8*	RenderTarget;

	Result = RenDev->Direct3DDevice8->GetRenderTarget(&RenderTarget);

	if( FAILED(Result) )
	{
		debugf(NAME_DevD3D,TEXT("D3D Driver: GetRenderTarget failed (%s)"),*D3DError(Result));
		return;
	}

	// Lock the render target.
	D3DLOCKED_RECT	LockedRect;

	Result = RenderTarget->LockRect(&LockedRect,NULL,0);

	if( FAILED(Result) )
	{
		debugf(NAME_DevD3D,TEXT("D3D Driver: LockRect failed (%s)"),*D3DError(Result));
		return;
	}

	// Check under cursor and restore.
	switch( LockedViewport->ColorBytes )
	{
		case 2:
		{
			_WORD* src = (_WORD*) LockedRect.pBits;
			src = (_WORD*) ((BYTE*)src + LockedViewport->HitX * 2 + LockedViewport->HitY * LockedRect.Pitch);
			for( INT Y=0; Y<LockedViewport->HitYL; Y++, src=(_WORD*)((BYTE*)src + LockedRect.Pitch) )
			{
				for( INT X=0; X<LockedViewport->HitXL; X++ )
				{
					if( src[X] != IGNOREPIX && !Hit)
					{
						HitColor = FColor( (src[X]>>11)<<3, ((src[X]>>6)&0x3f)<<2, (src[X]&0x1f)<<3 );
						Hit=1;
					}
					src[X] = (_WORD)HitPixels[X][Y];	
				
				}
			}
			break;
		}
		case 3:
		{
			BYTE* src = (BYTE*) LockedRect.pBits;
			src = src + LockedViewport->HitX*3  + LockedViewport->HitY * LockedRect.Pitch;
			for( INT Y=0; Y<LockedViewport->HitYL; Y++, src+=LockedRect.Pitch )
			{
				for( INT X=0; X<LockedViewport->HitXL; X++ )
				{
					if( *((DWORD*)&src[X*3]) != IGNOREPIX && !Hit )
					{
						HitColor = FColor( src[X*3]+2, src[X*3]+1, src[X*3] );
						Hit=1;
					}
					*((DWORD*)&src[X*3]) = HitPixels[X][Y];						
				}
			}			
			break;
		}
		case 4:
		{
			DWORD* src = (DWORD*) LockedRect.pBits;
			src = (DWORD*)((BYTE*)src + LockedViewport->HitX * 4 + LockedViewport->HitY * LockedRect.Pitch);
			for( INT Y=0; Y<LockedViewport->HitYL; Y++, src=(DWORD*)((BYTE*)src + LockedRect.Pitch) )
			{
				for( INT X=0; X<LockedViewport->HitXL; X++ )
				{						
					if ( src[X] != IGNOREPIX  && !Hit ) 
					{
						HitColor = FColor( (src[X]>>16)&0xff, (src[X]>>8)&0xff, (src[X])&0xff );
						Hit=1;
					}
					src[X] = HitPixels[X][Y];
				}
			}
			break;
		}		
	}

	// Unlock the render target, and release our reference to it.
	RenderTarget->UnlockRect();
	RenderTarget->Release();

	// Handle hit.
	if( Hit || Force )
	{
		((HHitProxy*)&HitStack(HitStack.Num()-Count))->HitColor = HitColor;
		if( HitStack.Num() <= *HitSize )
		{
			HitCount = HitStack.Num();
			appMemcpy( HitData, &HitStack(0), HitCount );
		}
		else HitCount = 0;
	}
	// Remove the passed info from the working stack.
	HitStack.Remove( HitStack.Num()-Count, Count );
#endif
	unguard;
}

//
//	FD3DRenderInterface::SetCullMode
//
void FD3DRenderInterface::SetCullMode(ECullMode CullMode)
{
	guard(FD3DRenderInterface::SetCullMode);

	// Determine the actual cull mode to use.
	D3DCULL	NewMode = D3DCULL_NONE;

	if(CullMode == CM_CW)
		NewMode = D3DCULL_CW;
	else if(CullMode == CM_CCW)
		NewMode = D3DCULL_CCW;
	else if(CullMode == CM_None)
		NewMode = D3DCULL_NONE;

	// Set the new cull mode.
	RenDev->DeferredState.SetRenderState( RS_CULLMODE, NewMode );
	CurrentState->CullMode = NewMode;

	unguard;
}

ECullMode FD3DRenderInterface::GetCullMode()
{
	ECullMode CullMode = CM_None;

	if( CurrentState->CullMode == D3DCULL_CW )
		CullMode = CM_CW;
	else if( CurrentState->CullMode == D3DCULL_CCW )
		CullMode = CM_CCW;
	else
		CullMode = CM_None;

	return CullMode;
}

//
//	FD3DRenderInterface::SetAmbientLight
//
void FD3DRenderInterface::SetAmbientLight(FColor Color)
{
	guard(FD3DRenderInterface::SetAmbientLight);

	RenDev->DeferredState.SetRenderState( RS_AMBIENT, Color );
	CurrentState->AmbientLightColor = Color;

	unguard;
}

//
//	FD3DRenderInterface::EnableLighting
//
void FD3DRenderInterface::EnableLighting(UBOOL UseDynamic, UBOOL UseStatic, UBOOL Modulate2X, FBaseTexture* LightmapTexture, UBOOL LightingOnly,const FSphere& LitSphere )
{
	guard(FD3DRenderInterface::EnableLighting);

	FD3DTexture* Lightmap = NULL;
	if( LightmapTexture )
		Lightmap = CacheTexture( LightmapTexture );

	CurrentState->Lightmap		= Lightmap;		
	CurrentState->LightingOnly	= LightingOnly;

	RenDev->DeferredState.SetRenderState( RS_LIGHTING, UseDynamic );
	CurrentState->UseDynamicLighting = UseDynamic;

	RenDev->DeferredState.SetRenderState( RS_COLORVERTEX, UseStatic );
	CurrentState->UseStaticLighting	= UseStatic;

	CurrentState->LightingModulate2X = Modulate2X;

	CurrentState->LitSphere = LitSphere;

	unguard;
}

//
//	UnrealAttenuation
//
static FLOAT UnrealAttenuation(FLOAT Distance,FLOAT Radius)
{
	if(Distance <= Radius)
	{
		FLOAT	A = Distance / Radius,					// Unreal's lighting model.
				B = (2 * A * A * A - 3 * A * A + 1);

		// scion sz - removed 2X from distance attenuation
		return B;	/* B / A * A * 2.0 */
	}
	else
		return 0.0f;
}

/* scion ======================================================================
* FD3DRenderInterface::SetLight
* Author: Epic
* 
* Use light information to set a hardware light.
* Point lights are automatically converted to directional lights when there
* are no more point light spots.  
* =============================================================================
*/
void FD3DRenderInterface::SetLight(INT LightIndex,FDynamicLight* Light,FLOAT Scale)
{
	guardSlow(FD3DRenderInterface::SetLight);
	checkSlow( LightIndex < Min<INT>(8,RenDev->DeviceCaps8.MaxActiveLights) || (Light && Light->LightEffect == LE_Ambient) );	
	
	// scion sz - ambient light type for adding to ambient term
	if( Light && Light->LightEffect == LE_Ambient )
	{
		RenDev->DeferredState.SetRenderState( RS_AMBIENT, CurrentState->AmbientLightColor + FColor(Light->Color * (Light->Alpha*0.5f*Scale)) );
		return;
	}

	UBOOL bEnableLight=0;
	if(Light)
	{
		if( LightIndex==0 )	{
			// scion sz - reset light type totals
			appMemzero( LightTypeTotals, sizeof(LightTypeTotals) );
		}
		INT TotalDirLts = GetLightTypeCount(D3DLIGHT_DIRECTIONAL);
		INT TotalPointLts = GetLightTypeCount(D3DLIGHT_POINT);
		static INT MaxDirLts = VS_Lights_Directional_MAX;
		static INT MaxPtLts = VS_Lights_Point_MAX;

		if( CurrentState->LightingModulate2X ) {
			Scale *= 0.5f;
		}
		// scion sz - see if we can add any more lights
		if( TotalDirLts < MaxDirLts || TotalPointLts < MaxPtLts )
		{
			//scion jg -- Only the wanted state contains lights now, so get
			// the memory from there
			D3DLIGHT8*	LightD3D = RenDev->DeferredState.GetLight(LightIndex);
			appMemzero(LightD3D,sizeof(D3DLIGHT8));
			
			BYTE LightEffect = Light->LightEffect;			

			if( LightEffect == LE_Sunlight || LightEffect == LE_Directional )
			{
				// only add the light if we are not over the limit
				if( TotalDirLts < MaxDirLts )
				{
					LightD3D->Type			= D3DLIGHT_DIRECTIONAL;
					LightD3D->Direction.x	= Light->Direction.X;
					LightD3D->Direction.y	= Light->Direction.Y;
					LightD3D->Direction.z	= Light->Direction.Z;

					if( LightEffect == LE_Sunlight )
					{
						Scale *= 1.75f;
					}

					// valid
					bEnableLight = 1;
				}				
			}
			// only process point lights if we have slots available for TRUE point lights
			else if( TotalPointLts < MaxPtLts )
			{
				LightD3D->Position.x	= Light->Position.X;
				LightD3D->Position.y	= Light->Position.Y;
				LightD3D->Position.z	= Light->Position.Z;

				if( LightEffect == LE_QuadraticNonIncidence || CurrentState->LitSphere.W == -1.0f )
				{
					LightD3D->Type			= D3DLIGHT_POINT;
					LightD3D->Attenuation0 = 0.0f;
					LightD3D->Attenuation1 = 0.0f;
					LightD3D->Attenuation2 = 8.0f / Square(Light->Radius);
					LightD3D->Range = Light->Radius;
				}
				// Spot Lights
				else if( LightEffect == LE_StaticSpot || LightEffect == LE_Spotlight ) 
				{
					LightD3D->Type			= D3DLIGHT_SPOT;
					LightD3D->Direction.x	= Light->Direction.X;
					LightD3D->Direction.y	= Light->Direction.Y;
					LightD3D->Direction.z	= Light->Direction.Z;					
					// Outer cone light limit. Fudge factor 1.2f makes it match lightmaps better with the current falloff.
					LightD3D->Phi			= 1.15f * appAcos( Square( 1.f - (FLOAT)(Light->LightCone)/256.f ) ); 
					// Inner cone.
					LightD3D->Theta			= 0.05f * LightD3D->Phi ; // Pretty arbitrary..
					// Outer-to-inner-cone falloff
					// higher: slower at center, faster at edge; lower = faster falloff at center.
					LightD3D->Falloff		= 1.00f; //  1.0f is linear, and supposed to be fastest for most hardware. 2.0f gives better gradual falloff though.

					// TODO: Special attenuation and range setting for spotlights - to more closely match software-computed spot lights on BSP and terrain ?
					FLOAT		CenterDistance = Max(0.1f,(CurrentState->LitSphere - Light->Position).Size()),
						MaxDistance = Clamp(CenterDistance + CurrentState->LitSphere.W,Light->Radius * 0.05f,Light->Radius * 0.9f),
						MinDistance = Clamp(CenterDistance - CurrentState->LitSphere.W,Light->Radius * 0.1f,Light->Radius * 0.95f),
						MinAttenuation = 1.0f / UnrealAttenuation(MinDistance,Light->Radius),
						MaxAttenuation = 1.0f / UnrealAttenuation(MaxDistance,Light->Radius);

					//!!vogel: fix for D3D debug runtime on XBox
					if( Abs(MinAttenuation - MaxAttenuation) < SMALL_NUMBER )
					{
						LightD3D->Attenuation0 = MinAttenuation;
						LightD3D->Attenuation1 = 0.0f;
					}
					else
					{
						LightD3D->Attenuation0 = Max(0.01f,MinAttenuation - (MaxAttenuation - MinAttenuation) / (MaxDistance - MinDistance) * MinDistance);
						LightD3D->Attenuation1 = Max(0.0f,(MinAttenuation - LightD3D->Attenuation0) / MinDistance);
					}

					LightD3D->Attenuation2 = 0.0f;					
					LightD3D->Range = (256.0f - LightD3D->Attenuation0) / Max(0.01f,LightD3D->Attenuation1);
				}
				else // Point lights.
				{	
					LightD3D->Type			= D3DLIGHT_POINT;					
					// Attempt to approximate Unreal's light attenuation using the provided attenuation factors.
					// This is REALLY limited by D3D forcing positive attenuation factors. :(
					FLOAT		CenterDistance = Max(0.1f,(CurrentState->LitSphere - Light->Position).Size()),
						MaxDistance = Clamp(CenterDistance + CurrentState->LitSphere.W,Light->Radius * 0.05f,Light->Radius * 0.9f),
						MinDistance = Clamp(CenterDistance - CurrentState->LitSphere.W,Light->Radius * 0.1f,Light->Radius * 0.95f),
						MinAttenuation = 1.0f / UnrealAttenuation(MinDistance,Light->Radius),
						MaxAttenuation = 1.0f / UnrealAttenuation(MaxDistance,Light->Radius);

					//!!vogel: fix for D3D debug runtime on XBox
					if( Abs(MinAttenuation - MaxAttenuation) < SMALL_NUMBER )
					{
						LightD3D->Attenuation0 = MinAttenuation;
						LightD3D->Attenuation1 = 0.0f;
					}
					else
					{
						LightD3D->Attenuation0 = Max(0.01f,MinAttenuation - (MaxAttenuation - MinAttenuation) / (MaxDistance - MinDistance) * MinDistance);
						LightD3D->Attenuation1 = Max(0.0f,(MinAttenuation - LightD3D->Attenuation0) / MinDistance);
					}

					LightD3D->Attenuation2 = 0.0f;					
					LightD3D->Range = (256.0f - LightD3D->Attenuation0) / Max(0.01f,LightD3D->Attenuation1);
				}
				
				static float MaxD3DRange = appSqrt(FLT_MAX);
				LightD3D->Range = Clamp<FLOAT>( LightD3D->Range, 0.0f, MaxD3DRange );

				LightD3D->Attenuation0 = Max( 0.f, LightD3D->Attenuation0 );
				LightD3D->Attenuation1 = Max( 0.f, LightD3D->Attenuation1 );
				LightD3D->Attenuation2 = Max( 0.f, LightD3D->Attenuation2 );

				// valid
				bEnableLight = 1;
			}
			// scion sz - try to approximate this light with just a directional
			else if( TotalDirLts < MaxDirLts )
			{
				LightD3D->Type			= D3DLIGHT_DIRECTIONAL;
				LightD3D->Direction.x	= CurrentState->LitSphere.X - Light->Position.X;
				LightD3D->Direction.y	= CurrentState->LitSphere.Y - Light->Position.Y;
				LightD3D->Direction.z	= CurrentState->LitSphere.Z - Light->Position.Z;
					
				// scale light color by dist attenuation amount
				Scale *= Light->SampleLightAtten( CurrentState->LitSphere );

				bEnableLight = 1;
			}

			if( bEnableLight )
			{
				LightD3D->Diffuse.r = Light->Color.X * Light->Alpha * Scale;
				LightD3D->Diffuse.g = Light->Color.Y * Light->Alpha * Scale;
				LightD3D->Diffuse.b = Light->Color.Z * Light->Alpha * Scale;
				LightD3D->Diffuse.a = 1.0f;

				IncrementLightType( LightD3D->Type );
			}
		}
	}
	
	// enable/disable light
	RenDev->DeferredState.LightEnable(LightIndex,bEnableLight);

	unguardSlow;
}

//
//	FD3DRenderInterface::SetGlobalColor
//
void FD3DRenderInterface::SetGlobalColor(FColor Color)
{
	guard(FD3DRenderInterface::SetGlobalColor);

	CurrentState->MaterialPasses[CurrentState->NumMaterialPasses-1]->TFactorColor = Color;
	RenDev->DeferredState.SetRenderState( RS_TEXTUREFACTOR, Color );

	unguard;
}


//
//	FD3DRenderInterface::SetNPatchTesselation
//
void FD3DRenderInterface::SetNPatchTesselation( FLOAT Tesselation )
{
	guard(FD3DRenderInterface::SetNPatchTesselation);

	if( (Tesselation >= 1.f) && RenDev->UseNPatches )
		Tesselation = Max(1.f, Tesselation * RenDev->TesselationFactor );
	else
		Tesselation = 1.f;
	//	CurrentState->NPatchTesselation = Tesselation;
	RenDev->DeferredState.SetRenderState( RS_PATCHSEGMENTS, *(DWORD*)(&Tesselation) );

	unguard;
}


//
//	FD3DRenderInterface::SetDistanceFog
//
void FD3DRenderInterface::SetDistanceFog(UBOOL Enable,FLOAT FogStart,FLOAT FogEnd,FColor FogColor)
{
	guard(FD3DRenderInterface::SetDistanceFog);

	if(Enable)
	{
		RenDev->DeferredState.SetRenderState( RS_FOGENABLE, TRUE );
		CurrentState->DistanceFogEnabled = Enable;

		// Set the fog color.
		RenDev->DeferredState.SetRenderState( RS_FOGCOLOR, FogColor );
		CurrentState->DistanceFogColor = FogColor;

		// Workaround for R100 6166 drivers which don't handle fogstart == fogend correctly with vertex fog.
		if( Abs(FogStart - FogEnd) < 1.f )
			FogStart = Max( 0.f, FogEnd - 1.f );

		// Set fog parameters.
		CurrentState->DistanceFogStart	= FogStart;
		CurrentState->DistanceFogEnd	= FogEnd;

		if( !RenDev->UseVertexFog && !(RenDev->DeviceCaps8.RasterCaps & D3DPRASTERCAPS_WFOG) )
		{
			FogStart /= FAR_CLIPPING_PLANE;
			FogEnd /= FAR_CLIPPING_PLANE;
		}

		RenDev->DeferredState.SetRenderState( RS_FOGSTART, *(DWORD*)(&FogStart) );
		RenDev->DeferredState.SetRenderState( RS_FOGEND, *(DWORD*)(&FogEnd) );
	}
	else
	{
		RenDev->DeferredState.SetRenderState( RS_FOGENABLE, FALSE );
		CurrentState->DistanceFogEnabled = Enable;
	}

	unguard;
}

//
//	FD3DRenderInterface::SetTransform
//
void FD3DRenderInterface::SetTransform(ETransformType Type,const FMatrix& Matrix)
{
	guard(FD3DRenderInterface::SetTransform);

	if(Type == TT_LocalToWorld)
	{
		RenDev->DeferredState.SetTransform(TS_WORLD,(D3DMATRIX*) &Matrix);		
		CurrentState->LocalToWorld = Matrix;

		// scion sz - 
		// Local to World Inverse required for vertex shader lighting
		D3DXMatrixInverse( (D3DXMATRIX*)RenDev->DeferredState.GetTransform(TS_WORLDINV), NULL, (D3DXMATRIX*)&Matrix);
	}
	else if(Type == TT_WorldToCamera)
	{
		CurrentState->WorldToCamera = Matrix;
		RenDev->DeferredState.SetTransform(TS_VIEW,(D3DMATRIX*) &Matrix);
	}
	else if(Type == TT_CameraToScreen)
	{
		CurrentState->CameraToScreen = Matrix;
		RenDev->DeferredState.SetTransform(TS_PROJECTION,(D3DMATRIX*) &Matrix);        
	}

	unguard;
}

//
//	FD3DRenderInterface::SetZBias
//
void FD3DRenderInterface::SetZBias(INT ZBias)
{
	guard(FD3DRenderInterface::SetZBias);
	ZBias *= 5;
	if( CurrentState->ZBias != ZBias )
		RenDev->DeferredState.SetRenderState( RS_ZBIAS, ZBias );
	CurrentState->ZBias = ZBias;

	unguard;
}

/* scion ======================================================================
 * SetStencilState
 * Author: jg
 * 
 * Sets all of the states for stencil in the deferred state
 *
 * input:	RenDev -- The render device to change
 *			pState -- The new block of stencil states to set
 *
 * ============================================================================
 */
FORCEINLINE void SetStencilState(UD3DRenderDevice* RenDev,
	FD3DStencilState* pState)
{
	RenDev->DeferredState.SetRenderState(RS_STENCILFUNC,
		pState->StencilTest);
	RenDev->DeferredState.SetRenderState(RS_STENCILREF,
		pState->StencilRef & 0xff);
	RenDev->DeferredState.SetRenderState(RS_STENCILMASK,
		pState->StencilMask & 0xff);
	RenDev->DeferredState.SetRenderState(RS_STENCILFAIL,
		pState->StencilFailOp);
	RenDev->DeferredState.SetRenderState(RS_STENCILZFAIL,
		pState->StencilZFailOp);
	RenDev->DeferredState.SetRenderState(RS_STENCILPASS,
		pState->StencilPassOp);
	RenDev->DeferredState.SetRenderState(RS_STENCILWRITEMASK,
		pState->StencilWriteMask & 0xff);
	// Mark it as dirty so it gets updated in commit
	RenDev->DeferredState.SetStencilDirty();
}

/* scion ======================================================================
 * FD3DRenderInterface::SetStencilOp
 * Author: jg
 * 
 * Pushes new stencil states onto the stencil stack
 *
 * ============================================================================
 */
void FD3DRenderInterface::SetStencilOp(ECompareFunction Test,DWORD Ref,
	DWORD Mask,EStencilOp FailOp,EStencilOp ZFailOp,EStencilOp PassOp,
	DWORD WriteMask)
{
	guard(FD3DRenderInterface::SetStencilOp);
	//scion jg -- Totally rewrote handling to push/pop stencil state less often
	SavedStencilState.PushStencilState(Test,Ref,Mask,FailOp,ZFailOp,
		PassOp,WriteMask);
	// Now update the deferred state
	SetStencilState(RenDev,SavedStencilState.GetCurrentState());
	unguard;
}

/* scion ======================================================================
 * FD3DRenderInterface::ResetStencilOp
 * Author: jg
 * 
 * Restores the previous stencil render states
 *
 * ============================================================================
 */
void FD3DRenderInterface::ResetStencilOp(void)
{
	guard(FD3DRenderInterface::ResetStencilOp);
	// Have it change the deferred renderstate
	SavedStencilState.PopStencilState();
	// Now update the deferred state
	SetStencilState(RenDev,SavedStencilState.GetCurrentState());
	unguard;
}

/* scion ======================================================================
 * FD3DRenderInterface::EnableStencil
 * Author: jg
 * 
 * Enables/disables stencil
 *
 * input:	bEnable -- whether to enable stencil or not
 *
 * ============================================================================
 */
void FD3DRenderInterface::EnableStencil(UBOOL bEnable)
{
	guard(FD3DRenderInterface::EnableStencil);
	// Mark it as dirty so it gets updated in commit
	RenDev->DeferredState.SetRenderState(RS_STENCILENABLE,bEnable);
	RenDev->DeferredState.SetStencilDirty();
	unguard;
}

/* scion ======================================================================
 * FD3DRenderInterface::EnableDepth
 * Author: jg
 * 
 * Enables/disables the depth buffer
 *
 * input:	bEnable -- whether to enable Z or not
 *
 * ============================================================================
 */
void FD3DRenderInterface::EnableDepth(UBOOL bEnable)
{
	guard(FD3DRenderInterface::EnableDepth);
	// Set the new state values
	RenDev->DeferredState.SetRenderState(RS_ZENABLE,bEnable);
	RenDev->DeferredState.SetRenderState(RS_ZWRITEENABLE,bEnable);
	unguard;
}

//
//  FD3DRenderInterface::SetPrecacheMode
//
void FD3DRenderInterface::SetPrecacheMode( EPrecacheMode InPrecacheMode )
{
	PrecacheMode = InPrecacheMode;
}

/* scion ======================================================================
 * FD3DRenderInterface::GetPrecacheMode
 * Author: jg
 * 
 * Returns the current precache mode
 *
 * ============================================================================
 */
EPrecacheMode FD3DRenderInterface::GetPrecacheMode(void)
{
	return PrecacheMode;
}

/*----------------------------------------------------------------------------
	CacheTexture.
----------------------------------------------------------------------------*/

FD3DTexture* FD3DRenderInterface::CacheTexture( FBaseTexture* Texture )
{
	guard(FD3DRenderInterface::CacheTexture);

	// Cache the texture
	//scion jg -- Completely rewrote the caching scheme for speed
	_WORD CacheId = Texture->GetCacheId();
	FD3DTexture* D3DTexture = NULL;
	// Check to see if this item has been cached before
	if (CacheId != INVALID_CACHEID)
	{
		// It's been cached so find the item
		D3DTexture = (FD3DTexture*)RenDev->GetCachedResource(CacheId,CH_Texture);
//		checkSlow(D3DTexture);
	}
	// It's possible that the item was cached, then flushed so cache again
	// if needed
	if (D3DTexture == NULL)
	{
		// Allocate a new texture
		D3DTexture = NEW FD3DTexture(RenDev,Texture);
		// Add it to the render device's cache
		RenDev->CacheResource(D3DTexture,CH_Texture);
		// Now propagate back the new cache ID
		Texture->SetCacheId(D3DTexture->CacheId,RenDev);
	}
	checkSlow(D3DTexture);

	if(D3DTexture->CachedRevision != Texture->GetRevision())
		D3DTexture->Cache(Texture);

	D3DTexture->LastFrameUsed = RenDev->FrameCounter;
	
	return D3DTexture;
	unguard;
}

//
//	FD3DRenderInterface::DescribeArg
//

FString FD3DRenderInterface::DescribeArg(FD3DMaterialState* MaterialState,INT StageIndex,DWORD Arg,UBOOL Alpha)
{
	if((Arg & D3DTA_SELECTMASK) == D3DTA_CURRENT)
		return FString::Printf(TEXT("(%s)"),*DescribeStage(MaterialState,StageIndex - 1,Alpha));
	else if((Arg & D3DTA_SELECTMASK) == D3DTA_TEXTURE)
		return FString::Printf(TEXT("texture(%u)"),StageIndex);
	else if((Arg & D3DTA_SELECTMASK) == D3DTA_DIFFUSE)
		return TEXT("diffuse");
	else if((Arg & D3DTA_SELECTMASK) == D3DTA_SPECULAR)
		return TEXT("specular");
	else if((Arg & D3DTA_SELECTMASK) == D3DTA_TFACTOR)
	{
		FColor	C(MaterialState->TFactorColor);
		return FString::Printf(TEXT("tfactor(%u,%u,%u,%u)"),C.R,C.G,C.B,C.A);
	}
	else
		return TEXT("?");
}

//
//	FD3DRenderInterface::DescribeStage
//

FString FD3DRenderInterface::DescribeStage(FD3DMaterialState* MaterialState,INT StageIndex,UBOOL Alpha)
{
	if(StageIndex < 0)
		return TEXT("diffuse");
	else
	{
		FD3DMaterialStateStage*	Stage = &MaterialState->Stages[StageIndex];
		DWORD					Op = Alpha ? Stage->AlphaOp : Stage->ColorOp,
								Arg1 = Alpha ? Stage->AlphaArg1 : Stage->ColorArg1,
								Arg2 = Alpha ? Stage->AlphaArg2 : Stage->ColorArg2;

		switch(Op)
		{
		case D3DTOP_SELECTARG1:
			return DescribeArg(MaterialState,StageIndex,Arg1,Alpha);
		case D3DTOP_SELECTARG2:
			return DescribeArg(MaterialState,StageIndex,Arg2,Alpha);
		case D3DTOP_MODULATE:
			return FString::Printf(TEXT("%s * %s"),*DescribeArg(MaterialState,StageIndex,Arg1,Alpha),*DescribeArg(MaterialState,StageIndex,Arg2,Alpha));
		case D3DTOP_MODULATE2X:
			return FString::Printf(TEXT("%s * %s * 2"),*DescribeArg(MaterialState,StageIndex,Arg1,Alpha),*DescribeArg(MaterialState,StageIndex,Arg2,Alpha));
		case D3DTOP_MODULATE4X:
			return FString::Printf(TEXT("%s * %s * 4"),*DescribeArg(MaterialState,StageIndex,Arg1,Alpha),*DescribeArg(MaterialState,StageIndex,Arg2,Alpha));
		case D3DTOP_ADD:
			return FString::Printf(TEXT("%s + %s"),*DescribeArg(MaterialState,StageIndex,Arg1,Alpha),*DescribeArg(MaterialState,StageIndex,Arg2,Alpha));
		case D3DTOP_ADDSIGNED:
			return FString::Printf(TEXT("%s + %s - 0.5"),*DescribeArg(MaterialState,StageIndex,Arg1,Alpha),*DescribeArg(MaterialState,StageIndex,Arg2,Alpha));
		case D3DTOP_ADDSIGNED2X:
			return FString::Printf(TEXT("(%s + %s - 0.5) * 2"),*DescribeArg(MaterialState,StageIndex,Arg1,Alpha),*DescribeArg(MaterialState,StageIndex,Arg2,Alpha));
		case D3DTOP_SUBTRACT:
			return FString::Printf(TEXT("%s - %s"),*DescribeArg(MaterialState,StageIndex,Arg1,Alpha),*DescribeArg(MaterialState,StageIndex,Arg2,Alpha));
		case D3DTOP_MODULATEALPHA_ADDCOLOR:
			return FString::Printf(TEXT("%s.rgb + %s.a * %s.rgb"),*DescribeArg(MaterialState,StageIndex,Arg1,0),*DescribeArg(MaterialState,StageIndex,Arg1,1),*DescribeArg(MaterialState,StageIndex,Arg2,0));
		case D3DTOP_BLENDTEXTUREALPHA:
			return FString::Printf(TEXT("%s * texture(%u).a + %s * (1 - texture(%u).a)"),*DescribeArg(MaterialState,StageIndex,Arg1,Alpha),StageIndex,*DescribeArg(MaterialState,StageIndex,Arg2,Alpha),StageIndex);
		case D3DTOP_BLENDCURRENTALPHA:
		{
			FString A = *DescribeStage(MaterialState,StageIndex - 1,1);
			return FString::Printf(TEXT("%s * %s.a + %s * (1 - %s.a)"),*DescribeArg(MaterialState,StageIndex,Arg1,Alpha),*A,*DescribeArg(MaterialState,StageIndex,Arg2,Alpha),*A);
		}
		case D3DTOP_BLENDFACTORALPHA:
			return FString::Printf(TEXT("%s * tfactor.a(%u) + %s * (1 - tfactor.a(%u))"),*DescribeArg(MaterialState,StageIndex,Arg1,Alpha),FColor(MaterialState->TFactorColor).A,*DescribeArg(MaterialState,StageIndex,Arg2,Alpha),FColor(MaterialState->TFactorColor).A);
		default:
			return TEXT("");
		};
	}
}

#ifndef _XBOX
/* scion ======================================================================
 * FD3DRenderInterface::GetStageCountForPass
 * Author: jg
 * 
 * Returns the number of textures stages used for a given pass. This lets us
 * determine whether a BSP lightmap would cause another pass or not
 *
 * input:	Pass -- The pass to check the stage count on
 *
 * ============================================================================
 */
DWORD FD3DRenderInterface::GetStageCountForPass(INT Pass)
{
	guard(FD3DRenderInterface::GetStageCountForPass);
	INT Num = 0;
	// If there is a valid state set
	if (CurrentState != NULL && Pass > 0 &&
		Pass <= CurrentState->NumMaterialPasses)
	{
		Num = CurrentState->MaterialPasses[Pass - 1]->StagesUsed;
	}
	return Num;
	unguard;
}
#endif

//
//	FD3DRenderInterface::SetMaterial.
//
INT FD3DRenderInterface::SetMaterial(UMaterial* InMaterial, FString* ErrorString, UMaterial** ErrorMaterial, INT* NumPasses)
{
	guard(FD3DRenderInterface::SetMaterial);

	FUNC_CLOCK(SetMat,GStats.DWORDStats(RenDev->D3DStats.STATS_SetMaterialCycles));

	// Set default texture if Material is NULL
	if( !InMaterial || (PrecacheMode == PRECACHE_VertexBuffers) )
		InMaterial = Cast<UMaterial>(UMaterial::StaticClass()->GetDefaultObject())->DefaultMaterial;

	// keep track of last material set
	CurrentState->LastMaterial = InMaterial;

	// Release old material state.
	for(INT PassIndex = 0;PassIndex < CurrentState->NumMaterialPasses;PassIndex++)
	{
		if(--CurrentState->MaterialPasses[PassIndex]->NumRefs == 0)
			MaterialStatePool.FreeState(CurrentState->MaterialPasses[PassIndex]);

		CurrentState->MaterialPasses[PassIndex] = NULL;
	}

	CurrentState->NumMaterialPasses = 1;
	
	// Initialized default state.
	CurrentState->MaterialPasses[0] = MaterialStatePool.AllocateState(&DefaultPass);

	// Check for circular material references
	if( GIsEditor )
	{
		static TArray<UMaterial*> History;
		History.Empty();
		if( !InMaterial->CheckCircularReferences(History) )
		{
			INT ErrorIndex = History.Num()-1;
			if( ErrorIndex >= 0 )
			{
				if( ErrorMaterial ) *ErrorMaterial = History(ErrorIndex);
				if( ErrorMaterial ) *ErrorString   = FString::Printf(TEXT("Circular material reference in %s"), History(ErrorIndex)->GetName() );
			}

			// Set null state
			CurrentState->NumMaterialPasses = 1;
			return CurrentState->NumMaterialPasses;
		}
	}

	if( CurrentState->LightingOnly )
	{
		// Initialized default state.
		//!!vogel: TODO: initializing all 8 stages is not necessarily needed.
		SetLightingOnlyMaterial();
	}
	else
	{
		// Check material type, stripping off and processing final modifiers.
		FD3DModifierInfo	ModifierInfo;
		UShader*			Shader;
		UBitmapMaterial*	BitmapMaterial;
		UConstantMaterial*	ConstantMaterial;
		UCombiner*			Combiner;
		UParticleMaterial*	ParticleMaterial;
		UTerrainMaterial*	TerrainMaterial;
		UProjectorMaterial*	ProjectorMaterial;
		UProjectorMultiMaterial*	ProjectorMultiMaterial;
		UMaterial*			NonModifier;
		UPixelShaderMaterial* PixelShaderMaterial;
		UMultiPassMaterial* MultiPassMaterial;
		//scion jg -- Added for movie to texture
		UMovieMaterial* pMovieMaterial;

		// Keep going until we have an renderable material.
		for(;;)
		{		    
			// Notify material it's being set.
			InMaterial->PreSetMaterial( Viewport->Actor->Level->TimeSeconds );

		    UBOOL Result = 0;
			if( (NonModifier=PixelShaderMaterial=CheckMaterial<UPixelShaderMaterial,MRH_PixelShaderMaterial>(this, InMaterial, &ModifierInfo))!=NULL)
			{
				Result = SetPixelShaderMaterial( PixelShaderMaterial, ModifierInfo, ErrorString, ErrorMaterial );
			}
			else
		    if( (NonModifier=Shader=CheckMaterial<UShader,MRH_Shader>(this, InMaterial, &ModifierInfo)) != NULL )
		    {
			    Result = SetShaderMaterial( Shader, ModifierInfo, ErrorString, ErrorMaterial );
		    }
		    else
		    if( (NonModifier=Combiner=CheckMaterial<UCombiner,MRH_Combiner>(this, InMaterial, &ModifierInfo))!=NULL )
		    {
			    Result = SetSimpleMaterial( Combiner, ModifierInfo, ErrorString, ErrorMaterial );
		    }
		    else
		    if( (NonModifier=ConstantMaterial=CheckMaterial<UConstantMaterial,MRH_ConstantMaterial>(this, InMaterial, &ModifierInfo))!=NULL )
		    {
			    Result = SetSimpleMaterial( ConstantMaterial, ModifierInfo, ErrorString, ErrorMaterial );
		    }
		    else
			//scion jg -- Added for movie to texture
			if( (NonModifier=pMovieMaterial=CheckMaterial<UMovieMaterial,MRH_MovieMaterial>(this, InMaterial, &ModifierInfo))!=NULL)
			{
				Result = SetMovieMaterial( pMovieMaterial, ModifierInfo, ErrorString, ErrorMaterial );
			}
			else
		    if( (NonModifier=BitmapMaterial=CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, InMaterial, &ModifierInfo))!=NULL )
		    {
			    Result = SetSimpleMaterial( BitmapMaterial, ModifierInfo, ErrorString, ErrorMaterial );
		    }
		    else
		    if( (NonModifier=TerrainMaterial=CheckMaterial<UTerrainMaterial,MRH_TerrainMaterial>(this, InMaterial, &ModifierInfo))!=NULL)
		    {
			    Result = SetTerrainMaterial( TerrainMaterial, ModifierInfo, ErrorString, ErrorMaterial );
		    }
		    else
			if( (NonModifier=ParticleMaterial=CheckMaterial<UParticleMaterial,MRH_ParticleMaterial>(this, InMaterial, &ModifierInfo))!=NULL)
		    {
			    Result = SetParticleMaterial( ParticleMaterial, ModifierInfo, ErrorString, ErrorMaterial );
		    }
			else
			if( (NonModifier=ProjectorMultiMaterial=CheckMaterial<UProjectorMultiMaterial,MRH_ProjectorMultiMaterial>(this, InMaterial, &ModifierInfo))!=NULL)
			{
				Result = SetProjectorMultiMaterial( ProjectorMultiMaterial, ModifierInfo, ErrorString, ErrorMaterial );
			}
			else
			if( (NonModifier=ProjectorMaterial=CheckMaterial<UProjectorMaterial,MRH_ProjectorMaterial>(this, InMaterial, &ModifierInfo))!=NULL)
			{
				Result = SetProjectorMaterial( ProjectorMaterial, ModifierInfo, ErrorString, ErrorMaterial );
			}
			else 
			if( (NonModifier=MultiPassMaterial=CheckMaterial<UMultiPassMaterial,MRH_MultiPassMaterial>(this, InMaterial, &ModifierInfo))!=NULL)
			{
				Result = SetMultiPassMaterial( MultiPassMaterial, ModifierInfo, ErrorString, ErrorMaterial );
			}
		    else
			    break;

			// Material is renderable.
			if( Result )
				break;

			// Use the default texture as a fallback.
			InMaterial = Cast<UMaterial>(UMaterial::StaticClass()->GetDefaultObject())->DefaultMaterial;

			// Clear any state we got part way through setting.
			for(INT PassIndex = 0;CurrentState->MaterialPasses[PassIndex];PassIndex++)
			{
				MaterialStatePool.FreeState(CurrentState->MaterialPasses[PassIndex]);
				CurrentState->MaterialPasses[PassIndex] = NULL;
			}
			CurrentState->MaterialPasses[0] = MaterialStatePool.AllocateState(&DefaultPass);
			CurrentState->NumMaterialPasses = 1;
		}
	}

#if 0	// Blending debugging info.
	debugf(NAME_DevD3D,TEXT("%s:"),InMaterial->GetFullName());
	for(INT PassIndex = 0;PassIndex < CurrentState->NumMaterialPasses;PassIndex++)
	{
		debugf(NAME_DevD3D,TEXT("	Pass %u:"),PassIndex);
		debugf(NAME_DevD3D,TEXT("		Color = %s"),*DescribeStage(CurrentState->MaterialPasses[PassIndex],CurrentState->MaterialPasses[PassIndex]->StagesUsed - 1,0));
		debugf(NAME_DevD3D,TEXT("		Alpha = %s"),*DescribeStage(CurrentState->MaterialPasses[PassIndex],CurrentState->MaterialPasses[PassIndex]->StagesUsed - 1,1));
	}
#endif

	if( NumPasses )
		*NumPasses = CurrentState->NumMaterialPasses;

	CurrentState->CurrentMaterialState = NULL;

	STAT_INC(GStats.DWORDStats(RenDev->D3DStats.STATS_SetMaterialCalls));

	//scion jg -- Return the number of passes this material needs
	return CurrentState->NumMaterialPasses;

	unguard;
}

// scion sz - 
// return material that was last set in the
// current state
UMaterial* FD3DRenderInterface::GetLastMaterial()
{
    return CurrentState->LastMaterial;
}

//
//	FD3DRenderInterface::GetMaterialVertexShaderType
//
DWORD FD3DRenderInterface::GetMaterialVertexShaderType(UMaterial* Material)
{
	guard(FD3DRenderInterface::GetMaterialVertexShaderType);

#if !defined(_XBOX)
	return VS_FixedFunction;
#endif

	UPixelShaderMaterial* PixelShaderMaterial = Cast<UPixelShaderMaterial>( Material );
	if( !PixelShaderMaterial )
	{
		return VS_FixedFunction;
	}
	else
	{
		return (DWORD)PixelShaderMaterial->VertexShaderType;
	}

	unguard;
}

//
//	FD3DRenderInterface::AddVertexShaderEnum
//
void FD3DRenderInterface::AddVertexShaderEnum( QWORD VertexShaderEnum )
{
	guard(FD3DRenderInterface::AddVertexShaderEnum);
	RenDev->DeferredState.SetVertexShaderEnum( RenDev->DeferredState.GetVertexShaderEnum() | VertexShaderEnum );	
	unguard;
}

//
//	FD3DRenderInterface::SetVertexStreams
//
INT FD3DRenderInterface::SetVertexStreams(QWORD VertexShaderEnum,FVertexStream** Streams,INT NumStreams)
{
	guard(FD3DRenderInterface::SetVertexStreams);

	FUNC_CLOCK(SetVS,GStats.DWORDStats(RenDev->D3DStats.STATS_SetVertexStreamsCycles));

	// Unset any additional old streams.
	for(INT StreamIndex = NumStreams;StreamIndex < CurrentState->NumStreams;StreamIndex++)
	{
		if(CurrentState->Streams[StreamIndex] != NULL)
		{
			CurrentState->Streams[StreamIndex] = NULL;
			RenDev->DeferredState.SetStreamSource(StreamIndex,NULL,0);
		}
	}

	// Build the shader declarations.
	FShaderDeclaration	ShaderDeclaration;

	ShaderDeclaration.NumStreams = NumStreams;

	// Add the vertex stream components to the shader declaration.
	for(INT StreamIndex = 0;StreamIndex < NumStreams;StreamIndex++)
		ShaderDeclaration.Streams[StreamIndex] = FStreamDeclaration(Streams[StreamIndex]);

	// scion sz -- actual vertex shader created in FD3DDeferredState::Commit
	// Set vertex shader enumeration and shader declaration 
	//RenDev->DeferredState.SetShaderDeclaration( ShaderDeclaration );
	FD3DVertexShader* VertexShader = RenDev->GetVertexShader( VertexShaderEnum, ShaderDeclaration );
	CurrentState->VertexShader = VertexShader;
	CurrentState->VertexShaderEnum = VertexShaderEnum;
	RenDev->DeferredState.SetVertexShader( VertexShader );
	RenDev->DeferredState.SetVertexShaderEnum( CurrentState->VertexShaderEnum );

	INT Size = 0;

	// Set the vertex streams.
	for(INT StreamIndex = 0;StreamIndex < NumStreams;StreamIndex++)
	{
		// Cache the vertex stream.
		//scion jg -- Completely rewrote the caching scheme for speed
		_WORD CacheId = Streams[StreamIndex]->GetCacheId();
		FD3DVertexStream* D3DVertexStream = NULL;
		// Check to see if this item has been cached before
		if (CacheId != INVALID_CACHEID)
		{
			// It's been cached so find the item
			D3DVertexStream = (FD3DVertexStream*)RenDev->GetCachedResource(CacheId,CH_VertexBuffer);
		}
		// It's possible that the item was cached, then flushed so cache again
		// if needed
		if (D3DVertexStream == NULL)
		{
			// Allocate a new texture
			D3DVertexStream = NEW FD3DVertexStream(RenDev,Streams[StreamIndex]);
			// Add it to the render device's cache
			RenDev->CacheResource(D3DVertexStream,CH_VertexBuffer);
			// Now propagate back the new cache ID
			Streams[StreamIndex]->SetCacheId(D3DVertexStream->CacheId,RenDev);
		}
		checkSlow(D3DVertexStream);

		if(D3DVertexStream->CachedRevision != Streams[StreamIndex]->GetRevision())
		{
			Size += Streams[StreamIndex]->GetSize();
			D3DVertexStream->Cache(Streams[StreamIndex]);
		}

		D3DVertexStream->LastFrameUsed = RenDev->FrameCounter;

		// Set the vertex stream.
		INT	Stride = Streams[StreamIndex]->GetStride();

		RenDev->DeferredState.SetStreamSource(StreamIndex,D3DVertexStream->Direct3DVertexBuffer8,Stride);
		CurrentState->Streams[StreamIndex]		= D3DVertexStream;
		CurrentState->StreamStrides[StreamIndex] = Stride;
	}

	CurrentState->NumStreams = NumStreams;

#ifndef FINAL_RELEASE
	GStats.DWORDStats(RenDev->D3DStats.STATS_SetVertexStreamsCalls)++;
#endif
	return Size;

	unguard;
}

//
//	FD3DRenderInterface::SetDynamicStream
//
INT FD3DRenderInterface::SetDynamicStream(QWORD VertexShaderEnum,FVertexStream* Stream)
{
	guard(FD3DRenderInterface::SetDynamicStream);

	FUNC_CLOCK(SetDS,GStats.DWORDStats(RenDev->D3DStats.STATS_SetDynamicStreamCycles));

	// If there isn't a dynamic vertex stream already, allocate one.
	if(!RenDev->DynamicVertexStream)
		RenDev->DynamicVertexStream = NEW FD3DDynamicVertexStream(RenDev);

	// Add the vertices in Stream to the dynamic vertex stream.
	INT	BaseVertexIndex = RenDev->DynamicVertexStream->AddVertices(Stream),
		Stride = Stream->GetStride();

	// Set the dynamic vertex stream.
	RenDev->DeferredState.SetStreamSource(0,RenDev->DynamicVertexStream->Direct3DVertexBuffer8,Stride);
	CurrentState->Streams[0] = RenDev->DynamicVertexStream;
	CurrentState->StreamStrides[0] = Stride;

	// Unset any additional old streams.
	for(INT StreamIndex = 1;StreamIndex < CurrentState->NumStreams;StreamIndex++)
	{
		CurrentState->Streams[StreamIndex] = NULL;
		RenDev->DeferredState.SetStreamSource(StreamIndex,NULL,0);
	}

	CurrentState->NumStreams = 1;

	// Find or create an appropriate vertex shader.
	FShaderDeclaration	ShaderDeclaration;

	ShaderDeclaration.NumStreams = 1;
	ShaderDeclaration.Streams[0] = FStreamDeclaration(Stream);

	// scion sz -- actual vertex shader created in FD3DDeferredState::Commit
	// Set vertex shader enumeration and shader declaration 
	//RenDev->DeferredState.SetShaderDeclaration( ShaderDeclaration );
	FD3DVertexShader* VertexShader = RenDev->GetVertexShader( VertexShaderEnum, ShaderDeclaration );
	CurrentState->VertexShader = VertexShader;
	CurrentState->VertexShaderEnum = VertexShaderEnum;
	RenDev->DeferredState.SetVertexShader( VertexShader );
	//scion jg -- Make sure that the material's vertex shader is respected
	RenDev->DeferredState.SetVertexShaderEnum( CurrentState->VertexShaderEnum );

#ifndef FINAL_RELEASE
	GStats.DWORDStats(RenDev->D3DStats.STATS_SetDynamicStreamCalls)++;
	GStats.DWORDStats(RenDev->D3DStats.STATS_DynamicVertexBytes) += Stream->GetSize();
#endif
	return BaseVertexIndex;

	unguard;
}

//
//	FD3DRenderInterface::SetIndexBuffer
//
INT FD3DRenderInterface::SetIndexBuffer(FIndexBuffer* IndexBuffer,INT BaseVertexIndex)
{
	guard(FD3DRenderInterface::SetIndexBuffer);

	FUNC_CLOCK(SetIB,GStats.DWORDStats(RenDev->D3DStats.STATS_SetIndexBufferCycles));

	UBOOL RequiresCaching = 0;
	if(IndexBuffer)
	{
		// Cache the index buffer.
		//scion jg -- Completely rewrote the caching scheme for speed
		_WORD CacheId = IndexBuffer->GetCacheId();
		FD3DIndexBuffer* D3DIndexBuffer = NULL;
		// Check to see if this item has been cached before
		if (CacheId != INVALID_CACHEID)
		{
			// It's been cached so find the item
			D3DIndexBuffer = (FD3DIndexBuffer*)RenDev->GetCachedResource(CacheId,
				CH_IndexBuffer);
		}
		// It's possible that the item was cached, then flushed so cache again
		// if needed
		if (D3DIndexBuffer == NULL)
		{
			// Allocate a new texture
			D3DIndexBuffer = NEW FD3DIndexBuffer(RenDev,IndexBuffer);
			// Add it to the render device's cache
			RenDev->CacheResource(D3DIndexBuffer,CH_IndexBuffer);
			// Now propagate back the new cache ID
			IndexBuffer->SetCacheId(D3DIndexBuffer->CacheId,RenDev);
		}
		checkSlow(D3DIndexBuffer);

		if(D3DIndexBuffer->CachedRevision != IndexBuffer->GetRevision())
		{
			D3DIndexBuffer->Cache(IndexBuffer);
			RequiresCaching |= 1;
		}

        checkSlow(IndexBuffer->GetSize()==D3DIndexBuffer->CachedSize);

		D3DIndexBuffer->LastFrameUsed = RenDev->FrameCounter;

		// Set the index buffer.
		CurrentState->IndexBuffer = D3DIndexBuffer;
		CurrentState->IndexBufferBase = BaseVertexIndex;
		RenDev->DeferredState.SetIndices(D3DIndexBuffer->Direct3DIndexBuffer8,BaseVertexIndex);
	}
	else
	{
		// Clear the index buffer.
		if(CurrentState->IndexBuffer != NULL)
		{
			CurrentState->IndexBuffer = NULL;
			CurrentState->IndexBufferBase = 0;
			RenDev->DeferredState.SetIndices(NULL,0);
		}
	}

#ifndef FINAL_RELEASE
	GStats.DWORDStats(RenDev->D3DStats.STATS_SetIndexBufferCalls)++;
#endif
	return RequiresCaching ? IndexBuffer->GetSize() : 0;

	unguard;
}

//
//	FD3DRenderInterface::SetDynamicIndexBuffer
//
INT FD3DRenderInterface::SetDynamicIndexBuffer(FIndexBuffer* IndexBuffer,INT BaseVertexIndex)
{
	guard(FD3DRenderInterface::SetDynamicIndexBuffer);

	FUNC_CLOCK(SetDIB,GStats.DWORDStats(RenDev->D3DStats.STATS_SetDynamicIndexBufferCycles));

	// If there isn't a dynamic index buffer already, allocate one.
	FD3DDynamicIndexBuffer* DynamicIndexBuffer;
	if( IndexBuffer->GetIndexSize() == sizeof(DWORD) )
	{
		if(!RenDev->DynamicIndexBuffer32)
			RenDev->DynamicIndexBuffer32 = NEW FD3DDynamicIndexBuffer(RenDev,sizeof(DWORD));
		DynamicIndexBuffer = RenDev->DynamicIndexBuffer32;
	}
	else
	{
		if(!RenDev->DynamicIndexBuffer16)
			RenDev->DynamicIndexBuffer16 = NEW FD3DDynamicIndexBuffer(RenDev,sizeof(_WORD));
		DynamicIndexBuffer = RenDev->DynamicIndexBuffer16;
	}

	// Add the indices in the index buffer to the dynamic index buffer.
	INT	BaseIndex = DynamicIndexBuffer->AddIndices(IndexBuffer);

	// Set the dynamic index buffer.
	CurrentState->IndexBuffer = DynamicIndexBuffer;
	CurrentState->IndexBufferBase = BaseVertexIndex;
	RenDev->DeferredState.SetIndices(DynamicIndexBuffer->Direct3DIndexBuffer8,BaseVertexIndex);

#ifndef FINAL_RELEASE
	GStats.DWORDStats(RenDev->D3DStats.STATS_SetDynamicIndexBufferCalls)++;
	GStats.DWORDStats(RenDev->D3DStats.STATS_DynamicIndexBytes) += IndexBuffer->GetSize();
#endif
	return BaseIndex;

	unguard;
}

//
//	FD3DRenderInterface::DrawPrimitive
//
void FD3DRenderInterface::DrawPrimitive(EPrimitiveType PrimitiveType,INT FirstIndex,INT NumPrimitives,INT MinIndex,INT MaxIndex)
{
	guard(FD3DRenderInterface::DrawPrimitive);

	for( INT pass=0;pass<CurrentState->NumMaterialPasses;pass++ )
	{
		SetMaterialBlending( CurrentState->MaterialPasses[pass], CurrentState->CullMode );

		// Fog hacks needed for translucent objects.
		UBOOL RestoreFogColor = 0;
		if( CurrentState->DistanceFogEnabled && CurrentState->CurrentMaterialState->OverrideFogColor )
		{
			RestoreFogColor = 1;
			RenDev->DeferredState.SetRenderState( RS_FOGCOLOR, (DWORD)CurrentState->CurrentMaterialState->OverriddenFogColor );
		}
	
#ifndef _XBOX
		DWORD ShaderType =  RenDev->DeferredState.GetVertexShaderEnum();
		// #SKEL - explicit stage state resetting apparently needed for any non-fixed-pipeline GPU vertex shaders - at least on PC...
		if( ShaderType & VS_SkeletalSkin )
		{	
			RenDev->DeferredState.SetTextureStageState( 0,TSS_TEXCOORDINDEX,0 );
			RenDev->DeferredState.SetTextureStageState( 1,TSS_TEXCOORDINDEX,1 );
			RenDev->DeferredState.SetTextureStageState( 2,TSS_TEXCOORDINDEX,2 );
			RenDev->DeferredState.SetTextureStageState( 3,TSS_TEXCOORDINDEX,3 );

			RenDev->DeferredState.SetTextureStageState( 0,TSS_TEXTURETRANSFORMFLAGS,0 );		
			RenDev->DeferredState.SetTextureStageState( 1,TSS_TEXTURETRANSFORMFLAGS,0 );		
			RenDev->DeferredState.SetTextureStageState( 2,TSS_TEXTURETRANSFORMFLAGS,0 );
			RenDev->DeferredState.SetTextureStageState( 3,TSS_TEXTURETRANSFORMFLAGS,0 );
		}
#endif

		// scion sz - 
		// make sure the deffered state vertex shader gets reset to the proper enumeration from the current state
		if( CurrentState->VertexShader )
		{
			//scion jg/sz -- Make sure that the material's vertex shader is respected
			RenDev->DeferredState.SetVertexShaderEnum( 
				CurrentState->VertexShaderEnum | CurrentState->MaterialPasses[pass]->MaterialVertexShaderEnum);
		}

		RenDev->DeferredState.Commit();		

#define VALIDATE_DEVICE 0
#if VALIDATE_DEVICE
		DWORD	NumPasses;
		HRESULT hr = RenDev->Direct3DDevice8->ValidateDevice( &NumPasses );
		switch( hr )
		{
		case D3DERR_CONFLICTINGTEXTUREFILTER :
			debugf(NAME_DevD3D,TEXT("D3DERR_CONFLICTINGTEXTUREFILTER"));
			break;
		case D3DERR_CONFLICTINGTEXTUREPALETTE :
			debugf(NAME_DevD3D,TEXT("D3DERR_CONFLICTINGTEXTUREPALETTE"));
			break;
		case D3DERR_TOOMANYOPERATIONS :
			debugf(NAME_DevD3D,TEXT("D3DERR_TOOMANYOPERATIONS"));
			break;
		case D3DERR_UNSUPPORTEDALPHAARG :
			debugf(NAME_DevD3D,TEXT("D3DERR_UNSUPPORTEDALPHAARG"));
			break;
		case D3DERR_UNSUPPORTEDALPHAOPERATION :
			debugf(NAME_DevD3D,TEXT("D3DERR_UNSUPPORTEDALPHAOPERATION"));
			break;
		case D3DERR_UNSUPPORTEDCOLORARG :
			debugf(NAME_DevD3D,TEXT("D3DERR_UNSUPPORTEDCOLORARG"));
			break;
		case D3DERR_UNSUPPORTEDCOLOROPERATION :  
			debugf(NAME_DevD3D,TEXT("D3DERR_UNSUPPORTEDCOLOROPERATION"));
			break;
		case D3DERR_UNSUPPORTEDFACTORVALUE : 
			debugf(NAME_DevD3D,TEXT("D3DERR_UNSUPPORTEDFACTORVALUE"));
			break;
		case D3DERR_UNSUPPORTEDTEXTUREFILTER :
			debugf(NAME_DevD3D,TEXT("D3DERR_UNSUPPORTEDTEXTUREFILTER"));
			break;
		case D3DERR_WRONGTEXTUREFORMAT :
			debugf(NAME_DevD3D,TEXT("D3DERR_WRONGTEXTUREFORMAT"));
			break;
		default:;
		}
		if( FAILED(hr) )
			debugf(NAME_DevD3D,TEXT("INSERT BREAKPOINT HERE"));
#endif

		guard(DrawPrim);
		FUNC_CLOCK(DPCyc,GStats.DWORDStats(RenDev->D3DStats.STATS_DrawPrimitiveCycles));
		//scion jg -- Removed all the if/else ifs and made the primitive
		// type enum match D3D's so that this was faster
		if(CurrentState->IndexBuffer)
		{
			RenDev->Direct3DDevice8->DrawIndexedPrimitive(
				(D3DPRIMITIVETYPE)PrimitiveType,MinIndex,
				MaxIndex - MinIndex + 1,FirstIndex,NumPrimitives);
			STAT_INC_BY(GStats.DWORDStats(RenDev->D3DStats.STATS_NumSVPVertices),MaxIndex - MinIndex + 1);
		}
		else
		{
			RenDev->Direct3DDevice8->DrawPrimitive((D3DPRIMITIVETYPE)PrimitiveType,
				FirstIndex,NumPrimitives);
#ifndef FINAL_RELEASE
			switch (PrimitiveType)
			{
				case PT_TriangleList:
					GStats.DWORDStats(RenDev->D3DStats.STATS_NumSVPVertices) += NumPrimitives * 3;
					break;
				case PT_TriangleStrip:
				case PT_TriangleFan:
					GStats.DWORDStats(RenDev->D3DStats.STATS_NumSVPVertices) += NumPrimitives + 2;
					break;
#ifdef _XBOX
				case PT_QuadList:
					GStats.DWORDStats(RenDev->D3DStats.STATS_NumSVPVertices) += NumPrimitives * 4;
					break;
				case PT_QuadStrip:
					GStats.DWORDStats(RenDev->D3DStats.STATS_NumSVPVertices) += NumPrimitives*2 + 2;
#endif
				case PT_PointList:
					GStats.DWORDStats(RenDev->D3DStats.STATS_NumSVPVertices) += NumPrimitives;
					break;
				case PT_LineList:
					GStats.DWORDStats(RenDev->D3DStats.STATS_NumSVPVertices) += NumPrimitives * 2;
					break;
			}
#endif
		}
		// Restore fog color set above.
		if( RestoreFogColor )
			RenDev->DeferredState.SetRenderState( RS_FOGCOLOR, CurrentState->DistanceFogColor );
		unguard;
	}

#ifndef FINAL_RELEASE
	GStats.DWORDStats(RenDev->D3DStats.STATS_NumPrimitives)	+= NumPrimitives;
#endif
#ifndef FINAL_RELEASE
	GStats.DWORDStats(RenDev->D3DStats.STATS_DrawPrimitiveCalls)++;
#endif
	unguard;
}


//
//	FD3DRenderInterface::GetD3DStencilOp
//
D3DSTENCILOP FD3DRenderInterface::GetD3DStencilOp( EStencilOp StencilOp )
{
	switch( StencilOp )
	{
	case SO_Keep:
		return D3DSTENCILOP_KEEP;
	case SO_Zero:
		return D3DSTENCILOP_ZERO;
	case SO_Replace:
		return D3DSTENCILOP_REPLACE;
	case SO_IncrementSat:
		return D3DSTENCILOP_INCRSAT;
	case SO_DecrementSat:
		return D3DSTENCILOP_DECRSAT;
	case SO_Invert:
		return D3DSTENCILOP_INVERT;
	case SO_Increment:
		return D3DSTENCILOP_INCR;
	case SO_Decrement:
		return D3DSTENCILOP_DECR; 
	default:
		appErrorf(TEXT("invalid stencil op"));
		return D3DSTENCILOP_KEEP;
	}
}

//
//	FD3DRenderInterface::GetD3DCompFunc
//
D3DCMPFUNC FD3DRenderInterface::GetD3DCompFunc( ECompareFunction CompFunc )
{
	switch( CompFunc )
	{
	case CF_Never:
		return D3DCMP_NEVER;
	case CF_Less:
		return D3DCMP_LESS;
	case CF_Equal:
		return D3DCMP_EQUAL;
	case CF_LessEqual:
		return D3DCMP_LESSEQUAL;
	case CF_Greater:
		return D3DCMP_GREATER;
	case CF_NotEqual:
		return D3DCMP_NOTEQUAL;
	case CF_GreaterEqual:
		return D3DCMP_GREATEREQUAL;
	case CF_Always:
		return D3DCMP_ALWAYS;
	default:
		appErrorf(TEXT("invalid compare function"));
		return D3DCMP_NEVER;
	}
}

/* scion ======================================================================
* FD3DRenderInterface::CopyDepthBuffer
* Author: sz
* 
* copy depth buffer from one render target to another
*
* ============================================================================
*/
void FD3DRenderInterface::CopyDepthBuffer( FRenderTarget* DestTarget, FRenderTarget* SrcTarget )
{
	guard(FD3DRenderInterface::CopyDepthBuffer);
#ifdef _XBOX

	STAT_INC(GStats.DWORDStats(RenDev->D3DStats.STATS_SetRenderTargetCalls));
	FUNC_CLOCK(SetRT,GStats.DWORDStats(RenDev->D3DStats.STATS_SetRenderTargetCycles));

	// save current target iot restore later
	FRenderTarget* SavedRenderTarget = GetRenderTarget();
	// make sure destination target texture is cached
	FD3DTexture* DestTexture = CacheTexture( DestTarget );		
	
	// switch format of the depth buffer on-the-fly so that we can use it 
	// as our destination color render target
	IDirect3DSurface8* pDepthSurf = DestTexture->DepthStencilSurface;
	D3DFORMAT SavedDepthFormat = (D3DFORMAT)((pDepthSurf->Format & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT);
    pDepthSurf->Format = (pDepthSurf->Format & ~D3DFORMAT_FORMAT_MASK) | (D3DFMT_LIN_A8R8G8B8 << D3DFORMAT_FORMAT_SHIFT);
	// set the render target to the current zbuffer texture
	RenDev->Direct3DDevice8->SetRenderTarget( DestTexture->DepthStencilSurface, NULL );

	// adjust UVs based on viewport size
	// so that we only blit the desired portion of the back buffer
	FLOAT OffsetX = (FLOAT)Viewport->OffsetX / (FLOAT)SrcTarget->GetWidth();
	FLOAT OffsetY = (FLOAT)Viewport->OffsetY / (FLOAT)SrcTarget->GetHeight();
	FLOAT U1[4] = { OffsetX, OffsetX, OffsetX, OffsetX };
	FLOAT V1[4] = { OffsetY, OffsetY, OffsetY, OffsetY };
	FLOAT U2[4] = { OffsetX+Viewport->ScaleX, OffsetX+Viewport->ScaleX, OffsetX+Viewport->ScaleX, OffsetX+Viewport->ScaleX };
	FLOAT V2[4] = { OffsetY+Viewport->ScaleY, OffsetY+Viewport->ScaleY, OffsetY+Viewport->ScaleY, OffsetY+Viewport->ScaleY };    

	// all enabled
	SetColorWriteMask( CWM_COLORWRITEENABLE_ALL );
	// Push the new viewport state too
	SetViewport(0,0,DestTexture->CachedWidth,DestTexture->CachedHeight);
	// setup depth buffer materail used to render a screen quad
	DECLARE_STATIC_UOBJECT(	UProxyBitmapMaterial, SrcMat, {});
	// update proxy bitmap interface
	SrcMat->SetTextureInterface( SrcTarget );
	// point sampled
	SrcMat->FilterMode = TF_NEAREST;
	// this tex matrix modifier is needed for u,v to s,t conversion
	DECLARE_STATIC_UOBJECT(	UTexMatrix,	SrcMatTexMat, 
	{
		SrcMatTexMat->Matrix.SetIdentity();
		SrcMatTexMat->Material = SrcMat;		
	});
	// pixel shader that swizzles the depth buffer colors
	DECLARE_STATIC_UOBJECT( UPixelShaderMaterial, PSDepthSwizzleColor,
	{
		PSDepthSwizzleColor->TextureStage0 = SrcMatTexMat;
		PSDepthSwizzleColor->PixelShaderType = PS_DepthSwizzleColor;
	});
	// use depth from render target as a color texture
	SrcTarget->SetMaterialUsage( FRenderTarget::RT_MATERIAL_USE_DEPTHBUFFER );

	// match the source format to the destination format
	// otherwise we will get funky swizzling of colors
	FD3DTexture* SrcTexture = CacheTexture( SrcTarget );
	IDirect3DTexture8* pSrcTex = SrcTexture->Direct3DTexture8;
	D3DFORMAT SavedSrcFormat = (D3DFORMAT)((pSrcTex->Format & D3DFORMAT_FORMAT_MASK) >> D3DFORMAT_FORMAT_SHIFT);
	pSrcTex->Format = (pSrcTex->Format & ~D3DFORMAT_FORMAT_MASK) | (D3DFMT_LIN_A8R8G8B8  << D3DFORMAT_FORMAT_SHIFT);

	static FColor DefColor(255,255,255,255);
	
	FScreenPrimUtil ScreenUtil( DestTarget, this );
	ScreenUtil.DrawTile(
		0, 0, DestTarget->GetWidth(), DestTarget->GetHeight(),
		U1, V1,	U2, V2,
		0.0f,
		SrcMatTexMat,
		DefColor
		);
	ScreenUtil.Flush();

	// restore the hacked surface format of the depth buffer
	pDepthSurf->Format = (pDepthSurf->Format & ~D3DFORMAT_FORMAT_MASK) | (SavedDepthFormat << D3DFORMAT_FORMAT_SHIFT);
	// restore the hacked surface format of the depth buffer
	pSrcTex->Format = (pSrcTex->Format & ~D3DFORMAT_FORMAT_MASK) | (SavedSrcFormat << D3DFORMAT_FORMAT_SHIFT);
	
	// color only again
	SetColorWriteMask( CWM_COLORWRITEENABLE_COLORONLY );
	// restore render target usage to color mode
	SrcTarget->SetMaterialUsage( FRenderTarget::RT_MATERIAL_USE_BACKBUFFER );
	// restore original render target before we started
	FD3DTexture* SavedTargetTexture = CacheTexture( SavedRenderTarget );
	RenDev->Direct3DDevice8->SetRenderTarget( SavedTargetTexture->RenderTargetSurface, 
		SavedTargetTexture->DepthStencilSurface );	
    // restore viewport
	ResetViewport();	

#endif
	unguard;
}

/* scion ======================================================================
* FD3DRenderInterface::GetBackBufferCopyTarget
* Author: sz
* 
* creates the back buffer copy render target if it doesn't exist and return it
*
* ============================================================================
*/
FRenderTarget* FD3DRenderInterface::GetBackBufferCopyTarget()
{
	guard(FD3DRenderInterface::GetBackBufferCopyTarget);

	// get a back buffer material
	UMaterial* SourceMaterial = GetBackBufferMaterial();	
	// scale to apply to back buffer size
	const FLOAT TestScale =  0.5f;	
	// get the full screen size of the back buffer
	INT FullScreenSizeX = SourceMaterial->MaterialUSize();
	INT FullScreenSizeY = SourceMaterial->MaterialVSize();
	// calc size of the back buffer copy
	INT CopyTargetSizeX = FullScreenSizeX * TestScale;
	INT CopyTargetSizeY = FullScreenSizeY * TestScale;

	// create the back buffer copy target if it doesn't exist
	// or if needs to be resized
	if( !BackBufferCopyTarget ||
		BackBufferCopyTarget->GetWidth() != CopyTargetSizeX ||
		BackBufferCopyTarget->GetHeight() != CopyTargetSizeY )
	{	
		// remove the old one
		if( BackBufferCopyTarget )
		{
			BackBufferCopyTarget->RemovePoolRef();
			BackBufferCopyTarget = NULL;
		}
        // this should reuse the same target as the bloom back buffer (320x240)
		BackBufferCopyTarget = RenderTargetPool.ObtainRenderTarget( CopyTargetSizeX, CopyTargetSizeY, 
			TEXF_RGBA8, FRenderTarget::RT_CREATE_COLOR|FRenderTarget::RT_CREATE_DEPTH );
	}

	return BackBufferCopyTarget;

	unguard;
}

/* scion ======================================================================
* FD3DRenderInterface::UpdateBackBufferCopyTarget
* Author: sz
* 
* blit portion of the back buffer set by the current viewport extents to
* the back buffer copy target
*
* ============================================================================
*/
void FD3DRenderInterface::UpdateBackBufferCopyTarget( UViewport* InViewport )
{
	guard(FD3DRenderInterface::UpdateBackBufferCopyTarget);

#ifndef _XBOX
	return;
#endif

	if( !InViewport )
	{
		InViewport = Viewport;
		check( InViewport );
	}

	// make sure it exists
    BackBufferCopyTarget = GetBackBufferCopyTarget();
   
	// switch to the back buffer target so we can blit the 
	// original back buffer to it
	SetRenderTarget( BackBufferCopyTarget );
	
#if 0
	// current render target
	FRenderTarget* SavedRenderTarget = GetRenderTarget();
	static FMultiSampleFilter SamplerFilter( FMultiSampleFilter::MULTI_SAMPLE_BOX );
	FilterCopyRenderTarget( BackBufferCopyTarget, SavedRenderTarget, InViewport, SamplerFilter, this );
#else

	// get a back buffer material
	UMaterial* SourceMaterial = GetBackBufferMaterial();	
	// get the full screen size of the back buffer
	INT FullScreenSizeX = SourceMaterial->MaterialUSize();
	INT FullScreenSizeY = SourceMaterial->MaterialVSize();
	// scale to apply to back buffer size
	FLOAT TestScale = (FLOAT)BackBufferCopyTarget->GetWidth() / (FLOAT)FullScreenSizeX;

	// offset matrices used for 4 sample blur
	DECLARE_STATIC_UOBJECT_ARRAY( UTexMatrix, SourceTexMatrix, 4,
	{
		for( INT i=0; i<4; i++ )
		{
			SourceTexMatrix[i]->Matrix.SetIdentity();
			SourceTexMatrix[i]->Material = SourceMaterial;
		}		
	} );
	// create a 4 sample blur pixel shader
	DECLARE_STATIC_UOBJECT( UPSBlurMaterial, BlurPixelShader,
	{
		BlurPixelShader->BlurSourceMaterial = SourceMaterial;
		BlurPixelShader->BlurSourceOffset0 = SourceTexMatrix[0];
		BlurPixelShader->BlurSourceOffset1 = SourceTexMatrix[1];
		BlurPixelShader->BlurSourceOffset2 = SourceTexMatrix[2];
		BlurPixelShader->BlurSourceOffset3 = SourceTexMatrix[3];
	});

	// setup the sample ofsets
	// weights should default to 0.25f
	FLOAT OneTexelU = 1.0f / (FLOAT)FullScreenSizeX;
	FLOAT OneTexelV = 1.0f / (FLOAT)FullScreenSizeY;

	// tex0 offset
	SourceTexMatrix[0]->Matrix.M[2][0] = OneTexelU * -1.0f;
	SourceTexMatrix[0]->Matrix.M[2][1] = OneTexelV * -1.0f;		

	// tex1 offset
	SourceTexMatrix[1]->Matrix.M[2][0] = OneTexelU * 1.0f;
	SourceTexMatrix[1]->Matrix.M[2][1] = OneTexelV * -1.0f;

	// tex2 offset
	SourceTexMatrix[2]->Matrix.M[2][0] = OneTexelU * -1.0f;
	SourceTexMatrix[2]->Matrix.M[2][1] = OneTexelV * 1.0f;

	// tex3 offset
	SourceTexMatrix[3]->Matrix.M[2][0] = OneTexelU * 1.0f;
	SourceTexMatrix[3]->Matrix.M[2][1] = OneTexelV * 1.0f;		

    // create a final blend to disable z-buffer writes
	DECLARE_STATIC_UOBJECT(	UFinalBlend, BackBufferFinalBlend, 
	{
		BackBufferFinalBlend->ZWrite = 0;
		BackBufferFinalBlend->Material = BlurPixelShader;
	} );
	// adjust UVs based on viewport size
	// so that we only blit the desired portion of the back buffer
	FLOAT OffsetX = (FLOAT)InViewport->OffsetX / (FLOAT)FullScreenSizeX;
	FLOAT OffsetY = (FLOAT)InViewport->OffsetY / (FLOAT)FullScreenSizeY;
	FLOAT SizeX = (FLOAT)InViewport->SizeX / (FLOAT)FullScreenSizeX * InViewport->RenDev->FSAAScale[0];
	FLOAT SizeY = (FLOAT)InViewport->SizeY / (FLOAT)FullScreenSizeY * InViewport->RenDev->FSAAScale[1];
	FLOAT U1[4] = { OffsetX, OffsetX, OffsetX, OffsetX };
	FLOAT V1[4] = { OffsetY, OffsetY, OffsetY, OffsetY };
	FLOAT U2[4] = { OffsetX+SizeX, OffsetX+SizeX, OffsetX+SizeX, OffsetX+SizeX };
	FLOAT V2[4] = { OffsetY+SizeY, OffsetY+SizeY, OffsetY+SizeY, OffsetY+SizeY };    
	static FColor WhiteOpaque(255,255,255,255);
	// draw full screen quad into BackBufferCopyTarget
	FScreenPrimUtil( BackBufferCopyTarget, this ).DrawTile(
		(InViewport->OffsetX)*TestScale, 
		(InViewport->OffsetY)*TestScale, 
		(InViewport->OffsetX+InViewport->SizeX)*TestScale, 
		(InViewport->OffsetY+InViewport->SizeY)*TestScale,
		U1, V1,	U2, V2,
		0.0f,
		BlurPixelShader,
		WhiteOpaque
		);
#endif
	

#ifdef _XBOX //jg -- Try to release this faster
	InsertFence();
#endif

	// set render target back to default	
	ResetRenderTarget();

	unguard;
}

//
//	FD3DRenderInterface::GetBackBufferCopyMaterial
//
//
UMaterial* FD3DRenderInterface::GetBackBufferCopyMaterial()
{
	guard(FD3DRenderInterface::GetBackBufferCopyMaterial);

#ifndef _XBOX
	return NULL;
#endif

	if( !BackBufferCopyTarget )
	{
		BackBufferCopyTarget = GetBackBufferCopyTarget();
	}

	DECLARE_STATIC_UOBJECT(	UProxyBitmapMaterial, BackBufferCopyMaterial, {});
	BackBufferCopyMaterial->SetTextureInterface( BackBufferCopyTarget );

	return BackBufferCopyMaterial;

	unguard;
}

UMaterial* FD3DRenderInterface::GetBackBufferMaterial()
{
	guard(FD3DRenderInterface::GetBackBufferMaterial);

#ifndef _XBOX
	return NULL;
#endif

	DECLARE_STATIC_UOBJECT(	UProxyBitmapMaterial, BackBufferMaterial, {});
	BackBufferMaterial->SetTextureInterface( RenDev->DefaultRenderTarget );		

    return BackBufferMaterial;

	unguard;
}

#ifdef _XBOX
/* scion ======================================================================
 * FD3DRenderInterface::DrawPrimitiveUP
 * Author: jg
 * 
 * Uses the xbox specific DrawPrimitiveUP method to copy the verts directly
 * into the push buffer bypassing the dynamic stream bottleneck
 *
 * input:	Shader			The vertex shader to use during the draw
 *			PrimitiveType	The type of primitive we are drawing
 *			NumPrimitives	The number of primitives to copy
 *			pVBStream		The vertex buffer stream that is being copied
 *			pIBStream		The index buffer stream that is being copied
 *
 * ============================================================================
 */
void FD3DRenderInterface::DrawPrimitiveUP(QWORD VertexShaderEnum,
	EPrimitiveType PrimitiveType,INT NumPrimitives,FVertexStream* pVBStream,
	FIndexBuffer* pIBStream)
{
	guard(FD3DRenderInterface::DrawPrimitiveUP);
	FUNC_CLOCK(UPCyc,GStats.DWORDStats(GEngineStats.STATS_PushBuffer_DrawUPCycles));
	// Find or create an appropriate vertex shader.
	FShaderDeclaration ShaderDeclaration;
	ShaderDeclaration.NumStreams = 1;
	ShaderDeclaration.Streams[0] = FStreamDeclaration(pVBStream);
	
	// Set vertex shader enumeration and shader declaration 
	// RenDev->DeferredState.SetShaderDeclaration( ShaderDeclaration );
	FD3DVertexShader* VertexShader = RenDev->GetVertexShader( VertexShaderEnum, ShaderDeclaration );
	CurrentState->VertexShader = VertexShader;
	CurrentState->VertexShaderEnum = VertexShaderEnum;
	RenDev->DeferredState.SetVertexShader( VertexShader );
	RenDev->DeferredState.SetVertexShaderEnum( CurrentState->VertexShaderEnum );

	void* pRawVBData;
	// Get a pointer to the raw vb data so we can copy it
	pVBStream->GetRawStreamData(&pRawVBData,0);
	// If this fails, it means the stream can't be used with DrawPrimUP
	checkSlow(pRawVBData);
	void* pRawIBData = NULL;
	// Check to see if there is an index buffer associated with this
	if (pIBStream != NULL)
	{
		// Can't use DrawPrimUP if this IB doesn't support direct access
		pRawIBData = pIBStream->GetContentsPtr();
		checkSlow(pRawIBData);
	}

	for( INT pass=0;pass<CurrentState->NumMaterialPasses;pass++ )
	{
		SetMaterialBlending( CurrentState->MaterialPasses[pass], CurrentState->CullMode );

		// Fog hacks needed for translucent objects.
		UBOOL RestoreFogColor = 0;
		if( CurrentState->DistanceFogEnabled && CurrentState->CurrentMaterialState->OverrideFogColor )
		{
			RestoreFogColor = 1;
			RenDev->DeferredState.SetRenderState( RS_FOGCOLOR, (DWORD)CurrentState->CurrentMaterialState->OverriddenFogColor );
		}

		// scion sz - 
		// make sure the deffered state vertex shader gets reset to the proper enumeration from the current state
		if( CurrentState->VertexShader )
		{
			RenDev->DeferredState.SetVertexShaderEnum( 
				CurrentState->VertexShaderEnum | CurrentState->MaterialPasses[pass]->MaterialVertexShaderEnum );
		}

		RenDev->DeferredState.Commit();		
				
		// If there is no index stream set, just copy the verts in
		if (pIBStream == NULL)
		{
			// Copy the verts directly to the pushbuffer
			RenDev->Direct3DDevice8->DrawPrimitiveUP(
				(D3DPRIMITIVETYPE)PrimitiveType,NumPrimitives,pRawVBData,
				pVBStream->GetStride());
			STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_PushBuffer_DrawUPCopied),
				NumPrimitives * pVBStream->GetStride());
		}
		else
		{
			// Otherwise copy the indices and the verts
			RenDev->Direct3DDevice8->DrawIndexedPrimitiveUP(
				(D3DPRIMITIVETYPE)PrimitiveType,0,0,NumPrimitives,
				pRawIBData,D3DFMT_INDEX16,pRawVBData,pVBStream->GetStride());
			STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_PushBuffer_DrawUPCopied),
				NumPrimitives * pVBStream->GetStride() + sizeof(_WORD) * NumPrimitives);
		}
		STAT_INC_BY(GStats.DWORDStats(RenDev->D3DStats.STATS_NumSVPVertices),NumPrimitives);

		// Restore fog color set above.
		if( RestoreFogColor )
			RenDev->DeferredState.SetRenderState( RS_FOGCOLOR, CurrentState->DistanceFogColor );

		STAT_INC_BY(GStats.DWORDStats(RenDev->D3DStats.STATS_NumPrimitives),NumPrimitives);
	}

	STAT_INC(GStats.DWORDStats(GEngineStats.STATS_PushBuffer_DrawUPCalls));

	unguard;
}

#include <d3d8perf.h>

/* scion ======================================================================
 * appD3DBeginEvent
 * Author: jg
 * 
 * Adds a PIX event using the D3DPerf api
 *
 * input:	Color The color to draw the event as
 *			szText The label for the event
 *
 * ============================================================================
 */
void appD3DBeginEvent(const FColor& Color,char* szText)
{
	D3DPERF_BeginEvent(Color.DWColor(),szText);
}

/* scion ======================================================================
 * appD3DEndEvent
 * Author: jg
 * 
 * Ends a PIX event
 *
 * ============================================================================
 */
void appD3DEndEvent(void)
{
	D3DPERF_EndEvent();
}

/* scion ======================================================================
 * FD3DRenderInterface::RunPushBuffer
 * Author: jg
 * 
 * Uses the xbox specific RunPushBuffer method to execute a previously built
 * push buffer
 *
 * input:	pPushBuffer FPushBuffer* The push buffer data to run
 *			bUseIndexStream UBOOL Whether to commit the index stream or not
 *			bCommit -- Whether a commit is needed or not
 *
 * ============================================================================
 */
void FD3DRenderInterface::RunPushBuffer(FPushBuffer* pPushBuffer,
	UBOOL bUseIndexStream,UBOOL bCommit)
{
	guard(FD3DRenderInterface::RunPushBuffer);
	FUNC_CLOCK(RPB,GStats.DWORDStats(GEngineStats.STATS_PushBuffer_RunPushBufferCycles));
	// Cache the old index buffer if desired
	D3DIndexBuffer* pBuffer = CurrentState->IndexBuffer ?
		CurrentState->IndexBuffer->Direct3DIndexBuffer8 : NULL;
	// Unset the index stream if desired
	if (bUseIndexStream == 0)
	{
		RenDev->DeferredState.SetIndices(NULL,CurrentState->IndexBufferBase);
	}
	for( INT pass=0;pass<CurrentState->NumMaterialPasses;pass++ )
	{
		UBOOL RestoreFogColor = 0;
		// Don't commit if not needed
		if (bCommit == 1)
		{
			SetMaterialBlending( CurrentState->MaterialPasses[pass], CurrentState->CullMode );
			// Fog hacks needed for translucent objects.
			if( CurrentState->DistanceFogEnabled && CurrentState->CurrentMaterialState->OverrideFogColor )
			{
				RestoreFogColor = 1;
				RenDev->DeferredState.SetRenderState( RS_FOGCOLOR,
					(DWORD)CurrentState->CurrentMaterialState->OverriddenFogColor );
			}

			// scion sz - 
			// make sure the deffered state vertex shader gets reset to the proper enumeration from the current state
			if( CurrentState->VertexShader )
			{
				//scion jg/sz -- Make sure that the material's vertex shader is respected
				RenDev->DeferredState.SetVertexShaderEnum(
					CurrentState->VertexShaderEnum | CurrentState->MaterialPasses[pass]->MaterialVertexShaderEnum);
			}
		
			RenDev->DeferredState.Commit();		
		}
				
		checkSlow(pPushBuffer);
		// Run it without fix ups TODO: Add fixup support
		RenDev->Direct3DDevice8->RunPushBuffer(
			(D3DPushBuffer*)pPushBuffer->GetPushBuffer(),NULL);
		//scion jg -- Attempt to catch nasty GHE errors better
		SINGLESTEPPUSHER;
		STAT_INC(GStats.DWORDStats(GEngineStats.STATS_PushBuffer_RunPushBufferCalls));

		// Don't commit if not needed
		if (bCommit == 1)
		{
			// Restore fog color set above.
			if( RestoreFogColor )
				RenDev->DeferredState.SetRenderState( RS_FOGCOLOR, CurrentState->DistanceFogColor );
		}
	}

	// Restore the index stream if it was removed
	if (bUseIndexStream == 0)
	{
		RenDev->DeferredState.SetIndices(pBuffer,CurrentState->IndexBufferBase);
	}
	unguard;
}

/* scion ======================================================================
 * FD3DRenderInterface::BeginPush
 * Author: jg
 * 
 * Optionally commits the D3D deferred states and gets a pointer to the main
 * push buffer. You must call EndPush for every BeginPush or crash, bang, boom.
 *
 * input:	dwSizeNeeded DWORD The size of the push buffer to write to
 *			bCommitStates UBOOL Whether to commit the D3D states or not
 *			bSetTexCoords Whether to set a 1 to 1 mapping of stage to uv coords
 *						  BeginPush requires a 1 to 1, so unless your material
 *						  sets this up correctly just let this auto do it.
 *						  NOTE: You will need the correct # of UVs
 *
 * returns: The pointer into the push buffer
 *
 * ============================================================================
 */
DWORD* FD3DRenderInterface::BeginPush(DWORD dwSizeNeeded,UBOOL bCommitStates,
	UBOOL bSetTexCoords)
{
	guard(FD3DRenderInterface::BeginPush);
	FUNC_CLOCK(BP,GStats.DWORDStats(GEngineStats.STATS_PushBuffer_BeginPushCycles));
	UBOOL RestoreFogColor = 0;
	// If the caller wants states commited
	if (bCommitStates)
	{
		SetMaterialBlending( CurrentState->MaterialPasses[0], CurrentState->CullMode );

		// If requested, adjust the texture coord remapping
		if (bSetTexCoords)
		{
			// Loop through and force coords. BeginPush does not like tex coord
			// remapping
			for (INT nStage = 0; nStage < 4; nStage++)
			{
				if( CurrentState->MaterialPasses[0]->MaterialVertexShaderEnum || CurrentState->VertexShaderEnum )
				{
 					// scion sz - vertex shaders can handle remapping of tex coord sources correctly.
 					// So, only set hardware state mappings. This prevents D3D from asserting when using a push buffer					
					
					// scion sz - removed since this was causing lot's of weird graphical glitches
					// remapping materials etc.

					/*
 					for (INT nStage = 0; nStage < 4; nStage++)
 					{
 						RenDev->Direct3DDevice8->SetTextureStageState( nStage, 
 							D3DTSS_TEXCOORDINDEX, nStage );
 					}
					*/

					// scion sz - NOTE: reseting all these modes causes particles to not work with distortion (like flame heat shimmer) 
					// This is because we are remapping tex coordinates during SetPixelShaderMaterial.
					/*
					for (INT nStage = 0; nStage < 4; nStage++)
					{
						RenDev->DeferredState.SetTextureStageState(nStage,
							TSS_TEXCOORDINDEX,nStage);
					}
					*/
				}
 				else
 				{
 					for (INT nStage = 0; nStage < 4; nStage++)
 					{
 						RenDev->DeferredState.SetTextureStageState(nStage,
 							TSS_TEXCOORDINDEX,nStage);
 					}
 				}		
			}
		}

		// Fog hacks needed for translucent objects.
		if( CurrentState->DistanceFogEnabled &&
			CurrentState->CurrentMaterialState->OverrideFogColor )
		{
			RestoreFogColor = 1;
			RenDev->DeferredState.SetRenderState( RS_FOGCOLOR,
				(DWORD)CurrentState->CurrentMaterialState->OverriddenFogColor );
		}

		// scion sz - 
		// make sure the deffered state vertex shader gets reset to the proper enumeration from the current state
		if( CurrentState->VertexShader )
		{
			//scion jg -- Make sure that the material's vertex shader is respected
			RenDev->DeferredState.SetVertexShaderEnum(
				CurrentState->VertexShaderEnum | CurrentState->MaterialPasses[0]->MaterialVertexShaderEnum);
		}
	
		RenDev->DeferredState.Commit();
	}
	DWORD* pBuffer = NULL;
	// NOTE: Push buffer size is in DWORDs so the size must be 4byte aligned
	checkSlow(dwSizeNeeded % sizeof(DWORD) == 0);
	// Grab a pointer to the push buffer
	HRESULT hr = RenDev->Direct3DDevice8->BeginPush(
		dwSizeNeeded / sizeof(DWORD),&pBuffer);
	if (FAILED(hr))
	{
		debugf(TEXT("Failed to get a pointer to the push buffer 0x%x. Size too big?"),hr);
	}
	STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_PushBuffer_BeginPushMem),dwSizeNeeded);
	STAT_INC(GStats.DWORDStats(GEngineStats.STATS_PushBuffer_BeginPushCalls));
	// If commited, possibly roll back fog color
	if (bCommitStates)
	{
		// Restore fog color set above.
		if( RestoreFogColor )
			RenDev->DeferredState.SetRenderState( RS_FOGCOLOR, CurrentState->DistanceFogColor );
	}
	return pBuffer;
	unguard;
}

/* scion ======================================================================
 * FD3DRenderInterface::BeginPush
 * Author: jg
 * 
 * This method is used when you need to set a vertex shader and stream
 * declaration before building verts directly into the push buffer.
 *
 * input:	dwSizeNeeded DWORD The size of the push buffer to write to
 *			Shader Which vertex shader to use
 *			pVBStream A vertex stream to get the declaration of
 *			bCommitStates UBOOL Whether to commit the D3D states or not
 *			bSetTexCoords Whether to set a 1 to 1 mapping of stage to uv coords
 *						  BeginPush requires a 1 to 1, so unless your material
 *						  sets this up correctly just let this auto do it.
 *						  NOTE: You will need the correct # of UVs
 *
 * returns: The pointer into the push buffer
 *
 * ============================================================================
 */
DWORD* FD3DRenderInterface::BeginPush(DWORD dwSizeNeeded,QWORD VertexShaderEnum,
	FVertexStream* pVBStream,UBOOL bCommitStates,UBOOL bSetTexCoords)
{
	guard(FD3DRenderInterface::BeginPush);
	FUNC_CLOCK(BP,GStats.DWORDStats(GEngineStats.STATS_PushBuffer_BeginPushCycles));
	DWORD Cycles = appCycles();
	// No point in setting the shader if it won't be committed
	if (bCommitStates)
	{
		// Find or create an appropriate vertex shader.
		FShaderDeclaration ShaderDeclaration;
		ShaderDeclaration.NumStreams = 1;
		ShaderDeclaration.Streams[0] = FStreamDeclaration(pVBStream);
		
		// scion sz -- actual vertex shader created in FD3DDeferredState::Commit
		// Set vertex shader enumeration and shader declaration 
		FD3DVertexShader* VertexShader = RenDev->GetVertexShader( VertexShaderEnum, ShaderDeclaration );
		CurrentState->VertexShader = VertexShader;
		CurrentState->VertexShaderEnum = VertexShaderEnum;
		RenDev->DeferredState.SetVertexShader( VertexShader );
		RenDev->DeferredState.SetVertexShaderEnum( CurrentState->VertexShaderEnum );
	}
	// Use the other version of the routine to get the push buffer pointer
	return BeginPush(dwSizeNeeded,bCommitStates,bSetTexCoords);
	unguard;
}

/* scion ======================================================================
 * FD3DRenderInterface::EndPush
 * Author: jg
 * 
 * Releases the lock on the push buffer. NOTE: D3D will validate that you have
 * not been naughty and written past the amount you requested.
 *
 * input:	pBuffer DWORD* The push buffer pointer after manipulation.
 *
 * ============================================================================
 */
void FD3DRenderInterface::EndPush(DWORD* pBuffer)
{
	guard(FD3DRenderInterface::EndPush);
	FUNC_CLOCK(BP,GStats.DWORDStats(GEngineStats.STATS_PushBuffer_BeginPushCycles));
	// Let D3D have control of the push buffer again
	RenDev->Direct3DDevice8->EndPush(pBuffer);
	//scion jg -- Attempt to catch nasty GHE errors better
	SINGLESTEPPUSHER;
	unguard;
}

/* scion ======================================================================
 * FD3DRenderInterface::InsertFence
 * Author: jg
 * 
 * Inserts a fence into the push buffer. This is how the CPU & GPU synchronize
 * inside of D3DResource::Lock(). By inserting a fence immediately after sending
 * the resource to the GPU, you can make it available to the CPU for locking
 * sooner. This should help, but not remove, Lock() stalls.
 *
 * ============================================================================
 */
DWORD FD3DRenderInterface::InsertFence(void)
{
	return RenDev->Direct3DDevice8->InsertFence();
}
#endif

//
// Constant uploading for SkinVertexShaders.
//
void FD3DRenderInterface::SetSkinBones(INT FirstBoneDestIndex, FCoords* FirstBone, INT BoneCount )
{	
	guard(FD3DRenderInterface::SetSkinBones);
	// We need a continous number of 4x3 matrices to upload.
	// scion sz - static so we construct/allocate BoneUpList once
	static TArray<D3DXVECTOR4> BoneUpList;
	BoneUpList.Reset();
	BoneUpList.Add(BoneCount * 3);

	for( INT t=0; t<BoneCount; t++)
	{
		FCoords& Bone = FirstBone[t];

		D3DXVECTOR4* BoneVector = &BoneUpList(t*3);

		BoneVector[0].x = Bone.XAxis.X;
		BoneVector[0].y = Bone.XAxis.Y;
		BoneVector[0].z = Bone.XAxis.Z;
		BoneVector[0].w = Bone.Origin.X;

		BoneVector[1].x = Bone.YAxis.X;
		BoneVector[1].y = Bone.YAxis.Y;
		BoneVector[1].z = Bone.YAxis.Z;
		BoneVector[1].w = Bone.Origin.Y;

		BoneVector[2].x = Bone.ZAxis.X;
		BoneVector[2].y = Bone.ZAxis.Y;
		BoneVector[2].z = Bone.ZAxis.Z;
		BoneVector[2].w = Bone.Origin.Z;
	}

	// Set bone array in vertex shader constants
	RenDev->SetVertexShaderConstant( VSC_BONEBASE + FirstBoneDestIndex*3, &(BoneUpList(0)), 3*BoneCount );	

	unguard;
}

// scion sz -
// sets the terrain world transform
void FD3DRenderInterface::SetTerrainTransform( FMatrix& WorldMat, INT HeightMapX, INT HeightMapY )
{	
	guard(FD3DRenderInterface::SetTerrainTransform);
#ifdef _XBOX
	FMatrix TerrainWorldMatT = WorldMat.Transpose();
	RenDev->SetVertexShaderConstant( VSC_TERRAIN_WORLD_MATRIX, TerrainWorldMatT.M, 4 );

	checkSlow( HeightMapX > 0 && HeightMapY > 0 );
	FLOAT OneOverHeightMap[4] = { 1.f/(FLOAT)HeightMapX, 1.f/(FLOAT)HeightMapY, 0.f, 0.f };
	RenDev->SetVertexShaderConstant( VSC_TERRAIN_MAP_ONEOVER_SIZE, OneOverHeightMap, 1 );
#endif
	unguard;
}

// scion sz --
//
void FD3DRenderInterface::UpdateDynamicGamma()
{
	guard(FD3DRenderInterface::UpdateDynamicGamma);

#ifndef _XBOX
	return;
#endif

	check(DynamicGamma);

	if( Viewport->GetOuterUClient()->DynamicGamma )
	{
		DynamicGamma->UpdateLuminanceTarget();	
		DynamicGamma->UpdateDeviceGamma();
	}

	if( Viewport->bDebugShowGamma )
		DynamicGamma->ShowDebug();

	unguard;
}

// scion sz --
//
FLOAT FD3DRenderInterface::GetAverageSceneLuminosity()
{
	FLOAT AverageSceneLuminosity = 0.5f;
	if( DynamicGamma )
	{
		AverageSceneLuminosity = (DynamicGamma->CurMaxL - DynamicGamma->CurMinL) * 0.5f ;
	}
	return AverageSceneLuminosity;
}

// scion sz --
//
FD3DDynamicGamma::FD3DDynamicGamma( FD3DRenderInterface* InRI ) :
	RI(InRI),
	UpdateRate(0.05f),
	TransitionRate(0.05f),
	LastUpdateTime(0),
	LuminanceSampleWidth(16),
	LuminanceSampleHeight(16),
	LuminanceTarget(NULL),
	ForceUpdate(0)
{
	appMemzero( &Histogram, sizeof(Histogram) );	
	CurMinL = 0.0f;
	CurMaxL = 1.0f;
	CurAvgL = 0.5f;
}


// scion sz --
//
void FD3DDynamicGamma::UpdateLuminanceTarget()
{
	guard(FD3DDynamicGamma::UpdateLuminanceTarget);

	// create a new render target if it doesn't exist and make sure it's the right size
	if( !LuminanceTarget ||
		LuminanceTarget->GetWidth() != LuminanceSampleWidth ||
		LuminanceTarget->GetHeight() != LuminanceSampleHeight )
	{
		if( LuminanceTarget )
			delete LuminanceTarget;
		LuminanceTarget = NEW FAuxRenderTarget( LuminanceSampleWidth, LuminanceSampleHeight, TEXF_RGBA8, FRenderTarget::RT_CREATE_COLOR );
	}

	check(LuminanceTarget);

	if( LastUpdateTime+UpdateRate > RI->Viewport->CurrentTime )
		return;

	LastUpdateTime = RI->Viewport->CurrentTime;

	RI->SetRenderTarget( LuminanceTarget );
	//scion jg -- Attempt to catch nasty GHE errors better
	RI_SINGLESTEPPUSHER;

	DECLARE_STATIC_UOBJECT(UPixelShaderMaterial,PSLuminanceTransform,
	{		
		PSLuminanceTransform->PixelShaderType = PS_LuminanceTexIndex;
		PSLuminanceTransform->RenderHint = MRH_PixelShaderMaterial;
	});
	DECLARE_STATIC_UOBJECT(	UTexMatrix,	BackBufferTexMatrix, 
	{
		BackBufferTexMatrix->Matrix.SetIdentity();		
	});
	BackBufferTexMatrix->Material = RI->GetBackBufferMaterial();
	PSLuminanceTransform->TextureStage0 = BackBufferTexMatrix;

	static FLOAT U1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	static FLOAT V1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	static FLOAT U2[4] = {1.0f,	1.0f, 1.0f,	1.0f};
	static FLOAT V2[4] = {1.0f,	1.0f, 1.0f,	1.0f};

	FScreenPrimUtil(LuminanceTarget,RI).DrawTile(
		0,0,LuminanceSampleWidth,LuminanceSampleHeight,
		U1,V1,U2,V2,
		0.f, 
		PSLuminanceTransform,
		FColor(255,255,255,255) 
		);
	//scion jg -- Pop the render target change
	RI->ResetRenderTarget();

#ifdef _XBOX
	// make GPU try to release LuminanceTarget texture sooner
	RI->InsertFence();
#endif

	// refresh histogram values
	UpdateHistogram();	

	unguard;
}

// scion sz --
//
void FD3DDynamicGamma::UpdateHistogram()
{
	guard(FD3DDynamicGamma::UpdateHistogram);
	check(LuminanceTarget);

	appMemzero( &Histogram, sizeof(Histogram) );
	
	FD3DTexture* LuminanceTex = (FD3DTexture*) RI->RenDev->GetCachedResource( LuminanceTarget->GetCacheId(),CH_Texture );
	if( !LuminanceTex || !LuminanceTex->Direct3DTexture8 /*|| LuminanceTex->Direct3DTexture8->IsBusy()*/ )
		return;

	DWORD Width = LuminanceTex->CachedWidth;
	DWORD Height = LuminanceTex->CachedHeight;

#ifdef _XBOX
	// don't lock the texture on the xbox
	BYTE* Bits = (BYTE*)(LuminanceTex->Direct3DTexture8->Data | 0x80000000);	
#else
	D3DLOCKED_RECT	LockedRect;
	if( FAILED( LuminanceTex->Direct3DTexture8->LockRect(0,&LockedRect,NULL,0) ) )
		return;
	BYTE* Bits = (BYTE*) LockedRect.pBits;
#endif

	FLOAT LuminosityBias = RI->Viewport->GetOuterUClient()->LuminosityBias;

	// fill in the histogram with values from the Luminosity texture
	Histogram.AvgL = 0.f;
	for( DWORD v=0; v<Height; v++ )	{
		for( DWORD u=0; u<Width; u++ ) {
			BYTE index = *(Bits + v*Width*sizeof(DWORD) + u*sizeof(DWORD));

			// apply luminosity scale to make the scene seem brighter
			index = (BYTE)Clamp<INT>( appTrunc( LuminosityBias * (FLOAT)index ), 0, 255 );

			Histogram.Values[index] += 1.0f;
			Histogram.AvgL += (FLOAT)index;
		}
	}
	
	// normalize histogram values
	FLOAT TotalSamples = Width*Height;
	FLOAT TotalSamplesInv = (TotalSamples > 0.f) ? 1.f/TotalSamples : 0.f;
	for( INT i=0; i<256; i++ ) {
		Histogram.Values[i] *= TotalSamplesInv;
	}

	static FLOAT AreaThreshold = 0.1f;
	FLOAT Area = 0.f;

    // calc minimum luminosity value
	// clamp to MinLThreshold
	Area = 0.f;
	Histogram.MinL = 0.f;
	for( INT i=0; i<256; i++ ) {
		Area += Histogram.Values[i];
		if( Area >= AreaThreshold ) {
			Histogram.MinL = (FLOAT)i/255.f;
			break;
		}
	}

	// calc maximum luminosity value
	// clamp to MaxLThreshold
	Area = 0.f;
	Histogram.MaxL = 1.f;
	for( INT i=255; i>=0; i-- ) {
		Area += Histogram.Values[i];
		if( Area >= AreaThreshold ) {
			Histogram.MaxL = (FLOAT)i/255.f;
			break;
		}
	}

	// calc average luminosity value
	Histogram.AvgL = Clamp<FLOAT>( Histogram.AvgL * TotalSamplesInv * 1.f/255.f, Histogram.MinL, Histogram.MaxL );

	// clamp MinL/MaxL using current contrast setting
	//FLOAT Contrast = RI->Viewport->GetOuterUClient()->Contrast;
	FLOAT Contrast = RI->Viewport->Actor->Level->LevelContrast;
	FLOAT MidPoint = Histogram.MinL + (Histogram.MaxL - Histogram.MinL)*0.5f;
	FLOAT ClampMinL = Max(0.0f, MidPoint - (1.0f - Contrast));
	FLOAT ClampMaxL = Min(1.0f, MidPoint + (1.0f - Contrast));
	Histogram.MinL = Clamp<FLOAT>( Histogram.MinL, 0.0f, ClampMinL );
	Histogram.MaxL = Clamp<FLOAT>( Histogram.MaxL, ClampMaxL, 1.0f );

	ForceUpdate = 1;
	
	unguard;
}

// scion sz --
//
void FD3DDynamicGamma::UpdateDeviceGamma()
{
	guard(FD3DDynamicGamma::UpdateDeviceGamma);

	static const FLOAT UpdateThreshold=0.01f;
	UBOOL DeviceUpdateRequired=0;

	// update current Min/Max/Avg luminosity values
	// only update if the values change by more than UpdateThreshold
	if( Abs(CurMinL-Histogram.MinL) > UpdateThreshold )
	{
		// interpolate between the current min luminosity and
		// the new desired luminosity stored in the histogram
		// this is done at a rate of TransitionRate
		CurMinL = Lerp( CurMinL, Histogram.MinL, TransitionRate );
        DeviceUpdateRequired=1;
	}
	if( Abs(CurMaxL-Histogram.MaxL) > UpdateThreshold )
	{
		CurMaxL = Lerp( CurMaxL, Histogram.MaxL, TransitionRate );
		DeviceUpdateRequired=1;
	}
	if( Abs(CurAvgL-Histogram.AvgL) > UpdateThreshold )
	{
		CurAvgL = Lerp( CurAvgL, Histogram.AvgL, TransitionRate );
		DeviceUpdateRequired=1;
	}

	if( !DeviceUpdateRequired && !ForceUpdate )
		return;

	// distance between current max and min luminosity
	FLOAT Dist = (CurMaxL - CurMinL);
	// current average luminosity is not (max-min)/2
	// calculate ratio of average luminosity (of all historgram values)
	// versus the distance between the max and min luminosity values
	// assumes CurMinL <= CurAvgL <= CurMaxL
	FLOAT AvgRatio = (CurAvgL - CurMinL) / Dist;
	//FLOAT Gamma = RI->Viewport->GetOuterUClient()->Gamma;
	FLOAT Gamma = RI->Viewport->Actor->Level->LevelGamma;
	// typcially the exponent would just be 1/gamma but we want to shift
	// the gamma curve in the direction of the average luminosity
	// this is done by simply adding the ratio of average luminosity to 1/gamma
	static const FLOAT AvgRatioScale = 0.3f;
	FLOAT Power = 1.f/Gamma + AvgRatio*AvgRatioScale;
	FLOAT x=0.f;

	for( INT i=0; i<256; i++ ) {
		x = i/255.f;
		if( x <= CurMinL )
		{
			GammaRamp.red[i] = GammaRamp.green[i] = GammaRamp.blue[i] = 0;
		}
		else if( x >= CurMaxL )
		{
			GammaRamp.red[i] = GammaRamp.green[i] = GammaRamp.blue[i] = 255;
		}
		else
		{
			GammaRamp.red[i] = GammaRamp.green[i] = GammaRamp.blue[i] = Clamp<BYTE>( appTrunc( 255.f * appPow( (x-CurMinL)/Dist, Power ) + 0.5f ), 0, 255 );
		}
	}


	RI->RenDev->Direct3DDevice8->SetGammaRamp( D3DSGR_CALIBRATE, &GammaRamp );		
	//scion jg -- Attempt to catch nasty GHE errors better
	RI_SINGLESTEPPUSHER;

	unguard;
}

// scion sz --
//
void FD3DDynamicGamma::ResetDeviceGamma()
{
	guard(FD3DDynamicGamma::ResetDeviceGamma);

	for( INT i=0; i<256; i++ )	{
		GammaRamp.red[i] = GammaRamp.green[i] = GammaRamp.blue[i] = i << 8;
	}
	RI->RenDev->Direct3DDevice8->SetGammaRamp( D3DSGR_CALIBRATE, &GammaRamp );
	//scion jg -- Attempt to catch nasty GHE errors better
	RI_SINGLESTEPPUSHER;

	unguard;
}


// scion sz --
//
void FD3DDynamicGamma::ShowDebug()
{
	guard(FD3DDynamicGamma::ShowDebug);
	checkSlow( RI->Viewport && RI->Viewport->Canvas );

	FCanvasUtil CanvasUtil( RI->GetRenderTarget(), RI);
	UCanvas* Canvas = RI->Viewport->Canvas;

	static const FLOAT SizeX=100.f;
	static const FLOAT SizeY=100.f;

	static const FColor White(255,255,255,255);
	static const FColor RedDk(128,0,0,255);
	static const FColor RedLt(255,128,128,255);
	static const FColor YellowDk(128,128,0,255);
	static const FColor YellowLt(255,255,128,255);	

	FLOAT OffsetX=5.f;
	FLOAT OffsetY=100.f;	

	if( LuminanceTarget )
	{
		DECLARE_STATIC_UOBJECT(	UProxyBitmapMaterial, LuminanceMaterial, {});
		LuminanceMaterial->SetTextureInterface( LuminanceTarget );

		DECLARE_STATIC_UOBJECT(	UTexMatrix,	LuminanceTexMatrix, 
		{
			LuminanceTexMatrix->Matrix.SetIdentity();
			LuminanceTexMatrix->Material = LuminanceMaterial;
		});

		// show the backbuffer luminance texture	
		CanvasUtil.DrawTile(
			OffsetX, OffsetY, OffsetX+SizeX, OffsetY+SizeY,
			0,0, LuminanceTarget->GetWidth(), LuminanceTarget->GetHeight(),
			0,
			LuminanceTexMatrix,
			White			
			);
	}

	// show the normalized  histogram 
	OffsetX += SizeX;
	FLOAT LargestValue=0.f;
	for( INT i=0; i < 256-1; i++ ) {
        if( Histogram.Values[i]+Histogram.Values[i+1] > LargestValue )
			LargestValue = Histogram.Values[i]+Histogram.Values[i+1];
	}
	FLOAT LargestValueInv = (LargestValue > 0.f) ? 1.f/LargestValue : 0.0f;
	FLOAT SavedOffsetX = OffsetX;
	for( INT i=0; i < 256-1; i++ )
	{
		OffsetX += 0.5f;
		FLOAT Y = ((Histogram.Values[i]+Histogram.Values[i+1])*LargestValueInv*SizeY);
		CanvasUtil.DrawLine( OffsetX, OffsetY+SizeY, OffsetX, OffsetY+SizeY-Y, RedDk );
	}

	// draw tick marks for min/max/avg 
	CanvasUtil.DrawLine( SavedOffsetX+Histogram.MinL*128.f, OffsetY+SizeY, SavedOffsetX+Histogram.MinL*128.f, OffsetY+SizeY-4.f, YellowDk );
	CanvasUtil.DrawLine( SavedOffsetX+Histogram.MaxL*128.f, OffsetY+SizeY, SavedOffsetX+Histogram.MaxL*128.f, OffsetY+SizeY-4.f, YellowDk );
	CanvasUtil.DrawLine( SavedOffsetX+Histogram.AvgL*128.f, OffsetY+SizeY, SavedOffsetX+Histogram.AvgL*128.f, OffsetY+SizeY-4.f, YellowDk );

	// show the gamma curve
	D3DGAMMARAMP TempRamp;
	RI->RenDev->Direct3DDevice8->GetGammaRamp( &TempRamp );
	OffsetX = SavedOffsetX;	
	for( INT i=0; i < 256-1; i++ )
	{
		OffsetX += 0.5f;
		FLOAT Y0 = ((FLOAT)TempRamp.red[i])*SizeY/255.f;
		FLOAT Y1 = ((FLOAT)TempRamp.red[i+1])*SizeY/255.f;
		CanvasUtil.DrawLine( OffsetX, OffsetY+SizeY-Y0, OffsetX+0.5f, OffsetY+SizeY-Y1, YellowDk );
	}

	// display current level gamma/contrast/brightness
	FLOAT Gamma = RI->Viewport->Actor->Level->LevelGamma;
	FLOAT Contrast = RI->Viewport->Actor->Level->LevelContrast;
	FLOAT Brightness = RI->Viewport->Actor->Level->LevelBrightness;
	OffsetY += SizeY+5.f;
	OffsetX = 10.f;
	CanvasUtil.DrawString( OffsetX,OffsetY, *(FString::Printf( TEXT("Gamma:%1.2f"), Gamma )), Canvas->SmallFont,White ); OffsetY+=10.f;
	CanvasUtil.DrawString( OffsetX,OffsetY, *(FString::Printf( TEXT("Contrast:%1.2f"), Contrast )), Canvas->SmallFont,White ); OffsetY+=10.f;
	CanvasUtil.DrawString( OffsetX,OffsetY, *(FString::Printf( TEXT("Brightness:%1.2f"), Brightness )), Canvas->SmallFont,White ); OffsetY+=10.f;
	

	unguard;
}

/* scion ======================================================================
* ::GetLightConfig
* Author: sz
* 
* helper routine to build possible light configurations
* ============================================================================
*/
QWORD GetLightConfig( INT NumDirectional, INT NumPoint )
{
	guard(::GetLightConfig);
	check( NumDirectional >= 0 && NumPoint >= 0 );
	check( NumDirectional + NumPoint <= VS_Lights_MAX );
	
	QWORD LightVSEnum = 0;

	INT VSLightIndex=0;
	for( INT dir=0; dir < NumDirectional;  dir++ )
	{
		VS_SETLIGHTTYPE( VSLightIndex, VS_Light_Type_Directional, LightVSEnum );
		VSLightIndex++;
	}

	for( INT point=0; point < NumPoint;  point++ )
	{
		VS_SETLIGHTTYPE( VSLightIndex, VS_Light_Type_Point, LightVSEnum );
		VSLightIndex++;
	}

	return LightVSEnum;

	unguard;
}

#if 0
/* scion ======================================================================
* FD3DRenderInterface::CreateMaterialVertexShader
* Author: sz
* 
* Sets the material and then tries to create a vertex shader enumeration from
* its state.  This VS Enum is then used to create the combined shader microcode
* using different lighting/fog configs.
* ============================================================================
*/
void FD3DRenderInterface::CreateMaterialVertexShaders( QWORD BaseVertexShaderEnum, UMaterial* Material )
{
	guard(FD3DRenderInterface::CreateMaterialVertexShader);
	
	// set the material in order to get the current state needed to render it
	SetMaterial( Material, NULL, NULL, NULL );

	// for each pass of the current state
	for( INT pass=0; pass < CurrentState->NumMaterialPasses; pass++ )
	{
		// set the base vertex shader for our current state
		// pass in VS_VertexColored and VS_NotBoned, VS_SkelThreeBoned
		QWORD NewVertexShader = BaseVertexShaderEnum|VS_NotBoned;

		// combine with the material's specific vertex shader enum 
		NewVertexShader |= CurrentState->MaterialPasses[pass]->MaterialVertexShaderEnum;

		// combine with the vertex shader fragments needed for the material's tex stage states
		NewVertexShader |= RenDev->DeferredState.GetTexStageVertexShader();

		// try to go through all possible rendering configs and generate vertex shader microcode
		for( INT fog=0; fog<2; fog++ )
		{
			if( fog == 1 )
			{
				// fog is off
				NewVertexShader |= VS_Fog;
			}
			else
			{
				// fog is on
				NewVertexShader &= ~VS_Fog;
			}

			for( INT lit=0; lit<2; lit++ )
			{
				if( lit == 1 )
				{
					// lighting is on
					NewVertexShader |= VS_Lit;

					// generate configs for our light configurations
					// assuming we don't use more than 2 directional lights
					const INT MaxNumDirectionalLights = 2;
					const INT MaxNumPointLights = VS_Lights_MAX;
					for( INT NumDir=0; NumDir < MaxNumDirectionalLights; NumDir++ )
					{
						INT MaxNumPoint = MaxNumPointLights-NumDir;
						for( INT NumPoint=0; NumPoint < MaxNumPoint; NumPoint++ )
						{
							// get light enum for this config of directional and point lights
							QWORD NewVSWithLights = GetLightConfig( NumDir, NumPoint );
							// combine with current vertex shader config
							NewVSWithLights |= NewVertexShader;
							// create the vertex shader for this config and store it
							RenDev->GetCombinedVertexShader( NewVSWithLights );
						}
					}
				}
				else
				{
					// lighting is off
					NewVertexShader &= ~VS_Lit;
					// create the vertex shader for this config and store it
					RenDev->GetCombinedVertexShader( NewVertexShader );					
				}
			}			
		}		
	}

	unguard;
}
#endif

/* scion ======================================================================
 * FD3DRenderInterface::SetRenderStateDirect
 * Author: jg
 * 
 * Sets the specified render state directly into D3D without having to go
 * through the deferred commit. Both the wanted & hardware states are kept in
 * sync.
 *
 * input:	State -- The render state to change
 *			Value -- The new value to set it to
 *
 * ============================================================================
 */
void FD3DRenderInterface::SetRenderStateDirect(BYTE State,DWORD Value)
{
	guard(FD3DRenderInterface::SetRenderStateDirect);
	RenDev->DeferredState.SetRenderStateDirect(State,Value);
	unguard;
}

/* scion ======================================================================
 * FD3DRenderInterface::SetVertexShaderConstant
 * Author: jg
 * 
 * Sets the specified render state directly into D3D without having to go
 * through the deferred commit. Both the wanted & hardware states are kept in
 * sync.
 *
 * input:	Register -- The register to put the new value(s) in
 *			pConstantData -- The new value to set it to
 *			ConstantCount -- The number of constants being written out
 *
 * ============================================================================
 */
void FD3DRenderInterface::SetVertexShaderConstant(INT Register,
	const void* pConstantData,DWORD ConstantCount)
{
	guard(FD3DRenderInterface::SetVertexShaderConstant);
	RenDev->SetVertexShaderConstant(Register,pConstantData,ConstantCount);
	unguard;
}

/* scion ======================================================================
 * FD3DRenderInterface::BeginOcclusionQuery
 * Author: jg
 * 
 * Tells the GPU to start tracking the number of pixels rendered for subsequent
 * rendering calls.
 *
 * returns:	Whether or not the call was successful
 *
 * ============================================================================
 */
UBOOL FD3DRenderInterface::BeginOcclusionQuery(void)
{
	guard(FD3DRenderInterface::BeginOcclusionQuery);
#ifdef _XBOX
	// Tell the device that we want to track the pixel results for future
	// rendering calls
	return RenDev->Direct3DDevice8->BeginVisibilityTest() == S_OK;
#else
	return 1;
#endif
	unguard;
}

/* scion ======================================================================
 * FD3DRenderInterface::EndOcclusionQuery
 * Author: jg
 * 
 * Tells the GPU to stop tracking the number of pixels rendered for previous
 * rendering calls.
 *
 * input:	QueryNum -- The unique query number used to track the pixle count
 *
 * returns:	Whether or not the call was successful
 *
 * ============================================================================
 */
UBOOL FD3DRenderInterface::EndOcclusionQuery(DWORD QueryNum)
{
	guard(FD3DRenderInterface::EndOcclusionQuery);
#ifdef _XBOX
	// Tell the device that we are done for this object
	return RenDev->Direct3DDevice8->EndVisibilityTest(QueryNum) == S_OK;
#else
	return 1;
#endif
	unguard;
}

/* scion ======================================================================
 * FD3DRenderInterface::GetOcclusionQueryResults
 * Author: jg
 * 
 * Gets the number of pixels rendered by the specified occlusion test. You can
 * choose whether to assume the object to be visible or not if the test is still
 * pending in the push buffer.
 *
 * input:	QueryNum -- The unique query number used to track the pixle count
 *			dwPixels -- Out value indicating the number of pixels rendered
 *
 * returns:	Whether the query completed or not
 *
 * ============================================================================
 */
UBOOL FD3DRenderInterface::GetOcclusionQueryResults(DWORD QueryNum,
	DWORD& dwPixels)
{
	guard(FD3DRenderInterface::GetOcclusionQueryResults);
#ifdef _XBOX
	// Request the results from the device
	return RenDev->Direct3DDevice8->GetVisibilityTestResult(QueryNum,
		(UINT*)&dwPixels,NULL) == S_OK;
#else
	return 1;
#endif
	unguard;
}

/* scion ======================================================================
 * FD3DRenderInterface::GetPixelAspectRatio
 * Author: sz
 * 
 * Returns the amount needed to scale the current aspect ratio to match
 * the 10x11 and/or widescreen modes.
 * ============================================================================
 */
FLOAT FD3DRenderInterface::GetPixelAspectRatio()
{
	FLOAT Result=1.f;
#ifdef _XBOX
    if( RenDev->PresentParms.Flags & D3DPRESENTFLAG_10X11PIXELASPECTRATIO )
	{
		// 10x11 pixel aspect		
		Result *= 1.f/1.1f;
	}
	if( RenDev->PresentParms.Flags & D3DPRESENTFLAG_WIDESCREEN )
	{
		// 16:9 widescreen aspect
		Result *= 4.f/3.f * Result;
	}
#endif
	return Result;
}