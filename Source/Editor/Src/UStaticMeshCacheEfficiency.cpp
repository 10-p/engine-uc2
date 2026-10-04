#include "EditorPrivate.h"


class UStaticMeshCalcCacheCommandlet : public UCommandlet
{
	DECLARE_CLASS(UStaticMeshCalcCacheCommandlet,UCommandlet,CLASS_Transient,Editor);
	void StaticConstructor()
	{
		guard(UStaticMeshCalcCacheCommandlet::StaticConstructor);

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
		guard(UStaticMeshCalcCacheCommandlet::Main);

		FString PackageName;
		if( !ParseToken(Parms, PackageName, 0) )
		{
			appErrorf( TEXT("Please specify the static mesh package name to check cache efficiency on") );
		}
		warnf(TEXT("Loading package name %s"),*PackageName);
		UPackage* Package = (UPackage*)LoadPackage(NULL,*PackageName,LOAD_NoWarn);
		DWORD dwTotalVerts = 0;
		DWORD dwTotalTLMissed = 0;
		DWORD dwTotalDataMissed = 0;
		// Find each static mesh in the package
		for (TObjectIterator<UStaticMesh> It; *It; ++It)
		{
			if (It->IsIn(Package))
			{
				warnf(TEXT("Analyzing %s"),It->GetName());
				warnf(TEXT(""));
				// Convert the pointer so we can get at the sections
				UStaticMesh* pMesh = Cast<UStaticMesh>(*It);
				warnf(TEXT("\tSection Degenerates T&L Misses T&L Misses%% Data Misses Data Misses%%"));
				// Analyze each section
				for (INT nSection = 0; nSection < pMesh->Sections.Num();
					nSection++)
				{
					if (pMesh->Sections(nSection).NumPrimitives)
					{
						// Are we a strip
						UBOOL bIsStrip = pMesh->Sections(nSection).IsStrip;
						// Get a pointer to the indices
						_WORD* pIndices = &pMesh->IndexBuffer.Indices(
							pMesh->Sections(nSection).FirstIndex);
						// Figure out how many primitives there are for % calcs
						_WORD wNumVerts = pMesh->Sections(nSection).MaxVertexIndex -
							pMesh->Sections(nSection).MinVertexIndex;
						dwTotalVerts += wNumVerts;
						// These are the out values that are calculated
						DWORD dwDegens = 0, dwTLMisses = 0, dwDataMisses = 0;
						// Go figure out how efficient this chunk of mesh is
						CalculateVertexCacheHits(bIsStrip,
							sizeof(FStaticMeshVertex),pIndices,wNumVerts,
							&dwDegens,&dwTLMisses,&dwDataMisses);
						dwTotalTLMissed += dwTLMisses;
						dwTotalDataMissed += dwDataMisses;
						// Dump the results
						warnf(TEXT("\t%7d %11d %10d %11d %10d %11d"),
							nSection,dwDegens,dwTLMisses,
							appRound((FLOAT)dwTLMisses / (FLOAT)wNumVerts * 100.f),
							dwDataMisses,
							appRound((FLOAT)dwDataMisses / (FLOAT)wNumVerts * 100.f));
						//TODO: Compare with a tristripped, data cache optimized version
					}
				}
				warnf(TEXT(""));
			}
		}
		warnf(TEXT("Vert Total %d, Average T&L Miss%% %d, Average Data Miss%% %d"),
			dwTotalVerts,
			appRound((FLOAT)dwTotalTLMissed / (FLOAT)dwTotalVerts * 100.f),
			appRound((FLOAT)dwTotalDataMissed / (FLOAT)dwTotalVerts * 100.f));
		GIsRequestingExit=1;
		return 0;
		unguard;
	}
};
IMPLEMENT_CLASS(UStaticMeshCalcCacheCommandlet)
