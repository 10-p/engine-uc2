/*=============================================================================
	UCC.cpp: Unreal command-line launcher.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Tim Sweeney.
=============================================================================*/

#if _MSC_VER	// engine-uc2: MinGW takes the portable path (FFileManagerUnix, POSIX threading, argv)
	#include <windows.h>
#else
	#include <errno.h>
	#include <sys/stat.h>
#endif
#include <malloc.h>
#include <stdio.h>

// Core and Engine
#include "Engine.h"

#if __STATIC_LINK

// Extra stuff for static links for Engine.
//#include "UnCon.h"
#include "UnRender.h"
#include "UnNet.h"

// NullNetDriver static stuff
//#include "NullNetDriver.h"

#if !defined(_MSC_VER)
// Every package linked into the portable ucc, as XboxLaunch/Src/Launch.cpp does for the static XBE.
#include "ULoadSaveManager.h"
#include "UnNullRenderDevice.h"
#include "UnIpDrv.h"
#include "UnTcpNetDriver.h"
#include "UnTcpNetDriverXbox.h"
#include "UnIpDrvCommandlets.h"
#include "UWeb.h"
#include "UWebNative.h"
#include "UnrealGame.h"
#include "UCElements.h"
#include "UCGameplay.h"
#include "GUI.h"
#if UC2_WITH_UC2ALAUDIO	// M8: the cooked-audio check commandlet (UC2ALAudio.UC2AudioCheck)
#include "UC2ALAudio.h"
#endif
#if UC2_WITH_UC2MOVIE	// M9: the movie player and its self-test commandlet (UC2Movie.UC2MovieSelfTest)
#include "UC2Movie.h"
#endif
#endif

#endif

INT GFilesOpen, GFilesOpened;

/*-----------------------------------------------------------------------------
	Global variables.
-----------------------------------------------------------------------------*/

// General.
#if _MSC_VER
	extern "C" {HINSTANCE hInstance;}
#endif
extern "C" {TCHAR GPackage[64]=TEXT("UCC");}

// Log.
#include "FOutputDeviceFile.h"
FOutputDeviceFile Log;

// Error.
#include "FOutputDeviceAnsiError.h"
FOutputDeviceAnsiError Error;

// Feedback.
#include "FFeedbackContextAnsi.h"
FFeedbackContextAnsi Warn;

// File manager.
#if _MSC_VER
	#include "FFileManagerWindows.h"
	FFileManagerWindows FileManager;
	#include "FMallocAnsi.h"
	FMallocAnsi Malloc;
#elif __PSX2_EE__
	#include "FFileManagerPSX2.h"
	FFileManagerPSX2 FileManager;
	#include "FMallocAnsi.h"
	FMallocAnsi Malloc;
#elif __LINUX_X86__ || defined(__MINGW32__)
	#include "FFileManagerLinux.h"
	FFileManagerLinux FileManager;
	#include "FMallocAnsi.h"
	FMallocAnsi Malloc;
#else
	#include "FFileManagerAnsi.h"
	FFileManagerAnsi FileManager;
	#include "FMallocAnsi.h"
	FMallocAnsi Malloc;
#endif

// Config.
#include "FConfigCacheIni.h"

//jg -- Thread synchronization factory
#if _MSC_VER
#include "UnThreadingWindows.h"
FSynchronizeFactoryWin SynchFactory;
FThreadFactoryWin ThreadFactory;
#else
#include "UnThreadingPosix.h"
FSynchronizeFactoryPosix SynchFactory;
FThreadFactoryPosix ThreadFactory;
#endif

/*-----------------------------------------------------------------------------
	Main.
-----------------------------------------------------------------------------*/

// Unreal command-line applet executor.
FString RightPad( FString In, INT Count )
{
	while( In.Len()<Count )
		In += TEXT(" ");
	return In;
}
static INT Compare( FString& A, FString& B )
{
	return appStricmp( *A, *B );
}
void ShowBanner( FOutputDevice& Warn )
{
	Warn.Logf( TEXT("=======================================") );
	Warn.Logf( TEXT("ucc.exe: UnrealOS execution environment") );
	Warn.Logf( TEXT("Copyright 2001 Epic Games Inc") );
	Warn.Logf( TEXT("=======================================") );
	Warn.Logf( TEXT("") );
}

#if __DO_LINUX_MCHECKING__
extern "C"
{
    int mcheck(void *feh);
}
#endif

#if __STATIC_LINK && !defined(_MSC_VER)
// "ucc verifyclasses": compare the script layout of every native class with the C++ layout, using the lists the
// generated class headers carry for the check in UGameEngine::LoadMap. That check only runs once a map has
// loaded, which is too late to diagnose a layout problem that breaks loading itself. Returns the mismatch count
// as seen in the log ("VERIFY: ..." lines).
static UBOOL VerifyNativeClasses()
{
	static const TCHAR* Packages[] = { TEXT("Engine"), TEXT("IpDrv"), TEXT("UnrealGame"), TEXT("UCElements"), TEXT("UCGameplay"), TEXT("GUI") };
	for( INT i=0; i<ARRAY_COUNT(Packages); i++ )
	{
		debugf( TEXT("VERIFY: loading package %s"), Packages[i] );
		UObject::LoadPackage( NULL, Packages[i], LOAD_NoFail );
	}
	UBOOL Mismatch = false;
	#define VERIFY_CLASS_SIZES
	#define NAMES_ONLY
	#define AUTOGENERATE_NAME(name)
	#define AUTOGENERATE_FUNCTION(cls,idx,name)
	#include "EngineClasses.h"
	#include "IpDrvClasses.h"
	#include "UnrealGameClasses.h"
	#include "UCElementsClasses.h"
	#include "UCGameplayClasses.h"
	#include "GUIClasses.h"
	#undef AUTOGENERATE_FUNCTION
	#undef AUTOGENERATE_NAME
	#undef NAMES_ONLY
	#undef VERIFY_CLASS_SIZES
	debugf( TEXT("VERIFY: %s"), Mismatch ? TEXT("C++ / script class layout MISMATCH") : TEXT("all native class layouts match") );
	return !Mismatch;
}
#endif

int main( int argc, char* argv[] )
{
    #if __LINUX__ || defined(__MINGW32__)
        setbuf(stdout, NULL);
        setbuf(stderr, NULL);
        #if __DO_LINUX_MCHECKING__
            mcheck(NULL);
        #endif
    #endif

	// (The static lookup tables are filled after appInit, with the class registrants; see below.)

	//scion jg -- Default to having an error
	INT ErrorLevel = 1;
	GIsStarted     = 1;
#ifndef _DEBUG
	try
#endif
	{
		GIsGuarded = 1;

		#if !_MSC_VER
		// Set module name.
		strcpy( GModule, "ucc" );
		//!!vogel: pathname might be too long
		//appStrcpy( GModule, argv[0] );
		#endif
		
		// Parse command line.
		TCHAR CmdLine[1024], *CmdLinePtr=CmdLine;
		*CmdLinePtr = 0;
		#if _MSC_VER
			ANSICHAR* Ch = GetCommandLineA();
			while( *Ch && *Ch!=' ' )
				Ch++;
			while( *Ch==' ' )
				Ch++;
			while( *Ch )
				*CmdLinePtr++ = *Ch++;
			*CmdLinePtr++ = 0;
		#else
			for( INT i=2; i<argc; i++ )
			{
				if( i>2 )
					appStrcat( CmdLine, TEXT(" ") );
				appStrcat( CmdLine, ANSI_TO_TCHAR(argv[i]) );
			}
		#endif

		//jg -- Set the pointer to the synchronization factory
		GSynchronizeFactory = &SynchFactory;
		GThreadFactory = &ThreadFactory;

		// Init engine core.
		appInit( TEXT("Avaris"), CmdLine, &Malloc, &Log, &Error, &Warn, &FileManager, FConfigCacheIni::Factory, 1 ); //!!

		// Init static classes: the native lookup tables and class registrants of every linked package,
		// in the order XboxLaunch/Src/Launch.cpp uses for the static XBE.
		#if __STATIC_LINK
			for( INT k=0; k<ARRAY_COUNT(GNativeLookupFuncs); k++ )
				GNativeLookupFuncs[k] = NULL;
			INT Lookup = 0;
			GNativeLookupFuncs[Lookup++] = &FindCoreUObjectNative;
			GNativeLookupFuncs[Lookup++] = &FindCoreUCommandletNative;
			AUTO_INITIALIZE_REGISTRANTS_ENGINE;
		#if !defined(_MSC_VER)
			AUTO_INITIALIZE_REGISTRANTS_IPDRV;
			AUTO_INITIALIZE_REGISTRANTS_UWEB;
			// UWeb's hand-written static header registers its classes but not its native lookups.
			GNativeLookupFuncs[Lookup++] = &FindUWebUWebResponseNative;
			GNativeLookupFuncs[Lookup++] = &FindUWebUWebRequestNative;
			AUTO_INITIALIZE_REGISTRANTS_UNREALGAME;
			AUTO_INITIALIZE_REGISTRANTS_UCELEMENTS;
			AUTO_INITIALIZE_REGISTRANTS_UCGAMEPLAY;
			AUTO_INITIALIZE_REGISTRANTS_GUI;
#if UC2_WITH_UC2ALAUDIO	// M8
			AUTO_INITIALIZE_REGISTRANTS_UC2ALAUDIO;
#endif
#if UC2_WITH_UC2MOVIE	// M9
			AUTO_INITIALIZE_REGISTRANTS_UC2MOVIE;
#endif
			check( Lookup < ARRAY_COUNT(GNativeLookupFuncs) );
			RegisterNamesGUI();
			RegisterNamesUCElements();
			RegisterNamesUCGameplay();
		#endif
		#endif

		// Set up meta package readers.
//		for( i=0; i<GSys->MetaPackages.Num(); i++ )
//		{
//			printf("%s\n", *GSys->MetaPackages(i));
//			((FFileManagerPSX2*) GFileManager)->AddMetaArchive( *GSys->MetaPackages(i), &Error );
//		}

		// Get the ucc stuff going.	
		UObject::SetLanguage(TEXT("int"));
		FString Token = argc>1 ? appFromAnsi(argv[1]) : TEXT("");
		TArray<FRegistryObjectInfo> List;
		UObject::GetRegistryObjects( List, UClass::StaticClass(), UCommandlet::StaticClass(), 0 );
		GIsClient = GIsServer = GIsEditor = GIsScriptable = GIsUCC = 1;
		GLazyLoad = 0;
		UBOOL Help = 0;
		DWORD LoadFlags = LOAD_NoWarn | LOAD_Quiet;
		if( Token==TEXT("") )
		{
			ShowBanner( Warn );
			Warn.Logf( TEXT("Use \"ucc help\" for help") );
		}
#if __STATIC_LINK && !defined(_MSC_VER)
		else if( Token==TEXT("VERIFYCLASSES") )
		{
			UBOOL Ok = VerifyNativeClasses();
			Warn.Logf( TEXT("%s (details: the VERIFY lines in the log)"), Ok ? TEXT("All native class layouts match") : TEXT("Class layout mismatches found") );
			ErrorLevel = Ok ? 0 : 1;
		}
#endif
		else if( Token==TEXT("HELP") )
		{
			ShowBanner( Warn );
			verify(UObject::StaticLoadClass( UCommandlet::StaticClass(), NULL, TEXT("Core.Commandlet"), NULL, LOAD_NoFail, NULL )==UCommandlet::StaticClass());
			const TCHAR* Tmp = appCmdLine();
			GIsEditor = 0; // To enable loading localized text.
			if( !ParseToken( Tmp, Token, 0 ) )
			{
				INT i;
				Warn.Logf( TEXT("Usage:") );
				Warn.Logf( TEXT("   ucc <command> <parameters>") );
				Warn.Logf( TEXT("") );
				Warn.Logf( TEXT("Commands for \"ucc\":") );
				TArray<FString> Items;
				for( i=0; i<List.Num(); i++ )
				{
					UClass* Class = UObject::StaticLoadClass( UCommandlet::StaticClass(), NULL, *List(i).Object, NULL, LoadFlags, NULL );
					if( Class )
					{
						UCommandlet* Default = (UCommandlet*)Class->GetDefaultObject();
						new(Items)FString( FString(TEXT("   ucc ")) + RightPad(Default->HelpCmd,21) + TEXT(" ") + Default->HelpOneLiner );
					}
				}
				new(Items)FString( TEXT("   ucc help <command>        Get help on a command") );
				Sort( &Items(0), Items.Num() );
				for( i=0; i<Items.Num(); i++ )
					Warn.Log( Items(i) );
			}
			else
			{
				Help = 1;
				goto Process;
			}
		}
		else
		{
			// Look it up.
			if( Token==TEXT("Make") )
				LoadFlags |= LOAD_DisallowFiles;
		Process:
			UClass* Class = UObject::StaticLoadClass( UCommandlet::StaticClass(), NULL, *Token, NULL, LoadFlags, NULL );
			if( !Class )
				Class = UObject::StaticLoadClass( UCommandlet::StaticClass(), NULL, *(Token+TEXT("Commandlet")), NULL, LoadFlags, NULL );
			if( !Class )
			{
				INT i;
				for( i=0; i<List.Num(); i++ )
				{
					FString Str = List(i).Object;
					while( Str.InStr(TEXT("."))>=0 )
						Str = Str.Mid(Str.InStr(TEXT("."))+1);
					if( Token==Str || Token+TEXT("Commandlet")==Str )
						break;
				}
				if( i<List.Num() )
					Class = UObject::StaticLoadClass( UCommandlet::StaticClass(), NULL, *List(i).Object, NULL, LoadFlags, NULL );
			}
			if( Class )
			{
				UCommandlet* Default = (UCommandlet*)Class->GetDefaultObject();
				if( Help )
				{
					// Get help on it.
					if( Default->HelpUsage!=TEXT("") )
					{
						Warn.Logf( TEXT("Usage:") );
						Warn.Logf( TEXT("   ucc %s"), *Default->HelpUsage );
					}
					if( Default->HelpParm[0]!=TEXT("") )
					{
						Warn.Logf( TEXT("") );
						Warn.Logf( TEXT("Parameters:") );
						for( INT i=0; i<ARRAY_COUNT(Default->HelpParm) && Default->HelpParm[i]!=TEXT(""); i++ )
							Warn.Logf( TEXT("   %s %s"), *RightPad(Default->HelpParm[i],16), *Default->HelpDesc[i] );
					}
					if( Default->HelpWebLink!=TEXT("") )
					{
						Warn.Logf( TEXT("") );
						Warn.Logf( TEXT("For more info, see") );
						Warn.Logf( TEXT("   %s"), *Default->HelpWebLink );
					}
				}
				else
				{
					// Run it.
					if( Default->LogToStdout )
					{
						Warn.AuxOut = GLog;
						GLog        = &Warn;
					}
					if( Default->ShowBanner )
					{
						ShowBanner( Warn );
					}
					debugf( TEXT("Executing %s"), Class->GetFullName() );
					GIsClient = Default->IsClient;
					GIsServer = Default->IsServer;
					GIsEditor = Default->IsEditor;
					GLazyLoad = Default->LazyLoad;
					UCommandlet* Commandlet = ConstructObject<UCommandlet>( Class );
					Commandlet->InitExecution();
					Commandlet->ParseParms( appCmdLine() );
					Commandlet->Main( appCmdLine() );
					if( Commandlet->ShowErrorCount )
					{
                        if ( Warn.ErrorCount == 0)
						    warnf( TEXT("Success - %d error(s), %d warning(s)"), Warn.ErrorCount, Warn.WarningCount );
                        else
                        {
						    warnf( TEXT("Failure - %d error(s), %d warning(s)"), Warn.ErrorCount, Warn.WarningCount );
                        }
					}
					//scion jg -- Determine if there is an error or not
					if (Warn.ErrorCount == 0 && Warn.WarningCount == 0)
					{
						ErrorLevel = 0;
					}
					if( Default->LogToStdout )
					{
						Warn.AuxOut = NULL;
						GLog        = &Log;
					}
				}
			}
			else
			{
				ShowBanner( Warn );
				Warn.Logf( TEXT("Commandlet %s not found"), *Token );
			}
		}
		appPreExit();
		GIsGuarded = 0;
	}
#ifndef _DEBUG
	catch( ... )
	{
		// Crashed.
		ErrorLevel = 1;
		GIsGuarded = 0;
		Error.HandleError();
	}
#endif
	appExit();
	GIsStarted = 0;
	return ErrorLevel;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
