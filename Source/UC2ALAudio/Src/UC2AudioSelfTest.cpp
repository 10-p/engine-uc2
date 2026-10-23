/*=============================================================================
	UC2AudioSelfTest.cpp: numeric checks of the audio device without ears.

	ucc-bin UC2ALAudio.UC2AudioSelfTest   (from System/, cooked Sounds/ beside it)

	The device is initialised in loopback mode (OpenAL Soft mixes into memory at a
	simulated clock, 48 kHz stereo float) and plays one known cue at a time through its
	own code (StartInst onwards). Each result is compared with what the XACT parameters
	say, computed here independently from XACT's rules:

	  gain    total energy out / energy of the decoded wave = Pan * 10^(mB/1000) / PitchRatio,
	          mB = min(Volume + group Volume + variation volume + Volume-parameter curve, 0)
	             + 1000*log10(clamp(category,0.1,1)) - 600 (headroom, unless boosted)
	             - 2000*log10(1 + Rolloff*(clamp(d,Min,Max)-Min)/Min)   (3D only), clamped at 0;
	          Pan (OpenAL Soft's gain for a centred source) is measured once per kind (2D, 3D)
	pan     a source on the listener's right is louder in the right channel
	pitch   the sound lasts Frames/Rate / 2^(cents/1200)
	loops   a play event with Loop Count n lasts n+1 times the wave; an infinite one never ends
	picker  the variation sequence of N plays follows the playlist type's rule
	hrtf    with OpenAL Soft's HRTF a source to one side reaches the near ear first (an interaural
	time difference); plain panning has none.
=============================================================================*/

#include "UC2ALAudioPrivate.h"

IMPLEMENT_CLASS(UUC2AudioSelfTestCommandlet);

void UUC2AudioSelfTestCommandlet::StaticConstructor()
{
	guard(UUC2AudioSelfTestCommandlet::StaticConstructor);
	LogToStdout		= 1;
	IsClient		= 0;
	IsEditor		= 0;
	IsServer		= 0;
	LazyLoad		= 1;
	ShowErrorCount	= 0;
	unguard;
}

static INT GFailures = 0, GChecks = 0;

static void Check( UBOOL bOk, const FString& Line )
{
	GChecks++;
	if( !bOk )
		GFailures++;
	GWarn->Logf( TEXT("%s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *Line );
}

// Interaural time difference of the output: the lag (in frames, -Max..Max) that maximises sum L[n]*R[n+lag].
// A sound that reaches the right ear first has its left channel late: a negative lag.
static INT CrossLag( const TArray<FLOAT>& Out, INT Max, DOUBLE& Peak )
{
	INT Frames = Out.Num()/2, Best = 0;
	Peak = -1e30;
	for( INT Lag=-Max; Lag<=Max; Lag++ )
	{
		DOUBLE Sum = 0.0;
		for( INT n=Max; n<Frames-Max; n++ )
			Sum += (DOUBLE)Out(2*n) * Out(2*(n+Lag)+1);
		if( Sum > Peak )
		{
			Peak = Sum;
			Best = Lag;
		}
	}
	return Best;
}

static DOUBLE Log10( DOUBLE X )
{
	return appLoge( X ) / appLoge( 10.0 );
}

// The decoded wave as the voice plays it (mono downmix on a 3D voice), energy in full-scale units; First/Last:
// the first and last frame within 60 dB of the peak.
static DOUBLE SourceEnergy( FUC2TestPlay& P, INT& Frames, INT& Rate, INT& First, INT& Last )
{
	TArray<SWORD> Pcm;
	INT Channels = 0;
	FString Error;
	Frames = Rate = 0;
	First = Last = -1;
	// (either source: the cooked pack the device loaded, or the shipped .xwb entry read again)
	if( !P.Wave || !P.WaveBank || (!P.WaveBank->bXbox && !P.WaveBank->Data.Num())
	 || !UC2DecodeWave( *P.WaveBank, *P.Wave, TEXT("../Sounds/"), Pcm, Channels, Rate, Error ) )
		return 0.0;
	Frames = Pcm.Num()/Channels;
	TArray<DOUBLE> Amp;
	Amp.Add( Frames );
	DOUBLE E = 0.0, Peak = 0.0;
	for( INT i=0; i<Frames; i++ )
	{
		DOUBLE A = 0.0;
		if( Channels==2 && P.b3DVoice )
		{
			DOUBLE X = ((DOUBLE)Pcm(2*i) + Pcm(2*i+1)) / 2.0 / 32768.0;
			E += X*X;
			A = Abs(X);
		}
		else
			for( INT c=0; c<Channels; c++ )
			{
				DOUBLE X = Pcm(Channels*i+c) / 32768.0;
				E += X*X;
				A = Max( A, Abs(X) );
			}
		Amp(i) = A;
		Peak = Max( Peak, A );
	}
	for( INT i=0; i<Frames; i++ )
		if( Amp(i) > Peak*1e-3 ) { if( First<0 ) First = i; Last = i; }
	return E;
}

// Output energy per channel; First/Last as above.
static DOUBLE OutputEnergy( const TArray<FLOAT>& Out, DOUBLE& Left, DOUBLE& Right, INT& First, INT& Last )
{
	Left = Right = 0.0;
	First = Last = -1;
	DOUBLE Peak = 0.0;
	for( INT i=0; i+1<Out.Num(); i+=2 )
	{
		Left += (DOUBLE)Out(i)*Out(i);
		Right += (DOUBLE)Out(i+1)*Out(i+1);
		Peak = Max( Peak, (DOUBLE)Max( Abs(Out(i)), Abs(Out(i+1)) ) );
	}
	for( INT i=0; i+1<Out.Num(); i+=2 )
		if( Max( Abs(Out(i)), Abs(Out(i+1)) ) > Peak*1e-3 ) { if( First<0 ) First = i/2; Last = i/2; }
	return Left + Right;
}

// The pitch actually applied, measured: 2 ms power envelopes of the source (at its rate) and of the output
// (48 kHz); the time scale r that best maps the output's envelope onto the source's (least variance of their
// log ratio, i.e. any constant gain is ignored) is the playback rate ratio. Searched over 0.5..2 in steps of
// 0.0005 (0.4-1.7 cents). (A first/last-audible-sample duration would be thrown off by the resampler's ringing
// at a hard cut, a 10-90% energy span by onset detail.)
static void Envelope( const TArray<DOUBLE>& Power, INT Window, TArray<DOUBLE>& Env )
{
	Env.Empty();
	for( INT i=0; i+Window<=Power.Num(); i+=Window )
	{
		DOUBLE E = 0.0;
		for( INT k=0; k<Window; k++ )
			E += Power(i+k);
		Env.AddItem( E/Window );
	}
}

static DOUBLE MeasuredRatio( const TArray<DOUBLE>& Src, INT SrcRate, const TArray<DOUBLE>& Out )
{
	TArray<DOUBLE> Es, Eo;
	Envelope( Src, Max( SrcRate/500, 1 ), Es );
	Envelope( Out, UC2_TEST_RATE/500, Eo );
	DOUBLE PeakS = 0.0, PeakO = 0.0;
	for( INT i=0; i<Es.Num(); i++ ) PeakS = Max( PeakS, Es(i) );
	for( INT i=0; i<Eo.Num(); i++ ) PeakO = Max( PeakO, Eo(i) );
	DOUBLE Best = 1e30, BestR = 0.0;
	for( DOUBLE R=0.5; R<=2.0; R+=0.0005 )
	{
		DOUBLE Sum = 0.0, Sum2 = 0.0;
		INT N = 0;
		for( INT j=0; j<Eo.Num(); j++ )
		{
			// window centres: output (j+0.5) <-> source (j+0.5)*R, interpolated between source windows
			DOUBLE T = (j+0.5)*R - 0.5;
			INT i = (INT)appFloor( T );
			if( i<0 )
				continue;
			if( i+1>=Es.Num() )
				break;
			DOUBLE Ev = Es(i) + (Es(i+1)-Es(i))*(T-i);
			if( Eo(j) > PeakO*1e-5 && Ev > PeakS*1e-5 )
			{
				DOUBLE D = appLoge( Eo(j)/Ev );
				Sum += D;
				Sum2 += D*D;
				N++;
			}
		}
		if( N < Eo.Num()/2 || N < 20 )
			continue;
		DOUBLE Var = Sum2/N - (Sum/N)*(Sum/N);
		if( Var < Best )
		{
			Best = Var;
			BestR = R;
		}
	}
	return BestR;
}

static void OutputPower( const TArray<FLOAT>& Out, TArray<DOUBLE>& Power )
{
	Power.Empty( Out.Num()/2 );
	for( INT i=0; i+1<Out.Num(); i+=2 )
		Power.AddItem( (DOUBLE)Out(i)*Out(i) + (DOUBLE)Out(i+1)*Out(i+1) );
}

static void SourcePower( FUC2TestPlay& P, TArray<DOUBLE>& Power, INT& Rate )
{
	TArray<SWORD> Pcm;
	INT Channels = 0;
	FString Error;
	Power.Empty();
	if( !P.Wave || !P.WaveBank || !UC2DecodeWave( *P.WaveBank, *P.Wave, TEXT("../Sounds/"), Pcm, Channels, Rate, Error ) )
		return;
	for( INT i=0; i<Pcm.Num()/Channels; i++ )
	{
		DOUBLE E = 0.0;
		for( INT c=0; c<Channels; c++ )
			E += (DOUBLE)Pcm(Channels*i+c)*Pcm(Channels*i+c);
		Power.AddItem( E );
	}
}

// DUMP=<dir>: every rendered case also goes to <dir>/<n>.wav (48 kHz stereo float) for a look with other tools.
static FString GDumpDir;
static INT GDumpCount = 0;
static void Dump( const TArray<FLOAT>& Out, const TCHAR* What )
{
	if( !GDumpDir.Len() )
		return;
	TArray<BYTE> W;
	INT Data = Out.Num()*sizeof(FLOAT);
	DWORD Hdr[] = { 0x46464952, (DWORD)(36+Data), 0x45564157, 0x20746d66, 16, 0x00020003, UC2_TEST_RATE,
		UC2_TEST_RATE*8, 0x00200008, 0x61746164, (DWORD)Data };
	W.Add( sizeof(Hdr) + Data );
	appMemcpy( &W(0), Hdr, sizeof(Hdr) );
	if( Data )
		appMemcpy( &W(sizeof(Hdr)), &Out(0), Data );
	FString Name = FString::Printf( TEXT("%s/%02d-%s.wav"), *GDumpDir, GDumpCount++, What );
	for( INT i=GDumpDir.Len()+1; i<Name.Len(); i++ )
		if( (*Name)[i]=='.' && i<Name.Len()-4 ) ((TCHAR*)*Name)[i] = '_';
	appSaveArrayToFile( W, *Name );
}

static FLOAT CurveMB( const FUC2Sound& S, FLOAT Value )
{
	// The Volume parameter's curve for track 0, piecewise linear in 0..65535 (a step where X repeats).
	for( INT r=0; r<S.Rpcs.Num(); r++ )
	{
		const FUC2Rpc& R = S.Rpcs(r);
		if( R.Name!=TEXT("Volume") || R.Track!=0 || !R.X.Num() || Value<=0.f )
			continue;
		FLOAT X = Value*65535.f, Y = R.Y(0);
		for( INT i=1; i<R.X.Num(); i++ )
			if( X >= R.X(i) )
				Y = R.Y(i);
			else if( X > R.X(i-1) )
				Y = R.Y(i-1) + (R.Y(i)-R.Y(i-1))*(X-R.X(i-1))/(R.X(i)-R.X(i-1));
		return Y;
	}
	return 0.f;
}

// What the cue tables and XACT's rules say the voice's volume is, in mB.
static DOUBLE ExpectedMB( const FUC2TestPlay& P, FLOAT Category, FLOAT Volume, UBOOL b3D, FLOAT Distance )
{
	const FUC2Sound& S = *P.Sound;
	DOUBLE MB = Min( (DOUBLE)(S.Volume + P.VarVolume + CurveMB( S, Volume )), 0.0 );
	MB += Category < 1e-4f ? -10000.0 : 1000.0 * Log10( Clamp( Category, 0.1f, 1.f ) );
	MB = Max( MB, -10000.0 ) + (S.bHeadroomBoost ? 0 : -600);
	if( b3D )
	{
		DOUBLE Min = S.bIs3D ? S.MinDistance : 1.0, Max = S.bIs3D ? S.MaxDistance : 1e9, Roll = S.bIs3D ? S.Rolloff : 1.0;
		DOUBLE D = Clamp( (DOUBLE)Distance, Min, Max );
		MB += -2000.0 * Log10( 1.0 + Roll*(D-Min)/Min );
	}
	return Min( MB, 0.0 );
}

struct FGainCase
{
	const TCHAR*	Cue;
	UBOOL			b2D;
	FLOAT			Distance, Category, Volume;
};

INT UUC2AudioSelfTestCommandlet::Main( const TCHAR* Parms )
{
	guard(UUC2AudioSelfTestCommandlet::Main);
	GFailures = GChecks = 0;
	Parse( Parms, TEXT("DUMP="), GDumpDir );
	UUC2ALAudioSubsystem* Audio = ConstructObject<UUC2ALAudioSubsystem>( UUC2ALAudioSubsystem::StaticClass() );
	Audio->LoopbackTest = 1;
	Audio->CueLog = 0;
	if( !Audio->Init() || !Audio->State )
	{
		GWarn->Logf( TEXT("FAIL no audio device") );
		return 1;
	}
	for( INT c=0; c<UAudioSubsystem::VOLUME_Max; c++ )
		Audio->SetCategoryVolume( (UAudioSubsystem::EVolumeCategory)c, 1.f );
	TArray<FLOAT> Out;
	FUC2TestPlay P;

	// --- 1. gain: volume, group, headroom, distance (between and beyond Min/Max), category, Volume parameter
	GWarn->Logf( TEXT("--- gain: measured energy against Pan * 10^(mB/1000) / pitch ratio (Pan measured on the first case of each kind)") );
	FGainCase Cases[] =
	{
		{ TEXT("A_Weapon.RL_Fire"),					0,   400.f, 1.f,   0.f },	// 3D reference (inside MinDistance 800)
		{ TEXT("A_Weapon.RL_Fire"),					1,  5000.f, 1.f,   0.f },	// 2D reference (distance ignored)
		{ TEXT("A_Weapon.RL_Fire"),					0,   800.f, 1.f,   0.f },
		{ TEXT("A_Weapon.RL_Fire"),					0,  1600.f, 1.f,   0.f },
		{ TEXT("A_Weapon.RL_Fire"),					0,  3200.f, 1.f,   0.f },
		{ TEXT("A_Weapon.RL_Fire"),					0,  8000.f, 1.f,   0.f },
		{ TEXT("A_Weapon.RL_Fire"),					0, 32000.f, 1.f,   0.f },
		{ TEXT("A_Weapon.RL_Fire"),					0, 99000.f, 1.f,   0.f },
		{ TEXT("A_Weapon.RL_Fire"),					0,200000.f, 1.f,   0.f },	// beyond MaxDistance: no further loss
		{ TEXT("A_Gameplay.CampFireBombExplosion"),	0,   275.f, 1.f,   0.f },	// Rolloff 3, group "all"
		{ TEXT("A_Gameplay.CampFireBombExplosion"),	0,   550.f, 1.f,   0.f },
		{ TEXT("A_Gameplay.CampFireBombExplosion"),	0,  1100.f, 1.f,   0.f },
		{ TEXT("A_Gameplay.CampFireBombExplosion"),	0,  5500.f, 1.f,   0.f },
		{ TEXT("A_Weapon.RL_Fire"),					0,   400.f, 0.5f,  0.f },	// FX category volume
		{ TEXT("A_Weapon.RL_Fire"),					0,   400.f, 0.2f,  0.f },
		{ TEXT("A_Weapon.RL_Fire"),					0,   400.f, 0.05f, 0.f },	// below 0.1: held at 0.1 (XACT CalcVolume)
		{ TEXT("A_Character.NK_Sobek_Death"),		0,  3000.f, 1.f,   0.f },	// Volume parameter not set
		{ TEXT("A_Character.NK_Sobek_Death"),		0,  3000.f, 1.f,  0.25f },	// below the step: +0
		{ TEXT("A_Character.NK_Sobek_Death"),		0, 60000.f, 1.f,   1.f },	// +40 dB on the sound's volume, clamped at 0 dB
		{ TEXT("A_Character.NK_Sobek_Death"),		0,  3000.f, 1.f,   1.f },	// before the headroom and the distance
		{ TEXT("A_Character.NK_Anubis_Pain"),		0,  3000.f, 1.f,   1.f },	// the hit heard by the attacker (+30 dB)
		{ TEXT("A_Character.NK_Anubis_Pain"),		0,  3000.f, 1.f,   0.f },	// the same hit heard by anyone else
	};
	DOUBLE Pan3D = 0.0, Pan2D = 0.0;
	for( INT i=0; i<ARRAY_COUNT(Cases); i++ )
	{
		FGainCase& C = Cases[i];
		Audio->SetCategoryVolume( UAudioSubsystem::VOLUME_FX, C.Category );
		Out.Empty();
		if( !UC2TestPlay( Audio, C.Cue, FVector( C.Distance, 0, 0 ), C.b2D, C.Volume, P ) )
		{
			Check( 0, FString::Printf( TEXT("gain %s: did not start"), C.Cue ) );
			continue;
		}
		UC2TestRender( Audio, 30.f, Out );
		UC2TestStopAll( Audio );
		Dump( Out, C.Cue );
		INT Frames, Rate, F0, F1, O0, O1;
		DOUBLE L, R;
		DOUBLE Es = SourceEnergy( P, Frames, Rate, F0, F1 );
		DOUBLE Eo = OutputEnergy( Out, L, R, O0, O1 );
		DOUBLE Ratio = appPow( 2.0, (P.Sound->Pitch + P.VarPitch)/1200.0 );
		DOUBLE MB = ExpectedMB( P, C.Category, C.Volume, !C.b2D, C.Distance );
		// Energy of a resampled wave scales with 1/ratio (it lasts 1/ratio as long), and with the output rate.
		DOUBLE Expected = Es * appPow( 10.0, MB/1000.0 ) / Ratio * UC2_TEST_RATE / Rate;
		if( i==0 ) Pan3D = Eo/Expected;
		if( i==1 ) Pan2D = Eo/Expected;
		DOUBLE Pan = C.b2D ? Pan2D : Pan3D;
		DOUBLE Err = 10.0*Log10( Eo / (Expected*Pan) );
		FString Line = FString::Printf( TEXT("gain %-34s %s d=%6.0f cat=%.2f Volume=%.2f: expected %7.1f mB, measured %7.1f mB, error %+.2f dB"),
			C.Cue, C.b2D ? TEXT("2D") : TEXT("3D"), C.Distance, C.Category, C.Volume, MB, MB + 100.0*Err, Err );
		if( i<2 )
			GWarn->Logf( TEXT("REF  %s (defines Pan%s = %.2f dB)"), *Line, C.b2D ? TEXT("2D") : TEXT("3D"), 10.0*Log10( C.b2D ? Pan2D : Pan3D ) );
		else
			Check( Abs(Err) <= 0.25, Line );
	}
	Audio->SetCategoryVolume( UAudioSubsystem::VOLUME_FX, 1.f );

	// --- 2. pan direction: Unreal +Y is the listener's right
	GWarn->Logf( TEXT("--- pan: a source 2000 units to the right / left of a listener looking down +X") );
	for( INT Side=-1; Side<=1; Side+=2 )
	{
		Out.Empty();
		UC2TestPlay( Audio, TEXT("A_Weapon.RL_Fire"), FVector( 0, Side*2000.f, 0 ), 0, 0.f, P );
		UC2TestRender( Audio, 30.f, Out );
		UC2TestStopAll( Audio );
		INT O0, O1;
		DOUBLE L, R;
		OutputEnergy( Out, L, R, O0, O1 );
		DOUBLE Db = 10.0*Log10( R/Max(L,1e-30) );
		Check( Side>0 ? Db>6.0 : Db<-6.0, FString::Printf( TEXT("pan %s: right minus left channel energy %+.1f dB"),
			Side>0 ? TEXT("right") : TEXT("left "), Db ) );
	}

	// --- 3. pitch: duration of the sound against Frames/Rate / 2^(cents/1200)
	GWarn->Logf( TEXT("--- pitch: playback rate ratio fitted from the output's envelope against the source's, against 2^(cents/1200)") );
	DOUBLE PitchBias = 0.0;
	const TCHAR* PitchCues[] = { TEXT("A_Weapon.RL_Fire"), TEXT("A_Pickup.Adren_Pickup"), TEXT("A_Pickup.Adren_SuperClose"), TEXT("A_Octane.Lift_Tech_Start02") };
	for( INT i=0; i<ARRAY_COUNT(PitchCues); i++ )
	{
		Out.Empty();
		if( !UC2TestPlay( Audio, PitchCues[i], FVector( 100, 0, 0 ), 1, 0.f, P ) )
		{
			Check( 0, FString::Printf( TEXT("pitch %s: did not start"), PitchCues[i] ) );
			continue;
		}
		UC2TestRender( Audio, 30.f, Out );
		UC2TestStopAll( Audio );
		Dump( Out, PitchCues[i] );
		TArray<DOUBLE> Ps, Po;
		INT Rate = 0;
		SourcePower( P, Ps, Rate );
		OutputPower( Out, Po );
		DOUBLE Cents = P.Sound->Pitch + P.VarPitch;
		DOUBLE Measured = MeasuredRatio( Ps, Rate, Po );
		DOUBLE MeasuredCents = Measured>0.0 ? 1200.0 * appLoge( Measured ) / appLoge( 2.0 ) : -99999.0;
		// The first cue has no pitch offset: what it measures is the method's own bias (the output's onset sits
		// about 2 ms later than the source's), taken off the others.
		if( i==0 )
			PitchBias = MeasuredCents - Cents;
		Check( Abs(MeasuredCents - Cents) <= 5.0 && (i==0 || Abs(MeasuredCents - PitchBias - Cents) <= 1.5),
			FString::Printf( TEXT("pitch %-30s expected %+5.0f cents (x%.4f), measured %+7.1f cents (x%.4f), error %+.1f cents, %+.1f after the method bias of the 0-cent control"),
			PitchCues[i], Cents, appPow( 2.0, Cents/1200.0 ), MeasuredCents, Measured, MeasuredCents - Cents, MeasuredCents - PitchBias - Cents ) );
	}

	// --- 4. loops: a finite Loop Count plays n+1 times; an infinite loop is still playing after 3 lengths
	GWarn->Logf( TEXT("--- loops") );
	{
		Out.Empty();
		UC2TestPlay( Audio, TEXT("A_WeaponMelee.STF_JumpChargeEthereal"), FVector( 100, 0, 0 ), 1, 0.f, P );
		UC2TestRender( Audio, 60.f, Out );
		UC2TestStopAll( Audio );
		INT Frames, Rate, F0, F1, O0, O1;
		DOUBLE L, R;
		SourceEnergy( P, Frames, Rate, F0, F1 );
		OutputEnergy( Out, L, R, O0, O1 );
		INT Loops = P.Sound ? P.Sound->Tracks(0).Loops : -1;
		DOUBLE Ratio = appPow( 2.0, (P.Sound->Pitch + P.VarPitch)/1200.0 );
		DOUBLE Expected = (DOUBLE)Frames*(Loops+1) / Rate / Ratio;
		// The first pass's leading silence and the last pass's trailing silence are not "active".
		DOUBLE ExpectedActive = ((DOUBLE)Frames*Loops + (F1 + 1) - F0) / Rate / Ratio;
		DOUBLE Measured = (DOUBLE)(O1 - O0 + 1) / UC2_TEST_RATE;
		Check( Abs(Measured/ExpectedActive - 1.0) < 0.01, FString::Printf( TEXT("loop count %d (STF_JumpChargeEthereal): plays %.3f s, expected %.3f s active (%.3f s total, %d frames x %d at %d Hz)"),
			Loops, Measured, ExpectedActive, Expected, Frames, Loops+1, Rate ) );
	}
	{
		Out.Empty();
		UC2TestPlay( Audio, TEXT("A_Combo.Powerup_Invis_Loop"), FVector( 100, 0, 0 ), 1, 0.f, P );
		DOUBLE Length = P.Wave ? (DOUBLE)P.Wave->Frames/P.Wave->Rate : 1.0;
		UC2TestRender( Audio, (FLOAT)(3.0*Length), Out );
		INT O0, O1;
		DOUBLE L, R;
		OutputEnergy( Out, L, R, O0, O1 );
		DOUBLE Tail = (DOUBLE)(Out.Num()/2 - 1 - O1) / UC2_TEST_RATE;
		Check( Tail < 0.05, FString::Printf( TEXT("infinite loop (Powerup_Invis_Loop, wave %.3f s, loop region %d+%d of %d frames): still sounding at %.3f s (last sample %.3f s from the end)"),
			Length, P.Wave ? P.Wave->LoopStart : 0, P.Wave ? P.Wave->LoopLength : 0, P.Wave ? P.Wave->Frames : 0, (DOUBLE)Out.Num()/2/UC2_TEST_RATE, Tail ) );
		UC2TestStopAll( Audio );
	}

	// --- 5. variation picker: N plays, the sequence against the playlist type's rule
	GWarn->Logf( TEXT("--- variations") );
	const TCHAR* VarCues[] = { TEXT("A_Character.Footstep_Metal"), TEXT("A_Combo.Combo_EMP"), TEXT("A_Weapon.Generic_BulletWhip") };
	for( INT v=0; v<ARRAY_COUNT(VarCues); v++ )
	{
		TArray<INT> Seq;
		INT Playlist = -1, Count = 0;
		for( INT n=0; n<60; n++ )
		{
			if( UC2TestPlay( Audio, VarCues[v], FVector( 100, 0, 0 ), 1, 0.f, P ) && P.Sound )
			{
				Seq.AddItem( P.Variation );
				Playlist = P.Sound->Tracks(0).Playlist;
				Count = P.Sound->Tracks(0).Variations.Num();
			}
			UC2TestStopAll( Audio );
		}
		FString S;
		TArray<INT> Hist;
		Hist.AddZeroed( Max(Count,1) );
		INT Repeats = 0, OutOfOrder = 0;
		for( INT i=0; i<Seq.Num(); i++ )
		{
			S += FString::Printf( TEXT("%d"), Seq(i) );
			if( Seq(i)>=0 && Seq(i)<Hist.Num() ) Hist(Seq(i))++;
			if( i>0 && Seq(i)==Seq(i-1) ) Repeats++;
			if( i>0 && Seq(i)!=(Seq(i-1)+1)%Count ) OutOfOrder++;
		}
		FString H;
		for( INT i=0; i<Hist.Num(); i++ )
			H += FString::Printf( TEXT("%s%d"), i ? TEXT(",") : TEXT(""), Hist(i) );
		UBOOL bOk = Seq.Num()==60;
		FString Rule;
		if( Playlist==PL_RandomNoRepeat )	{ bOk &= Repeats==0; Rule = FString::Printf( TEXT("random, no immediate repeat: %d repeats"), Repeats ); }
		else if( Playlist==PL_Ordered )		{ bOk &= OutOfOrder==0; Rule = FString::Printf( TEXT("ordered (all weights unset: index order): %d out of order"), OutOfOrder ); }
		else
		{
			// Random: every variation drawn, none more than 3x its fair share.
			INT MinH = 1000, MaxH = 0;
			for( INT i=0; i<Hist.Num(); i++ ) { MinH = Min( MinH, Hist(i) ); MaxH = Max( MaxH, Hist(i) ); }
			bOk &= MaxH <= 3*60/Count + 3;
			Rule = FString::Printf( TEXT("random: counts min %d max %d (fair share %.1f), %d immediate repeats"), MinH, MaxH, 60.f/Count, Repeats );
		}
		Check( bOk, FString::Printf( TEXT("variations %s (playlist %d, %d waves): %s; sequence %s; counts %s"),
			VarCues[v], Playlist, Count, *Rule, *S, *H ) );
	}

	// --- 6. reverb: the zone presets through EFX. A 3D voice's send gets the preset's reverb tail, whose decay
	// time is measured (slope of the tail's level between 5 and 35 dB below its start) against the preset's
	// DecayTime (I3DL2 decay time = RT60); a 2D voice has no send; NoReverb has no tail.
	GWarn->Logf( TEXT("--- reverb (EFX %s)"), Audio->State && GAL.alGenEffects ? TEXT("bound") : TEXT("missing") );
	struct { INT Preset; UBOOL b2D; } Verbs[] = { { 12, 0 }, { 0, 0 }, { 7, 0 }, { 25, 0 }, { 12, 1 } };
	for( INT v=0; v<ARRAY_COUNT(Verbs); v++ )
	{
		Audio->Exec( *FString::Printf( TEXT("UC2AUDIO ZONE %d"), Verbs[v].Preset ), *GNull );
		const FUC2I3DL2& Pr = GUC2ZonePresets[Verbs[v].Preset];
		Out.Empty();
		UC2TestPlay( Audio, TEXT("A_Weapon.RL_Fire"), FVector( 400, 0, 0 ), Verbs[v].b2D, 0.f, P );
		DOUBLE Dry = P.Wave ? (DOUBLE)P.Wave->Frames/P.Wave->Rate : 0.0;
		UC2TestRender( Audio, 30.f, Out );				// until the voice stops
		TArray<FLOAT> Tail;
		UC2TestRender( Audio, (FLOAT)Min( 2.5*Pr.DecayTime + 0.5, 20.0 ), Tail, 0 );	// then the room alone
		UC2TestStopAll( Audio );
		Dump( Out, *FString::Printf( TEXT("reverb-%s%s"), Pr.Name, Verbs[v].b2D ? TEXT("-2D") : TEXT("") ) );
		Dump( Tail, *FString::Printf( TEXT("reverb-%s%s-tail"), Pr.Name, Verbs[v].b2D ? TEXT("-2D") : TEXT("") ) );
		DOUBLE L, R, TL, TR;
		INT O0, O1, T0, T1;
		DOUBLE E = OutputEnergy( Out, L, R, O0, O1 );
		DOUBLE ET = OutputEnergy( Tail, TL, TR, T0, T1 );
		DOUBLE TailDb = 10.0*Log10( Max( ET, 1e-30 ) / Max( E, 1e-30 ) );
		// 20 ms levels of the tail; regression of dB on time between -5 and -35 dB from its first level.
		TArray<DOUBLE> Pw, Env;
		OutputPower( Tail, Pw );
		Envelope( Pw, UC2_TEST_RATE/50, Env );
		DOUBLE Rt60 = 0.0;
		if( Env.Num()>4 && Env(0)>0.0 )
		{
			DOUBLE Start = 10.0*Log10( Env(0) );
			DOUBLE Sx=0, Sy=0, Sxx=0, Sxy=0;
			INT N = 0;
			for( INT i=0; i<Env.Num(); i++ )
			{
				DOUBLE Db = 10.0*Log10( Max( Env(i), 1e-30 ) ) - Start;
				if( Db < -35.0 ) break;
				if( Db > -5.0 ) continue;
				DOUBLE T = i*0.02;
				Sx += T; Sy += Db; Sxx += T*T; Sxy += T*Db; N++;
			}
			if( N>=3 )
			{
				DOUBLE Slope = (N*Sxy - Sx*Sy) / (N*Sxx - Sx*Sx);
				Rt60 = Slope<0.0 ? -60.0/Slope : 0.0;
			}
		}
		UBOOL bOk;
		FString What;
		if( Verbs[v].b2D || Verbs[v].Preset==25 )
		{
			bOk = TailDb < -80.0;
			What = TEXT("no tail expected");
		}
		else
		{
			bOk = TailDb > -80.0 && Rt60 > 0.7*Pr.DecayTime && Rt60 < 1.3*Pr.DecayTime;
			What = FString::Printf( TEXT("decay time measured %.2f s, preset %.2f s (%+.0f%%)"), Rt60, Pr.DecayTime, 100.0*(Rt60/Pr.DecayTime-1.0) );
		}
		Check( bOk, FString::Printf( TEXT("reverb %-10s %s (dry wave %.2f s): tail energy %.1f dB of the sound's; %s"),
			Pr.Name, Verbs[v].b2D ? TEXT("2D") : TEXT("3D, send -300 mB"), Dry, TailDb, *What ) );
	}
	Audio->Exec( TEXT("UC2AUDIO ZONE 0"), *GNull );

	// --- 6. radius and duration as the engine asks for them (XACT GetSoundCueRadius / GetSoundCueDuration)
	GWarn->Logf( TEXT("--- radius and duration") );
	{
		FLOAT Radius = Audio->GetSoundCueRadius( FName(TEXT("A_Weapon.RL_Fire")) );		// Min 800, Rolloff 0.4
		Check( Abs(Radius - (2.f*800.f + 800.f*2.f/0.4f)) < 0.5f, FString::Printf( TEXT("radius A_Weapon.RL_Fire = %.1f (2*800 + 800*2/0.4 = 5600)"), Radius ) );
		FLOAT Radius2 = Audio->GetSoundCueRadius( FName(TEXT("A_Combo.Combo_Generic_Activate")) );	// not 3D
		Check( Abs(Radius2 - 4.f) < 0.01f, FString::Printf( TEXT("radius of a non-3D cue (A_Combo.Combo_Generic_Activate) = %.2f (DirectSound defaults: 4)"), Radius2 ) );
		FLOAT Dur = Audio->GetSoundCueDuration( FName(TEXT("A_Weapon.RL_Fire")) );
		Check( Abs(Dur - 1.828f) < 0.0015f, FString::Printf( TEXT("duration A_Weapon.RL_Fire = %.3f s (cue table: 1828 ms)"), Dur ) );
		FLOAT Inf = Audio->GetSoundCueDuration( FName(TEXT("A_Combo.Powerup_Invis_Loop")) );
		Check( Inf > 4294966.f, FString::Printf( TEXT("duration of an infinite loop = %.0f s (XACT: 0xFFFFFFFF ms)"), Inf ) );
		FLOAT Unknown = Audio->GetSoundCueDuration( FName(TEXT("A_Weapon.NoSuchCue")) );
		Check( Unknown==0.f, FString::Printf( TEXT("duration of an unknown cue = %.1f"), Unknown ) );
	}

	// --- 7. HRTF: the same side source as the pan check, with OpenAL Soft's HRTF off and on (alcResetDeviceSOFT
	// on the loopback device). Off: plain panning, no time difference (|lag| <= 2 frames: the far channel is 16 dB
	// down and carries the reverb send). On: the near ear leads by a fraction of a millisecond and is still the
	// louder one. Accepted: 8..48 frames (0.17..1.0 ms) at 48 kHz; a head's own ITD is ~0.6-0.8 ms at 90 degrees, and
	// the correlation peak also carries the data set's filter delay (about 42 frames with OpenAL Soft's embedded
	// "Default HRTF").
	GWarn->Logf( TEXT("--- hrtf: a source 2000 units to the right / left, OpenAL Soft's HRTF off and on (interaural time and level difference)") );
	for( INT On=0; On<=1; On++ )
	{
		FString Status;
		UBOOL bActive = UC2TestSetHRTF( Audio, On, Status );
		Check( bActive==On, FString::Printf( TEXT("hrtf %s: %s"), On ? TEXT("on ") : TEXT("off"), *Status ) );
		if( bActive!=On )
			continue;
		for( INT Side=-1; Side<=1; Side+=2 )
		{
			Out.Empty();
			UC2TestPlay( Audio, TEXT("A_Weapon.RL_Fire"), FVector( 0, Side*2000.f, 0 ), 0, 0.f, P );
			UC2TestRender( Audio, 30.f, Out );
			UC2TestStopAll( Audio );
			Dump( Out, *FString::Printf( TEXT("hrtf-%s-%s"), On ? TEXT("on") : TEXT("off"), Side>0 ? TEXT("right") : TEXT("left") ) );
			INT O0, O1;
			DOUBLE L, R, Peak;
			OutputEnergy( Out, L, R, O0, O1 );
			DOUBLE Db = 10.0*Log10( R/Max(L,1e-30) );
			INT Lag = CrossLag( Out, 48, Peak );
			INT NearLeads = Side>0 ? -Lag : Lag;		// frames by which the near ear's channel leads
			UBOOL bOk = Side>0 ? Db>3.0 : Db<-3.0;
			bOk = bOk && ( On ? (NearLeads>=8 && NearLeads<=48) : Abs(Lag)<=2 );
			Check( bOk, FString::Printf( TEXT("hrtf %s %s: right minus left %+.1f dB, near ear leads by %d frames (%.3f ms)%s"),
				On ? TEXT("on ") : TEXT("off"), Side>0 ? TEXT("right") : TEXT("left "), Db, NearLeads, 1000.0*NearLeads/UC2_TEST_RATE,
				On ? TEXT(" (expected 8..48)") : TEXT(" (expected -2..2)") ) );
		}
	}
	{
		FString Status;
		UC2TestSetHRTF( Audio, 0, Status );
	}

	GWarn->Logf( TEXT("UC2AudioSelfTest: %d checks, %d failed; Pan3D %.2f dB, Pan2D %.2f dB"), GChecks, GFailures,
		10.0*Log10(Max(Pan3D,1e-30)), 10.0*Log10(Max(Pan2D,1e-30)) );
	delete Audio;
	return GFailures ? 1 : 0;
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
