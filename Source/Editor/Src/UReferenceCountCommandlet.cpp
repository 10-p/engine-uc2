
#include "EditorPrivate.h"

struct StaticMeshRef
{
	UStaticMesh			*staticMesh;
	INT					totalRefs;
	TMap<INT,INT>		*perLevelRefs;
};

static QSORT_RETURN CDECL SortRefs(const StaticMeshRef* A, const StaticMeshRef* B)
{
	INT sectionDiff = B->staticMesh->Sections.Num() - A->staticMesh->Sections.Num();
	if (sectionDiff == 0)
	{
		return (A->totalRefs - B->totalRefs);
	}
	else
	{
		return sectionDiff;
	}
}

class UReferenceCountCommandlet : public UCommandlet
{
	DECLARE_CLASS(UReferenceCountCommandlet,UCommandlet,CLASS_Transient,Editor);
	void StaticConstructor()
	{
		guard(UAnalyzeContentCommandlet::StaticConstructor);

		LogToStdout     = 0;
		IsClient        = 1;
		IsEditor        = 1;
		IsServer        = 1;
		LazyLoad        = 1;
		ShowErrorCount  = 1;

		unguard;
	}
	INT Main(const TCHAR* parms)
	{
		GLazyLoad = 1;
		TArray<StaticMeshRef>		smRefs;
		// start iterating through all the packages given
		TArray<FString> FilesFound = GFileManager->FindFiles( TEXT("..\\Maps\\*.unr"), 1, 0 );
		INT fileIdx;
		for (fileIdx = 0; fileIdx < FilesFound.Num(); fileIdx++)
		{
			FString Pkg = FilesFound(fileIdx);
			warnf( TEXT("Analyzing package %s..."), *Pkg );

			UObject* Package = NULL;
			Package = LoadPackage(NULL,*Pkg,LOAD_NoWarn);
			if (Package != NULL)
			{
				// attempt to load a level from this package
				ULevel *level =	FindObject<ULevel>(Package,TEXT("MyLevel"));
				if (level != NULL)
				{
					// iterate through the actors in the level
					for (INT i = 0; i < level->Actors.Num(); i++)
					{
						AActor *actor = level->Actors(i);
						if (actor != NULL &&
							!actor->bDeleteMe &&
							actor->bStatic)
						{
							AStaticMeshActor *smActor = Cast<AStaticMeshActor>(actor);
							if (smActor != NULL &&
								smActor->StaticMesh != NULL &&
								smActor->StaticMesh->Sections.Num() > 1)
							{
								// yes this is slow, but i'm feeling lazy and it's not critical :|
								INT idx;
								for (idx = 0; idx < smRefs.Num(); idx++)
								{
									if (smRefs(idx).staticMesh == smActor->StaticMesh)
									{
										// increment the level ref cnt
										smRefs(idx).perLevelRefs->Set(fileIdx,smRefs(idx).perLevelRefs->FindRef(fileIdx)+1);
										// increment the total ref cnt
										smRefs(idx).totalRefs++;
										break;
									}
								}
								if (idx == smRefs.Num())
								{
									// add to the end of the list
									idx = smRefs.AddZeroed();
									smRefs(idx).staticMesh = smActor->StaticMesh;
									smRefs(idx).totalRefs = 1;
									smRefs(idx).perLevelRefs = NEW TMap<INT,INT>();
									smRefs(idx).perLevelRefs->Add(fileIdx,1);
									// mark the static mesh so that it isn't gc'd
									smActor->StaticMesh->SetFlags(RF_Keep);
								}
							}
						}
					}
				}
			}
			UObject::CollectGarbage(RF_Native);
		}
		guard(Sort)
		appQsort(&smRefs(0),smRefs.Num(),sizeof(smRefs(0)),(QSORT_COMPARE)SortRefs);
		unguard;
		guard(DumpRefs);
		warnf(TEXT("Static mesh reference counts:"));
		warnf(TEXT(""));
		for (INT i = 0; i < smRefs.Num(); i++)
		{
			warnf(TEXT("%s"),smRefs(i).staticMesh->GetPathName());
			warnf(TEXT("    [total refs: %d, sections: %d]"),smRefs(i).totalRefs,smRefs(i).staticMesh->Sections.Num());
			for (TMap<INT,INT>::TIterator mapIterator(*(smRefs(i).perLevelRefs)); mapIterator; ++mapIterator)
			{
				warnf(TEXT("    - %s [level refs: %d]"),*(FilesFound(mapIterator.Key())),mapIterator.Value());
			}
			warnf(TEXT(""));
			delete (smRefs(i).perLevelRefs);
		}
		unguard;
		GIsRequestingExit = 1;
		return 0;
	}
};
IMPLEMENT_CLASS(UReferenceCountCommandlet)
