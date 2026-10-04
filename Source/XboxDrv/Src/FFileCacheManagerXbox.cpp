/**
 * FFileCacheManagerXbox.cpp -- Implementation of the Xbox file cache manager
 *
 * Copyright 2004 Epic Games, Inc. All Rights Reserved.
 *
 * Revision history:
 *		Created by Joe Graf.
 */

#include "XboxDrv.h"
#include "FFileCacheManagerXbox.h"

void ShowDirtyDiscError(void);

/**
 * Zeros the various members. Mounts the utility drive too
 */
FFileCacheManagerXbox::FFileCacheManagerXbox(void) :
	Thread(NULL), CacheSynch(NULL), WorkEvent(NULL), bThreadShouldExit(0),
	CurrentBuffer(0)
{
	guard(FFileCacheManagerXbox::FFileCacheManagerXbox);
#ifndef FINAL_RELEASE
	UBOOL bMounted = XMountUtilityDrive(FALSE);
	if (bMounted == 0)
	{
		debugf(TEXT("Failed to mount utility partition 0x%X"),GetLastError());
	}
#else
	XMountUtilityDrive(FALSE);
#endif
	// Check for the "clean me" file or the disk being low on space or
	// a patch version that is newer than what is cached on the drive
	if (DoesCleanMeExist() || IsDriveNearlyFull() || IsNewerPatch())
	{
		// Format the utility drive. This file will only be there when
		// we need to format the drive because the game was shut down
		// while it was caching to the HD.
		XFormatUtilityDrive();
	}
	// Create the patch version file
	CreateSmallFile(PATCH_VER_FILE);
	unguard;
}

/**
 * Frees any resources, stops the background thread, etc.
 */
FFileCacheManagerXbox::~FFileCacheManagerXbox(void)
{
	guard(FFileCacheManagerXbox::~FFileCacheManagerXbox);
	// Kill the thread if it is running
	if (Thread != NULL)
	{
		Thread->Kill(TRUE);
	}
	// Release the synch objects
	if (WorkEvent != NULL)
	{
		GSynchronizeFactory->Destroy(WorkEvent);
	}
	if (CacheSynch != NULL)
	{
		GSynchronizeFactory->Destroy(CacheSynch);
	}
	unguard;
}

/**
 * Starts the background file caching thread
 *
 * @return true if successful, false otherwise
 */
UBOOL FFileCacheManagerXbox::InitFileCache(void)
{
	guard(FFileCacheManagerXbox::InitFileCache);
	// Init utility drive directories
	GFileManager->CreateCacheDirectory("");
	GFileManager->CreateCacheDirectory("Cache");
	GFileManager->CreateCacheDirectory("Movies");
	GFileManager->CreateCacheDirectory("Sounds");
	GFileManager->CreateCacheDirectory("System");
	GFileManager->CreateCacheDirectory("System\\Localization");
	GFileManager->CreateCacheDirectory("System\\Localization\\Int");
	GFileManager->CreateCacheDirectory("System\\Localization\\deu");
	GFileManager->CreateCacheDirectory("System\\Localization\\fra");
	GFileManager->CreateCacheDirectory("System\\Localization\\ita");
	GFileManager->CreateCacheDirectory("System\\Localization\\spa");
	GFileManager->CreateCacheDirectory("System\\Ladders");
	GFileManager->CreateCacheDirectory("Textures");
	GFileManager->CreateCacheDirectory("Textures\\Movies");
	GFileManager->CreateCacheDirectory("XboxAnimations");
	GFileManager->CreateCacheDirectory("XboxTextures");
	GFileManager->CreateCacheDirectory("XboxStaticMeshes");
	GFileManager->CreateCacheDirectory("XboxMaps");
	GFileManager->CreateCacheDirectory("XboxVertexShaders");
	// Create the access synch
	CacheSynch = GSynchronizeFactory->CreateCriticalSection();
	if (CacheSynch != NULL)
	{
		// Create the work event that wakes the thread up
		WorkEvent = GSynchronizeFactory->CreateSynchEvent(TRUE);
		if (WorkEvent != NULL)
		{
			// Now create the thread that will do the work
			Thread = GThreadFactory->CreateThread(this,0,0,12 * 1024,
				TPri_BelowNormal);
		}
	}
	return CacheSynch != NULL && WorkEvent != NULL && Thread != NULL;
	unguard;
}

/**
 * Searches the cached file list to see if the requested file is cached or not
 *
 * @param FileName the file to search for
 *
 * @return NULL if not cached, otherwise the path to the file
 */
const TCHAR* FFileCacheManagerXbox::GetCachedFileName(const FString& FileName)
{
	guard(FFileCacheManagerXbox::GetCachedFileName);
	FString Dir;
	FString File(FileName);
	// Split the directory from the file name for easier searching
	SplitDirFromFileName(Dir,File);
	// Now make the Z corrected version
	File = CreateCacheName(Dir,File);
	const TCHAR* Found = NULL;
	// Access to this list must be locked for the duration so no items
	// are added or removed while we iterate it
	FScopeLock sl(CacheSynch);
	// Search the list for a matching file name
	for (INT Index = 0; Index < CachedFiles.Num() && Found == NULL; Index++)
	{
		// If these match it was cached
		if (appStricmp(*CachedFiles(Index),*File) == 0)
		{
			Found = *CachedFiles(Index);
		}
	}
	return Found;
	unguard;
}

/**
 * Adds a file to the list of ones that need to be cached. Fires the event
 * telling the thread that it has work to do.
 *
 * @param FileName the file to cache from DVD to HD
 */
void FFileCacheManagerXbox::AddFileToCacheList(const FString& FileName)
{
	guard(FFileCacheManagerXbox::AddFileToCacheList);
	{
		// Briefly, lock the access to the members so we can add to the
		// work queue
		FScopeLock sl(CacheSynch);
		new(FilesToCache)FString(FileName);
	}
	// Now tell the thread that there is work to do
	WorkEvent->Trigger();
	unguard;
}

/**
 * Waits for work to be put on the queue.
 *
 * @return The exit code of the runnable object
 */
DWORD FFileCacheManagerXbox::Run(void)
{
	guard(FFileCacheManagerXbox::Run);
	// While we are not told to die
	do
	{
		// Wait for someone to tell us there is work to do
		WorkEvent->Wait();
		// Make sure work was being requested and not just telling us to stop
		if (bThreadShouldExit == FALSE)
		{
			// Create our marker in case we are powered off during caching
			CreateSmallFile(CLEAN_ME_FILE);
			// Get the next file name to copy
			FString FileName; 
			GetNextFileToProcess(FileName);
			// While there are files to process FileName will be "" when done
			while (FileName.Len() > 0)
			{
				// Copy the file to the hard drive
				CopyFile(FileName);
				// See if there are anymore to copy
				GetNextFileToProcess(FileName);
			}
			// Now destroy it so we don't needlessly format
			RemoveCleanMe();
			debugf(NAME_DevJoe,TEXT("Done caching files"));
		}
		// Say we are done with our work
		WorkEvent->Reset();
	}
	while (bThreadShouldExit == FALSE);
	return 0;
	unguard;
}

/**
 * Sets the "time to die" var and tells the even there is work to do
 */
void FFileCacheManagerXbox::Stop(void)
{
	guard(FFileCacheManagerXbox::Stop);
	// Thread safe update
	InterlockedIncrement((LPLONG)&bThreadShouldExit);
	// Wake the thread up if it is waiting
	WorkEvent->Trigger();
	unguard;
}

/**
 * Gets the name of the next file to be processed
 *
 * @param FileName the name of the file to copy in the background
 */
void FFileCacheManagerXbox::GetNextFileToProcess(FString& FileName)
{
	guard(FFileCacheManagerXbox::GetNextFileToProcess);
	// Lock access to the to cache list
	FScopeLock sl(CacheSynch);
	if (FilesToCache.Num() > 0)
	{
		// Grab the first file in the list
		FileName = *FilesToCache(0);
		// Remove it from the list to be processed
		FilesToCache.Remove(0);
	}
	else
	{
		FileName.Empty();
	}
	unguard;
}

/**
 * Handles all of the set up work before using the helper method to bulk copy
 * the data over
 *
 * @param FileName the name of the file to copy
 */
void FFileCacheManagerXbox::CopyFile(FString& FileName)
{
	guard(FFileCacheManagerXbox::CopyFile);
	LOCAL_CLOCK(CopyTime);
	FString Dir;
	// Build the directory, filename, and expand wildcards
	PreprocessFileName(Dir,FileName);
	// Create the file name that we are going to write out
	const FString& NewFileName = CreateCacheName(Dir,FileName);
	UBOOL bCachedOK = FALSE;
	// Don't copy over it if it's already there
	if (IsFileCached(*NewFileName) == FALSE)
	{
		// Make sure it's not in the list before we add it
		RemoveFromCacheList(NewFileName);
		FString SourceName(CreateSourceName(Dir,FileName));
		// Open the file for reading
		HANDLE Read = CreateFile(TCHAR_TO_ANSI(*SourceName),GENERIC_READ,0,NULL,
			OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING,
			NULL);
		if (Read != INVALID_HANDLE_VALUE)
		{
			// Bad things happen if the file is zero sized
			if (GetFileSize(Read,NULL) > 0)
			{
				// Open the destination file for writing
				HANDLE Write = CreateFile(TCHAR_TO_ANSI(*NewFileName),GENERIC_WRITE,
					0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
				if (Write != INVALID_HANDLE_VALUE)
				{
					// Create and zero the read overlapped IO struct
					OVERLAPPED ReadIO;
					appMemzero(&ReadIO,sizeof(OVERLAPPED));
					// Copy the file data using overlapped IO for reading and
					// synchronous IO for writing. The writes need to be
					// synchronous because we expect file sizes to be exact,
					// otherwise it would have to write them HD sector aligned,
					// breaking that code
					bCachedOK = CopyFileData(Read,ReadIO,Write);
					CloseHandle(Write);
				}
				else
				{
					debugf(TEXT("Write: Failed to cache file %s due to %d"),*NewFileName,
						GetLastError());
				}
			}
			else
			{
				debugf(TEXT("Failed to cache zero byte file %s"),*SourceName);
			}
			CloseHandle(Read);
		}
		else
		{
			debugf(TEXT("Read: Failed to cache file %s due to %d"),*SourceName,
				GetLastError());
		}
	}
	else
	{
		bCachedOK = TRUE;
	}
	if (bCachedOK == TRUE)
	{
		// Update the list of cached files
		AddToCacheList(NewFileName);
		debugf(NAME_DevJoe,TEXT("Background copying took %f for %s"),
			LOCAL_UNCLOCK(CopyTime) * GSecondsPerCycle,*NewFileName);
	}
	else
	{
		debugf(TEXT("Failed to cache file %s"),*FileName);
	}
	unguard;
}

/**
 * Places all path information into the Dir string and puts the file name
 * into the FileName string
 *
 * @param Dir the out var that will end up with the path information
 * @param FileName the string that will hold only the filename when done
 */
void FFileCacheManagerXbox::SplitDirFromFileName(FString& Dir,FString& FileName)
{
	guard(FFileCacheManagerXbox::SplitDirFromFileName);
	// Find the last part of the path
	INT Index = FileName.InStr(TEXT("\\"),TRUE);
	if (Index != -1)
	{
		// Now grab everything from 0 to Index
		Dir = FileName.Left(Index + 1);
		// Now grab only the file name
		FileName = FileName.Right(FileName.Len() - 1 - Index);
	}
	unguard;
}

/**
 * Checks for any wildcard entries and expands those into files to be
 * processed
 *
 * @param Dir the directory that we need to recompose as part of the file name
 * @param FileName the file name to check for wildcards
 */
void FFileCacheManagerXbox::ExpandWildcards(FString& Dir,FString& FileName)
{
	guard(FFileCacheManagerXbox::ExpandWildcards);
	TArray<FString> Files;
	// Use the file manager to find any matches
	GFileManager->GetFileListByExtension(Files,*FileName,*Dir);
	if (Files.Num() > 0)
	{
		// Copy the first one returned...
		FileName = GFileManager->ResolvePath(*(Dir * Files(0)));
		// and queue the rest
		for (INT Index = 1; Index < Files.Num(); Index++)
		{
			new(FilesToCache)FString(GFileManager->ResolvePath(*(Dir * Files(Index))));
		}
	}
	unguard;
}

/**
 * Splits the directory from the file name, expands wildcards, and removes
 * the drive letter
 *
 * @param Dir the out var that will end up with the path information
 * @param FileName the file name to check for wildcards
 */
void FFileCacheManagerXbox::PreprocessFileName(FString& Dir,FString& FileName)
{
	guard(FFileCacheManagerXbox::PreprocessFileName);
	// Split into two strings
	SplitDirFromFileName(Dir,FileName);
	// See if the file name contains a wild card
	if (FileName[0] == TEXT('*'))
	{
		// Build a list of files from the wildcard data
		ExpandWildcards(Dir,FileName);
		// Split into two strings (again due to wildcard expansion)
		SplitDirFromFileName(Dir,FileName);
	}
	unguard;
}

/** This list must always be cached to avoid cheating */
const TCHAR* ForceCacheExtList[] =
{
	TEXT("INI"),
	TEXT("INT"),
	TEXT("FRA"),
	TEXT("SPA"),
	TEXT("DEU"),
	TEXT("ITA"),
	NULL
};

/**
 * Checks to see if the file is on the cache drive already.
 * NOTE: Certain types of files are always cached to prevent cheating
 *
 * @param FileName the name of the file to check for
 */
UBOOL FFileCacheManagerXbox::IsFileCached(const TCHAR* FileName)
{
	guard(FFileCacheManagerXbox::IsFileCached);
	UBOOL bExists = FALSE;
	UBOOL bForceCache = FALSE;
	// Get the extension of this file
	const TCHAR* Ext = FileName + (appStrlen(FileName) - 3);
	// Search the force cache list for a type that must always be cached
	for (INT Index = 0;
		ForceCacheExtList[Index] != NULL && bForceCache == FALSE; Index++)
	{
		bForceCache = appStricmp(Ext,ForceCacheExtList[Index]) == 0;
	}
	// No need to check the file if force caching is requested
	if (bForceCache == FALSE)
	{
		// Open the file for read only
		HANDLE File = CreateFile(TCHAR_TO_ANSI(FileName),GENERIC_READ,0,NULL,
			OPEN_EXISTING,0,NULL);
		if (File != INVALID_HANDLE_VALUE)
		{
			// Get the size of the file as we expected it
			INT TOCSize = GFileManager->GetTOCFileSize(FileName);
			// Now get the size of the file on the cache drive
			INT CachedSize = GetFileSize(File,NULL);
			// Recache any files that don't match our predetermined size
			bExists = TOCSize == CachedSize && CachedSize > 0;
#ifndef FINAL_RELEASE
			// Help track down re-caching problems
			if (bExists == FALSE)
			{
				debugf(TEXT("Re-caching %s due to size mis-match (%d)(%d)"),
					FileName,TOCSize,CachedSize);
			}
#endif
			CloseHandle(File);
		}
		// Detect sound banks being in use for the single map list case
		else if (GetLastError() == 32)
		{
			debugf(TEXT("File %s is in use, not caching"),FileName);
			bExists = TRUE;
		}
	}
	return bExists;
	unguard;
}

/**
 * Helper routine that bulk copies the data and checks for an exit request
 *
 * @param Read the file to read from
 * @param ReadIO the overlapped IO struct for reading the data
 * @param Write the file to write to
 */
UBOOL FFileCacheManagerXbox::CopyFileData(HANDLE Read,OVERLAPPED& ReadIO,
	HANDLE Write)
{
	guard(FFileCacheManagerXbox::CopyFileData);
	check(Read && Write);
	// Figure out how large this file is
	DWORD FileSize = GetFileSize(Read,NULL);
	UBOOL bDone = FALSE;
	DWORD Ignored;
	// Always reset to the first buffer
	CurrentBuffer = 0;
	// Do a first "priming" read
	ReadFile(Read,Buffers[CurrentBuffer],_COPY_BUFFER_SIZE,&Ignored,&ReadIO);
	UBOOL bNeedReadBlock = TRUE;
	DWORD NumRead = 0;
	DWORD TotalWritten = 0;
	UBOOL bHasError = 0;
	// While there is more to copy do so
	while (bDone == FALSE)
	{
		if (bNeedReadBlock == TRUE)
		{
			// Block until the read & write is done
			if (GetOverlappedResult(Read,&ReadIO,&NumRead,TRUE) == TRUE)
			{
				// Update the overlapped structure or the API will get confused
				ReadIO.Offset += NumRead;
			}
			else
			{
				// Universal disc error message
				ShowDirtyDiscError();
			}
		}
		// Stash the current buffer that we'll write from
		INT WriteBuffer = CurrentBuffer;
		// Change buffers that we'll be operating on
		CurrentBuffer ^= 1;
		// Don't read past the end of file
		if (ReadIO.Offset < FileSize)
		{
			// Issue the next read request
			if (ReadFile(Read,Buffers[CurrentBuffer],_COPY_BUFFER_SIZE,&Ignored,
				&ReadIO) == FALSE && GetLastError() != ERROR_IO_PENDING)
			{
				debugf(TEXT("ReadFile() failed with 0x%X"),GetLastError());
				// Universal disc error message
				ShowDirtyDiscError();
			}
			bNeedReadBlock = TRUE;
		}
		else
		{
			bNeedReadBlock = FALSE;
		}
		DWORD NumWritten = 0;
		// Now that we've read the data in write the contents out
		UBOOL bWriteOK = WriteFile(Write,Buffers[WriteBuffer],NumRead,&NumWritten,NULL);
		if (bWriteOK == FALSE || NumWritten != NumRead)
		{
			debugf(TEXT("Failed to write to cache file 0x%X"),GetLastError());
			bHasError = TRUE;
		}
		TotalWritten += NumWritten;
		// Stop reading if we are done, the thread needs to die, or if
		// we had a write error
		bDone = TotalWritten >= FileSize || bThreadShouldExit == TRUE ||
			bHasError == TRUE;
	}
	// If we wrote the full file, then we're done
	return TotalWritten >= FileSize && bHasError == FALSE;
	unguard;
}

/**
 * Creates a small text file on the Z: drive. If this file is there at
 * boot time, then we know the cache is invalid.
 */
void FFileCacheManagerXbox::CreateSmallFile(const char* FileName)
{
	guard(FFileCacheManagerXbox::CreateSmallFile);
	// Open the file and write a few bytes to it
	HANDLE SmallFile = CreateFile(FileName,GENERIC_WRITE,0,NULL,
		CREATE_ALWAYS,0,NULL);
	if (SmallFile != INVALID_HANDLE_VALUE)
	{
		DWORD Written;
		DWORD FourBytes = 'JGJG';
		// Write the magic value out
		WriteFile(SmallFile,&FourBytes,4,&Written,NULL);
		// Don't leak the handle
		CloseHandle(SmallFile);
	}
	unguard;
}

/**
 * Checks to see if the cleanme file exists or not
 */
UBOOL FFileCacheManagerXbox::DoesCleanMeExist(void)
{
	guard(FFileCacheManagerXbox::DoesCleanMeExist);
	UBOOL bExists = FALSE;
	// Open the file to see if it's there
	HANDLE CleanMe = CreateFile(CLEAN_ME_FILE,GENERIC_READ,0,NULL,
		OPEN_EXISTING,0,NULL);
	if (CleanMe != INVALID_HANDLE_VALUE)
	{
		bExists = TRUE;
		// Don't leak the handle
		CloseHandle(CleanMe);
	}
	return bExists;
	unguard;
}

/**
 * Deletes the clean me file
 */
void FFileCacheManagerXbox::RemoveCleanMe(void)
{
	guard(FFileCacheManagerXbox::RemoveCleanMe);
	// Just delete it
	DeleteFile(CLEAN_ME_FILE);
	unguard;
}

/**
 * Checks for low disk space on the utility drive
 */
UBOOL FFileCacheManagerXbox::IsDriveNearlyFull(void)
{
	guard(FFileCacheManagerXbox::IsDriveNearlyFull);
	UBOOL bIsFull = TRUE;
	ULARGE_INTEGER AmountFree;
	// If the function fails, assume we're full. Otherwise check the free
	// space returned
	if (GetDiskFreeSpaceEx("Z:\\",&AmountFree,NULL,NULL))
	{
		// Less than this and we need to format
		bIsFull = (AmountFree.LowPart < (75 * 1024 * 1024));
	}
	return bIsFull;
	unguard;
}

/**
 * Checks the cache drive for a patch version file. If the patch version
 * file doesn't exist, then this is newer and the cache needs cleaning.
 * Writes a small file to indicate the patch version
 */
UBOOL FFileCacheManagerXbox::IsNewerPatch(void)
{
	guard(FFileCacheManagerXbox::IsNewerPatch);
	UBOOL bIncorrectVerion = TRUE;
	// Open the file to see if this exists
	HANDLE PatchVerFile = CreateFile(PATCH_VER_FILE,GENERIC_READ,0,NULL,
		OPEN_EXISTING,0,NULL);
	if (PatchVerFile != INVALID_HANDLE_VALUE)
	{
		bIncorrectVerion = FALSE;
		// Don't leak the handle
		CloseHandle(PatchVerFile);
	}
	return bIncorrectVerion;
	unguard;
}
