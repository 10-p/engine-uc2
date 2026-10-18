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
// The tab hidden: the engine keeps ticking, only the sound is silenced. The listener gain is the master volume
// of the current OpenAL context, which UC2ALAudio never
// sets (it sets each source's gain), so this does not fight the game's own volumes. Registered from the engine's
// worker, so the callback runs on that thread, between frames (at the yield).
//
static EM_BOOL UC2WebVisibilityChange( int EventType, const EmscriptenVisibilityChangeEvent* Event, void* UserData )
{
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
		emscripten_set_visibilitychange_callback( NULL, EM_TRUE, UC2WebVisibilityChange );
	}
}

//
// Yield the engine thread to its event loop once per frame.
//
// main() runs on a worker (-sPROXY_TO_PTHREAD) in a blocking loop. Everything else that must reach that
// thread arrives as an event-loop task: SDL's input events (registered with the calling thread as the
// callback thread), the host's console commands (postMessage, vendor/glue/wasmfs-shim.js), and later
// the composite of an OffscreenCanvas frame. UGameEngine::GetMaxTickRate() is 0 for a standalone client,
// so the launcher's limiter never sleeps and nothing would ever be delivered. Asyncify unwinds the engine
// stack here and resumes it from a 0 ms timer: a timer task, unlike a MessagePort task, also gives the
// compositor a rendering opportunity between frames.
// EM_ASYNC_JS registers itself as an Asyncify import (__asyncjs__*), no ASYNCIFY_IMPORTS entry needed.
//
EM_ASYNC_JS( void, uc2_web_yield_frame, (void), {
	await new Promise((resolve) => { setTimeout(resolve, 0); });
});

void UC2WebYieldFrame()
{
	uc2_web_yield_frame();
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
