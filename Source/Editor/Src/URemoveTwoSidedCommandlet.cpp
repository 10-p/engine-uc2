/* scion ======================================================================
* Author: sz
* ============================================================================
* URemoveTwoSidedCommandlet.cpp
* 
* Given a wildcard list of texture packages. Remove any bIsTwoSided flags from
* PSSkinShader materials
*
* usage:
* removetwosided pkgname(*.ukx)
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

class URemoveTwoSidedCommandlet : public UCommandlet
{
	DECLARE_CLASS(URemoveTwoSidedCommandlet,UCommandlet,CLASS_Transient,Editor);
	
	void StaticConstructor()
	{
		guard(URemoveTwoSidedCommandlet::StaticConstructor);

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
		guard(URemoveTwoSidedCommandlet::Main);

		UBOOL bPreviewOnly = 0;
		if( ParseParam( Parms, TEXT("preview") ) )
		{
			bPreviewOnly = 1;
			ParseToken(Parms,0);
		}

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
			if( Pkg.InStr( TEXT(".utx"),1) == INDEX_NONE )
				continue;

			warnf( TEXT("\nLoading package %s...\n"), *Pkg );
			UPackage* Package = Cast<UPackage>( LoadPackage(NULL,*Pkg,LOAD_NoWarn) );

			if( Package )
			{
				// for each animation set in the package
				for( TObjectIterator<UPSSkinShader> It; It; ++It )
				{
					UPSSkinShader* SkinShader = *It;

					// only check objects within our loaded package
					if( SkinShader->IsIn(Package) &&
						SkinShader->bIsTwoSided )
					{
						warnf( TEXT("Forcing [%s].bIsTwoSided=1"), SkinShader->GetFullName() );
						SkinShader->bIsTwoSided=0;
						Package->bDirty = !bPreviewOnly;
					}
				}

				// if anything was updated
				if( Package->bDirty )
				{
					// save the package
					warnf( TEXT("\nSaving package %s...\n"), *Pkg );
					SavePackage( Package, NULL, RF_Standalone, *Pkg, GError, NULL );
				}
				else
				{
					warnf( TEXT("\nNothing changed.. \n"), *Pkg );
				}
			}
			UObject::CollectGarbage(RF_Native);
		}			
		
		GIsRequestingExit=1;
		return 0;
		unguard;
	}


};
IMPLEMENT_CLASS(URemoveTwoSidedCommandlet)

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
