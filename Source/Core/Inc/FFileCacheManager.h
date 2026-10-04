/**
 * FFileCacheManager.h -- This is the abstract base class for the file cache
 * manager. It is called from Core/Engine to copy files over to the xbox
 * hard drive from the DVD.
 *
 * Copyright 2004 Epic Games, Inc. All Rights Reserved.
 *
 * Revision history:
 *		Created by Joe Graf.
 */

class CORE_API FFileCacheManager : public FRunnable
{
public:
	/**
	 * Starts the background file caching thread
	 *
	 * @return true if successful, false otherwise
	 */
	virtual UBOOL InitFileCache(void) = 0;

	/**
	 * Searches the cached file list to see if the requested file is cached or not
	 *
	 * @param FileName the file to search for
	 *
	 * @return NULL if not cached, otherwise the path to the file
	 */
	virtual const TCHAR* GetCachedFileName(const FString& FileName) = 0;

	/**
	 * Adds a file to the list of ones that need to be cached. Fires the event
	 * telling the thread that it has work to do.
	 *
	 * @param FileName the file to cache from DVD to HD
	 */
	virtual void AddFileToCacheList(const FString& FileName) = 0;

	/**
	 * Whether the thread is currently caching items or not
	 *
	 * @return true if still caching, false otherwise
	 */
	virtual UBOOL IsCachingFiles(void) = 0;
};

// The global file cache manager
extern CORE_API FFileCacheManager* GFileCacheManager;
