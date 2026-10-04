/**
 * FFileCacheManager.h -- Handles caching of files from DVD to HD. Also,
 * handles finding the cached versus uncached version of the file for
 * the file manager.
 *
 * Copyright 2004 Epic Games, Inc. All Rights Reserved.
 *
 * Revision history:
 *		Created by Joe Graf.
 */

#ifndef _FILE_CACHE_MANAGER_INC
#define _FILE_CACHE_MANAGER_INC

#ifndef _COPY_BUFFER_SIZE
#define _COPY_BUFFER_SIZE (32 * 1024)
#endif

#define CLEAN_ME_FILE "Z:\\CleanMe.txt"

// NOTE: This must be incremented/changed for each patch version
#define PATCH_VER_FILE "Z:\\PatchVer1.txt"


/**
 * This class handles all background copying of files.
 */
class FFileCacheManagerXbox : public FFileCacheManager
{
	/**
	 * Synchronizes access to any file cache data
	 */
	FCriticalSection* CacheSynch;
	/**
	 * The event used to notify the thread that there is work to do
	 */
	FEvent* WorkEvent;
	/**
	 * This is the thread that is handling our background streaming
	 */
	FRunnableThread* Thread;
	/**
	 * Tells the thread that it should stop
	 */
	UBOOL bThreadShouldExit;
	/**
	 * The list of files that need to be cached
	 */
	TArray<FString> FilesToCache;
	/**
	 * This is the list of files that have been cached to the harddrive
	 */
	TArray<FString> CachedFiles;

	/**
	 * Buffer to read into/write out of
	 */
	BYTE Buffers[2][_COPY_BUFFER_SIZE];
	/**
	 * Current read into buffer
	 */
	INT CurrentBuffer;

	/**
	 * Creates a small text file on the Z: drive. If this file is there at
	 * boot time, then we know the cache is invalid.
	 */
	void CreateSmallFile(const char* FileName);

	/**
	 * Checks to see if the cleanme file exists or not
	 */
	UBOOL DoesCleanMeExist(void);

	/**
	 * Deletes the clean me file
	 */
	void RemoveCleanMe(void);

	/**
	 * Checks for low disk space on the utility drive
	 */
	UBOOL IsDriveNearlyFull(void);

	/**
	 * Checks the cache drive for a patch version file and compares
	 * that to the global patch version string. If they don't match
	 * then the cache needs to be invalidated so that mixed version
	 * data doesn't cause crashes
	 */
	UBOOL IsNewerPatch(void);

	/**
	 * Gets the name of the next file to be processed
	 *
	 * @param FileName the name of the file to copy in the background
	 */
	void GetNextFileToProcess(FString& FileName);

	/**
	 * Copies the specified file to the cache on the xbox
	 *
	 * @param FileName the name of the file to copy
	 */
	void CopyFile(FString& FileName);

	/**
	 * Places all path information into the Dir string and puts the file name
	 * into the FileName string
	 *
	 * @param Dir the out var that will end up with the path information
	 * @param FileName the string that will hold only the filename when done
	 */
	void SplitDirFromFileName(FString& Dir,FString& FileName);

	/**
	 * Adds the file to the cache list if not already in there
	 *
	 * @param FileName the file to add to the list
	 */
	FORCEINLINE void AddToCacheList(const FString& FileName)
	{
		guard(FFileCacheManagerXbox::AddFileToCacheList);
		checkSlow(FileName.Len());
		// Lock access to the to cache list
		FScopeLock sl(CacheSynch);
		UBOOL bFound = 0;
		// Make sure it's not found before adding again
		for (INT Index = 0; Index < CachedFiles.Num() && bFound == 0; Index++)
		{
			bFound = appStricmp(*CachedFiles(Index),*FileName) == 0;
		}
		if (bFound == 0)
		{
			new(CachedFiles)FString(FileName);
		}
		unguard;
	}

	/**
	 * Adds the file to the cache list if not already in there
	 *
	 * @param FileName the file to add to the list
	 */
	FORCEINLINE void RemoveFromCacheList(const FString& FileName)
	{
		guard(FFileCacheManagerXbox::RemoveFromCacheList);
		checkSlow(FileName.Len());
		// Lock access to the to cache list
		FScopeLock sl(CacheSynch);
		// Search through and remove the file name if found
		for (INT Index = 0; Index < CachedFiles.Num(); Index++)
		{
			if (appStricmp(*CachedFiles(Index),*FileName) == 0)
			{
				CachedFiles.Remove(Index);
				break;
			}
		}
		unguard;
	}

	/**
	 * Helper routine that bulk copies the data and checks for an exit request
	 *
	 * @param Read the file to read from
	 * @param ReadIO the overlapped IO struct for reading the data
	 * @param Write the file to write to
	 */
	UBOOL CopyFileData(HANDLE Read,OVERLAPPED& ReadIO,HANDLE Write);

	/**
	 * Checks for any wildcard entries and expands those into files to be
	 * processed
	 *
	 * @param Dir the directory that we need to recompose as part of the file name
	 * @param FileName the file name to check for wildcards
	 */
	void ExpandWildcards(FString& Dir,FString& FileName);

	/**
	 * Splits the directory from the file name, expands wildcards, and removes
	 * the drive letter
	 *
	 * @param Dir the out var that will end up with the path information
	 * @param FileName the file name to check for wildcards
	 */
	void PreprocessFileName(FString& Dir,FString& FileName);

	/**
	 * Makes the drive letter match the temp drive. If there is a drive lettter
	 * then that is changed to be Z:. If there is the path relative indicator
	 * then that is changed to be Z:. If there is no path relative or drive
	 * letter, then Z:\System\ is prepended.
	 *
	 * @param Dir the directory to use
	 * @param FileName the file name to use
	 *
	 * @return The full path to the cached file name
	 */
	FORCEINLINE FString CreateCacheName(const FString& Dir,const FString& FileName)
	{
		guard(FFileCacheManagerXbox::CreateCacheName);
		// Handle $U in the path by stripping it out
		FString ContentDir = Dir;
		INT Pos = ContentDir.InStr(TEXT("$U"));
		if (Pos != -1)
		{
			ContentDir = Dir.Left(Pos);
			ContentDir += Dir.Right(Dir.Len() - Pos - 3);
		}
		FString NewFileName = ContentDir * FileName;
		// If there is a drive letter, then make it the Z drive
		if (NewFileName[1] == TEXT(':'))
		{
			NewFileName[0] = TEXT('Z');
		}
		// If this is path relative, change it to the Z drive
		else if (NewFileName[0] == TEXT('.') && NewFileName[1] == TEXT('.'))
		{
			// For non-demo this will return D:/<file>
			// For demo this will return D:/<demo path>/<file>
			NewFileName = GFileManager->GetDefaultDirectory() *
				NewFileName.Mid(3);
			NewFileName[0] = TEXT('Z');
		}
		// If the filename is assuming system, prepend Z:/System/
		else
		{
			// For non-demo this will return D:/System/<file>
			// For demo this will return D:/<demo path>/System/<file>
			NewFileName = GFileManager->GetDefaultDirectory() +
				TEXT("System\\") + NewFileName;
			NewFileName[0] = TEXT('Z');
		}
		return NewFileName;
		unguard;
	}

	/**
	 * Verifies that the source dir is not our cache drive. This can happen if
	 * the resolve name of the file manager finds it on the cache.
	 *
	 * @param Dir the directory to use
	 * @param FileName the file name to use
	 *
	 * @return The full path to the cached file name
	 */
	FORCEINLINE FString CreateSourceName(const FString& Dir,const FString& FileName)
	{
		guard(FFileCacheManagerXbox::CreateSourceName);
		FString Source(GFileManager->ResolvePath(*(Dir * FileName)));
		// Don't point to ourselves or bad things happen
		if (Source[0] == TEXT('Z'))
		{
			Source[0] = TEXT('D');
		}
		return Source;
		unguard;
	}

	/**
	 * Checks to see if the file is on the cache drive already
	 *
	 * @param FileName the name of the file to check for
	 */
	UBOOL IsFileCached(const TCHAR* FileName);

public:
	/**
	 * Zeros the various members
	 */
	FFileCacheManagerXbox(void);

	/**
	 * Frees any resources, stops the background thread, etc.
	 */
	~FFileCacheManagerXbox(void);

	/**
	 * Starts the background file caching thread
	 *
	 * @return true if successful, false otherwise
	 */
	virtual UBOOL InitFileCache(void);

	/**
	 * Searches the cached file list to see if the requested file is cached or not
	 *
	 * @param FileName the file to search for
	 *
	 * @return NULL if not cached, otherwise the path to the file
	 */
	virtual const TCHAR* GetCachedFileName(const FString& FileName);

	/**
	 * Adds a file to the list of ones that need to be cached. Fires the event
	 * telling the thread that it has work to do.
	 *
	 * @param FileName the file to cache from DVD to HD
	 */
	virtual void AddFileToCacheList(const FString& FileName);

	/**
	 * Whether the thread is currently caching items or not
	 *
	 * @return true if still caching, false otherwise
	 */
	virtual UBOOL IsCachingFiles(void)
	{
		guard(FFileCacheManagerXbox::IsCachingFiles);
		// Check the work event for being signaled or not
		return WorkEvent && WorkEvent->Wait(1);
		unguard;
	}

// FRunnable interface

	/**
	 * This object is already initialized so this call is ignored
	 *
	 * @return True if initialization was successful, false otherwise
	 */
	virtual UBOOL Init(void) { return TRUE; }

	/**
	 * Waits for work to be put on the queue.
	 *
	 * @return The exit code of the runnable object
	 */
	virtual DWORD Run(void);

	/**
	 * Sets the "time to die" var and tells the even there is work to do
	 */
	virtual void Stop(void);

	/**
	 * This is handled in the destructor
	 */
	virtual void Exit(void) { }
};
#endif