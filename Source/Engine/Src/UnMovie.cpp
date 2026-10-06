/*=============================================================================
	UnMovie.cpp: the engine side of movie playback on every platform (engine-uc2, M9).

	What XboxDrv/Src/XboxMovieThread.cpp and XMVHelper.cpp did around the XDK's decoder, written once for any
	render device, audio device and movie player (Engine/Inc/UnMovie.h):
	  - finding the cooked movie for a name the content gives, with the Xbox's audio-language rule;
	  - full-screen playback (start-up logos, ladder cinematics) in viewport 0, blocking, skippable, with
	    the "hide subtitles" bands and the "LOADING..." text of XboxMovieThread::Render;
	  - the loading screen (static: see PlayLoadingMovie);
	  - the MovieMaterial update a render device's SetMovieMaterial calls (D3DMaterialState.cpp:4539);
	  - the hooks UnGame.cpp calls (PlayIntroCinematic, PlayOutroCinematic, PlayLoadingMovie, SetEngineReady,
	    IsPlayingMovie: the names and signatures of XboxMovieThread.cpp).
	No thread: the Xbox ran full-screen movies on a thread; here they run in the calling thread's loop, which
	is also what the single-threaded web build can do. Design and evidence: docs/porting/analysis/video.md.
=============================================================================*/

#include "EnginePrivate.h"
#include "UnMovie.h"

#if !defined(_XBOX)

/*-----------------------------------------------------------------------------
	Globals.
-----------------------------------------------------------------------------*/

ENGINE_API FPortableMoviePlayer* (*GCreateMoviePlayer)() = NULL;
ENGINE_API UBOOL GShowSubtitles = 1;	// XboxClient.cpp:40

// Full-screen playback state (one movie at a time, as GMovieThread on the Xbox).
static UBOOL	GFullscreenMovie		= 0;
static UBOOL	GMovieSkipRequested		= 0;
static DOUBLE	GMovieSkipArmTime		= 0.0;

// Where the cooked movies are, relative to System/ (tools/cook-movies.py; the Xbox read D:\Movies and
// D:\Textures\Movies).
#define MOVIE_DIR			TEXT("../Movies")
#define TEXTURE_MOVIE_DIR	TEXT("../Textures/Movies")

/*-----------------------------------------------------------------------------
	Finding a movie.
-----------------------------------------------------------------------------*/

static FString MovieBaseName( const TCHAR* Name )
{
	FString S = Name;
	INT Slash = Max( S.InStr(TEXT("\\"),1), S.InStr(TEXT("/"),1) );
	if( Slash >= 0 )
		S = S.Mid( Slash+1 );
	INT Dot = S.InStr( TEXT("."), 1 );
	if( Dot >= 0 )
		S = S.Left( Dot );
	return S;
}

// Dir/Base+Ext, matching the base name without regard to case: the content names files as the Xbox's
// case-insensitive file system allowed (Portrait_Gametype_NS for Portrait_GameType_NS.xmv).
static UBOOL FindCookedFile( const TCHAR* Dir, const FString& Base, const TCHAR* Ext, FString& Out )
{
	FString Exact = FString::Printf( TEXT("%s/%s%s"), Dir, *Base, Ext );
	if( GFileManager->FileSize(*Exact) > 0 )
	{
		Out = Exact;
		return 1;
	}
	TArray<FString> Files = GFileManager->FindFiles( *FString::Printf(TEXT("%s/*%s"), Dir, Ext), 1, 0 );
	FString Want = Base + Ext;
	for( INT i=0; i<Files.Num(); i++ )
	{
		if( appStricmp( *Files(i), *Want ) == 0 )
		{
			Out = FString::Printf( TEXT("%s/%s"), Dir, *Files(i) );
			return 1;
		}
	}
	return 0;
}

// XMVHelper.cpp:160-189 GetLocalizedAudioStream: German 1, French 2, Italian 3, Spanish 4, else 0.
static INT LanguageTrack( const FString& Lang )
{
	static const TCHAR* Langs[] = { TEXT("int"), TEXT("deu"), TEXT("fra"), TEXT("ita"), TEXT("spa") };
	for( INT i=1; i<ARRAY_COUNT(Langs); i++ )
		if( appStricmp( *Lang, Langs[i] ) == 0 )
			return i;
	return 0;
}

UBOOL appFindMovie( const TCHAR* Name, FString& VideoFile, FString& AudioFile, FString* LanguageNote )
{
	guard(appFindMovie);
	VideoFile = AudioFile = TEXT("");
	if( !Name || !*Name )
		return 0;
	FString Base = MovieBaseName( Name );
	FString Lower = FString(Name).Caps();
	UBOOL bTextureFirst = appStrstr( *Lower, TEXT("TEXTURES\\MOVIES") ) || appStrstr( *Lower, TEXT("TEXTURES/MOVIES") );
	const TCHAR* Dirs[2] = { bTextureFirst ? TEXTURE_MOVIE_DIR : MOVIE_DIR, bTextureFirst ? MOVIE_DIR : TEXTURE_MOVIE_DIR };
	for( INT d=0; d<2 && !VideoFile.Len(); d++ )
		FindCookedFile( Dirs[d], Base, TEXT(".ogv"), VideoFile );
	if( !VideoFile.Len() )
		return 0;

	// The sound: the international track is in the .ogv itself; another language's track is a file of its
	// own, <Name>_<lang>.oga, which the cook leaves out when the movie has no such track (or it is identical).
	AudioFile = VideoFile;
	FString Lang = FString(UObject::GetLanguage()).Locs();
	INT Track = LanguageTrack( Lang );
	FString Dir = VideoFile.Left( VideoFile.InStr(TEXT("/"),1) );
	FString CookedBase = MovieBaseName( *VideoFile );
	if( Track > 0 )
	{
		FString LangFile;
		if( FindCookedFile( *Dir, CookedBase + TEXT("_") + Lang, TEXT(".oga"), LangFile ) )
		{
			AudioFile = LangFile;
			if( LanguageNote )
				*LanguageNote = FString::Printf( TEXT("language %s: audio track %i from %s"), *Lang, Track, *LangFile );
		}
		else if( LanguageNote )
			*LanguageNote = FString::Printf( TEXT("language %s: no track %i of its own, falling back to the international track (Xbox: English)"), *Lang, Track );
	}
	else if( LanguageNote )
		*LanguageNote = FString::Printf( TEXT("language %s: international track 0"), *Lang );
	return 1;
	unguard;
}

/*-----------------------------------------------------------------------------
	Saying why movies do not play (O-1): once in the log per movie, once for the player.
-----------------------------------------------------------------------------*/

static FString	GMovieNotice;			// waiting to be shown on the console (appTakeMovieNotice)
static UBOOL	GMovieNoticeGiven = 0;	// the "no movies at all" notice is given once per run

static void SetMovieNotice( const FString& Text )
{
	if( GMovieNoticeGiven )
		return;
	GMovieNoticeGiven = 1;
	GMovieNotice = Text;
	debugf( NAME_Init, TEXT("UC2Movie: %s"), *Text );
}

UBOOL appTakeMovieNotice( FString& Out )
{
	if( !GMovieNotice.Len() )
		return 0;
	Out = GMovieNotice;
	GMovieNotice = TEXT("");
	return 1;
}

// Is there any cooked movie at all? (A run tree or web manifest without the cooked folders: every movie is off.)
static UBOOL AnyCookedMovie()
{
	static INT Known = -1;
	if( Known < 0 )
		Known = GFileManager->FindFiles( MOVIE_DIR TEXT("/*.ogv"), 1, 0 ).Num() + GFileManager->FindFiles( TEXTURE_MOVIE_DIR TEXT("/*.ogv"), 1, 0 ).Num() > 0;
	return Known;
}

// Each missing movie once (the same name is asked for again by every material and level that uses it).
static UBOOL FirstTimeFor( const TCHAR* Kind, const TCHAR* Name )
{
	static TMap<FString,INT> Seen;
	FString Key = FString(Kind) + TEXT(":") + FString(Name).Caps();
	if( Seen.Find( Key ) )
		return 0;
	Seen.Set( *Key, 1 );
	return 1;
}

FPortableMoviePlayer* appOpenMovie( const TCHAR* Name, DWORD Flags, UAudioSubsystem* Audio )
{
	guard(appOpenMovie);
	if( !GCreateMoviePlayer )
	{
		static UBOOL Logged = 0;
		if( !Logged )
			debugf( NAME_Log, TEXT("UC2Movie: this build has no movie player: %s and every other movie stay off"), Name );
		Logged = 1;
		SetMovieNotice( TEXT("Movies are off: this build has no movie player.") );
		return NULL;
	}
	FString Video, AudioFile, Note;
	if( !appFindMovie( Name, Video, AudioFile, &Note ) )
	{
		if( !AnyCookedMovie() )
			SetMovieNotice( TEXT("Movies are off: there are no cooked movies in ../Movies or ../Textures/Movies ")
				TEXT("(tools/cook-movies.py makes them; tools/dev/mkrun-uc2.sh links them into a run tree; a web build needs them in its file manifest).") );
		if( FirstTimeFor( TEXT("missing"), Name ) )
			debugf( NAME_Log, TEXT("UC2Movie: no cooked movie for %s (%s)"), Name,
				AnyCookedMovie() ? TEXT("the cooked tree has no such movie; names the content references but the game never shipped, e.g. CH_*, failed to open on the Xbox too")
					: TEXT("no cooked movies at all: tools/cook-movies.py") );
		return NULL;
	}
	FPortableMoviePlayer* Player = GCreateMoviePlayer();
	if( !Player )
		return NULL;
	if( !Player->Open( *Video, (Flags & MOVIE_Audio) ? *AudioFile : NULL, Flags, (Flags & MOVIE_Audio) ? Audio : NULL ) )
	{
		debugf( NAME_Log, TEXT("UC2Movie: failed to open %s (%s)"), *Video, Name );
		delete Player;
		return NULL;
	}
	debugf( NAME_Log, TEXT("UC2Movie: opened %s for %s: %s%s%s"), *Video, Name, Player->GetDescription(),
		(Flags & MOVIE_Audio) ? TEXT("; ") : TEXT(""), (Flags & MOVIE_Audio) ? *Note : TEXT("") );
	return Player;
	unguard;
}

/*-----------------------------------------------------------------------------
	Full-screen playback.
-----------------------------------------------------------------------------*/

static UViewport* MovieViewport()
{
	UEngine* Engine = UEngine::GEngine;
	if( !Engine || !Engine->Client || Engine->Client->Viewports.Num()==0 )
		return NULL;
	UViewport* Viewport = Engine->Client->Viewports(0);
	// (No window yet: the menu's LoadMap inside UGameEngine::Init runs before Viewport->OpenWindow.)
	if( !Viewport || !Viewport->RenDev || !Viewport->Canvas || !Viewport->Actor || !Viewport->GetWindow() )
		return NULL;
	return Viewport;
}

UBOOL appCanPlayFullscreenMovie( EFullscreenMovieKind Kind )
{
	guard(appCanPlayFullscreenMovie);
	if( !GIsClient || GIsEditor || !GCreateMoviePlayer )
		return 0;
	UViewport* Viewport = MovieViewport();
	if( !Viewport )
		return 0;
	// The null render device draws nothing (and a dedicated server or a commandlet has no viewport).
	// -NULLMOVIES (tests, O-2): play them there all the same - the file access, the decoding, the sound and the
	// clock run as usual, only the picture is not drawn (the web build's audio and movie path before its GL
	// device exists; a headless run measuring decode cost).
	if( appStricmp( Viewport->RenDev->GetClass()->GetName(), TEXT("NullRenderDevice") ) == 0 && !ParseParam( appCmdLine(), TEXT("NULLMOVIES") ) )
		return 0;
	// -NOMOVIE: no full-screen movies at all; -NOSTARTUPMOVIES: no start-up logos (Epic's UE3 switches; UE2
	// had none). The same per kind in Avaris.ini: [UC2Movie] StartupMovies, Cinematics, LoadingMovie.
	if( ParseParam( appCmdLine(), TEXT("NOMOVIE") ) )
		return 0;
	if( Kind == FSMOVIE_Startup && ParseParam( appCmdLine(), TEXT("NOSTARTUPMOVIES") ) )
		return 0;
	const TCHAR* Key = Kind == FSMOVIE_Startup ? TEXT("StartupMovies") : Kind == FSMOVIE_Cinematic ? TEXT("Cinematics") : TEXT("LoadingMovie");
	UBOOL bOn = 1;
	GConfig->GetBool( TEXT("UC2Movie"), Key, bOn );
	return bOn;
	unguard;
}

UBOOL appMovieInputEvent( EInputKey Key, EInputAction Action )
{
	if( !GFullscreenMovie )
		return 0;
	// XboxMovieThread::WasInterruptRequested: any button. Here any key, mouse button or pad button press
	// (not mouse or stick motion), from half a second into the movie on (the Xbox armed the skip after
	// CreateMovie's 0.5 s sleep), so the press that started a travel does not also skip its cinematic.
	if( Action == IST_Press && appSeconds() >= GMovieSkipArmTime )
		GMovieSkipRequested = 1;
	return 1;	// a full-screen movie owns the input: nothing reaches the game or the menus meanwhile
}

// A transient MovieMaterial around a player we drive ourselves, so the render device draws it through the
// same SetMovieMaterial path as any MovieMaterial.
static UMovieMaterial* WrapPlayer( FPortableMoviePlayer* Player, const TCHAR* Name )
{
	UMovieMaterial* M = ConstructObject<UMovieMaterial>( UMovieMaterial::StaticClass(), UObject::GetTransientPackage(), NAME_None, RF_Transient );
	M->AddToRoot();
	M->MovieFileName = Name;
	M->MovieDecoder = (PTRINT)Player;
	M->bUpdateWhenPaused = 1;	// the level does not tick while a full-screen movie plays
	M->USize = Player->GetWidth();
	M->VSize = Player->GetHeight();
	M->UClampMode = TC_Clamp;
	M->VClampMode = TC_Clamp;
	return M;
}

static void UnwrapPlayer( UMovieMaterial* M )
{
	M->MovieDecoder = 0;	// the caller deletes the player
	M->RemoveFromRoot();
	M->ConditionalDestroy();
}

// -movieshot=<seconds>[,<seconds>...]: the engine's SHOT exec when the first full-screen movie's clock passes
// those times (verification: the picture is then a known frame of the cooked file, which the log names).
static TArray<FLOAT> GMovieShotTimes;
static void ParseMovieShots()
{
	static UBOOL Parsed = 0;
	if( Parsed )
		return;
	Parsed = 1;
	// (Parse() stops at a comma, so the list is read by hand up to the next space.)
	FString Cmd = FString(appCmdLine()).Caps();
	INT At = Cmd.InStr( TEXT("MOVIESHOT=") );
	if( At < 0 )
		return;
	FString List = Cmd.Mid( At + 10 );
	if( List.InStr( TEXT(" ") ) >= 0 )
		List = List.Left( List.InStr( TEXT(" ") ) );
	while( List.Len() )
	{
		INT Comma = List.InStr( TEXT(",") );
		FString Item = Comma >= 0 ? List.Left(Comma) : List;
		List = Comma >= 0 ? List.Mid(Comma+1) : FString();
		if( Item.Len() )
			GMovieShotTimes.AddItem( appAtof(*Item) );
	}
}

// One frame of XboxMovieThread::Render: clear, the movie on a quad keeping its aspect, the subtitle bands,
// the message, present.
static void DrawMovieFrame( UViewport* Viewport, UMovieMaterial* M, FPortableMoviePlayer* Player, const TCHAR* Message, UBOOL bHideSubtitles )
{
	if( !Viewport->Lock() )
		return;
	UCanvas* Canvas = Viewport->Canvas;
	Canvas->Update();
	Viewport->RI->Clear( 1, FColor(0,0,0), 1, 1.f );
	FLOAT SX = Viewport->SizeX, SY = Viewport->SizeY;
	FLOAT W = SX, H = SX * Player->GetHeight() / Max(Player->GetWidth(),1);
	if( H > SY )
	{
		H = SY;
		W = SY * Player->GetWidth() / Max(Player->GetHeight(),1);
	}
	FLOAT X = (SX - W) * 0.5f, Y = (SY - H) * 0.5f;
	if( Player->GetFrame() >= 0 )
		Canvas->DrawTile( M, X, Y, W, H, 0.f, 0.f, Player->GetWidth(), Player->GetHeight(), 0.f, FPlane(1.f,1.f,1.f,1.f) );
	FLOAT Scale = H / 480.f;	// the Xbox drew everything on a 640x480 screen
	if( bHideSubtitles )
	{
		// HideSubtitles (XboxMovieThread.cpp:134-145): black over rows 0-96 and 384-480.
		static UConstantColor* Black = NULL;
		if( !Black )
		{
			Black = ConstructObject<UConstantColor>( UConstantColor::StaticClass(), UObject::GetTransientPackage(), NAME_None, RF_Transient );
			Black->Color = FColor(0,0,0,255);
			Black->AddToRoot();
		}
		Canvas->DrawTile( Black, X, Y, W, 96.f*Scale, 0, 0, 1, 1, 0.f, FPlane(1,1,1,1) );
		Canvas->DrawTile( Black, X, Y + 384.f*Scale, W, 96.f*Scale, 0, 0, 1, 1, 0.f, FPlane(1,1,1,1) );
	}
	if( Message && *Message && Canvas->SmallFont )
	{
		// RenderMapName (XboxMovieThread.cpp:101-129): grey, right-aligned at x=598, centred in rows 420-450.
		FLOAT XL = 0.f, YL = 0.f;
		Canvas->ClippedStrLen( Canvas->SmallFont, 1.f, 1.f, XL, YL, Message );
		Canvas->CurX = X + 598.f*Scale - XL;
		Canvas->CurY = Y + 435.f*Scale - YL*0.5f;
		Canvas->Color = FColor(150,150,150,255);
		Canvas->ClippedPrint( Canvas->SmallFont, 1.f, 1.f, 0, Message );
	}
	// The canvas batches its tiles: draw them before the frame ends (UGameEngine::Draw does the same).
	if( Canvas->pCanvasUtil )
		Canvas->pCanvasUtil->Flush();
	Viewport->Unlock();
	Viewport->Present();
}

UBOOL appPlayMovieFullscreen( const TCHAR* Name, EFullscreenMovieKind Kind, UBOOL bSkippable )
{
	guard(appPlayMovieFullscreen);
	if( !Name || !*Name || GFullscreenMovie || !appCanPlayFullscreenMovie( Kind ) )
		return 0;
	UViewport* Viewport = MovieViewport();
	UEngine* Engine = UEngine::GEngine;
	FPortableMoviePlayer* Player = appOpenMovie( Name, MOVIE_Audio | MOVIE_ExclusiveAudio, Engine->Audio );
	if( !Player )
	{
		// Say why on the console once (outside rendering: this is the place); the log has it already.
		FString Notice;
		if( Viewport->Actor && appTakeMovieNotice( Notice ) )
		{
			debugf( NAME_Log, TEXT("UC2Movie: on the console: %s"), *Notice );
			Viewport->Actor->eventClientMessage( Notice, NAME_None );
		}
		return 0;
	}
	ParseMovieShots();
	UMovieMaterial* M = WrapPlayer( Player, Name );
	// Only XboxMovieThread (cinematics, credits) hid the bands, and never for the credits (bIsCreditsMovie);
	// the start-up logos went through FInitialMovieThread, which drew the whole picture.
	UBOOL bHideSubtitles = Kind == FSMOVIE_Cinematic && !GShowSubtitles && !appStrstr( Name, TEXT("UC2_Credits") );
	debugf( NAME_Log, TEXT("UC2Movie: subtitles %s (SUBTITLES; the profile's cinematic captions)%s"), GShowSubtitles ? TEXT("on") : TEXT("off"),
		bHideSubtitles ? TEXT(": the subtitle bands are hidden") : TEXT("") );

	GFullscreenMovie = 1;
	GMovieSkipRequested = 0;
	DOUBLE Start = appSeconds();
	GMovieSkipArmTime = Start + 0.5;
	debugf( NAME_Log, TEXT("UC2Movie: full-screen %s (%ix%i, %i frames at %.0f fps, viewport %ix%i)%s"), Name,
		Player->GetWidth(), Player->GetHeight(), Player->GetFrameCount(), Player->GetFrameRate(),
		Viewport->SizeX, Viewport->SizeY, bSkippable ? TEXT(", skippable") : TEXT("") );

	const TCHAR* Ending = TEXT("ended");
	INT Presented = 0, LastLogged = -1;
	DOUBLE LastTime = Start;
	// Decode cost (O-2): the time Advance takes (reading, decoding, converting, feeding the sound), per call.
	DOUBLE AdvanceTotal = 0.0, AdvanceMax = 0.0;
	INT Advances = 0, PictureChanges = 0;
	while( 1 )
	{
		DOUBLE Now = appSeconds();
		// Pump the window: input goes to appMovieInputEvent (the game and the menus see none of it).
		Viewport->UpdateInput( 0, (FLOAT)(Now - LastTime) );
		LastTime = Now;
		if( GIsRequestingExit )
		{
			Ending = TEXT("stopped: the game is exiting");
			break;
		}
		if( bSkippable && GMovieSkipRequested )
		{
			Ending = TEXT("skipped by a key or button");
			break;
		}
		DOUBLE A0 = appSeconds();
		if( Player->Advance() )
			PictureChanges++;
		DOUBLE A1 = appSeconds() - A0;
		AdvanceTotal += A1;
		AdvanceMax = Max( AdvanceMax, A1 );
		Advances++;
		if( !Player->IsPlaying() )
			break;
		DrawMovieFrame( Viewport, M, Player, NULL, bHideSubtitles );
		Presented++;
		INT Frame = Player->GetFrame();
		if( Frame / 30 != LastLogged / 30 || LastLogged < 0 )
		{
			// One line a second: the movie clock, the frame on screen, real time since the start.
			debugf( NAME_Log, TEXT("UC2Movie: t=%.3f frame %i, %.3f s after start"), Player->GetTime(), Frame, appSeconds() - Start );
			LastLogged = Frame;
		}
		if( GMovieShotTimes.Num() && Player->GetTime() >= GMovieShotTimes(0) )
		{
			debugf( NAME_Log, TEXT("UC2Movie: shot of %s at movie time %.3f s (asked %.3f s): frame %i of the cooked file"),
				Name, Player->GetTime(), GMovieShotTimes(0), Frame );
			GMovieShotTimes.Remove(0);
			Viewport->Exec( TEXT("SHOT"), *GLog );
		}
		// Sleep until the next picture is due, at most 10 ms (the sound queue is refilled by Advance).
		DOUBLE Due = (Frame + 1) / Max(Player->GetFrameRate(),1.f) - Player->GetTime();
		appSleep( (FLOAT)Clamp( Due, 0.002, 0.010 ) );
	}
	debugf( NAME_Log, TEXT("UC2Movie: %s %s at movie time %.3f s, frame %i of %i, %i pictures presented in %.3f s"),
		Name, Ending, Player->GetTime(), Player->GetFrame(), Player->GetFrameCount(), Presented, appSeconds() - Start );
	debugf( NAME_Log, TEXT("UC2Movie: decode cost of %s: %i updates, %i new pictures, %.2f ms per update on average, %.2f ms at most"),
		Name, Advances, PictureChanges, Advances ? 1000.0 * AdvanceTotal / Advances : 0.0, 1000.0 * AdvanceMax );
	GFullscreenMovie = 0;
	UnwrapPlayer( M );
	delete Player;	// closes the sound; the device resumes the others
	return 1;
	unguard;
}

/*-----------------------------------------------------------------------------
	MovieMaterial (D3DMaterialState.cpp:4539-4624 on the Xbox).
-----------------------------------------------------------------------------*/

// What a MovieMaterial shows while it has no picture: an opaque black 4x4 picture that never changes (O-1).
// Without it the device fell back to Material.DefaultMaterial, UC2's bright green DefaultTexture: every movie
// texture of a build without the cooked movies (the web build so far) was a green square.
class FBlackMoviePicture : public FPortableMoviePlayer
{
public:
	enum { SIZE = 4 };
	BYTE Pixels[SIZE*SIZE*4];
	FBlackMoviePicture()
	{
		for( INT i=0; i<SIZE*SIZE; i++ )
		{
			Pixels[i*4+0] = Pixels[i*4+1] = Pixels[i*4+2] = 0;
			Pixels[i*4+3] = 255;
		}
	}
	UBOOL Open( const TCHAR*, const TCHAR*, DWORD, UAudioSubsystem* ) { return 1; }
	UBOOL Advance() { return 0; }
	UBOOL IsPlaying() { return 0; }
	UBOOL HasEnded() { return 0; }
	void  Pause( UBOOL ) {}
	void  Rewind() {}
	INT   GetWidth() { return SIZE; }
	INT   GetHeight() { return SIZE; }
	INT   GetFrame() { return 0; }
	INT   GetFrameCount() { return 1; }
	FLOAT GetFrameRate() { return 30.f; }
	DOUBLE GetTime() { return 0.0; }
	// A revision no real picture has (a device keeps "revision + 1" of what it uploaded, 0 meaning none).
	INT   GetRevision() { return -2; }
	const BYTE* GetBGRA() { return Pixels; }
	const TCHAR* GetDescription() { return TEXT("black (no picture)"); }
};
static FBlackMoviePicture GBlackMoviePicture;

// No picture for M: its LoadingFallback when it has one (the Xbox's choice while a movie loaded), else black.
static FPortableMoviePlayer* NoPicture( UMovieMaterial* M, UBOOL& bOutFallback, const TCHAR* Why )
{
	if( FirstTimeFor( Why, M->GetPathName() ) )
		debugf( NAME_Log, TEXT("UC2Movie: MovieMaterial %s shows %s: %s"), M->GetPathName(),
			M->LoadingFallback ? *FString::Printf( TEXT("its LoadingFallback %s"), M->LoadingFallback->GetPathName() ) : TEXT("black"), Why );
	if( M->LoadingFallback )
	{
		bOutFallback = 1;
		return NULL;
	}
	if( M->USize <= 0 || M->VSize <= 0 )
	{
		M->USize = FBlackMoviePicture::SIZE;
		M->VSize = FBlackMoviePicture::SIZE;
	}
	return &GBlackMoviePicture;
}

FPortableMoviePlayer* appUpdateMovieMaterial( UMovieMaterial* M, UBOOL& bOutFallback )
{
	guard(appUpdateMovieMaterial);
	bOutFallback = 0;
	FPortableMoviePlayer* Player = (FPortableMoviePlayer*)M->MovieDecoder;
	// bReset: start again (CXMVPlayer::Reset).
	if( M->bReset )
	{
		if( Player )
			Player->Rewind();
		M->bHasError = M->bReset = 0;
	}
	// Open on first use. bDecodeFromMemory needs no background load here: the player streams from the file
	// with a bounded buffer (the Xbox read such movies whole into memory first, showing LoadingFallback).
	if( !Player && !M->bHasError )
	{
		UEngine* Engine = UEngine::GEngine;
		DWORD Flags = (M->bPlayLooping ? MOVIE_Loop : 0) | (M->bEnableAudio ? MOVIE_Audio : 0) | (M->bManualFramerateSynch ? MOVIE_ManualSync : 0);
		Player = appOpenMovie( *M->MovieFileName, Flags, Engine ? Engine->Audio : NULL );
		if( Player )
			M->MovieDecoder = (PTRINT)Player;
		else
		{
			// So we don't try this material again (the Xbox logged "Failed to open movie" to the artist).
			M->bHasError = 1;
			debugf( NAME_Log, TEXT("UC2Movie: MovieMaterial %s: failed to open movie (%s)"), M->GetPathName(), *M->MovieFileName );
		}
	}
	if( M->bHasError || !Player )
		return NoPicture( M, bOutFallback, TEXT("its movie cannot be opened (see the line above)") );
	if( M->bNeedsFrameUpdate )
	{
		UBOOL bWasPlaying = !Player->HasEnded();
		Player->Advance();
		// XMV_ENDOFFILE on a non-looping movie: tell whoever listens (once; the Xbox decoder kept answering
		// ENDOFFILE on every later call, which would call the delegate every frame).
		if( bWasPlaying && Player->HasEnded() && OBJ_DELEGATE_IS_SET(M,OnDonePlaying) )
			M->delegateOnDonePlaying();
	}
	// No picture yet: nothing decoded, or nothing converted (revision 0: the first packets were rejected or
	// duplicates, so the player's buffer holds no picture).
	if( Player->GetFrame() < 0 || Player->GetRevision() == 0 )
		return NoPicture( M, bOutFallback, TEXT("no picture decoded yet") );
	M->USize = Player->GetWidth();
	M->VSize = Player->GetHeight();
	// Evidence for the logs: each material's frame, at most every 2 s.
	static TMap<INT,DOUBLE> LastLogged;
	DOUBLE Now = appSeconds();
	DOUBLE* Last = LastLogged.Find( M->GetIndex() );
	if( !Last || Now - *Last >= 2.0 )
	{
		LastLogged.Set( M->GetIndex(), Now );
		debugf( NAME_Log, TEXT("UC2Movie: MovieMaterial %s frame %i of %i (pass %s, revision %i)"), M->GetPathName(),
			Player->GetFrame(), Player->GetFrameCount(), Player->HasEnded() ? TEXT("ended") : TEXT("playing"), Player->GetRevision() );
	}
	return Player;
	unguard;
}

/*-----------------------------------------------------------------------------
	The hooks UnGame.cpp calls (XboxMovieThread.cpp:207-505, same names and signatures).
-----------------------------------------------------------------------------*/

// GetMovieInfo (XboxMovieThread.cpp:358): the name from the URL option, ".xmv" added when it has no dot.
static FString MovieFromURL( const FURL& URL, const TCHAR* Option )
{
	const TCHAR* Value = URL.GetOption( Option, NULL );
	if( !Value )
		return FString();
	if( *Value == '=' )		// "For some silly reason GetOption() can return the '='"
		Value++;
	FString Name = FString(TEXT("D:\\Movies\\")) + Value;
	if( FString(Value).InStr(TEXT("."),1) == -1 )
		Name += TEXT(".xmv");
	return Name;
}

UBOOL IsPlayingMovie(void)
{
	// The Xbox answered "the loading movie's thread is running". Here a full-screen movie runs only inside
	// appPlayMovieFullscreen, which returns when it is over, so the engine never sees one running.
	return GFullscreenMovie;
}

void SetEngineReady(void)
{
	// Nothing runs in the background to stop (the Xbox stopped its loading movie's thread here).
}

void PlayOutroCinematic(const FURL& URL)
{
	guard(PlayOutroCinematic);
	FString Name = MovieFromURL( URL, TEXT("EndCinematic") );
	if( Name.Len() )
		appPlayMovieFullscreen( *Name, FSMOVIE_Cinematic, 1 );
	unguard;
}

void PlayIntroCinematic(const FURL& URL)
{
	guard(PlayIntroCinematic);
	FString Name = MovieFromURL( URL, TEXT("StartCinematic") );
	if( Name.Len() )
		appPlayMovieFullscreen( *Name, FSMOVIE_Cinematic, 1 );
	unguard;
}

// The Xbox played UI_LoadingMovie.xmv on its own thread for the whole of LoadMap (looping, with
// "LOADING..."). The loading here blocks the only thread there is, and drawing from inside the loader would
// re-enter the renderer while the level is being replaced, so the animated version stays off: this draws the
// movie's first picture with the message once, as a static loading screen, before the load starts.
void PlayLoadingMovie(const FURL& URL,UBOOL bSinglePlayer)
{
	guard(PlayLoadingMovie);
	if( GFullscreenMovie || !appCanPlayFullscreenMovie( FSMOVIE_Loading ) )
		return;
	UViewport* Viewport = MovieViewport();
	FPortableMoviePlayer* Player = appOpenMovie( TEXT("D:\\Textures\\Movies\\UI_LoadingMovie.xmv"), 0, NULL );
	if( !Player )
		return;
	Player->Advance();
	if( Player->GetFrame() >= 0 )
	{
		// BuildLoadingMessage (XboxMovieThread.cpp:426-453): "Loading" + "...", upper case.
		FString Msg = FString::Printf( LocalizeProgress(TEXT("Loading"),TEXT("Core")), TEXT("") );
		while( Msg.Len() && Msg.Right(1) == TEXT(" ") )
			Msg = Msg.LeftChop(1);
		Msg = (Msg + TEXT("...")).Caps();
		UMovieMaterial* M = WrapPlayer( Player, TEXT("UI_LoadingMovie") );
		DrawMovieFrame( Viewport, M, Player, *Msg, 0 );
		UnwrapPlayer( M );
		debugf( NAME_Log, TEXT("UC2Movie: loading screen for %s (static: the first picture of UI_LoadingMovie, \"%s\")"), *URL.Map, *Msg );
	}
	delete Player;
	unguard;
}

#endif // !_XBOX

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
