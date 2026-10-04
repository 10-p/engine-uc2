/*=============================================================================
	UnEngine.cpp: Unreal engine main.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "EnginePrivate.h"
#include "UnLinker.h"
/*-----------------------------------------------------------------------------
	Object class implementation.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(UEngine);
IMPLEMENT_CLASS(URenderDevice);

/*-----------------------------------------------------------------------------
	Engine init and exit.
-----------------------------------------------------------------------------*/

//
// Construct the engine.
//
UEngine::UEngine()
{
	guard(UEngine::UEngine);
	unguard;
}


//scion rwd - null the static engine ptr
UEngine* UEngine::GEngine			= NULL;

//
// Init class.
//
void UEngine::StaticConstructor()
{
	guard(UEngine::StaticConstructor);

	new(GetClass(),TEXT("CacheSizeMegs"),      RF_Public)UFloatProperty (CPP_PROPERTY(CacheSizeMegs      ), TEXT("Settings"), CPF_Config );	

	unguard;
}

// Register things.
#define NAMES_ONLY
#define AUTOGENERATE_NAME(name) ENGINE_API FName ENGINE_##name;
#define AUTOGENERATE_FUNCTION(cls,idx,name) IMPLEMENT_FUNCTION(cls,idx,name)
#include "EngineClasses.h"
#undef AUTOGENERATE_FUNCTION
#undef AUTOGENERATE_NAME
#undef NAMES_ONLY

// Register natives.
#define NATIVES_ONLY
#define NAMES_ONLY
#define AUTOGENERATE_NAME(name)
#define AUTOGENERATE_FUNCTION(cls,idx,name)
#include "EngineClasses.h"
#undef AUTOGENERATE_FUNCTION
#undef AUTOGENERATE_NAME
#undef NATIVES_ONLY
#undef NAMES_ONLY

#include "FContentManager.h"

//
// Init audio.
//
void UEngine::InitAudio()
{
	guard(UEngine::InitAudio);
	if (Audio == NULL)
	{
		if
		(	UseSound
		&&	GIsClient
		&&	!ParseParam(appCmdLine(),TEXT("NOSOUND")) )
		{
			UClass* AudioClass = StaticLoadClass( UAudioSubsystem::StaticClass(), NULL, TEXT("ini:Engine.Engine.AudioDevice"), NULL, LOAD_NoFail, NULL );
			Audio = ConstructObject<UAudioSubsystem>( AudioClass );
			if( !Audio->Init() )
			{
				debugf( NAME_Error, TEXT("Audio initialization failed.") );
				if (Audio != NULL)
				{
					delete Audio;
					Audio = NULL;
				}
			}
		}
		//scion jg -- Null audio device
		//scion rwd - changed to default the null audio device
		if (Audio == NULL)
		{
			UClass* AudioClass = StaticLoadClass( UAudioSubsystem::StaticClass(), NULL, TEXT("Engine.NullAudioDevice"), NULL, LOAD_NoFail, NULL );
			Audio = ConstructObject<UAudioSubsystem>( AudioClass );
			if( Audio == NULL || !Audio->Init() )
			{
				debugf( NAME_Error, TEXT("Audio initialization failed.") );
				if (Audio != NULL)
				{
					delete Audio;
					Audio = NULL;
				}
			}
		}
	}
#ifdef _XBOX
	if (Voice == NULL)
	{
		//epic rwd - init voice if we have valid audio
		UClass *voiceClass = StaticLoadClass(UVoiceSubsystem::StaticClass(),NULL,TEXT("XboxAudio.XHVVoiceSubsystem"),NULL,LOAD_NoFail,NULL);
		Voice = ConstructObject<UVoiceSubsystem>(voiceClass);
	}
	if (Voice != NULL &&
		Audio != NULL)
	{
		Voice->Init(Audio);
	}
#endif
	unguard;
}

//
// Initialize the engine.
//
void UEngine::Init()
{
	guard(UEngine::Init);

	// Add the intrinsic names.
	#define NAMES_ONLY
	#define AUTOGENERATE_NAME(name) ENGINE_##name = FName(TEXT(#name),FNAME_Intrinsic);
	#define AUTOGENERATE_FUNCTION(cls,idx,name)
	#include "EngineClasses.h"
	#include "UnPlayerStats.h"
	#undef AUTOGENERATE_FUNCTION
	#undef AUTOGENERATE_NAME
	#undef NAMES_ONLY

	// Subsystems.
	FURL::StaticInit();
	GEngineMem.Init( 65536 );
	GCache.Init( 1024 * 1024 * CacheSizeMegs, 4096 );
	debugf( NAME_Init, TEXT("GCache CacheSizeMegs=%2.2f"), CacheSizeMegs );
#ifndef FINAL_RELEASE
	GEngineStats.Init();
#endif

	// Initialize random number generator.
	if( GIsBenchmarking )
		appRandInit( 0 );
	else
		appRandInit( appCycles() );

#ifndef FINAL_RELEASE
	if(!GStatGraph)
		GStatGraph = NEW FStatGraph();
#endif

	if(!GTempLineBatcher)
		GTempLineBatcher = NEW FTempLineBatcher();

	// Objects.
	Cylinder = new UPrimitive;

	// Add to root.
	AddToRoot();

	// Create GGlobalTempObjects
	GGlobalTempObjects = new UGlobalTempObjects;

	// Ensure all native classes are loaded.
	for( TObjectIterator<UClass> It ; It ; ++It )
	{
		UClass* pClass = *It;
		if( !pClass->GetLinker() )
		{
			LoadObject<UClass>( pClass->GetOuter(), pClass->GetName(), NULL, LOAD_Quiet|LOAD_NoWarn, NULL );
		}
	}

	debugf( NAME_Init, TEXT("Unreal engine initialized") );
	unguard;
}

//
// Exit the engine.
//
void UEngine::Destroy()
{
	guard(UEngine::Destroy);

	// Remove from root.
	RemoveFromRoot();

	// Shut down all subsystems.
	Audio	 = NULL;
	Client	 = NULL;
	FURL::StaticExit();
	GEngineMem.Exit();
	GCache.Exit( 1 );
#ifndef FINAL_RELEASE
	if(GStatGraph)
	{
		delete GStatGraph;
		GStatGraph = NULL;
	}
#endif
	if(GTempLineBatcher)
	{
		delete GTempLineBatcher;
		GTempLineBatcher = NULL;
	}

	Super::Destroy();
	unguard;
}

//
// Flush all caches.
//
void UEngine::Flush( UBOOL AllowPrecache )
{
	guard(UEngine::Flush);

	GCache.Flush();
	if( Client )
		Client->Flush( AllowPrecache );

	unguard;
}

//
// Update Gamma/Brightness/Contrast settings.
//
void UEngine::UpdateGamma()
{
	guard(UEngine::UpdateGamma);

	if( Client )
		Client->UpdateGamma();

	unguard;
}

//
// Restore Gamma/Brightness/Contrast settings.
//
void UEngine::RestoreGamma()
{
	guard(UEngine::RestoreGamma);

	if( Client )
		Client->RestoreGamma();

	unguard;
}

//
// Tick rate.
//
FLOAT UEngine::GetMaxTickRate()
{
	guard(UEngine::GetMaxTickRate);
	return ( GIsEditor ? 30 : 0);
	unguard;
}

//
// Progress indicator.
//
void UEngine::SetProgress( const TCHAR* Str1, const TCHAR* Str2, FLOAT Seconds )
{
	guard(UEngine::SetProgress);
	unguard;
}

//
// Serialize.
//
void UEngine::Serialize( FArchive& Ar )
{
	guard(UGameEngine::Serialize);

	Super::Serialize( Ar );
	Ar << Cylinder << Client << Audio << GRenDev;

	unguardobj;
}

/*-----------------------------------------------------------------------------
	Input.
-----------------------------------------------------------------------------*/

//
// This always going to be the last exec handler in the chain. It
// handles passing the command to all other global handlers.
//
UBOOL UEngine::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UEngine::Exec);

	// See if any other subsystems claim the command.
	if( GSys    && GSys->Exec		(Cmd,Ar) ) return 1;
//#ifdef _XBOX
//	//!!vogel: use xbdbsmon to read output
//	if( UObject::StaticExec			(Cmd,*GLog) ) return 1;
//#else
	if( UObject::StaticExec			(Cmd,Ar) ) return 1;
//#endif
	if( GCache.Exec					(Cmd,Ar) ) return 1;
	if( GExec   && GExec->Exec      (Cmd,Ar) ) return 1;
	if( Client  && Client->Exec		(Cmd,Ar) ) return 1;
	if( Audio   && Audio->Exec		(Cmd,Ar) ) return 1;
	if (Voice != NULL && Voice->Exec(Cmd,Ar))
	{
		return 1;
	}
	
	FContentManager* ContentManager = FContentManager::GetInstance();
	if (ContentManager != NULL && ContentManager->Exec(Cmd, Ar))
	{
		return 1;
	}

#ifndef FINAL_RELEASE
	if( GStatGraph && GStatGraph->Exec(Cmd,Ar) ) return 1;
#endif

	// Handle engine command line.
	if( ParseCommand(&Cmd,TEXT("FLUSH")) )
	{
		Flush(1);
		Ar.Log( TEXT("Flushed engine caches") );
		return 1;
	}

#ifdef WITH_LIPSINC

	else if( ParseCommand(&Cmd,TEXT("LIPSINC")) )
	{
		// FIXME: HACK !!
		
		const TCHAR *Str = Cmd;
		
		TCHAR LIPSincAnimName[256];
		if(	ParseToken( Str, LIPSincAnimName, ARRAY_COUNT(LIPSincAnimName), 1 ) )
		{
			Ar.Logf( TEXT("Playing LIPSinc Animation: %s"), LIPSincAnimName );
			
			if( 1 )
			{
				AActor* Found   = NULL;
			
				APawn*  Player  = NULL;
				FLOAT   MinDist = 999999.0;
				
				for( TObjectIterator<APawn> It; It; ++It )
				{
					if ( It->IsHumanControlled() )
					{
						Player = *It;
					}
				}

				if( Player )
				{
					Ar.Logf(TEXT("Player found at location [%f, %f, %f]"), Player->Location.X, Player->Location.Y,
						Player->Location.Z);
				}
				else
				{
					Ar.Logf(TEXT("No Player?!"));
				}

				for( TObjectIterator<AActor> It; It; ++It )
				{
					FLOAT Dist = Player ? FDist(It->Location,Player->Location) : 0.0;
					if
						(	(!Player || It->GetLevel()==Player->GetLevel())
						&&	(!It->bDeleteMe)
						&&  (It->Mesh)
						&&	(It->Mesh->IsA(USkeletalMesh::StaticClass()) )
						&&  (Dist!=0.0)
						&&	(Dist<MinDist) )
					{
						USkeletalMesh* SkelMesh = (USkeletalMesh*)It->Mesh;
						if( SkelMesh->LIPSincController )
						{
							MinDist = Dist;
							Found   = *It;
						}
					}
				}
				
				if( Found )
				{
					Ar.Logf( TEXT("Found a skeletal mesh %s"), Found->GetName());
					
					//USkeletalMesh* SkelMesh = (USkeletalMesh*)Found->Mesh;
					
					//if( SkelMesh->LIPSincController )
					//{
						Ar.Logf( TEXT("Starting..."));
						((USkeletalMeshInstance*)Found->Mesh->MeshGetInstance(Found))->PlayLIPSincAnim(LIPSincAnimName, 1.0, 200.0, 1.0);
					//}
				}
			}
			else Ar.Logf( TEXT("No Skeletal Meshes Found.") );
		}
		return 1;
	}

#endif

	else if( ParseCommand(&Cmd,TEXT("STAT")) )
	{
		if( ParseCommand(&Cmd,TEXT("ANIM")) )
		{
			bShowAnimStats = !bShowAnimStats;
			return 1;
		}
#ifndef FINAL_RELEASE
		//jg -- Added for Steve to track listen server performance
		else if( ParseCommand(&Cmd,TEXT("STEVE")) )
		{
			GStats.bUseSteveStat = 1;
			return 1;
		}
		// scion sz - toggle temp stats
		else if( ParseCommand(&Cmd,TEXT("TEMP")) )
		{
			GStats.bUseTempStat = !GStats.bUseTempStat;
			return 1;
		}
#endif
		else if( ParseCommand(&Cmd,TEXT("LIPSINC")) )
		{
			// ifdef WITH_LIPSINC
			bShowLIPSincStats = !bShowLIPSincStats;
			return 1;
		}
		else if( ParseCommand(&Cmd,TEXT("DEFAULT")) || ParseCommand(&Cmd,TEXT("RESET")))
		{
			bShowAnimStats		= 0;
			
			// ifdef WITH_LIPSINC
			bShowLIPSincStats   = 0;

			bShowRenderStats	= 0;
			bShowHardwareStats	= 0;
			bShowMatineeStats	= 0;
			bShowGameStats		= 0;
			bShowAudioStats		= 0;
			bShowNetStats		= 0;
			bShowHistograph		= 0;
			return 1;
		}
		else if(ParseCommand(&Cmd,TEXT("FPS")))
		{
			bShowFrameRate = !bShowFrameRate;
			return 1;
		}
		else if(ParseCommand(&Cmd,TEXT("RENDER")))
		{
			bShowRenderStats = !bShowRenderStats;
			return 1;
		}
		else if(ParseCommand(&Cmd,TEXT("HARDWARE")))
		{
			bShowHardwareStats = !bShowHardwareStats;
			return 1;
		}
		else if(ParseCommand(&Cmd,TEXT("GAME")))
		{
			bShowGameStats = !bShowGameStats;
			return 1;
		}
		else if (ParseCommand(&Cmd,TEXT("CANVAS")))
		{
			bShowCanvasStats = !bShowCanvasStats;
			return 1;
		}
		else if(ParseCommand(&Cmd,TEXT("HISTOGRAPH")))
		{
			bShowHistograph = !bShowHistograph;
			return 1;
		}
		else if(ParseCommand(&Cmd,TEXT("XBOXMEM")))
		{
			bShowXboxMemStats = !bShowXboxMemStats;
			return 1;
		}
		else if(ParseCommand(&Cmd,TEXT("MATINEE")))
		{
			bShowMatineeStats = !bShowMatineeStats;
			return 1;
		}
		else if(ParseCommand(&Cmd,TEXT("AUDIO")))
		{
			bShowAudioStats = !bShowAudioStats;
			return 1;
		}
		else if(ParseCommand(&Cmd,TEXT("NET")))
		{
			bShowNetStats		= !bShowNetStats;
			return 1;
		}
		else if(ParseCommand(&Cmd,TEXT("SUMMARY")))
		{
			bShowStatsSummary		= !bShowStatsSummary;
			return 1;
		}
		else if(ParseCommand(&Cmd,TEXT("ALL")))
		{
			//bShowAnimStats	= 1;

			// ifdef WITH_LIPSINC
			bShowLIPSincStats   = 1;

			bShowFrameRate		= 1;
			bShowRenderStats	= 1;
			bShowHardwareStats	= 1;
			bShowMatineeStats	= 1;
			bShowGameStats		= 1;
			bShowAudioStats		= 1;
			bShowNetStats		= 1;
			bShowStatsSummary	= 1;
			return 1;
		}
		else if(ParseCommand(&Cmd,TEXT("NONE")))
		{
			bShowAnimStats		= 0;

			// ifdef WITH_LIPSINC
			bShowLIPSincStats   = 0;

			bShowFrameRate		= 0;
			bShowRenderStats	= 0;
			bShowHardwareStats	= 0;
			bShowMatineeStats	= 0;
			bShowGameStats		= 0;
			bShowAudioStats		= 0;
			bShowNetStats		= 0;
			bShowStatsSummary	= 0;
			return 1;
		}
		// scion sz - show only stats for items slower than the given threshold
		// usage "STAT SLOWEST 0.1"
		else if( ParseCommand(&Cmd,TEXT("SLOWEST")) )
		{
			FLOAT MinThreshold = 0.0f;
			MinThreshold = appAtof( *ParseToken(Cmd,0) );
			if( MinThreshold > 0 )
			{
                bShowSlowestStats = 1;
				MinStatMSec = MinThreshold;
			}
			else
			{
				bShowSlowestStats = 0;
			}
			return 1;
		}
		else
			return 0;
	}
	//scion jg -- Added so we can display slow collision options
	else if(ParseCommand(&Cmd,TEXT("SHOWSLOWCOLL")))
	{
		bShowSlowCollisionMeshes = !bShowSlowCollisionMeshes;
		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("CRACKURL")) )
	{
		FURL URL(NULL,Cmd,TRAVEL_Absolute);
		if( URL.Valid )
		{
			Ar.Logf( TEXT("     Protocol: %s"), *URL.Protocol );
			Ar.Logf( TEXT("         Host: %s"), *URL.Host );
			Ar.Logf( TEXT("         Port: %i"), URL.Port );
			Ar.Logf( TEXT("          Map: %s"), *URL.Map );
			Ar.Logf( TEXT("   NumOptions: %i"), URL.Op.Num() );
			for( INT i=0; i<URL.Op.Num(); i++ )
				Ar.Logf( TEXT("     Option %i: %s"), i, *URL.Op(i) );
			Ar.Logf( TEXT("       Portal: %s"), *URL.Portal );
			Ar.Logf( TEXT("       String: '%s'"), *URL.String() );
		}
		else Ar.Logf( TEXT("BAD URL") );
		return 1;
	}
	// scion sz
	else if( ParseCommand(&Cmd,TEXT("DEBUGLIGHTS")) )
	{
		bShowLightStats = !bShowLightStats;
		return 1;
	}
	else return 0;
	unguard;
}

//
// Key handler.
//
UBOOL UEngine::Key( UViewport* Viewport, EInputKey Key, TCHAR Unicode )
{
	guard(UEngine::Key);

	// Allow the Interaction Master to process the event.  If it doesn't for right now
	// we continue down the orignal road until I get the console recoding completed :)
	if( !GIsRunning )
		return false;
	else if(Client->InteractionMaster)
		return Client->InteractionMaster->MasterProcessKeyType(Key,Unicode);
	else
		return false;

	unguard;
}

//
// Input event handler.
//
UBOOL UEngine::InputEvent( UViewport* Viewport, EInputKey iKey, EInputAction State, FLOAT Delta)
{
	guard(UEngine::InputEvent);


	// Allow the Interaction Master to process the event.  If it doesn't for right now
	// we continue down the orignal road until I get the console recoding completed :)

	if( !GIsRunning )
	{
		return 0;
	}
	else
	{
		if ( (Client-> InteractionMaster) && ( Client->InteractionMaster->MasterProcessKeyEvent(iKey, State, Delta ) ) )
		{
			if ( (State==IST_Release) || (Client->InteractionMaster->bRequireRawJoystick) )
				Viewport->Input->PreProcess(iKey, State, Delta);

			return 1;
		}
		else if (	Viewport->Input->PreProcess( iKey, State, Delta ) && Viewport->Input->Process( *GLog, iKey, State, Delta ) )
		{
			// Input system handled it.
			return 1;
		}
	}

	return 0;

	unguard;
}

INT UEngine::ChallengeResponse( INT Challenge )
{
	guard(UEngine::ChallengeResponse);
	return 0;
	unguard;
}

/*-----------------------------------------------------------------------------
	UServerCommandlet.
-----------------------------------------------------------------------------*/

void UServerCommandlet::StaticConstructor()
{
	guard(UServerCommandlet::StaticConstructor);

	LogToStdout = 1;
	IsClient    = 0;
	IsEditor    = 0;
	IsServer    = 1;
	LazyLoad    = 1;

	unguard;
}

INT UServerCommandlet::Main( const TCHAR* Parms )
{
	guard(UServerCommandlet::Main);

	// Language.
	TCHAR Temp[256];
	if( GConfig->GetString( TEXT("Engine.Engine"), TEXT("Language"), Temp, ARRAY_COUNT(Temp) ) )
	UObject::SetLanguage( Temp );

	// Create the editor class.
	UClass* EngineClass = UObject::StaticLoadClass( UEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.GameEngine"), NULL, LOAD_NoFail, NULL );
	UEngine* Engine = ConstructObject<UEngine>( EngineClass );
	UEngine::GEngine = Engine;
	Engine->Init();

	// Main loop.
	GIsRunning = 1;
	DOUBLE OldTime = appSeconds();
	DOUBLE SecondStartTime = OldTime;
	INT TickCount = 0;
	while( GIsRunning && !GIsRequestingExit )
	{
#ifndef FINAL_RELEASE
		// Clear stats (will also update old stats).
		GStats.Clear();
#endif
		// Update the world.
		guard(UpdateWorld);
		DOUBLE NewTime = appSeconds();
		Engine->Tick( NewTime - OldTime );
		OldTime = NewTime;
		TickCount++;
		if( OldTime > SecondStartTime + 1 )
		{
			Engine->CurrentTickRate = (FLOAT)TickCount / (OldTime - SecondStartTime);
			SecondStartTime = OldTime;
			TickCount = 0;
		}
		unguard;

		// Enforce optional maximum tick rate.
		guard(EnforceTickRate);
		FLOAT MaxTickRate = Engine->GetMaxTickRate();
		if( MaxTickRate>0.f )
		{
			FLOAT Delta = (1.f/MaxTickRate) - (appSeconds()-OldTime);
			appSleep( Max(0.f,Delta) );
		}
		unguard;
	}
	GIsRunning = 0;
	return 0;
	unguard;
}

IMPLEMENT_CLASS(UServerCommandlet)

/*-----------------------------------------------------------------------------
	UGlobalTempObjects.
-----------------------------------------------------------------------------*/

ENGINE_API UGlobalTempObjects* GGlobalTempObjects = NULL;
IMPLEMENT_CLASS(UGlobalTempObjects)

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

