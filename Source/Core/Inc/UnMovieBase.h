/*=============================================================================
	UnMovieBase.h: the movie base classes every platform shares.

	UC2 declared these two classes in UnVcWin32.h (lines 792-806 of Epic's file), the Windows/Xbox platform
	header, so only the MSVC builds had them; the GCC port carried a copy in UnGnuG.h. They are moved here
	unchanged (M9) so every platform header includes one definition. The Xbox player (XboxDrv's CXMVPlayer,
	XboxMovieThread) and the portable one (Engine/Inc/UnMovie.h, Source/UC2Movie) derive from them.
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
