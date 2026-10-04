
#include "../../Editor/Src/EditorPrivate.h"
#include "UnLinker.h"
#include "UnPatchCommandlets.h"

static FString GetObjectExportName( UObject* Obj )
{
	FString Result = Obj->GetPathName();
	return Result.Replace(TEXT("."), TEXT("_")) + TEXT("_Export");
}

static INT FindPathSeparator( FString Path )
{
	if ( Path.Right(1) == TEXT("/") )
		Path = Path.LeftChop(1);
	else if ( Path.Right(1) == TEXT("\\") )
		Path = Path.LeftChop(1);

	INT pos = Path.InStr(TEXT("/"),TRUE);
	if ( pos == INDEX_NONE )
		pos = Path.InStr(TEXT("\\"),TRUE);

	return pos;
}

/*----------------------------------------------------------------------------
	FStructComparison.
----------------------------------------------------------------------------*/
struct FStructComparison
{
public:
	UBOOL operator==( const FStructComparison& Other ) const
	{
		if ( (Struct == NULL) != (Other.Struct == NULL) )
			return FALSE;

		if ( Struct != NULL )
		{
			TArray<BYTE>& Script = Struct->Script,
				&OtherScript = Other.Struct->Script;

			if ( Script.Num() != OtherScript.Num() )
				return FALSE;

			for ( INT i = 0; i < Script.Num(); i++ )
			{
				if ( Script(i) != OtherScript(i) )
					return FALSE;
			}
		}

		return TRUE;
	}
	UBOOL operator!=( const FStructComparison& Other ) const
	{
		if ( (Struct == NULL) != (Other.Struct == NULL) )
			return TRUE;

		if ( Struct != NULL )
		{
			TArray<BYTE>& Script = Struct->Script,
				&OtherScript = Other.Struct->Script;

			if ( Script.Num() != OtherScript.Num() )
				return TRUE;

			for ( INT i = 0; i < Script.Num(); i++ )
			{
				if ( Script(i) != OtherScript(i) )
					return TRUE;
			}
		}

		return FALSE;
	}

	FStructComparison( UStruct* inStruct )
	: Struct(inStruct)
	{}

	UStruct* Struct;
};

/*----------------------------------------------------------------------------
	FStructPair.
----------------------------------------------------------------------------*/
void FStructPair::CompileChildMap()
{
	if ( OriginalStruct != NULL )
	{
		for ( TFieldIterator<UStruct> It(OriginalStruct); It && It.GetStruct() == OriginalStruct; ++It )
		{
			UStruct* Struct = *It;
			FString StructId = Struct->GetPathName(Struct->GetOwnerClass());
			FStructPair* Pair = ChildMap.Find(*StructId);
			if ( Pair == NULL )
				Pair = &ChildMap.Set(*StructId, FStructPair(*StructId));

			check(Pair);
			Pair->OriginalStruct = Struct;
		}
	}

	if ( CurrentStruct != NULL )
	{
		for ( TFieldIterator<UStruct> It(CurrentStruct); It && It.GetStruct() == CurrentStruct; ++It )
		{
			UStruct* Struct = *It;
			FString StructId = Struct->GetPathName(Struct->GetOwnerClass());
			FStructPair* Pair = ChildMap.Find(*StructId);
			if ( Pair == NULL )
				Pair = &ChildMap.Set(*StructId, FStructPair(*StructId));

			check(Pair);
			Pair->CurrentStruct = Struct;
		}
	}

	// now verify that every struct has a corresponding struct in the other version of the package
	for ( TMap<FString,FStructPair>::TIterator It(ChildMap); It; ++It )
	{
		FStructPair& Pair = It.Value();
		Pair.CompileChildMap();
	}
}

void FStructPair::PerformComparison(FComparisonResults& Result)
{
	if ( CurrentStruct == NULL )
	{
		check(OriginalStruct);
		new(Result.StructsRemoved) FStructPair(*this);
	}
	else if ( OriginalStruct == NULL )
	{
		check(CurrentStruct);
		new(Result.StructsAdded) FStructPair(*this);
	}
	else
	{
		FStructComparison Original(OriginalStruct), Current(CurrentStruct);
		if ( Original != Current )
		{
			new(Result.StructsModified) FStructPair(*this);
		}
	}

	for ( TMap<FString,FStructPair>::TIterator It(ChildMap); It; ++It )
	{
		FStructPair& Pair = It.Value();
		Pair.PerformComparison(Result);
	}
}

void FStructPair::DisplayDifferences( INT Indent /* = 0  */ )
{
	if ( CurrentStruct == NULL )
	{
		check(OriginalStruct);
		warnf(TEXT("%sRemoved '%s' (doesn't exist in current version)"), appSpc(Indent), OriginalStruct->GetFullName());
	}

	else if ( OriginalStruct == NULL )
	{
		check(CurrentStruct);
		warnf(TEXT("%sAdded '%s' (doesn't exist in original version)"), appSpc(Indent), CurrentStruct->GetFullName());
	}

	else if ( OriginalStruct->Script.Num() != CurrentStruct->Script.Num() )
	{
		warnf(TEXT("%sChanged '%s' (%i/%i)"), appSpc(Indent), CurrentStruct->GetFullName(), OriginalStruct->Script.Num(), CurrentStruct->Script.Num());
	}

	for ( TMap<FString,FStructPair>::TIterator It(ChildMap); It; ++It )
	{
		FStructPair& Pair = It.Value();
		Pair.DisplayDifferences(Indent + 2);
	}
}

/*----------------------------------------------------------------------------
	FPackageComparison.
----------------------------------------------------------------------------*/
/**
 * Builds a list of class pairs contained by this package and its corresponding original version.
 * 
 * @param	Classes		[out] map of classname to classes instances
 */
void FPackageComparison::CompileClassMap( TMap<FString,FClassPair>& Classes )
{
	guard(FPackageComparison::CompileClassMap);

	for ( TObjectIterator<UClass> It; It; ++It )
	{
		UClass* Class = *It;
		if ( Class->IsIn(CurrentPackage.Package) )
		{
			FClassPair* Pair = Classes.Find(Class->GetName());
			if ( Pair == NULL )
				Pair = &Classes.Set(Class->GetName(), FClassPair(Class->GetName()));

			check(Pair);
			Pair->CurrentStruct = Class;
		}
		else if ( Class->IsIn(OriginalPackage.Package) )
		{
			FClassPair* Pair = Classes.Find(Class->GetName());
			if ( Pair == NULL )
				Pair = &Classes.Set(Class->GetName(), FClassPair(Class->GetName()));

			check(Pair);
			Pair->OriginalStruct = Class;
		}
	}

	// verify that all pairs contain both an original class and a current class
	for ( TMap<FString,FClassPair>::TIterator PairIt(Classes); PairIt; ++PairIt )
	{
		FString ClassName = PairIt.Key();
		FClassPair& Pair = PairIt.Value();

		if ( Pair.OriginalStruct == NULL )
		{
			warnf(TEXT(" Added class '%s' (doesn't exist in original package)"), *ClassName);
		}

		if ( Pair.CurrentStruct == NULL )
		{
			warnf(TEXT(" Removed class '%s' (doesn't exist in current package)"), *ClassName);
		}

		Pair.CompileChildMap();
	}
	unguard;
}

void FPackageComparison::ComparePackages( FScriptPatchExporter& Writer )
{
	guard(FPackageComparison::ComparePackages);

	TMap<FString,FClassPair> Classes;
	CompileClassMap(Classes);

	FComparisonResults Comparison(*CurrentPackage.PackageName);
	Comparison.ExportCount = OriginalPackage.Exports.Num();
	Comparison.NameCount = OriginalPackage.Names.Num();

	for ( INT i = OriginalPackage.Names.Num(); i < CurrentPackage.Names.Num(); i++ )
		new(Comparison.NamesAdded) FName(*CurrentPackage.Names(i));

	for ( INT i = OriginalPackage.Imports.Num(); i < CurrentPackage.Imports.Num(); i++ )
	{
		INT idx = Comparison.ImportsAdded.AddZeroed();
		Comparison.ImportsAdded(idx) = CurrentPackage.Imports(i);
	}

	for ( INT i = OriginalPackage.Exports.Num(); i < CurrentPackage.Exports.Num(); i++ )
	{
		FObjectExport& Export = CurrentPackage.Exports(i);

		INT idx = Comparison.ExportsAdded.AddZeroed();
		Comparison.ExportsAdded(idx) = Export;
	}

	for ( TMap<FString,FClassPair>::TIterator PairIt(Classes); PairIt; ++PairIt )
	{
		FString ClassName = PairIt.Key();
		FClassPair& Pair = PairIt.Value();

		Pair.PerformComparison(Comparison);
	}

	if ( Comparison )
	{
		warnf(TEXT("%sResults for %s:"), LINE_TERMINATOR, *CurrentPackage.PackageName);
		Comparison.DisplayResults(3);

		Comparison.ExportResults(Writer);
	}

	unguard;
}

void FPackageComparison::DisplayPackageSummary( FPackageData& Package )
{
	guard(FPackageComparison::DisplayPackageSummary);

	GWarn->Log( TEXT("********************************************") );
	GWarn->Logf( TEXT("Package '%s' Summary"), *Package.FileName );
	GWarn->Log( TEXT("--------------------------------------------") );

	GWarn->Logf( TEXT("\tPackageFlags: %x"), Package.Summary.PackageFlags );
	GWarn->Logf( TEXT("\t   NameCount: %d"), Package.Summary.NameCount );
	GWarn->Logf( TEXT("\t  NameOffset: %d"), Package.Summary.NameOffset );
	GWarn->Logf( TEXT("\t ImportCount: %d"), Package.Summary.ImportCount );
	GWarn->Logf( TEXT("\tImportOffset: %d"), Package.Summary.ImportOffset );
	GWarn->Logf( TEXT("\t ExportCount: %d"), Package.Summary.ExportCount );
	GWarn->Logf( TEXT("\tExportOffset: %d"), Package.Summary.ExportOffset );

	FString szGUID = Package.Summary.Guid.String();
	GWarn->Logf( TEXT("\t        Guid: %s"), *szGUID );
	GWarn->Log ( TEXT("\t Generations:"));
	for( INT i = 0; i < Package.Summary.Generations.Num(); ++i )
	{
		const FGenerationInfo& generationInfo = Package.Summary.Generations( i );
		GWarn->Logf( TEXT("\t\t%d) ExportCount=%d, NameCount=%d"), i, generationInfo.ExportCount, generationInfo.NameCount );
	}

	INT ClassCount = 0;
	for ( TObjectIterator<UClass> It; It; ++It )
	{
		if ( It->IsIn(Package.Package) )
			ClassCount++;
	}

	warnf(TEXT("Package %s contains %i classes"), *Package.FileName, ClassCount);

	unguard;
}

/*----------------------------------------------------------------------------
	FComparisonResults.
----------------------------------------------------------------------------*/
void FComparisonResults::DisplayResults( INT Indent )
{
	guard(FComparisonResults::DisplayResults);

	if ( StructsAdded.Num() || NamesAdded.Num() || ImportsAdded.Num() || ExportsAdded.Num() )
	{
		warnf(TEXT("%sAdded:"), appSpc(Indent));

		if ( NamesAdded.Num() )
		{
			warnf(TEXT("%s(Names)"), appSpc(Indent+1));
			for ( INT i = 0; i < NamesAdded.Num(); i++ )
			{
				FName& Name = NamesAdded(i);
				GWarn->Logf(TEXT("%s%s"),appSpc(Indent+3),*Name);
			}
		}
		if ( ImportsAdded.Num() )
		{
			warnf(TEXT("%s(Imports)"), appSpc(Indent+1));
			for ( INT i = 0; i < ImportsAdded.Num(); i++ )
			{
				FObjectImport& Import = ImportsAdded(i);
				GWarn->Logf(TEXT("%s%s"),appSpc(Indent+3),*Import.ObjectName);
			}
		}
		if ( ExportsAdded.Num() )
		{
			warnf(TEXT("%s(Exports)"), appSpc(Indent+1));
			for ( INT i = 0; i < ExportsAdded.Num(); i++ )
			{
				FObjectExport& Export = ExportsAdded(i);
				GWarn->Logf(TEXT("%s%s"),appSpc(Indent+3),*Export.ObjectName);
			}
		}
		if ( StructsAdded.Num() )
		{
			warnf(TEXT("%s(Structs)"), appSpc(Indent+1));
			for ( INT i = 0; i < StructsAdded.Num(); i++ )
			{
				FStructPair& Pair = StructsAdded(i);
				GWarn->Logf(TEXT("%s%s"),appSpc(Indent+3),Pair.CurrentStruct->GetPathName());
			}
		}
	}

	if ( StructsRemoved.Num() )
	{
		warnf(TEXT("%sRemoved:"), appSpc(Indent));
		for ( INT i = 0; i < StructsRemoved.Num(); i++ )
		{
			FStructPair& Pair = StructsRemoved(i);
			GWarn->Logf(TEXT("%s%s"),appSpc(Indent+3),Pair.OriginalStruct->GetPathName());
		}
	}

	if ( StructsModified.Num() )
	{
		warnf(TEXT("%sModifed:"), appSpc(Indent));
		for ( INT i = 0; i < StructsModified.Num(); i++ )
		{
			FStructPair& Pair = StructsModified(i);
			GWarn->Logf(TEXT("%s%s (%i/%i)"),appSpc(Indent+3),Pair.CurrentStruct->GetPathName(), Pair.OriginalStruct->Script.Num(), Pair.CurrentStruct->Script.Num());
		}
	}

	unguard;
}

void FComparisonResults::ExportResults( FScriptPatchExporter& Writer )
{
	guard(FComparisonResults::ExportResults);

	ExportObjectData(Writer);
	ExportModified(Writer);
	ExportPackageData(Writer);

	unguard;
}

void FComparisonResults::ExportObjectData(FScriptPatchExporter& Writer)
{
	guard(FComparisonResults::ExportObjectData);

	for ( INT i = 0; i < ExportsAdded.Num(); i++ )
	{
		FObjectExport& Export = ExportsAdded(i);

		// grab the linker for this object
		check(Export._Object);
		ULinkerLoad* Linker = Export._Object->GetLinker();
		check(Linker);

		FString PatchClass = GetObjectExportName(Export._Object);

		// ULinkerLoad keeps its Serialize function private, so grab the loader's
		// underlying file reader
		FArchive* Reader = Linker->Loader;
		INT PrevPos = Reader->Tell();

		// move to the beginning of this object's data on disk
		Reader->Seek(Export.SerialOffset);

		TArray<BYTE> Data;
		Data.Add(Export.SerialSize);

		// output the raw object data to the Data array, so we can export it
		Reader->Serialize(Data.GetData(), Export.SerialSize*sizeof(BYTE));
		Reader->Seek(PrevPos);

		// now we can create the byte array that will be embedded in the exe
		// class declaration
		Writer.Logf(TEXT("struct %s : public FPatchData\r\n{\r\n"), *PatchClass);

		// constructor declaration
		Writer.Logf(TEXT("\t%s()\r\n\t{\r\n"), *PatchClass);

		// constructor body
		Writer.Logf(TEXT("\t\tSize = %i;\r\n"), Export.SerialSize);
		Writer.Logf(TEXT("\t\tDataName = TEXT(\"%s\");\r\n"), *Export.ObjectName);
		Writer.Logf(TEXT("\t\tBYTE bytes[%i]=\r\n\t\t{"), Data.Num());

		// bytecode array definition
		for ( INT Index = 0; Index < Data.Num(); Index++ )
		{
			// 8 bytes-per-line
			if ( Index % 8 == 0 )
				Writer.Log(TEXT("\r\n\t\t\t"));

			Writer.Logf(TEXT("0x%02X%s"), Data(Index), Index < Data.Num() - 1 ? TEXT(",") : TEXT(""));
		}
		Writer.Log(TEXT("\r\n\t\t};\r\n"));

		Writer.Logf(TEXT("\t\tappMemcpy(ObjectData,bytes,%i*sizeof(BYTE));\r\n"), Data.Num());
		Writer.Log(TEXT("\t\tData = ObjectData;\r\n\t}\r\n\r\n"));

		// remainder of patch class
		Writer.Log(TEXT("private:\r\n"));
		Writer.Logf(TEXT("\tBYTE\tObjectData[%i];\r\n};\r\n"), Data.Num());
	}

	unguard;
}

void FComparisonResults::ExportModified( FScriptPatchExporter& Writer )
{
	guard(FComparisonResults::ExportModified);

	for ( INT i = 0; i < StructsModified.Num(); i++ )
	{
		UStruct* Struct = StructsModified(i).CurrentStruct;

		FString FullName = Struct->GetPathName();
		TArray<FString>		NameChain;

		FullName.ParseIntoArray(TEXT("."), &NameChain);

		INT x = Writer.PatchClassNames.AddZeroed();
		FString& PatchClass = Writer.PatchClassNames(x);

		for ( INT NameIndex = 0; NameIndex < NameChain.Num(); NameIndex++ )
		{
			if ( PatchClass.Len() )
				PatchClass += TEXT("_");

			PatchClass += NameChain(NameIndex);
		}

		// class declaration
		Writer.Logf(TEXT("struct %s : public FPatchData\r\n{\r\n"), *PatchClass);

		// constructor declaration
		Writer.Logf(TEXT("\t%s()\r\n\t{\r\n"), *PatchClass);

		// constructor body
		Writer.Logf(TEXT("\t\tSize = %i;\r\n"), Struct->Script.Num());
		Writer.Logf(TEXT("\t\tDataName = TEXT(\"%s\");\r\n"), *FullName);
		Writer.Logf(TEXT("\t\tBYTE bytes[%i]=\r\n\t\t{"), Struct->Script.Num());

		// bytecode array definition
		for ( INT ScriptIndex = 0; ScriptIndex < Struct->Script.Num(); ScriptIndex++ )
		{
			// 8 bytes-per-line
			if ( ScriptIndex % 8 == 0 )
				Writer.Log(TEXT("\r\n\t\t\t"));

			Writer.Logf(TEXT("0x%02X%s"), Struct->Script(ScriptIndex), ScriptIndex < Struct->Script.Num() - 1 ? TEXT(",") : TEXT(""));
		}
		Writer.Log(TEXT("\r\n\t\t};\r\n"));

		Writer.Logf(TEXT("\t\tappMemcpy(PatchBytes,bytes,%i*sizeof(BYTE));\r\n"), Struct->Script.Num());
		Writer.Log(TEXT("\t\tData = PatchBytes;\r\n\t}\r\n\r\n"));

		// remainder of patch class
		Writer.Log(TEXT("private:\r\n"));
		Writer.Logf(TEXT("\tBYTE\tPatchBytes[%i];\r\n};\r\n"), Struct->Script.Num());
	}

	unguard;
}

void FComparisonResults::ExportPackageData(FScriptPatchExporter& Writer)
{
	guard(FComparisonResults::ExportPackageData);

	INT idx = Writer.LoaderPatchClassNames.AddZeroed();
	FString& PatchClass = Writer.LoaderPatchClassNames(idx);
	
	PatchClass = PackageName + TEXT("_Patch");

	// class declaration
	Writer.Logf(TEXT("struct %s : public FLinkerPatchData\r\n{\r\n"), *PatchClass);

	// constructor declaration
	Writer.Logf(TEXT("\t%s()\r\n\t{\r\n"), *PatchClass);

	// initialization
	Writer.Logf(TEXT("\t\tPackageName = TEXT(\"%s\");\r\n"), *PackageName);
	Writer.Logf(TEXT("\t\tNameCount = %i;\r\n"), NameCount);
	Writer.Logf(TEXT("\t\tExportCount = %i;\r\n"), ExportCount);

	// names
	for ( INT i = 0; i < NamesAdded.Num(); i++ )
	{
		Writer.Logf(TEXT("\t\tnew(Names) FName(TEXT(\"%s\"));\r\n"), *NamesAdded(i));
	}

	for ( INT i = 0; i < ImportsAdded.Num(); i++ )
	{
		FObjectImport& Import = ImportsAdded(i);

		Writer.Logf(TEXT("\t\tnew(Imports) FObjectImport(BuildImport(FName(TEXT(\"%s\")),FName(TEXT(\"%s\")),FName(TEXT(\"%s\")),%i));\r\n"),
			*Import.ObjectName, *Import.ClassName, *Import.ClassPackage, Import.PackageIndex);
	}

	TArray<FString> ObjectData;
	for ( INT i = 0; i < ExportsAdded.Num(); i++ )
	{
		FObjectExport& Export = ExportsAdded(i);
		new(ObjectData) FString(GetObjectExportName(Export._Object));

		Writer.Logf(TEXT("\t\tnew(Exports) FObjectExport(BuildExport(FName(TEXT(\"%s\")), %d, %i, %i, %i));\r\n"),
			*Export.ObjectName, Export.ObjectFlags, Export.ClassIndex, Export.SuperIndex, Export.PackageIndex);
	}
	if ( ObjectData.Num() > 0 )
	{
		Writer.Log(TEXT("\r\n"));
		for ( INT i = 0; i < ObjectData.Num(); i++ )
		{
			Writer.Logf(TEXT("\t\tObjects.AddItem(new %s());\r\n"), *ObjectData(i));
		}
	}
	if ( Writer.PatchClassNames.Num() )
	{
		Writer.Log(TEXT("\r\n"));
		for ( INT i = 0; i < Writer.PatchClassNames.Num(); i++ )
		{
			Writer.Logf(TEXT("\t\tScriptPatches.AddItem(new %s());\r\n"), *Writer.PatchClassNames(i));
		}

		Writer.PatchClassNames.Empty();
	}

	Writer.Log(TEXT("\t}\r\n};\r\n"));

	unguard;
}

/*----------------------------------------------------------------------------
	FScriptPatchExporter.
----------------------------------------------------------------------------*/
void FScriptPatchExporter::SaveToFile()
{
	guard(FScriptPatchExporter::SaveToFile);

	Log(TEXT("\r\nvoid BuildPatchList( TArray<FLinkerPatchData*>& PackagePatches )\r\n{\r\n"));
	for ( INT i = 0; i < LoaderPatchClassNames.Num(); i++ )
	{
		Logf(TEXT("\tPackagePatches.AddItem(new %s());\r\n"), *LoaderPatchClassNames(i));
	}
	Log(TEXT("}\r\n\r\n"));

	if ( !appSaveStringToFile(*this, TEXT("..\\Engine\\Inc\\ScriptPatches.h")) )
	{
		warnf(TEXT("Failed to overwrite ..\\Engine\\Inc\\ScriptPatches.h!"));
	}

	unguard;
}

/*----------------------------------------------------------------------------
	FPackageName.
----------------------------------------------------------------------------*/
/**
 * Simple wrapper class for preventing normal FStrings from using the specialized comparison function below
 */
class FPackageName : public FString
{
public:
	FPackageName( const TCHAR* inData )
	: FString(inData)
	{}
};

/**
 * Comparison function for sorting package names according to their position in the EditPackages array
 */
static INT Compare( FPackageName& StringA, FPackageName& StringB )
{
	INT FirstIndex = 0, SecondIndex = 0;
	for ( INT i = 0; i < GEditor->EditPackages.Num(); i++ )
	{
		if ( StringA == GEditor->EditPackages(i) )
			FirstIndex = i;

		if ( StringB == GEditor->EditPackages(i) )
			SecondIndex = i;
	}

	return FirstIndex - SecondIndex;
}

/*----------------------------------------------------------------------------
	UMultiPackageLoadCommandlet.
----------------------------------------------------------------------------*/
class UMultiPackageLoadCommandlet : public UCommandlet
{
	DECLARE_ABSTRACT_CLASS(UMultiPackageLoadCommandlet,UCommandlet,CLASS_Transient,Editor);

	/** Used for generating unique package names for renamed packages */
	INT PackageNumber;

	/**
	 * Creates and initializes the editor engine.
	 */
	void InitializeCommandlet() const
	{
		UClass* EditorEngineClass = UObject::StaticLoadClass( UEditorEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.EditorEngine"), NULL, LOAD_NoFail | LOAD_DisallowFiles, NULL );
		GEditor  = ConstructObject<UEditorEngine>( EditorEngineClass );
		GEditor->UseSound = 0;
		GEditor->InitEditor();
		GIsRequestingExit = 1;
	}

	/**
	 * Static constructor for the commandlet
	 */
	void StaticConstructor()
	{
		guard(UMultiPackageLoadCommandlet::StaticConstructor);

		LogToStdout     = 0;
		IsClient        = 1;
		IsEditor        = 1;
		IsServer        = 1;
		LazyLoad        = 1;
		ShowErrorCount  = 1;

		GUglyHackFlags |= 64;
		PackageNumber = 0;

		unguard;
	}

	/**
	 * Renames an existing package.
	 * 
	 * @param	PackageName			current name of the package
	 * @param	PreviousPackageTag	text to append to the end of the package's current name when it's renamed
	 *
	 * @return	the new name of the renamed package, or NAME_None if there weren't any existing packages by that name
	 */
	FName RenameExistingPackage( FString PackageName, const TCHAR* PreviousPackageTag = NULL )
	{
		UPackage* ExistingPackage = FindObject<UPackage>( NULL, *PackageName );
		if ( ExistingPackage != NULL )
		{
			FString PackageTag = PreviousPackageTag ? PreviousPackageTag : appItoa(PackageNumber++);

			// detach this package's objects from the linker and delete it, so that we can unload this file
			UObject::ResetLoaders(ExistingPackage, FALSE, TRUE);

			// rename the package so that we can really load packages from different locations that have the same name
			// doing this enables each successively loaded version of this file to receive its own unique
			// copy of all the objects contained in the package
			ExistingPackage->Rename( *FString::Printf(TEXT("%s_%s"), ExistingPackage->GetName(), *PackageTag) );
			return ExistingPackage->GetFName();
		}

		return NAME_None;
	}

	/**
	 * For all duplicate packages, initializes any native classes that aren't part of the package's export map
	 * (and thus, won't be created and serialized as part of UObject::EndLoad())
	 * 
	 * @param	PackageRenameMap	list of packages that have been renamed;  maps the new name of the old package to
	 *								the new version of the package.  Any classes that have NULL linkers and are contained
	 *								within one of the new packages will be duplicated
	 */
	void DuplicateNativeClasses( TMap<FName,UPackage*>& PackageRenameMap, FName* OuterName = NULL )
	{
		TMap<UPackage*,ULinkerLoad*> PackageLinkerMap;
		for( TObjectIterator<UClass> It ; It; ++It )
		{
			UClass* pClass = *It;
			if ( OuterName != NULL && It->GetOuter()->GetFName() != *OuterName )
				continue;

			// if the class doesn't have a linker, it is either
			// A) a script-less native class
			// B) not part of the package's export map
			if( !pClass->GetLinker() )
			{
				UPackage* ClassPackage = pClass->GetOuterUPackage();

				// PackageLinkerMap is just an optimization - caches the linkers that we've already found
				ULinkerLoad** pLinker = PackageLinkerMap.Find(ClassPackage);
				ULinkerLoad* Linker = pLinker != NULL
					? Linker = *pLinker
					: Linker = UObject::GetPackageLinker(ClassPackage, NULL, LOAD_Quiet|LOAD_NoWarn|LOAD_NoVerify,NULL,NULL);

				// if the class has no linker, it might be from a package that has been renamed (in which case we do not need to create a new version of the class)
				if ( Linker == NULL )
				{
					// the original class's package will exist in the PackageRenameMap only if it has been renamed
					// (indicating that we have loaded a duplicate version of the package)
					UPackage** pNewClassPackage = PackageRenameMap.Find(ClassPackage->GetFName());
					if ( pNewClassPackage )
					{
						UPackage* NewClassPackage=*pNewClassPackage;
						check(NewClassPackage);

						// see if there is already an existing copy of this class that was created when the new package was loaded
						UClass* ClassCopy = FindObject<UClass>(NewClassPackage, pClass->GetName(), 1);

						// if there isn't a class contained by the new version of the package, it means the class isn't part of a
						// package's export map, but does exist in the package's import map.  This means trouble since UObject::CreateImport()
						// won't create new copies of native transient objects; therefore, create a new version of the class here
						if ( ClassCopy == NULL )
						{
							// just brute-force initialize the new class's properties, as there are various issues with doing it any other way
							// this guarantees that the new version of the class receives its own copy of all properties
							ClassCopy = Cast<UClass>(StaticConstructObject(pClass->GetClass(), NewClassPackage, pClass->GetName(), pClass->GetFlags()));

							// UClass properties
							ClassCopy->ClassReps = pClass->ClassReps;
							ClassCopy->NetFields = pClass->NetFields;
							ClassCopy->Dependencies = pClass->Dependencies;
							ClassCopy->PackageImports = pClass->PackageImports;
							ClassCopy->HideCategories = pClass->HideCategories;
							ClassCopy->DependentOn = pClass->DependentOn;
							ClassCopy->ClassFlags = pClass->ClassFlags;
							ClassCopy->ClassWithin = pClass->ClassWithin;
							ClassCopy->ClassConfigName = pClass->ClassConfigName;
							ClassCopy->DefaultPropText = pClass->DefaultPropText;
							ClassCopy->ClassUnique = pClass->ClassUnique;
							ClassCopy->bPrecached = pClass->bPrecached;
							ClassCopy->ClassConstructor = pClass->ClassConstructor;
							ClassCopy->ClassStaticConstructor = pClass->ClassStaticConstructor;

							// UState properties
							ClassCopy->ProbeMask = pClass->ProbeMask;
							ClassCopy->IgnoreMask = pClass->IgnoreMask;
							ClassCopy->FuncMap = pClass->FuncMap;
							ClassCopy->LabelTableOffset = pClass->LabelTableOffset;
							ClassCopy->StateFlags = pClass->StateFlags;

							// UStruct properties
							ClassCopy->Children = pClass->Children;
							ClassCopy->PropertiesSize = pClass->PropertiesSize;
							ClassCopy->Script = pClass->Script;
							ClassCopy->StructFlags = pClass->StructFlags;


							// UField properties
							ClassCopy->SuperField = pClass->SuperField;
							ClassCopy->Next = pClass->Next;

							// now initialize the class defaults
							ClassCopy->Defaults.Empty(ClassCopy->GetPropertiesSize());
							ClassCopy->Defaults.Add(ClassCopy->GetPropertiesSize());
							ClassCopy->GetDefaultObject()->InitClassDefaultObject(ClassCopy);
						}

						// this class was previously loaded in a different linker
						LoadObject<UClass>( ClassCopy->GetOuter(), ClassCopy->GetName(), NULL, LOAD_Quiet|LOAD_NoWarn, NULL );
					}
				}
				else
				{
					// cache the result so we don't have to iterate through the GObjLoaders array looking for this loader again
					PackageLinkerMap.Set(ClassPackage,Linker);
				}
			}
		}
	}

	/**
	 * Loads a list of packages, correctly handling multiple versions of the same package.
	 * 
	 * @param	Packages				[out] list of packages that were loaded
	 * @param	PackageWildcard			package filename to load - supports wildcards (*,?)
	 * @param	PreviousPackageTag		text that should be used to tag any packages that must be renamed
	 */
	void LoadPackages( TArray<FPackageData>& Packages, const FString& PackageWildcard, const TCHAR* PreviousPackageTag = NULL )
	{
		guard(UPatchScriptCommandlet::LoadPackages);

		FString Path;
		INT pos = FindPathSeparator(PackageWildcard);
		if ( pos != INDEX_NONE )
			Path = PackageWildcard.Left(pos);

		TMap<FName,UPackage*> PackageRenameMap;

		TArray<FString> PackageNames = GFileManager->FindFiles(*PackageWildcard, 1, 0);
		TArray<FPackageName> SortedPackageNames;

		for (INT idx = 0; idx < PackageNames.Num(); idx++)
		{
			FString& PackageName = PackageNames(idx);
			if ( PackageName.Right(2) == TEXT(".u") )
				PackageName = PackageName.LeftChop(2);

			if ( PackageName == TEXT("Core") )
			{
				debugf(TEXT("Skipping Core..."));
				continue;
			}

			// results returned from GFileManager->FindFiles will not contain path information,
			// so we may need to re-add it here
			FString Filename = Path.Len() ? Path * PackageName : PackageName;

			// if there is already a package loaded using this name, rename it so that we actually load the specified file from disk
			FName OldPackageName = RenameExistingPackage(PackageName,PreviousPackageTag);

			UPackage* Package = CreatePackage(NULL,*PackageName);
			if ( OldPackageName != NAME_None )
			{
				PackageRenameMap.Set(OldPackageName, Package);
			}

			new(SortedPackageNames) FPackageName(*PackageName);
		}


		// we need to load the new versions of the packages in the order that they appear in EditPackages
		// so that all of the inter-package dependencies are resolved correctly
		Sort(&SortedPackageNames(0), SortedPackageNames.Num());

		BeginLoad();
		for ( INT i = 0; i < SortedPackageNames.Num(); i++ )
		{
			FString& PackageName = SortedPackageNames(i);

			// results returned from GFileManager->FindFiles will not contain path information,
			// so we may need to re-add it here
			FString Filename = Path.Len() ? Path * PackageName : PackageName;

			UPackage* Package = Cast<UPackage>(LoadPackage(NULL,*Filename,LOAD_NoWarn|LOAD_NoVerify));
			if( Package == NULL )
			{
				appErrorf( TEXT("Can't find package '%s'"), *Filename );
			}

			ULinkerLoad* pLinker = UObject::GetPackageLinker(Package, *Filename, LOAD_NoWarn|LOAD_NoVerify,NULL,NULL);
			check(pLinker);

			new(Packages) FPackageData(pLinker);
		}

		// fixup any scriptless native transient classes that were loaded in the new packages
		// this must be done *BEFORE* EndLoad() is called - EndLoad connects all object references
		// with the actual UObjects, but transient native classes are assumed to have been already
		// created.  Thus, EndLoad() does not attempt to create these classes, so we need to create
		// them manually.
		DuplicateNativeClasses(PackageRenameMap);

		EndLoad();
		unguard;
	}
};
IMPLEMENT_CLASS(UMultiPackageLoadCommandlet);

/*-----------------------------------------------------------------------------
	UPatchScriptCommandlet.
-----------------------------------------------------------------------------*/

/**
 * Compares two versions of script packages, and exports bytecode for modified functions/states
 * in a C++ friendly format.
 */
class UPatchScriptCommandlet : public UMultiPackageLoadCommandlet
{
	DECLARE_CLASS(UPatchScriptCommandlet,UMultiPackageLoadCommandlet,CLASS_Transient,Editor);

	/**
	 * Parses the command-line for locations of packages that should be loaded
	 * 
	 * @param	Parms				text that was included in the command-line
	 * @param	PackageName			[out] the package name that should be loaded
	 * @param	CurrentDirectory	[out] the location of the current version of the packages
	 * @param	OriginalDirectory	[out] the location of the original version of the packages
	 */
	void GetOperationalParameters( const TCHAR* Parms, FString& PackageName, FString& CurrentDirectory, FString& OriginalDirectory ) const
	{
		guard(UPatchScriptCommandlet::GetOperationalParameters);

		if ( !ParseToken(Parms, PackageName, 0) )
		{
			appErrorf(TEXT("Package name not specified!"));
		}
		if( !ParseToken(Parms,CurrentDirectory,0) || CurrentDirectory.Left(1) == TEXT("-") )
		{
			CurrentDirectory = TEXT("../System");
		}
		if( !ParseToken(Parms,OriginalDirectory,0) || OriginalDirectory.Left(1) == TEXT("-") )
		{
			OriginalDirectory = TEXT("OriginalScript");
		}

		// preserve any path information
		FString RelativePath;
		INT pos = FindPathSeparator(CurrentDirectory);
		if ( pos != INDEX_NONE )
			RelativePath = CurrentDirectory.Left(pos);

		// make sure the directory containing the current script files exists
		TArray<FString> DirectoryPath = GFileManager->FindFiles(*CurrentDirectory, 0, 1);
		if ( DirectoryPath.Num() == 0 )
		{
			appErrorf(TEXT("You must specify a location for the current .u files!"));
		}
		CurrentDirectory = RelativePath.Len() ? RelativePath * DirectoryPath(0) : DirectoryPath(0);

		RelativePath.Empty();
		pos = FindPathSeparator(OriginalDirectory);
		if ( pos != INDEX_NONE )
		{
			RelativePath = OriginalDirectory.Left(pos);
		}
		// make sure the directory containing the original script files exists
		DirectoryPath = GFileManager->FindFiles(*OriginalDirectory, 0, 1);
		if ( DirectoryPath.Num() == 0 )
		{
			appErrorf(TEXT("You must specify a location for the original .u files or place the package in the System/OriginalScript directory!"));
		}
		OriginalDirectory = RelativePath.Len() ? RelativePath * DirectoryPath(0) : DirectoryPath(0);

		unguard;
	}

	INT Main( const TCHAR* Parms )
	{
		guard(UPatchScriptCommandlet::Main);

		InitializeCommandlet();

		FString PackageName, CurrentLocation, OriginalLocation;
		GetOperationalParameters(Parms, PackageName, CurrentLocation, OriginalLocation);

		// load all packages
		TArray<FPackageData>	LatestScript, OriginalScript;

		warnf(TEXT("Loading original packages..."));
		LoadPackages(OriginalScript, OriginalLocation * PackageName);

		warnf(TEXT("Loading current packages..."));
		LoadPackages(LatestScript, CurrentLocation * PackageName, TEXT("Original"));

		FScriptPatchExporter Writer;
		for ( INT i = 0; i < LatestScript.Num(); i++ )
		{
            FPackageComparison Comparison(LatestScript(i),OriginalScript(i));
			Comparison.ComparePackages(Writer);
			if ( !ParseParam(Parms,TEXT("nosummary")) )
			{
				warnf(TEXT("%s================================================================%s"), LINE_TERMINATOR, LINE_TERMINATOR);
				Comparison.DisplaySummaries();
			}
		}
		Writer.SaveToFile();

		return 0;
		unguard;
	}
};
IMPLEMENT_CLASS(UPatchScriptCommandlet);


enum EPackageInfoFlags
{
	PKGINFO_None		=0x0,
	PKGINFO_Names		=0x1,
	PKGINFO_Imports		=0x2,
	PKGINFO_Exports		=0x4,
	PKGINFO_Compact		=0x8,

	PKGINFO_All			= PKGINFO_Names|PKGINFO_Imports|PKGINFO_Exports,
};

/**
 * Displays information about an unreal package.
 */
class UPkgInfoCommandlet : public UMultiPackageLoadCommandlet
{
	DECLARE_CLASS(UPkgInfoCommandlet,UMultiPackageLoadCommandlet,CLASS_Transient,Editor);

	/**
	 * Displays the information for the packages specified
	 * 
	 * @param	Packages	list of packages to display information for
	 * @param	InfoFlags	bitmask: indicates which type of information to display
	 */
	void DisplayPackageInformation( TArray<FPackageData>& Packages, DWORD InfoFlags )
	{
		guard(UPkgInfoCommandlet::DisplayPackageInformation);

		for ( INT PackageIndex = 0; PackageIndex < Packages.Num(); PackageIndex++ )
		{
			FPackageData& Linker = Packages(PackageIndex);
			if ( PackageIndex > 0 )
				warnf(TEXT(""));

			// Display information about the package.
			FName LinkerName = FName(*Linker.PackageName);

			// Display summary info.
			GWarn->Log( TEXT("********************************************") );
			GWarn->Logf( TEXT("Package '%s' Summary (%s)"), *LinkerName, *Linker.FileName );
			GWarn->Log( TEXT("--------------------------------------------") );

			GWarn->Logf( TEXT("\tPackageFlags: %x"), Linker.Summary.PackageFlags );
			GWarn->Logf( TEXT("\t   NameCount: %d"), Linker.Summary.NameCount );
			GWarn->Logf( TEXT("\t  NameOffset: %d"), Linker.Summary.NameOffset );
			GWarn->Logf( TEXT("\t ImportCount: %d"), Linker.Summary.ImportCount );
			GWarn->Logf( TEXT("\tImportOffset: %d"), Linker.Summary.ImportOffset );
			GWarn->Logf( TEXT("\t ExportCount: %d"), Linker.Summary.ExportCount );
			GWarn->Logf( TEXT("\tExportOffset: %d"), Linker.Summary.ExportOffset );

			FString szGUID = Linker.Summary.Guid.String();
			GWarn->Logf( TEXT("\t        Guid: %s"), *szGUID );
			GWarn->Log ( TEXT("\t Generations:"));
			for( INT i = 0; i < Linker.Summary.Generations.Num(); ++i )
			{
				const FGenerationInfo& generationInfo = Linker.Summary.Generations( i );
				GWarn->Logf( TEXT("\t\t%d) ExportCount=%d, NameCount=%d"), i, generationInfo.ExportCount, generationInfo.NameCount );
			}

			if( (InfoFlags&PKGINFO_Names) != 0 )
			{
				GWarn->Log( TEXT("--------------------------------------------") );
				GWarn->Log ( TEXT("Name Map"));
				GWarn->Log ( TEXT("========"));
				for( INT i = 0; i < Linker.Names.Num(); ++i )
				{
					FName& name = Linker.Names( i );
					GWarn->Logf( TEXT("\t%d: Name '%s' Index %d"), i, *name, name.GetIndex() );
				}
			}

			// if we _only_ want name info, skip this part completely
			if ( InfoFlags != PKGINFO_Names )
			{
				if( (InfoFlags&PKGINFO_Imports) != 0 )
				{
					GWarn->Log( TEXT("--------------------------------------------") );
					GWarn->Log ( TEXT("Import Map"));
					GWarn->Log ( TEXT("=========="));
				}

				TArray<FName> DependentPackages;
				for( INT i = 0; i < Linker.Imports.Num(); ++i )
				{
					FObjectImport& import = Linker.Imports( i );

					FName PackageName = import.SourceLinker ? import.SourceLinker->LinkerRoot->GetFName() : NAME_None;

					FName OuterName = NAME_None;
					if ( import.PackageIndex > 0 )
					{
						FObjectExport& OuterExport = Linker.Exports(import.PackageIndex-1);
						OuterName = OuterExport.ObjectName;
					}
					else if ( import.PackageIndex < 0 )
					{
						FObjectImport& OuterImport = Linker.Imports(-import.PackageIndex-1);
						OuterName = OuterImport.ObjectName;
					}

					if ( (InfoFlags&PKGINFO_Imports) != 0 )
					{
						GWarn->Log ( TEXT("\t*************************"));
						GWarn->Logf( TEXT("\tImport %d: '%s'"), i, *import.ObjectName );
						GWarn->Logf( TEXT("\t\t       Outer: '%s' (%d)"), *OuterName, import.PackageIndex);
						GWarn->Logf( TEXT("\t\t     Package: '%s' (%d)"), *PackageName);
						GWarn->Logf( TEXT("\t\t       Class: '%s'"), *(import.ClassName) );
						GWarn->Logf( TEXT("\t\tClassPackage: '%s'"), *(import.ClassPackage) );
						GWarn->Logf( TEXT("\t\t     XObject: %s"), import.XObject ? TEXT("VALID") : TEXT("NULL"));
						GWarn->Logf( TEXT("\t\t SourceIndex: %d"), import.SourceIndex );
					}
					if ( PackageName != NAME_None && PackageName != LinkerName )
						DependentPackages.AddUniqueItem(PackageName);

					if ( import.ClassPackage != NAME_None && import.ClassPackage != LinkerName )
						DependentPackages.AddUniqueItem(import.ClassPackage);
				}

				if ( DependentPackages.Num() )
				{
					GWarn->Log( TEXT("--------------------------------------------") );
					warnf(TEXT("\tPackages referenced by %s:"), *LinkerName);
					for ( INT i = 0; i < DependentPackages.Num(); i++ )
					{
						warnf(TEXT("\t\t%i) %s"), i, *DependentPackages(i));
					}
				}
			}

			if( (InfoFlags&PKGINFO_Exports) != 0 )
			{
				GWarn->Log( TEXT("--------------------------------------------") );
				GWarn->Log ( TEXT("Export Map"));
				GWarn->Log ( TEXT("=========="));

				for( INT i = 0; i < Linker.Exports.Num(); ++i )
				{
					GWarn->Log ( TEXT("\t*************************"));
					FObjectExport& export = Linker.Exports( i );
					GWarn->Logf( TEXT("\tExport %d: '%s'"), i, *export.ObjectName );

					// find the name of this object's class
					INT ClassIndex = export.ClassIndex;
					FName ClassName = ClassIndex>0 
						? Linker.Exports(ClassIndex-1).ObjectName
						: ClassIndex<0 
						? Linker.Imports(-ClassIndex-1).ObjectName
						: FName(NAME_Class);

					// find the name of this object's parent...for UClasses, this will be the parent class
					// for UFunctions, this will be the SuperFunction, if it exists, etc.
					FName ParentName = NAME_None;
					if ( export.SuperIndex > 0 )
					{
						FObjectExport& ParentExport = Linker.Exports(export.SuperIndex-1);
						ParentName = ParentExport.ObjectName;
					}
					else if ( export.SuperIndex < 0 )
					{
						FObjectImport& ParentImport = Linker.Imports(-export.SuperIndex-1);
						ParentName = ParentImport.ObjectName;
					}

					// find the name of this object's Outer.  For UClasses, this will generally be the
					// top-level package itself.  For properties, a UClass, etc.
					FName OuterName = NAME_None;
					if ( export.PackageIndex > 0 )
					{
						FObjectExport& OuterExport = Linker.Exports(export.PackageIndex-1);
						OuterName = OuterExport.ObjectName;
					}
					else if ( export.PackageIndex < 0 )
					{
						FObjectImport& OuterImport = Linker.Imports(-export.PackageIndex-1);
						OuterName = OuterImport.ObjectName;
					}

					GWarn->Logf( TEXT("\t\t       Class: '%s' (%i)"), *ClassName, ClassIndex );
					GWarn->Logf( TEXT("\t\t      Parent: '%s' (%d)"), *ParentName, export.SuperIndex );
					GWarn->Logf( TEXT("\t\t       Outer: '%s' (%d)"), *OuterName, export.PackageIndex );
					GWarn->Logf( TEXT("\t\t ObjectFlags: %x"), export.ObjectFlags );
					GWarn->Logf( TEXT("\t\t     _Object: %x"), export._Object ? TEXT("VALID") : TEXT("NULL"));
					GWarn->Logf( TEXT("\t\t  _iHashNext: %d"), export._iHashNext );
					GWarn->Logf( TEXT("\t\t        Size: %d"), export.SerialSize );
//					GWarn->Logf( TEXT("\t\t      Offset: %d"), export.SerialOffset );
				}
			}
		}

		unguard;
	}

	INT UPkgInfoCommandlet::Main( const TCHAR* Parms )
	{
		guard(UPkgInfoCommandlet::Main);

		InitializeCommandlet();

		// find out which type of info we're looking for
		DWORD InfoFlags = PKGINFO_None;
		if( ParseParam(Parms,TEXT("names")))
		{
			InfoFlags |= PKGINFO_Names;
		}
		if( ParseParam(Parms,TEXT("imports")))
		{
			InfoFlags |= PKGINFO_Imports;
		}
		if( ParseParam(Parms,TEXT("exports")))
		{
			InfoFlags |= PKGINFO_Exports;
		}
		if ( ParseParam(Parms,TEXT("simple")) )
		{
			InfoFlags |= PKGINFO_Compact;
		}
		if ( ParseParam(Parms,TEXT("all")) )
		{
			InfoFlags = PKGINFO_All;
		}

		FString	PackageWildcard;
		TArray<FPackageData> Packages;
		while( ParseToken(Parms, PackageWildcard, 0) )
		{
			LoadPackages(Packages, PackageWildcard);
		}

		DisplayPackageInformation(Packages, InfoFlags);

		return 0;

		unguard;
	}

};
IMPLEMENT_CLASS(UPkgInfoCommandlet);

