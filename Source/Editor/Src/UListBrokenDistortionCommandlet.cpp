/* scion ======================================================================
* Author: sz
* ============================================================================
* UListBrokenDistortionCommandlet.cpp
* 
* Any static mesh or mesh that has more than one section and using distortion
* distortion, must use distortion in all or none of the sections.  Otherwise,
* it will not display correctly.
*
* usage:
* listbrokendistortion pkgname(*.ukx)
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

class UListBrokenDistortionCommandlet : public UCommandlet
{
	DECLARE_CLASS(UListBrokenDistortionCommandlet,UCommandlet,CLASS_Transient,Editor);
	
	void StaticConstructor()
	{
		guard(UListBrokenDistortionCommandlet::StaticConstructor);

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
		guard(UListBrokenDistortionCommandlet::Main);

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
			UPackage* Package = Cast<UPackage>( LoadPackage(NULL,*Pkg,LOAD_NoWarn) );

			if( Package )
			{
				// static meshes
				for( TObjectIterator<UStaticMesh> It; It; ++It )
				{
					if( It->IsIn(Package) )
					{
						UStaticMesh* pMesh = *It;
						UBOOL bHasPostProcess=0;
						UBOOL bIsBad=0;
						for( INT Pass=0; Pass<2; Pass++ )
						{
							if( Pass == 0 ) 
							{
								for( INT Idx=0; Idx<pMesh->Materials.Num(); Idx++ )
								{
									FStaticMeshMaterial& Material = pMesh->Materials(Idx);
									if( Material.Material && 
										Material.Material->IsA( UPSDistortionBaseMaterial::StaticClass() ) )
									{
										bHasPostProcess=1;
										break;
									}
								}
							}
							else if( bHasPostProcess )
							{
								for( INT Idx=0; Idx<pMesh->Materials.Num(); Idx++ )
								{
									FStaticMeshMaterial& Material = pMesh->Materials(Idx);
									if( Material.Material && 
										!Material.Material->IsA( UPSDistortionBaseMaterial::StaticClass() ) )
									{
										bIsBad=1;
										break;
									}
								}
							}
						}

						if( bIsBad )
						{
							warnf( TEXT("bad usage in static mesh [%s] materials"), pMesh->GetFullName() );
						}
					}
				}

				// skeletal meshes
				for( TObjectIterator<ULodMesh> It; It; ++It )
				{
					if( It->IsIn(Package) )
					{
						ULodMesh* pMesh = *It;
						UBOOL bHasPostProcess=0;
						UBOOL bIsBad=0;
						for( INT Pass=0; Pass<2; Pass++ )
						{
							if( Pass == 0 ) 
							{
								for( INT Idx=0; Idx<pMesh->Materials.Num(); Idx++ )
								{
									UMaterial* Material = pMesh->Materials(Idx);
									if( Material && 
										Material->IsA( UPSDistortionBaseMaterial::StaticClass() ) )
									{
										bHasPostProcess=1;
										break;
									}
								}
							}
							else if( bHasPostProcess )
							{
								for( INT Idx=0; Idx<pMesh->Materials.Num(); Idx++ )
								{
									UMaterial* Material = pMesh->Materials(Idx);
									if( Material && 
										!Material->IsA( UPSDistortionBaseMaterial::StaticClass() ) )
									{
										bIsBad=1;
										break;
									}
								}
							}
						}

						if( bIsBad )
						{
							warnf( TEXT("bad usage in skel mesh [%s] materials"), pMesh->GetFullName() );
						}
					}
				}

				// actor skins
				for( TObjectIterator<AActor> It; It; ++It )
				{
					if( It->IsIn(Package) )
					{
						AActor* pActor = *It;
						UBOOL bHasPostProcess=0;
						UBOOL bIsBad=0;
						for( INT Pass=0; Pass<2; Pass++ )
						{
							if( Pass == 0 ) 
							{
								for( INT Idx=0; Idx<pActor->Skins.Num(); Idx++ )
								{
									UMaterial* Material = pActor->Skins(Idx);
									if( Material &&
										Material->IsA( UPSDistortionBaseMaterial::StaticClass() ) )
									{
										bHasPostProcess=1;
										break;
									}
								}
							}
							else if( bHasPostProcess )
							{
								for( INT Idx=0; Idx<pActor->Skins.Num(); Idx++ )
								{
									UMaterial* Material = pActor->Skins(Idx);
									if( Material &&
										!Material->IsA( UPSDistortionBaseMaterial::StaticClass() ) )
									{
										bIsBad=1;
										break;
									}
								}
							}
						}

						if( bIsBad )
						{
							warnf( TEXT("bad usage in Actor=[%s] skins"), pActor->GetFullName() );
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
IMPLEMENT_CLASS(UListBrokenDistortionCommandlet)

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
