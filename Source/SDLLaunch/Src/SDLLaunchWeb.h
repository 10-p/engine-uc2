/*=============================================================================
	SDLLaunchWeb.h: browser-only launcher hooks (SDLLaunchWeb.cpp).
=============================================================================*/

#ifdef __EMSCRIPTEN__
class UEngine;
void UC2WebSetEngine( UEngine* Engine );	// NULL before shutdown
void UC2WebYieldFrame();					// once per frame, from the main loop
#endif
