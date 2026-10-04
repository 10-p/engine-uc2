//-----------------------------------------------------------------------------
// File: XBResource.cpp
//
// Desc: Loads resources from an XPR (Xbox Packed Resource) file.  
//
// Hist: 03.12.01 - New for April XDK release
//       10.15.02 - Modifed to register resources in a more flexible way
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//-----------------------------------------------------------------------------
#include <xtl.h>
#include <xgraphics.h>
#include <stdio.h>
#include "XBUtil.h"

typedef BOOL UBOOL;

#include "PushBuffer.h"
#include "XBResource.h"


void ShowDirtyDiscError(void);


//-----------------------------------------------------------------------------
// Magic values to identify XPR files
//-----------------------------------------------------------------------------
#define XPR0_MAGIC_VALUE 0x30525058
#define XPR1_MAGIC_VALUE 0x31525058




//-----------------------------------------------------------------------------
// Name: XBResource_SizeOf()
// Desc: Determines the byte size of a D3DResource
//-----------------------------------------------------------------------------
DWORD XBResource_SizeOf( LPDIRECT3DRESOURCE8 pResource )
{
    switch( pResource->GetType() )
    {
        case D3DRTYPE_TEXTURE:       return sizeof(D3DTexture);
        case D3DRTYPE_VOLUMETEXTURE: return sizeof(D3DVolumeTexture);
        case D3DRTYPE_CUBETEXTURE:   return sizeof(D3DCubeTexture);
        case D3DRTYPE_VERTEXBUFFER:  return sizeof(D3DVertexBuffer);
        case D3DRTYPE_INDEXBUFFER:   return sizeof(D3DIndexBuffer);
        case D3DRTYPE_PALETTE:       return sizeof(D3DPalette);
    }
    return 0;
}




//-----------------------------------------------------------------------------
// Name: CXBPackedResource()
// Desc: Constructor
//-----------------------------------------------------------------------------
CXBPackedResource::CXBPackedResource()
{
    m_pSysMemData       = NULL;
    m_dwSysMemDataSize  = 0L;
    m_pVidMemData       = NULL;
    m_dwVidMemDataSize  = 0L;
    m_pResourceTags     = NULL;
    m_dwNumResourceTags = 0L;
}




//-----------------------------------------------------------------------------
// Name: ~CXBPackedResource()
// Desc: Destructor
//-----------------------------------------------------------------------------
CXBPackedResource::~CXBPackedResource()
{
    Destroy();
}




//-----------------------------------------------------------------------------
// Name: GetData()
// Desc: Loads all the texture resources from the given XPR.
//-----------------------------------------------------------------------------
VOID* CXBPackedResource::GetData( const CHAR* strName )
{
    if( NULL==m_pResourceTags || NULL==strName )
        return NULL;
	//jg -- Set the page settings for the memory if needed. This is a work
	// around to a stupid MS bug
	if (m_bNeedsDeferredPageSettings == TRUE)
	{
		if (m_pVidMemData != NULL)
		{
#if !LIN_SAVE
			//jg -- Change the memory page to be write combined if this contains
			// push buffers
			if (m_bContainsPushBuffers == TRUE)
			{
				XPhysicalProtect(m_pVidMemData,m_dwVidMemDataSize,
					PAGE_WRITECOMBINE | PAGE_READWRITE);
			}
			// jg -- Change the memory page access to be cached so we can read
			// from the memory without penalty
			else if (m_bNeedsCPUFastAccess == TRUE)
			{
				XPhysicalProtect(m_pVidMemData,m_dwVidMemDataSize,PAGE_READONLY);
			}
#endif
		}
		// Don't do this again
		m_bNeedsDeferredPageSettings = FALSE;
	}

	//jg -- Changed searching to search from the last known resource since
	// items are usually loaded in the same order they are exported
	for( DWORD i = m_dwLastFoundResource; m_pResourceTags[i].strName; i++ )
    {
        if( !_stricmp( strName, m_pResourceTags[i].strName ) )
		{
			m_dwLastFoundResource = i;
            return &m_pSysMemData[m_pResourceTags[i].dwOffset];
		}
    }

#if 0
	OutputDebugStringA("Starting search from start for: ");
    OutputDebugStringA(strName);
	OutputDebugStringA("\r\n");
#endif

	//jg -- It wasn't found so search from the start to the last known spot
	for( DWORD i = 0; i < m_dwLastFoundResource; i++ )
    {
        if( !_stricmp( strName, m_pResourceTags[i].strName ) )
		{
			m_dwLastFoundResource = i;
            return &m_pSysMemData[m_pResourceTags[i].dwOffset];
		}
    }

    return NULL;
}




//-----------------------------------------------------------------------------
// Name: Create()
// Desc: Loads all the texture resources from the given XPR.
//-----------------------------------------------------------------------------
HRESULT CXBPackedResource::Create( const CHAR* strFilename,
	BOOL bContainsPushBuffers,BOOL bNeedsCPUFastAccess,DWORD dwNumResourceTags,
                                   XBRESOURCE* pResourceTags )
{
    BOOL bHasResourceOffsetsTable = FALSE;
	//jg -- Start at the beginning of the list
	m_dwLastFoundResource = 0;
	m_bNeedsCPUFastAccess = bNeedsCPUFastAccess;
	m_bContainsPushBuffers = bContainsPushBuffers;
	m_bNeedsDeferredPageSettings = m_bNeedsCPUFastAccess || m_bContainsPushBuffers;

    // Find the media file
    CHAR strResourcePath[512];
    if( FAILED( XBUtil_FindMediaFile( strResourcePath, strFilename ) ) )
	{
		XBUtil_DebugPrint("Failed to find file %s\n",strFilename);
		//jg -- Universal disc error message
		ShowDirtyDiscError();
        return E_FAIL;
	}

    // Open the file
    HANDLE hFile;
    DWORD dwNumBytesRead;
    hFile = CreateFile( strResourcePath, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL );
    if( hFile == INVALID_HANDLE_VALUE )
    {
		XBUtil_DebugPrint("Failed to open file '%s' with %d\n",strFilename,GetLastError());
        return E_FAIL;
    }
   
    // Read in and verify the XPR magic header
    XPR_HEADER xprh;
    if (ReadFile( hFile, &xprh, sizeof(XPR_HEADER), &dwNumBytesRead, NULL ) == 0)
	{
		XBUtil_DebugPrint("Failed to read XPR_HEADER from file '%s' with %d\n",
			strFilename,GetLastError());
		//jg -- Universal disc error message
		ShowDirtyDiscError();
	}
    
    if( xprh.dwMagic == XPR0_MAGIC_VALUE )
    {
        bHasResourceOffsetsTable = FALSE;
    }
    else if( xprh.dwMagic == XPR1_MAGIC_VALUE )
    {
        bHasResourceOffsetsTable = TRUE;
    }
    else
    {
        CloseHandle( hFile );
        OUTPUT_DEBUG_STRING( "Invalid Xbox Packed Resource (.xpr) file" );
        return E_INVALIDARG;
    }

    // Compute memory requirements
    m_dwSysMemDataSize = xprh.dwHeaderSize - sizeof(XPR_HEADER);
    m_dwVidMemDataSize = xprh.dwTotalSize - xprh.dwHeaderSize;

    // Allocate memory
    m_pSysMemData = new BYTE[m_dwSysMemDataSize];
#ifndef FINAL_RELEASE
	if (m_pSysMemData == NULL)
	{
		XBUtil_DebugPrint("Failed to allocate sys memory for '%s'\n",strFilename);
		ShowDirtyDiscError();
	}
#endif
	// It's possible that a pushbuffer xpr contain no physical data
	if (m_dwVidMemDataSize > 0)
	{
#if LIN_SAVE
		if (m_bContainsPushBuffers == FALSE && m_bNeedsCPUFastAccess == FALSE)
		{
			m_pVidMemData = new BYTE[m_dwVidMemDataSize];
		}
		else
		{
			m_pVidMemData = (BYTE*)ExternalContigMalloc( m_dwVidMemDataSize, D3DTEXTURE_ALIGNMENT );
		}
#else
		//jg -- Align based upon type. Pushbuffers don't need 128 byte alignment
		if (m_bContainsPushBuffers == FALSE && m_bNeedsCPUFastAccess == FALSE)
		{
			m_pVidMemData = (BYTE*)ExternalContigMalloc( m_dwVidMemDataSize, D3DTEXTURE_ALIGNMENT );
		}
		else
		{
			m_pVidMemData = (BYTE*)ExternalContigMalloc( m_dwVidMemDataSize, D3DPUSHBUFFER_ALIGNMENT );
		}
#ifndef FINAL_RELEASE
		if (m_pVidMemData == NULL)
		{
			XBUtil_DebugPrint("Failed to allocate contig memory for '%s'\n",strFilename);
			ShowDirtyDiscError();
		}
#endif
#endif
	}
	else
	{
		m_pVidMemData = NULL;
	}

    // Read in the data from the file
    if (ReadFile( hFile, m_pSysMemData, m_dwSysMemDataSize, &dwNumBytesRead, NULL ) == 0)
	{
		XBUtil_DebugPrint("Failed to read system data from file '%s' with %d\n",
			strFilename,GetLastError());
		//jg -- Universal disc error message
		ShowDirtyDiscError();
	}
	if (m_dwVidMemDataSize > 0)
	{
		if (ReadFile( hFile, m_pVidMemData, m_dwVidMemDataSize, &dwNumBytesRead, NULL ) == 0)
		{
			XBUtil_DebugPrint("Failed to read contig data from file '%s' with %d\n",
				strFilename,GetLastError());
			//jg -- Universal disc error message
			ShowDirtyDiscError();
		}
	}

    // Done with the file
    CloseHandle( hFile );
    
    // Extract resource table from the header data
    if( bHasResourceOffsetsTable )
    {
        m_dwNumResourceTags = *(DWORD*)(m_pSysMemData+0);
        m_pResourceTags     = (XBRESOURCE*)(m_pSysMemData+4);

        // Patch up the resource strings
        for( DWORD i=0; i<m_dwNumResourceTags; i++ )
            m_pResourceTags[i].strName = (CHAR*)( m_pSysMemData + (DWORD)m_pResourceTags[i].strName );
    }

    // Use user-supplied number of resources and the resource tags
    if( dwNumResourceTags !=0 || pResourceTags != NULL )
    {
        m_pResourceTags     = pResourceTags;
        m_dwNumResourceTags = dwNumResourceTags;
    }

	if (m_dwVidMemDataSize > 0)
	{
#if !LIN_SAVE
		//jg -- Change the memory page to be write combined if this contains
		// push buffers
		if (bContainsPushBuffers == TRUE)
		{
			XPhysicalProtect(m_pVidMemData,m_dwVidMemDataSize,
				PAGE_WRITECOMBINE | PAGE_READWRITE);
		}
		// jg -- Change the memory page access to be cached so we can read
		// from the memory without penalty
		if (bNeedsCPUFastAccess == TRUE)
		{
			XPhysicalProtect(m_pVidMemData,m_dwVidMemDataSize,PAGE_READONLY);
		}
#endif
	}
    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: GetResourceTags()
// Desc: Retrieves the resource tags
//-----------------------------------------------------------------------------
HRESULT CXBPackedResource::GetResourceTags( DWORD* pdwNumResourceTags,
                                            XBRESOURCE** ppResourceTags )
{
    if( pdwNumResourceTags )
        (*pdwNumResourceTags) = m_dwNumResourceTags;

    if( ppResourceTags )
        (*ppResourceTags) = m_pResourceTags;

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: Destroy()
// Desc: Cleans up the packed resource data
//-----------------------------------------------------------------------------
VOID CXBPackedResource::Destroy() 
{
    if( m_pSysMemData != NULL )
        delete[] m_pSysMemData;
    m_pSysMemData      = NULL;
    m_dwSysMemDataSize = 0L;
    
#if LIN_SAVE
	if (m_pVidMemData != NULL && m_bContainsPushBuffers == FALSE &&
		m_bNeedsCPUFastAccess == FALSE)
	{
        delete [] m_pVidMemData;
	}
	else if (m_pVidMemData != NULL)
	{
        ExternalContigFree( m_pVidMemData );
	}
#else
	if (m_pVidMemData != NULL)
	{
        ExternalContigFree( m_pVidMemData );
	}
#endif
    m_pVidMemData      = NULL;
    m_dwVidMemDataSize = 0L;
    
    m_pResourceTags     = NULL;
    m_dwNumResourceTags = 0L;
}

