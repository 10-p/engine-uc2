/* scion ======================================================================
* Author: sz
* ============================================================================
* UFindRefCommandlet.cpp
* 
* Try to find any references to the given object within the given list of packages.
* This can be used, for example, to find all references to a texture in all maps
* or in gamplay packages, etc.
*
* Usage:
*	ucc findref PACKAGE=<obj package> NAME=<obj name> <packages>
*	<obj package> = path of package the source object is in
*	<obj name> = path of object to find (including group name).
*	<packages> = path of package to search in (wildcards ok)
*
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

//
// Archive for finding who references an object.
//
class FArchiveFindCulprit : public FArchive
{
public:
	FArchiveFindCulprit( UObject* InFind, UObject* Src )
		: Find(InFind), Count(0)
	{
		Src->Serialize( *this );
	}
	INT GetCount()
	{
		return Count;
	}
	FArchive& operator<<( class UObject*& Obj )
	{
		if( Obj==Find )
			Count++;
		return *this;
	}
protected:
	UObject* Find;
	INT Count;
};

class UFindRefCommandlet : public UCommandlet
{
	DECLARE_CLASS(UFindRefCommandlet,UCommandlet,CLASS_Transient,Editor);
	
	void StaticConstructor()
	{
		guard(UFindRefCommandlet::StaticConstructor);

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
		guard(UFindRefCommandlet::Main);

		// get source package filename
		const INT FILE_NAME_SIZE = 255;
		TCHAR SrcPackageFileName[FILE_NAME_SIZE];
		if( !Parse( Parms, TEXT("PACKAGE="), SrcPackageFileName, FILE_NAME_SIZE ) )
		{
			appErrorf(TEXT("Source package name not specified."));
		}
		ParseToken( Parms, 0 );

		// get source object name
		const INT OBJ_NAME_SIZE = 255;
		TCHAR SrcObjName[OBJ_NAME_SIZE];
		if( !Parse( Parms, TEXT("NAME="), SrcObjName, OBJ_NAME_SIZE ) )
		{
			appErrorf(TEXT("Invalid object name."));
		}
		ParseToken( Parms, 0 );

		// get the filenames for packages to search in for 
		// references to the SrcObject (wildcards ok)
		FString WildCard;
		if( !ParseToken(Parms,WildCard,0) )
		{
			appErrorf(TEXT("Package search mask not specified."));
		}

		GLazyLoad = 0;

		// for each file found matching the wildcard
		TArray<FString> FilesFound = GFileManager->FindFiles( *WildCard, 1, 1 );
		// save directory name
		FString Path = GetDirName( WildCard );

		if( !FilesFound.Num() )
		{
			appErrorf(TEXT("No package files found to search."));
		}

		// process each file
		for( INT i=0; i < FilesFound.Num(); i++ )
		{
			// open the package matching that file
			FString Pkg = Path + FilesFound(i);
			warnf( TEXT("\nLoading package %s...\n"), *Pkg );
			UObject* Package = LoadPackage(NULL,*Pkg,LOAD_NoWarn);

			// load the source packagbe
			UObject* SrcPackage = LoadPackage( NULL, SrcPackageFileName, LOAD_NoWarn );
			if( !SrcPackage )
			{
				appErrorf(TEXT("Source package not found."));
			}

			// find the specified object in the source package
			UObject* SrcObject = UObject::StaticFindObject( NULL, SrcPackage, SrcObjName );
			if( !SrcObject )
			{
				appErrorf(TEXT("Source object not found."));
			}

			if( Package )
			{
				// for each object in the package
				for( TObjectIterator<UObject> It; It; ++It )
				{
					UObject* CurObj = *It;

					if( CurObj->IsIn(Package) )
					{
						FArchiveFindCulprit ArFind( SrcObject, CurObj );
						if( ArFind.GetCount() )
						{
							warnf( TEXT("   %s"), It->GetFullName() );
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
IMPLEMENT_CLASS(UFindRefCommandlet)

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
