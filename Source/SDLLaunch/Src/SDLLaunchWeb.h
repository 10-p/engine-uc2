/*=============================================================================
	SDLLaunchWeb.h: browser-only launcher hooks (M4w). See SDLLaunchWeb.cpp.
=============================================================================*/

#ifdef __EMSCRIPTEN__
class UEngine;
void UC2WebSetEngine( UEngine* Engine );	// NULL before shutdown
void UC2WebYieldFrame();					// once per frame, from the main loop
#endif
