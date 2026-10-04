//-----------------------------------------------------------------------------
//
// jg -- Contains the push buffer-specific logic for the bundler tool
//
// Copyright (c) Epic Games, Inc. All rights reserved.
//-----------------------------------------------------------------------------
#include "PushBuffer.h"
#include "Bundler.h"
#include <stdio.h>

//-----------------------------------------------------------------------------
// Name: CPushBuffer()
// Desc: Initializes member variables
//-----------------------------------------------------------------------------
CPushBuffer::CPushBuffer( CBundler* pBundler )
{
    m_pBundler  = pBundler;
	m_dwSize = 0;
	m_pPushBuffer = NULL;
}

//-----------------------------------------------------------------------------
// Name: ~CPushBuffer (destructor)
// Desc: Performs any cleanup needed before the object is destroyed
//-----------------------------------------------------------------------------
CPushBuffer::~CPushBuffer()
{
	// Release any allocated memory
	if (m_pPushBuffer != NULL)
	{
		delete m_pPushBuffer;
		m_pPushBuffer = NULL;
	}
}

//-----------------------------------------------------------------------------
// Name: LoadPushBufferFromFile
// Desc: Loads entire vertex data stream from file
//-----------------------------------------------------------------------------
HRESULT CPushBuffer::LoadPushBufferFromFile( CHAR* strFilename )
{
    // Open the file
    HANDLE hFile = CreateFileA( strFilename, 
                                GENERIC_READ, 
                                FILE_SHARE_READ, 
                                NULL,
                                OPEN_EXISTING,
                                FILE_FLAG_SEQUENTIAL_SCAN,
                                NULL );
    if( INVALID_HANDLE_VALUE == hFile )
	{
        m_pBundler->ErrorMsg( "Failed to read pushbuffer file %s with 0x%X\n",
			strFilename, GetLastError() );
        return E_FAIL;
	}

    m_dwFileSize = GetFileSize( hFile, NULL );

	// Allocate the buffer for that will hold the data
	m_pPushBuffer = new BYTE[m_dwFileSize];

    // Read the data
    DWORD dwRead;
    ReadFile( hFile, m_pPushBuffer, m_dwFileSize, &dwRead, NULL );
    if( dwRead != m_dwFileSize )
        return E_FAIL;

    CloseHandle( hFile );

    return S_OK;
}

//-----------------------------------------------------------------------------
// Name: SaveToBundle()
// Desc: Handles saving the appropriate data to the packed resource file
//-----------------------------------------------------------------------------
HRESULT CPushBuffer::SaveToBundle( DWORD * pcbHeader, DWORD * pcbData )
{
    HRESULT hr;

    if(m_pPushBuffer == NULL)
    {
        m_pBundler->ErrorMsg( "No pushbuffer specified\n" );
        return E_FAIL;
    }
    
    // Pad data file to proper alignment for the start of the push buffer
    hr = m_pBundler->PadToAlignment( D3DPUSHBUFFER_ALIGNMENT );
    if( FAILED( hr ) )
        return hr;

    // Save resource header
    hr = SaveHeaderInfo( m_pBundler->m_cbData, pcbHeader );
    if( FAILED( hr ) )
        return hr;

    // Save push buffer data
    hr = SavePushBufferData( pcbData );
    if( FAILED( hr ) )
        return hr;
     
    return S_OK;
}

//-----------------------------------------------------------------------------
// Name: SaveHeaderInfo()
// Desc: Saves the appropriate data to the header file
//-----------------------------------------------------------------------------
HRESULT CPushBuffer::SaveHeaderInfo( DWORD dwStart, DWORD * pcbHeader )
{
	// Double check the data
	if (m_dwFileSize == m_dwAllocationSize)
	{
		XPRPushBuffer PushBuffer;
		// Initialize this the same way we would in engine
		PushBuffer.Common = 1 | D3DCOMMON_TYPE_PUSHBUFFER;
		PushBuffer.Size = m_dwSize;
		PushBuffer.AllocatedSize = m_dwAllocationSize;
		// Runnable versus copy push buffers
		if (m_dwSize < MIN_RUNNABLE_SIZE)
		{
			PushBuffer.Common |= D3DPUSHBUFFER_RUN_USING_CPU_COPY;
			// Point to the offset into the system data
			PushBuffer.Offset = sizeof(XPRPushBuffer);
			// Build one contiguous buffer and write that out
			DWORD TotalSize = sizeof(XPRPushBuffer) + m_dwAllocationSize;
			BYTE* Temp = new BYTE[TotalSize];
			memcpy(Temp,&PushBuffer,sizeof(XPRPushBuffer));
			memcpy(Temp + sizeof(XPRPushBuffer),m_pPushBuffer,m_dwAllocationSize);
			// Include the pushbuffer in system memory
			*pcbHeader = TotalSize;
			// Write the resource header out
			if( FAILED( m_pBundler->WriteHeader( Temp, TotalSize ) ) )
				return E_FAIL;
			delete Temp;
		}
		else
		{
			// Point to the offset into the contiguous data
			PushBuffer.Offset = m_pBundler->m_cbData;
			*pcbHeader = sizeof( XPRPushBuffer );
			// Write the resource header out
			if( FAILED( m_pBundler->WriteHeader( &PushBuffer, sizeof( XPRPushBuffer ) ) ) )
				return E_FAIL;
		}
	}
	else
	{
		m_pBundler->ErrorMsg("Pushbuffer sizes do not match.\n");
		return E_FAIL;
	}

    return S_OK;
}

//-----------------------------------------------------------------------------
// Name: SavePushBufferData()
// Desc: Saves the raw VertexBuffer data to the XPR file
//-----------------------------------------------------------------------------
HRESULT CPushBuffer::SavePushBufferData( DWORD * pcbData )
{
	// Runnable versus copy push buffers
	if (m_dwSize >= MIN_RUNNABLE_SIZE)
	{
		*pcbData = m_dwSize;
		m_pBundler->WriteData( m_pPushBuffer, m_dwSize );
	}
	else
	{
		// All of the data went in the system section
		*pcbData = 0;
	}
    return S_OK;
}
