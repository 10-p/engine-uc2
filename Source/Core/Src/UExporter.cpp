/*=============================================================================
	UExporter.cpp: Exporter class implementation.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

// Core includes.
#include "CorePrivate.h"

/*----------------------------------------------------------------------------
	UExporter.
----------------------------------------------------------------------------*/

void UExporter::StaticConstructor()
{
	guard(UExporter::StaticConstructor);
	UArrayProperty* A = new(GetClass(),TEXT("Formats"),RF_Public)UArrayProperty(CPP_PROPERTY(Formats),TEXT(""),0);
	A->Inner = new(A,TEXT("StrProperty0"),RF_Public)UStrProperty;
	unguard;
}
UExporter::UExporter()
: Formats( E_NoInit )
{}
void UExporter::Serialize( FArchive& Ar )
{
	guard(UExporter::Serialize);
	Super::Serialize( Ar );
	Ar << SupportedClass << Formats;
	unguard;
}
IMPLEMENT_CLASS(UExporter);

/*----------------------------------------------------------------------------
	Object exporting.
----------------------------------------------------------------------------*/

//
// Find an exporter.
//
UExporter* UExporter::FindExporter( UObject* Object, const TCHAR* FileType )
{
	guard(UExporter::FindExporter);
	check(Object);

	TMap<UClass*,UClass*> Exporters;

	for( TObjectIterator<UClass> It; It; ++It )
	{
		if( It->IsChildOf(UExporter::StaticClass()) )
		{
			UExporter* Default = (UExporter*)It->GetDefaultObject();
			if( Default->SupportedClass && Object->IsA(Default->SupportedClass) )
				for( INT i=0; i<Default->Formats.Num(); i++ )
					if
					(	appStricmp( *Default->Formats(i), FileType  )==0
					||	appStricmp( *Default->Formats(i), TEXT("*") )==0 )
						Exporters.Set( Default->SupportedClass, *It );
		}
	}

	UClass** E;
	for( UClass* TempClass=Object->GetClass(); TempClass; TempClass=(UClass*)TempClass->SuperField )
		if( (E = Exporters.Find( TempClass )) != NULL )
			return ConstructObject<UExporter>( *E );
		
	return NULL;
	unguard;
}

//
// Export an object to an archive.
//
void UExporter::ExportToArchive( UObject* Object, UExporter* InExporter, FArchive& Ar, const TCHAR* FileType )
{
	guard(UExporter::ExportToArchive);
	check(Object);
	UExporter* Exporter = InExporter;
	if( !Exporter )
	{
		Exporter = FindExporter( Object, FileType );
	}
	if( !Exporter )
	{
		warnf( TEXT("No %s exporter found for %s"), FileType, Object, Object->GetFullName() );
		return;
	}
	check(Object->IsA(Exporter->SupportedClass));
	Exporter->ExportBinary( Object, FileType, Ar, GWarn );
	if( !InExporter )
		delete Exporter;
	unguard;
}

//
// Export an object to an output device.
//
void UExporter::ExportToOutputDevice( UObject* Object, UExporter* InExporter, FOutputDevice& Out, const TCHAR* FileType, INT Indent )
{
	guard(UExporter::ExportToOutputDevice);
	check(Object);
	UExporter* Exporter = InExporter;
	if( !Exporter )
	{
		Exporter = FindExporter( Object, FileType );
	}
	if( !Exporter )
	{
		warnf( TEXT("No %s exporter found for %s"), FileType, Object->GetFullName() );
		return;
	}
	check(Object->IsA(Exporter->SupportedClass));
	INT SavedIndent = Exporter->TextIndent;
	Exporter->TextIndent = Indent;
	Exporter->ExportText( Object, FileType, Out, GWarn );
	Exporter->TextIndent = SavedIndent;
	if( !InExporter )
		delete Exporter;
	unguard;
}

//
// Export this object to a file.  Child classes do not
// override this, but they do provide an Export() function
// to do the resoource-specific export work.  Returns 1
// if success, 0 if failed.
//
UBOOL UExporter::ExportToFile( UObject* Object, UExporter* InExporter, const TCHAR* Filename, UBOOL NoReplaceIdentical, UBOOL Prompt, UBOOL bForce )
{
	guard(UExporter::ExportToFile);
	check(Object);
	UExporter* Exporter = InExporter;
	const TCHAR* FileType = appFExt(Filename);
	UBOOL Result = 0;
	if( !Exporter )
	{
		Exporter = FindExporter( Object, FileType );
	}
	if( !Exporter )
	{
		warnf( TEXT("No %s exporter found for %s"), FileType, Object->GetFullName() );
		return 0;
	}
	if( Exporter->bText )
	{
		FStringOutputDevice Buffer;
		ExportToOutputDevice( Object, Exporter, Buffer, FileType, 0 );
		if( NoReplaceIdentical )
		{
			FString FileBytes;
			appLoadFileToString(FileBytes,Filename);
			if (Buffer.Len() == FileBytes.Len() &&
				appStrCrc(*Buffer) == appStrCrc(*FileBytes))
			{
				debugf( TEXT("Not replacing %s because identical"), Filename );
				Result = 1;
				goto Done;
			}
			if( Prompt )
			{
				if( !bForce && !GWarn->YesNof( LocalizeQuery(TEXT("Overwrite"),TEXT("Core")), Filename ) )
				{
					Result = 1;
					goto Done;
				}
			}
		}
		if( !appSaveStringToFile( Buffer, Filename ) ) {
			//scion capps
			if( bForce || GWarn->YesNof( LocalizeQuery(TEXT("CheckoutClassesFile"),TEXT("Core")), Filename ) )
			{
				appSystemCmd(TEXT("p4 edit"), Filename, NULL);
			}
		}

		if( !appSaveStringToFile( Buffer, Filename ) )
		{
			if( bForce || GWarn->YesNof( LocalizeQuery(TEXT("OverwriteReadOnly"),TEXT("Core")), Filename ) )
			{
				GFileManager->Delete( Filename, 0, 1 );
				if( appSaveStringToFile( Buffer, Filename ) )
				{
					Result = 1;
					goto Done;
				}
			}
			warnf( LocalizeError(TEXT("ExportOpen"),TEXT("Core")), Object->GetFullName(), Filename );
			goto Done;
		}
		Result = 1;
	}
	else
	{
		FBufferArchive Buffer;
		ExportToArchive( Object, Exporter, Buffer, FileType );
		if( NoReplaceIdentical )
		{
			TArray<BYTE> FileBytes;
			if
			(	appLoadFileToArray(FileBytes,Filename)
			&&	FileBytes.Num()==Buffer.Num()
			&&	appMemcmp(&FileBytes(0),&Buffer(0),Buffer.Num())==0 )
			{
				debugf( TEXT("Not replacing %s because identical"), Filename );
				Result = 1;
				goto Done;
			}
			if( Prompt )
			{
				if( !bForce && !GWarn->YesNof( LocalizeQuery(TEXT("Overwrite"),TEXT("Core")), Filename ) )
				{
					Result = 1;
					goto Done;
				}
			}
		}
		if( !appSaveArrayToFile( Buffer, Filename ) )
		{
			warnf( LocalizeError(TEXT("ExportOpen"),TEXT("Core")), Object->GetFullName(), Filename );
			goto Done;
		}
		Result = 1;
	}
Done:
	if( !InExporter )
		delete Exporter;
	return Result;
	unguard;
}


/*------------------------------------------------------------------------------
	UObjectExporterT3D implementation.
------------------------------------------------------------------------------*/

void UObjectExporterT3D::StaticConstructor()
{
	guard(UObjectExporterT3D::StaticConstructor);
	SupportedClass = UObject::StaticClass();
	bText = 1;
	new(Formats)FString(TEXT("T3D"));
	unguard;
}
UBOOL UObjectExporterT3D::ExportText( UObject* Object, const TCHAR* Type, FOutputDevice& Ar, FFeedbackContext* Warn )
{
	guard(UObjectExporterT3D::ExportText);
	Ar.Logf( TEXT("%sBegin Object Class=%s Name=%s Outer=%s\r\n"),appSpc(TextIndent), Object->GetClass()->GetName(), Object->GetName(), Object->GetOuter()->GetPathName() );
	ExportProperties( Ar, Object->GetClass(), (BYTE*)Object, TextIndent+3,Object->GetClass(), Object->GetClass()->GetDefaultsSafe() );
	Ar.Logf( TEXT("%sEnd Object\r\n"), appSpc(TextIndent) );
	return 1;
	unguard;
}
IMPLEMENT_CLASS(UObjectExporterT3D);

/* scion ======================================================================
 * UObjectExporterDefProps::ExportText
 * Author: jg
 *
 * Exports the passed in object as a set of defaultproperties. It places the
 * defaultproperties header into the stream, followed by any nested UObjects,
 * and finally by the object properties themselves.
 *
 * input:	pObject UObject The object to export as a set of defaultproperties
 *			Type const TCHAR* The type of export occuring
 *			Ar FOutputDevice The device to write the export to
 *			Warn FFeedbackContext Where to issue questions to the user
 * ============================================================================
 */
UBOOL UObjectExporterDefProps::ExportText(UObject* pObject,const TCHAR* Type,
	FOutputDevice& Ar,FFeedbackContext* Warn)
{
	guard(UObjectExporterDefProps::ExportText);
	// Write out the defaultproperties header info
	Ar.Log(TEXT("\r\ndefaultproperties\r\n{\r\n"));

	// RF_TagImp is used by ExportNestedObjects to prevent re-entrency during export
	// Reset RF_TagImp for all objects (since it may have been set during a previous call to ExportNestedObjects()) 
	for ( FObjectIterator It; It; ++It )
		It->ClearFlags(RF_TagImp);

	// Recursively export any nested UObject properties
	ExportNestedObjects(pObject,Type,Ar,Warn);
	// Now close the defaultproperties section
	Ar.Log(TEXT("}\r\n"));
	return 1;
	unguard;
}

/* scion ======================================================================
 * UObjectExporterDefProps::ExportNestedObjects
 * Author: jg
 *
 * Recursively exports the nested objects contained within the passed in object
 *
 * input:	pObject UObject The object to export its nested objects
 *			Type const TCHAR* The type of export occuring
 *			Ar FOutputDevice The device to write the export to
 *			Warn FFeedbackContext Where to issue questions to the user
 *			bEmitBeginEnd UBOOL Whether to emit the begin object end object
 *			wrapper
 * ============================================================================
 */
void UObjectExporterDefProps::ExportNestedObjects(UObject* pObject,
	const TCHAR* Type,FOutputDevice& Ar,FFeedbackContext* Warn,
	UBOOL bEmitBeginEnd)
{
	guard(UObjectExporterDefProps::ExportText);

	// Get the class of the object being exported so we can walk its properties
	UClass* pClass = pObject->GetClass();

	// Iterate through the properties on this object and export any
	// UObjectProperty or UArrayProperty containing UObjectProperty types first
	for (TFieldIterator<UProperty,CLASS_IsAUProperty> It(pClass);
		It; ++It)
	{
		// Check as to whether this property should be exported
		if (It->Port())
		{
			// In case this is an array of Objects
			for (INT nIndex = 0; nIndex < It->ArrayDim; nIndex++)
			{
				// Check for a UObject property
				if (It->IsA(UObjectProperty::StaticClass()) &&
					It->PropertyFlags & CPF_ExportObject)
				{
					// Do the pointer math to get the UObject in the array
					UObject* pNested = *(UObject**)((BYTE*)pObject + It->Offset +
						(nIndex * It->ElementSize));
					// Only export it if it is valid and not previously exported
					if (pNested && !(pNested->GetFlags() & RF_TagImp))
					{
						// Mark it so it does not get exported again
						pNested->SetFlags(RF_TagImp);

						// Recursively export it
						ExportNestedObjects(pNested,Type,Ar,Warn,1);
					}
				}
				// Check for UArrayProperty
				else if (It->IsA(UArrayProperty::StaticClass()))
				{
					// If pInner is not null, then it is a UObject
					UObjectProperty* pInner = Cast<UObjectProperty>(
						Cast<UArrayProperty>(*It)->Inner);
					// Check for an exportable UObject inner element
					if ((It->PropertyFlags & CPF_ExportObject) && pInner)
					{
						// Get a pointer to the array
						FArray* pArray = (FArray*)((BYTE*)pObject + It->Offset +
							nIndex * It->ElementSize);
						// Loop through the dynamic array and export those objects
						for (INT nDynArray = 0; nDynArray < pArray->Num();
							nDynArray++)
						{
							// Use the Data member of FArray to get to the
							// object pointer
							UObject* pNested =
								((UObject**)pArray->GetData())[nDynArray];
							// Only export it if it is valid and not previously
							// exported
							if (pNested && !(pNested->GetFlags() & RF_TagImp))
							{
								// Mark it so it does not get exported again
								pNested->SetFlags(RF_TagImp);

								// Recursively export it
								ExportNestedObjects(pNested,Type,Ar,Warn,1);
							}
						}
					}
				}
			}
		}
	}
	// Emit the begin/end wrapper if requested
	if (bEmitBeginEnd)
	{
		Ar.Logf(TEXT("%sBegin Object Class=%s Name=%s\r\n"),appSpc(2),
			pClass->GetName(),pObject->GetName());
	}
	// Now export the remainging object properties
	ExportProperties(Ar,pObject->GetClass(),(BYTE*)pObject,TextIndent + 3,
		pObject->GetClass(),pObject->GetClass()->GetDefaultsSafe());
	// Emit the begin/end wrapper if requested
	if (bEmitBeginEnd)
	{
		Ar.Logf(TEXT("%sEnd Object\r\n"),appSpc(2));
	}
	unguard;
}

IMPLEMENT_CLASS(UObjectExporterDefProps);
/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/

