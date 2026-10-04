/*=============================================================================
	D3DResource.cpp: Unreal Direct3D resource implementation.
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/

#include "D3DDrv.h"

#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
#define LOAD_VERTEX_SHADERS_HEADERS
#endif

#ifdef LOAD_VERTEX_SHADERS_HEADERS
#include "XboxVertexShaders.h"
#endif

#define RESOURCE_POOL D3DPOOL_MANAGED

INT appAnsiStrlen( ANSICHAR* p );

//
//	GetResourceHashIndex
//
INT GetResourceHashIndex(QWORD CacheId)
{
	return ((DWORD) CacheId & 0xfff) ^ (((DWORD) CacheId & 0xff00) >> 4) + (((DWORD) CacheId & 0xf0000) >> 16);
}

//
//	FD3DResource::FD3DResource
//
FD3DResource::FD3DResource(UD3DRenderDevice* InRenDev,FRenderResource* InRes)
{
	guard(FD3DResource::FD3DResource);

#ifdef _XBOX
	//scion jg -- Default to not using UMA
	bIsUMA = 0;
#endif

	RenDev = InRenDev;
	//scion jg -- Default to an uncached state. This is for the re-written
	// resource caching scheme
	CacheId = INVALID_CACHEID;
	HashNext = NULL;
	// This is for maintaining a link between engine/d3d
	RenderResource = InRes;

	CachedRevision = 0;

	// Add this resource to the device resource list.
	NextResource = RenDev->ResourceList;
	RenDev->ResourceList = this;

	LastFrameUsed = 0;

	unguard;
}

//
//	FD3DTexture::FD3DTexture
//
FD3DTexture::FD3DTexture(UD3DRenderDevice* InRenDev,FRenderResource* InRes) :
	FD3DResource(InRenDev,InRes)
{
	RenderTargetSurface		= NULL;
	DepthStencilSurface		= NULL;
	Direct3DTexture8		= NULL;
	Direct3DCubeTexture8	= NULL;
#ifdef _XBOX
	bIsUMA = 1;
#ifdef XBOX_ASSET_FORMAT
	bCameFromXPR = 0;
#endif
#endif
}

//
//	FD3DTexture::~FD3DTexture
//
FD3DTexture::~FD3DTexture()
{
	if( RenderTargetSurface )
		RenderTargetSurface->Release();
	if( DepthStencilSurface )
		DepthStencilSurface->Release();
	DeleteTexture();
	DeleteCubeMap();
}

/* ============================================================================
 * FD3DTexture::DeleteTexture
 * Author: jg
 * 
 * Checks for UMA support and deletes the texture accordingly
 *
 * ============================================================================
 */
void FD3DTexture::DeleteTexture(void)
{
	guard(FD3DTexture::DeleteTexture);
	if (Direct3DTexture8 != NULL)
	{
#ifdef _XBOX
		if (bIsUMA == 1)
		{
			Direct3DTexture8->BlockUntilNotBusy();
#ifdef XBOX_ASSET_FORMAT
			if (bCameFromXPR == 0)
			{
#endif
			// Get the real texture address
			BYTE* p = (BYTE*)(Direct3DTexture8->Data | 0x80000000);
			// Release the texture memory first
			appContiguousFree((void*)p);
			// Now delete the header
			delete Direct3DTexture8;
#ifdef XBOX_ASSET_FORMAT
			}
#endif
		}
		else
#endif
		{
			// The old fashioned way
			Direct3DTexture8->Release();
		}
	}
	Direct3DTexture8 = NULL;
	unguard;
}

/* ============================================================================
 * FD3DTexture::DeleteCubeMap
 * Author: jg
 * 
 * Checks for UMA support and deletes the cube map texture accordingly
 *
 * ============================================================================
 */
void FD3DTexture::DeleteCubeMap(void)
{
	guard(FD3DTexture::DeleteCubeMap);
	if (Direct3DCubeTexture8 != NULL)
	{
#ifdef _XBOX
		if (bIsUMA == 1)
		{
#ifdef XBOX_ASSET_FORMAT
			if (bCameFromXPR == 0)
			{
#endif
			Direct3DCubeTexture8->BlockUntilNotBusy();
			// Get the real texture address
			BYTE* p = (BYTE*)(Direct3DCubeTexture8->Data | 0x80000000);
			// Release the texture memory first
			appContiguousFree((void*)p);
			// Now delete the header
			delete Direct3DCubeTexture8;
#ifdef XBOX_ASSET_FORMAT
			}
#endif
		}
		else
#endif
		{
			// The old fashioned way
			Direct3DCubeTexture8->Release();
		}
	}
	Direct3DCubeTexture8 = NULL;
	unguard;
}

//
//  FD3DTexture::CalculateFootprint
//
INT FD3DTexture::CalculateFootprint()
{
	INT	Bytes = 0;

	/*
	for(INT MipIndex = CachedFirstMip;MipIndex < CachedNumMips;MipIndex++)
		Bytes += (GetFormatBPP(CachedFormat) * (CachedWidth >> MipIndex) / 8) * (CachedHeight >> MipIndex);
	*/

	D3DSURFACE_DESC SurfaceInfo;

	if( Direct3DTexture8 )
	{
		for( DWORD LevelIdx=0; LevelIdx < Direct3DTexture8->GetLevelCount(); LevelIdx++ )
		{
			Direct3DTexture8->GetLevelDesc( LevelIdx, &SurfaceInfo );
			Bytes += SurfaceInfo.Size;
		}
	}

	if( Direct3DCubeTexture8 )
	{
		for( DWORD LevelIdx=0; LevelIdx < Direct3DCubeTexture8->GetLevelCount(); LevelIdx++ )
		{
			Direct3DCubeTexture8->GetLevelDesc( LevelIdx, &SurfaceInfo );
			Bytes += SurfaceInfo.Size;
		}
	}

	return Bytes;
}

//
//	FD3DTexture::Cache
//
UBOOL FD3DTexture::Cache(FBaseTexture* SourceTexture)
{
	guard(FD3DTexture::Cache);

	UBOOL	Failed = 0;
	INT		FirstMip = SourceTexture->GetFirstMip(),
			Width	 = SourceTexture->GetWidth(),
			Height	 = SourceTexture->GetHeight(),
			NumMips  = SourceTexture->GetNumMips();
	HRESULT	Result;

	// Determine the source texture format.
	ETextureFormat	SourceFormat =		SourceTexture->GetFormat();

	// Determine the actual texture format.
	ETextureFormat	ActualFormat =		SourceFormat == TEXF_P8		? TEXF_RGBA8 :
										SourceFormat == TEXF_G16	? TEXF_RGBA8 :
										SourceFormat == TEXF_RGBA7	? TEXF_RGBA8 :
										SourceFormat;

	FCompositeTexture*	CompositeTexture = SourceTexture->GetCompositeTextureInterface();
	FCubemap*			Cubemap = SourceTexture->GetCubemapInterface();
	FTexture*			Texture = SourceTexture->GetTextureInterface();
	//scion jg -- Clear the memory usage tracking
	if (Texture != NULL && Texture->GetUTexture() != NULL)
	{
		Texture->GetUTexture()->MemUsage = 0;
	}
	FRenderTarget*		RenderTarget = SourceTexture->GetRenderTargetInterface();

	// Verify NumMips.
	INT MinDimension = Min( Width, Height );
	INT LogDimension = 0;
	while( MinDimension > 0 )
	{
		MinDimension >>= 1;
		LogDimension++;
	}
	
	// Spit our warning if too many miplevels.
	//if( NumMips > LogDimension && Texture )
	//	debugf(NAME_DevD3D, TEXT("Texture [%s] is %ix%i and has %i Mips instead of %i"), Texture->GetUTexture()->GetPathName(), Width, Height, NumMips, LogDimension );

    if( IsDXTC(SourceFormat) && (Width < 4 || Height < 4) )
	{
		warnf( TEXT("Texture [%s] is %ix%i and DXT formats require minimum UV dimensions of 4"), Texture->GetUTexture()->GetPathName(), Width, Height );
		return 0;
	}

	// Clamp number of miplevels to avoid crashes.
	NumMips = Min( NumMips, LogDimension );

	// Report dynamic uploads if wanted.
	UBOOL Precaching = RenDev->LockedViewport ? RenDev->LockedViewport->Precaching : 0;
	if( !GIsEditor && !Precaching && UTexture::__Client && UTexture::__Client->ReportDynamicUploads )
	{
		INT Size = 0;
		for( INT i=FirstMip; i<NumMips; i++ )
			Size += GetBytesPerPixel(ActualFormat,(Width >> i) * (Height >> i));
		if( RenderTarget )
		{
			FString Usage;
			if( RenderTarget->CreationFlags & FRenderTarget::RT_CREATE_COLOR )
				Usage += TEXT("COLOR");
			if( RenderTarget->CreationFlags & FRenderTarget::RT_CREATE_DEPTH )
				Usage += TEXT("|DEPTH");

			debugf(NAME_DevD3D,TEXT("Uploading RenderTarget: Size=[%i] Dim=(%ix%i) Mips=%i Format=%s Usage="),
				Size,
				RenderTarget->GetWidth(),
				RenderTarget->GetHeight(),
				RenderTarget->GetNumMips(),
				UTexture::GetFormatDesc( RenderTarget->GetFormat() )
				); 
		}
		else if( CompositeTexture )
		{
			debugf(NAME_DevD3D,TEXT("Uploading Lightmap!!!: Size=[%i] Dim=(%ix%i) Mips=%i Format=%s"), 
				Size, 
				CompositeTexture->GetWidth(), 
				CompositeTexture->GetHeight(),
				CompositeTexture->GetNumMips(),
				UTexture::GetFormatDesc( CompositeTexture->GetFormat() )
				);
		}
		else if( Cubemap )
		{
			debugf(NAME_DevD3D,TEXT("Uploading Cubemap: Texture=[%s], Size=[%i]"),Cubemap->GetFace(0)->GetUTexture()->GetPathName(),Size*6);
		}
		else if( Texture )
		{
			debugf(NAME_DevD3D,TEXT("Uploading Texture: Texture=[%s], Size=[%i] Dim=(%ix%i) Mips=%i Format=%s"),
				Texture->GetUTexture()->GetPathName(),
				Size,
				Texture->GetWidth(),
				Texture->GetHeight(),
				Texture->GetNumMips(),
				UTexture::GetFormatDesc( Texture->GetFormat() )
				);
		}
	}

	// Use first face if cubemaps aren't supported.
	if( Cubemap && !RenDev->SupportsCubemaps ) 
	{
		Texture = Cubemap->GetFace(0);
		Cubemap = NULL;
	}

	// Determine the corresponding Direct3D format.
	D3DFORMAT		Direct3DFormat =	ActualFormat == TEXF_RGBA8	? (RenDev->SupportsTextureFormat(TEXF_RGBA8) ? D3DFMT_A8R8G8B8 : D3DFMT_A4R4G4B4 ) : 
										ActualFormat == TEXF_DXT1	? D3DFMT_DXT1 :
										ActualFormat == TEXF_DXT3	? D3DFMT_DXT3 :
										ActualFormat == TEXF_DXT5	? D3DFMT_DXT5 :
										ActualFormat == TEXF_V8U8	? D3DFMT_V8U8 :
										ActualFormat == TEXF_L6V5U5 ? D3DFMT_L6V5U5 :
										ActualFormat == TEXF_V16U16	? D3DFMT_V16U16 :
										ActualFormat == TEXF_L8 ? D3DFMT_L8 :
										ActualFormat == TEXF_A8 ? D3DFMT_A8 :
										ActualFormat == TEXF_R5G6B5 ? D3DFMT_R5G6B5 :
										D3DFMT_UNKNOWN;

	// scion sz -
	// some devices don't support A8 format.
	// store as L8 format instead
	if( Direct3DFormat == D3DFMT_A8 && !RenDev->UseA8 )
	{
		Direct3DFormat = D3DFMT_L8;
	}

	// Calculate the first mipmap to use.
	UBOOL DownSample = 0;
	while((Width >> FirstMip) > (INT) RenDev->DeviceCaps8.MaxTextureWidth || (Height >> FirstMip) > (INT) RenDev->DeviceCaps8.MaxTextureHeight)
		if(++FirstMip >= NumMips)
			DownSample = 1;

	if( DownSample )
		FirstMip = 0;

	INT	USize = Width  >> FirstMip,
		VSize = Height >> FirstMip;

	INT ScaleX = 1,
		ScaleY = 1;
	if( DownSample )
	{
		USize	= Min<INT>(USize, RenDev->DeviceCaps8.MaxTextureWidth);
		VSize	= Min<INT>(VSize, RenDev->DeviceCaps8.MaxTextureHeight);
		ScaleX	= Width  / USize;
		ScaleY	= Height / VSize;
		//if( SourceTexture->GetTextureInterface() )
		//	debugf(NAME_DevD3D,TEXT("Downsampling: %s"), SourceTexture->GetTextureInterface()->GetUTexture()->GetFullName());
	}

	if( !VSize || !USize )
	{
		if(SourceTexture->GetTextureInterface() && SourceTexture->GetTextureInterface()->GetUTexture())
			debugf(NAME_DevD3D,TEXT("ERROR! ZERO USIZE OR VSIZE ON TEXTURE: %s"), SourceTexture->GetTextureInterface()->GetUTexture()->GetFullName());

		Direct3DCubeTexture8	= NULL;
		Direct3DTexture8		= NULL;
	}
	else if( CompositeTexture )
	{
		//!!vogel
		// In UT2003 lightmaps are the only composite textures. They are opaque RGBA8888 512x512 and
		// we therefore can easily convert them to 256x256 RGB565. If this assumption ever changes please
		// change the code below.
		INT	NumChildren = CompositeTexture->GetNumChildren();

		if(!Direct3DTexture8 || CachedWidth != USize || CachedHeight != VSize || CachedFirstMip != 0 || CachedNumMips != 1 || CachedFormat != Direct3DFormat)
		{
			//jg -- Use common method for cleaning up
			DeleteTexture();
#ifdef _XBOX
			//jg -- UMA allocation for textures. Header from non-contiguous RAM.
			// Data from contiguous RAM
            Direct3DTexture8 = NEW D3DTexture;
            // Set the header info and get the size needed
			DWORD SizeNeeded = XGSetTextureHeader(USize,VSize,1,0,Direct3DFormat,
				D3DPOOL_DEFAULT,Direct3DTexture8,0,0);
			// Grab a contiguous block memory
			void* pTexMem = appContiguousMalloc(SizeNeeded,D3DTEXTURE_ALIGNMENT);
			// Now set it in the header and strip off the physical bit
			Direct3DTexture8->Register(pTexMem);
#else
			// Voodoo 3 specific hack.
			if( ActualFormat == TEXF_RGBA8 && !RenDev->SupportsTextureFormat(TEXF_RGBA8) )
				Direct3DFormat = D3DFMT_R5G6B5;

			// Create a new Direct3D texture.
			Result = RenDev->Direct3DDevice8->CreateTexture(
				USize,
				VSize,
				1,
				0,
				Direct3DFormat,
				D3DPOOL_MANAGED,
				&Direct3DTexture8
				);
			if( FAILED(Result) )
				appErrorf(TEXT("CreateTexture failed(%s)."),*D3DError(Result));
#endif	

			CachedChildRevision.Empty(NumChildren);
		}

		if(CachedChildRevision.Num() != NumChildren)
		{
			CachedChildRevision.Empty(NumChildren);
			CachedChildRevision.AddZeroed(NumChildren);

			// Lock the Direct3D texture.
			D3DLOCKED_RECT	LockedRect;

			Result = Direct3DTexture8->LockRect(0,&LockedRect,NULL,0);

			if( FAILED(Result) )
				appErrorf(TEXT("LockRect failed(%s)."),*D3DError(Result));

			// Copy all children into the texture.
			BYTE*	Data	= (BYTE*) LockedRect.pBits;
			INT		Pitch	= LockedRect.Pitch;
#ifdef _XBOX
			//!!vogel: TODO: don't use dynamic memory allocation.
			if ( ActualFormat == TEXF_RGBA8 )
				Data = new BYTE[VSize * LockedRect.Pitch];
#else
			if( Direct3DFormat == D3DFMT_R5G6B5 )
			{
				Pitch	= Width * 4;
				Data	= new BYTE[Height * Pitch];
			}
#endif
			for(INT ChildIndex = 0;ChildIndex < NumChildren;ChildIndex++)
			{
				INT			ChildX = 0,
							ChildY = 0;
				FTexture*	Child = CompositeTexture->GetChild(ChildIndex,&ChildX,&ChildY);
				INT			ChildRevision = Child->GetRevision();
				BYTE*		TileData = CalculateTexelPointer(Data,ActualFormat,Pitch,ChildX,ChildY);

				// Read the child texture into the locked region.
				Child->GetTextureData(0,TileData,Pitch,ActualFormat,0);

				CachedChildRevision(ChildIndex) = ChildRevision;
			}

#ifdef _XBOX
			// Non compressed textures need swizzling.
			if( ActualFormat == TEXF_RGBA8 )
			{
				XGSwizzleRect(Data,LockedRect.Pitch,NULL,LockedRect.pBits,USize,VSize,NULL,4);
				delete [] Data;
			}
#else
			if( Direct3DFormat == D3DFMT_R5G6B5 )
			{
				// Convert 512x512 RGBA8888 lightmap to 256x256 RGB565
				DWORD* Src = (DWORD*) Data;
				_WORD* Dst = (_WORD*) LockedRect.pBits;
				
				check( (USize == 256) && (VSize == 256) && DownSample );
				for( INT y=0; y<VSize; y++ )
				{
					for( INT x=0; x<USize; x++ )
					{
						*(Dst++) = FColor(*Src).HiColor565();
						Src += 2;
					}
					Src += USize * 2;
				}
				delete [] Data;
			}
#endif
			// Unlock the Direct3D texture.
			Direct3DTexture8->UnlockRect(0);
		}
		else
		{
			// XBox relies on this code never be called.
			check(GIsEditor);

			// Update modified subrects of the texture.
			for(INT ChildIndex = 0;ChildIndex < NumChildren;ChildIndex++)
			{
				INT			ChildX = 0,
							ChildY = 0;
				FTexture*	Child = CompositeTexture->GetChild(ChildIndex,&ChildX,&ChildY);
				INT			ChildRevision = Child->GetRevision();

				if(CachedChildRevision(ChildIndex) != ChildRevision)
				{
					// Lock the Direct3D texture.
					D3DLOCKED_RECT	LockedRect;
					RECT			ChildRect;
			
					ChildRect.left = ChildX;
					ChildRect.top = ChildY;
					ChildRect.right = ChildX + Child->GetWidth();
					ChildRect.bottom = ChildY + Child->GetHeight();

					Result = Direct3DTexture8->LockRect(0,&LockedRect,&ChildRect,0);

					if( FAILED(Result) )
						appErrorf(TEXT("LockRect failed(%s)."),*D3DError(Result));

					// Read the child texture into the locked region.
					Child->GetTextureData(0,(void*) LockedRect.pBits,LockedRect.Pitch,ActualFormat,0);

					// Unlock the Direct3D texture.
					Direct3DTexture8->UnlockRect(0);

					CachedChildRevision(ChildIndex) = ChildRevision;
				}
			}
		}
	}
	else if ( Cubemap && RenDev->SupportsCubemaps && RenDev->SupportsTextureFormat(ActualFormat) )
	{
		//!! TODO: conversion code.
		if(!Direct3DCubeTexture8 || CachedWidth != USize || CachedHeight != VSize || CachedFirstMip != FirstMip || CachedNumMips != NumMips || CachedFormat != Direct3DFormat)
		{
			//jg -- Use common method for cleaning up
			DeleteCubeMap();
#if defined(_XBOX) && defined(XBOX_ASSET_FORMAT)
			if (Cubemap != NULL && Cubemap->GetUCubemap() != NULL)
			{
				extern LPDIRECT3DCUBETEXTURE8 GetCubemapFromXPR(const TCHAR* Name);
				//jg -- Load from XPR file if possible
				Direct3DCubeTexture8 = GetCubemapFromXPR(Cubemap->GetUCubemap()->GetName());
				// In place construct a cube map of the default texture if failed
				if (Direct3DCubeTexture8 == NULL)
				{
					debugf(TEXT("Failed to find %s in XPRs"),
						Cubemap->GetUCubemap()->GetFullName());
					// Make this a static so all missing cubemaps are the same
					static LPDIRECT3DCUBETEXTURE8 DefaultCubemap = NULL;
					if (DefaultCubemap == NULL)
					{
						// Create a bogus 4x4 cubemap
						RenDev->Direct3DDevice8->CreateCubeTexture(4,1,0,
							D3DFMT_DXT1,D3DPOOL_MANAGED,&DefaultCubemap);
					}
					Direct3DCubeTexture8 = DefaultCubemap;
				}
				// If it was found, we're done. Otherwise fallback to the old way
				if (Direct3DCubeTexture8 != NULL)
				{
					bCameFromXPR = 1;
					CachedRevision	= SourceTexture->GetRevision();
					CachedWidth		= USize;
					CachedHeight	= VSize;
					CachedFirstMip	= FirstMip;
					CachedNumMips	= NumMips;
					CachedFormat	= Direct3DFormat;
					return 1;
				}
			}
#endif
#ifdef _XBOX
			//jg -- UMA allocation for textures. Header from non-contiguous RAM.
			// Data from contiguous RAM
            Direct3DCubeTexture8 = NEW D3DCubeTexture;
            // Set the header info and get the size needed
			DWORD SizeNeeded = XGSetCubeTextureHeader(USize,
				RenDev->UseMippedCubemaps ? NumMips - FirstMip : 1,0,
				Direct3DFormat,D3DPOOL_DEFAULT,Direct3DCubeTexture8,0,0);
			// Grab a contiguous block memory
			void* pTexMem = appContiguousMalloc(SizeNeeded,D3DTEXTURE_ALIGNMENT);
			// Now set it in the header and strip off the physical bit
			Direct3DCubeTexture8->Register(pTexMem);
#else
			if( Width != Height )
				appErrorf(TEXT("Cubemaps must be square"));

			// Create a new Direct3D texture.
			Result = RenDev->Direct3DDevice8->CreateCubeTexture(
				USize,
				RenDev->UseMippedCubemaps ? NumMips - FirstMip : 1,
				0,
				Direct3DFormat,
				D3DPOOL_MANAGED,
				&Direct3DCubeTexture8
				);
			if( FAILED(Result) )
				appErrorf(TEXT("CreateCubeTexture failed(%s)."),*D3DError(Result));
#endif	
		}

		// Copy the cubemap into the Direct3D cube texture.
		INT Count = RenDev->UseMippedCubemaps ? NumMips : FirstMip+1; 
		for(INT MipIndex=FirstMip; MipIndex<Count; MipIndex++)
		{
			// Lock the Direct3D texture.
			D3DLOCKED_RECT	LockedRect;
	
			for(INT Face=0; Face<6; Face++)
			{
				FTexture*	Texture = Cubemap->GetFace(Face);

				if(Texture)
				{
					Result = Direct3DCubeTexture8->LockRect((D3DCUBEMAP_FACES)Face,MipIndex-FirstMip,&LockedRect,NULL,0);

					if(Result != D3D_OK)
						appErrorf(TEXT("LockRect failed(%s)."),*D3DError(Result));
#ifdef _XBOX
					// Non compressed textures need swizzling.
					if ( ActualFormat == TEXF_RGBA8 )
					{
						INT MipWidth  = USize >> (MipIndex - FirstMip);
						INT MipHeight = VSize >> (MipIndex - FirstMip);
						check( MipHeight && MipWidth );
						//!!vogel: TODO: don't use dynamic memory allocation.
						BYTE* Data  = new BYTE[MipHeight * LockedRect.Pitch];
						Texture->GetTextureData(MipIndex,(void*) Data,LockedRect.Pitch,ActualFormat);
						XGSwizzleRect(Data,LockedRect.Pitch,NULL,LockedRect.pBits,MipWidth,MipHeight,NULL,4);
						delete [] Data;
					}
					else
#endif
					Texture->GetTextureData(MipIndex,(void*) LockedRect.pBits,LockedRect.Pitch,ActualFormat);

					// Unlock the Direct3D texture.
					Direct3DCubeTexture8->UnlockRect((D3DCUBEMAP_FACES)Face,MipIndex-FirstMip);
				}
			}
		}
	}
	else if(RenderTarget)
	{
#ifdef _XBOX
		// scion sz -
		// non power of 2 textures (typical render target) require a non-swizzled linear format on the Xbox
		switch( Direct3DFormat )
		{
		case D3DFMT_L8:
			// this format does not work with z buffering
			Direct3DFormat = D3DFMT_LIN_L8;
			break;
		case D3DFMT_R5G6B5:
			Direct3DFormat = D3DFMT_LIN_R5G6B5;
			break;
		default:
			Direct3DFormat = D3DFMT_LIN_A8R8G8B8;
		}
#else
		//scion jg -- PC can't handle L8 so make D3DFMT_A8R8G8B8 instead
		if (Direct3DFormat == D3DFMT_L8)
		{
			Direct3DFormat = D3DFMT_A8R8G8B8;
		}
#endif
		if( RenderTarget == RenDev->DefaultRenderTarget )
		{
#ifdef _XBOX
			//jg -- These are no allocated via my routine
			bIsUMA = 0;
#endif
			// -- use default render target surfaces
			RenderTargetSurface		= RenDev->DefaultRenderTarget->BackBufferSurface;
			DepthStencilSurface		= RenDev->DefaultRenderTarget->DepthBufferSurface;
			if( RenderTarget->MaterialUsageFlag == FRenderTarget::RT_MATERIAL_USE_DEPTHBUFFER )
			{
				Direct3DTexture8	= RenDev->DefaultRenderTarget->DepthBufferTexture;
			}
			else if( RenderTarget->MaterialUsageFlag == FRenderTarget::RT_MATERIAL_USE_FRONTBUFFER )
			{
				Direct3DTexture8	= RenDev->DefaultRenderTarget->FrontBufferTexture;
				Direct3DTexture8->AddRef();
			}
			else // FRenderTarget::RT_MATERIAL_USE_BACKBUFFER
			{
				Direct3DTexture8	= RenDev->DefaultRenderTarget->BackBufferTexture;
			}

			// increment ref count since the frame buffer surfaces are treated as 
			// texture resources and their ref counts will decrement during Flush()
			RenderTargetSurface->AddRef();
			DepthStencilSurface->AddRef();
			Direct3DTexture8->AddRef();
		}
		else if( !(RenderTarget->CreationFlags & FRenderTarget::RT_CREATE_COLOR) )
		{
#ifdef _XBOX
			//jg -- These are no allocated via my routine
			bIsUMA = 0;
#endif
            // -- use default color render target
			Direct3DTexture8		= RenDev->DefaultRenderTarget->BackBufferTexture;
			RenderTargetSurface		= RenDev->DefaultRenderTarget->BackBufferSurface;			
			RenderTargetSurface->AddRef();
			Direct3DTexture8->AddRef();
		}
		else if(!Direct3DTexture8 || CachedWidth != USize || CachedHeight != VSize || CachedFirstMip != FirstMip || CachedNumMips != NumMips || CachedFormat != Direct3DFormat)
		{
			//jg -- Use common method for cleaning up
			DeleteTexture();
#ifdef _XBOX
			//jg -- UMA allocation for textures. Header from non-contiguous RAM.
			// Data from contiguous RAM
            Direct3DTexture8 = NEW D3DTexture;
            // Set the header info and get the size needed
			DWORD SizeNeeded = XGSetTextureHeader(USize,VSize,NumMips - FirstMip,
				D3DUSAGE_RENDERTARGET,Direct3DFormat,D3DPOOL_DEFAULT,
				Direct3DTexture8,0,0);
			// Grab a contiguous block memory
			void* pTexMem = appContiguousMalloc(SizeNeeded,D3DTEXTURE_ALIGNMENT);
			// Now set it in the header and strip off the physical bit
			Direct3DTexture8->Register(pTexMem);
#else
			// Try to allocate the render target.
			// Direct3D won't bump managed textures out of memory for it, so if the first allocation fails,
			// it will manually ask D3D to free up video memory and try to allocate the render target again.

			INT	NumTries = 0;

			Result = 0;

			while(NumTries < 2)
			{
				// Create a new Direct3D texture.

				Result = RenDev->Direct3DDevice8->CreateTexture(
					USize,
					VSize,
					NumMips - FirstMip,
					D3DUSAGE_RENDERTARGET,
					Direct3DFormat,
					D3DPOOL_DEFAULT,
					&Direct3DTexture8
					);

				if( !FAILED(Result) )
					break;

#ifndef _XBOX
				if( RenDev->AvoidHitches )
					NumTries++;
				else
				if( FAILED(RenDev->Direct3DDevice8->ResourceManagerDiscardBytes(0/*GetBytesPerPixel(ActualFormat,USize) * VSize*/)) )
					appErrorf(TEXT("ResourceManagerDiscardBytes failed."));
#endif

				NumTries++;
			};

			if( FAILED(Result) )
				Failed = 1;
			else
#endif
			{
				// Retrieve the texture's surface.

				Result = Direct3DTexture8->GetSurfaceLevel(0,&RenderTargetSurface);

				if( FAILED(Result) )
					appErrorf(TEXT("GetSurfaceLevel failed(%s)."),*D3DError(Result));
			}
		}

		if(!Failed && RenderTarget != RenDev->DefaultRenderTarget )
		{
			if( !(RenderTarget->CreationFlags & FRenderTarget::RT_CREATE_DEPTH) )
			{
#ifdef _XBOX
				//scion jg -- Don't set a depth buffer for L8. Causes GHEs.
				if (Direct3DFormat != D3DFMT_LIN_L8)
#endif
				{
					// -- use default depth surface for this render target
					DepthStencilSurface		= RenDev->DefaultRenderTarget->DepthBufferSurface;
					DepthStencilSurface->AddRef();
				}
#ifdef _XBOX
				else
				{
					DepthStencilSurface = NULL;
				}
#endif
			}
			else if(!DepthStencilSurface || CachedWidth != USize || CachedHeight != VSize || CachedFirstMip != FirstMip || CachedNumMips != NumMips)
			{
				// Release the existing texture.
				if(DepthStencilSurface)
					DepthStencilSurface->Release();

				INT	NumTries = 0;

				Result = 0;

				while(NumTries < 2)
				{
					// Create a new Direct3D depth-stencil surface.

					Result = RenDev->Direct3DDevice8->CreateDepthStencilSurface(
						USize,
						VSize,
#ifdef _XBOX
						D3DFMT_LIN_D24S8,
#else
						RenDev->DepthBufferFormat,
#endif
						D3DMULTISAMPLE_NONE,
						&DepthStencilSurface
						);

					if( !FAILED(Result) )
						break;

#ifndef _XBOX
					if( RenDev->AvoidHitches )
						NumTries++;
					else
					if( FAILED(RenDev->Direct3DDevice8->ResourceManagerDiscardBytes(0/*USize * VSize * 4*/)) )
						appErrorf(TEXT("ResourceManagerDiscardBytes failed."));
#endif

					NumTries++;
				};
		
				if( FAILED(Result) )
					Failed = 1;
			}
		}

		if(Failed)
		{
			if(RenderTargetSurface)
				RenderTargetSurface->Release();

			DeleteTexture();
		
			if(DepthStencilSurface)
				DepthStencilSurface->Release();

			Direct3DTexture8		= NULL;
			Direct3DCubeTexture8	= NULL;
			RenderTargetSurface		= NULL;
			DepthStencilSurface		= NULL;
		}
	}
	else if ( Texture )
	{
		// Conversion variables.
		BYTE* Data		= NULL;
		UBOOL ForceRGBA = 0;
		UBOOL WasRGBA	= 0;

		// Convert if necessary.
		if( !RenDev->SupportsTextureFormat(ActualFormat) && (IsDXTC(ActualFormat) || RenDev->Use16bitTextures) )
		{
			if( ActualFormat == TEXF_RGBA8 )
			{
				Direct3DFormat = D3DFMT_A4R4G4B4;
				if( DownSample )
				{
					Data = new BYTE[4*Width*Height];
					Texture->GetTextureData(FirstMip,Data,Width * 4,ActualFormat);
				}
				else
				{
					Data = new BYTE[4*USize*VSize];
					Texture->GetTextureData(FirstMip,Data,USize * 4,ActualFormat);
				}
				WasRGBA = 1;
			}
			else
			{
				
				ActualFormat = Texture->GetUTexture()->ConvertDXT( FirstMip, !RenDev->SupportsTextureFormat(TEXF_DXT1), 0, &Data );
				switch( ActualFormat )
				{
				case TEXF_RGBA8:
					Direct3DFormat	= RenDev->SupportsTextureFormat(TEXF_RGBA8) ? D3DFMT_A8R8G8B8 : D3DFMT_A4R4G4B4;
					ForceRGBA		= 1;
					break;
				case TEXF_DXT1:
					Direct3DFormat	= D3DFMT_DXT1;
					ForceRGBA		= 0;
					break;
				default:
					appErrorf(TEXT("ConvertDXT returned unknown texture format"));
				}
			}
		}

#if defined(_XBOX) && defined(XBOX_ASSET_FORMAT)
	if (Texture != NULL && Texture->GetUTexture() != NULL)
	{
		//jg -- Don't try to find runtime generated textures
		if (Texture->GetUTexture()->bParametric == FALSE)
		{
			extern LPDIRECT3DTEXTURE8 GetTextureFromXPR(const TCHAR* Name);
			//jg -- Load from XPR file if possible
			Direct3DTexture8 = GetTextureFromXPR(Texture->GetUTexture()->GetName());
			if (Direct3DTexture8 == NULL)
			{
				debugf(TEXT("Failed to find %s in XPRs"),
					Texture->GetUTexture()->GetFullName());
				Direct3DTexture8 = GetTextureFromXPR(TEXT("DefaultTexture"));
				check(Direct3DTexture8);
			}
			// If it was found, we're done. Otherwise fallback to the old way
			if (Direct3DTexture8 != NULL)
			{
				bCameFromXPR = 1;
				CachedRevision	= SourceTexture->GetRevision();
				CachedWidth		= USize;
				CachedHeight	= VSize;
				CachedFirstMip	= FirstMip;
				CachedNumMips	= NumMips;
				CachedFormat	= Direct3DFormat;
				return 1;
			}
		}
		else
		{
			// Mark it as from the normal path so it cleans up correctly
			bCameFromXPR = FALSE;
		}
	}
#endif

		if(!Direct3DTexture8 || CachedWidth != USize || CachedHeight != VSize || CachedFirstMip != FirstMip || CachedNumMips != NumMips || CachedFormat != Direct3DFormat)
		{
			//jg -- Use common method for cleaning up
			DeleteTexture();
#ifdef _XBOX
			//jg -- UMA allocation for textures. Header from non-contiguous RAM.
			// Data from contiguous RAM
            Direct3DTexture8 = NEW D3DTexture;
            // Set the header info and get the size needed
			DWORD SizeNeeded = XGSetTextureHeader(USize,VSize,NumMips - FirstMip,0,
				Direct3DFormat,D3DPOOL_DEFAULT,Direct3DTexture8,0,0);
			// Grab a contiguous block memory
			void* pTexMem = appContiguousMalloc(SizeNeeded,D3DTEXTURE_ALIGNMENT);
			// Now set it in the header and strip off the physical bit
			Direct3DTexture8->Register(pTexMem);
#else
			// Create a new Direct3D texture.
			Result = RenDev->Direct3DDevice8->CreateTexture(
				USize,
				VSize,
				NumMips - FirstMip,
				0,
				Direct3DFormat,
				D3DPOOL_MANAGED,
				&Direct3DTexture8
				);
	
			if( FAILED(Result) )
				appErrorf(TEXT("CreateTexture failed(%s)."),*D3DError(Result));
#endif
		}

		// Copy the texture into the Direct3D texture.
		for(INT MipIndex = FirstMip;MipIndex < NumMips;MipIndex++)
		{
			// Lock the Direct3D texture.
			D3DLOCKED_RECT	LockedRect;
			INT				MipWidth  = USize >> (MipIndex - FirstMip),
							MipHeight = VSize >> (MipIndex - FirstMip);
	
			Result = Direct3DTexture8->LockRect(MipIndex - FirstMip,&LockedRect,NULL,0);

			if( FAILED(Result) )
				appErrorf(TEXT("LockRect failed(%s)."),*D3DError(Result));

#ifdef _XBOX
			// Non compressed textures need swizzling.
			if ( ActualFormat == TEXF_RGBA8 )
			{
				if ( MipHeight && MipWidth ) //!!TODO: DEBUG
				{
					BYTE* Data  = new BYTE[MipHeight * LockedRect.Pitch];
					Texture->GetTextureData(MipIndex,(void*) Data,LockedRect.Pitch,ActualFormat,(MipIndex >= RenDev->FirstColoredMip));
					XGSwizzleRect(Data,LockedRect.Pitch,NULL,LockedRect.pBits,MipWidth,MipHeight,NULL,4);
					delete [] Data;
				}
				else
					debugf(NAME_DevD3D,TEXT("!!vogel: debug this"));
			}
			else if( ActualFormat == TEXF_V8U8 || ActualFormat == TEXF_L6V5U5 )
			{
				BYTE* Data  = new BYTE[MipHeight * LockedRect.Pitch];
				Texture->GetTextureData(MipIndex,(void*) Data,LockedRect.Pitch,ActualFormat,(MipIndex >= RenDev->FirstColoredMip));
				XGSwizzleRect(Data,LockedRect.Pitch,NULL,LockedRect.pBits,MipWidth,MipHeight,NULL,2);
				delete [] Data;
			}
			else if( ActualFormat == TEXF_V16U16 )
			{
				BYTE* Data  = new BYTE[MipHeight * LockedRect.Pitch];
				Texture->GetTextureData(MipIndex,(void*) Data,LockedRect.Pitch,ActualFormat,(MipIndex >= RenDev->FirstColoredMip));
				XGSwizzleRect(Data,LockedRect.Pitch,NULL,LockedRect.pBits,MipWidth,MipHeight,NULL,2);
				delete [] Data;
			}
			else
#else
			if( Data )
			{
				INT USize	= MipWidth;
				INT VSize	= MipHeight;
				INT Stride;
				if( IsDXTC(ActualFormat) )
				{
					USize	= Max( USize, 4 );
					VSize	= Max( VSize, 4 ) / 4;
					Stride	= GetBytesPerPixel(ActualFormat, USize) * 4;
				}
				else
					Stride	= GetBytesPerPixel(ActualFormat, USize);	

				if( RenDev->Use16bitTextures )
				{
					Stride /= 2;

					// convert to 4444
					DWORD* Src = (DWORD*) Data;
					_WORD* Dst = (_WORD*) Data;
					if( DownSample )
					{
						for( INT y=0; y<VSize; y++ )
						{
							for( INT x=0; x<USize; x++ )
							{
								*(Dst++) = FColor(*Src).HiColor4444();
								Src += ScaleX;
							}
							Src += Width * (ScaleY - 1);
						}
					}
					else
					{
						for( INT y=0; y<VSize; y++ )
						{
							for( INT x=0; x<USize; x++ )		
							{
								*(Dst++) = FColor(*(Src++)).HiColor4444();
							}
						}
					}
				}

				if( LockedRect.Pitch == Stride )
					appMemcpy( LockedRect.pBits, Data, Stride * VSize );
				else
				{
					BYTE* Dest = (BYTE*) LockedRect.pBits;
					BYTE* Src  = (BYTE*) Data;
					for( INT h=0; h<VSize; h++ )
					{
						appMemcpy( Dest, Src, Stride );
						Dest += LockedRect.Pitch;
						Src  += Stride;
					}
				}
			}
			else
#endif
			if( !RenDev->Use16bitTextures )
				Texture->GetTextureData(MipIndex,(void*) LockedRect.pBits,LockedRect.Pitch,ActualFormat,(MipIndex >= RenDev->FirstColoredMip));
	
			if( Data )
			{
				delete [] Data;
				Data = NULL;
				if( MipIndex < (NumMips-1) )
				{
					if( WasRGBA )
					{
						// Actually too much memory.
						Data = new BYTE[4 * MipWidth * MipHeight];
						Texture->GetTextureData(MipIndex+1,Data,MipWidth*2,ActualFormat);
					}
					else
						Texture->GetUTexture()->ConvertDXT( MipIndex+1, ForceRGBA, 0, &Data );		
				}
			}

			// Unlock the Direct3D texture.
			Direct3DTexture8->UnlockRect(MipIndex - FirstMip);
		}
		delete [] Data;
	}
	// Not supported. Instead of bombing out set the NULL texture.
	else
	{
		Direct3DCubeTexture8	= NULL;
		Direct3DTexture8		= NULL;
	}

	// Update the cached info.
	CachedRevision	= SourceTexture->GetRevision();
	CachedWidth		= USize;
	CachedHeight	= VSize;
	CachedFirstMip	= FirstMip;
	CachedNumMips	= NumMips;
	CachedFormat	= Direct3DFormat;

	return Failed;

	unguard;
}

/* scion ======================================================================
* FDefaultRenderTarget::FDefaultRenderTarget
* Author: sz
* 
* Store pointers to D3D back buffer and depth buffer surfaces.
*
* ============================================================================
*/
FDefaultRenderTarget::FDefaultRenderTarget( UD3DRenderDevice* InRenDev )
{
	guard( FDefaultRenderTarget::FDefaultRenderTarget );

	RenDev = InRenDev;
	BackBufferSurface = FrontBufferSurface = DepthBufferSurface = NULL;
	BackBufferTexture = FrontBufferTexture = DepthBufferTexture = NULL;

	UpdateSurfaces();

	//scion jg -- Make sure these default to not cached
	ResetCacheIDs();

	Revision = 1;

	unguard;
}

/* scion ======================================================================
* FDefaultRenderTarget::ResetCacheIDs()
* Author: jg
* 
* Resets the cache IDs to an invalid state so they will be recached
*
* ============================================================================
*/
void FDefaultRenderTarget::ResetCacheIDs(void)
{
	guard(FDefaultRenderTarget::ResetCacheIDs);
	CacheIdBack = CacheIdFront = CacheIdDepth = INVALID_CACHEID;
	unguard;
}


/* scion ======================================================================
* FDefaultRenderTarget::~FDefaultRenderTarget
* Author: sz
* 
* ============================================================================
*/
FDefaultRenderTarget::~FDefaultRenderTarget() 
{
	guard( FDefaultRenderTarget::~FDefaultRenderTarget );

#ifdef _XBOX
	// make sure to delete our D3DTexture wrapper for reformatting the depth surface
	if( DepthBufferTexture ) {
		delete DepthBufferTexture;		
	}
#endif

	unguard;
}

/* scion ======================================================================
* FDefaultRenderTarget::UpdateSurfaces
* Author: sz
* 
* Refresh back/depth buffer D3D surface pointers.  
* Reset Width/Height.
* ============================================================================
*/
void FDefaultRenderTarget::UpdateSurfaces()
{
	guard(FDefaultRenderTarget::UpdateSurfaces);

	D3DSURFACE_DESC desc;

	// get back buffer surface
	if( FAILED( RenDev->Direct3DDevice8->GetBackBuffer( 0, D3DBACKBUFFER_TYPE_MONO, &BackBufferSurface ) ))
	{
		appErrorf( TEXT("GetBackBuffer failed.") );
	}

	// get front buffer surface
#ifdef _XBOX
	if( FAILED( RenDev->Direct3DDevice8->GetBackBuffer( -1, D3DBACKBUFFER_TYPE_MONO, &FrontBufferSurface ) ))
	{
		appErrorf( TEXT("GetFrontBuffer failed.") );
	}
#else
	//if( FAILED(	RenDev->Direct3DDevice8->GetFrontBuffer( FrontBufferSurface ) ))
#endif	
	
	// get depth buffer surface	
	if( FAILED( RenDev->Direct3DDevice8->GetDepthStencilSurface( &DepthBufferSurface ) ))
	{
		appErrorf( TEXT("GetDepthStencilSurface failed.") );
	}

	// set back buffer texture
	BackBufferTexture = (IDirect3DTexture8*)BackBufferSurface;
	// set front buffer texture
	FrontBufferTexture = (IDirect3DTexture8*)FrontBufferSurface;

#ifdef _XBOX
	// use our own D3DTexture wrapper around the depth buffer surface
	// so that we can set its format to whatever we want
	if( DepthBufferTexture ) {
		delete DepthBufferTexture;		
	}
	DepthBufferTexture = new D3DTexture;
	DepthBufferSurface->GetDesc( &desc );
	XGSetTextureHeader( desc.Width, desc.Height, 1, 0, D3DFMT_LIN_B8G8R8A8, 0, DepthBufferTexture, 
		DepthBufferSurface->Data, desc.Width*XGBytesPerPixelFromFormat(D3DFMT_LIN_B8G8R8A8) );
#else	
	// set depth buffer texture
	DepthBufferTexture = (IDirect3DTexture8*)DepthBufferSurface;
#endif

	BackBufferSurface->GetDesc( &desc );
	Width = desc.Width;
	Height = desc.Height;
	Format =  TEXF_RGBA8;

	// make sure to release surfaces since GetBackBuffer call increases
	// ref count by 1 internally
	if( BackBufferSurface )
		BackBufferSurface->Release();
	if( DepthBufferSurface )
		DepthBufferSurface->Release();
	if( FrontBufferSurface )
		FrontBufferSurface->Release();

	unguard;
}

//
//	FDefaultRenderTarget::GetCacheId
//
_WORD FDefaultRenderTarget::GetCacheId()
{
	// -- each frame buffer surface is wrapped with a FD3DTexture resource and can
	//    be retried from the resource pool by specifying the following material usage flags

	if( MaterialUsageFlag == FRenderTarget::RT_MATERIAL_USE_BACKBUFFER )
	{
		return CacheIdBack;
	}
	else if( MaterialUsageFlag == FRenderTarget::RT_MATERIAL_USE_DEPTHBUFFER )
	{
		return CacheIdDepth;
	}
	else if( MaterialUsageFlag == FRenderTarget::RT_MATERIAL_USE_FRONTBUFFER )
	{
		return CacheIdFront;
	}

	return CacheIdBack;
}

void FDefaultRenderTarget::SetCacheId(_WORD NewCacheID,URenderDevice* pRenDev)
{
	// -- each frame buffer surface is wrapped with a FD3DTexture resource and can
	//    be retried from the resource pool by specifying the following material usage flags

	if( MaterialUsageFlag == FRenderTarget::RT_MATERIAL_USE_BACKBUFFER )
	{
		CacheIdBack = NewCacheID;
	}
	else if( MaterialUsageFlag == FRenderTarget::RT_MATERIAL_USE_DEPTHBUFFER )
	{
		CacheIdDepth = NewCacheID;
	}
	else if( MaterialUsageFlag == FRenderTarget::RT_MATERIAL_USE_FRONTBUFFER )
	{
		CacheIdFront = NewCacheID;
	}
	// Copy the pointer for later clean up
	m_pRenDev = pRenDev;
}

//
//	FD3DVertexStream::FD3DVertexStream
//
FD3DVertexStream::FD3DVertexStream(UD3DRenderDevice* InRenDev,FRenderResource* InRes) :
	FD3DResource(InRenDev,InRes)
{
	Direct3DVertexBuffer8 = NULL;
}

//
//	FD3DVertexStream::~FD3DVertexStream
//
FD3DVertexStream::~FD3DVertexStream()
{
	guard(FD3DVertexStream::~FD3DVertexStream);
	InternalRelease();
	unguard;
}

/* scion ======================================================================
 * FD3DVertexStream::InternalRelease
 * Author: jg
 * 
 * Either releases the internal pointer or deletes it based upon the UMA flag
 *
 * ============================================================================
 */
void FD3DVertexStream::InternalRelease(void)
{
	guard(FD3DVertexStream::InternalRelease)
	// If there is a currently valid buffer clean it up
	if (Direct3DVertexBuffer8)
	{
		// If it is UMA then we need to free the header manually rather than
		// just lowering its ref count
#ifdef _XBOX
		if (bIsUMA == 1)
		{
			// Bug fix from Richard Weeks of LucasArts. Thanks!
			Direct3DVertexBuffer8->BlockUntilNotBusy();
			delete Direct3DVertexBuffer8;
		}
		else
#endif
		{
			Direct3DVertexBuffer8->Release();
		}
		// Zero it so we don't accidentally clean it up twice
		Direct3DVertexBuffer8 = NULL;
	}
	unguard;
}

//
//  FD3DVertexStream::CalculateFootprint
//
INT FD3DVertexStream::CalculateFootprint()
{
	return CachedSize;
}

//
//	FD3DVertexStream::Cache
//
void FD3DVertexStream::Cache(FVertexStream* SourceStream)
{
	guard(FD3DVertexStream::Cache);

	check(SourceStream);
	check(RenDev);

	INT		Size = SourceStream->GetSize();
	check(Size > 0);
	HRESULT	Result;

	DWORD UsageFlags = D3DUSAGE_WRITEONLY;
	D3DPOOL PoolFlags = RESOURCE_POOL;
	DWORD LockFlags = 0;
	if( SourceStream->HintDynamic() )
	{
		UsageFlags = D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY;
		PoolFlags = D3DPOOL_DEFAULT;
#ifndef _XBOX
		LockFlags = D3DLOCK_DISCARD;
#endif
	}

#ifndef _XBOX
	if( SourceStream->UseNPatches() && RenDev->UseNPatches )
		UsageFlags |= D3DUSAGE_NPATCHES;

	if( !RenDev->UseHardwareTL )
		UsageFlags |= D3DUSAGE_SOFTWAREPROCESSING;
#endif

	if(!Direct3DVertexBuffer8 || CachedSize != Size)
	{
		// Release the existing vertex buffer.
		InternalRelease();

#ifdef _XBOX
		//scion jg -- If the stream supports UMA use that instead
		void* pData = 0;
		SourceStream->GetRawStreamData(&pData,0);
		// Make sure that it both supports this and there is a valid pointer
		if (SourceStream->bIsUMA && pData)
		{
			bIsUMA = 1;
			// Create our own self managed vertex buffer
			D3DVertexBuffer* pBuff = new D3DVertexBuffer;
			// Clear the memory out after allocating it
			appMemzero(pBuff,sizeof(D3DVertexBuffer));
			// Tell D3D we are a vertex buffer
			pBuff->Common = 1 | D3DCOMMON_TYPE_VERTEXBUFFER;
			// Set the data directly to that of the source index
			pBuff->Register(pData);
			Direct3DVertexBuffer8 = pBuff;

			// Update cached info.
			CachedRevision = SourceStream->GetRevision();
			CachedSize = Size;
			return;
		}
#endif
		D3DPOOL PoolFlags	= D3DPOOL_DEFAULT;
		INT Result	 = D3D_OK;
		INT NumTries = 0;

		while( NumTries < 3 )
		{
			// Use systemmem pool if allocation in default pool fails.
			if( NumTries == 2 )
				PoolFlags = D3DPOOL_SYSTEMMEM; 

			// Create a new Direct3D vertex buffer.
			Result = RenDev->Direct3DDevice8->CreateVertexBuffer(Size,UsageFlags,0,PoolFlags,&Direct3DVertexBuffer8);
			if( SUCCEEDED(Result) )
				break;
#ifndef _XBOX
			if( RenDev->AvoidHitches )
				NumTries++;
			else
			if( FAILED(RenDev->Direct3DDevice8->ResourceManagerDiscardBytes(0/*Size*/) ) )
				appErrorf(TEXT("ResourceManagerDiscardBytes failed."));
#endif
			NumTries++;
		}
	
		if( FAILED(Result) )
			appErrorf(TEXT("CreateVertexBuffer failed(%s)."),*D3DError(Result));
	}

	// Lock the vertex buffer.
	BYTE*	VertexBufferContents;

	Result = Direct3DVertexBuffer8->Lock(0,Size,&VertexBufferContents,LockFlags);

	if( FAILED(Result) )
		appErrorf(TEXT("IDirect3DVertexBuffer8::Lock failed(%s)."),*D3DError(Result));

	// Copy the stream contents into the vertex buffer.
	SourceStream->GetStreamData(VertexBufferContents);

	// Unlock the vertex buffer.
	Result = Direct3DVertexBuffer8->Unlock();

	if( FAILED(Result) )
		appErrorf(TEXT("IDirect3DVertexBuffer8::Unlock failed(%s)."),*D3DError(Result));

	// Update the cached info.
	CachedRevision = SourceStream->GetRevision();
	CachedSize = Size;

	unguard;
}

//
//	FD3DDynamicVertexStream::FD3DDynamicVertexStream
//
#ifdef _XBOX
//scion jg -- With some instrumentation I found that many smaller
// dynamic VBs performed better than a few large ones.
#define INITIAL_DYNAMIC_VERTEXBUFFER_SIZE	16*1024	// Initial size of dynamic vertex buffers, in bytes.
#else
#define INITIAL_DYNAMIC_VERTEXBUFFER_SIZE	65536		// Initial size of dynamic vertex buffers, in bytes.
#endif
FD3DDynamicVertexStream::FD3DDynamicVertexStream(UD3DRenderDevice* InRenDev) : FD3DVertexStream(InRenDev,NULL)
{
	guard(FD3DDynamicVertexStream::FD3DDynamicVertexStream);
	// SL MODIFY START
	for (INT i = 0; i < NUM_INTERNAL_BUFFERS; i++)
	{
		Direct3DVertexBuffer8 = NULL;
		CachedSize = 0;
		Tail = 0;

		Reallocate(INITIAL_DYNAMIC_VERTEXBUFFER_SIZE);

		Direct3DVertexBuffer8s[i] = Direct3DVertexBuffer8;
		CachedSizes[i] = CachedSize;
		Tails[i] = Tail;
	}
	CurrentBuffer = 0;

	// MSM: In the old code, because CachedSizes isn't set, the buffer is 
	// discarded as soon as AddVertices is called.
	Direct3DVertexBuffer8 = Direct3DVertexBuffer8s[0];
	CachedSize = CachedSizes[0];
	Tail = Tails[0];
	// SL MODIFY END
	unguard;
}

//
//	FD3DDynamicVertexStream::Reallocate
//
void FD3DDynamicVertexStream::Reallocate(INT NewSize)
{
	guard(FD3DDynamicVertexStream::Reallocate);

	debugf(NAME_DevD3D,TEXT("Allocating %u byte dynamic vertex buffer."),NewSize);

#ifdef _XBOX
	// MSM: Round the size up to the nearest 4K, since that is the allocation granularity.
	NewSize = ((NewSize + 4095) / 4096) * 4096;
#endif

	// Release the old vertex buffer.
	if(Direct3DVertexBuffer8)
	{
		// Make sure it's not currently bound via SetStreamSource.
		RenDev->DeferredState.UnSetVertexBuffer( Direct3DVertexBuffer8 );
		Direct3DVertexBuffer8->Release();
		Direct3DVertexBuffer8 = NULL;
	}

	// Create a dynamic vertex buffer to hold the vertices.
	CachedSize = NewSize;
	Tail = 0;

	DWORD UsageFlags = D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY;

#ifndef _XBOX
	if( !RenDev->UseHardwareTL )
		UsageFlags |= D3DUSAGE_SOFTWAREPROCESSING;
#endif

	D3DPOOL PoolFlags	= D3DPOOL_DEFAULT;
	INT Result	 = D3D_OK;
	INT NumTries = 0;

	while( NumTries < 3 )
	{
		// Use systemmem pool if allocation in default pool fails.
		if( NumTries == 2 )
			PoolFlags = D3DPOOL_SYSTEMMEM; 

		// Create a new Direct3D vertex buffer.
		Result = RenDev->Direct3DDevice8->CreateVertexBuffer(CachedSize,UsageFlags,0,PoolFlags,&Direct3DVertexBuffer8);

		if( SUCCEEDED(Result) )
				break;
#ifndef _XBOX
		if( RenDev->AvoidHitches )
			NumTries++;
		else
			if( FAILED(RenDev->Direct3DDevice8->ResourceManagerDiscardBytes(0/*CachedSize*/) ) )
				appErrorf(TEXT("ResourceManagerDiscardBytes failed."));
#endif
		NumTries++;
	}

	if( FAILED(Result) )
		appErrorf(TEXT("CreateVertexBuffer failed(%s)."),*D3DError(Result));

	unguard;
}

//
//	FD3DDynamicVertexStream::AddVertices
//
INT FD3DDynamicVertexStream::AddVertices(FVertexStream* Stream)
{
	guard(FD3DDynamicVertexStream::AddVertices);

	check(Stream);
	check(RenDev);
	check(Direct3DVertexBuffer8);

	INT	Size = Abs(Stream->GetSize()),
		Stride = Stream->GetStride();

#if 0
	//scion jg -- Use this to tune how big & how many VBs to use
	// Using this I found that 4 128K VBs was 12% slower than
	// having 16 16K buffers
	static INT NumRequests = 0;
	static INT TotalSize = 0;
	static INT MaxSize = 0;
	NumRequests++;
	TotalSize += Size;
	MaxSize = Max(MaxSize,Size);
	// Every 100 stream allocations return the average size requested
	if (NumRequests % 100 == 0)
	{
		debugf(TEXT("Average dynamic vertex buffer size request %d"),
			TotalSize / NumRequests);
		debugf(TEXT("Max dynamic vertex buffer size request %d"),
			MaxSize);
	}
#endif

	// MSM: Begin Modifications
	DWORD LockFlags;
	
	bool bFoundGoodBuffer;
#ifdef _XBOX
	INT LoopCount = 0;
#endif
	
	do
	{
		// MSM: Assume we found a good buffer.
		bFoundGoodBuffer = true;
		
		LockFlags = D3DLOCK_NOOVERWRITE;

		Direct3DVertexBuffer8 = Direct3DVertexBuffer8s[CurrentBuffer];
		CachedSize = CachedSizes[CurrentBuffer];
		Tail = Tails[CurrentBuffer];

		// If the dynamic vertex buffer isn't big enough to contain all the vertices, resize it.
		if(Size > CachedSize)
		{
#ifdef _XBOX
			// MSM: If the buffer is busy, we will stall.
			if (Direct3DVertexBuffer8 != NULL && Direct3DVertexBuffer8->IsBusy() && LoopCount <= NUM_INTERNAL_BUFFERS)
			{
				// Not a good buffer, try the next buffer.
				CurrentBuffer = (CurrentBuffer + 1) % NUM_INTERNAL_BUFFERS;
				bFoundGoodBuffer = false;
				LoopCount++;
				continue;
			}
#endif
			
			Reallocate(Size);

			// MSM: Save new buffer.
			Direct3DVertexBuffer8s[CurrentBuffer] = Direct3DVertexBuffer8;
			CachedSizes[CurrentBuffer] = CachedSize;
			Tails[CurrentBuffer] = Tail;

#ifndef _XBOX
			LockFlags = D3DLOCK_DISCARD;
#else
			LockFlags = 0;
#endif

			STAT_INC(GStats.DWORDStats(RenDev->D3DStats.STATS_DynamicVertexBufferDiscards));
		}

		// If the dynamic vertex buffer will overflow with the additional vertices, flush it.
		if( Stream->GetSize()<0 || Tail + Stride + Size > CachedSize || ((DWORD) (Tail + Stride + Size) / Stride) > RenDev->DeviceCaps8.MaxVertexIndex) // sjs - hacked
		{
#ifdef _XBOX
			// MSM: If the buffer is busy, we will stall.
			if (Direct3DVertexBuffer8->IsBusy() && LoopCount <= NUM_INTERNAL_BUFFERS)
			{
				// Not a good buffer, try the next buffer.
				CurrentBuffer = (CurrentBuffer + 1) % NUM_INTERNAL_BUFFERS;
				bFoundGoodBuffer = false;
				LoopCount++;
				continue;
			}
#endif

			Tail = 0;

#ifndef _XBOX
			LockFlags = D3DLOCK_DISCARD;
#else
			LockFlags = 0;
#endif
			
			STAT_INC(GStats.DWORDStats(RenDev->D3DStats.STATS_DynamicVertexBufferDiscards));
		}
	}
	while (!bFoundGoodBuffer);
	// MSM: End Modifications
	
	// Determine the offset in the vertex buffer to allocate the vertices at.
	INT	VertexBufferOffset = ((Tail + Stride - 1) / Stride) * Stride;

	// Lock the dynamic vertex buffer.
	BYTE*	VertexBufferContents = NULL;

	CLOCK(VBLock,GStats.DWORDStats(RenDev->D3DStats.STATS_DynamicVertexBufferLockCycles));
	HRESULT	Result = Direct3DVertexBuffer8->Lock(VertexBufferOffset,Size,&VertexBufferContents,LockFlags);
	STAT_INC(GStats.DWORDStats(RenDev->D3DStats.STATS_DynamicVertexBufferLockCalls));

	if( FAILED(Result) )
		appErrorf(TEXT("IDirect3DVertexBuffer8::Lock failed(%s)."),*D3DError(Result));
	UNCLOCK();

#ifdef _DEBUG
	BYTE*	Buffer		= VertexBufferContents;
	DWORD*	BufferEnd	= NULL;

	// Tag memory if overflow detection is enabled.
	if( RenDev->CheckForOverflow )
	{
		if( Size+4 > RenDev->StaticBuffer.Num() )
		{
			RenDev->StaticBuffer.Empty();
			RenDev->StaticBuffer.Add( 2 * Max(Size,65535) );
		}
		BufferEnd	= (DWORD*) &RenDev->StaticBuffer(Size+1);
		*BufferEnd	= 0x03221977;
		Buffer		= &RenDev->StaticBuffer(0);
	}
#endif

	// Copy the indices into the index buffer.
	Stream->GetStreamData(VertexBufferContents);

#ifdef _DEBUG
	// Verify tag.
	if( RenDev->CheckForOverflow )
	{
		check( *BufferEnd == 0x03221977 );
		appMemcpy( VertexBufferContents, Buffer, Size );
	}
#endif

	// Unlock the dynamic vertex buffer.
	HRESULT Result2 = Direct3DVertexBuffer8->Unlock();

	if( FAILED(Result2) )
		appErrorf(TEXT("IDirect3DVertexBuffer8::Unlock failed(%s)."),*D3DError(Result2));

	// Update the tail pointer.
	Tail = VertexBufferOffset + Size;

	 // SL MODIFY BEGIN
	Tails[CurrentBuffer] = Tail;
	// SL MODIFY END

	return VertexBufferOffset / Stride;

	unguard;
}

//
//	FD3DVertexShader::SetD3DShaderDeclaration
//
void FD3DVertexShader::SetD3DShaderDeclaration( FShaderDeclaration& InDeclaration )
{
	// Build the Direct3D shader declaration.
	for(INT StreamIndex = 0;StreamIndex < Declaration.NumStreams;StreamIndex++)
	{
		D3DDeclaration.AddItem(D3DVSD_STREAM(StreamIndex));

		for(INT ComponentIndex = 0;ComponentIndex < Declaration.Streams[StreamIndex].NumComponents;ComponentIndex++)
		{
			FVertexComponent&	Component = Declaration.Streams[StreamIndex].Components[ComponentIndex];
			DWORD				Type = 0,
								Register = 0;

			if(Component.Type == CT_Float4)
				Type = D3DVSDT_FLOAT4;
			else if(Component.Type == CT_Float3)
				Type = D3DVSDT_FLOAT3;
			else if(Component.Type == CT_Float2)
				Type = D3DVSDT_FLOAT2;
			else if(Component.Type == CT_Float1)
				Type = D3DVSDT_FLOAT1;
			else if(Component.Type == CT_Color)
				Type = D3DVSDT_D3DCOLOR;
#ifndef _XBOX //scion jg -- Not supported on xbox
			else if(Component.Type == CT_RGBA4)
				Type = D3DVSDT_UBYTE4;
#else
			//scion jg -- Packed normals
			else if(Component.Type == CT_NormPacked3)
				Type = D3DVSDT_NORMPACKED3;
			//scion jg -- 16 bit representation of FLOATs
			else if(Component.Type == CT_NormShort1)
				Type = D3DVSDT_NORMSHORT1;
			else if(Component.Type == CT_NormShort2)
				Type = D3DVSDT_NORMSHORT2;
			else if(Component.Type == CT_NormShort3)
				Type = D3DVSDT_NORMSHORT3;
			else if(Component.Type == CT_NormShort4)
				Type = D3DVSDT_NORMSHORT4;
			// scion sz - more 16 bit floats (these are not normalized to [0,1])
			else if(Component.Type == CT_Short1)
				Type = D3DVSDT_SHORT1;
			else if(Component.Type == CT_Short2)
				Type = D3DVSDT_SHORT2;
			else if(Component.Type == CT_Short3)
				Type = D3DVSDT_SHORT3;
			else if(Component.Type == CT_Short4)
				Type = D3DVSDT_SHORT4;
			// scion sz - packed byte formats
			else if(Component.Type == CT_PByte1)
				Type = D3DVSDT_PBYTE1;
			else if(Component.Type == CT_PByte2)
				Type = D3DVSDT_PBYTE2;
			else if(Component.Type == CT_PByte3)
				Type = D3DVSDT_PBYTE3;
			else if(Component.Type == CT_PByte4)
				Type = D3DVSDT_PBYTE4;
#endif

			if(Component.Function == FVF_Position)          // register 0
				Register = D3DVSDE_POSITION;           
			else if(Component.Function == FVF_BlendWeights) // register 1.
				Register = D3DVSDE_BLENDWEIGHT;     
			else if(Component.Function == FVF_Normal)		// register 3    XBox: 2
				Register = D3DVSDE_NORMAL;			
			else if(Component.Function == FVF_Diffuse)		// register 5    XBox: 3
				Register = D3DVSDE_DIFFUSE;
			else if(Component.Function == FVF_Specular)     // register 6    XBox: 4 
				Register = D3DVSDE_SPECULAR;
			else if(Component.Function == FVF_TexCoord0)    // register 7    XBox: 9
				Register = D3DVSDE_TEXCOORD0;
			else if(Component.Function == FVF_TexCoord1)    // register 8    XBox: 10
				Register = D3DVSDE_TEXCOORD1;
			else if(Component.Function == FVF_TexCoord2)    // register 9    XBox: 11
				Register = D3DVSDE_TEXCOORD2;
			else if(Component.Function == FVF_TexCoord3)    // register 10   XBox: 12
				Register = D3DVSDE_TEXCOORD3;

#ifndef _XBOX	// PC only registers.
			else if(Component.Function == FVF_BlendIndices) // register 2
				Register = D3DVSDE_BLENDINDICES;
			else if(Component.Function == FVF_PSize)		// register 4    
				Register = D3DVSDE_PSIZE;
			else if(Component.Function == FVF_TexCoord4)	// register 11
				Register = D3DVSDE_TEXCOORD4;
			else if(Component.Function == FVF_TexCoord5)	// register 12
				Register = D3DVSDE_TEXCOORD5;
			else if(Component.Function == FVF_TexCoord6)	// register 13
				Register = D3DVSDE_TEXCOORD6;
			else if(Component.Function == FVF_TexCoord7)	// register 14
				Register = D3DVSDE_TEXCOORD7;			
#else	// XBOX only registers
			else if(Component.Function == FVF_Fog)
				Register = D3DVSDE_FOG;					// Xbox extension - register 5
			else if(Component.Function == FVF_BackDiffuse)
				Register = D3DVSDE_BACKDIFFUSE;			// Xbox extension - register 7			
			else if(Component.Function == FVF_BackSpecular)
				Register = D3DVSDE_BACKSPECULAR;		// Xbox extension - register 8								
#endif	

			// Simple forcing of numerical register indices for programmable vertex shaders.
			else if( Component.Function >= FVF_GPUReg0 && Component.Function <= FVF_GPUReg15 )
				Register = ( Component.Function - FVF_GPUReg0 );

			D3DDeclaration.AddItem(D3DVSD_REG(Register,Type));
		}
	}
	D3DDeclaration.AddItem(D3DVSD_END());
}

//
//	FD3DVertexShader::FD3DVertexShader
//
FD3DVertexShader::FD3DVertexShader( UD3DRenderDevice* InRenDev, FShaderDeclaration& InDeclaration, QWORD InVertexShaderEnum, ANSICHAR* InSource )
{
	guard(FD3DVertexShader::FD3DVertexShader);

	RenDev = InRenDev;
	Declaration = InDeclaration;
	Source = InSource;
	VertexShaderEnum = InVertexShaderEnum;
	HandleVS = NULL;
	DWORD* Buffer=NULL;

	SetD3DShaderDeclaration(InDeclaration);

#ifdef _XBOX

	static const DWORD EmptyVertexShader[] =
	{
		0x00012078,
		0x00000000, 0x0020001b, 0x0836106c, 0x2070f801
	};	
	
	// scion sz - 
	//
	// Xbox vertex shaders are combined/spliced at run-time. This handle is only used for the D3D declaration.
	// Note the 'NULL' passed as the vertex shader program.
	//
	// HandleVS takes a dummy vertex shader that is just empty.  This is a bit wasteful, but
	// the documented way of creating an empty vertex shader using a NULL program doesn't seem to work.
	// It always defaults to fixed function.
	//
	HRESULT	Result = RenDev->Direct3DDevice8->CreateVertexShader( &D3DDeclaration(0), NULL, &Handle, 0 );	
			Result = RenDev->Direct3DDevice8->CreateVertexShader( &D3DDeclaration(0), EmptyVertexShader, &HandleVS, 0 );	

#else

	LPD3DXBUFFER	Object = NULL;
	LPD3DXBUFFER	Errors = NULL;	
	if( Source )
	{	
		INT SourceSize = appAnsiStrlen(Source);
		HRESULT Result = D3DXAssembleShader( Source, SourceSize, 0, NULL, &Object, &Errors ); //#SKEL - NO HEADER for PC yet.		
		if( FAILED(Result) )
		{
			if( Errors )
				appErrorf(TEXT("DXAssembleShader failed(%s): %s"),*D3DError(Result), appFromAnsi((ANSICHAR*)Errors->GetBufferPointer()) );
			else
				appErrorf(TEXT("DXAssembleShader failed(%s).  Len was %d"),*D3DError(Result), appAnsiStrlen(Source) );
		}
	}
	Buffer = (Object) ? (DWORD*)Object->GetBufferPointer() : NULL;
	HRESULT	Result = RenDev->Direct3DDevice8->CreateVertexShader( &D3DDeclaration(0), Buffer, &Handle, InRenDev->UseHardwareTL ? 0 : D3DUSAGE_SOFTWAREPROCESSING );

#endif

	if( FAILED(Result) )
	{
		appErrorf(TEXT("CreateVertexShader failed"));
	}
	
	// Add the vertex shader to the device vertex shader list.
	NextVertexShader = RenDev->VertexShaders;
	RenDev->VertexShaders = this;	

	unguard;
}

//
//	FD3DVertexShader::~FD3DVertexShader
//
FD3DVertexShader::~FD3DVertexShader()
{
	guard(FD3DVertexShader::~FD3DVertexShader);	

	// Remove the vertex shader from the device vertex shader list.
	FD3DVertexShader*	ShaderPtr;

	if(RenDev->VertexShaders != this)
	{
		ShaderPtr = RenDev->VertexShaders;

		while(ShaderPtr && ShaderPtr->NextVertexShader != this)
			ShaderPtr = ShaderPtr->NextVertexShader;

		if(ShaderPtr)
			ShaderPtr->NextVertexShader = NextVertexShader;

		NextVertexShader = NULL;
	}
	else
	{
		RenDev->VertexShaders = NextVertexShader;
		NextVertexShader = NULL;
	}

	// Delete the vertex shader.
	RenDev->DeferredState.DeleteVertexShader(Handle);
#ifdef _XBOX
	RenDev->DeferredState.DeleteVertexShader(HandleVS);
#endif

	unguard;
}

// scion sz - full path to vertex shaders
#ifdef _XBOX
#define VS_FILE_FULLPATH(Name) TEXT("D:\\ShaderData\\VertexShaders\\##Name##.xvu")
#else
#define VS_FILE_FULLPATH(Name) TEXT("..\\ShaderData\\VertexShaders\\##Name##.xvu")
#endif

// scion sz - fill in vertex shader microcode data and size directly
#define VS_MICROCODE_DEF_STRUCT(ShaderEnum,FileName) { \
	ShaderMicrocodeArray(ShaderEnum) = dw##FileName##VertexShader; \
	ShaderMicrocodeSizes(ShaderEnum) = sizeof(dw##FileName##VertexShader); \
}
// scion sz - fill in vertex shader filename only
#define VS_MICROCODE_DEF_FILE(ShaderEnum,FileName) { \
	FileNames(ShaderEnum) = FString( VS_FILE_FULLPATH(FileName) ); \
}
#ifdef LOAD_VERTEX_SHADERS_HEADERS
#define VS_MICROCODE_DEF(ShaderEnum,FileName) VS_MICROCODE_DEF_STRUCT(ShaderEnum,FileName)
#else
#define VS_MICROCODE_DEF(ShaderEnum,FileName) VS_MICROCODE_DEF_FILE(ShaderEnum,FileName)
#endif

//	scion sz -
//	FD3DVertexShaderFragments::FD3DVertexShaderFragments
//
FD3DVertexShaderFragments::FD3DVertexShaderFragments( UD3DRenderDevice* InRenDev ) :
RenDev(InRenDev)
{
	guard(FD3DVertexShaderFragments::FD3DVertexShaderFragments);

	FileNames.AddZeroed(VS_MAX_Code);
	ShaderMicrocodeArray.AddZeroed(VS_MAX_Code);
	ShaderMicrocodeSizes.AddZeroed(VS_MAX_Code);

	// scion sz - set vertex shader filenames or direct pointers to microcode data

	VS_MICROCODE_DEF( VS_Screenspace_Code, Screenspace );
	
	VS_MICROCODE_DEF( VS_NotBoned_Code, NotBoned );

//	VS_MICROCODE_DEF( VS_SkelOneBoned_Code, SkelOneBoned );
//	VS_MICROCODE_DEF( VS_SkelTwoBoned_Code, SkelTwoBoned );
	VS_MICROCODE_DEF( VS_SkelThreeBoned_Code, SkelThreeBoned );
//	VS_MICROCODE_DEF( VS_SkelFourBoned_Code, SkelFourBoned );
	
	VS_MICROCODE_DEF( VS_InitRGB_Code, InitRGB );	
	VS_MICROCODE_DEF( VS_Light_Ambient_Code, LightAmbient );
#ifdef HARDCODED_VS_LIGHTS
	VS_MICROCODE_DEF( VS_Light_HardDirectional5_Code, LightHardDirectional5 );
#else
	VS_MICROCODE_DEF( VS_InitLighting_Code, InitLighting );
	VS_MICROCODE_DEF( VS_Light_Point_Code, LightPoint );
	VS_MICROCODE_DEF( VS_Light_Directional_Code, LightDirectional );
	VS_MICROCODE_DEF( VS_Light_Spot_Code, LightSpot );
	VS_MICROCODE_DEF( VS_Next_Light_Code, NextLight );	
#endif	

	VS_MICROCODE_DEF( VS_Output_LightRGB_Code, OutLightRGB );
	VS_MICROCODE_DEF( VS_Output_VertexRGB_Code, OutVertexRGB );

	VS_MICROCODE_DEF( VS_Add_VertexRGB_Code, AddVertexRGB );
	VS_MICROCODE_DEF( VS_Add_VertexSpecular_Code, AddVertexSpec );
	VS_MICROCODE_DEF( VS_Mul_VertexSpecular_Code, MulVertexSpec );
	
	VS_MICROCODE_DEF( VS_Texcoord0_Pass_Code, TexPass0 );
	VS_MICROCODE_DEF( VS_Texcoord1_Pass_Code, TexPass1 );
	VS_MICROCODE_DEF( VS_Texcoord2_Pass_Code, TexPass2 );
	VS_MICROCODE_DEF( VS_Texcoord3_Pass_Code, TexPass3 );

	VS_MICROCODE_DEF( VS_Texcoord0_Transform_Code, TexTransform0 );
	VS_MICROCODE_DEF( VS_Texcoord1_Transform_Code, TexTransform1 );
	VS_MICROCODE_DEF( VS_Texcoord2_Transform_Code, TexTransform2 );
	VS_MICROCODE_DEF( VS_Texcoord3_Transform_Code, TexTransform3 );

	VS_MICROCODE_DEF( VS_Texcoord_Source0_Code, TexSource0 );
	VS_MICROCODE_DEF( VS_Texcoord_Source1_Code, TexSource1 );
	VS_MICROCODE_DEF( VS_Texcoord_Source2_Code, TexSource2 );
	VS_MICROCODE_DEF( VS_Texcoord_Source3_Code, TexSource3 );
	VS_MICROCODE_DEF( VS_Texcoord_Source_CAMERASPACEPOSITION_Code, TexSourceCamPos );
	VS_MICROCODE_DEF( VS_Texcoord_Source_CAMERASPACEREFLECTIONVECTOR_Code, TexSourceCamRef );
	VS_MICROCODE_DEF( VS_Texcoord_Source_CAMERASPACENORMAL_Code, TexSourceCamNorm );	
	VS_MICROCODE_DEF( VS_Texcoord_Source_TerrainSpecial_Code, TexSourceTerrainSpec );	
	VS_MICROCODE_DEF( VS_Texcoord_CAMERASPACEPOSITION_Code, TexCamPosition );
	VS_MICROCODE_DEF( VS_Texcoord_CAMERASPACEREFLECTIONVECTOR_Code, TexCamReflect );
	VS_MICROCODE_DEF( VS_Texcoord_CAMERASPACENORMAL_Code, TexCamNormal );	

	VS_MICROCODE_DEF( VS_Texcoord0_Source_Pass_Code, TexSourcePass0 );
	VS_MICROCODE_DEF( VS_Texcoord1_Source_Pass_Code, TexSourcePass1 );
	VS_MICROCODE_DEF( VS_Texcoord2_Source_Pass_Code, TexSourcePass2 );
	VS_MICROCODE_DEF( VS_Texcoord3_Source_Pass_Code, TexSourcePass3 );

	VS_MICROCODE_DEF( VS_Texcoord0_Source_Transform_m4x2_Code, TexSourceTransform0_m4x2 );
	VS_MICROCODE_DEF( VS_Texcoord1_Source_Transform_m4x2_Code, TexSourceTransform1_m4x2 );
	VS_MICROCODE_DEF( VS_Texcoord2_Source_Transform_m4x2_Code, TexSourceTransform2_m4x2 );
	VS_MICROCODE_DEF( VS_Texcoord3_Source_Transform_m4x2_Code, TexSourceTransform3_m4x2 );

	VS_MICROCODE_DEF( VS_Texcoord0_Source_Transform_m4x3z_Code, TexSourceTransform0_m4x3z );
	VS_MICROCODE_DEF( VS_Texcoord1_Source_Transform_m4x3z_Code, TexSourceTransform1_m4x3z );
	VS_MICROCODE_DEF( VS_Texcoord2_Source_Transform_m4x3z_Code, TexSourceTransform2_m4x3z );
	VS_MICROCODE_DEF( VS_Texcoord3_Source_Transform_m4x3z_Code, TexSourceTransform3_m4x3z );

	VS_MICROCODE_DEF( VS_Texcoord0_Source_Transform_m4x3w_Code, TexSourceTransform0_m4x3w );
	VS_MICROCODE_DEF( VS_Texcoord1_Source_Transform_m4x3w_Code, TexSourceTransform1_m4x3w );
	VS_MICROCODE_DEF( VS_Texcoord2_Source_Transform_m4x3w_Code, TexSourceTransform2_m4x3w );
	VS_MICROCODE_DEF( VS_Texcoord3_Source_Transform_m4x3w_Code, TexSourceTransform3_m4x3w );

	VS_MICROCODE_DEF( VS_Texcoord3_Source_Texkill_Code, TexSourceTexKill3 );	

	VS_MICROCODE_DEF( VS_Distortion_Code, Distortion );
	VS_MICROCODE_DEF( VS_DistortionAccum_Code, DistortionAccum );
	VS_MICROCODE_DEF( VS_TerrainSpecial_Code, TerrainSpecial );

	VS_MICROCODE_DEF( VS_Projector_Atten_Code, ProjectorAtten );

	VS_MICROCODE_DEF( VS_UnpackPosition_Code, UnpackPosition );
	VS_MICROCODE_DEF( VS_DefaultPosition_Code, DefaultPosition );

	VS_MICROCODE_DEF( VS_Fog_Code, Fog );	
	VS_MICROCODE_DEF( VS_ColorRemap_Code, ColorRemap );

	unguard;
}

//	scion sz -
//	FD3DVertexShaderFragments::~FD3DVertexShaderFragments
//
FD3DVertexShaderFragments::~FD3DVertexShaderFragments()
{
	guard(FD3DVertexShaderFragments::~FD3DVertexShaderFragments);

// scion sz - no need to remove these when microcode 
// loaded from autogenerated headers
#ifndef LOAD_VERTEX_SHADERS_HEADERS
	for( INT i=0; i<ShaderMicrocodeArray.Num(); i++ )
	{
		if( ShaderMicrocodeArray(i) )
		{
			delete ShaderMicrocodeArray(i);
			ShaderMicrocodeArray(i)=NULL;
		}
	}
#endif

	unguard;
}

//	scion sz -
//	FD3DVertexShaderFragments::LoadVertexShaderFragment
//
DWORD* FD3DVertexShaderFragments::LoadVertexShaderFragment( EVertexShaderCodeFragment FragmentID, DWORD* OutFragmentSize )
{
	guard(FD3DVertexShaderFragments::LoadVertexShaderFragment);
//jg -- We need to execute this code for preprocessing on the PC
#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)

	DWORD* pShaderMicrocodeData = ShaderMicrocodeArray(FragmentID);
	if( pShaderMicrocodeData )
	{
		if( OutFragmentSize )
		{
			*OutFragmentSize = ShaderMicrocodeSizes(FragmentID);
		}
		return pShaderMicrocodeData;
	}

#ifdef LOAD_VERTEX_SHADERS_HEADERS
	// scion sz - if load from autogenerated headers then we shouldn't get here
	// since ShaderMicrocodeArray entries should already be set
	appErrorf( TEXT("Missing vertex shader struct for FragmentID=%d"), (DWORD)FragmentID );
#endif

#ifdef _XBOX
	//jg -- Make sure to handle demo paths
	const FString& ResolvedName = GFileManager->ResolvePath(*FileNames(FragmentID));
	CHAR* FileName = TCHAR_TO_ANSI( *ResolvedName );
#else
	const TCHAR* FileName = *FileNames(FragmentID);
#endif
	
	// Open the vertex shader file
	DWORD dwNumBytesRead;
	HANDLE hFile = CreateFile( FileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
	if( hFile == INVALID_HANDLE_VALUE )
	{
		appErrorf( TEXT("Couldn't open vertex shader file : %s"), FileNames(FragmentID) );
		return NULL;
	}

	// Allocate memory to read the vertex shader file
	DWORD dwSize = GetFileSize(hFile, NULL);
	pShaderMicrocodeData = (DWORD*) new BYTE[dwSize];
	appMemzero( pShaderMicrocodeData, dwSize );
	ShaderMicrocodeArray(FragmentID) = pShaderMicrocodeData;
	ShaderMicrocodeSizes(FragmentID) = dwSize;

	// Read the pre-compiled vertex shader microcode
	ReadFile( hFile, pShaderMicrocodeData, dwSize, &dwNumBytesRead, 0 );

	CloseHandle( hFile );

	if( OutFragmentSize )
	{
		*OutFragmentSize = ShaderMicrocodeSizes(FragmentID);
	}

	return pShaderMicrocodeData;

#endif //_XBOX

	return NULL;

	unguard;    
}

DWORD* FD3DCombinedVertexShader::ShaderTokenArray[FD3DCombinedVertexShader::MAX_SHADER_FRAGMENTS];

//	scion sz -
//	FD3DVertexShaderFragments::GetVertexShaderFragmentHandle
//
DWORD* FD3DVertexShaderFragments::GetVertexShaderFragmentTokenData( EVertexShaderCodeFragment FragmentID )
{
	guard(FD3DVertexShaderFragments::GetVertexShaderFragmentHandle);
#ifdef _XBOX

	DWORD* pShaderMicrocodeData = ShaderMicrocodeArray(FragmentID);
	if( !pShaderMicrocodeData )
	{
		pShaderMicrocodeData = LoadVertexShaderFragment(FragmentID);
	}

	check(pShaderMicrocodeData);
	return (DWORD*)((LPXGBUFFER)pShaderMicrocodeData)->pData;

#endif //_XBOX
	return NULL;
	unguard;
}

//	scion sz -
//	FD3DCombinedVertexShader::AddFragmentToArray
//
DWORD FD3DCombinedVertexShader::AddFragmentToArray( DWORD Index, EVertexShaderCodeFragment FragmentType )
{
	check( Index < FD3DCombinedVertexShader::MAX_SHADER_FRAGMENTS );
	DWORD FragmentSize=0;
	ShaderTokenArray[Index] = RenDev->ShaderCodeFragments.LoadVertexShaderFragment( FragmentType, &FragmentSize );
#if defined(_DEBUG)
	ShaderFragmentEnums[Index] = FragmentType;
#endif
	return FragmentSize;
}

//	scion sz -
//	FD3DCombinedVertexShader::FD3DCombinedVertexShader
//
//  Creates a combination of vertex shader microcode fragments and splices them together.
//  The combination corresponds to the vertex shader enumeration given
FD3DCombinedVertexShader::FD3DCombinedVertexShader( UD3DRenderDevice* InRenDev, QWORD InVertexShaderEnum )
{
	guard(FD3DCombinedVertexShader::FD3DCombinedVertexShader);

	RenDev = InRenDev;
	VertexShaderEnum = InVertexShaderEnum;
	CombinedMicrocode = NULL;
	Handle = NULL;

//jg -- We need to execute this code for preprocessing on the PC
#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
	debugf(NAME_DevD3D,TEXT("Creating new shader configuration %s"), appI64toa((SQWORD)InVertexShaderEnum) );
#if defined(_DEBUG)
	appMemzero( ShaderFragmentEnums, FD3DCombinedVertexShader::MAX_SHADER_FRAGMENTS );
#endif

	INT NumShadersToSplice = 0;
	DWORD NewShaderSize = 0;

	UBOOL SkipScreenSpace=0;
	UBOOL SkipLighting=0;
	UBOOL SkipOutputColor=0;
	UBOOL SkipTexStage[4]={0,0,0,0};

	//jg -- Must be spliced in first 
	// Splice in either the raw position copy or the unpacked one
	if (VertexShaderEnum & VS_UnpackPosition)
	{		
		NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_UnpackPosition_Code );
	}
	// scion sz - all other shaders (except for terrain) should use default position fragment
	else if( !(VertexShaderEnum & VS_TerrainSpecial) )
	{
		NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_DefaultPosition_Code );
	}	

	// Vertex Transformation code
	if( VertexShaderEnum & VS_TerrainSpecial )
	{
		NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_TerrainSpecial_Code );
	}
	else if( !(VertexShaderEnum & VS_SkeletalSkin) )
	{
		NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_NotBoned_Code );
	}
	/*
	// scion sz - only 3 bone vert influencing supported
	else if( VertexShaderEnum & VS_SkelOneBoned )
	{
		NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_SkelOneBoned_Code );
	}
	else if( VertexShaderEnum & VS_SkelTwoBoned )
	{
		NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_SkelTwoBoned_Code );
	}
	else if( VertexShaderEnum & VS_SkelFourBoned )
	{
		NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_SkelFourBoned_Code );
	}
	*/
	else if( VertexShaderEnum & VS_SkelThreeBoned )
	{
		NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_SkelThreeBoned_Code );
	}	

	// handle unique vertex shader fragments	
	if( VS_GETUNIQUESHADER(VertexShaderEnum) == VS_Distortion )
	{
		//NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Distortion_Code );
		// stage 1 and 2 used for 3x2 matrix multiply in pixel shader when doing distortion, so skip
		SkipTexStage[1]=1;
		SkipTexStage[2]=1;
		// skip lighting
		SkipLighting=1;	
		// and screen space calcs
		SkipScreenSpace=1;
	}
	// handle unique vertex shader fragments	
	else if( VS_GETUNIQUESHADER(VertexShaderEnum) == VS_DistortionAccum )
	{
		//NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_DistortionAccum_Code );
		// stage 2 used for texcoord that represents distoriton amount
		SkipTexStage[2]=1;
		// skip lighting
		SkipLighting=1;		
	}
	//scion jg -- Handles distance based attenuation for projectors
	else if (VS_GETUNIQUESHADER(VertexShaderEnum) == VS_Projector_Atten)
	{
		// skip lighting
		SkipLighting = 1;
		// skip output color
		SkipOutputColor = 1;
		NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Projector_Atten_Code );
	}
	else if( VS_GETUNIQUESHADER(VertexShaderEnum) == VS_Projector_Atten_Clip )
	{
		// skip lighting
		SkipLighting = 1;
		// skip output color
		SkipOutputColor = 1;
		// skip stage 3 since it will be used for the texkill tex coords
		SkipTexStage[3]=1;
		NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Projector_Atten_Code );
		NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Texcoord3_Source_Texkill_Code );
	}
	//jg -- Look for color remapping shader
	else if( VS_GETUNIQUESHADER(VertexShaderEnum) == VS_ColorRemap )
	{
		NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_ColorRemap_Code );
		// stage 1 and 2 used for 3x2 matrix multiply in pixel shader when doing distortion, so skip
		SkipTexStage[1]=1;
		SkipTexStage[2]=1;
		// skip lighting
		SkipLighting=1;
		// skip output color
		SkipOutputColor = 1;
	}

	// Initialize final color value 
	NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_InitRGB_Code );
	// Ambient Light code - always loaded
	NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Light_Ambient_Code );
	
	if( !SkipLighting &&
		VertexShaderEnum & VS_Lit )
	{
#ifdef HARDCODED_VS_LIGHTS
		// Lights
		// hardcoded to 5 directional lights
		NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Light_HardDirectional5_Code );
#else
		// Lights
		// The lights should be sorted s.t. Directional < Point < Spot
		// This helps reduce the number of combinations
		UBOOL LightsInitialized = 0;
		INT NumLightsUsed = 0;
		for( INT i=0; i < VS_Lights_MAX; i++ )
		{
			if( !LightsInitialized && VS_GETLIGHTTYPE(i,VertexShaderEnum) )
			{
				// Initialize Lighting code
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_InitLighting_Code );
				LightsInitialized = 1;
			}

			if( VS_GETLIGHTTYPE(i,VertexShaderEnum) == VS_Light_Type_Point )
			{
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Light_Point_Code );
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Next_Light_Code );
				NumLightsUsed++;
			}
			else if( VS_GETLIGHTTYPE(i,VertexShaderEnum) == VS_Light_Type_Directional )
			{
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Light_Directional_Code );
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Next_Light_Code );
				NumLightsUsed++;
			}
			else if( VS_GETLIGHTTYPE(i,VertexShaderEnum) == VS_Light_Type_Spot )
			{
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Light_Spot_Code );
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Next_Light_Code );
				NumLightsUsed++;
			}
		}

		// last piece of shader code to load the next light is not needed
		//
		if( NumLightsUsed )
			NumShadersToSplice--;
#endif
	}

	if( !SkipOutputColor )
	{
		// if we are trying to do vertex coloring then we must
		// respect the current d3d material settings:
		// D3DRS_EMISSIVEMATERIALSOURCE = D3DMCS_COLOR1 (DIFFUSE)
		// D3DRS_SPECULARMATERIALSOURCE = D3DMCS_MATERIAL (1,1,1,1)
		// D3DRS_DIFFUSEMATERIALSOURCE = D3DMCS_COLOR2 (SPECULAR)
		// D3DRS_AMBIENTMATERIALSOURCE = D3DMCS_COLOR2 (SPECULAR)
		//
		// These are combined as follows (see D3D Reflectance Model for more info):
		// D.v = I.a*V.a + V.e + Sum( R.d*V.d*L.d + V.a*L.a )
		//
		// For our purposes, using the remappings of material source properties, we have:
		// oD0 = GlobalAmbient + VertexDiffuse + Sum(Light Ambient + Diffuse Lighting) * VertexSpecular
		//
		if( VertexShaderEnum & VS_VertexColored )
		{
			if( VertexShaderEnum & VS_StaticMeshVertexColor )
			{
				// multiply current lighting output by SPECULAR vertex color
				// Output = (GlobalAmbient + Diffuse Lighting) * VertexSpecular
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Mul_VertexSpecular_Code );

				// add DIFFUSE vertex color only if the stream was also added
				if( VertexShaderEnum & VS_StaticMeshVertexColorDiffuse )
				{
					// add DIFFUSE vertex color
					// Output = VertexSpecular + VertexDiffuse + (GlobalAmbient + Diffuse Lighting) * VertexSpecular
					NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Add_VertexRGB_Code );
				}
			}
			else
			{
				// add DIFFUSE vertex color when using vertex coloring
				// Output = VertexSpecular + VertexDiffuse + (GlobalAmbient + Diffuse Lighting) * VertexSpecular
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Add_VertexRGB_Code );
			}
		}		

		// Output Final Color 
		NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Output_LightRGB_Code );
	}

	UBOOL CameraSpacePositionInit = false;
	UBOOL CameraSpaceNormalInit = false;
	UBOOL CameraSpaceReflectInit = false;	

	// since the distortion amount is modulated by vertex alpha color 
	// then we have to add the fragments here AFTER vertex color calcs
	if( VS_GETUNIQUESHADER(VertexShaderEnum) == VS_Distortion )
	{
		// actual distortion vs fragment
		NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Distortion_Code );
	}
	else if( VS_GETUNIQUESHADER(VertexShaderEnum) == VS_DistortionAccum )
	{
		if (CameraSpacePositionInit == 0)
		{
			// for distance fading
			NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Texcoord_CAMERASPACEPOSITION_Code );
			CameraSpacePositionInit = true;
		}
		// accumulation distortion vs fragment
		NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_DistortionAccum_Code );		
	}			
	
	// Screen Space code
	if( !SkipScreenSpace )
	{
		NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Screenspace_Code );
	}

	// Texture coordinates
	//
	// First select the source input registers
	// Then apply it to the output texture register oT#
	for( DWORD StageIndex=0; StageIndex < RenDev->DeviceCaps8.MaxTextureBlendStages; StageIndex++ )
	{
		// skip flagged texture stages (some special case unique shaders require this)
		if( SkipTexStage[StageIndex] )
			continue;

		DWORD TexSource = VS_GETTEXSOURCE(StageIndex,VertexShaderEnum);

		if( TexSource == VS_Tex_Source_None )
			continue;

		if( TexSource < RenDev->DeviceCaps8.MaxTextureBlendStages )
		{
			// special case for terrain since we always generate coordinates based
			// on x/y indices
			if( VertexShaderEnum & VS_TerrainSpecial )
			{
				// fragment to generate terrain uv cooridantes and place them in the R_TEX_SOURCE register
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Texcoord_Source_TerrainSpecial_Code );
			}
			// default mapping for this texture stage
			// ie. TEXCOORD# corresponds to tex stage and is used as source
			else if( StageIndex == TexSource )
			{
				// noop
			}
			// TSS_TEXCOORDINDEX mapping
			// each stage's texture coordinates can come from any source (TEXCOORD#)
			else
			{
				// load the source into a temporary register
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, (EVertexShaderCodeFragment)(VS_Texcoord_Source0_Code + TexSource) );
			}
		}			
		else if( TexSource == VS_Tex_Source_CAMERASPACEPOSITION )
		{
			if( !CameraSpacePositionInit )
			{
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Texcoord_CAMERASPACEPOSITION_Code );
				CameraSpacePositionInit = true;
			}

			// just copies result to temp register
			NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Texcoord_Source_CAMERASPACEPOSITION_Code );
		}
		else if( TexSource == VS_Tex_Source_CAMERASPACENORMAL )
		{
			// transforms local space normal to camera space normal
			//
			if( !CameraSpaceNormalInit )
			{
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Texcoord_CAMERASPACENORMAL_Code );
				CameraSpaceNormalInit = true;
			}

			// just copies result to temp register
			NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Texcoord_Source_CAMERASPACENORMAL_Code );
		}
		else if( TexSource == VS_Tex_Source_CAMERASPACEREFLECTIONVECTOR )
		{
			// in order to compute the camera space reflection of the eye vector
			// we need the VS_Texcoord_CAMERASPACEPOSITION_Code and VS_Texcoord_CAMERASPACENORMAL_Code to also be loaded
			//
			if( !CameraSpacePositionInit )
			{
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Texcoord_CAMERASPACEPOSITION_Code );
				CameraSpacePositionInit = true;
			}
			if( !CameraSpaceNormalInit )
			{
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Texcoord_CAMERASPACENORMAL_Code );
				CameraSpaceNormalInit = true;
			}
			if( !CameraSpaceReflectInit )
			{
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Texcoord_CAMERASPACEREFLECTIONVECTOR_Code );
				CameraSpaceReflectInit = true;
			}

			// just copies result to temp register
			NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Texcoord_Source_CAMERASPACEREFLECTIONVECTOR_Code );
		}

		// now that we have the source mapped or texgened
		// we need to apply the texture transform matrix
		if( StageIndex == TexSource && 
			!(VertexShaderEnum & VS_TerrainSpecial) )	// terrain always generates its coords
		{
			if( VS_GETTEXTRANSFORM(StageIndex,VertexShaderEnum) == VS_Tex_Transform_None )
			{
				// just use pass thru fragment - don't apply tex transform
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, (EVertexShaderCodeFragment)(VS_Texcoord0_Pass_Code + StageIndex) );                        
			}
			else if( VS_GETTEXTRANSFORM(StageIndex,VertexShaderEnum) == VS_Tex_Transform_m4x2 )
			{
				// use 4x2 transform for this specific texture stage (saves an extra temp registar copy)
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, (EVertexShaderCodeFragment)(VS_Texcoord0_Transform_Code + StageIndex) );
			}
			else if( VS_GETTEXTRANSFORM(StageIndex,VertexShaderEnum) == VS_Tex_Transform_m4x3 )
			{
				// copy the tex coord to a temporary register and transform it by 4x3 (z)
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, (EVertexShaderCodeFragment)(VS_Texcoord_Source0_Code + TexSource) );
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, (EVertexShaderCodeFragment)(VS_Texcoord0_Source_Transform_m4x3z_Code + StageIndex) );
			}
			else if( VS_GETTEXTRANSFORM(StageIndex,VertexShaderEnum) == VS_Tex_Transform_m4x3_Projected )
			{
				// copy the tex coord to a temporary register and transform it by 4x3 (w)
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, (EVertexShaderCodeFragment)(VS_Texcoord_Source0_Code + TexSource) );
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, (EVertexShaderCodeFragment)(VS_Texcoord0_Source_Transform_m4x3w_Code + StageIndex) );
			}
		}
		else
		{
			if( VS_GETTEXTRANSFORM(StageIndex,VertexShaderEnum) == VS_Tex_Transform_None )
			{
				// just use pass thru fragment - don't apply tex transform
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, (EVertexShaderCodeFragment)(VS_Texcoord0_Source_Pass_Code + StageIndex) );
			}
			else if( VS_GETTEXTRANSFORM(StageIndex,VertexShaderEnum) == VS_Tex_Transform_m4x2 )
			{
				// apply 4x2 transform to tex coordinate source register
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, (EVertexShaderCodeFragment)(VS_Texcoord0_Source_Transform_m4x2_Code + StageIndex) );
			}
			else if( VS_GETTEXTRANSFORM(StageIndex,VertexShaderEnum) == VS_Tex_Transform_m4x3 )
			{
				// apply 4x3 transform to tex coordinate source register
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, (EVertexShaderCodeFragment)(VS_Texcoord0_Source_Transform_m4x3z_Code + StageIndex) );
			}
			else if( VS_GETTEXTRANSFORM(StageIndex,VertexShaderEnum) == VS_Tex_Transform_m4x3_Projected )
			{
				// apply 4x3 (with w-coordinate write) transform to tex coordinate source register
				NewShaderSize += AddFragmentToArray( NumShadersToSplice++, (EVertexShaderCodeFragment)(VS_Texcoord0_Source_Transform_m4x3w_Code + StageIndex) );
			}
		}

	} //for

	// Handle fog if needed
	if( VertexShaderEnum & VS_Fog )
	{
		// Check to see if the cameraspace chunk is already in
		if (CameraSpacePositionInit == 0)
		{
			NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Texcoord_CAMERASPACEPOSITION_Code );
		}
		// Now splice in the fog code
		NewShaderSize += AddFragmentToArray( NumShadersToSplice++, VS_Fog_Code );
	}

	if( NumShadersToSplice )
	{	
		CombinedMicrocodeSize = 0;
		XGSpliceVertexShaders( NULL, &CombinedMicrocodeSize, NULL, ShaderTokenArray, NumShadersToSplice, false );		
		CombinedMicrocode = new DWORD[CombinedMicrocodeSize/4];		

		CombinedMicrocodeInstructions=0;
#ifdef _DEBUG		
		if( FAILED(XGSpliceVertexShaders( CombinedMicrocode, &CombinedMicrocodeSize, &CombinedMicrocodeInstructions, ShaderTokenArray, NumShadersToSplice, false )) )
#else
		if( FAILED(XGSpliceVertexShaders( CombinedMicrocode, &CombinedMicrocodeSize, &CombinedMicrocodeInstructions, ShaderTokenArray, NumShadersToSplice, true )) )
#endif
		{
			appErrorf( TEXT("XGSpliceVertexShaders failed.") );
		}

		checkSlow(CombinedMicrocodeInstructions <= 136 &&
			"Too many instructions in Vertex Shader!");
	}	

	// handle is not required for a vertex shader since we can use LoadVertexShaderProgram() instaed of LoadVertexShader()
	// and the shader program is decoupled from the stream declaration
	// this also means that we only need one copy of the shader program instead of letting D3D duplicate it
	/*
	static DWORD TempDecl[] =
	{
		D3DVSD_STREAM(0),
		D3DVSD_REG(0,D3DVSDT_FLOAT3),
		D3DVSD_END()
	};
	HRESULT	Result = RenDev->Direct3DDevice8->CreateVertexShader( TempDecl, CombinedMicrocode, &Handle, 0 );
	*/

#endif //_XBOX

	unguardf(( TEXT("VertexShaderEnum=%s"),appI64toa((SQWORD)InVertexShaderEnum) ));
}

/* ============================================================================
 * FD3DCombinedVertexShader::FD3DCombinedVertexShader
 * Author: jg
 * 
 * Creates a new vertex shader from data that was compiled on the PC
 *
 * inputs:	InRenDev -- The render device
 *			InVSEnum -- The vertex shader enum
 *			pByteCode -- The byte code of the shader
 *			SizeInBytes -- The number of bytes this shader uses
 *
 * ============================================================================
 */
FD3DCombinedVertexShader::FD3DCombinedVertexShader(UD3DRenderDevice* InRenDev,
	QWORD InVSEnum,void* pByteCode,DWORD SizeInBytes)
{
	guard(FD3DCombinedVertexShader::FD3DCombinedVertexShader);
	RenDev = InRenDev;
	VertexShaderEnum = InVSEnum;
	// set byte size of loaded microcode
	CombinedMicrocodeSize = SizeInBytes;
	// Allocate the space for the shader
	CombinedMicrocode = NEW DWORD[SizeInBytes / 4];	
	// Copy the contents
	appMemcpy(CombinedMicrocode,pByteCode,SizeInBytes);
	unguard;
}


//	scion sz -
//	FD3DCombinedVertexShader::~FD3DCombinedVertexShader
//
FD3DCombinedVertexShader::~FD3DCombinedVertexShader()
{
	if( CombinedMicrocode )
		delete [] CombinedMicrocode;
}

//
//	FD3DIndexBuffer::FD3DIndexBuffer
//
FD3DIndexBuffer::FD3DIndexBuffer(UD3DRenderDevice* InRenDev,FRenderResource* InRes) :
	FD3DResource(InRenDev,InRes)
{
	Direct3DIndexBuffer8 = NULL;
}


/* scion ======================================================================
 * FD3DIndexBuffer::InternalRelease
 * Author: jg
 * 
 * Either releases the internal pointer or deletes it based upon the UMA flag
 *
 * ============================================================================
 */
void FD3DIndexBuffer::InternalRelease(void)
{
	// If there is a currently valid buffer clean it up
	if (Direct3DIndexBuffer8)
	{
		// If it is UMA then we need to free the resource manually rather than
		// just lowering its ref count
#ifdef _XBOX
		if (bIsUMA == 1)
		{
			delete Direct3DIndexBuffer8;
		}
		else
#endif
		{
			Direct3DIndexBuffer8->Release();
		}
		// Zero it so we don't accidentally clean it up twice
		Direct3DIndexBuffer8 = NULL;
	}
}

//
//	FD3DIndexBuffer::~FD3DIndexBuffer
//
FD3DIndexBuffer::~FD3DIndexBuffer()
{
	InternalRelease();
}

//
//  FD3DIndexBuffer::CalculateFootprint
//
INT FD3DIndexBuffer::CalculateFootprint()
{
	return CachedSize;
}

//
//	FD3DIndexBuffer::Cache
//
void FD3DIndexBuffer::Cache(FIndexBuffer* SourceIndexBuffer)
{
	guard(FD3DIndexBuffer::Cache);

	INT		Size = Max(2,SourceIndexBuffer->GetSize());
	HRESULT	Result;

	if(!Direct3DIndexBuffer8 || CachedSize != Size)
	{
		// Release the existing index buffer.
		InternalRelease();

		D3DFORMAT IndexFormat;
		if( SourceIndexBuffer->GetIndexSize() == sizeof(DWORD) )
#ifdef _XBOX
		{
			IndexFormat = (D3DFORMAT) 0;
			check(0);
		}
#else
			IndexFormat = D3DFMT_INDEX32;
#endif
		else
			IndexFormat = D3DFMT_INDEX16;

		DWORD UsageFlags = D3DUSAGE_WRITEONLY;

#ifndef _XBOX
		if( !RenDev->UseHardwareTL )
			UsageFlags |= D3DUSAGE_SOFTWAREPROCESSING;
#endif

#ifdef _XBOX
		// Make sure that it both supports this and there is a valid pointer
		DWORD dwPtr = (DWORD)SourceIndexBuffer->GetContentsPtr();
		//scion jg -- If the stream supports UMA use that instead
		if (SourceIndexBuffer->bIsUMA && dwPtr)
		{
			bIsUMA = 1;
			// Create our own self managed index buffer
			D3DIndexBuffer* pBuff = new D3DIndexBuffer;
			// Clear the memory out after allocating it
			appMemzero(pBuff,sizeof(D3DIndexBuffer));
			// Tell D3D we are an index buffer
			pBuff->Common = 1 | D3DCOMMON_TYPE_INDEXBUFFER;
			// Set the data directly to that of the source index
			pBuff->Data = dwPtr;
			Direct3DIndexBuffer8 = pBuff;

			// Update cached info.
			CachedRevision = SourceIndexBuffer->GetRevision();
			CachedSize = Size;
			return;
		}
		else
#endif
		{
			// Create a new index buffer.
			Result = RenDev->Direct3DDevice8->CreateIndexBuffer(Size,UsageFlags,IndexFormat,RESOURCE_POOL,&Direct3DIndexBuffer8);

			if( FAILED(Result) )
				appErrorf(TEXT("CreateIndexBuffer failed(%s)."),*D3DError(Result));
		}
	}

	if(CachedRevision != SourceIndexBuffer->GetRevision())
	{
		// Lock the index buffer.
		BYTE*	IndexBufferContents = NULL;

		Result = Direct3DIndexBuffer8->Lock(0,Size,&IndexBufferContents,0);

		if( FAILED(Result) )
			appErrorf(TEXT("IDirect3DIndexBuffer8::Lock failed(%s)."),*D3DError(Result));

		// Fill the index buffer.
		SourceIndexBuffer->GetContents(IndexBufferContents);

		// Unlock the index buffer.
		Result = Direct3DIndexBuffer8->Unlock();

		if( FAILED(Result) )
			appErrorf(TEXT("IDirect3DIndexBuffer8::Unlock failed(%s)."),*D3DError(Result));
	}

	// Update cached info.
	CachedRevision = SourceIndexBuffer->GetRevision();
	CachedSize = Size;

	unguard;
}

//
//	FD3DDynamicIndexBuffer::FD3DDynamicIndexBuffer
//
#define INITIAL_DYNAMIC_INDEXBUFFER_SIZE	16384	// Initial size of dynamic index buffers, in bytes.

FD3DDynamicIndexBuffer::FD3DDynamicIndexBuffer(UD3DRenderDevice* InRenDev, INT InIndexSize) : FD3DIndexBuffer(InRenDev,NULL), IndexSize(InIndexSize)
{
	guard(FD3DDynamicIndexBuffer::FD3DDynamicIndexBuffer);

	Reallocate(INITIAL_DYNAMIC_INDEXBUFFER_SIZE);

	unguard;
}

#ifndef _XBOX //scion jg -- Rewrote these for the xbox
//
//	FD3DDynamicIndexBuffer::Reallocate
//
void FD3DDynamicIndexBuffer::Reallocate(INT NewSize)
{
	guard(FD3DDynamicIndexBuffer::Reallocate);

	debugf(NAME_DevD3D,TEXT("Allocating %u byte dynamic index buffer."),NewSize);

	// Release the old index buffer.
	// NOTE: Unsure of this, but I imagine the old index buffer will actually
	// stick around until all rendering calls using it are retired.
	//!!vogel: see comment in VertexBuffer::Reallocate
	if(Direct3DIndexBuffer8)
	{
		Direct3DIndexBuffer8->Release();
		Direct3DIndexBuffer8 = NULL;
	}

	// Create a dynamic vertex buffer to hold the vertices.
	CachedSize = NewSize;
	Tail = 0;

	D3DFORMAT IndexFormat;
	if( IndexSize == sizeof(DWORD) )
#ifdef _XBOX
	{
		IndexFormat = (D3DFORMAT) 0;
		check(0);
	}
#else
		IndexFormat = D3DFMT_INDEX32;
#endif
	else
		IndexFormat = D3DFMT_INDEX16;

	DWORD UsageFlags = D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY;

#ifndef _XBOX
	if( !RenDev->UseHardwareTL )
		UsageFlags |= D3DUSAGE_SOFTWAREPROCESSING;
#endif
	
	D3DPOOL PoolFlags	= D3DPOOL_DEFAULT;
	INT Result	 = D3D_OK;
	INT NumTries = 0;

	while( NumTries < 3 )
	{
		// Use systemmem pool if allocation in default pool fails.
		if( NumTries == 2 )
			PoolFlags = D3DPOOL_SYSTEMMEM; 

		// Create a new Direct3D vertex buffer.
		Result = RenDev->Direct3DDevice8->CreateIndexBuffer(CachedSize,UsageFlags,IndexFormat,PoolFlags,&Direct3DIndexBuffer8);

		if( SUCCEEDED(Result) )
				break;
#ifndef _XBOX
		if( RenDev->AvoidHitches )
			NumTries++;
		else
			if( FAILED(RenDev->Direct3DDevice8->ResourceManagerDiscardBytes(0/*CachedSize*/) ) )
				appErrorf(TEXT("ResourceManagerDiscardBytes failed."));
#endif
		NumTries++;
	}

	if( FAILED(Result) )
		appErrorf(TEXT("CreateIndexBuffer failed(%s)."),*D3DError(Result));

	unguard;
}

//
//	FD3DDynamicIndexBuffer::AddIndices
//
INT FD3DDynamicIndexBuffer::AddIndices(FIndexBuffer* Indices)
{
	guard(FD3DDynamicIndexBuffer::AddIndices);

    INT	Size, Stride;
    guard(GetSize);
	Size = Indices->GetSize();
	Stride = Indices->GetIndexSize();
    unguard;

	// If the dynamic index buffer isn't big enough to contain all the indices, resize it.
	DWORD	LockFlags = D3DLOCK_NOOVERWRITE;

	if(Size > CachedSize)
	{
		Reallocate(Size);
#ifndef _XBOX
		LockFlags = D3DLOCK_DISCARD;
#else
		LockFlags = 0;
#endif
	}

	// If the dynamic index buffer will overflow with the additional indices, flush it.
	if(Tail + Size > CachedSize)
	{
		Tail = 0;
#ifndef _XBOX
		LockFlags = D3DLOCK_DISCARD;
#else
		LockFlags = 0;
#endif
	}

	// Determine the offset in the index buffer to allocate the indices at.
	INT	IndexBufferOffset = Tail;

	// Lock the dynamic index buffer.
	BYTE*	IndexBufferContents = NULL;

	HRESULT	Result = Direct3DIndexBuffer8->Lock(IndexBufferOffset,Size,&IndexBufferContents,LockFlags);

	if( FAILED(Result) )
		appErrorf(TEXT("IDirect3DIndexBuffer8::Lock failed(%s)."),*D3DError(Result));

	BYTE*	Buffer		= IndexBufferContents;
	DWORD*	BufferEnd	= NULL;

#ifdef _DEBUG
	// Tag memory if overflow detection is enabled.
	if( RenDev->CheckForOverflow )
	{
		if( Size+4 > RenDev->StaticBuffer.Num() )
		{
			RenDev->StaticBuffer.Empty();
			RenDev->StaticBuffer.Add( 2 * Max(Size,65535) );
		}
		BufferEnd	= (DWORD*) &RenDev->StaticBuffer(Size+1);
		*BufferEnd	= 0x03221977;
		Buffer		= &RenDev->StaticBuffer(0);
	}
#endif

	// Copy the indices into the index buffer.
	Indices->GetContents(Buffer);

	// Verify tag.
	if( RenDev->CheckForOverflow )
	{
		check( *BufferEnd == 0x03221977 );
		appMemcpy( IndexBufferContents, Buffer, Size );
	}

	// Unlock the dynamic index buffer.
	Result = Direct3DIndexBuffer8->Unlock();

	if( FAILED(Result) )
		appErrorf(TEXT("IDirect3DIndexBuffer8::Unlock failed(%s)."),*D3DError(Result));

	// Update the tail pointer.
	Tail = IndexBufferOffset + Size;

	return IndexBufferOffset / Stride;

	unguard;
}
#else
/* scion ======================================================================
 * FD3DDynamicIndexBuffer::Reallocate
 * Author: jg
 * 
 * Not used. All allocations occur in AddIndices()
 *
 * input:	ignored
 *
 * ============================================================================
 */
void FD3DDynamicIndexBuffer::Reallocate(INT)
{
}

/* scion ======================================================================
 * FD3DDynamicIndexBuffer::AddIndices
 * Author: jg
 * 
 * Determines if there is a previously allocated chunk of indices that is
 * available for reuse. If not it creates a new cached index buffer. This
 * method keeps all of the indices in non-contiguous RAM, which is faster.
 * Note: At first glance this looks like it can grow unbounded if there are a
 * lot of index buffers that are locked, but in reality that won't happen. Most
 * of the time there will never be more than 2 cached buffers in the array since
 * index buffers aren't GPU objects, they are released much faster.
 *
 * input:	Indices FIndexBuffer* The index buffer stream to add
 *
 * ============================================================================
 */
INT FD3DDynamicIndexBuffer::AddIndices(FIndexBuffer* Indices)
{
	guard(FD3DDynamicIndexBuffer::AddIndices);
	// Assume we do not have a valid buffer
	FCachedIndexBuffer* pBuffer = NULL;
	// Figure out how large of a buffer we need
	INT nSize = Indices->GetSize() / sizeof(_WORD);
	// Now search for an available buffer
	for (INT nNum = 0; nNum < CachedBuffers.Num() && pBuffer == NULL; nNum++)
	{
		// Check to see if this buffer is available
		if (CachedBuffers(nNum)->IndexBuffer.IsBusy() == 0)
		{
			// We have a winner
			pBuffer = CachedBuffers(nNum);
		}
	}
	// Allocate a whole new cached buffer if need be
	if (pBuffer == NULL)
	{
		// Create our own self managed dynamic index buffer
		pBuffer = NEW FCachedIndexBuffer;
		bIsUMA = 1;
		// Clear the memory out after allocating it
		appMemzero(&pBuffer->IndexBuffer,sizeof(D3DIndexBuffer));
		// Tell D3D we are an index buffer
		pBuffer->IndexBuffer.Common = 1 | D3DCOMMON_TYPE_INDEXBUFFER;
		// Add it to the cached buffers array, so we can reuse it later
		CachedBuffers.AddItem(pBuffer);
	}
	checkSlow(pBuffer);
	// See if there is enough space without reallocating
	if (pBuffer->Indices.Num() < nSize)
	{
		// Go ahead and size the TArray to be the right size
		pBuffer->Indices.Add(nSize - pBuffer->Indices.Num());
		// Re-update the data pointer in case this caused the memory
		// location to be changed
		pBuffer->IndexBuffer.Data = (DWORD)&pBuffer->Indices(0);
	}
	// Fill the index buffer
	Indices->GetContents(&pBuffer->Indices(0));
	// Now set the pointer so that the render interface knows who's active
	Direct3DIndexBuffer8 = &pBuffer->IndexBuffer;
	CachedSize = nSize;
	return 0;
	unguard;
}

/* scion ======================================================================
 * FD3DDynamicIndexBuffer::~FD3DDynamicIndexBuffer
 * Author: jg
 * 
 * Releases any of the memory that was allocated
 *
 * ============================================================================
 */
FD3DDynamicIndexBuffer::~FD3DDynamicIndexBuffer()
{
	Direct3DIndexBuffer8 = NULL;
	// Loop through dumping all of the indices and the delete the cache struct
	for (INT nIndex = 0; nIndex < CachedBuffers.Num(); nIndex++)
	{
		CachedBuffers(nIndex)->Indices.Empty();
		delete CachedBuffers(nIndex);
	}
	CachedBuffers.Empty();
}

#endif

INT appAnsiStrlen( ANSICHAR* p )
{
	INT i;
	for( i=0;p[i];i++ );
	return i;
}

//
// FD3DPixelShader::FD3DPixelShader
//
FD3DPixelShader::FD3DPixelShader( UD3DRenderDevice* InRenDev, EPixelShader InType, ANSICHAR* InSource, ANSICHAR* InFilename, DWORD* InData )
{
	guard(FD3DPixelShader::FD3DPixelShader);

	RenDev	= InRenDev;
	Type	= InType;
	Source	= InSource;
	Filename = InFilename;
	Data = InData;

	HRESULT Result;

#ifndef _XBOX

	ID3DXBuffer *Object = NULL;
	ID3DXBuffer *Errors = NULL;

	guard(AssemblePixelShader);	
	// Assemble the pixel shader source code
	Result = D3DXAssembleShader( Source, appAnsiStrlen(Source), 0, NULL, &Object, &Errors );
	if( FAILED(Result) )
	{		
		if( Errors )
			appErrorf(TEXT("DXAssembleShader failed(%s): %s"),*D3DError(Result), appFromAnsi((ANSICHAR*)Errors->GetBufferPointer()) );
		else
			appErrorf(TEXT("DXAssembleShader failed(%s).  Len was %d"),*D3DError(Result), appAnsiStrlen(Source) );
	}
	unguard;
	// Create the pixel shader
	Result = RenDev->Direct3DDevice8->CreatePixelShader( (DWORD*) Object->GetBufferPointer(), &Handle );
	Object->Release();

#else

	LPXGBUFFER	Errors	= NULL;

	// scion sz - load from precompiled microcode stored in struct
	if( Data )
	{
		// Create the pixel shader
		Result = RenDev->Direct3DDevice8->CreatePixelShader( (D3DPIXELSHADERDEF*)Data, &Handle );        
	}
	else if( Filename )
	{
		D3DPIXELSHADERDEF_FILE psdf;
		//jg -- Rewrote this code to use the file manager so that they can
		// be read off of the harddrive or DVD
		FArchive* ShaderFile = GFileManager->CreateFileReader(Filename);
		if (ShaderFile == NULL)
		{
			appErrorf(TEXT("Couldn't open pixel shader file : %s"),Filename);
		}
		// Read the pixel shader
		ShaderFile->Serialize(&psdf,sizeof(D3DPIXELSHADERDEF_FILE));
		// Now close the file
		delete ShaderFile;
		// Make sure the pixel shader is valid
		if (psdf.FileID != D3DPIXELSHADERDEF_FILE_ID)
		{
			appErrorf(TEXT("Invalid pixel shader in file : %s"),Filename);
		}
		// Create the pixel shader
		Result = RenDev->Direct3DDevice8->CreatePixelShader( (D3DPIXELSHADERDEF*) &(psdf.Psd), &Handle );
	}
	else
	{
		LPXGBUFFER	Object	= NULL;
		Result = XGAssembleShader( NULL, Source, appAnsiStrlen(Source), 0, NULL, &Object, &Errors, NULL, NULL, NULL, NULL );
		if( FAILED(Result) )
		{
			if( Errors )
				appErrorf(TEXT("XGAssembleShader failed(%s): %s"),*D3DError(Result), appFromAnsi((ANSICHAR*)XGBuffer_GetBufferPointer(Errors)) );
			else
				appErrorf(TEXT("XGAssembleShader failed(%s).  Len was %d"),*D3DError(Result), appAnsiStrlen(Source) );
		}
		// Create the pixel shader
		Result = RenDev->Direct3DDevice8->CreatePixelShader( (D3DPIXELSHADERDEF*) XGBuffer_GetBufferPointer(Object), &Handle );
	}	

#endif	

	if( FAILED(Result) )
		appErrorf(TEXT("CreatePixelShader failed(%s)."),*D3DError(Result));

	guard(AddPixelShader);
	// Add the Pixel shader to the device Pixel shader list.
	if( RenDev->PixelShaders(Type) )
		delete RenDev->PixelShaders(Type);
	RenDev->PixelShaders(Type) = this;
	unguard;

	unguard;
}

//
//	FD3DPixelShader::~FD3DPixelShader
//
FD3DPixelShader::~FD3DPixelShader()
{
	guard(FD3DPixelShader::~FD3DPixelShader);

	// Delete the Pixel shader from D3D
	if( Handle )
		RenDev->DeferredState.DeletePixelShader(Handle);
	Handle = 0;

	unguard;
}
