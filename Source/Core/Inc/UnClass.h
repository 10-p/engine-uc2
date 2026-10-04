/*=============================================================================
	UnClass.h: UClass definition.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney.
=============================================================================*/
#ifdef _MSC_VER
#pragma warning( disable : 4121 )
#endif

// Ray's precaching functions
UBOOL CORE_API ShouldPrecacheClass(UClass *testClass);
void CORE_API CheckObjArrayPrecache(UArrayProperty *arrayProp,BYTE* Data,TArray<UObject*> &precacheList);
void CORE_API CheckStrArrayPrecache(UArrayProperty *arrayProp,BYTE* Data,TArray<UObject*> &precacheList);
void CORE_API CheckStructArrayPrecache(UArrayProperty *arrayProp,BYTE* Data,TArray<UObject*> &precacheList);
void CORE_API CheckObjPrecache(UObjectProperty *objProp,BYTE* Data,TArray<UObject*> &precacheList);
void CORE_API CheckStrPrecache(UStrProperty *strProp,BYTE* Data,TArray<UObject*> &precacheList);
void CORE_API CheckStructPrecache(UStruct *structPrecache,BYTE* Data,TArray<UObject*> &precacheList);


/*-----------------------------------------------------------------------------
	Constants.
-----------------------------------------------------------------------------*/

// Boundary to align class properties on.
enum {PROPERTY_ALIGNMENT=4 };

/*-----------------------------------------------------------------------------
	FRepRecord.
-----------------------------------------------------------------------------*/

//
// Information about a property to replicate.
//
struct FRepRecord
{
	UProperty* Property;
	INT Index;
	FRepRecord(UProperty* InProperty,INT InIndex)
	: Property(InProperty), Index(InIndex)
	{}
};

/*-----------------------------------------------------------------------------
	FDependency.
-----------------------------------------------------------------------------*/

//
// One dependency record, for incremental compilation.
//
class CORE_API FDependency
{
public:
	// Variables.
	UClass*		Class;
	UBOOL		Deep;
	DWORD		ScriptTextCRC;

	// Functions.
	FDependency();
	FDependency( UClass* InClass, UBOOL InDeep );
	UBOOL IsUpToDate();
	CORE_API friend FArchive& operator<<( FArchive& Ar, FDependency& Dep );
};

/*-----------------------------------------------------------------------------
	FRepLink.
-----------------------------------------------------------------------------*/

//
// A tagged linked list of replicatable variables.
//
class FRepLink
{
public:
	UProperty*	Property;		// Replicated property.
	FRepLink*	Next;			// Next replicated link per class.
	FRepLink( UProperty* InProperty, FRepLink* InNext )
	:	Property	(InProperty)
	,	Next		(InNext)
	{}
};

/*-----------------------------------------------------------------------------
	FLabelEntry.
-----------------------------------------------------------------------------*/

//
// Entry in a state's label table.
//
struct CORE_API FLabelEntry
{
	// Variables.
	FName	Name;
	INT		iCode;

	// Functions.
	FLabelEntry( FName InName, INT iInCode );
	CORE_API friend FArchive& operator<<( FArchive& Ar, FLabelEntry &Label );
};

/*-----------------------------------------------------------------------------
	UField.
-----------------------------------------------------------------------------*/

//
// Base class of UnrealScript language objects.
//
class CORE_API UField : public UObject
{
	DECLARE_ABSTRACT_CLASS(UField,UObject,0,Core)
	NO_DEFAULT_CONSTRUCTOR(UField)

	// Variables.
	UField*			SuperField;
	UField*			Next;

	// Constructors.
	UField( ENativeConstructor, UClass* InClass, const TCHAR* InName, const TCHAR* InPackageName, DWORD InFlags, UField* InSuperField );
	UField( EStaticConstructor, const TCHAR* InName, const TCHAR* InPackageName, DWORD InFlags );
	UField( UField* InSuperField );

	// UObject interface.
	void Serialize( FArchive& Ar );
	void PostLoad();
	void Register();

	// UField interface.
	virtual void AddCppProperty( UProperty* Property );
	virtual UBOOL MergeBools();
	virtual void Bind();
	virtual UClass* GetOwnerClass() const;
	virtual INT GetPropertiesSize();
};

/*-----------------------------------------------------------------------------
	TFieldIterator.
-----------------------------------------------------------------------------*/

//
// For iterating through a linked list of fields.
//
template <class T, EClassFlags Flag=CLASS_None> class TFieldIterator
{
public:
	TFieldIterator( UStruct* InStruct )
	: Struct( InStruct )
	, Field( InStruct ? InStruct->Children : NULL )
	{
		IterateToNext();
	}
	inline operator UBOOL()
	{
		return Field != NULL;
	}
	inline void operator++()
	{
		checkSlow(Field);
		Field = Field->Next;
		IterateToNext();
	}
	inline T* operator*()
	{
		checkSlow(Field);
		return (T*)Field;
	}
	inline T* operator->()
	{
		checkSlow(Field);
		return (T*)Field;
	}
	inline UStruct* GetStruct()
	{
		return Struct;
	}
protected:
	inline void IterateToNext()
	{
		if ( Flag != CLASS_None )
		{
			while( Struct )
			{
				while( Field )
				{
					if( (Field->GetClass()->ClassFlags & Flag) )
						return;
					Field = Field->Next;
				}
				Struct = Struct->GetInheritanceSuper();
				if( Struct )
					Field = Struct->Children;
			}
		}
		else
		{
			while( Struct )
			{
				while( Field )
				{
					if( Field->IsA(T::StaticClass()) )
						return;
					Field = Field->Next;
				}
				Struct = Struct->GetInheritanceSuper();
				if( Struct )
					Field = Struct->Children;
			}
		}
	}
	UStruct* Struct;
	UField* Field;
};

/*-----------------------------------------------------------------------------
	UStruct.
-----------------------------------------------------------------------------*/

enum EStructFlags
{
	// State flags.
	STRUCT_Native		= 0x00000001,	
	STRUCT_Export		= 0x00000002,
	STRUCT_Transient	= 0x00000004,	// struct is never be used in defaultproperties - do not use *NoInit types for its inner members
};

//
// An UnrealScript structure definition.
//
class CORE_API UStruct : public UField
{
	DECLARE_CLASS(UStruct,UField,0,Core)
	NO_DEFAULT_CONSTRUCTOR(UStruct)

	// Variables.
	UField*				Children;
	INT					PropertiesSize;
	TArray<BYTE>		Script;

	// Compiler info.
	
#ifndef _XBOX
	//scion rwd - nuke some editor only props
	UTextBuffer*		ScriptText;
	UTextBuffer*		CppText;
	INT					Line;
	INT					TextPos;
	DWORD	   			StructFlags;

	//scion rwd - cache the script text crc
	DWORD				ScriptTextCRC;
#endif

	// In memory only.
	UProperty*			RefLink;
	UProperty*			PropertyLink;
	UProperty*			ConstructorLink;

	// Constructors.
	UStruct( ENativeConstructor, INT InSize, const TCHAR* InName, const TCHAR* InPackageName, DWORD InFlags, UStruct* InSuperStruct );
	UStruct( EStaticConstructor, INT InSize, const TCHAR* InName, const TCHAR* InPackageName, DWORD InFlags );
	UStruct( UStruct* InSuperStruct );

	// UObject interface.
	void Serialize( FArchive& Ar );
	void PostLoad();
	void Destroy();

	// UField interface.
	void AddCppProperty( UProperty* Property );

	// UStruct interface.
	virtual UStruct* GetInheritanceSuper() {return GetSuperStruct();}
	virtual void Link( FArchive& Ar, UBOOL Props );
	virtual void SerializeBin( FArchive& Ar, BYTE* Data, INT MaxReadBytes );

	// DELTA DEFAULTS
	virtual void LinkTaggedStruct( FArchive& ReadAr, FArchive& WriteAr );
	virtual void LinkTaggedItem( FArchive& ReadAr, FArchive& WriteAr, UProperty* Property, struct FPropertyTag* Tag );
	virtual void SerializeTaggedProperties( FArchive& Ar, BYTE* Data, UClass* DefaultsClass, UClass* ClassDefaults=NULL );

	virtual void CleanupDestroyed( BYTE* Data );
	//jg -- Useful method for breaking links to objects that you want to delete
	virtual UBOOL CleanupDestroyed(BYTE* Data, UObject* Destroyed);
	virtual EExprToken SerializeExpr( INT& iCode, FArchive& Ar );
	INT GetPropertiesSize()
	{
		return PropertiesSize;
	}
#ifndef _XBOX
	DWORD GetScriptTextCRC()
	{
		//scion rwd - cache the script text crc
		if (ScriptTextCRC == 0 &&
			ScriptText != NULL)
		{
			ScriptTextCRC = appStrCrc(*ScriptText->Text);
		}
		return ScriptTextCRC;
	}
#endif
	void SetPropertiesSize( INT NewSize )
	{
		PropertiesSize = NewSize;
	}
	UBOOL IsChildOf( const UStruct* SomeBase ) const
	{
		guardSlow(UStruct::IsChildOf);
		for( const UStruct* Struct=this; Struct; Struct=Struct->GetSuperStruct() )
			if( Struct==SomeBase )
				return 1;
		return 0;
		unguardobjSlow;
	}
	virtual TCHAR* GetNameCPP()
	{
		TCHAR* Result = appStaticString1024();
		appSprintf( Result, TEXT("F%s"), GetName() );
		return Result;
	}
	UStruct* GetSuperStruct() const
	{
		guardSlow(UStruct::GetSuperStruct);
		checkSlow(!SuperField||SuperField->IsA(UStruct::StaticClass()));
		return (UStruct*)SuperField;
		unguardSlow;
	}
	UBOOL StructCompare( const void* A, const void* B );
};

/*-----------------------------------------------------------------------------
	UFunction.
-----------------------------------------------------------------------------*/

//
// An UnrealScript function.
//
class CORE_API UFunction : public UStruct
{
	DECLARE_CLASS(UFunction,UStruct,CLASS_IsAUFunction,Core)
	DECLARE_WITHIN(UState)
	NO_DEFAULT_CONSTRUCTOR(UFunction)

	// Persistent variables.
	DWORD FunctionFlags;
	_WORD iNative;
	_WORD RepOffset;
	BYTE  OperPrecedence;
	FName				FriendlyName;				// friendly version for this function, mainly for operators

	// Variables in memory only.
	BYTE  NumParms;
	_WORD ParmsSize;
	_WORD ReturnValueOffset;
	void (UObject::*Func)( FFrame& TheStack, RESULT_DECL );

	// Constructors.
	UFunction( UFunction* InSuperFunction );

	// UObject interface.
	void Serialize( FArchive& Ar );
	void PostLoad();

	// UField interface.
	void Bind();

	// UStruct interface.
	UBOOL MergeBools() {return 0;}
	UStruct* GetInheritanceSuper() {return NULL;}
	void Link( FArchive& Ar, UBOOL Props );

	// UFunction interface.
	UFunction* GetSuperFunction() const
	{
		guardSlow(UFunction::GetSuperFunction);
		checkSlow(!SuperField||SuperField->IsA(UFunction::StaticClass()));
		return (UFunction*)SuperField;
		unguardSlow;
	}
	UProperty* GetReturnProperty();
};

/*-----------------------------------------------------------------------------
	UState.
-----------------------------------------------------------------------------*/

//
// An UnrealScript state.
//
class CORE_API UState : public UStruct
{
	DECLARE_CLASS(UState,UStruct,CLASS_IsAUState,Core)
	NO_DEFAULT_CONSTRUCTOR(UState)

	// Variables.
	// scion sz - reordered for better alignment
	QWORD ProbeMask;								// list of functions currently probed by the current class (see UnNames.h)
	QWORD IgnoreMask;								// list of functions currently ignored by the current statenode (see UnNames.h)
	TMap<FName,UFunction*>	FuncMap;				// map of all functions by name for quick lookup
	_WORD LabelTableOffset;							// offset into the Script array that contains the table of FLabelEntry's
	DWORD StateFlags;

	// Constructors.
	UState( ENativeConstructor, INT InSize, const TCHAR* InName, const TCHAR* InPackageName, DWORD InFlags, UState* InSuperState );
	UState( EStaticConstructor, INT InSize, const TCHAR* InName, const TCHAR* InPackageName, DWORD InFlags );
	UState( UState* InSuperState );

	// UObject interface.
	void Serialize( FArchive& Ar );
	void Destroy();

	// UStruct interface.
	UBOOL MergeBools() {return 1;}
	UStruct* GetInheritanceSuper() {return GetSuperState();}

	// UState interface.
	UState* GetSuperState() const
	{
		guardSlow(UState::GetSuperState);
		checkSlow(!SuperField||SuperField->IsA(UState::StaticClass()));
		return (UState*)SuperField;
		unguardSlow;
	}
};

/*-----------------------------------------------------------------------------
	UEnum.
-----------------------------------------------------------------------------*/

//
// An enumeration, a list of names usable by UnrealScript.
//
class CORE_API UEnum : public UField
{
	DECLARE_CLASS(UEnum,UField,0,Core)
	DECLARE_WITHIN(UStruct)
	NO_DEFAULT_CONSTRUCTOR(UEnum)

	// Variables.
	TArray<FName> Names;

	// Constructors.
	UEnum( UEnum* InSuperEnum );

	// UObject interface.
	void Serialize( FArchive& Ar );

	// UEnum interface.
	UEnum* GetSuperEnum() const
	{
		guardSlow(UEnum::GetSuperEnum);
		checkSlow(!SuperField||SuperField->IsA(UEnum::StaticClass()));
		return (UEnum*)SuperField;
		unguardSlow;
	}
};

/*-----------------------------------------------------------------------------
	UClass.
-----------------------------------------------------------------------------*/

#if SUPPORTS_PRAGMA_PACK
#pragma pack (push,1)
#endif
//
// An object class.
//
class CORE_API UClass : public UState
{
	DECLARE_CLASS(UClass,UState,0,Core)
	DECLARE_WITHIN(UPackage)

	// Variables.	
	TArray<FRepRecord>	ClassReps;
	TArray<UField*>		NetFields;
//scion rwd - remove some unused props for xbox build
// scion sz - reordered for better alignment
#ifndef _XBOX
	TArray<FDependency> Dependencies;
	TArray<FName>		PackageImports;
#endif
	TArray<BYTE>		Defaults;
#ifndef _XBOX
	TArray<FName>		HideCategories;
    TArray<FName>       DependentOn;
#endif
	DWORD				ClassFlags;
	UClass*				ClassWithin;
	FName				ClassConfigName;

	void(*ClassConstructor)(void*);
	void(UObject::*ClassStaticConstructor)();

#ifndef _XBOX
	// In memory only.
	FString				DefaultPropText;
#endif

	//jg -- 64K instances should be enough
	_WORD				ClassUnique;	

	//scion rwd - has this class been precached?
	BYTE				bPrecached;

	// Constructors.
	UClass();
	UClass( UClass* InSuperClass );
	UClass( ENativeConstructor, DWORD InSize, DWORD InClassFlags, UClass* InBaseClass, UClass* InWithinClass, FGuid InGuid, const TCHAR* InNameStr, const TCHAR* InPackageName, const TCHAR* InClassConfigName, DWORD InFlags, void(*InClassConstructor)(void*), void(UObject::*InClassStaticConstructor)() );
	UClass( EStaticConstructor, DWORD InSize, DWORD InClassFlags, FGuid InGuid, const TCHAR* InNameStr, const TCHAR* InPackageName, const TCHAR* InClassConfigName, DWORD InFlags, void(*InClassConstructor)(void*), void(UObject::*InClassStaticConstructor)() );

	// UObject interface.
	void Serialize( FArchive& Ar );
	void PostLoad();
	void Destroy();
	void Register();
	void LanguageChange();

	// DELTA DEFAULTS
	void SerializeTaggedDefaults( BYTE* Mem, UClass* DefaultsClass );
	void ExpandDefaultObject();
	void GCTaggedDefaults( FArchive& Ar );

	// UField interface.
	void Bind();

	// UStruct interface.
	UBOOL MergeBools() {return 1;}
	FORCEINLINE UStruct* GetInheritanceSuper() {return GetSuperClass();}
	TCHAR* GetNameCPP()
	{
		TCHAR* Result = appStaticString1024();
		UClass* C;
		for( C=this; C; C=C->GetSuperClass() )
			if( appStricmp(C->GetName(),TEXT("Actor"))==0 )
				break;
		appSprintf( Result, TEXT("%s%s"), C ? TEXT("A") : TEXT("U"), GetName() );
		return Result;
	}
	void Link( FArchive& Ar, UBOOL Props );

	// UClass interface.
	void AddDependency( UClass* InClass, UBOOL InDeep )
	{
#ifndef _XBOX
		guard(UClass::AddDependency);
		INT i;
		for( i=0; i<Dependencies.Num(); i++ )
			if( Dependencies(i).Class==InClass )
				Dependencies(i).Deep |= InDeep;
		if( i==Dependencies.Num() )
			new(Dependencies)FDependency( InClass, InDeep );
		unguard;
#endif
	}
	FORCEINLINE UClass* GetSuperClass() const
	{
		guardSlow(UClass::GetSuperClass);
		return (UClass *)SuperField;
		unguardSlow;
	}

	// scion sz - byte array defaults accessor
	BYTE* GetDefaults( INT Offset=0 )
	{
		guardSlow(UClass::GetDefaults);
		check(Defaults.Num()==GetPropertiesSize());
		return (BYTE*)&Defaults( Offset );
		unguardobjSlow;
	}
	// scion sz - same as above but creates
	// full defaults first
	BYTE* GetDefaultsSafe( INT Offset=0 )
	{
		guardSlow(UClass::GetDefaultsSafe);
		// DELTA DEFAULTS
		if( ClassFlags & CLASS_TaggedDefaults )
			ExpandDefaultObject();
		return GetDefaults( Offset );
		unguardobjSlow;
	}
	// scion sz - templated defaults accessor
	template<class T>
	T* GetDefaultObject()
	{
		guardSlow(UClass::GetDefaultObject);
		// DELTA DEFAULTS
		if( ClassFlags & CLASS_TaggedDefaults )
			ExpandDefaultObject();
		return (T*)&Defaults(0);
		unguardobjSlow;
	}
	UObject* GetDefaultObject()
	{
		guardSlow(UClass::GetDefaultObject);
		return GetDefaultObject<UObject>();
		unguardobjSlow;
	}
	class AActor* GetDefaultActor()
	{
		guardSlow(UClass::GetDefaultActor);
		return GetDefaultObject<AActor>();
		unguardobjSlow;
	}
	UBOOL HasNativesToExport( UObject* Outer )
	{
		guardSlow(UClass::HasNativesToExport);
#ifndef _XBOX
		if( GetFlags() & RF_Native )
		{
			if( GetFlags() & RF_TagExp )
				return 1;		
			if( ScriptText && GetOuter() == Outer )
				for( TFieldIterator<UFunction> Function(this); Function && Function.GetStruct()==this; ++Function )
					if( Function->FunctionFlags & FUNC_Native )
						return 1;
		}
#endif
		return 0;			
		unguardobjSlow;
	}

	//scion rwd - overloaded to precache various properties
	void Precache(UObject *src = NULL,TMap<UObject*,BYTE> *masterPrecacheList = NULL);

private:
	// Hide IsA because calling IsA on a class almost always indicates
	// an error where the caller should use IsChildOf.
	UBOOL IsA( UClass* Parent ) const {return UObject::IsA(Parent);}
};
#if SUPPORTS_PRAGMA_PACK
#pragma pack (pop)
#endif


/*-----------------------------------------------------------------------------
	UConst.
-----------------------------------------------------------------------------*/

//
// An UnrealScript constant.
//
class CORE_API UConst : public UField
{
	DECLARE_CLASS(UConst,UField,0,Core)
	DECLARE_WITHIN(UStruct)
	NO_DEFAULT_CONSTRUCTOR(UConst)

	// Variables.
	FString Value;

	// Constructors.
	UConst( UConst* InSuperConst, const TCHAR* InValue );

	// UObject interface.
	void Serialize( FArchive& Ar );

	// UConst interface.
	UConst* GetSuperConst() const
	{
		guardSlow(UConst::GetSuperStruct);
		checkSlow(!SuperField||SuperField->IsA(UConst::StaticClass()));
		return (UConst*)SuperField;
		unguardSlow;
	}
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

