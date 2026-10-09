/*=============================================================================
	SDLLaunch.cpp: the portable UC2 game launcher (uc2.bin).
	Copyright 2002 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Ryan C. Gordon, based on ut2003's win32 Launch.cpp.
	* engine-uc2 (M4): rewritten. Structure from engine-ut2004's SDL2 launcher
	  (Source/SDLLaunch/Src/SDLLaunch.cpp there); what UC2 itself does at startup from
	  Source/Launch/Src/Launch.cpp + Engine/Inc/UnEngineWin.h (the PC WinMain) and the static
	  registration from Source/XboxLaunch/Src/Launch.cpp, as Source/UCC/Src/UCC.cpp does it.
	  Epic's SDL 1.2 version of this file is in git history (7633696).

	Not here yet, by milestone: movies (M9), pads (M10), the Emscripten main loop (M4w).
	The OpenGL render device (UC2GLDrv) is in since M5, the OpenAL audio device (UC2ALAudio)
	since M8.
=============================================================================*/

#include "SDLLaunchPrivate.h"
#include "SDLLaunchWeb.h"	// M4w: empty off Emscripten

#if __STATIC_LINK
// Every package linked into uc2.bin, as XboxLaunch/Src/Launch.cpp does for the static XBE.
#include "ULoadSaveManager.h"
#include "UnNullRenderDevice.h"
#include "UnNet.h"
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
#define SDL2DRV_REGISTRATION_ONLY
#include "SDL2Drv.h"
#if UC2_WITH_UC2GLDRV
#include "UC2GLDrv.h"
#endif
#if UC2_WITH_UC2GL4ESDRV	// M11: the GL4ES binding of UC2GLDrv (GLES2 natively, WebGL on the web)
#include "UC2GL4ESDrv.h"
#endif
#if UC2_WITH_UC2ALAUDIO	// M8
#include "UC2ALAudio.h"
#endif
#if UC2_WITH_UC2MOVIE	// M9
#include "UC2Movie.h"
#endif
#if UC2_WITH_WEBRTCDRV	// E-2: the browser's network driver (web build only)
#include "WebRTCDrv.h"
#endif
#endif
#include "UnMovie.h"	// M9: appPlayMovieFullscreen (Engine)

/*-----------------------------------------------------------------------------
	Global variables.
-----------------------------------------------------------------------------*/

// General. Set to appPackage() (the module name, "Avaris") in main, as the PC WinMain does.
extern "C" {TCHAR GPackage[64]=TEXT("SDLLaunch");}

// Memory allocator.
#include "FMallocAnsi.h"
FMallocAnsi Malloc;

// Log file.
#include "FOutputDeviceFile.h"
FOutputDeviceFile Log;

// Error handler.
#include "FOutputDeviceAnsiError.h"
FOutputDeviceAnsiError Error;

// Feedback.
#include "FFeedbackContextAnsi.h"
FFeedbackContextAnsi Warn;

// File manager.
#include "FFileManagerUnix.h"
FFileManagerUnix FileManager;

// Config.
#include "FConfigCacheIni.h"

// Thread synchronization factories (must be set before appInit).
#include "UnThreadingPosix.h"
FSynchronizeFactoryPosix SynchFactory;
FThreadFactoryPosix ThreadFactory;

//
// True iff the class is compiled into this build (its UClass registered at startup). Looks only at
// registered objects: no package load, no throw.
//
static UBOOL IsClassAvailable( const TCHAR* Name )
{
	FString Path = Name;
	INT Dot = Path.InStr( TEXT(".") );
	const TCHAR* ClassName = ( Dot != INDEX_NONE ) ? ((*Path) + Dot + 1) : (*Path);
	return UObject::StaticFindObject( UClass::StaticClass(), ANY_PACKAGE, ClassName ) != NULL;
}

/*-----------------------------------------------------------------------------
	Splash screen (none yet: the PC build showed ..\Help\Logo.bmp, which UC2 does not ship).
-----------------------------------------------------------------------------*/

static void InitSplash( const TCHAR* Filename ) {}
static void ExitSplash() {}

/*-----------------------------------------------------------------------------
	Main loop (UnEngineWin.h's CMainLoop; the Windows message pump is the viewport's
	UpdateInput, which pumps the SDL event queue).
-----------------------------------------------------------------------------*/

class CMainLoop
{
public:
	CMainLoop( UEngine* InEngine, UBOOL InIncludeMessaging = true );
	~CMainLoop();

	void RunLoop();
	bool Finished() const { return !GIsRunning || GIsRequestingExit; }

private:
	UEngine*	Engine;
	UBOOL		IncludeMessaging;
	DOUBLE		OldTime;
	DOUBLE		SecondStartTime;
	INT			TickCount;
	DWORD		LastFrameCycles;

	// Benchmarking (-BENCHMARK -SECONDS=n): fixed 30 Hz steps, average logged at the end.
	INT			BMFrames;
	INT			BMDiscardedFrames;
	INT			BMMaxFrames;
	FLOAT		BMSeconds;

	// Movie recording.
	UBOOL		RecordingMovie;
};

CMainLoop::CMainLoop( UEngine* InEngine, UBOOL InIncludeMessaging )
:	Engine(InEngine), IncludeMessaging(InIncludeMessaging)
{
	guard(MainLoopCtor);
	check(Engine);

	GIsRunning			= 1;
	OldTime				= appSeconds();
	SecondStartTime		= OldTime;
	TickCount			= 0;

	BMFrames			= 0;
	BMDiscardedFrames	= 10;
	BMSeconds			= 0.0f;
	BMMaxFrames			= 0;
	Parse( appCmdLine(), TEXT("SECONDS="), BMMaxFrames );
	BMMaxFrames			*= 30;

	LastFrameCycles		= appCycles();

	// Seed random number generator with constant seed for benchmarking.
	if( GIsBenchmarking )
		appRandInit( 0 );

	RecordingMovie		= ParseParam( appCmdLine(), TEXT("RECORDMOVIE") );
	unguard;
}

void CMainLoop::RunLoop()
{
	if( Finished() )
		return;

#ifndef FINAL_RELEASE
	// Clear stats (will also update old stats).
	GStats.Clear();
#endif

	if( GIsBenchmarking )
	{
		if( BMFrames == BMDiscardedFrames )
			BMSeconds = 0.0f;
		else if( BMMaxFrames && BMFrames > BMMaxFrames )
			appRequestExit( 0 );
	}

	// Update the world.
	DOUBLE NewTime   = appSeconds();
	FLOAT  DeltaTime = NewTime - OldTime;

	guard(UpdateWorld);
	if( GIsBenchmarking || RecordingMovie )
	{
		Engine->Tick( 1.0f / 30.0f );
		if( GWindowManager )
			GWindowManager->Tick( 1.0f / 30.0f );
		if( BMFrames < BMMaxFrames )
			BMSeconds += DeltaTime;
		BMFrames++;
	}
	else
	{
		Engine->Tick( DeltaTime );
		if( GWindowManager )
			GWindowManager->Tick( DeltaTime );
	}
	TickCount++;
	OldTime = NewTime;
	if( OldTime > SecondStartTime + 1 )
	{
		Engine->CurrentTickRate = (FLOAT)TickCount / (OldTime - SecondStartTime);
		SecondStartTime = OldTime;
		TickCount = 0;
	}
	unguard;

	// Enforce optional maximum tick rate.
	UBOOL LimiterSlept = 0;	// F-4b: as engine-ut2004, the web yields only when the limiter did not sleep
	guard(EnforceTickRate);
	if( !GIsBenchmarking && IncludeMessaging )
	{
		FLOAT MaxTickRate = Engine->GetMaxTickRate();
		// A frame-rate cap for a game that has no cap of its own (a standalone game or a listen server:
		// GetMaxTickRate returns 0 there): [Engine.Engine] FrameRateCap=<fps>, the key engine-ut99 and
		// engine-unreal1 read (the frontend's launcher writes it with -ini:System:[Engine.Engine]:FrameRateCap=<n>).
		// Absent or <= 0: uncapped, as engine-ut2004 (which has no cap). F-4e: it replaces D-6's -maxfps=<n>
		// switch; tests set it with -ini: for frame-rate-dependent behaviour (Epic's integer view rotation per
		// tick, the lazy camera) without a GPU, where the null renderer otherwise runs at a thousand frames a second.
		static FLOAT MaxFps = -1.f;
		if( MaxFps < 0.f )
		{
			MaxFps = 0.f;
			GConfig->GetFloat( TEXT("Engine.Engine"), TEXT("FrameRateCap"), MaxFps, GIni );
			MaxFps = Max( 0.f, MaxFps );
			if( MaxFps > 0.f )
				debugf( NAME_Init, TEXT("FrameRateCap=%g: a game without a tick rate of its own is capped at %g fps"), MaxFps, MaxFps );
		}
		if( MaxFps > 0.f && (MaxTickRate <= 0.f || MaxFps < MaxTickRate) )
			MaxTickRate = MaxFps;
		// jitter (port, docs/porting/analysis/jitter.md): [SDLLaunch] FramePattern=<ms>:<ms>:... gives the frames
		// these lengths in turn (a game without a cap of its own only; it takes the place of FrameRateCap): uneven
		// frame pacing, as a browser has it, made repeatable for the frame trace (FRAMETRACE, UC2FrameTrace.cpp).
		// A test hook, set with -ini:System:[SDLLaunch]:FramePattern=16.7:33.3:... (F-4e: was -framepattern=).
		static INT   PatternNum = -1;
		static FLOAT Pattern[32];
		static INT   PatternIndex = 0;
		if( PatternNum < 0 )
		{
			PatternNum = 0;
			FString Spec;
			if( GConfig->GetString( TEXT("SDLLaunch"), TEXT("FramePattern"), Spec, GIni ) && Spec.Len() )
			{
				const TCHAR* S = *Spec;
				while( *S && PatternNum < ARRAY_COUNT(Pattern) )
				{
					FLOAT Ms = appAtof( S );
					if( Ms > 0.f )
						Pattern[PatternNum++] = Ms / 1000.f;
					while( *S && *S != ':' )
						S++;
					if( *S == ':' )
						S++;
				}
				debugf( NAME_Init, TEXT("FramePattern: %i frame lengths"), PatternNum );
			}
		}
		if( PatternNum > 0 && Engine->GetMaxTickRate() <= 0.f )
		{
			FLOAT Delta = Pattern[PatternIndex++ % PatternNum] - (appSeconds()-OldTime);
			appSleep( Max(0.f,Delta) );
			LimiterSlept = 1;
		}
		else if( MaxTickRate>0.0 )
		{
			FLOAT Delta = (1.0/MaxTickRate) - (appSeconds()-OldTime);
			appSleep( Max(0.f,Delta) );
			LimiterSlept = 1;
		}
	}
	unguard;

#ifdef __EMSCRIPTEN__
	// M4w: give the engine worker's event loop one turn per frame (input, host commands, the frame's present);
	// the limiter above already did when it slept (appSleep is emscripten_sleep, the same kind of yield), so
	// exactly one yield per frame, as engine-ut2004's WorkerYield. F-4b: the test is whether the limiter slept,
	// not the engine's own tick rate (the launcher's frame cap sleeps with an engine rate of 0, and yielded
	// twice per frame). SDLLaunchWeb.cpp says why.
	if( !LimiterSlept )
		UC2WebYieldFrame();
#else
	(void)LimiterSlept;
#endif

	// Handle all incoming messages.
	if( IncludeMessaging && Engine->Client && Engine->Client->Viewports.Num() )
	{
		guard(MessagePump);
		Engine->Client->Viewports(0)->UpdateInput( 0, 0 );
		unguard;
	}

	if( RecordingMovie && Engine->Client && Engine->Client->Viewports.Num() )
		Engine->Client->Viewports(0)->Exec( TEXT("shot") );

#ifndef FINAL_RELEASE
	GStats.DWORDStats( GEngineStats.STATS_Frame_TotalCycles ) = appCycles() - LastFrameCycles;
	GStats.DWORDStats( GEngineStats.STATS_Game_ScriptCycles ) = GScriptCycles;
	LastFrameCycles = appCycles();
#endif
	GScriptCycles = 0;
}

CMainLoop::~CMainLoop()
{
	guard(CMainLoopDtor);
	GIsRunning = 0;
	if( GIsBenchmarking && BMSeconds > 0.f )
	{
		// UnEngineWin.h writes CSVs under ..\Benchmark; the average is what matters here.
		INT Frames = BMMaxFrames - BMDiscardedFrames;
		debugf( TEXT("Benchmark: %i frames in %f seconds, average %f fps"), Frames, BMSeconds, Frames / BMSeconds );
	}
	GExec = NULL;
	unguard;
}

static void MainLoop( UEngine* Engine )
{
	guard(MainLoop);
	check(Engine);
	CMainLoop TheLoop( Engine );
	while( !TheLoop.Finished() )
		TheLoop.RunLoop();
	unguard;
}

/*-----------------------------------------------------------------------------
	Driver selection.
-----------------------------------------------------------------------------*/

//
// -*RENDERER switches (engine-ut2004's SelectRenderDevice, cut down): the first switch present whose
// device is in this build sets [Engine.Engine] RenderDevice for this run. The GL devices join at M5.
//
struct FRenderSwitch { const TCHAR* Param; const TCHAR* Device; };
static const FRenderSwitch GRenderSwitches[] =
{
	{ TEXT("NULLRENDERER"), TEXT("Engine.NullRenderDevice") },
	{ TEXT("OPENGL"),       TEXT("UC2GLDrv.UC2GLRenderDevice") },	// M5
	{ TEXT("GL4ES"),        TEXT("UC2GL4ESDrv.UC2GL4ESRenderDevice") },	// M11: the same driver on GLES2 through GL4ES
};

static void SelectRenderDevice()
{
	for( INT i=0; i<ARRAY_COUNT(GRenderSwitches); i++ )
	{
		const FRenderSwitch& Sw = GRenderSwitches[i];
		if( ParseParam( appCmdLine(), Sw.Param ) )
		{
			if( IsClassAvailable( Sw.Device ) )
			{
				debugf( NAME_Init, TEXT("-%s selects render device %s"), Sw.Param, Sw.Device );
				GConfig->SetString( TEXT("Engine.Engine"), TEXT("RenderDevice"), Sw.Device );
			}
			else
				debugf( NAME_Warning, TEXT("-%s: render device %s is not in this build, ignored"), Sw.Param, Sw.Device );
			return;
		}
	}
}

//
// An ini written for the Windows or Xbox build names drivers this build does not have
// (WinDrv.WindowsClient, D3DDrv.D3DRenderDevice, XboxDrv.XboxClient): use ours instead, and say so.
// A driver the ini names and this build has is left alone.
//
static void ReplaceMissingDrivers()
{
	struct { const TCHAR* Key; const TCHAR* Replacement; } Drivers[] =
	{
		{ TEXT("ViewportManager"),	TEXT("SDL2Drv.SDL2Client")		},
		{ TEXT("RenderDevice"),		TEXT("Engine.NullRenderDevice")	},
		{ TEXT("AudioDevice"),		TEXT("Engine.NullAudioDevice")	},
	};
	for( INT i=0; i<ARRAY_COUNT(Drivers); i++ )
	{
		FString Current = GConfig->GetStr( TEXT("Engine.Engine"), Drivers[i].Key );
		if( Current.Len() && !IsClassAvailable(*Current) )
		{
			debugf( NAME_Init, TEXT("%s=%s is not in this build: using %s"), Drivers[i].Key, *Current, Drivers[i].Replacement );
			GConfig->SetString( TEXT("Engine.Engine"), Drivers[i].Key, Drivers[i].Replacement );
		}
	}
#if UC2_WITH_WEBRTCDRV
	// E-2: a browser has no sockets. The web Default.ini names the WebRTC driver, but an Avaris.ini derived
	// by an older web build (the frontend keeps it) still names the TCP one: switch it, as engine-ut2004's
	// launcher does; the frontend leaves P2P to the engine (its netModeSetting.ts seeds nothing for it).
	FString NetDevice = GConfig->GetStr( TEXT("Engine.Engine"), TEXT("NetworkDevice") );
	if( appStricmp( *NetDevice, TEXT("IpDrv.TcpNetDriver") )==0 )
	{
		debugf( NAME_Init, TEXT("NetworkDevice=%s cannot work in a browser: using WebRTCDrv.WebRTCNetDriver"), *NetDevice );
		GConfig->SetString( TEXT("Engine.Engine"), TEXT("NetworkDevice"), TEXT("WebRTCDrv.WebRTCNetDriver") );
	}
#endif
}

/*-----------------------------------------------------------------------------
	Startup.
-----------------------------------------------------------------------------*/

//
// UnEngineWin.h's InitEngine without the Windows-only parts (config wizard, installer mutex,
// exec hook with property windows).
//
static UEngine* InitEngine()
{
	guard(InitEngine);
	DOUBLE LoadTime = appSeconds();

	// Set exec hook.
	GExec = NULL;

	// First-run.
	INT FirstRun=0;
	GConfig->GetInt( TEXT("FirstRun"), TEXT("FirstRun"), FirstRun );
	if( ParseParam(appCmdLine(),TEXT("FirstRun")) )
		FirstRun=0;

	ReplaceMissingDrivers();

	// EXEC from command-line.
	FString Command;
	if( Parse(appCmdLine(),TEXT("consolecommand="), Command) )
	{
		debugf( TEXT("Can't use consolecommand: no exec hook in this launcher") );
		return NULL;
	}

	// Test render device.
	FString Device;
	if( Parse(appCmdLine(),TEXT("testrendev="),Device) )
	{
		debugf( TEXT("Detecting %s"), *Device );
		UClass* Cls = IsClassAvailable(*Device) ? LoadClass<URenderDevice>( NULL, *Device, NULL, 0, NULL ) : NULL;
		GConfig->SetInt( *Device, TEXT("DescFlags"), RDDESCF_Incompatible );
		GConfig->Flush( 0 );
		if( Cls )
		{
			URenderDevice* RenDev = ConstructObject<URenderDevice>( Cls );
			if( RenDev && RenDev->Init() )
				debugf( TEXT("Successfully detected %s"), *Device );
			else
				delete RenDev;
		}
		FArchive* Ar = GFileManager->CreateFileWriter( TEXT("Detected.ini"), 0 );
		if( Ar )
			delete Ar;
		return NULL;
	}

	// Create is-running semaphore file.
	FArchive* Ar = GFileManager->CreateFileWriter( TEXT("Running.ini"), 0 );
	if( Ar )
		delete Ar;

	// Update first-run.
	if( FirstRun<ENGINE_VERSION )
		FirstRun = ENGINE_VERSION;
	GConfig->SetInt( TEXT("FirstRun"), TEXT("FirstRun"), FirstRun );

	// Create the global engine object.
	UClass* EngineClass = UObject::StaticLoadClass( UGameEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.GameEngine"), NULL, LOAD_NoFail, NULL );
	UEngine* Engine = ConstructObject<UEngine>( EngineClass );
	UEngine::GEngine = Engine;
	Engine->Init();
	debugf( TEXT("Startup time: %f seconds"), appSeconds()-LoadTime );

	return Engine;
	unguard;
}

//
// Command-line resolution switches (-640x480 ...), written to the active client's section as the
// PC WinMain does for [WinDrv.WindowsClient].
//
static void OverrideResolution()
{
	static const TCHAR* Modes[][3] =
	{
		{ TEXT("320x240"),   TEXT("320"),  TEXT("240")  },
		{ TEXT("512x384"),   TEXT("512"),  TEXT("384")  },
		{ TEXT("640x480"),   TEXT("640"),  TEXT("480")  },
		{ TEXT("800x600"),   TEXT("800"),  TEXT("600")  },
		{ TEXT("1024x768"),  TEXT("1024"), TEXT("768")  },
		{ TEXT("1280x960"),  TEXT("1280"), TEXT("960")  },
		{ TEXT("1280x1024"), TEXT("1280"), TEXT("1024") },
		{ TEXT("1600x1200"), TEXT("1600"), TEXT("1200") },
	};
	for( INT i=ARRAY_COUNT(Modes)-1; i>=0; i-- )
	{
		if( ParseParam( appCmdLine(), Modes[i][0] ) )
		{
			FString Client = GConfig->GetStr( TEXT("Engine.Engine"), TEXT("ViewportManager") );
			GConfig->SetString( *Client, TEXT("FullscreenViewportX"), Modes[i][1] );
			GConfig->SetString( *Client, TEXT("FullscreenViewportY"), Modes[i][2] );
			return;
		}
	}
}

/*-----------------------------------------------------------------------------
	main.
-----------------------------------------------------------------------------*/

int main( int argc, char** argv )
{
	setbuf( stdout, NULL );
	setbuf( stderr, NULL );
	appArgv0( argv[0] );

	// Command line: everything after the program name (appInit no longer drops a first token off Windows).
	// 4096 characters, the size of Core's GCmdLine: settings go in as -ini: tokens (F-4 follow-up), and a test run's
	// line passes 1024 (a token that does not fit is dropped whole, with a warning below).
	TCHAR CmdLine[4096]=TEXT("");
	UBOOL CmdLineCut = 0;
	for( INT i=1; i<argc; i++ )
	{
		if( appStrlen(CmdLine) + strlen(argv[i]) + 2 >= ARRAY_COUNT(CmdLine) )
		{
			CmdLineCut = 1;
			break;
		}
		if( i>1 )
			appStrcat( CmdLine, TEXT(" ") );
		appStrcat( CmdLine, ANSI_TO_TCHAR(argv[i]) );
	}

	INT ErrorLevel = 0;
	GIsStarted     = 1;

	// The module name decides the ini (Avaris.ini), the log (Avaris.log) and the product strings
	// (Localization/<lang>/avaris.<lang>), as Avaris.exe did on Windows.
	strcpy( GModule, "Avaris" );
	appStrcpy( GPackage, appPackage() );

#ifndef _DEBUG
	try
	{
#endif
		// Init core.
		GIsClient = GIsGuarded = 1;
		GSynchronizeFactory = &SynchFactory;
		GThreadFactory      = &ThreadFactory;
		appInit( GPackage, CmdLine, &Malloc, &Log, &Error, &Warn, &FileManager, FConfigCacheIni::Factory, 1 );
		if( CmdLineCut )
			debugf( NAME_Warning, TEXT("The command line is longer than %i characters: the arguments after '%s' were dropped"), ARRAY_COUNT(CmdLine), CmdLine + Max(0, appStrlen(CmdLine) - 40) );

		// Static classes: native lookup tables and class registrants of every linked package.
#if __STATIC_LINK
		for( INT k=0; k<ARRAY_COUNT(GNativeLookupFuncs); k++ )
			GNativeLookupFuncs[k] = NULL;
		INT Lookup = 0;
		GNativeLookupFuncs[Lookup++] = &FindCoreUObjectNative;
		GNativeLookupFuncs[Lookup++] = &FindCoreUCommandletNative;
		AUTO_INITIALIZE_REGISTRANTS_ENGINE;
		AUTO_INITIALIZE_REGISTRANTS_SDL2DRV;
#if UC2_WITH_UC2GLDRV
		AUTO_INITIALIZE_REGISTRANTS_UC2GLDRV;
#endif
#if UC2_WITH_UC2GL4ESDRV	// M11
		AUTO_INITIALIZE_REGISTRANTS_UC2GL4ESDRV;
#endif
#if UC2_WITH_UC2ALAUDIO	// M8
		AUTO_INITIALIZE_REGISTRANTS_UC2ALAUDIO;
#endif
#if UC2_WITH_UC2MOVIE	// M9
		AUTO_INITIALIZE_REGISTRANTS_UC2MOVIE;
#endif
#if UC2_WITH_WEBRTCDRV	// E-2
		AUTO_INITIALIZE_REGISTRANTS_WEBRTCDRV;
#endif
		AUTO_INITIALIZE_REGISTRANTS_IPDRV;
		AUTO_INITIALIZE_REGISTRANTS_UWEB;
		// UWeb's hand-written static header registers its classes but not its native lookups.
		GNativeLookupFuncs[Lookup++] = &FindUWebUWebResponseNative;
		GNativeLookupFuncs[Lookup++] = &FindUWebUWebRequestNative;
		AUTO_INITIALIZE_REGISTRANTS_UNREALGAME;
		AUTO_INITIALIZE_REGISTRANTS_UCELEMENTS;
		AUTO_INITIALIZE_REGISTRANTS_UCGAMEPLAY;
		AUTO_INITIALIZE_REGISTRANTS_GUI;
		check( Lookup < ARRAY_COUNT(GNativeLookupFuncs) );
		RegisterNamesGUI();
		RegisterNamesUCElements();
		RegisterNamesUCGameplay();
#endif

		// Init mode.
		GIsServer     = 1;
		GIsClient     = !ParseParam(appCmdLine(),TEXT("SERVER"));
		GIsEditor     = 0;
		GIsScriptable = 1;
		GLazyLoad     = !GIsClient || ParseParam(appCmdLine(),TEXT("LAZY"));

		UBOOL ShowLog = ParseParam(appCmdLine(),TEXT("LOG"));
		if( !ShowLog && !ParseParam(appCmdLine(),TEXT("server")) && !appStrfind(appCmdLine(),TEXT("TestRenDev")) )
			InitSplash( NULL );

		// -LOG: echo the log to the terminal (the PC build opened its log window).
		if( ShowLog )
		{
			Warn.AuxOut = GLog;
			GLog        = &Warn;
		}

		// Benchmarking.
		GIsBenchmarking = ParseParam(appCmdLine(),TEXT("BENCHMARK"));
		if( GIsBenchmarking )
		{
			GConfig->Detach( GIni );
			GConfig->Detach( GUserIni );
		}

		SelectRenderDevice();
		OverrideResolution();

		// Init engine.
		UEngine* Engine = NULL;
		if( !GIsRequestingExit )
			Engine = InitEngine();
		if( Engine )
		{
			ExitSplash();
#ifdef __EMSCRIPTEN__
			UC2WebSetEngine( Engine );	// M4w: ue_exec_command
#endif

			UViewport* InitialViewport = NULL;
			if( Engine->Client && Engine->Client->Viewports.Num() && Engine->Client->Viewports(0) )
				InitialViewport = Engine->Client->Viewports(0);

			// Optionally exec an exec file.
			FString Temp;
			if( Parse(appCmdLine(), TEXT("EXEC="), Temp) && InitialViewport )
			{
				Temp = FString(TEXT("exec ")) + Temp;
				InitialViewport->Exec( *Temp, *GLog );
			}
			if( InitialViewport )
				InitialViewport->Exec( TEXT("LOADWINDOWPOSITION") );

			// M9: the start-up logos, as the Xbox launcher played Movies/UC2_Logos.xmv (Launch.cpp:402,
			// XboxMovieThread.cpp:1049): full screen, skippable by a key or button. Nothing plays with the null
			// renderer, on a dedicated server, or with
			// [UC2Movie] StartupMovies=False (UnMovie.cpp, appCanPlayFullscreenMovie).
			if( !GIsRequestingExit )
				appPlayMovieFullscreen( TEXT("..\\Movies\\UC2_Logos.xmv"), FSMOVIE_Startup, 1 );

			// Start main engine loop.
			if( !GIsRequestingExit )
				MainLoop( Engine );

			if( Engine->GRenDev )
				Engine->GRenDev->Flush( NULL );
#ifdef __EMSCRIPTEN__
			UC2WebSetEngine( NULL );
#endif
		}

		// Clean shutdown.
		GFileManager->Delete( TEXT("Running.ini"), 0, 0 );
		debugf( NAME_Exit, TEXT("Exiting.") );
		appPreExit();
		if( GLog == &Warn )
		{
			GLog        = Warn.AuxOut;
			Warn.AuxOut = NULL;
		}
		GIsGuarded = 0;
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
