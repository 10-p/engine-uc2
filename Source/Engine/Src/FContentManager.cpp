/* epic ====================================================================================================
* Class:	FContentManager
* Author:	Per Vognsen
*
* The content manager is responsible for managing Xbox Live downloadable content and auto-updates. Its
* duties include checking the validity of such files via digital signatures before they are used by other
* parts of the engine, and maintaining a table of contents to facilitate fast filesystem access.
* ==========================================================================================================
*/

#include "Engine.h"
#include "FContentManager.h"

#ifdef _XBOX
#include "FFileManagerXbox.h"
#include <xtl.h>
#include <xonline.h>
#endif

#define DISABLE_SIGNATURE_CHECKING FALSE

#define LOG_FAILURE(Message) debugf(NAME_DevLive, TEXT("Failure in %s: %s (error %d)"), __FUNC_NAME__, Message, GetLastError())
#define LOG_WARNING(Message) debugf(NAME_DevLive, TEXT("Warning in %s: %s"), __FUNC_NAME__, Message)

#define ENABLE_LOG_INFO FALSE

#if ENABLE_LOG_INFO
#define LOG_INFO(Message) debugf(NAME_DevLive, TEXT("Info in %s: %s"), __FUNC_NAME__, Message)
#else
#define LOG_INFO(Message)
#endif

FContentManager::FHandlerEntry FContentManager::HandlerTable[] =
{
	// Activation files.
	{TEXT("act"),	&FContentManager::HandleActivationFile},
	// International/English localization files.
	{TEXT("int"),	&FContentManager::HandleLocalizationFile},
	// German localization files.
	{TEXT("deu"),	&FContentManager::HandleLocalizationFile},
	// French localization files.
	{TEXT("fra"),	&FContentManager::HandleLocalizationFile},
	// Italian localization files.
	{TEXT("ita"),	&FContentManager::HandleLocalizationFile},
	// Spanish localization files.
	{TEXT("spa"),	&FContentManager::HandleLocalizationFile},
	// Map files.
	{TEXT("unr"),	&FContentManager::HandleMapFile},
};

/* epic ====================================================================================================
* Function:	FContentManager::GetInstance
* Author:	Per Vognsen
*
* Returns the singleton instance of the content manager.
* ==========================================================================================================
*/
FContentManager* FContentManager::GetInstance()
{
	guard(FContentManager::GetInstance);

	static FContentManager* SingletonInstance = NULL;

	// If this is the first time this function is called, construct the singleton instance.
	if (SingletonInstance == NULL)
	{
		SingletonInstance = new FContentManager();
	}

	return SingletonInstance;

	unguard;
}

/* epic ====================================================================================================
* Function:	FContentManager::FindFile
* Author:	Per Vognsen
*
* Tries to find a file in the title-specific region. Returns TRUE if found, FALSE otherwise.
*
* Parameters:
* FileName		The name of the file to be searched for.
* OutFullPath	If the file is found, OutFullPath will contain the full path of the file.
* ==========================================================================================================
*/
UBOOL FContentManager::FindFile(const TCHAR* FileName, TCHAR* OutFullPath)
{
	guard(FContentManager::FindFile);

	// Do exact string matching (rather than substring matching) if a file extention is present.
	UBOOL bExact = strrchr(FileName, '.') != NULL;
	INT FileNameLength = appStrlen(FileName);

	// Attempt to find a matching file in our table of contents.
	for (INT Index = 0; Index < TOC.Num(); Index++)
	{
		FFileData FileData = TOC(Index);
		if (FileData.FileName != NULL && FileData.PathName != NULL)
		{
			UBOOL bMatchFound = FALSE;
			if (bExact)
			{
				// Try to do an exact string match.
				if (appStricmp(FileData.FileName, FileName) == 0)
				{
					bMatchFound = TRUE;
				}
			}
			else
			{
				// Try to do a substring match.
				if (appStrnicmp(FileData.FileName, FileName, FileNameLength) == 0 &&
					FileData.FileName[FileNameLength] == TEXT('.'))
				{
					bMatchFound = TRUE;
				}
			}

			if (bMatchFound)
			{
				// Found a match, so pass its full path to the caller.
				appSprintf(OutFullPath, TEXT("%s%s"), FileData.PathName, FileData.FileName);

				LOG_INFO(TEXT("Found matching file:"));
				LOG_INFO(OutFullPath);

				return TRUE;
			}
		}
	}

	// No match was found.
	return FALSE;

	unguard;
}

/* epic ====================================================================================================
* Function:	FContentManager::ProcessDirectory
* Author:	Per Vognsen
*
* Processes a directory in the title-specific region. This includes adding the directory to the content
* manager's table of contents. It also caches the names of package files to be loaded aftre. Note that the
* directory should have been previously verified to be uncorrupted by checking its digital signatures.
*
* Parameters:
* Directory		The full path (including the drive prefix) of the directory to be processed.
* ==========================================================================================================
*/
void FContentManager::ProcessDirectory(const FString& ContentDirectory, UBOOL bDownloadableContent)
{
	guard(FContentManager::ProcessDirectory);

#ifdef _XBOX
	TArray<FString> PendingDirectories;

	FString Slash(TEXT("\\"));
	FString Wildcard(TEXT("*"));

	LOG_INFO(TEXT("Content directory:"));
	LOG_INFO(*ContentDirectory);

	// Initialize the queue of pending directories with the directory that was passed in.
	new(PendingDirectories) FString(FString(ContentDirectory) + Slash);

	// While any unprocessed directories remain
	while (PendingDirectories.Num() > 0)
	{
		// Proceed to the next pending directory.
		FString PendingDirectory = PendingDirectories.Pop();

		LOG_INFO(TEXT("Pending directory:"));
		LOG_INFO(*PendingDirectory);

		const UBOOL bListFiles = TRUE;
		const UBOOL bListDirectories = TRUE;

		// Process all files in the directory.
		TArray<FString> Files = GFileManager->FindFiles(*(PendingDirectory + Wildcard), bListFiles, !bListDirectories);
		for (INT FileIdx = 0; FileIdx < Files.Num(); FileIdx++)
		{
			const FString& File = Files(FileIdx);

			LOG_INFO(TEXT("File:"));
			LOG_INFO(*File);

			// Skip any Xbox metadata files.
			if (appStrstr(*File, TEXT(".xbx")) != NULL)
			{
				continue;
			}

			// Try to open the file.
			HANDLE FileHandle = CreateFile(appToAnsi(*(PendingDirectory + File)), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if (FileHandle != INVALID_HANDLE_VALUE)
			{
				// Get the size of the file.
				DWORD FileSize = GetFileSize(FileHandle, NULL);

				// Close the file.
				CloseHandle(FileHandle);

				// Construct the table of contents entry.
				FFileData FileData;
				FileData.PathName = appStrdup(*PendingDirectory);
				FileData.FileName = appStrdup(*File);
				FileData.FileSize = FileSize;

				// Add the entry to the table of contents.
				new(TOC) FFileData(FileData);

				if (bDownloadableContent)
				{
					// Try to find the extention of this file.
					UBOOL bSearchFromRight = TRUE;
					INT FileExtentionIndex = File.InStr(TEXT("."), bSearchFromRight);
					if (FileExtentionIndex != -1)
					{
						// Get the file extention.
						const FString& FileExtention = File.Mid(FileExtentionIndex + 1);

						LOG_INFO(TEXT("File extention:"));
						LOG_INFO(*FileExtention);

						// Try to find a handler for this file extention.
						INT HandlerTableSize = sizeof(HandlerTable) / sizeof(HandlerTable[0]);
						for (INT HandlerIndex = 0; HandlerIndex < HandlerTableSize; HandlerIndex++)
						{
							FHandlerEntry HandlerEntry = HandlerTable[HandlerIndex];

							// Check if this is the right handler.
							if (appStricmp(*FileExtention, HandlerEntry.FileExtention) == 0)
							{
								// Call the handler for this file.
								FHandlerFunction Handler = HandlerEntry.Handler;
								(this->*Handler)(*(PendingDirectory + File));
							}
						}
					}
				}
			}
		}

		// Process all subdirectories of the directory.
		TArray<FString> Subdirectories = GFileManager->FindFiles(*(PendingDirectory + Wildcard), !bListFiles, bListDirectories);
		for (INT DirectoryIdx = 0; DirectoryIdx < Subdirectories.Num(); DirectoryIdx++)
		{
			const FString& Subdirectory = Subdirectories(DirectoryIdx);

			LOG_INFO(TEXT("Subdirectory:"));
			LOG_INFO(*Subdirectory);

			// Construct the full path of the subdirectory and queue it for processing.
			new(PendingDirectories) FString(PendingDirectory + Subdirectory + Slash);
		}
	}
#endif

	unguard;
}

/* epic ====================================================================================================
* Function:	FContentManager::IsDirectoryValid
* Author:	Per Vognsen
*
* Checks whether a root directory in the title-specific region is valid by checking the digital signatures
* in the accompanying metadata file. Returns TRUE if valid, FALSE otherwise.
* ==========================================================================================================
*/
UBOOL FContentManager::IsDirectoryValid(const FString& ContentDirectory)
{
	guard(FContentManager::IsDirectoryValid);

	if (DISABLE_SIGNATURE_CHECKING)
	{
		return TRUE;
	}

#ifdef _XBOX
	LOG_INFO(TEXT("Content directory:"));
	LOG_INFO(*ContentDirectory);

	// Try to load the signature registry for this content directory.
	HANDLE SignatureHandle = XLoadContentSignatures(appToAnsi(*ContentDirectory));
	if (SignatureHandle == NULL)
	{
		LOG_FAILURE(TEXT("Could not load signature registry:"));
		return FALSE;
	}

	FString Wildcard(TEXT("*"));
	FString Slash(TEXT("\\"));

	TArray<FString> PendingDirectories;

	// Initialize the queue of pending directories with the directory that was passed in.
	new(PendingDirectories) FString(TEXT(""));

	// While any unprocessed directories remain
	while (PendingDirectories.Num() > 0)
	{
		// Proceed to the next pending directory.
		FString PendingDirectory = PendingDirectories.Pop();

		LOG_INFO(TEXT("Pending directory:"));
		LOG_INFO(*PendingDirectory);

		const UBOOL bListFiles = TRUE;
		const UBOOL bListDirectories = TRUE;

		FString PathName(FString(ContentDirectory) + Slash + PendingDirectory);

		LOG_INFO(TEXT("Path name:"));
		LOG_INFO(*PathName);

		// Process all files in the directory.
		TArray<FString> Files = GFileManager->FindFiles(*(PathName + Wildcard), bListFiles, !bListDirectories);
		for (INT FileIdx = 0; FileIdx < Files.Num(); FileIdx++)
		{
			const FString& File = Files(FileIdx);

			LOG_INFO(TEXT("File:"));
			LOG_INFO(*File);

			// Skip any Xbox metadata files.
			if (appStricmp(*File, TEXT("contentmeta.xbx")) == 0 ||
				Files(FileIdx).InStr(TEXT(".xbe"),TRUE) != -1)
			{
				continue;
			}

			// Construct the full path name of the file.
			FString FullPathName(PathName + File);

			LOG_INFO(TEXT("Full path name:"));
			LOG_INFO(*FullPathName);

			// Try to open the file.
			HANDLE FileHandle = CreateFile(TCHAR_TO_ANSI(*FullPathName), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			if (FileHandle != INVALID_HANDLE_VALUE)
			{
				// Get the size of the file.
				DWORD FileSize = GetFileSize(FileHandle, NULL);

				// Close the file.
				CloseHandle(FileHandle);

				// Construct the name of the file in the signature registry.
				FString SignatureName(PendingDirectory + File);

				LOG_INFO(TEXT("Signature name:"));
				LOG_INFO(*SignatureName);

				// Try to find the existing signature for the file stored in the registry.
				BYTE* Signature = NULL;
				DWORD SignatureSize = XCALCSIG_SIGNATURE_SIZE;
				if (!XLocateSignatureByName(SignatureHandle, appToAnsi(*SignatureName), 0, FileSize, &Signature, &SignatureSize))
				{
					LOG_FAILURE(TEXT("Could not find existing signature for file in registry."));
					XCloseContentSignatures(SignatureHandle);
					return FALSE;
				}

				// Allocate a buffer large enough to contain the file.
				BYTE* FileBuffer = (BYTE*) appMalloc(FileSize);

				// Load the file into the allocated buffer.
				FArchive* FileArchive = GFileManager->CreateFileReader(appToAnsi(*FullPathName));
				FileArchive->Serialize(FileBuffer, FileSize);
				delete FileArchive;

				// Try to calculate the signature for the file.
				DWORD VerifySize = XCALCSIG_SIGNATURE_SIZE;
				BYTE Verify[XCALCSIG_SIGNATURE_SIZE];
				if (!XCalculateContentSignature(FileBuffer, FileSize, Verify, &VerifySize))
				{
					LOG_FAILURE(TEXT("Could not calculate signature for file."));
					XCloseContentSignatures(SignatureHandle);
					appFree(FileBuffer);
					return FALSE;	
				}

                // Compare the stored signature with the signature just calculated.
				if (SignatureSize != VerifySize || appMemcmp(Verify, Signature, VerifySize) != 0)
				{
					LOG_FAILURE(TEXT("Stored signature and calculated signature did not match."));
					XCloseContentSignatures(SignatureHandle);
					appFree(FileBuffer);
					return FALSE;
				}

				// The signatures matched, so free the allocated buffer and proceed to the next file.
				appFree(FileBuffer);
			}
		}

		// Process all subdirectories of the directory.
		TArray<FString> Subdirectories = GFileManager->FindFiles(*(PathName + Slash + Wildcard), !bListFiles, bListDirectories);
		for (INT DirectoryIdx = 0; DirectoryIdx < Subdirectories.Num(); DirectoryIdx++)
		{
			const FString& Subdirectory = Subdirectories(DirectoryIdx);

			LOG_INFO(TEXT("Subdirectory:"));
			LOG_INFO(*Subdirectory);

			// Construct the full path of the subdirectory.
			FString FullPathName = PendingDirectory + Subdirectory + Slash;

			LOG_INFO(TEXT("Full path name:"));
			LOG_INFO(*FullPathName);

			// Queue the subdirectory for processing.
			new(PendingDirectories) FString(FullPathName);
		}
	}

	// Everything in the content directory was valid.
	XCloseContentSignatures(SignatureHandle);
	return TRUE;
#else
	return TRUE;
#endif

	unguard;
}

/* epic ====================================================================================================
* Function:	FContentManager::Initialize
* Author:	Per Vognsen
*
* Processes all autoupdate and downloadable content directories, checking integrity via digital signatures,
* building the table of contents, and keeping track of corrupted content directories.
* ==========================================================================================================
*/
void FContentManager::Initialize()
{
	guard(FContentManager::Initialize);

#ifdef _XBOX
	UBOOL bDownloadableContent = TRUE;

	// Check the validity of the auto-update content directory.
	FString AutoUpdateDirectory(TEXT("T:\\$U"));
	if (IsDirectoryValid(AutoUpdateDirectory))
	{
		ProcessDirectory(AutoUpdateDirectory, !bDownloadableContent);
	}
	else
	{
		// Add the auto-update directory to the list of corrupted directories.
		new(CorruptedDirectories) FString(AutoUpdateDirectory);
	}

	// Check the validity of all downloadable content directories.
	FString BaseContentDirectory(TEXT("T:\\$C\\"));
	FString SearchPattern(BaseContentDirectory + FString(TEXT("*.*")));
	const UBOOL bListFiles = TRUE;
	const UBOOL bListDirectories = TRUE;
	TArray<FString> Directories = GFileManager->FindFiles(*SearchPattern, !bListFiles, bListDirectories);
	for (INT DirectoryIndex = 0; DirectoryIndex < Directories.Num(); DirectoryIndex++)
	{
		const FString& ContentDirectory = Directories(DirectoryIndex);
		FString FullPathName(BaseContentDirectory + ContentDirectory);

		if (IsDirectoryValid(FullPathName))
		{
			ProcessDirectory(FullPathName, bDownloadableContent);
		}
		else
		{
			// Add this downloadable content directory to the list of corrupted directories.
			new(CorruptedDirectories) FString(FullPathName);
		}
	}
#endif

	unguard;
}

/* epic ====================================================================================================
* Function:	FContentManager::HasCorruptedContent
* Author:	Per Vognsen
*
* Checks if there is any corrupted content directories in the title-specific region.
* ==========================================================================================================
*/
UBOOL FContentManager::HasCorruptedContent()
{
	guard(FContentManager::HasCorruptedContent);

	return CorruptedDirectories.Num() > 0;

	unguard;
}

/* epic ====================================================================================================
* Function:	FContentManager::DeleteCorruptedContent
* Author:	Per Vognsen
*
* Deletes all directories that have been marked corrupted/invalid.
* ==========================================================================================================
*/
void FContentManager::DeleteCorruptedContent()
{
	guard(FContentManager::DeleteCorruptedContent);

#ifdef _XBOX
	// For each corrupted directory
	for (INT Index = 0; Index < CorruptedDirectories.Num(); Index++)
	{
		const FString& CorruptedDirectory = CorruptedDirectories(Index);

		LOG_INFO(TEXT("Deleting corrupted directory:"));
		LOG_INFO(*CorruptedDirectory);

		// Delete corrupted content directory.
		if (!XRemoveContent(appToAnsi(*CorruptedDirectory)))
		{
			LOG_FAILURE(TEXT("XRemoveContent() failed."));
		}
	}

	// Clear the list of corrupted directories.
	CorruptedDirectories.Empty();
#endif

	unguard;
}

/* epic ====================================================================================================
* Function:	FContentManager::HandleActivationFile
* Author:	Per Vognsen
*
* Handles loading an activation file. (For activating content such as character skins and maps.)
*
* Parameters:
* FullPathName		The full path name of the activation file.
* ==========================================================================================================
*/
void FContentManager::HandleActivationFile(const FString& FullPathName)
{
	guard(FContentManager::HandleActivationFile);

	LOG_INFO(TEXT("FullPathName:"));
	LOG_INFO(*FullPathName);

	// Get the filename. (Everything after the rightmost slash.)
	UBOOL bSearchFromRight = TRUE;
	INT SlashIndex = FullPathName.InStr(TEXT("\\"), bSearchFromRight);
	FString File = FullPathName.Mid(SlashIndex + 1);

	LOG_INFO(TEXT("File:"));
	LOG_INFO(*File);

	// Strip off the file extention to get the name of the activated feature.
	INT DotIndex = File.InStr(TEXT("."), bSearchFromRight);
	FString ActivatedFeature = File.Left(DotIndex);

	LOG_INFO(TEXT("ActivatedFeature:"));
	LOG_INFO(*ActivatedFeature);

	// Add it to the list of activated features.
	new(ActivatedFeatures) FString(ActivatedFeature);

	unguard;
}

/* epic ====================================================================================================
* Function:	FContentManager::HandleLocalizationFile
* Author:	Per Vognsen
*
* Handles loading a localization file.
*
* Parameters:
* FullPathName		The full path name of the localization.
* ==========================================================================================================
*/
void FContentManager::HandleLocalizationFile(const FString& FullPathName)
{
	guard(FContentManager::HandleLocalizationFile);

	LOG_INFO(TEXT("FullPathName:"));
	LOG_INFO(*FullPathName);

#ifdef _XBOX
	// Get the filename. (Everything after the rightmost slash.)
	UBOOL bSearchFromRight = TRUE;
	INT SlashIndex = FullPathName.InStr(TEXT("\\"), bSearchFromRight);
	FString File = FullPathName.Mid(SlashIndex + 1);

	// Strip off the file extention to get the name of the localization language.
	INT DotIndex = File.InStr(TEXT("."), bSearchFromRight);
	FString Language = File.Mid(DotIndex + 1);

	// Copy the localization file to the cache drive.
	FString Destination = FString::Printf(TEXT("Z:\\System\\Localization\\%s\\%s"), *Language, *File);
	GFileManager->Copy(*Destination, *FullPathName, TRUE);

	// Add the localization file to the file manager's list of localization files for downloadable content.
	TArray<FString> ContentLocalizationFiles = GFileManager->GetContentLocalizationFiles();
	new(ContentLocalizationFiles) FString(FullPathName);
	GFileManager->SetContentLocalizationFiles(ContentLocalizationFiles);
#endif

	unguard;
}

/* epic ====================================================================================================
* Function:	FContentManager::HandleMapFile
* Author:	Per Vognsen
*
* Handles loading a map file.
*
* Parameters:
* FullPathName		The full path name of the map file.
* ==========================================================================================================
*/
void FContentManager::HandleMapFile(const FString& FullPathName)
{
	guard(FContentManager::HandleMapFile);

	// Get the filename. (Everything after the rightmost slash.)
	UBOOL bSearchFromRight = TRUE;
	INT SlashIndex = FullPathName.InStr(TEXT("\\"), bSearchFromRight);
	FString File = FullPathName.Mid(SlashIndex + 1);

	LOG_INFO(TEXT("File:"));
	LOG_INFO(*File);

	// Strip off the file extention to get the map name.
	INT DotIndex = File.InStr(TEXT("."), bSearchFromRight);
	FString MapName = File.Left(DotIndex);

	LOG_INFO(TEXT("MapName:"));
	LOG_INFO(*MapName);

	new(Maps) FString(MapName);

	unguard;
}

/* epic ====================================================================================================
* Function:	FContentManager::UpdateMapLists
* Author:	Per Vognsen
*
* Creates updated map lists by combining map lists for the downloaded maps with the master map list and
* writing it out to the cache HD partition.
* ==========================================================================================================
*/
void FContentManager::UpdateMapLists()
{
	guard(FContentManager::UpdateMapLists);

#ifdef _XBOX
	static const TCHAR* UpdatedMapListsPathName = TEXT("Z:\\MapLists.ini");

	// Remove the existing cached copy of MapLists.ini.
	GFileManager->Delete(UpdatedMapListsPathName);

	// Only create an updated map list if there are any downloaded maps available.
	if (Maps.Num() > 0)
	{
		// For each downloaded map file
		for (INT Index = 0; Index < Maps.Num(); Index++)
		{
			FString& MapFile = Maps(Index);

			// Construct the name of the INI file corresponding to the map.
			FString IniFile = MapFile + FString(TEXT(".ini"));

			LOG_INFO(TEXT("IniFile:"));
			LOG_INFO(*IniFile);

			// Get the map lists which the map is to be part of.
			TArray<FString> MapLists;
			GConfig->GetArray(TEXT("MapListsData"), TEXT("MapLists"), MapLists, *IniFile);
			GConfig->UnloadFile(*IniFile);

			LOG_INFO(TEXT("MapLists:"));
			// Add the map to the appropriate map lists.
			for (INT Index = 0; Index < MapLists.Num(); Index++)
			{
				const FString& MapList = MapLists(Index);

				LOG_INFO(*MapList);

				TArray<FString> MasterMapList;
				GConfig->GetArray(*MapList, TEXT("Maps"), MasterMapList, TEXT("MapLists.ini"));

				// Verify that duplicates are not being added to the map list.
				if (MasterMapList.FindItemIndex(MapFile) == -1)
				{
					new(MasterMapList) FString(MapFile);
					GConfig->SetArray(*MapList, TEXT("Maps"), MasterMapList, TEXT("MapLists.ini"));
				}
				else
				{
					LOG_INFO(TEXT("Master map list already contains map."));
					LOG_INFO(TEXT("MapFile:"));
					LOG_INFO(*MapFile);
					LOG_INFO(TEXT("MapList:"));
					LOG_INFO(*MapList);
					return;
				}
			}
		}

		// Write updated MapLists.ini to the cache HD partition.
		UBOOL bRead = TRUE;
		GConfig->Flush(bRead, TEXT("MapLists.ini"), UpdatedMapListsPathName);
		GConfig->UnloadFile(TEXT("MapLists.ini"));

		// Put MapLists.ini in the table of contents so the game will find the cached copy.
		FFileData FileData;
		FileData.PathName = appStrdup(TEXT("Z:\\"));
		FileData.FileName = appStrdup(TEXT("MapLists.ini"));
		FileData.FileSize = 1;

		new(TOC) FFileData(FileData);
	}
#endif

	unguard;
}

/* epic ====================================================================================================
* Function:	FContentManager::UpdateMapLimits
* Author:	Per Vognsen
*
* Updates information about recommended and maximum players for downloadable maps.
* ==========================================================================================================
*/
void FContentManager::UpdateMapLimits()
{
	guard(FContentManager::UpdateMapLimits);

#ifdef _XBOX
	static const TCHAR* UpdatedGametypesPathName = TEXT("Z:\\Gametypes.ini");

	// Remove the existing cached copy of Gametypes.ini.
	GFileManager->Delete(UpdatedGametypesPathName);

	// Only create an updated INI file if there are any downloaded maps available.
	if (Maps.Num() > 0)
	{
		// For each downloaded map file
		for (INT Index = 0; Index < Maps.Num(); Index++)
		{
			const FString& MapFile = Maps(Index);

			// Construct the name of the INI file corresponding to the map.
			FString IniFile = MapFile + FString(TEXT(".ini"));

			LOG_INFO(TEXT("IniFile:"));
			LOG_INFO(*IniFile);

			typedef TMultiMap<FName, FString> FSection;
			UBOOL bForce = TRUE;
			UBOOL bConst = TRUE;
			
			// Get the map limits section.
			FSection* MapLimitsSection =  GConfig->GetSectionPrivate(TEXT("MapLimitsData"), !bForce, bConst, *IniFile);
			if (MapLimitsSection != NULL)
			{
				for (FSection::TIterator It(*MapLimitsSection); It; ++It)
				{
					// The key corresponds to the name of the map's game type.
					FName Key = It.Key();
					const FString& Value = It.Value();

					LOG_INFO(TEXT("Key:"));
					LOG_INFO(*Key);

					LOG_INFO(TEXT("Value:"));
					LOG_INFO(*Value);

					TArray<FString> LevelData;
					if (GConfig->GetArray(*Key, TEXT("LevelData"), LevelData, TEXT("Gametypes.ini")) > 0)
					{
						// Add an entry for the map to this game type's game info.
						new (LevelData) FString(FString::Printf(TEXT("(Name=\"%s\",%s)"), *MapFile, *Value));
						GConfig->SetArray(*Key, TEXT("LevelData"), LevelData, TEXT("Gametypes.ini"));
					}
					else
					{
						LOG_FAILURE(TEXT("Could not find section in Gametypes.ini:"));
						LOG_FAILURE(*Key);
						return;
					}
				}
			}
			else
			{
				LOG_FAILURE(TEXT("MapLimitsData section not found in INI file:"));
				LOG_FAILURE(*IniFile);
				return;
			}

			GConfig->UnloadFile(*IniFile);
		}

		// Unload Gametypes.ini.
		UBOOL bRead = TRUE;
		GConfig->Flush(bRead, TEXT("Gametypes.ini"), UpdatedGametypesPathName);
		GConfig->UnloadFile(TEXT("Gametypes.ini"));

		// Add an entry for the updated version of Gametypes.ini to the table of contents.
		// Put MapLists.ini in the table of contents so the game will find the cached copy.
		FFileData FileData;
		FileData.PathName = appStrdup(TEXT("Z:\\"));
		FileData.FileName = appStrdup(TEXT("Gametypes.ini"));
		FileData.FileSize = 1;

		new(TOC) FFileData(FileData);

		// Force all game info configs to be reloaded.
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->IsChildOf(AGameInfo::StaticClass()))
			{
				It->GetDefaultObject()->LoadConfig();
			}
		}
	}
#endif

	unguard;
}

/* epic ====================================================================================================
* Function:	FContentManager::UpdateConfigs
* Author:	Per Vognsen
*
* Updates configuration files to incorporate information about downloadable maps.
* ==========================================================================================================
*/
void FContentManager::UpdateConfigs()
{
	guard(FContentManager::UpdateConfigs);

	UpdateMapLists();
	UpdateMapLimits();

	unguard;
}

/* epic ====================================================================================================
* Function:	FContentManager::IsCustomContent
* Author:	Ron Prestenback
*
* Checks whether the specified content is considered custom content.
*
* Parameters:
* ContentName		The name of the feature (e.g. character or map name).
* ==========================================================================================================
*/
UBOOL FContentManager::IsCustomContent( const FString& ContentName )
{
	guard(FContentManager::IsCustomContent);

	return HasMap(ContentName) || IsActivated(ContentName);

	unguard;
}

/* epic ====================================================================================================
* Function:	FContentManager::IsActivated
* Author:	Per Vognsen
*
* Checks whether a specific feature (for instance, a character or a map) has been activated/unlocked.
*
* Parameters:
* Name				The name of the feature (e.g. character or map name).
* ==========================================================================================================
*/
UBOOL FContentManager::IsActivated(const FString& Name)
{
	guard(FContentManager::IsActivated);

	FString NameWithNoSpaces(Name.RemoveSpaces());
	return (ActivatedFeatures.FindItemIndex(NameWithNoSpaces) != -1);

	unguard;
}

/* epic ====================================================================================================
* Function:	FContentManager::HasMap
* Author:	Per Vognsen
*
* Checks whether the user has downloaded a specific map.
*
* Parameters:
* Map				The name of the map.
* ==========================================================================================================
*/
UBOOL FContentManager::HasMap(const FString& Map)
{
	guard(FContentManager::HasMap);

	return (Maps.FindItemIndex(Map) != -1);

	unguard;
}

/* epic ====================================================================================================
* Function:	FContentManager::StartDownloader
* Author:	Per Vognsen
*
* Starts the downloadable content manager by launching a separate XBE.
*
* Parameters:
* Port				The port (0-3) of the user the downloader should automatically log on.
* ==========================================================================================================
*/
void FContentManager::StartDownloader(DWORD Port)
{
	guard(FContentManager::StartDownloader);

#ifdef _XBOX
	// Set up the launch data.
	LD_DOWNLOADER LaunchData;
	LaunchData.dwID = LAUNCH_DATA_DOWNLOADER_ID;
	LaunchData.dwBitFilter = 0xFFFFFFFF;

	// Try to make a copy of the logon state
	XONLINE_LOGON_STATE LogonState;
	HRESULT Result = XOnlineSaveLogonState(&LogonState);
	if (SUCCEEDED(Result))
	{
		// Save the copy of the logon state.
		appMemcpy(&(LaunchData.LogonState), &LogonState, sizeof(XONLINE_LOGON_STATE));

		// Specify the logon user index into the saved logon state for the user we want to automatically
		// log on in the downloader. This value can be 0 through 3 and must have valid logon state at it.
		LaunchData.bPremiumLogonPort = Port;
	}

	// Zero out the user-defined part of the launch data.
	appMemzero(&(LaunchData.UserDefined), sizeof(LaunchData.UserDefined));

	//jg -- Persist the display so it isn't black for 3 seconds
	extern IDirect3DDevice8* GD3DDevice;
	GD3DDevice->Present(NULL,NULL,NULL,NULL);
	GD3DDevice->PersistDisplay();

	// Launch the downloader
	XLaunchNewImage("D:\\downloader.xbe", (LAUNCH_DATA*) &LaunchData);

	appPreExit();
	GIsGuarded = FALSE;
#endif

	unguard;
}

/* epic ====================================================================================================
* Function:	FContentManager::Exec
* Author:	Per Vognsen
*
* Content manager specific console commands:
* CM INIT				Initializes the content manager, processes content directories, checks validity, etc.
* CM TOC				Prints the table of contents.
* CM CORRUPTED			Prints the list of corrupted content directories.
* CM ACTIVATED			Prints the list of activated features.
* CM ISACTIVATED <name>	Checks whether the specified feature is activated.
* CM DELTREE <dir>		Deletes a specified content directory.
* CM DELCORRUPTED		Deletes all corrupted directories.
* CM UPDATECONFIGS		Creates updated config files.
* ==========================================================================================================
*/
UBOOL FContentManager::Exec(const TCHAR* Cmd, FOutputDevice& Ar)
{
	guard(FContentManager::Exec);

	if (ParseCommand(&Cmd, TEXT("CM")))
	{
		if (ParseCommand(&Cmd, TEXT("TOC")))
		{
			// Print the table of contents.
			for (INT Index = 0; Index < TOC.Num(); Index++)
			{
				FFileData FileData = TOC(Index);
				debugf(TEXT("File: %s\t\tPath: %s\t\tSize: %d"), FileData.FileName, FileData.PathName, FileData.FileSize);
			}
		}
		else if (ParseCommand(&Cmd, TEXT("CORRUPTED")))
		{
			// Print the list of all corrupted content directories.
			for (INT Index = 0; Index < CorruptedDirectories.Num(); Index++)
			{
				const FString& ContentDirectory = CorruptedDirectories(Index);
				debugf(TEXT("Path: %s"), *ContentDirectory);
			}
		}
		else if (ParseCommand(&Cmd, TEXT("ACTIVATED")))
		{
			debugf(TEXT("Activated features:"));
			// Print the list of all activated features.
			for (INT Index = 0; Index < ActivatedFeatures.Num(); Index++)
			{
				debugf(*ActivatedFeatures(Index));
			}
		}
		else if (ParseCommand(&Cmd, TEXT("ISACTIVATED")))
		{
			if (IsActivated(FString(Cmd)))
			{
				debugf(TEXT("TRUE"));
			}
			else
			{
				debugf(TEXT("FALSE"));
			}
		}
		else if (ParseCommand(&Cmd, TEXT("DELCORRUPTED")))
		{
			// Delete all corrupted content directories.
			DeleteCorruptedContent();
		}
		else if (ParseCommand(&Cmd, TEXT("INIT")))
		{
			// Initializes the content manager.
			Initialize();
		}
		else if (ParseCommand(&Cmd, TEXT("DELTREE")))
		{
#ifdef _XBOX
			XRemoveContent(appToAnsi(Cmd));
#endif
		}
		else if (ParseCommand(&Cmd, TEXT("UPDATECONFIGS")))
		{
			UpdateConfigs();
		}
		else
		{
			debugf(TEXT("Invalid parameter. Valid parameters are: INIT, TOC, CORRUPTED, ACTIVATED, ISACTIVATED, DELTREE, DELCORRUPTED, UPDATECONFIGS."));
		}

		return 1;
	}

	return 0;

	unguard;
}

#undef LOG_FAILURE
#undef LOG_WARNING
#undef LOG_INFO