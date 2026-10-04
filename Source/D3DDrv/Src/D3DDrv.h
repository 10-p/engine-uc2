/*=============================================================================
	D3DDrv.cpp: Unreal Direct3D driver precompiled header generator.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by independent contractor who wishes to remain anonymous.
		* Taken over by Tim Sweeney.
=============================================================================*/

#ifndef HEADER_D3DDRV
#define HEADER_D3DDRV

#pragma warning(disable : 4291)
#pragma warning(disable : 4201)
#pragma warning(disable : 4714)

#ifndef D3DDRV_API
#ifndef _XBOX
#define D3DDRV_API DLL_IMPORT
#else
#define D3DDRV_API
#endif
#endif

// Windows/ XBox includes.
#ifdef _XBOX
	#if !defined(FINAL_RELEASE)
		#define DEBUG_KEYBOARD		//!!KEYBOARD HACK
	#endif

#include <xtl.h>

#include <xgraphics.h>
#else
#include <windows.h>
#include <objbase.h>
#endif

// Unreal includes.
#include "Engine.h"

// Direct3D includes.
#ifndef _XBOX
#define D3D_OVERLOADS 1
#include "d3d8.h"
#endif
#include "d3dx8.h"

//jg -- Include the PC version of these Xbox APIs
#if !defined(_XBOX) && defined(XBOX_ASSET_FORMAT)
#include "xgraphics.h"
#endif

class UD3DRenderDevice;

#define IGNOREPIX	0xfe0d
#define HIT_SIZE	8

//
//	D3DError
//	Returns a string describing the Direct3D error code given.
//
static inline FString D3DError( HRESULT h )
{
	#define D3DERR(x) case x: return TEXT(#x);
	switch( h )
	{
		D3DERR(D3D_OK)
		D3DERR(D3DERR_WRONGTEXTUREFORMAT)
		D3DERR(D3DERR_UNSUPPORTEDCOLOROPERATION)
		D3DERR(D3DERR_UNSUPPORTEDCOLORARG)
		D3DERR(D3DERR_UNSUPPORTEDALPHAOPERATION)
		D3DERR(D3DERR_UNSUPPORTEDALPHAARG)
		D3DERR(D3DERR_TOOMANYOPERATIONS)
		D3DERR(D3DERR_CONFLICTINGTEXTUREFILTER)
		D3DERR(D3DERR_UNSUPPORTEDFACTORVALUE)
		D3DERR(D3DERR_CONFLICTINGRENDERSTATE)
		D3DERR(D3DERR_UNSUPPORTEDTEXTUREFILTER)
		D3DERR(D3DERR_CONFLICTINGTEXTUREPALETTE)
		D3DERR(D3DERR_DRIVERINTERNALERROR)
		D3DERR(D3DERR_NOTFOUND)
		D3DERR(D3DERR_MOREDATA)
		D3DERR(D3DERR_DEVICELOST)
		D3DERR(D3DERR_DEVICENOTRESET)
		D3DERR(D3DERR_NOTAVAILABLE)
		D3DERR(D3DERR_OUTOFVIDEOMEMORY)
		D3DERR(D3DERR_INVALIDDEVICE)
		D3DERR(D3DERR_INVALIDCALL)
#ifndef _XBOX
		D3DERR(D3DERR_DRIVERINVALIDCALL)
#else
		D3DERR(D3DERR_TESTINCOMPLETE)
		D3DERR(D3DERR_BUFFERTOOSMALL)
#endif
		default: return FString::Printf(TEXT("%08X"),(INT)h);
	}
	#undef D3DERR
}

//
//	GetFormatBPP
//	Returns the number of bits/pixel used by a specified format.
//	If you add support for another format, you must add a case for it here.
//
static inline int GetFormatBPP(D3DFORMAT Format)
{
	switch ( Format )
	{
	case D3DFMT_A8R8G8B8:
	case D3DFMT_X8R8G8B8:
	case D3DFMT_D24S8:
#ifdef _XBOX
	case D3DFMT_LIN_A8R8G8B8:
	case D3DFMT_LIN_X8R8G8B8:		
#else
	case D3DFMT_Q8W8V8U8:
	case D3DFMT_X8L8V8U8:
	case D3DFMT_A2B10G10R10:
	case D3DFMT_V16U16:
	case D3DFMT_W11V11U10:
	case D3DFMT_G16R16:
	case D3DFMT_D24X4S4:
	case D3DFMT_D32:
	case D3DFMT_D24X8:
#endif
		return 32;

#ifndef _XBOX
	case D3DFMT_R8G8B8:
		return 24;
#endif

	case D3DFMT_X1R5G5B5:
	case D3DFMT_R5G6B5:
	case D3DFMT_A1R5G5B5:
	case D3DFMT_D16:
	case D3DFMT_A8L8:
	case D3DFMT_V8U8:
	case D3DFMT_A4R4G4B4:
	case D3DFMT_L6V5U5:
#ifdef _XBOX
	case D3DFMT_LIN_X1R5G5B5:
	case D3DFMT_LIN_R5G6B5:
#else
	case D3DFMT_A8P8:
	case D3DFMT_D16_LOCKABLE:
	case D3DFMT_X4R4G4B4:
	case D3DFMT_D15S1:
	case D3DFMT_A8R3G3B2:	
#endif
		return 16;

	case D3DFMT_P8:	
	case D3DFMT_A8:
	case D3DFMT_L8:	
	case D3DFMT_DXT3:
	case D3DFMT_DXT5:
#ifndef _XBOX
	case D3DFMT_DXT2:
	case D3DFMT_DXT4:
	case D3DFMT_R3G3B2:
	case D3DFMT_A4L4:
#endif
		return 8;

	case D3DFMT_DXT1:
		return 4;

	case D3DFMT_UYVY:
	case D3DFMT_YUY2:
	default:
		//appErrorf(TEXT("Unknown D3D format: %i"), Format );
		return 0;
	}
}

#include "D3DResource.h"
#include "D3DRenderInterface.h"
#include "D3DRenderState.h"
#include "D3DRenderDevice.h"

#endif
/*-----------------------------------------------------------------------------
	End.
-----------------------------------------------------------------------------*/
