/*=============================================================================
	FFileManagerLinear.h
	Copyright 1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

UBOOL DebugLinear = 0;
#define LINEAR_DATA  0x43
#define LINEAR_SEEK  0x67
#define LINEAR_SIZE  0xEC
#define LINEAR_CLOSE 0xB3

//jg -- Added this back because Josh was smarter than me and
// I defeated myself by removing it
FArchive* GLinearFile = NULL;

void HandleLinearURL(const TCHAR* S)
{
	return;
}

class FArchiveFileReaderSaveLinear : public FArchive
{
	/**
	 * The archive that we are reading from
	 */
	FArchive& Ar;

public:
	FArchiveFileReaderSaveLinear(FArchive& InAr) :
		Ar(InAr)
	{
		ArIsLoading = ArIsPersistent = 1;
	}
	UBOOL Close()
	{
		guard(FArchiveFileReaderSaveLinear::Close);
		BYTE B=LINEAR_CLOSE;
		if (GLinearFile != NULL)
		{
			(*GLinearFile) << B;
		}
        return Ar.Close();
		unguard;
	}
	INT Tell()
	{
		return Ar.Tell();
	}
	INT TotalSize()
	{
		return Ar.TotalSize();
	}
	void Seek(INT InPos)
	{
		guard(FArchiveFileReaderSaveLinear::Seek);
		Ar.Seek(InPos);
		unguard;
	}
	void Serialize( void* V, INT Length )
	{
		guard(FArchiveFileReaderSaveLinear::Serialize);
		Ar.Serialize(V,Length);
		if (GLinearFile != NULL)
		{
			GLinearFile->Serialize(V,Length);
			GLinearBytesSerialized += Length;
		}
		unguard;
	}
};

/**
 * This class is based off of code from Josh Adams at Secret Level. It allows
 * for linear files that can be closed and automatically become dynamic.
 */
class FFileReaderLinear : public FArchive
{
	/**
	 * The file to use when the "tear" off occurs
	 */
	FArchive* AltReader;
	/**
	 * The file to open when the tear off event occurs
	 */
	FString FileName;
	/**
	 * The offset into the file. This is faked since seeking doesn't work in LINs
	 */
	INT FakePos;
	/**
	 * The size of the file
	 */
	INT Size;
	/**
	 * Used to create the tear off file
	 */
	FFileManager* FileManager;

public:
	/**
	 * Constructor. Inits members
	 */
	FFileReaderLinear(const TCHAR* InFileName,INT InSize,FFileManager* FM) :
		Size(InSize), FileManager(FM)
	{
		guard(FFileReaderLinear::FFileReaderLinear);
		check(InSize > 0);
		// Strip off the path info
		const TCHAR* strFile = appStrrchr(InFileName,TEXT('\\'));
		FileName = strFile ? strFile + 1 : InFileName;
		ArIsLoading = ArIsPersistent = 1;
		FakePos = 0;
		AltReader = NULL;
		unguard;
	}

	/**
	 * Destructor. Cleans up file reader
	 */
	~FFileReaderLinear()
	{
		guard(FFileReaderLinear::~FFileReaderLinear);
		// Only delete the tear off file if present
		if (AltReader != NULL)
		{
			debugf(NAME_DevJoe,TEXT("Closing tear off file %s"),*FileName);
			delete AltReader;
		}
		else
		{
			debugf(NAME_DevJoe,TEXT("Closing non-tear off file %s"),*FileName);
		}
		unguard;
	}

	/**
	 * This function is used as notification that the linear file was closed
	 * it creates the AltReader
	 *
	 * @param FM the file manager to create the "tear" off file with
	 */
	void TearOff(void)
	{
		guard(FFileReaderLinear::Tell);
		debugf(NAME_DevJoe,TEXT("Tearing off file (%s)"),*FileName);
		TCHAR LocalName[_MAX_PATH];
		// Have the TOC file fill in the path information
		if (GFileManager->FindPackageFile(*FileName,LocalName) == TRUE)
		{
			FileName = LocalName;
		}
		// Create the file that will perform the reading
		AltReader = FileManager->CreateFileReader(*FileName);
		check(AltReader != NULL && "Failed to create tear off file");
		// Seek to where it thinks we are
		AltReader->Seek(FakePos);
		unguard;
	}

	INT Tell()
	{
		guard(FFileReaderLinear::Tell);
		if (GLinearFile == NULL)
		{
			if (AltReader == NULL)
			{
				TearOff();
			}
			return AltReader->Tell();
		}
		return FakePos;
		unguard;
	}
	INT TotalSize()
	{
		return Size;
	}
	void Seek(INT InPos)
	{
		guard(FFileReaderLinear::Seek);
		if (GLinearFile == NULL)
		{
			if (AltReader == NULL)
			{
				TearOff();
			}
			AltReader->Seek(InPos);
		}
		else
		{
			FakePos = InPos;
		}
		unguard;
	}
	void Serialize( void* V, INT Length )
	{
		guard(FFileReaderLinear::Serialize);
		if (GLinearFile == NULL)
		{
			if (AltReader == NULL)
			{
				TearOff();
			}
			AltReader->Serialize(V, Length);
		}
		else
		{
			GLinearFile->Serialize(V,Length);
			GLinearBytesSerialized += Length;
			check(!GLinearFile->IsError());
			FakePos += Length;
		}
		unguard;
	}
};


/**
 * This class is based off of code from Josh Adams at Secret Level. It allows
 * for linear files that can be closed and automatically become dynamic.
 */
class FFileManagerLinear : public FFileManagerGeneric
{
	/**
	 * The file manager to forward calls to
	 */
	FFileManager* FM;

public:
	FFileManagerLinear(FFileManager* InFM)
	{
		FM=InFM;
		GLinearFile = NULL;
	}
	~FFileManagerLinear()
	{
		delete GLinearFile;
	}
	void Init(UBOOL Startup)
	{
		FM->Init(Startup);
	}
	UBOOL IsLinear(const TCHAR* Filename)
	{
		// Only do the search if there is a linear file open
		UBOOL bRet = GLinearFile != NULL;
		if (bRet == 1)
		{
			// Get a pointer to the extension
			const TCHAR* Ext = &Filename[appStrlen(Filename) - 4];
			// And another to the localization language
			const TCHAR* LocExt = Ext + 1;
			// Loop through the various localization files and check for matches
			// we don't want them included in the LIN file
			for (INT Index = 0; Index < LC_MAXLANGS && bRet; Index++)
			{
				bRet = appStricmp(GLanguageExt[Index],LocExt) != 0;
			}
			if (bRet == 1)
			{
				// Now make sure to skip other files
				bRet = appStricmp(Ext,TEXT(".ini")) != 0
					&& appStricmp(Ext,TEXT(".xpr")) != 0
					&& appStricmp(Ext,TEXT(".xmv")) != 0;
			}
		}
		return bRet;
	}

	/**
	 * Forwards it to the aggregated file manager
	 *
	 * @param FileName the file to digitally sign
	 * @param VersionNumber the version of the file to add
	 * @param SignType the type of signature to create
	 */
	UBOOL SignFile(const TCHAR* FileName,DWORD VersionNumber,
		ESignatureType SignType = SIG_FLAG_SAVE_GAME)
	{
		guard(SignFile);
		return FM->SignFile(FileName,VersionNumber,SignType);
		unguard;
	}

	/**
	 * Forwards it to the aggregated file manager
	 *
	 * @param FileName the file to digitally sign
	 * @param VersionNumber the version of the file to add
	 * @param SignType the type of signature to create
	 */
	UBOOL IsSignedFileValid(const TCHAR* FileName,DWORD VersionNumber,
		ESignatureType SignType = SIG_FLAG_SAVE_GAME)
	{
		guard(IsSignedFileValid);
		return FM->IsSignedFileValid(FileName,VersionNumber,SignType);
		unguard;
	}

	/**
	 * Simply forwards to the TOC file manager
	 *
	 * @param Filename the file to check the size on
	 */
	INT GetTOCFileSize(const TCHAR* Filename)
	{
		guard(GetTOCFileSize);
		return FM->GetTOCFileSize(Filename);
		unguard;
	}

	/**
	 * Simply forwards to the TOC file manager
	 *
	 * @param Names the array that will get their file names
	 * @param Extension the extension to compare
	 */
	void GetFileListByExtension(TArray<FString>& Names,const TCHAR* Extension,
		const TCHAR* Path = NULL)
	{
		guard(GetFileListByExtension);
		FM->GetFileListByExtension(Names,Extension,Path);
		unguard;
	}

	/**
	 * Closes the current linear file which notifies all of the readers that they
	 * need to open "tear" off versions so that loading still functions.
	 */
	void CloseLinearFile(void)
	{
		guard(CloseLinearFile);
		if (GLinearFile != NULL)
		{
			debugf(NAME_DevJoe,
				TEXT("Closing previous Linear File, %d bytes were serialized"),
				GLinearBytesSerialized);
			delete GLinearFile;
			GLinearFile = NULL;
		}
		unguard;
	}

	/**
	 * Opens a linear file
	 */
	void OpenLinearFile(const TCHAR* FileName)
	{
		guard(OpenLinearFile);
		// Close the previous file if open
		CloseLinearFile();
		GLinearBytesSerialized = 0;
#ifdef LIN_SAVE
		GLinearFile = FM->CreateFileWriter(FileName,FILEWRITE_NoFail);
		debugf(NAME_DevJoe,TEXT("Saving to linear image [%s]"),FileName);
#endif
#ifdef LIN_LOAD
		TCHAR Out[255];
		// Use the TOC to find the directory for us
		if (FM->FindPackageFile(FileName,Out))
		{
			// Create the DVD sector optimized reader
			GLinearFile = FM->CreateLinearReader(Out);
			if (GLinearFile != NULL)
			{
				debugf(NAME_DevJoe,TEXT("Loading from linear image [%s]"),Out);
			}
			else
			{
				debugf(TEXT("Failed to load linear file %s"),Out);
			}
		}
		else
		{
			debugf(TEXT("Failed to find linear file %s"),FileName);
		}
#endif
		unguard;
	}

	/**
	 * Indicates whether there is a linear file open currently
	 */
	UBOOL IsLinearFileOpen(void)
	{
		return GLinearFile != NULL;
	}

	/**
	 * Just forward the call to the aggregated FM
	 */
	FString ResolvePath(const TCHAR* InFilename)
	{
		guard(ResolvePath);
		return FM->ResolvePath(InFilename);
		unguard;
	}

	/**
	 * Just forward the call to the aggregated FM
	 */
	FArchive* CreateLinearReader(const TCHAR* FileName)
	{
		guard(CreateLinearReader);
		return FM->CreateLinearReader(FileName);
		unguard;
	}

	/**
	 * Just forward the call to the aggregated FM
	 */
	void CreateCacheDirectory(const ANSICHAR* DirName)
	{
		guard(CreateCacheDirectory);
		FM->CreateCacheDirectory(DirName);
		unguard;
	}

	FArchive* CreateFileReader( const TCHAR* Filename, DWORD Flags, FOutputDevice* Error )
	{
		guard(CreateFileReader);
		FArchive* Result;
		if (IsLinear(Filename))
		{
			Result = NULL;
#ifdef LIN_SAVE
			debugf(NAME_DevJoe,TEXT("Linearizing file '%s'"), Filename);
			FArchive* Underlying = FM->CreateFileReader(Filename, Flags, Error);
			check(Underlying);
			FArchiveFileReaderSaveLinear* Writer = 
				NEW FArchiveFileReaderSaveLinear(*Underlying);
			Result = Writer;
#endif
#ifdef LIN_LOAD
			debugf(NAME_DevJoe,TEXT("CREATED LINEAR READER FOR %s"), Filename);
			// Create the linear reader that will tear off later
			FFileReaderLinear* Reader =
				NEW FFileReaderLinear(Filename, FileSize(Filename),FM);
			Result = Reader;
#endif
		}
		else
		{
			Result = FM->CreateFileReader(Filename, Flags, Error);
		}
		return Result;
		unguard;
	}

	FArchive* CreateFileWriter( const TCHAR* Filename, DWORD Flags, FOutputDevice* Error )
	{
		return FM->CreateFileWriter(Filename,Flags,Error);
	}
	UBOOL Delete( const TCHAR* Filename, UBOOL RequireExists=0, UBOOL EvenReadOnly=0 )
	{
		return FM->Delete(Filename,RequireExists,EvenReadOnly);
	}
	DWORD Copy( const TCHAR* InDestFile, const TCHAR* InSrcFile, UBOOL ReplaceExisting, UBOOL EvenIfReadOnly, UBOOL Attributes, DWORD Compress, FCopyProgress* Progress )
	{
		return FM->Copy(InDestFile, InSrcFile, ReplaceExisting, EvenIfReadOnly, Attributes, Compress, Progress);
	}
	INT FileSize( const TCHAR* Filename )
	{
		return FM->FileSize(Filename);
	}
	//scion capps: stolen from ut2k4
	UBOOL IsReadOnly( const TCHAR* Filename )
	{
		return FM->IsReadOnly(Filename);
	}
	SQWORD GetGlobalTime( const TCHAR* Filename )
	{
		return FM->GetGlobalTime(Filename);
	}
	UBOOL SetGlobalTime( const TCHAR* Filename )
	{
		return FM->SetGlobalTime(Filename);
	}
	UBOOL MakeDirectory( const TCHAR* Path, UBOOL Tree=0 )
	{
		return FM->MakeDirectory(Path,Tree);
	}
	UBOOL DeleteDirectory( const TCHAR* Path, UBOOL RequireExists=0, UBOOL Tree=0 )
	{
		return FM->DeleteDirectory(Path,RequireExists,Tree);
	}
	TArray<FString> FindFiles( const TCHAR* Filename, UBOOL Files, UBOOL Directories )
	{
		return FM->FindFiles(Filename,Files,Directories);
	}
	UBOOL SetDefaultDirectory( const TCHAR* Filename )
	{
		return FM->SetDefaultDirectory(Filename);
	}
	FString GetDefaultDirectory()
	{
		return FM->GetDefaultDirectory();
	}
	UBOOL PrintCorruptedContent(FOutputDevice& Ar)
	{
//		return FM->PrintCorruptedContent(Ar);
	}

	UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar )
	{
		return 0;
	}
	UBOOL FindPackageFile(const TCHAR* In,TCHAR* Out)
	{
		return FM->FindPackageFile(In,Out);
	}
	TArray<FString> GetContentLocalizationFiles()
	{
		return FM->GetContentLocalizationFiles();
	}
	void SetContentLocalizationFiles(const TArray<FString>& InContentLocalizationFiles)
	{
		FM->SetContentLocalizationFiles(InContentLocalizationFiles);
	}
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

