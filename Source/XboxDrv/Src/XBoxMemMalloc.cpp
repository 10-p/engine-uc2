#include "XboxDrv.h"
#include <xtl.h>


class FAllocMemTrack
{
public:

	FAllocMemTrack()
	{
		appMemzero(TypeTotals,sizeof(TypeTotals));
	}

	void AddByteCount( BYTE AllocID, DWORD NumBytes )
	{
		TypeTotals[AllocID] += NumBytes;
	}

	void RemoveByteCount( BYTE AllocID, DWORD NumBytes )
	{
		TypeTotals[AllocID] -= NumBytes;
	}

    void PrintTotals()
	{
		debugf( TEXT("=========================") );
		for( BYTE i=0; i < ARRAY_COUNT(TypeTotals); i++ )
		{
			debugf( TEXT("i=%d [%s] = %d total"), i, GetTypeName(i), TypeTotals[i] );
		}
	}

	static TCHAR* GetTypeName( BYTE AllocID )
	{
		static TCHAR* XboxTypes[] =
		{
			TEXT("eXALLOCAllocatorId_D3D8"),
			TEXT("eXALLOCAllocatorId_D3DX8"),
			TEXT("eXALLOCAllocatorId_DSOUND"),
			TEXT("eXALLOCAllocatorId_XAPI"),
			TEXT("eXALLOCAllocatorId_XACT"),
			TEXT("eXALLOCAllocatorId_XBOXKERNEL"),
			TEXT("eXALLOCAllocatorId_XBDM"),
			TEXT("eXALLOCAllocatorId_XGRAPHICS"),
			TEXT("eXALLOCAllocatorId_XONLINE"),
			TEXT("eXALLOCAllocatorId_XVOICE"),
			TEXT("eXALLOCAllocatorId_XHV"),
			TEXT("eXALLOCAllocatorId_USB"),
			TEXT("eXALLOCAllocatorId_XMV"),
			TEXT("eXALLOCAllocatorId_SHADERCOMPILER"),
			TEXT("eXALLOCAllocatorId_UIX")
		};
		if( AllocID < 128+ARRAY_COUNT(XboxTypes) )
		{
			return XboxTypes[AllocID-128];
		}
		else
		{
			return TEXT("UNKNOWN");
		}
	}

private:
	INT TypeTotals[255];
};

FAllocMemTrack GMemTrack;

//-----------------------------------------------------------------------------
// Name: XMemSize()
//
// Desc: The XMemSize function returns current size of address passed in.
//
// Arguments:
//      VOID* pAddress - Memory to check size
//      DWORD dwAllocAttributes - allocation attributes
//
// Return Value:
//      Memory size
//-----------------------------------------------------------------------------
SIZE_T WINAPI XMemSize( IN VOID* pAddress, IN DWORD dwAllocAttributes )
{
    // Not using our heap, use the default XMemSize method, which
    // is thread safe. The title is still responsible for making
    // thread-safe their own part of the custom allocator.
    return XMemSizeDefault( pAddress, dwAllocAttributes );
}

//-----------------------------------------------------------------------------
// Name: XMemAlloc()
//
// Desc: The XMemAlloc function calls our helper routine Alloc() to allocate
//       the specified number of bytes. Statistics tracking is also done here.
//
// Arguments:
//      SIZE_T dwSize - Size of memory to allocate
//      DWORD  dwAllocAttributes - allocation attributes
//
// Return Value:
//      Allocated memory or NULL.
//-----------------------------------------------------------------------------
VOID* WINAPI XMemAlloc( SIZE_T dwSize, DWORD dwAllocAttributes )
{
    VOID*              pvBuffer    = NULL;
	XALLOC_ATTRIBUTES* pAttributes = (XALLOC_ATTRIBUTES*)&dwAllocAttributes;

	/*
	if( pAttributes->dwMemoryType == XALLOC_MEMTYPE_PHYSICAL && 
		dwSize == 36864 )
	{
        debugf( TEXT("") );        
	}
	*/

	if( pAttributes->dwMemoryType == XALLOC_MEMTYPE_HEAP &&
		dwSize == 16 )
	{
		//debugf( TEXT("Type=%s"), FAllocMemTrack::GetTypeName(pAttributes->dwAllocatorId) );
		return XMemAllocDefault( dwSize, dwAllocAttributes );		
	}

    // Not using our heap, use the default XMemAlloc method, which
    // is thread safe. The title is still responsible for making
    // thread-safe their own part of the custom allocator.
    pvBuffer = XMemAllocDefault( dwSize, dwAllocAttributes );

    // Add statistics tracking here, utilizing dwSize and dwAllocAttributes.
    // Add your own thread safey if more than one thread allocs/frees memory.
    // For the sample we are simple tracking allocations and deallocations.

    return pvBuffer;
}




//-----------------------------------------------------------------------------
// Name: XMemFree()
// Desc: The XMemFree function allocates the specified number of bytes using
//       dwAllocType to determine where and how to allocate memory.
//
// Arguments:
//      VOID* pBaseAddress - Memory to free
//      DWORD dwAllocAttributes - allocation attributes
//-----------------------------------------------------------------------------
VOID WINAPI XMemFree( PVOID pBaseAddress, DWORD dwAllocAttributes )
{
    // Not using our heap, use the default XMemFree method, which
    // is thread safe. The title is still responsible for making
    // thread-safe their own part of the custom allocator.
    XMemFreeDefault( pBaseAddress, dwAllocAttributes );

    // Add statistics tracking here, utilizing dwSize and dwAllocAttributes.
    // Add your own thread safey if more than one thread allocs/frees memory.
    // For the sample we are simple tracking allocations and deallocations.

    return;
}
