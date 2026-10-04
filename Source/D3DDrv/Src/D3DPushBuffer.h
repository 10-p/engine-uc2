#include "PushBuffer.h"

// The following was taken from D3D8Types.h so it can be used without including
// all of the D3D headers

// Xbox-specific defines, taken from d3d8types-xbox.h and d3d8-xbox.h
#if !defined(_XBOX) && defined(XBOX_ASSET_FORMAT)

#define D3DPUSHBUFFER_ALIGNMENT     4

#define D3DCOMMON_TYPE_SHIFT         16
#define D3DCOMMON_TYPE_VERTEXBUFFER  0x00000000
#define D3DCOMMON_TYPE_INDEXBUFFER   0x00010000
#define D3DCOMMON_TYPE_PUSHBUFFER    0x00020000
#define D3DCOMMON_TYPE_PALETTE       0x00030000
#define D3DCOMMON_TYPE_TEXTURE       0x00040000
#define D3DCOMMON_TYPE_SURFACE       0x00050000
#define D3DCOMMON_VIDEOMEMORY        0x00800000

// For small pushbuffers
#define D3DPUSHBUFFER_RUN_USING_CPU_COPY 0x80000000

// Pushbuffer Format
typedef struct 
{
    DWORD Common;
    DWORD Data;
    DWORD Lock;
	// Size, in bytes, of the push-buffer program.
    DWORD Size;
    // Size, in bytes, of the allocation of the buffer pointed to by 'Data'.
    DWORD AllocationSize;
    // Internal D3D-only field that identifies the interrupt that the fix-up 
    // will be done on for the most recent RunPushBuffer call when using 
    // non-COPY push-buffers.
    DWORD InterruptId;
	void Register(void* InData)
	{
		Data = (DWORD)InData;
	}
	bool IsBusy(void) { return false; }
	void AddRef(void) { }
} D3DPushBuffer;

typedef enum _D3DPRIMITIVETYPE {
    D3DPT_POINTLIST             = 1,
    D3DPT_LINELIST              = 2,
    D3DPT_LINELOOP              = 3,  // Xbox extension
    D3DPT_LINESTRIP             = 4,
    D3DPT_TRIANGLELIST          = 5,
    D3DPT_TRIANGLESTRIP         = 6,
    D3DPT_TRIANGLEFAN           = 7,
    D3DPT_QUADLIST              = 8,  // Xbox extension
    D3DPT_QUADSTRIP             = 9,  // Xbox extension
    D3DPT_POLYGON               = 10, // Xbox extension

    D3DPT_MAX                   = 11,
    D3DPT_FORCE_DWORD           = 0x7fffffff, /* force 32-bit size enum */
} D3DPRIMITIVETYPE;

typedef DWORD HRESULT;
typedef DWORD UINT;
typedef unsigned short WORD;

extern "C" {
HRESULT __stdcall XGCompileDrawIndexedVertices(
  void *pBuffer,
  DWORD *pSize,
  D3DPRIMITIVETYPE PrimitiveType,
  UINT VertexCount,
  const WORD *pIndexData
  );}

#endif

#ifndef _XBOX
D3DPRIMITIVETYPE MapPrimitiveType(EPrimitiveType Prim)
{
	return Prim == PT_TriangleList ? D3DPT_TRIANGLELIST : D3DPT_TRIANGLESTRIP;
}
#else
// There is no mapping to do on the xbox
FORCEINLINE D3DPRIMITIVETYPE MapPrimitiveType(EPrimitiveType Prim)
{
	return (D3DPRIMITIVETYPE)Prim;
}
#endif

/* scion ======================================================================
 * Author: jg
 * 
 * This class is a wrapper around a D3DPushBuffer. It handles allocation and
 * cleanup of the internal push buffer data. It also sets the various push
 * buffer header stuff.
 *
 * ============================================================================
 */
class D3DDRV_API FD3DPushBuffer :
	// This interface is so that Engine can get to various members without
	// having any knowledge of D3DPushBuffer
	public FPushBuffer
{
public:
	D3DPushBuffer PushBuffer;
	// Go ahead and hang onto the push buffer data pointer
	void* pPBData;

	/**
	 * Used with pushbuffers loaded from an XPR file. Initializes things based
	 * off the XPR data and the data pointer
	 *
	 * @param InPB the pushbuffer data from the XPR file
	 * @param Data the pointer to use as the PB data
	 */
	FD3DPushBuffer(XPRPushBuffer& InPB,void* Data)
	{
		// Marking this pointer as NULL tells the object not to delete the
		// data on cleanup since it comes from an XPR
		pPBData = NULL;
		appMemzero(&PushBuffer,sizeof(D3DPushBuffer));
		// Copy over the pertinent data
		PushBuffer.Common = InPB.Common;
		PushBuffer.Size = InPB.Size;
		PushBuffer.AllocationSize = InPB.AllocatedSize;
		// Link up the PB data
		PushBuffer.Register(Data);
		// Add ref so it doesn't get whacked on us
		PushBuffer.AddRef();
	}

	// Sets all the header stuff up, allocates the push buffer data member.
	FD3DPushBuffer(DWORD dwSize)
	{
		pPBData = NULL;
		appMemzero(&PushBuffer,sizeof(D3DPushBuffer));
		// Init header info
		PushBuffer.Common = 1 | D3DCOMMON_TYPE_PUSHBUFFER;
		PushBuffer.Size = dwSize;
		PushBuffer.AllocationSize = dwSize;
		// If it is of a runnable size, then allocate contig memory
		if (dwSize >= MIN_RUNNABLE_SIZE)
		{
			// Allocate this in contiguous RAM
			pPBData = appContiguousMalloc(dwSize,D3DPUSHBUFFER_ALIGNMENT);
#ifdef _XBOX
			// Keep track our counts
			STAT_INC(GEngineStats.STATS_Cached_PushBuffer_Runnable);
			STAT_INC_BY(GEngineStats.STATS_Cached_PushBuffer_RunnableMem,
				dwSize + sizeof(FD3DPushBuffer));
#endif
		}
		else
		{
			// Push buffers must be a minimum of 660 bytes
			DWORD dwRealSize = Max<DWORD>(dwSize,MIN_ALLOC_SIZE);
			// Indicate a copy not a runnable one
			PushBuffer.Common |= D3DPUSHBUFFER_RUN_USING_CPU_COPY;
			// Allocate this in system RAM
			pPBData = appMalloc(dwRealSize);
			// If these don't match we need to place NOPs into the stream
			if (dwRealSize != dwSize)
			{
				// Figure out where they are going to write their last
				// command and place NOPs after that
				DWORD* pdwBuffer = (DWORD*)((BYTE*)pPBData + dwSize);
				// This places a NOP into the push buffer
				*pdwBuffer++ = 0x42700100;
				// Zero all data after that
				appMemzero(pdwBuffer,dwRealSize -
					((BYTE*)pdwBuffer - (BYTE*)pPBData));
			}
			PushBuffer.AllocationSize = dwRealSize;
#ifdef _XBOX
			// Keep track our counts
			STAT_INC(GEngineStats.STATS_Cached_PushBuffer_RunAsCopy);
			STAT_INC_BY(GEngineStats.STATS_Cached_PushBuffer_RunAsCopyMem,
				dwRealSize + sizeof(FD3DPushBuffer));
#endif
		}
		// Link up the PB data
		PushBuffer.Register(pPBData);
	}

	// Handles cleaning up of the allocated push buffer data
	virtual ~FD3DPushBuffer()
	{
#ifdef _XBOX
		// Block until this pushbuffer isn't used
		PushBuffer.BlockUntilNotBusy();
#endif
		if (pPBData != NULL)
		{
			if (PushBuffer.Size >= MIN_RUNNABLE_SIZE)
			{
				// Free the contiguous RAM
				appContiguousFree(pPBData);
#ifdef _XBOX
				// Keep track our counts
				STAT_DEC(GEngineStats.STATS_Cached_PushBuffer_Runnable);
				STAT_DEC_BY(GEngineStats.STATS_Cached_PushBuffer_RunnableMem,
					PushBuffer.Size + sizeof(FD3DPushBuffer));
#endif
			}
			else
			{
#ifdef _XBOX
				// Keep track our counts
				STAT_DEC(GEngineStats.STATS_Cached_PushBuffer_RunAsCopy);
				STAT_DEC_BY(GEngineStats.STATS_Cached_PushBuffer_RunAsCopyMem,
					PushBuffer.AllocationSize + sizeof(FD3DPushBuffer));
#endif
				// Free the non-contiguous ram
				appFree(pPBData);
			}
		}
	}
	
// FPushBuffer interface

	// Determines how large the pushbuffer is
	virtual DWORD GetSize(void) { return PushBuffer.Size; }
	// Determines how much memory the pushbuffer is using (runascopy)
	virtual DWORD GetAllocatedSize(void) { return PushBuffer.AllocationSize; }
	// Gets access to the push buffer data member for writing into it
	virtual void* GetPushBufferData(void) { return pPBData; }
	// Indicates whether this is a run as copy or not
	virtual UBOOL IsRunAsCopy(void)
		{ return PushBuffer.Common & D3DPUSHBUFFER_RUN_USING_CPU_COPY; }
	// Fetches the push buffer header
	virtual void* GetPushBuffer(void) { return (void*)&PushBuffer; }
	// Determines if the push buffer is being used or not
	virtual UBOOL IsBusy(void) { return PushBuffer.IsBusy(); }
	// Whether this is in a XPR or not
	virtual UBOOL IsInXPR(void) { return pPBData == NULL; }
	// scion sz - serializer to account for static push buffer memory
	virtual void Serialize( FArchive& Ar )
	{
		DWORD dwRealSize = PushBuffer.AllocationSize;
		if( IsRunAsCopy() )
		{
			// only virtual mem used
			Ar.CountBytes( dwRealSize, dwRealSize );            
		}
		else
		{
			// only contiguous mem used
			Ar.CountBytes( dwRealSize, dwRealSize, dwRealSize );
		}		
	}
};

/* scion ======================================================================
 * appAllocPushBuffer
 * Author: jg
 * 
 * Allocates a new push buffer. This function allows things outside of D3DDrv
 * to create push buffers without having to know about D3DPushBuffer.
 *
 * input:	dwSize DWORD The size of the push buffer to create
 *
 * returns: A new FPushBuffer object if it could be created
 *
 * ============================================================================
 */
FORCEINLINE FPushBuffer* appAllocPushBuffer(DWORD dwSize)
{
	return NEW FD3DPushBuffer(dwSize);
}

/* scion ======================================================================
 * appCompileIndicesToPushBuffer
 * Author: jg
 * 
 * Compiles the indices into a runnable push buffer. Performs all allocations.
 *
 * input:	Prim EPrimitiveType The primitive type to encode
 *			dwVertCount DWORD The number of verts being encoded
 *			pIndices const _WORD* The array of indices to write to the pb
 *			bRejectRunAsCopy UBOOL If true no pb is created for a RunAsCopy PB
 *
 * returns: A new FPushBuffer object if it could be created
 *
 * ============================================================================
 */
FPushBuffer* appCompileIndicesToPushBuffer(EPrimitiveType Prim,
	DWORD dwVertCount,const _WORD* pIndices,UBOOL bRejectRunAsCopy)
{
	guard(appCompileIndicesToPushBuffer);
	// Default to not creating
	FPushBuffer* pCreated = NULL;
	DWORD dwNumBytes = 0;
	// First have it tell us how big a buffer it needs
	XGCompileDrawIndexedVertices(NULL,&dwNumBytes,MapPrimitiveType(Prim),
		dwVertCount,pIndices);
	// If it is large enough or we are taking run as copy buffers,
	// then create and fill the push buffer
	if (dwNumBytes >= MIN_RUNNABLE_SIZE || bRejectRunAsCopy == 0)
	{
		// Create the new object
		pCreated = appAllocPushBuffer(dwNumBytes);
		// Now compile the indices into the buffer
		XGCompileDrawIndexedVertices(pCreated->GetPushBufferData(),
			&dwNumBytes,MapPrimitiveType(Prim),dwVertCount,pIndices);
		// If it is a runnable buffer it must be set to write combined
		if (pCreated->IsRunAsCopy() == 0)
		{
			// Make this writable and writecombined
			appMemCPUFastAccess(pCreated->GetPushBufferData(),
				pCreated->GetSize(),0x404);
		}
	}
	return pCreated;
	unguard;
}

