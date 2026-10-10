/*=============================================================================
	UnSplitScreenMgr.cpp: splitscreen management
	Copyright 2004 Scion Studios, LLC. All Rights Reserved.

	Revision history:
		* Created by Sam Zamani
=============================================================================*/

#include "EnginePrivate.h"
#include "Engine.h"
#include "UnNet.h"

IMPLEMENT_CLASS(USplitScreenMgr);

/* scion =====================================================================
* USplitScreenMgr::AddSplitEntry
* Author: sz
* ============================================================================
* Handle addition of a new splitscreen viewport
* ============================================================================
*/
UBOOL USplitScreenMgr::AddSplitEntry( UViewport* Viewport )
{
    guard( USplitScreenMgr::AddSplitEntry );
	UBOOL Result = 0;

	if( !Viewport )
		return Result;

	// only add a new entry if we are not at capacity
	if( NumActiveSplitViews() < MaxSplitScreens )
	{
		ActiveSplitViews.AddUniqueItem( Viewport );
	}

	return Result;
	unguard;
}

/* scion =====================================================================
* USplitScreenMgr::RemoveSplitEntry
* Author: sz
* ============================================================================
* Handle removal of an existing splitscreen viewport
* ============================================================================
*/
UBOOL USplitScreenMgr::RemoveSplitEntry( UViewport* Viewport )
{
	guard( USplitScreenMgr::RemoveSplitEntry );
	UBOOL Result = 0;

	if( !Viewport )
		return Result;
    
	// find the entry for this viewport and remove it
	INT FoundIdx = ActiveSplitViews.FindItemIndex( Viewport );
	if( FoundIdx != INDEX_NONE )
	{
		ActiveSplitViews.Remove( FoundIdx );
		Result = 1;
	}

    return Result;	
	unguard;
}

/* scion =====================================================================
* USplitScreenMgr::IsSplitScreenViewport
* Author: sz
* ============================================================================
* returns TRUE if the given viewport is a split screen entry
* ============================================================================
*/
UBOOL USplitScreenMgr::IsSplitScreenViewport( UViewport* Viewport )
{
	guard( USplitScreenMgr::IsSplitScreenViewport );
	UBOOL Result = 0;

	if( Viewport )
	{
		// search list of active split viewports
		INT FoundIdx = ActiveSplitViews.FindItemIndex( Viewport );
		if( FoundIdx != INDEX_NONE )
		{
			Result = 1;
		}
	}	

	return Result;
	unguard;
}

/* scion =====================================================================
* USplitScreenMgr::IsOverlayViewport
* Author: sz
* ============================================================================
* returns TRUE if the given viewport is the overlay viewport used for splitscreen
* ============================================================================
*/
UBOOL USplitScreenMgr::IsOverlayViewport( UViewport* Viewport )
{
	UBOOL Result = 0;

	if( Viewport && 
		Viewport == OverlayView )
	{
		Result = 1;
	}

	return Result;
}

/* scion =====================================================================
* USplitScreenMgr::IsMenuGameActive
* Author: sz
* ============================================================================
* returns TRUE if we are not in the menu
* ============================================================================
*/
UBOOL USplitScreenMgr::IsMenuGameActive()
{
	guard( USplitScreenMgr::IsMenuGameActive );
	UBOOL Result = 0;

	UGameEngine* GameEngine = Cast<UGameEngine>(UEngine::GEngine);
	ULevel* Level = GameEngine->GLevel;
	if( Level && 
		appStricmp( Level->GetOuter()->GetName(), TEXT("MENU") )==0 )
	{
		Result = 1;
	}

	return Result;
	unguard;
}

/* scion =====================================================================
* USplitScreenMgr::SetupFirstView
* Author: sz
* ============================================================================
* Setup the first splitscreen viewport using the default view actor that
* should already be spawned.  The first split viewport should always be the
* first entry in ActiveSplitViews
* ============================================================================
*/
UViewport* USplitScreenMgr::SetupFirstView()
{
	guard(USplitScreenMgr::SetupFirstView);
	
	UViewport* FirstSplitView = NULL;
	if( ActiveSplitViews.Num() > 0 )
	{
		// already exists
		FirstSplitView = ActiveSplitViews(0);		
	}
	else
	{
		// use default viewport
		FirstSplitView = Client->Viewports(0);

		// Add a new entry for this splitscreen
		AddSplitEntry( FirstSplitView );

		// the GUIController will hold a list of active child viewports
		FirstSplitView->GUIController->eventAddChildViewport( FirstSplitView );

		// create a new console
		//UInteraction *Console = Client->InteractionMaster->eventAddInteraction(TEXT("ini:Engine.Engine.Console"),FirstSplitView, false);
		//FirstSplitView->Console = Console;
	}	

	return FirstSplitView;
	unguard;
}

/* scion =====================================================================
* USplitScreenMgr::SetupOverlayViewport
* Author: sz
* ============================================================================
* The overlay viewport is used to draw the menu.
* It is a fullscreen viewport that will draw last on top of 
* the other viewports. 
* ============================================================================
*/
UViewport* USplitScreenMgr::SetupOverlayViewport()
{
	guard( USplitScreenMgr::SetupOverlayViewport );

	UGameEngine* GameEngine = Cast<UGameEngine>(UEngine::GEngine); check(GameEngine);
	ULevel* Level = GameEngine->GLevel; check(Level);	

	// see if we are still in the menu
	UBOOL bMenuActive = IsMenuGameActive();

	if( !OverlayView )
	{
		// create a new overlay viewport if one doesn't already exist
		OverlayView = Client->NewViewport( NAME_None );
		// use default GUIController
		OverlayView->GUIController = Client->Viewports(0)->GUIController;
		OverlayView->GUIController->ViewportOwner = OverlayView;
		// use default global console
		OverlayView->Console = Client->Viewports(0)->Console;
		// make sure its z offset always draws on the front
		const FLOAT OVERLAY_Z_OFFSET = -100.f;
		OverlayView->OffsetZ = OVERLAY_Z_OFFSET;
		
		debugf( NAME_DevSplitScreen, TEXT("New overlay viewport created [%s]"), OverlayView->GetName() );
	}

	if( !OverlayView->Actor )
	{		
		// spawn player for the overlay view. This will also create the
		// pause camera effect for this viewport
		APlayerController* NewPlayer = Level->SpawnOverlayCameraActor( OverlayView, ROLE_None );
		check( NewPlayer );

		// make sure we don't try to auto-bind this viewport to a controller 
		// by giving it a semi-valid gamepad id
		OverlayView->SetGamepadID( UViewport::INVALID_GAMEPAD_ID-1 );
		// init input system
		OverlayView->Input->Init( OverlayView );
		// open the window (initialized rendev etc)
		OverlayView->OpenWindow( 1, 0, 0,0,0,0 );	
		// update viewports
		UpdateSplitViewports();

		// hack - restart any scene managers with the new controller
		if( bMenuActive )
		{
			//Find the scene manager
			ASceneManager* SceneMgr=NULL;
			for( INT i=0; i < Level->Actors.Num(); i++ )
			{
				SceneMgr = Cast<ASceneManager>( Level->Actors(i) );
				if( SceneMgr )
				{
					// restart the matinee scene with the new overlay player actor
					SceneMgr->TriggerActor = NewPlayer;
					SceneMgr->SceneStarted();
					break;
				}
			}
		}
	}

    return OverlayView;
	unguard;
}

/* scion =====================================================================
* USplitScreenMgr::RemoveOverlayViewport
* Author: sz
* ============================================================================
* Remove the overlay viewport and its player controller
* This should only happen if there are no more splitscreens
* ============================================================================
*/
UBOOL USplitScreenMgr::RemoveOverlayViewport()
{
	guard( USplitScreenMgr::RemoveOverlayViewport );
	UBOOL Result = 1;

	UGameEngine* GameEngine = Cast<UGameEngine>(UEngine::GEngine); check(GameEngine);
	ULevel* Level = GameEngine->GLevel;	check(Level);

	// reset ViewportOwner
	Client->Viewports(0)->GUIController->ViewportOwner = Client->Viewports(0);
	// give console back to default
	Client->Viewports(0)->Console = Client->Viewports(0)->Console;

    if( OverlayView )
	{
		// don't delete default viewport
		if( OverlayView != Client->Viewports(0) )
		{
			if( OverlayView->Actor &&
				OverlayView->Actor != Client->Viewports(0)->Actor )
			{
				// remove any player controller we created
				Level->DestroyActor( OverlayView->Actor );
			}

			OverlayView->Actor = NULL;
			OverlayView->GUIController = NULL;
			OverlayView->Console = NULL;

			delete OverlayView;			
		}

		OverlayView = NULL;
	}

    return Result;
	unguard;
}

/* scion =====================================================================
* USplitScreenMgr::CreateSplitViewport
* Author: sz
* ============================================================================
* Set initial splitscreen viewport settings.
* They are not drawn initially and are used only for input processing.
* ============================================================================
*/
void USplitScreenMgr::InitSplitViewport( UViewport* NewViewport )
{
	guard( USplitScreenMgr::InitSplitViewport );

	if( !NewViewport )
		return;

	// set net speed for current viewport to same net speed as our parent connection's viewport
	NewViewport->CurrentNetSpeed = Client->Viewports(0)->CurrentNetSpeed;

	// init input system
	NewViewport->Input->Init( NewViewport );
	// open the window (initialized rendev etc)
	NewViewport->OpenWindow( 1, 0, 0,0,0,0 );

	unguard;
}


/* scion =====================================================================
* USplitScreenMgr::CreateSplitViewport
* Author: sz
* ============================================================================
* Create a new viewport for the given ViewActor.  Add this new viewport to
* the list of active splitscreens.  Update detail levels and sizes of viewports.
* ============================================================================
*/
UViewport* USplitScreenMgr::CreateSplitViewport()
{
	guard( USplitScreenMgr::CreateSplitViewport );
	
	UViewport* NewViewport = NULL;

	// make sure num split views are within capacity
	if( ActiveSplitViews.Num() >= MaxSplitScreens )
	{
		debugf( NAME_DevSplitScreen, TEXT("Could not create new splitscreen. Maximum active.") );
		return NewViewport;
	}

	// must always have at least one split viewport
	if( !ActiveSplitViews.Num() ||
		!ActiveSplitViews(0)->Actor )
	{
		SetupFirstView();
	}

	// must always have an overlay viewport for use by the menus
	if( !OverlayView ||
		!OverlayView->Actor )
	{
		SetupOverlayViewport();
	}

	// create a new viewport for this splitscreen
	NewViewport = Client->NewViewport( NAME_None );
	debugf( NAME_DevSplitScreen, TEXT("New splitscreen viewport created [%s]"), NewViewport->GetName() );
	
	// use default global console
	NewViewport->Console = Client->Viewports(0)->Console;
	//UInteraction *Console = Client->InteractionMaster->eventAddInteraction(TEXT("ini:Engine.Engine.Console"),NewViewport, false);
	//NewViewport->Console = Console;

	// Add a new entry for this splitscreen
	AddSplitEntry( NewViewport );

	// set init splitscreen viewport settings
	InitSplitViewport( NewViewport );

	// update viewports
	UpdateSplitViewports();

	// sort viewports back to front
	Client->ZSortViewports();

	return NewViewport;
	unguard;
}

/* scion =====================================================================
* USplitScreenMgr::RemoveSplitViewport
* Author: sz
* ============================================================================
* Clean up the splitscreen viewport and remove it from the list of active
* entries.
* ============================================================================
*/
UBOOL USplitScreenMgr::RemoveSplitViewport( UViewport* Viewport )
{
	guard( USplitScreenMgr::RemoveSplitViewport );
	UBOOL Result = 0;

	UGameEngine* GameEngine = Cast<UGameEngine>(UEngine::GEngine); check(GameEngine);
	ULevel* Level = GameEngine->GLevel; check(Level);

	if( !Viewport )
		return Result;

	RemoveSplitEntry( Viewport );
	Result = 1;

	// remove any consoles we created
	if( Viewport->Console && 
		Viewport->Console != Client->Viewports(0)->Console )
	{
		Viewport->Console->ViewportOwner = NULL;
		Client->InteractionMaster->eventRemoveInteraction( Viewport->Console );
		delete Viewport->Console;
		Viewport->Console = NULL;
	}
	
	if( Viewport->GUIController )
	{
		// remove child entry from GUIController
		Viewport->GUIController->eventRemoveChildViewport( Viewport );

		// remove any gui controller that we may have created
		if( Viewport->GUIController != Client->Viewports(0)->GUIController )
		{
			Viewport->GUIController->ViewportOwner = NULL;
			Client->InteractionMaster->eventRemoveInteraction( Viewport->GUIController );            
			delete Viewport->GUIController;
			Viewport->GUIController = NULL;
		}		
	}	

	if( Viewport != Client->Viewports(0) )
	{
		// don't delete default player
		if( Viewport->Actor && 
			Viewport->Actor != Client->Viewports(0)->Actor )
		{
			// remove any player controller we created
			Level->DestroyActor( Viewport->Actor );
		}

		Viewport->Actor = NULL;
		Viewport->GUIController = NULL;
		Viewport->Console = NULL;

		delete Viewport;
	}

	// if we are down to a single split entry then remove it as well
	if( ActiveSplitViews.Num() == 1 )
	{
		Result = RemoveSplitViewport( ActiveSplitViews(0) );
		return Result;
	}

	// also remove the OverlayView if we no longer have any split viewports
	if( ActiveSplitViews.Num() == 0 ) 
	{
		RemoveOverlayViewport();
	}
	
	// update viewports
	UpdateSplitViewports();

	return Result;
	unguard;
}

/* scion =====================================================================
* USplitScreenMgr::CreateSplitPlayer
* Author: sz
* ============================================================================
* Spawns a new player.  If a viewport is specified then it will use the profile
* information to spawn a new player and it will also be bound to that viewport.
* If no viewport is specified then a new one is created to be used with the 
* GamepadID specified
* ============================================================================
*/
APlayerController* USplitScreenMgr::CreateSplitPlayer( UViewport* Viewport, INT GamepadID )
{
	guard(USplitScreenMgr::CreateSplitPlayer);
	
	UGameEngine* GameEngine = Cast<UGameEngine>(UEngine::GEngine); check(GameEngine);
	ULevel* Level = GameEngine->GLevel; check(Level);
	ALevelInfo* LevelInfo = Level ? Level->GetLevelInfo() : NULL;

	APlayerController* NewPlayer = NULL;

	// make sure first viewport has an actor
	if( ActiveSplitViews.Num() )
	{
		check( ActiveSplitViews(0) );

		if( !ActiveSplitViews(0)->Actor )
		{
			SetupFirstView();
		}		

		if( ActiveSplitViews(0) == Viewport )
		{
			return Viewport->Actor;
		}
	}

	// make sure overlay viewport has an actor
	if( OverlayView )
	{
		if( !OverlayView->Actor )
		{
			SetupOverlayViewport();
		}		

		if( OverlayView == Viewport )
		{
			return OverlayView->Actor;	
		}
	}	

	FURL URL;
	// Create default URL
	URL.LoadURLConfig(TEXT("DefaultPlayer"), TEXT("DefUser"));

	if( Viewport && Viewport->Profile )
	{
		// create URL from viewport.profile
		UGameStartInfoBase* GSI = UGameStartInfoBase::GetGameStart();
		check(GSI);

		FString PlayerStr = GSI->eventGetSpawnURL(ActiveSplitViews.Num() ? 1 : 0, Viewport->Profile);
		URL = FURL( &Viewport->PlayerURL, *PlayerStr, TRAVEL_Partial );		
	}

	debugf( NAME_DevSplitScreen, TEXT("Split player spawn URL:'%s'"), *URL.String() );

	// spawn a player controller if we are not a client or dedicated server connection
	if( LevelInfo && 
		(LevelInfo->NetMode == NM_Standalone || LevelInfo->NetMode == NM_ListenServer ) )
	{
		if( NumActiveSplitViews() >= MaxSplitScreens )
		{
			debugf( NAME_DevSplitScreen, TEXT("Too many splitscreens.") );
		}
		else
		{
			// create the viewport, if necessary
			UBOOL bCreatedViewport = 0;
			if (!Viewport)
			{
				bCreatedViewport = 1;
				// create a new viewport for this player
				Viewport = CreateSplitViewport();
				if (!Viewport)
				{
					debugf(NAME_DevSplitScreen, TEXT("Could not create splitscreen viewport for GamepadID=%d"), GamepadID);
					return NULL;
				}			
			}
			// spawn new play actor.
			FString Error;
			NewPlayer = Level->SpawnPlayActor(Viewport, ROLE_SimulatedProxy, URL, Error);
			if (!NewPlayer)
			{
				debugf(NAME_DevSplitScreen, TEXT("Could not spawn player. Error=%s"), *Error);
				if (bCreatedViewport)
				{
					RemoveSplitViewport(Viewport);
				}
			}
			else if (bCreatedViewport)
			{
				//set new gamepad id
				Viewport->SetGamepadID(GamepadID);
				// set the one and only GUIController
				UBaseGUIController* GUIController = Cast<UBaseGUIController>(Client->InteractionMaster->GetGUIController(NULL));
				if (GUIController)
				{
					// the GUIController will hold a list of active child viewports
					GUIController->eventAddChildViewport(Viewport);
					Viewport->GUIController = GUIController;
				}
				else
				{
					// use default GUIController
					Viewport->GUIController = Client->Viewports(0)->GUIController;
				}
			}	
		}
	}

	// For client splits, request a new player join on the server.
	// Once the actual player controller gets replicated to the client,
	// it is handled in UChildConnection::HandleClientPlayer(). That
	// is where a new split screen viewport is created for it.
	if( LevelInfo && 
		(LevelInfo->NetMode == NM_Client) )
	{
		// only allow child join requests if the default client player has
		// already connected
		if( bClientPlayerActive )
		{
			// send server command for new child connection
			Level->NetDriver->ServerConnection->Logf( *FString::Printf(TEXT("JOINSPLIT GAMEPAD=%d URL=%s"), GamepadID, *URL.String() ) );
			// actually send command
			Level->NetDriver->ServerConnection->FlushNet();		
		}
		// if an existing viewport wants a player then create a placeholder one
		// until the default client player is ready
		else if( Viewport )
		{
			// create a temporary actor for this viewport
			// this actor will be destroyed once we get a replicated player 
			// (see UChildConnection::HandleClientPlayer)
			ACamera* TempCamera = Cast<ACamera>( Level->SpawnActor( ACamera::StaticClass(), NAME_None, FVector(0,0,0), FRotator(0,0,0), NULL, 1 ) );
			TempCamera->SetFlags( RF_NotForClient | RF_NotForServer );
			TempCamera->ShowFlags = SHOW_GameDefault;
			TempCamera->RendMap = REN_DynLight;
            // associate with viewport
			TempCamera->SetPlayer( Viewport );
			//set new gamepad id
			Viewport->SetGamepadID( GamepadID );
		}		
	}

	return NewPlayer;
	unguard;
}

/*==========================================================================================================
USplitScreenMgr::ResetState
Clears all splitscreen viewports.

last modified by:	Ron Prestenback
==========================================================================================================*/
void USplitScreenMgr::ResetState()
{
	guard(USplitScreenMgr::ResetState);

	// clear any pending hardware occlusion queries for the
	// splitscreen viewports before deleting them 
	// (since that changes their indices)
	for( INT vpIdx=0; vpIdx < ActiveSplitViews.Num(); vpIdx++ )
	{
		UViewport* Viewport = ActiveSplitViews(vpIdx);
		if( Viewport )
		{
			ClearQueriesForViewport( Viewport );
		}
	}
	// do the same for overlay viewport
	if( OverlayView )
	{
		ClearQueriesForViewport( OverlayView );
	}

	while ( ActiveSplitViews.Num() )
	{
		UViewport* Viewport = ActiveSplitViews(0);
		if ( !RemoveSplitViewport( Viewport ) )
		{
			if ( Viewport == NULL )
				ActiveSplitViews.Remove(0);
			else
			{
				debugf(TEXT("Could not remove split-screen viewport %i: '%s'"), 0, Viewport->GetName());
				break;
			}
		}
	}

	unguard;
}

/* scion =====================================================================
* USplitScreenMgr::MatchSplitViewportsToActors
* Author: sz
* ============================================================================
* Spawn players for any splitscreen viewports that need them.
* ============================================================================
*/
void USplitScreenMgr::MatchSplitViewportsToActors()
{
	guard(USplitScreenMgr::MatchSplitViewportsToActors);

	// remove all splits if we returned to the menu
	if( IsMenuGameActive() )
	{
		ResetState();
	}
	else
	{
		for( INT vpIdx=0; vpIdx < ActiveSplitViews.Num(); vpIdx++ )
		{
			UViewport* Viewport = ActiveSplitViews(vpIdx);
			CreateSplitPlayer( Viewport, Viewport->GamepadID );			
		}		

		// update viewports
		UpdateSplitViewports();
	}

	unguard;
}

/* scion =====================================================================
* USplitScreenMgr::NotifyReceivedClientPlayer
* Author: sz
* ============================================================================
* When in a network game, do not create any splitscreens until the default
* client player has connected first.
* ============================================================================
*/
void USplitScreenMgr::NotifyReceivedClientPlayer()
{
	guard(USplitScreenMgr::NotifyReceivedClientPlayer);	

	// the default client player has spawned in and is bound to
	// its viewport
	bClientPlayerActive = 1;
	// match up any splitscreens that were waiting for it to be available
	MatchSplitViewportsToActors();

	unguard;
}

/* scion =====================================================================
* USplitScreenMgr::NotifyLevelChange
* Author: sz
* ============================================================================
* Sent when a new map is loaded  
* ============================================================================
*/
void USplitScreenMgr::NotifyLevelChange()
{
	guard( USplitScreenMgr::NotifyLevelChange );
	
	// since all actors are destroyed on level change, the
	// default client player is no longer available
	bClientPlayerActive = 0;
	// needed?
	eventNotifyLevelChange();	

	unguard;
}

/* scion =====================================================================
* USplitScreenMgr::ClearBackground
* Author: sz
* ============================================================================
* Clear buffers to current BorderColor.
* ============================================================================
*/
void USplitScreenMgr::ClearBackground()
{
	guard( USplitScreenMgr::ClearBackground );

	UViewport* Viewport = Client->Viewports(0); check(Viewport);

	// set the viewport to the full screen width/height
	FRenderTarget* CurRenderTarget = Viewport->RI->GetRenderTarget();	
	Viewport->RI->SetViewport( 0, 0, CurRenderTarget->GetWidth(), CurRenderTarget->GetHeight() );
	// clear back buffer
	Viewport->RI->Clear(1,BorderColor,1,1.0f,1,0);
	Viewport->RenDev->Present( Client->Viewports(0) );
	// clear front buffer
	Viewport->RI->Clear(1,BorderColor,1,1.0f,1,0);
	// pop viewport changes
	Viewport->RI->ResetViewport();

	unguard;
}

/* scion =====================================================================
* USplitScreenMgr::UpdateSplitViewports
* Author: sz
* ============================================================================
* Update detail levels and sizes
* ============================================================================
*/
void USplitScreenMgr::UpdateSplitViewports()
{
	guard(USplitScreenMgr::UpdateSplitViewports);

	// update display settings for each split
	UpdateDisplayFlags();
	// reset level and viewport detail settings
	UpdateDetailLevels();
	// resize splitscreens
	UpdateSizes();

	unguard;
}

/* scion =====================================================================
* USplitScreenMgr::UpdateDisplayFlags
* Author: sz
* ============================================================================
* Decide which viewports should display the menu, the scene, or just be
* disabled.  
* ============================================================================
*/
void USplitScreenMgr::UpdateDisplayFlags()
{
	guard(USplitScreenMgr::UpdateDisplayFlags);

	if( !Client->Viewports.Num() ||
		!Client->Viewports(0) )
		return;

	if( ActiveSplitViews.Num() > 0 )
	{
		// see if we are still in the menu
		UBOOL bMenuActive = IsMenuGameActive();
		// iterate through active split views
		for( INT ViewIdx=0; ViewIdx < ActiveSplitViews.Num(); ViewIdx++ )
		{
			UViewport* Viewport = ActiveSplitViews(ViewIdx);
			// splitscreen viewports should never show the menu
			// only the overlay viewport shows the menu
			Viewport->bShowMenu = 0;
			// they should always display the scene
			Viewport->bShowScene = 1;
			// initially the viewport is not drawn
			// this should be disabled once the game starts
			Viewport->bDisableDraw = bMenuActive;
		}				
		
	}
	else
	{
		// restore defaults
		UViewport* DefaultView = Client->Viewports(0);
		DefaultView->bShowMenu = 1;
		DefaultView->bShowScene = 1;
		DefaultView->bDisableDraw = 0;
	}

	// setup overlay viewport settings
	if( OverlayView )
	{
		// see if we are still in the menu
		UBOOL bMenuActive = IsMenuGameActive();
		// always enable menu display for this viewport
		OverlayView->bShowMenu = 1;
		// initially enable level rendering
		// this should be disabled when the game starts
		OverlayView->bShowScene = bMenuActive;

	}

	unguard;
}

/* scion =====================================================================
* USplitScreenMgr::UpdateSizes
* Author: sz
* ============================================================================
* Resize and place currently active splitscreen viewports
* ============================================================================
*/
void USplitScreenMgr::UpdateSizes()
{
	guard( USplitScreenMgr::UpdateSizes );

	UGameEngine* GameEngine = Cast<UGameEngine>(UEngine::GEngine); check(GameEngine);
	ULevel* Level = GameEngine->GLevel; check(Level);

	if( !Client->Viewports.Num() ||
		!Client->Viewports(0) )
		return;	

	// remember the initial viewport size of our full screen
	INT InitSizeX,InitSizeY;
	Client->GetInitScreenSize( InitSizeX,InitSizeY );

	// SS_SPlit_Vertical - not used

	if( OverlayView )
	{
		OverlayView->OffsetX = 0;
		OverlayView->OffsetX = 0;
		OverlayView->SizeX = InitSizeX;
		OverlayView->SizeY = InitSizeY;
		OverlayView->ScaleX = 1.f;
		OverlayView->ScaleY = 1.f;
	}

	const TArrayNoInit<class UViewport*>& ValidSplitVPs = ActiveSplitViews;
	switch( ValidSplitVPs.Num() )
	{
	case 0:
	case 1:
		{
			// back to fullscreen
			UViewport* DefaultVP = Client->Viewports(0);
			DefaultVP->OffsetX = 0;
			DefaultVP->OffsetX = 0;
			DefaultVP->SizeX = InitSizeX;
			DefaultVP->SizeY = InitSizeY;
			DefaultVP->ScaleX = 1.f;
			DefaultVP->ScaleY = 1.f;
		}
		break;
	case 2:
		{
			// +-----+
			// |  0  |
			// +-----+
			// |  1  |
			// +-----+

			// top half
			ValidSplitVPs(0)->OffsetX = 0;
			ValidSplitVPs(0)->OffsetY = 0;
			ValidSplitVPs(0)->SizeX = InitSizeX;
			ValidSplitVPs(0)->SizeY = InitSizeY / 2;

			// bottom half
			ValidSplitVPs(1)->OffsetX = 0;
			ValidSplitVPs(1)->OffsetY = ValidSplitVPs(0)->SizeY;
			ValidSplitVPs(1)->SizeX = InitSizeX;
			ValidSplitVPs(1)->SizeY = InitSizeY / 2;
		}
		break;
	case 3:
		{
			// +-----+
			// |  0  |
			// +--+--+
			// |1 |2 |
			// +--+--+

			// top half
			ValidSplitVPs(0)->OffsetX = 0;
			ValidSplitVPs(0)->OffsetY = 0;
			ValidSplitVPs(0)->SizeX = InitSizeX;
			ValidSplitVPs(0)->SizeY = InitSizeY / 2;

			// bottom half

			// vertical split

			// left
			ValidSplitVPs(1)->OffsetX = 0;
			ValidSplitVPs(1)->OffsetY = 0 + ValidSplitVPs(0)->SizeY;
			ValidSplitVPs(1)->SizeX = InitSizeX / 2;
			ValidSplitVPs(1)->SizeY = InitSizeY / 2;

			// right
			ValidSplitVPs(2)->OffsetX = ValidSplitVPs(1)->SizeX;
			ValidSplitVPs(2)->OffsetY = ValidSplitVPs(1)->OffsetY;
			ValidSplitVPs(2)->SizeX = InitSizeX / 2;
			ValidSplitVPs(2)->SizeY = InitSizeY / 2;
		}
		break;
	case 4:
		{
			// +--+--+
			// |0 |1 |
			// +--+--+
			// |2 |3 |
			// +--+--+

			// top half

			// vertical split

			// left
			ValidSplitVPs(0)->OffsetX = 0;
			ValidSplitVPs(0)->OffsetY = 0;
			ValidSplitVPs(0)->SizeX = InitSizeX / 2;
			ValidSplitVPs(0)->SizeY = InitSizeY / 2;

			// right
			ValidSplitVPs(1)->OffsetX = ValidSplitVPs(0)->SizeX;
			ValidSplitVPs(1)->OffsetY = 0;
			ValidSplitVPs(1)->SizeX = InitSizeX / 2;
			ValidSplitVPs(1)->SizeY = InitSizeY / 2;

			// bottom half

			// vertical split

			// left
			ValidSplitVPs(2)->OffsetX = 0;
			ValidSplitVPs(2)->OffsetY = 0 + ValidSplitVPs(0)->SizeY;
			ValidSplitVPs(2)->SizeX = InitSizeX / 2;
			ValidSplitVPs(2)->SizeY = InitSizeY / 2;

			// right
			ValidSplitVPs(3)->OffsetX = ValidSplitVPs(2)->SizeX;
			ValidSplitVPs(3)->OffsetY = ValidSplitVPs(2)->OffsetY;
			ValidSplitVPs(3)->SizeX = InitSizeX / 2;
			ValidSplitVPs(3)->SizeY = InitSizeY / 2;
		}
		break;

	default:
		debugf( NAME_DevSplitScreen, TEXT("Splitscreen: invalid number of splits") );
	}

	// adjust offsets and sizes for border thickness
	for( INT i=0; i < ValidSplitVPs.Num(); i++ )
	{
		ValidSplitVPs(i)->OffsetX += BorderThickness;
		ValidSplitVPs(i)->OffsetY += BorderThickness;
		ValidSplitVPs(i)->SizeX -= BorderThickness * 2;
		ValidSplitVPs(i)->SizeY -= BorderThickness * 2;
	}

	// set viewport scales to match new sizes
	for( INT i=0; i < ValidSplitVPs.Num(); i++ )
	{
		ValidSplitVPs(i)->ScaleX = (FLOAT)ValidSplitVPs(i)->SizeX / (FLOAT)InitSizeX;
		ValidSplitVPs(i)->ScaleY = (FLOAT)ValidSplitVPs(i)->SizeY / (FLOAT)InitSizeY;
	}

	unguard;
}

/* uc2 port (G-6) ===========================================================
* USplitScreenMgr::RefreshPlayerHUDs
* ============================================================================
* On the Xbox the split-screen layout only changes in the menus, and every player gets its HUD when the next
* map logs it in (PlayerController.ClientSetHUD: the <HUD>_Split class and SetSplitPosition when more than one
* viewport is up). A player joining or leaving during a match (SDL2Drv's SplitScreenJoinInMatch, SPLIT ADD/
* REMOVE) changes the layout of players who are already in: they get their HUD again the way Epic's own
* PlayerController.PostBeginPlay re-sends it ("so that if we are a splitscreen client we can set up the HUD
* correctly"), from the HUD's base class, so ClientSetHUD picks the split or the full-screen one itself.
* ============================================================================
*/
void USplitScreenMgr::RefreshPlayerHUDs()
{
	guard(USplitScreenMgr::RefreshPlayerHUDs);

	if( IsMenuGameActive() )
		return;

	UFunction* ClientSetHUD = NULL;
	for( INT i=0; i<Client->Viewports.Num(); i++ )
	{
		UViewport* Viewport = Client->Viewports(i);
		APlayerController* PC = Viewport ? Viewport->Actor : NULL;
		if( !PC || !PC->myHUD || IsOverlayViewport(Viewport) || PC->bDeleteMe )
			continue;
		UClass* HUDClass = PC->myHUD->GetClass();
		FString Name = HUDClass->GetName();
		if( Name.Len() > 6 && appStricmp( *Name.Right(6), TEXT("_Split") )==0 )
		{
			UClass* Base = FindObject<UClass>( HUDClass->GetOuter(), *Name.LeftChop(6) );
			if( Base )
				HUDClass = Base;
		}
		struct { UClass* NewHUDClass; UClass* NewScoringType; } Parms;
		Parms.NewHUDClass		= HUDClass;
		Parms.NewScoringType	= PC->myHUD->ScoreBoard ? ((UObject*)PC->myHUD->ScoreBoard)->GetClass() : NULL;
		ClientSetHUD = PC->FindFunction( FName(TEXT("ClientSetHUD")) );
		if( ClientSetHUD )
		{
			debugf( NAME_DevSplitScreen, TEXT("Split screen: %s gets its HUD again (%s, viewport %i of %i)"), PC->GetName(), HUDClass->GetName(), i, Client->Viewports.Num() );
			PC->ProcessEvent( ClientSetHUD, &Parms );
		}
		// The weapons' crosshairs, which UCPlayerController caches (ScriptedCrosshairs), are squeezed for a split
		// HUD when a weapon first makes them (UCWeapon.CreateScriptedCrosshair: ScaleY = 0.5 if the HUD
		// IsSplitScreen, i.e. more than one viewport); one made before the layout changed keeps the old
		// shape (the crosshair of a player who was alone sat on the bottom edge of a half screen). Give them
		// the shape that rule gives now. (By name: the classes are UCGameplay's and UCElements' scripts.)
		UArrayProperty* Crosshairs = FindField<UArrayProperty>( PC->GetClass(), TEXT("ScriptedCrosshairs") );
		if( Crosshairs && Cast<UObjectProperty>(Crosshairs->Inner) )
		{
			FArray* Array = (FArray*)((BYTE*)PC + Crosshairs->Offset);
			for( INT c=0; c<Array->Num(); c++ )
			{
				UObject* Crosshair = ((UObject**)Array->GetData())[c];
				UFloatProperty* ScaleY = Crosshair ? FindField<UFloatProperty>( Crosshair->GetClass(), TEXT("ScaleY") ) : NULL;
				if( ScaleY )
					*(FLOAT*)((BYTE*)Crosshair + ScaleY->Offset) = Client->Viewports.Num() > 1 ? 0.5f
						: *(FLOAT*)((BYTE*)Crosshair->GetClass()->GetDefaultObject() + ScaleY->Offset);
			}
		}
	}

	unguard;
}

/* scion =====================================================================
* USplitScreenMgr::UpdateDetailLevels
* Author: sz
* ============================================================================
* Select current detail level based on the # of active split screens and update
* the level,client, and each viewport.
* ============================================================================
*/
void USplitScreenMgr::UpdateDetailLevels()
{
	guard( USplitScreenMgr::UpdateDetailLevels );

	UGameEngine* GameEngine = Cast<UGameEngine>(UEngine::GEngine); check(GameEngine);
	ULevel* Level = GameEngine->GLevel; check(Level);
	ALevelInfo* LevelInfo = Level->GetLevelInfo(); check(LevelInfo);

	if( !Client->Viewports.Num() ||
		!Client->Viewports(0) )
		return;	

	// dynamic gamma adjusts the device gamma ramp
	// so it is a full screen effect and doesn't make sense
	// when any split views are active
	// uc2 port (G-6): unless the render device applies it per viewport (FRenderCaps::ViewportDynamicGamma)
	URenderDevice* RenDev = Client->Viewports(0)->RenDev;
	UBOOL bViewportGamma = RenDev && RenDev->GetRenderCaps() && RenDev->GetRenderCaps()->ViewportDynamicGamma;
	if( NumActiveSplitViews() != 0 )
	{
		// disable dynamic gamma
		if( !bViewportGamma )
			Client->Exec( TEXT("DYNAMICGAMMA 0") );
		// disable dynamic draw scale
		LevelInfo->bUseDynamicDrawScale = 0;
	}
	else
	{
		// re-enable dynamic gamma
		if ( !IsMenuGameActive() )
			Client->Exec( TEXT("DYNAMICGAMMA 1") );

		// re-enable dynamic draw scale
		LevelInfo->bUseDynamicDrawScale = 1;
	}

	// see SplitScreenMgr.uc for list of detail levels
	if( SSDetailLevels.Num() )
	{
		// choose detail level settings based on the # of splitscreen
		// viewports that are currently active
		INT NumValidVPs = NumActiveSplitViews();
		if( NumValidVPs > 0 )
		{
			// subtract one since the 0th entry is the default
			// detail level we should revert to
			INT DetailLevelIdx = Min<INT>( NumValidVPs-1, SSDetailLevels.Num()-1 );		
			FSplitScreenDetailLevel& SplitDetailLevel = SSDetailLevels( DetailLevelIdx );
			// uc2 port (G-9): on a client that keeps full detail (SDL2Drv SplitScreenFullDetail, True on PC) every
			// split view gets entry 0, the single player's settings, except ReduceFOV: that is the FOV maths of
			// the layout (Epic's view with AutoFOV off; AutoFOV replaces it per frame), not a detail setting.
			FSplitScreenDetailLevel FullDetailLevel = SSDetailLevels( 0 );
			FullDetailLevel.ReduceFOV = SplitDetailLevel.ReduceFOV;
			UBOOL bFullDetail = Client->SplitScreenKeepsFullDetail();
			FSplitScreenDetailLevel& DetailLevel = bFullDetail ? FullDetailLevel : SplitDetailLevel;
			debugf( NAME_Log, TEXT("Split screen: %i views, detail %s (DetailMode %i, coronas %i, decals %i, projectors %i, deco layers %i, blob shadows %i, particles x%.1f, ReduceFOV %.2f)"),
				NumValidVPs, bFullDetail ? TEXT("of one player (SplitScreenFullDetail)") : *FString::Printf(TEXT("level %i (Xbox)"), DetailLevelIdx),
				DetailLevel.DetailMode, (INT)DetailLevel.bCoronas, (INT)DetailLevel.bDecals, (INT)DetailLevel.bProjectors, (INT)DetailLevel.bTerrainDecoLayers,
				(INT)DetailLevel.bBlobShadows, DetailLevel.ReduceParticles, DetailLevel.ReduceFOV );
			for( INT vpIdx=0; vpIdx < NumValidVPs; vpIdx++ )
			{			
				SetViewportDetail( ActiveSplitViews(vpIdx), DetailLevel );
				SetClientDetail( Client, DetailLevel );
				SetLevelDetail( Level, DetailLevel );
			}
		}
		else
		{
			// restore to original settings
			UViewport* DefaultView = Client->Viewports(0);			
			FSplitScreenDetailLevel& DetailLevel = SSDetailLevels( 0 );				
			SetViewportDetail( DefaultView, DetailLevel );			
			SetClientDetail( Client, DetailLevel );
			SetLevelDetail( Level, DetailLevel );
		}
		
	}
	else
	{
		debugf( NAME_DevSplitScreen, TEXT("No LOD information available") );
	}	

	unguard;
}

/* scion =====================================================================
* USplitScreenMgr::SetViewportDetail
* Author: sz
* ============================================================================
* Set the detail mode for the viewport
* depending on the current splitscreen detail level settings
* ============================================================================
*/
void USplitScreenMgr::SetViewportDetail( UViewport* Viewport, FSplitScreenDetailLevel& Detail )
{
	guard( USplitScreenMgr::SetViewportDetail );

	// blob shadows
	Viewport->bForceBlobShadows = Detail.bBlobShadows;
	// camera effects
	Viewport->bShowCameraEffects = Detail.bCameraEffects;
	// reduce FOV for this viewport by desired amount
	Viewport->ScaleFOVX = Detail.ReduceFOV;
	Viewport->ScaleFOVY = Detail.ReduceFOV;

	unguard;
}

/* scion =====================================================================
* USplitScreenMgr::SetClientDetailLevel
* Author: sz
* ============================================================================
* Set the detail mode for the Client
* depending on the current splitscreen detail level settings
* ============================================================================
*/
void USplitScreenMgr::SetClientDetail( UClient* InClient, FSplitScreenDetailLevel& Detail )
{
	guard( USplitScreenMgr::SetClientDetail );
	check(InClient);

	// corona rendering
	InClient->Coronas = Detail.bCoronas;
	// dynamic lighting (bsp cube maps)
	InClient->NoDynamicLights = (!Detail.bDynamicLights);
	// decal projectors
	InClient->Decals = Detail.bDecals;
	// toggle terrain deco layers
	InClient->DecoLayers = Detail.bTerrainDecoLayers;
	// toggle projectors
	InClient->Projectors = Detail.bProjectors && InClient->GetClass()->GetDefaultObject<UClient>()->Projectors;

	unguard;
}

/* scion =====================================================================
* USplitScreenMgr::SetLevelDetailLevel
* Author: sz
* ============================================================================
* Set the detail mode for the Level
* depending on the current splitscreen detail level settings
* ============================================================================
*/
void USplitScreenMgr::SetLevelDetail( ULevel* Level, FSplitScreenDetailLevel& Detail )
{
	guard(USplitScreenMgr::SetLevelDetail);
	check(Level);

	// level detail settings
	Level->DetailChange( (EDetailMode)Detail.DetailMode );
	//LevelInfo->DecalStayScale = Detail.DecalStayScale;
	// scale max # of active particles
	Level->Exec( *FString::Printf(TEXT("PARTICLEATTEN %f"), Detail.ReduceParticles ) );	
	
	unguard;
}


