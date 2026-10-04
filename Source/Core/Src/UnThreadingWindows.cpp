/**
 * UnThreadingWindows.cpp -- Contains all Windows platform specific definitions
 * of interfaces and concrete classes for multithreading support in the Unreal
 * engine.
 *
 * Copyright 2004 Epic Games, Inc. All Rights Reserved.
 *
 * Revision history:
 *		Created by Joe Graf.
 */

#include "Core.h"
// Undef the common things that both core and windows define
#undef INFINITE
#undef TEXT
#undef HANDLE
#undef HINSTANCE
#undef TCHAR

#ifdef _XBOX
#include <xtl.h>
#else
// SetThreadIdealProcessor() requires this
#define _WIN32_WINNT 0x0400
#include <windows.h>
#endif
#include "UnThreadingWindows.h"

// The global synchonization object factory
CORE_API FSynchronizeFactory* GSynchronizeFactory = NULL;
// The global thread factory
CORE_API FThreadFactory* GThreadFactory = NULL;

/**
 * Constructor that initializes the aggregated critical section
 */
FCriticalSectionWin::FCriticalSectionWin(void)
{
	guard(FCriticalSectionWin::FCriticalSectionWin);
	InitializeCriticalSection(&CriticalSection);
	unguard;
}

/**
 * Cleans up the aggregated critical section
 */
FCriticalSectionWin::~FCriticalSectionWin(void)
{
	guard(FCriticalSectionWin::~FCriticalSectionWin);
	DeleteCriticalSection(&CriticalSection);
	unguard;
}

/**
 * Locks the critical section
 */
void FCriticalSectionWin::Lock(void)
{
	guard(FCriticalSectionWin::Lock);
	EnterCriticalSection(&CriticalSection);
	unguard;
}

/**
 * Releases the lock on the critical seciton
 */
void FCriticalSectionWin::Unlock(void)
{
	guard(FCriticalSectionWin::Unlock);
	LeaveCriticalSection(&CriticalSection);
	unguard;
}

/**
 * Constructor that zeroes the handle
 */
FEventWin::FEventWin(void)
{
	guard(FEventWin::FEventWin);
	Event = NULL;
	unguard;
}

/**
 * Cleans up the event handle if valid
 */
FEventWin::~FEventWin(void)
{
	guard(FEventWin::~FEventWin);
	if (Event != NULL)
	{
		CloseHandle(Event);
	}
	unguard;
}

/**
 * Waits for the event to be signaled before returning
 */
void FEventWin::Lock(void)
{
	guard(FEventWin::Lock);
	WaitForSingleObject(Event,INFINITE);
	unguard;
}

/**
 * Triggers the event so any waiting threads are allowed access
 */
void FEventWin::Unlock(void)
{
	guard(FEventWin::Unlock);
	PulseEvent(Event);
	unguard;
}

/**
 * Creates the event. Manually reset events stay triggered until reset.
 * Named events share the same underlying event.
 *
 * @param bIsManualReset Whether the event requires manual reseting or not
 * @param InName Whether to use a commonly shared event or not. If so this
 * is the name of the event to share.
 *
 * @return Returns TRUE if the event was created, FALSE otherwise
 */
UBOOL FEventWin::Create(UBOOL bIsManualReset,const UTCHAR* InName)
{
	guard(FEventWin::Create);
	// Create the event and default it to non-signaled
	Event = CreateEvent(NULL,bIsManualReset,0,InName);
	return Event != NULL;
	unguard;
}

/**
 * Triggers the event so any waiting threads are activated
 */
void FEventWin::Trigger(void)
{
	guard(FEventWin::Trigger);
	check(Event);
	SetEvent(Event);
	unguard;
}

/**
 * Resets the event to an untriggered (waitable) state
 */
void FEventWin::Reset(void)
{
	guard(FEventWin::Reset);
	check(Event);
	ResetEvent(Event);
	unguard;
}

/**
 * Triggers the event and resets the triggered state NOTE: This behaves
 * differently for auto-reset versus manual reset events. All threads
 * are released for manual reset events and only one is for auto reset
 */
void FEventWin::Pulse(void)
{
	guard(FEventWin::Reset);
	check(Event);
	PulseEvent(Event);
	unguard;
}

/**
 * Waits for the event to be triggered
 *
 * @param WaitTime Time in milliseconds to wait before abandoning the event
 * (DWORD)-1 is treated as wait infinite
 *
 * @return TRUE if the event was signaled, FALSE if the wait timed out
 */
UBOOL FEventWin::Wait(DWORD WaitTime)
{
	guard(FEventWin::Wait);
	check(Event);
	return WaitForSingleObject(Event,WaitTime) == WAIT_OBJECT_0;
	unguard;
}

/**
 * Constructor that zeroes the handle
 */
FMutexWin::FMutexWin(void)
{
	guard(FMutexWin::FMutexWin);
	Mutex = NULL;
	unguard;
}

/**
 * Cleans up the mutex handle if valid
 */
FMutexWin::~FMutexWin(void)
{
	guard(FMutexWin::~FMutexWin);
	if (Mutex != NULL)
	{
		CloseHandle(Mutex);
	}
	unguard;
}

/**
 * Locks the mutex
 */
void FMutexWin::Lock(void)
{
	guard(FMutexWin::Lock);
	check(Mutex);
	WaitForSingleObject(Mutex,INFINITE);
	unguard;
}

/**
 * Releases the lock on the mutex
 */
void FMutexWin::Unlock(void)
{
	guard(FMutexWin::Unlock);
	check(Mutex);
	ReleaseMutex(Mutex);
	unguard;
}

/**
 * Creates the mutex. If a name is supplied, that name is used to find
 * a shared mutex instance.
 *
 * @param InName Whether to use a commonly shared mutex or not. If so this
 * is the name of the mutex to share.
 *
 * @return Returns TRUE if the mutex was created, FALSE otherwise
 */
UBOOL FMutexWin::Create(const UTCHAR* InName)
{
	guard(FMutexWin::Create);
	Mutex = CreateMutex(NULL,0,InName);
	return Mutex != NULL;
	unguard;
}

/**
 * Constructor that zeroes the handle
 */
FSemaphoreWin::FSemaphoreWin(void)
{
	guard(FSemaphoreWin::FSemaphoreWin);
	Semaphore = NULL;
	unguard;
}

/**
 * Cleans up the semaphore handle if valid
 */
FSemaphoreWin::~FSemaphoreWin(void)
{
	guard(FSemaphoreWin::~FSemaphoreWin);
	if (Semaphore != NULL)
	{
		CloseHandle(Semaphore);
	}
	unguard;
}

/**
 * Acquires a lock on the semaphore
 */
void FSemaphoreWin::Lock(void)
{
	guard(FSemaphoreWin::Lock);
	check(Semaphore);
	WaitForSingleObject(Semaphore,INFINITE);
	unguard;
}

/**
 * Releases one acquired lock back to the semaphore
 */
void FSemaphoreWin::Unlock(void)
{
	guard(FSemaphoreWin::Unlock);
	check(Semaphore);
	LONG Ignored;
	ReleaseSemaphore(Semaphore,1,&Ignored);
	unguard;
}

/**
 * Creates the semaphore with the specified number of locks. Named
 * semaphores share instances the same way events and mutexes do.
 *
 * @param InNumLocks The number of locks this semaphore will support
 * @param InName Whether to use a commonly shared semaphore or not. If so
 * this is the name of the semaphore to share.
 *
 * @return Returns TRUE if the semaphore was created, FALSE otherwise
 */
UBOOL FSemaphoreWin::Create(DWORD InNumLocks,const UTCHAR* InName)
{
	guard(FSemaphoreWin::Create);
	Semaphore = CreateSemaphore(NULL,0,InNumLocks,InName);
	return Semaphore != NULL;
	unguard;
}

/**
 * Zeroes its members
 */
FSynchronizeFactoryWin::FSynchronizeFactoryWin(void)
{
	guard(FSynchronizeFactoryWin::FSynchronizeFactoryWin);
	unguard;
}

/**
 * Initializes the factory
 */
void FSynchronizeFactoryWin::Init(void)
{
	guard(FSynchronizeFactoryWin::Init);
	unguard;
}

/**
 * Creates a new critical section
 *
 * @return The new critical section object or NULL otherwise
 */
FCriticalSection* FSynchronizeFactoryWin::CreateCriticalSection(void)
{
	guard(FSynchronizeFactoryWin::CreateCriticalSection);
	return NEW FCriticalSectionWin();
	unguard;
}

/**
 * Creates a new event
 *
 * @param bIsManualReset Whether the event requires manual reseting or not
 * @param InName Whether to use a commonly shared event or not. If so this
 * is the name of the event to share.
 *
 * @return Returns the new event object if successful, NULL otherwise
 */
FEvent* FSynchronizeFactoryWin::CreateSynchEvent(UBOOL bIsManualReset,
	const UTCHAR* InName)
{
	guard(FSynchronizeFactoryWin::CreateSynchEvent);
	// Allocate the new object
	FEvent* Event = NEW FEventWin();
	// If the internal create fails, delete the instance and return NULL
	if (Event->Create(bIsManualReset,InName) == FALSE)
	{
		delete Event;
		Event = NULL;
	}
	return Event;
	unguard;
}

/**
 * Creates a new semaphore
 *
 * @param InNumLocks The number of locks this semaphore will support
 * @param InName Whether to use a commonly shared semaphore or not. If so
 * this is the name of the semaphore to share.
 *
 * @return The new semaphore object or NULL if creation fails
 */
FSemaphore* FSynchronizeFactoryWin::CreateSemaphore(DWORD InNumLocks,const UTCHAR* InName)
{
	guard(FSynchronizeFactoryWin::CreateSemaphore);
	// Allocate the new object
	FSemaphore* Semaphore = NEW FSemaphoreWin();
	// If the internal create fails, delete the instance and return NULL
	if (Semaphore->Create(InNumLocks,InName) == FALSE)
	{
		delete Semaphore;
		Semaphore = NULL;
	}
	return Semaphore;
	unguard;
}

/**
 * Creates a new mutex
 *
 * @param InName Whether to use a commonly shared mutex or not. If so this
 * is the name of the mutex to share.
 *
 * @return The new mutex object or NULL if creation fails
 */
FMutex* FSynchronizeFactoryWin::CreateMutex(const UTCHAR* InName)
{
	guard(FSynchronizeFactoryWin::CreateMutex);
	// Allocate the new object
	FMutex* Mutex = NEW FMutexWin();
	// If the internal create fails, delete the instance and return NULL
	if (Mutex->Create(InName) == FALSE)
	{
		delete Mutex;
		Mutex = NULL;
	}
	return Mutex;
	unguard;
}

/**
 * Cleans up the specified synchronization object using the correct heap
 *
 * @param InSynchObj The synchronization object to destroy
 */
void FSynchronizeFactoryWin::Destroy(FSynchronize* InSynchObj)
{
	guard(FSynchronizeFactoryWin::Destroy);
	delete InSynchObj;
	unguard;
}

/**
 * Zeros any members
 */
FQueuedThreadWin::FQueuedThreadWin(void)
{
	guard(FQueuedThreadWin::FQueuedThreadWin);
	DoWorkEvent = NULL;
	TimeToDie = FALSE;
	QueuedWork = NULL;
	QueuedWorkSynch = NULL;
	OwningThreadPool = NULL;
	ThreadID = 0;
	ThreadHandle = NULL;
	unguard;
}

/**
 * Deletes any allocated resources. Kills the thread if it is running.
 */
FQueuedThreadWin::~FQueuedThreadWin(void)
{
	guard(FQueuedThreadWin::~FQueuedThreadWin);
	// If there is a background thread running, kill it
	if (ThreadHandle != NULL)
	{
		// Kill() will clean up the event
		Kill(TRUE);
	}
	unguard;
}

/**
 * The thread entry point. Simply forwards the call on to the right
 * thread main function
 */
DWORD STDCALL FQueuedThreadWin::_ThreadProc(LPVOID pThis)
{
	guard(FQueuedThreadWin::_ThreadProc);
	check(pThis);
	((FQueuedThreadWin*)pThis)->Run();
	return 0;
	unguard;
}

/**
 * The real thread entry point. It waits for work events to be queued. Once
 * an event is queued, it executes it and goes back to waiting.
 */
void FQueuedThreadWin::Run(void)
{
	guard(FQueuedThreadWin::Run);
	// While we are not told to die
	while (TimeToDie == FALSE)
	{
		// Wait for some work to do
		DoWorkEvent->Wait();
		{
			FScopeLock sl(QueuedWorkSynch);
			// If there is a valid job, do it otherwise check for time to exit
			if (QueuedWork != NULL)
			{
				// Tell the object to do the work
				QueuedWork->DoWork();
				// Let the object cleanup before we remove our ref to it
				QueuedWork->Dispose();
				QueuedWork = NULL;
			}
		}
		// Return ourselves to the owning pool
		OwningThreadPool->ReturnToPool(this);
	}
	unguard;
}

/**
 * Creates the thread with the specified stack size and creates the various
 * events to be able to communicate with it.
 *
 * @param InPool The thread pool interface used to place this thread
 * back into the pool of available threads when its work is done
 * @param InStackSize The size of the stack to create. 0 means use the
 * current thread's stack size
 *
 * @return True if the thread and all of its initialization was successful, false otherwise
 */
UBOOL FQueuedThreadWin::Create(FQueuedThreadPool* InPool,DWORD InStackSize)
{
	guard(FQueuedThreadWin::Create);
	check(OwningThreadPool == NULL && ThreadHandle == NULL);
	// Copy the parameters for use in the thread
	OwningThreadPool = InPool;
	// Create the work event used to notify this thread of work
	DoWorkEvent = GSynchronizeFactory->CreateSynchEvent();
	QueuedWorkSynch = GSynchronizeFactory->CreateCriticalSection();
	if (DoWorkEvent != NULL && QueuedWorkSynch != NULL)
	{
		// Create the new thread
		ThreadHandle = CreateThread(NULL,InStackSize,_ThreadProc,this,0,&ThreadID);
	}
	// If it fails, clear all the vars
	if (ThreadHandle == NULL)
	{
		OwningThreadPool = NULL;
		// Use the factory to clean up this event
		if (DoWorkEvent != NULL)
		{
			GSynchronizeFactory->Destroy(DoWorkEvent);
		}
		DoWorkEvent = NULL;
		if (QueuedWorkSynch != NULL)
		{
			// Clean up the work synch
			GSynchronizeFactory->Destroy(QueuedWorkSynch);
		}
		QueuedWorkSynch = NULL;
	}
	return ThreadHandle != NULL;
	unguard;
}

/**
 * Tells the thread to exit. If the caller needs to know when the thread
 * has exited, it should use the bShouldWait value and tell it how long
 * to wait before deciding that it is deadlocked and needs to be destroyed.
 * NOTE: having a thread forcibly destroyed can cause leaks in TLS, etc.
 *
 * @param bShouldWait If true, the call will wait for the thread to exit
 * @param MaxWaitTime The amount of time to wait before killing it. It
 * defaults to inifinite.
 * @param bShouldDeleteSelf Whether to delete ourselves upon completion
 *
 * @return True if the thread exited gracefully, false otherwise
 */
UBOOL FQueuedThreadWin::Kill(UBOOL bShouldWait,DWORD MaxWaitTime,
	UBOOL bShouldDeleteSelf)
{
	guard(FQueuedThreadWin::Create);
	UBOOL bDidExitOK = TRUE;
	// Tell the thread it needs to die
	InterlockedExchange((LPLONG)&TimeToDie,TRUE);
	// Trigger the thread so that it will come out of the wait state if
	// it isn't actively doing work
	DoWorkEvent->Trigger();
	// If waiting was specified, wait the amount of time. If that fails,
	// brute force kill that thread. Very bad as that might leak.
	if (bShouldWait == TRUE)
	{
		// If this times out, kill the thread
		if (WaitForSingleObject(ThreadHandle,MaxWaitTime) == WAIT_TIMEOUT)
		{
#ifdef _XBOX
			// Very bad, as we now have a zombie thread eating memory
			// Unfortunately there is no TerminateThread() on the Xbox
			SuspendThread(ThreadHandle);
#else
			// Kill the thread. This can leak TLS data
			TerminateThread(ThreadHandle,-1);
#endif
			bDidExitOK = FALSE;
		}
	}
	// Now clean up the thread handle so we don't leak
	CloseHandle(ThreadHandle);
	ThreadHandle = NULL;
	// Clean up the event
	GSynchronizeFactory->Destroy(DoWorkEvent);
	DoWorkEvent = NULL;
	// Clean up the work synch
	GSynchronizeFactory->Destroy(QueuedWorkSynch);
	QueuedWorkSynch = NULL;
	TimeToDie = FALSE;
	// Delete ourselves if requested
	if (bShouldDeleteSelf)
	{
		delete this;
	}
	return bDidExitOK;
	unguard;
}

/**
 * Tells the thread there is work to be done. Upon completion, the thread
 * is responsible for adding itself back into the available pool.
 *
 * @param InQueuedWork The queued work to perform
 */
void FQueuedThreadWin::DoWork(FQueuedWork* InQueuedWork)
{
	guard(FQueuedThreadWin::DoWork);
	{
		FScopeLock sl(QueuedWorkSynch);
		check(QueuedWork == NULL && "Can't do more than one task at a time");
		// Tell the thread the work to be done
		QueuedWork = InQueuedWork;
	}
	// Tell the thread to wake up and do its job
	DoWorkEvent->Trigger();
	unguard;
}

/**
 * Cleans up any threads that were allocated in the pool
 */
FQueuedThreadPoolWin::~FQueuedThreadPoolWin(void)
{
	guard(FQueuedThreadPoolWin::~FQueuedThreadPoolWin);
	if (QueuedThreads.Num() > 0)
	{
		Destroy();
	}
	unguard;
}

/**
 * Creates the thread pool with the specified number of threads
 *
 * @param InNumQueuedThreads Specifies the number of threads to use in the pool
 * @param StackSize The size of stack the threads in the pool need (32K default)
 *
 * @return Whether the pool creation was successful or not
 */
UBOOL FQueuedThreadPoolWin::Create(DWORD InNumQueuedThreads,DWORD StackSize)
{
	guard(FQueuedThreadPoolWin::Create);
	// Make sure we have synch objects
	UBOOL bWasSuccessful = CreateSynchObjects();
	if (bWasSuccessful == TRUE)
	{
		FScopeLock LockThreads(SynchThreadQueue);
		FScopeLock LockWork(SynchWorkQueue);
		// Presize the array so there is no extra memory allocated
		QueuedThreads.Empty(InNumQueuedThreads);
		// Now create each thread and add it to the array
		for (DWORD Count = 0; Count < InNumQueuedThreads && bWasSuccessful == TRUE;
			Count++)
		{
			// Create a new queued thread
			FQueuedThread* pThread = NEW FQueuedThreadWin();
			// Now create the thread and add it if ok
			if (pThread->Create(this,StackSize) == TRUE)
			{
				QueuedThreads.AddItem(pThread);
			}
			else
			{
				// Failed to fully create so clean up
				bWasSuccessful = FALSE;
				delete pThread;
			}
		}
	}
	// Destroy any created threads if the full set was not succesful
	if (bWasSuccessful == FALSE)
	{
		Destroy();
	}
	return bWasSuccessful;
	unguard;
}

/**
 * Zeroes members
 */
FRunnableThreadWin::FRunnableThreadWin(void)
{
	guard(FRunnableThreadWin::FRunnableThreadWin);
	Thread = NULL;
	Runnable = NULL;
	bShouldDeleteSelf = FALSE;
	bShouldDeleteRunnable = FALSE;
	unguard;
}

/**
 * Cleans up any resources
 */
FRunnableThreadWin::~FRunnableThreadWin(void)
{
	guard(FRunnableThreadWin::~FRunnableThreadWin);
	// Clean up our thread if it is still active
	if (Thread != NULL)
	{
		Kill(TRUE);
	}
	unguard;
}

/**
 * Creates the thread with the specified stack size and thread priority.
 *
 * @param InRunnable The runnable object to execute
 * @param bAutoDeleteSelf Whether to delete this object on exit
 * @param bAutoDeleteRunnable Whether to delete the runnable object on exit
 * @param InStackSize The size of the stack to create. 0 means use the
 * current thread's stack size
 * @param InThreadPri Tells the thread whether it needs to adjust its
 * priority or not. Defaults to normal priority
 *
 * @return True if the thread and all of its initialization was successful, false otherwise
 */
UBOOL FRunnableThreadWin::Create(FRunnable* InRunnable,
	UBOOL bAutoDeleteSelf,UBOOL bAutoDeleteRunnable,DWORD InStackSize,
	EThreadPriority InThreadPri)
{
	guard(FRunnableThreadWin::Create);
	check(InRunnable);
	Runnable = InRunnable;
	ThreadPriority = InThreadPri;
	bShouldDeleteSelf = bAutoDeleteSelf;
	bShouldDeleteRunnable = bAutoDeleteRunnable;
	DWORD ThreadID;
	// Create the new thread
	Thread = CreateThread(NULL,InStackSize,_ThreadProc,this,0,&ThreadID);
	// If it fails, clear all the vars
	if (Thread == NULL)
	{
		Runnable = NULL;
	}
	return Thread != NULL;
	unguard;
}

/**
 * Tells the thread to either pause execution or resume depending on the
 * passed in value.
 *
 * @param bShouldPause Whether to pause the thread (true) or resume (false)
 */
void FRunnableThreadWin::Suspend(UBOOL bShouldPause)
{
	guard(FRunnableThreadWin::Suspend);
	check(Thread);
	if (bShouldPause == TRUE)
	{
		SuspendThread(Thread);
	}
	else
	{
		ResumeThread(Thread);
	}
	unguard;
}

/**
 * Tells the thread to exit. If the caller needs to know when the thread
 * has exited, it should use the bShouldWait value and tell it how long
 * to wait before deciding that it is deadlocked and needs to be destroyed.
 * NOTE: having a thread forcibly destroyed can cause leaks in TLS, etc.
 *
 * @param bShouldWait If true, the call will wait for the thread to exit
 * @param MaxWaitTime The amount of time to wait before killing it.
 * Defaults to inifinite.
 *
 * @return True if the thread exited gracefull, false otherwise
 */
UBOOL FRunnableThreadWin::Kill(UBOOL bShouldWait,DWORD MaxWaitTime)
{
	guard(FRunnableThreadWin::Kill);
	check(Thread && Runnable && "Did you forget to call Create()?");
	UBOOL bDidExitOK = TRUE;
	// Let the runnable have a chance to stop without brute force killing
	Runnable->Stop();
	// If waiting was specified, wait the amount of time. If that fails,
	// brute force kill that thread. Very bad as that might leak.
	if (bShouldWait == TRUE)
	{
		// If this times out, kill the thread
		if (WaitForSingleObject(Thread,MaxWaitTime) == WAIT_TIMEOUT)
		{
#ifdef _XBOX
			// Very bad, as we now have a zombie thread eating memory
			// Unfortunately there is no TerminateThread() on the Xbox
			SuspendThread(Thread);
#else
			// Kill the thread. This can leak TLS data
			TerminateThread(Thread,-1);
#endif
			bDidExitOK = FALSE;
		}
	}
	// Now clean up the thread handle so we don't leak
	CloseHandle(Thread);
	Thread = NULL;
	// Should we delete the runnable?
	if (bShouldDeleteRunnable == TRUE)
	{
		delete Runnable;
		Runnable = NULL;
	}
	// Delete ourselves if requested
	if (bShouldDeleteSelf == TRUE)
	{
		GThreadFactory->Destroy(this);
	}
	return bDidExitOK;
	unguard;
}

/**
 * The thread entry point. Simply forwards the call on to the right
 * thread main function
 */
DWORD STDCALL FRunnableThreadWin::_ThreadProc(LPVOID pThis)
{
	guard(FRunnableThreadWin::_ThreadProc);
	check(pThis);
	return ((FRunnableThreadWin*)pThis)->Run();
	unguard;
}

/**
 * The real thread entry point. It calls the Init/Run/Exit methods on
 * the runnable object
 *
 * @return The exit code of the thread
 */
DWORD FRunnableThreadWin::Run(void)
{
	guard(FRunnableThreadWin::Run);
	// Assume we'll fail init
	DWORD ExitCode = 1;
	check(Runnable);
	// Twiddle the thread priority
	if (ThreadPriority != TPri_Normal)
	{
		SetThreadPriority(ThreadPriority);
	}
	// Initialize the runnable object
	if (Runnable->Init() == TRUE)
	{
		// Now run the task that needs to be done
		ExitCode = Runnable->Run();
		// Allow any allocated resources to be cleaned up
		Runnable->Exit();
	}
	// Clean ourselves up without waiting
	if (bShouldDeleteSelf == TRUE)
	{
		// Passing TRUE here will deadlock the thread
		Kill(FALSE);
	}
	return ExitCode;
	unguard;
}

/**
 * Changes the thread priority of the currently running thread
 *
 * @param NewPriority The thread priority to change to
 */
void FRunnableThreadWin::SetThreadPriority(EThreadPriority NewPriority)
{
	// Don't bother calling the OS if there is no need
	if (NewPriority != ThreadPriority)
	{
		ThreadPriority = NewPriority;
		// Change the priority on the thread
		::SetThreadPriority(Thread,
			ThreadPriority == TPri_AboveNormal ? THREAD_PRIORITY_ABOVE_NORMAL :
			ThreadPriority == TPri_BelowNormal ? THREAD_PRIORITY_BELOW_NORMAL :
			THREAD_PRIORITY_NORMAL);
	}
}

/**
 * Tells the OS the preferred CPU to run the thread on. This is supported by
 * PC and Xenon. The Xbox ignores this function
 *
 * @param ProcessorNum The preferred processor for executing the thread on
 */
void FRunnableThreadWin::SetProcessorAffinity(DWORD ProcessorNum)
{
#ifndef _XBOX
	SetThreadIdealProcessor(Thread,ProcessorNum);
#endif
}

/**
 * Halts the caller until this thread is has completed its work.
 */
void FRunnableThreadWin::WaitForCompletion(void)
{
	guard(FRunnableThreadWin::WaitForCompletion);
	// Block until this thread exits
	WaitForSingleObject(Thread,INFINITE);
	unguard;
}

/**
 * Initializes the factory
 */
void FThreadFactoryWin::Init(void)
{
}

/**
 * Creates the thread with the specified stack size and thread priority.
 *
 * @param InRunnable The runnable object to execute
 * @param bAutoDeleteSelf Whether to delete this object on exit
 * @param bAutoDeleteRunnable Whether to delete the runnable object on exit
 * @param InStackSize The size of the stack to create. 0 means use the
 * current thread's stack size
 * @param InThreadPri Tells the thread whether it needs to adjust its
 * priority or not. Defaults to normal priority
 *
 * @return The newly created thread or NULL if it failed
 */
FRunnableThread* FThreadFactoryWin::CreateThread(FRunnable* InRunnable,
	UBOOL bAutoDeleteSelf,UBOOL bAutoDeleteRunnable,DWORD InStackSize,
	EThreadPriority InThreadPri)
{
	check(InRunnable);
	// Create a new thread object
	FRunnableThreadWin* NewThread = NEW FRunnableThreadWin();
	if (NewThread)
	{
		// Call the thread's create method
		if (NewThread->Create(InRunnable,bAutoDeleteSelf,bAutoDeleteRunnable,
			InStackSize,InThreadPri) == FALSE)
		{
			// We failed to start the thread correctly so clean up
			Destroy(NewThread);
			NewThread = NULL;
		}
	}
	return NewThread;
}

/**
 * Cleans up the specified thread object using the correct heap
 *
 * @param InThread The thread object to destroy
 */
void FThreadFactoryWin::Destroy(FRunnableThread* InThread)
{
	delete InThread;
}
