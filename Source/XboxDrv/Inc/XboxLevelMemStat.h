/*=============================================================================
FXboxLevelMemStat: Map stats
Copyright 2004 Epic Games, Inc. All Rights Reserved.

Revision history:
* Created by Sam Zamani
=============================================================================*/

#if !defined(__XBOX_LEVEL_MEMSTAT_H) && !defined(FINAL_RELEASE)
#define __XBOX_LEVEL_MEMSTAT_H

//	Include Files / Forward References -----------------------------------------

//	Defines, Consts, and Macros ------------------------------------------------

//	Typedefs -------------------------------------------------------------------

//	Global Data ----------------------------------------------------------------

//	Global Class Definitions ---------------------------------------------------

class FXboxLevelMemStat
{

// Class DEFINES -------------------------------------------------------
public:
	enum ECachedResourceType
	{
		CACHED_TEXTURE,
		CACHED_TEXTURE_RENDERTARGET,
		CACHED_VERTEXSTREAM,
		CACHED_INDEXBUFFER,
		CACHED_VERTEXSHADER,
		CACHED_ALL
	};

// Class DATA ----------------------------------------------------------
private:
	UBOOL bShowDetail;
	UD3DRenderDevice* D3DRenDev;
	FOutputDevice& Ar;

// Class METHODS -------------------------------------------------------
public:
	FXboxLevelMemStat( FOutputDevice& InAr, UD3DRenderDevice* InD3DRenDev, UBOOL InShowDetail=1 ) :
		Ar(InAr),
		D3DRenDev(InD3DRenDev),
		bShowDetail(InShowDetail)
	{
	}

	~FXboxLevelMemStat()
	{
		Ar.Flush();
	}

	void ShowTextures();
	void ShowCachedTexture();
	void ShowCachedRenderTarget();
	void ShowCachedIndexBuffer();
	void ShowCachedVertexStream();
	void ShowCachedVertexShader();
	void ShowCachedResource( ECachedResourceType ResourceType=CACHED_ALL );
	void ShowStaticMeshes();
	void ShowStaticMeshInstances();
	void ShowTerrain();
	void ShowBSP();
	void ShowSkeletalMesh();
	void ShowAnimation();
	void ShowStaticProjectors();
	void ShowActors();

private:
	void PrintHeader( TCHAR* HeaderText );
	void PrintTotalBytes( TCHAR* Name, DWORD TotalByteCount );
};

//	Global Class Implementations -----------------------------------------------

//==============================================================================
#endif //_INC_XBOX_LEVEL_MEMSTAT