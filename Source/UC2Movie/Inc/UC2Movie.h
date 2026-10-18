/*=============================================================================
	UC2Movie.h: the movie player package.

	FUC2MoviePlayer (Src/UC2MoviePrivate.h) plays the cooked Ogg Theora/Vorbis movies through the engine's
	portable movie interface (Engine/Inc/UnMovie.h). Registering the package sets GCreateMoviePlayer, which
	switches movies on; nothing of Ogg, Theora or Vorbis is visible here.
=============================================================================*/

#ifndef _INC_UC2MOVIE
#define _INC_UC2MOVIE

#include "Engine.h"
#include "UnMovie.h"

FPortableMoviePlayer* UC2CreateMoviePlayer();

/*-----------------------------------------------------------------------------
	UUC2MovieSelfTestCommandlet: `ucc-bin UC2Movie.UC2MovieSelfTest REF=<dir>` decodes cooked movies with the
	player and compares every frame, every sound sample, the language choice, the loop wrap and the A/V clock
	with references written by ffmpeg (tools/dev/movie-selftest.sh). One PASS/FAIL line per check.
-----------------------------------------------------------------------------*/

class UUC2MovieSelfTestCommandlet : public UCommandlet
{
	DECLARE_CLASS(UUC2MovieSelfTestCommandlet,UCommandlet,CLASS_Transient,UC2Movie)
	void StaticConstructor();
	INT Main( const TCHAR* Parms );
};

#define AUTO_INITIALIZE_REGISTRANTS_UC2MOVIE \
	UUC2MovieSelfTestCommandlet::StaticClass(); \
	GCreateMoviePlayer = UC2CreateMoviePlayer;

#endif // _INC_UC2MOVIE

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
