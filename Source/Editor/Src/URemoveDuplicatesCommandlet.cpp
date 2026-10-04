/* scion ======================================================================
* Author: sz
* ============================================================================
* URemoveDuplicatesCommandlet.cpp
* 
* Given a wildcard list of packages.  Check all objects within the package
* to see if there are any duplicates.  Delete the duplicates.
*
* usage:
* removeduplicates /f mapname
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

class URemoveDuplicatesCommandlet : public UCommandlet
{
	DECLARE_CLASS(URemoveDuplicatesCommandlet,UCommandlet,CLASS_Transient,Editor);
	
	void StaticConstructor()
	{
		guard(URemoveDuplicatesCommandlet::StaticConstructor);

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
		guard(URemoveDuplicatesCommandlet::Main);

		// get the force delete parameter
		UBOOL bForceDelete = 0;
		if( ParseParam( Parms, TEXT("f") ) )
		{
			bForceDelete = 1;
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
			warnf( TEXT("\nLoading package %s...\n"), *Pkg );
			UObject* Package = LoadPackage(NULL,*Pkg,LOAD_NoWarn);

			if( Package )
			{
				// for each object in the package
				for( TObjectIterator<UObject> It; It; ++It )
				{
					UObject* CurObj = *It;

					// only check objects within our loaded package
					if( CurObj->IsIn(Package) )
					{
						UObject* DupToDelete = NULL;

						// search for duplicates of this object
						for( TObjectIterator<UObject> ItDup; ItDup && !DupToDelete; ++ItDup )
						{
                            if( ItDup->GetFName() == CurObj->GetFName() &&		// same name
								ItDup->GetIndex() != CurObj->GetIndex() &&		// different hash index
								ItDup->IsA( It->GetClass() ) &&					// same class type
								ItDup->IsIn(Package) )							// in same package
							{
								// if this duplicate object has no group as
								// an outer then we should delete it
								if( ItDup->GetOuter() == Package )
								{
									DupToDelete = *ItDup;
								}
								else if( CurObj->GetOuter() == Package )
								{
									DupToDelete = CurObj;
								}
								// or if they are both contained within the same group
								else if( CurObj->GetOuter() == ItDup->GetOuter() )
								{
									DupToDelete = *ItDup;
								}
							}								
						}
						
						// if we found a duplicate object
						// delete it
						if( DupToDelete )
						{
							// confirm deletion
							if( bForceDelete || GWarn->YesNof( TEXT("Delete? %s"), DupToDelete->GetFullName() ) )
							{
								// make sure the object is not referenced before deleting it
								if( !UObject::IsReferenced( DupToDelete, RF_Native | RF_Public, 0 ) )
								{
									warnf( TEXT("Deleting %s...\n"), DupToDelete->GetFullName() );
									((UPackage*)Package)->bDirty = 1;
									delete DupToDelete;
								}
							}
						}
					}
				}

				// if anything was updated
				if( ((UPackage*)Package)->bDirty )
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
IMPLEMENT_CLASS(URemoveDuplicatesCommandlet)

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
