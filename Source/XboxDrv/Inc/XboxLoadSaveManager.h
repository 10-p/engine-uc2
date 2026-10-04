#ifndef XBOXLOADSAVEMANAGER_H
#define XBOXLOADSAVEMANAGER_H

#define SIG_OVERHEAD (XCALCSIG_SIGNATURE_SIZE + sizeof(DWORD))

#include "ULoadSaveManager.h"

/* 
//scion bcl 
*/
// This sample generates fake saved game data that is some random 
// size between MIN and MAX
class DLL_EXPORT UXboxLoadSaveManager : public ULoadSaveManager
{
	DECLARE_CLASS(UXboxLoadSaveManager,ULoadSaveManager,CLASS_Transient,XboxDrv)

	UXboxLoadSaveManager();

	// ULoadSaveManager Interface

	UBOOL		IsEnoughSpaceAvailable( DWORD* out_BytesNeeded ) const;
	UBOOL		ExistsGameObject(const FString & profileName) const;
	UBOOL		LoadGameObject(const FString & profileName, UObject* Profile);
	UBOOL		DeleteGameObject(const FString & profileName);
	UBOOL		SaveGameObject(const FString & profileName, UObject * saveGameObject);
	UBOOL		ListGameObjects(TArray<FString> *ProfileName);

	void		SaveProfileIcon( ANSICHAR* pathName, UPlayerProfile* Profile ) const;

	// XBox Storage Helper functions

	const ANSICHAR* GetRootPath() const { return m_RootPath; }
	DWORD		GetSectorSize() const	{ return XGetDiskSectorSize(GetRootPath()); }
	DWORD		GetClusterSize() const	{ return XGetDiskClusterSize(GetRootPath()); }
	DWORD		GetSaveGameOverhead() const;
	DWORD		GetFileBytes( DWORD RawBytes ) const
	{
		DWORD ClusterSize = GetClusterSize();
		DWORD NumClusters = (RawBytes + (ClusterSize - 1)) / ClusterSize;
		return NumClusters * ClusterSize;
	}

	UBOOL		GetSizeParams( QWORD& out_TotalBytes, QWORD& out_UsedBytes, QWORD& out_FreeBytes ) const;
	DWORD		GetMaxSaveGameSize( UBOOL bBlocks = 0 ) const;

	virtual void		SetDisconnectFlag( UBOOL bNewValue, UBOOL bTeamGame  );
	virtual UBOOL		GetDisconnectFlag( BYTE& bTeamGame );

protected:
	FString		GetProfileRoot() const;

private:
	ANSICHAR	m_RootPath[5];
};
#endif //XBOXLOADSAVEMANAGER_H