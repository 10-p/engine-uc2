/* scion ======================================================================
* Author: sz
* ============================================================================
* UCheckLODActorsCommandlet.cpp
* 
* Given a wildcard list of level packages. Check all the actors' LOD settings.
*
* usage:
* checklodactors pkgname(*.unr)
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

class UCheckLODActorsCommandlet : public UCommandlet
{
	DECLARE_CLASS(UCheckLODActorsCommandlet,UCommandlet,CLASS_Transient,Editor);
	
	void StaticConstructor()
	{
		guard(UCheckLODActorsCommandlet::StaticConstructor);

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
		guard(UCheckLODActorsCommandlet::Main);

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
			// but only animation packages
			if( Pkg.InStr( TEXT(".unr"),1) == INDEX_NONE )
				continue;

			warnf( TEXT("\nLoading package %s..."), *Pkg );
			UPackage* Package = Cast<UPackage>( LoadPackage(NULL,*Pkg,LOAD_NoWarn) );
			warnf( TEXT("[ActorName] [StaticMesh] (bCollideActors/bBlockActors/bBlockPlayers)") );

			if( Package )
			{
				// for each animation set in the package
				for( TObjectIterator<AActor> It; It; ++It )
				{
					AActor* pActor= *It;

					// only check objects within our loaded package
					if( pActor->IsIn(Package) )
					{
						AnalyzeActor( pActor );
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

	void AnalyzeActor( AActor* pActor )
	{
		guard(UCheckLODActorsCommandlet::AnalyzeActor);
		check(pActor);

        if( pActor->DrawType == DT_StaticMesh &&
			pActor->StaticMesh &&
			pActor->DetailLevel == DM_SuperHigh &&
			pActor->bCollideActors &&
			(pActor->bBlockActors || pActor->bBlockPlayers) )
		{
			UStaticMesh* StaticMesh = pActor->StaticMesh;
			INT NumTriangles = 0;
			for( INT i=0; i < StaticMesh->Sections.Num(); i++ )
			{
				FStaticMeshSection& Section = StaticMesh->Sections( i );
				NumTriangles += Section.NumTriangles;
			}

			warnf( TEXT("[%s] [%s] (%d/%d/%d) Tris=%d"), 
				pActor->GetName(), 
				StaticMesh->GetFullName(),
				pActor->bCollideActors,
				pActor->bBlockActors,
				pActor->bBlockPlayers,
				NumTriangles 
				);            								
		}

		unguard;
	}
	
};
IMPLEMENT_CLASS(UCheckLODActorsCommandlet)

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
