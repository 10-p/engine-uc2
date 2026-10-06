/* 
   Implementation file for ULoadSaveManger class
   Some fraction of code lifted from Championship
   scion bcl
*/
#include "EnginePrivate.h"
#include "ULoadSaveManager.h"

IMPLEMENT_CLASS(ULoadSaveManager);

// engine-uc2, WEB ONLY: one flat file per profile, Profiles/<name>.PRO, instead of Epic's one directory per
// profile (Profiles/<name>/AVARIS_PROFILE.PRO, the Xbox save-game layout). The web host keeps a player's files
// with the target's `userFiles` list (ufrontend glue/src/target/user-files.js): a directory plus a glob in the
// LAST path segment only, listed every few seconds. `Profiles/*/AVARIS_PROFILE.PRO` is not a legal entry (the
// compiler throws), so profiles in their own directories could never be persisted; `Profiles/*.PRO` is. The
// bytes inside are the same as natively; the native layout is unchanged (UT2004's web profiles are flat for the
// same reason: Saves/*.uvx). Notes: docs/porting/analysis/web-ini-saves.md.
#ifndef UC2_FLAT_PROFILE_FILES	// (-DUC2_FLAT_PROFILE_FILES=1 tries the web layout on a native build)
	#ifdef __EMSCRIPTEN__
		#define UC2_FLAT_PROFILE_FILES 1
	#else
		#define UC2_FLAT_PROFILE_FILES 0
	#endif
#endif
#define FLAT_PROFILE_EXT TEXT(".PRO")

ULoadSaveManager::ULoadSaveManager()
: USubsystem(), GameProfileClass(NULL)
{
}

void ULoadSaveManager::Init(UClient* inClient)
{
	guard(ULoadSaveManager::Init);
	Client = inClient;


	/* rjp -- fix for profiles not loading 
	          Passing in 'TEXT("ini:Engine.Engine.GameProfileClass")' as the classname parameter to StaticLoadObject
			  results in ResolveName correctly processing and retrieving the value of that ini property.

			  The problem is that ResolveName() uses appStaticString1024() to "allocate" memory to hold the value of the 
			  resolved name.  The TCHAR*& parameter passed into ResolveName() is then assigned to this static area of memory
			  If ResolveName() then loads a package (as a result of passing in 1 for the "Create" parameter), LoadLocalized() &
			  LoadConfig() (which both use appStaticString1024() extensively) are called for each class in the package.

			  If appStaticString1024() is called 256 times or more during the package creation callstack from ResolveName(),
			  the memory that was previously storing the result now contains some arbitrary string

			  If the object that called ResolveName() needs to use the value of the TCHAR*& that was passed in, indeterminate
			  results may occur...random failures for things that shouldn't be failing
	   -- */

	FString ProfileClassName(TEXT(""));

	// Avoid the static string problem entirely by grabbing the value of GameProfileClass ourselves
	if ( !GConfig->GetString(TEXT("Engine.Engine"), TEXT("GameProfileClass"), ProfileClassName) )
		ProfileClassName = TEXT("Engine.PlayerProfile");

	GameProfileClass = Cast<UClass>(StaticLoadObject( UClass::StaticClass(), NULL, *ProfileClassName, NULL, 0, NULL ));  
	GLoadSaveManager = this;

	unguard;
}

UBOOL ULoadSaveManager::Exec(const TCHAR* Cmd, FOutputDevice& Ar)
{
	guard(ULoadSaveManager::Exec);

	if (ParseCommand(&Cmd,TEXT("LOADSAVE")))
	{
		return 1;
	}
	return 0;
	unguard;
}

void ULoadSaveManager::Serialize(FArchive& Ar)
{
	guard(ULoadSaveManager::Serialize);

	Super::Serialize(Ar);

	unguard;
}

void ULoadSaveManager::Destroy()
{
	guard(ULoadSaveManager::Destroy);

	Super::Destroy();
	unguard;
}

UBOOL ULoadSaveManager::LoadGameObject( const FString& profileName, UObject* Profile )
{
	guard(ULoadSaveManager::LoadGameObject);

	check(GameProfileClass);

	if ( Profile == NULL )
	{
		debugf(NAME_Warning, TEXT("Attempting to load a NULL profile!!"));
		return 0;
	}

	if (ExistsGameObject(profileName))
	{
#if UC2_FLAT_PROFILE_FILES
		FString pathName = GetProfileRoot() + profileName + FLAT_PROFILE_EXT;
#else
		FString pathName = GetProfilePath(profileName) + SAVE_FILE_NAME;
#endif
		FArchiveSaveGame* SaveGame = new FArchiveSaveGame(GFileManager->CreateFileReader(*pathName));
		if ( SaveGame == NULL )
			return 0;

		Profile->Serialize(*SaveGame);
		UBOOL bSuccess = SaveGame->Close();

		delete SaveGame;
		SaveGame = NULL;
		return bSuccess;
	}

	return 0;
	unguard;
} // LoadGameObject

UBOOL ULoadSaveManager::DeleteGameObject(const FString & profileName)
{
	guard(ULoadSaveManager::DeleteGameObject);

#if UC2_FLAT_PROFILE_FILES
	return GFileManager->Delete(*(GetProfileRoot() + profileName + FLAT_PROFILE_EXT), true, true);
#else
	FString pathName = GetProfilePath(profileName);
	return GFileManager->DeleteDirectory(*pathName, true, true);
#endif

	unguard;
} // DeleteGameObject

UBOOL ULoadSaveManager::SaveGameObject( const FString & profileName, UObject * saveGameObject )
{
	guard(ULoadSaveManager::SaveGameObject);

#if UC2_FLAT_PROFILE_FILES
	FString pathName = GetProfileRoot() + profileName + FLAT_PROFILE_EXT;
	if (EnsureDirectory(GetProfileRoot()))
	{
#else
	FString pathName = GetProfilePath(profileName);
	if (EnsureDirectory(GetProfileRoot()) && EnsureDirectory(pathName)) 
	{
		pathName += SAVE_FILE_NAME;
#endif

		FArchiveSaveGame* SaveGameAr = new FArchiveSaveGame(GFileManager->CreateFileWriter(*pathName));
		if ( SaveGameAr == NULL )
			return false;

		saveGameObject->Serialize(*SaveGameAr);
		UBOOL bSuccess = SaveGameAr->Close();
		delete SaveGameAr;

		SaveGameAr = NULL;

		return bSuccess;
	}
	return false;

	unguard;
} // SaveGameObject

UBOOL ULoadSaveManager::ListGameObjects(TArray<FString> *ProfileName )
{
	guard(ULoadSaveManager::ListGameObjects);

	if (!EnsureDirectory(GetProfileRoot()))
	{
		debugf(NAME_Warning, TEXT("Unable to ensure \"%s\""), *GetProfileRoot());
		return false;
	}

#if UC2_FLAT_PROFILE_FILES
	FString WildcardedName = GetProfileRoot() + TEXT("*") + FLAT_PROFILE_EXT;
	*ProfileName = GFileManager->FindFiles( *WildcardedName, true /*Files*/, false /*Directories*/ );
	for (INT i=0; i < ProfileName->Num(); i++)
		(*ProfileName)(i) = (*ProfileName)(i).Left( (*ProfileName)(i).Len() - appStrlen(FLAT_PROFILE_EXT) );
#else
	FString WildcardedName = GetProfileRoot() + TEXT("*");
	*ProfileName = GFileManager->FindFiles( *WildcardedName, false /*Files*/, true /*Directories*/ );
#endif

#ifdef DO_SLOW
	for (INT i=0; i < ProfileName->Num(); i++)
	{
		debugf(TEXT("Profile[%d] = \"%s\""), i, *(*ProfileName)(i));
	}
#endif
	return true;

	unguard;
} // ListGameObjects

// ============================================================================

UBOOL ULoadSaveManager::IsEnoughSpaceAvailable( DWORD* BlocksNeeded ) const
{
	return true;
}

UBOOL ULoadSaveManager::ExistsGameObject(const FString & profileName) const
{
	guard(ULoadSaveManager::ExistsGameObject);
#if UC2_FLAT_PROFILE_FILES
	return GFileManager->FileSize(*(GetProfileRoot() + profileName + FLAT_PROFILE_EXT)) >= 0;
#else
	return ExistsDirectory(GetProfilePath(profileName));
#endif
	unguard;	
}

UBOOL ULoadSaveManager::ExistsDirectory(const FString & pathName) const
{
	return GFileManager->Exists(*pathName);
}

FString ULoadSaveManager::GetProfileRoot() const
{
	return FString::Printf(TEXT("%s%sProfiles%s"), *GFileManager->GetDefaultDirectory(), PATH_SEPARATOR, PATH_SEPARATOR);
}

UBOOL ULoadSaveManager::EnsureDirectory(const FString & pathName)
{
	return GFileManager->MakeDirectory(*pathName, true);
}

FString ULoadSaveManager::GetProfilePath(const FString & profileName) const
{
	return GetProfileRoot() + profileName + PATH_SEPARATOR;
}

// ============================================================================

