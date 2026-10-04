/*=============================================================================
	XboxMovieThread.h:
	Copyright 2003 Scion Studios. All Rights Reserved.
=============================================================================*/

#ifndef _XBOX_MOVIE_THREAD_H
#define _XBOX_MOVIE_THREAD_H

#include "XMVHelper.h"

// Number of gamepads supported
#define GAMEPAD_COUNT 4

// Progress bar defines
#define PROGRESS_TOP (450.f)
#define PROGRESS_LEFT (40.f)
#define PROGRESS_WIDTH (560.f)
#define PROGRESS_HEIGHT (20.f)
#define PROGRESS_COLOR (D3DCOLOR_RGBA(0,200,0,128))

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * This class plays a specified movie in another thread so that the engine can
 * do other work while the user is entertained. All of the data intitialization
 * is done in the Start() routine to avoid having to synchronize access to the
 * heap. All releasing of resources are done in Exit() for the same reason. The
 * engine uses a manual reset event to tell the movie thread when it is done. At
 * that point the movie thread can skip further playback of the movie if the
 * user requests it. This must be coordinated since two threads cannot write to
 * D3D at the same time.
 *
 * ============================================================================
 */
class XboxMovieThread : public FMovieThread
{
	// The handle of the thread playing the movie
	HANDLE hThread;
	// The ID of the thread
	DWORD ThreadID;
	// The handle of the manual reset event the movie thread checks each frame
	HANDLE hEngineReadyEvent;
	// The handle of the manual reset event telling the thread to die
	HANDLE hDieEvent;
	// The movie player. Handles all decoding, etc.
	CXMVPlayer MoviePlayer;
	// Whether playback can be interrupted or not
	UBOOL bInterruptable;
	// Whether the movie is looping or not
	UBOOL bLoopMovie;
	// Whether to load from a chunk of memory or not
	UBOOL bLoadFromRam;
	// The render device
	LPDIRECT3DDEVICE8 pDevice;
	// The amount of time to wait between frame updates
	DWORD UpdateFrequency;
	// Map name for showing in the loading movie
	FString LoadingMsg;
	// Whether we are rendering the credits movie or not
	UBOOL bIsCreditsMovie;

	// This is the rendering primitive used to draw the movie
	struct FMovieStream
	{
		D3DVECTOR4 Vert;
		FLOAT U, V;
	};
	// This is the progress bar stream
	struct FProgressBarStream
	{
		D3DVECTOR4 Vert;
		D3DCOLOR Diffuse;
	};

	// Private thread proc. Simply interfaces with the Win32 API. All thread
	// activity is done in Run()
	static DWORD WINAPI ThreadProc(LPVOID pThis);
	// The real thread processing
	void Run(void);
	// Renders the movie and any "progress" update
	void Render(void);
	// Renders the current movie frame
	void RenderMovie(void);
	// Starts the background thread
	HRESULT StartThread(void);

public:
	// Construction should happen in Init()
	XboxMovieThread(void)
	{
		ThreadID = (DWORD)-1;
		UpdateFrequency = 1;
	}
	// The real clean up is in Exit()
	virtual ~XboxMovieThread(void) { Exit(); }

	// Initialization of the movie player and the thread
	UBOOL Start(const FString& MovieFile,const TCHAR* Loading,
		UBOOL bLooping,LPDIRECT3DDEVICE8 pDevice,
		UBOOL bInterrupt = 1);
	// Initialization of the movie player and the thread
	UBOOL StartFromMemory(const TCHAR* Loading,UBOOL bLooping,
		LPDIRECT3DDEVICE8 pDev);
	// Releases all the resources that were allocated
	virtual void Exit(void);

	// Tells the thread that the playback can be interrupted at any time
	void EngineIsReady(UBOOL bWaitForComplete = 1);
};

/**
 * Background thread used to stream the loading movie into a mem buffer
 */
class FLoadToMemThread : public FRunnable
{
	/**
	 * The buffer containing the movie data
	 */
	BYTE* MovieData;
	/**
	 * The event used to notify any callers when the loading is done
	 */
	FEvent* LoadingCompleteEvent;
	/**
	 * This is the thread that is handling our background streaming
	 */
	FRunnableThread* Thread;
	/**
	 * The file we are reading the data from
	 */
	FArchive* Reader;
	/**
	 * The amount of data to read from the file
	 */
	INT AmountToRead;
	/**
	 * The name of the movie to load into RAM
	 */
	FString MovieName;

	/**
	 * Can't call
	 */
	FLoadToMemThread(void);

public:
	/**
	 * Constructor zeros members
	 */
	FLoadToMemThread(const TCHAR* MovieName);

	/**
	 * Releases any allocated resources (not the movie buffer though)
	 */
	~FLoadToMemThread(void);

	/**
	 * Returns the size of the memory block
	 *
	 * @return length of the buffer in bytes
	 */
	FORCEINLINE DWORD GetMovieDataLength(void)
	{
		guard(FLoadToMemThread::GetMovieDataLength);
		return (DWORD)AmountToRead;
		unguard;
	}

	/**
	 * Returns the pointer to the buffer that contains the loading movie. If
	 * the movie is still loading it blocks until it's complete.
	 *
	 * @return The movie data pointer
	 */
	FORCEINLINE BYTE* GetMovieData(void)
	{
		guard(FLoadToMemThread::GetMovieData);
		// Return NULL if something is awry
		BYTE* pMovieData = NULL;
		// Make sure this is a valid object
		if (LoadingCompleteEvent != NULL)
		{
			// Wait for the background thread to say it's done loading
			LoadingCompleteEvent->Wait();
			check(MovieData);
			pMovieData = MovieData;
		}
		return pMovieData;
		unguard;
	}

	/**
	 * Creates the event, buffer, and thread needed to stream the movie in.
	 *
	 * @return True if it created ok, false otherwise
	 */
	UBOOL StartBackgroundLoad(void);

	/**
	 * Do a non blocking check for the event being triggered
	 */
	FORCEINLINE UBOOL IsLoaded(void)
	{
		return LoadingCompleteEvent && LoadingCompleteEvent->Wait(1);
	}

// FRunnable interface

	/**
	 * Opens the file, allocates the buffer, creates the needed events
	 *
	 * @return True if initialization was successful, false otherwise
	 */
	virtual UBOOL Init(void);

	/**
	 * Waits for work to be put on the queue.
	 *
	 * @return The exit code of the runnable object
	 */
	virtual DWORD Run(void);

	/**
	 * Ignored
	 */
	virtual void Stop(void) {}

	/**
	 * Cleans up any resources that were temporary for the thread
	 */
	virtual void Exit(void);
};

/**
 * Thread to play a movie while some items are background copied. This happens
 * before any of the main thread is executed. The main thread blocks until,
 * this thread is done. That is determined by whether enough stuff has been
 * copied over and whether the user has requested to skip the remainder.
 */
class FInitialMovieThread : public FRunnable
{
	/**
	 * The logo splash movies for Epic & Midway
	 */
	FLoadToMemThread* LogoMovie;
		/**
	 * The in memory movie player
	 */
	CXMVPlayer MoviePlayer;

public:
	/**
	 * Constructor zeros members
	 */
	FInitialMovieThread(void);

// FRunnable interface

	/**
	 * Creates all of the needed devices and creates the simple player
	 *
	 * @return True if initialization was successful, false otherwise
	 */
	virtual UBOOL Init(void);

	/**
	 * Periodically checks for input from the gamepads. Also checks the
	 * file cache manager to see if it is done caching files. If so and
	 * the user has pressed a button, then the movie playing is halted.
	 * Otherwise this thread waits for the movie to complete.
	 *
	 * @return The exit code of the runnable object
	 */
	virtual DWORD Run(void);

	/**
	 * Ignored
	 */
	virtual void Stop(void) {}

	/**
	 * Cleans up any resources that were temporary for the thread
	 */
	virtual void Exit(void);
};
#endif