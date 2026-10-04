/*=============================================================================
	UnViewportMgr.cpp: viewport management
	Copyright 2004 Scion Studios, LLC. All Rights Reserved.

	Revision history:
		* Created by Sam Zamani
=============================================================================*/

#include "EnginePrivate.h"

IMPLEMENT_CLASS(UViewportMgr);

/* scion =====================================================================
* UViewportMgr::execAddViewport
* Author: sz
* ============================================================================
* see ViewportMgr.uc
* ============================================================================
*/
void UViewportMgr::execAddViewport( FFrame& Stack, RESULT_DECL )
{
	guard(UViewportMgr::execAddViewport);
	P_GET_ACTOR( ViewActor );
	P_GET_INT( OffsetX );
	P_GET_INT( OffsetY );
	P_GET_INT( SizeX );
	P_GET_INT( SizeY );
	P_GET_INT_OPTX( Z, 0 );
	P_FINISH;

	INT AddIndex = AddViewport( ViewActor, OffsetX, OffsetY, SizeX, SizeY, Z );

	*(INT*)Result = AddIndex;

	unguard;
}

/* scion =====================================================================
* UViewportMgr::execRemoveViewport
* Author: sz
* ============================================================================
* see ViewportMgr.uc
* ============================================================================
*/
void UViewportMgr::execRemoveViewport( FFrame& Stack, RESULT_DECL )
{
	guard(UViewportMgr::execRemoveViewport);
	P_GET_INT( RemoveIndex );
	P_FINISH;

	RemoveViewport( RemoveIndex );

	unguard;
}


/* scion =====================================================================
* UViewportMgr::AddViewport
* Author: sz
* ============================================================================
* Create a new viewport of the given size and offset for the viewactor.
* If the viewactor is a playercontroller then it is linked to the new viewport
* directly.  Otherwise, a dummy playercontroller is used as a camera to 
* look at the viewactr.
* ============================================================================
*/
INT UViewportMgr::AddViewport( AActor* ViewActor, INT OffsetX, INT OffsetY, INT SizeX, INT SizeY, INT Z )
{
	guard(UViewportMgr::AddViewport);
	INT AddIndex = -1;

	if( !ViewActor || ViewActor->bDeleteMe )
		return AddIndex;

	UGameEngine* GameEngine = Cast<UGameEngine>(UEngine::GEngine); check(GameEngine);
	ULevel* Level = GameEngine->GLevel; check(Level);
	UClient* Client = GameEngine->Client; check(Client);

	// create a new viewport
	UViewport* NewViewport = Client->NewViewport( NAME_None );

	APlayerController* PC = Cast<APlayerController>( ViewActor );
	if( PC )
	{
		// use the player controller directly to control
		// this viewport
		PC->SetPlayer( NewViewport );
	}
	else
	{
		// only player controllers can use a viewport so
		// spawn a camera player controller and have it
		// look at the view actor
		Level->SpawnChaseCameraActor( ViewActor, NewViewport, ROLE_SimulatedProxy );
	}
	// add a new entry for this viewport to the viewport manager
	// try to find an unused entry first
	UBOOL Found=0;
	for( INT i=0; i < ViewportEntries.Num(); i++ )
	{
		FViewportEntry& VPEntry = ViewportEntries(i);
		if( !VPEntry.Viewport )
		{
			VPEntry.Viewport= NewViewport;
			VPEntry.ViewActor = ViewActor;
			VPEntry.PlayerController = NewViewport->Actor;
			AddIndex = i;
			Found=1;
			break;
		}
	}
	if( !Found )
	{
		// create a new entry and add to the list
		FViewportEntry VPEntry = { NewViewport, NewViewport->Actor, ViewActor };
		AddIndex = ViewportEntries.AddItem( VPEntry );
	}

	// create a new console
	UInteraction* Console = Client->InteractionMaster->eventAddInteraction(TEXT("ini:Engine.Engine.Console"),NewViewport, false);
	NewViewport->Console = Console;
	// init input and open device window
	NewViewport->Input->Init( NewViewport );	
#ifdef _XBOX
	NewViewport->OpenWindow( 1, 0, 0,0,0,0 );
#else
	NewViewport->OpenWindow( 0, 0, INDEX_NONE,INDEX_NONE,INDEX_NONE,INDEX_NONE );
#endif

	// set viewport position/size
	NewViewport->OffsetX = OffsetX;
	NewViewport->OffsetY = OffsetY;
	NewViewport->OffsetZ = Z;
	NewViewport->SizeX = SizeX;
	NewViewport->SizeY = SizeY;

	// disable some detail settings
	NewViewport->bForceBlobShadows = 1;
	NewViewport->bShowCameraEffects = 0;

	// sort viewports back to front
	Client->ZSortViewports();

	// return index into ViewportEntries array for this viewport
	return AddIndex;
	unguard;
}


/* scion =====================================================================
* UViewportMgr::RemoveViewport
* Author: sz
* ============================================================================
* 
* ============================================================================
*/
void UViewportMgr::RemoveViewport( INT RemoveIndex )
{
	guard(UViewportMgr::RemoveViewport);
	
	UGameEngine* GameEngine = Cast<UGameEngine>(UEngine::GEngine); check(GameEngine);
	ULevel* Level = GameEngine->GLevel; check(Level);
	UClient* Client = GameEngine->Client; check(Client);

	if( RemoveIndex < ViewportEntries.Num() )
	{
		FViewportEntry& VPEntry = ViewportEntries(RemoveIndex);

		AChaseCameraController* ChasePlayerController = Cast<AChaseCameraController>( VPEntry.PlayerController );
		if( ChasePlayerController )
		{
			// remove the dummy controller if one was created
			Level->DestroyActor( ChasePlayerController );
		}
		VPEntry.PlayerController = NULL;
		VPEntry.ViewActor = NULL;

		UViewport* Viewport = Cast<UViewport>( VPEntry.Viewport );
		if( Viewport )
		{
			// remove console we created
			if( Viewport->Console )
			{
				Viewport->Console->ViewportOwner = NULL;
				Client->InteractionMaster->eventRemoveInteraction( Viewport->Console );
			}
			// set viewport actor to null since 
			// we know we didn't create one
			Viewport->Actor = NULL;
			// destroy the viewport
			delete Viewport;
		}
		VPEntry.Viewport = NULL;		
	}

	unguard;
}


/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

