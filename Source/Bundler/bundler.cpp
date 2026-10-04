//-----------------------------------------------------------------------------
// File: Bundler.cpp
//
// Desc: Bundles up a list of resources into an Xbox Packed Resource (xpr)
//       file.
//
// Hist: 02.06.01 - New for March XDK release
//       01.29.02 - Updated to avoid using D3D
//       12.16.02 - Updated to output resource offsets table
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//-----------------------------------------------------------------------------
#include "Bundler.h"
#include "Texture.h"
#include "Cubemap.h"
#include "VolumeTexture.h"
#include "VB.h"
#include "UserData.h"
#include "IndexBuffer.h"
#include "PushBuffer.h"
#include "TUmaArray.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <delayimp.h>


#define INITIAL_HEADER_ALLOC 1024
#define INITIAL_DATA_ALLOC 65536

#define APPNAME "Bundler"


//jg -- Global to suppress log spam
BOOL bQuietMode;

//-----------------------------------------------------------------------------
// Name: CBundler()
// Desc: Sets initial values of member variables
//-----------------------------------------------------------------------------
CBundler::CBundler()
{
	//jg -- Use the format with named resource information
    m_dwOutputFileVersion = VERSION_XPR1;
    
    m_strRDF[0]    = '\0';
    m_strXPR[0]    = '\0';
    m_strHDR[0]    = '\0';
    m_strERR[0]    = '\0';
    m_strPrefix[0] = '\0';
    m_hfRDF    = INVALID_HANDLE_VALUE;
    m_hfXPR    = INVALID_HANDLE_VALUE;
    m_hfHDR    = INVALID_HANDLE_VALUE;
    m_hfERR    = INVALID_HANDLE_VALUE;
    m_bExplicitHDR    = FALSE;
    m_bExplicitXPR    = FALSE;
    m_bExplicitERR    = FALSE;
    m_bExplicitPrefix = FALSE;
    
    m_pbHeaderBuff = NULL;
    m_cbHeader     = 0;
    m_pbDataBuff   = NULL;
    m_cbData       = 0;
    
    m_dwOffsetsTableSize     = 0L;
    m_dwOffsetsTablePadBytes = 0L;
    m_dwBaseResourceOffset   = 0L;

    m_cNext0 = ' ';
    m_cNext1 = ' ';
    m_cNext2 = ' ';
    m_cNext3 = ' ';
    
    m_bSingleTexture = FALSE;
    m_bD3DCreated    = FALSE;

    m_pResources     = NULL;
    m_dwNumResources = 0;
    bQuietMode = FALSE;
}




//-----------------------------------------------------------------------------
// Name: ~CBundler()
// Desc: Performs any needed cleanup before shutting down the object
//-----------------------------------------------------------------------------
CBundler::~CBundler()
{
    CloseFiles();
    if( m_pbHeaderBuff )
        free( m_pbHeaderBuff );
    if( m_pbDataBuff )
        free( m_pbDataBuff );
    if( m_pResources )
        delete[] m_pResources;
}




//-----------------------------------------------------------------------------
// Name: PrintUsage()
// Desc: Prints out the usage string for help
//-----------------------------------------------------------------------------
void CBundler::PrintUsage()
{
    Msg( "Microsoft (R) Bundler Tool\n" );
    Msg( "Copyright (C) Microsoft Corporation 2000-2002. All rights reserved.\n" );
    Msg( "\n" );
    Msg( "usage:\n\n" );
    Msg( "  Bundler <resource description file> [-o <packed resource filename>]\n" );
    Msg( "                                      [-h <header filename>]\n");
    Msg( "                                      [-p <prefix for header file constants>]\n");
    Msg( "                                      [-e <error filename>]\n");
    Msg( "                                      [-q]\n");
    Msg( "                                      [-?]\n");
}




//-----------------------------------------------------------------------------
// Name: InternalPrintMessage()
// Desc: Helper function for ErrorMsg, WarningMsg and Msg
//-----------------------------------------------------------------------------
void CBundler::InternalPrintMessage( const char* strPrefix, const char* strFmt, va_list arglist )
{
    char strTemp[MAX_PATH];

    if( INVALID_HANDLE_VALUE == m_hfERR )
    {   
        m_hfERR = CreateFile( m_strERR, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL );
    }

    // Print to standard out.
    printf( "%s", strPrefix );
    vprintf( strFmt, arglist );

    if (INVALID_HANDLE_VALUE != m_hfERR)
    {
        // Have to use _vsnprintf to avoid potential buffer overruns.
        const int numElements = sizeof(strTemp) / sizeof(strTemp[0]);
        _vsnprintf( strTemp, numElements, strFmt, arglist );
        // _vsnprintf does not guarantee null-termination, so we have to explicitly force it.
        strTemp[numElements-1] = 0;
        DWORD cb;
        // Write the prefix.
        WriteFile( m_hfERR, strPrefix, strlen( strPrefix ), &cb, NULL );
        // Write the formatted data.
        WriteFile( m_hfERR, strTemp, strlen( strTemp ), &cb, NULL );
    }
}




//-----------------------------------------------------------------------------
// Name: ErrorMsg()
// Desc: Prints an error message to stdout, and writes to the error file
//-----------------------------------------------------------------------------
void CBundler::ErrorMsg( const char* strFmt, ... )
{
    va_list arglist;
    va_start( arglist, strFmt );

    // For Visual Studio to recognize the error message it must fit a certain
    // format. A tool name (or sourcefile(linenum)) then " : error :" or
    // ": warning :". See Kevin Perry for details on this format.
    InternalPrintMessage( APPNAME " : error : ", strFmt, arglist );

    va_end( arglist );
}




//-----------------------------------------------------------------------------
// Name: WarningMsg()
// Desc: Prints a warning message to stdout, and writes to the error file
//-----------------------------------------------------------------------------
void CBundler::WarningMsg( const char* strFmt, ... )
{
    va_list arglist;
    va_start( arglist, strFmt );

    // For Visual Studio to recognize the error message it must fit a certain
    // format. A tool name (or sourcefile(linenum)) then " : error :" or
    // ": warning :". See Kevin Perry for details on this format.
    InternalPrintMessage( APPNAME " : warning : ", strFmt, arglist );

    va_end( arglist );
}




//-----------------------------------------------------------------------------
// Name: Msg()
// Desc: Prints a message to stdout, and writes to the error file
//-----------------------------------------------------------------------------
void CBundler::Msg( const char* strFmt, ... )
{
    va_list arglist;
    va_start( arglist, strFmt );

    InternalPrintMessage( "", strFmt, arglist );

    va_end( arglist );
}




//-----------------------------------------------------------------------------
// Name: DelayLoadFailureHook
// Desc: This routine will be called by windows in the event of a failed
//       delay load of a DLL (in our case, D3D), so that we can print a useful
//       error message and abort
//-----------------------------------------------------------------------------
FARPROC WINAPI DelayLoadFailureHook( unsigned dliNotify, PDelayLoadInfo pdli )
{
    printf( "Fatal Error: You tried to load an image that requires the\n" );
    printf( "\tDirectX 8 runtime to load, but either DirectX 8 is not \n" );
    printf( "\tinstalled or couldn't be loaded.  Either convert the image to\n" );
    printf( "\ta Windows Bitmap (BMP), or install the DirectX 8 runtime.\n" );

    exit( EXIT_FAILURE );

    return NULL;
}




//-----------------------------------------------------------------------------
// Name: InitD3D()
// Desc: Initializes D3D for texture gunk
//-----------------------------------------------------------------------------
HRESULT CBundler::InitD3D()
{
    HRESULT hr;

    // If we've already init'ed D3D, then we're good to go
    if( m_bD3DCreated )
        return S_FALSE;

    // Set up our delay load failure hook in case DirectX 8 isn't present
    // When building with VS.Net, the delayimp files have changed to use
    // __pfnDliFailureHook2 instead of just __pfnDliFailureHook.  If you
    // need to build with VC6, undefine VSDOTNET in the project.
#ifdef VSDOTNET
    __pfnDliFailureHook2 = DelayLoadFailureHook;
#else
    __pfnDliFailureHook = DelayLoadFailureHook;
#endif

    m_pD3D = Direct3DCreate8( D3D_SDK_VERSION );
    if( NULL == m_pD3D )
    {
        ErrorMsg("Couldn't create Direct3d - is it installed?\n");
        return E_FAIL;
    }

    D3DDISPLAYMODE dispMode;
    m_pD3D->GetAdapterDisplayMode( D3DADAPTER_DEFAULT, &dispMode );

    D3DPRESENT_PARAMETERS presentParams;
    ZeroMemory( &presentParams, sizeof(presentParams) );
    presentParams.Windowed         = TRUE;
    presentParams.SwapEffect       = D3DSWAPEFFECT_COPY_VSYNC;
    presentParams.BackBufferWidth  = 8;
    presentParams.BackBufferHeight = 8;
    presentParams.BackBufferFormat = ( ( dispMode.Format == D3DFMT_R8G8B8 ) ? D3DFMT_R5G6B5 : dispMode.Format );

    hr = m_pD3D->CreateDevice( D3DADAPTER_DEFAULT, D3DDEVTYPE_REF,
                               GetDesktopWindow(), D3DCREATE_SOFTWARE_VERTEXPROCESSING, 
                               &presentParams, &m_pd3dDevice );
    if( FAILED(hr) )
    {
        ErrorMsg("Couldn't create reference device.  Error: %x\n", hr);
        return E_FAIL;
    }

    m_bD3DCreated = TRUE;

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: Initialize()
// Desc: Parses and validates the command line arguments
//-----------------------------------------------------------------------------
HRESULT CBundler::Initialize( int argc, char* argv[] )
{
    BOOL bHaveRDF = FALSE;

    for( int n=1; n<argc; n++ )
    {
        // Step through each command line parameter
        if( (argv[n][0] == '/') || (argv[n][0] == '-') )
        {
            if( lstrcmpi("q", &(argv[n][1])) == 0 )
            {
                bQuietMode = TRUE;
                continue; 
            }

            if( (n+1) == argc )
                return E_INVALIDARG;

            // Found an option
            if( lstrcmpi("o", &(argv[n][1])) == 0 )
            {
                // Output file
                lstrcpyA( m_strXPR, argv[n+1] );
                m_bExplicitXPR = TRUE;
            }
            else if( lstrcmpi("h", &(argv[n][1])) == 0 )
            {
                // Header file
                lstrcpyA( m_strHDR, argv[n+1] );
                m_bExplicitHDR = TRUE;
            }
            else if( lstrcmpi("p", &(argv[n][1])) == 0 )
            {
                // Prefix for header file constants
                lstrcpyA( m_strPrefix, argv[n+1] );
                m_bExplicitPrefix = TRUE;
            }
            else if( lstrcmpi("e", &(argv[n][1])) == 0 )
            {
                // Error file
                lstrcpyA( m_strERR, argv[n+1] );
                m_bExplicitERR = TRUE;
            }
            else if( lstrcmpi("?", &(argv[n][1])) == 0 )
                return E_INVALIDARG;    // Causes usage to be displayed
            else
                return E_INVALIDARG;
            
            n++;    // Skip two args
        }
        else
        {
            // Grab the RDF filename
            lstrcpyA( m_strRDF, argv[1] );
        
            // Find where the filename ends and the extension begins
            char* pch = m_strRDF;
            while( pch && strchr( pch, '.' ) )
            {
                pch = strchr( pch, '.' ) + 1;
            }
        
            // If we never found a '.', use the whole filename
            if( pch == m_strRDF )
                pch = m_strRDF + lstrlen( m_strRDF ) + 1;
        
            // Must have something for a base filename
            int nBaseFileLen = pch - m_strRDF - 1;
            if( nBaseFileLen <= 0 )
                return E_INVALIDARG;
        
            // Check to see if we are doing a quick single-texture conversion
            if( ( _stricmp(pch, "bmp") == 0 ) ||
                ( _stricmp(pch, "jpg") == 0 ) ||
                ( _stricmp(pch, "tga") == 0 ) ||
                ( _stricmp(pch, "dds") == 0 ) ||
                ( _stricmp(pch, "png") == 0 ) )
            {
                m_bSingleTexture = TRUE;
            }
            
            // Copy the base filename + appropriate extension to each string
            strncpy( m_strXPR, m_strRDF, nBaseFileLen );
            m_strXPR[nBaseFileLen] = '\0';
            strcat( m_strXPR, XPREXT );
            strncpy( m_strHDR, m_strRDF, nBaseFileLen );
            m_strHDR[nBaseFileLen] = '\0';
            strcat( m_strHDR, HDREXT );
            strncpy( m_strERR, m_strRDF, nBaseFileLen );
            m_strERR[nBaseFileLen] = '\0';
            strcat( m_strERR, ERREXT );

            strcpy( m_strPath, m_strXPR );
            if( strrchr( m_strPath, '\\' ) )
                strrchr( m_strPath, '\\' )[1] = 0;
            else
                m_strPath[0] = 0;

            bHaveRDF = TRUE;
        }
    }

    if( !bHaveRDF )
        return E_INVALIDARG;
 
    // Creating a D3D device sets the floating point control
    // word to single precision.  Now that we only create the
    // D3D device on demand, we _could_ use double precision,
    // but set to single precision to maintain bit-for-bit
    // output compatibility.
    _controlfp( _PC_24, _MCW_PC );

    // Allocate buffers for storing data
    m_pResources = new RESOURCE_STRUCT[MAX_RESOURCES];
    if( NULL == m_pResources )
        return E_OUTOFMEMORY;

    m_pbHeaderBuff = (BYTE *)malloc( INITIAL_HEADER_ALLOC );
    if( NULL == m_pbHeaderBuff )
        return E_OUTOFMEMORY;
    m_cbHeaderAlloc = INITIAL_HEADER_ALLOC;

    m_pbDataBuff = (BYTE *)malloc( INITIAL_DATA_ALLOC );
    if( NULL == m_pbDataBuff )
        return E_OUTOFMEMORY;
    m_cbDataAlloc = INITIAL_DATA_ALLOC;

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: OpenFiles()
// Desc: Opens file handles for all our output files
//-----------------------------------------------------------------------------
HRESULT CBundler::OpenFiles()
{
    m_hfXPR = CreateFile( m_strXPR, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL );
    if( INVALID_HANDLE_VALUE == m_hfXPR )
    {
        ErrorMsg( "Couldn't open XPR file <%s>", m_strXPR );
        return E_FAIL;
    }

    m_hfHDR = CreateFile( m_strHDR, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL );
    if( INVALID_HANDLE_VALUE == m_hfHDR )
    {
        ErrorMsg( "Couldn't open header file <%s>", m_strHDR );
        return E_FAIL;
    }

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: CloseFiles()
// Desc: Closes file handles for all our input/output files
//-----------------------------------------------------------------------------
HRESULT CBundler::CloseFiles()
{
    if( INVALID_HANDLE_VALUE != m_hfRDF )
        CloseHandle( m_hfRDF );
    if( INVALID_HANDLE_VALUE != m_hfXPR )
        CloseHandle( m_hfXPR );
    if( INVALID_HANDLE_VALUE != m_hfHDR )
        CloseHandle( m_hfHDR );
    if( INVALID_HANDLE_VALUE != m_hfERR )
        CloseHandle( m_hfERR );

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: FilterFromString()
// Desc: Returns a D3DXFILTER from the given string
//-----------------------------------------------------------------------------
DWORD CBundler::FilterFromString( char* strFilter )
{
    DWORD dwFilter  = D3DX_FILTER_TRIANGLE; // The default
    DWORD dwAddress = 0;
    DWORD dwDither  = 0;

    if( strFilter[0] )
    {
        // Scan string
        CHAR* strToken = strtok( strFilter, "|" );
        while( strToken != NULL )
        {
            if( !lstrcmpi( strToken, "NONE" ) )      dwFilter  = D3DX_FILTER_NONE;
            if( !lstrcmpi( strToken, "POINT" ) )     dwFilter  = D3DX_FILTER_POINT;
            if( !lstrcmpi( strToken, "LINEAR" ) )    dwFilter  = D3DX_FILTER_LINEAR;
            if( !lstrcmpi( strToken, "TRIANGLE" ) )  dwFilter  = D3DX_FILTER_TRIANGLE;
            if( !lstrcmpi( strToken, "BOX" ) )       dwFilter  = D3DX_FILTER_BOX;
            if( !lstrcmpi( strToken, "WRAP" ) )      dwAddress = 0;
            if( !lstrcmpi( strToken, "CLAMP" ) )     dwAddress = D3DX_FILTER_MIRROR;
            if( !lstrcmpi( strToken, "DITHER" ) )    dwDither  = D3DX_FILTER_DITHER;

            strToken = strtok( NULL, "|" );
        }
    }

    return (dwFilter|dwAddress|dwDither);
}




//-----------------------------------------------------------------------------
// Name: HandleTextureToken()
// Desc: Takes over to handle a texture token. Reads all tokens up to and
//       including the closing brace
//-----------------------------------------------------------------------------
HRESULT CBundler::HandleTextureToken()
{
    HRESULT      hr;
    BUNDLERTOKEN tok;
    char         strBuff[MAX_PATH];
    CTexture2D   tex(this);
    BOOL         bDone = FALSE;
    DWORD        cbHeader, cbData;

    // This is the texture name.
    hr = GetNextTokenString( m_pResources[m_dwNumResources].strIdentifier, TT_IDENTIFIER );
    if( FAILED(hr) )
        return hr;

    // Copy identifier to the name
    strcpy( m_pResources[m_dwNumResources].strName, m_pResources[m_dwNumResources].strIdentifier );

    // Check to see if it's a repeat identifier
    if( IsExistingIdentifier( m_pResources[m_dwNumResources].strIdentifier ) )
    {
        ErrorMsg( "Second usage of identifier <%s>", m_pResources[m_dwNumResources].strIdentifier );
        return E_FAIL;
    }

    // This should be the open brace
    hr = GetNextToken( &tok );
    if( FAILED(hr) )
        return hr;

    if( tok.eType != TOKEN_OPENBRACE )
    {
        ErrorMsg( "Texture name should be followed by an open brace\n" );
        return E_FAIL;
    }

    // Loop over all the properties
    while( !bDone )
    {
        hr = GetNextToken( &tok );
        if( FAILED(hr) )
            return hr;

        // Any property token needs a value
        if( tok.eType & TOKEN_PROPERTY )
        {
            hr = GetNextTokenString( strBuff, tok.propType );
            if( FAILED(hr) )
                return hr;
        }

        switch( tok.eType )
        {
            case TOKEN_PROPERTY_NAME:
                strcpy( m_pResources[m_dwNumResources].strName, strBuff );
                break;
            case TOKEN_PROPERTY_TEXTURE_SOURCE:
                strcpy( tex.m_strSource, strBuff );
                break;
            case TOKEN_PROPERTY_TEXTURE_ALPHASOURCE:
                strcpy( tex.m_strAlphaSource, strBuff );
                break;
            case TOKEN_PROPERTY_TEXTURE_FILTER:
                tex.m_dwFilter = FilterFromString( strBuff );
                break;
            case TOKEN_PROPERTY_TEXTURE_FORMAT:
                strcpy( tex.m_strFormat, strBuff );
                break;
            case TOKEN_PROPERTY_TEXTURE_WIDTH:
                tex.m_dwWidth = atoi( strBuff );
                break;
            case TOKEN_PROPERTY_TEXTURE_HEIGHT:
                tex.m_dwHeight = atoi( strBuff );
                break;
            case TOKEN_PROPERTY_TEXTURE_LEVELS:
                tex.m_dwLevels = atoi( strBuff );
                break;
            case TOKEN_PROPERTY_TEXTURE_RAW:
                tex.m_bLoadAndSaveRaw = atoi( strBuff );
                break;
            case TOKEN_CLOSEBRACE:
                bDone = TRUE;
                break;
            default:
                ErrorMsg("<%s> is not a texture property.\n", tok.strKeyword );
        }
    }

    // Save the texture to the bundle
    hr = tex.SaveToBundle( &cbHeader, &cbData );
    if( FAILED(hr) )
        return hr;

    if( !bQuietMode )
    {
        printf( "Texture: Wrote %s out in format %s (%d bytes)\n", 
                m_pResources[m_dwNumResources].strIdentifier, tex.m_strFormat, cbData );
        printf( "     %d x %d, %d levels\n\n", tex.m_dwWidth, tex.m_dwHeight, tex.m_dwLevels );
    }

    m_pResources[m_dwNumResources++].dwOffset = m_cbHeader;
    m_cbHeader += cbHeader;
    //m_cbData += cbData;

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: HandleCubemapToken()
// Desc: Takes over to handle a cubemap token. Reads all tokens up to and
//       including the closing brace
//-----------------------------------------------------------------------------
HRESULT CBundler::HandleCubemapToken()
{
    HRESULT      hr;
    BUNDLERTOKEN token;
    char         strBuffer[MAX_PATH];
    CCubemap     tex(this);
    BOOL         bDone = FALSE;
    DWORD        cbHeader;
    DWORD        cbData;

    // This is the texture name.
    hr = GetNextTokenString( m_pResources[m_dwNumResources].strIdentifier, TT_IDENTIFIER );
    if( FAILED(hr) )
        return hr;

    // Copy identifier to the name
    strcpy( m_pResources[m_dwNumResources].strName, m_pResources[m_dwNumResources].strIdentifier );

    // Check to see if it's a repeat identifier
    if( IsExistingIdentifier( m_pResources[m_dwNumResources].strIdentifier ) )
    {
        ErrorMsg( "Second usage of identifier <%s>", m_pResources[m_dwNumResources].strIdentifier );
        return E_FAIL;
    }

    // This should be the open brace
    hr = GetNextToken( &token );
    if( FAILED(hr) )
        return hr;

    if( token.eType != TOKEN_OPENBRACE )
    {
        ErrorMsg( "Cubemap name should be followed by an open brace\n" );
        return E_FAIL;
    }

    // Loop over all the properties
    while( !bDone )
    {
        hr = GetNextToken( &token );
        if( FAILED(hr) )
            return hr;

        // Any property token needs a value
        if( token.eType & TOKEN_PROPERTY )
        {
            hr = GetNextTokenString( strBuffer, token.propType );
            if( FAILED(hr) )
                return hr;
        }

        switch( token.eType )
        {
            case TOKEN_PROPERTY_NAME:
                strcpy( m_pResources[m_dwNumResources].strName, strBuffer );
                break;
            case TOKEN_PROPERTY_CUBEMAP_SOURCE_XP:
                strcpy( tex.m_strSourceXP, strBuffer );
                break;
            case TOKEN_PROPERTY_CUBEMAP_SOURCE_XN:
                strcpy( tex.m_strSourceXN, strBuffer );
                break;
            case TOKEN_PROPERTY_CUBEMAP_SOURCE_YP:
                strcat( tex.m_strSourceYP, strBuffer );
                break;
            case TOKEN_PROPERTY_CUBEMAP_SOURCE_YN:
                strcpy( tex.m_strSourceYN, strBuffer );
                break;
            case TOKEN_PROPERTY_CUBEMAP_SOURCE_ZP:
                strcpy( tex.m_strSourceZP, strBuffer );
                break;
            case TOKEN_PROPERTY_CUBEMAP_SOURCE_ZN:
                strcpy( tex.m_strSourceZN, strBuffer );
                break;
            case TOKEN_PROPERTY_CUBEMAP_ALPHASOURCE_XP:
                strcpy( tex.m_strAlphaSourceXP, strBuffer );
                break;
            case TOKEN_PROPERTY_CUBEMAP_ALPHASOURCE_XN:
                strcpy( tex.m_strAlphaSourceXN, strBuffer );
                break;
            case TOKEN_PROPERTY_CUBEMAP_ALPHASOURCE_YP:
                strcpy( tex.m_strAlphaSourceYP, strBuffer );
                break;
            case TOKEN_PROPERTY_CUBEMAP_ALPHASOURCE_YN:
                strcpy( tex.m_strAlphaSourceYN, strBuffer );
                break;
            case TOKEN_PROPERTY_CUBEMAP_ALPHASOURCE_ZP:
                strcpy( tex.m_strAlphaSourceZP, strBuffer );
                break;
            case TOKEN_PROPERTY_CUBEMAP_ALPHASOURCE_ZN:
                strcpy( tex.m_strAlphaSourceZN, strBuffer );
                break;
            case TOKEN_PROPERTY_TEXTURE_FILTER:
                tex.m_dwFilter = FilterFromString( strBuffer );
                break;
            case TOKEN_PROPERTY_TEXTURE_FORMAT:
                strcpy( tex.m_strFormat, strBuffer );
                break;
            case TOKEN_PROPERTY_CUBEMAP_SIZE:
                tex.m_dwSize = atoi( strBuffer );
                break;
            case TOKEN_PROPERTY_TEXTURE_LEVELS:
                tex.m_dwLevels = atoi( strBuffer );
                break;
            case TOKEN_PROPERTY_TEXTURE_RAW:
                tex.m_bLoadAndSaveRaw = atoi( strBuffer );
                break;
            case TOKEN_CLOSEBRACE:
                bDone = TRUE;
                break;
            default:
                ErrorMsg("<%s> is not a cubemap property.\n", token.strKeyword );
        }
    }

    // Save the texture to the bundle
    hr = tex.SaveToBundle( &cbHeader, &cbData );
    if( FAILED(hr) )
        return hr;

    if( !bQuietMode )
    {
        printf("Cubemap: Wrote %s out in format %s (%d bytes)\n", 
            m_pResources[m_dwNumResources].strIdentifier, tex.m_strFormat, cbData );
        printf("     6 x %d x %d, %d levels\n\n", tex.m_dwSize, tex.m_dwSize, tex.m_dwLevels);
    }

    m_pResources[m_dwNumResources++].dwOffset = m_cbHeader;
    m_cbHeader += cbHeader;
    //m_cbData += cbData;

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: HandleVolumeTextureToken()
// Desc: Takes over to handle a volume texture token. Reads all tokens up to
//       and including the closing brace
//-----------------------------------------------------------------------------
HRESULT CBundler::HandleVolumeTextureToken()
{
    HRESULT        hr;
    BUNDLERTOKEN   token;
    char           strBuffer[MAX_PATH];
    CVolumeTexture tex(this);
    BOOL           bDone = FALSE;
    DWORD          cbHeader;
    DWORD          cbData;
    DWORD          srcnum = 0;
    DWORD          alphasrcnum = 0;

    // This is the texture name.
    hr = GetNextTokenString( m_pResources[m_dwNumResources].strIdentifier, TT_IDENTIFIER );
    if( FAILED(hr) )
        return hr;

    // Copy identifier to the name
    strcpy( m_pResources[m_dwNumResources].strName, m_pResources[m_dwNumResources].strIdentifier );

    // Check to see if it's a repeat identifier
    if( IsExistingIdentifier( m_pResources[m_dwNumResources].strIdentifier ) )
    {
        ErrorMsg( "Second usage of identifier <%s>", m_pResources[m_dwNumResources].strIdentifier );
        return E_FAIL;
    }

    // This should be the open brace
    hr = GetNextToken( &token );
    if( FAILED(hr) )
        return hr;

    if( token.eType != TOKEN_OPENBRACE )
    {
        ErrorMsg( "Cubemap name should be followed by an open brace\n" );
        return E_FAIL;
    }

    // Loop over all the properties
    while( !bDone )
    {
        hr = GetNextToken( &token );
        if( FAILED(hr) )
            return hr;

        // Any property token needs a value
        if( token.eType & TOKEN_PROPERTY )
        {
            hr = GetNextTokenString( strBuffer, token.propType );
            if( FAILED(hr) )
                return hr;
        }

        switch( token.eType )
        {
            case TOKEN_PROPERTY_NAME:
                strcpy( m_pResources[m_dwNumResources].strName, strBuffer );
                break;
            case TOKEN_PROPERTY_TEXTURE_SOURCE:
                if( tex.m_dwDepth )
                {
                    if( srcnum <tex.m_dwDepth )
                        strcpy( tex.m_astrSource[srcnum++], strBuffer );
                    else
                        srcnum++;
                }
                else
                    ErrorMsg("Must specify volume texture DEPTH before SOURCE.\n" );
                break;
            case TOKEN_PROPERTY_TEXTURE_ALPHASOURCE:
                if( tex.m_dwDepth )
                {
                    if( alphasrcnum <tex.m_dwDepth )
                        strcpy( tex.m_astrAlphaSource[alphasrcnum++], strBuffer );
                    else
                        alphasrcnum++;
                }
                else
                    ErrorMsg("Must specify volume texture DEPTH before ALPHASOURCE.\n" );
                break;
            case TOKEN_PROPERTY_TEXTURE_FILTER:
                tex.m_dwFilter = FilterFromString( strBuffer );
                break;
            case TOKEN_PROPERTY_TEXTURE_FORMAT:
                strcpy( tex.m_strFormat, strBuffer );
                break;
            case TOKEN_PROPERTY_TEXTURE_WIDTH:
                tex.m_dwWidth = atoi( strBuffer );
                break;
            case TOKEN_PROPERTY_TEXTURE_HEIGHT:
                tex.m_dwHeight = atoi( strBuffer );
                break;
            case TOKEN_PROPERTY_VOLUMETEXTURE_DEPTH:
                if( 0 == tex.m_dwDepth )
                {   
                    tex.m_dwDepth         = atoi( strBuffer );
                    tex.m_astrSource      = new FILENAME[tex.m_dwDepth];
                    tex.m_astrAlphaSource = new FILENAME[tex.m_dwDepth];
                    ZeroMemory( tex.m_astrSource,      sizeof(FILENAME) * tex.m_dwDepth );
                    ZeroMemory( tex.m_astrAlphaSource, sizeof(FILENAME) * tex.m_dwDepth );
                }
                else
                    ErrorMsg("VolumeTexture DEPTH already specified.\n" );
                break;
            case TOKEN_PROPERTY_TEXTURE_LEVELS:
                tex.m_dwLevels = atoi( strBuffer );
                break;
            case TOKEN_PROPERTY_TEXTURE_RAW:
                tex.m_bLoadAndSaveRaw = atoi( strBuffer );
                break;
            case TOKEN_CLOSEBRACE:
                bDone = TRUE;
                break;
            default:
                ErrorMsg("<%s> is not a volume texture property.\n", token.strKeyword );
        }
    }

    // Check that we have a good number of sources
    if( ( srcnum != tex.m_dwDepth ) || ( alphasrcnum > 0  && alphasrcnum != tex.m_dwDepth ) )
    {   
        ErrorMsg("Volume texture must have same number of sources as the depth.\n" );
        return E_FAIL;
    }

    // Save the texture to the bundle
    hr = tex.SaveToBundle( &cbHeader, &cbData );
    if( FAILED(hr) )
        return hr;

    if( !bQuietMode )
    {
        printf("Volume texture: Wrote %s out in format %s (%d bytes)\n", 
            m_pResources[m_dwNumResources].strIdentifier, tex.m_strFormat, cbData );
        printf("     %d x %d x %d, %d levels\n\n", tex.m_dwWidth, tex.m_dwHeight, tex.m_dwDepth, tex.m_dwLevels);
    }

    m_pResources[m_dwNumResources++].dwOffset = m_cbHeader;
    m_cbHeader += cbHeader;
    //m_cbData += cbData;

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: Match()
// Desc: Returns TRUE if it successfuly reads a token of the specified type.
//-----------------------------------------------------------------------------
BOOL CBundler::Match(INT TokType, BUNDLERTOKEN *pTok)
{
    HRESULT hr;
    hr = GetNextToken( pTok );
    if( FAILED(hr) )
        return FALSE;

    if( pTok->eType != TokType )
        return FALSE;
    
    return TRUE;
}




//-----------------------------------------------------------------------------
// Name: HandleVertexBuffer()
// Desc: Takes over to handle a VertexBuffer token.  Reads all tokens up to and
//       including the closing brace
//-----------------------------------------------------------------------------
HRESULT CBundler::HandleVertexBufferToken()
{
    HRESULT hr;
    BUNDLERTOKEN tok;
    char strBuff[MAX_PATH], *pEnd;
    BOOL bDone = FALSE;
    DWORD cbHeader, cbData;

    // This is the vertex buffer name.
    hr = GetNextTokenString( m_pResources[m_dwNumResources].strIdentifier, TT_IDENTIFIER );
    if( FAILED(hr) )
        return hr;

    // Copy identifier to the name
    strcpy( m_pResources[m_dwNumResources].strName, m_pResources[m_dwNumResources].strIdentifier );

    // Check to see if it's a repeat identifier
    if( IsExistingIdentifier( m_pResources[m_dwNumResources].strIdentifier ) )
    {
        ErrorMsg( "Second usage of identifier <%s>", m_pResources[m_dwNumResources].strIdentifier );
        return E_FAIL;
    }

    if(!Match(TOKEN_OPENBRACE, &tok))
    {
        ErrorMsg( "Vertexbuffer name should be followed by an open brace\n" );
        return E_FAIL;
    }

    BOOL bGotVertexData = FALSE;
    CVertexBuffer *vb = new CVertexBuffer(this);
    
    // Loop over all the properties
    while( !bDone )
    {
        hr = GetNextToken( &tok );
        if( FAILED(hr) )
            return hr;

        switch( tok.eType )
        {
            case TOKEN_PROPERTY_NAME:
                hr = GetNextTokenString( strBuff, TT_ANY );
                if( FAILED(hr) )
                    return hr;
                strcpy( m_pResources[m_dwNumResources].strName, strBuff );
                break;
            case TOKEN_PROPERTY_VERTEXBUFFER_VERTEXFILE:
                // Only allow 1 instance of VertexData/VertexFile
                if( bGotVertexData )
                {
                    ErrorMsg( "Too many VertexData or DataFile statements\n" );
                    return E_FAIL;
                }
                bGotVertexData = TRUE;

                // Get the data file name
                hr = GetNextTokenString( strBuff, TT_FILENAME );
                if( FAILED(hr) )
                    return hr;

                // Load the data into our vb
                hr = vb->LoadVertexDataFromFile( strBuff );
                if( FAILED(hr) )
                    return hr;
            break;

            case TOKEN_PROPERTY_VERTEXBUFFER_VERTEXDATA:
                // Only allow 1 instance of VertexData/VertexFile
                if( bGotVertexData )
                {
                    ErrorMsg( "Too many VertexData or VertexFile statements\n" );
                    return E_FAIL;
                }
                bGotVertexData = TRUE;
                
                if(!Match(TOKEN_OPENBRACE, &tok))
                {
                    ErrorMsg( "VertexData property must begin with an open brace.\n" );
                    return E_FAIL;
                }
                while(TRUE)
                {
                    if(FAILED(hr = GetNextTokenString( strBuff, TT_ANY ))) return hr;
                
                    if((strBuff[0] == '}') || (strBuff[0] == TOKEOF))
                        break;
                    else if(ValidateType(strBuff, TT_HEXNUMBER) == S_OK)
                        vb->AddVertexData((DOUBLE)strtoul(strBuff, &pEnd, 16));
                    else if(ValidateType(strBuff, TT_INTEGER) == S_OK)
                        vb->AddVertexData(atof(strBuff));
                    else if(ValidateType(strBuff, TT_FLOAT) == S_OK)
                        vb->AddVertexData(atof(strBuff));
                }
                break;

            case TOKEN_PROPERTY_VERTEXBUFFER_VERTEXFORMAT:
                if(!Match(TOKEN_OPENBRACE, &tok))
                {
                    ErrorMsg( "VertexFormat property must begin with an open brace.\n" );
                    return E_FAIL;
                }
                while(TRUE)
                {
                    if(FAILED(hr = GetNextTokenString( strBuff, TT_ANY ))) return hr;
                
                    if((strBuff[0] == '}') || (strBuff[0] == TOKEOF))
                        break;
                    else
                    {
                        if((strcmp(strBuff, "D3DVSDT_FLOAT1") == 0) ||
                           (strcmp(strBuff, "FLOAT1") == 0))
                            vb->AddVertexFormat(XD3DVSDT_FLOAT1);
                        else if((strcmp(strBuff, "D3DVSDT_FLOAT2") == 0) ||
                                (strcmp(strBuff, "FLOAT2") == 0))
                            vb->AddVertexFormat(XD3DVSDT_FLOAT2);
                        else if((strcmp(strBuff, "D3DVSDT_FLOAT3") == 0) ||
                                (strcmp(strBuff, "FLOAT3") == 0))
                            vb->AddVertexFormat(XD3DVSDT_FLOAT3);
                        else if((strcmp(strBuff, "D3DVSDT_FLOAT4") == 0) ||
                                (strcmp(strBuff, "FLOAT4") == 0))
                            vb->AddVertexFormat(XD3DVSDT_FLOAT4);
                        else if((strcmp(strBuff, "D3DVSDT_D3DCOLOR") == 0) ||
                                (strcmp(strBuff, "D3DCOLOR") == 0))
                            vb->AddVertexFormat(XD3DVSDT_D3DCOLOR);
                        else if((strcmp(strBuff, "D3DVSDT_SHORT2") == 0) ||
                                (strcmp(strBuff, "SHORT2") == 0))
                            vb->AddVertexFormat(XD3DVSDT_SHORT2);
                        else if((strcmp(strBuff, "D3DVSDT_SHORT4") == 0) ||
                                (strcmp(strBuff, "SHORT4") == 0))
                            vb->AddVertexFormat(XD3DVSDT_SHORT4);
                        else if((strcmp(strBuff, "D3DVSDT_NORMSHORT1") == 0) ||
                                (strcmp(strBuff, "NORMSHORT1") == 0))
                            vb->AddVertexFormat(XD3DVSDT_NORMSHORT1);
                        else if((strcmp(strBuff, "D3DVSDT_NORMSHORT2") == 0) ||
                                (strcmp(strBuff, "NORMSHORT2") == 0))
                            vb->AddVertexFormat(XD3DVSDT_NORMSHORT2);
                        else if((strcmp(strBuff, "D3DVSDT_NORMSHORT3") == 0) ||
                                (strcmp(strBuff, "NORMSHORT3") == 0))
                            vb->AddVertexFormat(XD3DVSDT_NORMSHORT3);
                        else if((strcmp(strBuff, "D3DVSDT_NORMSHORT4") == 0) ||
                                (strcmp(strBuff, "NORMSHORT4") == 0))
                            vb->AddVertexFormat(XD3DVSDT_NORMSHORT4);
                        else if((strcmp(strBuff, "D3DVSDT_NORMPACKED3") == 0) ||
                                (strcmp(strBuff, "NORMPACKED3") == 0))
                            vb->AddVertexFormat(XD3DVSDT_NORMPACKED3);
                        else if((strcmp(strBuff, "D3DVSDT_SHORT1") == 0) ||
                                (strcmp(strBuff, "SHORT1") == 0))
                            vb->AddVertexFormat(XD3DVSDT_SHORT1);
                        else if((strcmp(strBuff, "D3DVSDT_SHORT3") == 0) ||
                                (strcmp(strBuff, "SHORT3") == 0))
                            vb->AddVertexFormat(XD3DVSDT_SHORT3);
                        else if((strcmp(strBuff, "D3DVSDT_PBYTE1") == 0) ||
                                (strcmp(strBuff, "PBYTE1") == 0))
                            vb->AddVertexFormat(XD3DVSDT_PBYTE1);
                        else if((strcmp(strBuff, "D3DVSDT_PBYTE2") == 0) ||
                                (strcmp(strBuff, "PBYTE2") == 0))
                            vb->AddVertexFormat(XD3DVSDT_PBYTE2);
                        else if((strcmp(strBuff, "D3DVSDT_PBYTE3") == 0) ||
                                (strcmp(strBuff, "PBYTE3") == 0))
                            vb->AddVertexFormat(XD3DVSDT_PBYTE3);
                        else if((strcmp(strBuff, "D3DVSDT_PBYTE4") == 0) ||
                                (strcmp(strBuff, "PBYTE4") == 0))
                            vb->AddVertexFormat(XD3DVSDT_PBYTE4);
                        else if((strcmp(strBuff, "D3DVSDT_FLOAT2H") == 0) ||
                                (strcmp(strBuff, "FLOAT2H") == 0))
                            vb->AddVertexFormat(XD3DVSDT_FLOAT2H);
                        else
                        {
                            ErrorMsg( "Unrecognized attribute format: %s\n", strBuff );
                            return E_FAIL;
                        }
                    }
                }
                break;

            case TOKEN_CLOSEBRACE:
                bDone = TRUE;
                break;
            default:
                ErrorMsg("<%s> is not a vertexbuffer property.\n", tok.strKeyword );
        }
    }

    // Save the vertexbuffer to the bundle
    hr = vb->SaveToBundle( &cbHeader, &cbData );
    if( FAILED(hr) )
        return hr;

    if( !bQuietMode )
    {
        printf("VB:  Wrote %s: %d bytes per vertex, %d vertices (%d bytes)\n\n", 
            m_pResources[m_dwNumResources].strIdentifier, vb->m_cBytesPerVertex,
            vb->m_cVertices, cbData );
    }

    m_pResources[m_dwNumResources++].dwOffset = m_cbHeader;
    m_cbHeader += cbHeader;
    //m_cbData += cbData;

    delete vb;

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: HandleUserDataToken()
// Desc: Takes over to handle a UserData token.  Reads all tokens up to and
//       including the closing brace
//-----------------------------------------------------------------------------
HRESULT CBundler::HandleUserDataToken()
{
    BUNDLERTOKEN token;
    CUserData    ud(this);
    CHAR         strBuff[MAX_PATH];
    BOOL         bDone = FALSE;
    DWORD        cbHeader, cbData;
    HRESULT      hr;

    // This is the texture name.
    hr = GetNextTokenString( m_pResources[m_dwNumResources].strIdentifier, TT_IDENTIFIER );
    if( FAILED(hr) )
        return hr;

    // Copy identifier to the name
    strcpy( m_pResources[m_dwNumResources].strName, m_pResources[m_dwNumResources].strIdentifier );

    // Check to see if it's a repeat identifier
    if( IsExistingIdentifier( m_pResources[m_dwNumResources].strIdentifier ) )
    {
        ErrorMsg( "Second usage of identifier <%s>", m_pResources[m_dwNumResources].strIdentifier );
        return E_FAIL;
    }

    // This should be the open brace
    hr = GetNextToken( &token );
    if( FAILED(hr) )
        return hr;

    if( token.eType != TOKEN_OPENBRACE )
    {
        ErrorMsg( "Texture name should be followed by an open brace\n" );
        return E_FAIL;
    }

    // Loop over all the properties
    while( !bDone )
    {
        hr = GetNextToken( &token );
        if( FAILED(hr) )
            return hr;

        // Any property token needs a value
        if( token.eType & TOKEN_PROPERTY )
        {
            hr = GetNextTokenString( strBuff, token.propType );
            if( FAILED(hr) )
                return hr;
        }

        switch( token.eType )
        {
            case TOKEN_PROPERTY_NAME:
                strcpy( m_pResources[m_dwNumResources].strName, strBuff );
                break;
            case TOKEN_PROPERTY_USERDATA_DATAFILE:
                strcpy( ud.m_strSource, strBuff );
                break;
            case TOKEN_CLOSEBRACE:
                bDone = TRUE;
                break;
            default:
                ErrorMsg("<%s> is not a userdata property.\n", token.strKeyword );
        }
    }

    // Save the UserData to the bundle
    hr = ud.SaveToBundle( &cbHeader, &cbData );
    if( FAILED(hr) )
        return hr;

    if( !bQuietMode )
    {
        printf( "UserData: Wrote %ld bytes.\n", cbHeader );
    }

    m_pResources[m_dwNumResources++].dwOffset = m_cbHeader;
    m_cbHeader += cbHeader;
    //m_cbData += cbData;

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: HandleIndexBufferToken()
// Desc: Takes over to handle a IndexBuffer token.  Reads all tokens up to and
//       including the closing brace
//-----------------------------------------------------------------------------
HRESULT CBundler::HandleIndexBufferToken()
{
    HRESULT hr;
    BUNDLERTOKEN tok;
    char strBuff[MAX_PATH];
    BOOL bDone = FALSE;
    DWORD cbHeader, cbData;

    // This is the vertex buffer name.
    hr = GetNextTokenString( m_pResources[m_dwNumResources].strIdentifier, TT_IDENTIFIER );
    if( FAILED(hr) )
        return hr;

    // Copy identifier to the name
    strcpy( m_pResources[m_dwNumResources].strName, m_pResources[m_dwNumResources].strIdentifier );

    // Check to see if it's a repeat identifier
    if( IsExistingIdentifier( m_pResources[m_dwNumResources].strIdentifier ) )
    {
        ErrorMsg( "Second usage of identifier <%s>", m_pResources[m_dwNumResources].strIdentifier );
        return E_FAIL;
    }

    if(!Match(TOKEN_OPENBRACE, &tok))
    {
        ErrorMsg( "IndexBuffer name should be followed by an open brace\n" );
        return E_FAIL;
    }

    CIndexBuffer *ib = new CIndexBuffer(this);
    BOOL bGotIndexData = FALSE;
    
    // Loop over all the properties
    while( !bDone )
    {
        hr = GetNextToken( &tok );
        if( FAILED(hr) )
            return hr;

        switch( tok.eType )
        {
            case TOKEN_PROPERTY_NAME:
                hr = GetNextTokenString( strBuff, TT_ANY );
                if( FAILED(hr) )
                    return hr;
                strcpy( m_pResources[m_dwNumResources].strName, strBuff );
                break;

            case TOKEN_PROPERTY_INDEXBUFFER_INDEXFILE:
                // Only allow 1 instance of IndexData/IndexFile
                if( bGotIndexData )
                {
                    ErrorMsg( "Too many IndexData or IndexFile statements\n" );
                    return E_FAIL;
                }
                bGotIndexData = TRUE;

                // Get the data file name
                hr = GetNextTokenString( strBuff, TT_FILENAME );
                if( FAILED(hr) )
                    return hr;

                // Load the data into our vb
                hr = ib->LoadIndicesFromFile( strBuff );
                if( FAILED(hr) )
                    return hr;
                break;

            case TOKEN_PROPERTY_INDEXBUFFER_INDEXDATA:
                // Only allow 1 instance of IndexData/IndexFile
                if( bGotIndexData )
                {
                    ErrorMsg( "Too many IndexData or IndexFile statements\n" );
                    return E_FAIL;
                }
                bGotIndexData = TRUE;
                
                if(!Match(TOKEN_OPENBRACE, &tok))
                {
                    ErrorMsg( "Index property must begin with an open brace.\n" );
                    return E_FAIL;
                }
                while(TRUE)
                {
                    if(FAILED(hr = GetNextTokenString( strBuff, TT_ANY ))) return hr;
                
                    if((strBuff[0] == '}') || (strBuff[0] == TOKEOF))
                        break;
                    else if( ValidateType( strBuff, TT_INTEGER ) == S_OK )
                        ib->AddIndex( (WORD)atol( strBuff ) );
                }
                break;

            case TOKEN_CLOSEBRACE:
                bDone = TRUE;
                break;
            default:
                ErrorMsg("<%s> is not a indexbuffer property.\n", tok.strKeyword );
        }
    }

    // Save the vertexbuffer to the bundle
    hr = ib->SaveToBundle( &cbHeader, &cbData );
    if( FAILED(hr) )
        return hr;

    if( !bQuietMode )
    {
        printf("IndexBuffer:  Wrote %d vertices (%d bytes)\n\n", ib->m_dwIndices,
                                                                cbData );
    }
            
    m_pResources[m_dwNumResources++].dwOffset = m_cbHeader;
    m_cbHeader += cbHeader;
    //m_cbData += cbData;

    delete ib;

    return S_OK;
}

/**
 * jg -- Handles loading a compiled push buffer from a file
 */
HRESULT CBundler::HandlePushBufferToken()
{
    HRESULT hr;
    BUNDLERTOKEN tok;
    char strBuff[MAX_PATH];
    BOOL bDone = FALSE;
    DWORD cbHeader, cbData;

    // This is the push buffer name.
    hr = GetNextTokenString( m_pResources[m_dwNumResources].strIdentifier, TT_IDENTIFIER );
    if( FAILED(hr) )
        return hr;

    // Copy identifier to the name
    strcpy( m_pResources[m_dwNumResources].strName, m_pResources[m_dwNumResources].strIdentifier );

    // Check to see if it's a repeat identifier
    if( IsExistingIdentifier( m_pResources[m_dwNumResources].strIdentifier ) )
    {
        ErrorMsg( "Second usage of identifier <%s>", m_pResources[m_dwNumResources].strIdentifier );
        return E_FAIL;
    }

    if(!Match(TOKEN_OPENBRACE, &tok))
    {
        ErrorMsg( "IndexBuffer name should be followed by an open brace\n" );
        return E_FAIL;
    }

    CPushBuffer* pb = new CPushBuffer(this);
    BOOL bGotPBData = FALSE;
    
    // Loop over all the properties
    while( !bDone )
    {
        hr = GetNextToken( &tok );
        if( FAILED(hr) )
            return hr;

        switch( tok.eType )
        {
            case TOKEN_PROPERTY_NAME:
                hr = GetNextTokenString( strBuff, TT_ANY );
                if( FAILED(hr) )
                    return hr;
                strcpy( m_pResources[m_dwNumResources].strName, strBuff );
                break;

            case TOKEN_PROPERTY_PUSHBUFFER_FILE:
                // Only allow 1 instance of File
                if( bGotPBData )
                {
                    ErrorMsg( "Too many File statements\n" );
                    return E_FAIL;
                }
                bGotPBData = TRUE;

                // Get the data file name
                hr = GetNextTokenString( strBuff, TT_FILENAME );
                if( FAILED(hr) )
                    return hr;

                // Load the data into our vb
                hr = pb->LoadPushBufferFromFile( strBuff );
                if( FAILED(hr) )
                    return hr;
                break;

            case TOKEN_PROPERTY_PUSHBUFFER_ALLOCSIZE:
				hr = GetNextTokenString( strBuff, tok.propType );
				if( FAILED(hr) )
					return hr;
                pb->m_dwAllocationSize = atoi( strBuff );
                break;

            case TOKEN_PROPERTY_PUSHBUFFER_SIZE:
				hr = GetNextTokenString( strBuff, tok.propType );
				if( FAILED(hr) )
					return hr;
                pb->m_dwSize = atoi( strBuff );
                break;

            case TOKEN_CLOSEBRACE:
                bDone = TRUE;
                break;
            default:
                ErrorMsg("<%s> is not a pushbuffer property.\n", tok.strKeyword );
        }
    }

    // Save the vertexbuffer to the bundle
    hr = pb->SaveToBundle( &cbHeader, &cbData );
    if( FAILED(hr) )
	{
		ErrorMsg("Failed to parse <%s>.\n",m_pResources[m_dwNumResources].strName);
		return hr;
	}

    if( !bQuietMode )
    {
        printf("PushBuffer:  Wrote pushbuffer (%d bytes)\n\n", cbData );
    }
            
    m_pResources[m_dwNumResources++].dwOffset = m_cbHeader;
    m_cbHeader += cbHeader;

    delete pb;

    return S_OK;
}

/**
 * jg -- Handles loading a TUmaArray from a file
 */
HRESULT CBundler::HandleTUmaArrayToken()
{
    HRESULT hr;
    BUNDLERTOKEN tok;
    char strBuff[MAX_PATH];
    BOOL bDone = FALSE;
    DWORD cbHeader, cbData;

    // This is the push buffer name.
    hr = GetNextTokenString( m_pResources[m_dwNumResources].strIdentifier, TT_IDENTIFIER );
    if( FAILED(hr) )
        return hr;

    // Copy identifier to the name
    strcpy( m_pResources[m_dwNumResources].strName, m_pResources[m_dwNumResources].strIdentifier );

    // Check to see if it's a repeat identifier
    if( IsExistingIdentifier( m_pResources[m_dwNumResources].strIdentifier ) )
    {
        ErrorMsg( "Second usage of identifier <%s>", m_pResources[m_dwNumResources].strIdentifier );
        return E_FAIL;
    }

    if(!Match(TOKEN_OPENBRACE, &tok))
    {
        ErrorMsg( "IndexBuffer name should be followed by an open brace\n" );
        return E_FAIL;
    }

    CTUmaArray* tuma = new CTUmaArray(this);
    BOOL bGotData = FALSE;
    
    // Loop over all the properties
    while( !bDone )
    {
        hr = GetNextToken( &tok );
        if( FAILED(hr) )
            return hr;

        switch( tok.eType )
        {
            case TOKEN_PROPERTY_NAME:
                hr = GetNextTokenString( strBuff, TT_ANY );
                if( FAILED(hr) )
                    return hr;
                strcpy( m_pResources[m_dwNumResources].strName, strBuff );
                break;

            case TOKEN_PROPERTY_TUMAARRAY_FILE:
                // Only allow 1 instance of File
                if( bGotData )
                {
                    ErrorMsg( "Too many File statements\n" );
                    return E_FAIL;
                }
                bGotData = TRUE;

                // Get the data file name
                hr = GetNextTokenString( strBuff, TT_FILENAME );
                if( FAILED(hr) )
                    return hr;

                // Load the data into our object
                hr = tuma->LoadTUmaArrayFromFile( strBuff );
                if( FAILED(hr) )
                    return hr;
                break;

            case TOKEN_PROPERTY_TUMAARRAY_COUNT:
				hr = GetNextTokenString( strBuff, tok.propType );
				if( FAILED(hr) )
					return hr;
                tuma->m_dwCount = atoi( strBuff );
                break;

			case TOKEN_CLOSEBRACE:
                bDone = TRUE;
                break;
            default:
                ErrorMsg("<%s> is not a TUmaArray property.\n", tok.strKeyword );
        }
    }

    // Save the TUmaArray to the bundle
    hr = tuma->SaveToBundle( &cbHeader, &cbData );
    if( FAILED(hr) )
        return hr;

    if( !bQuietMode )
    {
        printf("TUmaArray:  Wrote array data (%d bytes)\n\n", cbData );
    }
            
    m_pResources[m_dwNumResources++].dwOffset = m_cbHeader;
    m_cbHeader += cbHeader;

    delete tuma;

    return S_OK;
}

//-----------------------------------------------------------------------------
// Name: FlushDataFile()
// Desc: Writes out all the buffered data to the data file, including
//       appropriate padding
//-----------------------------------------------------------------------------
HRESULT CBundler::FlushDataFile()
{
    BOOL bWriteOK;
    DWORD cb;
    XPR_HEADER xprh;
    BYTE * pbTemp;
    DWORD cbFill = 0;

    // Pad the data buffer out to be a multiple of the DVD sector size (2k).
    // This is needed in order to DMA the file off the DVD drive on the
    // Xbox.
    PadToAlignment( 2048 );

    // Choose the magic value to identify the file. XPR0 is the default (version 0)
    // format, where XPR1 (version1) embeds an offsets table.
    switch( m_dwOutputFileVersion )
    {
        case VERSION_XPR0:
            xprh.dwMagic = XPR0_MAGIC_VALUE;
            break;
        case VERSION_XPR1:
            xprh.dwMagic = XPR1_MAGIC_VALUE;
            break;
    }

    // Compute size of resource table
    m_dwOffsetsTableSize     = 0L;
    m_dwOffsetsTablePadBytes = 0L;
    m_dwBaseResourceOffset   = 0L;

    if( m_dwOutputFileVersion >= VERSION_XPR1 )
    {
        // Add offsets table
        m_dwOffsetsTableSize = 4 + 8*m_dwNumResources + 4;

        // Add string space
        for( DWORD i = 0; i < m_dwNumResources; i++ )
            m_dwOffsetsTableSize += strlen(m_pResources[i].strName)+1;

        // Pad to 16 bytes
        m_dwOffsetsTablePadBytes = (16-(m_dwOffsetsTableSize%16))%16;
        m_dwOffsetsTableSize += m_dwOffsetsTablePadBytes;

        // The base resource offset just skips past the size of the table
        m_dwBaseResourceOffset = m_dwOffsetsTableSize;
    }

    // Calculate header size
    xprh.dwHeaderSize = sizeof(XPR_HEADER) + m_dwOffsetsTableSize + m_cbHeader;

    // We may need to pad the file to a sector multiple for the 
    // start of the data block
    if( xprh.dwHeaderSize % 2048 )
    {
        cbFill = 2048 - ( xprh.dwHeaderSize % 2048 );
        xprh.dwHeaderSize += cbFill;
    }
    xprh.dwTotalSize = xprh.dwHeaderSize + m_cbData;

    // Write out header stuff
    if( !WriteFile( m_hfXPR, &xprh, sizeof(XPR_HEADER), &cb, NULL ) )
        return E_FAIL;

    // Write out the resource offsets table
    if( m_dwOutputFileVersion >= VERSION_XPR1 )
    {
        // Write out the number of resources
        DWORD dwNumResources = m_dwNumResources;
        if( !WriteFile( m_hfXPR, &m_dwNumResources, sizeof(DWORD), &cb, NULL ) )
            return E_FAIL;

        // Write out the offsets of each resource
        DWORD dwStringBase = m_dwNumResources*(sizeof(CHAR*)+sizeof(DWORD)) + sizeof(DWORD) + sizeof(CHAR*);

        for( DWORD i = 0; i < m_dwNumResources; i++ )
        {
            // Write offset to string
            if( !WriteFile( m_hfXPR, &dwStringBase, sizeof(CHAR*), &cb, NULL ) )
                return E_FAIL;
            dwStringBase += strlen(m_pResources[i].strName)+1;

            // Write offset
            DWORD dwOffset = m_pResources[i].dwOffset + m_dwBaseResourceOffset;
            if( !WriteFile( m_hfXPR, &dwOffset, sizeof(DWORD), &cb, NULL ) )
                return E_FAIL;
        }

        // Write a NULL terminator
        DWORD dwNULL = 0L;
        if( !WriteFile( m_hfXPR, &dwNULL, sizeof(DWORD), &cb, NULL ) )
            return E_FAIL;

        // Write the string table
        for( i = 0; i < m_dwNumResources; i++ )
        {
            if( !WriteFile( m_hfXPR, m_pResources[i].strName, strlen(m_pResources[i].strName)+1, &cb, NULL ) )
                return E_FAIL;
        }

        // Add padding
        if( m_dwOffsetsTablePadBytes )
        {
            CHAR pad[16] = {0};
            if( !WriteFile( m_hfXPR, pad, m_dwOffsetsTablePadBytes, &cb, NULL ) )
                return E_FAIL;
        }
    }

    // Write out the header
    if( !WriteFile( m_hfXPR, m_pbHeaderBuff, m_cbHeader, &cb, NULL ) )
        return E_FAIL;

    if( cbFill )
    {
        // Allocate a buffer for fill data
        pbTemp = (BYTE *)malloc( cbFill );
        memset( pbTemp, 0xDEAD, cbFill );

        // Write it out
        bWriteOK = WriteFile( m_hfXPR, pbTemp, cbFill, &cb, NULL );
        free( pbTemp );
        if( !bWriteOK )
            return E_FAIL;
    }

    if( !WriteFile( m_hfXPR, m_pbDataBuff, m_cbData, &cb, NULL ) )
        return E_FAIL;

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: WriteHeaderFile()
// Desc: Writes out a .h file with useful defitions:
//       * NUM_RESOURCES
//       * XXX_OFFSET for each resource
//-----------------------------------------------------------------------------
HRESULT CBundler::WriteHeaderFile()
{
    DWORD i;
    DWORD cb;
    char strBuff[256];

    sprintf( strBuff, "// Automatically generated by the bundler tool from %s\n\n", m_strRDF );
    if( !WriteFile( m_hfHDR, strBuff, strlen(strBuff), &cb, NULL ) )
        return E_FAIL;

    // strip root filename out of m_strRDF;
    CHAR strPrefix[MAX_PATH];

    if( m_strPrefix[0] )
    {
        strcpy( strPrefix, m_strPrefix );
    }
    else
    {
        // Use the resource name as a prefix, after removing prepending path
        // and file extension.
        CHAR* pStr = strrchr( m_strRDF, '\\' );
        if( pStr )
            strcpy( strPrefix, pStr+1 );
        else
            strcpy( strPrefix, m_strRDF );

        pStr = strchr( strPrefix, '.' );
        if( pStr )
            *pStr = '\0';
    }

    // Write out the number of resources
    sprintf( strBuff, "#define %s_NUM_RESOURCES %dUL\n\n", strPrefix, m_dwNumResources );
    if( !WriteFile( m_hfHDR, strBuff, strlen(strBuff), &cb, NULL ) )
        return E_FAIL;

    // Write out the offset of each resource
    if( m_bSingleTexture )
    {
        sprintf( strBuff, "#define %s_OFFSET %dUL\n", strPrefix, 
                          m_pResources[0].dwOffset + m_dwBaseResourceOffset );
        if( !WriteFile( m_hfHDR, strBuff, strlen(strBuff), &cb, NULL ) )
            return E_FAIL;
    }
    else
    {
        for( i = 0; i < m_dwNumResources; i++ )
        {
            sprintf( strBuff, "#define %s_%s_OFFSET %dUL\n", strPrefix, 
                              m_pResources[i].strIdentifier,
                              m_pResources[i].dwOffset + m_dwBaseResourceOffset );
            if( !WriteFile( m_hfHDR, strBuff, strlen(strBuff), &cb, NULL ) )
                return E_FAIL;
        }
    }

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: HandleEOFToken()
// Desc: Handles any necessary end-of-input processing, such as patching up
//       header information
//-----------------------------------------------------------------------------
HRESULT CBundler::HandleEOFToken()
{
    if( FAILED( OpenFiles() ) )
        return E_FAIL;

    // write one last header entry to terminate the list
    DWORD tmp = 0xffffffff;
    WriteHeader( &tmp, sizeof(tmp) );
    m_cbHeader += sizeof(tmp);

    if( FAILED( FlushDataFile() ) )
        return E_FAIL;

    if( FAILED( WriteHeaderFile() ) )
        return E_FAIL;

    return S_FALSE;
}




//-----------------------------------------------------------------------------
// Name: HandleOutVersionToken()
// Desc: Takes over to handle the out_version token.  
//-----------------------------------------------------------------------------
HRESULT CBundler::HandleOutVersionToken()
{
    CHAR strVersion[MAX_PATH];
    GetNextTokenString( strVersion, TT_ANY );

    if( 0 == lstrcmpi( strVersion, "XPR0" ) )
    {
        m_dwOutputFileVersion = VERSION_XPR0;
    }
    else if( 0 == lstrcmpi( strVersion, "XPR1" ) )
    {
        m_dwOutputFileVersion = VERSION_XPR1;
    }
    else
    {
        ErrorMsg( "out_version requested unknown version!\n" );

        // Default to version 0
        m_dwOutputFileVersion = VERSION_XPR0;
    }

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: HandleOutPackedResourceToken()
// Desc: Takes over to handle the out_packedresource token.  
//-----------------------------------------------------------------------------
HRESULT CBundler::HandleOutPackedResourceToken()
{
    CHAR strFilename[MAX_PATH];
    GetNextTokenString( strFilename, TT_FILENAME );

    if( m_dwNumResources > 0 )
    {
        WarningMsg( "output files must be specified before resources\n" );
    }
    else if( !m_bExplicitXPR )
    {
        if( strchr( strFilename, ':' ) )
            strcpy( m_strXPR, strFilename );
        else
        {
            strcpy( m_strXPR, m_strPath );
            strcat( m_strXPR, strFilename );
        }
    }
    else
    {
        Msg( "out_packedresource overriden by command line\n" );
    }

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: HandleOutHeaderToken()
// Desc: Takes over to handle the out_header token.  
//-----------------------------------------------------------------------------
HRESULT CBundler::HandleOutHeaderToken()
{
    CHAR strFilename[MAX_PATH];
    GetNextTokenString( strFilename, TT_FILENAME );

    if( m_dwNumResources > 0 )
    {
        WarningMsg( "output files must be specified before resources\n" );
    }
    else if( !m_bExplicitHDR )
    {
        if( strchr( strFilename, ':' ) )
            strcpy( m_strHDR, strFilename );
        else
        {
            strcpy( m_strHDR, m_strPath );
            strcat( m_strHDR, strFilename );
        }
    }
    else
    {
        Msg( "out_header overriden by command line\n" );
    }

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: HandleOutPrefixToken()
// Desc: Takes over to handle the out_header token.  
//-----------------------------------------------------------------------------
HRESULT CBundler::HandleOutPrefixToken()
{
    CHAR strPrefix[MAX_PATH];
    GetNextTokenString( strPrefix, TT_ANY );

    if( m_dwNumResources > 0 )
    {
        WarningMsg( "output prefix must be specified before resources\n" );
    }
    else if( !m_bExplicitPrefix )
    {
        strcpy( m_strPrefix, strPrefix );
    }
    else
    {
        Msg( "out_prefix overriden by command line\n" );
    }

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: HandleOutErrorToken()
// Desc: Takes over to handle the out_error token.  
//-----------------------------------------------------------------------------
HRESULT CBundler::HandleOutErrorToken()
{
    CHAR strFilename[MAX_PATH];
    GetNextTokenString( strFilename, TT_FILENAME );

    if( m_dwNumResources > 0 )
    {
        WarningMsg( "output files must be specified before resources\n" );
    }
    else if( !m_bExplicitERR )
    {
        if( strchr( strFilename, ':' ) )
            strcpy( m_strERR, strFilename );
        else
        {
            strcpy( m_strERR, m_strPath );
            strcat( m_strERR, strFilename );
        }
    }
    else
    {
        Msg( "out_error overriden by command line\n" );
    }

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: WriteHeader()
// Desc: Copies resource header info to the header buffer.  Grows the header
//       buffer by multiples of 2.
//-----------------------------------------------------------------------------
HRESULT CBundler::WriteHeader( void * pbBuff, DWORD cb )
{
    DWORD cbNew = m_cbHeaderAlloc;

    // Find the next power-of-2 size that is big enough to hold the data
    while( m_cbHeader + cb > cbNew )
        cbNew <<= 1;

    // Realloc if necessary
    if( cbNew > m_cbHeaderAlloc )
    {
        BYTE * pbTemp = (BYTE *)realloc( m_pbHeaderBuff, cbNew );
        if( !pbTemp )
            return E_OUTOFMEMORY;
        m_pbHeaderBuff = pbTemp;
        m_cbHeaderAlloc = cbNew;
    }

    // Copy over the data
    memcpy( m_pbHeaderBuff + m_cbHeader, pbBuff, cb );

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: WriteData()
// Desc: Copies data to the data buffer.  Grows our data buffer by multiples
//       of 2
//-----------------------------------------------------------------------------
HRESULT CBundler::WriteData( void * pbBuff, DWORD cb )
{
    DWORD cbNew = m_cbDataAlloc;

    // Find the next power-of-2 size that is  big enough to hold the data
    while( m_cbData + cb > cbNew )
        cbNew <<= 1;
    
    // Realloc if necessary
    if( cbNew > m_cbDataAlloc )
    {
        BYTE * pbTemp = (BYTE *)realloc( m_pbDataBuff, cbNew );
        if( !pbTemp )
            return E_OUTOFMEMORY;
        m_pbDataBuff = pbTemp;
        m_cbDataAlloc = cbNew;
    }

    // Copy over the data
    memcpy( m_pbDataBuff + m_cbData, pbBuff, cb );
    m_cbData += cb;

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: PadToAlignment()
// Desc: Pads the data buffer to the specified alignment
//-----------------------------------------------------------------------------
HRESULT CBundler::PadToAlignment( DWORD dwAlign )
{
    HRESULT hr = S_OK;
    BYTE*   pbTemp = NULL;
    DWORD   cbFill = 0;

    if( m_cbData % dwAlign )
        cbFill = dwAlign - ( m_cbData % dwAlign );

    if( cbFill )
    {
        pbTemp = (BYTE *)malloc( cbFill );
        memset( pbTemp, 0xDEAD, cbFill );

        hr = WriteData( pbTemp, cbFill );
        
        free( pbTemp );
    }

    return hr;
}




//-----------------------------------------------------------------------------
// Name: IsExistingIdentifier()
// Desc: Returns TRUE if we've already processed a resource with the given
//       identifier
//-----------------------------------------------------------------------------
BOOL CBundler::IsExistingIdentifier( char* strIdentifier )
{
    DWORD dwStrLen = strlen( strIdentifier );

    for( DWORD dwIndex = 0; dwIndex < m_dwNumResources; dwIndex++ )
    {
        if( strlen( m_pResources[ dwIndex ].strIdentifier ) == dwStrLen &&
            !strncmp( strIdentifier, m_pResources[ dwIndex ].strIdentifier, dwStrLen ) )
            return TRUE;
    }

    return FALSE;
}




//-----------------------------------------------------------------------------
// Name: Process()
// Desc: Processes the resource definition file and processes it to create our
//       Xbox Packed Resource (xpr) file and header file
//-----------------------------------------------------------------------------
HRESULT CBundler::Process()
{
    HRESULT hr;
    BUNDLERTOKEN tok;

    // Special case: single-texture conversion
    if( m_bSingleTexture )
    {
        CTexture2D tex(this);
        DWORD cbHeader, cbData;
        
        if( strrchr( m_strRDF, '\\' ) )
            strcpy( tex.m_strSource, strrchr( m_strRDF, '\\' )+1 );
        else
            strcpy( tex.m_strSource, m_strRDF );
        
        // Save the texture to the bundle
        hr = tex.SaveToBundle( &cbHeader, &cbData );
        if( FAILED(hr) )
            return hr;
    
        if( !bQuietMode )
        {
            printf( "Texture: Wrote %s out in format %s (%d bytes)\n", 
                    tex.m_strSource, tex.m_strFormat, cbData );
            printf( "     %d x %d, %d levels\n\n", tex.m_dwWidth, tex.m_dwHeight, tex.m_dwLevels );
        }
    
        m_pResources[m_dwNumResources++].dwOffset = m_cbHeader;
        m_cbHeader += cbHeader;

        HandleEOFToken();

        return S_OK;
    }
    
    // Open the file
    m_hfRDF = CreateFile( m_strRDF, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL );
    if( INVALID_HANDLE_VALUE == m_hfRDF )
    {
        ErrorMsg( "Can't open <%s>\n", m_strRDF );
        return E_FAIL;
    }

    // Iterate over top-level tokens (Texture, out_xxxx, EOF, etc)
    for( ; ; )
    {
        hr = GetNextToken( &tok );
        if( FAILED(hr) )
            return hr;

        hr = (this->*tok.pfnHandler)();
        if( FAILED(hr) )
            return hr;
        else if( hr == S_FALSE  )
            break;
    }

    return S_OK;
}




//-----------------------------------------------------------------------------
// Name: main()
// Desc: Entry point to the program
//-----------------------------------------------------------------------------
int _cdecl main( int argc, char * argv[] )
{
	// Change the process priority for this task to not hammer Mike's machine
	SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS);

	// It's important to return an error message since otherwise Visual Studio
    // continues with the build, and it's easy to not realize that bundler failed.
    // It's also important to print a VC.Net compatible error message.
    const int kFailed = 10;
    HRESULT hr;
    CBundler bundler;

    hr = bundler.Initialize( argc, argv );
    if( hr == E_INVALIDARG )
    {
        bundler.PrintUsage();
        return kFailed;
    }
    else if( hr == E_OUTOFMEMORY )
    {
        bundler.ErrorMsg( "Not enough memory.\n" );
        return kFailed;
    }
    else if( FAILED(hr) )
    {
        bundler.ErrorMsg( "D3D Initialization failed.\n" );
        return kFailed;
    }

    hr = bundler.Process();
    if( FAILED(hr) )
        return kFailed;

    return 0;
}

