/*=============================================================================
	DebuggerLaunch.cpp: Debugger/Game launcher 
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Tim Sweeney.
	* Hijacked for UDebugger purposes by Lucas Alonso, Demiurge Studios.
=============================================================================*/

#include "DebuggerLaunchPrivate.h"
#include "UnEngineWin.h"

/*-----------------------------------------------------------------------------
	Global variables.
-----------------------------------------------------------------------------*/

// General.
extern "C" {HINSTANCE hInstance;}
extern "C" {TCHAR GPackage[64]=TEXT("Launch");}

// Memory allocator.
#if 0
	#include "FMallocDebug.h"
	FMallocDebug Malloc;
#else
	#include "FMallocWindows.h"
	FMallocWindows Malloc;
#endif

// Log file.
#include "FOutputDeviceFile.h"
FOutputDeviceFile Log;

// Error handler.
#include "FOutputDeviceWindowsError.h"
FOutputDeviceWindowsError Error;

// Feedback.
#include "FFeedbackContextWindows.h"
FFeedbackContextWindows Warn;

// File manager.
#include "FFileManagerWindows.h"
FFileManagerWindows FileManager;

// Config.
#include "FConfigCacheIni.h"

//jg -- Thread synchronization factory
#include "UnThreadingWindows.h"
FSynchronizeFactoryWin SynchFactory;
FThreadFactoryWin ThreadFactory;

/*-----------------------------------------------------------------------------
	WinMain.
-----------------------------------------------------------------------------*/

//
// Main entry point.
// This is an example of how to initialize and launch the engine.
//
INT WINAPI WinMain( HINSTANCE hInInstance, HINSTANCE hPrevInstance, char*, INT nCmdShow )
{
	// Remember instance.
	INT ErrorLevel = 0;
	GIsStarted     = 1;
	hInstance      = hInInstance;
	const TCHAR* CmdLine = GetCommandLine();
	if( !Parse( CmdLine, TEXT("INI="), GPackage, ARRAY_COUNT(GPackage) ) )
		appSprintf( GPackage, TEXT("Avaris") );

	// Begin guarded code.
#ifndef _DEBUG
	try
	{
#endif
		// Init core.
		GIsClient = GIsGuarded = 1;

		//jg -- Set the pointer to the synchronization factory
		GSynchronizeFactory = &SynchFactory;
		GThreadFactory = &ThreadFactory;

		appInit( GPackage, CmdLine, &Malloc, &Log, &Error, &Warn, &FileManager, FConfigCacheIni::Factory, 1 );

		// Init mode.
		GIsServer     = 1;
		GIsClient     = !ParseParam(appCmdLine(),TEXT("SERVER"));
		GIsEditor     = 0;
		GIsScriptable = 1;
		GLazyLoad     = !GIsClient || ParseParam(appCmdLine(),TEXT("LAZY"));

		// Init windowing.
		InitWindowing();

		UDebuggerCore* DebuggerCore = NEW UDebuggerCore();
		GDebugger = DebuggerCore;
		DebuggerCore->Initialize();

		UEngine* Engine = InitEngine();

		if ( !ParseParam(CmdLine, TEXT("EXTERNAL")) )
            DebuggerCore->LoadEditPackages();

		if( Engine )
		{
			// Hide splash screen.
			// hack to get this to work
			GIsEditor = TRUE;
			ExitSplash();
			GIsEditor = FALSE;

			UViewport* InitialViewport = NULL;
			if ( Engine->Client && Engine->Client->Viewports.Num() && Engine->Client->Viewports(0) )
				InitialViewport = Engine->Client->Viewports(0);

			// Optionally Exec an exec file
			FString Temp;
			if( Parse(CmdLine, TEXT("EXEC="), Temp) )
			{
				Temp = FString(TEXT("exec ")) + Temp;
				if( InitialViewport )
					InitialViewport->Exec( *Temp, *GLogWindow );
			}

			if ( InitialViewport )
				InitialViewport->Exec(TEXT("LOADWINDOWPOSITION"));

			// Start main engine loop, including the Windows message pump.
			if( !GIsRequestingExit )
				MainLoop( Engine );

		}
		// Clean shutdown.
		if ( GDebugger )
		{
			DebuggerCore->Close();
			delete DebuggerCore;
		}

		DebuggerCore = NULL;
		GDebugger = NULL;

		GFileManager->Delete(TEXT("Running.ini"),0,0);

		GIsGuarded = 0;
		
		appPreExit();
#ifndef _DEBUG
	}
	catch( ... )
	{
		// Crashed.
		ErrorLevel = 1;
		Error.HandleError();
	}
#endif

	// Final shut down.
	appExit();
	GIsStarted = 0;
	return ErrorLevel;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

