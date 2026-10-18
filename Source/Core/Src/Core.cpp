/*=============================================================================
	Core.cpp: Unreal core.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

#include "CorePrivate.h"

/*-----------------------------------------------------------------------------
	Temporary startup objects.
-----------------------------------------------------------------------------*/

// Error malloc.
class FMallocError : public FMalloc
{
	void Called( const TCHAR* Str )
		{appErrorf( TEXT("Called %s before memory init"), Str );}
#ifndef FINAL_RELEASE
	void* Malloc( DWORD Count, const ANSICHAR* File = __FILE__, INT Line = __LINE__  )
		{Called(TEXT("appMalloc"));return NULL;}
	void* Realloc( void* Original, DWORD Count, const ANSICHAR* File = __FILE__, INT Line = __LINE__  )
		{Called(TEXT("appRealloc"));return NULL;}
#else
	void* Malloc( DWORD Count )
		{Called(TEXT("appMalloc"));return NULL;}
	void* Realloc( void* Original, DWORD Count )
		{Called(TEXT("appRealloc"));return NULL;}
#endif
	void Free( void* Original )
		{Called(TEXT("appFree"));}
	void DumpAllocs()
		{Called(TEXT("appDumpAllocs"));}
	void HeapCheck()
		{Called(TEXT("appHeapCheck"));}
	void Init()
		{Called(TEXT("FMallocError::Init"));}
	void Exit()
		{Called(TEXT("FMallocError::Exit"));}
} MallocError;

class FAlignedMallocError : public FAlignedMalloc
{
	void Called( const TCHAR* Str )
		{appErrorf( TEXT("Called %s before memory init"), Str );}
#ifndef FINAL_RELEASE
	void* Malloc( DWORD Count, const ANSICHAR* File = __FILE__, INT Line = __LINE__  )
		{Called(TEXT("appMalloc"));return NULL;}
	void* Realloc( void* Original, DWORD Count, const ANSICHAR* File = __FILE__, INT Line = __LINE__  )
		{Called(TEXT("appRealloc"));return NULL;}
	void* AlignedMalloc(DWORD Size,DWORD Align, const ANSICHAR* File = __FILE__, INT Line = __LINE__ )
		{Called(TEXT("FMallocError::AlignedMalloc")); return NULL;}
#else
	void* Malloc( DWORD Count )
		{Called(TEXT("appMalloc"));return NULL;}
	void* Realloc( void* Original, DWORD Count )
		{Called(TEXT("appRealloc"));return NULL;}
		void* AlignedMalloc(DWORD Size,DWORD Align)
		{Called(TEXT("FMallocError::AlignedMalloc")); return NULL;}
#endif
	void Free( void* Original )
		{Called(TEXT("appFree"));}
	void DumpAllocs()
		{Called(TEXT("appDumpAllocs"));}
	void HeapCheck()
		{Called(TEXT("appHeapCheck"));}
	void Init()
		{Called(TEXT("FMallocError::Init"));}
	void Exit()
		{Called(TEXT("FMallocError::Exit"));}	
} AlignedMallocError;

// Error file manager.
class FFileManagerError : public FFileManager
{
public:
	FArchive* CreateFileReader( const TCHAR* Filename, DWORD Flags, FOutputDevice* Error )
		{appErrorf(TEXT("Called FFileManagerError::CreateFileReader")); return 0;}
	FArchive* CreateFileWriter( const TCHAR* Filename, DWORD Flags, FOutputDevice* Error )
		{appErrorf(TEXT("Called FFileManagerError::CreateFileWriter")); return 0;}
	INT FileSize( const TCHAR* Filename )
		{return -1;}
	UBOOL Delete( const TCHAR* Filename, UBOOL RequireExists=0, UBOOL EvenReadOnly=0 )
		{return 0;}
	UBOOL IsReadOnly( const TCHAR* Filename ) 
		{return 0;}
	DWORD Copy( const TCHAR* Dest, const TCHAR* Src, UBOOL Replace=1, UBOOL EvenIfReadOnly=0, UBOOL Attributes=0, DWORD Compress=FILECOPY_Normal, FCopyProgress* Progress=NULL )
		{return COPY_MiscFail;}
	UBOOL Move( const TCHAR* Dest, const TCHAR* Src, UBOOL Replace=1, UBOOL EvenIfReadOnly=0, UBOOL Attributes=0 )
		{return 0;}
	SQWORD GetGlobalTime( const TCHAR* Filename )
		{return 0;}
	UBOOL SetGlobalTime( const TCHAR* Filename )
		{return 0;}
	UBOOL MakeDirectory( const TCHAR* Path, UBOOL Tree=0 )
		{return 0;}
	UBOOL DeleteDirectory( const TCHAR* Path, UBOOL RequireExists=0, UBOOL Tree=0 )
		{return 0;}
	TArray<FString> FindFiles( const TCHAR* Filename, UBOOL Files, UBOOL Directories )
		{return TArray<FString>();}
	UBOOL SetDefaultDirectory( const TCHAR* Filename )
		{return 0;}
	FString GetDefaultDirectory()
		{return TEXT("");}
} FileError;

// Critical error output device.
class CORE_API FErrorOutError : public FOutputDeviceError
{
public:
	void Serialize( const TCHAR* V, EName Event )
	{}
	void HandleError()
	{}
} ErrorOutError;

// Log output device.
class CORE_API FLogOutError : public FOutputDevice
{
public:
	void Serialize( const TCHAR* V, EName Event )
	{}
} LogOutError;

// Exception thrower.
class CORE_API FThrowOut : public FOutputDevice
{
public:
	void Serialize( const TCHAR* V, EName Event )
	{
		throw( V );
	}
} ThrowOut;

// Null output device.
class CORE_API FNullOutError : public FOutputDevice
{
public:
	void Serialize( const TCHAR* V, enum EName Event )
	{}
} NullOutError;

// Dummy saver.
class CORE_API FArchiveDummySave : public FArchive
{
public:
	FArchiveDummySave() { ArIsSaving = 1; }
} GArchiveDummySave;

/*-----------------------------------------------------------------------------
	Global variables.
-----------------------------------------------------------------------------*/

CORE_API FMemStack				GMem;							/* Global memory stack */
CORE_API FOutputDevice*			GLog=&LogOutError;				/* Regular logging */
CORE_API FOutputDeviceError*	GError=&ErrorOutError;			/* Critical errors */
CORE_API FOutputDevice*			GNull=&NullOutError;			/* Log to nowhere */
CORE_API FOutputDevice*			GThrow=&ThrowOut;				/* Exception thrower */
CORE_API FFeedbackContext*		GWarn=NULL;						/* User interaction and non critical warnings */
CORE_API FConfigCache*			GConfig=NULL;					/* Configuration database cache */
CORE_API void					(*GConfigFileLoaded)( const TCHAR* Filename, class FConfigFile& File )=NULL;	/* Config file read hook (Core.h) */
CORE_API FTransactionBase*		GUndo=NULL;						/* Transaction tracker, non-NULL when a transaction is in progress */
CORE_API FOutputDevice*			GLogHook=NULL;					/* Launch log output hook */
CORE_API FExec*					GExec=NULL;						/* Launch command-line exec hook */
CORE_API FMalloc*				GMalloc=&MallocError;			/* Memory allocator */

//jg -- Contiguous malloc
CORE_API FAlignedMalloc*		GContigMalloc = &AlignedMallocError;	/* Contiguous memory allocator */

CORE_API FFileManager*			GFileManager=&FileError;		/* File manager */
CORE_API USystem*				GSys=NULL;						/* System control code */
CORE_API UProperty*				GProperty;						/* Property for UnrealScript interpretter */
CORE_API BYTE*					GPropAddr;						/* Property address for UnrealScript interpreter */
CORE_API UObject*				GPropObject;					/* Object with Property for UnrealScript interpreter */
CORE_API DWORD					GRuntimeUCFlags=0;				/* Property for storing flags between calls to bytecode functions */
CORE_API USubsystem*			GWindowManager=NULL;			/* Window update routine called once per tick */
#ifndef FINAL_RELEASE
CORE_API TCHAR					GErrorHist[4096]=TEXT("");		/* For building call stack text dump in guard/unguard mechanism */
#else
CORE_API TCHAR					GErrorHist[1]=TEXT("");		/* For building call stack text dump in guard/unguard mechanism */
#endif
CORE_API TCHAR					GYes[64]=TEXT("Yes");			/* Localized "yes" text */
CORE_API TCHAR					GNo[64]=TEXT("No");				/* Localized "no" text */
CORE_API TCHAR					GTrue[64]=TEXT("True");			/* Localized "true" text */
CORE_API TCHAR					GFalse[64]=TEXT("False");		/* Localized "false" text */
CORE_API TCHAR					GNone[64]=TEXT("None");			/* Localized "none" text */
CORE_API TCHAR                  GCdPath[256]=TEXT("");			/* Cd path, if any */
#ifdef _XBOX
CORE_API FLOAT					GSecondsPerCycle=1.f;			/* Seconds per CPU cycle for this PC */
#else
CORE_API DOUBLE					GSecondsPerCycle=1.0;			/* Seconds per CPU cycle for this PC */
#endif
CORE_API DOUBLE					GTempDouble=0.0;				/* Used during development for timing */
CORE_API void					(*GTempFunc)(void*)=NULL;		/* Used during development for debug hooks */
CORE_API QWORD					GTicks=1;						/* Number of non-persistent ticks thus far in this level, for profiling */
CORE_API INT					GScriptCycles;					/* Times script execution CPU cycles per tick */
CORE_API DWORD					GPageSize=4096;					/* Operating system page size */
CORE_API DWORD					GProcessorCount=1;				/* Number of CPUs in this PC */
CORE_API DWORD					GPhysicalMemory=16384*1024;		/* Bytes of physical memory in this PC */
CORE_API DWORD                  GUglyHackFlags=0;               /* Flags for passing around globally hacked stuff */
CORE_API UBOOL					GIsScriptable=0;				/* Whether script execution is allowed */
#ifndef _XBOX
CORE_API UBOOL					GIsEditor=0;					/* Whether engine was launched for editing */
#endif
CORE_API UBOOL					GIgnoreStaticFlag=0;			/* Whether we should ignore bstatic flag for spawning actors */
CORE_API UBOOL					GIsUCC=0;						/* Is UCC running? */
CORE_API UBOOL					GEdShowFogInViewports=1;		/* Show distance fog in viewports? */
CORE_API UBOOL					GEdSelectionLock=0;				/* Are selections locked? (you can't select/deselect additional actors) */
CORE_API UBOOL					GIsClient=0;					/* Whether engine was launched as a client */
CORE_API UBOOL					GIsServer=0;					/* Whether engine was launched as a server, true if GIsClient */
CORE_API UBOOL					GIsCriticalError=0;				/* An appError() has occured */
CORE_API UBOOL					GIsStarted=0;					/* Whether execution is happening from within main()/WinMain() */
CORE_API UBOOL					GIsGuarded=0;					/* Whether execution is happening within main()/WinMain()'s try/catch handler */
CORE_API UBOOL					GIsRunning=0;					/* Whether execution is happening within MainLoop() */
CORE_API UBOOL					GIsGarbageCollecting=0;			/* Whether we are inside garbage collection */
CORE_API UBOOL					GIsProcessMarkPass=0;			/* scion sz - Whether we are inside FArchiveTagUsed::ProcessMarkPass() */
CORE_API UBOOL					GIsSlowTask=0;					/* Whether there is a slow task in progress */
CORE_API UBOOL					GIsRequestingExit=0;			/* Indicates that MainLoop() should be exited at the end of the current iteration */
CORE_API UBOOL					GIsStrict=0;					/* Causes all UnrealScript execution warnings to be fatal errors */
CORE_API UBOOL					GScriptEntryTag=0;				/* Number of recursive UnrealScript calls currently on the stack */
CORE_API UBOOL					GLazyLoad=0;					/* Whether TLazyLoad arrays should be lazy-loaded or not */
CORE_API FGlobalMath			GMath;							/* Math code */
CORE_API FArchive*				GDummySave=&GArchiveDummySave;	/* No-op save archive */
CORE_API FFileStream*			GFileStream=NULL;				/* File streaming */
CORE_API FLOAT					GAudioMaxRadiusMultiplier=100.f;/* Max distance = Radius * GAudioMaxRadiusMultiplier */
CORE_API FLOAT					GAudioDefaultRadius=80.f;		/* Default radius for PlayOwnedSound */
CORE_API TArray<FEdLoadError>	GEdLoadErrors;					/* For keeping track of load errors in the editor */
#ifndef _XBOX
CORE_API UDebugger*				GDebugger=NULL;					//DEBUGGER /* Unrealscript Debugger */
#endif
CORE_API UBOOL					GIsBenchmarking=0;				/* Whether we are in benchmark mode or not */
CORE_API QWORD					GMakeCacheIDIndex=0;			/* Cache ID */
CORE_API TCHAR                  GBuildLabel[32]	= TEXT("No Label");
CORE_API TCHAR                  GMachineOS[32]	= TEXT("No OS");
CORE_API TCHAR                  GMachineCPU[32]	= TEXT("No CPU");
CORE_API TCHAR                  GMachineVideo[32]	= TEXT("No Video");
CORE_API TCHAR					GIni[32]			= TEXT("Z:\\Avaris.ini");
CORE_API TCHAR					GUserIni[32]		= TEXT("Z:\\User.ini");
CORE_API FLOAT					NEAR_CLIPPING_PLANE	= 10.0f;
CORE_API FLOAT					FAR_CLIPPING_PLANE  = 65536.f;
CORE_API UBOOL					GIsOpenGL = 0;
CORE_API ERunningOS				GRunningOS = OS_UNKNOWN;

CORE_API DWORD					GMemoryTag			= 0;
CORE_API DWORD					GAllocatedMemory	= 0;
CORE_API UBOOL						GHardwareSkinning = 0;

// Unicode.
#if UNICODE
CORE_API UBOOL GUnicode=1;
CORE_API UBOOL GUnicodeOS=0;
#else
CORE_API UBOOL GUnicode=0;
CORE_API UBOOL GUnicodeOS=0;
#endif

// System identification.
#if __INTEL__
#ifndef _XBOX
CORE_API UBOOL GIsSSE=0;
#endif
#endif

// For development.
UBOOL GNoGC=0;
UBOOL GCheckConflicts=0;
UBOOL GExitPurge=0;

//scion bcl
CORE_API class USubsystem *GLoadSaveManager = NULL;
//scion bcl -- modified from Championship; not inline because FString is not a complete type
// in Core.h (crafty, huh?)
// gam ---
// Converts a path like "..\Textures\MyFile.utx" to "C:\UC\Textures\MyFile.utx"
// DOES NOT convert a path like "..\Textures\..\MyFile.utx" to "C:\UC\MyFile.utx"
FString FFileManager::AbsolutePath( const TCHAR* RelativePath )
{
	guard(FFileManager::AbsolutePath);

	if( appStrchr( RelativePath, ':' ) != NULL )
		return( RelativePath );

	if( ( RelativePath[0] == '\\' ) || ( RelativePath[0] == '/' ) )
		return( RelativePath );

	FString NewPath = GetDefaultDirectory();
	const TCHAR* ParsePoint = RelativePath;

	{
		INT SlashPoint = Max( NewPath.InStr( TEXT("\\"), 1 ), NewPath.InStr( TEXT("/"), 1 ) );

		if( SlashPoint == NewPath.Len() - 1 )
			NewPath = NewPath.Left( SlashPoint );
	}

	for(;;)
	{
		if( (appStrncmp( ParsePoint, TEXT("..\\"), 3 ) == 0) || (appStrncmp( ParsePoint, TEXT("../"), 3 ) == 0) )
		{
			INT SlashPoint = Max( NewPath.InStr( TEXT("\\"), 1 ), NewPath.InStr( TEXT("/"), 1 ) );

			if( SlashPoint > 0 )
				NewPath = NewPath.Left( SlashPoint );

			ParsePoint += 3;
		}
		else if( (appStrncmp( ParsePoint, TEXT(".\\"), 2 ) == 0) || (appStrncmp( ParsePoint, TEXT("./"), 2 ) == 0) )
			ParsePoint += 2;
		else
		{
			return( NewPath + PATH_SEPARATOR + ParsePoint );        
		}
	}        

	unguard;
}
// --- gam

/**
 * Meant to be overloaded in child classes. Just returns the same string
 * in this version.
 *
 * @param In The file path to resolve
 *
 * @return The file path that has been resolved (same as in in this one)
 */
FString FFileManager::ResolvePath(const TCHAR* In)
{
	return FString(In);
}

TArray<FString> FFileManager::GetContentLocalizationFiles()
{
	guard(FFileManager::GetContentLocalizationFiles);

	// Return an empty array.
	return TArray<FString>();

	unguard;
}

void FFileManager::SetContentLocalizationFiles(const TArray<FString>& InContentLocalizationFiles)
{
	// Do nothing.
}

/*-----------------------------------------------------------------------------
	Package implementation.
-----------------------------------------------------------------------------*/

IMPLEMENT_PACKAGE(Core);

//jg -- File cache interface
CORE_API FFileCacheManager* GFileCacheManager = NULL;

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

