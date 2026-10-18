/*=============================================================================
	UC2ZoneSurvey.cpp: `ucc-bin UC2ALAudio.UC2ZoneSurvey [MAP=<name>]`.

	Loads every map in ../Maps (or one) and lists its zones - the LevelInfo (the default zone of a level) and
	every ZoneInfo, WaterZone, SkyZoneInfo... - with the EZoneAudioEffect each carries (ZoneInfo.uc), which is
	what UC2ALAudio's NotifyZoneChange turns into a reverb preset (the shipped Xbox code had that switch in
	#if 0). One line per map, then one per zone that is not Default. No audio device is involved.
=============================================================================*/

#include "UC2ALAudioPrivate.h"

void UUC2ZoneSurveyCommandlet::StaticConstructor()
{
	{
		guard(UUC2ZoneSurveyCommandlet::StaticConstructor);
		LogToStdout		= 1;
		IsClient		= 1;	// (a client: the maps' client-side classes load as in the game)
		IsEditor		= 0;
		IsServer		= 1;
		LazyLoad		= 1;
		ShowErrorCount	= 0;
		unguard;
	}
}

INT UUC2ZoneSurveyCommandlet::Main( const TCHAR* Parms )
{
	{
		guard(UUC2ZoneSurveyCommandlet::Main);
		FString One;
		Parse( Parms, TEXT("MAP="), One );
		TArray<FString> Files = GFileManager->FindFiles( TEXT("../Maps/*.unr"), 1, 0 );
		INT Maps = 0, MapsWithReverb = 0, Failed = 0;
		INT Totals[UC2_NUM_ZONE_PRESETS];
		appMemzero( Totals, sizeof(Totals) );
		for( INT f=0; f<Files.Num(); f++ )
		{
			FString Name = Files(f).Left( Files(f).InStr( TEXT("."), 1 ) );
			if( One.Len() && appStricmp( *One, *Name ) != 0 )
				continue;
			UObject* Package = NULL;
			try
			{
				Package = UObject::LoadPackage( NULL, *(FString(TEXT("../Maps/")) + Files(f)), LOAD_NoWarn | LOAD_Quiet );
			}
			catch( ... )
			{
				Package = NULL;
			}
			if( !Package )
			{
				GWarn->Logf( TEXT("ZONES %s: cannot load"), *Name );
				Failed++;
				continue;
			}
			Maps++;
			INT Counts[UC2_NUM_ZONE_PRESETS];
			appMemzero( Counts, sizeof(Counts) );
			TArray<FString> Lines;
			INT Zones = 0;
			for( TObjectIterator<AZoneInfo> It; It; ++It )
			{
				AZoneInfo* Z = *It;
				if( !Z->IsIn( Package ) )
					continue;
				Zones++;
				INT E = Z->ZoneAudioEffect < UC2_NUM_ZONE_PRESETS ? Z->ZoneAudioEffect : 0;
				Counts[E]++;
				Totals[E]++;
				if( Z->ZoneAudioEffect != 0 )
					new(Lines) FString( FString::Printf( TEXT("ZONE  %s %s (%s, tag %s%s%s): ZoneAudioEffect %i = %s"), *Name, Z->GetName(),
						Z->GetClass()->GetName(), *Z->ZoneTag, Z->LocationName.Len() ? TEXT(", ") : TEXT(""), *Z->LocationName,
						Z->ZoneAudioEffect, Z->ZoneAudioEffect < UC2_NUM_ZONE_PRESETS ? GUC2ZonePresets[Z->ZoneAudioEffect].Name : TEXT("?") ) );
			}
			FString Summary;
			for( INT i=0; i<UC2_NUM_ZONE_PRESETS; i++ )
				if( Counts[i] )
					Summary += FString::Printf( TEXT(" %s x%i"), GUC2ZonePresets[i].Name, Counts[i] );
			if( Zones > Counts[0] )
				MapsWithReverb++;
			GWarn->Logf( TEXT("ZONES %s: %i zones:%s"), *Name, Zones, *Summary );
			for( INT i=0; i<Lines.Num(); i++ )
				GWarn->Logf( TEXT("%s"), *Lines(i) );
			// Free the map before the next one.
			UObject::ResetLoaders( Package, 0, 1 );
			UObject::CollectGarbage( RF_Native | RF_Standalone );
		}
		FString Summary;
		for( INT i=0; i<UC2_NUM_ZONE_PRESETS; i++ )
			if( Totals[i] )
				Summary += FString::Printf( TEXT(" %s x%i"), GUC2ZonePresets[i].Name, Totals[i] );
		GWarn->Logf( TEXT("UC2ZoneSurvey: %i maps, %i with a zone that is not Default, %i not loaded; zones:%s"), Maps, MapsWithReverb, Failed, *Summary );
		return Failed ? 1 : 0;
		unguard;
	}
}
IMPLEMENT_CLASS(UUC2ZoneSurveyCommandlet);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
