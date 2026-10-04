/*=============================================================================
	UnContentExportCommandlets.cpp: UC2004 content data exporter.
	Copyright 1997-2004 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Ron Prestenback.
=============================================================================*/

#include "UnrealUtil.h"
#include "UCGameplay.h"

static INT Compare( FGameplayRecord& RecordA, FGameplayRecord& RecordB )
{
	UConst	*IndexA = FindField<UConst>(RecordA.GameObj->GetClass(), *RecordA.ConstName),
			*IndexB = FindField<UConst>(RecordB.GameObj->GetClass(), *RecordB.ConstName);

	if ( !IndexA || !IndexB )
		return 0;

	return appAtoi(*IndexA->Value) - appAtoi(*IndexB->Value);
}

static INT Compare( FString& A, FString& B )
{
	return appStrcmp(*A,*B);
}

static INT Compare( FComboRecord& FirstRecord, FComboRecord& SecondRecord )
{
	INT ComboResult = appStrcmp(*FirstRecord.ComboName, *SecondRecord.ComboName);
	if ( ComboResult == 0 )
		ComboResult = appStrcmp(FirstRecord.ComboClass->GetName(), SecondRecord.ComboClass->GetName());

	return ComboResult;
}

static INT Compare( FWeaponRecord& A, FWeaponRecord& B )
{
	INT WeaponResult = appStrcmp(*A.WeaponName,*B.WeaponName);
	if ( WeaponResult == 0 )
		WeaponResult = appStrcmp(*A.WeaponClassName,*B.WeaponClassName);

	return WeaponResult;
}

static void StripMapName( FString& MapName )
{
	guard(StripMapName);

	INT pos = MapName.InStr(TEXT(".unr"));
	if ( pos != INDEX_NONE )
		MapName = MapName.Left(pos);

	pos = MapName.InStr(TEXT("-"));
	if ( pos != INDEX_NONE )
		MapName = MapName.Mid(pos+1);

	unguard;
}

// =======================================================================================================================================================
// =======================================================================================================================================================
// UGameConfigurationCommandlet
// =======================================================================================================================================================
// =======================================================================================================================================================

INT UGameConfigurationCommandlet::Main( const TCHAR* Parms )
{
	guard(UGameConfigurationCommandlet::Main);

	GIsRequestingExit = 1; // Causes Ctrl-C to immediately exit

	TArray<FString> Files;

	// Parse the command line

	warnf(TEXT("Loading UCGameplay.u..."));
	UPackage* GameplayPackage = Cast<UPackage>(LoadPackage(NULL, TEXT("UCGameplay.u"),LOAD_NoWarn));
	if ( !GameplayPackage )
	{
		warnf(TEXT("Error loading package!"));
		return 1;
	}


	for ( TObjectIterator<UClass> It; It; ++It )
	{
		if ( It->IsIn(GameplayPackage) && It->IsChildOf(AGameInfo::StaticClass()) )
		{
			AGameInfo* Game = Cast<AGameInfo>(It->GetDefaultObject());
			if ( Game && Game->Acronym.Len() && Game->Acronym != TEXT("???") )
				new(Records) FGameplayRecord(Game);
		}
	}

	FString Output=TEXT("");
	INT i = 0;

	Sort( &Records(0), Records.Num() );
	for ( ; i < Records.Num(); i++ )
	{
		Output += FString::Printf(TEXT("\tGameRules(%s)=(GoalScore=%i,TimeLimit=%i,NumberOfBots=%i,MinPlayers=%i,MaxPlayers=%i)\t// %s\r\n"),
			*Records(i).ConstName, Records(i).GoalScore, Records(i).TimeLimit, Records(i).NumberOfBots, Records(i).MinPlayers, Records(i).MaxPlayers, Records(i).GameObj->GetClass()->GetPathName());
	}
	warnf(TEXT("Exported %i gametypes to GametypeList.txt!"), i);

	GFileManager->Delete(TEXT("GametypeList.txt"), 0, 1);
	appSaveStringToFile( Output, TEXT("GametypeList.txt"), GFileManager );
	return 0;
	unguard;
}

IMPLEMENT_CLASS(UGameConfigurationCommandlet);


// =======================================================================================================================================================
// =======================================================================================================================================================
// UMapInfoCommandlet
// =======================================================================================================================================================
// =======================================================================================================================================================
INT UMapInfoCommandlet::Main( const TCHAR* Parms )
{
	guard(UMapInfoCommandlet::Main);

	GIsRequestingExit = 1;
	TArray<FString> Maps;

	warnf(TEXT("Loading Engine.u..."));
	LoadPackage(NULL,TEXT("Engine.u"),LOAD_NoFail);

	UClass* CharClass = LoadClass<UObject>(NULL, TEXT("Engine.CharacterInformation"), NULL, 0, NULL);
	check(CharClass);

	UProperty* PrefixProp = FindField<UProperty>(CharClass, TEXT("GameTypePrefix"));
	check(PrefixProp);

	BYTE* Data = (BYTE*)CharClass->GetDefaultObject();

	// Build a list of acronyms
	TArray<FString> Acronyms;
	for ( INT i = 0; i < PrefixProp->ArrayDim; i++ )
	{
		FString& Prefix = *(FString*)(Data + PrefixProp->Offset + i * PrefixProp->ElementSize);
		INT j;
		for ( j = 0; j < Acronyms.Num(); j++ )
			if ( Acronyms(j) == Prefix )
				break;

		if ( j == Acronyms.Num() )
			new(Acronyms) FString(*Prefix);
	}

	// Find all valid maps
	TCHAR Wildcard[20];
	for ( INT i = 0; i < Acronyms.Num(); i++ )
	{
		appSprintf(Wildcard, TEXT("../Maps/%s-*.unr"), *Acronyms(i));
		TArray<FString> TheseMaps = GFileManager->FindFiles(Wildcard, 1, 0);

		for ( INT j = TheseMaps.Num() - 1; j >= 0; j-- )
		{
			StripMapName(TheseMaps(j));
			for ( INT k = 0; k < Maps.Num(); k++ )
				if ( Maps(k) == TheseMaps(j) )
					TheseMaps.Remove(j);
		}

		Maps += TheseMaps;
	}

	TCHAR PortraitName[128];

	warnf(TEXT("Loading T_Portrait.utx..."));
	UPackage* Portraits = Cast<UPackage>(LoadPackage(NULL,TEXT("T_Portrait.utx"),LOAD_NoFail));


	FStringOutputDevice Output;
	INT Count=0;

	if ( Maps.Num() )
		Sort(&Maps(0),Maps.Num());

	Output.Logf(TEXT("//====================================================================================\r\n"));
	Output.Logf(TEXT("//\tMAP DATA - ucc mapinfo\r\n"));

	for ( INT i = 0; i < Maps.Num(); i++ )
	{
		appSprintf(PortraitName, TEXT("Arena.UI_Arena_%s"), *Maps(i));
		UMaterial* Icon = Cast<UMaterial>(FindObject<UMaterial>(Portraits, PortraitName));
		if ( Icon != NULL )
			Output.Logf(TEXT("\tMapRecords(%i)=(MapName=\"%s\",MaterialName=\"T_Portrait.%s\")\r\n"), Count++, *Maps(i), Icon->GetName());
	}

	warnf(TEXT("Exported %i map records to MapRecordlist.txt!"), Count-1);
	GFileManager->Delete(TEXT("MapRecordlist.txt"), 0, 1);
	appSaveStringToFile( Output, TEXT("MapRecordlist.txt"), GFileManager );

	return 0;

	unguard;
}
IMPLEMENT_CLASS(UMapInfoCommandlet);

// =======================================================================================================================================================
// =======================================================================================================================================================
// UVerifyMaplistsCommandlet
// =======================================================================================================================================================
// =======================================================================================================================================================
INT UVerifyMaplistsCommandlet::Main( const TCHAR* Parms )
{
	guard(UMapInfoCommandlet::Main);

	GIsRequestingExit = 1;
	TArray<FString> Maps;

	warnf(TEXT("Loading Engine.u..."));
	LoadPackage(NULL,TEXT("Engine.u"),LOAD_NoFail);

	UClass* CharClass = LoadClass<UObject>(NULL, TEXT("Engine.CharacterInformation"), NULL, 0, NULL);
	check(CharClass);

	UProperty* ClassProp = FindField<UProperty>(CharClass, TEXT("GameTypeClassName"));
	check(ClassProp);

	BYTE* Data = (BYTE*)CharClass->GetDefaultObject();

	// Build a list of acronyms & prefixes
	TArray<FString> Maplists, Prefixes;
	for ( INT i = 0; i < ClassProp->ArrayDim; i++ )
	{
		FString& ClassName = *(FString*)(Data + ClassProp->Offset + ClassProp->ElementSize * i);
		warnf(TEXT("Loading %s..."), *ClassName);
		UClass* GameClass = LoadClass<AGameInfo>(NULL,*ClassName,NULL,0,NULL);

		AGameInfo* Game = CastChecked<AGameInfo>(GameClass->GetDefaultObject());

		new(Maplists) FString(*(Game->Acronym + TEXT("_Full")));
		new(Prefixes) FString(*Game->MapPrefix);
	}

	// Find all valid maps
	TCHAR Wildcard[20];
	for ( INT i = 0; i < Maplists.Num(); i++ )
	{
		TMultiMap<FName,FString>* Sec = GConfig->GetSectionPrivate(*Maplists(i), 0, 1, TEXT("Maplists.ini"));
		if ( Sec == NULL )
		{
			warnf(TEXT("No section found for maplist %s"), *Maplists(i));
			continue;
		}

		// Get the maps for this prefix
		appSprintf(Wildcard, TEXT("../Maps/%s-*.unr"), *Prefixes(i));
		TArray<FString> TheseMaps = GFileManager->FindFiles(Wildcard, 1, 0);

		TArray<FString> CurrentMaps;
		Sec->MultiFind(FName(TEXT("Maps")), CurrentMaps);

		for ( INT j = TheseMaps.Num() - 1; j >= 0; j-- )
		{
			FString Mapname = TheseMaps(j).LeftChop(4);
			INT k;
			for ( k = CurrentMaps.Num() - 1; k >= 0; k-- )
			{
				if ( CurrentMaps(k) == Mapname )
				{
					TheseMaps.Remove(j,1);
					CurrentMaps.Remove(k,1);
					break;
				}
			}
		}

		if ( TheseMaps.Num() )
		{
			warnf(TEXT("\r\nExisting maps missing from %s section of Maplists.ini:"), *Maplists(i));
			for ( INT j = 0; j < TheseMaps.Num(); j++ )
				warnf(TEXT("   %i) %s"), j, *TheseMaps(j));
		}

		if ( CurrentMaps.Num() )
		{
			warnf(TEXT("\r\nMaps listed in %s section of Maplists.ini that no longer exist:"), *Maplists(i));
			for ( INT j = 0; j < CurrentMaps.Num(); j++ )
				warnf(TEXT("   %i) %s"), j, *CurrentMaps(j));
		}
	}

	return 0;

	unguard;
}
IMPLEMENT_CLASS(UVerifyMaplistsCommandlet);


/*-----------------------------------------------------------------------------
	UCharacterInfoCommandlet.
-----------------------------------------------------------------------------*/
INT UCharacterInfoCommandlet::Main( const TCHAR* Parms )
{
	guard(UCharacterInfoCommandlet::Main);

	GIsRequestingExit = 1; // Causes Ctrl-C to immediately exit

	// Parse the command line

	warnf(TEXT("Loading Engine.u..."));
	LoadPackage(NULL, TEXT("Engine.u"), LOAD_NoFail);

	warnf(TEXT("Loading UCCharacters.u..."));
	LoadPackage(NULL, TEXT("UCCharacters.u"),LOAD_NoFail);

	warnf(TEXT("Loading UCGameplay.u..."));
	LoadPackage(NULL, TEXT("UCGameplay.u"),LOAD_NoFail);

	warnf(TEXT("Loading UCInventory.u..."));
	LoadPackage(NULL, TEXT("UCInventory.u"),LOAD_NoFail);

	UClass* RaceWeaponClass = LoadObject<UClass>(NULL,TEXT("UCGameplay.UCWeap_Race"),NULL,0,NULL);
	UClass* MeleeWeaponClass = LoadObject<UClass>(NULL,TEXT("UCGameplay.UCWeap_Melee"),NULL,0,NULL);
	if ( !RaceWeaponClass ) 
	{
		warnf(TEXT("Failed to load UCWeapRace!!"));
		return 1;
	}

	if ( !MeleeWeaponClass )
	{
		warnf(TEXT("Failed to load UCWeapMelee!!"));
		return 1;
	}

	ComboClasses = new FClassTree(AUCCombo::StaticClass());
	RaceWeaponClasses = new FClassTree(RaceWeaponClass);
	MeleeWeaponClasses = new FClassTree(MeleeWeaponClass);

	TArray<FString> ComboNames;
	GConfig->GetArray(TEXT("Engine.PlayerStats"), TEXT("ComboNames"), ComboNames, TEXT("Stats.ini"));

	for ( TObjectIterator<UClass> It; It; ++It )
	{
		if ( It->IsChildOf(AUCCombo::StaticClass()) )
		{
			if ( ComboNames.FindItemIndex(It->GetName()) != INDEX_NONE )
				ComboClasses->AddClass(*It);
			else if ( !appStricmp(It->GetName(), TEXT("UCCombo_Speed_Light")) )
				ComboClasses->AddClass(*It);
			
			continue;	
		}
		if ( RaceWeaponClasses->AddClass(*It) )
			continue;

		MeleeWeaponClasses->AddClass(*It);
	}

	FStringOutputDevice Ar;

	ExportCombos(Ar);
	
	Ar.Log(TEXT("\r\n"));
	ExportCustomWeapons(Ar);

	Ar.Log(TEXT("\r\n"));
	ExportRaceWeapons(Ar);

	Ar.Log(TEXT("\r\n"));
	ExportMeleeWeapons(Ar);

	Ar.Log(TEXT("\r\n"));
	ExportCharacters(Ar);

	GFileManager->Delete(TEXT("CharacterInfo.txt"), 0, 1);
	appSaveStringToFile( Ar, TEXT("CharacterInfo.txt"), GFileManager );

	delete ComboClasses;
	delete RaceWeaponClasses;
	delete MeleeWeaponClasses;

	ComboClasses = NULL;
	RaceWeaponClasses = NULL;
	MeleeWeaponClasses = NULL;

	return 0;

	unguard;
}

void UCharacterInfoCommandlet::ExportCombos( FStringOutputDevice& Ar )
{
	TArray<FClassTree*> SubClasses;
	ComboClasses->GetChildClasses(SubClasses);

	for ( INT j = 0; j < SubClasses.Num(); j++ )
		ExportComboClass( *SubClasses(j), ComboRecords );

	Sort( &ComboRecords(0), ComboRecords.Num() );
	INT i;
	for ( i = 0; i < ComboRecords.Num(); i++ )
	{
		if ( i == 0 )
		{
			Ar.Logf(TEXT("//====================================================================================\r\n"));
			Ar.Logf(TEXT("//\tCOMBO DATA - ucc characterinfo\r\n"));
		}
		Ar.Logf(TEXT("//------------------------------------------------------------------------------------\r\n"));

		Ar.Logf(TEXT("\tComboName(%i)=\"%s\"\r\n"), i, *ComboRecords(i).ComboName);
		Ar.Logf(TEXT("\tComboClasses(%i)=\"%s\"\r\n"), i, ComboRecords(i).ComboClass->GetPathName());
		Ar.Logf(TEXT("\tComboCost(%i)=%.2f\r\n"),  i, ComboRecords(i).ComboCost);
		Ar.Logf(TEXT("\tComboIcon(%i)=%s\r\n"), i, *ComboRecords(i).IconName);

		if ( ComboRecords(i).ComboInfo.Len() )
            Ar.Logf(TEXT("\tComboInfo(%i)=\"%s\"\r\n"), i, *ComboRecords(i).ComboInfo);
		else Ar.Logf(TEXT("\tComboInfo(%i)=\"\"\r\n"), i);
	}
	warnf(TEXT("Exported %i combos to CharacterInfo.txt!"), i);
	Ar.Logf(TEXT("\r\n"));
}

void UCharacterInfoCommandlet::ExportCharacters( FStringOutputDevice& Ar )
{
	guard(UCharacterInfoCommandlet::ExportCharacters);

	UClass* CharClass = LoadClass<UObject>(NULL, TEXT("Engine.CharacterInformation"), NULL, 0, NULL);
	check(CharClass);
	BYTE* Data = (BYTE*)CharClass->GetDefaultObject();


	TArray<FCharacterRecord> Records;
	
	DECLARESTRUCTPROP(Characters);
	for ( INT j = 0; j < Characters->ArrayDim; j++ )
		new(Records) FCharacterRecord(Characters->Struct, Data + Characters->Offset + j * Characters->ElementSize, j);

	FStringOutputDevice Error;
	for ( INT j = 0; j < Records.Num(); j++ )
		Records(j).SetComboIndexes(ComboRecords, Error);

	for ( INT j = 0; j < Records.Num(); j++ )
		Records(j).SetWeaponIndexes(RaceRecords, MeleeRecords, Error);

	if ( Error.Len() )
		warnf(*Error);

	INT i;
	for ( i = 0; i < Records.Num(); i++ )
	{
		if ( i == 0 )
		{
			Ar.Logf(TEXT("//====================================================================================\r\n"));
			Ar.Logf(TEXT("//\tCHARACTERS - ucc characterinfo\r\n"));
		}
		Ar.Logf(TEXT("//------------------------------------------------------------------------------------\r\n"));

		Records(i).Export( i, Ar, RaceRecords, MeleeRecords );
	}

	Ar.Logf(TEXT("\r\n"));

	warnf(TEXT("Exported %i characters to CharacterInfo.txt!"), i);
	unguard;
}

void UCharacterInfoCommandlet::ExportCustomWeapons( FStringOutputDevice& Ar )
{
	guard(UCharacterInfoCommandlet::ExportCustomWeapons);

	UClass* CharClass = LoadClass<UObject>(NULL, TEXT("Engine.CharacterInformation"), NULL, 0, NULL);
	check(CharClass);

	BYTE* Data = (BYTE*)CharClass->GetDefaultObject();

	TArray<UClass*> ExplosiveWeaponClasses, EnergyWeaponClasses;

	DECLAREPROP(ExplosiveWeaponClassName);
	DECLAREPROP(EnergyWeaponClassName);

	INT i;

	// first, build the list of explosive & energy classes
	for ( i = 0; i < ExplosiveWeaponClassName->ArrayDim; i++ )
	{
		const FString& WeaponClassName = *(FString*)(Data + ExplosiveWeaponClassName->Offset + i * ExplosiveWeaponClassName->ElementSize);
		if ( WeaponClassName != TEXT("") )
		{
			UClass* Cls = LoadClass<AWeapon>(NULL, *WeaponClassName, NULL, 0, NULL);
			if ( Cls != NULL )
				ExportWeaponClass(Cls, ExplosiveRecords);
		}
	}

	for ( i = 0; i < EnergyWeaponClassName->ArrayDim; i++ )
	{
		const FString& WeaponClassName = *(FString*)(Data + EnergyWeaponClassName->Offset + i * EnergyWeaponClassName->ElementSize);
		if ( WeaponClassName != TEXT("") )
		{
			UClass* Cls = LoadClass<AWeapon>(NULL, *WeaponClassName, NULL, 0, NULL);
			if ( Cls != NULL )
				ExportWeaponClass(Cls, EnergyRecords);
		}
	}

	DECLAREPROP(ExplosiveWeaponInfo);
	DECLAREPROP(ExplosiveWeaponName);
	DECLAREPROP(ExplosiveWeaponHintString);
	DECLAREPROP(ExplosiveWeaponPortraitMaterialName);
	DECLAREPROP(ExplosiveWeaponSelectCue);

	DECLAREPROP(EnergyWeaponName);
	DECLAREPROP(EnergyWeaponInfo);
	DECLAREPROP(EnergyWeaponHintString);
	DECLAREPROP(EnergyWeaponPortraitMaterialName);
	DECLAREPROP(EnergyWeaponSelectCue);

	// now export them to file
	for ( i = 0; i < ExplosiveRecords.Num(); i++ )
	{
		if ( i == 0 )
		{
			Ar.Logf(TEXT("//====================================================================================\r\n"));
			Ar.Logf(TEXT("//\tEXPLOSIVE WEAPONS\r\n"));
		}
		Ar.Logf(TEXT("//------------------------------------------------------------------------------------\r\n"));
		Ar.Logf(TEXT("\tExplosiveWeaponName(%i)=\"%s\"\r\n"), i, *ExplosiveRecords(i).WeaponName);
		Ar.Logf(TEXT("\tExplosiveWeaponClassName(%i)=\"%s\"\r\n"), i, *ExplosiveRecords(i).WeaponClassName);
		INT k;
		for ( k = 0; k <ExplosiveWeaponClassName->ArrayDim; k++ )
		{
			const FString& ClassName = *(FString*)(Data + ExplosiveWeaponClassName->Offset + k * ExplosiveWeaponClassName->ElementSize);
			const FString& WeaponName = *(FString*)(Data + ExplosiveWeaponName->Offset + k * ExplosiveWeaponName->ElementSize);

			if ( ClassName == ExplosiveRecords(i).WeaponClassName || WeaponName == ExplosiveRecords(i).WeaponName )
			{
				const FString& WeaponInfo = *(FString*)(Data + ExplosiveWeaponInfo->Offset + k * ExplosiveWeaponInfo->ElementSize);
				const FString& WeaponHint = *(FString*)(Data + ExplosiveWeaponHintString->Offset + k * ExplosiveWeaponHintString->ElementSize);
				const FString& WeaponPortrait = *(FString*)(Data + ExplosiveWeaponPortraitMaterialName->Offset + k * ExplosiveWeaponPortraitMaterialName->ElementSize);
				const FName& WeaponSelect = *(FName*)(Data + ExplosiveWeaponSelectCue->Offset + k * ExplosiveWeaponSelectCue->ElementSize);

				Ar.Logf(TEXT("\tExplosiveWeaponHintString(%i)=\"%s\"\r\n"), i, *WeaponHint);
				Ar.Logf(TEXT("\tExplosiveWeaponInfo(%i)=\"%s\"\r\n"), i, *WeaponInfo);
				Ar.Logf(TEXT("\tExplosiveWeaponPortraitMaterialName(%i)=\"%s\"\r\n"), i, *WeaponPortrait);
				Ar.Logf(TEXT("\tExplosiveWeaponSelectCue(%i)=%s\r\n"), i, *WeaponSelect);
				break;
			}
		}
		if ( k == ExplosiveWeaponClassName->ArrayDim )
		{
			Ar.Logf(TEXT("\tExplosiveWeaponHintString(%i)=\"\"\r\n"), i);
			Ar.Logf(TEXT("\tExplosiveWeaponInfo(%i)=\"\"\r\n"), i);
			Ar.Logf(TEXT("\tExplosiveWeaponPortraitMaterialName(%i)=\"\"\r\n"), i);
		}
		Ar.Logf(TEXT("\tExplosiveWeaponIcon(%i)=%s\r\n"), i, *ExplosiveRecords(i).IconName);

	}
	Ar.Logf(TEXT("\r\n"));
	warnf(TEXT("Exported %i explosive weapons to CharacterInfo.txt!"), i);

	for ( i = 0; i < EnergyRecords.Num(); i++ )
	{
		if ( i == 0 )
		{
			Ar.Logf(TEXT("//====================================================================================\r\n"));
			Ar.Logf(TEXT("//\tENERGY WEAPONS\r\n"));
		}
		Ar.Logf(TEXT("//------------------------------------------------------------------------------------\r\n"));
		Ar.Logf(TEXT("\tEnergyWeaponName(%i)=\"%s\"\r\n"), i, *EnergyRecords(i).WeaponName);
		Ar.Logf(TEXT("\tEnergyWeaponClassName(%i)=\"%s\"\r\n"), i, *EnergyRecords(i).WeaponClassName);
		INT k;
		for ( k = 0; k <EnergyWeaponClassName->ArrayDim; k++ )
		{
			const FString& ClassName = *(FString*)(Data + EnergyWeaponClassName->Offset + k * EnergyWeaponClassName->ElementSize);
			const FString& WeaponName = *(FString*)(Data + EnergyWeaponName->Offset + k * EnergyWeaponName->ElementSize);
			if ( ClassName == EnergyRecords(i).WeaponClassName || WeaponName == EnergyRecords(i).WeaponName )
			{
				const FString& WeaponInfo = *(FString*)(Data + EnergyWeaponInfo->Offset + k * EnergyWeaponInfo->ElementSize);
				const FString& WeaponHint = *(FString*)(Data + EnergyWeaponHintString->Offset + k * EnergyWeaponHintString->ElementSize);
				const FString& WeaponPortrait = *(FString*)(Data + EnergyWeaponPortraitMaterialName->Offset + k * EnergyWeaponPortraitMaterialName->ElementSize);
				const FName& WeaponSelect = *(FName*)(Data + EnergyWeaponSelectCue->Offset + k * EnergyWeaponSelectCue->ElementSize);

				Ar.Logf(TEXT("\tEnergyWeaponHintString(%i)=\"%s\"\r\n"), i, *WeaponHint);
				Ar.Logf(TEXT("\tEnergyWeaponInfo(%i)=\"%s\"\r\n"), i, *WeaponInfo);
				Ar.Logf(TEXT("\tEnergyWeaponPortraitMaterialName(%i)=\"%s\"\r\n"), i, *WeaponPortrait);
				Ar.Logf(TEXT("\tEnergyWeaponSelectCue(%i)=%s\r\n"), i, *WeaponSelect);
				break;
			}
		}
		if ( k == EnergyWeaponClassName->ArrayDim )
		{
			Ar.Logf(TEXT("\tEnergyWeaponHintString(%i)=\"\"\r\n"), i);
			Ar.Logf(TEXT("\tEnergyWeaponInfo(%i)=\"\"\r\n"), i);
			Ar.Logf(TEXT("\tEnergyWeaponPortraitMaterialName(%i)=\"\"\r\n"), i);
		}
		Ar.Logf(TEXT("\tEnergyWeaponIcon(%i)=%s\r\n"), i, *EnergyRecords(i).IconName);

	}
	Ar.Logf(TEXT("\r\n"));
	warnf(TEXT("Exported %i energy weapons to CharacterInfo.txt!"), i);

	unguard;
}

void UCharacterInfoCommandlet::ExportRaceWeapons( FStringOutputDevice& Ar )
{
	guard(UCharacterInfoCommandlet::ExportRaceWeapons);

	TArray<FClassTree*> SubClasses;
	RaceWeaponClasses->GetChildClasses(SubClasses);

	for ( INT j = 0; j < SubClasses.Num(); j++ )
		ExportWeapon( *SubClasses(j), RaceRecords );

	INT i;

	Sort( &RaceRecords(0), RaceRecords.Num() );
	UClass* CharClass = LoadClass<UObject>(NULL, TEXT("Engine.CharacterInformation"), NULL, 0, NULL);
	check(CharClass);

	BYTE* Data = (BYTE*)CharClass->GetDefaultObject();

	DECLAREPROP(RaceWeaponClassName);
	DECLAREPROP(RaceWeaponInfo);
	DECLAREPROP(RaceWeaponName);
	DECLAREPROP(RaceWeaponHintString);

	for ( i = 0; i < RaceRecords.Num(); i++ )
	{
		if ( i == 0 )
		{
			Ar.Logf(TEXT("//====================================================================================\r\n"));
			Ar.Logf(TEXT("//\tRACE WEAPONS\r\n"));
		}
		Ar.Logf(TEXT("//------------------------------------------------------------------------------------\r\n"));

		Ar.Logf(TEXT("\tRaceWeaponName(%i)=\"%s\"\r\n"), i, *RaceRecords(i).WeaponName);
		Ar.Logf(TEXT("\tRaceWeaponClassName(%i)=\"%s\"\r\n"), i, *RaceRecords(i).WeaponClassName);

		INT k;
		for ( k = 0; k <RaceWeaponClassName->ArrayDim; k++ )
		{
			FString& ClassName = *(FString*)(Data + RaceWeaponClassName->Offset + k * RaceWeaponClassName->ElementSize);

			if ( ClassName == RaceRecords(i).WeaponClassName )
			{
				const FString& WeaponInfo = *(FString*)(Data + RaceWeaponInfo->Offset + k * RaceWeaponInfo->ElementSize);
				const FString& WeaponHint = *(FString*)(Data + RaceWeaponHintString->Offset + k * RaceWeaponHintString->ElementSize);

				Ar.Logf(TEXT("\tRaceWeaponHintString(%i)=\"%s\"\r\n"), i, *WeaponHint);
				Ar.Logf(TEXT("\tRaceWeaponInfo(%i)=\"%s\"\r\n"), i, *WeaponInfo);
				break;
			}
		}

		if ( k == RaceWeaponClassName->ArrayDim )
		{
			Ar.Logf(TEXT("\tRaceWeaponHintString(%i)=\"\"\r\n"), i);
			Ar.Logf(TEXT("\tRaceWeaponInfo(%i)=\"\"\r\n"), i);
		}


		Ar.Logf(TEXT("\tRaceWeaponIcon(%i)=%s\r\n"), i, *RaceRecords(i).IconName);

	}
	Ar.Logf(TEXT("\r\n"));

	warnf(TEXT("Exported %i race weapons to CharacterInfo.txt!"), i);

	unguard;
}

void UCharacterInfoCommandlet::ExportMeleeWeapons( FStringOutputDevice& Ar )
{
	guard(UCharacterInfoCommandlet::ExportMeleeWeapons);

	TArray<FClassTree*> SubClasses;
	
	// Get an array containing all child classes of UCWeapon_Melee
	MeleeWeaponClasses->GetChildClasses(SubClasses);

	for ( INT j = 0; j < SubClasses.Num(); j++ )
		ExportWeapon( *SubClasses(j), MeleeRecords );

	INT i;


	Sort( &MeleeRecords(0), MeleeRecords.Num() );
	UClass* CharClass = LoadClass<UObject>(NULL, TEXT("Engine.CharacterInformation"), NULL, 0, NULL);
	check(CharClass);

	BYTE* Data = (BYTE*)CharClass->GetDefaultObject();

	DECLAREPROP(MeleeWeaponClassName);
	DECLAREPROP(MeleeWeaponInfo);
	DECLAREPROP(MeleeWeaponName);
	DECLAREPROP(MeleeWeaponHintString);

	for ( i = 0; i < MeleeRecords.Num(); i++ )
	{
		if ( i == 0 )
		{
			Ar.Logf(TEXT("//====================================================================================\r\n"));
			Ar.Logf(TEXT("//\tMELEE WEAPONS\r\n"));
		}
		Ar.Logf(TEXT("//------------------------------------------------------------------------------------\r\n"));

		Ar.Logf(TEXT("\tMeleeWeaponName(%i)=\"%s\"\r\n"), i, *MeleeRecords(i).WeaponName);
		Ar.Logf(TEXT("\tMeleeWeaponClassName(%i)=\"%s\"\r\n"), i, *MeleeRecords(i).WeaponClassName);

		INT k;
		for ( k = 0; k <MeleeWeaponClassName->ArrayDim; k++ )
		{
			FString& ClassName = *(FString*)(Data + MeleeWeaponClassName->Offset + k * MeleeWeaponClassName->ElementSize);
			if ( ClassName == MeleeRecords(i).WeaponClassName )
			{
				const FString& WeaponInfo = *(FString*)(Data + MeleeWeaponInfo->Offset + k * MeleeWeaponInfo->ElementSize);
				const FString& WeaponHint = *(FString*)(Data + MeleeWeaponHintString->Offset + k * MeleeWeaponHintString->ElementSize);

				Ar.Logf(TEXT("\tMeleeWeaponHintString(%i)=\"%s\"\r\n"), i, *WeaponHint);
				Ar.Logf(TEXT("\tMeleeWeaponInfo(%i)=\"%s\"\r\n"), i, *WeaponInfo);
				break;
			}
		}

		if ( k == MeleeWeaponClassName->ArrayDim )
		{
			Ar.Logf(TEXT("\tMeleeWeaponHintString(%i)=\"\"\r\n"), i);
			Ar.Logf(TEXT("\tMeleeWeaponInfo(%i)=\"\"\r\n"), i);
		}

		Ar.Logf(TEXT("\tMeleeWeaponIcon(%i)=%s\r\n"), i, *MeleeRecords(i).IconName);

	}
	Ar.Logf(TEXT("\r\n"));

	warnf(TEXT("Exported %i melee weapons to CharacterInfo.txt!"), i);

	unguard;
}

void UCharacterInfoCommandlet::ExportComboClass(FClassTree& BaseClass, TArray<FComboRecord>& Records )
{
	guard(UCharacterExportCommandlet::ExportComboClass);

	UClass* Cls = BaseClass.GetClass();
	AUCCombo* Combo = Cast<AUCCombo>(Cls->GetDefaultObject());

	TCHAR* IconName = appStaticString1024();
	*IconName = 0;

	UMaterial* IconMat = Combo->IconMaterial;
	INT IconU = Combo->IconIndexX * UCONST_COMBO_ICON_SIZE,
		IconV = Combo->IconIndexY * UCONST_COMBO_ICON_SIZE;

	if ( IconMat )
		appSprintf( IconName, TEXT("(ComboIconMaterialName=\"%s\",IconU=%i,IconV=%i)"), IconMat->GetPathName(), IconU, IconV );
	else
		appSprintf( IconName, TEXT("(ComboIconMaterialName=\"\")") );

	INT i;
	for ( i = 0; i < Records.Num(); i++ )
	{
		if ( Records(i).ComboClass == Combo->GetClass() )
			break;
	}

	if ( i == Records.Num() )
	{
		const TCHAR* ComboName = Combo->ComboTitle.Len() ? *Combo->ComboTitle : *Combo->ExecMessage;
		new(Records) FComboRecord(Combo->GetClass(),ComboName,IconName,*Combo->ComboDescription,Combo->AdrenalineCost);
	}

	TArray<FClassTree*> ChildClasses;
	BaseClass.GetChildClasses(ChildClasses);
	for ( INT i = 0; i < ChildClasses.Num(); i++ )
		ExportComboClass(*ChildClasses(i), Records);
	
	unguard;
}

void UCharacterInfoCommandlet::ExportWeapon( FClassTree& BaseClass, TArray<FWeaponRecord>& Records )
{
	guard(UCharacterInfoCommandlet::ExportRaceWeapon);

	UClass* Cls = BaseClass.GetClass();

	ExportWeaponClass(Cls, Records);

	TArray<FClassTree*> ChildClasses;
	BaseClass.GetChildClasses(ChildClasses);
	for ( INT i = 0; i < ChildClasses.Num(); i++ )
		ExportWeapon(*ChildClasses(i), Records);

	unguard;
}

void UCharacterInfoCommandlet::ExportWeaponClass( UClass* Cls, TArray<FWeaponRecord>& Records )
{
	guard(UCharacterInfoCommandlet::ExportWeaponClass);

	check(Cls);

	AUCWeapon* Weapon = Cast<AUCWeapon>(Cls->GetDefaultObject());

	TCHAR* IconName = appStaticString1024();
	*IconName = 0;

	UMaterial* IconMat = Weapon->IconMaterial;

	if( IconMat )
	{
		appSprintf
			(
			IconName, TEXT("(IconMaterialName=\"%s\",FillIcon=(U=%i,V=%i,UL=%i,VL=%i),LineIcon=(U=%i,V=%i,UL=%i,VL=%i))"),
			IconMat->GetPathName(),
			Weapon->FillIconU, Weapon->FillIconV, (INT)(Weapon->FillIconUL * 0.75), Weapon->FillIconVL,
			Weapon->LineIconU, Weapon->LineIconV, (INT)(Weapon->LineIconUL * 0.75), Weapon->LineIconVL
			);
	}
	else appStrcpy(IconName, TEXT("(IconMaterialName=\"\")"));

	if ( Weapon->ItemName != TEXT("Weapon") && Weapon->ItemName != TEXT("Melee Weapon") )
		new(Records) FWeaponRecord(Cls->GetPathName(), *Weapon->ItemName, IconName);

	unguard;
}

FCharacterRecord::FCharacterRecord( UStruct* Struct, BYTE* Data, INT CharacterIndex )
: m_RaceWeaponIdx(255), m_MeleeWeaponIdx(255), m_AltRaceWeaponIdx(255), m_AltMeleeWeaponIdx(255)
{
	guard(FCharacterRecord::FCharacterRecord);

	check(Struct);
	check(Data);
	check(CharacterIndex < 15);

	DECLAREINNERPROP(Struct,Name);
	DECLAREINNERPROP(Struct,AltName);
	DECLAREINNERPROP(Struct,ClassName);
	DECLAREINNERPROP(Struct,AltClassName);
	DECLAREINNERPROP(Struct,IconName);
	DECLAREINNERPROP(Struct,AltIconName);
	DECLAREINNERPROP(Struct,SelectedIconName);
	DECLAREINNERPROP(Struct,AltSelectedIconName);
	DECLAREINNERPROP(Struct,MovieName);
	DECLAREINNERPROP(Struct,AltMovieName);
	DECLAREINNERPROP(Struct,PortraitName);
	DECLAREINNERPROP(Struct,AltPortraitName);
	DECLAREINNERPROP(Struct,Hint);
	DECLAREINNERPROP(Struct,Bio);
	DECLAREINNERPROP(Struct,AltHint);
	DECLAREINNERPROP(Struct,AltBio);
	DECLAREINNERPROP(Struct,MaxHealth);
	DECLAREINNERPROP(Struct,Agility);
	DECLAREINNERPROP(Struct,AnnouncerCue);
	DECLAREINNERPROP(Struct,AltAnnouncerCue);

	m_Name = *(FString*)(Data + Name->Offset);
	m_AltName = *(FString*)(Data + AltName->Offset);
	m_ClassName = *(FString*)(Data + ClassName->Offset);
	m_AltClassName = *(FString*)(Data + AltClassName->Offset);
	m_IconName = *(FString*)(Data + IconName->Offset);
	m_AltIconName = *(FString*)(Data + AltIconName->Offset);
	m_SelectedIconName = *(FString*)(Data + SelectedIconName->Offset);
	m_AltSelectedIconName = *(FString*)(Data + AltSelectedIconName->Offset);
	m_MovieName = *(FString*)(Data + MovieName->Offset);
	m_AltMovieName = *(FString*)(Data + AltMovieName->Offset);
	m_PortraitName = *(FString*)(Data + PortraitName->Offset);
	m_AltPortraitName = *(FString*)(Data + AltPortraitName->Offset);
	m_Bio = *(FString*)(Data + Bio->Offset);
	m_Hint = *(FString*)(Data + Hint->Offset);
	m_AltBio = *(FString*)(Data + AltBio->Offset);
	m_AltHint = *(FString*)(Data + AltHint->Offset);
	m_AnnouncerCue = **(FName*)(Data + AnnouncerCue->Offset);
	m_AltAnnouncerCue = **(FName*)(Data + AltAnnouncerCue->Offset);

	m_MaxHealth = *(INT*)(Data + MaxHealth->Offset);
	m_Agility = *(INT*)(Data + Agility->Offset);

	unguard;
}

void FCharacterRecord::SetCombos( BYTE* Data, UStructProperty* ComboStructProp, TArray<BYTE>& ComboField )
{
	guard(FCharacterRecord::SetCombos);

	DECLAREINNERPROP(ComboStructProp->Struct,ComboIndex);

	FArray* Arr = (FArray*)(Data + ComboIndex->Offset);
	check(Arr);

	if ( Arr->Num() )
	{
		UProperty* InnerProp = Cast<UArrayProperty>(ComboIndex)->Inner;
		ComboField.Add(Arr->Num());
		for ( INT i = 0; i < Arr->Num(); i++ )
			ComboField(i) = *(BYTE*)((BYTE*)Arr->GetData() + i * InnerProp->ElementSize);
	}

	unguard;
}

UBOOL FCharacterRecord::LoadCombos( const TCHAR* SectionName, TArray<FComboRecord>& Records, FStringOutputDevice& Error, UBOOL bAltClass )
{
	guard(FCharacterRecord::LoadCombos);

	FString FullPathName( SectionName );

	FString PackageName, ClassName;
	FullPathName.Split(TEXT("."), &PackageName, &ClassName);

	FString UCFile;

	FString FilePath = FString::Printf(TEXT("..\\%s\\Classes\\%s.uc"), *PackageName, *ClassName);
	debugf(TEXT("Searching %s for combo names..."), *FilePath);

	// load the .uc file for this character
	if ( appLoadFileToString(UCFile, *FilePath) )
	{
		const TCHAR* UCFileChars = *UCFile;
		FString Line;

		// search the .uc file for the lines which contain the combo data
		while ( ParseLine(&UCFileChars, Line) )
		{
			debugf(TEXT("%s: %s"), *ClassName, *Line);
			if ( Line.InStr(TEXT("ComboList[")) != INDEX_NONE )
			{
				TArray<FString> Parts;

				// split the line by commas
				Line.ParseIntoArray(TEXT(","), &Parts);


				INT pos = Parts(1).InStr(TEXT("\""));

				// parse the fully qualified classname of this combo
				FString ComboName = Parts(1).Mid(pos+1).LeftChop(1);
				UClass* ComboClass = FindObject<UClass>(ANY_PACKAGE, *ComboName);
				check(ComboClass);

				UBOOL bFound = false;
				AUCCombo* DefaultCombo = ComboClass->GetDefaultObject<AUCCombo>();
				for ( FName ComboStatsName = ComboClass->GetFName(); ComboStatsName != NAME_None; ComboStatsName = DefaultCombo->StatsName )
				{
					for ( INT ComboRecordIndex = 0; ComboRecordIndex < Records.Num(); ComboRecordIndex++ )
					{
						FComboRecord& Record = Records(ComboRecordIndex);
						if ( ComboStatsName == Record.ComboClass->GetFName() )
						{
							if ( bAltClass )
								m_AltCombos.AddUniqueItem(ComboRecordIndex);
							else
								m_Combos.AddUniqueItem(ComboRecordIndex);

							bFound = true;
							break;
						}
					}

					if ( !bFound )
					{
						for ( INT ComboRecordIndex = 0; ComboRecordIndex < Records.Num(); ComboRecordIndex++ )
						{
							FComboRecord& Record = Records(ComboRecordIndex);
							AUCCombo* RecordCombo = Record.ComboClass->GetDefaultObject<AUCCombo>();
							if ( RecordCombo->StatsName != NAME_None && ComboStatsName == RecordCombo->StatsName )
							{
								if ( bAltClass )
									m_AltCombos.AddUniqueItem(ComboRecordIndex);
								else
									m_Combos.AddUniqueItem(ComboRecordIndex);
								
								bFound = true;
								break;
							}
						}
					}

					if ( bFound )
						break;
				}
			}
		}
	}
	else
		warnf(TEXT("Couldn't load file %s"), *FilePath);

/*
	UClass* CharacterClass = LoadClass<AUCPawn>(NULL,SectionName,NULL,0,NULL);
	if ( CharacterClass == NULL )
	{
		warnf(TEXT("Failed to load character class for %s"), SectionName);
		return 0;
	}

	AUCPawn* Pawn = CharacterClass->GetDefaultObject<AUCPawn>();
	for ( INT ComboListIndex = 0; ComboListIndex < Pawn->ComboList.Num(); ComboListIndex++ )
	{
		// find the index for this combo class
		for ( INT ComboRecordIndex = 0; ComboRecordIndex < Records.Num(); ComboRecordIndex++ )
		{
			if ( Records(ComboRecordIndex).ComboClass == Pawn->ComboList(ComboListIndex) )
			{
				if ( bAltClass )
					m_AltCombos.AddUniqueItem(ComboRecordIndex);
				else
					m_Combos.AddUniqueItem(ComboRecordIndex);
				break;
			}
		}
	}
*/

/*
	TArray<FString> Combos;

	// First, attempt to load the combo list from the current section
	Section->MultiFind( TEXT("ComboList"), Combos );

	FString BaseClass;
	GConfig->GetString(SectionName, TEXT("#Extends"), BaseClass, TEXT("Characters.ini"));

	// load the base class section, if it exists.
	if ( BaseClass.Len() )
		LoadCombos(*BaseClass,Records,Error);

	// when dealing with multimaps, the keynames will be returned in reverse order
	for ( INT i = Combos.Num() - 1, c = 0; i >= 0; i--, c++ )
	{
		FString ClassName(Combos(i).Mid(6).LeftChop(1));
		for ( INT j = 0; j < Records.Num(); j++ )
		{
			if ( !appStricmp(Records(j).ComboClass->GetPathName(),*ClassName) )
			{
				if ( c >= m_Combos.Num() )
					m_Combos.Add();

				m_Combos(c) = j;
				break;
			}
		}
	}
*/
	return 1;

	unguard;
}

UBOOL FCharacterRecord::SetComboIndexes( TArray<FComboRecord>& Records, FStringOutputDevice& Error )
{
	guard(FCharacterRecord::SetComboIndexes);

	UBOOL bResult = LoadCombos(*m_ClassName, Records, Error);
	if ( m_AltClassName.Len() )
		bResult = LoadCombos(*m_AltClassName, Records, Error, true) && bResult;

	return bResult;
		
	unguard;
}

UBOOL FCharacterRecord::SetWeaponIndexes( TArray<FWeaponRecord>& RaceWeapons, TArray<FWeaponRecord>& MeleeWeapons, FStringOutputDevice& Error )
{
	guard(FCharacterRecord::SetWeaponIndexes);

	UClass* CharacterClass = LoadClass<AUCPawn>(NULL,*m_ClassName,NULL,0,NULL);
	if ( CharacterClass == NULL )
	{
		warnf(TEXT("Failed to load character class for %s"), *m_ClassName);
		return 0;
	}

	UBOOL bResult = LoadRaceWeapons(CharacterClass, RaceWeapons, Error, false);
	bResult = LoadMeleeWeapons(CharacterClass,MeleeWeapons,Error, false) && bResult;

	if ( m_AltClassName.Len() )
	{
		CharacterClass = LoadClass<AUCPawn>(NULL,*m_AltClassName,NULL,0,NULL);
		if ( CharacterClass == NULL )
		{
			warnf(TEXT("Failed to load character class for %s"), *m_AltClassName);
		}
		else
		{
			bResult = LoadRaceWeapons(CharacterClass, RaceWeapons, Error, true) && bResult;
			bResult = LoadMeleeWeapons(CharacterClass, MeleeWeapons, Error, true) && bResult;
		}
	}

	return bResult;

	unguard;
}

UBOOL FCharacterRecord::LoadRaceWeapons( UClass* CharClass, TArray<FWeaponRecord>& RaceWeapons, FStringOutputDevice& Error, UBOOL bAltClass  )
{
	guard(FCharacterRecord::LoadRaceWeapons);

	BYTE& RaceWeaponIdx = bAltClass ? m_AltRaceWeaponIdx : m_RaceWeaponIdx;

	AUCPawn* Pawn = CharClass->GetDefaultObject<AUCPawn>();
	if ( Pawn->RequiredEquipment.Num() == 0 )
	{
		Error.Logf(TEXT("Failed to find race weapon for %s: No RequiredEquipment configured."), bAltClass ? *m_AltClassName : *m_ClassName);
		return 0;
	}

	FString& RaceWeapon = Pawn->RequiredEquipment(0);
	for ( INT i = 0; i < RaceWeapons.Num(); i++ )
	{
		if ( RaceWeapon == RaceWeapons(i).WeaponClassName )
		{
            RaceWeaponIdx = i;
			break;
		}
	}

	if ( RaceWeaponIdx != 255 )
		return 1;

	Error.Logf(TEXT("Failed to find race weapon for %s: No existing classes found matching configured value '%s'\r\n"), bAltClass ? *m_AltClassName : *m_ClassName, *RaceWeapon);

	return 0;
	unguard;
}

UBOOL FCharacterRecord::LoadMeleeWeapons( UClass* CharClass, TArray<FWeaponRecord>& MeleeWeapons, FStringOutputDevice& Error, UBOOL bAltClass  )
{
	guard(FCharacterRecord::LoadMeleeWeapons);

	BYTE& MeleeWeaponIdx = bAltClass ? m_AltMeleeWeaponIdx : m_MeleeWeaponIdx;
	AUCPawn* Pawn = CharClass->GetDefaultObject<AUCPawn>();
	if ( Pawn->RequiredEquipment.Num() == 0 )
	{
		Error.Logf(TEXT("Failed to find melee weapon for %s: No RequiredEquipment configured."), bAltClass ? *m_AltClassName : *m_ClassName);
		return 0;
	}

	FString& MeleeWeapon = Pawn->RequiredEquipment(1);
	for ( INT i = 0; i < MeleeWeapons.Num(); i++ )
	{
		if ( MeleeWeapon == MeleeWeapons(i).WeaponClassName )
		{
			MeleeWeaponIdx = i;
			break;
		}
	}

	if ( MeleeWeaponIdx != 255 )
		return 1;

	Error.Logf(TEXT("Failed to find melee weapon for %s: No existing classes found matching configured value '%s'\r\n"), bAltClass ? *m_AltClassName : *m_ClassName, *MeleeWeapon);

	return 0;
	unguard;
}

const TCHAR* Align( INT Num )
{
	TCHAR* Result = appStaticString1024();
	*Result = 0;

	for ( INT i = 0; i < Num && i < 1023; i++ )
		appStrcat(Result, TEXT("\t"));

	return Result;
}

#define NL TEXT("\r\n")
void FCharacterRecord::Export( INT CharIndex, FStringOutputDevice& Ar, const TArray<FWeaponRecord>& RaceRecords, const TArray<FWeaponRecord>& MeleeRecords )
{
	guard(FCharacterRecord::Export);

	Ar.Logf(TEXT("%sCharacters(%i)={(%s"), Align(1), CharIndex, NL);

	Ar.Logf(TEXT("%sName=\"%s\",%s"), Align(5), *m_Name, NL);
	Ar.Logf(TEXT("%sAltName=\"%s\",%s"), Align(5), *m_AltName, NL);
	Ar.Logf(TEXT("%sClassName=\"%s\",%s"), Align(5), *m_ClassName,NL);
	Ar.Logf(TEXT("%sAltClassName=\"%s\",%s"), Align(5), *m_AltClassName, NL);
	Ar.Logf(TEXT("%sIconName=\"%s\",%s"), Align(5), *m_IconName, NL);
	Ar.Logf(TEXT("%sAltIconName=\"%s\",%s"), Align(5), *m_AltIconName, NL);
	Ar.Logf(TEXT("%sSelectedIconName=\"%s\",%s"), Align(5), *m_SelectedIconName, NL);
	Ar.Logf(TEXT("%sAltSelectedIconName=\"%s\",%s"), Align(5), *m_AltSelectedIconName, NL);
	Ar.Logf(TEXT("%sMovieName=\"%s\",%s"), Align(5), *m_MovieName, NL);
	Ar.Logf(TEXT("%sAltMovieName=\"%s\",%s"), Align(5), *m_AltMovieName, NL);
	Ar.Logf(TEXT("%sPortraitName=\"%s\",%s"), Align(5), *m_PortraitName, NL);
	Ar.Logf(TEXT("%sAltPortraitName=\"%s\",%s"), Align(5), *m_AltPortraitName, NL);
	Ar.Logf(TEXT("%sBio=\"%s\",%s"), Align(5), *m_Bio, NL);
	Ar.Logf(TEXT("%sAltBio=\"%s\",%s"), Align(5), *m_AltBio, NL);
	Ar.Logf(TEXT("%sHint=\"%s\",%s"), Align(5), *m_Hint, NL);
	Ar.Logf(TEXT("%sAltHint=\"%s\",%s"), Align(5), *m_AltHint, NL);

	Ar.Logf(TEXT("%sMaxHealth=%i,Agility=%i,%s"), Align(5), m_MaxHealth, m_Agility, NL);
	if ( m_RaceWeaponIdx != 255 )
		Ar.Logf(TEXT("%sRaceWeaponIdx=%i,%s// %s%s"), Align(5), m_RaceWeaponIdx, Align(2), *RaceRecords(m_RaceWeaponIdx).WeaponClassName, NL);
	else Ar.Logf(TEXT("%sRaceWeaponIdx=%i,%s// %s%s"), Align(5), 255, Align(2), TEXT("None"), NL);
	if ( m_MeleeWeaponIdx != 255 )
		Ar.Logf(TEXT("%sMeleeWeaponIdx=%i,%s// %s%s"), Align(5), m_MeleeWeaponIdx, Align(2), *MeleeRecords(m_MeleeWeaponIdx).WeaponClassName, NL);
	else Ar.Logf(TEXT("%sMeleeWeaponIdx=%i,%s// %s%s"), Align(5), 255, Align(2), TEXT("None"), NL);

	if ( m_AltRaceWeaponIdx != 255 )
		Ar.Logf(TEXT("%sAltRaceWeaponIdx=%i,%s// %s%s"), Align(5), m_AltRaceWeaponIdx, Align(2), *RaceRecords(m_AltRaceWeaponIdx).WeaponClassName, NL);
	else Ar.Logf(TEXT("%sAltRaceWeaponIdx=%i,%s// %s%s"), Align(5), 255, Align(2), TEXT("None"), NL);
	if ( m_AltMeleeWeaponIdx != 255 )
		Ar.Logf(TEXT("%sAltMeleeWeaponIdx=%i,%s// %s%s"), Align(5), m_AltMeleeWeaponIdx, Align(2), *MeleeRecords(m_AltMeleeWeaponIdx).WeaponClassName, NL);
	else Ar.Logf(TEXT("%sAltMeleeWeaponIdx=%i,%s// %s%s"), Align(5), 255, Align(2), TEXT("None"), NL);

	Ar.Logf(TEXT("%sAnnouncerCue=%s,%s"), Align(5), *m_AnnouncerCue, NL);
	Ar.Logf(TEXT("%sAltAnnouncerCue=%s,%s"), Align(5), m_AltAnnouncerCue.Len() ? *m_AltAnnouncerCue : TEXT(""), NL);

	TCHAR temp[256] = TEXT("");
	for ( INT i = 0; i < m_Combos.Num(); i++ )
	{
		if ( m_Combos(i) != 255 )
		{
			if ( *temp == 0 )
				*temp = '(';
			else appStrncat(temp,TEXT(","),ARRAY_COUNT(temp));

			appStrncat(temp, appItoa(m_Combos(i)), ARRAY_COUNT(temp));
		}
	}

	if ( *temp )
		appStrncat(temp, TEXT(")"), ARRAY_COUNT(temp));
	Ar.Logf(TEXT("%sCombos=(ComboIndex=%s),%s"), Align(5), temp, NL);

	appMemzero(temp, ARRAY_COUNT(temp));
	for ( INT i = 0; i < m_AltCombos.Num(); i++ )
	{
		if ( m_AltCombos(i) != 255 )
		{
			if ( *temp == 0 )
				*temp = '(';
			else appStrncat(temp,TEXT(","),ARRAY_COUNT(temp));

			appStrncat(temp, appItoa(m_AltCombos(i)), ARRAY_COUNT(temp));
		}
	}

	if ( *temp )
		appStrncat(temp, TEXT(")"), ARRAY_COUNT(temp));
	Ar.Logf(TEXT("%sAltCombos=(ComboIndex=%s),%s"), Align(5), temp, NL);

	Ar.Logf(TEXT("%s)}%s"), Align(5), NL);
	unguard;
}

IMPLEMENT_CLASS(UCharacterInfoCommandlet);

/* epic ===============================================
* UStatsNamesCommandlet
* Author: moelfke
*
* Exports the names of all maps, characters, combos, and
* weapons that are not already known to the UPlayerStats
* object. To maintain stats compatibility, the entries
* listed by this commandlet must be appended to the
* corresponding arrays in UPlayerStats's configuration -
* any other order will corrupt the stats.
*
* =====================================================
*/

static INT Compare( FName& NameA, FName& NameB )
{
	return appStrcmp(*NameA, *NameB);
}

#define StatsFile TEXT("Stats.ini")
INT UStatsNamesCommandlet::Main(const TCHAR* Parms)
{
	guard(UStatsNamesCommandlet::Main);

	GIsRequestingExit = 1;

	// load all the packages we need
	warnf(TEXT("Loading Engine.u..."));
	LoadPackage(NULL, TEXT("Engine.u"), LOAD_NoFail);
	warnf(TEXT("Loading UCGameplay.u..."));
	LoadPackage(NULL, TEXT("UCGameplay.u"), LOAD_NoFail);
	warnf(TEXT("Loading UCCharacters.u..."));
	LoadPackage(NULL, TEXT("UCCharacters.u"), LOAD_NoFail);
	warnf(TEXT("Loading UCInventory.u..."));
	LoadPackage(NULL, TEXT("UCInventory.u"), LOAD_NoFail);
	warnf(TEXT("Loading UCMutators.u..."));
	LoadPackage(NULL, TEXT("UCMutators.u"), LOAD_NoFail);

	FName PropMap(TEXT("MapNames"));
	FName PropChar(TEXT("CharNames"));
	FName PropWeapon(TEXT("WeaponNames"));
	FName PropCombo(TEXT("ComboNames"));
	FName PropRung(TEXT("RungNames"));

/*
	UClass* StatsClass = LoadClass<UObject>(NULL, TEXT("Engine.PlayerStats"), NULL, LOAD_NoFail, NULL);
	UPlayerStats* DefaultStats = StatsClass->GetDefaultObject<UPlayerStats>();

	//initialize stats data with the current values
	for ( INT i = 0; i < DefaultStats->MapNames.Num(); i++ )
		StatsData.AddUnique(PropMap, DefaultStats->MapNames(i));

	for ( INT i = 0; i < DefaultStats->CharNames.Num(); i++ )
		StatsData.AddUnique(PropChar, DefaultStats->CharNames(i));

	for ( INT i = 0; i < DefaultStats->WeaponNames.Num(); i++ )
		StatsData.AddUnique(PropWeapon, DefaultStats->WeaponNames(i));

	for ( INT i = 0; i < DefaultStats->ComboNames.Num(); i++ )
		StatsData.AddUnique(PropCombo, DefaultStats->ComboNames(i));

	for ( INT i = 0; i < DefaultStats->RungNames.Num(); i++ )
		StatsData.AddUnique(PropRung, DefaultStats->RungNames(i));
*/

	// find all maps

	TArray<FName> Names;

	TArray<FString> Maps = GFileManager->FindFiles(TEXT("../Maps/*.unr"), 1, 0);
	warnf(TEXT("------------------- MAPS -------------------"));
	for (INT i = 0; i < Maps.Num(); i++)
	{
		
		FString Map = Maps(i);
		// ignore autosaves, the menu level, and Entry
		if (Map.Left(4) != TEXT("Auto") && Map != TEXT("Menu.unr") && Map != TEXT("Entry.unr"))
		{
			// get the map filename without the extension
			INT pos = Map.InStr(TEXT(".unr"));
			if (pos != INDEX_NONE)
			{
				Map = Map.Left(pos);
			}

			if ( !appStrnicmp(*Map, TEXT("ACT"), 3) || !appStrnicmp(*Map,TEXT("TUT"),3) )
				continue;

			// get the FName of the map name
			FName MapName = FName(*Map);

			StatsData.AddUnique(PropMap, MapName);
		}
	}

	warnf(TEXT("------------------- CHARACTERS -------------------"));
	UClass* CharClass = LoadClass<UObject>(NULL, TEXT("Engine.CharacterInformation"), NULL, 0, NULL);
	check(CharClass);
	BYTE* Data = (BYTE*)CharClass->GetDefaultObject();
	TArray<FCharacterRecord> Records;
	DECLARESTRUCTPROP(Characters);
	for ( INT j = 0; j < Characters->ArrayDim; j++ )
		new(Records) FCharacterRecord(Characters->Struct, Data + Characters->Offset + j * Characters->ElementSize, j);

	for ( INT i = 0; i < Records.Num(); i++ )
	{
		FCharacterRecord& Char = Records(i);

		INT pos = Char.m_ClassName.InStr(TEXT("."));
		if ( pos != INDEX_NONE )
		{
			FName CharClassName = FName(*Char.m_ClassName.Mid(pos+1));
			INT idx = Names.AddZeroed();
			Names(idx) = CharClassName;
		}

		if ( Char.m_AltClassName.Len() )
		{
			pos = Char.m_AltClassName.InStr(TEXT("."));
			if ( pos != INDEX_NONE )
			{
				FName CharClassName = FName(*Char.m_AltClassName.Mid(pos+1));
				INT idx = Names.AddZeroed();
				Names(idx) = CharClassName;
			}
		}
	}
//	for (TObjectIterator<UClass> It; It; ++It)
//	{
		// if this is a spawnable pawn class
//		if (It->IsChildOf(AUCPawn::StaticClass()) && !(It->ClassFlags & CLASS_Abstract))
//		{
//			Names.AddItem(It->GetFName());
//		}
//	}
	Sort(&Names(0), Names.Num());
	for ( int i = 0; i < Names.Num(); i++ )
        StatsData.AddUnique(PropChar, Names(i));
	Names.Empty();

	warnf(TEXT("------------------- WEAPONS -------------------"));
	for (TObjectIterator<UClass> It; It; ++It)
	{
		// if this is a spawnable weapon class
		if (It->IsChildOf(AWeapon::StaticClass()) && !(It->ClassFlags & CLASS_Abstract))
		{
			FName StatsName = It->GetDefaultObject<AWeapon>()->StatsName;
			if (StatsName == NAME_None)
			{
				StatsName = It->GetFName();
			}
			Names.AddItem(StatsName);
		}
	}
	Sort(&Names(0), Names.Num());
	for ( int i = 0; i < Names.Num(); i++ )
		StatsData.AddUnique(PropWeapon, Names(i));
	Names.Empty();


	warnf(TEXT("------------------- COMBOS -------------------"));
	for (TObjectIterator<UClass> It; It; ++It)
	{
		// if this is a spawnable combo class
		if ( It->IsChildOf(AUCCombo::StaticClass()) && !(It->ClassFlags & CLASS_Abstract))
		{
			AUCCombo* Default = It->GetDefaultObject<AUCCombo>();
			if (!Default->bNegativeCombo && !Default->bIgnoreForStats)
			{
				FName StatsName = Default->StatsName;
				if (StatsName == NAME_None)
				{
					StatsName = It->GetFName();
				}
				Names.AddItem(StatsName);
			}
		}
	}
	Sort(&Names(0), Names.Num());
	for ( int i = 0; i < Names.Num(); i++ )
		StatsData.AddUnique(PropCombo, Names(i));
	Names.Empty();


	warnf(TEXT("------------------- RUNGS -------------------"));
	FConfigCacheIni* Config = (FConfigCacheIni*)GConfig;
	FConfigFile* File = Config->Find(TEXT("Localization.ini"),0);
	check(File);

	FConfigSection* Sec = File->Find(TEXT("UCGameplay.TournamentLadderRung"));
	check(Sec);

	FPerObjectConfigList RungList(TEXT("UCGameplay.TournamentLadderRung"), *Sec);
	for ( INT i = 0; i < RungList.ObjectNames.Num(); i++ )
	{
		FString RungString = FString(*RungList.ObjectNames(i));
		INT pos = RungString.InStr(TEXT("_"), true);
		if ( pos == INDEX_NONE || !appIsDigit(**RungString.Mid(pos + 1)) )
			continue;

		StatsData.AddUnique(PropRung, RungList.ObjectNames(i));
	}
	warnf(TEXT("------------------- ALL DONE -------------------"));

	FStringOutputDevice Ar;

	Ar.Log(TEXT("[Engine.PlayerStats]"));

	INT MapCount = ExportStatValues(PropMap, Ar);
	warnf(TEXT("Exported %i maps to %s!"), MapCount, StatsFile);

	INT CharCount = ExportStatValues(PropChar, Ar);
	warnf(TEXT("Exported %i characters to %s!"), CharCount, StatsFile);

	INT WeapCount = ExportStatValues(PropWeapon, Ar);
	warnf(TEXT("Exported %i weapons to %s!"), WeapCount, StatsFile);

	INT ComboCount = ExportStatValues(PropCombo, Ar);
	warnf(TEXT("Exported %i combos to %s!"), ComboCount, StatsFile);

	INT RungCount = ExportStatValues(PropRung, Ar);
	warnf(TEXT("Exported %i ladder rungs to %s!"), RungCount, StatsFile);

	warnf(TEXT("Exported %i total records to %s!"), MapCount + CharCount + WeapCount + ComboCount + RungCount, StatsFile);

	if ( GFileManager->IsReadOnly(StatsFile) )
	{
		INT Code;
		void* Handle = appCreateProc( TEXT("p4"), *FString::Printf(TEXT("edit %s"), StatsFile) );
		while( !appGetProcReturnCode( Handle, &Code ) )
			appSleep(1);
	}

	if ( GFileManager->IsReadOnly(StatsFile) )
		warnf(NAME_Warning, TEXT("Failed to checkout %s: File is still read-only!"), StatsFile);
	else
	{
		GFileManager->Delete(TEXT("Stats.ini"), 0, 1);
		appSaveStringToFile( Ar, TEXT("Stats.ini"), GFileManager );
	}

	return 0;

	unguard;
}

INT UStatsNamesCommandlet::ExportStatValues( const FName& Type, FStringOutputDevice& Ar )
{
	TArray<FName> Values;
	StatsData.MultiFind(Type, Values);
	Ar.Log(TEXT("\r\n;------------------------------------------------------------\r\n"));
	Ar.Logf(TEXT(";	%s\r\n"), *Type);
	Ar.Log(TEXT(";------------------------------------------------------------\r\n"));
	for ( INT RevIndex = Values.Num() - 1, Index = 0; RevIndex >= 0; RevIndex--, Index++ )
	{
		Ar.Logf(TEXT("%s=%s\r\n"), *Type, *Values(RevIndex));
	}

	return Values.Num();
}

IMPLEMENT_CLASS(UStatsNamesCommandlet);

static void SetupDLOTracking()
{
	// load all native classes
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* pClass = *It;
		if (!pClass->GetLinker())
		{
			LoadObject<UClass>(pClass->GetOuter(), pClass->GetName(), NULL, LOAD_Quiet | LOAD_NoWarn, NULL);
		}
	}

	// need to create the packages Joe's load tracking hack expects
	UObject::CreatePackage(NULL, TEXT("UCMenu"));
	UObject::CreatePackage(NULL, TEXT("GUI"));
	UObject::CreatePackage(NULL, TEXT("UCElements"));
}

/* epic ===============================================
* UPawnSkinDLOsCommandlet
* Author: moelfke
*
* Lists all the objects required by the specified
* pawn's skins (team or non-team, also specified on the
* command line)
*
* NOTE: Each pawn's skins must be done seperately so
* that materials used by multiple will be listed for
* each one. I could have tried getting a list of
* pawn classes beforehand and then garbage collecting
* after each but that never seems to work property for
* some reason.
*
* =====================================================
*/
INT UPawnSkinDLOsCommandlet::Main(const TCHAR* Parms)
{
	guard(UPawnSkinDLOsCommandlet::Main);

	SetupDLOTracking();

	ShowErrorCount = 1;
	GIsRequestingExit = 1;

	// load the class
	FString PawnClassName;
	if (!ParseToken(Parms, PawnClassName, 0))
	{
		appErrorf(TEXT("Syntax: ucc pawnskindlos <package.class> <teamskins=true|false>"));
	}
	// if no package specified, assume UCCharacters
	if (PawnClassName.InStr(TEXT(".")) == -1)
	{
		PawnClassName = FString(TEXT("UCCharacters.")) + PawnClassName;
	}
	// load the class
	UClass* PawnClass = (UClass*)StaticLoadObject(UClass::StaticClass(), NULL, *PawnClassName, NULL, LOAD_NoWarn, NULL);
	if (!PawnClass)
	{
		appErrorf(TEXT("Failed to load class %s"), *PawnClassName);
	}
	if (!PawnClass->IsChildOf(AUCPawn::StaticClass()))
	{
		appErrorf(TEXT("Class %s is not a subclass of UCPawn"), *PawnClassName);
	}
	// find out whether to do team skins or normal skins
	UBOOL bTeamSkins;
	if (!ParseUBOOL(Parms, TEXT("TEAMSKINS="), bTeamSkins))
	{
		appErrorf(TEXT("Syntax: ucc pawnskindlos <package.class> <teamskins=true|false>"));
	}

	// turn on load tracking
	StaticExec(TEXT("TRACKLOADS"));

	// load the skins
	AUCPawn* DefaultPawn = PawnClass->GetDefaultObject<AUCPawn>();
	if (bTeamSkins)
	{
		for (INT i = 0; i < DefaultPawn->TeamSkins.Num(); i++)
		{
			if (DefaultPawn->TeamSkins(i).Len())
			{
				StaticLoadObject(UMaterial::StaticClass(), NULL, *DefaultPawn->TeamSkins(i), NULL, LOAD_NoWarn, NULL);
			}
		}
		for (INT i = 0; i < DefaultPawn->TeamHeadSkins.Num(); i++)
		{
			if (DefaultPawn->TeamHeadSkins(i).Len())
			{
				StaticLoadObject(UMaterial::StaticClass(), NULL, *DefaultPawn->TeamHeadSkins(i), NULL, LOAD_NoWarn, NULL);
			}
		}
	}
	else
	{
		for (INT i = 0; i < DefaultPawn->BodySkinStrings.Num(); i++)
		{
			if (DefaultPawn->BodySkinStrings(i).Len())
			{
				StaticLoadObject(UMaterial::StaticClass(), NULL, *DefaultPawn->BodySkinStrings(i), NULL, LOAD_NoWarn, NULL);
			}
		}
		if (DefaultPawn->HeadSkinString.Len())
		{
			StaticLoadObject(UMaterial::StaticClass(), NULL, *DefaultPawn->HeadSkinString, NULL, LOAD_NoWarn, NULL);
		}
	}

	// log all the objects loaded
	if (bTeamSkins)
	{
		debugf(TEXT("Objects loaded for skins of %s (team)"), *PawnClassName);
	}
	else
	{
		debugf(TEXT("Objects loaded for skins of %s (non-team)"), *PawnClassName);
	}
	StaticExec(TEXT("DUMPLOADS"));

	return 0;

	unguard;
}
IMPLEMENT_CLASS(UPawnSkinDLOsCommandlet);

/* epic ===============================================
* UHandDLOsCommandlet
* Author: moelfke
*
* Lists all the objects required by the specified
* UCHands's mesh and skins
*
* =====================================================
*/
INT UHandsDLOsCommandlet::Main(const TCHAR* Parms)
{
	guard(UHandsDLOsCommandlet::Main);

	SetupDLOTracking();

	ShowErrorCount = 1;
	GIsRequestingExit = 1;

	// load the class
	FString HandsClassName;
	if (!ParseToken(Parms, HandsClassName, 0))
	{
		appErrorf(TEXT("Syntax: ucc handsdlos <package.class>"));
	}
	// if no package specified, assume UCCharacters
	if (HandsClassName.InStr(TEXT(".")) == -1)
	{
		HandsClassName = FString(TEXT("UCCharacters.")) + HandsClassName;
	}
	// load the class
	UClass* HandsClass = (UClass*)StaticLoadObject(UClass::StaticClass(), NULL, *HandsClassName, NULL, LOAD_NoWarn, NULL);
	if (!HandsClass)
	{
		appErrorf(TEXT("Failed to load class %s"), *HandsClassName);
	}
	UBOOL bIsAHands = false;
	for (UClass* TestClass = HandsClass; TestClass && !bIsAHands; TestClass = TestClass->GetSuperClass())
	{
		if (TestClass->GetFName() == FName(TEXT("UCHands")))
		{
			bIsAHands = true;
		}
	}
	if (!bIsAHands)
	{
		appErrorf(TEXT("Class %s is not a subclass of UCHands"), *HandsClassName);
	}
	
	// turn on load tracking
	StaticExec(TEXT("TRACKLOADS"));

	// get the properties we're interested in
	UProperty* HandsProperties[3] = {	FindField<UArrayProperty>(HandsClass, TEXT("AnimationSets")),
										FindField<UStrProperty>(HandsClass, TEXT("MeshString")),
										FindField<UArrayProperty>(HandsClass, TEXT("SkinStrings"))	};

	// load everything
	for (INT i = 0; i < ARRAY_COUNT(HandsProperties); i++)
	{
		if (HandsProperties[i]->IsA(UArrayProperty::StaticClass()))
		{
			if (((UArrayProperty*)HandsProperties[i])->Inner->IsA(UStrProperty::StaticClass()))
			{
				TArray<FString>* Strings = (TArray<FString>*)(HandsClass->GetDefaultsSafe() + HandsProperties[i]->Offset);
				for (INT j = 0; j < Strings->Num(); j++)
				{
					if ((*Strings)(j).Len())
					{
						if ( !StaticLoadObject(UMaterial::StaticClass(), NULL, *(*Strings)(j), NULL, LOAD_NoWarn | LOAD_Quiet, NULL) &&
							!StaticLoadObject(UClass::StaticClass(), NULL, *(*Strings)(j), NULL, LOAD_NoWarn | LOAD_Quiet, NULL) &&
							!StaticLoadObject(UMesh::StaticClass(), NULL, *(*Strings)(j), NULL, LOAD_NoWarn | LOAD_Quiet, NULL) &&
							!StaticLoadObject(UObject::StaticClass(), NULL, *(*Strings)(j), NULL, LOAD_NoWarn | LOAD_Quiet, NULL) )
						{
							warnf(TEXT("Failed to load object %s"), *(*Strings)(j));
						}
					}
				}
			}
		}
		else if (HandsProperties[i]->IsA(UStrProperty::StaticClass()))
		{
			FString* String = (FString*)(HandsClass->GetDefaultsSafe() + HandsProperties[i]->Offset);
			if (String->Len())
			{
				if ( !StaticLoadObject(UMaterial::StaticClass(), NULL, *(*String), NULL, LOAD_NoWarn | LOAD_Quiet, NULL) &&
					!StaticLoadObject(UClass::StaticClass(), NULL, *(*String), NULL, LOAD_NoWarn | LOAD_Quiet, NULL) &&
					!StaticLoadObject(UMesh::StaticClass(), NULL, *(*String), NULL, LOAD_NoWarn | LOAD_Quiet, NULL) &&
					!StaticLoadObject(UObject::StaticClass(), NULL, *(*String), NULL, LOAD_NoWarn | LOAD_Quiet, NULL) )
				{
					warnf(TEXT("Failed to load object %s"), *(*String));
				}
			}
		}
	}
	
	// log all the objects loaded
	debugf(TEXT("Objects loaded for %s"), *HandsClassName);
	StaticExec(TEXT("DUMPLOADS"));

	return 0;

	unguard;
}
IMPLEMENT_CLASS(UHandsDLOsCommandlet);

/* epic ===============================================
* UWeapon1pDLOsCommandlet
* Author: moelfke
*
* Lists all the objects required by the specified
* UCWeapon's first person meshes/skins/effects
*
* =====================================================
*/
INT UWeapon1pDLOsCommandlet::Main(const TCHAR* Parms)
{
	guard(UWeapon1pDLOsCommandlet::Main);

	SetupDLOTracking();

	ShowErrorCount = 1;
	GIsRequestingExit = 1;

	// load the class
	FString WeaponClassName;
	if (!ParseToken(Parms, WeaponClassName, 0))
	{
		appErrorf(TEXT("Syntax: ucc weapon1pdlos <package.class>"));
	}
	// if no package specified, assume UCInventory
	if (WeaponClassName.InStr(TEXT(".")) == -1)
	{
		WeaponClassName = FString(TEXT("UCInventory.")) + WeaponClassName;
	}
	// load the class
	UClass* WeaponClass = (UClass*)StaticLoadObject(UClass::StaticClass(), NULL, *WeaponClassName, NULL, LOAD_NoWarn, NULL);
	if (!WeaponClass)
	{
		appErrorf(TEXT("Failed to load class %s"), *WeaponClassName);
	}
	if (!WeaponClass->IsChildOf(AUCWeapon::StaticClass()))
	{
		appErrorf(TEXT("Class %s is not a subclass of UCWeapon"), *WeaponClassName);
	}
	
	// load all the general weapon objects that we always load so they aren't listed when tracking the 1p loads
	FString GeneralWeaponNames[] = {	TEXT("UCGameplay.UCWeapon"),
										TEXT("UCGameplay.UCWeap_Ranged"),
										TEXT("UCGameplay.UCWeap_Race"),
										TEXT("UCGameplay.UCWeap_Melee"),
										TEXT("UCGameplay.UCWeap_Melee_Jugg"),
										TEXT("UCGameplay.UCWeap_Melee_Liandri"),
										TEXT("UCGameplay.UCWeap_Melee_Nakhti"),
										TEXT("UCGameplay.UCWeap_Melee_Necris"),
										TEXT("UCGameplay.UCWeap_Melee_Skaarj"),
										TEXT("UCGameplay.UCWeap_Melee_Terran")	};
	for (INT i = 0; i < ARRAY_COUNT(GeneralWeaponNames); i++)
	{
		UClass* WeaponClass = (UClass*)StaticLoadObject(UClass::StaticClass(), NULL, *GeneralWeaponNames[i], NULL, LOAD_NoWarn, NULL);
		checkSlow(WeaponClass->IsChildOf(AUCWeapon::StaticClass()));
		AUCWeapon* DefaultWeapon = WeaponClass->GetDefaultObject<AUCWeapon>();
		for (INT j = 0; j < DefaultWeapon->GeneralDLOList.Num(); j++)
		{
			StaticLoadObject( DefaultWeapon->GeneralDLOList(j).ObjectClass, NULL,
								*FString::Printf( TEXT("%s.%s"), *DefaultWeapon->GeneralDLOList(j).PackageName,
													*DefaultWeapon->GeneralDLOList(j).ObjectName ),
								NULL, LOAD_NoWarn, NULL );
		}
	}

	// turn on load tracking
	StaticExec(TEXT("TRACKLOADS"));

	// load all the 1p stuff for this weapon
	TArray<FString> List;
	WeaponClass->GetDefaultObject<AUCWeapon>()->eventGetFirstPersonAssetList(List);
	for (INT i = 0; i < List.Num(); i++)
	{
		if (List(i) != TEXT(""))
		{
			// try various base classes
			if ( !StaticLoadObject(UMaterial::StaticClass(), NULL, *List(i), NULL, LOAD_NoWarn | LOAD_Quiet, NULL) &&
				!StaticLoadObject(UClass::StaticClass(), NULL, *List(i), NULL, LOAD_NoWarn | LOAD_Quiet, NULL) &&
				!StaticLoadObject(UMesh::StaticClass(), NULL, *List(i), NULL, LOAD_NoWarn | LOAD_Quiet, NULL) &&
				!StaticLoadObject(UObject::StaticClass(), NULL, *List(i), NULL, LOAD_NoWarn | LOAD_Quiet, NULL))
			{
				warnf(TEXT("Failed to load object %s"), *List(i));
			}
		}
	}

	// log all the objects loaded
	debugf(TEXT("Objects loaded for %s"), *WeaponClassName);
	StaticExec(TEXT("DUMPLOADS"));

	return 0;

	unguard;
}
IMPLEMENT_CLASS(UWeapon1pDLOsCommandlet);

/* epic ===============================================
* UMapMemoryUsageCommandlet
* Author: moelfke
*
* Prints a sorted list of the memory used by the
* LIN and XPRs for each map
*
* =====================================================
*/
struct FMapMemoryInfo
{
	FString MapName;
	INT TotalSize;
};

static FORCEINLINE INT Compare(const FMapMemoryInfo& A, const FMapMemoryInfo& B)
{
	return (B.TotalSize - A.TotalSize);
}

INT UMapMemoryUsageCommandlet::Main(const TCHAR* Parms)
{
	guard(UMapMemoryUsageCommandlet::Main);

	SetupDLOTracking();

	ShowErrorCount = 1;
	GIsRequestingExit = 1;

	// find all maps
	TArray<FString> MapNames = GFileManager->FindFiles(TEXT("..\\XboxMaps\\*.LIN"), true, false);
	if (MapNames.Num() == 0)
	{
		appErrorf(TEXT("Failed to find any LIN files in ..\\XboxMaps\\"));
	}

	TArray<FMapMemoryInfo> MapMemoryInfos;
	// for each map
	for (INT i = 0; i < MapNames.Num(); i++)
	{
		INT InfoIndex = MapMemoryInfos.AddZeroed();
		// get the name without extension
		MapMemoryInfos(InfoIndex).MapName = MapNames(i).Left(MapNames(i).InStr(TEXT(".")));
		// get the size of the LIN
		MapMemoryInfos(InfoIndex).TotalSize += GFileManager->FileSize(*FString::Printf(TEXT("..\\XboxMaps\\%s.LIN"), *MapMemoryInfos(InfoIndex).MapName));
		// add the size of the XPRs
		MapMemoryInfos(InfoIndex).TotalSize += GFileManager->FileSize(*FString::Printf(TEXT("..\\XboxTextures\\%s.xpr"), *MapMemoryInfos(InfoIndex).MapName));
		MapMemoryInfos(InfoIndex).TotalSize += GFileManager->FileSize(*FString::Printf(TEXT("..\\XboxTextures\\%s_uma.xpr"), *MapMemoryInfos(InfoIndex).MapName));
		MapMemoryInfos(InfoIndex).TotalSize += GFileManager->FileSize(*FString::Printf(TEXT("..\\XboxTextures\\%s_pb.xpr"), *MapMemoryInfos(InfoIndex).MapName));
	}

	Sort<FMapMemoryInfo>(&MapMemoryInfos(0), MapMemoryInfos.Num());

	// print the results
	for (INT i = 0; i < MapMemoryInfos.Num(); i++)
	{
		warnf(TEXT("%s %d"), *MapMemoryInfos(i).MapName, MapMemoryInfos(i).TotalSize);
	}

	return 0;

	unguard;
}
IMPLEMENT_CLASS(UMapMemoryUsageCommandlet);