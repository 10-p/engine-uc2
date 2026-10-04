/*=============================================================================
	UnrealUtil.h: Unreal utility package public header file.
	Copyright 1997-2004 Epic Games, Inc. All Rights Reserved.

Revision History:
	* Created by Ron Prestenback.
=============================================================================*/
#include "Engine.h"
#include "FConfigCacheIni.h"

#ifndef UNREALUTIL_API
#define UNREALUTIL_API DLL_IMPORT
#endif

#ifndef __UNREALUTIL_H__
#define __UNREALUTIL_H__

//extern "C" { void RegisterNamesUnrealUtil(void); }

class FClassTree
{
	UClass* Base;
	FClassTree* Parent;
	TArray<FClassTree*> Children;

	INT AddChild( FClassTree* NewChild )
	{
		check(NewChild);

		NewChild->Parent = this;
		return Children.AddUniqueItem(NewChild);
	}

	void Replace( UClass* Intermediate, FClassTree* CurrentChild )
	{
		FClassTree* NewChild = new FClassTree( Intermediate );
		INT OldIndex = Children.FindItemIndex(CurrentChild);
		Children.Remove(OldIndex);

		INT NewIndex = AddChild(NewChild);
		Children(NewIndex)->AddChild(CurrentChild);
	}


public:
	FClassTree( UClass* BaseClass )
	: Base(BaseClass), Parent(NULL)
	{
		check(Base);
	}

	~FClassTree()
	{
		Base = NULL;

		for ( INT i = 0; i < Children.Num(); i++ )
		{
			FClassTree* p = Children(i);
			if ( p )
                delete p;
		}

		Children.Empty();
	}

	UBOOL AddClass( UClass* ChildClass )
	{
		check(ChildClass);

		if ( ChildClass == Base )
			return 0;

		if ( !ChildClass->IsChildOf(Base) )
		{
			if ( Parent && ChildClass == Base->GetSuperClass() && ChildClass != Parent->GetClass() )
			{
				Parent->Replace(ChildClass,this);
				return 1;
			}

			return 0;
		}

		for ( INT i = 0; i < Children.Num(); i++ )
		{
			if ( Children(i)->AddClass(ChildClass) )
				return 1;
		}

		FClassTree* NewChild = new FClassTree( ChildClass );
		AddChild(NewChild);
		return 1;
	}

	UClass* GetClass() const
	{
		return Base;
	}

	INT Num() const
	{
		INT Count = 1;
		for ( INT i = 0; i < Children.Num(); i++ )
			Count += Children(i)->Num();

		return Count;
	}

	void GetChildClasses( TArray<FClassTree*>& ChildClasses ) const
	{
		ChildClasses.Empty();
		for ( INT i = 0; i < Children.Num(); i++ )
			ChildClasses.AddItem( Children(i));
	}

	INT FindChildIndex( UClass* Cls ) const
	{
		for ( INT i = 0; i < Children.Num(); i++ )
			if ( Children(i)->GetClass() == Cls )
				return i;

		return INDEX_NONE;
	}

	const FClassTree* FindNode( UClass* Cls ) const
	{
		guard(FClassTree::FindNode);

		const FClassTree* Result(NULL);

		if ( Cls == Base )
			Result = this;
		else if ( Cls->IsChildOf(Base) )
		{
			for ( INT i = 0; i < Children.Num(); i++ )
			{
				Result = Children(i)->FindNode(Cls);
				if ( Result )
					break;
			}
		}

		return Result;

		unguard;
	}
};

struct FPerObjectConfigList
{
	FString			ClassName;
	TArray<FName>	ObjectNames;

	FPerObjectConfigList( const FString& SectionName, FConfigSection& Section )
	{
		ClassName = SectionName;

		TArray<FString> ReverseObjectNames;

		// Get the list of object names we need to create
		Section.MultiFind(TEXT("Names"), ReverseObjectNames);

		// check if any of the names were pointers to another file
		for ( INT i = 0; i < ReverseObjectNames.Num(); i++ )
		{
			FString& CurName = ReverseObjectNames(i);
			// would be something like file:blah.ini
			if ( CurName.Left(5) == TEXT("file:") )
			{
				// strip off the 'file:' part
				FString FileName = CurName.Mid(5);
				ReverseObjectNames.Remove(i--);

				// find the file specified
				FConfigFile* File = ((FConfigCacheIni*)GConfig)->Find(*FileName,0);
				if ( File )
				{
					// add all the section names found in this file to our list
					// of objectnames
					INT CurrentIndex = ReverseObjectNames.Num();
					for ( FConfigFile::TIterator It(*File); It; ++It )
					{
						ReverseObjectNames.InsertZeroed(CurrentIndex);
						ReverseObjectNames(CurrentIndex) = It.Key();
					}
				}
			}
		}

		ObjectNames.AddZeroed(ReverseObjectNames.Num());
		for ( INT RevIndex = ReverseObjectNames.Num() - 1, Index = 0; RevIndex >= 0; RevIndex--, Index++ )
		{
			ObjectNames(Index) = FName(*ReverseObjectNames(RevIndex));
		}
	}
};



#define BEGIN_COMMANDLET(ClassName) \
class UNREALUTIL_API U##ClassName##Commandlet : public UCommandlet \
{ \
	DECLARE_CLASS(U##ClassName##Commandlet,UCommandlet,CLASS_Transient,UnrealUtil); \
	virtual void StaticInitialize() { } \

#define BEGIN_CHILD_COMMANDLET(BaseClass,ClassName) \
class UNREALUTIL_API U##ClassName##Commandlet : public U##BaseClass##Commandlet \
{ \
	DECLARE_CLASS(U##ClassName##Commandlet,U##BaseClass##Commandlet,CLASS_Transient,UnrealUtil); \


#define END_COMMANDLET \
	void StaticConstructor() \
	{ \
		LogToStdout     = 0; \
		IsClient        = 1; \
		IsEditor        = 1; \
		IsServer        = 1; \
		LazyLoad        = 0; \
		ShowErrorCount  = 1; \
		StaticInitialize(); \
	} \
	INT Main(const TCHAR* Parms); \
}; \

struct UNREALUTIL_API FComboRecord
{
	UClass* ComboClass;
	FString ComboName, IconName, ComboInfo;
	FLOAT ComboCost;

	FComboRecord( UClass* InComboClass, const TCHAR* InComboName, const TCHAR* InIconName, const TCHAR* InComboInfo, FLOAT InComboCost )
		: ComboCost(InComboCost), ComboClass(InComboClass)
	{
		if ( InComboName )
			ComboName = InComboName;
		else ComboName = TEXT("");

		if ( InIconName )
			IconName = InIconName;
		else IconName = TEXT("");

		if ( InComboInfo )
			ComboInfo = InComboInfo;
		else ComboInfo = TEXT("");
	}
};

struct UNREALUTIL_API FWeaponRecord
{
	FString WeaponClassName, WeaponName, IconName;

	FWeaponRecord( const TCHAR* InClassName, const TCHAR* InWeaponName, const TCHAR* InIconName )
	{
		check(InClassName);
		check(InWeaponName);
		check(InIconName);

		WeaponClassName = InClassName;
		WeaponName = InWeaponName;
		IconName = InIconName;
	}
};

#define DECLAREPROP(prop) UProperty* prop = FindField<UProperty>(CharClass,TEXT(#prop)); check(prop);
#define DECLARESTRUCTPROP(prop) UStructProperty* prop = FindField<UStructProperty>(CharClass,TEXT(#prop)); check(prop);
#define DECLAREINNERPROP(propscope,prop) UProperty* prop = FindField<UProperty>(propscope,TEXT(#prop)); check(prop);

struct UNREALUTIL_API FCharacterRecord
{

	FString m_Name, m_AltName, m_ClassName, m_AltClassName;
	FString m_IconName, m_SelectedIconName, m_AltIconName, m_AltSelectedIconName;
	FString m_MovieName, m_AltMovieName;
	FString m_PortraitName, m_AltPortraitName;
	FString m_Bio, m_Hint, m_AltBio, m_AltHint;
	FString m_AnnouncerCue, m_AltAnnouncerCue;

	INT m_MaxHealth, m_Agility;
	BYTE m_RaceWeaponIdx, m_AltRaceWeaponIdx, m_MeleeWeaponIdx, m_AltMeleeWeaponIdx;
	TArray<BYTE> m_Combos, m_AltCombos;

	FCharacterRecord( UStruct* Struct, BYTE* Data, INT CharacterIndex );

	void SetCombos( BYTE* Data, UStructProperty* ComboStruct, TArray<BYTE>& ComboField );
	UBOOL LoadCombos( const TCHAR* SectionName, TArray<FComboRecord>& Records, FStringOutputDevice& Error, UBOOL bAltClass=0 );
	UBOOL SetComboIndexes( TArray<FComboRecord>& Records, FStringOutputDevice& Error );
	UBOOL SetWeaponIndexes( TArray<FWeaponRecord>& RaceWeapons, TArray<FWeaponRecord>& MeleeWeapons, FStringOutputDevice& Error );

	UBOOL LoadRaceWeapons( UClass* CharClass, TArray<FWeaponRecord>& RaceWeapons, FStringOutputDevice& Error, UBOOL bAltVersion );
	UBOOL LoadMeleeWeapons( UClass* CharClass, TArray<FWeaponRecord>& RaceWeapons, FStringOutputDevice& Error, UBOOL bAltVersion );

	void Export( INT CharIndex, FStringOutputDevice& Ar, const TArray<FWeaponRecord>& RaceRecords, const TArray<FWeaponRecord>& MeleeRecords );
};

struct UNREALUTIL_API FGameplayRecord
{
	AGameInfo* GameObj;
	FString ConstName;
	INT GoalScore, TimeLimit, NumberOfBots, MinPlayers, MaxPlayers;

	FGameplayRecord( AGameInfo* Game )
	: GameObj(Game),
	GoalScore(Game->GoalScore),
	TimeLimit(Game->TimeLimit),
	MaxPlayers(Game->MaxPlayers),
	NumberOfBots(0)
	{
		UProperty* MinPlayersProp = FindField<UProperty>(Game->GetClass(),TEXT("MinPlayers"));
		if ( MinPlayersProp )
			MinPlayers = *(INT*)((BYTE*)Game + MinPlayersProp->Offset);

		ConstName = TEXT("GAME_");
		ConstName += GameObj->Acronym;
	}
};

BEGIN_COMMANDLET(CharacterInfo)

	FClassTree* ComboClasses, *RaceWeaponClasses, *MeleeWeaponClasses;
	TArray<FComboRecord> ComboRecords;
	TArray<FWeaponRecord> RaceRecords, MeleeRecords, EnergyRecords, ExplosiveRecords;

	void ExportCombos( FStringOutputDevice& Ar );
	void ExportRaceWeapons( FStringOutputDevice& Ar );
	void ExportMeleeWeapons( FStringOutputDevice& Ar );
	void ExportCharacters( FStringOutputDevice& Ar );

	void ExportComboClass(FClassTree& BaseClass, TArray<FComboRecord>& Records );
	void ExportWeapon( FClassTree& BaseClass, TArray<FWeaponRecord>& Records );
	void ExportWeaponClass( UClass* WeaponClass, TArray<FWeaponRecord>& Records );
	void ExportCustomWeapons( FStringOutputDevice& Ar );


END_COMMANDLET

BEGIN_COMMANDLET(GameConfiguration)
	TArray<FGameplayRecord> Records;
END_COMMANDLET

BEGIN_COMMANDLET(RenameSection)

END_COMMANDLET

BEGIN_COMMANDLET(MapInfo)

END_COMMANDLET

BEGIN_COMMANDLET(VerifyMaplists)
END_COMMANDLET

BEGIN_COMMANDLET(DumpFont)
END_COMMANDLET

BEGIN_COMMANDLET(CompareFonts)
END_COMMANDLET

// GUI Commandlets
BEGIN_COMMANDLET(GUIBase)

	FClassTree* Classes;

	void StaticConstructor()
	{
		LogToStdout     = 0;
		IsClient        = 1;
		IsEditor        = 1;
		IsServer        = 1;
		LazyLoad        = 0;
		ShowErrorCount  = 1;
		StaticInitialize();
	}

	void LoadClassTree();
	void Destroy()
	{
		if ( Classes )
			delete Classes;

		Classes = NULL;

		Super::Destroy();
	}
};

BEGIN_CHILD_COMMANDLET(GUIBase,TestClassTree)
	static INT Indent;
	void LogClasses( FClassTree* Node );
END_COMMANDLET

BEGIN_CHILD_COMMANDLET(GUIBase,VerifyStyles)

	TMap<UClass*,FString> UnusedStyles;
	TMap<UClass*,FString> UnusedFonts;
	TArray<UClass*> UsedStyles;

	void RegisterFont( const FClassTree* Node );
	void RegisterStyle( const FClassTree* Node );

	void PurgeUsedStyles( const FClassTree* Node );
	void PurgeInnerStyles( class UGUIComponent* Comp );

	void PurseUsedFonts();

END_COMMANDLET

BEGIN_COMMANDLET(StatsNames)
	TMultiMap<FName,FName> StatsData;
	INT ExportStatValues( const FName& Type, FStringOutputDevice& Ar );
END_COMMANDLET

BEGIN_COMMANDLET(PawnSkinDLOs)
END_COMMANDLET

BEGIN_COMMANDLET(HandsDLOs)
END_COMMANDLET

BEGIN_COMMANDLET(Weapon1pDLOs)
END_COMMANDLET

BEGIN_COMMANDLET(MapMemoryUsage)
END_COMMANDLET

#endif	// __UNREALUTIL_H__
