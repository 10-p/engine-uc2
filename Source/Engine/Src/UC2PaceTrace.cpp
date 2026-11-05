/*=============================================================================
	UC2PaceTrace.cpp: a per-frame record of the frame pacing: when each frame started,
	the game-time step the engine ticked it with, when it was handed to the display (the
	swap) and how long the swap and the launcher's frame limiter waited. Quiet unless asked for.

	A steady frame rate that stutters is a frame whose game-time step differs from the time
	it is on the screen, so both are recorded for every frame.

	Console command (also a key binding, the SDL2 test channel's "exec ...", the web's ue_exec_command):
	  PACETRACE <frames> [tag]   record the next <frames> frames (at most 100000), then log them (0 stops it)

	The frames are kept in memory and logged together after the last one, so the recording
	writes nothing while it runs. Lines, logged under the name PaceTrace (all times ms):
	  pacetrace <tag> begin <frames> frames device=<class> vsync=<UseVSync> cap=<FrameRateCap> view=<W>x<H>
	  pacetrace <tag> <i> start=<t> dt=<d> tick=<d> swap0=<t> swap1=<t> end=<t> want=<d> slept=<d> swaps=<n>
	  pacetrace <tag> summary <n> frames <s> s <fps> fps; dt ...; step ...; swap wait ...; limiter ...; mismatch ...
	start: the frame's start, where the launcher measures DeltaTime (ms since the first recorded start);
	dt: that DeltaTime; tick: the step the engine was ticked with (dt unless a test key fixes it);
	swap0 / swap1: the call and the return of the buffer swap made in this frame (-1: none), which shows
	the previous frame's picture (UGameEngine::Draw presents the last frame before it draws the next);
	end: UEngine::Tick returned; want: the sleep the frame limiter asked for (-1: no limiter); slept: how
	long it took. Summary: dt and step (from the picture before to this frame's picture on the display:
	the next frame's swap1 minus this one's) as avg p50 p1 p99 max sd; the swap's wait; the limiter's request and its
	oversleep (slept minus want); mismatch = a frame's tick minus its step, as rms and the frames where it
	exceeds 2 ms; long steps = steps above 1.5 times the median step (a repeated picture at a steady rate).
	tools/dev/pace-analyze.py reads the lines from the log.
=============================================================================*/

#include "EnginePrivate.h"

static FName PaceTraceLogName()
{
	// RF_Native keeps a run-time name out of the purge of unreferenced names (as UC2FrameTrace.cpp).
	static FName Name;
	if( Name==NAME_None )
	{
		Name = FName( TEXT("PaceTrace") );
		Name.SetFlags( RF_Native );
	}
	return Name;
}
#define PACETRACE ((EName)PaceTraceLogName().GetIndex())

struct FPaceFrame
{
	DOUBLE	Start;			// appSeconds at the frame's start
	FLOAT	Delta;			// the DeltaTime measured there
	FLOAT	TickDelta;		// the step the engine was ticked with
	DOUBLE	Swap0, Swap1;	// the first swap's call, the last swap's return (0: none)
	DOUBLE	TickEnd;		// UEngine::Tick returned
	FLOAT	Want;			// the limiter's requested sleep (-1: none)
	FLOAT	Slept;
	INT		Swaps;
};

static TArray<FPaceFrame>	GPaceFrames;
static INT					GPaceWanted = 0;	// frames to record; 0: off
static FString				GPaceTag;
static FString				GPaceBegin;			// the begin line, written when the command arms the trace

static QSORT_RETURN CDECL ComparePaceFloat( const void* A, const void* B )
{
	const FLOAT X = *(const FLOAT*)A, Y = *(const FLOAT*)B;
	return X < Y ? -1 : X > Y ? 1 : 0;
}

// "avg a p50 b p1 c p99 d max e sd f" of the values, in ms.
static FString PaceStats( TArray<FLOAT>& V )
{
	const INT N = V.Num();
	if( N <= 0 )
		return TEXT("none");
	DOUBLE Sum = 0.0, Sq = 0.0;
	for( INT i=0; i<N; i++ )
		Sum += V(i);
	const DOUBLE Avg = Sum / N;
	for( INT i=0; i<N; i++ )
		Sq += (V(i) - Avg) * (V(i) - Avg);
	appQsort( &V(0), N, sizeof(FLOAT), ComparePaceFloat );
	return FString::Printf( TEXT("avg %.3f p50 %.3f p1 %.3f p99 %.3f max %.3f sd %.3f"),
		Avg, V(N/2), V(Min(N-1,(INT)(N*0.01f))), V(Min(N-1,(INT)(N*0.99f))), V(N-1), appSqrt( Sq / N ) );
}

static void PaceTraceDump()
{
	const INT N = GPaceFrames.Num();
	if( N <= 0 )
		return;
	const DOUBLE T0 = GPaceFrames(0).Start;
	#define PACE_MS(t) ((t) > 0.0 ? 1000.0 * ((t) - T0) : -1.0)
	debugf( PACETRACE, TEXT("%s"), *GPaceBegin );
	for( INT i=0; i<N; i++ )
	{
		const FPaceFrame& F = GPaceFrames(i);
		debugf( PACETRACE, TEXT("pacetrace %s %i start=%.3f dt=%.3f tick=%.3f swap0=%.3f swap1=%.3f end=%.3f want=%.3f slept=%.3f swaps=%i"),
			*GPaceTag, i, PACE_MS(F.Start), 1000.f * F.Delta, 1000.f * F.TickDelta, PACE_MS(F.Swap0), PACE_MS(F.Swap1),
			PACE_MS(F.TickEnd), F.Want >= 0.f ? 1000.f * F.Want : -1.f, F.Want >= 0.f ? 1000.f * F.Slept : -1.f, F.Swaps );
	}
	#undef PACE_MS

	TArray<FLOAT> Dt, Step, Wait, Want, Over, Mis;
	for( INT i=0; i<N; i++ )
	{
		const FPaceFrame& F = GPaceFrames(i);
		Dt.AddItem( 1000.f * F.TickDelta );
		if( F.Swaps )
			Wait.AddItem( 1000.f * (F.Swap1 - F.Swap0) );
		if( F.Want >= 0.f )
		{
			Want.AddItem( 1000.f * F.Want );
			Over.AddItem( 1000.f * (F.Slept - F.Want) );
		}
		// This frame's picture goes out with the next frame's swap.
		if( i > 0 && i+1 < N && F.Swaps && GPaceFrames(i+1).Swaps )
		{
			const FLOAT S = 1000.f * (GPaceFrames(i+1).Swap1 - F.Swap1);
			Step.AddItem( S );
			Mis.AddItem( 1000.f * F.TickDelta - S );
		}
	}
	DOUBLE MisSq = 0.0;
	INT MisOver = 0;
	for( INT i=0; i<Mis.Num(); i++ )
	{
		MisSq += Mis(i) * Mis(i);
		MisOver += Abs( Mis(i) ) > 2.f;
	}
	// Long steps against the median step (sorted by PaceStats).
	const FString StepStats = PaceStats( Step );
	const FLOAT Median = Step.Num() ? Step(Step.Num()/2) : 0.f;
	INT Long = 0;
	for( INT i=0; i<Step.Num(); i++ )
		Long += Step(i) > 1.5f * Median;
	const DOUBLE Secs = GPaceFrames(N-1).Start - T0 + GPaceFrames(N-1).Delta;
	debugf( PACETRACE, TEXT("pacetrace %s summary %i frames %.2f s %.1f fps; dt ms %s; step ms %s; swap wait ms %s; limiter want ms %s, overslept ms %s; mismatch rms %.3f ms, over 2 ms in %i of %i; long steps %i"),
		*GPaceTag, N, Secs, Secs > 0.0 ? N / Secs : 0.0, *PaceStats( Dt ), *StepStats, *PaceStats( Wait ),
		*PaceStats( Want ), *PaceStats( Over ), Mis.Num() ? appSqrt( MisSq / Mis.Num() ) : 0.0, MisOver, Mis.Num(), Long );
}

// The launcher, at the start of every frame: Now is where DeltaTime was measured.
ENGINE_API void UC2PaceFrameStart( DOUBLE Now, FLOAT DeltaTime, FLOAT TickDelta )
{
	if( GPaceWanted <= 0 )
		return;
	// The last frame is complete once the next one starts.
	if( GPaceFrames.Num() >= GPaceWanted )
	{
		PaceTraceDump();
		debugf( PACETRACE, TEXT("pacetrace %s done after %i frames"), *GPaceTag, GPaceFrames.Num() );
		GPaceWanted = 0;
		GPaceFrames.Empty();
		return;
	}
	FPaceFrame& F	= GPaceFrames( GPaceFrames.AddZeroed() );
	F.Start			= Now;
	F.Delta			= DeltaTime;
	F.TickDelta		= TickDelta;
	F.Want			= -1.f;
}

// The launcher, when UEngine::Tick returned.
ENGINE_API void UC2PaceTickEnd( DOUBLE Now )
{
	if( GPaceWanted > 0 && GPaceFrames.Num() )
		GPaceFrames.Last().TickEnd = Now;
}

// The launcher's frame limiter: the sleep it asked for and how long it took, seconds.
ENGINE_API void UC2PaceLimiter( FLOAT Want, FLOAT Slept )
{
	if( GPaceWanted > 0 && GPaceFrames.Num() )
	{
		GPaceFrames.Last().Want  = Want;
		GPaceFrames.Last().Slept = Slept;
	}
}

// The render device, around its buffer swap.
ENGINE_API void UC2PaceSwap( DOUBLE Before, DOUBLE After )
{
	if( GPaceWanted <= 0 || !GPaceFrames.Num() )
		return;
	FPaceFrame& F = GPaceFrames.Last();
	if( !F.Swaps )
		F.Swap0 = Before;
	F.Swap1 = After;
	F.Swaps++;
}

// PACETRACE <frames> [tag]
ENGINE_API UBOOL UC2PaceTraceExec( UEngine* Engine, const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UC2PaceTraceExec);
	if( !ParseCommand(&Cmd,TEXT("PACETRACE")) )
		return 0;
	FString Frames, Tag;
	ParseToken( Cmd, Frames, 0 );
	if( !ParseToken( Cmd, Tag, 0 ) )
		Tag = TEXT("p");
	GPaceWanted = Clamp( appAtoi(*Frames), 0, 100000 );
	GPaceTag    = Tag;
	GPaceFrames.Empty( GPaceWanted );

	// What the frames are paced by: the device's UseVSync and the launcher's FrameRateCap.
	URenderDevice* RenDev = Engine ? Engine->GRenDev : NULL;
	UBoolProperty* VSync = RenDev ? FindField<UBoolProperty>( RenDev->GetClass(), TEXT("UseVSync") ) : NULL;
	FLOAT Cap = 0.f;
	GConfig->GetFloat( TEXT("Engine.Engine"), TEXT("FrameRateCap"), Cap, GIni );
	INT SizeX = 0, SizeY = 0;
	if( Engine && Engine->Client && Engine->Client->Viewports.Num() && Engine->Client->Viewports(0) )
	{
		SizeX = Engine->Client->Viewports(0)->SizeX;
		SizeY = Engine->Client->Viewports(0)->SizeY;
	}
	GPaceBegin = FString::Printf( TEXT("pacetrace %s begin %i frames device=%s vsync=%s cap=%g view=%ix%i"),
		*GPaceTag, GPaceWanted, RenDev ? RenDev->GetClass()->GetPathName() : TEXT("none"),
		VSync ? ((*(BITFIELD*)((BYTE*)RenDev + VSync->Offset) & VSync->BitMask) ? TEXT("True") : TEXT("False")) : TEXT("-"),
		Cap, SizeX, SizeY );
	Ar.Logf( TEXT("PACETRACE: %s armed for %i frames"), *GPaceTag, GPaceWanted );
	return 1;
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
