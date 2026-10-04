		TOCData = FileSystemTOC;
		NumEntries = ARRAY_COUNT(FileSystemTOC);
	}

	/**
	 * Search the TOC data for the specified file
	 */
	INT GetTOCFileSize(const TCHAR* Filename)
	{
		guard(GetTOCFileSize);
		// Do a partial search if there is a full path specified
		TCHAR* Slash = strrchr(Filename, '\\');
		const TCHAR* ShortName = Slash ? Slash + 1 : Filename;
		// Check each entry (brute force :( )
		for (DWORD Index = 0; Index < NumEntries; Index++)
		{
			// Check for a matching file name
			if (TOCData[Index].FileName != NULL &&
				appStricmp(TOCData[Index].FileName,ShortName) == 0)
			{
				return TOCData[Index].FileSize;
			}
		}
		return -1;
		unguard;
	}


	/**
	 * Search the TOC data for the specified file
	 */
	INT FileSize(const TCHAR* Filename)
	{
		guard(FileSize);
		// Use fallback method if the file is on the Z/U/T: drive
		if (Filename[1] == TEXT(':') &&
			(appToLower(Filename[0]) == TEXT('z') ||
			 appToLower(Filename[0]) == TEXT('u') ||
			 appToLower(Filename[0]) == TEXT('t')))
		{
			return FFileManagerXbox::FileSize(Filename);
		}

		FContentManager* ContentManager = FContentManager::GetInstance();
		if (ContentManager != NULL)
		{
			TCHAR* FullPath = appStaticString1024();
			if (ContentManager->FindFile(Filename, FullPath))
			{
				return FFileManagerXbox::FileSize(FullPath);
			}
		}

		return GetTOCFileSize(Filename);
		unguard;
	}

	/**
	 * Rather than generating tons of various combinations for finding the
	 * name of a package, just search our list for a match and return that
	 * as the path name
	 */
	UBOOL FindPackageFile(const TCHAR* In,TCHAR* Out)
	{
		guard(FindPackageFile);
		// Are we doing an exact name search or not. No extension means
		// use the substring search
		UBOOL bExact = strrchr(In,'.') != NULL;
		INT CompareNum = appStrlen(In);
		// Check to see if the demo path is there or not
		UBOOL bNeedsDemoPath = GFileManager->GetDefaultDirectory().Len() > 3;
		// Search the table of contents for matching package names
		for (DWORD Index = 0; Index < NumEntries; Index++)
		{
			if (TOCData[Index].FileName != NULL && TOCData[Index].PathName != NULL &&
				((bExact == TRUE && appStricmp(TOCData[Index].FileName,In) == 0) ||
				(bExact == FALSE && appStrnicmp(TOCData[Index].FileName,In,CompareNum) == 0 &&
				TOCData[Index].FileName[CompareNum] == TEXT('.'))))
			{
				if (bNeedsDemoPath == TRUE)
				{
					// Strip off the path info
					const TCHAR* ModifiedPath = appStrchr(TOCData[Index].PathName,
						TEXT('\\')) + 1;
					// Now create demopath\modifiedpath\filename
					appSprintf(Out,TEXT("%s%s%s"),
						*GFileManager->GetDefaultDirectory(),ModifiedPath,
						TOCData[Index].FileName);
				}
				else
				{
					// We have a match so create the out name
					appSprintf(Out,TEXT("%s%s"),TOCData[Index].PathName,
						TOCData[Index].FileName);
				}
				// Give the content manager a chance to override the file
				FContentManager* ContentManager = FContentManager::GetInstance();
				if (ContentManager != NULL)
				{
					TCHAR NewOut[255];
                    if (ContentManager->FindFile(Out,NewOut))
					{
						appStrcpy(Out,NewOut);
					}
				}
				return TRUE;
			}
		}
		return FALSE;
		unguard;
	}

	/**
	 * Builds a list of files by their extension. It scans the TOC looking for
	 * files that have the same extension as was specified and makes a list
	 *
	 * @param Names the array that will get their file names
	 * @param Extension the extension to compare
	 */
	void GetFileListByExtension(TArray<FString>& Names,const TCHAR* Extension,
		const TCHAR* Path = NULL)
	{
		guard(GetFileListByExtension);
		// Skip the wildcard chara
		if (Extension[0] == TEXT('*'))
		{
			Extension++;
		}
		// Figure out how long the extension specified
		INT ExtLen = appStrlen(Extension);
		// Search the table of contents for matching package names
		for (DWORD Index = 0; TOCData[Index].FileName != NULL; Index++)
		{
			// Find the extension only
			const TCHAR* Ext = TOCData[Index].FileName +
				(appStrlen(TOCData[Index].FileName) - ExtLen);
			// Compare only the extensions
			if (appStrnicmp(Extension,Ext,ExtLen) == 0)
			{
				// If we didn't specify a path, or the paths match
				if (Path == NULL || (TOCData[Index].PathName != NULL &&
					appStricmp(Path,TOCData[Index].PathName) == 0))
				{
					// Add to the array
					new(Names)FString(TOCData[Index].FileName);
				}
			}
		}
		unguard;
	}
};

