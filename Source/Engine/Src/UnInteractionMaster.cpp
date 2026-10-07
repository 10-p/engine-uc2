/*=============================================================================
	UnInteractionMaster.cpp: See .UC for for info
	Copyright 1997-2001 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Joe Wilcox
=============================================================================*/

#include "EnginePrivate.h"

IMPLEMENT_CLASS(UInteractions);
IMPLEMENT_CLASS(UInteractionMaster);

// Exec - Allows other Interaction Objects to send console commands

UBOOL UInteractionMaster::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{

	guardSlow(UInteractionMaster::Exec);
	
	if ( (!Client) || (!Client->Viewports.Num()) )
		return false;

	return Client->Viewports(0)->Exec(Cmd, Ar);

	unguardSlow;

}

//  execTravel - Allows traveling to a new Map

void UInteractionMaster::execTravel(FFrame& Stack, RESULT_DECL )
{
	guardSlow(UInteractionMaster::execTravel);

	P_GET_STR(NewURL);
	P_FINISH;
	
	if ( (!Client) || (!Client->Viewports.Num()) )
		return;

	// Setup the Travel for the next tick

	Client->Viewports(0)->TravelURL	   = NewURL;
	Client->Viewports(0)->TravelType   = TRAVEL_Absolute;
	Client->Viewports(0)->bTravelItems = false;

	unguardexecSlow
}


// Displays the Copyright message

void UInteractionMaster::DisplayCopyright(void)
{
	guard(UninteractionMaster::DisplayCopyright);

    /* gam ---
	warnf(LocalizeGeneral("Engine",TEXT("Core")));
	warnf(LocalizeGeneral("Copyright",TEXT("Core")));
	warnf(TEXT(" "));
	warnf(TEXT(" "));
    --- gam */

	unguard;
}

UBaseGUIController* UInteractionMaster::GetGUIController( UViewport* InViewport )
{
	guard(UInteractionMaster::GetGUIController);

	UBaseGUIController* Result = NULL;
	for ( INT i = 0; i < GlobalInteractions.Num(); i++ )
	{
		Result = Cast<UBaseGUIController>(GlobalInteractions(i));
		if ( Result )
			break;
	}

	return Result;

	unguard;
}

// Process all InputEvents. 

int UInteractionMaster::MasterProcessKeyType(EInputKey iKey, TCHAR Unicode)
{

	guard(UInteractionMaster::MasterProcessKeyType);

	// Do all Globals first

	BYTE tiKey=iKey;

	if ( eventProcess_KeyType( GlobalInteractions, tiKey, FString::Printf(TEXT("%c"), Unicode) ) )
		return 1;
	

	// Do all Native Globals

	for (INT i=0; i < GlobalInteractions.Num(); i++)
	{
		if ( (GlobalInteractions(i)->bNativeEvents) && (GlobalInteractions(i)->bActive) )
		{
			if ( GlobalInteractions(i)->NativeKeyType(tiKey,Unicode) )
				return 1;
		}
	}

	// Then all Viewports

	for( INT i=0; i<Client->Viewports.Num(); i++ )
	{
		if ( eventProcess_KeyType( Client->Viewports(i)->LocalInteractions, tiKey, FString::Printf(TEXT("%c"), Unicode) ) )
			return 1;
	}

	return 0;

	unguard;

}

int UInteractionMaster::MasterProcessKeyEvent(EInputKey iKey, EInputAction State, FLOAT Delta )
{

	guard(UInteractionMaster::MasterProcessKeyEvent);

	// Do all Globals first	

	BYTE tiKey = iKey;
	BYTE tState = State;

	if ( eventProcess_KeyEvent( GlobalInteractions, tiKey, tState, Delta) )
			return 1;

	// Do all Native Globals

	for (INT i=0; i < GlobalInteractions.Num(); i++)
	{
		if ( (GlobalInteractions(i)->bNativeEvents) && (GlobalInteractions(i)->bActive) )
		{
			if ( GlobalInteractions(i)->NativeKeyEvent(tiKey, tState, Delta) )
				return 1;
		}
	}

	// Then all Viewports

	UBOOL Success = 0;
	for( INT i=0; i<Client->Viewports.Num(); i++ )
	{
		if ( eventProcess_KeyEvent( Client->Viewports(i)->LocalInteractions, tiKey, tState, Delta) )
			Success = 1;
	}
	return Success;
	
	unguard;	
}

int UInteractionMaster::MasterProcessJoyInput(INT ControlIndex, EInputKey iKey, FLOAT Axis)
{
	return MasterProcessKeyEvent(iKey, IST_Axis, Axis);
}


void UInteractionMaster::MasterProcessTick(float DeltaTime)
{
	guard(UInteractionMaster::MasterProcessTick);
	
	// Do all Globals first
	
	eventProcess_Tick( GlobalInteractions, DeltaTime );
	
	// Do all Native Globals

	for (INT i=0; i < GlobalInteractions.Num(); i++)
	{
		if ( (GlobalInteractions(i)->bNativeEvents) && (GlobalInteractions(i)->bRequiresTick) )
		{
			GlobalInteractions(i)->NativeTick(DeltaTime);
		}
	}

	// Then all Viewports

	for( INT i=0; i<Client->Viewports.Num(); i++ )
		eventProcess_Tick( Client->Viewports(i)->LocalInteractions, DeltaTime );


	unguard;	
}



void UInteractionMaster::MasterProcessPreRender(UViewport* Viewport)
{
	guard(UInteractionMaster::MasterProcessPreRender);
	checkSlow(Viewport);
	checkSlow(Viewport->Canvas);

	// skip rendering of menu/interaction items if needed
	if( !Viewport->bShowMenu )
		return;

	UCanvas* Canvas = Viewport->Canvas;
		
	// only need to process the currently active viewport 
	eventProcess_PreRender( Viewport->LocalInteractions, Canvas  );

	// Do all Native Globals

	INT i = GlobalInteractions.Num();
	while (i != 0) 
	{
		--i;
		if ( (GlobalInteractions(i)->bNativeEvents) && (GlobalInteractions(i)->bVisible) )
		{
			GlobalInteractions(i)->NativePreRender(Canvas);
		}
	}

	eventProcess_PreRender( GlobalInteractions, Canvas );

	unguard;	
} // MasterProcessPreRender

void UInteractionMaster::MasterProcessPostRender(UViewport* Viewport)
{
	guard(UInteractionMaster::MasterProcessPostRender);
	checkSlow(Viewport);
	checkSlow(Viewport->Canvas);

	// skip rendering of menu/interaction items if needed
	if( !Viewport->bShowMenu )
		return;
	
	UCanvas* Canvas = Viewport->Canvas;

	// Do all Natives

	// Remember: Input is processed from front to back of the Interaction chain and output is
	// processed from back to front. This lets the first element in the chain "hijack" the whole 
	// I/O process
	INT i = GlobalInteractions.Num();
	while (i != 0) 
	{
		--i;
		if ( (GlobalInteractions(i)->bNativeEvents) && (GlobalInteractions(i)->bVisible) )
		{
			GlobalInteractions(i)->NativePostRender(Canvas);
		}
	}
	// D-2: the global interactions (the console's typing line, the GUI) are UI: clipped to the UI's box as the
	// Xbox's 640x480 screen clipped them (the console's line starts at x=32 and is 640 wide); UnCanvas.cpp
	Canvas->SetUIContext( UICONTEXT_Menu );
	eventProcess_PostRender( GlobalInteractions, Canvas );
	Canvas->SetUIContext( UICONTEXT_Game );

	// only need to process the currently active viewport 
	eventProcess_PostRender( Viewport->LocalInteractions, Canvas  );

	unguard;	
} // MasterProcessPostRender

void UInteractionMaster::MasterProcessMessage(const FString& Msg, FLOAT MsgLife)
{
	guard(UInteractionMaster::MasterProcessMessage);
	
	// Do all Globals first
	
	eventProcess_Message( Msg, MsgLife, GlobalInteractions);
	
	// Then all Viewports

	for( INT i=0; i<Client->Viewports.Num(); i++ )
		eventProcess_Message( Msg, MsgLife, Client->Viewports(i)->LocalInteractions );

	// Handle Natives

	for (INT i=0; i < GlobalInteractions.Num(); i++)
	{
		if (GlobalInteractions(i)->bNativeEvents) 
		{
			GlobalInteractions(i)->NativeMessage(Msg, MsgLife);
		}
	}

	unguard;	
}

