/*=============================================================================
	UnName.cpp: Unreal global name code.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "CorePrivate.h"

// Only declared as a friend of FNameEntry, which ordinary lookup does not find (engine-ut2004 does the same).
CORE_API FNameEntry* AllocateNameEntry( const ANSICHAR* Name, DWORD Index, DWORD Flags, FNameEntry* HashNext );

/*-----------------------------------------------------------------------------
	FName statics.
-----------------------------------------------------------------------------*/

// Static variables.
UBOOL				FName::Initialized = 0;
FNameEntry*			FName::NameHash[4096];
TArray<FNameEntry*>	FName::Names;
TArray<_WORD>       FName::Available;

#ifndef _XBOX
#ifdef _DEBUG
//scion jg -- For converting names back to TCHAR
FNameEntry::FNameConvert FNameEntry::ConversionQueue;
// JG - Added for debugging FName support
#endif
#endif

/*-----------------------------------------------------------------------------
	FName debugging hooks.
-----------------------------------------------------------------------------*/

#ifndef _XBOX
#ifdef _DEBUG
#include "FNameDebug.h"
extern FNameDebug g_FNameDebug;
#endif
#endif
// JG - End additions

/*-----------------------------------------------------------------------------
	FName implementation.
-----------------------------------------------------------------------------*/
#if __MWERKS__
#pragma auto_inline off
#endif

//
// Hardcode a name.
//
void FName::Hardcode( FNameEntry* AutoName )
{
	guard(FName::Hardcode);

	// Add name to name hash.
	INT iHash          = appStrihash(AutoName->GetRawName()) & (ARRAY_COUNT(NameHash)-1);
	AutoName->HashNext = NameHash[iHash];
	NameHash[iHash]    = AutoName;

	// Expand the table if needed.
	for( INT i=Names.Num(); i<=AutoName->Index; i++ )
		Names.AddItem( NULL );

	// Add name to table.
	if( Names(AutoName->Index) )
		appErrorf( TEXT("Hardcoded name %i was duplicated"), AutoName->Index );
	Names(AutoName->Index) = AutoName;

	// JG - Added for FName debugging
#ifndef _XBOX
#ifdef _DEBUG
	// Add this to the FName debugging COM object
	g_FNameDebug.AddFName(AutoName->Index,AutoName->GetUnicodeName());
#endif
#endif
	// JG - End additions

	unguard;
}
#if __MWERKS__
#pragma auto_inline reset
#endif

//
// FName constructor.
//
FName::FName( const TCHAR* Name, EFindName FindType )
{
    check(Name);
	verifySlow(appStrlen(Name) < NAME_SIZE);
    Construct(Name, FindType);
}

void FName::Construct( const ANSICHAR* Name, EFindName FindType )
{
    guard(FName::Construct);
	check(Name);
	if( !Initialized )
		appErrorf( TEXT("FName constructed before FName::StaticInit") );

	// If empty or invalid name was specified, return NAME_None.
	if( !Name[0] )
	{
		Index = NAME_None;
		return;
	}

	// Try to find the name in the hash.
	INT iHash = appStrihash(Name) & (ARRAY_COUNT(NameHash)-1);
	for( FNameEntry* Hash=NameHash[iHash]; Hash; Hash=Hash->HashNext )
	{
		if( stricmp( Name, Hash->GetRawName() )==0 )
		{
			// Found it in the hash.
			Index = Hash->Index;

			// If it already existed in the hash, but we are adding an auto-generated
			// native name entry, set the flag to prevent it being GC'd.
			if( FindType==FNAME_Intrinsic )
				Names(Index)->Flags |= RF_Native;
			return;
		}
	}

	// Didn't find name.
	if( FindType==FNAME_Find )
	{
		// Not found.
		Index = NAME_None;
		return;
	}

	// Find an available entry in the name table.
	if( Available.Num() )
	{
		Index = Available( Available.Num()-1 );
		Available.Remove( Available.Num()-1 );
	}
	else
	{
		Index = Names.Add();
	}

	// Allocate and set the name.
	Names(Index) = NameHash[iHash] = AllocateNameEntry( Name, Index, 0, NameHash[iHash] );
	if( FindType==FNAME_Intrinsic )
		Names(Index)->Flags |= RF_Native;

#if defined(XBOX_ASSET_FORMAT) && !defined(_XBOX)
	//jg -- Don't let names be GCed during asset conversion
	Names(Index)->Flags |= RF_Native;
#endif

	// JG - Added for FName debugging
#ifndef _XBOX
#ifdef _DEBUG
	// Add this to the FName debugging COM object
	g_FNameDebug.AddFName(Index,Names(Index)->GetUnicodeName());
#endif
#endif
	// JG - End additions
	unguard;
}

/*-----------------------------------------------------------------------------
	FName subsystem.
-----------------------------------------------------------------------------*/

//
// Initialize the name subsystem.
//
void FName::StaticInit()
{
	guard(FName::StaticInit);
	check(Initialized==0);
	check((ARRAY_COUNT(NameHash)&(ARRAY_COUNT(NameHash)-1)) == 0);
	Initialized = 1;

	// Init the name hash.
	{for( INT i=0; i<ARRAY_COUNT(FName::NameHash); i++ )
		NameHash[i] = NULL;}

	// Register all hardcoded names.
	#define REGISTER_NAME(num,namestr) \
		Hardcode(AllocateNameEntry(#namestr,num,RF_Native,NULL));
	#define REG_NAME_HIGH(num,namestr) \
		Hardcode(AllocateNameEntry(#namestr,num,RF_Native|RF_HighlightedName,NULL));
	#include "UnNames.h"

	// Verify no duplicate names.
	{for( INT i=0; i<ARRAY_COUNT(NameHash); i++ )
		for( FNameEntry* Hash=NameHash[i]; Hash; Hash=Hash->HashNext )
			for( FNameEntry* Other=Hash->HashNext; Other; Other=Other->HashNext )
				if( stricmp(Hash->GetRawName(),Other->GetRawName())==0 )
					appErrorf( TEXT("Name '%s' was duplicated"), Hash->GetName() );}

	debugf( NAME_Init, TEXT("Name subsystem initialized") );
	unguard;
}

//
// Shut down the name subsystem.
//
void FName::StaticExit()
{
	guard(FName::StaticExit);
	check(Initialized);

	// Kill all names.
	for( INT i=0; i<Names.Num(); i++ )
		if( Names(i) )
			delete Names(i);

	// Empty tables.
	Names.Empty();
	Available.Empty();
	Initialized = 0;

	debugf( NAME_Exit, TEXT("Name subsystem shut down") );
	unguard;
}

//
// Display the contents of the global name hash.
//
void FName::DisplayHash( FOutputDevice& Ar )
{
	guard(FName::DisplayHash);

	INT UsedBins=0, NameCount=0;
	DWORD UsedBytes = 0;
	for( INT i=0; i<ARRAY_COUNT(NameHash); i++ )
	{
		if( NameHash[i] != NULL ) 
		{
			UsedBins++;
		}
		for( FNameEntry *Hash = NameHash[i]; Hash; Hash=Hash->HashNext )
		{
			NameCount++;
			UsedBytes += sizeof(FNameEntry) - (NAME_SIZE - strlen(Hash->GetRawName()) - 1)*sizeof(ANSICHAR);
		}
	}
	Ar.Logf( TEXT("Hash: %i names, %i/%i hash bins, memory=%5.1f KB"), NameCount, UsedBins, ARRAY_COUNT(NameHash), UsedBytes/1024.f );
	unguard;
}

//
// Delete an name permanently; called by garbage collector.
//
void FName::DeleteEntry( INT i )
{
	guard(FName::DeleteEntry);

	// Unhash it.
	FNameEntry* NameEntry = Names(i);
	check(NameEntry);
	check(!(NameEntry->Flags & RF_Native));
	INT iHash = appStrihash(NameEntry->GetRawName()) & (ARRAY_COUNT(NameHash)-1);
	FNameEntry** HashLink;
	for( HashLink=&NameHash[iHash]; *HashLink && *HashLink!=NameEntry; HashLink=&(*HashLink)->HashNext );
	if( !*HashLink )
		appErrorf( TEXT("Unhashed name '%s'"), NameEntry->GetName() );
	*HashLink = (*HashLink)->HashNext;

	// Delete it.
	delete NameEntry;
	Names(i) = NULL;
	Available.AddItem( i );

	unguard;
}

/*-----------------------------------------------------------------------------
	FNameEntry implementation.
-----------------------------------------------------------------------------*/

CORE_API FArchive& operator<<( FArchive& Ar, FNameEntry& E )
{
	guard(FNameEntry<<);
	FString Str;
	//jg -- Don't crash if the stack doesn't contain a zero on it
	if (Ar.IsSaving())
	{
		Str = E.Name;
	}
	Ar << Str;
	if (Ar.IsLoading())
	{
		appStrcpy(E.Name,*Str);
	}
	return Ar << E.Flags;
	unguard;
}

#if __MWERKS__
#pragma auto_inline off
#endif
CORE_API FNameEntry* AllocateNameEntry( const ANSICHAR* Name, DWORD Index, DWORD Flags, FNameEntry* HashNext )
{
	guard(AllocateNameEntry);

    int len = strlen(Name);

	FNameEntry* NameEntry = (FNameEntry*)appMalloc( sizeof(FNameEntry) - (NAME_SIZE - len - 1)*sizeof(ANSICHAR));
	NameEntry->Index      = Index;
	NameEntry->Flags      = Flags;
	NameEntry->HashNext   = HashNext;
    strcpy(NameEntry->Name, Name);
	return NameEntry;

	unguard;
}
#if __MWERKS__
#pragma auto_inline reset
#endif

#ifndef _XBOX
#ifdef _DEBUG
//scion jg -- Convert the ANSI FName to UNICHAR
CORE_API const UNICHAR* FNameEntry::GetUnicodeName(void)
{
	// Figure out how many characters we need
	DWORD StrLen = strlen(Name) + sizeof(ANSICHAR);
	// Get the buffer to convert into
	UNICHAR* szNextBuffer = ConversionQueue.GetLocation(StrLen);
	// Now convert the ansi string
	winToUNICODE(szNextBuffer,Name,StrLen);
	return szNextBuffer;
}
#endif
#endif

#if defined(_DEBUG) || defined(PROFILE)
CORE_API const TCHAR* DebugName(INT Index)
{
	return FName::SafeString((EName)Index);
}
#endif

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

