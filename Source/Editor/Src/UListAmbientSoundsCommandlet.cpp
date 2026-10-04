/* scion ======================================================================
* Author: sz
* ============================================================================
* UListAmbientSoundsCommandlet.cpp
* 
* Given a wildcard list of level packages. Load each level and list hte 
* ambient sounds in each level.
*
* usage:
* ucc listambientsounds pkgname(*.unr)
* ============================================================================
*/

#include "EditorPrivate.h"

// get directory portion of the path
static FString GetDirName( const FString &Path )
{
	INT chopPoint;

	chopPoint = Max (Path.InStr( TEXT("/"), 1 ) + 1, Path.InStr( TEXT("\\"), 1 ) + 1);

	if (chopPoint < 0)
		chopPoint = Path.InStr( TEXT("*"), 1 );

	if (chopPoint < 0)
		return (TEXT(""));

	return (Path.Left( chopPoint ) );
}

class UListAmbientSoundsCommandlet : public UCommandlet
{
	DECLARE_CLASS(UListAmbientSoundsCommandlet,UCommandlet,CLASS_Transient,Editor);
	
	void StaticConstructor()
	{
		guard(UListAmbientSoundsCommandlet::StaticConstructor);

		LogToStdout     = 0;
		IsClient        = 1;
		IsEditor        = 1;
		IsServer        = 1;
		LazyLoad        = 1;
		ShowErrorCount  = 1;

		unguard;
	}

	INT Main( const TCHAR* Parms )
	{
		guard(UListAmbientSoundsCommandlet::Main);

		// get the filename (wildcards ok)
		FString WildCard;
		if( !ParseToken(Parms,WildCard,0) )
			appErrorf(TEXT("Package search mask not specified."));

		GLazyLoad = 0;

		// for each file found matching the wildcard
		TArray<FString> FilesFound = GFileManager->FindFiles( *WildCard, 1, 1 );
		// save directory name
		FString Path = GetDirName( WildCard );

		// process each file
		for( INT i=0; i < FilesFound.Num(); i++ )
		{
			// open the package matching that file
			FString Pkg = Path + FilesFound(i);
			// but only level packages
			if( Pkg.InStr( TEXT(".unr"),1) == INDEX_NONE )
				continue;

			warnf( TEXT("\nLoading package %s...\n"), *Pkg );
			UPackage* Package = Cast<UPackage>( LoadPackage(NULL,*Pkg,LOAD_NoWarn) );

			if( Package )
			{
				// for each ambient sound in the package
				for( TObjectIterator<AActor> It; It; ++It )
				{
					AActor* pActor = *It;
					// only check objects within our loaded package
					if( pActor->IsIn(Package) )
					{
						if( (pActor->AmbientSoundCue.IsValid() && pActor->AmbientSoundCue != NAME_None) || 
							pActor->IsA(AAmbientSound::StaticClass()) )
						{
							warnf( TEXT("Ambient sound Actor=[%s] using sound=[%s]"), 
								pActor->GetName(), 
								(pActor->AmbientSoundCue.IsValid()) ? *pActor->AmbientSoundCue:TEXT("None") );
						}
					}
				}				
			}
			UObject::CollectGarbage(RF_Native);
		}			
		
		GIsRequestingExit=1;
		return 0;
		unguard;
	}

private:

};
IMPLEMENT_CLASS(UListAmbientSoundsCommandlet)

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
