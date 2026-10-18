/*=============================================================================
	UnClass.cpp: Object class implementation.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "CorePrivate.h"
#include "UC2ScriptSaveConfig.h"

/*-----------------------------------------------------------------------------
	FPropertyTag.
-----------------------------------------------------------------------------*/

//
// A tag describing a class property, to aid in serialization.
//
struct FPropertyTag
{
	// Archive for counting property sizes.
	class FArchiveCountSize : public FArchive
	{
	public:
		virtual UBOOL ShouldSerializeProperty( const UProperty* Prop )
		{
			return SaveAr.ShouldSerializeProperty(Prop);
		}

		FArchiveCountSize( FArchive& InSaveAr )
		: Size(0), SaveAr(InSaveAr)
		{
			ArIsSaving     = InSaveAr.IsSaving();
			ArIsPersistent = InSaveAr.IsPersistent();
		}
		INT Size;
	private:
		FArchive& SaveAr;
		FArchive& operator<<( UObject*& Obj )
		{
			INT Index = SaveAr.MapObject(Obj);
			return *this << AR_INDEX(Index);
		}
		FArchive& operator<<( FName& Name )
		{
			INT Index = SaveAr.MapName(&Name);
			return *this << AR_INDEX(Index);
		}
		void Serialize( void* V, INT Length )
		{
			Size += Length;
		}

	};

	// DELTA DEFAULTS
	// if TaggedVer==0 then named property on disk
	// if TaggedVer==1 then ref to property in mem
	BYTE	TaggedVer;

	// Variables.
	BYTE	Type;		// Type of property, 0=end.
	BYTE	Info;		// Packed info byte.
	FName	Name;		// Name of property.
	FName	ItemName;	// Struct name if UStructProperty.
	INT		Size;       // Property size.
	INT		ArrayIndex;	// Index if an array; else 0.

	// DELTA DEFAULTS
	// If stripped. (TaggedVer = 1)
	// not serialized to disk (only in mem)
	UProperty* Property;

	// Constructors.
	FORCEINLINE FPropertyTag()
	{}
	FORCEINLINE FPropertyTag( FArchive& InSaveAr, UProperty* InProperty, INT InIndex, BYTE* Value )
	:	Type		( InProperty->GetID() )
	,	Name		( InProperty->GetFName() )
	,	ItemName	( NAME_None     )
	,	Size		( 0             )
	,	ArrayIndex	( InIndex       )
	,	Info		( InProperty->GetID() )
	// DELTA DEFAULTS
	,	Property	( InProperty )
	,	TaggedVer	( 0	)
	{
		// Handle structs.
		UStructProperty* StructProperty = Cast<UStructProperty>( Property );
		if( StructProperty )
			ItemName = StructProperty->Struct->GetFName();

		// Set size.
		FArchiveCountSize ArCount( InSaveAr );
		SerializeTaggedProperty( ArCount, Property, Value, 0 );
		Size = ArCount.Size;

		// Update info bits.
		Info |=
		(	Size==1		? 0x00
		:	Size==2     ? 0x10
		:	Size==4     ? 0x20
		:	Size==12	? 0x30
		:	Size==16	? 0x40
		:	Size<=255	? 0x50
		:	Size<=65536 ? 0x60
		:			      0x70);
		UBoolProperty* Bool = Cast<UBoolProperty>( Property );
		if( ArrayIndex || (Bool && (*(BITFIELD*)Value & Bool->BitMask)) )
			Info |= 0x80;
	}

	// Serializer.
	friend FArchive& operator<<( FArchive& Ar, FPropertyTag& Tag )
	{
#ifdef _DEBUG
		static TCHAR PrevTag[NAME_SIZE]=TEXT("");
#endif
		guard(FPropertyTag<<);
		BYTE SizeByte;
		_WORD SizeWord;
		INT SizeInt;

		// DELTA DEFAULTS
		// if TaggedVer==0 then properties stored by name
		if( Ar.Ver() >= 148 )
			Ar << Tag.TaggedVer;
		else
			Tag.TaggedVer = 0;

		// Name.
		guard(TagName);

		// DELTA DEFAULTS
		// Stored by name.
		if( Tag.TaggedVer == 0 )
		{
			// read name of property
			Ar << Tag.Name;
			if( Tag.Name == NAME_None )
				return Ar;
		}
		// Stored by ref
		else 
		{
			// read ptr to property
			Ar << Tag.Property;
			Tag.Name = NAME_None;
			if( Tag.Property == NULL )
				return Ar;
		}

#ifdef _DEBUG
		appStrcpy( PrevTag, *Tag.Name );
#endif
		unguard;

		// Packed info byte:
		// Bit 0..3 = raw type.
		// Bit 4..6 = serialized size: [1 2 4 12 16 byte word int].
		// Bit 7    = array flag.
		Ar << Tag.Info;
		Tag.Type = Tag.Info & 0x0f;
		
		// DELTA DEFAULTS
		if( Tag.TaggedVer == 0 )
		{
			// only need type when reading named property from disk
			if( Tag.Type == NAME_StructProperty )
				Ar << Tag.ItemName;
		}
		
		switch( Tag.Info & 0x70 )
		{
			case 0x00:
				Tag.Size = 1;
				break;
			case 0x10:
				Tag.Size = 2;
				break;
			case 0x20:
				Tag.Size = 4;
				break;
			case 0x30:
				Tag.Size = 12;
				break;
			case 0x40:
				Tag.Size = 16;
				break;
			case 0x50:
				SizeByte =  Tag.Size;
				Ar       << SizeByte;
				Tag.Size =  SizeByte;
				break;
			case 0x60:
				SizeWord =  Tag.Size;
				Ar       << SizeWord;
				Tag.Size =  SizeWord;
				break;
			case 0x70:
				SizeInt		=  Tag.Size;
				Ar          << SizeInt;
				Tag.Size    =  SizeInt;
				break;
		}
		if( (Tag.Info&0x80) && Tag.Type!=NAME_BoolProperty )
		{
			BYTE B
			=	(Tag.ArrayIndex<=127  ) ? (Tag.ArrayIndex    )
			:	(Tag.ArrayIndex<=16383) ? (Tag.ArrayIndex>>8 )+0x80
			:	                          (Tag.ArrayIndex>>24)+0xC0;
			Ar << B;
			if( (B & 0x80)==0 )
			{
				Tag.ArrayIndex = B;
			}
			else if( (B & 0xC0)==0x80 )
			{
				BYTE C = Tag.ArrayIndex & 255;
				Ar << C;
				Tag.ArrayIndex = ((INT)(B&0x7F)<<8) + ((INT)C);
			}
			else
			{
				BYTE C = Tag.ArrayIndex>>16;
				BYTE D = Tag.ArrayIndex>>8;
				BYTE E = Tag.ArrayIndex;
				Ar << C << D << E;
				Tag.ArrayIndex = ((INT)(B&0x3F)<<24) + ((INT)C<<16) + ((INT)D<<8) + ((INT)E);
			}
		}
		else Tag.ArrayIndex = 0;
		return Ar;
#ifdef _DEBUG
		unguardf(( TEXT("(After %s)"), PrevTag ));
#else
		unguard;
#endif
	}

	// Property serializer.
	FORCEINLINE void SerializeTaggedProperty( FArchive& Ar, UProperty* Property, BYTE* Value, INT MaxReadBytes )
	{
		guard(FPropertyTag::SerializeTaggedProperty);
		if( Property->GetClass()==UBoolProperty::StaticClass() )
		{
			UBoolProperty* Bool = (UBoolProperty*)Property;
			check(Bool->BitMask!=0);
			if( Ar.IsLoading() )				
			{
				if( Info&0x80)	*(BITFIELD*)Value |=  Bool->BitMask;
				else			*(BITFIELD*)Value &= ~Bool->BitMask;
			}
		}
		else
		{
			Property->SerializeItem( Ar, Value, MaxReadBytes );
		}
		unguard;
	}
};

/*-----------------------------------------------------------------------------
	UField implementation.
-----------------------------------------------------------------------------*/

UField::UField( ENativeConstructor, UClass* InClass, const TCHAR* InName, const TCHAR* InPackageName, DWORD InFlags, UField* InSuperField )
: UObject				( EC_NativeConstructor, InClass, InName, InPackageName, InFlags )
, SuperField			( InSuperField )
, Next					( NULL )
{}
UField::UField( EStaticConstructor, const TCHAR* InName, const TCHAR* InPackageName, DWORD InFlags )
: UObject				( EC_StaticConstructor, InName, InPackageName, InFlags )
, Next					( NULL )
{}
UField::UField( UField* InSuperField )
:	SuperField( InSuperField )
{}
UClass* UField::GetOwnerClass() const
{
	guardSlow(UField::GetOwnerClass);
	const UObject* Obj;
	for( Obj=this; Obj->GetClass()!=UClass::StaticClass(); Obj=Obj->GetOuter() );
	return (UClass*)Obj;
	unguardSlow;
}
void UField::Bind()
{
	guard(UField::Bind);
	unguard;
}
void UField::PostLoad()
{
	guard(UField::PostLoad);
	Super::PostLoad();
	Bind();
	unguard;
}
void UField::Serialize( FArchive& Ar )
{
	guard(UField::Serialize);
	Super::Serialize( Ar );

	Ar << SuperField << Next;

	unguardobj;
}
INT UField::GetPropertiesSize()
{
	return 0;
}
UBOOL UField::MergeBools()
{
	return 1;
}
void UField::AddCppProperty( UProperty* Property )
{
	guard(UField::AddCppProperty);
	appErrorf(TEXT("UField::AddCppProperty"));
	unguard;
}
void UField::Register()
{
	guard(UField::Register);
	Super::Register();
	if( SuperField )
		SuperField->ConditionalRegister();
	unguard;
}
IMPLEMENT_CLASS(UField)

/*-----------------------------------------------------------------------------
	UStruct implementation.
-----------------------------------------------------------------------------*/

//
// Constructors.
//
#if PLATFORM_64BITS
// The C++ sizeof of every natively declared class (DECLARE_CLASS passes it here), kept because Link
// replaces PropertiesSize with the size the loaded script properties give. Plain BSS: these constructors run
// during static initialisation.
enum { MAX_NATIVE_STRUCT_SIZES=4096 };
static UStruct* GNativeStructs[MAX_NATIVE_STRUCT_SIZES];
static INT      GNativeStructSizes[MAX_NATIVE_STRUCT_SIZES];
static INT      GNativeStructCount=0;
CORE_API UBOOL  GLayoutWarnOnly=0;		// ucc verifyclasses: report every mismatch instead of stopping at the first.
CORE_API INT    GLayoutMismatches=0;
CORE_API INT GetNativeStructSize( UStruct* Struct )
{
	for( INT i=0; i<GNativeStructCount; i++ )
		if( GNativeStructs[i]==Struct )
			return GNativeStructSizes[i];
	return -1;
}
#endif
UStruct::UStruct( ENativeConstructor, INT InSize, const TCHAR* InName, const TCHAR* InPackageName, DWORD InFlags, UStruct* InSuperStruct )
:	UField			( EC_NativeConstructor, UClass::StaticClass(), InName, InPackageName, InFlags, InSuperStruct )
,	Children		( NULL )
,	PropertiesSize	( InSize )
,	Script			()
,	RefLink			( NULL )
,	PropertyLink	( NULL )
,	ConstructorLink	( NULL )
#ifndef _XBOX
,	ScriptText		( NULL )
,	CppText			( NULL )
,	TextPos			( 0 )
,	Line			( 0 )
	// Epic's DLL builds construct native classes in zero-initialised static storage; the static link
	// (__STATIC_LINK) constructs them on the heap. A non-zero ScriptTextCRC is taken as the cached CRC
	// (GetScriptTextCRC), which `ucc make` writes into the dependency records of native classes.
,	StructFlags		( 0 )
,	ScriptTextCRC	( 0 )
#endif
{
#if PLATFORM_64BITS
	if( GNativeStructCount<MAX_NATIVE_STRUCT_SIZES )
	{
		GNativeStructs[GNativeStructCount]     = this;
		GNativeStructSizes[GNativeStructCount] = InSize;
		GNativeStructCount++;
	}
#endif
}
UStruct::UStruct( EStaticConstructor, INT InSize, const TCHAR* InName, const TCHAR* InPackageName, DWORD InFlags )
:	UField			( EC_StaticConstructor, InName, InPackageName, InFlags )
,	Children		( NULL )
,	PropertiesSize	( InSize )
,	Script			()
,	RefLink			( NULL )
,	PropertyLink	( NULL )
,	ConstructorLink	( NULL )
#ifndef _XBOX
,	ScriptText		( NULL )
,	CppText			( NULL )
,	TextPos			( 0 )
,	Line			( 0 )
	// Epic's DLL builds construct native classes in zero-initialised static storage; the static link
	// (__STATIC_LINK) constructs them on the heap. A non-zero ScriptTextCRC is taken as the cached CRC
	// (GetScriptTextCRC), which `ucc make` writes into the dependency records of native classes.
,	StructFlags		( 0 )
,	ScriptTextCRC	( 0 )
#endif
{
#if PLATFORM_64BITS
	// Static link (__STATIC_LINK, every target here) registers classes through this constructor.
	if( GNativeStructCount<MAX_NATIVE_STRUCT_SIZES )
	{
		GNativeStructs[GNativeStructCount]     = this;
		GNativeStructSizes[GNativeStructCount] = InSize;
		GNativeStructCount++;
	}
#endif
}
UStruct::UStruct( UStruct* InSuperStruct )
:	UField( InSuperStruct )
,	PropertiesSize( InSuperStruct ? InSuperStruct->GetPropertiesSize() : 0 )
,	RefLink( NULL )
{}

//
// Add a property.
//
void UStruct::AddCppProperty( UProperty* Property )
{
	guard(UStruct::AddCppProperty);
	Property->Next = Children;
	Children       = Property;
	unguard;
}

//
// Native placeholders. The shipped packages were compiled for 32-bit pointers and declare as `int` the
// script variables that stand in for C++ data script never touches: the object header, vtable pointers of
// secondary C++ bases, private pointers and maps. On a 64-bit target the C++ data is bigger than its stand-in,
// so Link gives each one the size of its C++ counterpart (the property itself stays a 4-byte int). Kind
// PH_Member advances the layout by Size; PH_ArrayElement makes Size the element stride of an array<int> that
// C++ fills with pointers. Lookup by string: a cached FName can be purged and its index reused.
// Checked by `ucc-bin verifyclasses` (sizes, offsets, and that no bytecode reads a placeholder) and
// tools/dev/layout-check.py.
// The table is also the one source the C++ header exporter reads (any pointer width: `ucc-bin make`
// is a 32-bit program), so the generated <Pkg>Classes.h are 64-bit-correct as generated. CppType is the C++
// spelling of one element of the stand-in, written in place of `INT` (`PTRINT Node`, `PTRINT TextureInterfaces[3]`,
// `TArrayNoInit<PTRINT> OctreeNodes`); NULL for C++ data a script-exported member cannot spell (the object
// header, base-class vtables, maps): their classes are `noexport` and hand-written, and exporting one is an error
// (GetNativePlaceholderCppType).
//
enum EPlaceholderKind { PH_Member, PH_ArrayElement };
struct FNativePlaceholder
{
	const TCHAR* Package;
	const TCHAR* Struct;
	const TCHAR* Property;
	INT Size;
	INT Kind;
	const TCHAR* CppType;
};
static const FNativePlaceholder GNativePlaceholders[] =
{
	// vtable, Index, HashNext, HashOuterNext, StateFrame, _Linker, _LinkerIndex: everything before Outer.
	{ TEXT("Core"),   TEXT("Object"),       TEXT("ObjectInternal"), (INT)(sizeof(UObject) - sizeof(UObject*) - sizeof(DWORD) - sizeof(FName) - sizeof(UClass*)), PH_Member, NULL },
	{ TEXT("Core"),   TEXT("Subsystem"),    TEXT("ExecVtbl"),       (INT)sizeof(FExec),          PH_Member, NULL },
	{ TEXT("Core"),   TEXT("Orientation"),  TEXT("MA"),             (INT)sizeof(void*),          PH_Member, TEXT("PTRINT") },	// UMatSubAction* (FOrientation, Engine.h)
	{ TEXT("Engine"), TEXT("Player"),       TEXT("vfOut"),          (INT)sizeof(FOutputDevice),  PH_Member, NULL },
	{ TEXT("Engine"), TEXT("Player"),       TEXT("vfExec"),         (INT)sizeof(FExec),          PH_Member, NULL },
	{ TEXT("Engine"), TEXT("ActorRenderDataPtr"),               TEXT("Ptr"), (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FDynamicActor*
	{ TEXT("Engine"), TEXT("LightRenderDataPtr"),               TEXT("Ptr"), (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FDynamicLight*
	{ TEXT("Engine"), TEXT("ProjectorRenderInfoPtr"),           TEXT("Ptr"), (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FProjectorRenderInfo*
	{ TEXT("Engine"), TEXT("StaticMeshProjectorRenderInfoPtr"), TEXT("Ptr"), (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FStaticProjectorInfo*
	{ TEXT("Engine"), TEXT("Actor"),          TEXT("OctreeNodes"),           (INT)sizeof(void*), PH_ArrayElement, TEXT("PTRINT") },	// FOctreeNode*
	{ TEXT("Engine"), TEXT("StateInfo"),      TEXT("Node"),                  (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// UStruct*
	{ TEXT("Engine"), TEXT("StateInfo"),      TEXT("StateNode"),             (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// UState*
	{ TEXT("Engine"), TEXT("StateInfo"),      TEXT("Code"),                  (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// BYTE*
	{ TEXT("Engine"), TEXT("FileLog"),        TEXT("LogAr"),                 (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FArchive*
	{ TEXT("Engine"), TEXT("MovieMaterial"),  TEXT("MovieDecoder"),          (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FMoviePlayer*
	{ TEXT("Engine"), TEXT("MovieMaterial"),  TEXT("RenderInterface"),       (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FMovieTexture*
	{ TEXT("Engine"), TEXT("MovieMaterial"),  TEXT("InMemoryDecodingBlock"), (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// BYTE*
	{ TEXT("Engine"), TEXT("MovieMaterial"),  TEXT("ThreadSynch"),           (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FCriticalSection*
	{ TEXT("Engine"), TEXT("MovieMaterial"),  TEXT("Thread"),                (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FRunnableThread*
	{ TEXT("Engine"), TEXT("ParticleEmitter"),TEXT("Particles"),             (INT)sizeof(void*), PH_ArrayElement, TEXT("PTRINT") },	// FParticle*
	{ TEXT("GUI"),    TEXT("GUIController"),  TEXT("Designer"),              (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// UPropertyManagerBase*
	{ TEXT("IpDrv"),  TEXT("InternetLink"),   TEXT("PrivateResolveInfo"),    (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FResolveInfo*
	{ TEXT("IpDrv"),  TEXT("MasterServerLink"),  TEXT("LinkPtr"),            (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FMasterServer*Link*
	{ TEXT("IpDrv"),  TEXT("MasterServerClient"),TEXT("MSLinkPtr"),          (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FMasterServerClientLink*
	{ TEXT("Engine"), TEXT("Texture"),        TEXT("RenderInterface"),       (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FStaticTexture*
	{ TEXT("Engine"), TEXT("ScriptedTexture"),TEXT("RenderTarget"),          (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FScriptedRenderTarget* (punned)
	{ TEXT("Engine"), TEXT("ShadowBitmapMaterial"),TEXT("TextureInterfaces"),(INT)(3*sizeof(void*)), PH_Member, TEXT("PTRINT") },	// FAuxRenderTarget*[3] (punned)
	{ TEXT("Engine"), TEXT("Cubemap"),        TEXT("CubemapRenderInterface"),(INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FStaticCubemap*
	{ TEXT("Engine"), TEXT("Canvas"),         TEXT("pCanvasUtil"),           (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FCanvasUtil*
	{ TEXT("Engine"), TEXT("MotionBlur"),     TEXT("RenderTargets"),         (INT)(2*sizeof(void*)), PH_Member, TEXT("PTRINT") },	// FAuxRenderTarget*[2]
	{ TEXT("Engine"), TEXT("FluidSurfaceInfo"),TEXT("Indices"),              (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// void*
	{ TEXT("UCGameplay"), TEXT("UCScreenBloom"),      TEXT("EmissiveResultTarget"), (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FRenderTarget*
	{ TEXT("UCGameplay"), TEXT("UCScreenEmissive"),   TEXT("BackBufferTarget"),     (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FRenderTarget*
	{ TEXT("UCGameplay"), TEXT("UCScreenEmissive"),   TEXT("SelectTarget"),         (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FRenderTarget*
	{ TEXT("UCGameplay"), TEXT("UCScreenEmissive"),   TEXT("BlurTargets"),          (INT)sizeof(void*), PH_ArrayElement, TEXT("PTRINT") },	// FRenderTarget*
	{ TEXT("UCGameplay"), TEXT("UCScreenDOF"),        TEXT("BackBufferTarget"),     (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FRenderTarget*
	{ TEXT("UCGameplay"), TEXT("UCScreenDOF"),        TEXT("BlurTargets"),          (INT)sizeof(void*), PH_ArrayElement, TEXT("PTRINT") },	// FRenderTarget*
	{ TEXT("UCGameplay"), TEXT("UCScreenMotionBlur"), TEXT("BackBufferTarget"),     (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FRenderTarget*
	{ TEXT("UCGameplay"), TEXT("UCScreenMotionBlur"), TEXT("AccumulationTarget"),   (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FRenderTarget*
	{ TEXT("UCGameplay"), TEXT("UCScreenRadialBlur"), TEXT("BackBufferTarget"),     (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FRenderTarget*
	{ TEXT("UCGameplay"), TEXT("UCScreenRadialBlur"), TEXT("AccumulationTarget"),   (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FRenderTarget*
	{ TEXT("UCGameplay"), TEXT("UCScreenHeatShimmer"),TEXT("BackBufferTarget"),     (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FRenderTarget*
	{ TEXT("UCGameplay"), TEXT("UCScreenHeatShimmer"),TEXT("OffscreenTarget"),      (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// FRenderTarget*
	{ TEXT("GUI"),    TEXT("UIXInteraction"), TEXT("m_pFont"),               (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// CXBFont*
	{ TEXT("GUI"),    TEXT("UIXInteraction"), TEXT("m_pLiveEngine"),         (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// ILiveEngine*
	{ TEXT("GUI"),    TEXT("UIXInteraction"), TEXT("m_pUIPlugin"),           (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// ITitleUIPlugin*
	{ TEXT("GUI"),    TEXT("UIXInteraction"), TEXT("LiveInterface"),         (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// XboxLiveManager*
	{ TEXT("GUI"),    TEXT("UIXInteraction"), TEXT("FriendsInterface"),      (INT)sizeof(void*), PH_Member, TEXT("PTRINT") },	// LiveFriendsList*
	{ TEXT("UWeb"),   TEXT("WebResponse"),  TEXT("ReplacementMap"), (INT)sizeof(TMap<FString,FString>),      PH_Member, NULL },
	{ TEXT("UWeb"),   TEXT("WebRequest"),   TEXT("VariableMap"),    (INT)sizeof(TMultiMap<FString,FString>), PH_Member, NULL },
};
CORE_API INT GetNativePlaceholderCount() { return ARRAY_COUNT(GNativePlaceholders); }
CORE_API void GetNativePlaceholder( INT i, const TCHAR*& Package, const TCHAR*& Struct, const TCHAR*& Property, INT& Size, INT& Kind )
{
	const FNativePlaceholder& P = GNativePlaceholders[i];
	Package = P.Package; Struct = P.Struct; Property = P.Property; Size = P.Size; Kind = P.Kind;
}
static const FNativePlaceholder* FindNativePlaceholder( UStruct* Owner, UProperty* Property )
{
	if( !Owner->GetOuter() || (!Cast<UIntProperty>(Property) && !Cast<UArrayProperty>(Property)) )
		return NULL;
	// Script structs are outered to their class; the package is the outermost object.
	UObject* Package = Owner;
	while( Package->GetOuter() )
		Package = Package->GetOuter();
	for( INT i=0; i<ARRAY_COUNT(GNativePlaceholders); i++ )
	{
		const FNativePlaceholder& P = GNativePlaceholders[i];
		if
		(	appStricmp( Property->GetName(), P.Property )==0
		&&	appStricmp( Owner->GetName(), P.Struct )==0
		&&	appStricmp( Package->GetName(), P.Package )==0
		&&	(P.Kind==PH_Member ? Cast<UIntProperty>(Property)!=NULL : Cast<UArrayProperty>(Property) && Cast<UIntProperty>(((UArrayProperty*)Property)->Inner)!=NULL) )
			return &P;
	}
	return NULL;
}
CORE_API UBOOL IsNativePlaceholder( UProperty* Property )
{
	UStruct* Owner = Cast<UStruct>( Property->GetOuter() );
	return Owner && FindNativePlaceholder( Owner, Property )!=NULL;
}
CORE_API const TCHAR* GetNativePlaceholderCppTypeAt( INT i )
{
	return GNativePlaceholders[i].CppType;
}
//
// The C++ type the header exporter writes for an `int` property (UIntProperty::ExportCppItem): `INT`,
// or the table's CppType when the property is a placeholder member (`int X` / `int X[N]` of the table's struct) or
// the element of a placeholder array (`array<int> X`; the inner property is outered to the array property). Same
// lookup as Link, by string, at every pointer width. A placeholder with no C++ spelling (NULL CppType) in an exported
// class is an error in the make log: its class must stay `noexport` (hand-written), or the header would be wrong at
// 64 bits.
//
CORE_API const TCHAR* GetNativePlaceholderCppType( const UProperty* IntProperty )
{
	UProperty* Property = (UProperty*)IntProperty;
	UArrayProperty* Array = Cast<UArrayProperty>( Property->GetOuter() );
	if( Array && Array->Inner==Property )
		Property = Array;
	UStruct* Owner = Cast<UStruct>( Property->GetOuter() );
	const FNativePlaceholder* P = Owner ? FindNativePlaceholder( Owner, Property ) : NULL;
	if( !P || (Property==Array)!=(P->Kind==PH_ArrayElement) )
		return TEXT("INT");
	if( !P->CppType )
	{
		GWarn->Logf( NAME_Error, TEXT("Native placeholder %s.%s.%s has no C++ spelling: its class must be noexport (Core/Src/UnClass.cpp GNativePlaceholders)"), P->Package, P->Struct, P->Property );
		GWarn->ErrorCount++;	// "Failure - N error(s)" (an NAME_Error line alone counts as a warning)
		return TEXT("INT");
	}
	return P->CppType;
}

//
// Link offsets.
//
void UStruct::Link( FArchive& Ar, UBOOL Props )
{
	guard(UStruct::Link);

	// Link the properties.
	guard(LinkProperties);
	if( Props )
	{
		PropertiesSize = 0;
		if( GetInheritanceSuper() )
		{
			Ar.Preload( GetInheritanceSuper() );
			PropertiesSize = Align(GetInheritanceSuper()->GetPropertiesSize(),4);
		}
		UProperty* Prev = NULL;
		for( UField* Field=Children; Field; Field=Field->Next )
		{
			Ar.Preload( Field );
			if( Field->GetOuter()!=this )
				break;
			UProperty* Property = Cast<UProperty>( Field, CLASS_IsAUProperty );
			if( Property )
			{
				Property->Link( Ar, Prev );
#if PLATFORM_64BITS
				const FNativePlaceholder* Placeholder = FindNativePlaceholder( this, Property );
				if( Placeholder && Placeholder->Kind==PH_ArrayElement )
					((UArrayProperty*)Property)->Inner->ElementSize = Placeholder->Size;
				PropertiesSize = Property->Offset + (Placeholder && Placeholder->Kind==PH_Member ? Placeholder->Size : Property->GetSize());
#else
				PropertiesSize = Property->Offset + Property->GetSize();
#endif
				Prev = Property;
			}
		}
		PropertiesSize = Align(PropertiesSize,4);
#if PLATFORM_64BITS
		// A native class whose C++ size differs from the size its script properties give is read through
		// two layouts; every member past the first difference is silent corruption, and objects are allocated
		// at the script size. Fatal, except while `ucc verifyclasses` lists them all.
		INT NativeSize = GetNativeStructSize( this );
		if( NativeSize>=0 && NativeSize!=PropertiesSize )
		{
			GLayoutMismatches++;
			if( GLayoutWarnOnly )
				GWarn->Logf( NAME_Warning, TEXT("VERIFY: [layout] native %s: C++ sizeof=%i but script layout=%i"), GetPathName(), NativeSize, PropertiesSize );
			else
				appErrorf( TEXT("Native class %s: C++ sizeof=%i but script layout=%i (run ucc-bin verifyclasses)"), GetPathName(), NativeSize, PropertiesSize );
		}
#endif
	}
	else
	{
		UProperty* Prev = NULL;
		for( UField* Field=Children; Field && Field->GetOuter()==this; Field=Field->Next )
		{
			UProperty* Property = Cast<UProperty>( Field, CLASS_IsAUProperty );
			if( Property )
			{
				UBoolProperty*	BoolProperty = Cast<UBoolProperty>(Property, CLASS_IsAUBoolProperty);
				INT				SavedOffset = Property->Offset;
				BITFIELD		SavedBitMask = BoolProperty ? BoolProperty->BitMask : 0;

				Property->Link( Ar, Prev );
				Property->Offset = SavedOffset;
				Prev = Property;

				if(Cast<UBoolProperty>(Property, CLASS_IsAUBoolProperty))
					Cast<UBoolProperty>(Property, CLASS_IsAUBoolProperty)->BitMask = SavedBitMask;
			}
		}
	}
	unguard;

	//jg -- Use a cache for the traversal of the fields
	TArray<UProperty*,8,1> CachedProps;

	// Link the references, structs, and arrays for optimized cleanup.
	// Note: Could optimize further by adding UProperty::NeedsDynamicRefCleanup, excluding things like arrays of ints.
	guard(LinkRefs);
	UProperty** RefLinkPtr = (UProperty**)&RefLink;
	for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(this); It; ++It)
	{
		if( ExactCast<UClassProperty>(*It) || ExactCast<UObjectProperty>(*It) ||
			ExactCast<UStructProperty>(*It) || ExactCast<UArrayProperty>(*It) ||
			ExactCast<UDelegateProperty>(*It) )
		{
			*RefLinkPtr = *It;
			RefLinkPtr=&(*RefLinkPtr)->NextRef;
		}
		// Add to cache for use below (one traverse through all fields instead of 3)
		CachedProps.AddItem(*It);
	}
	*RefLinkPtr = NULL;
	unguard;

	// Link the cleanup.
	guard(LinkCleanup);
	TMap<UProperty*,INT> Map;
	UProperty** PropertyLinkPtr    = &PropertyLink;
	UProperty** ConstructorLinkPtr = &ConstructorLink;
	for (INT Index = 0; Index < CachedProps.Num(); Index++)
	{
		UProperty* PropC = CachedProps(Index);
		if( (PropC->PropertyFlags & CPF_Net) && !GIsEditor )
		{
			PropC->RepOwner = PropC;
			FArchive TempAr;
			INT iCode = PropC->RepOffset;
			PropC->GetOwnerClass()->SerializeExpr( iCode, TempAr );
			Map.Set( PropC, iCode );
			for (INT Index2 = 0; Index2 < CachedProps.Num() && Index2 < Index; Index2++)
			{
				UProperty* PropD = CachedProps(Index2);
				if( PropD->PropertyFlags & CPF_Net )
				{
					INT* iCodePtr = Map.Find( PropD );
					check(iCodePtr);
					if
					(	iCode-PropC->RepOffset==*iCodePtr-PropD->RepOffset
					&&	appMemcmp(&PropC->GetOwnerClass()->Script(PropC->RepOffset),&PropD->GetOwnerClass()->Script(PropD->RepOffset),iCode-PropC->RepOffset)==0 )
					{
						PropD->RepOwner = PropC->RepOwner;
					}
				}
			}
		}
		if( PropC->PropertyFlags & CPF_NeedCtorLink )
		{
			*ConstructorLinkPtr = PropC;
			ConstructorLinkPtr  = &(*ConstructorLinkPtr)->ConstructorLinkNext;
		}
		*PropertyLinkPtr = PropC;
		PropertyLinkPtr  = &(*PropertyLinkPtr)->PropertyLinkNext;
	}
	*PropertyLinkPtr    = NULL;
	*ConstructorLinkPtr = NULL;
	unguard;

	unguard;
}

//
// Serialize all of the class's data that belongs in a particular
// bin and resides in Data.
//
void UStruct::SerializeBin( FArchive& Ar, BYTE* Data, INT MaxReadBytes )
{
	INT MaxReadPos = Ar.Tell() + MaxReadBytes;
	INT Index=0;
	guard(UStruct::SerializeBin);
	
	//epic moelfke: use PropertyLink instead of a slower TFieldIterator
	for (UProperty* It = PropertyLink; It; It = It->PropertyLinkNext)
	{
		if( It->ShouldSerializeValue(Ar) )
		{
			for( Index=0; Index<It->ArrayDim; Index++ )
			{	
				if( Ar.Ver() >= 118 || !MaxReadBytes || !Ar.IsLoading() || Ar.Tell() < MaxReadPos )
					It->SerializeItem( Ar, Data + It->Offset + Index*It->ElementSize, 0 );
				else
					debugf( NAME_Warning, TEXT("Binary-serialized struct property %s of %s does not exist on disk.  Skipping."), It->GetName(), GetName() );
			}
		}
	}
	unguardf(( TEXT("(%s [%i])"), GetFullName(), Index ));
}

// DELTA DEFAULTS. Tagged default helper archives.
class FTaggedDefaultWriter : public FArchive
{
public:
	FTaggedDefaultWriter( TArray<BYTE>& InBytes, UClass* LoadClass )
	:	Bytes	( InBytes )
	,	Pos 	( 0 )
	,	LoadingClass( LoadClass )
	{
		ArIsSaving = 1;
		Remap = 0;
	}
	void Serialize( void* InData, INT Length )
	{
		if( Pos+Length>Bytes.Num() )
			Bytes.Add( Pos+Length-Bytes.Num() );
		appMemcpy( &Bytes(Pos), InData, Length );
		Pos += Length;
	}
	INT Tell()
	{
		return Pos;
	}
	void Seek( INT InPos )
	{
		Pos = InPos;
	}
	INT TotalSize()
	{
		return Bytes.Num();
	}
	virtual FArchive& operator<<( class FName& N )
	{
		Serialize( &N, sizeof(FName) );
		return *this;
	}
	virtual FArchive& operator<<( class UObject*& Res )
	{
		Serialize( &Res, sizeof(UObject*) );
		return *this;
	}

	UBOOL Remap;
private:
	TArray<BYTE>& Bytes;
	INT Pos;
	UClass* LoadingClass;
};

// DELTA DEFAULTS
class FTaggedDefaultArchive : public FArchive
{
public:
	FTaggedDefaultArchive( TArray<BYTE>& InBytes, UClass* LoadClass )
	:	Bytes	( InBytes )
	,	Pos 	( 0 )
	,	LoadingClass( LoadClass )
	{
		ArIsLoading = 1;
		ArIsTrans = 1;
	}
	void Serialize( void* Data, INT Num )
	{
		check(Pos>=0);
		check(Pos+Num<=Bytes.Num());
		appMemcpy( Data, &Bytes(Pos), Num );
		Pos += Num;
	}
	INT Tell()
	{
		return Pos;
	}
	INT TotalSize()
	{
		return Bytes.Num();
	}
	void Seek( INT InPos )
	{
		check(InPos>=0);
		check(InPos<=Bytes.Num());
		Pos = InPos;
	}
	UBOOL AtEnd()
	{
		return Pos>=Bytes.Num();
	}
	virtual FArchive& operator<<( class FName& N )
	{
		Serialize( &N, sizeof(FName) );
		return *this;
	}
	virtual FArchive& operator<<( class UObject*& Res )
	{
		Serialize( &Res, sizeof(UObject*) );
		return *this;
	}


private:
	TArray<BYTE>& Bytes;
	INT Pos;
	UClass* LoadingClass;
};

// DELTA DEFAULTS
class CORE_API FTaggedDefaultReader : public FArchive
{
public:
	FTaggedDefaultReader( UClass* LoadClass, FArchive* InAr )
	:	LoadingClass( LoadClass )
	,	LinkerAr( InAr )

	{
		ArVer			= InAr->Ver();
		ArNetVer		= InAr->NetVer();
		ArLicenseeVer	= InAr->LicenseeVer();
		ArIsLoading		= InAr->IsLoading();
		ArIsSaving		= InAr->IsSaving();
		ArIsTrans		= InAr->IsTrans();
		ArIsPersistent  = InAr->IsPersistent();
		ArIsError       = InAr->IsError();
		ArIsCriticalError= InAr->IsCriticalError();
		ArForEdit		= InAr->ForEdit();
		ArForClient		= InAr->ForClient();
		ArForServer		= InAr->ForServer();
		ArContainsCode  = InAr->ContainsCode();
		ArIsRawUData	= InAr->IsRawUData();
		ArIsLoading = ArIsTrans = 1;

	}
	void Serialize( void* Data, INT Num )
	{
		LinkerAr->Serialize( Data, Num );
	}

	virtual void Preload( UObject* Object )
	{
		return LinkerAr->Preload( Object );
	}

	virtual void CountBytes( SIZE_T InNum, SIZE_T InMax )
	{}

	virtual INT MapName( FName* Name )
	{
		return LinkerAr->MapName( Name );
	}
	virtual INT MapObject( UObject* Object )
	{
		return LinkerAr->MapObject( Object );
	}
	virtual UBOOL AtStopper()
	{
		return LinkerAr->AtStopper();
	}

	virtual void SetStopper( INT InStopper=INDEX_NONE )
	{
		LinkerAr->SetStopper( InStopper );
	}

	virtual void AttachLazyLoader( FLazyLoader* LazyLoader )
	{
		LinkerAr->AttachLazyLoader( LazyLoader );
	}

	virtual void DetachLazyLoader( FLazyLoader* LazyLoader )
	{
		LinkerAr->DetachLazyLoader( LazyLoader );
	}

	virtual void Precache( INT HintCount )
	{
		LinkerAr->Precache( HintCount );
	}

	virtual void Flush()
	{
		LinkerAr->Flush();
	}
	virtual UBOOL Close()
	{
		return LinkerAr->Close();
	}
	virtual UBOOL GetError()
	{
		return LinkerAr->GetError();
	}

	INT Tell()
	{
		return LinkerAr->Tell();
	}
	INT TotalSize()
	{
		return LinkerAr->TotalSize();
	}
	void Seek( INT InPos )
	{
		LinkerAr->Seek( InPos );
	}
	UBOOL AtEnd()
	{
		return LinkerAr->AtEnd();
	}

	virtual FArchive& operator<<( class FName& N )
	{
		NAME_INDEX NameIndex;
		*LinkerAr << AR_INDEX(NameIndex);
		N = LoadingClass->GetLinker()->IndexToName( NameIndex );		
		return *this;
	}
	virtual FArchive& operator<<( class UObject*& Res )
	{
		INT Index;
		*LinkerAr << AR_INDEX(Index);
		Res = LoadingClass->GetLinker()->IndexToObject( Index );
		return *this;
	}
private:
	UClass* LoadingClass;
	FArchive* LinkerAr;
};

// DELTA DEFAULTS
void UStruct::LinkTaggedItem( FArchive& ReadAr, FArchive& WriteAr, UProperty* Property, FPropertyTag* Tag )
{
	// Tagged array. Might have tagged or untagged inner properties.
	if( Property->GetID() == NAME_ArrayProperty )
	{
		UArrayProperty* ArrProp = (UArrayProperty*)Property;
		if
		(	ArrProp->Inner->GetID() == NAME_ObjectProperty
		||	ArrProp->Inner->GetID() == NAME_ClassProperty
		||	ArrProp->Inner->GetID() == NAME_NameProperty
		||	ArrProp->Inner->GetID() == NAME_StructProperty
		||	ArrProp->Inner->GetID() == NAME_DelegateProperty )
		{
			INT n = 0;
			ReadAr << AR_INDEX(n);
			WriteAr << AR_INDEX(n);
			for( INT i=0; i<n; i++ )
			{
				LinkTaggedItem( ReadAr, WriteAr, ArrProp->Inner, NULL );
			}
			return;
		}
	}

	// Expand name, object AND struct properties (will get turned into valid pointers for linker indicies).
	if
	(	Property->GetID() == NAME_ObjectProperty
	||	Property->GetID() == NAME_ClassProperty
	||	Property->GetID() == NAME_NameProperty
	||	Property->GetID() == NAME_DelegateProperty )
	{
		// !!
		BYTE* TempData = (BYTE*)appAlloca( Property->ElementSize );
		appMemzero( TempData, Property->ElementSize );

		Property->SerializeItem( ReadAr, TempData );

		// Write them back.
		WriteAr.Serialize( TempData, Property->ElementSize );
	}
	// Sub-tagged struct.
	else if( Property->GetID() == NAME_StructProperty
			&&	(	((UStructProperty*)Property)->Struct->GetFName()!=NAME_Vector
				&&	((UStructProperty*)Property)->Struct->GetFName()!=NAME_Rotator
				&&	((UStructProperty*)Property)->Struct->GetFName()!=NAME_Color ) )
	{
		((UStructProperty*)Property)->Struct->LinkTaggedStruct( ReadAr, WriteAr );
	}
	// Tagged property, we know the full serialized size, so just do it directly.
	// !! Optimize.
	else if( Tag )
	{
		for( int i=0; i<Tag->Size; i++ )
		{
			BYTE Temp;
			ReadAr << Temp;
			WriteAr << Temp;
		}
	}
	// Untagged string.
	else if( Property->GetID() == NAME_StrProperty )
	{
		FString Temp;
		ReadAr << Temp;
		WriteAr << Temp;
	}
	// Final attempt, basic property, no tagged size!
	else
	{
		for( int i=0; i<Property->ElementSize; i++ )
		{
			BYTE Temp;
			ReadAr << Temp;
			WriteAr << Temp;
		}
	}
}

// DELTA DEFAULTS
void UStruct::LinkTaggedStruct( FArchive& ReadAr, FArchive& WriteAr )
{
	while( 1 )
	{
		FPropertyTag Tag;
		ReadAr << Tag;

		if( Tag.TaggedVer == 0 )
		{
			Tag.TaggedVer = 1;
			UProperty* Property = NULL;
			if( Tag.Name != NAME_None )
			{
				for( Property=PropertyLink; Property; Property=Property->PropertyLinkNext )
						if( Property->GetFName()==Tag.Name )
							break;
			}
			Tag.Property = Property;
		}


		WriteAr << Tag;

		if( Tag.TaggedVer == 0 && Tag.Name == NAME_None )
			break;
		else if( Tag.TaggedVer == 1 && Tag.Property == NULL )
				break;

		LinkTaggedItem( ReadAr, WriteAr, Tag.Property, &Tag );
	}
}

void UStruct::SerializeTaggedProperties( FArchive& Ar, BYTE* Data, UClass* DefaultsClass, UClass* ClassDefaults )
{
	FName PropertyName(NAME_None);
	INT Index=-1;
	guard(UStruct::SerializeTaggedProperties);
	check(Ar.IsLoading() || Ar.IsSaving());

	// Find defaults.
	BYTE* Defaults      = NULL;
	INT   DefaultsCount = 0;
	if( DefaultsClass )
	{
		Defaults      = &DefaultsClass->Defaults(0);
		DefaultsCount =  DefaultsClass->Defaults.Num();
	}

	// DELTA DEFAULTS
	// We're serializing the class defaults. Use a pair of FArchives to transfer the tagged properties
	// directly into our defaults array, but pre-link objects and names correctly.
	if( ClassDefaults )
	{
		ClassDefaults->Defaults.Empty();

		FTaggedDefaultReader TransAr( ClassDefaults, &Ar);
		FTaggedDefaultWriter DefaultAr( ClassDefaults->Defaults, ClassDefaults );

		LinkTaggedStruct( TransAr, DefaultAr );

        ClassDefaults->Defaults.Shrink();

		//debugf(TEXT("Deferred default tag serialization for class %s %i"), ClassDefaults->GetName(), TotalSize );
		ClassDefaults->ClassFlags |= CLASS_TaggedDefaults;
		return;
	}

	// Load/save.
	if( Ar.IsLoading() )
	{
		// Load all stored properties.
		INT Count=0;
		guard(LoadStream);
		//jg -- Moved to here so that we can keep track of where we are in
		// the property stream. This code assumes that properties are loaded
		// in the same order they are saved in. This removes a n^2 search and
		// makes it an O(n) when properties are saved in the same order as
		// they are loaded (default case). In the case that a property was
		// reordered the code falls back to a slower search
		UProperty* Property = PropertyLink;
		while( 1 )
		{
			FPropertyTag Tag;
			Ar << Tag;

			// DELTA DEFAULTS
			// If named ver, find property.
			if( Tag.TaggedVer == 0 )
			{
				if( Tag.Name == NAME_None )
					break;
				PropertyName = Tag.Name;

				// If this property is not the one we expect, do the brute force search
				if (Property == NULL || Property->GetFName() != Tag.Name)
				{
					UProperty* pEnd = Property;
					// Search forward first
					for ( ; Property; Property = Property->PropertyLinkNext )
					{
						if (Property->GetFName() == Tag.Name)
						{
							break;
						}
					}
					// If we still haven't found it
					if (Property == NULL)
					{
						// Then search from the beginning to the last known point
						for (Property = PropertyLink; Property && Property != pEnd;
							Property = Property->PropertyLinkNext)
						{
							if (Property->GetFName() == Tag.Name)
							{
								break;
							}
						}
						// In case we didn't find it
						if (Property == pEnd)
						{
							Property = NULL;
						}
					}
				}
			}
			// DELTA DEFAULTS
			// use explicit property ref
			else if( Tag.TaggedVer == 1 )
			{
				Property = Tag.Property;
				if( Tag.Property == NULL )
					break;
			}

			if( !Property )
			{
				debugf( NAME_Warning, TEXT("Property %s of %s not found"), *Tag.Name, GetFullName() );
			}
#ifndef _XBOX
			else if( Cast<UArrayProperty>(Property) && Tag.Type!=NAME_ArrayProperty && Cast<UArrayProperty>(Property)->Inner->GetID()==Tag.Type )
			{
				//oldver: Convert fixed-length arrays into dynamic arrays
				debugf( NAME_Warning, TEXT("Converting in %s of %s to dynamic array."), *Tag.Name, GetName() );
				UProperty* InnerProp = Cast<UArrayProperty>(Property)->Inner;
				FArray* Arr = (FArray*)(Data + Property->Offset);
				if( Arr->Num() <= Tag.ArrayIndex )
					Arr->AddZeroed( InnerProp->ElementSize, 1+Tag.ArrayIndex-Arr->Num() );
				Tag.SerializeTaggedProperty( Ar, InnerProp, (BYTE*)Arr->GetData()+Tag.ArrayIndex * InnerProp->ElementSize, 0 );
				//jg -- Move to the next property to be serialized
				Property = Property->PropertyLinkNext;
				continue;
			}
			else if( Tag.Type==NAME_ByteProperty && Property->GetID()==NAME_FloatProperty )
			{
				//oldver: Convert bytes to floats.
				BYTE B;
				Ar << B;
				*(FLOAT*)(Data + Property->Offset + Tag.ArrayIndex * Property->ElementSize ) = (FLOAT) B;
				//jg -- Move to the next property to be serialized
				Property = Property->PropertyLinkNext;
				continue;
			}
			else if( Tag.TaggedVer != 1 && Tag.Type!=Property->GetID() )
			{
				debugf( NAME_Warning, TEXT("Type mismatch in %s of %s: file %i, class %i"), *Tag.Name, GetName(), Tag.Type, Property->GetID() );
			}
			else if( Tag.ArrayIndex>=Property->ArrayDim )
			{
				debugf( NAME_Warning, TEXT("Array bounds in %s of %s: %i/%i"), *Tag.Name, GetName(), Tag.ArrayIndex, Property->ArrayDim );
			}
			else if( Tag.TaggedVer != 1 && Tag.Type==NAME_StructProperty && Tag.ItemName!=CastChecked<UStructProperty>(Property)->Struct->GetFName() )
			{
				debugf( NAME_Warning, TEXT("Property %s of %s struct type mismatch %s/%s"), *Tag.Name, GetName(), *Tag.ItemName, CastChecked<UStructProperty>(Property)->Struct->GetName() );
			}
			else if( !Property->ShouldSerializeValue(Ar) )
			{
#ifdef _DEBUG
				if( appStricmp(*Tag.Name,TEXT("XLevel"))!=0 )
					debugf( NAME_Warning, TEXT("Property %s of %s is not serializable"), *Tag.Name, GetName() );
#endif
			}
#endif
			else
			{
				// This property is ok.
				/*INT StartPos =*/ Ar.Tell();							
				Tag.SerializeTaggedProperty( Ar, Property, Data + Property->Offset + Tag.ArrayIndex*Property->ElementSize, Tag.Size );
				//jg -- Move to the next property to be serialized
				Property = Property->PropertyLinkNext;
				continue;
			}

#ifdef _DEBUG
			// Skip unknown or bad property.
			if( appStricmp(*Tag.Name,TEXT("XLevel"))!=0 )
			{
				debugf( NAME_Warning, TEXT("Skipping %i bytes of type %i"), Tag.Size, Tag.Type );
			}
#endif
			BYTE B;
			for( INT i=0; i<Tag.Size; i++ )
			{
				Ar << B;
			}
		}
		unguardf(( TEXT("(Count %i)"), Count ));
		Count = 0;
	}
	else
	{
		// Save tagged properties.
		guard(SaveStream);
		//jg -- Iterate through and save properties in the same order
		for (UProperty* Property = PropertyLink; Property;
			Property = Property->PropertyLinkNext)
		{
			if( Property->ShouldSerializeValue(Ar) )
			{
				PropertyName = Property->GetFName();
				for( Index=0; Index < Property->ArrayDim; Index++ )
				{
					INT Offset = Property->Offset + Index * Property->ElementSize;
					if((!IsA(UClass::StaticClass()) && !Defaults) ||
						!Property->Matches( Data, (Offset + Property->ElementSize <= DefaultsCount) ? Defaults : NULL, Index) )
					{						
						FPropertyTag Tag( Ar, Property, Index, Data + Offset );
						Ar << Tag;
						Tag.SerializeTaggedProperty( Ar, Property, Data + Offset, 0 );
					}
				}
			}
		}

		// DELTA DEFAULTS
		if( Ar.Ver() >= 148 )
		{
			BYTE Tagged = 1;
			Ar << Tagged;
			UProperty* NullProp = NULL;
			Ar << NullProp;
		}
		else
		{
			FName Temp(NAME_None);
			Ar << Temp;
		}

		unguard;
	}
	unguardf(( TEXT("(%s[%i])"), *PropertyName, Index ));
}

void UStruct::Destroy()
{
	guard(UStruct::Destroy);
	Script.Empty();
	Super::Destroy();
	unguard;
}

void UStruct::Serialize( FArchive& Ar )
{
	guard(UStruct::Serialize);
	Super::Serialize( Ar );

	// Serialize stuff.
#ifdef _XBOX
	UTextBuffer	*ScriptText = NULL,
				*CppText = NULL;
#endif
	Ar << ScriptText << Children;
	if (Ar.Ver() < 138)
	{
		FName dummyFriendlyName;
		Ar << dummyFriendlyName;
	}

	Ar << CppText;

	// Compiler info.
#ifdef _XBOX
	//scion rwd - fake serialization for xbox mem savings
	INT Line = 0,
		TextPos = 0;
#endif
	Ar << Line << TextPos;

	// Script code.
	INT ScriptSize = Script.Num();
	//epic moelfke: better counting of how much memory these take
	Ar.CountBytes(ScriptSize, ScriptSize);
	Ar << ScriptSize;
	if( Ar.IsLoading() )
	{
		Script.Empty(ScriptSize);
		Script.Add( ScriptSize );
	}

	INT iCode = 0;
	while( iCode < ScriptSize )
		SerializeExpr( iCode, Ar );
	if( iCode != ScriptSize )
		appErrorf( TEXT("Script serialization mismatch: Got %i, expected %i"), iCode, ScriptSize );

	Ar.ThisContainsCode();
	// Link the properties.
	if( Ar.IsLoading() )
		Link( Ar, 1 );

	unguardobj;
}

//
// Actor reference cleanup.
//
void UStruct::CleanupDestroyed( BYTE* Data )
{
	guard(UStruct::CleanupDestroyed);
	if( GIsEditor )
	{
		// Slow cleanup in editor where optimized structures don't exist.
		for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(this); It; ++It )
			(*It)->CleanupDestroyed(Data+It->Offset);
	}
	else
	{
		// Optimal cleanup during gameplay.
		for( UProperty* Ref=RefLink; Ref; Ref=Ref->NextRef )
			Ref->CleanupDestroyed(Data+Ref->Offset);
	}
	unguardobj;
}

//
// Object reference cleanup.
//
UBOOL UStruct::CleanupDestroyed(BYTE* Data, UObject* Destroyed)
{
	guard(UStruct::CleanupDestroyed);
	UBOOL bReferenced = 0;
	if( GIsEditor )
	{
		// Slow cleanup in editor where optimized structures don't exist.
		for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(this); It; ++It )
		{
			bReferenced |= (*It)->CleanupDestroyed(Data+It->Offset, Destroyed);
		}
	}
	else
	{
		// Optimal cleanup during gameplay.
		for( UProperty* Ref=RefLink; Ref; Ref=Ref->NextRef )
		{
			bReferenced |= Ref->CleanupDestroyed(Data+Ref->Offset, Destroyed);
		}
	}
	return bReferenced;
	unguardobj;
}
IMPLEMENT_CLASS(UStruct);

/*-----------------------------------------------------------------------------
	UState.
-----------------------------------------------------------------------------*/

UState::UState( UState* InSuperState )
: UStruct( InSuperState )
{}
UState::UState( ENativeConstructor, INT InSize, const TCHAR* InName, const TCHAR* InPackageName, DWORD InFlags, UState* InSuperState )
:	UStruct			( EC_NativeConstructor, InSize, InName, InPackageName, InFlags, InSuperState )
,	ProbeMask		( 0 )
,	IgnoreMask		( 0 )
,	StateFlags		( 0 )
,	LabelTableOffset( 0 )
{}
UState::UState( EStaticConstructor, INT InSize, const TCHAR* InName, const TCHAR* InPackageName, DWORD InFlags )
:	UStruct			( EC_StaticConstructor, InSize, InName, InPackageName, InFlags )
,	ProbeMask		( 0 )
,	IgnoreMask		( 0 )
,	StateFlags		( 0 )
,	LabelTableOffset( 0 )
{}
void UState::Destroy()
{
	guard(UState::Destroy);
	Super::Destroy();
	unguard;
}
void UState::Serialize( FArchive& Ar )
{
	guard(UState::Serialize);
	Super::Serialize( Ar );

	// Class/State-specific union info.
	Ar << ProbeMask << IgnoreMask;
	Ar << LabelTableOffset << StateFlags;
	if (!Ar.IsLoading() ||
		Ar.Ver() >= 138)
	{
		// serialize the map directly
		Ar << FuncMap;
	}
	else
	{
		// Initialize function map
		for( TFieldIterator<UFunction> It(this); It && It->GetOuter()==this; ++It )
		{
			//epic moelfke: we never need to look for native final functions in the function map unless they're events
			if (!(It->FunctionFlags & FUNC_Native) || !(It->FunctionFlags & FUNC_Final) || (It->FunctionFlags & FUNC_Event))
			{
				FuncMap.Set(It->GetFName(),*It);
			}
		}
	}
	unguard;
}
IMPLEMENT_CLASS(UState);

/*-----------------------------------------------------------------------------
	UClass implementation.
-----------------------------------------------------------------------------*/

#if PLATFORM_64BITS
// A native class's config name waits here until Register(). Epic parked the `const TCHAR*` in the 4-byte
// ClassConfigName (an FName), which on 64-bit wrote its high half over ClassConstructor (UClass is pack(1)).
// Plain BSS: the constructors run during static initialisation.
enum { MAX_NATIVE_CONFIG_NAMES=4096 };
static UClass*      GNativeConfigClasses[MAX_NATIVE_CONFIG_NAMES];
static const TCHAR* GNativeConfigNames[MAX_NATIVE_CONFIG_NAMES];
static INT          GNativeConfigCount=0;
static void StashNativeConfigName( UClass* Class, const TCHAR* ConfigName )
{
	check(GNativeConfigCount<MAX_NATIVE_CONFIG_NAMES);
	GNativeConfigClasses[GNativeConfigCount] = Class;
	GNativeConfigNames  [GNativeConfigCount] = ConfigName;
	GNativeConfigCount++;
}
static const TCHAR* TakeNativeConfigName( UClass* Class )
{
	for( INT i=0; i<GNativeConfigCount; i++ )
		if( GNativeConfigClasses[i]==Class )
			return GNativeConfigNames[i];
	appErrorf( TEXT("No stashed config name for native class %s"), Class->GetName() );
	return NULL;
}
#endif

//
// Register the native class.
//
void UClass::Register()
{
	guard(UClass::Register);
	Super::Register();

	// Get stashed registration info.
#if PLATFORM_64BITS
	const TCHAR* InClassConfigName = TakeNativeConfigName( this );
#else
	const TCHAR* InClassConfigName = *(TCHAR**)&ClassConfigName;
#endif
	ClassConfigName = InClassConfigName;

	// Init default object.
	Defaults.Empty( GetPropertiesSize() );
	Defaults.Add( GetPropertiesSize() );
	GetDefaultObject()->InitClassDefaultObject( this );

	// Perform static construction.
	if( !GetSuperClass() || GetSuperClass()->ClassStaticConstructor!=ClassStaticConstructor )
		(GetDefaultObject()->*ClassStaticConstructor)();

	// Propagate inhereted flags.
	if( SuperField )
		ClassFlags |= (GetSuperClass()->ClassFlags & CLASS_Inherit);

	// Link the cleanup.
	FArchive ArDummy;
	Link( ArDummy, 0 );

	// Load defaults.
	GetDefaultObject()->LoadConfig();
	UC2NoteDefaultOverrides( this );
	GetDefaultObject()->LoadLocalized();

	unguardf(( TEXT("(%s)"), GetName() ));
}

void UClass::LanguageChange()
{
	guard(UClass::LanguageChange);

	Super::LanguageChange();
	GetDefaultObject()->LoadLocalized();

	unguardf(( TEXT("(%s)"), GetName() ));
}

//
// Find the class's native constructor.
//
void UClass::Bind()
{
	guard(UClass::Bind);
	UStruct::Bind();
	check(GIsEditor || GetSuperClass() || this==UObject::StaticClass());
	if( !ClassConstructor && (GetFlags() & RF_Native) )
	{
		// Find the native implementation.
		TCHAR ProcName[256];
		appSprintf( ProcName, TEXT("autoclass%s"), GetNameCPP() );

		// Find export from the DLL.
		UPackage* ClassPackage = GetOuterUPackage();
		UClass** ClassPtr = (UClass**)ClassPackage->GetDllExport( ProcName, 0 );
		if( ClassPtr )
		{
			check(*ClassPtr);
			if ( (GUglyHackFlags&64)==0 )
				check(*ClassPtr==this);
			ClassConstructor = (*ClassPtr)->ClassConstructor;
		}
		else if( !GIsEditor )
		{
			appErrorf( TEXT("Can't bind to native class %s"), GetPathName() );
		}
	}
	if( !ClassConstructor && GetSuperClass() )
	{
		// Chase down constructor in parent class.
		GetSuperClass()->Bind();
		ClassConstructor = GetSuperClass()->ClassConstructor;
	}
	check(GIsEditor || ClassConstructor);
	unguardobj;
}

// DELTA DEFAULTS
class CORE_API FTaggedGCArchive : public FArchive
{
public:
	FTaggedGCArchive( const TArray<BYTE>& InBytes, UClass* LoadClass, FArchive* GCArch )
	:	Bytes	( InBytes )
	,	Pos 	( 0 )
	,	LoadingClass( LoadClass )
	,	GCAr( GCArch )
	{
		ArIsLoading = 1;
		ArIsTrans = 1;
		ArIsSaving = 0;
	}
	void Serialize( void* Data, INT Num )
	{
		check(Pos>=0);
		check(Pos+Num<=Bytes.Num());
		appMemcpy( Data, &Bytes(Pos), Num );
		Pos += Num;
	}
	INT Tell()
	{
		return Pos;
	}
	INT TotalSize()
	{
		return Bytes.Num();
	}
	void Seek( INT InPos )
	{
		check(InPos>=0);
		check(InPos<=Bytes.Num());
		Pos = InPos;
	}
	UBOOL AtEnd()
	{
		return Pos>=Bytes.Num();
	}
	virtual FArchive& operator<<( class FName& N )
	{
		Serialize( &N, sizeof(FName) );
		*GCAr << N;
		return *this;
	}
	virtual FArchive& operator<<( class UObject*& Res )
	{
		Serialize( &Res, sizeof(UObject*) );
		*GCAr << Res;
		return *this;
	}
private:
	const TArray<BYTE>& Bytes;
	INT Pos;
	UClass* LoadingClass;
	FArchive* GCAr;
};

// DELTA DEFAULTS
void UClass::GCTaggedDefaults( FArchive& Ar )
{
	FTaggedGCArchive GCAr( Defaults, this, &Ar );

	TArray<BYTE> Temp;

	while( 1 )
	{
		FPropertyTag Tag;
		GCAr << Tag;

		if( Tag.TaggedVer == 0 && Tag.Name == NAME_None )
			break;
		else if( Tag.TaggedVer == 1 && Tag.Property == NULL )
			break;

		// This will recurse, but only on object references, in which case I don't care if it overwrites this anyhow.
		INT SizeNeed=Max<INT>( 4096, Tag.Size );
		Temp.Empty(SizeNeed);
		Temp.AddZeroed(SizeNeed);
		Tag.SerializeTaggedProperty( GCAr, Tag.Property, (BYTE*)&Temp(0), Tag.Size );
	}
}

// DELTA DEFAULTS
void UClass::SerializeTaggedDefaults( BYTE* Dest, UClass* DefaultsClass )
{
	if( !(ClassFlags & CLASS_TaggedDefaults) )
	{
		//appMemcpy( Dest + sizeof(UObject), &Defaults(0) + sizeof(UObject), Defaults.Num() - sizeof(UObject) );
		InitProperties( Dest, GetPropertiesSize(), this, &Defaults(0), GetPropertiesSize() );
		return;
	}

	debugf( NAME_DevLoad, TEXT("Serializing tagged defaults for %s"), GetName() );

	if( GetSuperClass() )
		GetSuperClass()->SerializeTaggedDefaults( Dest, DefaultsClass );

	FTaggedDefaultArchive DefaultAr( Defaults, this );
	SerializeTaggedProperties( DefaultAr, Dest, DefaultsClass );
}

// DELTA DEFAULTS
void UClass::ExpandDefaultObject()
{
	if( !(ClassFlags & CLASS_TaggedDefaults) )
		return;

	if( ClassFlags & CLASS_FullDefaults )
		return;

	ClassFlags |= CLASS_FullDefaults;

	UObject::BeginLoad();

	debugf( NAME_Warning, TEXT("Expanding default object on access for %s"), GetName() );

	// Copy to a temp array for expansion.
	BYTE* Temp = (BYTE*)appAlloca( GetPropertiesSize() );
	appMemzero( Temp, GetPropertiesSize() );

	((UObject*)Temp)->InitClassDefaultObject( this, 1, 1 );

	SerializeTaggedDefaults( Temp, GetSuperClass() );

	Defaults.Empty( GetPropertiesSize() );
	Defaults.Add( GetPropertiesSize() );
	appMemcpy( &Defaults(0), Temp, GetPropertiesSize() );

	ClassFlags &= ~CLASS_TaggedDefaults;
	ClassFlags &= ~CLASS_FullDefaults;

	UObject::EndLoad();
}

/*-----------------------------------------------------------------------------
	UClass UObject implementation.
-----------------------------------------------------------------------------*/

static INT Compare( UField* A, UField* B )
{
	if( !A->GetLinker() || !B->GetLinker() )
		return 0;
#if ENGINE_VERSION<230
	INT Diff = CompareGuids( &A->GetLinker()->Summary.Guid, &B->GetLinker()->Summary.Guid );
	if( Diff )
		return Diff;
#endif
	return A->GetLinkerIndex() - B->GetLinkerIndex();
}

void UClass::Destroy()
{
	guard(UClass::Destroy);

	// Empty arrays.
	//warning: Must be emptied explicitly in order for intrinsic classes
	// to not show memory leakage on exit.
	NetFields.Empty();
//scion rwd - remove some unused props for xbox build
#ifndef _XBOX
	Dependencies.Empty();
	DefaultPropText=TEXT("");
	PackageImports.Empty();
#endif

	// DELTA DEFAULTS
	if( !(ClassFlags & CLASS_TaggedDefaults) )
		ExitProperties( &Defaults(0), this );

	Defaults.Empty();

	Super::Destroy();
	unguard;
}

void UClass::PostLoad()
{
	guard(UClass::PostLoad);
	check(ClassWithin);
	Super::PostLoad();

	// Postload super.
	if( GetSuperClass() )
	{
		GetSuperClass()->ConditionalPostLoad();
	}

	unguardobj;
}
void UClass::Link( FArchive& Ar, UBOOL Props )
{
	guard(UClass::Link);
	Super::Link( Ar, Props );
	if( !GIsEditor )
	{
		NetFields.Empty();
#if ENGINE_VERSION<230
		ClassReps.Empty();
		for( TFieldIterator<UField> It(this); It; ++It )
#else
		ClassReps = SuperField ? GetSuperClass()->ClassReps : TArray<FRepRecord>();
		for( TFieldIterator<UField> It(this); It && It->GetOwnerClass()==this; ++It )
#endif
		{
			UProperty* P;
			UFunction* F;
			if( (P=Cast<UProperty>(*It))!=NULL )
			{
				if( P->PropertyFlags&CPF_Net )
				{
					NetFields.AddItem( *It );
					if( P->GetOuter()==this )
					{
						P->RepIndex = ClassReps.Num();
						for( INT i=0; i<P->ArrayDim; i++ )
							new(ClassReps)FRepRecord(P,i);
					}
				}
			}
			else if( (F=Cast<UFunction>(*It))!=NULL )
			{
				if( (F->FunctionFlags&FUNC_Net) && !F->GetSuperFunction() )
					NetFields.AddItem( *It );
			}
		}
		NetFields.Shrink();
		Sort( &NetFields(0), NetFields.Num() );
	}
	unguard;
}
void UClass::Serialize( FArchive& Ar )
{
	guard(UClass::Serialize);
	Super::Serialize( Ar );

//scion rwd - remove some unused props for xbox build
#ifdef _XBOX
	TArray<FDependency> Dependencies;
	TArray<FName> PackageImports;
	TArray<FName> HideCategories;
#endif

	Ar << ClassFlags;
	//jg -- Safely removed the ClassGuid
	if (Ar.Ver() < 139 && Ar.IsLoading())
	{
		FGuid ClassGuid;
		Ar << ClassGuid;
	}
	Ar << Dependencies << PackageImports;
	Ar << ClassWithin << ClassConfigName;
	Ar << HideCategories;

	// Defaults.
	if( Ar.IsLoading() )
	{
		check(GetPropertiesSize()>=sizeof(UObject));
		check(!GetSuperClass() || !(GetSuperClass()->GetFlags()&RF_NeedLoad));

		// DELTA DEFAULTS
		UBOOL UseTaggedDefaults = 1;

		ClassFlags |= CLASS_FullDefaults;

		// DELTA DEFAULTS - full defaults required for classes using per-object configs
		for( UClass* Parent=this; Parent && !(ClassFlags&CLASS_FullDefaults); Parent=Parent->GetSuperClass() )
		{
			if( Parent->ClassFlags & CLASS_PerObjectConfig )
			{
				ClassFlags |= CLASS_FullDefaults;
				break;
			}
		}		

		// DELTA DEFAULTS - full defaults required for classes using configs 
		// but only if config entries exist for the properties
		UBOOL bClassConfig=0;
		if( !(ClassFlags & CLASS_ConfigLoaded) )
		{
			for( UClass* Parent=this; Parent && !(ClassFlags&CLASS_FullDefaults); Parent=Parent->GetSuperClass() )
			{
				if( Parent->ClassFlags & CLASS_Config )
				{
					bClassConfig=1;
					break;
				}
			}
		}

		// DELTA DEFAULTS - full defaults required for classes using localized configs 
		// but only if localized config entries exist for the properties
		UBOOL bClassLocalized=0;
		if( !(ClassFlags & CLASS_LocalizedLoaded) )
		{
			for( UClass* Parent=this; Parent && !(ClassFlags&CLASS_FullDefaults); Parent=Parent->GetSuperClass() )
			{
				if( Parent->ClassFlags & CLASS_Localized )
				{
					bClassLocalized=1;
					break;
				}
			}
		}

		// force to full defaults
		if( (bClassConfig && 1/*UsesConfigEntries( this )*/) ||
			(bClassLocalized && 1/*UsesLocalizedEntries( this )*/) )
		{
            UseTaggedDefaults = 0;
		}

		if( this == UObject::StaticClass() || ClassFlags & CLASS_FullDefaults || GIsUCC || GIsEditor )
			UseTaggedDefaults = 0;		

		// DELTA DEFAULTS
		if( !(ClassFlags & CLASS_TaggedDefaults) )
		{
			if( !UseTaggedDefaults )
			{
				Defaults.Empty( GetPropertiesSize() );
				Defaults.Add( GetPropertiesSize() );
				GetDefaultObject()->InitClassDefaultObject( this );
				SerializeTaggedProperties( Ar, &Defaults(0), GetSuperClass() );
				GetDefaultObject()->LoadConfig();
				UC2NoteDefaultOverrides( this );
				GetDefaultObject()->LoadLocalized();
			}
			else
			{
				SerializeTaggedProperties( Ar, &Defaults(0), GetSuperClass(), this );
			}
		}
		ClassUnique = 0;
	}
	else if( Ar.IsSaving() )
	{
		check(Defaults.Num()==GetPropertiesSize());
		SerializeTaggedProperties( Ar, &Defaults(0), GetSuperClass() );
	}
	else
	{
		if( !(ClassFlags & CLASS_TaggedDefaults) )
		{
			check(Defaults.Num()==GetPropertiesSize());
			Defaults.CountBytes( Ar );
			SerializeBin( Ar, &Defaults(0), 0 );
		}
		else
		{
			// DELTA DEFAULTS
			GCTaggedDefaults( Ar );
		}
	}
	//jg -- Check to see if this version supports poolable classes and
	// strip out the old data since we aren't using that anymore
	if (Ar.Ver() == 141 && Ar.IsLoading())
	{
		_WORD PoolableInstances, MaxInstances;
		Ar << PoolableInstances << MaxInstances;
	}
	unguardobj;
}

/*-----------------------------------------------------------------------------
	UClass constructors.
-----------------------------------------------------------------------------*/

//
// Internal constructor.
//
UClass::UClass()
:	ClassWithin( UObject::StaticClass() )
{}

//
// Create a new UClass given its superclass.
//
UClass::UClass( UClass* InBaseClass )
:	UState( InBaseClass )
,	ClassWithin( UObject::StaticClass() )
{
	guard(UClass::UClass);
	if( GetSuperClass() )
	{
		ClassWithin = GetSuperClass()->ClassWithin;
		Defaults = GetSuperClass()->Defaults;
		Bind();		
	}
	unguardobj;
}

//
// UClass autoregistry constructor.
//warning: Called at DLL init time.
//
UClass::UClass
(
	ENativeConstructor,
	DWORD			InSize,
	DWORD			InClassFlags,
	UClass*			InSuperClass,
	UClass*			InWithinClass,
	FGuid			InGuid,
	const TCHAR*	InNameStr,
	const TCHAR*    InPackageName,
	const TCHAR*    InConfigName,
	DWORD			InFlags,
	void			(*InClassConstructor)(void*),
	void			(UObject::*InClassStaticConstructor)()
)
:	UState					( EC_NativeConstructor, InSize, InNameStr, InPackageName, InFlags, InSuperClass!=this ? InSuperClass : NULL )
,	ClassFlags				( InClassFlags | CLASS_Parsed | CLASS_Compiled )
,	ClassUnique				( 0 )
,	ClassWithin				( InWithinClass )
,	ClassConfigName			()
#ifndef _XBOX
,	Dependencies			()
,	PackageImports			()
#endif
,	Defaults				()
,	NetFields				()
,	ClassConstructor		( InClassConstructor )
,	ClassStaticConstructor	( InClassStaticConstructor )
,	bPrecached				(0)								//scion rwd - mark this class as not precached
{
#if PLATFORM_64BITS
	StashNativeConfigName( this, InConfigName );
#else
	*(const TCHAR**)&ClassConfigName = InConfigName;
#endif
}

// Called when statically linked.
UClass::UClass
(
	EStaticConstructor,
	DWORD			InSize,
	DWORD			InClassFlags,
	FGuid			InGuid,
	const TCHAR*	InNameStr,
	const TCHAR*    InPackageName,
	const TCHAR*    InConfigName,
	DWORD			InFlags,
	void			(*InClassConstructor)(void*),
	void			(UObject::*InClassStaticConstructor)()
)
:	UState					( EC_StaticConstructor, InSize, InNameStr, InPackageName, InFlags )
,	ClassFlags				( InClassFlags | CLASS_Parsed | CLASS_Compiled )
,	ClassUnique				( 0 )
,	ClassConfigName			()
#ifndef _XBOX
,	Dependencies			()
,	PackageImports			()
#endif
,	Defaults				()
,	NetFields				()
,	ClassConstructor		( InClassConstructor )
,	ClassStaticConstructor	( InClassStaticConstructor )
,	bPrecached				(0)								//scion rwd - mark this class as not precached
{
#if PLATFORM_64BITS
	StashNativeConfigName( this, InConfigName );
#else
	*(const TCHAR**)&ClassConfigName = InConfigName;
#endif
}

UBOOL CORE_API ShouldPrecacheClass(UClass *testClass)
{
	while (testClass != NULL)
	{
		if (testClass->ClassFlags & CLASS_AutoPrecache)
		{
			return 1;
		}
		testClass = testClass->GetSuperClass();
	}
	return 0;
}

void CORE_API CheckObjArrayPrecache(UArrayProperty *arrayProp,BYTE* Data,TArray<UObject*> &precacheList)
{
	UObjectProperty *objProp = Cast<UObjectProperty>(arrayProp->Inner);
	// make sure either the prop is marked precache
	// or the prop class is marked precache
	// or the prop class is a class
	if (objProp->PropertyFlags & CPF_AutoPrecache ||
		ShouldPrecacheClass(objProp->PropertyClass) ||
		objProp->IsA(UClassProperty::StaticClass()))
	{
		FArray *Array	= (FArray*)(Data + arrayProp->Offset);
		for (INT i = 0; i < Array->Num(); i++)
		{
			UObject *propObj = *(UObject**)((BYTE*)Array->GetData() + i * arrayProp->Inner->ElementSize);
			if (propObj != NULL)
			{
				//debugf(NAME_Precache,TEXT("- array %s [%d]: %s"),arrayProp->GetName(),i,propObj->GetName());
				precacheList.AddUniqueItem(propObj);
			}
		}
	}
}

void CORE_API CheckStrArrayPrecache(UArrayProperty *arrayProp,BYTE* Data,TArray<UObject*> &precacheList)
{
	FArray *Array = (FArray*)(Data + arrayProp->Offset);
	for (INT i = 0; i < Array->Num(); i++)
	{
		FString objStr = *(FString*)((BYTE*)Array->GetData() + i * arrayProp->Inner->ElementSize);
		if (objStr.Len() > 0)
		{
			UObject *obj = arrayProp->StaticLoadObject(UObject::StaticClass(), NULL, *objStr, NULL, LOAD_NoRemap|LOAD_NoWarn|LOAD_Quiet, NULL);
			if (obj != NULL)
			{
				precacheList.AddUniqueItem(obj);
			}
			else
			{
				// try to load the class
				obj = arrayProp->StaticLoadObject(UClass::StaticClass(), NULL, *objStr, NULL, LOAD_NoRemap|LOAD_NoWarn| LOAD_Quiet, NULL);
				if (obj != NULL &&
					((UClass*)obj)->bPrecached == 0)
				{
					precacheList.AddUniqueItem(obj);
				}
			}
		}
	}
}

void CORE_API CheckStructArrayPrecache(UArrayProperty *arrayProp,BYTE* Data,TArray<UObject*> &precacheList)
{
	FArray *Array = (FArray*)(Data + arrayProp->Offset);
	UStruct *precacheStruct = ((UStructProperty*)(arrayProp->Inner))->Struct;
	for (INT i = 0; i < Array->Num(); i++)
	{
		CheckStructPrecache(precacheStruct,(BYTE*)Array->GetData() + (i * precacheStruct->PropertiesSize),precacheList);
	}
}

void CORE_API CheckObjPrecache(UObjectProperty *objProp,BYTE* Data,TArray<UObject*> &precacheList)
{
	if (objProp->PropertyFlags & CPF_AutoPrecache ||
		ShouldPrecacheClass(objProp->PropertyClass) ||
		objProp->IsA(UClassProperty::StaticClass()) ||
		ShouldPrecacheClass(objProp->GetOwnerClass()) )
	{
		for (INT i = 0; i < objProp->ArrayDim; i++)
		{
			UObject *propObj = *(UObject**)(Data + objProp->Offset + (i * objProp->ElementSize));
			if (propObj != NULL)
			{
				precacheList.AddUniqueItem(propObj);
			}
		}
	}
}

void CORE_API CheckStrPrecache(UStrProperty *strProp,BYTE* Data,TArray<UObject*> &precacheList)
{
	for (INT i = 0; i < strProp->ArrayDim; i++)
	{
		FString objStr = *(FString*)(Data + strProp->Offset + (i * strProp->ElementSize));
		if (objStr.Len() > 0)
		{
			UObject *obj = strProp->StaticLoadObject(UObject::StaticClass(), NULL, *objStr, NULL, LOAD_NoRemap|LOAD_NoWarn|LOAD_Quiet, NULL);
			if (obj != NULL)
			{
				precacheList.AddUniqueItem(obj);
			}
			else
			{
				// try to load the class
				obj = strProp->StaticLoadObject(UClass::StaticClass(), NULL, *objStr, NULL, LOAD_NoRemap|LOAD_NoWarn|LOAD_Quiet, NULL);
				if (obj != NULL &&
					((UClass*)obj)->bPrecached == 0)
				{
					precacheList.AddUniqueItem(obj);
				}
			}
		}
	}
}

void CORE_API CheckStructPrecache(UStruct *structPrecache,BYTE* Data,TArray<UObject*> &precacheList)
{
	for (TFieldIterator<UProperty,CLASS_IsAUProperty> It(structPrecache); It; ++It)
	{
		// special handle dynamic arrays of objects/strings
		UArrayProperty *arrayProp = Cast<UArrayProperty>(*It);
		if (arrayProp != NULL)
		{
			if (arrayProp->Inner->IsA(UObjectProperty::StaticClass()))
			{
				CheckObjArrayPrecache(arrayProp,Data,precacheList);
			}
			else
			if (arrayProp->PropertyFlags & CPF_AutoPrecache &&
				arrayProp->Inner->IsA(UStrProperty::StaticClass()))
			{
				CheckStrArrayPrecache(arrayProp,Data,precacheList);
			}
			else
			if (arrayProp->Inner->IsA(UStructProperty::StaticClass()))
			{
				CheckStructArrayPrecache(arrayProp,Data,precacheList);
			}
		}
		else
		{
			// check normal object precache
			if (It->IsA(UObjectProperty::StaticClass()))
			{
				CheckObjPrecache((UObjectProperty*)(*It),Data,precacheList);
			}
			else
			// check normal string precache
			if (It->PropertyFlags & CPF_AutoPrecache &&
				It->IsA(UStrProperty::StaticClass()))
			{
				CheckStrPrecache((UStrProperty*)(*It),Data,precacheList);
			}
			// check for struct properties
			else
			if (It->IsA(UStructProperty::StaticClass()))
			{
				UStructProperty *structProp = (UStructProperty*)(*It);
				FName structName = structProp->GetFName();
				// skip some structs taht would never need precaching
				if (structName != NAME_Vector &&
					structName != NAME_Rotator &&
					structName != NAME_Color)
				{
					UStruct *precacheStruct = structProp->Struct;
					for (INT i = 0; i < structProp->ArrayDim; i++)
					{
						CheckStructPrecache(precacheStruct,Data + structProp->Offset + (i * precacheStruct->PropertiesSize),precacheList);
					}
				}
			}
		}
	}
}

//scion rwd - check if this class should precache
void UClass::Precache(UObject *src,TMap<UObject*,BYTE> *masterPrecacheList)
{
	guard(UClass::PreCache);

	// DELTA DEFAULTS
	// precaching will load full default object for all classes
	return;

	debugf( NAME_Precache, TEXT("CLASS=%s  OBJ=%s"), GetFullName(), src ? src->GetFullName() : TEXT("") );

	BYTE *srcData = (BYTE*)src;
	if (srcData != NULL ||
		bPrecached == 0)
	{
		if (srcData == NULL)
		{
			//debugf(NAME_Precache,TEXT("Precaching class defaults: %s"),GetName());
			srcData = GetDefaultsSafe();
			// mark as precached immediately, to avoid recursive hell
			bPrecached = 1;
		}
		TArray<UObject*> precacheList;
		// examine this classes properties and check for any that should be precached
		CheckStructPrecache(this,srcData,precacheList);
		// precache each found object
		for (INT i = 0; i < precacheList.Num(); i++)
		{
			if (masterPrecacheList != NULL)
			{
				// if not already precached,
				if (!masterPrecacheList->FindRef(precacheList(i)))
				{
					// add to list
					masterPrecacheList->Add(precacheList(i),1);
					// and precache
					precacheList(i)->Precache(NULL,masterPrecacheList);					
				}
			}
			else
			{
				precacheList(i)->Precache(NULL,masterPrecacheList);
			}
		}
	}
	unguardf((TEXT("Precache class %s"),GetName()));
}

IMPLEMENT_CLASS(UClass);

/*-----------------------------------------------------------------------------
	FDependency.
-----------------------------------------------------------------------------*/

//
// FDepdendency inlines.
//
FDependency::FDependency()
{}
FDependency::FDependency( UClass* InClass, UBOOL InDeep )
:	Class( InClass )
,	Deep( InDeep )
#ifndef _XBOX
,	ScriptTextCRC( Class ? Class->GetScriptTextCRC() : 0 )
#endif
{}
UBOOL FDependency::IsUpToDate()
{
	guard(FDependency::IsUpToDate);
	check(Class!=NULL);
#ifdef _XBOX
	return 1;
#else
	return Class->GetScriptTextCRC()==ScriptTextCRC;
#endif
	unguard;
}
CORE_API FArchive& operator<<( FArchive& Ar, FDependency& Dep )
{
	return Ar << Dep.Class << Dep.Deep << Dep.ScriptTextCRC;
}

/*-----------------------------------------------------------------------------
	FLabelEntry.
-----------------------------------------------------------------------------*/

FLabelEntry::FLabelEntry( FName InName, INT iInCode )
:	Name	(InName)
,	iCode	(iInCode)
{}
CORE_API FArchive& operator<<( FArchive& Ar, FLabelEntry &Label )
{
	Ar << Label.Name;
	Ar << Label.iCode;
	return Ar;
}

/*-----------------------------------------------------------------------------
	UStruct implementation.
-----------------------------------------------------------------------------*/


//
// Serialize an expression to an archive.
// Returns expression token.
//
EExprToken UStruct::SerializeExpr( INT& iCode, FArchive& Ar )
{
	EExprToken Expr=(EExprToken)0;
	guard(SerializeExpr);
	#define XFER(T) {Ar << *(T*)&Script(iCode); iCode += sizeof(T); }

	#define XFERNAME() {if((GUglyHackFlags&64)==0) XFER(FName) else XFER(INT)}

#if !SERIAL_POINTER_INDEX
	#define XFERPTR(T) if ( (GUglyHackFlags&64) == 0 ) XFER(T) else XFER(INT)
#else
    #define XFERPTR(T) \
    { \
        T x = NULL; \
        if (!Ar.IsLoading()) \
            x = (T) UObject::SerialIndexPointer(*(INT*)&Script(iCode)); \
        Ar << x; \
        *(INT*)&Script(iCode) = UObject::SerialPointerIndex(x); \
        iCode += sizeof (INT); \
    }
#endif

	//DEBUGGER: To mantain compatability between debug and non-debug clases
	#define HANDLE_OPTIONAL_DEBUG_INFO() \
    if (iCode < Script.Num()) \
    { \
	    int RemPos = Ar.Tell(); \
	    int OldiCode = iCode;	\
	    XFER(BYTE); \
	    int NextCode = Script(iCode-1); \
	    int GVERSION = -1;	\
	    if ( NextCode == EX_DebugInfo ) \
	    {	\
		    XFER(INT); \
		    GVERSION = *(INT*)&Script(iCode-sizeof(INT));	\
	    }	\
	    iCode = OldiCode;	\
	    Ar.Seek( RemPos );	\
	    if ( GVERSION == 100 )	\
		    SerializeExpr( iCode, Ar );	\
    } \

	// Get expr token.
	XFER(BYTE);
	Expr = (EExprToken)Script(iCode-1);
	if( Expr >= EX_FirstNative )
	{
		// Native final function with id 1-127.
		while( SerializeExpr( iCode, Ar ) != EX_EndFunctionParms );
		HANDLE_OPTIONAL_DEBUG_INFO(); //DEBUGGER
	}
	else if( Expr >= EX_ExtendedNative )
	{
		// Native final function with id 256-16383.
		XFER(BYTE);
		while( SerializeExpr( iCode, Ar ) != EX_EndFunctionParms );
		HANDLE_OPTIONAL_DEBUG_INFO(); //DEBUGGER
	}
	else switch( Expr )
	{
		case EX_PrimitiveCast:
		{
			// A type conversion.
			XFER(BYTE); //which kind of conversion
			SerializeExpr( iCode, Ar );
			break;
		}
		case EX_Let:
		case EX_LetBool:
		case EX_LetDelegate:
		{
			SerializeExpr( iCode, Ar ); // Variable expr.
			SerializeExpr( iCode, Ar ); // Assignment expr.
			break;
		}
		case EX_Jump:
		{
			XFER(_WORD); // Code offset.
			break;
		}
		case EX_LocalVariable:
		case EX_InstanceVariable:
		case EX_DefaultVariable:
		//epic moelfke: special bytecode for out params so they can be seperated to really be pass by ref
		case EX_LocalOutVariable:
		{
			XFERPTR(UProperty*);
			break;
		}
		case EX_DebugInfo:
		{
			XFER(INT);	// Version
			XFER(INT);	// Line number
			XFER(INT);	// Character pos
			XFER(BYTE); // OpCode
			break;
		}
		case EX_BoolVariable:
		{
			//scion rwd - tim's fix for bool prop serialization bug
             SerializeExpr(iCode,Ar);
			 break;
		}
		case EX_Nothing:
		case EX_EndFunctionParms:
		case EX_IntZero:
		case EX_IntOne:
		case EX_True:
		case EX_False:
		case EX_NoObject:
		case EX_Self:
		case EX_IteratorPop:
		case EX_Stop:
		case EX_IteratorNext:
		{
			break;
		}
		//epic moelfke: destroy an unused return value from a function
		case EX_DestroyReturnValue:
		{
			XFERPTR(UProperty*); // the return value property
			break;
		}
		case EX_Return:
		{
			SerializeExpr( iCode, Ar ); // Return expression.
			break;
		}
		case EX_FinalFunction:
		{
			XFERPTR(UStruct*); // Stack node.
			while( SerializeExpr( iCode, Ar ) != EX_EndFunctionParms ); // Parms.
			HANDLE_OPTIONAL_DEBUG_INFO(); //DEBUGGER
			break;
		}
		case EX_VirtualFunction:
		case EX_GlobalFunction:
		{
			XFERNAME(); // Virtual function name.
			while( SerializeExpr( iCode, Ar ) != EX_EndFunctionParms ); // Parms.
			HANDLE_OPTIONAL_DEBUG_INFO(); //DEBUGGER
			break;
		}
		case EX_DelegateFunction:
		{
			XFERPTR(UProperty*);	// Delegate property
			XFERNAME();				// Delegate function name (in case the delegate is NULL)
			break;
		}
		case EX_FinalDelegate:
		{
			XFERPTR(UProperty*);	// Delegate property
			XFERNAME();				// Delegate function name (in case the delegate is NULL)
			XFERPTR(UStruct*);		// Stack Node
			break;
		}
		case EX_NativeParm:
		{
			XFERPTR(UProperty*);
			break;
		}
		case EX_ClassContext:
		case EX_Context:
		{
			SerializeExpr( iCode, Ar ); // Object expression.
			XFER(_WORD); // Code offset for NULL expressions.
			XFER(BYTE); // Zero-fill size if skipped.
			SerializeExpr( iCode, Ar ); // Context expression.
			break;
		}
		case EX_ArrayElement:
		case EX_DynArrayElement:
		{
			SerializeExpr( iCode, Ar ); // Index expression.
			SerializeExpr( iCode, Ar ); // Base expression.
			break;
		}
		case EX_DynArrayLength:
		{
			SerializeExpr( iCode, Ar ); // Base expression.
			break;
		}
		case EX_DynArrayInsert:
		case EX_DynArrayRemove:
		{
			SerializeExpr( iCode, Ar ); // Base expression
			SerializeExpr( iCode, Ar ); // Index
			SerializeExpr( iCode, Ar ); // Count
			break;
 		}
		//scion rwd - array shuffle
		case EX_DynArrayShuffle:
		{
			SerializeExpr(iCode,Ar);
			break;
		}
		//epic moelfke: append an array to another array of the same type
		case EX_DynArrayAppendArray:
		{
			SerializeExpr(iCode, Ar); // base expression
			SerializeExpr(iCode, Ar); // array to append
			break;
		}
#if 1 //scion rwd - merged in tertiary operator support from Lucas Alonso of digitalo
		case EX_Conditional:
		{
			SerializeExpr( iCode, Ar ); // Boolean Expr
			XFER(_WORD);    // Skip
			SerializeExpr( iCode, Ar ); // Result Expr 1;
			XFER(_WORD);    // Skip2
			SerializeExpr( iCode, Ar ); // Result Expr 2;
			break;
		}
#endif
		case EX_New:
		{
			SerializeExpr( iCode, Ar ); // Parent expression.
			SerializeExpr( iCode, Ar ); // Name expression.
			SerializeExpr( iCode, Ar ); // Flags expression.
			SerializeExpr( iCode, Ar ); // Class expression.
			break;
		}
		case EX_IntConst:
		{
			XFER(INT);
			break;
		}
		case EX_FloatConst:
		{
			XFER(FLOAT);
			break;
		}
		case EX_StringConst:
		{
			do XFER(BYTE) while( Script(iCode-1) );
			break;
		}
		case EX_UnicodeStringConst:
		{
			do XFER(_WORD) while( Script(iCode-1) );
			break;
		}
		case EX_ObjectConst:
		{
			XFERPTR(UObject*);
			break;
		}
		case EX_NameConst:
		{
			XFERNAME();
			break;
		}
		case EX_RotationConst:
		{
			XFER(INT); XFER(INT); XFER(INT);
			break;
		}
		case EX_VectorConst:
		{
			XFER(FLOAT); XFER(FLOAT); XFER(FLOAT);
			break;
		}
		//scion rwd - added missing support for rng()
		 case EX_RangeConst:
			 {
				 XFER(FLOAT); XFER(FLOAT);
				 break;
			 }
		case EX_ByteConst:
		case EX_IntConstByte:
		{
			XFER(BYTE);
			break;
		}
		case EX_MetaCast:
		{
			XFERPTR(UClass*);
			SerializeExpr( iCode, Ar );
			break;
		}
		case EX_DynamicCast:
		{
			XFERPTR(UClass*);
			SerializeExpr( iCode, Ar );
			break;
		}
		case EX_JumpIfNot:
		{
			XFER(_WORD); // Code offset.
			SerializeExpr( iCode, Ar ); // Boolean expr.
			break;
		}
		case EX_Iterator:
		{
			SerializeExpr( iCode, Ar ); // Iterator expr.
			XFER(_WORD); // Code offset.
			break;
		}
		case EX_Switch:
		{
			XFER(BYTE); // Value size.
			SerializeExpr( iCode, Ar ); // Switch expr.
			break;
		}
		case EX_Assert:
		{
			XFER(_WORD); // Line number.
			SerializeExpr( iCode, Ar ); // Assert expr.
			break;
		}
		case EX_Case:
		{
			_WORD W;
//			_WORD* W=(_WORD*)&Script(iCode);
			XFER(_WORD); // Code offset.
			appMemcpy(&W, &Script(iCode-sizeof(_WORD)), sizeof(_WORD));
			if( W != MAXWORD )
				SerializeExpr( iCode, Ar ); // Boolean expr.
			break;
		}
		case EX_LabelTable:
		{
			check((iCode&3)==0);
			for( ; ; )
			{
				FLabelEntry* E = (FLabelEntry*)&Script(iCode);
				XFER(FLabelEntry);
				if( E->Name == NAME_None )
					break;
			}
			break;
		}
		case EX_GotoLabel:
		{
			SerializeExpr( iCode, Ar ); // Label name expr.
			break;
		}
		case EX_Skip:
		{
			XFER(_WORD); // Skip size.
			SerializeExpr( iCode, Ar ); // Expression to possibly skip.
			break;
		}
		case EX_StructCmpEq:
		case EX_StructCmpNe:
		{
			XFERPTR(UStruct*); // Struct.
			SerializeExpr( iCode, Ar ); // Left expr.
			SerializeExpr( iCode, Ar ); // Right expr.
			break;
		}
		case EX_StructMember:
		{
			XFERPTR(UProperty*); // Property.
			SerializeExpr( iCode, Ar ); // Inner expr.
			break;
		}
		case EX_DelegateProperty:
		{
			XFERNAME();	// Name of function we're assigning to the delegate.
			break;
		}
		default:
		{
			// This should never occur.
			appErrorf( TEXT("Bad expr token %02x"), Expr );
			break;
		}
	}
	return Expr;
	#undef XFER
	#undef XFERPTR
	#undef XFERNAME
	unguardf(( TEXT("(%02X)"), Expr ));
}

void UStruct::PostLoad()
{
	guard(UStruct::PostLoad);
	Super::PostLoad();
	unguard;
}

/*-----------------------------------------------------------------------------
	UFunction.
-----------------------------------------------------------------------------*/

UFunction::UFunction( UFunction* InSuperFunction )
: UStruct( InSuperFunction )
{
}

void UFunction::Serialize( FArchive& Ar )
{
	guard(UFunction::Serialize);
	Super::Serialize( Ar );

	// Function info.
	Ar << iNative;
	Ar << OperPrecedence;
	Ar << FunctionFlags;

	// Replication info.
	if( FunctionFlags & FUNC_Net )
		Ar << RepOffset;

	// Precomputation.
	if( Ar.IsLoading() )
	{
		NumParms          = 0;
		ParmsSize         = 0;
		ReturnValueOffset = MAXWORD;
		for( UProperty* Property=Cast<UProperty>(Children); Property && (Property->PropertyFlags & CPF_Parm); Property=Cast<UProperty>(Property->Next) )
		{
			NumParms++;
			ParmsSize = Property->Offset + Property->GetSize();
			if( Property->PropertyFlags & CPF_ReturnParm )
				ReturnValueOffset = Property->Offset;
		}
	}

	if (!Ar.IsLoading() ||
		Ar.Ver() >= 138)
	{
		Ar << FriendlyName;
	}
	else
	{
		FriendlyName = GetFName();
	}
	unguard;
}
void UFunction::PostLoad()
{
	guard(UFunction::PostLoad);
	Super::PostLoad();
	unguard;
}
UProperty* UFunction::GetReturnProperty()
{
	guard(UFunction::GetReturnProperty);
	for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(this); It && (It->PropertyFlags & CPF_Parm); ++It )
		if( It->PropertyFlags & CPF_ReturnParm )
			return *It;
	return NULL;
	unguard;
}
void UFunction::Bind()
{
	guard(UFunction::Bind);
	if( !(FunctionFlags & FUNC_Native) )
	{
		// Use UnrealScript processing function.
		check(iNative==0);
		Func = &UObject::ProcessInternal;
	}
	else if( iNative != 0 )
	{
		// Find hardcoded native.
		check(iNative<EX_Max);
		check(GNatives[iNative]!=0);
		Func = GNatives[iNative];
	}
	else
	{
		// Find dynamic native.
		TCHAR Proc[256];
		appSprintf( Proc, TEXT("int%sexec%s"), GetOwnerClass()->GetNameCPP(), GetName() );
		UPackage* ClassPackage = GetOwnerClass()->GetOuterUPackage();
		Native* Ptr = (Native*)ClassPackage->GetDllExport( Proc, 1 );
		if( Ptr )
			Func = *Ptr;
	}
	unguard;
}
void UFunction::Link( FArchive& Ar, UBOOL Props )
{
	guard(UFunction::Link);
	Super::Link( Ar, Props );
	unguard;
}
IMPLEMENT_CLASS(UFunction);

/*-----------------------------------------------------------------------------
	UConst.
-----------------------------------------------------------------------------*/

UConst::UConst( UConst* InSuperConst, const TCHAR* InValue )
:	UField( InSuperConst )
,	Value( InValue )
{}
void UConst::Serialize( FArchive& Ar )
{
	guard(UConst::Serialize);
	Super::Serialize( Ar );
	Ar << Value;
	unguard;
}
IMPLEMENT_CLASS(UConst);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

