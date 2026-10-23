/*=============================================================================
	SDLLaunchWeb.h: browser-only launcher hooks (SDLLaunchWeb.cpp).
=============================================================================*/

#ifdef __EMSCRIPTEN__
class UEngine;
void UC2WebSetEngine( UEngine* Engine );	// NULL before shutdown
void UC2WebYieldFrame();					// once per frame, from the main loop, when the limiter did not wait
void UC2WebWaitCapped( DOUBLE Period );	// the frame-rate cap's wait (Period seconds a frame)
#endif
