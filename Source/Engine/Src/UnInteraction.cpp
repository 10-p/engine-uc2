/*=============================================================================
	UnInteraction.cpp: See .UC for for info
	Copyright 1997-2001 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Joe Wilcox
=============================================================================*/

#include "EnginePrivate.h"
#include "FContentManager.h"

IMPLEMENT_CLASS(UInteraction);
IMPLEMENT_CLASS(UConsole);
IMPLEMENT_CLASS(UBaseGUIController);
IMPLEMENT_CLASS(UAbstractUIXInteraction);

//
// Initialize the state system
//
void UInteraction::execInitialize( FFrame& Stack, RESULT_DECL )
{
	guard(UInteraction::_Init);

	P_FINISH;

	Initialize();
	unguardexec;
}

void UInteraction::Initialize()
{
	guard(UInteraction::Initialize);

	// Init scripting.
	InitExecution();

	// Signal the Initialized event in script

	eventInitialized();


	unguard;
}

void UInteraction::execConsoleCommand( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UInteraction::execConsoleCommand);

	P_GET_STR(S);
	P_FINISH;

	if (!Master)
	{
		warnf(TEXT("Attempt to execute a console command through an Interaction that is without a master"));
		return;
	}

	if (ViewportOwner)
		*(DWORD*)Result = Master->Exec( *S, *ViewportOwner);
	else
		*(DWORD*)Result = Master->Exec( *S, *Master->Client->Viewports(0));

	unguardexecSlow;
}

void  UInteraction::NativeMessage(const FString Msg, FLOAT MsgLife)
{
}

UBOOL UInteraction::NativeKeyType(BYTE& iKey, TCHAR Unicode )
{
	return false;
}

UBOOL UInteraction::NativeKeyEvent(BYTE& iKey, BYTE& State, FLOAT Delta )
{
	return false;
}

void  UInteraction::NativeTick(FLOAT DeltaTime)
{
}

void  UInteraction::NativePreRender(UCanvas* Canvas)
{
}

void  UInteraction::NativePostRender(UCanvas* Canvas)
{
}

// ===============================================================================================================
// The Base GUI Controller - Determine if we are on a console, and init the script side
// ===============================================================================================================

void UBaseGUIController::InitializeController()
{

#ifdef _XBOX
	bIsConsole = true;
	debugf(NAME_DevUI,TEXT("Console GUI System Initialized"));
#else
	bIsConsole = false;
	debugf(NAME_DevUI,TEXT("PC GUI System Initialized"));
#endif

	eventInitializeController();
}

void UBaseGUIController::execHasContent( FFrame& Stack, RESULT_DECL )
{
	guard(UBaseGUIController::execHasContent);

	P_GET_INT(Type);
	P_GET_STR(ContentId);
	P_FINISH;

	*(UBOOL*)Result = HasContent(Type, ContentId);
	unguardexec;
}

UBOOL UBaseGUIController::HasContent( INT ContentType, const FString& ContentId )
{
	guard(UBaseGUIController::HasContent);

	UBOOL bResult = 0;
	if ( ContentId.Len() == 0 )
	{
		debugf(TEXT("%s - No ContentId specified!"), __FUNC_NAME__);
	}
	else
	{
		FContentManager* ContentManager = FContentManager::GetInstance();
		switch ( ContentType )
		{
		case UCONST_CONTENT_Skin:		// alt skin
			bResult = ContentManager && ContentManager->IsActivated(ContentId);
			break;

		case UCONST_CONTENT_Map:		// map
			{
				if ( ContentId == TEXT("DM-EvilHands") || ContentId == TEXT("CTF-Monoxide") ||
					 ContentId == TEXT("DM-Sever") || ContentId == TEXT("OD-Ascension") )
				{
					bResult = ContentManager && ContentManager->IsActivated(ContentId);
				}
				else
				{
					bResult = true;
				}
//				FString MapFileName = FString::Printf(TEXT("..\\Maps\\%s.unr"), *ContentId);
//				bResult = GFileManager->FileSize(*MapFileName) != -1;
			}
			break;

		case UCONST_CONTENT_Mutator:		// mutator
			{
				INT pos = ContentId.InStr(TEXT("."));
				if ( pos == INDEX_NONE )
					bResult = true;
				else
				{
					FString MutatorPackageName = ContentId.Left(pos);
					if ( MutatorPackageName == TEXT("Engine") || MutatorPackageName == TEXT("UCMutators") )
					{
						bResult = true;
					}
					else
					{
						bResult = ContentManager && ContentManager->IsActivated(ContentId);
					}

				}
			}
			break;
		}
	}

	return bResult;
	unguard;
}

// ===============================================================================================================
// The AbstractUIXInteraction
// ===============================================================================================================
void UAbstractUIXInteraction::execDestroyed( FFrame& Stack, RESULT_DECL )
{
	guard(UAbstractUIXInteraction::execDestroyed);

	P_FINISH;

	DestroyUIX();

	unguardexec;
}

void UAbstractUIXInteraction::execStartSignIn( FFrame& Stack, RESULT_DECL )
{
	guard(UAbstractUIXInteraction::execStartSignIn);

	P_GET_UBOOL_OPTX(bSilent,0);
	P_FINISH;

	StartSignIn(bSilent);
	unguardexec;
}

void UAbstractUIXInteraction::execSignedIn( FFrame& Stack, RESULT_DECL )
{
	guard(UAbstractUIXInteraction::execSignedIn);

	P_FINISH;

	*(UBOOL*)Result = SignedIn();

	unguardexec;
}

void UAbstractUIXInteraction::execStartSignOut( FFrame& Stack, RESULT_DECL )
{
	guard(UAbstractUIXInteraction::execStartSignOut);

	P_FINISH;

	StartSignOut();

	unguardexec;
}

void UAbstractUIXInteraction::execGetGamerTag( FFrame& Stack, RESULT_DECL )
{
	guard(UAbstractUIXInteraction::execGetGamerTag);

	P_GET_INT(GamepadID);
	P_FINISH;

	GetGamerTag(GamepadID, *(FString*)Result);

	unguardexec;
}

void UAbstractUIXInteraction::execRegisterSession( FFrame& Stack, RESULT_DECL )
{
	guard(UAbstractUIXInteraction::execRegisterSession);

	P_FINISH;
	RegisterSession();

	unguardexec;
}

void UAbstractUIXInteraction::execUpdateSession( FFrame& Stack, RESULT_DECL )
{
	guard(UAbstractUIXInteraction::execUpdateSession);

	P_FINISH;
	UpdateSession();

	unguardexec;
}

void UAbstractUIXInteraction::execEndSession( FFrame& Stack, RESULT_DECL )
{
	guard(UAbstractUIXInteraction::execEndSession);

	P_GET_UBOOL(bBlocking);
	P_FINISH;

	EndSession(bBlocking);

	unguardexec;
}

void UAbstractUIXInteraction::execShowFriends( FFrame& Stack, RESULT_DECL )
{
	guard(UAbstractUIXInteraction::execShowFriends);

	P_GET_INT(GamepadID);
	P_FINISH;

	ShowFriends(GamepadID);

	unguardexec;
}

void UAbstractUIXInteraction::execShowPlayers( FFrame& Stack, RESULT_DECL )
{
	guard(UAbstractUIXInteraction::execShowPlayers);

	P_GET_INT(GamepadID);
	P_FINISH;

	ShowPlayers(GamepadID);

	unguardexec;
}

void UAbstractUIXInteraction::execUpdateOnlineState( FFrame& Stack, RESULT_DECL )
{
	guard(UAbstractUIXInteraction::execUpdateOnlineState);

	P_FINISH;

	UpdateOnlineState();

	unguardexec;
}

void UAbstractUIXInteraction::execIsPlayerTalking(FFrame& Stack, RESULT_DECL)
{
	guardSlow(UAbstractUIXInteraction::execIsPlayerTalking);

	P_GET_OBJECT(APlayerReplicationInfo, PRI);
	P_FINISH;

	*(UBOOL*)Result = IsPlayerTalking(PRI);

	unguardexecSlow;
}