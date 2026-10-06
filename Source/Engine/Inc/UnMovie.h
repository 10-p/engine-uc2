/*=============================================================================
	UnMovie.h: the portable movie interface (engine-uc2, M9).

	UC2's movies were Xbox-only: XboxDrv's CXMVPlayer (an FMoviePlayer) decoded XMV files with the XDK's
	XMV decoder, XboxMovieThread drew them full screen on a thread, and D3DDrv's SetMovieMaterial put their
	frames on MovieMaterial textures. On every other platform nothing played.

	This header is the platform-independent replacement of the same interface:
	  - FPortableMoviePlayer: an FMoviePlayer that decodes one movie and delivers frames as BGRA pictures,
	    clocked by its sound (or by real time when it has none). Implemented by Source/UC2Movie (cooked Ogg
	    Theora/Vorbis files, tools/cook-movies.py) and created through GCreateMoviePlayer, which stays NULL in a
	    build without a movie package (then nothing plays, as before).
	  - FMovieAudioStream: one streamed 2D sound for a movie, implemented by the audio device
	    (UAudioSubsystem::OpenMovieAudio; UC2ALAudio on OpenAL Soft).
	  - The engine-side behaviour of XboxMovieThread.cpp, written portably in UnMovie.cpp: full-screen
	    playback (start-up logos, ladder cinematics, the loading screen), name resolution from the Xbox paths
	    the content uses, and the MovieMaterial update a render device calls from SetMovieMaterial.
	WITH_PORTABLE_MOVIES is the named capability that lifts the Xbox-only movie hooks in UnGame.cpp.

	Design, threading, synchronisation and the cooked format: docs/porting/analysis/video.md.
=============================================================================*/

#ifndef _INC_UNMOVIE
#define _INC_UNMOVIE

#if !defined(_XBOX)
#define WITH_PORTABLE_MOVIES 1
#else
#define WITH_PORTABLE_MOVIES 0
#endif

/*-----------------------------------------------------------------------------
	FMovieAudioStream: a movie's sound, as one streamed 2D source of the audio device.
	Created by UAudioSubsystem::OpenMovieAudio, closed by deleting it.
-----------------------------------------------------------------------------*/

class FMovieAudioStream
{
public:
	virtual ~FMovieAudioStream() {}
	// Frames Write can take now (the device's queue is bounded).
	virtual INT FreeFrames()=0;
	// Interleaved 16-bit frames, in the channel count the stream was opened with.
	virtual void Write( const SWORD* Pcm, INT Frames )=0;
	// Frames played since the stream opened: the clock the movie's video follows. Includes the
	// position inside the buffer playing now; stops advancing when the queue runs dry.
	virtual DOUBLE PlayedFrames()=0;
	// Frames written and not yet played.
	virtual INT QueuedFrames()=0;
	virtual void Pause( UBOOL bPause )=0;
};

/*-----------------------------------------------------------------------------
	FPortableMoviePlayer.
-----------------------------------------------------------------------------*/

enum EMovieOpenFlags
{
	MOVIE_Loop				= 0x01,	// XMVFLAG_FULL_LOOP: frame 0 follows the last frame
	MOVIE_Audio				= 0x02,	// play the sound (bEnableAudio)
	MOVIE_ManualSync		= 0x04,	// XMVFLAG_UNSYNCHRONIZED_PLAYBACK: one frame per Advance, no clock
	MOVIE_ExclusiveAudio	= 0x08,	// full-screen movie: the device pauses every other sound meanwhile
};

class ENGINE_API FPortableMoviePlayer : public FMoviePlayer
{
public:
	// VideoFile: a cooked movie (Theora, with or without Vorbis). AudioFile: where to take the sound from
	// (the same file, a per-language Vorbis file, or NULL for none). Audio: the device to play it on (NULL:
	// silent, real-time clock).
	virtual UBOOL Open( const TCHAR* VideoFile, const TCHAR* AudioFile, DWORD Flags, UAudioSubsystem* Audio )=0;
	// Bring the picture up to the movie clock (or, with MOVIE_ManualSync, one frame on). Keeps the sound
	// queue filled. Returns 1 when the picture changed.
	virtual UBOOL Advance()=0;
	virtual UBOOL IsPlaying()=0;		// opened and not past its end (a looping movie never ends)
	virtual UBOOL HasEnded()=0;			// reached the end of a non-looping movie
	virtual void  Pause( UBOOL bPause )=0;
	virtual void  Rewind()=0;			// MovieMaterial bReset: start again from frame 0
	virtual INT   GetWidth()=0;
	virtual INT   GetHeight()=0;
	virtual INT   GetFrame()=0;			// index of the picture shown, -1 before the first
	virtual INT   GetFrameCount()=0;
	virtual FLOAT GetFrameRate()=0;
	virtual DOUBLE GetTime()=0;			// movie clock, seconds into the current pass
	virtual INT   GetRevision()=0;		// changes whenever the picture changes (also across loops)
	// The picture: GetWidth()*GetHeight() texels, top row first, bytes B,G,R,A (TEXF_RGBA8, alpha 255),
	// converted from the decoded Y'CbCr with the BT.601 matrix, limited ("TV") range.
	virtual const BYTE* GetBGRA()=0;
	virtual const TCHAR* GetDescription()=0;	// for the log
};

// Set by a movie package when it registers (Source/UC2Movie). NULL: no movies in this build.
ENGINE_API extern FPortableMoviePlayer* (*GCreateMoviePlayer)();

/*-----------------------------------------------------------------------------
	Engine side (UnMovie.cpp).
-----------------------------------------------------------------------------*/

// The cooked files for a movie name as the content gives it (D:\Movies\01_AscPre1.xmv,
// D:\Textures\Movies\Portrait_Char_Anubis.xmv, ..\Movies\UC2_Logos.xmv, or a bare name). AudioFile gets the
// file holding the sound in the game's language (Xbox rule: German, French, Italian, Spanish tracks, else
// the first), or stays empty when the movie has no sound. Returns 0 when there is no cooked movie.
ENGINE_API UBOOL appFindMovie( const TCHAR* Name, FString& VideoFile, FString& AudioFile, FString* LanguageNote=NULL );

// Create a player for a movie name and open it. NULL (and a log line) when the movie is missing or the build
// has no movie player.
ENGINE_API FPortableMoviePlayer* appOpenMovie( const TCHAR* Name, DWORD Flags, UAudioSubsystem* Audio );

// Can this run show full-screen movies? (A client with a viewport on a real render device; not the null
// renderer, a dedicated server or a commandlet; and not switched off.)
enum EFullscreenMovieKind { FSMOVIE_Startup, FSMOVIE_Cinematic, FSMOVIE_Loading };
ENGINE_API UBOOL appCanPlayFullscreenMovie( EFullscreenMovieKind Kind );

// Play a movie full screen in viewport 0, blocking, until it ends or (bSkippable) a key or button is
// pressed, as XboxMovieThread did. Returns 1 if it played (to the end or skipped).
ENGINE_API UBOOL appPlayMovieFullscreen( const TCHAR* Name, EFullscreenMovieKind Kind, UBOOL bSkippable );

// The MovieMaterial part of SetMovieMaterial (D3DMaterialState.cpp:4539 on the Xbox), for any render device:
// bReset, opening on first use, advancing when bNeedsFrameUpdate, OnDonePlaying, USize/VSize. Returns the
// player whose picture to show, or NULL with bOutFallback: show the material's LoadingFallback.
// While a movie has no picture (not decoded yet, or it cannot be opened at all) and the material has no
// LoadingFallback, the "player" returned is a small opaque black picture (O-1, 2026-10-06): returning NULL
// made the device fall back to Material.DefaultMaterial, which is UC2's bright green DefaultTexture (the
// Xbox did the same for a movie it could not open, but there every movie existed). Logged once per material.
ENGINE_API FPortableMoviePlayer* appUpdateMovieMaterial( class UMovieMaterial* Material, UBOOL& bOutFallback );

// A message for the player about movies being unavailable (no cooked movies at all, or no movie player in
// this build), to be shown once on the console by whoever can do so safely outside rendering (the
// full-screen player, the audio device's update). Returns 1 and the text once.
ENGINE_API UBOOL appTakeMovieNotice( FString& Out );

// Input while a full-screen movie plays: UEngine::InputEvent hands every event here first. Returns 1 when
// the event belongs to the movie (it is then not passed on).
ENGINE_API UBOOL appMovieInputEvent( EInputKey Key, EInputAction Action );

// The SUBTITLES console command of the Xbox client (XboxClient.cpp:169-180): with subtitles off, the
// full-screen player hides the burnt-in subtitle bands of the cinematics (HideSubtitles).
ENGINE_API extern UBOOL GShowSubtitles;

#endif // _INC_UNMOVIE

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
