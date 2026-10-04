/* scion ======================================================================
* Author: sz
* ============================================================================
* UAnalyzeMeshesCommandlet.cpp
* 
* Given a wildcard list of animation packages. Reprocess the meshes.
*
* usage:
* analyzemeshes pkgname(*.ukx)
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

class UAnalyzeMeshesCommandlet : public UCommandlet
{
	DECLARE_CLASS(UAnalyzeMeshesCommandlet,UCommandlet,CLASS_Transient,Editor);
	
	void StaticConstructor()
	{
		guard(UAnalyzeMeshesCommandlet::StaticConstructor);

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
		guard(UAnalyzeMeshesCommandlet::Main);

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
			if( Pkg.InStr( TEXT(".ukx"),1) == INDEX_NONE )
				continue;

			warnf( TEXT("\nLoading package %s..."), *Pkg );
			UPackage* Package = Cast<UPackage>( LoadPackage(NULL,*Pkg,LOAD_NoWarn) );

			if( Package )
			{
				// for each animation set in the package
				if(1)
				for( TObjectIterator<USkeletalMesh> It; It; ++It )
				{
					USkeletalMesh* Mesh = *It;
					// only check objects within our loaded package
					if( Mesh->IsIn(Package) )
					{
						AnalyzeSkeletalMesh( Mesh );						
					}
				}
				if(1)
				for( TObjectIterator<USkeletalMesh> It; It; ++It )
				{
					USkeletalMesh* Mesh = *It;
					// only check objects within our loaded package
					if( Mesh->IsIn(Package) )
					{
						CheckBoneAliases( Mesh );						
					}
				}
				if(1)
				for( TObjectIterator<USkeletalMesh> It; It; ++It )
				{
					USkeletalMesh* Mesh = *It;
					// only check objects within our loaded package
					if( Mesh->IsIn(Package) )
					{
						CheckAllBoneAliases( Mesh );
					}
				}
			}
			UObject::CollectGarbage(RF_Native);
		}			
		
		GIsRequestingExit=1;
		return 0;
		unguard;
	}

private:

	void AnalyzeSkeletalMesh( USkeletalMesh* Mesh )
	{
		guard(UAnalyzeMeshesCommandlet::AnalyzeSkeletalMesh);
		check(Mesh);

		warnf( TEXT("\nChecking [%s]"), Mesh->GetFullName() );

        // check # of bones
		warnf( TEXT("Ref Bones=%d"), Mesh->RefSkeleton.Num() );
		
		// check LOD models
		for( INT Idx=0; Idx < Mesh->LODModels.Num(); Idx++ )
		{
			const FLOAT MAXFLOAT=3.402823466e+38F;
			FLOAT U[2]={MAXFLOAT,-MAXFLOAT},V[2]={MAXFLOAT,-MAXFLOAT};
			warnf( TEXT("LOD Model=%d"), Idx );
			FStaticLODModel& LODModel = Mesh->LODModels(Idx);			
			// check for GPU stream in each LOD model
			if( !(LODModel.GPUVertexStream.Vertices.Num() || LODModel.GPUMultiBoneSections.Num()) )
			{
				warnf( TEXT("\tNot GPU Skinned!") );  
			}
			else
			{
				// check number of sections in each model
				if( LODModel.GPUMultiBoneSections.Num() )
				{
					warnf( TEXT("\tMulti Bone Sections=%d"), LODModel.GPUMultiBoneSections.Num() );
                    for( INT SecIdx=0; SecIdx < LODModel.GPUMultiBoneSections.Num(); SecIdx++ )
					{
                        FSkelMeshSection& MeshSec = LODModel.GPUMultiBoneSections(SecIdx);
						warnf( TEXT("\tSection[%d] Vertices=%d"), SecIdx, MeshSec.TotalVerts );
					}
				}
				else
				{
					warnf( TEXT("\tVertices=%d"), LODModel.GPUVertexStream.Vertices.Num() );
				}

				for( INT vIdx=0; vIdx < LODModel.GPUVertexStream.Vertices.Num(); vIdx++ )
				{
					FSkinGPUVertex& Vertex = LODModel.GPUVertexStream.Vertices(vIdx);
                    U[0] = Min<FLOAT>( U[0], Vertex.U );
					U[1] = Max<FLOAT>( U[1], Vertex.U );
					V[0] = Min<FLOAT>( V[0], Vertex.V );
					V[1] = Max<FLOAT>( V[1], Vertex.V );
				}

                warnf( TEXT("UVRange U=[%.3f,%.3f] V=[%.3f,%.3f]"), U[0], U[1], V[0], V[1] );
			}
		}		

		unguard;
	}

	void CheckBoneAliases( USkeletalMesh* Mesh )
	{
		check(Mesh);
		static TCHAR* BoneAliases[]=
		{
			TEXT("particlefoot_L"),
			TEXT("particleknee_L"),
			TEXT("particlefoot_R"),
			TEXT("particleknee_R"),
			TEXT("particleshoulder_R"),
			TEXT("particleelbow_R"),
			TEXT("particleshoulder_L"),
			TEXT("particleelbow_L")
		};
        
		for( INT Idx=0; Idx < ARRAY_COUNT(BoneAliases); Idx++ )
		{
			if( Mesh->MatchRefBone( FName(BoneAliases[Idx]) ) == INDEX_NONE )
			{
				warnf( TEXT("[%s] missing bone (%s)"), Mesh->GetFullName(), BoneAliases[Idx] );
			}
		}
	}

	void CheckAllBoneAliases( USkeletalMesh* Mesh )
	{
		check(Mesh);

		for( INT t=0; t < Mesh->TagAliases.Num(); t++)
		{
			UBOOL bMatch = 0;
			for( INT p=0; p < Mesh->RefSkeleton.Num(); p++ )
			{	
				if( Mesh->RefSkeleton(p).Name == Mesh->TagAliases(t) ||
					Mesh->RefSkeleton(p).Name == Mesh->TagNames(t) )
				{
					bMatch=1;					
					break;
				}
			}
			if( !bMatch )
			{
				warnf( TEXT("[%s] bone tag=(%s) referencing missing bone=(%s)"), 
					Mesh->GetFullName(), *Mesh->TagAliases(t), *Mesh->TagNames(t) );
			}
		}
	}
	
};
IMPLEMENT_CLASS(UAnalyzeMeshesCommandlet)

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
