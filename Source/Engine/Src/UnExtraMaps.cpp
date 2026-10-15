/*=============================================================================
	UnExtraMaps.cpp: the maps a player added, in the map menus (ufront).

	The map menus show maplists only (Maplists.ini, one [<Acr>_<List>] section per tab), and a map's player counts
	come from Gametypes.ini (each game class's LevelData). A map dropped into Maps/ (or uploaded as a mod on the web)
	is in neither, so it appeared nowhere, and with no player counts it could not be picked with a set bot count.
	At boot, before the first level, this adds in memory only:

	- [<Acr>_Extra] Label="EXTRA", Maps=... for every game whose <Acr>_Full list takes the map's prefix (DM_Full
	  takes DM- and OD-; CTF_Full CTF-; ...): the menus' Extra tab (script patch set extra-maps).
	- LevelData=(Name="<Map>",RecommendedPlayers=4,MaxPlayers=8) in every Gametypes.ini section whose LevelData
	  lists maps of that prefix, unless the map already has a record there (a record the player adds wins).

	An added map is one no section of Maplists.ini names (the _Extra sections excepted). Nothing is written: the two
	files keep the Dirty flag they had, and an _Extra section a later save wrote is regenerated each boot. Epic's
	Xbox build did the same for downloaded maps (FContentManager::UpdateMapLists, from <Map>.ini [MapListsData]).
=============================================================================*/

#include "EnginePrivate.h"
#include "FConfigCacheIni.h"

enum { EXTRA_RECOMMENDED_PLAYERS = 4, EXTRA_MAX_PLAYERS = 8 };

static const TCHAR* MaplistsIni  = TEXT("Maplists.ini");
static const TCHAR* GametypesIni = TEXT("Gametypes.ini");

// "DM-Deck17" -> "DM"; "" when the name has no prefix (Entry, Menu).
static FString MapPrefix( const FString& Map )
{
	INT Dash = Map.InStr( TEXT("-") );
	return Dash>0 ? Map.Left( Dash ) : FString();
}

static UBOOL EndsWith( const FString& S, const TCHAR* Suffix )
{
	INT Len = appStrlen( Suffix );
	return S.Len()>=Len && appStricmp( *S.Right( Len ), Suffix )==0;
}

// The map name of a LevelData entry: (Name="DM-Apex",RecommendedPlayers=2,MaxPlayers=6) -> DM-Apex.
static FString LevelDataName( const FString& Entry )
{
	INT At = Entry.Caps().InStr( TEXT("NAME=") );
	if( At<0 )
		return FString();
	FString Rest = Entry.Mid( At+5 );
	if( Rest.Left(1)==TEXT("\"") )
		Rest = Rest.Mid( 1 );
	INT End = 0;
	while( End<Rest.Len() && Rest[End]!='"' && Rest[End]!=',' && Rest[End]!=')' )
		End++;
	return Rest.Left( End );
}

// TArray<FString>: AddItem/AddUniqueItem are for types without a destructor (they assign into unconstructed memory);
// a string is added by constructing it in place, as Epic's code does.
static void AddString( TArray<FString>& Array, const FString& S )
{
	new(Array) FString( S );
}
static void AddUniqueString( TArray<FString>& Array, const FString& S )
{
	if( Array.FindItemIndex( S )==INDEX_NONE )
		new(Array) FString( S );
}

struct FExtraMapsGame
{
	FString			Acronym;	// DM, TDM, NS, SV, CTF, OD (the <Acr>_Full sections)
	TArray<FString>	Prefixes;	// the prefixes of the maps its _Full list takes
	TArray<FString>	Maps;		// the added maps it takes
};

// The game classes read LevelData when they load, and the native packages (UCGameplay too) load at startup, before
// this. So every loaded GameInfo class gets it again, parents first: from its own section when it has LevelData there
// (as LoadConfig reads it), else its parent's (what a subclass without the key inherited when it loaded).
static void RefreshLevelData()
{
	UArrayProperty* Prop = FindField<UArrayProperty>( AGameInfo::StaticClass(), TEXT("LevelData") );
	if( !Prop || !(Prop->PropertyFlags & CPF_Config) )
	{
		debugf( NAME_Init, TEXT("ExtraMaps: GameInfo has no config LevelData: player counts of added maps are not set") );
		return;
	}
	TArray<UClass*> Classes;
	TArray<INT> Depths;
	for( TObjectIterator<UClass> It; It; ++It )
		if( It->IsChildOf( AGameInfo::StaticClass() ) && It->GetDefaultObject() )
		{
			INT Depth = 0;
			for( UClass* C=*It; C; C=C->GetSuperClass() )
				Depth++;
			// Insertion by depth: a parent is refreshed before its subclasses.
			INT At = 0;
			while( At<Depths.Num() && Depths(At)<=Depth )
				At++;
			Classes.Insert( At );
			Classes(At) = *It;
			Depths.Insert( At );
			Depths(At) = Depth;
		}
	INT Size = Prop->Inner->ElementSize, Refreshed = 0;
	for( INT i=0; i<Classes.Num(); i++ )
	{
		UClass* Class = Classes(i);
		FArray* Value = (FArray*)((BYTE*)Class->GetDefaultObject() + Prop->Offset);
		TArray<FString> List;
		GConfig->GetArray( Class->GetPathName(), TEXT("LevelData"), List, *Class->ClassConfigName );
		if( List.Num() )
		{
			Prop->DestroyValue( Value );
			Value->AddZeroed( Size, List.Num() );
			for( INT j=0; j<List.Num(); j++ )
				Prop->Inner->ImportText( *List(j), (BYTE*)Value->GetData() + j*Size, 0 );
		}
		else if( Class!=AGameInfo::StaticClass() && Class->GetSuperClass() && Class->GetSuperClass()->GetDefaultObject() )
			Prop->CopyCompleteValue( (BYTE*)Value, (BYTE*)Class->GetSuperClass()->GetDefaultObject() + Prop->Offset );
		else
			continue;
		Refreshed++;
	}
	debugf( NAME_Init, TEXT("ExtraMaps: LevelData refreshed in %d loaded game class(es)"), Refreshed );
}

void RegisterExtraMaps()
{
	guard(RegisterExtraMaps);
	FConfigCacheIni* Ini = (FConfigCacheIni*)GConfig;
	FConfigFile* Maplists = Ini ? Ini->Find( MaplistsIni, 0 ) : NULL;
	if( !Maplists )
	{
		debugf( NAME_Init, TEXT("ExtraMaps: no %s, no Extra map lists"), MaplistsIni );
		return;
	}
	UBOOL MaplistsDirty = Maplists->Dirty;

	// The maps the game's lists name, and what each game's _Full list takes.
	TArray<FString> Listed, ExtraSections;
	TArray<FExtraMapsGame> Games;
	for( FConfigFile::TIterator It(*Maplists); It; ++It )
	{
		const FString& Section = It.Key();
		if( EndsWith( Section, TEXT("_Extra") ) )
		{
			AddString( ExtraSections, Section );
			continue;
		}
		TArray<FString> Maps;
		GConfig->GetArray( *Section, TEXT("Maps"), Maps, MaplistsIni );
		FExtraMapsGame* Game = NULL;
		if( EndsWith( Section, TEXT("_Full") ) )
		{
			Game = new(Games) FExtraMapsGame;
			Game->Acronym = Section.Left( Section.Len()-5 );
		}
		for( INT i=0; i<Maps.Num(); i++ )
		{
			AddUniqueString( Listed, Maps(i) );
			FString Prefix = MapPrefix( Maps(i) );
			if( Game && Prefix.Len() )
				AddUniqueString( Game->Prefixes, Prefix );
		}
	}

	// The maps on the map paths that no list names and that some game takes.
	TArray<FString> Added;
	FString MapWildcard = FString(TEXT("*.")) + FURL::DefaultMapExt;
	for( INT p=0; p<GSys->Paths.Num(); p++ )
	{
		if( !EndsWith( GSys->Paths(p), *MapWildcard ) )
			continue;
		TArray<FString> Files = GFileManager->FindFiles( *GSys->Paths(p), 1, 0 );
		for( INT f=0; f<Files.Num(); f++ )
		{
			FString Map = Files(f).Left( Files(f).Len() - FURL::DefaultMapExt.Len() - 1 );
			FString Prefix = MapPrefix( Map );
			if( !Prefix.Len() || Listed.FindItemIndex( Map )!=INDEX_NONE || Added.FindItemIndex( Map )!=INDEX_NONE )
				continue;
			UBOOL Taken = 0;
			for( INT g=0; g<Games.Num(); g++ )
				if( Games(g).Prefixes.FindItemIndex( Prefix )!=INDEX_NONE )
				{
					AddString( Games(g).Maps, Map );
					Taken = 1;
				}
			if( Taken )
				AddString( Added, Map );
		}
	}

	// [<Acr>_Extra], and an empty one where a saved section names maps that are gone.
	for( INT g=0; g<Games.Num(); g++ )
	{
		FString Section = Games(g).Acronym + TEXT("_Extra");
		UBOOL Existed = ExtraSections.FindItemIndex( Section )!=INDEX_NONE;
		if( !Games(g).Maps.Num() && !Existed )
			continue;
		GConfig->SetArray( *Section, TEXT("Maps"), Games(g).Maps, MaplistsIni );
		GConfig->SetString( *Section, TEXT("Label"), TEXT("EXTRA"), MaplistsIni );
		GConfig->SetString( *Section, TEXT("MaplistTitle"), TEXT("Extra Maps"), MaplistsIni );
		if( Games(g).Maps.Num() )
		{
			FString Names;
			for( INT i=0; i<Games(g).Maps.Num(); i++ )
			{
				if( i )
					Names += TEXT(", ");
				Names += Games(g).Maps(i);
			}
			debugf( NAME_Init, TEXT("ExtraMaps: %s: %s"), *Section, *Names );
		}
	}
	Maplists->Dirty = MaplistsDirty;

	// Player counts: each Gametypes.ini section that has LevelData of the map's prefix gets a record for it.
	FConfigFile* Gametypes = Ini->Find( GametypesIni, 0 );
	UBOOL Touched = 0;
	if( Gametypes && Added.Num() )
	{
		UBOOL GametypesDirty = Gametypes->Dirty;
		TArray<FString> Sections;
		for( FConfigFile::TIterator It(*Gametypes); It; ++It )
			AddString( Sections, It.Key() );
		for( INT s=0; s<Sections.Num(); s++ )
		{
			TArray<FString> LevelData, Names, Prefixes;
			if( !GConfig->GetArray( *Sections(s), TEXT("LevelData"), LevelData, GametypesIni ) )
				continue;
			for( INT i=0; i<LevelData.Num(); i++ )
			{
				AddString( Names, LevelDataName( LevelData(i) ) );
				FString Prefix = MapPrefix( Names.Last() );
				if( Prefix.Len() )
					AddUniqueString( Prefixes, Prefix );
			}
			INT Before = LevelData.Num();
			for( INT m=0; m<Added.Num(); m++ )
				if( Prefixes.FindItemIndex( MapPrefix( Added(m) ) )!=INDEX_NONE && Names.FindItemIndex( Added(m) )==INDEX_NONE )
					AddString( LevelData, FString::Printf( TEXT("(Name=\"%s\",RecommendedPlayers=%d,MaxPlayers=%d)"), *Added(m),
						(INT)EXTRA_RECOMMENDED_PLAYERS, (INT)EXTRA_MAX_PLAYERS ) );
			if( LevelData.Num()==Before )
				continue;
			GConfig->SetArray( *Sections(s), TEXT("LevelData"), LevelData, GametypesIni );
			debugf( NAME_Init, TEXT("ExtraMaps: [%s] %d LevelData record(s) added"), *Sections(s), LevelData.Num()-Before );
			Touched = 1;
		}
		Gametypes->Dirty = GametypesDirty;
	}
	if( Touched )
		RefreshLevelData();
	debugf( NAME_Init, TEXT("ExtraMaps: %d added map(s)"), Added.Num() );
	unguard;
}
