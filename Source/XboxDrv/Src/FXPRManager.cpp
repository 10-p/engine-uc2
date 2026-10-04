/**
 * This class manages loading/unloading of all XPR resources. It maintains a
 * list of loaded XPR files. It queries each of these in turn when a texture
 * is cached.
 *
 * @author jg
 */

#include "XboxDrv.h"
#include <xtl.h>
#include "FXPRManager.h"

// Global instance of this class
FXPRManager GXPRManager;

/**
 * Simple function to allow calling on the global without without having
 * to extern the object and mess with includes, etc. Skips files if they
 * have no size in case a group is missing an type of XPR
 *
 * @param Name the name of the XPR group (without extension) to load
 * @param bAsynchLoad whether to background load or not
 */
void LoadXPRGroup(const TCHAR* Name,UBOOL bAsynchLoad)
{
	guard(LoadXPRGroup);
#if LIN_SAVE
	// Don't load anything asynch when linear saving
	bAsynchLoad = FALSE;
#endif
	FString XPRName = GFileManager->ResolvePath(
		*FString::Printf(TEXT("..\\XboxTextures\\%s.xpr"),Name));
	if (GFileManager->FileSize(*XPRName) > 0)
	{
		GXPRManager.LoadTexturesXPR(*XPRName,bAsynchLoad);
	}
	// Load the pushbuffer XPR
	XPRName = GFileManager->ResolvePath(
		*FString::Printf(TEXT("..\\XboxTextures\\%s_pb.xpr"),Name));
	if (GFileManager->FileSize(*XPRName) > 0)
	{
		GXPRManager.LoadPBXPR(*XPRName,bAsynchLoad);
	}
	// Load the UMA XPR
	XPRName = GFileManager->ResolvePath(
		*FString::Printf(TEXT("..\\XboxTextures\\%s_uma.xpr"),Name));
	if (GFileManager->FileSize(*XPRName) > 0)
	{
		GXPRManager.LoadUMAXPR(*XPRName,bAsynchLoad);
	}
	unguard;
}

/**
 * Searches through the XPR holders to determine if all the needed XPR files
 * for a given group have been loaded fully
 *
 * @param Name the name of the XPR group (without extension) to verify
 */
UBOOL IsXPRGroupLoaded(const TCHAR* Name)
{
	guard(IsXPRGroupLoaded);
	UBOOL bIsTexLoaded = TRUE;
	UBOOL bIsUMALoaded = TRUE;
	UBOOL bIsPBLoaded = TRUE;
	// Build the texture XPR name
	const FString& TexName =
		FString::Printf(TEXT("..\\XboxTextures\\%s.xpr"),Name);
	// Don't check this if it doesn't exist
	if (GFileManager->FileSize(*TexName) > 0)
	{
		// Check the texture XPR first
		bIsTexLoaded = GXPRManager.IsTextureXPRLoaded(
			*GFileManager->ResolvePath(*TexName));
	}
	if (bIsTexLoaded == TRUE)
	{
		// Build the TUmaArray XPR name
		const FString& UMAName =
			FString::Printf(TEXT("..\\XboxTextures\\%s_uma.xpr"),Name);
		// Don't check this if it doesn't exist
		if (GFileManager->FileSize(*UMAName) > 0)
		{
			// Check the UMA XPR
			bIsUMALoaded = GXPRManager.IsUMAXPRLoaded(
				*GFileManager->ResolvePath(*UMAName));
		}
		if (bIsUMALoaded == TRUE)
		{
			// Build the pushbuffer name
			const FString& PBName =
				FString::Printf(TEXT("..\\XboxTextures\\%s_pb.xpr"),Name);
			// Don't check this if it doesn't exist, or we'll say No when it
			// may be done
			if (GFileManager->FileSize(*PBName) > 0)
			{
				// Load the pushbuffer XPR
				bIsPBLoaded = GXPRManager.IsPBXPRLoaded(
					*GFileManager->ResolvePath(*PBName));
			}
		}
	}
	// We are only done if these are all met. NOTE: If the group doesn't
	// contain one of the types that type will default to true, so it
	// won't break the checking
	return bIsTexLoaded && bIsUMALoaded && bIsPBLoaded;
	unguard;
}

/**
 * Unloads a group of XPR files
 *
 * @param Name the name of the XPR group (without extension) to unload
 */
void UnloadXPRGroup(const TCHAR* Name)
{
	guard(UnloadXPRGroup);
	// Unload the texture XPR
	GXPRManager.UnloadTexturesXPR(*GFileManager->ResolvePath(
		*FString::Printf(TEXT("..\\XboxTextures\\%s.xpr"),Name)));
	// Unload the UMA XPR
	GXPRManager.UnloadUMAXPR(*GFileManager->ResolvePath(
		*FString::Printf(TEXT("..\\XboxTextures\\%s_uma.xpr"),Name)));
	// Unload the pushbuffer XPR
	GXPRManager.UnloadPBXPR(*GFileManager->ResolvePath(
		*FString::Printf(TEXT("..\\XboxTextures\\%s_pb.xpr"),Name)));
	unguard;
}

/**
 * Simple function to allow calling on the global without without having
 * to extern the object and mess with includes, etc.
 *
 * @param URL the url of the map just loaded
 */
void LoadXPR(const FURL& URL)
{
	guard(LoadXPR);
    // Build the XPR name from the URL
	INT nIndex = URL.Map.InStr(TEXT("."),1);
	// Load each of the needed XPR files
	LoadXPRGroup((nIndex != -1) ? *URL.Map.Left(nIndex) : *URL.Map,FALSE);
	unguard;
}

/**
 * Simple function to allow calling on the global without without having
 * to extern the object and mess with includes, etc.
 *
 * @param Name the name of the texture to load
 */
LPDIRECT3DTEXTURE8 GetTextureFromXPR(const TCHAR* Name)
{
	return GXPRManager.GetTexture(Name);
}

/**
 * Simple function to allow calling on the global without without having
 * to extern the object and mess with includes, etc.
 *
 * @param Name the name of the cubemap to load
 */
LPDIRECT3DCUBETEXTURE8 GetCubemapFromXPR(const TCHAR* Name)
{
	return GXPRManager.GetCubemap(Name);
}

/**
 * Simple function to allow calling on the global without without having
 * to extern the object and mess with includes, etc.
 *
 * @param PBName the name of the pushbuffer to load
 */
FPushBuffer* GetPushbufferFromXPR(const TCHAR* PBName)
{
	return GXPRManager.GetPushbuffer(PBName);
}

/**
 * Simple function to allow calling on the global without without having
 * to extern the object and mess with includes, etc.
 */
void LoadXPRForNatives(void)
{
	LoadXPRGroup(NATIVE_XPR,FALSE);
}

/**
 * Simple function to allow calling on the global without without having
 * to extern the object and mess with includes, etc.
 *
 * Asks the XPR manager for the named array data. The Count & Data
 * pointers are updated and are out params
 *
 * @param ArrayName the named TUmaArray to get the information for
 * @param OutCount the out param to get the count of items
 * @param OutData the out param that is set to the contiguous buffer data
 */
UBOOL GetTUmaArrayFromXPR(const TCHAR* ArrayName,INT& OutCount,void*& OutData)
{
	guard(GetTUmaArray);
	return GXPRManager.GetTUmaArray(ArrayName,OutCount,OutData);
	unguard;
}

/**
 * Constructor that sets the filename and creates the XPR
 *
 * @param InFileName The file to open as a bundled file
 * @param bAsynchLoad whether we'll be background loading or not
 * @param bContainsPushBuffers whether we need writecombining or not
 * @param bNeedsCPUFastAccess whether we need fast CPU access or not
 */
FXPRManager::FXPRFile::FXPRFile(const TCHAR* InFileName,UBOOL bAsynchLoad,
	UBOOL _bContainsPushBuffers,UBOOL _bNeedsCPUFastAccess)
{
	guard(FXPRManager::FXPRFile::FXPRFile);
	LOCAL_CLOCK(LoadTime);
	// Create our synch object
	LoadingLock = GSynchronizeFactory->CreateCriticalSection();
	check(LoadingLock);
	// Make sure to correctly zero these
	bDoneLoading = FALSE;
	// This will be incremented in the common XPR loading code
	RefCount = 0;
	// Copy the name for searches later
	FileName = InFileName;
	bContainsPushBuffers = _bContainsPushBuffers;
	UBOOL bNeedsCPUFastAccess = _bNeedsCPUFastAccess;
	// Only do if not deferred
	if (bAsynchLoad == FALSE)
	{
		FScopeLock sl(LoadingLock);
		// Load the image into RAM
		HRESULT hr = XPR.Create(TCHAR_TO_ANSI(InFileName),bContainsPushBuffers,
			bNeedsCPUFastAccess);
		debugf(TEXT("XPR file %s loaded in %f with 0x%X"),InFileName,
			GSecondsPerCycle * LOCAL_UNCLOCK(LoadTime),hr);
		bDoneLoading = TRUE;
		check(SUCCEEDED(hr) && "Failed to load XPR file");
	}
#ifdef _DEBUGXPR
	DebugXPR(&XPR);
#endif
	unguard;
}

/**
 * Finds a texture in the list of cached XPR files and regisers it with D3D
 * 
 * @param Name The name of the texture to load
 *
 * @return Pointer to the D3D texture or NULL if no matching texture is found
 */
LPDIRECT3DTEXTURE8 FXPRManager::GetTexture(const TCHAR* Name)
{
	guard(FXPRManager::GetTexture);
	// Default to not finding this texture
	LPDIRECT3DTEXTURE8 pTexture = NULL;
	// Convert to something the API knows
	const CHAR* _Name = TCHAR_TO_ANSI(Name);
	// Go through the list and see if it has the item
	for (INT Index = 0; Index < TextureXPRFiles.Num() && pTexture == NULL; Index++)
	{
		FScopeLock sl(TextureXPRFiles(Index)->LoadingLock);
		// Check to see if it's in this XPR
		pTexture = TextureXPRFiles(Index)->XPR.GetTexture(_Name);
	}
	return pTexture;
	unguard;
}

/**
 * Finds a cubemap in the list of cached XPR files and regisers it with D3D
 * 
 * @param Name The name of the cubemap to load
 *
 * @return Pointer to the D3D texture or NULL if no matching cubemap is found
 */
LPDIRECT3DCUBETEXTURE8 FXPRManager::GetCubemap(const TCHAR* Name)
{
	guard(FXPRManager::GetCubemap);
	// Default to not finding this cubemap
	LPDIRECT3DCUBETEXTURE8 pCubemap = NULL;
	// Convert to something the API knows
	const CHAR* _Name = TCHAR_TO_ANSI(Name);
	// Go through the list and see if it has the item
	for (INT Index = 0; Index < TextureXPRFiles.Num() && pCubemap == NULL; Index++)
	{
		FScopeLock sl(TextureXPRFiles(Index)->LoadingLock);
		// Check to see if it's in this XPR
		pCubemap = TextureXPRFiles(Index)->XPR.GetCubemap(_Name);
	}
	return pCubemap;
	unguard;
}

/**
 * Searches the loaded XPR files for the specified array data
 *
 * @param ArrayName the named TUmaArray to get the information for
 * @param OutCount the out param to get the count of items
 * @param OutData the out param that is set to the contiguous buffer data
 */
UBOOL FXPRManager::GetTUmaArray(const TCHAR* ArrayName,INT& OutCount,
	void*& OutData)
{
	guard(FXPRManager::GetTUmaArray);
	// Zero the values in case we don't find an item
	OutCount = 0;
	OutData = 0;
	UBOOL bFound = FALSE;
	// Convert to something the API knows
	const CHAR* _Name = TCHAR_TO_ANSI(ArrayName);
	// Go through the list and see if it has the item
	for (INT Index = 0; Index < UMAXPRFiles.Num() && bFound == FALSE; Index++)
	{
		FScopeLock sl(UMAXPRFiles(Index)->LoadingLock);
		// Check to see if it's in this XPR
		bFound = UMAXPRFiles(Index)->XPR.GetTUmaArray(_Name,OutCount,OutData);
	}
	return bFound;
	unguard;
}

/**
 * Finds a pushbuffer in the list of cached XPR files and regisers it with D3D
 * 
 * @param Name The name of the pushbuffer to load
 */
FPushBuffer* FXPRManager::GetPushbuffer(const TCHAR* Name)
{
	guard(FXPRManager::GetPushbuffer);
	// Default to not finding this pushbuffer
	FPushBuffer* PushBuffer = NULL;
	// Convert to something the API knows
	const CHAR* _Name = TCHAR_TO_ANSI(Name);
	// Go through the list and see if it has the item
	for (INT Index = 0; Index < PushbufferXPRFiles.Num() && PushBuffer == NULL; Index++)
	{
		FScopeLock sl(PushbufferXPRFiles(Index)->LoadingLock);
		// Check to see if it's in this XPR
		PushBuffer = PushbufferXPRFiles(Index)->XPR.GetPushbuffer(_Name);
	}
	return PushBuffer;
	unguard;
}

/**
 * Unloads all XPR files except the natives.xpr one
 */
void FXPRManager::UnloadAllXPRs(void)
{
	guard(FXPRManager::UnloadAllXPRs);
	if (bPoolWasCreated == TRUE)
	{
		// Abort any background loading of XPR files
		AsynchLoadPool.ClearQueuedWork();
		// Give anything that was loading time to finish
		appSleep(0.1f);
	}
	// Unload all XPRs from each array except native XPRs
	ForceUnloadXPRs(TextureXPRFiles);
	ForceUnloadXPRs(UMAXPRFiles);
	ForceUnloadXPRs(PushbufferXPRFiles);
	unguard;
}

/**
 * Just forwards the call
 */
void UnloadAllXPRs(void)
{
	GXPRManager.UnloadAllXPRs();
}

/**
 * This is a hack due to the nasty tangled mess the headers are in
 * This should be in XBResouce, but can't be
 */
FPushBuffer* ExternalFixup(XPRPushBuffer* Data,XBRESOURCE& XBRes,
	void* Sys,void* Video)
{
	guard(ExternalFixup);
	BYTE* SysMem = (BYTE*)Sys;
	BYTE* VideoMem = (BYTE*)Video;
	// Find the pushbuffer for this item
	FD3DPushBuffer* PushBuffer = NULL;
	void* RegisterData = NULL;
	// The data either lives in the system area or the video area
	// RunAsCopy lives in system, while Runnable in video
	if (Data->Common & D3DPUSHBUFFER_RUN_USING_CPU_COPY)
	{
		RegisterData = &SysMem[XBRes.dwOffset + Data->Offset];
	}
	else
	{
		RegisterData = &VideoMem[Data->Offset];
	}
	// Allocate a new pushbuffer wrapper
	PushBuffer = NEW FD3DPushBuffer(*Data,RegisterData);
#if 0
debugf(TEXT("\nPushBuffer (%s) data 0x%X with D3D wrapper of 0x%X"),
	   XBRes.strName,Data,PushBuffer);
#endif
	return PushBuffer;
	unguard;
}

/**
 * Checks the ref count of each XPR to see if they would unload
 *
 * @param Name the group to check the ref count on
 */
UBOOL WouldXPRGroupUnload(const TCHAR* Name)
{
	guard(IsXPRGroupLoaded);
	DWORD TexRefCount = 0;
	DWORD UmaRefCount = 0;
	DWORD PbRefCount = 0;
	// Build the texture XPR name
	const FString& TexName =
		FString::Printf(TEXT("..\\XboxTextures\\%s.xpr"),Name);
	if (GFileManager->FileSize(*TexName) > 0)
	{
		// Get the texture XPR refcount
		TexRefCount = GXPRManager.GetTextureXPRRefCount(
			*GFileManager->ResolvePath(*TexName));
	}
	// Build the TUmaArray XPR name
	const FString& UMAName =
		FString::Printf(TEXT("..\\XboxTextures\\%s_uma.xpr"),Name);
	if (GFileManager->FileSize(*UMAName) > 0)
	{
		// Get the UMA XPR refcount
		UmaRefCount = GXPRManager.GetUMAXPRRefCount(
			*GFileManager->ResolvePath(*UMAName));
	}
	// Build the pushbuffer name
	const FString& PBName =
		FString::Printf(TEXT("..\\XboxTextures\\%s_pb.xpr"),Name);
	if (GFileManager->FileSize(*PBName) > 0)
	{
		// Get the pushbuffer XPR refcount
		PbRefCount = GXPRManager.GetPBXPRRefCount(
			*GFileManager->ResolvePath(*PBName));
	}
	// Check to see if these would all unload
	return TexRefCount <= 1 && UmaRefCount <= 1 && PbRefCount <= 1;
	unguard;
}

#ifdef _DEBUGXPR
/**
 * Function dumps the contents of the XPR, locations, sizes, etc. to aid in
 * debugging problems related adding new types
 *
 * @param XPR the resource file to dump the contents of
 */
void DebugXPR(CXBPackedResource* XPR)
{
	guard(DebugXPR);
	debugf(TEXT("XPR Info:"));
	debugf(TEXT("System memory used (%d) starting at 0x%x"),
		XPR->m_dwSysMemDataSize,XPR->m_pSysMemData);
	debugf(TEXT("Video memory used (%d) starting at 0x%x"),
		XPR->m_dwVidMemDataSize,XPR->m_pVidMemData);
	debugf(TEXT("Resource table:"));
	debugf(TEXT("Offset\tName"));
    // Walk the list of named resources
    for (DWORD i = 0; i < XPR->m_dwNumResourceTags; i++)
	{
		debugf(TEXT("%d\t%s"),XPR->m_pResourceTags[i].dwOffset,
			XPR->m_pResourceTags[i].strName);
	}
	// Go through each pushbuffer and dump the info so we can verify it
	if (XPR->m_bContainsPushBuffers == TRUE)
	{
		debugf(TEXT("Pushbuffers:"));
        // Walk the list of named resources
        for (DWORD i = 0; i < XPR->m_dwNumResourceTags; i++)
		{
			// Log the name
			debugf(XPR->m_pResourceTags[i].strName);
			// Now get the pushbuffer object
			XPRPushBuffer* PushBuffer = (XPRPushBuffer*)XPR->GetData(
				XPR->m_pResourceTags[i].strName);
			debugf(TEXT("Common %d (%s,%s)"),PushBuffer->Common,
				(PushBuffer->Common & D3DCOMMON_TYPE_PUSHBUFFER) ? TEXT("D3DCOMMON_TYPE_PUSHBUFFER") : TEXT(""),
				(PushBuffer->Common & D3DPUSHBUFFER_RUN_USING_CPU_COPY) ? TEXT("D3DPUSHBUFFER_RUN_USING_CPU_COPY") : TEXT(""));
			debugf(TEXT("Size %d"),PushBuffer->Size);
			debugf(TEXT("AllocatedSize %d"),PushBuffer->AllocatedSize);
			debugf(TEXT("Offset %d"),PushBuffer->Offset);
		}
	}
	unguard;
}
#endif

#ifndef FINAL_RELEASE
/**
 * Forwards the call to the global for debugging
 */
void DumpLoadedXPRInfo(void)
{
	GXPRManager.DumpLoadedXPRInfo();
}

/**
 * Dumps all of the info about the loaded XPR files
 */
void FXPRManager::DumpLoadedXPRInfo(void)
{
	guard(FXPRManager::DumpLoadedXPRInfo);
	LogXPRInfo(TextureXPRFiles);
	LogXPRInfo(UMAXPRFiles);
	LogXPRInfo(PushbufferXPRFiles);
	unguard;
}

/**
 * Forwards the call to the global for debugging
 */
void DumpXPRDuplicates(void)
{
	GXPRManager.DumpXPRDuplicates();
}

/**
 * Searches each type of XPR for duplicate items
 */
void FXPRManager::DumpXPRDuplicates(void)
{
	guard(FXPRManager::DumpXPRDuplicates);
	debugf(TEXT("Dumping texture duplicates"));
	DumpXPRDuplicates(TextureXPRFiles);
	debugf(TEXT("Dumping mesh duplicates"));
	DumpXPRDuplicates(UMAXPRFiles);
	debugf(TEXT("Dumping pushbuffer duplicates"));
	DumpXPRDuplicates(PushbufferXPRFiles);
	unguard;
}

/**
 * Gets the list of items for a given XPR file and looks in all other XPRs for
 * duplicates in them
 *
 * @param XPRArray the array to check for duplicates on
 */
void FXPRManager::DumpXPRDuplicates(TArray<FXPRFile*>& XPRArray)
{
	guard(FXPRManager::DumpXPRDuplicates);
	// Go through each XPR and search all others for duplicates
	for (INT Index = 0; Index < XPRArray.Num() - 1; Index++)
	{
		DWORD ResourceCount = 0;
		XBRESOURCE* pResources = NULL;
		// Get the list of resources to search for
		if (SUCCEEDED(XPRArray(Index)->XPR.GetResourceTags(&ResourceCount,
			&pResources)))
		{
			// For each resource
			for (DWORD Count = 0; Count < ResourceCount; Count++)
			{
				// Search the remaining XPRs
				for (INT SearchIndex = Index + 1; SearchIndex < XPRArray.Num();
					SearchIndex++)
				{
					// See if this one has the item we are looking for
					if (XPRArray(SearchIndex)->XPR.GetData(
						pResources[Count].strName) != NULL)
					{
						debugf(TEXT("Duplicate '%s' found in XPR '%s'"),
							pResources->strName,*XPRArray(SearchIndex)->FileName);
					}
				}
			}
		}
	}
	unguard;
}
#endif