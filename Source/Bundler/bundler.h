//-----------------------------------------------------------------------------
// File: Bundler.h
//
// Desc: Header file for the bundler tool
//
// Hist: 02.06.01 - New for March XDK release
//       01.29.02 - Updated to avoid using D3D
//       12.16.02 - Updated to output resource offsets table
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//-----------------------------------------------------------------------------
#ifndef BUNDLER_H
#define BUNDLER_H

#include <assert.h>
#include <windows.h>
#include <d3dx8.h>


// Bundler output file versions
enum
{
    VERSION_XPR0 = 0, // Version 0 = Default version
    VERSION_XPR1 = 1, // Version 1 = Outputs resource offsets table
};

#define XPR0_MAGIC_VALUE 0x30525058 // 'XPR0'
#define XPR1_MAGIC_VALUE 0x31525058 // 'XPR1'


// Default extensions for output files
#define XPREXT ".xpr"
#define HDREXT ".h"
#define ERREXT ".err"

#define MAX_RESOURCES 1024

#define TOKEOF ((CHAR)0xff)


class CBundler;
typedef HRESULT (CBundler::* TOKENHANDLERFN)();


// Enums for all of our known tokens
typedef enum 
{ 
    TOKEN_OPENBRACE                      = 0x00000001,
    TOKEN_CLOSEBRACE                     = 0x00000002,
    TOKEN_EOF                            = 0x00000003,
    TOKEN_OUT_VERSION                    = 0x00000004,
    TOKEN_OUT_PACKEDRESOURCE             = 0x00000005,
    TOKEN_OUT_HEADER                     = 0x00000006, 
    TOKEN_OUT_PREFIX                     = 0x00000007, 
    TOKEN_OUT_ERROR                      = 0x00000008,

    TOKEN_RESOURCE_TEXTURE               = 0x00010000,
    TOKEN_RESOURCE_CUBEMAP               = 0x00020000,
    TOKEN_RESOURCE_VOLUMETEXTURE         = 0x00040000,
    TOKEN_RESOURCE_VERTEXBUFFER          = 0x00080000,
    TOKEN_RESOURCE_USERDATA              = 0x00100000,
    TOKEN_RESOURCE_INDEXBUFFER           = 0x00200000,
	//jg -- Bundler option for handling compiled index push buffers
	TOKEN_RESOURCE_PUSHBUFFER            = 0x00400000,
	//jg -- Bundler option for bulk loading of TUmaArray data
	TOKEN_RESOURCE_TUMAARRAY			 = 0x00800000,

    // Properties for a resource have the resource
    // value shifted over by 16 bits
    TOKEN_PROPERTY                        = 0x80000000,
    TOKEN_PROPERTY_NAME                   = TOKEN_PROPERTY | 0x00000001,
    
    TOKEN_PROPERTY_TEXTURE                = TOKEN_PROPERTY | TOKEN_RESOURCE_TEXTURE,
    TOKEN_PROPERTY_TEXTURE_SOURCE         = TOKEN_PROPERTY_TEXTURE | 0x00000001,
    TOKEN_PROPERTY_TEXTURE_ALPHASOURCE    = TOKEN_PROPERTY_TEXTURE | 0x00000002,
    TOKEN_PROPERTY_TEXTURE_FILTER         = TOKEN_PROPERTY_TEXTURE | 0x00000003,
    TOKEN_PROPERTY_TEXTURE_FORMAT         = TOKEN_PROPERTY_TEXTURE | 0x00000004,
    TOKEN_PROPERTY_TEXTURE_WIDTH          = TOKEN_PROPERTY_TEXTURE | 0x00000005,
    TOKEN_PROPERTY_TEXTURE_HEIGHT         = TOKEN_PROPERTY_TEXTURE | 0x00000006,
    TOKEN_PROPERTY_TEXTURE_LEVELS         = TOKEN_PROPERTY_TEXTURE | 0x00000007,
	//jg -- Raw handling of textures
    TOKEN_PROPERTY_TEXTURE_RAW	          = TOKEN_PROPERTY_TEXTURE | 0x00000008,

    TOKEN_PROPERTY_CUBEMAP                = TOKEN_PROPERTY | TOKEN_RESOURCE_CUBEMAP,
    TOKEN_PROPERTY_CUBEMAP_SOURCE_XP      = TOKEN_PROPERTY_CUBEMAP | 0x00000001,
    TOKEN_PROPERTY_CUBEMAP_SOURCE_XN      = TOKEN_PROPERTY_CUBEMAP | 0x00000002,
    TOKEN_PROPERTY_CUBEMAP_SOURCE_YP      = TOKEN_PROPERTY_CUBEMAP | 0x00000003,
    TOKEN_PROPERTY_CUBEMAP_SOURCE_YN      = TOKEN_PROPERTY_CUBEMAP | 0x00000004,
    TOKEN_PROPERTY_CUBEMAP_SOURCE_ZP      = TOKEN_PROPERTY_CUBEMAP | 0x00000005,
    TOKEN_PROPERTY_CUBEMAP_SOURCE_ZN      = TOKEN_PROPERTY_CUBEMAP | 0x00000006,
    TOKEN_PROPERTY_CUBEMAP_ALPHASOURCE_XP = TOKEN_PROPERTY_CUBEMAP | 0x00000011,
    TOKEN_PROPERTY_CUBEMAP_ALPHASOURCE_XN = TOKEN_PROPERTY_CUBEMAP | 0x00000012,
    TOKEN_PROPERTY_CUBEMAP_ALPHASOURCE_YP = TOKEN_PROPERTY_CUBEMAP | 0x00000013,
    TOKEN_PROPERTY_CUBEMAP_ALPHASOURCE_YN = TOKEN_PROPERTY_CUBEMAP | 0x00000014,
    TOKEN_PROPERTY_CUBEMAP_ALPHASOURCE_ZP = TOKEN_PROPERTY_CUBEMAP | 0x00000015,
    TOKEN_PROPERTY_CUBEMAP_ALPHASOURCE_ZN = TOKEN_PROPERTY_CUBEMAP | 0x00000016,
    TOKEN_PROPERTY_CUBEMAP_SIZE           = TOKEN_PROPERTY_CUBEMAP | 0x00000022,

    TOKEN_PROPERTY_VOLUMETEXTURE             = TOKEN_PROPERTY | TOKEN_RESOURCE_VOLUMETEXTURE,
//  TOKEN_PROPERTY_VOLUMETEXTURE_SOURCE      = TOKEN_PROPERTY_VOLUMETEXTURE | 0x00000001,
//  TOKEN_PROPERTY_VOLUMETEXTURE_ALPHASOURCE = TOKEN_PROPERTY_VOLUMETEXTURE | 0x00000002,
    TOKEN_PROPERTY_VOLUMETEXTURE_DEPTH       = TOKEN_PROPERTY_VOLUMETEXTURE | 0x00000003,

    TOKEN_PROPERTY_VERTEXBUFFER              = TOKEN_PROPERTY | TOKEN_RESOURCE_VERTEXBUFFER,
    TOKEN_PROPERTY_VERTEXBUFFER_VERTEXDATA   = TOKEN_PROPERTY_VERTEXBUFFER | 0x01000001,
    TOKEN_PROPERTY_VERTEXBUFFER_VERTEXFORMAT = TOKEN_PROPERTY_VERTEXBUFFER | 0x01000002,
    TOKEN_PROPERTY_VERTEXBUFFER_VERTEXFILE   = TOKEN_PROPERTY_VERTEXBUFFER | 0x01000003,

    TOKEN_PROPERTY_USERDATA               = TOKEN_PROPERTY | TOKEN_RESOURCE_USERDATA,
    TOKEN_PROPERTY_USERDATA_DATAFILE      = TOKEN_PROPERTY_USERDATA | 0x00000001,

    TOKEN_PROPERTY_INDEXBUFFER            = TOKEN_PROPERTY | TOKEN_RESOURCE_INDEXBUFFER,
    TOKEN_PROPERTY_INDEXBUFFER_INDEXDATA  = TOKEN_PROPERTY_INDEXBUFFER | 0x00000001,
    TOKEN_PROPERTY_INDEXBUFFER_INDEXFILE  = TOKEN_PROPERTY_INDEXBUFFER | 0x00000002,

	//jg -- Compiled push buffer support
	TOKEN_PROPERTY_PUSHBUFFER = TOKEN_PROPERTY | TOKEN_RESOURCE_PUSHBUFFER,
	TOKEN_PROPERTY_PUSHBUFFER_FILE = TOKEN_PROPERTY_PUSHBUFFER | 0x00000001,
	TOKEN_PROPERTY_PUSHBUFFER_SIZE = TOKEN_PROPERTY_PUSHBUFFER | 0x00000002,
	TOKEN_PROPERTY_PUSHBUFFER_ALLOCSIZE = TOKEN_PROPERTY_PUSHBUFFER | 0x00000003,
	//jg -- Bulk TUmaArray support
	TOKEN_PROPERTY_TUMAARRAY = TOKEN_PROPERTY | TOKEN_RESOURCE_TUMAARRAY,
	TOKEN_PROPERTY_TUMAARRAY_FILE = TOKEN_PROPERTY_TUMAARRAY | 0x00000001,
	TOKEN_PROPERTY_TUMAARRAY_COUNT = TOKEN_PROPERTY_TUMAARRAY | 0x00000002,
} TOKEN_ID;


// Types of tokens for validation
typedef enum
{
    TT_ANY = 0,
    TT_IDENTIFIER,
    TT_HEXNUMBER,
    TT_INTEGER,
    TT_FLOAT,
    TT_FILENAME,
} TOKENTYPE;


// Token struct that is passed around
struct BUNDLERTOKEN
{
    CHAR*           strKeyword;
    TOKEN_ID        eType;
    TOKENTYPE       propType;
    TOKENHANDLERFN  pfnHandler;
};


// Resource struct for tracking
struct RESOURCE_STRUCT
{
    CHAR    strIdentifier[128];
    CHAR    strName[128];
    DWORD   dwOffset;
};




//-----------------------------------------------------------------------------
// Name: class CBundler
// Desc: Main application class for driving the bundler.  Handles data 
//       management, resource tracking, file management, and tokenizing
//-----------------------------------------------------------------------------
class CBundler
{
public:
    CBundler();
    ~CBundler();

    HRESULT InitD3D();
    HRESULT Initialize( int argc, char * argv[] );
    HRESULT Process();
    void    PrintUsage();

    HRESULT WriteHeader( void * pbBuff, DWORD cb );
    HRESULT WriteData( void * pbBuff, DWORD cb );
    HRESULT PadToAlignment( DWORD dwAlign );

    // Token handling routines
    HRESULT HandleOutVersionToken();
    HRESULT HandleOutPackedResourceToken();
    HRESULT HandleOutHeaderToken();
    HRESULT HandleOutPrefixToken();
    HRESULT HandleOutErrorToken();
    HRESULT HandleTextureToken();
    HRESULT HandleCubemapToken();
    HRESULT HandleVolumeTextureToken();
    HRESULT HandleVertexBufferToken();
    HRESULT HandleUserDataToken();
    HRESULT HandleIndexBufferToken();
    HRESULT HandleEOFToken();
	//jg -- Compiled push buffer support
	HRESULT HandlePushBufferToken();
	//jg -- Compiled TUmaArray support
	HRESULT HandleTUmaArrayToken();

    DWORD   FilterFromString( char* strFilter );

    // D3D stuff for manipulating textures
    LPDIRECT3D8       m_pD3D;
    LPDIRECT3DDEVICE8 m_pd3dDevice;

    // Routine to call w/ error messages - prints to stdout and file
    // Prepends "Bundler : error : " so that Visual Studio will recognize the
    // error message. Only use for fatal errors.
    void ErrorMsg( const char * strFmt, ... );
    // Ditto for warnings - prepends "Bundler : warning :". Only use for
    // genuine warnings.
    void WarningMsg( const char * strFmt, ... );
    // No prefix. Just print the specified text.
    void Msg( const char * strFmt, ... );

    BOOL IsExistingIdentifier( char * strIdentifier );

    DWORD m_cbHeader;
    DWORD m_cbData;

public:

    HRESULT OpenFiles();            // Open output files
    HRESULT CloseFiles();           // Close all files
    HRESULT FlushDataFile();        // Flush from buffer to output file
    HRESULT WriteHeaderFile();      // Create .h header file

    BOOL    Match( INT TokenType, BUNDLERTOKEN* pToken );
    HRESULT GetNextToken( BUNDLERTOKEN* pToken );
    HRESULT GetNextTokenString( CHAR* strTokenString, TOKENTYPE tt );
    HRESULT ValidateType( CHAR* strToken, TOKENTYPE tt );
    HRESULT GetTokenFromString( CHAR* strTokenString, BUNDLERTOKEN* pToken );
    CHAR    GetChar();
    CHAR    PeekChar();

    // Bundler output file version
    DWORD  m_dwOutputFileVersion;

    // Filenames
    CHAR   m_strPath[MAX_PATH];
    CHAR   m_strRDF[MAX_PATH];
    CHAR   m_strXPR[MAX_PATH];
    CHAR   m_strHDR[MAX_PATH];
    CHAR   m_strERR[MAX_PATH];

    // Prefix for header file constants
    CHAR   m_strPrefix[MAX_PATH];

    // File handles
    HANDLE m_hfRDF;
    HANDLE m_hfXPR;
    HANDLE m_hfHDR;
    HANDLE m_hfERR;

    // Data buffer management
    BYTE*  m_pbHeaderBuff;
    DWORD  m_cbHeaderAlloc;
    BYTE*  m_pbDataBuff;
    DWORD  m_cbDataAlloc;

    // Values for the resource offsets table
    DWORD  m_dwOffsetsTableSize;
    DWORD  m_dwOffsetsTablePadBytes;
    DWORD  m_dwBaseResourceOffset;

    // Resource tracking
    RESOURCE_STRUCT* m_pResources;
    DWORD            m_dwNumResources;

    // For specifying output files and prefix on cmd line vs. in RDF
    BOOL   m_bExplicitXPR;
    BOOL   m_bExplicitHDR;
    BOOL   m_bExplicitERR;
    BOOL   m_bExplicitPrefix;

    // Single character read buffers
    CHAR   m_cNext0;
    CHAR   m_cNext1;
    CHAR   m_cNext2;
    CHAR   m_cNext3;

    // Indicates a single-texture conversion
    BOOL   m_bSingleTexture;

    BOOL   m_bD3DCreated;

    BOOL   m_bQuiet;
private:
    // Support for function for ErrorMsg, WarningMsg and Msg
    void InternalPrintMessage( const char* prefix, const char* strFmt, va_list args);
};

#endif // BUNDLER_H
