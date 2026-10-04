//-----------------------------------------------------------------------------
//
// jg -- Contains the TUmaArray specific logic for the bundler tool
//
// Copyright (c) Epic Games, Inc. All rights reserved.
//-----------------------------------------------------------------------------
#include "TUmaArray.h"
#include "Bundler.h"
#include <stdio.h>

//-----------------------------------------------------------------------------
// Name: CTUmaArray()
// Desc: Initializes member variables
//-----------------------------------------------------------------------------
CTUmaArray::CTUmaArray( CBundler* pBundler )
{
    m_pBundler = pBundler;
	m_dwSize = 0;
	m_pData = NULL;
}

//-----------------------------------------------------------------------------
// Name: ~CTUmaArray (destructor)
// Desc: Performs any cleanup needed before the object is destroyed
//-----------------------------------------------------------------------------
CTUmaArray::~CTUmaArray()
{
	// Release any allocated memory
	if (m_pData != NULL)
	{
		delete m_pData;
		m_pData = NULL;
	}
}

//-----------------------------------------------------------------------------
// Name: LoadTUmaArrayFromFile
// Desc: Loads an entire TUmaArray data portion from a file
//-----------------------------------------------------------------------------
HRESULT CTUmaArray::LoadTUmaArrayFromFile( CHAR* strFilename )
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
        m_pBundler->ErrorMsg( "Failed to read TUmaArray file %s with 0x%X\n",
			strFilename, GetLastError() );
        return E_FAIL;
	}

    m_dwSize = GetFileSize( hFile, NULL );

	// Allocate the buffer for that will hold the data
	m_pData = new BYTE[m_dwSize];

    // Read the data
    DWORD dwRead;
    ReadFile( hFile, m_pData, m_dwSize, &dwRead, NULL );
    if( dwRead != m_dwSize )
        return E_FAIL;

    CloseHandle( hFile );

    return S_OK;
}

//-----------------------------------------------------------------------------
// Name: SaveToBundle()
// Desc: Handles saving the appropriate data to the packed resource file
//-----------------------------------------------------------------------------
HRESULT CTUmaArray::SaveToBundle( DWORD * pcbHeader, DWORD * pcbData )
{
    HRESULT hr;

    if(m_pData == NULL)
    {
        m_pBundler->ErrorMsg( "No TUmaArray data specified\n" );
        return E_FAIL;
    }
    
    // Pad data file to proper alignment for the start of the array data
    hr = m_pBundler->PadToAlignment( 4 );
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
HRESULT CTUmaArray::SaveHeaderInfo( DWORD dwStart, DWORD * pcbHeader )
{
	FTUmaArrayHeader Header;
	// Use the count specified in the RDF
	Header.Count = m_dwCount;
	// And get the current start from the beginning of the data buffer
	Header.Data = m_pBundler->m_cbData;

    // Write the resource header out
    if( FAILED( m_pBundler->WriteHeader( &Header, sizeof( FTUmaArrayHeader ) ) ) )
        return E_FAIL;

    *pcbHeader = sizeof( FTUmaArrayHeader );

    return S_OK;
}

//-----------------------------------------------------------------------------
// Name: SavePushBufferData()
// Desc: Saves the raw VertexBuffer data to the XPR file
//-----------------------------------------------------------------------------
HRESULT CTUmaArray::SavePushBufferData( DWORD * pcbData )
{
    *pcbData = m_dwSize;
    m_pBundler->WriteData( m_pData, m_dwSize );
    return S_OK;
}
