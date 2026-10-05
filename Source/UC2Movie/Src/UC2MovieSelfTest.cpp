/*=============================================================================
	UC2MovieSelfTest.cpp: `ucc-bin UC2Movie.UC2MovieSelfTest REF=<dir>` (M9, V3).

	Decodes a sample of cooked movies with the game's own player and compares with references ffmpeg wrote
	from the same files (tools/dev/movie-selftest.sh makes them and runs this):
	  - frames: the frame count against the stream's packet count; the frames where a new picture starts
	    against ffmpeg's decoded pictures (ffmpeg does not output the zero-byte "held" packets); every picture's
	    Adler-32 (of the Y'CbCr planes, packed as yuv420p) against ffmpeg's framecrc;
	  - colour: the player's BGRA of two pictures against ffmpeg's swscale (BT.601, limited range), and the
	    player's fixed-point conversion against the float formula;
	  - sound: sample count against the cook's (the end-of-stream granule), values against ffmpeg's decode,
	    the padding to the video's length;
	  - the language choice (appFindMovie) for int/deu/spa/an unknown language and a movie without tracks;
	  - looping: frame 0 after the last frame, by frame steps, by the real-time clock and by the sound clock;
	  - A/V sync: driven by a simulated sound device, the picture shown at sound time t is the one whose
	    timestamp covers t, over the whole movie.
	One PASS/FAIL line per check, then "UC2MovieSelfTest: <n> checks, <m> failed".
=============================================================================*/

#include "UC2MoviePrivate.h"

void UUC2MovieSelfTestCommandlet::StaticConstructor()
{
	guard(UUC2MovieSelfTestCommandlet::StaticConstructor);
	LogToStdout		= 1;
	IsClient		= 0;
	IsEditor		= 0;
	IsServer		= 0;
	LazyLoad		= 1;
	ShowErrorCount	= 0;
	unguard;
}

static INT GChecks = 0, GFailed = 0;

static void Check( UBOOL bOk, const FString& Line )
{
	GChecks++;
	if( !bOk )
		GFailed++;
	GWarn->Logf( TEXT("%s %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *Line );
}

// ffmpeg's framecrc: av_adler32_update(0, data, size).
static DWORD Adler32( DWORD Adler, const BYTE* Data, INT Len )
{
	DWORD A = Adler & 0xffff, B = Adler >> 16;
	for( INT i=0; i<Len; i++ )
	{
		A = (A + Data[i]) % 65521;
		B = (B + A) % 65521;
	}
	return (B << 16) | A;
}

static DWORD PictureCrc( FUC2MoviePlayer* P )
{
	th_ycbcr_buffer Y;
	if( !P->GetYCbCr( Y ) )
		return 0;
	DWORD Crc = 0;
	for( INT Plane=0; Plane<3; Plane++ )
	{
		INT W = Plane ? (P->GetWidth() + 1) / 2 : P->GetWidth();
		INT H = Plane ? (P->GetHeight() + 1) / 2 : P->GetHeight();
		for( INT Row=0; Row<H; Row++ )
			Crc = Adler32( Crc, Y[Plane].data + Row * Y[Plane].stride, W );
	}
	return Crc;
}

struct FRefPicture { INT Pts; DWORD Crc; };

static UBOOL LoadText( const FString& Path, TArray<FString>& Lines )
{
	FString Text;
	if( !appLoadFileToString( Text, *Path ) )
		return 0;
	Lines.Empty();
	while( Text.Len() )
	{
		INT Nl = Text.InStr( TEXT("\n") );
		FString Line = Nl >= 0 ? Text.Left(Nl) : Text;
		Text = Nl >= 0 ? Text.Mid(Nl+1) : FString();
		if( Line.Len() )
			new(Lines) FString( Line );	// not AddItem: it assigns into unconstructed memory (M8 finding)
	}
	return 1;
}

static void SplitWords( const FString& Line, TArray<FString>& Words, const TCHAR* Sep )
{
	Words.Empty();
	FString S = Line;
	while( S.Len() )
	{
		INT I = S.InStr( Sep );
		FString W = I >= 0 ? S.Left(I) : S;
		S = I >= 0 ? S.Mid(I+appStrlen(Sep)) : FString();
		while( W.Len() && W[0] == ' ' )
			W = W.Mid(1);
		if( W.Len() )
			new(Words) FString( W );
	}
}

// The simulated sound device: a bounded queue whose "played" position the test moves.
struct FTestSink : public FMovieAudioStream
{
	INT				Channels, Capacity;
	TArray<SWORD>	Written;
	DOUBLE			Played;
	FTestSink( INT InChannels ) : Channels(InChannels), Capacity(16384), Played(0.0) {}
	INT WrittenFrames() { return Written.Num() / Max(Channels,1); }
	INT FreeFrames() { return Max( 0, Capacity - (WrittenFrames() - (INT)Played) ); }
	void Write( const SWORD* Pcm, INT Frames )
	{
		INT At = Written.Add( Frames * Channels );
		appMemcpy( &Written(At), Pcm, Frames * Channels * sizeof(SWORD) );
	}
	DOUBLE PlayedFrames() { return Played; }
	INT QueuedFrames() { return WrittenFrames() - (INT)Played; }
	void Pause( UBOOL ) {}
};

static DOUBLE GTestNow = 0.0;
static DOUBLE TestNow() { return GTestNow; }

// A deterministic pseudo-random step, 5..40 ms.
static DWORD GSeed = 7;
static DOUBLE RandomStep()
{
	GSeed = GSeed * 1103515245 + 12345;
	return 0.005 + ((GSeed >> 16) % 1000) * 0.000035;
}

static void ColourChecks( const FString& Case, FUC2MoviePlayer* P, INT Frame, const TArray<BYTE>& Ours, const FString& RefFile )
{
	TArray<BYTE> Ref;
	INT W = P->GetWidth(), H = P->GetHeight();
	if( !appLoadFileToArray( Ref, *RefFile ) || Ref.Num() != W*H*4 || Ours.Num() != W*H*4 )
	{
		Check( 0, FString::Printf( TEXT("%s colour frame %i: no reference %s (%i bytes) or picture"), *Case, Frame, *RefFile, Ref.Num() ) );
		return;
	}
	INT MaxDiff = 0, Over1 = 0;
	for( INT i=0; i<W*H*4; i++ )
	{
		INT D = Abs( (INT)Ours(i) - (INT)Ref(i) );
		MaxDiff = Max( MaxDiff, D );
		if( D > 1 )
			Over1++;
	}
	Check( MaxDiff <= 1, FString::Printf( TEXT("%s colour frame %i vs ffmpeg swscale (bt601, tv range, chroma replicated): max difference %i, %i of %i values differ by more than 1"),
		*Case, Frame, MaxDiff, Over1, W*H*4 ) );
}

// The player's 8.8 fixed-point conversion against the float BT.601 formula, on the current picture.
static void FormulaCheck( const FString& Case, FUC2MoviePlayer* P, INT Frame )
{
	th_ycbcr_buffer Y;
	if( !P->GetYCbCr( Y ) )
		return;
	INT W = P->GetWidth(), H = P->GetHeight(), MaxDiff = 0;
	const BYTE* Ours = P->GetBGRA();
	for( INT y=0; y<H; y++ )
		for( INT x=0; x<W; x++ )
		{
			FLOAT L = 1.164383f * (Y[0].data[y*Y[0].stride + x] - 16);
			FLOAT U = Y[1].data[(y/2)*Y[1].stride + x/2] - 128.f;
			FLOAT V = Y[2].data[(y/2)*Y[2].stride + x/2] - 128.f;
			FLOAT C[3] = { L + 2.017232f*U, L - 0.391762f*U - 0.812968f*V, L + 1.596027f*V };	// B G R
			for( INT c=0; c<3; c++ )
			{
				INT F = Clamp( appRound( C[c] ), 0, 255 );
				MaxDiff = Max( MaxDiff, Abs( F - (INT)Ours[(y*W + x)*4 + c] ) );
			}
		}
	Check( MaxDiff <= 1, FString::Printf( TEXT("%s colour frame %i: fixed-point conversion vs the float BT.601 formula: max difference %i"), *Case, Frame, MaxDiff ) );
}

static void VideoCase( const FString& Ref, const FString& Case, const FString& Video )
{
	// References.
	TArray<FString> Lines, Words;
	TArray<FRefPicture> Pics;
	if( LoadText( Ref + TEXT("/") + Case + TEXT(".vcrc"), Lines ) )
		for( INT i=0; i<Lines.Num(); i++ )
		{
			if( Lines(i)[0] == '#' )
				continue;
			SplitWords( Lines(i), Words, TEXT(",") );
			if( Words.Num() >= 6 )
			{
				FRefPicture R;
				R.Pts = appAtoi( *Words(2) );
				R.Crc = (DWORD)strtoul( appToAnsi(*Words(5)), NULL, 16 );
				Pics.AddItem( R );
			}
		}
	FString PacketsText, MidText;
	appLoadFileToString( PacketsText, *(Ref + TEXT("/") + Case + TEXT(".packets")) );
	appLoadFileToString( MidText, *(Ref + TEXT("/") + Case + TEXT(".mid")) );
	INT Packets = appAtoi( *PacketsText ), Mid = appAtoi( *MidText );

	// Every frame, one per Advance.
	FUC2MoviePlayer* P = new FUC2MoviePlayer;
	if( !P->Open( *Video, NULL, MOVIE_ManualSync, NULL ) )
	{
		Check( 0, FString::Printf( TEXT("%s: cannot open %s"), *Case, *Video ) );
		delete P;
		return;
	}
	GWarn->Logf( TEXT("case %s: %s: %s"), *Case, *Video, P->GetDescription() );
	INT N = P->GetFrameCount();
	Check( N == Packets && Packets > 0, FString::Printf( TEXT("%s frame count: player %i, packets in the stream (ffprobe) %i"), *Case, N, Packets ) );
	TArray<DWORD> Crcs;
	TArray<INT> NewPictures;
	TArray<BYTE> Bgra0, BgraMid;
	INT OrderErrors = 0;
	for( INT n=0; n<N; n++ )
	{
		UBOOL bNew = P->Advance();
		if( P->GetFrame() != n )
			OrderErrors++;
		if( bNew )
			NewPictures.AddItem( n );
		Crcs.AddItem( PictureCrc( P ) );
		if( n == 0 || n == Mid )
		{
			TArray<BYTE>& Dst = n == 0 ? Bgra0 : BgraMid;
			Dst.Empty();
			Dst.Add( P->GetWidth() * P->GetHeight() * 4 );
			appMemcpy( &Dst(0), P->GetBGRA(), Dst.Num() );
			FormulaCheck( Case, P, n );
		}
	}
	Check( OrderErrors == 0, FString::Printf( TEXT("%s frame order: %i frames stepped, %i out of order"), *Case, N, OrderErrors ) );
	// Timestamps: a new picture exactly at the frames where ffmpeg decodes one.
	UBOOL bSame = NewPictures.Num() == Pics.Num();
	for( INT i=0; bSame && i<Pics.Num(); i++ )
		bSame = NewPictures(i) == Pics(i).Pts;
	Check( bSame, FString::Printf( TEXT("%s timestamps: %i new pictures at the same frame numbers as ffmpeg's %i decoded pictures (the other %i frames are held pictures)"),
		*Case, NewPictures.Num(), Pics.Num(), N - NewPictures.Num() ) );
	INT Same = 0;
	for( INT i=0; i<Pics.Num(); i++ )
		if( Pics(i).Pts >= 0 && Pics(i).Pts < Crcs.Num() && Crcs(Pics(i).Pts) == Pics(i).Crc )
			Same++;
	Check( Same == Pics.Num() && Pics.Num() > 0, FString::Printf( TEXT("%s pictures: %i of %i identical to ffmpeg's decode (Adler-32 of the Y'CbCr planes)"), *Case, Same, Pics.Num() ) );
	ColourChecks( Case, P, 0, Bgra0, Ref + TEXT("/") + Case + TEXT(".bgra0") );
	ColourChecks( Case, P, Mid, BgraMid, Ref + FString::Printf( TEXT("/%s.bgra%i"), *Case, Mid ) );

	// Loop wrap by frame steps: frame 0, with frame 0's picture, right after the last frame.
	delete P;
	P = new FUC2MoviePlayer;
	P->Open( *Video, NULL, MOVIE_ManualSync | MOVIE_Loop, NULL );
	INT Wrong = 0;
	for( INT k=0; k<N+3; k++ )
	{
		P->Advance();
		if( P->GetFrame() != k % N || PictureCrc( P ) != Crcs(k % N) )
			Wrong++;
	}
	Check( Wrong == 0, FString::Printf( TEXT("%s loop by frames: %i steps across the end, frame %i is followed by frame 0 with its picture (%i wrong)"), *Case, N+3, N-1, Wrong ) );
	delete P;

	// Loop wrap on the real-time clock (no sound): at t = (k+0.5)/fps the frame is k mod N, twice round.
	P = new FUC2MoviePlayer;
	P->TestClock = TestNow;
	GTestNow = 1000.0;
	P->Open( *Video, NULL, MOVIE_Loop, NULL );
	Wrong = 0;
	FLOAT Fps = P->GetFrameRate();
	for( INT k=0; k<2*N+2; k++ )
	{
		GTestNow = 1000.0 + (k + 0.5) / Fps;
		P->Advance();
		if( P->GetFrame() != k % N || PictureCrc( P ) != Crcs(k % N) )
			Wrong++;
	}
	Check( Wrong == 0 && P->GetPasses() == 2, FString::Printf( TEXT("%s loop on the real-time clock: %i frame times over two wraps, %i wrong, %i passes"), *Case, 2*N+2, Wrong, P->GetPasses() ) );
	delete P;

	// Real-time clock, random steps: the frame shown at time t covers t.
	P = new FUC2MoviePlayer;
	P->TestClock = TestNow;
	GTestNow = 2000.0;
	P->Open( *Video, NULL, 0, NULL );
	INT Steps = 0, MaxErr = 0;
	DOUBLE T = 0.0;
	while( !P->HasEnded() && Steps < 200000 )
	{
		T += RandomStep();
		GTestNow = 2000.0 + T;
		P->Advance();
		if( P->HasEnded() )
			break;
		MaxErr = Max( MaxErr, Abs( P->GetFrame() - Min( (INT)appFloor( T * Fps ), N-1 ) ) );
		Steps++;
	}
	Check( MaxErr == 0 && P->HasEnded() && Abs( T - N / Fps ) < 0.05, FString::Printf( TEXT("%s real-time clock: %i random steps of 5-40 ms, frame error %i, ended at t=%.3f s (length %.3f s)"),
		*Case, Steps, MaxErr, T, N / Fps ) );
	delete P;
}

static void SoundCase( const FString& Ref, const FString& Case, const FString& Video, const FString& Audio )
{
	// The sound through the player's own feed path into a simulated device, the picture on its clock.
	FUC2MoviePlayer* P = new FUC2MoviePlayer;
	FTestSink* Sink = new FTestSink( 2 );
	P->TestAudio = Sink;
	P->TestClock = TestNow;
	GTestNow = 3000.0;
	if( !P->Open( *Video, *Audio, MOVIE_Audio, NULL ) || P->GetAudioRate() <= 0 )
	{
		Check( 0, FString::Printf( TEXT("%s: cannot open %s with sound %s"), *Case, *Video, *Audio ) );
		delete P;
		delete Sink;
		return;
	}
	Sink->Channels = P->GetAudioChannels();
	GWarn->Logf( TEXT("case %s (sound): %s"), *Case, P->GetDescription() );
	INT Rate = P->GetAudioRate(), N = P->GetFrameCount(), Pass = P->GetPassFrames();
	FLOAT Fps = P->GetFrameRate();
	INT Steps = 0, MaxErr = 0;
	P->Advance();
	while( !P->HasEnded() && Steps < 400000 )
	{
		Sink->Played = Min<DOUBLE>( Sink->WrittenFrames(), Sink->Played + RandomStep() * Rate );
		P->Advance();
		if( P->HasEnded() )
			break;
		INT Expect = Min( (INT)appFloor( Sink->Played * Fps / Rate + 1e-6 ), N-1 );
		MaxErr = Max( MaxErr, Abs( P->GetFrame() - Expect ) );
		Steps++;
	}
	Check( MaxErr == 0 && P->HasEnded() && (INT)Sink->Played == Pass, FString::Printf(
		TEXT("%s A/V sync: %i random steps of 5-40 ms of simulated sound, picture vs sound clock error %i frames; ended when %i frames had played (pass %i = %i pictures x %i/%g)"),
		*Case, Steps, MaxErr, (INT)Sink->Played, Pass, N, Rate, Fps ) );

	// Sample count and values against ffmpeg's decode of the same file.
	TArray<BYTE> RefBytes;
	appLoadFileToArray( RefBytes, *(Ref + TEXT("/") + Case + TEXT(".s16")) );
	FString ExpectText;
	appLoadFileToString( ExpectText, *(Ref + TEXT("/") + Case + TEXT(".samples")) );
	INT Expected = appAtoi( *ExpectText );
	INT C = P->GetAudioChannels(), Total = P->GetAudioFrames(), RefFrames = RefBytes.Num() / (2*C);
	Check( Total == Expected && Expected > 0, FString::Printf( TEXT("%s sound length: %i frames by the end-of-stream granule, %i in the cook's manifest (XMV decoded); ffmpeg decodes %i (no end trim: +%i)"),
		*Case, Total, Expected, RefFrames, RefFrames - Total ) );
	const SWORD* R = (const SWORD*)(RefBytes.Num() ? &RefBytes(0) : NULL);
	INT Common = Min( Total, Min( RefFrames, Sink->WrittenFrames() ) ) * C;
	INT MaxDiff = 0, Over1 = 0;
	for( INT i=0; i<Common; i++ )
	{
		INT D = Abs( (INT)Sink->Written(i) - (INT)R[i] );
		MaxDiff = Max( MaxDiff, D );
		if( D > 1 )
			Over1++;
	}
	Check( Common == Total*C && MaxDiff <= 2, FString::Printf( TEXT("%s sound values: %i samples vs ffmpeg's decode, max difference %i (16-bit steps), %i differ by more than 1"),
		*Case, Common, MaxDiff, Over1 ) );
	INT NonZeroPad = 0;
	for( INT i=Total*C; i<Sink->Written.Num(); i++ )
		if( Sink->Written(i) )
			NonZeroPad++;
	Check( Sink->WrittenFrames() == Pass && NonZeroPad == 0 && Pass == appRound( N * (DOUBLE)Rate / Fps ),
		FString::Printf( TEXT("%s sound padding: %i frames written = %i pictures at %g fps x %i Hz; the %i frames after the sound's end are silence (%i not)"),
		*Case, Sink->WrittenFrames(), N, Fps, Rate, Sink->WrittenFrames() - Total, NonZeroPad ) );
	delete P;
	delete Sink;

	// Looping with sound: across the end the picture goes to frame 0 and the sound starts over seamlessly.
	P = new FUC2MoviePlayer;
	Sink = new FTestSink( C );
	P->TestAudio = Sink;
	P->TestClock = TestNow;
	P->Open( *Video, *Audio, MOVIE_Audio | MOVIE_Loop, NULL );
	INT LastFrame = -1, Wraps = 0, Jumps = 0;
	P->Advance();
	while( Sink->Played < Pass + 2.0 * Rate )
	{
		Sink->Played = Min<DOUBLE>( Sink->WrittenFrames(), Sink->Played + Rate / (DOUBLE)Fps * 0.5 );
		P->Advance();
		INT F = P->GetFrame();
		if( F < LastFrame )
		{
			Wraps++;
			if( LastFrame != N-1 || F != 0 )
				Jumps++;
		}
		else if( LastFrame >= 0 && F > LastFrame + 1 )
			Jumps++;
		LastFrame = F;
	}
	INT Seam = 0;
	for( INT i=0; i<Min( Total, 44100 )*C && Pass*C + i < Sink->Written.Num(); i++ )
		if( Sink->Written(Pass*C + i) != Sink->Written(i) )
			Seam++;
	Check( Wraps == 1 && Jumps == 0 && Seam == 0, FString::Printf( TEXT("%s loop with sound: %i wrap (frame %i -> 0), %i skipped frames, second pass's first second of sound identical to the first (%i samples differ)"),
		*Case, Wraps, N-1, Jumps, Seam ) );
	delete P;
	delete Sink;
}

static void LanguageChecks()
{
	FString Saved = UObject::GetLanguage();
	struct { const TCHAR* Lang; const TCHAR* Name; const TCHAR* Video; const TCHAR* Audio; } Cases[] =
	{
		{ TEXT("int"), TEXT("D:\\Movies\\03_AscPre3.xmv"), TEXT("../Movies/03_AscPre3.ogv"), TEXT("../Movies/03_AscPre3.ogv") },
		{ TEXT("deu"), TEXT("D:\\Movies\\03_AscPre3.xmv"), TEXT("../Movies/03_AscPre3.ogv"), TEXT("../Movies/03_AscPre3_deu.oga") },
		{ TEXT("fra"), TEXT("D:\\Movies\\03_AscPre3.xmv"), TEXT("../Movies/03_AscPre3.ogv"), TEXT("../Movies/03_AscPre3_fra.oga") },
		{ TEXT("ita"), TEXT("D:\\Movies\\03_AscPre3.xmv"), TEXT("../Movies/03_AscPre3.ogv"), TEXT("../Movies/03_AscPre3_ita.oga") },
		{ TEXT("spa"), TEXT("D:\\Movies\\03_AscPre3.xmv"), TEXT("../Movies/03_AscPre3.ogv"), TEXT("../Movies/03_AscPre3_spa.oga") },
		{ TEXT("jpn"), TEXT("D:\\Movies\\03_AscPre3.xmv"), TEXT("../Movies/03_AscPre3.ogv"), TEXT("../Movies/03_AscPre3.ogv") },
		{ TEXT("deu"), TEXT("..\\Movies\\UC2_Logos.xmv"), TEXT("../Movies/UC2_Logos.ogv"), TEXT("../Movies/UC2_Logos.ogv") },
		{ TEXT("int"), TEXT("D:\\Textures\\Movies\\Portrait_Gametype_NS.xmv"), TEXT("../Textures/Movies/Portrait_GameType_NS.ogv"), TEXT("../Textures/Movies/Portrait_GameType_NS.ogv") },
		{ TEXT("int"), TEXT("D:\\Textures\\Movies\\CH_Flak_Fire.xmv"), TEXT(""), TEXT("") },
	};
	for( INT i=0; i<ARRAY_COUNT(Cases); i++ )
	{
		UObject::SetLanguage( Cases[i].Lang );
		FString Video, Audio, Note;
		UBOOL bFound = appFindMovie( Cases[i].Name, Video, Audio, &Note );
		UBOOL bOk = *Cases[i].Video ? (bFound && Video == Cases[i].Video && Audio == Cases[i].Audio) : !bFound;
		Check( bOk, FString::Printf( TEXT("language %s, %s -> video %s, sound %s (expected %s, %s)%s%s"), Cases[i].Lang, Cases[i].Name,
			bFound ? *Video : TEXT("none"), bFound ? *Audio : TEXT("none"), *Cases[i].Video ? Cases[i].Video : TEXT("none"),
			*Cases[i].Audio ? Cases[i].Audio : TEXT("none"), Note.Len() ? TEXT(": ") : TEXT(""), *Note ) );
	}
	UObject::SetLanguage( *Saved );
}

INT UUC2MovieSelfTestCommandlet::Main( const TCHAR* Parms )
{
	guard(UUC2MovieSelfTestCommandlet::Main);
	GChecks = GFailed = 0;
	FString Ref;
	if( !Parse( Parms, TEXT("REF="), Ref ) )
	{
		GWarn->Logf( TEXT("usage: ucc-bin UC2Movie.UC2MovieSelfTest REF=<dir written by tools/dev/movie-selftest.sh>") );
		return 1;
	}
	TArray<FString> Lines, Words;
	if( !LoadText( Ref + TEXT("/cases.txt"), Lines ) )
	{
		GWarn->Logf( TEXT("FAIL no %s/cases.txt"), *Ref );
		return 1;
	}
	for( INT i=0; i<Lines.Num(); i++ )
	{
		SplitWords( Lines(i), Words, TEXT(" ") );
		if( Words.Num() < 5 )
			continue;
		VideoCase( Ref, Words(0), Words(1) );
		if( Words(2) != TEXT("-") )
			SoundCase( Ref, Words(0), Words(1), Words(2) );
	}
	LanguageChecks();
	GWarn->Logf( TEXT("UC2MovieSelfTest: %i checks, %i failed"), GChecks, GFailed );
	return GFailed ? 1 : 0;
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
