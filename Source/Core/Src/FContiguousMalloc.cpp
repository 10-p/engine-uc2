/* ============================================================================
 * Author: jg
 * ============================================================================
 *
 * This class is a simple best fit allocator. It uses a singly linked list of
 * free blocks to find the best block. Best in this case is smallest block to
 * fit and highest in address space, which should prevent fragmentation and
 * allow for good merging of free blocks. Search times should be acceptable
 * since contiguous blocks are allocated during serialization and released
 * during garbage collection. All blocks are stored in address order. Upon
 * freeing of a block an attempt is made to merge the blocks into larger
 * sized blocks.
 *
 * http://www.cs.utexas.edu/users/wilson/papers/fragsolved.pdf explains how
 * little fragmentation there can be when using this strategy.
 *
 * ============================================================================
 */

#include "CorePrivate.h"
#include "FContiguousMalloc.h"

// Undo any Windows defines that core might have defined
#undef BYTE
#undef WORD
#undef DWORD
#undef INT
#undef FLOAT
#undef MAXBYTE
#undef MAXWORD
#undef MAXDWORD
#undef MAXINT
#undef CDECL
#undef HANDLE
#undef HINSTANCE
#undef TEXT
#undef INFINITE

#include <xtl.h>


/* ============================================================================
 * FContiguousMalloc::Init
 * Author: jg
 * 
 * Allocates the initial block of memory that all allocations will come from.
 * Note: this malloc requires the other FMalloc to be created first.
 *
 * ============================================================================
 */
void FContiguousMalloc::Init(void)
{
	guard(FContiguousMalloc::Init);
	if (pMem == NULL)
	{
		// Allocate our initial block of memory 4 byte aligned
		pMem = D3D_AllocContiguousMemory(InitialSize,4);
		// Now create our first free list pointer
		pFreeList = NEW FContiguousBlock(pMem,InitialSize,NULL);
		check(pFreeList);
		debugf(TEXT("Contiguous memory usable from 0x%X through 0x%X"),
			(DWORD)pMem,(DWORD)pMem + InitialSize);
	}
	unguard;
}

/* ============================================================================
 * FContiguousMalloc::Exit
 * Author: jg
 * 
 * Returns the allocated block of memory back to the OS
 *
 * ============================================================================
 */
void FContiguousMalloc::Exit(void)
{
	guard(FContiguousMalloc::Exit);
	if (pMem != NULL)
	{
		// Release the block of memory we are doling out allocations from
		D3D_FreeContiguousMemory(pMem);
		// Clean up any allocated blocks
		FContiguousBlock* pDelete = pFreeList;
		while (pDelete != NULL)
		{
			FContiguousBlock* pNext = pDelete->pNext;
			delete pDelete;
			pDelete = pNext;
		}
		pFreeList = NULL;
	}
	unguard;
}

#ifndef FINAL_RELEASE

// comparison function for appQsort() call in ListObjects(), see below
static QSORT_RETURN CDECL CompareItems(const FItem* A, const FItem* B)
{
	return B->Max - A->Max;
}

/* ============================================================================
 * ListObjects
 * Author: moelfke
 * 
 * Debug helper function. If FContiguousMalloc::AlignedMalloc() fails to allocate
 * the memory, before crashing it calls this to print out a list of all objects
 * to help find any leaks. Mostly copied from UGameEngine::Exec(), although
 * simplified since the code to filter out objects is not used here.
 *
 * ============================================================================
 */
static FORCEINLINE void ListObjects()
{
	debugf(TEXT("Objects:"));
	debugf(TEXT(""));

	TArray<FItem> List;
	FItem Total;
	// iterate through all objects
	for (FObjectIterator It; It; ++It)
	{
		FArchiveCountMem Count(*It);
		// if an object of this class is already in the list, increment its entry, otherwise add a new entry
		INT i;
		for(i = 0; i < List.Num(); i++)
		{
			if (List(i).Class == It->GetClass())
			{
				break;
			}
		}
		if (i == List.Num())
		{
			i = List.AddItem(FItem(It->GetClass()));
		}
		List(i).Add(Count);
		Total.Add(Count);
	}
	if (List.Num())
	{
		// quicksort the list by amount of memory used, highest memory usage first
		appQsort(&List(0), List.Num(), sizeof(List(0)), (QSORT_COMPARE)CompareItems);
		// print the header
		debugf(TEXT(" %30s % 6s % 10s  % 10s "), TEXT("Class"), TEXT("Count"), TEXT("NumBytes"), TEXT("MaxBytes"));
		// print out the list
		for (INT i = 0; i < List.Num(); i++)
		{
			debugf(TEXT(" %30s % 6i % 10iK % 10iK"), List(i).Class->GetName(), List(i).Count, List(i).Num/1024, List(i).Max/1024);
		}
		debugf(TEXT(""));
	}
	// print summary
	debugf( TEXT("%i Objects (%.3fM / %.3fM)"), Total.Count, (FLOAT)Total.Num/1024.0/1024.0, (FLOAT)Total.Max/1024.0/1024.0 );
}
#endif // FINAL_RELEASE

/* ============================================================================
 * FContiguousMalloc::AlignedMalloc
 * Author: jg
 * 
 * Allocates a block of memory from the free pool and returns that to the
 * caller. It searches the free list for a best fit (smallest block with the
 * highest address). This is O(n) but should never be a long search due to the
 * merging of blocks and the normal usage pattern.
 *
 * inputs:	Size -- The size of the allocation
 *			Alignment -- The pointer alignment needed for this block
 *
 * returns: NULL if it failed to allocate, a valid pointer otherwise
 *
 * ============================================================================
 */
#ifndef FINAL_RELEASE
void* FContiguousMalloc::AlignedMalloc(DWORD Size,DWORD Alignment, const ANSICHAR* File, INT Line)
#else
void* FContiguousMalloc::AlignedMalloc(DWORD Size,DWORD Alignment)
#endif
{
	guard(FContiguousMalloc::AlignedMalloc);
	// Must be aligned on power of two
	check(FIsPowerOfTwo(Alignment));
	// Align can't be bigger than 256 since I pack size and alignment in a DWORD
	check(Alignment <= 256);
	// Our max size is 24 bits (3 bytes)
	check(Size <= (1 << 24));
	// Determine the real size we need. The 4 bytes is used for our internal
	// size that we stored in the allocated chunk
	DWORD AlignedSize = Size + Alignment + sizeof(FAllocationDetail);
	// Find the best block to pull the data from
	FContiguousBlock* pBestFit = GetBestFitBlock(AlignedSize);
	// Default to not finding a chunk
	void* pAlignedMem = NULL;
	// Only handle if the best fit is large enough
	if (pBestFit != NULL && pBestFit->Size >= AlignedSize)
	{
		// Change the attributes on the page
		XPhysicalProtect(pBestFit->pMem,pBestFit->Size,PAGE_READWRITE);
		// We have a chunk of memory so snip off what we need. Allocate from the end
		// of the memory block so we only need to adjust the size
		pBestFit->Size -= AlignedSize;
		// Figure out the max size we'll need
		DWORD pUnaligned = (DWORD)pBestFit->pMem + pBestFit->Size;
		// Now adjust the pointer that will be returned to be aligned correctly
		pAlignedMem = (void*)Align(pUnaligned + sizeof(FAllocationDetail),
			Alignment);
		// Treat the block of memory minus the size as our structure
		FAllocatedBlock* pNewBlock =
			(FAllocatedBlock*)((DWORD)pAlignedMem - sizeof(FAllocationDetail));
		// Set the new block's size/alignment for freeing later
		pNewBlock->SetSizeAndAlignment(AlignedSize,Alignment);
		pNewBlock->SetOriginalPointer(pUnaligned);
		pNewBlock->pMem = pAlignedMem;
		// Verify the allocation has correct alignment
		check(((DWORD)pNewBlock->pMem & (Alignment - 1)) == 0);
	}
#ifndef FINAL_RELEASE
	else
	{
		guard(OutOfContiguousMemory);
		if (pBestFit == NULL || pBestFit->Size < AlignedSize)
		{
			debugf(TEXT("Failed to alloc (%d) with alignment (%d)"),Size,
				Alignment);
			debugf(TEXT("Out of contigous memory, printing object list"));
			ListObjects();
			debugf(TEXT("Dumping allocation info"));
			DumpAllocs();
			check(pBestFit && pBestFit->Size >= AlignedSize &&
				"Out of contiguous memory!");
		}
		unguard;
	}
#endif
	// Return the usable portion of the memory
	return pAlignedMem;
	unguard;
}

/* ============================================================================
 * FContiguousMalloc::Free
 * Author: jg
 * 
 * Returns the chunk of memory back to the free list. It returns the block to
 * the free list in address order. It tries to merge blocks at either side of
 * the memory block to make one larger block.
 *
 * inputs:	pMemBlock -- The chunk of memory to return
 *
 * ============================================================================
 */
void FContiguousMalloc::Free(void* pMemBlock)
{
	guard(FContiguousMalloc::Free);
	if (pMemBlock != NULL)
	{
		// Must be a physical address
		check((DWORD)pMemBlock & 0x80000000);
		// Convert to our internal type
		FAllocatedBlock* pFreed = (FAllocatedBlock*)((DWORD)pMemBlock - sizeof(FAllocationDetail));
		// Start our search at the beginning of the free list
		FContiguousBlock* pLessThanBlock = pFreeList;
		// Default to being at the end of the chain
		FContiguousBlock* pGreaterThanBlock = NULL;
		// Search the free list to find where to insert this block
		FindInsertionPoint(pLessThanBlock,pGreaterThanBlock,pFreed);
		// Try to merge the blocks together. If they are not contiguous
		// then just insert the freed block into the free list
		if (MergeBlocks(pLessThanBlock,pGreaterThanBlock,pFreed) == 0)
		{
			// Create the new block linking it to the greater
			FContiguousBlock* pNewBlock = NEW FContiguousBlock(
				pFreed->GetOriginalPointer(),pFreed->GetSize(),pGreaterThanBlock);
			// Determine if this is an insert between or at the beginning
			if (pLessThanBlock != NULL)
			{
				// Now link it in
				pLessThanBlock->pNext = pNewBlock;
			}
			else
			{
				// Insert at the beginning of the list
				pFreeList = pNewBlock;
			}
		}
	}
	unguard;
}

/* ============================================================================
 * FContiguousMalloc::FindInsertionPoint
 * Author: jg
 * 
 * Searches the free list from the passed in starting point to determine where
 * the newly freed block of memory needs to reside.
 *
 * inputs:	pLessThanBlock -- The starting point of the search
 *			pGreaterThanBlock -- The node that lies in higher address space
 *			pFreed -- The newly freed block of memory
 *
 * returns:	Both the less than/greater than pointers are updated in here
 *
 * ============================================================================
 */
void FContiguousMalloc::FindInsertionPoint(FContiguousBlock*& pLessThanBlock,
	FContiguousBlock*& pGreaterThanBlock,FAllocatedBlock* pFreed)
{
	guard(FContiguousMalloc::FindInsertionPoint);
	// Move this one block out
	pGreaterThanBlock = pLessThanBlock->pNext;
	// Search through the free list and find where this block lands
	while (pGreaterThanBlock != NULL && pGreaterThanBlock->pMem < pFreed)
	{
		pGreaterThanBlock = pGreaterThanBlock->pNext;
		pLessThanBlock = pLessThanBlock->pNext;
	}
	// Set up our pointers so that the blocks can be merged/inserted into
	// correctly based upon where the freed memory lands
	if (pLessThanBlock->pMem > pFreed)
	{
		// In this case the freed block is the smallest address. Zero
		// the less than block so we can insert in front of it
		pGreaterThanBlock = pLessThanBlock;
		pLessThanBlock = NULL;
	}
	unguard;
}

/* ============================================================================
 * FContiguousMalloc::MergeBlocks
 * Author: jg
 * 
 * Takes the recently freed block and tries to merge it with either the previous
 * or next memory address block in the free list. Then it tries to merge the
 * free list blocks in case merging the freed block fills in the "gap".
 *
 * inputs:	pLessThanBlock -- The free list block with the lower memory address
 *			pGreaterThanBlock -- Higher address version of the above
 *			pFreed -- The recently freed memory block
 *
 * returns: True if it merged the blocks, false otherwise
 *
 * ============================================================================
 */
UBOOL FContiguousMalloc::MergeBlocks(FContiguousBlock* pLessThanBlock,
	FContiguousBlock* pGreaterThanBlock,FAllocatedBlock* pFreed)
{
	guard(FContiguousMalloc::MergeBlocks);
	// Default to failure on all of these
	UBOOL bMergeGreater = 0;
	UBOOL bMergeLesser = 0;
	UBOOL bMergeFreeList = 0;
	// Check to see if we can merge the freed block with the higher address one
	bMergeGreater = CanMergeBlocks(pFreed,pGreaterThanBlock);
	if (bMergeGreater == 1)
	{
		// Make the freed block part of the greater block. Since the freed block
		// address is less than the greater block, we need to set the available
		// address to the freed block
		pGreaterThanBlock->pMem = (void*)pFreed->GetOriginalPointer();
		pGreaterThanBlock->Size += pFreed->GetSize();
	}
	else
	{
		// Couldn't merge with the higher address space block so try merging
		// with the lower address space block
		bMergeLesser = CanMergeBlocks(pLessThanBlock,pFreed);
		if (bMergeLesser == 1)
		{
			// All we need to do is add to the free space since the address is
			// contiguous and is larger than the address in the less than block
			pLessThanBlock->Size += pFreed->GetSize();
		}
	}
	bMergeFreeList = CanMergeBlocks(pLessThanBlock,pGreaterThanBlock);
	if (bMergeFreeList == 1)
	{
		// Unlink the higher address block
		pLessThanBlock->pNext = pGreaterThanBlock->pNext;
		// Now add its size to the current block
		pLessThanBlock->Size += pGreaterThanBlock->Size;
		// The greater block is no longer needed so we can delete it
		delete pGreaterThanBlock;
	}
	// Indicate to the caller that we merged something
	return bMergeGreater || bMergeLesser || bMergeFreeList;
	unguard;
}
