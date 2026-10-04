#include "XboxDrv.h"
#include "XboxLoadSaveManager.h"
/*-----------------------------------------------------------------------------
UXboxLoadSaveManager implementation.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(UXboxLoadSaveManager);


UXboxLoadSaveManager::UXboxLoadSaveManager()
: ULoadSaveManager()
{
	appToAnsi( *GetProfileRoot(), m_RootPath );
}

// ============================================================================

UBOOL UXboxLoadSaveManager::LoadGameObject( const FString& profileName, UObject* Profile )
{
	guard(UXboxLoadSaveManager::LoadGameObject);

	check(GameProfileClass);

	if ( Profile == NULL )
	{
		debugf(NAME_Warning, TEXT("Attempting to load a NULL profile!!"));
		return 0;
	}

	CHAR pathBuffer[MAX_PATH];

	// TCR 3-7 Saved Game Management
	DWORD dwSuccess = XCreateSaveGame( *GetProfileRoot(), appToUnicode(*profileName), OPEN_EXISTING, 0, pathBuffer, MAX_PATH );
	if( dwSuccess == ERROR_SUCCESS )
	{
		FString pathName(pathBuffer);
		FString fullPathName = pathName + SAVE_FILE_NAME;

		if (GFileManager->IsSignedFileValid(*fullPathName,SAVE_VERSION_NUMBER))
		{
			FArchiveSaveGame* SaveGame = new FArchiveSaveGame(GFileManager->CreateFileReader(*fullPathName));
			if ( SaveGame == NULL )
				return 0;

			Profile->Serialize(*SaveGame);

			UBOOL bSuccess = SaveGame->Close();
			delete SaveGame;
			SaveGame = NULL;

			return bSuccess;
		}
	}

	return 0;
	unguard;
} // LoadGameObject



UBOOL UXboxLoadSaveManager::DeleteGameObject(const FString & profileName)
{
	guard(UXboxLoadSaveManager::DeleteGameObject);

	DWORD dwSuccess = XDeleteSaveGame(*GetProfileRoot(),appToUnicode(*profileName));
	return (dwSuccess == ERROR_SUCCESS);

	unguard;
}

UBOOL UXboxLoadSaveManager::SaveGameObject( const FString& profileName, UObject* saveGameObject )
{
	guard(UXboxLoadSaveManager::SaveGameObject);

	ANSICHAR pathBuffer[MAX_PATH];

	// TCR 3-7 Saved Game Management
	DWORD dwSuccess = XCreateSaveGame( GetRootPath(), appToUnicode(*profileName), OPEN_ALWAYS, 0, pathBuffer, MAX_PATH );
	if( dwSuccess == ERROR_SUCCESS )
	{
		FString pathName(pathBuffer);
		pathName += SAVE_FILE_NAME;

		FArchiveSaveGame* SaveGameAr = new FArchiveSaveGame(GFileManager->CreateFileWriter(*pathName));
		if ( SaveGameAr == NULL )
			return 0;

		saveGameObject->Serialize(*SaveGameAr);
		UBOOL bSuccess = SaveGameAr->Close();

		delete SaveGameAr;
		SaveGameAr = NULL;

		if ( bSuccess )
		{
			UBOOL bSuccess = GFileManager->SignFile(*pathName, SAVE_VERSION_NUMBER);

			if ( bSuccess )
			{
				UPlayerProfile* Profile = Cast<UPlayerProfile>(saveGameObject);
				if ( Profile && Profile->thePlayerController )
				{
					UMaterial* SaveIcon = Profile->eventGetCharacterIcon(1);
					if ( SaveIcon )
					{
						// Copy the icon from the source dir to the save game dir
						GFileManager->Copy(*FString::Printf(SAVE_GAME_DEST_PF,pathBuffer),
							*FString::Printf(SAVE_GAME_SOURCE_PF,SaveIcon->GetName()));
					}
				}
			}
		}

		return bSuccess;
	}

	return false;

	unguard;
}

UBOOL UXboxLoadSaveManager::ListGameObjects(TArray<FString> *ProfileName )
{
	guard(UXboxLoadSaveManager::ListGameObjects);

	ProfileName->Empty();

	XGAME_FIND_DATA gameFindData;
	HANDLE h = XFindFirstSaveGame(GetRootPath(), &gameFindData);
	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			new(*ProfileName) FString(appFromUnicode(gameFindData.szSaveGameName));
		} while(XFindNextSaveGame(h, &gameFindData));
	}

	if (GetLastError() != ERROR_NO_MORE_FILES)
	{
		// deal with error here
	}


	if ((h != INVALID_HANDLE_VALUE) && (!XFindClose(h)))
	{
		// deal with error here
	}

	return (ProfileName->Num() > 0);

	unguard;
}

// ============================================================================

UBOOL UXboxLoadSaveManager::ExistsGameObject(const FString & profileName) const
{
	guard(UXboxLoadSaveManager::ExistsGameObject);
	CHAR pathBuffer[MAX_PATH];
	DWORD dwSuccess = XCreateSaveGame( GetRootPath(), appToUnicode(*profileName), OPEN_EXISTING, 0, pathBuffer, MAX_PATH );
	return (dwSuccess == ERROR_SUCCESS);
	unguard;

}

FString UXboxLoadSaveManager::GetProfileRoot() const
{
	return FString::Printf(TEXT("U:%s"), PATH_SEPARATOR);
}

UBOOL UXboxLoadSaveManager::GetSizeParams( QWORD& out_TotalBytes, QWORD& out_UsedBytes, QWORD& out_FreeBytes ) const
{
	guard(UXboxLoadSaveManager::GetTotalSize);

	ULARGE_INTEGER FreeBytes, TotalBytes, TotalFreeBytes;
	if ( !GetDiskFreeSpaceEx(GetRootPath(), &FreeBytes, &TotalBytes, &TotalFreeBytes) )
		return FALSE;

	out_FreeBytes = FreeBytes.QuadPart;
	out_TotalBytes = TotalBytes.QuadPart;
	out_UsedBytes = out_TotalBytes - out_FreeBytes;
	return TRUE;

	unguard;
}

DWORD UXboxLoadSaveManager::GetSaveGameOverhead() const
{
	guard(UXboxLoadSaveManager::GetSaveGameOverhead);
	// Potential overhead includes:
	//
	//   Title directory: 1 cluster
	//   Title metadata file (TitleMeta.xbx): 1 cluster
	//   Title image (TitleImage.xbx): 1 cluster
	//   Default save image (SaveImage.xbx): 1 cluster
	//
	// These four clusters are only allocated once per title, not once per 
	// save. However, since the Xbox Dashboard will automatically delete 
	// these clusters if there are no saved games, they must be accounted 
	// for if there are no saved games.

	// If no saves, then add 4 clusters
	DWORD dwClusters = 0;
	XGAME_FIND_DATA SaveGameData;
	HANDLE hAnySaves = XFindFirstSaveGame( GetRootPath(), &SaveGameData );
	if( hAnySaves == INVALID_HANDLE_VALUE )
		dwClusters += 4;
	else
		XFindClose( hAnySaves );

	// Normal overhead includes:
	//
	//   Save game directory: 1 cluster
	//   Save game metadata (SaveMeta.xbx): 1 cluster
	//   
	// Additional potential overhead includes the save game 
	// image (SaveImage.xbx), but because that is in control of the title,
	// the title can account for that directly.

	dwClusters += 2;
	return( GetClusterSize() * dwClusters );

	unguard;
}

DWORD UXboxLoadSaveManager::GetMaxSaveGameSize( UBOOL bBlocks ) const
{
	guard(UXboxLoadSaveManager::GetMaxSaveGameSize);

	DWORD HeaderSize = sizeof(DWORD) + XCalculateSignatureGetSize(XCALCSIG_FLAG_SAVE_GAME);
	
	DWORD SaveGameSize = GetFileBytes(MAX_PROFILE_SIZE + HeaderSize);
	DWORD SaveGameImageSize = GetFileBytes( IMAGE_META_HDR_SIZE + IMAGE_META_DATA_SIZE );
	DWORD SaveGameOverhead = GetSaveGameOverhead();

	DWORD TotalSaveGameBytes = SaveGameSize + SaveGameImageSize + SaveGameOverhead;
	return bBlocks
		? (TotalSaveGameBytes % BLOCK_SIZE == 0
			? TotalSaveGameBytes / BLOCK_SIZE			// if the total bytes is exactly divisible by the block size, we won't need an extra block
			: (TotalSaveGameBytes / BLOCK_SIZE) + 1)	// otherwise, add an extra block to account for the remainder bytes
		: TotalSaveGameBytes;

	unguard;
}

UBOOL UXboxLoadSaveManager::IsEnoughSpaceAvailable( DWORD* out_BlocksNeeded ) const
{
	guard(UXboxLoadSaveManager::IsEnoughSpaceAvailable);

	if ( out_BlocksNeeded )
		*out_BlocksNeeded = 0;

	QWORD TotalBytes, UsedBytes, FreeBytes;

	if ( !GetSizeParams(TotalBytes, UsedBytes, FreeBytes) )
	{
		// error message
		return false;
	}

	// we have lots of free space left
	if ( FreeBytes > ULONG_MAX )
		return true;

	DWORD dFreeBytes = (DWORD)FreeBytes;
	if ( GetMaxSaveGameSize() <= dFreeBytes )
		return true;

	if ( out_BlocksNeeded )
	{
		FLOAT NeededBytes = GetMaxSaveGameSize() - dFreeBytes;
		*out_BlocksNeeded = (DWORD)NeededBytes % BLOCK_SIZE > 0 
			? (NeededBytes / BLOCK_SIZE) + 1
			: NeededBytes / BLOCK_SIZE;
	}

	return false;
	unguard;
}

/*==========================================================================================================
function	UXboxLoadSaveManager::SetDisconnectFlag
Desc:		Stores a flag on the title-persistent data region regarding whether the user has a pending
			disconnect to be written to live.

last modified by:	jg (rewrote)
==========================================================================================================*/
void UXboxLoadSaveManager::SetDisconnectFlag( UBOOL bNewValue, UBOOL bTeamGame ) 
{
	guard(UXboxLoadSaveManager::SetDisconnectFlag);
	UBOOL bIsError = FALSE;
	// Create the file with overwrite
	HANDLE hFile = CreateFile(DISCONNECT_FILENAME,GENERIC_WRITE,0,NULL,
		CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
	if (hFile != INVALID_HANDLE_VALUE)
	{
		DWORD NumWritten;
		// Write the first bool
		if (WriteFile(hFile,&bNewValue,sizeof(UBOOL),&NumWritten,NULL) &&
			NumWritten == sizeof(UBOOL))
		{
			// Now write the second bool
			if (WriteFile(hFile,&bTeamGame,sizeof(UBOOL),&NumWritten,NULL) == FALSE ||
				NumWritten != sizeof(UBOOL))
			{
				debugf(TEXT("Failed to write to the disconnect flag file"));
				bIsError = TRUE;
			}
		}
		else
		{
			debugf(TEXT("Failed to write to the disconnect flag file"));
			bIsError = TRUE;
		}
		CloseHandle(hFile);
	}
	else
	{
		debugf(TEXT("Failed to create to the disconnect flag file"));
		bIsError = TRUE;
	}
	// Only sign the file if everthing went ok
	if (bIsError == FALSE)
	{
		bIsError = GFileManager->SignFile(DISCONNECT_FILENAME,DISCONNECT_VERSION,SIG_FLAG_NON_ROAMABLE) != TRUE;
	}
	if (bIsError == TRUE)
	{
		// Don't leave a partial/corrupt file around
		DeleteFile(DISCONNECT_FILENAME);
	}
	unguard;
}

/*==========================================================================================================
function	UXboxLoadSaveManager::GetDisconnectFlag
Desc:		Retrieves the disconnect flag on the title-persisten data region

last modified by:	Ron Prestenback
==========================================================================================================*/
UBOOL UXboxLoadSaveManager::GetDisconnectFlag( BYTE& byTeamGame ) 
{
	guard(UXboxLoadSaveManager::GetDisconnectFlag);
	if ( !GFileManager->Exists(DISCONNECT_FILENAME) )
		return 0;
	UBOOL bResult = 1;
	// Make sure the file is valid
	if (GFileManager->IsSignedFileValid(DISCONNECT_FILENAME,DISCONNECT_VERSION,SIG_FLAG_NON_ROAMABLE))
	{
		// Create the file with overwrite
		HANDLE hFile = CreateFile(DISCONNECT_FILENAME,GENERIC_READ,0,NULL,
			OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
		if (hFile != INVALID_HANDLE_VALUE)
		{
			DWORD NumRead;
			// Read the first bool
			if (ReadFile(hFile,&bResult,sizeof(UBOOL),&NumRead,NULL) &&
				NumRead == sizeof(UBOOL))
			{
				UBOOL bIsTeamGame = FALSE;
				// Now the second bool
				ReadFile(hFile,&bIsTeamGame,sizeof(UBOOL),&NumRead,NULL);
				// Make a byte
				byTeamGame = (BYTE)bIsTeamGame;
			}
			else
			{
				debugf(TEXT("Failed to read to the disconnect flag file"));
			}
			CloseHandle(hFile);
		}
		else
		{
			debugf(TEXT("Failed to create to the disconnect flag file"));
		}
	}
	else
	{
		// Don't leave a partial/corrupt file around
		DeleteFile(DISCONNECT_FILENAME);
	}
	return bResult;
	unguard;
}



