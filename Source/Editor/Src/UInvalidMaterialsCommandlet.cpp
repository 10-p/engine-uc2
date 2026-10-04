/* scion ======================================================================
* Author: sz
* ============================================================================
* UInvalidMaterialsCommandlet.cpp
* 
* Given a wildcard list of texture packages. List all Modifiers with no material
* associated with them.
*
* usage:
* invalidmaterials package(s)
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

class UInvalidMaterialsCommandlet : public UCommandlet
{
	DECLARE_CLASS(UInvalidMaterialsCommandlet,UCommandlet,CLASS_Transient,Editor);
	
	void StaticConstructor()
	{
		guard(UInvalidMaterialsCommandlet::StaticConstructor);

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
		guard(UInvalidMaterialsCommandlet::Main);

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
			warnf( TEXT("\nLoading package %s...\n"), *Pkg );
			UObject* Package = LoadPackage(NULL,*Pkg,LOAD_NoWarn);
			warnf( TEXT("Searching for invalid material Modifiers...\n") );
			if( Package )
			{
				// for each object in the package
				for( TObjectIterator<UModifier> It; It; ++It )
				{
					UModifier* Modifier = *It;

					// only check objects within our loaded package
					if( Modifier->IsIn(Package) )
					{
						if( !Modifier->Material )
						{
							warnf( TEXT("[%s].Material=NULL"), Modifier->GetFullName() );
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
};
IMPLEMENT_CLASS(UInvalidMaterialsCommandlet)

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
