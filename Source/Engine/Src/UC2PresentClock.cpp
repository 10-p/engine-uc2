/*=============================================================================
	UC2PresentClock.cpp: when the frames are handed to the display, for the launcher's
	game-time step.

	The render device reports each buffer swap's return (UC2FramePresented) and whether its swaps
	wait for the display's refresh (UC2SetPresentVSync); the launcher reads both (UC2PresentClock) to
	move the world on by whole refreshes under vsync (SDLLaunch.cpp, FVSyncStep).
=============================================================================*/

#include "EnginePrivate.h"

static DOUBLE	GPresentTime  = 0.0;	// appSeconds at the last swap's return
static INT		GPresentCount = 0;		// swaps so far
static UBOOL	GPresentVSync = 0;		// the swaps wait for the display's refresh

// The render device, when a buffer swap returned (appSeconds).
ENGINE_API void UC2FramePresented( DOUBLE When )
{
	GPresentTime = When;
	GPresentCount++;
}

// The render device, whenever it sets its swap interval: whether the swaps now wait for the refresh.
ENGINE_API void UC2SetPresentVSync( UBOOL VSync )
{
	GPresentVSync = VSync;
}

// The last swap's return and the number of swaps so far; returns whether the swaps wait for the refresh.
ENGINE_API UBOOL UC2PresentClock( DOUBLE& Time, INT& Count )
{
	Time  = GPresentTime;
	Count = GPresentCount;
	return GPresentVSync;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
