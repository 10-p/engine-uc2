//
//
// File: wordwrap.cpp
//
// Desc.
//
//

#ifdef _XBOX
#include <xtl.h>
#elif defined(_MSC_VER)
#include <windows.h>
#else
// engine-uc2: portable build (GCC, Emscripten). Core's types (FLOAT, UNICHAR, CORE_API) instead of
// <windows.h>; the Win32 code-page functions (ANSI variants, GetCPInfoEx) are left out, as on the Xbox.
#include "Core.h"
#endif
#if defined(_MSC_VER)
#include <malloc.h>
#endif
#include "wordwrap.h"

// WW_WIN32_API: the Win32 PC build, which has the code-page API (GetVersionEx, GetCPInfoEx,
// MultiByteToWideChar, CharNextExA). The Xbox and the portable build do without it.
#if defined(_MSC_VER) && !defined(_XBOX)
	#define WW_WIN32_API 1
#else
	#define WW_WIN32_API 0
#endif

#if !defined(_MSC_VER)
// WCHAR is 16 bits here, wchar_t is not: the two wide-string helpers TokenizeW needs, for WCHAR.
static inline int WW_wcslen( LPCWSTR s ) { int n = 0; while( s[n] ) n++; return n; }
static inline LPWSTR WW_wcsncpy( LPWSTR d, LPCWSTR s, int n ) { int i = 0; for( ; i < n && s[i]; i++ ) d[i] = s[i]; for( ; i < n; i++ ) d[i] = 0; return d; }
#define wcslen  WW_wcslen
#define wcsncpy WW_wcsncpy
#endif

//-----------------------------------------------------------------------------
// defines
//-----------------------------------------------------------------------------

// #define KEYSTONE 1 // uncomment or define this in compiler option in case KEYSTONE.
#define COUNTOF(a) ( sizeof( a ) / sizeof( (a)[0] ) )

//-----------------------------------------------------------------------------
// globals
//-----------------------------------------------------------------------------

CB_GetWidthW	GetWidthW	= NULL;
CB_Reserved		Reserved	= NULL;

UINT	g_uCP				= 1252;
UINT	g_uOption			= 0;
BOOL	g_bIsNT				= FALSE;
#if defined(_MSC_VER)
WCHAR	g_szUniErrChar[2]	= L"";
#else
WCHAR	g_szUniErrChar[2]	= { 0, 0 };	// L"" is a 32-bit wchar_t string here
#endif
CHAR	g_szMBErrChar[3] 	= "";

//-----------------------------------------------------------------------------
//	program
//-----------------------------------------------------------------------------

void WordWrap_SetCallback( CB_GetWidthW pGetWidthW, CB_Reserved pReserved )
{
	if ( pGetWidthW ){
		GetWidthW = pGetWidthW;
	}
	if ( pReserved ){
		Reserved = pReserved;
	}
}

void WordWrap_SetOption( UINT uCP, UINT uOption )
{
	g_uCP = uCP;
	g_uOption = uOption;
	
#if WW_WIN32_API
	// Check NT
	OSVERSIONINFO verinfo;
	verinfo.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx( &verinfo );
	g_bIsNT = ((verinfo.dwPlatformId == VER_PLATFORM_WIN32_NT ) ? TRUE : FALSE);
#else
	g_bIsNT = 0;
#endif
//	OutputDebugString( TEXT("'g_bIsNT' initialized\r\n") ); // DEBUG
	
	// Check Unicode Error Char
#if WW_WIN32_API
	CPINFOEX cpinfo;
	if ( GetCPInfoEx( g_uCP, 0, &cpinfo ) == 0 ){ // FAIL ?
		cpinfo.UnicodeDefaultChar = 0;
	}
	BOOL bError;
	g_szUniErrChar[0] = cpinfo.UnicodeDefaultChar;
#else
	BOOL bError;
	g_szUniErrChar[0] = 0;
#endif
	g_szUniErrChar[1] = 0;
	g_szMBErrChar[0] = g_szMBErrChar[1] = g_szMBErrChar[2] = 0;
#if defined(_MSC_VER)
	WideCharToMultiByte( g_uCP, 0, g_szUniErrChar, -1, g_szMBErrChar, sizeof(g_szMBErrChar), NULL, &bError );
#endif
//	TCHAR buf[128]; // DEBUG
//	wsprintf( buf, TEXT("g_szUniErrChar=%04X\r\n"), cpinfo.UnicodeDefaultChar ); // DEBUG
//	OutputDebugString( buf ); // DEBUG
//	OutputDebugString( TEXT("g_szMBErrChar=[") ); // DEBUG
//	OutputDebugStringA( g_szMBErrChar ); // DEBUG
//	OutputDebugString( TEXT("]\r\n") ); // DEBUG
}

#define IsWhiteSpace(c) ( ( c ) == L'\t' || ( c ) == L'\r' || ( c ) == L' ' || ( c ) == 0x3000 ) // RAID#210: Add full-width space(0x3000) : Jan.20,2003 00:20 by yutaka.
#define IsLineFeed(c,eol) ( ( c ) == L'\n' || c == eol )

static bool IsNonBeginningChar( WCHAR c )
{
	if ( ! (g_uOption & WW_PROHIBITION) ) return FALSE;
	
	return (
#if 1 // Additional EA characters for RoN : Nov.21,2002 01:19 by yutaka.
	( c ) == 0xff0c ||		// 
	( c ) == 0xff0e ||		// 
	( c ) == 0xff1a ||		// 
	( c ) == 0xfe30 ||		// 
	( c ) == 0xfe50 ||		// 
	( c ) == 0x00B7 ||		// 
	( c ) == 0xfe56 ||		// 
	( c ) == 0xff5d ||		// 
	( c ) == 0x300F ||		// 
	( c ) == 0xfe5c ||		// 
	( c ) == 0x201D ||		// 
	( c ) == 0x003A ||		// 
	( c ) == 0x005D ||		// 
	( c ) == 0x2022 ||		// 
	( c ) == 0x2027 ||		// 
	( c ) == 0x2026 ||		// 
	( c ) == 0xfe51 ||		// 
	( c ) == 0xfe54 ||		// 
	( c ) == 0xfe57 ||		// 
	( c ) == 0x3011 ||		// 
	( c ) == 0x300D ||		// 
	( c ) == 0xfe5e ||		// 
	( c ) == 0x301E ||		// 
	( c ) == 0x003B ||		// 
	( c ) == 0x007D ||		// 
	( c ) == 0xff1b ||		// 
	( c ) == 0xff01 ||		// 
	( c ) == 0xfe52 ||		// 
	( c ) == 0xfe55 ||		// 
	( c ) == 0x3015 ||		// 
	( c ) == 0x3009 ||		// 
	( c ) == 0xfe5a ||		// 
	( c ) == 0x2019 ||		// 
	( c ) == 0x2032 ||		// 
	( c ) == 0x0025 ||		// 
	( c ) == 0x00B0 ||		// 
	( c ) == 0x2033 ||		// 
	( c ) == 0x2103 ||		// 
	( c ) == 0x300b ||		// 
	( c ) == 0xff05 ||		// 
	( c ) == 0xff3d ||		// 
	( c ) == 0xffe0 ||		// 
	( c ) == 0x2013 ||		// 
	( c ) == 0x2014 ||		// 
#endif
#if 1 // RAID#149: Additional JP punctation characters : Jan.20,2003 00:23 by yutaka.
	( c ) == 0xff61 ||		// 
	( c ) == 0xff64 ||		// 
#endif
#if 1 // RAID#212: Additional JP contracted sound characters : Jan.20,2003 00:27 by yutaka.
	( c ) == 0x3063 ||		// 
	( c ) == 0x3083 ||		// 
	( c ) == 0x3085 ||		// 
	( c ) == 0x3087 ||		// 
	( c ) == 0x30c3 ||		// 
	( c ) == 0x30e3 ||		// 
	( c ) == 0x30e5 ||		// 
	( c ) == 0x30e7 ||		// 

	( c ) == 0x3041 ||		// 
	( c ) == 0x3043 ||		// 
	( c ) == 0x3045 ||		// 
	( c ) == 0x3047 ||		// 
	( c ) == 0x3049 ||		// 
	( c ) == 0x308e ||		// 
	( c ) == 0x30a1 ||		// 
	( c ) == 0x30a3 ||		// 
	( c ) == 0x30a5 ||		// 
	( c ) == 0x30a7 ||		// 
	( c ) == 0x30a9 ||		// 
	( c ) == 0x30ee ||		// 
#endif
#if 1 // RAID#211: TEST : Jan.20,2003 01:46 by yutaka.
	( c ) == 0x0022 ||		// Left parenthesis
#endif
	( c ) == 0x0021 ||		// Exclamation mark
	( c ) == 0x0029 ||		// Right parenthesis
	( c ) == 0x002c ||		// Comma
	( c ) == 0x002e ||		// Full stop (period)
	( c ) == 0x003f ||		// Question mark
	( c ) == 0x3001 ||		// Ideographic comma
	( c ) == 0x3002 ||		// Ideographic full stop
	( c ) == 0x30fc ||		// Katakana-hiragana prolonged sound mark
	( c ) == 0xff01 ||		// Fullwidth exclamation mark
	( c ) == 0xff09 ||		// Fullwidth right parenthesis
	( c ) == 0xff1f ||		// Fullwidth question mark
	( c ) == 0xff70 ||		// Halfwidth Katakana-hiragana prolonged sound mark
	( c ) == 0xff9e ||		// Halfwidth Katakana voiced sound mark
	( c ) == 0xff9f );		// Halfwidth Katakana semi-voiced sound mark
}

static bool IsNonEndingChar( WCHAR c )
{
	if ( ! (g_uOption & WW_PROHIBITION) ) return FALSE;
	
	return (
#if 1 // Additional EA characters for RoN : Nov.21,2002 01:19 by yutaka.
	( c ) == 0x3010 ||		// 
	( c ) == 0x300C ||		// 
	( c ) == 0xfe5d ||		// 
	( c ) == 0x301D ||		// 
	( c ) == 0x005B ||		// 
	( c ) == 0x3014 ||		// 
	( c ) == 0x3008 ||		// 
	( c ) == 0xfe59 ||		// 
	( c ) == 0x2018 ||		// 
	( c ) == 0x2035 ||		// 
	( c ) == 0x007B ||		// 
	( c ) == 0xff5b ||		// 
	( c ) == 0x300E ||		// 
	( c ) == 0xfe5b ||		// 
	( c ) == 0x201C ||		// 
	( c ) == 0x005C ||		// 
	( c ) == 0x0024 ||		// 
	( c ) == 0xffe1 ||		// 
	( c ) == 0x300A ||		// 
	( c ) == 0xff04 ||		// 
	( c ) == 0xff3b ||		// 
	( c ) == 0xffe6 ||		// 
	( c ) == 0xffe5 ||		// 
#endif
#if 1 // RAID#211: TEST : Jan.20,2003 01:46 by yutaka.
	( c ) == 0x0022 ||		// Left parenthesis
#endif
	( c ) == 0x0028 ||		// Left parenthesis
	( c ) == 0xff08 );		// Fullwidth left parenthesis
}

static bool IsEastAsianChar( WCHAR c )
{
	return
		( 0x1100 <= c ) && ( c <= 0x11FF ) ||		// Hangul Jamo
		( 0x3000 <= c ) && ( c <= 0xD7AF ) ||		// CJK symbols - Hangul Syllables
		( 0xF900 <= c ) && ( c <= 0xFAFF ) ||		// CJK compat
		( 0xFF00 <= c ) && ( c <= 0xFFDC );			// Halfwidth / Fullwidth
}

#ifdef KEYSTONE

static bool CanBreakLineAtForKeystone( LPCWSTR psz, LPCWSTR pszStart )
{
	if ( psz == pszStart )
	{
		return false;	// leave at least one character in a line
	}
	return
		// The current character *psz can be a new token if the following
		// condition holds. 
		//
		( IsEastAsianChar( *psz ) || 
		  IsEastAsianChar( psz[-1] ) || 
		  IsWhiteSpace(psz[-1]) || ( L'@' == psz[-1] ) ) &&
		  !IsNonBeginningChar( *psz ) && !IsNonEndingChar( psz[-1] ) && 
		  !IsWhiteSpace(*psz) && ( L'@' != *psz );
}
#endif // KEYSTONE

#if 1 // 020513 ///////////////////////////////////////////////////////////////
static bool CanBreakLineAt( LPCWSTR psz, LPCWSTR pszStart )
{
	if ( psz == pszStart )
	{
		return false;	// leave at least one character in a line
	}
	return
		( IsWhiteSpace( *psz ) || IsEastAsianChar( *psz ) || IsEastAsianChar( psz[-1] ) ) &&
		!IsNonBeginningChar( *psz ) && !IsNonEndingChar( psz[-1] );
}
#endif // 020513

static LPCWSTR FindNonWhiteSpaceForward( LPCWSTR psz, WCHAR eol )
{
	while ( IsWhiteSpace( *psz ) )
	{
		psz++;
	}
#ifndef NO_RAID_151_152 // May.21,2002 06:04 by yutaka.
	if ( psz && IsLineFeed(*psz,eol) ) psz++;
#endif // NO_RAID_151_152
	return ( *psz ) ? psz : NULL;
}

static LPCWSTR FindNonWhiteSpaceBackward( LPCWSTR psz, LPCWSTR pszSource, WCHAR eol )
{
	while ( psz >= pszSource && ( IsWhiteSpace( *psz ) || IsLineFeed( *psz, eol ) ) )
	{
		psz--;
	}
	return ( psz < pszSource ) ? NULL : psz;
}

//
// FindNextLine
// param:	pszSource: (in) the line to break
//			uWidth: (in) width to wrap the line
//			ppszEOL: (out) receive the pointer to the last non-white space char in the line. 
// return:	The beginning of next line. NULL if the line fits within the specified width.
//			*ppszEOL will be NULL if the wrapped line consists of only white space chars, 
//
// Case / Expected
// - Line width less than a character width / The line will contain at least one character
// - All white spaces ||
// - All unbreakable character ||
// - All non-starting characters ||
// - All non-terminating characters / Break at the character that overflows the width
// - Zero length line / return NULL w/ EOL NULL
//
LPCWSTR WordWrap_FindNextLineW( LPCWSTR pszSource, UINT uWidth, LPCWSTR* ppszEOL, WCHAR EOLChar )
{
	if ( GetWidthW == NULL || pszSource == NULL || !pszSource[0] )
	{
		*ppszEOL = NULL;
		return NULL;
	}
    
	LPCWSTR psz = pszSource;
	UINT uCurrent;
    
	BOOL bContinue = *psz && !IsLineFeed( *psz, EOLChar );
	for ( uCurrent = 0; bContinue; psz++ )
	{
		bContinue = *(psz+1) && !IsLineFeed(*(psz+1),EOLChar);
		FLOAT ChW = GetWidthW( *psz, bContinue );
		uCurrent += (INT)(ChW + 0.5);
		if ( uCurrent > uWidth )
			break;
	}
	if ( pszSource == psz )
	{
		// Break at second character if line width is less than character width
		*ppszEOL = FindNonWhiteSpaceBackward( psz, pszSource, EOLChar );
//		OutputDebugString( TEXT("[1]\r\n") ); // DEBUG
		return FindNonWhiteSpaceForward( psz + 1, EOLChar );
	}
	if ( IsLineFeed( *psz, EOLChar ) )
	{
		psz++;
	}
	if ( uCurrent <= uWidth )
	{
		// The line is shorter than the width
		*ppszEOL = FindNonWhiteSpaceBackward( psz - 1, pszSource, EOLChar );
//		OutputDebugString( TEXT("[2]\r\n") ); // DEBUG
#ifndef NO_RAID_151_152 // May.21,2002 06:04 by yutaka.
		if ( psz-1 >= pszSource && IsLineFeed( *(psz-1), EOLChar ) ){
			return psz;
		}
#endif // NO_RAID_151_152
		return ( *psz ) ? FindNonWhiteSpaceForward( psz, EOLChar ) : NULL;
	}
    
	LPCWSTR pszOverflow = psz;

#if 1 // 020513 ///////////////////////////////////////////////////////////////
	while ( psz > pszSource )
	{
		if ( IsWhiteSpace( *psz ) )
		{
			*ppszEOL = FindNonWhiteSpaceBackward( psz, pszSource, EOLChar );
			if ( !*ppszEOL )
			{
//				OutputDebugString( TEXT("[3]\r\n") ); // DEBUG
				return FindNonWhiteSpaceForward( psz + 1, EOLChar );
			}
			psz = *ppszEOL + 1;
		}
		if ( CanBreakLineAt( psz, pszSource ) )
		{
			break;
		}
		psz--;
	}
	if ( psz <= pszSource )
	{
		// couldn't find any character to break the line
		*ppszEOL = pszOverflow - 1;
//		OutputDebugString( TEXT("[4]\r\n") ); // DEBUG
		return pszOverflow;
	}
	*ppszEOL = psz - 1;
//	OutputDebugString( TEXT("[5]\r\n") ); // DEBUG
	return FindNonWhiteSpaceForward( psz, EOLChar );
#else // 020513 ///////////////////////////////////////////////////////////////
	while ( psz > pszSource )
	{
		if ( IsWhiteSpace( *psz ) || IsEastAsianChar( *psz ) || IsEastAsianChar( psz[-1] ) )
		{
			LPCWSTR pszNextLine = FindNonWhiteSpaceForward( psz, EOLChar );
			psz = FindNonWhiteSpaceBackward( psz - 1, pszSource, EOLChar );
			if ( ( !psz || !IsNonEndingChar( *psz ) ) && ( !pszNextLine || !IsNonBeginningChar( *pszNextLine ) ) )
			{
//				OutputDebugString( TEXT("[3']\r\n") ); // DEBUG
				*ppszEOL = psz;
				return pszNextLine;
			}
		}
		else
		{
			psz--;
		}
	}
	// couldn't find any character to break the line
	*ppszEOL = pszOverflow - 1;
//	OutputDebugString( TEXT("[4']\r\n") ); // DEBUG
	return pszOverflow;
#endif // 020513 //////////////////////////////////////////////////////////////
}

#pragma warning( disable : 4244 )
#if WW_WIN32_API
LPCSTR WordWrap_FindNextLineA( LPCSTR pszSource, UINT uWidth, LPCSTR* ppszEOL )
{
	if ( GetWidthW == NULL || pszSource == NULL || !pszSource[0] )
	{
		*ppszEOL = NULL;
		return NULL;
	}
	
#ifdef TEST_LBBUF10
	static WCHAR szTemp[10]; // just for testing use
#else // TEST_LBBUF10
	// As long as 1 line doesn't exceed following buffer size (=char num), 
	// string is processed correctly.
	static WCHAR szTemp[1024*8];
#endif // TEST_LBBUF10
#ifndef NO_RAID_156
	BOOL bIsInsufficientBuffer = FALSE;
#endif // NO_RAID_156
	
#ifndef NO_RAID_156
	int ret = MultiByteToWideChar( g_uCP, 0, pszSource, -1, szTemp, COUNTOF(szTemp) );
	if ( ret == 0 ){
		if ( GetLastError() == ERROR_INSUFFICIENT_BUFFER ){
			ret = COUNTOF(szTemp)-1;
			bIsInsufficientBuffer = TRUE;
		}
	}
#else // NO_RAID_156
	int ret = MultiByteToWideChar( g_uCP, 0, pszSource, -1, szTemp, COUNTOF(szTemp)-1 );
#endif // NO_RAID_156
//	int ret = MultiByteToWideChar( g_uCP, MB_USEGLYPHCHARS, pszSource, -1, szTemp, COUNTOF(szTemp)-1 ); // no effect for '?'(0x3F)
//	int ret = MultiByteToWideChar( g_uCP, MB_ERR_INVALID_CHARS, pszSource, -1, szTemp, COUNTOF(szTemp)-1 ); // err if error char found
	if ( ret == 0 ){
		*ppszEOL = NULL;
		return NULL;
	}
#ifndef NO_RAID_156
	szTemp[COUNTOF(szTemp)-1] = 0;
#else // NO_RAID_156
	szTemp[ret] = 0;
#endif // NO_RAID_156
	
	if ( !(g_uOption & WW_NOMBCSSP) && (!g_bIsNT) && g_szUniErrChar[0] ){ // 2002.05.28.0001
		
		LPCSTR pszTemp = (LPCSTR)pszSource;
		for ( int i=0; szTemp[i]; i++ ){
			if ( szTemp[i] == g_szUniErrChar[0] ){
				if ( pszTemp[0]==g_szMBErrChar[0] && 
                     (g_szMBErrChar[1]==0 || (pszTemp[1]==g_szMBErrChar[1] && g_szMBErrChar[2]==0)) ){
					// do nothing.
				} else {
					// Conversion Error Char
					
					/*
                      Win9x's TextOut() shows Error Char as a dot as it's 
                      defined in SJIS font, and it's not necessarily as same as 
                      Unicode Error Character = U+30FB(SJIS=0x8145).
                      
                      Further, these width are different.
                      On Win9x, U+30FB is narrower than the dot which is 
                      defined in SJIS font.
                      
                      I'm adjusting it to replacing the character internally 
                      to fit the dot, by setting wider (and closer) character 
                      here.
					*/
					
					if ( g_uCP == 1252 ){
						szTemp[i] = 0x003F; // L'W'; // replace with the wider char for USA
					} else {
						szTemp[i] = 0x4E9E; // L'˜±'; // replace with the wider char for EA
						// Unicode		U+4E9E
						// JPN(SJIS)	0x98B1
						// CHT(BIG5)	0xE4AC
						// CHS(BG2312)	0xA8C8
					}
				}
			}
			pszTemp = CharNextExA( g_uCP, pszTemp, 0 );
		}
	}
	
#if 0 // DEBUG
	{
		OutputDebugString( TEXT(">>>") );
		for ( int i=0; szTemp[i] ; i++ ){
			TCHAR buf[128];
			wsprintf( buf, TEXT("[%04X]"), szTemp[i] );
			OutputDebugString( buf );
		}
		OutputDebugString( TEXT("\r\n") );
	}
#endif // DEBUG
	
	LPCWSTR pszEOL = NULL;
	LPCWSTR pszNext = WordWrap_FindNextLineW( szTemp, uWidth, &pszEOL );
	
	LPCSTR pszRet = NULL;
#ifndef NO_RAID_156
	if ( !pszNext && bIsInsufficientBuffer ){
		pszNext = szTemp + COUNTOF(szTemp)-1;
	}
#endif // NO_RAID_156
	if ( pszNext ){
		int n = pszNext - szTemp;
		pszRet = pszSource;
		while ( n-- ){
			pszRet = CharNextExA( g_uCP, pszRet, 0 );
		}
	}
	
#ifdef NO_RAID_154 // 2002.05.28.0001
	*ppszEOL = pszSource;
#endif // NO_RAID_154
	if ( pszEOL ){
#ifndef NO_RAID_154 // 2002.05.28.0001
		*ppszEOL = pszSource;
#endif // NO_RAID_154
		int n = pszEOL - szTemp;
		if ( n ){
			while ( n-- ){
				*ppszEOL = CharNextExA( g_uCP, *ppszEOL, 0 );
			}
		}
	}
	
	return pszRet;
}
#endif // _XBOX

#pragma warning( default : 4244 )

//
// WordWrap_Tokenize
//
// Desc.	Tokenizes an input string.  Each Japanese/Chinese character is
//			a token on its own unless it is a punctuation.  All other languages
//			are tokenized based on having a space between sets of letters.
//
// param:	pszSource: (in) NULL terminated wide character string
//			ppszDest:  (out) NULL terminated list of strings; each string is a 
//			NULL terminated wide characters.  If NULL is passed, the function
//			returns the number of tokens that will be created.
//	
// return:	Number of tokens that will be created.  This is meaningful only if
//			ppszDest is NULL
//
// Note:	Caller is responsible for allocating the memory for the array of pointers.
//			The function will allocate the memory for the each string in the array,
//			but the caller is responsible for freeing each of strings.  Use 
//			WordWrap_ClearTokens to free the memory used by the list of string pointers.
//
int WordWrap_TokenizeW( LPCWSTR pszSource, LPWSTR* ppszDest)
{
	if( !pszSource ) {
		return 0;
	}

	// Start from the second character because whether or not a character can be
	// tokenized depends on character preceeding the current character.
	//
	LPCWSTR pToken = pszSource + 1; 
	LPCWSTR pBegin = pszSource;
	int iTokens = 0;
	int nLength = wcslen(pszSource);
	for(int i = 0; i < nLength; i++) {
#ifdef KEYSTONE
		if( CanBreakLineAtForKeystone( pToken,NULL ) || !*pToken ) {
#else // KEYSTONE
		if( CanBreakLineAt( pToken,NULL ) || !*pToken ) {
#endif // KEYSTONE
			if( ppszDest ) {
				*ppszDest = new WCHAR[pToken-pBegin+1];
				wcsncpy(*ppszDest,pBegin,pToken-pBegin);
				(*ppszDest)[pToken-pBegin] = 0;
				++ppszDest;
			}
			else {
				++iTokens;
			}
			pBegin = pToken;
		}
		++pToken;
	}

	return iTokens;
}

#if WW_WIN32_API
// ANSI version of the above function
//
int WordWrap_TokenizeA( LPCSTR pszSource, LPSTR* ppszDest)
{
	if( !pszSource ) {
		return 0;
	}

	int iTokens = 0;
	int iSize = MultiByteToWideChar( g_uCP, 0, pszSource, -1, NULL, 0 );

	// This memory only needs to exists during the lifespan of this
	// function.  So, allocate the memory on stack
	//
	LPWSTR pwch = (LPWSTR) _alloca( sizeof(WCHAR) * iSize );

	// If there is not enough memory in stack, use the heap instead.
	//
	bool bHeapUsed = false;
	if(!pwch) {
		bHeapUsed = true;
		pwch = new WCHAR [iSize];
	}
	
	if(MultiByteToWideChar( g_uCP, 0, pszSource, -1, pwch, iSize )) {
		LPCWSTR pToken = pwch + 1; 
		LPCWSTR pBegin = pwch;
		int nLength = wcslen(pwch);
		for(int i = 0; i < nLength; i++) {
#ifdef KEYSTONE
			if( CanBreakLineAtForKeystone( pToken,NULL ) || !*pToken ) {
#else // KEYSTONE
			if( CanBreakLineAt( pToken,NULL ) || !*pToken ) {
#endif // KEYSTONE
				if( ppszDest ) {
					int iBytes = WideCharToMultiByte( g_uCP, 0, pBegin, pToken - pBegin, 
						                              NULL, 0, NULL, NULL );
					*ppszDest = new char [iBytes + 1];
					WideCharToMultiByte( g_uCP, 0, pBegin, pToken - pBegin,
						                 *ppszDest, iBytes, NULL, NULL );
					(*ppszDest)[iBytes] = 0;
					++ppszDest;

				}
				else {
					++iTokens;
				}
				pBegin = pToken;
			}
			++pToken;
		}
	}

	if(bHeapUsed) {
		delete [] pwch;
	}

	return iTokens;
}
#endif // _XBOX

//
// WordWrap_ClearTokens
//
// Desc.	Deallocates the memory used by the token
//
// param:	ppszTokens	Null terminated list of NULL terminated strings
//	
void WordWrap_ClearTokensW( LPWSTR* ppszTokens )
{
	if(ppszTokens) {
		LPWSTR* ppSave = ppszTokens;
		while(*ppszTokens) {
			delete [] *ppszTokens;
			++ppszTokens;
		}
		delete [] ppSave;
	}
}

#ifdef _XBOX
// ANSI version of the above function
//
void WordWrap_ClearTokensA( LPSTR* ppszTokens )
{
	if(ppszTokens) {
		LPSTR* ppSave = ppszTokens;
		while(*ppszTokens) {
			delete [] *ppszTokens;
			++ppszTokens;
		}
		delete [] ppSave;
	}
}
#endif // _XBOX

//
// WordWrap_InitTokens
//
// Desc.	Initializes the NULL terminated list of NULL terminated strings by allocating
//			memory, and putting NULL at the end of the list to denote NULL.
//
// param:	ppszTokens	Reference to the list of NULL terminated NULL terminated strings
//			iSize		Number of elements (strings) in the list
//
void WordWrap_InitTokensW( LPWSTR* &ppszTokens, int iSize )
{
	if(ppszTokens)
		return;
	ppszTokens = new LPWSTR [iSize+1];
	ppszTokens[iSize] = 0;
}

#ifdef _XBOX
// ANSI version of the above function
//
void WordWrap_InitTokensA( LPSTR* &ppszTokens, int iSize )
{
	if(ppszTokens)
		return;
	ppszTokens = new LPSTR [iSize+1];
	ppszTokens[iSize] = 0;
}
#endif // _XBOX