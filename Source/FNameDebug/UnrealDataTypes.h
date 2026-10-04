/// 
/// Copyright © 2002 Epic Games. All right reserved.
/// 
/// 
/// <author>Joe Graf</author>
/// 

#ifndef _UNREAL_DATA_TYPES_H
#define _UNREAL_DATA_TYPES_H

#define CORE_API
#define UBOOL BOOL
#define SBYTE BYTE
#define SWORD WORD
#define _WORD WORD
#define FLOAT float

#define E_NoInit -1
#define NAME_None 0

class FArchive;

//
// Base dynamic array.
//
class CORE_API FArray
{
public:
	void* GetData();
	const void* GetData() const;
	void SetData( void* InData, DWORD Size );
	UBOOL IsValidIndex( INT i ) const;
	INT Num() const;
	void InsertZeroed( INT Index, INT Count, INT ElementSize );
	void Insert( INT Index, INT Count, INT ElementSize );
	INT Add( INT Count, INT ElementSize );
	INT AddZeroed( INT ElementSize, INT n=1 );
	void Shrink( INT ElementSize );
	void Empty( INT ElementSize, INT Slack=0 );
	FArray() : ArrayNum( 0 ),ArrayMax( 0 ),Data	( NULL ){}
	FArray( INT ) {}
	~FArray() {}
	void CountBytes( FArchive& Ar, INT ElementSize );
	void Remove( INT Index, INT Count, INT ElementSize );
public:
	void Realloc( INT ElementSize );
	FArray( INT InNum, INT ElementSize ) : ArrayNum( InNum ),ArrayMax( InNum ),
		Data( NULL  ) {}

	void* Data;
	INT	  ArrayNum;
	INT	  ArrayMax;
};

//
// Templated dynamic array.
//
template< class T > class TArray : public FArray
{
public:
	typedef T ElementType;
	TArray() : FArray() {}
	TArray( INT InNum )	: FArray( InNum, sizeof(T) ) {}
	TArray( const TArray& Other ) : FArray( Other.ArrayNum, sizeof(T) ) {}
	TArray( DWORD ) : FArray( E_NoInit ) {}
	~TArray() {}
    T& operator()( INT i );
	const T& operator()( INT i ) const;
	T Pop();
	T& Last( INT c=0 );
	const T& Last( INT c=0 ) const;
	void Shrink();
	UBOOL FindItem( const T& Item, INT& Index ) const;
	INT FindItemIndex( const T& Item ) const;
	friend FArchive& operator<<( FArchive& Ar, TArray& A );
	void CountBytes( FArchive& Ar );
	void SetData( void* InData, DWORD Size );

	// Add, Insert, Remove, Empty interface.
	INT Add( INT n=1 );
	void Insert( INT Index, INT Count=1 );
	void InsertZeroed( INT Index, INT Count=1 );
	void Remove( INT Index, INT Count=1 );
	void Empty( INT Slack=0 );

	// Functions dependent on Add, Remove.
	TArray& operator+( const TArray& Other );
	TArray& operator+=( const TArray& Other );
	TArray& operator=( const TArray& Other );
	INT AddItem( const T& Item );
	INT AddZeroed( INT n=1 );
	INT AddUniqueItem( const T& Item );
	INT RemoveItem( const T& Item );

	// Iterator.
	class TIterator
	{
	public:
		TIterator( TArray<T>& InArray ) : Array(InArray), Index(-1) { ++*this;      }
		void operator++()      { ++Index;                                           }
		void RemoveCurrent()   { Array.Remove(Index--); }
		INT GetIndex()   const { return Index;                                      }
		operator UBOOL() const { return Index < Array.Num();                        }
		T& operator*()   const { return Array(Index);                               }
		T* operator->()  const { return &Array(Index);                              }
		T& GetCurrent()  const { return Array( Index );                             }
		T& GetPrev()     const { return Array( Index ? Index-1 : Array.Num()-1 );   }
		T& GetNext()     const { return Array( Index<Array.Num()-1 ? Index+1 : 0 ); }
	private:
		TArray<T>& Array;
		INT Index;
	};
};

//
// A dynamically sizeable string.
//
class CORE_API FString : public TArray<WCHAR>
{
public:
	FString() : TArray<WCHAR>() {}
	FString( const FString& Other )	: TArray<WCHAR>( Other.ArrayNum ) {}
	FString( const WCHAR* In ) : TArray<WCHAR>( 0 )	{ }
	FString( DWORD ) : TArray<WCHAR>( E_NoInit ) {}
	explicit FString( BYTE   Arg, INT Digits=1 );
	explicit FString( SBYTE  Arg, INT Digits=1, INT Dummy = 0 );
	explicit FString( _WORD  Arg, INT Digits=1, DWORD Dummy = 0 );
	explicit FString( SWORD  Arg, INT Digits=1, INT Dummy = 0 );
	explicit FString( INT    Arg, INT Digits=1 );
	explicit FString( DWORD  Arg, INT Digits=1 );
	explicit FString( float  Arg, INT Digits=1, INT RightDigits=0, UBOOL LeadZero=1 );
	explicit FString( double Arg, INT Digits=1, INT RightDigits=0, UBOOL LeadZero=1 );
	FString& operator=( const WCHAR* Other );
	FString& operator=( const FString& Other );
	~FString() { }
	void Empty();
	void Shrink();
	const WCHAR* operator*() const;
	operator UBOOL() const;
	TArray<WCHAR>& GetCharArray();
	FString& operator+=( const WCHAR* Str );
	FString& operator+=( const FString& Str );
	FString operator+( const WCHAR* Str );
	FString operator+( const FString& Str );
	FString& operator*=( const WCHAR* Str );
	FString& operator*=( const FString& Str );
	FString operator*( const WCHAR* Str ) const;
	FString operator*( const FString& Str ) const;
	UBOOL operator<=( const WCHAR* Other ) const;
	UBOOL operator<( const WCHAR* Other ) const;
	UBOOL operator>=( const WCHAR* Other ) const;
	UBOOL operator>( const WCHAR* Other ) const;
	UBOOL operator==( const WCHAR* Other ) const;
	UBOOL operator==( const FString& Other ) const;
	UBOOL operator!=( const WCHAR* Other ) const;
	UBOOL operator!=( const FString& Other ) const;
	INT Len() const;
	FString Left( INT Count ) const;
	FString LeftChop( INT Count ) const;
	FString Right( INT Count ) const;
	FString Mid( INT Start, INT Count=-1 ) const;
	FString RemoveSpaces() const;
	INT InStr( const WCHAR* SubStr, UBOOL Right=0 ) const;
	INT InStr( const FString& SubStr, UBOOL Right=0 ) const;
	UBOOL Split( const FString& InS, FString* LeftS, FString* RightS, UBOOL Right=0 ) const;
	FString Caps() const;
	FString Locs() const;
	FString LeftPad( INT ChCount );
	FString RightPad( INT ChCount );
	static FString Printf( const WCHAR* Fmt, ... );
	static FString Chr( WCHAR Ch );
	CORE_API friend FArchive& operator<<( FArchive& Ar, FString& S );
	// Breaks up this delimited string into elements of a string array.
	void ParseIntoArray( const WCHAR* pchDelim, TArray<FString>* InArray);
private:
	FString( INT InCount, const WCHAR* InSrc ) : TArray<WCHAR>( 0 ) {}
};

// Maximum size of name.
enum {NAME_SIZE	= 64};

// Name index.
typedef INT NAME_INDEX;

// Enumeration for finding name.
enum EFindName
{
	FNAME_Find,			// Find a name; return 0 if it doesn't exist.
	FNAME_Add,			// Find a name or add it if it doesn't exist.
	FNAME_Intrinsic,	// Find a name or add it intrinsically if it doesn't exist.
};

/*----------------------------------------------------------------------------
	FNameEntry.
----------------------------------------------------------------------------*/

//
// A global name, as stored in the global name table.
//
struct FNameEntry
{
	// Variables.
	NAME_INDEX	Index;				// Index of name in hash.
	DWORD		Flags;				// RF_TagImp, RF_TagExp, RF_Native.
	FNameEntry*	HashNext;			// Pointer to the next entry in this hash bin's linked list.

	// The name string.
	WCHAR		Name[NAME_SIZE];	// Name, variable-sized.

	// Functions.
	CORE_API friend FArchive& operator<<( FArchive& Ar, FNameEntry& E );
	CORE_API friend FNameEntry* AllocateNameEntry( const WCHAR* Name, DWORD Index, DWORD Flags, FNameEntry* HashNext );
};

/*----------------------------------------------------------------------------
	FName.
----------------------------------------------------------------------------*/

//
// Public name, available to the world.  Names are stored as WORD indices
// into the name table and every name in Unreal is stored once
// and only once in that table.  Names are case-insensitive.
//
class CORE_API FName 
{
public:
	// Accessors.
	const WCHAR* operator*() const;
	NAME_INDEX GetIndex() const;
	DWORD GetFlags() const;
	void SetFlags( DWORD Set ) const;
	void ClearFlags( DWORD Clear ) const;
	UBOOL operator==( const FName& Other ) const;
	UBOOL operator!=( const FName& Other ) const;
	UBOOL IsValid();

	// Constructors.
	FName( enum EName N ) : Index( N ) {}
	FName()	{}
	FName( const WCHAR* Name, EFindName FindType=FNAME_Add ) {}

	// Name subsystem.
	static void StaticInit();
	static void StaticExit();
	static void DeleteEntry( int i );
	static void DisplayHash( class FOutputDevice& Ar );
	static void Hardcode( FNameEntry* AutoName );

	// Name subsystem accessors.
	static const WCHAR* SafeString( EName Index );
	static UBOOL SafeSuppressed( EName Index );
	static int GetMaxNames();
	static FNameEntry* GetEntry( int i );
	static UBOOL GetInitialized();

public:
	// Name index.
	NAME_INDEX Index;

	TArray<FNameEntry*>* pNameArray;

	// Static subsystem variables.
	static TArray<FNameEntry*>	Names;			 // Table of all names.
	static TArray<INT>          Available;       // Indices of available names.
	static FNameEntry*			NameHash[4096];  // Hashed names.
	static UBOOL				Initialized;	 // Subsystem initialized.
};

// This is the offset of the FName in an object
#define UOBJECT_FNAME_OFFSET 36

#endif // _UNREAL_DATA_TYPES_H