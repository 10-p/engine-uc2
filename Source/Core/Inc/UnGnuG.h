/*=============================================================================
	UnGnuG.h: Unreal definitions for Gnu G++. Unfinished. Unsupported.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	engine-uc2 (M1): rebased on engine-ut2004's UnGnuG.h (GCC 15 / Emscripten / MinGW port of the UT2004
	header), then given back UC2's platform contract, which is UnVcWin32.h's: ANSI-only TCHAR conversions,
	appTrunc, appSqrt(FLOAT), appInvSqrt, appCycles64, LARGEINT, FMovieThread/FMoviePlayer,
	appGetLanguageID. UC2's own copy of this file was a stale UT2003 leftover. The math intrinsics and
	timer-reset parts of the UT2004 header are NOT here: UC2 declares its math in UnFile.h and defines it in
	UnAnsi.cpp. See docs/porting/analysis/core-port.md.
=============================================================================*/

/*----------------------------------------------------------------------------
	Platform compiler definitions.
----------------------------------------------------------------------------*/
#ifndef _INCL_UNGNUG_H_
#define _INCL_UNGNUG_H_

#include <stdio.h>
#include <stdarg.h>

#if ((defined __LINUX_X86__) || (defined __FreeBSD__))
	#define __UNIX__  1
	#define __LINUX__ 1
	#define __INTEL__ 1
	#define __INTEL_BYTE_ORDER__ 1
	#if __ICC
		#undef ASM
		#undef ASM3DNOW
		#undef ASMKNI
		#define ASMLINUX 1
		#define COMPILER "Compiled with Intel C++"
	#else
		#undef ASM
		#undef ASM3DNOW
		#undef ASMKNI
		#define ASMLINUX 1
		#define COMPILER "Compiled with GNU g++ (" __VERSION__ ")"
	#endif
	// Emscripten: keep ASMLINUX=1 for the preprocessor paths (UT_NO_X86_INTRIN below removes the asm itself).
	#ifdef __EMSCRIPTEN__
		#undef COMPILER
		#define COMPILER "Compiled with Emscripten"
	#endif
#elif (defined MACOSX)
	#define __UNIX__  1
	#undef ASM
	#undef ASM3DNOW
	#undef ASMKNI
	#define COMPILER "Compiled with MacOS X GNU g++ (" __VERSION__ ")"
#elif __PSX2_EE__
	#define __UNIX__ 1
	#define __LINUX__ 1
	#define __INTEL__ 1
	#define __INTEL_BYTE_ORDER__ 1
	#undef ASM
	#undef ASM3DNOW
	#undef ASMKNI
	#undef ASMLINUX
	#define ASMPSX2 1
	#define COMPILER "Compiled with PSX2-EE g++ (" __VERSION__ ")"
	// Force use single precision
	#define tanh(x) tanhf(x)
	#define ceil(x) ceilf(x)
	#define fabs(x) fabsf(x)
	#define floor(x) floorf(x)
	#define cosh(x) coshf(x)
	#define sinh(x) sinhf(x)
	#define exp(x) expf(x)
	#define ldexp(x,y) ldexpf(x,y)
	#define log(x) logf(x)
	#define log10(x) log10f(x)
	#define pow(x,y) powf(x,y)
	#define sqrt(x) sqrtf(x)
	#define fmod(x,y) fmodf(x,y)
	#define sin( x ) sinf( x )
	#define cos( x ) cosf( x )
	#define tan( x ) tanf( x )
	#define asin( x ) asinf( x )
	#define acos( x ) acosf( x )
	#define atan( x ) atanf( x )
	#define atan2( x, y ) atan2f( x, y )
#elif __GCN__
	#define __UNIX__ 1
	#define __LINUX__ 1
	#define __INTEL__ 1
	#define __INTEL_BYTE_ORDER__ 1
	#undef ASM
	#undef ASM3DNOW
	#undef ASMKNI
	#undef ASMLINUX
	#undef ASMPSX2
	#define COMPILER "Compiled for GCN (" __VERSION__ ")"
#elif defined(__MINGW32__)
	// Windows built with GCC (engine-ut2004 2.33). ASMLINUX means "GCC asm", not "Linux"; what must NOT
	// come with it is the Unix identity (__UNIX__/__LINUX__ select the POSIX file/socket/loader paths).
	#define __INTEL__ 1
	#define __INTEL_BYTE_ORDER__ 1
	#undef ASM
	#undef ASM3DNOW
	#undef ASMKNI
	#define ASMLINUX 1
	#define COMPILER "Compiled with MinGW g++ (" __VERSION__ ")"
#else
	#error Unsupported platform.
#endif

// Non-x86 targets (Emscripten/wasm, arm) have no x86 inline asm (rdtsc/rorl/roll/cpuid) and no SSE. They
// share the portable-C fallbacks below. Only __i386__/__x86_64__ keep the x86 inline asm.
#if !defined(__i386__) && !defined(__x86_64__)
	#define UT_NO_X86_INTRIN 1
#endif

// alloca
#include <stdlib.h>

// va_list
#include <stdarg.h>

// FLT_MAX
#include <float.h>


/*----------------------------------------------------------------------------
	Platform specifics types and defines.
----------------------------------------------------------------------------*/

// Windows/MinGW: the Win32 headers already declare DWORD and SIZE_T; take the platform's.
#ifdef _WIN32
#include <minwindef.h>
#include <basetsd.h>
#endif

// Undo any Windows defines.
#undef BYTE
#undef WORD
#undef DWORD
#undef INT
#undef FLOAT
#undef MAXBYTE
#undef MAXWORD
#undef MAXDWORD
#undef MAXINT
#ifndef _WIN32
#undef VOID
#endif
#undef CDECL

// Make sure HANDLE is defined. UC2: pointer-sized, as on UC2's PC build (UnVcWin32.h), not the DWORD of
// the old Unix header -- nothing portable stores one in an int.
#ifndef _WIN32
#define HANDLE void*
#define HINSTANCE void*
#endif

// Sizes.
enum {DEFAULT_ALIGNMENT = 8 }; // Default boundary to align memory allocations on.
enum {CACHE_LINE_SIZE   = 32}; // Cache line size.

// Member alignment. aligned(n) only (no `packed`), as in UT2004's shipped Linux header: GCC honours the
// engine's own #pragma pack blocks. Struct layout against the shipped .u files is checked at M3.
#if __GNUG__
#define GCC_PACK(n)  __attribute__((aligned(n)))
#define GCC_ALIGN(n) __attribute__((aligned(n)))
#define GCC_MOVE_ALIGN(n)
#else
#define GCC_PACK(n)
#define GCC_ALIGN(n)
#define GCC_MOVE_ALIGN(n)
#endif

// Optimization macros
#define DISABLE_OPTIMIZATION
#define ENABLE_OPTIMIZATION

// Function type macros.
#define DLL_IMPORT
#if defined(__PSX2_EE__) || defined(__GCN__)
#define DLL_EXPORT
#else
#define DLL_EXPORT			extern "C"
#endif
#define DLL_EXPORT_CLASS
#define VARARGS
#define CDECL
// On Windows STDCALL is a real calling convention (Win32 callback typedefs carry it).
#ifdef _WIN32
#define STDCALL __stdcall
#else
#define STDCALL
#endif
#define FORCEINLINE inline
#define ZEROARRAY 0 /* Zero-length arrays in structs */
#define __cdecl

#define __forceinline inline

// Variable arguments. UC2's MSVC build routes these through appGetVarArgs(), which reads the stack
// directly; here they are the plain va_list forms.
#define VSNPRINTF vsnprintf

#define GET_VARARGS(msg,len,lastarg,fmt)	\
{	\
	va_list ArgPtr;	\
	va_start( ArgPtr, lastarg );	\
	VSNPRINTF( msg, len, fmt, ArgPtr );	\
	va_end( ArgPtr );	\
}

#define GET_VARARGS_ANSI(msg,len,lastarg,fmt)	\
{	\
	va_list ArgPtr;	\
	va_start( ArgPtr, lastarg );	\
	vsnprintf( msg, len, fmt, ArgPtr );	\
	va_end( ArgPtr );	\
}

#define GET_VARARGS_RESULT(msg,len,lastarg,fmt,result)	\
{	\
	va_list ArgPtr;	\
	va_start( ArgPtr, lastarg );	\
	result = VSNPRINTF( msg, len, fmt, ArgPtr );	\
	va_end( ArgPtr );	\
}

// Unsigned base types.
typedef unsigned char		BYTE;		// 8-bit  unsigned.
typedef unsigned short		_WORD;		// 16-bit unsigned.
#ifndef _WIN32
typedef unsigned int		DWORD;		// 32-bit unsigned.
#endif
typedef unsigned long long	QWORD;		// 64-bit unsigned.

// Signed base types.
typedef	signed char			SBYTE;		// 8-bit  signed.
typedef signed short		SWORD;		// 16-bit signed.
typedef signed int  		INT;		// 32-bit signed.
typedef signed long long	SQWORD;		// 64-bit signed.

// Character types.
typedef char			    ANSICHAR;	// An ANSI character.
typedef unsigned char		ANSICHARU;	// An ANSI character.
typedef unsigned short      UNICHAR;	// A unicode character (UC2: storage type only, TCHAR is ANSI).
typedef unsigned short      UNICHARU;	// A unicode character.

// Other base types.
typedef signed int			UBOOL;		// Boolean 0 (false) or 1 (true).
typedef float				FLOAT;		// 32-bit IEEE floating point.
#ifdef __PSX2_EE__
typedef float				DOUBLE;		// 32-bit IEEE floating point.
#else
typedef double				DOUBLE;		// 64-bit IEEE double.
#endif
#ifndef _WIN32
typedef unsigned int        SIZE_T;     // Corresponds to C SIZE_T.
#endif

// Bitfield type.
#if FORCE_EXTREME_PACKING
typedef BYTE                BITFIELD;	// For bitfields.
#elif defined(_WIN32)
typedef DWORD				BITFIELD;	// Same TYPE as DWORD, so `Ar << Bitfield` picks the DWORD overload.
#else
typedef unsigned int		BITFIELD;	// For bitfields.
#endif

// An unsigned integer as wide as a pointer. UC2's own code does not use it yet (UT2004 added it for
// 64-bit); it is here for the ports of drivers and for M13. `unsigned long` everywhere except LLP64
// (Win64), where long is 4 bytes. On every 32-bit target this is 4 bytes and is NOT DWORD's type.
#if defined(_WIN64) || \
    (defined(__SIZEOF_POINTER__) && defined(__SIZEOF_LONG__) && \
     __SIZEOF_POINTER__ > __SIZEOF_LONG__)
typedef unsigned long long PTRINT;
#else
typedef unsigned long PTRINT;
#endif

// !!! FIXME: This should really just include a header.
#if ((!defined MACOSX) && (!defined __x86_64__) && (!defined __SIZE_TYPE__))
typedef unsigned int size_t;
#endif

// Make sure characters are signed.
#ifdef __CHAR_UNSIGNED__
	#error "Bad compiler option: Characters must be signed"
#endif

// Strings.
#if __UNIX__
#define LINE_TERMINATOR TEXT("\n")
#define PATH_SEPARATOR TEXT("/")
#define DLLEXT TEXT(".so")
#else
#define LINE_TERMINATOR TEXT("\r\n")
#define PATH_SEPARATOR TEXT("\\")
#define DLLEXT TEXT(".dll")
#endif

// NULL.
#undef NULL
#define NULL 0

// Package implementation. With __STATIC_LINK, appGetDllHandle() looks this symbol up (dlsym) to answer
// "is package X linked into this binary?".
#define IMPLEMENT_PACKAGE_PLATFORM(pkgname) \
	BYTE GLoaded##pkgname;

// Platform support options.
#define PLATFORM_NEEDS_ARRAY_NEW 1
#define FORCE_ANSI_LOG           0

// OS string conversion. UC2 removed Unicode ("jg -- Removed unicode support", Core.h): TCHAR is ANSICHAR
// on every target, so these are the casts of UC2's PC build (UnVcWin32.h), which also drop const.
#if defined(UNICODE) && !defined(NO_UNICODE_OS_SUPPORT)
	#error "UC2 is ANSI-only: TCHAR is ANSICHAR. Do not define UNICODE (build with NO_UNICODE_OS_SUPPORT)."
#endif
#define TCHAR_CALL_OS(funcW,funcA) (funcA)
#define TCHAR_TO_ANSI(str) (ANSICHAR*)(str)
#define TCHAR_TO_OEM(str) (ANSICHAR*)(str)
#define ANSI_TO_TCHAR(str) (ANSICHAR*)(str)
// The Unix file manager's helpers (engine-ut2004 FFileManagerUnix.h); with ANSI TCHAR they copy.
inline char* unixToANSI( char* out, const char* in ) { char* ret = out; if(out != in) { while((*out++ = *in++)); } return ret; }
inline int unixGetSizeANSI( const char* str ) { int len=0; while(str[len]) len++; return len+1; }
inline char* unixToUNICODE( char* out, const char* in ) { char* ret = out; if(out != in) { while((*out++ = *in++)); } return ret; }
inline int unixGetSizeUNICODE( const char* str ) { int len=0; while(str[len]) len++; return len+1; }
inline char* unixANSIToUNICODE( char* str ) { return str; }

// Memory
#define appAlloca(size) ((size==0) ? 0 : alloca((size+7)&~7))

// System identification.
extern "C"
{
	extern HINSTANCE      hInstance;
	extern CORE_API UBOOL GIsSSE;
}

// Module name
extern ANSICHAR GModule[32];
extern CORE_API DOUBLE GSecondsPerCycle;

char *strlwr(char *str);

CORE_API DOUBLE appSecondsSlow();

/*----------------------------------------------------------------------------
	Functions (UC2 contract, mirrors UnVcWin32.h's PC branch).
----------------------------------------------------------------------------*/

// Converts to an integer with truncation towards zero.
#define appTrunc(x) (INT)(x)

// Single-precision square root, defined in UnUnix.cpp (UnAnsi.cpp has the DOUBLE overload).
CORE_API FLOAT appSqrt(FLOAT fl);

// Inverse square root. The Xbox build uses rsqrtss + one Newton-Raphson step; the PC build divides.
inline FLOAT appInvSqrt( FLOAT F ) { return 1.f / appSqrt(F); }

// Defined in UnUnix.cpp; launchers call it with argv[0] to locate the binary.
#define DEFINED_appArgv0 1
CORE_API void appArgv0(const char *argv0);

// A win32 LARGE_INTEGER.
typedef union _LARGEINT
{
	struct
	{
		DWORD LowPart;
		DWORD HighPart;
	};
	QWORD QuadPart;
} LARGEINT;

//
// CPU cycles, related to GSecondsPerCycle.
//
// x86: the time-stamp counter. Elsewhere (UT_NO_X86_INTRIN) a microsecond counter built on
// appSecondsSlow(); appPlatformInit() pins GSecondsPerCycle to 1e-6 on that path so the two agree.
// Callers only ever take unsigned differences, which stay right across a wrap.
//
#if ASMLINUX
#define DEFINED_appCycles 1
inline DWORD appCycles()
{
	DWORD r;
#ifdef UT_NO_X86_INTRIN
	r = (DWORD)(appSecondsSlow() * 1000000.0);
#else
	asm volatile("rdtsc" : "=a" (r) : : "edx");
#endif
	return r;
}

#define DEFINED_appCycles64 1
inline SQWORD appCycles64()
{
#ifdef UT_NO_X86_INTRIN
	return (SQWORD)(appSecondsSlow() * 1000000.0);
#else
	DWORD L,H;
	asm volatile("rdtsc" : "=a" (L), "=d" (H));
	return ((SQWORD)L +  ((SQWORD)H<<32));
#endif
}
#endif

//
// Seconds, arbitrarily based: out of line in UnUnix.cpp (gettimeofday based, as in engine-ut2004 --
// the rdtsc version drifts with CPU frequency scaling).
//

//
// Memory copy.
//
#define DEFINED_appMemcpy 1
inline void appMemcpy( void* Dest, const void* Src, INT Count )
{
	memcpy( Dest, Src, Count );
}

//
// Memory zero.
//
#define DEFINED_appMemzero 1
inline void appMemzero( void* Dest, INT Count )
{
	memset( Dest, 0, Count );
}

#if ASMLINUX
#ifndef _rotr
inline DWORD _rotr(DWORD val, INT shift)
{
#ifdef UT_NO_X86_INTRIN
	shift &= 31;
	return shift ? ((val >> shift) | (val << (32 - shift))) : val;
#else
	DWORD retval;
	asm("rorl %%cl, %%eax" : "=a" (retval) : "a" (val), "c" (shift));
	return retval;
#endif
}
#endif

#ifndef _rotl
inline DWORD _rotl(DWORD val, INT shift)
{
#ifdef UT_NO_X86_INTRIN
	shift &= 31;
	return shift ? ((val << shift) | (val >> (32 - shift))) : val;
#else
	DWORD retval;
	asm("roll %%cl, %%eax" : "=a" (retval) : "a" (val), "c" (shift));
	return retval;
#endif
}
#endif
#endif

/*----------------------------------------------------------------------------
	UC2 platform classes (UnVcWin32.h:792-819 on the MSVC build).
----------------------------------------------------------------------------*/

// FMovieThread, FMoviePlayer (UnVcWin32.h:792-806 on the MSVC build): one portable definition (M9).
#include "UnMovieBase.h"

//scion jg -- Wrapper around the xbox function for determining language (UnUnix.cpp: LC_INT, as on PC)
CORE_API DWORD appGetLanguageID(void);

//epic bcl -- The player list item type (needs to be at the global scope)
class AvarisPlayerItem;

#endif  // include-once blocker.

/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/
