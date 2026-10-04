/*=============================================================================
	XboxMovieThread.h:
	Copyright 2003 Scion Studios. All Rights Reserved.
=============================================================================*/

#include "XboxDrv.h"

#include "XboxMovieThread.h"
#include <xfont.h>

// Global for tracking the currently running movie
XboxMovieThread* GMovieThread = NULL;

// Global for tracking the "preload" movie
FLoadToMemThread* GLoadToMemThread = NULL;

// Globals for dealing with gamepad input. This is needed because the xbox API
// won't let you open multiple handles to the same gamepad. Annoying, because
// now we have to resort to using globals so that changes in either thread are
// reflected. Thread safe only because of the usage pattern.
extern FXboxGamepadState Gamepads[4];

// Used to tell the main thread that stuff happened during transition
UBOOL GamepadRemovedDuringMovie[4] = { 0,0,0,0 };
UBOOL GamepadAddedDuringMovie[4] = { 0,0,0,0 };

// Global D3D device
extern IDirect3DDevice8* GD3DDevice;

#ifndef FINAL_RELEASE
// Global for tracking whether to show the safezone or not
UBOOL GShowMovieSafezone = 0;

/**
 * Renders the safe zone so that sub-titling can be verified
 */
void RenderSafeZone(void)
{
	guard(RenderSafeZone);
	//jg -- For showing safe zone. Safe zone is 87.5% of the screen area,
	// which is 40-600 by 30-450. The consts off by two because the line
	// is 2 wide
	FLOAT X1 = 37.5f;
	FLOAT Y1 = 27.5f;
	FLOAT X2 = 639.5f - 38.f;
	FLOAT Y2 = 479.5f - 28.f;
	// Build the static quad
	struct FStream
	{
		D3DVECTOR4 Vert;
		FLOAT U, V;
	};
	FStream Stream[] =
	{
		// First quad
		{{X1,Y1,0.f,1.f},0.f,0.f},
		{{X1,Y2,0.f,1.f},0.f,1.f},
		{{X2,Y2,0.f,1.f},1.f,1.f},
		{{X2,Y1,0.f,1.f},1.f,0.f},
		// Second quad (just inside first)
		{{X1 + 1.f,Y1 + 1.f,0.f,1.f},0.f,0.f},
		{{X1 + 1.f,Y2 - 1.f,0.f,1.f},0.f,1.f},
		{{X2 - 1.f,Y2 - 1.f,0.f,1.f},1.f,1.f},
		{{X2 - 1.f,Y1 + 1.f,0.f,1.f},1.f,0.f}
	};
	// Set the texture stuff
	GD3DDevice->SetTexture(0,NULL);
	GD3DDevice->SetPixelShader(NULL);
	GD3DDevice->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);
	GD3DDevice->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
	GD3DDevice->SetTextureStageState(0,D3DTSS_ADDRESSU,D3DTADDRESS_CLAMP);
	GD3DDevice->SetTextureStageState(0,D3DTSS_ADDRESSV,D3DTADDRESS_CLAMP);
	// Set render state stuff
	GD3DDevice->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
	GD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
	GD3DDevice->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);
	GD3DDevice->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ZERO);
	GD3DDevice->SetRenderState(D3DRS_ZENABLE,FALSE);
	GD3DDevice->SetRenderState(D3DRS_FOGENABLE,FALSE);
	GD3DDevice->SetRenderState(D3DRS_FILLMODE,D3DFILL_WIREFRAME);
	GD3DDevice->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
	// Render the quads
	GD3DDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_TEX1);
	GD3DDevice->DrawPrimitiveUP(D3DPT_QUADLIST,2,&Stream,sizeof(FStream));
	unguard;
}
#endif

#define MAP_NAME_AREA_X (110)
#define MAP_NAME_AREA_X1 (600)
#define MAP_NAME_AREA_Y (420)
#define MAP_NAME_AREA_Y1 (450)
#define MAP_NAME_AREA_WIDTH (MAP_NAME_AREA_X1 - MAP_NAME_AREA_X)
#define MAP_NAME_AREA_HEIGHT (MAP_NAME_AREA_Y1 - MAP_NAME_AREA_Y)

/**
 * Draws the Loading <mapname> message
 *
 * @param LoadingMsg the loading message to show
 */
void RenderMapName(const TCHAR* LoadingMsg)
{
	guard(RenderMapName);
	XFONT* DefaultFont;
	// Get the font stored in xgraphics.lib
	XFONT_OpenDefaultFont(&DefaultFont);
	IDirect3DSurface8* pBackbuffer = NULL;
	// Get the back buffer as a surface
	GD3DDevice->GetBackBuffer(0,0,&pBackbuffer);
	// Set the color to white
	DefaultFont->SetTextColor(FColor(150,150,150,128));
	// See how tall it is
	DWORD Height = DefaultFont->GetTextHeight();
	DWORD Width;
	// Convert to UNICODE
	LPCWSTR OutputString = appToUnicode(LoadingMsg);
	// See how wide the string will be
	DefaultFont->GetTextExtent(OutputString,-1,(UINT*)&Width);
	// Move it to the center of the available area
	DWORD X = (MAP_NAME_AREA_X1 - Width - 2);
	DWORD Y = MAP_NAME_AREA_Y + ((MAP_NAME_AREA_HEIGHT - Height) / 2);
	// Now draw this directly into the backbuffer
	DefaultFont->TextOut(pBackbuffer,OutputString,-1,X,Y);
	// Let go of our render target
	pBackbuffer->Release();
	// Don't hold a pointer to the font
	DefaultFont->Release();
	unguard;
}

/**
 * Renders a set of black boxes over the non-video area, hiding subtitles
 */
void HideSubtitles(void)
{
	guard(HideSubtitles);
	D3DRECT Rects[] =
	{
		{ 0, 0, 640, 96 },
		{ 0, 384, 640, 480 }
	};
	// Clear the rects defined above
    GD3DDevice->Clear(2,Rects,D3DCLEAR_TARGET,0x00000000,1.f,0L);
	unguard;
}

/**
 * Renders a full screen quad with the specified material
 *
 * @param Texture the texture to render on the quad
 */
void RenderFullScreenQuad(LPDIRECT3DTEXTURE8 Texture)
{
	guard(RenderFullScreenQuad);
	// This is an embarrassing hack, but needed because a thread might be
	// writing to the backbuffer while another needs to show the error
	extern UBOOL GDirtyDisc;
	if (GDirtyDisc == TRUE)
	{
		void ShowDirtyDiscError(void);
		ShowDirtyDiscError();
	}
	// Build the static quad
	struct FStream
	{
		D3DVECTOR4 Vert;
		FLOAT U, V;
	};
	FStream Stream[] =
	{
		// Vert followed by UVs
		{{-0.5f,-0.5f,0.f,1.f},0.f,0.f},
		{{-0.5f,479.5f,0.f,1.f},0.f,480.f},
		{{639.5f,479.5f,0.f,1.f},640.f,480.f},
		{{639.5f,-0.5f,0.f,1.f},640.f,0.f}
	};
	// Set the texture stuff
	GD3DDevice->SetTexture(0,Texture);
	GD3DDevice->SetPixelShader(NULL);
	GD3DDevice->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
	GD3DDevice->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);
	GD3DDevice->SetTextureStageState(0,D3DTSS_ADDRESSU,D3DTADDRESS_CLAMP);
	GD3DDevice->SetTextureStageState(0,D3DTSS_ADDRESSV,D3DTADDRESS_CLAMP);
	// Set render state stuff
	GD3DDevice->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
	GD3DDevice->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
	GD3DDevice->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);
	GD3DDevice->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ZERO);
	GD3DDevice->SetRenderState(D3DRS_ZENABLE,FALSE);
	GD3DDevice->SetRenderState(D3DRS_FOGENABLE,FALSE);
	GD3DDevice->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);
	GD3DDevice->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
	// Render the quad with the specified texture
	GD3DDevice->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_TEX1);
	GD3DDevice->DrawPrimitiveUP(D3DPT_QUADLIST,1,&Stream,sizeof(FStream));
	unguard;
}

/* scion ======================================================================
 * IsPlayingMovie
 * Author: jg
 * 
 * Checks to see if the movie player is valid
 *
 * ============================================================================
 */
UBOOL IsPlayingMovie(void)
{
	return GMovieThread != NULL;
}

/* scion ======================================================================
 * SetEngineReady
 * Author: jg
 * 
 * Tells the background thread that the engine is done loading the map.
 *
 * input:	Movie The XboxMovieThread object to trigger
 *
 * ============================================================================
 */
void SetEngineReady(void)
{
	// Make sure there is a movie playing
	if (GMovieThread != NULL)
	{
		XboxMovieThread* pMovie = GMovieThread;
		GMovieThread = NULL;
		// Wait for the movie thread to finish
		pMovie->EngineIsReady();
		// Free the memory
		delete pMovie;
	}
}

/* scion ======================================================================
 * WasInterruptRequested
 * Author: jg
 * 
 * Checks the gamepad controllers for input. Returns true if the user gave some
 * kind of input.
 *
 * ============================================================================
 */
static UBOOL WasInterruptRequested(void)
{
	guard(WasInterruptRequested);
	DWORD dwInserted = 0;
	DWORD dwRemoved = 0;
	// Check to see if any of the existing devices was removed or new ones
	// were added
	if (SUCCEEDED(XGetDeviceChanges(XDEVICE_TYPE_GAMEPAD,&dwInserted,
		&dwRemoved)))
	{
		// Loop through checking the devices
		for (INT nIndex = 0; nIndex < GAMEPAD_COUNT; nIndex++)
		{
			// Check to see if something we knew about was removed
			if (((1 << nIndex) & dwRemoved) &&
				Gamepads[nIndex].Handle != NULL)
			{
				// Close and zero the device
				XInputClose(Gamepads[nIndex].Handle);
				Gamepads[nIndex].Handle = NULL;
				GamepadRemovedDuringMovie[nIndex] = 1;
			}
			// See if a new one was added and get a handle for it
			if (((1 << nIndex) & dwInserted))
			{
				// Try to open the handle
				Gamepads[nIndex].Handle = XInputOpen(XDEVICE_TYPE_GAMEPAD,
					nIndex,XDEVICE_NO_SLOT,NULL);
				GamepadAddedDuringMovie[nIndex] = 1;
			}
		}
	}
	UBOOL bInterrupt = 0;
	// Now check all of the valid devices for input and break if it is there
	for (INT nIndex = 0; bInterrupt == 0 && nIndex < GAMEPAD_COUNT; nIndex++)
	{
		// Only check input on valid gamepads
		if (Gamepads[nIndex].Handle != NULL)
		{
			XINPUT_STATE InputState;
			// Check the state of this gamepad's buttons
			if (SUCCEEDED(XInputGetState(Gamepads[nIndex].Handle,&InputState)))
			{
				// Convert the 8 byte array for button state into a QWORD
				QWORD* qwButtons = (QWORD*)InputState.Gamepad.bAnalogButtons;
				// Check for any buttons being depressed
				bInterrupt = *qwButtons != 0;
			}
		}
	}
	return bInterrupt;
	unguard;
}

/* scion ======================================================================
 * CreateMovie
 * Author: jg
 * 
 * Creates a background renderer for the specified movie.
 *
 * input:	MovieName the name of the movie to play
 *			bWaitForComplete Whether to wait for the movie to complete or not
 *			bLoopMovie whether to play looping or not
 *			LoadingMsg the message to display while loading
 *
 * ============================================================================
 */
FMovieThread* CreateMovie(const FString& MovieName,UBOOL bWaitForComplete,
	UBOOL bLoopMovie,const TCHAR* LoadingMsg)
{
	XboxMovieThread* pThread = NULL;
	// The global device on the xbox
	extern IDirect3DDevice8* GD3DDevice;
	if (GMovieThread == NULL && GD3DDevice != NULL && MovieName.Len() > 0)
	{
		// Create a new movie thread
		pThread = (XboxMovieThread*) NEW XboxMovieThread();
		if (pThread != NULL)
		{
			// Initialize the movie with the count & the movie name
			if (pThread->Start(MovieName,LoadingMsg,bLoopMovie,
				GD3DDevice) == 0)
			{
				debugf(TEXT("Failed to load movie %s"),*MovieName);
				// Failed to create so clean it up
				delete pThread;
				pThread = NULL;
			}
		}
		// Set the global
		GMovieThread = pThread;
		// Block until complete
		if (GMovieThread != NULL && bWaitForComplete == 1)
		{
			appSleep(0.5f);
			GMovieThread->EngineIsReady(1);
		}
	}
	return pThread;
}

/* scion ======================================================================
 * GetMovieInfo
 * Author: jg
 * 
 * Finds a movie associated with the URL specified.
 *
 * input:	URL -- The URL of the map being loaded
 *			URLOpt -- The option that we are looking for
 *			MovieName -- The out string that gets the name
 *
 * ============================================================================
 */
void GetMovieInfo(const FURL& URL,const TCHAR* URLOpt,FString& MovieName)
{
	guard(GetMovieInfo);
	// Get the name of the movie from the URL
	const TCHAR* URLMovie = URL.GetOption(URLOpt,NULL);
	if (URLMovie != NULL)
	{
		// For some silly reason GetOption() can return the '='
		if (*URLMovie == TEXT('='))
		{
			URLMovie++;
		}
		// Build the full path to the file name
		MovieName = TEXT("D:\\Movies\\");
		MovieName += URLMovie;
		// In case the extension isn't there (99% of the time it won't be)
		INT Index = MovieName.InStr(TEXT("."),1);
		if (Index == -1)
		{
			MovieName += TEXT(".xmv");
		}
	}
	unguard;
}

/**
 * Determines the movie that needs to be played for an "outro" sequence.
 * It then plays the movie blocking until it is done or skipped.
 *
 * @param URL The URL to find the cinematic name on
 */
void PlayOutroCinematic(const FURL& URL)
{
	guard(PlayOutroCinematic);
	FString MovieName;
	// Get the name of the movie we are going to show
	GetMovieInfo(URL,TEXT("EndCinematic"),MovieName);
	// Now create and play that movie, blocking until done
	CreateMovie(MovieName,TRUE,FALSE,TEXT(""));
	unguard;
}

/**
 * Determines the movie that needs to be played for an "intro" sequence.
 * It then plays the movie until it is done or skipped.
 *
 * @param URL The URL to find the cinematic name on
 */
void PlayIntroCinematic(const FURL& URL)
{
	guard(PlayIntroCinematic);
	FString MovieName;
	// Get the name of the movie we are going to show
	GetMovieInfo(URL,TEXT("StartCinematic"),MovieName);
	// Now create and play that movie
	CreateMovie(MovieName,TRUE,FALSE,TEXT(""));
	unguard;
}

/**
 * Creates the string that will be displayed by the renderer during movie
 * playing. Only used during the "loading" movie. Assumes the buffer is
 * large enough to hold the resulting string
 *
 * @param LoadingMsg the buffer to build the message into
 * @param URL the url being loaded
 * @param bSinglePlayer whether this is a load for a singleplayer match or not
 */
void BuildLoadingMessage(TCHAR* LoadingMsg,const FURL& URL,UBOOL bSinglePlayer)
{
	guard(BuildLoadingMessage);
#if 0
	// Don't show any text other than loading for 
	if (appStricmp(*URL.Map.Left(4),TEXT("Menu")) != 0 && bSinglePlayer == FALSE)
	{
		// Build the loading string
		appSprintf(LoadingMsg,
			LocalizeProgress(TEXT("Loading"),TEXT("Core")),
			URL.Map.InStr(TEXT("."),1) != -1 ? *URL.Map.Left(URL.Map.Len() - 4) : *URL.Map);
	}
	else
#endif
	{
		// Can't say loading menu so append some dots
		appSprintf(LoadingMsg,
			LocalizeProgress(TEXT("Loading"),TEXT("Core")),TEXT(""));
		// Trim off the trailing space if present
		if (LoadingMsg[appStrlen(LoadingMsg) - 1] == TEXT(' '))
		{
			LoadingMsg[appStrlen(LoadingMsg) - 1] = TEXT('\0');
		}
		// Add the dots required by TCR
		appStrcat(LoadingMsg,TEXT("..."));
	}
	unguard;
}

/**
 * Starts the global from memory movie playing
 *
 * @param LoadingMsg the loading message to show
 */
void CreateInMemoryMovie(const TCHAR* LoadingMsg)
{
	guard(CreateInMemoryMovie);
	// Create a new movie thread
	XboxMovieThread* pThread = NEW XboxMovieThread();
	if (pThread != NULL)
	{
		// Initialize the movie with the count & the movie name
		if (pThread->StartFromMemory(LoadingMsg,TRUE,GD3DDevice) == 0)
		{
			// Failed to create so clean it up
			delete pThread;
			pThread = NULL;
		}
	}
	// Set the global
	GMovieThread = pThread;
	unguard;
}

/**
 * Creates the loading movie and displays that while loading a level. This
 * movie is non-blocking and will automatically stop once loading is done
 *
 * @param URL the url that is being loaded
 */
void PlayLoadingMovie(const FURL& URL,UBOOL bSinglePlayer)
{
	guard(PlayLoadingMovie);
	TCHAR LoadingMsg[256];
	// Build the string that will be displayed while loading
	BuildLoadingMessage(LoadingMsg,URL,bSinglePlayer);
	// Use the in-memory movie if present
	if (GLoadToMemThread != NULL)
	{
		CreateInMemoryMovie(LoadingMsg);
	}
	else
	{
		debugf(TEXT("In-memory loading movie not present. Loading will be slower!!!!"));
		FString MovieName(TEXT("D:\\Textures\\Movies\\UI_LoadingMovie.xmv"));
		// Now create and play that movie
		CreateMovie(MovieName,FALSE,TRUE,LoadingMsg);
	}
	unguard;
}

/* scion ======================================================================
 * XboxMovieThread::EngineIsReady
 * Author: jg
 * 
 * Tells the movie thread that the engine is done loading and is ready to take
 * over. If wait for complete is specified, this function will block until the
 * movie thread is done playing the movie.
 *
 * input:	bWaitForComplete Whether the call should block until the movie is
 *			done or not
 *
 * ============================================================================
 */
void XboxMovieThread::EngineIsReady(UBOOL bWaitForComplete)
{
	guard(XboxMovieThread::EngineIsReady);
	check(hEngineReadyEvent);
	// Tell the thread the engine is ready
	SetEvent(hEngineReadyEvent);
	if (bWaitForComplete == 1)
	{
		// Block until the thread exits
		WaitForSingleObject(hThread,INFINITE);
	}
	unguard;
}

/* scion ======================================================================
 * XboxMovieThread::ThreadProc
 * Author: jg
 * 
 * Called by the OS when the thread is created. Just passes the call the "this"
 * aware thread method.
 *
 * input:	pThis The "this" pointer for our class instance
 *
 * ============================================================================
 */
DWORD XboxMovieThread::ThreadProc(LPVOID pThis)
{
	guard(XboxMovieThread::ThreadProc);
	check(pThis);
	// Just forward the call on to the Run() method where the real work is done
	((XboxMovieThread*)pThis)->Run();
	return 0;
	unguard;
}

/* scion ======================================================================
 * XboxMovieThread::Exit
 * Author: jg
 * 
 * Cleans up any allocated resources.
 *
 * ============================================================================
 */
void XboxMovieThread::Exit(void)
{
	guard(XboxMovieThread::Exit);
	// Don't call exit from the same thread
	check(GetCurrentThreadId() != ThreadID);
	// Check to see if the the thread is still running
	if (hThread != NULL && WaitForSingleObject(hThread,1) == WAIT_TIMEOUT)
	{
		check(hDieEvent);
		// Tell it to die no matter what
		SetEvent(hDieEvent);
		// Wait for a nice painless death
		WaitForSingleObject(hThread,INFINITE);
	}
	// Tell the movie player to let go of resources
	MoviePlayer.Destroy();
	// Clean up all handles
	if (hThread != NULL)
	{
		CloseHandle(hThread);
		hThread = NULL;
	}
	if (hDieEvent != NULL)
	{
		CloseHandle(hDieEvent);
		hDieEvent = NULL;
	}
	if (hEngineReadyEvent != NULL)
	{
		CloseHandle(hEngineReadyEvent);
		hEngineReadyEvent = NULL;
	}
	// Clean up the load to memory movie if valid
	if (bLoadFromRam == TRUE && GLoadToMemThread != NULL)
	{
		delete GLoadToMemThread;
		GLoadToMemThread = NULL;
	}
	unguard;
}

/* scion ======================================================================
 * XboxMovieThread::StartFromMemory
 * Author: jg
 * 
 * Initializes the movie decoder, creates the events and kicks off the
 * background thread. Returns true if everything worked ok
 *
 * input:	Loading The string to show while loading
 *			bLooping Whether the movie should loop or not
 *			pDevice The direct3d device to draw with
 *
 * ============================================================================
 */
UBOOL XboxMovieThread::StartFromMemory(const TCHAR* Loading,UBOOL bLooping,
	LPDIRECT3DDEVICE8 pDev)
{
	guard(XboxMovieThread::StartFromMemory);
	// Store all the passed in values
	pDevice = pDev;
	bInterruptable = TRUE;
	bLoopMovie = bLooping;
	LoadingMsg = appStrupr((TCHAR*)Loading);
	bLoadFromRam = TRUE;
	bIsCreditsMovie = FALSE;
	// Assume we fail
	HRESULT hr = E_FAIL;
	check(GLoadToMemThread);
	debugf(TEXT("Playing movie from RAM"));
	// Open the movie from the memory buffer
	hr = MoviePlayer.OpenMovieFromMemory(D3DFMT_LIN_A8R8G8B8,pDev,1,1,
		GLoadToMemThread->GetMovieData(),GLoadToMemThread->GetMovieDataLength(),
		bLooping);
	if (SUCCEEDED(hr))
	{
		// Kick off the background thread that will render the movie
		hr = StartThread();
	}
	else
	{
		debugf(TEXT("Failed to create movie player for in-memory movie"));
	}
	// Clean up if failed
	if (FAILED(hr))
	{
		Exit();
	}
	return SUCCEEDED(hr);
	unguard;
}

/* scion ======================================================================
 * XboxMovieThread::StartThread
 * Author: jg
 * 
 * Starts the background thread that will render the movie
 *
 * ============================================================================
 */
HRESULT XboxMovieThread::StartThread(void)
{
	guard(XboxMovieThread::Start);
	HRESULT hr = E_FAIL;
	// Create the events. Make sure they are manual reset events
	hDieEvent = CreateEvent(NULL,TRUE,FALSE,NULL);
	if (hDieEvent != NULL)
	{
		hEngineReadyEvent = CreateEvent(NULL,TRUE,FALSE,NULL);
		if (hEngineReadyEvent != NULL)
		{
			// Kick off the thread
			hThread = CreateThread(NULL,12 * 1024,ThreadProc,this,0,&ThreadID);
			if (hThread != NULL)
			{
				// Bump up the thread priority just a touch
				SetThreadPriority(hThread,THREAD_PRIORITY_ABOVE_NORMAL);
				// Change the update interval if it has no audio
				if (MoviePlayer.IsAudioEnabled() == FALSE)
				{
					UpdateFrequency = 28;
				}
				hr = S_OK;
			}
		}
		else
		{
			GLog->Log(TEXT("Failed to create ready event for movie thread"));
		}
	}
	else
	{
		GLog->Log(TEXT("Failed to create death event for movie thread"));
	}
	return hr;
	unguard;
}

/* scion ======================================================================
 * XboxMovieThread::Start
 * Author: jg
 * 
 * Initializes the movie decoder, creates the events and kicks off the
 * background thread. Returns true if everything worked ok
 *
 * input:	MovieFile The name of the file to open
 *			Loading The string to show while loading
 *			dwMaxProgress The total amount is to be loaded
 *			bLooping Whether the movie should loop or not
 *			pDevice The direct3d device to draw with
 *			bInterrupt Whether playback can be interrupted or not
 *
 * ============================================================================
 */
UBOOL XboxMovieThread::Start(const FString& MovieFile,const TCHAR* Loading,
	UBOOL bLooping,LPDIRECT3DDEVICE8 pDev,UBOOL bInterrupt)
{
	guard(XboxMovieThread::Start);
	// Store all the passed in values
	pDevice = pDev;
	bInterruptable = bInterrupt;
	bLoopMovie = bLooping;
	LoadingMsg = appStrupr((TCHAR*)Loading);
	bIsCreditsMovie = appStrstr(*MovieFile,TEXT("UC2_Credits")) != NULL;
	// Assume we fail
	HRESULT hr = E_FAIL;
	debugf(TEXT("Playing movie '%s'"),*MovieFile);
	// Open the specified file name and get the decoder primed
	hr = MoviePlayer.OpenFile(MovieFile,D3DFMT_LIN_A8R8G8B8,pDev,1,1,
		bLooping);
	if (SUCCEEDED(hr))
	{
		// Kick off the background thread that will render the movie
		hr = StartThread();
	}
	else
	{
		debugf(TEXT("Failed to create movie player for file %s"),*MovieFile);
	}
	// Clean up if failed
	if (FAILED(hr))
	{
		Exit();
	}
	return SUCCEEDED(hr);
	unguard;
}

/* scion ======================================================================
 * XboxMovieThread::Run
 * Author: jg
 * 
 * Continually updates the movie, renders the progress, and checks for any
 * cancel events. If the engine is ready to take over, this code also checks
 * for gamepad activity to abort the playback.
 *
 * ============================================================================
 */
void XboxMovieThread::Run(void)
{
	guard(XboxMovieThread::Run);
	DWORD dwWait = WAIT_TIMEOUT;
	// Wait until the engine tells us we can start checking for interruption
	UBOOL bCheckForInterrupt = 0;
	// The list of events to wait upon
	HANDLE arrEvents[] = { hDieEvent, hEngineReadyEvent };
	// While the death event has not been triggered
	while (dwWait > WAIT_OBJECT_0)
	{
		// Did the engine say it was ready?
		if (dwWait == (WAIT_OBJECT_0 + 1) && (bInterruptable == 1 || bLoopMovie))
		{
			bCheckForInterrupt = 1;
		}
		// Update the display for this go round
		Render();
		// Check the various event states and pause this thread but not longer
		// than it will take for us to keep rendering at 30hz
		dwWait = WaitForMultipleObjects(2,arrEvents,FALSE,UpdateFrequency);
		// If the engine is ready and the player wants to abort or the movie
		// finished playing
		if ((bCheckForInterrupt && (WasInterruptRequested() || bLoopMovie)) ||
			MoviePlayer.IsPlaying() == 0)
		{
			// Act as if the die event was triggered
			dwWait = WAIT_OBJECT_0;
		}
	}
	unguard;
}

/* scion ======================================================================
 * XboxMovieThread::Render
 * Author: jg
 * 
 * Renders the movie scene and renders the progress update indicator.
 *
 * ============================================================================
 */
void XboxMovieThread::Render(void)
{
	guard(XboxMovieThread::Render);
	check(pDevice);
	// Clear the backbuffer, depth, & stencil
    pDevice->Clear(0,NULL,
		D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL,0x00000000,1.f,0L);
	// Update the movie
	RenderMovie();
#ifndef FINAL_RELEASE
	// Show the safezone if requested
	if (GShowMovieSafezone)
	{
		RenderSafeZone();
	}
#endif
	extern UBOOL GShowSubtitles;
	if (LoadingMsg.Len() > 0)
	{
		// Render the map name to the screen
		RenderMapName(*LoadingMsg);
	}
	// Hide the subtitles if set
	else if (GShowSubtitles == FALSE && bIsCreditsMovie == FALSE)
	{
		HideSubtitles();
	}
	// Present the changes
	pDevice->Present(NULL,NULL,NULL,NULL);
	unguard;
}

/* scion ======================================================================
 * XboxMovieThread::RenderMovie
 * Author: jg
 * 
 * Renders the movie scene
 *
 * ============================================================================
 */
void XboxMovieThread::RenderMovie(void)
{
	guard(XboxMovieThread::RenderMovie);
	// Get the current frame of the movie
	DWORD XmvResult = 0;
	// Get the next frame of the movie
	LPDIRECT3DTEXTURE8 pCurrentFrame = MoviePlayer.AdvanceFrameForTexturing(
			pDevice,XmvResult);
	if (pCurrentFrame != NULL)
	{
		RenderFullScreenQuad(pCurrentFrame);
	}
	unguard;
}

/**
 * Copies the movie name to load
 */
FLoadToMemThread::FLoadToMemThread(const TCHAR* InMovieName)
{
	guard(FLoadToMemThread::FLoadToMemThread);
	MovieName = InMovieName;
	LoadingCompleteEvent = NULL;
	Thread = NULL;
	MovieData = NULL;
	Reader = NULL;
	AmountToRead = 0;
	unguard;
}

/**
 * Releases any allocated resources (not the movie buffer though)
 */
FLoadToMemThread::~FLoadToMemThread(void)
{
	guard(FLoadToMemThread::~FLoadToMemThread);
	// Clean up the thread if needed
	if (Thread != NULL)
	{
		Thread->Kill(1);
		GThreadFactory->Destroy(Thread);
		Thread = NULL;
	}
	// Clean up the synchronization object
	if (LoadingCompleteEvent != NULL)
	{
		GSynchronizeFactory->Destroy(LoadingCompleteEvent);
		LoadingCompleteEvent = NULL;
	}
	// This buffer is freed by the movie codec
	MovieData = NULL;
	unguard;
}

/**
 * Creates the event, buffer, and thread needed to stream the movie in.
 *
 * @return True if it created ok, false otherwise
 */
UBOOL FLoadToMemThread::StartBackgroundLoad(void)
{
	guard(FLoadToMemThread::StartBackgroundLoad);
	// Now create the thread that will do the work
	Thread = GThreadFactory->CreateThread(this,0,0,14 * 1024,
		TPri_BelowNormal);
	return Thread != NULL;
	unguard;
}

/**
 * Opens the file, allocates the buffer, creates the needed events
 *
 * @return True if initialization was successful, false otherwise
 */
UBOOL FLoadToMemThread::Init(void)
{
	guard(FLoadToMemThread::Init);
	// Create the event that is triggered when loading is complete
	LoadingCompleteEvent = GSynchronizeFactory->CreateSynchEvent(TRUE);
	if (LoadingCompleteEvent != NULL)
	{
		// Create the file reader that we will do our background loading from
		Reader = GFileManager->CreateLinearReader(*MovieName);
		if (Reader != NULL)
		{
			AmountToRead = Reader->TotalSize();
			// Finally, allocate the buffer to hold the data
			MovieData = new BYTE[AmountToRead];
		}
		else
		{
			debugf(TEXT("Failed load movie %s"),*MovieName);
		}
	}
	else
	{
		debugf(TEXT("Failed to create event %d"),GetLastError());
	}
	return LoadingCompleteEvent != NULL && Reader != NULL && MovieData != NULL;
	unguard;
}

/**
 * Waits for work to be put on the queue.
 *
 * @return The exit code of the runnable object
 */
DWORD FLoadToMemThread::Run(void)
{
	guard(FLoadToMemThread::Run);
	// Read the block in. The linear reader will handle all blocking
	Reader->Serialize(MovieData,AmountToRead);
	// Trigger the event so any other thread knows we are done loading it
	LoadingCompleteEvent->Trigger();
	return 0;
	unguard;
}

/**
 * Cleans up any resources that were temporary for the thread
 */
void FLoadToMemThread::Exit(void)
{
	guard(FLoadToMemThread::Exit);
	// Clean up the file reader
	if (Reader != NULL)
	{
		delete Reader;
		Reader = NULL;
	}
	unguard;
}

/**
 * Creates the loading thread and starts the background streaming process
 */
void LoadLoadingMovie(void)
{
	guard(LoadLoadingMovie);
	// Only stream it in once
	if (GLoadToMemThread == NULL)
	{
		// Create our global streaming object
		GLoadToMemThread = NEW FLoadToMemThread(
			TEXT("..\\Textures\\Movies\\UI_LoadingMovie.xmv"));
		if (GLoadToMemThread != NULL)
		{
			// Let the thread do its thing
			if (GLoadToMemThread->StartBackgroundLoad() == FALSE)
			{
				debugf(TEXT("StartBackgroundLoad() failed"));
				delete GLoadToMemThread;
			}
		}
		else
		{
			debugf(TEXT("Failed to create FLoadToMemThread()"));
		}
	}
	else
	{
		debugf(TEXT("Called LoadLoadingMovie"));
	}
	unguard;
}

/**
 * Checks to see if the background thread has loaded the movie or not
 */
UBOOL IsLoadingMovieReady(void)
{
	guard(IsLoadingMovieReady);
	UBOOL bDone = FALSE;
	// Say we're done if there is no background loader
	if (GLoadToMemThread != NULL)
	{
		// See if its done
		bDone = GLoadToMemThread->IsLoaded();
	}
	else
	{
		bDone = TRUE;
	}
	return bDone;
	unguard;
}

/**
 * Constructor zeros members
 */
FInitialMovieThread::FInitialMovieThread(void)
{
	guard(FInitialMovieThread::FInitialMovieThread);
	LogoMovie = NULL;
	unguard;
}

/**
 * Creates all of the needed devices and creates the simple player
 *
 * @return True if initialization was successful, false otherwise
 */
UBOOL FInitialMovieThread::Init(void)
{
	guard(FInitialMovieThread::Init);
	// Global since the UD3DRenderDevice doesn't exist yet
	extern IDirect3DDevice8* GD3DDevice;
	// Load the first movie into RAM
	LogoMovie = new FLoadToMemThread(TEXT("..\\Movies\\UC2_Logos.xmv"));
	// Load all of the data for the movie into the memory buffer
	if (LogoMovie->Init() == TRUE)
	{
		LogoMovie->Run();
		LogoMovie->Exit();
	}
	// Load the second movie into RAM
	LoadLoadingMovie();
	// Give the other thread a chance to start up
	appSleep(0.01f);
	// Now open it for playback
	HRESULT hr = MoviePlayer.OpenMovieFromMemory(D3DFMT_LIN_A8R8G8B8,
		GD3DDevice,TRUE,TRUE,LogoMovie->GetMovieData(),
		LogoMovie->GetMovieDataLength(),FALSE);
	// This will block until fully loaded
	return LogoMovie->GetMovieData() && GLoadToMemThread->GetMovieData() &&
		SUCCEEDED(hr);
	unguard;
}

/**
 * Advances the frames of the movie. Periodically checks for input
 * from the gamepads. Also checks the file cache manager to see if
 * it is done caching files. If so and the user has pressed a
 * button, then the movie playing is halted. Otherwise this thread
 * waits for the movie to complete.
 *
 * @return The exit code of the runnable object
 */
DWORD FInitialMovieThread::Run(void)
{
	guard(FInitialMovieThread::Run);
	// Clear and present a frame so the movie will render
	GD3DDevice->Clear(0,NULL,D3DCLEAR_TARGET,0,0.f,0);
	GD3DDevice->Swap(D3DSWAP_DEFAULT);
	UBOOL bDone = FALSE;
	UBOOL bCheckInterrupt = TRUE;
	UBOOL bDoneCaching = FALSE;
	// Keep looping until the caching is complete, the movie is done, or
	// the user has asked to skip it
	while (bDone == FALSE)
	{
		DirectSoundDoWork();
		DWORD XmvResult = 0;
		// Get the next frame of the movie
		LPDIRECT3DTEXTURE8 pCurrentFrame = MoviePlayer.AdvanceFrameForTexturing(
				GD3DDevice,XmvResult);
		// Render the next frame of the movie
		if (pCurrentFrame != NULL)
		{
			RenderFullScreenQuad(pCurrentFrame);
			GD3DDevice->Swap(D3DSWAP_DEFAULT);
		}
		// Check to see if we are done with the movie
		bDone = XmvResult == XMV_ENDOFFILE;
		// Check to see if all the critical files have been cached
		bDoneCaching = GFileCacheManager->IsCachingFiles() == FALSE;
		if (bDoneCaching == TRUE)
		{
			// Now see if the user wanted it skipped
			if (bCheckInterrupt && WasInterruptRequested())
			{
				bCheckInterrupt = FALSE;
				bDone = TRUE;
			}
		}
		// Ghetto, but I don't care
		appSleep(0.01f);
	}
	return 0;
	unguard;
}

/**
 * Cleans up any resources that were temporary for the thread
 */
void FInitialMovieThread::Exit(void)
{
	guard(FInitialMovieThread::Exit);
	// Tell the movie player to let go of resources
	MoviePlayer.Destroy();
	delete LogoMovie;
	LogoMovie = NULL;
	unguard;
}

