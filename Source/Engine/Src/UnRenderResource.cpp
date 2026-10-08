/*=============================================================================
	UnRenderResource.cpp: Render resource implementation.
	Copyright 1997-2000 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/

#include "EnginePrivate.h"

IMPLEMENT_CLASS(URenderResource);
IMPLEMENT_CLASS(UVertexStreamBase);
IMPLEMENT_CLASS(UVertexStreamVECTOR);
IMPLEMENT_CLASS(UVertexStreamCOLOR);
IMPLEMENT_CLASS(UVertexStreamUV);
IMPLEMENT_CLASS(UVertexStreamPosNormTex);
IMPLEMENT_CLASS(UVertexBuffer);
IMPLEMENT_CLASS(UIndexBuffer);

//	URenderResource
void URenderResource::Serialize(FArchive& Ar)
{
	guard(URenderResource::Serialize);

	Super::Serialize(Ar);

	Ar << Revision;

	unguard;
}

// UIndexBuffer
void UIndexBuffer::Serialize(FArchive& Ar)
{
	guard(UIndexBuffer::Serialize);

	Super::Serialize(Ar);

	Ar << Indices;

	unguard;
}

//
// UC2 port (M7): the capability behind which the Xbox-only post-process paths run
// (FRenderCaps::XboxPostProcess, UnRenDev.h).
//
ENGINE_API UBOOL RenDevHasXboxPostProcess()
{
	UClient* Client = UTexture::__Client;
	if( !Client || !Client->Engine || !Client->Engine->GRenDev )
		return 0;
	FRenderCaps* Caps = Client->Engine->GRenDev->GetRenderCaps();
	return Caps && Caps->XboxPostProcess;
}

//
// UC2 port (M7b): the capability behind which level-static projectors are batched and drawn
// with the Xbox projector material (FRenderCaps::XboxProjectors, UnRenDev.h; UnGame.cpp).
//
ENGINE_API UBOOL RenDevHasXboxProjectors()
{
#ifdef _XBOX
	return 1;	// Epic's Xbox build always took this branch
#endif
	UClient* Client = UTexture::__Client;
	if( !Client || !Client->Engine || !Client->Engine->GRenDev )
		return 0;
	FRenderCaps* Caps = Client->Engine->GRenDev->GetRenderCaps();
	return Caps && Caps->XboxProjectors;
}

//
// UC2 port (M7b): the capability behind which the Xbox shadow-texture filter runs
// (FRenderCaps::XboxShadows, UnRenDev.h; UnShadowProjector.cpp).
//
ENGINE_API UBOOL RenDevHasXboxShadows()
{
#ifdef _XBOX
	return 1;	// Epic's Xbox build always took this branch
#endif
	UClient* Client = UTexture::__Client;
	if( !Client || !Client->Engine || !Client->Engine->GRenDev )
		return 0;
	FRenderCaps* Caps = Client->Engine->GRenDev->GetRenderCaps();
	return Caps && Caps->XboxShadows;
}

//
// UC2 port (pp-scale): FRenderCaps::PostProcessReferenceHeight of the engine's render device
// (0 without a client, a device or a reference: sizes and distances in back-buffer pixels).
//
ENGINE_API INT RenDevPostProcessReferenceHeight()
{
	UClient* Client = UTexture::__Client;
	if( !Client || !Client->Engine || !Client->Engine->GRenDev )
		return 0;
	FRenderCaps* Caps = Client->Engine->GRenDev->GetRenderCaps();
	return Caps ? Max( Caps->PostProcessReferenceHeight, 0 ) : 0;
}

/* scion ======================================================================
* FRenderTarget::RemovePoolRef
* Author: sz
* 
* Remove a refrence to this render target from its associated RenderTargetPool
* if one exists.
*
* ============================================================================
*/
void FRenderTarget::RemovePoolRef()
{
	guard(FRenderTarget::RemovePoolRef);
	
	if( RenderTargetPool && 
		PoolIndex != INDEX_NONE )
	{
		RenderTargetPool->RemoveRef( this );
	}

	unguard;
}

/* scion ======================================================================
* FRenderTargetPool::ObtainRenderTarget
* Author: sz
* 
* Returns a render target of the given size and format, or creates a new one
* if one doesn't exist
*
* ============================================================================
*/
FRenderTarget* FRenderTargetPool::ObtainRenderTarget( INT Width, INT Height, ETextureFormat TexFormat, INT CreationFlags )
{
	guard(FRenderTargetPool::ObtainRenderTarget);

	FRenderTarget* pRenderTarget=NULL;
	for( INT i=0; i<RenderTargets.Num(); i++ )
	{
		pRenderTarget = RenderTargets(i);

		if( pRenderTarget && 
			pRenderTarget->GetWidth() == Width &&
			pRenderTarget->GetHeight() == Height && 
			pRenderTarget->CreationFlags == CreationFlags &&
			pRenderTarget->GetFormat() == TexFormat )
		{
			// increment ref count of the matching render target and return it
			RefCounts(i) += 1;
			return pRenderTarget;			
		}        		
	}

	// create a new render target if no match was found and adds it to the
	// list of render targets
	pRenderTarget = NEW FAuxRenderTarget(Width, Height, TexFormat, CreationFlags );
	RenderTargets.AddItem( pRenderTarget );

	// ref count starts at 1
	pRenderTarget->PoolIndex = RefCounts.AddItem( 1 );
	pRenderTarget->RenderTargetPool = this;

    return pRenderTarget;

	unguard;
}

/* scion ======================================================================
* FRenderTargetPool::ClearRenderTargets
* Author: sz
* 
* Delete all render targets and clear the array
*
* ============================================================================
*/
void FRenderTargetPool::ClearRenderTargets()
{
	guard(FRenderTargetPool::ClearRenderTargets);

	for( INT i=0; i<RenderTargets.Num(); i++ )
	{
		check( RefCounts(i) == 0 );
		if( RenderTargets(i) )
		{
			delete RenderTargets(i);
			RenderTargets(i) = NULL;
		}
	}
	RenderTargets.Empty();
	RefCounts.Empty();

	unguard;
}

/* scion ======================================================================
* FRenderTargetPool::AddRef
* Author: sz
* 
* Add a refrence to the given render target. Increment refcount by 1.
*
* ============================================================================
*/
void FRenderTargetPool::AddRef( FRenderTarget* RenderTarget )
{
	guard( FRenderTargetPool::AddRef );
	check( RenderTarget );

	if( RenderTarget->RenderTargetPool == this && 
		RenderTarget->PoolIndex != INDEX_NONE )
	{
		RefCounts(RenderTarget->PoolIndex) += 1;
	}

	unguard;
}

/* scion ======================================================================
* FRenderTargetPool::RemoveRef
* Author: sz
* 
* Remove a refrence to the given render target.  Decrement refcount by 1. 
* If its refcount is <= 0 then it can be removed from the render target pool.
*
* ============================================================================
*/
void FRenderTargetPool::RemoveRef( FRenderTarget* RenderTarget )
{
	guard( FRenderTargetPool::RemoveRef );
	check( RenderTarget );

	INT PoolIndex = RenderTarget->PoolIndex;

	// decrement ref count for this render target item
	if( PoolIndex != INDEX_NONE )
	{
		RefCounts(PoolIndex) -= 1;
		check( RefCounts(PoolIndex) >= 0 );
	}

	// remove if the ref count reaches 0
	if( RefCounts(PoolIndex) == 0 &&
		RenderTargets(PoolIndex) )
	{
		delete RenderTargets(PoolIndex);
		RenderTargets(PoolIndex) = NULL;
		// don't remove the entry since other render target pool 
		// indices need to still be valid
	}

	unguard;
}

//scion jg -- Flush any D3D resource associated with this
FRenderResource::~FRenderResource()
{
	checkSlow(CacheId == INVALID_CACHEID);
}

/**
 * Flushes the resource from the renderer
 */
void FRenderResource::Flush(void)
{
	guard(FRenderResource::Flush);
	// Don't flush if we haven't been cached
	if (CacheId != INVALID_CACHEID && m_pRenDev != NULL)
	{
		// Figure out which cache to remove us from
		ECacheHint CacheType = GetBaseTextureInterface() != NULL ? CH_Texture :
			GetVertexStreamInterface() != NULL ? CH_VertexBuffer : CH_IndexBuffer;
		if (m_pRenDev->ResourceCached(CacheId,CacheType))
		{
			// Flush the resource from the cache
			m_pRenDev->FlushResource(CacheId,CacheType);
		}
		// Break the link with D3D
		CacheId = INVALID_CACHEID;
		m_pRenDev = NULL;
	}
	unguard;
}

//scion jg -- Flush any D3D resource associated with this
FVertexStream::~FVertexStream()
{
	// Flush the resource if we have one
	if (m_pRenDev != NULL && CacheId != INVALID_CACHEID)
	{
		m_pRenDev->FlushResource(CacheId,CH_VertexBuffer);
		// Make sure this is cleared
		SetCacheId(INVALID_CACHEID,NULL);
	}
}

//scion jg -- Flush any D3D resource associated with this
FIndexBuffer::~FIndexBuffer()
{
	// Flush the resource if we have one
	if (m_pRenDev != NULL && CacheId != INVALID_CACHEID)
	{
		m_pRenDev->FlushResource(CacheId,CH_IndexBuffer);
		// Make sure this is cleared
		SetCacheId(INVALID_CACHEID,NULL);
	}
}

//scion jg -- Flush any D3D resource associated with this
FBaseTexture::~FBaseTexture()
{
	// Flush the resource if we have one
	if (m_pRenDev != NULL && CacheId != INVALID_CACHEID)
	{
		m_pRenDev->FlushResource(CacheId,CH_Texture);
		// Make sure this is cleared
		SetCacheId(INVALID_CACHEID,NULL);
	}
}
