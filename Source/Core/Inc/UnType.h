/*=============================================================================
	UnType.h: Unreal engine base type definitions.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

/*-----------------------------------------------------------------------------
	UProperty.
-----------------------------------------------------------------------------*/

// Property exporting flags.
enum EPropertyPortFlags
{
	PPF_Localized = 1,
	PPF_Delimited = 2,
	PPF_CheckReferences = 4,
	PPF_LocalizedOnly   = 8, // rjp struct localization
};

//
// An UnrealScript variable.
//
class CORE_API UProperty : public UField
{
	DECLARE_ABSTRACT_CLASS(UProperty,UField,CLASS_IsAUProperty,Core)
	DECLARE_WITHIN(UField)

	// Persistent variables.
//scion rwd - reduce property size a bit
	_WORD		ArrayDim;
	_WORD		ElementSize;
	DWORD		PropertyFlags;
#ifndef _XBOX
	FName		Category;
#endif
	_WORD		RepOffset;
	_WORD		RepIndex;

	// In memory variables.
	INT			Offset;
	UProperty*	PropertyLinkNext;
	UProperty*	ConstructorLinkNext;
	UProperty*  NextRef;
	UProperty*	RepOwner;

	// Constructors.
	UProperty();
	UProperty( ECppProperty, INT InOffset, const TCHAR* InCategory, DWORD InFlags );

	// UObject interface.
	void Serialize( FArchive& Ar );

	// UProperty interface.
	virtual void CleanupDestroyed( BYTE* Data ) const;
	//jg -- Useful method for breaking links to objects that you want to delete
	virtual UBOOL CleanupDestroyed(BYTE* Data, UObject* Destroyed) const { return 0; }
	virtual void Link( FArchive& Ar, UProperty* Prev );
	virtual UBOOL Identical( const void* A, const void* B ) const=0;
	virtual void ExportCpp( FOutputDevice& Out, UBOOL IsLocal, UBOOL IsParm, UBOOL IsEvent, UBOOL IsStruct ) const;
	virtual void ExportCppItem( FOutputDevice& Out, UBOOL IsEvent=0 ) const=0;
	virtual void SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes=0 ) const=0;
	virtual UBOOL NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const;
	virtual void ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const=0;
	virtual const TCHAR* ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const=0;
	virtual UBOOL ExportText( INT ArrayElement, TCHAR* ValueStr, BYTE* Data, BYTE* Delta, INT PortFlags ) const;
	virtual void CopySingleValue( void* Dest, void* Src, UObject* SuperObject=NULL ) const;
	virtual void CopyCompleteValue( void* Dest, void* Src, UObject* SuperObject=NULL ) const;
	virtual void ClearValue( BYTE* Data ) const =0;
	virtual void DestroyValue( void* Dest ) const;
	virtual UBOOL HasValue( const BYTE* Data ) const=0; // rjp struct localization

	virtual UBOOL Port() const;
	virtual BYTE GetID() const;

	// Localization
	virtual UBOOL IsLocalized() const; // rjp struct localization

	// Inlines.
	UBOOL Matches( const void* A, const void* B, INT ArrayIndex ) const
	{
		guardSlow(UProperty::Matches);
		INT Ofs = Offset + ArrayIndex * ElementSize;
		return Identical( (BYTE*)A + Ofs, B ? (BYTE*)B + Ofs : NULL );
		unguardobjSlow;
	}
	INT GetSize() const
	{
		guardSlow(UProperty::GetSize);
		return ArrayDim * ElementSize;
		unguardobjSlow;
	}
	UBOOL ShouldSerializeValue( FArchive& Ar ) const
	{
		guardSlow(UProperty::ShouldSerializeValue);
		UBOOL Skip
		=	((PropertyFlags & CPF_Native   )										)
		||	((PropertyFlags & CPF_Transient) && Ar.IsPersistent()					)
		||	((PropertyFlags & CPF_Deprecated) && ( Ar.IsSaving() || Ar.IsTrans() )	)
		||	( !Ar.ShouldSerializeProperty(this)										);
		return !Skip;
		unguardobjSlow;
	}
};

/*-----------------------------------------------------------------------------
	UByteProperty.
-----------------------------------------------------------------------------*/

//
// Describes an unsigned byte value or 255-value enumeration variable.
//
class CORE_API UByteProperty : public UProperty
{
	DECLARE_CLASS(UByteProperty,UProperty,CLASS_IsAUProperty,Core)

	// Variables.
	UEnum* Enum;

	// Constructors.
	UByteProperty()
	{}
	UByteProperty( ECppProperty, INT InOffset, const TCHAR* InCategory, DWORD InFlags, UEnum* InEnum=NULL )
	:	UProperty( EC_CppProperty, InOffset, InCategory, InFlags )
	,	Enum( InEnum )
	{}

	// UObject interface.
	void Serialize( FArchive& Ar );

	// UProperty interface.
	void Link( FArchive& Ar, UProperty* Prev );
	UBOOL Identical( const void* A, const void* B ) const;
	void SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const;
	UBOOL NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const;
	void ExportCppItem( FOutputDevice& Out, UBOOL IsEvent=0 ) const;
	void ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const;
	const TCHAR* ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const;
	void CopySingleValue( void* Dest, void* Src, UObject* SuperObject=NULL ) const;
	void CopyCompleteValue( void* Dest, void* Src, UObject* SuperObject=NULL ) const;
	UBOOL HasValue( const BYTE* Data ) const; // rjp struct localization
	void ClearValue( BYTE* Data ) const;
};

/*-----------------------------------------------------------------------------
	UIntProperty.
-----------------------------------------------------------------------------*/

//
// Describes a 32-bit signed integer variable.
//
class CORE_API UIntProperty : public UProperty
{
	DECLARE_CLASS(UIntProperty,UProperty,CLASS_IsAUProperty,Core)

	// Constructors.
	UIntProperty()
	{}
	UIntProperty( ECppProperty, INT InOffset, const TCHAR* InCategory, DWORD InFlags )
	:	UProperty( EC_CppProperty, InOffset, InCategory, InFlags )
	{}

	// UProperty interface.
	void Link( FArchive& Ar, UProperty* Prev );
	UBOOL Identical( const void* A, const void* B ) const;
	void SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const;
	UBOOL NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const;
	void ExportCppItem( FOutputDevice& Out, UBOOL IsEvent=0 ) const;
	void ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const;
	const TCHAR* ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const;
	void CopySingleValue( void* Dest, void* Src, UObject* SuperObject=NULL ) const;
	void CopyCompleteValue( void* Dest, void* Src, UObject* SuperObject=NULL ) const;
	UBOOL HasValue( const BYTE* Data ) const; // rjp struct localization
	void ClearValue( BYTE* Data ) const;
};

/*-----------------------------------------------------------------------------
	UBoolProperty.
-----------------------------------------------------------------------------*/

//
// Describes a single bit flag variable residing in a 32-bit unsigned double word.
//
class CORE_API UBoolProperty : public UProperty
{
	DECLARE_CLASS(UBoolProperty,UProperty,CLASS_IsAUProperty|CLASS_IsAUBoolProperty,Core)

	// Variables.
	BITFIELD BitMask;

	// Constructors.
	UBoolProperty()
	{}
	UBoolProperty( ECppProperty, INT InOffset, const TCHAR* InCategory, DWORD InFlags )
	:	UProperty( EC_CppProperty, InOffset, InCategory, InFlags )
	,	BitMask( 1 )
	{}

	// UObject interface.
	void Serialize( FArchive& Ar );

	// UProperty interface.
	void Link( FArchive& Ar, UProperty* Prev );
	UBOOL Identical( const void* A, const void* B ) const;
	void SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const;
	UBOOL NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const;
	void ExportCppItem( FOutputDevice& Out, UBOOL IsEvent=0 ) const;
	void ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const;
	const TCHAR* ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const;
	void CopySingleValue( void* Dest, void* Src, UObject* SuperObject=NULL ) const;
	UBOOL HasValue( const BYTE* Data ) const; // rjp struct localization
	void ClearValue( BYTE* Data ) const;
};

/*-----------------------------------------------------------------------------
	UFloatProperty.
-----------------------------------------------------------------------------*/

//
// Describes an IEEE 32-bit floating point variable.
//
class CORE_API UFloatProperty : public UProperty
{
	DECLARE_CLASS(UFloatProperty,UProperty,CLASS_IsAUProperty,Core)

	// Constructors.
	UFloatProperty()
	{}
	UFloatProperty( ECppProperty, INT InOffset, const TCHAR* InCategory, DWORD InFlags )
	:	UProperty( EC_CppProperty, InOffset, InCategory, InFlags )
	{}

	// UProperty interface.
	void Link( FArchive& Ar, UProperty* Prev );
	UBOOL Identical( const void* A, const void* B ) const;
	void SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const;
	UBOOL NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const;
	void ExportCppItem( FOutputDevice& Out, UBOOL IsEvent=0 ) const;
	void ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const;
	const TCHAR* ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const;
	void CopySingleValue( void* Dest, void* Src, UObject* SuperObject=NULL ) const;
	void CopyCompleteValue( void* Dest, void* Src, UObject* SuperObject=NULL ) const;
	UBOOL HasValue( const BYTE* Data ) const; // rjp struct localization
	void ClearValue( BYTE* Data ) const;
};

/*-----------------------------------------------------------------------------
	UObjectProperty.
-----------------------------------------------------------------------------*/

//
// Describes a reference variable to another object which may be nil.
//
class CORE_API UObjectProperty : public UProperty
{
	DECLARE_CLASS(UObjectProperty,UProperty,CLASS_IsAUProperty|CLASS_IsAUObjectProperty,Core)

	// Variables.
	class UClass* PropertyClass;

	// Constructors.
	UObjectProperty()
	{}
	UObjectProperty( ECppProperty, INT InOffset, const TCHAR* InCategory, DWORD InFlags, UClass* InClass )
	:	UProperty( EC_CppProperty, InOffset, InCategory, InFlags )
	,	PropertyClass( InClass )
	{}

	// UObject interface.
	void Serialize( FArchive& Ar );

	// UProperty interface.
	void CleanupDestroyed( BYTE* Data ) const;
	//jg -- Useful method for breaking links to objects that you want to delete
	virtual UBOOL CleanupDestroyed(BYTE* Data, UObject* Destroyed) const;
	void Link( FArchive& Ar, UProperty* Prev );
	UBOOL Identical( const void* A, const void* B ) const;
	void SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const;
	UBOOL NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const;
	void ExportCppItem( FOutputDevice& Out, UBOOL IsEvent=0 ) const;
	void ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const;
	const TCHAR* ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const;
	void CopySingleValue( void* Dest, void* Src, UObject* SuperObject=NULL ) const;
	void CopyCompleteValue( void* Dest, void* Src, UObject* SuperObject=NULL ) const;
	UBOOL HasValue( const BYTE* Data ) const; // rjp struct localization
	void ClearValue( BYTE* Data ) const;
};

/*-----------------------------------------------------------------------------
	UObjectProperty.
-----------------------------------------------------------------------------*/

//
// Describes a reference variable to another object which may be nil.
//
class CORE_API UClassProperty : public UObjectProperty
{
	DECLARE_CLASS(UClassProperty,UObjectProperty,0,Core)

	// Variables.
	class UClass* MetaClass;

	// Constructors.
	UClassProperty()
	{}
	UClassProperty( ECppProperty, INT InOffset, const TCHAR* InCategory, DWORD InFlags, UClass* InMetaClass )
	:	UObjectProperty( EC_CppProperty, InOffset, InCategory, InFlags, UClass::StaticClass() )
	,	MetaClass( InMetaClass )
	{}

	// UObject interface.
	void Serialize( FArchive& Ar );

	// UProperty interface.
	const TCHAR* ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const;
	BYTE GetID() const
	{
		return NAME_ObjectProperty;
	}
	UBOOL HasValue( const BYTE* Data ) const; // rjp struct localization
	void ClearValue( BYTE* Data ) const;
};

/*-----------------------------------------------------------------------------
	UNameProperty.
-----------------------------------------------------------------------------*/

//
// Describes a name variable pointing into the global name table.
//
class CORE_API UNameProperty : public UProperty
{
	DECLARE_CLASS(UNameProperty,UProperty,CLASS_IsAUProperty,Core)

	// Constructors.
	UNameProperty()
	{}
	UNameProperty( ECppProperty, INT InOffset, const TCHAR* InCategory, DWORD InFlags )
	:	UProperty( EC_CppProperty, InOffset, InCategory, InFlags )
	{}

	// UProperty interface.
	void Link( FArchive& Ar, UProperty* Prev );
	UBOOL Identical( const void* A, const void* B ) const;
	void SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const;
	void ExportCppItem( FOutputDevice& Out, UBOOL IsEvent=0 ) const;
	void ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const;
	const TCHAR* ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const;
	void CopySingleValue( void* Dest, void* Src, UObject* SuperObject=NULL ) const;
	void CopyCompleteValue( void* Dest, void* Src, UObject* SuperObject=NULL ) const;
	UBOOL HasValue( const BYTE* Data ) const; // rjp struct localization
	void ClearValue( BYTE* Data ) const;
};

/*-----------------------------------------------------------------------------
	UStrProperty.
-----------------------------------------------------------------------------*/

//
// Describes a dynamic string variable.
//
class CORE_API UStrProperty : public UProperty
{
	DECLARE_CLASS(UStrProperty,UProperty,CLASS_IsAUProperty,Core)

	// Constructors.
	UStrProperty()
	{}
	UStrProperty( ECppProperty, INT InOffset, const TCHAR* InCategory, DWORD InFlags )
	:	UProperty( EC_CppProperty, InOffset, InCategory, InFlags )
	{}

	// UObject interface.
	void Serialize( FArchive& Ar );

	// UProperty interface.
	void Link( FArchive& Ar, UProperty* Prev );
	UBOOL Identical( const void* A, const void* B ) const;
	void SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const;
	void ExportCppItem( FOutputDevice& Out, UBOOL IsEvent=0 ) const;
	void ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const;
	const TCHAR* ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const;
	void CopySingleValue( void* Dest, void* Src, UObject* SuperObject=NULL ) const;
	void DestroyValue( void* Dest ) const;
	UBOOL HasValue( const BYTE* Data ) const; // rjp struct localization
	void ClearValue( BYTE* Data ) const;
};

/*-----------------------------------------------------------------------------
	UFixedArrayProperty.
-----------------------------------------------------------------------------*/

//
// Describes a fixed length array.
//
class CORE_API UFixedArrayProperty : public UProperty
{
	DECLARE_CLASS(UFixedArrayProperty,UProperty,CLASS_IsAUProperty,Core)

	// Variables.
	UProperty* Inner;
	INT Count;

	// Constructors.
	UFixedArrayProperty()
	{}
	UFixedArrayProperty( ECppProperty, INT InOffset, const TCHAR* InCategory, DWORD InFlags )
	:	UProperty( EC_CppProperty, InOffset, InCategory, InFlags )
	{}

	// UObject interface.
	void Serialize( FArchive& Ar );

	// UProperty interface.
	void CleanupDestroyed( BYTE* Data ) const;
	//jg -- Useful method for breaking links to objects that you want to delete
	virtual UBOOL CleanupDestroyed(BYTE* Data, UObject* Destroyed) const;
	void Link( FArchive& Ar, UProperty* Prev );
	UBOOL Identical( const void* A, const void* B ) const;
	void SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const;
	UBOOL NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const;
	void ExportCppItem( FOutputDevice& Out, UBOOL IsEvent=0 ) const;
	void ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const;
	const TCHAR* ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const;
	void CopySingleValue( void* Dest, void* Src, UObject* SuperObject=NULL ) const;
	void DestroyValue( void* Dest ) const;
	UBOOL HasValue( const BYTE* Data ) const; // rjp struct localization
	void ClearValue( BYTE* Data ) const;

	// UFixedArrayProperty interface.
	void AddCppProperty( UProperty* Property, INT Count );
};

/*-----------------------------------------------------------------------------
	UArrayProperty.
-----------------------------------------------------------------------------*/

//
// Describes a dynamic array.
//
class CORE_API UArrayProperty : public UProperty
{
	DECLARE_CLASS(UArrayProperty,UProperty,CLASS_IsAUProperty,Core)

	// Variables.
	UProperty* Inner;

	// Constructors.
	UArrayProperty()
	{}
	UArrayProperty( ECppProperty, INT InOffset, const TCHAR* InCategory, DWORD InFlags )
	:	UProperty( EC_CppProperty, InOffset, InCategory, InFlags )
	{}

	// UObject interface.
	void Serialize( FArchive& Ar );

	// UProperty interface.
	void CleanupDestroyed( BYTE* Data ) const;
	//jg -- Useful method for breaking links to objects that you want to delete
	virtual UBOOL CleanupDestroyed(BYTE* Data, UObject* Destroyed) const;
	void Link( FArchive& Ar, UProperty* Prev );
	UBOOL Identical( const void* A, const void* B ) const;
	void SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const;
	UBOOL NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const;
	void ExportCppItem( FOutputDevice& Out, UBOOL IsEvent=0 ) const;
	void ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const;
	const TCHAR* ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const;
	void CopyCompleteValue( void* Dest, void* Src, UObject* SuperObject=NULL ) const;
	void ClearValue( BYTE* Data ) const;
	void DestroyValue( void* Dest ) const;
	UBOOL IsLocalized() const; // rjp struct localization
	UBOOL HasValue( const BYTE* Data ) const;    // rjp struct localization

	// UArrayProperty interface.
	void AddCppProperty( UProperty* Property );
};

/*-----------------------------------------------------------------------------
	UMapProperty.
-----------------------------------------------------------------------------*/

//
// Describes a dynamic map.
//
class CORE_API UMapProperty : public UProperty
{
	DECLARE_CLASS(UMapProperty,UProperty,CLASS_IsAUProperty,Core)

	// Variables.
	UProperty* Key;
	UProperty* Value;

	// Constructors.
	UMapProperty()
	{}
	UMapProperty( ECppProperty, INT InOffset, const TCHAR* InCategory, DWORD InFlags )
	:	UProperty( EC_CppProperty, InOffset, InCategory, InFlags )
	{}

	// UObject interface.
	void Serialize( FArchive& Ar );

	// UProperty interface.
	void Link( FArchive& Ar, UProperty* Prev );
	UBOOL Identical( const void* A, const void* B ) const;
	void SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const;
	UBOOL NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const;
	void ExportCppItem( FOutputDevice& Out, UBOOL IsEvent=0 ) const;
	void ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const;
	const TCHAR* ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const;
	void CopySingleValue( void* Dest, void* Src, UObject* SuperObject=NULL ) const;
	void DestroyValue( void* Dest ) const;
	UBOOL HasValue( const BYTE* Data ) const; // rjp struct localization
	void ClearValue( BYTE* Data ) const {}
};

/*-----------------------------------------------------------------------------
	UStructProperty.
-----------------------------------------------------------------------------*/

//
// Describes a structure variable embedded in (as opposed to referenced by)
// an object.
//
class CORE_API UStructProperty : public UProperty
{
	DECLARE_CLASS(UStructProperty,UProperty,CLASS_IsAUProperty,Core)

	// Variables.
	class UStruct* Struct;

	// Constructors.
	UStructProperty()
	{}
	UStructProperty( ECppProperty, INT InOffset, const TCHAR* InCategory, DWORD InFlags, UStruct* InStruct )
	:	UProperty( EC_CppProperty, InOffset, InCategory, InFlags )
	,	Struct( InStruct )
	{}

	// UObject interface.
	void Serialize( FArchive& Ar );

	// UProperty interface.
	void CleanupDestroyed( BYTE* Data ) const;
	//jg -- Useful method for breaking links to objects that you want to delete
	virtual UBOOL CleanupDestroyed(BYTE* Data, UObject* Destroyed) const;
	void Link( FArchive& Ar, UProperty* Prev );
	UBOOL Identical( const void* A, const void* B ) const;
	void SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const;
	UBOOL NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const;
	void ExportCppItem( FOutputDevice& Out, UBOOL IsEvent=0 ) const;
	void ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const;
	const TCHAR* ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const;
	void CopySingleValue( void* Dest, void* Src, UObject* SuperObject=NULL ) const;
	void CopyCompleteValue( void* Dest, void* Src, UObject* SuperObject=NULL ) const;
	void ClearValue( BYTE* Data ) const;
	void DestroyValue( void* Dest ) const;
	UBOOL IsLocalized() const; // rjp struct localization
	UBOOL HasValue( const BYTE* Data ) const;    // rjp struct localization
};

/*-----------------------------------------------------------------------------
	UDelegateProperty.
-----------------------------------------------------------------------------*/

//
// Describes a pointer to a function bound to an Object
//
class CORE_API UDelegateProperty : public UProperty
{
	DECLARE_CLASS(UDelegateProperty,UProperty,CLASS_IsAUProperty,Core)

	UFunction* Function;

	// Constructors.
	UDelegateProperty()
	{}
	UDelegateProperty( ECppProperty, INT InOffset, const TCHAR* InCategory, DWORD InFlags )
	:	UProperty( EC_CppProperty, InOffset, InCategory, InFlags )
	{}

	// UObject interface.
	void Serialize( FArchive& Ar );

	// UProperty interface.
	void CleanupDestroyed( BYTE* Data ) const;
	//jg -- Useful method for breaking links to objects that you want to delete
	virtual UBOOL CleanupDestroyed(BYTE* Data, UObject* Destroyed) const;
	void Link( FArchive& Ar, UProperty* Prev );
	UBOOL Identical( const void* A, const void* B ) const;
	void SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const;
	UBOOL NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const;
	void ExportCppItem( FOutputDevice& Out, UBOOL IsEvent=0 ) const;
	void ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const;
	const TCHAR* ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const;
	void CopySingleValue( void* Dest, void* Src, UObject* SuperObject=NULL ) const;
	void CopyCompleteValue( void* Dest, void* Src, UObject* SuperObject=NULL ) const;
	UBOOL HasValue( const BYTE* Data ) const; // rjp struct localization
	void ClearValue( BYTE* Data ) const;
};

/*-----------------------------------------------------------------------------
	Field templates.
-----------------------------------------------------------------------------*/

// Declared ahead of FindField, which uses it (two-phase lookup).
template<class T> FORCEINLINE T* FindFieldNamed(UStruct* Owner, const FName& Name);

//
// Find a typed field in a struct.
//
template<class T> T* FindField(UStruct* Owner, const TCHAR* FieldName)
{
	// lookup the string name in the Name hash
	FName Name(FieldName, FNAME_Find);
	// If we didn't find it, we know the field won't exist in this Struct
	if (Name == NAME_None)
	{
			return NULL;
	}
	return FindFieldNamed<T>(Owner, Name);
}

// find the field by comparing FNames instead of strings
template<class T> FORCEINLINE T* FindFieldNamed(UStruct* Owner, const FName& Name)
{
	// Search by comparing FNames (INTs), not strings
	for (TFieldIterator<T> It(Owner); It; ++It)
	{
			if (It->GetFName() == Name)
			{
				return *It;
			}
	}
	// If we didn't find it, return no field
	return NULL;
}

/*-----------------------------------------------------------------------------
	UObject accessors that depend on UClass.
-----------------------------------------------------------------------------*/

//
// See if this object belongs to the specified class.
//
FORCEINLINE UBOOL UObject::IsA( class UClass* SomeBase ) const
{
	guardSlow(UObject::IsA);
	for( UClass* TempClass=Class; TempClass; TempClass=(UClass*)TempClass->SuperField )
		if( TempClass==SomeBase )
			return 1;
	return SomeBase==NULL;
	unguardobjSlow;
}

//
// See if this object is in a certain package.
//
FORCEINLINE UBOOL UObject::IsIn( class UObject* SomeOuter ) const
{
	guardSlow(UObject::IsIn);
	for( UObject* It=GetOuter(); It; It=It->GetOuter() )
		if( It==SomeOuter )
			return 1;
	return SomeOuter==NULL;
	unguardobjSlow;
}

//
// Return whether an object wants to receive a named probe message.
//
FORCEINLINE UBOOL UObject::IsProbing( FName ProbeName )
{
	guardSlow(UObject::IsProbing);
	return	(ProbeName.GetIndex() <  NAME_PROBEMIN)
	||		(ProbeName.GetIndex() >= NAME_PROBEMAX)
	||		(!StateFrame)
	||		(StateFrame->ProbeMask & ((QWORD)1 << (ProbeName.GetIndex() - NAME_PROBEMIN)));
	unguardobjSlow;
}

/*-----------------------------------------------------------------------------
	UStruct inlines.
-----------------------------------------------------------------------------*/

//
// UStruct inline comparer.
//
FORCEINLINE UBOOL UStruct::StructCompare( const void* A, const void* B )
{
	guardSlow(UStruct::StructCompare);
	for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(this); It; ++It )
		for( INT i=0; i<It->ArrayDim; i++ )
			if( !It->Matches(A,B,i) )
				return 0;
	unguardobjSlow;
	return 1;
}

/*-----------------------------------------------------------------------------
	C++ property macros.
-----------------------------------------------------------------------------*/

#define CPP_PROPERTY(name) \
	EC_CppProperty, (BYTE*)&((ThisClass*)NULL)->name - (BYTE*)NULL

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

