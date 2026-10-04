/* ================================================================
   XboxLive.h
   Author: brian
   Copyright: Scion Studios LLC (c) 2003

   This defines the base classes for XBoxLive operaion in the Avaris
   code base. Based in part on similar code from Unreal Championship

   ================================================================
*/

#ifndef XBOXLIVE_H
#define XBOXLIVE_H

#ifndef _XBOX
class XboxLiveManager;
class XboxLiveUser;
#else
class UTcpNetDriverXbox;

#include "GUI.h"
#include "UnUIXInteraction.h"
#include "XboxDrv.h"
#include "UnPlayerStats.h"
#include <xtl.h>
#include <xonline.h>

class UXboxLiveInterface;
typedef void (UXboxLiveInterface::*QueryCompletionFunc)();
typedef void (UXboxLiveInterface::*ExtraInfoCompletionFunc)(INT NumResults, const FString& ExtraInfo);

//-----------------------------------------------------------------------------
// Constants
//-----------------------------------------------------------------------------

//
// Attribute IDs                          ID num   Data Type
//                                        ------   ----------------------------
const DWORD XATTRIB_HOST				= 0x0001 | X_ATTRIBUTE_DATATYPE_STRING;
const DWORD XATTRIB_MAP					= 0x0002 | X_ATTRIBUTE_DATATYPE_STRING;
const DWORD XATTRIB_MUTATORS			= 0x0004 | X_ATTRIBUTE_DATATYPE_STRING;
const DWORD XATTRIB_DEDICATED			= 0x0005 | X_ATTRIBUTE_DATATYPE_INTEGER;
const DWORD XATTRIB_NUMBER_OF_PLAYERS	= 0x0006 | X_ATTRIBUTE_DATATYPE_INTEGER;
const DWORD XATTRIB_MAXIMUM_PLAYERS		= 0x0007 | X_ATTRIBUTE_DATATYPE_INTEGER;
const DWORD XATTRIB_PURITY				= 0x0009 | X_ATTRIBUTE_DATATYPE_INTEGER;
const DWORD XATTRIB_GAME_TYPE			= 0x000A | X_ATTRIBUTE_DATATYPE_INTEGER;
const DWORD XATTRIB_PREVIOUS_SESSION_ID	= 0x000B | X_ATTRIBUTE_DATATYPE_INTEGER;
const DWORD XATTRIB_MAPLIST				= 0x000C | X_ATTRIBUTE_DATATYPE_STRING;
const DWORD XATTRIB_PLAYER_NAMES		= 0x000D | X_ATTRIBUTE_DATATYPE_STRING;
const DWORD XATTRIB_CUSTOM_CONTENT		= 0x000E | X_ATTRIBUTE_DATATYPE_INTEGER;
const DWORD XATTRIB_AVG_PLAYER_SKILL	= 0x000F | X_ATTRIBUTE_DATATYPE_INTEGER;

//
// Attribute Maximum Lengths
// (for strings, this doesn't include the terminating NULL)
//
const DWORD XATTRIB_HOST_MAX_LEN = 65;		// Host attribute
const DWORD XATTRIB_MAP_MAX_LEN = 65;		// Map attribute
const DWORD XATTRIB_MUTATORS_MAX_LEN = 400;	// Mutators attribute
const DWORD XATTRIB_MAPLIST_MAX_LEN = 65;	// Maplist attribute
const DWORD XATTRIB_PLAYER_NAMES_MAX_LEN = 400; // PlayerNames attribute

// Specify X_MATCH_NULL_INTEGER for optional integer query arguments
const ULONGLONG X_MATCH_NULL_INTEGER  = 0x7FFFFFFFFFFFFFFFui64;

// Maximum number of sessions returned by MatchAnySession query
const DWORD MAX_MATCH_ANY_SESSION_RESULTS = 50;

// Maximum number of sessions returned by MatchBySessionID query
const DWORD MAX_MATCH_BY_SESSION_ID_RESULTS = 1;

// Maximum number of sessions returned by MatchOptimatch query
const DWORD MAX_MATCH_OPTIMATCH_RESULTS = 25;

// Maximum number of sessions returned by MatchByPreviousSessionID query
const DWORD MAX_MATCH_BY_PREVIOUS_SESSION_ID_RESULTS = 1;

// Maximum number of sessions returned by RetrieveExtraInfo query
const DWORD MAX_RETRIEVE_EXTRA_INFO_RESULTS = 1;

UBOOL IsConnectionError( HRESULT hr );

//
// FMatchSessionResult represents a single return result from a SessionQueryTask
//
#pragma pack(push, 1)
class FMatchSessionResult
{
public:
	// The query return attributes must come first, and in the order
	// returned by the query

	// QUERY RETURN ATTRIBUTES ->
	WCHAR     Host[XATTRIB_HOST_MAX_LEN+1];
	WCHAR     Map[XATTRIB_MAP_MAX_LEN+1];
	WCHAR     Mutators[XATTRIB_MUTATORS_MAX_LEN+1];
	WCHAR     Maplist[XATTRIB_MAPLIST_MAX_LEN+1];
	ULONGLONG GameType;
	ULONGLONG Purity;
	ULONGLONG Dedicated;
	ULONGLONG CustomContent;
	ULONGLONG AvgPlayerSkill;
	// <- QUERY RETURN ATTRIBUTES

	XNKID      SessionID;
	XNKEY      KeyExchangeKey;
	XNADDR     HostAddress;
	DWORD      PublicOpen;
	DWORD      PrivateOpen;
	DWORD      PublicFilled;
	DWORD      PrivateFilled;

	UBOOL		bValid;

	void Parse( FbeaconInformation& ServerData );

	FMatchSessionResult() 
	: bValid(1)
	{ }
};
#pragma pack(pop)

class MatchQueryResults
{
public:
	friend class QuickMatchQueryTask;
	friend class OptiMatchQueryTask;

	MatchQueryResults()
	{
		ClearResults();
	}

	void ClearResults()
	{
		appMemzero(m_Results, ARRAY_COUNT(m_Results));
		m_Size = 0;
	}

	UINT size()
	{
		return m_Size;
	}

	FMatchSessionResult&  operator[] (UINT i)
	{
		check(i < m_Size);
		return m_Results[i];
	}

private:
	UINT m_Size;
	FMatchSessionResult m_Results[MAX_MATCH_ANY_SESSION_RESULTS];
};


//
// CMatchBySessionIDResult represents a single return result from the
// MatchBySessionID query
//
#pragma pack(push, 1)
class FMatchSessionIDResult
{
public:
    // The query return attributes must come first, and in the order
    // returned by the query
	// QUERY RETURN ATTRIBUTES ->
    WCHAR     Host[XATTRIB_HOST_MAX_LEN+1];
    WCHAR     Map[XATTRIB_MAP_MAX_LEN+1];
    WCHAR     Mutators[XATTRIB_MUTATORS_MAX_LEN+1];
    ULONGLONG GameType;
	// <- QUERY RETURN ATTRIBUTES


    XNKID      SessionID;
    XNKEY      KeyExchangeKey;
    XNADDR     HostAddress;
    DWORD      PublicOpen;
    DWORD      PrivateOpen;
    DWORD      PublicFilled;
    DWORD      PrivateFilled;

};
#pragma pack(pop)



class MatchSessionIDResults
{
public:
	friend class SessionQueryTask;

	MatchSessionIDResults( INT count )
	{
		ClearResults();
		m_Size = count;
	}

	void ClearResults()
	{
		appMemzero(m_Results, ARRAY_COUNT(m_Results));
		m_Size = 0;
	}

	UINT size()
	{
		return m_Size;
	}

	FMatchSessionIDResult&  operator[] (UINT i)
	{
		check(i < m_Size);
		return m_Results[i];
	}

private:
	UINT m_Size;
	FMatchSessionIDResult m_Results[MAX_MATCH_BY_SESSION_ID_RESULTS];
};

// FRetrieveExtraInfoResult represents a single return result from the
// RetrieveExtraInfo query
//
#pragma pack(push, 1)
class FRetrieveExtraInfoResult
{
public:
	// The query return attributes must come first, and in the order
	// returned by the query
	WCHAR     PlayerNames[XATTRIB_PLAYER_NAMES_MAX_LEN+1];

	XNKID      SessionID;
	XNKEY      KeyExchangeKey;
	XNADDR     HostAddress;
	DWORD      PublicOpen;
	DWORD      PrivateOpen;
	DWORD      PublicFilled;
	DWORD      PrivateFilled;

};
#pragma pack(pop)

//  Collection of results for the RetrieveExtraInfo query
class FRetrieveExtraInfoResults
{
public:
	FRetrieveExtraInfoResults() { m_dwSize = 0; }
	DWORD Size() { return m_dwSize; }
	FRetrieveExtraInfoResult &operator[]( DWORD i ) { return v[i]; }
private:
	void Clear() { m_dwSize = 0; }
	void Remove( DWORD i )
	{
		assert( i < m_dwSize );
		if( i < m_dwSize )
		{
			m_dwSize--;
			memcpy( &v[i], &v[i+1], sizeof( v[0] ) * ( m_dwSize - i ) );
		}
	}
	void SetSize( DWORD dwSize ) { m_dwSize = dwSize; }
	friend class RetrieveExtraInfoTask;
	FRetrieveExtraInfoResult v[MAX_RETRIEVE_EXTRA_INFO_RESULTS];
	DWORD m_dwSize;
};

/* ================================================================
XboxLiveOnlineTask - Modified from Microsoft's XboxLiveOnlineTask in 
their sample code. Designed as a base class for specialized tasks
(that know how to terminate themselves and call the right 
event functions).
================================================================
*/

#ifndef XBOXLIVEONLINETASK_H
#define XBOXLIVEONLINETASK_H

class XboxLiveOnlineTask
{
	friend class XboxLiveManager;

protected:
	FString				m_TaskName;
	XONLINETASK_HANDLE	m_hOnlineTask;
	XboxLiveManager*	LiveInterface;
	enum TaskState
	{
		TS_Nothing,		// base value; should probably never occur
		TS_Query,		// querying on-line information
		TS_Update,		// updating on-line information
		TS_Probing,		// probing QoS
	};

	TaskState m_State;

public:
	explicit
	XboxLiveOnlineTask
	(
		XboxLiveManager*	myManager,
		XONLINETASK_HANDLE	hOnlineTask	= NULL,
		TaskState			ts			= TS_Nothing
	)
	: LiveInterface	( myManager						)
	, m_hOnlineTask	( hOnlineTask					)
	, m_TaskName	( TEXT("XboxLiveOnlineTask")	)
	, m_State		( ts							)
	{}

	/* Close the running task if it is not already closed. */
	virtual ~XboxLiveOnlineTask()
	{
		if (!IsClosed())
			Close();
	}

	const FString& Name() const { return m_TaskName; }

	operator XONLINETASK_HANDLE() const
	{
		return m_hOnlineTask;
	}

	XONLINETASK_HANDLE* operator&()
	{
		// If we're passing the task handle to a function that's changing it, the
		// handle should be closed (NULL). If this assertion fails, it's because
		// Close() -- i.e. XOnlineTaskClose -- wasn't called.
		check( m_hOnlineTask == NULL );
		return &m_hOnlineTask;
	}

	BOOL    IsOpen() const 
	{
		return( m_hOnlineTask != NULL );
	}

	BOOL    IsClosed() const
	{
		return( m_hOnlineTask == NULL );
	}

	void    Close()
	{
		if( m_hOnlineTask != NULL )
		{
			XOnlineTaskClose( m_hOnlineTask );
			m_hOnlineTask = NULL;
		}
	}

	virtual HRESULT Continue() 
	{
		return XOnlineTaskContinue( m_hOnlineTask );
	}

	virtual UBOOL Finished()
	{
		// handle signalling that the task wrapped in this object is finished. 
		// This "abstract" version does nothing. Specific types of tasks should handle
		// this operation appropriately.


		return true;
	}

	virtual UBOOL HandleFailure( HRESULT hr )
	{
		guard(XboxLiveOnlineTask::HandleFailure);
		UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
		if ( UIX )
		{
			if ( IsConnectionError(hr) )
                UIX->eventShowQuestion(LIVE_NoConnection,0);
			else UIX->eventShowMessage(Localize(TEXT("UIX"), TEXT("LiveErrorGeneric"), TEXT("Engine")), 0);
		}
		return TRUE;
		unguard;
	}

private:
	XboxLiveOnlineTask( const XboxLiveOnlineTask& );
	XboxLiveOnlineTask& operator=( const XboxLiveOnlineTask& );
};


/* ================================================================
   SessionQueryTask
   ================================================================
*/
class SessionQueryTask : public XboxLiveOnlineTask
{
	BOOL bRetryConnection;
	UTcpNetDriverXbox* theNet;

public:
	SessionQueryTask
	(
		XboxLiveManager* myManager,
		XONLINETASK_HANDLE hOnlineTask = NULL,
		BOOL bRetry = false,
		UTcpNetDriverXbox * netToUse = NULL
	)
	: XboxLiveOnlineTask(myManager, hOnlineTask, TS_Query)
	, bRetryConnection(bRetry)
	, theNet(netToUse)
	{
		m_TaskName = (TEXT("SessionQueryTask"));
	}

	virtual ~SessionQueryTask() { }

	virtual UBOOL HandleFailure( HRESULT hr )
	{
		guard(SessionQueryTask::HandleFailure);
		UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
		if ( UIX )
		{
			if ( IsConnectionError(hr) )
				UIX->eventShowQuestion(LIVE_NoConnection,0);
			else UIX->eventShowMessage(Localize(TEXT("UIX"), TEXT("LiveErrorSessionQuery"), TEXT("Engine")), 0);
		}
		return TRUE;
		unguard;
	}

	virtual UBOOL Finished();
	static const XONLINE_ATTRIBUTE_SPEC MatchBySessionIDAttributeSpec[];
};

/* ================================================================
SessionUpdateTask
================================================================
*/
class SessionUpdateTask : public XboxLiveOnlineTask
{
public:
	// ----- Constants -----
	static const float SESSION_UPDATE_INTERVAL;

	SessionUpdateTask
	(
		XboxLiveManager* myManager,
		XONLINETASK_HANDLE hOnlineTask = NULL,
		float update = SESSION_UPDATE_INTERVAL
	)
	: XboxLiveOnlineTask(myManager, hOnlineTask, TS_Query)
	, m_UpdateInterval(update)
	, m_UpdateTimer(update + appSeconds()) 
	{
			m_TaskName = (TEXT("SessionUpdateTask"));
	}

	virtual ~SessionUpdateTask() { }

	virtual HRESULT Continue();
	virtual UBOOL Finished();

	virtual UBOOL HandleFailure( HRESULT hr )
	{
		guard(SessionUpdateTask::HandleFailure);
		UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
		if ( UIX )
		{
			if ( IsConnectionError(hr) )
				UIX->eventShowQuestion(LIVE_ConnectionLost,0);
			else 
			{
				// do nothing - no need for error message for session udpate tasks, since they are called very often
				//UIX->eventShowMessage(Localize(TEXT("UIX"), TEXT("LiveErrorSessionUpdate"), TEXT("Engine")), 0);
			}

		}
		return TRUE;
		unguard;
	}

private:
	float m_UpdateInterval;
	float m_UpdateTimer;
};

/* ================================================================
SessionStartTask
================================================================
*/
class SessionStartTask : public XboxLiveOnlineTask
{
public:
	SessionStartTask( XboxLiveManager* myManager, XONLINETASK_HANDLE hOnlineTask = NULL )
	: XboxLiveOnlineTask(myManager, hOnlineTask, TS_Update)
	{
			m_TaskName = (TEXT("SessionStartTask"));
	}
	
	virtual ~SessionStartTask() { }

	virtual UBOOL Finished();

	virtual UBOOL HandleFailure( HRESULT hr )
	{
		guard(SessionStartTask::HandleFailure);
		UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
		if ( UIX )
		{
			if ( IsConnectionError(hr) )
				UIX->eventShowQuestion(LIVE_ConnectionLost,0);
			else UIX->eventShowMessage(Localize(TEXT("UIX"), TEXT("LiveErrorSessionBegin"), TEXT("Engine")), 0);
		}
		return TRUE;
		unguard;
	}
};

/* ================================================================
SessionEndTask
================================================================
*/
class SessionEndTask : public XboxLiveOnlineTask
{
public:
	SessionEndTask(XboxLiveManager * myManager, XONLINETASK_HANDLE hOnlineTask = NULL)
	: XboxLiveOnlineTask(myManager, hOnlineTask, TS_Update) 
	{
			m_TaskName = (TEXT("SessionEndTask"));
	}
	
	virtual ~SessionEndTask() { }

	virtual UBOOL Finished();
	virtual UBOOL HandleFailure( HRESULT hr )
	{
		guard(SessionEndTask::HandleFailure);
		UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
		if ( UIX )
		{
			if ( IsConnectionError(hr) )
				UIX->eventShowQuestion(LIVE_ConnectionLost,0);
			else
			{
				// do nothing
			}
		}
		return TRUE;
		unguard;
	}
};

/* ================================================================
QuickMatchQueryTask
================================================================
*/
class QuickMatchQueryTask : public XboxLiveOnlineTask
{
	friend class XboxLiveManager;

public:
	QuickMatchQueryTask(XboxLiveManager * myManager, UBOOL bInIncludeCustom, INT inPurity, INT inGametype, XONLINETASK_HANDLE hOnlineTask = NULL)
	: XboxLiveOnlineTask(myManager, hOnlineTask, TS_Query)
	, bIncludeCustom(bInIncludeCustom)
	, Purity(inPurity)
	, Gametype(inGametype)
	{
		m_TaskName = (TEXT("QuickMatchQueryTask"));
	}

	virtual ~QuickMatchQueryTask() { }
		 
	virtual HRESULT Continue()
	{
		if (m_State == TS_Query)
			return XboxLiveOnlineTask::Continue();

		else if (m_State == TS_Probing)
		{
			// handle QoS probe data here
		}

		return S_OK;
	}

	virtual UBOOL Finished();

	virtual UBOOL HandleFailure( HRESULT hr )
	{
		guard(QuickMatchQueryTask::HandleFailure);
		UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
		if ( UIX )
		{
			if ( IsConnectionError(hr) )
				UIX->eventShowQuestion(LIVE_NoConnection,0);
			else UIX->eventShowMessage(Localize(TEXT("UIX"), TEXT("LiveErrorQuickmatchQuery"), TEXT("Engine")), 0);
		}
		return TRUE;
		unguard;
	}

private:
	static const XONLINE_ATTRIBUTE_SPEC MatchAnySessionAttributeSpec[];

	UBOOL bIncludeCustom;
	INT Purity, Gametype;

	UXboxLiveInterface* XboxLiveInterface;
};

/* ================================================================
   OptiMatchQueryTask
   ================================================================
*/
class OptiMatchQueryTask : public XboxLiveOnlineTask
{
	friend class XboxLiveManager;

public:
	OptiMatchQueryTask(XboxLiveManager * myManager, XONLINETASK_HANDLE hOnlineTask = NULL)
	: XboxLiveOnlineTask(myManager, hOnlineTask, TS_Query)
	{
		m_TaskName = TEXT("OptiMatchQueryTask");
	}

	virtual ~OptiMatchQueryTask() { }

	virtual HRESULT Continue()
	{
		if (m_State == TS_Query)
			return XboxLiveOnlineTask::Continue();

		else if (m_State == TS_Probing) 
		{
			// handle QoS probe data here
		}

		return S_OK;
	}
	virtual UBOOL Finished();

	virtual UBOOL HandleFailure( HRESULT hr )
	{
		guard(OptiMatchQueryTask::HandleFailure);
		UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
		if ( UIX )
		{
			if ( IsConnectionError(hr) )
				UIX->eventShowQuestion(LIVE_NoConnection,0);
			else UIX->eventShowMessage(Localize(TEXT("UIX"), TEXT("LiveErrorOptimatchQuery"), TEXT("Engine")), 0);
		}
		return TRUE;
		unguard;
	}

private:
	static const XONLINE_ATTRIBUTE_SPEC MatchOptimatchAttributeSpec[];

	UXboxLiveInterface* XboxLiveInterface;
};

class RetrieveExtraInfoTask : public XboxLiveOnlineTask
{
	friend class XboxLiveManager;

public:
	FRetrieveExtraInfoResults Results;

	RetrieveExtraInfoTask(XboxLiveManager* Manager, XONLINETASK_HANDLE OnlineTask = NULL)
	: XboxLiveOnlineTask(Manager, OnlineTask, TS_Query)
	, XboxLiveInterface(NULL)
	, ExtraInfoCompletion(NULL)
	{
		m_TaskName = TEXT("RetrieveExtraInfoTask");
	}

	virtual ~RetrieveExtraInfoTask() { }

	virtual HRESULT Continue()
	{
		guard(RetrieveExtraInfoTask::Continue);

		if (m_State == TS_Query)
		{
			return XboxLiveOnlineTask::Continue();
		}

		return S_OK;
		unguard;
	}

	virtual UBOOL Finished();

	virtual UBOOL HandleFailure(HRESULT Result)
	{
		guard(RetrieveExtraInfoTask::HandleFailure);

		// Report failure.
		if (XboxLiveInterface != NULL && ExtraInfoCompletion != NULL)
		{
			(XboxLiveInterface->*ExtraInfoCompletion)(0, TEXT(""));
		}

		return TRUE;
		unguard;
	}

protected:
	static const XONLINE_ATTRIBUTE_SPEC AttributeSpec[];

private:
	UXboxLiveInterface* XboxLiveInterface;
	ExtraInfoCompletionFunc ExtraInfoCompletion;
};

class StatsWriteTask : public XboxLiveOnlineTask
{
	friend class XboxLiveManager;

public:
	StatsWriteTask(XboxLiveManager* Manager, XONLINETASK_HANDLE OnlineTask = NULL)
	: XboxLiveOnlineTask(Manager, OnlineTask, TS_Update)
	{
		m_TaskName = TEXT("StatsWriteTask");
	}

	virtual ~StatsWriteTask() { }

	virtual UBOOL Finished();
};

class StatsReadTask : public XboxLiveOnlineTask
{
	friend class XboxLiveManager;

public:
	StatsReadTask(INT InNumStatSpecs, XONLINE_STAT_SPEC* InStatSpecs,
		          XboxLiveManager* Manager, XONLINETASK_HANDLE OnlineTask = NULL)
	: XboxLiveOnlineTask(Manager, OnlineTask, TS_Query), NumStatSpecs(InNumStatSpecs), StatSpecs(InStatSpecs)	
	{
		m_TaskName = TEXT("StatsReadTask");
	}

	virtual ~StatsReadTask() { }

	virtual BOOL HandleFailure(HRESULT Result)
	{
		delete[] StatSpecs;
		return TRUE;
	}

	virtual UBOOL Finished();

private:
	INT NumStatSpecs;
	XONLINE_STAT_SPEC* StatSpecs;
};

class StatsEnumerateTask : public XboxLiveOnlineTask
{
	friend class XboxLiveManager;

public:
	StatsEnumerateTask(INT InNumUsers, INT InNumStats, XboxLiveManager* Manager, XONLINETASK_HANDLE OnlineTask = NULL)
	: XboxLiveOnlineTask(Manager, OnlineTask, TS_Query), NumStats(InNumStats), NumUsers(InNumUsers)
	{
		m_TaskName = TEXT("StatsEnumerateTask");
	}

	virtual ~StatsEnumerateTask() { }

	virtual UBOOL Finished();

private:
	INT NumStats;
	INT NumUsers;
};

class StatsRungQueryTask : public XboxLiveOnlineTask
{
	friend class XboxLiveManager;

public:
	StatsRungQueryTask(FRungDataDetailed InRungData, XboxLiveManager* Manager, XONLINETASK_HANDLE OnlineTask = NULL)
	: XboxLiveOnlineTask(Manager, OnlineTask, TS_Query), RungData(InRungData)
	{
		m_TaskName = TEXT("StatsRungQueryTask");
	}

	virtual ~StatsRungQueryTask() { }

	virtual UBOOL Finished();

private:
	enum { NumUsers = 100 };
	static const WORD StatIndices[];
	FRungDataDetailed RungData;
};

class StatsResetTask : public XboxLiveOnlineTask
{
	friend class XboxLiveManager;

public:
	StatsResetTask(XboxLiveManager* Manager, XONLINETASK_HANDLE OnlineTask = NULL)
	: XboxLiveOnlineTask(Manager, OnlineTask, TS_Update)
	{
		m_TaskName = TEXT("StatsResetTask");
	}

	virtual ~StatsResetTask() { }
};

class StatsLadderReadTask : public XboxLiveOnlineTask
{
	friend class XboxLiveManager;

public:
	StatsLadderReadTask(XboxLiveManager* Manager, XONLINETASK_HANDLE OnlineTask = NULL)
	: XboxLiveOnlineTask(Manager, OnlineTask, TS_Query), LadderData()
	{
		m_TaskName = TEXT("StatsLadderReadTask");
	}

	virtual ~StatsLadderReadTask() { }

	virtual UBOOL Finished();

private:
	TArray<FRungDataSummary> LadderData;
};

class StatsLadderWriteTask : public XboxLiveOnlineTask
{
	friend class XboxLiveManager;

public:
	StatsLadderWriteTask(XboxLiveManager* Manager, XONLINETASK_HANDLE OnlineTask = NULL)
	: XboxLiveOnlineTask(Manager, OnlineTask, TS_Update)
	{
		m_TaskName = TEXT("StatsLadderWriteTask");
	}

	virtual ~StatsLadderWriteTask() { }

	virtual UBOOL Finished();
	virtual UBOOL HandleFailure(HRESULT Result)
	{
		if (Result == XONLINE_E_QUERY_ENTITY_NOT_FOUND || Result == XONLINE_E_QUERY_PERMISSION_DENIED)
		{
			Finished();
			return FALSE;
		}
		else
		{
			return TRUE;
		}
	}

private:
	enum { STATE_Reading, STATE_Removing, STATE_Writing } State;

	INT LadderIndex;
	INT RungIndex;
	FLOAT NewBestTime;	
	TArray<QWORD> BestScores;
};

class StatsQueryTask : public XboxLiveOnlineTask
{
	friend class XboxLiveManager;

public:
	StatsQueryTask(XboxLiveManager* Manager, XONLINETASK_HANDLE TaskHandle = NULL)
	: XboxLiveOnlineTask(Manager, TaskHandle, TS_Query), NumStatSpecs(0), StatSpecs(NULL)
	{
		m_TaskName = TEXT("StatsQueryTask");
	}

	virtual ~StatsQueryTask() { }

	virtual UBOOL HandleFailure(HRESULT Result)
	{
		if ( Query )
		{
			while ( Query->QuerySpecs.Num() )
			{
				FPlayerQuerySpec& Dest = Query->QuerySpecs(0);
				Dest.DestPlayerStat->eventReceivedQueryResults(true);
				Query->QuerySpecs.Remove(0);
			}

			Query->QueryCompleted(true);
		}
		delete[] StatSpecs;
		return TRUE;
	}

	virtual UBOOL Finished();

private:
	INT NumStatSpecs;
	XONLINE_STAT_SPEC* StatSpecs;
	UStatsQuery* Query;
	TArray<UPlayerStats*> DestPlayerStats;
};

class MuteListQueryTask : public XboxLiveOnlineTask
{
	friend class XboxLiveManager;

public:
	MuteListQueryTask(XboxLiveManager* Manager, XONLINETASK_HANDLE TaskHandle = NULL)
	: XboxLiveOnlineTask(Manager, TaskHandle, TS_Query)
	{
		m_TaskName = TEXT("MuteListQueryTask");
	}

	virtual ~MuteListQueryTask() { }

	virtual HRESULT Continue()
	{
		guard(MuteListQueryTask::Continue);

		if (m_State == TS_Query)
		{
			return XboxLiveOnlineTask::Continue();
		}

		return S_OK;
		unguard;
	}
	virtual UBOOL Finished();
	
protected:
#ifdef _XBOX
	XONLINE_MUTELISTUSER MuteList[MAX_MUTELISTUSERS];
	DWORD MuteListSize;
#endif
};

#endif // XBOXLIVEONLINETASK_H

#ifndef XBOXLIVESESSIONATTRIBUTES_H
#define XBOXLIVESESSIONATTRIBUTES_H
/* ================================================================
   XboxLiveSessionAttributes
   ================================================================
*/
class XboxLiveSessionAttributes
{
public:
	XboxLiveSessionAttributes()
	: m_SessionAttributeCount(0)
	{
		appMemzero(m_SessionAttributes, sizeof(m_SessionAttributes));
	}

	void Clear();
	void touchAll();
	void set(DWORD attribID); // sets to approriate NULL value
	void set(DWORD attribID, const FString & strValue);
	void set(DWORD attribID, ULONGLONG intValue);
	void set(DWORD attribID, void * binaryValue, INT binaryValueLength);
	UINT size() { return m_SessionAttributeCount; }
	XONLINE_ATTRIBUTE* get() { return m_SessionAttributes; }
	operator XONLINE_ATTRIBUTE* ()
	{
		return m_SessionAttributes;
	}
private:
	UINT slot(DWORD attribID);
	static const DWORD		MAX_SESSION_ATTRIBUTES = 13;
	XONLINE_ATTRIBUTE       m_SessionAttributes[ MAX_SESSION_ATTRIBUTES ];
	UINT                    m_SessionAttributeCount;
};
#endif XBOXLIVESESSIONATTRIBUTES_H

const TCHAR * ParseXOnlineResult(HRESULT hr);

/* ================================================================
   XboxLiveUser
   ================================================================
*/
class XboxLiveUser 
{
public:
	XboxLiveUser()
	: XUser(NULL)
	, bVoiceAllowed(0)
	, bGuest(0)
	, OnlineState(XONLINE_FRIENDSTATE_FLAG_NONE)
	{
		*strGamertag = 0;
	}

	void LinkWithXUser( XONLINE_USER* in_Xuser )
	{
		XUser = in_Xuser;
		if ( XUser == NULL )
			Clear();
		else
		{
			if ( !LoggedIn() )
			{
				*strGamertag = 0;
				bVoiceAllowed = FALSE;
				bGuest = 0;
				OnlineState = XONLINE_FRIENDSTATE_FLAG_NONE;
			}
			else
			{
				debugf(NAME_DevLive, TEXT("Logging in user (0x%016I64X - 0x%08X) on port:%i"), XUser->xuid.qwUserID, XUser->xuid.dwUserFlags, GetPort());
				appStrncpy(strGamertag, appFromAnsi(XUser->szGamertag), ARRAY_COUNT(strGamertag));
				bGuest = XOnlineIsUserGuest(XUser->xuid.dwUserFlags);
				bVoiceAllowed = XOnlineIsUserVoiceAllowed( XUser->xuid.dwUserFlags );
			}
		}
	}

	void Clear()
	{
		*strGamertag = 0;
		bVoiceAllowed = FALSE;
		bGuest = 0;
		OnlineState = XONLINE_FRIENDSTATE_FLAG_NONE;
		XUser = NULL;
		SetPort(255);
	}

	UBOOL Valid() const
	{
		return LoggedIn() && !IsGuest();
	}

	UBOOL LoggedIn() const
	{
		return XUser && XUser->xuid.qwUserID != 0;
	}

	UBOOL IsGuest() const
	{
		return bGuest;
	}

	UBOOL VoiceAllowed() const
	{
		return bVoiceAllowed;
	}

	DWORD GetUserFlags() const
	{
		if ( XUser )
			return XUser->xuid.dwUserFlags;

		return 0;
	}

	void SetState(DWORD newFlags)
	{
		OnlineState = newFlags;
	}

	DWORD GetState() const
	{
		return OnlineState;
	}

	XUID GetXUID() const
	{
		if ( XUser )
			return XUser->xuid;
		return XUID();
	}

	UBOOL GetPlayerXUID( BYTE* PlayerXUID ) const
	{
		guard(XBoxUser::GetXUID);
		if ( XUser && LoggedIn() )
		{
			appMemcpy(PlayerXUID, &XUser->xuid, sizeof(XUID));
			return true;
		}
		return false;

		unguard;
	}

	const HRESULT GetStatus() const
	{
		return XUser ? XUser->hr : 0;
	}

	const TCHAR* GetGamerTag() const
	{
		return strGamertag;
	}

	void SetPort( BYTE Port )
	{
		UserPort = Port;
	}
	BYTE GetPort() const
	{
		return UserPort;
	}

private:
	TCHAR	strGamertag[XONLINE_MAX_GAMERTAG_LENGTH+1];
	UBOOL	bVoiceAllowed;
	UBOOL	bGuest;

	/* hack (recommended by microsoft no less) for working around issue with silent-login.
	silent-login always logs in as port 0, regardless of which gamepad actually initiated the sign-in
	manual login logs the user into live correctly.
	UserPort will contain the login position (0 based) for this XBoxUser -

	If the game is started using gamepad port 3, assuming a successful a silent signin, 
	Users[3] of XboxLiveManager would return true for SignedIn(), but Users[3].UserPort
	will be 0
	*/
	BYTE	UserPort;
	DWORD	OnlineState;
	XONLINE_USER* XUser;
};

/* ================================================================
   XboxLiveManager: Singleton that keeps track of users, user state,
   on-line tasks, and on-line task's state.
   ================================================================
*/

class UGameEngine;
class UTcpNetDriverXbox;
const int XBOXLIVE_MANAGER_USERCOUNT = 4;	// maximum number of users 

/* Note that these numbers were proted from UC1; they could well benefit from 
   some UC2 based love
*/
const float SESSION_CREATE_INTERVAL = 5.0f;		// time between retries for session creation
const float SESSION_UPDATE_INTERVAL = 10.0f;	// steady-state time between session update calls
const float LAST_MATCH_RETRY_TIME = 1.f;		// time (in seconds) to wait between retries
const double PLAYER_LIST_UPDATE_INTERVAL = 1.0f;// time (in seconds) between updates of the player list.
const float BLOCKING_SESSION_DELETE_SECS = 30.0f; // max seconds to block while deleting a session

class XboxLiveManager
{
	friend class XboxLiveOnlineTask;
	friend class QuickMatchQueryTask;
	friend class OptiMatchQueryTask;
	friend class SessionStartTask;
	friend class SessionEndTask;
	friend class SessionQueryTask;
	friend class MuteListQueryTask;

	void SetInterface()
	{
		guard(XboxLiveManager::SetInterface);

		UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
		check(UIX);

		UGUIController* Controller = UIX->findGUIController();
		check(Controller);

		Interface = Controller->eventGetXboxLiveInterface();
		unguard;
	}

protected:
	XboxLiveManager();
	// These two are unimplemented; just here to enforce singleton-ness
	XboxLiveManager(const XboxLiveManager & original);
	XboxLiveManager & operator= (const XboxLiveManager & rhs);

public:
	
	enum MatchMakingState
	{
		MM_SESSION_NONE,
		MM_SESSION_ERROR,               // Not authenticated, do not use live services!
		MM_SESSION_CREATE,              // Creating session.
		MM_SESSION_CREATE_RETRY,        // Waiting to retry match creation.
		MM_SESSION_DELETE,              // Deleting session.
		MM_SESSION_UPDATE,              // Updating session.
		MM_SESSION_ACTIVE,              // Session is active.
		MM_SESSION_CLIENT_TRAVEL,		// Looking to follow a session across XboxLive
		MM_SESSION_CLIENT_TRAVEL_RETRY,		// Looking to follow a session across XboxLive
	};

	static XboxLiveManager* getXboxLiveManager()
	{
		if (theOnlyManager == NULL)
			theOnlyManager = new XboxLiveManager();
		return theOnlyManager;
	}

	UXboxLiveInterface* GetInterface()
	{
		if ( Interface == NULL )
			SetInterface();
		return Interface;
	}

	virtual void Tick();

	// Implemented in XboxLiveMatchMaking.cpp
	UBOOL UsingCustomContent( const FString& MapName, const TArray<FString>& Mutators ) const;

	void StartFindFromPreviousIDQuery( XNKID sessionID, BOOL bWasInvited, BOOL bRetryConnection = false, UTcpNetDriverXbox * netToUse = NULL );
	void StartFindFromIDQuery( XNKID sessionID, BOOL bWasInvited, BOOL bRetryConnection = false, UTcpNetDriverXbox * netToUse = NULL );
	void StartQuickMatchQuery( INT Gametype, UBOOL bIncludeCustom, INT PlayerSkill, INT Purity );
	void StartOptimatchMatchQuery( INT GameType, INT MinPlayers, INT MaxPlayers, INT Dedicated, INT Purity, INT PlayerSkill );
	void StartExtraInfoQuery(XNKID SessionID, UXboxLiveInterface* LiveInterface, ExtraInfoCompletionFunc CompletionFunc);
	void StartMuteListQuery();
	void StopMuteListQuery();

	void StopSessionQueries();

#ifdef _XBOX
	void StartStatsQuery(INT NumStatSpecs, XONLINE_STAT_SPEC* StatSpecs, UStatsQuery* QueryItem);
	void StartStatsWrite(INT NumStatProcs, XONLINE_STAT_PROC* StatProcs);
	void StartStatsRead(INT NumStatSpecs, XONLINE_STAT_SPEC* StatSpecs);
	void StartStatsEnumerate(INT LeaderboardIndex, INT NumUsers, INT NumStats, WORD* Stats);
	void StartStatsRungQuery(INT RungIndex, FRungDataDetailed RungData);
	void StartStatsReset(XUID User, INT LeaderboardIndex);
	void StartStatsLadderWrite(INT LadderIndex, INT RungIndex, FLOAT NewBestTime);
	void StartStatsLadderRead(INT LadderIndex, const TArray<FRungDataSummary>& LadderData);

	void KillStatsQuery();
	void KillStatsWrite();
	void KillQueryWrite();
#endif // _XBOX

	void StartNewSession();

	template<class Type>
	void UpdateTask(Type*& phTask);
	void ConnectTo(PXONLINE_MATCH_SEARCHRESULT sr);
	void RetryConnectTo(PXONLINE_MATCH_SEARCHRESULT sr, UTcpNetDriverXbox * netToUse = NULL);

	template< class QueryClass >
	void SetTaskHandle( QueryClass*& phMemberTask, QueryClass* phNewTask );

	// Start the asynch ending (deletion) of an Xbox Live session.
	void EndSession();

	// Start the sync ending (deletion) of an Xbox Live session.
	void EndSessionBlocking();

	// examine server status and update matchmaking's image of the game
	void UpdateSession();
	// update the online status for the user; on-line or not, joinable or not, etc.
	UBOOL UpdatePlayerOnlineState( DWORD dwUser, BOOL bForce = false);
	
	void StartSessionUpdateTask() 
	{ 
		guard(XboxLiveManager::StartSessionUpdateTask);

		m_MatchState = MM_SESSION_ACTIVE;
		if ( m_phUpdateTask == NULL )
			m_phUpdateTask = new SessionUpdateTask(this);

		// if the session update task's online handle is valid at this point, it means that session
		// attributes were updated while we were pumping an existing session update task.
		// in this case, the data from the previous session update task is no longer valid, so 
		// we can just close the current task and start a new one
		if ( m_phUpdateTask && m_phUpdateTask->IsOpen() )
			m_phUpdateTask->Close();

		unguard;
	}

	void CheckUserStates()
	{
		for ( INT i = 0; i < XBOXLIVE_MANAGER_USERCOUNT; i++ )
		{
			XboxLiveUser* User = GetUser(i);
			if ( User && User->LoggedIn() )
			{
				static HRESULT lastStatus = S_OK;
				HRESULT hr = User->GetStatus();
				if ( hr != lastStatus )
				{
					debugf(NAME_DevLive, TEXT("%f Login flags for user %s: %i (%s)"), (FLOAT)appSeconds(), User->GetGamerTag(), hr, ParseXOnlineResult(hr));
					lastStatus = hr;
					if ( hr == XONLINE_S_LOGON_USER_HAS_MESSAGE )
					{
						UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
						if ( UIX )
						{
							// this user has messages waiting - notify the user about the message,
							// and give the option to check it, but it's not required.
							UIX->eventShowQuestion(CODE_UserHasMessage,0);
						}
					}
				}
			}
		}
	}

	// using clearUser, clear all of the users' information
	void clearAllUsers()
	{
		for (INT i = 0; i < XBOXLIVE_MANAGER_USERCOUNT; ++i)
			Users[i].Clear();
	}

	UBOOL   StoredHostInfo;
	FString StoredHostSessionID;
	FString StoredHostExchangeKey;

	FORCEINLINE UGameEngine * getEngine();
	FORCEINLINE UTcpNetDriverXbox * getNet();
	FORCEINLINE UGUIController* getGUIController()
	{
		UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
		if ( UIX )
			return UIX->findGUIController();

		return NULL;
	}

	XboxLiveUser* GetUser( BYTE GamepadPort )
	{
		if ( GamepadPort < XBOXLIVE_MANAGER_USERCOUNT )
			return &Users[GamepadPort];

		return NULL;
	}

	UBOOL QueryComplete( UBOOL bValidate = 0, INT Gametype = -1, UBOOL bIncludeCustom = 0, INT Purity = 0 );

	// Note that optimatch and quick match queries return results in the same format so 
	// there is only one routine to fetch them 
	MatchQueryResults* getMatchQueryResults()
	{ 
		return m_phMatchResults; 
	}

	void ClearQueryResults()
	{
		if (m_phMatchResults)
		{
			delete m_phMatchResults;
			m_phMatchResults = NULL;
		}
	}

	MatchMakingState getMatchState()
	{
		return m_MatchState;
	}

	FORCEINLINE void StartFindMatchCounter( FLOAT QueryStartTime )
	{
		m_FindMatchStartTime = QueryStartTime;
	}

	MatchMakingState			m_MatchState;
	UBOOL						bSilentSignin;

private:
	// ------ Pointer at the single instance
	static XboxLiveManager * theOnlyManager;
	UXboxLiveInterface*		Interface;

	// Survey the state of the game, copying attributes into the m_Attributes
	// data member. Return 1 if there 
	UBOOL UpdateSessionAttribs();

	XboxLiveUser			Users[XBOXLIVE_MANAGER_USERCOUNT];

	XboxLiveSessionAttributes	m_Attributes;

	// holds a finished QuickMatchQueryTask so we have access to the collection
	MatchQueryResults*			m_phMatchResults;

	// Tasks -- There should only be one task of each "type" operating at any one time. The exact type of the task is determined 
	// when it is created and the different task types have "personalized" Finished functions.
	XboxLiveOnlineTask*	m_phQueryTask;
	XboxLiveOnlineTask*	m_phMatchTask;
	XboxLiveOnlineTask*	m_phUpdateTask;
	XboxLiveOnlineTask*	m_phStatsReadTask;
	XboxLiveOnlineTask*	m_phStatsWriteTask;
	XboxLiveOnlineTask*	m_phQueryWriteTask;
	RetrieveExtraInfoTask* m_phExtraInfoTask;
	MuteListQueryTask*	m_phMuteListTask;
	StatsQueryTask*		m_phCacheQueryTask;

	// Timers and counters for retries of various items.
	FLOAT               m_SessionCreateTimer;
	INT                 m_SessionCreateRetries;
	FLOAT				m_FindMatchTimer;
	FLOAT				m_FindMatchStartTime;
	FLOAT				m_FindMatchTimeout;
	FLOAT               m_SessionUpdateTimer;
	UBOOL               m_bForceSessionUpdate;
	FLOAT				m_NextPlayersListUpdate;

	XNKID				m_SessionID;
	XNKID				m_PreviousSessionID;
	UTcpNetDriverXbox*	m_pNetToUse;
	UBOOL				m_bWasInvited;
	UBOOL				m_bRetryConnection;

	DWORD				DesiredSkill;

};

#ifndef FINAL_RELEASE
#define CHECKFLAG(flag) if ( State & flag ) { if (*c) appStrncat(c,TEXT(","),1024); appStrncat(c,TEXT(#flag)+25,1024); }
TCHAR* GetStateFlagText( DWORD State );
#endif	// FINAL_RELEASE

#endif // _XBOX
#endif // XBOXLIVE_H
