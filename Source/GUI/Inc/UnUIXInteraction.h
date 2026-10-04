#ifndef UnUIXInteraction_H
#define UnUIXInteraction_H

#include "GUI.h"

#ifndef UIX_MESSAGEMENU_PRIORITY
#define UIX_MESSAGEMENU_PRIORITY UCONST_UIX_MENU_PRIORITY + 10
#endif

// class declarations for PC side compilation
//scion bcl -- Xbox Live/UIX includes
#ifdef _XBOX
#include <xonline.h>
#include <uix.h>
#include <XBFont.h>
#include <xgraphics.h>
#include <xact.h>
#else
class ILiveEngine;
struct ITitleUIPlugin;
struct _XONLINE_USER;
struct XONLINE_FRIEND;
struct LiveFriendsList;
class CXBFont;
#if !defined(_WIN32)
// Off Windows there is no <wtypes.h> to borrow these from.
typedef QWORD ULONGLONG;
typedef INT   HRESULT;
typedef INT   BOOL;
#ifndef S_OK
#define S_OK ((HRESULT)0)
#endif
#endif
//bcl epic -- Have a TArray of these things...need a definition for the type on PC
typedef struct
{
	ULONGLONG qwUserID;
	ULONGLONG qwTeamID;
	DWORD dwUserFlags;
} XUID;
#endif

class UIXFont;
#ifdef _XBOX
//-----------------------------------------------------------------------------
// Name: class UIXFont
// Desc: A wrapper around a CXBFont class that works with UIX
//-----------------------------------------------------------------------------
class UIXFont : public ITitleFontRenderer
{
	CXBFont* m_pFont;
	DWORD    m_dwCurrentColor;

public:
	UIXFont( CXBFont* pFont )
	{ 
		m_pFont          = pFont;
		m_dwCurrentColor = 0xffffffff;
	}

	virtual ULONG __stdcall Release()
	{ 
		return 0; 
	}

	virtual HRESULT __stdcall SetHeight( DWORD Height ) 
	{ 
		FLOAT fScale = (FLOAT)Height/m_pFont->m_fFontYAdvance;
		m_pFont->SetScaleFactors( fScale, fScale );
		return S_OK;
	}

	virtual HRESULT __stdcall SetColor( D3DCOLOR Color )
	{
		m_dwCurrentColor = (DWORD)Color;
		return S_OK;
	}

	virtual HRESULT __stdcall DrawText( const WCHAR* pText,
		DWORD X, DWORD Y, DWORD dwMaxWidth )
	{  
		if( dwMaxWidth == 0 )
			return m_pFont->DrawText( (FLOAT)X, (FLOAT)Y, m_dwCurrentColor, pText );
		else
			return m_pFont->DrawText( (FLOAT)X, (FLOAT)Y, m_dwCurrentColor, pText,
			XBFONT_TRUNCATED, (FLOAT)dwMaxWidth );
	}

	virtual HRESULT __stdcall GetTextSize( const WCHAR* pText,
		DWORD* pWidth, DWORD* pHeight )
	{
		FLOAT fWidth;
		FLOAT fHeight;
		m_pFont->GetTextExtent( pText, &fWidth, &fHeight );
		(*pWidth)  = (DWORD)fWidth;
		(*pHeight) = (DWORD)fHeight;
		return S_OK;
	}
};
#endif
//-----------------------------------------------------------------------------
// Enum for tracking which screen/mode we are in.
//-----------------------------------------------------------------------------
enum WHICH_SCREEN
{
	SCREEN_NOT_UIX,
	SCREEN_INTRO,               // Intro screen/movie, shown prior to first button press
	SCREEN_MAIN,                // Main menu - branches to live menu and other game features
	SCREEN_LIVE,                // The live menu - access to friends feature
	SCREEN_LOGON,               // Logging on with UIX mode
	SCREEN_FRIENDS,             // Friends with UIX mode

	// Players list screens (UIXPlayers.h)
	SCREEN_LOBBY,               // Using the player's list to build a list of players
	SCREEN_MYTEAM_ONLY,         // Showing one team only, using bit flags
	SCREEN_GUEST_VIEW,          // The guest view, with no feedback options
	SCREEN_PLAYERS, // Typical start-menu players list
	SCREEN_DEPARTED_PLAYERS_ONLY,   // Showing departed players only

	// An options screen that can be invoked from the lobby
	SCREEN_OPTIONS_OVERLAY,     // Options menu that is displayed while the players feature is running
	SCREEN_COUNT,               // Total number of screens
	SCREEN_FORCE32 = 0x7FFFFFFF // force to 32 bits (for script matching) 
};

// The ITitlePlayerListItem interface for PRI's
class AvarisPlayerItem;

// ----- The manager class; defined for use in/out of _XBOX
class XboxLiveManager;

enum EOnlineState
{
	ONLINE_None		=0,
	ONLINE_Pending	=1,
	ONLINE_Success	=2,
	ONLINE_Failed	=3,
	ONLINE_MAX		=4,
};

enum ELoginFailure
{
	ERROR_None					=0,
	ERROR_NoAccounts			=1,
	ERROR_SilentLoginDisabled	=2,
	ERROR_NeedPasscode			=3,
	ERROR_DuplicateLogin		=4,
	ERROR_ConnectionLost		=5,
	ERROR_MAX					=6,
};

enum ECrossTitleInvite
{
	CTI_None                =0,
	CTI_MustConfirm         =1,
	CTI_Confirming          =2,
	CTI_Confirmed           =3,
	CTI_PendingLogin		=4,
	CTI_IncorrectAccount	=5,
	CTI_Done				=6,
	CTI_MAX                 =7,
	CTI_MakeDWORD = 0x7fffffff,
};

enum ELaunchDestination
{
	LAUNCH_Memory					=0,
	LAUNCH_NewAccount				=1,
	LAUNCH_AccountManagement		=2,
	LAUNCH_NetworkTroubleshooter	=3,
	LAUNCH_UIX						=4,
	LAUNCH_MAX						=5,
};

#define CODE_CrossTitleInvite		1000
#define CODE_UserHasMessage			1001
#define CODE_DownloadContent		1002
#define CODE_UIXReboot				1003

#define STATE_Confirmation			2001

#define LIVE_NoConnection			3000
#define LIVE_ConnectionLost			3001
#define LIVE_ServiceUnavailable		3002
#define LIVE_UpdateRequired			3003
#define LIVE_TooBusy				3004

struct UUIXInteraction_eventShowQuestion_Parms
{
	INT MessageCode;
	BYTE Priority;
	FString MessageString;
	BITFIELD ReturnValue;
};
struct UUIXInteraction_eventShowMessage_Parms
{
	FString Message;
	BYTE Priority;
	BITFIELD ReturnValue;
};
struct UUIXInteraction_eventCloseUIXMenu_Parms
{
	BITFIELD bSingleMenuOnly;
};
// delay before registering; supposed to account for loading/precaching delays so that the
// UIX engine is not started and then NOT pumped. Wait, then start the process so the engine
// can DoWork();
const int REGISTRATION_COUNTDOWN_SECONDS = 60;
class GUI_API UUIXInteraction : public UAbstractUIXInteraction
{
public:
	INT getGamepadPort();

protected:
	static UUIXInteraction*		theOnlyUIX;
	static UGUIController*		staticGUIController;

	void SetOnlineState( EOnlineState NewState );
	AvarisPlayerItem* AddPlayerItem( APlayerReplicationInfo* PRI, BYTE GamepadPort );
	void RemovePlayerItem( APlayerReplicationInfo* PRI );

	DECLARE_CLASS(UUIXInteraction,UAbstractUIXInteraction,0|CLASS_Transient,GUI)
	NO_DEFAULT_CONSTRUCTOR(UUIXInteraction)

	// Variables/state needed for UIX
	CXBFont*			m_pFont;            // Font object
	ILiveEngine*		m_pLiveEngine;      // The UIX engine
	ITitleUIPlugin*		m_pUIPlugin;        // The plugin for UI rendering
	DWORD				m_dwLiveWorkFlags;  // Flags return from DoWork() and used elsewhere.
	WHICH_SCREEN		m_WhichScreen;
	XboxLiveManager*	LiveInterface;
	LiveFriendsList*	FriendsInterface;
	INT					m_RegisterCountdown;
	ECrossTitleInvite	m_CTIState;
	INT					UIXState;

	UPlayerStatCache*	StatCache;
	UStatsQuery*		Query;

	INT					InviteIconX, InviteIconY;
	UGUIImage*			GameInviteIcon;
	UGUIImage*			FriendInviteIcon;



	// number of seconds to display icon at full alpha before fading begins
	FLOAT				IconDisplaySeconds;

	// number of seconds to fade
	FLOAT				IconFadeSeconds;

	// multiplier to use when fading (see GUIComponent.ColorRecord)
	FLOAT				IconFadeMultiplier;

	// number of seconds to wait after displaying an icon before it will be displayed again
	// (assuming the invite is still active)
	FLOAT				IconRepeatSeconds;

	FLOAT				LastNetworkCheck;
	FLOAT				QueryDelaySeconds;

	FString				ConfirmationMessage;

	BYTE				OnlineState  GCC_PACK(4);
	BYTE				LoginFailureReason;
	BITFIELD			bNetworkActive:1 GCC_PACK(4);
	BITFIELD			bStateChanged:1;				// need to call UpdateState
	BITFIELD			bPendingFriendInvite:1;
	BITFIELD			bPendingGameInvite:1;

	static TArray<AvarisPlayerItem *> RegisteredPlayers;

	static UBOOL IsNetworkActive();
	static UUIXInteraction* getUUIXInteraction() { return theOnlyUIX; }

	UBOOL ShouldRenderUIX()
	{
		if ( UIXState == STATE_Confirmation )
			return false;

		UGUIController* gui = findGUIController();
		return gui != NULL && gui->ControllerMask == 255;
	}

	void InitUIXVoiceSupport();

	void TickUIX();			// give a slice of time to the UIX interface
	void HandleFeatureExit(); // the UIX state handler
	void Reset();
	UBOOL IsSupportedLanguage( DWORD& XLanguage ) const;

	void SetPendingGameInvite( UBOOL bValue );
	void SetPendingFriendRequest( UBOOL bValue );
	void InitializeIconDisplay( UGUIImage* Image );

	/* epic ====================================================================================================
	* Function:	UUIXInteraction::StartDownloader
	* Author:	Per Vognsen
	*
	* Starts the downloadable content manager.
	* ==========================================================================================================
	*/
	void StartDownloader();

#ifdef _XBOX
	void RebootXbox( ELaunchDestination Reason );
	void ShowFriendStates();
	UBOOL IsCurrentSession( const XNKID& SessionID ) const;

	// Tick stuff
	void UpdateNetworkStatus();
	void ProcessCrossTitleInvites();
	void UpdateUIXScreen();
	void UpdateGamepadStates();
	void UpdateGameInviteStatus();
	void ProcessLiveWorkTasks();
	void ProcessPendingNotifications( DWORD GamepadPort, DWORD Notifications );

	void HandleLoginSuccess();
	void HandleLoginFailure( HRESULT hr );
	AvarisPlayerItem* FindPlayerItem( const APlayerReplicationInfo* PRI ) const;
	AvarisPlayerItem* FindPlayerItem( const XUID* PlayerXUID ) const;
	AvarisPlayerItem* FindPlayerItem( const UNetConnection* NetConnection ) const;
	virtual UBOOL IsPlayerTalking(const APlayerReplicationInfo* PRI) const;

	void UpdateRemoteCommunicator( const XUID& playerXUID, DWORD newState );

	void SetVoiceMailMode(UBOOL bVoiceThroughSpeakers);

	UBOOL ShouldSendTo(const XUID& SrcPlayer, const XUID& DestPlayer);

	static XONLINE_MUTELISTUSER CachedMuteList[MAX_MUTELISTUSERS];
	void UpdateMuteList(XONLINE_MUTELISTUSER* MuteList, DWORD MuteListSize);
	void ProcessMuteList();
#endif

	UGUIController* findGUIController();

	// Since we permit only one log on to live we can figure out THE XUID for the logged on player.
	// Return value is valid ONLY if bLoggedOn is true
	XUID GetXUID();

	void VoiceCommunicatorUpdate(DWORD dwUser);
	HRESULT StartPlayersList( BOOL LobbyMode, BOOL DisplayDeparted, BOOL DisplayActive, DWORD portNumber, int FilterFlags );

	void RegisterPRI( APlayerReplicationInfo* PRI );
	void UnregisterPRI( APlayerReplicationInfo* PRI );
	void RegisterPlayer( APlayerController* PC );

	void BeginFullQuery( UPlayerStatCache* Stats );
	void CleanupStatsQuery();

// =====================================================================================================================
// =====================================================================================================================
// AbstractUIXInteraction Interface
// =====================================================================================================================
// =====================================================================================================================
	UAbstractUIXInteraction* GetUIXInteraction() { return getUUIXInteraction(); }
	void InitUIX();
	void DestroyUIX();

	void StartSignIn(UBOOL bSilent = 0);
	void StartSignOut();

	UBOOL SignedIn() { return OnlineState == ONLINE_Success; }
	void GetGamerTag( INT GamepadID, FString& out_GamerTag );

	void UpdateOnlineState();
	void RegisterSession();
	void UpdateSession();
	void EndSession(UBOOL bBlocking);

	void ShowFriends( INT GamepadID );
	void ShowPlayers( INT GamepadID );

// =====================================================================================================================
// =====================================================================================================================
//	Interaction Interface
// =====================================================================================================================
// =====================================================================================================================
	void Initialize();
#ifdef _XBOX
	void NativeTick(FLOAT DeltaTime);
	void NativePreRender(UCanvas* Canvas);
	void NativePostRender(UCanvas* Canvas);
	UBOOL NativeKeyEvent(BYTE& iKey, BYTE& State, FLOAT Delta );
#endif

// =====================================================================================================================
// =====================================================================================================================
// UnrealScript Interface
// =====================================================================================================================
// =====================================================================================================================
	DECLARE_FUNCTION(execStartDownloader);
	DECLARE_FUNCTION(execRebootXbox);
	DECLARE_FUNCTION(execEndFeature);
	DECLARE_FUNCTION(execfindGUIController);
	DECLARE_FUNCTION(execGetUIXInteraction);
	DECLARE_FUNCTION(execDebugIconFade);

	DECLARE_FUNCTION(execActivateFriendsList);
	DECLARE_FUNCTION(execDeactivateFriendsList);

	DECLARE_FUNCTION(execFriendsListReady);
	DECLARE_FUNCTION(execFriendsListChanged);
	DECLARE_FUNCTION(execRefreshFriendsList);

	DECLARE_FUNCTION(execGetFriendsList);
	DECLARE_FUNCTION(execIsAFriend);
	DECLARE_FUNCTION(execQueryFullStats);
	DECLARE_FUNCTION(execKillStatsQuery);

	BITFIELD eventShowQuestion(INT MessageCode, BYTE Priority, const FString MessageString = TEXT(""))
	{
		UUIXInteraction_eventShowQuestion_Parms Parms;
		Parms.ReturnValue=0;
		Parms.MessageCode=MessageCode;
		Parms.Priority=Priority;
		Parms.MessageString=MessageString;
		ProcessEvent(FindFunctionChecked(TEXT("ShowQuestion")),&Parms);
		return Parms.ReturnValue;
	}
	BITFIELD eventShowMessage(const FString& Message, BYTE Priority)
	{
		UUIXInteraction_eventShowMessage_Parms Parms;
		Parms.ReturnValue=0;
		Parms.Message=Message;
		Parms.Priority=Priority;
		ProcessEvent(FindFunctionChecked(TEXT("ShowMessage")),&Parms);
		return Parms.ReturnValue;
	}
	void eventCloseUIXMenu(BITFIELD bSingleMenuOnly)
	{
		UUIXInteraction_eventCloseUIXMenu_Parms Parms;
		Parms.bSingleMenuOnly=bSingleMenuOnly;
		ProcessEvent(FindFunctionChecked(TEXT("CloseUIXMenu")),&Parms);
	}
};

extern TCHAR* GetKeyStateName( BYTE State );
extern TCHAR* GetXCodeName( BYTE XCode );

#endif //UnUIXInteraction_H

