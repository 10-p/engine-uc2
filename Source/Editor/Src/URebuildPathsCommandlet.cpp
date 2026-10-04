/*=============================================================================
	URebuildPathsCommandlet.cpp: Rebuilds paths in all specified maps
	Copyright 2004 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Matt Oelfke
=============================================================================*/

#include "EditorPrivate.h"
#include "UnPath.h"

static FString GetDirName(const FString &Path)
{
    INT chopPoint;

    chopPoint = Max(Path.InStr(TEXT("/"), 1) + 1, Path.InStr(TEXT("\\"), 1) + 1);

    if (chopPoint < 0)
	{
        chopPoint = Path.InStr( TEXT("*"), 1 );
	}

	return (chopPoint < 0) ? TEXT("") : Path.Left(chopPoint);
}

class URebuildPathsCommandlet : public UCommandlet
{
	DECLARE_CLASS(URebuildPathsCommandlet,UCommandlet,CLASS_Transient,Editor);
	
	void StaticConstructor()
	{
		guard(URebuildPathsCommandlet::StaticConstructor);

		LogToStdout     = 0;
		IsClient        = 1;
		IsEditor        = 1;
		IsServer        = 1;
		LazyLoad        = 0;
		ShowErrorCount  = 1;

		unguard;
	}

	INT Main(const TCHAR* Parms)
	{
		guard(URebuildPathsCommandlet::Main);

		// Create the editor class.
		UClass* EditorEngineClass = UObject::StaticLoadClass(UEditorEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.EditorEngine"), NULL, LOAD_NoFail | LOAD_DisallowFiles, NULL);
		GEditor = ConstructObject<UEditorEngine>(EditorEngineClass);
		GEditor->UseSound = 0;
        GEditor->InitEditor();

		GIsRequestingExit = 1; // Causes ctrl-c to immediately exit.

		// get the specified filename/wildcard
		FString PackageWildcard;
		if (!ParseToken(Parms, PackageWildcard, 0))
		{
			appErrorf(TEXT("Syntax: ucc rebuildpaths <file/wildcard>"));
		}
		
		const TArray<FString>& FilesInPath = GFileManager->FindFiles(*PackageWildcard, 1, 0);
		if (!FilesInPath.Num())
		{
			appErrorf(TEXT("No packages found matching %s!"), *PackageWildcard);
		}
		FString PathPrefix = GetDirName(PackageWildcard);

		for (INT i = 0; i < FilesInPath.Num(); i++)
		{
			FString PackageName = FilesInPath(i);
			// get the full path name to the file
			FString FileName = PathPrefix + PackageName;
			// skip read only files
			if (GFileManager->IsReadOnly(*FileName))
            {
				warnf(TEXT("Skipping %s (read-only)"), *PackageName);
            }
			else
			{
				warnf(TEXT("Loading %s..."), *PackageName); 
				UObject* Package = LoadPackage(NULL, *PackageName, LOAD_NoWarn);
				checkSlow(Package);
				ULevel* Level = FindObject<ULevel>(Package, TEXT("MyLevel"));
				if (!Level)
				{
					warnf(NAME_Error, TEXT("%s is not a level"), *PackageName);
				}
				else
				{
					// rebuild the paths
					warnf(TEXT("Rebuilding paths..."));
					Level->Engine = GEditor;
					Level->SetActorCollision(1);
					FPathBuilder Builder;
					Builder.definePaths(Level);
					Level->SetActorCollision(0, 1);
					// save the map
					warnf(TEXT("Saving %s..."), *PackageName);
					SavePackage(Package, Level, 0, *FileName, GWarn);
				}

				// get rid of the loaded map
				warnf(TEXT("Cleaning up..."));
				CollectGarbage(RF_Native);
			}
		}

		return 0;

		unguard;
	}
};
IMPLEMENT_CLASS(URebuildPathsCommandlet)

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/