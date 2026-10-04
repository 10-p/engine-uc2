//-----------------------------------------------------------------------------
// jg -- Header file containing class, struct, and constant definitions
//       for the TUmaArray related functionality of the bundler tool
//
// Copyright (c) Epic Games, Inc. All rights reserved.
//-----------------------------------------------------------------------------
#ifndef TUMAARRAY_H
#define TUMAARRAY_H

#include <windows.h>
#include <d3dx8.h>
#include <xgraphics.h>


class CBundler;

typedef struct
{
	// The number of items in the array
	DWORD Count;
	// The offset into to the physical memory buffer that the array lives at
	DWORD Data;
} FTUmaArrayHeader;

//-----------------------------------------------------------------------------
// Name: class CTUmaArray
// Desc: Handles all push buffer-specific processing
//-----------------------------------------------------------------------------
class CTUmaArray
{
public:
    CTUmaArray( CBundler * pBundler );
    ~CTUmaArray();

    HRESULT SaveToBundle( DWORD *pcbHeader, DWORD *pcbData );
    HRESULT LoadTUmaArrayFromFile( CHAR* strFilename );

    HRESULT SaveHeaderInfo( DWORD dwStart, DWORD * pcbHeader );
    HRESULT SavePushBufferData( DWORD * pcbData );

    CBundler * m_pBundler;
	// The raw data from the TUmaArray
	BYTE* m_pData;
	// The size of the data in bytes
	DWORD m_dwSize;
	// The number of elements in the array
	DWORD m_dwCount;
};


#endif