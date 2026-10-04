/* Implementation file for the native classes in the UCMenu package */


//jg -- DO NOT BREAK MY PCH SETTINGS!
#include "GUI.h"

#if __STATIC_LINK
#undef MM_MAX
#include "UCGameplay.h"
#include "UnIpDrv.h"
#endif

#ifdef _XBOX
// ----- Need access to the XboxLiveManager
#include "XboxLive.h"
#include "GUI.h"
#endif

IMPLEMENT_CLASS(UXboxLiveInterface);
// ==============================================================================
// UXboxLiveInterface
// ==============================================================================
void UXboxLiveInterface::execUpdateServerListInfo( FFrame & Stack, RESULT_DECL )
{
	guard(UXboxLiveInterface::execUpdateServerListInfo);
	
	P_GET_UBOOL(bStartNewQuery);
	P_FINISH;

#ifdef _XBOX
	// Okay, the server list lives at allBeacons
	XboxLiveManager* live = XboxLiveManager::getXboxLiveManager();
	if ( live && bStartNewQuery )
	{
		if (bUseOptimatch)
			live->StartOptimatchMatchQuery(GameTypeNdx, MinPlayers, MaxPlayers, Dedicated, Purity, DesiredSkill);
		else
			live->StartQuickMatchQuery(GameTypeNdx, 1, DesiredSkill, VC_Pure);
	}

	MatchQueryResults* results = (live != NULL) ? live->getMatchQueryResults() : NULL;
	allBeacons.Empty();
	if  (results != NULL)
	{
		// make this easier to read
		MatchQueryResults& QueryResults = *results;
		for (UINT i = 0; i < results->size(); ++i)
		{
			FMatchSessionResult& Match = (*results)[i];
			if ( Match.bValid )
			{
				INT idx = allBeacons.AddZeroed();
				Match.Parse(allBeacons(idx));
			}
		}
	}
#endif // _XBOX
	unguard;
} // execUpdateServerListInfo

void UXboxLiveInterface::execClearQueryResults( FFrame & Stack, RESULT_DECL )
{
	guard(UXboxLiveInterface::execClearQueryResults);
	P_FINISH;
#ifdef _XBOX
	// Okay, the server list lives at allBeacons
	XboxLiveManager * live = XboxLiveManager::getXboxLiveManager();
	live->ClearQueryResults();
#endif
	unguard;
}

void UXboxLiveInterface::execGetNdx( FFrame & Stack, RESULT_DECL )
{
	guard(UXboxLiveInterface::execGetNdx);
	P_GET_STR(Addr);
	P_FINISH;

	DWORD rv = -1;
#ifdef _XBOX
	// Okay, the server list lives at serverList->List->Elements
	XboxLiveManager * live = XboxLiveManager::getXboxLiveManager();
	MatchQueryResults * results = (live != NULL) ? live->getMatchQueryResults() : NULL;
	XNKID xsid;
	StringToBytes((BYTE *)&xsid, sizeof(XNKID), Addr);
	if  (results != NULL)
	{
		for (UINT i = 0; i < results->size(); ++i) 
		{
			if (appMemcmp(&(*results)[i].SessionID, &xsid, sizeof(XNKID)) == 0) 
			{
				rv = i;
				break;
			}
		}
	}
#endif // _XBOX

	*(DWORD *)Result = rv;
	unguard;
} // execGetNdx

void UXboxLiveInterface::execGetSessionID( FFrame & Stack, RESULT_DECL )
{
	guard(UXboxLiveInterface::execGetSessionID);

	P_GET_INT(ndx);
	P_FINISH;

	FString rv;
#ifdef _XBOX
	// Okay, the server list lives at serverList->List->Elements
	XboxLiveManager * live = XboxLiveManager::getXboxLiveManager();
	MatchQueryResults * results = (live != NULL) ? live->getMatchQueryResults() : NULL;
	if  ((results != NULL) && (0 <= ndx) && ((UINT)ndx < results->size()))
	{
		BytesToString((BYTE *)&(*results)[ndx].SessionID, sizeof(XNKID), rv);
	}

#endif // _XBOX

	*(FString *)Result = rv;
	unguard;
} // execGetSessionID

void UXboxLiveInterface::execGetExchangeKey( FFrame & Stack, RESULT_DECL )
{
	guard(UXboxLiveInterface::execGetExchangeKey);

	P_GET_INT(ndx);
	P_FINISH;

	FString rv;
#ifdef _XBOX
	// Okay, the server list lives at serverList->List->Elements
	XboxLiveManager * live = XboxLiveManager::getXboxLiveManager();
	MatchQueryResults * results = (live != NULL) ? live->getMatchQueryResults() : NULL;
	if  ((results != NULL) && (0 <= ndx) && ((UINT)ndx < results->size())) 
	{
		BytesToString((BYTE *)&(*results)[ndx].KeyExchangeKey, sizeof(XNKEY), rv);
	}

#endif // _XBOX
	*(FString *)Result = rv;
	unguard;
} // execGetExchangeKey

void UXboxLiveInterface::execGetHostAddr( FFrame & Stack, RESULT_DECL )
{
	guard(UXboxLiveInterface::execGetHostAddr);

	P_GET_INT(ndx);
	P_FINISH;

	FString rv;
#ifdef _XBOX
	// Okay, the server list lives at serverList->List->Elements
	XboxLiveManager * live = XboxLiveManager::getXboxLiveManager();
	MatchQueryResults * results = (live != NULL) ? live->getMatchQueryResults() : NULL;
	if  ((results != NULL) && (0 <= ndx) && ((UINT)ndx < results->size())) 
	{
		BytesToString((BYTE *)&(*results)[ndx].HostAddress, sizeof(XNADDR), rv);
	}

#endif // _XBOX
	*(FString *)Result = rv;
	unguard;
} // execGetHostAddr

void UXboxLiveInterface::execStartExtraInfoQuery(FFrame& Stack, RESULT_DECL)
{
	guard(UXboxLiveInterface::execStartExtraInfoQuery);

	P_GET_STR(SessionIDString);
	P_FINISH;

#ifdef _XBOX
	XNKID SessionID;
	StringToBytes((BYTE *) &SessionID, sizeof(XNKID), SessionIDString);

	XboxLiveManager* Manager = XboxLiveManager::getXboxLiveManager();
	Manager->StartExtraInfoQuery(SessionID, this, delegateOnExtraInfoCompletion);
#endif

	unguard;
}

// StatsManager

void UStatsManager::execGetInstance(FFrame& Stack, RESULT_DECL)
{
	guard(UStatsManager::execGetInstance);

	P_FINISH;

	*(UStatsManager**) Result = GetInstance();

	unguard;
}

void UStatsManager::execBeginLadderQuery(FFrame& Stack, RESULT_DECL)
{
	guard(UStatsManager::execBeginLadderQuery);

	P_GET_TARRAY_REF(RungNames, FName);
	P_GET_INT(LadderIndex);
	P_FINISH;

	BeginLadderQuery(*RungNames, LadderIndex);

	unguard;
}

void UStatsManager::execBeginRungQuery(FFrame& Stack, RESULT_DECL)
{
	guard(UStatsManager::execBeginRungQuery);

	P_GET_NAME(RungName);
	P_GET_INT(LadderIndex);
	P_FINISH;

	BeginRungQuery(RungName, LadderIndex);

	unguard;
}

UStatsManager* UStatsManager::GetInstance()
{
	guard(UStatsManager::GetInstance);

	static UStatsManager* Instance = NULL;

	// If the singleton instance has not been previously created, construct it.
	if (Instance == NULL)
	{
		Instance = ConstructObject<UStatsManager>(UStatsManager::StaticClass(),
			                                      UObject::GetTransientPackage(), NAME_None,
								 	 	    	  RF_Public | RF_Standalone | RF_Native);
		Instance->AddToRoot();
	}

	return Instance;

	unguard;
}

void UStatsManager::BeginLadderQuery(const TArray<FName>& RungNames, INT LadderIndex)
{
	guard(UStatsManager::BeginLadderQuery);

#ifdef _XBOX
	// Fill in the ladder data.
	TArray<FRungDataSummary> LadderData;
	LadderData.AddZeroed(RungNames.Num());
	for (INT RungNameIndex = 0; RungNameIndex < RungNames.Num(); RungNameIndex++)
	{
		LadderData(RungNameIndex).RungName = RungNames(RungNameIndex);
		LadderData(RungNameIndex).LadderIndex = LadderIndex;

		FString RungNameString(*RungNames(RungNameIndex));
		INT UnderscoreIndex = RungNameString.InStr(TEXT("_"), TRUE);
		if (UnderscoreIndex != -1)
		{
			LadderData(RungNameIndex).RungIndex = appAtoi(*RungNameString.Mid(UnderscoreIndex + 1));
		}
		else
		{
			LadderData(RungNameIndex).RungIndex = -1;
		}
	}

	XboxLiveManager* Manager = XboxLiveManager::getXboxLiveManager();
	if (Manager != NULL)
	{
		Manager->StartStatsLadderRead(LadderIndex, LadderData);
	}
#endif

	unguard;
}

void UStatsManager::BeginRungQuery(const FName& RungName, INT LadderIndex)
{
	guard(UStatsManager::BeginRungQuery);

#ifdef _XBOX
	INT RungIndex = UPlayerStats::GetRungIndexByName(RungName);
	if (RungIndex != -1)
	{
		FRungDataDetailed RungData;
		RungData.RungName = RungName;

		FString RungNameString(*RungName);
		INT UnderscoreIndex = RungNameString.InStr(TEXT("_"), TRUE);
		if (UnderscoreIndex != -1)
		{
			RungData.RungIndex = appAtoi(*RungNameString.Mid(UnderscoreIndex + 1));
		}
		else
		{
			RungData.RungIndex = -1;
		}

		RungData.LadderIndex = LadderIndex;

		XboxLiveManager* Manager = XboxLiveManager::getXboxLiveManager();
		if (Manager != NULL)
		{
			Manager->StartStatsRungQuery(RungIndex, RungData);
		}
	}
#endif

	unguard;
}