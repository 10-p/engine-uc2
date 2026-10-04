/*=============================================================================
FXboxLevelMemStat.cpp: 
Copyright 2004 Epic Games, Inc. All Rights Reserved.

Revision history:
* Created by Sam Zamani
=============================================================================*/

//	Includes -------------------------------------------------------------------

#include "XboxDrv.h"
#include "XboxLevelMemStat.h"

#ifndef FINAL_RELEASE

//	Defines and Consts ---------------------------------------------------------

//	Macros and Typedefs --------------------------------------------------------

//	Global Data ----------------------------------------------------------------

//	Static Data ----------------------------------------------------------------

//	Local Prototypes -----------------------------------------------------------

//	Class Definitions ----------------------------------------------------------

//	Class Implementations ------------------------------------------------------

void FXboxLevelMemStat::PrintHeader( TCHAR* HeaderText )
{
	if( !HeaderText )
		return;

	Ar.Logf( TEXT("") );
	Ar.Logf( TEXT("===============================================================================") );
	Ar.Logf( TEXT("== %s =="), HeaderText );
	Ar.Logf( TEXT("===============================================================================") );
}

void FXboxLevelMemStat::PrintTotalBytes( TCHAR* Name, DWORD TotalByteCount )
{
	if( !Name )
		return;

	Ar.Logf( TEXT("Total %25s Bytes: %6.1f KB (%3.1f MB)"), Name, TotalByteCount/1024.f, TotalByteCount/1024.f/1024.f );
}

void FXboxLevelMemStat::ShowTextures()
{
	if( bShowDetail )
	{
		PrintHeader( TEXT("Textures") );
		Ar.Logf( TEXT("%10s %13s %6s %13s %30s"), TEXT("Format"), TEXT("Size"), TEXT("Mips"), TEXT("Bytes"), TEXT("Name") );
	}	

	DWORD TotalByteCount = 0;

	for( TObjectIterator<UTexture> It; It; ++It )
	{
		UTexture* Texture = *It;

		FArchiveCountMem Count( Texture );
		TotalByteCount += Count.GetMax();

		if( bShowDetail )
		{
			FString FormatDesc = Texture->GetFormatDesc();

			Ar.Logf( TEXT("%10s [%4i x%5i] %6i %13.1f KB %29s"),
				*FormatDesc,
				Texture->MaterialUSize(),
				Texture->MaterialVSize(),
				Texture->GetNumMips(),
				Count.GetMax()/1024.f,
				Texture->GetFullName()
				);
		}
	}

	PrintTotalBytes( TEXT("Texture"), TotalByteCount );	
}


FString GetFormatDesc( D3DFORMAT InFormat )
{
	switch( InFormat )
	{
	case D3DFMT_P8:			return TEXT("");
	case D3DFMT_A8R8G8B8:	return TEXT("A8R8G8B8");
	case D3DFMT_A4R4G4B4:	return TEXT("A4R4G4B4");
	case D3DFMT_DXT1:		return TEXT("DXT1");
	case D3DFMT_DXT3:		return TEXT("DXT3");
	case D3DFMT_DXT5:		return TEXT("DXT5");
	case D3DFMT_L8:			return TEXT("L8");
	case D3DFMT_V8U8:		return TEXT("V8U8");
	case D3DFMT_L6V5U5:		return TEXT("L6V5U5");
	case D3DFMT_V16U16:		return TEXT("V16U16");
	case D3DFMT_A8:			return TEXT("A8");
	case D3DFMT_R5G6B5:		return TEXT("R5G6B5");
	default:
		if( InFormat >= D3DFMT_LIN_A1R5G5B5 &&
			InFormat <= D3DFMT_LIN_F16 )
		{
			return TEXT("LINEAR");
		}
		return TEXT("?");
	}
	
}

FString GetFormatDesc( ETextureFormat ActualFormat )
{
	D3DFORMAT		Direct3DFormat =	ActualFormat == TEXF_RGBA8	? D3DFMT_A8R8G8B8: 
										ActualFormat == TEXF_DXT1	? D3DFMT_DXT1 :
										ActualFormat == TEXF_DXT3	? D3DFMT_DXT3 :
										ActualFormat == TEXF_DXT5	? D3DFMT_DXT5 :
										ActualFormat == TEXF_V8U8	? D3DFMT_V8U8 :
										ActualFormat == TEXF_L6V5U5 ? D3DFMT_L6V5U5 :
										ActualFormat == TEXF_V16U16	? D3DFMT_V16U16 :
										ActualFormat == TEXF_L8 ? D3DFMT_L8 :
										ActualFormat == TEXF_A8 ? D3DFMT_A8 :
										ActualFormat == TEXF_R5G6B5 ? D3DFMT_R5G6B5 :
										D3DFMT_UNKNOWN;

	return GetFormatDesc( Direct3DFormat );
}


void FXboxLevelMemStat::ShowCachedTexture()
{
	if( bShowDetail )
	{
		PrintHeader( TEXT("Cached Textures") );
		Ar.Logf( TEXT("%10s %13s %6s %13s %25s"), TEXT("Format"), TEXT("Size"), TEXT("Mips"), TEXT("Bytes"), TEXT("Notes") );
	}

	DWORD TotalByteCount = 0;

	for( FD3DResource* Resource = D3DRenDev->ResourceList; Resource; Resource = Resource->NextResource )
	{
		FD3DTexture* Texture = Resource->GetTexture();

		if( !Texture || 
			Texture->RenderTargetSurface ||
			Texture->DepthStencilSurface )
			continue;

		if( bShowDetail )
		{
			Ar.Logf( TEXT("%10s [%4i x%5i] %6i %13.1f KB"),
				*GetFormatDesc( Texture->CachedFormat ),
				Texture->CachedWidth,
				Texture->CachedHeight,
				Texture->CachedNumMips,
				Texture->CalculateFootprint()/1024.f				
				);
		}

		TotalByteCount += Texture->CalculateFootprint();
	}

	PrintTotalBytes( TEXT("Cached Texture"), TotalByteCount );
}

void FXboxLevelMemStat::ShowCachedRenderTarget()
{
	if( bShowDetail )
	{
		PrintHeader( TEXT("Cached Render Targets") );
		Ar.Logf( TEXT("%10s %13s %6s %13s %25s"), TEXT("Format"), TEXT("Size"), TEXT("Mips"), TEXT("Bytes"), TEXT("Notes") );
	}

	DWORD TotalByteCount = 0;

	// show default color (back) buffer surface
	if( D3DRenDev->DefaultRenderTarget->BackBufferSurface )
	{
		D3DSURFACE_DESC SurfaceInfo;
		D3DRenDev->DefaultRenderTarget->BackBufferSurface->GetDesc( &SurfaceInfo );
		TotalByteCount += SurfaceInfo.Size;

		if( bShowDetail )
		{
			Ar.Logf( TEXT("%10s [%4i x%5i] %6i %13.1f KB %29s"),
				*GetFormatDesc( SurfaceInfo.Format ),
				SurfaceInfo.Width,
				SurfaceInfo.Height,
				1,
				SurfaceInfo.Size/1024.f,
				TEXT("Default Back Buffer")
				);
		}
	}

	// show default color (front) buffer surface
	if( D3DRenDev->DefaultRenderTarget->FrontBufferSurface )
	{
		D3DSURFACE_DESC SurfaceInfo;
		D3DRenDev->DefaultRenderTarget->FrontBufferSurface->GetDesc( &SurfaceInfo );
		TotalByteCount += SurfaceInfo.Size;

		if( bShowDetail )
		{
			Ar.Logf( TEXT("%10s [%4i x%5i] %6i %13.1f KB %29s"),
				*GetFormatDesc( SurfaceInfo.Format ),
				SurfaceInfo.Width,
				SurfaceInfo.Height,
				1,
				SurfaceInfo.Size/1024.f,
				TEXT("Default Front Buffer")
				);
		}
	}

	// show default depth buffer surface
	if( D3DRenDev->DefaultRenderTarget->DepthBufferSurface )
	{
		D3DSURFACE_DESC SurfaceInfo;
		D3DRenDev->DefaultRenderTarget->DepthBufferSurface->GetDesc( &SurfaceInfo );
		TotalByteCount += SurfaceInfo.Size;

		if( bShowDetail )
		{
			Ar.Logf( TEXT("%10s [%4i x%5i] %6i %13.1f KB %29s"),
				*GetFormatDesc( SurfaceInfo.Format ),
				SurfaceInfo.Width,
				SurfaceInfo.Height,
				1,
				SurfaceInfo.Size/1024.f,
				TEXT("Default Depth Buffer")
				);
		}
	}

	for( FD3DResource* Resource = D3DRenDev->ResourceList; Resource; Resource = Resource->NextResource )
	{
		FD3DTexture* Texture = Resource->GetTexture();

		if( !Texture || 
			!(Texture->RenderTargetSurface || Texture->DepthStencilSurface) )
			continue;

		DWORD ByteCount = 0;

		// make sure to only count default color buffer surfaces once
		if( Texture->RenderTargetSurface &&
			Texture->RenderTargetSurface != D3DRenDev->DefaultRenderTarget->BackBufferSurface &&
			Texture->RenderTargetSurface != D3DRenDev->DefaultRenderTarget->FrontBufferSurface &&
			Texture->RenderTargetSurface != D3DRenDev->DefaultRenderTarget->DepthBufferSurface )
		{
			D3DSURFACE_DESC SurfaceInfo;
			Texture->RenderTargetSurface->GetDesc( &SurfaceInfo );
			ByteCount += SurfaceInfo.Size;
		}

		// make sure to only count default depth buffer surfaces once
		if( Texture->DepthStencilSurface &&
			Texture->DepthStencilSurface != D3DRenDev->DefaultRenderTarget->DepthBufferSurface )
		{
			D3DSURFACE_DESC SurfaceInfo;
			Texture->DepthStencilSurface->GetDesc( &SurfaceInfo );
			ByteCount += SurfaceInfo.Size;
		}

		if( bShowDetail )
		{
			Ar.Logf( TEXT("%10s [%4i x%5i] %6i %13.1f KB"),
				*GetFormatDesc( Texture->CachedFormat ),
				Texture->CachedWidth,
				Texture->CachedHeight,
				Texture->CachedNumMips,
				ByteCount/1024.f
				);
		}

		TotalByteCount += ByteCount;
	}

	PrintTotalBytes( TEXT("Cached Render Target"), TotalByteCount );
}

void FXboxLevelMemStat::ShowCachedIndexBuffer()
{
	if( bShowDetail )
	{
		PrintHeader( TEXT("Cached Index Buffers") );
		Ar.Logf( TEXT("%13s"), TEXT("Bytes") );
	}

	DWORD TotalByteCount = 0;

	for( FD3DResource* Resource = D3DRenDev->ResourceList; Resource; Resource = Resource->NextResource )
	{
		FD3DIndexBuffer* IndexBuffer = Resource->GetIndexBuffer();

		if( !IndexBuffer )
			continue;

		if( bShowDetail )
		{
			Ar.Logf( TEXT("%13.1f KB"),
				IndexBuffer->CalculateFootprint()/1024.f
				);
		}

		TotalByteCount += IndexBuffer->CalculateFootprint();
	}

	PrintTotalBytes( TEXT("Cached Index Buffer"), TotalByteCount );
}

void FXboxLevelMemStat::ShowCachedVertexStream()
{
	if( bShowDetail )
	{
		PrintHeader( TEXT("Cached Vertex Streams") );
		Ar.Logf( TEXT("%13s"), TEXT("Bytes") );
	}

	DWORD TotalByteCount = 0;

	for( FD3DResource* Resource = D3DRenDev->ResourceList; Resource; Resource = Resource->NextResource )
	{
		FD3DVertexStream* VertexStream = Resource->GetVertexStream();

		if( !VertexStream )
			continue;

		if( bShowDetail )
		{
			Ar.Logf( TEXT("%13.1f KB"),
				VertexStream->CalculateFootprint()/1024.f
				);
		}

		TotalByteCount += VertexStream->CalculateFootprint();
	}

	PrintTotalBytes( TEXT("Cached Vertex Stream"), TotalByteCount );
}

void FXboxLevelMemStat::ShowCachedVertexShader()
{
	if( bShowDetail )
	{
		PrintHeader( TEXT("Cached Vertex Shaders") );
		Ar.Logf( TEXT("%20s %30s %13s"), TEXT("Enum"), TEXT("NUM of INSTRUCTIONS"), TEXT("Bytes") );
	}

	DWORD TotalByteCount = 0;

	for( TMap<QWORD, FD3DCombinedVertexShader*>::TIterator It(D3DRenDev->CombinedVertexShaders); It; ++It )
	{
		FD3DCombinedVertexShader* pCombinedVertexShader = It.Value();

		if( !pCombinedVertexShader )
			continue;

		if(0)/*if( bShowDetail )*/
		{
			Ar.Logf( TEXT("%29s %13i %13.1f KB"),
				appI64toa( (SQWORD)pCombinedVertexShader->VertexShaderEnum ),
				pCombinedVertexShader->CombinedMicrocodeInstructions,
				pCombinedVertexShader->CalculateFootprint()/1024.f
				);				
		}

		TotalByteCount += pCombinedVertexShader->CalculateFootprint();
	}	

	PrintTotalBytes( TEXT("Cached Vertex Shader"), TotalByteCount );
}

void FXboxLevelMemStat::ShowCachedResource( ECachedResourceType ResourceType )
{
	switch( ResourceType )
	{
	case CACHED_TEXTURE:
		ShowCachedTexture();
		break;
	case CACHED_TEXTURE_RENDERTARGET:
		ShowCachedRenderTarget();
		break;
	case CACHED_VERTEXSTREAM:
		ShowCachedVertexStream();
		break;
	case CACHED_INDEXBUFFER:
		ShowCachedIndexBuffer();
		break;
	case CACHED_VERTEXSHADER:
		ShowCachedVertexShader();
		break;
	case CACHED_ALL:
	default:
		ShowCachedTexture();
		ShowCachedRenderTarget();
		ShowCachedVertexStream();
		ShowCachedIndexBuffer();
		ShowCachedVertexShader();
		break;
	}		
}

void FXboxLevelMemStat::ShowStaticMeshes()
{
	if( bShowDetail )
	{
		PrintHeader( TEXT("Static Meshes") );
		Ar.Logf( TEXT("%15s %15s %13s %30s"), TEXT("Triangles"), TEXT("Vertices"), TEXT("Bytes"), TEXT("Name") );
	}

	DWORD TotalByteCount = 0;

	for( TObjectIterator<UStaticMesh> It; It; ++It )
	{
		UStaticMesh* StaticMesh = *It;

		FArchiveCountMem Count( StaticMesh );
		TotalByteCount += Count.GetMax();

		if( bShowDetail )
		{
			INT NumTriangles = 0;
			for( INT i=0; i < StaticMesh->Sections.Num(); i++ )
			{
				FStaticMeshSection& Section = StaticMesh->Sections( i );
				NumTriangles += Section.NumTriangles;
			}
			INT NumVertices = StaticMesh->VertexStream.Vertices.Num();			

			Ar.Logf( TEXT("%15i %15i %13.1f KB %29s"),
				NumTriangles,
				NumVertices,
				Count.GetMax()/1024.f,
				StaticMesh->GetFullName() 
				);
		}
	}

	PrintTotalBytes( TEXT("Static Mesh"), TotalByteCount );
}

void FXboxLevelMemStat::ShowStaticMeshInstances()
{
	if( bShowDetail )
	{
		PrintHeader( TEXT("Static Mesh Instances") );
		Ar.Logf( TEXT("%13s %30s"), TEXT("Bytes"), TEXT("Name") );
	}

	DWORD TotalByteCount = 0;

	for( TObjectIterator<UStaticMeshInstance> It; It; ++It )
	{
		UStaticMeshInstance* StaticMeshInst = *It;

		FArchiveCountMem Count( StaticMeshInst );
		TotalByteCount += Count.GetMax();

		if( bShowDetail )
		{
			Ar.Logf( TEXT("%13.1f KB %29s"),
				Count.GetMax()/1024.f,
				StaticMeshInst->GetFullName() 
				);
		}
	}

	PrintTotalBytes( TEXT("Static Mesh Instance"), TotalByteCount );
}

void FXboxLevelMemStat::ShowTerrain()
{
	if( bShowDetail )
	{
		PrintHeader( TEXT("Terrain") );
		Ar.Logf( TEXT("%15s %15s %13s %30s"), TEXT("Triangles"), TEXT("Vertices"), TEXT("Bytes"), TEXT("Name") );
	}

	DWORD TotalByteCount = 0;

	for( TObjectIterator<ATerrainInfo> It; It; ++It )
	{
		ATerrainInfo* Terrain = *It;

		FArchiveCountMem Count( Terrain );
		DWORD ByteCount = Count.GetMax();

		for( INT BlockIdx=0; BlockIdx < Terrain->SectorBlocks.Num(); BlockIdx++ )
		{
			UTerrainSectorBlock* SecBlock = Terrain->SectorBlocks(BlockIdx);
			if( SecBlock )
			{
				FArchiveCountMem CountSecBlock( SecBlock );				
				ByteCount += CountSecBlock.GetMax();
			}
		}

		for( INT SectorIdx=0; SectorIdx < Terrain->Sectors.Num(); SectorIdx++ )
		{
			UTerrainSector* Sector = Terrain->Sectors(SectorIdx);
			if( Sector )
			{
				FArchiveCountMem CountSector( Sector );
				ByteCount += CountSector.GetMax();
			}
		}

		if( bShowDetail )
		{
			INT NumTriangles = Terrain->HeightmapX * Terrain->HeightmapY * 2;
			INT NumVertices = 0;
			for( INT BlockIdx=0; BlockIdx < Terrain->SectorBlocks.Num(); BlockIdx++ )
			{
				UTerrainSectorBlock* SecBlock = Terrain->SectorBlocks(BlockIdx);
				if( SecBlock )
				{
					NumVertices += SecBlock->VertexStream.Vertices.Num();
				}
			}			

			Ar.Logf( TEXT("%15i %15i %13.1f KB %29s"),
				NumTriangles,
				NumVertices,
				ByteCount/1024.f,
				Terrain->GetFullName() 
				);
		}

#if 0
		if( Terrain->TerrainMap )
		{
			FArchiveCountMem Count( Terrain->TerrainMap );
			debugf( TEXT("TerrainMap=%s [%d]"), Terrain->TerrainMap->GetFullName(), Count.GetMax() );
		}

		if( Terrain->CollapsedLayers )
		{
			FArchiveCountMem Count( Terrain->CollapsedLayers );
			debugf( TEXT("CollapsedLayers=%s [%d]"), Terrain->CollapsedLayers->GetFullName(), Count.GetMax() );
		}

		for( INT i=0; i < 32; i++ )
		{
			debugf( TEXT("Terrain Layer [%d]"), i );
			if( Terrain->Layers[i].Texture )
			{
				FArchiveCountMem Count( Terrain->Layers[i].Texture );
				debugf( TEXT("Texture=%s [%d]"), Terrain->Layers[i].Texture->GetFullName(), Count.GetMax() );
			}
			if( Terrain->Layers[i].AlphaMap )
			{
				FArchiveCountMem Count( Terrain->Layers[i].AlphaMap );
				debugf( TEXT("AlphaMap=%s [%d]"), Terrain->Layers[i].AlphaMap->GetFullName(), Count.GetMax() );
			}
			if( Terrain->Layers[i].LayerWeightMap )
			{
				FArchiveCountMem Count( Terrain->Layers[i].LayerWeightMap );
				debugf( TEXT("LayerWeightMap=%s [%d]"), Terrain->Layers[i].LayerWeightMap->GetFullName(), Count.GetMax() );
			}
		}

		for( INT i=0; i < Terrain->DecoLayers.Num(); i++ )
		{
			debugf( TEXT("Terrain DecoLayer [%d]"), i );
			if( Terrain->DecoLayers(i).ScaleMap )
			{
				FArchiveCountMem Count( Terrain->DecoLayers(i).ScaleMap );
				debugf( TEXT("ScaleMap=%s [%d]"), Terrain->DecoLayers(i).ScaleMap->GetFullName(), Count.GetMax() );
			}
			if( Terrain->DecoLayers(i).DensityMap )
			{
				FArchiveCountMem Count( Terrain->DecoLayers(i).DensityMap );
				debugf( TEXT("DensityMap=%s [%d]"), Terrain->DecoLayers(i).DensityMap->GetFullName(), Count.GetMax() );
			}
			if( Terrain->DecoLayers(i).ColorMap )
			{
				FArchiveCountMem Count( Terrain->DecoLayers(i).ColorMap );
				debugf( TEXT("ColorMap=%s [%d]"), Terrain->DecoLayers(i).ColorMap->GetFullName(), Count.GetMax() );
			}			
		}
#endif

		TotalByteCount += ByteCount;
	}

	PrintTotalBytes( TEXT("Terrain"), TotalByteCount );
}

void FXboxLevelMemStat::ShowBSP()
{
	if( bShowDetail )
	{
		PrintHeader( TEXT("BSP") );
		Ar.Logf( TEXT("%15s %15s %13s %30s"), TEXT("Surfaces"), TEXT("Vertices"), TEXT("Bytes"), TEXT("Name") );
	}

	DWORD TotalByteCount = 0;

	for( TObjectIterator<UModel> It; It; ++It )
	{
		UModel* Model = *It;

		FArchiveCountMem Count( Model );
		TotalByteCount += Count.GetMax();

		if( bShowDetail )
		{
			INT NumVertices = Model->Verts.Num();
			INT NumSurfaces = Model->Surfs.Num();

			Ar.Logf( TEXT("%15i %15i %13.1f KB %29s"),
				NumSurfaces,
				NumVertices,
				Count.GetMax()/1024.f,
				Model->GetFullName() 
				);			
		}
	}

	if( bShowDetail )
	{
		PrintHeader( TEXT("Light Maps") );
		Ar.Logf( TEXT("%10s %13s %6s %13s"), TEXT("Format"), TEXT("Size"), TEXT("Mips"), TEXT("Bytes") );
	}

	// show lightmap stats
	for( TObjectIterator<UModel> It; It; ++It )
	{
		UModel* Model = *It;

		for( INT TexIdx=0; TexIdx < Model->LightMapTextures.Num(); TexIdx++ )
		{
			FLightMapTexture& LightMap = Model->LightMapTextures( TexIdx );
			FBaseTexture* pLightMapTex = &LightMap;
			// actual light map texture is either compressed and stored in StaticTexture
			// or it is a composite texture
			if( LightMap.StaticTexture.Revision == LightMap.Revision) 
			{
				pLightMapTex = &LightMap.StaticTexture;
			}

			ETextureFormat TexFormat = pLightMapTex->GetFormat();
			INT Width = pLightMapTex->GetWidth();
			INT Height = pLightMapTex->GetHeight();
			INT NumMips = pLightMapTex->GetNumMips();

			DWORD LightMapSize = 0;			
			for( INT i=0; i < NumMips; i++ )
			{
				LightMapSize += GetBytesPerPixel( TexFormat, (Width >> i) * (Height >> i) );
			}

			TotalByteCount += LightMapSize;

			if( bShowDetail )
			{
				FString FormatDesc = GetFormatDesc( TexFormat );
				Ar.Logf( TEXT("%10s [%4i x%5i] %6i %13.1f KB"),
					*FormatDesc,
					Width,
					Height,
					NumMips,
					LightMapSize/1024.f
					);				
			}
		}		
	}

	PrintTotalBytes( TEXT("BSP"), TotalByteCount );
}

void FXboxLevelMemStat::ShowSkeletalMesh()
{
	if( bShowDetail )
	{
		PrintHeader( TEXT("Skeletal Meshes") );
		Ar.Logf( TEXT("%20s %13s %35s"), TEXT("Vertices"), TEXT("Bytes"), TEXT("Name") );
	}

	DWORD TotalByteCount = 0;

	for( TObjectIterator<USkeletalMesh> It; It; ++It )
	{
		USkeletalMesh* SkelMesh = *It;

		FArchiveCountMem Count( SkelMesh );
		TotalByteCount += Count.GetMax();

		if( bShowDetail )
		{
			INT NumVertices = 0;
			for( INT lodIdx=0; lodIdx < SkelMesh->LODModels.Num(); lodIdx++ )
			{
				FStaticLODModel& LODModel = SkelMesh->LODModels(lodIdx);
				NumVertices += LODModel.GPUVertexStream.Vertices.Num();
				NumVertices += LODModel.SmoothVerts.Num();
				NumVertices += LODModel.RigidVertexStream.Vertices.Num();
			}

			Ar.Logf( TEXT("%20i %13.1f KB %34s"),
				NumVertices,
				Count.GetMax()/1024.f,
				SkelMesh->GetFullName() 
				);
		}
	}

	PrintTotalBytes( TEXT("Skeletal Mesh"), TotalByteCount );
}

void FXboxLevelMemStat::ShowAnimation()
{
	if( bShowDetail )
	{
		PrintHeader( TEXT("Skeletal Animation Sequences") );		
	}

	DWORD TotalByteCount = 0;

	for( TObjectIterator<UMeshAnimation> It; It; ++It )
	{
		UMeshAnimation* MeshAnim = *It;

		FArchiveCountMem Count( MeshAnim );
		TotalByteCount += Count.GetMax();

		if( bShowDetail )
		{
			Ar.Logf( TEXT("%20s [%s]"), TEXT(""), MeshAnim->GetFullName() );
			Ar.Logf( TEXT("%20s %13s %30s"), TEXT("Frames"), TEXT("Bytes"), TEXT("Name") );
			for( INT SeqIdx=0; SeqIdx < MeshAnim->AnimSeqs.Num(); SeqIdx++ )
			{
				DWORD ByteCount = 0;
				FMeshAnimSeq& AnimSeq = MeshAnim->AnimSeqs( SeqIdx );
				MotionChunk* AnimData = MeshAnim->GetMovement( AnimSeq.Name );
				if( AnimData )
				{
					ByteCount = AnimData->CalculateMemFootprint();
				}

				Ar.Logf( TEXT("%20i %13.1f KB %29s"),
					AnimSeq.NumFrames,
					ByteCount/1024.f,
					*AnimSeq.Name
					);

#if 0
				Ar.Logf( TEXT("AnalogTracks=%d"), AnimData->AnimTracks.Num() );
				Ar.Logf( TEXT("FlexTracks=%d"), AnimData->FlexTracks.Num() );
				Ar.Logf( TEXT("RawTracks=%d"), AnimData->RawTracks.Num() );

				for( INT i=0; i < AnimData->FlexTracks.Num(); i++ )
				{
					if( AnimData->FlexTracks(i).FlexTrackPtr )
					{
						switch( AnimData->FlexTracks(i).FlexTrackPtr->GetClassType() )
						{
						case ADT_48Bit:				
							debugf( TEXT("[%d]=ADT_48Bit"), i );
							break;
						case ADT_48BitRotOnly:
							debugf( TEXT("[%d]=ADT_48BitRotOnly"), i );
							break;
						case ADT_Static:
							debugf( TEXT("[%d]=ADT_Static"), i );
							break;
						case ADT_Raw:
							debugf( TEXT("[%d]=ADT_Raw"), i );
							break;
						case ADT_Empty:						
							debugf( TEXT("[%d]=ADT_Empty"), i );
							break;
						default:
							debugf( TEXT("[%d]=UNKNOWN"), i );
						}
					}
				}
#endif

			}
			Ar.Logf( TEXT("TotalBytes:		%13.1f KB"), Count.GetMax()/1024.f );
			Ar.Logf( TEXT("") );
		}
	}

	PrintTotalBytes( TEXT("Skeletal Animation"), TotalByteCount );
}

void FXboxLevelMemStat::ShowStaticProjectors()
{
	if( bShowDetail )
	{
		PrintHeader( TEXT("Static Projectors") );
		Ar.Logf( TEXT("%30s %13s %30s"), TEXT("Vertices"), TEXT("Bytes"), TEXT("Name") );
	}

	DWORD TotalByteCount = 0;

	// BSP static projectors
	for( TObjectIterator<UModel> It; It; ++It )
	{
		UModel* Model = *It;

		for( INT NodeIdx=0; NodeIdx < Model->Nodes.Num(); NodeIdx++ )
		{
			FBspNode& BspNode = Model->Nodes( NodeIdx );
			for( INT ProjIdx=0; ProjIdx < BspNode.Projectors.Num(); ProjIdx++ )
			{
				FStaticProjectorInfo* StaticProjInfo = BspNode.Projectors( ProjIdx );
				if( StaticProjInfo )
				{
					INT ByteCount = StaticProjInfo->CalculateFootPrint();

					if( bShowDetail )
					{
						Ar.Logf( TEXT("%30i %13.1f KB %29s"),
							StaticProjInfo->Vertices.Num(),
							ByteCount/1024.f,
							(StaticProjInfo->RenderInfo && StaticProjInfo->RenderInfo->Projector) ? StaticProjInfo->RenderInfo->Projector->GetFullName() : TEXT("")
							);
					}

					TotalByteCount += ByteCount;
				}					
			}
		}
	}

	// Terrain static projectors
	for( TObjectIterator<ATerrainInfo> It; It; ++It )
	{
		ATerrainInfo* TerrainInfo = *It;
		for( INT SecIdx=0; SecIdx < TerrainInfo->Sectors.Num(); SecIdx++ )
		{
			UTerrainSector* Sector = TerrainInfo->Sectors( SecIdx );

			if( !Sector )
				continue;

			for( INT ProjIdx=0; ProjIdx < Sector->Projectors.Num(); ProjIdx++ )
			{
				FStaticProjectorInfo* StaticProjInfo = Sector->Projectors( ProjIdx );
				if( StaticProjInfo )
				{
					INT ByteCount = StaticProjInfo->CalculateFootPrint();

					if( bShowDetail )
					{
						Ar.Logf( TEXT("%30i %13.1f KB %29s"),
							StaticProjInfo->Vertices.Num(),
							ByteCount/1024.f,
							(StaticProjInfo->RenderInfo && StaticProjInfo->RenderInfo->Projector) ? StaticProjInfo->RenderInfo->Projector->GetFullName() : TEXT("")
							);
					}

					TotalByteCount += ByteCount;
				}					
			}				
		}
	}

	// Static mesh static projectors
	for( TObjectIterator<AStaticMeshActor> It; It; ++It )
	{
		AStaticMeshActor* StaticMeshActor = *It;
		for( INT ProjIdx=0; ProjIdx < StaticMeshActor->StaticMeshProjectors.Num(); ProjIdx++ )
		{
			FStaticProjectorInfo* StaticProjInfo = StaticMeshActor->StaticMeshProjectors( ProjIdx );
			if( StaticProjInfo )
			{
				INT ByteCount = StaticProjInfo->CalculateFootPrint();

				if( bShowDetail )
				{
					Ar.Logf( TEXT("%30i %13.1f KB %29s"),
						StaticProjInfo->Vertices.Num(),
						ByteCount/1024.f,
						(StaticProjInfo->RenderInfo && StaticProjInfo->RenderInfo->Projector) ? StaticProjInfo->RenderInfo->Projector->GetFullName() : TEXT("")
						);
				}

				TotalByteCount += ByteCount;
			}					
		}
	}

	PrintTotalBytes( TEXT("Static Projector"), TotalByteCount );
}

void FXboxLevelMemStat::ShowActors()
{
	if( bShowDetail )
	{
		PrintHeader( TEXT("Actors") );
		Ar.Logf( TEXT("%30s %13s %30s"), TEXT("Class"), TEXT("Bytes"), TEXT("Name") );
	}

	DWORD TotalByteCount = 0;

	for( TObjectIterator<AActor> It; It; ++It )
	{
		AActor* Actor = *It;

		if( Actor->bDeleteMe )
			continue;

		FArchiveCountMem Count( Actor );
		TotalByteCount += Count.GetMax();

		if( bShowDetail )
		{
			Ar.Logf( TEXT("%30s %13.1f KB %29s"),
				Actor->GetClass()->GetName(),
				Count.GetMax()/1024.f,
				Actor->GetFullName() 
				);
		}
	}

	PrintTotalBytes( TEXT("Actor"), TotalByteCount );
}

//------------------------------------------------------------------------------

#endif //#ifndef FINAL_RELEASE