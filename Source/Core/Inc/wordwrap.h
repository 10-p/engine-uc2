#pragma once

#if !defined(_XBOX) && defined(_MSC_VER)
#undef TEXT
#undef HINSTANCE
#undef HANDLE
#undef INFINITE
#include <windows.h>
#elif !defined(_XBOX)
// engine-uc2: the Win32 types this interface uses, for builds without <windows.h> (GCC, Emscripten).
// WCHAR is 16 bits as on Windows -- UCanvas::WrapStringToArray passes appToUnicode() UNICHARs.
typedef UNICHAR			WCHAR;
typedef char			CHAR;
typedef int				BOOL;
typedef unsigned int	UINT;
typedef const WCHAR*	LPCWSTR;
typedef WCHAR*			LPWSTR;
typedef const CHAR*		LPCSTR;
typedef CHAR*			LPSTR;
#ifndef VOID
#define VOID void
#endif
#endif

//
//
// File: wordwrap.h
//
//

typedef FLOAT (*CB_GetWidthW)( WCHAR, BOOL );
typedef DWORD (*CB_Reserved)( VOID );

CORE_API void 		WordWrap_SetOption( UINT uCP, UINT uOption );
//CORE_API void	WordWrap_SetCallback( (UINT (*)( WCHAR )) );
CORE_API void		WordWrap_SetCallback( CB_GetWidthW pGetWidthW, CB_Reserved pReserved );

CORE_API LPCWSTR	WordWrap_FindNextLineW( LPCWSTR pszSource, UINT uWidth, LPCWSTR* ppszEOL, WCHAR EOLChar = '\n' );
CORE_API int		WordWrap_TokenizeW( LPCWSTR pszSource, LPWSTR* ppszDest);
CORE_API void		WordWrap_ClearTokensW( LPWSTR* ppszTokens );
CORE_API void		WordWrap_InitTokensW( LPWSTR* &ppszTokens, int iSize );

#if 1//def UNICODE
#define	WordWrap_FindNextLine		WordWrap_FindNextLineW
#define WordWrap_CharNext(cp,p)		CharNext(p)
#define WordWrap_Tokenize			WordWrap_TokenizeW
#define WordWrap_ClearTokens		WordWrap_ClearTokensW
#define WordWrap_InitTokens			WordWrap_InitTokensW
#else	// UNICODE
#define WordWrap_FindNextLine		WordWrap_FindNextLineA
#define WordWrap_CharNext(cp,p)		CharNextExA(cp,p,0)
#define WordWrap_Tokenize			WordWrap_TokenizeA
#define WordWrap_ClearTokens		WordWrap_ClearTokensA
#define WordWrap_InitTokens			WordWrap_InitTokensA
#endif	// UNICODE

#define	WW_PROHIBITION	0x00000001
#define	WW_NOMBCSSP		0x00000002 // specify when disable MBCS SP Process

//
// for compatibillity
//
#define LB_KINSOKU					WW_PROHIBITION
#define	LB_NOMBCSSP					WW_NOMBCSSP

#define LineBreak_SetOption			WordWrap_SetOption
//#define LineBreak_SetCallback		WordWrap_SetCallback
#define LineBreak_SetCallback		WordWrap_SetCallback

#define LineBreak_FindNextLineW		WordWrap_FindNextLineW
#define LineBreak_FindNextLineA		WordWrap_FindNextLineA
#define LineBreak_TokenizeW			WordWrap_TokenizeW
#define LineBreak_TokenizeA			WordWrap_TokenizeA
#define LineBreak_ClearTokensW		WordWrap_ClearTokensW
#define LineBreak_ClearTokensA		WordWrap_ClearTokensA
#define LineBreak_InitTokensW		WordWrap_InitTokensW
#define LineBreak_InitTokensA		WordWrap_InitTokensA

#if 1//def UNICODE
#define LineBreak_FindNextLine 		WordWrap_FindNextLineW
#define LineBreak_CharNext(cp,p) 	CharNext(p)
#define LineBreak_Tokenize			WordWrap_TokenizeW
#define LineBreak_ClearTokens		WordWrap_ClearTokensW
#define LineBreak_InitTokens		WordWrap_InitTokensW
#else
#define LineBreak_FindNextLine		WordWrap_FindNextLineA
#define LineBreak_CharNext(cp,p)	CharNextExA(cp,p,0)
#define LineBreak_Tokenize			WordWrap_TokenizeA
#define LineBreak_ClearTokens		WordWrap_ClearTokensA
#define LineBreak_InitTokens		WordWrap_InitTokensA
#endif
