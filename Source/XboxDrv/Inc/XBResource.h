//-----------------------------------------------------------------------------
// File: XBResource.h
//
// Desc: Loads resources from an XPR (Xbox Packed Resource) file.  
//
// Hist: 03.12.01 - New for April XDK release
//       10.15.02 - Modifed to register resources in a more flexible way
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//-----------------------------------------------------------------------------
#ifndef XBRESOURCE_H
#define XBRESOURCE_H

//jg -- Turn this on when you want to see all the contents of an XPR
//#define _DEBUGXPR

typedef BOOL UBOOL;

//jg -- Typedef for our UMA array data
typedef struct
{
	// The number of items in the array
	DWORD Count;
	// The offset into to the physical memory buffer that the array lives at
	DWORD Offset;
} FTUmaArrayHeader;

//-----------------------------------------------------------------------------
// Name: XBResource_SizeOf()
// Desc: Determines the byte size of a D3DResource
//-----------------------------------------------------------------------------
DWORD XBResource_SizeOf( LPDIRECT3DRESOURCE8 pResource );

//-----------------------------------------------------------------------------
// Name: struct XBRESOURCE
// Desc: Name tag for resources. An app may initialize this structure, and pass
//       it to the resource's Create() function. From then on, the app may call
//       GetResource() to retrieve a resource using an ascii name.
//-----------------------------------------------------------------------------
struct XBRESOURCE
{
    CHAR* strName;
    DWORD dwOffset;
};

//-----------------------------------------------------------------------------
// Name: class CXBPackedResource
// Desc: 
//-----------------------------------------------------------------------------
class CXBPackedResource
{
//jg -- Make everything public for my debug function
#ifdef _DEBUGXPR
public:
#else
protected:
#endif
    BYTE*       m_pSysMemData;        // Alloc'ed memory for resource headers etc.
    DWORD       m_dwSysMemDataSize;

    BYTE*       m_pVidMemData;        // Alloc'ed memory for resource data, etc.
    DWORD       m_dwVidMemDataSize;
 
    XBRESOURCE* m_pResourceTags;     // Tags to associate names with the resources
    DWORD       m_dwNumResourceTags; // Number of resource tags
	//jg -- Used to track the last searched resource tag. This is an
	// optimzation. Content is usually loaded in the same order as it
	// was exported, so this prevents us from having to needlessly search.
	DWORD m_dwLastFoundResource;
	//jg -- Hold onto the flags passed in for memory access changes
	BOOL m_bContainsPushBuffers;
	BOOL m_bNeedsCPUFastAccess;
	//jg -- If this is set, the memory doesn't have correct attributes set so
	// set them in the main thread not the loading one. This is a work around
	// to a stupid MS bug
	BOOL m_bNeedsDeferredPageSettings;

public:
    // Loads the resources out of the specified bundle
    HRESULT Create( const CHAR* strFilename,
		//jg -- Needs to be true if we are loading pushbuffers
		BOOL bContainsPushBuffers = FALSE,
		// jg -- Needs to be true if we are accessing this data with the CPU
		BOOL bNeedsCPUFastAccess = FALSE,
		DWORD dwNumResourceTags = 0L,XBRESOURCE* pResourceTags = NULL );

    VOID Destroy();

    // Retrieves the resource tags
    HRESULT GetResourceTags( DWORD* pdwNumResourceTags, XBRESOURCE** ppResourceTags );

    // Helper function to make sure a resource is registered
    LPDIRECT3DRESOURCE8 RegisterResource( LPDIRECT3DRESOURCE8 pResource )
    {
        // Register the resource, if it has not yet been registered. We mark
        // a resource as registered by upping it's reference count.
        if( pResource && ( pResource->Common & D3DCOMMON_REFCOUNT_MASK ) == 1 )
        {
            // Special case CPU-copy push buffers (which live in system memory)
            if( ( pResource->Common & D3DCOMMON_TYPE_PUSHBUFFER ) &&
                ( pResource->Common & D3DPUSHBUFFER_RUN_USING_CPU_COPY ) )
                pResource->Data += (DWORD)m_pSysMemData;
            else
                pResource->Register( m_pVidMemData );

            pResource->AddRef();
        }
        return pResource;
    }

    // Functions to retrieve resources by their offset
    inline VOID* GetData( DWORD dwOffset )
    {
		return &m_pSysMemData[dwOffset];
	}

    inline LPDIRECT3DRESOURCE8 GetResource( DWORD dwOffset )
    {
		return RegisterResource( (LPDIRECT3DRESOURCE8)GetData(dwOffset) );
	}

    inline LPDIRECT3DTEXTURE8 GetTexture( DWORD dwOffset )
    {
		return (LPDIRECT3DTEXTURE8)GetResource( dwOffset );
	}

    inline LPDIRECT3DCUBETEXTURE8 GetCubemap( DWORD dwOffset )
    {
		return (LPDIRECT3DCUBETEXTURE8)GetResource( dwOffset );
	}

    // Functions to retrieve resources by their name
    VOID* GetData( const CHAR* strName );

    inline LPDIRECT3DRESOURCE8 GetResource( const CHAR* strName )
    {
		return RegisterResource( (LPDIRECT3DRESOURCE8)GetData( strName ) );
	}

    inline LPDIRECT3DTEXTURE8 GetTexture( const CHAR* strName )
    {
		return (LPDIRECT3DTEXTURE8)GetResource( strName );
	}

    inline LPDIRECT3DCUBETEXTURE8 GetCubemap( const CHAR* strName )
    {
		return (LPDIRECT3DCUBETEXTURE8)GetResource( strName );
	}

	/**
	 * Finds the UMA array data for specified array
	 */
	inline BOOL GetTUmaArray(const CHAR* strName,INT& Count,void*& Data)
	{
		// Find the FTUmaArrayHeader for this item
		FTUmaArrayHeader* Header = (FTUmaArrayHeader*)GetData(strName);
		if (Header != NULL)
		{
			// Now fix up the out params
			Count = Header->Count;
			// Get a pointer to the actual data
			Data = &m_pVidMemData[Header->Offset];
		}
		return Header != NULL;
	}

    /**
	 * Finds the pushbuffer and registers it for use
	 */
	inline FPushBuffer* GetPushbuffer( const CHAR* strName )
    {
		FPushBuffer* PushBuffer = NULL;
		// See if this xpr contains the data
		XPRPushBuffer* Data = (XPRPushBuffer*)GetData(strName);
		if (Data != NULL)
		{
			extern FPushBuffer* ExternalFixup(XPRPushBuffer*,XBRESOURCE&,void*,void*);
			// Use an external fixup because the includes for this are stupid
			PushBuffer = ExternalFixup(Data,m_pResourceTags[m_dwLastFoundResource],
				m_pSysMemData,m_pVidMemData);
		}
		return PushBuffer;
	}

	// Constructor/destructor
    CXBPackedResource();
    ~CXBPackedResource();

	inline DWORD GetSysMemSize(void) { return m_dwSysMemDataSize; }
	inline DWORD GetVidMemSize(void) { return m_dwVidMemDataSize; }
};

#ifdef _DEBUGXPR
void DebugXPR(CXBPackedResource* XPR);
#endif

#endif XBRESOURCE_H
