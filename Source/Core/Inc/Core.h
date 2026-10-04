/*=============================================================================
	Core.h: Unreal core public header file.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#ifndef _INC_CORE
#define _INC_CORE

/*----------------------------------------------------------------------------
	Low level includes.
----------------------------------------------------------------------------*/

// API definition.
#ifndef CORE_API
#define CORE_API DLL_IMPORT
#endif

// Unwanted Intel C++ level 4 warnings/ remarks.
#if __ICL
#pragma warning(disable : 873)
#pragma warning(disable : 981)
#pragma warning(disable : 522)
#pragma warning(disable : 271)
#pragma warning(disable : 424)
#pragma warning(disable : 193)
#pragma warning(disable : 444)
#pragma warning(disable : 440)
#pragma warning(disable : 171)
#pragma warning(disable : 1125)
#pragma warning(disable : 488)
#pragma warning(disable : 858)
#pragma warning(disable : 82)
#pragma warning(disable : 1)
#pragma warning(disable : 177)
#pragma warning(disable : 279)
#endif

// Build options.
#include "UnBuild.h"

#if !__PSX2_EE__ && !__GCN__

#define FTime DOUBLE

#else
// Time (needed so that the PS2 doesn't use doubles)
// Note that this is only used on the PC in UTexture::LastUpdateTime
#define FIXTIME 4294967296.f
class FTime
{
#if __GNUG__
#define TIMETYP long long
#else // __MWERKS__
#define TIMETYP unsigned long
#endif
public:

	        FTime      ()               {v=0;}
	        FTime      (float f)        {v=(TIMETYP)(f*FIXTIME);}
//	        FTime      (double d)       {v=(TIMETYP)(d*FIXTIME);}
	        FTime      (TIMETYP i,int)  {v=i;}
	float   GetFloat   ()               {return v/FIXTIME;}
	FTime   operator+  (float f) const  {return FTime(v+(TIMETYP)(f*FIXTIME),0);}
	float   operator-  (FTime t) const  {return (v-t.v)/FIXTIME;}
	FTime   operator*  (float f) const  {return FTime(v*f);}
	FTime   operator/  (float f) const  {return FTime(v/f);}
	FTime&  operator+= (float f)        {v=v+(TIMETYP)(f*FIXTIME); return *this;}
	FTime&  operator*= (float f)        {v=(TIMETYP)(v*f); return *this;}
	FTime&  operator/= (float f)        {v=(TIMETYP)(v/f); return *this;}
	int     operator== (FTime t)        {return v==t.v;}
	int     operator!= (FTime t)        {return v!=t.v;}
	int     operator>  (FTime t)        {return v>t.v;}
	FTime&  operator=  (const FTime& t) {v=t.v; return *this;}
	TIMETYP v;
};
#endif

// Are we on a console or not?
#if defined(_XBOX) || defined(__PSX2_EE__) || defined(__GCN__)
#define CONSOLE 1
#endif

#if _MSC_VER || __ICC || __LINUX__
	#define SUPPORTS_PRAGMA_PACK 1
#else
	#define SUPPORTS_PRAGMA_PACK 0
#endif

// Compiler specific include.
#if __GCN__
	#ifdef EMU
		typedef char TCHAR;
		#include "UnGCNEmu.h"
	#else
		#include <string.h>
		#include "UnMWerks.h" // Using UnVcWin32.h for GCN emulator, should be UnGnuG.h or something.
	#endif
#elif _MSC_VER
	#include "UnVcWin32.h"
#elif __GNUG__
	#include <string.h>
	#include "UnGnuG.h"
#elif __MWERKS__
	#include <string.h>
	#include "UnMWerks.h"
#elif __ICC
	#if __LINUX_X86__
		#include <string.h>
		#include "UnGnuG.h"
	#else
		#error Not yet supported
	#endif
#else
	#error Unknown Compiler
#endif


// OS specific include.
#if __UNIX__
	#include "UnUnix.h"
#ifndef __MWERKS__
	#include <signal.h>
#endif
#endif


// CPU specific include.
#if ((__INTEL__) && (!defined __GNUC__))
#define __HAS_SSE__ 1
#include <xmmintrin.h>
#endif


// Global constants.
enum {MAXBYTE		= 0xff       };
enum {MAXWORD		= 0xffffU    };
enum {MAXDWORD		= 0xffffffffU};
enum {MAXSBYTE		= 0x7f       };
enum {MAXSWORD		= 0x7fff     };
enum {MAXINT		= 0x7fffffff };
enum {INDEX_NONE	= -1         };
enum {UNICODE_BOM   = 0xfeff     };
enum ENoInit {E_NoInit = 0};

enum ERunningOS
{
	OS_WIN95	= 0,
	OS_WIN98,
	OS_WINME,
	OS_WIN2K,
	OS_WINXP,
	OS_WINNT,
	OS_LINUX_X86,
	OS_FREEBSD_X86,
	OS_LINUX_X86_64,
	OS_MAC_OSX_PPC,
	OS_UNKNOWN	= 255
};

// Unicode or single byte character set mappings.
#if 0 //jg -- Removed unicode support
//#ifdef _UNICODE
	//#ifndef _TCHAR_DEFINED
		typedef UNICHAR  TCHAR;
		typedef UNICHARU TCHARU;
	//#endif

    #ifndef _TEXT_DEFINED
	#undef TEXT
	#define TEXT(s) L##s
    #endif

    #ifndef _US_DEFINED
	#undef US
	#define US FString(L"")
    #endif

	inline TCHAR    FromAnsi   ( ANSICHAR In ) { return (BYTE)In;                        }
	inline TCHAR    FromUnicode( UNICHAR In  ) { return In;                              }
	inline ANSICHAR ToAnsi     ( TCHAR In    ) { return (_WORD)In<0x100 ? In : MAXSBYTE; }
	inline UNICHAR  ToUnicode  ( TCHAR In    ) { return In;                              }
#else
	//#ifndef _TCHAR_DEFINED
	#ifdef TCHAR
	#undef TCHAR
	#endif
		typedef ANSICHAR  TCHAR;
		typedef ANSICHARU TCHARU;
	//#endif
	#undef TEXT
	#define TEXT(s) s
	#undef US
	#define US FString("")
	inline TCHAR    FromAnsi   ( ANSICHAR In ) { return In;                              }
	inline TCHAR    FromUnicode( UNICHAR In  ) { return (_WORD)In<0x100 ? In : MAXSBYTE; }
	inline ANSICHAR ToAnsi     ( TCHAR In    ) { return (_WORD)In<0x100 ? In : MAXSBYTE; }
	inline UNICHAR  ToUnicode  ( TCHAR In    ) { return (BYTE)In;                        }
#endif

#if __linux__
// !!! FIXME: dirty, dirty, dirty.  --ryan.
extern TCHAR *GUnixSpawnOnExit;
#endif

/*----------------------------------------------------------------------------
	Forward declarations.
----------------------------------------------------------------------------*/

// Objects.
class	UObject;
class		UExporter;
class		UFactory;
class		UField;
class			UConst;
class			UEnum;
class			UProperty;
class				UByteProperty;
class				UIntProperty;
class				UBoolProperty;
class				UFloatProperty;
class				UObjectProperty;
class					UClassProperty;
class				UNameProperty;
class				UStructProperty;
class               UStrProperty;
class               UArrayProperty;
class				UDelegateProperty;
class			UStruct;
class				UFunction;
class				UState;
class					UClass;
class		ULinker;
class			ULinkerLoad;
class			ULinkerSave;
class		UPackage;
class		USubsystem;
class			USystem;
class		UTextBuffer;
class       URenderDevice;
class		UPackageMap;
class		UDebugger; //DEBUGGER

// Structs.
class FName;
class FArchive;
class FCompactIndex;
class FExec;
class FGuid;
class FMemCache;
class FMemStack;
class FPackageInfo;
class FTransactionBase;
class FUnknown;
class FRepLink;
class FArray;
class FLazyLoader;
class FString;
class FMalloc;

// Templates.
enum
{
#ifdef _XBOX
	// scion sz - reduced from 32 to 1
	DEFAULT_ARRAY_SLACK_PAD=1,
#else
	DEFAULT_ARRAY_SLACK_PAD=32,
#endif
	// scion sz - unchanged (ratio is in 1/8th intervals)
	DEFAULT_ARRAY_SLACK_RATIO=3

};
// scion sz - added SlackRatio,SlackPad
template<class T, INT SlackRatio=DEFAULT_ARRAY_SLACK_RATIO, INT SlackPad=DEFAULT_ARRAY_SLACK_PAD> class TArray;
template<class T> class TTransArray;
template<class T> class TLazyArray;
//scion jg -- Added the third parameter so I could control the minimum hash size
template<class TK, class TI,INT MinHashSize = 8> class TMap;
template<class TK, class TI,INT MinHashSize = 8> class TMultiMap;

// Globals.
CORE_API extern class FOutputDevice* GNull;

// EName definition.
#include "UnNames.h"

/*-----------------------------------------------------------------------------
	Abstract interfaces.
-----------------------------------------------------------------------------*/

// An output device.
class CORE_API FOutputDevice
{
public:
	// FOutputDevice interface.
	virtual void Serialize( const TCHAR* V, EName Event )=0;
	virtual void Flush(){};

	// Simple text printing.
	void Log( const TCHAR* S );
	void Log( enum EName Type, const TCHAR* S );
	void Log( const FString& S );
	void Log( enum EName Type, const FString& S );
	void Logf( const TCHAR* Fmt, ... );
	void Logf( enum EName Type, const TCHAR* Fmt, ... );

// rcg04262002 Not sure what the ramifications are for other platforms yet.
#if ((defined __LINUX__) && (UNICODE))
	void Log( const ANSICHAR* S );
	void Log( enum EName Type, const ANSICHAR* S );
	void Logf( const ANSICHAR* Fmt, ... );
	void Logf( enum EName Type, const ANSICHAR* Fmt, ... );
#endif
};

// Error device.
class CORE_API FOutputDeviceError : public FOutputDevice
{
public:
	virtual void HandleError()=0;
};

// Memory allocator.
class CORE_API FMalloc
{
public:
#ifndef FINAL_RELEASE //scion jg -- Compile out the file/line info in our final build
	virtual void* Malloc( DWORD Count, const ANSICHAR* File = __FILE__, INT Line = __LINE__  )=0;
	virtual void* Realloc( void* Original, DWORD Count, const ANSICHAR* File = __FILE__, INT Line = __LINE__  )=0;
#else
	virtual void* Malloc( DWORD Count )=0;
	virtual void* Realloc( void* Original, DWORD Count )=0;
#endif
	virtual void Free( void* Original )=0;
	virtual void DumpAllocs()=0;
	virtual void DumpAllocsTagged( DWORD FindTag ){}
	virtual void HeapCheck()=0;
	virtual void Init()=0;
	virtual void Exit()=0;
};

//jg -- Base class of contiguous allocator
class CORE_API FAlignedMalloc : public FMalloc
{
public:
#ifndef FINAL_RELEASE
	virtual void* AlignedMalloc(DWORD Size,DWORD Align, const ANSICHAR* File = __FILE__, INT Line = __LINE__ ) = 0;
#else
	virtual void* AlignedMalloc(DWORD Size,DWORD Align) = 0;
#endif
};

// Configuration database cache.
class FConfigCache
{
public:
	virtual UBOOL GetBool( const TCHAR* Section, const TCHAR* Key, UBOOL& Value, const TCHAR* Filename=NULL )=0;
	virtual UBOOL GetInt( const TCHAR* Section, const TCHAR* Key, INT& Value, const TCHAR* Filename=NULL )=0;
	virtual UBOOL GetFloat( const TCHAR* Section, const TCHAR* Key, FLOAT& Value, const TCHAR* Filename=NULL )=0;
	virtual UBOOL GetString( const TCHAR* Section, const TCHAR* Key, TCHAR* Value, INT Size, const TCHAR* Filename=NULL )=0;
	virtual UBOOL GetString( const TCHAR* Section, const TCHAR* Key, class FString& Str, const TCHAR* Filename=NULL )=0;
	virtual INT GetArray( const TCHAR* Section, const TCHAR* Key, TArray<FString>& Arr, const TCHAR* Filename=NULL )=0;
	virtual const TCHAR* GetStr( const TCHAR* Section, const TCHAR* Key, const TCHAR* Filename=NULL )=0;
	virtual UBOOL GetSection( const TCHAR* Section, TCHAR* Value, INT Size, const TCHAR* Filename=NULL )=0;
	virtual TMultiMap<FName,FString>* GetSectionPrivate( const TCHAR* Section, UBOOL Force, UBOOL Const, const TCHAR* Filename=NULL )=0;
	virtual void EmptySection( const TCHAR* Section, const TCHAR* Filename=NULL )=0;
	virtual void SetBool( const TCHAR* Section, const TCHAR* Key, UBOOL Value, const TCHAR* Filename=NULL )=0;
	virtual void SetInt( const TCHAR* Section, const TCHAR* Key, INT Value, const TCHAR* Filename=NULL )=0;
	virtual void SetFloat( const TCHAR* Section, const TCHAR* Key, FLOAT Value, const TCHAR* Filename=NULL )=0;
	virtual void SetString( const TCHAR* Section, const TCHAR* Key, const TCHAR* Value, const TCHAR* Filename=NULL )=0;
	virtual void SetArray( const TCHAR* Section, const TCHAR* Key, const TArray<FString>& Value, const TCHAR* Filename = NULL )=0;
	virtual void Flush( UBOOL Read, const TCHAR* InFilename=NULL, const TCHAR* OutFilename=NULL )=0;
	virtual void UnloadFile( const TCHAR* Filename )=0;
	virtual void UnloadAll()=0;
	virtual void Detach( const TCHAR* Filename )=0;
	virtual void Init( const TCHAR* InSystem, const TCHAR* InUser, UBOOL RequireConfig )=0;
	virtual void Exit()=0;
	virtual void Dump( FOutputDevice& Ar )=0;
	virtual ~FConfigCache() {};
};

// Any object that is capable of taking commands.
class CORE_API FExec
{
public:
	virtual UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar )=0;
};

// Notification hook.
class CORE_API FNotifyHook
{
public:
	virtual void NotifyDestroy( void* Src ) {}
	virtual void NotifyPreChange( void* Src ) {}
	virtual void NotifyPostChange( void* Src ) {}
	virtual void NotifyExec( void* Src, const TCHAR* Cmd ) {}
};

// Interface for returning a context string.
class FContextSupplier
{
public:
	virtual FString GetContext()=0;
};

// A context for displaying modal warning messages.
class CORE_API FFeedbackContext : public FOutputDevice
{
public:
	virtual UBOOL YesNof( const TCHAR* Fmt, ... )=0;
	virtual void BeginSlowTask( const TCHAR* Task, UBOOL StatusWindow )=0;
	virtual void EndSlowTask()=0;
	virtual UBOOL VARARGS StatusUpdatef( INT Numerator, INT Denominator, const TCHAR* Fmt, ... )=0;
	virtual void SetContext( FContextSupplier* InSupplier )=0;
	virtual void MapCheck_Show() {};
	virtual void MapCheck_ShowConditionally() {};
	virtual void MapCheck_Hide() {};
	virtual void MapCheck_Clear() {};
	virtual void MapCheck_Add( INT InType, void* InActor, const TCHAR* InMessage ) {};

	INT WarningCount;
	INT ErrorCount;

	FFeedbackContext()
    : ErrorCount( 0 )
	, WarningCount( 0 )
    {}
};

// Class for handling undo/redo transactions among objects.
typedef void( *STRUCT_AR )( FArchive& Ar, void* TPtr );
typedef void( *STRUCT_DTOR )( void* TPtr );
class CORE_API FTransactionBase
{
public:
	virtual void SaveObject( UObject* Object )=0;
	virtual void SaveArray( UObject* Object, FArray* Array, INT Index, INT Count, INT Oper, INT ElementSize, STRUCT_AR Serializer, STRUCT_DTOR Destructor )=0;
	virtual void Apply()=0;
};

// File manager.
enum EFileTimes
{
	FILETIME_Create      = 0,
	FILETIME_LastAccess  = 1,
	FILETIME_LastWrite   = 2,
};
enum EFileWrite
{
	FILEWRITE_NoFail            = 0x01,
	FILEWRITE_NoReplaceExisting = 0x02,
	FILEWRITE_EvenIfReadOnly    = 0x04,
	FILEWRITE_Unbuffered        = 0x08,
	FILEWRITE_Append			= 0x10,
	FILEWRITE_AllowRead         = 0x20,
};
enum EFileRead
{
	FILEREAD_NoFail             = 0x01,
};
enum ECopyCompress
{
	FILECOPY_Normal				= 0x00,
	FILECOPY_Compress			= 0x01,
	FILECOPY_Decompress			= 0x02,
};
enum ECopyResult
{
	COPY_OK						= 0x00,
	COPY_MiscFail				= 0x01,
	COPY_ReadFail				= 0x02,
	COPY_WriteFail				= 0x03,
	COPY_CompFail				= 0x04,
	COPY_DecompFail				= 0x05,
	COPY_Canceled				= 0x06,
};
#define COMPRESSED_EXTENSION	TEXT(".uz2")

struct FCopyProgress
{
	virtual UBOOL Poll( FLOAT Fraction )=0;
};


CORE_API const TCHAR* appBaseDir();

//jg -- File description class used to verify files
struct CORE_API FFileData
{
	// NOTE: This relies on /Gf optimization being on or it will eat memory
	const TCHAR* PathName;
	const TCHAR* FileName;
	INT FileSize;
};

//jg -- Enum for signed file types
enum ESignatureType
{
	SIG_FLAG_SAVE_GAME		= 0,
	SIG_FLAG_NON_ROAMABLE	= 1
};

class CORE_API FFileManager
{
public:
//jg -- Added these for linear loading
	virtual void OpenLinearFile(const TCHAR* FileName) {}
	virtual void CloseLinearFile(void) {}
	virtual UBOOL IsLinearFileOpen(void) { return 0; }
	virtual UBOOL FindPackageFile(const TCHAR* In,TCHAR* Out) { return 0; }
	virtual FArchive* CreateLinearReader(const TCHAR* FileName) { return NULL; }
	virtual INT GetTOCFileSize(const TCHAR* Filename) { return -1; }
	virtual void GetFileListByExtension(TArray<FString>& Names,const TCHAR* Extension,const TCHAR* Path = NULL) { }
	virtual UBOOL SignFile(const TCHAR* FileName,DWORD VersionNumber,
		ESignatureType SignType = SIG_FLAG_SAVE_GAME) { return 0; }
	virtual UBOOL IsSignedFileValid(const TCHAR* FileName,DWORD VersionNumber,
		ESignatureType SignType = SIG_FLAG_SAVE_GAME) { return 0; }
//jg -- End additions
	//jg -- For external entities needing to find files that are cached or
	// in downloadable content, or path relative due to Demo.
	virtual FString ResolvePath(const TCHAR* In);
	//jg -- Used to set up the cache directories on the xbox
	virtual void CreateCacheDirectory(const ANSICHAR* DirName) {}
	//jg -- Used to delete a list of files
	virtual void DeleteFiles(const TCHAR* FilePattern,UBOOL EvenReadOnly = 0) {}
	virtual void Init(UBOOL Startup) {}
	virtual FArchive* CreateFileReader( const TCHAR* Filename, DWORD ReadFlags=0, FOutputDevice* Error=GNull )=0;
	virtual FArchive* CreateFileWriter( const TCHAR* Filename, DWORD WriteFlags=0, FOutputDevice* Error=GNull )=0;
	virtual INT FileSize( const TCHAR* Filename )=0;
	virtual UBOOL Delete( const TCHAR* Filename, UBOOL RequireExists=0, UBOOL EvenReadOnly=0 )=0;
	virtual UBOOL IsReadOnly( const TCHAR* Filename )=0; //scion capps: stolen from ut2k4
	virtual DWORD Copy( const TCHAR* Dest, const TCHAR* Src, UBOOL Replace=1, UBOOL EvenIfReadOnly=0, UBOOL Attributes=0, DWORD Compress=FILECOPY_Normal, FCopyProgress* Progress=NULL )=0;
	virtual UBOOL Move( const TCHAR* Dest, const TCHAR* Src, UBOOL Replace=1, UBOOL EvenIfReadOnly=0, UBOOL Attributes=0 )=0;
	virtual SQWORD GetGlobalTime( const TCHAR* Filename )=0;
	virtual UBOOL SetGlobalTime( const TCHAR* Filename )=0;
	virtual UBOOL MakeDirectory( const TCHAR* Path, UBOOL Tree=0 )=0;
	virtual UBOOL DeleteDirectory( const TCHAR* Path, UBOOL RequireExists=0, UBOOL Tree=0 )=0;
	virtual TArray<FString> FindFiles( const TCHAR* Filename, UBOOL Files, UBOOL Directories )=0;
	virtual UBOOL SetDefaultDirectory( const TCHAR* Filename )=0;
	virtual FString GetDefaultDirectory()=0;
    virtual const TCHAR *CalcHomeDir(void) { return(appBaseDir()); }
	//scion bcl -- file manager extensions to ease changes in the Xbox version; lifted from Championship
	// gam ---
	virtual FString AbsolutePath( const TCHAR* RelativePath );
	virtual INT CompareFileTimes( const TCHAR* FileA, const TCHAR* FileB ) { return false; }
	// --- gam
	// amb ---
	virtual UBOOL Exists( const TCHAR* Filename ) { return FileSize(Filename) >= 0; }
	// --- amb
	virtual TArray<FString> GetContentLocalizationFiles();
	virtual void SetContentLocalizationFiles(const TArray<FString>& InContentLocalizationFiles);
};

/*----------------------------------------------------------------------------
	Global variables.
----------------------------------------------------------------------------*/

// Core globals.
CORE_API extern FMemStack				GMem;
CORE_API extern FOutputDevice*			GNull;
CORE_API extern FOutputDevice*		    GThrow;
CORE_API extern FOutputDevice* GLog;
CORE_API extern FOutputDeviceError* GError;
CORE_API extern FFeedbackContext*		GWarn;
CORE_API extern FConfigCache*			GConfig;
CORE_API extern FTransactionBase*		GUndo;
CORE_API extern FOutputDevice*			GLogHook;
CORE_API extern FExec*					GExec;
CORE_API extern FMalloc*				GMalloc;

//jg -- Contiguous malloc
CORE_API extern FAlignedMalloc*			GContigMalloc;		/* Contiguous memory allocator */

CORE_API extern FFileManager*			GFileManager;
CORE_API extern USystem*				GSys;
CORE_API extern UProperty*				GProperty;
CORE_API extern BYTE*					GPropAddr;
CORE_API extern UObject*				GPropObject;
CORE_API extern DWORD					GRuntimeUCFlags;
CORE_API extern USubsystem*				GWindowManager;
CORE_API extern USubsystem*				GLoadSaveManager;
#ifndef FINAL_RELEASE
CORE_API extern TCHAR				    GErrorHist[4096];
#else
CORE_API extern TCHAR					GErrorHist[1];		/* For building call stack text dump in guard/unguard mechanism */
#endif
CORE_API extern TCHAR                   GTrue[64], GFalse[64], GYes[64], GNo[64], GNone[64];
CORE_API extern TCHAR					GCdPath[];
#ifdef _XBOX
CORE_API extern	FLOAT					GSecondsPerCycle;
#else
CORE_API extern	DOUBLE					GSecondsPerCycle;
#endif
CORE_API extern	DOUBLE					GTempDouble;
CORE_API extern void					(*GTempFunc)(void*);
CORE_API extern QWORD					GTicks;
CORE_API extern INT                     GScriptCycles;
CORE_API extern DWORD					GPageSize;
CORE_API extern DWORD					GProcessorCount;
CORE_API extern DWORD					GPhysicalMemory;
CORE_API extern DWORD                   GUglyHackFlags;
CORE_API extern UBOOL					GIsScriptable;
#ifndef _XBOX
CORE_API extern UBOOL					GIsEditor;
#else
#define GIsEditor 0
#endif
CORE_API extern	UBOOL					GIgnoreStaticFlag;
CORE_API extern UBOOL					GIsUCC;
CORE_API extern UBOOL					GEdShowFogInViewports;
CORE_API extern UBOOL					GEdSelectionLock;
CORE_API extern UBOOL					GIsClient;
CORE_API extern UBOOL					GIsServer;
CORE_API extern UBOOL					GIsCriticalError;
CORE_API extern UBOOL					GIsStarted;
CORE_API extern UBOOL					GIsRunning;
CORE_API extern UBOOL					GIsGarbageCollecting;
CORE_API extern UBOOL					GIsProcessMarkPass;
CORE_API extern UBOOL					GIsSlowTask;
CORE_API extern UBOOL					GIsGuarded;
CORE_API extern UBOOL					GIsRequestingExit;
CORE_API extern UBOOL					GIsStrict;
CORE_API extern UBOOL                   GScriptEntryTag;
CORE_API extern UBOOL                   GLazyLoad;
CORE_API extern UBOOL					GUnicode;
CORE_API extern UBOOL					GUnicodeOS;
CORE_API extern class FGlobalMath		GMath;
CORE_API extern class FArchive*         GDummySave;
CORE_API extern FLOAT					GAudioMaxRadiusMultiplier; //!! vogel: TODO: merge audio globals into one struct
CORE_API extern FLOAT					GAudioDefaultRadius;
#ifndef _XBOX
CORE_API extern	UDebugger*				GDebugger; //DEBUGGER
#endif
CORE_API extern UBOOL					GIsBenchmarking;
CORE_API extern QWORD					GMakeCacheIDIndex;
CORE_API extern TCHAR                   GBuildLabel[32];
CORE_API extern TCHAR                   GMachineOS[32];
CORE_API extern TCHAR                   GMachineCPU[32];
CORE_API extern TCHAR                   GMachineVideo[32];
CORE_API extern	TCHAR					GIni[32];
CORE_API extern	TCHAR					GUserIni[32];
CORE_API extern FLOAT					NEAR_CLIPPING_PLANE;
CORE_API extern FLOAT					FAR_CLIPPING_PLANE;
CORE_API extern UBOOL					GIsOpenGL;
CORE_API extern ERunningOS				GRunningOS;

CORE_API extern DWORD					GMemoryTag;
CORE_API extern DWORD					GAllocatedMemory;
CORE_API extern UBOOL					GHardwareSkinning;

// Per module globals.
extern "C" DLL_EXPORT TCHAR GPackage[];

//scion jg -- Logging macros/classes that can be conditionally compiled out
#ifdef DEBUG_LOGGING
/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * This class allows us to define a simple macros to log with but have it be
 * compiled out in FINAL_RELEASE builds. This class lives long enough to unpack
 * the var args into a string and sends them to the specified log device. When
 * the FINAL_RELEASE build is compliled this is replaced with __noop
 *
 * ============================================================================
 */
class CORE_API FTraceLog
{
	// The device this instance will log to
	FOutputDevice* pDev;

public:
	FTraceLog(FOutputDevice* _pDev = GLog) : pDev(_pDev) {}

	// Operator for logging var args without a specified FName
	void __cdecl operator()(const TCHAR* pszFmt, ...) const;
	// Operator for logging var args with a specified FName
	void __cdecl operator()(enum EName Type,const TCHAR* pszFmt, ...) const;
};

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * An error generating version of FTraceLog
 *
 * ============================================================================
 */
class CORE_API FErrorTraceLog : public FTraceLog
{
public:
	FErrorTraceLog(FOutputDevice* _pDev = GError) : FTraceLog(_pDev) {}
};

// This macro uses the operator() to pull the var args off the stack and
// send them to GLog
#define debugf FTraceLog()
#define appErrorf FErrorTraceLog()
#define warnf GWarn->Logf
#else
// Causes the logs to be ignored during compilation (leet trick)
#define debugf __noop
#define appErrorf __noop
#define warnf __noop
#endif

// Normal includes.
#include "UnFile.h"			// Low level utility code.
#include "UnObjVer.h"		// Object version info.
#include "UnArc.h"			// Archive class.
#include "UnTemplate.h"     // Dynamic arrays.
#include "UnName.h"			// Global name subsystem.
#include "UnStack.h"		// Script stack definition.
#include "UnObjBas.h"		// Object base class.
#include "UnCoreNet.h"		// Core networking.
#include "UnCorObj.h"		// Core object class definitions.
#include "UnClass.h"		// Class definition.
#include "UnType.h"			// Base property type.
#include "UnScript.h"		// Script class.
#include "UFactory.h"		// Factory definition.
#include "UExporter.h"		// Exporter definition.
#include "UnCache.h"		// Cache based memory management.
#include "UnMem.h"			// Stack based memory management.
#include "UnCId.h"          // Cache ID's.
#include "UnBits.h"         // Bitstream archiver.
#include "UnMath.h"         // Vector math functions.

//jg -- Non-platform specific multi-threaded support.
#include "UnThreadingBase.h"

//jg -- Include the file cache manager interface which needs thread support
#include "FFileCacheManager.h"


// Worker class for tracking loading errors in the editor
class CORE_API FEdLoadError
{
public:
	FEdLoadError()
	{}
	FEdLoadError( INT InType, TCHAR* InDesc )
	{
		Type = InType;
		Desc = InDesc;
	}
	~FEdLoadError()
	{}

	// The types of things that could be missing.
	enum
	{
		TYPE_FILE		= 0,	// A totally missing file
		TYPE_RESOURCE	= 1,	// Texture/Sound/StaticMesh/etc
	};

	INT Type;		// TYPE_
	FString Desc;	// Description of the error

	UBOOL operator==( const FEdLoadError& LE ) const
	{
		return Type==LE.Type && Desc==LE.Desc;
	}
	FEdLoadError& operator=( const FEdLoadError Other )
	{
		Type = Other.Type;
		Desc = Other.Desc;
		return *this;
	}
};

CORE_API extern TArray<FEdLoadError>	GEdLoadErrors;

//
// Archive for counting memory usage.
//
class CORE_API FArchiveCountMem : public FArchive
{
public:
	FArchiveCountMem( UObject* Src )
	: Num(0), Max(0), Contiguous(0)
	{
		Src->Serialize( *this );
	}
	SIZE_T GetNum()
	{
		return Num;
	}
	SIZE_T GetMax()
	{
		return Max;
	}
	SIZE_T GetContiguous()
	{
		return Contiguous;
	}
	void CountBytes( SIZE_T InNum, SIZE_T InMax )
	{
		Num += InNum;
		Max += InMax;
	}
	// scion sz - added iot count contiguous memory separately
	void CountBytes( SIZE_T InNum, SIZE_T InMax, SIZE_T InContiguous )
	{
		Num += InNum;
		Max += InMax;
		Contiguous += InContiguous;
	}
protected:
	SIZE_T Num, Max;
	SIZE_T Contiguous;
};

enum
{
	MCTYPE_ERROR	= 0,
	MCTYPE_WARNING	= 1,
	MCTYPE_NOTE		= 2
};

typedef struct {
	INT Type;
	AActor* Actor;
	FString Message;
} MAPCHECK;

// A convenience to allow referring to axis' by name instead of number
enum EAxis
{
	AXIS_X	= 0,
	AXIS_Y	= 1,
	AXIS_Z	= 2,
};

// Very basic abstract debugger class.
class UDebugger //DEBUGGER
{
public:
	virtual void  DebugInfo( const UObject* Debugee, const FFrame* Stack, BYTE OpCode, INT LineNumber, INT InputPos )=0;
	virtual void  NotifyGC()=0;	// Called when garbage collection has begun
	virtual void  NotifyAccessedNone()=0;
	virtual UBOOL NotifyAssertionFailed( const INT LineNumber )=0;
	virtual UBOOL NotifyInfiniteLoop()=0;
};

#include "UnCoreNative.h"

struct CORE_API FItem
{
	UClass*	Class;
	INT		Count;
	SIZE_T	Num, Max, Contiguous;
	FItem( UClass* InClass=NULL )
	: Class(InClass), Count(0), Num(0), Max(0), Contiguous(0)
	{}
	void Add( FArchiveCountMem& Ar )
	{
		Count++; 
		Num+=Ar.GetNum(); 
		Max+=Ar.GetMax();
		Contiguous += Ar.GetContiguous();
	}
};
struct CORE_API FSubItem
{
	UObject* Object;
	SIZE_T Num, Max, Contiguous;
	FSubItem( UObject* InObject, SIZE_T InNum, SIZE_T InMax, SIZE_T InContiguous=0 )
	: Object( InObject ), Num( InNum ), Max( InMax ), Contiguous( InContiguous )
	{}
};

//
// File Streaming.
//

// gethostbyname() returns a static buffer that is overwritten
//  each call, so we need to serialize access to it so that correct
//  results make it back for each query. This is largely for IpDrv, but
//  UnUnix.cpp uses gethostbyname(), too, so it's in Core. --ryan.
CORE_API extern FCriticalSection *gethostbyname_mutex;

enum EFileStreamType
{
	ST_Regular		= 0,
	ST_Ogg			= 1,
	ST_OggLooping	= 2
};

struct FStream
{
	void*	Data;
	void*	Handle;
	void*	TDD;		// type dependent data
	INT		FileSeek;
	INT		ChunkSize;
	INT		ChunksRequested;
	INT		Used;
	INT		EndOfFile;
	INT		Looped;
	EFileStreamType	Type;
};

class CORE_API FFileStream
{
public:
	static FFileStream* Init( INT MaxStreams );
	static void Destroy();

	// Interface functions.
	INT CreateStream( const TCHAR* Filename, INT ChunkSize, INT InitialChunks, void* Data, EFileStreamType Type, void* TDD );
	void RequestChunks( INT StreamId, INT Chunks, void* Data );
	UBOOL QueryStream( INT StreamId, INT& ChunksQueued );
	void DestroyStream( INT StreamId, UBOOL ReadQueuedChunks );

	// Only use the below functions in the thread's main loop.
	UBOOL Read( INT StreamId, INT Bytes );
	UBOOL Create( INT StreamId, const TCHAR* Filename );
	UBOOL Destroy( INT StreamId );

	static FFileStream* Instance;
	static INT StreamIndex;
	static INT MaxStreams;
	static FStream* Streams;
	static INT Destroyed;
	static FCriticalSection* CriticalSection;

private:
	FFileStream() {}
	~FFileStream() {}
};

CORE_API extern FFileStream*			GFileStream;

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
#endif

