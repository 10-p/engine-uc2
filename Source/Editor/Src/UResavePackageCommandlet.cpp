/*=============================================================================
	UResavePackageCommandlet.cpp: Resaves a set of packages (force
	Copyright 2003 Scion Studios. All Rights Reserved.

Revision history:
	* Created by Joe Graf
=============================================================================*/

#include "EditorPrivate.h"

class UResavePackageCommandlet : public UCommandlet
{
	DECLARE_CLASS(UResavePackageCommandlet,UCommandlet,CLASS_Transient,Editor);
	void StaticConstructor()
	{
		guard(UResavePackageCommandlet::StaticConstructor);

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
		guard(UResavePackageCommandlet::Main);
		// Init editor stuff
		UClass* EditorEngineClass	= UObject::StaticLoadClass( UEditorEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.EditorEngine"), NULL, LOAD_NoFail | LOAD_DisallowFiles, NULL );
		GEditor						= ConstructObject<UEditorEngine>( EditorEngineClass );
		GEditor->UseSound = 0;
		GEditor->InitEditor();

		FString WildCard;
		FString Dir;
		if( !ParseToken(Parms,Dir,0) )
			appErrorf(TEXT("Please specify a directory"));
		if( !ParseToken(Parms,WildCard,0) )
			appErrorf(TEXT("Package search mask not specified."));

		GLazyLoad = 0;

		GFileManager->SetDefaultDirectory(*Dir);

		TArray<FString> FilesFound = GFileManager->FindFiles( *WildCard, 1, 0 );
		for (INT i=0; i<FilesFound.Num(); i++)
		{
			FString Pkg = FilesFound(i);
			warnf( TEXT("\nLoading package %s...\n"), *Pkg );

			UObject* Package = NULL;
			try
			{
				Package = LoadPackage(NULL,*Pkg,LOAD_NoWarn);
			}
			catch(...)
			{
				warnf( TEXT("\nError loading package %s...\n"), *Pkg );
			}
		
			if (Package != NULL)
			{
				// Save differently if we are a package versus a level
				ULevel* Level = FindObject<ULevel>( Package, TEXT("MyLevel") );
				if (Level)
				{
					warnf( TEXT("\nResaving map %s...\n"), *Pkg );
					SavePackage(Package,Level,0,*Pkg,GError,NULL);
				}
				else
				{
					warnf( TEXT("\nResaving package %s...\n"), *Pkg );
					SavePackage(Package,NULL,RF_Standalone,*Pkg,GError,NULL);
				}
			}
			try
			{
				UObject::ResetLoaders( NULL, 0, 1 );
				UObject::CollectGarbage(RF_Native);
			}
			catch(...)
			{
				warnf( TEXT("\nError garbage collecting...\n"));
			}
		}
		GIsRequestingExit=1;
		return 0;
		unguard;
	}
};
IMPLEMENT_CLASS(UResavePackageCommandlet)
