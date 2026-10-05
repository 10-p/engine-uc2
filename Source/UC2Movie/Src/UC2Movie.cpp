/*=============================================================================
	UC2Movie.cpp: package and player factory.
=============================================================================*/

#include "UC2MoviePrivate.h"

IMPLEMENT_PACKAGE(UC2Movie);
IMPLEMENT_CLASS(UUC2MovieSelfTestCommandlet);

FPortableMoviePlayer* UC2CreateMoviePlayer()
{
	return new FUC2MoviePlayer;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
