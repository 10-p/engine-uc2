/*=============================================================================
	FMallocDebug.h: Debug memory allocator.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
		* Ripped apart by Joe Graf
=============================================================================*/

#include <malloc.h>

#include "UnThreadingWindows.h"

// Define this if you want full on memory tracking
//#define MEM_TRACKING 1

#if MEM_TRACKING
#ifdef _XBOX
// Protoype the methods we use from the xbox libs
extern "C" {
HRESULT __stdcall DmRegisterAllocationType(const char*,_WORD*);
HRESULT __stdcall DmInsertAllocationEntry(void*,SIZE_T,_WORD);
HRESULT __stdcall DmRemoveAllocationEntry(void*,SIZE_T,_WORD);
};
#endif
#endif

// Structure for memory debugging.
struct FMemDebugBase
{
#if MEM_TRACKING
	// The FMemTrackItem associated with this alloc
	struct FMemTrackItem* pItem;
#endif
	DWORD		MemTag;
	SIZE_T		Size;
	INT			RefCount;
	INT			PreTag;
};

// scion sz -
DWORD GMemoryTagTest=0;

//scion jg -- Additional memory tracking routines
#if MEM_TRACKING
// Used to find items of the same type. It holds a unique allocation signature
// that we can use to search on. It also holds current count, max count,
// and total ever allocated.
struct FMemTrackItem
{
	// Next item in the hash
	FMemTrackItem* pNext;
	// The file this was allocated in
	const ANSICHAR* FileID;
	// The line it was allocated on
	INT LineNo;
	// The current number of items allocated
	INT CurrentCount;
	// The maximum number of these items active at once
	INT MaxCount;
	// The total number of items ever allocated
	INT TotalCount;
#ifdef _XBOX
	// For use with XbMemDump
	_WORD TrackingKey;
#endif

	// Constuctor to zero things
	void Init(const ANSICHAR* _FileID,const INT _Line,
		FMemTrackItem* _Hash)
	{
		FileID = _FileID;
		LineNo = _Line;
		CurrentCount = MaxCount = TotalCount = 1;
		pNext = _Hash;
#ifdef _XBOX
		char szType[256];
		// Create a readable string
		sprintf(szType,"%s:%d",_FileID,_Line);
		// Now tell the xbox lib about our type so we can track later
		DmRegisterAllocationType(szType,&TrackingKey);
#endif
	}
};

#define HASH_SIZE 4096

// Lame hash key calculator
INT CalcHashKey(const ANSICHAR* FileID,const INT LineNo)
{
	return ((INT)FileID + LineNo) % HASH_SIZE;
}

// Searches the hash for an existing item of this type. If found, it updates
// the allocation stats. If not found it creates a new item and adds it to
// the hash
FMemTrackItem* AddMemTrack(FMemTrackItem* Hash[],const ANSICHAR* FileID,
	const INT LineNo,void* pBlock,const INT Size)
{
	guard(AddMemTrack);
	// Figure out the hash key for this
	INT Key = CalcHashKey(FileID,LineNo);
	// Search until we find the one we want
	FMemTrackItem* pFound = Hash[Key];
	while (pFound != NULL && pFound->FileID != FileID &&
		pFound->LineNo != LineNo)
	{
		pFound = pFound->pNext;
	}
	// Found one, so just update the stats
	if (pFound != NULL)
	{
		pFound->CurrentCount++;
		pFound->TotalCount++;
		pFound->MaxCount = Max(pFound->MaxCount,pFound->CurrentCount);
	}
	// Not found so allocate an initial one
	else
	{
		// Allocate it directly
		pFound = (FMemTrackItem*)malloc(sizeof(FMemTrackItem));
		checkSlow(pFound);
		// Now initialize it
		pFound->Init(FileID,LineNo,NULL);
		// Store it back in the hash
		Hash[Key] = pFound;
	}
#ifdef _XBOX
	// Tell the xbox lib about this allocation
	DmInsertAllocationEntry(pBlock,Size,pFound->TrackingKey);
#endif
	return pFound;
	unguard;
}

// Updates the statistics
void DecMemTrack(FMemTrackItem* pItem,void* pBlock)
{
	guard(DecMemTrack);
	checkSlow(pItem);
	pItem->CurrentCount--;
#ifdef _XBOX
	// Tell the xbox lib that we freed this allocation
	DmRemoveAllocationEntry(pBlock,0,pItem->TrackingKey);
#endif
	unguard;
}

// Walks all of the hash table dumping out the information
void DumpHashInfo(FMemTrackItem* Hash[])
{
	debugf(TEXT("File, Line, Current, Max, Total"));
	for (INT Index = 0; Index < HASH_SIZE; Index++)
	{
		// Start at the first item in this hash slot
		FMemTrackItem* pItem = Hash[Index];
		// Loop through and dump each item's stats
		while (pItem)
		{
			debugf(TEXT("%s, %d, %d, %d, %d"),
				ANSI_TO_TCHAR(pItem->FileID),pItem->LineNo,
				pItem->CurrentCount,pItem->MaxCount,pItem->TotalCount);
			// Move to the next item in the hash
			pItem = pItem->pNext;
		}
	}
}
#endif

//scion jg -- Since the named mallocs causes us to run out of memory on the
// xbox, turn this on only when tracking a problem that needs it
#ifdef _XBOX
#define WANT_NAMED_MALLOCS 0
#define WANT_LAST_ALLOCED 0
#define NUM_ALLOCS_TO_DUMP 200
#else
#define WANT_NAMED_MALLOCS 1
#define WANT_LAST_ALLOCED 0
#endif


// Tags.
enum {MEM_PreTag =0xf0ed1cee};
enum {MEM_PostTag=0xdeadf00f};
enum {MEM_Tag    =0xfe      };
enum {MEM_WipeTag=0xcd      };

// Debug memory allocator.
class FMallocDebug : public FMalloc
{
private:
#if MEM_TRACKING
	// Hash of all allocated FMemTrackItems
	FMemTrackItem* Hash[HASH_SIZE];
#endif
	typedef TDoubleLinkedList<FMemDebugBase> FMemDebug;

	// Variables.
	FMemDebug* GFirstDebug;
	UBOOL MemInited;
#ifdef _XBOX
	//jg -- Make this malloc thread safe
	FCriticalSectionWin AccessSynch;
#endif

public:
	// FMalloc interface.
	FMallocDebug()
	:	GFirstDebug	( NULL )
	,	MemInited	( 0 )
	{}
	//scion jg -- Added the __FILE__ & __LINE__ vars for improved memory tracking
#ifndef FINAL_RELEASE
	void* Malloc( DWORD Size, const ANSICHAR* File = __FILE__, INT Line = __LINE__ )
#else
	void* Malloc( DWORD Size )
#endif
	{
		guard(FMallocDebug::Malloc);
#ifdef _XBOX
		//jg -- Synchronize thread access
		FScopeLock sl(&AccessSynch);
#endif
		checkSlow(MemInited);
#if 0 //scion jg -- It is perfectly valid to allocate zero bytes and
	  // XGAssembleShader likes to do so. Let it succeed or no shaders
	  // can be used on the xbox in debug.
#ifdef _XBOX
		//!!vogel: needed as XGAssembleShader uses our overloaded operator.
		if( Size == 0 )
			return NULL;
#endif
		check((INT)Size>0);
#endif
		FMemDebug* Ptr = NULL;
		guard(CallMalloc);
			Ptr = (FMemDebug*)malloc( sizeof(FMemDebug) + Size + sizeof(INT) );
			check(Ptr);
		unguard;
#if MEM_TRACKING
		// Add this item to the hash
		Ptr->pItem = AddMemTrack(Hash,File,Line,Ptr+1,Size);
#endif
		guard(SetPtr);
			Ptr->MemTag	  = GMemoryTag;
			Ptr->RefCount = 1;
			Ptr->Size     = Size;
			Ptr->Next     = GFirstDebug;
			Ptr->PrevLink = &GFirstDebug;
			Ptr->PreTag   = MEM_PreTag;
		unguard;
		guard(SetPost);
			*(INT*)((BYTE*)Ptr+sizeof(FMemDebug)+Size) = MEM_PostTag;
		unguard;
		guard(FillMem);
			appMemset( Ptr+1, MEM_Tag, Size );
		unguard;
		guard(DoPrevLink);
			if( GFirstDebug )
			{
				check(GIsCriticalError||GFirstDebug->PrevLink==&GFirstDebug);
				GFirstDebug->PrevLink = &Ptr->Next;
			}
			GFirstDebug = Ptr;
		unguard;

		GAllocatedMemory += Size;

		return Ptr+1;
		unguardf(( TEXT("%i %s"), Size, Tag ));
	}
	//scion jg -- Added the __FILE__ & __LINE__ vars for improved memory tracking
#ifndef FINAL_RELEASE
	void* Realloc( void* InPtr, DWORD NewSize, const ANSICHAR* File = __FILE__, INT Line = __LINE__ )
#else
	void* Realloc( void* InPtr, DWORD NewSize )
#endif
	{
		guard(FMallocDebug::Realloc);
#ifdef _XBOX
		//jg -- Synchronize thread access
		FScopeLock sl(&AccessSynch);
#endif
		checkSlow(MemInited);
		if( InPtr && NewSize )
		{
			check(GIsCriticalError||((FMemDebug*)InPtr-1)->RefCount==1);
			check(GIsCriticalError||((FMemDebug*)InPtr-1)->Size>0);
#ifndef FINAL_RELEASE
			void* Result = appMalloc( NewSize, File, Line );
#else
			void* Result = appMalloc( NewSize );
#endif
			appMemcpy( Result, InPtr, Min(((FMemDebug*)InPtr-1)->Size,NewSize) );
			appFree( InPtr );
			return Result;
		}
		else if( NewSize )
		{
#ifndef FINAL_RELEASE
			return appMalloc( NewSize, File, Line );
#else
			return appMalloc( NewSize );
#endif
		}
		else
		{
			if( InPtr )
				appFree( InPtr );
			return NULL;
		}
		unguardf(( TEXT("%08X %i %s"), (INT)InPtr, NewSize, Tag ));
	}
	void Free( void* InPtr )
	{
		guard(FMallocDebug::Free);
#ifdef _XBOX
		//jg -- Synchronize thread access
		FScopeLock sl(&AccessSynch);
#endif
		checkSlow(MemInited);
		if( !InPtr )
			return;

		FMemDebug* Ptr = (FMemDebug*)InPtr - 1;
		check(GIsCriticalError||Ptr->Size>0);
		check(GIsCriticalError||Ptr->RefCount==1);
		check(GIsCriticalError||Ptr->PreTag==MEM_PreTag);
		check(GIsCriticalError||*(INT*)((BYTE*)InPtr+Ptr->Size)==MEM_PostTag);
#if MEM_TRACKING
		// Decrement the count for these
		DecMemTrack(Ptr->pItem,InPtr);
#endif
		appMemset( InPtr, MEM_WipeTag, Ptr->Size );
		Ptr->Size = 0;
		Ptr->RefCount = 0;

		GAllocatedMemory -= Ptr->Size;

		check(GIsCriticalError||Ptr->PrevLink);
		check(GIsCriticalError||*Ptr->PrevLink==Ptr);
		*Ptr->PrevLink = Ptr->Next;
		if( Ptr->Next )
			Ptr->Next->PrevLink = Ptr->PrevLink;

		free( Ptr );

		unguard;
	}
	void DumpAllocsTagged( DWORD FindTag )
	{
		guard(FMallocDebug::DumpAllocsTagged);

		GMemoryTagTest=FindTag;
		DumpAllocs();

		unguard;
	}
	void DumpAllocs()
	{
		guard(FMallocDebug::DumpAllocs);
#ifdef _XBOX
		//jg -- Synchronize thread access
		FScopeLock sl(&AccessSynch);
#endif
#if MEM_TRACKING
		DumpHashInfo(Hash);
		return;
#endif
		INT Count=0;
		INT Chunks=0;
#if WANT_LAST_ALLOCED
		//scion jg -- Dump the last N number of allocs. This is used to try
		// and track leaks on the xbox, since you can't get a shutdown report
		INT nDumpCount = NUM_ALLOCS_TO_DUMP + 1;
		debugf(TEXT("Dumping last %d allocs:"),NUM_ALLOCS_TO_DUMP);
		// Now work backwards from the end dumping the allocs
		for (FMemDebug* Ptr = GFirstDebug; nDumpCount > 0 && Ptr;
			Ptr = Ptr->Next)
		{
			// scion sz - only process tagged mem
			if( Ptr->MemTag != GMemoryTagTest )
				continue;

			TCHAR Temp[256];
			Temp[255] = L'\0';
			appMemcpy( Temp, Ptr + 1, Min((SIZE_T)255 * sizeof(TCHAR),Ptr->Size) );
#if WANT_NAMED_MALLOCS //scion jg -- See previous WANT_NAMED_MALLOCS comment
#if MEM_TRACKING
			debugf( TEXT("   0x%x %10i %s:%i <%s>"), Ptr, Ptr->Size,
				Ptr->pItem->FileID, Ptr->pItem->LineNo, Temp );
#else
			debugf( TEXT("   0x%x %10i <%s>"), Ptr, Ptr->Size, Temp );
#endif
#else
			debugf( TEXT("   0x%x %10i <%s>"), Ptr, Ptr->Size, Temp );
#endif
			Count += Ptr->Size;
			nDumpCount--;
		}
		debugf( TEXT("%i Bytes allocated in last %d allocations"), Count,
			NUM_ALLOCS_TO_DUMP);
#else
		debugf( TEXT("Unfreed memory:") );
		for( FMemDebug* Ptr=GFirstDebug; Ptr; Ptr=Ptr->Next )
		{
			// scion sz - only process tagged mem
			if( Ptr->MemTag != GMemoryTagTest )
				continue;

			TCHAR Temp[256];
			appStrncpy( Temp, (TCHAR*)(Ptr+1), Min((SIZE_T)255,Ptr->Size) );
#if WANT_NAMED_MALLOCS //scion jg -- See previous comment
#if MEM_TRACKING
			debugf( TEXT("   0x%x %10i %s:%i <%s>"), Ptr, Ptr->Size,
				Ptr->pItem->FileID, Ptr->pItem->LineNo, Temp );
#else
			debugf( TEXT("   0x%x %10i <%s>"), Ptr, Ptr->Size, Temp );
#endif
#else
			debugf( TEXT("   0x%x %10i <%s>"), Ptr, Ptr->Size, Temp );
#endif
			Count += Ptr->Size;
			Chunks++;
		}
		if( Count || Chunks )
		{
			debugf( TEXT("End of list: %i Bytes still allocated"), Count );
			debugf( TEXT("             %i Chunks allocated"), Chunks );
		}		
#endif
#if USE_MEMTAGGING
		INT Count=0;
		INT Chunks=0;

#define NUM_MEMTAGS 10
		INT TagMemory[NUM_MEMTAGS];
		appMemzero( TagMemory, sizeof(TagMemory) );

		debugf( TEXT("Unfreed memory:") );
		for( FMemDebug* Ptr=GFirstDebug; Ptr; Ptr=Ptr->Next )
		{
			TagMemory[Ptr->MemTag] += Ptr->Size;

			Count += Ptr->Size;
			Chunks++;
		}
		for( INT i=0; i<NUM_MEMTAGS; i++ )
			debugf( TEXT("Tag %i     : %i Bytes still allocated"), i, TagMemory[i] );
#endif

		unguard;
	}
	void HeapCheck()
	{
		guard(FMallocDebug::HeapCheck);
#ifdef _XBOX
		//jg -- Synchronize thread access
		FScopeLock sl(&AccessSynch);
#endif
		for( FMemDebug** Link = &GFirstDebug; *Link; Link=&(*Link)->Next )
			check(GIsCriticalError||*(*Link)->PrevLink==*Link);
#if (defined _MSC_VER) && (!defined _XBOX || defined _DEBUG) && (!defined __GCN__)
		INT Result = _heapchk();
		check(Result!=_HEAPBADBEGIN);
		check(Result!=_HEAPBADNODE);
		check(Result!=_HEAPBADPTR);
		check(Result!=_HEAPEMPTY);
		check(Result==_HEAPOK);
#endif
		unguard;
	}
	void Init()
	{
		guard(FMallocDebug::Init);
#ifdef _XBOX
		//jg -- Synchronize thread access
		FScopeLock sl(&AccessSynch);
#endif
		check(!MemInited);
#if MEM_TRACKING
		// Zero the hash
		appMemzero(Hash,sizeof(FMemTrackItem*) * HASH_SIZE);
#endif
		MemInited=1;
		unguard;
	}
	void Exit()
	{
		guard(FMallocDebug::Exit);
#ifdef _XBOX
		//jg -- Synchronize thread access
		FScopeLock sl(&AccessSynch);
#endif
		check(MemInited);
		MemInited=0;
		unguard;
	}
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

