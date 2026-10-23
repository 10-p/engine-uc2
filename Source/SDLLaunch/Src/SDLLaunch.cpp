/*=============================================================================
	SDLLaunch.cpp: the portable UC2 game launcher (uc2.bin).
	Copyright 2002 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Ryan C. Gordon, based on ut2003's win32 Launch.cpp.
	* Rewritten for SDL2 and UC2's static link: what UC2 does at start-up comes from
	Source/Launch/Src/Launch.cpp + Engine/Inc/UnEngineWin.h (the PC WinMain), the static
	registration from Source/XboxLaunch/Src/Launch.cpp, as Source/UCC/Src/UCC.cpp does it.
	The browser's main loop is in SDLLaunchWeb.cpp.
=============================================================================*/

#include "SDLLaunchPrivate.h"
#include "SDLLaunchWeb.h"	// empty off Emscripten

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
#if UC2_WITH_UC2GL4ESDRV	// The GL4ES binding of UC2GLDrv (GLES2 natively, WebGL on the web)
#include "UC2GL4ESDrv.h"
#endif
#if UC2_WITH_UC2ALAUDIO
#include "UC2ALAudio.h"
#endif
#if UC2_WITH_UC2MOVIE
#include "UC2Movie.h"
#endif
#if UC2_WITH_WEBRTCDRV	// The browser's network driver (web build only)
#include "WebRTCDrv.h"
#endif
#endif
#include "UnMovie.h"	// AppPlayMovieFullscreen (Engine)

/*-----------------------------------------------------------------------------
	Global variables.
-----------------------------------------------------------------------------*/

// General. The module name; main sets it again from appPackage(), as the PC WinMain does.
extern "C" {TCHAR GPackage[64]=TEXT("Avaris");}

// Memory allocator.
#include "FMallocAnsi.h"
FMallocAnsi Malloc;

// Log file.
#include "FOutputDeviceFile.h"
FOutputDeviceFile Log;

#if defined(__ANDROID__)
#include <android/log.h>
#include <pthread.h>
#include <unistd.h>

//
// The log on logcat (tag UC2), one entry per line, and in Avaris.log: appInit is given this device, which
// hands every line on to the file device.
//
class FOutputDeviceAndroidLog : public FOutputDevice
{
public:
	FOutputDevice* AuxOut;
	FOutputDeviceAndroidLog( FOutputDevice* InAuxOut ) : AuxOut( InAuxOut ) {}
	void Serialize( const TCHAR* Data, enum EName Event )
	{
		if( Event != NAME_Title && !FName::SafeSuppressed(Event) )
			__android_log_print( ANDROID_LOG_INFO, "UC2", "%s: %s", appToAnsi(FName::SafeString(Event)), appToAnsi(Data) );
		AuxOut->Serialize( Data, Event );
	}
	void Flush()
	{
		AuxOut->Flush();
	}
};
FOutputDeviceAndroidLog AndroidLog( &Log );

//
// stdout and stderr (printf, the error device, the libraries' own messages) to logcat: an app's stdio goes
// nowhere, so a pipe takes it and a thread writes each line as an entry with the tag UC2-stdio.
//
static int GStdioPipe[2] = { -1, -1 };
static void* StdioToLogcat( void* )
{
	char Line[1024];
	size_t Len = 0;
	char Buf[512];
	ssize_t N;
	while( (N = read( GStdioPipe[0], Buf, sizeof(Buf) )) > 0 )
	{
		for( ssize_t i=0; i<N; i++ )
		{
			if( Buf[i] == '\n' || Len == sizeof(Line)-1 )
			{
				Line[Len] = 0;
				if( Len )
					__android_log_write( ANDROID_LOG_INFO, "UC2-stdio", Line );
				Len = 0;
				if( Buf[i] == '\n' )
					continue;
			}
			if( Buf[i] != '\r' )
				Line[Len++] = Buf[i];
		}
	}
	return NULL;
}
static void RedirectStdioToLogcat()
{
	if( pipe( GStdioPipe ) != 0 )
		return;
	dup2( GStdioPipe[1], STDOUT_FILENO );
	dup2( GStdioPipe[1], STDERR_FILENO );
	pthread_t Thread;
	if( pthread_create( &Thread, NULL, StdioToLogcat, NULL ) == 0 )
		pthread_detach( Thread );
}
#endif

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
	UBOOL LimiterSlept = 0;	// the web yields only when the limiter did not sleep
	guard(EnforceTickRate);
	if( !GIsBenchmarking && IncludeMessaging )
	{
		FLOAT MaxTickRate = Engine->GetMaxTickRate();
		// A frame-rate cap for a game that has no cap of its own (a standalone game or a listen server:
		// GetMaxTickRate returns 0 there): [Engine.Engine] FrameRateCap=<fps>, the key the frontend's launcher
		// writes (-ini:System:[Engine.Engine]:FrameRateCap=<n>). Absent or <= 0: uncapped. Tests set it with -ini:
		// for frame-rate-dependent behaviour (Epic's integer view rotation per
		// tick, the lazy camera) without a GPU, where the null renderer otherwise runs at a thousand frames a second.
		static FLOAT MaxFps = -1.f;
		if( MaxFps < 0.f )
		{
			MaxFps = 0.f;
			GConfig->GetFloat( TEXT("Engine.Engine"), TEXT("FrameRateCap"), MaxFps, GIni );
			MaxFps = Max( 0.f, MaxFps );
			debugf( NAME_Init, TEXT("ufront: client frame rate cap %.0f fps%s"), MaxFps, MaxFps > 0.f ? TEXT("") : TEXT(" (uncapped)") );
		}
		if( MaxFps > 0.f && (MaxTickRate <= 0.f || MaxFps < MaxTickRate) )
			MaxTickRate = MaxFps;
		// [SDLLaunch] FramePattern=<ms>:<ms>:... gives the frames
		// these lengths in turn (a game without a cap of its own only; it takes the place of FrameRateCap): uneven
		// frame pacing, as a browser has it, made repeatable for the frame trace (FRAMETRACE, UC2FrameTrace.cpp).
		// A test key, set with -ini:System:[SDLLaunch]:FramePattern=16.7:33.3:..., read once
		// and never kept in the ini (appReadTestKey).
		static INT   PatternNum = -1;
		static FLOAT Pattern[32];
		static INT   PatternIndex = 0;
		if( PatternNum < 0 )
		{
			PatternNum = 0;
			FString Spec;
			if( appReadTestKey( TEXT("FramePattern"), Spec, TEXT("SDLLaunch") ) && Spec.Len() )
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
#ifdef __EMSCRIPTEN__
			UC2WebWaitCapped( 1.0/MaxTickRate );
#else
			FLOAT Delta = (1.0/MaxTickRate) - (appSeconds()-OldTime);
			appSleep( Max(0.f,Delta) );
#endif
			LimiterSlept = 1;
		}
	}
	unguard;

#ifdef __EMSCRIPTEN__
	// Give the engine worker's event loop one turn per frame (input, host commands), until the display's next
	// frame; the limiter above already did when it waited, so exactly one yield per frame. The test is whether
	// the limiter waited, not the engine's own tick rate (the launcher's frame cap waits with an engine rate of
	// 0). SDLLaunchWeb.cpp has the waits.
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
// -*RENDERER switches: the first switch present whose device is in this build and runs on this platform
// sets [Engine.Engine] RenderDevice for this run. The web build links UC2GLDrv.UC2GLRenderDevice as the
// GL4ES device's parent class, but its desktop binding cannot make a context in a browser
// (UC2GL_HAS_DESKTOP_BINDING is 0), so a switch also says whether its device runs here; one that does not
// is logged and ignored, and the ini's device (GL4ES on the web) boots.
//
#if UC2_WITH_UC2GLDRV && !UC2GL_HAS_DESKTOP_BINDING
	#define UC2_DESKTOP_GL_RUNS 0
#else
	#define UC2_DESKTOP_GL_RUNS 1
#endif
struct FRenderSwitch { const TCHAR* Param; const TCHAR* Device; UBOOL RunsHere; };
static const FRenderSwitch GRenderSwitches[] =
{
	{ TEXT("NULLRENDERER"), TEXT("Engine.NullRenderDevice"),			1 },
	{ TEXT("OPENGL"),       TEXT("UC2GLDrv.UC2GLRenderDevice"),			UC2_DESKTOP_GL_RUNS },
	{ TEXT("GL4ES"),        TEXT("UC2GL4ESDrv.UC2GL4ESRenderDevice"),	1 },	// The same driver on OpenGL ES through GL4ES
};

static void SelectRenderDevice()
{
	for( INT i=0; i<ARRAY_COUNT(GRenderSwitches); i++ )
	{
		const FRenderSwitch& Sw = GRenderSwitches[i];
		if( ParseParam( appCmdLine(), Sw.Param ) )
		{
			if( !IsClassAvailable( Sw.Device ) )
				debugf( NAME_Warning, TEXT("-%s: render device %s is not in this build, ignored"), Sw.Param, Sw.Device );
			else if( !Sw.RunsHere )
				debugf( NAME_Warning, TEXT("-%s: render device %s cannot run on this platform, ignored: keeping RenderDevice=%s"), Sw.Param, Sw.Device, GConfig->GetStr( TEXT("Engine.Engine"), TEXT("RenderDevice") ) );
			else
			{
				debugf( NAME_Init, TEXT("-%s selects render device %s"), Sw.Param, Sw.Device );
				GConfig->SetString( TEXT("Engine.Engine"), TEXT("RenderDevice"), Sw.Device );
			}
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
#if defined(__ANDROID__) && UC2_WITH_UC2GL4ESDRV
	// Android has OpenGL ES only: the desktop device (linked as the GL4ES device's parent class) cannot make a
	// context, so an ini from a desktop build, or one naming a device this build lacks, gets the GL4ES device.
	const TCHAR* RenderReplacement = TEXT("UC2GL4ESDrv.UC2GL4ESRenderDevice");
	FString IniDevice = GConfig->GetStr( TEXT("Engine.Engine"), TEXT("RenderDevice") );
	if( appStricmp( *IniDevice, TEXT("UC2GLDrv.UC2GLRenderDevice") )==0 )
	{
		debugf( NAME_Init, TEXT("RenderDevice=%s cannot run on Android (no desktop OpenGL): using %s"), *IniDevice, RenderReplacement );
		GConfig->SetString( TEXT("Engine.Engine"), TEXT("RenderDevice"), RenderReplacement );
	}
#else
	const TCHAR* RenderReplacement = TEXT("Engine.NullRenderDevice");
#endif
	struct { const TCHAR* Key; const TCHAR* Replacement; } Drivers[] =
	{
		{ TEXT("ViewportManager"),	TEXT("SDL2Drv.SDL2Client")		},
		{ TEXT("RenderDevice"),		RenderReplacement				},
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
	// A browser has no sockets. The web Default.ini names the WebRTC driver, but an Avaris.ini derived
	// by an older web build (the frontend keeps it) may still name the TCP one: switch it; the frontend leaves
	// P2P to the engine (its netModeSetting.ts seeds nothing for it).
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

#if defined(__ANDROID__)
static int UC2Main( int argc, char** argv )
#else
int main( int argc, char** argv )
#endif
{
	setbuf( stdout, NULL );
	setbuf( stderr, NULL );
	appArgv0( argv[0] );

	// Command line: everything after the program name (appInit keeps the first token off Windows too).
	// 4096 characters, the size of Core's GCmdLine: settings go in as -ini: tokens, and a test run's
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
#if defined(__ANDROID__)
		appInit( GPackage, CmdLine, &Malloc, &AndroidLog, &Error, &Warn, &FileManager, FConfigCacheIni::Factory, 1 );
#else
		appInit( GPackage, CmdLine, &Malloc, &Log, &Error, &Warn, &FileManager, FConfigCacheIni::Factory, 1 );
#endif
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
#if UC2_WITH_UC2GL4ESDRV
		AUTO_INITIALIZE_REGISTRANTS_UC2GL4ESDRV;
#endif
#if UC2_WITH_UC2ALAUDIO
		AUTO_INITIALIZE_REGISTRANTS_UC2ALAUDIO;
#endif
#if UC2_WITH_UC2MOVIE
		AUTO_INITIALIZE_REGISTRANTS_UC2MOVIE;
#endif
#if UC2_WITH_WEBRTCDRV
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
			UC2WebSetEngine( Engine );	// Ue_exec_command
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

			// The start-up logos, as the Xbox launcher played Movies/UC2_Logos.xmv (Launch.cpp:402,
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

#if defined(__ANDROID__)
//
// Android: SDLActivity loads libUC2.so and calls SDL_main on its SDL thread, whose stack is Android's default
// 1 MB. The engine runs on a thread of its own with the 8 MB a Linux main thread has (the script VM and the
// level code recurse deeply); SDL_main waits for it. SDL's Android calls attach whichever thread makes them.
//
struct FUC2MainArgs { int argc; char** argv; int Result; };
static void* UC2MainThread( void* Arg )
{
	FUC2MainArgs* Args = (FUC2MainArgs*)Arg;
	Args->Result = UC2Main( Args->argc, Args->argv );
	return NULL;
}
extern "C" __attribute__((visibility("default"))) int SDL_main( int argc, char** argv )
{
	RedirectStdioToLogcat();
	FUC2MainArgs Args = { argc, argv, 1 };
	pthread_attr_t Attr;
	pthread_attr_init( &Attr );
	pthread_attr_setstacksize( &Attr, 8 * 1024 * 1024 );
	pthread_t Thread;
	if( pthread_create( &Thread, &Attr, UC2MainThread, &Args ) != 0 )
	{
		__android_log_print( ANDROID_LOG_ERROR, "UC2", "could not start the engine thread: running it on the SDL thread" );
		Args.Result = UC2Main( argc, argv );
	}
	else
		pthread_join( Thread, NULL );
	pthread_attr_destroy( &Attr );
	return Args.Result;
}
#endif

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
