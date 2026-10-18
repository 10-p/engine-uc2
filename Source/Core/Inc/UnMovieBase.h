/*=============================================================================
	UnMovieBase.h: the movie base classes every platform shares.

	Epic declared these two classes in UnVcWin32.h, the Windows/Xbox platform header; here they are, unchanged,
	the one definition every platform header includes. The Xbox player (XboxDrv's CXMVPlayer, XboxMovieThread)
	and the portable one (Engine/Inc/UnMovie.h, Source/UC2Movie) derive from them.
=============================================================================*/

#ifndef _INC_UNMOVIEBASE
#define _INC_UNMOVIEBASE

//scion jg -- Base class of movie thread for correct delete operation
class FMovieThread
{
public:
	// Must be defined by child class
	virtual void Exit(void) = 0;
};

//scion jg -- Base class for the CXMVPlayer so we can delete it correctly
class FMoviePlayer
{
public:
	FMoviePlayer() {}
	virtual ~FMoviePlayer() {}
};

#endif // _INC_UNMOVIEBASE

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
