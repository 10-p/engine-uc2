/*=============================================================================
	UnPlayerStats.h: Xbox Live player statistics functionality.
	Copyright 1997-2004 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Per Vognsen.
=============================================================================*/

#if SUPPORTS_PRAGMA_PACK
#pragma pack (push,4)
#endif

#ifndef ENGINE_API
#define ENGINE_API DLL_IMPORT
#endif

#ifndef NAMES_ONLY
#define AUTOGENERATE_NAME(name) extern ENGINE_API FName ENGINE_##name;
#endif

AUTOGENERATE_NAME(OnQueryCompleted)
AUTOGENERATE_NAME(CalculateRating)
AUTOGENERATE_NAME(ReceivedPlayerSkill)
AUTOGENERATE_NAME(UpdatePlayerSkill)

#ifndef NAMES_ONLY
#undef AUTOGENERATE_NAME

#ifndef __UNPLAYERSTATS_H__
#define __UNPLAYERSTATS_H__

#ifdef _XBOX
#include "xtl.h"
#include "xonline.h"
#endif // _XBOX
/*
enum EComboFloatStat
{
	COMBO_STAT_FLOAT_MAX    =0,
};
*/
enum EComboIntStat
{
	COMBO_STAT_INT_USES     =0,
	COMBO_STAT_INT_MAX      =1,
};

enum EMapFloatStat
{
	MAP_STAT_FLOAT_FASTEST_WIN=0,
	MAP_STAT_FLOAT_FASTEST_LOSS=1,
	MAP_STAT_FLOAT_AIR_TIME =2,
	MAP_STAT_FLOAT_MAX      =3,
};

enum EMapIntStat
{
	MAP_STAT_INT_WINS       =0,
	MAP_STAT_INT_LOSSES     =1,
	MAP_STAT_INT_PICKUPS    =2,
	MAP_STAT_INT_GAME_DEFENSIVE_OBJECTIVES=3,
	MAP_STAT_INT_GAME_OFFENSIVE_OBJECTIVES=4,
	MAP_STAT_INT_GAME_SCORE =5,
	MAP_STAT_INT_CONSECUTIVE_DODGES=6,
	MAP_STAT_INT_MAX        =7,
};

enum EWeaponFloatStat
{
	WEAPON_STAT_FLOAT_TIME  =0,
	WEAPON_STAT_FLOAT_MAX   =1,
};

enum EWeaponIntStat
{
	WEAPON_STAT_INT_USES    =0,
	WEAPON_STAT_INT_KILLS   =1,
	WEAPON_STAT_INT_SHOTS   =2,
	WEAPON_STAT_INT_FINISHES=3,
	WEAPON_STAT_INT_DEATHS  =4,
	WEAPON_STAT_INT_SUICIDES=5,
	WEAPON_STAT_INT_SPECIALS=6,
	WEAPON_STAT_INT_COMBOS  =7,
	WEAPON_STAT_INT_COMBO_KILLS=8,
	WEAPON_STAT_INT_SHOTS_AT=9,
	WEAPON_STAT_INT_REFLECTIONS=10,
	WEAPON_STAT_INT_MAX     =11,
};

enum ECharFloatStat
{
	CHAR_STAT_FLOAT_TIME    =0,
	CHAR_STAT_FLOAT_MAX     =1,
};

enum ECharIntStat
{
	CHAR_STAT_INT_USES      =0,
	CHAR_STAT_INT_KILLS     =1,
	CHAR_STAT_INT_DEATHS    =2,
	CHAR_STAT_INT_SUICIDES	=3,
	CHAR_STAT_INT_MAX       =4,
};

enum EIntStat
{
	INT_STAT_WINS,
	INT_STAT_LOSSES,
	INT_STAT_DISCONNECTS,
	INT_STAT_HIGHEST_MULTIKILL,
	INT_STAT_HIGHEST_SPREE,
	INT_STAT_NUM_MULTIKILL_1,
    INT_STAT_NUM_MULTIKILL_2,
    INT_STAT_NUM_MULTIKILL_3,
    INT_STAT_NUM_MULTIKILL_4,
    INT_STAT_NUM_MULTIKILL_5,
    INT_STAT_NUM_MULTIKILL_6,
    INT_STAT_NUM_MULTIKILL_7,
    INT_STAT_NUM_MULTIKILL_8,
    INT_STAT_NUM_SPREE_1,
    INT_STAT_NUM_SPREE_2,
    INT_STAT_NUM_SPREE_3,
    INT_STAT_NUM_SPREE_4,
    INT_STAT_NUM_SPREE_5,
    INT_STAT_NUM_SPREE_6,
	INT_STAT_KILLS,
    INT_STAT_QUALITY_KILLS,
    INT_STAT_DEATHS,
	INT_STAT_SUICIDES,

	INT_STAT_MATCHES_PLAYED_NONTEAM,
	INT_STAT_MATCHES_PLAYED_TEAM,
	INT_STAT_MATCHES_WON_NONTEAM,
	INT_STAT_MATCHES_WON_TEAM,
	INT_STAT_MAX,
};

enum ERungIntStat
{
	RUNG_STAT_INT_HIGHEST_WIN_DIFFICULTY=0,
	RUNG_STAT_INT_HIGHEST_SCORE			=1,
	RUNG_STAT_INT_LATEST_SCORE			=2,
	RUNG_STAT_INT_MAX					=3,
};

enum ERungFloatStat
{
	RUNG_STAT_FLOAT_FASTEST_WIN=0,
	RUNG_STAT_FLOAT_LATEST_TIME=1,
	RUNG_STAT_FLOAT_MAX	=2,
};

enum EStatType
{
	ST_INVALID,
	ST_GENERAL,
	ST_RUNG,
	ST_MAP,
	ST_CHAR,
	ST_COMBO,
	ST_WEAPON,
	ST_MAX,
};

enum EStatValueType
{
	SVT_INT,
	SVT_FLOAT
};

enum EUpdateMethod
{
	UPDATE_ADD      = 0,
	UPDATE_MIN      = 1,
	UPDATE_MAX      = 2,
	UPDATE_REPLACE  = 3,
};

class ENGINE_API UPlayerStats : public UObject
{
	friend class UPlayerProfile;
	friend class APlayerController;

	FLOAT UpdateValue(EUpdateMethod UpdateMethod, FLOAT SrcValue, FLOAT DestValue);
	INT UpdateValue(EUpdateMethod UpdateMethod, INT SrcValue, INT DestValue);

	void UpdateRungs(const UPlayerStats& SrcStats);

	void UpdateStats(const UPlayerStats& SrcStats);
	void UpdateGeneralStats(const UPlayerStats& SrcStats);
	void UpdateMaps(const UPlayerStats& SrcStats);
	void UpdateCharacters(const UPlayerStats& SrcStats);
	void UpdateWeapons(const UPlayerStats& SrcStats);
	void UpdateCombos(const UPlayerStats& SrcStats);

public:
	DECLARE_FUNCTION(execPrintTable);

	DECLARE_FUNCTION(execGetFloatStatByCombo);
	DECLARE_FUNCTION(execSetFloatStatByCombo);
	DECLARE_FUNCTION(execGetIntStatByCombo);
	DECLARE_FUNCTION(execSetIntStatByCombo);

	DECLARE_FUNCTION(execGetFloatStatByMap);
	DECLARE_FUNCTION(execSetFloatStatByMap);
	DECLARE_FUNCTION(execGetIntStatByMap);
	DECLARE_FUNCTION(execSetIntStatByMap);

	DECLARE_FUNCTION(execGetFloatStatByWeapon);
	DECLARE_FUNCTION(execSetFloatStatByWeapon);
	DECLARE_FUNCTION(execGetIntStatByWeapon);
	DECLARE_FUNCTION(execSetIntStatByWeapon);

	DECLARE_FUNCTION(execGetFloatStatByCharacter);
	DECLARE_FUNCTION(execSetFloatStatByCharacter);
	DECLARE_FUNCTION(execGetIntStatByCharacter);
	DECLARE_FUNCTION(execSetIntStatByCharacter);

	DECLARE_FUNCTION(execGetFloatStatByRung);
	DECLARE_FUNCTION(execSetFloatStatByRung);
	DECLARE_FUNCTION(execGetIntStatByRung);
	DECLARE_FUNCTION(execSetIntStatByRung);

	DECLARE_FUNCTION(execGetIntStat);
	DECLARE_FUNCTION(execSetIntStat);
	DECLARE_FUNCTION(execSetIntStatIfHigher);
	DECLARE_FUNCTION(execInitialize);
	DECLARE_FUNCTION(execResetValues);

	DECLARE_CLASS(UPlayerStats,UObject,0|CLASS_Config,Engine)

	// ...

	void Initialize();
	void ResetValues();

	UBOOL CanRecord() { return bRecording; }

	// ...

	void InitializeIndexTable(TMap<FName, INT>** IndexTable, const TArrayNoInit<FName>& Names, EStatType Type);

	// ...

#ifdef _XBOX
	void GetRungFromLive(FName RungName);
	void EnumerateRungFromLive(FName RungName, INT NumUsers);
#endif // _XBOX

	// ...

	FString PrintTable(INT TableIdx);

	// ...

	static INT GetIndexByName(TMap<FName, INT>* IndexTable, const FName& Name);
	FName GetNameByIndex( EStatType TableType, INT TableIndex );

	static INT GetCharIndexByName(const FName& CharName);
	static INT GetMapIndexByName(const FName& MapName);
	static INT GetWeaponIndexByName(const FName& WeaponName);
	static INT GetComboIndexByName(const FName& ComboName);
	static INT GetRungIndexByName(const FName& RungName);

	// ...

	template<EStatType StatType>
	static INT GetCacheIndex(INT EntryIdx, INT StatIdx, EStatValueType ValueType);

	template<EStatType StatType>
	INT GetIntFromCache(INT EntryIdx, INT StatIdx) const;

	template<EStatType StatType>
	UBOOL PutIntInCache(INT EntryIdx, INT StatIdx, INT IntValue, UBOOL bAdditive);

	template<EStatType StatType>
	FLOAT GetFloatFromCache(INT EntryIdx, INT StatIdx) const;

	template<EStatType StatType>
	UBOOL PutFloatInCache(INT EntryIdx, INT StatIdx, FLOAT FloatValue, UBOOL bAdditive);

	// set an int value in the summary tables
	void SetIntStat(BYTE StatIdx, INT IntValue, UBOOL bAdditive);
	INT GetIntStat(BYTE StatIdx) const;
	EStatType GetStatType( INT CacheIndex ) const;

	template<EStatType Type>
	UBOOL ReverseMap( INT CacheIndex, INT& StatTableIndex, INT& StatTableSlot, FName& DataName, INT& NameIndex, INT& IntStatIndex, INT& FloatStatIndex );

	void DebugDump();

	INT GetIntStatByCombo(const FName& ComboName, INT StatIndex) const;
	void SetIntStatByCombo(const FName& ComboName, INT StatIndex, FLOAT IntValue, UBOOL bAdditive);

	FLOAT GetFloatStatByMap(const FName& MapName, INT StatIndex) const;
	void SetFloatStatByMap(const FName& MapName, INT StatIndex, FLOAT FloatValue, UBOOL bAdditive);
	INT GetIntStatByMap(const FName& MapName, INT StatIndex) const;
	void SetIntStatByMap(const FName& MapName, INT StatIndex, FLOAT IntValue, UBOOL bAdditive);

	FLOAT GetFloatStatByWeapon(const FName& WeaponName, INT StatIndex) const;
	void SetFloatStatByWeapon(const FName& WeaponName, INT StatIndex, FLOAT FloatValue, UBOOL bAdditive);
	INT GetIntStatByWeapon(const FName& WeaponName, INT StatIndex) const;
	void SetIntStatByWeapon(const FName& WeaponName, INT StatIndex, FLOAT IntValue, UBOOL bAdditive);	

	FLOAT GetFloatStatByCharacter(const FName& CharName, INT StatIndex) const;
	void SetFloatStatByCharacter(const FName& CharName, INT StatIndex, FLOAT FloatValue, UBOOL bAdditive);
	INT GetIntStatByCharacter(const FName& CharName, INT StatIndex) const;
	void SetIntStatByCharacter(const FName& CharName, INT StatIndex, FLOAT IntValue, UBOOL bAdditive);

	FLOAT GetFloatStatByRung(const FName& RungName, INT StatIndex) const;
	void SetFloatStatByRung(const FName& RungName, INT StatIndex, FLOAT FloatValue, UBOOL bAdditive);
	INT GetIntStatByRung(const FName& RungName, INT StatIndex) const;
	void SetIntStatByRung(const FName& RungName, INT StatIndex, FLOAT IntValue, UBOOL bAdditive);

	static TMap<FName, INT>* CharIndexTable;
	static TMap<FName, INT>* MapIndexTable;
	static TMap<FName, INT>* WeaponIndexTable;
	static TMap<FName, INT>* ComboIndexTable;
	static TMap<FName, INT>* RungIndexTable;

	static TArray<FName>*	ReverseLoopupTable[ST_MAX];

	static FName AnubisName;
	static FName DevastationName;

	BITFIELD			bMultiPlayer:1;
	BITFIELD			bRecording:1;
	// ...

	TArrayNoInit<FName> MapNames;
	TArrayNoInit<FName> CharNames;
	TArrayNoInit<FName> WeaponNames;
	TArrayNoInit<FName> ComboNames;
	TArrayNoInit<FName> RungNames;

	// ...

	union StatValue
	{
		INT IntValue;
		FLOAT FloatValue;
	};

	TArray<StatValue> StatsCache;

	FLiveUser User;

	// ...

	static const INT NumTables;
	static const INT NumSummaryTables = 1;

	static const INT NumFieldsPerTable	= 64;
};

// TFloorDiv<Num,Denum>::Value is the floor of the quotient Num / Denum.
template<int Num, int Denum>
struct TFloorDiv
{
	static const int Value = Num / Denum;
};

// TCeilDiv<Num,Denum>::Value is the ceiling of the quotient Num / Denum.
template<int Num, int Denum>
struct TCeilDiv
{
	static const int Value = (Num % Denum == 0) ? (Num / Denum) : int((float(Num) / float(Denum)) + 1.0f);
};

template<EStatType StatType>
struct TStatTypeTraits
{
};

// Combo stats are special as they are stored in the first table, along with the general stats.
template<>
struct TStatTypeTraits<ST_COMBO>
{
	// this is the index of the leaderboard where the combo stats begin
	static const INT FirstTableWithEntries = 0;

	// this is the total number of combos we can track
	static const INT NumEntries = 37;

	// this is the number of int stats we track, per combo
	static const INT NumIntStatsPerEntry	= COMBO_STAT_INT_MAX;

	// this is the number of float stats we track, per combo
	static const INT NumFloatStatsPerEntry	= 0/*COMBO_STAT_FLOAT_MAX*/;

	// this is the total number of stats we track per combo
	static const INT NumStatsPerEntry		= NumIntStatsPerEntry + NumFloatStatsPerEntry;

	// a single leaderboard can fit this many combo's worth of stats (e.g. 64 slots / 1 stats per combo = 64 combos per leaderboard)
	// this number changes when the number of stats tracked for each combo is modifeid
	static const INT NumEntriesPerTable	= NumEntries;

	// this is the total number of leaderboards we require in order to track all combo stats
	// (e.g. 50 combos % 64 combos per leaderboard = 50, thus we require (50 / 64)+1 [or 1] leaderboard, where the last leaderboard only has 50 entries)
	// this number changes when number of stats tracked per combo is changed, or the number of combos tracked is changed
	static const INT NumTablesWithEntries = 1;

	static const INT FirstStatIndex = INT_STAT_MAX;

	static const EUpdateMethod IntStatsUpdateMethods[COMBO_STAT_INT_MAX];
//	static const EUpdateMethod FloatStatsUpdateMethods[COMBO_STAT_FLOAT_MAX];
};

template<>
struct TStatTypeTraits<ST_MAP>
{
	// this is the index of the leaderboard where the map stats begin
	static const INT FirstTableWithEntries = UPlayerStats::NumSummaryTables;

	// this is the total number of maps we can track
	static const INT NumEntries = 70;

	// this is the number of int stats we track, per map
	static const INT NumIntStatsPerEntry	= MAP_STAT_INT_MAX;

	// this is the number of float stats we track, per map
	static const INT NumFloatStatsPerEntry	= MAP_STAT_FLOAT_MAX;

	// this is the total number of stats we track per map
	static const INT NumStatsPerEntry		= NumIntStatsPerEntry + NumFloatStatsPerEntry;

	// a single leaderboard can fit this many map's worth of stats (e.g. 64 slots / 10 stats per map = 6 maps per leaderboard [with 4 unused slots])
	static const INT NumEntriesPerTable	= TFloorDiv<UPlayerStats::NumFieldsPerTable, NumStatsPerEntry>::Value;

	// this is the total number of leaderboards we require in order to track all map stats
	// (e.g. 70 maps % 6 maps per leaderboard = 6, thus we require (70 / 6)+1 [or 12] leaderboards, where the last leaderboard only has 4 entries)
	static const INT NumTablesWithEntries = TCeilDiv<NumEntries, NumEntriesPerTable>::Value;

	static const INT FirstStatIndex = UPlayerStats::NumFieldsPerTable * UPlayerStats::NumSummaryTables;
	static const EUpdateMethod IntStatsUpdateMethods[MAP_STAT_INT_MAX];
	static const EUpdateMethod FloatStatsUpdateMethods[MAP_STAT_FLOAT_MAX];
};

template<>
struct TStatTypeTraits<ST_CHAR>
{
	// this is the index of the leaderboard where the character stats begin
	static const INT FirstTableWithEntries = TStatTypeTraits<ST_MAP>::FirstTableWithEntries + TStatTypeTraits<ST_MAP>::NumTablesWithEntries;

	// this is the total number of character we can track
	static const INT NumEntries = 30;

	// this is the number of int stats we track, per character
	static const INT NumIntStatsPerEntry	= CHAR_STAT_INT_MAX;

	// this is the number of float stats we track, per character
	static const INT NumFloatStatsPerEntry	= CHAR_STAT_FLOAT_MAX;

	// this is the total number of stats we track per character
	static const INT NumStatsPerEntry		= NumIntStatsPerEntry + NumFloatStatsPerEntry;

	// a single leaderboard can fit this many character's worth of stats (e.g. 64 slots / 6 stats per character = 10 character per leaderboard [with 4 unused slots])
	static const INT NumEntriesPerTable	= TFloorDiv<UPlayerStats::NumFieldsPerTable, NumStatsPerEntry>::Value;

	// this is the total number of leaderboards we require in order to track all character stats
	// (e.g. 30 characters % 10 character per leaderboard = 0, thus we require 30 / 10 [or 3] leaderboards)
	static const INT NumTablesWithEntries = TCeilDiv<NumEntries, NumEntriesPerTable>::Value;

	static const INT FirstStatIndex = TStatTypeTraits<ST_MAP>::FirstStatIndex + TStatTypeTraits<ST_MAP>::NumTablesWithEntries * UPlayerStats::NumFieldsPerTable;

	static const EUpdateMethod IntStatsUpdateMethods[CHAR_STAT_INT_MAX];
	static const EUpdateMethod FloatStatsUpdateMethods[CHAR_STAT_FLOAT_MAX];
};

template<>
struct TStatTypeTraits<ST_WEAPON>
{
	// this is the index of the leaderboard where the weapon stats begin
	static const INT FirstTableWithEntries = TStatTypeTraits<ST_CHAR>::FirstTableWithEntries + TStatTypeTraits<ST_CHAR>::NumTablesWithEntries;

	// this is the total number of weapon we can track
	static const INT NumEntries = 15;

	// this is the number of int stats we track, per weapon
	static const INT NumIntStatsPerEntry	= WEAPON_STAT_INT_MAX;
	
	// this is the number of float stats we track, per weapon
	static const INT NumFloatStatsPerEntry	= WEAPON_STAT_FLOAT_MAX;

	// this is the total number of stats we track per weapon
	static const INT NumStatsPerEntry		= NumIntStatsPerEntry + NumFloatStatsPerEntry;

	// a single leaderboard can fit this many weapon's worth of stats (e.g. 64 slots / 12 stats per weapon = 5 weapons per leaderboard [with 4 unused slots])
	static const INT NumEntriesPerTable	= TFloorDiv<UPlayerStats::NumFieldsPerTable, NumStatsPerEntry>::Value;

	// this is the total number of leaderboards we require in order to track all weapon stats
	// (e.g. 15 characters % 5 character per leaderboard = 0, thus we require 15 / 5 [or 3] leaderboards)
	static const INT NumTablesWithEntries = TCeilDiv<NumEntries, NumEntriesPerTable>::Value;

	static const INT FirstStatIndex = TStatTypeTraits<ST_CHAR>::FirstStatIndex + TStatTypeTraits<ST_CHAR>::NumTablesWithEntries * UPlayerStats::NumFieldsPerTable;

	static const EUpdateMethod IntStatsUpdateMethods[WEAPON_STAT_INT_MAX];
	static const EUpdateMethod FloatStatsUpdateMethods[WEAPON_STAT_FLOAT_MAX];
};

template<>
struct TStatTypeTraits<ST_RUNG>
{
	// this is the index of the leaderboard where the rung stats begin
	static const INT FirstTableWithEntries = TStatTypeTraits<ST_WEAPON>::FirstTableWithEntries + TStatTypeTraits<ST_WEAPON>::NumTablesWithEntries;

	// this is the total number of rungs we can track
	static const INT NumEntries = 200;

	// this is the number of int stats we track, per rung
	static const INT NumIntStatsPerEntry	= RUNG_STAT_INT_MAX;

	// this is the number of float stats we track, per rung
	static const INT NumFloatStatsPerEntry	= RUNG_STAT_FLOAT_MAX;

	// this is the total number of stats we track per rung
	static const INT NumStatsPerEntry		= NumIntStatsPerEntry + NumFloatStatsPerEntry;

	// a single leaderboard has this many rung's worth of stats
	static const INT NumEntriesPerTable = 1;

	// this is the total number of leaderboards we require in order to track all rung stats
	static const INT NumTablesWithEntries = NumEntries;

	static const INT FirstStatIndex = TStatTypeTraits<ST_WEAPON>::FirstStatIndex + TStatTypeTraits<ST_WEAPON>::NumTablesWithEntries * UPlayerStats::NumFieldsPerTable;

	static const EUpdateMethod IntStatsUpdateMethods[RUNG_STAT_INT_MAX];
	static const EUpdateMethod FloatStatsUpdateMethods[RUNG_STAT_FLOAT_MAX];
};

struct ENGINE_API UStatsQuery_eventOnQueryCompleted_Parms
{
	class UStatsQuery* Query;
	UBOOL bIsError;
};

struct ENGINE_API FPlayerQuerySpec
{
	INT						StatSpecCount;
	class UPlayerStatCache*	DestPlayerStat;
};

class ENGINE_API UStatsQuery : public UObject
{
	DECLARE_CLASS(UStatsQuery, UObject, 0|CLASS_Config, Engine)

	NO_DEFAULT_CONSTRUCTOR(UStatsQuery)

public:
	DECLARE_FUNCTION(execSetUser);
	DECLARE_FUNCTION(execAddGeneralStats);
	DECLARE_FUNCTION(execAddMap);
	DECLARE_FUNCTION(execAddWeapon);
	DECLARE_FUNCTION(execAddCharacter);
	DECLARE_FUNCTION(execAddCombo);
	DECLARE_FUNCTION(execSubmit);
	DECLARE_FUNCTION(execHasPendingData);
	
	void delegateOnQueryCompleted(UStatsQuery* Query,UBOOL bIsError)
	{
		UStatsQuery_eventOnQueryCompleted_Parms Parms;
		Parms.Query=Query;
		Parms.bIsError=bIsError;
		ProcessDelegate(ENGINE_OnQueryCompleted,&__OnQueryCompleted__Delegate,&Parms);
	}

	void SetUser(BYTE NewUserXUID[12], UPlayerStats* PlayerStats);

	void AddGeneralStats();
	void AddMap(FName MapName);
	void AddCharacter(FName CharacterName);
	void AddWeapon(FName WeaponName);
	void AddCombo(FName ComboName);

	void Submit();
	UBOOL HasPendingData();
	void QueryCompleted( UBOOL bIsError );

	void Destroy();

private:
	enum { MaxLeaderboardsPerQuery = 5 };

	template<EStatType StatType>
	void AddEntry(INT EntryIndex);

#ifdef _XBOX
	TArray<XONLINE_STAT_SPEC> PendingStatSpecs;
#else
	TArray<INT> PendingStatSpecs;
#endif

public:
	TArray<FPlayerQuerySpec> QuerySpecs;
	UBOOL bQueryInProgress;
	FScriptDelegate __OnQueryCompleted__Delegate;
};

class ENGINE_API UStatsWrite : public UObject
{
	DECLARE_CLASS(UStatsWrite, UObject, 0|CLASS_Config, Engine)

	NO_DEFAULT_CONSTRUCTOR(UStatsWrite)

public:
	DECLARE_FUNCTION(execAddUser);
	DECLARE_FUNCTION(execSubmit);

#ifdef _XBOX
	void Write(UPlayerStats* PlayerStats, UBOOL bSinglePlayer);
	void Submit();

	void Destroy();

private:
	void WriteGeneralStats(XUID PlayerXUID, UPlayerStats* PlayerStats);
	void WriteRungs(XUID PlayerXUID, UPlayerStats* PlayerStats);
	void WriteMaps(XUID PlayerXUID, UPlayerStats* PlayerStats);
	void WriteCharacters(XUID PlayerXUID, UPlayerStats* PlayerStats);
	void WriteCombos(XUID PlayerXUID, UPlayerStats* PlayerStats);
	void WriteWeapons(XUID PlayerXUID, UPlayerStats* PlayerStats);

	TArray<XONLINE_STAT_PROC> StatProcs;
#else
	TArray<INT>	StatProcs;
#endif
};

#endif	// NAMES_ONLY

#endif	// __UNPLAYERSTATS_H__

#if SUPPORTS_PRAGMA_PACK
#pragma pack (pop)
#endif


