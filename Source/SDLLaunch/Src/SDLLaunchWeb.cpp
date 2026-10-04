/*=============================================================================
	SDLLaunchWeb.cpp: what the launcher needs only in the browser (M4w).

	Compiled only under Emscripten (Source/SDLLaunch/CMakeLists.txt). SDLLaunch.cpp calls
	UC2WebSetEngine() once the engine exists and UC2WebYieldFrame() once per frame, both inside
	#ifdef __EMSCRIPTEN__ blocks. The arrangement is engine-ut2004's (its SDLLaunch.cpp, ufront 2.13.2
	and 2.43); notebook: docs/porting/analysis/web-smoke.md (W1).
=============================================================================*/

// Before the engine headers: emscripten.h pulls in <time.h>, and UnFile.h #defines clock().
#include <emscripten.h>

#include "SDLLaunchPrivate.h"
#include "SDLLaunchWeb.h"

// The engine for ue_exec_command (set after InitEngine, cleared before shutdown).
static UEngine* GUC2WebEngine = NULL;

void UC2WebSetEngine( UEngine* Engine )
{
	GUC2WebEngine = Engine;
}

//
// Yield the engine thread to its event loop once per frame.
//
// main() runs on a worker (-sPROXY_TO_PTHREAD) in a blocking loop. Everything else that must reach that
// thread arrives as an event-loop task: SDL's input events (registered with the calling thread as the
// callback thread), the host's console commands (postMessage, see vendor/glue/wasmfs-shim.js), and later
// the composite of an OffscreenCanvas frame. UGameEngine::GetMaxTickRate() is 0 for a standalone client,
// so the launcher's limiter never sleeps and nothing would ever be delivered. Asyncify unwinds the engine
// stack here and resumes it from a 0 ms timer: engine-ut2004 measured that a timer task, unlike a
// MessagePort task, also gives the compositor a rendering opportunity between frames (ufront 2.43).
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
		GUC2WebEngine->Exec( Cmd, Out );
		Captured = Out;
	}
	return (char*)*Captured;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
