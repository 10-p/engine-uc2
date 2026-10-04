#ifndef _FCONTIGUOUSMALLOC_H
#define _FCONTIGUOUSMALLOC_H

/* ============================================================================
 * Author: jg
 * ============================================================================
 *
 * This class is a simple best fit allocator. It uses a singly linked list of
 * free blocks to find the best block. Best in this case is smallest block to
 * fit and highest in adress space, which should prevent fragmentation and
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
class CORE_API FContiguousMalloc : public FAlignedMalloc
{
	// Base block that represents the data we need to save per allocation
	struct FAllocationDetail
	{
	private:
		// The size/alignment of the allocation including this header
		DWORD Info;
		// The original pointer returned (might be different due to alignment)
		DWORD pUnaligned;
	public:

		// Returns the size of the allocation
		FORCEINLINE DWORD GetSize(void)
		{
			guard(GetSize);
			return (Info & 0xFFFFFF00) >> 8;
			unguard;
		}

		// Returns the alignment of the allocation
		FORCEINLINE DWORD GetAlignment(void)
		{
			guard(GetAlignment);
			return (Info & 0x000000FF);
			unguard;
		}

		// Sets the size & alignment of this allocation
		FORCEINLINE void SetSizeAndAlignment(DWORD Size,DWORD Alignment)
		{
			guard(SetSizeAndAlignment);
			Info = ((Size & 0x00FFFFFF) << 8) | (Alignment & 0x000000FF);
			unguard;
		}

		// Returns the unaligned original pointer for coalescing
		FORCEINLINE void* GetOriginalPointer(void)
		{
			guard(GetOriginalPointer);
			return (void*)pUnaligned;
			unguard;
		}

		// Sets the unaligned original pointer
		FORCEINLINE void SetOriginalPointer(void* pOrig)
		{
			guard(SetOriginalPointer);
			pUnaligned = (DWORD)pOrig;
			unguard;
		}

		// Sets the unaligned original pointer
		FORCEINLINE void SetOriginalPointer(DWORD Orig)
		{
			guard(SetOriginalPointer);
			pUnaligned = Orig;
			unguard;
		}
	};
	// This is an allocated block of memory
	struct FAllocatedBlock : public FAllocationDetail
	{
		// The address of the block
		void* pMem;
	};
	// Represents a block of memory that is available for allocation
	struct FContiguousBlock
	{
		// The next block in the chain of free blocks
		FContiguousBlock* pNext;
		// The size of the block
		DWORD Size;
		// The address of the free block
		void* pMem;

		// Simple constructor to init values
		FContiguousBlock(void* MemIn,DWORD SizeIn,FContiguousBlock* Next)
		{
			pMem = MemIn;
			Size = SizeIn;
			pNext = Next;
		}
		// Default constructor. Zeros everything
		FContiguousBlock()
		{
			pMem = NULL;
			Size = 0;
			pNext = NULL;
		}
	};
	// The first free block in the list
	FContiguousBlock* pFreeList;
	// This is the size of the contiguous block we initially allocate
	DWORD InitialSize;
	// This is the original block of memory returned by the OS
	void* pMem;

	// Finds the point in the free list the newly freed block should placed
	void FindInsertionPoint(FContiguousBlock*& pLessThanBlock,
		FContiguousBlock*& pGreaterThanBlock,FAllocatedBlock* pFreed);
	// Merges a recently freed block with the free pool
	UBOOL MergeBlocks(FContiguousBlock* pLessThanBlock,
		FContiguousBlock* pGreaterThanBlock,FAllocatedBlock* pFreed);
	
	// Determines if a recently freed block can be merged
	FORCEINLINE UBOOL CanMergeBlocks(FContiguousBlock* pLessThanBlock,
		FAllocatedBlock* pFreed)
	{
		guard(CanMergeBlocks);
		checkSlow(pFreed);
		// If the next address is the address of the block, then they
		// can be merged into one block
		DWORD OriginalPtr = (DWORD)pFreed->GetOriginalPointer();
		return pLessThanBlock &&
			((DWORD)pLessThanBlock->pMem + pLessThanBlock->Size) == OriginalPtr;
		unguard;
	}

	// Determines if a recently freed block can be merged
	UBOOL CanMergeBlocks(FAllocatedBlock* pFreed,
		FContiguousBlock* pGreaterThanBlock)
	{
		guard(CanMergeBlocks);
		checkSlow(pFreed);
		// If the next address is the address of the block, then they
		// can be merged into one block
		DWORD NextAddress = ((DWORD)pFreed->GetOriginalPointer() + pFreed->GetSize());
		return pGreaterThanBlock && NextAddress == (DWORD)pGreaterThanBlock->pMem;
		unguard;
	}

	// Determines if two free blocks can be merged
	UBOOL CanMergeBlocks(FContiguousBlock* pLessThanBlock,
		FContiguousBlock* pGreaterThanBlock)
	{
		guard(CanMergeBlocks);
		// If the next address is the address of the block, then they
		// can be merged into one block
		return pLessThanBlock && pGreaterThanBlock &&
			((DWORD)pLessThanBlock->pMem + pLessThanBlock->Size) == (DWORD)pGreaterThanBlock->pMem;
		unguard;
	}

	// Searches through the free list looking for the best fit block
	FORCEINLINE FContiguousBlock* GetBestFitBlock(DWORD AlignedSize)
	{
		guard(GetBestFitBlock);
		// Start at the head of the list
		FContiguousBlock* pBestFit = NULL;
		FContiguousBlock* pSearch = pFreeList;
		// Search the entire list for the best fitting block
		while (pSearch != NULL)
		{
			// The size must be right, as small as possible, with as high an
			// address as possible
			if (pSearch->Size >= AlignedSize &&
				(pBestFit == NULL ||
				(pSearch->Size <= pBestFit->Size &&
				pSearch->pMem > pBestFit->pMem)))
			{
				// This is our new best fit
				pBestFit = pSearch;
			}
			// Move to the next free block
			pSearch = pSearch->pNext;
		}
		return pBestFit;
		unguard;
	}
public:
	// Init internal vars
	FContiguousMalloc(DWORD Size) : InitialSize(Size), pMem(NULL),
		pFreeList(NULL)
	{
	}

// FMalloc interface
#ifndef FINAL_RELEASE// Compile out the file/line info in our final build
	virtual void* Malloc(DWORD Count,const ANSICHAR* File = __FILE__,
		INT Line = __LINE__)
	{
		return AlignedMalloc(Count,4);
	}
	virtual void* Realloc(void* Original,DWORD Count,
		const ANSICHAR* File = __FILE__,INT Line = __LINE__  )
	{
		// Since we know the usage pattern doesn't ever realloc, don't support
		// realloc calls to this
		check(0);
		return NULL;
	}
#else
	virtual void* Malloc(DWORD Count)
	{
		return AlignedMalloc(Count,4);
	}
	virtual void* Realloc(void* Original,DWORD Count)
	{
		// Since we know the usage pattern doesn't ever realloc, don't support
		// realloc calls to this
		check(0);
		return NULL;
	}
#endif
	virtual void Free(void* Original);
	virtual void Init(void);
	virtual void Exit(void);

	virtual void DumpAllocs(void) 
	{
		debugf(TEXT("Free contiguous blocks:"));
		// sum free blocks
		DWORD TotalFreeBlockSize = 0;
		// Walk the free list chain listing each block
		for (FContiguousBlock* pIter = pFreeList; pIter; pIter = pIter->pNext)
		{
			TotalFreeBlockSize += pIter->Size;
			// Dump info about the block
			debugf(TEXT("Memory block 0x%X with a size of %d (0x%X to 0x%X)"),
				pIter->pMem,pIter->Size,pIter->pMem,
				((DWORD)pIter->pMem + pIter->Size));
		}
		// scion sz - some basic info
		debugf( TEXT("") );
		debugf( TEXT("Contiguous mem totals:") );
		debugf( TEXT("max contiguous %d (%6.1f KB)"), InitialSize, InitialSize/1024.f );
		debugf( TEXT("free contiguous %d (%6.1f KB)"), TotalFreeBlockSize, TotalFreeBlockSize/1024.f );
		debugf( TEXT("used contiguous %d (%6.1f KB)"), (InitialSize-TotalFreeBlockSize), (InitialSize-TotalFreeBlockSize)/1024.f );
	}
	virtual void HeapCheck(void) {}

// Special contiguous allocation method
#ifndef FINAL_RELEASE
	virtual void* AlignedMalloc(DWORD Size,DWORD Align, const ANSICHAR* File = __FILE__, INT Line = __LINE__ );
#else
	virtual void* AlignedMalloc(DWORD Size,DWORD Align);
#endif
};

#endif