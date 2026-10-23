/*=============================================================================
	SDLLaunchWeb.cpp: what the launcher needs only in the browser.

	Compiled only under Emscripten (added by Source/SDLLaunch/cmake/Web.cmake). SDLLaunch.cpp calls
	UC2WebSetEngine() once the engine exists and UC2WebYieldFrame() once per frame, both inside
	#ifdef __EMSCRIPTEN__ blocks.
=============================================================================*/

// Before the engine headers: emscripten.h pulls in <time.h>, and UnFile.h #defines clock().
#include <emscripten.h>
#include <emscripten/html5.h>
#if UC2_WITH_UC2ALAUDIO
#include <AL/al.h>	// openal-soft, linked into the module through UC2ALAudio (Web.cmake adds its include path)
#endif

#include "SDLLaunchPrivate.h"
#include "SDLLaunchWeb.h"

// The engine for ue_exec_command (set after InitEngine, cleared before shutdown).
static UEngine* GUC2WebEngine = NULL;

//
// Whether the page is hidden, for the frame wait below (the worker has no document of its own).
//
EM_JS( void, uc2_web_set_hidden, (int Hidden), {
	globalThis.uc2WebHidden = !!Hidden;
});

//
// The tab hidden: the engine keeps ticking (from timers: a hidden page has no display frames), only the sound
// is silenced. The listener gain is the master volume of the current OpenAL context, which UC2ALAudio never
// sets (it sets each source's gain), so this does not fight the game's own volumes. Registered from the engine's
// worker, so the callback runs on that thread, between frames (at the frame wait).
//
static EM_BOOL UC2WebVisibilityChange( int EventType, const EmscriptenVisibilityChangeEvent* Event, void* UserData )
{
	uc2_web_set_hidden( Event->hidden );
#if UC2_WITH_UC2ALAUDIO
	alListenerf( AL_GAIN, Event->hidden ? 0.0f : 1.0f );
#endif
	return EM_TRUE;
}

void UC2WebSetEngine( UEngine* Engine )
{
	GUC2WebEngine = Engine;
	static UBOOL Registered = 0;
	if( Engine && !Registered )
	{
		Registered = 1;
		EmscriptenVisibilityChangeEvent Status;
		if( emscripten_get_visibility_status( &Status ) == EMSCRIPTEN_RESULT_SUCCESS )
			uc2_web_set_hidden( Status.hidden );
		emscripten_set_visibilitychange_callback( NULL, EM_TRUE, UC2WebVisibilityChange );
	}
}

//
// Yield the engine thread to its event loop until the next frame may start.
//
// main() runs on a worker (-sPROXY_TO_PTHREAD) in a blocking loop. Everything else that must reach that
// thread arrives as an event-loop task: SDL's input events (registered with the calling thread as the
// callback thread), the host's console commands (postMessage, vendor/glue/wasmfs-shim.js). Asyncify unwinds
// the engine stack here and resumes it at the display's next frame (the worker's requestAnimationFrame), or
// at the first display frame that reaches WaitMs from now: frames that start on the display's frames take
// the browser's mouse motion (one coalesced event a display frame) one batch a frame, and are shown at an
// even pace. Where the worker has no requestAnimationFrame, while the page is hidden and with -TIMERFRAMES, a
// timer resumes it instead (WaitMs, or 0 ms); a display frame that does not come within 100 ms (the page
// hidden meanwhile) gives way to a timer too.
// EM_ASYNC_JS registers itself as an Asyncify import (__asyncjs__*), no ASYNCIFY_IMPORTS entry needed.
//
EM_ASYNC_JS( void, uc2_web_wait_frame, (int OnDisplayFrame, double WaitMs), {
	const end = performance.now() + Math.max(0, WaitMs);
	for (;;) {
		if (!OnDisplayFrame || typeof requestAnimationFrame !== 'function' || globalThis.uc2WebHidden) {
			const left = end - performance.now();
			await new Promise((resolve) => { setTimeout(resolve, Math.max(0, left)); });
			return;
		}
		await new Promise((resolve) => {
			const t = setTimeout(resolve, 100);
			requestAnimationFrame(() => { clearTimeout(t); resolve(); });
		});
		if (performance.now() >= end - 1)
			return;
	}
});

//
// -TIMERFRAMES (e.g. a benchmark): frames resume from timers alone.
//
static INT UC2WebOnDisplayFrame()
{
	static INT OnDisplayFrame = -1;
	if( OnDisplayFrame < 0 )
	{
		OnDisplayFrame = !ParseParam( appCmdLine(), TEXT("TIMERFRAMES") );
		debugf( NAME_Init, TEXT("UC2Web: frames start %s"), OnDisplayFrame ? TEXT("on the display's frames") : TEXT("from timers") );
	}
	return OnDisplayFrame;
}

void UC2WebYieldFrame()
{
	uc2_web_wait_frame( UC2WebOnDisplayFrame(), 0.0 );
}

//
// The frame-rate cap: frames start on display frames, at most one a Period on average. Each frame's due
// time is the one before plus Period, so a cap the display's rate does not divide still averages the cap
// (some display frames are skipped); a frame later than its due time moves the schedule to itself.
//
void UC2WebWaitCapped( DOUBLE Period )
{
	static DOUBLE Due = 0.0;
	const DOUBLE Now = appSeconds();
	uc2_web_wait_frame( UC2WebOnDisplayFrame(), Max( 0.0, Due - Now ) * 1000.0 );
	Due = Max( Due + Period, appSeconds() );
}

//
// Frozen ABI export (vendor/glue/ue_abi.h): run a console command and return its captured output.
// TCHAR is ANSI on every target here, so the command and the result pass through unchanged. The result
// stays valid until the next call; an empty string (never NULL) before the engine exists.
//
extern "C" EMSCRIPTEN_KEEPALIVE char* ue_exec_command( const char* Cmd )
{
	static FString Captured;
	Captured = TEXT("");
	if( GUC2WebEngine && Cmd )
	{
		FStringOutputDevice Out;
		// What the engine itself does not take goes to the first viewport, as a line typed
		// on the console would (the render device's commands, the player's exec functions).
		if( !GUC2WebEngine->Exec( Cmd, Out ) && GUC2WebEngine->Client && GUC2WebEngine->Client->Viewports.Num() )
			GUC2WebEngine->Client->Viewports(0)->Exec( Cmd, Out );
		Captured = Out;
	}
	return (char*)*Captured;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
