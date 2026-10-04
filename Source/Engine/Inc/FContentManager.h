#ifndef __FCONTENTMANAGER_H__
#define __FCONTENTMANAGER_H__

/* epic ====================================================================================================
* Class:	FContentManager
* Author:	Per Vognsen
*
* The content manager is responsible for managing Xbox Live downloadable content and auto-updates. Its
* duties include checking the validity of such files via digital signatures before they are used by other
* parts of the engine, and maintaining a table of contents to facilitate fast filesystem access.
* ==========================================================================================================
*/
class ENGINE_API FContentManager
{
public:
	/* epic ====================================================================================================
	* Function:	FContentManager::GetInstance
	* Author:	Per Vognsen
	*
	* Returns the singleton instance of the content manager.
	* ==========================================================================================================
	*/
	static FContentManager* GetInstance();

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
	* ==========================================================================================================
	*/
	UBOOL Exec(const TCHAR* Cmd, FOutputDevice& Ar);

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
	UBOOL FindFile(const TCHAR* FileName, TCHAR* OutFullPath);

	/* epic ====================================================================================================
	* Function:	FContentManager::Initialize
	* Author:	Per Vognsen
	*
	* Processes all autoupdate and downloadable content directories, checking integrity via digital signatures,
	* building the table of contents, and keeping track of corrupted content directories.
	* ==========================================================================================================
	*/
	void Initialize();

	/* epic ====================================================================================================
	* Function:	FContentManager::HasCorruptedContent
	* Author:	Per Vognsen
	*
	* Checks if there is any corrupted content directories in the title-specific region.
	* ==========================================================================================================
	*/
	UBOOL HasCorruptedContent();

	/* epic ====================================================================================================
	* Function:	FContentManager::DeleteCorruptedContent
	* Author:	Per Vognsen
	*
	* Deletes all directories that have been marked corrupted/invalid.
	* ==========================================================================================================
	*/
	void DeleteCorruptedContent();

	/* epic ====================================================================================================
	* Function:	FContentManager::UpdateMapLists
	* Author:	Per Vognsen
	*
	* Creates updated map lists by combining map lists for the downloaded maps with the master map list and
	* writing it out to the cache HD partition.
	* ==========================================================================================================
	*/
	void UpdateMapLists();

	/* epic ====================================================================================================
	* Function:	FContentManager::UpdateMapLimits
	* Author:	Per Vognsen
	*
	* Updates information about recommended and maximum players for downloadable maps.
	* ==========================================================================================================
	*/
	void UpdateMapLimits();

	/* epic ====================================================================================================
	* Function:	FContentManager::UpdateConfigs
	* Author:	Per Vognsen
	*
	* Updates configuration files to incorporate information about downloadable maps.
	* ==========================================================================================================
	*/
	void UpdateConfigs();

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
	UBOOL IsCustomContent( const FString& ContentName );

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
	UBOOL IsActivated(const FString& Name);

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
	UBOOL HasMap(const FString& Map);

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
	void StartDownloader(DWORD Port);

private:
	typedef void (FContentManager::*FHandlerFunction)(const FString& FullPathName);

	struct FHandlerEntry
	{
		const TCHAR* FileExtention;
		FHandlerFunction Handler;
	};

	static FHandlerEntry HandlerTable[];

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
	void HandleActivationFile(const FString& FullPathName);

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
	void HandleLocalizationFile(const FString& FullPathName);

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
	void HandleMapFile(const FString& FullPathName);

	// The default constructor is private to ensure that FContentManager be used as a singleton.
	FContentManager() { }

	/* epic ====================================================================================================
	* Function:	FContentManager::IsDirectoryValid
	* Author:	Per Vognsen
	*
	* Checks whether a root directory in the title-specific region is valid by checking the digital signatures
	* in the accompanying metadata file. Returns TRUE if valid, FALSE otherwise.
	* ==========================================================================================================
	*/
	UBOOL IsDirectoryValid(const FString& Directory);

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
	void ProcessDirectory(const FString& Directory, UBOOL bDownloadableContent);

	// The corrupted directories 
	TArray<FString> CorruptedDirectories;

	// The table of contents for all non-corrupted/verified files in the title-specific region.
	TArray<FFileData> TOC;

	// The list of activated/unlocked features (character skins, maps, etc).
	TArray<FString> ActivatedFeatures;

	// The list of downloaded maps.
	TArray<FString> Maps;
};

#endif // __FCONTENTMANAGER_H__