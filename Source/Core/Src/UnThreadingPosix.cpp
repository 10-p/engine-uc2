/*=============================================================================
	UnThreadingPosix.cpp: pthread implementation of the UnThreadingBase.h
	interfaces (the counterpart of UnThreadingWindows.cpp).

	UnThreadingPosix.h says what differs from Win32.
=============================================================================*/

#if !defined(_MSC_VER)

#include <errno.h>
#include <sys/time.h>
#include <time.h>

#include "CorePrivate.h"
#include "UnThreadingPosix.h"

/** The global synchonization object factory (set by the launcher). */
CORE_API FSynchronizeFactory* GSynchronizeFactory = NULL;
/** The global thread factory (set by the launcher). */
CORE_API FThreadFactory* GThreadFactory = NULL;

/*-----------------------------------------------------------------------------
	Helpers.
-----------------------------------------------------------------------------*/

// Absolute CLOCK_REALTIME deadline WaitTime milliseconds from now, for pthread_cond_timedwait.
static void MakeDeadline( DWORD WaitTime, struct timespec& Deadline )
{
	struct timeval Now;
	gettimeofday( &Now, NULL );
	QWORD Nsec = (QWORD)Now.tv_usec * 1000 + (QWORD)(WaitTime % 1000) * 1000000;
	Deadline.tv_sec  = Now.tv_sec + (time_t)(WaitTime / 1000) + (time_t)(Nsec / 1000000000);
	Deadline.tv_nsec = (long)(Nsec % 1000000000);
}

static void InitRecursiveMutex( pthread_mutex_t& Mutex )
{
	pthread_mutexattr_t Attr;
	pthread_mutexattr_init( &Attr );
	pthread_mutexattr_settype( &Attr, PTHREAD_MUTEX_RECURSIVE );
	verify( pthread_mutex_init( &Mutex, &Attr ) == 0 );
	pthread_mutexattr_destroy( &Attr );
}

/*-----------------------------------------------------------------------------
	FCriticalSectionPosix.
-----------------------------------------------------------------------------*/

FCriticalSectionPosix::FCriticalSectionPosix(void)
{
	guard(FCriticalSectionPosix::FCriticalSectionPosix);
	InitRecursiveMutex( Mutex );
	unguard;
}

FCriticalSectionPosix::~FCriticalSectionPosix(void)
{
	guard(FCriticalSectionPosix::~FCriticalSectionPosix);
	pthread_mutex_destroy( &Mutex );
	unguard;
}

void FCriticalSectionPosix::Lock(void)
{
	pthread_mutex_lock( &Mutex );
}

void FCriticalSectionPosix::Unlock(void)
{
	pthread_mutex_unlock( &Mutex );
}

/*-----------------------------------------------------------------------------
	FEventPosix.
-----------------------------------------------------------------------------*/

FEventPosix::FEventPosix(void)
:	bInitialized( FALSE )
,	bManualReset( FALSE )
,	bTriggered( FALSE )
,	PulseCount( 0 )
{
}

FEventPosix::~FEventPosix(void)
{
	guard(FEventPosix::~FEventPosix);
	if( bInitialized )
	{
		pthread_cond_destroy( &Condition );
		pthread_mutex_destroy( &Mutex );
	}
	unguard;
}

// As FEventWin: Lock waits for the event, Unlock pulses it.
void FEventPosix::Lock(void)
{
	Wait( INFINITE );
}

void FEventPosix::Unlock(void)
{
	Pulse();
}

UBOOL FEventPosix::Create(UBOOL bIsManualReset,const UTCHAR* InName)
{
	guard(FEventPosix::Create);
	check(!bInitialized);
	if( pthread_mutex_init( &Mutex, NULL ) != 0 )
		return FALSE;
	if( pthread_cond_init( &Condition, NULL ) != 0 )
	{
		pthread_mutex_destroy( &Mutex );
		return FALSE;
	}
	bManualReset = bIsManualReset;
	bTriggered   = FALSE;
	bInitialized = TRUE;
	return TRUE;
	unguard;
}

void FEventPosix::Trigger(void)
{
	check(bInitialized);
	pthread_mutex_lock( &Mutex );
	bTriggered = TRUE;
	// A manual-reset event releases every waiter; an auto-reset one releases one and resets.
	if( bManualReset )
		pthread_cond_broadcast( &Condition );
	else
		pthread_cond_signal( &Condition );
	pthread_mutex_unlock( &Mutex );
}

void FEventPosix::Reset(void)
{
	check(bInitialized);
	pthread_mutex_lock( &Mutex );
	bTriggered = FALSE;
	pthread_mutex_unlock( &Mutex );
}

// PulseEvent: release the threads waiting right now (all for manual reset, one for auto reset) and
// leave the event non-signalled.
void FEventPosix::Pulse(void)
{
	check(bInitialized);
	pthread_mutex_lock( &Mutex );
	PulseCount++;
	bTriggered = FALSE;
	if( bManualReset )
		pthread_cond_broadcast( &Condition );
	else
		pthread_cond_signal( &Condition );
	pthread_mutex_unlock( &Mutex );
}

UBOOL FEventPosix::Wait(DWORD WaitTime)
{
	check(bInitialized);
	struct timespec Deadline;
	if( WaitTime != INFINITE )
		MakeDeadline( WaitTime, Deadline );

	pthread_mutex_lock( &Mutex );
	const DWORD StartPulse = PulseCount;
	UBOOL bSignalled = FALSE;
	for( ;; )
	{
		if( bTriggered )
		{
			if( !bManualReset )
				bTriggered = FALSE;
			bSignalled = TRUE;
			break;
		}
		if( PulseCount != StartPulse )
		{
			bSignalled = TRUE;
			break;
		}
		if( WaitTime == INFINITE )
			pthread_cond_wait( &Condition, &Mutex );
		else if( WaitTime == 0 || pthread_cond_timedwait( &Condition, &Mutex, &Deadline ) == ETIMEDOUT )
		{
			bSignalled = bTriggered || PulseCount != StartPulse;
			if( bSignalled && bTriggered && !bManualReset )
				bTriggered = FALSE;
			break;
		}
	}
	pthread_mutex_unlock( &Mutex );
	return bSignalled;
}

/*-----------------------------------------------------------------------------
	FMutexPosix.
-----------------------------------------------------------------------------*/

FMutexPosix::FMutexPosix(void)
:	bInitialized( FALSE )
{
}

FMutexPosix::~FMutexPosix(void)
{
	guard(FMutexPosix::~FMutexPosix);
	if( bInitialized )
		pthread_mutex_destroy( &Mutex );
	unguard;
}

void FMutexPosix::Lock(void)
{
	check(bInitialized);
	pthread_mutex_lock( &Mutex );
}

void FMutexPosix::Unlock(void)
{
	check(bInitialized);
	pthread_mutex_unlock( &Mutex );
}

UBOOL FMutexPosix::Create(const UTCHAR* InName)
{
	guard(FMutexPosix::Create);
	check(!bInitialized);
	InitRecursiveMutex( Mutex );
	bInitialized = TRUE;
	return TRUE;
	unguard;
}

/*-----------------------------------------------------------------------------
	FSemaphorePosix.
-----------------------------------------------------------------------------*/

FSemaphorePosix::FSemaphorePosix(void)
:	bInitialized( FALSE )
,	Count( 0 )
,	MaxCount( 0 )
{
}

FSemaphorePosix::~FSemaphorePosix(void)
{
	guard(FSemaphorePosix::~FSemaphorePosix);
	if( bInitialized )
	{
		pthread_cond_destroy( &Condition );
		pthread_mutex_destroy( &Mutex );
	}
	unguard;
}

void FSemaphorePosix::Lock(void)
{
	check(bInitialized);
	pthread_mutex_lock( &Mutex );
	while( Count == 0 )
		pthread_cond_wait( &Condition, &Mutex );
	Count--;
	pthread_mutex_unlock( &Mutex );
}

void FSemaphorePosix::Unlock(void)
{
	check(bInitialized);
	pthread_mutex_lock( &Mutex );
	// ReleaseSemaphore fails (and changes nothing) past the maximum count.
	if( Count < MaxCount )
	{
		Count++;
		pthread_cond_signal( &Condition );
	}
	pthread_mutex_unlock( &Mutex );
}

UBOOL FSemaphorePosix::Create(DWORD InNumLocks,const UTCHAR* InName)
{
	guard(FSemaphorePosix::Create);
	check(!bInitialized);
	if( pthread_mutex_init( &Mutex, NULL ) != 0 )
		return FALSE;
	if( pthread_cond_init( &Condition, NULL ) != 0 )
	{
		pthread_mutex_destroy( &Mutex );
		return FALSE;
	}
	// CreateSemaphore(NULL,0,InNumLocks,InName): starts empty.
	Count        = 0;
	MaxCount     = InNumLocks;
	bInitialized = TRUE;
	return TRUE;
	unguard;
}

/*-----------------------------------------------------------------------------
	FSynchronizeFactoryPosix.
-----------------------------------------------------------------------------*/

FSynchronizeFactoryPosix::FSynchronizeFactoryPosix(void)
{
}

void FSynchronizeFactoryPosix::Init(void)
{
}

FCriticalSection* FSynchronizeFactoryPosix::CreateCriticalSection(void)
{
	guard(FSynchronizeFactoryPosix::CreateCriticalSection);
	return NEW FCriticalSectionPosix();
	unguard;
}

FEvent* FSynchronizeFactoryPosix::CreateSynchEvent(UBOOL bIsManualReset,const UTCHAR* InName)
{
	guard(FSynchronizeFactoryPosix::CreateSynchEvent);
	FEvent* Event = NEW FEventPosix();
	if( Event->Create(bIsManualReset,InName) == FALSE )
	{
		delete Event;
		Event = NULL;
	}
	return Event;
	unguard;
}

FSemaphore* FSynchronizeFactoryPosix::CreateSemaphore(DWORD InNumLocks,const UTCHAR* InName)
{
	guard(FSynchronizeFactoryPosix::CreateSemaphore);
	FSemaphore* Semaphore = NEW FSemaphorePosix();
	if( Semaphore->Create(InNumLocks,InName) == FALSE )
	{
		delete Semaphore;
		Semaphore = NULL;
	}
	return Semaphore;
	unguard;
}

FMutex* FSynchronizeFactoryPosix::CreateMutex(const UTCHAR* InName)
{
	guard(FSynchronizeFactoryPosix::CreateMutex);
	FMutex* Mutex = NEW FMutexPosix();
	if( Mutex->Create(InName) == FALSE )
	{
		delete Mutex;
		Mutex = NULL;
	}
	return Mutex;
	unguard;
}

void FSynchronizeFactoryPosix::Destroy(FSynchronize* InSynchObj)
{
	guard(FSynchronizeFactoryPosix::Destroy);
	delete InSynchObj;
	unguard;
}

/*-----------------------------------------------------------------------------
	FPosixThreadHandle.
-----------------------------------------------------------------------------*/

FPosixThreadHandle::FPosixThreadHandle(void)
:	bRunning( FALSE )
,	bHasThread( FALSE )
{
	pthread_mutex_init( &Mutex, NULL );
	pthread_cond_init( &Finished, NULL );
}

FPosixThreadHandle::~FPosixThreadHandle(void)
{
	pthread_cond_destroy( &Finished );
	pthread_mutex_destroy( &Mutex );
}

UBOOL FPosixThreadHandle::Start(void* (*Proc)(void*),void* Arg,DWORD StackSize)
{
	pthread_attr_t Attr;
	pthread_attr_init( &Attr );
	if( StackSize )
	{
		// pthreads refuses stacks below PTHREAD_STACK_MIN; Win32 rounds up silently.
		if( StackSize < 64 * 1024 )
			StackSize = 64 * 1024;
		pthread_attr_setstacksize( &Attr, StackSize );
	}
	bRunning = TRUE;
	const int Result = pthread_create( &Thread, &Attr, Proc, Arg );
	pthread_attr_destroy( &Attr );
	bHasThread = (Result == 0);
	if( !bHasThread )
		bRunning = FALSE;
	return bHasThread;
}

void FPosixThreadHandle::MarkFinished(void)
{
	pthread_mutex_lock( &Mutex );
	bRunning = FALSE;
	pthread_cond_broadcast( &Finished );
	pthread_mutex_unlock( &Mutex );
}

UBOOL FPosixThreadHandle::WaitAndRelease(DWORD MaxWaitTime)
{
	if( !bHasThread )
		return TRUE;
	struct timespec Deadline;
	if( MaxWaitTime != INFINITE )
		MakeDeadline( MaxWaitTime, Deadline );
	pthread_mutex_lock( &Mutex );
	while( bRunning )
	{
		if( MaxWaitTime == INFINITE )
			pthread_cond_wait( &Finished, &Mutex );
		else if( pthread_cond_timedwait( &Finished, &Mutex, &Deadline ) == ETIMEDOUT )
			break;
	}
	const UBOOL bDone = !bRunning;
	pthread_mutex_unlock( &Mutex );
	if( bDone )
		pthread_join( Thread, NULL );
	else
		pthread_detach( Thread );	// Win32 would TerminateThread here; pthreads has no safe equivalent.
	bHasThread = FALSE;
	return bDone;
}

void FPosixThreadHandle::Release(void)
{
	if( bHasThread )
		pthread_detach( Thread );
	bHasThread = FALSE;
}

UBOOL FPosixThreadHandle::IsCurrentThread(void)
{
	return bHasThread && pthread_equal( Thread, pthread_self() );
}

/*-----------------------------------------------------------------------------
	FQueuedThreadPosix.
-----------------------------------------------------------------------------*/

FQueuedThreadPosix::FQueuedThreadPosix(void)
{
	guard(FQueuedThreadPosix::FQueuedThreadPosix);
	DoWorkEvent      = NULL;
	TimeToDie        = FALSE;
	QueuedWork       = NULL;
	QueuedWorkSynch  = NULL;
	OwningThreadPool = NULL;
	ThreadHandle     = NULL;
	unguard;
}

FQueuedThreadPosix::~FQueuedThreadPosix(void)
{
	guard(FQueuedThreadPosix::~FQueuedThreadPosix);
	if( ThreadHandle != NULL )
		Kill(TRUE);
	unguard;
}

void* FQueuedThreadPosix::_ThreadProc(void* pThis)
{
	check(pThis);
	FQueuedThreadPosix* Self = (FQueuedThreadPosix*)pThis;
	// Kill() may delete the object once MarkFinished has run, so take the handle first.
	FPosixThreadHandle* Handle = Self->ThreadHandle;
	Self->Run();
	Handle->MarkFinished();
	return NULL;
}

void FQueuedThreadPosix::Run(void)
{
	while( __atomic_load_n( &TimeToDie, __ATOMIC_SEQ_CST ) == FALSE )
	{
		// Wait for some work to do.
		DoWorkEvent->Wait();
		{
			FScopeLock sl(QueuedWorkSynch);
			if( QueuedWork != NULL )
			{
				QueuedWork->DoWork();
				QueuedWork->Dispose();
				QueuedWork = NULL;
			}
		}
		if( __atomic_load_n( &TimeToDie, __ATOMIC_SEQ_CST ) == FALSE )
			OwningThreadPool->ReturnToPool(this);
	}
}

UBOOL FQueuedThreadPosix::Create(FQueuedThreadPool* InPool,DWORD InStackSize)
{
	guard(FQueuedThreadPosix::Create);
	check(OwningThreadPool == NULL && ThreadHandle == NULL);
	OwningThreadPool = InPool;
	DoWorkEvent      = GSynchronizeFactory->CreateSynchEvent();
	QueuedWorkSynch  = GSynchronizeFactory->CreateCriticalSection();
	if( DoWorkEvent != NULL && QueuedWorkSynch != NULL )
	{
		ThreadHandle = new FPosixThreadHandle;
		if( !ThreadHandle->Start( _ThreadProc, this, InStackSize ) )
		{
			delete ThreadHandle;
			ThreadHandle = NULL;
		}
	}
	if( ThreadHandle == NULL )
	{
		OwningThreadPool = NULL;
		if( DoWorkEvent != NULL )
			GSynchronizeFactory->Destroy(DoWorkEvent);
		DoWorkEvent = NULL;
		if( QueuedWorkSynch != NULL )
			GSynchronizeFactory->Destroy(QueuedWorkSynch);
		QueuedWorkSynch = NULL;
	}
	return ThreadHandle != NULL;
	unguard;
}

UBOOL FQueuedThreadPosix::Kill(UBOOL bShouldWait,DWORD MaxWaitTime,UBOOL bShouldDeleteSelf)
{
	guard(FQueuedThreadPosix::Kill);
	UBOOL bDidExitOK = TRUE;
	__atomic_store_n( &TimeToDie, TRUE, __ATOMIC_SEQ_CST );
	DoWorkEvent->Trigger();
	if( bShouldWait == TRUE )
		bDidExitOK = ThreadHandle->WaitAndRelease( MaxWaitTime );
	else
		ThreadHandle->Release();
	// A thread that did not finish still references its handle and event; leak them rather than free
	// memory a live thread uses (Win32 terminates the thread instead).
	if( bDidExitOK && bShouldWait )
	{
		delete ThreadHandle;
		GSynchronizeFactory->Destroy(DoWorkEvent);
		GSynchronizeFactory->Destroy(QueuedWorkSynch);
	}
	ThreadHandle    = NULL;
	DoWorkEvent     = NULL;
	QueuedWorkSynch = NULL;
	TimeToDie       = FALSE;
	if( bShouldDeleteSelf )
		delete this;
	return bDidExitOK;
	unguard;
}

void FQueuedThreadPosix::DoWork(FQueuedWork* InQueuedWork)
{
	guard(FQueuedThreadPosix::DoWork);
	{
		FScopeLock sl(QueuedWorkSynch);
		check(QueuedWork == NULL && "Can't do more than one task at a time");
		QueuedWork = InQueuedWork;
	}
	DoWorkEvent->Trigger();
	unguard;
}

/*-----------------------------------------------------------------------------
	FQueuedThreadPoolPosix.
-----------------------------------------------------------------------------*/

FQueuedThreadPoolPosix::~FQueuedThreadPoolPosix(void)
{
	guard(FQueuedThreadPoolPosix::~FQueuedThreadPoolPosix);
	if( QueuedThreads.Num() > 0 )
		Destroy();
	unguard;
}

UBOOL FQueuedThreadPoolPosix::Create(DWORD InNumQueuedThreads,DWORD StackSize)
{
	guard(FQueuedThreadPoolPosix::Create);
	UBOOL bWasSuccessful = CreateSynchObjects();
	if( bWasSuccessful == TRUE )
	{
		FScopeLock LockThreads(SynchThreadQueue);
		FScopeLock LockWork(SynchWorkQueue);
		QueuedThreads.Empty(InNumQueuedThreads);
		for( DWORD Count = 0; Count < InNumQueuedThreads && bWasSuccessful == TRUE; Count++ )
		{
			FQueuedThread* pThread = NEW FQueuedThreadPosix();
			if( pThread->Create(this,StackSize) == TRUE )
			{
				QueuedThreads.AddItem(pThread);
			}
			else
			{
				bWasSuccessful = FALSE;
				delete pThread;
			}
		}
	}
	if( bWasSuccessful == FALSE )
		Destroy();
	return bWasSuccessful;
	unguard;
}

/*-----------------------------------------------------------------------------
	FRunnableThreadPosix.
-----------------------------------------------------------------------------*/

FRunnableThreadPosix::FRunnableThreadPosix(void)
{
	guard(FRunnableThreadPosix::FRunnableThreadPosix);
	Thread                = NULL;
	Runnable              = NULL;
	bShouldDeleteSelf     = FALSE;
	bShouldDeleteRunnable = FALSE;
	ThreadPriority        = TPri_Normal;
	unguard;
}

FRunnableThreadPosix::~FRunnableThreadPosix(void)
{
	guard(FRunnableThreadPosix::~FRunnableThreadPosix);
	if( Thread != NULL )
	{
		// Already being destroyed: Kill must not hand us to GThreadFactory->Destroy a second time.
		bShouldDeleteSelf = FALSE;
		Kill(TRUE);
	}
	unguard;
}

UBOOL FRunnableThreadPosix::Create(FRunnable* InRunnable,UBOOL bAutoDeleteSelf,
	UBOOL bAutoDeleteRunnable,DWORD InStackSize,EThreadPriority InThreadPri)
{
	guard(FRunnableThreadPosix::Create);
	check(InRunnable);
	Runnable              = InRunnable;
	ThreadPriority        = InThreadPri;
	bShouldDeleteSelf     = bAutoDeleteSelf;
	bShouldDeleteRunnable = bAutoDeleteRunnable;
	Thread = new FPosixThreadHandle;
	if( !Thread->Start( _ThreadProc, this, InStackSize ) )
	{
		delete Thread;
		Thread   = NULL;
		Runnable = NULL;
	}
	return Thread != NULL;
	unguard;
}

// pthreads cannot suspend another thread. Nothing in UC2's portable code calls this.
void FRunnableThreadPosix::Suspend(UBOOL bShouldPause)
{
	guard(FRunnableThreadPosix::Suspend);
	check(Thread);
	debugf( NAME_Warning, TEXT("FRunnableThreadPosix::Suspend(%i): not supported, ignored"), bShouldPause );
	unguard;
}

UBOOL FRunnableThreadPosix::Kill(UBOOL bShouldWait,DWORD MaxWaitTime)
{
	guard(FRunnableThreadPosix::Kill);
	check(Thread && Runnable && "Did you forget to call Create()?");
	UBOOL bDidExitOK = TRUE;
	// Let the runnable clean itself up.
	Runnable->Stop();
	// Waiting on ourselves (a self-deleting thread at the end of Run) would deadlock.
	if( bShouldWait == TRUE && !Thread->IsCurrentThread() )
		bDidExitOK = Thread->WaitAndRelease( MaxWaitTime );
	else
		Thread->Release();
	// The handle is freed only if the thread is known to be done with it (see _ThreadProc).
	if( bDidExitOK && bShouldWait && !Thread->bHasThread && !Thread->bRunning )
		delete Thread;
	Thread = NULL;
	if( bShouldDeleteRunnable == TRUE )
	{
		delete Runnable;
		Runnable = NULL;
	}
	if( bShouldDeleteSelf == TRUE )
		GThreadFactory->Destroy(this);
	return bDidExitOK;
	unguard;
}

void* FRunnableThreadPosix::_ThreadProc(void* pThis)
{
	check(pThis);
	FRunnableThreadPosix* Self = (FRunnableThreadPosix*)pThis;
	// Run() may delete Self (bAutoDeleteSelf), so keep the handle; a self-deleting thread released
	// (detached) its handle in Kill(FALSE) and nobody else will free it, so it is freed here.
	FPosixThreadHandle* Handle = Self->Thread;
	const UBOOL bSelfDeleting = Self->bShouldDeleteSelf;
	Self->Run();
	Handle->MarkFinished();
	if( bSelfDeleting )
		delete Handle;
	return NULL;
}

DWORD FRunnableThreadPosix::Run(void)
{
	DWORD ExitCode = 1;
	check(Runnable);
	if( ThreadPriority != TPri_Normal )
		SetThreadPriority(ThreadPriority);
	if( Runnable->Init() == TRUE )
	{
		ExitCode = Runnable->Run();
		Runnable->Exit();
	}
	// Clean ourselves up without waiting
	if( bShouldDeleteSelf == TRUE )
		Kill(FALSE);
	return ExitCode;
}

// Thread priorities need privileges on Linux and do not exist on the web: recorded, not applied.
void FRunnableThreadPosix::SetThreadPriority(EThreadPriority NewPriority)
{
	ThreadPriority = NewPriority;
}

// SetThreadIdealProcessor is a hint; the scheduler decides here.
void FRunnableThreadPosix::SetProcessorAffinity(DWORD ProcessorNum)
{
}

void FRunnableThreadPosix::WaitForCompletion(void)
{
	guard(FRunnableThreadPosix::WaitForCompletion);
	check(Thread);
	pthread_mutex_lock( &Thread->Mutex );
	while( Thread->bRunning )
		pthread_cond_wait( &Thread->Finished, &Thread->Mutex );
	pthread_mutex_unlock( &Thread->Mutex );
	unguard;
}

/*-----------------------------------------------------------------------------
	FThreadFactoryPosix.
-----------------------------------------------------------------------------*/

void FThreadFactoryPosix::Init(void)
{
}

FRunnableThread* FThreadFactoryPosix::CreateThread(FRunnable* InRunnable,
	UBOOL bAutoDeleteSelf,UBOOL bAutoDeleteRunnable,DWORD InStackSize,
	EThreadPriority InThreadPri)
{
	check(InRunnable);
	FRunnableThreadPosix* NewThread = NEW FRunnableThreadPosix();
	if( NewThread )
	{
		if( NewThread->Create(InRunnable,bAutoDeleteSelf,bAutoDeleteRunnable,
			InStackSize,InThreadPri) == FALSE )
		{
			Destroy(NewThread);
			NewThread = NULL;
		}
	}
	return NewThread;
}

void FThreadFactoryPosix::Destroy(FRunnableThread* InThread)
{
	// FRunnableThread has no virtual destructor (UnThreadingBase.h), so delete through the concrete type
	// this factory created (FThreadFactoryWin deletes through the base).
	delete (FRunnableThreadPosix*)InThread;
}

#endif // !_MSC_VER
