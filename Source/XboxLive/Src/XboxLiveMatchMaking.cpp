#if __STATIC_LINK
#include "UCGameplay.h"
#endif


#include <xtl.h>
#include "XboxLive.h"
#include "UnTcpNetDriverXbox.h"
#include "FContentManager.h"
#include "UnPlayerStats.h"

#ifndef FINAL_RELEASE
TCHAR* GetStateFlagText( DWORD State )
{
	TCHAR* c = appStaticString1024();
	CHECKFLAG(XONLINE_FRIENDSTATE_FLAG_NONE);
	CHECKFLAG(XONLINE_FRIENDSTATE_FLAG_INVITEACCEPTED);
	CHECKFLAG(XONLINE_FRIENDSTATE_FLAG_INVITEREJECTED);
	CHECKFLAG(XONLINE_FRIENDSTATE_FLAG_JOINABLE);
	CHECKFLAG(XONLINE_FRIENDSTATE_FLAG_ONLINE);
	CHECKFLAG(XONLINE_FRIENDSTATE_FLAG_PLAYING);
	CHECKFLAG(XONLINE_FRIENDSTATE_FLAG_RECEIVEDINVITE);
	CHECKFLAG(XONLINE_FRIENDSTATE_FLAG_RECEIVEDREQUEST);
	CHECKFLAG(XONLINE_FRIENDSTATE_FLAG_SENTINVITE);
	CHECKFLAG(XONLINE_FRIENDSTATE_FLAG_SENTREQUEST);
	CHECKFLAG(XONLINE_FRIENDSTATE_FLAG_VOICE);

	return c;
}
#endif

enum { ParamPage = 0x02000030 };
enum { NumRungScoresPerPage = 20 };

enum Dataset
{
	DatasetRungs = 1
};

enum RungProcedure
{
	ProcGetRungScores = 0x100,
	ProcRemoveForUser = 0x001,
};

enum RungAttributeType
{
	AttribScoresBase	= 0x00000001,
	AttribDateWritten	= 0x00000020,
	AttribPage			= 0x00000030,
};

static const XONLINE_ATTRIBUTE_SPEC RungResultsSpecs[NumRungScoresPerPage + 1] =
{
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof(ULONGLONG) },
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof(ULONGLONG) },
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof(ULONGLONG) },
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof(ULONGLONG) },
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof(ULONGLONG) },
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof(ULONGLONG) },
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof(ULONGLONG) },
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof(ULONGLONG) },
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof(ULONGLONG) },
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof(ULONGLONG) },
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof(ULONGLONG) },
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof(ULONGLONG) },
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof(ULONGLONG) },
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof(ULONGLONG) },
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof(ULONGLONG) },
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof(ULONGLONG) },
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof(ULONGLONG) },
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof(ULONGLONG) },
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof(ULONGLONG) },
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof(ULONGLONG) },
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof(ULONGLONG) },
};

struct RungResults
{
	ULONGLONG EntityID;
	ULONGLONG Scores[NumRungScoresPerPage];
};

void FMatchSessionResult::Parse( FbeaconInformation& ServerData )
{
	guard(FMatchSessionResult::Parse);

	appMemzero(&ServerData, sizeof(FbeaconInformation));

	UGUIController* gui = UUIXInteraction::getUUIXInteraction()->findGUIController();

	ServerData.GameTypeName = gui->eventGetGametypePrefix(GameType);
	ServerData.ServerName = appFromUnicode(Host);
	ServerData.MapName = appFromUnicode(Map);
	ServerData.CurrentPlayers = FString::Printf(TEXT("%d"), PublicFilled + PrivateFilled);
	ServerData.MaxPlayers = FString::Printf(TEXT("%d"), PublicFilled + PublicOpen + PrivateFilled + PrivateOpen);
	ServerData.Mutators = appFromUnicode(Mutators);
	ServerData.Purity = FString::Printf(TEXT("%d"), Purity);
	ServerData.MaplistName = appFromUnicode(Maplist);
	ServerData.AveragePlayerSkill = AvgPlayerSkill;
	BytesToString( (BYTE*)&SessionID, sizeof(XNKID), ServerData.UniqueAddress );

	unguard;
}

// =====================================================================================
// SessionStartTask
// =====================================================================================

UBOOL SessionStartTask::Finished() 
{
	guard(SessionStartTask::Finished);

	debugf( NAME_DevLive, TEXT("> SessionStartTask::Finished()"));
	UGameEngine* gameEngine = LiveInterface->getEngine();
	UTcpNetDriverXbox* net = LiveInterface->getNet();

	XNKID   SessionID;
	XNKEY   ExchangeKey;

	// Extract the new session ID
	HRESULT hr = XOnlineMatchSessionGetInfo( m_hOnlineTask, &SessionID, &ExchangeKey );

	// by documentation, XOnlineMatchSessionGetInfo cannot fail; always returns S_OK.
	if( XNetRegisterKey( &SessionID, &ExchangeKey ) )
	{
		debugf( NAME_Error, TEXT("XNetRegisterKey failed."));
		LiveInterface->m_MatchState = XboxLiveManager::MM_SESSION_ERROR;
		Close();
		return true;
	}

	net->SetHostKey( &SessionID, &ExchangeKey );

	LiveInterface->m_MatchState = XboxLiveManager::MM_SESSION_ACTIVE;
	LiveInterface->m_SessionUpdateTimer = appSeconds() + SESSION_UPDATE_INTERVAL;

#ifndef FINAL_RELEASE
	{
		FString SessionID, ExchangeKey, HostAddr;

		BytesToString((BYTE*)&net->GetHostInfo()->SessionID, sizeof(net->GetHostInfo()->SessionID), SessionID);
		BytesToString((BYTE*)&net->GetHostInfo()->ExchangeKey, sizeof(net->GetHostInfo()->ExchangeKey), ExchangeKey);
		BytesToString((BYTE*)&net->GetHostInfo()->HostAddr, sizeof(net->GetHostInfo()->HostAddr), HostAddr);

		debugf( NAME_DevLive, TEXT("SessionStartTask::Finsh: SessionID: %s"), *SessionID );
		debugf( NAME_DevLive, TEXT("SessionStartTask::Finsh: ExchangeKey: %s"), *ExchangeKey );
		debugf( NAME_DevLive, TEXT("SessionStartTask::Finsh: HostAddr: %s"), *HostAddr );
	}
#endif

	for( DWORD dwUser = 0; dwUser < XONLINE_MAX_LOGON_USERS; dwUser++ )
		LiveInterface->UpdatePlayerOnlineState( dwUser );

	return true;
	unguard;
}

// =====================================================================================
// SessionUpdateTask
// =====================================================================================
const float SessionUpdateTask::SESSION_UPDATE_INTERVAL = 10.0;

HRESULT SessionUpdateTask::Continue()
{
	if (appSeconds() > m_UpdateTimer) 
		LiveInterface->UpdateSession();
	if (m_hOnlineTask) 
		return XOnlineTaskContinue( m_hOnlineTask );
	else 
		return S_OK;
}

UBOOL SessionUpdateTask::Finished()
{
	guard(SessionUpdateTask::Finished());
	return true;
	unguard;
}

// =====================================================================================
// SessionEndTask
// =====================================================================================

UBOOL SessionEndTask::Finished()
{
	guard(SessionEndTask::Finished());
	LiveInterface->m_MatchState = XboxLiveManager::MM_SESSION_NONE;
	LiveInterface->m_Attributes.Clear();
	return true;
	unguard;
}

// =====================================================================================
// QuickMatchQueryTask
// =====================================================================================
// The order of these fields must exactly match the order the order the fields in FMatchSessionResult
const XONLINE_ATTRIBUTE_SPEC QuickMatchQueryTask::MatchAnySessionAttributeSpec[]=
{
	{ X_ATTRIBUTE_DATATYPE_STRING, ( XATTRIB_HOST_MAX_LEN + 1 ) * sizeof( WCHAR ) }, // Host
	{ X_ATTRIBUTE_DATATYPE_STRING, ( XATTRIB_MAP_MAX_LEN + 1 ) * sizeof( WCHAR ) }, // Map
	{ X_ATTRIBUTE_DATATYPE_STRING, ( XATTRIB_MUTATORS_MAX_LEN + 1 ) * sizeof( WCHAR ) }, // Mutators
	{ X_ATTRIBUTE_DATATYPE_STRING, ( XATTRIB_MAPLIST_MAX_LEN + 1 ) * sizeof( WCHAR ) }, // Maplist
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof( ULONGLONG ) }, // GameType
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof( ULONGLONG ) }, // Purity
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof( ULONGLONG ) }, // Dedicated
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof( ULONGLONG ) }, // CustomContent
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof( ULONGLONG ) }, // AvgPlayerSkill
};

UBOOL QuickMatchQueryTask::Finished() 
{
	guard(QuickMatchQueryTask::Finished());

	debugf(NAME_DevLive, TEXT("> %s"), __FUNC_NAME__);

	/* There are two times when this task is finished: When the server has provided a list of 
	   matching servers (m_State == TS_Query) and when the task has finished probing quality of service QoS
	   (m_State == TS_Probing).
    */

	if (m_State == TS_Query)
	{
		UGameEngine* gameEngine = LiveInterface->getEngine();
		UTcpNetDriverXbox* net = LiveInterface->getNet();

		MatchQueryResults* results = new MatchQueryResults();
		LiveInterface->SetTaskHandle<MatchQueryResults>(LiveInterface->m_phMatchResults, results);

		if ( results )
		{
			MatchQueryResults& QueryResults = *results;
			PXONLINE_MATCH_SEARCHRESULT* ppSearchResults;
			DWORD dwNumSessions;

			HRESULT hr = XOnlineMatchSearchGetResults( m_hOnlineTask, &ppSearchResults, &dwNumSessions );
			if( SUCCEEDED( hr ) )
			{
				QueryResults.m_Size = dwNumSessions;

				// ----------------------------------------------------------------------
				debugf( NAME_DevLive, TEXT("QuickMatch Succeeded with %d results."), QueryResults.size() );
				// ----------------------------------------------------------------------
				for( DWORD i=0; i < dwNumSessions; ++i )
				{
					XONLINE_MATCH_SEARCHRESULT* pxms = ppSearchResults[i];
					FMatchSessionResult& Match = QueryResults[i];

					Match.SessionID = pxms->SessionID;
					Match.KeyExchangeKey = pxms->KeyExchangeKey;
					Match.HostAddress = pxms->HostAddress;
					Match.PublicOpen = pxms->dwPublicOpen;
					Match.PrivateOpen = pxms->dwPrivateOpen;
					Match.PublicFilled = pxms->dwPublicFilled;
					Match.PrivateFilled = pxms->dwPrivateFilled;
					hr = XOnlineMatchSearchParse( ppSearchResults[i], ARRAY_COUNT(MatchAnySessionAttributeSpec),
						MatchAnySessionAttributeSpec, &Match );
					check(SUCCEEDED(hr));
					debugf(NAME_DevLive, TEXT("Match[%i]:%s (pub:%i/%i - priv:%i/%i - purity:%i - maplist:%s)"), i, appFromUnicode(Match.Map), Match.PublicFilled, (Match.PublicFilled + Match.PublicOpen), Match.PrivateFilled, (Match.PrivateFilled + Match.PrivateOpen), Match.Purity, appFromUnicode(Match.Maplist) );
//					debugf(NAME_DevLive, TEXT("Match[%i]:%s (pub:%i/%i - priv:%i/%i - purity:%i - custom:%i - maplist:%s)"), i, appFromUnicode(Match.Map), Match.PublicFilled, (Match.PublicFilled + Match.PublicOpen), Match.PrivateFilled, (Match.PrivateFilled + Match.PrivateOpen), Match.Purity, Match.CustomContent, appFromUnicode(Match.Maplist) );
				}
			}
			else
			{
				warnf(TEXT("Failure in %s: %s (0x%08X)"), __FUNC_NAME__, ParseXOnlineResult(hr), hr);
				return true;
			}
		}
	}

	Close();

	// Notify the Xbox Live interface of the query's completion
	return LiveInterface->QueryComplete(TRUE, Gametype, bIncludeCustom, Purity);

	unguard;
}

// =====================================================================================
// OptiMatchQueryTask
// =====================================================================================
// The order of these fields must exactly match the order the order of the fields in FMatchSessionResult
const XONLINE_ATTRIBUTE_SPEC OptiMatchQueryTask::MatchOptimatchAttributeSpec[]=
{
	{ X_ATTRIBUTE_DATATYPE_STRING, ( XATTRIB_HOST_MAX_LEN + 1 ) * sizeof( WCHAR ) }, // Host
	{ X_ATTRIBUTE_DATATYPE_STRING, ( XATTRIB_MAP_MAX_LEN + 1 ) * sizeof( WCHAR ) }, // Map
	{ X_ATTRIBUTE_DATATYPE_STRING, ( XATTRIB_MUTATORS_MAX_LEN + 1 ) * sizeof( WCHAR ) }, // Mutators
	{ X_ATTRIBUTE_DATATYPE_STRING, ( XATTRIB_MAPLIST_MAX_LEN + 1 ) * sizeof( WCHAR ) }, // Maplist
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof( ULONGLONG ) }, // GameType
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof( ULONGLONG ) }, // Purity
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof( ULONGLONG ) }, // Dedicated
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof( ULONGLONG ) }, // CustomContent
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof( ULONGLONG ) }, // AvgPlayerSkill
};

UBOOL OptiMatchQueryTask::Finished()
{
	guard(OptiMatchQueryTask::Finished());

	debugf(NAME_DevLive, TEXT("> %s"), __FUNC_NAME__);

	/* There are two times when this task is finished: When the server has provided a list of 
	matching servers (m_State == TS_Query) and when the task has finished probing quality of service QoS
	(m_State == TS_Probing).
	*/

	if (m_State == TS_Query)
	{
		UGameEngine* gameEngine = LiveInterface->getEngine();
		UTcpNetDriverXbox* net = LiveInterface->getNet();

		MatchQueryResults* results = new MatchQueryResults();
		LiveInterface->SetTaskHandle<MatchQueryResults>(LiveInterface->m_phMatchResults, results);
		if ( results )
		{
			MatchQueryResults& QueryResults = *results;
			PXONLINE_MATCH_SEARCHRESULT* ppSearchResults;
			DWORD dwNumSessions;

			HRESULT hr = XOnlineMatchSearchGetResults( m_hOnlineTask, &ppSearchResults, &dwNumSessions );
			if( SUCCEEDED(hr) )
			{
				QueryResults.m_Size = dwNumSessions;

				// ----------------------------------------------------------------------
				debugf(NAME_DevLive, TEXT("OptiMatch Succeeded with %i results."), QueryResults.size() );
				// ----------------------------------------------------------------------
				for( DWORD i=0; i < dwNumSessions; ++i )
				{
					XONLINE_MATCH_SEARCHRESULT* pxms = ppSearchResults[i];
					FMatchSessionResult& Match = QueryResults[i];

					Match.SessionID = pxms->SessionID;
					Match.KeyExchangeKey = pxms->KeyExchangeKey;
					Match.HostAddress = pxms->HostAddress;
					Match.PublicOpen = pxms->dwPublicOpen;
					Match.PrivateOpen = pxms->dwPrivateOpen;
					Match.PublicFilled = pxms->dwPublicFilled;
					Match.PrivateFilled = pxms->dwPrivateFilled;

					hr = XOnlineMatchSearchParse( ppSearchResults[i], ARRAY_COUNT(MatchOptimatchAttributeSpec),
						MatchOptimatchAttributeSpec, &Match );

					check(SUCCEEDED(hr));
					debugf(NAME_DevLive, TEXT("Match[%i]:%s (pub:%i/%i - priv:%i/%i - purity:%i - maplist:%s)"), i, appFromUnicode(Match.Map), Match.PublicFilled, (Match.PublicFilled + Match.PublicOpen), Match.PrivateFilled, (Match.PrivateFilled + Match.PrivateOpen), Match.Purity, appFromUnicode(Match.Maplist) );
				}
			}
			else
			{
				warnf(TEXT("Failure in %s: %s (0x%08X)"), __FUNC_NAME__, ParseXOnlineResult(hr), hr);
				return true;
			}
		}
	}

	Close();

	// Notify the Xbox Live interface of the query's completion.
	return LiveInterface->QueryComplete();
	unguard;
}

// =====================================================================================
// SessionQueryTask
// =====================================================================================
// Attribute layout for MatchBySessionID return attributes.
// This must match the order of both the query return attributes
// and the fields specified in the CMatchBySessionIDResult class.
const XONLINE_ATTRIBUTE_SPEC SessionQueryTask::MatchBySessionIDAttributeSpec[]=
{
	{ X_ATTRIBUTE_DATATYPE_STRING, ( XATTRIB_HOST_MAX_LEN + 1 ) * sizeof( WCHAR ) }, // Host
	{ X_ATTRIBUTE_DATATYPE_STRING, ( XATTRIB_MAP_MAX_LEN + 1 ) * sizeof( WCHAR ) }, // Map
	{ X_ATTRIBUTE_DATATYPE_STRING, ( XATTRIB_MUTATORS_MAX_LEN + 1 ) * sizeof( WCHAR ) }, // Mutators
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof( ULONGLONG ) }, // GameType
};
UBOOL SessionQueryTask::Finished()
{
	guard(SessionQueryTask::Finished());

	// Get the list returned by the matchmaking server
	PXONLINE_MATCH_SEARCHRESULT *ppSearchResult;
	DWORD dwResults; // number of results
	HRESULT hr = XOnlineMatchSearchGetResults( m_hOnlineTask, &ppSearchResult, &dwResults );

	// Handle errors
	if( FAILED(hr) )
	{
		Close();
		return true;
	}

	/*
	If we fail, we will try again (if that is possible). 
	*/
	if( dwResults ) // more than 1 could be bad - retry perhaps??!
	{
		LiveInterface->m_FindMatchTimer = 0.0f;

		debugf(NAME_DevLive, TEXT("SessionQueryTask::Finished : %d Results"), dwResults);
		if (bRetryConnection)
			LiveInterface->RetryConnectTo(ppSearchResult[0], theNet);
		else
			LiveInterface->ConnectTo(ppSearchResult[0]);
	}
	else
	{
		// terminate AND show that the results are dire
		warnf(TEXT("Failure to match session ID."));
		LiveInterface->m_FindMatchTimer = appSeconds() + LAST_MATCH_RETRY_TIME;
		LiveInterface->m_MatchState = XboxLiveManager::MM_SESSION_CLIENT_TRAVEL_RETRY;
	}

	return true;
	unguard;
}

// Attribute layout for RetrieveExtraInfo return attributes.
// This must match the order of both the query return attributes
// and the fields specified in the CRetrieveExtraInfoResult class.
const XONLINE_ATTRIBUTE_SPEC RetrieveExtraInfoTask::AttributeSpec[]=
{
	{ X_ATTRIBUTE_DATATYPE_STRING, ( XATTRIB_PLAYER_NAMES_MAX_LEN + 1 ) * sizeof( WCHAR ) }, // Players
};

UBOOL RetrieveExtraInfoTask::Finished()
{
	guard(RetrieveExtraInfoTask::Finished);

	// Fetch results
	HRESULT Result;
	XONLINE_MATCH_SEARCHRESULT** ppSearchResults;
	DWORD dwNumSessions;
	Result = XOnlineMatchSearchGetResults( m_hOnlineTask, &ppSearchResults, &dwNumSessions );
	if(SUCCEEDED(Result))
	{
		// A search-by-ID matchmaking query should return at most one result.
		check(dwNumSessions <= 1);

		Results.SetSize(dwNumSessions);
		for (DWORD i = 0; i < dwNumSessions; i++)
		{
			XONLINE_MATCH_SEARCHRESULT* pxms = ppSearchResults[i];

			Results.v[i].SessionID = pxms->SessionID;
			Results.v[i].KeyExchangeKey = pxms->KeyExchangeKey;
			Results.v[i].HostAddress = pxms->HostAddress;
			Results.v[i].PublicOpen = pxms->dwPublicOpen;
			Results.v[i].PrivateOpen = pxms->dwPrivateOpen;
			Results.v[i].PublicFilled = pxms->dwPublicFilled;
			Results.v[i].PrivateFilled = pxms->dwPrivateFilled;
			Result = XOnlineMatchSearchParse(ppSearchResults[i], sizeof(AttributeSpec) / sizeof(AttributeSpec[0]), AttributeSpec, &Results.v[i]);
		}

		if (XboxLiveInterface != NULL && ExtraInfoCompletion != NULL)
		{
			if (dwNumSessions > 0)
			{
				const WCHAR* Players = Results[0].PlayerNames;
				(XboxLiveInterface->*ExtraInfoCompletion)(dwNumSessions, appFromUnicode(Players));
			}
			else
			{
				(XboxLiveInterface->*ExtraInfoCompletion)(0, TEXT(""));
			}
		}
	}

	return true;
    
	unguard;
}

UBOOL StatsWriteTask::Finished()
{
	guard(StatsWriteTask::Finished);

	// We intentionally do not call XOnlineStatWriteGetResult() before closing the task
	// since we do not support user uploaded content.
	Close();

	return TRUE;

	unguard
}

UBOOL StatsReadTask::Finished()
{
	guard(StatsReadTask::Finished);

	HRESULT Result = XOnlineStatReadGetResult(m_hOnlineTask, NumStatSpecs, StatSpecs, 0, NULL);
	if (FAILED(Result))
	{
		debugf(TEXT("StatsReadTask::Finished -- XOnlineStatReadGetResult() failed: %s"), ParseXOnlineResult(Result));
		Close();
		return TRUE;
	}

	// Print the results to the debug console.
	for (INT StatSpecIndex = 0; StatSpecIndex < NumStatSpecs; StatSpecIndex++)
	{
		XONLINE_STAT_SPEC StatSpec = StatSpecs[StatSpecIndex];

		debugf(NAME_DevPer, TEXT("Leaderboard: %d"), StatSpec.dwLeaderBoardID);

		for (DWORD StatIndex = 0; StatIndex < StatSpec.dwNumStats; StatIndex++)
		{
			XONLINE_STAT Stat = StatSpec.pStats[StatIndex];
			switch(Stat.type)
			{
			case XONLINE_STAT_NONE:
				debugf(NAME_DevPer, TEXT("#%d: Uninitialized"), Stat.wID);
				break;

			case XONLINE_STAT_DOUBLE:
				debugf(NAME_DevPer, TEXT("#%d: %f"), Stat.wID, (FLOAT) Stat.dValue);
				break;

			case XONLINE_STAT_LONG:
				debugf(NAME_DevPer, TEXT("#%d: %d"), Stat.wID, (INT) Stat.lValue);
				break;

			default:
				// It should never get this far.
				debugf(NAME_DevPer, TEXT("#%d: Unknown type."), Stat.wID);
				break;
			}
		}

		delete[] StatSpec.pStats;
	}

	delete[] StatSpecs;

	Close();
	return TRUE;

	unguard;
}

UBOOL StatsEnumerateTask::Finished()
{
	guard(StatsEnumerateTask::Finished);

	INT NumTotalStats = NumStats * NumUsers;

	TArray<XONLINE_STAT> Stats;
	Stats.Add(NumTotalStats);

	TArray<XONLINE_STAT_USER> Users;
	Users.Add(NumUsers);

	DWORD LeaderboardSize;
	DWORD NumReturnedResults;

	HRESULT Result = XOnlineStatLeaderEnumerateGetResults(m_hOnlineTask, NumUsers, &Users(0), NumTotalStats, &Stats(0),
														  &LeaderboardSize, &NumReturnedResults, 0, NULL);
	if (FAILED(Result))
	{
		debugf(TEXT("StatsEnumerateTask::Finished -- XOnlineStatLeaderEnumerateGetResults() failed: %s"), ParseXOnlineResult(Result));
		Close();
		return TRUE;
	}

	debugf(NAME_DevPer, TEXT("LeaderboardSize: %d"), LeaderboardSize);
	debugf(NAME_DevPer, TEXT("NumReturnedResults: %d"), NumReturnedResults);

	// Print the returned results to the debug console.
	for (INT UserIndex = 0; UserIndex < INT(NumReturnedResults); UserIndex++)
	{
		XONLINE_STAT_USER User = Users(UserIndex);
		debugf(NAME_DevPer, TEXT("User: 0x%016I64X"), User.xuidUser.qwUserID);
		debugf(NAME_DevPer, TEXT("Nickname: %s"), appFromAnsi(User.szGamertag));

		for (INT StatIndex = 0; StatIndex < NumStats; StatIndex++)
		{
			XONLINE_STAT Stat = Stats(StatIndex + UserIndex * NumStats);
			switch (Stat.type)
			{
			case XONLINE_STAT_NONE:
				debugf(NAME_DevPer, TEXT("#%d: Uninitialized"), Stat.wID);
				break;

			case XONLINE_STAT_DOUBLE:
				debugf(NAME_DevPer, TEXT("#%d: %f"), Stat.wID, (FLOAT) Stat.dValue);
				break;

			case XONLINE_STAT_LONG:
				debugf(NAME_DevPer, TEXT("#%d: %d"), Stat.wID, (INT) Stat.lValue);
				break;

			case XONLINE_STAT_LPCWSTR:
				debugf(NAME_DevPer, TEXT("#%d: %s"), Stat.wID, appFromUnicode(Stat.lpString));
				break;

			default:
				// It should never get this far.
				debugf(NAME_DevPer, TEXT("#%d: Unknown type."), Stat.wID);
			}
		}
	}

	Close();
	return TRUE;

	unguard;
}

const WORD StatsRungQueryTask::StatIndices[] = {RUNG_STAT_INT_MAX + RUNG_STAT_FLOAT_FASTEST_WIN + 1, XONLINE_STAT_RANK};
 
UBOOL StatsRungQueryTask::Finished()
{
	guard(StatsRungQueryTask::Finished);

	INT NumStatIndices = sizeof(StatIndices) / sizeof(StatIndices[0]);
	INT NumStats = NumStatIndices * NumUsers;

	TArray<XONLINE_STAT> Stats;
	Stats.Add(NumStats);

	TArray<XONLINE_STAT_USER> Users;
	Users.Add(NumUsers);

	DWORD LeaderboardSize;
	DWORD NumReturnedResults;

	HRESULT Result = XOnlineStatLeaderEnumerateGetResults(m_hOnlineTask, NumUsers, &Users(0),
		                                                  NumStats, &Stats(0),
		                                                  &LeaderboardSize, &NumReturnedResults, 0, NULL);
	if (FAILED(Result))
	{
		debugf(TEXT("StatsRungQueryTask::Finished -- XOnlineStatLeaderEnumerateGetResults() failed: %s"), ParseXOnlineResult(Result));
		return TRUE;
	}

	for (INT UserIndex = 0; UserIndex < INT(NumReturnedResults); UserIndex++)
	{
		XONLINE_STAT_USER User = Users(UserIndex);
		FPlayerRungData PlayerRungData;

		PlayerRungData.Nickname = FString(appFromAnsi(User.szGamertag));
		appMemcpy(&PlayerRungData.XUID, &User.xuidUser, sizeof(User.xuidUser));

		for (INT StatIndex = 0; StatIndex < NumStatIndices; StatIndex++)
		{
			XONLINE_STAT Stat = Stats(StatIndex + UserIndex * NumStatIndices);
			switch (Stat.wID)
			{
			case RUNG_STAT_INT_MAX + RUNG_STAT_FLOAT_FASTEST_WIN + 1:
				PlayerRungData.BestTimeInSeconds = (FLOAT) Stat.dValue;
				break;
			case XONLINE_STAT_RANK:
				PlayerRungData.Rank = (INT) Stat.llValue;
				break;
			default:
				debugf(TEXT("#%d: Unexpected stat ID."), Stat.wID);
				break;
			}
		}

		new(RungData.PlayersData) FPlayerRungData(PlayerRungData);
	}

	for (INT Index = 0; Index < RungData.PlayersData.Num(); Index++)
	{
		FPlayerRungData PlayerRungData = RungData.PlayersData(Index);

		debugf(NAME_DevPer, TEXT("Nickname: %s"), PlayerRungData.Nickname);
		debugf(NAME_DevPer, TEXT("XUID: 0x%016I64X"), ((XUID*) &PlayerRungData.XUID)->qwUserID);
		debugf(NAME_DevPer, TEXT("Rank: %d"), PlayerRungData.Rank);
		debugf(NAME_DevPer, TEXT("Best time: %f"), PlayerRungData.BestTimeInSeconds);
	}

	UGUIController* GUI = LiveInterface->getGUIController();
	if (GUI != NULL)
	{
		GUI->eventRungQueryComplete(RungData);
	}

	return TRUE;

	unguard;
}

UBOOL StatsLadderReadTask::Finished()
{
	guard(StatsLadderReadTask::Finished);

	DWORD NumTotalResults, NumResults;
	RungResults Results;
	DWORD ResultsSize = sizeof(Results);

	HRESULT Result = XOnlineQuerySearchGetResults(m_hOnlineTask, &NumTotalResults, &NumResults,
												  &ResultsSize, (PBYTE) &Results);
	if (FAILED(Result))
	{
		debugf(TEXT("StatsLadderReadTask::Finished() -- XOnlineQuerySearchGetResults failed: %s(%x)"), ParseXOnlineResult(Result), Result);
		return TRUE;
	}

	if (NumTotalResults > 0)
	{
		for (INT RungIndex = 0; RungIndex < LadderData.Num(); RungIndex++)
		{
			INT Score = Results.Scores[RungIndex];
			if (Score == 0)
			{
				Score = 1000000;
			}

			LadderData(RungIndex).BestPlayerData.BestTimeInSeconds = 1000000 - Score;
		}
	}

	UGUIController* GUI = LiveInterface->getGUIController();
	if (GUI != NULL)
	{
		GUI->eventLadderQueryComplete(LadderData);
	}

	return TRUE;

	unguard;
}

UBOOL StatsLadderWriteTask::Finished()
{
	guard(StatsLadderWriteTask::Finished);

	INT PortIndex = 0;
	UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
	if (UIX != NULL)
	{
		PortIndex = UIX->getGamepadPort();
	}

	switch (State)
	{
	case STATE_Reading:
	{
		DWORD NumTotalResults, NumResults;
		DWORD ResultsSize = sizeof(RungResults);
		RungResults Results;

		HRESULT Result = XOnlineQuerySearchGetResults(m_hOnlineTask, &NumTotalResults, &NumResults, &ResultsSize, (BYTE*) &Results);
		if (FAILED(Result))
		{
			debugf(TEXT("StatsLadderWriteTask::Finished() -- XOnlineQuerySearchGetResults failed: %s"), ParseXOnlineResult(Result));
			return TRUE;
		}

		if (NumTotalResults > 0)
		{
			// Compare the best Xbox Live time to our own best time.
			FLOAT BestXboxLiveTime = 1000000 - Results.Scores[RungIndex];
			if (NewBestTime <= BestXboxLiveTime || BestXboxLiveTime == 0.0f)
			{
				// Our time is better so proceed to removing any existing entry we have for this ladder.
				BestScores.AddZeroed(NumRungScoresPerPage);
				appMemcpy(&BestScores(0), Results.Scores, sizeof(QWORD) * NumRungScoresPerPage);

				XONLINE_ATTRIBUTE SearchAttributes[1];
				SearchAttributes[0].dwAttributeID = ParamPage;
				SearchAttributes[0].fChanged = TRUE;
				SearchAttributes[0].info.integer.qwValue = LadderIndex;

				HRESULT Result = XOnlineQueryRemove(PortIndex, 0, DatasetRungs, ProcRemoveForUser, 1,
													SearchAttributes, NULL, &m_hOnlineTask);
				if (FAILED(Result))
				{
					debugf(TEXT("StatsLadderWriteTask::Finished() -- XOnlineQueryRemove failed: %s"), ParseXOnlineResult(Result));
					return TRUE;
				}

				State = STATE_Removing;
				return FALSE;
			}
			else
			{
				// Our time was worse so don't write it.
				return TRUE;
			}
		}
		else
		{
			// There were no existing results, so we proceed directly to writing the new best time.
			ULONGLONG CurrentTime;
			GetSystemTimeAsFileTime((FILETIME*) &CurrentTime);
			XONLINE_ATTRIBUTE Attributes[NumRungScoresPerPage + 2];
			for (int Index = 0; Index < NumRungScoresPerPage; Index++)
			{
				Attributes[Index].dwAttributeID = AttribScoresBase + Index;
				Attributes[Index].fChanged = TRUE;
				if (Index == RungIndex)
				{
					Attributes[Index].info.integer.qwValue = 1000000 - NewBestTime;
				}
				else
				{
					Attributes[Index].info.integer.qwValue = 1000000;
				}
			}
			Attributes[NumRungScoresPerPage].dwAttributeID = AttribDateWritten;
			Attributes[NumRungScoresPerPage].fChanged = TRUE;
			Attributes[NumRungScoresPerPage].info.integer.qwValue = CurrentTime;
			Attributes[NumRungScoresPerPage+1].dwAttributeID = AttribPage;
			Attributes[NumRungScoresPerPage+1].fChanged = TRUE;
			Attributes[NumRungScoresPerPage+1].info.integer.qwValue = LadderIndex;

			HRESULT Result = XOnlineQueryAdd(PortIndex, 0, DatasetRungs, NumRungScoresPerPage + 2, Attributes,
											 NULL, &m_hOnlineTask);
			if (FAILED(Result))
			{
				debugf(TEXT("StatsLadderWriteTask::Finished() -- XOnlineQueryAdd failed: %s"), ParseXOnlineResult(Result));
				return TRUE;
			}

			State = STATE_Writing;
			return FALSE;
		}
		break;
	}

	case STATE_Removing:
	{
		ULONGLONG CurrentTime;
		GetSystemTimeAsFileTime((FILETIME*) &CurrentTime);
		XONLINE_ATTRIBUTE Attributes[NumRungScoresPerPage + 2];
		for (int Index = 0; Index < NumRungScoresPerPage; Index++)
		{
			Attributes[Index].dwAttributeID = AttribScoresBase + Index;
			Attributes[Index].fChanged = TRUE;
			if (Index == RungIndex)
			{
				Attributes[Index].info.integer.qwValue = 1000000 - NewBestTime;
			}
			else if (BestScores(Index) == 0)
			{
				Attributes[Index].info.integer.qwValue = 1000000;
			}
			else
			{
				Attributes[Index].info.integer.qwValue = BestScores(Index);
			}
		}
		Attributes[NumRungScoresPerPage].dwAttributeID = AttribDateWritten;
		Attributes[NumRungScoresPerPage].fChanged = TRUE;
		Attributes[NumRungScoresPerPage].info.integer.qwValue = CurrentTime;
		Attributes[NumRungScoresPerPage+1].dwAttributeID = AttribPage;
		Attributes[NumRungScoresPerPage+1].fChanged = TRUE;
		Attributes[NumRungScoresPerPage+1].info.integer.qwValue = LadderIndex;

		HRESULT Result = XOnlineQueryAdd(PortIndex, 0, DatasetRungs, NumRungScoresPerPage + 2, Attributes,
										 NULL, &m_hOnlineTask);
		if (FAILED(Result))
		{
			debugf(TEXT("StatsLadderWriteTask::Finished() -- XOnlineQueryAdd failed: %s"), ParseXOnlineResult(Result));
			return TRUE;
		}

		State = STATE_Writing;
		return FALSE;
		break;
	}

	case STATE_Writing:
		return TRUE;
		break;
	}

	// It should never get this far.
	check(false);
	return TRUE;

	unguard;
}

UBOOL StatsQueryTask::Finished()
{
	guard(StatsQueryTask::Finished);

	HRESULT Result = XOnlineStatReadGetResult(m_hOnlineTask, NumStatSpecs, StatSpecs, 0, NULL);
	if (FAILED(Result))
	{
		debugf(TEXT("StatsQueryTask::Finished -- XOnlineStatReadGetResult() failed: %s"), ParseXOnlineResult(Result));
		return TRUE;
	}

	check(Query);
	check(Query->QuerySpecs.Num());


	// Fill in the stats object and print the results to the debug console.
	for (INT StatSpecIndex = 0; StatSpecIndex < NumStatSpecs; StatSpecIndex++)
	{
		FPlayerQuerySpec& Dest = Query->QuerySpecs(0);
		XONLINE_STAT_SPEC StatSpec = StatSpecs[StatSpecIndex];

		debugf(NAME_DevStats, TEXT("Leaderboard: %d"), StatSpec.dwLeaderBoardID);
		for (DWORD StatIndex = 0; StatIndex < StatSpec.dwNumStats; StatIndex++)
		{
			XONLINE_STAT Stat = StatSpec.pStats[StatIndex];
			switch(Stat.type)
			{
			case XONLINE_STAT_NONE:
				debugf(NAME_DevStats, TEXT("   #%d: Uninitialized"), Stat.wID);
				break;

			case XONLINE_STAT_DOUBLE:
				if (Dest.DestPlayerStat != NULL)
				{
					Dest.DestPlayerStat->StatsCache((Stat.wID - 1) + (StatSpec.dwLeaderBoardID - 1) * UPlayerStats::NumFieldsPerTable).FloatValue = Stat.dValue;
				}
				debugf(NAME_DevStats, TEXT("   #%d: %f"), Stat.wID, (FLOAT) Stat.dValue);
				break;

			case XONLINE_STAT_LONG:
				if (Dest.DestPlayerStat != NULL)
				{
					Dest.DestPlayerStat->StatsCache((Stat.wID - 1) + (StatSpec.dwLeaderBoardID - 1) * UPlayerStats::NumFieldsPerTable).IntValue = Stat.lValue;
				}
				debugf(NAME_DevStats, TEXT("   #%d: %d"), Stat.wID, (INT) Stat.lValue);
				break;

			default:
				// It should never get this far.
				debugf(NAME_DevStats, TEXT("   #%d: Unknown type."), Stat.wID);
				break;
			}
		}

		delete[] StatSpec.pStats;

		Dest.StatSpecCount--;
		if ( Dest.StatSpecCount == 0 )
		{
			Dest.DestPlayerStat->eventReceivedQueryResults(false);
			Query->QuerySpecs.Remove(0);
		}
	}

	delete[] StatSpecs;

	Query->QueryCompleted(false);
	Query = NULL;

	return TRUE;

	unguard;
}

UBOOL MuteListQueryTask::Finished()
{
	guard(MuteListQueryTask::Finished);

#ifdef _XBOX
	UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
	UIX->UpdateMuteList(MuteList, MuteListSize);

	Close();
#endif

	return true;

	unguard;
}

// =====================================================================================
// XboxLiveSessionAttributes
// =====================================================================================

void XboxLiveSessionAttributes::Clear() 
{
	guard(XboxLiveSessionAttributes::Clear);

	for ( UINT i = 0; i < m_SessionAttributeCount; i++ )
	{
		if ( ((m_SessionAttributes[i].dwAttributeID&X_ATTRIBUTE_DATATYPE_STRING) == X_ATTRIBUTE_DATATYPE_STRING) &&
			m_SessionAttributes[i].info.string.lpValue )
		{
			delete [] m_SessionAttributes[i].info.string.lpValue;
			m_SessionAttributes[i].info.string.lpValue = NULL;
		}

		if ( ((m_SessionAttributes[i].dwAttributeID&X_ATTRIBUTE_DATATYPE_BLOB) == X_ATTRIBUTE_DATATYPE_BLOB) &&
			m_SessionAttributes[i].info.blob.pvValue )
		{
			delete [] m_SessionAttributes[i].info.blob.pvValue;
			m_SessionAttributes[i].info.blob.pvValue = NULL;
		}
	}

	m_SessionAttributeCount = 0;
	appMemzero(m_SessionAttributes, sizeof(m_SessionAttributes));
	unguard;
}

void XboxLiveSessionAttributes::touchAll()
{
	for (UINT i = 0; i != MAX_SESSION_ATTRIBUTES; ++i)
		m_SessionAttributes[i].fChanged = 1;
}

void XboxLiveSessionAttributes::set(DWORD attribID)
{
	guard(XboxLiveSessionAttributes::set);

	UINT Index = slot(attribID);
	check(Index < MAX_SESSION_ATTRIBUTES);

	m_SessionAttributes[Index].dwAttributeID = X_ATTRIBUTE_DATATYPE_NULL;
	m_SessionAttributes[Index].fChanged = 0;

	unguard;
}

void XboxLiveSessionAttributes::set(DWORD attribID, const FString & strValue) 
{
	guard(XboxLiveSessionAttributes::set);
	UINT Index = slot(attribID);
	check(Index < MAX_SESSION_ATTRIBUTES);

	//debugf(NAME_DevLive, TEXT("set: Index = %d, attribID = %d, strValue = \"%s\""), Index, attribID, *strValue);

	m_SessionAttributes[Index].dwAttributeID = attribID;

	if( m_SessionAttributes[Index].info.string.lpValue && 
		!appStrcmp( appFromUnicode(m_SessionAttributes[Index].info.string.lpValue), *strValue ) )
	{
		m_SessionAttributes[Index].fChanged = 0;
	}
	else
	{
		m_SessionAttributes[Index].fChanged = 1;

		if( !m_SessionAttributes[Index].info.string.lpValue )
			m_SessionAttributes[Index].info.string.lpValue = new UNICHAR[X_MAX_STRING_ATTRIBUTE_LEN];

		check( strValue.Len() < X_MAX_STRING_ATTRIBUTE_LEN );
		wcscpy(m_SessionAttributes[Index].info.string.lpValue, appToUnicode(*strValue));
	}

	unguard;
}

void XboxLiveSessionAttributes::set(DWORD attribID, ULONGLONG intValue)
{
	guard(XboxLiveSessionAttributes::set);	
	UINT Index = slot(attribID);
	check(Index < MAX_SESSION_ATTRIBUTES);

	m_SessionAttributes[Index].dwAttributeID = attribID;

	if( m_SessionAttributes[Index].info.integer.qwValue == intValue )
	{
		m_SessionAttributes[Index].fChanged = 0;
	}
	else
	{
		m_SessionAttributes[Index].fChanged = 1;
		m_SessionAttributes[Index].info.integer.qwValue = intValue;
	}

	unguard;
}

void XboxLiveSessionAttributes::set(DWORD attribID, void * binaryValue, INT binaryValueLength)
{
	guard(XboxLiveSessionAttributes::set);	
	UINT Index = slot(attribID);
	check(Index < MAX_SESSION_ATTRIBUTES);

	check(binaryValue);
	check(binaryValueLength);

	m_SessionAttributes[Index].dwAttributeID = attribID;

	if ( m_SessionAttributes[Index].info.blob.pvValue &&
		(m_SessionAttributes[Index].info.blob.dwLength == binaryValueLength) &&
		!appMemcmp( m_SessionAttributes[Index].info.blob.pvValue, binaryValue, binaryValueLength) )
	{
		m_SessionAttributes[Index].fChanged = 0;
	}
	else
	{
		m_SessionAttributes[Index].fChanged = 1;

		if( m_SessionAttributes[Index].info.blob.pvValue && 
			(m_SessionAttributes[Index].info.blob.dwLength != binaryValueLength) )
			delete[] m_SessionAttributes[Index].info.blob.pvValue;

		if( !m_SessionAttributes[Index].info.blob.pvValue )
			m_SessionAttributes[Index].info.blob.pvValue = new BYTE[binaryValueLength];

		appMemcpy(m_SessionAttributes[Index].info.blob.pvValue, binaryValue, binaryValueLength);
		m_SessionAttributes[Index].info.blob.dwLength = binaryValueLength;
	}

	unguard;
}

/*
   Find the index of a slot for the attribute. This index will be:
     The index of that attribute if it is a unique attribute and is already in the set
	 The index of the next open attribute slot otherwise

   "unique" in this context means it is not just an INT, STRING, NULL, or BLOB, it is "named" 
   attribute (named with the title-specific bits being set).
*/
UINT XboxLiveSessionAttributes::slot(DWORD attribID)
{
	UINT i;
	if (!( (attribID == X_ATTRIBUTE_DATATYPE_INTEGER) ||
		   (attribID == X_ATTRIBUTE_DATATYPE_STRING) ||
		   (attribID == X_ATTRIBUTE_DATATYPE_BLOB) ||
		   (attribID == X_ATTRIBUTE_DATATYPE_NULL) ) ) 
	{
		for (i = 0; i != m_SessionAttributeCount; ++i) {
			if (m_SessionAttributes[i].dwAttributeID == attribID)
			{
				return i;
			}
		}
	}
	// Note: No error checking here (to see if we go beyond the maximum number of
	// attributes. 
	
	return m_SessionAttributeCount++; // post increment so count now includes new element
}

// =====================================================================================
// XboxLiveManager
// =====================================================================================

XboxLiveManager * XboxLiveManager::theOnlyManager = NULL;

XboxLiveManager::XboxLiveManager()
: m_phQueryTask(0), m_phMatchTask(0), m_phUpdateTask(0), m_phStatsReadTask(0), m_phStatsWriteTask(0), m_phExtraInfoTask(0), m_phMuteListTask(0), m_phQueryWriteTask(0), m_phCacheQueryTask(NULL)
, m_phMatchResults(0) , m_SessionCreateTimer(0.f), m_SessionCreateRetries(0), m_FindMatchTimer(0.f), m_FindMatchStartTime(0)
, m_SessionUpdateTimer(0.0f), m_bForceSessionUpdate(0), StoredHostInfo(0), m_MatchState(MM_SESSION_NONE), m_bWasInvited(0)
, bSilentSignin(0)
, Interface(NULL)
{
	clearAllUsers();
	m_FindMatchTimeout = GetDefault<UTcpNetDriverXbox>()->InitialConnectTimeout;
}


FORCEINLINE UGameEngine * XboxLiveManager::getEngine()
{
	return CastChecked<UGameEngine>( UUIXInteraction::getUUIXInteraction()->Master->Client->Engine );
}
FORCEINLINE UTcpNetDriverXbox * XboxLiveManager::getNet() 
{
	return (UTcpNetDriverXbox *)getEngine()->GLevel->NetDriver;
}

void XboxLiveManager::Tick()
{
	guard(XboxLiveManager::Tick);
	if (m_phQueryTask)
		UpdateTask<XboxLiveOnlineTask>(m_phQueryTask);
	if (m_phMatchTask)
		UpdateTask<XboxLiveOnlineTask>(m_phMatchTask);
	if (m_phStatsReadTask)
		UpdateTask<XboxLiveOnlineTask>(m_phStatsReadTask);
	if (m_phStatsWriteTask)
		UpdateTask<XboxLiveOnlineTask>(m_phStatsWriteTask);
	if (m_phQueryWriteTask)
		UpdateTask<XboxLiveOnlineTask>(m_phQueryWriteTask);
	if (m_phExtraInfoTask != NULL)
	{
		UpdateTask<RetrieveExtraInfoTask>(m_phExtraInfoTask);
	}
	if (m_phMuteListTask != NULL)
	{
		UpdateTask<MuteListQueryTask>(m_phMuteListTask);
	}

	if ( m_phCacheQueryTask != NULL )
		UpdateTask<StatsQueryTask>(m_phCacheQueryTask);

	switch( m_MatchState )
	{
	case MM_SESSION_NONE:
		break;
	case MM_SESSION_ERROR:
		break;
	case MM_SESSION_CLIENT_TRAVEL:
		break;
	case MM_SESSION_CLIENT_TRAVEL_RETRY:
		{
			FLOAT CurrentTime = appSeconds();

			// if we've been retrying too long (generally the same number of seconds as the 
			// NetDriver's InitialConnectTimeout), then stop retrying
			if ( CurrentTime - m_FindMatchStartTime >= m_FindMatchTimeout )
			{
				debugf(NAME_Error, TEXT("Unable to find matching ID"));
				m_MatchState = MM_SESSION_ERROR;

				if ( !m_bRetryConnection )
				{
					// display the session is no longer available message
					UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
					if ( UIX != NULL )
					{
						UIX->eventShowMessage(Localize(TEXT("Localizer"), TEXT("sNetworkConnectionLostText"), TEXT("Engine")), 0);
					}
				}
			}

			// wait a second or so after the last query finishes before we attempt another, so that we don't
			// flood the live datacenter
			else if ( CurrentTime >= m_FindMatchTimer )								
			{
				if (*(ULONGLONG *)&m_SessionID != 0)
					StartFindFromIDQuery(m_SessionID, m_bWasInvited, m_bRetryConnection, m_pNetToUse);
				else
					StartFindFromPreviousIDQuery(m_PreviousSessionID, m_bWasInvited, m_bRetryConnection, m_pNetToUse);
			}
		}

		break;
	case MM_SESSION_CREATE:
		break;
	case MM_SESSION_CREATE_RETRY:
		if( appSeconds() >= m_SessionCreateTimer )
			StartNewSession();
		break;
	case MM_SESSION_ACTIVE:
		if( appSeconds() >= m_SessionUpdateTimer )
			UpdateSession();
		break;
	case MM_SESSION_UPDATE:
		break;
	case MM_SESSION_DELETE:
		break;
	}

	if (m_phUpdateTask)
		UpdateTask<XboxLiveOnlineTask>(m_phUpdateTask);

	// only check for whether the user has any recommended messages waiting if
	// we logged on using silent sign-in (otherwise, UIX will handle displaying the prompt)
	if ( bSilentSignin )
		CheckUserStates();

	unguard;
}

/*==========================================================================================================
function	XboxLiveManager::SetTaskHandle
Desc:		Assigns a new task handle to one of the task handle member fields.  If there is an existing task
			there, cleans up the previous task.

input:		phMemberTask
				the member field that holds the task handle

			phNewTask
				the pointer to the handle for the new task that should be assigned to this field.

last modified by:	Ron Prestenback
==========================================================================================================*/
template<class QueryClass>
void XboxLiveManager::SetTaskHandle( QueryClass*& phMemberTask, QueryClass* phNewTask ) 
{
    guard(XboxLiveManager::SetTaskHandle);

	if ( phMemberTask )
	{
		delete phMemberTask;
		phMemberTask = NULL;
	}

	phMemberTask = phNewTask;

    unguard;
}



/*==========================================================================================================
function	XboxLiveManager::UsingCustomContent
Desc:		Determines whether the current session is using any custom content.

Params:		MapName:
				name of the map for the current session

			Mutators
				array of mutator class names used in the current session

return:		Returns true if the map or any of the mutators are custom content.

last modified by:	Ron Prestenback
==========================================================================================================*/
UBOOL XboxLiveManager::UsingCustomContent( const FString& MapName, const TArray<FString>& Mutators ) const
{
    guard(XboxLiveManager::UsingCustomContent);

	FContentManager* ContentManager = FContentManager::GetInstance();
	if ( ContentManager == NULL )
		return false;

	if ( ContentManager->IsCustomContent(MapName) )
		return true;

	for ( INT i = 0; i < Mutators.Num(); i++ )
		if ( ContentManager->IsCustomContent(Mutators(i)) )
			return true;

	return false;
    unguard;
}


void XboxLiveManager::ConnectTo(PXONLINE_MATCH_SEARCHRESULT sr) 
{
	guard(XboxLiveManager::ConnectTo);

	FString SessionID, ExchangeKey, HostAddr;

	UGUIController * gui = getGUIController();
	if (gui != NULL) 
	{
		MatchSessionIDResults MatchResult(1);
		FMatchSessionIDResult& Match = MatchResult[0];

		appMemzero(&Match, sizeof(FMatchSessionIDResult));

		Match.SessionID = sr->SessionID;
		Match.KeyExchangeKey = sr->KeyExchangeKey;
		Match.HostAddress = sr->HostAddress;
		Match.PublicOpen = sr->dwPublicOpen;
		Match.PrivateOpen = sr->dwPrivateOpen;
		Match.PublicFilled = sr->dwPublicFilled;
		Match.PrivateFilled = sr->dwPrivateFilled;

		HRESULT hr = XOnlineMatchSearchParse( sr, ARRAY_COUNT(SessionQueryTask::MatchBySessionIDAttributeSpec),
			SessionQueryTask::MatchBySessionIDAttributeSpec, &Match );
		check(SUCCEEDED(hr));

		BytesToString((BYTE*)&sr->SessionID, sizeof(sr->SessionID), SessionID);
		BytesToString((BYTE*)&sr->KeyExchangeKey, sizeof(sr->KeyExchangeKey), ExchangeKey);
		BytesToString((BYTE*)&sr->HostAddress, sizeof(sr->HostAddress), HostAddr);

		FbeaconInformation ServerData;
		appMemzero(&ServerData, sizeof(FbeaconInformation));

		ServerData.GameTypeName = gui->eventGetGametypePrefix(Match.GameType);
		ServerData.ServerName = appFromUnicode(Match.Host);
		ServerData.MapName = appFromUnicode(Match.Map);
		ServerData.CurrentPlayers = FString::Printf(TEXT("%d"), (sr->dwPublicFilled + sr->dwPrivateFilled));
		ServerData.MaxPlayers = FString:: Printf(TEXT("%d"), (sr->dwPublicFilled + sr->dwPublicOpen + sr->dwPrivateFilled + sr->dwPrivateOpen));
		ServerData.Mutators = appFromUnicode(Match.Mutators);
		ServerData.UniqueAddress = SessionID;

		gui->eventJoinFriendServer(ServerData, ExchangeKey, HostAddr, m_bWasInvited);
	}

	unguard;
}

void XboxLiveManager::RetryConnectTo(PXONLINE_MATCH_SEARCHRESULT sr, UTcpNetDriverXbox * netToUse)
{
	guard(XboxLiveManager::RetryConnectTo);

	FString SessionID, ExchangeKey, HostAddr;

	BytesToString((BYTE*)&sr->SessionID, sizeof(sr->SessionID), SessionID);
	BytesToString((BYTE*)&sr->KeyExchangeKey, sizeof(sr->KeyExchangeKey), ExchangeKey);
	BytesToString((BYTE*)&sr->HostAddress, sizeof(sr->HostAddress), HostAddr);

	UTcpNetDriverXbox* net = (netToUse) ? netToUse : getNet();
	if (net)
	{
		net->RetryConnection(SessionID, ExchangeKey, HostAddr);
	}

	unguard;
}

// Attribute layout for MatchByPreviousSessionID return attributes.
// This must match the order of both the query return attributes
// and the fields specified in the CMatchByPreviousSessionIDResult class.
static const
XONLINE_ATTRIBUTE_SPEC MatchByPreviousSessionIDAttributeSpec[]=
{
	{ X_ATTRIBUTE_DATATYPE_STRING, ( XATTRIB_HOST_MAX_LEN + 1 ) * sizeof( WCHAR ) }, // Host
	{ X_ATTRIBUTE_DATATYPE_STRING, ( XATTRIB_MAP_MAX_LEN + 1 ) * sizeof( WCHAR ) }, // Map
	{ X_ATTRIBUTE_DATATYPE_STRING, ( XATTRIB_MUTATORS_MAX_LEN + 1 ) * sizeof( WCHAR ) }, // Mutators
	{ X_ATTRIBUTE_DATATYPE_INTEGER, sizeof( ULONGLONG ) }, // GameType
};

void XboxLiveManager::StartFindFromPreviousIDQuery(XNKID sessionID, BOOL bWasInvited, BOOL bRetryConnection, UTcpNetDriverXbox * netToUse)
{
	guard(XboxLiveManager::StartFindFromPreviousIDQuery);

	//debug -------------------------------------------------------
	FString sID;
	BytesToString((BYTE *)&sessionID, sizeof(sessionID), sID);
	debugf(NAME_DevLive, TEXT("> %s(%s)"), __FUNC_NAME__, *sID);
	//debug--------------------------------------------------------

	SetTaskHandle<XboxLiveOnlineTask>(m_phQueryTask, new SessionQueryTask(this, NULL, bRetryConnection, netToUse));

	const DWORD dwStoredQueryNumber = 0x05;

	XboxLiveSessionAttributes MatchParams;
	MatchParams.set(X_ATTRIBUTE_DATATYPE_INTEGER, *(ULONGLONG *) &sessionID); 

	DWORD dwResultsLen = XOnlineMatchSearchResultsLen(
		MAX_MATCH_BY_SESSION_ID_RESULTS, 
		ARRAY_COUNT(SessionQueryTask::MatchBySessionIDAttributeSpec), 
		SessionQueryTask::MatchBySessionIDAttributeSpec
		);

	HRESULT hr = XOnlineMatchSearch(
		dwStoredQueryNumber,
		MAX_MATCH_BY_SESSION_ID_RESULTS,
		MatchParams.size(),
		MatchParams.get(),
		dwResultsLen,
		NULL,
		&(*m_phQueryTask)
		);

	if( FAILED(hr) )
	{
		warnf(TEXT("Error code from XOnlineMatchSessionFindFromPreviousID %d"), hr);
		return;
	}

	*(ULONGLONG *)&m_SessionID = 0;
	m_PreviousSessionID = sessionID;
	m_bWasInvited = bWasInvited;
	m_bRetryConnection = bRetryConnection;
	m_pNetToUse = netToUse;
	m_MatchState = MM_SESSION_CLIENT_TRAVEL;

	debugf(NAME_DevLive, TEXT("< StartFindFromPreviousIDQuery"));

	unguard;
}

void XboxLiveManager::StartFindFromIDQuery(XNKID sessionID, BOOL bWasInvited, BOOL bRetryConnection, UTcpNetDriverXbox * netToUse)
{
	guard(XboxLiveManager::StartFindFromIDQuery);

	//debug -------------------------------------------------------
	FString sID;
	BytesToString((BYTE *)&sessionID, sizeof(XNKID), sID);
	debugf(NAME_DevLive, TEXT("> %s(%s)"), __FUNC_NAME__, *sID);
	//debug--------------------------------------------------------

	SetTaskHandle<XboxLiveOnlineTask>(m_phQueryTask, new SessionQueryTask(this, NULL, bRetryConnection, netToUse));
	const DWORD dwStoredQueryNumber = 0x03;

	XboxLiveSessionAttributes MatchParams;
	MatchParams.set(X_ATTRIBUTE_DATATYPE_INTEGER, *(ULONGLONG *) &sessionID); 
	
	DWORD dwResultsLen = XOnlineMatchSearchResultsLen(
		MAX_MATCH_BY_SESSION_ID_RESULTS, 
		ARRAY_COUNT(SessionQueryTask::MatchBySessionIDAttributeSpec), 
		SessionQueryTask::MatchBySessionIDAttributeSpec
		);

	HRESULT hr = XOnlineMatchSearch(
		dwStoredQueryNumber,
		MAX_MATCH_BY_SESSION_ID_RESULTS,
		MatchParams.size(),
		MatchParams.get(),
		dwResultsLen,
		NULL,
		&(*m_phQueryTask)
		);

	if( FAILED(hr) )
	{
		warnf(TEXT("Error code from XOnlineMatchSessionFindFromID %d"), hr);
		return;
	}

	m_SessionID = sessionID;
	*(ULONGLONG *)&m_PreviousSessionID = 0;
	m_bWasInvited = bWasInvited;
	m_bRetryConnection = bRetryConnection;
	m_pNetToUse = netToUse;
	m_MatchState = MM_SESSION_CLIENT_TRAVEL;

	debugf(NAME_DevLive, TEXT("< StartFindFromIDQuery"));

	unguard;
}

UBOOL XboxLiveManager::QueryComplete( UBOOL bValidate, INT Gametype, UBOOL bIncludeCustom, INT Purity )
{
	guard(XboxLiveManager::QueryComplete);

	if ( bValidate )
	{
		// Validate Quickmatch query results to guarantee that we don't select any
		// servers which are using content we don't have
		MatchQueryResults* results = getMatchQueryResults();
		if ( results )
		{
			UGUIController* GUI = getGUIController();
			check(GUI);

			INT ValidMatches = 0;
			ULONGLONG GameIdx = Gametype;
			for ( UINT i = 0; i < results->size(); i++ )
			{
				FMatchSessionResult& Match = (*results)[i];

				// if this match doesn't use any custom content, no need to check it
				if ( Match.CustomContent == 0 )
					ValidMatches++;
				else
				{
					// make sure we have the map the session is running
					if ( !GUI->HasContent(UCONST_CONTENT_Map, appFromUnicode(Match.Map)) )
					{
						// mark this match as invalid, so that XboxLiveInterface won't include this
						// match in the query results it returns to script
						Match.bValid = 0;
					}
					else
					{
						// see if the session is running any mutators we don't have
						TArray<FString> Mutators;
						FString MutatorString = appFromUnicode(Match.Mutators);
						if ( MutatorString.Len() )
						{
							MutatorString.ParseIntoArray(TEXT(" "), &Mutators);
							for ( INT MutIndex = 0; MutIndex < Mutators.Num(); MutIndex++ )
							{
								if ( !GUI->HasContent(UCONST_CONTENT_Mutator,Mutators(MutIndex)) )
								{
									// mark this match as invalid, so that XboxLiveInterface won't include this
									// match in the query results it returns to script
									Match.bValid = 0;
									break;
								}
							}
						}
					}

					if ( Match.bValid )
						ValidMatches++;
				}
			}

			// the initial quickmatch query will allow servers that are running custom content.  if
			// all of the sessions returned are running content that we don't have, we need to try another
			// query that only returns sessions not running custom content.
			if (ValidMatches == 0)
			{
				// we didn't have any valid matches.

				// if we included custom content in that query, try another query using the same purity, but filter out sessions using custom content
				if ( bIncludeCustom )
				{
					StartQuickMatchQuery(GameIdx, 0, DesiredSkill, Purity);
					return false;
				}

				// if the previous query did not include custom content, increment the purity level and redo the process
				else if ( Purity < VC_MAX )
				{
					StartQuickMatchQuery( GameIdx, 1, DesiredSkill, Purity + 1 );
					return false;
				}
			}
		}
	}

	SetInterface();
	Interface->delegateOnQueryCompletion();
	return true;

	unguard;
}

void XboxLiveManager::StartQuickMatchQuery( INT Gametype, UBOOL bIncludeCustom, INT PlayerSkill, INT Purity  )
{
	guard(XboxLiveManager::StartQuickMatchQuery);

	debugf(NAME_DevLive, TEXT("> %s"), __FUNC_NAME__);

	QuickMatchQueryTask * quick = new QuickMatchQueryTask(this, bIncludeCustom, Purity, Gametype);
	SetTaskHandle<XboxLiveOnlineTask>(m_phQueryTask, quick);

	const DWORD dwStoredQueryNumber = 0x02;
	const DWORD cMaxResults = MAX_MATCH_ANY_SESSION_RESULTS;

	XboxLiveSessionAttributes QuickMatchParams;

	if (Gametype >= 0)
	{
		QuickMatchParams.set(X_ATTRIBUTE_DATATYPE_INTEGER, Gametype);
	}
	else 
	{
		QuickMatchParams.set(X_ATTRIBUTE_DATATYPE_NULL);
	}

	QuickMatchParams.set(X_ATTRIBUTE_DATATYPE_INTEGER, bIncludeCustom);
	QuickMatchParams.set(X_ATTRIBUTE_DATATYPE_INTEGER, PlayerSkill);
	QuickMatchParams.set(X_ATTRIBUTE_DATATYPE_INTEGER, Purity);
	DesiredSkill = PlayerSkill;

	DWORD dwResultsLen = XOnlineMatchSearchResultsLen( cMaxResults, ARRAY_COUNT(QuickMatchQueryTask::MatchAnySessionAttributeSpec), QuickMatchQueryTask::MatchAnySessionAttributeSpec );
	
	HRESULT hr = XOnlineMatchSearch( dwStoredQueryNumber, MAX_MATCH_ANY_SESSION_RESULTS,
									 QuickMatchParams.size(), (XONLINE_ATTRIBUTE *)QuickMatchParams, 
									 dwResultsLen, NULL, &(*m_phQueryTask) );

	debugf(NAME_DevLive, TEXT("< %s"), __FUNC_NAME__);

	unguard;
}

void XboxLiveManager::StartOptimatchMatchQuery( INT GameType, INT MinPlayers, INT MaxPlayers, INT Dedicated, INT Purity, INT PlayerSkill)
{
	guard(XboxLiveManager::StartOptimatchMatchQuery);

	debugf(NAME_DevLive, TEXT("> %s"), __FUNC_NAME__);

	OptiMatchQueryTask* quick = new OptiMatchQueryTask(this);
	SetTaskHandle<XboxLiveOnlineTask>(m_phQueryTask, quick);

	const DWORD dwStoredQueryNumber = 0x04;
	const DWORD cMaxResults = MAX_MATCH_OPTIMATCH_RESULTS;
	const DWORD dwMatchParamCount = 0;

	XboxLiveSessionAttributes OptiMatchParams;

	// The fields must match the order of the Query Parameters in the MatchOptimatch
	// query in the avaris.xms (MatchSim file)
	if (GameType >= 0)
		OptiMatchParams.set(X_ATTRIBUTE_DATATYPE_INTEGER, GameType);
	else 
		OptiMatchParams.set(X_ATTRIBUTE_DATATYPE_NULL);

	if ( MinPlayers >= 0 )
		OptiMatchParams.set(X_ATTRIBUTE_DATATYPE_INTEGER, MinPlayers);
	else OptiMatchParams.set(X_ATTRIBUTE_DATATYPE_NULL, MinPlayers);

	if ( MaxPlayers >= 0 )
		OptiMatchParams.set(X_ATTRIBUTE_DATATYPE_INTEGER, MaxPlayers);
	else OptiMatchParams.set(X_ATTRIBUTE_DATATYPE_NULL, MaxPlayers);

	// dedicated-only "we don't care" value is 0, not -1
	if ( Dedicated > 0 )
		OptiMatchParams.set(X_ATTRIBUTE_DATATYPE_INTEGER, Dedicated);
	else OptiMatchParams.set(X_ATTRIBUTE_DATATYPE_NULL, Dedicated);

	if ( Purity >= 0 )
		OptiMatchParams.set(X_ATTRIBUTE_DATATYPE_INTEGER, Purity);
	else OptiMatchParams.set(X_ATTRIBUTE_DATATYPE_NULL, Purity);

	DesiredSkill = PlayerSkill;
	if ( PlayerSkill >= 0 )
		OptiMatchParams.set(X_ATTRIBUTE_DATATYPE_INTEGER, PlayerSkill);
	else
		OptiMatchParams.set(X_ATTRIBUTE_DATATYPE_NULL, PlayerSkill);

	DWORD dwResultsLen = XOnlineMatchSearchResultsLen( cMaxResults, ARRAY_COUNT(OptiMatchQueryTask::MatchOptimatchAttributeSpec), OptiMatchQueryTask::MatchOptimatchAttributeSpec);

	debugf(NAME_DevLive, TEXT("Optimatch Query params (Gametype:%i  MinPlayers: %i  MaxPlayers:%i  Dedicated:%i  Purity:%i  Skill:%i)"), GameType, MinPlayers, MaxPlayers, Dedicated, Purity, DesiredSkill); 
	HRESULT hr = XOnlineMatchSearch( 
		dwStoredQueryNumber, MAX_MATCH_OPTIMATCH_RESULTS,
		OptiMatchParams.size(), OptiMatchParams.get(), 
		dwResultsLen, NULL, &(*m_phQueryTask) );

	debugf(NAME_DevLive, TEXT("< %s"), __FUNC_NAME__);

	unguard;
}

/* epic ====================================================================================================
* Function:	XboxLiveManager::StartExtraInfoQuery
* Author:	Per Vognsen
*
* Stops an extra information query. This is used for retrieving such things as the players list for
* the "Extra Info" page in the server browser.
* ==========================================================================================================
*/
void XboxLiveManager::StartExtraInfoQuery(XNKID SessionID, UXboxLiveInterface* XboxLiveInterface, ExtraInfoCompletionFunc CompletionFunc)
{
	guard(XboxLiveManager::StartExtraInfoQuery);

	const DWORD SEARCH_PROC_ID = 0x6;

	RetrieveExtraInfoTask* ExtraInfoTask = new RetrieveExtraInfoTask(this);
	SetTaskHandle<RetrieveExtraInfoTask>(m_phExtraInfoTask, ExtraInfoTask);

	ExtraInfoTask->XboxLiveInterface = XboxLiveInterface;
	ExtraInfoTask->ExtraInfoCompletion = CompletionFunc;

	XONLINE_ATTRIBUTE QueryParameters[1] = {0};
	QueryParameters[0].dwAttributeID = X_ATTRIBUTE_DATATYPE_INTEGER;
	QueryParameters[0].info.integer.qwValue = *(ULONGLONG *) &SessionID;

	// Calculate maximum space required to hold results
	DWORD dwResultsLen = XOnlineMatchSearchResultsLen(MAX_RETRIEVE_EXTRA_INFO_RESULTS, sizeof(RetrieveExtraInfoTask::AttributeSpec) / sizeof(RetrieveExtraInfoTask::AttributeSpec[0]), RetrieveExtraInfoTask::AttributeSpec);

	HRESULT hr = XOnlineMatchSearch(SEARCH_PROC_ID, MAX_RETRIEVE_EXTRA_INFO_RESULTS, 1, QueryParameters, dwResultsLen, NULL, &(*m_phExtraInfoTask));
	if (FAILED(hr))
	{
		debugf(NAME_DevLive, TEXT("StartExtraInfoQuery -- XOnlineMatchSearch failed."));
		return;
	}

	unguard;
}

void XboxLiveManager::StartStatsQuery(INT NumStatSpecs, XONLINE_STAT_SPEC* StatSpecs, UStatsQuery* QueryItem)
{
	guard(XboxLiveManager::StartStatsQuery);


	for (INT StatSpecIndex = 0; StatSpecIndex < NumStatSpecs; StatSpecIndex++)
	{
		XONLINE_STAT_SPEC StatSpec = StatSpecs[StatSpecIndex];
		debugf(NAME_DevStats, TEXT("StatSpec - User: 0x%016I64X   Leaderboard: %d"), StatSpec.xuidUser.qwUserID, StatSpec.dwLeaderBoardID);
	}

	StatsQueryTask* StatsTask = new StatsQueryTask(this);
	SetTaskHandle<StatsQueryTask>(m_phCacheQueryTask, StatsTask);

	StatsTask->NumStatSpecs = NumStatSpecs;
	StatsTask->StatSpecs = StatSpecs;
	StatsTask->Query = QueryItem;

	HRESULT Result = XOnlineStatRead(NumStatSpecs, StatSpecs, NULL, &(*StatsTask));
	if (FAILED(Result))
	{
		debugf(TEXT("XboxLiveManager::StartStatsQuery -- XOnlineStatRead failed: %s"), ParseXOnlineResult(Result));
		return;
	}

	unguard;
}

/*==========================================================================================================
function	XboxLiveManager::KillStatsQuery
Desc:		Cleans up and deletes any active stats queries.

last modified by:	Ron Prestenback
==========================================================================================================*/
void XboxLiveManager::KillStatsQuery() 
{
    guard(XboxLiveManager::KillStatsQuery);

	SetTaskHandle<StatsQueryTask>(m_phCacheQueryTask, NULL);

    unguard;
}

/*==========================================================================================================
function	XboxLiveManager::KillStatsWrite
Desc:		Cleans up and deletes any active stats write tasks.

last modified by:	Ron Prestenback
==========================================================================================================*/
void XboxLiveManager::KillStatsWrite() 
{
    guard(XboxLiveManager::KillStatsWrite);

	SetTaskHandle<XboxLiveOnlineTask>(m_phStatsWriteTask, NULL);

    unguard;
}

void XboxLiveManager::StartStatsWrite(INT NumStatProcs, XONLINE_STAT_PROC* StatProcs)
{
	guard(XboxLiveManager::StartStatsWrite);

	if (NumStatProcs > 0)
	{
		StatsWriteTask* StatsTask = new StatsWriteTask(this);
		SetTaskHandle<XboxLiveOnlineTask>(m_phStatsWriteTask, StatsTask);

		HRESULT Result = XOnlineStatWriteEx(NumStatProcs, StatProcs, NULL, &(*StatsTask));
		if (FAILED(Result))
		{
			debugf(TEXT("XboxLiveManager::StartStatsWrite -- XOnlineStatWriteEx failed: %s"), ParseXOnlineResult(Result));
			return;
		}
	}

	unguard;
}

void XboxLiveManager::StartStatsRead(INT NumStatSpecs, XONLINE_STAT_SPEC* StatSpecs)
{
	guard(XboxLiveManager::StartStatsWrite);


	StatsReadTask* StatsTask = new StatsReadTask(NumStatSpecs, StatSpecs, this);
	SetTaskHandle<XboxLiveOnlineTask>(m_phStatsReadTask,StatsTask);

	HRESULT Result = XOnlineStatRead(NumStatSpecs, StatSpecs, NULL, &(*StatsTask));
	if (FAILED(Result))
	{
		debugf(TEXT("XboxLiveManager::StartStatsRead -- XOnlineStatRead failed: %s"), ParseXOnlineResult(Result));
		return;
	}

	unguard;
}

void XboxLiveManager::StartStatsEnumerate(INT LeaderboardIndex, INT NumUsers, INT NumStats, WORD* Stats)
{
	guard(XboxLiveManager::StartStatsEnumerate);

	StatsEnumerateTask* StatsTask = new StatsEnumerateTask(NumUsers, NumStats, this);
	SetTaskHandle<XboxLiveOnlineTask>(m_phStatsReadTask,StatsTask);

	HRESULT Result = XOnlineStatLeaderEnumerate(NULL, 1, NumUsers, LeaderboardIndex, NumStats, Stats, NULL, &(*StatsTask));
	if (FAILED(Result))
	{
		debugf(TEXT("XboxLiveManager::StartStatsEnumerate -- XOnlineStatLeaderEnumerate failed: %s"), ParseXOnlineResult(Result));
		return;
	}

	unguard;
}

void XboxLiveManager::StartStatsRungQuery(INT RungIndex, FRungDataDetailed RungData)
{
	guard(XboxLiveManager::StartStatsRungQuery);

	StatsRungQueryTask* StatsTask = new StatsRungQueryTask(RungData, this);
	SetTaskHandle<XboxLiveOnlineTask>(m_phStatsReadTask, StatsTask);

	INT LeaderboardIndex = UPlayerStats::GetCacheIndex<ST_RUNG>(RungIndex, 0, SVT_INT) / UPlayerStats::NumFieldsPerTable;
	debugf(TEXT("StartStatsRungQuery  LeaderboardIndex:%i  RungIndex:%i"), LeaderboardIndex, RungIndex);

	INT NumUsers = StatsRungQueryTask::NumUsers;
	INT NumStatIndices = sizeof(StatsRungQueryTask::StatIndices) / sizeof(StatsRungQueryTask::StatIndices[0]);
	HRESULT Result = XOnlineStatLeaderEnumerate(NULL, 1, NumUsers, 1 + LeaderboardIndex,
												NumStatIndices, StatsRungQueryTask::StatIndices,
												NULL, &(*StatsTask));
	if (FAILED(Result))
	{
		debugf(TEXT("XboxLiveManager::StartStatsRungQuery -- XOnlineStatLeaderEnumerate failed: %s"), ParseXOnlineResult(Result));
		return;
	}

	unguard;
}

void XboxLiveManager::StartStatsReset(XUID User, INT LeaderboardIndex)
{
	guard(XboxLiveManager::StartStatsReset);

	StatsResetTask* StatsTask = new StatsResetTask(this);
	SetTaskHandle<XboxLiveOnlineTask>(m_phStatsWriteTask, StatsTask);

	HRESULT Result = XOnlineStatReset(User, LeaderboardIndex, NULL, &(*StatsTask));
	if (FAILED(Result))
	{
		debugf(TEXT("XboxLiveManager::StartStatsReset -- XOnlineStatReset failed: %s"), ParseXOnlineResult(Result));
		return;
	}

	unguard;
}

void XboxLiveManager::StartStatsLadderWrite(INT LadderIndex, INT RungIndex, FLOAT NewBestTime)
{
	guard(XboxLiveManager::StartStatsLadderWrite);

	StatsLadderWriteTask* StatsTask = new StatsLadderWriteTask(this);
	SetTaskHandle<XboxLiveOnlineTask>(m_phQueryWriteTask, StatsTask);

	StatsTask->State = StatsLadderWriteTask::STATE_Reading;
	StatsTask->LadderIndex = LadderIndex;
	StatsTask->RungIndex = RungIndex;
	StatsTask->NewBestTime = NewBestTime;

	XONLINE_ATTRIBUTE SearchAttributes[1];
	SearchAttributes[0].dwAttributeID = ParamPage;
	SearchAttributes[0].fChanged = TRUE;
	SearchAttributes[0].info.integer.qwValue = LadderIndex;

	HRESULT Result = XOnlineQuerySearch(DatasetRungs, ProcGetRungScores, 0, 1, NumRungScoresPerPage + 1,
										RungResultsSpecs, 1, SearchAttributes, NULL, &(*StatsTask));
	if (FAILED(Result))
	{
		debugf(TEXT("XboxLiveManager::StartStatsLadderWrite() -- XOnlineQuerySearch failed: %s"), ParseXOnlineResult(Result));
		return;
	}

	unguard;
}

void XboxLiveManager::StartStatsLadderRead(INT LadderIndex, const TArray<FRungDataSummary>& LadderData)
{
	guard(XboxLiveManager::StartStatsLadderRead);

	StatsLadderReadTask* StatsTask = new StatsLadderReadTask(this);
	SetTaskHandle<XboxLiveOnlineTask>(m_phStatsReadTask, StatsTask);

	StatsTask->LadderData.AddZeroed(LadderData.Num());
	for (INT Index = 0; Index < LadderData.Num(); Index++)
	{
		StatsTask->LadderData(Index).RungName = LadderData(Index).RungName;
		StatsTask->LadderData(Index).LadderIndex = LadderData(Index).LadderIndex;
		StatsTask->LadderData(Index).RungIndex = LadderData(Index).RungIndex;
	}

	XONLINE_ATTRIBUTE SearchAttributes[1];
	SearchAttributes[0].dwAttributeID = ParamPage;
	SearchAttributes[0].fChanged = TRUE;
	SearchAttributes[0].info.integer.qwValue = LadderIndex;

	HRESULT Result = XOnlineQuerySearch(DatasetRungs, ProcGetRungScores, 0, 1, NumRungScoresPerPage + 1,
										RungResultsSpecs, 1, SearchAttributes, NULL, &(*StatsTask));
	if (FAILED(Result))
	{
		debugf(TEXT("XboxLiveManager::StartStatsLadderRead() -- XOnlineQuerySearch failed: %s"), ParseXOnlineResult(Result));
		return;
	}

	unguard;
}

/* epic ====================================================================================================
* Function:	XboxLiveManager::StartMuteListQuery
* Author:	Per Vognsen
*
* Starts a new mutelist query.
* ==========================================================================================================
*/
void XboxLiveManager::StartMuteListQuery()
{
	guard(XboxLiveManager::StartMuteListQuery);

	UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
	if ( UIX == NULL )
	{
		return;
	}

	StopMuteListQuery();

	MuteListQueryTask* MuteListTask = new MuteListQueryTask(this);
	m_phMuteListTask = MuteListTask;

	DWORD ActualPort = UIX->getGamepadPort();
	XboxLiveUser* User = GetUser(ActualPort);
	if ( User )
	{
		// ghetto-azz xbox live always uses port 0 for silent sign-in, and for this particular
		// function call, the "fake" port needs to be used
		// https://xds.xbox.com/BPProgInfo.asp?Page=content/prog_faq_online.htm#uix_4

		BYTE UserPort = User->GetPort();
		HRESULT Result = XOnlineMutelistGet(UserPort, MAX_MUTELISTUSERS, NULL, &(*m_phMuteListTask), MuteListTask->MuteList, &(MuteListTask->MuteListSize));
		if (FAILED(Result))
		{
			debugf(NAME_DevLive, TEXT("StartMuteListQuery -- XOnlineMutelistGet failed."));
			return;
		}
	}
	else
	{
		debugf(NAME_DevLive,TEXT("Invalid gamepad port:%u"), ActualPort);
	}

	unguard;
}

/* epic ====================================================================================================
* Function:	XboxLiveManager::StopMuteListQuery
* Author:	Per Vognsen
*
* Stops the mutelist query task, if ongoing.
* ==========================================================================================================
*/
void XboxLiveManager::StopMuteListQuery()
{
	guard(XboxLiveManager::StopMuteListQuery);

	if (m_phMuteListTask != NULL)
	{
		delete m_phMuteListTask;
		m_phMuteListTask = NULL;
	}

	unguard;
}

/* epic ====================================================================================================
* Function:	XboxLiveManager::StopSessionQueries
* Author:	Per Vognsen
*
* Stops all ongoing session queries.
* ==========================================================================================================
*/
void XboxLiveManager::StopSessionQueries()
{
	guard(XboxLiveManager::StopSessionQueries);

	if (m_phQueryTask != NULL)
	{
		delete m_phQueryTask;
		m_phQueryTask = NULL;
	}	

	unguard;
}

template<class Type>
void XboxLiveManager::UpdateTask( Type*& phTask )
{
	guard(XboxLiveManager::UpdateTask);

	if (phTask == NULL)
	{
		warnf(NAME_Error, TEXT("%s: Task handle is NULL."), __FUNC_NAME__);
		return;
	}
	if (phTask->IsClosed())
	{
		warnf(NAME_Error, TEXT("%s: %s - task handle is closed."), __FUNC_NAME__, *phTask->Name());
		delete phTask;
		phTask = NULL;
		return;
	}

	// Wait for XboxLive server to return results
	HRESULT hr = phTask->Continue();
	if( hr == XONLINETASK_S_RUNNING )
		return;

	// Handle errors
	if( FAILED(hr) )
	{
		warnf(NAME_Error, TEXT("%s: %s Failed Continue with %s (0x%08X)."), __FUNC_NAME__, *phTask->Name(), ParseXOnlineResult(hr), hr);
		
		if (phTask->HandleFailure(hr))
		{
			delete phTask;
			phTask = NULL;
		}
	}
	else if ( phTask->Finished() )
	{
		phTask->Close();
		delete phTask;
		phTask = NULL;
	}
	unguard;
}

// --------------------
// StartNewSession
// --------------------
void XboxLiveManager::StartNewSession()
{
	guard(XboxLiveManager::StartNewSession);
	debugf(NAME_DevLive, TEXT("> %s"), __FUNC_NAME__);

	if( m_phMatchTask != NULL )
	{
		delete m_phMatchTask;
		m_phMatchTask = NULL;
	}

	UTcpNetDriverXbox* net = getNet();
	if (net && (net->LoadHostInfo()))
	{
		// if this is not the first session by this host, only update is required to get us back in the list of available matches.
		UpdateSession();
	}
	else
	{
		m_phMatchTask = new SessionStartTask(this);
		UpdateSessionAttribs();
		m_Attributes.touchAll();

		AGameInfo*  gameInfo = getEngine()->GLevel->GetLevelInfo()->Game;
		debugf(NAME_DevLive, TEXT("Starting new session - '%s' (%s) Purity:%u  Dedicated:%u  AvgSkill:%u"), *gameInfo->XLevel->URL.Map, *gameInfo->CurrentMaplistName, gameInfo->CalculatePurity(), gameInfo->bDedicatedServer, gameInfo->CalculateAverageSkill());
		INT PrivateSlotsFilled = Min( gameInfo->NumInvitedPlayers, gameInfo->NumReservedSlots );
		INT PrivateSlotsAvailable = gameInfo->NumReservedSlots - PrivateSlotsFilled;

		INT PublicSlotsFilled = gameInfo->NumPlayers - PrivateSlotsFilled;
		INT PublicSlotsAvailable = gameInfo->MaxPlayers - (gameInfo->NumReservedSlots + PublicSlotsFilled);
		
		HRESULT hr = XOnlineMatchSessionCreate(
			PublicSlotsFilled, PublicSlotsAvailable,
			PrivateSlotsFilled, PrivateSlotsAvailable,
			m_Attributes.size(),
			m_Attributes,
			NULL, &(*m_phMatchTask)
		);

		if( FAILED( hr ) )
		{
			if ( m_phMatchTask )
				delete m_phMatchTask;
			m_phMatchTask = NULL;
			debugf( NAME_Error, TEXT("XOnlineMatchSessionCreate failed: %s (0x%08X)"), ParseXOnlineResult(hr), hr);
			return;
		}

		m_MatchState = MM_SESSION_CREATE;
	}
	debugf(NAME_DevLive, TEXT("< %s"), __FUNC_NAME__);
	unguard;
}

/* Begin asynchronous end of session. */
void XboxLiveManager::EndSession()
{
	guard(XboxLiveManager::EndSession);

//	if ( m_MatchState == MM_SESSION_NONE )
//	{
//		debugf(NAME_DevLive, TEXT("EndSession called while match state is MM_SESSION_NONE!"));
//		return;
//	}

	UTcpNetDriverXbox * net = getNet();
	if (net == NULL || net->GetHostInfo() == NULL) 
	{
		return;
	}

	debugf(NAME_DevLive, TEXT("> %s"), __FUNC_NAME__);

	SetTaskHandle<XboxLiveOnlineTask>(m_phMatchTask, new SessionEndTask(this));

#ifndef FINAL_RELEASE
	{
		FString SessionID, ExchangeKey, HostAddr;

		BytesToString((BYTE*)&net->GetHostInfo()->SessionID, sizeof(net->GetHostInfo()->SessionID), SessionID);
		BytesToString((BYTE*)&net->GetHostInfo()->ExchangeKey, sizeof(net->GetHostInfo()->ExchangeKey), ExchangeKey);
		BytesToString((BYTE*)&net->GetHostInfo()->HostAddr, sizeof(net->GetHostInfo()->HostAddr), HostAddr);

		debugf( NAME_DevLive, TEXT("XboxLiveManager::EndSession: SessionID: %s"), *SessionID );
		debugf( NAME_DevLive, TEXT("XboxLiveManager::EndSession: ExchangeKey: %s"), *ExchangeKey );
		debugf( NAME_DevLive, TEXT("XboxLiveManager::EndSession: HostAddr: %s"), *HostAddr );
	}
#endif

	HRESULT hr = XOnlineMatchSessionDelete(
		net->GetHostInfo()->SessionID,
		NULL, &(*m_phMatchTask)
	);

	if( FAILED( hr ) )
	{
		SetTaskHandle<XboxLiveOnlineTask>(m_phMatchTask, NULL);
		debugf( NAME_Error, TEXT("XOnlineMatchSessionDelete failed: %s (0x%08X)"), ParseXOnlineResult(hr), hr);
		m_MatchState = MM_SESSION_NONE;
		return;
	}
	m_MatchState = MM_SESSION_DELETE;
	debugf(NAME_DevLive, TEXT("< %s"), __FUNC_NAME__);
	unguard;
}

// Synchronous deletion of a session.
void XboxLiveManager::EndSessionBlocking()
{
	guard(XboxLiveManager::EndSession);
//	if ( m_MatchState == MM_SESSION_NONE )
//	{
//		debugf(NAME_DevLive, TEXT("EndSessionBlocking called while match state is MM_SESSION_NONE!"));
//		return;
//	}

	UTcpNetDriverXbox * net = getNet();
	if (!net->GetHostInfo()) 
	{
		return;
	}

	// Launch the asynchronous delete
	EndSession();

	float timeOut = appSeconds() + BLOCKING_SESSION_DELETE_SECS;
	
	while (m_MatchState == MM_SESSION_DELETE)
	{
		UpdateTask<XboxLiveOnlineTask>(m_phMatchTask);
		if( m_MatchState == MM_SESSION_ERROR || appSeconds() > timeOut)
		{
			break;
		}
		else if (m_MatchState == MM_SESSION_NONE)
		{
			break;
		}
		debugf(TEXT("XBoxLiveManager::EndSessionBlocking -- Sleeping."));
		// wait for half a second to try again to delete the session
		appSleep(0.5f);
	}
	unguard;
}

// --------------------------
// Update online information
// --------------------------
UBOOL XboxLiveManager::UpdateSessionAttribs()
{
	guard(XboxLiveManager::UpdateSessionAttribs);
	UGameEngine* gameEngine = getEngine();
	ALevelInfo* levelInfo = gameEngine->GLevel->GetLevelInfo();
	// Make sure this call CAN succeed
	check(levelInfo);
	check(levelInfo->NetMode != NM_Client);
	AGameInfo*  gameInfo = levelInfo->Game;
	check(gameInfo);
	AGameReplicationInfo * GRI = gameInfo->GameReplicationInfo;
	check(GRI);

	UTcpNetDriverXbox * net = getNet();
	check(net);

	XNKID PreviousSessionID = net->getPreviousSessionID();

	int Dedicated = levelInfo->GRI->bDedicatedServer;
	FString HostName(*GRI->eventGetServerName());

	TArray<FString> MutatorList;

	// Concatenate the class names of all active mutators into a space-separated list.
	for (AMutator* Mutator = gameInfo->BaseMutator; Mutator != NULL; Mutator = Mutator->NextMutator)
	{
		const TCHAR* PackageName = Mutator->GetClass()->GetOuter() ? Mutator->GetClass()->GetOuter()->GetName() : TEXT("");
		
		// Skip mutators in the Engine package, e.g. Engine.Mutator.
		if (appStrncmp(PackageName, TEXT("Engine"), 6) == 0)
			continue;

		// Only include the package the mutator is in if it is not UCMutators.
		if (appStrncmp(PackageName, TEXT("UCMutators"), 10) == 0)
		{
			new(MutatorList) FString(Mutator->GetClass()->GetName());
		}
		else
		{
			new(MutatorList) FString(Mutator->GetClass()->GetPathName());
		}
	}

	// now build the string for the mutators list
	FString MutatorString;
	for ( INT i = 0; i < MutatorList.Num(); i++ )
	{
		if ( MutatorString != TEXT("") )
			MutatorString += TEXT(" ");

		MutatorString += MutatorList(i);
	}

	if (MutatorString.Len() > X_MAX_STRING_ATTRIBUTE_LEN - 1)
	{
		MutatorString = MutatorString.Left(X_MAX_STRING_ATTRIBUTE_LEN-1);
	}

	FString Players;
	for (INT PlayerIdx = 0; PlayerIdx < GRI->PRIArray.Num(); PlayerIdx++)
	{
		APlayerReplicationInfo* PRI = GRI->PRIArray(PlayerIdx);
		if ( PRI == NULL || PRI->bBot || PRI->bDedicatedServerSpectator || PRI->bOnlySpectator )
			continue;

		if ( Players != TEXT("") )
			Players += TEXT(";");

		Players += PRI->PlayerName;
	}

	if ( Players.Len() > X_MAX_STRING_ATTRIBUTE_LEN - 1 )
		Players = Players.Left(X_MAX_STRING_ATTRIBUTE_LEN - 1);

	UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
	check(UIX);
	INT GameTypeNdx = UIX->findGUIController()->eventGetGametypeIndex(GRI->GameClass->GetPathName());

	// GameTypeNdx is -1 if not a known game type, index of the matching gametype otherwise

	m_Attributes.set(XATTRIB_HOST, HostName);
	m_Attributes.set(XATTRIB_MAP, levelInfo->XLevel->URL.Map);
	m_Attributes.set(XATTRIB_MAPLIST, gameInfo->CurrentMaplistName);
	m_Attributes.set(XATTRIB_GAME_TYPE, GameTypeNdx);
	m_Attributes.set(XATTRIB_MUTATORS, MutatorString);
	m_Attributes.set(XATTRIB_DEDICATED, Dedicated);
	m_Attributes.set(XATTRIB_NUMBER_OF_PLAYERS, gameInfo->NumPlayers);
	m_Attributes.set(XATTRIB_MAXIMUM_PLAYERS, gameInfo->MaxPlayers);
	m_Attributes.set(XATTRIB_PREVIOUS_SESSION_ID, *(ULONGLONG *)&PreviousSessionID);

	GRI->ServerPurity = gameInfo->CalculatePurity();
	m_Attributes.set(XATTRIB_PURITY, GRI->ServerPurity);
	m_Attributes.set(XATTRIB_PLAYER_NAMES, Players);
	m_Attributes.set(XATTRIB_CUSTOM_CONTENT, UsingCustomContent(levelInfo->XLevel->URL.Map,MutatorList));

	BYTE PlayerSkill = gameInfo->CalculateAverageSkill();
	m_Attributes.set(XATTRIB_AVG_PLAYER_SKILL, PlayerSkill);

	for (UINT i = 0; i < m_Attributes.size(); ++i) {
		if (m_Attributes[i].fChanged)
			return 1;
	}

	return 0;
	unguard;
} // UpdateSessionAttribs

//-----------------------------------------------------------------------------
// UpdateSession() - Update game attributes and, if necessary, generate a new
// UpdateSessionTask.
//-----------------------------------------------------------------------------
void XboxLiveManager::UpdateSession()
{
	guard(XboxLiveManager::UpdateSession);
	if (!UpdateSessionAttribs()) 
	{
		m_SessionUpdateTimer = appSeconds() + SESSION_UPDATE_INTERVAL;
		return;
	}

	debugf(NAME_DevLive, TEXT("Updating session"));

	UGameEngine* gameEngine = getEngine();
	if ( gameEngine == NULL || gameEngine->GLevel == NULL )
		return;

	ALevelInfo* levelInfo = gameEngine->GLevel->GetLevelInfo();
	if ( levelInfo == NULL )
		return;

	AGameInfo*  gameInfo = levelInfo->Game;
	
	INT PrivateSlotsFilled = Min( gameInfo->NumInvitedPlayers, gameInfo->NumReservedSlots );
	INT PrivateSlotsAvailable = gameInfo->NumReservedSlots - PrivateSlotsFilled;

	INT PublicSlotsFilled = gameInfo->NumPlayers - PrivateSlotsFilled;
	INT PublicSlotsAvailable = gameInfo->MaxPlayers - (gameInfo->NumReservedSlots + PublicSlotsFilled);
	
	// grab the current 
	UTcpNetDriverXbox* net = getNet();
	if ( net )
	{
		UTcpNetDriverXbox::FSessionInfo* SessionInfo = net->GetHostInfo();
		check(SessionInfo);

		//	Create a new SessionUpdateTask, if necessary
		StartSessionUpdateTask();
		HRESULT hr = XOnlineMatchSessionUpdate( SessionInfo->SessionID,
			PublicSlotsFilled, PublicSlotsAvailable, PrivateSlotsFilled,
			PrivateSlotsAvailable, m_Attributes.size(), m_Attributes, NULL,
			&(*m_phUpdateTask) );

		if ( FAILED(hr) )
		{
			debugf(NAME_DevLive, TEXT("UpdateSession failed with error code: %s (0x08X)"), ParseXOnlineResult(hr), hr);
		}
	}

	unguard;
}

//-----------------------------------------------------------------------------
// Name: UpdatePlayerOnlineState()
// Desc: Broadcast updated User state for the world
//-----------------------------------------------------------------------------
UBOOL XboxLiveManager::UpdatePlayerOnlineState( DWORD dwUser, BOOL bForce )
{
	guard(XboxLiveManager::UpdatePlayerOnlineState);

	check( dwUser >= 0 );
	check( dwUser < XONLINE_MAX_LOGON_USERS );

	if( !Users[dwUser].LoggedIn() )
		return false;

	if( Users[dwUser].IsGuest() )
		return true;

	APlayerController* PC = NULL;
	UViewport* Viewport = NULL;
	UGameEngine* gameEngine = getEngine();
	UTcpNetDriverXbox* net = getNet();

	if (gameEngine == NULL || gameEngine->GLevel == NULL)
		return false;

	ALevelInfo* levelInfo = gameEngine->GLevel->GetLevelInfo();
	UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();

	if (UIX == NULL || UIX->m_pLiveEngine == NULL || levelInfo == NULL)
		return false;

	if ( gameEngine->Client && gameEngine->Client->Viewports.Num() )
	{
		Viewport = gameEngine->Client->Viewports(0);
		if ( Viewport )
			PC = Viewport->Actor;
	}

	if ( PC == NULL || PC->GameReplicationInfo == NULL )
		return false;

	AGameReplicationInfo* GRI = levelInfo->GRI ? levelInfo->GRI : PC->GameReplicationInfo;
	FString MapName = gameEngine->GLevel->URL.Map;
	if (MapName.Right(4) != TEXT(".unr"))
		MapName += TEXT(".unr");

	XNKID SessionID;
	// Setting the session ID to zero causes all invitations to be retracted.
	appMemzero( &SessionID, sizeof(XNKID) );

	UBOOL bPlaying = false;
	if ( net )
	{
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

		bPlaying = SessionInfo != NULL;
	}

	DWORD dwState = XONLINE_FRIENDSTATE_FLAG_NONE;
	UBOOL bInvitable = false;
	if ( PC )
	{
		if ( Viewport->Profile == NULL || !Viewport->Profile->eventGetAppearOffline(0) )
			dwState |= XONLINE_FRIENDSTATE_FLAG_ONLINE;

		if ( bPlaying )
			dwState |= XONLINE_FRIENDSTATE_FLAG_PLAYING;

		if ( bPlaying && GRI )
		{
			bInvitable = GRI->bInvitable;
			if ( GRI->bJoinable )
				dwState |= XONLINE_FRIENDSTATE_FLAG_JOINABLE;
		}

		DWORD dwMicrophoneState = XGetDevices( XDEVICE_TYPE_VOICE_MICROPHONE );
		DWORD dwHeadphoneState = XGetDevices( XDEVICE_TYPE_VOICE_HEADPHONE );
		DWORD UserFlags = Users[dwUser].GetUserFlags();

		UBOOL bVoiceActive = XOnlineIsUserVoiceAllowed( UserFlags ) &&
							( dwMicrophoneState & ( 1 << dwUser ) ) &&
							( dwHeadphoneState  & ( 1 << dwUser ) );

		if (bVoiceActive)
			dwState |= XONLINE_FRIENDSTATE_FLAG_VOICE;
	}

	HRESULT hr = UIX->m_pLiveEngine->NotificationSetState( Users[dwUser].GetPort(), dwState, SessionID, 0, NULL );
	if( FAILED(hr) )
	{
		debugf(NAME_DevLive, TEXT("NotificationSetState error %s (0x08X)"), ParseXOnlineResult(hr), hr);
		return false;
	}

	Users[dwUser].SetState(dwState);

	// Permit invitation IF the player is joinable
	hr = UIX->m_pLiveEngine->SetProperty(UIX_PROPERTY_ALLOW_GAME_INVITES, bInvitable);
	if( FAILED(hr) )
	{
		debugf(NAME_Error, TEXT("SetProperty error %s (0x08X)"), ParseXOnlineResult(hr), hr);
		return true;
	}

#ifndef FINAL_RELEASE
	XUID tempXUID = Users[dwUser].GetXUID();
	FString SessionIDString;
	BytesToString((BYTE*)&SessionID, sizeof(XNKID), SessionIDString);
	debugf(NAME_DevLive, TEXT("Set online status for %s (0x%016I64X) to %s for Session:%s"), Users[dwUser].GetGamerTag(), tempXUID.qwUserID, GetStateFlagText(dwState), *SessionIDString);
#else
	debugf( NAME_DevLive, TEXT("Set online status for %s to 0x%08X."), Users[dwUser].GetGamerTag(), dwState );
#endif

	return true;

	unguard;
}

UBOOL IsConnectionError( HRESULT hr )
{
	switch ( hr )
	{
	case XONLINE_E_LOGON_NO_NETWORK_CONNECTION:
	case XONLINE_E_LOGON_CANNOT_ACCESS_SERVICE:
	case XONLINE_E_LOGON_CONNECTION_LOST:
		return true;
	}

	return false;
}

#define HRESULT_TEXT(a) case (a): return appFromAnsi(#a)
const TCHAR * ParseXOnlineResult(HRESULT hr) 
{
	guard(ParseXOnlineResult);

	switch( hr )
	{
		HRESULT_TEXT(XONLINETASK_S_SUCCESS);
		HRESULT_TEXT(XONLINETASK_S_RUNNING);
		HRESULT_TEXT(XONLINETASK_S_RESULTS_AVAIL);
		HRESULT_TEXT(XONLINETASK_S_RUNNING_IDLE);

		//Generic Errors
		HRESULT_TEXT(XONLINE_E_OVERFLOW);
		HRESULT_TEXT(XONLINE_E_NO_SESSION);
		HRESULT_TEXT(XONLINE_E_USER_NOT_LOGGED_ON);
		HRESULT_TEXT(XONLINE_E_NO_GUEST_ACCESS);
		HRESULT_TEXT(XONLINE_E_NOT_INITIALIZED);
		HRESULT_TEXT(XONLINE_E_NO_USER);
		HRESULT_TEXT(XONLINE_E_INTERNAL_ERROR);
		HRESULT_TEXT(XONLINE_E_OUT_OF_MEMORY);
		HRESULT_TEXT(XONLINE_E_TASK_BUSY);
		HRESULT_TEXT(XONLINE_E_SERVER_ERROR);
		HRESULT_TEXT(XONLINE_E_IO_ERROR);
		HRESULT_TEXT(XONLINE_E_BAD_CONTENT_TYPE);
		HRESULT_TEXT(XONLINE_E_USER_NOT_PRESENT);
		HRESULT_TEXT(XONLINE_E_PROTOCOL_MISMATCH);
		HRESULT_TEXT(XONLINE_E_INVALID_SERVICE_ID);
		HRESULT_TEXT(XONLINE_E_INVALID_REQUEST);

		//Failures from XOnlineLogon
		HRESULT_TEXT(XONLINE_E_LOGON_NO_NETWORK_CONNECTION);
		HRESULT_TEXT(XONLINE_E_SILENT_LOGON_DISABLED);
		HRESULT_TEXT(XONLINE_E_SILENT_LOGON_NO_ACCOUNTS);
		HRESULT_TEXT(XONLINE_E_SILENT_LOGON_PASSCODE_REQUIRED);

		//XOnlineLogon task successful return states
		HRESULT_TEXT(XONLINE_S_LOGON_CONNECTION_ESTABLISHED);

		//XOnlineLogon task failure return values
		HRESULT_TEXT(XONLINE_E_LOGON_CANNOT_ACCESS_SERVICE);
		HRESULT_TEXT(XONLINE_E_LOGON_UPDATE_REQUIRED);
		HRESULT_TEXT(XONLINE_E_LOGON_SERVERS_TOO_BUSY);
		HRESULT_TEXT(XONLINE_E_LOGON_CONNECTION_LOST);
		HRESULT_TEXT(XONLINE_E_LOGON_KICKED_BY_DUPLICATE_LOGON);

		//Service errors after XOnlineLogon task completion
		HRESULT_TEXT(XONLINE_E_LOGON_SERVICE_NOT_REQUESTED);
		HRESULT_TEXT(XONLINE_E_LOGON_SERVICE_NOT_AUTHORIZED);
		HRESULT_TEXT(XONLINE_E_LOGON_SERVICE_TEMPORARILY_UNAVAILABLE);

		//User warnings after XOnlineLogon task completion
		HRESULT_TEXT(XONLINE_S_LOGON_USER_HAS_MESSAGE);

		//User errors after XOnlineLogon task completion
		HRESULT_TEXT(XONLINE_E_LOGON_USER_ACCOUNT_REQUIRES_MANAGEMENT);

		//XOnlineChangeLogonUsers task successful return states
		HRESULT_TEXT(XONLINE_S_LOGON_COMMIT_USER_CHANGE);
		HRESULT_TEXT(XONLINE_S_LOGON_USER_CHANGE_COMPLETE);

		//XOnlineChangeLogonUsers task failure return values
		HRESULT_TEXT(XONLINE_E_LOGON_CHANGE_USER_FAILED);

		//Other generic auth related errors
		HRESULT_TEXT(XONLINE_E_LOGON_MU_NOT_MOUNTED);
		HRESULT_TEXT(XONLINE_E_LOGON_MU_IO_ERROR);
		HRESULT_TEXT(XONLINE_E_LOGON_NOT_LOGGED_ON);

		//Errors returned by Presence/Notification
		HRESULT_TEXT(XONLINE_E_NOTIFICATION_BAD_CONTENT_TYPE);
		HRESULT_TEXT(XONLINE_E_NOTIFICATION_REQUEST_TOO_SMALL);
		HRESULT_TEXT(XONLINE_E_NOTIFICATION_INVALID_MESSAGE_TYPE);
		HRESULT_TEXT(XONLINE_E_NOTIFICATION_NO_ADDRESS);
		HRESULT_TEXT(XONLINE_E_NOTIFICATION_INVALID_PUID);
		HRESULT_TEXT(XONLINE_E_NOTIFICATION_NO_CONNECTION);
		HRESULT_TEXT(XONLINE_E_NOTIFICATION_SEND_FAILED);
		HRESULT_TEXT(XONLINE_E_NOTIFICATION_RECV_FAILED);
		HRESULT_TEXT(XONLINE_E_NOTIFICATION_MESSAGE_TRUNCATED);
		HRESULT_TEXT(XONLINE_E_NOTIFICATION_SERVER_BUSY);
		HRESULT_TEXT(XONLINE_E_NOTIFICATION_LIST_FULL);
		HRESULT_TEXT(XONLINE_E_NOTIFICATION_BLOCKED);
		HRESULT_TEXT(XONLINE_E_NOTIFICATION_FRIEND_PENDING);
		HRESULT_TEXT(XONLINE_E_NOTIFICATION_FLUSH_TICKETS);
		HRESULT_TEXT(XONLINE_E_NOTIFICATION_TOO_MANY_REQUESTS);
		HRESULT_TEXT(XONLINE_E_NOTIFICATION_USER_ALREADY_EXISTS);
		HRESULT_TEXT(XONLINE_E_NOTIFICATION_USER_NOT_FOUND);
		HRESULT_TEXT(XONLINE_E_NOTIFICATION_OTHER_LIST_FULL);
		HRESULT_TEXT(XONLINE_E_NOTIFICATION_SELF);
		HRESULT_TEXT(XONLINE_E_NOTIFICATION_SAME_TITLE);

		//Errors returned by matchmaking
		HRESULT_TEXT(XONLINE_E_MATCH_INVALID_SESSION_ID);
		HRESULT_TEXT(XONLINE_E_MATCH_INVALID_TITLE_ID);
		HRESULT_TEXT(XONLINE_E_MATCH_INVALID_DATA_TYPE);
		HRESULT_TEXT(XONLINE_E_MATCH_REQUEST_TOO_SMALL);
		HRESULT_TEXT(XONLINE_E_MATCH_REQUEST_TRUNCATED);
		HRESULT_TEXT(XONLINE_E_MATCH_INVALID_SEARCH_REQ);
		HRESULT_TEXT(XONLINE_E_MATCH_INVALID_OFFSET);
		HRESULT_TEXT(XONLINE_E_MATCH_INVALID_ATTR_TYPE);
		HRESULT_TEXT(XONLINE_E_MATCH_INVALID_VERSION);
		HRESULT_TEXT(XONLINE_E_MATCH_OVERFLOW);
		HRESULT_TEXT(XONLINE_E_MATCH_INVALID_RESULT_COL);
		HRESULT_TEXT(XONLINE_E_MATCH_INVALID_STRING);
		HRESULT_TEXT(XONLINE_E_MATCH_STRING_TOO_LONG);
		HRESULT_TEXT(XONLINE_E_MATCH_BLOB_TOO_LONG);
		//HRESULT_TEXT(XONLINE_E_MATCH_PUID_MISMATCH);
		//HRESULT_TEXT(XONLINE_E_MATCH_INVALID_PLAYER);
		HRESULT_TEXT(XONLINE_E_MATCH_INVALID_ATTRIBUTE_ID);
		//HRESULT_TEXT(XONLINE_E_MATCH_PLAYER_ALREADY_EXISTS);
		HRESULT_TEXT(XONLINE_E_MATCH_SESSION_ALREADY_EXISTS);
		//HRESULT_TEXT(XONLINE_E_MATCH_NO_TITLES);
		//HRESULT_TEXT(XONLINE_E_MATCH_TITLE_ALREADY_EXISTS);
		HRESULT_TEXT(XONLINE_E_MATCH_CRITICAL_DB_ERR);
		HRESULT_TEXT(XONLINE_E_MATCH_NOT_ENOUGH_COLUMNS);
		HRESULT_TEXT(XONLINE_E_MATCH_PERMISSION_DENIED);
		HRESULT_TEXT(XONLINE_E_MATCH_INVALID_PART_SCHEME);
		HRESULT_TEXT(XONLINE_E_MATCH_INVALID_PARAM);
		//HRESULT_TEXT(XONLINE_E_MATCH_USE_SETPLAYER_PROC);
		//HRESULT_TEXT(XONLINE_E_MATCH_PLAYER_REQUIRED);
		//HRESULT_TEXT(XONLINE_E_MATCH_PLAYER_NOT_NEEDED);
		HRESULT_TEXT(XONLINE_E_MATCH_DATA_TYPE_MISMATCH);
		HRESULT_TEXT(XONLINE_E_MATCH_SERVER_ERROR);
		HRESULT_TEXT(XONLINE_E_MATCH_NO_USERS);
		HRESULT_TEXT(XONLINE_E_MATCH_INVALID_BLOB);

		//Errors returned by billing/offerings
		HRESULT_TEXT(XONLINE_S_OFFERING_NEW_CONTENT);
		HRESULT_TEXT(XONLINE_S_OFFERING_NO_NEW_CONTENT);

		HRESULT_TEXT(XONLINE_E_OFFERING_BAD_REQUEST);
		HRESULT_TEXT(XONLINE_E_OFFERING_INVALID_USER);
		HRESULT_TEXT(XONLINE_E_OFFERING_INVALID_OFFER_ID);
		HRESULT_TEXT(XONLINE_E_OFFERING_INELIGIBLE_FOR_OFFER);
		HRESULT_TEXT(XONLINE_E_OFFERING_OFFER_EXPIRED);
		HRESULT_TEXT(XONLINE_E_OFFERING_SERVICE_UNREACHABLE);
		HRESULT_TEXT(XONLINE_E_OFFERING_PURCHASE_BLOCKED);
		HRESULT_TEXT(XONLINE_E_OFFERING_PURCHASE_DENIED);
		HRESULT_TEXT(XONLINE_E_OFFERING_BILLING_SERVER_ERROR);
		HRESULT_TEXT(XONLINE_E_OFFERING_OFFER_NOT_CANCELABLE);
		HRESULT_TEXT(XONLINE_E_OFFERING_NOTHING_TO_CANCEL);
		HRESULT_TEXT(XONLINE_E_OFFERING_ALREADY_OWN_MAX);
		HRESULT_TEXT(XONLINE_E_OFFERING_NO_CHARGE);
		HRESULT_TEXT(XONLINE_E_OFFERING_PERMISSION_DENIED);
		HRESULT_TEXT(XONLINE_E_OFFERING_NAME_TAKEN);

		HRESULT_TEXT(XONLINE_E_QUERY_QUOTA_FULL);
		HRESULT_TEXT(XONLINE_E_QUERY_ENTITY_NOT_FOUND);
		HRESULT_TEXT(XONLINE_E_QUERY_PERMISSION_DENIED);
		HRESULT_TEXT(XONLINE_E_QUERY_ATTRIBUTE_TOO_LONG);
		HRESULT_TEXT(XONLINE_E_QUERY_UNEXPECTED_ATTRIBUTE);
		HRESULT_TEXT(XONLINE_E_QUERY_INVALID_ACTION);
		HRESULT_TEXT(XONLINE_E_QUERY_SPEC_COUNT_MISMATCH);
		HRESULT_TEXT(XONLINE_E_QUERY_DATASET_NOT_FOUND);
		HRESULT_TEXT(XONLINE_E_QUERY_PROCEDURE_NOT_FOUND);
		HRESULT_TEXT(XONLINE_E_QUERY_DUPLICATE_ENTRY);
		HRESULT_TEXT(XONLINE_E_QUERY_RETRY);

		//Errors returned by uodb procs
		HRESULT_TEXT(XONLINE_E_UODB_KEY_ALREADY_EXISTS);

		//Errors returned by the MessageService
		HRESULT_TEXT(XONLINE_E_MSGSVR_INVALID_REQUEST);

		//Errors returned by the FeedbackService
		HRESULT_TEXT(XONLINE_E_FEEDBACK_NULL_TARGET);
		HRESULT_TEXT(XONLINE_E_FEEDBACK_BAD_TYPE);
		//HRESULT_TEXT(XONLINE_E_FEEDBACK_MISMATCH_NICKNAME_SIZE);
		HRESULT_TEXT(XONLINE_E_FEEDBACK_CANNOT_LOG);

		//Errors returned by the StatisticsService
		HRESULT_TEXT(XONLINE_E_STAT_BAD_REQUEST);
		HRESULT_TEXT(XONLINE_E_STAT_INVALID_TITLE_OR_LEADERBOARD);
		HRESULT_TEXT(XONLINE_E_STAT_TOO_MANY_SPECS);
		HRESULT_TEXT(XONLINE_E_STAT_TOO_MANY_STATS);
		HRESULT_TEXT(XONLINE_E_STAT_USER_NOT_FOUND);
		HRESULT_TEXT(XONLINE_E_STAT_SET_FAILED_0);
		HRESULT_TEXT(XONLINE_E_STAT_PERMISSION_DENIED);
		HRESULT_TEXT(XONLINE_E_STAT_LEADERBOARD_WAS_RESET);
		HRESULT_TEXT(XONLINE_E_STAT_INVALID_ATTACHMENT);
		HRESULT_TEXT(XONLINE_S_STAT_CAN_UPLOAD_ATTACHMENT);
		HRESULT_TEXT(XONLINE_E_STAT_TOO_MANY_PARAMETERS);
		HRESULT_TEXT(XONLINE_E_STAT_TOO_MANY_PROCEDURES);
		HRESULT_TEXT(XONLINE_E_STAT_STAT_POST_PROC_ERROR);
	default:
		break;
	}
	return TEXT("ERROR CODE NOT DEFINED!");

	unguard;
} // ParseXOnlineResult
