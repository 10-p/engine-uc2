/*=============================================================================
	UStripSourceCommandlet.cpp: Load a .u file and remove the script text from
	all classes.
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Jack Porter

=============================================================================*/

#include "EditorPrivate.h"

/*-----------------------------------------------------------------------------
	UStripSourceCommandlet
-----------------------------------------------------------------------------*/

class UStripSourceCommandlet : public UCommandlet
{
	DECLARE_CLASS(UStripSourceCommandlet,UCommandlet,CLASS_Transient,Editor);
	void StaticConstructor()
	{
		guard(UStripSourceCommandlet::StaticConstructor);

		LogToStdout     = 0;
		IsClient        = 1;
		IsEditor        = 1;
		IsServer        = 1;
		LazyLoad        = 0;
		ShowErrorCount  = 1;

		unguard;
	}
	INT Main( const TCHAR* Parms )
	{
		guard(UStripSourceCommandlet::Main);

		// Get a list of the script packages to convert
		TArray<FString> ScriptFiles = GFileManager->FindFiles(TEXT("*.u"),1,0);
		// In the order NTFS listed them on Epic's machine (UC2SortLikeNTFS, Core/Src/UnObj.cpp).
		UC2SortLikeNTFS( ScriptFiles );
		// Loop through each script package, load it, strip the source from the
		// class data, and save it back out
		for (INT Index = 0; Index < ScriptFiles.Num(); Index++)
		{
			// Skip the editor package since it won't be loaded
			if (ScriptFiles(Index) == TEXT("Editor.u"))
			{
				continue;
			}
			warnf( TEXT("Loading package %s..."),*ScriptFiles(Index));
			warnf(TEXT(""));
			UObject* Package = LoadPackage(NULL,*ScriptFiles(Index),LOAD_NoWarn);
			if( !Package )
			{
				appErrorf( TEXT("Unable to load %s"),*ScriptFiles(Index));
			}
			// Iterate all classes stripping the source
			for (TObjectIterator<UClass> It; It; ++It)
			{
				// If this class is in the specified package
				if (It->GetOuter() == Package)
				{
					GWarn->Logf(NAME_Progress,TEXT("Stripping %s                  "),
						It->GetName());
					// Strip the source code if there
					if (It->ScriptText != NULL)
					{
						It->ScriptText->Text = FString(TEXT(" "));
						It->ScriptText->Pos = 0;
						It->ScriptText->Top = 0;
					}
					// Strip the CppText if there
					if (It->CppText != NULL)
					{
						It->CppText->Text = FString(TEXT(" "));
						It->CppText->Pos = 0;
						It->CppText->Top = 0;
					}
				}
			}
			warnf(TEXT(""));
			warnf(TEXT("Saving %s..."),*ScriptFiles(Index));
			// -CONFORM saves conforming to ../GUIRes/<Pkg>.u, as `ucc make` does, so the result
			// keeps the shipped package's name/import/export order and GUID (a new generation is added). Without
			// it the save is Epic's: a first-generation file with a new GUID.
			ULinkerLoad* Conform = NULL;
			if( ParseParam(Parms,TEXT("CONFORM")) )
			{
				FString PkgName = ScriptFiles(Index).LeftChop(2);
				BeginLoad();
				Conform = UObject::GetPackageLinker( CreatePackage(NULL,*(PkgName+TEXT("_OLD"))), *(FString(TEXT("..")) * TEXT("GUIRes") * PkgName + TEXT(".u")), LOAD_NoWarn|LOAD_NoVerify, NULL, NULL );
				EndLoad();
			}
			SavePackage(Package,NULL,RF_Standalone,*ScriptFiles(Index),GWarn,Conform);
		}
	
		GIsRequestingExit=1;
		return 0;
		unguard;
	}
};
IMPLEMENT_CLASS(UStripSourceCommandlet)

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
