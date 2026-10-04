/*=============================================================================
	UnLadderRung.cpp:	Custom implementation of LoadConfig for single player ladders.
	Copyright 1997-2004 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Ron Prestenback
=============================================================================*/

#include "UCGameplay.h"

IMPLEMENT_CLASS(UTournamentLadderRung);

#define INIPATH FString(TEXT("Ladders"))

UBOOL UTournamentLadderRung::ParseBaseSections( FString& CharacterName, FString& LadderName, FString& RungIndex )
{
	guard(UTournamentLadderRung::ParseBaseSections);

	FString FullSection = GetName();

	/* FullSection should look either
	a. Ascension_0 (for ascension or challenges)
	b. Anubis_Tournament_0 (for tournament)
	*/

	INT pos = FullSection.InStr(TEXT("_"),1);
	if ( pos == INDEX_NONE )
		return 0;

	RungIndex = FullSection.Mid(pos+1);
	LadderName = FullSection.Left(pos);

	pos = LadderName.InStr(TEXT("_"));
	if ( pos == INDEX_NONE )
	{
		// this is an ascension or challenges ladder, so there won't be a character name
		CharacterName = TEXT("");
		return 1;
	}

	CharacterName = LadderName.Left(pos);
	LadderName = LadderName.Mid(pos+1);
	return 1;

	unguard;
}

void UTournamentLadderRung::LoadConfig( UBOOL PropagateIgnored, UClass* ClassIgnored, const TCHAR* FilenameIgnored )
{
	guard(UTournamentLadderRung::LoadConfig);

	if ( !((GetClass()->ClassFlags & CLASS_PerObjectConfig) && GetIndex()!=INDEX_NONE) )
	{
		Super::LoadConfig(PropagateIgnored,ClassIgnored,FilenameIgnored);
		return;
	}

	FString CharacterName, LadderName, RungIndex;

	// Parse my object name into its components
	if ( !ParseBaseSections(CharacterName, LadderName, RungIndex) )
		return;

	// Build a list of properties, so that in LoadRungConfig, we don't need to use the TFieldIterator...we can just use
	// the TArray
	TArray<UProperty*> Properties;
	for ( TFieldIterator<UProperty,CLASS_IsAUProperty> It(GetClass()); It; ++It )
		if( It->PropertyFlags & CPF_Config )
			Properties.AddItem(*It);

	FString Filename = INIPATH * (CharacterName.Len() > 0 ? CharacterName : LadderName);
	FString BaseSection = CharacterName.Len() ? CharacterName + TEXT("_") + LadderName : LadderName,
		Section = BaseSection + TEXT("_") + RungIndex;

	// First load values from the base section (i.e. Anubis_Ascension)
	LoadRungConfig( Filename, BaseSection, Properties );

	// Then load all values from this object's section (i.e. Anubis_Ascension_0)
	LoadRungConfig( Filename, Section, Properties );

	unguard;
}


void UTournamentLadderRung::LoadRungConfig( FString& Filename, FString& SectionName, TArray<UProperty*>& Properties )
{
	guard(UTournamentLadderRung::LoadRungConfig);

#ifdef _DEBUG
	debugf(NAME_DevSave, TEXT("*(%s) '%s' loading configuration from %s"), GetClass()->GetName(), GetName(), *Filename);
#endif

	UProperty* Prop = NULL;
	UArrayProperty* ArrayProp = NULL;

	for ( INT i = 0; i < Properties.Num(); i++ )
	{
		Prop = Properties(i);
		ArrayProp = Cast<UArrayProperty>(Prop);

#ifdef _DEBUG
		debugf(NAME_DevSave, TEXT("*   Loading value for %s from [%s]"), Prop->GetName(), *SectionName);
#endif

		if ( ArrayProp )
		{
			// We want direct access to the section of the ini containing this property
			TMultiMap<FName,FString>* Sec = GConfig->GetSectionPrivate( *SectionName, 0, 1, *Filename );
			if( Sec )
			{
				TArray<FString> List;

				// Get all values for this array that exist in the current section
				Sec->MultiFind( ArrayProp->GetFName(), List );

				// Determine the address of the FArray corresponding to this UArrayProperty
				FArray* Array  = (FArray*)((BYTE*)this + ArrayProp->Offset);

				// Determine the size of the array's type
				INT     Size = ArrayProp->Inner->ElementSize;

				// Clear the current value of the array
				ArrayProp->DestroyValue( Array );

				// Preallocate the size necessary to store all of the new elements
				Array->AddZeroed( Size, List.Num() );

				// Import each value, in reverse order (since TIterators (which is used by FConfigFile to build it TMultiMap)
				// return values in the opposite order from which they're declared in the .ini)
				for( INT i=List.Num()-1,c=0; i>=0; i--,c++ )
					ArrayProp->Inner->ImportText( *List(i), (BYTE*)Array->GetData() + c*Size, 0 );
			}
		}

		else 
		{
			TCHAR TempKey[256], Value[1024]=TEXT("");
			const TCHAR* Key = Prop->ArrayDim == 1 ? Prop->GetName() : TempKey;

			for( INT i=0; i<Prop->ArrayDim; i++ )
			{
				// If this property is a static array, add the subscript to the property name
				if( Prop->ArrayDim!=1 )
					appSprintf( TempKey, TEXT("%s[%i]"), Prop->GetName(), i );

				// Import the value from the .ini file
				if( GConfig->GetString( *SectionName, Key, Value, ARRAY_COUNT(Value), *Filename ) )
					Prop->ImportText( Value, (BYTE*)this + Prop->Offset + i*Prop->ElementSize, 0 );
			}
		}
	}

	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
