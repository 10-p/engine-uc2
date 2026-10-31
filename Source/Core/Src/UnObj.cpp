/*=============================================================================
	UnObj.cpp: Unreal object manager.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "CorePrivate.h"
#include "UC2ScriptSaveConfig.h"

// GEARBOX/jmw: This line enables Iterative Garbage Collection, which reduces total stack size needed for the game.
#define ITERATIVE_GC 1

/*-----------------------------------------------------------------------------
	Globals.
-----------------------------------------------------------------------------*/

// Object manager internal variables.
UBOOL						UObject::GObjInitialized		= 0;
UBOOL						UObject::GObjNoRegister			= 0;
INT							UObject::GObjBeginLoadCount		= 0;
INT							UObject::GObjRegisterCount		= 0;
INT							UObject::GImportCount			= 0;
UObject*					UObject::GAutoRegister			= NULL;
UPackage*					UObject::GObjTransientPkg		= NULL;
TCHAR						UObject::GObjCachedLanguage[32] = TEXT("");
TCHAR						UObject::GLanguage[64]          = TEXT("int");

//scion superville: Setup default for static Localization folder variables
// Always default to international (int) language
TCHAR						UObject::GLocalizationFolder[64]		= TEXT("Localization\\int\\");
TCHAR						UObject::GDefaultLocalizationFolder[64] = TEXT("Localization\\int\\");

UObject*					UObject::GObjHash[OBJECT_HASH_BINS];
//jg -- Added for loading speed up
UObject*					UObject::GObjHashOuter[OBJECT_HASH_BINS];
TArray<UObject*>			UObject::GObjLoaded;
TArray<UObject*>			UObject::GObjObjects;
TArray<INT>					UObject::GObjAvailable;
TArray<UObject*>			UObject::GObjLoaders;
TArray<UObject*>			UObject::GObjRoot;
TArray<UObject*>			UObject::GObjRegistrants;
TArray<FPreferencesInfo>	UObject::GObjPreferences;
TArray<FRegistryObjectInfo> UObject::GObjDrivers;
TMultiMap<FName,FName>*		UObject::GObjPackageRemap;
static INT GGarbageRefCount=0;

//scion jg -- Xbox language ID to 3 letter language ID conversion
// These are from the ISO standard 639-2 which can be found at:
// http://www.loc.gov/standards/iso639-2/englangn.html
CORE_API TCHAR* GLanguageExt[LC_MAXLANGS] =
{
#if 1 // Fallback to INT for languages we don't support
	TEXT("INT"),
	TEXT("INT"),
	TEXT("INT"),
	TEXT("DEU"),
	TEXT("FRA"),
	TEXT("SPA"),
	TEXT("ITA"),
	TEXT("INT"),
	TEXT("INT"),
	TEXT("INT")
#else // The full list if we supported all Xbox languages
	TEXT("INT"),
	TEXT("INT"),
	TEXT("JPN"),
	TEXT("DEU"),
	TEXT("FRA"),
	TEXT("SPA"),
	TEXT("ITA"),
	TEXT("KOR"),
	TEXT("CHI"),
	TEXT("POR")
#endif
};

/*-----------------------------------------------------------------------------
	UObject constructors.
-----------------------------------------------------------------------------*/

UObject::UObject()
{}
UObject::UObject( const UObject& Src )
{
	guard(UObject::UObject);
	check(&Src);
	if( Src.GetClass()!=GetClass() )
		appErrorf( TEXT("Attempt to copy-construct %s from %s"), GetFullName(), Src.GetFullName() );
	unguard;
}
UObject::UObject( EInPlaceConstructor, UClass* InClass, UObject* InOuter, FName InName, DWORD InFlags )
{
	guard(UObject::UObject);
	StaticAllocateObject( InClass, InOuter, InName, InFlags, NULL, GError, this );
	unguard;
}
UObject::UObject( ENativeConstructor, UClass* InClass, const TCHAR* InName, const TCHAR* InPackageName, DWORD InFlags )
:	Index		( INDEX_NONE				)
,	HashNext	( NULL						)
,	HashOuterNext	( NULL						)
,	StateFrame	( NULL						)
,	_Linker		( NULL						)
,	ObjectFlags	( InFlags | RF_Native	    )
,	Class		( InClass					)
,	_LinkerIndex( INDEX_NONE				)
,	Outer       ( NULL						)
,	Name        ( NAME_None					)
{
	guard(UObject::UObject);

	// Make sure registration is allowed now.
	check(!GObjNoRegister);

	// Setup registration info, for processing now (if inited) or later (if starting up).
	check(sizeof(Outer       )>=sizeof(InPackageName));
#if SERIAL_POINTER_INDEX
	// FName and _LinkerIndex are 4 bytes, the stashed name and the autoregister chain are pointers.
	// Park them in pointer-sized members that are NULL until Register() (which clears them again).
	*(const TCHAR  **)&StateFrame   = InName;
	*(const TCHAR  **)&Outer        = InPackageName;
	*(UObject      **)&_Linker      = GAutoRegister;
#else
	check(sizeof(_LinkerIndex)>=sizeof(GAutoRegister));
	check(sizeof(Name        )>=sizeof(InName       ));
	*(const TCHAR  **)&Name         = InName;

	*(const TCHAR  **)&Outer        = InPackageName;
	*(UObject      **)&_LinkerIndex = GAutoRegister;
#endif
	GAutoRegister                   = this;

	// Call native registration from terminal constructor.
	if( GetInitialized() && GetClass()==StaticClass() )
		Register();

	unguard;
}
UObject::UObject( EStaticConstructor, const TCHAR* InName, const TCHAR* InPackageName, DWORD InFlags )
:	Index		( INDEX_NONE				)
,	HashNext	( NULL						)
,	StateFrame	( NULL						)
,	_Linker		( NULL						)
,	ObjectFlags	( InFlags | RF_Native	    )
,	_LinkerIndex( INDEX_NONE				)
,	Outer       ( NULL						)
,	Name        ( NAME_None					)
{
	guard(UObject::UObject);

	// Setup registration info, for processing now (if inited) or later (if starting up).
	check(sizeof(Outer       )>=sizeof(InPackageName));
#if SERIAL_POINTER_INDEX
	// As in the native constructor above.
	*(const TCHAR  **)&StateFrame   = InName;
#else
	check(sizeof(_LinkerIndex)>=sizeof(GAutoRegister));
	check(sizeof(Name        )>=sizeof(InName       ));
	*(const TCHAR  **)&Name         = InName;
#endif

	*(const TCHAR  **)&Outer        = InPackageName;

	
	// If we are not initialized yet, auto register.
	if (!GObjInitialized)
	{
#if SERIAL_POINTER_INDEX
		*(UObject      **)&_Linker      = GAutoRegister;
#else
		*(UObject      **)&_LinkerIndex = GAutoRegister;
#endif
		GAutoRegister                   = this;
	}
	unguard;
}
UObject& UObject::operator=( const UObject& Src )
{
	guard(UObject::operator=);
	check(&Src);
	if( Src.GetClass()!=GetClass() )
		appErrorf( TEXT("Attempt to assign %s from %s"), GetFullName(), Src.GetFullName() );
	return *this;
	unguardobj;
}

/*-----------------------------------------------------------------------------
	UObject class initializer.
-----------------------------------------------------------------------------*/

void UObject::StaticConstructor()
{
	guard(UObject::StaticConstructor);
	unguard;
}

/*-----------------------------------------------------------------------------
	UObject implementation.
-----------------------------------------------------------------------------*/

//
// Rename this object to a unique name.
//
void UObject::Rename( const TCHAR* InName, UObject* NewOuter )
{
	guard(UObject::Rename);

	FName NewName = InName ? FName(InName) : MakeUniqueObjectName( NewOuter ? NewOuter : GetOuter(), GetClass() );
	UnhashObject( Outer ? Outer->GetIndex() : 0 );
	debugf( TEXT("Renaming %s to %s"), *Name, *NewName );
	Name = NewName;
	if( NewOuter )
		Outer = NewOuter;
	HashObject();

	unguardobj;
}

//
// Shutdown after a critical error.
//
void UObject::ShutdownAfterError()
{
	guard(UObject::ShutdownAfterError);
	unguard;
}

//
// Make sure the object is valid.
//
UBOOL UObject::IsValid()
{
	guard(UObject::IsValid);
	if( !this )
	{
		debugf( NAME_Warning, TEXT("NULL object") );
		return 0;
	}
	else if( !GObjObjects.IsValidIndex(GetIndex()) )
	{
		debugf( NAME_Warning, TEXT("Invalid object index %i"), GetIndex() );
		debugf( NAME_Warning, TEXT("This is: %s"), GetName() );
		return 0;
	}
	else if( GObjObjects(GetIndex())==NULL )
	{
		debugf( NAME_Warning, TEXT("Empty slot") );
		debugf( NAME_Warning, TEXT("This is: %s"), GetFullName() );
		return 0;
	}
	else if( GObjObjects(GetIndex())!=this )
	{
		debugf( NAME_Warning, TEXT("Other object in slot") );
		debugf( NAME_Warning, TEXT("Other is: %s"), GObjObjects(GetIndex())->GetFullName() );
		debugf( NAME_Warning, TEXT("This is: %s"), GetName() );
		return 0;
	}
	else return 1;
	unguardobj;
}

//
// Do any object-specific cleanup required
// immediately after loading an object, and immediately
// after any undo/redo.
//
void UObject::PostLoad()
{
	guard(UObject::PostLoad);

	// Note that it has propagated.
	SetFlags( RF_DebugPostLoad );

	// Load per-object localization
	if( ObjectFlags&RF_PerObjectLocalized)
		LoadLocalized();
	//scion jg -- Complain about loading a deprecated object
	if (GIsUCC == 0 && (StaticClass()->ClassFlags & CLASS_Deprecated))
	{
		debugf(NAME_Warning,
			TEXT("Object '%s' is deprecated and should not be used"),
			GetFullName());
	}
	unguardobj;
}

//
// Edit change notification.
//
void UObject::PostEditChange()
{
	guard(UObject::PostEditChange);
	Modify();
	unguardobj;
}

//
// Do any object-specific cleanup required immediately
// before an object is killed.  Child classes may override
// this if they have to do anything here.
//
void UObject::Destroy()
{
	guard(UObject::Destroy);
	SetFlags( RF_DebugDestroy );

	// Destroy properties.
	ExitProperties( (BYTE*)this, GetClass() );

	// Log message.
	if( GObjInitialized && !GIsCriticalError )
		debugf( NAME_DevKill, TEXT("Destroying %s"), GetFullName() );

	// Unhook from linker.
	SetLinker( NULL, INDEX_NONE );

	// Remember outer name index.
	_LinkerIndex = Outer ? Outer->GetIndex() : 0;

	unguardobj;
}

//
// Set the object's linker.
//
void UObject::SetLinker( ULinkerLoad* InLinker, INT InLinkerIndex )
{
	guard(UObject::SetLinker);

	// Detach from existing linker.
	if( _Linker )
	{
//		check(_Linker->ExportMap(_LinkerIndex)._Object!=NULL);
//		check(_Linker->ExportMap(_LinkerIndex)._Object==this);
		_Linker->ExportMap(_LinkerIndex)._Object = NULL;
	}

	// Set new linker.
	_Linker      = InLinker;
	_LinkerIndex = InLinkerIndex;

	unguardobj;
}

//
// Return the object's path name.
//warning: Must be safe for NULL objects.
//warning: Must be safe for class-default meta-objects.
//
const TCHAR* UObject::GetPathName( UObject* StopOuter, TCHAR* Str, INT MaxLen ) const
{
	guard(UObject::GetPathName);
	
	// Return one of 8 circular results.
	TCHAR* Result = Str ? Str : appStaticString1024();
	if( this!=StopOuter )
	{
		*Result = 0;

		if( Outer!=StopOuter )
		{
			Outer->GetPathName( StopOuter, Result, MaxLen );
			appStrcat( Result, TEXT(".") );
		}
		appStrcat( Result, GetName() );
	}
	else appSprintf( Result, TEXT("None") );

	return Result;
	unguard; // Not unguardobj, because it causes crash recusion.
}

//
// Return the object's full name.
//warning: Must be safe for NULL objects.
//
const TCHAR* UObject::GetFullName( TCHAR* Str ) const
{
	guard(UObject::GetFullName);

	// Return one of 8 circular results.
	TCHAR* Result = Str ? Str : appStaticString1024();
	if( this )
	{
		appSprintf( Result, TEXT("%s "), GetClass()->GetName() );

		INT MaxLen = 1024 - appStrlen(Result);
		GetPathName( NULL, Result + appStrlen(Result), MaxLen );
	}
	else
	{
		appStrcpy( Result, TEXT("None") );
	}

	return Result;
	unguard; // Not unguardobj, because it causes crash recusion.
}

//
// Destroy the object if necessary.
//
UBOOL UObject::ConditionalDestroy()
{
	guard(UObject::ConditionalDestroy);
	if( Index!=INDEX_NONE && !(GetFlags() & RF_Destroyed) )
	{
		SetFlags( RF_Destroyed );
#ifndef FINAL_RELEASE
		// make sure that Super.Destroy is being called
		DWORD SavedFlags = GetFlags();
		ClearFlags( RF_DebugDestroy );
		Destroy();
		if( !(GetFlags()&RF_DebugDestroy) ) {
			appErrorf( TEXT("%s failed to route Destroy"), GetFullName() );
		}
		// restore flags to original state
		ClearFlags( RF_DebugDestroy );
		SetFlags( SavedFlags );
#else
		// skip check for Destroy routing
		Destroy();
#endif
		return 1;
	}
	else return 0;
	unguardobj;
}

//
// Register if needed.
//
void UObject::ConditionalRegister()
{
	guard(UObject::ConditionalRegister);
	if( GetIndex()==INDEX_NONE )
	{
		// Verify this object is on the list to register.
		INT i;
		for( i=0; i<GObjRegistrants.Num(); i++ )
			if( GObjRegistrants(i)==this )
				break;
		check(i!=GObjRegistrants.Num());

		// Register it.
		Register();
	}
	unguardobj;
}

//
// PostLoad if needed.
//
void UObject::ConditionalPostLoad()
{
	guard(UObject::ConditionalPostLoad);
	if( GetFlags() & RF_NeedPostLoad )
	{
		ClearFlags( RF_NeedPostLoad | RF_DebugPostLoad );
		PostLoad();
		if( !(GetFlags() & RF_DebugPostLoad) )
 			appErrorf( TEXT("%s failed to route PostLoad"), GetFullName() );
	}
	unguardobj;
}

//
// UObject destructor.
//warning: Called at shutdown.
//
UObject::~UObject()
{
	guard(UObject::~UObject);

	// If not initialized, skip out.
	if( Index!=INDEX_NONE && GObjInitialized && !GIsCriticalError )
	{
		// Validate it.
		check(IsValid());

		// Destroy the object if necessary.
		ConditionalDestroy();

		// Remove object from table.
		UnhashObject( _LinkerIndex );
		GObjObjects(Index) = NULL;
		GObjAvailable.AddItem( Index );
	}

	// Free execution stack.
	if( StateFrame )
		delete StateFrame;

	unguard;
}

//
// Archive for persistent CRC's.
//
class FArchiveCRC : public FArchive
{
public:
	FArchiveCRC( UObject* Src, UObject* InBase )
	: CRC( 0 )
	, Base( InBase )
	{
		ArIsSaving = ArIsPersistent = 1;
		Src->Serialize( *this );
	}
	SIZE_T GetCRC()
	{
		return CRC;
	}
	void Serialize( void* Data, INT Num )
	{
		CRC = appMemCrc( Data, Num, CRC );
	}
	void SerializeCapsString( const TCHAR* Str )
	{
		TCHAR TCh;
		while( *Str )
		{
			TCh = appToUpper(*Str++);
			Serialize( &TCh, sizeof(TCh) );
		}
		TCh = 0;
		Serialize( &TCh, sizeof(TCh) );
	}
	virtual FArchive& operator<<( class FName& N )
	{
		SerializeCapsString( *N );
		return *this;
	}
	virtual FArchive& operator<<( class UObject*& Res )
	{
		if( Res )
		{
			SerializeCapsString( Res->GetClass()->GetName() );
			SerializeCapsString( Res->GetPathName(Res->IsIn(Base) ? Base : NULL) );
		}
		else SerializeCapsString( TEXT("None") );
		return *this;
	}
protected:
	DWORD CRC;
	UObject* Base;
};

//
// Note that the object has been modified.
//
void UObject::Modify()
{
	guard(UObject::Modify);

	// Perform transaction tracking.
	if( GUndo && (GetFlags() & RF_Transactional) )
		GUndo->SaveObject( this );

	unguardobj;
}

//
// UObject serializer.
//
void UObject::Serialize( FArchive& Ar )
{
	guard(UObject::Serialize);
	SetFlags( RF_DebugSerialize );

	// Make sure this object's class's data is loaded.
	if( Class != UClass::StaticClass() )
		Ar.Preload( Class );

	// Special info.
	if( (!Ar.IsLoading() && !Ar.IsSaving()) )
		Ar << Name << Outer << Class;
	if( !Ar.IsLoading() && !Ar.IsSaving() )
		Ar << _Linker;

	// Execution stack.
	//!!how does the stack work in conjunction with transaction tracking?
	guard(SerializeStack);
	if( !Ar.IsTrans() )
	{
		if( GetFlags() & RF_HasStack )
		{
			if( !StateFrame )
				StateFrame = NEW FStateFrame( this );
			Ar << StateFrame->Node << StateFrame->StateNode;
			Ar << StateFrame->ProbeMask;
			//jg -- Handle latent action size change
			if (Ar.Ver() < 143 && Ar.IsLoading())
			{
				INT LatentAction;
				// Read in an INT's worth of data
				Ar << LatentAction;
				// Shrink to our needs
				StateFrame->LatentAction = (_WORD)LatentAction;
			}
			else
			{
				Ar << StateFrame->LatentAction;
			}
			if( StateFrame->Node )
			{
				Ar.Preload( StateFrame->Node );
				if( Ar.IsSaving() && StateFrame->Code )
					check(StateFrame->Code>=&StateFrame->Node->Script(0) && StateFrame->Code<&StateFrame->Node->Script(StateFrame->Node->Script.Num()));
				INT Offset = StateFrame->Code ? StateFrame->Code - &StateFrame->Node->Script(0) : INDEX_NONE;
				Ar << AR_INDEX(Offset);
				if( Offset!=INDEX_NONE )
					if( Offset<0 || Offset>=StateFrame->Node->Script.Num() )
						appErrorf( TEXT("%s: Offset mismatch: %i %i"), GetFullName(), Offset, StateFrame->Node->Script.Num() );
				StateFrame->Code = Offset!=INDEX_NONE ? &StateFrame->Node->Script(Offset) : NULL;
			}
			else StateFrame->Code = NULL;
		}
		else if( StateFrame )
		{
			delete StateFrame;
			StateFrame = NULL;
		}
	}
	unguard;

	// Serialize object properties which are defined in the class.
	if( Class != UClass::StaticClass() )
	{
		if( (Ar.IsLoading() || Ar.IsSaving()) && !Ar.IsTrans() && !Ar.IsRawUData() )
			GetClass()->SerializeTaggedProperties( Ar, (BYTE*)this, Class );
		else
			GetClass()->SerializeBin( Ar, (BYTE*)this, 0 );
	}

	// Memory counting.
	SIZE_T Size = GetClass()->GetPropertiesSize();
	Ar.CountBytes( Size, Size );
	unguardobj;
}

//
// Export text object properties.
//
void UObject::ExportProperties
(
	FOutputDevice&	Out,
	UClass*			ObjectClass,
	BYTE*			Object,
	INT				Indent,
	UClass*			DiffClass,
	BYTE*			Diff
)
{
	const TCHAR* ThisName = TEXT("(none)");
	guard(UObject::ExportProperties);
	check(ObjectClass!=NULL);
	for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(ObjectClass); It; ++It )
	{
		if( It->Port() )
		{
			ThisName = It->GetName();			
			for( INT j=0; j<It->ArrayDim; j++ )
			{
				TCHAR Value[4096];
				if( It->IsA(UArrayProperty::StaticClass()) )
				{
					// Export dynamic array.
					UProperty* InnerProp = Cast<UArrayProperty>(*It)->Inner;
					UBOOL ExportObject = (It->PropertyFlags & CPF_ExportObject) && InnerProp->IsA(UObjectProperty::StaticClass());
											
					FArray* Arr = (FArray*)((BYTE*)Object + It->Offset + j*It->ElementSize);
					for( INT k=0;k<Arr->Num();k++ )
					{
						if( InnerProp->ExportText( k, Value, (BYTE*)Arr->GetData(), NULL, PPF_Delimited ) )
						{					
							if( ExportObject )
							{
								UObject* Obj = ((UObject**)Arr->GetData())[k];
								if( Obj && !(Obj->GetFlags() & RF_TagImp) )
								{
									// Don't export more than once.
									UExporter::ExportToOutputDevice( Obj, NULL, Out, TEXT("T3D"), Indent+1 );
									Obj->SetFlags( RF_TagImp );
								}
							}
		
							Out.Logf( TEXT("%s %s(%i)=%s\r\n"), appSpc(Indent), It->GetName(), k, Value );
						}
					}
				}
				else
				{
					// Export single element.
					if( It->ExportText( j, Value, Object, (DiffClass && DiffClass->IsChildOf(It.GetStruct())) ? Diff : NULL, PPF_Delimited ) )
					{
						if
						(	(It->IsA(UObjectProperty::StaticClass()))
						&&	(It->PropertyFlags & CPF_ExportObject) )
						{
							UObject* Obj = *(UObject **)((BYTE*)Object + It->Offset + j*It->ElementSize);					
							if( Obj && !(Obj->GetFlags() & RF_TagImp) )
							{
								// Don't export more than once.
								UExporter::ExportToOutputDevice( Obj, NULL, Out, TEXT("T3D"), Indent+1 );
								Obj->SetFlags( RF_TagImp );
							}
						}						

						if( It->ArrayDim == 1 )
							Out.Logf( TEXT("%s %s=%s\r\n"), appSpc(Indent), It->GetName(), Value );
						else
							Out.Logf( TEXT("%s %s(%i)=%s\r\n"), appSpc(Indent), It->GetName(), j, Value );
					}
				}
			}
		}
	}
	unguardf(( TEXT("(%s)"), ThisName ));
}

//
// Initialize script execution.
//
void UObject::InitExecution()
{
	guard(UObject::InitExecution);
	check(GetClass()!=NULL);

	if( StateFrame )
		delete StateFrame;
	StateFrame = NEW FStateFrame( this );
	SetFlags( RF_HasStack );

	unguardobj;
}

//
// Command line.
//
UBOOL UObject::ScriptConsoleExec( const TCHAR* Str, FOutputDevice& Ar, UObject* Executor )
{
	guard(UObject::ScriptConsoleExec);

	// No script execution?
	if( !GIsScriptable )
		return 0;

	// Find UnrealScript exec function.
	TCHAR MsgStr[NAME_SIZE];
	FName Message;
	UFunction* Function;
	if
	(	!ParseToken(Str,MsgStr,ARRAY_COUNT(MsgStr),1)
	||	(Message=FName(MsgStr,FNAME_Find))==NAME_None
	||	(Function=FindFunction(Message))==NULL
	||	!(Function->FunctionFlags & FUNC_Exec) )
		return 0;

	// Parse all function parameters.
	BYTE* Parms = (BYTE*)appAlloca(Function->ParmsSize);
	appMemzero( Parms, Function->ParmsSize );
	UBOOL Failed = 0;
	INT Count = 0;
	for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(Function); It && (It->PropertyFlags & (CPF_Parm|CPF_ReturnParm))==CPF_Parm; ++It,Count++ )
	{
		if( Count==0 && Executor )
		{
			UObjectProperty* Op = Cast<UObjectProperty>(*It);
			if( Op && Executor->IsA(Op->PropertyClass) )
			{
				// First parameter is implicit reference to object executing the command.
				*(UObject**)(Parms + It->Offset) = Executor;
				continue;
			}
		}
		ParseNext( &Str );
		Str = It->ImportText( Str, Parms+It->Offset, PPF_Localized | PPF_Delimited );
		if( !Str )
		{
			if( !(It->PropertyFlags & CPF_OptionalParm) )
			{
				Ar.Logf( LocalizeError(TEXT("BadProperty"),TEXT("Core")), *Message, It->GetName() );
				Failed = 1;
			}
			break;
		}
	}
	if( !Failed )
		ProcessEvent( Function, Parms );
	//!!destructframe see also UObject::ProcessEvent
	{for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(Function); It && (It->PropertyFlags & (CPF_Parm|CPF_ReturnParm))==CPF_Parm; ++It )
		It->DestroyValue( Parms + It->Offset );}

	// Success.
	return 1;
	unguardobj;
}

//
// Find an UnrealScript field.
//warning: Must be safe with class default metaobjects.
//
UField* UObject::FindObjectField( FName InName, UBOOL Global )
{
	guardSlow(UObject::StaticFindField);
	// Search current state scope.
	if( StateFrame && StateFrame->StateNode && !Global )
		for( TFieldIterator<UField> It(StateFrame->StateNode); It; ++It )
			if( It->GetFName()==InName )
				return *It;

	// Search the global scope.
	for( TFieldIterator<UField> It(GetClass()); It; ++It )
		if( It->GetFName()==InName )
			return *It;

	// Not found.
	return NULL;
	unguardfSlow(( TEXT("%s (node %s)"), GetFullName(), *InName ));
}
UFunction* UObject::FindFunction( FName InName, UBOOL Global )
{
	guardSlow(UObject::FindFunction);
	UFunction *func = NULL;
	if (StateFrame != NULL &&
		StateFrame->StateNode != NULL &&
		!Global)
	{
		UState *searchState = StateFrame->StateNode;
		while (searchState != NULL &&
			   func == NULL)
		{
			func = searchState->FuncMap.FindRef(InName);
			searchState = searchState->GetSuperState();
		}
	}
	if (func == NULL)
	{
		UClass *searchClass = GetClass();
		while (searchClass != NULL &&
			   func == NULL)
		{
			func = searchClass->FuncMap.FindRef(InName);
			searchClass = searchClass->GetSuperClass();
		}
	}
    return func;
	unguardfSlow(( TEXT("%s (function %s)"), GetFullName(), *InName ));
}

#ifndef FindFunctionChecked
//epic superville: Skip FindFunctionChecked if on XBox FINAL_RELEASE
// since it will never need it's checks
// It's redefined as FindFunction in UnObjBase.h
UFunction* UObject::FindFunctionChecked( FName InName, UBOOL Global )
{
	guardSlow(UObject::FindFunctionChecked);

//epic superville: Script is always enabled on XBox
#if !defined(_XBOX)
	if( !GIsScriptable )
		return NULL;
#endif

    UFunction* Result = FindFunction(InName,Global);
	if( !Result )
		appErrorf( TEXT("Failed to find function %s in %s"), *InName, GetFullName() );

	return Result;

	unguardfSlow(( TEXT("%s (function %s)"), GetFullName(), *InName ));
}
#endif

UState* UObject::FindState( FName InName )
{
	guardSlow(UObject::FindState);
	
	//epic moelfke: made this faster by not iterating through all the fields
	UState* State = NULL;
	for (TFieldIterator<UState, CLASS_IsAUState> It(GetClass()); It && !State; ++It)
	{
		if (It->GetFName() == InName)
		{
			State = *It;
		}
	}
    return State;

	unguardfSlow(( TEXT("%s (state %s)"), GetFullName(), *InName ));
}
IMPLEMENT_CLASS(UObject);

/*-----------------------------------------------------------------------------
	UObject configuration.
-----------------------------------------------------------------------------*/

//
// Load configuration.
//warning: Must be safe on class-default metaobjects.
//
void UObject::LoadConfig( UBOOL Propagate, UClass* Class, const TCHAR* InFilename )
{
	guard(UObject::LoadConfig);
	if( !Class )
		Class = GetClass();
	if( !(Class->ClassFlags & CLASS_Config) )
		return;
	if( Propagate && Class->GetSuperClass() )
		LoadConfig( Propagate, Class->GetSuperClass(), InFilename );
	UBOOL PerObject = ((GetClass()->ClassFlags & CLASS_PerObjectConfig) && GetIndex()!=INDEX_NONE);

	// scion sz - skip if config properties already in defaults
	if( (Class->ClassFlags & CLASS_ConfigLoaded) && 
		!PerObject )
		return;

	TCHAR Filename[256];
	//jg -- Copy this since the temporary one is getting stomped
	appStrcpy(Filename,InFilename ? InFilename :
		(PerObject && Outer!=GObjTransientPkg) ? GetOuter()->GetName() :
		*GetClass()->ClassConfigName);

#ifdef _DEBUG
	if ( GSys )
		debugf(NAME_DevSave, TEXT("(%s) '%s' loading configuration from %s"), Class->GetName(), GetName(), Filename);
#endif

	//epic moelfke: use PropertyLink instead of a slower TFieldIterator
	for (UProperty* It = Class->PropertyLink; It; It = It->PropertyLinkNext)
	{
		if( It->PropertyFlags & CPF_Config )
		{
			TCHAR TempKey[256], Value[1024]=TEXT("");
			UClass*         BaseClass = (It->PropertyFlags & CPF_GlobalConfig) ? It->GetOwnerClass() : Class;
			const TCHAR*    Section   = PerObject ? GetName() : BaseClass->GetPathName();
			const TCHAR*    Key       = It->ArrayDim==1 ? It->GetName() : TempKey;
			UArrayProperty* Array     = Cast<UArrayProperty>(It);
			UMapProperty*   Map       = Cast<UMapProperty>(It);

#ifdef _DEBUG
			if ( GSys && It->ArrayDim == 1 )
				debugf(NAME_DevSave,TEXT("   Loading value for %s from [%s]"), Key, Section);
#endif

			if( Array )
			{
				TMultiMap<FName,FString>* Sec = GConfig->GetSectionPrivate( Section, 0, 1, Filename );
				if( Sec )
				{
					TArray<FString> List;
					Sec->MultiFind( FName(Key), List );
					FArray* Ptr  = (FArray*)((BYTE*)this + It->Offset);
					INT     Size = Array->Inner->ElementSize;

					//epic superville: Clobber array only if this section has new data to put in it
					if( List.Num() > 0 )
					{
						Array->DestroyValue( Ptr );
						Ptr->AddZeroed( Size, List.Num() );
						for( INT i=List.Num()-1,c=0; i>=0; i--,c++ )
						{
							Array->Inner->ImportText( *List(i), (BYTE*)Ptr->GetData() + c*Size, 0 );
						}
					}
				}
			}
			else if( Map )
			{
				TMultiMap<FName,FString>* Sec = GConfig->GetSectionPrivate( Section, 0, 1, Filename );
				if( Sec )
				{
					TArray<FString> List;
					Sec->MultiFind( FName(Key), List );
					//FArray* Ptr  = (FArray*)((BYTE*)this + It->Offset);
					//INT     Size = Array->Inner->ElementSize;
					//Map->DestroyValue( Ptr );//!!won't do dction
					//Ptr->AddZeroed( Size, List.Num() );
					//for( INT i=List.Num()-1,c=0; i>=0; i--,c++ )
					//	Array->Inner->ImportText( *List(i), (BYTE*)Ptr->GetData() + c*Size, 0 );
				}
			}
			else for( INT i=0; i<It->ArrayDim; i++ )
			{
				if( It->ArrayDim!=1 )
				{
					appSprintf( TempKey, TEXT("%s[%i]"), It->GetName(), i );
#ifdef _DEBUG
					if ( GSys )
						debugf(NAME_DevSave, TEXT("   Loading value for %s from [%s]"), Key, Section);
#endif
				}
				if( GConfig->GetString( Section, Key, Value, ARRAY_COUNT(Value), Filename ) ) 
				{
					It->ImportText( Value, (BYTE*)this + It->Offset + i*It->ElementSize, 0 );
				}
			}
		}
	}

	unguardobj;
}

/* epic ===============================================
* UObject::UsesConfigEntries
* Author: sz
*
* Iterate through all properties for the class and see
* if a config entry is needed for any of them.
*
* =====================================================
*/
UBOOL UObject::UsesConfigEntries( UClass* Class )
{
	guard(UsesConfigEntries);
	check(Class);

	if( !(Class->ClassFlags & CLASS_Config) )
		return 0;

	//epic moelfke: use PropertyLink instead of a slower TFieldIterator
	for (UProperty* It = Class->PropertyLink; It; It = It->PropertyLinkNext)
	{
		if( It->PropertyFlags & CPF_Config )
		{
			TCHAR TempKey[256], Value[1024]=TEXT("");
			UClass*         BaseClass = (It->PropertyFlags & CPF_GlobalConfig) ? It->GetOwnerClass() : Class;
			const TCHAR*    Section   = BaseClass->GetPathName();
			const TCHAR*    Key       = It->ArrayDim==1 ? It->GetName() : TempKey;
			UArrayProperty* Array     = Cast<UArrayProperty>(It);
			UMapProperty*   Map       = Cast<UMapProperty>(It);

			debugf(NAME_DevSave,TEXT("   Loading value for %s from [%s]"), Key, Section);

			if( Array )
			{
				TMultiMap<FName,FString>* Sec = GConfig->GetSectionPrivate( Section, 0, 1, *Class->ClassConfigName );
				if( Sec )
				{
					return 1;
				}
			}
			else if( Map )
			{
				TMultiMap<FName,FString>* Sec = GConfig->GetSectionPrivate( Section, 0, 1, *Class->ClassConfigName );
				if( Sec )
				{
					return 1;
				}
			}
			else for( INT i=0; i<It->ArrayDim; i++ )
			{
				if( It->ArrayDim!=1 )
				{
					appSprintf( TempKey, TEXT("%s[%i]"), It->GetName(), i );
					debugf(NAME_DevSave, TEXT("   Loading value for %s from [%s]"), Key, Section);
				}
				if( GConfig->GetString( Section, Key, Value, ARRAY_COUNT(Value), *Class->ClassConfigName ) )
				{
					return 1;
				}					
			}
		}
	}
	return 0;

	unguard;
}

/* epic ===============================================
* UObject::UsesLocalizedEntries
* Author: sz
*
* Iterate through all properties for the class and see
* if a localized config entry is needed for any of them.
*
* =====================================================
*/
UBOOL UObject::UsesLocalizedEntries( UClass* Class )
{
	guard(UsesLocalizedEntries);
	check(Class);

	if( !(Class->ClassFlags & CLASS_Localized) )
		return 0;

	// not implemented

	return 1;

	unguard;
}

static void LoadLocalizedProp( UProperty* Prop, const TCHAR *IntName, const TCHAR *SectionName, const TCHAR *KeyPrefix, BYTE* Data, TCHAR* Buffer, INT Size);
static void LoadLocalizedStruct( UStruct* Struct, const TCHAR *IntName, const TCHAR *SectionName, const TCHAR *KeyPrefix, BYTE* Data );
static void LoadLocalizedDynamicArray( UArrayProperty *Prop, const TCHAR* IntName, const TCHAR* SectionName, const TCHAR* KeyPrefix, BYTE* Data );

//scion ===========================================================
//	SCION Function: LoadLocalizedProp
//	Author: superville
//
//	Description: Recurse down to the base level of the property
//		and load localized value from the correct language folder
//		Then import it into the property
//
//	Input:	UProperty*		Prop		- Property being loaded (could be a struct or a field)
//			const TCHAR*	IntName		- Name of localization file to load from
//			const TCHAR*	SectionName	- Name of section to load variable from
//			const TCHAR*	KeyPrefix	- Name of variable to load value of
//			BYTE*			Data		- Starting address of this properties data (used to load retrieved value)
//			TCHAR*			Buffer		- Buffer to use for the result
//			INT				Size		- Size of the buffer
//
//	Notes:
//	Last Modified: 01/15/04 (superville)
// ================================================================
static void LoadLocalizedProp
(
	UProperty*		Prop,
	const TCHAR*	IntName,
	const TCHAR*	SectionName,
	const TCHAR*	KeyPrefix,
	BYTE*			Data,
	TCHAR*			Buffer,
	INT				Size
)
{
	guard(LoadLocalizedProp);

	// Try casting the property to a struct
	UStructProperty* StructProperty = Cast<UStructProperty>( Prop );
	// If property is a struct
	if( StructProperty )
	{
		// Recurse into the struct to find the localized data
        LoadLocalizedStruct(StructProperty->Struct, IntName, SectionName, KeyPrefix, Data);
        return;
    }

	// Try casting the property to a dyanmic array
	UArrayProperty* ArrayProperty = Cast<UArrayProperty>( Prop );
	// If property is a dynamic array
	if( ArrayProperty ) {
		// If the property is not localized - exit
		/* rjp struct localization */
		if( !Prop->IsLocalized() )
			return;

		/* rjp struct localization
		if( !ArrayProperty->Inner->IsA( UStrProperty::StaticClass() ) ) {
			warnf( NAME_Log, TEXT("Non-String Dynamic arrays (%s) cannot be localized!"), ArrayProperty->GetPathName() );
			return;
		}
		*/

		// Load each item in the array - this actually imports the text into the property
		LoadLocalizedDynamicArray( ArrayProperty, IntName, SectionName, KeyPrefix, Data );
		return;
	}

	// If the property is not localized - exit
	/* rjp struct localization */
    if( !Prop->IsLocalized() )
        return;

	// Build the full key as <SectionName>.<KeyName>
	// ie GameInfo.DefaultPlayerName
//superville: No longer using section name as prefix for the key
//	FString FullKey = FString(SectionName) + FString(TEXT(".")) + FString(KeyPrefix);
	FString FullKey = FString(KeyPrefix);

	// Load the localized text from the file/section/fullkey
	//scion jg -- Changed to default to the globally set language
	if ( LocalizePropertyValue( SectionName, *FullKey, IntName, Buffer, Size, UObject::GetLanguage(), 1 ) )
	{
		// If a value was retrieved
		// Import it into the property

		/* rjp struct localization */
		Prop->ImportText( Buffer, Data, PPF_LocalizedOnly );
	}

	unguard;
}

//scion ===========================================================
//	SCION Function: LoadLocalizedDynamicArray
//	Author: superville
//
//	Description: Get every mapped entry for the dynamic array from the
//		localization file and import it into the property array
//
//	Input:	UArrayProperty* Prop		- Array property to load
//			const TCHAR*	IntName		- Name of localization file to load from
//			const TCHAR*	SectionName - Name of section to load from
//			const TCHAR*	KeyPrefix	- Variable name to load
//			BYTE*			Data		- Start of the ARRAY property (not the base object)
//
//	Last Modified: 02/04/04 (superville)
// ================================================================
static void LoadLocalizedDynamicArray
(
	UArrayProperty *Prop,
	const TCHAR* IntName,
	const TCHAR* SectionName,
	const TCHAR* KeyPrefix,
	BYTE* Data
)
{
	TCHAR Filename[256];

	appSprintf( Filename, TEXT("%s%s.%s"), UObject::GetLocalizationFolder(), IntName, UObject::GetLanguage() );
		

	// Build the full key as <SectionName>.<KeyName>
	// ie GameInfo.DefaultPlayerName
//superville: No longer using section name as key prefix
//	FString FullKey = FString(SectionName) + FString(TEXT(".")) + FString(KeyPrefix);
	FString FullKey = FString(KeyPrefix);

	// Get the string map of entire section from localization folder
	TMultiMap<FName,FString>* Sec = GConfig->GetSectionPrivate( SectionName, 0, 1, Filename );

	// If couldn't find section in loc folder
	if( !Sec ) 
	{
		// Look again in the default localization folder
		appSprintf( Filename, TEXT("%s%s.%s"), UObject::GetDefaultLocalizationFolder(), IntName, TEXT("int") );
		Sec = GConfig->GetSectionPrivate( SectionName, 0, 1, Filename );
	}
	
	// If the section was found
	if( Sec )
	{
		// Get array of properties
		FArray* Ptr  = (FArray*)(Data + Prop->Inner->Offset);

		TMap<INT,FString> LocValues;
		TArray<FString> List;

		// Find each entry in the section
		Sec->MultiFind( FName(*FullKey), List );

		if( List.Num() > 0 )
		{
			for ( INT i = List.Num() - 1, cnt = 0; i >= 0; i--, cnt++ )
				LocValues.Add(cnt, *List(i));
		}
		else
		{
			// If nothing was found, try searching for indexed keys
			INT Index = 0;
			FString* FoundKey = NULL;

			do
			{
				TCHAR IndexedKey[NAME_SIZE+1];

				// Add array index number to end of key
				appSprintf(IndexedKey, TEXT("%s[%i]"), *FullKey, Index);

				// Try to find value of key
				FoundKey  = Sec->Find(FName(IndexedKey));

				// If found
				if( FoundKey )
				{
					// add to map of loc values
					LocValues.Add(Index, **FoundKey);
				}

				Index++;
			} while( FoundKey || Index < Ptr->Num() );
		}

#define _LOCAL_XXX_
#ifdef _LOCAL_XXX_
		// If we are XXX'n out the characters, go through each item
		// and set each character to X
		// (DO NOT alter strings that are in prechaced properties - ie FontNames)
		if( !(appStricmp(UObject::GetLanguage(), TEXT("xxx"))) &&
			!(Prop->PropertyFlags & CPF_AutoPrecache) )
		{
			for( INT item = 0; item < List.Num(); item++ )
			{
				int strl = List(item).Len();

				for( int i = 0; i < strl; i++ ) 
				{
					List(item)[i] = TCHAR('X');
				}
			}
		}
#endif // _LOCAL_XXX_

		// Get size of each element to help walk the list
		INT     Size = Prop->Inner->ElementSize;

		INT ImportedCount = 0;
		for ( TMap<INT,FString>::TIterator It(LocValues); It; ++It )
		{
			INT ArrayIndex = It.Key();
			const FString& LocString = It.Value();

			// if this index is higher than the number of elements currently in the array (+1),
			// add empty elements to fill the gap
			if ( ArrayIndex >= Ptr->Num() )
			{
				INT AddCount = (ArrayIndex - Ptr->Num()) + 1;
				Ptr->AddZeroed(Size, AddCount);
			}

			// keep track of the highest index we imported - we should remove all pre-existing elements that
			// are located higher than this index
			if ( ArrayIndex + 1 > ImportedCount )
				ImportedCount = ArrayIndex + 1;

			Prop->Inner->ImportText(*LocString, (BYTE*)Ptr->GetData() + ArrayIndex * Size, PPF_LocalizedOnly);
		}

		// this code would remove any pre-existing elements that had a higher index than the highest
		// index we found while importing localized text, but I haven't found any situations where
		// we actually want this to occur....
//		if ( Ptr->Num() > 0 && ImportedCount > 0 && Ptr->Num() > ImportedCount )
//			Ptr->Remove(ImportedCount, Ptr->Num() - ImportedCount, Size);
	}
}

//scion ===========================================================
//	SCION Function: LoadLocalizedStruct
//	Author: superville
//
//	Description: Iterate through each property of the struct and
//		try to load the localized version of it.  The proper Key
//		is generated for nested structs.
//
//	Input:	UStruct*		Struct		- Struct to load
//			const TCHAR*	IntName		- Name of localization file
//			const TCHAR*	SectionName - Name of section to load
//			const TCHAR*	KeyPrefix	- Name of Key to load
//										(NULL if struct is the outer object)
//			BYTE*			Data		- Starting address of THIS struct (not outer obect)
//
//	Notes:	THIS FUNCTION UPDATES DATA TO BE THE START OF THE STRUCT
//			IT WANTS TO LOCALIZE! (Most other functions leave the pointer untouched
//			and pass an aggregate offset)
//
//	Last Modified: 01/15/04 (superville)
// ================================================================
static void LoadLocalizedStruct
(
	UStruct*		Struct,
	const TCHAR*	IntName,
	const TCHAR*	SectionName,
	const TCHAR*	KeyPrefix,
	BYTE*			Data
)
{
	guard(LoadLocalizedStruct);

	// create on the heap
	TCHAR* Buffer = new TCHAR[1800];

	// For each property in the struct
	for (UProperty* Prop = Struct->PropertyLink; Prop; Prop = Prop->PropertyLinkNext)
	{
		// For each entry in that field
	    for( INT i = 0; i < Prop->ArrayDim; i++ )
	    {
    	    FString NewPrefix;

			// If key already exists, get new key ready to append more to it
			if( KeyPrefix )
				NewPrefix = FString::Printf( TEXT("%s."), KeyPrefix );

			// If multiple entries for this field
			if( Prop->ArrayDim > 1 ) 
			{
				// New key is an index into a static array - set the index number
                NewPrefix += FString::Printf( TEXT("%s[%d]"), Prop->GetName(), i );
			}
			else 
			{
				// Otherwise, only one entry
				// Just expand new key to include property (variable) name
                NewPrefix += Prop->GetName();
			}

			// New offset is computed to start of property being localized!
            BYTE* NewData = Data + (Prop->Offset) + (i * Prop->ElementSize );

			// Localized the desired property
            LoadLocalizedProp( Prop, IntName, SectionName, *NewPrefix, NewData, Buffer, 1800 );
	    }
	}

	delete[] Buffer;

	unguard;
}

//
// Load localized text.
// Warning: Must be safe on class-default metaobjects.
//

void UObject::LoadLocalized( UBOOL* bHasLocEntry )
{
	guard(UObject::LoadLocalized);

	UClass* Class = GetClass();
	checkSlow( Class );
	
	if( !(Class->ClassFlags & CLASS_Localized) )
		return;

	if( GIsEditor )
		return;

	if( GetIndex()!=INDEX_NONE && ObjectFlags&RF_PerObjectLocalized && GetOuter()->GetOuter() )
	{
		// Localize as an instanced subobject
	    LoadLocalizedStruct( Class, GetOuter()->GetOuter()->GetName(), GetOuter()->GetName(), GetName(), (BYTE*)this );
	}
	else
	{
		// scion sz - skip if config properties already in defaults
		if( (Class->ClassFlags & CLASS_LocalizedLoaded) && 
			!(Class->ClassFlags & CLASS_PerObjectConfig) )
			return;

		// for packagename, use the class's package name if this object *is* the class,
		UBOOL UseClassPackage = GetIndex()==INDEX_NONE || 
			// or if this class is PerObjectConfig, and it's not an object that was placed in a map
			((Class->ClassFlags & CLASS_PerObjectConfig) &&(GetOuter() == GetTransientPackage() ||
															GetOuter()->IsA(UClass::StaticClass())));		

		const TCHAR* IntName     = UseClassPackage ? Class->GetOuter()->GetName()	: GetOuter()->GetName();
		const TCHAR* SectionName = GetIndex()==INDEX_NONE ? Class->GetName()		: GetName();
		LoadLocalizedStruct( Class, IntName, SectionName, NULL, (BYTE*)this );
	}
	unguardobj;
}

// When set, SaveConfig writes only these properties (SAVECONFIGKEYS, UC2SaveConfigKeys below).
static const TArray<UProperty*>* GSaveConfigOnly = NULL;

// Set while script's SaveConfig() / StaticSaveConfig() run (UC2SaveConfigFromScript below): a globalconfig
// property that another class deliberately sets is not written, and its name is collected here.
static UBOOL GSaveConfigSkipSetByOther = 0;
static FString GSaveConfigNotWritten;

// A globalconfig property is read from the section of the class that declares it, for every subclass of that class,
// so a value written there replaces what any such class's defaultproperties say (a saved Engine.PlayerController
// DesiredFOV=85 would override UCPlayerController's 90).
// When a class default has loaded (UClass::Link / UClass::Serialize, after LoadConfig, so ini keys are applied and
// the defaults are still the ones script's defaultproperties made), UC2NoteDefaultOverrides records each inherited
// globalconfig property whose default differs from the superclass's: the class sets it on purpose. A script save
// leaves such a property out (UC2SetByAnotherClass). Recorded at load because later the two cannot be told apart: a
// subclass that did not see a change script made to its parent's default (Handedness after SetHand) differs too.
// One record per config property a class default holds a value of its own for, taken when the class default has loaded
// its config: the properties it declares (bOverride 0: no superclass value to compare with later) and the inherited ones
// it sets differently from its superclass (bOverride 1: its defaultproperties made it so). Text is the value then
// (UProperty::ExportText, compared with the same call later). Never freed (the static destruction order).
struct FUC2ConfigRecord { UClass* Class; UProperty* Prop; INT Index; UBOOL bOverride; FString Text; };
static TArray<FUC2ConfigRecord>* GConfigRecords = NULL;

void UC2NoteDefaultOverrides( UClass* Class )
{
	// (not skipped under GIsEditor: ucc sets it for its commandlets, and the records are cheap)
	if( !Class->Defaults.Num() )
		return;
	UClass* Super = Class->GetSuperClass();
	const BYTE* Mine = (const BYTE*)Class->GetDefaultObject();
	const BYTE* Parent = (Super && Super->Defaults.Num()) ? (const BYTE*)Super->GetDefaultObject() : NULL;
	for( UProperty* It = Class->PropertyLink; It; It = It->PropertyLinkNext )
	{
		if( !(It->PropertyFlags & CPF_Config) || Cast<UArrayProperty>(It) || Cast<UMapProperty>(It) )
			continue;
		UBOOL bDeclared = It->GetOwnerClass()==Class;
		if( !bDeclared && !Parent )
			continue;
		for( INT i=0; i<It->ArrayDim; i++ )
		{
			INT Ofs = It->Offset + i*It->ElementSize;
			if( !bDeclared && It->Identical( Mine + Ofs, Parent + Ofs ) )
				continue;
			// ExportText writes into the 1024-character buffer SaveConfig uses too: leave out a longer string
			if( Cast<UStrProperty>(It) && ((FString*)(Mine + Ofs))->Len() > 900 )
				continue;
			TCHAR Text[1024]=TEXT("");
			It->ExportText( i, Text, (BYTE*)Mine, (BYTE*)Mine, 0 );
			if( !GConfigRecords )
				GConfigRecords = new TArray<FUC2ConfigRecord>;
			FUC2ConfigRecord& R = *new(*GConfigRecords) FUC2ConfigRecord;
			R.Class = Class;
			R.Prop = It;
			R.Index = i;
			R.bOverride = !bDeclared;
			R.Text = Text;
		}
	}
}

// Does a class other than this object's own (a descendant of the declaring class) deliberately set Prop?
static UBOOL UC2SetByAnotherClass( UObject* Object, UProperty* Prop )
{
	if( !GConfigRecords )
		return 0;
	UClass* Owner = Prop->GetOwnerClass();
	for( INT i=0; i<GConfigRecords->Num(); i++ )
	{
		const FUC2ConfigRecord& R = (*GConfigRecords)(i);
		if( R.bOverride && R.Prop==Prop && R.Class!=Object->GetClass() && R.Class->IsChildOf(Owner) )
			return 1;
	}
	return 0;
}

// Set while a class default is saved for script (StaticSaveConfig): write only what changed. A whole-class dump
// writes every config property at its current value, which for the ones nobody touched is the compiled default: the ini
// would then hold a stale copy of a script default, shadowing the next build's (a User.ini lives long, on the web too).
// A property is written when its value differs from the ini's (key present), or, for one the class inherits,
// from the superclass default's (key absent); one the class declares itself, with no key in the ini, from the value
// the class default had when it loaded (UC2NoteDefaultOverrides). Arrays and maps are not compared and not written.
static UBOOL GSaveConfigChangedOnly = 0;

static UBOOL UC2ConfigValueChanged( UObject* Object, UProperty* Prop, INT Index, const TCHAR* Section, const TCHAR* Key, const TCHAR* Filename )
{
	BYTE* Current = (BYTE*)Object + Prop->Offset + Index*Prop->ElementSize;
	TCHAR IniText[1024];
	if( GConfig->GetString( Section, Key, IniText, ARRAY_COUNT(IniText), Filename ) )
	{
		BYTE* Scratch = (BYTE*)appAlloca( Prop->ElementSize );
		appMemzero( Scratch, Prop->ElementSize );
		Prop->ImportText( IniText, Scratch, 0 );
		UBOOL Same = Prop->Identical( Scratch, Current );
		Prop->DestroyValue( Scratch );
		return !Same;
	}
	// a value of its own the class default had when it loaded: its declared property, or an inherited one it overrides
	for( INT i=0; GConfigRecords && i<GConfigRecords->Num(); i++ )
	{
		const FUC2ConfigRecord& R = (*GConfigRecords)(i);
		if( R.Class==Object->GetClass() && R.Prop==Prop && R.Index==Index )
		{
			TCHAR Text[1024]=TEXT("");
			Prop->ExportText( Index, Text, (BYTE*)Object, (BYTE*)Object, 0 );
			return appStrcmp( Text, *R.Text )!=0;
		}
	}
	// declared here but never recorded: no baseline. Inherited and not overridden: the superclass default's value.
	UClass* Super = Object->GetClass()->GetSuperClass();
	if( Prop->GetOwnerClass()==Object->GetClass() || !Super || !Super->Defaults.Num() )
		return 0;
	return !Prop->Identical( (BYTE*)Super->GetDefaultObject() + Prop->Offset + Index*Prop->ElementSize, Current );
}

//
// Save configuration.
//warning: Must be safe on class-default metaobjects.
//!!may benefit from hierarchical propagation, deleting keys that match superclass...not sure what's best yet.
//
void UObject::SaveConfig( DWORD Flags, const TCHAR* InFilename )
{
	guard(UObject::SaveConfig);

// rjp - removed, as it violates TCR's
// Allow the save & flush in non-final release, for compatibility with vertex shader code
#ifndef FINAL_RELEASE

	UBOOL PerObject = ((GetClass()->ClassFlags & CLASS_PerObjectConfig) && GetIndex()!=INDEX_NONE);
	const TCHAR* Filename
	=	InFilename
	?	InFilename
	:	(PerObject && Outer!=GObjTransientPkg)
	?	GetOuter()->GetName()
	:	*GetClass()->ClassConfigName;
	//epic moelfke: use PropertyLink instead of a slower TFieldIterator
	for (UProperty* It = GetClass()->PropertyLink; It; It = It->PropertyLinkNext)
	{
		if( GSaveConfigOnly && GSaveConfigOnly->FindItemIndex(It)==INDEX_NONE )	// uc2: SAVECONFIGKEYS
			continue;
		if( (It->PropertyFlags & Flags)==Flags )
		{
			if( GSaveConfigSkipSetByOther && !GSaveConfigChangedOnly && (It->PropertyFlags & CPF_GlobalConfig) && UC2SetByAnotherClass(this,It) )
			{
				GSaveConfigNotWritten += FString(GSaveConfigNotWritten.Len() ? TEXT(", ") : TEXT("")) + It->GetName();
				continue;
			}
			TCHAR TempKey[256], Value[1024]=TEXT("");
			const TCHAR*    Key     = It->GetName();
			const TCHAR*    Section = PerObject ? GetName() : (It->PropertyFlags & CPF_GlobalConfig) ? It->GetOwnerClass()->GetPathName() : GetClass()->GetPathName();
			UArrayProperty* Array   = Cast<UArrayProperty>(It);
			UMapProperty*   Map     = Cast<UMapProperty>(It);
			if( GSaveConfigChangedOnly && (Array || Map) )	// uc2
				continue;
			if( Array )
			{
				TMultiMap<FName,FString>* Sec = GConfig->GetSectionPrivate( Section, 1, 0, Filename );
				check(Sec);
				Sec->Remove( FName(Key) );
				FArray* Ptr  = (FArray*)((BYTE*)this + It->Offset);
				INT     Size = Array->Inner->ElementSize;
				for( INT i=0; i<Ptr->Num(); i++ )
				{
					TCHAR Buffer[1024]=TEXT("");
					BYTE* Dest = (BYTE*)Ptr->GetData() + i*Size;
					Array->Inner->ExportTextItem( Buffer, Dest, Dest, 0 );
					Sec->Add( FName(Key), Buffer );
				}
			}
			else if( Map )
			{
				TMultiMap<FName,FString>* Sec = GConfig->GetSectionPrivate( Section, 1, 0, Filename );
				check(Sec);
				Sec->Remove( FName(Key) );
				//FArray* Ptr  = (FArray*)((BYTE*)this + It->Offset);
				//INT     Size = Array->Inner->ElementSize;
				//for( INT i=0; i<Ptr->Num(); i++ )
				//{
				//	TCHAR Buffer[1024]="";
				//	BYTE* Dest = (BYTE*)Ptr->GetData() + i*Size;
				//	Array->Inner->ExportTextItem( Buffer, Dest, Dest, 0 );
				//	Sec->Add( Key, Buffer );
				//}
			}
			else for( INT Index=0; Index<It->ArrayDim; Index++ )
			{
				if( It->ArrayDim!=1 )
				{
					appSprintf( TempKey, TEXT("%s[%i]"), It->GetName(), Index );
					Key = TempKey;
				}
				if( GSaveConfigChangedOnly )	// uc2: StaticSaveConfig() from script
				{
					if( !UC2ConfigValueChanged( this, It, Index, Section, Key, Filename ) )
						continue;
					if( GSaveConfigSkipSetByOther && (It->PropertyFlags & CPF_GlobalConfig) && UC2SetByAnotherClass(this,It) )
					{
						GSaveConfigNotWritten += FString(GSaveConfigNotWritten.Len() ? TEXT(", ") : TEXT("")) + It->GetName();
						continue;
					}
				}
				It->ExportText( Index, Value, (BYTE*)this, (BYTE*)this, 0 );
				GConfig->SetString( Section, Key, Value, Filename );
			}
		}
	}
	GConfig->Flush( 0 );

// But never allow this code, as it has too many side effects to allow FINAL_RELEASE builds to have different behavior
#if 0
	UClass* BaseClass;
	for( BaseClass=GetClass(); BaseClass->GetSuperClass(); BaseClass=BaseClass->GetSuperClass() )
		if( !(BaseClass->GetSuperClass()->ClassFlags & CLASS_Config) )
			break;
	if( BaseClass )
		for( TObjectIterator<UClass> It; It; ++It )
			if( It->IsChildOf(BaseClass) )
				It->GetDefaultObject()->LoadConfig( 1 );//!!propagate??
#endif	// 0
#endif	// FINAL_RELEASE

	unguardobj;
}

//
// Reset configuration.
//
void UObject::ResetConfig( UClass* Class )
{
	guard(UObject::ResetConfig);
	const TCHAR* SrcFilename=NULL;
	if( Class->ClassConfigName==NAME_System )
		SrcFilename = TEXT("Default.ini");
	else if( Class->ClassConfigName==NAME_User )
	{
		SrcFilename = TEXT("DefUser.ini");
	}
	else if (Class->ClassConfigName != NAME_None)
	{
		//scion rwd - allow reset of any object
		SrcFilename = *(Class->ClassConfigName);
	}
	else
		return;
	//scion rwd - force a reload of the file in question
	GConfig->UnloadFile(*FString::Printf(TEXT("%s.ini"),SrcFilename));
	//scion rwd - i have no idea what the point of this was, so it's nuked :)
	/*
	TCHAR Buffer[32000];
	if( GConfig->GetSection( Class->GetPathName(), Buffer, ARRAY_COUNT(Buffer), SrcFilename  ) )
	{
		TCHAR* NewKey;
		for( TCHAR* Key=Buffer; *Key; Key=NewKey )
		{	
			NewKey = Key + appStrlen(Key) + 1;
			TCHAR* Value=appStrstr(Key,TEXT("="));
			if( Value )
			{
				*Value++ = 0;
				GConfig->SetString( Class->GetPathName(), Key, Value, *Class->ClassConfigName );
			}
		}
	}
	*/
	for( TObjectIterator<UClass> ItC; ItC; ++ItC )
		if( ItC->IsChildOf(Class) )
			ItC->GetDefaultObject()->LoadConfig( 1, *ItC, SrcFilename );//!!propagate?
	for( TObjectIterator<UObject> ItO; ItO; ++ItO )
		if( ItO->IsA(Class) )
			{ItO->LoadConfig( 1, Class, SrcFilename ); ItO->PostEditChange();}//!!propagate?
	unguard;
}

/*-----------------------------------------------------------------------------
	Mo Functions.
-----------------------------------------------------------------------------*/

//
// Object accessor.
//
UObject* UObject::GetIndexedObject( INT Index )
{
	guardSlow(UObject::GetIndexedObject);
	if( Index>=0 && Index<GObjObjects.Num() )
		return GObjObjects(Index);
	else
		return NULL;
	unguardSlow;
}

/**
 * Fast version of StaticFindObject that relies on the passed in FName being the object name 
 * without any group/ package qualifiers.
 *
 * @param	InClass		The to be found object's class
 * @param	InOuter		The to be found object's outer
 * @param	InName		The to be found object's name
 * @param	ExactClass	Whether to require an exact match with the passed in class
 * @param	AnyPackage	Whether to look in any package
 * @return	Returns a pointer to the found object or NULL if none could be found
 */
UObject* UObject::StaticFindObjectFast( UClass* InClass, UObject* InOuter, FName InName, UBOOL ExactClass, UBOOL AnyPackage )
{
	guard(UObject::StaticFindObjectFast);
	//jg -- If they specified an outer use that during the hashing
	if (InOuter != NULL)
	{
		// Find in the specified package.
		INT iHash = GetObjectOuterHash(InName,(DWORD)(PTRINT)InOuter);
		for( UObject* Hash = GObjHashOuter[iHash]; Hash != NULL; Hash = Hash->HashOuterNext )
		{
			/*
			InName: the object name to search for. Two possibilities.
				A = No dots. ie: 'S_Actor', a texture in Engine
				B = Dots. ie: 'Package.Name' or 'Package.Group.Name', or an even longer chain of outers. The first one needs to be relative to InObjectPackage.
				I'll define InName's package to be everything before the last period, or "" otherwise.
			InObjectPackage: the package or Outer to look for the object in. Can be ANY_PACKAGE, or NULL. Three possibilities:
				A = Non-null. Search for the object relative to this package.
				B = Null. We're looking for the object, as specified exactly in InName.
				C = ANY_PACKAGE. Search anywhere for the package (resrictions on functionality, see below)
			ObjectPackage: The package we need to be searching in. NULL means we don't care what package to search in
				InName.A &&  InObjectPackage.C ==> ObjectPackage = NULL
				InName.A && !InObjectPackage.C ==> ObjectPackage = InObjectPackage
				InName.B &&  InObjectPackage.C ==> ObjectPackage = InName's package
				InName.B && !InObjectPackage.C ==> ObjectPackage = InName's package, but as a subpackage of InObjectPackage
			*/
			if
			(	(Hash->GetFName()==InName)
				/*If there is no package (no InObjectPackage specified, and InName's package is "")
				and the caller specified any_package, then accept it, regardless of its package.*/
				&&	(	(!InOuter && AnyPackage)
				/*Or, if the object is directly within this package, (whether that package is non-NULL or not),
				then accept it immediately.*/
				||	(Hash->Outer == InOuter)
				/*Or finally, if the caller specified any_package, but they specified InName's package,
				then check if the object IsIn (is recursively within) InName's package.
				We don't check if IsIn if ObjectPackage is NULL, since that would be true for every object.
				This check also allows you to specify Package.Name to access an object in Package.Group.Name.*/
			||	(AnyPackage && InOuter && Hash->IsIn(InOuter)) )
			&&	(InClass==NULL || (ExactClass ? Hash->GetClass()==InClass : Hash->IsA(InClass))) )
			{
				return Hash;
			}
		}
	}
	//jg -- It's possible that the object wasn't correctly found by it's outer
	// name index hash. This seems to happen when a script class does not fully
	// qualify the path name to the object it is trying to load. In this case
	// or if no outer was specified, go through the slower hash

	// Find in the specified package.
	INT iHash = GetObjectHash( InName );
	for( UObject* Hash=GObjHash[iHash]; Hash!=NULL; Hash=Hash->HashNext )
	{
		/*
		InName: the object name to search for. Two possibilities.
			A = No dots. ie: 'S_Actor', a texture in Engine
			B = Dots. ie: 'Package.Name' or 'Package.Group.Name', or an even longer chain of outers. The first one needs to be relative to InObjectPackage.
			I'll define InName's package to be everything before the last period, or "" otherwise.
		InObjectPackage: the package or Outer to look for the object in. Can be ANY_PACKAGE, or NULL. Three possibilities:
			A = Non-null. Search for the object relative to this package.
			B = Null. We're looking for the object, as specified exactly in InName.
			C = ANY_PACKAGE. Search anywhere for the package (resrictions on functionality, see below)
		ObjectPackage: The package we need to be searching in. NULL means we don't care what package to search in
			InName.A &&  InObjectPackage.C ==> ObjectPackage = NULL
			InName.A && !InObjectPackage.C ==> ObjectPackage = InObjectPackage
			InName.B &&  InObjectPackage.C ==> ObjectPackage = InName's package
			InName.B && !InObjectPackage.C ==> ObjectPackage = InName's package, but as a subpackage of InObjectPackage
		*/
		if
		(	(Hash->GetFName()==InName)
			/*If there is no package (no InObjectPackage specified, and InName's package is "")
			and the caller specified any_package, then accept it, regardless of its package.*/
			&&	(	(!InOuter && AnyPackage)
			/*Or, if the object is directly within this package, (whether that package is non-NULL or not),
			then accept it immediately.*/
			||	(Hash->Outer == InOuter)
			/*Or finally, if the caller specified any_package, but they specified InName's package,
			then check if the object IsIn (is recursively within) InName's package.
			We don't check if IsIn if ObjectPackage is NULL, since that would be true for every object.
			This check also allows you to specify Package.Name to access an object in Package.Group.Name.*/
		||	(AnyPackage && InOuter && Hash->IsIn(InOuter)) )
		&&	(InClass==NULL || (ExactClass ? Hash->GetClass()==InClass : Hash->IsA(InClass))) )
		{
			return Hash;
	}
	}
	// Not found.
	return NULL;
	unguard;
}

//
// Find an optional object.
//
UObject* UObject::StaticFindObject( UClass* ObjectClass, UObject* InObjectPackage, const TCHAR* InName, UBOOL ExactClass )
{
	guard(UObject::StaticFindObject);

	// Resolve the object and package name.
	UObject* ObjectPackage = InObjectPackage!=ANY_PACKAGE ? InObjectPackage : NULL;
	TCHAR ResolvedName[1024];
	if( !ResolveName( ObjectPackage, InName, ResolvedName, 0, 0 ) )
		return NULL;

	// Make sure it's an existing name.
	FName ObjectName(ResolvedName,FNAME_Find);
	if( ObjectName==NAME_None )
		return NULL;

	return StaticFindObjectFast( ObjectClass, ObjectPackage, ObjectName, ExactClass, InObjectPackage==ANY_PACKAGE );
	unguard;
}

//
// Find an object; can't fail.
//
UObject* UObject::StaticFindObjectChecked( UClass* ObjectClass, UObject* ObjectParent, const TCHAR* InName, UBOOL ExactClass )
{
	guard(UObject::StaticFindObjectChecked);
	UObject* Result = StaticFindObject( ObjectClass, ObjectParent, InName, ExactClass );
	if( !Result )
		appErrorf( LocalizeError(TEXT("ObjectNotFound"),TEXT("Core")), ObjectClass->GetName(), ObjectParent==ANY_PACKAGE ? TEXT("Any") : ObjectParent ? ObjectParent->GetName() : TEXT("None"), InName );
	return Result;
	unguard;
}

//
// Binary initialize object properties to zero or defaults.
//
void UObject::InitProperties( BYTE* Data, INT DataCount, UClass* DefaultsClass, BYTE* Defaults, INT DefaultsCount, UObject* DestObject, UObject* SuperObject )
{
	guard(UObject::InitProperties);
	check(DataCount>=sizeof(UObject));
	INT Inited = sizeof(UObject);
	UBOOL Template = Defaults != NULL;

	// Find class defaults if no template was specified.
	//warning: At startup, DefaultsClass->Defaults.Num() will be zero for some native classes.
	guardSlow(FindDefaults);
	if( !Defaults && DefaultsClass && DefaultsClass->Defaults.Num() )
	{
		Defaults      = &DefaultsClass->Defaults(0);
		DefaultsCount =  DefaultsClass->Defaults.Num();
	}
	unguardSlow;

	// DELTA DEFAULTS
	if( !Template && DefaultsClass && DefaultsClass->ClassFlags & CLASS_TaggedDefaults )
	{
		appMemzero( Data + Inited, DataCount - Inited );
		UObject::BeginLoad();
		DefaultsClass->SerializeTaggedDefaults( Data, NULL );
		UObject::EndLoad();
	}
	else
	{
		// Copy defaults.
		guardSlow(DefaultsFill);
		if( Defaults )
		{
			checkSlow(DefaultsCount>=Inited);
			checkSlow(DefaultsCount<=DataCount);
			appMemcpy( Data+Inited, Defaults+Inited, DefaultsCount-Inited );
			Inited = DefaultsCount;
		}
		unguardSlow;

		// Zero-fill any remaining portion.
		guardSlow(ZeroFill);
		if( Inited < DataCount )
			appMemzero( Data+Inited, DataCount-Inited );
		unguardSlow;
	}

	// This is a duplicate. Set all transients to their default value.
	// DELTA DEFAULTS
	if( SuperObject && !(DefaultsClass->ClassFlags & CLASS_TaggedDefaults ) )
	{
		checkSlow(DestObject);
		BYTE* ClassDefaults = &DefaultsClass->Defaults(0);
		//epic moelfke: use PropertyLink instead of a slower TFieldIterator
		for (UProperty* It = DestObject->GetClass()->PropertyLink; It; It = It->PropertyLinkNext)
		{
			if( It->PropertyFlags & CPF_Transient )
			{
				/*  rjp -- using appMemcpy causes problems for bools, e.g.
				4 UBOOL's packed together, where the last bool of the pack is transient.
				When objects are instanced, values of first three bools would be overwritten
				by the appMemcpy of the last bool (more than likely with 0)
				*/
				if (Cast<UBoolProperty>(It, CLASS_IsAUBoolProperty))
					It->CopyCompleteValue(Data + It->Offset, ClassDefaults + It->Offset, NULL);
				else
					appMemcpy( Data + It->Offset, ClassDefaults + It->Offset, It->ArrayDim*It->ElementSize );
			}
		}
	}

	// Construct anything required.
	guardSlow(ConstructProperties);
	if( DefaultsClass )
	{
		for( UProperty* P=DefaultsClass->ConstructorLink; P; P=P->ConstructorLinkNext )
		{
			// This is the object that will be passed to our inner properties as the SuperObject
			UObject* NewSuper = SuperObject ? SuperObject : DestObject;

			/* ron --
			P->CopyCompleteValue( Data + P->Offset, Defaults + P->Offset, SuperObject ? SuperObject : DestObject );

			fix for nesting inline objects
			ObjectA contains instanced ObjectB, which contains instanced ObjectC -
			pass ObjectB in as ObjectC's SuperObject, rather than ObjectA

			DestObject + SuperObject indicates that we are instancing a new object (DestObject) within another 
			object (SuperObject) using an inline subobject definition
			For any object properties (or arrays of object properties) of this class, pass in DestObject
			rather than SuperObject, so that delegates can bind to the instanced object, rather than the default
			inline subobject
			-- ron */

			if ( NewSuper && DestObject && NewSuper != DestObject )
			{
				// If this is an object property, use the instanced object as the object property's SuperObject
				if ( Cast<UObjectProperty>(P,CLASS_IsAUObjectProperty) )
					NewSuper = DestObject;
				else
				{
					// Likewise if this is an array of objects
					UArrayProperty* ArrayProp = Cast<UArrayProperty>(P);
					if ( ArrayProp && Cast<UObjectProperty>(ArrayProp->Inner,CLASS_IsAUObjectProperty) )
						NewSuper = DestObject;
				}
			}

			// DELTA DEFAULTS
			// Duplicate exportobject properties from themselves, as we can't do it from the tagged defaults.
			if( !Template && ((DefaultsClass->ClassFlags & CLASS_TaggedDefaults) && (P->PropertyFlags & CPF_ExportObject|CPF_NeedCtorLink)) )
			{
				// !! Might be slow, use a static buffer?
				BYTE* DestTmp = (BYTE*)appAlloca( P->ElementSize * P->ArrayDim );
				appMemzero( DestTmp, P->ElementSize * P->ArrayDim );
				P->CopyCompleteValue( DestTmp, Data + P->Offset, NewSuper );
				appMemcpy( Data + P->Offset, DestTmp, P->ElementSize * P->ArrayDim );
			}
			else if( !(DefaultsClass->ClassFlags & CLASS_TaggedDefaults) || Template )
			{
				if( P->Offset < DefaultsCount )
				{
					appMemzero( Data + P->Offset, P->GetSize() );//bad for bools, but not a real problem because they aren't constructed!!
					P->CopyCompleteValue( Data + P->Offset, Defaults + P->Offset, NewSuper );
				}
			}
		}
	}
	unguardSlow;

	unguard;
}

//
// Destroy properties.
//
void UObject::ExitProperties( BYTE* Data, UClass* Class )
{
	UProperty* P=NULL;
	guard(UObject::ExitProperties);
	for( P=Class->ConstructorLink; P; P=P->ConstructorLinkNext )
	{
		// Only destroy values of properties which have been loaded completely.
		// This can be encountered by loading a package in ucc make which references a class which has already been compiled and saved.
		// The class is already in memory, so when the class is loaded from the package on disk, it reuses the memory address of the existing class with the same name.
		// Before reusing the memory address, it destructs the default properties of the existing class.
		// However, at this point, the properties may have been reallocated but not deserialized, leaving them in an invalid state.
		if(!(P->GetFlags() & RF_NeedLoad))
		{
			P->DestroyValue( Data + P->Offset );
		}
		else
		{
			check(GIsUCC || GIsEditor);
		}
	}
	unguardf(( TEXT("(%s)"), P->GetFullName() ));
}

//
// Init class default object.
//
void UObject::InitClassDefaultObject( UClass* InClass, UBOOL SetOuter, UBOOL SkipInit )
{
	guard(UObject::InitClassDefaultObject);

	// Init UObject portion.
	appMemset( this, 0, sizeof(UObject) );
	*(void**)this = *(void**)InClass;
	Class         = InClass;
	Index         = INDEX_NONE;

	// Init post-UObject portion.
	if( SetOuter )
		Outer = InClass->GetOuter();

	// DELTA DEFAULTS
	if( !SkipInit )
		InitProperties( (BYTE*)this, InClass->GetPropertiesSize(), InClass->GetSuperClass(), NULL, 0, SetOuter ? this : NULL );		

	unguard;
}

//
// Global property setting.
//
void UObject::GlobalSetProperty( const TCHAR* Value, UClass* Class, UProperty* Property, INT Offset, UBOOL Immediate )
{
	guard(UObject::GlobalSetProperty);

	// Apply to existing objects of the class, with notification.
	if( Immediate )
	{
		for( FObjectIterator It; It; ++It )
		{
			if( It->IsA(Class) )
			{
				Property->ImportText( Value, (BYTE*)*It + Offset, PPF_Localized );
				It->PostEditChange();
			}
		}
	}

	// DELTA DEFAULTS
	if( Class->ClassFlags & CLASS_TaggedDefaults )
		Class->ExpandDefaultObject();

	// Apply to defaults.
	Property->ImportText( Value, Class->GetDefaultsSafe(Offset), PPF_Localized );

#ifndef _XBOX
	if ( GIsEditor )
		Class->GetDefaultObject()->SaveConfig();
#endif

	unguard;
}

/*-----------------------------------------------------------------------------
	Object registration.
-----------------------------------------------------------------------------*/

//
// Preregister an object.
//warning: Sometimes called at startup time.
//
void UObject::Register()
{
	guard(UObject::Register);
	check(GObjInitialized);

	// Get stashed registration info.
	const TCHAR* InOuter = *(const TCHAR**)&Outer;

#if SERIAL_POINTER_INDEX
	const TCHAR* InName  = *(const TCHAR**)&StateFrame;
	StateFrame   = NULL;
	_Linker      = NULL;
#else
	const TCHAR* InName  = *(const TCHAR**)&Name;
#endif

	// Set object properties.
	Outer        = CreatePackage(NULL,InOuter);
	Name         = InName;
	_LinkerIndex = INDEX_NONE;

#if _DEBUG //scion rwd - only do error checking if in debug
	// Validate the object.
	if( Outer==NULL )
		appErrorf( TEXT("Autoregistered object %s is unpackaged"), GetFullName() );
	if( GetFName()==NAME_None )
		appErrorf( TEXT("Autoregistered object %s has invalid name"), GetFullName() );
	if( StaticFindObject( NULL, GetOuter(), GetName() ) )
		appErrorf( TEXT("Autoregistered object %s already exists"), GetFullName() );
#endif

	// Add to the global object table.
	AddObject( INDEX_NONE );

	unguard;
}

//
// Handle language change.
//
void UObject::LanguageChange()
{
	guard(UObject::LanguageChange);
	LoadLocalized();
	unguard;
}

/*-----------------------------------------------------------------------------
	StaticInit & StaticExit.
-----------------------------------------------------------------------------*/

void SerTest( FArchive& Ar, DWORD& Value, DWORD Max )
{
	DWORD NewValue=0;
	for( DWORD Mask=1; NewValue+Mask<Max && Mask; Mask*=2 )
	{
		BYTE B = ((Value&Mask)!=0);
		Ar.SerializeBits( &B, 1 );
		if( B )
			NewValue |= Mask;
	}
	Value = NewValue;
}

//
// Init the object manager and allocate tables.
//
void UObject::StaticInit()
{
	guard(UObject::StaticInit);
	GObjNoRegister = 1;

	// Checks.
	check(sizeof(BYTE)==1);
	check(sizeof(SBYTE)==1);
	check(sizeof(_WORD)==2);
	check(sizeof(DWORD)==4);
	check(sizeof(QWORD)==8);
	check(sizeof(ANSICHAR)==1);
	check(sizeof(UNICHAR)==2);
	check(sizeof(SWORD)==2);
	check(sizeof(INT)==4);
	check(sizeof(SQWORD)==8);
	check(sizeof(UBOOL)==4);
	check(sizeof(FLOAT)==4);
	check(sizeof(DOUBLE)==8);
	check(ENGINE_MIN_NET_VERSION<=ENGINE_VERSION);

	// Development.
	GCheckConflicts = ParseParam(appCmdLine(),TEXT("CONFLICTS"));
	GNoGC           = ParseParam(appCmdLine(),TEXT("NOGC"));

	// Init hash.
	appMemzero(GObjHash,sizeof(GObjHash));
	appMemzero(GObjHashOuter,sizeof(GObjHashOuter));

	// If statically linked, initialize registrants.
	#if __STATIC_LINK
		AUTO_INITIALIZE_REGISTRANTS;
	#endif
	
	// Note initialized.
	GObjInitialized = 1;

	// Add all autoregistered classes.
	ProcessRegistrants();

	// Allocate special packages.
	GObjTransientPkg = new( NULL, TEXT("Transient") )UPackage;
	GObjTransientPkg->AddToRoot();

	// Load package remap.
	GObjPackageRemap = NEW TMultiMap<FName,FName>;
	GObjPackageRemap->Add( TEXT("UnrealI"), TEXT("UnrealShare") );

	debugf( NAME_Init, TEXT("Object subsystem initialized") );
	unguard;
}

//
// Process all objects that have registered.
//
void UObject::ProcessRegistrants()
{
	guard(UObject::ProcessRegistrants);
	if( ++GObjRegisterCount==1 )
	{
		// Make list of all objects to be registered.
#if SERIAL_POINTER_INDEX
		for( ; GAutoRegister; GAutoRegister=*(UObject **)&GAutoRegister->_Linker )
#else
		for( ; GAutoRegister; GAutoRegister=*(UObject **)&GAutoRegister->_LinkerIndex )
#endif
			GObjRegistrants.AddItem( GAutoRegister );
		for( INT i=0; i<GObjRegistrants.Num(); i++ )
			GObjRegistrants(i)->ConditionalRegister();
		GObjRegistrants.Empty();
		check(!GAutoRegister);
	}
	GObjRegisterCount--;
	unguard;
}

//
// Shut down the object manager.
//
void UObject::StaticExit()
{
	guard(UObject::StaticExit);
	check(GObjLoaded.Num()==0);
	check(GObjRegistrants.Num()==0);
	check(!GAutoRegister);

	// Cleanup root.
	GObjTransientPkg->RemoveFromRoot();

	// Tag all objects as unreachable.
	for( FObjectIterator It; It; ++It )
		It->SetFlags( RF_Unreachable | RF_TagGarbage );

	// Tag all names as unreachable.
	for( INT i=0; i<FName::GetMaxNames(); i++ )
		if( FName::GetEntry(i) )
			FName::GetEntry(i)->Flags |= RF_Unreachable;

	// Purge all objects.
	GExitPurge = 1;
	PurgeGarbage( RF_Unreachable, RF_Native );
	GObjObjects.Empty();

	// Empty arrays to prevent falsely-reported memory leaks.
	GObjLoaded			.Empty();
	GObjObjects			.Empty();
	GObjAvailable		.Empty();
	GObjLoaders			.Empty();
	GObjRoot			.Empty();
	GObjRegistrants		.Empty();
	GObjPreferences		.Empty();
	GObjDrivers			.Empty();
	delete GObjPackageRemap;

	GObjInitialized = 0;
	debugf( NAME_Exit, TEXT("Object subsystem successfully closed.") );
	unguard;
}

/*-----------------------------------------------------------------------------
	UObject Tick.
-----------------------------------------------------------------------------*/

//
// Mark one unit of passing time. This is used to update the object
// caching status. This must be called when the object manager is
// in a clean state (outside of all code which retains pointers to
// object data that was gotten).
//
void UObject::StaticTick()
{
	guard(UObject::StaticTick);
	check(GObjBeginLoadCount==0);

	// Check natives.
	CORE_API extern int GNativeDuplicate;
	if( GNativeDuplicate )
		appErrorf( TEXT("Duplicate native registered: %i"), GNativeDuplicate );

	CORE_API extern int GCastDuplicate;
	if( GCastDuplicate )
		appErrorf( TEXT("Duplicate cast registered: %i"), GCastDuplicate );

	unguard;
}

/*-----------------------------------------------------------------------------
   Shutdown.
-----------------------------------------------------------------------------*/

//
// Make sure this object has been shut down.
//
void UObject::ConditionalShutdownAfterError()
{
	if( !(GetFlags() & RF_ErrorShutdown) )
	{
		SetFlags( RF_ErrorShutdown );
		try
		{
			ShutdownAfterError();
		}
		catch( ... )
		{
			debugf( NAME_Exit, TEXT("Double fault in object ShutdownAfterError") );
		}
	}
}

//
// After a critical error, shutdown all objects which require
// mission-critical cleanup, such as restoring the video mode,
// releasing hardware resources.
//
void UObject::StaticShutdownAfterError()
{
	guard(UObject::StaticShutdownAfterError);
	if( GObjInitialized )
	{
		static UBOOL Shutdown=0;
		if( Shutdown )
			return;
		Shutdown = 1;
		debugf( NAME_Exit, TEXT("Executing UObject::StaticShutdownAfterError") );
		try
		{
			for( INT i=0; i<GObjObjects.Num(); i++ )
				if( GObjObjects(i) )
					GObjObjects(i)->ConditionalShutdownAfterError();
		}
		catch( ... )
		{
			debugf( NAME_Exit, TEXT("Double fault in object manager ShutdownAfterError") );
		}
	}
	unguard;
}

//
// Bind package to DLL.
//warning: Must only find packages in the \Unreal\System directory!
//
void UObject::BindPackage( UPackage* Pkg )
{
	guard(UPackage::BindPackage);
	if( !Pkg->DllHandle && !Pkg->GetOuter() && !Pkg->AttemptedBind )
	{
		FString PathName = FString(appBaseDir()) + Pkg->GetName();	// a path of any length
		Pkg->AttemptedBind  = 1;
		GObjNoRegister      = 0;
		Pkg->DllHandle      = appGetDllHandle( *PathName );
		GObjNoRegister      = 1;
		if( Pkg->DllHandle )
		{
			#if !__STATIC_LINK
			debugf( NAME_Log, TEXT("Bound to %s%s"), Pkg->GetName(), DLLEXT );
			#endif
			ProcessRegistrants();
		}
	}
	unguard;
}



/*---------------------------------------------------------------------------------
   By-name member access functionality.    Useful for non-native object dissection.
-----------------------------------------------------------------------------------*/

UBOOL UObject::FindArrayProperty( FString Name, FArray** Array, INT* ElementSize )
{
	BYTE* Base = (BYTE*) this;
	for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(GetClass()); It; ++It )
	{				
		UArrayProperty* ArrayProperty;
		if( (ArrayProperty=Cast<UArrayProperty>(*It)) != NULL )
		{			
			FString ArrayName = It->GetName();					
			if( ArrayName == Name)
			{						
				*Array = (FArray*)(Base+It->Offset);
				*ElementSize = ArrayProperty->Inner->ElementSize;
				return true;
			}
		}
	}
	return false;
}

UBOOL UObject::FindObjectProperty( FString Name, UObject** Object )
{
	BYTE* Base = (BYTE*) this;
	for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(GetClass()); It; ++It )
	{				
		UObjectProperty* ObjectProperty;
		if( (ObjectProperty=Cast<UObjectProperty>(*It)) != NULL )
		{			
			FString ObjectName = It->GetName();					
			if( ObjectName == Name)
			{						
				*Object = (UObject*)(Base+It->Offset);
				return true;
			}
		}
	}
	return false;
}

UBOOL UObject::FindStructProperty( FString Name, UStruct** Struct )
{
	BYTE* Base = (BYTE*) this;
	for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(GetClass()); It; ++It )
	{				
		UStructProperty* StructProperty;
		if( (StructProperty=Cast<UStructProperty>(*It)) != NULL )
		{			
			FString StructName = It->GetName();					
			if( StructName == Name)
			{						
				*Struct = (UStruct*)(Base+It->Offset);
				return true;
			}
		}
	}
	return false;
}

UBOOL UObject::FindFloatProperty( FString Name, FLOAT* FloatVar )
{
	BYTE* Base = (BYTE*) this;
	for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(GetClass()); It; ++It )
	{				
		UFloatProperty* FloatProperty;
		if( (FloatProperty=Cast<UFloatProperty>(*It)) != NULL )
		{			
			FString FloatName = It->GetName();					
			if( FloatName == Name)
			{						
				*FloatVar = *(FLOAT*)(Base+It->Offset);
				return true;
			}
		}
	}
	return false;
}

UBOOL UObject::FindIntProperty( FString Name, INT* IntVar )
{
	BYTE* Base = (BYTE*) this;
	for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(GetClass()); It; ++It )
	{				
		UIntProperty* IntProperty;
		if( (IntProperty=Cast<UIntProperty>(*It)) != NULL )
		{			
			FString IntName = It->GetName();					
			if( IntName == Name)
			{						
				*IntVar = *(INT*)(Base+It->Offset);
				return true;
			}
		}
	}
	return false;
}

UBOOL UObject::FindBoolProperty( FString Name, UBOOL* BoolVar )
{
	BYTE* Base = (BYTE*) this;
	for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(GetClass()); It; ++It )
	{				
		UBoolProperty* BoolProperty;
		if( (BoolProperty=Cast<UBoolProperty>(*It)) != NULL )
		{			
			FString BoolName = It->GetName();					
			if( BoolName == Name)
			{						
				*BoolVar = *(UBOOL*)(Base+It->Offset);
				return true;
			}
		}
	}
	return false;
}

UBOOL UObject::FindFNameProperty( FString Name, FName* FNameVar )
{	
	BYTE* Base = (BYTE*) this;
	for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(GetClass()); It; ++It )
	{				
		UNameProperty* FNameProperty;
		if( (FNameProperty=Cast<UNameProperty>(*It)) != NULL )
		{			
			FString FNameName = It->GetName();					
			if( FNameName == Name)
			{						
				*FNameVar = *(FName*)(Base+It->Offset);
				return true;
			}
		}
	}
	return false;		
}



/*-----------------------------------------------------------------------------
   Command line.
-----------------------------------------------------------------------------*/

//
// Archive for enumerating other objects referencing a given object.
//
class FArchiveShowReferences : public FArchive
{
public:
	FArchiveShowReferences( FOutputDevice& InAr, UObject* InOuter, UObject* InObj, TArray<UObject*>& InExclude )
	: DidRef( 0 ), Ar( InAr ), Parent( InOuter ), Obj( InObj ), Exclude( InExclude )
	{
		// rjp  --
		// there are several types of objects we don't want listed, for different reasons.
		// Prevent them from being logged by adding them to our Found list before we start
		// serialization, so that they won't be listed

		// quick sanity check
		check(Obj);
		verify(Obj->IsValid());

		// every object we serialize obviously references our package
		Found.AddUniqueItem( Parent );

		// every object we serialize obviously references its linker
		Found.AddUniqueItem(Obj->GetLinker());

		// every object we serialize obviously references its class and its class's parent classes
		for ( UClass* ObjectClass = Obj->GetClass(); ObjectClass; ObjectClass = ObjectClass->GetSuperClass() )
			Found.AddUniqueItem( ObjectClass );

		// similarly, if the object is a class, they all obviously reference their parent classes
		if ( Obj->IsA(UClass::StaticClass()) )
		{
			for ( UClass* ParentClass = Cast<UClass>(Obj)->GetSuperClass(); ParentClass; ParentClass = ParentClass->GetSuperClass() )
				Found.AddUniqueItem( ParentClass );
		}

		// OK, now we're all set to go - let's see what Obj is referencing.
		// -- rjp
		Obj->Serialize( *this );
	}
	UBOOL DidRef;
private:
	FArchive& operator<<( UObject*& RefObj )
	{
		guard(FArchiveShowReferences<<Obj);
		if( RefObj &&									// RefObj will be None, most of the time
			!RefObj->IsIn(Parent) &&					// we don't care about other objects in our own package (or group)
			Found.FindItemIndex(RefObj) == INDEX_NONE )	// don't list the same object as being referenced more than once 
		{
			Found.AddUniqueItem(RefObj);

			INT i;
			for( i=0; i<Exclude.Num(); i++ )
				if ( RefObj->GetOuter() == Exclude(i) )
					break;

			if( i==Exclude.Num() )
			{
				if( !DidRef )
					Ar.Logf( TEXT("   %s references:"), Obj->GetFullName() );
				
				Ar.Logf( TEXT("      %s"), RefObj->GetFullName() );
				DidRef=1;
			}
		}
		return *this;
		unguard;
	}
	FOutputDevice& Ar;
	UObject* Parent;
	UObject* Obj;
	TArray<UObject*>& Exclude;
	TArray<UObject*> Found;
};

//
// Archive for finding who references an object.
//
class FArchiveFindCulprit : public FArchive
{
public:
	FArchiveFindCulprit( UObject* InFind, UObject* Src )
	: Find(InFind), Count(0)
	{
		Src->Serialize( *this );
	}
	INT GetCount()
	{
		return Count;
	}
	FArchive& operator<<( class UObject*& Obj )
	{
		if( Obj==Find )
			Count++;
		return *this;
	}
protected:
	UObject* Find;
	INT Count;
};

//
// Archive for finding shortest path from root to a particular object.
// Depth-first search.
//
struct FTraceRouteRecord
{
	INT Depth;
	UObject* Referencer;
	FTraceRouteRecord( INT InDepth, UObject* InReferencer )
	: Depth(InDepth), Referencer(InReferencer)
	{}
};
class FArchiveTraceRoute : public FArchive
{
public:
	static TArray<UObject*> FindShortestRootPath( UObject* Obj )
	{
		guard(FArchiveTraceRoute::FindShortestRootPath);
		TMap<UObject*,FTraceRouteRecord> Routes;
		FArchiveTraceRoute Rt( Routes );
		TArray<UObject*> Result;
		if( Routes.Find(Obj) )
		{
			Result.AddItem( Obj );
			for( ; ; )
			{
				FTraceRouteRecord* Rec = Routes.Find(Obj);
				if( Rec->Depth==0 )
					break;
				Obj = Rec->Referencer;
				Result.Insert(0);
				Result(0) = Obj;
			}
		}
		return Result;
		unguard;
	}
private:
	FArchiveTraceRoute( TMap<UObject*,FTraceRouteRecord>& InRoutes )
	: Routes(InRoutes), Depth(0), Prev(NULL)
	{
		{for( FObjectIterator It; It; ++It )
			It->SetFlags( RF_TagExp );}
		UObject::SerializeRootSet( *this, RF_Native, 0 );
		{for( FObjectIterator It; It; ++It )
			It->ClearFlags( RF_TagExp );}
	}
	FArchive& operator<<( class UObject*& Obj )
	{
		if( Obj )
		{
			FTraceRouteRecord* Rec = Routes.Find(Obj);
			if( !Rec || Depth<Rec->Depth )
				Routes.Set( Obj, FTraceRouteRecord(Depth,Prev) );
		}
		if( Obj && (Obj->GetFlags() & RF_TagExp) )
		{
			Obj->ClearFlags( RF_TagExp );
			UObject* SavedPrev = Prev;
			Prev = Obj;
			Depth++;
			Obj->Serialize( *this );
			Depth--;
			Prev = SavedPrev;
		}
		return *this;
	}
	TMap<UObject*,FTraceRouteRecord>& Routes;
	INT Depth;
	UObject* Prev;
};

#ifndef FINAL_RELEASE
#ifdef LIN_LOAD
static INT Compare( FString& A, FString& B )
{
	return appStricmp( *A, *B );
}
#endif
//jg -- Hacks for getting a list of objects that are loaded at an arbitrary time
UBOOL GTrackObjectLoads = FALSE;
TArray<FString>* GLoadedObjects = NULL;
#endif

//
// Show the inheretance graph of all loaded classes.
//
static void ShowClasses( UClass* Class, FOutputDevice& Ar, int Indent )
{
	Ar.Logf( TEXT("%s%s"), appSpc(Indent), Class->GetName() );
	for( TObjectIterator<UClass> It; It; ++It )
		if( It->GetSuperClass() == Class )
			ShowClasses( *It, Ar, Indent+2 );
}

//
// Script's SaveConfig() / StaticSaveConfig() (UnCorSc.cpp): UObject::SaveConfig, except that a globalconfig
// property another class sets its own default for is left out (UC2SetByAnotherClass) and logged.
//
void UC2SaveConfigFromScript( UObject* Object, UBOOL bStatic )
{
	guard(UC2SaveConfigFromScript);
	if( GUfrontLog )
		debugf( NAME_Log, TEXT("Script SaveConfig: %s on %s"), bStatic ? TEXT("StaticSaveConfig") : TEXT("SaveConfig"), Object->GetClass()->GetPathName() );
	GSaveConfigSkipSetByOther = 1;
	GSaveConfigChangedOnly = Object->GetIndex()==INDEX_NONE;	// a class default: only what changed; an instance: all
	GSaveConfigNotWritten = TEXT("");
	Object->SaveConfig();
	GSaveConfigSkipSetByOther = 0;
	GSaveConfigChangedOnly = 0;
	if( GSaveConfigNotWritten.Len() )
		debugf( NAME_Log, TEXT("Script SaveConfig: %s: not written, another class sets its own default and globalconfig is shared with it: %s"),
			Object->GetClass()->GetName(), *GSaveConfigNotWritten );
	unguard;
}

//
// SAVECONFIGKEYS <Class> <Property> [<Property>...]
//
// Writes the named config properties of the class's default object to the ini, and only those. Script's
// StaticSaveConfig() writes the whole class, which carries along anything else that was changed in the class
// default at run time (UCPlayerInput's defaults hold the active profile's pad invert and sensitivity, written
// there by the profile). A setting that must persist alone (the mouse settings, UCPlayerInput.MouseLookSensitivity /
// bInvertMouseLook) asks for exactly its keys: ConsoleCommand("SAVECONFIGKEYS UCGameplay.UCPlayerInput <Prop>").
// Each key is written by UObject::SaveConfig itself (same file, section, ExportText, flush). ScriptSaveConfig=False
// answers "off" and writes nothing. Reply: one "SaveConfigKeys:" line per property.
//
UBOOL UC2SaveConfigKeys( const TCHAR* Args, FOutputDevice& Ar )
{
	guard(UC2SaveConfigKeys);
	if( !UC2ScriptSaveConfigEnabled() )
	{
		Ar.Logf( TEXT("SaveConfigKeys: off ([Engine.Engine] ScriptSaveConfig=False): nothing written") );
		return 1;
	}
	TCHAR ClassName[256];
	UClass* SaveClass = NULL;
	if( !ParseToken( Args, ClassName, ARRAY_COUNT(ClassName), 1 ) || (SaveClass=FindObject<UClass>( ANY_PACKAGE, ClassName ))==NULL )
	{
		Ar.Logf( NAME_ExecWarning, TEXT("SaveConfigKeys: unrecognized class %s"), ClassName );
		return 1;
	}
	TArray<UProperty*> Only;
	TCHAR PropertyName[256];
	while( ParseToken( Args, PropertyName, ARRAY_COUNT(PropertyName), 1 ) )
	{
		UProperty* Property = FindField<UProperty>( SaveClass, PropertyName );
		if( !Property )
			Ar.Logf( NAME_ExecWarning, TEXT("SaveConfigKeys: %s has no property %s"), SaveClass->GetName(), PropertyName );
		else if( !(Property->PropertyFlags & CPF_Config) )
			Ar.Logf( NAME_ExecWarning, TEXT("SaveConfigKeys: %s.%s is not a config property"), SaveClass->GetName(), PropertyName );
		else
			Only.AddUniqueItem( Property );
	}
	if( !Only.Num() )
	{
		Ar.Logf( NAME_ExecWarning, TEXT("SaveConfigKeys: usage: SAVECONFIGKEYS <Class> <Property> [<Property>...]") );
		return 1;
	}
	GSaveConfigOnly = &Only;
	SaveClass->GetDefaultObject()->SaveConfig();
	GSaveConfigOnly = NULL;
	for( INT i=0; i<Only.Num(); i++ )
	{
		TCHAR Value[256]=TEXT("");
		Only(i)->ExportText( 0, Value, SaveClass->GetDefaultsSafe(0), SaveClass->GetDefaultsSafe(0), 0 );
		Ar.Logf( TEXT("SaveConfigKeys: %s.%s=%s written to %s.ini"), SaveClass->GetName(), Only(i)->GetName(), Value, *SaveClass->ClassConfigName );
		// a script's ConsoleCommand() returns the reply to the script and logs nothing: with -UFRONTLOG, leave a line for the log
		if( GUfrontLog )
			debugf( NAME_Log, TEXT("SaveConfigKeys: %s.%s=%s written to %s.ini"), SaveClass->GetName(), Only(i)->GetName(), Value, *SaveClass->ClassConfigName );
	}
	return 1;
	unguard;
}

UBOOL UObject::StaticExec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UObject::StaticExec);
	const TCHAR *Str = Cmd;
	if( ParseCommand(&Str,TEXT("SAVECONFIGKEYS")) )
		return UC2SaveConfigKeys( Str, Ar );
	else if( ParseCommand(&Str,TEXT("MEM")) )
	{
		GMalloc->DumpAllocs();
#ifdef USE_GCONTIG
		GContigMalloc->DumpAllocs();
#endif
		return 1;
	}
#if PROFILE
	else if( ParseCommand(&Str,TEXT("PROFILESCRIPT")) )
	{
		if( ParseCommand(&Str,TEXT("START")) )
		{
			delete GScriptCallGraph;
			GScriptCallGraph = new FScriptCallGraph( 16 * 1024 * 1024 );
			return 1;
		}
		else if( ParseCommand(&Str,TEXT("STOP")) )
		{
			if( GScriptCallGraph )
			{
				GScriptCallGraph->EmitEnd();

				FString fileName;
				if ( !Parse(Str, TEXT("FILE="), fileName) || fileName == TEXT("") )
					fileName = TEXT("Z:\\uc2.uprof");

				FArchive* Archive = GFileManager->CreateFileWriter(*fileName);
				if( Archive )
				{
					GScriptCallGraph->Serialize( *Archive );
					delete Archive;
				}

				delete GScriptCallGraph;
				GScriptCallGraph = NULL;
			}
			return 1;
		}
		else if( ParseCommand(&Str,TEXT("RESET"))  )
		{
			if( GScriptCallGraph )
				GScriptCallGraph->Reset();
			return 1;
		}
		return 0;
	}
#endif
	else if( ParseCommand(&Str,TEXT("DUMPNATIVES")) )
	{
		// Linux: Defined out because of error: "taking
		// the address of a bound member function to form
		// a pointer to member function, say '&Object::execUndefined'
		#if _MSC_VER
		for( INT i=0; i<EX_Max; i++ )
			if( GNatives[i] == &execUndefined )
				debugf( TEXT("Native index %i is available"), i );
		#endif
		return 1;
	}
	else if( ParseCommand(&Str,TEXT("GET")) )
	{
		// Get a class default variable.
		TCHAR ClassName[256], PropertyName[256];
		UClass* Class;
		UProperty* Property;
		if
		(	ParseToken( Str, ClassName, ARRAY_COUNT(ClassName), 1 )
		&&	(Class=FindObject<UClass>( ANY_PACKAGE, ClassName))!=NULL )
		{
			if
			(	ParseToken( Str, PropertyName, ARRAY_COUNT(PropertyName), 1 )
			&&	(Property=FindField<UProperty>( Class, PropertyName))!=NULL )
			{
				TCHAR Temp[256]=TEXT("");
				if( Class->Defaults.Num() )
					Property->ExportText( 0, Temp, Class->GetDefaultsSafe(0), Class->GetDefaultsSafe(0), PPF_Localized );
				Ar.Log( Temp );
			}
			else Ar.Logf( NAME_ExecWarning, TEXT("Unrecognized property %s"), PropertyName );
		}
		else Ar.Logf( NAME_ExecWarning, TEXT("Unrecognized class %s"), ClassName );
		return 1;
	}
	else if ( ParseCommand(&Str, TEXT("GETALL")) ) // -- rjp
	{
		// Iterate through all objects of the specified type and return the value for each object
		TCHAR ClassName[256], PropertyName[256];
		UClass* Class;
		UProperty* Property;

		if ( ParseToken(Str,ClassName,ARRAY_COUNT(ClassName), 1) &&
			(Class=FindObject<UClass>( ANY_PACKAGE, ClassName)) != NULL )
		{
			if ( ParseToken(Str,PropertyName,ARRAY_COUNT(PropertyName),1) &&
				(Property=FindField<UProperty>(Class,PropertyName)) != NULL )
			{
				INT cnt = 0;
				UBOOL bNoClasses = !ParseCommand(&Str,TEXT("SHOWCLASSES"));
				UBOOL bShowPendingKills = ParseCommand(&Str, TEXT("SHOWPENDINGKILLS"));
				for ( TObjectIterator<UObject> It; It; ++It )
				{
					static TCHAR Temp [256*256] = TEXT("");
					Temp[0] = 0;

					UClass* ClsIt = Cast<UClass>(*It);
					if ( !bNoClasses && ClsIt && (ClsIt == Class || ClsIt->IsChildOf(Class)) )
					{
						if ( ClsIt->Defaults.Num() )
						{
							TCHAR* c = Temp;

							if ( Property->ArrayDim > 1 )
								*c++ = '(';

							for ( INT i = 0; i < Property->ArrayDim; i++ )
							{
								if ( i > 0 )
								{
									c += appStrlen(c);
									appStrcat(c++,TEXT(","));
								}

								Property->ExportText( i, c, ClsIt->GetDefaultsSafe(0), ClsIt->GetDefaultsSafe(0), PPF_Localized );
							}

							if ( Property->ArrayDim > 0 && c > Temp )
							{
								c += appStrlen(c);
								*c++ = ')';
								*c++ = 0;
							}

							Ar.Logf(TEXT("%i) %s.%s = %s"), cnt++, ClsIt->GetFullName(), Property->GetName(), Temp);
						}
					}
					else if ((bShowPendingKills || !It->IsPendingKill()) && It->IsA(Class))
					{
						TCHAR* c = Temp;
						if ( Property->ArrayDim > 1 )
							*c++ = '(';

						for ( INT i = 0; i < Property->ArrayDim; i++ )
						{
							if ( i > 0 )
							{
								c += appStrlen(c);
								appStrcat(c++,TEXT(","));
							}

							Property->ExportText( i, c, (BYTE*)*It, (BYTE*)*It, PPF_Localized );
						}

						if ( Property->ArrayDim > 1 && c > Temp )
						{
							c += appStrlen(c);
							*c++ = ')';
							*c++ = 0;
						}

						Ar.Logf(TEXT("%i) %s.%s = %s"), cnt++, It->GetFullName(), Property->GetName(), Temp);
					}
				}
			}
			else Ar.Logf( NAME_ExecWarning, TEXT("Unrecognized property %s"), PropertyName );
		}
		else Ar.Logf( NAME_ExecWarning, TEXT("Unrecognized class %s"), ClassName );
		return 1;
	}
	//epic moelfke: iterates through all objects of the specified class and logs the state they're in
	else if (ParseCommand(&Str, TEXT("GETALLSTATE")))
	{
		TCHAR ClassName[256];
		UClass* Class;

		if ( ParseToken(Str, ClassName, ARRAY_COUNT(ClassName), 1) &&
			(Class = FindObject<UClass>(ANY_PACKAGE, ClassName)) != NULL )
		{
			UBOOL bShowPendingKills = ParseCommand(&Str, TEXT("SHOWPENDINGKILLS"));
			INT cnt = 0;
			for (TObjectIterator<UObject> It; It; ++It)
			{
				if ((bShowPendingKills || !It->IsPendingKill()) && It->IsA(Class))
				{
					Ar.Logf( TEXT("%i) %s is in state %s"), cnt++, It->GetFullName(),
							(It->GetStateFrame() && It->GetStateFrame()->StateNode) ?
								It->GetStateFrame()->StateNode->GetName() :
								TEXT("None") );
				}
			}
		}
		else
		{
			Ar.Logf(NAME_ExecWarning, TEXT("Unrecognized class %s"), ClassName);
		}
		return 1;
	}
	else if( ParseCommand(&Str,TEXT("SET")) )
	{
		// Set a class default variable.
		TCHAR ClassName[256], PropertyName[256];
		UClass* Class;
		UProperty* Property;
		if
		(	ParseToken( Str, ClassName, ARRAY_COUNT(ClassName), 1 )
		&&	(Class=FindObject<UClass>( ANY_PACKAGE, ClassName))!=NULL )
		{
			if
			(	ParseToken( Str, PropertyName, ARRAY_COUNT(PropertyName), 1 )
			&&	(Property=FindField<UProperty>( Class, PropertyName))!=NULL )
			{
				while( *Str==' ' )
					Str++;
				GlobalSetProperty( Str, Class, Property, Property->Offset, 1 );
			}
			else Ar.Logf( NAME_ExecWarning, TEXT("Unrecognized property %s"), PropertyName );
		}
		else Ar.Logf( NAME_ExecWarning, TEXT("Unrecognized class %s"), ClassName );
		return 1;
	}
#if LIN_LOAD && !defined(FINAL_RELEASE)
	else if( ParseCommand(&Str,TEXT("DUMPDLOS")) )
	{
		extern TArray<FString>* GDLOArray;
		// Ignore if we haven't done any DLOs
		if (GDLOArray != NULL)
		{
			Ar.Logf(TEXT("List of classes/objects DLOed:"));
			// Sort this list for convenience
			Sort(&(*GDLOArray)(0),GDLOArray->Num());
			// Log the class name for each DLO
			for (INT Index = 0; Index < GDLOArray->Num(); Index++)
			{
				Ar.Logf(*(*GDLOArray)(Index));
			}
		}
		return 1;
	}
	else if( ParseCommand(&Str,TEXT("EMPTYDLOS")) )
	{
		extern TArray<FString>* GDLOArray;
		// Ignore if we haven't done any DLOs
		if (GDLOArray != NULL)
		{
			GDLOArray->Empty();
		}
		return 1;
	}
#endif
#ifndef FINAL_RELEASE
	//jg -- Hacks for getting a list of objects that are loaded at an arbitrary time
	else if( ParseCommand(&Str,TEXT("TRACKLOADS")) )
	{
		static TArray<FString> TrackedLoads;
		// Toggle the tracking state
		GTrackObjectLoads ^= TRUE;
		// If we need our global set it up
		if (GTrackObjectLoads == TRUE && GLoadedObjects == NULL)
		{
			GLoadedObjects = &TrackedLoads;
		}
		return 1;
	}
	else if( ParseCommand(&Str,TEXT("DUMPLOADS")) )
	{
		// Ignore if this isn't valid
		if (GLoadedObjects != NULL)
		{
			Ar.Logf(TEXT("List of objects loaded since tracking was enabled:"));
			// Log the class name for each DLO
			for (INT Index = 0; Index < GLoadedObjects->Num(); Index++)
			{
				Ar.Logf(*(*GLoadedObjects)(Index));
			}
		}
		return 1;
	}
	else if( ParseCommand(&Str,TEXT("EMPTYLOADS")) )
	{
		if (GLoadedObjects != NULL)
		{
			GLoadedObjects->Empty();
		}
		return 1;
	}
	else if ( ParseCommand(&Str, TEXT("SETLANG")) )
	{
		ParseNext(&Str);

		if ( appStrlen(Str) == 3 )
		{
			debugf(TEXT("Changing language to %s"), Str);
			UObject::SetLanguage(Str);
		}

		else debugf(TEXT("Must specify a 3 letter language code"));
		return 1;
	}
#endif
	else if( ParseCommand(&Str,TEXT("OBJ")) )
	{
		if( ParseCommand(&Str,TEXT("GARBAGE")) )
		{
			// Purge unclaimed objects.
			UBOOL GSavedNoGC=GNoGC;
			GNoGC = 0;
			CollectGarbage( RF_Native | (GIsEditor ? RF_Standalone : 0) );
			GNoGC = GSavedNoGC;
			return 1;
		}
		else if( ParseCommand(&Str,TEXT("MARK")) )
		{
			debugf( TEXT("Marking objects") );
			for( FObjectIterator It; It; ++It )
				It->SetFlags( RF_Marked );
			return 1;
		}
		else if( ParseCommand(&Str,TEXT("MARKCHECK")) )
		{
			debugf( TEXT("Unmarked objects:") );
			for( FObjectIterator It; It; ++It )
				if( !(It->GetFlags() & RF_Marked) )
					debugf( TEXT("%s"), It->GetFullName() );
			return 1;
		}
		else if( ParseCommand(&Str,TEXT("REFS")) )
		{
			UClass* Class;
			UObject* Object;
			if
			(	ParseObject<UClass>( Str, TEXT("CLASS="), Class, ANY_PACKAGE )
			&&	ParseObject(Str,TEXT("NAME="), Class, Object, ANY_PACKAGE ) )
			{
				Ar.Logf( TEXT("") );
				Ar.Logf( TEXT("Referencers of %s:"), Object->GetFullName() );
				for( FObjectIterator It; It; ++It )
				{
					if ( It->IsIn(Object) )
					{
						// ALL objects reference their outers...it's just log spam here
						continue;
					}

					if ( *It == Object )
					{
						// this one is pretty easy  :)
						continue;
					}

					FArchiveFindCulprit ArFind(Object,*It);
					if( ArFind.GetCount() )
						Ar.Logf( TEXT("   %s (%i refs)"), It->GetFullName(), ArFind.GetCount() );
				}
				Ar.Logf(TEXT("") );
				Ar.Logf(TEXT("Shortest reachability from root to %s:"), Object->GetFullName() );
				TArray<UObject*> Rt = FArchiveTraceRoute::FindShortestRootPath(Object);
				for( INT i=0; i<Rt.Num(); i++ )
					Ar.Logf(TEXT("   %s%s"), Rt(i)->GetFullName(), i!=0 ? TEXT("") : (Rt(i)->GetFlags()&RF_Native)?TEXT(" (native)"):TEXT(" (root)") );
				if( !Rt.Num() )
					Ar.Logf(TEXT("   (Object is not currently rooted)"));
				Ar.Logf(TEXT("") );
			}
			return 1;
		}
		else if( ParseCommand(&Str,TEXT("HASH")) )
		{
			// Hash info.
			FName::DisplayHash( Ar );
			INT ObjCount=0, HashCount=0;
			for( FObjectIterator It; It; ++It )
				ObjCount++;
			for( INT i=0; i<ARRAY_COUNT(GObjHash); i++ )
			{
				INT c=0;
				for( UObject* Hash=GObjHash[i]; Hash; Hash=Hash->HashNext )
					c++;
				if( c )
					HashCount++;
				//debugf( "%i: %i", i, c );
			}
			return 1;
		}
		else if( ParseCommand(&Str,TEXT("HASHOUTER")) )
		{
			INT nSlotsInUse = 0;
			INT nTotalCollisions = 0;
			INT nMinCollisions = 0;
			INT nMaxCollisions = 0;
			INT nMaxBin = 0;
			// Work through each slot and figure out how many collisions
			for (DWORD CurrSlot = 0; CurrSlot < OBJECT_HASH_BINS; CurrSlot++)
			{
				INT nCollisions = 0;
				// Get the first item in the slot
				UObject* Hash = GObjHashOuter[CurrSlot];
				// Determine if this slot is being used
				if (Hash != NULL)
				{
					// This slot is in use
					nSlotsInUse++;
					// Now count how many hash collisions there are
					while (Hash)
					{
						Hash = Hash->HashOuterNext;
						// If there is another item in the chain
						if (Hash != NULL)
						{
							// Then increment the number of collisions
							nCollisions++;
						}
					}
					// Keep the global stats
					nTotalCollisions += nCollisions;
					if (nCollisions > nMaxCollisions)
					{
						nMaxBin = CurrSlot;
					}
					nMaxCollisions = Max<INT>(nCollisions,nMaxCollisions);
					nMinCollisions = Min<INT>(nCollisions,nMinCollisions);
					// Now log the output
					Ar.Logf(TEXT("\tSlot %d has %d collisions"),CurrSlot,
						nCollisions);
				}
			}
			// Dump how many slots were in use
			Ar.Logf(TEXT("Slots in use %d"),nSlotsInUse);
			// Now dump how efficient the hash is
			Ar.Logf(TEXT("Collision Stats: Best Case (%d), Average Case (%d), Worst Case (%d)"),
				nMinCollisions,appFloor(((FLOAT)nTotalCollisions / 4096.f)),
				nMaxCollisions);
			INT Count = 0;
			UObject* Hash = GObjHashOuter[nMaxBin];
			while (Hash != NULL && Count < 30)
			{
				Ar.Logf(TEXT("Object is %s (%s)"),Hash->GetName(),Hash->GetFullName());
				Hash = Hash->HashOuterNext;
				Count++;
			}
			return 1;
		}
		else if( ParseCommand(&Str,TEXT("CLASSES")) )
		{
			ShowClasses( StaticClass(), Ar, 0 );
			return 1;
		}
		else if( ParseCommand(&Str,TEXT("DEPENDENCIES")) )
		{
			UPackage* Pkg;
			if( ParseObject<UPackage>(Str,TEXT("PACKAGE="),Pkg,NULL) )
			{
				TArray<UObject*> Exclude;

				// check if we want to ignore references from any packages
				for( int i=0; i<16; i++ )
				{
					TCHAR Temp[32];
					appSprintf( Temp, TEXT("EXCLUDE%i="), i );
					FName F;
					if( Parse(Str,Temp,F) )
						Exclude.AddItem( CreatePackage(NULL,*F) );
				}
				Ar.Logf( TEXT("Dependencies of %s:"), Pkg->GetPathName() );

				UBOOL bRecurse=0;

				// rjp -- Should we recurse into inner packages?
				Parse(Str, TEXT("RECURSE"), bRecurse);

				// Iterate through the object list
				for( FObjectIterator It; It; ++It )
				{
					// if this object is within the package specified, serialize the object
					// into a specialized archive which logs object names encountered during
					// serialization -- rjp
					if ( It->IsIn(Pkg) )
					{
						if ( It->GetOuter() == Pkg )
							FArchiveShowReferences ArShowReferences( Ar, Pkg, *It, Exclude );
						else if ( bRecurse )
						{
							// Two options - 
							// a) this object is a function or something (which we don't care about)
							// b) this object is inside a group inside the specified package (which we do care about)
							UObject* CurrentObject = *It;
							UObject* CurrentOuter = It->GetOuter();
							while ( CurrentObject && CurrentOuter )
							{
								// this object is a UPackage (a group inside a package)
								// abort
								if ( CurrentObject->GetClass() == UPackage::StaticClass() )
									break;

								// see if this object's outer is a UPackage
								if ( CurrentOuter->GetClass() == UPackage::StaticClass() )
								{
									// if this object's outer is our original package, the original object (It)
									// wasn't inside a group, it just wasn't at the base level of the package
									// (its Outer wasn't the Pkg, it was something else e.g. a function, state, etc.)
									/// ....just skip it
									if ( CurrentOuter == Pkg )
										break;

									// otherwise, we've successfully found an object that was in the package we
									// were searching, but would have been hidden within a group - let's log it
									FArchiveShowReferences ArShowReferences( Ar, CurrentOuter, CurrentObject, Exclude );
									break;
								}

								CurrentObject = CurrentOuter;
								CurrentOuter = CurrentObject->GetOuter();
							}
						}
					}
				}
			}
			else
				debugf(TEXT("Package wasn't found."));
			return 1;
		}
		//scion jg -- Finds all classes that reference something in the
		// specified package
		else if(ParseCommand(&Str,TEXT("DEPENDSON")))
		{
			UPackage* pPackage = NULL;
			UClass* pClass = NULL;
			// Get the package that was requested
			if (ParseObject<UPackage>( Str, TEXT("PACKAGE="), pPackage,  NULL ))
			{
				// Look at all linker load objects for an import to this package
				for (INT nLinker = 0; nLinker < GObjLoaders.Num(); nLinker++)
				{
					ULinkerLoad* Linker = CastChecked<ULinkerLoad>( GObjLoaders(nLinker) );
					// Search the the import map for a reference to this package
					for (INT nImport = 0; nImport < Linker->ImportMap.Num(); nImport++ )
					{
						// If the import reference's the correct package
						if (Linker->ImportMap(nImport).ClassPackage ==
							pPackage->GetFName() && Linker->LinkerRoot != NULL)
						{
							// Log the importing package and the class that is
							// being used by the import
							Ar.Logf(TEXT("Referenced by package %s"),
								Linker->LinkerRoot->GetName());
							Ar.Logf(TEXT("    Class referenced is %s"),
								*Linker->ImportMap(nImport).ClassName);
							Ar.Logf(TEXT("    Object referencing is %s"),
								*Linker->ImportMap(nImport).ObjectName);
						}
					}
				}
			}
			else if (ParseObject<UClass>( Str, TEXT("CLASS="), pClass,  ANY_PACKAGE ))
			{
				// Look at all linker load objects for an import to this class
				for (INT nLinker = 0; nLinker < GObjLoaders.Num(); nLinker++)
				{
					ULinkerLoad* Linker = CastChecked<ULinkerLoad>( GObjLoaders(nLinker) );
					// Search the the import map for a reference to this class
					for (INT nImport = 0; nImport < Linker->ImportMap.Num(); nImport++ )
					{
						// If the import reference's the correct class
						if (Linker->ImportMap(nImport).ClassName ==
							pClass->GetFName() && Linker->LinkerRoot != NULL)
						{
							// Log the importing package and the object that is
							// using the import
							Ar.Logf(TEXT("Referenced by package %s"),
								Linker->LinkerRoot->GetName());
							Ar.Logf(TEXT("    Object referencing is %s"),
								*Linker->ImportMap(nImport).ObjectName);
						}
					}
				}
			}
			return 1;
		}
		else if( ParseCommand(&Str,TEXT("LINKERS")) )
		{
			Ar.Logf( TEXT("Linkers:") );
			for( INT i=0; i<GObjLoaders.Num(); i++ )
			{
				ULinkerLoad* Linker = CastChecked<ULinkerLoad>( GObjLoaders(i) );
				INT NameSize = 0;
				for( INT i=0; i<Linker->NameMap.Num(); i++ )
					if( Linker->NameMap(i) != NAME_None )
						NameSize += sizeof(FNameEntry) - (NAME_SIZE - appStrlen(*Linker->NameMap(i)) - 1) * sizeof(TCHAR);
				Ar.Logf
				(
					TEXT("%s (%s - %s): Names=%i (%iK/%iK) Imports=%i (%iK) Exports=%i (%iK) Gen=%i Lazy=%i"),
					*Linker->Filename,
					Linker->GetName(),
					Linker->LinkerRoot->GetFullName(),
					Linker->NameMap.Num(),
					Linker->NameMap.Num() * sizeof(FName) / 1024,
					NameSize / 1024,
					Linker->ImportMap.Num(),
					Linker->ImportMap.Num() * sizeof(FObjectImport) / 1024,
					Linker->ExportMap.Num(),
					Linker->ExportMap.Num() * sizeof(FObjectExport) / 1024,
					Linker->Summary.Generations.Num(),
					Linker->LazyLoaders.Num()
				);
			}
			return 1;
		}
		//scion rwd - dump object properties to log file
		else if (ParseCommand(&Str,TEXT("DUMP")))
		{
			UObject *Obj = NULL;
			if (ParseObject<UObject>(Str,TEXT("NAME="),Obj,ANY_PACKAGE))
			{
				UClass *ObjClass = Obj->GetClass();
				UBOOL bClassSpecified = appStrfind(Str, TEXT("CLASS=")) != NULL;
				ParseObject<UClass>(Str,TEXT("CLASS="),ObjClass,ANY_PACKAGE);
				debugf(TEXT("Properties for %s [%s]  ClassSpecified:%s"),Obj->GetName(),ObjClass->GetName(), bClassSpecified ? GTrue : GFalse);
				TCHAR* Temp = (TCHAR*)appAlloca(4096 * sizeof(TCHAR));
				for (TFieldIterator<UProperty,CLASS_IsAUProperty> It(ObjClass); It; ++It)
				{
					*Temp = 0;
					if (!bClassSpecified || It->GetOuter() == ObjClass)
					{
						It->ExportText( 0, Temp, (BYTE*)Obj, (BYTE*)Obj, PPF_Localized );
						debugf(TEXT("- %s: %s"),It->GetName(),Temp);
					}
				}
			}
			else
			{
				debugf(TEXT("failed to find object (obj dump name=xxx)"));
			}
			return 1;
		}
		// scion sz
		// find object with the given name and/or class in all packages
		else if( ParseCommand(&Str,TEXT("FIND")) )
		{
			UClass	*ClassType = NULL;
			ParseObject<UClass>( Str, TEXT("CLASS="), ClassType, ANY_PACKAGE );

			FName ObjName = NAME_None;
			if( Parse( Str, TEXT("NAME="), ObjName ) )
			{
				Ar.Logf( TEXT("Objects found:") );
				for( FObjectIterator It; It; ++It )
				{
					if(	It->GetFName() == ObjName &&
						(!ClassType || (ClassType && It->IsA(ClassType))) )
					{
						Ar.Logf( TEXT("%s"), It->GetFullName() );												
					}
				}
			}
			else
			{
				Ar.Logf( TEXT("Enter name of object to find: NAME=<object name>") );
			}
			return 1;
		}
		else return 0;
	}
	else if( ParseCommand(&Str,TEXT("GTIME")) )
	{
		debugf( TEXT("GTime = %f"), GTempDouble );
		return 1;
	}
#ifndef FINAL_RELEASE
	//scion rwd - suppress/unsuppress toggle support
	else if (ParseCommand(&Str,TEXT("SUPPRESS")))
	{
		FString EventName;
		if (Parse(Str,TEXT("EVENT="),EventName))
		{
			FName(*EventName).SetFlags(RF_Suppress);
			debugf(TEXT("Suppressed event %s"),*EventName);
		}
		else
		{
			debugf(TEXT("Suppress syntax ex: SUPPRESS EVENT=DevNet"));
		}
		return 1;
	}
	else if (ParseCommand(&Str,TEXT("UNSUPPRESS")))
	{
		FString EventName;
		if (Parse(Str,TEXT("EVENT="),EventName))
		{
			FName(*EventName).ClearFlags(RF_Suppress);
			debugf(TEXT("Unsuppressed event %s"),*EventName);
		}
		else
		{
			debugf(TEXT("Unsuppress syntax ex: UNSUPPRESS EVENT=DevNet"));
		}
		return 1;
	}
	// scion sz - tag all allocations
	else if( ParseCommand(&Str,TEXT("MEMTAG")) )
	{
		GMemoryTag = appAtoi( *ParseToken(Str,0) );
		return 1;
	}
	// scion sz - show tagged memory
	else if( ParseCommand(&Str,TEXT("DUMPTAG")) )
	{
		DWORD FindTag = appAtoi( *ParseToken(Str,0) );
		debugf( TEXT("DUMPTAG %d"), FindTag );
		GMalloc->DumpAllocsTagged( FindTag );
		return 1;
	}
	// DELTA DEFAULTS
	else if( ParseCommand(&Str,TEXT("DEFLIST")) )
	{
		for( TObjectIterator<UClass> It; It; ++It )
		{
			if( It->ClassFlags & CLASS_TaggedDefaults )
				debugf( TEXT("DELTA DEFAULTS Class=[%s]"), It->GetFullName() );
		}
		for( TObjectIterator<UClass> It; It; ++It )
		{
			if( It->ClassFlags & CLASS_FullDefaults )
				debugf( TEXT("FULL DEFAULTS Class=[%s]"), It->GetFullName() );
		}
		for( TObjectIterator<UClass> It; It; ++It )
		{
			if( !(It->ClassFlags & (CLASS_FullDefaults|CLASS_TaggedDefaults)) )
				debugf( TEXT("NORMAL DEFAULTS Class=[%s]"), It->GetFullName() );
		}
		return 1;
	}
#endif
	else return 0; // Not executed

	unguard;
}

/*-----------------------------------------------------------------------------
   File loading.
-----------------------------------------------------------------------------*/

//
// Safe load error-handling.
//
void UObject::SafeLoadError( DWORD LoadFlags, const TCHAR* Error, const TCHAR* Fmt, ... )
{
	// Variable arguments setup.
	TCHAR TempStr[4096];
	GET_VARARGS( TempStr, ARRAY_COUNT(TempStr), Fmt, Fmt );

	guard(UObject::SafeLoadError);
	if( !(LoadFlags & LOAD_Quiet)  ) debugf( NAME_Warning, TempStr );
#ifndef __EMSCRIPTEN__
	if(   LoadFlags & LOAD_Throw   ) appThrowf( TEXT("%s"), Error   );
#else
	// The web build compiles C++ exception catching out (cmake/UC2Web.cmake), so a throw is an abort.
	// No re-throw on LOAD_Throw: each loader
	// (GetPackageLinker, StaticLoadObject, LoadPackage) has already reported the error and returns NULL,
	// which its callers handle (DynamicLoadObject -> None, LoadMap -> "could not load map").
	(void)Error;
#endif
	if(   LoadFlags & LOAD_NoFail  ) appErrorf( TEXT("%s"), TempStr );
	if( !(LoadFlags & LOAD_NoWarn) ) warnf( TEXT("%s"), TempStr );
	unguard;
}

//
// Find or create the linker for a package.
//
ULinkerLoad* UObject::GetPackageLinker
(
	UObject*		InOuter,
	const TCHAR*	InFilename,
	DWORD			LoadFlags,
	UPackageMap*	Sandbox,
	FGuid*			CompatibleGuid
)
{
	guard(UObject::GetPackageLinker);
	check(GObjBeginLoadCount);

	// See if there is already a linker for this package.
	ULinkerLoad* Result = NULL;
	if( InOuter )
		for( INT i=0; i<GObjLoaders.Num() && !Result; i++ )
			if( GetLoader(i)->LinkerRoot == InOuter )
				Result = GetLoader( i );

	// Try to load the linker.
	try
	{
		// See if the linker is already loaded.
		TCHAR NewFilename[256]=TEXT("");
		if( Result )
		{
			// Linker already found.
			appStrcpy( NewFilename, TEXT("") );
		}
		else if( !InFilename )
		{
			// Resolve filename from package name.
			if( !InOuter )
#ifdef __EMSCRIPTEN__
				return NULL;	// fail soft on the web (SafeLoadError)
#else
				appThrowf( LocalizeError(TEXT("PackageResolveFailed"),TEXT("Core")) );
#endif
			if( !appFindPackageFile( InOuter->GetName(), CompatibleGuid, NewFilename ) )
			{
				// See about looking in the dll.
				if( (LoadFlags & LOAD_AllowDll) && InOuter->IsA(UPackage::StaticClass()) && ((UPackage*)InOuter)->DllHandle )
					return NULL;
#ifdef __EMSCRIPTEN__
				// Fail soft on the web: a missing package is a failed load, not an abort.
				debugf( NAME_Warning, TEXT("Package not found: %s (returning NULL, not throwing)"), InOuter->GetName() );
				return NULL;
#else
				appThrowf( LocalizeError(TEXT("PackageNotFound"),TEXT("Core")), InOuter->GetName() );
#endif
			}
		}
		else
		{
			// Verify that the file exists.
			if( !appFindPackageFile( InFilename, CompatibleGuid, NewFilename ) )
#ifdef __EMSCRIPTEN__
			{
				// Fail soft on the web. Callers handle a NULL linker.
				debugf( NAME_Warning, TEXT("Package file not found: %s (returning NULL, not throwing)"), InFilename );
				return NULL;
			}
#else
				appThrowf( LocalizeError(TEXT("FileNotFound"),TEXT("Core")), InFilename );
#endif

			// Resolve package name from filename.
			TCHAR Tmp[256], *T=Tmp;
			appStrncpy( Tmp, InFilename, ARRAY_COUNT(Tmp) );
			while( 1 )
			{
				if( appStrstr(T,PATH_SEPARATOR) )
					T = appStrstr(T,PATH_SEPARATOR)+appStrlen(PATH_SEPARATOR);
				// '\\' as on Windows, where it is PATH_SEPARATOR: a script's `#exec OBJ LOAD FILE=..\Textures\T_HUD.utx`
				// must name the package T_HUD, not "" (cut at the first '.').
				else if( appStrstr(T,TEXT("\\")) )
					T = appStrstr(T,TEXT("\\"))+1;
				else if( appStrstr(T,TEXT("/")) )
					T = appStrstr(T,TEXT("/"))+1;
				else if( appStrstr(T,TEXT(":")) )
					T = appStrstr(T,TEXT(":"))+1;
				else
					break;
			}
			if( appStrstr(T,TEXT(".")) )
				*appStrstr(T,TEXT(".")) = 0;
			UPackage* FilenamePkg = CreatePackage( NULL, T );

			// If no package specified, use package from file.
			if( InOuter==NULL )
			{
				if( !FilenamePkg )
#ifdef __EMSCRIPTEN__
				{
					debugf( NAME_Warning, TEXT("[PACKAGE-LOAD] Could not derive a package name from file '%s'"), InFilename );
					return NULL;
				}
#else
					appThrowf( LocalizeError(TEXT("FilenameToPackage"),TEXT("Core")), InFilename );
#endif
				InOuter = FilenamePkg;
				for( INT i=0; i<GObjLoaders.Num() && !Result; i++ )
					if( GetLoader(i)->LinkerRoot == InOuter )
						Result = GetLoader(i);
			}
			else if( InOuter != FilenamePkg )//!!should be tested and validated in new UnrealEd
			{
				// Loading a new file into an existing package, so reset the loader.
				debugf( TEXT("New File, Existing Package (%s, %s)"), InOuter->GetFullName(), FilenamePkg->GetFullName() );
				ResetLoaders( InOuter, 0, 1 );
			}
		}

		// Make sure the package is accessible in the sandbox.
		if( Sandbox && !Sandbox->SupportsPackage(InOuter) )
#ifdef __EMSCRIPTEN__
		{
			debugf( NAME_Warning, TEXT("[PACKAGE-LOAD] Package '%s' is not allowed in this context (sandbox)"), InOuter->GetName() );
			return NULL;
		}
#else
			appThrowf( LocalizeError(TEXT("Sandbox"),TEXT("Core")), InOuter->GetName() );
#endif

		// Create new linker.
#ifdef __EMSCRIPTEN__
		const UBOOL FreshLinker = !Result;	// only a linker made here may be dropped below
#endif
		if( !Result )
		{
			Result = new ULinkerLoad( InOuter, NewFilename, LoadFlags );
#ifdef __EMSCRIPTEN__
			// The ULinkerLoad constructor fails soft on the web (an unopenable, corrupt or too old package
			// leaves Success != 1 and logs why). Drop the half-built linker and return NULL: a failed load,
			// not an abort. Only a freshly created linker: a found one
			// may legitimately be mid-construction (Success == the sentinel) during nested imports.
			if( Result && Result->Success != 1 )
			{
				GObjLoaders.RemoveItem( Result );
				return NULL;
			}
#endif
#ifndef _XBOX//scion jg -- If editor, load the entire package
			if (GIsEditor == 1 && GIsUCC == 0)
			{
				Result->LoadAllObjects();
			}
#endif
		}

		// Verify compatibility.
		if( CompatibleGuid && Result->Summary.Guid!=*CompatibleGuid )
#ifdef __EMSCRIPTEN__
		{
			debugf( NAME_Warning, TEXT("[PACKAGE-LOAD] '%s' is an incompatible version of package '%s' (GUID mismatch)"), NewFilename, InOuter->GetName() );
			// Drop the linker only if it was made here: one that was already there belongs to a package in use
			// (natively the throw leaves it alone too).
			if( FreshLinker )
				GObjLoaders.RemoveItem( Result );
			return NULL;
		}
#else
			appThrowf( LocalizeError(TEXT("PackageVersion"),TEXT("Core")), InOuter->GetName() );
#endif
	}
	catch( const TCHAR* Error )
	{
		// If we're in the editor (and not running from UCC) we don't want this to be a fatal error.
		if( GIsEditor && !GIsUCC )
		{
			EdLoadErrorf( FEdLoadError::TYPE_FILE, InFilename ? InFilename : InOuter ? InOuter->GetPathName() : TEXT("NULL") );
			debugf( LocalizeError(TEXT("FailedLoad"),TEXT("Core")), InFilename ? InFilename : InOuter ? InOuter->GetName() : TEXT("NULL"), Error);
		}
		else
			SafeLoadError( LoadFlags, Error, LocalizeError(TEXT("FailedLoad"),TEXT("Core")), InFilename ? InFilename : InOuter ? InOuter->GetName() : TEXT("NULL"), Error );
	}

	// Success.
	return Result;
	unguard;
}

//
// Find or optionally create a package.
//
UPackage* UObject::CreatePackage( UObject* InOuter, const TCHAR* InName )
{
	guard(UObject::CreatePackage);

	TCHAR ResolvedName[1024];
	ResolveName( InOuter, InName, ResolvedName, 1, 0 );
	UPackage* Result = FindObject<UPackage>( InOuter, ResolvedName );
	if( !Result )
		Result = new( InOuter, ResolvedName, RF_Public )UPackage;
	return Result;

	unguard;
}

//
// Resolve a package and name.
//
UBOOL UObject::ResolveName( UObject*& InPackage, const TCHAR* InName, TCHAR* OutName, UBOOL Create, UBOOL Throw )
{
	guard(UObject::ResolveName);
	check(InName);
	check(OutName);

	// make a copy of the name
	appStrcpy( OutName, InName );

	// See if the name is specified in the .ini file.
	UBOOL SystemIni = (appStrnicmp( OutName, TEXT("ini:"), 4 )==0);
	UBOOL UserIni   = (appStrnicmp( OutName, TEXT("usr:"), 4 )==0);
	if( (SystemIni||UserIni) && appStrlen(OutName)<1024 && appStrstr(OutName,TEXT(".")) )
	{
		// Get .ini key and section.
		TCHAR Section[256];
		appStrncpy( Section, OutName+4, ARRAY_COUNT(Section) );
		TCHAR* Key = Section;
		while( appStrstr(Key,TEXT(".")) )
			Key = appStrstr(Key,TEXT("."))+1;
		check(Key!=Section);
		Key[-1] = 0;

		// Look up name.
		if( !GConfig->GetString( Section, Key, OutName, 1024, SystemIni?TEXT("System"):TEXT("User") ) )
		{
			if( Throw )
				appThrowf( LocalizeError(TEXT("ConfigNotFound"),TEXT("Core")), OutName );
			return 0;
		}
	}

	// Handle specified packages.
	while( appStrstr(OutName,TEXT(".")) )
	{
		TCHAR PartialName[256];
		appStrncpy( PartialName, OutName, ARRAY_COUNT(PartialName) );
		*appStrstr( PartialName, TEXT(".") ) = 0;
		if( Create )
		{
			InPackage = CreatePackage( InPackage, PartialName );
		}
		else
		{
			UObject* NewPackage = FindObject<UPackage>( InPackage, PartialName );
			if( !NewPackage )
			{
				NewPackage = FindObject<UObject>( InPackage, PartialName );
				if( !NewPackage )
					return 0;
			}
			InPackage = NewPackage;
		}
		// The source lies inside the destination buffer. strcpy on overlapping strings is undefined (glibc's
		// corrupts the result); MSVC's byte-by-byte copy happened to work. Move the tail down instead.
		const TCHAR* Rest = appStrstr(OutName,TEXT("."))+1;
		appMemmove( OutName, Rest, (appStrlen(Rest)+1)*sizeof(TCHAR) );
	}
	return 1;
	unguard;
}

//#define	TRACK_STATIC_LOAD

//
// Load an object.
//
UObject* UObject::StaticLoadObject( UClass* ObjectClass, UObject* InOuter, const TCHAR* InName, const TCHAR* Filename, DWORD LoadFlags, UPackageMap* Sandbox )
{
	guard(UObject::StaticLoadObject);
	check(ObjectClass);
	check(InName);

#ifdef TRACK_STATIC_LOAD   //scion rwd - tracking down some evil load times
    DWORD Load = 0;
    clock(Load);
#endif

	// Try to load.
	UObject* Result=NULL;
	BeginLoad();
	debugf( NAME_DevLoad, TEXT("Loading Object Class=%s Name=%s Outer=%s"), ObjectClass->GetFullName(), InName, InOuter ? InOuter->GetFullName() : TEXT("") );
	try
	{ 
		// Create a new linker object which goes off and tries load the file.
		ULinkerLoad* Linker = NULL;
		TCHAR ResolvedName[1024];
		ResolveName( InOuter, InName, ResolvedName, 1, 1 );
		UObject*	TopOuter = InOuter;
		while( TopOuter && TopOuter->GetOuter() )//!!can only load top-level packages from files
			TopOuter = TopOuter->GetOuter();
		if( !(LoadFlags & LOAD_DisallowFiles) )
			Linker = GetPackageLinker( TopOuter, Filename, LoadFlags | LOAD_Throw | LOAD_AllowDll, Sandbox, NULL );
		//!!this sucks because it supports wildcard sub-package matching of InName, which requires a long search.
		//!!also because linker classes require exact match
		if( Linker )
			Result = Linker->Create( ObjectClass, ResolvedName, LoadFlags, 0 );
		if( !Result )
			Result = StaticFindObjectFast( ObjectClass, InOuter, ResolvedName );
		if( !Result && !(LoadFlags & LOAD_NoRemap) )
#ifdef __EMSCRIPTEN__
		{
			// No throw on the web (catching is compiled
			// out there). The same as the catch below does with that error: try a remapped package, then report
			// the failed load (SafeLoadError does not throw on the web) and return NULL.
			EndLoad();
			const FString Error = FString::Printf( LocalizeError(TEXT("ObjectNotFound"),TEXT("Core")), ObjectClass->GetName(), InOuter ? InOuter->GetPathName() : TEXT("None"), ResolvedName );
			if( InOuter && !Filename )
			{
				TArray<FName> Remaps;
				GObjPackageRemap->MultiFind( InOuter->GetFName(), Remaps );
				for( INT i=0; i<Remaps.Num(); i++ )
				{
					InOuter = CreatePackage( NULL, *Remaps(i) );
					Result  = StaticLoadObject( ObjectClass, InOuter, InName, Filename, LoadFlags|LOAD_NoRemap, Sandbox );
					if( Result )
						return Result;
				}
			}
			SafeLoadError( LoadFlags, *Error, LocalizeError(TEXT("FailedLoadObject"),TEXT("Core")), ObjectClass->GetName(), InOuter ? InOuter->GetPathName() : TEXT("None"), InName, *Error );
			return NULL;
		}
#else
			appThrowf( LocalizeError(TEXT("ObjectNotFound"),TEXT("Core")), ObjectClass->GetName(), InOuter ? InOuter->GetPathName() : TEXT("None"), ResolvedName );
#endif
		else if( !Result )
		{
			SafeLoadError( LoadFlags, 
				LocalizeError(TEXT("ObjectNotFound"),TEXT("Core")), 
				LocalizeError(TEXT("FailedLoadObject"), TEXT("Core")), 
				ObjectClass->GetName(), 
				InOuter ? InOuter->GetPathName() : TEXT("None"), 
				ResolvedName, 
				LocalizeError(TEXT("ObjectNotFound"),TEXT("Core")) );
		}
		EndLoad();
	}
	catch( const TCHAR* Error )
	{
		EndLoad();
		if( InOuter && !(LoadFlags & LOAD_NoRemap) && !Filename )
		{
			// Try loading a remapped package.
			TArray<FName> Remaps;
			GObjPackageRemap->MultiFind( InOuter->GetFName(), Remaps );
			for( INT i=0; i<Remaps.Num(); i++ )
			{
				InOuter = CreatePackage( NULL, *Remaps(i) );
				Result  = StaticLoadObject( ObjectClass, InOuter, InName, Filename, LoadFlags|LOAD_NoRemap, Sandbox );
				if( Result )
					return Result;
			}
		}
		SafeLoadError( LoadFlags, Error, LocalizeError(TEXT("FailedLoadObject"),TEXT("Core")), ObjectClass->GetName(), InOuter ? InOuter->GetPathName() : TEXT("None"), InName, Error );
	}
#ifdef TRACK_STATIC_LOAD   //scion rwd - tracking down some evil load times
    unclock(Load);
    DOUBLE LoadTime = (DOUBLE)Load * GSecondsPerCycle * 1024.f;
    if (Result &&
        LoadTime > 2.f) {
        debugf(TEXT("%s took %2.1fms to load"),Result->GetPathName(),LoadTime);
    }
#endif
	return Result;
	unguardf(( TEXT("(%s %s.%s %s)"), ObjectClass->GetPathName(), InOuter ? InOuter->GetPathName() : TEXT("None"), InName, Filename ? Filename : TEXT("NULL") ));
}

//
// Load a class.
//
UClass* UObject::StaticLoadClass( UClass* BaseClass, UObject* InOuter, const TCHAR* InName, const TCHAR* Filename, DWORD LoadFlags, UPackageMap* Sandbox )
{
	guard(UObject::StaticLoadClass);
	check(BaseClass);
	try
	{
		UClass* Class = LoadObject<UClass>( InOuter, InName, Filename, LoadFlags | LOAD_Throw, Sandbox );
		if( Class && !Class->IsChildOf(BaseClass) )
			// The message is "%s is not a child class of %s.%s": the class, then the base class's package and name.
			appThrowf( LocalizeError(TEXT("LoadClassMismatch"),TEXT("Core")), Class->GetFullName(), BaseClass->GetOuter()->GetName(), BaseClass->GetName() );
		return Class;
	}
	catch( const TCHAR* Error )
	{
		// Failed.
		SafeLoadError( LoadFlags, Error, Error );
		return NULL;
	}
	unguard;
}

//
// Load all objects in a package.
//
UObject* UObject::LoadPackage( UObject* InOuter, const TCHAR* Filename, DWORD LoadFlags )
{
	guard(UObject::LoadPackage);
	UObject* Result;

    if( *Filename == '\0' )
        return NULL;

	// Try to load.
	BeginLoad();
	try
	{
		// Create a new linker object which goes off and tries load the file.
		ULinkerLoad* Linker = GetPackageLinker( InOuter, Filename ? Filename : InOuter->GetName(), LoadFlags | LOAD_Throw, NULL, NULL );
        if( !Linker )
		{
			EndLoad();
            return( NULL );
		}

		if( !(LoadFlags & LOAD_Verify) )
			Linker->LoadAllObjects();
		Result = Linker->LinkerRoot;
		EndLoad();
	}
	catch( const TCHAR* Error )
	{
		EndLoad();
		SafeLoadError( LoadFlags, Error, LocalizeError(TEXT("FailedLoadPackage"),TEXT("Core")), Error );
		Result = NULL;
	}
	return Result;
	unguard;
}

//
// Verify a linker.
//
void UObject::VerifyLinker( ULinkerLoad* Linker )
{
	guard(UObject::VerifyLinker);
	Linker->Verify();
	unguard;
}

//
// Begin loading packages.
//warning: Objects may not be destroyed between BeginLoad/EndLoad calls.
//
void UObject::BeginLoad()
{
	guard(UObject::BeginLoad);
	if( ++GObjBeginLoadCount == 1 )
	{
		// Validate clean load state.
		//!!needed? check(GObjLoaded.Num()==0);
		check(!GAutoRegister);
		for( INT i=0; i<GObjLoaders.Num(); i++ )
			check(GetLoader(i)->Success);
	}
	unguard;
}

#ifndef _XBOX//jg -- This code, while nice for the pc, is satanic to linear loading
static inline int Compare( const UObject* T1, const UObject* T2 )
{
	ULinker* L1 = T1->GetLinker();
	ULinker* L2 = T2->GetLinker();

	if( !L1 || !L2 || L1!=L2 )
		return L1 - L2;

	FObjectExport& E1 = L1->ExportMap( T1->GetLinkerIndex() );
	FObjectExport& E2 = L2->ExportMap( T2->GetLinkerIndex() );
	return E1.SerialOffset - E2.SerialOffset;	
}
#endif

//
// End loading packages.
//
void UObject::EndLoad()
{
	guard(UObject::EndLoad);
	check(GObjBeginLoadCount>0);
	if( (GObjBeginLoadCount-1) == 0 )
	{
		try
		{
			TArray<UObject*>	ObjLoaded;

			while(GObjLoaded.Num())
			{
				appMemcpy(&ObjLoaded(ObjLoaded.Add(GObjLoaded.Num())),&GObjLoaded(0),GObjLoaded.Num() * sizeof(UObject*));
				GObjLoaded.Empty();
#ifndef _XBOX//jg -- This code, while nice for the pc, is satanic to linear loading
				// Sort by file and seek
				Sort( &ObjLoaded(0), ObjLoaded.Num() );
#endif
				// Finish loading everything.
				//warning: Array may expand during iteration.
				guard(PreLoadObjects);
//				debugf( NAME_DevLoad, TEXT("Loading objects...") );
#if __PSX2_EE__ // add the cool endLoad printout
				for( INT i=0,n=0; i<ObjLoaded.Num(); i++ )
				{
					if( i==n )
					{
						n=ObjLoaded.Num();
						debugf(TEXT("endLoad: %i"),n-i);
					}
					// Preload.
					UObject* Obj = ObjLoaded(i);
					{
						if( Obj->GetFlags() & RF_NeedLoad )
						{
							check(Obj->GetLinker());
							Obj->GetLinker()->Preload( Obj );
						}
					}
				}
#else
				for( INT i=0; i<ObjLoaded.Num(); i++ )
				{
					// Preload.
					UObject* Obj = ObjLoaded(i);
					{
						if( Obj->GetFlags() & RF_NeedLoad )
						{
							check(Obj->GetLinker());
							Obj->GetLinker()->Preload( Obj );
						}
					}
				}
#endif
				unguard;

				if(GObjLoaded.Num())
					continue;

				// Postload objects.
				guard(PostLoadObjects);
				for( INT i=0; i<ObjLoaded.Num(); i++ )
					ObjLoaded(i)->ConditionalPostLoad();
				unguard;
			}

			// Dissociate all linker import object references, since they
			// may be destroyed, causing their pointers to become invalid.
			guard(DissociateImports);
			if( GImportCount )
			{
				for( INT i=0; i<GObjLoaders.Num(); i++ )
				{
					for( INT j=0; j<GetLoader(i)->ImportMap.Num(); j++ )
					{
						FObjectImport& Import = GetLoader(i)->ImportMap(j);
						if( Import.XObject && !(Import.XObject->GetFlags() & RF_Native) )
							Import.XObject = NULL;
					}
				}
			}
			GImportCount=0;
			unguard;
		}
		catch( const TCHAR* Error )
		{
			// Any errors here are fatal.
			appErrorf( Error );
			Error++;
		}
	}

	// DELTA DEFAULTS
	// Decrease load count AFTER we've endloaded objects.
	// Still not sure about the full implications of this, but it's definitely needed to avoid recursive
	// CreateExport calls and duplicate object creation when using tagged defaults.
	GObjBeginLoadCount--;

	unguard;
}

//
// Empty the loaders.
//
void UObject::ResetLoaders( UObject* Pkg, UBOOL DynamicOnly, UBOOL ForceLazyLoad )
{
	guard(UObject::ResetLoaders);
	for( INT i=GObjLoaders.Num()-1; i>=0; i-- )
	{
		ULinkerLoad* Linker = CastChecked<ULinkerLoad>( GetLoader(i) );
		if( Pkg==NULL || Linker->LinkerRoot==Pkg )
		{
			if( DynamicOnly )
			{
				// Reset runtime-dynamic objects.
				for( INT i=0; i<Linker->ExportMap.Num(); i++ )
				{
					UObject*& Object = Linker->ExportMap(i)._Object;
					if( Object && !(Object->GetClass()->ClassFlags & CLASS_RuntimeStatic))
						Linker->DetachExport( i );
				}
			}
			else
			{
				// Fully reset the loader.
				if( ForceLazyLoad )
					Linker->DetachAllLazyLoaders( 1 );
			}
		}
		else
		{
			for(INT i = 0;i < Linker->ImportMap.Num();i++)
			{
				if(Linker->ImportMap(i).SourceLinker && Linker->ImportMap(i).SourceLinker->LinkerRoot == Pkg)
				{
					Linker->ImportMap(i).SourceLinker = NULL;
					Linker->ImportMap(i).SourceIndex = INDEX_NONE;
				}
			}
		}
	}
	for( INT i=GObjLoaders.Num()-1; i>=0; i-- )
	{
		ULinkerLoad* Linker = CastChecked<ULinkerLoad>( GetLoader(i) );
		if( Pkg==NULL || Linker->LinkerRoot==Pkg )
		{
			if( !DynamicOnly )
			{
				delete Linker;
			}
		}
	}
	unguard;
}

/*-----------------------------------------------------------------------------
	File saving.
-----------------------------------------------------------------------------*/

//
// Archive for tagging objects and names that must be exported
// to the file.  It tags the objects passed to it, and recursively
// tags all of the objects this object references.
//
class FArchiveSaveTagExports : public FArchive
{
public:
	FArchiveSaveTagExports( UObject* InOuter )
	: Parent(InOuter)
	{
		ArIsSaving = ArIsPersistent = 1;
	}
	FArchive& operator<<( UObject*& Obj )
	{
		guard(FArchiveSaveTagExports<<Obj);
		if( Obj && Obj->IsIn(Parent) && !(Obj->GetFlags() & (RF_Transient|RF_TagExp)) )//&& !Obj->IsPendingKill() )
		{
			// Set flags.
			Obj->SetFlags(RF_TagExp);
			if( !(Obj->GetFlags() & RF_NotForEdit  ) ) Obj->SetFlags(RF_LoadForEdit);
			if( !(Obj->GetFlags() & RF_NotForClient) ) Obj->SetFlags(RF_LoadForClient);
			if( !(Obj->GetFlags() & RF_NotForServer) ) Obj->SetFlags(RF_LoadForServer);

			// Recurse with this object's class and package.
			UObject* Class  = Obj->GetClass();
			UObject* Parent = Obj->GetOuter();
			*this << Class << Parent;

			// Recurse with this object's children.
			Obj->Serialize( *this );
		}
		return *this;
		unguard;
	}
	UObject* Parent;
};

//
// QSort comparators.
//
static ULinkerSave* GTempSave;
INT CDECL LinkerNameSort( const void* A, const void* B )
{
	return GTempSave->MapName((FName*)B) - GTempSave->MapName((FName*)A);
}
INT CDECL LinkerImportSort( const void* A, const void* B )
{
	return GTempSave->MapObject(((FObjectImport*)B)->XObject) - GTempSave->MapObject(((FObjectImport*)A)->XObject);
}
INT CDECL LinkerExportSort( const void* A, const void* B )
{
	return GTempSave->MapObject(((FObjectExport*)B)->_Object) - GTempSave->MapObject(((FObjectExport*)A)->_Object);
}

// SavePackage's three sorts by usage count must not depend on the C library. appQsort is the C library's qsort,
// which leaves the order of equal keys to the library (glibc's merge sort keeps table order, msvcrt's does not),
// so ucc-bin and ucc.exe would append the new names, imports and exports of a conformed save in different orders.
// A stable merge sort gives every platform the order glibc gives.
static void UC2StableSort( void* Base, INT Num, INT Width, QSORT_COMPARE Compare )
{
	if( Num < 2 )
		return;
	TArray<BYTE> Temp;
	Temp.Add( Num * Width );
	BYTE* Src = (BYTE*)Base;
	BYTE* Dst = &Temp(0);
	for( INT Run=1; Run<Num; Run*=2 )
	{
		for( INT Lo=0; Lo<Num; Lo+=2*Run )
		{
			INT Mid = Min( Lo+Run, Num ), Hi = Min( Lo+2*Run, Num );
			INT i = Lo, j = Mid, k = Lo;
			while( i<Mid && j<Hi )
				// Equal keys: the left (earlier) one first.
				if( Compare( Src+j*Width, Src+i*Width ) < 0 )
					appMemcpy( Dst+(k++)*Width, Src+(j++)*Width, Width );
				else
					appMemcpy( Dst+(k++)*Width, Src+(i++)*Width, Width );
			if( i<Mid )
				appMemcpy( Dst+k*Width, Src+i*Width, (Mid-i)*Width );
			if( j<Hi )
				appMemcpy( Dst+k*Width, Src+j*Width, (Hi-j)*Width );
		}
		Exchange( Src, Dst );
	}
	if( Src != (BYTE*)Base )
		appMemcpy( Base, Src, Num*Width );
}

//
// Archive for tagging objects and names that must be listed in the
// file's imports table.
//
class FArchiveSaveTagImports : public FArchive
{
public:
	DWORD ContextFlags;
	FArchiveSaveTagImports( ULinkerSave* InLinker, DWORD InContextFlags )
	: ContextFlags( InContextFlags ), Linker( InLinker )
	{
		ArIsSaving = ArIsPersistent = 1;
	}
	FArchive& operator<<( UObject*& Obj )
	{
		guard(FArchiveSaveTagImports<<Obj);
		if( Obj && !Obj->IsPendingKill() )
		{
			if( !(Obj->GetFlags() & RF_Transient) || (Obj->GetFlags() & RF_Public) )
			{
				Linker->ObjectIndices(Obj->GetIndex())++;
				if( !(Obj->GetFlags() & RF_TagExp ) )
				{
					Obj->SetFlags( RF_TagImp );
					if( !(Obj->GetFlags() & RF_NotForEdit  ) ) Obj->SetFlags(RF_LoadForEdit);
					if( !(Obj->GetFlags() & RF_NotForClient) ) Obj->SetFlags(RF_LoadForClient);
					if( !(Obj->GetFlags() & RF_NotForServer) ) Obj->SetFlags(RF_LoadForServer);
					UObject* Parent = Obj->GetOuter();
					if( Parent )
						*this << Parent;
				}
			}
		}
		return *this;
		unguard;
	}
	FArchive& operator<<( FName& Name )
	{
		guard(FArchiveSaveTagImports<<Name);
		Name.SetFlags( RF_TagExp | ContextFlags );
		Linker->NameIndices(Name.GetIndex())++;
		return *this;
		unguard;
	}
	ULinkerSave* Linker;
};

//
// Save one specific object into an Unrealfile.
//
UBOOL UObject::SavePackage( UObject* InOuter, UObject* Base, DWORD TopLevelFlags, const TCHAR* Filename, FOutputDevice* Error, ULinkerLoad* Conform )
{
	guard(UObject::SavePackage);
	check(InOuter);
	check(Filename);
	LOCAL_CLOCK(Time);

	// Tag parent flags.
	//!!should be user-controllable in UnrealEd.
	if( Cast<UPackage>( InOuter ) )
		Cast<UPackage>( InOuter )->PackageFlags |= PKG_AllowDownload;

	// Make temp file.
	TCHAR TempFilename[256];
	appStrncpy( TempFilename, Filename, ARRAY_COUNT(TempFilename) );
	INT c = appStrlen(TempFilename);
	while( c>0 && TempFilename[c-1]!=PATH_SEPARATOR[0] && TempFilename[c-1]!='/' && TempFilename[c-1]!=':' )
		c--;
	TempFilename[c]=0;
	appStrcat( TempFilename, TEXT("Save.tmp") );

	// Init.
	GWarn->StatusUpdatef( 0, 0, LocalizeProgress(TEXT("Saving"),TEXT("Core")), Filename );
	UBOOL Success = 0;

	// If we have a loader for the package, unload it to prevent conflicts.
	ResetLoaders( InOuter, 0, 1 );

	// Untag all objects and names.
	guard(Untag);
	for( FObjectIterator It; It; ++It )
		It->ClearFlags( RF_TagImp | RF_TagExp | RF_LoadForEdit | RF_LoadForClient | RF_LoadForServer );
	for( INT i=0; i<FName::GetMaxNames(); i++ )
		if( FName::GetEntry(i) )
			FName::GetEntry(i)->Flags &= ~(RF_TagImp | RF_TagExp | RF_LoadForEdit | RF_LoadForClient | RF_LoadForServer);
	unguard;

	// Export objects.
	guard(TagExports);
	FArchiveSaveTagExports Ar( InOuter );
	if( Base )
		Ar << Base;
	for( FObjectIterator It; It; ++It )
	{
		if( (It->GetFlags() & TopLevelFlags) && It->IsIn(InOuter) )
		{
			UObject* Obj = *It;
			Ar << Obj;
		}
	}
	unguard;

	ULinkerSave* Linker = NULL;
	try
	{
		// Allocate the linker.
		Linker = new ULinkerSave( InOuter, TempFilename );

		// Import objects and names.
		guard(TagImports);
		for( FObjectIterator It; It; ++It )
		{
			if( It->GetFlags() & RF_TagExp )
			{
				// Build list.
				FArchiveSaveTagImports Ar( Linker, It->GetFlags() & RF_LoadContextFlags );
				It->Serialize( Ar );
				UClass* Class = It->GetClass();
				Ar << Class;
				if( It->IsIn(GetTransientPackage()) )
					appErrorf( LocalizeError(TEXT("TransientImport"),TEXT("Core")), It->GetFullName() );
			}
		}
		unguard;

		// Export all relevant object, class, and package names.
		guard(ExportNames);
		for( FObjectIterator It; It; ++It )
		{
			if( It->GetFlags() & (RF_TagExp|RF_TagImp) )
			{
				It->GetFName().SetFlags( RF_TagExp | RF_LoadForEdit | RF_LoadForClient | RF_LoadForServer );
				if( It->GetOuter() )
					It->GetOuter()->GetFName().SetFlags( RF_TagExp | RF_LoadForEdit | RF_LoadForClient | RF_LoadForServer );
				if( It->GetFlags() & RF_TagImp )
				{
					It->Class->GetFName().SetFlags( RF_TagExp | RF_LoadForEdit | RF_LoadForClient | RF_LoadForServer );
					check(It->Class->GetOuter());
					It->Class->GetOuter()->GetFName().SetFlags( RF_TagExp | RF_LoadForEdit | RF_LoadForClient | RF_LoadForServer );
					if( !(It->GetFlags() & RF_Public) )
					{
						warnf( TEXT("Referencers of %s:"), It->GetFullName() );
						for( FObjectIterator RefIt; RefIt; ++RefIt )
						{
							FArchiveFindCulprit ArFind(*It,*RefIt);
							if( ArFind.GetCount() )
								warnf( TEXT("   %s"), RefIt->GetFullName() );
						}
						appThrowf( LocalizeError(TEXT("FailedSavePrivate"),TEXT("Core")), Filename, It->GetFullName() );
					}
				}
				else debugf( NAME_DevSave, TEXT("Saving %s"), It->GetFullName() );
			}
		}
		unguard;

		// Write fixed-length file summary to overwrite later.
		guard(SaveSummary);
		if( Conform )
		{
			// Conform to previous generation of file.
			debugf( TEXT("Conformal save, relative to: %s, Generation %i"), *Conform->Filename, Conform->Summary.Generations.Num()+1 );
			Linker->Summary.Guid        = Conform->Summary.Guid;
			Linker->Summary.Generations = Conform->Summary.Generations;
		}
		else
		{
			// First generation file.
			Linker->Summary.Guid        = appCreateGuid();
			Linker->Summary.Generations = TArray<FGenerationInfo>();
		}
		new(Linker->Summary.Generations)FGenerationInfo( 0, 0 );
		*Linker << Linker->Summary;
		unguard;

		// Build NameMap.
		guard(BuildNameMap);
		Linker->Summary.NameOffset = Linker->Tell();
		for( INT i=0; i<FName::GetMaxNames(); i++ )
		{
			if( FName::GetEntry(i) )
			{
				FName Name( (EName)i );
				if( Name.GetFlags() & RF_TagExp )
					Linker->NameMap.AddItem( Name );
			}
		}
		unguard;

		// Sort names by usage count in order to maximize compression.
		GTempSave = Linker;
		{
			guard(SortNames);
			INT SortStartPosition = 0;
			if( Conform )
			{
	#if DO_GUARD_SLOW
				// intended to be a copy
				TArray<FName> Orig = Linker->NameMap;
	#endif
				TArray<FName>& Names = Linker->NameMap;
				for( INT i=0; i<Conform->NameMap.Num(); i++ )
				{
					FName& CurrentName = Conform->NameMap(i);

					// determine whether the new package contains this name from the old package
					INT Index = Names.FindItemIndex(CurrentName);
					if( Index == INDEX_NONE )
					{
						// if this name doesn't exist in the new package, add it to the end of our names list because it exists in the old package
						Names.Add();
						Names.Last() = Names(i);
					}
					else
					{
						Exchange( Names(i), Names(Index) );
					}
					Names(i) = CurrentName;
				}
	#if DO_GUARD_SLOW
				for( INT i=0; i<Conform->NameMap.Num(); i++ )
					check(Linker->NameMap(i)==Conform->NameMap(i));
				for( INT i=0; i<Orig.Num(); i++ )
					check(Linker->NameMap.FindItemIndex(Orig(i))!=INDEX_NONE);
	#endif
				SortStartPosition = Conform->NameMap.Num();
			}
			UC2StableSort( &Linker->NameMap(SortStartPosition), Linker->NameMap.Num()-SortStartPosition, sizeof(Linker->NameMap(0)), LinkerNameSort );
			unguard;

			// Save names.
			guard(SaveNames);
			Linker->Summary.NameCount = Linker->NameMap.Num();
			for( INT i=0; i<Linker->NameMap.Num(); i++ )
			{
				*Linker << *FName::GetEntry( Linker->NameMap(i).GetIndex() );
				Linker->NameIndices(Linker->NameMap(i).GetIndex()) = i;
			}
			unguard;
		}

		// Build ImportMap.
		for( FObjectIterator It; It; ++It )
		{
			if( It->GetFlags() & RF_TagImp )
				new( Linker->ImportMap )FObjectImport( *It );
		}

		{
			INT SortStartPosition=0;
			TArray<FObjectImport>& Imports=Linker->ImportMap;
			if ( Conform )
			{
				// intended to be a copy
				TArray<FObjectImport> Orig = Imports;
				Imports.Empty();

				// this array tracks which imports from the new package exist in the old package
				TArray<BYTE> Used;
				Used.AddZeroed(Orig.Num());

				TMap<FString,INT> OriginalImportIndexes;
				for ( INT i = 0; i < Orig.Num(); i++ )
				{
					FObjectImport& Import = Orig(i);
					OriginalImportIndexes.Set( Import.XObject->GetFullName(), i );
				}

				for( INT i=0; i<Conform->ImportMap.Num(); i++ )
				{
					// determine whether the new version of the package contains this export from the old package
					INT* OriginalImportPosition = OriginalImportIndexes.Find( Conform->GetImportFullName(i/*,Linker->LinkerRoot->GetPathName()*/) );
					if( OriginalImportPosition )
					{
						// this import exists in the new package as well,
						// create a copy of the FObjectImport located at the original index and place it
						// into the matching position in the new package's import map
						FObjectImport* NewImport = new(Imports) FObjectImport( Orig(*OriginalImportPosition) );
						check(NewImport->XObject == Orig(*OriginalImportPosition).XObject);
						Used( *OriginalImportPosition ) = 1;
					}
					else
					{
						// this import no longer exists in the new package
						new(Imports)FObjectImport( NULL );
					}
				}

				SortStartPosition = Conform->ImportMap.Num();
				for( INT i=0; i<Used.Num(); i++ )
				{
					if( !Used(i) )
					{
						// the FObjectImport located at pos "i" in the original import table did 
						// exist in the old package - add it to the end of the import table
						new(Imports)FObjectImport( Orig(i) );
					}
				}
			}

			// Sort imports by usage count.
			// Sort only the imports after the conformed ones, as the names and exports are sorted. Epic's line sorted
			// Num()-SortStartPosition entries from 0, which reorders that many of the previous generation's imports (and the
			// operand bytes of every function that names them) in a conformed save that adds imports.
			GTempSave = Linker;
			UC2StableSort( &Imports(SortStartPosition), Imports.Num() - SortStartPosition, sizeof(Imports(0)), LinkerImportSort );
			Linker->Summary.ImportCount = Linker->ImportMap.Num();
		}

		for( FObjectIterator It; It; ++It )
			if( It->GetFlags() & RF_TagExp )
				new( Linker->ExportMap )FObjectExport( *It );

		// Sort exports by usage count.
		guard(SortExports);
		{
			INT SortStartPosition = 0;
			TArray<FObjectExport>& Exports = Linker->ExportMap;
			if( Conform )
			{
				// intended to be a copy
				TArray<FObjectExport> Orig = Exports;

				Exports.Empty( Exports.Num() );

				// this array tracks which exports from the new package are represented in the old package
				TArray<BYTE> Used;
				Used.AddZeroed( Orig.Num() );
				
				TMap<FString,INT> OriginalExportIndexes;
				for( INT i=0; i<Orig.Num(); i++ )
				{
					OriginalExportIndexes.Set( Orig(i)._Object->GetFullName(), i );
				}
				for( INT i=0; i<Conform->ExportMap.Num(); i++ )
				{
					// determine whether the new version of the package contains this export from the old package
					INT* OriginalExportPosition = OriginalExportIndexes.Find( Conform->GetExportFullName(i,Linker->LinkerRoot->GetPathName()) );
					if( OriginalExportPosition )
					{
						// this export exists in the new package as well,
						// create a copy of the FObjectExport located at the 
						new(Exports) FObjectExport( Orig(*OriginalExportPosition) );
						check(Exports.Last()._Object == Orig(*OriginalExportPosition)._Object);
						Used( *OriginalExportPosition ) = 1;
					}
					else
					{
						// this export no longer exists in the new package
						new(Exports)FObjectExport( NULL );
					}
				}

				SortStartPosition = Conform->ExportMap.Num();
				for( INT i=0; i<Used.Num(); i++ )
				{
					if( !Used(i) )
					{
						// the FObjectExport located at pos "i" in the original export table did 
						// exist in the old package - add it to the end of the export table
						new(Exports)FObjectExport( Orig(i) );
					}
				}

	#if DO_GUARD_SLOW
				for( INT i=0; i<Orig.Num(); i++ )
				{
					INT j = 0;
					for( j=0; j<Exports.Num(); j++ )
						if( Exports(j)._Object == Orig(i)._Object )
							break;
					check(j<Exports.Num());
				}
	#endif
			}
			UC2StableSort( &Exports(SortStartPosition), Exports.Num()-SortStartPosition, sizeof(Exports(0)), LinkerExportSort );
			Linker->Summary.ExportCount = Exports.Num();
		}
		unguard;

		// Set linker reverse mappings.
		guard(SetLinkerMappings);
		for( INT i=0; i<Linker->ExportMap.Num(); i++ )
		{
			if( Linker->ExportMap(i)._Object )
			{
				Linker->ObjectIndices(Linker->ExportMap(i)._Object->GetIndex()) = i+1;
			}
		}
		for( INT i=0; i<Linker->ImportMap.Num(); i++ )
		{
			if ( Linker->ImportMap(i).XObject )
				Linker->ObjectIndices(Linker->ImportMap(i).XObject->GetIndex()) = -i-1;
		}
		unguard;

		// Save exports.
		guard(SaveExports);
		for( INT i=0; i<Linker->ExportMap.Num(); i++ )
		{
			FObjectExport& Export = Linker->ExportMap(i);
			if( Export._Object )
			{
				// Set class index.
				if( !Export._Object->IsA(UClass::StaticClass()) )
				{
					Export.ClassIndex = Linker->ObjectIndices(Export._Object->GetClass()->GetIndex());
					check(Export.ClassIndex!=0);
				}
				if( Export._Object->IsA(UStruct::StaticClass()) )
				{
					UStruct* Struct = (UStruct*)Export._Object;
					if( Struct->SuperField )
					{
						Export.SuperIndex = Linker->ObjectIndices(Struct->SuperField->GetIndex());
						check(Export.SuperIndex!=0);
					}
				}

				// Set package index.
				if( Export._Object->GetOuter() != InOuter )
				{
					check(Export._Object->GetOuter()->IsIn(InOuter));
					Export.PackageIndex = Linker->ObjectIndices(Export._Object->GetOuter()->GetIndex());
					check(Export.PackageIndex>0);
				}

				// Save it.
				Export.SerialOffset = Linker->Tell();
				Export._Object->Serialize( *Linker );
				Export.SerialSize = Linker->Tell() - Linker->ExportMap(i).SerialOffset;
			}
		}
		unguard;

		// Save the import map.
		guard(SaveImportMap);
		Linker->Summary.ImportOffset = Linker->Tell();
		for( INT i=0; i<Linker->ImportMap.Num(); i++ )
		{
			FObjectImport& Import = Linker->ImportMap( i );
			// Set the package index.
			if ( Import.XObject && Import.XObject->GetOuter() )
			{
				check(!Import.XObject->GetOuter()->IsIn(InOuter));
				Import.PackageIndex = Linker->ObjectIndices(Import.XObject->GetOuter()->GetIndex());
				check(Import.PackageIndex<0);
			}

			// Save it.
			*Linker << Import;
		}
		unguard;

		// Save the export map.
		guard(SaveExportMap);
		Linker->Summary.ExportOffset = Linker->Tell();
		for( int i=0; i<Linker->ExportMap.Num(); i++ )
			*Linker << Linker->ExportMap( i );
		unguard;

		// Rewrite updated file summary.
		guard(RewriteSummary);
		GWarn->StatusUpdatef( 0, 0, LocalizeProgress(TEXT("Closing"),TEXT("Core")) );
		Linker->Summary.Generations.Last().ExportCount = Linker->Summary.ExportCount;
		Linker->Summary.Generations.Last().NameCount   = Linker->Summary.NameCount;
		Linker->Seek(0);
		*Linker << Linker->Summary;
		unguard;

		Success = 1;
	}
	catch( const TCHAR* Msg )
	{
		// Delete the temporary file.
		GFileManager->Delete( TempFilename );
		Error->Logf( NAME_Warning, TEXT("%s"), Msg );
	}
	if( Linker )
	{
#if defined(XBOX_ASSET_FORMAT) && !defined(_XBOX)
		//jg -- Save the linker save if we have this flag set
		extern TArray<FObjectExport>* GExportMap;
		if (GExportMap != NULL)
		{
			*GExportMap = Linker->ExportMap;
		}
#endif
		delete Linker;
	}
	debugf( NAME_Log, TEXT("Save=%f"), GSecondsPerCycle*1000* LOCAL_UNCLOCK(Time) );
	if( Success )
	{
		// Move the temporary file.
		debugf( NAME_Log, TEXT("Moving '%s' to '%s'"), TempFilename, Filename );
		if( !GFileManager->Move( Filename, TempFilename ) )
		{
			GFileManager->Delete( TempFilename );
			warnf( LocalizeError(TEXT("SaveWarning"),TEXT("Core")), Filename );
			Success = 0;
		}
	}
	return Success;
	unguard;
}

/*-----------------------------------------------------------------------------
	Misc.
-----------------------------------------------------------------------------*/

//
// Return whether statics are initialized.
//
UBOOL UObject::GetInitialized()
{
	return UObject::GObjInitialized;
}

//
// Return the static transient package.
//
UPackage* UObject::GetTransientPackage()
{
	return UObject::GObjTransientPkg;
}

//
// Return the ith loader.
//
ULinkerLoad* UObject::GetLoader( INT i )
{
	return (ULinkerLoad*)GObjLoaders(i);
}

//
// Add an object to the root array. This prevents the object and all
// its descendents from being deleted during garbage collection.
//
void UObject::AddToRoot()
{
	guard(UObject::AddToRoot);
	SetFlags( RF_TagPersist );
	GObjRoot.AddItem( this );
	unguard;
}

//
// Remove an object from the root array.
//
void UObject::RemoveFromRoot()
{
	guard(UObject::RemoveFromRoot);
	ClearFlags( RF_TagPersist );
	GObjRoot.RemoveItem( this );
	unguard;
}

/*-----------------------------------------------------------------------------
	Object name functions.
-----------------------------------------------------------------------------*/

//
// Create a unique name by combining a base name and an arbitrary number string.
// The object name returned is guaranteed not to exist.
//
FName UObject::MakeUniqueObjectName( UObject* Parent, UClass* Class )
{
	guard(UObject::MakeUniqueObjectName);
	check(Class);
	
	TCHAR NewBase[NAME_SIZE], Result[NAME_SIZE];
	TCHAR TempIntStr[NAME_SIZE];

	// Make base name sans appended numbers.
	appStrcpy( NewBase, Class->GetName() );
	TCHAR* End = NewBase + appStrlen(NewBase);
	while( End>NewBase && appIsDigit(End[-1]) )
		End--;
	*End = 0;

	// Append numbers to base name.
	do
	{
		appSprintf( TempIntStr, TEXT("%i"),(DWORD)Class->ClassUnique++ );
		appStrncpy( Result, NewBase, NAME_SIZE-appStrlen(TempIntStr)-1 );
		appStrcat( Result, TempIntStr );
	} while( StaticFindObject( NULL, Parent, Result ) );

	return Result;
	unguard;
}

/*-----------------------------------------------------------------------------
	Object hashing.
-----------------------------------------------------------------------------*/

//
// Add an object to the hash table.
//
void UObject::HashObject()
{
	guard(UObject::HashObject);

	INT iHash       = GetObjectHash( Name );
	HashNext        = GObjHash[iHash];
	GObjHash[iHash] = this;

	//jg -- Add to hash with the outer
	iHash			= GetObjectOuterHash(Name,(DWORD)(PTRINT)Outer);
	HashOuterNext	= GObjHashOuter[iHash];
	GObjHashOuter[iHash] = this;

	unguard;
}

//
// Remove an object from the hash table.
//
void UObject::UnhashObject( INT OuterIndex )
{
	guard(UObject::UnhashObject);
	INT       iHash   = GetObjectHash( Name );
	UObject** Hash    = &GObjHash[iHash];
	INT       Removed = 0;
	while( *Hash != NULL )
	{
		if( *Hash != this )
		{
			Hash = &(*Hash)->HashNext;
 		}
		else
		{
			*Hash = (*Hash)->HashNext;
			Removed++;
		}
	}
	check(Removed!=0);
	check(Removed==1);
	//jg -- Remove from the outer hash
	iHash   = GetObjectOuterHash(Name,(DWORD)(PTRINT)Outer);
	Hash    = &GObjHashOuter[iHash];
	Removed = 0;
	while( *Hash != NULL )
	{
		if( *Hash != this )
		{
			Hash = &(*Hash)->HashOuterNext;
 		}
		else
		{
			*Hash = (*Hash)->HashOuterNext;
			Removed++;
		}
	}
	// If this failed, do a brute force search (icky). This has never
	// happened in testing, but just in case
	if (Removed == 0)
	{
		for (INT Index = 0; Index < OBJECT_HASH_BINS && Removed == 0;
			Index++)
		{
			Hash = &GObjHashOuter[Index];
			while( *Hash != NULL )
			{
				if( *Hash != this )
				{
					Hash = &(*Hash)->HashOuterNext;
 				}
				else
				{
					*Hash = (*Hash)->HashOuterNext;
					Removed++;
				}
			}
		}
	}
#if 0
	if (Removed == 0)
	{
		debugf(TEXT("Failed to remove %s from the OuterHash"),*Name);
	}
#endif
	unguard;
}

/*-----------------------------------------------------------------------------
	Creating and allocating data for new objects.
-----------------------------------------------------------------------------*/

//
// Add an object to the table.
//
void UObject::AddObject( INT InIndex )
{
	guard(UObject::AddObject);

	// Find an available index.
	if( InIndex==INDEX_NONE )
	{
		if( GObjAvailable.Num() )
		{
			InIndex = GObjAvailable.Pop();
			check(GObjObjects(InIndex)==NULL);
		}
		else InIndex = GObjObjects.Add();
	}

	// Add to global table.
	GObjObjects(InIndex) = this;
	Index = InIndex;
	HashObject();

	unguard;
}

//
// Create a new object or replace an existing one.
// If Name is NAME_None, tries to create an object with an arbitrary unique name.
// No failure conditions.
//
UObject* UObject::StaticAllocateObject
(
	UClass*			InClass,
	UObject*		InOuter,
	FName			InName,
	DWORD			InFlags,
	UObject*		InTemplate,
	FOutputDevice*	Error,
	UObject*		Ptr,
	UObject*		SuperObject
)
{
	guard(UObject::StaticAllocateObject);
	check(Error);
	check(!InClass || InClass->ClassWithin);
	check(!InClass || InClass->ClassConstructor);

	// Validation checks.
	if( !InClass )
	{
		Error->Logf( TEXT("Empty class for object %s"), *InName );
		return NULL;
	}
	if( InClass->GetIndex()==INDEX_NONE && GObjRegisterCount==0 )
	{
		Error->Logf( TEXT("Unregistered class for %s"), *InName );
		return NULL;
	}
	if( InClass->ClassFlags & CLASS_Abstract )
	{
		Error->Logf( LocalizeError(TEXT("Abstract"),TEXT("Core")), *InName, InClass->GetName() );
		return NULL;
	}
	if( !InOuter && InClass!=UPackage::StaticClass() )
	{
		Error->Logf( LocalizeError(TEXT("NotPackaged"),TEXT("Core")), InClass->GetName(), *InName );
		return NULL;
	}
	if( InOuter && !InOuter->IsA(InClass->ClassWithin) )
	{
		Error->Logf( LocalizeError(TEXT("NotWithin"),TEXT("Core")), InClass->GetName(), *InName, InOuter->GetClass()->GetName(), InClass->ClassWithin->GetName() );
		return NULL;
	}

#ifndef _DEBUG
	//scion jg -- Transient naming fix similar to 2k3.
	if (!GIsEditor && InName == NAME_None &&
		// If its outer is the transient package
		(InOuter == GetTransientPackage() ||
		// Or its outer is contained within the transient package
		InOuter->IsIn(GetTransientPackage()) ||
		// Or it was requested to be spawned as a transient
		InFlags & RF_Transient ||
		// Or its class type is marked as transient
		InClass->ClassFlags & CLASS_Transient ||
		// Or its class type is marked as poolable
		InClass->ClassFlags & CLASS_Poolable ||
		// Or its outer class is marked as transient
		InOuter->GetClass()->ClassFlags & CLASS_Transient ||
		// Or it is a subobject whose super object is transient (defprops)
		(SuperObject && SuperObject->GetFlags() & RF_Transient)))
	{
		InName = NAME_Transient;
	}
#endif

	// Compose name, if public and unnamed.
	if( InName==NAME_None )//oldver?? what about compatibility matching of unnamed objects?
		InName = MakeUniqueObjectName( InOuter, InClass );

#ifndef FINAL_RELEASE
	// Check for name conflicts.
	if( GCheckConflicts && InName!=NAME_None )
		for( UObject* Hash=GObjHash[GetObjectHash(InName)]; Hash!=NULL; Hash=Hash->HashNext )
			if
			(	Hash->GetFName()==InName
			&&	Hash->Outer==InOuter
			&&	Hash->GetClass()!=InClass )
				debugf( NAME_Log, TEXT("CONFLICT: %s - %s"), Hash->GetFullName(), InClass->GetName() );
#endif

	// See if object already exists.
	UObject* Obj = NULL;
	//scion jg -- Transient naming fix similar to 2k3
	if (InName != NAME_Transient && 
		(GIsEditor || ((InClass->ClassFlags & CLASS_Poolable) == 0)) )
	{
		Obj = StaticFindObjectFast( InClass, InOuter, InName );//oldver: Should use NULL instead of InClass to prevent conflicts by name rather than name-class.
	}
	UClass*  Cls                        = Cast<UClass>( Obj );
	INT      Index                      = INDEX_NONE;
	UClass*  ClassWithin				= NULL;
	DWORD    ClassFlags                 = 0;
	void     (*ClassConstructor)(void*) = NULL;
	if( !Obj )
	{
		//scion jg -- Changed object allocation
		// If the caller passed in an object to reuse, reuse it
		// If the class is poolable, get a pooled instance if available
		// Otherwise get the memory from FMalloc
		Obj = Ptr;
		if (Obj == NULL)
		{
			if ((InClass->ClassFlags & CLASS_Poolable) && GIsEditor == 0)
			{
				Obj = GObjPool.GetNextInstance(InClass);
			}
			else
			{
				Obj = (UObject*)appMalloc( InClass->GetPropertiesSize() );
			}
		}
		// Check and see if there was memory or a pooled instance available
		if (Obj == NULL)
		{
			return NULL;
		}
	}
	else
	{
		// Replace an existing object without affecting the original's address or index.
		check(!Ptr || Ptr==Obj);
		debugf( NAME_DevReplace, TEXT("Replacing %s"), Obj->GetName() );

		// Can only replace if class is identical.
		if( Obj->GetClass() != InClass )
			appErrorf( LocalizeError(TEXT("NoReplace"),TEXT("Engine")), Obj->GetFullName(), InClass->GetName() );

		// Remember flags, index, and native class info.
		InFlags |= (Obj->GetFlags() & RF_Keep);
		Index    = Obj->Index;
		if( Cls )
		{
			ClassWithin		 = Cls->ClassWithin;
			ClassFlags       = Cls->ClassFlags & CLASS_Abstract;
			ClassConstructor = Cls->ClassConstructor;
		}

		// Destroy the object.
		Obj->~UObject();
		check(GObjAvailable.Num() && GObjAvailable.Last()==Index);
		GObjAvailable.Pop();
	}

	// If class is transient, objects must be transient.
	if( InClass->ClassFlags & CLASS_Transient )
		InFlags |= RF_Transient;

	// Set the base properties.
	Obj->Index			 = INDEX_NONE;
	Obj->HashNext		 = NULL;
	Obj->StateFrame      = NULL;
	Obj->_Linker		 = NULL;
	Obj->_LinkerIndex	 = INDEX_NONE;
	Obj->Outer			 = InOuter;
	Obj->ObjectFlags	 = InFlags;
	//scion jg -- Transient naming fix similar to 2k3
	Obj->Name			 = InName == NAME_Transient ? InClass->GetFName() : InName;
	Obj->Class			 = InClass;

	// Init the properties.
	InitProperties( (BYTE*)Obj, InClass->GetPropertiesSize(), InClass, (BYTE*)InTemplate, InClass->GetPropertiesSize(), (InFlags&RF_NeedLoad) ? NULL : Obj, SuperObject );

	// Add to global table.
	Obj->AddObject( Index );
	check(Obj->IsValid());

	// Restore class information if replacing native class.
	if( Cls )
	{
		Cls->ClassWithin	   = ClassWithin;
		Cls->ClassFlags       |= ClassFlags;
		Cls->ClassConstructor  = ClassConstructor;
	}

	// Success.
	return Obj;
	unguardf(( TEXT("(%s %s)"), InClass ? InClass->GetName() : TEXT("NULL"), *InName ));
}

//
// Construct an object.
//
UObject* UObject::StaticConstructObject
(
	UClass*			InClass,
	UObject*		InOuter,
	FName			InName,
	DWORD			InFlags,
	UObject*		InTemplate,
	FOutputDevice*	Error,
	UObject*		SuperObject
)
{
	guard(UObject::StaticConstructObject);
	check(Error);

	// Allocate the object.
	UObject* Result = StaticAllocateObject( InClass, InOuter, InName, InFlags, InTemplate, Error, NULL, SuperObject );
	if( Result )
	{
		(*InClass->ClassConstructor)( Result );

		// If config is per-object, load it.
		// rjp - moved from StaticAllocateObject, as LoadConfig is now virtual, 
		// so if must be called after the Object's vtable has been initialized
		// DELTA DEFAULTS
		if( InClass->ClassFlags & CLASS_PerObjectConfig || InClass->ClassFlags & CLASS_TaggedDefaults )
		{
			Result->LoadConfig();
			Result->LoadLocalized(); 
		}
	}

	return Result;

	unguard;
}

/*-----------------------------------------------------------------------------
   Garbage collection.
-----------------------------------------------------------------------------*/

//
// Serialize the global root set to an archive.
//
void UObject::SerializeRootSet( FArchive& Ar, DWORD KeepFlags, DWORD RequiredFlags )
{
	guard(UObject::SerializeRootSet);
	Ar << GObjRoot;
	for( FObjectIterator It; It; ++It )
	{
		if
		(	(It->GetFlags() & KeepFlags)
		&&	(It->GetFlags()&RequiredFlags)==RequiredFlags )
		{
			UObject* Obj = *It;
			Ar << Obj;
		}
	}
	unguard;
}

//
// Archive for finding unused objects.
//
class FArchiveTagUsed : public FArchive
{
public:
	FArchiveTagUsed()
	: Context( NULL )
#if ITERATIVE_GC
	, NdxBuildingArray( 0 )
#endif ITERATIVE_GC
	{
		guard(FArchiveTagUsed::FArchiveTagUsed);
		GGarbageRefCount=0;

		// Tag all objects as unreachable.
		for( FObjectIterator It; It; ++It )
			It->SetFlags( RF_Unreachable | RF_TagGarbage );

		// Tag all names as unreachable.
		for( INT i=0; i<FName::GetMaxNames(); i++ )
			if( FName::GetEntry(i) )
				FName::GetEntry(i)->Flags |= RF_Unreachable;

		unguard;
	}
#if ITERATIVE_GC

	UBOOL ProcessMarkPass()
	{
		// Set the build array to be the working array and prepare the (old) working array to be the building array.
		// Then, go through all objects in the (new) working array and serialize them to this FArchive, marking them as used and creating the build array.
		// Returns true so long as more objects want processing.
		
		check( ArrayObjects[ NdxBuildingArray ].Num() > 0 );
		GIsProcessMarkPass=1;

		// Swap the roles of the arrays.
		INT NdxWorkingArray = NdxBuildingArray;
		NdxBuildingArray = NdxWorkingArray ? 0 : 1;

		// Prepare the building array for another pass. Empty it, but avoid Realloc() because the next pass will probably be just about the same size.
		ArrayObjects[ NdxBuildingArray ].Reset();

		// Process all objects in the working array.

		INT nWorkingObjects = ArrayObjects[ NdxWorkingArray ].Num();
		for( INT i = 0; i < nWorkingObjects; ++i )
		{
			UObject* Obj = ArrayObjects[ NdxWorkingArray ]( i );

			Obj->Serialize( *this );

			if( !(Obj->GetFlags() & RF_DebugSerialize) )
			{
				appErrorf( TEXT("%s failed to route Serialize"), Obj->GetFullName() );
			}
		}

		GIsProcessMarkPass=0;
		return ArrayObjects[ NdxBuildingArray ].Num() > 0;
	}

#endif
private:
	FArchive& operator<<( UObject*& Object )
	{
		guardSlow(FArchiveTagUsed<<Obj);
		GGarbageRefCount++;

		// Object could be a misaligned pointer.
		// Copy the contents of the pointer into a temporary and work on that.
		UObject* Obj;
		appMemcpy(&Obj, &Object, sizeof(UObject*));	// a pointer's width
		
#if DO_GUARD_SLOW
		guard(CheckValid);
		if( Obj )
			check(Obj->IsValid());
		unguard;
#endif
		if( Obj && (Obj->GetFlags() & RF_EliminateObject) )
		{
			// Dereference it.
			Obj = NULL;
		}
		else if( Obj && (Obj->GetFlags() & RF_Unreachable) )
		{
			// Only recurse the first time object is claimed.
			guard(TestReach);
			Obj->ClearFlags( RF_Unreachable | RF_DebugSerialize );

#if ITERATIVE_GC

			// Add this object to the array of objects to serialize for used children.
			if( Obj->GetFlags() & RF_TagGarbage )
			{
				ArrayObjects[ NdxBuildingArray ].AddItem( Obj );	// TODO AddItem() is correct method?
			}

#else		// Recursive, old-style garbage collection.

			// Recurse.
			if( Obj->GetFlags() & RF_TagGarbage )
			{
				// Recurse down the object graph.
				UObject* OriginalContext = Context;
				Context = Obj;
				Obj->Serialize( *this );
				if( !(Obj->GetFlags() & RF_DebugSerialize) )
					appErrorf(( TEXT("%s failed to route Serialize"), Obj->GetFullName(TEXT("")) ));
				Context = OriginalContext;
			}
			else
			{
				// For debugging.
				debugf(( NAME_Log, TEXT("%s is referenced by %s"), Obj->GetFullName(TEXT("")), Context ? Context->GetFullName(TEXT("")) : NULL ));
			}
#endif		//ITERATIVE_GC
			unguardf(( TEXT("(%s)"), Obj->GetFullName() ));
		}

		// Contents of pointer might have been modified.
		// Copy the results back into the misaligned pointer.
		appMemcpy(&Object, &Obj, sizeof(UObject*));
		return *this;
		unguardSlow;
	}
	FArchive& operator<<( FName& Name )
	{
		guardSlow(FArchiveTagUsed::Name);

		// Name is probably not aligned correctly.
		// Memcpy workaround copies into aligned temporary then back.
		FName LocalName;
		appMemcpy(&LocalName, &Name, sizeof(FName));
		LocalName.ClearFlags( RF_Unreachable );
		appMemcpy(&Name, &LocalName, sizeof(FName));

		return *this;
		unguardSlow;
	}
	UObject* Context;
#if ITERATIVE_GC

	TArray< UObject* > ArrayObjects[ 2 ];		// Two arrays for use in garbage collection; one being worked on in order to build the other.
	INT NdxBuildingArray;

#endif
};

//
// Purge garbage.
//
void UObject::PurgeGarbage( DWORD RemoveFlags, DWORD KeepFlags )
{
	guard(UObject::PurgeGarbage);	
	if( GNoGC )
	{
		debugf( NAME_DevGarbage, TEXT("Not purging garbage") );
		return;
	}
	debugf( NAME_DevGarbage, TEXT("Purging garbage") );

	// scion sz - remove all unreachable objects/names except for any natives ones
	RemoveObjects( RemoveFlags, KeepFlags );
	RemoveNames( RF_Unreachable, KeepFlags|RF_Native );

	unguard;
}

//
// Delete all unreferenced objects.
//
// scion sz -
// bOnlyUnreachables=1 uses default GC behavior of only
// removing objects that are not tagged during GC serialization
//
void UObject::CollectGarbage( DWORD KeepFlags, UBOOL bOnlyUnreachables )
{
	guard(UObject::CollectGarbage);
	debugf( NAME_DevGarbage, TEXT("Collecting garbage") );

#ifndef _XBOX
	// always keep RF_Native objectws
	KeepFlags |= RF_Native;
#endif

	if( !bOnlyUnreachables )
	{
		// scion sz - flag all objects that don't match KeepFlags as RF_EliminateObject
		// so that any references to these objects will be nulled out during the
		// FArchiveTagUsed serialization step
		for( INT i=0; i<GObjObjects.Num(); i++ )
		{
			UObject* Obj = GObjObjects(i);
			if(	Obj && 
				!(Obj->GetFlags() & KeepFlags) )
			{
				Obj->SetFlags( RF_EliminateObject );
			}
		}
	}

	// Tag and purge garbage.
	FArchiveTagUsed TagUsedAr;
	SerializeRootSet( TagUsedAr, KeepFlags, RF_TagGarbage );

#if ITERATIVE_GC

	// Iteratively pass through objects, marking them as used and registering objects they refer to for processing on a subsequent iteration.
	while( TagUsedAr.ProcessMarkPass() )
	{
		// Intentionally blank.
	}

	// TODO Allow a limitation on the number of ProcessMarkPass() calls executed at one time, so that GC can be run in small doses throughout the game.

#endif ITERATIVE_GC

	if( bOnlyUnreachables )
	{
		// default GC behavior 
		// purge only objects that were marked as unreachable and that are not native
		PurgeGarbage( RF_Unreachable, KeepFlags );
	}
	else
	{
		// actually remove all objects that don't match the keep flags	
		PurgeGarbage( RF_ALL, KeepFlags );
	}

	unguard;
}

/* scion ======================================================================
 * UObject::RemoveObjects
 * Author: sz
 * 
 * Remove any objects that match RemoveFlags except for the ones that match
 * KeepFlags. First calls destroy on the object and then deletes it.
 * ============================================================================
 */
void UObject::RemoveObjects( DWORD RemoveFlags, DWORD KeepFlags )
{
	guard(UObject::RemoveObjects);
	INT CountBefore=0, CountPurged=0;

	GIsGarbageCollecting = 1; // Set 'I'm garbage collecting' flag - might be checked inside UObject::Destropy etc.

	//scion jg -- Empty the object pool so any Object's UClass won't be
	// deleted beneath it
	GObjPool.Empty();

	// Dispatch all Destroy messages.
	guard(DispatchDestroys);
	for( INT i=0; i<GObjObjects.Num(); i++ )
	{
		guard(DispatchDestroy);
		CountBefore += (GObjObjects(i)!=NULL);
		if
		(	GObjObjects(i)
		&&	(GObjObjects(i)->GetFlags() & RemoveFlags)
		&&  (!(GObjObjects(i)->GetFlags() & KeepFlags) || GExitPurge) )
		{
			debugf( NAME_DevGarbage, TEXT("Garbage collected object %i: %s"), i, GObjObjects(i)->GetFullName() );
			GObjObjects(i)->ConditionalDestroy();
			CountPurged++;
		}
		unguardf(( TEXT("(%i: %s)"), i, GObjObjects(i)->GetName() ));
	}

	unguard;

	// Purge all destroyed objects.
	//warning: Can't use FObjectIterator here because classes may be destroyed before objects.
	FName DeleteName=NAME_None;
	guard(DeleteGarbage);
	for( INT i=0; i<GObjObjects.Num(); i++ )
	{
		guard(DeleteObject);
		if
		(	GObjObjects(i) &&			
			(GObjObjects(i)->GetFlags() & RemoveFlags) &&
			!(GObjObjects(i)->GetFlags() & KeepFlags)  )
		{
			DeleteName = GObjObjects(i)->GetFName();
			delete GObjObjects(i);
		}
		unguardf(( TEXT("(%i)"), i ));
	}
	unguardf(( TEXT("(%s)"), *DeleteName ));

	GIsGarbageCollecting = 0; // Unset flag

	debugf(NAME_DevGarbage, TEXT("Removed objects: %i->%i; refs: %i"),CountBefore,CountBefore-CountPurged,GGarbageRefCount);
	unguard;
}

/* scion ======================================================================
 * UObject::RemoveNames
 * Author: sz
 * 
 * Remove any names that match RemoveFlags except for the ones that match
 * KeepFlags.
 * ============================================================================
 */
void UObject::RemoveNames( DWORD RemoveFlags, DWORD KeepFlags )
{
	guard(UObject::RemoveNames);

#if 0
	GConfig->Flush(0);
#endif

	// The config cache is dropped by UnloadAll() below, and with it every value that was set but not written
	// yet. SetString (appApplyIniOverrides' `-ini:` switches, FURL::SaveURLConfig's [DefaultPlayer]
	// name/class/weapons, the launcher's resolution switches) only marks the ini dirty; only SaveConfig flushes.
	// A garbage purge runs at every level change and in StaticExit, before appExit's own flush, and on the web
	// nothing else writes the inis before the tab is closed: write the dirty files first, while the FNames the
	// cache is keyed by are still valid. (The Xbox never writes inis.)
#ifndef _XBOX
	if( GConfig )
		GConfig->Flush( 0 );
#endif

	for( INT i=0; i<FName::GetMaxNames(); i++ )
	{
		FNameEntry* Name = FName::GetEntry(i);
		if
		(	(Name)
		&&	(Name->Flags & RemoveFlags)
		&& !(Name->Flags & KeepFlags     ) )
		{
			debugf( NAME_DevGarbage, TEXT("Garbage collected name %i: %s"), i, Name->GetName() );
			FName::DeleteEntry(i);
		}
	}

	//epic moelfke: need to nuke the config cache since the names it contains may no longer be valid
	GConfig->UnloadAll();

	unguard;
}

//
// Returns whether an object is referenced, not counting the
// one reference at Obj. No side effects.
//
UBOOL UObject::IsReferenced( UObject*& Obj, DWORD KeepFlags, UBOOL IgnoreReference )
{
	guard(UObject::RefCount);

	// Remember it.
	UObject* OriginalObj = Obj;
	if( IgnoreReference )
		Obj = NULL;

	// Tag all garbage.
	FArchiveTagUsed TagUsedAr;
	OriginalObj->ClearFlags( RF_TagGarbage );
	SerializeRootSet( TagUsedAr, KeepFlags, RF_TagGarbage );

#if ITERATIVE_GC

	// Iteratively pass through objects, marking them as used and registering objects they refer to for processing on a subsequent iteration.
	while( TagUsedAr.ProcessMarkPass() )
	{
		// Intentionally blank.
	}

	// TODO Allow a limitation on the number of ProcessMarkPass() calls executed at one time, so that GC can be run in small doses throughout the game.

#endif ITERATIVE_GC

	// Stick the reference back.
	Obj = OriginalObj;

	// Return whether this is tagged.
	return (Obj->GetFlags() & RF_Unreachable)==0;
	unguard;
}

//
// Attempt to delete an object. Only succeeds if unreferenced.
//
UBOOL UObject::AttemptDelete( UObject*& Obj, DWORD KeepFlags, UBOOL IgnoreReference )
{
	guard(UObject::AttemptDelete);
	if( !(Obj->GetFlags() & RF_Native) && !IsReferenced( Obj, KeepFlags, IgnoreReference ) )
	{
		PurgeGarbage( RF_ALL, KeepFlags|RF_Native );
		return 1;
	}
	else return 0;
	unguard;
}

/*-----------------------------------------------------------------------------
	Importing and exporting.
-----------------------------------------------------------------------------*/

//
// Import an object from a file.
//
void UObject::ParseParms( const TCHAR* Parms )
{
	guard(ParseObjectParms);
	if( !Parms )
		return;
	for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(GetClass()); It; ++It )
	{
		if( It->GetOuter()!=UObject::StaticClass() )
		{
			FString Value;
			if( Parse(Parms,*(FString(It->GetName())+TEXT("=")),Value) )
				It->ImportText( *Value, (BYTE*)this + It->Offset, PPF_Localized );
		}
	}
	unguard;
}

/*-----------------------------------------------------------------------------
	Driver support.
-----------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------------
	File order.

	On Epic's machine FindFiles returned files in NTFS directory order: names compared code unit by code unit
	after upper-casing (NTFS's upcase table; for these ASCII names, so '_' sorts after the letters). readdir()
	order on Linux is the file system's (ext4: hash order), and the order matters: UMakeCommandlet creates
	classes in .uc file order; CacheDrivers below reads the localization files in this order, and reading a
	file creates the FNames of its sections and keys, so the order fixes their FName indices, which decide the
	order of equal-usage names that a conformed save appends.
-----------------------------------------------------------------------------*/

static INT UC2CompareNTFS( const FString& A, const FString& B )
{
	const TCHAR* a = *A;
	const TCHAR* b = *B;
	for( ; ; a++, b++ )
	{
		BYTE ca = (BYTE)appToUpper(*a), cb = (BYTE)appToUpper(*b);
		if( ca!=cb )
			return ca<cb ? -1 : 1;
		if( !ca )
			return 0;
	}
}
CORE_API void UC2SortLikeNTFS( TArray<FString>& Files )
{
	for( INT i=1; i<Files.Num(); i++ )
		for( INT j=i; j>0 && UC2CompareNTFS(Files(j-1),Files(j))>0; j-- )
			Exchange( Files(j-1), Files(j) );
}

//scion ===========================================================
//	SCION Function: CacheDrivers
//	Author: superville
//
//	Description: Cache a list of globally registered objects into memory
//		(Including those defined in PUBLIC localized file sections)
//		Also register all language classes through localization folder
//		hierarchy.
//
//	Input: UBOOL ForceRefresh - Cache even if language has not changed
//	Last Modified: 01/13/04 (superville)
// ================================================================
void UObject::CacheDrivers( UBOOL ForceRefresh )
{
	guard(UObject::CacheDrivers);
	const INT BUFF_SIZE = 32 * 1024;
	//jg -- This is a bit weak, but it keeps the stack small
	TCHAR* Buffer = NEW TCHAR[BUFF_SIZE];
	// Auto delete the pointer on scope change
	TScopePointer<TCHAR> ScopeBuffer(Buffer);

	// If force or the language hasn't been cached
	if( ForceRefresh || appStricmp(GObjCachedLanguage,UObject::GetLanguage())!=0 )
	{
		// Store cached language extension
		appStrcpy( GObjCachedLanguage, UObject::GetLanguage() );

		// Empty all cached info
		GObjPreferences.Empty();
		GObjDrivers.Empty();

		// For each system path
		TCHAR Filename[256];

		// Build a list of all localization files in the default (international)
		// localization folder.  Then look through the current language localization
		// folder to find the PUBLIC sections.  If the file or section doesn't exist
		// in the current language folder, go back to the default and use the PUBLIC
		// information there.

		// Format Filename to be the DEFAULT language localization folder
		appSprintf( Filename, TEXT("%s*.%s"), UObject::GetDefaultLocalizationFolder(), GLanguageExt[LC_INT] );

		// Gather list of all files in the default localization folder
		// with the default language extension
		TArray<FString> Files = GFileManager->FindFiles( Filename, 1, 0 );
		UC2SortLikeNTFS( Files ); // Windows' order, on every file system

		// For each file in the list
		for( INT j = 0; j < Files.Num(); j++ )
		{
			// Copy the current filename into our working TCHAR array
			appSprintf( Filename, TEXT("%s%s"), UObject::GetLocalizationFolder(), *Files(j) );

			// Overwrite the .int part with the current language extension
			TCHAR* End = Filename + appStrlen( Filename ) - 4;
			appSprintf( End, TEXT(".%s"), GObjCachedLanguage );

			// Find the PUBLIC section in the localized version of this file
			// might fail if the file (Core.jap) doesn't exist, or the file doesn't have a public section
			UBOOL Success = GConfig->GetSection( TEXT("Public"), Buffer, BUFF_SIZE, Filename );

			// If PUBLIC section could not be found, search for the public section in
			// the corresponding english version of this file, unless the current language is english
			// in which case, this would be redundant
			if( !Success && appStricmp(GObjCachedLanguage,GLanguageExt[LC_INT]) )
			{
				// Rebuild path using base path
				appSprintf( Filename, TEXT("%s%s"), UObject::GetDefaultLocalizationFolder(), *Files(j) );

				// Try to find PUBLIC section again
				Success = GConfig->GetSection( TEXT("Public"), Buffer, BUFF_SIZE, Filename );
			}

			// If the PUBLIC section was found
			if( Success )
			{
				// Register the object.
				RegisterObject(Buffer);
			}
		}

#ifdef _XBOX
		// Find all .int files in downloadable content directories.
		Files = GFileManager->GetContentLocalizationFiles();

		for (INT Index = 0; Index < Files.Num(); Index++)
		{
			const FString& IntFile = Files(Index);

			// Copy the name of the .int file into the filename buffer.
			appStrcpy(Filename, *IntFile);

			// Overwrite the .int part with the current language extension.
			TCHAR* End = Filename + appStrlen(Filename) - 4;
			appSprintf(End, TEXT(".%s"), GObjCachedLanguage);

			// Find the public section in the localized version of this file.
			// This might fail if the file doesn't exist or the file doesn't have a public section.
			UBOOL Success = GConfig->GetSection(TEXT("Public"), Buffer, BUFF_SIZE, Filename);

			// If the public section could not be found, search for the public section in the
			// corresponding english version of this file, unless the current language is English
			// in which case, this would be redundant.
			if(!Success && appStricmp(GObjCachedLanguage, GLanguageExt[LC_INT]))
			{
				// Rebuild path using base path.
				appStrcpy(Filename, *IntFile);

				// Try to find the public section again.
				Success = GConfig->GetSection(TEXT("Public"), Buffer, BUFF_SIZE, Filename);
			}

			// If the public section was found
			if(Success)
			{
				// Register the object.
				RegisterObject(Buffer);
			}
		}
#endif

		// Get a list of all localization directories
		TArray<FString> CoreLangs = GFileManager->FindFiles( TEXT("Localization\\*"), 0, 1 );
		UC2SortLikeNTFS( CoreLangs );

		// For each directory
		for( INT i = 0; i < CoreLangs.Num(); i++ ) {
			// Register that directory name as a language
			FRegistryObjectInfo& Info = *new(GObjDrivers)FRegistryObjectInfo;
			Info.Object		= CoreLangs(i);
			Info.Class		= TEXT("Class");
			Info.MetaClass	= ULanguage::StaticClass()->GetPathName();
		}
	}

	unguard;
}

void UObject::RegisterObject(TCHAR* Buffer)
{
	guard(UObject::RegisterObject);

	TCHAR* Next;

	// Loop through each key entry in the buffer
	for (TCHAR* Key = Buffer; *Key; Key=Next)
	{
		// Assign next pointer to be one step after end of line marker
		Next = Key + appStrlen(Key) + 1;

		// Get the value of the current key
		TCHAR* Value = appStrstr(Key,TEXT("="));

		// If Value is valid
		if( Value )
		{
			// Seperate the Key and Value by setting the '=' to null terminate
			// and update Value pointer to next char
			*Value++ = 0;

			// Strip prefixed open paren (if any) from value and udpate pointer to next value
			if( *Value=='(' )
				*Value++ = 0;

			// Strip postfixed close paren (if any) from value
			if( *Value && Value[appStrlen(Value)-1]==')' )
				Value[appStrlen(Value)-1] = 0;

			// If Key was an OBJECT
			if( appStricmp(Key,TEXT("Object"))==0 )
			{
				// Register the object and parse values directly into registry info
				FRegistryObjectInfo& Info = *new(GObjDrivers)FRegistryObjectInfo;
				Parse( Value, TEXT("Name="), Info.Object );
				Parse( Value, TEXT("Class="), Info.Class );
				Parse( Value, TEXT("MetaClass="), Info.MetaClass );
				Parse( Value, TEXT("Description="), Info.Description );
				Parse( Value, TEXT("Autodetect="), Info.Autodetect );
			}
			else if( appStricmp(Key,TEXT("Preferences"))==0 )
			{
				// Otherwise, if Key was PREFERENCES

				// Register the preferences and parse values directly into preference info
				FPreferencesInfo& Info = *new(GObjPreferences)FPreferencesInfo;
				Parse( Value, TEXT("Caption="), Info.Caption );
				Parse( Value, TEXT("Parent="), Info.ParentCaption );
				Parse( Value, TEXT("Class="), Info.Class );
				Parse( Value, TEXT("Category="), Info.Category );
				ParseUBOOL( Value, TEXT("Immediate="), Info.Immediate );
				Info.Category.SetFlags( RF_Native );
			}
		}
	}	

	unguard;
}

//
// Get a list of globally registered objects complying to
// the specified class and optional metaclass (base class of UClasses).
//
void UObject::GetRegistryObjects
(
	TArray<FRegistryObjectInfo>&	Results,
	UClass*							Class,
	UClass*							MetaClass,
	UBOOL							ForceRefresh
)
{
	guard(UObject::GetDriverClasses);
	check(Class);
	check(Class!=UClass::StaticClass() || MetaClass);
	CacheDrivers( ForceRefresh );
	const TCHAR* ClassName = Class->GetName();
	const TCHAR* MetaClassName = MetaClass ? MetaClass->GetPathName() : TEXT("");
	for( INT i=0; i<GObjDrivers.Num(); i++ )
	{
		if
		(	appStricmp(*GObjDrivers(i).Class, ClassName)==0
		&&	appStricmp(*GObjDrivers(i).MetaClass, MetaClassName)==0 )
			new(Results)FRegistryObjectInfo(GObjDrivers(i));
	}
	unguard;
}

//
// Get a list of everything that should show up in the hierarchical
// preferences window.
//
void UObject::GetPreferences( TArray<FPreferencesInfo>& Results, const TCHAR* ParentCaption, UBOOL ForceRefresh )
{
	guard(UObject::GetPreferences);
	CacheDrivers( ForceRefresh );
	Results.Empty();
	for( INT i=0; i<GObjPreferences.Num(); i++ )
		if( appStricmp(*GObjPreferences(i).ParentCaption,ParentCaption)==0 )
			new(Results)FPreferencesInfo(GObjPreferences(i));
	unguard;
}

static void FindObjRefs(UObject *checkObj)
{
	debugf(TEXT("####### searching for ref to (%s)"),checkObj->GetFullName());
	for (FObjectIterator It; It; ++It)
	{
		// Iterate it's object properties and figure out if there is a
		// reference to this destroyed object in an object's properties
		for (TFieldIterator<UProperty,CLASS_IsAUProperty>
			FieldIt(It->GetClass()); FieldIt; ++FieldIt)
		{
			// If this is an object property
			if (Cast<UObjectProperty>(*FieldIt,CLASS_IsAUObjectProperty))
			{
				// Loop through static arrays of objects
				for (INT Index = 0; Index < FieldIt->ArrayDim; Index++)
				{
					// Get the object pointer
					UObject* Obj = *(UObject**)(((BYTE*)*It) +
						FieldIt->Offset + (Index * FieldIt->ElementSize));
					// If they match, bad things will happen once the
					// actor in question is deleted
					if (Obj == checkObj)
					{
						debugf(TEXT("######## (%s) ref'd by (%s) [property: %s]"),
							checkObj->GetFullName(),It->GetFullName(),FieldIt->GetFullName());
					}
				}
			}
			else
			{
				// Check for a dyn array of objects
				UArrayProperty* ArrayProp = Cast<UArrayProperty>(*FieldIt);
				if (ArrayProp && Cast<UObjectProperty>(ArrayProp->Inner,
					CLASS_IsAUObjectProperty))
				{
					// Get the dynamic array pointer
					FArray* Array = (FArray*)(((BYTE*)*It) + FieldIt->Offset);
					// Now iterate through each element and check
					for (INT Index = 0; Index < Array->Num(); Index++)
					{
						// Get the object at the specified index
						UObject* Obj = *(UObject**)((BYTE*)Array->GetData() +
							Index * ArrayProp->Inner->ElementSize);
						// If they match, bad things will happen once the
						// actor in question is deleted
						if (Obj == checkObj)
						{
							debugf(TEXT("######## (%s) ref'd by (%s) [property: %s]"),
								checkObj->GetFullName(),It->GetFullName(),FieldIt->GetFullName());
						}
					}
				}
			}
		}
	}
}

/* scion ======================================================================
 * UObject::CheckDanglingRefs
 * Author: jg
 *
 * Searches through all objects checking for any object that still maintains a
 * reference to the passed in object. Slow as hell, but faster than chasing
 * crashes down.
 *
 * input:	pDeadObject -- The object that is deemed dead and not usable
 *
 * ============================================================================
 */
void UObject::CheckDanglingRefs(UObject* pDeadObject)
{
	guard(UObject::CheckDanglingRefs);
	check(pDeadObject);
	// Iterate all objects and check for object still holding a ref to this
	// object
	for (FObjectIterator It; It; ++It)
	{
		// Iterate it's object properties and figure out if there is a
		// reference to this destroyed object in an object's properties
		for (TFieldIterator<UProperty,CLASS_IsAUProperty>
			FieldIt(It->GetClass()); FieldIt; ++FieldIt)
		{
			// If this is an object property
			if (Cast<UObjectProperty>(*FieldIt,CLASS_IsAUObjectProperty))
			{
				// Loop through static arrays of objects
				for (INT Index = 0; Index < FieldIt->ArrayDim; Index++)
				{
					// Get the object pointer
					UObject* Obj = *(UObject**)(((BYTE*)*It) +
						FieldIt->Offset + (Index * FieldIt->ElementSize));
					// If they match, bad things will happen once the
					// actor in question is deleted
					if (Obj == pDeadObject)
					{
						debugf(TEXT("########Reference to destroyed object (%s) [property: %s] [obj: %s]"),
							pDeadObject->GetFullName(),FieldIt->GetFullName(),It->GetFullName());
						FindObjRefs(*It);
					}
				}
			}
			else
			{
				// Check for a dyn array of objects
				UArrayProperty* ArrayProp = Cast<UArrayProperty>(*FieldIt);
				if (ArrayProp && Cast<UObjectProperty>(ArrayProp->Inner,
					CLASS_IsAUObjectProperty))
				{
					// Get the dynamic array pointer
					FArray* Array = (FArray*)(((BYTE*)*It) + FieldIt->Offset);
					// Now iterate through each element and check
					for (INT Index = 0; Index < Array->Num(); Index++)
					{
						// Get the object at the specified index
						UObject* Obj = *(UObject**)((BYTE*)Array->GetData() +
							Index * ArrayProp->Inner->ElementSize);
						// If they match, bad things will happen once the
						// actor in question is deleted
						if (Obj == pDeadObject)
						{
							debugf(TEXT("########Reference to destroyed object (%s) [property: %s]"),
								pDeadObject->GetFullName(),FieldIt->GetFullName());
							FindObjRefs(*It);
						}
					}
				}
			}
		}
	}
	unguard;
}

/* scion ======================================================================
 * UObject::CheckDanglingOuter
 * Author: jg
 *
 * Verify that no objects have this object as an outer. This is a very slow
 * safety net that should only be used outside of production
 *
 * input:	pDeadObject -- The object that is deemed dead and not usable
 *
 * ============================================================================
 */
void UObject::CheckDanglingOuter(UObject* pDeadObject)
{
	guard(UObject::CheckDanglingOuter);
	check(pDeadObject);
	// Iterate all objects and check for an outer of this actor
	for (FObjectIterator It; It; ++It)
	{
		// This is mega bad (GC crash forthcoming) so log it
		if (pDeadObject == It->GetOuter())
		{
			debugf(TEXT("******Object (%s) has a destroyed outer of (%s)"),
				It->GetFullName(),pDeadObject->GetFullName());
		}
	}
	unguard;
}

/*-----------------------------------------------------------------------------
	UTextBuffer implementation.
-----------------------------------------------------------------------------*/

UTextBuffer::UTextBuffer( const TCHAR* InText )
: Text( InText )
{}
void UTextBuffer::Serialize( const TCHAR* Data, EName Event )
{
	guard(UTextBuffer::Serialize);
	Text += (TCHAR*)Data;
	unguardobj;
}
void UTextBuffer::Serialize( FArchive& Ar )
{
	guard(UTextBuffer::Serialize);
	Super::Serialize(Ar);
	Ar << Pos << Top << Text;
	unguardobj;
}
IMPLEMENT_CLASS(UTextBuffer);

/*-----------------------------------------------------------------------------
	UEnum implementation.
-----------------------------------------------------------------------------*/

UEnum::UEnum( UEnum* InSuperEnum )
: UField( InSuperEnum )
{}
void UEnum::Serialize( FArchive& Ar )
{
	guard(UEnum::Serialize);
	Super::Serialize(Ar);
	Ar << Names;
	unguardobj;
}
IMPLEMENT_CLASS(UEnum);

/*-----------------------------------------------------------------------------
	FCompactIndex implementation.
-----------------------------------------------------------------------------*/

//
// FCompactIndex serializer.
//
FArchive& operator<<( FArchive& Ar, FCompactIndex& I )
{
	guard(FCompactIndex<<);
	if( !Ar.IsLoading() && !Ar.IsSaving() )
	{
		Ar << I.Value;
	}
	else
	{
		//jg -- Load/Save as an INT for speed
		if (Ar.Ver() >= 145)
		{
			Ar << I.Value;
		}
		// Load/save the old compressed slow way
		else
		{
			INT   Original = I.Value;
			DWORD V        = Abs(I.Value);
			BYTE  B0       = ((I.Value>=0) ? 0 : 0x80) + ((V < 0x40) ? V : ((V & 0x3f)+0x40));
			I.Value        = 0;
			Ar << B0;
			if( B0 & 0x40 )
			{
				V >>= 6;
				BYTE B1 = (V < 0x80) ? V : ((V & 0x7f)+0x80);
				Ar << B1;
				if( B1 & 0x80 )
				{
					V >>= 7;
					BYTE B2 = (V < 0x80) ? V : ((V & 0x7f)+0x80);
					Ar << B2;
					if( B2 & 0x80 )
					{
						V >>= 7;
						BYTE B3 = (V < 0x80) ? V : ((V & 0x7f)+0x80);
						Ar << B3;
						if( B3 & 0x80 )
						{
							V >>= 7;
							BYTE B4 = V;
							Ar << B4;
							I.Value = B4;
						}
						I.Value = (I.Value << 7) + (B3 & 0x7f);
					}
					I.Value = (I.Value << 7) + (B2 & 0x7f);
				}
				I.Value = (I.Value << 7) + (B1 & 0x7f);
			}
			I.Value = (I.Value << 6) + (B0 & 0x3f);
			if( B0 & 0x80 )
				I.Value = -I.Value;
			if( Ar.IsSaving() && I.Value!=Original )
				appErrorf( TEXT("Mismatch: %08X %08X"), I.Value, Original );
		}
	}
	return Ar;
	unguard;
}

/*-----------------------------------------------------------------------------
	Implementations.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(ULinker);
IMPLEMENT_CLASS(ULinkerLoad);
IMPLEMENT_CLASS(ULinkerSave);
IMPLEMENT_CLASS(USubsystem);

/*-----------------------------------------------------------------------------
	UCommandlet.
-----------------------------------------------------------------------------*/

UCommandlet::UCommandlet()
: HelpCmd(E_NoInit), HelpOneLiner(E_NoInit), HelpUsage(E_NoInit), HelpWebLink(E_NoInit)
{}
INT UCommandlet::Main( const TCHAR* Parms )
{
	guard(UCommandlet::Main);
	return eventMain( Parms );
	unguard;
}
void UCommandlet::execMain( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UCommandlet::execMain);

	P_GET_STR(Parms);
	P_FINISH;

	*(INT*)Result = Main( *Parms );

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( UCommandlet, INDEX_NONE, execMain );
IMPLEMENT_CLASS(UCommandlet);

/*-----------------------------------------------------------------------------
	UPackage.
-----------------------------------------------------------------------------*/

UPackage::UPackage()
{
	guard(UPackage::UPackage);

	// Bind to a matching DLL, if any.
	BindPackage( this );
	bDirty = 0;

	unguard;
}
void UPackage::Serialize( FArchive& Ar )
{
	guard(UPackage::Serialize);
	Super::Serialize( Ar );
	unguard;
}
void UPackage::Destroy()
{
	guard(UPackage::Destroy);
	if( DllHandle )
	{
		#if !__STATIC_LINK
		debugf( NAME_Log, TEXT("Unbound to %s%s"), GetName(), DLLEXT );
		#endif
		//Danger: If package is garbage collected before its autoregistered classes,
		//the autoregistered classes will be unmapped from memory.
		//appFreeDllHandle( DllHandle );
	}
	Super::Destroy();
	unguard;
}
void* UPackage::GetDllExport( const TCHAR* ExportName, UBOOL Checked )
{
	guard(UPackage::GetDllExport);
	void* Result;
	#if !__STATIC_LINK
	if( !DllHandle )
	{
		if( Checked && !ParseParam(appCmdLine(),TEXT("nobind")) )
			appErrorf( LocalizeError(TEXT("NotDll"),TEXT("Core")), GetName(), ExportName );
		Result = NULL;
	}
	else
	#endif
	{
		#if __STATIC_LINK
		Result = FindNative( (TCHAR*) ExportName );
		#else
		Result = appGetDllExport( DllHandle, ExportName );
		#endif
		if( !Result && Checked && !ParseParam(appCmdLine(),TEXT("nobind")) )
			appErrorf( LocalizeError(TEXT("NotInDll"),TEXT("Core")), ExportName, GetName() );
		if( Result )
			debugf( NAME_DevBind, TEXT("Found %s in %s%s"), ExportName, GetName(), DLLEXT );
		
	}
	return Result;
	unguard;
}
IMPLEMENT_CLASS(UPackage);

/*-----------------------------------------------------------------------------
	UTextBufferFactory.
-----------------------------------------------------------------------------*/

void UTextBufferFactory::StaticConstructor()
{
	guard(UTextBufferFactory::StaticConstructor);

	SupportedClass = UTextBuffer::StaticClass();
	bCreateNew     = 0;
	bText          = 1;
	new(Formats)FString( TEXT("txt;Text files") );

	unguard;
}
UTextBufferFactory::UTextBufferFactory()
{}
UObject* UTextBufferFactory::FactoryCreateText
(
	ULevel*				InLevel,
	UClass*				Class,
	UObject*			InOuter,
	FName				InName,
	DWORD				InFlags,
	UObject*			Context,
	const TCHAR*		Type,
	const TCHAR*&		Buffer,
	const TCHAR*		BufferEnd,
	FFeedbackContext*	Warn
)
{
	guard(UTextBufferFactory::FactoryCreateText);

	// Import.
	UTextBuffer* Result = new(InOuter,InName,InFlags)UTextBuffer;
	Result->Text = Buffer;
	return Result;

	unguard;
}
IMPLEMENT_CLASS(UTextBufferFactory);

/*----------------------------------------------------------------------------
	ULanguage.
----------------------------------------------------------------------------*/

const TCHAR* UObject::GetLanguage()
{
	guard(UObject::GetLanguage);
	return GLanguage;
	unguard;
}

//scion ===========================================================
//	SCION Function: SetLanguage
//	Author: superville
//
//	Description: Get language extension and set up all localized
//		global variables.
//
//	Input: const TCHAR* LangExt - 3 letter lang abbrev to set as language
//	Last Modified: 01/13/04 (superville)
// ================================================================
void UObject::SetLanguage( const TCHAR* LangExt )
{
	guard(UObject::SetLanguage);

	// If not already set to this language
	if( appStricmp(LangExt,GLanguage)!=0 )
	{
		// Set global language extension
		appStrcpy( GLanguage, LangExt );

		// Set localization folder from language code
		appSprintf( GLocalizationFolder, TEXT("Localization\\%s\\"), GLanguage );
		
		// Set all localized global variables
		appStrcpy( GNone,  LocalizeGeneral( TEXT("None"),  TEXT("Core")) );
		appStrcpy( GTrue,  LocalizeGeneral( TEXT("True"),  TEXT("Core")) );
		appStrcpy( GFalse, LocalizeGeneral( TEXT("False"), TEXT("Core")) );
		appStrcpy( GYes,   LocalizeGeneral( TEXT("Yes"),   TEXT("Core")) );
		appStrcpy( GNo,    LocalizeGeneral( TEXT("No"),    TEXT("Core")) );

		// Loop through each object and inform of language change
		for( FObjectIterator It; It; ++It )
			It->LanguageChange();
	}
	unguard;
}

//scion ===========================================================
//	SCION Function: GetLocalizationFolder
//	Author: superville
//
//	Description: Retrieve name of current language localization folder
//
//	Output: TCHAR* Folder name
//	Last Modified: 01/13/04 (superville)
// ================================================================
const TCHAR* UObject::GetLocalizationFolder()
{
	guard(UObject::GetLocalizationFolder);
	return GLocalizationFolder;
	unguard;
}

//scion ===========================================================
//	SCION Function: GetLocalizationFolder
//	Author: superville
//
//	Description: Retrieve name of default language localization folder
//
//	Output: TCHAR* Folder name
//	Last Modified: 01/13/04 (superville)
// ================================================================
const TCHAR* UObject::GetDefaultLocalizationFolder()
{
	guard(UObject::GetDefaultLocalizationFolder);
	return GDefaultLocalizationFolder;
	unguard;
}

IMPLEMENT_CLASS(ULanguage);

//scion rwd - object precaching
void UObject::Precache(UObject *src,TMap<UObject*,BYTE> *masterPrecacheList)
{
	GetClass()->Precache(this,masterPrecacheList);
}

// Singleton object pool
FObjectPool UObject::GObjPool;
// Singleton list node pool
// (An explicit specialisation needs template<>, and is only a definition with an initialiser.)
template<> FObjList::TPoolInstance<UObject*> FObjList::PoolInstance = FObjList::TPoolInstance<UObject*>();

/* scion ======================================================================
 * UObject::operator delete
 * Author: jg
 *
 * Returns an object back to the object pool if it is poolable, otherwise it
 * deletes it
 *
 * input:	Object -- The object to put back in the pool
 *			Size -- the size of the object
 *
 * ============================================================================
 */
void UObject::operator delete(void* Object,size_t Size)
{
	guard(UObject::operator delete);
	UObject* pObject = (UObject*)Object;
	// Don't use the pool if we are collecting garbage
	if (GIsGarbageCollecting == 0 &&
#ifndef _XBOX
		GIsEditor == 0 &&
#endif
		pObject->GetClass()->ClassFlags & CLASS_Poolable)
	{
		GObjPool.ReturnInstance(pObject);
	}
	else
	{
		appFree(Object);
	}
	unguard;
}

/* scion ======================================================================
 * FObjectPool::GetNextInstance
 * Author: jg
 *
 * Finds a pooled instance for this type or allocates a new one unless the max
 * number of objects of this type have already been spawned
 *
 * input:	pClass -- The class type to allocate space for
 *
 * ============================================================================
 */
UObject* FObjectPool::GetNextInstance(UClass* pClass)
{
	guard(FObjectPool::GetNextInstance);
	check(pClass);
	// Allocate the object list map if needed
	if (ObjectListMap == NULL)
	{
		ObjectListMap = NEW FObjectListMap;
	}
	check(ObjectListMap);
	// Check and see if an instance is available
	FPooledItem* pPooledItem = ObjectListMap->FindRef(pClass->GetPropertiesSize());
	if (pPooledItem == NULL)
	{
		// This is the first time that the item was requested from the pool
		pPooledItem = NEW FPooledItem(pClass);
		// Now actually place the new pooled item in the map
		ObjectListMap->Set(pClass->GetPropertiesSize(),pPooledItem);
	}
	checkSlow(pPooledItem);
	// Get the next instance from the pool
	return pPooledItem->GetNextInstance();
	unguard;
}

/* scion ======================================================================
 * FObjectPool::ReturnInstance
 * Author: jg
 *
 * Returns the object to its pool
 *
 * input:	pObject -- The object to return
 *
 * ============================================================================
 */
void FObjectPool::ReturnInstance(UObject* pObject)
{
	guard(FObjectPool::ReturnInstance);
	// Allocate the object list map if needed
	if (ObjectListMap == NULL)
	{
		ObjectListMap = NEW FObjectListMap;
	}
	check(pObject && ObjectListMap);
	// Check and see if an instance is available
	FPooledItem* pPooledItem = ObjectListMap->FindRef(pObject->GetClass()->GetPropertiesSize());
	// If this object wasn't created via our pool, free it
	// Otherwise return it to the free object list
	if (pPooledItem != NULL)
	{
		// Return the item
		pPooledItem->ReturnInstance(pObject,MaxPoolable);
	}
	else
	{
		// Go ahead and release the memory
		appFree(pObject);
	}
	unguard;
}

/* scion ======================================================================
 * FObjectPool::Empty
 * Author: jg
 *
 * Returns the object to its pool
 *
 * input:	pObject -- The object to return
 *
 * ============================================================================
 */
void FObjectPool::Empty(void)
{
	guard(FObjectPool::Empty);
	// Free the map if allocated
	if (ObjectListMap != NULL)
	{
		// Clean up each allocated item
		for (FObjectListMap::TIterator It(*ObjectListMap); It; ++It)
		{
			delete It.Value();
		}
		delete ObjectListMap;
		ObjectListMap = NULL;
	}
	// Delete all memory associated with the pool
	FObjList::FreePool();
	unguard;
}

/* scion ======================================================================
 * FObjectPool::FPooledItem::GetNextInstance
 * Author: jg
 *
 * Either returns one of the cached instances or returns one. Unless the max is
 * hit in which case it returns NULL.
 *
 * ============================================================================
 */
UObject* FObjectPool::FPooledItem::GetNextInstance(void)
{
	guard(FObjectPool::FPooledItem::GetNextInstance);
	// Defualt to failure
	UObject* pObject = NULL;
	// Check to see if we have one pending
	if (ObjList != NULL)
	{
		FObjList* pDel = ObjList;
		// Snip off this item
		ObjList = ObjList->Next;
		// Get the object pointer
		pObject = pDel->Element;
		// No longer using this memory so recycle it
		FObjList::ReturnInstance(pDel);
		// Decrement how many we are holding
		NumPooled--;
	}
	else
	{
		// No pooled items so create if possible
		pObject = (UObject*)appMalloc(ClassSize);
	}
	return pObject;
	unguard;
}

/* scion ======================================================================
 * FObjectPool::FPooledItem::ReturnInstance
 * Author: jg
 *
 * Places the object in the pool unless the pool limit is reached. If it is the
 * item is deleted.
 *
 * input:	pObject -- The object to return to the pool
 *			MaxPoolable -- The maximum number of instances to pool
 *
 * ============================================================================
 */
void FObjectPool::FPooledItem::ReturnInstance(UObject* pObject,BYTE MaxPoolable)
{
	guard(FObjectPool::FPooledItem::ReturnInstance);
	check(pObject);
	// Link back in if we aren't over the limit (or always link if there is
	// no limit specified)
	if (NumPooled + 1 < MaxPoolable || MaxPoolable == 0)
	{
		// Link this object into the list
		ObjList = FObjList::GetInstance(pObject,ObjList);
		// Increment the number we have stored
		NumPooled++;
	}
	else
	{
		// Can't pool it so free it
		appFree(pObject);
	}
	unguard;
}

/* scion ======================================================================
 * FObjectPool::FPooledItem::FPooledItem
 * Author: jg
 *
 * Inits all values
 *
 * ============================================================================
 */
FObjectPool::FPooledItem::FPooledItem(UClass* pClass)
{
	guard(FObjectPool::FPooledItem::FPooledItem);
	check(pClass);
	ClassSize = pClass->GetPropertiesSize();
	NumPooled = 0;
	ObjList = NULL;
	unguard;
}

/* scion ======================================================================
 * FObjectPool::FPooledItem::~FPooledItem
 * Author: jg
 *
 * Frees any memory stored
 *
 * ============================================================================
 */
FObjectPool::FPooledItem::~FPooledItem(void)
{
	guard(FObjectPool::FPooledItem::~FPooledItem);
	// Walk the list freeing the RAM and deleting the list items
	while (ObjList != NULL)
	{
		FObjList* pDel = ObjList;
		// Snip off this item
		ObjList = ObjList->Next;
		// Free the object space
		appFree(pDel->Element);
		// Delete the list item
		FObjList::ReturnInstance(pDel);
	}
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

