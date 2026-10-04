/*=============================================================================
	D3DRenderDevice.h: Unreal Direct3D render device definition.
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/

#ifndef HEADER_D3DRENDERDEVICE
#define HEADER_D3DRENDERDEVICE

#include "D3DShaderConstants.h"

#ifdef _XBOX
// scion sz - d3d fixed function pipeline is never used when this is defined
#define VERTEXSHADERS_ALWAYS_ON
#endif 

#define AUTO_INITIALIZE_REGISTRANTS_D3DDRV UD3DRenderDevice::StaticClass();

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * This is a generic class for caching D3D resources. It creates a unique key
 * for each item stored, minimizing hash collisions. It also separates out each
 * type of cached item into its own instance, allowing the key to be smaller and
 * again minimizing hash collisions. Note: cache IDs are reused if an item that
 * was flushed resulted in a slot being completely open. This keeps temporary
 * items from causing longer than necessary search times.
 *
 * ============================================================================
 */
template<class TYPE,_WORD wNumSlots = 1024> class TResourceCache
{
	// Slots that have become available due to Flush()ing
	TArray<_WORD> SlotsAvailable;
	// Contains a hash table of items to store
	TYPE* ResourceHash[wNumSlots];
	// The next cache ID to use
	_WORD NextCacheID;

public:
	// Simple constructor
	TResourceCache()
	{
		Reset();
	}

   /* scion ======================================================================
	* TResourceCache::GetCachedResource
	* Author: jg
	* 
	* Finds the cached resource item. Determines which slot the resource is in and
	* then searches the hash chain for the correct item. Returns NULL if the cache
	* ID is bogus.
	*
	* input:	CacheID The cache ID of the item to find
	*
	* ============================================================================
	*/
	FORCEINLINE TYPE* GetCachedResource(_WORD CacheID)
	{
		// Find the first item in the chain at this slot
		TYPE* pResource = ResourceHash[CacheID % wNumSlots];
		// Iterate through the hash chain if needed
		while (pResource && pResource->CacheId != CacheID)
		{
			pResource = (TYPE*)pResource->HashNext;
		}
		return pResource;
	}

   /* scion ======================================================================
	* TResourceCache::Cache
	* Author: jg
	* 
	* Adds an item to the resource hash and increments the next hash value. This
	* function also assigns the CacheID of the resource.
	*
	* input:	pResource The resource that needs to be cached
	*
	* ============================================================================
	*/
	void Cache(TYPE* pResource)
	{
		// Verify that we haven't blown our max
		checkSlow(NextCacheID < INVALID_CACHEID);
		// Reuse a completely open slot if found
		_WORD NewCacheID = INVALID_CACHEID;
		if (SlotsAvailable.Num() == 0)
		{
			// No reusable slots present so take the next CacheID and move to
			// the next one
			NewCacheID = NextCacheID++;
		}
		else
		{
			// Get the more recently freed slot
			INT Index = SlotsAvailable.Num() - 1;
			// Get the cache ID of that slot
			NewCacheID = SlotsAvailable(Index);
			// Remove from the end to avoid memory thrashing
			SlotsAvailable.Remove(Index);
		}
		check(NewCacheID < INVALID_CACHEID);
		// Assign the selected cache ID
		pResource->CacheId = NewCacheID;
		// Add this resource to the chain
		TYPE* pChain = ResourceHash[pResource->CacheId % wNumSlots];
		// If the slot is currently empty, make this resource own it
		if (pChain == NULL)
		{
			ResourceHash[pResource->CacheId % wNumSlots] = pResource;
		}
		else
		{
			// Find the end of the chain
			while (pChain->HashNext != NULL)
			{
				pChain = (TYPE*)pChain->HashNext;
			}
			// Add this resource to the end
			pChain->HashNext = pResource;
		}
	}

   /* scion ======================================================================
	* TResourceCache::Flush
	* Author: jg
	* 
	* Removes an item from the resource hash.
	*
	* input:	pResource The resource that needs to be flushed
	*
	* ============================================================================
	*/
	void Flush(_WORD CacheID)
	{
		// Find the first item in the chain at this slot
		TYPE* pResource = ResourceHash[CacheID % wNumSlots];
		// Iterate through the hash chain if needed
		while (pResource && pResource->HashNext &&
			pResource->HashNext->CacheId != CacheID)
		{
			pResource = (TYPE*)pResource->HashNext;
		}
		// Make sure this isn't a double flush
		if (pResource != NULL)
		{
			// Check to see if we are the only item in the slot
			if (pResource->HashNext == NULL)
			{
				ResourceHash[CacheID % wNumSlots] = NULL;
				// Since this slot is now empty, make it available for reuse
				SlotsAvailable.AddItem(CacheID);
			}
			else
			{
				// Link this resource to the one following our match
				pResource->HashNext = pResource->HashNext->HashNext;
			}
		}
	}

   /* scion ======================================================================
	* TResourceCache::DumpHashInfo
	* Author: jg
	* 
	* Dumps out statistics about the resource cache
	*
	* input:	Ar The archive to write information to
	*
	* ============================================================================
	*/
	void DumpHashInfo(FOutputDevice& Ar)
	{
		INT nSlotsInUse = 0;
		INT nTotalCollisions = 0;
		INT nMinCollisions = 0;
		INT nMaxCollisions = 0;
		// Work through each slot and figure out how many collisions
		for (_WORD wCurrSlot = 0; wCurrSlot < wNumSlots; wCurrSlot++)
		{
			INT nCollisions = 0;
			// Get the first item in the slot
			TYPE* pResource = ResourceHash[wCurrSlot];
			// Determine if this slot is being used
			if (pResource != NULL)
			{
				// This slot is in use
				nSlotsInUse++;
				// Now count how many hash collisions there are
				while (pResource)
				{
					pResource = (TYPE*)pResource->HashNext;
					// If there is another item in the chain
					if (pResource != NULL)
					{
						// Then increment the number of collisions
						nCollisions++;
					}
				}
				// Keep the global stats
				nTotalCollisions += nCollisions;
				nMaxCollisions = Max<INT>(nCollisions,nMaxCollisions);
				nMinCollisions = Min<INT>(nCollisions,nMinCollisions);
				// Now log the output
				Ar.Logf(TEXT("\tSlot %d has %d collisions"),(INT)wCurrSlot,
					nCollisions);
			}
		}
		// Dump how many slots were in use
		Ar.Logf(TEXT("Slots in use %d"),nSlotsInUse);
		// Now dump how efficient the hash is
		Ar.Logf(TEXT("Collision Stats: Best Case (%d), Average Case (%d), Worst Case (%d)"),
			nMinCollisions,appFloor(((FLOAT)nTotalCollisions / (FLOAT)wNumSlots)),
			nMaxCollisions);
	}

   /* scion ======================================================================
	* TResourceCache::Reset
	* Author: jg
	* 
	* Reinitializes the hash
	*
	* ============================================================================
	*/
	void Reset(void)
	{
		// Start the caching from zero
		NextCacheID = 0;
		// Make all pointers initially NULL
		appMemzero(ResourceHash,sizeof(ResourceHash));
		// Empty all the avail slots
		SlotsAvailable.Empty();
	}
};

#ifdef _XBOX
//scion jg -- Special GHE debugging macros
	#ifndef FINAL_RELEASE
		#define SINGLESTEPPUSHER { \
			if (RenDev->UseSSP) \
			{ \
				extern ANSICHAR* GSSPFile; \
				extern INT GSSPLineNo; \
				GSSPFile = __FILE__; \
				GSSPLineNo = __LINE__; \
				RenDev->Direct3DDevice8->BlockUntilIdle(); \
			} \
		}
		#define RI_SINGLESTEPPUSHER { \
			if (RI->RenDev->UseSSP) \
			{ \
				extern ANSICHAR* GSSPFile; \
				extern INT GSSPLineNo; \
				GSSPFile = __FILE__; \
				GSSPLineNo = __LINE__; \
				RI->RenDev->Direct3DDevice8->BlockUntilIdle(); \
			} \
		}
	#else
		#define SINGLESTEPPUSHER
		#define RI_SINGLESTEPPUSHER
	#endif
#else
	#define SINGLESTEPPUSHER
	#define RI_SINGLESTEPPUSHER
#endif

//
//	UD3DRenderDevice
//
class D3DDRV_API UD3DRenderDevice : public URenderDevice
{
	DECLARE_CLASS(UD3DRenderDevice,URenderDevice,CLASS_Config,D3DDrv);
public:

	// Resource management.
	FD3DResource*					ResourceList;

	//scion jg -- New resource hashing scheme 
	TResourceCache<FD3DTexture,128> TextureCache;
	TResourceCache<FD3DVertexStream,256> VertexBufferCache;
	TResourceCache<FD3DIndexBuffer,16> IndexBufferCache;

	// Vertex shader management
	// scion sz - individual vertex shader pieces (see EVertexShaderCodeFragment).  
	// These are combined to create each of the FD3DCombinedVertexShader(s).
	FD3DVertexShaderFragments				ShaderCodeFragments;
	// scion sz - each entry in this map<> corresponds to an enumerated 
	// vertex shader combination (see EVertexShader for possible combinations)
	TMap<QWORD, FD3DCombinedVertexShader*>	CombinedVertexShaders;
	// scion sz - for xbox these are just vertex format declarations. 
	// For PC they also include the shader program.
	FD3DVertexShader*						VertexShaders;
	// scion sz - container for possible vertex shader configurations that we 
	// might need. used for precaching of vertex shader microcode.
	UVertexShaderConfigs*					VertexShaderConfigs;
	// scion sz - container for possible vertex shader configurations that we might need. 
	// used for precaching of vertex shader microcode.
	UUserVertexShaderConfigs*				UserVertexShaderConfigs;

	// Pixel shader management
	TArray<FD3DPixelShader*>				PixelShaders;			

	FD3DDynamicVertexStream*		DynamicVertexStream;
	FD3DDynamicIndexBuffer*			DynamicIndexBuffer16;
	FD3DDynamicIndexBuffer*			DynamicIndexBuffer32;

	// Default frame buffer
	FDefaultRenderTarget*			DefaultRenderTarget;

	// Configuration.
	UBOOL							UsePrecaching,
									UseTrilinear,
									UseMipmapping,
									UseVSync,
									UseHardwareTL,
									UseHardwareVS,
									UseCubemaps,
									UseMippedCubemaps,
									UseDXT1,
									UseDXT3,
									UseDXT5,
									UseA8,
									UseTripleBuffering,
									ReduceMouseLag,
									UseXBoxFSAA,
									IsKyro,
									IsGeForce,
									UseVertexFog,
									UseRangeFog,
									CheckForOverflow,
									UseNPatches,
									DecompressTextures,
									AvoidHitches,
									OverrideDesktopRefreshRate,
									HasNVCubemapBug,
#ifdef _XBOX
#ifndef FINAL_RELEASE
									UseSSP,
#endif
#endif
									UsePCSkeletalShader,
									UseXBOXSkeletalShader;
	INT								AdapterNumber,
									FirstColoredMip,
									MaxPixelShaderVersion,
									LevelOfAnisotropy;
    FLOAT                           DetailTexMipBias,
									DefaultTexMipBias,
									TesselationFactor;
	DWORD							DesiredRefreshRate,
									UsedLevelOfAnisotropy;

	UBOOL							bInitialized;

	// Direct3D device info.
    D3DCAPS8						DeviceCaps8;
	D3DADAPTER_IDENTIFIER8			DeviceIdentifier;
	_WORD							wProduct,
									wVersion,
									wSubVersion,
									wBuild;
	TArray<D3DDISPLAYMODE>			DisplayModes;
	D3DFORMAT						BackBufferFormat;
	D3DFORMAT						DepthBufferFormat;
	D3DTEXTUREADDRESS				CubemapTextureAddressing;

	INT								MaxResWidth,
									MaxResHeight,
									PixelShaderVersion;	// 11 == 1.1, 14 == 1.4

	DWORD							InitialTextureMemory;

	TArray<D3DADAPTER_IDENTIFIER8>	Adapters;
	INT								BestAdapter;

	// Direct3D device state.
	UBOOL							ForceReset,
									CurrentFullscreen;
	UViewport*						LockedViewport;
	INT								CurrentColorBytes,
									FullScreenWidth,
									FullScreenHeight,
									FullScreenRefreshRate,
									FrameCounter,
									DesktopColorBits;

	// Direct3D interfaces.
    IDirect3D8*						Direct3D8;
    IDirect3DDevice8*				Direct3DDevice8;
	D3DPRESENT_PARAMETERS			PresentParms;

	// Editor specific stuff for windowed gamma.
	HDC								hDC;
	HWND							hWnd;

	// Our render interface.
	FD3DRenderInterface				RenderInterface;

#ifdef _DEBUG
	// Debugging data.
	TArray<BYTE>					StaticBuffer;
#endif

	// Deferred state.
	FD3DDeferredState				DeferredState;

#ifndef FINAL_RELEASE
	class FD3DStats
	{
	public:

		INT							STATS_FirstEntry,
									STATS_PushStateCalls,
									STATS_PushStateCycles,
									STATS_PopStateCalls,
									STATS_PopStateCycles,
									STATS_SetRenderTargetCalls,
									STATS_SetRenderTargetCycles,
									STATS_SetMaterialCalls,
									STATS_SetMaterialCycles,
									STATS_SetMaterialBlendingCalls,
									STATS_SetMaterialBlendingCycles,
									STATS_SetVertexStreamsCalls,
									STATS_SetVertexStreamsCycles,
									STATS_SetDynamicStreamCalls,
									STATS_SetDynamicStreamCycles,
									STATS_SetIndexBufferCalls,
									STATS_SetIndexBufferCycles,
									STATS_SetDynamicIndexBufferCalls,
									STATS_SetDynamicIndexBufferCycles,
									STATS_DrawPrimitiveCalls,
									STATS_DrawPrimitiveCycles,
									STATS_NumPrimitives,
									STATS_NumSVPVertices,
									STATS_LockCalls,
									STATS_LockCycles,
									STATS_UnlockCalls,
									STATS_UnlockCycles,
									STATS_PresentCalls,
									STATS_PresentCycles,
									STATS_DynamicVertexBytes,
									STATS_DynamicIndexBytes,
									STATS_NumTextures,
									STATS_TextureBytes,
									STATS_NumVertexStreams,
									STATS_VertexStreamBytes,
									STATS_NumIndexBuffers,
									STATS_IndexBufferBytes,
									STATS_StateChanges,
									STATS_StateChangeCycles,
									STATS_TextureChanges,
									STATS_TextureChangeCycles,
									STATS_TransformChanges,
									STATS_TransformChangeCycles,
									STATS_LightChanges,
									STATS_LightChangeCycles,
									STATS_LightSetChanges,
									STATS_LightSetCycles,
									STATS_DynamicVertexBufferLockCycles,
									STATS_DynamicVertexBufferLockCalls,
									STATS_DynamicVertexBufferDiscards,
									STATS_ClearCalls,
									STATS_ClearCycles,
									STATS_StreamSourceChanges,
									STATS_ResourceBytes,
									STATS_DrawPrimitiveUPCycles,
									STATS_DrawPrimitiveUPCalls,
									STATS_RunPushBufferCycles,
									STATS_RunPushBufferCalls,
									STATS_LastEntry;
		FD3DStats();
		void Init();
	} D3DStats;
#endif

	// Constructor/destructor.
	UD3DRenderDevice();

	void StaticConstructor();

	//scion jg -- Changes for faster caching scheme
	// GetCachedResource - Finds the cached copy of the given resource.  Returns NULL if failure.
	FD3DResource* GetCachedResource(_WORD CacheId,ECacheHint CacheHint);
	// FlushResource - Ensures that the given resource isn't being cached.
	void FlushResource(_WORD CacheId,ECacheHint CacheHint);
	// FlushResource - Ensures that the given resource isn't being cached.
	void FlushResource(FD3DResource* pResource);
	// Adds a resource to one of the caches
	void CacheResource(FD3DResource* pResource,ECacheHint CacheHint);
	// ResourceCached - Returns whether a resource is cached or not.
	UBOOL ResourceCached(_WORD CacheID,ECacheHint CacheHint)
	{
		return GetCachedResource(CacheID,CacheHint) != NULL;
	}
	//scion jg --  Clean up all resources on exit
	void Destroy(void);
	//scion jg -- For resetting render states without flushing
	virtual void ResetRenderStates(void);

	// scion sz -
	// finds a vertex shader with the given type/declaration. creates one if none is found.
	FD3DVertexShader* GetVertexShader(DWORD Type,FShaderDeclaration& Declaration);
	// scion sz -
	// loads the vertex shader microcode for the given configuration. creates one if it doesn't exist
	FORCEINLINE FD3DCombinedVertexShader* GetCombinedVertexShader( QWORD VertexShaderEnum );
	FORCEINLINE void SetVertexShaderConstant( INT Register, const void* pConstantData, DWORD ConstantCount ) 
	{
#ifdef _XBOX

#ifndef VERTEXSHADERS_ALWAYS_ON
		static DWORD ShaderConstantMode=0;
		Direct3DDevice8->GetShaderConstantMode( &ShaderConstantMode );
		if( !(ShaderConstantMode & D3DSCM_192CONSTANTS) )
		{
			Direct3DDevice8->SetShaderConstantMode( D3DSCM_192CONSTANTS );
		}
#endif //!VERTEXSHADERS_ALWAYS_ON

		if( FAILED(Direct3DDevice8->SetVertexShaderConstantFast( Register, pConstantData, ConstantCount )) )
		{
			appErrorf( NAME_Error, TEXT("SetVertexShaderConstant Failed"));
		}
#else
		if( FAILED(Direct3DDevice8->SetVertexShaderConstant(Register, pConstantData, ConstantCount)) )
		{
			appErrorf( NAME_Error, TEXT("SetVertexShaderConstant Failed"));
		}
#endif //_XBOX		
	}

	// scion sz -
	// interface to make sure vertex shader microcode is loaded
	virtual void LoadVertexShaderEnum( QWORD VertexShaderEnum )
	{ 
        GetCombinedVertexShader( VertexShaderEnum );
	}
	
	// GetPixelShader - Finds a Pixel shader with the given type.
	FD3DPixelShader* GetPixelShader(EPixelShader Type);

#ifdef _XBOX //jg -- Support for directly setting shader microcode on the GPU
	D3DPIXELSHADERDEF* GetPixelShaderProgram(EPixelShader Type);
#endif

	inline void SetPixelShaderConstant( INT Register, const void* pConstantData, DWORD ConstantCount ) 
	{
		if( FAILED(Direct3DDevice8->SetPixelShaderConstant(Register, pConstantData, ConstantCount)) )
		{
			appErrorf( NAME_Error, TEXT("SetPixelShaderConstant Failed"));
		}
	}

	// URenderDevice interface.
	virtual UBOOL Init();
	virtual void Exit(UViewport* Viewport);

	virtual UBOOL SetRes(UViewport* Viewport,INT NewX,INT NewY,UBOOL Fullscreen,INT ColorBytes=0,UBOOL bSaveSize=true);
	UBOOL UnSetRes( const TCHAR* Msg, HRESULT h, UBOOL Fatal = 0 );

	virtual void Flush(UViewport* Viewport);

	virtual void UpdateGamma(UViewport* Viewport);
	virtual void RestoreGamma();

	virtual UBOOL Exec(const TCHAR* Cmd,FOutputDevice& Ar);

	virtual FRenderInterface* Lock(UViewport* Viewport,BYTE* HitData,INT* HitSize);
	virtual void Unlock(FRenderInterface* RI);
	virtual void Present(UViewport* Viewport);

	virtual void ReadPixels(UViewport* Viewport,FColor* Pixels);

	virtual UBOOL SupportsTextureFormat( ETextureFormat );

	virtual void SetEmulationMode(EHardwareEmulationMode Mode);
	virtual FRenderCaps* GetRenderCaps();
};

#endif
