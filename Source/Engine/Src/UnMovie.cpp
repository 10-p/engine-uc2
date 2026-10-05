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

FPortableMoviePlayer* appOpenMovie( const TCHAR* Name, DWORD Flags, UAudioSubsystem* Audio )
{
	guard(appOpenMovie);
	if( !GCreateMoviePlayer )
	{
		static UBOOL Logged = 0;
		if( !Logged )
			debugf( NAME_Log, TEXT("UC2Movie: this build has no movie player: %s and every other movie stay off"), Name );
		Logged = 1;
		return NULL;
	}
	FString Video, AudioFile, Note;
	if( !appFindMovie( Name, Video, AudioFile, &Note ) )
	{
		debugf( NAME_Log, TEXT("UC2Movie: no cooked movie for %s (tools/cook-movies.py)"), Name );
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
	if( !Viewport || !Viewport->RenDev || !Viewport->Canvas || !Viewport->Actor )
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
	if( appStricmp( Viewport->RenDev->GetClass()->GetName(), TEXT("NullRenderDevice") ) == 0 )
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
	M->MovieDecoder = (INT)Player;
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
	FString List;
	if( !Parse( appCmdLine(), TEXT("MOVIESHOT="), List ) )
		return;
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
		return 0;
	ParseMovieShots();
	UMovieMaterial* M = WrapPlayer( Player, Name );
	// XboxMovieThread::Start: the credits keep their bands (bIsCreditsMovie).
	UBOOL bHideSubtitles = !GShowSubtitles && !appStrstr( Name, TEXT("UC2_Credits") );

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
		Player->Advance();
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
	GFullscreenMovie = 0;
	UnwrapPlayer( M );
	delete Player;	// closes the sound; the device resumes the others
	return 1;
	unguard;
}

/*-----------------------------------------------------------------------------
	MovieMaterial (D3DMaterialState.cpp:4539-4624 on the Xbox).
-----------------------------------------------------------------------------*/

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
			M->MovieDecoder = (INT)Player;
		else
		{
			// So we don't try this material again (the Xbox logged "Failed to open movie" to the artist).
			M->bHasError = 1;
			debugf( NAME_Log, TEXT("UC2Movie: MovieMaterial %s: failed to open movie (%s)"), M->GetPathName(), *M->MovieFileName );
		}
	}
	if( M->bHasError || !Player )
		return NULL;
	if( M->bNeedsFrameUpdate )
	{
		UBOOL bWasPlaying = !Player->HasEnded();
		Player->Advance();
		// XMV_ENDOFFILE on a non-looping movie: tell whoever listens (once; the Xbox decoder kept answering
		// ENDOFFILE on every later call, which would call the delegate every frame).
		if( bWasPlaying && Player->HasEnded() && OBJ_DELEGATE_IS_SET(M,OnDonePlaying) )
			M->delegateOnDonePlaying();
		if( Player->GetFrame() < 0 )
		{
			bOutFallback = 1;	// no picture yet: LoadingFallback or the default material
			return NULL;
		}
	}
	else if( Player->GetFrame() < 0 )
		return NULL;
	M->USize = Player->GetWidth();
	M->VSize = Player->GetHeight();
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
