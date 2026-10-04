/*=============================================================================
	FFileManagerXbox.h: Unreal X-Box based file manager.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "FFileManagerGeneric.h"
#include "FContentManager.h"
#include "../../zlib/zlib.h"

#include <sys/types.h>
#include <sys/stat.h>

//jg -- Our catch all file handler
void ShowDirtyDiscError(void);

#define COPY_BUFFER_SIZE		65536
#define SIG_OVERHEAD (XCALCSIG_SIGNATURE_SIZE + sizeof(DWORD))


/*-----------------------------------------------------------------------------
	File Manager.
-----------------------------------------------------------------------------*/

// File manager.
class FArchiveFileReaderCompressed : public FArchive
{
public:
	FArchiveFileReaderCompressed( HANDLE InHandle, FOutputDevice* InError, INT InSize )
	:	Handle			( InHandle )
	,	Error			( InError )
	,	Size			( InSize )
	,	Pos				( 0 )
	,	BufferBase		( 0 )
	,	BufferCount		( 0 )
	{
		guard(FArchiveFileReaderCompressed::FArchiveFileReaderCompressed);
		ArIsLoading = ArIsPersistent = 1;
		BufferSize = ARRAY_COUNT(Buffer);
		unguard;
	}
	~FArchiveFileReaderCompressed()
	{
		guard(FArchiveFileReaderCompressed::~FArchiveFileReaderCompressed);
		if( Handle )
			Close();
		unguard;
	}
	void Seek( INT InPos )
	{
		guard(FArchiveFileReaderCompressed::Seek);
		appErrorf(TEXT("Can't seek compressed file"));
		unguard;
	}
	INT Tell()
	{
		return Pos;
	}
	INT TotalSize()
	{
		return Size;
	}
	UBOOL Close()
	{
		guardSlow(FArchiveFileReaderCompressed::Close);
		if( Handle )
			CloseHandle( Handle );
		Handle = NULL;
		return !ArIsError;
		unguardSlow;
	}
	void Serialize( void* V, INT Length )
	{
		Pos += Length;
		INT Offset = 0;
		while( Length )
		{
			if( Length > BufferCount )
			{
				if( BufferCount )
				{
					appMemcpy( (BYTE*) V + Offset, Buffer+BufferBase, BufferCount );
					Length		-= BufferCount;
					Offset		+= BufferCount;
				}

				DWORD Count			= 0;
				DWORD Data[2];
				DWORD& Uncompressed	= Data[0];
				DWORD& Compressed	= Data[1];
				
				ReadFile( Handle, Data, 8, &Count, NULL );
				ReadFile( Handle, &Buffer[16384], Compressed, &Count, NULL );				

				if( uncompress( Buffer, &Uncompressed, &Buffer[16384], Compressed ) != Z_OK )
				{
					// Universal disc error message
					ShowDirtyDiscError();
					appErrorf(TEXT("zlib: uncompress failed"));
				}

				BufferCount = Uncompressed;
				BufferBase	= 0;
			}
			else
			{
				appMemcpy( (BYTE*) V + Offset, Buffer+BufferBase, Length );
				BufferCount -= Length;
				BufferBase  += Length;
				Length		= 0;
			}
		}
	}
protected:
	HANDLE			Handle;
	FOutputDevice*	Error;
	INT				Size;
	INT				Pos;
	INT				BufferBase;
	INT				BufferCount;
	INT				BufferSize;
	BYTE			Buffer[32768];
};



// File manager.
class FArchiveFileReader : public FArchive
{
public:
	FArchiveFileReader( HANDLE InHandle, FOutputDevice* InError, INT InSize, UBOOL InIsLinear )
	:	Handle			( InHandle )
	,	Error			( InError )
	,	Size			( InSize )
	,	Pos				( 0 )
	,	BufferBase		( 0 )
	,	BufferCount		( 0 )
	,	IsLinear		( InIsLinear )
	{
		guard(FArchiveFileReader::FArchiveFileReader);
		ArIsLoading = ArIsPersistent = 1;
		if( IsLinear )
		{
			BufferSize = 16384;
			Buffer = new BYTE[BufferSize];			
		}
		else
		{
			BufferSize = ARRAY_COUNT(RegularBuffer);
			Buffer = RegularBuffer;
		}

		unguard;
	}
	~FArchiveFileReader()
	{
		guard(FArchiveFileReader::~FArchiveFileReader);
		if( Handle )
			Close();
		if( BufferSize > 2048 )
			delete [] Buffer;
		Buffer = NULL;
		unguard;
	}
	void Precache( INT HintCount )
	{
		guardSlow(FArchiveFileReader::Precache);
		checkSlow(Pos==BufferBase+BufferCount);
		BufferBase	= Pos;
		BufferCount = Min( Min( HintCount, (INT)(BufferSize - (Pos&(BufferSize-1))) ), Size-Pos );
		INT Count=0;
		ReadFile( Handle, Buffer, BufferCount, (DWORD*)&Count, NULL );
		if( Count!=BufferCount )
		{
			ArIsError = 1;
			Error->Logf( TEXT("ReadFile failed: Count=%i BufferCount=%i Error=%s"), Count, BufferCount, appGetSystemErrorMessage() );
			// Universal disc error message
			ShowDirtyDiscError();
		}
		unguardSlow;
	}
	void Seek( INT InPos )
	{
		guard(FArchiveFileReader::Seek);
		check(InPos>=0);
		check(InPos<=Size);
		if( SetFilePointer( Handle, InPos, 0, FILE_BEGIN )==0xFFFFFFFF )
		{
			ArIsError = 1;
			Error->Logf( TEXT("SetFilePointer Failed %i/%i: %i %s"), InPos, Size, Pos, appGetSystemErrorMessage() );
			// Universal disc error message
			ShowDirtyDiscError();
		}
		Pos         = InPos;
		BufferBase  = Pos;
		BufferCount = 0;
		unguard;
	}
	INT Tell()
	{
		return Pos;
	}
	INT TotalSize()
	{
		return Size;
	}
	UBOOL Close()
	{
		guardSlow(FArchiveFileReader::Close);
		if( Handle )
			CloseHandle( Handle );
		Handle = NULL;
		return !ArIsError;
		unguardSlow;
	}
	void Serialize( void* V, INT Length )
	{
		guardSlow(FArchiveFileReader::Serialize);
		while( Length>0 )
		{
			INT Copy = Min( Length, BufferBase+BufferCount-Pos );
			if( Copy==0 )
			{
				if( Length >= BufferSize )
				{
					INT Count=0;
					ReadFile( Handle, V, Length, (DWORD*)&Count, NULL );
					if( Count!=Length )
					{
						ArIsError = 1;
						Error->Logf( TEXT("ReadFile failed: Count=%i Length=%i Error=%s"), Count, Length, appGetSystemErrorMessage() );
						// Universal disc error message
						ShowDirtyDiscError();
					}
					Pos += Length;
					BufferBase += Length;
					return;
				}
				Precache( MAXINT );
				Copy = Min( Length, BufferBase+BufferCount-Pos );
				if( Copy<=0 )
				{
					ArIsError = 1;
					Error->Logf( TEXT("ReadFile beyond EOF %i+%i/%i"), Pos, Length, Size );
					// Universal disc error message
					ShowDirtyDiscError();
				}
				if( ArIsError )
					return;
			}
			appMemcpy( V, Buffer+Pos-BufferBase, Copy );
			Pos       += Copy;
			Length    -= Copy;
			V          = (BYTE*)V + Copy;
		}
		unguardSlow;
	}
protected:
	HANDLE			Handle;
	FOutputDevice*	Error;
	INT				Size;
	INT				Pos;
	INT				BufferBase;
	INT				BufferCount;
	INT				BufferSize;
	BYTE*			Buffer;
	UBOOL			IsLinear;
	BYTE			RegularBuffer[2048];
};


class FArchiveFileWriter : public FArchive
{
public:
	FArchiveFileWriter( HANDLE InHandle, FOutputDevice* InError, INT InPos, UBOOL InIsLinear )
	:	Handle		( InHandle )
	,	Error		( InError )
	,	Pos			( InPos )
	,	BufferCount	( 0 )
	,	IsLinear	( InIsLinear )
	{
		ArIsSaving = ArIsPersistent = 1;
	}
	~FArchiveFileWriter()
	{
		guard(FArchiveFileWriter::~FArchiveFileWriter);
		if( Handle )
			Close();
		Handle = NULL;
		unguard;
	}
	void Seek( INT InPos )
	{
		Flush();
		if( SetFilePointer( Handle, InPos, 0, FILE_BEGIN )==0xFFFFFFFF )
		{
			ArIsError = 1;
			Error->Logf( LocalizeError("SeekFailed",TEXT("Core")) );
		}
		Pos = InPos;
	}
	INT Tell()
	{
		return Pos;
	}
	UBOOL Close()
	{
		guardSlow(FArchiveFileWriter::Close);
		Flush();
		if( Handle && !CloseHandle(Handle) )
		{
			ArIsError = 1;
			Error->Logf( LocalizeError("WriteFailed",TEXT("Core")) );
		}
		return !ArIsError;
		unguardSlow;
	}
	void Serialize( void* V, INT Length )
	{
		Pos += Length;
		INT Copy;
		while( Length > (Copy=ARRAY_COUNT(Buffer)-BufferCount) )
		{
			appMemcpy( Buffer+BufferCount, V, Copy );
			BufferCount += Copy;
			Length      -= Copy;
			V            = (BYTE*)V + Copy;
			Flush();
		}
		if( Length )
		{
			appMemcpy( Buffer+BufferCount, V, Length );
			BufferCount += Length;
		}
	}
	void Flush()
	{
		if( BufferCount )
		{
			DWORD Result=0;
			
			if( !WriteFile( Handle, Buffer, BufferCount, &Result, NULL ) )
			{
				ArIsError = 1;
				Error->Logf( LocalizeError("WriteFailed",TEXT("Core")) );
			}
		}
		BufferCount = 0;
	}
protected:
	UBOOL			IsLinear;
	HANDLE			Handle;
	FOutputDevice*	Error;
	INT				Pos;
	INT				BufferCount;
	BYTE			Buffer[16384];
};

//jg -- DVD emulation shows this to be ~49% faster than 2K blocking reads
#define DEFAULT_BUFFER_SIZE (64 * 1024)
#define DVD_BUFFER_COUNT 2
/**
 * This file reader uses overlapped i/o and double buffering to optimally read
 * from an Xbox DVD.
 */
class FDvdFileReader : public FArchive
{
protected:
	/**
	 * The file handle to operate on
	 */
	HANDLE Handle;
	/**
	 * The size of the file that is being read
	 */
	INT Size;
	/**
	 * Overall position in the file
	 */
	INT FilePos;
	/**
	 * These are the two buffers used for reading the file asynchronously
	 */
	BYTE* Buffers[DVD_BUFFER_COUNT];
	/**
	 * The size of the buffers in bytes
	 */
	INT BufferSize;
	/**
	 * The current index of the buffer that we are serializing from
	 */
	INT SerializeBuffer;
	/**
	 * The current index of the streaming buffer for asynch reading into
	 */
	INT StreamBuffer;
	/**
	 * Where we are in the serialize buffer
	 */
	INT SerializePos;
	/**
	 * The overlapped IO struct to use for determining asynch state
	 */
	OVERLAPPED OverlappedIO;

public:
	/**
	 * Constructor. Zeros values and copies parameters
	 */
	FDvdFileReader(HANDLE InHandle,INT InSize,INT InBufferSize = DEFAULT_BUFFER_SIZE) :
		Handle(InHandle), Size(InSize), FilePos(0), SerializeBuffer(0),
		StreamBuffer(1), SerializePos(0)
	{
		guard(FDvdFileReader::FDvdFileReader);
		check(InSize > 0 && "Cannot use Overlapped I/O on zero sized files");
		// Zero the overlapped structure
		appMemzero(&OverlappedIO,sizeof(OVERLAPPED));
		DWORD NumRead;
		// Set the size of our buffers
		BufferSize = InBufferSize;
		// Allocate our two buffers
		Buffers[0] = (BYTE*)appMalloc(BufferSize);
		Buffers[1] = (BYTE*)appMalloc(BufferSize);
		// Go ahead and kick off the first read
		ReadFile(Handle,Buffers[SerializeBuffer],BufferSize,&NumRead,
			&OverlappedIO);
		// Block on this first one so we can start reading the rest async
		if (GetOverlappedResult(Handle,&OverlappedIO,&NumRead,TRUE) != FALSE)
		{
			// Update the overlapped structure or the API will get confused
			OverlappedIO.Offset += NumRead;
			// If we need the second buffer, start an async read
			if (NumRead == BufferSize)
			{
				DWORD Ignored;
				// Stream the next block into our alternate buffer. Note this
				// won't block and we won't check the results until the other
				// buffer is fully serialized
				ReadFile(Handle,Buffers[StreamBuffer],BufferSize,&Ignored,
					&OverlappedIO);
			}
		}
		// With blocking this shouldn't happen, but just in case
		else if (GetLastError() != ERROR_IO_INCOMPLETE)
		{
			debugf(TEXT("GetOverlappedResult() failed with 0x%X"),GetLastError());
			// Universal disc error message
			ShowDirtyDiscError();
		}
		unguard;
	}

	/**
	 * Destructor. Cleans up any per file resources.
	 */
	virtual ~FDvdFileReader(void)
	{
		guard(FDvdFileReader::~FDvdFileReader);
		Close();
		appFree(Buffers[0]);
		appFree(Buffers[1]);
		unguard;
	}

	/**
	 * We shouldn't be seeking...this should be a linear file only
	 */
	void Seek(INT InPos)
	{
		guard(FDvdFileReader::Seek);
		check(InPos >= 0);
		check(InPos <= Size);
		// We should not be seeking to a place other than where we are
		if (InPos != FilePos)
		{
			debugf(TEXT("Seek() called on a DVD linear file!!"));
		}
		FilePos = InPos;
		unguard;
	}

	/**
	 * Return the file position
	 */
	INT Tell(void)
	{
		return FilePos;
	}

	/**
	 * Return the file size
	 */
	INT TotalSize()
	{
		return Size;
	}

	/**
	 * Closes the file handle
	 */
	UBOOL Close(void)
	{
		guard(FDvdFileReader::Close);
		if (Handle != NULL)
		{
		// Close the file handle
		CloseHandle(Handle);
			Handle = NULL;
		}
		return TRUE;
		unguard;
	}

	/**
	 * Reads the amount of data into the specified location
	 *
	 * @param Dest the destination memory address
	 * @param NumBytes the number of bytes to read
	 */
	void Serialize(void* Dest,INT NumBytes)
	{
		guard(FDvdFileReader::Serialize);
		// While there is data to copy
		while (NumBytes > 0)
		{
			// Figure out how many bytes we can read from buffer
			INT NumToCopy = Min<INT>(NumBytes,BufferSize - SerializePos);
			// See if we are at the end of the serialize buffer or not
			if (NumToCopy > 0)
			{
				// Do the copy
				appMemcpy(Dest,&Buffers[SerializeBuffer][SerializePos],NumToCopy);
				// Update the internal positions
				SerializePos += NumToCopy;
				check(SerializePos <= BufferSize);
				FilePos += NumToCopy;
				check(FilePos <= Size);
				// Decrement the number of bytes we copied
				NumBytes -= NumToCopy;
				// Now offset the dest pointer
				Dest = (BYTE*)Dest + NumToCopy;
			}
			else
			{
				DWORD NumRead;
				// We are so we need to do a blocking file status
				if (GetOverlappedResult(Handle,&OverlappedIO,&NumRead,TRUE) != FALSE)
				{
					// Update the overlapped structure or the API will get confused
					OverlappedIO.Offset += NumRead;
					// Now we need to swap the buffers. This toggles them
					// between buffer indices 0 & 1
					StreamBuffer ^= 1;
					SerializeBuffer ^= 1;
					// We are now at the beginning of the serialize buffer
					SerializePos = 0;
					// Now kick off an async read
					ReadFile(Handle,Buffers[StreamBuffer],BufferSize,&NumRead,
						&OverlappedIO);
				}
				else
				{
					debugf(TEXT("GetOverlappedResult() failed during Serialize() with 0x%X"),
						GetLastError());
					//jg -- Universal disc error message
					ShowDirtyDiscError();
				}
			}
		}
		unguard;
	}
};

class FFileManagerXbox : public FFileManagerGeneric
{
	TCHAR CurrentDirectory[256];

public:
	FFileManagerXbox()
	{
		appStrcpy(CurrentDirectory,TEXT("D:\\"));
		// Clear out any cached INI files
		DeleteFiles(TEXT("Z:\\*.ini"),1);
		// Have it build the demo path
		FString SysDir = ResolvePath(TEXT("*.ini"));
		SysDir[0] = TEXT('Z');
		// Now clean out the system inis
		DeleteFiles(*SysDir,1);
	}

	virtual void Init(UBOOL Startup)
	{
		// Initialize the content manager.
		if (Startup)
		{
			FContentManager* ContentManager = FContentManager::GetInstance();
			if (ContentManager != NULL)
			{
				ContentManager->Initialize();
			}
		}
	}

	/**
	 * Resolves a file name into something that the Xbox can open. Path
	 * relative files are turned into full paths. Paths to the cache
	 * drive are ignored. It also checks the file cache manager to see
	 * if a file has been moved from the DVD to the HD
	 *
	 * @param InFilename The file name to resolve
	 *
	 * @return the file name after being resolved
	 */
	virtual FString ResolvePath(const TCHAR* InFilename)
	{
		guard(ResolvePath);
		// Leave Z:, U: and T: paths alone as they are already resolved
		if (InFilename[1] == TEXT(':') &&
			(appToLower(InFilename[0]) == TEXT('z') ||
			 appToLower(InFilename[0]) == TEXT('u') ||
			 appToLower(InFilename[0]) == TEXT('t')))
		{
			return FString(InFilename);
		}
		FString	Filename = InFilename;
		// Check the title-specific region first.
		FContentManager* ContentManager = FContentManager::GetInstance();
		if (ContentManager != NULL)
		{
			FString StrippedFilename(Filename);
			// Find the last part of the path
			INT LastSlashIndex = Filename.InStr(TEXT("\\"),TRUE);
			if (LastSlashIndex != -1)
			{
				// Strip everything but the filename part.
				StrippedFilename = Filename.Right(Filename.Len() - 1 - LastSlashIndex);
			}
			TCHAR* ContentFilename = appStaticString1024();
			// Try to find the file in the title-specific region.
			if (ContentManager->FindFile(*StrippedFilename, ContentFilename))
			{
				return FString(ContentFilename);
			}
		}

		// Check the file cache.
		if (GFileCacheManager != NULL)
		{
			const TCHAR* CachedFileName = GFileCacheManager->GetCachedFileName(Filename);
			if (CachedFileName != NULL)
			{
				debugf(NAME_DevJoe,TEXT("Loading file (%s) from cache"),CachedFileName);
				return FString(CachedFileName);
			}
		}
		// If the directory is fully specified and not on the cache drive bail
		if (appStrnicmp(InFilename,CurrentDirectory,appStrlen(CurrentDirectory)) == 0)
		{
			return FString(InFilename);
		}
		// Strip off drive letter or path relative information
		if (Filename[1] == TEXT(':') ||
			(Filename[0] == TEXT('.') && Filename[1] == TEXT('.')))
		{
			Filename = Filename.Mid(3);
		}
		// Assume that it wants the System directory
		else
		{
			Filename = FString(TEXT("System\\")) * Filename;
		}
		// Now handle the DVD/Demo path
		return FString::Printf(TEXT("%s%s"),CurrentDirectory,*Filename);
		unguard;
	}

	/**
	 * Builds the correct cache directory based upon demo paths
	 *
	 * @param DirName The directory to resolve the name for and create
	 */
	void CreateCacheDirectory(const ANSICHAR* DirName)
	{
		guard(CreateCacheDirectory);
		// Prepend the current directory (D:\ or the demo path)
		FString FullDirName(CurrentDirectory);
		FullDirName += DirName;
		// Make it go to the cache drive
		FullDirName[0] = TEXT('Z');
		// Attempt to open the directory as a file. This way we don't truncate any
		// existing data in there
		HANDLE DirHandle = CreateFile(TCHAR_TO_ANSI(*FullDirName),GENERIC_READ,
			FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,NULL);
		if (DirHandle == INVALID_HANDLE_VALUE)
		{
			debugf(NAME_DevJoe,TEXT("Creating cache dir %s"),*FullDirName);
			// Now create the directory
			CreateDirectory(TCHAR_TO_ANSI(*FullDirName),NULL);
		}
		else
		{
			CloseHandle(DirHandle);
		}
		unguard;
	}

	FArchive* CreateFileReader( const TCHAR* Filename, DWORD Flags, FOutputDevice* Error )
	{
		guard(FFileManagerXbox::CreateFileReader);
		DWORD  Access    = GENERIC_READ;
		DWORD  WinFlags  = FILE_SHARE_READ;
		DWORD  Create    = OPEN_EXISTING;

		HANDLE Handle = CreateFile( TCHAR_TO_ANSI(*ResolvePath(Filename)), Access, WinFlags, NULL, Create, FILE_ATTRIBUTE_NORMAL, NULL );
	
		if( Handle==INVALID_HANDLE_VALUE )
		{
			debugf( TEXT("Failed to read file: %s"), *ResolvePath(Filename) );
			// Universal disc error message
			ShowDirtyDiscError();
			if( Flags & FILEREAD_NoFail )
				appErrorf( TEXT("Failed to read file: %s"), *ResolvePath(Filename) );
			return NULL;
		}
		return NEW FArchiveFileReader(Handle,Error,GetFileSize(Handle,NULL),0);
		unguard;
	}

	/**
	 * Creates a file reader that is optimized for streaming linear files
	 * from the DVD. It uses the non-buffered I/O and asynch reading in order
	 * to maximize throughput from DVD to the buffers. Serialization takes
	 * place via double buffers to prevent many waits for disk I/O.
	 *
	 * @param FileName the name of the file to open as a linear file
	 *
	 * @return Returns the file reader object or null if it failed
	 */
	FArchive* CreateLinearReader(const TCHAR* FileName)
	{
		guard(FFileManagerXbox::CreateFileReader);
		FArchive* NewArchive = NULL;
		// Make sure to handle path adjustments for demos/caching
		const FString& ResolvedPath = ResolvePath(FileName);
		// Open the file with overlapped i/o and no buffering
		HANDLE Handle = CreateFile(TCHAR_TO_ANSI(*ResolvedPath),GENERIC_READ,
			FILE_SHARE_READ,NULL,OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING,
			NULL);
		if (Handle != INVALID_HANDLE_VALUE)
		{
			// Create the new reader
			NewArchive = NEW FDvdFileReader(Handle,GetFileSize(Handle,NULL));
		}
		else
		{
			debugf(TEXT("Failed to open file %s with 0x%X"),FileName,GetLastError());
			// Universal disc error message
			ShowDirtyDiscError();
		}
		return NewArchive;
		unguard;
	}

	FArchive* CreateFileWriter( const TCHAR* Filename, DWORD Flags, FOutputDevice* Error )
	{
		guard(FFileManagerXbox::CreateFileWriter);
		if( Flags & FILEWRITE_EvenIfReadOnly )
			SetFileAttributes(TCHAR_TO_ANSI(*ResolvePath(Filename)), 0);
		DWORD  Access    = GENERIC_WRITE;
		DWORD  WinFlags  = (Flags & FILEWRITE_AllowRead) ? FILE_SHARE_READ : 0;
		DWORD  Create    = (Flags & FILEWRITE_Append) ? OPEN_ALWAYS : (Flags & FILEWRITE_NoReplaceExisting) ? CREATE_NEW : CREATE_ALWAYS;

		HANDLE Handle    = CreateFile( TCHAR_TO_ANSI(*ResolvePath(Filename)), Access, WinFlags, NULL, Create, FILE_ATTRIBUTE_NORMAL, NULL );
		
		INT    Pos       = 0;
		if( Handle==INVALID_HANDLE_VALUE )
		{
			if( Flags & FILEWRITE_NoFail )
				appErrorf( TEXT("Failed to create file: %s"), *ResolvePath(Filename) );
			return NULL;
		}

		if( Flags & FILEWRITE_Append )
			Pos = SetFilePointer( Handle, 0, 0, FILE_END );

		return NEW FArchiveFileWriter(Handle,Error,Pos,0);
		unguard;
	}

	INT FileSize( const TCHAR* Filename )
	{
		guard(FFileManagerXbox::FileSize);	
		HANDLE Handle = CreateFile( TCHAR_TO_ANSI(*ResolvePath(Filename)), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
		if( Handle==INVALID_HANDLE_VALUE )
			return -1;
		DWORD Result = GetFileSize( Handle, NULL );
		CloseHandle( Handle );
		return Result;
		unguard;
	}
	DWORD Copy( const TCHAR* DestFile, const TCHAR* SrcFile, UBOOL ReplaceExisting, UBOOL EvenIfReadOnly, UBOOL Attributes, DWORD Compress, FCopyProgress* Progress )
	{
		guard(FFileManagerXbox::Copy);
		debugf(TEXT("Copying [%s] to [%s]"), SrcFile, DestFile);	
		if( EvenIfReadOnly )
			SetFileAttributes(TCHAR_TO_ANSI(*ResolvePath(DestFile)), 0);
		DWORD Result = COPY_OK;
		if( Progress || Compress != FILECOPY_Normal )
		{
			Result = FFileManagerGeneric::Copy( *ResolvePath(DestFile), *ResolvePath(SrcFile), ReplaceExisting, EvenIfReadOnly, Attributes, Compress, Progress );
		}
		else
		{
			if( CopyFile( TCHAR_TO_ANSI(*ResolvePath(SrcFile)), TCHAR_TO_ANSI(*ResolvePath(DestFile)), !ReplaceExisting ) )
				Result = COPY_OK;
			else
				Result = COPY_MiscFail;
		}
		if( Result==COPY_OK && !Attributes )
			SetFileAttributes(TCHAR_TO_ANSI(*ResolvePath(DestFile)), 0);
		return Result;
		unguard;
	}
	UBOOL Delete( const TCHAR* Filename, UBOOL RequireExists=0, UBOOL EvenReadOnly=0 )
	{
		guard(FFileManagerXbox::Delete);
		if( EvenReadOnly )
			SetFileAttributesA(TCHAR_TO_ANSI(*ResolvePath(Filename)),FILE_ATTRIBUTE_NORMAL);
		return DeleteFile(TCHAR_TO_ANSI(*ResolvePath(Filename)))!=0 || (!RequireExists && GetLastError()==ERROR_FILE_NOT_FOUND);
		unguard;
	}

	/**
	 * Deletes a set of files from the specified path
	 *
	 * @param FilePattern The wildcard filename to delete
	 * @param EvenReadOnly Whether to delete read only files
	 *
	 * @return Whether the call was successful or not
	 */
	void DeleteFiles(const TCHAR* FilePattern,UBOOL EvenReadOnly = 0)
	{
		guard(DeleteFiles);
		FString Dir(FilePattern);
		// Strip off the trailing file pattern, so it is a pure directory
		INT Index = Dir.InStr(TEXT("\\"),1);
		if (Index != -1)
		{
			Dir = Dir.Left(Index + 1);
		}
		const TArray<FString>& DelFiles = FindFiles(FilePattern,1,0);
		// Now loop through each returned file and delete it
		for (INT Index = 0; Index < DelFiles.Num(); Index++)
		{
			// Delete the file using the full path being prepended
			Delete(*(Dir * DelFiles(Index)),0,EvenReadOnly);
		}
		unguard;
	}

    UBOOL IsReadOnly( const TCHAR* Filename )
    {
		guard(FFileManagerXbox::IsReadOnly);

        DWORD rc;

        if( FileSize( Filename ) < 0 )
            return( 0 );

		rc = TCHAR_CALL_OS(GetFileAttributes(appToAnsi(Filename)),GetFileAttributesA((LPCSTR)TCHAR_TO_ANSI(Filename)));

        if (rc != 0xFFFFFFFF)
            return ((rc & FILE_ATTRIBUTE_READONLY) != 0);
        else
        {
			debugf( NAME_Warning, TEXT("Error reading attributes for '%s'"), Filename );
            return (0);
        }

		unguard;
    }
	UBOOL Move( const TCHAR* Dest, const TCHAR* Src, UBOOL Replace=1, UBOOL EvenIfReadOnly=0, UBOOL Attributes=0 )
	{
		guard(FFileManagerXbox::Move);
		//warning: MoveFileEx is broken on Windows 95 (Microsoft bug).
		Delete( Dest, 0, EvenIfReadOnly );
		INT Result = MoveFile(TCHAR_TO_ANSI(*ResolvePath(Src)),TCHAR_TO_ANSI(*ResolvePath(Dest)));
		if( !Result )
			debugf( NAME_Warning, TEXT("Error moving file '%s' to '%s'"), *ResolvePath(Src), *ResolvePath(Dest) );
		return Result!=0;
		unguard;
	}
	SQWORD GetGlobalTime( const TCHAR* Filename )
	{
		return 0;
	}
	UBOOL SetGlobalTime( const TCHAR* Filename )
	{
		return 0;//!!
	}
	UBOOL MakeDirectory( const TCHAR* Path, UBOOL Tree=0 )
	{
		guard(FFileManagerXbox::MakeDirectory);
		if( Tree )
			return FFileManagerGeneric::MakeDirectory( Path, Tree );
		UBOOL rv = CreateDirectory(TCHAR_TO_ANSI(*ResolvePath(Path)),NULL);
		DWORD lastError = GetLastError();
		return  rv || lastError==ERROR_ALREADY_EXISTS;
		unguard;
	}
	UBOOL DeleteDirectory( const TCHAR* Path, UBOOL RequireExists=0, UBOOL Tree=0 )
	{
		guard(FFileManagerXbox::DeleteDirectory);
		if( Tree )
			return FFileManagerGeneric::DeleteDirectory( Path, RequireExists, Tree );
		return RemoveDirectory(TCHAR_TO_ANSI(*ResolvePath(Path)))!=0 || (!RequireExists && GetLastError()==ERROR_FILE_NOT_FOUND);
		unguard;
	}

	TArray<FString> FindFiles( const TCHAR* Filename, UBOOL Files, UBOOL Directories )
	{
		guard(FFileManagerXbox::FindFiles);
		TArray<FString> Result;
		HANDLE Handle=NULL;
		WIN32_FIND_DATAA Data;

		// Search in scratch area
		Handle=FindFirstFileA(TCHAR_TO_ANSI(*ResolvePath(Filename)),&Data);
		if( Handle!=INVALID_HANDLE_VALUE )
		{
			do
			{
				if (appStricmp(Data.cFileName,TEXT("."))
				&&	appStricmp(Data.cFileName,TEXT(".."))
				&&	((Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? Directories : Files) )
				{
					new(Result)FString(Data.cFileName);
				}
			}
			while( FindNextFileA(Handle,&Data) );

			if( Handle )
				FindClose( Handle );
		}
		return Result;
		unguard;
	}
	UBOOL SetDefaultDirectory( const TCHAR* Filename )
	{
		guard(FFileManagerXbox::SetDefaultDirectory);
		appStrcpy(CurrentDirectory,Filename);
		return 1;
		unguard;
	}
	FString GetDefaultDirectory()
	{
		guard(FFileManagerXbox::GetDefaultDirectory);
		return FString(CurrentDirectory);
		unguard;
	}

	//scion bcl -- following routines modified out of Championship codebase

	UBOOL Exists( const TCHAR* fileName )
	{
		guard(FFileManagerXbox::Exists);
		UBOOL exists = false;       
		HANDLE Handle = CreateFile( appToAnsi(*ResolvePath(fileName)), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
		if( Handle!=INVALID_HANDLE_VALUE )
		{
			exists = true;
			CloseHandle( Handle );
		}
		return exists;
		unguard;
	}

	/**
	 * Creates a signature and appends it to the file
	 *
	 * @param FileName the file to digitally sign
	 * @param VersionNumber the version of the file to add
	 * @param SignType the type of signature to create
	 */
	UBOOL SignFile(const TCHAR* FileName,DWORD VersionNumber,
		ESignatureType SignType = SIG_FLAG_SAVE_GAME)
	{
		guard(SignFile);
		check(SignType <= SIG_FLAG_NON_ROAMABLE);
		UBOOL bSignedOK = FALSE;
		// Create the file for reading and writing. We'll read to the end
		// and then write the signature out
		HANDLE hFile = CreateFile(FileName,GENERIC_READ | GENERIC_WRITE,
			0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
		if (hFile != INVALID_HANDLE_VALUE)
		{
			// Figure out how much data to sign
			DWORD FileSize = GetFileSize(hFile,NULL);
			// Handle the empty file case
			if (FileSize > 0)
			{
				// Create the signature block with the specified type
				HANDLE hSig = XCalculateSignatureBegin((DWORD)SignType);
				if (hSig != INVALID_HANDLE_VALUE)
				{
					BYTE Buffer[1024];
					UBOOL bIsOk = TRUE;
					DWORD SizeRemaining = FileSize;
					// While there is still data to read
					while (SizeRemaining > 0 && bIsOk == TRUE)
					{
						DWORD NumRead = 0;
						// Read one buffer's worth
						bIsOk = ReadFile(hFile,Buffer,1024,&NumRead,NULL);
						if (bIsOk && NumRead > 0)
						{
							// Decrement how much is left to read/sign
							SizeRemaining -= NumRead;
							// Add to our signature
							bIsOk = XCalculateSignatureUpdate(hSig,Buffer,NumRead) ==
								ERROR_SUCCESS;
						}
						else if (bIsOk == FALSE)
						{
							debugf(TEXT("Failed during read of data 0x%X"),
								GetLastError());
						}
					}
					XCALCSIG_SIGNATURE SigData;
					// Zero our data
					appMemzero(&SigData,sizeof(XCALCSIG_SIGNATURE));
					// Now close the handle and copy out the signature info
					XCalculateSignatureEnd(hSig,&SigData);
					// Don't sign the file if the data is broken
					if (bIsOk && SizeRemaining == 0)
					{
						DWORD NumWritten = 0;
						// Write the version number out
						if (WriteFile(hFile,&VersionNumber,sizeof(DWORD),&NumWritten,
							NULL) && NumWritten == sizeof(DWORD))
						{
							// Now write the signature
							if (WriteFile(hFile,&SigData,sizeof(XCALCSIG_SIGNATURE),
								&NumWritten,NULL) &&
								NumWritten == sizeof(XCALCSIG_SIGNATURE))
							{
								// Everything worked fine
								bSignedOK = TRUE;
							}
							else
							{
								debugf(TEXT("Failed during write of signature 0x%X"),
									GetLastError());
							}
						}
						else
						{
							debugf(TEXT("Failed during write of version 0x%X"),
								GetLastError());
						}
					}
				}
				else
				{
					debugf(TEXT("Failed when creating signature handle 0x%X"),
						GetLastError());
				}
			}
			CloseHandle(hFile);
		}
		return bSignedOK;
		unguard;
	}

	/**
	 * Verifies the signature for a file
	 *
	 * @param FileName the file to check the signature on
	 * @param VersionNumber the expected version of the file
	 * @param SignType the signature type to check
	 */
	UBOOL IsSignedFileValid(const TCHAR* FileName,DWORD VersionNumber,
		ESignatureType SignType = SIG_FLAG_SAVE_GAME)
	{
		guard(IsSignedFileValid);
		check(SignType <= SIG_FLAG_NON_ROAMABLE);
		UBOOL bSignedOK = FALSE;
		// Create the file for reading only
		HANDLE hFile = CreateFile(FileName,GENERIC_READ,0,NULL,OPEN_EXISTING,
			FILE_ATTRIBUTE_NORMAL,NULL);
		if (hFile != INVALID_HANDLE_VALUE)
		{
			// Figure out how much data to read for comparing the signatures
			DWORD FileSize = GetFileSize(hFile,NULL);
			// Handle mangled files that have been truncated
			if (FileSize > SIG_OVERHEAD)
			{
				// Create the signature block with the specified type
				HANDLE hSig = XCalculateSignatureBegin((DWORD)SignType);
				if (hSig != INVALID_HANDLE_VALUE)
				{
					BYTE Buffer[1024];
					UBOOL bIsOk = TRUE;
					DWORD SizeRemaining = FileSize - SIG_OVERHEAD;
					// While there is still data to read
					while (SizeRemaining > 0 && bIsOk == TRUE)
					{
						DWORD NumRead = 0;
						// Read chunks or the remaining size
						DWORD NumToRead = Min<DWORD>(1024,SizeRemaining);
						// Read one buffer's worth
						bIsOk = ReadFile(hFile,Buffer,NumToRead,&NumRead,NULL);
						if (bIsOk && NumRead == NumToRead)
						{
							// Decrement how much is left to read/sign
							SizeRemaining -= NumRead;
							// Add to our signature
							bIsOk = XCalculateSignatureUpdate(hSig,Buffer,NumRead) ==
								ERROR_SUCCESS;
						}
						else if (bIsOk == FALSE)
						{
							debugf(TEXT("Failed during read of data 0x%X"),
								GetLastError());
						}
					}
					XCALCSIG_SIGNATURE SigData;
					// Zero our data
					appMemzero(&SigData,sizeof(XCALCSIG_SIGNATURE));
					// Now close the handle and copy out the signature info
					XCalculateSignatureEnd(hSig,&SigData);
					DWORD NumRead = 0;
					DWORD FileVer = 0;
					// Now read the version number
					if (ReadFile(hFile,&FileVer,sizeof(DWORD),&NumRead,NULL))
					{
						// Don't bother reading the signature if these don't match
						if (NumRead == sizeof(DWORD) && FileVer == VersionNumber)
						{
							XCALCSIG_SIGNATURE FileSig;
							// Zero our data
							appMemzero(&FileSig,sizeof(XCALCSIG_SIGNATURE));
							// Now read the signature from the file
							if (ReadFile(hFile,&FileSig,sizeof(XCALCSIG_SIGNATURE),
								&NumRead,NULL))
							{
								// Check and see if they match
								bSignedOK = NumRead == sizeof(XCALCSIG_SIGNATURE) &&
									appMemcmp(&FileSig,&SigData,sizeof(XCALCSIG_SIGNATURE)) == 0;
							}
							else
							{
								debugf(TEXT("Failed during read of signature 0x%X"),
									GetLastError());
							}
						}
						else
						{
							debugf(TEXT("Failed during read of version 0x%X"),
								GetLastError());
						}
					}
					else
					{
						debugf(TEXT("Failed during read of version 0x%X"),
							GetLastError());
					}
				}
				else
				{
					debugf(TEXT("Failed when creating signature handle 0x%X"),
						GetLastError());
				}
			}
			CloseHandle(hFile);
		}
		return bSignedOK;
		unguard;
	}

	TArray<FString> ContentLocalizationFiles;

	TArray<FString> GetContentLocalizationFiles()
	{
		return ContentLocalizationFiles;
	}

	void SetContentLocalizationFiles(const TArray<FString>& InContentLocalizationFiles)
	{
		ContentLocalizationFiles = InContentLocalizationFiles;
	}
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

