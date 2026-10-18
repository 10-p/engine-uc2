/*=============================================================================
	UnExtraMaps.cpp: the maps a player added, in the map menus.

	The map menus show maplists only (Maplists.ini, one [<Acr>_<List>] section per tab), and a map's player counts
	come from Gametypes.ini (each game class's LevelData). A map dropped into Maps/ (or uploaded as a mod on the web)
	is in neither, so it would appear nowhere, and without player counts it could not be picked with a set bot count.
	At boot, before the first level, this adds in memory only:

	- [<Acr>_Extra] Label="EXTRA", Maps=... for every game whose <Acr>_Full list takes the map's prefix (DM_Full
	  takes DM- and OD-; CTF_Full CTF-; ...): the menus' Extra tab (script patch set extra-maps).
	- LevelData=(Name="<Map>",RecommendedPlayers=4,MaxPlayers=8) in every Gametypes.ini section whose LevelData
	  lists maps of that prefix, unless the map already has a record there (a record the player adds wins).

	A map may say more in its own System/<Map>.ini, Epic's format for the Xbox's downloaded maps
	(FContentManager::UpdateMapLists/UpdateMapLimits): [MapLimitsData] <GameClass>=RecommendedPlayers=6,MaxPlayers=8
	(its player counts, for those game classes) and [MapListsData] MapLists=DM_Small (more lists than Extra to join).

	An added map is one no section of Maplists.ini names (the _Extra sections excepted). Nothing is written: the keys
	go into the config cache's copy of the two files, and again every time the cache reads them from disk
	(GConfigFileLoaded, Core.h): CollectGarbage empties the whole cache (GConfig->UnloadAll, the names it holds may
	be gone), so an addition made once at boot would be lost at the first level change. Epic's Xbox build did the same for
	downloaded maps by writing merged copies to its cache partition (FContentManager::UpdateMapLists/UpdateMapLimits).
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

// A key the engine adds to a file: its values replace the key's (KEY_Replace), are LevelData records joined where
// their map has none (KEY_LevelData), or join the key where it lacks them (KEY_Append: a map into a game's list).
enum EExtraKeyMode { KEY_Replace, KEY_LevelData, KEY_Append };
struct FExtraIniKey
{
	FString			Section, Key;
	TArray<FString>	Values;
	INT				Mode;
	FExtraIniKey() : Mode(KEY_Replace) {}
};
static TArray<FExtraIniKey> GExtraMaplists, GExtraGametypes;

static FExtraIniKey& AddKey( TArray<FExtraIniKey>& Keys, const FString& Section, const TCHAR* Key, INT Mode )
{
	FExtraIniKey* K = new(Keys) FExtraIniKey;
	K->Section = Section;
	K->Key = Key;
	K->Mode = Mode;
	return *K;
}

// The additions, into one loaded copy of the file (its sections directly: the file's Dirty flag is not touched).
static void ApplyExtraKeys( FConfigFile& File, const TArray<FExtraIniKey>& Keys )
{
	for( INT i=0; i<Keys.Num(); i++ )
	{
		const FExtraIniKey& K = Keys(i);
		FConfigSection* Sec = File.Find( *K.Section );
		if( !Sec )
			Sec = &File.Set( *K.Section, FConfigSection() );
		FName Key( *K.Key );
		TArray<FString> Have, HaveNames;
		if( K.Mode==KEY_Replace )
			Sec->Remove( Key );
		else
		{
			Sec->MultiFind( Key, Have );
			for( INT h=0; h<Have.Num(); h++ )
				AddString( HaveNames, K.Mode==KEY_LevelData ? LevelDataName( Have(h) ) : Have(h) );
		}
		for( INT v=0; v<K.Values.Num(); v++ )
			if( K.Mode==KEY_Replace || HaveNames.FindItemIndex( K.Mode==KEY_LevelData ? LevelDataName( K.Values(v) ) : K.Values(v) )==INDEX_NONE )
				Sec->Add( Key, *K.Values(v) );
	}
}

// "Maplists.ini", "../System/Maplists.ini" -> is it that ini?
static UBOOL IsIni( const TCHAR* Filename, const TCHAR* Ini )
{
	FString F( Filename );
	INT Slash = Max( F.InStr( TEXT("/"), 1 ), F.InStr( TEXT("\\"), 1 ) );
	if( Slash>=0 )
		F = F.Mid( Slash+1 );
	return appStricmp( *F, Ini )==0;
}

// GConfigFileLoaded: the cache read one of the two files from disk again (after CollectGarbage emptied it).
static void ExtraMapsConfigLoaded( const TCHAR* Filename, FConfigFile& File )
{
	if( GExtraMaplists.Num() && IsIni( Filename, MaplistsIni ) )
		ApplyExtraKeys( File, GExtraMaplists );
	else if( GExtraGametypes.Num() && IsIni( Filename, GametypesIni ) )
		ApplyExtraKeys( File, GExtraGametypes );
}

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
	GExtraMaplists.Empty();
	GExtraGametypes.Empty();
	for( INT g=0; g<Games.Num(); g++ )
	{
		FString Section = Games(g).Acronym + TEXT("_Extra");
		if( !Games(g).Maps.Num() && ExtraSections.FindItemIndex( Section )==INDEX_NONE )
			continue;
		AddKey( GExtraMaplists, Section, TEXT("Maps"), KEY_Replace ).Values = Games(g).Maps;
		AddString( AddKey( GExtraMaplists, Section, TEXT("Label"), KEY_Replace ).Values, TEXT("EXTRA") );
		AddString( AddKey( GExtraMaplists, Section, TEXT("MaplistTitle"), KEY_Replace ).Values, TEXT("Extra Maps") );
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

	// A map's own System/<Map>.ini (Epic's format): its player counts per game class, and more lists to join.
	TArray<FString> LimitSections, LimitRecords;	// one pair per [MapLimitsData] line: the game class, its record
	for( INT m=0; m<Added.Num(); m++ )
	{
		FString MapIni = Added(m) + TEXT(".ini");
		if( GFileManager->FileSize( *MapIni )<0 )
			continue;
		TMultiMap<FName,FString>* Limits = GConfig->GetSectionPrivate( TEXT("MapLimitsData"), 0, 1, *MapIni );
		INT NumLimits = 0;
		if( Limits )
			for( TMultiMap<FName,FString>::TIterator It(*Limits); It; ++It, NumLimits++ )
			{
				AddString( LimitSections, FString( *It.Key() ) );
				AddString( LimitRecords, FString::Printf( TEXT("(Name=\"%s\",%s)"), *Added(m), *It.Value() ) );
			}
		TArray<FString> Lists;
		GConfig->GetArray( TEXT("MapListsData"), TEXT("MapLists"), Lists, *MapIni );
		for( INT l=0; l<Lists.Num(); l++ )
			AddString( AddKey( GExtraMaplists, Lists(l), TEXT("Maps"), KEY_Append ).Values, Added(m) );
		debugf( NAME_Init, TEXT("ExtraMaps: %s: its %s gives %d player count(s) and %d list(s)"), *Added(m), *MapIni,
			NumLimits, Lists.Num() );
		GConfig->UnloadFile( *MapIni );
	}

	// Player counts: each Gametypes.ini section that has LevelData of the map's prefix gets a record for it (the map's
	// own one when its ini gives one for that class), and so does every class its ini names.
	FConfigFile* Gametypes = Added.Num() ? Ini->Find( GametypesIni, 0 ) : NULL;
	if( Gametypes )
	{
		for( FConfigFile::TIterator It(*Gametypes); It; ++It )
		{
			TArray<FString> LevelData, Names, Prefixes;
			if( !GConfig->GetArray( *It.Key(), TEXT("LevelData"), LevelData, GametypesIni ) )
				continue;
			for( INT i=0; i<LevelData.Num(); i++ )
			{
				AddString( Names, LevelDataName( LevelData(i) ) );
				FString Prefix = MapPrefix( Names.Last() );
				if( Prefix.Len() )
					AddUniqueString( Prefixes, Prefix );
			}
			TArray<FString> Records;
			for( INT m=0; m<Added.Num(); m++ )
			{
				if( Names.FindItemIndex( Added(m) )!=INDEX_NONE )
					continue;	// the player's own record wins
				INT Own = INDEX_NONE;
				for( INT r=0; r<LimitSections.Num() && Own==INDEX_NONE; r++ )
					if( LimitSections(r)==It.Key() && LevelDataName( LimitRecords(r) )==Added(m) )
						Own = r;
				if( Own!=INDEX_NONE )
					AddString( Records, LimitRecords(Own) );
				else if( Prefixes.FindItemIndex( MapPrefix( Added(m) ) )!=INDEX_NONE )
					AddString( Records, FString::Printf( TEXT("(Name=\"%s\",RecommendedPlayers=%d,MaxPlayers=%d)"), *Added(m),
						(INT)EXTRA_RECOMMENDED_PLAYERS, (INT)EXTRA_MAX_PLAYERS ) );
			}
			if( !Records.Num() )
				continue;
			AddKey( GExtraGametypes, It.Key(), TEXT("LevelData"), KEY_LevelData ).Values = Records;
			debugf( NAME_Init, TEXT("ExtraMaps: [%s] %d LevelData record(s) added"), *It.Key(), Records.Num() );
		}
	}

	// Into the cache's copies now, and again whenever it reads the files from disk.
	ApplyExtraKeys( *Maplists, GExtraMaplists );
	if( Gametypes )
		ApplyExtraKeys( *Gametypes, GExtraGametypes );
	if( GExtraMaplists.Num() || GExtraGametypes.Num() )
		GConfigFileLoaded = ExtraMapsConfigLoaded;
	if( GExtraGametypes.Num() )
		RefreshLevelData();
	debugf( NAME_Init, TEXT("ExtraMaps: %d added map(s)"), Added.Num() );
	unguard;
}
