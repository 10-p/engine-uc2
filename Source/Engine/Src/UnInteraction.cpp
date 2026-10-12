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

	// ufront "Unlock all content" (owner, 2026-10-12): [GUI.GUIController] bUnlockAll=True in the System ini (Avaris.ini;
	// the launcher's button writes it) opens what the menus keep locked. The script above has just cleared
	// bUnlockCharacters and bUnlockRaiden (Epic's title-update fix in InitializeController, so the PC community's
	// bUnlockCharacters=True never works); Epic's own menu flags are set after it, in memory only: nothing saves this
	// object's config. bUnlockCharacters: every character, bot and alternate skin; bUnlockRaiden; bShowAllMuts: every
	// mutator; bLaddersLocked=False: every ladder, rung and challenge and the Extras cinematics. The profile is never
	// touched: what the player wins is recorded as always. docs/porting/analysis/unlock-all.md.
	UBOOL UnlockAll = 0;
	if( GConfig->GetBool( GetClass()->GetPathName(), TEXT("bUnlockAll"), UnlockAll, GIni ) && UnlockAll )
	{
		static const struct { const TCHAR* Name; UBOOL Value; } Flags[] =
		{
			{ TEXT("bUnlockCharacters"), 1 },
			{ TEXT("bUnlockRaiden"),     1 },
			{ TEXT("bShowAllMuts"),      1 },
			{ TEXT("bLaddersLocked"),    0 },
		};
		INT Set = 0;
		for( INT i=0; i<(INT)ARRAY_COUNT(Flags); i++ )
		{
			UBoolProperty* Prop = FindField<UBoolProperty>( GetClass(), Flags[i].Name );
			if( !Prop )
				continue;
			BITFIELD* Bits = (BITFIELD*)((BYTE*)this + Prop->Offset);
			*Bits = Flags[i].Value ? (*Bits | Prop->BitMask) : (*Bits & ~Prop->BitMask);
			Set++;
		}
		debugf( TEXT("Unlock all: every character, bot, mutator, ladder and cinematic is open (%s bUnlockAll=True, %i of %i menu flags set)"),
			GetClass()->GetPathName(), Set, (INT)ARRAY_COUNT(Flags) );
	}
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
#if __PSX2_EE__ || __GCN__ || __LINUX__
		// guard() does not declare __FUNC_NAME__ on these platforms (same test as Core/Inc/UnFile.h).
		debugf(TEXT("%s - No ContentId specified!"), TEXT("UBaseGUIController::HasContent"));
#else
		debugf(TEXT("%s - No ContentId specified!"), __FUNC_NAME__);
#endif
	}
	else
	{
#ifndef _XBOX
		// ufront (owner, 2026-10-12): there is no Xbox Live off the Xbox, so nothing ever activates its downloadable
		// content (FContentManager::Initialize and the activation files are Xbox-only: IsActivated is always false here).
		// The ten alternate skins and the four maps (DM-EvilHands, CTF-Monoxide, DM-Sever, OD-Ascension) are in the game
		// folder like everything else: what is installed is available, as on an Xbox that had downloaded it.
		bResult = 1;
#else
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
#endif
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