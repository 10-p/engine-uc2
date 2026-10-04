/*=============================================================================
	UnPatchCommandlets.h: Utility commandlets for patching UC2.
	Copyright 1997-2004 Epic Games, Inc. All Rights Reserved.

Revision History:
	* Created by Ron Prestenback.
=============================================================================*/
/**
 * Keeps track of two versions of the same struct.
 */
struct FStructPair
{
	/** the name of the struct represented by this pair */
	FString						StructName;

	/** the version of the struct from the original file */
	UStruct*					OriginalStruct;

	/** the version of the struct from the current file */
	UStruct*					CurrentStruct;

	/** 
	 * the child structs of this struct - discrepancies in the child structs between the original
	 * version and the new version are not supported
	 **/
	TMap<FString,FStructPair>	ChildMap;

	void CompileChildMap();

	void PerformComparison( struct FComparisonResults& Result);
	void DisplayDifferences( INT Indent = 0 );

	FStructPair( const TCHAR* inStructName )
	: StructName(inStructName), OriginalStruct(NULL), CurrentStruct(NULL)
	{}

	/**
	 * Copy constructor
	 */
	FStructPair( const FStructPair& Other )
	{
		StructName = Other.StructName;
		OriginalStruct = Other.OriginalStruct;
		CurrentStruct = Other.CurrentStruct;
	}

	virtual ~FStructPair()
	{ }
};

struct FClassPair : public FStructPair
{
	FClassPair( const TCHAR* inClassName )
	: FStructPair(inClassName)
	{}
};


/**
 * Since we must unload the linker associated with the current version of the script package,
 * this data structure stores the information we needed from the linker.
 */
struct FPackageData
{
	/** this is the bare name of the package (e.g. Engine) */
	FString							PackageName;

	/** this is the filename of the package, including any path info (e.g. ../System/OriginalScript/Engine.u) */
	FString							FileName;

	/** these are copied directly from the linker */
	FPackageFileSummary				Summary;
	TArray<FName>					Names;
	TArray<FObjectImport>			Imports;
	TArray<FObjectExport>			Exports;

	UPackage*						Package;

	FPackageData( ULinkerLoad* Linker )
	{
		Package = CastChecked<UPackage>(Linker->LinkerRoot);
		Summary = Linker->Summary;

		PackageName = Package->GetName();
		FileName = Linker->Filename;

		Names = Linker->NameMap;
		Imports = Linker->ImportMap;
		Exports = Linker->ExportMap;
	}
};

class FScriptPatchExporter : public FStringOutputDevice
{
public:
	TArray<FString> PatchClassNames;
	TArray<FString>	LoaderPatchClassNames;

	void SaveToFile();
};

struct FComparisonResults
{
	FString					PackageName;
	TArray<FStructPair>		StructsAdded;
	TArray<FStructPair>		StructsRemoved;
	TArray<FStructPair>		StructsModified;

	TArray<FName>			NamesAdded;
	
	TArray<FObjectImport>	ImportsAdded;
	TArray<FObjectExport>	ExportsAdded;

	INT	ExportCount, NameCount;

	operator UBOOL()
	{
		return StructsAdded.Num() || StructsRemoved.Num() || StructsModified.Num() || NamesAdded.Num() || ImportsAdded.Num() || ExportsAdded.Num();
	}

	void DisplayResults( INT Indent = 0 );
	void ExportResults(FScriptPatchExporter& Writer);

	FComparisonResults( const TCHAR* inName )
	: PackageName(inName)
	{ }

private:
	void ExportObjectData(FScriptPatchExporter& Writer);
	void ExportModified(FScriptPatchExporter& Writer);
	void ExportPackageData(FScriptPatchExporter& Writer);
};


class FPackageComparison
{
public:
	FPackageData& CurrentPackage, &OriginalPackage;

	/**
	 * Builds a list of class pairs contained by this package and its corresponding original version.
	 * 
	 * @param	Classes		[out] map of classname to classes instances
	 */
	void CompileClassMap( TMap<FString,FClassPair>& Classes );

	void ComparePackages( FScriptPatchExporter& Writer );

	void DisplayPackageSummary( FPackageData& Package );
	void DisplaySummaries()
	{
		DisplayPackageSummary(CurrentPackage);
		DisplayPackageSummary(OriginalPackage);
	}

	FPackageComparison( FPackageData& Current, FPackageData& Original )
	: CurrentPackage(Current), OriginalPackage(Original)
	{
	}
};

