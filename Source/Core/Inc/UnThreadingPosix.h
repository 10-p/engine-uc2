/*=============================================================================
	UnThreadingPosix.h: pthread implementation of the UnThreadingBase.h
	interfaces (the counterpart of UnThreadingWindows.h).

	Used by every non-MSVC target: Linux, Emscripten (built
	with -pthread) and MinGW (winpthreads). The class set, factory methods and
	semantics follow UnThreadingWindows.h/.cpp one for one; where Win32 has no
	pthread equivalent (TerminateThread, SuspendThread, thread priorities,
	ideal processor) the note at the definition says what happens instead.

	Launchers install the factories exactly as with the Win32 classes:
		FSynchronizeFactoryPosix SynchFactory;   GSynchronizeFactory = &SynchFactory;
		FThreadFactoryPosix      ThreadFactory;  GThreadFactory      = &ThreadFactory;
	appInit() needs GSynchronizeFactory (UnMisc.cpp creates critical sections).
=============================================================================*/

#ifndef _UNTHREADING_POSIX_H
#define _UNTHREADING_POSIX_H

#include <pthread.h>

/**
 * A recursive mutex: a Win32 CRITICAL_SECTION may be re-entered by its owner.
 */
class CORE_API FCriticalSectionPosix : public FCriticalSection
{
	pthread_mutex_t Mutex;

public:
	FCriticalSectionPosix(void);
	~FCriticalSectionPosix(void);
	virtual void Lock(void);
	virtual void Unlock(void);
};

/**
 * A Win32-style event (manual or auto reset, with PulseEvent) built on a
 * mutex and a condition variable.
 */
class CORE_API FEventPosix : public FEvent
{
	pthread_mutex_t Mutex;
	pthread_cond_t Condition;
	UBOOL bInitialized;
	UBOOL bManualReset;
	UBOOL bTriggered;
	// Bumped by Pulse() so the threads waiting at that moment are released.
	DWORD PulseCount;

public:
	FEventPosix(void);
	virtual ~FEventPosix(void);
	virtual void Lock(void);
	virtual void Unlock(void);
	virtual UBOOL Create(UBOOL bIsManualReset = FALSE,const UTCHAR* InName = NULL);
	virtual void Trigger(void);
	virtual void Reset(void);
	virtual void Pulse(void);
	virtual UBOOL Wait(DWORD WaitTime = (DWORD)-1);
};

/**
 * A Win32 mutex is recursive and, when unnamed, process local. Names are
 * ignored here (nothing in UC2 shares a mutex across processes).
 */
class CORE_API FMutexPosix : public FMutex
{
	pthread_mutex_t Mutex;
	UBOOL bInitialized;

public:
	FMutexPosix(void);
	virtual ~FMutexPosix(void);
	virtual void Lock(void);
	virtual void Unlock(void);
	virtual UBOOL Create(const UTCHAR* InName = NULL);
};

/**
 * A counting semaphore with the Win32 creation contract: initial count 0,
 * maximum InNumLocks. Lock() waits for and takes one, Unlock() releases one.
 */
class CORE_API FSemaphorePosix : public FSemaphore
{
	pthread_mutex_t Mutex;
	pthread_cond_t Condition;
	UBOOL bInitialized;
	DWORD Count;
	DWORD MaxCount;

public:
	FSemaphorePosix(void);
	virtual ~FSemaphorePosix(void);
	virtual void Lock(void);
	virtual void Unlock(void);
	virtual UBOOL Create(DWORD InNumLocks,const UTCHAR* InName = NULL);
};

class CORE_API FSynchronizeFactoryPosix : public FSynchronizeFactory
{
public:
	FSynchronizeFactoryPosix(void);
	virtual void Init(void);
	virtual FCriticalSection* CreateCriticalSection(void);
	virtual FEvent* CreateSynchEvent(UBOOL bIsManualReset = FALSE,const UTCHAR* InName = NULL);
	virtual FSemaphore* CreateSemaphore(DWORD InNumLocks,const UTCHAR* InName = NULL);
	virtual FMutex* CreateMutex(const UTCHAR* InName = NULL);
	virtual void Destroy(FSynchronize* InSynchObj);
};

/**
 * Joinable thread bookkeeping shared by the two thread classes: pthreads has
 * no timed join, so completion is signalled through a condition variable and
 * the join happens only once the thread is known to have finished.
 */
struct CORE_API FPosixThreadHandle
{
	pthread_t Thread;
	pthread_mutex_t Mutex;
	pthread_cond_t Finished;
	UBOOL bRunning;
	UBOOL bHasThread;

	FPosixThreadHandle(void);
	~FPosixThreadHandle(void);
	// pthread_create with the requested stack size (0 = default).
	UBOOL Start(void* (*Proc)(void*),void* Arg,DWORD StackSize);
	// Called by the thread itself as its last act.
	void MarkFinished(void);
	// Waits up to MaxWaitTime ms (INFINITE = forever); joins if the thread
	// finished, otherwise detaches it (there is no safe TerminateThread).
	UBOOL WaitAndRelease(DWORD MaxWaitTime);
	// Releases the handle without waiting (CloseHandle on a running thread).
	void Release(void);
	UBOOL IsCurrentThread(void);
};

class CORE_API FQueuedThreadPosix : public FQueuedThread
{
	FEvent* DoWorkEvent;
	FPosixThreadHandle* ThreadHandle;
	volatile UBOOL TimeToDie;
	FQueuedWork* QueuedWork;
	FCriticalSection* QueuedWorkSynch;
	FQueuedThreadPool* OwningThreadPool;

	static void* _ThreadProc(void* pThis);
	void Run(void);

public:
	FQueuedThreadPosix(void);
	virtual ~FQueuedThreadPosix(void);
	virtual UBOOL Create(FQueuedThreadPool* InPool,DWORD InStackSize = 0);
	virtual UBOOL Kill(UBOOL bShouldWait = FALSE,DWORD MaxWaitTime = INFINITE,UBOOL bShouldDeleteSelf = FALSE);
	virtual void DoWork(FQueuedWork* InQueuedWork);
};

class CORE_API FQueuedThreadPoolPosix : public FQueuedThreadPoolBase
{
public:
	virtual ~FQueuedThreadPoolPosix(void);
	virtual UBOOL Create(DWORD InNumQueuedThreads,DWORD StackSize = (32 * 1024));
};

class CORE_API FRunnableThreadPosix : public FRunnableThread
{
	FPosixThreadHandle* Thread;
	FRunnable* Runnable;
	UBOOL bShouldDeleteSelf;
	UBOOL bShouldDeleteRunnable;
	EThreadPriority ThreadPriority;

	static void* _ThreadProc(void* pThis);
	DWORD Run(void);

public:
	FRunnableThreadPosix(void);
	~FRunnableThreadPosix(void);
	UBOOL Create(FRunnable* InRunnable,UBOOL bAutoDeleteSelf = 0,
		UBOOL bAutoDeleteRunnable = 0,DWORD InStackSize = 0,
		EThreadPriority InThreadPri = TPri_Normal);
	virtual void SetThreadPriority(EThreadPriority NewPriority);
	virtual void SetProcessorAffinity(DWORD ProcessorNum);
	virtual void Suspend(UBOOL bShouldPause = 1);
	virtual UBOOL Kill(UBOOL bShouldWait = 0,DWORD MaxWaitTime = 0);
	virtual void WaitForCompletion(void);
};

class CORE_API FThreadFactoryPosix : public FThreadFactory
{
public:
	virtual void Init(void);
	virtual FRunnableThread* CreateThread(FRunnable* InRunnable,
		UBOOL bAutoDeleteSelf = 0,UBOOL bAutoDeleteRunnable = 0,
		DWORD InStackSize = 0,EThreadPriority InThreadPri = TPri_Normal);
	virtual void Destroy(FRunnableThread* InThread);
};

#endif
