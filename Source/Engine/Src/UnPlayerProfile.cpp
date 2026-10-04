

#include "EnginePrivate.h"
#include "FConfigCacheIni.h"
#include "ULoadSaveManager.h"

IMPLEMENT_CLASS(UGameStartInfoBase);
IMPLEMENT_CLASS(UPlayerProfile);

static INT Compare( FString& A, FString& B )
{
	return appStricmp(*A, *B);
}

// =====================================================================================================================
// =====================================================================================================================
//  UPlayerProfile Unrealscript Interface
// =====================================================================================================================
// =====================================================================================================================
void UPlayerProfile::execSpaceAvailable( FFrame& Stack, RESULT_DECL )
{
	guard(UPlayerProfile::execSpaceAvailable);

	P_GET_INT_REF(out_BlocksNeeded);
	P_FINISH;

	DWORD* dwBlocksNeeded = (DWORD*)out_BlocksNeeded;

	ULoadSaveManager* mgr = Cast<ULoadSaveManager>(GLoadSaveManager);
	*(UBOOL*)Result = mgr->IsEnoughSpaceAvailable(dwBlocksNeeded);

	unguardexec;
}

void UPlayerProfile::execMergeStats( FFrame& Stack, RESULT_DECL )
{
	guard(UPlayerProfile::execMergeStats);

	P_GET_OBJECT(UPlayerStats,SessionStats);
	P_GET_UBOOL(bRungData);
	P_FINISH;

	MergeStatData(SessionStats, bRungData);
	unguardexec;
}

void UPlayerProfile::execListProfiles( FFrame& Stack, RESULT_DECL )
{
	guard(UPlayerProfile::execListProfiles);
	
	P_GET_TARRAY_REF(outProfileList, FString);
	P_FINISH;

	*(UBOOL *)Result = ListProfiles(outProfileList);

	unguardexec;
}

void UPlayerProfile::execSaveProfile( FFrame& Stack, RESULT_DECL )
{
	guard(UPlayerProfile::execSaveProfile);

	P_FINISH;

	*(UBOOL *)Result = SaveProfile();
	
	unguardexec;
}

void UPlayerProfile::execDeleteProfile( FFrame& Stack, RESULT_DECL )
{
	guard(UPlayerProfile::execDeleteProfile);

	P_GET_STR(profileName);
	P_FINISH;

	*(UBOOL*)Result = UPlayerProfile::DeleteProfile(profileName);

	unguardexec;
}

// =====================================================================================================================
// =====================================================================================================================
//  UPlayerProfile Implementation
// =====================================================================================================================
// =====================================================================================================================

void UPlayerProfile::Serialize( FArchive& Ar )
{
	guard(UPlayerProfile::Serialize);

	Super::Serialize(Ar);
	if ( Stats && (Ar.IsLoading() || Ar.IsSaving()) )
	{
		Stats->Serialize(Ar);
	}

	unguard;
}

void UPlayerProfile::Destroy()
{
	guard(UPlayerProfile::Destroy);

	if (Stats != NULL)
	{
		delete Stats;
		Stats = NULL;
	}

	Super::Destroy();

	unguard;
}

/*==========================================================================================================
function	UPlayerProfile::MergeStatData
Desc:		Updates the profile's stats object with the stats data from the current gaming session.

input:		SessionStats
				the stats object that contains the stats data from the current match.

last modified by:	Ron Prestenback
==========================================================================================================*/
void UPlayerProfile::MergeStatData( UPlayerStats* SessionStats, UBOOL bRungData ) 
{
	guard(UPlayerProfile::MergeStatData);

	if ( Stats && SessionStats )
	{
		if ( bRungData )
			Stats->UpdateRungs(*SessionStats);
		else Stats->UpdateStats(*SessionStats);

		SetDirty();
	}

	unguard;
}



/*
	UPlayerProfile::ListProfiles

	Queries the LoadSaveManager for the names of all existing profiles
*/
UBOOL UPlayerProfile::ListProfiles(TArray<FString>* outProfileList )
{
	guard(UPlayerProfile::ListProfiles);

	ULoadSaveManager* LoadSaveManager = CastChecked<ULoadSaveManager>(GLoadSaveManager);
	UBOOL Result = LoadSaveManager->ListGameObjects(outProfileList);
	if ( Result )
		Sort( &((*outProfileList)(0)), outProfileList->Num() );

	return Result;

	unguard;
}

/*
	UPlayerProfile::LoadProfile

	Serializes the profile from disk.  Returns true if the profile was successfully loaded.
 */
UBOOL UPlayerProfile::LoadProfile(const FString& profileName) 
{
	guard(UPlayerProfile::LoadProfile);

	ULoadSaveManager* LoadSaveManager = CastChecked<ULoadSaveManager>(GLoadSaveManager);
	if ( LoadSaveManager->LoadGameObject(profileName,this) )
	{
		SetClean();
		SetTransient(false);
		return 1;
	}
	else
		debugf(NAME_Warning, TEXT("Unable to load profile \"%s\""), *profileName);

	return 0;

	unguard;
}


/*
 Serializes the profile to disk.

 return:	Returns true if the profile was successfully saved. Attempting to save a transient
			profile always returns true, though it doesn't actually save the profile.

 */
UBOOL UPlayerProfile::SaveProfile()
{
	guard(UPlayerProfile::SaveProfile);

	ULoadSaveManager* LoadSaveManager = CastChecked<ULoadSaveManager>(GLoadSaveManager);
	UBOOL retval = true;

	// If this profile is marked as dirty, and this profile is not a transient profile,
	// save the profile to disk
	if ( IsDirty() && !IsTransient() )
		retval = LoadSaveManager->SaveGameObject(ProfileName, this);

	// Clear the dirty bit, preserving the transient bit
	SetClean();
	return retval;

	unguard;
}

/* 
	UPlayerProfile::DeleteProfile
	Removes the stored version of the profile specified by profileName.
*/
UBOOL UPlayerProfile::DeleteProfile(const FString& profileName)
{
	guard(UPlayerProfile::DeleteProfile);

	ULoadSaveManager* LoadSaveManager = CastChecked<ULoadSaveManager>(GLoadSaveManager);
	return LoadSaveManager->DeleteGameObject(profileName);

	unguard;
}

// =====================================================================================================================
// =====================================================================================================================
//  The End.
// =====================================================================================================================
// =====================================================================================================================

