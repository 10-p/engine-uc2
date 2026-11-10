/*=============================================================================
	UC2PresentClock.cpp: when the frames are handed to the display, for the launcher's
	game-time step.

	The render device reports each buffer swap's return and how long the swap waited
	(UC2FramePresented), and whether its swaps wait for the display's refresh (UC2SetPresentVSync);
	the launcher reads them (UC2PresentClock) to move the world on by whole refreshes under vsync
	(SDLLaunch.cpp, FVSyncStep). On desktop builds the launcher reports whether its frame cap times
	the swaps (UC2SetCapPaces); the render device reads it (UC2CapPaces) to have each frame drawn
	before the cap's wait, so that the swap at the frame's due time shows a finished frame. While the
	launcher ends the frames at the display's vertical blank it asks whether the GPU has finished the
	picture the last swap handed over (UC2WantPresentDone, UC2PresentDone); the render device answers
	with a fence after each swap (UC2PresentDoneWanted, UC2SetPresentDoneQuery).
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

// Whether the GPU has finished the picture the last swap handed over: the launcher asks for it while it ends the frames
// at the display's vertical blank, and the render device then puts a fence after each swap and sets the query.
static UBOOL	GPresentDoneWanted = 0;
static INT		(*GPresentDoneQuery)() = NULL;

// The launcher: whether it wants to know.
ENGINE_API void UC2WantPresentDone( UBOOL Want )
{
	GPresentDoneWanted = Want;
}

// The render device: whether to put the fences.
ENGINE_API UBOOL UC2PresentDoneWanted()
{
	return GPresentDoneWanted;
}

// The render device: the query, or NULL when it has none (no fence, no context).
ENGINE_API void UC2SetPresentDoneQuery( INT (*Query)() )
{
	GPresentDoneQuery = Query;
}

// The launcher: 1 when the GPU has finished the picture the last swap handed over, 0 when not yet, -1 when not known.
// It does not wait.
ENGINE_API INT UC2PresentDone()
{
	return GPresentDoneWanted && GPresentDoneQuery ? GPresentDoneQuery() : -1;
}
#endif

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
