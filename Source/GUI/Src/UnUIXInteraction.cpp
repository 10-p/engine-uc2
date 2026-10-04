#include "UnIpDrv.h"
#include "UnUIXInteraction.h"

#define STAT_QUERY_DELAY_SECONDS 1.5f

#ifdef _XBOX
#include "XboxDrv.h"
#include "XboxLive.h"
#include "XHVVoiceSubsystem.h"
#include "AvarisPlayerItem.h"
#include "UCGameplay.h"
#include "FContentManager.h"

// Need access to the raw gamepad input for passing to the UIX system.
extern FXboxGamepadState Gamepads[4];

#ifdef FINAL_RELEASE
#define ShowWorkFlag(flags)
#define ShowScreenName(name)
#define CHECKFLAG(state,flag,c)
#else // FINAL_RELEASE

#define SHOWCASE(x) case x: debugf(TEXT("UIX Screen Now %s"), TEXT(#x)); break;
void AddFlag( TCHAR* Text, TCHAR* NewFlag )
{
	if ( *Text )
		appStrcat(Text, TEXT(","));

	appStrcat(Text, NewFlag);
}

void LogWorkFlagText( DWORD Flags )
{
	TCHAR* Result = appStaticString1024();
	*Result = 0;

	//	if ( Flags & UIX_DOWORK_NEED_TO_RENDER )
	//		AddFlag(Result, TEXT("UIX_DOWORK_NEED_TO_RENDER"));

	if ( Flags & UIX_DOWORK_NOTIFICATIONS )
		AddFlag(Result,TEXT("UIX_DOWORK_NOTIFICATIONS"));

	if ( Flags & UIX_DOWORK_NEED_TO_REBOOT )
		AddFlag(Result,TEXT("UIX_DOWORK_NEED_TO_REBOOT"));

	if ( Flags & UIX_DOWORK_FEATURE_EXIT )
		AddFlag(Result,TEXT("UIX_DOWORK_FEATURE_EXIT"));

	//	if ( Flags & UIX_DOWORK_PROCESSING_INPUT )
	//		AddFlag(Result,TEXT("UIX_DOWORK_PROCESSING_INPUT"));

	if ( *Result )
		debugf(NAME_DevLive, TEXT("DoWork: %s"), Result);
}

void LogScreenName( WHICH_SCREEN screen )
{
	switch ( screen )
	{
		SHOWCASE(SCREEN_NOT_UIX)
			SHOWCASE(SCREEN_INTRO)
			SHOWCASE(SCREEN_MAIN)
			SHOWCASE(SCREEN_LIVE)
			SHOWCASE(SCREEN_LOGON)
			SHOWCASE(SCREEN_LOBBY)
			SHOWCASE(SCREEN_FRIENDS)
			SHOWCASE(SCREEN_MYTEAM_ONLY)
			SHOWCASE(SCREEN_GUEST_VIEW)
			SHOWCASE(SCREEN_PLAYERS)
			SHOWCASE(SCREEN_DEPARTED_PLAYERS_ONLY)
			SHOWCASE(SCREEN_OPTIONS_OVERLAY)
			SHOWCASE(SCREEN_COUNT)
			SHOWCASE(SCREEN_FORCE32)
	}
}

#define ShowWorkFlag(flags) LogWorkFlagText(flags)
#define ShowScreenName(screen) LogScreenName(screen)
#endif	// FINAL_RELEASE
#endif	// _XBOX


/* noexport -> nobody adds our native functions to the export list for the DLL;
IMPLEMENT_FUNCTION does this for each function. 
*/
IMPLEMENT_FUNCTION(UUIXInteraction,-1,execGetUIXInteraction);
IMPLEMENT_FUNCTION(UUIXInteraction,-1,execfindGUIController);
IMPLEMENT_FUNCTION(UUIXInteraction,-1,execEndFeature);
IMPLEMENT_FUNCTION(UUIXInteraction,-1,execRebootXbox);
IMPLEMENT_FUNCTION(UUIXInteraction,-1,execStartDownloader);
IMPLEMENT_FUNCTION(UUIXInteraction,-1,execDebugIconFade);

IMPLEMENT_FUNCTION(UUIXInteraction,-1,execActivateFriendsList);
IMPLEMENT_FUNCTION(UUIXInteraction,-1,execDeactivateFriendsList);

IMPLEMENT_FUNCTION(UUIXInteraction,-1,execFriendsListReady);
IMPLEMENT_FUNCTION(UUIXInteraction,-1,execFriendsListChanged);
IMPLEMENT_FUNCTION(UUIXInteraction,-1,execRefreshFriendsList);

IMPLEMENT_FUNCTION(UUIXInteraction,-1,execGetFriendsList);
IMPLEMENT_FUNCTION(UUIXInteraction,-1,execIsAFriend);
IMPLEMENT_FUNCTION(UUIXInteraction,-1,execQueryFullStats);
IMPLEMENT_FUNCTION(UUIXInteraction,-1,execKillStatsQuery);

IMPLEMENT_CLASS(UUIXInteraction);

// ----- Static member ------
UUIXInteraction * UUIXInteraction::theOnlyUIX = NULL;
UGUIController*	UUIXInteraction::staticGUIController = NULL;

TArray<AvarisPlayerItem *> UUIXInteraction::RegisteredPlayers;

#define UIX_HANDLES_NOTIFICATION 0

#ifdef _XBOX
// This is the X-Title invite (if there is one)

struct FJoinGameInfo
{
	FString JoiningPlayerGamertag;
	UBOOL bInvited;
	XONLINE_ACCEPTED_GAMEINVITE InviteInfo;
} crosstitle_invite;

XONLINE_MUTELISTUSER UUIXInteraction::CachedMuteList[MAX_MUTELISTUSERS];
#endif

//
// Initialize the state system
//
void UUIXInteraction::Initialize()
{
	guard(UUIXInteraction::Initialize);

	Super::Initialize();

	// scion sz - obj persists across level loads
	SetFlags( RF_TagPersist );

	theOnlyUIX = this;
	UAbstractUIXInteraction::UIXInteractionSingleton = this;
#ifdef _XBOX
	// start up the Xbox Live library
	HRESULT hr = InitXNet();
	if ( FAILED( hr ) )
	{
		warnf( NAME_Error, TEXT("XOnlineStartup failed (0x%x)"), hr );
		return;
	}

	appMemzero(CachedMuteList, sizeof(XONLINE_MUTELISTUSER) * MAX_MUTELISTUSERS);
#endif

	unguard;
}


UBOOL UUIXInteraction::IsNetworkActive()
{
	guard(UUIXInteraction::IsNetworkActive);

#ifdef _XBOX
	return XNetGetEthernetLinkStatus() != 0;
#else
	return true;
#endif

	unguard;
}

void UUIXInteraction::InitUIXVoiceSupport()
{
	guard(UUIXInteraction::InitUIXVoiceSupport);

#ifdef _XBOX
	// Enable support for voice attachments.
	XHVEngine* pXHVEngine = UXHVVoiceSubsystem::getUXHVVoiceSubsystem()->GetXHVEngine();
	if (pXHVEngine != NULL)
	{
		debugf(NAME_DevLive, TEXT("UUIXInteraction::InitUIXVoiceSupport () -- Succesfully initialized."));
		m_pLiveEngine->UseVoiceMail(UIX_VOICE_MAIL);
		m_pLiveEngine->SetProperty(UIX_PROPERTY_VOICE_MAIL_ENGINE, (DWORD)pXHVEngine);
		m_pLiveEngine->SetProperty(UIX_PROPERTY_VOICE_MAIL_TO_SPEAKERS, TRUE);
	}
	else
	{
		debugf(NAME_DevLive, TEXT("UUIXInteraction::InitUIXVoiceSupport() -- Failed to initialize since XHV has not been initialized."));
	}
#endif // _XBOX

	unguard;
}

void UUIXInteraction::TickUIX()
{
	guard(UUIXInteraction::TickUIX);
#ifdef _XBOX

	UpdateNetworkStatus();

	ProcessCrossTitleInvites();
	if (m_RegisterCountdown)
	{
		--m_RegisterCountdown;
		if (!m_RegisterCountdown)
			LiveInterface->StartNewSession();
	}

	if (LiveInterface)
		LiveInterface->Tick();

	UpdateUIXScreen();

	// ----------------------------------------------------
	if (!m_pLiveEngine) 
		return;
	// ----------------------------------------------------

	if ( ShouldRenderUIX() )
		UpdateGamepadStates();

	// Updated the allowing of invites in the friends feature; if this is too expensive it could be changed
	// to be event driven rather than polled
	UpdateGameInviteStatus();

	// Let the UIX engine do work. This also sets m_dwLiveWorkFlags
	// based on what UIX is doing, if anything.
	// This should be called every frame, even when UIX is dormant,
	// because UIX still watches for unexpected logoffs.
	m_pLiveEngine->DoWork( &m_dwLiveWorkFlags );
	ShowWorkFlag(m_dwLiveWorkFlags);

	ProcessLiveWorkTasks();

	if ( bStateChanged )
		UpdateOnlineState();

#endif
	unguard;
}

void UUIXInteraction::HandleFeatureExit()
{
	guard(UUIXInteraction::HandleFeatureExit);

#ifdef _XBOX
	// exitInfo contains FeatureID, ExitCode, hr, and pExitData
	UIX_EXIT_INFO exitInfo;
	m_pLiveEngine->GetExitInfo( &exitInfo );

	switch ( exitInfo.ExitCode )
	{
	case UIX_EXIT_NONE:
		// this can occur if the login feature is active, and UIX requests to reboot.  At this point, we
		// display a dialog to allow the user to confirm that they wish to leave the game.  If the user
		// cancels, we call Logoff() on the live engine, which results in a UIX_EXIT_NONE exit code.
		m_pLiveEngine->SetProperty( UIX_PROPERTY_DISPLAY_CONNECTION_ERRORS, FALSE );
		SetOnlineState(ONLINE_None);
		m_WhichScreen = SCREEN_NOT_UIX;
		break;

	case UIX_EXIT_LOGON_SUCCESSFUL:
		// we don't want UIX to handle messages like log off due to duplicate login, network
		// connection lost, etc. because we don't receive notifications in these cases and I need to disable
		// attract mode for this. therefore, we'll only enable connection errors while we're actually 
		// attempting to log-in.
		m_pLiveEngine->SetProperty( UIX_PROPERTY_DISPLAY_CONNECTION_ERRORS, FALSE );
		HandleLoginSuccess();
		break;

	case UIX_EXIT_LOGON_FAILED:
		// we don't want UIX to handle messages like log off due to duplicate login, network
		// connection lost, etc. because we don't receive notifications in these cases and I need to disable
		// attract mode for this. therefore, we'll only enable connection errors while we're actually 
		// attempting to log-in.
		m_pLiveEngine->SetProperty( UIX_PROPERTY_DISPLAY_CONNECTION_ERRORS, FALSE );

		if ( OnlineState == ONLINE_Pending || OnlineState == ONLINE_Success )
		{
			HandleLoginFailure(exitInfo.hr);
			break;
		}
		else
		{

			// if the OnlineState is Failed, this means that there was some problem logging into Xbox live, and if this isn't a silent sign-in,
			// the user selected to fix it (start troubleshooter, whatever), but cancelled when they received
			// the 'loss of game state' confirmation dialog.  In this situation, we should behave as though they had
			// cancelled the login dialog.
			if ( !LiveInterface->bSilentSignin )
			{
				SetOnlineState(ONLINE_None);
				Reset();
			}
			break;
		}

		// This means the user backed out of the logon screen.
	case UIX_EXIT_LOGON_USER_EXIT:
		m_pLiveEngine->SetProperty( UIX_PROPERTY_DISPLAY_CONNECTION_ERRORS, FALSE );

		SetOnlineState(ONLINE_None);
		m_WhichScreen = SCREEN_NOT_UIX;
		break;

		// Friends feature exit codes:
		// This means the user backed out of the friends feature.
	case UIX_EXIT_FRIENDS_NORMAL_EXIT:
		m_WhichScreen = SCREEN_NOT_UIX;
		break;

		// Accept invitation or choose to join a friend
	case UIX_EXIT_FRIENDS_JOIN_GAME:
		{
			// Get information about the friend that invited us to join their game.
			XONLINE_FRIEND* pFriendData = ( XONLINE_FRIEND*)exitInfo.pExitData;
			BOOL bInvited = (pFriendData->dwFriendState&XONLINE_FRIENDSTATE_FLAG_RECEIVEDINVITE) != 0;

#ifndef FINAL_RELEASE
			debugf(NAME_DevLive, TEXT("JOIN_FRIEND Invited:%s Flags:%s"), bInvited ? GTrue : GFalse, GetStateFlagText(pFriendData->dwFriendState));
#endif

			// first, make sure we're not trying to join the session we're currently in.
			if ( IsCurrentSession(pFriendData->sessionID) )
			{
				m_WhichScreen = SCREEN_NOT_UIX;
				eventShowMessage(Localize(TEXT("UIX"), TEXT("LiveJoinOwnSession"), TEXT("Engine")), UIX_MESSAGEMENU_PRIORITY);
				break;
			}

			// If the player chooses "Join Friend" from their friends list, don't consider them "invited" to the match
			LiveInterface->StartFindFromIDQuery(pFriendData->sessionID, bInvited);

			UGameStartInfoBase* GSI = UGameStartInfoBase::GetGameStart();
			check(GSI);

			GSI->eventSetURLKeyValue(0, TEXT("XboxLiveClient"), GTrue);
			m_WhichScreen = SCREEN_NOT_UIX;
			break;
		}

	case UIX_EXIT_FRIENDS_JOIN_GAME_CROSS_TITLE:
		{
			// This means the user accepted an invite to join another
			// title. They need to switch DVDs now. It is all handled inside UIX!
			debugf(NAME_DevLive, TEXT("JOIN_GAME_CROSS_TITLE") );
			m_WhichScreen = SCREEN_NOT_UIX;
			break;
		}
	case UIX_EXIT_FRIENDS_SIGNED_OUT: 
		{
			// This message is received when the user logs out from the
			// friends menu. SHOULD NEVER HAPPEN!
			debugf(NAME_DevLive, TEXT("FRIENDS_SIGNED_OUT") );
			SetOnlineState(ONLINE_None);
			Reset();
			m_WhichScreen = SCREEN_NOT_UIX;
			break;
		}

	case UIX_EXIT_PLAYERS_NORMAL_EXIT:
		{

			XboxLiveManager* Manager = XboxLiveManager::getXboxLiveManager();
			if (Manager != NULL)
			{
				Manager->StartMuteListQuery();
			}

			m_WhichScreen = SCREEN_NOT_UIX;
			break;
		}

	default:
		{

			appErrorf(TEXT("Unhandled Feature Exit Code 0x%x"), exitInfo.ExitCode);
		}
	}

	ConfirmationMessage = TEXT("");
	if (m_WhichScreen == SCREEN_NOT_UIX)
	{
		eventCloseUIXMenu(0);
	}

#endif
	unguard;
} // HandleFeatureExit

//-----------------------------------------------------------------------------
// Name: Reset()
// Desc: Reset to a known-good state. Logged off and at the main menu.
//-----------------------------------------------------------------------------
void UUIXInteraction::Reset()
{
	guard(UUIXInteraction::Reset);
#ifdef _XBOX
	m_WhichScreen = SCREEN_NOT_UIX;
	UIXState = 0;
	ConfirmationMessage = TEXT("");

	// clear any pending invite flags
	SetPendingGameInvite(false);
	SetPendingFriendRequest(false);

	UGUIController* gui = findGUIController();
	if ( gui && gui->bActive )
		gui->eventNotifyInviteReceived(INVITE_Clear);

	if ( StatCache )
	{
		if ( ViewportOwner && ViewportOwner->Actor && ViewportOwner->Actor->PlayerReplicationInfo )
		{
			if ( ViewportOwner->Actor->PlayerReplicationInfo->StatCache == StatCache )
				ViewportOwner->Actor->PlayerReplicationInfo->StatCache = NULL;
		}

		delete StatCache;
		StatCache = NULL;
	}

	CleanupStatsQuery();
	if ( m_pLiveEngine )
	{
		// if we have any players in our UIX player list, unregister them now
		if ( RegisteredPlayers.Num() )
		{
			ILivePlayersList* pPlayers;
			if ( SUCCEEDED(m_pLiveEngine->GetFeatureInterface(UIX_PLAYERS_FEATURE, NULL, (LPVOID*)&pPlayers)) )
			{
				for ( INT i = 0; i < RegisteredPlayers.Num(); i++ )
				{
					AvarisPlayerItem* Player = RegisteredPlayers(i);
					if ( Player )
					{
						pPlayers->UnregisterPlayer(Player);
						delete Player;
					}
				}
			}

			RegisteredPlayers.Empty();
		}

		UXHVVoiceSubsystem* Voice = Cast<UXHVVoiceSubsystem>(UEngine::GEngine->Voice);
		if ( Voice && Voice->Initialized() )
		{
			m_pLiveEngine->SetProperty(UIX_PROPERTY_VOICE_MAIL_ENGINE, NULL);
			if ( !Voice->Shutdown() )
			{
				debugf(NAME_DevLive, TEXT("ERROR ENCOUNTERED WHILE SHUTTING DOWN XHV!"));
			}
		}

		for( DWORD i = 0; i < XONLINE_MAX_LOGON_USERS; ++i )
		{
			XboxLiveUser* User = LiveInterface->GetUser(i);
			if ( User && User->Valid() )
				m_pLiveEngine->NotificationSetState(User->GetPort(), 0, XNKID(), 0, NULL);
		}

		m_pLiveEngine->LogOff();
	}

	LiveInterface->clearAllUsers();
#endif
	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::IsSupportedLanguage
Desc:		Returns whether the specified language is supported.

input:		XLanguage
				use the result of XGetLanguage()

ouput:		XLanguage
				if the specified language is unsupported, XLanguage will be set to the default language
				(usually english)

last modified by:	Ron Prestenback
==========================================================================================================*/
UBOOL UUIXInteraction::IsSupportedLanguage( DWORD& XLanguage ) const 
{
	guard(UUIXInteraction::IsSupportedLanguage);

#ifdef _XBOX
	switch ( XLanguage )
	{
	case XC_LANGUAGE_GERMAN:
	case XC_LANGUAGE_FRENCH:
	case XC_LANGUAGE_SPANISH:
	case XC_LANGUAGE_ITALIAN:
	case XC_LANGUAGE_ENGLISH:
		return true;

	case XC_LANGUAGE_JAPANESE:
	case XC_LANGUAGE_KOREAN:
	case XC_LANGUAGE_TCHINESE:
	case XC_LANGUAGE_PORTUGUESE:
		XLanguage = XC_LANGUAGE_ENGLISH;
		return false;
	}
#endif

	return true;

	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::SetPendingGameInvite
Desc:		Sets the value of the pending game invite flag.  When the flag is first set to true, sets up the
			fade animations for the game invite image.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::SetPendingGameInvite( UBOOL bValue ) 
{
	guard(UUIXInteraction::SetPendingGameInvite);

	if ( bValue != bPendingGameInvite )
	{
		// just received a new game invite - setup the fade animations for the invite icon
		if ( bValue )
		{
			InitializeIconDisplay(GameInviteIcon);
		}
		else
		{
			// game invite flag was just unset - kill any animations in the invite icon, then hide it
			GameInviteIcon->eventKillAnimation(0, false);
			GameInviteIcon->eventClearAnimationLoop(0);
			GameInviteIcon->ComponentColor.A = 0;
			GameInviteIcon->TimerCountdown = 0;
		}
	}

	bPendingGameInvite = bValue;

	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::SetPendingFriendRequest
Desc:		Sets the value of the pending friend request flag.  When the flag is first set to true, sets up the
			fade animations for the friend request image.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::SetPendingFriendRequest( UBOOL bValue ) 
{
	guard(UUIXInteraction::SetPendingFriendRequest);

	if ( bValue != bPendingFriendInvite )
	{
		if ( bValue )
			InitializeIconDisplay(FriendInviteIcon);
		else
		{
			FriendInviteIcon->eventKillAnimation(0,false);
			FriendInviteIcon->eventClearAnimationLoop(0);
			FriendInviteIcon->ComponentColor.A = 0;
			FriendInviteIcon->TimerCountdown = 0;
		}
	}

	bPendingFriendInvite = bValue;
	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::InitializeIconDisplay
Desc:		Initializes the fade animations for a notification icon.

input:		Image
				the image to initialize - would be either the gameinvite icon or friend request icon

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::InitializeIconDisplay( UGUIImage* Image ) 
{
	guard(UUIXInteraction::InitializeIconDisplay);

	check(Image);

	// first kill any existing animations
	Image->eventKillAnimation(0, false);

	// also kill any existing loops
	Image->eventClearAnimationLoop(0);

	// bring the icon to full color
	Image->ComponentColor.A = 255;

	// setup the fade animation
	// show the image at full alpha for a few seconds
	Image->eventPulse(Image->ComponentColor, IconDisplaySeconds, 0, 1.0);
	Image->eventPulseB(255,255,255,0,IconFadeSeconds,0,IconFadeMultiplier);

	// set the seconds for this image to be shown again
	Image->TimerCountdown = IconRepeatSeconds;

	unguard;
}

/* epic ====================================================================================================
* Function:	UUIXInteraction::StartDownloader
* Author:	Per Vognsen
*
* Starts the downloadable content manager.
* ==========================================================================================================
*/
void UUIXInteraction::StartDownloader()
{
	guard(UUIXInteraction::StartDownloader);

#ifdef _XBOX
	FContentManager* ContentManager = FContentManager::GetInstance();
	if (ContentManager != NULL)
	{
		ContentManager->StartDownloader(getGamepadPort());
	}
#endif

	unguard;
}

#ifdef _XBOX
/*==========================================================================================================
function	UUIXInteraction::IsCurrentSession
Desc:		Determines whether we're currently playing in a match that has the specified SessionID.

input:		SessionID
				the XNKID for the session to check


notes:		I suppose this function should really be in UTcpNetDriverXbox, for better encapsulation....
last modified by:	Ron Prestenback
==========================================================================================================*/
UBOOL UUIXInteraction::IsCurrentSession( const XNKID& TestID ) const
{
	guard(UUIXInteraction::IsCurrentSession);

	UBOOL bResult = false;
	if ( LiveInterface != NULL )
	{
		UTcpNetDriverXbox* net = LiveInterface->getNet();
		if ( net )
		{
			XNKID SessionID;
			appMemzero( &SessionID, sizeof(XNKID) );

			// if we're a client
			UTcpNetDriverXbox::FSessionInfo* SessionInfo = net->GetRemoteInfo();
			if( SessionInfo )
				SessionID = SessionInfo->SessionID;
			else
			{
				// if we're a server
				SessionInfo = net->GetHostInfo();
				if ( SessionInfo )
				{
					SessionID = SessionInfo->SessionID;
				}
			}

			bResult = !appMemcmp( &TestID, &SessionID, sizeof(XNKID) );
		}
	}

	return bResult;

	unguard;
}


/*==========================================================================================================
function	UUIXInteraction::RebootXbox
Desc:		Restarts the xbox.

input:		Reason
				the reason for rebooting the xbox (i.e. where should we reboot to)

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::RebootXbox( ELaunchDestination Reason )
{
	guard(UUIXInteraction::RebootXbox);

	LD_LAUNCH_DASHBOARD LaunchData;
	appMemzero( &LaunchData, sizeof(LD_LAUNCH_DASHBOARD) );

	switch ( Reason )
	{
	case LAUNCH_AccountManagement:
		// we do not want to logout of xbox live if we're going to account management
		LaunchData.dwReason = XLD_LAUNCH_DASHBOARD_ACCOUNT_MANAGEMENT;
		break;

	case LAUNCH_NetworkTroubleshooter:
		SetOnlineState(ONLINE_None);
		Reset();

		LaunchData.dwReason = XLD_LAUNCH_DASHBOARD_NETWORK_CONFIGURATION;
		break;

	case LAUNCH_Memory:
		{
			SetOnlineState(ONLINE_None);
			Reset();

			// If the player chose to go to the Dash, go to memory area
			LaunchData.dwReason = XLD_LAUNCH_DASHBOARD_MEMORY;

			// This value will be returned to the title via XGetLaunchInfo
			// in the LD_FROM_DASHBOARD struct when the Dashboard reboots
			// into the title. If not required, set to zero.
			LaunchData.dwContext = 0;

			// Specify the logical drive letter of the region where
			// data needs to be removed; either T or U.
			LaunchData.dwParameter1 = (DWORD) 'U';

			// Specify the number of 16-KB blocks that need to be freed
			ULoadSaveManager* LoadSaveManager = CastChecked<ULoadSaveManager>(GLoadSaveManager);
			DWORD BlocksNeeded = LoadSaveManager->GetMaxSaveGameSize(true);
			LaunchData.dwParameter2 = BlocksNeeded;
		}
		break;

	case LAUNCH_NewAccount:
		LaunchData.dwReason = XLD_LAUNCH_DASHBOARD_NEW_ACCOUNT_SIGNUP;
		break;

	case LAUNCH_UIX:
		if ( m_pLiveEngine )
		{
			m_pLiveEngine->Reboot(0);
			return;
		}
	}

	XLaunchNewImage( NULL, (PLAUNCH_DATA)(&LaunchData));
	unguard;
}


/*==========================================================================================================
function	UUIXInteraction::UpdateNetworkStatus
Desc:		Determines the current connection state, and notifies the GUI is network status has changed.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::UpdateNetworkStatus()
{
	guard(UUIXInteraction::UpdateNetworkStatus);

	FLOAT CurrentTime = FLOAT(appSeconds());
	// Check the network status every half second.
	if (CurrentTime - LastNetworkCheck >= 0.5f)
	{
		UBOOL bNetworkActiveNow = IsNetworkActive();
		// Network status has changed.
		if (bNetworkActiveNow != bNetworkActive)
		{
			bNetworkActive = bNetworkActiveNow;
			UGUIController* GUI = findGUIController();
			// Notify the active GUI page of the change in network status.
			if ( GUI )
			{
				/*
				to have servers/clients immediately travel back to menu, uncomment this block
				UGameEngine* Game = Cast<UGameEngine>(UEngine::GEngine);
				ALevelInfo* LI = Game && Game->GLevel ? Game->GLevel->GetLevelInfo() : NULL;

				// if our line was disconnected, and we are a client or server,
				// return the to menu, since the connection cannot be recovered.
				if ( !bNetworkActive && LI->NetMode != NM_Standalone )
				{
					FURL URL;
					URL.AddOption(TEXT("failed"));
					URL.AddOption(TEXT("disconnect"));

					FString BrowseError;
					Game->Browse(URL, NULL, BrowseError);
				}
				// if we were disconnected from a network game (whether client or server),
				// the GUI should always be active at this point, since we should be back
				// in the menus by now
				*/

				// Notify the GUI system about this event
				GUI->eventNotifyNetworkStatusChanged(bNetworkActive);
			}
		}
	}

	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::ProcessCrossTitleInvites
Desc:		Determines whether we have any cross-title invites pending confirmation, and if so, 
			sends a message to the user to allow them to respond.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::ProcessCrossTitleInvites()
{
	guard(UUIXInteraction::ProcessCrossTitleInvites);

	// wait until the user passes the IIS.
	if ( getGamepadPort() == UViewport::INVALID_GAMEPAD_ID )
		return;

	switch ( m_CTIState )
	{
	case CTI_MustConfirm:
		{
			UGUIController* gui = findGUIController();
			if (gui)
			{
				m_CTIState = CTI_Confirming;

				// get the gamer tag for the player that accepted the invite.
				FString HostGamertag = ANSI_TO_TCHAR(crosstitle_invite.InviteInfo.InvitingFriend.szGamertag);

				// determine whether the player invited us to join their match, or if we chose to join the match using our friend's list.
				FILETIME ZeroTime = {0};
				crosstitle_invite.bInvited = appMemcmp(&crosstitle_invite.InviteInfo.InvitingFriend.gameinviteTime, &ZeroTime, sizeof(FILETIME));


				// now get the gamertag for the account we need to sign-in as in order to join this friend.
				XONLINE_USER LatestUsers[XONLINE_MAX_STORED_ONLINE_USERS];
				appMemzero(LatestUsers, sizeof(LatestUsers));

				DWORD NumUsers;
				HRESULT result = XOnlineGetUsers(LatestUsers, &NumUsers);
				if ( FAILED(result) )
				{
					debugf(NAME_DevLive, TEXT("XOnlineUsers failed in ProcessCrossTitleInvites with error code 0xX (%s)"), result, ParseXOnlineResult(result));
					m_CTIState = CTI_None;
					break;
				}

				// next, find out which member of the pLogonUsers was actually signed in
				// (for silent sign-in, this will always be 0, otherwise, the member at PC's gamepad port)
				for( DWORD i = 0; i < XONLINE_MAX_LOGON_USERS; ++i )
				{
					if ( XOnlineAreUsersIdentical(&LatestUsers[i].xuid, &crosstitle_invite.InviteInfo.xuidAcceptedFriend) )
					{
						crosstitle_invite.JoiningPlayerGamertag = ANSI_TO_TCHAR(LatestUsers[i].szGamertag);
						break;
					}
				}

				FString Question;

				// determine which version of the confirmation message should be shown to the user.
				if ( crosstitle_invite.bInvited )
				{
					Question = FString::Printf(Localize(TEXT("UIX"), TEXT("CrossTitleAcceptedInvite"), TEXT("Engine")), *HostGamertag, *crosstitle_invite.JoiningPlayerGamertag);
				}
				else 
				{
					Question = FString::Printf(Localize(TEXT("UIX"), TEXT("CrossTitleJoinedFriend"), TEXT("Engine")), *HostGamertag, *crosstitle_invite.JoiningPlayerGamertag);
				}

				// now display the prompt to allow the user to decide whether the join the cross-title invite
				eventShowQuestion(CODE_CrossTitleInvite, 50, Question);
			}
		}

		break;

	case CTI_Confirmed:
		// user confirmed that they would like to join the match stored in the cross-title invite.  Display the sign-in screen now.

		// this should never be executed, since we start the sign-in immediately when the user accepts the prompt
		m_CTIState = CTI_PendingLogin;
		StartSignIn();
		break;

	case CTI_Done:
		{
			// we have confirmed that we want to join the match, and we have signed in using the correct account
			// begin the connection process (which will automatically open the character selection menu)
			m_CTIState = CTI_None;
			LiveInterface->StartFindFromIDQuery(crosstitle_invite.InviteInfo.InvitingFriend.sessionID, crosstitle_invite.bInvited);
		}

		break;
	}

	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::UpdateUIXScreen
Desc:		Updates the current UIX screen, and kills attract mode while UIX has control.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::UpdateUIXScreen()
{
	guard(UUIXInteraction::UpdateUIXScreen);

	static WHICH_SCREEN remember = SCREEN_FORCE32;
	if (m_WhichScreen != remember)
	{
		ShowScreenName(m_WhichScreen);
		remember = m_WhichScreen;

		UGUIController* gui = findGUIController();
		if ( gui )
		{
			// if we're currently in UIX menus, disable attract mode
			gui->eventEndTeaser();
			gui->bAttractEnabled = m_WhichScreen == SCREEN_NOT_UIX;
		}

		if ( m_WhichScreen == SCREEN_FRIENDS )
		{
			// clear any pending invite flags
			SetPendingFriendRequest(false);
			SetPendingGameInvite(false);

			if ( gui && gui->bActive )
				gui->eventNotifyInviteReceived(INVITE_Clear);
		}
	}

	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::UpdateGamepadStates
Desc:		Propagates the current gamepad states for each gamepad to the XboxLive manager.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::UpdateGamepadStates()
{
	guard(UUIXInteraction::UpdateGamepadStates);

	INT GamepadPort = UViewport::INVALID_GAMEPAD_ID;

	for (INT i = 0; i < Master->Client->Viewports.Num(); ++i)
	{
		UViewport* Viewport = Master->Client->Viewports(i);
		if ( Viewport && Viewport->Actor )
		{
			GamepadPort = Viewport->GamepadID;
			if ( GamepadPort != UViewport::INVALID_GAMEPAD_ID &&
				// scion sz - careful,overlay viewport uses invalid id
				GamepadPort < NUM_GAMEPADS && 
				Gamepads[GamepadPort].Handle != 0 ) 
			{
				m_pLiveEngine->SetInput(GamepadPort, &Gamepads[GamepadPort].State);
			}

		}
	}

	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::UpdateGameInviteStatus
Desc:		Responsible for notifying the live engine of whether the current gametype supports invites.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::UpdateGameInviteStatus()
{
	guard(UUIXInteraction::UpdateGameInviteStatus);

	static bool bWasJoinable = false;
	static bool bWasInvitable = false;
	if ( ViewportOwner )
	{
		APlayerController* PC = ViewportOwner->Actor;
		if ( PC && PC->GameReplicationInfo )
		{
			if ( PC->GameReplicationInfo->bJoinable != bWasJoinable )
				bStateChanged = true;

			if ( PC->GameReplicationInfo->bInvitable != bWasInvitable )
				bStateChanged = true;

			bWasJoinable = PC->GameReplicationInfo->bJoinable;
			bWasInvitable = PC->GameReplicationInfo->bInvitable;
		}
		else
		{
			if ( bWasInvitable )
				bStateChanged = true;

			if ( bWasJoinable )
				bStateChanged = true;

			bWasJoinable = false;
			bWasInvitable = false;
		}
	}
	else
	{
		if ( bWasInvitable )
			bStateChanged = true;

		if ( bWasJoinable )
			bStateChanged = true;

		bWasJoinable = false;
		bWasInvitable = false;
	}

	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::ProcessLiveWorkTasks
Desc:		Called immediately after DoWork is called on the Xbox LIVE engine.  Checks for flags set by the
			LIVE engine and performs the appropriate action.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::ProcessLiveWorkTasks()
{
	guard(UUIXInteraction::ProcessLiveWorkTasks);

	if ( UIX_HANDLES_NOTIFICATION || (m_dwLiveWorkFlags & UIX_DOWORK_NOTIFICATIONS) )
	{
		// we have pending notifications (game invite, friend invite, etc.)
		DWORD ActualPort = getGamepadPort();
		XboxLiveUser* User = LiveInterface->GetUser(ActualPort);
		if ( User )
		{
			// ghetto-azz xbox live always uses port 0 for silent sign-in, and for this particular
			// function call, the "fake" port needs to be used
			// https://xds.xbox.com/BPProgInfo.asp?Page=content/prog_faq_online.htm#uix_4

			BYTE UserPort = User->GetPort();
			if ( (UIX_PORT_0 << UserPort) & m_dwLiveWorkFlags )
			{
				DWORD Notifications = 0;
				m_pLiveEngine->GetNotifications(UserPort, UIX_NOTIFICATION_MENU, &Notifications);
				ProcessPendingNotifications(ActualPort, Notifications);
			}
		}
	}

	if ( m_dwLiveWorkFlags & UIX_DOWORK_FEATURE_EXIT )
	{
		// Process the exit codes.
		HandleFeatureExit();
	}

	if ( m_dwLiveWorkFlags & UIX_DOWORK_NEED_TO_REBOOT )
	{
		if ( UIXState != STATE_Confirmation )
		{
			// Do any work needed prior to rebooting, then let UIX reboot.
			// You can pass in a context value that will be placed in
			// LD_LAUNCH_DASHBOARD::dwContext and will subsequently be passed
			// back to the you when the dashboard launches you. You can use
			// this to return to the same location in your menus.
			if ( ConfirmationMessage.Len() )
			{
				UIXState = STATE_Confirmation;
				eventShowQuestion(CODE_UIXReboot, UIX_MESSAGEMENU_PRIORITY, ConfirmationMessage);
				ConfirmationMessage = TEXT("");
			}
			else
			{
				m_pLiveEngine->Reboot( 0 );
			}
		}
	}
	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::ProcessPendingNotifications
Desc:		Checks for changes in notification status (game invite received, etc.) and takes the appropriate
			action.  For example, we might need to display an icon on-screen, or if the GUI is active, notify
			the GUI so it can render the icon.

input:		ActualPort
				the gamepad port of the user (zero-based).

			Notifications
				bitmask of notification flags. Generally the same DWORD used for the pNotifications parameter
				of ILiveEngine::GetNotifications()

effects:	None
notes:		None
last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::ProcessPendingNotifications( DWORD ActualPort, DWORD Notifications ) 
{
	guard(UUIXInteraction::ProcessPendingNotifications);

	if ( ActualPort == UViewport::INVALID_GAMEPAD_ID )
		return;

	UBOOL bWaitingFriendRequest = false, bWaitingGameInvite = false;
	UBOOL bNotifyFriendInvite = false, bNotifyGameInvite = false, bClearInvite = false;
	if ( m_WhichScreen != SCREEN_FRIENDS )
	{
		// determine if we currently have a friend request pending
		if ( Notifications & UIX_DOWORK_NOTIFY_FRIEND_REQUEST )
		{
			bWaitingFriendRequest = true;
		}

		// determine if we have a game invite pending
		if ( Notifications & UIX_DOWORK_NOTIFY_GAME_INVITE )
		{
			bWaitingGameInvite = true;
		}

	}

	// determine if our friend invite state has changed since last tick
	if ( bWaitingFriendRequest != bPendingFriendInvite )
	{
		// set the persistent value
		SetPendingFriendRequest(bWaitingFriendRequest);

		// if we've received a new invite, we need to send notification to the gui
		if ( bPendingFriendInvite )
		{
			debugf(NAME_DevLive, TEXT("Received a new friend request on port %u"), ActualPort);
			bNotifyFriendInvite = true;
		}

		// invite was deleted before we had a chance to check the friends list
		// send the clear flag
		else
		{
			debugf(NAME_DevLive, TEXT("Friend request for user on port %u was cancelled."), ActualPort);
			bClearInvite = true;
		}
	}

	// determine if our game invite state has changed since our last tick
	if ( bWaitingGameInvite != bPendingGameInvite )
	{
		// If we're processing a cross-title invite, skip the check for game-invite status.
		if ( bWaitingGameInvite || m_CTIState != CTI_MustConfirm )
		{
			// set the persistent value
			SetPendingGameInvite(bWaitingGameInvite);

			// if we've received a new invite, we need to send notification to the gui
			if ( bPendingGameInvite )
			{
				debugf(NAME_DevLive, TEXT("Received a new game invite for user on port %u."), ActualPort);
				bNotifyGameInvite = true;
			}

			// invite was deleted before we had a chance to check the friends list
			// send the clear flag
			else
			{
				debugf(NAME_DevLive, TEXT("Game invite for user on port %u was cancelled."), ActualPort);
				bClearInvite = true;
			}
		}
	}

	UGUIController* gui = findGUIController();
	if ( gui )
	{
		// if any invites were deleted, send the clear message first
		if ( bClearInvite )
			gui->eventNotifyInviteReceived(INVITE_Clear);

		// if we've received a new friend request,
		// or we had to clear invites, but we still have a pending friend request,
		// notify the gui
		if ( bNotifyFriendInvite ||
			(bClearInvite && bPendingFriendInvite) )
			gui->eventNotifyInviteReceived(INVITE_Friend);

		// if we've received a new game invite,
		// or we had to clear invites, but we still have a pending game invite,
		// notify the gui
		if ( bNotifyGameInvite ||
			(bClearInvite && bPendingGameInvite) )
			gui->eventNotifyInviteReceived(INVITE_Game);
	}

	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::HandleLoginSuccess
Desc:		Called when the user has successfully logged into Xbox Live.  Perform any required initialization.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::HandleLoginSuccess()
{
	guard(UUIXInteraction::HandleLoginSuccess);

	// We have successfully logged on one or more gamers.
	if ( m_WhichScreen == SCREEN_LOGON )
		m_WhichScreen = SCREEN_NOT_UIX;

	check(LiveInterface);

	XONLINE_USER* pLogonUsers = XOnlineGetLogonUsers();
	if ( pLogonUsers == NULL )
		return;

	// is this the user that was is associated with the pending cross-title invite (if applicable)?
	UBOOL bCrossTitleInviteUser = 0;

	// Authentication successful
	// Silent sign-in always signs in as user 0
	// Manual sign-in signs in as the correct user (user corresponding to the gamepad port)
	// We only allow one person to be signed on, so we can make this very easy to deal with
	UViewport* View = (UViewport*)ViewportOwner;
	APlayerController* PC = View ? View->Actor : NULL;
	if ( PC )
	{
		// next, find out which member of the pLogonUsers was actually signed in
		// (for silent sign-in, this will always be 0, otherwise, the member at PC's gamepad port)
		for( DWORD i = 0; i < XONLINE_MAX_LOGON_USERS; ++i )
		{
			if ( pLogonUsers[i].xuid.dwUserFlags != 0 )
			{
				// found the position of the logged in user
				XboxLiveUser* User = LiveInterface->GetUser(PC->GamepadID);
				check(User != NULL && "PlayerController has invalid GamepadID");

				User->SetPort(i);
				User->LinkWithXUser(&pLogonUsers[i]);

				// set this to true so that the user's presence information is updated in the next tick.
				bStateChanged = true;

				// if we have a crosstitle invite pending, check whether the logged in use is the same user that accepted
				// the cross-title invite.
				if ( m_CTIState == CTI_PendingLogin || m_CTIState == CTI_IncorrectAccount )
				{
					bCrossTitleInviteUser = bCrossTitleInviteUser || XOnlineAreUsersIdentical(&pLogonUsers[i].xuid, &crosstitle_invite.InviteInfo.xuidAcceptedFriend);
				}
			}
		}

		if ( PC->GameReplicationInfo )
		{
			XUID Zero = {0};
			for ( INT i = 0; i < PC->GameReplicationInfo->PRIArray.Num(); i++ )
			{
				APlayerReplicationInfo* PRI = PC->GameReplicationInfo->PRIArray(i);
				if ( PRI && PRI != PC->PlayerReplicationInfo && !PRI->bBot && 
					!PRI->bDedicatedServerSpectator && !PRI->bOnlySpectator )
				{
					// if the player's XUID is not zero
					if ( appMemcmp(&PRI->XUID, &Zero, sizeof(XUID)) &&
						PRI->GamerTag != TEXT("") )
					{
						RegisterPRI(PRI);
					}
				}
			}
		}
	}

	SetOnlineState(ONLINE_Success);
	if ( PC )
	{
		// register this player's XUID and gamer tag on the server
		// (which will copy the data into the player's PRI)
		RegisterPlayer(PC);
		if ( PC->PlayerReplicationInfo )
			StatCache = PC->PlayerReplicationInfo->StatCache;

		if ( StatCache )
		{
			StatCache->bNetOwner = true;
			StatCache->bRefreshCache = true;
		}
	}

	// Now that we are signed into Live, we can initialize XHV. This will in turn call UIX to set up voice support.
	UEngine* Engine = UEngine::GEngine;
	UXHVVoiceSubsystem* Voice = Cast<UXHVVoiceSubsystem>(Engine->Voice);
	if (Voice != NULL)
	{
		Voice->Init(Engine->Audio);

		// If XHV was succesfully initialized
		if (Voice->Initialized())
		{
			UViewport* Viewport = Engine->Audio->GetViewport();
			Voice->SetViewport(Viewport);
		}
	}

	// if the user that logged in was a user that has a pending cross-title invite,
	// set the flag which starts the connection process.
	if ( m_CTIState == CTI_PendingLogin || m_CTIState == CTI_IncorrectAccount )
	{
		if ( bCrossTitleInviteUser )
		{
			// user logged in using the account that accepted/initiated the cross-title join.
			m_CTIState = CTI_Done;
	/*
			do this in ProcessCrossTitleInvites instead

			XONLINE_FRIEND* pFriendData = ( XONLINE_FRIEND*)exitInfo.pExitData;
			BOOL bInvited = (pFriendData->dwFriendState&XONLINE_FRIENDSTATE_FLAG_RECEIVEDINVITE) != 0;

			LiveInterface->StartFindFromIDQuery(pFriendData->sessionID, bInvited);
			m_CTIState = CTI_None;
	*/
		}

		else if ( m_CTIState == CTI_PendingLogin )
		{
			// the user that logged in was not the user that accepted the invite
			FString Message = FString::Printf(Localize(TEXT("UIX"), TEXT("CrossTitleWrongAccount"), TEXT("Engine")), *crosstitle_invite.JoiningPlayerGamertag);
			eventShowMessage(Message, UIX_MESSAGEMENU_PRIORITY);
			m_CTIState = CTI_IncorrectAccount;
		}
		else
		{
			// this is the second time the user logged in using the wrong account while we had a pending game invite....let's just bail
			m_CTIState = CTI_None;
		}
	}

	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::HandleLoginFailure
Desc:		Called when an attempt to login to Xbox live has failed.  Determines the appropriate resonse
			depending on the reason for failure.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::HandleLoginFailure( HRESULT hr )
{
	guard(UUIXInteraction::HandleLoginFailure);

	// This exit code either means that logon failed or it means that
	// the users were logged off after a successful logon.
	// Because of this second use, this exit feature message can arrive
	// even when no feature is running.
	// Players can be logged off unexpectedly if they log on to a second
	// Xbox or if they lose their network connection.
#ifdef _XBOX
	warnf(TEXT("Authentication Failed with Error 0x%x (%s)"), hr, ParseXOnlineResult(hr)  );
#endif
	// Give specific information about known errors.

	switch ( hr )
	{
		// SILENT LOGON FAILCODES
	case XONLINE_E_SILENT_LOGON_DISABLED:
		// vvvvv - fall through
	case XONLINE_E_SILENT_LOGON_PASSCODE_REQUIRED:
		if ( hr == XONLINE_E_SILENT_LOGON_DISABLED )
		{
			LoginFailureReason = ERROR_SilentLoginDisabled;
			debugf(NAME_DevLive, TEXT("SilentLogin failed: silent login disabled from dashboard!"));
		}
		else
		{
			LoginFailureReason = ERROR_NeedPasscode;
			debugf(NAME_DevLive, TEXT("SilentLogin failed: Passcode required!"));
		}

		break;

	case XONLINE_E_SILENT_LOGON_NO_ACCOUNTS:
		// If there are no Live accounts, display nothing.
		LoginFailureReason = ERROR_NoAccounts;
		debugf(NAME_DevLive, TEXT("SilentLogin failed: no xbox live accounts"));
		break;

	case XONLINE_E_LOGON_KICKED_BY_DUPLICATE_LOGON:
		LoginFailureReason = ERROR_DuplicateLogin;
		warnf(NAME_DevLive, TEXT("Login failed: duplicate login"));
		break;

	case XONLINE_E_LOGON_NO_NETWORK_CONNECTION:
	case XONLINE_E_LOGON_CONNECTION_LOST:
		LoginFailureReason = ERROR_ConnectionLost;
		break;

		// We must display messages for the following error codes
	case XONLINE_E_LOGON_SERVERS_TOO_BUSY:
		if ( LiveInterface->bSilentSignin )
			eventShowMessage(Localize(TEXT("UIX"), TEXT("LiveErrorServerBusy"), TEXT("Engine")), UIX_MESSAGEMENU_PRIORITY);

		LoginFailureReason = ERROR_None;
		break;

	case XONLINE_E_LOGON_USER_ACCOUNT_REQUIRES_MANAGEMENT:
		break;

	default:
		LoginFailureReason = ERROR_None;
		break;
	}

	m_CTIState = CTI_None;
	SetOnlineState(ONLINE_Failed);
	Reset();

	unguard;
}


#endif

/* Search the list of GlobalInteractions kept by the InteractionMaster to find the GUIController 
   (the menu system interaction). 
*/
UGUIController* UUIXInteraction::findGUIController() 
{
	guard(UUIXInteraction::findGUIController);

	/* Need to get the GUI interface to hide any current menu and to display the UIX background
	screen. Requires finding the GUIController. */
	if ( staticGUIController == NULL )
	{
		for (int i = 0; i < Master->GlobalInteractions.Num(); ++i)
		{
			staticGUIController = Cast<UGUIController>(Master->GlobalInteractions(i));
			if ( staticGUIController )
				break;
		}

	}

	return staticGUIController;
	unguard;
}

XUID UUIXInteraction::GetXUID()
{
	guard(UUIXInteraction::GetXUID);

#ifdef _XBOX
	if ( LiveInterface && SignedIn() )
	{
		INT Port = getGamepadPort();
		if ( Port != -1 )
		{
			XboxLiveUser* User = LiveInterface->GetUser(Port);
			if ( User && User->Valid() )
				return User->GetXUID();
		}
	}
#endif

	return XUID();
	unguard;
}

INT UUIXInteraction::getGamepadPort()
{
	INT GamepadPort = UViewport::INVALID_GAMEPAD_ID;
#ifdef _XBOX

	if ( Master && Master->Client && Master->Client->Viewports.Num() )
	{
		for ( INT i = 0; i < Master->Client->Viewports.Num(); i++ )
		{
			UViewport* Viewport = Master->Client->Viewports(i);
			if ( Viewport )
			{
				GamepadPort = Viewport->GetGamepadID();

				// check for less than 5 because the overlay viewport's gamepadID is
				// not INVALID_GAMEPAD_ID, but we never want to return that one.
				if ( GamepadPort < 5 )
					break;
			}
		}
	}

#endif
	return GamepadPort;
}

void UUIXInteraction::VoiceCommunicatorUpdate(DWORD dwUser)
{
	guard(UUIXInteraction::VoiceCommunicatorUpdate);
#ifdef _XBOX
	debugf(NAME_DevPer, TEXT("UUIXInteraction::VoiceCommunicatorUpdate"));
	bStateChanged = true;
#endif // _XBOX
	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::SetOnlineState
Desc:		Updates the OnlineState to reflect the specified state, then notifies the GUI.

input:		NewState
				current online status

notes:		None
last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::SetOnlineState( EOnlineState NewState )
{
	guard(UUIXInteraction::SetOnlineState);

	UBOOL bNotify = OnlineState != NewState;
	OnlineState = NewState;

	// reset the login failcode
	if ( OnlineState != ONLINE_Failed )
		LoginFailureReason = ERROR_None;

	if ( bNotify )
	{
		UGUIController* C = findGUIController();
		if ( C )
			C->eventNotifyOnlineStateChanged(OnlineState);
	}

	unguard;
}

//-----------------------------------------------------------------------------
// Name: StartPlayersList()
// Desc: Call this function to start the UIX Players List feature. The structure
// will be initialized appropriately based on the parameters you pass.
//-----------------------------------------------------------------------------
HRESULT UUIXInteraction::StartPlayersList( BOOL LobbyMode, BOOL DisplayDeparted, BOOL DisplayActive, DWORD portNumber, int FilterFlags )
{
	guard(UUIXInteraction::StartPlayersList);
	HRESULT hr = S_OK;
#ifdef _XBOX

	// Declare and zero UIX_LOGON_PARAMS
	UIX_PLAYERS_PARAMS PlayersParams;
	appMemzero(&PlayersParams, sizeof(UIX_PLAYERS_PARAMS));
	PlayersParams.StructSize = sizeof(UIX_PLAYERS_PARAMS);

	// The port of the controller used to control the player's screen
	// Set this to UIX_INVALID_VALUE if you want this value ignored.
	// It will also be ignored in UIX_PLAYERS_DISPLAY_LOBBY_MODE
	PlayersParams.UserPort = portNumber;

	// The player controlling port UserPort. They will be displayed as
	// the first player in the list, but won't be selectable (can be
	// highlighted, but nothing happens).
	PlayersParams.pPlayerControllingScreen = 0;
	if( portNumber != UIX_INVALID_VALUE )
	{
		// ASSUMPTION: The viewport's owner is the player controller that logged on
		UViewport * View = (UViewport*) ViewportOwner;
		APlayerController * PC = View ? (APlayerController *) View->Actor : NULL;
		APlayerReplicationInfo * PRI = PC ? PC->PlayerReplicationInfo : NULL;
		PlayersParams.pPlayerControllingScreen = FindPlayerItem(PRI);
	}

	if (!PlayersParams.pPlayerControllingScreen)
		debugf(TEXT("NO player controlling screen..."));
	// The XUID of the player that should be selected the first time the screen is shown.
	PlayersParams.pSelectedPlayerXUID = 0;

	// Should the player list be sorted? If so, use the compare function in ITitlePlayersListItem
	PlayersParams.SortCurrentPlayersList = FALSE;

	// A flag that is masked against ITitlePlayersListItem::GetBitFlags() to filter which
	// players are shown. A value of zero implies that all players should be shown.
	PlayersParams.FilterFlags = FilterFlags;
	// Record the filter flags so we can do custom rendering based on them.
	//m_FilterFlags = FilterFlags;

	// Can be UIX_PLAYERS_DISPLAY_CURRENT_PLAYERS, UIX_PLAYERS_DISPLAY_DEPARTED_PLAYERS,
	// or UIX_PLAYERS_DISPLAY_DEFAULT - which is
	// equal to (UIX_PLAYERS_DISPLAY_CURRENT_PLAYERS | UIX_PLAYERS_DISPLAY_DEPARTED_PLAYERS)
	// You can also optionally OR in UIX_PLAYERS_DISPLAY_LOBBY_MODE
	PlayersParams.DisplayType = 0;
	if( DisplayActive )
		PlayersParams.DisplayType |= UIX_PLAYERS_DISPLAY_CURRENT_PLAYERS;
	if( DisplayDeparted )
		PlayersParams.DisplayType |= UIX_PLAYERS_DISPLAY_DEPARTED_PLAYERS;
	assert( PlayersParams.DisplayType != 0 );
	if( LobbyMode )
		PlayersParams.DisplayType |= UIX_PLAYERS_DISPLAY_LOBBY_MODE;

	// Stop any mutelist query in progress so we don't confuse the UIX players list.
	XboxLiveManager* Manager = XboxLiveManager::getXboxLiveManager();
	if (Manager != NULL)
	{
		Manager->StopMuteListQuery();
	}

	// Start the players feature.
	hr = m_pLiveEngine->StartFeature( UIX_PLAYERS_FEATURE, &PlayersParams );
#endif
	return hr;
	unguard;
}
// =====================================================================================================================
// =====================================================================================================================
// AbstractUIXInteraction Interface
// =====================================================================================================================
// =====================================================================================================================
void UUIXInteraction::execGetUIXInteraction( FFrame& Stack, RESULT_DECL )
{
	guard(UUIXInteraction::execGetUIXInteraction);

	P_FINISH;

	*(UAbstractUIXInteraction**)Result = theOnlyUIX;

	unguardexec;
}

/* Initialize the UIX engine so that it can connect to Xbox Live (and monitor what
is going on there during gameplay. */
void UUIXInteraction::InitUIX()
{
	guard(UUIXInteraction::InitUIX);

	debugf(NAME_DevLive, TEXT("UIXInteraction initialized!"));
	//SetFlags( RF_TagPersist );

	UGUIController* gui = findGUIController();
	GameInviteIcon->eventInitComponent(gui, NULL);
	FriendInviteIcon->eventInitComponent(gui, NULL);

#ifdef _XBOX

	LiveInterface = XboxLiveManager::getXboxLiveManager();
	SetOnlineState(ONLINE_None);

	bNetworkActive = IsNetworkActive();
	LastNetworkCheck = appSeconds();

	//jg -- Correctly handle demo paths
	FString strFontFileName = GFileManager->ResolvePath(TEXT("..\\Textures\\Font.xpr"));
	HRESULT hr = 0; // result of calling XboxLive routines

	// Create the font
	m_pFont = new CXBFont();
	if( !m_pFont || FAILED( hr = m_pFont->Create( *strFontFileName ) ) )
	{
		warnf( NAME_Error, TEXT("Unable to create XOnline required font (0x%x)"), hr );
		return;
	}

	// start up the Xbox Live library
	// this UIX wrapper function handles calling XNetStartup and WSAStartup with default parameters
	// in order to enable insecure communication, we need to call XNetStartup ourselves prior to this
	// call (see UUIXInteraction::Initialize())
	hr = XOnlineStartup( NULL );
	if ( FAILED( hr ) )
	{
		warnf( NAME_Error, TEXT("XOnlineStartup failed (0x%x)"), hr );
		return;
	}

	// Create the UI plugin
	static UIXFont m_UIXFont( m_pFont );

	hr = UIXCreateUIPlugin( &m_UIXFont, &m_pUIPlugin );
	if ( FAILED( hr ) )
	{
		warnf( NAME_Error, TEXT("UIXCreateUIPlugin failed (0x%x)"), hr );
		return;
	}

	// Create the UIX engine
	FString skinFileName = GFileManager->ResolvePath(TEXT("..\\Textures\\Avaris.uix"));

	DWORD XLanguage = appGetLanguageID();
	if ( !IsSupportedLanguage(XLanguage) )
	{
		debugf(NAME_DevLive, TEXT("Language %s unsupported, so using %s."), GLanguageExt[XGetLanguage()], GLanguageExt[XLanguage]);
	}

	hr = UIXCreateLiveEngine( appToAnsi(*skinFileName), XLanguage, &m_pLiveEngine );
	if ( FAILED( hr ) )
	{
		warnf( NAME_Error, TEXT("UIXCreateLiveEngine failed (0x%x)"), hr );
		m_pLiveEngine = NULL; // make sure it remains NULL
		return;
	}


	// Here is where sound engine set up takes place in the sample code

	// Setup the UI plugin and enable the desired features
	m_pLiveEngine->SetUIPlugin( m_pUIPlugin );
	m_pLiveEngine->EnableFeature( UIX_LOGON_FEATURE );
	m_pLiveEngine->EnableFeature( UIX_FRIENDS_FEATURE );
	m_pLiveEngine->EnableFeature( UIX_PLAYERS_FEATURE );

	Reset();

	// We don't want UIX to handle displaying notifications for things like
	// invites and friend requests, since we need to be able to position the notifications
	// in different places depending on which menu is being shown.
	m_pLiveEngine->SetProperty( UIX_PROPERTY_DISPLAY_NOTIFICATIONS, UIX_HANDLES_NOTIFICATION );

	// InitUIX is only called once, so we'll need to update this property somewhere else each time a match begins
	m_pLiveEngine->SetProperty( UIX_PROPERTY_ALLOW_GAME_INVITES, FALSE );

	// 
	m_pLiveEngine->SetProperty( UIX_PROPERTY_HOST_NO_MIGRATION, TRUE );

	m_CTIState = CTI_None;
	if (XOnlineFriendsGetAcceptedGameInvite(&crosstitle_invite.InviteInfo) == S_OK)
	{
		// we have a cross-title join-match waiting, we'll need to display a prompt to join the match
		// as soon as the user bypasses the IIS
		m_CTIState = CTI_MustConfirm;
	}
#endif
	unguard;
}

void UUIXInteraction::DestroyUIX()
{
	guard(UUIXInteraction::DestroyUIX);

	Reset();

#ifdef _XBOX
	if (m_pLiveEngine)
		m_pLiveEngine->Release();
	if (m_pUIPlugin)
		m_pUIPlugin->Release();
#endif

	unguard;
}

/* StartLogon passes UI control to the drop in Xbox user interface (UIX).
   This is exposed to script so that menus can do it (instead of passing
   control to another menu, for example).
*/
void UUIXInteraction::StartSignIn( UBOOL bSilentLogon )
{
	guard(UUIXInteraction::StartSignIn);
#ifdef _XBOX

	switch ( m_CTIState )
	{
	case CTI_None:
		// no cross-title invites pending
		break;

	case CTI_MustConfirm:
		// if we have a cross-title invite in progress, and the user hasn't yet confirmed
		// can't sign in
		return;

	case CTI_Confirmed:
		// user has confirmed that they would like to login and join the session
		break;

	case CTI_IncorrectAccount:
		// user confirmed that they wanted to join the pending cross-title session, but then
		// logged in using the wrong account, logged out, and is now logging in again
		break;

	case CTI_Done:
		// dunno yet
		break;
	}

	// first, clear out all current data
	LiveInterface->clearAllUsers();

	// Declare and zero UIX_LOGON_PARAMS
	UIX_LOGON_PARAMS    LogonParams = {0};
	LogonParams.StructSize = sizeof(UIX_LOGON_PARAMS);

	UGUIController * gui = findGUIController();

	// Specify how many users should be allowed to logon.
	// For manual or game invite login, this can be 1, 2, or 4.
	// For silent login, this should always be one.


	// We only support one signed in user, so this is always 1.
	LogonParams.LogonUserCount = 1;

	// we don't want UIX to handle messages like logoff due to duplicate login, network
	// connection lost, etc. because we don't receive notifications in these cases and I need to disable
	// attract mode for this. therefore, we'll only enable connection errors while we're actually 
	// attempting to log-in.
	m_pLiveEngine->SetProperty( UIX_PROPERTY_DISPLAY_CONNECTION_ERRORS, TRUE );

	// There was a cross-title invite waiting, and the user has confirmed that they
	// would like to sign in and join the match.
	if ( m_CTIState == CTI_Confirmed )
	{
		m_CTIState = CTI_PendingLogin;

		// there is a UIX_LOGON_TYPE_RETRIEVED_GAME_INVITE that can be used,
		// but it's kinda ghetto so we'll just have the user sign-in normally
		bSilentLogon = false;
	}
	
	if ( bSilentLogon )
	{
		// Select the type of logon - normal, silent, etc.
		// Silent logon is typically used for live aware titles.
		LogonParams.LogonType = UIX_LOGON_TYPE_SILENT;
	}
	else
	{	
		if (gui != NULL)
		{
			gui->eventOpenMenu(TEXT("UCMenu.UIXMenu"), TEXT(""), TEXT(""));
		}

		// Normal logon is typically used otherwise.
		LogonParams.LogonType = UIX_LOGON_TYPE_NORMAL;
	}

	LiveInterface->bSilentSignin = bSilentLogon;

	// Specify one or more services, consecutively starting from index zero.
	// ms-help://MS.VSCC.2003/MS.XboxSDK/XboxSDK/html/XOnlineLogon.htm
	LogonParams.LogonServiceIDs[0] = XONLINE_MATCHMAKING_SERVICE;
	LogonParams.LogonServiceIDs[1] = XONLINE_FEEDBACK_SERVICE;
	LogonParams.LogonServiceIDs[2] = XONLINE_NAT_TYPE_DETECTION_SERVICE;
	LogonParams.LogonServiceIDs[3] = XONLINE_CONTENT_AVAILABLE_SERVICE;
	LogonParams.LogonServiceIDs[4] = XONLINE_STATISTICS_SERVICE;
	LogonParams.LogonServiceIDs[5] = XONLINE_QUERY_SERVICE;

	// Signify the end of the services we request
	LogonParams.LogonServiceIDs[6] = XONLINE_INVALID_SERVICE;

	// Start the process of logging on to Live
	HRESULT hr = m_pLiveEngine->StartFeature( UIX_LOGON_FEATURE, &LogonParams );
	if ( FAILED(hr) )
	{
		// don't handle silent sign-on errors here
		if ( (hr & 0x80) == 0 )
		{
			debugf(NAME_Error, TEXT("ILiveEngine::StartFeature failed with error 0x%08X"), hr);
			Reset();
			LoginFailureReason = ERROR_None;
			SetOnlineState(ONLINE_Failed);
			return;
		}
	}

	// Let the user know that they are signing in. Only critical for silent
	// logon (might not be displayed otherwise).
	SetOnlineState(ONLINE_Pending);

	if ( !bSilentLogon )
	{
		// With silent logon we don't change menus to logon.
		// With a manual logon we go to the logon screen.
		m_WhichScreen = SCREEN_LOGON;
	}

#endif
	unguard;
} // StartSignIn

void UUIXInteraction::StartSignOut()
{
	guard(UUIXInteraction::StartSignOut);

	// reinitialize everything back to defaults
	SetOnlineState(ONLINE_None);
	Reset();

	unguard;
}

/* Return the gamer tag for the given gamepad port. "Guest" if logged in as guest
and "" if not logged in.
*/
void UUIXInteraction::GetGamerTag( INT GamepadID, FString& out_GamerTag )
{
	guard(UUIXInteraction::GetGamerTag);

	out_GamerTag = TEXT("");

#ifdef _XBOX
	if ( SignedIn() )
	{
		if ( GamepadID >= NUM_GAMEPADS )
		{
			debugf(TEXT("UIXInteraction::GetGamerTag: Specified invalid GamepadID: %i"), GamepadID);
			GamepadID = getGamepadPort();
		}

		XboxLiveUser* User = LiveInterface->GetUser(GamepadID);
		if ( User != NULL && User->LoggedIn() )
		{
			if ( User->IsGuest() )
				out_GamerTag = Localize( TEXT("UIX"), TEXT("Guest"), TEXT("Engine") );
			else
				out_GamerTag = User->GetGamerTag();
		}
	}
#endif
	unguard;
}

void UUIXInteraction::UpdateOnlineState()
{
	guard(UUIXInteraction::UpdateOnlineState);

#ifdef _XBOX
	UXHVVoiceSubsystem* Voice = Cast<UXHVVoiceSubsystem>(UEngine::GEngine->Voice);
	if (Voice && Voice->Initialized())
	{
		SetVoiceMailMode(Master->Client->Viewports(0)->Profile->bVoiceOverSpeakers);
	}

	for( DWORD dwUser = 0; dwUser < XONLINE_MAX_LOGON_USERS; dwUser++ )
	{
		if ( LiveInterface->UpdatePlayerOnlineState( dwUser ) )
			bStateChanged = false;
	}
#endif // _XBOX

	unguard;
}

void UUIXInteraction::RegisterSession()
{
	guard(UUIXInteraction::RegisterSession);

#ifdef _XBOX
	m_RegisterCountdown = REGISTRATION_COUNTDOWN_SECONDS;
#endif

	unguard;
}

void UUIXInteraction::UpdateSession()
{
	guard(UUIXInteraction::UpdateSession);

#ifdef _XBOX
	LiveInterface->UpdateSession();
#endif

	unguard;
}

void UUIXInteraction::EndSession(UBOOL bBlocking)
{
	guard(UUIXInteraction::EndSession);

#ifdef _XBOX
	if (m_RegisterCountdown > 0)
		m_RegisterCountdown = 0; // stop countdown to registering game

	if (LiveInterface)
	{
		if (bBlocking)
		{
			LiveInterface->EndSessionBlocking();
		}
		else
		{
			LiveInterface->EndSession();
		}
	}
#endif
	unguard;
}


void UUIXInteraction::ShowFriends( INT DesiredUserIndex )
{
	guard(UUIXInteraction::ShowFriends);
#ifdef _XBOX

	// UIX can't access the friends list if we're accessing it, so make sure we clear
	// any open handles to the friends list first.
	if ( m_pLiveEngine && FriendsInterface )
	{
		FriendsInterface->Release();
		FriendsInterface = NULL;
	}

	UGUIController * gui = findGUIController();
	if (gui != NULL)
	{
		gui->eventOpenMenu(TEXT("UCMenu.UIXMenu"), TEXT(""), TEXT(""));
	}
	UIX_FRIENDS_PARAMS  FriendsParams = {0};
	FriendsParams.StructSize = sizeof( FriendsParams );
	FriendsParams.UserPort = DesiredUserIndex;

	// FriendsParams.SelectedFriendXUID = XUID of friend to initially select in list
	// Set this if you want to allow signout from the friend's feature.
	FriendsParams.SignOutEnabled = FALSE;

	// Start the friends feature
	HRESULT hr = m_pLiveEngine->StartFeature( UIX_FRIENDS_FEATURE, &FriendsParams );

	if ( FAILED( hr ) )
	{
		warnf(NAME_Error, TEXT("Managing Friends Failed with Error 0x%X"), hr );
		m_WhichScreen = SCREEN_NOT_UIX;
		eventCloseUIXMenu(0);
	}
	else
		m_WhichScreen = SCREEN_FRIENDS;
#endif
	unguard;
}

void UUIXInteraction::ShowPlayers( INT DesiredUserIndex )
{
	guard(UUIXInteraction::ShowPlayers);
#ifdef _XBOX
	UGUIController * gui = findGUIController();
	if (gui != NULL)
	{
		gui->eventOpenMenu(TEXT("UCMenu.UIXMenu"), TEXT(""), TEXT(""));
	}

	UIX_PLAYERS_PARAMS  PlayersParams = {0};
	PlayersParams.StructSize = sizeof( PlayersParams );
	PlayersParams.UserPort = DesiredUserIndex;

	UViewport* Viewport = Cast<UViewport>(ViewportOwner);
	if ( Viewport )
	{
		BOOL bMenuLevel=0; 
		if ( Viewport->Actor )
		{
			bMenuLevel = !appStricmp(Viewport->Actor->GetLevel()->GetOuter()->GetName(), TEXT("Menu"));
		}

		if (HRESULT hr =  StartPlayersList( FALSE, TRUE, !bMenuLevel, DesiredUserIndex, 1 ) )
		{
			warnf(NAME_Error, TEXT("Managing Players Failed with Error 0x%X"), hr );
			m_WhichScreen = SCREEN_NOT_UIX;
			eventCloseUIXMenu(0);
		}
		else
			m_WhichScreen = SCREEN_PLAYERS;
	}
#endif
	unguard;
}


/*==========================================================================================================
function	UUIXInteraction::RegisterPlayer
Desc:		Adds a player to the player's list.  This is the version that is called on the server
			as well as the owning client.  When registering the owning client (whether that player
			is the host of the match, or an actual client), once we've created the player list item
			for ourselves which contains the correct XUID, gamepad port, etc. - we then perform an RPC
			to update the XUID and GamerTag values for our PRI on the server.  These values are then
			replicated to all other clients, which causes those players to register our XUID into their
			local player lists.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::RegisterPlayer( APlayerController* PC )
{
	guard(UUIXInteraction::RegisterPlayer);
#ifdef _XBOX
	if ( PC == NULL )
	{
		debugf(NAME_Warning, TEXT("UUIXInteraction::RegisterPlayer - Attempted to register NULL PlayerController!"));
	}

	// if this player is going away, don't allow, since they'd never be unregistered
	// (unregistration is handled in APlayerController::Destroy())
	if ( PC->bDeleteMe )
		return;

	// This function should be called from PlayerController.ReceivedViewport(), thus,
	// the player should have a viewport.  Let's check for that next.
	if ( PC->Player == NULL )
	{
		debugf(NAME_DevLive, TEXT("UUIXInteraction::RegisterPlayer - Attempted to register a player with null Viewport!"));
		return;
	}

	// the player should ALWAYS have a PRI by now...let's check for that next
	if ( PC->PlayerReplicationInfo == NULL )
	{
		debugf(NAME_DevLive, TEXT("UUIXInteraction::RegisterPlayer - Attempted to register a player with null PRI!"));
		return;
	}

	if ( PC->PlayerReplicationInfo->bDeleteMe )
		return;

	// only allow this if we're currently signed-in.
	if ( OnlineState != ONLINE_Success )
		return;

	BYTE GamepadPort = PC->Player->GetGamepadID();
	debugf(NAME_DevLive, TEXT("UUIXInteraction::RegisterPlayer (GamepadID:%u)"), GamepadPort);
	XboxLiveUser* User = LiveInterface->GetUser(GamepadPort);
	if ( User )
	{
		// registering the local player (might be a server or client)
		PC->PlayerReplicationInfo->GamerTag = User->GetGamerTag();
		User->GetPlayerXUID(PC->PlayerReplicationInfo->XUID);

		PC->PlayerReplicationInfo->StatCache = StatCache;
		AddPlayerItem(PC->PlayerReplicationInfo, GamepadPort);

		// now perform the registration on the server
		// this will also assign the values of our PRI XUID and GamerTag
#ifndef FINAL_RELEASE
		XUID UserXUID = User->GetXUID();
		debugf(NAME_DevLive, TEXT("Sending XUID (0x%016I64X - 0x%08X) to server for %s"), UserXUID.qwUserID, UserXUID.dwUserFlags, *PC->PlayerReplicationInfo->GamerTag);
#endif
		PC->eventServerRegisterUIX( 
			PC->PlayerReplicationInfo->XUID[0],
			PC->PlayerReplicationInfo->XUID[1],
			PC->PlayerReplicationInfo->XUID[2],
			PC->PlayerReplicationInfo->XUID[3],
			PC->PlayerReplicationInfo->XUID[4],
			PC->PlayerReplicationInfo->XUID[5],
			PC->PlayerReplicationInfo->XUID[6],
			PC->PlayerReplicationInfo->XUID[7],
			PC->PlayerReplicationInfo->XUID[8],
			PC->PlayerReplicationInfo->XUID[9],
			PC->PlayerReplicationInfo->XUID[10],
			PC->PlayerReplicationInfo->XUID[11],
			PC->PlayerReplicationInfo->GamerTag );
	}
	else
	{

		// registering a remove client on the server
		AvarisPlayerItem* Player = AddPlayerItem(PC->PlayerReplicationInfo, 255);

		// notify the voice engine in case it needs to perform any initialization
		UXHVVoiceSubsystem* XHV = UXHVVoiceSubsystem::getUXHVVoiceSubsystem();
		if ( Player && XHV )
			XHV->ReceivedNewPlayer( *Player->GetXUID() );
	}

#endif
	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::RegisterPRI
Desc:		Adds a player to the player's list.  This version is only called on clients for other clients, 
			once value of that player's XUID has been replicated.
			e.g. we'll never call this function where PRI is the local player's PRI

input:		PRI
				the PlayerReplicationInfo for the player we want to register.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::RegisterPRI( APlayerReplicationInfo* PRI )
{
	guard(UUIXInteraction::RegisterPRI);

	// the player should ALWAYS have a PRI by now...let's check for that next
	if ( PRI == NULL )
	{
		debugf(NAME_DevLive, TEXT("UUIXInteraction::RegisterPRI - Attempted to register a null PRI!"));
		return;
	}

	if ( PRI->bDeleteMe )
		return;

	// only allow this if we're currently signed-in.
	if ( OnlineState != ONLINE_Success )
		return;

	AddPlayerItem( PRI, 255 );

	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::UnregisterPRI
Desc:		Removes a player from the players list.  This version is called for both clients and servers.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::UnregisterPRI( APlayerReplicationInfo* PRI )
{
	guard(UUIXInteraction::UnregisterPRI);

#ifdef _XBOX
	if ( PRI == NULL )
	{
		debugf(NAME_DevLive, TEXT("UUIXInteraction::UnregisterPRI - Attempted to unregister a null PRI!"));
		return;
	}

	AvarisPlayerItem* Player = FindPlayerItem(PRI);
	if ( Player == NULL )
	{
		debugf(NAME_DevLive, TEXT("UUIXInteraction::UnregisterPRI failed to find registered PRI '%s'"), *PRI->PlayerName);
		return;
	}

	RemovePlayerItem(PRI);

#endif

	unguard;
}



/*==========================================================================================================
function	UUIXInteraction::AddPlayerItem
Desc:		Responsible for actually adding the specified PlayerReplicationInfo into the UIX player list.

notes:		None
last modified by:	Ron Prestenback
==========================================================================================================*/
AvarisPlayerItem* UUIXInteraction::AddPlayerItem( APlayerReplicationInfo* PRI, BYTE GamepadPort )
{
	guard(UUIXInteraction::AddPlayerItem);

#ifdef _XBOX

	check(PRI);

	XUID NewPlayerXUID;
	appMemcpy( &NewPlayerXUID, &PRI->XUID, sizeof(XUID) );

	ILivePlayersList* pPlayers;
	if (FAILED(m_pLiveEngine->GetFeatureInterface(UIX_PLAYERS_FEATURE, NULL, (LPVOID*)&pPlayers)))
	{
		return NULL;
	}

	// If this player has already been registered, don't register again
	// - just update the values for that list item

	// on a client, it's possible for the voicechat code to receive a voicechat packet
	// for a player before that player has been registered with UIX.  If this happens, the
	// voicechat code will "preregister" the player with UIX. That is, we'll have an
	// AvarisPlayerItem with a NULL PRI but a valid XUID.  this is why we search using the slighly
	// slower XUID match, instead of the direct PRI match
	AvarisPlayerItem* Player = FindPlayerItem(&NewPlayerXUID);
	if ( Player )
	{
		debugf(NAME_DevLive, TEXT("UUIXInteraction::AddPlayerItem - Matched existing AvarisPlayerItem to XUID.  '%s' (0x%016I64X)"), *PRI->GamerTag, NewPlayerXUID.qwUserID);

		PRI->bRegisteredUIX = true;
		Player->SetName(PRI->GamerTag);
		Player->SetPort(GamepadPort < 4 ? GamepadPort : 255);
		Player->SetPRI(PRI);
		Player->SetBitflags(1);

		pPlayers->Refresh();

		ProcessMuteList();

		return Player;
	}

	if ( m_pLiveEngine == NULL )
		return NULL;

	PRI->bRegisteredUIX = true;
	UBOOL bNoGamerTag = PRI->GamerTag.Len() == 0;
	Player = new AvarisPlayerItem(NewPlayerXUID, *PRI->GamerTag, GamepadPort < 4 ? GamepadPort : 255, PRI, bNoGamerTag ? 0 : 1);
	HRESULT result = pPlayers->RegisterPlayer(Player);
	if (SUCCEEDED(result))
	{
		RegisteredPlayers.AddItem(Player);
	}
	else
	{
		debugf(NAME_Error, TEXT("RegisterPlayer failed with code 0x%x"), result);
		delete Player;
		Player = NULL;
	}

	ProcessMuteList();

	return Player;
#endif

	return NULL;
	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::RemovePlayerItem
Desc:		Responsible for actually removing the specified PlayerReplicationInfo into the UIX player list.

notes:		None
last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::RemovePlayerItem( APlayerReplicationInfo* PRI )
{
	guard(UUIXInteraction::RemovePlayerItem);

#ifdef _XBOX
	if ( PRI == NULL )
		return;

	if ( m_pLiveEngine == NULL )
		return;

	AvarisPlayerItem* Player = FindPlayerItem(PRI);
	if ( Player == NULL )
	{
		debugf(NAME_DevLive, TEXT("UUIXInteraction::RemovePlayerItem - PRI not registered '%s'"), *PRI->PlayerName);
		return;
	}

	// Never remove the local player, just clear the reference to the local player's PRI
	if ( Player->GetPort() < 4 )
	{
		Player->SetPRI(NULL);
		return;
	}

	ILivePlayersList* pPlayers;
	if ( SUCCEEDED(m_pLiveEngine->GetFeatureInterface(UIX_PLAYERS_FEATURE, NULL, (LPVOID*)&pPlayers)) )
	{
		HRESULT result = pPlayers->UnregisterPlayer(Player);
		PRI->bRegisteredUIX = false;
		if (FAILED(result))
			debugf(NAME_Error, TEXT("UnregisterPlayer failed with code 0x%x"), result);
	}

	RegisteredPlayers.RemoveItem(Player);
	delete Player;
	Player = NULL;
#endif
	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::BeginFullQuery
Desc:		Begins a query to retrieve all stats from xbox live for the player stat object specified.

input:		Stats
				the PlayerStatCache object to retrieve stats for.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::BeginFullQuery( UPlayerStatCache* Stats ) 
{
	guard(UUIXInteraction::BeginFullQuery);

	if ( Stats == NULL )
	{
		debugf(TEXT("NULL Stats object specified for BeginFullQuery!"));
		return;
	}

	if ( Stats->eventBeginFullQuery(Query) )
	{
		Stats->bRefreshCache = false;
		if ( QueryDelaySeconds <= 0.f )
		{
			Query->Submit();
			if ( Query->HasPendingData() )
			{
				QueryDelaySeconds = STAT_QUERY_DELAY_SECONDS;
			}
		}
	}

	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::CleanupStatsQuery
Desc:		Cleans up any existing stats queries.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::CleanupStatsQuery() 
{
	guard(UUIXInteraction::CleanupStatsQuery);

	if ( Query != NULL )
	{
		delete Query;
		Query = NULL;
	}

	unguard;
}


#ifdef _XBOX
/*==========================================================================================================
function	UUIXInteraction::FindPlayerItem
Desc:		Returns the AvarisPlayerItem associated with the PlayerReplicationInfo specified by PRI, or
			NULL if no players in the list have that PRI

last modified by:	Ron Prestenback
==========================================================================================================*/
AvarisPlayerItem* UUIXInteraction::FindPlayerItem( const APlayerReplicationInfo* PRI ) const
{
	guard(UUIXInteraction::FindPlayerItem::APlayerReplicationInfo);

	if ( PRI == NULL )
		return NULL;

	for ( INT i = 0; i < RegisteredPlayers.Num(); i++ )
	{
		AvarisPlayerItem* Player = RegisteredPlayers(i);
		if ( Player && Player->GetPRI() == PRI )
			return Player;
	}

	return NULL;

	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::FindPlayerItem
Desc:		Returns the AvarisPlayerItem associated with the XUID specified by PlayerXUID, or
			NULL if no players in the list have that XUID

last modified by:	Ron Prestenback
==========================================================================================================*/
AvarisPlayerItem* UUIXInteraction::FindPlayerItem( const XUID* PlayerXUID ) const
{
	guard(UUIXInteraction::FindPlayerItem::XUID);


	if ( PlayerXUID == NULL )
		return NULL;

	for ( INT i = 0; i < RegisteredPlayers.Num(); i++ )
	{
		AvarisPlayerItem* Player = RegisteredPlayers(i);
		if ( Player )
		{
			if ( XOnlineAreUsersIdentical(PlayerXUID, Player->GetXUID()) )
				return Player;
		}
	}

	return NULL;

	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::FindPlayerItem
Desc:		Returns the AvarisPlayerItem associated with the player that owns the UNetConnection specified 
			by NetConnection, or NULL if no players in the list have that NetConnection.

notes:		This function is only useful for the server, since other players won't have UNetConnections on
			a client.
last modified by:	Ron Prestenback
==========================================================================================================*/
AvarisPlayerItem* UUIXInteraction::FindPlayerItem( const UNetConnection* NetConnection ) const
{
	guard(UUIXInteraction::FindPlayerItem::UNetConnection);

	if ( NetConnection == NULL )
		return NULL;

	for ( INT i = 0; i < RegisteredPlayers.Num(); i++ )
	{
		AvarisPlayerItem* Player = RegisteredPlayers(i);
		if ( Player )
		{
			APlayerReplicationInfo* PRI = Player->GetPRI();
			if ( PRI && PRI->Owner && PRI->Owner->IsAPlayerController() )
			{
				APlayerController* PC = (APlayerController*)PRI->Owner;
				if ( PC->Player == NetConnection )
					return Player;
			}
		}
	}

	return NULL;
	unguard;
}

UBOOL UUIXInteraction::IsPlayerTalking(const APlayerReplicationInfo* PRI) const
{
	guard(UUIXInteraction::IsPlayerTalking);

	AvarisPlayerItem* PlayerItem = FindPlayerItem(PRI);
	return (PlayerItem && PlayerItem->IsTalking());

	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::UpdateRemoteCommunicator
Desc:		Updates the local player list with the communicator status for the specified player.

input:		RemoteXUID
				xuid associated with remote player

			newState
				new communicator state

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::UpdateRemoteCommunicator( const XUID& RemoteXUID, DWORD newState )
{
	guard(UUIXInteraction::UpdateRemoteCommunicator);


	AvarisPlayerItem* Player = FindPlayerItem(&RemoteXUID);
	if ( Player )
	{
		if ( !XOnlineIsUserVoiceAllowed(RemoteXUID.dwUserFlags) )
			newState = COMM_Speaker;

		Player->SetVoiceStatus( (UIX_VOICE_STATUS_TYPE)newState );
	}
	else if ( m_pLiveEngine )
	{
		// determine if we're playing an instant action match, in which case we don't want to register
		if ( ViewportOwner && ViewportOwner->Actor )
		{
			APlayerController* PC = ViewportOwner->Actor;
			if ( PC->Level && PC->Level->NetMode == NM_Standalone )
			{
				return;
			}
		}

		// the voice chat engine received a status update from a player that hasn't been registered
		// by UIX yet.  In this situation, we'll add a placeholder AvarisPlayerItem for this player's
		// communicator status.  When the normal UIX registration occurs for this player, the remaining values
		// of the AvarisPlayerItem will be filled in there at that time.
		ILivePlayersList* pPlayers;
		if ( SUCCEEDED(m_pLiveEngine->GetFeatureInterface(UIX_PLAYERS_FEATURE, NULL, (LPVOID*)&pPlayers)) )
		{
			Player = new AvarisPlayerItem(RemoteXUID, TEXT(""), 255, NULL);
			HRESULT result = pPlayers->RegisterPlayer(Player);
			if (SUCCEEDED(result))
			{
				RegisteredPlayers.AddItem(Player);
				Player->SetVoiceStatus( (UIX_VOICE_STATUS_TYPE)newState );
			}
			else
			{
				debugf(NAME_Error, TEXT("RegisterPlayer in UpdateRemoteCommunicator failed with code 0x%X"), result);
				delete Player;
				Player = NULL;
			}
		}
	}

	unguard;
}

void UUIXInteraction::SetVoiceMailMode(UBOOL bVoiceThroughSpeakers)
{
	guard(UUIXInteraction::SetVoiceMailMode);

	m_pLiveEngine->SetProperty(UIX_PROPERTY_VOICE_MAIL_TO_SPEAKERS, bVoiceThroughSpeakers);

	unguard;
}

UBOOL UUIXInteraction::ShouldSendTo(const XUID& SrcPlayer, const XUID& DestPlayer)
{
	guard(UUIXInteraction::ShouldSendTo);

	AvarisPlayerItem* SrcPlayerItem = FindPlayerItem(&SrcPlayer);
	AvarisPlayerItem* DestPlayerItem = FindPlayerItem(&DestPlayer);
	if (SrcPlayerItem != NULL && DestPlayerItem != NULL)
	{
		APlayerReplicationInfo* SrcPlayerPRI = SrcPlayerItem->GetPRI();
		APlayerReplicationInfo* DestPlayerPRI = DestPlayerItem->GetPRI();

		if (SrcPlayerPRI != NULL && 
			DestPlayerPRI != NULL &&
			SrcPlayerPRI->bTeamChat &&
			SrcPlayerPRI->Team != NULL &&
			DestPlayerPRI->Team != NULL &&
			SrcPlayerPRI->Team != DestPlayerPRI->Team)
		{
			return FALSE;
		}
	}
	return TRUE;

	unguard;
}

void UUIXInteraction::UpdateMuteList(XONLINE_MUTELISTUSER* MuteList, DWORD MuteListSize)
{
	guard(UUIXInteraction::UpdateMuteList);

	// Clear the locally cached mutelist.
	appMemzero(CachedMuteList, sizeof(CachedMuteList));

	// Copy the mutelist to the locally cached mutelist.
	appMemcpy(CachedMuteList, MuteList, MuteListSize * sizeof(XONLINE_MUTELISTUSER));

	// Process any changes to the mutelist.
	ProcessMuteList();

	unguard;
}

void UUIXInteraction::ProcessMuteList()
{
	guard(UUIXInteraction::ProcessMuteList);

	UXHVVoiceSubsystem* Voice = UXHVVoiceSubsystem::getUXHVVoiceSubsystem();
	if (Voice == NULL) { return; }

	for (INT PlayerIdx = 0; PlayerIdx < RegisteredPlayers.Num(); PlayerIdx++)
	{
		AvarisPlayerItem* Player = RegisteredPlayers(PlayerIdx);
		if (Player == NULL) { continue;	}

		XUID PlayerXUID = *Player->GetXUID();
		BOOL PlayerFound = FALSE;

		for (DWORD MuteIdx = 0; MuteIdx < MAX_MUTELISTUSERS; MuteIdx++)
		{
			XUID MuteeXUID = CachedMuteList[MuteIdx].xuid;

			if (PlayerXUID.qwUserID == MuteeXUID.qwUserID)
			{
				PlayerFound = TRUE;
			}
		}

		if (PlayerFound)
		{
			Voice->MuteTalker(PlayerXUID);
		}
		else
		{
			Voice->UnMuteTalker(PlayerXUID);
		}
	}

	unguard;
}


// =====================================================================================================================
// =====================================================================================================================
//	Interaction Interface
// =====================================================================================================================
// =====================================================================================================================
/* Native key event handling. Body of function is only on the Xbox so key events are
generated by a gamepad. All keystrokes are copied to the UIX engine (as per UIX Samples
in the XDK). This interaction also hijacks the input when UIX is active so that the
basic menu system (and console) don't see the UIX input stream.
*/
UBOOL UUIXInteraction::NativeKeyEvent(BYTE& iKey, BYTE& State, FLOAT Delta )
{
	guard(UUIXInteraction::NativeKeyEvent);
#if _DEBUG
	if ( State != IST_Axis )
		debugf(NAME_DevBrian, TEXT("UUIXInteraction::NativeKeyEvent Key:%s State:%s"), Cast<UViewport>(ViewportOwner)->Input->GetKeyName(EInputKey(iKey)), GetKeyStateName(State));
#endif	// _DEBUG

	// IK_JoySlider1 = ControllerInserted, IK_JoySlider2 = ControllerRemoved
	// these should always be forwarded to the GUIController for processing
	if ( iKey == IK_JoySlider1 || iKey == IK_JoySlider2 )
	{
		if ( m_pLiveEngine )
			m_pLiveEngine->ClearInput();

		return false;
	}

	if ( !ShouldRenderUIX() )
		return false;

	if ( iKey == IK_JoyR )
	{
		Reset();
		return true;
	}

	// UIX is getting the input (during TickUIX) so Unreal should NOT process it. We consume the event.
	if (m_WhichScreen != SCREEN_NOT_UIX)
		return true;

	return false;
	unguard;
}

void  UUIXInteraction::NativeTick(FLOAT DeltaTime)
{
	guard(UUIXInteraction::NativeTick);
	TickUIX();

	if ( Query && QueryDelaySeconds > 0.f )
	{
		QueryDelaySeconds -= DeltaTime;
		if ( QueryDelaySeconds <= 0.f )
		{
			Query->Submit();
			if ( Query->HasPendingData() )
			{
				QueryDelaySeconds = STAT_QUERY_DELAY_SECONDS;
			}
		}
	}

	unguard;
}

void UUIXInteraction::NativePreRender(UCanvas* Canvas)
{
	guard(UUIXInteraction::NativePreRender);

	UGUIController* gui = findGUIController();

	// if the GUIController's ControllerMask is not 255, it means that the GUI is currently
	// displaying the "controller was removed message"...in this case, don't render UIX so
	// that the menu displaying the controllerremoved message appears by itself
	if ( ShouldRenderUIX() )
	{
		if ( gui && !gui->bActive &&
			(m_WhichScreen == SCREEN_NOT_UIX || m_WhichScreen == SCREEN_PLAYERS) )
		{
			FLOAT IconY = InviteIconY;
			if ( gui->ChildViewports.Num() > 1 )
			{
				// we're in splitscreen mode
				IconY = IconY / 2;
			}

			FLOAT X = InviteIconX;
			if ( bPendingFriendInvite )
			{
				if ( FriendInviteIcon->TimerCountdown > 0 )
				{
					FriendInviteIcon->TimerCountdown -= gui->RenderDelta;

					// time to show this icon again
					if ( FriendInviteIcon->TimerCountdown <= 0 )
						InitializeIconDisplay(FriendInviteIcon);
				}

				FriendInviteIcon->WinLeft = X;
				FriendInviteIcon->WinTop = IconY; 
				FriendInviteIcon->PreDraw(Canvas);
				X -= FriendInviteIcon->WinWidth + 5;
			}

			if ( bPendingGameInvite )
			{
				if ( GameInviteIcon->TimerCountdown > 0 )
				{
					GameInviteIcon->TimerCountdown -= gui->RenderDelta;

					// time to show this icon again
					if ( GameInviteIcon->TimerCountdown <= 0 )
						InitializeIconDisplay(GameInviteIcon);
				}

				GameInviteIcon->WinLeft = X;
				GameInviteIcon->WinTop = IconY; 
				GameInviteIcon->PreDraw(Canvas);
			}
		}

	}

	unguard;
}

void  UUIXInteraction::NativePostRender(UCanvas* Canvas)
{
	guard(UUIXInteraction::NativePostRender);

	UGUIController* gui = findGUIController();

	// if the GUIController's ControllerMask is not 255, it means that the GUI is currently
	// displaying the "controller was removed message"...in this case, don't render UIX so
	// that the menu displaying the controllerremoved message appears by itself
	if ( ShouldRenderUIX() )
	{
		if ( m_dwLiveWorkFlags & UIX_DOWORK_NEED_TO_RENDER )
		{
			// Make sure any pending writes to the screen are flushed
			Canvas->Flush();
			IDirect3DSurface8 * pSurface = 0;
			extern IDirect3DDevice8 * D3D__pDevice; // the global D3D device
			if (D3D__pDevice != NULL)
			{
				D3D__pDevice->GetBackBuffer( 0, D3DBACKBUFFER_TYPE_MONO, &pSurface );
			}

			BEGIN_D3D_EVENT(UIX)(PIX_CANVAS,TEXT("UIX"));
			m_pLiveEngine->Render( pSurface );
			END_D3D_EVENT;

			pSurface->Release();

			// scion sz - better safe..
			Canvas->Viewport->RenDev->ResetRenderStates();
		}

		if ( (gui == NULL || !gui->bActive) &&
			(m_WhichScreen == SCREEN_NOT_UIX || m_WhichScreen == SCREEN_PLAYERS) )
		{
			if ( bPendingFriendInvite )
				FriendInviteIcon->Draw(Canvas);

			if ( bPendingGameInvite )
				GameInviteIcon->Draw(Canvas);
		}
	}
	unguard;
}
#endif

// =====================================================================================================================
// =====================================================================================================================
// UnrealScript Interface
// =====================================================================================================================
// =====================================================================================================================
void UUIXInteraction::execfindGUIController( FFrame& Stack, RESULT_DECL )
{
	guard(UUIXInteraction::execfindGUIController);

	P_FINISH;

	*(UGUIController **)Result = findGUIController();

	unguardexec;
}

void UUIXInteraction::execEndFeature(FFrame& Stack, RESULT_DECL)
{
	guard(UUIXInteraction::execEndFeature);

	P_FINISH;

#ifdef _XBOX
	if ( m_pLiveEngine )
		m_pLiveEngine->EndFeature();

	//	TickUIX();
#endif

	unguardexec;
}

void UUIXInteraction::execRebootXbox( FFrame& Stack, RESULT_DECL )
{
	guard(UUIXnteraction::execRebootXbox);

	P_GET_BYTE(Reason);
	P_FINISH;

#ifdef _XBOX
	RebootXbox(ELaunchDestination(Reason));
#endif

	unguardexec;
}

void UUIXInteraction::execStartDownloader(FFrame& Stack, RESULT_DECL)
{
	guard(UUIXInteraction::execStartDownloader);
	P_FINISH;

#ifdef _XBOX
	StartDownloader();
#endif

	unguardexec;
}

void UUIXInteraction::execQueryFullStats(FFrame& Stack, RESULT_DECL )
{
	guard(UUIXInteraction::execQueryFullStats);

	P_GET_OBJECT(UPlayerStatCache,Stat);
	P_FINISH;

	BeginFullQuery(Stat);
	unguardexec;
}

void UUIXInteraction::execKillStatsQuery( FFrame& Stack, RESULT_DECL )
{
	guard(UUIXInteraction::execKillStatsQuery);

	P_FINISH;

	CleanupStatsQuery();
	unguardexec;
}

/*==========================================================================================================
function	UUIXInteraction::ActivateFriendsList
Desc:		Retrieves and makes available the friends list for the player.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::execActivateFriendsList( FFrame& Stack, RESULT_DECL ) 
{
	guard(UUIXInteraction::execActivateFriendsList);

	P_FINISH;

#ifdef _XBOX

	if ( m_pLiveEngine )
	{
		if ( LiveInterface != NULL )
		{
			if ( m_WhichScreen != SCREEN_FRIENDS )
			{
				DWORD ActualPort = getGamepadPort();
				XboxLiveUser* User = LiveInterface->GetUser(ActualPort);
				if ( User )
				{
					// ghetto-azz xbox live always uses port 0 for silent sign-in, and for this particular
					// function call, the "fake" port needs to be used
					// https://xds.xbox.com/BPProgInfo.asp?Page=content/prog_faq_online.htm#uix_4

					BYTE UserPort = User->GetPort();
					if ( SUCCEEDED(m_pLiveEngine->GetFriendsForUser(UserPort, &FriendsInterface)) )
					{
						*(UBOOL*)Result = 1;
						return;
					}
				}
			}
			else
			{
				debugf(TEXT("ActivateFriendsList: Cannot activate FriendsInterface while UIX is displaying the friends list!"));
			}
		}
	}

	*(UBOOL*)Result = 0;
#endif
	unguardexec;
}

/*==========================================================================================================
function	UUIXInteraction::DeactivateFriendsList
Desc:		Closes the friends list.  Called when we no longer need access to the friends list.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::execDeactivateFriendsList( FFrame& Stack, RESULT_DECL ) 
{
	guard(UUIXInteraction::execDeactivateFriendsList);

	P_FINISH;

#ifdef _XBOX

	if ( m_pLiveEngine && FriendsInterface )
	{
		FriendsInterface->Release();
		FriendsInterface = NULL;
	}
#endif


	unguardexec;
}

/*==========================================================================================================
function	UUIXInteraction::execFriendsListReady
Desc:		Simple script wrapper for ILiveFriendsInterface::IsReady

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::execFriendsListReady( FFrame& Stack, RESULT_DECL ) 
{
	guard(UUIXInteraction::execFriendsListReady);

	P_FINISH;

	*(UBOOL*)Result = 0;
#ifdef _XBOX
	if ( FriendsInterface )
	{
		*(UBOOL*)Result = FriendsInterface->IsReady();
	}
#endif

	unguardexec;
}

/*==========================================================================================================
function	UUIXInteraction::execFriendsListChanged
Desc:		Script wrapper for ILiveFriendsList::HasChanged

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::execFriendsListChanged( FFrame& Stack, RESULT_DECL ) 
{
	guard(UUIXInteraction::execFriendsListChanged);

	P_FINISH;

	*(UBOOL*)Result = 0;
#ifdef _XBOX
	if ( FriendsInterface )
	{
		*(UBOOL*)Result = FriendsInterface->HasChanged();
	}
#endif

	unguardexec;
}

void UUIXInteraction::execRefreshFriendsList( FFrame& Stack, RESULT_DECL )
{
	guard(UUIXInteraction::execRefreshFriendsList);

	P_FINISH;

#ifdef _XBOX
	if ( FriendsInterface )
	{
		FriendsInterface->Refresh();
	}
#endif
	unguardexec;
}

/*==========================================================================================================
function	UUIXInteraction::execGetFriendsList
Desc:		Get an array of LivePlayer structs representing the signed in player's friends list.

return:		Returns whether the friends list was successfully gotten.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::execGetFriendsList( FFrame& Stack, RESULT_DECL ) 
{
	guard(UUIXInteraction::execGetFriendsList);

	P_GET_TARRAY_REF(Friends,FLiveUser);
	P_FINISH;

	Friends->Empty();
	*(UBOOL*)Result = 0;

#ifdef _XBOX
	if ( FriendsInterface )
	{
		if ( FriendsInterface->IsReady() )
		{
			INT Count = FriendsInterface->Count();
			XONLINE_FRIEND Friend;
			for ( INT i = 0; i < Count; i++ )
			{
				if ( SUCCEEDED(FriendsInterface->GetFriendByIndex(i,&Friend)) )
				{
					// Make sure this is an actual friend. Users we have sent friend invites to/received invites from are still listed in the friends list, so we have to filter them out manually.
					UBOOL bIsRealFriend = !((Friend.dwFriendState & XONLINE_FRIENDSTATE_FLAG_RECEIVEDREQUEST) || (Friend.dwFriendState & XONLINE_FRIENDSTATE_FLAG_SENTREQUEST));
					if (bIsRealFriend)
					{
						INT idx = Friends->AddZeroed();
						FLiveUser& User = (*Friends)(idx);

						User.GamerTag = Friend.szGamertag;
						appMemcpy(User.XUID, &Friend.xuid.qwUserID, ARRAY_COUNT(User.XUID));
					}
				}
			}

			*(UBOOL*)Result = 1;
		}
		else
		{
			debugf(TEXT("%s: FriendsInterface is not ready!"), __FUNC_NAME__);
		}
	}
#endif

	unguard;
}

/*==========================================================================================================
function	UUIXInteraction::execIsAFriend
Desc:		Returns whether the specified player is in our friends list.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UUIXInteraction::execIsAFriend( FFrame& Stack, RESULT_DECL ) 
{
	guard(UUIXInteraction::execIsAFriend);

	P_GET_STRUCT_REF(FPlayerRungData,PlayerData);
	P_FINISH;

	*(UBOOL*)Result = 0;


#ifdef _XBOX
	if ( FriendsInterface )
	{
		if ( FriendsInterface->IsReady() )
		{
			UBOOL bIsFriend = 0;

			XONLINE_FRIEND Friend;
			XUID* PlayerXUID = (XUID*)PlayerData->XUID;
			if ( SUCCEEDED(FriendsInterface->GetFriendByXUID(*PlayerXUID, &Friend)) )
			{
				// Make sure this is an actual friend. Users we have sent friend invites to/received invites from are still listed in the friends list, so we have to filter them out manually.
				bIsFriend = !((Friend.dwFriendState & XONLINE_FRIENDSTATE_FLAG_RECEIVEDREQUEST) || (Friend.dwFriendState & XONLINE_FRIENDSTATE_FLAG_SENTREQUEST));
			}

			*(UBOOL*)Result = bIsFriend;
		}
		else
		{
			debugf(TEXT("%s: FriendsInterface is not ready!"), __FUNC_NAME__);
		}
	}
#else
	PlayerData->Rank = 0;
#endif

	unguardexec;
}

void UUIXInteraction::execDebugIconFade( FFrame& Stack, RESULT_DECL )
{
	guard(UUIXInteraction::execDebugIconFade);

	P_GET_UBOOL(bGameInvite);
	P_GET_FLOAT(NewIconDisplay);
	P_GET_FLOAT(NewIconFade);
	P_GET_FLOAT(NewIconRepeat);
	P_GET_FLOAT(NewFadeMultiplier);
	P_FINISH;

	IconDisplaySeconds = NewIconDisplay;
	IconFadeSeconds = NewIconFade;
	IconFadeMultiplier = NewFadeMultiplier;
	IconRepeatSeconds = NewIconRepeat;

	if ( bGameInvite )
		bPendingGameInvite = true;
	else bPendingFriendInvite = true;

	debugf(TEXT("%f Initializing %s icon - DisplaySeconds:%f FadeSeconds:%f RepeatSeconds:%f FadeMultiplier:%f"), ViewportOwner->Actor->Level->TimeSeconds, bGameInvite ? TEXT("game invite") : TEXT("friend request"), NewIconDisplay, NewIconFade, NewIconRepeat, NewFadeMultiplier);
	InitializeIconDisplay( bGameInvite ? GameInviteIcon : FriendInviteIcon );

	unguardexec;
}

