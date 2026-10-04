#include "EditorPrivate.h"

/*-----------------------------------------------------------------------------
	UCheckStaticMeshesCommandlet
-----------------------------------------------------------------------------*/

class UCheckStaticMeshes : public UCommandlet
{
	DECLARE_CLASS(UCheckStaticMeshes,UCommandlet,CLASS_Transient,Editor);
	void StaticConstructor()
	{
		guard(UStaticMeshesTextures::StaticConstructor);

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
		guard(UCheckStaticMeshes::Main);
		
		FString	PackageWildcard;

		UClass* StaticMeshClass		= FindObjectChecked<UClass>( ANY_PACKAGE, TEXT("StaticMesh") );
		UClass* StaticMeshActorClass= FindObjectChecked<UClass>( ANY_PACKAGE, TEXT("StaticMeshActor") );
		UClass* EditorEngineClass	= UObject::StaticLoadClass( UEditorEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.EditorEngine"), NULL, LOAD_NoFail | LOAD_DisallowFiles, NULL );
		GEditor						= ConstructObject<UEditorEngine>( EditorEngineClass );
		GEditor->UseSound = 0;
		GEditor->InitEditor();

		GLazyLoad = 0;

		while( ParseToken(Parms, PackageWildcard, 0) )
		{
			TArray<FString> FilesInPath;
			FString			PathPrefix;

			FilesInPath = GFileManager->FindFiles( *PackageWildcard, 1, 0 );

			if( !FilesInPath.Num() )
				continue;

			for( INT FileIndex = 0; FileIndex < FilesInPath.Num(); FileIndex++ )
			{
				const FString &File = FilesInPath(FileIndex);
				UObject* Package;

				warnf(NAME_Log, TEXT("Loading %s"), *File);

				try
				{
					Package = UObject::LoadPackage( NULL, *File, LOAD_NoWarn );

					if( !Package )
					{
						warnf(NAME_Log, TEXT("    Error loading %s!"), *File);
						UObject::ResetLoaders( NULL, 0, 1 );
						UObject::CollectGarbage(RF_Native);
						continue;
					}

					guard(LoadStaticMeshes);
					for( TObjectIterator<UObject> It; It; ++It )
					{
						if( It->IsA(StaticMeshClass) && It->IsIn(Package) )
						{
							UStaticMesh* StaticMesh = CastChecked<UStaticMesh>(*It);

							StaticMesh->RawTriangles.Load();
							if( StaticMesh->RawTriangles.Num() == 0 )
								warnf(NAME_Log, TEXT("WARNING: [%s] is corrupt and needs to be re-imported"),StaticMesh->GetPathName());
							StaticMesh->RawTriangles.Unload();
						}
					}
					unguard;

					guard(LoadStaticMeshActors);
					UBOOL RebuildLighting = 0;
					for( TObjectIterator<UObject> It; It; ++It )
					{
						if( It->IsA(StaticMeshActorClass) && It->IsIn(Package) )
						{
							AStaticMeshActor* StaticMeshActor = CastChecked<AStaticMeshActor>(*It);

							if( StaticMeshActor->StaticMeshInstance && StaticMeshActor->StaticMesh )
							{
								UBOOL ReportMesh = 0;

								if( StaticMeshActor->StaticMeshInstance->ColorStream.Colors.Num() != StaticMeshActor->StaticMesh->VertexStream.Vertices.Num() )
									ReportMesh = 1;

								for( INT LightIndex=0; LightIndex<StaticMeshActor->StaticMeshInstance->Lights.Num(); LightIndex++ )
									if( StaticMeshActor->StaticMeshInstance->Lights(LightIndex).VisibilityBits.Num() != (StaticMeshActor->StaticMesh->VertexStream.Vertices.Num() + 7) / 8 )
										ReportMesh = 1;

								if( ReportMesh )
								{
									warnf(NAME_Log, TEXT("WARNING: [%s] [%s]"),StaticMeshActor->GetPathName(), StaticMeshActor->StaticMesh->GetPathName());
									RebuildLighting = 1;
								}
							}
						}
					}
					if( RebuildLighting )
						warnf(NAME_Log, TEXT("WARNING: \"%s\" needs to have lighting rebuild."),Package->GetPathName());
					unguard;
				}
				catch( ... )
				{
					warnf(NAME_Log, TEXT("    Error loading %s!"), *File);
				}

				UObject::ResetLoaders( NULL, 0, 1 );
				UObject::CollectGarbage(RF_Native);
			}
		}
	
		GIsRequestingExit=1;
		return 0;
		unguard;
	}
};
IMPLEMENT_CLASS(UCheckStaticMeshes)

/*-----------------------------------------------------------------------------
	UListUsedMaterialTypesCommandlet
-----------------------------------------------------------------------------*/

class UListUsedMaterialTypesCommandlet : public UCommandlet
{
	DECLARE_CLASS(UListUsedMaterialTypesCommandlet,UCommandlet,CLASS_Transient,Editor);

	void StaticConstructor()
	{
		guard(UListUsedMaterialTypesCommandlet::StaticConstructor);

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
		guard(UListUsedMaterialTypesCommandlet::Main);

		UClass* EditorEngineClass	= UObject::StaticLoadClass(UEditorEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.EditorEngine"), NULL, LOAD_NoFail | LOAD_DisallowFiles, NULL);
		GEditor = ConstructObject<UEditorEngine>(EditorEngineClass);
		GEditor->UseSound = 0;
		GEditor->InitEditor();

		GLazyLoad = 0;

		UBOOL bUsedMaterialType[MT_Max] = {0};
		UBOOL bLoadedFiles = 0;
		FString	PackageWildcard;
		while (ParseToken(Parms, PackageWildcard, 0))
		{
			TArray<FString> FilesInPath;
			FString PathPrefix;

			FilesInPath = GFileManager->FindFiles(*PackageWildcard, 1, 0);

			if (FilesInPath.Num())
			{
				for (INT FileIndex = 0; FileIndex < FilesInPath.Num(); FileIndex++)
				{
					const FString &File = FilesInPath(FileIndex);

					try
					{
						warnf(NAME_Log, TEXT("Loading %s..."), *File);
						UObject* Package;
						Package = UObject::LoadPackage(NULL, *File, LOAD_NoWarn);

						if (!Package)
						{
							warnf(NAME_Log, TEXT("    Error loading %s!"), *File);
						}
						else
						{
							warnf(TEXT("Determining MaterialTypes in use..."));
							bLoadedFiles = 1;
							for (TObjectIterator<UObject> It; It; ++It)
							{
								if (It->IsA(UMaterial::StaticClass()))
								{
									bUsedMaterialType[((UMaterial*)*It)->MaterialType] = 1;
								}
								else if (It->IsA(ABlockingVolume::StaticClass()))
								{
									bUsedMaterialType[((ABlockingVolume*)*It)->MaterialType] = 1;
								}
							}
							warnf(TEXT("Cleaning up..."));
						}
					}
					catch (...)
					{
						warnf(NAME_Log, TEXT("    Error loading %s!"), *File);
					}

					UObject::ResetLoaders(NULL, 0, 1);
					UObject::CollectGarbage(RF_Native);
				}
			}
		}

		if (bLoadedFiles)
		{
			UEnum* MaterialTypeEnum = (UEnum*)StaticFindObjectFast(UEnum::StaticClass(), NULL, FName(TEXT("EMaterialType")), 1, 1);
			checkSlow(MaterialTypeEnum);
			warnf(TEXT("Used MaterialTypes:"));
			for (INT i = 0; i < MT_Max; i++)
			{
				if (bUsedMaterialType[i])
				{
					warnf(*MaterialTypeEnum->Names(i));
				}
			}
			warnf(TEXT("Unused MaterialTypes:"));
			for (INT i = 0; i < MT_Max; i++)
			{
				if (!bUsedMaterialType[i])
				{
					warnf(*MaterialTypeEnum->Names(i));
				}
			}
		}

		GIsRequestingExit = 1;
		return 0;

		unguard;
	}
};
IMPLEMENT_CLASS(UListUsedMaterialTypesCommandlet)