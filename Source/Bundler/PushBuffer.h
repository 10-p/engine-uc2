//-----------------------------------------------------------------------------
// jg -- Header file containing class, struct, and constant definitions
//       for the push buffer-related functionality of the bundler tool
//
// Copyright (c) Epic Games, Inc. All rights reserved.
//-----------------------------------------------------------------------------
#ifndef PUSHBUFFER_H
#define PUSHBUFFER_H

#include <windows.h>
#include <d3dx8.h>
#include <xgraphics.h>


class CBundler;

// Xbox-specific defines, taken from d3d8types-xbox.h and d3d8-xbox.h

#define D3DPUSHBUFFER_ALIGNMENT     4

#define D3DCOMMON_TYPE_SHIFT         16
#define D3DCOMMON_TYPE_VERTEXBUFFER  0x00000000
#define D3DCOMMON_TYPE_INDEXBUFFER   0x00010000
#define D3DCOMMON_TYPE_PUSHBUFFER    0x00020000
#define D3DCOMMON_TYPE_PALETTE       0x00030000
#define D3DCOMMON_TYPE_TEXTURE       0x00040000
#define D3DCOMMON_TYPE_SURFACE       0x00050000
#define D3DCOMMON_VIDEOMEMORY        0x00800000

// For small pushbuffers
#define D3DPUSHBUFFER_RUN_USING_CPU_COPY 0x80000000

// Pushbuffer Format
typedef struct 
{
    DWORD Common;
    DWORD Size;
    DWORD AllocatedSize;
	DWORD Offset;
} XPRPushBuffer;

#define MIN_RUNNABLE_SIZE 2540

//-----------------------------------------------------------------------------
// Name: class CPushBuffer
// Desc: Handles all push buffer-specific processing
//-----------------------------------------------------------------------------
class CPushBuffer
{
public:
    CPushBuffer( CBundler * pBundler );
    ~CPushBuffer();

    HRESULT SaveToBundle( DWORD *pcbHeader, DWORD *pcbData );
    HRESULT LoadPushBufferFromFile( CHAR* strFilename );

    HRESULT SaveHeaderInfo( DWORD dwStart, DWORD * pcbHeader );
    HRESULT SavePushBufferData( DWORD * pcbData );

    CBundler * m_pBundler;
	// The push buffer data (allocated based upon the file size)
	BYTE* m_pPushBuffer;
	// The size of the push buffer
	DWORD m_dwSize;
	// The full size of the pushbuffer
	DWORD m_dwAllocationSize;
	// The file size for error checking
	DWORD m_dwFileSize;
};


#endif // PushBuffer_H