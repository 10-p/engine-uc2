/*=============================================================================
//	UDumpIntCommandlet.cpp: Imports/Merges/Exports INTs for specified packages.
//
//	Copyright 2003 Epic Games. All Rights Reserved.
=============================================================================*/

#include "EditorPrivate.h"
#include "../../Core/Inc/FConfigCacheIni.h"

/*-----------------------------------------------------------------------------
 Internal helper functions.
-----------------------------------------------------------------------------*/

static bool bPropertyCountOnly=false;
static INT GPropertyCount=0, GLineCount=0;

static INT Compare( FString& A, FString& B )
{
	return appStricmp( *A, *B );
}

static INT Compare (const UObject *p1, const UObject *p2)
{
	return appStricmp(p1->GetPathName(), p2->GetPathName());
}

static void IntExportStruct( UClass* Class, UClass* SuperClass, UClass* OuterClass, UStruct* Struct, const TCHAR *IntName, const TCHAR *SectionName, const TCHAR *KeyPrefix, BYTE* DataBase, INT DataOffset, bool AtRoot = false );
static void IntExportDynamicArray( UClass* Class, UClass* SuperClass, UClass* OuterClass, UArrayProperty * Prop, const TCHAR *IntName, const TCHAR *SectionName, const TCHAR *KeyPrefix, BYTE* DataBase, INT DataOffset, bool AtRoot = false );

#define PROPTEXTSIZE 65536

//scion ===========================================================
//	SCION Function: IntExportProp
//	Author: superville
//
//	Description: Get key value from given property, determine if
//		the property is different from parent class property (if any)
//		If it's not different, build the unique key name and write
//		it out to the given .int file.
//
//	Input:	UClass* Class				- Class being exported
//			UClass* SuperClass			- Parent class
//			UClass* OuterClass			- Owner class (??)
//			UProperty* Prop				- Property structure to export
//			const TCHAR* IntName		- Name of .int file
//			const TCHAR* SectionName	- Name of section to export to (or create if doesn't exist)
//			const TCHAR* KeyPrefix		- Name of variable to export
//			BYTE* DataBase				- Starting address of data for this property
//			INT DataOffset				- Offset distance from start address of entire property struct
//
//	Notes:
//	Last Modified: 01/15/04 (superville)
// ================================================================
static void IntExportProp
( 
	UClass*			Class, 
	UClass*			SuperClass, 
	UClass*			OuterClass, 
	UProperty*		Prop, 
	const TCHAR*	IntName, 
	const TCHAR*	SectionName, 
	const TCHAR*	KeyPrefix, 
	BYTE*			DataBase, 
	INT				DataOffset 
)
{
	// Try casting property to a struct
	UStructProperty* StructProperty = Cast<UStructProperty>( Prop );

	// If property is a struct
	if( StructProperty )
	{
		// Export it as such and exit
		IntExportStruct( Class, SuperClass, OuterClass, StructProperty->Struct, IntName, SectionName, KeyPrefix, DataBase, DataOffset );
		return;
	}

	// Try casting property to a dynamic array
	UArrayProperty* ArrayProperty = Cast<UArrayProperty>( Prop );

	// If property is a dynamic array
	if( ArrayProperty )
	{
		// Export it as such and exit
		IntExportDynamicArray( Class, SuperClass, OuterClass, ArrayProperty, IntName, SectionName, KeyPrefix, DataBase, DataOffset );
		return;
	}

	TArray<TCHAR> RealValueTemp(PROPTEXTSIZE);
	TCHAR* RealValue = &RealValueTemp(0);

	BYTE* DefaultData = NULL;
	if ( SuperClass && SuperClass->IsChildOf(OuterClass) )
		DefaultData = (BYTE*)&SuperClass->Defaults(0);

	if ( DefaultData && DefaultData != DataBase )
	{
		if ( Prop->Identical(DataBase + DataOffset, DefaultData + DataOffset) )
			return;
	}

	// Ask the property to format itself into a string assigned into the
	// RealValue out string with delimiters included
	// ExportTextItem declaration:
	// ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const
	Prop->ExportTextItem( RealValue, DataBase + DataOffset, DefaultData ? DefaultData + DataOffset : NULL, PPF_Delimited|PPF_LocalizedOnly );

	// Make sure length of returned string does not exceed max property size
	INT RealLength = appStrlen( RealValue );
	check( RealLength < PROPTEXTSIZE );

	// If property value is empty (zero length or empty string) - exit
	if( ( RealLength == 0 ) || !appStrcmp( RealValue, TEXT("\"\"") ) )
		return;

	if ( !bPropertyCountOnly )
	{
		// Build the full key as <SectionName>.<KeyName>
		// ie GameInfo.DefaultPlayerName
//superville: No longer using section name as prefix
//		FString FullKey = FString(SectionName) + FString(TEXT(".")) + FString(KeyPrefix);
		FString FullKey = FString(KeyPrefix);

		// The string is different, write it out to the file
		// File output should be:
		// [SectionName]
		// <SectionName>.<KeyName>=<RealValue>
		GConfig->SetString( SectionName, *FullKey, RealValue, IntName );
	}

	else GLineCount++;

	// Update exported property count
	GPropertyCount++;
}

//scion ===========================================================
//	SCION Function: IntExportDynamicArray
//	Author: superville
//
//	Description: Export a dynamic array
//
//	Input:
//	Last Modified: 02/04/04 (superville)
// ================================================================
static void IntExportDynamicArray
( 
	UClass* Class,
	UClass* SuperClass, 
	UClass* OuterClass, 
	UArrayProperty *Prop, 
	const TCHAR *IntName, 
	const TCHAR *SectionName, 
	const TCHAR *KeyPrefix, 
	BYTE* DataBase, 
	INT DataOffset, 
	bool AtRoot 
)
{
	BYTE* DefaultDataBase = NULL;
	if ( SuperClass && DataOffset < SuperClass->Defaults.Num() )
		DefaultDataBase = ((BYTE*)&SuperClass->Defaults(0)) + DataOffset;

	// Build the full key as <SectionName>.<KeyName>
	// ie GameInfo.DefaultPlayerName
//superville: No longer using section name as prefix
//	FString FullKey = FString(SectionName) + FString(TEXT(".")) + FString(KeyPrefix);
	FString FullKey = FString(KeyPrefix);

	BYTE* DefaultData = NULL;
	if ( SuperClass && SuperClass->IsChildOf(OuterClass) )
		DefaultData = (BYTE*)&SuperClass->Defaults(0);

	if ( DefaultData && DefaultData != DataBase )
	{
		if ( Prop->Identical(DataBase + DataOffset, DefaultData + DataOffset) )
			return;
	}

	// Get a mapped section of the .int file (don't force it yet)	
	TMultiMap<FName,FString>* Sec = GConfig->GetSectionPrivate( SectionName, 0, 0, IntName );

	// Get array of properties (this is the start of the array property)
	FArray* Ptr  = (FArray*)(DataBase + DataOffset);
//	FArray* DefaultPtr = (FArray*)DefaultDataBase;

	// Get the size of each element
	INT     Size = Prop->Inner->ElementSize;

	// For each item in the array
	for( INT i = 0; i < Ptr->Num(); i++ )
	{
		TCHAR Buffer[1024]=TEXT("");

		// Get the address of the data we are exporting
		// Manually walk across the array by index * size
		BYTE* Dest = (BYTE*)Ptr->GetData() + i * Size;

		// if the entire array wasn't identical, we must export the entire thing to the child class's section as well,
		// or the localized property value will be overwritten when the child class calls SerializeTaggedProperties().
		//BYTE* Default = DefaultPtr && i < DefaultPtr->Num() ? (BYTE*)DefaultPtr->GetData() + i * Size : NULL;

		// Export the property into buffer
		Prop->Inner->ExportTextItem( Buffer, Dest, NULL, PPF_LocalizedOnly );

		// Add array index number to end of key
		FString IndexedKey = FString(KeyPrefix) + FString(TEXT("[")) + FString(appItoa(i)) + FString(TEXT("]"));

		if ( appStrlen(Buffer) > 0 )
		{
			if ( !bPropertyCountOnly )
			{
				// Write the value to the section in the .int file
				// since we have something to write, *now* we force it
				if ( Sec == NULL )
					Sec = GConfig->GetSectionPrivate( SectionName, 1, 0, IntName );
				
				check(Sec);
				Sec->Add( FName(*IndexedKey), Buffer );
			}
			else GLineCount++;
		}
	}

	GPropertyCount++;

}

static void IntExportStruct( UClass* Class, UClass* SuperClass, UClass* OuterClass, UStruct* Struct, const TCHAR *IntName, const TCHAR *SectionName, const TCHAR *KeyPrefix, BYTE* DataBase, INT DataOffset, bool AtRoot )
{
	for( TFieldIterator<UProperty,CLASS_IsAUProperty> It( Struct ); It; ++It )
	{
		UProperty* Prop = *It;

		/* rjp struct localization */
		// Make sure we should localize it
		if ( !Prop->IsLocalized() )
			continue;

		for( INT i = 0; i < Prop->ArrayDim; i++ )
		{
			FString NewPrefix;

			if( KeyPrefix )
				NewPrefix = FString::Printf( TEXT("%s."), KeyPrefix );

			if( Prop->ArrayDim > 1 )
				NewPrefix += FString::Printf( TEXT("%s[%d]"), Prop->GetName(), i );
			else
				NewPrefix += Prop->GetName();

			INT NewOffset = DataOffset + (Prop->Offset) + (i * Prop->ElementSize );

			IntExportProp( Class, SuperClass, AtRoot ? CastChecked<UClass>(Prop->GetOuter()) : OuterClass, Prop, IntName, SectionName, *NewPrefix, DataBase, NewOffset );
		}
	}
}

static UBOOL IntExport (UObject *Package, const TCHAR *IntName, UBOOL ExportFresh, UBOOL ExportInstances, UBOOL bAutoCheckout, UBOOL bQuiet)
{
	TArray<UObject *> Objects;
	INT objectNumber;

	// export localized text to temporary file, then compare to original afterwards
	// *then* only checkout file if it's different
	TCHAR* TmpName = new TCHAR[appStrlen(IntName) + 6];
	appStrcpy(TmpName,IntName);
	appStrncat(TmpName,TEXT(".tmp"),appStrlen(IntName)+6);

	GFileManager->Delete( TmpName, 0, 1);

	FConfigCacheIni* Config = (FConfigCacheIni*)GConfig;

	// Save any sections that aren't normally exported
	FString PersistantData = TEXT("");
	TArray<FString> StandardSections;
	new(StandardSections) FString(TEXT("DecoText"));
	new(StandardSections) FString(TEXT("Errors"));
	new(StandardSections) FString(TEXT("General"));
	new(StandardSections) FString(TEXT("LevelInfo")); //mvc to keep LevelInfo.Description working
	new(StandardSections) FString(TEXT("KeyNames"));
	new(StandardSections) FString(TEXT("Language"));
	new(StandardSections) FString(TEXT("Progress"));
	new(StandardSections) FString(TEXT("Public"));
	new(StandardSections) FString(TEXT("Query"));
	new(StandardSections) FString(TEXT("TcpNetDriver"));
	new(StandardSections) FString(TEXT("UpgradeDrivers"));
	new(StandardSections) FString(TEXT("UdpBeacon"));
	new(StandardSections) FString(TEXT("UIX"));

	// Build a list of objects that should be exported
	// Ignore any classes which are localized classes
	for( FObjectIterator It; It; ++It )
	{
	    UObject *Obj = *It;

		// determine whether this object should be skipped
 		UClass* Cls = Cast<UClass>(*It);
		if ( Cls != NULL )
		{
			// if the object is a class object, but not from the current package, skip it
			if ( !Cls->IsIn(Package) )
				continue;

			// if this is a commandlet, preserve its section, since it won't be
			// re-exported if it's a native-only commandlet
			if ( Cls->IsChildOf(UCommandlet::StaticClass()) )
				new(StandardSections) FString( Obj->GetName() );
		}
		else 
		{
			// if this isn't a subobject from the current package
			if ( !Obj->IsIn(Package) &&
				// and this isn't a PerObjectConfig object, skip it
				!(Obj->GetClass()->ClassFlags & CLASS_PerObjectConfig) )
				continue;
		}

		DWORD ObjFlags = Obj->GetFlags();
		if ( (ObjFlags & RF_Transient) || (ObjFlags & RF_NotForClient) || (ObjFlags & RF_NotForServer) || (ObjFlags & RF_Destroyed) )
			continue;

		if ( Cls && Cls->ClassFlags & CLASS_Localized )
			Objects.AddItem(Obj);
		else if ( Obj->GetClass()->ClassFlags & CLASS_Localized )
            Objects.AddItem (Obj);
	}

    if( Objects.Num() )
		Sort (&Objects(0), Objects.Num());

	if ( !bPropertyCountOnly )
	{
		// Determine whether the old file contained any sections we want to save
		const TCHAR* nl = TEXT("\r\n");
		for (INT i = 0; i < StandardSections.Num(); i++)
		{
			TMultiMap<FName,FString>* Buffer = GConfig->GetSectionPrivate( *StandardSections(i), 0, 1, IntName );
			if (Buffer)
			{
				if (PersistantData != TEXT(""))
					PersistantData += nl;

				PersistantData += FString::Printf(TEXT("[%s]%s"),*StandardSections(i), nl);
				for (TMultiMap<FName,FString>::TIterator It(*Buffer); It; ++It)
					PersistantData += (FString::Printf(TEXT("%s=%s%s"),*It.Key(),*It.Value(), nl));
			}
		}

		//  - save everything to temp file for now
		if ( PersistantData.Len() )
			appSaveStringToFile(PersistantData,TmpName,GFileManager);
	
		GPropertyCount = 0;
	}

	UObject* obj = NULL;
	UClass* cls = NULL;

	for (objectNumber = 0; objectNumber < Objects.Num(); objectNumber++)
	{
		obj = Objects(objectNumber);
		if ( obj == NULL )
			continue;

		cls = Cast<UClass>(obj);
		if ( cls )
		{
			if ( !bQuiet && !bPropertyCountOnly )
				warnf(TEXT("Exporting class %s"), cls->GetName() );
			IntExportStruct( cls, cls->GetSuperClass(), cls, cls, TmpName, cls->GetName(), NULL, &cls->Defaults(0), 0, true );
		}
		else
		{
			cls = obj->GetClass();
			UClass* SubObjectOuter = Cast<UClass>(obj->GetOuter());
			if( SubObjectOuter )	// if it's a subobject, export as [OuterClass] SubObjectName.Key=Value
			{
				// Find the Class property which references this subobject
				if( obj->GetFlags()&RF_PerObjectLocalized )
					IntExportStruct( NULL, cls, NULL, cls, TmpName, obj->GetOuter()->GetName(), obj->GetName(), (BYTE*)obj, 0, true );
			}
			else
			{
				// ExportInstances is true when the package we're exporting is a map file
				if( ExportInstances || (cls->ClassFlags & CLASS_PerObjectConfig) )
				{
					IntExportStruct( cls, cls, NULL, cls, TmpName, obj->GetName(), NULL, (BYTE*)obj, 0, true );
				}
			}
		}
	}

	if ( !bPropertyCountOnly )
	{
		// All localized text has been exported to a temporary file.  Compare the new version to the old version
		// If they're different, and the file is read-only, attempt to checkout the file
		FConfigFile *NewFile = Config->Find(TmpName,0), *CurrentFile = Config->Find(IntName,NewFile != NULL);
		if ( NewFile )
		{
			if ( (*CurrentFile) != (*NewFile) )
			{
				INT Code;
				*CurrentFile = *NewFile;

				// Be sure the set the bDirty flag so that the file will be saved
				CurrentFile->Dirty = 1;

				if ( GFileManager->IsReadOnly(IntName) && bAutoCheckout )
				{
					void* Handle = appCreateProc( TEXT("p4"), *FString::Printf(TEXT("edit %s"), IntName) );
					while( !appGetProcReturnCode( Handle, &Code ) )
						appSleep(1);
				}

				// If the file is still read-only, it isn't currently in source control, or
				// auto-checkout wasn't specified in the commandline
				if ( GFileManager->IsReadOnly(IntName) )
					warnf(NAME_Warning, TEXT("Failed to export %d properties for %s: File is read-only"), GPropertyCount, IntName);

				else
				{
					GConfig->Flush( 0, IntName );
					warnf( NAME_Log, TEXT("Exported %d properties."), GPropertyCount );
				}
			}
			Config->UnloadFile(TmpName);
		}

		// delete the temporary file we were using
		if ( GFileManager->FileSize(TmpName) >= 0 )
			GFileManager->Delete(TmpName);
	}
	delete [] TmpName;
	return 1;
}

static void IntGetNameFromPackageName (const FString &PackageName, FString &IntName)
{
	INT i;

	IntName = PackageName;

	i = IntName.InStr (TEXT ("."), 1);

	if (i >= 0)
		IntName = IntName.Left (i);

	IntName += TEXT (".int");

	i = IntName.InStr (TEXT ("/"), 1);

	if (i >= 0)
		IntName = IntName.Right (IntName.Len () - i - 1);

	i = IntName.InStr (TEXT ("\\"), 1);

	if (i >= 0)
		IntName = IntName.Right (IntName.Len () - i - 1);

	//scion superville: Create file in default localization folder
	IntName = FString(appBaseDir()) + FString(UObject::GetDefaultLocalizationFolder()) + IntName;
}

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

/*-----------------------------------------------------------------------------
	UDumpIntCommandlet.
-----------------------------------------------------------------------------*/

/*
	FLocalizationObject
		Contains information about PerObjectConfig objects that exist in this package.  Since these
		types of objects are localized on a per-object basis, we'll need to create these objects in
		order for the to be exported to the .int file.
*/

struct FLocalizationObject
{
	FString			ClassName;
	TArray<FString>	ObjectNames;

	FLocalizationObject( const FString& SectionName, FConfigSection& Section )
	{
		ClassName = SectionName;

		// Get the list of object names we need to create
		Section.MultiFind(TEXT("Names"), ObjectNames);

		// check if any of the names were pointers to another file
		for ( INT i = ObjectNames.Num() - 1; i >= 0; i-- )
		{
			FString& CurName = ObjectNames(i);
			// would be something like file:blah.ini
			if ( CurName.Left(5) == TEXT("file:") )
			{
				// strip off the 'file:' part
				FString FileName = CurName.Mid(5);
				ObjectNames.Remove(i);

				// find the file specified
				FConfigFile* File = ((FConfigCacheIni*)GConfig)->Find(*FileName,0);
				if ( File )
				{
					// add all the section names found in this file to our list
					// of objectnames
					for ( FConfigFile::TIterator It(*File); It; ++It )
						new(ObjectNames) FString(It.Key());
				}
			}
		}
	}
};

class UDumpIntCommandlet : public UCommandlet
{
	DECLARE_CLASS(UDumpIntCommandlet,UCommandlet,CLASS_Transient,Editor);

	void StaticConstructor()
	{
		guard(UDumpIntCommandlet::StaticConstructor);

		LogToStdout     = 0;
		IsClient        = 1;
		IsEditor        = 1;
		IsServer        = 1;
		LazyLoad        = 1;
		ShowErrorCount  = 1;

		unguard;
	}

	INT Main( const TCHAR *Parms )
	{
		guard(UDumpIntCommandlet::Main);

    	FString PackageWildcard;
        INT Count = 0;

		UClass* EditorEngineClass = UObject::StaticLoadClass( UEditorEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.EditorEngine"), NULL, LOAD_NoFail | LOAD_DisallowFiles, NULL );
		GEditor  = ConstructObject<UEditorEngine>( EditorEngineClass );
		GEditor->UseSound = 0;
        GEditor->InitEditor();
		GIsRequestingExit = 1; // Causes ctrl-c to immediately exit.

		TArray<FString> Files;
		UBOOL bAutoCheckout(0), bQuiet(1);

		// Parse all tokens in the command line stream
		// If the token is a parameter, set the appropriate flag;  if the token is a package wildcard,
		// get a list of file matching the wildcard from the file manager, and add them our list of files to export
		while( ParseToken(Parms, PackageWildcard, 0) )
        {
			if ( PackageWildcard == TEXT("-auto") )
			{
				bAutoCheckout = 1;
				continue;
			}
			
			if ( PackageWildcard == TEXT("-noisy") || PackageWildcard == TEXT("-n") )
			{
				bQuiet = 0;
				continue;
			}

			if ( PackageWildcard == TEXT("-count") || PackageWildcard == TEXT("-c") )
			{
				bPropertyCountOnly = true;
				continue;
			}

            TArray<FString> FilesInPath;
			FString PathPrefix(GetDirName(PackageWildcard));
			UBOOL bFound(0);

			// If a path prefix was specified, then proceed normally
			if ( PathPrefix != TEXT("") )
			{
				FilesInPath = GFileManager->FindFiles(*PackageWildcard,1,0);
				for ( INT i = 0; i < FilesInPath.Num(); i++ )
					new(Files) FString(PathPrefix+FilesInPath(i));

				bFound = bFound || FilesInPath.Num() > 0;
			}

			// Otherwise, search the paths array for the specified package type
			// This allows specifying package names for export without the need to include the full path
			else
			{
				for( INT DoCD=0; DoCD<1+(GCdPath[0]!=0); DoCD++ )
				{
					for( INT i=0; i<GSys->Paths.Num(); i++ )
					{
						TCHAR Test[256] = TEXT("");
						if (DoCD)
						{
							appStrcat( Test, GCdPath );
							appStrcat( Test, TEXT("System")PATH_SEPARATOR );
						}

						appStrcat( Test, *GSys->Paths(i) );
						if ( PackageWildcard != TEXT("*.*") )
						{
							*appStrstr( Test, TEXT("*.") ) = 0;

							PathPrefix = Test;
							appStrcat( Test, *PackageWildcard );
						}

						FilesInPath = GFileManager->FindFiles( Test, 1, 0 );
						bFound = bFound || FilesInPath.Num() > 0;

						for ( INT j = 0; j < FilesInPath.Num(); j++ )
							new(Files) FString(PathPrefix + FilesInPath(j));
					}
				}
			}

			if ( !bFound )
			{
                warnf( NAME_Error, TEXT("No packages found matching %s!"), *PackageWildcard );
                continue;
            }
		}

		if ( Files.Num() > 0 )
            Sort( &Files(0), Files.Num() );

		TArray<FLocalizationObject> Sections;
		FConfigCacheIni* Config = (FConfigCacheIni*)GConfig;

		// see if we have any PerObject config objects to check for
		// this is stored in the Localization.ini file.
		FConfigFile* File = Config->Find( TEXT("Localization"), 0 );
		if( File )
		{
			// each section in the Localization.ini file represents a different class.
			// under each section is a list of names for PerObjectConfig objects
			// of that class.
			for ( FConfigFile::TIterator It(*File); It; ++It )
				new(Sections) FLocalizationObject(It.Key(), It.Value());
		}

		for ( INT i = 0; i < Files.Num(); i++ )
		{
			FString IntName;
			warnf(NAME_Log,TEXT("Loading %s..."), *Files(i));

			UPackage* Package = Cast<UPackage>(LoadPackage(NULL, *Files(i), LOAD_NoWarn));

			// Check that the package successfully loaded here, prior to passing this package pointer into the
			// IntExport functions
			// This way, when exporting multiple package via a wildcard or batch file, a single package load failure
			// doesn't cause the entire batch to fail.
			if ( !Package )
			{
				warnf(NAME_Error, TEXT("    Unable to load package '%s'"), *Files(i));
				continue;
			}

			UBOOL ScriptPackage = Files(i).Right(2) == TEXT(".u");
			if ( ScriptPackage )
			{
				// check our list of PerObjectConfig objects to see if we need to load any of them
				for ( INT i = Sections.Num() - 1; i >= 0; i-- )
				{
					FLocalizationObject& CurSection = Sections(i);

					// extract the class's package name
					FString ClassPackage = CurSection.ClassName.Left(CurSection.ClassName.InStr(TEXT(".")));

					// if the section's class is contained in the package we're about to export
					if ( ClassPackage == Package->GetName() )
					{
						// and we actually have names in this section
						if ( CurSection.ObjectNames.Num() )
						{
							// load the class, and create objects for each of the names specified in the class's section
							// so that they will be found during iteration and exported.
							UClass* Cls = LoadClass<UObject>(NULL, *CurSection.ClassName, NULL, 0, NULL);
							if ( Cls == NULL )
								continue;

							for ( INT ObjIndex = CurSection.ObjectNames.Num() - 1; ObjIndex >= 0; ObjIndex-- )
							{
								// note: this might be incorrect for PerObjectConfig objects where the class is declared 'within' another class
								UObject* Obj =
									ConstructObject<UObject>(Cls, UObject::GetTransientPackage(), *CurSection.ObjectNames(ObjIndex), 0);

								if ( Obj != NULL )
									debugf(TEXT(" Adding %s to the object list."), Obj->GetName());
							}
						}

						Sections.Remove(i);
					}
				}
			}

            IntGetNameFromPackageName ( *Files(i), IntName );

            IntExport( Package, *IntName, true, Files(i).Right(2) != TEXT(".u"), bAutoCheckout, bQuiet );

            UObject::CollectGarbage( RF_Native );

            Count++;
        }

        if( !Count )
            GWarn->Log( NAME_Error, TEXT("Syntax: ucc DumpInt <file.ext> [file.ext...] [-auto|-a] [-noisy|-n] [-count|-c]") );

		if ( bPropertyCountOnly )
			warnf(TEXT("Exported %i properties (%i lines)."), GPropertyCount, GLineCount);

		GIsRequestingExit=1;
		return 0;

		unguard;
	}
};
IMPLEMENT_CLASS(UDumpIntCommandlet)

/*-----------------------------------------------------------------------------
	UCompareIntCommandlet.
-----------------------------------------------------------------------------*/

class FLocalizationFile
{
public:
	void GetMissingSections( TArray<FString>& Sections )
	{
		Sections += UnmatchedSections;
	}

	void GetMissingProperties( TArray<FString>& Properties )
	{
		Properties += UnmatchedProperties;
	}

	void GetIdenticalProperties( TArray<FString>& Properties )
	{
		Properties += IdenticalProperties;
	}

	void Link( FLocalizationFile* Other )
	{
		guard(FLocalizationFile::Link);

		check(Other);
		
		FConfigFile* OtherFile = Other->GetFile();
		check(OtherFile);

		for ( FConfigFile::TIterator It(*LocFile); It; ++It )
		{
			FString& SectionName = It.Key();
			FConfigSection& MySection = It.Value();

			FConfigSection* OtherSection = OtherFile->Find(SectionName);
			if ( OtherSection != NULL )
			{
				for ( TMap<FName,FString>::TIterator It(MySection); It; ++It )
				{
					FName& Propname = It.Key();
					FString& PropValue = It.Value();

					FString* OtherValue = OtherSection->Find(Propname);
					if ( OtherValue )
					{
						if ( PropValue == *OtherValue )
						{
							INT Index = IdenticalProperties.AddZeroed();
							IdenticalProperties(Index) = FString::Printf(TEXT("%s.%s"), *SectionName, *Propname);
						}
					}
					else
					{
						INT Index = UnmatchedProperties.AddZeroed();
						UnmatchedProperties(Index) = FString::Printf(TEXT("%s.%s"), *SectionName, *Propname);
					}
				}
			}
			else
			{
				INT Index = UnmatchedSections.AddZeroed();
				UnmatchedSections(Index) = FString::Printf(TEXT("%s.%s"), *LocFilename, *SectionName);
			}
		}

		unguard;
	}

	UBOOL Matches( const FLocalizationFile& Other ) const
	{
		guard(FLocalizationFile::Matches);

		const TCHAR* OtherFilename = Other.GetFilename();
		if ( OtherFilename == NULL )
			return 0;

		return !appStricmp(*LocFilename, OtherFilename);
		unguard;
	}

	const TCHAR* GetFullName() const		{ return *FullPath; }
	const TCHAR* GetDirectoryName() const	{ return *LocDirectory; }
	const TCHAR* GetFilename() const		{ return *LocFilename; }
	const TCHAR* GetExtension() const		{ return *LocExtension; }
	FConfigFile* GetFile() const			{ return LocFile; }

	static void ParsePathName( FString inFullPathName, FString& out_DirectoryPart, FString& out_FilenamePart, FString& out_ExtensionPart )
	{
		guard(FLocalizationFile::ParsePathName);

		if ( inFullPathName.Len() == 0 )
			return;

		// find the extension
		INT pos = inFullPathName.InStr(TEXT("."),1);
		if ( pos != INDEX_NONE )
		{
			out_ExtensionPart = inFullPathName.Mid(pos+1);
			inFullPathName = inFullPathName.Left(pos);
		}

		// find the filename
		pos = inFullPathName.InStr(PATH_SEPARATOR, 1);
		if ( pos != INDEX_NONE )
		{
			out_FilenamePart = inFullPathName.Mid(pos+1);
			out_DirectoryPart = inFullPathName.Left(pos);
		}
		else
		{
			out_FilenamePart = inFullPathName;
			out_DirectoryPart = TEXT("");
		}

		unguard;
	}

	FLocalizationFile( const TCHAR* inPath )
	: LocFile(NULL)
	{
		guard(FLocalizationFile::FLocalizationFile);

		FullPath = inPath;
		ParsePathName(FullPath, LocDirectory, LocFilename, LocExtension);

		LocFile = ((FConfigCacheIni*)GConfig)->Find(inPath,0);

		unguard;
	}

	// copy constructor
	FLocalizationFile( const FLocalizationFile& Other )
	{
		FullPath = Other.GetFullName();
		LocDirectory = Other.GetDirectoryName();
		LocFilename=Other.GetFilename();
		LocExtension=Other.GetExtension();
		LocFile = Other.GetFile();
	}

	~FLocalizationFile()
	{
		guard(FLocalizationFile::~FLocalizationFile);

		LocFile = NULL;

		unguard;
	}

private:

	FString FullPath;

	FString LocDirectory;
	FString LocFilename;
	FString LocExtension;


	TArray<FString> UnmatchedSections;		// sections that do not exist in the counterpart file.
	TArray<FString> UnmatchedProperties,	// properties that are missing from the corresponding section in the other file
					IdenticalProperties;	// properties that have identical values in the other file file

	FConfigFile* LocFile;
};

class FLocalizationFilePair
{
public:

	void CompareSections()
	{
		verify( HasEnglishFile() || HasForeignFile() );

		if ( HasEnglishFile() && HasForeignFile() )
		{
			EnglishFile->Link(ForeignFile);
			ForeignFile->Link(EnglishFile);			
		}
	}

	void GetMissingLocFiles( TArray<FString>& Files )
	{
		guard(FLocalizationFilePair::GetMissingLocFiles);

		if ( !HasForeignFile() )
		{
			new(Files) FString(EnglishFile->GetFilename());
		}

		unguard;
	}

	void GetObsoleteLocFiles( TArray<FString>& Files )
	{
		guard(FLocalizationFilePair::GetObsoleteLocFiles);

		if ( !HasEnglishFile() )
		{
			new(Files) FString(ForeignFile->GetFilename());
		}

		unguard;
	}

	void GetMissingSections( TArray<FString>& Sections )
	{
		guard(FLocalizationFilePair::GetMissingSections);

		if ( HasEnglishFile() && HasForeignFile() )
		{
			EnglishFile->GetMissingSections(Sections);
		}

		unguard;
	}

	void GetObsoleteSections( TArray<FString>& Sections )
	{
		guard(FLocalizationFilePair::GetObsoleteSections);

		if ( HasEnglishFile() && HasForeignFile() )
		{
			ForeignFile->GetMissingSections(Sections);
		}

		unguard;
	}

	void GetMissingProperties( TArray<FString>& Properties )
	{
		guard(FLocalizationFilePair::GetMissingProperties);

		if ( HasEnglishFile() && HasForeignFile() )
		{
			EnglishFile->GetMissingProperties(Properties);
		}

		unguard;
	}

	void GetObsoleteProperties( TArray<FString>& Properties )
	{
		guard(FLocalizationFilePair::GetObsoleteProperties);

		if ( HasEnglishFile() && HasForeignFile() )
		{
			ForeignFile->GetMissingProperties(Properties);
		}

		unguard;
	}

	void GetUntranslatedProperties( TArray<FString>& Properties )
	{
		guard(FLocalizationFilePair::GetUntranslatedProperties);

		if ( HasEnglishFile() && HasForeignFile() )
		{
			EnglishFile->GetIdenticalProperties(Properties);
		}

		unguard;
	}


	UBOOL SetEnglishFile( const TCHAR* EnglishFilename )
	{
		if ( EnglishFilename == NULL )
			return false;

		if ( EnglishFile )
		{
			delete EnglishFile;
			EnglishFile = NULL;
		}

		EnglishFile = new FLocalizationFile(EnglishFilename);
		return EnglishFile != NULL && EnglishFile->GetFile() != NULL;
	}

	UBOOL SetForeignFile( const TCHAR* ForeignFilename )
	{
		if ( ForeignFilename == NULL )
			return false;

		if ( ForeignFile )
		{
			delete ForeignFile;
			ForeignFile = NULL;
		}

		ForeignFile = new FLocalizationFile(ForeignFilename);
		return ForeignFile != NULL && ForeignFile->GetFile() != NULL;
	}

	const TCHAR* GetFilename()
	{
		return HasEnglishFile()
			? EnglishFile->GetFilename()
			: ForeignFile->GetFilename();
	}

	UBOOL HasEnglishFile() { return EnglishFile != NULL && EnglishFile->GetFile() != NULL; }
	UBOOL HasForeignFile() { return ForeignFile != NULL && ForeignFile->GetFile() != NULL; }
	UBOOL HasEnglishFile( const TCHAR* Filename ) { return HasEnglishFile() && !appStricmp(EnglishFile->GetFilename(), Filename); }
	UBOOL HasForeignFile( const TCHAR* Filename ) { return HasForeignFile() && !appStricmp(ForeignFile->GetFilename(), Filename); }

	FLocalizationFilePair()
	: EnglishFile(NULL), ForeignFile(NULL)
	{ }

	~FLocalizationFilePair()
	{
		if ( EnglishFile )
		{
			delete EnglishFile;
			EnglishFile = NULL;
		}

		if ( ForeignFile )
		{
			delete ForeignFile;
			ForeignFile = NULL;
		}
	}

	FLocalizationFile *EnglishFile, *ForeignFile;
};

class UCompareIntCommandlet : public UCommandlet
{
	DECLARE_CLASS(UCompareIntCommandlet,UCommandlet,CLASS_Transient,Editor);
	void StaticConstructor()
	{
		guard(UCompareIntCommandlet::StaticConstructor);

		LogToStdout     = 0;
		IsClient        = 1;
		IsEditor        = 1;
		IsServer        = 1;
		LazyLoad        = 1;
		ShowErrorCount  = 1;

		unguard;
	}

	INT FindEnglishIndex( const TCHAR* Filename )
	{
		guard(UCompareIntCommandlet::FindEnglishIndex);

		INT Result = INDEX_NONE;
		if ( Filename != NULL && appStrlen(Filename) > 0 )
		{
			for ( INT i = 0; i < LocPairs.Num(); i++ )
			{
				if ( LocPairs(i).HasEnglishFile(Filename) )
				{
					Result = i;
					break;
				}
			}
		}
		return Result;

		unguard;
	}

	INT FindForeignIndex( const TCHAR* Filename )
	{
		guard(UCompareIntCommandlet::FindForeignIndex);

		INT Result = INDEX_NONE;
		if ( Filename != NULL && appStrlen(Filename) > 0 )
		{
			for ( INT i = 0; i < LocPairs.Num(); i++ )
			{
				if ( LocPairs(i).HasForeignFile(Filename) )
				{
					Result = i;
					break;
				}
			}
		}

		return Result;

		unguard;
	}


	void AddEnglishFile( const TCHAR* Filename )
	{
		if ( Filename && appStrlen(Filename) > 0 )
		{
			FLocalizationFile locFile(Filename);

			// attempt to find the matching foreign file for this english file
			INT Index = FindForeignIndex(locFile.GetFilename());
			if ( Index == INDEX_NONE )
			{
				Index = LocPairs.AddZeroed();
			}

			LocPairs(Index).SetEnglishFile(Filename);
		}
	}

	void AddForeignFile( const TCHAR* Filename )
	{
		if ( Filename && appStrlen(Filename) > 0 )
		{
			FLocalizationFile locFile(Filename);

			// attempt to find the matching foreign file for this english file
			INT Index = FindEnglishIndex(locFile.GetFilename());
			if ( Index == INDEX_NONE )
			{
				Index = LocPairs.AddZeroed();
			}

			LocPairs(Index).SetForeignFile(Filename);
		}
	}


	void ReadLocFiles( const TArray<FString>& EnglishFilenames, TArray<FString>& ForeignFilenames )
	{
		guard(UCompareIntCommandlet::ReadLocFiles);
		
		for ( INT i = 0; i < EnglishFilenames.Num(); i++ )
		{
			AddEnglishFile(*EnglishFilenames(i));
		}

		for ( INT i = 0; i < ForeignFilenames.Num(); i++ )
		{
			AddForeignFile(*ForeignFilenames(i));
		}

		unguard;
	}

	INT Main( const TCHAR *Parms )
	{
		guard(UCompareIntCommandlet::Main);

		UClass* EditorEngineClass = UObject::StaticLoadClass( UEditorEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.EditorEngine"), NULL, LOAD_NoFail | LOAD_DisallowFiles, NULL );
		GEditor  = ConstructObject<UEditorEngine>( EditorEngineClass );
		GEditor->UseSound = 0;
        GEditor->InitEditor();
		GIsRequestingExit = 1; // Causes ctrl-c to immediately exit.

		// get the extension that we want to check
		if( !ParseToken(Parms, LangExt, 0) )
			appErrorf(TEXT("Example: ucc compareint <ext>"));

		TArray<FString> EnglishFilenames;
		TArray<FString> ForeignFilenames;

		FString EnglishLocation = FString::Printf(TEXT("%s*.int"), GetDefaultLocalizationFolder());
		FString ForeignLocation = FString::Printf(TEXT("Localization\\%s\\*.%s"), *LangExt, *LangExt);

		// grab the list of english loc files
		EnglishFilenames = GFileManager->FindFiles(*EnglishLocation, 1, 0);
		if ( EnglishFilenames.Num() == 0 )
		{
			appErrorf(TEXT("Failed to load english loc files at '%s'"), *EnglishLocation);
			return 1;
		}

		// get a list of foreign loc files
		ForeignFilenames = GFileManager->FindFiles(*ForeignLocation, 1, 0);
		if ( ForeignFilenames.Num() == 0 )
		{
			appErrorf(TEXT("Failed to load foreign loc files at '%s'"), *ForeignLocation);
			return 1;
		}

		FString EnglishLocFolder = GetDefaultLocalizationFolder();
		FString ForeignLocFolder = FString::Printf(TEXT("Localization\\%s\\"), *LangExt);


		// load them all into the config cache
		// for some reason, I have to force the config cache to load all the files I'll be reading, or it crashes
		// when it reallocs at 140 elements.
		for ( INT i = 0; i < EnglishFilenames.Num(); i++ )
		{
			EnglishFilenames(i) = EnglishLocFolder + EnglishFilenames(i);
			((FConfigCacheIni*)GConfig)->Find(*EnglishFilenames(i),0);
		}

		for ( INT i = 0; i < ForeignFilenames.Num(); i++ )
		{
			ForeignFilenames(i) = ForeignLocFolder + ForeignFilenames(i);
			((FConfigCacheIni*)GConfig)->Find(*ForeignFilenames(i),0);
		}

		ReadLocFiles(EnglishFilenames, ForeignFilenames);

		// show the results
		TArray<FString> MissingFiles, ObsoleteFiles;

		// for each file in the list, 
		for ( INT i = 0; i < LocPairs.Num(); i++ )
		{
			TArray<FString> MissingSections, ObsoleteSections;
			TArray<FString> MissingProperties, ObsoleteProperties, UntranslatedProperties;

			FLocalizationFilePair& Pair = LocPairs(i);

			Pair.CompareSections();

			// first, search for any english files that don't have corresponding foreign files,
			// and add them to the "missing foreign file" list
			Pair.GetMissingLocFiles(MissingFiles);

			// next, search for any foreign files that don't have corresponding english files,
			// and add them to the "obsolete foreign file" list
			Pair.GetObsoleteLocFiles(ObsoleteFiles);

			// search for any sections that exist only in the english version, and add these section to
			// the "section missing from foreign file" list
			Pair.GetMissingSections(MissingSections);

			// search for any sections that exist only in the foreign version, and add these sections to
			// the "obsolete foreign sections" list
			Pair.GetObsoleteSections(ObsoleteSections);

			// for each section, search for any properties that only exist in the english version, and add these to the
			// "properties missing from foreign" section list
			Pair.GetMissingProperties(MissingProperties);

			// next, for each section, search for any properties that only exist in the foreign version, and
			// add these to the "obsolete properties in foreign section" list
			Pair.GetObsoleteProperties(ObsoleteProperties);

			// finally, find all properties that have identical values in both the english and foreigh versions,
			// and add these properties to the "haven't been localized" list
			Pair.GetUntranslatedProperties(UntranslatedProperties);

			if ( MissingSections.Num() || ObsoleteSections.Num() ||
				MissingProperties.Num() || ObsoleteProperties.Num() ||
				UntranslatedProperties.Num() )
			{
				warnf(TEXT("\r\n======== %s ========"), Pair.GetFilename());
			}


			// display the results of our findings.
			if ( MissingSections.Num() )
			{
				warnf(TEXT("\r\n    MISSING SECTIONS:"));
				for ( INT i = 0; i < MissingSections.Num(); i++ )
				{
					warnf(TEXT("        %s"), *MissingSections(i));
				}
			}

			if ( ObsoleteSections.Num() )
			{
				warnf(TEXT("\r\n    OBSOLETE SECTIONS:"));
				for ( INT i = 0; i < ObsoleteSections.Num(); i++ )
				{
					warnf(TEXT("        %s"), *ObsoleteSections(i));
				}
			}

			if ( MissingProperties.Num() )
			{
				warnf(TEXT("\r\n    MISSING PROPERTIES:"));
				for ( INT i = 0; i < MissingProperties.Num(); i++ )
				{
					warnf(TEXT("        %s"), *MissingProperties(i));
				}
			}

			if ( ObsoleteProperties.Num() )
			{
				warnf(TEXT("\r\n    OBSOLETE PROPERTIES:"));
				for ( INT i = 0; i < ObsoleteProperties.Num(); i++ )
				{
					warnf(TEXT("        %s"), *ObsoleteProperties(i));
				}
			}

			if ( UntranslatedProperties.Num() )
			{
				warnf(TEXT("\r\n    UNTRANSLATED PROPERTIES:"));
				for ( INT i = 0; i < UntranslatedProperties.Num(); i++ )
				{
					warnf(TEXT("        %s"), *UntranslatedProperties(i));
				}
			}

		}
		if ( MissingFiles.Num() )
		{
			warnf(TEXT("\r\nEnglish files with no matching %s file:"), *LangExt);
			for ( INT i = 0; i < MissingFiles.Num(); i++ )
			{
				warnf(TEXT("  %s"), *MissingFiles(i));
			}
		}

		if ( ObsoleteFiles.Num() )
		{
			warnf(TEXT("\r\n%s files with no matching english file:"), *LangExt);
			for ( INT i = 0; i < ObsoleteFiles.Num(); i++ )
			{
				warnf(TEXT("  %s"), *ObsoleteFiles(i));
			}
		}

		return 0;

		unguard;
	}

	FString LangExt;
	TArray<FLocalizationFilePair> LocPairs;
};
IMPLEMENT_CLASS(UCompareIntCommandlet)


/*-----------------------------------------------------------------------------
	UMergeIntCommandlet.
-----------------------------------------------------------------------------*/

class UMergeIntCommandlet : public UCommandlet
{
	DECLARE_CLASS(UMergeIntCommandlet,UCommandlet,CLASS_Transient,Editor);
	void StaticConstructor()
	{
		guard(UMergeIntCommandlet::StaticConstructor);

		LogToStdout     = 0;
		IsClient        = 1;
		IsEditor        = 1;
		IsServer        = 1;
		LazyLoad        = 1;
		ShowErrorCount  = 1;

		unguard;
	}

	INT Main( const TCHAR *Parms )
	{
		guard(UMergeIntCommandlet::Main);

		UClass* EditorEngineClass = UObject::StaticLoadClass( UEditorEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.EditorEngine"), NULL, LOAD_NoFail | LOAD_DisallowFiles, NULL );
		GEditor  = ConstructObject<UEditorEngine>( EditorEngineClass );
		GEditor->UseSound = 0;
        GEditor->InitEditor();
		GIsRequestingExit = 1; // Causes ctrl-c to immediately exit.

		FString DiffFile, IntExt;

		if( !ParseToken(Parms, DiffFile, 0) )
			appErrorf(TEXT("You must specify a change file"));
		if( !ParseToken(Parms, IntExt, 0) )
			appErrorf(TEXT("You must specify an intfile extension"));
		FString DiffText;
		if( GFileManager->FileSize(*DiffFile) < 0 || !appLoadFileToString( DiffText, *DiffFile ) )
			appErrorf(TEXT("Could not open %s"),*DiffFile);

		const TCHAR* Ptr = *DiffText;
		TMap<FString,FString> FileMap;
		FString StrLine;
		FString CurrentFile;
		while(ParseLine(&Ptr,StrLine))
		{
			if( StrLine.Left(5) == TEXT("-----") )
			{
				CurrentFile = StrLine.Mid( StrLine.InStr(TEXT(" ")) + 1 );
				CurrentFile = CurrentFile.Left( CurrentFile.InStr(TEXT(".")) );
			}
			else
			{
				FString* ExistingStr = FileMap.Find( *CurrentFile );
				if( ExistingStr )
					FileMap.Set( *CurrentFile, *(*ExistingStr + TEXT("\r\n") + StrLine) );
				else
					FileMap.Set( *CurrentFile, *StrLine );
			}		
		}

		for( TMultiMap<FString,FString>::TIterator It(FileMap); It; ++It )
		{
			appSaveStringToFile( It.Value(), *(It.Key()+TEXT(".temp")) );

			FConfigFile* NewSections = ((FConfigCacheIni*)(GConfig))->Find( *(It.Key()+TEXT(".temp")), 0 );
			check( NewSections );
			
			for( TMap<FString,FConfigSection>::TIterator It2(*NewSections); It2; ++It2 )
			{
				for( TMultiMap<FName,FString>::TIterator It3(It2.Value()); It3; ++It3 )
				{
					FConfigFile* ExistingFile = ((FConfigCacheIni*)(GConfig))->Find( *(FString(*It.Key())+TEXT(".")+IntExt), 0 );
					ExistingFile->Quotes = 1;

					warnf(TEXT("%s.%s: [%s] %s=\"%s\""), *It.Key(), *IntExt, *It2.Key(), *It3.Key(), *It3.Value() );
					GConfig->SetString( *It2.Key(), *It3.Key(), *It3.Value(), *(FString(*It.Key())+TEXT(".")+IntExt));
				}
			}
		}

		for( TMultiMap<FString,FString>::TIterator It(FileMap); It; ++It )
			GFileManager->Delete(*(It.Key()+TEXT(".temp")));

		return 0;

		unguard;
	}
};
IMPLEMENT_CLASS(UMergeIntCommandlet)

/*-----------------------------------------------------------------------------
	URearrangeIntCommandlet.
-----------------------------------------------------------------------------*/

class URearrangeIntCommandlet : public UCommandlet
{
	DECLARE_CLASS(URearrangeIntCommandlet,UCommandlet,CLASS_Transient,Editor);
	void StaticConstructor()
	{
		guard(URearrangeIntCommandlet::StaticConstructor);

		LogToStdout     = 0;
		IsClient        = 1;
		IsEditor        = 1;
		IsServer        = 1;
		LazyLoad        = 1;
		ShowErrorCount  = 1;

		unguard;
	}

	INT Main( const TCHAR *Parms )
	{
		guard(URearrangeIntCommandlet::Main);

		UClass* EditorEngineClass = UObject::StaticLoadClass( UEditorEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.EditorEngine"), NULL, LOAD_NoFail | LOAD_DisallowFiles, NULL );
		GEditor  = ConstructObject<UEditorEngine>( EditorEngineClass );
		GEditor->UseSound = 0;
        GEditor->InitEditor();
		GIsRequestingExit = 1; // Causes ctrl-c to immediately exit.

		FString NewInt, OldInt;

		if( !ParseToken(Parms, NewInt, 0) )
			appErrorf(TEXT("Example: uuc RearrangeInt newint.frt oldint.frt"));

		if( !ParseToken(Parms, OldInt, 0) )
			appErrorf(TEXT("Example: uuc RearrangeInt newint.frt oldint.frt"));

		FConfigFile* IntSections = ((FConfigCacheIni*)(GConfig))->Find( *OldInt, 0 );
		check( IntSections );

		for( TMap<FString,FConfigSection>::TIterator It(*IntSections); It; ++It )
		{
			for( TMultiMap<FName,FString>::TIterator It2(It.Value()); It2; ++It2 )
			{
				FString Section = It.Key();
				FString Key		= *It2.Key();
				FString Value	= It2.Value();

				FString TempStr;
				if( GConfig->GetString( *Section, *Key, TempStr, *NewInt ) )
					GConfig->SetString( *Section, *Key, *Value, *NewInt );
				else
				{
					// new subobject format
					INT i = Section.InStr(TEXT("."));
					if( i != -1 )
					{
						Key = Section.Mid(i+1) + TEXT(".") + Key;
						Section = Section.Left(i);
						if( GConfig->GetString( *Section, *Key, TempStr, *NewInt ) )
							GConfig->SetString( *Section, *Key, *Value, *NewInt );
					}
				}
			}
		}

		return 0;
		unguard;
	}
};
IMPLEMENT_CLASS(URearrangeIntCommandlet);

