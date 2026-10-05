#ifndef ULOADSAVEMANAGER_H
#define ULOADSAVEMANAGER_H
/* 
  //scion bcl 
  Saved game manager. This class is primarily an interface that provides simple but usable implementations of the various functions.
  It is much like a filemanager but has some knowledge of sizes of its managed object's sizes. Concept and some utility code lifted from 
  Championship

  rjp --
  It seems like this class was intended to serve as a general load/save manager, an application layer to interface
  with the XBox API.  Unfortunately, it is currently bound to PlayerProfile, but it would be very simple to change this.
  LoadConfig()/SaveConfig() could then be changed to go through this class, so that SaveConfig() doesn't have to be disabled
  for xbox projects, and you do not need to create specialized handlers for any classes that need to save to disk.
  
*/

#ifndef ENGINE_API
#define ENGINE_API DLL_IMPORT
#endif

//const DWORD MIN_SAVE_DATA_CHUNKS = 1;  // 128K
//const DWORD MAX_SAVE_DATA_CHUNKS = 16; // 2 MB

// Savegame icon image data
const DWORD IMAGE_WH = 64;										// width and height
const DWORD IMAGE_META_HDR_SIZE  = 2048;						// size of image header (including overhead)
const DWORD IMAGE_META_DATA_SIZE = (IMAGE_WH * IMAGE_WH) / 2;	// DXT1 is 4 bits per pixel (raw image size)

const DWORD BLOCK_SIZE = 16 * 1024;			// TCR number of bytes represented by a single "block" of memory (dashboard, menus, etc.)
const DWORD MAX_PROFILE_SIZE = BLOCK_SIZE * 10;	// we can increase this if necessary

#define SAVE_VERSION_NUMBER 1051
#define SAVE_FILE_NAME TEXT("AVARIS_PROFILE.PRO")
#define DISCONNECT_FILENAME TEXT("T:\\qfh838393")
#define DISCONNECT_VERSION 3


// Used for copying the save game images
// Example:
//				GFileManager->Copy(*FString::Printf(SAVE_GAME_DEST_PF,SaveGameDir),
//					*FString::Printf(SAVE_GAME_SOURCE_PF,Tex->GetName()),...);
//
#define SAVE_GAME_SOURCE_PF TEXT("D:\\Textures\\%s.xpr")
#define SAVE_GAME_DEST_PF TEXT("%sSaveImage.xbx")

 
//
//	Archive for serializing save-game data.  Allows for easy serialization of UObject property data, for a user-configured subset
//	of UProperties.  
//	
//	Properties marked as automated are automatically ignored by this archive.  This is a hack to support specifying
//	properties that should not be serialized, but that need to have default values (thus, the transient modifier cannot be used);
//	to avoid the need to add another property flag, I piggy-back on CPF_Automated.  Obviously, better would be to allow transients 
//	to have defaultproperties.
//
//	Delegates are always ignored by this archive, as the only time we'd want delegates to be serialized is when serializing subobjects,
//	but this archive is not designed to be used with subobjects.
//
//	This class is designed to serve as a wrapper around another, more functional, archive type.  For the most part, all functions calls
//	are simply passed through to the internal archive.  The exceptions are functions that determine which properties should be serialized.
//	While FArchiveSaveGame is currently only used to wrap FArchiveReader & FArchiveWriter, it should function correctly regardless of the
//	internal archive type, though this has not been verified.

class ENGINE_API FArchiveSaveGame : public FArchive
{
public:
	// FArchive interface.
	virtual void Serialize( void* V, INT Length )
	{
		if ( InternalAr != NULL )
			InternalAr->Serialize(V, Length);
	}
	virtual void SerializeBits( void* V, INT LengthBits )
	{
		if ( InternalAr != NULL )
			InternalAr->SerializeBits(V, LengthBits);
	}
	virtual void SerializeInt( DWORD& Value, DWORD Max )
	{
		if ( InternalAr != NULL )
			InternalAr->ByteOrderSerialize( &Value, sizeof(Value) );
	}
	virtual void Preload( UObject* Object )
	{
		if ( InternalAr != NULL )
			InternalAr->Preload(Object);
	}
	virtual void CountBytes( SIZE_T InNum, SIZE_T InMax )
	{
		if ( InternalAr != NULL )
			InternalAr->CountBytes(InNum, InMax);
	}
	// scion sz - added iot count contiguous memory separately
	virtual void CountBytes( SIZE_T InNum, SIZE_T InMax, SIZE_T InContiguous )
	{
		if ( InternalAr != NULL )
			InternalAr->CountBytes(InNum, InMax, InContiguous);
	}
	virtual FArchive& operator<<( class FName& N )
	{
		guard(FArchiveSaveGame::FName<<);

		UBOOL bSerializeName = 0;
		if ( InternalAr != NULL )
		{
			// only serialize the object if the internal archive didn't serialize the object
			INT PreviousPos = InternalAr->Tell();
			*InternalAr << N;
			bSerializeName = InternalAr->Tell() == PreviousPos;
		}

		if ( bSerializeName )
		{
			if (IsSaving())
			{
				INT EntryIndex = NameTable.AddUniqueItem(N);
				*InternalAr << EntryIndex;
			}

			if (IsLoading())
			{
				INT EntryIndex;
				*InternalAr << EntryIndex;
				N = NameTable(EntryIndex);
			}
		}

		return *this;

		unguard;
	}
	virtual FArchive& operator<<( class UObject*& Res )
	{
		UBOOL bSerializeObject = 0;
		if ( InternalAr != NULL )
		{
			// only serialize the object if the internal archive didn't serialize the object
			INT PreviousPos = InternalAr->Tell();
			*InternalAr << Res;
			bSerializeObject = InternalAr->Tell() == PreviousPos;
		}

		if ( bSerializeObject )
		{
			// M13: a saved object reference is always written as None in 4 bytes, the size it had in every
			// shipped build: sizeof(UObject*) would make a 64-bit profile unreadable by a 32-bit game (and every
			// property tag after it misplaced). Loading reads the 4 bytes and gives None, as before.
			if ( IsSaving() )
			{
				// null out object references before saving.
				INT Obj = 0;
				Serialize( &Obj, sizeof(INT) );
			}
			else
			{
				INT Obj = 0;
				Serialize( &Obj, sizeof(INT) );
				Res = NULL;
			}
		}

		return *this;
	}
	virtual INT MapName( FName* Name )
	{
		if ( InternalAr != NULL )
			return InternalAr->MapName(Name);

		return FArchive::MapName(Name);
	}
	virtual INT MapObject( UObject* Object )
	{
		if ( InternalAr != NULL )
			return InternalAr->MapObject(Object);

		return FArchive::MapObject(Object);;
	}
	virtual INT Tell()
	{
		if ( InternalAr != NULL )
			return InternalAr->Tell();

		return FArchive::Tell();
	}
	virtual INT TotalSize()
	{
		if ( InternalAr != NULL )
			return InternalAr->TotalSize();

		return FArchive::TotalSize();
	}
	virtual UBOOL AtEnd()
	{
		if ( InternalAr != NULL )
			return InternalAr->AtEnd();

		return FArchive::AtEnd();
	}
	virtual UBOOL AtStopper()
	{
		if ( InternalAr != NULL )
			return InternalAr->AtStopper();

		return FArchive::AtStopper();
	}
	virtual void SetStopper( INT InStopper=INDEX_NONE )
	{
		FArchive::SetStopper(InStopper);
		if ( InternalAr != NULL )
			InternalAr->SetStopper(InStopper);
	}
	virtual void Seek( INT InPos )
	{
		if ( InternalAr != NULL )
			InternalAr->Seek(InPos);
	}
	virtual void AttachLazyLoader( FLazyLoader* LazyLoader )
	{
		if ( InternalAr != NULL )
			InternalAr->AttachLazyLoader(LazyLoader);
	}
	virtual void DetachLazyLoader( FLazyLoader* LazyLoader )
	{
		if ( InternalAr != NULL )
			InternalAr->DetachLazyLoader(LazyLoader);
	}
	virtual void Precache( INT HintCount )
	{
		if ( InternalAr != NULL )
			InternalAr->Precache(HintCount);
	}
	virtual void Flush()
	{
		if ( InternalAr != NULL )
			InternalAr->Flush();
	}
	virtual UBOOL Close()
	{

		if ( InternalAr != NULL )
		{
			if ( IsSaving() )
			{
				// make sure the internal archive is completely flushed.
				InternalAr->Flush();

				// fixup the dummy offset with the current position of the archive
				INT CurrentPos = Tell();
				Seek(0);

				*InternalAr << CurrentPos;
				Seek(CurrentPos);

				// now serialize the name table
				INT NameCount = NameTable.Num();
				*InternalAr << NameCount;

				for ( INT i = 0; i < NameCount; i++ )
				{
					*InternalAr << *FName::GetEntry(NameTable(i).GetIndex());
				}
			}

			return InternalAr->Close();
		}

		return FArchive::Close();
	}
	virtual UBOOL GetError()
	{
		if ( InternalAr != NULL )
			return InternalAr->GetError();

		return FArchive::GetError();
	}

	virtual UBOOL ShouldSerializeProperty(const UProperty* Prop )
	{
		guard(FArchiveSaveGame::ShouldSerializeProperty);

		if ( Prop->IsA(UDelegateProperty::StaticClass()) )
			return false;

		if ( Prop->PropertyFlags & CPF_Automated )
			return false;

		return FArchive::ShouldSerializeProperty(Prop);
		unguard;
	}

	// FArchiveSaveGame Interface
	FArchiveSaveGame( FArchive* Ar = NULL )
		: InternalAr(Ar)
		, FArchive()
	{
		guard(FArchiveSaveGame::FArchiveSaveGame::Ar);

		ArIsRawUData		= 1;
		if ( InternalAr != NULL )
		{
			ArVer				= InternalAr->Ver();
			ArNetVer			= InternalAr->NetVer();
			ArIsLoading			= InternalAr->IsLoading();
			ArIsSaving			= InternalAr->IsSaving();
			ArIsTrans			= InternalAr->IsTrans();
			ArIsPersistent		= InternalAr->IsPersistent();
			ArIsError			= InternalAr->IsError();
			ArIsCriticalError	= InternalAr->IsCriticalError();
			ArForEdit			= InternalAr->ForEdit();
			ArForClient			= InternalAr->ForClient();
			ArForServer			= InternalAr->ForServer();
			ArContainsCode		= InternalAr->ContainsCode();
			InternalAr->SetRawUData(ArIsRawUData);

			InitializeNameTable();
		}

		unguard;
	}

	void SetInternalArchive( FArchive* Ar )
	{
		guard(FArchiveSaveGame::SetInternalArchive);
		if ( InternalAr != NULL )
			delete InternalAr;

		InternalAr = Ar;
		if ( InternalAr != NULL )
		{
			ArVer				= InternalAr->Ver();
			ArNetVer			= InternalAr->NetVer();
			ArIsLoading			= InternalAr->IsLoading();
			ArIsSaving			= InternalAr->IsSaving();
			ArIsTrans			= InternalAr->IsTrans();
			ArIsPersistent		= InternalAr->IsPersistent();
			ArIsError			= InternalAr->IsError();
			ArIsCriticalError	= InternalAr->IsCriticalError();
			ArForEdit			= InternalAr->ForEdit();
			ArForClient			= InternalAr->ForClient();
			ArForServer			= InternalAr->ForServer();
			ArContainsCode		= InternalAr->ContainsCode();
			InternalAr->SetRawUData(ArIsRawUData);

			InitializeNameTable();
		}

		unguard;
	}

	void InitializeNameTable()
	{
		guard(FArchiveSaveGame::InitializeNameTable);

		if ( InternalAr )
		{
			if ( IsLoading() )
			{
				Seek(0);

				// load the offset of the beginning of the name table
				INT Offset=0;
				*InternalAr << Offset;

				// store the current position (should be 4)
				INT InitialPos = Tell();

				// then go to the name table
				Seek(Offset);

				INT Count=0;
				*InternalAr << Count;

				for ( INT i = 0; i < Count; i++ )
				{
					FNameEntry Entry;
					*InternalAr << Entry;
					NameTable.AddItem(FName(Entry.GetName()));
				}

				Seek(InitialPos);
			}
			else if ( IsSaving() )
			{
				Seek(0);

				// Emit a dummy offset that we'll fixup later.
				INT DummyOffset=0;
				*InternalAr << DummyOffset;
			}
		}

		unguard;
	}

	~FArchiveSaveGame()
	{
		guard(~FArchiveSaveGame);

		if ( InternalAr != NULL )
		{
			delete InternalAr;
		}

		InternalAr = NULL;
		unguard;
	}

protected:
	TArray<FName>	NameTable;
	class FArchive* InternalAr;
};

class ENGINE_API ULoadSaveManager : public USubsystem
{
	DECLARE_CLASS(ULoadSaveManager,USubsystem,CLASS_Transient,Engine)

	enum LSM_Errors {
		LSM_OK,
		LSM_Error_UnknownDevice,
		LSM_Error_DeviceFull,
	};
	ULoadSaveManager();

	// USubsystem Interface
	virtual void        Init(UClient* inClient);
	virtual void        Tick(FLOAT DeltaTime) {}
	virtual UBOOL       Exec(const TCHAR* Cmd, FOutputDevice& Ar = *GLog);
	virtual void        Serialize(FArchive& Ar);
	virtual void        Destroy();

	// ULoadSaveManager Interface
	UClass* getGameProfileClass() { return GameProfileClass; }

	// do we have enough room to create a new savegame?  BytesNeeded is how many bytes we're short, if the result is false
	virtual UBOOL		IsEnoughSpaceAvailable(DWORD* BlocksNeeded) const;
	virtual DWORD		GetMaxSaveGameSize( UBOOL bBlocks = 0 ) const { return 0; }


	// Load an object from the file; it is assumed that the loader knows what type of object is going to be loaded

	virtual UBOOL		LoadGameObject(const FString & profileName, UObject* Profile);
	virtual UBOOL		ExistsGameObject(const FString & profileName) const;
	virtual UBOOL		DeleteGameObject(const FString & profileName);
	virtual UBOOL		SaveGameObject(const FString & profileName, UObject * saveGameObject);
	virtual UBOOL		ListGameObjects( TArray<FString> *ProfileName);

	virtual void		SetDisconnectFlag( UBOOL bNewValue, UBOOL bTeamGame ) { }
	virtual UBOOL		GetDisconnectFlag( BYTE& bTeamGame ) { return 0; }
	LSM_Errors			LastError() { LSM_Errors e = lastError; lastError = LSM_OK; return e; }
protected:


	virtual UBOOL		ExistsDirectory(const FString & pathName) const;
	virtual UBOOL		EnsureDirectory(const FString & pathName);
	virtual FString		GetProfileRoot() const;

	// translates a device and a profile into a directory path for the object in question. Note that this is the name that FFileManagers can use to access the file
	virtual FString		GetProfilePath(const FString & profileName) const;
	
	LSM_Errors lastError;
	UClient * Client;
	UClass*             GameProfileClass;
}; // ULoadSaveManager
#endif // ULOADSAVEMANAGER_H