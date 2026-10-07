/*=============================================================================
	UC2Profiles.cpp: the player starts in the profile they last played with.
	uc2 port (D-3); not Epic's.

	Epic's game starts every viewport on a transient "no profile" profile (UViewport::Init)
	and only ever picks a saved one by itself when a page that needs a profile opens and
	exactly one exists (UC2PageBase.ForceProfileLoad). A player who created a profile
	found "No Profile" again at the next start, with the default settings, and concluded
	that nothing had been saved (owner's review, D-3). Nothing in Epic's code remembers a
	profile across runs (the Xbox keeps only the save games themselves).

	What this file adds, for the primary player (viewport 0) only:
	- REMEMBER: once per tick, the name of the profile the primary viewport holds (empty
	  while it holds the transient "no profile" one) is compared with the remembered one;
	  when it differs it is written to User.ini [Profiles] LastProfile and User.ini is
	  flushed. Comparing state instead of hooking script covers every way a profile
	  changes (select, create, copy, rename, delete, "no profile") without a script change.
	- START WITH IT: UGameEngine::Init, right after it creates the first viewport, loads the
	  remembered profile into that viewport's profile object the way
	  APlayerController::execLoadProfile does (UPlayerProfile::LoadProfile, then
	  SetProfileName); the PlayerController takes it over at its first SetGamepadID ->
	  SetPlayerOwner -> Initialize, as for any loaded profile. A remembered profile that
	  no longer exists or does not load leaves the transient profile (logged), and the
	  next tick forgets it.

	User.ini is per-player state and the web host persists it (ufront: the engine-owned
	inis, written on `flushconfig`); the frontend reads the same key to know that a profile
	names the player (D-4). Notes: docs/porting/analysis/profiles.md.
=============================================================================*/

#include "EnginePrivate.h"
#include "ULoadSaveManager.h"

static const TCHAR* LastProfileSection	= TEXT("Profiles");
static const TCHAR* LastProfileKey		= TEXT("LastProfile");
static const TCHAR* LastProfileIni		= TEXT("User");

// The value User.ini holds (as last read or written by this file); compared case-sensitively.
static FString GRememberedProfile;
static UBOOL GRememberedProfileRead = 0;

static void ReadRememberedProfile()
{
	if( !GRememberedProfileRead && GConfig )
	{
		FString Value;
		GConfig->GetString( LastProfileSection, LastProfileKey, Value, LastProfileIni );
		GRememberedProfile = Value;
		GRememberedProfileRead = 1;
	}
}

// The name of the profile a viewport holds, or "" for the transient "no profile" profile.
static FString ActiveProfileName( UViewport* Viewport )
{
	UPlayerProfile* Profile = Viewport ? Viewport->Profile : NULL;
	if( !Profile || Profile->IsTransient() )
		return FString();
	return Profile->ProfileName;
}

ENGINE_API void UC2StartWithLastProfile( UViewport* Viewport )
{
	guard(UC2StartWithLastProfile);

	ReadRememberedProfile();
	if( !Viewport || !Viewport->Profile || GIsEditor || !GRememberedProfile.Len() )
		return;

	ULoadSaveManager* Manager = Cast<ULoadSaveManager>(GLoadSaveManager);
	if( !Manager || !Manager->ExistsGameObject(GRememberedProfile) )
	{
		debugf( NAME_Init, TEXT("Profiles: the last profile \"%s\" does not exist; starting with no profile"), *GRememberedProfile );
		return;
	}
	if( !Viewport->Profile->LoadProfile(GRememberedProfile) )
	{
		debugf( NAME_Init, TEXT("Profiles: the last profile \"%s\" did not load; starting with no profile"), *GRememberedProfile );
		return;
	}
	// As APlayerController::execLoadProfile: the loader does not store the names in the file.
	Viewport->Profile->eventSetProfileName( GRememberedProfile );
	debugf( NAME_Init, TEXT("Profiles: started with the last profile \"%s\""), *GRememberedProfile );

	unguard;
}

ENGINE_API void UC2RememberProfile( UClient* Client )
{
	guard(UC2RememberProfile);

	if( !Client || !Client->Viewports.Num() || GIsEditor || !GConfig )
		return;
	ReadRememberedProfile();

	FString Active = ActiveProfileName( Client->Viewports(0) );
	if( appStrcmp(*Active, *GRememberedProfile)==0 )
		return;

	GConfig->SetString( LastProfileSection, LastProfileKey, *Active, LastProfileIni );
	GConfig->Flush( 0, LastProfileIni );
	debugf( NAME_Log, TEXT("Profiles: remembered profile \"%s\" (was \"%s\")"), *Active, *GRememberedProfile );
	GRememberedProfile = Active;

	unguard;
}

