/*=============================================================================
	UnPlayerStats.cpp: Xbox Live player statistics functionality.
	Copyright 1997-2004 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Per Vognsen.
=============================================================================*/

#include "EnginePrivate.h"
#include "UnPlayerStats.h"

#define NAMES_ONLY
#define AUTOGENERATE_NAME(name) ENGINE_API FName ENGINE_##name;
#include "UnPlayerStats.h"
#undef AUTOGENERATE_NAME
#undef NAMES_ONLY

#ifdef _XBOX
#include "UnUIXInteraction.h"
#include "XboxLive.h"
#endif

IMPLEMENT_CLASS(UPlayerStats);
IMPLEMENT_CLASS(UPlayerStatCache);
IMPLEMENT_CLASS(UStatsQuery);
IMPLEMENT_CLASS(UStatsWrite);

IMPLEMENT_FUNCTION(UPlayerStats, -1, execPrintTable);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execGetIntStatByCombo);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execSetIntStatByCombo);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execGetFloatStatByMap);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execSetFloatStatByMap);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execGetIntStatByMap);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execSetIntStatByMap);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execGetFloatStatByWeapon);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execSetFloatStatByWeapon);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execGetIntStatByWeapon);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execSetIntStatByWeapon);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execGetFloatStatByCharacter);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execSetFloatStatByCharacter);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execGetIntStatByCharacter);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execSetIntStatByCharacter);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execGetFloatStatByRung);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execSetFloatStatByRung);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execGetIntStatByRung);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execSetIntStatByRung);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execSetIntStat);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execSetIntStatIfHigher);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execGetIntStat);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execInitialize);
IMPLEMENT_FUNCTION(UPlayerStats, -1, execResetValues);

// Initialize static member variables.

TMap<FName, INT>* UPlayerStats::CharIndexTable		= NULL;
TMap<FName, INT>* UPlayerStats::MapIndexTable		= NULL;
TMap<FName, INT>* UPlayerStats::WeaponIndexTable	= NULL;
TMap<FName, INT>* UPlayerStats::ComboIndexTable		= NULL;
TMap<FName, INT>* UPlayerStats::RungIndexTable		= NULL;

TArray<FName>* UPlayerStats::ReverseLoopupTable[ST_MAX] =
{
	NULL, NULL, NULL, NULL, NULL, NULL, NULL,
};

// If you change the value of UPlayerStats::NumTables, you must
// increment the value of SAVE_VERSION_NUMBER in ULoadSaveManager.h
const INT UPlayerStats::NumTables = UPlayerStats::NumSummaryTables +
                                    TStatTypeTraits<ST_MAP>::NumTablesWithEntries +
                                    TStatTypeTraits<ST_CHAR>::NumTablesWithEntries +
                                    TStatTypeTraits<ST_WEAPON>::NumTablesWithEntries +
									TStatTypeTraits<ST_RUNG>::NumTablesWithEntries;

// Update methods for the general statistics.
static const EUpdateMethod IntStatsUpdateMethods[INT_STAT_MAX] =
{
	UPDATE_ADD, // INT_STAT_WINS
	UPDATE_ADD, // INT_STAT_LOSSES
	UPDATE_ADD, // INT_STAT_DISCONNECTS
	UPDATE_MAX, // INT_STAT_HIGHEST_MULTIKILL
	UPDATE_MAX, // INT_STAT_HIGHEST_SPREE
	UPDATE_ADD, // INT_STAT_NUM_MULTIKILL_1
	UPDATE_ADD, // INT_STAT_NUM_MULTIKILL_2
	UPDATE_ADD, // INT_STAT_NUM_MULTIKILL_3
	UPDATE_ADD, // INT_STAT_NUM_MULTIKILL_4
	UPDATE_ADD, // INT_STAT_NUM_MULTIKILL_5
	UPDATE_ADD, // INT_STAT_NUM_MULTIKILL_6
	UPDATE_ADD, // INT_STAT_NUM_MULTIKILL_7
	UPDATE_ADD, // INT_STAT_NUM_MULTIKILL_8
	UPDATE_ADD, // INT_STAT_NUM_SPREE_1
	UPDATE_ADD, // INT_STAT_NUM_SPREE_2
	UPDATE_ADD, // INT_STAT_NUM_SPREE_3
	UPDATE_ADD, // INT_STAT_NUM_SPREE_4
	UPDATE_ADD, // INT_STAT_NUM_SPREE_5
	UPDATE_ADD, // INT_STAT_NUM_SPREE_6
	UPDATE_ADD, // INT_STAT_KILLS
	UPDATE_ADD, // INT_STAT_QUALITY_KILLS
	UPDATE_ADD, // INT_STAT_DEATHS
	UPDATE_ADD, // INT_STAT_SUICIDES
	UPDATE_ADD, // INT_STAT_MATCHES_PLAYED_NONTEAM
	UPDATE_ADD, // INT_STAT_MATCHES_PLAYED_TEAM
	UPDATE_ADD, // INT_STAT_MATCHES_WON_NONTEAM
	UPDATE_ADD, // INT_STAT_MATCHES_WON_TEAM
};

const EUpdateMethod TStatTypeTraits<ST_MAP>::IntStatsUpdateMethods[MAP_STAT_INT_MAX] =
{
	UPDATE_ADD,	// MAP_STAT_INT_WINS
	UPDATE_ADD, // MAP_STAT_INT_LOSSES
	UPDATE_ADD, // MAP_STAT_INT_PICKUPS
	UPDATE_ADD, // MAP_STAT_INT_GAME_DEFENSIVE_OBJECTIVES
	UPDATE_ADD, // MAP_STAT_INT_GAME_OFFENSIVE_OBJECTIVES
	UPDATE_ADD, // MAP_STAT_INT_GAME_SCORE
	UPDATE_MAX, // MAP_STAT_INT_CONSECUTIVE_DODGES
};

const EUpdateMethod TStatTypeTraits<ST_MAP>::FloatStatsUpdateMethods[MAP_STAT_FLOAT_MAX] =
{
	UPDATE_MIN, // MAP_STAT_FLOAT_FASTEST_WIN
	UPDATE_MIN, // MAP_STAT_FLOAT_FASTEST_LOSS
	UPDATE_MAX, // MAP_STAT_FLOAT_AIR_TIME
};

const EUpdateMethod TStatTypeTraits<ST_CHAR>::IntStatsUpdateMethods[CHAR_STAT_INT_MAX] =
{
	UPDATE_ADD, // CHAR_STAT_INT_USES
	UPDATE_ADD, // CHAR_STAT_INT_KILLS
	UPDATE_ADD, // CHAR_STAT_INT_DEATHS
	UPDATE_ADD, // CHAR_STAT_INT_SUICIDES
};

const EUpdateMethod TStatTypeTraits<ST_CHAR>::FloatStatsUpdateMethods[CHAR_STAT_FLOAT_MAX] =
{
	UPDATE_ADD, // CHAR_STAT_FLOAT_TIME
};

const EUpdateMethod TStatTypeTraits<ST_WEAPON>::IntStatsUpdateMethods[WEAPON_STAT_INT_MAX] =
{
	UPDATE_ADD, // WEAPON_STAT_INT_USES
	UPDATE_ADD, // WEAPON_STAT_INT_KILLS
	UPDATE_ADD, // WEAPON_STAT_INT_SHOTS
	UPDATE_ADD, // WEAPON_STAT_INT_FINISHES
	UPDATE_ADD, // WEAPON_STAT_INT_DEATHS
	UPDATE_ADD, // WEAPON_STAT_INT_SUICIDES
	UPDATE_ADD, // WEAPON_STAT_INT_SPECIALS
	UPDATE_ADD, // WEAPON_STAT_INT_COMBOS
	UPDATE_ADD, // WEAPON_STAT_INT_COMBO_KILLS
	UPDATE_ADD, // WEAPON_STAT_INT_SHOTS_AT
	UPDATE_ADD, // WEAPON_STAT_INT_REFLECTIONS
};

const EUpdateMethod TStatTypeTraits<ST_WEAPON>::FloatStatsUpdateMethods[WEAPON_STAT_FLOAT_MAX] =
{
	UPDATE_ADD, // WEAPON_STAT_FLOAT_TIME
};

const EUpdateMethod TStatTypeTraits<ST_COMBO>::IntStatsUpdateMethods[COMBO_STAT_INT_MAX] =
{
	UPDATE_ADD, // COMBO_STAT_INT_USES
};

const EUpdateMethod TStatTypeTraits<ST_RUNG>::IntStatsUpdateMethods[RUNG_STAT_INT_MAX] =
{
	UPDATE_MAX,     // RUNG_STAT_INT_HIGHEST_WIN_DIFFICULTY
	UPDATE_MAX,     // RUNG_STAT_INT_HIGHEST_SCORE
	UPDATE_REPLACE, // RUNG_STAT_INT_LATEST_SCORE
};

const EUpdateMethod TStatTypeTraits<ST_RUNG>::FloatStatsUpdateMethods[RUNG_STAT_FLOAT_MAX] =
{
	UPDATE_MIN, // RUNG_STAT_FLOAT_FASTEST_WIN
	UPDATE_REPLACE, // RUNG_STAT_FLOAT_LATEST_TIME
};

UBOOL GStatsWritingEnabled = TRUE;

void UPlayerStats::Initialize()
{
	guard(UPlayerStats::Initialize);

	debugf(NAME_DevPer, TEXT("UPlayerStats::Initialize"));

	// Initialize look-up tables from name arrays.

	InitializeIndexTable(&CharIndexTable, CharNames, ST_CHAR);
	InitializeIndexTable(&MapIndexTable, MapNames, ST_MAP);
	InitializeIndexTable(&WeaponIndexTable, WeaponNames, ST_WEAPON);
	InitializeIndexTable(&ComboIndexTable, ComboNames, ST_COMBO);

	if ( !bMultiPlayer )
		InitializeIndexTable(&RungIndexTable, RungNames, ST_RUNG);

	// Initialize stats cache.
	ResetValues();

	unguard;
}

void UPlayerStats::ResetValues()
{
	guard(UPlayerStats::ResetValues);

	INT TableCount = NumTables;

	// if we're a multiplayer-only stat object, remove the entries for single player rungs
	if ( bMultiPlayer )
		TableCount -= TStatTypeTraits<ST_RUNG>::NumTablesWithEntries;

	StatsCache.Empty(TableCount * NumFieldsPerTable);
	StatsCache.AddZeroed(TableCount * NumFieldsPerTable);

	unguard;
}

// Initialize fast look-up table from linear name array.
void UPlayerStats::InitializeIndexTable(TMap<FName, INT>** IndexTable, const TArrayNoInit<FName>& Names, EStatType Type)
{
	guard(UPlayerStats::InitializeIndexTable);

	if (*IndexTable == NULL)
	{
		*IndexTable = new TMap<FName, INT>;

		TArray<FName>* ReverseTable = NEW TArray<FName>;
		ReverseLoopupTable[Type] = ReverseTable;

		for (INT NameIdx = 0; NameIdx < Names.Num(); NameIdx++)
		{
			(*IndexTable)->Add(Names(NameIdx), NameIdx);
			ReverseTable->AddItem(Names(NameIdx));
		}
	}

	unguard;
}

FString UPlayerStats::PrintTable(INT TableIdx)
{
	FString TableContents = FString::Printf(TEXT("Table %d:\n"), TableIdx);

	for (INT FieldIdx = 0, CacheIdx = TableIdx * NumFieldsPerTable; FieldIdx < NumFieldsPerTable; CacheIdx++, FieldIdx++)
	{
		StatValue Value = StatsCache(CacheIdx);
		FString Str = FString::Printf(TEXT("    as int: %d        as float: %f"), Value.IntValue, Value.FloatValue);
		debugf(NAME_DevPer, *Str);
	}

	return TableContents;
}

/*==========================================================================================================
function	UPlayerStats::GetNameByIndex
Desc:		Goes the opposite of GetIndexByName.  Returns the name located in the name/index map associated
			with the specified stat type for the index specified.

input:		TableType
				the table type to look in.

			TableIndex
				the index of the name.

last modified by:	Ron Prestenback
==========================================================================================================*/
FName UPlayerStats::GetNameByIndex( EStatType TableType, INT TableIndex )
{
	guard(UPlayerStats::GetNameByIndex);

	check(TableType>ST_INVALID);
	check(TableType<ST_MAX);

	TArray<FName>* Table = ReverseLoopupTable[TableType];
	if ( Table != NULL && TableIndex >= 0 && TableIndex < Table->Num() )
		return (*Table)(TableIndex);

	return NAME_None;

	unguard;
}

INT UPlayerStats::GetIndexByName(TMap<FName, INT>* IndexTable, const FName& Name)
{
	guard(UPlayerStats::GetIndexByName);

	if (IndexTable != NULL)
	{
		INT* ValuePtr = IndexTable->Find(Name);
		if (ValuePtr != NULL)
		{
			return *ValuePtr;
		}
	}

	return -1;

	unguard;
}

INT UPlayerStats::GetCharIndexByName(const FName& CharName)
{
	guard(UPlayerStats::GetCharIndexByName);

	return GetIndexByName(CharIndexTable, CharName);

	unguard;
}

INT UPlayerStats::GetMapIndexByName(const FName& MapName)
{
	guard(UPlayerStats::GetMapIndexByName);

	return GetIndexByName(MapIndexTable, MapName);

	unguard;
}

INT UPlayerStats::GetWeaponIndexByName(const FName& WeaponName)
{
	guard(UPlayerStats::GetWeaponIndexByName);

	return GetIndexByName(WeaponIndexTable, WeaponName);

	unguard;
}

INT UPlayerStats::GetComboIndexByName(const FName& ComboName)
{
	guard(UPlayerStats::GetComboIndexByName);

	return GetIndexByName(ComboIndexTable, ComboName);

	unguard;
}

INT UPlayerStats::GetRungIndexByName( const FName& RungName )
{
	guard(UPlayerStats::GetRungIndexByName);

	return GetIndexByName(RungIndexTable, RungName);
	unguard;
}

// Calculate an index into the linear stats cache given an entry, a statistic and the type of the statistic.
template<EStatType StatType>
INT UPlayerStats::GetCacheIndex(INT EntryIdx, INT StatIdx, EStatValueType ValueType)
{
	if (EntryIdx == -1)
	{
		return -1;
	}

	// assume second map of the mapnames array, third int map stat
	// TableIdx = 201 + (2 / 6 [0]); = 201
	int TableIdx = TStatTypeTraits<StatType>::FirstTableWithEntries + (EntryIdx / TStatTypeTraits<StatType>::NumEntriesPerTable);

	// FieldIdx = 10 * (2 % 5 [2]) + 3 = 23
	int FieldIdx = TStatTypeTraits<StatType>::NumStatsPerEntry * (EntryIdx % TStatTypeTraits<StatType>::NumEntriesPerTable) + StatIdx;

	// Float values are placed after integer values so if dealing with a float, offset by the appropriate amount.
	if (ValueType == SVT_FLOAT)
	{
		FieldIdx += TStatTypeTraits<StatType>::NumIntStatsPerEntry;
	}

	// Calculate the index into the linear cache.

	// 23 + (201 * 64) = 12887
	return FieldIdx + (TableIdx * NumFieldsPerTable);
}

template<EStatType StatType>
INT UPlayerStats::GetIntFromCache(INT EntryIdx, INT StatIdx) const
{
	INT IntValue = 0;

	INT CacheIdx = GetCacheIndex<StatType>(EntryIdx, StatIdx, SVT_INT);
	if (CacheIdx >= 0 && CacheIdx < StatsCache.Num())
	{
		StatValue Value = StatsCache(CacheIdx);
		IntValue = Value.IntValue;
	}

	return IntValue;
}

template<EStatType StatType>
UBOOL UPlayerStats::PutIntInCache(INT EntryIdx, INT StatIdx, INT IntValue, UBOOL bAdditive)
{
	if ( CanRecord() )
	{
		INT CacheIdx = GetCacheIndex<StatType>(EntryIdx, StatIdx, SVT_INT);
		if (CacheIdx >= 0 && CacheIdx < StatsCache.Num())
		{
			StatsCache(CacheIdx).IntValue = bAdditive ? StatsCache(CacheIdx).IntValue + IntValue : IntValue;
			return true;
		}
	}

	return false;
}

template<EStatType StatType>
FLOAT UPlayerStats::GetFloatFromCache(INT EntryIdx, INT StatIdx) const
{
	FLOAT FloatValue = 0.0f;

	INT CacheIdx = GetCacheIndex<StatType>(EntryIdx, StatIdx, SVT_FLOAT);
	if (CacheIdx >= 0 && CacheIdx < StatsCache.Num())
	{
		StatValue Value = StatsCache(CacheIdx);
		FloatValue = Value.FloatValue;
	}

	return FloatValue;
}

template<EStatType StatType>
UBOOL UPlayerStats::PutFloatInCache(INT EntryIdx, INT StatIdx, FLOAT FloatValue, UBOOL bAdditive)
{
	INT CacheIdx = GetCacheIndex<StatType>(EntryIdx, StatIdx, SVT_FLOAT);

	if ( CanRecord() )
	{
		if (CacheIdx >= 0 && CacheIdx < StatsCache.Num())
		{
			StatsCache(CacheIdx).FloatValue = bAdditive ? StatsCache(CacheIdx).FloatValue + FloatValue : FloatValue;
			return true;
		}
	}

	return false;
}

#ifdef _XBOX

void UPlayerStats::GetRungFromLive(FName RungName)
{
	guard(UPlayerStats::GetRungFromLive);

	// Check that the user is signed in.
	UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
	if (UIX == NULL || !UIX->SignedIn())
	{
		debugf(TEXT("Trying to call UPlayerStats::UpdateRungOnLive() without being signed into Live."));
		return;
	}

	TArray<XONLINE_STAT> Stats;

	const INT NumIntRungStats = RUNG_STAT_INT_MAX;
	for (INT StatIndex = 0; StatIndex < NumIntRungStats; StatIndex++)
	{
		XONLINE_STAT Stat;
		Stat.wID = StatIndex + 1;
		Stat.type = XONLINE_STAT_LONG;
		new(Stats) XONLINE_STAT(Stat);
	}

	const INT NumFloatRungStats = RUNG_STAT_FLOAT_MAX;
	for (INT StatIndex = 0; StatIndex < NumFloatRungStats; StatIndex++)
	{
		XONLINE_STAT Stat;
		Stat.wID = StatIndex + NumIntRungStats + 1;
		Stat.type = XONLINE_STAT_DOUBLE;
		new(Stats) XONLINE_STAT(Stat);
	}

	INT LeaderboardIndex = GetCacheIndex<ST_RUNG>(GetRungIndexByName(RungName), 0, SVT_INT) / NumFieldsPerTable;

	XONLINE_STAT_SPEC* StatSpec = new XONLINE_STAT_SPEC;
	StatSpec->xuidUser = UIX->GetXUID();
	StatSpec->dwLeaderBoardID = 1 + LeaderboardIndex;
	StatSpec->dwNumStats = Stats.Num();
	StatSpec->pStats = Stats.Release();

	XboxLiveManager* Manager = XboxLiveManager::getXboxLiveManager();
	if (Manager != NULL)
	{
		Manager->StartStatsRead(1, StatSpec);
	}

	unguard;
}

void UPlayerStats::EnumerateRungFromLive(FName RungName, INT NumUsers)
{
	guard(UPlayerStats::EnumerateRungFromLive);

	// Check that the user is signed in.
	UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
	if (UIX == NULL || !UIX->SignedIn())
	{
		debugf(TEXT("Trying to call UPlayerStats::UpdateRungOnLive() without being signed into Live."));
		return;
	}

	TArray<WORD> Stats;
	for (WORD StatIndex = 0; StatIndex < RUNG_STAT_FLOAT_MAX + RUNG_STAT_INT_MAX; StatIndex++)
	{
		new(Stats) WORD(StatIndex + 1);
	}

	INT LeaderboardIndex = GetCacheIndex<ST_RUNG>(GetRungIndexByName(RungName), 0, SVT_INT) / NumFieldsPerTable;

	XboxLiveManager* Manager = XboxLiveManager::getXboxLiveManager();
	if (Manager != NULL)
	{
		Manager->StartStatsEnumerate(1 + LeaderboardIndex, NumUsers, Stats.Num(), &Stats(0));
	}

	unguard;
}
#endif // _XBOX

void UPlayerStats::execPrintTable(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execPrintTable);

	P_GET_INT(TableIdx);
	P_FINISH;

	*(FString *) Result = PrintTable(TableIdx);

	unguardexec;
}

EStatType UPlayerStats::GetStatType( INT CacheIndex ) const
{
	guard(UPlayerStats::GetStatType);

	if ( CacheIndex < 0 || CacheIndex >= StatsCache.Num() )
		return ST_INVALID;

	if ( CacheIndex < TStatTypeTraits<ST_MAP>::FirstStatIndex )
		return ST_GENERAL;

	if ( CacheIndex < TStatTypeTraits<ST_CHAR>::FirstStatIndex )
		return ST_MAP;

	if ( CacheIndex < TStatTypeTraits<ST_WEAPON>::FirstStatIndex )
		return ST_CHAR;

	if ( CacheIndex < TStatTypeTraits<ST_RUNG>::FirstStatIndex )
		return ST_WEAPON;

	return ST_RUNG;

	unguard;
}

/*==========================================================================================================
function	UPlayerStats::ReverseMap
Desc:		Given an index into the StatsCache array, retrieves data about the stat that lives at that index.

input:		CacheIndex:
				index into the PlayerStats.StatsCache array for the stat data we want to grab.

output:		StatTableIndex
				Out of the total number of leaderboards required for this stat type, indicates which leaderboard
				the specified stat exists on.

			StatTableSlot:
				this represents the index within the current leaderboard for the specified stat.

			DataName:
				name of the stat as it appears in the corresonding names array for this stat type.  (e.g.
				if the cache index specified points to a map stat, this will be set to the name of the map)

			NameIndex:
				the index into the specified stats corresonding names array for the specified stat. 
				(i.e. this stat belongs to the first map, second map, third map, so on)

			IntStatIndex:
				If this stat is an int stat, this will be set to the index corresponding to the enum value
				that represents this stat.  Will be set to -1 if the stat is a float stat.

			FloatStatIndex:
				If this stat is a float stat, this will be set the index corresponding to the enum value
				that represents this stat.  Will be set to -1 if the stat is an int stat.

return:		Returns true if the specified CacheIndex corresponds to a valid stat, or false if the specified
			CacheIndex is pointing to an empty slot.

last modified by:	Ron Prestenback
==========================================================================================================*/
template<EStatType Type>
UBOOL UPlayerStats::ReverseMap( INT CacheIndex, INT& StatTableIndex, INT& StatTableSlot, FName& DataName, INT& NameIndex, INT& IntStatIndex, INT& FloatStatIndex )
{
	guard(UPlayerStats::ReverseMap);

	/* this is how far away this stat is from the first stat of this type, across all leaderboards of this type */
	INT TotalStatDelta = CacheIndex - TStatTypeTraits<Type>::FirstStatIndex;

	/* out of the total number of leaderboards required to record this stat type, which leaderboard are we on? */
	StatTableIndex = TotalStatDelta / NumFieldsPerTable;

	/* this is how far away we are from the first stat of our own leaderboard */
	StatTableSlot = TotalStatDelta - NumFieldsPerTable * StatTableIndex;

	/*	if the offset from the beginning of the leaderboard falls within a leaderboard
	"dead-zone" (where there wasn't enough slots left in the leaderboard to place
	another full stat entry), then there is no stat here.
	*/
	if ( StatTableSlot >= TStatTypeTraits<Type>::NumStatsPerEntry * TStatTypeTraits<Type>::NumEntriesPerTable )
	{
		DataName = NAME_None;
		NameIndex = 0;
		IntStatIndex = -1;
		FloatStatIndex = -1;
		return false;
	}

	/* this is the number of index for the first name of the first entry stored on
	our leaderboard. (i.e. if the stat type stores 6 items per leaderboard, and
	we are on the second leaderboard, MemberOffset should be 6.
	*/
	INT MemberOffset = StatTableIndex * TStatTypeTraits<Type>::NumEntriesPerTable;

	/* this is the index for the member of the names array that corresponds to our stat, within this leaderboard */
	INT MemberIndex = StatTableSlot / TStatTypeTraits<Type>::NumStatsPerEntry;

	/* this is the index into the stat type's items (both enums) for this particular stat */
	INT InnerStatIndex = StatTableSlot % TStatTypeTraits<Type>::NumStatsPerEntry;

	/* this is the index for the name this stat is associated with */
	NameIndex = MemberOffset + MemberIndex;

	/* this is the name this stat is associated with */
	DataName = GetNameByIndex(Type, NameIndex);
	if ( InnerStatIndex < TStatTypeTraits<Type>::NumIntStatsPerEntry )
	{
		/* this is an int stat */
		IntStatIndex = InnerStatIndex;
		FloatStatIndex = -1;
	}
	else
	{
		InnerStatIndex -= TStatTypeTraits<Type>::NumIntStatsPerEntry;
		FloatStatIndex = InnerStatIndex;
		IntStatIndex = -1;
	}

	return true;
	unguard;
}

void UPlayerStats::DebugDump()
{
	guard(UPlayerStats::DebugDump);

	for ( INT i = 0; i < StatsCache.Num(); i++ )
	{
		EStatType Type = GetStatType(i);
		FName DataName;
		INT LeaderboardIndex = 0,
			LeaderboardSlot = 0,
			NameIndex = 0,
			IntStatIndex = -1,
			FloatStatIndex = -1;

		UBOOL bValid = 0;
		switch ( Type )
		{
		case ST_MAP:
			{
				bValid = ReverseMap<ST_MAP>(i, LeaderboardIndex, LeaderboardSlot, DataName, NameIndex, IntStatIndex, FloatStatIndex);
				break;
			}
		case ST_CHAR:
			{
				bValid = ReverseMap<ST_CHAR>(i, LeaderboardIndex, LeaderboardSlot, DataName, NameIndex, IntStatIndex, FloatStatIndex);
				break;
			}
		case ST_WEAPON:
			{
				bValid = ReverseMap<ST_WEAPON>(i, LeaderboardIndex, LeaderboardSlot, DataName, NameIndex, IntStatIndex, FloatStatIndex);
				break;
			}
		case ST_RUNG:
			{
				bValid = ReverseMap<ST_RUNG>(i, LeaderboardIndex, LeaderboardSlot, DataName, NameIndex, IntStatIndex, FloatStatIndex);
				break;
			}
		case ST_COMBO:
			{
				bValid = ReverseMap<ST_COMBO>(i, LeaderboardIndex, LeaderboardSlot, DataName, NameIndex, IntStatIndex, FloatStatIndex);
				break;
			}
		case ST_GENERAL:
			{
				debugf(TEXT("%05i (General) = %i"), i, StatsCache(i).IntValue);
				break;
			}
		}

		if ( Type == ST_INVALID || Type == ST_GENERAL )
			continue;

		if ( IntStatIndex != -1 )
		{
			debugf( TEXT("%05i: <%02i - %02i> (%02i %i * ) %s = %i"), i, LeaderboardIndex, LeaderboardSlot, NameIndex, IntStatIndex, *DataName, StatsCache(i).IntValue );
		}
		else if ( FloatStatIndex != -1 )
		{
			debugf( TEXT("%05i: <%02i - %02i> (%02i %i **) %s = %f"), i, LeaderboardIndex, LeaderboardSlot, NameIndex, FloatStatIndex, *DataName, StatsCache(i).FloatValue );
		}

		if ( LeaderboardSlot == NumFieldsPerTable - 1 )
			debugf(TEXT("==================================================================="));
	}

	unguard;
}

FLOAT UPlayerStats::UpdateValue(EUpdateMethod UpdateMethod, FLOAT SrcValue, FLOAT DestValue)
{
	guard(UPlayerStats::UpdateValue);

	FLOAT Result;

	if (SrcValue == 0.0f)
	{
		return DestValue;
	}

	switch (UpdateMethod)
	{
	case UPDATE_ADD:
		Result = SrcValue + DestValue;
		break;

	case UPDATE_MIN:
		if (DestValue != 0.0f)
		{
			Result = Min(SrcValue, DestValue);
		}
		else
		{
			Result = SrcValue;
		}
		break;

	case UPDATE_MAX:
		if (DestValue != 0.0f)
		{
			Result = Max(SrcValue, DestValue);
		}
		else
		{
			Result = SrcValue;
		}
		break;

	case UPDATE_REPLACE:
		Result = SrcValue;
		break;

	default:
		// This should never happen.
		Result = 0;
		check(false);
		break;
	}

	return Result;

	unguard;
}

INT UPlayerStats::UpdateValue(EUpdateMethod UpdateMethod, INT SrcValue, INT DestValue)
{
	guard(UPlayerStats::UpdateValue);

	INT Result;

	if (SrcValue == 0)
	{
		return DestValue;
	}

	switch (UpdateMethod)
	{
	case UPDATE_ADD:
		Result = SrcValue + DestValue;
		break;

	case UPDATE_MIN:
		if (DestValue != 0)
		{
			Result = Min(SrcValue, DestValue);
		}
		else
		{
			Result = SrcValue;
		}
		break;

	case UPDATE_MAX:
		if (DestValue != 0)
		{
			Result = Max(SrcValue, DestValue);
		}
		else
		{
			Result = SrcValue;
		}
		break;

	case UPDATE_REPLACE:
		Result = SrcValue;
		break;

	default:
		// This should never happen.
		Result = 0;
		check(false);
		break;
	}

	return Result;

	unguard;
}

void UPlayerStats::UpdateRungs(const UPlayerStats& SrcStats)
{
	guard(UPlayerStats::UpdateRungs);

	// For each rung
	for (INT RungIndex = 0; RungIndex < RungNames.Num(); RungIndex++)
	{
		// Get the name of the rung.
		FName RungName = RungNames(RungIndex);

		// For each integer statistic
		const INT NumIntRungStats = RUNG_STAT_INT_MAX;
		for (INT StatIndex = 0; StatIndex < NumIntRungStats; StatIndex++)
		{
			// Find the update method for the statistic.
			EUpdateMethod UpdateMethod = TStatTypeTraits<ST_RUNG>::IntStatsUpdateMethods[StatIndex];
			// Get the value of the statistic in the source.
			INT SrcValue = SrcStats.GetIntStatByRung(RungName, StatIndex);
			// Get the value of the statistic in the destination.
			INT DestValue = GetIntStatByRung(RungName, StatIndex);
			// Calculate the updated value of the statistic.
			INT UpdatedValue = UpdateValue(UpdateMethod, SrcValue, DestValue);
			// Set the update value in the destination.
			const UBOOL bAdditive = TRUE;
			SetIntStatByRung(RungName, StatIndex, UpdatedValue, !bAdditive);
		}

		// For each floating-point statistic
		const INT NumFloatRungStats = RUNG_STAT_FLOAT_MAX;
		for (INT StatIndex = 0; StatIndex < NumFloatRungStats; StatIndex++)
		{
			// Find the update method for the statistic.
			EUpdateMethod UpdateMethod = TStatTypeTraits<ST_RUNG>::FloatStatsUpdateMethods[StatIndex];
			// Get the value of the statistic in the source.
			FLOAT SrcValue = SrcStats.GetFloatStatByRung(RungName, StatIndex);
			// Get the value of the statistic in the destination.
			FLOAT DestValue = GetFloatStatByRung(RungName, StatIndex);
			// Calculate the updated value of the statistic.
			FLOAT UpdatedValue = UpdateValue(UpdateMethod, SrcValue, DestValue);
			// Set the update value in the destination.
			UBOOL bAdditive = TRUE;
			SetFloatStatByRung(RungName, StatIndex, UpdatedValue, !bAdditive);
		}
	}

	unguard;
}

void UPlayerStats::UpdateGeneralStats(const UPlayerStats& SrcStats)
{
	guard(UPlayerStats::UpdateGeneralStats);

	// For each integer statistic
	const INT NumIntStats = INT_STAT_MAX;
	for (INT StatIndex = 0; StatIndex < NumIntStats; StatIndex++)
	{
		// Find the update method for the statistic.
		EUpdateMethod UpdateMethod = IntStatsUpdateMethods[StatIndex];
		// Get the value of the statistic in the source.
		INT SrcValue = SrcStats.GetIntStat(StatIndex);
		// Get the value of the statistic in the destination.
		INT DestValue = GetIntStat(StatIndex);
		// Calculate the updated value of the statistic.
		INT UpdatedValue = UpdateValue(UpdateMethod, SrcValue, DestValue);
		// Set the update value in the destination.
		const UBOOL bAdditive = TRUE;
		SetIntStat(StatIndex, UpdatedValue, !bAdditive);
	}

	unguard;
}

void UPlayerStats::UpdateMaps(const UPlayerStats& SrcStats)
{
	guard(UPlayerStats::UpdateMaps);

	// For each map
	for (INT MapIndex = 0; MapIndex < MapNames.Num(); MapIndex++)
	{
		// Get the name of the map.
		FName MapName = MapNames(MapIndex);

		// For each integer statistic
		const INT NumIntStats = TStatTypeTraits<ST_MAP>::NumIntStatsPerEntry;
		for (INT StatIndex = 0; StatIndex < NumIntStats; StatIndex++)
		{
			// Find the update method for the statistic.
			EUpdateMethod UpdateMethod = TStatTypeTraits<ST_MAP>::IntStatsUpdateMethods[StatIndex];
			// Get the value of the statistic in the source.
			INT SrcValue = SrcStats.GetIntStatByMap(MapName, StatIndex);
			// Get the value of the statistic in the destination.
			INT DestValue = GetIntStatByMap(MapName, StatIndex);
			// Calculate the updated value of the statistic.
			INT UpdatedValue = UpdateValue(UpdateMethod, SrcValue, DestValue);
			// Set the update value in the destination.
			const UBOOL bAdditive = TRUE;
			SetIntStatByMap(MapName, StatIndex, UpdatedValue, !bAdditive);
		}

		// For each floating-point statistic
		const INT NumFloatStats = TStatTypeTraits<ST_MAP>::NumFloatStatsPerEntry;
		for (INT StatIndex = 0; StatIndex < NumFloatStats; StatIndex++)
		{
			// Find the update method for the statistic.
			EUpdateMethod UpdateMethod = TStatTypeTraits<ST_MAP>::FloatStatsUpdateMethods[StatIndex];
			// Get the value of the statistic in the source.
			FLOAT SrcValue = SrcStats.GetFloatStatByMap(MapName, StatIndex);
			// Get the value of the statistic in the destination.
			FLOAT DestValue = GetFloatStatByMap(MapName, StatIndex);
			// Calculate the updated value of the statistic.
			FLOAT UpdatedValue = UpdateValue(UpdateMethod, SrcValue, DestValue);
			// Set the update value in the destination.
			UBOOL bAdditive = TRUE;
			SetFloatStatByMap(MapName, StatIndex, UpdatedValue, !bAdditive);
		}
	}

	unguard;
}

void UPlayerStats::UpdateCombos(const UPlayerStats& SrcStats)
{
	guard(UPlayerStats::UpdateCombos);

	// For each map
	for (INT ComboIndex = 0; ComboIndex < ComboNames.Num(); ComboIndex++)
	{
		// Get the name of the map.
		FName ComboName = ComboNames(ComboIndex);

		// For each integer statistic
		const INT NumIntStats = TStatTypeTraits<ST_COMBO>::NumIntStatsPerEntry;
		for (INT StatIndex = 0; StatIndex < NumIntStats; StatIndex++)
		{
			// Find the update method for the statistic.
			EUpdateMethod UpdateMethod = TStatTypeTraits<ST_COMBO>::IntStatsUpdateMethods[StatIndex];
			// Get the value of the statistic in the source.
			INT SrcValue = SrcStats.GetIntStatByCombo(ComboName, StatIndex);
			// Get the value of the statistic in the destination.
			INT DestValue = GetIntStatByCombo(ComboName, StatIndex);
			// Calculate the updated value of the statistic.
			INT UpdatedValue = UpdateValue(UpdateMethod, SrcValue, DestValue);
			// Set the update value in the destination.
			const UBOOL bAdditive = TRUE;
			SetIntStatByCombo(ComboName, StatIndex, UpdatedValue, !bAdditive);
		}
/*
		// For each floating-point statistic
		const INT NumFloatStats = TStatTypeTraits<ST_COMBO>::NumFloatStatsPerEntry;
		for (INT StatIndex = 0; StatIndex < NumFloatStats; StatIndex++)
		{
			// Find the update method for the statistic.
			EUpdateMethod UpdateMethod = TStatTypeTraits<ST_COMBO>::IntStatsUpdateMethods[StatIndex];
			// Get the value of the statistic in the source.
			FLOAT SrcValue = SrcStats.GetFloatStatByCombo(ComboName, StatIndex);
			// Get the value of the statistic in the destination.
			FLOAT DestValue = GetFloatStatByCombo(ComboName, StatIndex);
			// Calculate the updated value of the statistic.
			FLOAT UpdatedValue = UpdateValue(UpdateMethod, SrcValue, DestValue);
			// Set the update value in the destination.
			UBOOL bAdditive = TRUE;
			SetFloatStatByCombo(ComboName, StatIndex, UpdatedValue, !bAdditive);
		}
*/
	}

	unguard;
}

void UPlayerStats::UpdateCharacters(const UPlayerStats& SrcStats)
{
	guard(UPlayerStats::UpdateCharacters);

	// For each map
	for (INT CharacterIndex = 0; CharacterIndex < CharNames.Num(); CharacterIndex++)
	{
		// Get the name of the map.
		FName CharacterName = CharNames(CharacterIndex);

		// For each integer statistic
		const INT NumIntStats = TStatTypeTraits<ST_CHAR>::NumIntStatsPerEntry;
		for (INT StatIndex = 0; StatIndex < NumIntStats; StatIndex++)
		{
			// Find the update method for the statistic.
			EUpdateMethod UpdateMethod = TStatTypeTraits<ST_CHAR>::IntStatsUpdateMethods[StatIndex];
			// Get the value of the statistic in the source.
			INT SrcValue = SrcStats.GetIntStatByCharacter(CharacterName, StatIndex);
			// Get the value of the statistic in the destination.
			INT DestValue = GetIntStatByCharacter(CharacterName, StatIndex);
			// Calculate the updated value of the statistic.
			INT UpdatedValue = UpdateValue(UpdateMethod, SrcValue, DestValue);
			// Set the update value in the destination.
			const UBOOL bAdditive = TRUE;
			SetIntStatByCharacter(CharacterName, StatIndex, UpdatedValue, !bAdditive);
		}

		// For each floating-point statistic
		const INT NumFloatStats = TStatTypeTraits<ST_CHAR>::NumFloatStatsPerEntry;
		for (INT StatIndex = 0; StatIndex < NumFloatStats; StatIndex++)
		{
			// Find the update method for the statistic.
			EUpdateMethod UpdateMethod = TStatTypeTraits<ST_CHAR>::FloatStatsUpdateMethods[StatIndex];
			// Get the value of the statistic in the source.
			FLOAT SrcValue = SrcStats.GetFloatStatByCharacter(CharacterName, StatIndex);
			// Get the value of the statistic in the destination.
			FLOAT DestValue = GetFloatStatByCharacter(CharacterName, StatIndex);
			// Calculate the updated value of the statistic.
			FLOAT UpdatedValue = UpdateValue(UpdateMethod, SrcValue, DestValue);
			// Set the update value in the destination.
			UBOOL bAdditive = TRUE;
			SetFloatStatByCharacter(CharacterName, StatIndex, UpdatedValue, !bAdditive);
		}
	}

	unguard;
}

void UPlayerStats::UpdateWeapons(const UPlayerStats& SrcStats)
{
	guard(UPlayerStats::UpdateWeapons);

	// For each map
	for (INT WeaponIndex = 0; WeaponIndex < WeaponNames.Num(); WeaponIndex++)
	{
		// Get the name of the map.
		FName WeaponName = WeaponNames(WeaponIndex);

		// For each integer statistic
		const INT NumIntStats = TStatTypeTraits<ST_WEAPON>::NumIntStatsPerEntry;
		for (INT StatIndex = 0; StatIndex < NumIntStats; StatIndex++)
		{
			// Find the update method for the statistic.
			EUpdateMethod UpdateMethod = TStatTypeTraits<ST_WEAPON>::IntStatsUpdateMethods[StatIndex];
			// Get the value of the statistic in the source.
			INT SrcValue = SrcStats.GetIntStatByWeapon(WeaponName, StatIndex);
			// Get the value of the statistic in the destination.
			INT DestValue = GetIntStatByWeapon(WeaponName, StatIndex);
			// Calculate the updated value of the statistic.
			INT UpdatedValue = UpdateValue(UpdateMethod, SrcValue, DestValue);
			// Set the update value in the destination.
			const UBOOL bAdditive = TRUE;
			SetIntStatByWeapon(WeaponName, StatIndex, UpdatedValue, !bAdditive);
		}

		// For each floating-point statistic
		const INT NumFloatStats = TStatTypeTraits<ST_WEAPON>::NumFloatStatsPerEntry;
		for (INT StatIndex = 0; StatIndex < NumFloatStats; StatIndex++)
		{
			// Find the update method for the statistic.
			EUpdateMethod UpdateMethod = TStatTypeTraits<ST_WEAPON>::FloatStatsUpdateMethods[StatIndex];
			// Get the value of the statistic in the source.
			FLOAT SrcValue = SrcStats.GetFloatStatByWeapon(WeaponName, StatIndex);
			// Get the value of the statistic in the destination.
			FLOAT DestValue = GetFloatStatByWeapon(WeaponName, StatIndex);
			// Calculate the updated value of the statistic.
			FLOAT UpdatedValue = UpdateValue(UpdateMethod, SrcValue, DestValue);
			// Set the update value in the destination.
			UBOOL bAdditive = TRUE;
			SetFloatStatByWeapon(WeaponName, StatIndex, UpdatedValue, !bAdditive);
		}
	}

	unguard;
}

void UPlayerStats::UpdateStats(const UPlayerStats& SrcStats)
{
	guard(UPlayerStats::UpdateStats);

	UpdateGeneralStats(SrcStats);
	UpdateMaps(SrcStats);
	UpdateCharacters(SrcStats);
	UpdateWeapons(SrcStats);
	UpdateCombos(SrcStats);

	unguard;
}

FLOAT UPlayerStats::GetFloatStatByRung(const FName& RungName, INT StatIndex) const
{
	guard(UPlayerStats::GetFloatStatByRung);

	DWORD RungIndex = GetRungIndexByName(RungName);
	if (RungIndex == -1)
	{
		debugf(TEXT("UPlayerStats::GetFloatStatByRung -- Index for rung %s not found."), *RungName);
		return 0;
	}

	return GetFloatFromCache<ST_RUNG>(GetRungIndexByName(RungName), StatIndex);

	unguard;
}

void UPlayerStats::SetFloatStatByRung(const FName& RungName, INT StatIndex, FLOAT FloatValue, UBOOL bAdditive)
{
	guard(UPlayerStats::SetFloatStatByRung);

	DWORD RungIndex = GetRungIndexByName(RungName);
	if (RungIndex == -1)
	{
		debugf(TEXT("UPlayerStats::SetFloatStatByRung -- Index for rung %s not found."), *RungName);
		return;
	}

	PutFloatInCache<ST_RUNG>(GetRungIndexByName(RungName), StatIndex, FloatValue, bAdditive);

	unguard;
}

INT UPlayerStats::GetIntStatByRung(const FName& RungName, INT StatIndex) const
{
	guard(UPlayerStats::GetIntStatByRung);

	DWORD RungIndex = GetRungIndexByName(RungName);
	if (RungIndex == -1)
	{
		debugf(TEXT("UPlayerStats::GetIntStatByRung -- Index for rung %s not found."), *RungName);
		return 0;
	}

	return GetIntFromCache<ST_RUNG>(GetRungIndexByName(RungName), StatIndex);

	unguard;
}

void UPlayerStats::SetIntStatByRung(const FName& RungName, INT StatIndex, FLOAT IntValue, UBOOL bAdditive)
{
	guard(UPlayerStats::SetIntStatByRung);

	DWORD RungIndex = GetRungIndexByName(RungName);
	if (RungIndex == -1)
	{
		debugf(TEXT("UPlayerStats::SetIntStatByRung -- Index for rung %s not found."), *RungName);
		return;
	}

	PutIntInCache<ST_RUNG>(GetRungIndexByName(RungName), StatIndex, IntValue, bAdditive);

	unguard;
}

void UPlayerStats::execGetIntStatByCombo(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execGetIntStatByCombo);

	P_GET_NAME(ComboName);
	P_GET_BYTE(StatIndex);
	P_FINISH;

	*(INT *) Result = GetIntStatByCombo(ComboName, StatIndex);

	unguardexec;
}

void UPlayerStats::execSetIntStatByCombo(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execSetIntStatByCombo);

	P_GET_NAME(ComboName);
	P_GET_BYTE(StatIndex);
	P_GET_INT(IntValue);
	P_GET_UBOOL_OPTX(bAdditive,0);
	P_FINISH;

	SetIntStatByCombo(ComboName, StatIndex, IntValue, bAdditive);

	unguardexec;
}

void UPlayerStats::execGetFloatStatByMap(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execGetFloatStatByMap);

	P_GET_NAME(MapName);
	P_GET_BYTE(StatIndex);
	P_FINISH;

	*(FLOAT *) Result = GetFloatStatByMap(MapName, StatIndex);

	unguardexec;
}

void UPlayerStats::execSetFloatStatByMap(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execSetFloatStatByMap);

	P_GET_NAME(MapName);
	P_GET_BYTE(StatIndex);
	P_GET_FLOAT(FloatValue);
	P_GET_UBOOL_OPTX(bAdditive,0);
	P_FINISH;

	SetFloatStatByMap(MapName, StatIndex, FloatValue, bAdditive);

	unguardexec;
}


void UPlayerStats::execGetIntStatByMap(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execGetIntStatByMap);

	P_GET_NAME(MapName);
	P_GET_BYTE(StatIndex);
	P_FINISH;

	*(INT *) Result = GetIntStatByMap(MapName, StatIndex);

	unguardexec;
}

void UPlayerStats::execSetIntStatByMap(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execSetIntStatByMap);

	P_GET_NAME(MapName);
	P_GET_BYTE(StatIndex);
	P_GET_INT(IntValue);
	P_GET_UBOOL_OPTX(bAdditive,0);
	P_FINISH;

	SetIntStatByMap(MapName, StatIndex, IntValue, bAdditive);

	unguardexec;
}

void UPlayerStats::execGetFloatStatByWeapon(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execGetFloatStatByWeapon);

	P_GET_NAME(WeaponName);
	P_GET_BYTE(StatIndex);
	P_FINISH;

	*(FLOAT *) Result = GetFloatStatByWeapon(WeaponName, StatIndex);

	unguardexec;
}

void UPlayerStats::execSetFloatStatByWeapon(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execSetFloatStatByWeapon);

	P_GET_NAME(WeaponName);
	P_GET_BYTE(StatIndex);
	P_GET_FLOAT(FloatValue);
	P_GET_UBOOL_OPTX(bAdditive,0);
	P_FINISH;

	SetFloatStatByWeapon(WeaponName, StatIndex, FloatValue, bAdditive);

	unguardexec;
}


void UPlayerStats::execGetIntStatByWeapon(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execGetIntStatByWeapon);

	P_GET_NAME(WeaponName);
	P_GET_BYTE(StatIndex);
	P_FINISH;

	*(INT *) Result = GetIntStatByWeapon(WeaponName, StatIndex);

	unguardexec;
}

void UPlayerStats::execSetIntStatByWeapon(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execSetIntStatByWeapon);

	P_GET_NAME(WeaponName);
	P_GET_BYTE(StatIndex);
	P_GET_INT(IntValue);
	P_GET_UBOOL_OPTX(bAdditive,0);
	P_FINISH;

	SetIntStatByWeapon(WeaponName, StatIndex, IntValue, bAdditive);

	unguardexec;
}

void UPlayerStats::execGetFloatStatByCharacter(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execGetFloatStatByCharacter);

	P_GET_NAME(CharName);
	P_GET_BYTE(StatIndex);
	P_FINISH;

	*(FLOAT *) Result = GetFloatStatByCharacter(CharName, StatIndex);

	unguardexec;
}

void UPlayerStats::execSetFloatStatByCharacter(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execSetFloatStatByCharacter);

	P_GET_NAME(CharName);
	P_GET_BYTE(StatIndex);
	P_GET_FLOAT(FloatValue);
	P_GET_UBOOL_OPTX(bAdditive,0);
	P_FINISH;

	SetFloatStatByCharacter(CharName, StatIndex, FloatValue, bAdditive);

	unguardexec;
}


void UPlayerStats::execGetIntStatByCharacter(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execGetIntStatByCharacter);

	P_GET_NAME(CharName);
	P_GET_BYTE(StatIndex);
	P_FINISH;

	*(INT *) Result = GetIntStatByCharacter(CharName, StatIndex);

	unguardexec;
}

void UPlayerStats::execSetIntStatByCharacter(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execSetIntStatByCharacter);

	P_GET_NAME(CharName);
	P_GET_BYTE(StatIndex);
	P_GET_INT(IntValue);
	P_GET_UBOOL_OPTX(bAdditive,0);
	P_FINISH;

	SetIntStatByCharacter(CharName, StatIndex, IntValue, bAdditive);

	unguardexec;
}

void UPlayerStats::execGetFloatStatByRung(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execGetFloatStatByRung);

	P_GET_NAME(Rung);
	P_GET_BYTE(StatIndex);
	P_FINISH;

	*(FLOAT *) Result = GetFloatStatByRung(Rung, StatIndex);

	unguardexec;
}

void UPlayerStats::execSetFloatStatByRung(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execSetFloatStatByRung);

	P_GET_NAME(Rung);
	P_GET_BYTE(StatIndex);
	P_GET_FLOAT(FloatValue);
	P_GET_UBOOL_OPTX(bAdditive,0);
	P_FINISH;

	SetFloatStatByRung(Rung, StatIndex, FloatValue, bAdditive);

	unguardexec;
}


void UPlayerStats::execGetIntStatByRung(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execGetIntStatByRung);

	P_GET_NAME(Rung);
	P_GET_BYTE(StatIndex);
	P_FINISH;

	*(INT *) Result = GetIntStatByRung(Rung, StatIndex);

	unguardexec;
}

void UPlayerStats::execSetIntStatByRung(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execSetIntStatByRung);

	P_GET_NAME(Rung);
	P_GET_BYTE(StatIndex);
	P_GET_INT(IntValue);
	P_GET_UBOOL_OPTX(bAdditive,0);
	P_FINISH;

	SetIntStatByRung(Rung, StatIndex, IntValue, bAdditive);

	unguardexec;
}

// ...

void UPlayerStats::execGetIntStat(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execGetIntStat);

	P_GET_BYTE(StatIdx);
	P_FINISH;

	*(INT *) Result = GetIntStat(StatIdx);

	unguardexec;
}

void UPlayerStats::execSetIntStat(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execSetIntStat);

	P_GET_BYTE(StatIdx);
	P_GET_INT(IntValue);
	P_GET_UBOOL_OPTX(bAdditive,0);
	P_FINISH;

	SetIntStat(StatIdx, IntValue, bAdditive);

	unguardexec;
}

void UPlayerStats::SetIntStat(BYTE StatIdx, INT IntValue, UBOOL bAdditive)
{
	guard(UPlayerStats::SetIntStat);

	if (CanRecord() && StatIdx >= 0 && StatsCache.Num() > 0)
	{
		if (bAdditive)
		{
			StatsCache(StatIdx).IntValue += IntValue;
		}
		else
		{
			StatsCache(StatIdx).IntValue = IntValue;
		}
	}

	unguard;
}

INT UPlayerStats::GetIntStat(BYTE StatIdx) const
{
	guard(UPlayerStats::GetIntStat);

	if (StatIdx >= 0 && StatsCache.Num() > 0)
	{
		return StatsCache(StatIdx).IntValue;
	}
	else
	{
		return 0;
	}

	unguard;
}

void UPlayerStats::execSetIntStatIfHigher(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execSetIntStatIfHigher)

	P_GET_BYTE(StatIdx);
	P_GET_INT(IntValue);
	P_FINISH;

	if (StatIdx >=0 && StatsCache.Num() > 0 && IntValue > StatsCache(StatIdx).IntValue)
	{
		StatsCache(StatIdx).IntValue = IntValue;
	}

	unguard;
}

void UPlayerStats::execInitialize(FFrame& Stack, RESULT_DECL)
{
	guard(UPlayerStats::execInitialize);

	P_FINISH;

	Initialize();

	unguardexec;
}

void UPlayerStats::execResetValues( FFrame& Stack, RESULT_DECL )
{
	guard(UPlayerStats::execResetValues);

	P_FINISH;

	ResetValues();

	unguardexec;
}

INT UPlayerStats::GetIntStatByCombo(const FName& ComboName, INT StatIndex) const
{
	guard(UPlayerStats::GetIntStatByCombo);

	check(StatIndex == 0);
	INT ComboIndex = GetComboIndexByName(ComboName);
	if (ComboIndex != -1)
	{
		INT CacheIndex = ComboIndex + TStatTypeTraits<ST_COMBO>::FirstStatIndex;
		check(CacheIndex < 64);
		if (CacheIndex < StatsCache.Num())
		{
			return StatsCache(CacheIndex).IntValue;
		}
		else
		{
			return 0;
		}
	}
	else
	{
		debugf(TEXT("UPlayerStats::GetIntStatByCombo -- Bad combo name: %s"), *ComboName);
		return 0;
	}

	unguard;
}

void UPlayerStats::SetIntStatByCombo(const FName& ComboName, INT StatIndex, FLOAT IntValue, UBOOL bAdditive)
{
	guard(UPlayerStats::SetIntStatByCombo);

	check(StatIndex == 0);
	INT ComboIndex = GetComboIndexByName(ComboName);
	if (ComboIndex != -1)
	{
		INT CacheIndex = ComboIndex + TStatTypeTraits<ST_COMBO>::FirstStatIndex;
		check(CacheIndex < 64);
		if (CacheIndex < StatsCache.Num())
		{
			if (bAdditive)
			{
				StatsCache(CacheIndex).IntValue += IntValue;
			}
			else
			{
				StatsCache(CacheIndex).IntValue = IntValue;
			}
		}
	}
	else
	{
		debugf(TEXT("UPlayerStats::SetIntStatByCombo -- Bad combo name: %s"), *ComboName);
	}

	unguard;
}

FLOAT UPlayerStats::GetFloatStatByMap(const FName& MapName, INT StatIndex) const
{
	guard(UPlayerStats::GetFloatStatByMap);

	return GetFloatFromCache<ST_MAP>(GetMapIndexByName(MapName), StatIndex);

	unguard;
}


void UPlayerStats::SetFloatStatByMap(const FName& MapName, INT StatIndex, FLOAT FloatValue, UBOOL bAdditive)
{
	guard(UPlayerStats::SetFloatStatByMap);

	PutFloatInCache<ST_MAP>(GetMapIndexByName(MapName), StatIndex, FloatValue, bAdditive);

	unguard;
}

INT UPlayerStats::GetIntStatByMap(const FName& MapName, INT StatIndex) const
{
	guard(UPlayerStats::GetIntStatByMap);

	return GetIntFromCache<ST_MAP>(GetMapIndexByName(MapName), StatIndex);

	unguard;
}

void UPlayerStats::SetIntStatByMap(const FName& MapName, INT StatIndex, FLOAT IntValue, UBOOL bAdditive)
{
	guard(UPlayerStats::SetIntStatByMap);

	PutIntInCache<ST_MAP>(GetMapIndexByName(MapName), StatIndex, IntValue, bAdditive);

	unguard;
}

FLOAT UPlayerStats::GetFloatStatByWeapon(const FName& WeaponName, INT StatIndex) const
{
	guard(UPlayerStats::GetFloatStatByWeapon);

	return GetFloatFromCache<ST_WEAPON>(GetWeaponIndexByName(WeaponName), StatIndex);

	unguard;
}


void UPlayerStats::SetFloatStatByWeapon(const FName& WeaponName, INT StatIndex, FLOAT FloatValue, UBOOL bAdditive)
{
	guard(UPlayerStats::SetFloatStatByWeapon);

	PutFloatInCache<ST_WEAPON>(GetWeaponIndexByName(WeaponName), StatIndex, FloatValue, bAdditive);

	unguard;
}

INT UPlayerStats::GetIntStatByWeapon(const FName& WeaponName, INT StatIndex) const
{
	guard(UPlayerStats::GetIntStatByWeapon);

	return GetIntFromCache<ST_WEAPON>(GetWeaponIndexByName(WeaponName), StatIndex);

	unguard;
}

void UPlayerStats::SetIntStatByWeapon(const FName& WeaponName, INT StatIndex, FLOAT IntValue, UBOOL bAdditive)
{
	guard(UPlayerStats::SetIntStatByWeapon);

	PutIntInCache<ST_WEAPON>(GetWeaponIndexByName(WeaponName), StatIndex, IntValue, bAdditive);

	unguard;
}

FLOAT UPlayerStats::GetFloatStatByCharacter(const FName& CharName, INT StatIndex) const
{
	guard(UPlayerStats::GetFloatStatByCharacter);

	return GetFloatFromCache<ST_CHAR>(GetCharIndexByName(CharName), StatIndex);

	unguard;
}

void UPlayerStats::SetFloatStatByCharacter(const FName& CharName, INT StatIndex, FLOAT FloatValue, UBOOL bAdditive)
{
	guard(UPlayerStats::SetFloatStatByCharacter);

	PutFloatInCache<ST_CHAR>(GetCharIndexByName(CharName), StatIndex, FloatValue, bAdditive);

	unguard;
}

INT UPlayerStats::GetIntStatByCharacter(const FName& CharName, INT StatIndex) const
{
	guard(UPlayerStats::GetIntStatByCharacter);

	return GetIntFromCache<ST_CHAR>(GetCharIndexByName(CharName), StatIndex);

	unguard;
}

void UPlayerStats::SetIntStatByCharacter(const FName& CharName, INT StatIndex, FLOAT IntValue, UBOOL bAdditive)
{
	guard(UPlayerStats::SetIntStatByChar);

	PutIntInCache<ST_CHAR>(GetCharIndexByName(CharName), StatIndex, IntValue, bAdditive);

	unguard;
}

// =======================================================================================================================================================
// =======================================================================================================================================================
// UPlayerStatCache
// =======================================================================================================================================================
// =======================================================================================================================================================
void UPlayerStatCache::execGetQueriedStats( FFrame& Stack, RESULT_DECL )
{
	guard(UPlayerStatCache::execGetQueriedStats);

	P_GET_TARRAY_REF(Maps,FName);
	P_GET_TARRAY_REF(Characters,FName);
	P_GET_TARRAY_REF(Weapons,FName);
	P_GET_TARRAY_REF(Combos,FName);
	P_FINISH;

	Maps->Empty();
	Characters->Empty();
	Weapons->Empty();
	Combos->Empty();

	for ( INT i = 0; i < StatsCache.Num(); i++ )
	{
		EStatType Type = GetStatType(i);
		FName DataName;
		INT LeaderboardIndex = 0,
			LeaderboardSlot = 0,
			NameIndex = 0,
			IntStatIndex = -1,
			FloatStatIndex = -1;

		switch ( Type )
		{
		case ST_MAP:
			{
				if ( ReverseMap<ST_MAP>(i, LeaderboardIndex, LeaderboardSlot, DataName, NameIndex, IntStatIndex, FloatStatIndex) )
				{
					if ( DataName != NAME_None && StatsCache(i).IntValue != 0 )
					{
						Maps->AddItem(DataName);
					}
				}
				break;
			}
		case ST_CHAR:
			{
				if ( ReverseMap<ST_CHAR>(i, LeaderboardIndex, LeaderboardSlot, DataName, NameIndex, IntStatIndex, FloatStatIndex) )
				{
					if ( DataName != NAME_None && StatsCache(i).IntValue != 0 )
					{
						Characters->AddItem(DataName);
					}
				}
				break;
			}
		case ST_WEAPON:
			{
				if ( ReverseMap<ST_WEAPON>(i, LeaderboardIndex, LeaderboardSlot, DataName, NameIndex, IntStatIndex, FloatStatIndex) )
				{
					if ( DataName != NAME_None && StatsCache(i).IntValue != 0 )
					{
						Weapons->AddItem(DataName);
					}
				}
				break;
			}
		case ST_COMBO:
			{
				if ( ReverseMap<ST_COMBO>(i, LeaderboardIndex, LeaderboardSlot, DataName, NameIndex, IntStatIndex, FloatStatIndex) )
				{
					if ( DataName != NAME_None && StatsCache(i).IntValue != 0 )
					{
						Combos->AddItem(DataName);
					}
				}
				break;
			}
		}
	}

	*(UBOOL*)Result = 1;

	unguardexec;
}

// =======================================================================================================================================================
// =======================================================================================================================================================
// UStatsQuery
// =======================================================================================================================================================
// =======================================================================================================================================================
void UStatsQuery::AddGeneralStats()
{
	guard(UStatsQuery::AddGeneralStats);

#ifdef _XBOX
	check(QuerySpecs.Num() > 0);
	FPlayerQuerySpec& Item = QuerySpecs.Last();

	if ( Item.DestPlayerStat == NULL )
	{
		debugf(TEXT("AddGeneralStats: NULL PlayerStatsCache object!"));
		return;
	}

	TArray<XONLINE_STAT> Stats;

	const INT NumIntStats = INT_STAT_MAX;
	for (INT StatIndex = 0; StatIndex < NumIntStats; StatIndex++)
	{
		XONLINE_STAT Stat;
		Stat.wID = 1 + StatIndex;
		Stat.type = XONLINE_STAT_LONG;
		new(Stats) XONLINE_STAT(Stat);
	}

	Item.StatSpecCount++;

	XONLINE_STAT_SPEC StatSpec;
	StatSpec.xuidUser = *(XUID*)Item.DestPlayerStat->User.XUID;
	StatSpec.dwLeaderBoardID = 1;
	StatSpec.dwNumStats = NumIntStats;
	StatSpec.pStats = Stats.Release();

	new(PendingStatSpecs) XONLINE_STAT_SPEC(StatSpec);
#endif

	unguard;
}

void UStatsQuery::AddMap(FName MapName)
{
	guard(UStatsQuery::AddMap);

#ifdef _XBOX
	INT MapIndex = UPlayerStats::GetMapIndexByName(MapName);
	if (MapIndex != -1)
	{
		AddEntry<ST_MAP>(MapIndex);
	}
#endif

	unguard;
}
void UStatsQuery::AddCharacter(FName CharacterName)
{
	guard(UStatsQuery::AddCharacter);

#ifdef _XBOX
	INT CharIndex = UPlayerStats::GetCharIndexByName(CharacterName);
	if (CharIndex != -1)
	{
		AddEntry<ST_CHAR>(CharIndex);
	}
#endif

	unguard;
}

void UStatsQuery::AddWeapon(FName WeaponName)
{
	guard(UStatsQuery::AddWeapon);

#ifdef _XBOX
	INT WeaponIndex = UPlayerStats::GetWeaponIndexByName(WeaponName);
	if (WeaponIndex != -1)
	{
		AddEntry<ST_WEAPON>(WeaponIndex);
	}
#endif

	unguard;
}

void UStatsQuery::AddCombo(FName ComboName)
{
	guard(UStatsQuery::AddCombo);

#ifdef _XBOX
	INT ComboIndex = UPlayerStats::GetComboIndexByName(ComboName);
	if (ComboIndex != -1)
	{
		check(QuerySpecs.Num() > 0);
		FPlayerQuerySpec& Item = QuerySpecs.Last();

		if ( Item.DestPlayerStat == NULL )
		{
			debugf(TEXT("AddCombo: NULL PlayerStatsCache object!"));
			return;
		}

		TArray<XONLINE_STAT> Stats;

		XONLINE_STAT Stat;
		Stat.wID = 1 + ComboIndex + TStatTypeTraits<ST_COMBO>::FirstStatIndex;
		Stat.type = XONLINE_STAT_LONG;
		new(Stats) XONLINE_STAT(Stat);

		Item.StatSpecCount++;

        XONLINE_STAT_SPEC StatSpec;
		StatSpec.xuidUser = *(XUID*)Item.DestPlayerStat->User.XUID;
		StatSpec.dwLeaderBoardID = 1;
		StatSpec.dwNumStats = Stats.Num();
		StatSpec.pStats = Stats.Release();

		new(PendingStatSpecs) XONLINE_STAT_SPEC(StatSpec);
	}
#endif

	unguard;
}

template<EStatType StatType>
void UStatsQuery::AddEntry(INT EntryIndex)
{
	guard(UStatsQuery::Add);

#ifdef _XBOX
	FPlayerQuerySpec& Item = QuerySpecs.Last();
	if ( Item.DestPlayerStat == NULL )
	{
		debugf(TEXT("AddEntry (%i): NULL DestPlayerStat specified!"), EntryIndex);
		return;
	}

	TArray<XONLINE_STAT> Stats;

	const INT NumIntStats = TStatTypeTraits<StatType>::NumIntStatsPerEntry;
	for (INT StatIndex = 0; StatIndex < NumIntStats; StatIndex++)
	{
		XONLINE_STAT Stat;
		INT CacheIndex = UPlayerStats::GetCacheIndex<StatType>(EntryIndex, StatIndex, SVT_INT);
		Stat.wID = 1 + CacheIndex % UPlayerStats::NumFieldsPerTable;
		Stat.type = XONLINE_STAT_LONG;
		new(Stats) XONLINE_STAT(Stat);
	}

	const INT NumFloatStats = TStatTypeTraits<StatType>::NumFloatStatsPerEntry;
	for (INT StatIndex = 0; StatIndex < NumFloatStats; StatIndex++)
	{
		XONLINE_STAT Stat;
		INT CacheIndex = UPlayerStats::GetCacheIndex<StatType>(EntryIndex, StatIndex, SVT_FLOAT);
		Stat.wID = 1 + CacheIndex % UPlayerStats::NumFieldsPerTable;
		Stat.type = XONLINE_STAT_DOUBLE;
		new(Stats) XONLINE_STAT(Stat);
	}

	Item.StatSpecCount++;

	XONLINE_STAT_SPEC StatSpec;
	StatSpec.xuidUser = *(XUID*)Item.DestPlayerStat->User.XUID;
	StatSpec.dwLeaderBoardID = 1 + UPlayerStats::GetCacheIndex<StatType>(EntryIndex, 0, SVT_INT) / UPlayerStats::NumFieldsPerTable;
	StatSpec.dwNumStats = NumIntStats + NumFloatStats;
	StatSpec.pStats = Stats.Release();


	new(PendingStatSpecs) XONLINE_STAT_SPEC(StatSpec);
#endif

	unguard;
}

UBOOL UStatsQuery::HasPendingData()
{
	guard(UStatsQuery::HasPendingData);

	return PendingStatSpecs.Num() > 0;

	unguard;
}

void UStatsQuery::Submit()
{
	guard(UStatsQuery::Submit);

#ifdef _XBOX
	if (!bQueryInProgress && HasPendingData())
	{
		TArray<XONLINE_STAT_SPEC> StatSpecs;
		TArray<INT> Leaderboards;
		QWORD CurrentUserID = 0;

		while (PendingStatSpecs.Num() > 0)
		{
			XONLINE_STAT_SPEC StatSpec = PendingStatSpecs(0);

			// If this is a stat spec for a new user, reset the array of leaderboards encountered so far.
			if (StatSpec.xuidUser.qwUserID != CurrentUserID)
			{
				CurrentUserID = StatSpec.xuidUser.qwUserID;
				Leaderboards.Empty();
			}

			// If this stat spec involves a new leaderboard and we cannot fit more leaderboards into this batch
			if (Leaderboards.FindItemIndex(StatSpec.dwLeaderBoardID) == -1 &&
				Leaderboards.Num() == MaxLeaderboardsPerQuery)
			{
				debugf(NAME_DevRon,TEXT("UStatsQuery::Submit bailing because hit max leaderboards for query."));

				for ( INT i = 0; i < PendingStatSpecs.Num(); i++ )
				{
					XONLINE_STAT_SPEC& Spec = PendingStatSpecs(i);
					debugf(NAME_DevRon,TEXT("   Pending stat spec waiting %i (0x%016I64X - LeaderboardID:%d  NumStats:%d)"), i, Spec.xuidUser.qwUserID, Spec.dwLeaderBoardID, Spec.dwNumStats);
				}
				break;
			}

			Leaderboards.AddUniqueItem(StatSpec.dwLeaderBoardID);
			PendingStatSpecs.Remove(0, 1);
			new(StatSpecs) XONLINE_STAT_SPEC(StatSpec);
		}

		bQueryInProgress = TRUE;

		XboxLiveManager* Manager = XboxLiveManager::getXboxLiveManager();
		if (Manager != NULL)
		{
			INT NumStatSpecs = StatSpecs.Num();

			for ( INT i = 0; i < StatSpecs.Num(); i++ )
			{
				XONLINE_STAT_SPEC& Spec = StatSpecs(i);

				debugf(NAME_DevRon,TEXT("Beginning stats query for spec %i (0x%016I64X - LeaderboardID:%d  NumStats:%d)"), i, Spec.xuidUser.qwUserID, Spec.dwLeaderBoardID, Spec.dwNumStats);
			}

			Manager->StartStatsQuery(NumStatSpecs, StatSpecs.Release(), this);
		}
	}
#endif

	unguard;
}

void UStatsQuery::Destroy()
{
	guard(UStatsQuery::Destroy);

#ifdef _XBOX
	
	XboxLiveManager* Manager = XboxLiveManager::getXboxLiveManager();
	if (Manager != NULL)
		Manager->KillStatsQuery();

#endif

	Super::Destroy();

	unguard;
}

/*==========================================================================================================
function	UStatsQuery::QueryCompleted
Desc:		Called when all PlayerStatCache objects have received their results.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UStatsQuery::QueryCompleted( UBOOL bIsError )
{
    guard(UStatsQuery::QueryCompleted);

	bQueryInProgress = FALSE;

	if ( !HasPendingData() )
	{
		if ( DELEGATE_IS_SET(OnQueryCompleted) )
			delegateOnQueryCompleted(this, bIsError);
	}
    unguard;
}


IMPLEMENT_FUNCTION(UStatsQuery, -1, execAddGeneralStats);
void UStatsQuery::execAddGeneralStats(FFrame& Stack, RESULT_DECL)
{
	guard(UStatsQuery::execAddGeneralStats);

	P_FINISH;

	AddGeneralStats();

	unguardexec;
}

IMPLEMENT_FUNCTION(UStatsQuery, -1, execAddMap);
void UStatsQuery::execAddMap(FFrame& Stack, RESULT_DECL)
{
	guard(UStatsQuery::execAddMap);

	P_GET_NAME(MapName);
	P_FINISH;

	AddMap(MapName);

	unguardexec;
}

IMPLEMENT_FUNCTION(UStatsQuery, -1, execAddCharacter);
void UStatsQuery::execAddCharacter(FFrame& Stack, RESULT_DECL)
{
	guard(UStatsQuery::execAddCharacter);

	P_GET_NAME(CharacterName);
	P_FINISH;

	AddCharacter(CharacterName);

	unguardexec;
}

IMPLEMENT_FUNCTION(UStatsQuery, -1, execAddWeapon);
void UStatsQuery::execAddWeapon(FFrame& Stack, RESULT_DECL)
{
	guard(UStatsQuery::execAddWeapon);

	P_GET_NAME(WeaponName);
	P_FINISH;

	AddWeapon(WeaponName);

	unguardexec;
}

IMPLEMENT_FUNCTION(UStatsQuery, -1, execAddCombo);
void UStatsQuery::execAddCombo(FFrame& Stack, RESULT_DECL)
{
	guard(UStatsQuery::execAddCombo);

	P_GET_NAME(ComboName);
	P_FINISH;

	AddCombo(ComboName);

	unguardexec;
}

IMPLEMENT_FUNCTION(UStatsQuery, -1, execSubmit);
void UStatsQuery::execSubmit(FFrame& Stack, RESULT_DECL)
{
	guard(UStatsQuery::execSubmit);

	P_FINISH;

	Submit();
	
	unguardexec;
}

IMPLEMENT_FUNCTION(UStatsQuery, -1, execHasPendingData);
void UStatsQuery::execHasPendingData(FFrame& Stack, RESULT_DECL)
{
	guard(UStatsQuery::execHasPendingData);

	P_FINISH;

	*(UBOOL *) Result = HasPendingData();

	unguardexec;
}


#ifdef _XBOX
void UStatsWrite::Write(UPlayerStats* PlayerStats, UBOOL bSinglePlayer)
{
	guard(UStatsWrite::Write);

	check(PlayerStats);

	if (GStatsWritingEnabled)
	{
		// Only write the stats if we are signed into Xbox Live.
		UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
		if (UIX != NULL && UIX->SignedIn())
		{
			XUID PlayerXUID;
			appMemcpy( &PlayerXUID, PlayerStats->User.XUID, sizeof(XUID) );

			debugf(NAME_DevLive, TEXT("Posting stats for user '%s' (0x%016I64X)"), *PlayerStats->User.GamerTag, PlayerXUID.qwUserID);

			if (bSinglePlayer)
			{
				WriteRungs(PlayerXUID, PlayerStats);
			}
			else
			{
				WriteGeneralStats(PlayerXUID, PlayerStats);
				WriteMaps(PlayerXUID, PlayerStats);
				WriteCharacters(PlayerXUID, PlayerStats);
				WriteWeapons(PlayerXUID, PlayerStats);
				WriteCombos(PlayerXUID, PlayerStats);
			}
		}
	}

	unguard;
}

void UStatsWrite::WriteMaps(XUID PlayerXUID, UPlayerStats* PlayerStats)
{
	guard(UStatsWrite::WriteMaps);

	for (INT MapIndex = 0; MapIndex < PlayerStats->MapNames.Num(); MapIndex++)
	{
		FName MapName = PlayerStats->MapNames(MapIndex);

		// For each integer statistic
		const INT NumIntStats = TStatTypeTraits<ST_MAP>::NumIntStatsPerEntry;
		for (INT StatIndex = 0; StatIndex < NumIntStats; StatIndex++)
		{
			INT CacheIndex = UPlayerStats::GetCacheIndex<ST_MAP>(MapIndex, StatIndex, SVT_INT);
			INT LeaderboardIndex = CacheIndex / UPlayerStats::NumFieldsPerTable;
			INT LeaderboardOffset = CacheIndex % UPlayerStats::NumFieldsPerTable;

			// Find the update method for the statistic.
			EUpdateMethod UpdateMethod = TStatTypeTraits<ST_MAP>::IntStatsUpdateMethods[StatIndex];
			// Get the value of the statistic.
			LONG Value = PlayerStats->GetIntStatByMap(MapName, StatIndex);
			// If the statistic has been previously set
			if (Value != 0)
			{
				XONLINE_STAT_UPDATE StatUpdate;
				StatUpdate.xuid = PlayerXUID;
				StatUpdate.dwLeaderBoardID = 1 + LeaderboardIndex;

				XONLINE_STAT_PROC StatProc;

				switch (UpdateMethod)
				{
				case UPDATE_ADD:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_INCREMENT;
					StatUpdate.dwConditionalIndex = 0;
					break;

				case UPDATE_MIN:
				case UPDATE_MAX:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_REPLACE;
					StatUpdate.dwConditionalIndex = StatProcs.Num() + 1;

					XONLINE_STAT CompareStat;
					CompareStat.wID = 1 + LeaderboardOffset;
					CompareStat.type = XONLINE_STAT_LONG;
					CompareStat.lValue = Value;

					XONLINE_STAT_CONDITIONAL StatConditional;
					StatConditional.xuid = PlayerXUID;
					StatConditional.dwLeaderBoardID = 1 + LeaderboardIndex;
					StatConditional.StatToCompare = CompareStat;
					StatConditional.bComparisonType = (UpdateMethod == UPDATE_MIN) ? XONLINE_STAT_COMPTYPE_GREATER : XONLINE_STAT_COMPTYPE_LESS;

					XONLINE_STAT_PROC ConditionalStatProc;
					ConditionalStatProc.wProcedureID = XONLINE_STAT_PROCID_CONDITIONAL;
					ConditionalStatProc.Conditional = StatConditional;

					new(StatProcs) XONLINE_STAT_PROC(ConditionalStatProc);
					break;

				case UPDATE_REPLACE:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_REPLACE;
					StatUpdate.dwConditionalIndex = 0;
					break;

				default:
					// It should never get this far.
					check(FALSE);
					break;
				}

				StatUpdate.dwNumStats = 1;
				StatUpdate.pStats = new XONLINE_STAT[1];
				StatUpdate.pStats[0].wID = 1 + LeaderboardOffset;
				StatUpdate.pStats[0].type = XONLINE_STAT_LONG;
				StatUpdate.pStats[0].lValue = Value;

				StatProc.Update = StatUpdate;

				new(StatProcs) XONLINE_STAT_PROC(StatProc);
			}
		}

		// For each floating-point statistic
		const INT NumFloatStats = TStatTypeTraits<ST_MAP>::NumFloatStatsPerEntry;
		for (INT StatIndex = 0; StatIndex < NumFloatStats; StatIndex++)
		{
			INT CacheIndex = UPlayerStats::GetCacheIndex<ST_MAP>(MapIndex, StatIndex, SVT_FLOAT);
			INT LeaderboardIndex = CacheIndex / UPlayerStats::NumFieldsPerTable;
			INT LeaderboardOffset = CacheIndex % UPlayerStats::NumFieldsPerTable;

			// Find the update method for the statistic.
			EUpdateMethod UpdateMethod = TStatTypeTraits<ST_MAP>::FloatStatsUpdateMethods[StatIndex];
			// Get the value of the statistic.
			FLOAT Value = PlayerStats->GetFloatStatByMap(MapName, StatIndex);
			// If the statistic has been previously set
			if (Value != 0.0f)
			{
				XONLINE_STAT_UPDATE StatUpdate;
				StatUpdate.xuid = PlayerXUID;
				StatUpdate.dwLeaderBoardID = 1 + LeaderboardIndex;

				XONLINE_STAT_PROC StatProc;

				switch (UpdateMethod)
				{
				case UPDATE_ADD:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_INCREMENT;
					StatUpdate.dwConditionalIndex = 0;
					break;

				case UPDATE_MIN:
				case UPDATE_MAX:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_REPLACE;
					StatUpdate.dwConditionalIndex = StatProcs.Num() + 1;

					XONLINE_STAT CompareStat;
					CompareStat.wID = 1 + LeaderboardOffset;
					CompareStat.type = XONLINE_STAT_DOUBLE;
					CompareStat.dValue = (DOUBLE) Value;

					XONLINE_STAT_CONDITIONAL StatConditional;
					StatConditional.xuid = PlayerXUID;
					StatConditional.dwLeaderBoardID = 1 + LeaderboardIndex;
					StatConditional.StatToCompare = CompareStat;
					StatConditional.bComparisonType = (UpdateMethod == UPDATE_MIN) ? XONLINE_STAT_COMPTYPE_GREATER : XONLINE_STAT_COMPTYPE_LESS;

					XONLINE_STAT_PROC ConditionalStatProc;
					ConditionalStatProc.wProcedureID = XONLINE_STAT_PROCID_CONDITIONAL;
					ConditionalStatProc.Conditional = StatConditional;

					new(StatProcs) XONLINE_STAT_PROC(ConditionalStatProc);
					break;

				case UPDATE_REPLACE:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_REPLACE;
					StatUpdate.dwConditionalIndex = 0;
					break;

				default:
					// It should never get this far.
					check(FALSE);
					break;
				}

				StatUpdate.dwNumStats = 1;
				StatUpdate.pStats = new XONLINE_STAT[1];
				StatUpdate.pStats[0].wID = 1 + LeaderboardOffset;
				StatUpdate.pStats[0].type = XONLINE_STAT_DOUBLE;
				StatUpdate.pStats[0].dValue = (DOUBLE) Value;

				StatProc.Update = StatUpdate;

				new(StatProcs) XONLINE_STAT_PROC(StatProc);
			}
		}
	}

	unguard;
}

void UStatsWrite::WriteWeapons(XUID PlayerXUID, UPlayerStats* PlayerStats)
{
	guard(UStatsWrite::WriteWeapons);

	for (INT WeaponIndex = 0; WeaponIndex < PlayerStats->WeaponNames.Num(); WeaponIndex++)
	{
		FName WeaponName = PlayerStats->WeaponNames(WeaponIndex);

		// For each integer statistic
		const INT NumIntStats = TStatTypeTraits<ST_WEAPON>::NumIntStatsPerEntry;
		for (INT StatIndex = 0; StatIndex < NumIntStats; StatIndex++)
		{
			INT CacheIndex = UPlayerStats::GetCacheIndex<ST_WEAPON>(WeaponIndex, StatIndex, SVT_INT);
			INT LeaderboardIndex = CacheIndex / UPlayerStats::NumFieldsPerTable;
			INT LeaderboardOffset = CacheIndex % UPlayerStats::NumFieldsPerTable;

			// Find the update method for the statistic.
			EUpdateMethod UpdateMethod = TStatTypeTraits<ST_WEAPON>::IntStatsUpdateMethods[StatIndex];
			// Get the value of the statistic.
			LONG Value = PlayerStats->GetIntStatByWeapon(WeaponName, StatIndex);
			// If the statistic has been previously set
			if (Value != 0)
			{
				XONLINE_STAT_UPDATE StatUpdate;
				StatUpdate.xuid = PlayerXUID;
				StatUpdate.dwLeaderBoardID = 1 + LeaderboardIndex;

				XONLINE_STAT_PROC StatProc;

				switch (UpdateMethod)
				{
				case UPDATE_ADD:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_INCREMENT;
					StatUpdate.dwConditionalIndex = 0;
					break;

				case UPDATE_MIN:
				case UPDATE_MAX:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_REPLACE;
					StatUpdate.dwConditionalIndex = StatProcs.Num() + 1;

					XONLINE_STAT CompareStat;
					CompareStat.wID = 1 + LeaderboardOffset;
					CompareStat.type = XONLINE_STAT_LONG;
					CompareStat.lValue = Value;

					XONLINE_STAT_CONDITIONAL StatConditional;
					StatConditional.xuid = PlayerXUID;
					StatConditional.dwLeaderBoardID = 1 + LeaderboardIndex;
					StatConditional.StatToCompare = CompareStat;
					StatConditional.bComparisonType = (UpdateMethod == UPDATE_MIN) ? XONLINE_STAT_COMPTYPE_GREATER : XONLINE_STAT_COMPTYPE_LESS;

					XONLINE_STAT_PROC ConditionalStatProc;
					ConditionalStatProc.wProcedureID = XONLINE_STAT_PROCID_CONDITIONAL;
					ConditionalStatProc.Conditional = StatConditional;

					new(StatProcs) XONLINE_STAT_PROC(ConditionalStatProc);
					break;

				case UPDATE_REPLACE:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_REPLACE;
					StatUpdate.dwConditionalIndex = 0;
					break;

				default:
					// It should never get this far.
					check(FALSE);
					break;
				}

				StatUpdate.dwNumStats = 1;
				StatUpdate.pStats = new XONLINE_STAT[1];
				StatUpdate.pStats[0].wID = 1 + LeaderboardOffset;
				StatUpdate.pStats[0].type = XONLINE_STAT_LONG;
				StatUpdate.pStats[0].lValue = Value;

				StatProc.Update = StatUpdate;

				new(StatProcs) XONLINE_STAT_PROC(StatProc);
			}
		}

		// For each floating-point statistic
		const INT NumFloatStats = TStatTypeTraits<ST_WEAPON>::NumFloatStatsPerEntry;
		for (INT StatIndex = 0; StatIndex < NumFloatStats; StatIndex++)
		{
			INT CacheIndex = UPlayerStats::GetCacheIndex<ST_WEAPON>(WeaponIndex, StatIndex, SVT_FLOAT);
			INT LeaderboardIndex = CacheIndex / UPlayerStats::NumFieldsPerTable;
			INT LeaderboardOffset = CacheIndex % UPlayerStats::NumFieldsPerTable;

			// Find the update method for the statistic.
			EUpdateMethod UpdateMethod = TStatTypeTraits<ST_WEAPON>::FloatStatsUpdateMethods[StatIndex];
			// Get the value of the statistic.
			FLOAT Value = PlayerStats->GetFloatStatByWeapon(WeaponName, StatIndex);
			// If the statistic has been previously set
			if (Value != 0.0f)
			{
				XONLINE_STAT_UPDATE StatUpdate;
				StatUpdate.xuid = PlayerXUID;
				StatUpdate.dwLeaderBoardID = 1 + LeaderboardIndex;

				XONLINE_STAT_PROC StatProc;

				switch (UpdateMethod)
				{
				case UPDATE_ADD:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_INCREMENT;
					StatUpdate.dwConditionalIndex = 0;
					break;

				case UPDATE_MIN:
				case UPDATE_MAX:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_REPLACE;
					StatUpdate.dwConditionalIndex = StatProcs.Num() + 1;

					XONLINE_STAT CompareStat;
					CompareStat.wID = 1 + LeaderboardOffset;
					CompareStat.type = XONLINE_STAT_DOUBLE;
					CompareStat.dValue = (DOUBLE) Value;

					XONLINE_STAT_CONDITIONAL StatConditional;
					StatConditional.xuid = PlayerXUID;
					StatConditional.dwLeaderBoardID = 1 + LeaderboardIndex;
					StatConditional.StatToCompare = CompareStat;
					StatConditional.bComparisonType = (UpdateMethod == UPDATE_MIN) ? XONLINE_STAT_COMPTYPE_GREATER : XONLINE_STAT_COMPTYPE_LESS;

					XONLINE_STAT_PROC ConditionalStatProc;
					ConditionalStatProc.wProcedureID = XONLINE_STAT_PROCID_CONDITIONAL;
					ConditionalStatProc.Conditional = StatConditional;

					new(StatProcs) XONLINE_STAT_PROC(ConditionalStatProc);
					break;

				case UPDATE_REPLACE:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_REPLACE;
					StatUpdate.dwConditionalIndex = 0;
					break;

				default:
					// It should never get this far.
					check(FALSE);
					break;
				}

				StatUpdate.dwNumStats = 1;
				StatUpdate.pStats = new XONLINE_STAT[1];
				StatUpdate.pStats[0].wID = 1 + LeaderboardOffset;
				StatUpdate.pStats[0].type = XONLINE_STAT_DOUBLE;
				StatUpdate.pStats[0].dValue = (DOUBLE) Value;

				StatProc.Update = StatUpdate;

				new(StatProcs) XONLINE_STAT_PROC(StatProc);
			}
		}
	}

	unguard;
}

void UStatsWrite::WriteCombos(XUID PlayerXUID, UPlayerStats* PlayerStats)
{
	guard(UStatsWrite::WriteCombos);

	for (INT ComboIndex = 0; ComboIndex < PlayerStats->ComboNames.Num(); ComboIndex++)
	{
		FName ComboName = PlayerStats->ComboNames(ComboIndex);

		// For each integer statistic
		const INT NumIntStats = TStatTypeTraits<ST_COMBO>::NumIntStatsPerEntry;
		for (INT StatIndex = 0; StatIndex < NumIntStats; StatIndex++)
		{
			INT LeaderboardIndex = 0;
			INT LeaderboardOffset = ComboIndex + TStatTypeTraits<ST_COMBO>::FirstStatIndex;

			// Find the update method for the statistic.
			EUpdateMethod UpdateMethod = TStatTypeTraits<ST_COMBO>::IntStatsUpdateMethods[StatIndex];
			// Get the value of the statistic.
			LONG Value = PlayerStats->GetIntStatByCombo(ComboName, StatIndex);
			// If the statistic has been previously set
			if (Value != 0)
			{
				XONLINE_STAT_UPDATE StatUpdate;
				StatUpdate.xuid = PlayerXUID;
				StatUpdate.dwLeaderBoardID = 1 + LeaderboardIndex;

				XONLINE_STAT_PROC StatProc;

				switch (UpdateMethod)
				{
				case UPDATE_ADD:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_INCREMENT;
					StatUpdate.dwConditionalIndex = 0;
					break;

				case UPDATE_MIN:
				case UPDATE_MAX:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_REPLACE;
					StatUpdate.dwConditionalIndex = StatProcs.Num() + 1;

					XONLINE_STAT CompareStat;
					CompareStat.wID = 1 + LeaderboardOffset;
					CompareStat.type = XONLINE_STAT_LONG;
					CompareStat.lValue = Value;

					XONLINE_STAT_CONDITIONAL StatConditional;
					StatConditional.xuid = PlayerXUID;
					StatConditional.dwLeaderBoardID = 1 + LeaderboardIndex;
					StatConditional.StatToCompare = CompareStat;
					StatConditional.bComparisonType = (UpdateMethod == UPDATE_MIN) ? XONLINE_STAT_COMPTYPE_GREATER : XONLINE_STAT_COMPTYPE_LESS;

					XONLINE_STAT_PROC ConditionalStatProc;
					ConditionalStatProc.wProcedureID = XONLINE_STAT_PROCID_CONDITIONAL;
					ConditionalStatProc.Conditional = StatConditional;

					new(StatProcs) XONLINE_STAT_PROC(ConditionalStatProc);
					break;

				case UPDATE_REPLACE:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_REPLACE;
					StatUpdate.dwConditionalIndex = 0;
					break;

				default:
					// It should never get this far.
					check(FALSE);
					break;
				}

				StatUpdate.dwNumStats = 1;
				StatUpdate.pStats = new XONLINE_STAT[1];
				StatUpdate.pStats[0].wID = 1 + LeaderboardOffset;
				StatUpdate.pStats[0].type = XONLINE_STAT_LONG;
				StatUpdate.pStats[0].lValue = Value;

				StatProc.Update = StatUpdate;

				new(StatProcs) XONLINE_STAT_PROC(StatProc);
			}
		}
	}

	unguard;
}

void UStatsWrite::WriteCharacters(XUID PlayerXUID, UPlayerStats* PlayerStats)
{
	guard(UStatsWrite::WriteCharacters);

	for (INT CharacterIndex = 0; CharacterIndex < PlayerStats->CharNames.Num(); CharacterIndex++)
	{
		FName CharacterName = PlayerStats->CharNames(CharacterIndex);

		// For each integer statistic
		const INT NumIntStats = TStatTypeTraits<ST_CHAR>::NumIntStatsPerEntry;
		for (INT StatIndex = 0; StatIndex < NumIntStats; StatIndex++)
		{
			INT CacheIndex = UPlayerStats::GetCacheIndex<ST_CHAR>(CharacterIndex, StatIndex, SVT_INT);
			INT LeaderboardIndex = CacheIndex / UPlayerStats::NumFieldsPerTable;
			INT LeaderboardOffset = CacheIndex % UPlayerStats::NumFieldsPerTable;

			// Find the update method for the statistic.
			EUpdateMethod UpdateMethod = TStatTypeTraits<ST_CHAR>::IntStatsUpdateMethods[StatIndex];
			// Get the value of the statistic.
			LONG Value = PlayerStats->GetIntStatByCharacter(CharacterName, StatIndex);
			// If the statistic has been previously set
			if (Value != 0)
			{
				XONLINE_STAT_UPDATE StatUpdate;
				StatUpdate.xuid = PlayerXUID;
				StatUpdate.dwLeaderBoardID = 1 + LeaderboardIndex;

				XONLINE_STAT_PROC StatProc;

				switch (UpdateMethod)
				{
				case UPDATE_ADD:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_INCREMENT;
					StatUpdate.dwConditionalIndex = 0;
					break;

				case UPDATE_MIN:
				case UPDATE_MAX:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_REPLACE;
					StatUpdate.dwConditionalIndex = StatProcs.Num() + 1;

					XONLINE_STAT CompareStat;
					CompareStat.wID = 1 + LeaderboardOffset;
					CompareStat.type = XONLINE_STAT_LONG;
					CompareStat.lValue = Value;

					XONLINE_STAT_CONDITIONAL StatConditional;
					StatConditional.xuid = PlayerXUID;
					StatConditional.dwLeaderBoardID = 1 + LeaderboardIndex;
					StatConditional.StatToCompare = CompareStat;
					StatConditional.bComparisonType = (UpdateMethod == UPDATE_MIN) ? XONLINE_STAT_COMPTYPE_GREATER : XONLINE_STAT_COMPTYPE_LESS;

					XONLINE_STAT_PROC ConditionalStatProc;
					ConditionalStatProc.wProcedureID = XONLINE_STAT_PROCID_CONDITIONAL;
					ConditionalStatProc.Conditional = StatConditional;

					new(StatProcs) XONLINE_STAT_PROC(ConditionalStatProc);
					break;

				case UPDATE_REPLACE:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_REPLACE;
					StatUpdate.dwConditionalIndex = 0;
					break;

				default:
					// It should never get this far.
					check(FALSE);
					break;
				}

				StatUpdate.dwNumStats = 1;
				StatUpdate.pStats = new XONLINE_STAT[1];
				StatUpdate.pStats[0].wID = 1 + LeaderboardOffset;
				StatUpdate.pStats[0].type = XONLINE_STAT_LONG;
				StatUpdate.pStats[0].lValue = Value;

				StatProc.Update = StatUpdate;

				new(StatProcs) XONLINE_STAT_PROC(StatProc);
			}
		}

		// For each floating-point statistic
		const INT NumFloatStats = TStatTypeTraits<ST_CHAR>::NumFloatStatsPerEntry;
		for (INT StatIndex = 0; StatIndex < NumFloatStats; StatIndex++)
		{
			INT CacheIndex = UPlayerStats::GetCacheIndex<ST_CHAR>(CharacterIndex, StatIndex, SVT_FLOAT);
			INT LeaderboardIndex = CacheIndex / UPlayerStats::NumFieldsPerTable;
			INT LeaderboardOffset = CacheIndex % UPlayerStats::NumFieldsPerTable;

			// Find the update method for the statistic.
			EUpdateMethod UpdateMethod = TStatTypeTraits<ST_CHAR>::FloatStatsUpdateMethods[StatIndex];
			// Get the value of the statistic.
			FLOAT Value = PlayerStats->GetFloatStatByCharacter(CharacterName, StatIndex);
			// If the statistic has been previously set
			if (Value != 0.0f)
			{
				XONLINE_STAT_UPDATE StatUpdate;
				StatUpdate.xuid = PlayerXUID;
				StatUpdate.dwLeaderBoardID = 1 + LeaderboardIndex;

				XONLINE_STAT_PROC StatProc;

				switch (UpdateMethod)
				{
				case UPDATE_ADD:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_INCREMENT;
					StatUpdate.dwConditionalIndex = 0;
					break;

				case UPDATE_MIN:
				case UPDATE_MAX:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_REPLACE;
					StatUpdate.dwConditionalIndex = StatProcs.Num() + 1;

					XONLINE_STAT CompareStat;
					CompareStat.wID = 1 + LeaderboardOffset;
					CompareStat.type = XONLINE_STAT_DOUBLE;
					CompareStat.dValue = (DOUBLE) Value;

					XONLINE_STAT_CONDITIONAL StatConditional;
					StatConditional.xuid = PlayerXUID;
					StatConditional.dwLeaderBoardID = 1 + LeaderboardIndex;
					StatConditional.StatToCompare = CompareStat;
					StatConditional.bComparisonType = (UpdateMethod == UPDATE_MIN) ? XONLINE_STAT_COMPTYPE_GREATER : XONLINE_STAT_COMPTYPE_LESS;

					XONLINE_STAT_PROC ConditionalStatProc;
					ConditionalStatProc.wProcedureID = XONLINE_STAT_PROCID_CONDITIONAL;
					ConditionalStatProc.Conditional = StatConditional;

					new(StatProcs) XONLINE_STAT_PROC(ConditionalStatProc);
					break;

				case UPDATE_REPLACE:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_REPLACE;
					StatUpdate.dwConditionalIndex = 0;
					break;

				default:
					// It should never get this far.
					check(FALSE);
					break;
				}

				StatUpdate.dwNumStats = 1;
				StatUpdate.pStats = new XONLINE_STAT[1];
				StatUpdate.pStats[0].wID = 1 + LeaderboardOffset;
				StatUpdate.pStats[0].type = XONLINE_STAT_DOUBLE;
				StatUpdate.pStats[0].dValue = (DOUBLE) Value;

				StatProc.Update = StatUpdate;

				new(StatProcs) XONLINE_STAT_PROC(StatProc);
			}
		}
	}

	unguard;
}

void UStatsWrite::WriteRungs(XUID PlayerXUID, UPlayerStats* PlayerStats)
{
	guard(UStatsWrite::WriteRungs);

	for (INT RungIndex = 0; RungIndex < PlayerStats->RungNames.Num(); RungIndex++)
	{
		FName RungName = PlayerStats->RungNames(RungIndex);
		INT LeaderboardIndex = UPlayerStats::GetCacheIndex<ST_RUNG>(RungIndex, 0, SVT_INT) / UPlayerStats::NumFieldsPerTable;

		FLOAT BestTime = PlayerStats->GetFloatStatByRung(RungName, RUNG_STAT_FLOAT_FASTEST_WIN);
		if (BestTime != 0.0f)
		{
			XONLINE_STAT_UPDATE StatUpdate;
			StatUpdate.xuid = PlayerXUID;
			StatUpdate.dwLeaderBoardID = 1 + LeaderboardIndex;
			StatUpdate.dwConditionalIndex = StatProcs.Num() + 1;

			ULONGLONG NewRating = 1000000 - BestTime;

			XONLINE_STAT CompareStat;
			CompareStat.wID = XONLINE_STAT_RATING;
			CompareStat.type = XONLINE_STAT_LONGLONG;
			CompareStat.llValue = NewRating;

			XONLINE_STAT_CONDITIONAL StatConditional;
			StatConditional.xuid = PlayerXUID;
			StatConditional.dwLeaderBoardID = 1 + LeaderboardIndex;
			StatConditional.StatToCompare = CompareStat;
			StatConditional.bComparisonType = XONLINE_STAT_COMPTYPE_LESS;

			XONLINE_STAT_PROC ConditionalStatProc;
			ConditionalStatProc.wProcedureID = XONLINE_STAT_PROCID_CONDITIONAL;
			ConditionalStatProc.Conditional = StatConditional;

			new(StatProcs) XONLINE_STAT_PROC(ConditionalStatProc);

			StatUpdate.dwNumStats = 1;
			StatUpdate.pStats = new XONLINE_STAT[1];
			StatUpdate.pStats[0].wID = XONLINE_STAT_RATING;
			StatUpdate.pStats[0].type = XONLINE_STAT_LONGLONG;
			StatUpdate.pStats[0].llValue = NewRating;

			XONLINE_STAT_PROC StatProc;
			StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_REPLACE;
			StatProc.Update = StatUpdate;

			new(StatProcs) XONLINE_STAT_PROC(StatProc);
		}

		// For each integer statistic
		const INT NumIntRungStats = RUNG_STAT_INT_MAX;
		for (INT StatIndex = 0; StatIndex < NumIntRungStats; StatIndex++)
		{
			// Find the update method for the statistic.
			EUpdateMethod UpdateMethod = TStatTypeTraits<ST_RUNG>::IntStatsUpdateMethods[StatIndex];
			// Get the value of the statistic.
			LONG Value = PlayerStats->GetIntStatByRung(RungName, StatIndex);
			// If the statistic has been previously set
			if (Value != 0)
			{
				XONLINE_STAT_UPDATE StatUpdate;
				StatUpdate.xuid = PlayerXUID;
				StatUpdate.dwLeaderBoardID = 1 + LeaderboardIndex;

				XONLINE_STAT_PROC StatProc;

				switch (UpdateMethod)
				{
				case UPDATE_ADD:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_INCREMENT;
					StatUpdate.dwConditionalIndex = 0;
					break;

				case UPDATE_MIN:
				case UPDATE_MAX:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_REPLACE;
					StatUpdate.dwConditionalIndex = StatProcs.Num() + 1;

					XONLINE_STAT CompareStat;
					CompareStat.wID = StatIndex + 1;
					CompareStat.type = XONLINE_STAT_LONG;
					CompareStat.lValue = Value;

					XONLINE_STAT_CONDITIONAL StatConditional;
					StatConditional.xuid = PlayerXUID;
					StatConditional.dwLeaderBoardID = 1 + LeaderboardIndex;
					StatConditional.StatToCompare = CompareStat;
					StatConditional.bComparisonType = (UpdateMethod == UPDATE_MIN) ? XONLINE_STAT_COMPTYPE_GREATER : XONLINE_STAT_COMPTYPE_LESS;

					XONLINE_STAT_PROC ConditionalStatProc;
					ConditionalStatProc.wProcedureID = XONLINE_STAT_PROCID_CONDITIONAL;
					ConditionalStatProc.Conditional = StatConditional;

					new(StatProcs) XONLINE_STAT_PROC(ConditionalStatProc);
					break;

				case UPDATE_REPLACE:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_REPLACE;
					StatUpdate.dwConditionalIndex = 0;
					break;

				default:
					// It should never get this far.
					check(FALSE);
					break;
				}

				StatUpdate.dwNumStats = 1;
				StatUpdate.pStats = new XONLINE_STAT[1];
				StatUpdate.pStats[0].wID = StatIndex + 1;
				StatUpdate.pStats[0].type = XONLINE_STAT_LONG;
				StatUpdate.pStats[0].lValue = Value;

				StatProc.Update = StatUpdate;

				new(StatProcs) XONLINE_STAT_PROC(StatProc);
			}
		}

		// For each floating-point statistic
		const INT NumFloatRungStats = RUNG_STAT_FLOAT_MAX;
		for (INT StatIndex = 0; StatIndex < NumFloatRungStats; StatIndex++)
		{
			// Find the update method for the statistic.
			EUpdateMethod UpdateMethod = TStatTypeTraits<ST_RUNG>::FloatStatsUpdateMethods[StatIndex];
			// Get the value of the statistic.
			FLOAT Value = PlayerStats->GetFloatStatByRung(RungName, StatIndex);
			// If the statistic has been previously set
			if (Value != 0.0f)
			{
				XONLINE_STAT_UPDATE StatUpdate;
				StatUpdate.xuid = PlayerXUID;
				StatUpdate.dwLeaderBoardID = 1 + LeaderboardIndex;

				XONLINE_STAT_PROC StatProc;

				switch (UpdateMethod)
				{
				case UPDATE_ADD:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_INCREMENT;
					StatUpdate.dwConditionalIndex = 0;
					break;

				case UPDATE_MIN:
				case UPDATE_MAX:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_REPLACE;
					StatUpdate.dwConditionalIndex = StatProcs.Num() + 1;

					XONLINE_STAT CompareStat;
					CompareStat.wID = StatIndex + NumIntRungStats + 1;
					CompareStat.type = XONLINE_STAT_DOUBLE;
					CompareStat.dValue = (DOUBLE) Value;

					XONLINE_STAT_CONDITIONAL StatConditional;
					StatConditional.xuid = PlayerXUID;
					StatConditional.dwLeaderBoardID = 1 + LeaderboardIndex;
					StatConditional.StatToCompare = CompareStat;
					StatConditional.bComparisonType = (UpdateMethod == UPDATE_MIN) ? XONLINE_STAT_COMPTYPE_GREATER : XONLINE_STAT_COMPTYPE_LESS;

					XONLINE_STAT_PROC ConditionalStatProc;
					ConditionalStatProc.wProcedureID = XONLINE_STAT_PROCID_CONDITIONAL;
					ConditionalStatProc.Conditional = StatConditional;

					new(StatProcs) XONLINE_STAT_PROC(ConditionalStatProc);
					break;

				case UPDATE_REPLACE:
					StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_REPLACE;
					StatUpdate.dwConditionalIndex = 0;
					break;

				default:
					// It should never get this far.
					check(FALSE);
					break;
				}

				StatUpdate.dwNumStats = 1;
				StatUpdate.pStats = new XONLINE_STAT[1];
				StatUpdate.pStats[0].wID = StatIndex + NumIntRungStats + 1;
				StatUpdate.pStats[0].type = XONLINE_STAT_DOUBLE;
				StatUpdate.pStats[0].dValue = (DOUBLE) Value;

				StatProc.Update = StatUpdate;

				new(StatProcs) XONLINE_STAT_PROC(StatProc);
			}
		}
	}

	unguard;
}

void UStatsWrite::WriteGeneralStats(XUID PlayerXUID, UPlayerStats* PlayerStats)
{
	guard(UStatsWrite::WriteGeneralStats);

	// For each integer statistic
	const INT NumIntStats = INT_STAT_MAX;
	for (INT StatIndex = 0; StatIndex < NumIntStats; StatIndex++)
	{
		INT LeaderboardIndex = 0;
		INT LeaderboardOffset = StatIndex;

		// Find the update method for the statistic.
		EUpdateMethod UpdateMethod = IntStatsUpdateMethods[StatIndex];
		// Get the value of the statistic.
		LONG Value = PlayerStats->GetIntStat(StatIndex);
		// If the statistic has been previously set
		if (Value != 0)
		{
			XONLINE_STAT_UPDATE StatUpdate;
			StatUpdate.xuid = PlayerXUID;
			StatUpdate.dwLeaderBoardID = 1 + LeaderboardIndex;

			XONLINE_STAT_PROC StatProc;

			switch (UpdateMethod)
			{
			case UPDATE_ADD:
				StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_INCREMENT;
				StatUpdate.dwConditionalIndex = 0;
				break;

			case UPDATE_MIN:
			case UPDATE_MAX:
				StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_REPLACE;
				StatUpdate.dwConditionalIndex = StatProcs.Num() + 1;

				XONLINE_STAT CompareStat;
				CompareStat.wID = 1 + LeaderboardOffset;
				CompareStat.type = XONLINE_STAT_LONG;
				CompareStat.lValue = Value;

				XONLINE_STAT_CONDITIONAL StatConditional;
				StatConditional.xuid = PlayerXUID;
				StatConditional.dwLeaderBoardID = 1 + LeaderboardIndex;
				StatConditional.StatToCompare = CompareStat;
				StatConditional.bComparisonType = (UpdateMethod == UPDATE_MIN) ? XONLINE_STAT_COMPTYPE_GREATER : XONLINE_STAT_COMPTYPE_LESS;

				XONLINE_STAT_PROC ConditionalStatProc;
				ConditionalStatProc.wProcedureID = XONLINE_STAT_PROCID_CONDITIONAL;
				ConditionalStatProc.Conditional = StatConditional;

				new(StatProcs) XONLINE_STAT_PROC(ConditionalStatProc);
				break;

			case UPDATE_REPLACE:
				StatProc.wProcedureID = XONLINE_STAT_PROCID_UPDATE_REPLACE;
				StatUpdate.dwConditionalIndex = 0;
				break;

			default:
				// It should never get this far.
				check(FALSE);
				break;
			}

			StatUpdate.dwNumStats = 1;
			StatUpdate.pStats = new XONLINE_STAT[1];
			StatUpdate.pStats[0].wID = 1 + LeaderboardOffset;
			StatUpdate.pStats[0].type = XONLINE_STAT_LONG;
			StatUpdate.pStats[0].lValue = Value;

			StatProc.Update = StatUpdate;

			new(StatProcs) XONLINE_STAT_PROC(StatProc);
		}
	}

	unguard;
}

void UStatsWrite::Submit()
{
	guard(UStatsWrite::Submit);

	if (GStatsWritingEnabled)
	{
		XboxLiveManager* Manager = XboxLiveManager::getXboxLiveManager();
		if (Manager != NULL)
		{
			INT NumStatProcs = StatProcs.Num();
			Manager->StartStatsWrite(NumStatProcs, StatProcs.Release());
		}
	}

	unguard;
}

void UStatsWrite::Destroy()
{
	guard(UStatsWrite::Destroy);
//
//	XboxLiveManager* Manager = XboxLiveManager::getXboxLiveManager();
//	if ( Manager != NULL )
//	{
//		Manager->KillStatsWrite();
//	}
//
	Super::Destroy();

	unguard;

}

#endif // _XBOX

IMPLEMENT_FUNCTION(UStatsWrite, -1, execSubmit);
void UStatsWrite::execSubmit(FFrame& Stack, RESULT_DECL)
{
	guard(UStatsWrite::execSubmit);

	P_FINISH;

#ifdef _XBOX
	Submit();
#endif

	unguard;
}

IMPLEMENT_FUNCTION(UStatsWrite, -1, execAddUser);
void UStatsWrite::execAddUser(FFrame& Stack, RESULT_DECL)
{
	guard(UStatsWrite::execAddUser);

	P_GET_OBJECT(UPlayerStats, PlayerStats);
	P_GET_UBOOL(bSinglePlayer);
	P_FINISH;

#ifdef _XBOX
	Write(PlayerStats, bSinglePlayer);
#endif

	unguard;
}
