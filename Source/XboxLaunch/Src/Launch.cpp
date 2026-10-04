/*=============================================================================
	Launch.cpp: Game launcher.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Tim Sweeney.
=============================================================================*/

#if !defined(FINAL_RELEASE)
	#define DEBUG_KEYBOARD		//!!KEYBOARD HACK
#endif

#include <xtl.h> 
// For dirty disc message
#include <xfont.h>

#include <malloc.h>
#include <stdio.h>
#include <io.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <direct.h>  

#include "Core.h"
#include "Engine.h"
//scion bcl
#include "ULoadSaveManager.h"

#if __STATIC_LINK
#include "UnRender.h"
#include "UnNullRenderDevice.h"
#include "UnNet.h"
#include "XboxDrv.h"
#include "D3DDrv.h"
#include "XBoxAudio.h"
#include "UnrealGame.h"
#include "UCElements.h"
#include "UCGameplay.h"
#include "GUI.h"
// IpDrv static stuff.
#include "UnIpDrv.h"
#include "UnTcpNetDriver.h"
#include "UnTcpNetDriverXbox.h"
#include "UnIpDrvCommandlets.h"
#endif 

/*-----------------------------------------------------------------------------
	Global variables.
-----------------------------------------------------------------------------*/

// Linear loading fun.

#define	TOKEN_LINEAR_LOAD		0
#define TOKEN_LINEAR_SAVE		1

#ifndef FINAL_RELEASE
#define BENCHMARKING			0
//scion jg -- Turn off GPU to see how bad we are CPU bound
#define BENCHMARKING_NOGPU		0
#define USE_STATS				(1 | BENCHMARKING)
#endif
UBOOL	GLinearLoad				= 0;
UBOOL	GLinearSave				= 0;
DWORD	GLinearBytesSerialized	= 0;
extern	TCHAR GCmdLine[];

//scion jg -- Movie recording for the xbox
UBOOL GIsRecordingMovie = 0;
FLOAT GMovieFPS = 30.f;
FLOAT GMovieTimeStep = 1.f / GMovieFPS;
//scion jg -- Hack for movie recording and lip sinc
DOUBLE GLipSincTimeHack = 0.f;

// General.
extern "C" {HINSTANCE hInstance;}
extern "C" {TCHAR GPackage[64]=TEXT("Avaris");}

// Memory allocator.
#ifdef _DEBUG
	#include "FMallocDebug.h"
	FMallocDebug Malloc;
#else
	//jg -- Use the Doug Lea allocator as it is smaller, faster, and fragments less
	#include "FMallocDL.h"
	FMallocDL Malloc;
#endif

#ifdef USE_GCONTIG
#include "FContiguousMalloc.h"
//TEMP -- change to final size as needed
//jg -- Contiguous memory allocator
FContiguousMalloc ContigMalloc(22 * 1024 * 1024);
#endif

#ifndef DEBUG_LOGGING//scion jg -- Null device only
#include "FOutputDeviceNull.h"
// Null log device
FOutputDeviceNull Log;
// Null error device
class FNullError : public FOutputDeviceError
{
public:
	void Serialize( const TCHAR* V, enum EName Event ) {}
	virtual void HandleError(void) {}
} Error;
// Null warn device
class FNullWarn : public FFeedbackContext
{
public:
	void Serialize( const TCHAR* V, enum EName Event ) {}
	virtual UBOOL YesNof( const TCHAR* Fmt, ... ) { return 0; }
	virtual void BeginSlowTask( const TCHAR* Task, UBOOL StatusWindow ) {}
	virtual void EndSlowTask() {}
	virtual UBOOL VARARGS StatusUpdatef( INT Numerator, INT Denominator, const TCHAR* Fmt, ... ) { return 0; }
	virtual void SetContext( FContextSupplier* InSupplier ) {}
} Warn;
#else
// Log file.
#if 1
#include "FOutputDeviceDebug.h"
FOutputDeviceDebug Log;
#else
#include "FOutputDeviceFile.h"
FOutputDeviceFile Log;
#endif

// Error handler.
#include "FOutputDeviceAnsiError.h"
FOutputDeviceAnsiError Error;

// Feedback.
#include "FFeedbackContextAnsi.h"
FFeedbackContextAnsi Warn;
#endif

// File managers.
#include "FFileManagerXbox.h"
#include "FFileManagerLinear.h"
#include "FContentManager.h"
//jg -- Include our file description objects
#include "XboxFileSizes.h"

// Config.
#include "FConfigCacheIni.h"

//jg -- Thread synchronization factory
#include "UnThreadingWindows.h"
FSynchronizeFactoryWin SynchFactory;
FThreadFactoryWin ThreadFactory;

//jg -- Background thread for caching content
#include "FFileCacheManagerXbox.h"
FFileCacheManagerXbox FileCacheMan;

//jg -- Various movie thread support
#include "XboxMovieThread.h"

// Globals for the various devices
IDirectSound8* GDirectSound = NULL;
IDirect3D8* GD3D = NULL;
IDirect3DDevice8* GD3DDevice = NULL;

// scion sz - Keep a global copy of present params.
// Needed for reseting the device when changing FSAA modes
D3DPRESENT_PARAMETERS GPresentParms;

// Hack to not throttle bandwidth on Midway's servers
UBOOL GIsMidwayServer = FALSE;
// Hack to show stat net info for people with a specific profile
UBOOL GShowStatNet = FALSE;

/**
 * Enumerates profiles and checks for Midway's servers and for our special
 * hack profile name
 */
void NetHackCheck(void)
{
	XGAME_FIND_DATA GameFindData;
	// Enumerate the profiles on the U: drive
	HANDLE hFindData = XFindFirstSaveGame("U:\\",&GameFindData);
	if (hFindData != INVALID_HANDLE_VALUE)
	{
		do
		{
			// Copy the profile name
			FString Profile(appFromUnicode(GameFindData.szSaveGameName));
			// Now see if it contains "Midway"
			if (GIsMidwayServer == FALSE && Profile.Locs().InStr(TEXT("midway")) != -1)
			{
				GIsMidwayServer = TRUE;
			}
			// Now check for our special "stat net" hack
			else if (GShowStatNet == FALSE && Profile.InStr(TEXT("jgsspvrpmosz")) != -1)
			{
				GShowStatNet = TRUE;
			}
		}
		while (XFindNextSaveGame(hFindData,&GameFindData) &&
			(GIsMidwayServer == FALSE || GShowStatNet == FALSE));
		XFindClose(hFindData);
	}
}

/**
 * Creates the devices (D3D, DirectSound) used to run the game.
 * Sets the pushbuffer size too
 *
 * @return TRUE if successful, FALSE otherwise
 */
UBOOL CreateDevices(void)
{
	UBOOL bSuccess = FALSE;
	// Create direct sound
	if (SUCCEEDED(DirectSoundCreate(NULL,&GDirectSound,NULL)))
	{
		// Create the D3D subsystem
		GD3D = Direct3DCreate8(D3D_SDK_VERSION);
		if (GD3D != NULL)
		{
			// The push buffer needs to be bigger to support our
			// various BeginPush/EndPush operations
			//TODO jg -- Tune the size and the kick off size
			GD3D->SetPushBufferSize(1024 * 1024,32 * 1024);
			// Setup the presentation parameters
			D3DPRESENT_PARAMETERS PresentParms;
			appMemzero(&PresentParms,sizeof(PresentParms));
			// Handle HDTV if it is there
			if (XGetVideoFlags() & XC_VIDEO_FLAGS_HDTV_480p)
			{
				PresentParms.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER |
					D3DPRESENTFLAG_10X11PIXELASPECTRATIO |
					D3DPRESENTFLAG_PROGRESSIVE;
			}
			else
			{
				PresentParms.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER |
					D3DPRESENTFLAG_10X11PIXELASPECTRATIO |
					D3DPRESENTFLAG_INTERLACED;
			}
			// scion sz - set widescreen if needed
			if (XGetVideoFlags() & XC_VIDEO_FLAGS_WIDESCREEN)
			{
				PresentParms.Flags |= D3DPRESENTFLAG_WIDESCREEN;
			}
			PresentParms.SwapEffect = D3DSWAPEFFECT_DISCARD;
			PresentParms.BackBufferWidth = 640;
			PresentParms.BackBufferHeight = 480;
			PresentParms.BackBufferCount = 1;
			PresentParms.EnableAutoDepthStencil = 1;
			// Check for PAL support
			if (XGetVideoStandard() == XC_VIDEO_STANDARD_PAL_I)
			{
				PresentParms.FullScreen_RefreshRateInHz = 50;
				// Check for PAL60 support
				if (XGetVideoFlags() & XC_VIDEO_FLAGS_PAL_60Hz)
				{
					PresentParms.FullScreen_RefreshRateInHz = 60;
				}
			}
			else
			{
				PresentParms.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
			}
#ifdef PROFILE
			PresentParms.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
#else
			PresentParms.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_ONE_OR_IMMEDIATE;
#endif
			PresentParms.AutoDepthStencilFormat = D3DFMT_D24S8;
			PresentParms.BackBufferFormat = D3DFMT_A8R8G8B8;
			// Create the D3D rendering device
			bSuccess = SUCCEEDED(GD3D->CreateDevice(0,D3DDEVTYPE_HAL,NULL,
				D3DCREATE_HARDWARE_VERTEXPROCESSING,&PresentParms,
				&GD3DDevice));
			// scion sz - copy to global
			appMemcpy( &GPresentParms, &PresentParms, sizeof(D3DPRESENT_PARAMETERS) );
		}
	}
	return bSuccess;
}

/**
 * Figures out which files to cache for the selected language
 */
void CacheLocFiles(void)
{
	guard(CacheLocFiles);
	// Always cache the INT files
	GFileCacheManager->AddFileToCacheList(FString("D:\\System\\Localization\\Int\\*.int"));
	// Check our language
	DWORD LangID = appGetLanguageID();
	// Don't do the extra caching for english
	if (LangID > LC_ENGLISH)
	{
		// Figure out the path and extension for this language
		const TCHAR* Lang = GLanguageExt[LangID];
		// Now handle the language specific
		GFileCacheManager->AddFileToCacheList(FString::Printf(TEXT("D:\\System\\Localization\\%s\\*.%s"),
			Lang,Lang));
	}
	unguard;
}

/**
 * Tells the file caching code which files are needed on the HD
 */
void PerformStartupCopying(void)
{
	guard(PerformStartupCopying);
#if USE_LINEAR_LOADING
	// Cache the loading movie to hd
	GFileCacheManager->AddFileToCacheList(FString(TEXT("D:\\Textures\\Movies\\UI_LoadingMovie.xmv")));
	// Now the lin files
	GFileCacheManager->AddFileToCacheList(FString("D:\\System\\Script.lin"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\XboxMaps\\Menu.lin"));
	// Copy over all script files
	GFileCacheManager->AddFileToCacheList(FString("D:\\System\\Core.u"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\System\\Editor.u"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\System\\Engine.u"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\System\\GamePlay.u"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\System\\GUI.u"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\System\\IpDrv.u"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\System\\UCAnnouncements.u"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\System\\UCCharacters.u"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\System\\UCEffects.u"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\System\\UCElements.u"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\System\\UCGameplay.u"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\System\\UCInventory.u"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\System\\UCMenu.u"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\System\\UCMutators.u"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\System\\UnrealGame.u"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\System\\UWeb.u"));
	// Copy over the sound files that we need to stream from
	GFileCacheManager->AddFileToCacheList(FString("D:\\Sounds\\A_Character.xwb"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\Sounds\\A_Combo.xwb"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\Sounds\\A_Gameplay.xwb"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\Sounds\\A_Pickup.xwb"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\Sounds\\A_Weapon.xwb"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\Sounds\\A_WeaponMelee.xwb"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\Sounds\\A_WeaponRace.xwb"));
	// Small commonly used files
	GFileCacheManager->AddFileToCacheList(FString("D:\\XboxTextures\\*.utx"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\System\\*.ini"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\System\\Ladders\\*.ini"));
	CacheLocFiles();
	// Now copy the animation packages
	GFileCacheManager->AddFileToCacheList(FString("D:\\XboxAnimations\\K_Male.ukx"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\XboxAnimations\\K_Female.ukx"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\XboxAnimations\\K_Skaarj.ukx"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\XboxAnimations\\K_Weapons.ukx"));
	GFileCacheManager->AddFileToCacheList(FString("D:\\XboxAnimations\\K_Pickups.ukx"));
#endif
	unguard;
}

/**
 * Creates a localized XPR file name for the start up screens
 */
FString GetStartupXPRName(void)
{
	DWORD LangID = appGetLanguageID();
	// Only pick a different file for outside the North American market
	if (XGetGameRegion() > XC_GAME_REGION_NA)
	{
		// Build the legalize files by language
		return FString::Printf(TEXT("..\\Startup\\UC2Startup_%s.xpr"),
			GLanguageExt[LangID]);
	}
	else
	{
		// North American only
		return FString(TEXT("..\\Startup\\UC2Startup.xpr"));
	}
}

/**
 * Loads the UC2Startup.xpr, shows the legal image for 5 secs, followed by the
 * ESRB image for 5 secs. During that time it is copying to the hard drive.
 */
void ShowSplashScreens(void)
{
	guard(ShowSplashScreens);
	extern void RenderFullScreenQuad(LPDIRECT3DTEXTURE8 Texture);
	// Resolve the path so the demo doesn't hurt us
	const FString& StartupFile = GFileManager->ResolvePath(*GetStartupXPRName());
	CXBPackedResource Startup;
	debugf(NAME_DevJoe,TEXT("Loading XPR %s"),*StartupFile);
	// Load the XPR file
	HRESULT hr = Startup.Create(TCHAR_TO_ANSI(*StartupFile));
	// Get the legal screen
	LPDIRECT3DTEXTURE8 LegalTexture = Startup.GetTexture("UC2Legal");
	check(LegalTexture && "Missing the legal texture in UC2Startup.xpr");
	FLOAT Time = appSeconds();
	// Create our global sound & graphics devices now. Deferred so we don't have
	// a non-interactive state > 500ms
	verify(CreateDevices());
	// Render the legal texture for ~5 seconds
	RenderFullScreenQuad(LegalTexture);
	// Present the frame
	GD3DDevice->Present(NULL,NULL,NULL,NULL);
	FInitialMovieThread LogoPlayer;
	// Load both movies into RAM
	UBOOL bLogoOK = LogoPlayer.Init();
	// Once this file is loaded, start the caching
	PerformStartupCopying();
	// Now sleep until we're done
	appSleep(5.f - (appSeconds() - Time));
	debugf(NAME_DevJoe,TEXT("Done showing legal splash screen"));
	// Get the rating screen
	LPDIRECT3DTEXTURE8 RatingTexture = Startup.GetTexture("UC2Rating");
	check(RatingTexture && "Missing the ratings texture in UC2Startup.xpr");
	// Render the ratings screen for ~5 secs
	RenderFullScreenQuad(RatingTexture);
	// Present the frame
	GD3DDevice->Present(NULL,NULL,NULL,NULL);
	// Now sleep until we're done
	appSleep(4.9f);
	debugf(NAME_DevJoe,TEXT("Done showing ratings splash screen"));
	// Clear this out so the api doesn't freak out when it goes away
	GD3DDevice->SetTexture(0,NULL);
	// Play the movie if it loaded into RAM correctly
	if (bLogoOK == TRUE)
	{
		LogoPlayer.Run();
		LogoPlayer.Exit();
	}
	debugf(NAME_DevJoe,TEXT("Done with logo movie"));
	extern void PlayLoadingMovie(const FURL& URL,UBOOL bSinglePlayer);
	// Play the loading movie until we get to the menu
	PlayLoadingMovie(FURL(NULL,TEXT(""),TRAVEL_Absolute),TRUE);
	FLOAT WaitSecs = appSeconds();
	// Now wait for all caching to be done
	while (GFileCacheManager->IsCachingFiles())
	{
		appSleep(0.1f);
	}
	debugf(TEXT("Waited %f seconds for caching to be done"),
		appSeconds() - WaitSecs);
	unguard;
}

/* ============================================================================
 * ::ReplaceSearchPaths
 * Author: jg
 * 
 * Replaces any system defined paths with paths that point to the Xbox format
 * packages.
 *
 * ============================================================================
 */
void ReplaceSearchPaths(void)
{
	guard(ReplaceSearchPaths);
	for (INT Index = 0; Index < GSys->Paths.Num(); Index++)
	{
		if (appStrstr(*GSys->Paths(Index),TEXT("\\StaticMeshes")) != NULL)
		{
			GSys->Paths(Index) = TEXT("..\\XboxStaticMeshes\\*.usx");
		}
		else if (appStrstr(*GSys->Paths(Index),TEXT("\\Maps")) != NULL)
		{
			GSys->Paths(Index) = TEXT("..\\XboxMaps\\*.unr");
		}
		else if (appStrstr(*GSys->Paths(Index),TEXT("\\Animations")) != NULL)
		{
			GSys->Paths(Index) = TEXT("..\\XboxAnimations\\*.ukx");
		}
		else if (appStrstr(*GSys->Paths(Index),TEXT("\\Textures")) != NULL)
		{
			GSys->Paths(Index) = TEXT("..\\XboxTextures\\*.utx");
		}
	}
	unguard;
}

/**
 * Initializes the base set of devices for the start up movie(s)
 */
void InitGamepads(void)
{
	guard(InitGamepads);
	XDEVICE_PREALLOC_TYPE PreallocationInfo[] =
	{
		{XDEVICE_TYPE_GAMEPAD, 4},
#ifndef FINAL_RELEASE
		{XDEVICE_TYPE_DEBUG_KEYBOARD, 1},
#endif
		{XDEVICE_TYPE_VOICE_HEADPHONE, 1},
		{XDEVICE_TYPE_VOICE_MICROPHONE, 1}
	};
	XInitDevices(ARRAY_COUNT(PreallocationInfo),PreallocationInfo);	
	extern FXboxGamepadState Gamepads[4];
	// Open all input devices
	for (INT Index = 0; Index < 4; Index++)
	{
		Gamepads[Index].Handle = XInputOpen(XDEVICE_TYPE_GAMEPAD,
			Index,XDEVICE_NO_SLOT,NULL);
	}
	unguard;
}

/**
 * Gets the string to use for displaying the localized dirty disc message
 */
const UNICHAR* GetDirtyDiscLine1(void)
{
	return appToUnicode(LocalizeError(TEXT("DirtyDiscLine1"),TEXT("Core")));
}

/**
 * Gets the second string to use for displaying the localized dirty disc message
 */
const UNICHAR* GetDirtyDiscLine2(void)
{
	return appToUnicode(LocalizeError(TEXT("DirtyDiscLine2"),TEXT("Core")));
}

UBOOL GDirtyDisc = 0;

/**
 * Draws a dirty disc message on the screen and pauses the game forever
 */
void ShowDirtyDiscError(void)
{
#if !defined(LIN_SAVE)
	XFONT* DefaultFont;
	// Mark that we are in error. Huge hack but otherwise XMVs continue to play
	GDirtyDisc = 1;
	// Get the font stored in xgraphics.lib
	XFONT_OpenDefaultFont(&DefaultFont);
	IDirect3DSurface8* pBackbuffer = NULL;
	// Get the back buffer as a surface
	GD3DDevice->GetBackBuffer(0,0,&pBackbuffer);
	// Clear the backbuffer
	GD3DDevice->Clear(0,NULL,D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,
		0,1.f,0);
	DWORD Width;
	// Set the color to white
	DefaultFont->SetTextColor(FColor(255,255,255,255));
	// See how tall it is
	DWORD Height = DefaultFont->GetTextHeight();
	// See how wide the first string will be
	DefaultFont->GetTextExtent(GetDirtyDiscLine1(),-1,(UINT*)&Width);
	// Move it to the center of the screeen
	DWORD X = (640 - Width) / 2;
	DWORD Y = (480 - (Height * 2 + 2)) / 2;
	// Now draw this directly into the backbuffer
	DefaultFont->TextOut(pBackbuffer,GetDirtyDiscLine1(),-1,X,Y);
	// See how wide the string will be
	DefaultFont->GetTextExtent(GetDirtyDiscLine2(),-1,(UINT*)&Width);
	// Move it to the center of the screeen
	X = (640 - Width) / 2;
	// Move it down one line plus a little breathing room
	Y += Height + 2;
	// Now draw this directly into the backbuffer
	DefaultFont->TextOut(pBackbuffer,GetDirtyDiscLine2(),-1,X,Y);
	// Show it and then sleep the long sleep
	GD3DDevice->Present(NULL,NULL,NULL,NULL);
#ifndef FINAL_RELEASE
	check(0 && "Scott send Joe the stack for this missing/corrupt file");
#endif
	DWORD Infinite = INFINITE / 1000;
	appSleep((FLOAT)Infinite);
#endif
}

// Unreal's main message loop.  All windows in Unreal receive messages
// somewhere below this function on the stack.
static void MainLoop( UEngine* Engine )
{
	guard(MainLoop);
	check(Engine);

	// Loop while running.
	GIsRunning = 1;
	// scion sz - use cycles instead of seconds so that we maintain precision
	SQWORD OldCycles = appCycles64();
	FLOAT OldTime = appSeconds();
	FLOAT SecondStartTime = OldTime;
	//scion jg -- Init the hack time counter
	GLipSincTimeHack = OldTime;
	INT TickCount = 0;
#if BENCHMARKING
	INT		BMFrames			= 0;
	INT		BMDiscardedFrames	= 10;
	INT		BMMaxFrames			= 46 * 30 + 10;
	FLOAT	BMSeconds			= 0.0f;

#if BENCHMARKING_NOGPU
	//scion jg -- Mimic infinitely fast GPU
	extern BOOL D3D__NullHardware;
	D3D__NullHardware = 1;
#endif

	// Seed random number generator with constant seed for benchmarking.
	appRandInit( 0 );

	GIsBenchmarking = 1;
#endif

	while( GIsRunning && !GIsRequestingExit )
	{
#if USE_STATS
		// Clear stats (will also update old stats).
		GStats.Clear();
		DWORD FrameStartCycles = appCycles();
#endif
		// Update the world.
		guard(UpdateWorld);

		// scion sz - get current cycles to calculate delta time
		SQWORD NewCycles = appCycles64();
		FLOAT NewTime = appSeconds();

		//scion jg -- Handle recording movie to disk TODO remove from final build
		FLOAT DeltaTime = GIsRecordingMovie ? GMovieTimeStep : (DOUBLE)(NewCycles - OldCycles) * GSecondsPerCycle;
		//debugf( TEXT("DeltaTime=%4.6f (%3.3f FPS)"), DeltaTime, 1.f/DeltaTime );		

		//scion jg -- Hack hack baby
		GLipSincTimeHack += DeltaTime;
		
#if BENCHMARKING
		if( BMFrames == BMDiscardedFrames )
			BMSeconds	= 0.0f;
		else if( BMFrames > BMMaxFrames )
			appRequestExit(0);
		
		//Update.
		Engine->Tick( 1.0f / 30.0f );
		if( GWindowManager )
			GWindowManager->Tick( 1.0f / 30.0f );

		if( BMFrames < BMMaxFrames )
			BMSeconds += DeltaTime;
		BMFrames++;
#else
		//Update.
		Engine->Tick( DeltaTime );
		if( GWindowManager )
			GWindowManager->Tick( DeltaTime );
#endif	
	
		//scion jg -- Take a screen shot of the current frame
		if (GIsRecordingMovie == 1)
		{
			Engine->Client->Viewports(0)->Exec(TEXT("shot"));
		}

		OldCycles = NewCycles;
		OldTime = NewTime;
		TickCount++;
		if( OldTime > SecondStartTime + 1 )
		{
			Engine->CurrentTickRate = (FLOAT)TickCount / (OldTime - SecondStartTime);
			SecondStartTime = OldTime;
			TickCount = 0;
		}
		unguard;
#if 0
		// Enforce optional maximum tick rate.
		guard(EnforceTickRate);
		FLOAT MaxTickRate = Engine->GetMaxTickRate();
		if( MaxTickRate>0.0 )
		{
			FLOAT Delta = (1.0/MaxTickRate) - (appSeconds()-OldTime);
			appSleep( Max(0.f,Delta) );
		}
		unguard;
#endif

#if USE_STATS
		GStats.DWORDStats( GEngineStats.STATS_Frame_TotalCycles ) = appCycles() - FrameStartCycles;
		GStats.DWORDStats( GEngineStats.STATS_Game_ScriptCycles ) = GScriptCycles;
		GScriptCycles = 0;
#endif
	}

#if BENCHMARKING
	INT Frames = BMMaxFrames - BMDiscardedFrames;
	debugf(TEXT("Average: %f fps"), Frames / BMSeconds );
#endif

	GIsRunning = 0;

	unguard;
}

#if USE_LINEAR_LOADING
#ifdef LIN_SAVE
/** The list of icons to use for save games */
const TCHAR* SaveGameIcons[] =
{
	"Portrait_Icon_Anubis_1",
	"Portrait_Icon_Arclite_1",
	"Portrait_Icon_Brock_1",
	"Portrait_Icon_Devastation_1",
	"Portrait_Icon_Gorge_1",
	"Portrait_Icon_Lauren_1",
	"Portrait_Icon_Malcolm_1",
	"Portrait_Icon_MercAnubis_1",
	"Portrait_Icon_Raiden_1",
	"Portrait_Icon_Raptor_1",
	"Portrait_Icon_Sapphire_1",
	"Portrait_Icon_Selket_1",
	"Portrait_Icon_Sobek_1",
	"Portrait_Icon_Szalor_1",
	"Portrait_Icon_Torg_1",
	"Portrait_Icon_Apophis",
	"Portrait_Icon_Calypso",
	"Portrait_Icon_Corrosion",
	"Portrait_Icon_Darius",
	"Portrait_Icon_Garek",
	"Portrait_Icon_Hyena",
	"Portrait_Icon_Jackhammer",
	"Portrait_Icon_Judas",
	"Portrait_Icon_Korig",
	"Portrait_Icon_Kraag",
	"Portrait_Icon_Lilith",
	"Portrait_Icon_Memphis",
	"Portrait_Icon_Mirage",
	"Portrait_Icon_NecrisSelket_1",
	"Portrait_Icon_Syzygy",
	NULL
};

/**
 * Creates a set of save game images that Ron associates with a profile
 */
void GenerateSaveGameImages(void)
{
	guard(GenerateSaveGameImages);
	// Load the XPR with all of the icons in it
	CXBPackedResource Natives;
	// Load the XPR file
	HRESULT hr = Natives.Create("d:\\xboxtextures\\natives.xpr",0,1);
	// Create an image for each save game icon
	for (INT Index = 0; SaveGameIcons[Index] != NULL; Index++)
	{
		LPDIRECT3DTEXTURE8 Texture = Natives.GetTexture(SaveGameIcons[Index]);
		if (Texture != NULL)
		{
			LPDIRECT3DSURFACE8 pRgb = NULL;
			// Create our save game texture
			HRESULT hr = GD3DDevice->CreateImageSurface(64,64,
				D3DFMT_X8R8G8B8,&pRgb);
			if (SUCCEEDED(hr))
			{
				LPDIRECT3DSURFACE8 pSurface = NULL;
				// Get the surface for this texture
				hr = Texture->GetSurfaceLevel(0,&pSurface);
				check(SUCCEEDED(hr));
				// Copy and filter the texture
				hr = D3DXLoadSurfaceFromSurface(pRgb,NULL,NULL,
					pSurface,NULL,NULL,D3DX_DEFAULT,D3DCOLOR(0));
				check(SUCCEEDED(hr));
				// Get rid of it now that the copy is done
				pSurface->Release();
				LPDIRECT3DSURFACE8 pDxt1 = NULL;
				// Create our save game texture
				hr = GD3DDevice->CreateImageSurface(64,64,
					D3DFMT_DXT1,&pDxt1);
				check(SUCCEEDED(hr));
				// Copy and filter the texture
				hr = D3DXLoadSurfaceFromSurface(pDxt1,NULL,NULL,
					pRgb,NULL,NULL,D3DX_DEFAULT,D3DCOLOR(0));
				check(SUCCEEDED(hr));
				const FString& Name = FString::Printf(TEXT("D:\\System\\%s.xpr"),
					SaveGameIcons[Index]);
				// Write the file out
				hr = XGWriteSurfaceOrTextureToXPR(pDxt1,*Name,TRUE);
				if (FAILED(hr))
				{
					debugf(TEXT("Failed to write icon (%s) image 0x%X"),
						SaveGameIcons[Index],hr);
				}
				// Clean up the created texture
				pDxt1->Release();
				pRgb->Release();
			}
			else
			{
				debugf(TEXT("Failed to create RGB intermediate texture 0x%X"),hr);
			}
		}
	}
	unguard;
}

/**
 * Iterate through all of the UNR files in XboxMaps and make a lin file for
 * each. Skip entry.lin and menu.lin since they are created during engine
 * initialization.
 *
 * @param GameEngine the game engine object to load maps with
 */
void SaveMapLins(UGameEngine* GameEngine)
{
	guard(SaveMapLins);
#if 0
	// Only used to regenerate images
	GenerateSaveGameImages();
#endif
	// Get a list of the maps to convert
#if 1
	TArray<FString> MapList = GFileManager->FindFiles(
		TEXT("D:\\XboxMaps\\*.unr"),1,0);
#else 
	TArray<FString> MapList;
//	new(MapList)FString(TEXT("od-cancer.unr"));
//	new(MapList)FString(TEXT("dm-solarflare.unr"));
//	new(MapList)FString(TEXT("tut-advmovement.unr"));
	new(MapList)FString(TEXT("dm-acidrain.unr"));
#endif
	// For each map in this list, create a LIN
	for (INT Index = 0; Index < MapList.Num(); Index++)
	{
		FString& MapName = MapList(Index);
		// Don't process files that have already been done or aren't worth doing
		if (MapName.Left(4) != TEXT("Auto") &&
			MapName.Left(4) != TEXT("menu") &&
			MapName.Left(5) != TEXT("entry"))
		{
			// Ignored, but required for the API
			TMap<FString,FString> Travel;
			FString Error;
			// Open the map, which will create the LIN file
			GameEngine->LoadMap(FURL(*MapName),NULL,&Travel,Error);
		}
	}
	// We're done, so reboot
	XLaunchNewImage(NULL,NULL);
	unguard;
}
#endif
#endif

/*-----------------------------------------------------------------------------
	WinMain.
-----------------------------------------------------------------------------*/

//
// Main entry point.
// This is an example of how to initialize and launch the engine.
//
INT main(INT ArgC,char* ArgV[])
{
	guard(main);
	// Remember instance.
	INT ErrorLevel = 0;
	GIsStarted     = 1;

	//jg -- Change precision to 24 bits for speed. Slight framerate improvement
	_controlfp(_PC_24,_MCW_PC);

	XSetFileCacheSize( 4 * 1024 * 1024 );
	MEMORYSTATUS MemStatus;

	// Get command line.
	DWORD LaunchType;
	LAUNCH_DATA LaunchData;

	DWORD Result = XGetLaunchInfo( &LaunchType, &LaunchData );
	if( Result != ERROR_NOT_FOUND )
	{
		switch( LaunchType )
		{
			case LDT_FROM_DASHBOARD:
			{
				appStrcpy(GCmdLine,TEXT(" "));
				break;
			}
			case LDT_FROM_DEBUGGER_CMDLINE:
			{
                if ( LaunchData.Data[0] != 0 )
			        appStrcpy( GCmdLine, appFromAnsi( (char*)LaunchData.Data ));
				break;
			}
  			case LDT_TITLE:
			{
				break;
			}
			default:
				break;
		}
	}

#if USE_LINEAR_LOADING
//jg -- Choose the right loading/saving options
#ifdef LIN_LOAD
	GLinearLoad = 1;
#endif
#ifdef LIN_SAVE
	GLinearSave = 1;
#endif

	GLazyLoad		= 0;	// Needed by linear loading.
//jg -- We load texture data out of XPR files so don't force load the mips
//	GUglyHackFlags |= 32;	//!!vogel: temporary hack.

#endif

	// Static classes.
	static UClass*  EngineClass;
	static UEngine* EngineObject;

	//!!
	GMalloc = &Malloc;

	// File managers.
#if USE_LINEAR_LOADING
#if LIN_LOAD
	FFileManagerTOC FileManagerRaw; 
#else
	FFileManagerXbox FileManagerRaw;
#endif
	FFileManagerLinear FileManagerLinear(&FileManagerRaw);
#else
	FFileManagerXbox	FileManagerRaw;
#endif

	guard(Devices);
#ifdef LIN_SAVE//jg -- This is deferred to just before loading the splash screens for normal builds
	//jg -- Create our global sound & graphics devices now
	verify(CreateDevices());
#endif
	//jg -- Moved into here for start up movie support
	InitGamepads();
	unguard;

	//jg -- Set the pointer to the synchronization factory
	GSynchronizeFactory = &SynchFactory;
	GThreadFactory = &ThreadFactory;

	// Init core.
	GIsClient = GIsGuarded = 1;
#if USE_LINEAR_LOADING
	appInit( GPackage, GCmdLine, &Malloc, &Log, &Error, &Warn, &FileManagerLinear, FConfigCacheIni::Factory, 1 );
#else
	appInit( GPackage, GCmdLine, &Malloc, &Log, &Error, &Warn, &FileManagerRaw, FConfigCacheIni::Factory, 1 );
#endif

#ifdef USE_GCONTIG
	//jg -- Init the contiguous malloc
	ContigMalloc.Init();
	GContigMalloc = &ContigMalloc;
#endif

	//jg -- File cache set up
	GFileCacheManager = &FileCacheMan;
	UBOOL bSuccess = GFileCacheManager->InitFileCache();
	check(bSuccess);

	FLOAT StartTime = appSeconds();

#ifndef LIN_SAVE
	guard(SplashScreen);
	//jg -- Cache needed files to the hard drive and play the startup movies
	ShowSplashScreens();
	unguard;
#endif
	//jg -- Track how long it takes to load to menu post movies
	FLOAT LogoEndTime = appSeconds();

#ifdef XBOX_ASSET_FORMAT
	//jg -- Load all textures for native packages
	extern void LoadXPRForNatives(void);
	LoadXPRForNatives();
#endif

	//jg -- Hack for not throttling Midway servers
	NetHackCheck();

#if USE_LINEAR_LOADING
	//jg -- Start/create the script.lin file
	GFileManager->OpenLinearFile(TEXT("Script.lin"));
#endif

    // Static linking.
#if __STATIC_LINK
    for( INT k=0; k<ARRAY_COUNT(GNativeLookupFuncs); k++ )
		GNativeLookupFuncs[k] = NULL;
    INT Lookup = 0;
    // Core natives.
    GNativeLookupFuncs[Lookup++] = &FindCoreUObjectNative;
    GNativeLookupFuncs[Lookup++] = &FindCoreUCommandletNative;
    // auto-generated lookups and statics
	AUTO_INITIALIZE_REGISTRANTS_ENGINE;
	AUTO_INITIALIZE_REGISTRANTS_XBOXDRV;
	AUTO_INITIALIZE_REGISTRANTS_XBOXAUDIO;
	AUTO_INITIALIZE_REGISTRANTS_D3DDRV;
	AUTO_INITIALIZE_REGISTRANTS_IPDRV;
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
#ifndef _XBOX
	GIsEditor     = 0;
#endif
	GIsScriptable = 1;

#ifdef XBOX_ASSET_FORMAT
	//jg -- Change the search paths to point at the optimized content only
	ReplaceSearchPaths();
#endif

	EngineClass   = UObject::StaticLoadClass(UGameEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.GameEngine"), NULL, LOAD_NoFail, NULL );
	EngineObject  = ConstructObject<UEngine>( EngineClass );

	// Print some memory stats.
	GlobalMemoryStatus( &MemStatus );
	debugf(TEXT("MEMORY: %d KByte of physical memory in use"), (MemStatus.dwTotalPhys - MemStatus.dwAvailPhys) / 1024 );

	// Engine initialization.
	FLOAT Dummy = appSeconds();
	//scion rwd - set the static engine ptr
	UEngine::GEngine = EngineObject;
	EngineObject->Init();
	debugf(TEXT("Engine->Init: %f"), (FLOAT) (appSeconds() - Dummy));

#if !USE_LINEAR_LOADING
	// Precaching.
	Dummy = appSeconds();
	GIsRunning = 1;
	if( EngineObject->Client && EngineObject->Client->Viewports(0) )
		EngineObject->Client->Viewports(0)->Repaint( 0 );
	GIsRunning = 0;
	debugf(TEXT("Precaching: %f"), (FLOAT) (appSeconds() - Dummy));	
#endif

#if USE_LINEAR_LOADING
#ifdef LIN_SAVE
	//jg -- Process all the linear files. Do this here before slowing
	// things down by using a small file cache. Reboot once done.
	SaveMapLins((UGameEngine*)EngineObject);
#endif
#endif

	XSetFileCacheSize( 64 * 1024 );

#ifndef FINAL_RELEASE
	// Print some memory stats.
	GlobalMemoryStatus( &MemStatus );
	debugf(TEXT("MEMORY: %d KByte of physical memory in use"), (MemStatus.dwTotalPhys - MemStatus.dwAvailPhys) / 1024 );

	// Start main engine loop.
	Dummy = appSeconds();
	Log.Logf(TEXT("Starting Engine"));
#endif
	for( TObjectIterator<UClass> It; It; ++It )
		It->LoadConfig();
	
	debugf( TEXT("Startup time %f"),(FLOAT) (appSeconds() - StartTime));
	debugf(TEXT("Startup time from logo end %f"),(FLOAT)(appSeconds() - LogoEndTime));

	// Begin guarded code.
#ifndef _DEBUG
	guard(MainLoop);
	{
#endif
		// Main engine loop.
		if( !GIsRequestingExit )
			MainLoop( EngineObject );

		debugf(TEXT("Exiting Loop"));
		XLaunchNewImage ( NULL, NULL );  //scion capps To have exit drop you back at launcher/dashboard

		appPreExit();
		GIsGuarded = 0;
#ifndef _DEBUG
	}
	unguard;
#endif

	// Final shut down.
	appExit();
	GIsStarted = 0;

	return ErrorLevel;
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
