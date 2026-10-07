/*=============================================================================
	UnGame.cpp: Unreal game engine.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "EnginePrivate.h"
#include "UnNet.h"
#include "UnLinker.h"
#include "FContentManager.h"

#define UNLOADMAP_DEBUG_LOG 0
#if UNLOADMAP_DEBUG_LOG
#include "FOutputDeviceFile.h"
#endif

#ifdef _XBOX
#include "XboxLive.h"
#endif // _XBOX

#include "UnScriptPatcher.h"
#include "UnMovie.h"	// engine-uc2 (M9): WITH_PORTABLE_MOVIES lifts the Xbox-only movie hooks below

// uc2 port (D-3): start in the last used profile, remember it (UC2Profiles.cpp).
ENGINE_API void UC2StartWithLastProfile( UViewport* Viewport );
ENGINE_API void UC2RememberProfile( UClient* Client );

/*-----------------------------------------------------------------------------
	Object class implementation.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(UGameEngine);

/*-----------------------------------------------------------------------------
	cleanup!!
-----------------------------------------------------------------------------*/

UAbstractUIXInteraction* UAbstractUIXInteraction::UIXInteractionSingleton = NULL;

void UAbstractUIXInteraction::execGetUIXInteraction(FFrame& Stack, RESULT_DECL)
{
	guard(UAbstractUIXInteraction::execGetUIXInteraction);

	P_FINISH;

	*(UAbstractUIXInteraction**) Result = UIXInteractionSingleton;

	unguardexec;
}


void UGameEngine::PaintProgress()
{
	guard(PaintProgress);

#ifdef _XBOX
	extern UBOOL IsPlayingMovie(void);
	//scion jg -- This is handled via movie now
	if (IsPlayingMovie())
	{
		return;
	}
#endif

    if( !Client || Client->Viewports.Num()==0 )
        return;

	UViewport* Viewport=Client->Viewports(0);

    if( !Viewport || !Viewport->Canvas || !Viewport->RI || Viewport->Actor == NULL )
        return;

    Viewport->Canvas->Update();
	Viewport->RI->Clear(0,FColor(0,0,0),1,1.0f);

	Draw( Viewport, 0.f );

    //if ( Viewport->Canvas->pCanvasUtil )
    //    Viewport->Canvas->pCanvasUtil->Flush();
	
	if( Viewport->PendingFrame )
		Viewport->Present();

	unguard;
}

INT UGameEngine::ChallengeResponse( INT Challenge )
{
	guard(UGameEngine::ChallengeResponse);
	return (Challenge*237) ^ (0x93fe92Ce) ^ (Challenge>>16) ^ (Challenge<<16);
	unguard;
}

void UGameEngine::UpdateConnectingMessage()
{
	guard(UGameEngine::UpdateConnectingMessage);
	if( GPendingLevel && Client && Client->Viewports.Num() )
	{
		if( Client->Viewports(0)->Actor->ProgressTimeOut<Client->Viewports(0)->Actor->Level->TimeSeconds )
		{
			TCHAR Msg1[256], Msg2[256];
			if( GPendingLevel->DemoRecDriver )
			{
				appSprintf( Msg1, TEXT("") );
				appSprintf( Msg2, *GPendingLevel->URL.Map );
			}
			else
			{
				appSprintf( Msg1, LocalizeProgress(TEXT("ConnectingText"),TEXT("Engine")) );
				appSprintf( Msg2, LocalizeProgress(TEXT("ConnectingURL"),TEXT("Engine")), *GPendingLevel->URL.Host, *GPendingLevel->URL.Map );
			}
			SetProgress( Msg1, Msg2, 60.f );
		}
	}
	unguard;
}
void UGameEngine::BuildServerMasterMap( UNetDriver* NetDriver, ULevel* InLevel )
{
	guard(UGameEngine::BuildServerMasterMap);
	check(NetDriver);
	check(InLevel);
	BeginLoad();
	{
		// Init LinkerMap.
		check(InLevel->GetLinker());
		NetDriver->MasterMap->AddLinker( InLevel->GetLinker() );

		// Load server-required packages.
		for( INT i=0; i<ServerPackages.Num(); i++ )
		{
			debugf( TEXT("Server Package: %s"), *ServerPackages(i) );
			ULinkerLoad* Linker = GetPackageLinker( NULL, *ServerPackages(i), LOAD_NoFail, NULL, NULL );
			if( NetDriver->MasterMap->AddLinker( Linker )==INDEX_NONE )
				debugf( TEXT("   (server-side only)") );
		}

		// Add GameInfo's package to map.
		check(InLevel->GetLevelInfo());
		check(InLevel->GetLevelInfo()->Game);
		check(InLevel->GetLevelInfo()->Game->GetClass()->GetLinker());
		NetDriver->MasterMap->AddLinker( InLevel->GetLevelInfo()->Game->GetClass()->GetLinker() );

		// Precompute linker info.
		NetDriver->MasterMap->Compute();
	}
	EndLoad();
	unguard;
}

/*-----------------------------------------------------------------------------
	Game init and exit.
-----------------------------------------------------------------------------*/

//
// Construct the game engine.
//
UGameEngine::UGameEngine()
: LastURL(TEXT(""))
{}


void UGameEngine::Init()
{
	guard(UGameEngine::Init);
	check(sizeof(*this)==GetClass()->GetPropertiesSize());

	// Call base.
	UEngine::Init();

	// Init variables.
	GLevel = NULL;
	GEntry = NULL;

	// Delete temporary files in cache.
	appCleanFileCache();

	// If not a dedicated server.
	if( GIsClient )
	{	
		// Init client.
		UClass* ClientClass = StaticLoadClass( UClient::StaticClass(), NULL, TEXT("ini:Engine.Engine.ViewportManager"), NULL, LOAD_NoFail, NULL );
		Client = ConstructObject<UClient>( ClientClass );
		Client->Init( this );

		// Init Render Device
		UClass* RenDevClass = StaticLoadClass( URenderDevice::StaticClass(), NULL, TEXT("ini:Engine.Engine.RenderDevice"), NULL, LOAD_NoFail, NULL );
		GRenDev = ConstructObject<URenderDevice>( RenDevClass );
		GRenDev->Init();
	}

#ifdef WITH_KARMA
    KInitGameKarma(); // Init Karma physics.
#endif

#ifdef WITH_LIPSINC
	GLIPSincInitGame(); // Init LIPSinc system.
#endif

	// Load the entry level.
	FString Error;	
	if( Client )
	{
		if( !LoadMap( FURL(TEXT("Entry")), NULL, NULL, Error ) )
			appErrorf( LocalizeError(TEXT("FailedBrowse"),TEXT("Engine")), TEXT("Entry"), *Error );
		// scion sz - 
		GEntry = GLevel;
		//Exchange( GLevel, GEntry );
	}

	// Create default URL.
	FURL DefaultURL;
	DefaultURL.LoadURLConfig( TEXT("DefaultPlayer"), TEXT("User") );

	// Open initial Viewport.
	UViewport* Viewport = NULL;
	if( Client )
	{
		// Init input.!!Temporary
		UInput::StaticInitInput();

		// Create the InteractionMaster

		UClass* IMClass = StaticLoadClass(UInteractionMaster::StaticClass(), NULL, TEXT("engine.InteractionMaster"), NULL, LOAD_NoFail, NULL);
		Client->InteractionMaster = ConstructObject<UInteractionMaster>(IMClass);

		// Setup reference (pointer) to the client

		Client->InteractionMaster->Client = Client;

		// Display Copyright Notice

		Client->InteractionMaster->DisplayCopyright();

		// Create viewport.

		Viewport = Client->NewViewport( NAME_None );

		// uc2 port (D-3): the first player starts in the profile they last played with (UC2Profiles.cpp).
		UC2StartWithLastProfile( Viewport );

		// Add code to Create the Menu System here				

		//scion bcl -- some simple changes to make the GUIController code less cast happy
		UInteraction *GUIInteraction = Client->InteractionMaster->eventAddInteraction(TEXT("ini:Engine.Engine.GUIController"),NULL, false);
		UBaseGUIController *GUIController = Cast<UBaseGUIController>(GUIInteraction);
		if (GUIController)
		{
			GUIController->ViewportOwner = Viewport;
			GUIController->InitializeController();	// Initialize it
			Viewport->GUIController = GUIController;
		}
		else
			debugf(TEXT("Could not spawn a GUI Controller!"));

#ifdef _XBOX
		Client->InteractionMaster->UIX = Cast<UAbstractUIXInteraction>(Client->InteractionMaster->eventAddInteraction(TEXT("GUI.UIXInteraction"),NULL,true));
		Client->InteractionMaster->UIX->ViewportOwner = Viewport;
#endif

		// Create the Console
		// scion sz - console should be a global interaction
		UInteraction *Console = Client->InteractionMaster->eventAddInteraction(TEXT("ini:Engine.Engine.Console"),NULL, false);
		if (Console) {
			Client->InteractionMaster->Console = Console;
			Viewport->Console = Console;
		}

		// initialize the input
		Viewport->Input->Init( Viewport );

		// scion sz - 
		// spawn a default player in Entry
		if( !Viewport->Actor || Viewport->Actor->bDeleteMe ) 
		{
			if( !GLevel->SpawnPlayActor( Viewport, ROLE_SimulatedProxy, DefaultURL, Error ) )
				appErrorf(( TEXT("%s"), *Error ));
		}
        
		// Initialize Audio.
		InitAudio();
	}
	debugf( NAME_Init, TEXT("Game engine initialized") );

	// dump various sizes for memory hunting
	debugf( NAME_Init, TEXT("Struct size: %d"), sizeof(UStruct) );
	debugf( NAME_Init, TEXT("Class size: %d"), sizeof(UClass) );
	debugf( NAME_Init, TEXT("Object size: %d"), sizeof(UObject) );
	debugf( NAME_Init, TEXT("Actor size: %d"), sizeof(AActor) );

	// scion sz -
	// flag all objects that have been loaded up to this point so that
	// they are not deleted when traveling between levels
	for( FObjectIterator It; It; ++It )
	{
		UObject* CurObject = *It;
		CurObject->SetFlags( RF_TagPersist );
	}

	FContentManager* ContentManager = FContentManager::GetInstance();
	if (ContentManager != NULL)
	{
		ContentManager->UpdateConfigs();
	}

	// Enter initial world.
	TCHAR Parm[4096]=TEXT("");
	const TCHAR* Tmp = appCmdLine();
	if
		(	!ParseToken( Tmp, Parm, ARRAY_COUNT(Parm), 0 )
		||	(appStricmp(Parm,TEXT("SERVER"))==0 && !ParseToken( Tmp, Parm, ARRAY_COUNT(Parm), 0 ))
		||	Parm[0]=='-'
		// A launcher may pass global options first ("Name=Player"): that is not a map either. Without this the
		// engine tried to load a package called "Name=Player" and exited (seen on the first browser boot).
		||	appIsGlobalOptionToken(Parm) )
		appStrcpy( Parm, *FURL::DefaultLocalMap );
	FURL URL( &DefaultURL, Parm, TRAVEL_Partial );
	if( !URL.Valid )
		appErrorf( LocalizeError(TEXT("InvalidUrl"),TEXT("Engine")), Parm );

	// browse to url from command line
	UBOOL Success = Browse( URL, NULL, Error );

	// If waiting for a network connection, go into the starting level.
	if( !Success && Error==TEXT("") && appStricmp( Parm, *FURL::DefaultLocalMap )!=0 )
		Success = Browse( FURL(&DefaultURL,*FURL::DefaultLocalMap,TRAVEL_Partial), NULL, Error );

	// Handle failure.
	if( !Success )
		appErrorf( LocalizeError(TEXT("FailedBrowse"),TEXT("Engine")), Parm, *Error );

	// If RF_TagPersist GC is re-enabled, the following line should be moved up to just after the call to InitAudio()
	// This screws up initial mouse capture when running in windowed mode, however, so leave it here otherwise
	// initialize RenDev and create window
	if ( Client )
		Viewport->OpenWindow( 0, 0, (INT) INDEX_NONE, (INT) INDEX_NONE, (INT) INDEX_NONE, (INT) INDEX_NONE );

	unguard;
}

//
// Game exit.
//
void UGameEngine::Destroy()
{
	guard(UGameEngine::Destroy);

	// Game exit.
	if( GPendingLevel )
		CancelPending();
	GLevel = NULL;
	debugf( NAME_Exit, TEXT("Game engine shut down") );

#ifdef WITH_KARMA
	if (KGData != NULL)
	{
		KGData->bShutdownPending = 1;
	}
    KTermGameKarma();
#endif

#ifdef WITH_LIPSINC
	GLIPSincShutdownGame();
#endif

	Super::Destroy();
	unguard;
}

//
// Progress text.
//
void UGameEngine::SetProgress( const TCHAR* Str1, const TCHAR* Str2, FLOAT Seconds )
{
	guard(UGameEngine::SetProgress);
	if( Client && Client->Viewports.Num() )
	{
		APlayerController* Actor = Client->Viewports(0)->Actor;
		//jg -- The actor may not be set yet
		if (Actor != NULL)
		{
			if( Seconds==-1.f )
			{
				// Upgrade message.
				if ( Actor->myHUD )
					Actor->myHUD->eventShowUpgradeMenu();
			}

			Actor->eventSetProgressMessage(0, Str1, FColor(255,255,255));
			Actor->eventSetProgressMessage(1, Str2, FColor(255,255,255));
			Actor->eventSetProgressTime(Seconds);
		}
	}
	unguard;
}

/*-----------------------------------------------------------------------------
	Command line executor.
-----------------------------------------------------------------------------*/
static QSORT_RETURN CDECL CompareSubItems( const FSubItem* A, const FSubItem* B )
{
	return B->Max - A->Max;
}
static QSORT_RETURN CDECL CompareItems( const FItem* A, const FItem* B )
{
	return B->Max - A->Max;
}

static INT Compare( const FSubItem& A, const FSubItem& B )
{
	return appStricmp(A.Object->GetPathName(), B.Object->GetPathName());
}

static INT Compare( const FItem& A, const FItem& B )
{
	return appStricmp(A.Class->GetName(), B.Class->GetName());
}

//
// This always going to be the last exec handler in the chain. It
// handles passing the command to all other global handlers.
//
// uc2 port: the network tests' commands (UC2NetTest.cpp).
ENGINE_API UBOOL UC2NetTestExec( UGameEngine* Engine, const TCHAR* Cmd, FOutputDevice& Ar );
ENGINE_API void UC2NetTestTick( UGameEngine* Engine );

UBOOL UGameEngine::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UGameEngine::Exec);
	if( UC2NetTestExec( this, Cmd, Ar ) )	// uc2 port: NETPROBE/NETPLACE/NETAIM
		return 1;
	const TCHAR* Str=Cmd;
#if WITH_PORTABLE_MOVIES
	// engine-uc2 (M9): the Xbox client's SUBTITLES command (XboxClient.cpp:169-180), which the profile sends
	// when "cinematic captions" changes; off hides the cinematics' burnt-in subtitle bands (UnMovie.cpp).
	if( ParseCommand( &Str, TEXT("SUBTITLES") ) )
	{
		GShowSubtitles = ParseCommand(&Str,TEXT("TRUE")) || ParseCommand(&Str,GTrue);
		return 1;
	}
#endif
	if( ParseCommand( &Str, TEXT("OPEN") ) )
	{
		FString Error;
		if( Client && Client->Viewports.Num() )
			SetClientTravel( Client->Viewports(0), Str, 0, TRAVEL_Partial );

		else
		if( !Browse( FURL(&LastURL,Str,TRAVEL_Partial), NULL, Error ) && Error!=TEXT("") )
			Ar.Logf( TEXT("Open failed: %s"), *Error );
		return 1;
	}
	else if( ParseCommand( &Str, TEXT("START") ) )
	{
		FString Error;
		if( Client && Client->Viewports.Num() )
			SetClientTravel( Client->Viewports(0), Str, 0, TRAVEL_Absolute );
		else
		if( !Browse( FURL(&LastURL,Str,TRAVEL_Absolute), NULL, Error ) && Error!=TEXT("") )
			Ar.Logf( TEXT("Start failed: %s"), *Error );
		return 1;
	}
	else if( ParseCommand( &Str, TEXT("SERVERTRAVEL") ) && GIsServer )
	{
		GLevel->GetLevelInfo()->eventServerTravel(Str,0);
		return 1;
	}
	else if( (GIsServer && !GIsClient) && ParseCommand( &Str, TEXT("SAY") ) )
	{
		GLevel->GetLevelInfo()->Game->eventBroadcast(NULL, Str, NAME_None);
		return 1;
	}
	else if( ParseCommand(&Str, TEXT("DISCONNECT")) )
	{
		FString Error;
		if( Client && Client->Viewports.Num() )
		{
			if( GLevel && GLevel->NetDriver && GLevel->NetDriver->ServerConnection && GLevel->NetDriver->ServerConnection->Channels[0] )
			{
				GLevel->NetDriver->ServerConnection->Channels[0]->Close();
				GLevel->NetDriver->ServerConnection->FlushNet();
			}
			if( GPendingLevel && GPendingLevel->NetDriver && GPendingLevel->NetDriver->ServerConnection && GPendingLevel->NetDriver->ServerConnection->Channels[0] )
			{
				GPendingLevel->NetDriver->ServerConnection->Channels[0]->Close();
				GPendingLevel->NetDriver->ServerConnection->FlushNet();
			}

			// immediately notify all connected clients that we're closing down
			if ( GLevel && GLevel->NetDriver && GLevel->NetDriver->ClientConnections.Num() )
			{
				for ( INT i = 0; i < GLevel->NetDriver->ClientConnections.Num(); i++ )
				{
					GLevel->NetDriver->ClientConnections(i)->Logf(TEXT("FAILURE FORCED"));
					GLevel->NetDriver->ClientConnections(i)->FlushNet();
				}
			}

			if (!ParseCommand(&Str, TEXT("NOLOAD")))
			{
				FString DisconnectString = FString::Printf(TEXT("?failed?disconnect%s"), Str);
				SetClientTravel( Client->Viewports(0), *DisconnectString, 0, TRAVEL_Absolute );
			}
		}
		else
		{
			if( !Browse( FURL(&LastURL,TEXT("?failed?disconnect"),TRAVEL_Absolute), NULL, Error ) && Error!=TEXT("") )
				Ar.Logf( TEXT("Disconnect failed: %s"), *Error );
			return 1;
		}
	}
	else if( ParseCommand(&Str, TEXT("RECONNECT")) )
	{
		FString Error;
		if( Client && Client->Viewports.Num() )
			SetClientTravel( Client->Viewports(0), TEXT("?reconnect"), 0, TRAVEL_Absolute );
		else
		if( !Browse( FURL(LastURL), NULL, Error ) && Error!=TEXT("") )
			Ar.Logf( TEXT("Reconnect failed: %s"), *Error );
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("EXIT")) || ParseCommand(&Cmd,TEXT("QUIT")))
	{
		if( GLevel && GLevel->NetDriver && GLevel->NetDriver->ServerConnection && GLevel->NetDriver->ServerConnection->Channels[0] )
		{
			GLevel->NetDriver->ServerConnection->Channels[0]->Close();
			GLevel->NetDriver->ServerConnection->FlushNet();
		}
		if( GPendingLevel && GPendingLevel->NetDriver && GPendingLevel->NetDriver->ServerConnection && GPendingLevel->NetDriver->ServerConnection->Channels[0] )
		{
			GPendingLevel->NetDriver->ServerConnection->Channels[0]->Close();
			GPendingLevel->NetDriver->ServerConnection->FlushNet();
		}
		Ar.Log( TEXT("Closing by request") );
		appRequestExit( 0 );
		return 1;
	}
	else if( ParseCommand( &Str, TEXT("GETCURRENTTICKRATE") ) )
	{
		Ar.Logf( TEXT("%f"), CurrentTickRate );
		return 1;
	}
	else if( ParseCommand( &Str, TEXT("GETMAXTICKRATE") ) )
	{
		Ar.Logf( TEXT("%f"), GetMaxTickRate() );
		return 1;
	}
	else if( ParseCommand( &Str, TEXT("GSPYLITE") ) )
	{
		FString Error;
		appLaunchURL( TEXT("GSpyLite.exe"), TEXT(""), &Error );
		return 1;
	}
	else if( ParseCommand(&Str,TEXT("SAVEGAME")) )
	{
		if( appIsDigit(Str[0]) )
			SaveGame( appAtoi(Str) );
		return 1;
	}
	else if( ParseCommand( &Cmd, TEXT("CANCEL") ) )
	{
		static UBOOL InCancel = 0;
		if( !InCancel )	
		{
			//!!Hack for broken Input subsystem.  JP.
			//!!Inside LoadMap(), ResetInput() is called,
			//!!which can retrigger an Exec call.
			InCancel = 1;
			if( GPendingLevel )
			{
				if( GPendingLevel->TrySkipFile() )
				{
					InCancel = 0;
					return 1;
				}
				SetProgress( LocalizeProgress(TEXT("CancelledConnect"),TEXT("Engine")), TEXT(""), 2.f );
			}
			else
				SetProgress( TEXT(""), TEXT(""), 0.f );
			CancelPending();
			InCancel = 0;
		}
		return 1;
	}
    else if( ParseCommand(&Cmd,TEXT("SOUND_REBOOT"))  )
    {
		UViewport* Viewport = Audio != NULL ? Audio->GetViewport() : NULL;
        if (Audio != NULL)
        {
			delete Audio;
			Audio = NULL;
		}
		if (Voice != NULL)
		{
			delete Voice;
			Voice = NULL;
		}
		InitAudio();
		if (Audio != NULL)
		{
			Audio->SetViewport(Viewport);
		}
		if (Voice != NULL)
		{
			Voice->SetViewport(Viewport);
		}
		return 1;
    }
	else if( GLevel && GLevel->Exec( Cmd, Ar ) )
	{
		return 1;
	}
	else if( GLevel && GLevel->GetLevelInfo()->Game && GLevel->GetLevelInfo()->Game->ScriptConsoleExec(Cmd,Ar,NULL) )
	{
		return 1;
	}
	//jg -- Force clean actors that are pending delete
	else if( ParseCommand(&Str,TEXT("CLEANUPDESTROYED")) )
	{
		GLevel->CleanupDestroyed(1);
		return 1;
	}
	//jg -- Be able to calculate how much memory a given property type uses in all objects
	else if( ParseCommand(&Str,TEXT("PROPERTYMEMUSAGE")) )
	{
		UClass* PropType = NULL;
		// Figure out which type of property to count memory usage on
		if (ParseObject<UClass>(Str,TEXT("CLASS="),PropType,ANY_PACKAGE))
		{
			DWORD MemUsed = 0;
			// Iterate through all objects
			for (FObjectIterator It; It; ++It)
			{
				// Iterate through all properties on the object
				for (TFieldIterator<UProperty,CLASS_IsAUProperty> PropIt(It->GetClass());
					PropIt; ++PropIt)
				{
					if (PropIt->IsA(PropType))
					{
						MemUsed += PropIt->GetSize();
					}
					// If this is a string, figure out how big it is
					if (PropIt->IsA(UStrProperty::StaticClass()))
					{
						// Access the string object so we can figure out how big it is
						MemUsed += (*(FString*)((BYTE*)*It + PropIt->Offset)).Len() * sizeof(TCHAR);
					}
				}
			}
			Ar.Logf(TEXT("Memory used for property type %s is %d"),
				PropType->GetName(),MemUsed);
			return 1;
		}
	}
	else if( ParseCommand(&Str,TEXT("OBJ")) )
	{
		if( ParseCommand(&Str,TEXT("LIST")) )
		{
			//scion jg -- Force a cleanup so that we get accurate leak detection
			if( GLevel != NULL && 
				GLevel != GEntry )
			{
				GLevel->CleanupDestroyed(1);
			}
			Ar.Log( TEXT("Objects:") );
			Ar.Log( TEXT("") );
			UClass*   CheckType     = NULL;
			UClass*   MetaClass		= NULL;
			//epic moelfke: allow checking for any Outer, not just a UPackage
			UObject* CheckOuter = NULL;
			UPackage* InsidePackage = NULL;
			ParseObject<UClass>  ( Str, TEXT("CLASS="  ), CheckType,     ANY_PACKAGE );
			ParseObject<UObject>(Str, TEXT("PACKAGE="), CheckOuter, NULL);
			if (!CheckOuter)
			{
				ParseObject<UObject>(Str, TEXT("OUTER="), CheckOuter, ANY_PACKAGE);
			}
			ParseObject<UPackage>( Str, TEXT("INSIDE=" ), InsidePackage, NULL );
			TArray<FItem> List;
			TArray<FSubItem> Objects;
			FItem Total;

			// rjp - support specifying metaclasses when listing class objects
			if ( CheckType && CheckType == UClass::StaticClass() )
				ParseObject<UClass>  ( Str, TEXT("TYPE="   ), MetaClass,     ANY_PACKAGE );

			// rjp -- if we specified a parameter in the command, but no objects of that parameter were found,
			// don't be dumb and list all objects
			if ( (CheckType || !appStrfind(Str,TEXT("CLASS="))) &&
				 (MetaClass || !appStrfind(Str,TEXT("TYPE="))) &&
				 (CheckOuter || (!appStrfind(Str,TEXT("PACKAGE=")) && !appStrfind(Str,TEXT("OUTER=")))) &&
				 (InsidePackage || !appStrfind(Str,TEXT("INSIDE="))) )
			{
				for( FObjectIterator It; It; ++It )
				{
					if ( CheckType && !It->IsA(CheckType) )
						continue;

					if ( CheckOuter && It->GetOuter() != CheckOuter )
						continue;

					if ( InsidePackage && !It->IsIn(InsidePackage) )
						continue;

					if ( MetaClass )
					{
						UClass* ClassObj = Cast<UClass>(*It);
						if ( ClassObj && !ClassObj->IsChildOf(MetaClass) )
							continue;
					}

					FArchiveCountMem Count( *It );
					INT i;
					for( i=0; i<List.Num(); i++ )
						if( List(i).Class == It->GetClass() )
							break;
					if( i==List.Num() )
						i = List.AddItem(FItem( It->GetClass() ));
					if( CheckType || CheckOuter || InsidePackage )
						new(Objects)FSubItem( *It, Count.GetNum(), Count.GetMax(), Count.GetContiguous() );
					List(i).Add( Count );
					Total.Add( Count );
				}
			}

			UBOOL bAlphaSort=0;
			ParseUBOOL(Str, TEXT("AlphaSort="), bAlphaSort);
			if( Objects.Num() )
			{
				if ( bAlphaSort )
					Sort( &Objects(0), Objects.Num() );
				else
					appQsort( &Objects(0), Objects.Num(), sizeof(Objects(0)), (QSORT_COMPARE)CompareSubItems );

				Ar.Logf( TEXT("%60s % 10s % 10s % 10s"), TEXT("Object"), TEXT("NumBytes"), TEXT("MaxBytes"), TEXT("ContBytes") );
				for( INT i=0; i<Objects.Num(); i++ )
					Ar.Logf( TEXT("%60s % 9i % 9i %9i"), Objects(i).Object->GetFullName(), Objects(i).Num, Objects(i).Max, Objects(i).Contiguous );
				Ar.Log( TEXT("") );
			}
			if( List.Num() )
			{
				if ( bAlphaSort )
					Sort( &List(0), List.Num() );
				else
                    appQsort( &List(0), List.Num(), sizeof(List(0)), (QSORT_COMPARE)CompareItems );

				Ar.Logf(TEXT(" %30s % 6s % 10s  % 10s % 10s"), TEXT("Class"), TEXT("Count"), TEXT("NumBytes"), TEXT("MaxBytes"), TEXT("ContBytes") );
				for( INT i=0; i<List.Num(); i++ )
					Ar.Logf(TEXT(" %30s % 6i % 9iK % 9iK %9iK"), List(i).Class->GetName(), List(i).Count, List(i).Num/1024, List(i).Max/1024, List(i).Contiguous/1024 );
				Ar.Log( TEXT("") );
			}
			Ar.Logf( TEXT("%i Objects (%.3fM / %.3fM) (%.3fM Contigous Bytes)"), Total.Count, (FLOAT)Total.Num/1024.0/1024.0, (FLOAT)Total.Max/1024.0/1024.0, (FLOAT)Total.Contiguous/1024.f/1024.f );
			return 1;
		}
	}
#ifndef FINAL_RELEASE
	//scion jg -- Figure out if we are leaking objects or not
	else if (ParseCommand(&Str,TEXT("LEAKCHECK")))
	{
		// Force a cleanup so that we get accurate leak detection
		if (GLevel != NULL)
		{
			GLevel->CleanupDestroyed(1);
		}
		UClass*   CheckType     = NULL;
		UPackage* CheckPackage  = NULL;
		UPackage* InsidePackage = NULL;
		ParseObject<UClass>  ( Str, TEXT("CLASS="  ), CheckType,     ANY_PACKAGE );
		ParseObject<UPackage>( Str, TEXT("PACKAGE="), CheckPackage,  NULL );
		ParseObject<UPackage>( Str, TEXT("INSIDE=" ), InsidePackage, NULL );
		static TArray<FItem> FirstPassList;
		static FItem FirstPassTotal;
		static TArray<FSubItem> FirstPassObjects;
		TArray<FItem> SecondPassList;
		FItem SecondPassTotal;
		TArray<FSubItem> SecondPassObjects;
		// Get a snap shot to compare against
		if (ParseCommand(&Str,TEXT("START")))
		{
			FirstPassList.Empty();
			FirstPassTotal = FItem();
			for( FObjectIterator It; It; ++It )
			{
				if ((CheckType     && It->IsA(CheckType))
				||	(CheckPackage  && It->GetOuter()==CheckPackage)
				||	(InsidePackage && It->IsIn(InsidePackage))
				||	(!CheckType && !CheckPackage && !InsidePackage) )
				{
					FArchiveCountMem Count( *It );
					INT i;
					for( i=0; i<FirstPassList.Num(); i++ )
						if( FirstPassList(i).Class == It->GetClass() )
							break;
					if( i==FirstPassList.Num() )
						i = FirstPassList.AddItem(FItem( It->GetClass() ));
					FirstPassList(i).Add( Count );
					FirstPassTotal.Add( Count );
					new(FirstPassObjects)FSubItem( *It, Count.GetNum(), Count.GetMax() );
				}
			}
		}
		// Do a second pass check and figure out what changed size in the
		// wrong way
		else if (ParseCommand(&Str,TEXT("END")))
		{
			SecondPassList.Empty();
			SecondPassTotal = FItem();
			for( FObjectIterator It; It; ++It )
			{
				if ((CheckType     && It->IsA(CheckType))
				||	(CheckPackage  && It->GetOuter()==CheckPackage)
				||	(InsidePackage && It->IsIn(InsidePackage))
				||	(!CheckType && !CheckPackage && !InsidePackage) )
				{
					FArchiveCountMem Count( *It );
					INT i;
					for( i=0; i<SecondPassList.Num(); i++ )
						if( SecondPassList(i).Class == It->GetClass() )
							break;
					if( i==SecondPassList.Num() )
						i = SecondPassList.AddItem(FItem( It->GetClass() ));
					SecondPassList(i).Add( Count );
					SecondPassTotal.Add( Count );
					new(SecondPassObjects)FSubItem( *It, Count.GetNum(), Count.GetMax() );
				}
			}
			// Dump out the size differences if present
			if (FirstPassTotal.Count < SecondPassTotal.Count ||
				FirstPassTotal.Num < SecondPassTotal.Num ||
				FirstPassTotal.Max < SecondPassTotal.Max)
			{
				Ar.Logf(TEXT("Object count or size difference!"));
				Ar.Logf(TEXT("\tDifference is %d Objects (%.3fM / %.3fM)"),
					SecondPassTotal.Count - FirstPassTotal.Count,
					(SecondPassTotal.Num - FirstPassTotal.Num) / (1024.f * 1024.f),
					(SecondPassTotal.Max - FirstPassTotal.Max) / (1024.f * 1024.f));
			}
			TArray<UClass*> Classes;
			Ar.Logf(TEXT(""));
			Ar.Logf(TEXT("Potential leaks (by class):"));
			Ar.Logf(TEXT(" %30s % 6s % 10s  % 10s "), TEXT("Class"),
				TEXT("Count"), TEXT("NumBytes"), TEXT("MaxBytes") );
			// Do a brute force search (mmmm N^2) to find objects that
			// changed in number
			for (INT FirstIndex = 0; FirstIndex < FirstPassList.Num();
				FirstIndex++)
			{
				// Find this object in the second list
				for (INT SecondIndex = 0; SecondIndex < SecondPassList.Num();
					SecondIndex++)
				{
					FItem& First = FirstPassList(FirstIndex);
					FItem& Second = SecondPassList(SecondIndex);
					// See if the classes match
					if (First.Class && Second.Class &&
						First.Class == Second.Class &&
						(First.Num < Second.Num || First.Max < Second.Max) &&
						Second.Class->IsChildOf(UField::StaticClass()) == 0 &&
						Second.Class->IsChildOf(ULinker::StaticClass()) == 0 &&
						Second.Class->IsChildOf(UPackage::StaticClass()) == 0)
					{
						// Dump out the difference
						Ar.Logf(TEXT(" %30s % 6d % 10dK % 10dK"),
							First.Class->GetName(),
							Second.Count - First.Count,
							((Second.Num - First.Num) / 1024),
							((Second.Max - First.Max) / 1024));
						Classes.AddItem(Second.Class);
					}
				}
			}
			Ar.Logf(TEXT(""));
			Ar.Logf(TEXT("Potential leaks (by object):"));
			// Now loop through and figure out which object are different for
			// each class that changed size/count. Super slow search!
			for (INT Index = 0; Index < Classes.Num(); Index++)
			{
				// For this class find all objects in second pass that weren't
				// in the first pass
				for (INT ObjIndex = 0; ObjIndex < SecondPassObjects.Num();
					ObjIndex++)
				{
					UObject* pSecObj = SecondPassObjects(ObjIndex).Object;
					// If this class matches
					if (pSecObj->IsA(Classes(Index)))
					{
						UBOOL bFound = 0;
						// Search the first list to see if it existed then
						for (INT FirstIndex = 0;
							FirstIndex < FirstPassObjects.Num() && bFound == 0;
							FirstIndex++)
						{
							UObject* pFirstObj = FirstPassObjects(ObjIndex).Object;
							// Bail out if this object was in the first pass
							bFound = pFirstObj == pSecObj;
						}
						// If it wasn't found it's a potential leak
						if (bFound == 0)
						{
							// Dump this object's name
							Ar.Logf(pSecObj->GetFullName());
						}
					}
				}
			}
		}
		return 1;
	}
	//scion jg -- Finds all textures being referenced
	else if (ParseCommand(&Str,TEXT("TEXUSAGE")))
	{
		UPackage* Package = NULL;
		ParseObject<UPackage>(Str,TEXT("PACKAGE="),Package,NULL);
		if (Package != NULL)
		{
			TArray<UObject*> PrecacheItems;
			// Search through all objects and match them to the chosen package
			for (FObjectIterator It; It; ++It)
			{
				if (It->IsIn(Package))
				{
					// Find the items that need precaching for this object
					CheckStructPrecache(It->GetClass(),(BYTE*)*It,PrecacheItems);
				}
			}
			Ar.Logf(TEXT("Textures referenced by %s:"),Package->GetName());
			Ar.Logf(TEXT("%30s %10s"),TEXT("Texture"),TEXT("Size"));
			INT Total = 0;
			// Search through the required precached fields for textures
			for (INT Index = 0; Index < PrecacheItems.Num(); Index++)
			{
				UObject* pObject = PrecacheItems(Index);
				// Report any bitmap derived material
				if (pObject->IsA(UBitmapMaterial::StaticClass()))
				{
					// Figure out how much memory it is using
					FArchiveCountMem Count(pObject);
					// Log the texture and its memory
					Ar.Logf(TEXT("%30s %10d"),pObject->GetName(),
						Count.GetMax());
					// Keep track of cumulative total
					Total += Count.GetMax();
				}
			}
			Ar.Logf(TEXT("%30s %10d"),TEXT("Total"),Total);
		}
		return 1;
	}
	// scion sz - for debug
	else if( ParseCommand(&Str,TEXT("EMPTYFONTS")) )
	{
		for( TObjectIterator<UFont> It; It; ++It )
		{
			UFont* Font = *It;
			if( Font )
			{
				for( INT t=0;t<Font->Textures.Num();t++ )
				{
					if( Font->Textures(t) )
					{
						for( INT i=0; i < Font->Textures(t)->Mips.Num(); i++ )
							Font->Textures(t)->Mips(i).DataArray.Empty();
					}
				}
			}
		}
		return 1;
	}
	// scion sz - for debug
	else if ( ParseCommand(&Cmd, TEXT("PARTICLEBOOLS")) )
	{
		debugf( TEXT("Loading all server packages...") );
		for( INT i=0; i < ServerPackages.Num(); i++ )
		{
			debugf( TEXT("Server Package: %s"), *ServerPackages(i) );		
			LoadPackage(NULL, *ServerPackages(i), LOAD_NoWarn);
		}

		// map for property name and its bool count
		TMap<FName,INT> PropCountMap;
		UClass* Class = Class=FindObject<UClass>( ANY_PACKAGE, TEXT("ParticleEmitter"));
		if ( Class )
		{
			// for all bool properties of Class(PArticleEmitter)
			for( TFieldIterator<UBoolProperty,CLASS_IsAUBoolProperty> PropIt(Class); PropIt; ++PropIt)			
			{
				UBoolProperty* Property = *PropIt;

				INT* PropCount = PropCountMap.Find( Property->GetFName() );
				if( !PropCount )
				{
					PropCount = &PropCountMap.Add( Property->GetFName(), 0 );
				}
				// for all instanced objects of its type
				for( TObjectIterator<UParticleEmitter> It; It; ++It )
				{	
					// increment bool count if it is true
					BYTE* Data = (BYTE*)*It + Property->Offset;
					*PropCount += Property->HasValue( Data ) ? 1 : 0;
				}
			}
		}
        // dump bool property names and their total True counts
		for( TMap<FName,INT>::TIterator MapIt(PropCountMap); MapIt; ++MapIt )
		{
			debugf( TEXT("%50s =%d"), *MapIt.Key(), MapIt.Value() );
		}
			
		return 1;
	}
#endif
	else
	{
		// disallow set of pawn property if network game
		INT AllowSet = 1;
		if ( GLevel && (GLevel->GetLevelInfo()->NetMode == NM_Client) )
		{
			const TCHAR *Str = Cmd;
			if ( ParseCommand(&Str,TEXT("SET")) )
			{
				TCHAR ClassName[256];
				UClass* Class;
				if
				(	ParseToken( Str, ClassName, ARRAY_COUNT(ClassName), 1 )
				&&	(Class=FindObject<UClass>( ANY_PACKAGE, ClassName))!=NULL )
				{
					TCHAR PropertyName[256];
					UProperty* Property;
					if
					(	ParseToken( Str, PropertyName, ARRAY_COUNT(PropertyName), 1 )
					&&	(Property=FindField<UProperty>( Class, PropertyName))!=NULL )
					{
					if ( Class->IsChildOf(AActor::StaticClass())
							&& !Class->IsChildOf(AGameInfo::StaticClass())
							&& !(Property->PropertyFlags & (CPF_Config|CPF_GlobalConfig)) )
						{
						AllowSet = 0;
							debugf(
								TEXT("Not allowing set command: %u %u %u"),
								Class->IsChildOf(AActor::StaticClass()),
								!Class->IsChildOf(AGameInfo::StaticClass()),
								!(Property->PropertyFlags & (CPF_Config|CPF_GlobalConfig))
								);
						}
					}
				}
			}
		}

		if ( !AllowSet )
			return 1;
	}

	if ( UEngine::Exec(Cmd, Ar) )
		return 1;

	return 0;
	//scion bcl restructured to eliminate compiler warning
	unguard;
}

/*-----------------------------------------------------------------------------
	Serialization.
-----------------------------------------------------------------------------*/

//
// Serializer.
//
void UGameEngine::Serialize( FArchive& Ar )
{
	guard(UGameEngine::Serialize);
	Super::Serialize( Ar );

	Ar << GLevel << GEntry << GPendingLevel;

	unguardobj;
}

/*-----------------------------------------------------------------------------
	Game entering.
-----------------------------------------------------------------------------*/

//
// Cancel pending level.
//
void UGameEngine::CancelPending()
{
	guard(UGameEngine::CancelPending);

	if( GPendingLevel )
	{
		if( GPendingLevel->NetDriver && GPendingLevel->NetDriver->ServerConnection && GPendingLevel->NetDriver->ServerConnection->Channels[0] )
		{
			GPendingLevel->NetDriver->ServerConnection->Channels[0]->Close();
			GPendingLevel->NetDriver->ServerConnection->FlushNet();
		}
		delete GPendingLevel;
		GPendingLevel = NULL;
	}

	debugf(NAME_DevRon, TEXT("CancelPending"));
	check(GPendingLevel == NULL);

	unguard;
}

/* scion ======================================================================
* ::MatchViewportsToActors
* Author: epic
* 
* Match currently active viewports with new player controllers.
*
* =============================================================================
*/
static void MatchViewportsToActors( UClient* Client, ULevel* Level, const FURL& URL )
{
	guard(MatchViewportsToActors);

	// scion sz - get ptr to split screen manager
	USplitScreenMgr* SplitScreenMgr = Client->GetSplitScreenMgr();
	// iterate through current viewports
	for( INT i=0; i<Client->Viewports.Num(); i++ )
	{
		UViewport* Viewport = Client->Viewports(i);
		if( Viewport )
		{
			UBOOL bSplitView = (SplitScreenMgr && (SplitScreenMgr->IsSplitScreenViewport( Viewport ) || SplitScreenMgr->IsOverlayViewport( Viewport ))) ? 1:0;
			// scion sz - skip splitscreen viewports
			if( Viewport == Client->Viewports(0) ||	
				!bSplitView )
			{
				// else spawn player using level URL
				FString Error;
				debugf( NAME_Log, TEXT("Spawning new actor for Viewport %s"), Viewport->GetName() );
				if( !Level->SpawnPlayActor( Viewport, ROLE_SimulatedProxy, URL, Error ) )
					appErrorf( TEXT("%s"), *Error );
			}
		}		
	}
	// scion sz - 
	// allow splitscreen manager to handle splitscreen viewport actor spawning
	if( SplitScreenMgr )
	{
		SplitScreenMgr->MatchSplitViewportsToActors();
	}

	unguardf(( TEXT("(%s)"), *Level->URL.Map ));
}


/* scion ======================================================================
* ::DissociateViewports
* Author: epic
* 
* Destroy viewport player controllers and their pawns.
* This happens whenever a level is unloaded.
*
* =============================================================================
*/
void UGameEngine::DissociateViewports()
{
	// Dissociate Viewport actors.
	guard(DissociateViewports);
	
	if( !Client )
		return;

	for( INT i=0; i<Client->Viewports.Num(); i++ )
	{
		UViewport* Viewport = Client->Viewports(i);
		check(Viewport);
		APlayerController* Actor = Viewport->Actor;
		if( Actor )
		{
			ULevel* Level = Actor->GetLevel();
			check(Level);
			Viewport->Actor = NULL;
			if( Actor->Pawn ) 
			{
				Level->DestroyActor(Actor->Pawn);
			}
			//epic moelfke: changed this to call ConditionalDestroy() on Actor before setting Actor->Player to NULL
			// so APlayerController::Destroy() has access to it for saving stats
			Actor->ConditionalDestroy();
			Actor->Player = NULL;
			Level->DestroyActor( Actor );
		}		
		// scion sz - make sure to null the reference to the 
		// player controller in the profile				
		if( Viewport->Profile )
		{
			Viewport->Profile->eventSetPlayerOwner( NULL );
		}
	}

	// scion sz - remove Entry's playercontrollers
	for( INT Idx=0; Idx < GEntry->Actors.Num(); Idx++ )
	{
		APlayerController* ViewActor = Cast<APlayerController>(GEntry->Actors(Idx));
		if( ViewActor )
		{
            GEntry->DestroyActor( ViewActor );
		}
	}

	unguard;
}

//
// Browse to a specified URL, relative to the current one.
//
UBOOL UGameEngine::Browse( FURL URL, const TMap<FString,FString>* TravelInfo, FString& Error )
{
	guard(UGameEngine::Browse);
	Error = TEXT("");
	const TCHAR* Option;

	// Convert .unreal link files.
	const TCHAR* LinkStr = TEXT(".unreal");//!!
	if( appStrstr(*URL.Map,LinkStr)-*URL.Map==appStrlen(*URL.Map)-appStrlen(LinkStr) )
	{
		debugf( TEXT("Link: %s"), *URL.Map );
		FString NewUrlString;
		if( GConfig->GetString( TEXT("Link")/*!!*/, TEXT("Server"), NewUrlString, *URL.Map ) )
		{
			// Go to link.
			URL = FURL( NULL, *NewUrlString, TRAVEL_Absolute );//!!
		}
		else
		{
			// Invalid link.
			guard(InvalidLink);
			Error = FString::Printf( LocalizeError(TEXT("InvalidLink"),TEXT("Engine")), *URL.Map );
			unguard;
			return 0;
		}
	}

	// Crack the URL.
	debugf(NAME_DevRon,  TEXT("Browse: %s"), *URL.String() );

	// Handle it.
	if( !URL.Valid )
	{
		// Unknown URL.
		guard(UnknownURL);
		Error = FString::Printf( LocalizeError(TEXT("InvalidUrl"),TEXT("Engine")), *URL.String() );
		unguard;
		return 0;
	}
	else if( URL.HasOption(TEXT("failed")) )
	{
		// Handle failure URL.
		guard(FailedURL::AbortToEntry);
		debugf( NAME_Log, LocalizeError(TEXT("AbortToEntry"),TEXT("Engine")) );

		//epic moelfke: delete the pending level
		if (GPendingLevel)
		{
			CancelPending();
		}

#ifdef _XBOX
		// Clear any outstanding session queries so they don't finish while we're in the menus.
		XboxLiveManager* LiveManager = XboxLiveManager::getXboxLiveManager();
		if (LiveManager != NULL)
		{
			LiveManager->StopSessionQueries();
			LiveManager->m_MatchState = XboxLiveManager::MM_SESSION_NONE;
		}
#endif // _XBOX

		// rjp -- display reason for failure using ConnectionFailureMenu.  this menu reloads the
		// menu level when the user presses the OK button, so no need to do it here
		//epic moelfke: unless the local PlayerController is not in GLevel (if an error occurs during initial connection
		// but after loading, it will be in Entry while GLevel will be the newly loaded level)
		// In that case, we MUST return to the default level, otherwise nothing will be rendered and the user will be
		// unable to see the menu
		FString FailText = URL.GetOption(TEXT("failmsg="), TEXT(""));
		if (!FailText.Len() || !Client->Viewports(0)->Actor || Client->Viewports(0)->Actor->XLevel != GLevel)
		{
			FURL DisconnectURL(*FURL::DefaultLocalMap);
			if ( !URL.HasOption(TEXT("disconnect")) )
			{
				if ( Client->Viewports(0)->GUIController != NULL )
					Client->Viewports(0)->GUIController->bLostConnection = true;
			}
			else
			{
				// Transfer any special options when returning to menus
				FString ContextString = URL.GetOption(TEXT("MenuOptions="),TEXT(""));
				if ( ContextString != TEXT("") )
				{
					DisconnectURL.AddOption( *(FString(TEXT("MenuOptions=")) + ContextString));
				}
			}
			
			if( !LoadMap(DisconnectURL,NULL,NULL,Error) )
			{
				debugf( NAME_Error, LocalizeError(TEXT("FailedBrowse"),TEXT("Engine")), *FURL::DefaultLocalMap, *Error );
				return 0;
			}
		}
		if (Client->Viewports(0)->Actor && FailText.Len())
		{
			Client->Viewports(0)->Actor->eventClientOpenMenu(TEXT("UCMenu.ConnectionFailureMenu"), 0, FailText, URL.String());
		}

		return 1;
		unguard;
#if 0
		/*
		if( GLevel && GLevel!=GEntry )
			ResetLoaders( GLevel->GetOuter(), 1, 0 );
		NotifyLevelChange();

		GLevel = GEntry;
		GLevel->GetLevelInfo()->LevelAction = LEVACT_None;
		check(Client && Client->Viewports.Num());
		MatchViewportsToActors( Client, GLevel, URL );
		//CollectGarbage( RF_Native ); // Causes texture corruption unless you flush.
		if( URL.HasOption(TEXT("failed")) )
		{
			if( !GPendingLevel )
				SetProgress( LocalizeError(TEXT("ConnectionFailed"),TEXT("Engine")), TEXT(""), 6.f );

		}
		unguard;
		return 1;
		*/
#endif
	}
	else if( URL.HasOption(TEXT("pop")) )
	{
		// Pop the hub.
		guard(PopURL);
		if( GLevel && GLevel->GetLevelInfo()->HubStackLevel>0 )
		{
			TCHAR Filename[256], SavedPortal[256];
			appSprintf( Filename, TEXT("%s") PATH_SEPARATOR TEXT("Game%i.usa"), *GSys->SavePath, GLevel->GetLevelInfo()->HubStackLevel-1 );
			appStrcpy( SavedPortal, *URL.Portal );
			URL = FURL( &URL, Filename, TRAVEL_Partial );
			URL.Portal = SavedPortal;
		}
		else return 0;
		unguard;
	}
	else if( URL.HasOption(TEXT("restart")) )
	{
		// Handle restarting.
		guard(RestartURL);

		// Use GLevel->URL, since this will be the most up to date version, but
		// would also contain unwanted (no longer relevant) host information, so
		// copy the host info from LastURL.
		URL = GLevel ? GLevel->URL : LastURL;
		URL.Host = LastURL.Host;
		unguard;
	}
	else if( (Option=URL.GetOption(TEXT("load="),NULL))!=NULL )
	{
		// Handle loadgame.
		guard(LoadURL);
		FString Temp=FString::Printf( TEXT("%s") PATH_SEPARATOR TEXT("Save%i.usa?load"), *GSys->SavePath, appAtoi(Option) );
		if( LoadMap(FURL(&LastURL,*Temp,TRAVEL_Partial),NULL,NULL,Error) )
		{
			// Copy the hub stack.
			INT i;
			for( i=0; i<GLevel->GetLevelInfo()->HubStackLevel; i++ )
			{
				TCHAR Src[256], Dest[256];//!!
				appSprintf( Src, TEXT("%s") PATH_SEPARATOR TEXT("Save%i%i.usa"), *GSys->SavePath, appAtoi(Option), i );
				appSprintf( Dest, TEXT("%s") PATH_SEPARATOR TEXT("Game%i.usa"), *GSys->SavePath, i );
				GFileManager->Copy( Src, Dest );
			}
			while( 1 )
			{
				Temp = FString::Printf( TEXT("%s") PATH_SEPARATOR TEXT("Game%i.usa"), *GSys->SavePath, i++ );
				if( GFileManager->FileSize(*Temp)<=0 )
					break;
				GFileManager->Delete( *Temp );
			}
			LastURL = GLevel->URL;
			return 1;
		}
		else return 0;
		unguard;
	}

	UViewport* PrimaryViewport = Client && Client->Viewports.Num() && Client->Viewports(0)
		? Client->Viewports(0)
		: NULL;

	// Handle normal URL's.
	if( URL.IsLocalInternal() )
	{
		// Local map file.
		guard(LocalMapURL);
		// Calling InitializeURL when TravelType is TRAVEL_Absolute prevents things like
		// 'start menu?game=ucmenu.ucmenugame' from working correctly (since the game option would
		// be overwritten by the player's URL version)
		// If there is any reason why we need to call InitializeURL when the TravelType is TRAVEL_Absolute,
		// please document why
		if ( PrimaryViewport && PrimaryViewport->TravelType != TRAVEL_Absolute )
			PrimaryViewport->InitializeURL(URL);

		UBOOL MapRet = LoadMap(URL, NULL, TravelInfo, Error )!=NULL;
		return MapRet;
		unguard;
	}
	else if( URL.IsInternal() && GIsClient )
	{
		// Network URL.
		guard(NetworkURL);
		if( GPendingLevel )
			CancelPending();

		// Calling InitializeURL when TravelType is TRAVEL_Absolute prevents things like
		// 'start menu?game=ucmenu.ucmenugame' from working correctly (since the game option would
		// be overwritten by the player's URL version)
		// If there is any reason why we need to call InitializeURL when the TravelType is TRAVEL_Absolute,
		// please document why
		if ( PrimaryViewport && PrimaryViewport->TravelType != TRAVEL_Absolute  )
			PrimaryViewport->InitializeURL(URL);

		GPendingLevel = new UNetPendingLevel( this, URL );
		if( !GPendingLevel->NetDriver )
		{
			SetProgress( TEXT("Networking Failed"), *GPendingLevel->Error, 6.f );
			delete GPendingLevel;
			GPendingLevel = NULL;
		}
		else
		{
			//epic moelfke: see if we can load the level now before handshaking, which saves time when moving to a new
			//level on the same server since it will mean the client is loading the next level at the same time as the server
			((UNetPendingLevel*)GPendingLevel)->TryPreloadingLevel(this);

#if DEBUG_SERVERTRAVEL
			if ( GPendingLevel->GetDriver()->ServerConnection )
				GPendingLevel->GetDriver()->ServerConnection->bForceTimeout = true;
#endif
		}
		return 0;
		unguard;
	}
	else if( URL.IsInternal() )
	{
		// Invalid.
		guard(InvalidURL);
		Error = LocalizeError(TEXT("ServerOpen"),TEXT("Engine"));
		unguard;
		return 0;
	}
	else
	{
		// External URL.
		guard(ExternalURL);
		//Client->Viewports(0)->Exec(TEXT("ENDFULLSCREEN"));
		appLaunchURL( *URL.String(), TEXT(""), &Error );
		unguard;
		return 0;
	}
	unguard;
}

//
// Notify that level is changing
//
void UGameEngine::NotifyLevelChange()
{
	guard(UGameEngine::NotifyLevelChange);

// rjp - this breaks debugging dedicated servers on PC
#ifdef _XBOX
	checkSlow( Client );
#endif

	//epic moelfke: set level change in GLevel's LevelInfo so any script calls that get triggered know
	if (GLevel && GLevel->GetLevelInfo())
	{
		GLevel->GetLevelInfo()->bLevelChange = true;
	}

	// Make sure cinematic view is turned off when the level changes
    if( Client && Client->Viewports.Num() )
	{
		for( INT x = 0 ; x < Client->Viewports.Num() ; x++ )
		{
			UViewport* Viewport = Client->Viewports(x);
			if ( Viewport )
			{
				Viewport->bRenderCinematics = 0;

				// notify the canvas
				if ( Viewport->Canvas )
					Viewport->Canvas->NotifyLevelChange();
			}
		}
	}

	//scion bcl -- NotifyLevelChange was promoted to InteractionMaster (so it forwards the event to all interactions).
	if ( Client && Client->Viewports.Num() && Client->InteractionMaster ) {
		Client->InteractionMaster->eventNotifyLevelChange();
	}

	//epic rwd - notify audio of level change
    if (Audio != NULL)
	{
		Audio->NotifyLevelChange();
	}
	//epic rwd - also notify the voice subsystem of level change
	if (Voice != NULL &&
		Voice->Initialized())
	{
		Voice->NotifyLevelChange();
	}

	// scion sz - notify splitscreen manager of level change
	if( Client )
	{
		USplitScreenMgr* SplitScreenMgr = Client->GetSplitScreenMgr();
		check( SplitScreenMgr );
		SplitScreenMgr->NotifyLevelChange();
	}    	

	unguard;	
}

// Fixup a map
// hack to post release fix map actor problems without breaking compatibility
void UGameEngine::FixUpLevel( )
{
	debugf(TEXT("Level is %s %s"), GLevel->GetFullName(), GLevel->GetOuter()->GetName() );
	
	// Fixup CTF-Turmoil
	if( appStricmp( GLevel->GetOuter()->GetName(), TEXT("CTF-Turmoil") ) == 0 )
	{
		for( INT Idx = 0; Idx < GLevel->Actors.Num(); Idx++ )
		{
			AActor* TestActor = GLevel->Actors(Idx);
			if( TestActor )
			{
				// Make sure these static meshes will always show up
				if( appStricmp( TestActor->GetName(), TEXT("StaticMeshActor247") ) == 0 ||
					appStricmp( TestActor->GetName(), TEXT("StaticMeshActor259") ) == 0 ||
					appStricmp( TestActor->GetName(), TEXT("StaticMeshActor267") ) == 0 ||
					appStricmp( TestActor->GetName(), TEXT("StaticMeshActor268") ) == 0 )
				{
					TestActor->DetailLevel = DM_Low;
				}
			}				
		}
	}
	// Fixup DM-AcidRain
	else if( appStricmp( GLevel->GetOuter()->GetName(), TEXT("DM-AcidRain") ) == 0 )
	{
		for( INT Idx = 0; Idx < GLevel->Actors.Num(); Idx++ )
		{
			AActor* TestActor = GLevel->Actors(Idx);
			if( TestActor )
			{
				// Make sure these static meshes will always show up
				if( appStricmp( TestActor->GetName(), TEXT("StaticMeshActor19")  ) == 0 ||
					appStricmp( TestActor->GetName(), TEXT("StaticMeshActor102") ) == 0 )
				{
					TestActor->DetailLevel = DM_Low;
				}
			}				
		}
	}

	// fixup any movers that have the wrong physics mode
	for (INT i = 0; i < GLevel->Actors.Num(); i++)
	{
		if (GLevel->Actors(i) != NULL && GLevel->Actors(i)->IsAMover() && GLevel->Actors(i)->Physics != PHYS_MovingBrush)
		{
			GLevel->Actors(i)->setPhysics(PHYS_MovingBrush);
		}
	}
}

// Hack number 2 to fixup maps - called at the end of load map
void UGameEngine::PostFixupLevel( TCHAR* GameClassName )
{
	// Fixup CTF-TotalControl
	if (appStricmp(GLevel->GetOuter()->GetName(), TEXT("CTF-TotalControl")) == 0)
	{
		// Add blocking volume behind blue flag to prevent player from falling through terrain
		FVector Loc(5154.88f, -70097.86f, 3101.0f);
		UClass* ActorClass = LoadClass<AKeypoint>(NULL, TEXT("Gameplay.BlockAll"), NULL, 0, NULL);

		// HACK! BlockAll is bStatic but we need it to spawn anyway
		// So say we are the editor and that all is ok with the world of too many flags
		// Then reset it when we are done
		GIgnoreStaticFlag = 1;
		AActor* NewActor = GLevel->SpawnActor(ActorClass, NAME_None, Loc, FRotator(), NULL, IGNORE_SPAWN_Collision | IGNORE_SPAWN_DetailLevel, 0, NULL, NULL);
		GIgnoreStaticFlag = 0;
		if (NewActor)
		{
			NewActor->SetCollisionSize(768.f, 1024.f);
		}
	}

	// Get the map name and find the dash
	FString MapName = GLevel->GetOuter()->GetName();
	INT		Dash = MapName.InStr( TEXT("-") );
	// If there is a dash
	if( Dash > -1 )
	{
		// Get gametype abbrev
		FString GameType = MapName.Left( Dash );
		// If the map is an OD map but the game type is not OD
		if( appStricmp( *GameType, TEXT("OD") ) == 0 && 
			appStricmp( GameClassName, TEXT("UCGameplay.UCOverflowGame") ) != 0 )
		{
			// Look through all the actors
			for( INT Idx = 0; Idx < GLevel->Actors.Num(); Idx++ )
			{
				AActor* TestActor = GLevel->Actors(Idx);
				if( TestActor )
				{
					// Force delete any actor that is for OD only
					if( appStricmp( TestActor->GetClass()->GetName(), TEXT("UCFX_OverflowGoal_Green") )	== 0 ||
						appStricmp( TestActor->GetClass()->GetName(), TEXT("UCFX_OverflowGoal_White") )	== 0 ||
						appStricmp( TestActor->GetClass()->GetName(), TEXT("OverflowGoalBase") )		== 0 )

					{
						GLevel->DestroyActor( TestActor, 1, 1, 1 );
					}
				}
			}
		}
	}
}

/* scion ======================================================================
* UGameEngine::UnloadMap
* Author: sz
* 
* Removes all objects that were loaded after the engine was initialized and 
* the Entry level was loaded.  Assumes that all objects that should not be
* removed are flagged as RF_TagPersist.  The only level loaded after this
* function finishes should be Entry.  This is required for linear loading to
* work properly without rebooting - everything in a linear file must be 
* serialized, so we must always start from this consistent state.
*
* =============================================================================
*/
void UGameEngine::UnloadMap()
{
	guard(UGameEngine::UnloadMap);

	// Entry must be loaded by this point, unless we're a PC dedicated server
	if ((!GEntry && Client) || !GLevel)
	{
        return;
	}

	// Notify of the level change, before we dissociate Viewport actors
	NotifyLevelChange();

	// remove view actors from viewports
	DissociateViewports();

	// remove actors we may have destroyed in Entry
	if (Client)
	{
		GEntry->CleanupDestroyed( 1 );
	}

	// Clean up game state.
	guard(ExitLevel);
	// Shut down.
	ResetLoaders( GLevel->GetOuter(), 1, 0 );
	if( GLevel->NetDriver )
	{
		delete GLevel->NetDriver;
		GLevel->NetDriver = NULL;
	}
	if( GLevel->DemoRecDriver )
	{
		delete GLevel->DemoRecDriver;
		GLevel->DemoRecDriver = NULL;
	}
	unguard;

	guard(FlushResources);
	// flush GCache and RenDev resources
	//jg -- This MUST come before GC
	Flush(NULL);		
	unguard;

#ifdef _XBOX
	guard(UnloadLevel);

#if UNLOADMAP_DEBUG_LOG
	FOutputDeviceFile LogFileAr;
	static INT FileNum=0;
	appStrcpy( LogFileAr.Filename, *FString::Printf(TEXT("%s%d-UnloadLevel-%s.log"),
		appBaseDir(), FileNum++, GLevel->GetOuter()->GetName()) );
	FPrintLocalMem::CurrentMemUsed( TEXT("UnloadLevel before GC"), &LogFileAr );
#endif

	// remove any global objects
	for( INT Idx=0; Idx < GGlobalTempObjects->GlobalObjectPtrs.Num(); Idx++ )
	{
		UObject** pTempObj = GGlobalTempObjects->GlobalObjectPtrs(Idx);
		if( pTempObj && *pTempObj )
		{
			(*pTempObj)->SetFlags( RF_EliminateObject );
		}
	}
	// remove actors that were destroyed
	GLevel->CleanupDestroyed(1);

#if defined(_XBOX) && defined(XBOX_ASSET_FORMAT)
	// Unloads all non-native XPR files
	extern void UnloadAllXPRs();
	UnloadAllXPRs();
#ifdef _DEBUG
	debugf(TEXT("XPRs loaded after purge:"));
	extern void DumpLoadedXPRInfo();
	// Check for leaked XPRs
	DumpLoadedXPRInfo();
#ifdef USE_GCONTIG
	// Log the current state of the contiguous allocator
	GContigMalloc->DumpAllocs();
#endif
#endif
#endif

	// prevent GLevel from being serialized during GC
	// set current level to Entry
	GLevel = GEntry;
#if 0
	// remove all objects except for those tagged as persistent
	CollectGarbage( RF_TagPersist, 0 );
#else
	// remove objects that can not be serialized from root
	CollectGarbage( RF_Native );
#endif

	// free pooled particle item entries
	TPooledList<FParticle>::FreePool();
	// free pooled light list item entries
	FLightListPool::FreePool();
	// free pooled projector list item entries
	FProjectorListPool::FreePool();

#if UNLOADMAP_DEBUG_LOG
	FPrintLocalMem::CurrentMemUsed( TEXT("UnloadLevel after GC"), &LogFileAr );
	//Exec( TEXT("OBJ LIST class=actor package=entry"), LogFileAr );
	Exec( TEXT("OBJ LIST alphasort=1"), LogFileAr );	
	Client->Exec( TEXT("memused detail resource"), LogFileAr );
	LogFileAr.Flush();	
	// pause 
	//Client->Exec( TEXT("WAITFORKEYPRESS") );
#endif

//	Exec(TEXT("OBJ LIST CLASS=Class AlphaSort=true"));
//	Exec(TEXT("OBJ LIST AlphaSort=true"));
	unguard;
#endif //_XBOX

	unguard;
}


#if defined(_XBOX) || WITH_PORTABLE_MOVIES
	//scion jg -- Returns a pointer to the movie thread that runs for this URL
	FMovieThread* CreateLoadingMovie(const FURL& URL,const TCHAR* URLOpt,
		UBOOL bWaitForComplete,UBOOL bUseDefault);
	//scion jg -- Let the movie know it can be interrupted
	void SetEngineReady(void);
	// For playing our loading movie
	void PlayLoadingMovie(const FURL& URL,UBOOL bSinglePlayer);
	// For playing an intro cinematic (non-blocking, skippable)
	void PlayIntroCinematic(const FURL& URL);
	// For playing an outro cinematic (blocking, skippable)
	void PlayOutroCinematic(const FURL& URL);
	UBOOL IsPlayingMovie(void);
#endif


#if USE_LINEAR_LOADING
// For loading the menu assets, etc.
#include "LinLoadDLOs.h"
#endif

//
// Load a map.
//
ULevel* UGameEngine::LoadMap( const FURL& URL, UPendingLevel* Pending, const TMap<FString,FString>* TravelInfo, FString& Error, UBOOL bQuiet )
{
	guard(UGameEngine::LoadMap);
#ifndef FINAL_RELEASE
	FLOAT LoadTime = appSeconds();
#endif
	//jg -- Determine where we are going to or coming from
	UBOOL bIsEntry = appStrnicmp(*URL.Map,TEXT("entry"),5) == 0;	
#if USE_LINEAR_LOADING
	UBOOL bIsMenu = appStrnicmp(*URL.Map,TEXT("menu"),4) == 0;
#endif
#if !defined(LIN_SAVE) && (defined(_XBOX) || WITH_PORTABLE_MOVIES)
	UBOOL bWasMenu = appStrnicmp(*LastURL.Map,TEXT("menu"),4) == 0;
#endif

#if (defined(_XBOX) || WITH_PORTABLE_MOVIES) && !defined(_DEBUG) && !defined(LIN_SAVE)
	// Check to see if we have intro or outr movies to show
	UBOOL bHasIntroMovie = URL.GetOption(TEXT("StartCinematic"),NULL) != NULL;
	UBOOL bHasOutroMovie = URL.GetOption(TEXT("EndCinematic"),NULL) != NULL;
	// Reset the render states so that there are no textures/vbs set
	if (IsPlayingMovie() == FALSE && GLevel && GLevel->Engine->GRenDev)	// engine-uc2 (M9): no level or device on the first load / a server
	{
		// Flush render states
		GLevel->Engine->GRenDev->ResetRenderStates();
	}
	// Don't play any movies if loading entry
	if (bIsEntry == FALSE && IsPlayingMovie() == FALSE)
	{
		// If either of these are set then it's a single player match
		UBOOL bSinglePlayer = (bHasIntroMovie || bHasOutroMovie);
		// If we not in the menu, check for an Outro Cinematic
		if (bWasMenu == FALSE && bHasOutroMovie == TRUE)
		{
			// Play any outro cinematic
			PlayOutroCinematic(URL);
			// Make sure the background thread is dead
			SetEngineReady();
		}
		// Play the loading movie until we're done with basic loading
		PlayLoadingMovie(URL,bSinglePlayer);
	}
#endif 

	Error = TEXT("");
	debugf( NAME_Log, TEXT("LoadMap: %s"), *URL.String() );
	GInitRunaway();

	// Remember current level's stack level.
	INT SavedHubStackLevel = GLevel ? GLevel->GetLevelInfo()->HubStackLevel : 0;

	// Get network package map.
	UPackageMap* PackageMap = NULL;

	// Verify that we can load all packages we need.
	UObject* MapParent = NULL;

	// scion sz - unload the previous map (GLevel=GEntry)
	UnloadMap();

#if USE_LINEAR_LOADING
	check(GFileCacheManager->IsCachingFiles() == FALSE &&
		"Can't load while files are caching");
	// Load the level and all objects under it, using the proper Guid.
	extern UBOOL GLinearLoad;
	//jg -- Set the linear URL
	FString MapName(URL.Map);
	// Strip off the .unr if present
	INT Index = MapName.InStr(TEXT("."),1);
	if (Index != -1)
	{
		MapName = MapName.Left(MapName.Len() - 4);
	}
	// Now add on the LIN name
	MapName += TEXT(".LIN");
	// Figure out if this is entry. Entry is contained in Script.lin so we don't
	// need a new linear file
	if (bIsEntry == 0)
	{
		// Open the linear file which will funnel all loading through the LIN file
		GFileManager->OpenLinearFile(*MapName);
#if LIN_LOAD
		// Turn off logging during load
		extern UBOOL GLogDLOs;
		GLogDLOs = FALSE;
#endif
	}
	else
	{
		// Load all of the common objects/classes that we need and mark them as native
		LoadGeneralObjects();
		LoadGeneralClasses();
	}
#endif

	// Remember the URL.
	guard(RememberURL);
	LastURL = URL;
	unguard;

#ifndef _XBOX
	// Display loading screen.
	guard(LoadingScreen);
	if( Client && Client->Viewports.Num() && GLevel && !URL.HasOption(TEXT("quiet")) )
	{
		GLevel->GetLevelInfo()->LevelAction = LEVACT_Loading;
		GLevel->GetLevelInfo()->Pauser = NULL;
		PaintProgress();
		GLevel->GetLevelInfo()->LevelAction = LEVACT_None;
	}
	unguard;
#endif

	guard(PushLevel);
	if( GLevel )
	{
		if( URL.HasOption(TEXT("push")) )
		{
			// Save the current level minus players actors.
			GLevel->CleanupDestroyed( 1 );
			TCHAR Filename[256];
			appSprintf( Filename, TEXT("%s") PATH_SEPARATOR TEXT("Game%i.usa"), *GSys->SavePath, SavedHubStackLevel );
			SavePackage( GLevel->GetOuter(), GLevel, 0, Filename, GLog );
		}
	}
	unguard;

	// scion sz - entry needs to be loaded only once
	if( bIsEntry && GEntry ) 
	{
		// current level should be Entry
		GLevel = GEntry;
        // make sure viewports have valid actors
		check( Client && Client->Viewports.Num() );
		if( !Client->Viewports(0)->Actor )
		{
			MatchViewportsToActors( Client, GEntry, FURL(TEXT("entry")) );
		}
		// return Entry level
		return GLevel;
	}

#if defined(_XBOX) && defined(XBOX_ASSET_FORMAT)
	extern void LoadXPR(const FURL& URL);
	if (bIsEntry == FALSE)
	{
		//jg -- Load the XPR files for this level. NOTE: This must be done
		// before loading the level because of mesh data being fixed up in
		// PostLoad()
		LoadXPR(URL);
#ifdef LIN_SAVE
		extern void LoadXPRGroup(const TCHAR*,UBOOL);
		// Load every one of the specific XPRs so we don't have to deal
		// with crashes
		for (INT XPRIndex = 0; LoadXPRNames[XPRIndex] != NULL; XPRIndex++)
		{
			LoadXPRGroup(LoadXPRNames[XPRIndex],0);
		}
#endif
	}
#endif

#if USE_LINEAR_LOADING
	guard(LoadLevel);
#if LIN_LOAD
	{
		FLOAT Secs = appSeconds();
		// Load the map which will serialize everything from/to the LIN
		UObject* Package = UObject::LoadPackage(MapParent,
			bIsEntry == 0 ? *MapName : *URL.Map,LOAD_NoFail);
		check(Package);
		// Make sure we have a level object
		GLevel = FindObject<ULevel>(Package,TEXT("MyLevel"));
		check(GLevel);
		debugf(TEXT("%s loaded from loadpackage in %f seconds"),*URL.Map,appSeconds() - Secs);
	}
#endif
#if LIN_SAVE
	{
		// Or load the UNR file which will write the data to the LIN file
		UObject* Package = UObject::LoadPackage(MapParent,*URL.Map,LOAD_NoFail);
		check(Package);
		// Make sure we have a level object
		GLevel = FindObject<ULevel>(Package,TEXT("MyLevel"));
		check(GLevel);
	}
#endif
	unguard;
#else
	// Load the level and all objects under it, using the proper Guid.
	guard(LoadLevel);
	UObject* Package = UObject::LoadPackage(MapParent,*URL.Map,LOAD_NoFail);
	check(Package);
	// Make sure we have a level object
	GLevel = FindObject<ULevel>(Package,TEXT("MyLevel"));
	check(GLevel);
	//jg -- Empty the object tracking log post level load to only get things that
	// are loaded for gameplay
	StaticExec(TEXT("EMPTYLOADS"));
	unguard;	
#endif

#ifndef _XBOX
	// Verify classes.
	guard(VerifyClasses);
	UBOOL Mismatch = false;
	#define VERIFY_CLASS_SIZES
	#define NAMES_ONLY
	#define AUTOGENERATE_NAME(name)
	#define AUTOGENERATE_FUNCTION(cls,idx,name)
	#include "EngineClasses.h"
	#undef AUTOGENERATE_FUNCTION
	#undef AUTOGENERATE_NAME
	#undef NAMES_ONLY
	#undef VERIFY_CLASS_SIZES
	if( Mismatch )
		appErrorf(TEXT("C++/ Script class size mismatch"));

	VERIFY_CLASS_OFFSET( A, Actor,       Owner         );
	VERIFY_CLASS_OFFSET( A, Actor,       TimerCounter  );
	VERIFY_CLASS_OFFSET( A, PlayerController,  Player  );
	VERIFY_CLASS_OFFSET( A, Pawn,  Health );

	VERIFY_CLASS_SIZE( UCanvas );
    VERIFY_CLASS_SIZE( UCubemap );
    VERIFY_CLASS_SIZE( UEngine );
    VERIFY_CLASS_SIZE( UGameEngine );
    VERIFY_CLASS_SIZE( UPalette );
    VERIFY_CLASS_SIZE( UPlayer );
    VERIFY_CLASS_SIZE( UTexture );
	unguard;
#endif

	// Get LevelInfo.
	check(GLevel);
	ALevelInfo* Info = GLevel->GetLevelInfo();
	Info->ComputerName = appComputerName();

	// Handle pushing.
	guard(ProcessHubStack);
	Info->HubStackLevel
	=	URL.HasOption(TEXT("load")) ? Info->HubStackLevel
	:	URL.HasOption(TEXT("push")) ? SavedHubStackLevel+1
	:	URL.HasOption(TEXT("pop" )) ? Max(SavedHubStackLevel-1,0)
	:	URL.HasOption(TEXT("peer")) ? SavedHubStackLevel
	:	                              0;
	unguard;

	// Handle pending level.
	guard(ActivatePending);
	if( Pending )
	{
		check(Pending==GPendingLevel);

		// Hook network driver up to level.
		GLevel->NetDriver = Pending->NetDriver;
		if( GLevel->NetDriver )
		{
			GLevel->NetDriver->Notify = GLevel;
#ifdef DEBUG_SERVERTRAVEL
			// rjp -- reset forcetimeout value
			if ( GLevel->NetDriver->ServerConnection )
				GLevel->NetDriver->ServerConnection->bForceTimeout = false;
#endif
		}

		// Hook demo playback driver to level
		GLevel->DemoRecDriver = Pending->DemoRecDriver;
		if( GLevel->DemoRecDriver )
			GLevel->DemoRecDriver->Notify = GLevel;

		// Setup level.
		Info->NetMode = NM_Client;
	}
	else check(!GLevel->NetDriver);
	unguard;

	// Set level info.
	guard(InitLevel);
	if( !URL.GetOption(TEXT("load"),NULL) )
		GLevel->URL = URL;
	Info->EngineVersion = FString::Printf( TEXT("%i"), ENGINE_VERSION );
	Info->MinNetVersion = FString::Printf( TEXT("%i"), ENGINE_MIN_NET_VERSION );
	GLevel->Engine = this;
	if( TravelInfo )
		GLevel->TravelInfo = *TravelInfo;
	unguard;

#ifndef _XBOX
	// Remove cubemaps.
	if( GLevel->Engine->GRenDev && GLevel->Engine->GRenDev->Is3dfx )
	{
		for( TObjectIterator<UModifier> It; It; ++It )
		{
			if( It->Material && It->Material->IsA(UCubemap::StaticClass()) )
				It->Material = NULL;
		}

		for( TObjectIterator<UCombiner> It; It; ++It )
		{
			if( It->Material1 && It->Material1->IsA(UCubemap::StaticClass()) )
			{
				It->Material1 = It->Material2;
				It->Material2 = NULL;
			}
			if( It->Material2 && It->Material2->IsA(UCubemap::StaticClass()) )
				It->Material2 = NULL;
		}

		for( TObjectIterator<UShader> It; It; ++It )
		{
			if( It->Diffuse && It->Diffuse->IsA(UCubemap::StaticClass()) )
				It->Diffuse = NULL;

			if( It->Opacity && It->Opacity->IsA(UCubemap::StaticClass()) )
				It->Opacity = NULL;

			if( It->Specular && It->Specular->IsA(UCubemap::StaticClass()) )
				It->Specular = NULL;

			if( It->SpecularityMask && It->SpecularityMask->IsA(UCubemap::StaticClass()) )
				It->SpecularityMask = NULL;

			if( It->SelfIllumination && It->SelfIllumination->IsA(UCubemap::StaticClass()) )
				It->SelfIllumination = NULL;

			if( It->SelfIlluminationMask && It->SelfIlluminationMask->IsA(UCubemap::StaticClass()) )
				It->SelfIlluminationMask = NULL;

			if( It->Detail && It->Detail->IsA(UCubemap::StaticClass()) )
				It->Detail = NULL;
		}
	}

	// Purge unused objects and flush caches.
	guard(Cleanup);
	if( bIsEntry == 0 )
	{
		Flush(0);
		UViewport* Viewport = NULL;
		if (Audio != NULL)
		{
			Viewport = Audio->GetViewport();
			Audio->SetViewport(NULL);
		}
		if (Voice != NULL &&
			Voice->Initialized())
		{
			Voice->SetViewport(NULL);
		}
		{for( TObjectIterator<AActor> It; It; ++It )
			if( It->IsIn(GLevel->GetOuter()) )
				It->SetFlags( RF_EliminateObject );}
		{for( INT i=0; i<GLevel->Actors.Num(); i++ )
			if( GLevel->Actors(i) )
				GLevel->Actors(i)->ClearFlags( RF_EliminateObject );}
		CollectGarbage( RF_Native );
		if (Audio != NULL)
		{
			Audio->SetViewport(Viewport);
		}
		if (Voice != NULL &&
			Voice->Initialized())
		{
			Voice->SetViewport(Viewport);
		}
	}
	unguard;
#endif //!_XBOX

#if USE_LINEAR_LOADING && LIN_LOAD
	//jg -- Loop through and perform mandatory XPR loading
	for (INT Index = 0; Index < Info->RequiredXPRs.Num(); Index++)
	{
		extern void LoadXPRGroup(const TCHAR*,UBOOL);
		LoadXPRGroup(*Info->RequiredXPRs(Index),FALSE);
	}
#endif

	// Tell the audio driver to clean up.
	if( Audio )
	{
		Audio->CleanUp();
	}

	//scion jg -- Don't let our physics volume be deleted underneath us
	// during gameplay. This will force the creation of a new default
	// physics volume
	if (Info && Info->PhysicsVolume && Info->PhysicsVolume->bDeleteMe)
	{
		Info->PhysicsVolume = NULL;
	}

	FLOAT Secs = appSeconds();
	// Init collision.
	GLevel->SetActorCollision( 1 );
	debugf(TEXT("SetActorCollision() took %f"),appSeconds() - Secs);

	// Update the LevelInfo's time.
	GLevel->UpdateTime(Info);

	// Init the game info.
	TCHAR Options[1024]=TEXT("");
	TCHAR GameClassName[256]=TEXT("");
	guard(InitGameInfo);
	for( INT i=0; i<URL.Op.Num(); i++ )
	{
		appStrcat( Options, TEXT("?") );
		appStrcat( Options, *URL.Op(i) );
		Parse( *URL.Op(i), TEXT("GAME="), GameClassName, ARRAY_COUNT(GameClassName) );
	}

	debugf( NAME_DevSam,  TEXT("Browse: %s"), *URL.String() );
	debugf( NAME_DevSam, TEXT("Game=%s"), GameClassName );

#if USE_LINEAR_LOADING
	Secs = appSeconds();
	//jg -- Hardcode which gametypes are loaded for linear loading
	// For CTF maps just load the CTF game type. For the menu, only the menu
	// gametype. For DM maps load both TDM and DM gametypes.
	UBOOL bIsCTFGame = bIsMenu == 0 && appStrnicmp(*URL.Map,TEXT("CTF"),3) == 0;
	UBOOL bIsOverdose = bIsMenu == 0 && appStrnicmp(*URL.Map,TEXT("OD"),2) == 0;
	// The normal game modes require some extra classes to be force loaded
	// so skip those for entry and menu
	if (bIsEntry == 1 || bIsMenu == 1)
	{
		if (bIsEntry == 1)
		{
			// Load the super tiny game class
			UClass* GameClass = StaticLoadClass( AGameInfo::StaticClass(), NULL,
				TEXT("Engine.MinimalGameInfo"), NULL, LOAD_NoFail, PackageMap );
			check(GameClass != NULL);
			// Only spawn the type if we are the server
			if (GLevel->IsServer())
			{
				// Now create it
				Info->Game = (AGameInfo*)GLevel->SpawnActor( GameClass );
				check(Info->Game != NULL);
			}
		}
		// Are we loading the menu
		else if (bIsMenu == 1)
		{
			// Load all the required assets
			LoadMenuObjects();
			// Load all the required classes
			LoadMenuClasses();
			// Load the menu game class
			UClass* MenuGameClass = StaticLoadClass( AGameInfo::StaticClass(), NULL,
				TEXT("UCMenu.UCMenuGame"), NULL, LOAD_NoFail, PackageMap );
			check(MenuGameClass != NULL);
			// Only spawn the type if we are the server
			if (GLevel->IsServer())
			{
				// Now create it
				Info->Game = (AGameInfo*)GLevel->SpawnActor( MenuGameClass );
				check(Info->Game != NULL);
			}
		}
	}
	else
	{
		// Now do the game type specific loads
		if (bIsCTFGame == 1)
		{
			TArray<UObject*> Ignored;
			// Load all the required assets
			LoadCTFClassesObjects(Ignored);
			// Load the CTF game class
			UClass* GameClass = StaticLoadClass(AGameInfo::StaticClass(), NULL,
				TEXT("UCGameplay.CTFGame"), NULL, LOAD_NoFail, PackageMap);
			check(GameClass != NULL);
			// Only spawn the type if we are the server
			if (GLevel->IsServer())
			{
				// Now create it
				Info->Game = (AGameInfo*)GLevel->SpawnActor( GameClass );
				check(Info->Game != NULL);
			}
		}
		else if (bIsOverdose)
		{
			TArray<UObject*> Ignored;
			// Load all the required assets
			LoadOverdoseClassesObjects(Ignored);
			// Load the Overdose game class
			UClass* ODGameClass = StaticLoadClass(AGameInfo::StaticClass(), NULL,
				TEXT("UCGameplay.UCOverflowGame"), NULL, LOAD_NoFail, PackageMap);
			check(ODGameClass != NULL);
			// Load the DM game class
			UClass* GameClass = StaticLoadClass(AGameInfo::StaticClass(), NULL,
				TEXT("UCGameplay.UCDeathMatch"), NULL, LOAD_NoFail, PackageMap);
			check(GameClass != NULL);
			// Only spawn the type if we are the server
			if (GLevel->IsServer())
			{
				// Compare the names to what got passed on the URL
				if (appStrfind(GameClassName,ODGameClass->GetName()) != 0)
				{
					// Now create the overdose match
					Info->Game = (AGameInfo*)GLevel->SpawnActor( ODGameClass );
					check(Info->Game != NULL);
				}
				else if (appStrfind(GameClassName,GameClass->GetName()) != 0)
				{
					// Now create the overdose match
					Info->Game = (AGameInfo*)GLevel->SpawnActor( GameClass );
					check(Info->Game != NULL);
				}
			}
		}
		else
		{
			// Load the DM game class
			UClass* GameClass = StaticLoadClass(AGameInfo::StaticClass(), NULL,
				TEXT("UCGameplay.UCDeathMatch"), NULL, LOAD_NoFail, PackageMap);
			check(GameClass != NULL);
			// Load the TDM game class
			UClass* TeamGameClass = StaticLoadClass(AGameInfo::StaticClass(), NULL,
				TEXT("UCGameplay.UCTeamGame"), NULL, LOAD_NoFail, PackageMap);
			check(TeamGameClass != NULL);
			// Load the Overdose game class
			UClass* SurvivalGameClass = StaticLoadClass(AGameInfo::StaticClass(), NULL,
				TEXT("UCGameplay.UCSurvivalGame"), NULL, LOAD_NoFail, PackageMap);
			check(SurvivalGameClass != NULL);
			// Load nalislaughter so we can check for it
			UClass* NaliSlaughterGame = StaticLoadClass(AGameInfo::StaticClass(), NULL,
				TEXT("UCGameplay.NaliSlaughterGame"), NULL, LOAD_NoFail, PackageMap);
			check(NaliSlaughterGame != NULL);
			// Handle single player ladder gametype
			UClass* SPGame = StaticLoadClass(AGameInfo::StaticClass(), NULL,
				TEXT("UCGameplay.UCTrainingGameType"), NULL, LOAD_NoFail, PackageMap);
			check(SPGame != NULL);
			// Only spawn the type if we are the server
			if (GLevel->IsServer())
			{
				// Compare the names to what got passed on the URL
				if (appStrfind(GameClassName,GameClass->GetName()) != 0)
				{
					// Now create the death match game
					Info->Game = (AGameInfo*)GLevel->SpawnActor( GameClass );
					check(Info->Game != NULL);		
				}
				else if (appStrfind(GameClassName,TeamGameClass->GetName()) != 0)
				{
					// Now create the team game
					Info->Game = (AGameInfo*)GLevel->SpawnActor( TeamGameClass );
					check(Info->Game != NULL);		
				}
				else if (appStrfind(GameClassName,SurvivalGameClass->GetName()) != 0)
				{
					// Now create the survival game
					Info->Game = (AGameInfo*)GLevel->SpawnActor( SurvivalGameClass );
					check(Info->Game != NULL);		
				}
				else if (appStrfind(GameClassName,NaliSlaughterGame->GetName()) != 0)
				{
					// Now create the nalislaughter game
					Info->Game = (AGameInfo*)GLevel->SpawnActor( NaliSlaughterGame );
					check(Info->Game != NULL);		
				}
				else if (appStrfind(GameClassName,SPGame->GetName()) != 0)
				{
					// Now create the singleplayer ladder game
					Info->Game = (AGameInfo*)GLevel->SpawnActor( SPGame );
					check(Info->Game != NULL);		
				}
				else
				{
					// BAD BAD BAD
					// Assume they meant death match
					Info->Game = (AGameInfo*)GLevel->SpawnActor( GameClass );
					check(Info->Game != NULL);		
				}
			}
		}
	}
	debugf(TEXT("Per game type loads took %f"),appSeconds() - Secs);
#else
	if( GLevel->IsServer() && !Info->Game )
	{
		// Get the GameInfo class.
		UClass* GameClass=NULL;

		// If the ?Game parameter was passed on the URL, attempt to load that gameinfo class
		if ( GameClassName[0] )
			GameClass = StaticLoadClass( AGameInfo::StaticClass(), NULL, GameClassName, NULL, LOAD_NoFail, PackageMap );

		// If we don't have a GameInfo yet, and we're attempting to load Entry.unr, use the MinimalGameInfo class
		if( !GameClass && appStricmp(GLevel->GetOuter()->GetName(),TEXT("Entry"))==0 )
		{
			// Always use StaticLoadClass to load script classes - do not reference the AMinimalGameInfo class directly
			// or the defaults for the class will not be loaded if this class hasn't been loaded since the last garbage collection
			//GameClass = AMinimalGameInfo::StaticClass();
			Info->DefaultGameType = TEXT("Engine.MinimalGameInfo");
		}

		// If we still don't have a GameInfo class, see if the map has an associated gametype it would like to use
		if( !GameClass && Info->DefaultGameType.Len() > 0 )
			GameClass = StaticLoadClass( AGameInfo::StaticClass(), NULL, *(Info->DefaultGameType), NULL, LOAD_NoFail, PackageMap );

		// Still nothing?  try loading the default GameInfo class as specified in the ini file.
		if( !GameClass )
			GameClass = StaticLoadClass( AGameInfo::StaticClass(), NULL, Client ? TEXT("ini:Engine.Engine.DefaultGame") : TEXT("ini:Engine.Engine.DefaultServerGame"), NULL, LOAD_NoFail, PackageMap );

		// If that didn't work, just load the base gameinfo class
        if ( !GameClass )
		{
			// see comment above regarding referencing script classes directly
			//GameClass = AGameInfo::StaticClass();

			GameClass = StaticLoadClass(AGameInfo::StaticClass(), NULL, TEXT("Engine.GameInfo"), NULL, LOAD_NoFail, PackageMap);
		}

		check(GameClass); 

		// Spawn the GameInfo.
		debugf( NAME_Log, TEXT("Game class is '%s'"), GameClass->GetName() );
		Info->Game = (AGameInfo*)GLevel->SpawnActor( GameClass );
		check(Info->Game!=NULL);		
	}
#endif
	unguard;

#if USE_LINEAR_LOADING && LIN_LOAD
	//epic moelfke: check if the gametype has any mandatory XPR loading
	if (Info->Game)
	{
		for (INT Index = 0; Index < Info->Game->RequiredXPRs.Num(); Index++)
		{
			extern void LoadXPRGroup(const TCHAR*, UBOOL);
			LoadXPRGroup(*Info->Game->RequiredXPRs(Index), FALSE);
		}
	}
#endif

	//epic moelfke: make sure the sound/wave banks for this map have been cached, as otherwise XACT will go crazy
	UBOOL bOldBegunPlay = Info->bBegunPlay;
	Info->bBegunPlay = true;
	Info->eventPrecacheMap(FString(GLevel->GetOuter()->GetName()));
#ifndef FINAL_RELEASE
	if (GFileCacheManager && GFileCacheManager->IsCachingFiles())
	{
		debugf(TEXT("Needed to cache files for map %s"), GLevel->GetOuter()->GetName());
	}
#endif
	while (GFileCacheManager && GFileCacheManager->IsCachingFiles())
	{
		appSleep(0.001f);
	}
	Info->bBegunPlay = bOldBegunPlay;

	Secs = appSeconds();
	//epic rwd - check to create the voice system and initialize
	InitAudio();
	if( Audio != NULL &&
		Client && 
		Client->Viewports.Num() )
	{
		Audio->SetViewport( Client->Viewports(0) );
	}
	debugf(TEXT("InitAudio() took %f"),appSeconds() - Secs);

	// Init detail.
	Info->DetailMode = DM_SuperHigh;
	if(Client && Client->Viewports.Num() && Client->Viewports(0)->RenDev)
	{
		if(Client->Viewports(0)->RenDev->SuperHighDetailActors)
			Info->DetailMode = DM_SuperHigh;
		else if(Client->Viewports(0)->RenDev->HighDetailActors)
			Info->DetailMode = DM_High;
		else
			Info->DetailMode = DM_Low;
	}

#ifndef FINAL_RELEASE
	// Clear any existing stat graphs.
	if(GStatGraph)
		GStatGraph->Reset();
#endif

	// Apply any script fixups
	FScriptPatcher Patcher;

	Secs = appSeconds();
	// Init level gameplay info.
	guard(BeginPlay);
	GLevel->iFirstDynamicActor = 0;
	if( !Info->bBegunPlay )
	{
		// fix up level problems
		FixUpLevel();
		if ( !bIsEntry )
			Patcher.ApplyPatches();

		// Check that paths are valid
		if ( !GLevel->GetLevelInfo()->bPathsRebuilt )
			debugf(TEXT("*** WARNING - PATHS MAY NOT BE VALID ***"));
		// Lock the level.
		debugf( NAME_Log, TEXT("Bringing %s up for play (%i)..."), GLevel->GetFullName(), appRound(GetMaxTickRate()) );
		GLevel->FinishedPrecaching = 0;
		GLevel->TimeSeconds = 0;
		GLevel->GetLevelInfo()->TimeSeconds = 0;
		GLevel->GetLevelInfo()->GetDefaultPhysicsVolume()->bNoDelete = true;

		if (Voice != NULL)
		{
			Voice->Init(Audio);
		}

		// Kill off actors that aren't interesting to the client.
		if( !GLevel->IsServer() )
		{
			for( INT i=0; i<GLevel->Actors.Num(); i++ )
			{
				AActor* Actor = GLevel->Actors(i);
				if( Actor )
				{
					if( Actor->bStatic || Actor->bNoDelete )
						Exchange( Actor->Role, Actor->RemoteRole );
					else
						GLevel->DestroyActor( Actor );
				}
			}
		}

		// Init touching actors & clear LastRenderTime
		for( INT i=0; i<GLevel->Actors.Num(); i++ )
			if( GLevel->Actors(i) )
			{
				GLevel->Actors(i)->LastRenderTime = 0.f;
				GLevel->Actors(i)->Touching.Empty();
				GLevel->Actors(i)->PhysicsVolume = GLevel->GetLevelInfo()->GetDefaultPhysicsVolume();
			}

		// Init scripting.
		for( INT i=0; i<GLevel->Actors.Num(); i++ )
			if( GLevel->Actors(i) )
				GLevel->Actors(i)->InitExecution();

		// Enable actor script calls.
		Info->bBegunPlay = 1;
		Info->bStartup = 1;

#ifdef WITH_KARMA
		if(!GIsEditor && !GLevel->GetLevelInfo()->bKNoInit)
		{
			KInitLevelKarma(GLevel);

			for( INT i=0; i<GLevel->Actors.Num(); i++ )
				if( GLevel->Actors(i) )
					KInitActorKarma( GLevel->Actors(i) );
		}
#endif

		// Set the net mode before the game is inited
		GLevel->GetLevelInfo()->NetMode = !GLevel->IsServer() ? NM_Client :
			GLevel->Engine->Client == NULL ? NM_DedicatedServer :
			URL.HasOption(TEXT("Listen")) ? NM_ListenServer : NM_Standalone;

		// Init the game.
		if( Info->Game )
		{
			//epic moelfke: removed unused Error parameter from InitGame()
			Info->Game->eventInitGame(Options);
		}

		// Send PreBeginPlay.
		for( INT i=0; i<GLevel->Actors.Num(); i++ )
			if( GLevel->Actors(i) && !GLevel->Actors(i)->bScriptInitialized )
				GLevel->Actors(i)->eventPreBeginPlay();

		// Set BeginPlay.
		for( INT i=0; i<GLevel->Actors.Num(); i++ )
			if( GLevel->Actors(i) && !GLevel->Actors(i)->bScriptInitialized )
				GLevel->Actors(i)->eventBeginPlay();

		// Set zones && gather volumes.
		TArray<AVolume*> LevelVolumes;
		for( INT i=0; i<GLevel->Actors.Num(); i++ )
		{
			if( GLevel->Actors(i) && !GLevel->Actors(i)->bScriptInitialized )
				GLevel->Actors(i)->SetZone( 1, 1 );

			AVolume* Volume = Cast<AVolume>(GLevel->Actors(i));
			if( Volume )
				LevelVolumes.AddItem(Volume);
		}
		
		// Set appropriate volumes for each actor.
		for( INT i=0; i<GLevel->Actors.Num(); i++ )
			if( GLevel->Actors(i) && !GLevel->Actors(i)->bScriptInitialized )
				GLevel->Actors(i)->SetVolumes( LevelVolumes );

		// Post begin play.
		for( INT i=0; i<GLevel->Actors.Num(); i++ )
			if( GLevel->Actors(i) && !GLevel->Actors(i)->bScriptInitialized )
			{
				GLevel->Actors(i)->eventPostBeginPlay();

				if(GLevel->Actors(i))
					GLevel->Actors(i)->PostBeginPlay();
			}

		// Post net begin play.
		for( INT i=0; i<GLevel->Actors.Num(); i++ )
			if( GLevel->Actors(i) && !GLevel->Actors(i)->bScriptInitialized )
				GLevel->Actors(i)->eventPostNetBeginPlay();

		// Begin scripting.
		for( INT i=0; i<GLevel->Actors.Num(); i++ )
			if( GLevel->Actors(i) && !GLevel->Actors(i)->bScriptInitialized )
				GLevel->Actors(i)->eventSetInitialState();

		// Find bases
		for( INT i=0; i<GLevel->Actors.Num(); i++ )
		{
			if( GLevel->Actors(i) )
			{
				if ( GLevel->Actors(i)->AttachTag != NAME_None )
				{
					//find actor to attach self onto
					for( INT j=0; j<GLevel->Actors.Num(); j++ )
					{
						if( GLevel->Actors(j)
							&& ((GLevel->Actors(j)->Tag == GLevel->Actors(i)->AttachTag) || (GLevel->Actors(j)->GetFName() == GLevel->Actors(i)->AttachTag))  )
						{
							GLevel->Actors(i)->SetBase(GLevel->Actors(j), FVector(0,0,1), 0);
							break;
						}
					}
				}
				else if( GLevel->Actors(i)->bCollideWorld && GLevel->Actors(i)->bShouldBaseAtStartup
				 &&	((GLevel->Actors(i)->Physics == PHYS_None) || (GLevel->Actors(i)->Physics == PHYS_Rotating)) )
				{
					 GLevel->Actors(i)->FindBase();
				}
			}
		}

#if defined(_XBOX)
	FLOAT ProjSecs = appSeconds();

#if !defined(XBOX_ASSET_FORMAT)
	// scion sz - batch the geometry generated by any static projectors
	for( INT ActorIdx=0; ActorIdx < GLevel->Actors.Num(); ActorIdx++ )
	{
		AProjector* Projector = Cast<AProjector>( GLevel->Actors(ActorIdx) );
		if( Projector &&
			Projector->bLevelStatic &&
			!Projector->bDynamicAttach )
		{
			Projector->RebuildStaticPrimsBatch();
		}
	}	
	// scion sz - done with static mesh indices (used for clipping with projectors)
	// so they can be removed now
	for( TObjectIterator<UStaticMesh> It; It; ++It )
	{
        UStaticMesh* StaticMesh = *It;
		StaticMesh->IndexBuffer.Indices.Empty();
	}
#endif //!XBOX_ASSET_FORMAT
	
	debugf(TEXT("ProjSecs were %f"),appSeconds() - ProjSecs);
#endif //_XBOX

#if 0
		for( INT i=0; i<GLevel->Actors.Num(); i++ )
		{
			if(GLevel->Actors(i))
			{
				AActor* actor = GLevel->Actors(i);				

#ifdef WITH_KARMA
				if(actor->Physics != PHYS_Karma || !actor->KParams || !actor->KParams->IsA(UKarmaParams::StaticClass()))
					continue;

				UKarmaParams* kparams = Cast<UKarmaParams>(actor->KParams);

				// If running below HighDetailPhysics, turn off karma dynamics for actors with bHighDetailOnly set true.
				if(	GLevel->GetLevelInfo()->PhysicsDetailLevel < PDL_High && kparams->bHighDetailOnly )
					KTermActorDynamics(actor);

				// If dedicated server, turn off karma for actors with bHighDetailOnly or bClientsOnly
				if(	GLevel->GetLevelInfo()->NetMode == NM_DedicatedServer && (kparams->bHighDetailOnly || kparams->bClientOnly) )					
					KTermActorDynamics(actor);
#endif
			}
		}
#endif

		// Preprocess Index/Vertex buffers for skeletal actors currently in memory.
		// #DEBUG - currently disabled.
#if (0)
		for( TObjectIterator<USkeletalMesh> It; It; ++It )
		{
			USkeletalMesh* SkelMesh = *It;				
			debugf(TEXT("D3D-predigesting skeletal mesh: %s"),SkelMesh->GetFullName());
			SkelMesh->RenderPreProcess( Client->HardwareSkinning );
		}
#endif


		Info->bStartup = 0;
	}
	else GLevel->TimeSeconds = GLevel->GetLevelInfo()->TimeSeconds;
	unguard;
	debugf(TEXT("BeginPlay() took %f"),appSeconds() - Secs);

	Secs = appSeconds();
	// Rearrange actors: static first, then others.
	guard(Rearrange);
	TArray<AActor*> Actors;
	Actors.AddItem(GLevel->Actors(0));
	Actors.AddItem(GLevel->Actors(1));
	for( INT i=2; i<GLevel->Actors.Num(); i++ )
		if( GLevel->Actors(i) && GLevel->Actors(i)->bStatic && !GLevel->Actors(i)->bAlwaysRelevant )
			Actors.AddItem( GLevel->Actors(i) );
	GLevel->iFirstNetRelevantActor=Actors.Num();
	for( INT i=2; i<GLevel->Actors.Num(); i++ )
		if( GLevel->Actors(i) && GLevel->Actors(i)->bStatic && GLevel->Actors(i)->bAlwaysRelevant )
			Actors.AddItem( GLevel->Actors(i) );
	GLevel->iFirstDynamicActor=Actors.Num();
	for( INT i=2; i<GLevel->Actors.Num(); i++ )
		if( GLevel->Actors(i) && !GLevel->Actors(i)->bStatic )
			Actors.AddItem( GLevel->Actors(i) );
	GLevel->Actors.Empty();
	GLevel->Actors.Add( Actors.Num() );
	for( INT i=0; i<Actors.Num(); i++ )
		GLevel->Actors(i) = Actors(i);
	unguard;
	debugf(TEXT("rearrange() took %f"),appSeconds() - Secs);

#ifdef USE_LINEAR_LOADING
	// Entry is included in script.lin and will be closed when menu.lin is opened
	if (bIsEntry == 0)
	{
		// Clear the linear URL to go back to normal loading
		GFileManager->CloseLinearFile();
#if LIN_LOAD
		// Turn on logging post load
		extern UBOOL GLogDLOs;
		GLogDLOs = FALSE;
#endif
	}
#endif
	//jg -- Fire the PostLinearLoad() event so that we can do dynamic actions
	if( Info->Game )
	{
		Info->Game->eventPostLinearLoad();
	}
	//epic moelfke: on the client, flag the LevelInfo if we're joining a team game (needed for dynamic loading code)
	else if (GameClassName[0])
	{
		UClass* GameClass = StaticLoadClass(AGameInfo::StaticClass(), NULL, GameClassName, NULL, LOAD_NoFail, PackageMap);
		if (GameClass)
		{
			Info->bPendingNetTeamGame = GameClass->GetDefaultObject<AGameInfo>()->bTeamGame;
		}
	}

#if 1//jg -- Moved to happen after linear file is closed
	Secs = appSeconds();
	if (bIsEntry == 0)
	{
		// Get network package map.
		if( Pending )
			PackageMap = Pending->GetDriver()->ServerConnection->PackageMap;

		guard(VerifyPackages);
		try
		{
			BeginLoad();
			if( Pending )
			{
				// Verify that we can load everything needed for client in this network level.
				for (INT i = 0; i < PackageMap->List->Num(); i++)
				{
					(*PackageMap->List)(i).Linker = GetPackageLinker
					(
						(*PackageMap->List)(i).Parent,
						NULL,
						LOAD_Verify | LOAD_Throw | LOAD_NoWarn | LOAD_NoVerify,
						NULL,
						&(*PackageMap->List)(i).Guid
					);
				}
				for (INT i = 0; i < PackageMap->List->Num(); i++)
				{
					VerifyLinker((*PackageMap->List)(i).Linker);
				}
				if (PackageMap->List->Num())
				{
					MapParent = (*PackageMap->List)(0).Parent;
				}
			}
			LoadObject<ULevel>( MapParent, TEXT("MyLevel"), *URL.Map, LOAD_Verify | LOAD_Throw | LOAD_NoWarn, NULL );
			EndLoad();
		}
		#if UNICODE
		catch( TCHAR* CatchError )
		#else
		catch( char* CatchError )
		#endif
		{
			// Safely failed loading.
			EndLoad();
			Error = CatchError;
			#if UNICODE
			TCHAR *e = CatchError;
			#else
			TCHAR *e = ANSI_TO_TCHAR(CatchError);
			#endif
			//epic moelfke: don't call SetProgress() if we should be quiet
			if (!bQuiet)
			{
				SetProgress( LocalizeError(TEXT("UrlFailed"),TEXT("Core")), e, 6.f );
			}
			//epic moelfke: since this was moved to after the level takes the NetDriver away from the pending level
			//if we fail we must give it back
			if (GLevel->NetDriver)
			{
				GLevel->NetDriver->Notify = Pending;
				GLevel->NetDriver = NULL;
			}
			if (GLevel->DemoRecDriver)
			{
				GLevel->DemoRecDriver->Notify = NULL;
				GLevel->DemoRecDriver = NULL;
			}
			
			return NULL;
		}
		unguard;

		// If pending network level.
		if( Pending )
		{
			// If playing this network level alone, ditch the pending level.
			if( Pending && Pending->LonePlayer )
				Pending = NULL;

			// Setup network package info.
			if( PackageMap )
			{
				PackageMap->Compute();
				for (INT i = 0; i < PackageMap->List->Num(); i++)
				{
					if ((*PackageMap->List)(i).LocalGeneration != (*PackageMap->List)(i).RemoteGeneration)
					{
						Pending->GetDriver()->ServerConnection->Logf(TEXT("HAVE GUID=%s GEN=%i"), (*PackageMap->List)(i).Guid.String(), (*PackageMap->List)(i).LocalGeneration);
					}
				}
			}		
		}
	}
	debugf(TEXT("Verify took %f"),appSeconds() - Secs);
#endif // 1

	//jg -- Moved the listen initialization to after the packagemap was
	// built and after the client was inited
	// Listen for clients.
	guard(Listen);
	if( !Client || URL.HasOption(TEXT("Listen")) )
	{
		if( GPendingLevel )
		{
			guard(CancelPendingForListen);
			check(!Pending);
			delete GPendingLevel;
			GPendingLevel = NULL;
			unguard;
		}
		FString Error;
		if( !GLevel->Listen( Error ) )
			appErrorf( LocalizeError(TEXT("ServerListen"),TEXT("Engine")), *Error );
	}
	unguard;

	// Client init.
	guard(ClientInit);
	if( Client )
	{
		// Match Viewports to actors.
		MatchViewportsToActors( Client, GLevel->IsServer() ? GLevel : GEntry, URL );

		// Reset viewports.
		for( INT i=0; i<Client->Viewports.Num(); i++ )
		{
			UViewport* Viewport = Client->Viewports(i);
			Viewport->Input->ResetInput();
		}

		if (Voice != NULL &&
			Voice->Initialized())
		{
			Voice->SetViewport(Client->Viewports(0));
		}
	}
	unguard;

	// Init detail.
	GLevel->DetailChange( (EDetailMode)Info->DetailMode );

	// Remember DefaultPlayer options.
#ifndef _XBOX
	if( GIsClient )
	{
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("Name" ), TEXT("User") );
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("Team" ), TEXT("User") );
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("Class"), TEXT("User") );
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("Skin" ), TEXT("User") );
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("Face" ), TEXT("User") );
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("Voice" ), TEXT("User") );
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("EnergyWeapon" ), TEXT("User") );
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("ExplosiveWeapon" ), TEXT("User") );
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("PlayerHandicap" ), TEXT("User") );
		URL.SaveURLConfig( TEXT("DefaultPlayer"), TEXT("OverrideClass" ), TEXT("User") );
	}
#endif	// XBOX

#ifdef WITH_KARMA
	// Pre-allocate pool of KarmaTriListData structs
	// Don't bother if bKNoInit is false
	ALevelInfo* lInfo = GLevel->GetLevelInfo();
	if(!lInfo->bKNoInit)
	{
		for(INT i=0; i<lInfo->MaxRagdolls; i++)
		{
			KarmaTriListData* list = (KarmaTriListData*)appMalloc(sizeof(KarmaTriListData));
			KarmaTriListDataInit(list);
			GLevel->TriListPool.AddItem(list);
		}
	}
#endif	


#ifndef FINAL_RELEASE
	FLOAT TotalTime = appSeconds() - LoadTime;
	debugf(TEXT("Loading of %s took %f seconds"),*URL.Map,TotalTime);
#endif

#if (defined(_XBOX) || WITH_PORTABLE_MOVIES) && !defined(_DEBUG) && !defined(LIN_SAVE)
	// If there was a specified cinematic, stop the loading movie
	// and play it instead
	if (bHasIntroMovie == TRUE)
	{
		// Stop the loading movie
		SetEngineReady();
		// Now play the cinematic
		PlayIntroCinematic(URL);
	}
#endif 

	// Have another go at fixing up the level after all actors have had their beginplay events called
	PostFixupLevel( GameClassName );

	// apply any patches that couldn't be successfully applied prior to game initialization
	if ( !bIsEntry )
		Patcher.ApplyPatches(TRUE);

	// Successfully started local level.
	return GLevel;
	unguard;
}

/*-----------------------------------------------------------------------------
	Game Viewport functions.
-----------------------------------------------------------------------------*/

void UGameEngine::PrecacheStaticMeshes(UViewport *Viewport, BYTE* HitData, INT* HitSize, AActor *CameraActor, FVector &CameraLocation, FRotator &CameraRotation)
{
	// Precache "dynamic" static meshes (e.g. weapon effects).
	TArray<UStaticMesh*> *meshList = &Viewport->Actor->Level->PrecacheStaticMeshes;
	for( INT i=0; i < meshList->Num(); i++ )
	{
		// Set vertex and index buffers.
		UStaticMesh*	StaticMesh = (*meshList)(i);
		if (StaticMesh != NULL)
		{
			for( INT MatIndex=0; MatIndex<StaticMesh->Materials.Num(); MatIndex++ )		
			{
				//Viewport->RI->PrecacheMaterial(staticMesh->Materials(MatIndex).Material);
				Viewport->Actor->Level->PrecacheMaterials.AddUniqueItem(StaticMesh->Materials(MatIndex).Material);
			}			
		}
	}
	meshList->Empty();
}

void UGameEngine::PrecacheMeshes(UViewport *Viewport, BYTE* HitData, INT* HitSize, AActor *CameraActor, FVector &CameraLocation, FRotator &CameraRotation)
{
	TArray<UMesh*> *meshList = &Viewport->Actor->Level->PrecacheMeshes;

	// precache materials from the meshes
	for( INT i=0; i < meshList->Num(); i++ )
	{	
		USkeletalMesh* SkelMesh = Cast<USkeletalMesh>( (*meshList)(i) );

		if( SkelMesh )
		{
			for( INT j=0; j<SkelMesh->Materials.Num(); j++ )
			{
				if( SkelMesh->Materials(j) )
				{
					//Viewport->RI->SetMaterial( mesh->Materials(j) );
					Viewport->Actor->Level->PrecacheMaterials.AddUniqueItem(SkelMesh->Materials(j));
				}
			}	
		}				
	}
	meshList->Empty();
}

void UGameEngine::PrecacheMaterials(UViewport *Viewport, BYTE* HitData, INT* HitSize, AActor *CameraActor, FVector &CameraLocation, FRotator &CameraRotation)
{
	if(Viewport->Lock(HitData,HitSize))
	{
		FPlayerSceneNode SceneNode(Viewport,&Viewport->RenderTarget,CameraActor,CameraLocation,CameraRotation,Viewport->Actor->FovAngle);
		Viewport->LodSceneNode = &SceneNode;
		Viewport->RI->SetPrecacheMode( PRECACHE_All );

		// Precache dynamic materials (e.g. player skins).
		TArray<UMaterial*>	&materialList = Viewport->Actor->Level->PrecacheMaterials;
		for (INT i = 0; i < materialList.Num(); i++)
		{
			if (materialList(i) != NULL)
			{
				//debugf(NAME_Precache,TEXT("Precache material [%s]"),materialList(i)->GetName());
				Viewport->RI->SetMaterial(materialList(i));
			}
		}
		materialList.Empty();

//		SceneNode.Render(Viewport->RI);
		Viewport->LodSceneNode = NULL;
		Viewport->Unlock();
	}
}

//
// Draw a global view.
//
void UGameEngine::Draw( UViewport* Viewport, FLOAT DeltaSeconds, UBOOL Blit, BYTE* HitData, INT* HitSize )
{
	guard(UGameEngine::Draw);

	// If not up and running yet, don't draw.
	if(!GIsRunning)
		return;

	FUNC_CLOCK(RenCyc,GStats.DWORDStats( GEngineStats.STATS_Frame_RenderCycles ));

	// Determine the camera actor, location and rotation.
	AActor*		CameraActor		= Viewport->Actor;
	FVector		CameraLocation	= CameraActor->Location;
	FRotator	CameraRotation	= CameraActor->Rotation;

	// Get camera view info - force camera update if level is paused 
	// (allows AlwaysTick matinee sequences to get proper camera actor location)
	Viewport->Actor->eventPlayerCalcView( DeltaSeconds, CameraActor, CameraLocation, CameraRotation, Viewport->Actor->GetLevel()->IsPaused() );

	if(!CameraActor)
	{
		debugf(TEXT("Warning: NULL CameraActor returned from PlayerCalcView for %s"),Viewport->Actor->GetPathName());
		CameraActor = Viewport->Actor;
	}
	
#ifdef _XBOX
	extern UBOOL IsPlayingMovie(void);
	UBOOL bNeedFlush = IsPlayingMovie();
#else
	UBOOL bNeedFlush = FALSE;
#endif
	//jg -- If a movie is playing and we're loading entry, bail from here
	if(Viewport->Actor->XLevel != GLevel || (GEntry == GLevel && bNeedFlush == TRUE))
	{
		return;
	}
	if (bNeedFlush == TRUE && ( Cast<ACamera>(Viewport->Actor) != NULL ||
								(Client != NULL && Client->SplitScreenMgr != NULL && Client->SplitScreenMgr->IsOverlayViewport(Viewport)) ) )
	{
		return;
	}

#ifndef _XBOX//scion jg -- The movie handles this
	// Render the level.
	UpdateConnectingMessage();
#endif

	BYTE SavedAction = Viewport->Actor->Level->LevelAction;
	
	if( Viewport->RenDev->PrecacheOnFlip )
	{
		Viewport->Actor->Level->LevelAction = LEVACT_Precaching;
	}

	// Present the last frame.
	if(Viewport->PendingFrame && (!(Viewport->RenDev->PrecacheOnFlip) )  )
	{
		Viewport->Present();
	}

	// check for any assets to precache
	ALevelInfo *levelInfo = Viewport->Actor->Level;
	if (levelInfo->PrecacheStaticMeshes.Num() > 0)
	{
		Viewport->Precaching = 1;
		PrecacheStaticMeshes(Viewport,HitData,HitSize,CameraActor,CameraLocation,CameraRotation);
	}
	if (levelInfo->PrecacheMeshes.Num() > 0)
	{
		Viewport->Precaching = 1;
		PrecacheMeshes(Viewport,HitData,HitSize,CameraActor,CameraLocation,CameraRotation);
	}
	if (levelInfo->PrecacheMaterials.Num() > 0)
	{
		Viewport->Precaching = 1;
		PrecacheMaterials(Viewport,HitData,HitSize,CameraActor,CameraLocation,CameraRotation);
	}
	if (Viewport->Precaching == 1)
	{
		Viewport->Precaching = 0;
		//scion jg -- Turn off precaching
		Viewport->RI->SetPrecacheMode( PRECACHE_None );
	}

#ifdef _XBOX
	//scion jg -- If we have a movie running, let it know that we can break
	// into action at any time
	SetEngineReady();
	if (bNeedFlush == 1)
	{
		// Flush render states
		GLevel->Engine->GRenDev->ResetRenderStates();
	}
#endif

	// Precache now if desired.
	if(Viewport->RenDev->PrecacheOnFlip )//&& !Viewport->Actor->Level->bNeverPrecache )
	{
		Viewport->RenDev->PrecacheOnFlip = 0;

		Viewport->Precaching = 1;

		Viewport->Precaching	= 0;

		Viewport->PendingFrame	= 0;

		Viewport->Actor->GetLevel()->FinishedPrecaching = 1;
	}
	else if(Viewport->Lock(HitData,HitSize))
	{
		if( Viewport->Actor->UseFixedVisibility )
		{
			FMatrix& WorldToCamera = Viewport->Actor->RenderWorldToCamera;
			WorldToCamera = FTranslationMatrix(-CameraLocation);

			if(!Viewport->IsOrtho())
				WorldToCamera = WorldToCamera * FInverseRotationMatrix(CameraRotation);

			if(Viewport->Actor->RendMap == REN_OrthXY)
				WorldToCamera = WorldToCamera * FMatrix(
											FPlane(Viewport->ScaleX,	0,					0,					0),
											FPlane(0,					-Viewport->ScaleY,	0,					0),
											FPlane(0,					0,					-1,					0),
											FPlane(0,					0,					-CameraLocation.Z,	1));
			else if(Viewport->Actor->RendMap == REN_OrthXZ)
				WorldToCamera = WorldToCamera * FMatrix(
											FPlane(Viewport->ScaleX,	0,					0,					0),
											FPlane(0,					0,					-1,					0),
											FPlane(0,					Viewport->ScaleY,	0,					0),
											FPlane(0,					0,					-CameraLocation.Y,	1));
			else if(Viewport->Actor->RendMap == REN_OrthYZ)
				WorldToCamera = WorldToCamera * FMatrix(
											FPlane(0,					0,					1,					0),
											FPlane(Viewport->ScaleX,	0,					0,					0),
											FPlane(0,					Viewport->ScaleY,	0,					0),
											FPlane(0,					0,					CameraLocation.X,	1));
			else
				WorldToCamera = WorldToCamera * FMatrix(
											FPlane(0,					0,					1,	0),
											FPlane(Viewport->ScaleX,	0,					0,	0),
											FPlane(0,					Viewport->ScaleY,	0,	0),
											FPlane(0,					0,					0,	1));

			CameraLocation = Viewport->Actor->FixedLocation;
			CameraRotation = Viewport->Actor->FixedRotation;

			Viewport->RI->Clear();
		}

		if( Viewport->bShowScene )
		{
			FPlayerSceneNode	SceneNode(Viewport,&Viewport->RenderTarget,CameraActor,CameraLocation,CameraRotation,Viewport->Actor->FovAngle);

			Viewport->LodSceneNode = &SceneNode;

#ifdef _XBOX //scion jg -- Clear the xbox stencil to zero
			Viewport->RI->Clear(0,FColor(0,0,0),1,1.0f,1,0);
#else
			// On the PC clear it to zero if we aren't in depth complexity mode
			if (Viewport->IsDepthComplexity())
			{
				Viewport->RI->Clear(0,FColor(0,0,0),1,1.0f,1,~DEPTH_COMPLEXITY_MASK(Viewport));
			}
			else
			{
				Viewport->RI->Clear(0,FColor(0,0,0),1,1.0f,1,0);
			}
#endif

			// if this viewport has both bShowScene and bShowMenu set, it indicates that
			// this is the only viewport, and the audio subsystem should be ticked from here
			if (Viewport->bShowMenu)
			{
				// Update level audio.
				if (Audio)
				{
					Audio->Update();
				}
				//epic rwd - update voice status
				if (Voice && Voice->Initialized())
				{
					Voice->Update();
				}
			}

			SceneNode.Render(Viewport->RI);
		}		
		else if( Viewport->bShowMenu ) // !Viewport->bShowScene
		{
			// scion sz -
			// for the overlay viewport (splitscreen) we only want to
			// render the menus 
			FInteractionRenderSceneNode MenuSceneNode(
				Viewport,
				&Viewport->RenderTarget,
				CameraActor,
				CameraLocation,
				CameraRotation,
				Viewport->Actor->FovAngle 
				);

			Viewport->LodSceneNode = &MenuSceneNode;

			//epic rwd - update voice status
			// in splitscreen, only the overlay viewport has bShowMenu set,
			// so we'll update the audio and voice from here so that they're only updated once.
			if (Audio)
			{
				Audio->Update();
			}
			if (Voice && Voice->Initialized())
			{
				Voice->Update();
			}
			MenuSceneNode.Render( Viewport->RI );
		}
		
		if ( Viewport->Canvas->pCanvasUtil )
			Viewport->Canvas->pCanvasUtil->Flush();
		
		Viewport->Precaching = 1;

		// Precache "dynamic" static meshes (e.g. weapon effects).
		for( INT i=0; i<Viewport->Actor->Level->PrecacheStaticMeshes.Num(); i++ )
		{
			// Set vertex and index buffers.
			UStaticMesh*	StaticMesh = Viewport->Actor->Level->PrecacheStaticMeshes(i);
			if( !StaticMesh )
				continue;

			FVertexStream*	VertexStreams[9] = { &StaticMesh->VertexStream };
			INT				NumVertexStreams = 1;

			if( !StaticMesh->VertexStream.Vertices.Num() )
				continue;

			if( StaticMesh->UseVertexColor )
			{
				if( StaticMesh->ColorStream.Colors.Num() )
					VertexStreams[NumVertexStreams++] = &StaticMesh->ColorStream;
			}
			else
			{
				if( StaticMesh->AlphaStream.Colors.Num() )
					VertexStreams[NumVertexStreams++] = &StaticMesh->AlphaStream;
			}

			for(INT UVIndex = 0;UVIndex < StaticMesh->UVStreams.Num();UVIndex++)
				VertexStreams[NumVertexStreams++] = &StaticMesh->UVStreams(UVIndex);

			Viewport->RI->SetVertexStreams(VS_FixedFunction,VertexStreams,NumVertexStreams);
			Viewport->RI->SetIndexBuffer(&StaticMesh->IndexBuffer,0);

			for( INT MatIndex=0; MatIndex<StaticMesh->Materials.Num(); MatIndex++ )
				Viewport->Actor->Level->PrecacheMaterials.AddItem( StaticMesh->Materials(MatIndex).Material );
		}
		Viewport->Actor->Level->PrecacheStaticMeshes.Empty();

		// Precache dynamic materials (e.g. player skins).
		for( INT i=0; i<Viewport->Actor->Level->PrecacheMaterials.Num(); i++ )
			Viewport->RI->SetMaterial( Viewport->Actor->Level->PrecacheMaterials(i) );
		Viewport->Actor->Level->PrecacheMaterials.Empty();		

		Viewport->Precaching = 0;

		Viewport->LodSceneNode = NULL;
		Viewport->Unlock();
		Viewport->PendingFrame = Blit ? 1 : 0;		
	}

	Viewport->Actor->Level->LevelAction = SavedAction;

	unguard;
}

static void ExportTravel( FOutputDevice& Out, AActor* Actor )
{
	guard(ExportTravel);
	debugf( TEXT("Exporting travelling actor of class %s"), Actor->GetClass()->GetPathName() );//!!xyzzy
	check(Actor);
	if( !Actor->bTravel )
		return;
	Out.Logf( TEXT("Class=%s Name=%s\r\n{\r\n"), Actor->GetClass()->GetPathName(), Actor->GetName() );
	for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(Actor->GetClass()); It; ++It )
	{
		for( INT Index=0; Index<It->ArrayDim; Index++ )
		{
			TCHAR Value[1024];
			if
			(	(It->PropertyFlags & CPF_Travel)
			&&	It->ExportText( Index, Value, (BYTE*)Actor, Actor->GetClass()->GetDefaultsSafe(0), 0 ) )
			{
				Out.Log( It->GetName() );
				if( It->ArrayDim!=1 )
					Out.Logf( TEXT("[%i]"), Index );
				Out.Log( TEXT("=") );
				UObjectProperty* Ref = Cast<UObjectProperty>( *It );
				if( Ref && Ref->PropertyClass->IsChildOf(AActor::StaticClass()) )
				{
					UObject* Obj = *(UObject**)( (BYTE*)Actor + It->Offset + Index*It->ElementSize );
					Out.Logf( TEXT("%s\r\n"), Obj ? Obj->GetName() : TEXT("None") );
				}
				Out.Logf( TEXT("%s\r\n"), Value );
			}
		}
	}
	Out.Logf( TEXT("}\r\n") );
	unguard;
}

//
// Jumping viewport.
//
void UGameEngine::SetClientTravel( UPlayer* Player, const TCHAR* NextURL, UBOOL bItems, ETravelType TravelType )
{
	guard(UGameEngine::SetClientTravel);
	check(Player);

	// make sure to turn off any force feedback
	if (Player->Actor && Player->Actor->WaveformManager)
	{
		delete Player->Actor->WaveformManager;
		Player->Actor->WaveformManager = NULL;
	}

	UViewport* Viewport    = CastChecked<UViewport>( Player );

	debugf(NAME_DevRon, TEXT("%s::SetClientTravel: %s"), Viewport->GetName(), NextURL);
	//epic moelfke: use accessor to set travel info
	Viewport->SetClientTravel(NextURL, bItems, TravelType);

	unguard;
}

/*-----------------------------------------------------------------------------
	Tick.
-----------------------------------------------------------------------------*/

//
// Get tick rate limitor.
//
FLOAT UGameEngine::GetMaxTickRate()
{
	guard(UGameEngine::GetMaxTickRate);
	static UBOOL LanPlay = ParseParam(appCmdLine(),TEXT("lanplay"));

	if( GLevel && GLevel->DemoRecDriver && !GLevel->DemoRecDriver->ServerConnection && GLevel->NetDriver && !GIsClient )
	{
		// We're a dedicated server recording a demo, use the high framerate demo tick.
		return Clamp( LanPlay ? GLevel->DemoRecDriver->LanServerMaxTickRate : GLevel->DemoRecDriver->NetServerMaxTickRate, 20, 60 );
	}
	else
	if( GLevel && GLevel->NetDriver && !GIsClient )
	{
		// We're a dedicated server, use the LAN or Net tick rate.
		return Clamp( LanPlay ? GLevel->NetDriver->LanServerMaxTickRate : GLevel->NetDriver->NetServerMaxTickRate, 10, 120 );
	}
	else
	if( GLevel && GLevel->NetDriver && GLevel->NetDriver->ServerConnection && GLevel->GetLevelInfo()->bCapFramerate )
	{
		// We're a network client with bCapFramerate - so cap it.
		return ::Min(90.f,GLevel->NetDriver->ServerConnection->CurrentNetSpeed/GLevel->GetLevelInfo()->MoveRepSize);
	}
	else
		return 0;
	unguard;
}

//
// Update everything.
//
void UGameEngine::Tick( FLOAT DeltaSeconds )
{
	guard(UGameEngine::Tick);
	FUNC_CLOCK(TiCyc,TickCycles);
	UC2NetTestTick( this );	// uc2 port: the -nettest=<file> command file (UC2NetTest.cpp); nothing without it
	UC2RememberProfile( Client );	// uc2 port (D-3): User.ini [Profiles] LastProfile follows the first player's profile

    if( DeltaSeconds < 0.0f )
        appErrorf(TEXT("Negative delta time!"));

	// If all viewports closed, time to exit.
	if( Client && Client->Viewports.Num()==0 )
	{
		debugf( TEXT("All Windows Closed") );
		appRequestExit( 0 );
		return;
	}

	// If game is paused, release the cursor.
	static UBOOL WasPaused=1;
	if
	(	Client
	&&	Client->Viewports.Num()==1
	&&	GLevel
	&&	!Client->Viewports(0)->IsFullscreen() )
	{
		UBOOL IsPaused
		=	GLevel->IsPaused()
		||	Client->Viewports(0)->bShowWindowsMouse;
		if( IsPaused && !WasPaused )
			Client->Viewports(0)->SetMouseCapture( 0, 0, 0 );
		else if( WasPaused && !IsPaused && Client->CaptureMouse )
			Client->Viewports(0)->SetMouseCapture( 1, 1, 1 );
		WasPaused = IsPaused;
	}
	else WasPaused=0;

	// Update subsystems.
	UObject::StaticTick();				
	GCache.Tick();

	// Update the level.
	guard(TickLevel);
	GameCycles=0;
	CLOCK(GameCyc,GameCycles);
	if( GLevel )
	{
		// Decide whether to drop high detail because of frame rate
		if( Client && !GIsBenchmarking )
		{
			ALevelInfo* pLevelInfo = GLevel->GetLevelInfo();
			static FLOAT MinFrameTime = 1.f/Clamp(Client->MinDesiredFrameRate,1.f,100.f);
			static FLOAT MinAgressiveFrameTime = 1.f/Clamp(Client->MinDesiredFrameRate - 5.f,1.f,100.f);			

			pLevelInfo->bDropDetail = (DeltaSeconds > MinFrameTime);
			pLevelInfo->bAggressiveLOD = (DeltaSeconds > MinAgressiveFrameTime);

			// scion sz -
			// attenuate particles spawning based on framerate
			static FLOAT	PARTICLE_ATTEN_STEP =0.03f;
			// step towards the desired attenuation 
			FLOAT Sign = (DeltaSeconds > MinAgressiveFrameTime) ? -1.f : 1.f;
			pLevelInfo->SpawnParticleAtten =  Clamp<FLOAT>( 
				pLevelInfo->SpawnParticleAtten + Sign * PARTICLE_ATTEN_STEP,
				pLevelInfo->ParticleAttenRange.Min,
				pLevelInfo->ParticleAttenRange.Max 
				);
			// attenuate active particles (already spawned) to match level's ParticleAtten
			pLevelInfo->DynamicParticleAtten = pLevelInfo->SpawnParticleAtten;

		}
		// tick the level
		GLevel->Tick( LEVELTICK_All, DeltaSeconds );
	}
	if( GEntry && GEntry!=GLevel )
	{
		//scion jg -- Changed to only update time so that matinee doesn't crash
		GEntry->Tick( LEVELTICK_TimeOnly, DeltaSeconds );
	}
	if( Client && Client->Viewports.Num() )
	{ 
		APlayerController* ViewActor = Client->Viewports(0)->Actor;
		if( ViewActor && 
			ViewActor->GetLevel()!=GLevel )
		{
			ViewActor->GetLevel()->Tick( LEVELTICK_All, DeltaSeconds );
		}
	}
	//scion jg -- Unprog bug fix
	if ( Client && Client->InteractionMaster )
		Client->InteractionMaster->MasterProcessTick(DeltaSeconds);
	UNCLOCK();
	unguard;

	ALevelInfo* LevelInfo = GLevel ? GLevel->GetLevelInfo() : NULL;

	// Handle server travelling.
	guard(ServerTravel);

	if( LevelInfo && LevelInfo->NextURL!=TEXT("") )
	{
		if( (LevelInfo->NextSwitchCountdown-=DeltaSeconds) <= 0.f )
		{
			// Travel to new level, and exit.
			TMap<FString,FString> TravelInfo;
			if( LevelInfo->NextURL==TEXT("?RESTART") )
			{
				TravelInfo = GLevel->TravelInfo;
			}
			else if( LevelInfo->bNextItems )
			{
				TravelInfo = GLevel->TravelInfo;
				for( INT i=0; i<GLevel->Actors.Num(); i++ )
				{
					APlayerController* P = Cast<APlayerController>( GLevel->Actors(i) );
					if( P && P->Player && P->Pawn )
					{
						// Export items and self.
						FStringOutputDevice PlayerTravelInfo;
						ExportTravel( PlayerTravelInfo, P->Pawn );
						for( AInventory* Inv=P->Pawn->Inventory; Inv; Inv=Inv->Inventory )
							ExportTravel( PlayerTravelInfo, Inv );
						TravelInfo.Set( *P->PlayerReplicationInfo->PlayerName, *PlayerTravelInfo );

						// Prevent local ClientTravel from taking place, since it will happen automatically.
						if( Cast<UViewport>( P->Player ) )
							Cast<UViewport>( P->Player )->TravelURL = TEXT("");
					}
				}
			}

			// Clear the TravelURL for any local players
			if ( Client )
			{
				for ( INT i = 0; i < Client->Viewports.Num(); i++ )
				{
					UViewport* Viewport = Client->Viewports(i);
					if ( Viewport )
					{
						debugf(NAME_DevRon, TEXT("Clearing TravelURL for Viewport '%s'"), Viewport->GetName());
						Viewport->TravelURL = TEXT("");
					}
				}
			}

			//scion bcl -- added to clear NextURL before browsing
			FString NextURL = LevelInfo->NextURL;
			LevelInfo->NextURL = TEXT("");
			//epic moelfke: we're travelling to a local map, so make sure there's no host in the URL
			GLevel->URL.Host = TEXT("");

			debugf( TEXT("Server switch level: %s"), *NextURL );
			FString Error;
#ifdef _XBOX
			Browse( FURL(&GLevel->URL,*NextURL,TRAVEL_Partial), &TravelInfo, Error );
#else
			Browse( FURL(&GLevel->URL,*NextURL,TRAVEL_Relative), &TravelInfo, Error );
#endif

			return;
		}
	}
	unguard;

	// Handle client travelling.
	guard(ClientTravel);
	
	UViewport* Viewport = Client?Client->Viewports.Num()?Client->Viewports( 0 ):NULL:NULL;
	if( (!LevelInfo||LevelInfo->NextURL==TEXT("")) && Viewport && Viewport->TravelURL!=TEXT(""))
	{
		// Travel to new level, and exit.
		TMap<FString,FString> TravelInfo;

		// Export items.
		if( appStricmp(*Viewport->TravelURL,TEXT("?RESTART"))==0 )
		{
			TravelInfo = GLevel->TravelInfo;
		}
		else if( Viewport->bTravelItems )
		{
			debugf( NAME_DevLoad, TEXT("Export travel for: %s"), *Viewport->Actor->PlayerReplicationInfo->PlayerName );
			FStringOutputDevice PlayerTravelInfo;
			ExportTravel( PlayerTravelInfo, Viewport->Actor->Pawn );
			for( AInventory* Inv=Viewport->Actor->Pawn->Inventory; Inv; Inv=Inv->Inventory )
				ExportTravel( PlayerTravelInfo, Inv );
			TravelInfo.Set( *Viewport->Actor->PlayerReplicationInfo->PlayerName, *PlayerTravelInfo );
		}
		FString Error;
		FURL NextURL(&LastURL, *Viewport->TravelURL, Viewport->TravelType);

		// Not sure why this needed to be here, since Browse() should always call InitializeURL on the 
		// primary viewport, where appropriate...if it's determined that this also needs to be here as well,
		// please comment why
		//Viewport->InitializeURL(NextURL);
		debugf(NAME_DevRon, TEXT("ClientTravel %s: %s"), Viewport->GetName(), *NextURL.String());

		Browse( NextURL, &TravelInfo, Error );
		Viewport->TravelURL=TEXT("");

		return;
	}
	unguard;

	// Update the pending level.
	guard(TickPending);
	if( GPendingLevel )
	{
		GPendingLevel->Tick( DeltaSeconds );
		if( GPendingLevel->Error!=TEXT("") )
		{
			// Pending connect failed.
			guard(PendingFailed);

			debugf( NAME_Log, LocalizeError(TEXT("Pending"),TEXT("Engine")), *GPendingLevel->URL.String(), *GPendingLevel->Error );
			if ( Client && Client->Viewports.Num() && Client->Viewports(0) )
			{
				UViewport* Viewport = Client->Viewports(0);
				if ( Viewport->Actor )
				{
					Viewport->Actor->eventClientOpenMenu(TEXT("UCMenu.ConnectionFailureMenu"), 0, GPendingLevel->Error, GPendingLevel->URL.String());
				}
				//epic moelfke: if there's no local PlayerController, we need to reload the default map to get a new one
				else
				{
					FString Error;
					Browse(FURL(&GPendingLevel->URL, TEXT("?failed"), TRAVEL_Absolute), NULL, Error);
				}
			}
			CancelPending();
			unguard;
		}
		//epic moelfke: don't send join until any DLOs we need to do are finished
		else if ( GPendingLevel->Success && !GPendingLevel->FilesNeeded && !GPendingLevel->SentJoin &&
					( !GLevel || !GLevel->GetLevelInfo() ||
						GLevel->GetLevelInfo()->LoadingXPRs.Num() + GLevel->GetLevelInfo()->DynamicLoadList.Num() == 0 ) )
		{
			// Attempt to load the map.
#ifdef DEBUG_SERVERTRAVEL
			debugf( TEXT("UGameEngine::Tick() - GPendingLevel->Success:%s  FilesNeeded:%s   SentJoin:%s"),
				GPendingLevel->Success ? TEXT("True") : TEXT("False"),
				GPendingLevel->FilesNeeded ? TEXT("True") : TEXT("False"),
				GPendingLevel->SentJoin ? TEXT("True") : TEXT("False"));
#endif
			guard(AttemptLoadPending);
			FString Error;
			//epic moelfke: check if level has already been loaded
			if (!GPendingLevel->IsA(UNetPendingLevel::StaticClass()) || !((UNetPendingLevel*)GPendingLevel)->bPreloadedLevel)
			{
				LoadMap( GPendingLevel->URL, GPendingLevel, NULL, Error );
			}
			if( Error!=TEXT("") )
			{
				SetProgress( LocalizeError(TEXT("ConnectionFailed"),TEXT("Engine")), *Error, 4.f );
			}
			else if( !GPendingLevel->LonePlayer )
			{
				// Show connecting message, cause precaching to occur.
				GLevel->GetLevelInfo()->LevelAction = LEVACT_Connecting;
				GEntry->GetLevelInfo()->LevelAction = LEVACT_Connecting;
				if( Client )
				{
					Client->Tick( DeltaSeconds );
				}

				// Send join.
				GPendingLevel->SendJoin();
				GPendingLevel->NetDriver = NULL;
				GPendingLevel->DemoRecDriver = NULL;
			}
			unguard;

			// Kill the pending level.
			guard(KillPending);
			delete GPendingLevel;
			GPendingLevel = NULL;
			unguard;
		}
	}
	unguard;

	// Render everything.
	guard(ClientTick);
	INT LocalClientCycles=0;
	if( Client )
	{
		ClientCycles = 0;
		FUNC_CLOCK(CliCyc,LocalClientCycles);
		Client->Tick( DeltaSeconds );
	}
	unguard;

	GTicks++;
#if PROFILE
	if( GScriptCallGraph )
		GScriptCallGraph->Tick();
#endif
	unguard;
}

/*-----------------------------------------------------------------------------
	Saving the game.
-----------------------------------------------------------------------------*/

//
// Save the current game state to a file.
//
void UGameEngine::SaveGame( INT Position )
{
	guard(UGameEngine::SaveGame);

	TCHAR Filename[256];
	GFileManager->MakeDirectory( *GSys->SavePath, 0 );
	appSprintf( Filename, TEXT("%s") PATH_SEPARATOR TEXT("Save%i.usa"), *GSys->SavePath, Position );
	GLevel->GetLevelInfo()->LevelAction=LEVACT_Saving;
	PaintProgress();
	GWarn->BeginSlowTask( LocalizeProgress(TEXT("Saving"),TEXT("Engine")), 1);
	GLevel->CleanupDestroyed( 1 );
	if( SavePackage( GLevel->GetOuter(), GLevel, 0, Filename, GLog ) )
	{
		// Copy the hub stack.
		INT i;
		for( i=0; i<GLevel->GetLevelInfo()->HubStackLevel; i++ )
		{
			TCHAR Src[256], Dest[256];
			appSprintf( Src, TEXT("%s") PATH_SEPARATOR TEXT("Game%i.usa"), *GSys->SavePath, i );
			appSprintf( Dest, TEXT("%s") PATH_SEPARATOR TEXT("Save%i%i.usa"), *GSys->SavePath, Position, i );
			GFileManager->Copy( Src, Dest );
		}
		while( 1 )
		{
			appSprintf( Filename, TEXT("%s") PATH_SEPARATOR TEXT("Save%i%i.usa"), *GSys->SavePath, Position, i++ );
			if( GFileManager->FileSize(Filename)<=0 )
				break;
			GFileManager->Delete( Filename );
		}
	}
	for( INT i=0; i<GLevel->Actors.Num(); i++ )
		if( Cast<AMover>(GLevel->Actors(i)) )
			Cast<AMover>(GLevel->Actors(i))->SavedPos = FVector(-1,-1,-1);
	GWarn->EndSlowTask();
	GLevel->GetLevelInfo()->LevelAction=LEVACT_None;
	GCache.Flush();

	unguard;
}

/*-----------------------------------------------------------------------------
	Mouse feedback.
-----------------------------------------------------------------------------*/

void UGameEngine::MouseWheel( UViewport* Viewport, DWORD Buttons, INT Delta )
{
}

//
// Mouse delta while dragging.
//
void UGameEngine::MouseDelta( UViewport* Viewport, DWORD ClickFlags, FLOAT DX, FLOAT DY )
{
	guard(UGameEngine::MouseDelta);
	if
	(	(ClickFlags & MOUSE_FirstHit)
	&&	Client
	&&	Client->Viewports.Num()==1
	&&	GLevel
	&&	!Client->Viewports(0)->IsFullscreen()
	&&	!GLevel->IsPaused()
	&&  !Viewport->bShowWindowsMouse )
	{
		Viewport->SetMouseCapture( 1, 1, 1 );
	}
	else if( (ClickFlags & MOUSE_LastRelease) && !Client->CaptureMouse )
	{
		Viewport->SetMouseCapture( 0, 0, 0 );
	}
	unguard;
}

//
// Absolute mouse position.
//
void UGameEngine::MousePosition( UViewport* Viewport, DWORD ClickFlags, FLOAT X, FLOAT Y )
{
	guard(UGameEngine::MousePosition);

	if( Viewport )
	{
		Viewport->WindowsMouseX = X;
		Viewport->WindowsMouseY = Y;
	}

	unguard;
}

//
// Mouse clicking.
//
void UGameEngine::Click( UViewport* Viewport, DWORD ClickFlags, FLOAT X, FLOAT Y )
{
	guard(UGameEngine::Click);
	unguard;
}

void UGameEngine::UnClick( UViewport* Viewport, DWORD ClickFlags, INT MouseX, INT MouseY )
{
	guard(UGameEngine::UnClick);
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

