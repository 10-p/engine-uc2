

/**
 * Contains info about a UObject patch that needs to be applied to the image at runtime.
 */
struct FPatchData
{
	/** the name associated with this data */
	FString	DataName;

	/** number of bytes stored at Data */
	INT		Size;

	/** binary data - might be UObject data or bytecode */
	BYTE*	Data;

	virtual ~FPatchData() {}
};

/**
 * Simple archive for serializing UObject data from memory
 */
class FPatchReader : public FArchive
{
public:
	/** Constructors */
	FPatchReader()
	: Offset(0), Loader(NULL), FArchive()
	{
		ArIsLoading = ArIsTrans = 1;
	}
	FPatchReader( FPatchData& Patch )
	: Offset(0), Loader(NULL), FArchive()
	{
		guard(FPatchReader::FPatchReader);

		ArIsLoading = ArIsTrans = 1;

		AddPatch(Patch);

		unguard;
	}

	/**
	 * Adds a new source patch to this archive
	 * 
	 * @param	Patch	the patch to add to this archive
	 */
	void AddPatch( FPatchData& Patch )
	{
		INT Size = TotalSize();

		Bytes.Add(Patch.Size);
		appMemcpy(&Bytes(Size), Patch.Data, Patch.Size);
	}

	void SetLoader( ULinkerLoad* inLoader )
	{
		Loader = inLoader;
	}

	/** FArchive interface */
	INT TotalSize()
	{
		return Bytes.Num();
	}
	void Seek( INT InPos )
	{
		if ( InPos >= Bytes.Num() )
			debugf(TEXT(""));

		Offset = InPos;
	}
	INT Tell()
	{
		return Offset;
	}
	FArchive& operator<<( class FName& Name )
	{
		check(Loader);

		INT NameIndex;
		*this << AR_INDEX(NameIndex);

		FName Temporary = Loader->IndexToName( NameIndex );
		appMemcpy(&Name, &Temporary, sizeof(FName));

		return *this;
	}
	FArchive& operator<<(class UObject*& Obj)
	{
		check(Loader);

		INT Index;
		*this << AR_INDEX(Index);

		UObject* Temporary = Loader->IndexToObject( Index );
		appMemcpy(&Obj, &Temporary, sizeof(UObject*));

		return *this;
	}
	INT MapName( FName* Name )
	{
		if ( Loader != NULL )
			return Loader->MapName(Name);

		return FArchive::MapName(Name);
	}
	INT MapObject( UObject* Object )
	{
		if ( Loader != NULL )
			return Loader->MapObject(Object);

		return FArchive::MapObject(Object);;
	}
	void Preload( UObject* Object )
	{
		if ( Loader != NULL )
			Loader->Preload(Object);
	}
private:
	void Serialize( void* Data, INT Num )
	{
		checkSlow(Offset+Num<=Bytes.Num());
		appMemcpy( Data, &Bytes(Offset), Num );
		Offset += Num;
	}

	/** the raw data contained by this archive */
	TArray<BYTE>	Bytes;

	/** the current position of the reader */
	INT				Offset;

	/** the linker that is currently using this reader for serialization */
	ULinkerLoad*	Loader;

};

struct FLinkerPatchData
{
	FString					PackageName;
	TArray<FName>			Names;
	TArray<FObjectExport>	Exports;
	TArray<FObjectImport>	Imports;

	TArray<FPatchData*>		Objects;
	TArray<FPatchData*>		ScriptPatches;

	INT						ExportCount, NameCount;

	static FObjectExport BuildExport( FName ObjectName, DWORD ObjectFlags, SWORD ClassIndex, SWORD SuperIndex, SWORD PackageIndex )
	{
		FObjectExport Export;
		Export.ClassIndex = ClassIndex;
		Export.SuperIndex = SuperIndex;
		Export.PackageIndex = PackageIndex;
		Export.ObjectName = ObjectName;
		Export.ObjectFlags = ObjectFlags;

		Export._iHashNext = 0;
		Export.SerialOffset = 0;
		Export.SerialSize = 0;
		Export._Object = NULL;
		return Export;
	}
	static FObjectImport BuildImport( FName ObjectName, FName ClassName, FName ClassPackage, SWORD PackageIndex )
	{
		FObjectImport Import;
		Import.ObjectName = ObjectName;
		Import.ClassName = ClassName;
		Import.ClassPackage = ClassPackage;
		Import.PackageIndex = PackageIndex;

		Import.XObject = NULL;
		Import.SourceIndex = INDEX_NONE;
		Import.SourceLinker = NULL;
		return Import;
	}

	UBOOL Apply( ULinkerLoad* Loader, FPatchReader& Ar, UBOOL bAllowCreate );

	void PreloadExports( ULinkerLoad* Loader );
	UBOOL VerifyExport( FObjectExport& Export, ULinkerLoad* Loader, INT Slack );
	UBOOL ValidateLinkerIndex( ULinkerLoad* Linker, INT Index, INT Slack );

	~FLinkerPatchData()
	{
		for ( INT i = 0; i < Objects.Num(); i++ )
			delete Objects(i);

		for ( INT i = 0; i < ScriptPatches.Num(); i++ )
			delete ScriptPatches(i);

		ScriptPatches.Empty();
		Objects.Empty();
	}
};

/**
 * Class for replacing in-memory bytecode.
 */
struct FScriptPatcher
{
public:

	/**
	 * Constructor
	 */
	FScriptPatcher();
	~FScriptPatcher();

	/**
	 * Applies the patched bytecode to the appropriate UStructs
	 */
	void ApplyPatches( UBOOL bCreateExports = FALSE );

private:
	TArray<FLinkerPatchData*>	PackageUpdates;
};


