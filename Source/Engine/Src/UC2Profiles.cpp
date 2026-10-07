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
static INT GRememberTicks = 0;		// how often UC2RememberProfile looked (PROFILESTATE)

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
	GRememberTicks++;
	ReadRememberedProfile();

	FString Active = ActiveProfileName( Client->Viewports(0) );
	// Every change the poll sees, once (DevProfiles is not suppressed by default: the evidence a test reads).
	static FString LastSeen;
	if( appStrcmp(*Active, *LastSeen)!=0 )
	{
		debugf( NAME_Log, TEXT("Profiles: viewport 0 holds \"%s\" (was \"%s\"; remembered \"%s\"; %i viewport(s))"),
			*Active, *LastSeen, *GRememberedProfile, Client->Viewports.Num() );
		LastSeen = Active;
	}
	if( appStrcmp(*Active, *GRememberedProfile)==0 )
		return;

	GConfig->SetString( LastProfileSection, LastProfileKey, *Active, LastProfileIni );
	GConfig->Flush( 0, LastProfileIni );
	debugf( NAME_Log, TEXT("Profiles: remembered profile \"%s\" (was \"%s\")"), *Active, *GRememberedProfile );
	GRememberedProfile = Active;

	unguard;
}

/*-----------------------------------------------------------------------------
	D-4: whose name is it?

	With a saved profile active the player is named after the profile (GUIController.
	SetProfilePlayerName), so the Name of every URL the menus build is the PROFILE's name.
	UGameEngine::LoadMap writes each loaded URL's Name back to User.ini [DefaultPlayer]
	(Epic's PC code), which made the profile's name the player's "own" name from then on: the
	name a transient profile uses (D-4, GUI.u) and the name the web launcher reads back into
	its name field. LoadMap asks this first and leaves [DefaultPlayer] Name alone while a
	profile names the player.
-----------------------------------------------------------------------------*/

ENGINE_API UBOOL UC2ProfileNamesThePlayer( UClient* Client )
{
	return Client && Client->Viewports.Num() && ActiveProfileName( Client->Viewports(0) ).Len() > 0;
}

/*-----------------------------------------------------------------------------
	D-4: a local player with an active profile logs in under the profile's name.

	The menus put the profile's name in every URL they build (GameStartInfo.GetName), and
	GUIController.SetProfilePlayerName renames the player whenever the profile changes, but
	only once the GUI has bound that delegate (when a menu is first shown). A level loaded
	from the command line, by the web launcher's Play form (`DM-Deadbolt?Name=<launcher's
	name>`) or by a console `open` logged the player in under the URL's Name, so a profile
	made at the menus did not name the player in a map started directly (seen in
	tools/dev/d3-profiles.sh r2m once the profile's name stopped leaking into [DefaultPlayer]).
	MatchViewportsToActors spawns the primary viewport's PlayerController with a copy of the
	URL whose Name is the profile's (PlayerName: the underscore form the URLs use).
-----------------------------------------------------------------------------*/

ENGINE_API void UC2ProfileNameInURL( UViewport* Viewport, FURL& URL )
{
	guard(UC2ProfileNameInURL);
	UPlayerProfile* Profile = Viewport ? Viewport->Profile : NULL;
	if( !Profile || Profile->IsTransient() || !Profile->PlayerName.Len() )
		return;
	URL.AddOption( *(FString(TEXT("Name=")) + Profile->PlayerName) );
	unguard;
}

/*-----------------------------------------------------------------------------
	PROFILESTATE: what the remembering looks at, for tests and for the curious.
	Prints the remembered name, how often the tick looked, and every viewport's profile.
-----------------------------------------------------------------------------*/

ENGINE_API UBOOL UC2ProfilesExec( UClient* Client, const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UC2ProfilesExec);
	if( !ParseCommand(&Cmd,TEXT("PROFILESTATE")) )
		return 0;
	ReadRememberedProfile();
	Ar.Logf( TEXT("ProfileState: remembered \"%s\" (read %i), ticks %i, viewports %i"),
		*GRememberedProfile, GRememberedProfileRead, GRememberTicks, Client ? Client->Viewports.Num() : -1 );
	for( INT i=0; Client && i<Client->Viewports.Num(); i++ )
	{
		UViewport* V = Client->Viewports(i);
		UPlayerProfile* P = V ? V->Profile : NULL;
		Ar.Logf( TEXT("ProfileState: viewport %i %s profile %s name \"%s\" transient %i dirtymask %i"),
			i, V ? V->GetName() : TEXT("None"), P ? P->GetName() : TEXT("None"),
			P ? *P->ProfileName : TEXT(""), P ? (INT)P->IsTransient() : -1, P ? (INT)P->DirtyMask : -1 );
	}
	return 1;
	unguard;
}
