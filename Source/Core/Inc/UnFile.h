/*=============================================================================
	UnFile.h: General-purpose file utilities.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

/*-----------------------------------------------------------------------------
	Global variables.
-----------------------------------------------------------------------------*/

// Global variables.
CORE_API extern DWORD GCRCTable[];

/*----------------------------------------------------------------------------
	Byte order conversion.
----------------------------------------------------------------------------*/

// Bitfields.
#ifndef NEXT_BITFIELD
	#if __INTEL_BYTE_ORDER__
		#define NEXT_BITFIELD(b) ((b)<<1)
		#define FIRST_BITFIELD   (1)
		#define INTEL_ORDER(x)   (x)
	#else
		#define NEXT_BITFIELD(b) ((b)>>1)
		#define FIRST_BITFIELD   (0x80000000)
		#define INTEL_ORDER(x)   (((x)>>24) + (((x)>>8)&0xff00) + (((x)<<8)&0xff0000) + ((x)<<24))
	#endif
#endif

/*-----------------------------------------------------------------------------
	Stats.
-----------------------------------------------------------------------------*/

#if STATS
	#define STAT(x) x
#else
	#define STAT(x)
#endif

/*-----------------------------------------------------------------------------
	Global init and exit.
-----------------------------------------------------------------------------*/

CORE_API void appInit( const TCHAR* InPackage, const TCHAR* InCmdLine, FMalloc* InMalloc, FOutputDevice* InLog, FOutputDeviceError* InError, FFeedbackContext* InWarn, FFileManager* InFileManager, FConfigCache*(*ConfigFactory)(), UBOOL RequireConfig );
CORE_API void appPreExit();
CORE_API void appExit();

/*-----------------------------------------------------------------------------
	Logging and critical errors.
-----------------------------------------------------------------------------*/

CORE_API void appRequestExit( UBOOL Force );

CORE_API void VARARGS appFailAssert( const ANSICHAR* Expr, const ANSICHAR* File, INT Line );
CORE_API void VARARGS appUnwindf( const TCHAR* Fmt, ... );
CORE_API const TCHAR* appGetSystemErrorMessage( INT Error=0 );
CORE_API const void appDebugMessagef( const TCHAR* Fmt, ... );
CORE_API const UBOOL appMsgf( INT Type, const TCHAR* Fmt, ... );
CORE_API const void appGetLastError( void );
CORE_API const void EdClearLoadErrors();
CORE_API const void EdLoadErrorf( INT Type, const TCHAR* Fmt, ... );
CORE_API void appDebugBreak();
CORE_API UBOOL appIsDebuggerPresent();

/*-----------------------------------------------------------------------------
	Misc.
-----------------------------------------------------------------------------*/

CORE_API void* appGetDllHandle( const TCHAR* DllName );
CORE_API void appFreeDllHandle( void* DllHandle );
CORE_API void* appGetDllExport( void* DllHandle, const TCHAR* ExportName );
CORE_API void appLaunchURL( const TCHAR* URL, const TCHAR* Parms=NULL, FString* Error=NULL );
CORE_API void appSystemCmd( const TCHAR* Cmd, const TCHAR* Parms=NULL, FString* Error=NULL );
CORE_API void* appCreateProc( const TCHAR* URL, const TCHAR* Parms );
CORE_API UBOOL appGetProcReturnCode( void* ProcHandle, INT* ReturnCode );
CORE_API class FGuid appCreateGuid();
CORE_API void appCreateTempFilename( const TCHAR* Path, TCHAR* Result256 );
CORE_API void appCleanFileCache();
CORE_API UBOOL appFindPackageFile( const TCHAR* In, const FGuid* Guid, TCHAR* Out );
CORE_API UBOOL appCreateBitmap( const TCHAR* Pattern, INT Width, INT Height, DWORD* Data, FFileManager* FileManager = GFileManager );

/*-----------------------------------------------------------------------------
	Clipboard.
-----------------------------------------------------------------------------*/

CORE_API void appClipboardCopy( const TCHAR* Str );
CORE_API FString appClipboardPaste();

/*-----------------------------------------------------------------------------
	Guard macros for call stack display.
-----------------------------------------------------------------------------*/

//
// guard/unguardf/unguard macros.
// For showing calling stack when errors occur in major functions.
// Meant to be enabled in release builds.
//

// rjp -- temporary fix for bug in VisualAssistX parser
// VISUAL_ASSIST_HACK is apparently a define built into the VAX VS.NET add-on
#ifdef VISUAL_ASSIST_HACK
	#define guard() guard
	#define unguard() unguard
	#define unguardf() unguardf
	#define guardSlow() guardSlow
	#define unguardSlow() unguardSlow
	#define unguardfSlow() unguardfSlow
#endif	// VISUAL_ASSIST_HACK


#if _DEBUG || !DO_GUARD || __UNIX__
#if __PSX2_EE__ || __GCN__ || /* FIXME: */ __LINUX__
	#define guard(func)			{
#else
	#define guard(func)			{static const TCHAR __FUNC_NAME__[]=TEXT(#func);
#endif
	#define unguard				}
	#define unguardf(msg)		}
#else
	#define guard(func)			{static const TCHAR __FUNC_NAME__[]=TEXT(#func); try{
	#define unguard				}catch(TCHAR*Err){throw Err;}catch(...){appUnwindf(TEXT("%s"),__FUNC_NAME__); throw;}}
	#define unguardf(msg)		}catch(TCHAR*Err){throw Err;}catch(...){appUnwindf(TEXT("%s"),__FUNC_NAME__); appUnwindf msg; throw;}}
#endif

//
// guardSlow/unguardfSlow/unguardSlow macros.
// For showing calling stack when errors occur in performance-critical functions.
// Meant to be disabled in release builds.
//
#if defined(_DEBUG) || !DO_GUARD || !DO_GUARD_SLOW
	#define guardSlow(func)		{
	#define unguardfSlow(msg)	}
	#define unguardSlow			}
	#define unguardfSlow(msg)	}
#else
	#define guardSlow(func)		guard(func)
	#define unguardSlow			unguard
	#define unguardfSlow(msg)	unguardf(msg)
#endif

//
// For throwing string-exceptions which safely propagate through guard/unguard.
//
CORE_API void VARARGS appThrowf( const TCHAR* Fmt, ... );

/*-----------------------------------------------------------------------------
	Check macros for assertions.
-----------------------------------------------------------------------------*/

//
// "check" expressions are only evaluated if enabled.
// "verify" expressions are always evaluated, but only cause an error if enabled.
//
#if DO_CHECK
	#define check(expr)  {if(!(expr)) appFailAssert( #expr, __FILE__, __LINE__ );}
	#define verify(expr) {if(!(expr)) appFailAssert( #expr, __FILE__, __LINE__ );}
#else
#if defined(_MSC_VER)
	#define check __noop
#else
	#define check(...) ((void)0) /* __noop is an MSVC intrinsic: the argument is not evaluated */
#endif
	#define verify(expr) if(expr){}
#endif

//
// Check for development only.
//
#if DO_GUARD_SLOW
	#define checkSlow(expr)  {if(!(expr)) appFailAssert( #expr, __FILE__, __LINE__ );}
	#define verifySlow(expr) {if(!(expr)) appFailAssert( #expr, __FILE__, __LINE__ );}
#else
#if defined(_MSC_VER)
	#define checkSlow __noop
#else
	#define checkSlow(...) ((void)0) /* __noop is an MSVC intrinsic: the argument is not evaluated */
#endif
	#define verifySlow(expr) if(expr){}
#endif

/*-----------------------------------------------------------------------------
	Timing macros.
-----------------------------------------------------------------------------*/

//
// Normal timing.
//
//#define clock(Timer)   {Timer -= appCycles();}
//#define unclock(Timer) {Timer += appCycles()-12;}

#ifndef FINAL_RELEASE //scion jg -- Scoped stats tracking.
// This class tracks the cycles used during the lifetime of this class.
// It updates the item that it has a reference to.
class CORE_API FScopedStat
{
	DWORD* ExternalStat;
	DWORD StartCycles;

public:
	FScopedStat(DWORD* Stat) : ExternalStat(Stat)
	{
		StartCycles = appCycles();
	}

	~FScopedStat()
	{
		*ExternalStat += GetCurrent();
	}

	FORCEINLINE DWORD GetCurrent(void)
	{
		return appCycles() - StartCycles;
	}
};

// This class is a locally declared stat that can only be read via
// the GetCurrent() function, which returns the currently elapsed cycles
class CORE_API FLocalStat
{
	DWORD StartCycles;

public:
	FLocalStat()
	{
		StartCycles = appCycles();
	}

	~FLocalStat()
	{
	}

	FORCEINLINE DWORD GetCurrent(void)
	{
		return appCycles() - StartCycles;
	}
};

// Function level scoping for stats
#define FUNC_CLOCK(Name,Stat) FScopedStat ScopedStat_##Name((DWORD*)&Stat);

// Smaller unit scoped stats
#define CLOCK(Name,Stat) { FUNC_CLOCK(Name,Stat)
#define UNCLOCK() }

// Local stats for simple time logging
#define LOCAL_CLOCK(Name) FLocalStat LocalStat##Name;
#define LOCAL_UNCLOCK(Name) LocalStat##Name.GetCurrent()

// Stat increment methods
#define STAT_INC(x) x++
#define STAT_INC_BY(x,y) x += y
#define STAT_DEC(x) x--
#define STAT_DEC_BY(x,y) x -= y

#else
// Function level scoping for stats
#define FUNC_CLOCK(Name,Stat)

// Smaller unit scoped stats
#define CLOCK(Name,Stat)
#define UNCLOCK()

// Local stats for simple time logging
#define LOCAL_CLOCK(Name)
#define LOCAL_UNCLOCK(Name) 0

// Stat increment methods
#define STAT_INC(x)
#define STAT_INC_BY(x,y)
#define STAT_DEC(x)
#define STAT_DEC_BY(x,y)

#endif

//
// Performance critical timing.
//
#if DO_CLOCK_SLOW
	#define clockSlow(Timer)   {Timer-=appCycles();}
	#define unclockSlow(Timer) {Timer+=appCycles()-12;}
#else
	#define clockSlow(Timer) {}
	#define unclockSlow(Timer) {}
#endif

/*-----------------------------------------------------------------------------
	Text format.
-----------------------------------------------------------------------------*/

CORE_API FString appFormat( FString Src, const TMultiMap<FName,FString>& Map );

/*-----------------------------------------------------------------------------
	File functions.
-----------------------------------------------------------------------------*/

// File utilities.
CORE_API const TCHAR* appFExt( const TCHAR* Filename );
CORE_API UBOOL appUpdateFileModTime( TCHAR* Filename );
CORE_API FString appGetGMTRef();

/*-----------------------------------------------------------------------------
	OS functions.
-----------------------------------------------------------------------------*/

CORE_API const TCHAR* appCmdLine();
// True when a command-line token is a global option of the form Key=Value rather than a map URL
// (a URL's own options follow a '?', so "DM-Map?Name=x" is a URL and "Name=x" is an option).
CORE_API UBOOL appIsGlobalOptionToken( const TCHAR* Token );
// Apply every `-ini:<File>:[<Section>]:<Key>=<Value>` token on the command line (engine-ut2004's switch,
// ported for the launcher's player name and any per-launch setting). <File> is the config cache's generic
// name (`System` = Avaris.ini, `User` = User.ini) or a literal ini name. A value with spaces needs the whole
// token quoted ("-ini:...=a b"); the value is WRITTEN into the ini (the file is marked dirty), not just
// overridden for the run. Called once by appInit, right after GConfig->Init.
CORE_API void appApplyIniOverrides();
// uc2 port (F-4 follow-up): a TEST key of the system ini (Avaris.ini), set from outside with -ini:. One-shot: read once
// and taken out of the in-memory ini of every named section, so a test hook never persists into Avaris.ini (a later
// run of the same tree, or the web's saved ini, cannot inherit it). The value comes from the first section that has
// the key. Returns whether it was there.
CORE_API UBOOL appReadTestKey( const TCHAR* Key, FString& Value, const TCHAR* Section, const TCHAR* Section2=NULL, const TCHAR* Section3=NULL );
// The same for a flag: True/1/Yes/On.
CORE_API UBOOL appReadTestFlag( const TCHAR* Key, const TCHAR* Section, const TCHAR* Section2=NULL, const TCHAR* Section3=NULL );
CORE_API const TCHAR* appBaseDir();
CORE_API const TCHAR* appPackage();
CORE_API const TCHAR* appComputerName();
CORE_API const TCHAR* appUserName();

/*-----------------------------------------------------------------------------
	Timing functions.
-----------------------------------------------------------------------------*/

#if !DEFINED_appCycles
CORE_API DWORD appCycles();
#endif

#if !DEFINED_appCycles64
CORE_API DWORD appCycles64();
#endif

#if !DEFINED_appSeconds
CORE_API DOUBLE appSeconds();
#endif

CORE_API void appSystemTime( INT& Year, INT& Month, INT& DayOfWeek, INT& Day, INT& Hour, INT& Min, INT& Sec, INT& MSec );
CORE_API SQWORD appSystemTime64();
CORE_API const TCHAR* appTimestamp();
CORE_API DOUBLE appSecondsSlow();
CORE_API void appSleep( FLOAT Seconds );

/*-----------------------------------------------------------------------------
	Character type functions.
-----------------------------------------------------------------------------*/
#define UPPER_LOWER_DIFF	32

inline TCHAR appToUpper( TCHAR c )
{
	// some special cases
	switch (c)
	{
		// these are not 32 apart
	case 'ÿ': return 'Ÿ';
	case 'œ': return 'Œ';

		// characters within the 192 - 255 range which have no uppercase/lowercase equivalents
	case 'ð': return c;
	case 'Ð': return c;
	case 'ß': return c;
	case '÷': return c;
	}

	if ( (c>='a' && c<='z') || (c>='ß' && c <= 'ÿ') )
		return c - UPPER_LOWER_DIFF;

	// no uppercase equivalent
	return c;
}
inline TCHAR appToLower( TCHAR c )
{
	// some special cases
	switch (c)
	{
		// these are not 32 apart
	case 'Ÿ': return 'ÿ';
	case 'Œ': return 'œ';

		// characters within the 192 - 255 range which have no uppercase/lowercase equivalents
	case 'ð': return c;
	case 'Ð': return c;
	case 'ß': return c;
	case '÷': return c;
	}

	if ( (c>='À' && c<='ß') || (c>='A' && c<='Z') )
		return c + UPPER_LOWER_DIFF;

	// no lowercase equivalent
	return c;
}
inline UBOOL appIsUpper( TCHAR c )
{
	return
		(c=='Ÿ') || (c=='Œ') ||	// these are outside the standard range
		(c=='ð') || (c=='÷') ||	// these have no lowercase equivalents
		(c>='A' && c<='Z') || (c>='À' && c<='ß');
}
inline UBOOL appIsLower( TCHAR c )
{
	return 
		(c=='œ') ||							// outside the standard range
		(c=='×') || (c=='Ð') ||	(c=='ß') ||	// these have no uppercase equivalents
		(c>='a' && c<='z') || (c>='à'&&c<='ÿ');
}
inline UBOOL appIsAlpha( TCHAR c )
{
	return (c>='A' && c<='Z') || (c>='À' && c<='ÿ') || (c>='a' && c<='z') || (c>='à' && c<='ÿ') ||
		(c=='Ÿ') || (c=='Œ') || (c=='œ');	// these are outside the standard range
		
}
inline UBOOL appIsDigit( TCHAR c )
{
	return c>='0' && c<='9';
}
inline UBOOL appIsAlnum( TCHAR c )
{
	return appIsAlpha(c) || (c>='0' && c<='9');
}

#include <ctype.h>
inline UBOOL appIsSpace( TCHAR c )
{
#if UNICODE
    return( iswspace(c) );
#else
    return( isspace(c) );
#endif
}

/*-----------------------------------------------------------------------------
	String functions.
-----------------------------------------------------------------------------*/

CORE_API const ANSICHAR* appToAnsi( const TCHAR* Str, ANSICHAR* ACh=NULL  );
CORE_API const UNICHAR* appToUnicode( const TCHAR* Str, UNICHAR* UChar=NULL );
CORE_API const TCHAR* appFromAnsi( const ANSICHAR* Str, TCHAR* TCh=NULL );
CORE_API const TCHAR* appFromUnicode( const UNICHAR* Str );
CORE_API UBOOL appIsPureAnsi( const TCHAR* Str );

CORE_API TCHAR* appStrcpy( TCHAR* Dest, const TCHAR* Src );
CORE_API INT appStrcpy( const TCHAR* String );
CORE_API INT appStrlen( const TCHAR* String );
CORE_API TCHAR* appStrstr( const TCHAR* String, const TCHAR* Find );
CORE_API TCHAR* appStrchr( const TCHAR* String, INT c );
CORE_API TCHAR* appStrrchr( const TCHAR* String, INT c );
CORE_API TCHAR* appStrcat( TCHAR* Dest, const TCHAR* Src );
CORE_API TCHAR* appStrdup(const TCHAR* Src);
CORE_API INT appStrcmp( const TCHAR* String1, const TCHAR* String2 );
CORE_API INT appStricmp( const TCHAR* String1, const TCHAR* String2 );
CORE_API INT appStrncmp( const TCHAR* String1, const TCHAR* String2, INT Count );
CORE_API TCHAR* appStaticString1024();
CORE_API ANSICHAR* appAnsiStaticString1024();

CORE_API const TCHAR* appSpc( int Num );
CORE_API TCHAR* appStrncpy( TCHAR* Dest, const TCHAR* Src, int Max);
CORE_API TCHAR* appStrncat( TCHAR* Dest, const TCHAR* Src, int Max);
CORE_API TCHAR* appStrupr( TCHAR* String );
CORE_API const TCHAR* appStrfind(const TCHAR* Str, const TCHAR* Find);
CORE_API DWORD appStrCrc( const TCHAR* Data );
CORE_API DWORD appStrCrcCaps( const TCHAR* Data );
CORE_API INT appAtoi( const TCHAR* Str );
CORE_API TCHAR* appItoa( const INT Num );
CORE_API FLOAT appAtof( const TCHAR* Str );
CORE_API INT appStrtoi( const TCHAR* Start, TCHAR** End, INT Base );
CORE_API INT appStrnicmp( const TCHAR* A, const TCHAR* B, INT Count );
CORE_API INT appSprintf( TCHAR* Dest, const TCHAR* Fmt, ... );
CORE_API INT appSnprintf( TCHAR* Dest, INT Size, const TCHAR* Fmt, ... );
CORE_API void appTrimSpaces( ANSICHAR* String);

CORE_API SQWORD appAtoi64( const TCHAR* Str );
CORE_API TCHAR* appI64toa( const SQWORD Num );

#if UNICODE
	#define appSSCANF	swscanf
#else
	#define appSSCANF	sscanf
#endif

#if _MSC_VER
	CORE_API INT appGetVarArgs( TCHAR* Dest, INT Count, const TCHAR*& Fmt );
	CORE_API INT appGetVarArgsAnsi( ANSICHAR* Dest, INT Count, const ANSICHAR*& Fmt );
#else
	#include <stdio.h>
	#include <stdarg.h>
#endif

typedef int QSORT_RETURN;
typedef QSORT_RETURN(CDECL* QSORT_COMPARE)( const void* A, const void* B );
CORE_API void appQsort( void* Base, INT Num, INT Width, QSORT_COMPARE Compare );
// uc2 port: file names in the order NTFS lists them (FindFiles' order on Epic's machines; readdir() order is
// the file system's). M14 (Editor, .uc files), S2 (Core, localization files). Core/Src/UnObj.cpp.
CORE_API void UC2SortLikeNTFS( TArray<FString>& Files );

//
// Case insensitive string hash function.
//
inline DWORD appStrihash( const TCHAR* Data )
{
	DWORD Hash=0;
	while( *Data )
	{
		TCHAR Ch = appToUpper(*Data++);
		BYTE  B  = Ch;
		Hash     = ((Hash >> 8) & 0x00FFFFFF) ^ GCRCTable[(Hash ^ B) & 0x000000FF];
#if UNICODE
		B        = Ch>>8;
		Hash     = ((Hash >> 8) & 0x00FFFFFF) ^ GCRCTable[(Hash ^ B) & 0x000000FF];
#endif
	}
	return Hash;
}

/*-----------------------------------------------------------------------------
	Parsing functions.
-----------------------------------------------------------------------------*/

CORE_API UBOOL ParseCommand( const TCHAR** Stream, const TCHAR* Match );
CORE_API UBOOL Parse( const TCHAR* Stream, const TCHAR* Match, class FName& Name );
CORE_API UBOOL Parse( const TCHAR* Stream, const TCHAR* Match, DWORD& Value );
CORE_API UBOOL Parse( const TCHAR* Stream, const TCHAR* Match, class FGuid& Guid );
CORE_API UBOOL Parse( const TCHAR* Stream, const TCHAR* Match, TCHAR* Value, INT MaxLen );
CORE_API UBOOL Parse( const TCHAR* Stream, const TCHAR* Match, BYTE& Value );
CORE_API UBOOL Parse( const TCHAR* Stream, const TCHAR* Match, SBYTE& Value );
CORE_API UBOOL Parse( const TCHAR* Stream, const TCHAR* Match, _WORD& Value );
CORE_API UBOOL Parse( const TCHAR* Stream, const TCHAR* Match, SWORD& Value );
CORE_API UBOOL Parse( const TCHAR* Stream, const TCHAR* Match, FLOAT& Value );
CORE_API UBOOL Parse( const TCHAR* Stream, const TCHAR* Match, INT& Value );
CORE_API UBOOL Parse( const TCHAR* Stream, const TCHAR* Match, FString& Value );
CORE_API UBOOL Parse( const TCHAR* Stream, const TCHAR* Match, QWORD& Value );
CORE_API UBOOL Parse( const TCHAR* Stream, const TCHAR* Match, SQWORD& Value );
CORE_API UBOOL ParseUBOOL( const TCHAR* Stream, const TCHAR* Match, UBOOL& OnOff );
CORE_API UBOOL ParseObject( const TCHAR* Stream, const TCHAR* Match, class UClass* Type, class UObject*& DestRes, class UObject* InParent );
CORE_API UBOOL ParseLine( const TCHAR** Stream, TCHAR* Result, INT MaxLen, UBOOL Exact=0 );
CORE_API UBOOL ParseLine( const TCHAR** Stream, FString& Resultd, UBOOL Exact=0 );
CORE_API UBOOL ParseToken( const TCHAR*& Str, TCHAR* Result, INT MaxLen, UBOOL UseEscape );
CORE_API UBOOL ParseToken( const TCHAR*& Str, FString& Arg, UBOOL UseEscape );
CORE_API FString ParseToken( const TCHAR*& Str, UBOOL UseEscape );
CORE_API void ParseNext( const TCHAR** Stream );
CORE_API UBOOL ParseParam( const TCHAR* Stream, const TCHAR* Param );

/*-----------------------------------------------------------------------------
	Math functions.
-----------------------------------------------------------------------------*/

#ifndef _XBOX
CORE_API DOUBLE appExp( DOUBLE Value );
CORE_API DOUBLE appLoge( DOUBLE Value );
CORE_API DOUBLE appFmod( DOUBLE A, DOUBLE B );
CORE_API DOUBLE appSin( DOUBLE Value );
CORE_API DOUBLE appAsin( DOUBLE Value );
CORE_API DOUBLE appCos( DOUBLE Value );
CORE_API DOUBLE appAcos( DOUBLE Value );
CORE_API DOUBLE appTan( DOUBLE Value );
CORE_API DOUBLE appAtan( DOUBLE Value );
CORE_API DOUBLE appAtan2( DOUBLE Y, DOUBLE X );
CORE_API DOUBLE appSqrt( DOUBLE Value );
CORE_API DOUBLE appPow( DOUBLE A, DOUBLE B );

CORE_API FLOAT appExp( FLOAT Value );
CORE_API FLOAT appLoge( FLOAT Value );
CORE_API FLOAT appFmod( FLOAT A, FLOAT B );
CORE_API FLOAT appSin( FLOAT Value );
CORE_API FLOAT appAsin( FLOAT Value );
CORE_API FLOAT appCos( FLOAT Value );
CORE_API FLOAT appAcos( FLOAT Value );
CORE_API FLOAT appTan( FLOAT Value );
CORE_API FLOAT appAtan( FLOAT Value );
CORE_API FLOAT appAtan2( FLOAT Y, FLOAT X );
CORE_API FLOAT appPow( FLOAT A, FLOAT B );

CORE_API UBOOL appIsNan( DOUBLE Value );
CORE_API void appRandInit( INT Seed );
CORE_API INT appRand();
CORE_API FLOAT appFrand();
//scion bcl -- copy sign (faster than signum and then other stuff)
CORE_API DOUBLE appCopySign( DOUBLE A, DOUBLE B);
#else
//jg -- Use instrinsics for all math operations

#include <math.h>
#include <stdlib.h>
#include <float.h>

FORCEINLINE FLOAT appExp( FLOAT Value )
{
	return exp(Value);
}
FORCEINLINE FLOAT appLoge( FLOAT Value )
{
	return log(Value);
}
FORCEINLINE FLOAT appFmod( FLOAT Y, FLOAT X )
{
	return fmod(Y,X);
}
FORCEINLINE FLOAT appSin( FLOAT Value )
{
	return sin(Value);
}
FORCEINLINE FLOAT appAsin( FLOAT Value )
{
	return asin(Value);
}
FORCEINLINE FLOAT appCos( FLOAT Value )
{
	return cos(Value);
}
FORCEINLINE FLOAT appAcos( FLOAT Value )
{
	return acos(Value);
}
FORCEINLINE FLOAT appTan( FLOAT Value )
{
	return tan(Value);
}
FORCEINLINE FLOAT appAtan( FLOAT Value )
{
	return atan(Value);
}
FORCEINLINE FLOAT appAtan2( FLOAT Y, FLOAT X )
{
	return atan2(Y,X);
}
FORCEINLINE FLOAT appPow( FLOAT A, FLOAT B )
{
	return pow(A,B);
}

FORCEINLINE UBOOL appIsNan( FLOAT A )
{
	return A == A;
}

FORCEINLINE INT appRand()
{
	return rand();
}
FORCEINLINE void appRandInit(INT Seed)
{
	srand( Seed );
}

FORCEINLINE FLOAT appFrand()
{
	static FLOAT INV_RAND_MAX = 1.f/(FLOAT)RAND_MAX;
	return rand() * INV_RAND_MAX;
}

FORCEINLINE FLOAT appCopySign( FLOAT A, FLOAT B)
{
	return (FLOAT)_copysign(A,B);
}
#endif

#if !DEFINED_appSRandInit
CORE_API void appSRandInit( INT );
#endif

#if !DEFINED_appSRand
CORE_API FLOAT appSRand();
#endif

#if !DEFINED_appRound
CORE_API INT appRound( FLOAT Value );
#endif

#if !DEFINED_appFloor
CORE_API INT appFloor( FLOAT Value );
#endif

#if !DEFINED_appCeil
CORE_API INT appCeil( FLOAT Value );
#endif

#if !DEFINED_appFractional
CORE_API FLOAT appFractional( FLOAT Value );
#endif

/*-----------------------------------------------------------------------------
	Array functions.
-----------------------------------------------------------------------------*/

// Core functions depending on TArray and FString.
CORE_API UBOOL appLoadFileToArray( TArray<BYTE>& Result, const TCHAR* Filename, FFileManager* FileManager=GFileManager );
CORE_API UBOOL appLoadFileToString( FString& Result, const TCHAR* Filename, FFileManager* FileManager=GFileManager );
CORE_API UBOOL appSaveArrayToFile( const TArray<BYTE>& Array, const TCHAR* Filename, FFileManager* FileManager=GFileManager );
CORE_API UBOOL appSaveStringToFile( const FString& String, const TCHAR* Filename, FFileManager* FileManager=GFileManager );

/*-----------------------------------------------------------------------------
	Memory functions.
-----------------------------------------------------------------------------*/

CORE_API void* appMemmove( void* Dest, const void* Src, INT Count );
CORE_API INT appMemcmp( const void* Buf1, const void* Buf2, INT Count );
CORE_API UBOOL appMemIsZero( const void* V, int Count );
CORE_API DWORD appMemCrc( const void* Data, INT Length, DWORD CRC=0 );
CORE_API void appMemswap( void* Ptr1, void* Ptr2, DWORD Size );
CORE_API void appMemset( void* Dest, INT C, INT Count );

#ifndef DEFINED_appMemcpy
CORE_API void appMemcpy( void* Dest, const void* Src, INT Count );
#endif

#ifndef DEFINED_appMemzero
CORE_API void appMemzero( void* Dest, INT Count );
#endif

//
// C style memory allocation stubs.
//
#define appMalloc     GMalloc->Malloc
#define appFree       GMalloc->Free
#define appRealloc    GMalloc->Realloc

//
// C++ style memory allocation.
//
//scion jg -- Changed the allocator to use the file & line syntax since that is
// more specific in finding leaks
#ifndef FINAL_RELEASE
inline void* operator new( size_t Size,const ANSICHAR* File, INT Line = __LINE__ )
{
	guardSlow(new);
	return appMalloc( Size, File, Line );
	unguardSlow;
}
#if !defined(_MSC_VER)
// NEW T[n]: MSVC 7.1 falls back to the placement operator new above; standard C++ looks for new[].
inline void* operator new[]( size_t Size,const ANSICHAR* File, INT Line = __LINE__ )
{
	guardSlow(new[]);
	return appMalloc( Size, File, Line );
	unguardSlow;
}
#endif
#endif

// UT_REPLACEMENT_ALLOC (engine-ut2004 2.19c): these replace the global allocation functions. GCC/clang
// emit an inline replacement out of line (weak) wherever it is not inlined -- always at -O0 -- and the
// linker then routes the WHOLE program's operator new through GMalloc, including Emscripten's own static
// initialisers that run before appInit (GMalloc is still the error allocator then). Forcing the inline
// keeps engine allocations on GMalloc and system allocations on the C++ runtime at every -O level.
#if defined(__GNUC__) || defined(__clang__)
#define UT_REPLACEMENT_ALLOC __attribute__((always_inline))
#else
#define UT_REPLACEMENT_ALLOC
#endif

UT_REPLACEMENT_ALLOC inline void* operator new( size_t Size )
{
	guardSlow(new);
	return appMalloc( Size );
	unguardSlow;
}

UT_REPLACEMENT_ALLOC inline void operator delete( void* Ptr )
{
	guardSlow(delete);
	appFree( Ptr );
	unguardSlow;
}

#if !defined(_MSC_VER)
// C++14 sized deallocation: without these the compiler calls the C++ runtime's sized operator delete,
// which hands memory that came from GMalloc to free(). (engine-ut2004 defines the same pair.)
UT_REPLACEMENT_ALLOC inline void operator delete( void* Ptr, size_t )
{
	guardSlow(delete);
	appFree( Ptr );
	unguardSlow;
}
#endif

#if PLATFORM_NEEDS_ARRAY_NEW
UT_REPLACEMENT_ALLOC inline void* operator new[]( size_t Size )
{
	guardSlow(new[]);
	return appMalloc( Size );
	unguardSlow;
}

UT_REPLACEMENT_ALLOC inline void operator delete[]( void* Ptr )
{
	guardSlow(delete);
	appFree( Ptr );
	unguardSlow;
}

#if !defined(_MSC_VER)
UT_REPLACEMENT_ALLOC inline void operator delete[]( void* Ptr, size_t )
{
	guardSlow(delete);
	appFree( Ptr );
	unguardSlow;
}
#endif
#endif

#ifndef FINAL_RELEASE
//scion jg -- To compile out file/line number information we use macros to
// trick the compiler into being happy with no ambiguous calls to new

// Mark all new operations with file/line information
#define NEW new(__FILE__,__LINE__)
#define OBJ_NEW new(UObject::GetTransientPackage())
#else
// Don't use file/line info here
#define NEW new
#define OBJ_NEW new
#endif

#if 0//LIN_LOAD
#define USE_GCONTIG
#endif
#ifndef USE_GCONTIG
//scion jg -- Contigous memory allocator for GPU objects
CORE_API void* appContiguousMalloc(DWORD dwSize,DWORD dwAlign);
//scion jg -- Contigous memory deallocator for GPU objects
CORE_API void appContiguousFree(void* p);
#else
#ifndef FINAL_RELEASE
#define appContiguousMalloc(x,y) GContigMalloc->AlignedMalloc(x,y,__FILE__,__LINE__)
#else
#define appContiguousMalloc(x,y) GContigMalloc->AlignedMalloc(x,y)
#endif
#define appContiguousFree(x) GContigMalloc->Free(x)
#endif

/**
 * For non-Unreal allocations
 *
 * @param Size the size of the block needed
 * @param Align the byte alignment for the allocation
 */
void* ExternalContigMalloc(DWORD Size,DWORD Align);

/**
 * Frees a contiguously allocated block of memory
 *
 * @param Data the pointer of the data block being freed
 */
void ExternalContigFree(void* Data);

//scion jg -- Memory protectiong routine for changing CPU access to
// GPU objects. NOTE: 0x02 is PAGE_READONLY
CORE_API void appMemCPUFastAccess(void* pAddr,DWORD dwSize,
	DWORD dwOpts = 0x02);
#ifndef FINAL_RELEASE
//jg -- Silly debug helper for DmXXX() calls
CORE_API const TCHAR* appDecodeDmHRESULT(INT hr);
#endif

/*-----------------------------------------------------------------------------
	Math.
-----------------------------------------------------------------------------*/

CORE_API BYTE appCeilLogTwo( DWORD Arg );

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

