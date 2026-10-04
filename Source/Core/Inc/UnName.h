/*=============================================================================
	UnName.h: Unreal global name types.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

/*----------------------------------------------------------------------------
	Definitions.
----------------------------------------------------------------------------*/

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

//scion jg -- Number of FNames to be converted in the buffer
#define MAX_FNAME_CONVERT 4096
#define FNAME_CONVERT_SIZE (MAX_FNAME_CONVERT * NAME_SIZE)

//
// A global name, as stored in the global name table.
//
//scion jg -- Changed the actual string data to be private to prevent direct
// access. All access must go through the public helper method which handles
// the conversion to TCHAR. The name data is now stored as an ANSI string
// and converted to TCHAR on demand, using the FNameConvert structure. The
// FNameConvert structure provides a conversion queue to allow pointers to
// converted data to be used without allocating space.
struct FNameEntry
{
	// Variables.
	NAME_INDEX	Index;				// Index of name in hash.
	DWORD		Flags;				// RF_TagImp, RF_TagExp, RF_Native.
	FNameEntry*	HashNext;			// Pointer to the next entry in this hash bin's linked list.

private:
	// Absolutely must come last in the variable list since it
	// isn't actually this size
	// The name string.
	ANSICHAR	Name[NAME_SIZE];	// Name, variable-sized.
public:

#ifndef _XBOX
#ifdef _DEBUG
	struct FNameConvert
	{
		// The next spot to use to hold the conversion data
		DWORD CurrentOffset;
		// This is a large circular buffer that string are converted in
		UNICHAR TempFName[FNAME_CONVERT_SIZE];

		// Zero the contents
		FNameConvert()
		{
			appMemzero(this,sizeof(FNameConvert));
		}

		// Returns the current location to convert the string at
		FORCEINLINE UNICHAR* GetLocation(DWORD StrLen)
		{
			// Check for wrapping
			if (CurrentOffset + StrLen > FNAME_CONVERT_SIZE)
			{
				CurrentOffset = 0;
			}
			// Get the pointer that we are going to write to
			UNICHAR* szCurr = &TempFName[CurrentOffset];
			// Now move the position in the queue
			CurrentOffset += StrLen;
			return szCurr;
		}
	};

	// The one and only conversion queue
	static FNameConvert ConversionQueue;
	// Returns the string in WCHAR form
	CORE_API const UNICHAR* GetUnicodeName(void);
#endif
#endif

	// Functions.
	CORE_API friend FArchive& operator<<( FArchive& Ar, FNameEntry& E );
	CORE_API friend FNameEntry* AllocateNameEntry( const ANSICHAR* Name, DWORD Index, DWORD Flags, FNameEntry* HashNext );

	// Returns the ansi string
	FORCEINLINE CORE_API const ANSICHAR* GetRawName(void) { return Name; }
	// Returns the string in FString form
	FORCEINLINE CORE_API void GetName(FString& String)
	{
		String = Name;
	}
	FORCEINLINE CORE_API const TCHAR* GetName(void)
	{
		return Name;
	}
};

template <> struct TTypeInfo<FNameEntry*> : public TTypeInfoBase<FNameEntry*>
{
	static UBOOL NeedsDestructor() {return 0;}
};

/*----------------------------------------------------------------------------
	FName.
----------------------------------------------------------------------------*/

//
// Public name, available to the world.  Names are stored as WORD indices
// into the name table and every name in Unreal is stored once
// and only once in that table.  Names are case-insensitive.
//
#define checkName checkSlow
class CORE_API FName 
{
public:
	// Accessors.
	const TCHAR* operator*() const
	{
		checkName(Index < Names.Num());
		checkName(Names(Index));
		return Names(Index)->GetName();
	}
	NAME_INDEX GetIndex() const
	{
		checkName(Index < Names.Num());
		checkName(Names(Index));
		return Index;
	}
	DWORD GetFlags() const
	{
		checkName(Index < Names.Num());
		checkName(Names(Index));
		return Names(Index)->Flags;
	}
	void SetFlags( DWORD Set ) const
	{
		checkName(Index < Names.Num());
		checkName(Names(Index));
		Names(Index)->Flags |= Set;
	}
	void ClearFlags( DWORD Clear ) const
	{
		checkName(Index < Names.Num());
		checkName(Names(Index));
		Names(Index)->Flags &= ~Clear;
	}
	UBOOL operator==( const FName& Other ) const
	{
		return Index==Other.Index;
	}
	UBOOL operator!=( const FName& Other ) const
	{
		return Index!=Other.Index;
	}
	UBOOL IsValid()
	{
		return Index>=0 && Index<Names.Num() && Names(Index)!=NULL;
	}

	void GetName(FString& String) const
	{
		checkName(Index < Names.Num());
		checkName(Names(Index));
		Names(Index)->GetName(String);
	}

	FORCEINLINE TCHAR* GetName(void)
	{
		checkName(Index < Names.Num());
		checkName(Names(Index));
		return (TCHAR*)Names(Index)->GetRawName();
	}

	// Constructors.
	FName( enum EName N )
	: Index( N )
	{}
	FName()
	{}
	FName( const TCHAR* Name, EFindName FindType=FNAME_Add );
    void Construct( const ANSICHAR* Name, EFindName FindType ); // sjs

	// Name subsystem.
	static void StaticInit();
	static void StaticExit();
	static void DeleteEntry( int i );
	static void DisplayHash( class FOutputDevice& Ar );
	static void Hardcode( FNameEntry* AutoName );

	// Name subsystem accessors.
	static const TCHAR* SafeString( EName Index )
	{
		return( Initialized && (Index < Names.Num()) ? Names(Index)->GetName() : TEXT("Uninitialized") );
	}
	static UBOOL SafeSuppressed( EName Index )
	{
	    // gam ---
		return( Initialized && (Index < Names.Num()) && (Names(Index)->Flags & 0x00001000) );
		// --- gam
	}
	static int GetMaxNames()
	{
		return Names.Num();
	}
	static FNameEntry* GetEntry( int i )
	{
		return Names(i);
	}
	static UBOOL GetInitialized()
	{
		return Initialized;
	}

private:
	// Name index.
	NAME_INDEX Index;

	// Static subsystem variables.
	static TArray<FNameEntry*>	Names;			 // Table of all names.
	static TArray<_WORD>        Available;       // Indices of available names.
	static FNameEntry*			NameHash[4096];  // Hashed names.
	static UBOOL				Initialized;	 // Subsystem initialized.
};
inline DWORD GetTypeHash( const FName N )
{
	return N.GetIndex();
}

/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/

