/*=============================================================================
	UnClsPrp.cpp: FProperty implementation
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "CorePrivate.h"

//!!fix hardcoded lengths
/*-----------------------------------------------------------------------------
	Helpers.
-----------------------------------------------------------------------------*/

//
// Parse a hex digit.
//
static INT HexDigit( TCHAR c )
{
	if( c>='0' && c<='9' )
		return c - '0';
	else if( c>='a' && c<='f' )
		return c + 10 - 'a';
	else if( c>='A' && c<='F' )
		return c + 10 - 'A';
	else
		return 0;
}

//
// Parse a token.
//
const TCHAR* ReadToken( const TCHAR* Buffer, TCHAR* String, INT MaxLength, UBOOL DottedNames=0 )
{
	guard(ReadToken);
	INT Count=0;
	if( *Buffer == 0x22 )
	{
		// Get quoted string.
		Buffer++;
		while( *Buffer && *Buffer!=0x22 && *Buffer!=13 && *Buffer!=10 && Count<MaxLength-1 )
		{
			if( *Buffer != '\\' )
			{
				String[Count++] = *Buffer++;
			}
			else if( *++Buffer=='\\' )
			{
				String[Count++] = '\\';
				Buffer++;
			}
			else
			{
				String[Count++] = HexDigit(Buffer[0])*16 + HexDigit(Buffer[1]);
				Buffer += 2;
			}
		}
		if( Count==MaxLength-1 )
		{
			debugf( NAME_Warning, TEXT("ReadToken: Quoted string too long") );
			return NULL;
		}
		if( *Buffer++!=0x22 )
		{
			warnf( NAME_Warning, TEXT("ReadToken: Bad quoted string") );
			return NULL;
		}
	}
	else if( appIsAlnum( *Buffer ) )
	{
		// Get identifier.
		while
		(	(appIsAlnum(*Buffer) || *Buffer=='_' || *Buffer=='-' || *Buffer=='[' || *Buffer==']' || (DottedNames && *Buffer=='.' ))
		&&	Count<MaxLength-1 )
			String[Count++] = *Buffer++;
		if( Count==MaxLength-1 )
		{
			debugf( NAME_Warning, TEXT("ReadToken: Alphanumeric overflow") );
			return NULL;
		}
	}
	else
	{
		// Get just one.
		String[Count++] = *Buffer;
	}
	String[Count] = 0;
	return Buffer;
	unguard;
}

/*-----------------------------------------------------------------------------
	UProperty implementation.
-----------------------------------------------------------------------------*/

//
// Constructors.
//
UProperty::UProperty()
:	UField( NULL )
,	ArrayDim( 1 )
,	NextRef( NULL )
{}
UProperty::UProperty( ECppProperty, INT InOffset, const TCHAR* InCategory, DWORD InFlags )
:	UField( NULL )
,	ArrayDim( 1 )
,	PropertyFlags( InFlags )
#ifndef _XBOX
,	Category( InCategory )
#endif
,	Offset( InOffset )
,	NextRef( NULL )
{
	GetOuterUField()->AddCppProperty( this );
}
void UProperty::CleanupDestroyed( BYTE* Data ) const {}

//
// Serializer.
//
void UProperty::Serialize( FArchive& Ar )
{
	guard(UProperty::Serialize);
	Super::Serialize( Ar );

	// Archive the basic info.
#ifdef _XBOX
	FName Category = NAME_None;
#endif
	Ar << ArrayDim << PropertyFlags << Category;
	if( PropertyFlags & CPF_Net )
		Ar << RepOffset;
	if( Ar.IsLoading() )
	{
		Offset = 0;
		ConstructorLinkNext = NULL;
	}
	unguardobj;
}

//
// Export this class property to an output
// device as a C++ header file.
//
void UProperty::ExportCpp( FOutputDevice& Out, UBOOL IsLocal, UBOOL IsParm, UBOOL IsEvent, UBOOL IsStruct ) const
{
	guard(UProperty::ExportCpp)
	TCHAR ArrayStr[80] = TEXT("");
	if
	(	IsParm
	&&	IsA(UStrProperty::StaticClass())
	&&	!(PropertyFlags & CPF_OutParm) )
		Out.Log( TEXT("const ") );
	ExportCppItem( Out, IsEvent || IsStruct );
	if( ArrayDim != 1 )
		appSprintf( ArrayStr, TEXT("[%i]"), ArrayDim );
	if( IsA(UBoolProperty::StaticClass()) )
	{
		if( ArrayDim==1 && !IsLocal && !IsParm )
			Out.Logf( TEXT(" %s%s:1"), GetName(), ArrayStr );
		else if( IsParm && (PropertyFlags & CPF_OutParm) )
			Out.Logf( TEXT("& %s%s"), GetName(), ArrayStr );
		else
			Out.Logf( TEXT(" %s%s"), GetName(), ArrayStr );
	}
	else if( IsA(UStrProperty::StaticClass()) )
	{
		if( IsParm && ArrayDim>1 )
			Out.Logf( TEXT("* %s"), GetName() );
		else if( IsParm )
			Out.Logf( TEXT("& %s"), GetName() );
		else if( IsLocal )
			Out.Logf( TEXT(" %s"), GetName() );
		else if( IsStruct )
			Out.Logf( TEXT(" %s%s"), GetName(), ArrayStr );
		else
			Out.Logf( TEXT("NoInit %s%s"), GetName(), ArrayStr );
	}
	else
	{
		if( IsParm && ArrayDim>1 )
			Out.Logf( TEXT("* %s"), GetName() );
		else if( IsParm && (PropertyFlags & CPF_OutParm) )
			Out.Logf( TEXT("& %s%s"), GetName(), ArrayStr );
		else
			Out.Logf( TEXT(" %s%s"), GetName(), ArrayStr );
	}
	unguardobj;
}

//
// Export the contents of a property.
//
UBOOL UProperty::ExportText
(
	INT		Index,
	TCHAR*	ValueStr,
	BYTE*	Data,
	BYTE*	Delta,
	INT		PortFlags
) const
{
	guard(UProperty::ExportText);
	ValueStr[0]=0;

	/* rjp struct localization */
	if ( (PortFlags & PPF_LocalizedOnly) && !IsLocalized() )
		return 0;
	// -- rjp

	if( Data==Delta || !Matches(Data,Delta,Index) )
	{
		ExportTextItem
		(
			ValueStr,
			Data + Offset + Index * ElementSize,
			Delta ? (Delta + Offset + Index * ElementSize) : NULL,
			PortFlags
		);
		return 1;
	}
	else return 0;
	unguardobj;
}

//
// Copy a unique instance of a value.
//
void UProperty::CopySingleValue( void* Dest, void* Src, UObject* SuperObject ) const
{
	guardSlow(UProperty::CopySingleValue);
	appMemcpy( Dest, Src, ElementSize );
	unguardobjSlow;
}

//
// Destroy a value.
//
void UProperty::DestroyValue( void* Dest ) const
{}

//
// Net serialization.
//
UBOOL UProperty::NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const
{
	guardSlow(UProperty::NetSerializeItem);
	SerializeItem( Ar, Data, 0 );
	return 1;
	unguardobjSlow;
}

/* rjp struct localization */
UBOOL UProperty::IsLocalized() const
{
	return PropertyFlags & CPF_Localized;
}

//
// Return whether the property should be exported.
//
UBOOL UProperty::Port() const
{
	return
	(	GetSize()
#ifndef _XBOX
	&&	(Category!=NAME_None || !(PropertyFlags & (CPF_Transient | CPF_Native)))
#endif
	&&	GetFName()!=NAME_Class );
}

//
// Return type id for encoding properties in .u files.
//
BYTE UProperty::GetID() const
{
	return GetClass()->GetFName().GetIndex();
}

//
// Copy a complete value.
//
void UProperty::CopyCompleteValue( void* Dest, void* Src, UObject* SuperObject ) const
{
	guardSlow(UProperty::CopyCompleteValue);
	for( INT i=0; i<ArrayDim; i++ )
		CopySingleValue( (BYTE*)Dest+i*ElementSize, (BYTE*)Src+i*ElementSize, SuperObject );
	unguardobjSlow;
}

//
// Link property loaded from file.
//
void UProperty::Link( FArchive& Ar, UProperty* Prev )
{}

IMPLEMENT_CLASS(UProperty);

/*-----------------------------------------------------------------------------
	UByteProperty.
-----------------------------------------------------------------------------*/

void UByteProperty::Link( FArchive& Ar, UProperty* Prev )
{
	guard(UByteProperty::Link);
	Super::Link( Ar, Prev );
	ElementSize = sizeof(BYTE);
	Offset      = Align( GetOuterUField()->GetPropertiesSize(), sizeof(BYTE) );
	unguardobj;
}
void UByteProperty::CopySingleValue( void* Dest, void* Src, UObject* SuperObject ) const
{
	guardSlow(UByteProperty::CopySingleValue);
	*(BYTE*)Dest = *(BYTE*)Src;
	unguardSlow;
}
void UByteProperty::CopyCompleteValue( void* Dest, void* Src, UObject* SuperObject ) const
{
	guardSlow(UByteProperty::CopyCompleteValue);
	if( ArrayDim==1 )
		*(BYTE*)Dest = *(BYTE*)Src;
	else
		appMemcpy( Dest, Src, ArrayDim );
	unguardSlow;
}
UBOOL UByteProperty::Identical( const void* A, const void* B ) const
{
	guardSlow(UByteProperty::Identical);
	return *(BYTE*)A == (B ? *(BYTE*)B : 0);
	unguardobjSlow;
}
void UByteProperty::SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const
{
	guardSlow(UByteProperty::SerializeItem);
	Ar << *(BYTE*)Value;
	unguardobjSlow;
}
UBOOL UByteProperty::NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const
{
	guardSlow(UByteProperty::NetSerializeItem);
	Ar.SerializeBits( Data, Enum ? appCeilLogTwo(Enum->Names.Num()) : 8 );
	return 1;
	unguardobjSlow;
}
void UByteProperty::Serialize( FArchive& Ar )
{
	guard(UByteProperty::Serialize);
	Super::Serialize( Ar );
	Ar << Enum;
	unguardobj;
}
void UByteProperty::ExportCppItem( FOutputDevice& Out, UBOOL IsParam ) const
{
	guard(UByteProperty::ExportCppItem);
	Out.Log( TEXT("BYTE") );
	unguardobj;
}
void UByteProperty::ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const
{
	guard(UByteProperty::ExportTextItem);
	if( Enum )
		appSprintf( ValueStr, TEXT("%s"), *PropertyValue < Enum->Names.Num() ? *Enum->Names(*PropertyValue) : TEXT("(INVALID)") );
	else
		appSprintf( ValueStr, TEXT("%i"), *PropertyValue );
	unguardobj;
}
const TCHAR* UByteProperty::ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const
{
	guard(UByteProperty::ImportText);
	TCHAR Temp[1024];
	if( Enum )
	{
		Buffer = ReadToken( Buffer, Temp, ARRAY_COUNT(Temp) );
		if( !Buffer )
			return NULL;
		FName EnumName = FName( Temp, FNAME_Find );
		if( EnumName != NAME_None )
		{
			INT EnumIndex=0;
			if( Enum->Names.FindItem( EnumName, EnumIndex ) )
			{
				*(BYTE*)Data = EnumIndex;
				return Buffer;
			}
		}
	}
	if( appIsDigit(*Buffer) )
	{
		*(BYTE*)Data = appAtoi( Buffer );
		while( *Buffer>='0' && *Buffer<='9' )
			Buffer++;
	}
	else
	{
		//debugf( "Import: Missing byte" );
		return NULL;
	}
	return Buffer;
	unguardobj;
}
UBOOL UByteProperty::HasValue( const BYTE* Data ) const
{
	guard(UByteProperty::HasValue);
	return *(BYTE*)Data != 0;
	unguardobj;
}

void UByteProperty::ClearValue( BYTE* Data ) const
{
	guard(UByteProperty::ClearValue);

	check(Data);
	*(BYTE*)Data = 0;

	unguardobj;
}

IMPLEMENT_CLASS(UByteProperty);

/*-----------------------------------------------------------------------------
	UIntProperty.
-----------------------------------------------------------------------------*/

void UIntProperty::Link( FArchive& Ar, UProperty* Prev )
{
	guard(UIntProperty::Link);
	Super::Link( Ar, Prev );
	ElementSize = sizeof(INT);
	Offset      = Align( GetOuterUField()->GetPropertiesSize(), sizeof(INT) );
	unguardobj;
}
void UIntProperty::CopySingleValue( void* Dest, void* Src, UObject* SuperObject ) const
{
	guardSlow(UIntProperty::CopySingleValue);
	*(INT*)Dest = *(INT*)Src;
	unguardSlow;
}
void UIntProperty::CopyCompleteValue( void* Dest, void* Src, UObject* SuperObject ) const
{
	guardSlow(UIntProperty::CopyCompleteValue);
	if( ArrayDim==1 )
		*(INT*)Dest = *(INT*)Src;
	else
	{
		/*
		for( INT i=0; i<ArrayDim; i++ )
			((INT*)Dest)[i] = ((INT*)Src)[i];
		*/
		appMemcpy(Dest,Src,ArrayDim*ElementSize);
	}
	unguardSlow;
}
UBOOL UIntProperty::Identical( const void* A, const void* B ) const
{
	guardSlow(UIntProperty::Identical);
	return *(INT*)A == (B ? *(INT*)B : 0);
	unguardobjSlow;
}
void UIntProperty::SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const
{
	guardSlow(UIntProperty::SerializeItem);
	Ar << *(INT*)Value;
	unguardobjSlow;
}
UBOOL UIntProperty::NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const
{
	guardSlow(UIntProperty::NetSerializeItem);
	Ar << *(INT*)Data;
	return 1;
	unguardSlow;
}
void UIntProperty::ExportCppItem( FOutputDevice& Out, UBOOL IsParam ) const
{
	guard(UIntProperty::ExportCppItem);
	// A native placeholder (an int standing in for a C++ pointer) is exported as its C++ type,
	// PTRINT, so the generated headers are right at 64 bits (Core/Src/UnClass.cpp, GNativePlaceholders).
	extern CORE_API const TCHAR* GetNativePlaceholderCppType( const UProperty* IntProperty );
	Out.Log( GetNativePlaceholderCppType( this ) );
	unguardobj;
}
void UIntProperty::ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const
{
	guard(UIntProperty::ExportTextItem);
	appSprintf( ValueStr, TEXT("%i"), *(INT *)PropertyValue );
	unguardobj;
}
const TCHAR* UIntProperty::ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const
{
	guard(UIntProperty::ImportText);
	if( *Buffer=='-' || (*Buffer>='0' && *Buffer<='9') )
	{
		*(INT*)Data = appAtoi( Buffer );
		while( *Buffer=='-' || (*Buffer>='0' && *Buffer<='9') )
			Buffer++;
	}
	return Buffer;
	unguardobj;
}
UBOOL UIntProperty::HasValue( const BYTE* Data ) const
{
	guard(UIntProperty::HasValue);
	return *(INT*)Data != 0;
	unguardobj;
}

void UIntProperty::ClearValue( BYTE* Data ) const
{
	guard(UIntProperty::ClearValue);
	*(INT*)Data = 0;
	unguardobj;
}

IMPLEMENT_CLASS(UIntProperty);

/*-----------------------------------------------------------------------------
	UDelegateProperty.
-----------------------------------------------------------------------------*/

void UDelegateProperty::Link( FArchive& Ar, UProperty* Prev )
{
	guard(UDelegateProperty::Link);
	Super::Link( Ar, Prev );
	ElementSize = sizeof(FScriptDelegate);
	Offset      = Align( GetOuterUField()->GetPropertiesSize(), sizeof(INT) );
	PropertyFlags |= CPF_NeedCtorLink;
	unguardobj;
}
void UDelegateProperty::CopySingleValue( void* Dest, void* Src, UObject* SuperObject ) const
{
	guardSlow(UDelegateProperty::CopySingleValue);
	*(FScriptDelegate*)Dest = *(FScriptDelegate*)Src;
	unguardSlow;
}
void UDelegateProperty::CopyCompleteValue( void* Dest, void* Src, UObject* SuperObject ) const
{
	guardSlow(UDelegateProperty::CopyCompleteValue);

	FScriptDelegate *Source = (FScriptDelegate*)Src,
					*Destination = (FScriptDelegate*)Dest;
 
/*	rjp --
	SuperObject will always be the top-level class/object.  When instancing subobjects,
	SuperObject is the class that contains the instanced subobject.  Here we must decide
	if this delegate is being assigned inside a subobject definition, or at the class level.
*/
	if( SuperObject && (SuperObject->GetFName() != NAME_None) && Source->FunctionName != NAME_None )
	{
	// Uncomment to debug
	//	if (Source->FunctionName != NAME_None && !appStricmp(*Source->FunctionName,TEXT("PreRenderIcon")))
	//		debugf(TEXT("breakpoint here"));
		// Haven't seen arrays of delegates yet, so ArrayDim should always be 1.
		if( ArrayDim==1)
		{
			/*
				The source object always points to the object where the delegate assignment
				occurs.  If the delegate assignment is not in default properties, Cls will
				be none.
			*/
			UClass* Cls = Cast<UClass>(Source->Object);

			// Uncomment to debug
//			if (Cls && appStricmp(Cls->GetName(),TEXT("RungRosterPanel")) == 0) // Substitute the .uc class name where the assignment happens
//				debugf(TEXT("breakpoint here too"));

			// Assign the function name.  This will be valid for inherited classes.
			Destination->FunctionName = Source->FunctionName;

			/*
				DestObject will always be the object containing the delegate.  If a delegate
				is being assigned to a function in the same class, we want the new delegate's
				object pointer to point to DestObject.  Otherwise, we still have some work to
				do.
			*/
			UObject* DestObject = (UObject*)(((BYTE*)Dest) - Offset);
			if (Cls)
			{
				/* 
					The delgate was assigned in default properties.
					If SuperObject->IsA(Cls), we are assigning the delegate to a function
					in the top level class. Both of the following would qualify:
					ex:
					defaultproperties
					{
						OnChange=InternalOnChange
						Begin Object Class=moCheckBox Name=SomeCheckBox
							OnChange=InternalOnChange
						End Object
					}
				*/
				if (SuperObject->IsA(Cls))
					Destination->Object = SuperObject;


				/*
					This is the really tricky one.  We are instancing a subobject which assigns
					a delegate in its default properties block, and we aren't overriding that
					delegate assignment in our subobject definition.  
					
					This could also happen when a subobject we're instancing has its own instanced
					subobjects that assign delegates in default properties.
				*/
				else if (DestObject && DestObject->IsA(Cls))
					Destination->Object = DestObject;

				else Destination->Object = Source->Object;
			}
			else
			{
				/*
					The delegate assignment is occuring somewhere in script.  However,
					it is still possible for the source delegate's object to be pointing at
					the default instanced object, so let's assign the new delegate's object
					to DestObject, but only if the delegate is actually assigned to a function!
				*/
				if (DestObject)
					Destination->Object = DestObject;

				// Either we don't have an object, or we don't have a function.  Either way,
				// assign the delegate to the source object, which should always be NULL, if
				// we're here.
				else 
					Destination->Object = Source->Object;
			}
		}
		else
		{
			/*
				If we somehow have an array of delegates, just do the same procedure
				as above for each element.
			*/
			for( INT i=0; i<ArrayDim; i++ )
			{
				UClass* Cls = Cast<UClass>(Source[i].Object);			
				Destination[i].FunctionName = Source[i].FunctionName;

				// I don't know if this notation actually works correctly. I cannot test for arrays
				// of delegates.
				UObject* DestObject = (UObject*)((((BYTE*)Dest)[i]) - (Offset + (i * sizeof(UObject*))));
				if (Cls)
				{
					if (SuperObject->IsA(Cls))
						Destination[i].Object = SuperObject;

					else if (DestObject && DestObject->IsA(Cls))
						Destination[i].Object = DestObject;

					else Destination[i].Object = Source[i].Object;
				}

				else
				{
					if (DestObject)
						Destination[i].Object = DestObject;

					else Destination[i].Object = Source[i].Object;
				}
			
			}
		}
	}
	else
	{
		/*	-- rjp
			We do not have a SuperObject if we are initializing the ClassDefaultObject during the
			initial loading of the package.  Assign the delegate to the default value.
		*/
		if( ArrayDim==1 )
			*Destination = *Source;
		else
			for( INT i=0; i<ArrayDim; i++ )
				Destination[i] = Source[i];
	}
	unguardSlow;
}
UBOOL UDelegateProperty::Identical( const void* A, const void* B ) const
{
	guardSlow(UDelegateProperty::Identical);
	FScriptDelegate* DA = (FScriptDelegate*)A;
	FScriptDelegate* DB = (FScriptDelegate*)B;
	if( !DB )
		return DA->Object==NULL;
	return ( DA->Object == DB->Object && DA->FunctionName == DB->FunctionName );
	unguardobjSlow;
}
void UDelegateProperty::SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const
{
	guardSlow(UDelegateProperty::SerializeItem);
	Ar << *(FScriptDelegate*)Value;
	unguardobjSlow;
}
UBOOL UDelegateProperty::NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const
{
	guardSlow(UDelegateProperty::NetSerializeItem);
	Ar << *(FScriptDelegate*)Data;
	return 1;
	unguardSlow;
}
void UDelegateProperty::ExportCppItem( FOutputDevice& Out, UBOOL IsParam ) const
{
	guard(UDelegateProperty::ExportCppItem);
	Out.Log( TEXT("FScriptDelegate") );
	unguardobj;
}
void UDelegateProperty::ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const
{
	guard(UDelegateProperty::ExportTextItem);
	UObject* Obj = (*(FScriptDelegate*)PropertyValue).Object;
	appSprintf( ValueStr, TEXT("%s.%s"), Obj ? Obj->GetName() : TEXT("None"), Obj ? *(*(FScriptDelegate*)PropertyValue).FunctionName : TEXT("None"));
	Obj = NULL;
	unguardobj;
}
const TCHAR* UDelegateProperty::ImportText( const TCHAR* Buffer, BYTE* PropertyValue, INT PortFlags ) const
{
	guard(UDelegateProperty::ImportText);
	TCHAR ObjName[NAME_SIZE];
	TCHAR FuncName[NAME_SIZE];
	// Get object name
	INT i;
	for( i=0; *Buffer && *Buffer != '.'; Buffer++ )
		ObjName[i++] = *Buffer;
	ObjName[i] = '\0';
	// Get function name
	if( *Buffer )
	{
		Buffer++;
		for( i=0; *Buffer; Buffer++ )
			FuncName[i++] = *Buffer;
		FuncName[i] = '\0';
	}
	else
		FuncName[0] = '\0';
	UObject* Object = StaticFindObject( UObject::StaticClass(), ANY_PACKAGE, ObjName );
	UFunction* Func = NULL;
	if( Object )
	{
        UClass* Cls = Cast<UClass>(Object);
		if( !Cls )
			Cls = Object->GetClass();
		// Check function params.
		Func = FindField<UFunction>( Cls, FuncName );
		if( Func )
		{
			// Find the delegate UFunction to check params
			checkSlow(appStrlen(GetName()) > 2 );
			appStrcpy( FuncName, &(GetName())[2] );
			check(appStrlen(FuncName) > 10 );
			FuncName[appStrlen(FuncName)-10] = '\0';	//!! hackish   GetName() is something like __DelegateName__Delegate
            UFunction* Delegate = FindField<UFunction>( CastChecked<UClass>(GetOuter()), FuncName );
			check(Delegate);
			// check return type and params
			if(	Func->NumParms == Delegate->NumParms )
			{
				INT Count=0;
				for( TFieldIterator<UProperty,CLASS_IsAUProperty> It1(Func),It2(Delegate); Count<Delegate->NumParms; ++It1,++It2,++Count )
				{
					if( It1->GetClass()!=It2->GetClass() || (It1->PropertyFlags&CPF_OutParm)!=(It2->PropertyFlags&CPF_OutParm) )
					{
						Func = NULL;
						break;
					}
				}
			}
			else
				Func = NULL;
		}
	}
	(*(FScriptDelegate*)PropertyValue).Object		= Func ? Object				: NULL;
	(*(FScriptDelegate*)PropertyValue).FunctionName = Func ? Func->GetFName()	: NAME_None;
	return Buffer;
	unguardobj;
}

void UDelegateProperty::Serialize( FArchive& Ar )
{
	guard(UDelegateProperty::Serialize);
	Super::Serialize( Ar );
	Ar << Function;
	unguardobj;
}
void UDelegateProperty::CleanupDestroyed( BYTE* Data ) const
{
	guard(UDelegateProperty::CleanupDestroyed);
	for(int i=0; i<ArrayDim; i++)
	{
		FScriptDelegate* Delegate = (FScriptDelegate*)(Data+i*ElementSize);
		if(Delegate->Object)
		{
			check(Delegate->Object->IsValid());
			if( Delegate->Object->IsPendingKill() )
			{
				Delegate->Object->Modify();
				Delegate->Object = NULL;
				Delegate->FunctionName = NAME_None;
			}
		}
	}
	unguardobj;
}


/**
 * Useful method for breaking links to objects that you want to delete
 *
 * @param Data Pointer to the object having refs cleaned up
 * @param Destroyed Pointer to the object that is being destroyed
 *
 * @return Whether this property contained a reference to the destroyed or not
 */
UBOOL UDelegateProperty::CleanupDestroyed(BYTE* Data, UObject* Destroyed) const
{
	guard(UDelegateProperty::CleanupDestroyed);
	// Let the caller know
	UBOOL bReferenced = 0;
	// Iterate through all array entries and clean those up
	for (INT Index = 0; Index < ArrayDim; Index++)
	{
		FScriptDelegate* Delegate = (FScriptDelegate*)(Data + Index * ElementSize);
		// If these match, zero the property
		if (Delegate->Object && Delegate->Object == Destroyed)
		{
			Delegate->Object->Modify();
			Delegate->Object = NULL;
			Delegate->FunctionName = NAME_None;
			bReferenced |= 1;
		}
	}
	return bReferenced;
	unguardobj;
}

UBOOL UDelegateProperty::HasValue( const BYTE* Data ) const
{
	guard(UDelegateProperty::HasValue);
	return (*(FScriptDelegate*)Data).Object != NULL && (*(FScriptDelegate*)Data).FunctionName != NAME_None;
	unguardobj;
}

void UDelegateProperty::ClearValue( BYTE* Data ) const
{
	guard(UDelegateProperty::ClearValue);

	(*(FScriptDelegate*)Data).Object = NULL;
	(*(FScriptDelegate*)Data).FunctionName = NAME_None;
	unguardobj;
}

IMPLEMENT_CLASS(UDelegateProperty);

/*-----------------------------------------------------------------------------
	UBoolProperty.
-----------------------------------------------------------------------------*/

void UBoolProperty::Link( FArchive& Ar, UProperty* Prev )
{
	guard(UBoolProperty::Link);
	Super::Link( Ar, Prev );
	UBoolProperty* PrevBool = Cast<UBoolProperty>( Prev );
	if( GetOuterUField()->MergeBools() && PrevBool && NEXT_BITFIELD(PrevBool->BitMask) )
	{
		Offset  = Prev->Offset;
		BitMask = NEXT_BITFIELD(PrevBool->BitMask);
	}
	else
	{
		Offset  = Align(GetOuterUField()->GetPropertiesSize(),sizeof(BITFIELD));
		BitMask = FIRST_BITFIELD;
	}
	ElementSize = sizeof(BITFIELD);
	unguardobj;
}
void UBoolProperty::Serialize( FArchive& Ar )
{
	guard(UBoolProperty::Serialize);
	Super::Serialize( Ar );
	if( !Ar.IsLoading() && !Ar.IsSaving() )
#if __MWERKS__
	{
		QWORD Temp = BitMask;
		Ar << Temp;
		BitMask = Temp;
	}
#else
		Ar << BitMask;
#endif
	unguardobj;
}
void UBoolProperty::ExportCppItem( FOutputDevice& Out, UBOOL IsParam ) const
{
	guard(UBoolProperty::ExportCppItem);
	Out.Log( TEXT("BITFIELD") );
	unguardobj;
}
void UBoolProperty::ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const
{
	guard(UBoolProperty::ExportTextItem);
	TCHAR* Temp
	=	(TCHAR*) ((PortFlags & PPF_Localized)
	?	(((*(BITFIELD*)PropertyValue) & BitMask) ? GTrue  : GFalse )
	:	(((*(BITFIELD*)PropertyValue) & BitMask) ? TEXT("True") : TEXT("False")));
	appSprintf( ValueStr, TEXT("%s"), Temp );
	unguardobj;
}
const TCHAR* UBoolProperty::ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const
{
	guard(UBoolProperty::ImportText);
	TCHAR Temp[1024];
	Buffer = ReadToken( Buffer, Temp, ARRAY_COUNT(Temp) );
	if( !Buffer )
		return NULL;
	if( appStricmp(Temp,TEXT("1"))==0 || appStricmp(Temp,TEXT("True"))==0 || appStricmp(Temp,GTrue)==0 )
	{
		*(BITFIELD*)Data |= BitMask;
	}
	else if( appStricmp(Temp,TEXT("0"))==0 || appStricmp(Temp,TEXT("False"))==0  || appStricmp(Temp,GFalse)==0 )
	{
		*(BITFIELD*)Data &= ~BitMask;
	}
	else
	{
		//debugf( "Import: Failed to get bool" );
		return NULL;
	}
	return Buffer;
	unguardobj;
}
UBOOL UBoolProperty::Identical( const void* A, const void* B ) const
{
	guardSlow(UBoolProperty::Identical);
	return ((*(BITFIELD*)A ^ (B ? *(BITFIELD*)B : 0)) & BitMask) == 0;
	unguardobjSlow;
}
void UBoolProperty::SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const
{
	guardSlow(UBoolProperty::SerializeItem);
	BYTE B = (*(BITFIELD*)Value & BitMask) ? 1 : 0;
	Ar << B;
	if( B ) *(BITFIELD*)Value |=  BitMask;
	else    *(BITFIELD*)Value &= ~BitMask;
	unguardobjSlow;
}
UBOOL UBoolProperty::NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const
{
	guardSlow(UByteProperty::NetSerializeItem);
	BYTE Value = ((*(BITFIELD*)Data & BitMask)!=0);
	Ar.SerializeBits( &Value, 1 );
	if( Value )
		*(BITFIELD*)Data |= BitMask;
	else
		*(BITFIELD*)Data &= ~BitMask;
	return 1;
	unguardobjSlow;
}
void UBoolProperty::CopySingleValue( void* Dest, void* Src, UObject* SuperObject ) const
{
	guardSlow(UProperty::CopySingleValue);
	*(BITFIELD*)Dest = (*(BITFIELD*)Dest & ~BitMask) | (*(BITFIELD*)Src & BitMask);
	unguardobjSlow;
}
UBOOL UBoolProperty::HasValue( const BYTE* Data ) const
{
	guard(UBoolProperty::HasValue);
	return *(BITFIELD*)Data & BitMask;
	unguardobj;
}

void UBoolProperty::ClearValue( BYTE* Data ) const
{
	guard(UBoolProperty::ClearValue);
	*(BITFIELD*)Data &= ~BitMask;
	unguardobj;
}

IMPLEMENT_CLASS(UBoolProperty);

/*-----------------------------------------------------------------------------
	UFloatProperty.
-----------------------------------------------------------------------------*/

void UFloatProperty::Link( FArchive& Ar, UProperty* Prev )
{
	guard(UFloatProperty::Link);
	Super::Link( Ar, Prev );
	ElementSize = sizeof(FLOAT);
	Offset      = Align( GetOuterUField()->GetPropertiesSize(), sizeof(FLOAT) );
	unguardobj;
}
void UFloatProperty::CopySingleValue( void* Dest, void* Src, UObject* SuperObject ) const
{
	guardSlow(UFloatProperty::CopySingleValue);
	*(FLOAT*)Dest = *(FLOAT*)Src;
	unguardSlow;
}
void UFloatProperty::CopyCompleteValue( void* Dest, void* Src, UObject* SuperObject ) const
{
	guardSlow(UFloatProperty::CopyCompleteValue);
	if( ArrayDim==1 )
		*(FLOAT*)Dest = *(FLOAT*)Src;
	else
	{
		/*
		for( INT i=0; i<ArrayDim; i++ )
			((FLOAT*)Dest)[i] = ((FLOAT*)Src)[i];
		*/
		appMemcpy( Dest, Src, ArrayDim*ElementSize );
	}
	unguardSlow;
}
UBOOL UFloatProperty::Identical( const void* A, const void* B ) const
{
	guardSlow(UFloatProperty::Identical);
	return *(FLOAT*)A == (B ? *(FLOAT*)B : 0);
	unguardobjSlow;
}
void UFloatProperty::SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const
{
	guardSlow(UFloatProperty::SerializeItem);
	Ar << *(FLOAT*)Value;
	unguardobjSlow;
}
UBOOL UFloatProperty::NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const
{
	guardSlow(UFloatProperty::NetSerializeItem);
	Ar << *(FLOAT*)Data;
	return 1;
	unguardSlow;
}
void UFloatProperty::ExportCppItem( FOutputDevice& Out, UBOOL IsParam ) const
{
	guard(UFloatProperty::ExportCppItem);
	Out.Log( TEXT("FLOAT") );
	unguardobj;
}
void UFloatProperty::ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const
{
	guard(UFloatProperty::ExportTextItem);
	appSprintf( ValueStr, TEXT("%f"), *(FLOAT*)PropertyValue );
	unguardobj;
}
const TCHAR* UFloatProperty::ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const
{
	guard(UFloatProperty::ImportText);
	if ( *Buffer == '+' || *Buffer == '-' || *Buffer == '.' || (*Buffer >= '0' && *Buffer <= '9') )
	{
		// only import this value if Buffer is numeric
		*(FLOAT*)Data = appAtof(Buffer);
		while( *Buffer == '+' || *Buffer == '-' || *Buffer == '.' || (*Buffer >= '0' && *Buffer <= '9') )
			Buffer++;

		if ( *Buffer == 'f' || *Buffer == 'F' )
			Buffer++;
	}
//	else
//		warnf(TEXT("Failed to import float value for property '%s':%s"), GetName(), Buffer);

	return Buffer;
	unguardobj;
}
UBOOL UFloatProperty::HasValue( const BYTE* Data ) const
{
	guard(UFloatProperty::HasValue);
	return *(FLOAT*)Data != 0.f;
	unguardobj;
}

void UFloatProperty::ClearValue( BYTE* Data ) const
{
	guard(UFloatProperty::ClearValue);
	*(FLOAT*)Data = 0.f;
	unguardobj;
}

IMPLEMENT_CLASS(UFloatProperty);

/*-----------------------------------------------------------------------------
	UObjectProperty.
-----------------------------------------------------------------------------*/

void UObjectProperty::CleanupDestroyed( BYTE* Data ) const
{
	guard(UObjectProperty::CleanupDestroyed);
	for(int i=0; i<ArrayDim; i++)
	{
		UObject*& Obj=*(UObject**)(Data+i*ElementSize);
		if(Obj)
		{
			check(Obj->IsValid());
			if( Obj->IsPendingKill() )
			{
				Obj->Modify();
				Obj = NULL;
			}
		}
	}
	unguardobj;
}

/**
 * Useful method for breaking links to objects that you want to delete
 *
 * @param Data Pointer to the object having refs cleaned up
 * @param Destroyed Pointer to the object that is being destroyed
 *
 * @return Whether this property contained a reference to the destroyed or not
 */
UBOOL UObjectProperty::CleanupDestroyed(BYTE* Data, UObject* Destroyed) const
{
	guard(UObjectProperty::CleanupDestroyed);
	// Let the caller know
	UBOOL bReferenced = 0;
	// Iterate through all array entries and clean those up
	for (INT Index = 0; Index < ArrayDim; Index++)
	{
		UObject*& Obj = *(UObject**)(Data + Index * ElementSize);
		// If these match, zero the property
		if(Obj && Obj == Destroyed)
		{
			Obj->Modify();
			Obj = NULL;
			bReferenced |= 1;
		}
	}
	return bReferenced;
	unguardobj;
}

void UObjectProperty::Link( FArchive& Ar, UProperty* Prev )
{
	guard(UObjectProperty::Link);
	Super::Link( Ar, Prev );
	ElementSize = sizeof(UObject*);
	// 4, not sizeof(UObject*). The C++ mirrors of script classes are #pragma pack(4) (Epic's /Zp4), so an
	// 8-byte pointer sits on a 4-byte boundary there too. The same value as sizeof on every 32-bit target.
	Offset      = Align( GetOuterUField()->GetPropertiesSize(), 4 );
	if( ((PropertyFlags & CPF_EditInline) && (PropertyFlags & CPF_ExportObject)) || (PropertyFlags & CPF_Automated) )
		PropertyFlags |= CPF_NeedCtorLink;
	unguardobj;
}
void UObjectProperty::CopySingleValue( void* Dest, void* Src, UObject* SuperObject ) const
{
	guardSlow(UObjectProperty::CopySingleValue);
	*(UObject**)Dest = *(UObject**)Src;
	unguardSlow;
}
void UObjectProperty::CopyCompleteValue( void* Dest, void* Src, UObject* SuperObject ) const
{
	guardSlow(UObjectProperty::CopyCompleteValue);

	//jg -- Hack to avoid instancing subobjects in netplay
    //!! Don't instance subobjects if running UCC MAKE.
	if(!(GUglyHackFlags & 0x20000) && !(GIsEditor&&GIsUCC) && (PropertyFlags & CPF_NeedCtorLink) && SuperObject )
	{
		for( INT i=0; i<ArrayDim; i++ )
		{
			if( ((UObject**)Src)[i] )
			{
				UClass* Cls = ((UObject**)Src)[i]->GetClass();
				((UObject**)Dest)[i] = StaticConstructObject( Cls, SuperObject->GetOuter(), NAME_None, 0, ((UObject**)Src)[i], GError, SuperObject );
			}
			else
				((UObject**)Dest)[i] = ((UObject**)Src)[i];
		}
	}
	else
	{
		for( INT i=0; i<ArrayDim; i++ )
			((UObject**)Dest)[i] = ((UObject**)Src)[i];
	}
	unguardSlow;
}
UBOOL UObjectProperty::Identical( const void* A, const void* B ) const
{
	guardSlow(UObjectProperty::Identical);
	return *(UObject**)A == (B ? *(UObject**)B : NULL);
	unguardobjSlow;
}
void UObjectProperty::SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const
{
	guardSlow(UObjectProperty::SerializeItem);
	Ar << *(UObject**)Value;
	unguardobjSlow;
}
UBOOL UObjectProperty::NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const
{
	guardSlow(UByteProperty::NetSerializeItem);
	return Map->SerializeObject( Ar, PropertyClass, *(UObject**)Data );
	unguardobjSlow;
}
void UObjectProperty::Serialize( FArchive& Ar )
{
	guard(UObjectProperty::Serialize);
	//!! HACKish: Propagate autoinstance property flag.  Needs to be here as it's the only time
	//            PropertyClass is guaranteed to be completely compiled.
	if( Ar.IsSaving() && (PropertyClass->ClassFlags&CLASS_AutoInstancedProps) )
		PropertyFlags |= CPF_EditInline | CPF_ExportObject;
	Super::Serialize( Ar );
	Ar << PropertyClass;
	unguardobj;
}
void UObjectProperty::ExportCppItem( FOutputDevice& Out, UBOOL IsParam ) const
{
	guard(UObjectProperty::ExportCppItem);
	Out.Logf( TEXT("class %s*"), PropertyClass->GetNameCPP() );
	unguardobj;
}
void UObjectProperty::ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const
{
	guard(UObjectProperty::ExportTextItem);
	UObject* Temp = *(UObject **)PropertyValue;
	if( Temp != NULL )
		appSprintf( ValueStr, TEXT("%s'%s'"), Temp->GetClass()->GetName(), Temp->GetPathName() );
	else
		appStrcpy( ValueStr, TEXT("None") );
	unguardobj;
}
const TCHAR* UObjectProperty::ImportText( const TCHAR* InBuffer, BYTE* Data, INT PortFlags ) const
{
	guard(UObjectProperty::ImportText);
    const TCHAR* Buffer = InBuffer;
	TCHAR Temp[1024], Other[1024];
	Buffer = ReadToken( Buffer, Temp, ARRAY_COUNT(Temp), 1 );
	if( !Buffer )
	{
		return NULL;
	}
	if( appStricmp( Temp, TEXT("None") )==0 )
	{
		*(UObject**)Data = NULL;
	}
	else
	{
		while( *Buffer == ' ' )
			Buffer++;
		if( *Buffer++ != '\'' )
		{
			*(UObject**)Data = StaticFindObject( PropertyClass, ANY_PACKAGE, Temp );
			if( !*(UObject**)Data )
            {
                if( PortFlags & PPF_CheckReferences )
					warnf( NAME_Error, TEXT("%s: unresolved reference to '%s'"), GetFullName(), InBuffer );
 				return NULL;
            }
		}
		else
		{
			Buffer = ReadToken( Buffer, Other, ARRAY_COUNT(Temp), 1 );
			if( !Buffer )
				return NULL;
			if( *Buffer++ != '\'' )
				return NULL;
			UClass* ObjectClass = FindObject<UClass>( ANY_PACKAGE, Temp );
			if( !ObjectClass )
            {
                if( PortFlags & PPF_CheckReferences )
					warnf( NAME_Error, TEXT("%s: unresolved cast in '%s'"), GetFullName(), InBuffer );
				return NULL;
            }

			// Try the find the object.
			*(UObject**)Data = StaticFindObject( ObjectClass, ANY_PACKAGE, Other );

			// If we can't find it, try to load it.
			if( !*(UObject**)Data )
				*(UObject**)Data = StaticLoadObject( ObjectClass, NULL, Other, NULL, LOAD_NoWarn, NULL );

            if( *(UObject**)Data && (PortFlags & PPF_CheckReferences) && !(*(UObject**)Data)->GetClass()->IsChildOf(PropertyClass) )
				warnf( NAME_Error, TEXT("%s: bad cast in '%s'"), GetFullName(), InBuffer );

			// If we couldn't find it or load it, we'll have to do without it.
			if( !*(UObject**)Data )
            {
                if( PortFlags & PPF_CheckReferences )
					warnf( NAME_Error, TEXT("%s: unresolved reference to '%s'"), GetFullName(), InBuffer );
				return NULL;
            }
		}
	}
	return Buffer;
	unguardobj;
}
UBOOL UObjectProperty::HasValue( const BYTE* Data ) const
{
	guard(UObjectProperty::HasValue);
	return *(UObject**)Data != NULL;
	unguard;
}

void UObjectProperty::ClearValue( BYTE* Data ) const
{
	guard(UObjectProperty::ClearValue);
	*(UObject**)Data = NULL;
	unguardobj;
}

IMPLEMENT_CLASS(UObjectProperty);

/*-----------------------------------------------------------------------------
	UClassProperty.
-----------------------------------------------------------------------------*/

void UClassProperty::Serialize( FArchive& Ar )
{
	guard(UClassProperty::Serialize);
	Super::Serialize( Ar );
	Ar << MetaClass;
	check(MetaClass);
	unguardobj;
}
const TCHAR* UClassProperty::ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const
{
	guard(UClassProperty::ImportText);
	const TCHAR* Result = UObjectProperty::ImportText( Buffer, Data, PortFlags );
	if( Result )
	{
		// Validate metaclass.
		UClass*& C = *(UClass**)Data;
		if( C && C->GetClass()!=UClass::StaticClass() || !C->IsChildOf(MetaClass) )
			C = NULL;
	}
	return Result;
	unguard;
}
UBOOL UClassProperty::HasValue( const BYTE* Data ) const
{
	guard(UClassProperty::HasValue);
	return *(UClass**)Data != NULL;
	unguardobj;
}

void UClassProperty::ClearValue( BYTE* Data ) const
{
	guard(UClassProperty::ClearValue);
	*(UClass**)Data = NULL;
	unguardobj;
}

IMPLEMENT_CLASS(UClassProperty);

/*-----------------------------------------------------------------------------
	UNameProperty.
-----------------------------------------------------------------------------*/

void UNameProperty::Link( FArchive& Ar, UProperty* Prev )
{
	guard(UNameProperty::Link);
	Super::Link( Ar, Prev );
	ElementSize = sizeof(FName);
	Offset      = Align( GetOuterUField()->GetPropertiesSize(), sizeof(FName) );
	unguardobj;
}
void UNameProperty::CopySingleValue( void* Dest, void* Src, UObject* SuperObject ) const
{
	guardSlow(UNameProperty::CopySingleValue);
	*(FName*)Dest = *(FName*)Src;
	unguardSlow;
}
void UNameProperty::CopyCompleteValue( void* Dest, void* Src, UObject* SuperObject ) const
{
	guardSlow(UNameProperty::CopyCompleteValue);
	if( ArrayDim==1 )
		*(FName*)Dest = *(FName*)Src;
	else
		for( INT i=0; i<ArrayDim; i++ )
			((FName*)Dest)[i] = ((FName*)Src)[i];
	unguardSlow;
}
UBOOL UNameProperty::Identical( const void* A, const void* B ) const
{
	guardSlow(UNameProperty::Identical);
	return *(FName*)A == (B ? *(FName*)B : FName(NAME_None));
	unguardobjSlow;
}
void UNameProperty::SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const
{
	guardSlow(UNameProperty::SerializeItem);
	Ar << *(FName*)Value;
	unguardobjSlow;
}
void UNameProperty::ExportCppItem( FOutputDevice& Out, UBOOL IsParam ) const
{
	guard(UNameProperty::ExportCppItem);
	Out.Log( TEXT("FName") );
	unguardobj;
}
void UNameProperty::ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const
{
	guard(UNameProperty::ExportTextItem);
	FName Temp = *(FName*)PropertyValue;
	if( !(PortFlags & PPF_Delimited) )
		appStrcpy( ValueStr, *Temp );
	else if ( HasValue(PropertyValue) ) // rjp
		appSprintf( ValueStr, TEXT("\"%s\""), *Temp );
	unguardobj;
}
const TCHAR* UNameProperty::ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const
{
	guard(UNameProperty::ImportText);
	TCHAR Temp[1024];
	//scion rwd - changed to allow import of names with '.'
	Buffer = ReadToken( Buffer, Temp, ARRAY_COUNT(Temp), 1 );
	if( !Buffer )
		return NULL;
	*(FName*)Data = FName(Temp);
	return Buffer;
	unguardobj;
}
UBOOL UNameProperty::HasValue( const BYTE* Data ) const
{
	guard(UNameProperty::HasValue);
	return *(FName*)Data != NAME_None;
	unguardobj;
}

void UNameProperty::ClearValue( BYTE* Data ) const
{
	guard(UNameProperty::ClearValue);
	*(FName*)Data = NAME_None;
	unguardobj;
}

IMPLEMENT_CLASS(UNameProperty);

/*-----------------------------------------------------------------------------
	UStrProperty.
-----------------------------------------------------------------------------*/

void UStrProperty::Link( FArchive& Ar, UProperty* Prev )
{
	guard(UStrProperty::Link);
	Super::Link( Ar, Prev );
	ElementSize    = sizeof(FString);
	Offset         = Align( GetOuterUField()->GetPropertiesSize(), PROPERTY_ALIGNMENT );
	if( !(PropertyFlags & CPF_Native) )
		PropertyFlags |= CPF_NeedCtorLink;
	unguardobj;
}
UBOOL UStrProperty::Identical( const void* A, const void* B ) const
{
	guardSlow(UStrProperty::Identical);
	return appStricmp( **(const FString*)A, B ? **(const FString*)B : TEXT("") )==0;
	unguardobjSlow;
}
void UStrProperty::SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const
{
	guardSlow(UStrProperty::SerializeItem);
	Ar << *(FString*)Value;
	unguardobjSlow;
}
void UStrProperty::Serialize( FArchive& Ar )
{
	guard(UStrProperty::Serialize);
	Super::Serialize( Ar );
	unguardobj;
}
void UStrProperty::ExportCppItem( FOutputDevice& Out, UBOOL IsParam ) const
{
	guard(UStrProperty::ExportCppItem);
	Out.Log( TEXT("FString") );
	unguardobj;
}
void UStrProperty::ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const
{
	guard(UStrProperty::ExportTextItem);
	/* rjp struct localization */
	*ValueStr=0;
	if( !(PortFlags & PPF_Delimited) )
		appStrcpy( ValueStr, **(FString*)PropertyValue );
	else if ( HasValue(PropertyValue) )  // rjp
	{
		appStrcpy(ValueStr, TEXT("\""));
		appStrcat(ValueStr, **(FString*)PropertyValue);
		appStrcat(ValueStr, TEXT("\""));
	
		// prevents strings longer than 1024 character from being exported completely
		// appSprintf( ValueStr, TEXT("\"%s\""), **(FString*)PropertyValue );
	}
	unguardobj;
}
const TCHAR* UStrProperty::ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const
{
	guard(UStrProperty::ImportText);
	if( !(PortFlags & PPF_Delimited) )
	{
		*(FString*)Data = Buffer;
	}
	else
	{
		TCHAR Temp[4096];//!!
		// MC: Use of ParseToken instead of ReadToken
		ParseToken( Buffer, Temp, ARRAY_COUNT(Temp), 1 );
		//			return NULL;
		if (Buffer == NULL)
		{
			return NULL;
		}
		*(FString*)Data = Temp;
	}
	return Buffer;
	unguardobj;
}
void UStrProperty::CopySingleValue( void* Dest, void* Src, UObject* SuperObject ) const
{
	guardSlow(UStrProperty::CopySingleValue);
	*(FString*)Dest = *(FString*)Src;
	unguardobjSlow;
}
void UStrProperty::DestroyValue( void* Dest ) const
{
	guardSlow(UStrProperty::DestroyValue);
	for( INT i=0; i<ArrayDim; i++ )
		(*(FString*)((BYTE*)Dest+i*ElementSize)).~FString();
	unguardobjSlow;
}
UBOOL UStrProperty::HasValue( const BYTE* Data ) const
{
	guard(UStrProperty::HasValue);
	return (*(FString*)Data).Len() > 0;
	unguardobj;
}

void UStrProperty::ClearValue( BYTE* Data ) const
{
	guard(UStrProperty::ClearValue);
	(*(FString*)Data).Empty();
	unguardobj;
}

IMPLEMENT_CLASS(UStrProperty);

/*-----------------------------------------------------------------------------
	UFixedArrayProperty.
-----------------------------------------------------------------------------*/

void UFixedArrayProperty::CleanupDestroyed( BYTE* Data ) const
{
	guard(UFixedArrayProperty::CleanupDestroyed);
	for(int i=0; i<Count; i++)
		Inner->CleanupDestroyed(Data+i*GetSize());
	unguardobj;
}

/**
 * Useful method for breaking links to objects that you want to delete
 *
 * @param Data Pointer to the object having refs cleaned up
 * @param Destroyed Pointer to the object that is being destroyed
 *
 * @return Whether this property contained a reference to the destroyed or not
 */
UBOOL UFixedArrayProperty::CleanupDestroyed(BYTE* Data, UObject* Destroyed) const
{
	guard(UFixedArrayProperty::CleanupDestroyed);
	// Let the caller know
	UBOOL bReferenced = 0;
	// Iterate through all array entries and clean those up
	for (INT Index = 0; Index < ArrayDim; Index++)
	{
		bReferenced |= Inner->CleanupDestroyed(Data + Index * GetSize(),Destroyed);
	}
	return bReferenced;
	unguardobj;
}

void UFixedArrayProperty::Link( FArchive& Ar, UProperty* Prev )
{
	guard(UFixedArrayProperty::Link);
	checkSlow(Count>0);
	Super::Link( Ar, Prev );
	Ar.Preload( Inner );
	Inner->Link( Ar, NULL );
	ElementSize    = Inner->ElementSize * Count;
	Offset         = Align( GetOuterUField()->GetPropertiesSize(), PROPERTY_ALIGNMENT );
	if( !(PropertyFlags & CPF_Native) )
		PropertyFlags |= (Inner->PropertyFlags & CPF_NeedCtorLink);
	unguardobj;
}
UBOOL UFixedArrayProperty::Identical( const void* A, const void* B ) const
{
	guardSlow(UFixedArrayProperty::Identical);
	checkSlow(Inner);
	for( INT i=0; i<Count; i++ )
		if( !Inner->Identical( (BYTE*)A+i*Inner->ElementSize, B ? ((BYTE*)B+i*Inner->ElementSize) : NULL ) )
			return 0;
	return 1;
	unguardobjSlow;
}
void UFixedArrayProperty::SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const
{
	guardSlow(UFixedArrayProperty::SerializeItem);
	checkSlow(Inner);
	for( INT i=0; i<Count; i++ )
		Inner->SerializeItem( Ar, (BYTE*)Value + i*Inner->ElementSize, MaxReadBytes>0?MaxReadBytes/Count:0 );
	unguardobjSlow;
}
UBOOL UFixedArrayProperty::NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const
{
	guardSlow(UByteProperty::NetSerializeItem);
	return 1;
	unguardobjSlow;
}
void UFixedArrayProperty::Serialize( FArchive& Ar )
{
	guard(UFixedArrayProperty::Serialize);
	Super::Serialize( Ar );
	Ar << Inner << Count;
	checkSlow(Inner);
	unguardobj;
}
void UFixedArrayProperty::ExportCppItem( FOutputDevice& Out, UBOOL IsParam ) const
{
	guard(UFixedArrayProperty::ExportCppItem);
	checkSlow(Inner);
	Inner->ExportCppItem( Out );
	Out.Logf( TEXT("[%i]"), Count );
	unguardobj;
}
void UFixedArrayProperty::ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const
{
	guard(UFixedArrayProperty::ExportTextItem);
	checkSlow(Inner);
	*ValueStr++ = '(';
	for( INT i=0; i<Count; i++ )
	{
		if( i>0 )
			*ValueStr++ = ',';
		Inner->ExportTextItem( ValueStr, PropertyValue + i*Inner->ElementSize, DefaultValue ? (DefaultValue + i*Inner->ElementSize) : NULL, PortFlags|PPF_Delimited );
		ValueStr += appStrlen(ValueStr);
	}
	*ValueStr++ = ')';
	*ValueStr++ = 0;
	unguardobj;
}
const TCHAR* UFixedArrayProperty::ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const
{
	guard(UFixedArrayProperty::ImportText);
	checkSlow(Inner);
	if( *Buffer++ != '(' )
		return NULL;
	appMemzero( Data, ElementSize );
	for( INT i=0; i<Count; i++ )
	{
		Buffer = Inner->ImportText( Buffer, Data + i*Inner->ElementSize, PortFlags|PPF_Delimited );
		if( !Buffer )
			return NULL;
		if( *Buffer!=',' && i!=Count-1 )
			return NULL;
		Buffer++;
	}
	if( *Buffer++ != ')' )
		return NULL;
	return Buffer;
	unguardobj;
}
void UFixedArrayProperty::AddCppProperty( UProperty* Property, INT InCount )
{
	guard(UFixedArrayProperty::AddCppProperty);
	check(!Inner);
	check(Property);
	check(InCount>0);

	Inner = Property;
	Count = InCount;

	unguardobj;
}
void UFixedArrayProperty::CopySingleValue( void* Dest, void* Src, UObject* SuperObject ) const
{
	guardSlow(UFixedArrayProperty::CopySingleValue);
	for( INT i=0; i<Count; i++ )
		Inner->CopyCompleteValue( (BYTE*)Dest + i*Inner->ElementSize, Src ? ((BYTE*)Src + i*Inner->ElementSize) : NULL );
	unguardobjSlow;
}
void UFixedArrayProperty::DestroyValue( void* Dest ) const
{
	guardSlow(UFixedArrayProperty::DestroyValue);
	for( INT i=0; i<Count; i++ )
		Inner->DestroyValue( (BYTE*)Dest + i*Inner->ElementSize );
	unguardobjSlow;
}
UBOOL UFixedArrayProperty::HasValue( const BYTE* Data ) const
{
	return 0;
}

void UFixedArrayProperty::ClearValue( BYTE* Data ) const
{
}

IMPLEMENT_CLASS(UFixedArrayProperty);

/*-----------------------------------------------------------------------------
	UArrayProperty.
-----------------------------------------------------------------------------*/

void UArrayProperty::CleanupDestroyed( BYTE* Data ) const
{
	guard(UArrayProperty::CleanupDestroyed);
	for(int i=0; i<ArrayDim; i++)
	{
		FArray* A=(FArray*)( Data + i*ElementSize );
		for(int j=0; j<A->Num(); j++)
			Inner->CleanupDestroyed((BYTE*)A->GetData() + j*Inner->GetSize());
	}
	unguardobj;
}

/**
 * Useful method for breaking links to objects that you want to delete
 *
 * @param Data Pointer to the object having refs cleaned up
 * @param Destroyed Pointer to the object that is being destroyed
 *
 * @return Whether this property contained a reference to the destroyed or not
 */
UBOOL UArrayProperty::CleanupDestroyed(BYTE* Data, UObject* Destroyed) const
{
	guard(UArrayProperty::CleanupDestroyed);
	// Let the caller know
	UBOOL bReferenced = 0;
	// Iterate through all array entries and clean those up
	for (INT Index = 0; Index < ArrayDim; Index++)
	{
		FArray* A = (FArray*)(Data + Index * ElementSize);
		for (INT j = 0; j < A->Num(); j++)
		{
			bReferenced |= Inner->CleanupDestroyed((BYTE*)A->GetData() + j * Inner->GetSize(),Destroyed);
		}
	}
	return bReferenced;
	unguardobj;
}

void UArrayProperty::Link( FArchive& Ar, UProperty* Prev )
{
	guard(UArrayProperty::Link);
	Super::Link( Ar, Prev );
	Ar.Preload( Inner );
	Inner->Link( Ar, NULL );
	ElementSize    = sizeof(FArray);
	Offset         = Align( GetOuterUField()->GetPropertiesSize(), PROPERTY_ALIGNMENT );
	if( !(PropertyFlags & CPF_Native) )
		PropertyFlags |= CPF_NeedCtorLink;
	unguardobj;
}
UBOOL UArrayProperty::Identical( const void* A, const void* B ) const
{
	guardSlow(UArrayProperty::Identical);
	checkSlow(Inner);
	INT n = ((FArray*)A)->Num();
	if( n!=(B ? ((FArray*)B)->Num() : 0) )
		return 0;
	INT   c = Inner->ElementSize;
	BYTE* p = (BYTE*)((FArray*)A)->GetData();
	if( B )
	{
		BYTE* q = (BYTE*)((FArray*)B)->GetData();
		for( INT i=0; i<n; i++ )
			if( !Inner->Identical( p+i*c, q+i*c ) )
				return 0;
	}
	else
	{
		for( INT i=0; i<n; i++ )
			if( !Inner->Identical( p+i*c, 0 ) )
				return 0;
	}
	return 1;
	unguardobjSlow;
}
void UArrayProperty::SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const
{
	guardSlow(UArrayProperty::SerializeItem);
	checkSlow(Inner);
	INT   c = Inner->ElementSize;
	INT   n = ((FArray*)Value)->Num();
	Ar << AR_INDEX(n);
	if( Ar.IsLoading() )
	{
		//epic moelfke: need to use DestroyValue() if Inner needs destruction to prevent memory leak
		if (Inner->PropertyFlags & CPF_NeedCtorLink)
		{
			DestroyValue(Value);
		}
		else
		{
			((FArray*)Value)->Empty(c);
		}
		((FArray*)Value)->AddZeroed( c, n );
	}
	BYTE* p = (BYTE*)((FArray*)Value)->GetData();
	for( INT i=0; i<n; i++ )
		Inner->SerializeItem( Ar, p+i*c, MaxReadBytes>0?MaxReadBytes/n:0 );
	unguardobjSlow;
}
UBOOL UArrayProperty::NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const
{
	guardSlow(UByteProperty::NetSerializeItem);
	return 1;
	unguardobjSlow;
}
void UArrayProperty::Serialize( FArchive& Ar )
{
	guard(UArrayProperty::Serialize);
	Super::Serialize( Ar );
	Ar << Inner;
	checkSlow(Inner);
	unguardobj;
}
void UArrayProperty::ExportCppItem( FOutputDevice& Out, UBOOL IsParam ) const
{
	guard(UArrayProperty::ExportCppItem);
	checkSlow(Inner);
	if( IsParam )
		Out.Log( TEXT("TArray<") );
	else
		Out.Log( TEXT("TArrayNoInit<") );
	Inner->ExportCppItem( Out );
	Out.Log( TEXT(">") );
	unguardobj;
}
void UArrayProperty::ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const
{
	guard(UArrayProperty::ExportTextItem);
	checkSlow(Inner);

	FArray* Array       = (FArray*)PropertyValue;
	FArray* Default     = (FArray*)DefaultValue;
	INT     ElementSize = Inner->ElementSize;

/* rjp struct localization
	*ValueStr++ = '(';
	for( INT i=0; i<Array->Num(); i++ )
	{
		if( i>0 )
			*ValueStr++ = ',';
		Inner->ExportTextItem( ValueStr, (BYTE*)Array->GetData() + i*ElementSize, (Default && Default->Num()>=i) ? (BYTE*)Default->GetData() + i*ElementSize : 0, PortFlags|PPF_Delimited );
		ValueStr += appStrlen(ValueStr);
	}
	*ValueStr++ = ')';
	*ValueStr++ = 0;
*/
	INT Count(0);
	for( INT i=0; i<Array->Num(); i++ )
	{
		if( ++Count == 1 )
			*ValueStr++ = '(';
		else
			*ValueStr++ = ',';

		BYTE* PropData = (BYTE*)Array->GetData() + i*ElementSize;
		BYTE* PropDefault = (Default && Default->Num()>i) ? (BYTE*)Default->GetData() + i*ElementSize : NULL;

		// Do not re-export duplicate data from superclass when exporting to .int file
//		if ( (PortFlags & PPF_LocalizedOnly) && Inner->Identical(PropData, PropDefault) )
//			continue;

		Inner->ExportTextItem( ValueStr, PropData, PropDefault, PortFlags|PPF_Delimited );
		ValueStr += appStrlen(ValueStr);
	}
	if( Count > 0 )
		*ValueStr++ = ')';

	*ValueStr = 0;
	unguardobj;
}
const TCHAR* UArrayProperty::ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const
{
	guard(UArrayProperty::ImportText);
	checkSlow(Inner);
	if( *Buffer++ != '(' )
		return NULL;
	FArray* Array       = (FArray*)Data;
	INT     ElementSize = Inner->ElementSize;

	/* rjp struct localization
	only clear the array if we're not importing localized text
	*/
	if ( !(PortFlags & PPF_LocalizedOnly) )
	{
		//epic moelfke: need to use DestroyValue() if Inner needs destruction to prevent memory leak
		if (Inner->PropertyFlags & CPF_NeedCtorLink)
		{
			DestroyValue(Array);
		}
		else
		{
			Array->Empty(ElementSize);
		}
	}

	INT Index(0);
	while( *Buffer != ')' )
	{
/* rjp struct localization
		INT Index = Array->Add( 1, ElementSize );
		appMemzero( (BYTE*)Array->GetData() + Index*ElementSize, ElementSize );
*/
	    // advance past empty elements -- rjp
	    while ( *Buffer == ',' )
		{
			Buffer++;
		    if ( Index >= Array->Num() )
		    {
			    Array->Add( 1, ElementSize );
			    appMemzero( (BYTE*)Array->GetData() + Index*ElementSize, ElementSize );
		    }
			Index++;

			if ( *Buffer == ')' )
			{
				// Remove any additional elements from this array
				if ( (PortFlags && PPF_LocalizedOnly) && Index < Array->Num() )
					Array->Remove( Index, Array->Num() - Index, ElementSize );

				Buffer++;
				return Buffer;
			}
		}

		if ( Index >= Array->Num() )
		{
			Array->Add( 1, ElementSize );
			appMemzero( (BYTE*)Array->GetData() + Index*ElementSize, ElementSize );
		}

		Buffer = Inner->ImportText( Buffer, (BYTE*)Array->GetData() + Index*ElementSize, PortFlags|PPF_Delimited );
		Index++;
		if( !Buffer )
			return NULL;
		if( *Buffer!=',' )
			break;
		Buffer++;
	}
	if( *Buffer++ != ')' )
		return NULL;

	// rjp struct localization
	// Remove any additional elements from this array
	if ( (PortFlags && PPF_LocalizedOnly) && Index < Array->Num() )
		Array->Remove( Index, Array->Num() - Index, ElementSize );

	return Buffer;
	unguardobj;
}
void UArrayProperty::AddCppProperty( UProperty* Property )
{
	guard(UArrayProperty::AddCppProperty);
	check(!Inner);
	check(Property);

	Inner = Property;

	unguardobj;
}
void UArrayProperty::CopyCompleteValue( void* Dest, void* Src, UObject* SuperObject ) const
{
	guardSlow(UArrayProperty::CopyCompleteValue);
	//epic moelfke: don't do anything if the pointers are the same (no point, and otherwise they'll incorrectly get emptied)
	if (Src != Dest)
	{
		FArray* SrcArray = (FArray*)Src;
		FArray* DestArray = (FArray*)Dest;
		INT Size = Inner->ElementSize;
		//epic moelfke: (from UT2004) need to use DestroyValue() if Inner needs destruction to prevent memory leak
		if (Inner->PropertyFlags & CPF_NeedCtorLink)
		{
			DestroyValue(Dest);
		}
		else
		{
			DestArray->Empty(Size, SrcArray->Num());
		}
		//epic moelfke: (from UT2004) no point in doing anything further if SrcArray is empty
		if (SrcArray->Num() > 0)
		{
			if (Inner->PropertyFlags & CPF_NeedCtorLink)
			{
				// Copy all the elements.
				DestArray->AddZeroed( Size, SrcArray->Num() );
				BYTE* SrcData  = (BYTE*)SrcArray->GetData();
				BYTE* DestData = (BYTE*)DestArray->GetData();
				for (INT i = 0; i < DestArray->Num(); i++)
				{
					Inner->CopyCompleteValue(DestData + i * Size, SrcData + i * Size, SuperObject);
				}
			}
			else
			{
				// Copy all the elements.
				DestArray->Add(SrcArray->Num(), Size);
				appMemcpy(DestArray->GetData(), SrcArray->GetData(), SrcArray->Num() * Size);
			}
		}
	}
	unguardobjSlow;
}
void UArrayProperty::ClearValue( BYTE* Data ) const
{
	guard(UArrayProperty::ClearValue);

	//epic moelfke: need to use DestroyValue() if Inner needs destruction to prevent memory leak
	if (Inner->PropertyFlags & CPF_NeedCtorLink)
	{
		DestroyValue(Data);
	}
	else
	{
		(*(FArray*)Data).Empty(ElementSize);
	}
	
	unguardobj;
}
void UArrayProperty::DestroyValue( void* Dest ) const
{
	guardSlow(UArrayProperty::DestroyValue);
	//!! fix for ucc make crash
	if( (Offset == 0) && GetOuter()->IsA(UClass::StaticClass()) )
 		return;
	FArray* DestArray = (FArray*)Dest;
	if( Inner->PropertyFlags & CPF_NeedCtorLink )
	{
		BYTE* DestData = (BYTE*)DestArray->GetData();
		INT   Size     = Inner->ElementSize;
		for( INT i=0; i<DestArray->Num(); i++ )
			Inner->DestroyValue( DestData+i*Size );
	}
	DestArray->~FArray();
	unguardobjSlow;
}
UBOOL UArrayProperty::HasValue( const BYTE* Data ) const
{
	guard(UArrayProperty::HasValue);
	return (*(FArray*)Data).Num() != 0;
	unguard;
}
/* rjp struct localization */
UBOOL UArrayProperty::IsLocalized() const
{
	guard(UArrayProperty::IsLocalized);
	if ( Inner->IsLocalized() )
		return 1;
	return Super::IsLocalized();
	unguardobj;
}

IMPLEMENT_CLASS(UArrayProperty);

/*-----------------------------------------------------------------------------
	UMapProperty.
-----------------------------------------------------------------------------*/

void UMapProperty::Link( FArchive& Ar, UProperty* Prev )
{
	guard(UMapProperty::Link);
	Super::Link( Ar, Prev );
	Ar.Preload( Key );
	Key->Link( Ar, NULL );
	Ar.Preload( Value );
	Value->Link( Ar, NULL );
	ElementSize    = sizeof(TMap<BYTE,BYTE>);
	Offset         = Align( GetOuterUField()->GetPropertiesSize(), PROPERTY_ALIGNMENT );
	if( !(PropertyFlags&CPF_Native) )
		PropertyFlags |= CPF_NeedCtorLink;
	unguardobj;
}
UBOOL UMapProperty::Identical( const void* A, const void* B ) const
{
	guardSlow(UMapProperty::Identical);
	checkSlow(Key);
	checkSlow(Value);
	/*
	INT n = ((FArray*)A)->Num();
	if( n!=(B ? ((FArray*)B)->Num() : 0) )
		return 0;
	INT   c = Inner->ElementSize;
	BYTE* p = (BYTE*)((FArray*)A)->GetData();
	if( B )
	{
		BYTE* q = (BYTE*)((FArray*)B)->GetData();
		for( INT i=0; i<n; i++ )
			if( !Inner->Identical( p+i*c, q+i*c ) )
				return 0;
	}
	else
	{
		for( INT i=0; i<n; i++ )
			if( !Inner->Identical( p+i*c, 0 ) )
				return 0;
	}
	*/
	return 1;
	unguardobjSlow;
}
void UMapProperty::SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const
{
	guardSlow(UMapProperty::SerializeItem);
	checkSlow(Key);
	checkSlow(Value);
	/*
	INT   c = Inner->ElementSize;
	INT   n = ((FArray*)Value)->Num();
	Ar << AR_INDEX(n);
	if( Ar.IsLoading() )
	{
		((FArray*)Value)->Empty( c );
		((FArray*)Value)->Add( n, c );
	}
	BYTE* p = (BYTE*)((FArray*)Value)->GetData();
	for( INT i=0; i<n; i++ )
		Inner->SerializeItem( Ar, p+i*c );
	*/
	unguardobjSlow;
}
UBOOL UMapProperty::NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const
{
	guardSlow(UByteProperty::NetSerializeItem);
	return 1;
	unguardobjSlow;
}
void UMapProperty::Serialize( FArchive& Ar )
{
	guard(UMapProperty::Serialize);
	Super::Serialize( Ar );
	Ar << Key << Value;
	checkSlow(Key);
	checkSlow(Value);
	unguardobj;
}
void UMapProperty::ExportCppItem( FOutputDevice& Out, UBOOL IsParam ) const
{
	guard(UMapProperty::ExportCppItem);
	checkSlow(Key);
	checkSlow(Value);
	Out.Log( TEXT("TMap<") );
	Key->ExportCppItem( Out );
	Out.Log( TEXT(",") );
	Value->ExportCppItem( Out );
	Out.Log( TEXT(">") );
	unguardobj;
}
void UMapProperty::ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const
{
	guard(UMapProperty::ExportTextItem);
	checkSlow(Key);
	checkSlow(Value);
	/*
	*ValueStr++ = '(';
	FArray* Array       = (FArray*)PropertyValue;
	FArray* Default     = (FArray*)DefaultValue;
	INT     ElementSize = Inner->ElementSize;
	for( INT i=0; i<Array->Num(); i++ )
	{
		if( i>0 )
			*ValueStr++ = ',';
		Inner->ExportTextItem( ValueStr, (BYTE*)Array->GetData() + i*ElementSize, Default ? (BYTE*)Default->GetData() + i*ElementSize : 0, PortFlags|PPF_Delimited );
		ValueStr += appStrlen(ValueStr);
	}
	*ValueStr++ = ')';
	*ValueStr++ = 0;
	*/
	unguardobj;
}
const TCHAR* UMapProperty::ImportText( const TCHAR* Buffer, BYTE* Data, INT PortFlags ) const
{
	guard(UMapProperty::ImportText);
	checkSlow(Key);
	checkSlow(Value);
	/*
	if( *Buffer++ != '(' )
		return NULL;
	FArray* Array       = (FArray*)Data;
	INT     ElementSize = Inner->ElementSize;
	Array->Empty( ElementSize );
	while( *Buffer != ')' )
	{
		INT Index = Array->Add( 1, ElementSize );
		appMemzero( (BYTE*)Array->GetData() + Index*ElementSize, ElementSize );
		Buffer = Inner->ImportText( Buffer, (BYTE*)Array->GetData() + Index*ElementSize, PortFlags|PPF_Delimited );
		if( !Buffer )
			return NULL;
		if( *Buffer!=',' )
			break;
		Buffer++;
	}
	if( *Buffer++ != ')' )
		return NULL;
	*/
	return Buffer;
	unguardobj;
}
void UMapProperty::CopySingleValue( void* Dest, void* Src, UObject* SuperObject ) const
{
	guardSlow(UMapProperty::CopySingleValue);
	/*
	TMap<BYTE,BYTE>* SrcMap    = (TMap<BYTE,BYTE>*)Src;
	TMap<BYTE,BYTE>* DestMap   = (TMap<BYTE,BYTE>*)Dest;
	INT              KeySize   = Key->ElementSize;
	INT              ValueSize = Value->ElementSize;
	DestMap->Empty( Size, SrcArray->Num() );//must destruct it if really copying
	if( Inner->PropertyFlags & CPF_NeedsCtorLink )
	{
		// Copy all the elements.
		DestArray->AddZeroed( Size, SrcArray->Num() );
		BYTE* SrcData  = (BYTE*)SrcArray->GetData();
		BYTE* DestData = (BYTE*)DestArray->GetData();
		for( INT i=0; i<DestArray->Num(); i++ )
			Inner->CopyCompleteValue( DestData+i*Size, SrcData+i*Size );
	}
	else
	{
		// Copy all the elements.
		DestArray->Add( SrcArray->Num(), Size );
		appMemcpy( DestArray->GetData(), SrcArray->GetData(), SrcArray->Num()*Size );
	}*/
	unguardobjSlow;
}
void UMapProperty::DestroyValue( void* Dest ) const
{
	guardSlow(UMapProperty::DestroyValue);
	/*
	FArray* DestArray = (FArray*)Dest;
	if( Inner->PropertyFlags & CPF_NeedsCtorLink )
	{
		BYTE* DestData = (BYTE*)DestArray->GetData();
		INT   Size     = Inner->ElementSize;
		for( INT i=0; i<DestArray->Num(); i++ )
			Inner->DestroyValue( DestData+i*Size );
	}
	DestArray->~FArray();
	*/
	unguardobjSlow;
}
UBOOL UMapProperty::HasValue( const BYTE* Data ) const
{
	return 0;
}
IMPLEMENT_CLASS(UMapProperty);

/*-----------------------------------------------------------------------------
	UStructProperty.
-----------------------------------------------------------------------------*/

void UStructProperty::CleanupDestroyed( BYTE* Data ) const
{
	guard(UStructProperty::CleanupDestroyed);
	for(int i=0; i<ArrayDim; i++)
		Struct->CleanupDestroyed(Data+i*ElementSize);
	unguardobj;
}

/**
 * Useful method for breaking links to objects that you want to delete
 *
 * @param Data Pointer to the object having refs cleaned up
 * @param Destroyed Pointer to the object that is being destroyed
 *
 * @return Whether this property contained a reference to the destroyed or not
 */
UBOOL UStructProperty::CleanupDestroyed(BYTE* Data, UObject* Destroyed) const
{
	guard(UStructProperty::CleanupDestroyed);
	// Let the caller know
	UBOOL bReferenced = 0;
	// Iterate through all array entries and clean those up
	for (INT Index = 0; Index < ArrayDim; Index++)
	{
		bReferenced |= Struct->CleanupDestroyed(Data + Index * ElementSize,Destroyed);
	}
	return bReferenced;
	unguardobj;
}

void UStructProperty::Link( FArchive& Ar, UProperty* Prev )
{
	guard(UStructProperty::Link);
	Super::Link( Ar, Prev );
	Ar.Preload( Struct );
	ElementSize    = Struct->PropertiesSize;
	Offset         = Align( GetOuterUField()->GetPropertiesSize(), ElementSize==2 ? 2 : ElementSize>=4 ? 4 : 1 );
	if( Struct->ConstructorLink && !(PropertyFlags & CPF_Native) )
		PropertyFlags |= CPF_NeedCtorLink;
	unguardobj;
}
UBOOL UStructProperty::Identical( const void* A, const void* B ) const
{
	guardSlow(UStructProperty::Identical);
	for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(Struct); It; ++It )
		for( INT i=0; i<It->ArrayDim; i++ )
			if( !It->Matches(A,B,i) )
				return 0;
	return 1;
	unguardobjSlow;
}
void UStructProperty::SerializeItem( FArchive& Ar, void* Value, INT MaxReadBytes ) const
{
	guardSlow(UStructProperty::SerializeItem);
	if( !(Ar.IsLoading() || Ar.IsSaving()) ||
		Ar.Ver() < 118 || Ar.IsRawUData() ||
		Struct->GetFName()==NAME_Vector ||
		Struct->GetFName()==NAME_Rotator ||
		Struct->GetFName()==NAME_Color )
	{
		Ar.Preload( Struct );
		Struct->SerializeBin( Ar, (BYTE*)Value, MaxReadBytes );
	}
	else
	{
		Struct->SerializeTaggedProperties( Ar, (BYTE*)Value, NULL );
	}
	unguardobjSlow;
}
UBOOL UStructProperty::NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data ) const
{
	guardSlow(UStructProperty::NetSerializeItem);
	if( Struct->GetFName()==NAME_Vector )
	{
		FVector& V = *(FVector*)Data;
		INT X(appRound(V.X)), Y(appRound(V.Y)), Z(appRound(V.Z));
		DWORD Bits = Clamp<DWORD>( appCeilLogTwo(1+Max(Max(Abs(X),Abs(Y)),Abs(Z))), 1, 20 )-1;
		Ar.SerializeInt( Bits, 20 );
		INT   Bias = 1<<(Bits+1);
		DWORD Max  = 1<<(Bits+2);
		DWORD DX(X+Bias), DY(Y+Bias), DZ(Z+Bias);
		Ar.SerializeInt( DX, Max );
		Ar.SerializeInt( DY, Max );
		Ar.SerializeInt( DZ, Max );
		if( Ar.IsLoading() )
			V = FVector((INT)DX-Bias,(INT)DY-Bias,(INT)DZ-Bias);
	}
	else if( Struct->GetFName()==NAME_Rotator )
	{
		FRotator& R = *(FRotator*)Data;
		BYTE Pitch(R.Pitch>>8), Yaw(R.Yaw>>8), Roll(R.Roll>>8), B;
		B = (Pitch!=0);
		Ar.SerializeBits( &B, 1 );
		if( B )
			Ar << Pitch;
		else
			Pitch = 0;
		B = (Yaw!=0);
		Ar.SerializeBits( &B, 1 );
		if( B )
			Ar << Yaw;
		else
			Yaw = 0;
		B = (Roll!=0);
		Ar.SerializeBits( &B, 1 );
		if( B )
			Ar << Roll;
		else
			Roll = 0;
		if( Ar.IsLoading() )
			R = FRotator(Pitch<<8,Yaw<<8,Roll<<8);
	}
	else if( Struct->GetFName()==NAME_Plane )
	{
		FPlane& P = *(FPlane*)Data;
		SWORD X(appRound(P.X)), Y(appRound(P.Y)), Z(appRound(P.Z)), W(appRound(P.W));
		Ar << X << Y << Z << W;
		if( Ar.IsLoading() )
			P = FPlane(X,Y,Z,W);
	}
	else if ( Struct->GetFName()==NAME_CompressedPosition )
	{
		FCompressedPosition& C = *(FCompressedPosition*)Data;

		INT VX = appRound(C.Velocity.X);
		INT VY = appRound(C.Velocity.Y);
		INT VZ = appRound(C.Velocity.Z);
		DWORD VelBits = Clamp<DWORD>( appCeilLogTwo(1+Max(Max(Abs(VX),Abs(VY)),Abs(VZ))), 1, 20 )-1;

		// Location
		INT X(appRound(C.Location.X)), Y(appRound(C.Location.Y)), Z(appRound(C.Location.Z));
		DWORD Bits = Clamp<DWORD>( appCeilLogTwo(1+Max(Max(Abs(X),Abs(Y)),Abs(Z))), 1, 20 )-1;
		Ar.SerializeInt( Bits, 20 );
		INT   Bias = 1<<(Bits+1);
		DWORD Max  = 1<<(Bits+2);
		DWORD DX(X+Bias), DY(Y+Bias), DZ(Z+Bias);
		Ar.SerializeInt( DX, Max );
		Ar.SerializeInt( DY, Max );
		Ar.SerializeInt( DZ, Max );
		if( Ar.IsLoading() )
			C.Location = FVector((INT)DX-Bias,(INT)DY-Bias,(INT)DZ-Bias);

		// Rotation
		BYTE Pitch(C.Rotation.Pitch>>8), Yaw(C.Rotation.Yaw>>8);
		Ar << Pitch;
		Ar << Yaw;
		if( Ar.IsLoading() )
			C.Rotation = FRotator(Pitch<<8,Yaw<<8,0);
		
		// Velocity
		Ar.SerializeInt( VelBits, 20 );
		Bias = 1<<(VelBits+1);
		Max  = 1<<(VelBits+2);
		DX = VX+Bias;
		DY = VY+Bias;
		DZ = VZ+Bias;
		Ar.SerializeInt( DX, Max );
		Ar.SerializeInt( DY, Max );
		Ar.SerializeInt( DZ, Max );
		if( Ar.IsLoading() )
			C.Velocity = FVector((INT)DX-Bias,(INT)DY-Bias,(INT)DZ-Bias);
	}
	else
	{
		for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(Struct); It; ++It )
			if( Map->ObjectToIndex(*It)!=INDEX_NONE )
				for( INT i=0; i<It->ArrayDim; i++ )
					It->NetSerializeItem( Ar, Map, (BYTE*)Data+It->Offset+i*It->ElementSize );
	}
	return 1;
	unguardobjSlow;
}
void UStructProperty::Serialize( FArchive& Ar )
{
	guard(UStructProperty::Serialize);
	Super::Serialize( Ar );
	Ar << Struct;
	unguardobj;
}
void UStructProperty::ExportCppItem( FOutputDevice& Out, UBOOL IsParam ) const
{
	guard(UStructProperty::ExportCppItem);
	Out.Logf( TEXT("%s"), Struct->GetNameCPP() );
	unguardobj;
}
void UStructProperty::ExportTextItem( TCHAR* ValueStr, BYTE* PropertyValue, BYTE* DefaultValue, INT PortFlags ) const
{
	guard(UStructProperty::ExportTextItem);
#ifdef __GCN__
	TCHAR* Value = (TCHAR*)appMalloc(65536, TEXT("ExportTextItem"));
#endif
	INT Count=0;
	for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(Struct); It; ++It )
	{
		if( It->Port() )
		{
			for( INT Index=0; Index<It->ArrayDim; Index++ )
			{
#ifndef __GCN__
				TCHAR Value[65536];
#endif
				if( It->ExportText(Index,Value,PropertyValue,DefaultValue,PPF_Delimited|PortFlags) )
				{
					Count++;
					if( Count == 1 )
						*ValueStr++ = '(';
					else
						*ValueStr++ = ',';
					if( It->ArrayDim == 1 )
						ValueStr += appSprintf( ValueStr, TEXT("%s="), It->GetName() );
					else
						ValueStr += appSprintf( ValueStr, TEXT("%s[%i]="), It->GetName(), Index );
					ValueStr += appSprintf( ValueStr, TEXT("%s"), Value );
				}
			}
		}
	}
/* rjp struct localization
	always terminate the ValueStr, even when we didn't export anything
*/
	if( Count > 0 )
		*ValueStr++ = ')';
	*ValueStr = 0;

#ifdef __GCN__
	appFree(Value);
#endif
	unguardobj;
}
const TCHAR* UStructProperty::ImportText( const TCHAR* InBuffer, BYTE* Data, INT PortFlags ) const
{
    const TCHAR* Buffer = InBuffer;

	guard(UStructProperty::ImportText);
	if( *Buffer++ == '(' )
	{
		// Parse all properties.
		while( *Buffer != ')' )
		{
			// Get key name.
			TCHAR Name[NAME_SIZE];
			int Count=0;
			while( Count<NAME_SIZE-1 && *Buffer && *Buffer!='=' && *Buffer!='[' )
				Name[Count++] = *Buffer++;
			Name[Count++] = 0;

			// Get optional array element.
			INT Element = -1;
			if( *Buffer=='[' )
			{
				const TCHAR* Start=++Buffer;

				if ( appIsDigit(*Buffer) )
				{
					while( *Buffer>='0' && *Buffer<='9' )
						Buffer++;
					if( *Buffer++ != ']' )
					{
						warnf( NAME_Error, TEXT("ImportText: Illegal array element in: %s"), InBuffer );
						return NULL;
					}
					Element = appAtoi( Start );
				}
				else
				{
					// Resolve const reference in default properties subscript within structs
					TCHAR TempToken[NAME_SIZE], *Pos = TempToken;
					while( *Buffer && *Buffer!=']' )
					{
						if ( Pos - Start >= NAME_SIZE )
						{
							warnf(NAME_Error, TEXT("ImportText: Array subscript const name too large for '%s' in: %s"), Name, InBuffer);
							return NULL;
						}
						if ( *Buffer == ')' || *Buffer == ',' )
						{
							warnf( NAME_Error, TEXT("ImportText: Missing closing ']' for '%s' in: %s"), Name, InBuffer );
							return NULL;
						}

						*Pos++ = *Buffer++;
					}

					*Pos=0;

					UConst* Const = FindField<UConst>( GetOwnerClass(), TempToken );
					if ( Const == NULL )
					{
						warnf(NAME_ExecWarning, TEXT("Invalid subscript in defaultproperties for '%s' in: %s"), Name, InBuffer);
						continue;
					}

					Element = appAtoi(*(Const->Value));
					Buffer++;
				}
			}

			// Verify format.
			if( *Buffer++ != '=' )
			{
				warnf( NAME_Error, TEXT("%s::ImportText: Illegal or missing key name in: %s"), GetName(), InBuffer ); // gam
				return NULL;
			}

			// See if the property exists in the struct.
			FName GotName( Name, FNAME_Find );
			UBOOL Parsed = 0;
			if( GotName!=NAME_None )
			{
				for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(Struct); It; ++It )
				{
					UProperty* Property = *It;

					if(	Property->GetFName()!=GotName ||
						Property->GetSize()==0 ||
						!Property->Port() )
                        continue;

					//epic moelfke: don't complain about index for dynamic arrays
					if (Element >= Property->ArrayDim && !Property->IsA(UArrayProperty::StaticClass()))
					{
						warnf( NAME_Error, TEXT("%s::ImportText: Array index out of bounds in: %s"), GetName(), InBuffer );
				        return NULL;
                    }

					/*  rjp struct localization
					Do not attempt to import this property if there is no value for it - i.e. (Prop1=,Prop2=)
					This normally only happens for empty strings or empty dynamic arrays, and the alternative
					is for strings and dynamic arrays to always export blank delimiters, such as Array=() or String="", 
					but this tends to cause problems with inherited property values being overwritten, especially in the localization 
					import/export code, or when a property has both CPF_Localized & CPF_Config flags

					The safest way is to interpret blank delimiters as an indication that the current value should be overwritten with an empty
					value, while the lack of any value or delimiter as an indication to not import this property, thereby preventing any current
					values from being overwritten if this is not the intent.

					Thus, arrays and strings will only export empty delimiters when overriding an inherited property's value with an 
					empty value.

				    -- rjp */

					if ( *Buffer != ',' && *Buffer != ')' )
					{
						//epic moelfke: copied from ImportProperties() to support setting single dynamic array elements
						if (Element > -1 && Property->IsA(UArrayProperty::StaticClass()))
						{
							FArray* Array = (FArray*)(Data + Property->Offset);
							UArrayProperty* ArrayProp = (UArrayProperty*)Property;
							if (Element >= Array->Num())
							{
								Array->AddZeroed(ArrayProp->Inner->ElementSize, Element - Array->Num() + 1);
							}
							Buffer = ArrayProp->Inner->ImportText(Buffer, (BYTE*)Array->GetData() + Element * ArrayProp->Inner->ElementSize, PPF_Delimited | PPF_CheckReferences);
						}
						else
						{
							if (Element == -1)
							{
								Element = 0;
							}
							Buffer = Property->ImportText(Buffer, Data + Property->Offset + Element * Property->ElementSize, PortFlags | PPF_Delimited);
						}
					}

					if( Buffer == NULL )
                    {
						warnf( NAME_Error, TEXT("%s::ImportText failed in: %s"), GetName(), InBuffer );
						return NULL;
                    }

					Parsed = 1;
                    break;
				}
			}

			// If not parsed, skip this property in the stream.
			if( !Parsed )
			{
			    warnf( NAME_Error, TEXT("Unknown member %s in %s"), Name, GetName() );

				INT SubCount=0;
				while
				(	*Buffer
				&&	*Buffer!=10
				&&	*Buffer!=13
				&&	(SubCount>0 || *Buffer!=')')
				&&	(SubCount>0 || *Buffer!=',') )
				{
					if( *Buffer == 0x22 )
					{
						while( *Buffer && *Buffer!=0x22 && *Buffer!=10 && *Buffer!=13 )
							Buffer++;
						if( *Buffer != 0x22 )
						{
							warnf( NAME_Error, TEXT("ImportText: Bad quoted string in: %s %s"), GetName(), InBuffer );
							return NULL;
						}
					}
					else if( *Buffer == '(' )
					{
						SubCount++;
					}
					else if( *Buffer == ')' )
					{
						SubCount--;
						if( SubCount < 0 )
						{
							warnf( NAME_Error, TEXT("ImportText: Bad parenthesised struct %s in: %s"), GetName(), InBuffer );
							return NULL;
						}
					}
					Buffer++;
				}
				if( SubCount > 0 )
				{
					warnf( NAME_Error, TEXT("ImportText: Incomplete parenthesised struct %s in: %s"), GetName(), InBuffer );
					return NULL;
				}
			}

			// Skip comma.
			if( *Buffer==',' )
			{
				// Skip comma.
				Buffer++;
			}
			else if( *Buffer!=')' )
			{
				warnf( NAME_Error, TEXT("ImportText: %s Bad termination in: %s"), GetName(), InBuffer );
				return NULL;
			}
		}

		// Skip trailing ')'.
		Buffer++;
	}
	else
	{
		// rjp -- only log warning if the value isn't blank
		if ( *(Buffer-1) )
			debugf( NAME_Warning, TEXT("ImportText: Struct %s missing '(' in: %s"), GetName(), InBuffer );

		return NULL;
	}
	return Buffer;
	unguardobj;
}
void UStructProperty::CopySingleValue( void* Dest, void* Src, UObject* SuperObject ) const
{
	guardSlow(UStructProperty::CopySingleValue);
	if (PropertyFlags & CPF_NeedCtorLink)
	{
		for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(Struct); It; ++It )
			It->CopyCompleteValue( (BYTE*)Dest + It->Offset, (BYTE*)Src + It->Offset, SuperObject );
	}
	else
	{
		appMemcpy( Dest, Src, ElementSize );
	}
	unguardobjSlow;
}
void UStructProperty::CopyCompleteValue( void* Dest, void* Src, UObject* SuperObject) const
{
	if (PropertyFlags & CPF_NeedCtorLink)
	{
		Super::CopyCompleteValue(Dest,Src,SuperObject);
	}
	else
	{
		// memcpy the entire struct
		appMemcpy( Dest, Src, ArrayDim*ElementSize );
	}
}
void UStructProperty::ClearValue( BYTE* Data ) const
{
	guard(UStructProperty::ClearValue);

	for ( TFieldIterator<UProperty,CLASS_IsAUProperty> It(Struct); It; ++It )
		It->ClearValue(Data + It->Offset);

	unguardobj;
}
void UStructProperty::DestroyValue( void* Dest ) const
{
	guardSlow(UStructProperty::DestroyValue);
	for( UProperty* P=Struct->ConstructorLink; P; P=P->ConstructorLinkNext )
	{
		if( ArrayDim <= 0 )
		{
			P->DestroyValue( (BYTE*) Dest + P->Offset );
		}
		else
		{
			for( INT i=0; i<ArrayDim; i++ )
				P->DestroyValue( (BYTE*)Dest + i*ElementSize + P->Offset );
		}
	}
	unguardobjSlow;
}
UBOOL UStructProperty::HasValue( const BYTE* Data ) const
{
	guard(UStructProperty::HasValue);
	for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(Struct); It; ++It )
		if ( It->HasValue(Data + It->Offset) )
			return 1;

	return 0;
	unguardobj;
}
/* rjp struct localization */
UBOOL UStructProperty::IsLocalized() const
{
	guard(UStructProperty::IsLocalized);
	for ( TFieldIterator<UProperty,CLASS_IsAUProperty> It(Struct); It; ++It )
		if ( It->IsLocalized() )
			return 1;
	return Super::IsLocalized();
	unguardobj;
}

IMPLEMENT_CLASS(UStructProperty);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

