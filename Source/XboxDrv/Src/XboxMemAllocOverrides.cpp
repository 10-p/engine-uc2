/* scion ======================================================================
 * Author: sz
 * ============================================================================
 *
 * Overrides for any xtl memory allocations.
 *
 * Assumes that GMalloc and GContigMalloc have already been created and
 * initialized.
 *
 * ============================================================================
 */

#include <xtl.h>
#include "Core.h"
#include "Engine.h"

#define ALLOC_HEADER_SIZE 8
#define MAX_HEAP_ALIGNMENT XALLOC_ALIGNMENT_4

/*=============================================================================
 * Name: IsHeapOverrideID()
 *
 * Desc: xtl lib allocator ids that we want to override for heap allocs
 *
 * Arguments:
 *      AllocatorId 
 *
 * Return Value:
 *      True or False
 *=============================================================================
 */
FORCEINLINE UBOOL IsHeapOverrideID( BYTE AllocatorId )
{
	switch ( AllocatorId )
	{
	case eXALLOCAllocatorId_D3D8:
	case eXALLOCAllocatorId_D3DX8:
	case eXALLOCAllocatorId_DSOUND:
//	case eXALLOCAllocatorId_XAPI:
	case eXALLOCAllocatorId_XACT:
	case eXALLOCAllocatorId_XBOXKERNEL:
	case eXALLOCAllocatorId_XBDM:
	case eXALLOCAllocatorId_XGRAPHICS:
	case eXALLOCAllocatorId_XONLINE:
	case eXALLOCAllocatorId_XVOICE:
	case eXALLOCAllocatorId_XHV:
	case eXALLOCAllocatorId_USB:
	case eXALLOCAllocatorId_XMV:
	case eXALLOCAllocatorId_SHADERCOMPILER:
	case eXALLOCAllocatorId_UIX:		
		return 1;
	};
	return 0;
}

/*=============================================================================
 * Name: IsPhysicalOverrideID()
 *
 * Desc: xtl lib allocator ids that we want to override for physical allocs
 *
 * Arguments:
 *      AllocatorId
 *
 * Return Value:
 *      True or False
 *=============================================================================
 */
FORCEINLINE UBOOL IsPhysicalOverrideID( BYTE AllocatorId )
{
	return ( AllocatorId == eXALLOCAllocatorId_XMV );
}

/*=============================================================================
 * Name: GetPhysicalAlignment()
 *
 * Desc: Number of bytes of alignment needed for alignmentID used by physical
 * allocation.
 *
 * Arguments:
 *      AlignmentId
 *
 * Return Value:
 *      byte alignment value
 *=============================================================================
 */
FORCEINLINE DWORD GetPhysicalAlignment( DWORD AlignmentId )
{
	if( AlignmentId == XALLOC_PHYSICAL_ALIGNMENT_DEFAULT ||
		AlignmentId == XALLOC_PHYSICAL_ALIGNMENT_4K_BELOW_16M ) {
			return 4096;
		}
    
	return ( 0x1 << AlignmentId );
}

/*=============================================================================
 * Name: XMemSize()
 *
 * Desc: The XMemSize function returns current size of address passed in.
 *
 * Arguments:
 *      VOID* pAddress = Memory to check size
 *      DWORD dwAllocAttributes = allocation attributes
 *
 * Return Value:
 *      Memory size
 *=============================================================================
 */
SIZE_T WINAPI XMemSize( IN VOID* pAddress, IN DWORD dwAllocAttributes )
{
	XALLOC_ATTRIBUTES* pAttributes = (XALLOC_ATTRIBUTES*)&dwAllocAttributes;

	if( pAttributes->dwMemoryType == XALLOC_MEMTYPE_HEAP &&
		pAttributes->dwAlignment == MAX_HEAP_ALIGNMENT &&
		IsHeapOverrideID(pAttributes->dwAllocatorId) )
	{
		return *(DWORD*)(((BYTE*)pAddress) - ALLOC_HEADER_SIZE);
	}
// TODOSZ - may not need this
#if defined(USE_GCONTIG) && 0
	else 
	if( pAttributes->dwMemoryType == XALLOC_MEMTYPE_PHYSICAL &&
		pAttributes->dwAlignment != XALLOC_PHYSICAL_ALIGNMENT_4K_BELOW_16M &&
		IsPhysicalOverrideID(pAttributes->dwAllocatorId) )
	{
		FPhyscialAllocHeader* pHeader = PhysAllocMap.Find( pAddress );
		check(pHeader);
		return pHeader->dwSize;
	}
#endif

	// use default
    return XMemSizeDefault( pAddress, dwAllocAttributes );
}

/*=============================================================================
 * Name: XMemAlloc()
 *
 * Desc: The XMemAlloc function calls our GMalloc=>Malloc() to allocate
 *       the specified number of bytes. 
 *
 * Arguments:
 *      SIZE_T dwSize = Size of memory to allocate
 *      DWORD  dwAllocAttributes = allocation attributes
 *
 * Return Value:
 *      Allocated memory or NULL.
 *=============================================================================
 */
VOID* WINAPI XMemAlloc( SIZE_T dwSize, DWORD dwAllocAttributes )
{
    VOID*              pvBuffer    = NULL;
	XALLOC_ATTRIBUTES* pAttributes = (XALLOC_ATTRIBUTES*)&dwAllocAttributes;

	check(dwAllocAttributes);

	if( pAttributes->dwMemoryType == XALLOC_MEMTYPE_HEAP &&
		pAttributes->dwAlignment == MAX_HEAP_ALIGNMENT &&
		IsHeapOverrideID(pAttributes->dwAllocatorId) )
	{
		DWORD* pHeader = (DWORD*)GMalloc->Malloc( ALLOC_HEADER_SIZE + dwSize );
		pvBuffer = (VOID*)( ((BYTE*)pHeader) + ALLOC_HEADER_SIZE );
		if( pAttributes->dwZeroInitialize )
		{
			appMemzero( pvBuffer, dwSize );
		}
		*(pHeader) = dwSize;
		*(pHeader+1) = dwAllocAttributes;
	}
// TODOSZ - may not need this
#if defined(USE_GCONTIG) && 0
	else 
	if( pAttributes->dwMemoryType == XALLOC_MEMTYPE_PHYSICAL &&
		pAttributes->dwAlignment != XALLOC_PHYSICAL_ALIGNMENT_4K_BELOW_16M &&
		IsPhysicalOverrideID(pAttributes->dwAllocatorId) )
	{
		pvBuffer = GContigMalloc->AlignedMalloc( dwSize, GetPhysicalAlignment(pAttributes->dwAlignment) );
		PhysAllocMap.Add( pvBuffer, FPhyscialAllocHeader(dwSize,dwAllocAttributes) );
	}
#endif
	else
	{
		// use default
		pvBuffer = XMemAllocDefault( dwSize, dwAllocAttributes );
	}

    check( !pAttributes->dwMustSucceed || (pAttributes->dwMustSucceed && pvBuffer != NULL) );
    return pvBuffer;
}

/*=============================================================================
 * Name: XMemFree()
 * Desc: The XMemFree function allocates the specified number of bytes using
 *       dwAllocType to determine where and how to allocate memory.
 *
 * Arguments:
 *      VOID* pBaseAddress = Memory to free
 *      DWORD dwAllocAttributes = allocation attributes
 *=============================================================================
 */
VOID WINAPI XMemFree( PVOID pBaseAddress, DWORD dwAllocAttributes )
{
	XALLOC_ATTRIBUTES* pAttributes = (XALLOC_ATTRIBUTES*)&dwAllocAttributes;

	if( pAttributes->dwMemoryType == XALLOC_MEMTYPE_HEAP &&
		pAttributes->dwAlignment == MAX_HEAP_ALIGNMENT &&
		IsHeapOverrideID(pAttributes->dwAllocatorId) )
	{
		GMalloc->Free( ((BYTE*)pBaseAddress) - ALLOC_HEADER_SIZE );		
	}
// TODOSZ - may not need this
#if defined(USE_GCONTIG) && 0
	else
	if( pAttributes->dwMemoryType == XALLOC_MEMTYPE_PHYSICAL &&
		pAttributes->dwAlignment != XALLOC_PHYSICAL_ALIGNMENT_4K_BELOW_16M &&
		IsPhysicalOverrideID(pAttributes->dwAllocatorId) )
	{
		GContigMalloc->Free( pBaseAddress );
		PhysAllocMap.Remove( pBaseAddress );
	}
#endif
	else
	{
		// use default
		XMemFreeDefault( pBaseAddress, dwAllocAttributes );
	}

    return;
}

