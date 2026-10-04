//-----------------------------------------------------------------------------
// File: VolumeTexture.cpp
//
// Desc: Contains the VolumeTexture-specific logic for the bundler tool
//
// Hist: 04.11.01 - New for May XDK release
//       01.29.02 - Updated to avoid using D3D
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//-----------------------------------------------------------------------------
#include "VolumeTexture.h"
#include "Bundler.h"
#include <stdio.h>



//-----------------------------------------------------------------------------
// Name: CVolumeTexture()
// Desc: Initializes member variables
//-----------------------------------------------------------------------------
CVolumeTexture::CVolumeTexture( CBundler* pBundler )
               :CBaseTexture( pBundler )
{
    m_apImages        = NULL;
    m_pVolume         = NULL;
    m_astrSource      = NULL;
    m_astrAlphaSource = NULL;
    m_dwWidth         = 0L;
    m_dwHeight        = 0L;
    m_dwDepth         = 0L;
}




//-----------------------------------------------------------------------------
// Name: ~CVolumeTexture()
// Desc: Performs any cleanup needed before the object is destroyed
//-----------------------------------------------------------------------------
CVolumeTexture::~CVolumeTexture()
{
    if( m_apImages )
    {
        for( DWORD i = 0; i < m_dwDepth; i++ )
        {
                delete m_apImages[i];
        }
        delete[] m_apImages;
    }

    delete m_pVolume;
    m_pVolume = NULL;
    if( m_astrSource )
        delete[] m_astrSource;
    if( m_astrAlphaSource )
        delete[] m_astrAlphaSource;
}




//-----------------------------------------------------------------------------
// Name: SaveToBundle()
// Desc: Handles saving the appropriate data to the packed resource file
//-----------------------------------------------------------------------------
HRESULT CVolumeTexture::SaveToBundle( DWORD* pcbHeader, DWORD* pcbData )
{
    HRESULT hr;

    hr = LoadVolumeTexture();
    if( FAILED( hr ) )
        return hr;

    // Pad data file to proper alignment for the start of the texture
    hr = m_pBundler->PadToAlignment( D3DTEXTURE_ALIGNMENT );
    if( FAILED( hr ) )
        return hr;

    // Save resource header
    hr = SaveHeaderInfo( m_pBundler->m_cbData, pcbHeader );
    if( FAILED( hr ) )
        return hr;

    // Save voleume texture data
    (*pcbData) = 0;

    SaveVolumeTexture( pcbData, m_dwLevels, m_pVolume );

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: LoadVolumeTexture()
// Desc: Loads the texture from the file, and sets any properties that were
//       not specified with values from the file (width, height, format, etc)
//-----------------------------------------------------------------------------
HRESULT CVolumeTexture::LoadVolumeTexture()
{
    HRESULT hr;

    // Try to look up our format string
    m_nFormat = FormatFromString( m_strFormat );
    if( m_nFormat < -1 )
    {
        m_pBundler->ErrorMsg( "Invalid texture format: %s", m_strFormat );
        return E_FAIL;
    }
    lstrcpyA( m_strFormat, g_TextureFormats[m_nFormat].strFormat );

    // Make sure depth is a power of two for non-linear textures
    if( g_TextureFormats[m_nFormat].Type == FMT_LINEAR )
    {
        m_pBundler->ErrorMsg( "Volume textures must be swizzled or compressed.\n" );
        return E_FAIL;
    }

    // Make sure depth is a power of two
    DWORD dwDepth;
    for( dwDepth=1; dwDepth < m_dwDepth; dwDepth<<=1 );
    if( dwDepth != m_dwDepth )
    {
        m_pBundler->ErrorMsg( "Volume texture dimensions must be a power-of-two" );
        return E_FAIL;
    }

    if( m_dwDepth > 512 )
    {
        CHAR msg2[2048];
        ZeroMemory( msg2, 2048 );

        _snprintf( msg2, 2040, 
                    "Warning: Volume texture dimensions "
                    "must not Exceed 512.\n  Clamping Depth to 512\n" );
        m_pBundler->ErrorMsg( msg2 );
        m_dwDepth = 512;
    }

    // If an alpha source was specified, make sure the format supports alpha
    for( DWORD i=0; i<m_dwDepth; i++ )
    {
        if( m_astrAlphaSource[i][0] ) 
        {
            if( g_TextureFormats[m_nFormat].dwNumAlphaBits == 0 )
            {
                m_pBundler->WarningMsg( "an ALPHASOURCE was specified, yet the "
                                      "requested texture format, %s,  does not have "
                                      "any alpha.\n", m_strFormat );
            }
        }
    }

    // Load the image surfaces from the file (using default width, height, and a
    // A8R8G8B8 surface format)
    m_apImages = new CImage*[m_dwDepth];
    for( i = 0; i < m_dwDepth; i++ )
    {
        if( FAILED( hr = LoadImage( m_astrSource[i], m_astrAlphaSource[i], &m_apImages[i] ) ) )
        {
            if( m_astrAlphaSource[i] )
                m_pBundler->ErrorMsg( "Volume texture: Couldn't load source file <%s> or <%s>", m_astrSource[i], m_astrAlphaSource[i] );
            else
                m_pBundler->ErrorMsg( "Volume texture: Couldn't load source file <%s>", m_astrSource[i] );
            return hr;
        }
    }

    // If width or height wasn't specified, use dimensions from first source texture
    if( m_dwWidth == 0 )
    {
        DWORD dwWidth = m_apImages[0]->m_Width;

        // Enforce power-of-two dimensions
        for( m_dwWidth = 1;  m_dwWidth < dwWidth;  m_dwWidth <<= 1 );

        if( m_dwWidth > 512 )
        {
            CHAR msg2[2048];
            ZeroMemory( msg2, 2048 );

            _snprintf( msg2, 2040, 
                        "Warning: Volume texture dimensions "
                        "must not Exceed 512.\n  Clamping Width to 512\n" );
            m_pBundler->ErrorMsg( msg2 );
            m_dwWidth = 512;
        }
    }

    if( m_dwHeight == 0 )
    {
        DWORD dwHeight = m_apImages[0]->m_Height;

        // Enforce power-of-two dimensions
        for( m_dwHeight = 1; m_dwHeight < dwHeight; m_dwHeight <<= 1 );

        if( m_dwHeight > 512 )
        {
            CHAR msg2[2048];
            ZeroMemory( msg2, 2048 );

            _snprintf( msg2, 2040, 
                        "Warning: Volume texture dimensions "
                        "must not Exceed 512.\n  Clamping Height to 512\n" );
            m_pBundler->ErrorMsg( msg2 );
            m_dwHeight = 512;
        }
    }

    // Keep dimensions in check
    if( m_dwWidth>512 || m_dwHeight>512 || m_dwDepth>512 )
    {
        m_pBundler->ErrorMsg( "The max for volume texture dimensions is 512.\n" );
        return hr;
    }

    // Determine final number of miplevels
    DWORD dwLevels = 1; 
    DWORD dwMaxdim = ( m_dwHeight < m_dwWidth ) ? m_dwWidth : m_dwHeight;
    dwMaxdim = ( m_dwDepth < dwMaxdim ) ? dwMaxdim : m_dwDepth;

    while( ( 1UL<<( dwLevels-1 ) ) < dwMaxdim )
        dwLevels++;
    
    if( m_dwLevels < 1 || m_dwLevels > dwLevels )
        m_dwLevels = dwLevels;

    // Change the size of the surfaces
    for( i=0; i<m_dwDepth; i++ )
    {
        hr = ResizeImage( m_dwWidth, m_dwHeight, &m_apImages[i] );
    }

    m_pVolume = new CVolume( m_dwWidth, m_dwHeight, m_dwDepth, D3DFMT_A8R8G8B8 );

    BYTE* pDest = (BYTE *)m_pVolume->m_pData;

    for( i = 0; i < m_dwDepth; i++ )
    {
        memcpy( pDest, m_apImages[i]->m_pData, m_dwWidth * m_dwHeight * sizeof( DWORD ) );
        pDest += m_dwWidth * m_dwHeight * sizeof( DWORD );
    }

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: SaveHeaderInfo()
// Desc: Saves the appropriate data to the header file
//-----------------------------------------------------------------------------
HRESULT CVolumeTexture::SaveHeaderInfo( DWORD dwStart, DWORD* pcbHeader )
{
    D3DTexture d3dtex;

    XGSetVolumeTextureHeader( m_dwWidth, m_dwHeight, m_dwDepth, m_dwLevels, 0,
                              (D3DFORMAT)g_TextureFormats[m_nFormat].dwXboxFormat,
                              D3DPOOL_DEFAULT, (IDirect3DVolumeTexture8*)&d3dtex,
                              dwStart, 0 );

    // Overwrite the error in XGSetVolumeTextureHeader
    {
        for( DWORD LogWidth=0;  (1UL<<LogWidth)  <= m_dwWidth;  LogWidth++ )
        for( DWORD LogHeight=0; (1UL<<LogHeight) <= m_dwHeight; LogHeight++ )
        for( DWORD LogDepth=0;  (1UL<<LogDepth)  <= m_dwDepth;  LogDepth++ )
        d3dtex.Format =  3 << D3DFORMAT_DIMENSION_SHIFT /* UNDONE: verify */
                         | g_TextureFormats[m_nFormat].dwXboxFormat << D3DFORMAT_FORMAT_SHIFT
                         | m_dwLevels << D3DFORMAT_MIPMAP_SHIFT
                         | LogWidth   << D3DFORMAT_USIZE_SHIFT
                         | LogHeight  << D3DFORMAT_VSIZE_SHIFT
                         | LogDepth   << D3DFORMAT_PSIZE_SHIFT
                         | D3DFORMAT_DMACHANNEL_B
                         | D3DFORMAT_BORDERSOURCE_COLOR;
    }

    // Write the resource header out
    if( FAILED( m_pBundler->WriteHeader( &d3dtex, sizeof( d3dtex ) ) ) )
        return E_FAIL;

    (*pcbHeader) = sizeof(d3dtex);

    return S_OK;
}




