/*=============================================================================
	D3DRenderDevice.cpp: Unreal Direct3D render device implementation.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/

#include "D3DDrv.h"

#ifdef _XBOX
#define LOAD_PIXEL_SHADERS_HEADERS
#endif

// scion sz - pixel shader headers
#ifdef LOAD_PIXEL_SHADERS_HEADERS
#include "XboxPixelShaders.h"
#endif

#ifdef PROFILE
#ifdef _XBOX
#define PERFORMANCE_STATS 1
#else
#define PERFORMANCE_STATS 0
#endif
#endif

#if PERFORMANCE_STATS
#include <d3d8perf.h>
#endif

IMPLEMENT_CLASS(UD3DRenderDevice);

//jg -- Load the vertex shader cache using the vertexshaders.xvs package
#if !defined(XBOX_ASSET_FORMAT) && (defined(_XBOX) && !defined(_DEBUG))
#define XBOX_ASSET_FORMAT
#endif

//
// Voodoo 3 support globals.
//
D3DTEXTUREOP DUMMY_MODULATE2X = D3DTOP_MODULATE2X;

//
//	Definitions.
//
#define HIT_SIZE 8
#define HIT_COLOR 0xfe0d

#ifdef _XBOX
#ifndef FINAL_RELEASE
//scion jg -- Globals for helping in GHE debugging
ANSICHAR* GSSPFile = NULL;
INT GSSPLineNo = 0;
#endif
#endif

//
//	UD3DRenderDevice::UD3DRenderDevice
//
UD3DRenderDevice::UD3DRenderDevice() :
	RenderInterface(this),
	DefaultRenderTarget(NULL),
	ShaderCodeFragments(this),
	bInitialized(0)
{
	PixelShaders.AddZeroed(PS_MAX);
#ifdef _XBOX
	// scion sz - copy global present params
	extern D3DPRESENT_PARAMETERS GPresentParms;
	appMemcpy( &PresentParms, &GPresentParms, sizeof(D3DPRESENT_PARAMETERS) );
#endif
	FSAAScale[0] = FSAAScale[1] = 1.f;
}

//
//	UD3DRenderDevice::StaticConstructor
//
void UD3DRenderDevice::StaticConstructor()
{
	guard(UD3DRenderDevice::StaticConstructor);

	new(GetClass(),TEXT("UseHardwareTL"),		RF_Public)UBoolProperty ( CPP_PROPERTY(UseHardwareTL       ), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("UseHardwareVS"),		RF_Public)UBoolProperty ( CPP_PROPERTY(UseHardwareVS       ), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("UsePrecaching"),		RF_Public)UBoolProperty ( CPP_PROPERTY(UsePrecaching       ), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("UseTrilinear"),        RF_Public)UBoolProperty ( CPP_PROPERTY(UseTrilinear        ), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("UseVSync"),            RF_Public)UBoolProperty ( CPP_PROPERTY(UseVSync            ), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("UseTripleBuffering"),	RF_Public)UBoolProperty ( CPP_PROPERTY(UseTripleBuffering  ), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("UseCubemaps"),			RF_Public)UBoolProperty ( CPP_PROPERTY(UseCubemaps		   ), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("ReduceMouseLag"),      RF_Public)UBoolProperty ( CPP_PROPERTY(ReduceMouseLag      ), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("UseXBoxFSAA"),			RF_Public)UBoolProperty ( CPP_PROPERTY(UseXBoxFSAA		   ), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("UseNPatches"),			RF_Public)UBoolProperty ( CPP_PROPERTY(UseNPatches		   ), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("CheckForOverflow"),	RF_Public)UBoolProperty ( CPP_PROPERTY(CheckForOverflow	   ), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("DecompressTextures"),	RF_Public)UBoolProperty ( CPP_PROPERTY(DecompressTextures  ), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("AvoidHitches"),		RF_Public)UBoolProperty ( CPP_PROPERTY(AvoidHitches        ), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("OverrideDesktopRefreshRate"),	RF_Public)UBoolProperty ( CPP_PROPERTY(OverrideDesktopRefreshRate ), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("AdapterNumber"),       RF_Public)UIntProperty  ( CPP_PROPERTY(AdapterNumber       ), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("MaxPixelShaderVersion"),	RF_Public)UIntProperty  ( CPP_PROPERTY(MaxPixelShaderVersion), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("DesiredRefreshRate"),	RF_Public)UIntProperty  ( CPP_PROPERTY(DesiredRefreshRate	), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("LevelOfAnisotropy"),	RF_Public)UIntProperty  ( CPP_PROPERTY(LevelOfAnisotropy	), TEXT("Options"), CPF_Config );
    new(GetClass(),TEXT("DetailTexMipBias"),    RF_Public)UFloatProperty( CPP_PROPERTY(DetailTexMipBias	), TEXT("Options"), CPF_Config );
    new(GetClass(),TEXT("DefaultTexMipBias"),   RF_Public)UFloatProperty( CPP_PROPERTY(DefaultTexMipBias	), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("TesselationFactor"),   RF_Public)UFloatProperty( CPP_PROPERTY(TesselationFactor	), TEXT("Options"), CPF_Config );

	new(GetClass(),TEXT("UsePCSkeletalShader"),   RF_Public)UBoolProperty( CPP_PROPERTY(UsePCSkeletalShader), TEXT("Options"), CPF_Config );
	new(GetClass(),TEXT("UseXBOXSkeletalShader"),   RF_Public)UBoolProperty( CPP_PROPERTY(UseXBOXSkeletalShader), TEXT("Options"), CPF_Config );

	// Assume we are supporting ZBIAS until we know better.
	SupportsZBIAS = 1;
	unguard;
}

/* scion ======================================================================
 * UD3DRenderDevice::GetCachedResource
 * Author: jg
 * 
 * Determines which hash to find the resource in and uses that to find the
 * resource.
 *
 * input:	CacheID The resource to find by ID
 *			CacheHint Indicates which cache to search
 *
 * ============================================================================
 */
FD3DResource* UD3DRenderDevice::GetCachedResource(_WORD CacheID,
	ECacheHint CacheHint)
{
	guard(UD3DRenderDevice::GetCachedResource);
	// Default to unknown
	FD3DResource* pResource = NULL;
	// Figure out which cache to search
	switch (CacheHint)
	{
		case CH_Texture:
			pResource = TextureCache.GetCachedResource(CacheID);
			break;
		case CH_VertexBuffer:
			pResource = VertexBufferCache.GetCachedResource(CacheID);
			break;
		case CH_IndexBuffer:
			pResource = IndexBufferCache.GetCachedResource(CacheID);
			break;
	}
	return pResource;
	unguard;
}

/* scion ======================================================================
 * UD3DRenderDevice::CacheResource
 * Author: jg
 * 
 * Adds an item to the resource cache of the specified type
 *
 * input:	pResource The resource to add to the cache
 *			CacheHint Indicates which cache to add to
 *
 * ============================================================================
 */
void UD3DRenderDevice::CacheResource(FD3DResource* pResource,
	ECacheHint CacheHint)
{
	guard(UD3DRenderDevice::CacheResource);
	// Don't cache an item multiple times
	if (pResource->CacheId == INVALID_CACHEID)
	{
		// Figure out which cache to add to
		switch (CacheHint)
		{
			case CH_Texture:
				TextureCache.Cache((FD3DTexture*)pResource);
				break;
			case CH_VertexBuffer:
				VertexBufferCache.Cache((FD3DVertexStream*)pResource);
				break;
			case CH_IndexBuffer:
				IndexBufferCache.Cache((FD3DIndexBuffer*)pResource);
				break;
		}
	}
#ifdef _DEBUG
	else
	{
		debugf(TEXT("Attempt to re-cache item CacheId(%d), Type(%d)"),
			(INT)pResource->CacheId,CacheHint);
	}
#endif
	unguard;
}

/* scion ======================================================================
 * UD3DRenderDevice::FlushResource
 * Author: jg
 * 
 * Removes a cached item from the correct resource cache
 *
 * input:	CacheID The resource to find by ID
 *			CacheHint Indicates which cache to search
 *
 * ============================================================================
 */
void UD3DRenderDevice::FlushResource(_WORD CacheID,ECacheHint CacheHint)
{
	guard(UD3DRenderDevice::FlushResource);

	FD3DResource* CachedResource = GetCachedResource(CacheID,CacheHint);

	if (CachedResource)
	{
		// Tell the cache to remove this item
		switch (CacheHint)
		{
			case CH_Texture:
				TextureCache.Flush(CacheID);
				break;
			case CH_VertexBuffer:
				VertexBufferCache.Flush(CacheID);
				break;
			case CH_IndexBuffer:
				IndexBufferCache.Flush(CacheID);
				break;
		}
		delete CachedResource;
	}
#ifndef FINAL_RELEASE
	else
	{
		debugf(TEXT("Attempt to flush unknown resource %d type %d"),
			(INT)CacheID,CacheHint);
	}
#endif

	unguard;
}

/* scion ======================================================================
 * UD3DRenderDevice::FlushResource
 * Author: jg
 * 
 * Figures out which cache to flush and flushes the resource
 *
 * input:	CacheID The resource to find by ID
 *			CacheHint Indicates which cache to search
 *
 * ============================================================================
 */
void UD3DRenderDevice::FlushResource(FD3DResource* pResource)
{
	guard(UD3DRenderDevice::FlushResource);
	check(pResource);
	// Assume texture
	ECacheHint CacheHint = CH_Texture;
	// Check for a vertex buffer
	if (pResource->GetVertexStream())
	{
		// This is a vertex stream, so change the cache hint
		CacheHint = CH_VertexBuffer;
	}
	else if (pResource->GetIndexBuffer())
	{
		// This is a index buffer, so change the cache hint
		CacheHint = CH_IndexBuffer;
	}
	// Only flush if it was cached
	if (ResourceCached(pResource->CacheId,CacheHint))
	{
		// Call the common method
		FlushResource(pResource->CacheId,CacheHint);
	}
	// Otherwise it is safe to delete
	else
	{
		delete pResource;
	}
	unguard;
}

/* scion ======================================================================
 * UD3DRenderDevice::Destroy
 * Author: jg
 * 
 * Flushes all D3D resources before exiting
 *
 * ============================================================================
 */
void UD3DRenderDevice::Destroy(void)
{
	guard(UD3DRenderDevice::Destroy);
	Super::Destroy();
	// Clean up the D3D resources
	Flush(NULL);

	// scion sz
	// clean up the combined vertex shader microcode
	guard(CombinedVertexShaders);
	for( TMap<QWORD, FD3DCombinedVertexShader*>::TIterator It(CombinedVertexShaders); It; ++It )
	{
		FD3DCombinedVertexShader* pCombinedVertexShader = It.Value();
		if( pCombinedVertexShader )
			delete pCombinedVertexShader;
	}
	CombinedVertexShaders.Empty();
	unguard;

	// scion sz
	// clean up the vertex shader configs
	if( VertexShaderConfigs )
	{
		delete VertexShaderConfigs;
		VertexShaderConfigs = NULL;
	}
	if( UserVertexShaderConfigs )
	{
		delete UserVertexShaderConfigs;
		UserVertexShaderConfigs = NULL;
	}

	unguard;
}

//
//	UD3DRenderDevice::Init
//
UBOOL UD3DRenderDevice::Init()
{
	guard(UD3DRenderDevice::Init);

	// scion sz - don't initialize more than once
	if( bInitialized )
		return 1;
	bInitialized = 1;

	HRESULT	Result;

#ifdef _DEBUG
	// Memory overflow detection.
	if( CheckForOverflow )
	{
		//TODO jg -- remove this crap
		StaticBuffer.Empty();
		StaticBuffer.Add( 512 * 1024 );
	}
#endif

#ifndef _XBOX
	HWND hwndDesktop = GetDesktopWindow();
	HDC  hdcDesktop  = GetDC(hwndDesktop);
	DesktopColorBits = GetDeviceCaps( hdcDesktop, BITSPIXEL );
#else
	DesktopColorBits = 32;
#endif

	if(!Direct3D8 || !Direct3DDevice8)
	{
		// Create the Direct3D object.
		guard(CreateDirect3D);

		Direct3D8 = Direct3DCreate8(D3D_SDK_VERSION);

		if(!Direct3D8)
			appErrorf(NAME_FriendlyError, TEXT("Please install DirectX 8.1b or later (see Release Notes for instructions on how to obtain it)"));

		unguard;

		// Enumerate Direct3D adapters.
		guard(EnumAdapters);

		INT	NumAdapters = Direct3D8->GetAdapterCount();

		Adapters.Empty(NumAdapters);

		debugf(NAME_DevD3D,TEXT("Direct3D adapters detected:"));

		for(INT Index = 0;Index < NumAdapters;Index++)
		{
			D3DADAPTER_IDENTIFIER8	AdapterIdentifier;

			Result = Direct3D8->GetAdapterIdentifier(Index,D3DENUM_NO_WHQL_LEVEL,&AdapterIdentifier);

			if( FAILED(Result) )
				appErrorf(TEXT("GetAdapterIdentifier failed(%s)."),*D3DError(Result));

			debugf(NAME_DevD3D,TEXT("	%s/%s"),appFromAnsi(AdapterIdentifier.Driver),appFromAnsi(AdapterIdentifier.Description));

			Adapters.AddItem(AdapterIdentifier);
		}

		if(!Adapters.Num())
			appErrorf(TEXT("No Direct3D adapters found."));

		unguard;
	}

	// Find best Direct3D adapter.
	BestAdapter = D3DADAPTER_DEFAULT;

#ifndef _XBOX
	guard(FindBestAdapter);
	//!!vogel: I assume this is a relic from 3dfx days.
	//for(INT Index = 0;Index < Adapters.Num();Index++)
	//	if(appStrstr(appFromAnsi(Adapters(Index).Description),TEXT("Primary")))
	//		BestAdapter = Index;
	if ( (AdapterNumber>=0) && (AdapterNumber < Adapters.Num()) )
		BestAdapter = AdapterNumber;
	unguard;
#endif

	// Get the Direct3D caps for the best adapter.
	guard(GetDeviceCaps);
	Result = Direct3D8->GetDeviceCaps(BestAdapter,D3DDEVTYPE_HAL,&DeviceCaps8);
	if( FAILED(Result) )
		appErrorf(NAME_FriendlyError, TEXT("Please enable Direct3D acceleration. You can do this by starting dxdiag and enabling Direct3D Acceleration in the Display1/2 tab after installing DirectX 8.1b (or later) and the latest drivers for your graphics card."));
//		appErrorf(TEXT("GetDeviceCaps failed(%s) on adapter %i."),*D3DError(Result), BestAdapter);
	unguard;

	// Check device caps.
	guard(CheckDeviceCaps);

	DeviceCaps8.MaxSimultaneousTextures = Min<DWORD>(4,DeviceCaps8.MaxSimultaneousTextures);
	DeviceCaps8.MaxTextureBlendStages   = Min<DWORD>(4,DeviceCaps8.MaxTextureBlendStages);

	if( (DeviceCaps8.MaxSimultaneousTextures != 4) || (DeviceCaps8.MaxTextureBlendStages != 4) )
		appErrorf( TEXT("Card not suitable for creating content for UC2004") );

	// Check multitexture caps.
	debugf(NAME_DevD3D,TEXT("D3D Driver: MaxTextureBlendStages=%i"),DeviceCaps8.MaxTextureBlendStages);
	debugf(NAME_DevD3D,TEXT("D3D Driver: MaxSimultaneousTextures=%i"),DeviceCaps8.MaxSimultaneousTextures);
	
#ifndef _XBOX
	// Use software pipeline if less than 8 streams are exposed.
	if( DeviceCaps8.MaxStreams < 8 )
		UseHardwareTL = 0;

	// Software pipeline lies about caps.
	if( !(DeviceCaps8.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) || !UseHardwareTL )
	{
		DeviceCaps8.MaxStreams		= 16;
		DeviceCaps8.MaxActiveLights	= 8;
	}
#endif
	
	debugf(NAME_DevD3D,TEXT("D3D Driver: MaxActiveLights=%u"),DeviceCaps8.MaxActiveLights);
	debugf(NAME_DevD3D,TEXT("D3D Driver: MaxPrimitiveCount=%u"),DeviceCaps8.MaxPrimitiveCount);
	debugf(NAME_DevD3D,TEXT("D3D Driver: MaxVertexIndex=%u"),DeviceCaps8.MaxVertexIndex);
	debugf(NAME_DevD3D,TEXT("D3D Driver: MaxStreams=%u"),DeviceCaps8.MaxStreams);
	debugf(NAME_DevD3D,TEXT("D3D Driver: MaxStreamStride=%u"),DeviceCaps8.MaxStreamStride);
	debugf(NAME_DevD3D,TEXT("D3D Driver: MaxVertexShaderConst=%u"),DeviceCaps8.MaxVertexShaderConst);
	debugf(NAME_DevD3D,TEXT("D3D Driver: VertexShaderVersion=%u.%u"),(DeviceCaps8.VertexShaderVersion & 0xFF00) >> 8, DeviceCaps8.VertexShaderVersion & 0xFF );
	debugf(NAME_DevD3D,TEXT("D3D Driver: PixelShaderVersion=%u.%u"),((DeviceCaps8.PixelShaderVersion & 0xFF00) >> 8),DeviceCaps8.PixelShaderVersion & 0xFF );

	// Disable stencil with 16 bit rendering.
	UseStencil &= !(Use16bit && !GIsEditor);

	// Check whether AGP is enabled (major slowdown if not).
	if (DeviceCaps8.DevCaps & D3DDEVCAPS_TEXTURENONLOCALVIDMEM)
		debugf(NAME_DevD3D,TEXT("D3D Driver: AGP support detected"));
	else
		debugf(NAME_DevD3D,TEXT("D3D Driver: WARNING : no AGP support detected"));

	// Calculate pixel shader version.
	PixelShaderVersion = ((DeviceCaps8.PixelShaderVersion & 0xFF00) >> 8) * 10 + DeviceCaps8.PixelShaderVersion & 0xFF;
	PixelShaderVersion = Min(PixelShaderVersion,MaxPixelShaderVersion);

	// Check for ZBIAS support - e.g. TNT2 doesn't support it.
	SupportsZBIAS = DeviceCaps8.RasterCaps & D3DPRASTERCAPS_ZBIAS;

	// Check for cubemap support.
	if( DeviceCaps8.TextureCaps & D3DPTEXTURECAPS_CUBEMAP )
		SupportsCubemaps = UseCubemaps;
	else
		SupportsCubemaps = 0;

	// Check for mipped cubemap support.
	if( DeviceCaps8.TextureCaps & D3DPTEXTURECAPS_MIPCUBEMAP ) 
		UseMippedCubemaps = 1;
	else
		UseMippedCubemaps = 0;

	// Check for DXT1/3/5 support.
	UseDXT1 = SUCCEEDED(Direct3D8->CheckDeviceFormat(BestAdapter,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,0,D3DRTYPE_TEXTURE,D3DFMT_DXT1));
	UseDXT3 = SUCCEEDED(Direct3D8->CheckDeviceFormat(BestAdapter,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,0,D3DRTYPE_TEXTURE,D3DFMT_DXT3));
	UseDXT5 = SUCCEEDED(Direct3D8->CheckDeviceFormat(BestAdapter,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,0,D3DRTYPE_TEXTURE,D3DFMT_DXT5));
	UseA8	= SUCCEEDED(Direct3D8->CheckDeviceFormat(BestAdapter,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,0,D3DRTYPE_TEXTURE,D3DFMT_A8));

	// Override DXT support.
	if( DecompressTextures )
		UseDXT1 = UseDXT3 = UseDXT5 = 0;

	if( !UseDXT1 )
		debugf(NAME_DevD3D,TEXT("D3D Driver: WARNING: no support for DXT1"));
	if( !UseDXT3 )
		debugf(NAME_DevD3D,TEXT("D3D Driver: WARNING: no support for DXT3"));
	if( !UseDXT5 )
		debugf(NAME_DevD3D,TEXT("D3D Driver: WARNING: no support for DXT5"));
	
	// Don't use compressed lightmaps if DXTC not available.
	if( !UseDXT1 || !UseDXT3 || !UseDXT5 )
		UseCompressedLightmaps = 0;

	// Don't use 16 bit textures if DXTC is available.
	if( UseDXT1 )
		Use16bitTextures = 0;

	// Use vertex fog if table fog is not supported.
	UseVertexFog = 0;
	UseRangeFog	 = 0;
	if( !(DeviceCaps8.RasterCaps & D3DPRASTERCAPS_FOGTABLE) )
	{
		UseVertexFog = 1;
		if( DeviceCaps8.RasterCaps & D3DPRASTERCAPS_FOGRANGE )
			UseRangeFog = 1;
	}

	// Log which type of fog we use.
	if( UseVertexFog )
	{
		if( UseRangeFog )
			debugf(NAME_DevD3D,TEXT("D3D Driver: Using range fog"));
		else
			debugf(NAME_DevD3D,TEXT("D3D Driver: Using vertex fog"));
	}
	else
	{
		if( DeviceCaps8.RasterCaps & D3DPRASTERCAPS_WFOG )
			debugf(NAME_DevD3D,TEXT("D3D Driver: Using w- pixel fog"));
		else
			debugf(NAME_DevD3D,TEXT("D3D Driver: Using z- pixel fog"));
	}

	// Only used for debugging.
	FirstColoredMip = 0xFFFF;

	// Verify mipmapping supported.
	if
	(	!(DeviceCaps8.TextureFilterCaps & D3DPTFILTERCAPS_MIPFPOINT)
	&&	!(DeviceCaps8.TextureFilterCaps & D3DPTFILTERCAPS_MIPFLINEAR)
	)
	{
		UseMipmapping = 0;
		debugf(NAME_DevD3D, TEXT("D3D Driver: Mipmapping not available with this driver"));
	}
	else
	{
		UseMipmapping = 1;
		if( DeviceCaps8.TextureFilterCaps & D3DPTFILTERCAPS_MIPFLINEAR )
			debugf(NAME_DevD3D, TEXT("D3D Driver: Supports trilinear"));
		else
			UseTrilinear = 0;
	}

	// Limit texture size if using 16 bit textures.
	if( Use16bitTextures )
	{
		DeviceCaps8.MaxTextureHeight	= 256;
		DeviceCaps8.MaxTextureWidth		= 256;
	}

	if( DeviceCaps8.TextureOpCaps & D3DTEXOPCAPS_BLENDDIFFUSEALPHA )
		debugf(NAME_DevD3D, TEXT("D3D Driver: Supports BLENDDIFFUSEALPHA") );
	else
		DetailTextures = 0;

	if( DeviceCaps8.RasterCaps & D3DPRASTERCAPS_MIPMAPLODBIAS )
 		debugf(NAME_DevD3D, TEXT("D3D Driver: Supports LOD biasing") );

	if( DeviceCaps8.RasterCaps & D3DPRASTERCAPS_ZBIAS )
 		debugf(NAME_DevD3D, TEXT("D3D Driver: Supports Z biasing") );

	if( DeviceCaps8.TextureOpCaps & D3DTEXOPCAPS_ADDSIGNED2X )
 		debugf(NAME_DevD3D, TEXT("D3D Driver: Supports D3DTOP_ADDSIGNED2X") );

	if( DeviceCaps8.TextureOpCaps & D3DTEXOPCAPS_BUMPENVMAP )
 		debugf(NAME_DevD3D, TEXT("D3D Driver: Supports D3DTOP_BUMPENVMAP") );

	if( DeviceCaps8.TextureOpCaps & D3DTEXOPCAPS_BUMPENVMAPLUMINANCE )
 		debugf(NAME_DevD3D, TEXT("D3D Driver: Supports D3DTOP_BUMPENVMAPLUMINANCE") );

	if( DeviceCaps8.TextureOpCaps & D3DTEXOPCAPS_DOTPRODUCT3 )
 		debugf(NAME_DevD3D, TEXT("D3D Driver: Supports D3DTOP_DOTPRODUCT3") );

	if( DeviceCaps8.TextureOpCaps & D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR )
 		debugf(NAME_DevD3D, TEXT("D3D Driver: Supports D3DTOP_MODULATEALPHA_ADDCOLOR") );

	if( DeviceCaps8.TextureOpCaps & D3DTEXOPCAPS_MODULATECOLOR_ADDALPHA )
 		debugf(NAME_DevD3D, TEXT("D3D Driver: Supports D3DTOP_MODULATECOLOR_ADDALPHA") ); 

	// Get device identifier.
	// szDriver, szDescription aren't guaranteed consistent (might change by mfgr, distrubutor, language, etc). Don't do any compares on these.
	// liDriverVersion is safe to do QWORD comparisons on.
	// User has changed drivers/cards iff guidDeviceIdentifier changes.
	guard(GetDeviceIdentifier);
	DeviceIdentifier = Adapters(BestAdapter);
	debugf(NAME_DevD3D,TEXT("Unreal Engine Direct3D support - internal version: SB3"));
	debugf(NAME_DevD3D,TEXT("D3D Device: szDriver=%s"),      appFromAnsi(DeviceIdentifier.Driver));
	debugf(NAME_DevD3D,TEXT("D3D Device: szDescription=%s"), appFromAnsi(DeviceIdentifier.Description));
	debugf(NAME_DevD3D,TEXT("D3D Device: wProduct=%i"),      wProduct=HIWORD(DeviceIdentifier.DriverVersion.HighPart));
	debugf(NAME_DevD3D,TEXT("D3D Device: wVersion=%i"),      wVersion=LOWORD(DeviceIdentifier.DriverVersion.HighPart));
	debugf(NAME_DevD3D,TEXT("D3D Device: wSubVersion=%i"),   wSubVersion=HIWORD(DeviceIdentifier.DriverVersion.LowPart));
	debugf(NAME_DevD3D,TEXT("D3D Device: wBuild=%i"),        wBuild=LOWORD(DeviceIdentifier.DriverVersion.LowPart));
	debugf(NAME_DevD3D,TEXT("D3D Device: dwVendorId=%i"),    DeviceIdentifier.VendorId);
	debugf(NAME_DevD3D,TEXT("D3D Device: dwDeviceId=%i"),    DeviceIdentifier.DeviceId);
	debugf(NAME_DevD3D,TEXT("D3D Device: dwSubSysId=%i"),    DeviceIdentifier.SubSysId);
	debugf(NAME_DevD3D,TEXT("D3D Device: dwRevision=%i"),    DeviceIdentifier.Revision);
	unguard;

    appSprintf( GMachineVideo, TEXT("%s (%i)"), appFromAnsi(DeviceIdentifier.Description), (INT)(LOWORD(DeviceIdentifier.DriverVersion.LowPart)) );

	// Hardware-specific initialization and workarounds for driver and hardware! bugs.
	CubemapTextureAddressing	= D3DTADDRESS_CLAMP;
	IsKyro						= 0;
	IsVoodoo3					= 0;
	Is3dfx						= 0;
	IsGeForce					= 0;
	HasNVCubemapBug				= 0;
	
	if( ParseParam(appCmdLine(),TEXT("nodeviceid")) )
	{
		debugf(NAME_DevD3D,TEXT("D3D Detected: -nodeviceid specified, 3D device identification skipped"));
	}
	else if( DeviceIdentifier.VendorId==0x1002 )
	{
		debugf(NAME_DevD3D,TEXT("D3D Detected: ATI video card"));
		if( DeviceCaps8.MaxSimultaneousTextures == 3 )
			DeviceCaps8.MaxSimultaneousTextures = 2;
	}
	else if( DeviceIdentifier.VendorId==0x121A )
	{
		debugf(NAME_DevD3D,TEXT("D3D Detected: 3dfx video card"));
		Is3dfx = 1;
		if( DeviceIdentifier.DeviceId < 0x0009 )	
		{
			// Voodoo 3/4
			IsVoodoo3			= 1;
			Use16bitTextures	= 1;
			DUMMY_MODULATE2X	= D3DTOP_MODULATE;
		}
	}
	else if( DeviceIdentifier.VendorId==0x8086 )
	{
		debugf(NAME_DevD3D,TEXT("D3D Detected: Intel video card"));			
#if 0
		// No 810/815/830 series chips are supported.
		if( DeviceIdentifier.DeviceId == 0x7121 
		||	DeviceIdentifier.DeviceId == 0x7123
		||	DeviceIdentifier.DeviceId == 0x7125
		||	DeviceIdentifier.DeviceId == 0x7127
		||	DeviceIdentifier.DeviceId == 0x1132
		||	DeviceIdentifier.DeviceId == 0x3577
		)
			appErrorf( NAME_FriendlyError, TEXT("Your graphics card is not supported. Please consult the FAQ"));
#endif
	}
	//else if( DeviceIdentifier.VendorId==0x104A )
	else if( DeviceIdentifier.DeviceId==0x010 )
	{
		debugf(NAME_DevD3D,TEXT("D3D Detected: Kyro video card"));
		IsKyro = 1;
	}
	else if( DeviceIdentifier.VendorId==0x10DE )
	{
		if( DeviceCaps8.MaxSimultaneousTextures == 2 )
		{
			DeviceCaps8.MaxTextureBlendStages = 3;
			IsGeForce = 1;
		}
		CubemapTextureAddressing = D3DTADDRESS_WRAP;
		debugf(NAME_DevD3D,TEXT("D3D Detected: NVidia video card"));
		if( DeviceIdentifier.DeviceId==0x0150 )
		{
			debugf(NAME_DevD3D,TEXT("D3D Detected: GeForce2 GTS"));
			UseMippedCubemaps	  = 0;
		}
		if( DeviceIdentifier.DeviceId==0x0151 )
		{
			debugf(NAME_DevD3D,TEXT("D3D Detected: GeForce2 GTS"));
			UseMippedCubemaps	  = 0;
		}
		if( DeviceIdentifier.DeviceId==0x0152 )
		{
			debugf(NAME_DevD3D,TEXT("D3D Detected: GeForce2 Ultra"));
			UseMippedCubemaps	  = 0;
		}
		if( DeviceIdentifier.DeviceId==0x0153 )
		{
			debugf(NAME_DevD3D,TEXT("D3D Detected: Quadro2 Pro"));
			UseMippedCubemaps	  = 0;
		}

		HasNVCubemapBug = !UseMippedCubemaps;
	}
	else if( DeviceIdentifier.VendorId==0x102B )
	{
		debugf(NAME_DevD3D,TEXT("D3D Detected: Matrox video card"));
		// No G200 chips are supported.
		if( DeviceIdentifier.DeviceId == 0x0521 )
#if 0
			appErrorf( NAME_FriendlyError, TEXT("Your graphics card is not supported. Please consult the FAQ"));
#else
			Is3dfx = 1;
#endif
		// Treat G400/ G450/ G500 like a 3dfx card and remove cubemaps.
		if( (DeviceIdentifier.DeviceId == 0x0525) || (DeviceIdentifier.DeviceId == 0x2527) ) 
		{
			// Card lies about amount of texture units.
			DeviceCaps8.MaxSimultaneousTextures	= 2;
			Is3dfx								= 1;
		}
	}
	else if( DeviceIdentifier.VendorId==0x1023 )
	{
		debugf(NAME_DevD3D,TEXT("D3D Detected: Trident video card"));
	}
	else if( DeviceIdentifier.VendorId==0x1039 )
	{
		debugf(NAME_DevD3D,TEXT("D3D Detected: SiS video card"));
	}
	else if( DeviceIdentifier.VendorId==0x5333 )
	{
		debugf(NAME_DevD3D,TEXT("D3D Detected: S3 video card"));
		switch( DeviceIdentifier.DeviceId )
		{
		case 0x5631: case 0x8811: case 0x8812: case 0x883d:	case 0x8880: case 0x88c0: case 0x88c1: case 0x88d0:
		case 0x88d1: case 0x88f0: case 0x8901: case 0x8904:	case 0x8a01: case 0x8a10: case 0x8a13: case 0x8a20:
		case 0x8a21: case 0x8a22: case 0x8a23: case 0x8a25:	case 0x8a26: case 0x8c00: case 0x8c01: case 0x8c02:
		case 0x8c03: case 0x8c10: case 0x8c12: case 0x8c22: case 0x8c2a: case 0x8c2b: case 0x8c2c: case 0x8c2d:
		case 0x8c2e: case 0x8c2f: case 0x8d04: case 0x9102:
			// Treat S3 cards like 3dfx cards.
			Is3dfx = 1;
			break;
		default:
			break;
		}
	}
	else
	{
		debugf(NAME_DevD3D,TEXT("D3D Detected: Generic 3D accelerator"));
	}

	if( SupportsCubemaps )
	{
		if ( UseMippedCubemaps )
			debugf(NAME_DevD3D, TEXT("D3D Device: using cubemaps [with mipmaps]") );
		else
			debugf(NAME_DevD3D, TEXT("D3D Device: using cubemaps [without mipmaps]") );
	}
	else
		debugf(NAME_DevD3D, TEXT("D3D Device: not using cubemaps") );

#ifdef _XBOX
	//!!vogel: workaround for Feb XDK
	DeviceCaps8.MaxTextureBlendStages = 4;
#endif
	unguard;

#ifndef FINAL_RELEASE
	// Register Stats.
	D3DStats.Init();
#endif

	MEMORYSTATUS MemStatusBefore;
	GlobalMemoryStatus( &MemStatusBefore );

#ifndef FINAL_RELEASE
	// scion sz -
	// precache all default vertex shader microcode combinations
	if( !VertexShaderConfigs )
	{
		// load configuration from VertexShaderConfigs.ini file
		VertexShaderConfigs = ConstructObject<UVertexShaderConfigs>(UVertexShaderConfigs::StaticClass(), (UObject*)-1, NAME_None, RF_Keep);		
		// unload the config entry
		GConfig->UnloadFile( *FString::Printf(TEXT("%s.ini"),*UVertexShaderConfigs::StaticClass()->ClassConfigName) );

#ifndef XBOX_ASSET_FORMAT //jg -- Don't do this if we are using our cache
 		// create microcode from the vertex shader config ini files
		RenderInterface.SetPrecacheMode( PRECACHE_VertexShaders );
		// precache all default configs
		VertexShaderConfigs->PrecacheVertexShaderEnums( this );		
		RenderInterface.SetPrecacheMode( PRECACHE_None );
#endif
		// empty list of enums since it won't be used again
		VertexShaderConfigs->VertexShaderEnums.Empty();
	}

	// scion sz -
	// precache all user vertex shader microcode combinations
	if( !UserVertexShaderConfigs )
	{
		// load configuration from UserVertexShaderConfigs.ini file
		// any new vertex shader enums will be appended to the user config and saved to ini
		UserVertexShaderConfigs = ConstructObject<UUserVertexShaderConfigs>(UUserVertexShaderConfigs::StaticClass(), (UObject*)-1, NAME_None, RF_Keep);
		// load config for this class only and don't propagate to superclass configs
		UserVertexShaderConfigs->VertexShaderEnums.Empty();
		UserVertexShaderConfigs->LoadConfig( 0 );

#ifndef XBOX_ASSET_FORMAT //jg -- Don't do this if we are using our cache
		// create microcode from the vertex shader config ini files
		RenderInterface.SetPrecacheMode( PRECACHE_VertexShaders );
		// precache user configs
		UserVertexShaderConfigs->PrecacheVertexShaderEnums( this );
		RenderInterface.SetPrecacheMode( PRECACHE_None );		
#endif
	}
#endif //!FINAL_RELEASE

#if defined(XBOX_ASSET_FORMAT) && defined(_XBOX)
	//jg -- Load the cache from our prebuilt object
	debugf(TEXT("Loading vertex shaders from VertexShaders.xvs"));
	// Create the named instance of the vertex shader cache
	UVertexShaderCache* pCache = LoadObject<UVertexShaderCache>(NULL,
		TEXT("VertexShaderCache"),TEXT("VertexShaders.xvs"),LOAD_NoFail,NULL);
	check(pCache);
	// Iterate through all of its precompiled vertex shaders and add them to our RenDev
	for (INT Index = 0; Index < pCache->VertexShaders.Num(); Index++)
	{
		FVertexShader& VShader = pCache->VertexShaders(Index);
		// Only create the shader with valid bodies
		if (VShader.VSByteCode.Num() > 0)
		{
			// Rebuild the vertex shader enum
			DWORD High = VShader.VSEnumHigh;
			DWORD Low = VShader.VSEnumLow;
			QWORD VSEnum = (((QWORD)High << 32) | (QWORD)Low);
			// Make sure there are no duplicates
			if (CombinedVertexShaders.FindRef(VSEnum) == 0)
			{
				// Now create the shader
				FD3DCombinedVertexShader* pShader = NEW FD3DCombinedVertexShader(this,
					VSEnum,&VShader.VSByteCode(0),VShader.VSByteCode.Num());
				check(pShader);
				// Now add this to our map so we never have to create it
				CombinedVertexShaders.Add(VSEnum,pShader);
			}
		}
	}
	// Delete the vertex shader cache object as it is no longer needed
	delete pCache;
#endif

	MEMORYSTATUS MemStatusAfter;
	GlobalMemoryStatus( &MemStatusAfter );	
	debugf( NAME_DevSam, TEXT("MEMORY: %d KByte of physical memory in use by VERTEX SHADERS"), (MemStatusBefore.dwAvailPhys - MemStatusAfter.dwAvailPhys) / 1024 );

	return 1;
	unguard;
}

//
//	UD3DRenderDevice::Exit
//
void UD3DRenderDevice::Exit(UViewport* Viewport)
{
	guard(UD3DRenderDevice::Exit);
#ifdef _XBOX
	// Persist display.
	Direct3DDevice8->Present( NULL, NULL, NULL, NULL );
	Direct3DDevice8->PersistDisplay();
#else

	Flush(Viewport);

	RestoreGamma();

	if(Direct3D8)
	{
		Direct3D8->Release();
		Direct3D8 = NULL;
	}

	if(Direct3DDevice8)
	{
		Direct3DDevice8->Release();
		Direct3DDevice8 = NULL;
	}

	if( hDC )
		ReleaseDC(hWnd,hDC);

#endif
	unguard;
}

//
//	UD3DRenderDevice::SetRes
//
UBOOL UD3DRenderDevice::SetRes(UViewport* Viewport,INT NewX,INT NewY,UBOOL Fullscreen,INT ColorBytes,UBOOL bSaveSize)
{
	guard(UD3DRenderDevice::SetRes);

	HRESULT	Result = S_OK;

	//!!vogel: returning 0 causes infinite loops and I'd rather catch those bugs at the root.

	if( !GIsEditor && LockedViewport )
		appErrorf(TEXT("Can't change resolution while render device is locked!"));

#if 0
	// Make sure that menus are at least 640x480 even if you ALT-TAB in/ out of fullscreen.
	if( Viewport->GUIController && Viewport->GUIController->bActive && Viewport->GUIController->eventNeedsMenuResolution() )
	{
		if( (NewX < 640) || (NewY < 480 ) )
		{
			debugf(NAME_DevD3D,TEXT("clamping"));
			NewX		= 640;
			NewY		= 480;
			bSaveSize	= 0;
		}
	}
#endif

	debugf(NAME_DevD3D,TEXT("Enter SetRes: %dx%d Fullscreen %d"), NewX, NewY, Fullscreen );

	Flush(Viewport);

#ifndef _XBOX//jg -- Removed all this code because devices are created at startup

	// Only allow 16 bit color on 3dfx cards.
	if( Is3dfx )
		ColorBytes = 2;

	switch( ColorBytes )
	{
	case 0:
		break;
	case 2:
		Use16bit = 1;
		break;
	case 4:
		Use16bit = 0;
	}
	INT NewColorBits = (Use16bit && !GIsEditor) ? 16 : 32;

	// Force same color depth as desktop in windowed mode.
	if( !Fullscreen && (NewColorBits != DesktopColorBits) )
	{
		Use16bit		= DesktopColorBits == 16;
		NewColorBits	= DesktopColorBits;
		bSaveSize		= 0;
	}

	//!!vogel: always recreate the device
#if 0
	// See if anything has changed which warrants recreating the device.
	if( ForceReset				||
		Direct3DDevice8==NULL	|| 
		Fullscreen				||
		CurrentFullscreen )
#endif
	{
		ForceReset = 0;

		// Enumerate device display modes.
		guard(EnumDisplayModes);

		DisplayModes.Empty(Direct3D8->GetAdapterModeCount(BestAdapter));

		for(DWORD Index = 0;Index < Direct3D8->GetAdapterModeCount(BestAdapter);Index++)
		{
			D3DDISPLAYMODE	DisplayMode;

			Direct3D8->EnumAdapterModes(BestAdapter,Index,&DisplayMode);

			DisplayModes.AddItem(DisplayMode);
#ifdef _XBOX
			//debugf(NAME_DevD3D,TEXT("%ux%u (%uHz) [%i])"), DisplayMode.Width, DisplayMode.Height, DisplayMode.RefreshRate, DisplayMode.Flags);
#endif	
		}

		unguard;

		// See if the window is full screen.
		INT FullScreenWidth	 = Max(NewX, 1);
		INT FullScreenHeight = Max(NewY, 1);
		D3DFORMAT		AdapterFormat;
		D3DDISPLAYMODE	DisplayMode;

		if(Fullscreen)
		{
			if(DisplayModes.Num()==0 )
				return UnSetRes(TEXT("No fullscreen display modes found"),0,1);

			// Find matching display mode.
			guard(FindBestMatchingMode);

			INT	BestMode  = 0,
				BestError = MAXINT;

			DWORD RefreshRate = ((DesiredRefreshRate <= 60) && !OverrideDesktopRefreshRate) ? 0 : DesiredRefreshRate;

			for(INT Index = 0;Index < DisplayModes.Num();Index++)
			{
				INT ThisError
				=	Abs((INT)DisplayModes(Index).Width - (INT)NewX)
				+	Abs((INT)DisplayModes(Index).Height- (INT)NewY)
				+	Abs((INT)GetFormatBPP(DisplayModes(Index).Format)-(INT)(NewColorBits));

				if((ThisError < BestError || (ThisError == BestError && (!RefreshRate || DisplayModes(Index).RefreshRate == RefreshRate))) && (GetFormatBPP(DisplayModes(Index).Format) == NewColorBits))
				{
					BestMode = Index;
					BestError = ThisError;
				}
			}

			if(BestError == MAXINT)
				return UnSetRes(TEXT("No acceptable display modes found"),0,1);

			// Use the best display mode
			DisplayMode		= DisplayModes(BestMode);
			NewColorBits	= GetFormatBPP(DisplayModes(BestMode).Format);
			NewX			= DisplayModes(BestMode).Width;
			NewY			= DisplayModes(BestMode).Height;

			FullScreenWidth  = Max(NewX, 1);
			FullScreenHeight = Max(NewY, 1);
			CurrentFullscreen = 1;

			AdapterFormat = DisplayModes(BestMode).Format;
			FullScreenRefreshRate = RefreshRate ? DisplayModes(BestMode).RefreshRate : D3DPRESENT_RATE_DEFAULT;

			debugf(NAME_DevD3D,TEXT("Best-match display mode: %ix%ix%i@%i"),DisplayModes(BestMode).Width,DisplayModes(BestMode).Height,GetFormatBPP(DisplayModes(BestMode).Format),DisplayModes(BestMode).RefreshRate);

			unguard;
		}
		else
		{
			guard(GetCurrentMode);

			Result = Direct3D8->GetAdapterDisplayMode(BestAdapter,&DisplayMode);

			if( FAILED(Result) )
				return UnSetRes(TEXT("GetAdapterDisplayMode failed."),Result,1);

			AdapterFormat = DisplayMode.Format;
	
			if( GIsEditor )
			{
				FullScreenWidth  = DisplayMode.Width;
				FullScreenHeight = DisplayMode.Height;
			}

			CurrentFullscreen	= 0;
			//NewColorBits		= GetFormatBPP(AdapterFormat);
			//debugf(NAME_DevD3D,TEXT("Current display mode is %d bits (%d)"), NewColorBits, AdapterFormat);
			unguard;
		}

		// Setup the presentation parameters.
		guard(SetupPresentParms);
		appMemzero(&PresentParms,sizeof(PresentParms));
		PresentParms.Flags					= D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
		PresentParms.Windowed				= !Fullscreen;
		PresentParms.hDeviceWindow			= (HWND)Viewport->GetWindow();
		PresentParms.SwapEffect				= D3DSWAPEFFECT_DISCARD;//Fullscreen ? D3DSWAPEFFECT_DISCARD : D3DSWAPEFFECT_COPY;
		PresentParms.BackBufferWidth		= FullScreenWidth;
		PresentParms.BackBufferHeight		= FullScreenHeight;
		PresentParms.EnableAutoDepthStencil = TRUE;
#ifndef _XBOX
		PresentParms.BackBufferCount = (Fullscreen && UseTripleBuffering) ? 2 : 1;
		PresentParms.FullScreen_RefreshRateInHz	= Fullscreen ? FullScreenRefreshRate : 0;
		PresentParms.FullScreen_PresentationInterval = Fullscreen ? (UseVSync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE) : D3DPRESENT_INTERVAL_DEFAULT;
#else		
		D3DXSetDXT3DXT5( TRUE );  // -- we never use DXT2/4
		PresentParms.FullScreen_PresentationInterval = UseVSync ? D3DPRESENT_INTERVAL_ONE :		
			//scion jg -- Changed this on a PIX recommendation
			D3DPRESENT_INTERVAL_ONE_OR_IMMEDIATE;
		PresentParms.Flags |= DisplayMode.Flags;
		PresentParms.BackBufferCount = 1;
		PresentParms.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
		if( FullScreenWidth == 640 && UseXBoxFSAA )
			PresentParms.MultiSampleType = D3DMULTISAMPLE_2_SAMPLES_MULTISAMPLE_QUINCUNX;
		else
			PresentParms.MultiSampleType = D3DMULTISAMPLE_NONE;
#if PERFORMANCE_STATS
		PresentParms.MultiSampleType = D3DMULTISAMPLE_NONE;
		PresentParms.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;		
#endif		
#endif

		guard(DetermineFormats);

		// Determine which back buffer format to use.
		// scion sz -- D3DFMT_A8R8G8B8 is used instead of D3DFMT_X8R8G8B8 in order to support 
		// emissive modulation with frame buffer alpha values.  Alpha writes should be disabled
		// for everything except emissive materials.
#ifdef _XBOX
		BackBufferFormat = NewColorBits == 32 ? D3DFMT_A8R8G8B8 : D3DFMT_R5G6B5;
#else
		BackBufferFormat = NewColorBits == 32 ? D3DFMT_X8R8G8B8 : D3DFMT_R5G6B5;
#endif


		Result = Direct3D8->CheckDeviceFormat(BestAdapter,D3DDEVTYPE_HAL,AdapterFormat,D3DUSAGE_RENDERTARGET,D3DRTYPE_SURFACE,BackBufferFormat);
		if( FAILED(Result) )
			return UnSetRes(TEXT("CheckDeviceFormat failed"),Result,1);
	
		if(Fullscreen)
			AdapterFormat = BackBufferFormat;

		CurrentColorBytes = NewColorBits / 8;

		PresentParms.BackBufferFormat = BackBufferFormat;

		debugf(NAME_DevD3D,TEXT("Using back-buffer format %u(%u-bit)"),BackBufferFormat,GetFormatBPP(BackBufferFormat));

		// Determine which depth buffer format to use.
#ifdef _XBOX
        DepthBufferFormat = D3DFMT_D24S8;
#else
		DepthBufferFormat = (Use16bit && !GIsEditor) ? D3DFMT_D16 : (UseStencil || GIsEditor) ? D3DFMT_D24S8 : D3DFMT_D24X8;
		INT Tries = 0;
#endif
		while( 
			FAILED(Direct3D8->CheckDeviceFormat(BestAdapter,D3DDEVTYPE_HAL,AdapterFormat,D3DUSAGE_DEPTHSTENCIL,D3DRTYPE_SURFACE,DepthBufferFormat)) 
		||	FAILED(Direct3D8->CheckDepthStencilMatch(BestAdapter,D3DDEVTYPE_HAL,AdapterFormat,BackBufferFormat,DepthBufferFormat))
		)
		{
#ifndef _XBOX
			Tries++;

			if( Tries == 3 )
				return UnSetRes(TEXT("CheckDepthStencilMatch failed."),0,1);

			if( UseStencil )
			{
				if( Tries == 1 )
				{
					// Kyros don't support 8 bit stencil.
					DepthBufferFormat = D3DFMT_D24X4S4;
				}
				else
				{
					// give up on stencil
					DepthBufferFormat = D3DFMT_D24X8;
				}				
			}
			else
			{
				if( Tries == 1 )
				{
					// Xabre & Kyro e.g. only support D32.
					DepthBufferFormat = D3DFMT_D32;
				}
				else
				{
					// TNT2 only supports D24S8 in 32 bit windowed mode.
					// Parhelia doesn't expose any non stencil formats in 32 bit.
					DepthBufferFormat = D3DFMT_D24S8;
				}
			}
#else
			return UnSetRes(TEXT("CheckDepthStencilMatch failed"),0,1);
#endif
		}
				
		debugf(NAME_DevD3D,TEXT("Using depth-buffer format %u(%u-bit)"),DepthBufferFormat,GetFormatBPP(DepthBufferFormat));

		PresentParms.AutoDepthStencilFormat = DepthBufferFormat;

		unguard;
		unguard;

		if( Direct3DDevice8 != NULL )
		{
			guard(ResetDevice);
#ifndef _XBOX
			// Present a black screen while precaching.
			if( 0 )//!GIsEditor )
			{
				Direct3DDevice8->Clear( 0, NULL, D3DCLEAR_TARGET, 0, 0, 0 );
				Direct3DDevice8->Present( NULL, NULL, NULL, NULL );
			}
#endif

			// Recreate for the game and use Reset for the Editor.
			if( !GIsEditor || (GIsEditor && FAILED(Direct3DDevice8->Reset(&PresentParms))) )
			{
				Direct3DDevice8->Release();
				Direct3DDevice8 = NULL;
			}

			unguard;
		}

		// Create D3D device.
		if( Direct3DDevice8 == NULL )
		{
			guard(CreateDevice);
			debugf(NAME_DevD3D,TEXT("Creating device"));

#ifndef _XBOX
			if( (DeviceCaps8.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) == 0 )
			UseHardwareTL = false;
	
			if( (((DeviceCaps8.VertexShaderVersion) & 0xFF00) < 0x0100) || !UseHardwareTL ) 
			UseHardwareVS = false;
#endif
#ifndef _XBOX				
			DWORD BehaviorFlags = D3DCREATE_FPU_PRESERVE | (UseHardwareVS ? /*D3DCREATE_PUREDEVICE |*/ D3DCREATE_HARDWARE_VERTEXPROCESSING : UseHardwareTL ? D3DCREATE_MIXED_VERTEXPROCESSING : D3DCREATE_SOFTWARE_VERTEXPROCESSING);
#else
			//!!vogel: Xbox doesn't need FPU_PRESERVE as runtime doesn't fiddle with precision.
			//DWORD BehaviorFlags = D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE;
			DWORD BehaviorFlags = D3DCREATE_HARDWARE_VERTEXPROCESSING;
#endif

#ifndef _XBOX				
			if( ( UseHardwareVS == false) && ( UsePCSkeletalShader ) )
			{
				UsePCSkeletalShader = false;
				debugf(NAME_DevD3D,TEXT("Can't use PC Skeletal Shader if  UseHardwareVS  is false."));
			}
			GHardwareSkinning =  UsePCSkeletalShader;  // Allow GPU vertex streams in the renderer.
#else
			if( ( UseHardwareVS == false) && ( UseXBOXSkeletalShader ) )
			{
				UseXBOXSkeletalShader = false;
				debugf(NAME_DevD3D,TEXT("Can't use XBOX Skeletal Shader if  UseHardwareVS  is false."));
			}
			GHardwareSkinning =  UseXBOXSkeletalShader;  // Allow GPU vertex streams in the renderer.
#endif

			D3DDEVTYPE DevType;
#ifndef _XBOX
			if( Viewport->UseSoftwareRasterizer )
				DevType = D3DDEVTYPE_REF;
			else
#endif
				DevType = D3DDEVTYPE_HAL;


			// -- remove old default render target before (re)creating the device
			if( DefaultRenderTarget )
			{
				delete DefaultRenderTarget;
				DefaultRenderTarget=NULL;
			}

#ifdef _XBOX
			//scion jg -- The push buffer needs to be bigger to support our
			// various BeginPush/EndPush operations
			//TODO jg -- Tune the size and the kick off size
			Direct3D8->SetPushBufferSize(1024 * 1024,32 * 1024);
			//scion jg -- Check for 480p support and enable it if there
			if (XGetVideoFlags() & XC_VIDEO_FLAGS_HDTV_480p)
			{
				PresentParms.BackBufferWidth = 640;
				PresentParms.BackBufferHeight = 480;
				PresentParms.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER |
					D3DPRESENTFLAG_10X11PIXELASPECTRATIO |
					D3DPRESENTFLAG_PROGRESSIVE;
				debugf(NAME_DevD3D,TEXT("Running in 480p 640x480"));
			}
			// scion sz - set widescreen if needed
			if( XGetVideoFlags() & XC_VIDEO_FLAGS_WIDESCREEN )
			{
				PresentParms.Flags |= D3DPRESENTFLAG_WIDESCREEN;
			}
#endif

			// Create the device.
			Result = Direct3D8->CreateDevice(BestAdapter,DevType,(HWND)Viewport->GetWindow(),BehaviorFlags,&PresentParms,&Direct3DDevice8);

			if(Result != D3D_OK)
				return UnSetRes(TEXT("CreateDevice failed"),Result,1);
			unguard;

			debugf(NAME_DevD3D,TEXT("D3D Driver: CreateDevice: will use %s transform and lighting."),UseHardwareTL ? TEXT("hardware"):TEXT("software"));
			debugf(NAME_DevD3D,TEXT("D3D Driver: CreateDevice: will use %s vertex processing"), UseHardwareTL ? UseHardwareVS ? TEXT("hardware"):TEXT("mixed") : TEXT("software"));

			// keep track of the default render target surfaces
			if( !GIsEditor )
			{				
				DefaultRenderTarget = (FDefaultRenderTarget*) NEW FDefaultRenderTarget( this );
				RenderInterface.SavedRenderTargetState.GetCurrentState()->RenderTarget = DefaultRenderTarget;
			}			

#ifndef _XBOX
			// Present a black screen while precaching.
			if( !GIsEditor )
			{
				Direct3DDevice8->Clear( 0, NULL, D3DCLEAR_TARGET, 0, 0, 0 );
				Direct3DDevice8->Present( NULL, NULL, NULL, NULL );
			}
#endif
		}
#else
		//jg -- Change due to startup device creation
		extern IDirect3D8* GD3D;
		extern IDirect3DDevice8* GD3DDevice;
		check(GD3D && GD3DDevice);
		// Assign from the globals
		Direct3D8 = GD3D;
		Direct3DDevice8 = GD3DDevice;
		GHardwareSkinning = 1;
		CurrentFullscreen = 1;
		CurrentColorBytes = 4;
		FullScreenWidth = 640;
		FullScreenHeight = 480;
		// keep track of the default render target surfaces
		if( DefaultRenderTarget == NULL )
		{				
			DefaultRenderTarget = (FDefaultRenderTarget*) NEW FDefaultRenderTarget( this );
			RenderInterface.SavedRenderTargetState.GetCurrentState()->RenderTarget = DefaultRenderTarget;
		}
	{
#endif

#ifdef _XBOX
        InitialTextureMemory = -1;
#else
		InitialTextureMemory = Direct3DDevice8->GetAvailableTextureMem();
#endif

		// Init render states.
		guard(InitRenderState);
		{
			Direct3DDevice8->SetRenderState( D3DRS_ZENABLE, TRUE );
			Direct3DDevice8->SetRenderState( D3DRS_SHADEMODE, D3DSHADE_GOURAUD );
			Direct3DDevice8->SetRenderState( D3DRS_SPECULARENABLE, FALSE );
			Direct3DDevice8->SetRenderState( D3DRS_DITHERENABLE, TRUE );
			Direct3DDevice8->SetRenderState( D3DRS_NORMALIZENORMALS, TRUE );		
			Direct3DDevice8->SetRenderState( D3DRS_RANGEFOGENABLE, UseRangeFog ? TRUE : FALSE );
#ifdef _XBOX
            Direct3DDevice8->SetRenderState( D3DRS_FOGTABLEMODE, D3DFOG_LINEAR );
#else
			if( UseVertexFog )
				Direct3DDevice8->SetRenderState( D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR );
			else
				Direct3DDevice8->SetRenderState( D3DRS_FOGTABLEMODE, D3DFOG_LINEAR );
#endif

#ifndef _XBOX
			// Doesn't have a performance impact on T&L cards but needed for e.g. Kyro cards.
			Direct3DDevice8->SetRenderState( D3DRS_CLIPPING, TRUE );
#endif
			D3DMATERIAL8	Material8;

			appMemzero(&Material8,sizeof(Material8));

			Material8.Ambient.r = 1.0f;
			Material8.Ambient.g = 1.0f;
			Material8.Ambient.b = 1.0f;
			Material8.Ambient.a = 1.0f;

			Material8.Diffuse.r = 1.0f;
			Material8.Diffuse.g = 1.0f;
			Material8.Diffuse.b = 1.0f;
			Material8.Diffuse.a = 1.0f;

			Material8.Specular.r = 1.0f;
			Material8.Specular.g = 1.0f;
			Material8.Specular.b = 1.0f;
			Material8.Specular.a = 1.0f;
			
			Material8.Power = 0.0f;

			Direct3DDevice8->SetMaterial(&Material8);

			Direct3DDevice8->SetRenderState(D3DRS_EMISSIVEMATERIALSOURCE,D3DMCS_COLOR1);
			Direct3DDevice8->SetRenderState(D3DRS_SPECULARMATERIALSOURCE,D3DMCS_MATERIAL);

			if( !(DeviceCaps8.VertexProcessingCaps & D3DVTXPCAPS_MATERIALSOURCE7) )
			{
				debugf(NAME_DevD3D,TEXT("D3D Device: Device doesn't support D3DVTXPCAPS_MATERIALSOURCE7"));
				Direct3DDevice8->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE,D3DMCS_MATERIAL);
				Direct3DDevice8->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE,D3DMCS_MATERIAL);
			}
			else
			{
				Direct3DDevice8->SetRenderState(D3DRS_DIFFUSEMATERIALSOURCE,D3DMCS_COLOR2);
				Direct3DDevice8->SetRenderState(D3DRS_AMBIENTMATERIALSOURCE,D3DMCS_COLOR2);
			}
		}
		unguard;

		UsedLevelOfAnisotropy = Max<DWORD>( 1, Min<DWORD>( DeviceCaps8.MaxAnisotropy, LevelOfAnisotropy ) );

		// Init texture stage state.
		guard(InitTextureStageState);
		{
			//!!MAT
			FLOAT LodBias = DefaultTexMipBias;
			for( INT Stage=0; Stage<((INT)DeviceCaps8.MaxTextureBlendStages); Stage++ )
			{
				Direct3DDevice8->SetTextureStageState( Stage, D3DTSS_MIPMAPLODBIAS, *(DWORD*)&LodBias );
				Direct3DDevice8->SetTextureStageState( Stage, D3DTSS_MAGFILTER, UsedLevelOfAnisotropy > 1 ? D3DTEXF_ANISOTROPIC : D3DTEXF_LINEAR );
				Direct3DDevice8->SetTextureStageState( Stage, D3DTSS_MINFILTER, UsedLevelOfAnisotropy > 1 ? D3DTEXF_ANISOTROPIC : D3DTEXF_LINEAR );
				Direct3DDevice8->SetTextureStageState( Stage, D3DTSS_MIPFILTER, UseMipmapping ? (UseTrilinear ? D3DTEXF_LINEAR : D3DTEXF_POINT) : D3DTEXF_NONE );
				Direct3DDevice8->SetTextureStageState( Stage, D3DTSS_MAXANISOTROPY, UsedLevelOfAnisotropy );
			}
		}
		unguard;
	}

	// resize the viewport.
	verify(Viewport->ResizeViewport( (Fullscreen ? BLIT_Fullscreen : 0) | BLIT_Direct3D, NewX, NewY, bSaveSize ));

	// Init deferred state.
	guard(InitDeferredState);
#ifdef _XBOX
	extern UBOOL IsPlayingMovie(void);
	//jg -- Don't do this if the startup movie is playing
	if (IsPlayingMovie() == FALSE)
	{
		//scion jg -- Use common method to do this
		ResetRenderStates();
	}
#else
	ResetRenderStates();
#endif

	unguard;

#ifndef _XBOX
	if( GIsEditor )
	{
		hWnd	= (HWND) Viewport->GetWindow();
		hDC		= GetDC( hWnd );
	}
	else
	{
		hWnd	= NULL;
		hDC		= NULL;
	}
#endif

	UpdateGamma(Viewport);

	Viewport->PendingFrame = 0;

	return 1;

	unguard;
}

//
//	UD3DRenderDevice::UnSetRes
//
UBOOL UD3DRenderDevice::UnSetRes( const TCHAR* Msg, HRESULT h, UBOOL Fatal )
{
	guard(UD3DRenderDevice::UnSetRes);
	if( Msg )
		debugf(NAME_DevD3D,TEXT("%s (%s)"),Msg,*D3DError(h));
	if( Fatal )
	{
		if( Msg )
			appErrorf(TEXT("Error setting display mode: %s (%s). Please delete your avaris.ini file if this error prevents you from starting the game."),Msg,*D3DError(h));
		else
			appErrorf(TEXT("Error setting display mode. Please delete your avaris.ini file."));
	}
	return 0;
	unguard;
}

//
//	UD3DRenderDevice::Flush
//
void UD3DRenderDevice::Flush(UViewport* Viewport)
{
	guard(UD3DRenderDevice::Flush);

	guard(StateClearing);
#ifdef _XBOX
	extern UBOOL IsPlayingMovie(void);
	//scion jg -- We need to flush the render states so that SwitchTexture()
	// doesn't screw up due to a linear texture being in a stage after flushing
	if (Direct3DDevice8 != NULL && IsPlayingMovie() == FALSE)
#else
	if (Direct3DDevice8 != NULL)
#endif
	{
		// Reset the render states
		ResetRenderStates();
	}
	unguard;

	DynamicVertexStream = NULL;
	DynamicIndexBuffer16 = NULL;
	DynamicIndexBuffer32 = NULL;

	guard(ResourceList);
	while(ResourceList)
	{
		//scion jg -- Flushing also deletes the resource
		if (ResourceList->CacheId != INVALID_CACHEID)
		{
			FlushResource(ResourceList);
		}
		else
		{
			// Not cached in anything so safe to delete
			delete ResourceList;
		}
	}
	unguard;

	//scion jg -- Reset the default render target
	guard(DefRenTarget);
	if (DefaultRenderTarget != NULL)
	{
		DefaultRenderTarget->ResetCacheIDs();
	}
	unguard;

	guard(ResourceCache);
	//scion jg -- Clean out all of the caches
	TextureCache.Reset();
	VertexBufferCache.Reset();
	IndexBufferCache.Reset();
	unguard;

#ifndef FINAL_RELEASE
	// scion sz - reset mem counts for UTextures
	// since all the cached data has been removed
	for( TObjectIterator<UTexture> It; It; ++It )
	{
		It->MemUsage = 0;
	}
#endif

	// scion sz
	// no need to clear out combined vertex shader microcodes
	// they should persist.  They get cleaned up in UD3DRenderDevice::Destroy
	// 

	if( !Viewport ||
		Viewport->GetOuterUClient()->Viewports.Num() == 0 )
	{
		// scion sz 
		guard(VertexShaders);
		while(VertexShaders)
			delete VertexShaders;
		unguard;

		// scion sz
		// cleanup pixel shader microcode
		guard(PixelShaders);
		for( INT i=0; i<PixelShaders.Num(); i++ )
		{
			if( PixelShaders(i) )
			{
				delete PixelShaders(i);
				PixelShaders(i)=NULL;
			}
		}
		unguard;
	}

	if( Viewport && 
		Viewport->GetViewportIndex()==0 )
	{
		// clear pending occlusion queries
		for( INT vpIdx=0; vpIdx < Viewport->GetOuterUClient()->Viewports.Num(); vpIdx++ )
		{
            UViewport* pView = Viewport->GetOuterUClient()->Viewports(vpIdx);
			ClearQueriesForViewport( pView );
		}
		// free the global hw occlusion pool items
		FlushHWOcclusionQueryPool();
	}

	PrecacheOnFlip = UsePrecaching;

	unguard;
}

//
//	UD3DRenderDevice::UpdateGamma
//
void UD3DRenderDevice::UpdateGamma(UViewport* Viewport)
{
	guard(UD3DRenderDevice::UpdateGamma);

	if( ParseParam(appCmdLine(),TEXT("NOGAMMA")) )
		return;

	FLOAT	Gamma = Viewport->GetOuterUClient()->Gamma,
			Brightness = Viewport->GetOuterUClient()->Brightness,
			Contrast = Viewport->GetOuterUClient()->Contrast;

#ifdef _XBOX
	if( Direct3DDevice8)
	{
		// XBox difference: BYTE ranges, rather than _WORDs.
		D3DGAMMARAMP	Ramp;
		for(INT i=0; i<256; i++)
			Ramp.red[i] = Ramp.green[i] = Ramp.blue[i] = Clamp<INT>( appRound( (Contrast+0.5f)*appPow(i/255.f,1.0f/Gamma)*255.f + (Brightness-0.5f)*128.f - Contrast*128.f + 64.f ), 0, 255 );
		Direct3DDevice8->SetGammaRamp(D3DSGR_CALIBRATE,&Ramp);
	}
#else
	D3DGAMMARAMP	Ramp;
	for(INT i=0; i<256; i++)
		Ramp.red[i] = Ramp.green[i] = Ramp.blue[i] = Clamp<INT>( appRound( (Contrast+0.5f)*appPow(i/255.f,1.0f/Gamma)*65535.f + (Brightness-0.5f)*32768.f - Contrast*32768.f + 16384.f ), 0, 65535 );

	if( !GIsEditor && Direct3DDevice8 && (DeviceCaps8.Caps2 & D3DCAPS2_FULLSCREENGAMMA) )
		Direct3DDevice8->SetGammaRamp(D3DSGR_CALIBRATE,&Ramp);
	else
		SetDeviceGammaRamp( hDC, &Ramp );
#endif

	unguard;
}

//
//	UD3DRenderDevice::RestoreGamma
//
void UD3DRenderDevice::RestoreGamma()
{
	guard(UD3DRenderDevice::RestoreGamma);

	if( ParseParam(appCmdLine(),TEXT("NOGAMMA")) )
		return;

	D3DGAMMARAMP	Ramp;

	for(INT ColorIndex = 0;ColorIndex < 256;ColorIndex++)
		Ramp.red[ColorIndex] = Ramp.green[ColorIndex] = Ramp.blue[ColorIndex] = ColorIndex << 8;

	if( !GIsEditor && Direct3DDevice8 && (DeviceCaps8.Caps2 & D3DCAPS2_FULLSCREENGAMMA) )
		Direct3DDevice8->SetGammaRamp(D3DSGR_CALIBRATE,&Ramp);		
#ifndef _XBOX
	else
//		SetDeviceGammaRamp( hDC, &Ramp );
		SetDeviceGammaRamp( GetDC( GetDesktopWindow() ), &Ramp );
#endif
	unguard;
}

//
//	UD3DRenderDevice::Exec
//
UBOOL UD3DRenderDevice::Exec(const TCHAR* Cmd,FOutputDevice& Ar)
{
	guard(UD3DRenderDevice::Exec);

	if(ParseCommand(&Cmd,TEXT("DUMPRESOURCEHASH")))
	{
		//scion jg -- Changed for the new hashing scheme
		Ar.Log(TEXT("Texture Cache:"));
		TextureCache.DumpHashInfo(Ar);
		Ar.Log(TEXT(""));
		Ar.Log(TEXT("Vertex Buffer Cache:"));
		VertexBufferCache.DumpHashInfo(Ar);
		Ar.Log(TEXT(""));
		Ar.Log(TEXT("Index Buffer Cache:"));
		IndexBufferCache.DumpHashInfo(Ar);
		Ar.Log(TEXT(""));
		return 1;
	}
	else if(ParseCommand(&Cmd,TEXT("FIRSTCOLOREDMIP")))
	{
		if (appStrcmp(Cmd,TEXT("")) != 0) 
			FirstColoredMip = appAtoi(Cmd);
		else
			FirstColoredMip = 255;
		Flush(NULL);
		return 1;
	}
	else if(ParseCommand(&Cmd,TEXT("NEARCLIP")))
	{
		if (appStrcmp(Cmd,TEXT("")) != 0) 
			NEAR_CLIPPING_PLANE = appAtof(Cmd);
		return 1;
	}
#ifdef _XBOX
	//scion jg -- Do a screen shot using the xbox API
	else if (ParseCommand(&Cmd,TEXT("SHOT")))
	{
		static BitmapIndex = 0;
		IDirect3DSurface8* pBackbuffer = NULL;
		// Get the back buffer as a surface
		Direct3DDevice8->GetBackBuffer(0,0,&pBackbuffer);
		if (pBackbuffer != NULL)
		{
			// Now write that surface out to the specified file
			XGWriteSurfaceToFile(pBackbuffer,(LPCSTR)(TCHAR_TO_ANSI(
				*FString::Printf(TEXT("D:\\system\\frame%05d.bmp"),
				BitmapIndex))));
			pBackbuffer->Release();
			BitmapIndex++;
		}
		return 1;
	}
	//jg -- Create the icon for saved games
	else if (ParseCommand(&Cmd,TEXT("EXPORTSAVEGAMEIMAGE")))
	{
		debugf(TEXT("Deprecated do not call"));
		return 1;
	}
#endif
	else if( ParseCommand(&Cmd,TEXT("D3DRESOURCES") ))
	{
		// Calculate resource usage
		INT StatTextureBytes=0, StatVertexStreamBytes=0, StatIndexBufferBytes=0, StatOtherBytes=0;
		INT StatNumTextures=0, StatNumVertexStreams=0, StatNumIndexBuffers=0, StatNumOther=0;
		for(FD3DResource* Resource = ResourceList;Resource;Resource = Resource->NextResource)
		{
			if(Resource->GetTexture())
			{
				StatTextureBytes += Resource->CalculateFootprint();
				StatNumTextures++;
			}
			else if(Resource->GetVertexStream())
			{
				StatVertexStreamBytes += Resource->CalculateFootprint();
				StatNumVertexStreams++;
			}
			else if(Resource->GetIndexBuffer())
			{
				StatIndexBufferBytes += Resource->CalculateFootprint();
				StatNumIndexBuffers++;
			}
			else
			{
				StatOtherBytes += Resource->CalculateFootprint();
				StatNumOther++;
			}
		}

#ifndef _XBOX
		DWORD AllocKBytes = (InitialTextureMemory - Direct3DDevice8->GetAvailableTextureMem()) / 1024;
#endif

		Ar.Logf( TEXT("Direct3D Resource Usage"));
		Ar.Logf( TEXT(""));
		Ar.Logf( TEXT("Resource Type      Count   Total Bytes"));
		Ar.Logf( TEXT("--------------------------------------"));
		Ar.Logf( TEXT("Textures            %4d      %8d"), StatNumTextures, StatTextureBytes );
		Ar.Logf( TEXT("Vertex Streams      %4d      %8d"), StatNumVertexStreams, StatVertexStreamBytes );
		Ar.Logf( TEXT("Index Buffers       %4d      %8d"), StatNumIndexBuffers, StatIndexBufferBytes );
		Ar.Logf( TEXT("Other               %4d      %8d"), StatNumOther, StatOtherBytes );
#ifndef _XBOX
		Ar.Logf( TEXT("") );
		Ar.Logf( TEXT("Allocated D3D resources account for %d KBytes"), AllocKBytes );
#endif
		Ar.Logf( TEXT("") );

		debugf( TEXT("Direct3D Resource Usage"));
		debugf( TEXT(""));
		debugf( TEXT("Resource Type      Count   Total Bytes"));
		debugf( TEXT("--------------------------------------"));
		debugf( TEXT("Textures            %4d      %8d"), StatNumTextures, StatTextureBytes );
		debugf( TEXT("Vertex Streams      %4d      %8d"), StatNumVertexStreams, StatVertexStreamBytes );
		debugf( TEXT("Index Buffers       %4d      %8d"), StatNumIndexBuffers, StatIndexBufferBytes );
		debugf( TEXT("Other               %4d      %8d"), StatNumOther, StatOtherBytes );
#ifndef _XBOX
		debugf( TEXT("") );
		debugf( TEXT("Allocated D3D resources account for %d KBytes"), AllocKBytes );
#endif
		debugf( TEXT("") );


		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("SUPPORTEDRESOLUTION")) )
	{
		INT		Width		= 0,
				Height		= 0,
				BitDepth 	= 0;
		UBOOL	Supported 	= 0;

		if(Parse(Cmd,TEXT("WIDTH="),Width) && Parse(Cmd,TEXT("HEIGHT="),Height) && Parse(Cmd,TEXT("BITDEPTH="),BitDepth))
		{
			for(INT ModeIndex = 0;ModeIndex < DisplayModes.Num();ModeIndex++)
			{
				D3DDISPLAYMODE&	DisplayMode = DisplayModes(ModeIndex);

				if(DisplayMode.Width == Width && DisplayMode.Height == Height && GetFormatBPP(DisplayMode.Format) == BitDepth)
				{
					Supported = 1;
					break;
				}
			}
		}

		Ar.Logf(TEXT("%u"),Supported);

		return 1;
	}
#ifdef _XBOX
#ifndef FINAL_RELEASE
	//scion jg -- This command enables the single step pusher, which causes
	// everything that is written to the push buffer to be checked for
	// correctness. This helps find GPU crash errors.
	else if(ParseCommand(&Cmd,TEXT("SINGLESTEPPUSHER")))
	{
#ifdef _DEBUG
		extern UBOOL D3D__SingleStepPusher;
		D3D__SingleStepPusher = UseSSP == 0 ? 2 : 0;
#endif
		UseSSP = !UseSSP;
		return 1;
	}
#endif
#endif
	return 0;
	unguard;
}

//
//	UD3DRenderDevice::Lock
//
FRenderInterface* UD3DRenderDevice::Lock(UViewport* Viewport,BYTE* InHitData,INT* InHitSize)
{
	guard(UD3DRenderDevice::Lock);

	FUNC_CLOCK(LockCyc,GStats.DWORDStats(D3DStats.STATS_LockCycles));

	FrameCounter++;

#ifndef _XBOX
	// Check cooperative level.
	HRESULT hr=NULL;
	guard(RecreateDevice);
	if( Direct3DDevice8 == NULL )
	{
		// This can happen when coming out of fullscreen: CreateDevice fails because fullscreen colordepth
		// isn't available in windowed mode.
		ForceReset = 1;
		if( !SetRes(Viewport, Viewport->SizeX, Viewport->SizeY, CurrentFullscreen) )
			appErrorf(TEXT("Failed resetting mode"));				
	}
	unguard;
	guard(TestCooperativeLevel);
	hr=Direct3DDevice8->TestCooperativeLevel();
	unguard;
	if( FAILED(hr) )
	{
		//debugf(NAME_DevD3D,TEXT("TestCooperativeLevel failed (%s)"),*D3DError(hr));

		guard(HandleBigChange);
		// D3DERR_DEVICELOST is returned if the device was lost, but exclusive mode isn't available again yet.
		// D3DERR_DEVICENOTRESET is returned if the device was lost, but can be reset.

		// Can't render at this time ?
		if( hr == D3DERR_DEVICELOST ) 
			return NULL;

		if( hr == D3DERR_DEVICENOTRESET )
		{
			ForceReset = 1;
			if( !SetRes(Viewport, Viewport->SizeX, Viewport->SizeY, CurrentFullscreen) )
				appErrorf(TEXT("Failed resetting mode"));				
		}
		unguard;
	}
#endif

	LockedViewport = Viewport;

	// Set viewport.
	guard(SetViewport);
	D3DVIEWPORT8	ViewportInfo;
	
	ViewportInfo.Width      = Viewport->SizeX;
	ViewportInfo.Height     = Viewport->SizeY;

#ifndef _XBOX
	if( !CurrentFullscreen )
	{
		RECT ClientRect;
		GetClientRect( (HWND)Viewport->GetWindow(), &ClientRect );
		ViewportInfo.Width      = ClientRect.right;
		ViewportInfo.Height     = ClientRect.bottom;
	}
#endif
	
	ViewportInfo.X          = Viewport->OffsetX;	// scion sz - (splitscreen)
	ViewportInfo.Y          = Viewport->OffsetY;
	ViewportInfo.MinZ       = 0.0;
	ViewportInfo.MaxZ       = 1.0;

#ifdef _XBOX//jg -- We know the size
	ViewportInfo.Width		= 640;
	ViewportInfo.Height		= 480;
#else
	ViewportInfo.Width		= Min<DWORD>( ViewportInfo.Width,	PresentParms.BackBufferWidth	);
	ViewportInfo.Height		= Min<DWORD>( ViewportInfo.Height,	PresentParms.BackBufferHeight	);
#endif

	if( FAILED(Direct3DDevice8->SetViewport(&ViewportInfo) ) )
	{
		LockedViewport = NULL;
		return NULL;
	}

	unguard;

	// Begin scene.
	guard(BeginScene);

	HRESULT	Result = Direct3DDevice8->BeginScene();

	if( FAILED(Result) )
	{
#ifndef _XBOX
//		if( ++FailCount==1 )
//			goto Failed;
#endif
		appErrorf(TEXT("BeginScene failed (%s)"),*D3DError(Result));
	}

	unguard;

	// Create the render interface.
	RenderInterface.Locked( Viewport, InHitData, InHitSize );

	STAT_INC(GStats.DWORDStats(D3DStats.STATS_LockCalls));

	return &RenderInterface;

	unguard;
}

//
//	UD3DRenderDevice::Unlock
//
void UD3DRenderDevice::Unlock(FRenderInterface* RI)
{
	guard(UD3DRenderDevice::Unlock);

	FUNC_CLOCK(UnCyc,GStats.DWORDStats(D3DStats.STATS_UnlockCycles));

#ifndef _XBOX //scion jg -- Not used on xbox
	// Update resource stats.
	INT	NumTextures			= 0,
		TextureBytes		= 0,
		NumIndexBuffers		= 0,
		IndexBufferBytes	= 0,
		NumVertexStreams	= 0,
		VertexStreamBytes	= 0,
		ResourceBytes		= 0;

	// Iterate through resources collecting stats.
	for(FD3DResource* Resource = ResourceList;Resource;Resource = Resource->NextResource)
	{
		if(Resource->LastFrameUsed == FrameCounter)
		{
			if(Resource->GetTexture())
			{
				TextureBytes += Resource->CalculateFootprint();
				NumTextures++;
			}
			else if(Resource->GetVertexStream())
			{
				VertexStreamBytes += Resource->CalculateFootprint();
				NumVertexStreams++;
			}
			else if(Resource->GetIndexBuffer())
			{
				IndexBufferBytes += Resource->CalculateFootprint();
				NumIndexBuffers++;
			}
		}
	}

#ifndef _XBOX
	ResourceBytes = InitialTextureMemory - Direct3DDevice8->GetAvailableTextureMem();
#endif


	// Propagate to GStats.
	STAT_INC_BY(GStats.DWORDStats( D3DStats.STATS_NumTextures		),NumTextures);
	STAT_INC_BY(GStats.DWORDStats( D3DStats.STATS_TextureBytes		),TextureBytes);
	STAT_INC_BY(GStats.DWORDStats( D3DStats.STATS_NumIndexBuffers	),NumIndexBuffers);
	STAT_INC_BY(GStats.DWORDStats( D3DStats.STATS_IndexBufferBytes	),IndexBufferBytes);
	STAT_INC_BY(GStats.DWORDStats( D3DStats.STATS_VertexStreamBytes	),VertexStreamBytes);
	STAT_INC_BY(GStats.DWORDStats( D3DStats.STATS_ResourceBytes		),ResourceBytes);

	// Hit detection.
	FD3DRenderInterface*	D3DRI = (FD3DRenderInterface*) RI;
	if( D3DRI )
	{		
		check(D3DRI->HitStack.Num() == 0);

		if(D3DRI->HitSize)
			*D3DRI->HitSize = D3DRI->HitCount;
	}
#endif

	// Actual "unlock".
	RenderInterface.Unlocked();

	if( Direct3DDevice8 )
		Direct3DDevice8->EndScene();

	LockedViewport = NULL;

	STAT_INC(GStats.DWORDStats(D3DStats.STATS_UnlockCalls));
	unguard;
}

//
//  UD3DRenderDevice::Present
//

void UD3DRenderDevice::Present(UViewport* Viewport)
{
	guard(UD3DRenderDevice::Present);

	FUNC_CLOCK(PresCyc,GStats.DWORDStats(D3DStats.STATS_PresentCycles));

	if( Direct3DDevice8 )
	{

#ifndef _XBOX
	    // Stall HW to reduce mouse lag. (unless it's a Kyro)
	    if( ReduceMouseLag && !GIsBenchmarking && !IsKyro )
		    ReadPixels( (UViewport*) NULL, NULL );
#endif
    
		if( CurrentFullscreen || !Viewport )
			Direct3DDevice8->Present(NULL,NULL,NULL,NULL);
	    else
	    {
#ifdef _XBOX
			appErrorf( TEXT("DUMMY") );
#else
		    HWND hwnd = (HWND)Viewport->GetWindow();
		    RECT ClientRect;
    
		    //GetClientRect( hwnd, &ClientRect );
		    ClientRect.top		= 0;
		    ClientRect.left		= 0;
		    ClientRect.right	= Viewport->SizeX;
		    ClientRect.bottom	= Viewport->SizeY;
    
		    Direct3DDevice8->Present(&ClientRect,&ClientRect,hwnd,NULL);
#endif
		}
	}

	STAT_INC(GStats.DWORDStats(D3DStats.STATS_PresentCalls));
	unguard;
}

//
//	UD3DRenderDevice::ReadPixels
//
void UD3DRenderDevice::ReadPixels(UViewport* Viewport,FColor* Pixels)
{
	guard(UD3DRenderDevice::ReadPixels);

	// Retrieve the last back buffer.
	IDirect3DSurface8*	BackBuffer;

	HRESULT	Result = Direct3DDevice8->GetBackBuffer(0,D3DBACKBUFFER_TYPE_MONO,&BackBuffer);

	if( FAILED(Result) )
		appErrorf(TEXT("GetBackBuffer failed: %s"),*D3DError(Result));

	// Determine the format of the back buffer.
	D3DSURFACE_DESC	SurfaceDesc;

	Result = BackBuffer->GetDesc(&SurfaceDesc);

	if( FAILED(Result) )
		appErrorf(TEXT("GetDesc failed: %s"),*D3DError(Result));

	// Lock the back buffer.
	D3DLOCKED_RECT LockedRect;

	if( Viewport && Pixels )
	{	
		Result = BackBuffer->LockRect(&LockedRect,NULL,D3DLOCK_READONLY);
	}
	else
	{
		RECT DestRect;
		DestRect.top	= 0;
		DestRect.left	= 0;
		DestRect.bottom = 1;
		DestRect.right  = 1;
		Result = BackBuffer->LockRect(&LockedRect,&DestRect,D3DLOCK_READONLY);
	}

	if( FAILED(Result) )
		appErrorf(TEXT("LockRect failed: %s"),*D3DError(Result));

#ifndef _XBOX
	if( Viewport && Pixels )
	{
		if(SurfaceDesc.Format == D3DFMT_X8R8G8B8 || SurfaceDesc.Format == D3DFMT_A8R8G8B8 || SurfaceDesc.Format == D3DFMT_R5G6B5 )
		{
			// Copy the contents of the back buffer to the destination.
			FColor*	Dest = Pixels;
			INT		X,
					Y;

			for(Y = 0;Y < Viewport->SizeY;Y++)
			{
				DWORD*	SourceRow32 = (DWORD*) ((BYTE*) LockedRect.pBits + LockedRect.Pitch * Y);
				_WORD*	SourceRow16 = (_WORD*) ((BYTE*) LockedRect.pBits + LockedRect.Pitch * Y);

				if( SurfaceDesc.Format == D3DFMT_R5G6B5 )
				{
					for(X = 0;X < Viewport->SizeX;X++)
					{
						_WORD& C = SourceRow16[X];
						*Dest++  = FColor((C>>11) << 3, ((C >> 5)& 63) << 2 , (C & 31) << 3);
					}
				}
				else
				{
					for(X = 0;X < Viewport->SizeX;X++)
						*Dest++ = SourceRow32[X];
				}
			}
		}
		else
			debugf(NAME_DevD3D,TEXT("ReadPixels: Unknown back-buffer format."));
	}
#endif

	// Unlock the back buffer.
	BackBuffer->UnlockRect();

	// Release the back buffer.
	BackBuffer->Release();

	unguard;
}


//
// UD3DRenderDevice::SupportsTextureFormat
//
UBOOL UD3DRenderDevice::SupportsTextureFormat( ETextureFormat Format )
{
	switch( Format )
	{
	case TEXF_DXT1:
		return UseDXT1;
	case TEXF_DXT3:
		return UseDXT3;
	case TEXF_DXT5:
		return UseDXT5;
	case TEXF_RGBA8:
	case TEXF_RGB8:
		return !Use16bitTextures;
	default:
		return true;
	}
}

//
// UD3DRenderDevice::SetEmulationMode
//
void UD3DRenderDevice::SetEmulationMode(EHardwareEmulationMode Mode)
{
	guard(UD3DRenderDevice::SetEmulationMode)

	D3DCAPS8 LocalDeviceCaps8; 
	guard(GetDeviceCaps);
	HRESULT Result = Direct3D8->GetDeviceCaps(BestAdapter,D3DDEVTYPE_HAL,&LocalDeviceCaps8);
	if( FAILED(Result) )
		appErrorf(TEXT("GetDeviceCaps failed(%s) on adapter %i."),*D3DError(Result), BestAdapter);
	unguard;

	//!! warning: Direct3D8->GetDeviceCaps not necessarily == Direct3DDevice8->GetDeviceCaps

	DeviceCaps8.MaxSimultaneousTextures = LocalDeviceCaps8.MaxSimultaneousTextures;
	DeviceCaps8.MaxTextureBlendStages   = LocalDeviceCaps8.MaxTextureBlendStages;
	PixelShaderVersion = ((LocalDeviceCaps8.PixelShaderVersion & 0xFF00) >> 8) * 10 + LocalDeviceCaps8.PixelShaderVersion & 0xFF;
	
	switch( Mode )
	{
	case HEM_GeForce1:
		DeviceCaps8.MaxSimultaneousTextures = 2;
		DeviceCaps8.MaxTextureBlendStages   = 3;
		PixelShaderVersion					= 0;
		break;
	case HEM_XBox:
		DeviceCaps8.MaxSimultaneousTextures = 4;
		DeviceCaps8.MaxTextureBlendStages   = 4;
		break;
	}

	UViewport::RefreshAll();
	
	unguard;
}

//
// UD3DRenderDevice::GetRenderCaps
//
FRenderCaps* UD3DRenderDevice::GetRenderCaps()
		{
	static FRenderCaps RenderCaps;
#if _XBOX
	//scion jg -- Removed vogel's forcing to one layer
	RenderCaps.MaxSimultaneousTerrainLayers = 3;
#else
	if( PixelShaderVersion >= 14 )
		RenderCaps.MaxSimultaneousTerrainLayers = 3; //!!vogel: 4 is slower due to increased overhead
	else
	if( PixelShaderVersion >= 11 )
		RenderCaps.MaxSimultaneousTerrainLayers = 3;
	else
	if( DeviceCaps8.MaxSimultaneousTextures >= 7 && IsKyro )
		RenderCaps.MaxSimultaneousTerrainLayers = 3;	//!!powervr_aaron: Kyro doesn't support TEMP, but 3 layers can be done in 7 stages without it
	else
	if( DeviceCaps8.MaxSimultaneousTextures >= 4 && !IsKyro )	//!! Kyro doesn't support TEMP
		RenderCaps.MaxSimultaneousTerrainLayers = 2;
	else
	if( DeviceCaps8.MaxSimultaneousTextures >= 2 )
		RenderCaps.MaxSimultaneousTerrainLayers = 1;
#endif
	//!!powervr_aaron: Terrain code needs to know whether pixel shaders are being used, and whether SW TnL is being used
	RenderCaps.PixelShaderVersion	= PixelShaderVersion;
	RenderCaps.HardwareTL			= UseHardwareTL;
	return &RenderCaps;
}



#ifndef FINAL_RELEASE
UD3DRenderDevice::FD3DStats::FD3DStats()
{
	guard(FD3DStats::FD3DStats)
	appMemset( &STATS_FirstEntry, 0xFF, (DWORD) &STATS_LastEntry - (DWORD) &STATS_FirstEntry );
	unguard;
}

void UD3DRenderDevice::FD3DStats::Init()
{
	guard(FD3DStats::Init);

	// If already initialized retrieve indices from GStats.
	if( GStats.Registered[STATSTYPE_Hardware] )
	{
		INT* Dummy = &STATS_PushStateCalls;
		for( INT i=0; i<GStats.Stats[STATSTYPE_Hardware].Num(); i++ )
			*(Dummy++) = GStats.Stats[STATSTYPE_Hardware](i).Index;
		return;
	}

	// Register stats with GStat.
	STATS_NumPrimitives					= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("NumPrimitives"		), TEXT("Hardware"		), STATSUNIT_Default				);
	STATS_NumSVPVertices				= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("NumSVPVertices"		), TEXT("Hardware"		), STATSUNIT_Default				);
	STATS_NumTextures					= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("NumTextures"			), TEXT("Hardware"		), STATSUNIT_Default				);
	STATS_TextureBytes					= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("TextureBytes"			), TEXT("Hardware"		), STATSUNIT_KByte					);
	STATS_ResourceBytes					= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("Total Resources"		), TEXT("Hardware"		), STATSUNIT_KByte					);
	STATS_PushStateCalls				= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("PushState"			), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_PushStateCycles				= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("PushState"			), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_PopStateCalls					= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("PopState"			), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_PopStateCycles				= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("PopState"			), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_SetRenderTargetCalls			= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("SetRenderTarget"		), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_SetRenderTargetCycles			= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("SetRenderTarget"		), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_SetMaterialCalls				= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("SetMaterial"			), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_SetMaterialCycles				= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("SetMaterial"			), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_SetMaterialBlendingCalls		= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("SetMaterialBlending"	), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_SetMaterialBlendingCycles		= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("SetMaterialBlending"	), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_SetVertexStreamsCalls			= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("SetVertexStream"		), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_SetVertexStreamsCycles		= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("SetVertexStream"		), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_SetDynamicStreamCalls			= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("SetDynStream"		), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_SetDynamicStreamCycles		= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("SetDynStream"		), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_SetIndexBufferCalls			= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("SetIB"				), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_SetIndexBufferCycles			= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("SetIB"				), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_SetDynamicIndexBufferCalls	= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("SetDynIB"			), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_SetDynamicIndexBufferCycles	= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("SetDynIB"			), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_DrawPrimitiveCalls			= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("DrawPrimitive"		), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_DrawPrimitiveCycles			= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("DrawPrimitive"		), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_DrawPrimitiveUPCycles			= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("DrawPrimitiveUP"		), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_DrawPrimitiveUPCalls			= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("DrawPrimitiveUP"		), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_RunPushBufferCycles			= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("RunPushBuffer"		), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_RunPushBufferCalls			= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("RunPushBuffer"		), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_LockCalls						= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("Lock"				), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_LockCycles					= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("Lock"				), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_UnlockCalls					= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("Unlock"				), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_UnlockCycles					= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("Unlock"				), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_PresentCalls					= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("Present"				), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_PresentCycles					= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("Present"				), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_StateChanges					= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("StateChanges"		), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_StateChangeCycles				= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("StateChanges"		), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_TextureChanges				= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("TextureChanges"		), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_TextureChangeCycles			= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("TextureChanges"		), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_TransformChanges				= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("TransformChanges"	), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_TransformChangeCycles			= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("TransformChanges"	), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_LightChanges					= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("LightChanges"		), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_LightChangeCycles				= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("LightChanges"		), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_LightSetChanges				= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("LightSet"			), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_LightSetCycles				= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("LightSet"			), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_ClearCalls					= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("Clear"				), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_ClearCycles					= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("Clear"				), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_DynamicVertexBufferLockCalls	= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("DynVBLock"			), TEXT("Hardware"		), STATSUNIT_Combined_Default_MSec	);
	STATS_DynamicVertexBufferLockCycles	= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("DynVBLock"			), TEXT("Hardware"		), STATSUNIT_MSec					);
	STATS_DynamicVertexBufferDiscards	= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("DynVBDiscards"		), TEXT("Hardware"		), STATSUNIT_Default				);
	STATS_DynamicVertexBytes			= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("DynVBBytes"			), TEXT("Hardware"		), STATSUNIT_KByte					);
	STATS_DynamicIndexBytes				= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("DynIBBytes"			), TEXT("Hardware"		), STATSUNIT_KByte					);
	STATS_NumVertexStreams				= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("NumVertexStreams"	), TEXT("Hardware"		), STATSUNIT_Default				);
	STATS_NumIndexBuffers				= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("NumIndexBuffers"		), TEXT("Hardware"		), STATSUNIT_Default				);
	STATS_VertexStreamBytes				= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("VertexStreamBytes"	), TEXT("Hardware"		), STATSUNIT_KByte					);
	STATS_IndexBufferBytes				= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("IndexBufferBytes"	), TEXT("Hardware"		), STATSUNIT_KByte					);
	STATS_StreamSourceChanges			= GStats.RegisterStats( STATSTYPE_Hardware, STATSDATATYPE_DWORD, TEXT("StreamSourceChanges"	), TEXT("Hardware"		), STATSUNIT_Default				);

	// Initialized.
	GStats.Registered[STATSTYPE_Hardware] = 1;

#if PERFORMANCE_STATS
	D3DPERF_GetStatistics()->m_dwDumpFPSInfoMask |=  (D3DPERF_DUMP_FPS_MEMORY | D3DPERF_DUMP_FPS_PUSHBUFFER | D3DPERF_DUMP_FPS_WAITINFO | D3DPERF_DUMP_FPS_VERTEXINFO | D3DPERF_DUMP_FPS_PERFPROFILE);
	D3DPERF_GetStatistics()->m_DumpWaitCycleTimesThreshold = 3000;
#endif

	unguard;
}
#endif

#ifndef _XBOX //scion jg -- We use the pre-compiled shaders on the xbox

class FTerrain3LayerPixelShader11: public FD3DPixelShader
{
public:
	// Constructor/destructor.
	FTerrain3LayerPixelShader11(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_Terrain3Layer, 
			"ps.1.1																\n"\
			"def c0, 1, 0, 0, 0				// used to extract value from R		\n"\
			"def c1, 0, 1, 0, 0				// used to extract value from G		\n"\
			"def c2, 0, 0, 1, 0				// used to extract value from B		\n"\
			"tex t0							// weightmaps in R, G, B			\n"\
			"tex t1							// layer 1							\n"\
			"tex t2							// layer 2							\n"\
			"tex t3							// layer 3							\n"\
			"dp3 r1, t0, c0					// r1 = Weight1						\n"\
			"mul r0, t1, r1					// r0 = T1*W1						\n"\
			"dp3 r1, t0, c1					// r1 = Weight2						\n"\
			"mad r0, t2, r1, r0				// r0 = T2*W2 + T1*W1				\n"\
			"dp3 r1, t0, c2					// r1 = Weight3						\n"\
			"mad r0, t3, r1, r0				// r0 = T3*W3 + T2*W2 + T1*W1		\n"\
			"mul_x2 r0, r0, v0				// r0 = r0 * lighting				\n"
		)
	{
	}
};

class FTerrain3LayerPixelShader14: public FD3DPixelShader
{
public:
	// Constructor/destructor.
	FTerrain3LayerPixelShader14(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_Terrain3Layer, 
			"ps.1.4																\n" \
			"texld r0, t0					// rgba - blending weights			\n" \
			"texld r1, t1					// tex 1							\n" \
			"texld r2, t2					// tex 2							\n" \
			"texld r3, t3					// tex 3							\n" \
			"mul r1, r1, r0.r													\n" \
			"mad r1, r2, r0.g, r1												\n" \
			"mad r1, r3, r0.b, r1												\n" \
			"mul_x2 r0, r1, v0													\n"
		)
	{}
};

class FTerrain4LayerPixelShader14: public FD3DPixelShader
{
public:
	// Constructor/destructor.
	FTerrain4LayerPixelShader14(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_Terrain4Layer, 		
			"ps.1.4									\n" \
			"texld r0, t0					// rgba - blending weights			\n" \
			"texld r1, t1					// tex 1							\n" \
			"texld r2, t2					// tex 2							\n" \
			"texld r3, t3					// tex 3							\n" \
			"texld r4, t4					// tex 4							\n" \
			"mul r1, r1, r0.r													\n" \
			"mad r1, r2, r0.g, r1												\n" \
			"mad r1, r3, r0.b, r1												\n" \
			"mad r1, r4, r0.a, r1												\n" \
			"mul_x2 r0, r1, v0													\n"
		)
	{}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * This pixel shader converts the source texture to a luminance value and
 * modulates that with a color. c1 contains the color to modulate the luminance
 * by. This modulation effectively changes the indexing into the next texture
 *
 * ============================================================================
 */
class FLuminanceTexIndexPixelShader: public FD3DPixelShader
{
public:
	// Constructor/destructor.
	FLuminanceTexIndexPixelShader(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_LuminanceTexIndex,
			"ps.1.1																\n" \
			"def c0, 0.30f,0.59f,0.11f,1.0f // Constant setup for luminance		\n" \
			"tex t0							// Load the texture to modify		\n" \
			"dp3 r1, t0, c0					// Apply a luminance filter			\n" \
			"mul_x2 r0, r1, c1				// Modulate the index with the specified filter\n" 
		)
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * This pixel shader uses the green and blue from texture stage0 to lookup the
 * correct texel in texture stage1.
 *
 * ============================================================================
 */
class FUseTexIndexPixelShader: public FD3DPixelShader
{
public:
	// Constructor/destructor.
	FUseTexIndexPixelShader(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_UseTexIndex,
			"ps.1.1														\n" \
			"tex t0					// Load the texture to modify		\n" \
			"texreg2gb t1, t0		// Index into the next texture stage\n" \
			"mov r0, t1													\n"
		)
	{
	}
};

// --	FBlurPixelShader11 
//		4 tap sample blur for D3D
class FBlurPixelShader11 : public FD3DPixelShader
{
public:
	  // Constructor/destructor.
	  FBlurPixelShader11(UD3DRenderDevice* InRenDev) :
	  FD3DPixelShader( InRenDev, PS_Blur,
	  "ps.1.1																		\n" \
	  "def c0, 0.25f, 0.25f, 0.25f, 0.25f											\n" \
	  "def c1, 0.25f, 0.25f, 0.25f, 0.25f											\n" \
	  "def c2, 0.25f, 0.25f, 0.25f, 0.25f											\n" \
	  "def c3, 0.25f, 0.25f, 0.25f, 0.25f											\n" \
	  "tex t0																		\n" \
	  "tex t1																		\n" \
	  "tex t2																		\n" \
	  "tex t3																		\n" \
	  "mul r0, c0, t0																\n" \
	  "mad r0, c1, t1, r0															\n" \
	  "mad r0, c2, t2, r0															\n" \
	  "mad r0, c3, t3, r0															\n"
	  )
	  {}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * This material is an optimized version of the original Unreal Shader class
 * used for character skins. It takes materials that would have been 2 passes
 * using the fixed function pipeline and makes them single pass materials. This
 * code assumes that the Alpha channel of the diffuse is the mask to use for
 * the specular. It also applies the mesh lighting to the diffuse only. Note:
 * c0 contains a color to modulate with.
 *
 * ============================================================================
 */
class FSkinShader: public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See SkinShader.psh for the documentation/details
	FSkinShader(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_SkinShader,
			"ps.1.1\r\n" \
			"tex t0\r\n" \
			"tex t1\r\n" \
			"tex t2\r\n" \
			"mul r1, t1, t0.a\r\n" \
			"add r1, r1, t2\r\n" \
			"mul_x2 r0, t0, v0\r\n" \
			"mad r0, r0, c0, r1\r\n"
		)
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * This material is an optimized version of the original Unreal Shader class
 * used for character skins. It takes materials that would have been 2 passes
 * using the fixed function pipeline and makes them single pass materials. This
 * code assumes that the Alpha channel of the diffuse is the mask to use for
 * the specular. It also applies the mesh lighting to both the specular and the
 * diffuse textures. Note: c0 contains a color to modulate with.
 *
 * ============================================================================
 */
class FSkinShaderSpecLight: public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See SkinShaderSpecLight.psh for the documentation/details
	FSkinShaderSpecLight(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_SkinShaderSpecLight,
			"ps.1.1\r\n" \
			"tex t0\r\n" \
			"tex t1\r\n" \
			"tex t2\r\n" \
			"mul r1, t1, t0.a\r\n" \
			"add r1, r1, t2\r\n" \
			"add r0, t0, r1\r\n" \
			"mul r0, r0, v0\r\n" \
			"mul_x2 r0, r0, c0\r\n"
		)
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * This material is an optimized version of the original Unreal Shader class
 * used for character skins. It takes materials that would have been 2 passes
 * using the fixed function pipeline and makes them single pass materials. This
 * code assumes that the Alpha channel of the diffuse is the mask to use for
 * the specular. The alpha channel of the TeamMask (t3) determines where to mask
 * the team color information. It also applies the mesh lighting to the diffuse
 * only. Note: c0 contains a color to modulate with.
 *
 * ============================================================================
 */
class FTeamSkinShader: public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See SkinShader.psh for the documentation/details
	FTeamSkinShader(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_TeamSkinShader,
			"ps.1.1\r\n" \
			"tex t0\r\n" \
			"tex t1\r\n" \
			"tex t2\r\n" \
			"tex t3\r\n" \
			"mul r1, t1, t0.a\r\n" \
			"add r1, r1, t2\r\n" \
			"mul_x2 r0, t0, v0\r\n" \
			"add r0, r0, r1\r\n" \
			"mul r1, c0, t3.a\r\n" \
			"mul r1, r0, r1\r\n" \
			"add r0, r0, r1\r\n"
		)
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * This material is an optimized version of the original Unreal Shader class
 * used for character skins. It takes materials that would have been 2 passes
 * using the fixed function pipeline and makes them single pass materials. This
 * code assumes that the Alpha channel of the diffuse is the mask to use for
 * the specular. The alpha channel of the TeamMask (t3) determines where to mask
 * the team color information. It also applies the mesh lighting to both the
 * specular and the diffuse textures. Note: c0 contains a color to modulate
 * with.
 *
 * ============================================================================
 */
class FTeamSkinShaderSpecLight: public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See TeamSkinShaderSpecLight.psh for the documentation/details
	FTeamSkinShaderSpecLight(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_TeamSkinShaderSpecLight,
			"ps.1.1\r\n" \
			"tex t0\r\n" \
			"tex t1\r\n" \
			"tex t2\r\n" \
			"tex t3\r\n" \
			"mul r1, t1, t0.a\r\n" \
			"add r1, r1, t2\r\n" \
			"add r0, t0, r1\r\n" \
			"mul_x2 r0, r0, v0\r\n" \
			"mul r1, c0, t3.a\r\n" \
			"mul r1, r0, r1\r\n" \
			"add r0, r0, r1\r\n"
		)
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DualDiffuse_NM_Add_NSL.psh
 *
 * ============================================================================
 */
class FDualDiffuse_NM_Add_NSL: public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_NM_Add_NSL(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_NM_Add_NSL,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"add r0, t0, t1\r\n" \
		"mul_x2 r0, r0, v0\r\n" \
		"add r0, r0, t2\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DualDiffuse_NM_Add_SL.psh
 *
 * ============================================================================
 */
class FDualDiffuse_NM_Add_SL: public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_NM_Add_SL(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_NM_Add_SL,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"add r0, t0, t1\r\n" \
		"add r0, r0, t2\r\n" \
		"mul_x2 r0, r0, v0\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DualDiffuse_NM_Mod_NSL.psh
 *
 * ============================================================================
 */
class FDualDiffuse_NM_Mod_NSL : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_NM_Mod_NSL(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_NM_Mod_NSL,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"mul r0, t0, t1\r\n" \
		"mul_x2 r0, r0, v0\r\n" \
		"add r0, r0, t2\r\n" )
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DualDiffuse_NM_Mod_SL.psh
 *
 * ============================================================================
 */
class FDualDiffuse_NM_Mod_SL : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_NM_Mod_SL(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_NM_Mod_SL,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"mul r0, t0, t1\r\n" \
		"add r0, r0, t2\r\n" \
		"mul_x2 r0, r0, v0\r\n" )
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DualDiffuse_M1_Add_NSL.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M1_Add_NSL : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M1_Add_NSL(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M1_Add_NSL,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"mul r1, t0, t0.a\r\n" \
		"mul r0, t1, t0.a\r\n" \
		"add r0, r0, r1\r\n" \
		"mul_x2 r0, r0, v0\r\n" \
		"add r0, r0, t2\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DualDiffuse_M1_Add_SL.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M1_Add_SL : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M1_Add_SL(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M1_Add_SL,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"mul r1, t0, t0.a\r\n" \
		"mul r0, t1, t0.a\r\n" \
		"add r0, r0, r1\r\n" \
		"add r0, r0, t2\r\n" \
		"mul_x2 r0, r0, v0\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See FDualDiffuse_M1_Mod_NSL.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M1_Mod_NSL : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M1_Mod_NSL(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M1_Mod_NSL,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"mul r1, t0, t1\r\n" \
		"mul r0, r1, t0.a\r\n" \
		"mul_x2 r0, r0, v0\r\n" \
		"add r0, r0, t2\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See FDualDiffuse_M1_Mod_SL.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M1_Mod_SL : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M1_Mod_SL(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M1_Mod_SL,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"mul r1, t0, t1\r\n" \
		"mul r0, r1, t0.a\r\n" \
		"add r0, r0, t2\r\n" \
		"mul_x2 r0, r0, v0\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See FDualDiffuse_M1_Blend_NSL.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M1_Blend_NSL : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M1_Blend_NSL(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M1_Blend_NSL,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"mul r0, t0, t0.a\r\n" \
		"mul r1, t1, 1 - t0.a\r\n" \
		"add r0, r0, r1\r\n" \
		"mul_x2 r0, r0, v0\r\n" \
		"add r0, r0, t2\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See FDualDiffuse_M1_Blend_SL.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M1_Blend_SL : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M1_Blend_SL(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M1_Blend_SL,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"mul r0, t0, t0.a\r\n" \
		"mul r1, t1, 1 - t0.a\r\n" \
		"add r0, r0, r1\r\n" \
		"add r0, r0, t2\r\n" \
		"mul_x2 r0, r0, v0\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DualDiffuse_M2_Add_NSL.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M2_Add_NSL : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M2_Add_NSL(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M2_Add_NSL,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"mul r1, t0, t1.a\r\n" \
		"mul r0, t1, t1.a\r\n" \
		"add r0, r0, r1\r\n" \
		"mul_x2 r0, r0, v0\r\n" \
		"add r0, r0, t2\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DualDiffuse_M2_Add_SL.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M2_Add_SL : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M2_Add_SL(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M2_Add_SL,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"mul r1, t0, t1.a\r\n" \
		"mul r0, t1, t1.a\r\n" \
		"add r0, r0, r1\r\n" \
		"add r0, r0, t2\r\n" \
		"mul_x2 r0, r0, v0\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See FDualDiffuse_M2_Mod_NSL.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M2_Mod_NSL : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M2_Mod_NSL(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M2_Mod_NSL,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"mul r1, t0, t1\r\n" \
		"mul r0, r1, t1.a\r\n" \
		"mul_x2 r0, r0, v0\r\n" \
		"add r0, r0, t2\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See FDualDiffuse_M2_Mod_SL.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M2_Mod_SL : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M2_Mod_SL(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M2_Mod_SL,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"mul r1, t0, t1\r\n" \
		"mul r0, r1, t1.a\r\n" \
		"add r0, r0, t2\r\n" \
		"mul_x2 r0, r0, v0\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See FDualDiffuse_M2_Blend_NSL.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M2_Blend_NSL : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M2_Blend_NSL(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M2_Blend_NSL,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"mul r0, t0, t1.a\r\n" \
		"mul r1, t1, 1 - t1.a\r\n" \
		"add r0, r0, r1\r\n" \
		"mul_x2 r0, r0, v0\r\n" \
		"add r0, r0, t2\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See FDualDiffuse_M2_Blend_SL.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M2_Blend_SL : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M2_Blend_SL(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M2_Blend_SL,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"mul r0, t0, t1.a\r\n" \
		"mul r1, t1, 1 - t1.a\r\n" \
		"add r0, r0, r1\r\n" \
		"add r0, r0, t2\r\n" \
		"mul_x2 r0, r0, v0\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DiffuseOpacitySpecularMask.psh
 *
 * ============================================================================
 */
class FDiffuseOpacitySpecularMask : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDiffuseOpacitySpecularMask(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DiffuseOpacitySpecularMask,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"mul_x2 r0, t0, v0\r\n" \
		"add r0.rgb, r0, t1\r\n" \
		"+mov r0.a, t0.a\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DiffuseOpacitySpecularMaskSpecLight.psh
 *
 * ============================================================================
 */
class FDiffuseOpacitySpecularMaskSpecLight : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDiffuseOpacitySpecularMaskSpecLight(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DiffuseOpacitySpecularMaskSpecLight,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"add r0, t1, t0\r\n" \
		"mul_x2 r0.rgb, r0, v0\r\n" \
		"+mov r0.a, t0.a\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DiffuseOpacitySpecularMask2.psh
 *
 * ============================================================================
 */
class FDiffuseOpacitySpecularMask2 : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDiffuseOpacitySpecularMask2(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DiffuseOpacitySpecularMask2,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"mul_x2 r0, t0, v0\r\n" \
		"mul r1, t1, t2.a\r\n" \
		"add r0.rgb, r0, r1\r\n" \
		"+mov r0.a, t0.a\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DiffuseOpacitySpecularMaskSpecLight2.psh
 *
 * ============================================================================
 */
class FDiffuseOpacitySpecularMaskSpecLight2 : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDiffuseOpacitySpecularMaskSpecLight2(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DiffuseOpacitySpecularMaskSpecLight2,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"mul r0, t1, t2.a\r\n" \
		"add r0, r1, r0\r\n" \
		"mul_x2 r0.rgb, r0, v0\r\n" \
		"+mov r0.a, t0.a\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DualDiffuse_NM_Add_NSL_SM.psh
 *
 * ============================================================================
 */
class FDualDiffuse_NM_Add_NSL_SM : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_NM_Add_NSL_SM(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_NM_Add_NSL_SM,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"add r0, t0, t1\r\n" \
		"mul_x2 r0, r0, v0\r\n" \
		"mul r1, t2, t3.a\r\n" \
		"add r0, r0, r1\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DualDiffuse_NM_Add_SL_SM.psh
 *
 * ============================================================================
 */
class FDualDiffuse_NM_Add_SL_SM : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_NM_Add_SL_SM(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_NM_Add_SL_SM,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"add r0, t0, t1\r\n" \
		"mul r1, t2, t3.a\r\n" \
		"add r0, r0, r1\r\n" \
		"mul_x2 r0, r0, v0\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DualDiffuse_NM_Mod_NSL_SM.psh
 *
 * ============================================================================
 */
class FDualDiffuse_NM_Mod_NSL_SM : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_NM_Mod_NSL_SM(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_NM_Mod_NSL_SM,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul r0, t0, t1\r\n" \
		"mul_x2 r0, r0, v0\r\n" \
		"mul r1, t2, t3.a\r\n" \
		"add r0, r0, r1\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DualDiffuse_NM_Mod_SL.psh
 *
 * ============================================================================
 */
class FDualDiffuse_NM_Mod_SL_SM : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_NM_Mod_SL_SM(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_NM_Mod_SL_SM,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul r0, t0, t1\r\n" \
		"mul r1, t2, t3.a\r\n" \
		"add r0, r0, r1\r\n" \
		"mul_x2 r0, r0, v0\r\n" )
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DualDiffuse_M1_Add_NSL_SM.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M1_Add_NSL_SM : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M1_Add_NSL_SM(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M1_Add_NSL_SM,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul r1, t0, t0.a\r\n" \
		"mul r0, t1, t0.a\r\n" \
		"add r0, r0, r1\r\n" \
		"mul_x2 r0, r0, v0\r\n" \
		"mul r1, t2, t3.a\r\n" \
		"add r0, r0, r1\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DualDiffuse_M1_Add_SL_SM.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M1_Add_SL_SM : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M1_Add_SL_SM(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M1_Add_SL_SM,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul r1, t0, t0.a\r\n" \
		"mul r0, t1, t0.a\r\n" \
		"add r0, r0, r1\r\n" \
		"mul r1, t2, t3.a\r\n" \
		"add r0, r0, r1\r\n" \
		"mul_x2 r0, r0, v0\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See FDualDiffuse_M1_Mod_NSL_SM.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M1_Mod_NSL_SM : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M1_Mod_NSL_SM(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M1_Mod_NSL_SM,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul r1, t0, t1\r\n" \
		"mul r0, r1, t0.a\r\n" \
		"mul_x2 r0, r0, v0\r\n" \
		"mul r1, t2, t3.a\r\n" \
		"add r0, r0, r1\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See FDualDiffuse_M1_Mod_SL_SM.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M1_Mod_SL_SM : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M1_Mod_SL_SM(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M1_Mod_SL_SM,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul r1, t0, t1\r\n" \
		"mul r0, r1, t0.a\r\n" \
		"mul r1, t2, t3.a\r\n" \
		"add r0, r0, r1\r\n" \
		"mul_x2 r0, r0, v0\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See FDualDiffuse_M1_Blend_NSL_SM.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M1_Blend_NSL_SM : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M1_Blend_NSL_SM(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M1_Blend_NSL_SM,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul r0, t0, t0.a\r\n" \
		"mul r1, t1, 1 - t0.a\r\n" \
		"add r0, r0, r1\r\n" \
		"mul_x2 r0, r0, v0\r\n" \
		"mul r1, t2, t3.a\r\n" \
		"add r0, r0, r1\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See FDualDiffuse_M1_Blend_SL_SM.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M1_Blend_SL_SM : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M1_Blend_SL_SM(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M1_Blend_SL_SM,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul r0, t0, t0.a\r\n" \
		"mul r1, t1, 1 - t0.a\r\n" \
		"add r0, r0, r1\r\n" \
		"mul r1, t2, t3.a\r\n" \
		"add r0, r0, r1\r\n" \
		"mul_x2 r0, r0, v0\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DualDiffuse_M2_Add_NSL_SM.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M2_Add_NSL_SM : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M2_Add_NSL_SM(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M2_Add_NSL_SM,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul r1, t0, t1.a\r\n" \
		"mul r0, t1, t1.a\r\n" \
		"add r0, r0, r1\r\n" \
		"mul_x2 r0, r0, v0\r\n" \
		"mul r1, t2, t3.a\r\n" \
		"add r0, r0, r1\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DualDiffuse_M2_Add_SL_SM.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M2_Add_SL_SM : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M2_Add_SL_SM(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M2_Add_SL_SM,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul r1, t0, t1.a\r\n" \
		"mul r0, t1, t1.a\r\n" \
		"add r0, r0, r1\r\n" \
		"mul r1, t2, t3.a\r\n" \
		"add r0, r0, r1\r\n" \
		"mul_x2 r0, r0, v0\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DualDiffuse_M2_Mod_NSL_SM.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M2_Mod_NSL_SM : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M2_Mod_NSL_SM(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M2_Mod_NSL_SM,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul r1, t0, t1\r\n" \
		"mul r0, r1, t1.a\r\n" \
		"mul_x2 r0, r0, v0\r\n" \
		"mul r1, t2, t3.a\r\n" \
		"add r0, r0, r1\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DualDiffuse_M2_Mod_SL.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M2_Mod_SL_SM : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M2_Mod_SL_SM(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M2_Mod_SL_SM,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul r1, t0, t1\r\n" \
		"mul r0, r1, t1.a\r\n" \
		"mul r1, t2, t3.a\r\n" \
		"add r0, r0, r1\r\n" \
		"mul_x2 r0, r0, v0\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DualDiffuse_M2_Blend_NSL_SM.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M2_Blend_NSL_SM : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M2_Blend_NSL_SM(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M2_Blend_NSL_SM,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul r0, t0, t1.a\r\n" \
		"mul r1, t1, 1 - t1.a\r\n" \
		"add r0, r0, r1\r\n" \
		"mul_x2 r0, r0, v0\r\n" \
		"mul r1, t2, t3.a\r\n" \
		"add r0, r0, r1\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DualDiffuse_M2_Blend_SL_SM.psh
 *
 * ============================================================================
 */
class FDualDiffuse_M2_Blend_SL_SM : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDualDiffuse_M2_Blend_SL_SM(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DualDiffuse_M2_Blend_SL_SM,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul r0, t0, t1.a\r\n" \
		"mul r1, t1, 1 - t1.a\r\n" \
		"add r0, r0, r1\r\n" \
		"mul r1, t2, t3.a\r\n" \
		"add r0, r0, r1\r\n" \
		"mul_x2 r0, r0, v0\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See ShenuSkin.psh
 *
 * ============================================================================
 */
class FShenuSkin : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FShenuSkin(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_ShenuSkin,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"add r1, t0, t1\r\n" \
		"mul r1, r1, t0.a\r\n" \
		"mov r0, r1\r\n" \
		"mul r1, r1, t2.a\r\n" \
		"add r0, r0, r1\r\n" \
		"mad r0.a, t0.a, t0.a, t0.a\r\n")
	{
	}
};

/* scion ======================================================================
* Author: sz
* ============================================================================
*
* See EmissiveConstColor.psh
*
* ============================================================================
*/
class FEmissiveConstColorPS : public FD3DPixelShader
{
public:
	FEmissiveConstColorPS(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_EmissiveConstColor,
		"ps.1.1						\r\n" \
		"mov r0, c0					\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See TeamShader.psh
 *
 * ============================================================================
 */
class FTeamShader : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FTeamShader(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_TeamShader,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul_x2 r0, t0, v0\r\n" \
		"mad r0, t1, t2.a, r0\r\n" \
		"mul r1, c0, r0\r\n" \
		"lrp r0, t3.a, r1, r0\r\n" \
		"mov r0.a, c0.a")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See TeamShader.psh
 *
 * ============================================================================
 */
class FTeamShaderSpecLight : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FTeamShaderSpecLight(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_TeamShaderSpecLight,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul r0, t1, t2.a\n" \
		"add r0, r0, t0\n" \
		"mul_x2 r0, r0, v0\n" \
		"mul r1, c0, r0\n" \
		"lrp r0, t3.a, r1, r0\n" \
		"mov r0.a, c0.a")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See EmissiveDiffuseOnly.psh
 *
 * ============================================================================
 */
class FEmissiveDiffuseOnly : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FEmissiveDiffuseOnly(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_EmissiveShaderDiffuseOnly,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"mul_x2 r0, t0, v0\r\n" \
		"mul_x4 r1, t1.a, c0\r\n" \
		"mul_x4_sat r1, r1, r1\r\n" \
		"mul r1.rgb, t0, c1\r\n" \
		"lrp r0, r1.a, r1, r0\r\n" \
		"mul r0.a, t1.a, c1.a\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See EmissiveShader.psh
 *
 * ============================================================================
 */
class FEmissiveShader : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FEmissiveShader(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_EmissiveShaderSpec,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul_x2 r0, t0, v0\r\n" \
		"mad r0, t1, t2.a, r0\r\n" \
		"mul_x4 r1, t3.a, c0\r\n" \
		"mul_x4_sat r1, r1, r1\r\n" \
		"mul r1.rgb, t0, c1\r\n" \
		"lrp r0, r1.a, r1, r0\r\n" \
		"mul r0.a, t1.a, c1.a\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See EmissiveShaderSpecLight.psh
 *
 * ============================================================================
 */
class FEmissiveShaderSpecLight : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FEmissiveShaderSpecLight(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_EmissiveShaderSpecLight,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul r0, t1, t2.a\r\n" \
		"add r0, t0, r0\r\n" \
		"mul_x2 r0, r0, v0\r\n" \
		"mul_x4 r1, t3.a, c0\r\n" \
		"mul_x4_sat r1, r1, r1\r\n" \
		"mul r1.rgb, t0, c1\r\n" \
		"lrp r0, r1.a, r1, r0\r\n" \
		"mul r0.a, t1.a, c1.a\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See EmissiveSpecular.psh
 *
 * ============================================================================
 */
class FEmissiveSpecular : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FEmissiveSpecular(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_EmissiveSpecular,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"mul_x2 r0, t0, v0\r\n" \
		"mul_x4 r1, t2.a, c0\r\n" \
		"mov_sat r1, r1\r\n" \
		"mul r1.rgb, t1, c1\r\n" \
		"lrp r0, r1.a, r1, r0\r\n" \
		"mul r0.a, t2.a, c1.a\r\n")
	{
	}
};

/* scion ======================================================================
* Author: sz
* ============================================================================
* see SkinShaderOpacity.psh
* ============================================================================
*/
class FSkinShaderOpacity: public FD3DPixelShader
{
public:
	FSkinShaderOpacity(UD3DRenderDevice* InRenDev)	: FD3DPixelShader( InRenDev, PS_SkinShader,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul r1, t1, t0.a\r\n" \
		"add r1, r1, t3\r\n" \
		"mul_x2 r0, t0, v0\r\n" \
		"mad r0.rgb, r0, c0, r1\r\n" \
		"add r0.a, t2.a, c0_bias.a\r\n"
		)
	{
	}
};

/* scion ======================================================================
* Author: sz
* ============================================================================
* see SkinShaderSpecLightOpacity.psh
* ============================================================================
*/
class FSkinShaderSpecLightOpacity: public FD3DPixelShader
{
public:
	FSkinShaderSpecLightOpacity(UD3DRenderDevice* InRenDev)	: FD3DPixelShader( InRenDev, PS_SkinShader,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul r1, t1, t0.a\r\n" \
		"add r1, r1, t3\r\n" \
		"add r0, t0, r1\r\n" \
		"mul_x2 r0, r0, v0\r\n" \
		"mul r0.rgb, r0, c0\r\n" \
		"add r0.a, t2.a, c0_bias.a\r\n"
		)
	{
	}
};

/* scion ======================================================================
* Author: sz
* ============================================================================
* see TeamSkinShaderOpacity.psh
* ============================================================================
*/
class FTeamSkinShaderOpacity: public FD3DPixelShader
{
public:
	FTeamSkinShaderOpacity(UD3DRenderDevice* InRenDev)	: FD3DPixelShader( InRenDev, PS_SkinShader,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul r1, t1, t0.a\r\n" \
		"mul_x2 r0.rgb, t0, v0\r\n" \
		"add r0.rgb, r0, r1\r\n" \
		"add r0.a, t2.a, c0_bias.a\r\n" \
		"mul r1, c0, t3.a\r\n" \
		"add r0.rgb, r1, r0"
		)
	{
	}
};

/* scion ======================================================================
* Author: sz
* ============================================================================
* see TeamSkinShaderSpecLightOpacity.psh
* ============================================================================
*/
class FTeamSkinShaderSpecLightOpacity: public FD3DPixelShader
{
public:
	FTeamSkinShaderSpecLightOpacity(UD3DRenderDevice* InRenDev)	: FD3DPixelShader( InRenDev, PS_SkinShader,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"tex t2\r\n" \
		"tex t3\r\n" \
		"mul r1, t1, t0.a\r\n" \
		"add r0.rgb, t0, r1\r\n" \
		"mul_x2 r0.rgb, r0, v0\r\n" \
		"add r0.a, t2.a, c0_bias.a\r\n" \
		"mul r1, c0, t3.a\r\n" \
		"add r0.rgb, r1, r0"
		)
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See MaskedLuminance.psh
 *
 * ============================================================================
 */
class FMaskedLuminance : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FMaskedLuminance(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_MaskedLuminance,
		"ps.1.1\n" \
		"tex t0\n" \
		"tex t1\n" \
		"dp3 r1, t0, c0\n" \
		"lrp r0, t1.a, r1, t0\r\n" \
		"mov r0.a, t1.a\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See TeamShaderNoSpec.psh
 *
 * ============================================================================
 */
class FTeamShaderNoSpec : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FTeamShaderNoSpec(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_TeamShaderNoSpec,
		"ps.1.1\n" \
		"tex t0\n" \
		"tex t1\n" \
		"mul_x2 r0, t0, v0\n" \
		"mul r1, c0, r0\n" \
		"lrp r0, t1.a, r1, r0\n" \
		"mov r0.a, c0.a\r\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See TeamBSPShader.psh
 *
 * ============================================================================
 */
class FTeamBSPShader : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FTeamBSPShader(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_TeamBSPShader,
		"ps.1.1\n" \
		"def c0, 1, 1, 1, 1\n"\
		"tex t0\n" \
		"tex t1\n" \
		"tex t2\n" \
		"mov r0, t0\n" \
		"mul r1, c0, r0\n" \
		"lrp r0, t1.a, r1, r0\n" \
		"mul r0, t2, r0\n" \
		"mov r0.a, c0.a\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See UseTexIndexMasked.psh
 *
 * ============================================================================
 */
class FUseTexIndexMasked : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FUseTexIndexMasked(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_UseTexIndexMasked,
		"ps.1.1\n" \
		"tex t0\n" \
		"tex t1\n" \
		"texreg2gb t2, t0\n" \
		"mov r0.rgb, t2\n" \
		"+mov r0.a, t1.a\n")
	{
	}
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * See DiffuseOpacity.psh
 *
 * ============================================================================
 */
class FDiffuseOpacity : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDiffuseOpacity(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_DiffuseOpacity,
		"ps.1.1\r\n" \
		"tex t0\r\n" \
		"tex t1\r\n" \
		"mul_x2 r0.rgb, t0, v0\r\n" \
		"+mov r0.a, t1.a\n")
	{
	}
};

/* scion ======================================================================
 * Author: sz
 * ============================================================================
 *
 * See Distortion.psh
 *
 * ============================================================================
 */
class FDistortionPS : public FD3DPixelShader
{
public:
	// Constructor/destructor.
	//scion jg -- See the matching pixel shader file for the documentation/details
	FDistortionPS(UD3DRenderDevice* InRenDev)
	:	FD3DPixelShader( InRenDev, PS_Distortion,
		"ps.1.1\n" \
		"tex t0\n" \
		"mov r0, t0\n" \
		"mul r0, r0, t0.a\n" )
	{
	}
};
#endif

// scion sz - load from arrays
#define PS_SOURCE_STRUCT(Name) NULL, dw##Name##PixelShader
// scion sz - load from file
#define PS_SOURCE_FILE(Name) "D:\\ShaderData\\PixelShaders\\Name##.xpu", NULL

//jg -- Direct access to the shader microcode
#define PS_MICROCODE(Name) ((D3DPIXELSHADERDEF*)&dw##Name##PixelShader)

#ifdef LOAD_PIXEL_SHADERS_HEADERS
#define PS_SOURCE(Name) PS_SOURCE_STRUCT(Name) 
#else
#define PS_SOURCE(Name) PS_SOURCE_FILE(Name) 
#endif


FD3DPixelShader* UD3DRenderDevice::GetPixelShader(EPixelShader Type)
{
	guard(UD3DRenderDevice::GetPixelShader);
	
	if( Type == PS_None )
		return NULL;

	// check for an existing pixel shader
	if( PixelShaders(Type) )
		return PixelShaders(Type);

#ifdef _XBOX
	// scion sz - Create a new pixel shader.
	switch(Type)
	{
	case PS_Terrain3Layer:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(Terrain3layer) );
	case PS_Blur:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(Blur) );
	case PS_DepthOfField:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DOF) );
	case PS_Mask:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(Mask) );
	case PS_MotionBlur:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(MotionBlur) );
	case PS_Distortion:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(Distortion) );
//	case PS_DistortionBlended:
//		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DistortionBlended) );
	case PS_DistortionAccum:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DistortionAccum) );
	case PS_Refraction:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(Refraction) );
	case PS_BloomSelect:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(BloomSelect) );
	case PS_EmissiveSelect:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(EmissiveSelect) );
	case PS_EmissiveSelectBlur:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(EmissiveSelectBlur) );
	case PS_LuminanceTexIndex:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(LuminanceTexIndex) );
	case PS_UseTexIndex:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(UseTexIndex) );
	case PS_ModulatedDistortion:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(ModulatedDistortion) );
	case PS_AdditiveDistortion:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(AdditiveDistortion) );
	case PS_SubtractiveDistortion:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(SubtractiveDistortion) );
	case PS_SkinShader:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(SkinShader) );
	case PS_SkinShaderSpecLight:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(SkinShaderSpecLight) );
	case PS_TeamSkinShader:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(TeamSkinShader) );
	case PS_TeamSkinShaderSpecLight:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(TeamSkinShaderSpecLight) );
	case PS_TeamSkinShaderSpecLightAlpha:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(TeamSkinSpecLightAlpha) );
	case PS_SkinShaderOpacity:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(SkinShaderOpacity) );
	case PS_SkinShaderSpecLightOpacity:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(SkinShaderSpecLightOpacity) );
	case PS_TeamSkinShaderOpacity:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(TeamSkinShaderOpacity) );
	case PS_TeamSkinShaderSpecLightOpacity:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(TeamSkinShaderSpecLightOpacity) );
//scion jg -- Dual diffuse start
	case PS_DualDiffuse_NM_Add_NSL:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_NM_Add_NSL) );
	case PS_DualDiffuse_NM_Add_SL:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_NM_Add_SL) );
	case PS_DualDiffuse_NM_Mod_NSL:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_NM_Mod_NSL) );
	case PS_DualDiffuse_NM_Mod_SL:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_NM_Mod_SL) );
	case PS_DualDiffuse_M1_Add_NSL:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M1_Add_NSL) );
	case PS_DualDiffuse_M1_Add_SL:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M1_Add_SL) );
	case PS_DualDiffuse_M1_Mod_NSL:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M1_Mod_NSL) );
	case PS_DualDiffuse_M1_Mod_SL:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M1_Mod_SL) );
	case PS_DualDiffuse_M1_Blend_NSL:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M1_Blend_NSL) );
	case PS_DualDiffuse_M1_Blend_SL:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M1_Blend_SL) );
	case PS_DualDiffuse_M2_Add_NSL:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M2_Add_NSL) );
	case PS_DualDiffuse_M2_Add_SL:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M2_Add_SL) );
	case PS_DualDiffuse_M2_Mod_NSL:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M2_Mod_NSL) );
	case PS_DualDiffuse_M2_Mod_SL:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M2_Mod_SL) );
	case PS_DualDiffuse_M2_Blend_NSL:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M2_Blend_NSL) );
	case PS_DualDiffuse_M2_Blend_SL:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M2_Blend_SL) );
	case PS_DualDiffuse_NM_Add_NSL_SM:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_NM_Add_NSL_SM) );
	case PS_DualDiffuse_NM_Add_SL_SM:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_NM_Add_SL_SM) );
	case PS_DualDiffuse_NM_Mod_NSL_SM:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_NM_Mod_NSL_SM) );
	case PS_DualDiffuse_NM_Mod_SL_SM:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_NM_Mod_SL_SM) );
	case PS_DualDiffuse_M1_Add_NSL_SM:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M1_Add_NSL_SM) );
	case PS_DualDiffuse_M1_Add_SL_SM:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M1_Add_SL_SM) );
	case PS_DualDiffuse_M1_Mod_NSL_SM:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M1_Mod_NSL_SM) );
	case PS_DualDiffuse_M1_Mod_SL_SM:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M1_Mod_SL_SM) );
	case PS_DualDiffuse_M1_Blend_NSL_SM:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M1_Blend_NSL_SM) );
	case PS_DualDiffuse_M1_Blend_SL_SM:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M1_Blend_SL_SM) );
	case PS_DualDiffuse_M2_Add_NSL_SM:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M2_Add_NSL_SM) );
	case PS_DualDiffuse_M2_Add_SL_SM:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M2_Add_SL_SM) );
	case PS_DualDiffuse_M2_Mod_NSL_SM:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M2_Mod_NSL_SM) );
	case PS_DualDiffuse_M2_Mod_SL_SM:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M2_Mod_SL_SM) );
	case PS_DualDiffuse_M2_Blend_NSL_SM:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M2_Blend_NSL_SM) );
	case PS_DualDiffuse_M2_Blend_SL_SM:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DualDiffuse_M2_Blend_SL_SM) );
//scion jg -- Dual diffuse end
	case PS_DiffuseOpacitySpecularMask:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DiffuseOpacitySpecularMask) );
	case PS_DiffuseOpacitySpecularMaskSpecLight:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DiffuseOpacitySpecularMaskSpecLight) );
	case PS_DiffuseOpacitySpecularMask2:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DiffuseOpacitySpecularMask2) );
	case PS_DiffuseOpacitySpecularMaskSpecLight2:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DiffuseOpacitySpecularMaskSpecLight2) );
	case PS_ShenuSkin:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(ShenuSkin) );
	//scion jg -- Emissive character skin shaders
	case PS_EmissiveSkinShader:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(EmissiveSkinShader) );
	case PS_EmissiveSkinShaderSpecLight:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(EmissiveSkinShaderSpecLight) );
	case PS_EmissiveSkinShaderSpecLightAlpha:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(EmissiveSkinShaderSpecLightAlpha) );
	case PS_EmissiveShaderDiffuseOnly:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(EmissiveDiffuseOnly) );
	case PS_EmissiveShaderSpec:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(EmissiveShader) );
	case PS_EmissiveShaderSpecLight:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(EmissiveShaderSpecLight) );
	case PS_EmissiveConstColor:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(EmissiveConstColor) );
	case PS_EmissiveSpecular:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(EmissiveSpecular) );
	// pixel shaders to batch projectors
	case PS_Projector_Add_1:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(Proj_Add_1) );
	case PS_Projector_Add_2:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(Proj_Add_2) );
	case PS_Projector_Add_3:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(Proj_Add_3) );
	case PS_Projector_AlphaBlend_1:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(Proj_AlphaBlend_1) );
	case PS_Projector_AlphaBlend_2:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(Proj_AlphaBlend_2) );
	case PS_Projector_AlphaBlend_3:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(Proj_AlphaBlend_3) );
	case PS_Projector_Modulate_1:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(Proj_Modulate_1) );
	case PS_Projector_Modulate_2:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(Proj_Modulate_2) );
	case PS_Projector_Modulate_3:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(Proj_Modulate_3) );

	// pixel shaders to batch projectors and clip pixels using projector's frustum planes
	case PS_Projector_Add_Clip:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(Proj_Add_Clip) );
	case PS_Projector_AlphaBlend_Clip:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(Proj_AlphaBlend_Clip) );
	case PS_Projector_Modulate_Clip:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(Proj_Modulate_Clip) );

	//scion jg -- Team skinned world objects
	case PS_TeamShader:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(TeamShader) );
	case PS_TeamShaderSpecLight:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(TeamShaderSpecLight) );
	case PS_MaskedLuminance:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(MaskedLuminance) );
	case PS_PlayerCam:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(PlayerCam) );
	case PS_PlayerCamNoise:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(PlayerCamNoise) );
	case PS_PlayerCamTwoNoise:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(PlayerCamTwoNoise) );
	case PS_TeamShaderNoSpec:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(TeamShaderNoSpec) );
	case PS_TeamBSPShader:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(TeamBSPShader) );
	case PS_ShadowBlur:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(ShadowBlur) );
	case PS_UseTexIndexMasked:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(UseTexIndexMasked) );
	case PS_DiffuseOpacity:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DiffuseOpacity) );
	case PS_ColorRemap:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(ColorRemap) );
	case PS_ColorRemapMasked:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(ColorRemapMasked) );
	case PS_DepthSwizzleColor:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(DepthSwizzleColor) );
	case PS_ColorRemapGrayScale:
		return new FD3DPixelShader( this, Type, NULL, PS_SOURCE(ColorRemapGrayScale) );
	default:
		return NULL;
	}

#else
	// Create a new pixel shader.
	switch(Type)
	{
	case PS_Terrain3Layer:
		if( PixelShaderVersion >= 14 )
			return NEW FTerrain3LayerPixelShader14(this);
		else
			return NEW FTerrain3LayerPixelShader11(this);
	case PS_Terrain4Layer:
		return NEW FTerrain4LayerPixelShader14(this);
	case PS_Blur:
		return NEW FBlurPixelShader11(this);
//scion jg -- Skins start
	case PS_SkinShader:
		return NEW FSkinShader(this);
	case PS_SkinShaderSpecLight:
		return NEW FSkinShaderSpecLight(this);
	case PS_TeamSkinShader:
		return NEW FTeamSkinShader(this);
	case PS_TeamSkinShaderSpecLight:
		return NEW FTeamSkinShaderSpecLight(this);
	case PS_SkinShaderOpacity:
		return NEW FSkinShaderOpacity(this);
	case PS_SkinShaderSpecLightOpacity:
		return NEW FSkinShaderSpecLightOpacity(this);
	case PS_TeamSkinShaderOpacity:
		return NEW FTeamSkinShaderOpacity(this);
	case PS_TeamSkinShaderSpecLightOpacity:
		return NEW FTeamSkinShaderSpecLightOpacity(this);
//scion jg -- Skins end
//scion jg -- Dual diffuse start
	case PS_DualDiffuse_NM_Add_NSL:
		return NEW FDualDiffuse_NM_Add_NSL(this);
	case PS_DualDiffuse_NM_Add_SL:
		return NEW FDualDiffuse_NM_Add_SL(this);
	case PS_DualDiffuse_NM_Mod_NSL:
		return NEW FDualDiffuse_NM_Mod_NSL(this);
	case PS_DualDiffuse_NM_Mod_SL:
		return NEW FDualDiffuse_NM_Mod_SL(this);
	case PS_DualDiffuse_M1_Add_NSL:
		return NEW FDualDiffuse_M1_Add_NSL(this);
	case PS_DualDiffuse_M1_Add_SL:
		return NEW FDualDiffuse_M1_Add_SL(this);
	case PS_DualDiffuse_M1_Mod_NSL:
		return NEW FDualDiffuse_M1_Mod_NSL(this);
	case PS_DualDiffuse_M1_Mod_SL:
		return NEW FDualDiffuse_M1_Mod_SL(this);
	case PS_DualDiffuse_M1_Blend_NSL:
		return NEW FDualDiffuse_M1_Blend_NSL(this);
	case PS_DualDiffuse_M1_Blend_SL:
		return NEW FDualDiffuse_M1_Blend_SL(this);
	case PS_DualDiffuse_M2_Add_NSL:
		return NEW FDualDiffuse_M2_Add_NSL(this);
	case PS_DualDiffuse_M2_Add_SL:
		return NEW FDualDiffuse_M2_Add_SL(this);
	case PS_DualDiffuse_M2_Mod_NSL:
		return NEW FDualDiffuse_M2_Mod_NSL(this);
	case PS_DualDiffuse_M2_Mod_SL:
		return NEW FDualDiffuse_M2_Mod_SL(this);
	case PS_DualDiffuse_M2_Blend_NSL:
		return NEW FDualDiffuse_M2_Blend_NSL(this);
	case PS_DualDiffuse_M2_Blend_SL:
		return NEW FDualDiffuse_M2_Blend_SL(this);
	case PS_DualDiffuse_NM_Add_NSL_SM:
		return NEW FDualDiffuse_NM_Add_NSL_SM(this);
	case PS_DualDiffuse_NM_Add_SL_SM:
		return NEW FDualDiffuse_NM_Add_SL_SM(this);
	case PS_DualDiffuse_NM_Mod_NSL_SM:
		return NEW FDualDiffuse_NM_Mod_NSL_SM(this);
	case PS_DualDiffuse_NM_Mod_SL_SM:
		return NEW FDualDiffuse_NM_Mod_SL_SM(this);
	case PS_DualDiffuse_M1_Add_NSL_SM:
		return NEW FDualDiffuse_M1_Add_NSL_SM(this);
	case PS_DualDiffuse_M1_Add_SL_SM:
		return NEW FDualDiffuse_M1_Add_SL_SM(this);
	case PS_DualDiffuse_M1_Mod_NSL_SM:
		return NEW FDualDiffuse_M1_Mod_NSL_SM(this);
	case PS_DualDiffuse_M1_Mod_SL_SM:
		return NEW FDualDiffuse_M1_Mod_SL_SM(this);
	case PS_DualDiffuse_M1_Blend_NSL_SM:
		return NEW FDualDiffuse_M1_Blend_NSL_SM(this);
	case PS_DualDiffuse_M1_Blend_SL_SM:
		return NEW FDualDiffuse_M1_Blend_SL_SM(this);
	case PS_DualDiffuse_M2_Add_NSL_SM:
		return NEW FDualDiffuse_M2_Add_NSL_SM(this);
	case PS_DualDiffuse_M2_Add_SL_SM:
		return NEW FDualDiffuse_M2_Add_SL_SM(this);
	case PS_DualDiffuse_M2_Mod_NSL_SM:
		return NEW FDualDiffuse_M2_Mod_NSL_SM(this);
	case PS_DualDiffuse_M2_Mod_SL_SM:
		return NEW FDualDiffuse_M2_Mod_SL_SM(this);
	case PS_DualDiffuse_M2_Blend_NSL_SM:
		return NEW FDualDiffuse_M2_Blend_NSL_SM(this);
	case PS_DualDiffuse_M2_Blend_SL_SM:
		return NEW FDualDiffuse_M2_Blend_SL_SM(this);
//scion jg -- Dual diffuse end
	case PS_DiffuseOpacitySpecularMask:
		return NEW FDiffuseOpacitySpecularMask(this);
	case PS_DiffuseOpacitySpecularMaskSpecLight:
		return NEW FDiffuseOpacitySpecularMaskSpecLight(this);
	case PS_DiffuseOpacitySpecularMask2:
		return NEW FDiffuseOpacitySpecularMask2(this);
	case PS_DiffuseOpacitySpecularMaskSpecLight2:
		return NEW FDiffuseOpacitySpecularMaskSpecLight2(this);
	case PS_ShenuSkin:
		return NEW FShenuSkin(this);
	case PS_EmissiveConstColor:
		return NEW FEmissiveConstColorPS(this);
	//scion jg -- Team skinned world objects
	case PS_TeamShader:
		return NEW FTeamShader(this);
	case PS_TeamShaderSpecLight:
		return NEW FTeamShaderSpecLight(this);
	case PS_EmissiveShaderDiffuseOnly:
		return NEW FEmissiveDiffuseOnly(this);
	case PS_EmissiveShaderSpec:
		return NEW FEmissiveShader(this);
	case PS_EmissiveShaderSpecLight:
		return NEW FEmissiveShaderSpecLight(this);
	case PS_EmissiveSpecular:
		return NEW FEmissiveSpecular(this);
	case PS_MaskedLuminance:
		return NEW FMaskedLuminance(this);
	case PS_TeamShaderNoSpec:
		return NEW FTeamShaderNoSpec(this);
	case PS_TeamBSPShader:
		return NEW FTeamBSPShader(this);
	case PS_UseTexIndexMasked:
		return NEW FUseTexIndexMasked(this);
	case PS_DiffuseOpacity:
		return NEW FDiffuseOpacity(this);
	case PS_Distortion:
//	case PS_DistortionBlended:
	case PS_ModulatedDistortion:
	case PS_AdditiveDistortion:
	case PS_SubtractiveDistortion:
		return NEW FDistortionPS(this);
	case PS_ColorRemap:
	case PS_ColorRemapMasked:
	case PS_ColorRemapGrayScale:
	default:
		return NULL;
	}
#endif

	unguard;
}

#ifdef _XBOX
/**
 * Returns a pointer to the pixel shader microcode directly. This is safe
 * because it is part of our XBE and will never be deleted
 *
 * @param Type the shader program to get the pointer to
 */
D3DPIXELSHADERDEF* UD3DRenderDevice::GetPixelShaderProgram(EPixelShader Type)
{
	guard(UD3DRenderDevice::GetPixelShaderProgram);
	
	if( Type == PS_None )
		return NULL;
	// scion sz - Create a new pixel shader.
	switch(Type)
	{
	case PS_Terrain3Layer:
		return PS_MICROCODE(Terrain3layer);
	case PS_Blur:
		return PS_MICROCODE(Blur);
	case PS_DepthOfField:
		return PS_MICROCODE(DOF);
	case PS_Mask:
		return PS_MICROCODE(Mask);
	case PS_MotionBlur:
		return PS_MICROCODE(MotionBlur);
	case PS_Distortion:
		return PS_MICROCODE(Distortion);
	case PS_DistortionAccum:
		return PS_MICROCODE(DistortionAccum);
	case PS_Refraction:
		return PS_MICROCODE(Refraction);
	case PS_BloomSelect:
		return PS_MICROCODE(BloomSelect);
	case PS_EmissiveSelect:
		return PS_MICROCODE(EmissiveSelect);
	case PS_EmissiveSelectBlur:
		return PS_MICROCODE(EmissiveSelectBlur);
	case PS_LuminanceTexIndex:
		return PS_MICROCODE(LuminanceTexIndex);
	case PS_UseTexIndex:
		return PS_MICROCODE(UseTexIndex);
	case PS_ModulatedDistortion:
		return PS_MICROCODE(ModulatedDistortion);
	case PS_AdditiveDistortion:
		return PS_MICROCODE(AdditiveDistortion);
	case PS_SubtractiveDistortion:
		return PS_MICROCODE(SubtractiveDistortion);
	case PS_SkinShader:
		return PS_MICROCODE(SkinShader);
	case PS_SkinShaderSpecLight:
		return PS_MICROCODE(SkinShaderSpecLight);
	case PS_TeamSkinShader:
		return PS_MICROCODE(TeamSkinShader);
	case PS_TeamSkinShaderSpecLight:
		return PS_MICROCODE(TeamSkinShaderSpecLight);
	case PS_TeamSkinShaderSpecLightAlpha:
		return PS_MICROCODE(TeamSkinSpecLightAlpha);
	case PS_SkinShaderOpacity:
		return PS_MICROCODE(SkinShaderOpacity);
	case PS_SkinShaderSpecLightOpacity:
		return PS_MICROCODE(SkinShaderSpecLightOpacity);
	case PS_TeamSkinShaderOpacity:
		return PS_MICROCODE(TeamSkinShaderOpacity);
	case PS_TeamSkinShaderSpecLightOpacity:
		return PS_MICROCODE(TeamSkinShaderSpecLightOpacity);
//scion jg -- Dual diffuse start
	case PS_DualDiffuse_NM_Add_NSL:
		return PS_MICROCODE(DualDiffuse_NM_Add_NSL);
	case PS_DualDiffuse_NM_Add_SL:
		return PS_MICROCODE(DualDiffuse_NM_Add_SL);
	case PS_DualDiffuse_NM_Mod_NSL:
		return PS_MICROCODE(DualDiffuse_NM_Mod_NSL);
	case PS_DualDiffuse_NM_Mod_SL:
		return PS_MICROCODE(DualDiffuse_NM_Mod_SL);
	case PS_DualDiffuse_M1_Add_NSL:
		return PS_MICROCODE(DualDiffuse_M1_Add_NSL);
	case PS_DualDiffuse_M1_Add_SL:
		return PS_MICROCODE(DualDiffuse_M1_Add_SL);
	case PS_DualDiffuse_M1_Mod_NSL:
		return PS_MICROCODE(DualDiffuse_M1_Mod_NSL);
	case PS_DualDiffuse_M1_Mod_SL:
		return PS_MICROCODE(DualDiffuse_M1_Mod_SL);
	case PS_DualDiffuse_M1_Blend_NSL:
		return PS_MICROCODE(DualDiffuse_M1_Blend_NSL);
	case PS_DualDiffuse_M1_Blend_SL:
		return PS_MICROCODE(DualDiffuse_M1_Blend_SL);
	case PS_DualDiffuse_M2_Add_NSL:
		return PS_MICROCODE(DualDiffuse_M2_Add_NSL);
	case PS_DualDiffuse_M2_Add_SL:
		return PS_MICROCODE(DualDiffuse_M2_Add_SL);
	case PS_DualDiffuse_M2_Mod_NSL:
		return PS_MICROCODE(DualDiffuse_M2_Mod_NSL);
	case PS_DualDiffuse_M2_Mod_SL:
		return PS_MICROCODE(DualDiffuse_M2_Mod_SL);
	case PS_DualDiffuse_M2_Blend_NSL:
		return PS_MICROCODE(DualDiffuse_M2_Blend_NSL);
	case PS_DualDiffuse_M2_Blend_SL:
		return PS_MICROCODE(DualDiffuse_M2_Blend_SL);
	case PS_DualDiffuse_NM_Add_NSL_SM:
		return PS_MICROCODE(DualDiffuse_NM_Add_NSL_SM);
	case PS_DualDiffuse_NM_Add_SL_SM:
		return PS_MICROCODE(DualDiffuse_NM_Add_SL_SM);
	case PS_DualDiffuse_NM_Mod_NSL_SM:
		return PS_MICROCODE(DualDiffuse_NM_Mod_NSL_SM);
	case PS_DualDiffuse_NM_Mod_SL_SM:
		return PS_MICROCODE(DualDiffuse_NM_Mod_SL_SM);
	case PS_DualDiffuse_M1_Add_NSL_SM:
		return PS_MICROCODE(DualDiffuse_M1_Add_NSL_SM);
	case PS_DualDiffuse_M1_Add_SL_SM:
		return PS_MICROCODE(DualDiffuse_M1_Add_SL_SM);
	case PS_DualDiffuse_M1_Mod_NSL_SM:
		return PS_MICROCODE(DualDiffuse_M1_Mod_NSL_SM);
	case PS_DualDiffuse_M1_Mod_SL_SM:
		return PS_MICROCODE(DualDiffuse_M1_Mod_SL_SM);
	case PS_DualDiffuse_M1_Blend_NSL_SM:
		return PS_MICROCODE(DualDiffuse_M1_Blend_NSL_SM);
	case PS_DualDiffuse_M1_Blend_SL_SM:
		return PS_MICROCODE(DualDiffuse_M1_Blend_SL_SM);
	case PS_DualDiffuse_M2_Add_NSL_SM:
		return PS_MICROCODE(DualDiffuse_M2_Add_NSL_SM);
	case PS_DualDiffuse_M2_Add_SL_SM:
		return PS_MICROCODE(DualDiffuse_M2_Add_SL_SM);
	case PS_DualDiffuse_M2_Mod_NSL_SM:
		return PS_MICROCODE(DualDiffuse_M2_Mod_NSL_SM);
	case PS_DualDiffuse_M2_Mod_SL_SM:
		return PS_MICROCODE(DualDiffuse_M2_Mod_SL_SM);
	case PS_DualDiffuse_M2_Blend_NSL_SM:
		return PS_MICROCODE(DualDiffuse_M2_Blend_NSL_SM);
	case PS_DualDiffuse_M2_Blend_SL_SM:
		return PS_MICROCODE(DualDiffuse_M2_Blend_SL_SM);
//scion jg -- Dual diffuse end
	case PS_DiffuseOpacitySpecularMask:
		return PS_MICROCODE(DiffuseOpacitySpecularMask);
	case PS_DiffuseOpacitySpecularMaskSpecLight:
		return PS_MICROCODE(DiffuseOpacitySpecularMaskSpecLight);
	case PS_DiffuseOpacitySpecularMask2:
		return PS_MICROCODE(DiffuseOpacitySpecularMask2);
	case PS_DiffuseOpacitySpecularMaskSpecLight2:
		return PS_MICROCODE(DiffuseOpacitySpecularMaskSpecLight2);
	case PS_ShenuSkin:
		return PS_MICROCODE(ShenuSkin);
	//scion jg -- Emissive character skin shaders
	case PS_EmissiveSkinShader:
		return PS_MICROCODE(EmissiveSkinShader);
	case PS_EmissiveSkinShaderSpecLight:
		return PS_MICROCODE(EmissiveSkinShaderSpecLight);
	case PS_EmissiveSkinShaderSpecLightAlpha:
		return PS_MICROCODE(EmissiveSkinShaderSpecLightAlpha);
	case PS_EmissiveShaderDiffuseOnly:
		return PS_MICROCODE(EmissiveDiffuseOnly);
	case PS_EmissiveShaderSpec:
		return PS_MICROCODE(EmissiveShader);
	case PS_EmissiveShaderSpecLight:
		return PS_MICROCODE(EmissiveShaderSpecLight);
	case PS_EmissiveConstColor:
		return PS_MICROCODE(EmissiveConstColor);
	case PS_EmissiveSpecular:
		return PS_MICROCODE(EmissiveSpecular);
	// pixel shaders to batch projectors
	case PS_Projector_Add_1:
		return PS_MICROCODE(Proj_Add_1);
	case PS_Projector_Add_2:
		return PS_MICROCODE(Proj_Add_2);
	case PS_Projector_Add_3:
		return PS_MICROCODE(Proj_Add_3);
	case PS_Projector_AlphaBlend_1:
		return PS_MICROCODE(Proj_AlphaBlend_1);
	case PS_Projector_AlphaBlend_2:
		return PS_MICROCODE(Proj_AlphaBlend_2);
	case PS_Projector_AlphaBlend_3:
		return PS_MICROCODE(Proj_AlphaBlend_3);
	case PS_Projector_Modulate_1:
		return PS_MICROCODE(Proj_Modulate_1);
	case PS_Projector_Modulate_2:
		return PS_MICROCODE(Proj_Modulate_2);
	case PS_Projector_Modulate_3:
		return PS_MICROCODE(Proj_Modulate_3);

	// pixel shaders to batch projectors and clip pixels using projector's frustum planes
	case PS_Projector_Add_Clip:
		return PS_MICROCODE(Proj_Add_Clip);
	case PS_Projector_AlphaBlend_Clip:
		return PS_MICROCODE(Proj_AlphaBlend_Clip);
	case PS_Projector_Modulate_Clip:
		return PS_MICROCODE(Proj_Modulate_Clip);

	//scion jg -- Team skinned world objects
	case PS_TeamShader:
		return PS_MICROCODE(TeamShader);
	case PS_TeamShaderSpecLight:
		return PS_MICROCODE(TeamShaderSpecLight);
	case PS_MaskedLuminance:
		return PS_MICROCODE(MaskedLuminance);
	case PS_PlayerCam:
		return PS_MICROCODE(PlayerCam);
	case PS_PlayerCamNoise:
		return PS_MICROCODE(PlayerCamNoise);
	case PS_PlayerCamTwoNoise:
		return PS_MICROCODE(PlayerCamTwoNoise);
	case PS_TeamShaderNoSpec:
		return PS_MICROCODE(TeamShaderNoSpec);
	case PS_TeamBSPShader:
		return PS_MICROCODE(TeamBSPShader);
	case PS_ShadowBlur:
		return PS_MICROCODE(ShadowBlur);
	case PS_UseTexIndexMasked:
		return PS_MICROCODE(UseTexIndexMasked);
	case PS_DiffuseOpacity:
		return PS_MICROCODE(DiffuseOpacity);
	case PS_ColorRemap:
		return PS_MICROCODE(ColorRemap);
	case PS_ColorRemapMasked:
		return PS_MICROCODE(ColorRemapMasked);
	case PS_DepthSwizzleColor:
		return PS_MICROCODE(DepthSwizzleColor);
	case PS_ColorRemapGrayScale:
		return PS_MICROCODE(ColorRemapGrayScale);
	default:
		return NULL;
	}
	unguard;
}
#endif

// scion sz - not needed on Xbox
#ifndef _XBOX
//
//	FD3DFixedVertexShader
//
class FD3DFixedVertexShader : public FD3DVertexShader
{
public:
	FD3DFixedVertexShader(UD3DRenderDevice* InRenDev,FShaderDeclaration& InDeclaration) :
	  FD3DVertexShader(InRenDev,InDeclaration,VS_FixedFunction, NULL)  
	  {
	  }
};

//
//	FD3DSkinShader
//

class FD3DSkinVertexShader : public FD3DVertexShader
{
public:
	
	// #SKEL
	// Constructor/destructor.
	// FD3DSkinVertexShader( UD3DRenderDevice* InRenDev, FShaderDeclaration& InDeclaration );
	// :
	// FD3DVertexShader(InRenDev,InDeclaration,NULL)  
	//  {
	//	  Type = VS_SkeletalSkin;
	//  }

	public:
	FD3DSkinVertexShader( UD3DRenderDevice* InRenDev, FShaderDeclaration& InDeclaration) :	  
		FD3DVertexShader( InRenDev, InDeclaration, 	VS_SkelThreeBoned,


#if 0
		"vs.1.1\n"				// Required version number.				
		"m4x3	r4, v0, c[18]\n"    // First-bone only, used as local-to-world // Minimal alternative....	VSREG_BONEBASE	(18)
		"mov		r4.w, c[5].w \n"			// Necessary zeroing of r4.w  // VSREG_FIXEDCONST0  in  c[4]   FIXEDCONST1 in c[5]
		"m4x4	oPos,  r4, c[0]\n"	// World to clipspace. 
		//"m4x4	oPos, r4, c[0]\n"	// World to clipspace. 
		//"m4x4	oPos,v0,c[0]\n"	// World to clipspace.  directly -no bone
		"mov		oD0,c[9]\n"		// Color.
		"mov		oT0.xy,v2\n" 	// Tex coords.		
#endif		
				

#if 1
		"vs.1.1\n" // Required version number.						

		// retrieve bone index 0. Must be * 3 !
		"mov a0.x, v3.x\n"
		// transform with first bone\n"HACK: use c0
		"m4x3 r4, v0, c[a0.x + 18]\n"
		// use first for normal 
		"m4x3 r8, v1, c[a0.x + 18]\n"

		// retrieve bone index 1
		"mov a0.x, v3.y\n"
		// transform with second
		"m4x3 r5, v0, c[a0.x + 18]\n"
		// use second for normal
		"m4x3 r9, v1, c[a0.x + 18]\n"

		// retrieve bone index 2
		"mov a0.x, v3.z\n"
		// transform with third
		"m4x3 r6, v0, c[a0.x + 18]\n"
		// use third for normal
		"m4x3 r10, v1, c[a0.x + 18]\n"

		//
		///\n" retrieve bone index 3
		/// mov a0.x, v3.w
		///\n" transform with fourth
		/// m4x3 r7, v0, c[a0.x + 18]
		////
		/// use fourth for normal..
		/// m4x3 r11, v1, c[a0.x + 18]
		//
		//
		// Normal transformation: cheat, do only the first 2 bones' influence ?
		// otherwise you need special transform-a-vector code- though that's easy to do with just ignoring the offset???
		//
		//
		// weights - final vertex determination
		//
		/// r4 =   (v4.x * r4 ) + (v4.y * r4) + (v4.z * r4)

		"mul r4.xyzw, v4.x, r4.xyz\n"
		"mad r4.xyzw, v4.y, r5.xyzw, r4.xyzw\n"
		"mad r4.xyzw, v4.z, r6.xyzw, r4.xyzw\n"
		/// mul r7.xyzw, v4.w, r7.xyz  // unused fourth

		// Light-normal determination r8 = ...
		"mul r8.xyzw, v4.x,  r8.xyz\n"
		"mad r8.xyzw, v4.y,  r9.xyzw, r8.xyzw\n"
		"mad r8.xyzw, v4.z, r10.xyzw, r8.xyzw\n"
		/// mul r10.xyzw, v4.z, r10.xyz // unused fourth

		"mov r4.w, c[5].w \n" // Necessary. Avoid by smarter use of automatic expansion ?\n" // c[4] = zeroes  c[5] = ones

		// project world into clip space... => also does viewspace trafo.
		"m4x4 oPos,r4, c[0] \n" //

		// TEXTURE
		"mov oT0.xy, v2	\n"   // tex coords'

		// LIGHTING
		"sub r8.xyz, r8.xyz, r4.xyz\n"  // Normal = normalpoint - skinpoint

		// Normalize normal
		"dp3 r8.w, r8,r8  \n" // dot itself into w
		"rsq r8.w, r8.w   \n" // take square root
		"mul r8, r8, r8.w \n" // normalize by multiplying with length

		/// Now light 4 lights...  11 instructions/light with correct falloff. 

		// Read in constants - saves 3 instructions
		"mov r5, c[6] \n"   // c[6] :  1.5f, 0.5f, 1.0f, 0.2f   VSREG_FIXEDCONST2

		// LIGHT 0
		"sub  r0.xyz, c[10].xyz, r4.xyz   \n" // NormalVector LightVector  = Light->Location - Vert.Point
					    					// FLOAT   LightSize    = SqrtApprox( LightVector.SizeSquared()  )//   Actual distance
		"dp3  r0.w, r0, r0                \n" //     
		"rsq  r0.w, r0.w                  \n" // r0.w = 1/ LightSize //
		"dp3  r1.w, r0, r8                \n" // LightVector | Vertnormal  xyz
		"rcp  r2, r0.w                    \n" // r2(x=y=z=w)  = LightSize
		"mad  r1.w, r1.w, r0.w, r5.z\n"
		"mad  r1.w, r1.w, r1.w,-r5.x\n" // Square  - 1.5
					 						// G     = Square(1.0 + (LightVector | Vert.Normal) / LightSize) - 1.5//    ( r1.w = G)
											// G *= ( 1.0 - LightSize * Light->RRadius )//\n" RRadius = 1/actual radius.
		"mad  r1.x, r2.w, c[10].w, -r5.x \n" // c[10].w = light->RRadius\n"
		"mul  r1.w, r1.w, -r1.x          \n" // G *=...   
		"max  r9, r9, c[5]                \n" // Clamping necessary. VSREG_FIXEDCONST1 1,1,1,1
		"mul  r9.xyz, r1.w, c[11].xyz     \n" // multiply by actual light color.

		// LIGHT 1

		"sub  r0.xyz, c[12].xyz, r4.xyz   \n" // NormalVector LightVector  = Light->Location - Vert.Point
					    					// FLOAT   LightSize    = SqrtApprox( LightVector.SizeSquared()  )//   Actual distance
		"dp3  r0.w, r0, r0                \n" //
		"rsq  r0.w, r0.w                  \n" // r0.w = 1/ LightSize
		"dp3  r1.w, r0, r8                \n" // LightVector | Vertnormal  xyz
		"rcp  r2, r0.w                    \n" // r2(x=y=z=w)  = LightSize
		"mad  r1.w, r1.w, r0.w, r5.z\n"
		"mad  r1.w, r1.w, r1.w,-r5.x\n" // Square  - 1.5
					 						// G     = Square(1.0 + (LightVector | Vert.Normal) / LightSize) - 1.5//    ( r1.w = G)
											// G *= ( 1.0 - LightSize * Light->RRadius )//\n" RRadius = 1/actual radius.
		"mad  r1.x, r2.w, c[12].w, -r5.x      \n" // c[10].w = light->RRadius\n"
		"mul  r1.w, r1.w, -r1.x               \n" // G *=...   
		"max  r9, r9, c[5]                    \n" // Clamping necessary.
		"mad  r9.xyz, r1.w, c[13].xyz, r9.xyz \n" // multiply by actual light color\n"

		// LIGHT 2

		"sub  r0.xyz, c[14].xyz, r4.xyz   \n" // NormalVector LightVector  = Light->Location - Vert.Point\n"
					    					// FLOAT   LightSize    = SqrtApprox( LightVector.SizeSquared()  )//   Actual distance
		"dp3  r0.w, r0, r0                \n" //\n" //
		"rsq  r0.w, r0.w                  \n" // r0.w = 1/ LightSize //
		"dp3  r1.w, r0, r8                \n" // LightVector | Vertnormal  xyz\n"
		"rcp  r2, r0.w                    \n" // r2(x=y=z=w)  = LightSize
		"mad  r1.w, r1.w, r0.w, r5.z      \n"
		"mad  r1.w, r1.w, r1.w,-r5.x\n" // Square  - 1.5
 					 						// G     = Square(1.0 + (LightVector | Vert.Normal) / LightSize) - 1.5//    ( r1.w = G)
 											// G *= ( 1.0 - LightSize * Light->RRadius )//\n" RRadius = 1/actual radius.
		"mad  r1.x, r2.w, c[14].w, -r5.x      \n" // c[10].w = light->RRadius\n"
		"mul  r1.w, r1.w, -r1.x               \n" // G *=...   
		"max  r9, r9, c[5]                    \n" // Clamping necessary.
		"mad  r9.xyz, r1.w, c[15].xyz, r9.xyz \n" // multiply by actual light color\n"

		// LIGHT 3

		"sub  r0.xyz, c[16].xyz, r4.xyz   \n" // NormalVector LightVector  = Light->Location - Vert.Point\n"
					    					// FLOAT   LightSize    = SqrtApprox( LightVector.SizeSquared()  )//   Actual distance
		"dp3  r0.w, r0, r0                \n" //\n" //
		"rsq  r0.w, r0.w                  \n" // r0.w = 1/ LightSize //
		"dp3  r1.w, r0, r8                \n" // LightVector | Vertnormal  xyz\n"
		"rcp  r2, r0.w                    \n" // r2(x=y=z=w)  = LightSize
		"mad  r1.w, r1.w, r0.w, r5.z\n"
		"mad  r1.w, r1.w, r1.w,-r5.x\n" // Square  - 1.5
					 						// G     = Square(1.0 + (LightVector | Vert.Normal) / LightSize) - 1.5//    ( r1.w = G)
											// G *= ( 1.0 - LightSize * Light->RRadius )//\n" RRadius = 1/actual radius.
		
		"mad  r1.x, r2.w, c[16].w, -r5.x      \n" // c[10].w = light->RRadius\n"
		"mul  r1.w, r1.w, -r1.x               \n" // G *=...   
		"max  r9, r9, c[5]                    \n" // Clamping necessary.
		"mad  r9.xyz, r1.w, c[17].xyz, r9.xyz \n" // multiply by actual light color\n"		

		/// Finalize color - r9 is accumulator
		"add oD0 , r9.xyz, c[9].xyz \n" // add ambient and store.				
	/*
		Register usage for the vertex shader:
		
		#define VSREG_VIEWPROJ				(0)
		#define VSREG_FIXEDCONST0		(4)
		#define VSREG_FIXEDCONST1		(5)
		#define VSREG_FIXEDCONST2		(6)
		#define VSREG_AMBIENTRGB			(9)
		#define VSREG_LIGHTBASE				(10)
		#define VSREG_BONEBASE				(18)

	*/
		)		
	#endif	
		{

		}		
};
#endif //_XBOX

//
//	UD3DRenderDevice::GetVertexShader
//
FD3DVertexShader* UD3DRenderDevice::GetVertexShader(DWORD Type,FShaderDeclaration& Declaration)
{
	guard(UD3DRenderDevice::GetVertexShader);

	// Find an existing vertex shader with the same type/declaration.
	FD3DVertexShader*	ShaderPtr = VertexShaders;
	while(ShaderPtr)
	{
		if( ShaderPtr->Declaration == Declaration )
			return ShaderPtr;

		ShaderPtr = ShaderPtr->NextVertexShader;
	};        

	// Create a new vertex shader.
#ifdef _XBOX
	// scion sz - always just return an empty vertex shader since on Xbox shader programs are loaded
	// using the LoadVertexShaderProgram() API.  The vertex shader handle created here is still important
	// for setting the shader declaration.
	return new FD3DVertexShader( this, Declaration, Type, NULL );	
#else
	switch( Type )
	{	
	case VS_SkelThreeBoned: 
		{			
			debugf(NAME_DevD3D,TEXT("Creating new PC vertex shader"));
			return NEW FD3DSkinVertexShader(this,Declaration);	
		}
	case VS_FixedFunction:
	default:
		return NEW FD3DFixedVertexShader(this,Declaration);
	}
#endif

	unguard;
}

//	scion sz -
//	UD3DRenderDevice::GetCombinedVertexShader
//	
FD3DCombinedVertexShader* UD3DRenderDevice::GetCombinedVertexShader( QWORD VertexShaderEnum )
{
	guard(UD3DRenderDevice::GetCombinedVertexShader);

	// Try to find a match for this vertex shader enumeration in the TMap<> of shaders.
	FD3DCombinedVertexShader* pCombinedVertexShader = CombinedVertexShaders.FindRef( VertexShaderEnum );

	// If one is not found then create one for this VertexShaderEnum
	if( !pCombinedVertexShader )
	{
		pCombinedVertexShader = CombinedVertexShaders.Add( VertexShaderEnum, NEW FD3DCombinedVertexShader( this,VertexShaderEnum ) );

#ifndef FINAL_RELEASE
		// check to see if we are already precaching vertex shaders - no need to add to the config list if we are
		if( RenderInterface.GetPrecacheMode() != PRECACHE_VertexShaders &&
			UserVertexShaderConfigs )
		{
			// add a new entry for this microcode to this user's precache list of vertex shader configurations
			UserVertexShaderConfigs->AddVertexShaderEnum( VertexShaderEnum );
		}
#endif

	}

	return pCombinedVertexShader;

	unguard;
}

/* scion ======================================================================
 * UD3DRenderDevice::ResetRenderStates
 * Author: jg
 * 
 * Re-initializes the render states for the engine
 *
 * ============================================================================
 */
void UD3DRenderDevice::ResetRenderStates(void)
{
	guard(UD3DRenderDevice::ResetRenderStates);
#ifdef _XBOX
	// Make sure the GPU isn't doing anything before we start changing state
	Direct3DDevice8->BlockUntilIdle();
#endif
	// Use the common init code
	DeferredState.Init(this);
	unguard;
}