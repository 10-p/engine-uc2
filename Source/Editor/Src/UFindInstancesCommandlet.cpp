/*=============================================================================
	UFindInstancesCommandlet.cpp: Searches the given packages for instances of the specified class
	Copyright 2005 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Matt Oelfke
=============================================================================*/

#include "EditorPrivate.h"

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

class UFindInstancesCommandlet : public UCommandlet
{
	DECLARE_CLASS(UFindInstancesCommandlet,UCommandlet,CLASS_Transient,Editor);
	
	void StaticConstructor()
	{
		guard(UFindInstancesCommandlet::StaticConstructor);

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
		guard(UFindInstancesCommandlet::Main);

		// Create the editor class.
		UClass* EditorEngineClass = UObject::StaticLoadClass(UEditorEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.EditorEngine"), NULL, LOAD_NoFail | LOAD_DisallowFiles, NULL);
		GEditor = ConstructObject<UEditorEngine>(EditorEngineClass);
		GEditor->UseSound = 0;
        GEditor->InitEditor();

		GIsRequestingExit = 1; // Causes ctrl-c to immediately exit.

		// get the class to find instances of
		FString ClassName;
		UClass* TestClass = NULL;
		if (!ParseToken(Parms, ClassName, 0))
		{
			appErrorf(TEXT("Syntax: ucc findinstances <package.class> <file/wildcard>"));
		}
		else
		{
			TestClass = (UClass*)StaticLoadObject(UClass::StaticClass(), NULL, *ClassName, NULL, LOAD_NoWarn, NULL);
			if (!TestClass)
			{
				appErrorf(TEXT("Failed to load class: %s"), *ClassName);
			}
		}

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

		INT TotalRefs = 0;
		for (INT i = 0; i < FilesInPath.Num(); i++)
		{
			FString PackageName = FilesInPath(i);
			// get the full path name to the file
			FString FileName = PathPrefix + PackageName;
			// load the package
			warnf(TEXT("Loading %s..."), *PackageName); 
			UObject* Package = LoadPackage(NULL, *PackageName, LOAD_NoWarn);
			checkSlow(Package);
			// look for instances of the specified class
			INT Refs = 0;
			for (FObjectIterator It(TestClass); It; ++It)
			{
				if (It->IsIn(Package))
				{
					Refs++;
				}
			}
			if (Refs > 0)
			{
				warnf(TEXT("Found %i instances of %s in %s"), Refs, TestClass->GetFullName(), *PackageName);
				TotalRefs += Refs;
			}

			// get rid of the loaded map
			warnf(TEXT("Cleaning up..."));
			CollectGarbage(RF_Native);
		}
		
		warnf(TEXT("Found %i total instances of %s"), TotalRefs, TestClass->GetFullName());

		return 0;

		unguard;
	}
};
IMPLEMENT_CLASS(UFindInstancesCommandlet)

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/