/*=============================================================================
	UC2PresentClock.cpp: when the frames are handed to the display, for the launcher's
	game-time step.

	The render device reports each buffer swap's return and how long the swap waited
	(UC2FramePresented), and whether its swaps wait for the display's refresh (UC2SetPresentVSync);
	the launcher reads them (UC2PresentClock) to move the world on by whole refreshes under vsync
	(SDLLaunch.cpp, FVSyncStep). On desktop builds the launcher reports whether its frame cap times
	the swaps (UC2SetCapPaces); the render device reads it (UC2CapPaces) to have each frame drawn
	before the cap's wait, so that the swap at the frame's due time shows a finished frame.
=============================================================================*/

#include "EnginePrivate.h"

static DOUBLE	GPresentTime   = 0.0;	// appSeconds at the last swap's return
static DOUBLE	GPresentWaited = 0.0;	// how long that swap took, seconds
static INT		GPresentCount  = 0;		// swaps so far
static UBOOL	GPresentVSync  = 0;		// the swaps wait for the display's refresh

// The render device, when a buffer swap returned (appSeconds) after Waited seconds in the swap.
ENGINE_API void UC2FramePresented( DOUBLE When, DOUBLE Waited )
{
	GPresentTime   = When;
	GPresentWaited = Waited;
	GPresentCount++;
}

// The render device, whenever it sets its swap interval: whether the swaps now wait for the refresh.
ENGINE_API void UC2SetPresentVSync( UBOOL VSync )
{
	GPresentVSync = VSync;
}

// The last swap's return, how long it waited and the number of swaps so far; returns whether the swaps wait for the
// refresh.
ENGINE_API UBOOL UC2PresentClock( DOUBLE& Time, INT& Count, DOUBLE& Waited )
{
	Time   = GPresentTime;
	Waited = GPresentWaited;
	Count  = GPresentCount;
	return GPresentVSync;
}

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
// Desktop builds: the browser and Android's compositor take each frame whole when it is done.
static UBOOL	GCapPaces      = 0;		// the launcher's frame cap times the swaps (it waits for each frame's due time)

// The launcher, each frame: whether its frame cap waited for this frame's due time, the display not pacing the swaps.
ENGINE_API void UC2SetCapPaces( UBOOL Paces )
{
	GCapPaces = Paces;
}

// The render device: whether the launcher's frame cap times the swaps.
ENGINE_API UBOOL UC2CapPaces()
{
	return GCapPaces;
}
#endif

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
