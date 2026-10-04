#include <xgraphics.h>
#include "D3DPushBuffer.h"
#include "XBResource.h"

// For asynch loading
#include "UnThreadingWindows.h"

#ifndef _XPR_MANAGER_H
#define _XPR_MANAGER_H

#define NATIVE_XPR TEXT("Natives")

/**
 * This class manages loading/unloading of all XPR resources. It maintains a
 * list of loaded XPR files. It queries each of these in turn when a texture
 * is cached.
 *
 * @author jg
 */
class FXPRManager
{
	/** Simple helper struct for managing named XPR files */
	struct FXPRFile
	{
		/** The number of times this has been referenced, delete on zero */
		DWORD RefCount;
		/** The name of the XPR file */
		FString FileName;
		/** The class wrapper around the XPR file */
		CXBPackedResource XPR;
		/** Holds the settings for asynch loading */
		UBOOL bContainsPushBuffers;
		/** Holds the settings for asynch loading */
		UBOOL bNeedsCPUFastAccess;
		/** Whether this item is done loading */
		UBOOL bDoneLoading;
		/** Critical section to control access to the loading */
		FSynchronize* LoadingLock;

		/**
		 * Constructor that sets the filename and creates the XPR
		 *
		 * @param InFileName The file to open as a bundled file
		 * @param bAsynchLoad whether we'll be background loading or not
		 * @param bContainsPushBuffers whether we need writecombining or not
		 * @param bNeedsCPUFastAccess whether we need fast CPU access or not
		 */
		FXPRFile(const TCHAR* InFileName,UBOOL bAsynchLoad,
			UBOOL bContainsPushBuffers = FALSE,
			UBOOL bNeedsCPUFastAccess = FALSE);

		/** Releases any memory used by the XPR file */
		~FXPRFile(void)
		{
			XPR.Destroy();
			// Destroy our per file synch object
			GSynchronizeFactory->Destroy(LoadingLock);
		}
	};

	/** Contains the list of loaded texture XPR files */
	TArray<FXPRFile*> TextureXPRFiles;
	/** Contains the list of loaded UMA XPR files */
	TArray<FXPRFile*> UMAXPRFiles;
	/** Contains the list of loaded pushbuffer XPR files */
	TArray<FXPRFile*> PushbufferXPRFiles;
	/** Thread pool for background loading */
	FQueuedThreadPoolWin AsynchLoadPool;
	/** Whether we've created the pool yet or not */
	UBOOL bPoolWasCreated;
	/** Critical section to control access to the loading */
	FCriticalSectionWin UnloadLock;
	
	/** Simple class that gets queued and background loads a file */
	struct FAsynchFileLoad : public FQueuedWork
	{
		/** The file we are loading asynch */
		FXPRFile* XPRFile;

		/** Simple constructor */
		FAsynchFileLoad(FXPRFile* InXPRFile)
		{
			XPRFile = InXPRFile;
		}

		/**
		 * Calls the Create() function to perform the work on another thread
		 */
		void DoWork(void)
		{
			guard(FAsynchFileLoad::DoWork);
			{
				check(XPRFile != NULL);
				// Lock access to this file
				FScopeLock sl(XPRFile->LoadingLock);
				LOCAL_CLOCK(Time);
				// Load the image into RAM
				HRESULT hr = XPRFile->XPR.Create(*XPRFile->FileName,
					XPRFile->bContainsPushBuffers,
					XPRFile->bContainsPushBuffers);
				// Log some stats
				debugf(TEXT("XPR file %s asynch loaded in %f with 0x%X"),
					*XPRFile->FileName,
					GSecondsPerCycle * LOCAL_UNCLOCK(Time),hr);
				check(SUCCEEDED(hr) && "Failed to load XPR file");
			}
			// Don't immediately return to the queue
			Sleep(20);
			unguard;
		}

		/**
		 * Deletes this object since it is no longer needed
		 */ 
		void Abandon(void)
		{
			guard(FAsynchFileLoad::Abandon);
			Dispose();
			unguard;
		}

		/**
		 * Deletes this object since it is no longer needed
		 */ 
		void Dispose(void)
		{
			guard(FAsynchFileLoad::Dispose);
			// Block in case this is being accessed by another thread
			{
				FScopeLock sl(XPRFile->LoadingLock);
				XPRFile->bDoneLoading = TRUE;
			}
			delete this;
			unguard;
		}
	};

	/**
	 * Adds the XPR to the asynch loading queue. Inits the queue if it hasn't
	 * yet been started
	 *
	 * @param XPRFile the file to load asynch
	 */
	void AddQueuedXPR(FXPRFile* XPRFile)
	{
		guard(AddQueuedXPR);
		// Init the pool if needed
		if (bPoolWasCreated == FALSE)
		{
			bPoolWasCreated = AsynchLoadPool.Create(1,12 * 1024);
		}
		// Now, create the queued work and add it to the pool to do
		AsynchLoadPool.AddQueuedWork(NEW FAsynchFileLoad(XPRFile));
		unguard;
	}

	/**
	 * Common routine for adding a XPR to a collection of XPRs
	 *
	 * @param FileName the name of the XPR to add
	 * @param XPRArray the collection of XPRs to add to
	 * @param bAsynchLoad whether to use the asynch loading mechanism
	 * @param bContainsPushBuffers pass through to the packed resource class
	 * @param bNeedsCPUFastAccess pass through to the packed resource class
	 */
	FORCEINLINE void LoadXPR(const TCHAR* FileName,TArray<FXPRFile*>& XPRArray,
		UBOOL bAsynchLoad,UBOOL bContainsPushBuffers,UBOOL bNeedsCPUFastAccess)
	{
		guard(FXPRManager::LoadXPR);
		FScopeLock sl(&UnloadLock);
		FXPRFile* XPRFile = NULL;
		// Look for an existing XPR of this name
		for (INT Index = 0; Index < XPRArray.Num() && XPRFile == NULL; Index++)
		{
			if (appStricmp(*XPRArray(Index)->FileName,FileName) == 0)
			{
				XPRFile = XPRArray(Index);
			}
		}
		// Don't add this if already present
		if (XPRFile == NULL)
		{
			XPRFile = NEW FXPRFile(FileName,bAsynchLoad,bContainsPushBuffers,
				bNeedsCPUFastAccess);
			// Allocate a new XPR entry, note it needs fast CPU access
			XPRArray.AddItem(XPRFile);
			// Add it to the thread pool's work queue
			if (bAsynchLoad)
			{
				AddQueuedXPR(XPRFile);
			}
		}
		// Increment our reference count
		XPRFile->RefCount++;
		unguard;
	}

	/**
	 * Waits for a XPR to be done loading before deleting it
	 *
	 * @param XPRFile the file that needs to be deleted
	 */
	FORCEINLINE void DeleteXPR(FXPRFile* XPRFile)
	{
		guard(DeleteXPR);
		UBOOL bWait = TRUE;
		INT WaitCount = 0;
		// Perform a few waits in case this is background loading
		while (bWait)
		{
			// Don't unload ones that are still potentially being streamed
			if (XPRFile->bDoneLoading == TRUE)
			{
				debugf(TEXT("Unloading XPR %s"),*XPRFile->FileName);
				// Free the XPR memory
				delete XPRFile;
				bWait = FALSE;
			}
			// Wait a sec and then try again
			if (bWait == TRUE)
			{
				WaitCount++;
#ifndef FINAL_RELEASE
				debugf(TEXT("Waiting %d times for XPR %s to finish loading"),
					WaitCount,*XPRFile->FileName);
#endif
				appSleep(0.1f);
			}
		}
		unguard;
	}

	/**
	 * Common routine for checking the reference count on a XPR and cleaning
	 * up if needed.
	 *
	 * @param FileName the name of the XPR to removed
	 * @param XPRArray the collection of XPRs to search
	 */
	FORCEINLINE void UnloadXPR(const TCHAR* FileName,TArray<FXPRFile*>& XPRArray)
	{
		guard(FXPRManager::UnloadXPR);
		UBOOL bFound = 0;
		FScopeLock sl(&UnloadLock);
		// Go through the list searching for the XPR file to clean up
		for (INT Index = 0; Index < XPRArray.Num() && bFound == 0; Index++)
		{
			// Check for a match so we don't always search the full list
			bFound = appStricmp(*XPRArray(Index)->FileName,FileName) == 0;
			// If found and the ref count is zero, clean up
			if (bFound && (--XPRArray(Index)->RefCount) == 0)
			{
				// Block momentarily if it is being read
				{
					FScopeLock sl(XPRArray(Index)->LoadingLock);
				}
				DeleteXPR(XPRArray(Index));
				// Clear the invalid pointer
				XPRArray.Remove(Index);
				Index--;
			}
		}
		unguard;
	}

	/**
	 * Common routine for checking the loading state of an XPR
	 *
	 * @param FileName the name of the XPR to check the loading state on
	 * @param XPRArray the collection of XPRs to search
	 */
	FORCEINLINE UBOOL IsXPRLoaded(const TCHAR* FileName,TArray<FXPRFile*>& XPRArray)
	{
		guard(FXPRManager::IsXPRLoaded);
		UBOOL bFound = FALSE;
		UBOOL bLoaded = FALSE;
		// Go through the list searching for the specific file and
		// check to see if it is loaded yet
		for (INT Index = 0; Index < XPRArray.Num() && bFound == FALSE; Index++)
		{
			// Check for a match so we don't always search the full list
			if (appStricmp(*XPRArray(Index)->FileName,FileName) == 0)
			{
				bFound = TRUE;
				// Check the loading state
				bLoaded = XPRArray(Index)->bDoneLoading;
			}
		}
		return bLoaded;
		unguard;
	}

	/**
	 * Common routine for checking the ref count of an XPR
	 *
	 * @param FileName the name of the XPR to get the ref count for
	 * @param XPRArray the collection of XPRs to search
	 */
	FORCEINLINE DWORD GetXPRRefCount(const TCHAR* FileName,TArray<FXPRFile*>& XPRArray)
	{
		guard(FXPRManager::IsXPRLoaded);
		UBOOL bFound = FALSE;
		DWORD RefCount = 0;
		// Go through the list searching for the specific file and
		// check to see if it is loaded yet
		for (INT Index = 0; Index < XPRArray.Num() && bFound == FALSE; Index++)
		{
			// Check for a match so we don't always search the full list
			if (appStricmp(*XPRArray(Index)->FileName,FileName) == 0)
			{
				bFound = TRUE;
				// Get the ref count
				RefCount = XPRArray(Index)->RefCount;
			}
		}
		return RefCount;
		unguard;
	}

	/**
	 * Common routine for unloading non-native XPRs.
	 *
	 * @param FileName the name of the XPR to removed
	 * @param XPRArray the collection of XPRs to search
	 */
	FORCEINLINE void ForceUnloadXPRs(TArray<FXPRFile*>& XPRArray)
	{
		guard(FXPRManager::ForceUnloadXPRs);
		FScopeLock sl(&UnloadLock);
		// Go through the list unloading them all
		for (INT Index = 0; Index < XPRArray.Num(); Index++)
		{
			// Don't unload any native XPRs
			if (XPRArray(Index)->FileName.InStr(NATIVE_XPR) == -1)
			{
				DeleteXPR(XPRArray(Index));
				// Clear the invalid pointer
				XPRArray.Remove(Index);
				Index--;
			}
		}
		unguard;
	}

#ifndef FINAL_RELEASE
	/**
	 * Common routine for logging out XPR information
	 *
	 * @param XPRArray the list of XPR files to log out
	 */
	FORCEINLINE void LogXPRInfo(TArray<FXPRFile*>& XPRArray)
	{
		guard(FXPRManager::LogXPRInfo);
		// Go through the list unloading them all
		for (INT Index = 0; Index < XPRArray.Num(); Index++)
		{
			debugf(TEXT("%s"),*XPRArray(Index)->FileName);
			debugf(TEXT("RefCount %d"),XPRArray(Index)->RefCount);
			debugf(TEXT("Type %s"),
				XPRArray(Index)->bContainsPushBuffers == TRUE ? TEXT("Pushbuffer") :
				XPRArray(Index)->bNeedsCPUFastAccess == TRUE ? TEXT("MeshData") :
				TEXT("Textures"));
			debugf(TEXT("DoneLoading %s"),XPRArray(Index)->bDoneLoading ? TEXT("true") : TEXT("false"));
			debugf(TEXT("Sysmem size %d"),XPRArray(Index)->XPR.GetSysMemSize());
			debugf(TEXT("Vidmem size %d"),XPRArray(Index)->XPR.GetVidMemSize());
		}
		unguard;
	}

	/**
	 * Gets the list of items for a given XPR file and looks in all other XPRs for
	 * duplicates in them
	 *
	 * @param XPRArray the array to check for duplicates on
	 */
	void DumpXPRDuplicates(TArray<FXPRFile*>& XPRArray);
#endif

public:
	/** Default constructor */
	FXPRManager(void) : bPoolWasCreated(FALSE)
	{
	}
	
	/** Cleans up any allocated resources */
	~FXPRManager(void)
	{
	}

	/**
	 * Adds a XPR file to the managed set if it isn't already in the
	 * cache.
	 *
	 * @param FileName The name of the XPR to add to the cache
	 * @param bAsynchLoad whether to background load or not
	 */
	FORCEINLINE void LoadTexturesXPR(const TCHAR* FileName,UBOOL bAsynchLoad)
	{
		LoadXPR(FileName,TextureXPRFiles,bAsynchLoad,FALSE,FALSE);
	}

	/**
	 * Adds a UMA XPR file to the managed set if it isn't already in the
	 * cache.
	 *
	 * @param FileName The name of the XPR to add to the cache
	 * @param bAsynchLoad whether to background load or not
	 */
	FORCEINLINE void LoadUMAXPR(const TCHAR* FileName,UBOOL bAsynchLoad)
	{
		LoadXPR(FileName,UMAXPRFiles,bAsynchLoad,FALSE,TRUE);
	}

	/**
	 * Adds a pushbuffer XPR file to the managed set if it isn't already in the
	 * cache.
	 *
	 * @param FileName The name of the XPR to add to the cache
	 * @param bAsynchLoad whether to background load or not
	 */
	FORCEINLINE void LoadPBXPR(const TCHAR* FileName,UBOOL bAsynchLoad)
	{
		LoadXPR(FileName,PushbufferXPRFiles,bAsynchLoad,TRUE,FALSE);
	}

	/**
	 * Removes a named XPR from the textures XPR array
	 *
	 * @param FileName The name of the XPR to unload
	 */
	FORCEINLINE void UnloadTexturesXPR(const TCHAR* FileName)
	{
		UnloadXPR(FileName,TextureXPRFiles);
	}

	/**
	 * Removes a named XPR from the UMA XPR array
	 *
	 * @param FileName The name of the XPR to unload
	 */
	FORCEINLINE void UnloadUMAXPR(const TCHAR* FileName)
	{
		UnloadXPR(FileName,UMAXPRFiles);
	}

	/**
	 * Removes a named XPR from the pushbuffer XPR array
	 *
	 * @param FileName The name of the XPR to unload
	 */
	FORCEINLINE void UnloadPBXPR(const TCHAR* FileName)
	{
		UnloadXPR(FileName,PushbufferXPRFiles);
	}

	/**
	 * Determines if the specified file is loaded yet
	 *
	 * @param FileName The name of the XPR to check
	 */
	FORCEINLINE UBOOL IsTextureXPRLoaded(const TCHAR* FileName)
	{
		return IsXPRLoaded(FileName,TextureXPRFiles);
	}

	/**
	 * Determines if the specified file is loaded yet
	 *
	 * @param FileName The name of the XPR to check
	 */
	FORCEINLINE UBOOL IsUMAXPRLoaded(const TCHAR* FileName)
	{
		return IsXPRLoaded(FileName,UMAXPRFiles);
	}

	/**
	 * Determines if the specified file is loaded yet
	 *
	 * @param FileName The name of the XPR to check
	 */
	FORCEINLINE UBOOL IsPBXPRLoaded(const TCHAR* FileName)
	{
		return IsXPRLoaded(FileName,PushbufferXPRFiles);
	}

	/**
	 * Determines if the specified file is loaded yet
	 *
	 * @param FileName The name of the XPR to check
	 */
	FORCEINLINE DWORD GetTextureXPRRefCount(const TCHAR* FileName)
	{
		return GetXPRRefCount(FileName,TextureXPRFiles);
	}

	/**
	 * Determines if the specified file is loaded yet
	 *
	 * @param FileName The name of the XPR to check
	 */
	FORCEINLINE DWORD GetUMAXPRRefCount(const TCHAR* FileName)
	{
		return GetXPRRefCount(FileName,UMAXPRFiles);
	}

	/**
	 * Determines if the specified file is loaded yet
	 *
	 * @param FileName The name of the XPR to check
	 */
	FORCEINLINE DWORD GetPBXPRRefCount(const TCHAR* FileName)
	{
		return GetXPRRefCount(FileName,PushbufferXPRFiles);
	}

	/**
	 * Finds a texture in the list of cached XPR files and regisers it with D3D
	 * 
	 * @param Name The name of the texture to load
	 *
	 * @return Pointer to the D3D texture or NULL if no matching texture is found
	 */
	LPDIRECT3DTEXTURE8 GetTexture(const TCHAR* Name);

	/**
	 * Finds a cubemap in the list of cached XPR files and regisers it with D3D
	 * 
	 * @param Name The name of the cubemap to load
	 *
	 * @return Pointer to the D3D texture or NULL if no matching cubemap is found
	 */
	LPDIRECT3DCUBETEXTURE8 GetCubemap(const TCHAR* Name);

	/**
	 * Searches the loaded XPR files for the specified array data
	 *
	 * @param ArrayName the named TUmaArray to get the information for
	 * @param OutCount the out param to get the count of items
	 * @param OutData the out param that is set to the contiguous buffer data
	 */
	UBOOL GetTUmaArray(const TCHAR* ArrayName,INT& OutCount,void*& OutData);

	/**
	 * Finds a pushbuffer in the list of cached XPR files and regisers it with D3D
	 * 
	 * @param Name The name of the pushbuffer to load
	 */
	FPushBuffer* GetPushbuffer(const TCHAR* Name);

	/**
	 * Unloads all XPR files except the natives.xpr one
	 */
	void UnloadAllXPRs(void);

#ifndef FINAL_RELEASE
	/**
	 * Dumps all of the info about the loaded XPR files
	 */
	void DumpLoadedXPRInfo(void);

	/**
	 * Searches each type of XPR for duplicate items
	 */
	void DumpXPRDuplicates(void);
#endif
};

#endif