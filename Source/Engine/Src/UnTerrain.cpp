/*=============================================================================
	UnTerrain.cpp: Unreal objects
	Copyright 1997-2000 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Jack Porter
=============================================================================*/

#include "EnginePrivate.h"
#include <math.h>

#include "UnRenderPrivate.h"

#define MAXSTREAMVERTICES 65535
#define TERRAINSECTORSIZE (TerrainSectorSize)

#ifndef CONSOLE
extern ENGINE_API FTerrainTools		GTerrainTools;
extern ENGINE_API FRebuildTools		GRebuildTools;
#endif

#ifdef _XBOX
// scion sz - enabled since vertex shaders should
// handle tex coord source remapping properly
#define JG_PUSHBUFFER_INDICES
#endif

// Only the modes we need to know about are duplicated here.
enum EEditorMode
{
	EM_TerrainEdit		= 28,	// Terrain editing.
};

// Terrain sector sorting.
static INT IntSortOrder = 1;
static INT Compare(INT& A,INT& B)
{
	return (A - B) * IntSortOrder;
}

/*------------------------------------------------------------------------------
    UTerrainSector Implementation.
------------------------------------------------------------------------------*/

UTerrainSector::UTerrainSector( ATerrainInfo* InInfo, INT InOffsetX, INT InOffsetY, INT InQuadsX, INT InQuadsY )
:	Info		( InInfo )
,	OffsetX		( InOffsetX )
,	OffsetY		( InOffsetY )
,	QuadsX		( InQuadsX )
,	QuadsY		( InQuadsY )
,	BlockIndex	(-1)
{
	guard(UTerrainSector::UTerrainSector);
	unguard;
}

void UTerrainSector::Serialize(FArchive& Ar)
{
	guard(UTerrainSector::Serialize);
	Super::Serialize(Ar);

	Ar	<< Info;

	Ar 	<< QuadsX << QuadsY
		<< OffsetX << OffsetY;

	Ar << BoundingBox;

	Ar << LightInfos;

#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
	if(Ar.LicenseeVer() == XBOX_PACKAGE_FORMAT)
	{
		Ar << RenderPasses << BlockIndex << VertexStreamOffset;
	}
	else
#endif
	if( Ar.IsLoading() )
	{
        BlockIndex = -1;
	}
	else if( !(Ar.IsLoading() || Ar.IsSaving()) )
	{
		Ar	<< CompleteIndexBuffer 
			<< CompleteNumTriangles 
			<< CompleteNumIndices 
			<< CompleteMinIndex 
			<< CompleteMaxIndex 
			<< Projectors 
			<< RenderPasses
			<< BlockIndex;
	}

	unguard;
}

void UTerrainSector::PostLoad()
{
	guard(UTerrainSector::PostLoad);
	Super::PostLoad();

	unguard;	
}

void UTerrainSector::Destroy()
{
	guard(UTerrainSector::Destroy);
	Super::Destroy();
	for(INT ProjectorIndex = 0;ProjectorIndex < Projectors.Num();ProjectorIndex++)
	{
		Projectors(ProjectorIndex)->RenderInfo->RemoveReference();
		Projectors(ProjectorIndex) = Projectors(ProjectorIndex)->RemoveRef();
	}
	unguard;
}

//
// Terrain pass/layer triangluation and optimization
//
UBOOL UTerrainSector::IsSectorAll( INT Layer, BYTE AlphaValue )
{
	guard(UTerrainSector::IsSectorAll);

	UTexture* AlphaMap = Info->Layers[Layer].AlphaMap;
	FLOAT Ratio = (FLOAT)AlphaMap->USize / (FLOAT)Info->HeightmapX;

	INT MinX = appFloor(Ratio * OffsetX);
	INT MaxX = appCeil(Ratio * (OffsetX + QuadsX));
	INT MinY = appFloor(Ratio * OffsetY);
	INT MaxY = appCeil(Ratio * (OffsetY + QuadsY));

	for( INT x=MinX;x<=MaxX;x++ )
		for( INT y=MinY;y<=MaxY;y++ )
			if( Info->GetLayerAlpha( x, y, -2, AlphaMap ) != AlphaValue )
				return 0;

	return 1;
	unguard;
}

UBOOL UTerrainSector::IsTriangleAll( INT Layer, INT X, INT Y, INT Tri, INT Turned, BYTE AlphaValue )
{
	guard(UTerrainSector::IsTriangleAll);

	UTexture* AlphaMap = Info->Layers[Layer].AlphaMap;

	//!! Alphamaps must be square!
	check(AlphaMap);
	check(AlphaMap->USize == AlphaMap->VSize);

	// Special-case 1:1 alphamap:heightmap ratio for performance
	if( AlphaMap->USize == (FLOAT)Info->HeightmapX )
	{
		INT x = X+OffsetX;
		INT y = Y+OffsetY;

		if( Turned )
		{
			if( Tri )
			{
				// 432
				if( Info->GetLayerAlpha( x,   y+1, -2, AlphaMap ) != AlphaValue ||
					Info->GetLayerAlpha( x+1, y+1, -2, AlphaMap ) != AlphaValue ||
					Info->GetLayerAlpha( x+1, y,   -2, AlphaMap ) != AlphaValue )
					return 0;
			}
			else
			{
				// 142
				if( Info->GetLayerAlpha( x,   y,   -2, AlphaMap ) != AlphaValue ||
					Info->GetLayerAlpha( x,   y+1, -2, AlphaMap ) != AlphaValue ||
					Info->GetLayerAlpha( x+1, y,   -2, AlphaMap ) != AlphaValue )
					return 0;
			}
		}
		else
		{
			if( Tri )
			{
				// 132
				if( Info->GetLayerAlpha( x,   y,   -2, AlphaMap ) != AlphaValue ||
					Info->GetLayerAlpha( x+1, y+1, -2, AlphaMap ) != AlphaValue ||
					Info->GetLayerAlpha( x+1, y,   -2, AlphaMap ) != AlphaValue )
					return 0;
			}
			else
			{
				// 143
				if( Info->GetLayerAlpha( x,   y,   -2, AlphaMap ) != AlphaValue ||
					Info->GetLayerAlpha( x,   y+1, -2, AlphaMap ) != AlphaValue ||
					Info->GetLayerAlpha( x+1, y+1, -2, AlphaMap ) != AlphaValue )
					return 0;
			}
		}

	}
	else
	{
		FLOAT Ratio = (FLOAT)AlphaMap->USize / (FLOAT)Info->HeightmapX;

		INT MinX = appFloor(Ratio * (X + OffsetX));
		INT MaxX = appCeil(Ratio * (X + OffsetX + 1));
		INT MinY = appFloor(Ratio * (Y + OffsetY));
		INT Range = MaxX - MinX;

		if( Turned )
		{
			if( Tri )
			{
				// 432
				for( INT x=0; x<=Range; x++ )
					for( INT y=Range;y>=Range-x;y-- )
						if( Info->GetLayerAlpha( x+MinX, y+MinY, -2, AlphaMap ) != AlphaValue )
							return 0;
			}	
			else
			{
				// 142
				for( INT x=0; x<=Range; x++ )
					for( INT y=0;y<=Range-x;y++ )
						if( Info->GetLayerAlpha( x+MinX, y+MinY, -2, AlphaMap ) != AlphaValue )
							return 0;
			}
		}
		else
		{
			if( Tri )
			{
				// 132
				for( INT x=0; x<=Range; x++ )
					for( INT y=0;y<=x;y++ )
						if( Info->GetLayerAlpha( x+MinX, y+MinY, -2, AlphaMap ) != AlphaValue )
							return 0;
			}
			else
			{
				// 143
				for( INT x=0; x<=Range; x++ )
					for( INT y=Range;y>=x;y-- )
						if( Info->GetLayerAlpha( x+MinX, y+MinY, -2, AlphaMap ) != AlphaValue )
							return 0;
			}
		}
	}
	
	return 1;

	unguard;
}

UBOOL UTerrainSector::PassShouldRenderTriangle( INT Pass, INT X, INT Y, INT Tri, INT Turned )
{
	guard(UTerrainSector::PassShouldRenderTriangle);

	FTerrainSectorRenderPass &RenderPass = RenderPasses(Pass);
	FTerrainRenderCombination* RenderCombination = RenderPass.GetRenderCombination(Info);
	if( RenderCombination->RenderMethod == RM_AlphaMap )
	{
		// 1. Check if this triangle is completely transparent in all layers in this pass.
		UBOOL Transparent=1;
		for( INT l=0;l<RenderCombination->Layers.Num();l++ )
		{
			if( !IsTriangleAll( RenderCombination->Layers(l), X, Y, Tri, Turned, 0 ) )
			{
				Transparent = 0;
				break;
			}
		}

		if( Transparent )
			return 0;
		

		// 2. Check if the triangle in a layer in any pass above this layer occludes this triangle.
		for( INT p=Pass+1;p<RenderPasses.Num();p++ )
		{
			for( INT l=0;l<RenderPasses(p).GetRenderCombination(Info)->Layers.Num();l++ )
			{
				if( !Info->Layers[RenderPasses(p).GetRenderCombination(Info)->Layers(l)].Texture->IsTransparent() &&
					IsTriangleAll( RenderPasses(p).GetRenderCombination(Info)->Layers(l), X, Y, Tri, Turned, 255 ) )
					return 0;
			}
		}
		return 1;
	}
	else
	{
		// Triangles in the first pass must always be written to the framebuffer
		if( Pass == 0 )
			return 1;

		FColor Black = FColor(0,0,0,255);
		INT x = X+OffsetX;
		INT y = Y+OffsetY;

		for( INT l=0;l<RenderCombination->Layers.Num();l++ )
		{
			UTexture* WeightMap = Info->Layers[RenderCombination->Layers(l)].LayerWeightMap;

			// Triangles in subsequent passes can be omitted if they're solid black
			if( Info->GetTextureColor( x,   y,   WeightMap ) != Black ||
				Info->GetTextureColor( x+1, y,   WeightMap ) != Black ||
				Info->GetTextureColor( x+1, y+1, WeightMap ) != Black ||
				Info->GetTextureColor( x,   y+1, WeightMap ) != Black )
				return 1;
		}
		return 0;
	}

	unguard;
}

/* scion ======================================================================
* UTerrainSector::AddToSectorBlock
* Author: sz
*
* Add this sector to its corresponding block
* ============================================================================
*/
INT UTerrainSector::AddToSectorBlock()
{
	guard(UTerrainSector::AddToSectorBlock);
	checkSlow(Info);

	INT Index = Info->GetSectorBlockIndex( OffsetX, OffsetY );
	checkSlow( Index < Info->SectorBlocks.Num() );

    UTerrainSectorBlock* Block = Info->SectorBlocks( Index );
	Block->AddSector( this );

	BlockIndex = Index;

	return Index;

	unguard;
}



void UTerrainSector::GenerateTriangles()
{
	guard(UTerrainSector::GenerateTriangles);

	if( !Info->TerrainMap )
		return;

	// scion sz -	
	if( BlockIndex == -1 )
	{
		// make sure we belong to a sector block
		AddToSectorBlock();
		checkSlow( BlockIndex > -1 );

		// get vertex stream from block we reisde in
		FTerrainVertexStream& VertexStream = GetVertexStream();

		// calc our new offset
		VertexStreamOffset = VertexStream.Vertices.Num();

		// add enough space for our quads in the vertex stream
		INT count = (QuadsX+1) * (QuadsY+1);
		VertexStream.Vertices.AddZeroed(count);
	}

	//
	// Update vertex positions
	//
	BoundingBox.Init();
	// get vertex stream from block we reisde in
	FTerrainVertexStream& VertexStream = GetVertexStream();
	// starting vertex
	FTerrainVertex* Vertex = &VertexStream.Vertices(VertexStreamOffset);
	// get block
	UTerrainSectorBlock* Block = Info->SectorBlocks(BlockIndex);	Block=Block;
	checkSlow(Block);

	for( INT y=0;y<=QuadsY;y++ )
	{
		for( INT x=0;x<=QuadsX;x++ )
		{
			INT g = GetGlobalVertex(x,y);
			BoundingBox += Info->Vertices( g );

#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
			// scion sz - WORD height value
			// note that the heightmap value is an unsigned word and must be converted to signed
			// this is done by subtracting half of max unsigned word and then scaling back up
			// using the matrix transform
			Vertex->Height = (SWORD)(Info->GetHeightmap( x + OffsetX, y + OffsetY ) - ((MAXWORD+1)/2));

			// scion sz - BYTE xy offset values
			// offsetXY stores the offset from the origin of this block
			// the offset is transformed to world space using the block offset and current world
			// matrix in a vertex shader (see TerrainSpecial.vsh)
			Vertex->OffsetXY[0] = (x + OffsetX - Block->OffsetX);
			Vertex->OffsetXY[1] = (y + OffsetY - Block->OffsetY);

			// normal and color stored as before
			Vertex->Normal = Info->GetVertexNormal(x+OffsetX,y+OffsetY).Pack();
			Vertex->Color = Info->VertexColors( g );
#else
			Vertex->Position = Info->Vertices( g );
			Vertex->Normal = Info->GetVertexNormal(x+OffsetX,y+OffsetY);
			Vertex->Color = Info->VertexColors( g );
			Vertex->U = ((FLOAT)(x+OffsetX) + 0.5f) / (FLOAT)Info->HeightmapX;
			Vertex->V = ((FLOAT)(y+OffsetY) + 0.5f) / (FLOAT)Info->HeightmapY;
#endif
			
			Vertex++;
		}
	}
	// flag to re-cache
	VertexStream.Revision++;

	//
	// Work out layer triangulation.
	//
	TArray<INT> SectorLayers;
	for( INT l=0; l<ARRAY_COUNT(Info->Layers) && Info->Layers[l].AlphaMap && Info->Layers[l].Texture; l++ )
	{
		// if the layer is completely transparent in this sector, skip it.
		if( IsSectorAll( l, 0 ) )
			continue;

		// see if this layer is completely occluded by another layer above it in this sector.
		for( INT o=l+1;o<ARRAY_COUNT(Info->Layers) && Info->Layers[o].AlphaMap && Info->Layers[o].Texture; o++)
			if( !Info->Layers[o].Texture->IsTransparent() && IsSectorAll( o, 255) )
				goto SkipLayer;

		SectorLayers.AddItem(l);
		SkipLayer:;
	}

#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
	//jg -- Hardcode on the xbox or during conversion
	INT MaxSimultaneousLayers = 3;
	UBOOL ForceAlphaMap = 0;
#else
	// Work out the number of passes required for this sector.
	INT MaxSimultaneousLayers = Info->GetLevel()->Engine->GRenDev ? Info->GetLevel()->Engine->GRenDev->GetRenderCaps()->MaxSimultaneousTerrainLayers : 1;

	UBOOL ForceAlphaMap =  GIsEditor || (Info->GetLevel()->Engine->GRenDev ? Info->GetLevel()->Engine->GRenDev->IsVoodoo3 : 0);
#endif
	//INT MaxSimultaneousLayers = GIsEditor ? 1 : Min<INT>( Info->DrawScale, 3 );
	//UBOOL ForceAlphaMap =  GIsEditor || Info->bActorShadows;

	RenderPasses.Empty();
	TArray<INT> PassLayers;
	for( INT i=0; i<SectorLayers.Num(); i++ )
	{
		// non-texture or alphatexture: render with alphamap
		UTexture* LayerTexture = Cast<UTexture>(Info->Layers[SectorLayers(i)].Texture);
			//!!vogel: OPENGL TODO
		if( ForceAlphaMap || !LayerTexture || LayerTexture->IsTransparent() )
		{
			PassLayers.AddItem( SectorLayers(i) );

			FTerrainSectorRenderPass* Pass = new(RenderPasses) FTerrainSectorRenderPass;
			Pass->RenderCombinationNum = Info->GetRenderCombinationNum(PassLayers, RM_AlphaMap);					
			PassLayers.Empty();
			// all subsequent layers must be rendered individually.
			ForceAlphaMap = 1;
			continue;
		}
	
		// Look for layers we can render simultaneously.
		while( i<SectorLayers.Num() )
		{
			// Fall out if we hit a layer which can't be rendered simultaneously.
			LayerTexture = Cast<UTexture>(Info->Layers[SectorLayers(i)].Texture);
			if( !LayerTexture || LayerTexture->IsTransparent() )
			{
				i--;
				break;
			}

			// Render this layer simultaneously.
			PassLayers.AddItem( SectorLayers(i) );
			if( PassLayers.Num()<MaxSimultaneousLayers )
				i++;
			else
				break;
		}

		// Render these layers simultaneously
		if( PassLayers.Num() )
		{
			FTerrainSectorRenderPass* Pass = new(RenderPasses) FTerrainSectorRenderPass;
			//jg -- We know we are using pixel shaders so only use weightmap for < 3 layers
			Pass->RenderCombinationNum = Info->GetRenderCombinationNum(PassLayers,
				PassLayers.Num() <= 2 ? RM_WeightMap : RM_CombinedWeightMap);
			PassLayers.Empty();
		}
	}
	// scion sz - shrink array
	RenderPasses.Shrink();
	
#if 0
	// Validate
	TArray<INT> CheckLayers;
	for( INT p=0;p<RenderPasses.Num();p++ )
	{
		FTerrainRenderCombination& RC = Info->RenderCombinations(RenderPasses(p).RenderCombinationNum);
		for( INT l=0;l<RC.Layers.Num();l++ )
		{
			// Check each layer is used only once.
			check( CheckLayers.FindItemIndex(RC.Layers(l)) == INDEX_NONE );
			CheckLayers.AddItem(RC.Layers(l));
		}
	}
	
	// Check each layer affecting the sector is rendered
	for( INT l=0;l<SectorLayers.Num();l++ )
		check( CheckLayers.FindItemIndex(SectorLayers(l)) != INDEX_NONE );

	// Check there are no extra layers
	check( CheckLayers.Num() == CheckLayers.Num() );
	// End validation
#endif

	// triangulate
	for( INT pass=0; pass<RenderPasses.Num(); pass++ )
	{
		// If layer is visible, build index buffer for it.
		RenderPasses(pass).Indices.Empty();
		RenderPasses(pass).NumTriangles = 0;

		for( INT y=0;y<QuadsY;y++ )
		{
			for( INT x=0;x<QuadsX;x++ )
			{				
				if( Info->GetQuadVisibilityBitmap( x+OffsetX, y+OffsetY ) )
				{					
					INT V1 = GetLocalVertex( x, y ) + VertexStreamOffset;
					INT V2 = V1+1;
					INT V3 = GetLocalVertex( x+1, y+1 ) + VertexStreamOffset;
					INT V4 = V3-1;
				
					if( Info->GetEdgeTurnBitmap( x+OffsetX, y+OffsetY ) )
					{
#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
						if( pass == 0 || PassShouldRenderTriangle( pass, x, y, 0, 1 ) ||
							PassShouldRenderTriangle( pass, x, y, 1, 1 ))
						{
							RenderPasses(pass).Indices.AddItem(V4);
							RenderPasses(pass).Indices.AddItem(V3);
							RenderPasses(pass).Indices.AddItem(V2);
							RenderPasses(pass).Indices.AddItem(V1);
							RenderPasses(pass).NumTriangles++;
						}
#else
						// triangle 1
						if( pass == 0 || PassShouldRenderTriangle( pass, x, y, 0, 1 ) )
						{
							RenderPasses(pass).Indices.AddItem(V1);
							RenderPasses(pass).Indices.AddItem(V4);
							RenderPasses(pass).Indices.AddItem(V2);
							RenderPasses(pass).NumTriangles++;
						}
						// triangle 2
						if( pass == 0 || PassShouldRenderTriangle( pass, x, y, 1, 1 ) )
						{
							RenderPasses(pass).Indices.AddItem(V4);
							RenderPasses(pass).Indices.AddItem(V3);
							RenderPasses(pass).Indices.AddItem(V2);
							RenderPasses(pass).NumTriangles++;
						}
#endif
					}
					else
					{
#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
						if( pass == 0 || PassShouldRenderTriangle( pass, x, y, 0, 0 ) ||
							PassShouldRenderTriangle( pass, x, y, 1, 0 ) )
						{
							RenderPasses(pass).Indices.AddItem(V1);
							RenderPasses(pass).Indices.AddItem(V4);
							RenderPasses(pass).Indices.AddItem(V3);
							RenderPasses(pass).Indices.AddItem(V2);
							RenderPasses(pass).NumTriangles++;
						}
#else
						// triangle 1
						if( pass == 0 || PassShouldRenderTriangle( pass, x, y, 0, 0 ) )
						{
							RenderPasses(pass).Indices.AddItem(V1);
							RenderPasses(pass).Indices.AddItem(V4);
							RenderPasses(pass).Indices.AddItem(V3);
							RenderPasses(pass).NumTriangles++;
						}
						// triangle 2
						if( pass == 0 || PassShouldRenderTriangle( pass, x, y, 1, 0 ) )
						{
							RenderPasses(pass).Indices.AddItem(V1);
							RenderPasses(pass).Indices.AddItem(V3);
							RenderPasses(pass).Indices.AddItem(V2);
							RenderPasses(pass).NumTriangles++;
						}
#endif
					}
				}
			}
		}

		RenderPasses(pass).NumIndices = RenderPasses(pass).Indices.Num();
		if( RenderPasses(pass).NumIndices )
		{
			// Work out the min and max indices
			RenderPasses(pass).MinIndex = MAXINT;
			RenderPasses(pass).MaxIndex = 0;
			for( INT j=0;j<RenderPasses(pass).NumIndices;j++ )
			{
				if( RenderPasses(pass).Indices(j) < RenderPasses(pass).MinIndex )
					RenderPasses(pass).MinIndex = RenderPasses(pass).Indices(j);
				if( RenderPasses(pass).Indices(j) > RenderPasses(pass).MaxIndex )
					RenderPasses(pass).MaxIndex = RenderPasses(pass).Indices(j);
			}
		}
		else
		{
			// Remove this pass if there are no triangles to render.
			RenderPasses.Remove(pass--);
		}
	}

	// Complete sector index buffer for rendering editor tools and wireframe.
	CompleteIndexBuffer.Indices.Empty();
	CompleteNumTriangles = 0;
#ifndef _XBOX
	//scion jg -- Don't rebuild these on the xbox since they take up space
	for( INT y=0;y<QuadsY;y++ )
	{
		for( INT x=0;x<QuadsX;x++ )
		{				
			if( Info->GetQuadVisibilityBitmap( x+OffsetX, y+OffsetY ) )
			{
			    INT V1 = GetLocalVertex( x, y ) + VertexStreamOffset;
			    INT V2 = V1+1;
			    INT V3 = GetLocalVertex( x+1, y+1 ) + VertexStreamOffset;
			    INT V4 = V3-1;
    
			    if( Info->GetEdgeTurnBitmap( x+OffsetX, y+OffsetY ) )
			    {
				    // triangle 1
				    CompleteIndexBuffer.Indices.AddItem(V1);
				    CompleteIndexBuffer.Indices.AddItem(V4);
				    CompleteIndexBuffer.Indices.AddItem(V2);
				    // triangle 2
				    CompleteIndexBuffer.Indices.AddItem(V4);
				    CompleteIndexBuffer.Indices.AddItem(V3);
				    CompleteIndexBuffer.Indices.AddItem(V2);
				    CompleteNumTriangles += 2;
			    }
			    else
			    {
				    // triangle 1
				    CompleteIndexBuffer.Indices.AddItem(V1);
				    CompleteIndexBuffer.Indices.AddItem(V4);
				    CompleteIndexBuffer.Indices.AddItem(V3);
				    // triangle 2
				    CompleteIndexBuffer.Indices.AddItem(V1);
				    CompleteIndexBuffer.Indices.AddItem(V3);
				    CompleteIndexBuffer.Indices.AddItem(V2);
				    CompleteNumTriangles += 2;
			    }
		    }
	    }
	}

	CompleteNumIndices = CompleteIndexBuffer.Indices.Num();
	CompleteMinIndex = MAXINT;
	CompleteMaxIndex = 0;
	for( INT j=0;j<CompleteNumIndices;j++ )
	{
		if( CompleteIndexBuffer.Indices(j) < CompleteMinIndex )
			CompleteMinIndex = CompleteIndexBuffer.Indices(j);
		if( CompleteIndexBuffer.Indices(j) > CompleteMaxIndex )
			CompleteMaxIndex = CompleteIndexBuffer.Indices(j);
	}
	CompleteIndexBuffer.Revision++;
#endif

	// Calculate the location/ center of the sector.
	FVector P1	= Info->Vertices(GetGlobalVertex(0,0));
	FVector P2	= Info->Vertices(GetGlobalVertex(0,QuadsY));
	FVector P3	= Info->Vertices(GetGlobalVertex(QuadsX,QuadsY));
	FVector P4	= Info->Vertices(GetGlobalVertex(QuadsX,0));

	Location	= P1 + (P2 - P1) / 2.f + (P4 - P1) / 2.f;
	Radius		= FDist( Location, P1 );

	// Update Decorations.
	INT SectorIndex;
	if ( Info->Sectors.FindItem( this, SectorIndex ) )
		Info->UpdateDecorations( SectorIndex );

	unguard;
}

void UTerrainSector::StaticLight( UBOOL Force )
{
	if( !Force )
	{
		UBOOL TerrainLightingChanged = 0;
		for( INT light=0; light<LightInfos.Num();light++ )
		{
			if( LightInfos(light).LightActor->bDynamicLight || LightInfos(light).LightActor->bLightChanged )
			{
				TerrainLightingChanged = 1;
				break;
			}
		}

		if( !TerrainLightingChanged )
			return;
	}

	// Set ambient color
	AZoneInfo* ZoneActor = Info->Region.Zone;
	FPlane	AbmientColor = FGetHSV( ZoneActor->AmbientHue, ZoneActor->AmbientSaturation, ZoneActor->AmbientBrightness ) * ZoneActor->Level->Brightness * 0.5f;
	
	INT VertexStreamMax = (QuadsX+1)*(QuadsY+1)+VertexStreamOffset;
	FTerrainVertexStream& VertexStream = GetVertexStream();

	for(INT VertexIndex = VertexStreamOffset;VertexIndex < VertexStreamMax;VertexIndex++)
		VertexStream.Vertices(VertexIndex).Color =
			FColor((BYTE) Min<INT>(255*AbmientColor.X,255),(BYTE) Min<INT>(255*AbmientColor.Y,255),(BYTE) Min<INT>(255*AbmientColor.Z,255), 255);

	for( INT InfoIndex=0; InfoIndex<LightInfos.Num();InfoIndex++ )
	{
		TArray<BYTE>&	VisibilityBitmap = LightInfos(InfoIndex).VisibilityBitmap;
		INT				BitmapIndex = 0;
		BYTE			BitMask = 1;
		FDynamicLight Light( LightInfos(InfoIndex).LightActor );

		for(INT VertexIndex = VertexStreamOffset;VertexIndex < VertexStreamMax;VertexIndex++)
		{
			if(VisibilityBitmap(BitmapIndex) & BitMask)
			{				
				FTerrainVertex* Vertex = &VertexStream.Vertices(VertexIndex);
#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
				// scion sz - calc world position for vertex (same as UTerrainSector::GetVertexPos)
				UTerrainSectorBlock* Block = Info->SectorBlocks( BlockIndex );
				FPlane Pos( Vertex->OffsetXY[0], Vertex->OffsetXY[1], Vertex->Height, 1.0f );
				FVector Position = Block->WorldMat.TransformFPlane( Pos );
#else
				FVector Position = Vertex->Position;
#endif
				VertexStream.Vertices(VertexIndex).Color += FColor(Light.Color * Light.SampleIntensity(Position, Vertex->Normal) * 0.5f);
			}

			BitMask <<= 1;

			if(!BitMask)
			{
				BitmapIndex++;
				BitMask = 1;
			}
		}
	}
	VertexStream.Revision++;

	for( INT y=0;y<=QuadsY;y++ )
		for( INT x=0;x<=QuadsX;x++ )
			Info->VertexColors(GetGlobalVertex(x,y)) = VertexStream.Vertices(GetLocalVertex(x,y)+VertexStreamOffset).Color;
}

static inline INT CheckVertexForProjector( const FVector& V, FPlane* FrustumPlanes )
{
	INT Result=0;
	if( FrustumPlanes[0].PlaneDot(V) <= 0 )
		Result |= 1;
	if( FrustumPlanes[1].PlaneDot(V) <= 0 )
		Result |= 2;
	if( FrustumPlanes[2].PlaneDot(V) <= 0 )
		Result |= 4;
	if( FrustumPlanes[3].PlaneDot(V) <= 0 )
		Result |= 8;
	if( FrustumPlanes[4].PlaneDot(V) <= 0 )
		Result |= 16;
	if( FrustumPlanes[5].PlaneDot(V) <= 0 )
		Result |= 32;
	return Result;
}

// Projectors.
void UTerrainSector::AttachProjector(AProjector* Projector,FProjectorRenderInfo* RenderInfo,INT MinQuadX,INT MinQuadY,INT MaxQuadX,INT MaxQuadY)
{
	guard(UTerrainSector::AttachProjector);

	Info->CheckComputeDataOnLoad();

	// Remove any old projectors.

	for(INT ProjectorIndex = 0;ProjectorIndex < Projectors.Num();ProjectorIndex++)
	{
		if(!Projectors(ProjectorIndex)->RenderInfo->Render(Projector->Level->TimeSeconds))
		{
			Projectors(ProjectorIndex) = Projectors(ProjectorIndex)->RemoveRef();
			Projectors.Remove(ProjectorIndex--);
		}
	}

	// Create the projector with the vertices for MinQuadX,MinQuadY -> MaxQuadX,MaxQuadY.

	FStaticProjectorInfo*	ProjectorInfo = NEW FStaticProjectorInfo;
#if DYNAMIC_PROJ_STREAM
	FVector					WorldDirection = RenderInfo->Projector->Rotation.Vector();
	FLOAT					InvMaxTraceDistance = 1.0f / RenderInfo->Projector->MaxTraceDistance;
#endif

	for(INT Y = MinQuadY;Y <= MaxQuadY + 1;Y++)
	{
		for(INT X = MinQuadX;X <= MaxQuadX + 1;X++)
		{
			FStaticProjectorVertex*	DestVertex = new(ProjectorInfo->Vertices) FStaticProjectorVertex;
			
			// scion sz - get vertex pos from stream
			DestVertex->WorldPosition = GetVertexPos( X, Y );
			// scion sz - use normal from vertex stream
			INT Index = GetLocalVertex(X,Y) + VertexStreamOffset;
			// scion sz - store normals
			DestVertex->Normal = GetVertexStream().Vertices(Index).Normal;

#if DYNAMIC_PROJ_STREAM
			FLOAT	DirectionalAttenuation = 1.0f;
			FVector Normal = DestVertex->Normal;
			if(!(RenderInfo->ProjectorFlags & PRF_ProjectOnBackfaces))
			{				
				DirectionalAttenuation = Max( Normal | -WorldDirection,0.0f);
			}

			FLOAT	DistanceAttenuation = 1.0f;

			if(RenderInfo->ProjectorFlags & PRF_Gradient)
				DistanceAttenuation = Clamp(1.0f - ((DestVertex->WorldPosition - RenderInfo->Projector->Location) | WorldDirection) * InvMaxTraceDistance,0.0f,1.0f);

			DestVertex->Attenuation = DirectionalAttenuation * DistanceAttenuation;
			//scion jg -- Calculate the projected position only once
			DestVertex->ProjectedPosition = RenderInfo->Matrix.TransformFVector(
				DestVertex->WorldPosition);
#endif
		}
	}

	// Work out which quads this projector intersects.

	for(INT Y = MinQuadY;Y <= MaxQuadY;Y++)
	{
		for(INT X = MinQuadX;X <= MaxQuadX;X++)
		{
			//scion jg -- Unprog bugfix
			if( !Info->GetQuadVisibilityBitmap( X+OffsetX, Y+OffsetY ) )
				continue;

			// scion sz - get world position from vertex stream
			FVector V1 = GetVertexPos(X,Y);
			FVector V2 = GetVertexPos(X+1,Y);
			FVector V3 = GetVertexPos(X+1,Y+1);
			FVector V4 = GetVertexPos(X,Y+1);

			INT O1 = CheckVertexForProjector( V1, Projector->FrustumPlanes );
			INT O2 = CheckVertexForProjector( V2, Projector->FrustumPlanes );
			INT O3 = CheckVertexForProjector( V3, Projector->FrustumPlanes );
			INT O4 = CheckVertexForProjector( V4, Projector->FrustumPlanes );
		
			UBOOL Turned = Info->GetEdgeTurnBitmap( X+OffsetX, Y+OffsetY );

			if( Turned )
			{
				// 142
				for( INT i=0;i<6;i++ )
					if( O1&(1<<i) && O4&(1<<i) && O2&(1<<i) )
						goto SkipTri1;
				ProjectorInfo->Indices.AddItem((X - MinQuadX + 0) + (Y - MinQuadY + 0) * (MaxQuadX - MinQuadX + 2));
				if( Info->Inverted )
				{
					ProjectorInfo->Indices.AddItem((X - MinQuadX + 1) + (Y - MinQuadY + 0) * (MaxQuadX - MinQuadX + 2));
					ProjectorInfo->Indices.AddItem((X - MinQuadX + 0) + (Y - MinQuadY + 1) * (MaxQuadX - MinQuadX + 2));
				}
				else
				{
					ProjectorInfo->Indices.AddItem((X - MinQuadX + 0) + (Y - MinQuadY + 1) * (MaxQuadX - MinQuadX + 2));
					ProjectorInfo->Indices.AddItem((X - MinQuadX + 1) + (Y - MinQuadY + 0) * (MaxQuadX - MinQuadX + 2));
				}
			}
			else
			{
				// 143
				for( INT i=0;i<6;i++ )
					if( O1&(1<<i) && O4&(1<<i) && O3&(1<<i) )
						goto SkipTri1;
				ProjectorInfo->Indices.AddItem((X - MinQuadX + 0) + (Y - MinQuadY + 0) * (MaxQuadX - MinQuadX + 2));
				if( Info->Inverted )
				{
					ProjectorInfo->Indices.AddItem((X - MinQuadX + 1) + (Y - MinQuadY + 1) * (MaxQuadX - MinQuadX + 2));
					ProjectorInfo->Indices.AddItem((X - MinQuadX + 0) + (Y - MinQuadY + 1) * (MaxQuadX - MinQuadX + 2));
				}
				else
				{
					ProjectorInfo->Indices.AddItem((X - MinQuadX + 0) + (Y - MinQuadY + 1) * (MaxQuadX - MinQuadX + 2));
					ProjectorInfo->Indices.AddItem((X - MinQuadX + 1) + (Y - MinQuadY + 1) * (MaxQuadX - MinQuadX + 2));
				}
			}
			
SkipTri1:;
			if( Turned )
			{
				// 432
				for( INT i=0;i<6;i++ )
					if( O4&(1<<i) && O3&(1<<i) && O2&(1<<i) )
						goto SkipTri2;
				ProjectorInfo->Indices.AddItem((X - MinQuadX + 0) + (Y - MinQuadY + 1) * (MaxQuadX - MinQuadX + 2));
				if( Info->Inverted )
				{
					ProjectorInfo->Indices.AddItem((X - MinQuadX + 1) + (Y - MinQuadY + 0) * (MaxQuadX - MinQuadX + 2));
					ProjectorInfo->Indices.AddItem((X - MinQuadX + 1) + (Y - MinQuadY + 1) * (MaxQuadX - MinQuadX + 2));
				}
				else
				{
					ProjectorInfo->Indices.AddItem((X - MinQuadX + 1) + (Y - MinQuadY + 1) * (MaxQuadX - MinQuadX + 2));
					ProjectorInfo->Indices.AddItem((X - MinQuadX + 1) + (Y - MinQuadY + 0) * (MaxQuadX - MinQuadX + 2));
				}
			}
			else
			{
				// 132
				for( INT i=0;i<6;i++ )
					if( O1&(1<<i) && O3&(1<<i) && O2&(1<<i) )
						goto SkipTri2;
				ProjectorInfo->Indices.AddItem((X - MinQuadX + 0) + (Y - MinQuadY + 0) * (MaxQuadX - MinQuadX + 2));
				if( Info->Inverted )
				{
					ProjectorInfo->Indices.AddItem((X - MinQuadX + 1) + (Y - MinQuadY + 0) * (MaxQuadX - MinQuadX + 2));
					ProjectorInfo->Indices.AddItem((X - MinQuadX + 1) + (Y - MinQuadY + 1) * (MaxQuadX - MinQuadX + 2));
				}
				else
				{
					ProjectorInfo->Indices.AddItem((X - MinQuadX + 1) + (Y - MinQuadY + 1) * (MaxQuadX - MinQuadX + 2));
					ProjectorInfo->Indices.AddItem((X - MinQuadX + 1) + (Y - MinQuadY + 0) * (MaxQuadX - MinQuadX + 2));
				}
			}
SkipTri2:;
		}
	}

	if(ProjectorInfo->Indices.Num())
	{
		ProjectorInfo->BaseMaterial = NULL;
		ProjectorInfo->TwoSided = 0;
		ProjectorInfo->RenderInfo = RenderInfo->AddReference();
		Projectors.AddItem(ProjectorInfo);
	}
	else
		ProjectorInfo = ProjectorInfo->RemoveRef();

	unguard;
}

IMPLEMENT_CLASS(UTerrainSector);

/*------------------------------------------------------------------------------
    UTerrainPrimitive Implementation.
------------------------------------------------------------------------------*/

UBOOL UTerrainPrimitive::LineCheck(FCheckResult &Result,AActor* Owner, AActor* Source, const FVector& End,const FVector& Start,const FVector& Extent,DWORD ExtraNodeFlags,DWORD TraceFlags)
{
	guard(UTerrainPrimitive::LineCheck);
	check(Info==Owner);
	return Info->LineCheck( Result, End, Start, Extent, TraceFlags, 0 );
	unguard;
}

UBOOL UTerrainPrimitive::PointCheck(FCheckResult &Result,AActor* Owner,const FVector& Point,const FVector& Extent,DWORD ExtraNodeFlags)
{
	guard(UTerrainPrimitive::LineCheck);
	check(Info==Owner);
	return Info->PointCheck( Result, Point, Extent, 0 );
	unguard;
}

FBox UTerrainPrimitive::GetRenderBoundingBox( const AActor* Owner, UBOOL Exact )
{
	guard(UTerrainPrimitive::GetRenderBoundingBox);
	return FBox(Owner->Location,Owner->Location);
	unguard;
}

void UTerrainPrimitive::Illuminate(AActor* Owner,UBOOL ChangedOnly)
{
	guard(UTerrainPrimitive::Illuminate);

#if !defined(_XBOX) && !defined(XBOX_ASSET_FORMAT)
	Info->CheckComputeDataOnLoad();

	// If we're only rebuilding visible actors and this terrains owner is hidden, leave.
	FRebuildOptions* RO = GRebuildTools.GetCurrent();
	if( (RO->Options&REBUILD_OnlyVisible) && Owner->IsHiddenEd() )
		return;

	GWarn->BeginSlowTask(TEXT("Raytracing Terrain"),1);

	TArray<AActor*>	Lights;

	for(INT ActorIndex = 0;ActorIndex < Owner->XLevel->Actors.Num();ActorIndex++)
	{
		AActor*	LightActor = Owner->XLevel->Actors(ActorIndex);
        if( LightActor && LightActor->LightType==LT_Steady && LightActor->bStatic &&
			LightActor->bNoDelete &&
			(LightActor->Region.ZoneNumber==Info->Region.ZoneNumber || Cast<ASkyZoneInfo>(LightActor->Region.Zone)) &&
			LightActor->bSpecialLit==Info->bSpecialLit &&
			//scion jg -- Reject skeletal only lights
			LightActor->AffectType != AT_AffectSkeletal)
		{
			if(!ChangedOnly || LightActor->bLightChanged)
				Lights.AddItem(LightActor);
		}
	}

	// Get the zone number each terrrain vertex is in.
	TArray<UBOOL> VertexZones( Info->Vertices.Num() );
	for( INT i=0;i<Info->Vertices.Num();i++ )
		VertexZones(i) = Info->Level->XLevel->Model->PointRegion(Info->Level,Info->Vertices(i)).ZoneNumber == Info->Region.ZoneNumber;

	for( INT i=0;i<Info->Sectors.Num();i++ )
	{
		GWarn->StatusUpdatef(i,Info->Sectors.Num(),TEXT("Raytracing Terrain"));
		UTerrainSector* S = Info->Sectors(i);
		FSphere			BoundingSphere = FSphere(&S->BoundingBox.GetExtrema(0), 2);

		if(!ChangedOnly)
			S->LightInfos.Empty();

		// Setup LightInfos
		TArray<INT>	SectorLights;
		for( INT LightIndex=0; LightIndex<Lights.Num(); LightIndex++ )
		{
			AActor*	LightActor = Lights(LightIndex);

			if(LightActor->LightEffect == LE_Sunlight || ((LightActor->Location - BoundingSphere).SizeSquared() < Square(LightActor->WorldLightRadius() + BoundingSphere.W)))
			{
				FTerrainSectorLightInfo*	LightInfo = NULL;

				// Try to find the LightInfos the light already hits, if we're updating only changed lights.
				if(ChangedOnly)
				{
					for(INT ExistingLightIndex = 0;ExistingLightIndex < S->LightInfos.Num();ExistingLightIndex++)
						if(S->LightInfos(ExistingLightIndex).LightActor == LightActor)
							LightInfo = &S->LightInfos(ExistingLightIndex);
				}

				// If this light didn't 
				if(!LightInfo)
					LightInfo = new(S->LightInfos) FTerrainSectorLightInfo(LightActor);

				LightInfo->VisibilityBitmap.Empty();
				LightInfo->VisibilityBitmap.AddZeroed(((S->QuadsX+1)*(S->QuadsY+1) + 7) / 8);
				SectorLights.AddItem((INT)(LightInfo - &S->LightInfos(0)));
			}
		}

		if(SectorLights.Num())
		{
			INT		BitmapIndex = 0;
			BYTE	BitMask = 1;
			for( INT y=0; y<=S->QuadsY; y++ )
			{
				for( INT x=0; x<=S->QuadsX; x++ )
				{
					// Check if any neighboring vertices are in the correct zone.  If none are,
					// we don't need to raytrace this vertex.
					UBOOL VertexZoneValid = 0;
					for( INT yy=Max(0,y+S->OffsetY-1);yy<=Min(Info->HeightmapY-1,y+S->OffsetY+1);yy++ )
					{
						for( INT xx=Max(0,x+S->OffsetX-1);xx<=Min(Info->HeightmapX-1,x+S->OffsetX+1);xx++ )
						{
							if( VertexZones( Info->GetGlobalVertex( xx, yy ) ) )
							{
								VertexZoneValid = 1;
								goto CheckedZones;
							}
						}
					}
					CheckedZones:;

					// Check if any quads used by this are visible.  If none are, we can avoid 
					if( VertexZoneValid &&
						(
							x+S->OffsetX == 0 ||
							y+S->OffsetY == 0 ||
							x+S->OffsetX == Info->HeightmapX-1 ||
							y+S->OffsetY == Info->HeightmapY-1 ||
							Info->GetQuadVisibilityBitmap(x+S->OffsetX, y+S->OffsetY) ||
							Info->GetQuadVisibilityBitmap(x+S->OffsetX-1, y+S->OffsetY) ||
							Info->GetQuadVisibilityBitmap(x+S->OffsetX, y+S->OffsetY-1) ||
							Info->GetQuadVisibilityBitmap(x+S->OffsetX-1, y+S->OffsetY-1)
						)
					  )
					{	
						INT Index = S->GetLocalVertex(x,y) + S->VertexStreamOffset;
						FVector	SamplePoint = S->GetVertexStream().Vertices(Index).Position,
								SampleNormal = S->GetVertexStream().Vertices(Index).Normal;

						SamplePoint += SampleNormal;
						for(INT LightIndex = 0;LightIndex < SectorLights.Num();LightIndex++)
						{
							AActor* LightActor = S->LightInfos(SectorLights(LightIndex)).LightActor;
							FLOAT WorldLightRadiusSquared = LightActor->WorldLightRadius();
							WorldLightRadiusSquared *= WorldLightRadiusSquared;
							FVector LightPoint = LightActor->LightEffect==LE_Sunlight ?
												SamplePoint-WORLD_MAX*LightActor->Rotation.Vector() :
												LightActor->Location;

							FCheckResult Result;
							if(
								   (
									LightActor->LightEffect==LE_Sunlight || 
									(
									  (LightActor->Location-SamplePoint).SizeSquared() < WorldLightRadiusSquared && 
									  (SampleNormal|(LightActor->Location-SamplePoint)) > 0.0 
									)
								   ) 
								 && (!Info->bStaticLighting || Info->Level->XLevel->SingleLineCheck(Result, NULL, LightPoint, SamplePoint, TRACE_ShadowCast|TRACE_Level|TRACE_Actors ) )
							  )
								S->LightInfos(SectorLights(LightIndex)).VisibilityBitmap(BitmapIndex) |= BitMask;
						}
					}
					BitMask <<= 1;
					if(!BitMask)
					{
						BitmapIndex++;
						BitMask = 1;
					}
				}
			}

			for( INT LightIndex=0;LightIndex<S->LightInfos.Num();LightIndex++ )
			{
				for( INT j=0;j<S->LightInfos(LightIndex).VisibilityBitmap.Num(); j++ )
					if( S->LightInfos(LightIndex).VisibilityBitmap(j) )
						goto LightVisible;

				// Light is not visible to this sector, remove.
				S->LightInfos(LightIndex).VisibilityBitmap.Empty();
				S->LightInfos.Remove(LightIndex);
				LightIndex--;
		LightVisible:
				;
			}
		}
		S->StaticLight(1);
	}

	GWarn->EndSlowTask();
#endif //!CONSOLE
	unguard;
}

void UTerrainPrimitive::Serialize(FArchive& Ar)
{
	guard(UTerrainPrimitive::Serialize);
	Super::Serialize(Ar);
	Ar << Info;
	unguard;
}

IMPLEMENT_CLASS(UTerrainPrimitive);

/*------------------------------------------------------------------------------
    ATerrainInfo Implementation.
------------------------------------------------------------------------------*/

void ATerrainInfo::PrecomputeLayerWeights()
{
	guard(ATerrainInfo::PrecomputeLayerWeights);

	INT i;
	for( i=0;i<ARRAY_COUNT(Layers);i++ )
	{
		if( !Layers[i].Texture || !Layers[i].AlphaMap || Layers[i].Texture->IsTransparent() || !Cast<UTexture>(Layers[i].Texture) )
			break;

		if(
			!Layers[i].LayerWeightMap || 
			 Layers[i].LayerWeightMap->USize != Layers[i].AlphaMap->USize ||
			 Layers[i].LayerWeightMap->VSize != Layers[i].AlphaMap->VSize ||
			 Layers[i].LayerWeightMap->Format != TEXF_RGBA8
		  )
		{
			Layers[i].LayerWeightMap = ConstructObject<UTexture>( UTexture::StaticClass(), Level, NAME_None, RF_Public );
			Layers[i].LayerWeightMap->Format = TEXF_RGBA8;
			Layers[i].LayerWeightMap->Init( Layers[i].AlphaMap->USize, Layers[i].AlphaMap->VSize );
			Layers[i].LayerWeightMap->PostLoad();
			Layers[i].LayerWeightMap->LODSet = LODSET_None;
		}
	}

	INT NumLayers = i;

	if( !NumLayers )
		return;

	Layers[0].LayerWeightMap->Clear(FColor(255,255,255,255));
	for( i=1;i<NumLayers;i++ )
	{
		Layers[i].LayerWeightMap->ArithOp( Layers[i].AlphaMap, TAO_AssignAlpha );
		for( INT j=0;j<i;j++ )
			Layers[j].LayerWeightMap->ArithOp( Layers[i].AlphaMap, TAO_MultiplyOneMinusAlpha );
	}


	for( i=0;i<NumLayers;i++ )
		Layers[i].LayerWeightMap->CreateMips( 1, 1 );

	unguard;
}

INT ATerrainInfo::GetRenderCombinationNum( TArray<INT>& Layers, ETerrainRenderMethod RenderMethod )
{
	guard(ATerrainInfo::GetRenderCombination);

	for( INT c=0;c<RenderCombinations.Num();c++ )
	{
		if( RenderCombinations(c).RenderMethod == RenderMethod &&
			RenderCombinations(c).Layers.Num() == Layers.Num() )
		{
			for( INT l=0;l<Layers.Num();l++ )
			{
				if( Layers(l) != RenderCombinations(c).Layers(l) )
					goto TryNextCombination;
			}
			return c;
		}
TryNextCombination:;		
	}

	FTerrainRenderCombination* NewCombination = new(RenderCombinations) FTerrainRenderCombination;
	NewCombination->CombinedWeightMaps = NULL;
	NewCombination->Layers.Add( Layers.Num() );
	appMemcpy( &NewCombination->Layers(0), &Layers(0), Layers.Num() * sizeof(INT) );
	NewCombination->RenderMethod = RenderMethod;

	return RenderCombinations.Num()-1;
	unguard;
}

void ATerrainInfo::CombineLayerWeights()
{
	guard(ATerrainInfo::CombineLayerWeights);

	for( INT c=0;c<RenderCombinations.Num();c++ )
	{
		if( RenderCombinations(c).RenderMethod == RM_CombinedWeightMap )
		{
			RenderCombinations(c).CombinedWeightMaps = ConstructObject<UTexture>( UTexture::StaticClass(), Level, NAME_None, RF_Public );
			RenderCombinations(c).CombinedWeightMaps->Format = TEXF_RGBA8;
			RenderCombinations(c).CombinedWeightMaps->Init( Layers[RenderCombinations(c).Layers(0)].AlphaMap->USize, Layers[RenderCombinations(c).Layers(0)].AlphaMap->VSize ); //!!
			RenderCombinations(c).CombinedWeightMaps->PostLoad();
			RenderCombinations(c).CombinedWeightMaps->Clear(FColor(0,0,0,0));
			RenderCombinations(c).CombinedWeightMaps->LODSet = LODSET_None;

			switch( RenderCombinations(c).Layers.Num() )
			{
			case 5:
			case 4:
				RenderCombinations(c).CombinedWeightMaps->ArithOp( Layers[RenderCombinations(c).Layers(3)].LayerWeightMap, TAO_AssignLtoA );
			case 3:
				RenderCombinations(c).CombinedWeightMaps->ArithOp( Layers[RenderCombinations(c).Layers(2)].LayerWeightMap, TAO_AssignLtoB );
			case 2:
				RenderCombinations(c).CombinedWeightMaps->ArithOp( Layers[RenderCombinations(c).Layers(1)].LayerWeightMap, TAO_AssignLtoG );
			case 1:
				RenderCombinations(c).CombinedWeightMaps->ArithOp( Layers[RenderCombinations(c).Layers(0)].LayerWeightMap, TAO_AssignLtoR );
				break;
			default:
				appErrorf(TEXT("CombineLayerWeights: invalid number of passes %d"), RenderCombinations(c).Layers.Num()); 
			}
			RenderCombinations(c).CombinedWeightMaps->CreateMips( 1, 1 );
		}
	}

	unguard;
}

ATerrainInfo::ATerrainInfo()
{
	guard(ATerrainInfo::ATerrainInfo);
	CalcCoords();
	unguard;
}
void ATerrainInfo::PostEditChange()
{
	guard(ATerrainInfo::PostEditChange);
	Super::PostEditChange();
	Level->XLevel->UpdateTerrainArrays();
	DecoLayerData.Empty();
	RenderCombinations.Empty();	
	SetupSectors();	
	CalcCoords();
	Update(0.f);
	unguard;
}
void ATerrainInfo::Destroy()
{
	guard(ATerrainInfo::Destroy);
	if( Level && Level->XLevel )
		Level->XLevel->UpdateTerrainArrays();
	Super::Destroy();
	unguard;
}
void ATerrainInfo::Serialize(FArchive& Ar)
{
	guard(ATerrainInfo::Serialize);
	Super::Serialize(Ar);

	Ar	<< Sectors
		<< Vertices
		<< SectorsX << SectorsY
		<< FaceNormals;

	Ar	<< ToWorld
		<< ToHeightmap;

	Ar	<< HeightmapX 
		<< HeightmapY;

	// Set OldTerrainMap on load
	if( Ar.IsLoading() )
		OldTerrainMap = TerrainMap;

	Ar << VertexColors;

#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
	if(Ar.LicenseeVer() == XBOX_PACKAGE_FORMAT)
	{
		Ar << DecoLayerData
			<< Primitive
			<< RenderCombinations
			<< CollapsedLayers
			// scion sz - added some missing items
			<< QuadDomMaterialBitmap
			<< QuadVisibilityBitmap
			<< EdgeTurnBitmap
			<< SectorBlocks;
	}
	else
#endif
	// Stuff not saved to disk
	if( !Ar.IsSaving() && !Ar.IsLoading() )
	{
		Ar	<< SelectedVertices
			<< DecoLayerData
			<< ShowGrid
			<< OldTerrainMap
			<< Projectors
			<< Primitive
			<< PaintedColor
			<< RenderCombinations
			<< CollapsedLayers
			// scion sz - added some missing items
			<< QuadDomMaterialBitmap
			<< QuadVisibilityBitmap
			<< EdgeTurnBitmap
			<< SectorBlocks;
	}

	unguard;
}

void ATerrainInfo::CalcCoords()
{
	ToWorld = FCoords(	FVector( -Location.X/TerrainScale.X, -Location.Y/TerrainScale.Y, -256.f*Location.Z/TerrainScale.Z),
						FVector( TerrainScale.X,0,0), FVector(0,TerrainScale.Y,0), FVector(0,0,TerrainScale.Z/256.f) );
	if( TerrainMap )
		ToWorld /= FVector( HeightmapX/2, HeightmapY/2, 32767 );

	ToHeightmap = ToWorld.Inverse();
}

#if __STATIC_LINK ||  __LINUX__
BYTE ATerrainInfo::GetLayerAlpha( INT x, INT y, INT Layer, UTexture* InAlphaMap )
#else
inline BYTE ATerrainInfo::GetLayerAlpha( INT x, INT y, INT Layer, UTexture* InAlphaMap )
#endif
{
	guard(ATerrainInfo::GetLayerAlpha);
	UTexture* Texture;

	if( InAlphaMap ) 
		Texture = InAlphaMap;
	else
		Texture = (Layer == -1) ? TerrainMap : Layers[Layer].AlphaMap;

	if(!Texture)
		return 0;

	// -2 means don't scale
	if( Layer != -2 )
	{
		x = x * Texture->USize / HeightmapX;
		y = y * Texture->VSize / HeightmapY;
	}

	FStaticTexture StaticTexture( Texture );
	BYTE* Data = (BYTE*) StaticTexture.GetRawTextureData(0);

	BYTE ret = 0;
	if ( Data )
	{
		switch( Texture->Format )
		{
			case TEXF_L8:
				ret = Data[ x + y*Texture->USize ];
				break;
			case TEXF_P8:
				ret = Texture->Palette->Colors(Data[ x + y*Texture->USize ]).R;
				break;
			case TEXF_RGBA8:
				ret = ((FColor*)&Data[4*(x + y*Texture->USize)])->A;
				break;
		}
	}
	return ret;
	unguard;
}

#if __STATIC_LINK || __LINUX__
FColor ATerrainInfo::GetTextureColor( INT x, INT y, UTexture* Texture )
#else
inline FColor ATerrainInfo::GetTextureColor( INT x, INT y, UTexture* Texture )
#endif
{
	x = x * Texture->USize / HeightmapX;
	y = y * Texture->VSize / HeightmapY;

	FColor Color(0);

	FStaticTexture StaticTexture( Texture );
	BYTE* Data = (BYTE*) StaticTexture.GetRawTextureData(0);

	if ( Data )
	{
		switch( Texture->Format )
		{
			case TEXF_RGBA8:
				Color = *((FColor*)&Data[4*(x + y*Texture->USize)]);
				break;
		}
	}
	return Color;
}

#if __STATIC_LINK || __LINUX__
void ATerrainInfo::SetTextureColor( INT x, INT y, UTexture* Texture, FColor& Color )
#else
inline void ATerrainInfo::SetTextureColor( INT x, INT y, UTexture* Texture, FColor& Color )
#endif
{

	x = x * Texture->USize / HeightmapX;
	y = y * Texture->VSize / HeightmapY;

	// Retain the existing alpha value
	FColor SaveAlpha = GetTextureColor( x, y, Texture );
	Color.A = SaveAlpha.A;

	FStaticTexture StaticTexture( Texture );
	BYTE* Data = (BYTE*) StaticTexture.GetRawTextureData(0);

	if ( Data )
	{
		switch( Texture->Format )
		{
			case TEXF_RGBA8:
				*((FColor*)&Data[ 4*(x + y*Texture->USize)]) = Color;
				break;
		}
	}
}

#if __STATIC_LINK || __LINUX__
void ATerrainInfo::SetLayerAlpha( FLOAT x, FLOAT y, INT Layer, BYTE Alpha, UTexture* InAlphaMap )
#else
inline void ATerrainInfo::SetLayerAlpha( FLOAT x, FLOAT y, INT Layer, BYTE Alpha, UTexture* InAlphaMap )
#endif
{
	UTexture* Texture;

	if( InAlphaMap ) 
		Texture = InAlphaMap;
	else
		Texture = (Layer == -1) ? TerrainMap : Layers[Layer].AlphaMap;

	INT X = x * Texture->USize / HeightmapX;
	INT Y = y * Texture->VSize / HeightmapY;

	FStaticTexture StaticTexture( Texture );
	BYTE* Data = (BYTE*) StaticTexture.GetRawTextureData(0);

	if ( Data )
	{
		switch( Texture->Format )
		{
			case TEXF_L8:
				Data[ X + Y*Texture->USize ] = Alpha;
				break;
			case TEXF_P8:
				Data[ X + Y*Texture->USize ] = Alpha;
				break;
			case TEXF_RGBA8:
				((FColor*)&Data[ 4*(X + Y*Texture->USize)])->A = Alpha;
				break;
		}
	}
}

_WORD ATerrainInfo::GetHeightmap( INT x, INT y )
{
	switch( TerrainMap->Format )
	{
	case TEXF_G16:
		return *(_WORD *)(&TerrainMap->Mips(0).DataArray((x+y*TerrainMap->USize)*2)); 
	case TEXF_P8:
		return (_WORD)256 * (_WORD)TerrainMap->Mips(0).DataArray(x+y*TerrainMap->USize); 
	}
	return 0;
}

void ATerrainInfo::SetHeightmap( INT x, INT y, _WORD w )
{
	switch( TerrainMap->Format )
	{
	case TEXF_G16:
		*(_WORD *)(&TerrainMap->Mips(0).DataArray((x+y*TerrainMap->USize)*2)) = w;
		break;
	}
}

void ATerrainInfo::PostLoad()
{
	guard(ATerrainInfo::PostLoad);
	Super::PostLoad();

#if (defined(_XBOX) && !defined(XBOX_ASSET_FORMAT)) || (!defined(_XBOX) && defined(XBOX_ASSET_FORMAT)) || (!defined(_XBOX) && !defined(XBOX_ASSET_FORMAT))
	// remember that we've just loaded so we rebuild terrain data before we try to render.
	JustLoaded = 1;
	// scion sz -
	SetupSectorBlocks();
#endif

#if !defined(_XBOX) && !defined(XBOX_ASSET_FORMAT)
	// OLDVER: make sure the bitmaps are of the appropriate size
	INT BitmapSize = HeightmapX*HeightmapY/32;
	if( QuadVisibilityBitmap.Num() != BitmapSize )
	{
		QuadVisibilityBitmap.Empty();
		QuadVisibilityBitmap.Add( BitmapSize );		
		appMemset( &QuadVisibilityBitmap(0), 0xFF, BitmapSize * sizeof(DWORD) );
	}
	if( EdgeTurnBitmap.Num() != BitmapSize )
	{
		EdgeTurnBitmap.Empty();
		EdgeTurnBitmap.Add( BitmapSize );		
		appMemset( &EdgeTurnBitmap(0), 0, BitmapSize * sizeof(DWORD) );
	}

	// Generate DomMaterialBitmap at loadtime
	// Dont do this in editor. Don't think we'd need it, and it would change as you paint.
	if(!GIsEditor)
	{
		if(QuadDomMaterialBitmap.Num() != 0)
		{
			debugf(TEXT("DomMaterialBitmap not empty."));
			QuadDomMaterialBitmap.Empty();
		}
		QuadDomMaterialBitmap.AddZeroed( HeightmapX * HeightmapY );		

		int NumLayers=0;
		while(NumLayers < ARRAY_COUNT(Layers) && Layers[NumLayers].Texture && Layers[NumLayers].AlphaMap)
			NumLayers++;

		// For each quad - find dominant material.
		for(INT y=0; y<HeightmapY-1; y++)
		{
			for(INT x=0; x<HeightmapX-1; x++)
			{
				// To find dominant material, we need to build a 'temporary' weight-map.
				// (we might not have a stored one)
				BYTE W[4][32];
				W[0][0] = W[1][0] = W[2][0] = W[3][0] = 1;

				for( INT l=1; l<NumLayers; l++ )
				{
					// Work up layers. Each time, scaling weights of all layers below by 
					// this layers alpha.

					W[0][l] = GetLayerAlpha(x,   y,   l);
					W[1][l] = GetLayerAlpha(x+1, y,   l);
					W[2][l] = GetLayerAlpha(x,   y+1, l);
					W[3][l] = GetLayerAlpha(x+1, y+1, l);

					for(INT i=0; i<l; i++ )
					{
						W[0][i] = W[0][i] * (1 - W[0][l]);
						W[1][i] = W[1][i] * (1 - W[1][l]);
						W[2][i] = W[2][i] * (1 - W[2][l]);
						W[3][i] = W[3][i] * (1 - W[3][l]);
					}
				} 

				// Now we have weights for the corners of all layers at this location, 
				// find layer with max (dominant) overall weight.
				UMaterial* domMat = NULL;
				BYTE domMatIdx = 0;
				INT maxWeight = 0;		
				for( INT l=0; l<NumLayers; l++ )
				{
					INT quadWeight = W[0][l] + W[1][l] + W[2][l] + W[3][l];
					if(quadWeight > maxWeight)
					{
						domMatIdx = (BYTE)l;
						domMat = Layers[l].Texture;
						maxWeight = quadWeight;
					}
				}

				// scion sz - store index into Layers[]
				QuadDomMaterialBitmap(x + (y * HeightmapX)) = domMatIdx;
			}
		}

		// scion sz - shrink arrays
		QuadDomMaterialBitmap.Shrink();
		QuadVisibilityBitmap.Shrink();
		EdgeTurnBitmap.Shrink();
	}
#endif
	unguard;
}

FVector ATerrainInfo::GetVertexNormal( INT x, INT y )
{
	if( FaceNormals.Num() )
	{
		FVector N;
		INT v;

		v = GetGlobalVertex(x,y);
		N = FaceNormals(v).Normal1 + FaceNormals(v).Normal2;
		if( x > 0 )
			N += FaceNormals(v-1).Normal1 + FaceNormals(v-1).Normal2;
		if( y > 0 )
		{
			v = GetGlobalVertex(x,y-1);
			N += FaceNormals(v).Normal1 + FaceNormals(v).Normal2;
			if( x > 0 )
				N += FaceNormals(v-1).Normal1 + FaceNormals(v-1).Normal2;
		}
		return Inverted ? -N.SafeNormal() : N.SafeNormal();
	}
	else
	{
		// scion sz - use normal stored in vertex stream if no facenormals[]
		INT SectorIdx = GetSectorIndex( x, y );
		checkSlow( SectorIdx < Sectors.Num() );
		UTerrainSector* Sector = Sectors( SectorIdx );
		return Sector->GetVertexNormal( x - Sector->OffsetX, y - Sector->OffsetY );
	}
}

/*------------------------------------------------------------------------------
	ATerrainInfo terrain updating.
------------------------------------------------------------------------------*/

void ATerrainInfo::CheckComputeDataOnLoad()
{
	guard(ATerrainInfo::CheckComputeDataOnLoad);
#if (!defined(_XBOX) && defined(XBOX_ASSET_FORMAT)) || (defined(_XBOX) && !defined(XBOX_ASSET_FORMAT)) || (!defined(_XBOX) && !defined(XBOX_ASSET_FORMAT))
	if( JustLoaded )
	{
		// rebuild vertex buffers and triangles
		Update(0.f);
		JustLoaded = 0;
	}
#endif
	unguard;
}


void ATerrainInfo::SetupSectors()
{
	guard(ATerrainInfo::SetupSectors);

	// See if the terrainmap has changed size.
	if( TerrainMap )
	{
		UBOOL JustPasted = (HeightmapX == 0 && HeightmapY == 0);
		HeightmapX = TerrainMap->USize;
		HeightmapY = TerrainMap->VSize;
		SectorsX = HeightmapX / Clamp(TERRAINSECTORSIZE,2,256);
		SectorsY = HeightmapY / Clamp(TERRAINSECTORSIZE,2,256);

		if( JustPasted )
		{
			// Make sure T3D pasted bitmap arrays are expanded to the required size.
			INT BitmapSize = HeightmapX*HeightmapY/32;
			if( QuadVisibilityBitmap.Num() < BitmapSize )
			{
				INT OldNum = QuadVisibilityBitmap.Num();
				QuadVisibilityBitmap.Add( BitmapSize-OldNum );				
				appMemset( &QuadVisibilityBitmap(OldNum), 0xFF, (BitmapSize-OldNum) * sizeof(DWORD) );
			}
			if( EdgeTurnBitmap.Num() < BitmapSize )
			{
				INT OldNum = EdgeTurnBitmap.Num();
				EdgeTurnBitmap.Add( BitmapSize-OldNum );
				appMemset( &EdgeTurnBitmap(OldNum), 0, (BitmapSize-OldNum) * sizeof(DWORD) );
			}
		}
	}
	else
	{
		HeightmapX = HeightmapY = 0;
		SectorsX = SectorsY = 0;
	}

	if( Sectors.Num() == SectorsX*SectorsY )
		return;

	// If the heightmap has changed size, reinitialize the bitmaps.
	INT BitmapSize = HeightmapX*HeightmapY/32;
	if( QuadVisibilityBitmap.Num() != BitmapSize )
	{
		QuadVisibilityBitmap.Empty();
		QuadVisibilityBitmap.Add( BitmapSize );
		appMemset( &QuadVisibilityBitmap(0), 0xFF, BitmapSize * sizeof(DWORD) );
	}

	if( EdgeTurnBitmap.Num() != BitmapSize )
	{
		EdgeTurnBitmap.Empty();
		EdgeTurnBitmap.Add( BitmapSize );		
		appMemset( &EdgeTurnBitmap(0), 0, BitmapSize * sizeof(DWORD) );
	}

	Sectors.Empty();
	RenderCombinations.Empty();

	for( INT y=0;y<SectorsY;y++ )
		for( INT x=0;x<SectorsX;x++ )
			Sectors.AddItem( new(this) UTerrainSector	(	this, 
															x*TERRAINSECTORSIZE, y*TERRAINSECTORSIZE,
															x<SectorsX-1?TERRAINSECTORSIZE:TERRAINSECTORSIZE-1,
															y<SectorsY-1?TERRAINSECTORSIZE:TERRAINSECTORSIZE-1
														) );

	// scion sz - shrink arrays
	QuadVisibilityBitmap.Shrink();
	EdgeTurnBitmap.Shrink();

	// scion sz -
	// reinitialize sector blocks and vertex streams
	SetupSectorBlocks();

	unguard;
}

/* scion ======================================================================
 * ATerrainInfo::SetupSectorBlocks
 * Author: sz
 *
 * Create list of sector blocks needed for the entire terrain.  Insert sectors
 * into their corresponding blocks.
 * ============================================================================
 */
void ATerrainInfo::SetupSectorBlocks()
{
	guard(ATerrainInfo::SetupSectorBlocks);

	// make sure sectors have been setup first
	if( !Sectors.Num() )
		SetupSectors();

	// empty current sector blocks
	SectorBlocks.Empty();

	INT SectorBlockSize = GetSectorBlockSize();

	// add enough blocks to span the terrain 
	for( INT y=0; y < GetSectorBlockSpanX(); y++ )
	{
		for( INT x=0; x < GetSectorBlockSpanY(); x++ )
		{
			INT OffsetX = x * SectorBlockSize;
			INT OffsetY = y * SectorBlockSize;
			SectorBlocks.AddItem( new(this) UTerrainSectorBlock( this, OffsetX, OffsetY, SectorBlockSize, SectorBlockSize ) );
		}
	}	
	SectorBlocks.Shrink();

	unguard;
}

void ATerrainInfo::CalcLayerTexCoords()
{
	guard(ATerrainInfo::CalcLayerTexCoords);
	for( INT i=0;i<ARRAY_COUNT(Layers);i++ )
	{
		if( Layers[i].Texture && Layers[i].AlphaMap )
		{	
			//!!OLDVER Hack alpha in P8 textures
			if( Layers[i].AlphaMap->Format == TEXF_P8 )
				for( INT p=0;p<256;p++ )
					Layers[i].AlphaMap->Palette->Colors(p).A = Layers[i].AlphaMap->Palette->Colors(p).R;

			// Generate texture coordinates
			FCoords TexCoords = ( GMath.UnitCoords / FRotator( 0.f, Layers[i].TextureRotation, 0.f) );
			TexCoords *= ToHeightmap.Transpose();
			TexCoords.XAxis /= Layers[i].UScale;
			TexCoords.YAxis /= Layers[i].VScale;
			TexCoords.Origin += FVector( Layers[i].UPan*Layers[i].UScale, Layers[i].VPan*Layers[i].VScale, 0.f ).TransformVectorBy(ToWorld);
			switch( Layers[i].TextureMapAxis )
			{
			case TEXMAPAXIS_XY:
				break;
			case TEXMAPAXIS_XZ:
				Exchange( TexCoords.Origin.Y, TexCoords.Origin.Z );
				Exchange( TexCoords.Origin.X, TexCoords.Origin.Z );
				Exchange( TexCoords.XAxis.X, TexCoords.XAxis.Z );
				Exchange( TexCoords.YAxis.X, TexCoords.YAxis.Y );
				Exchange( TexCoords.ZAxis.Y, TexCoords.ZAxis.Z );
				break;
			case TEXMAPAXIS_YZ:
				Exchange( TexCoords.Origin.X, TexCoords.Origin.Z );
				Exchange( TexCoords.XAxis.X, TexCoords.XAxis.Z );
				Exchange( TexCoords.ZAxis.X, TexCoords.ZAxis.Z );
				break;
			}
			TexCoords = TexCoords * ( GMath.UnitCoords / Layers[i].LayerRotation );
			Layers[i].TextureMatrix = TexCoords.Matrix();
		}
	}
	unguard;
}

void ATerrainInfo::Update( FLOAT Time, INT StartX, INT StartY, INT EndX, INT EndY, UBOOL UpdateLighting )
{
	guard(ATerrainInfo::Update);
#if defined(_XBOX) && defined(XBOX_ASSET_FORMAT)
	check(0);
#endif

	if( EndX==0 )
		EndX = HeightmapX;
	if( EndY==0 )
		EndY = HeightmapY;

#if !defined(_XBOX) && !defined(XBOX_ASSET_FORMAT)
	if( !GIsEditor )
#endif
	{
		PrecomputeLayerWeights();
	}

	CalcLayerTexCoords();
	UpdateVertices(Time, StartX, StartY, EndX, EndY );
	UpdateTriangles( StartX, StartY, EndX, EndY, UpdateLighting );

#if !defined(_XBOX) && !defined(XBOX_ASSET_FORMAT)
	if( !GIsEditor )
#endif
	{
		CombineLayerWeights();
	}

// scion sz - not used on xbox
#if !defined(_XBOX) && !defined(XBOX_ASSET_FORMAT)
	//!!powervr_aaron: terrain blending without using D3DTA_TEMP requires blend weight to be in Alpha (RGB is not used); D3DFMT_A8 would be better
	for( INT i=0;i<ARRAY_COUNT(Layers);i++ )
	{
		if( Layers[i].LayerWeightMap )
		{
			Layers[i].LayerWeightMap->ArithOp( Layers[i].LayerWeightMap, TAO_AssignLtoA );
			Layers[i].LayerWeightMap->CreateMips( 1, 1 );
		}
	}
#endif

#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
	//scion jg -- Memory savings
	VertexColors.Empty();
	// scion sz - vertices are reused from the stream after pre-processing is done	
	Vertices.Empty();
	// scion sz - remove facenormals since they are not used after pre-processing
	FaceNormals.Empty();
#endif
#if defined(_XBOX) && defined(XBOX_ASSET_FORMAT)
	// scion sz - shrink streams
	for( INT sb=0; sb < SectorBlocks.Num(); sb++ )
	{
		UTerrainSectorBlock* Block = SectorBlocks(sb);
		Block->VertexStream.Vertices.Shrink();
		Block->VertexStream.Revision++;
	}
	// scion sz - don't remove weight maps if it is being used by any of
	// the render combinations for any of the sectors
	UBOOL bWeightMapNeeded=0;
	for( INT RenIdx=0; RenIdx < RenderCombinations.Num() && !bWeightMapNeeded; RenIdx++ )
	{
		FTerrainRenderCombination& RenderCombination = RenderCombinations(RenIdx);
		if( RenderCombination.RenderMethod == RM_WeightMap )
		{   
			bWeightMapNeeded=1;	
		}
	}
   	// remove terrain map
	for( INT MipIdx=0; MipIdx < TerrainMap->Mips.Num(); MipIdx++ )
	{
		TerrainMap->Mips(MipIdx).DataArray.Unload();
	}
	TArray<UTexture*> RemoveMap;
	TArray<UTexture*> KeepMap;
	for (INT Layer = 0;	Layer < 32; Layer++ )
	{
		// remove terrain alpha maps
		if( Layers[Layer].AlphaMap )
		{
			for( INT MipIdx=0; MipIdx < Layers[Layer].AlphaMap->Mips.Num(); MipIdx++ )
			{
				Layers[Layer].AlphaMap->Mips(MipIdx).DataArray.Unload();
			}			
		}

		// sicon sz - remove terrain weight map only if it is not in use by
		// any of the sectors rendering with <= 2 layers
		if( Layers[Layer].LayerWeightMap )
		{	
			if( !bWeightMapNeeded )
			{
				// add it to list for removal
				RemoveMap.AddUniqueItem( Layers[Layer].LayerWeightMap );
				// null entry
				Layers[Layer].LayerWeightMap = NULL;
			}
			else
			{
				// make sure it is not removed
				KeepMap.AddUniqueItem( Layers[Layer].LayerWeightMap );
			}
		}
	}
	// scion sz - remove deco layer ScaleMap, DensityMap, and WeightMap
	for (INT Layer = 0;	Layer < DecoLayers.Num(); Layer++ )
	{
		FDecorationLayer& DecoLayer = DecoLayers(Layer);
		if( DecoLayer.ScaleMap )
		{
			RemoveMap.AddUniqueItem( DecoLayer.ScaleMap );
			DecoLayer.ScaleMap = NULL;			
		}
		if( DecoLayer.DensityMap )
		{
			RemoveMap.AddUniqueItem( DecoLayer.DensityMap );
			DecoLayer.DensityMap = NULL;
		}
		if( DecoLayer.ColorMap )
		{
			RemoveMap.AddUniqueItem( DecoLayer.ColorMap );			
			DecoLayer.ColorMap = NULL;
		}
	}
	// actually delete entries
	for( INT RemoveIdx=0; RemoveIdx < RemoveMap.Num(); RemoveIdx++ )
	{
		UTexture* Texture = RemoveMap(RemoveIdx);
		// only remove the texture if it doesn't exist in the keeper list
		if( Texture &&
			KeepMap.FindItemIndex(Texture) == INDEX_NONE )
		{
			// unload mip data
			for( INT MipIdx=0; MipIdx < Texture->Mips.Num(); MipIdx++ )
			{
				Texture->Mips(MipIdx).DataArray.Unload();
			}
			// delete UTexture
			delete Texture;
		}
		RemoveMap(RemoveIdx) = NULL;
	}
	
#endif

	unguard;	
}

void ATerrainInfo::UpdateVertices( FLOAT Time, INT StartX, INT StartY, INT EndX, INT EndY )
{
	guard(ATerrainInfo::UpdateVertices);

	if( !TerrainMap )
		return;

	if( Vertices.Num() < HeightmapX*HeightmapY )
		Vertices.Add( HeightmapX*HeightmapY - Vertices.Num() );
	if( FaceNormals.Num() < HeightmapX*HeightmapY )
		FaceNormals.Add( HeightmapX*HeightmapY - FaceNormals.Num() );
	if( VertexColors.Num() < HeightmapX*HeightmapY )
	{
		VertexColors.Empty();
		VertexColors.Add( HeightmapX*HeightmapY );
		for( INT i=0;i<VertexColors.Num();i++ )
			VertexColors(i) = FColor(255,255,255,255);
	}

	// Calculate face normals.
	TerrainMap->Mips(0).DataArray.Load();
	for( INT y=StartY; y<EndY; y++ )
	{
		for( INT x=StartX; x<EndX; x++ )
		{
			Vertices(GetGlobalVertex(x, y)) = HeightmapToWorld( FVector(x, y, GetHeightmap(x, y)) );

			if( x>0 && y>0 )
			{
				if( GetEdgeTurnBitmap( x-1, y-1 ) )
				{
					// 124, 423
					FaceNormals(GetGlobalVertex(x-1, y-1)).Normal1 = FPlane( Vertices(GetGlobalVertex(x-1, y-1)), Vertices(GetGlobalVertex(x, y-1)), Vertices(GetGlobalVertex(x-1, y)) ).SafeNormal();
					FaceNormals(GetGlobalVertex(x-1, y-1)).Normal2 = FPlane( Vertices(GetGlobalVertex(x-1, y)), Vertices(GetGlobalVertex(x, y-1)), Vertices(GetGlobalVertex(x, y)) ).SafeNormal();
				}
				else
				{
					// 123, 134
					FaceNormals(GetGlobalVertex(x-1, y-1)).Normal1 = FPlane( Vertices(GetGlobalVertex(x-1, y-1)), Vertices(GetGlobalVertex(x, y-1)), Vertices(GetGlobalVertex(x, y)) ).SafeNormal();
					FaceNormals(GetGlobalVertex(x-1, y-1)).Normal2 = FPlane( Vertices(GetGlobalVertex(x-1, y-1)), Vertices(GetGlobalVertex(x, y)), Vertices(GetGlobalVertex(x-1, y)) ).SafeNormal();
				}
			}
		}
	}

	INT MaxLayer = 0;
	for( INT Layer=0;Layer<32&&Layers[Layer].Texture&&Layers[Layer].AlphaMap;Layer++ )
	{
		Layers[Layer].AlphaMap->Mips(0).DataArray.Load();
		MaxLayer++;
	}
	unguard;
}

void ATerrainInfo::UpdateTriangles( INT StartX, INT StartY, INT EndX, INT EndY, UBOOL UpdateLighting )
{
	guard(ATerrainInfo::UpdateTriangles);

	//!! make this faster
	for( INT i=0;i<Sectors.Num();i++ )
	{
//jg -- Skip these on the xbox and during conversion
#if !defined(_XBOX) && !defined(XBOX_ASSET_FORMAT)
		if( 
			Sectors(i)->OffsetX > EndX ||
			Sectors(i)->OffsetY > EndY ||
			Sectors(i)->OffsetX+Sectors(i)->QuadsX < StartX ||
			Sectors(i)->OffsetY+Sectors(i)->QuadsY < StartY 
		  )
			continue;

		if( UpdateLighting )
			Sectors(i)->StaticLight(1);
#endif
		Sectors(i)->GenerateTriangles();
	}

	unguard;
}

/*------------------------------------------------------------------------------
    Terrain	Collision
------------------------------------------------------------------------------*/

// GetTriNormal doesn't waste time calculating the FPlane.W, as done in FPlane's three-vector constructor.
static inline FVector GetTriNormal(FVector& A, FVector& B, FVector& C) 
{
	return( (( B-A)^(C-A)).SafeNormal() );
}

//scion jg -- Changed to pass by ref for FVectors
UBOOL ATerrainInfo::LineCheckWithQuad( INT X, INT Y, FCheckResult &Result,
	const FVector& End, const FVector& Start, const FVector& Extent,
	DWORD TraceFlags, UBOOL CheckInvisibleQuads )
{
	guard(ATerrainInfo::LineCheckWithQuad);

	if( bDeleteMe )
		return 0;

	if( !CheckInvisibleQuads && !GetQuadVisibilityBitmap( X, Y ) )
		return 0;

	UBOOL ret = 0;
	INT Flip = Inverted ? -1 : 1;
	UBOOL Turned = GetEdgeTurnBitmap( X, Y );

	FVector V1; 
	FVector V2; 
	FVector V3; 
	FVector V4;

	// scion sz - calc world vertex position for these indices
	V1 = GetVertexPos(X,Y);
	V2 = GetVertexPos(X+1,Y);
	V3 = GetVertexPos(X+1,Y+1);
	V4 = GetVertexPos(X,Y+1);

	FLOAT ZExt = Flip * Extent.Z;
	V1.Z +=ZExt;
	V2.Z +=ZExt;
	V3.Z +=ZExt;
	V4.Z +=ZExt;
	
	FVector StartEnd = End - Start;

	// Normal: 123
	// Turned: 124
	FVector N = Turned ?  (Inverted ? GetTriNormal( V1, V4, V2 ) : GetTriNormal( V1, V2, V4 ))
						: (Inverted ? GetTriNormal( V1, V3, V2 ) : GetTriNormal( V1, V2, V3 ));
	if( (N|StartEnd) < -0.0001f )
	{
		FVector Int = FLinePlaneIntersection( End, Start, FPlane(V1, N) );
		if( Result.Actor == NULL ||	((Int-Start).SizeSquared() < (Result.Location-Start).SizeSquared()) )  
		{
			FLOAT T = ((Int-Start|StartEnd)/(StartEnd|StartEnd));
			if( T >=0.f && T<=1.f )
			{		
				FVector& Vert3 = Turned ? V4 : V3;
				FVector NF = N*Flip;

				FPlane Plan1 = FPlane( V1, (V2-V1)^NF );
				FLOAT Dot1 = Plan1.PlaneDot(Int);				
				if( Dot1 <= Extent.X )
				{
					FPlane Plan2 =  FPlane( V2, (Vert3-V2)^NF );
					FLOAT Dot2 = Plan2.PlaneDot(Int);
					if( Dot2 <= Extent.X ) 
					{
						FPlane Plan3 = FPlane( Vert3, (V1-Vert3)^NF );
						FLOAT Dot3 = Plan3.PlaneDot(Int);
						if( Dot3 <= Extent.X ) 
				        {
					        FVector Dir = End - Start;
					        Dir.Normalize();
					        Result.Location = Int;
					        Result.Normal	= N;
					        FVector V       = End-Start;
					        Result.Time     = ((Result.Location-Start)|V)/(V|V);
					        Result.Time		= Clamp( Result.Time - 0.5f / V.Size(), 0.f, 1.f );
					        Result.Location	= Start + V * Result.Time;
        
					        if(TraceFlags & TRACE_Material)
						        Result.Material = GetQuadDomMaterialBitmap( X, Y );
					        else
						        Result.Material = NULL;
        
					        Result.Actor	= this;
					        if ( Owner )
						        Result.Normal = Result.Normal.TransformVectorBy(Owner->ToWorld());
					        ret = 1;
				        }
			        }
		        }
	        }
		}
	}

	// Normal: 134
	// Turned: 423
	N = Turned ?  (Inverted ? GetTriNormal( V4, V3, V2 ) : GetTriNormal( V4, V2, V3 ))
				: (Inverted ? GetTriNormal( V1, V4, V3 ) : GetTriNormal( V1, V3, V4 ));
	if( (N|StartEnd) < -0.0001f )
	{	
		FVector Int = FLinePlaneIntersection( End, Start, FPlane(V3, N) );
		if( Result.Actor == NULL ||
			((Int-Start).SizeSquared() < (Result.Location-Start).SizeSquared()) )  
		{
			FLOAT T = ((Int-Start|StartEnd)/(StartEnd|StartEnd));
			if( T >=0.f && T<=1.f )
			{
				FVector& Vert1 = Turned ? V4 : V1;				
				FVector& Vert3 = Turned ? V2 : V3;
				FVector& Vert4 = Turned ? V3 : V4;
				FVector NF = N*Flip;
				FLOAT Dot1 = FPlane( Vert1, (Vert3-Vert1)^NF ).PlaneDot(Int);
				if( Dot1 <= Extent.X ) 
				{
					FLOAT Dot2 = FPlane( Vert3, (Vert4-Vert3)^NF ).PlaneDot(Int);
					if( Dot2 <= Extent.X ) 
					{
						FLOAT Dot3 = FPlane( Vert4, (Vert1-Vert4)^NF ).PlaneDot(Int);
						if( Dot3 <= Extent.X )
						{
							FVector Dir = End - Start;
							Dir.Normalize();
							Result.Location = Int;
							Result.Normal	= N;
							FVector V       = End-Start;
							Result.Time     = ((Result.Location-Start)|V)/(V|V);
							Result.Time		= Clamp( Result.Time - 0.5f / V.Size(), 0.f, 1.f );
							Result.Location	= Start + V * Result.Time;

							if(TraceFlags & TRACE_Material)
								Result.Material = GetQuadDomMaterialBitmap( X, Y );
							else
								Result.Material = NULL;

							Result.Actor	= this;
							if ( Owner )
								Result.Normal = Result.Normal.TransformVectorBy(Owner->ToWorld());
							ret = 1;
						}
					}
				} 
			}
		}
	}

	return ret;
	unguard;
}
#if defined(_XBOX) 
#pragma ENABLE_OPTIMIZATION
#endif


// quick sector rejection.
#define SECTORREJECTFAILED( X, Y ) (!SectorRejectMap || SectorRejectFailed( this, SectorRejectMap, InStart, InEnd, WorldDirection, WorldOneOverDirection, X, Y ))
FORCEINLINE UBOOL SectorRejectFailed( ATerrainInfo* Info, BYTE* Map,const FVector& Start,const FVector& End,const FVector& Direction, FVector& InvDirection, INT X, INT Y )
{
	//scion jg -- Removed the appFloor since it is unnecessary and slow
	INT s = Y / Info->TerrainSectorSize * Info->SectorsX + X / Info->TerrainSectorSize;
	if( Map[s] == 0 )
	{
		if( FLineBoxIntersection( Info->Sectors(s)->BoundingBox, Start, End, Direction, InvDirection ) )
			Map[s] = 2;	// line hit the sector box, we have to check quads.
		else
			Map[s] = 1;	// line missed the sector box, we can reject the entire sector.
	}

	if( Map[s] == 1 )
		return 0;			// missed, we can reject.
	else
		return 1;			// hit, sector reject failed.
}

UBOOL ATerrainInfo::LineCheck( FCheckResult &Result,const FVector& InEnd,const FVector& InStart,const FVector& InExtent, DWORD TraceFlags, UBOOL CheckInvisibleQuads )
{
	guard(ATerrainInfo::LineCheck);
	if( !TerrainMap )
		return 1;

	FUNC_CLOCK(TerCollCyc,GStats.DWORDStats(GEngineStats.STATS_Terrain_CollisionCycles));

	CheckComputeDataOnLoad();

	FVector End(InEnd.TransformPointBy( ToHeightmap ));
	FVector Start(InStart.TransformPointBy( ToHeightmap ));
	FVector HeightmapExtent(InExtent.TransformVectorBy( ToHeightmap ));
	FVector TraceDir((End - Start).SafeNormal());
	FLOAT HeightmapExtentX(Abs(HeightmapExtent.X));

	FVector WorldDirection;
	FVector WorldOneOverDirection;

	FMemMark Mark(GMem);
	BYTE* SectorRejectMap;
	if( InExtent.IsZero() )
	{
		SectorRejectMap = new( GMem, SectorsX*SectorsY ) BYTE; 
		appMemzero( SectorRejectMap, SectorsX*SectorsY );		
		WorldDirection = InEnd - InStart;
		WorldOneOverDirection = FVector( 1.f/WorldDirection.X, 1.f/WorldDirection.Y, 1.f/WorldDirection.Z );
	}
	else
		SectorRejectMap = NULL;

	Start -= TraceDir*HeightmapExtentX;
	End   += TraceDir*HeightmapExtentX;

	// scion sz - to prevent implicit ftol
	INT		StartX = appTrunc(Start.X),
			StartY = appTrunc(Start.Y),
			EndX = appTrunc(End.X),
			EndY = appTrunc(End.Y);

	UBOOL Hit = 0;

	if( StartX < 0 && EndX < 0 )
		goto ReturnLineCheck;
	if( StartY < 0 && EndY < 0 )
		goto ReturnLineCheck;
	if( StartX > HeightmapX-2 && EndX > HeightmapX-2 )
		goto ReturnLineCheck;
	if( StartY > HeightmapY-2 && EndY > HeightmapY-2 )
		goto ReturnLineCheck;

	{
	Result.Actor = NULL;

	INT X1 = Clamp<INT>( StartX, 0, HeightmapX-2 );
	INT X2 = Clamp<INT>( EndX  , 0, HeightmapX-2 );

	if( X1 == X2 )
	{
		INT Y1 = Clamp<INT>( StartY, 0, HeightmapY-2 );
		INT Y2 = Clamp<INT>( EndY  , 0, HeightmapY-2 );
		if( Y1 == Y2 )
		{
			Hit = SECTORREJECTFAILED(X1, Y1) && LineCheckWithQuad( X1, Y1, Result, InEnd, InStart, InExtent, TraceFlags, CheckInvisibleQuads );
			goto ReturnLineCheck;
		}

		INT YDir = Y1 > Y2 ? -1 : 1;
		// X = mY + c
		FLOAT m = (End.X - Start.X) / (End.Y - Start.Y);
		FLOAT c = End.X - m*End.Y;
		INT HitY=0;
		for( INT y=Y1; y*YDir<=Y2*YDir; y+=YDir )
		{
			FLOAT fx1 = m*y + c;
			FLOAT fx2 = m*(y+1) + c;
			if( fx1 > fx2 )
				Exchange( fx1, fx2 );
			fx1 -= HeightmapExtentX;
			fx2 += HeightmapExtentX;
			INT x1 = Clamp<INT>( appTrunc(fx1), 0, HeightmapX-2 );
			INT x2 = Clamp<INT>( appTrunc(fx2), 0, HeightmapX-2 );
			for( INT x=x1;x<=x2;x++ )
			{
				if( SECTORREJECTFAILED(x, y) && LineCheckWithQuad( x, y, Result, InEnd, InStart, InExtent, TraceFlags, CheckInvisibleQuads ) )
				{
					if( !Hit )
						HitY = y;
					Hit = 1;
				}
			}
			if( Hit && Abs(HitY - y) > HeightmapExtentX )
				break;
		}
	}
	else
	{
		INT XDir = X1 > X2 ? -1 : 1;
		// Y = mX + c
		FLOAT m = (End.Y - Start.Y) / (End.X - Start.X);
		FLOAT c = End.Y - m*End.X;
		INT HitX=0;
		for( INT x=X1; x*XDir<=X2*XDir; x+=XDir )
		{
			FLOAT fy1 = m*x + c;
			FLOAT fy2 = m*(x+1) + c;
			if( fy1 > fy2 )
				Exchange( fy1, fy2 );
			fy1 -= HeightmapExtentX;
			fy2 += HeightmapExtentX;
			INT y1 = Clamp<INT>( appTrunc(fy1), 0, HeightmapY-2 );
			INT y2 = Clamp<INT>( appTrunc(fy2), 0, HeightmapY-2 );
			for( INT y=y1;y<=y2;y++ )
			{
				if( SECTORREJECTFAILED(x, y) && LineCheckWithQuad( x, y, Result, InEnd, InStart, InExtent, TraceFlags, CheckInvisibleQuads ) )
				{
					if( !Hit )
						HitX = x;
					Hit = 1;
				}
			}
			if( Hit && Abs(HitX - x) > HeightmapExtentX )
				break;
		}
	}
	}

ReturnLineCheck:
	Mark.Pop();
	return !Hit;
	unguard;
}

//
//	ATerrainInfo::PointCheck
//

UBOOL ATerrainInfo::PointCheck( FCheckResult &Result,const FVector& InPoint, const FVector& InExtent, UBOOL CheckInvisibleQuads )
{
	guard(ATerrainInfo::PointCheck);

	if(LineCheck(Result,InPoint - FVector(0,0,InExtent.Z),InPoint + FVector(0,0,InExtent.Z),FVector(InExtent.X,InExtent.Y,0),0,CheckInvisibleQuads)==0)
	{
		Result.Location.Z += InExtent.Z;
		return 0;
	}
	else
		return 1;

	unguard;
}

/*------------------------------------------------------------------------------
    Terrain	Editor stuff
------------------------------------------------------------------------------*/

// Finds the closest vertex on the terrain to "InLocation", and optionally puts the
// results in "InOutput", "InX" and "InY".  Returns FALSE if vertex cannot be found.
UBOOL ATerrainInfo::GetClosestVertex( FVector& InLocation, FVector* InOutput, INT* InX, INT* InY )
{
	FVector H = InLocation.TransformPointBy( ToHeightmap );
	INT X = appRound(H.X);
	INT Y = appRound(H.Y);

	if( InX )	*InX = X;
	if( InY )	*InY = Y;

	if( X<0 || Y<0 || X>=HeightmapX || Y>=HeightmapY )
		return 0;

	if( InOutput )
	{
		*InOutput = GetVertexPos( X, Y );
	}

	return 1;
}

UBOOL ATerrainInfo::SelectVertexX( INT InX, INT InY )
{
#ifndef CONSOLE
	INT i;
	for( i=0;i<SelectedVertices.Num();i++ )
		if( SelectedVertices(i).X==InX && SelectedVertices(i).Y==InY )
		{
			SelectedVertices.Remove(i);
			return 0;
		}

	i = SelectedVertices.Add();
	SelectedVertices(i).X = InX;
	SelectedVertices(i).Y = InY;
	SelectedVertices(i).Weight = 1.0f * (GTerrainTools.GetStrength()/100.f);
	SelectedVertices(i).OldHeight = GetHeightmap(InX,InY);
	SelectedVertices(i).Delta = 0;
#endif
    return 1;
}

UBOOL ATerrainInfo::SelectVertex( FVector Location )
{
	guard(ATerrainInfo::SelectVertex);
#ifndef CONSOLE
	INT X, Y;
	if( !GetClosestVertex( Location, NULL, &X, &Y ) )
		return 0;

	// Capture information about the first vertex clicked.  Some brushes need this info.
	if( GTerrainTools.bFirstClick )
	{
		GTerrainTools.RefHeight = GetHeightmap(X,Y);
		GTerrainTools.bFirstClick = 0;
	}

	if( !SelectVertexX( X, Y ) ) return 0;

	// Select mirrored vertices

	switch( GTerrainTools.GetMirrorAxis() )
	{
		case MIRRORAXIS_X:
			if( !SelectVertexX( TerrainMap->USize - X, Y ) ) return 0;
			break;

		case MIRRORAXIS_Y:
			if( !SelectVertexX( X, TerrainMap->VSize - Y ) ) return 0;
			break;

		case MIRRORAXIS_XY:
			if( !SelectVertexX( TerrainMap->USize - X, Y ) ) return 0;
			if( !SelectVertexX( X, TerrainMap->VSize - Y ) ) return 0;
			if( !SelectVertexX( TerrainMap->USize - X, TerrainMap->VSize - Y ) ) return 0;
			break;
	}

#endif
	return 1;
	unguard;
}

void ATerrainInfo::ConvertHeightmapFormat()
{
	guard(ATerrainInfo::ConvertHeightmapFormat);

	if(TerrainMap->Format != TEXF_P8)	return;

	TArray<BYTE> OldData(TerrainMap->USize*TerrainMap->VSize);
	appMemcpy( &OldData(0), &TerrainMap->Mips(0).DataArray(0), TerrainMap->USize*TerrainMap->VSize);
	TerrainMap->Palette = NULL;
	TerrainMap->Format	= TEXF_G16;
	TerrainMap->Init( TerrainMap->USize, TerrainMap->VSize );
	TerrainMap->bRealtimeChanged = 1;

	for( INT i = 0 ; i < OldData.Num() ; i++ )
		*(_WORD *)(&TerrainMap->Mips(0).DataArray((i*2))) = 256*OldData(i);

	unguard;
}

// Soft select vertices on the terrain.  Verts within the InnerRadius get a full weight associated with them.
void ATerrainInfo::SoftSelect( FLOAT InnerRadius, FLOAT OuterRadius )
{
	guard(ATerrainInfo::SoftSelect);
#ifndef CONSOLE
	// Reset deltas
	for( INT i=0;i<SelectedVertices.Num();i++ )
	{
		SelectedVertices(i).Delta = 0.f;
		SelectedVertices(i).OldHeight = GetHeightmap(SelectedVertices(i).X,SelectedVertices(i).Y);
	}

	FVector R = (Rotation.Vector() * OuterRadius).TransformVectorBy( ToHeightmap );
	INT Delta = (INT)(R.Size2D() + 1);;

	INT Num = SelectedVertices.Num();

	for( INT i=0;i<Num;i++ )
	{
		FVector Vertex = Vertices( GetGlobalVertex(SelectedVertices(i).X,SelectedVertices(i).Y) );

		INT MaxDist=0;
		for( INT y=Max<INT>(0,SelectedVertices(i).Y-Delta);y<=Min<INT>(SelectedVertices(i).Y+Delta,HeightmapY-1); y++ )
			for( INT x=Max<INT>(0,SelectedVertices(i).X-Delta);x<=Min<INT>(SelectedVertices(i).X+Delta,HeightmapX-1); x++ )
			{
				FLOAT Dist = (Vertex-Vertices(GetGlobalVertex(x,y))).Size2D();
				if( Dist<OuterRadius && Dist>MaxDist )
					MaxDist = (INT) Dist;
			}

		for( INT y=Max<INT>(0,SelectedVertices(i).Y-Delta);y<=Min<INT>(SelectedVertices(i).Y+Delta,HeightmapY-1); y++ )
		{
			for( INT x=Max<INT>(0,SelectedVertices(i).X-Delta);x<=Min<INT>(SelectedVertices(i).X+Delta,HeightmapX-1); x++ )
			{
				INT GlobalVertex = GetGlobalVertex(x,y);
				FLOAT Dist = (Vertex-Vertices(GlobalVertex)).Size2D();

				FLOAT NewDist = Dist - InnerRadius;
				INT NewMaxDist = (INT)(MaxDist - InnerRadius);
				if(NewDist > NewMaxDist)
					continue;

				FLOAT Weight;
				if( Dist > InnerRadius )
					Weight = SelectedVertices(i).Weight * (0.5+(0.5f*appCos(PI*(NewDist/NewMaxDist))));
				else
					Weight = 1.0f;

				Weight *= (GTerrainTools.GetStrength()/100.f);
							
				if( Weight > 0 )
				{
					UBOOL bFound = 0;
					for( INT j=0;j<SelectedVertices.Num();j++ )
					{
						if( SelectedVertices(j).X==x && 
							SelectedVertices(j).Y==y )
						{
							bFound = 1;
							if( SelectedVertices(j).Weight < Weight )
								SelectedVertices(j).Weight = Weight;
						}
					}
					if( !bFound )
					{
						INT idx = SelectedVertices.Add();
						SelectedVertices(idx).X = x;
						SelectedVertices(idx).Y = y;
						SelectedVertices(idx).Weight = Weight;
						SelectedVertices(idx).OldHeight = GetHeightmap(x,y);
						SelectedVertices(idx).Delta = 0;
					}
				}
			}
		}
	}
#endif
	unguard;
}
void ATerrainInfo::SoftDeselect()
{
	guard(ATerrainInfo::SoftDeselect);
	for( INT i=0;i<SelectedVertices.Num();i++ )
	{
		if( SelectedVertices(i).Weight != 1.f )
		{
			SelectedVertices.Remove(i);
			--i;
		}
	}
	unguard;
}
// Selects all the vertices on the heightmap which fall inside the specified box.
void ATerrainInfo::SelectVerticesInBox( FBox& InRange )
{
	guard(ATerrainInfo::SelectVerticesInBox);

	FVector Start, End;

	Start = InRange.Min.TransformPointBy( ToHeightmap );
	End = InRange.Max.TransformPointBy( ToHeightmap );

	SelectedVertices.Empty();

	for( INT y = (INT)Start.Y ; y <= (INT)End.Y ; y++ )
	{
		for( INT x = (INT)Start.X ; x <= (INT)End.X ; x++ )
		{
			INT i = SelectedVertices.Add();
			SelectedVertices(i).X = x;
			SelectedVertices(i).Y = y;
			SelectedVertices(i).Weight = 1.0f;
			SelectedVertices(i).OldHeight = GetHeightmap(x,y);
			SelectedVertices(i).Delta = 0;
		}
	}

	unguard;
}

void ATerrainInfo::MoveVertices( FLOAT Delta )
{
	guard(ATerrainInfo::MoveVertices);
	if( !SelectedVertices.Num() )
		return;

	for( INT i=0;i<SelectedVertices.Num();i++ )
	{
		SelectedVertices(i).Delta += Delta;

		INT NewHeight = Clamp<INT>( SelectedVertices(i).OldHeight - 8.f * SelectedVertices(i).Delta * SelectedVertices(i).Weight, 0, 65535 );

		SetHeightmap( SelectedVertices(i).X, SelectedVertices(i).Y, NewHeight );
	}

	UpdateFromSelectedVertices();

	unguard;
}
void ATerrainInfo::ResetMove()
{
	guard(ATerrainInfo::ResetMove);
	if( !SelectedVertices.Num() )
		return;
	for( INT i=0;i<SelectedVertices.Num();i++ )
	{
		SelectedVertices(i).Delta = 0;
		SetHeightmap( SelectedVertices(i).X, SelectedVertices(i).Y, SelectedVertices(i).OldHeight );
	}

	UpdateFromSelectedVertices();
	unguard;
}
FBox ATerrainInfo::GetSelectedVerticesBounds()
{
	guard(ATerrainInfo::GetSelectedVerticesBounds);

	FBox BBox( FVector(HeightmapX, HeightmapY, 0), FVector(-HeightmapX, -HeightmapY, 0) );

	for( INT i = 0 ; i < SelectedVertices.Num() ; i++ )
		BBox += FVector( SelectedVertices(i).X, SelectedVertices(i).Y, 0 );

	return BBox;

	unguard;

}
// Updates the terrain to reflect changes to the selected vertices.
void ATerrainInfo::UpdateFromSelectedVertices()
{
	guard(ATerrainInfo::UpdateFromSelectedVertices);

	FBox BBox = GetSelectedVerticesBounds();

	BBox.Min.X--;
	BBox.Min.Y--;
	BBox.Max.X++;
	BBox.Max.Y++;

	// Make sure we're contained within the heightmap
	BBox.Min.X = Max<FLOAT>( BBox.Min.X, 0.f );
	BBox.Min.Y = Max<FLOAT>( BBox.Min.Y, 0.f );
	BBox.Max.X = Min<FLOAT>( BBox.Max.X, HeightmapX );
	BBox.Max.Y = Min<FLOAT>( BBox.Max.Y, HeightmapY );

	Update(0.f, BBox.Min.X, BBox.Min.Y, BBox.Max.X, BBox.Max.Y, 1 );

	unguard;
}

IMPLEMENT_CLASS(ATerrainInfo);

// Draws a shape conforming circle at a 3D location on the terrain.
static inline void DrawCircleOnTerrainX( FRenderInterface* RI, ATerrainInfo* InTerrainInfo, FVector InLocation, INT InRadius, FColor InColor )
{
	guard(DrawCircleOnTerrain);

	// Compute a decent number of sides for the radius markers, based on the radii.
	#define SIDES_MAX	32
	INT Sides = InRadius/24;
	Sides = Min( Sides, SIDES_MAX );	// Stay between 8 and SIDES_MAX
	Sides = Max( Sides,  8 );

	FVector Points[SIDES_MAX];
	FVector Origin = FVector( InRadius, 0, 0);
	FRotator StepRotation( 0, 65536/Sides, 0 );
	FCheckResult Check;
	INT Flip = InTerrainInfo->Inverted ? -1 : 1;

	for( INT x = 0 ; x < Sides ; x++ )
	{
		Points[x] = Origin.TransformVectorBy( GMath.UnitCoords * (StepRotation * x)) + InLocation;
		Points[x].Z = HALF_WORLD_MAX/2;
		
		if( InTerrainInfo->LineCheck( Check, Points[x] + (FVector(0,0,-Flip)*WORLD_MAX), Points[x] - (FVector(0,0,-Flip)*WORLD_MAX), FVector(0,0,0), 0, 1 )==0 )
			Points[x] = Check.Location;
		else
			Points[x] = x > 0 ? Points[x-1] : InLocation;
	}

	FLineBatcher	LineBatcher(RI,0);

	for( INT x = 0 ; x < Sides ; x++ )
		LineBatcher.DrawLine( Points[x], Points[(x+1)%Sides], InColor );

	LineBatcher.Flush();

	unguard;
}

#ifndef CONSOLE
static inline void DrawCircleOnTerrain( FRenderInterface* RI, ATerrainInfo* InTerrainInfo, FVector& InLocation, INT InRadius, FColor InColor )
{
	guard(DrawCircleOnTerrain);

	DrawCircleOnTerrainX( RI, InTerrainInfo, InLocation, InRadius, InColor );

	FVector Mirror;
	Mirror = InLocation.TransformPointBy( InTerrainInfo->ToHeightmap );
	Mirror.X = InTerrainInfo->TerrainMap->USize - Mirror.X;
	Mirror.Y = InTerrainInfo->TerrainMap->VSize - Mirror.Y;
	Mirror = Mirror.TransformPointBy( InTerrainInfo->ToWorld );

	switch( GTerrainTools.GetMirrorAxis() )
	{
		case MIRRORAXIS_X:
			DrawCircleOnTerrainX( RI, InTerrainInfo, FVector( Mirror.X, InLocation.Y, InLocation.Z), InRadius, InColor );
			break;

		case MIRRORAXIS_Y:
			DrawCircleOnTerrainX( RI, InTerrainInfo, FVector( InLocation.X, Mirror.Y, InLocation.Z), InRadius, InColor );
			break;

		case MIRRORAXIS_XY:
			DrawCircleOnTerrainX( RI, InTerrainInfo, FVector( Mirror.X, InLocation.Y, InLocation.Z), InRadius, InColor );
			DrawCircleOnTerrainX( RI, InTerrainInfo, FVector( InLocation.X, Mirror.Y, InLocation.Z), InRadius, InColor );
			DrawCircleOnTerrainX( RI, InTerrainInfo, FVector( Mirror.X, Mirror.Y, InLocation.Z), InRadius, InColor );
			break;
	}

	unguard;
}
#endif

#ifndef CONSOLE
static void DrawSegmentedLine( FRenderInterface* RI, ATerrainInfo* InTerrainInfo, FVector InStart, FVector InEnd, FColor InColor )
{
	guard(DrawSegmentedLine);

	FLineBatcher	LineBatcher(RI,0);
	FVector			Start,
					End,
					Step;

	Start = InStart.TransformPointBy( InTerrainInfo->ToHeightmap );
	End = InEnd.TransformPointBy( InTerrainInfo->ToHeightmap );

	FBox Rect(0);
	Rect += Start;
	Rect += End;

	// Assumes that the line will run along either the X or Y axis.  Diagonal lines aren't supported.
	FVector LineStart, LineEnd;
	if( Rect.Max.X - Rect.Min.X )
	{
		LineStart = InTerrainInfo->Vertices( InTerrainInfo->GetGlobalVertex(Rect.Min.X, Rect.Min.Y ) );
		for( INT x = (INT)Rect.Min.X+1 ; x <= (INT)Rect.Max.X ; x++ )
		{
			LineEnd = InTerrainInfo->Vertices( InTerrainInfo->GetGlobalVertex(x, Rect.Min.Y ) );
			LineBatcher.DrawLine(LineStart,LineEnd,InColor);

			LineStart = LineEnd;
		}
	}
	else
	{
		LineStart = InTerrainInfo->Vertices( InTerrainInfo->GetGlobalVertex(Rect.Min.X, Rect.Min.Y ) );
		for( INT y = (INT)Rect.Min.Y+1 ; y <= (INT)Rect.Max.Y ; y++ )
		{
			LineEnd = InTerrainInfo->Vertices( InTerrainInfo->GetGlobalVertex(Rect.Min.X, y ) );
			LineBatcher.DrawLine(LineStart,LineEnd,InColor);

			LineStart = LineEnd;
		}
	}
	LineBatcher.Flush();

	unguard;
}

// Draws a shape conforming rectangle on the terrain.
static void DrawRectOnTerrain( FRenderInterface* RI, ATerrainInfo* InTerrainInfo, FBox InRect, FColor InColor )
{
	guard(DrawRectOnTerrain);

	DrawSegmentedLine( RI, InTerrainInfo, FVector( InRect.Min.X, InRect.Min.Y, 0), FVector( InRect.Min.X, InRect.Max.Y, 0 ), InColor );
	DrawSegmentedLine( RI, InTerrainInfo, FVector( InRect.Min.X, InRect.Max.Y, 0), FVector( InRect.Max.X, InRect.Max.Y, 0 ), InColor );
	DrawSegmentedLine( RI, InTerrainInfo, FVector( InRect.Max.X, InRect.Max.Y, 0), FVector( InRect.Max.X, InRect.Min.Y, 0 ), InColor );
	DrawSegmentedLine( RI, InTerrainInfo, FVector( InRect.Max.X, InRect.Min.Y, 0), FVector( InRect.Min.X, InRect.Min.Y, 0 ), InColor );

	unguard;
}
#endif

// Draws a vertex marker
static inline void DrawVertex( FSceneNode* SceneNode, FRenderInterface* RI, FVector& InLocation, FColor InColor )
{
	guard(DrawVertex);
#ifndef CONSOLE
	// Draw the pin.

	FLineBatcher(RI).DrawLine(InLocation,InLocation - FVector(0,0,32),InColor);

	// Draw the pinhead.

	FCanvasUtil	CanvasUtil(&SceneNode->Viewport->RenderTarget,RI);
	UTexture*	Tex = GTerrainTools.WhiteCircle;
	FPlane		P = SceneNode->Project(InLocation);
	FVector		C = CanvasUtil.ScreenToCanvas.TransformFVector(P);

	CanvasUtil.DrawTile(
			C.X - (Tex->USize/2),
			C.Y - (Tex->VSize/2),
			C.X + (Tex->USize/2),
			C.Y + (Tex->VSize/2),
			0,0,
			Tex->UClamp,Tex->VClamp,
			C.Z,
			Tex,
			//!!MAT   PF_Masked,
			InColor
			);
#endif
	unguard;
}


/*------------------------------------------------------------------------------
    Terrain Rendering.
------------------------------------------------------------------------------*/

IMPLEMENT_CLASS(UTerrainMaterial);


FTerrainRenderCombination* FTerrainSectorRenderPass::GetRenderCombination(ATerrainInfo* TerrainInfo)
{
	return &TerrainInfo->RenderCombinations(RenderCombinationNum);
}

// 
// FTerrainRenderThing
//
// - There is one of these for each combination of simultaneous layers and vertex stream.
// - A number of these will be rendered in the same DrawPrimitive call.
//
struct FTerrainRenderThing
{
	INT ForceSortOrder;
	INT NumLayers;
	FTerrainSectorRenderPass* RenderPass;
	FTerrainVertexStream* VertexStream;
	FLOAT Distance;
    FDynamicLight* DynamicLights[8];

	void Set( INT InForceSortOrder, FTerrainSectorRenderPass* InRenderPass, FTerrainVertexStream* InVertexStream, INT InNumLayers, FLOAT InDistance, FDynamicLight* InDynamicLights[8] )
	{
		ForceSortOrder	= InForceSortOrder;
		RenderPass		= InRenderPass;
		VertexStream	= InVertexStream;
		NumLayers		= InNumLayers;
		Distance		= InDistance;
        for( INT i=0; i<8; i++ )
            DynamicLights[i] = InDynamicLights[i];
	}

    UBOOL LightsMatch( FTerrainRenderThing& OtherThing )
    {
        for( INT i=0; i<8; i++ )
            if( DynamicLights[i] != OtherThing.DynamicLights[i] )
                return 0;
        return 1;
	}	
};

static INT Compare(FTerrainRenderThing& A,FTerrainRenderThing& B)
{
	// 1.  Sort by vertex stream
	if( A.VertexStream > B.VertexStream )
		return 1;
	else if( A.VertexStream < B.VertexStream )
		return -1;

	// 2.  Force order-dependent stuff
	if( A.ForceSortOrder != B.ForceSortOrder )
		return A.ForceSortOrder - B.ForceSortOrder;

	// 3.  Sort by number of simultaneous layers
	if( A.NumLayers != B.NumLayers )
		return -1 * (A.NumLayers - B.NumLayers);

	// 4. Sort by RenderCombination
	if( A.RenderPass->RenderCombinationNum > B.RenderPass->RenderCombinationNum )
		return 1;
	else
	if( A.RenderPass->RenderCombinationNum < B.RenderPass->RenderCombinationNum )
		return -1;

	// 5. Sort by distance (front to back)
	return A.Distance - B.Distance;
}

//
// Dynamic index buffer for terrain rendering
//
class FTerrainIndexBuffer : public FIndexBuffer
{
public:
	FTerrainRenderThing**			RenderThings;  // List of things we're going to render simultaneously.
	INT								RenderThingsNum;
	INT								MinIndex;
	INT								MaxIndex;
	INT								NumTriangles;

	FTerrainIndexBuffer( INT MaxThings )
	{
		// Allocate memory for the RenderThings array.
		RenderThings = new(GMem, MaxThings) (FTerrainRenderThing*);
	}

	void Init()
	{
		RenderThingsNum = 0;
		MinIndex = MAXINT;
		MaxIndex = 0;
		NumTriangles = 0;
	}

	//
	// AddThing - Add another thing to render with the same DrawPrimitive call.
	//
	void AddThing( FTerrainRenderThing* Thing )
	{
		RenderThings[RenderThingsNum++] = Thing;
		MinIndex = Min<INT>( MinIndex, Thing->RenderPass->MinIndex );
		MaxIndex = Max<INT>( MaxIndex, Thing->RenderPass->MaxIndex );
		NumTriangles += Thing->RenderPass->NumTriangles;
	}

	virtual INT GetRevision()
	{
		return 1;
	}

	virtual INT GetSize()
	{
		INT total=0;
		for( INT i=0;i<RenderThingsNum;i++ )
			total += RenderThings[i]->RenderPass->Indices.Num();
		return total * sizeof(_WORD);
	}

	virtual INT GetIndexSize()
	{
		return sizeof(_WORD);
	}

	virtual void GetContents(void* Data)
	{
		_WORD* Ptr = (_WORD*)Data;
		//
		// Copy the indices of the things we're going to render simultaneously.
		//
		for( INT i=0;i<RenderThingsNum;i++ )
		{
			INT count = RenderThings[i]->RenderPass->Indices.Num();
			appMemcpy( Ptr, &RenderThings[i]->RenderPass->Indices(0), count * sizeof(_WORD) );
			Ptr += count;
		}
	}
};

//
// FTerrainWireframeIndexBuffer - used to render individual terrain layer wireframes.
//
class FTerrainWireframeIndexBuffer : public FIndexBuffer
{
	FTerrainSectorRenderPass*	RenderPass;

public:
	FTerrainWireframeIndexBuffer( FTerrainSectorRenderPass* InRenderPass )
	{
		RenderPass = InRenderPass;
	}

	virtual INT GetRevision()
	{
		return 1;
	}

	virtual INT GetSize()
	{
		return RenderPass->NumIndices * sizeof(_WORD);
	}

	virtual INT GetIndexSize()
	{
		return sizeof(_WORD);
	}

	virtual void GetContents(void* Data)
	{
		appMemcpy( Data, &RenderPass->Indices(0), RenderPass->NumIndices * sizeof(_WORD) );
	}
};

//
//	FDynamicTerrainProjector
//
class FDynamicTerrainProjector : public FIndexBuffer
{
public:

	UTerrainSector*				Sector;
	FProjectorRenderInfo*		ProjectorInfo;
	FDynamicTerrainProjector*	Next;
	INT							MinQuadX,
								MinQuadY,
								MaxQuadX,
								MaxQuadY;

	// Only valid after GetContents.

	INT	ClippedNumTriangles,
		MinIndex,
		MaxIndex;

	// Constructor.

	FDynamicTerrainProjector(UTerrainSector* InSector,FProjectorRenderInfo* InInfo,INT InMinX,INT InMinY,INT InMaxX,INT InMaxY,FDynamicTerrainProjector* InNext)
	{
		Sector = InSector;
		ProjectorInfo = InInfo;
		MinQuadX = InMinX;
		MinQuadY = InMinY;
		MaxQuadX = InMaxX;
		MaxQuadY = InMaxY;
		Next = InNext;
		ClippedNumTriangles = 0;
		MinIndex = MAXWORD;
		MaxIndex = 0;
	}

	// FRenderResource interface.
	virtual INT GetRevision() { return 1; }

	// FIndexBuffer interface.
	virtual INT GetSize() { return (MaxQuadX - MinQuadX + 1) * (MaxQuadY - MinQuadY + 1) * 2 * 3 * sizeof(_WORD); }
	virtual INT GetIndexSize() { return sizeof(_WORD); }
	virtual void GetContents(void* Data)
	{
        guard(FDynamicTerrainProjector::GetContents);
		_WORD*	DestIndex = (_WORD*)Data;

		for( INT y=MinQuadY;y<=MaxQuadY;y++ )
		{
			for( INT x=MinQuadX;x<=MaxQuadX;x++ )
			{
				// scion sz - unprog fix also needed here
				if( !Sector->Info->GetQuadVisibilityBitmap( x+Sector->OffsetX, y+Sector->OffsetY ) )
					continue;

				// index buffer vertex offsets
				INT v1 = Sector->GetLocalVertex(x,y) + Sector->VertexStreamOffset;
				INT v2 = v1+1;
				INT v3 = Sector->GetLocalVertex(x+1,y+1) + Sector->VertexStreamOffset;
				INT v4 = v3-1;				

				// scion sz - get world position from vertex stream
				FVector V1 = Sector->GetVertexPos(x,y);
				FVector V2 = Sector->GetVertexPos(x+1,y);
				FVector V3 = Sector->GetVertexPos(x+1,y+1);
				FVector V4 = Sector->GetVertexPos(x,y+1);

				INT O1 = CheckVertexForProjector( V1, ProjectorInfo->FrustumPlanes );
				INT O2 = CheckVertexForProjector( V2, ProjectorInfo->FrustumPlanes );
				INT O3 = CheckVertexForProjector( V3, ProjectorInfo->FrustumPlanes );
				INT O4 = CheckVertexForProjector( V4, ProjectorInfo->FrustumPlanes );
			
				UBOOL Turned = Sector->Info->GetEdgeTurnBitmap( x+Sector->OffsetX, y+Sector->OffsetY );

				if( Turned )
				{
					// 142
					for( INT i=0;i<6;i++ )
						if( O1&(1<<i) && O4&(1<<i) && O2&(1<<i) )
							goto SkipTri1;
					*DestIndex++ = v1;
					*DestIndex++ = v4;
					*DestIndex++ = v2;

					ClippedNumTriangles++;
					MinIndex = Min(MinIndex,v1);
					MaxIndex = Max(MaxIndex,v4);
				}
				else
				{
					// 143
					for( INT i=0;i<6;i++ )
						if( O1&(1<<i) && O4&(1<<i) && O3&(1<<i) )
							goto SkipTri1;
					*DestIndex++ = v1;
					*DestIndex++ = v4;
					*DestIndex++ = v3;

					ClippedNumTriangles++;
					MinIndex = Min(MinIndex,v1);
					MaxIndex = Max(MaxIndex,v3);
				}
				
	SkipTri1:;
				if( Turned )
				{
					// 432
					for( INT i=0;i<6;i++ )
						if( O4&(1<<i) && O3&(1<<i) && O2&(1<<i) )
							goto SkipTri2;
					*DestIndex++ = v4;
					*DestIndex++ = v3;
					*DestIndex++ = v2;

					ClippedNumTriangles++;
					MinIndex = Min(MinIndex,v2);
					MaxIndex = Max(MaxIndex,v3);
				}
				else
				{
					// 132
					for( INT i=0;i<6;i++ )
						if( O1&(1<<i) && O3&(1<<i) && O2&(1<<i) )
							goto SkipTri2;
					*DestIndex++ = v1;
					*DestIndex++ = v3;
					*DestIndex++ = v2;

					ClippedNumTriangles++;
					MinIndex = Min(MinIndex,v1);
					MaxIndex = Max(MaxIndex,v3);
				}
	SkipTri2:;
			}
		}
        unguard;
	}
	// scion sz - direct access to index buffer
	virtual void* GetContentsPtr(void)
	{
		void* RawIndices = NULL;		
		return RawIndices;
	}
};

#ifdef JG_PUSHBUFFER_INDICES
/* scion ======================================================================
 * EncodeIndicesIntoPushBuffer
 * Author: jg
 * 
 * Encodes the indices for a list of render things into a push buffer.
 *
 * input:	Things FTerrainRenderThing* The list of render things to build the
 *			indices into a push buffer for
 *			dwNumPerEncode DWORD Number of render things
 *			dwIndexCount DWORD& Out value for the number of indices
 *			pRI FRenderInterface* The render interface to draw with
 *
 * ============================================================================
 */
void EncodeIndicesIntoPushBuffer(FTerrainRenderThing** Things,
	DWORD dwNumPerEncode,DWORD& dwIndexCount,FRenderInterface* pRI)
{
	guard(EncodeIndicesAsPushBuffer);
	dwIndexCount = 0;
	// Loop through the render things and figure out how many indices we are
	// going to encode, so we can calculate how large of a pushbuffer we'll
	// need to use
	for (DWORD dwThing = 0; dwThing < dwNumPerEncode; dwThing++)
	{
		dwIndexCount += Things[dwThing]->RenderPass->Indices.Num();
	}
	// Better be a list of quads
	checkSlow(dwIndexCount % 4 == 0);
	// Every so many indices we need to encode a new begin indices command.
	// We need 2 DWORDs for the primitive type and 1 for each begin indices
	// command and 2 DWORDs for the terminator
	DWORD dwHeaderSize = (dwIndexCount / MAX_INDICES_IN_PUSH) + 5;
	// Come up with a total for all indices and the header(s)
	DWORD dwNumBytes = dwIndexCount * sizeof(_WORD) +
		dwHeaderSize * sizeof(DWORD);
	// Get a pointer directly into the main push buffer
	DWORD* pdwBuff = (DWORD*)pRI->BeginPush(dwNumBytes);
	// Encode the start command & primitive type
	*pdwBuff++ = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END,1);
	*pdwBuff++ = PT_QuadList;
	// Used to keep track of how many indices encoded thus far
	DWORD dwEncodedCount = 0;
	// Size of the batch (MAX_INDICES_IN_PUSH or less)
	DWORD dwNumThisBatch = 0;
	// The current render thing having its indices copied
	DWORD dwCurrentThing = 0;
	// How many indices are left for this render thing
	DWORD dwThingIndicesLeft = Things[0]->RenderPass->Indices.Num();
	// Maintains the source pointer for copies
	DWORD* pdwIndices = (DWORD*)&Things[0]->RenderPass->Indices(0);
	// Now loop through the render things and add their indices in
	while (dwEncodedCount < dwIndexCount)
	{
		// Check to see if we need to encode a begin batch command
		if (dwEncodedCount % MAX_INDICES_IN_PUSH == 0)
		{
			// Figure out how many in this batch we are encoding
			dwNumThisBatch = Min<DWORD>((dwIndexCount - dwEncodedCount),
				MAX_INDICES_IN_PUSH);
			// Note that the count of indices is not the number of indices
			// but the number of DWORDs encoded
			*pdwBuff++ = D3DPUSH_ENCODE(
				D3DPUSH_NOINCREMENT_FLAG | D3DPUSH_DRAW_INDEX_BATCH,
				dwNumThisBatch / 2);
		}
		// If we are done with the indices for this render thing, update
		// the counts and pointers with the next render thing
		if (dwThingIndicesLeft == 0)
		{
			dwCurrentThing++;
			pdwIndices = (DWORD*)&Things[dwCurrentThing]->RenderPass->Indices(0);
			dwThingIndicesLeft = Things[dwCurrentThing]->RenderPass->Indices.Num();
		}
		// Figure out how many indices we can copy in one call
		DWORD dwCopy = Min<DWORD>(dwNumThisBatch,dwThingIndicesLeft);
		// Use a fast copy rather than hand encoding each one
		appMemcpy(pdwBuff,pdwIndices,dwCopy * sizeof(_WORD));
		// Increment pointer positions to reflect the new source/dest
		pdwBuff += dwCopy / sizeof(_WORD);
		pdwIndices += dwCopy / sizeof(_WORD);
		// Update the various counters
		dwEncodedCount += dwCopy;
		dwThingIndicesLeft -= dwCopy;
		dwNumThisBatch -= dwCopy;
	}
	// Encode the terminator info
	*pdwBuff++ = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END,1);
	*pdwBuff++ = 0;
	// Let go of the push buffer
	pRI->EndPush(pdwBuff);
	unguard;
}
#endif

//
// ATerrainInfo::Render - render terrain
//
void ATerrainInfo::Render(FLevelSceneNode* SceneNode,FRenderInterface* RI,FVisibilityInterface* VI, FDynamicLight** DynamicLights,INT NumDynamicLights,FProjectorRenderInfo** DynamicProjectors,INT NumDynamicProjectors)
{
	guard(ATerrainInfo::Render);

	if( !TerrainMap )
		return;

    TArray<AActor*> WorldDynLights;

				
	if( GIsEditor )
	{
		// Hide this terrain if the terraininfo actor is hidden.
		if( IsHiddenEd() )
			return;
	}

	CheckComputeDataOnLoad();

	RI->PushState();

	RI->SetTransform(TT_LocalToWorld,FMatrix::Identity);

	PUSH_HIT(SceneNode->Viewport,HTerrain(this));

#ifdef __PSX2_EE__
	for( INT s=0;s<Sectors.Num();s++ )
	{
		UTerrainSector* Sector = Sectors(SectorIndices[s]);
		for( INT passindex=0;passindex<Sector->RenderPasses.Num();passindex++ )
		{		
			extern void PSX2Render_RenderTerrainSector(void* PS2Data, INT Layer, INT QuadsX, INT QuadsY, INT OffsetX, INT OffsetY);
			PSX2Render_RenderTerrainSector(Sector->PS2Data, passindex, Sector->QuadsX + 1, Sector->QuadsY + 1, Sector->OffsetX, Sector->OffsetY);
			RI->SetIndexBuffer(NULL,0);
		}
	}
#else		

#ifndef _XBOX //scion jg -- No need to check this on xbox since I removed indices
	if( SceneNode->Viewport->Actor->RendMap == REN_Wire)
	{
		for( INT s=0;s<Sectors.Num();s++ )
		{
			UTerrainSector* Sector = Sectors(s);
			if(VI && !VI->Visible(Sector->BoundingBox))
				continue;

			if( Sector->CompleteNumTriangles )
			{
			    FVertexStream*	Streams[1] = { &Sector->GetVertexStream() };
			    RI->SetVertexStreams(VS_FixedFunction,Streams,1);
			    DECLARE_STATIC_UOBJECT( UConstantColor, WireframeColor, {} );
			    DECLARE_STATIC_UOBJECT(
				    UShader, WireframeShader, 
				    {
					    WireframeShader->Diffuse = WireframeColor;
					    WireframeShader->Wireframe = 1;
				    }
			    );
			    WireframeColor->Color = GetLevel()->Engine->C_TerrainWire;	
			    RI->EnableLighting(0,0);
			    RI->SetMaterial(WireframeShader);								
			    RI->SetIndexBuffer(&Sector->CompleteIndexBuffer,0);
			    RI->DrawPrimitive(PT_TriangleList,0,Sector->CompleteNumTriangles,Sector->CompleteMinIndex,Sector->CompleteMaxIndex);
		    }
	    }
	}
	else
	if( CollapsedLayers && SceneNode->Viewport->RenDev->LowQualityTerrain )
	{
		for( INT s=0;s<Sectors.Num();s++ )
		{
			UTerrainSector* Sector = Sectors(s);
			if(VI && !VI->Visible(Sector->BoundingBox))
				continue;
			
			if( Sector->CompleteNumTriangles )
			{
			    FVertexStream*	Streams[1] = { &Sector->GetVertexStream() };
			    RI->SetVertexStreams(VS_FixedFunction,Streams,1);
			    RI->EnableLighting(0,0);
			    RI->SetMaterial(CollapsedLayers);								
			    RI->SetIndexBuffer(&Sector->CompleteIndexBuffer,0);
			    RI->DrawPrimitive(PT_TriangleList,0,Sector->CompleteNumTriangles,Sector->CompleteMinIndex,Sector->CompleteMaxIndex);
		    }
	    }
	}
	else
#endif
	{
		// Get list of everything we need to render in its own pass
		FMemMark Mark(GMem);

		// Build a list of dynamic projectors hitting each sector.
		FDynamicTerrainProjector**	SectorProjectors = NewZeroed<FDynamicTerrainProjector*>(GMem,Sectors.Num());

		CLOCK(BuilProj,GStats.DWORDStats( GEngineStats.STATS_Terrain_ProjectorRenderCycles ));
		for(INT ProjectorIndex = 0;ProjectorIndex < NumDynamicProjectors;ProjectorIndex++)
		{
			FProjectorRenderInfo*	ProjectorInfo = DynamicProjectors[ProjectorIndex];

			if( !ProjectorInfo->Render( Level->TimeSeconds ) )
				continue;

			if( !ProjectorInfo->Projector->bProjectTerrain )
				continue;
	
			checkSlow(ProjectorInfo->Projector);			

			FBox	LocalBoundingBox = ProjectorInfo->Projector->Box.TransformBy(ToHeightmap);
			INT		MinX = appFloor(LocalBoundingBox.Min.X),
					MaxX = appFloor(LocalBoundingBox.Max.X),
					MinY = appFloor(LocalBoundingBox.Min.Y),
					MaxY = appFloor(LocalBoundingBox.Max.Y),
					MinSectorX = MinX / TerrainSectorSize,
					MaxSectorX = MaxX / TerrainSectorSize,
					MinSectorY = MinY / TerrainSectorSize,
					MaxSectorY = MaxY / TerrainSectorSize;

			for(INT Y = MinSectorY;Y <= MaxSectorY;Y++)
			{
				if(Y >= 0 && Y < SectorsY)
				{
					for(INT X = MinSectorX;X <= MaxSectorX;X++)
					{
						if(X >= 0 && X < SectorsX)
						{
							INT				SectorIndex = X + Y * SectorsX;
							UTerrainSector*	Sector = Sectors(SectorIndex);

							if(Sector->BoundingBox.Intersect(ProjectorInfo->Projector->Box))
								SectorProjectors[SectorIndex] = new(GMem) FDynamicTerrainProjector(
																			Sector,
																			ProjectorInfo,
																			Clamp(MinX - X * TerrainSectorSize,0,Sector->QuadsX - 1),
																			Clamp(MinY - Y * TerrainSectorSize,0,Sector->QuadsY - 1),
																			Clamp(MaxX - X * TerrainSectorSize,0,Sector->QuadsX - 1),
																			Clamp(MaxY - Y * TerrainSectorSize,0,Sector->QuadsY - 1),
																			SectorProjectors[SectorIndex]
																			);
						}
					}
				}
			}
			
		}
		UNCLOCK();

		// scion sz - render one block of sectors at a time
		for( INT BlockIdx=0; BlockIdx < SectorBlocks.Num(); BlockIdx++ )
		{
			UTerrainSectorBlock* SectorBlock = SectorBlocks( BlockIdx );			

			TArray<UTerrainSector*>& CurrentSectors = SectorBlock->Sectors;

			// Get a list of visible sectors.
			INT* VisibleSectors = new(GMem, CurrentSectors.Num()) INT;
			INT VisibleSectorsCount = 0;
			INT RenderThingsCount = 0;
			for( INT s=0;s<CurrentSectors.Num();s++ )
			{
				UTerrainSector* Sector = CurrentSectors(s);
				if(VI && !VI->Visible(Sector->BoundingBox))
					continue;
				VisibleSectors[VisibleSectorsCount++] = s;
				STAT_INC(GStats.DWORDStats( GEngineStats.STATS_Terrain_Sectors ));
				if(GIsEditor)
					Sector->StaticLight(0);
				RenderThingsCount += Sector->RenderPasses.Num();
			}

			// Get a list of unique sector/layer combinations.
			FTerrainRenderThing* RenderThings = new(GMem, RenderThingsCount) FTerrainRenderThing;
			RenderThingsCount = 0;
			for( INT s=0;s<VisibleSectorsCount;s++ )
			{
				UTerrainSector* Sector = CurrentSectors(VisibleSectors[s]);
				FLOAT Dist = FDist( SceneNode->ViewOrigin, Sector->Location );
				FDynamicLight* ConsiderLights[8];
				appMemzero(ConsiderLights,sizeof(ConsiderLights));
				INT l=0;
				for( INT LightIndex = 0;LightIndex < NumDynamicLights;LightIndex++ )
				{
					if( DynamicLights[LightIndex]->Dynamic || DynamicLights[LightIndex]->Changed )
					{
						FBox BoundingBox = Sector->BoundingBox.ExpandBy(DynamicLights[LightIndex]->Radius);
						if( FPointBoxIntersection(DynamicLights[LightIndex]->Position, BoundingBox) )
						{
							ConsiderLights[l++] = DynamicLights[LightIndex];
							if( l > ARRAY_COUNT(ConsiderLights)-1 )
								break;
						}
					}
				}
				for( INT p=0;p<Sector->RenderPasses.Num();p++ )
					RenderThings[RenderThingsCount++].Set( (p==0 || Sector->RenderPasses(p).GetRenderCombination(this)->RenderMethod==RM_AlphaMap) ? p : 1, 
					&Sector->RenderPasses(p), 
					&SectorBlock->VertexStream, 
					Sector->RenderPasses(p).GetRenderCombination(this)->Layers.Num(), 
					Dist,
					ConsiderLights );
			}


			// sort it to minimize state changes
			Sort( RenderThings, RenderThingsCount );

#ifdef JG_PUSHBUFFER_INDICES
			//scion jg -- These are used to build a list of the indices that are
			// going to be rendered in a single pass
			FTerrainRenderThing** Things = (FTerrainRenderThing**)new(GMem, RenderThingsCount)DWORD;
			DWORD dwNumPerEncode = 0;
#else
			FTerrainIndexBuffer IndexBuffer( RenderThingsCount );
#endif
			// Render
			for( INT t=0;t<RenderThingsCount;t++ )
			{
#ifdef JG_PUSHBUFFER_INDICES
				// Start placing them at the beginning
				dwNumPerEncode = 0;
				Things[dwNumPerEncode++] = &RenderThings[t];
#else
				// clear out the dynamic index buffer
				IndexBuffer.Init();
				IndexBuffer.AddThing( &RenderThings[t] );
#endif

				//!!powervr_aaron: DrawIndexedPrimitive gets smaller ranges, so D3D SW TnL transforms fewer vertices, if the following is skipped
				if( GetLevel()->Engine->GRenDev->GetRenderCaps()->HardwareTL )
				{
					// Check the following RenderThings and render them simultaneously
					// if they have the same textures and vertex stream.
					for( INT n=t+1;n<RenderThingsCount;n++ )
					{
						if( RenderThings[t].ForceSortOrder != RenderThings[n].ForceSortOrder ||
							RenderThings[t].VertexStream != RenderThings[n].VertexStream ||
							RenderThings[t].RenderPass->RenderCombinationNum != RenderThings[n].RenderPass->RenderCombinationNum ||
							!RenderThings[t].LightsMatch( RenderThings[n] ) )
							break;
#ifdef JG_PUSHBUFFER_INDICES
						// Add this to be built into the push buffer
						Things[dwNumPerEncode++] = &RenderThings[n];
#else
						IndexBuffer.AddThing( &RenderThings[n] );
#endif
						t = n;
					}
				}

				FTerrainRenderCombination* RenderCombination = RenderThings[t].RenderPass->GetRenderCombination(this);

				FVertexStream*	Streams[1] = { RenderThings[t].VertexStream };

#ifdef _XBOX	
				// scion sz - use terrain vertex shader
				RI->SetVertexStreams( VS_TerrainSpecial, Streams, 1 );
				// scion sz - unique world transform needed for each block
				RI->SetTerrainTransform( SectorBlock->WorldMat, HeightmapX, HeightmapY );
#else
				RI->SetVertexStreams( VS_FixedFunction, Streams, 1 );
#endif

#ifndef JG_PUSHBUFFER_INDICES
				INT BaseIndex = RI->SetDynamicIndexBuffer(&IndexBuffer,0);
#endif
				DECLARE_STATIC_UOBJECT( UTerrainMaterial, TerrainMaterial, {} );

				// Copy the data in for the TerrainMaterial.
				TerrainMaterial->FirstPass = RenderThings[t].ForceSortOrder == 0 ? 1 : 0;
				TerrainMaterial->RenderMethod = RenderCombination->RenderMethod;

				// Make sure the material has the right number layers
				if( TerrainMaterial->Layers.Num() < RenderCombination->Layers.Num() )
					TerrainMaterial->Layers.Add( RenderCombination->Layers.Num() - TerrainMaterial->Layers.Num() );
				else
					if( TerrainMaterial->Layers.Num() > RenderCombination->Layers.Num() )
						TerrainMaterial->Layers.Remove( RenderCombination->Layers.Num(), TerrainMaterial->Layers.Num() - RenderCombination->Layers.Num() );

				// Copy the layers' textures to the UTerrainMaterial.
				for( INT layer=0;layer<RenderCombination->Layers.Num();layer++ )
				{
					TerrainMaterial->Layers(layer).Texture = Layers[RenderCombination->Layers(layer)].Texture;
					switch( RenderCombination->RenderMethod )
					{
					case RM_AlphaMap:
						TerrainMaterial->Layers(layer).AlphaWeight = Layers[RenderCombination->Layers(layer)].AlphaMap;
						break;
					case RM_WeightMap:
						TerrainMaterial->Layers(layer).AlphaWeight = Layers[RenderCombination->Layers(layer)].LayerWeightMap;
						break;
					case RM_CombinedWeightMap:
						// The combined weightmap is set only for the first layer.
						TerrainMaterial->Layers(layer).AlphaWeight = layer==0 ? RenderCombination->CombinedWeightMaps : NULL;
						break;
					}
					TerrainMaterial->Layers(layer).TextureMatrix = Layers[RenderCombination->Layers(layer)].TextureMatrix;
				}

				if( SceneNode->Viewport->Actor->RendMap==REN_LightingOnly )
				{
					RI->EnableLighting(1, 1, 1, NULL, 1, FSphere(FVector(0,0,0),-1.0f));
					RI->SetMaterial(TerrainMaterial);
				}
				else if( SceneNode->Viewport->Actor->RendMap==REN_PlainTex )
				{
					RI->EnableLighting(0, 0);
					RI->SetMaterial(TerrainMaterial);
				}
				else
				{
					RI->EnableLighting(1, 1, 1, NULL, 0, FSphere(FVector(0,0,0),-1.0f));
					RI->SetMaterial(TerrainMaterial);
				}

				for( INT i=0; i<8; i++ )
					RI->SetLight( i, RenderThings[t].DynamicLights[i] );

#ifdef _XBOX
#ifdef JG_PUSHBUFFER_INDICES
				//scion jg -- We don't use dynamic indices to render terrain on the
				// xbox, since that causes 2 memcpys of indices. Instead we encode
				// the indices directly into the main push buffer. This is 12-18%
				// faster but removes the ability to use texture coords from stage
				// 1 across stages 2-4.

				// scion sz - vertex shader fragments can correctly handle tex coord
				// source remapping.  So, we can encode indices directly now.

				// No indices. These will be built into the push buffer
				RI->SetIndexBuffer(NULL,0);
				DWORD dwNumIndices = 0;
				// Build a new push buffer out of the indices
				EncodeIndicesIntoPushBuffer(Things,dwNumPerEncode,dwNumIndices,RI);
				STAT_INC_BY(GStats.DWORDStats( GEngineStats.STATS_Terrain_Triangles ),dwNumIndices / 2);
#else
				BEGIN_D3D_EVENT(BDE)(PIX_TERRAIN,TEXT("%s"),GetName());
				//scion jg -- Render terrain as quads
				RI->DrawPrimitive(PT_QuadList,BaseIndex,IndexBuffer.NumTriangles,
					IndexBuffer.MinIndex,IndexBuffer.MaxIndex);			
				END_D3D_EVENT;
				STAT_INC_BY(GStats.DWORDStats( GEngineStats.STATS_Terrain_Triangles ),IndexBuffer.NumTriangles * 2);
#endif
#else
				RI->DrawPrimitive(PT_TriangleList,BaseIndex,IndexBuffer.NumTriangles,IndexBuffer.MinIndex,IndexBuffer.MaxIndex);			
				STAT_INC_BY(GStats.DWORDStats( GEngineStats.STATS_Terrain_Triangles ),IndexBuffer.NumTriangles);
#endif
				STAT_INC(GStats.DWORDStats( GEngineStats.STATS_Terrain_DrawPrimitives ));
			}

#ifndef FINAL_RELEASE
			if(!SceneNode->Viewport->IsWire() && (SceneNode->Viewport->Actor->ShowFlags & SHOW_Projectors))
#endif
			{
				FUNC_CLOCK(Proj,GStats.DWORDStats( GEngineStats.STATS_Terrain_ProjectorRenderCycles ));

				// Render projectors for all visible sectors
				guard(ATerrainInfo::Render::RenderProjectors);
				for( INT s=0;s<VisibleSectorsCount;s++ )
				{
					UTerrainSector* Sector = CurrentSectors(VisibleSectors[s]);

					if( Sector->Projectors.Num() )
					{
						//scion jg -- Added so we can see these broken out
						FUNC_CLOCK(Proj,GStats.DWORDStats( GEngineStats.STATS_Projector_RenderCycles ));
						RI->PushState();

						// Render projectors for this sector.
						for( INT i=0;i<Sector->Projectors.Num();i++ )
						{
							FStaticProjectorInfo*	ProjectorInfo = Sector->Projectors(i);
							if( !ProjectorInfo->RenderInfo->Render( Level->TimeSeconds ) )
							{
								Sector->Projectors(i) = Sector->Projectors(i)->RemoveRef();
								Sector->Projectors.Remove(i--);
								continue;
							}

							// Find an existing batch for the projector.
							FProjectorRenderBatch*	ProjectorBatch = NULL;
							for(TList<FProjectorRenderBatch*>* ProjectorBatchList = GProjectorBatchList;ProjectorBatchList;ProjectorBatchList = ProjectorBatchList->Next)
							{
								if( ProjectorBatchList->Element->MatchProjectorInfo( ProjectorInfo->RenderInfo, ProjectorInfo->BaseMaterial, ProjectorInfo->TwoSided ) )
								{
									ProjectorBatch = ProjectorBatchList->Element;
									break;
								}
							}

							// Create a new batch if necessary.
							if(!ProjectorBatch)
							{
#if !DYNAMIC_PROJ_STREAM
								ProjectorBatch = new(GSceneMem) FProjectorRenderBatchDef( ProjectorInfo->RenderInfo );
#else
								ProjectorBatch = new(GSceneMem) FProjectorRenderBatchDef(
									ProjectorInfo->BaseMaterial,
									ProjectorInfo->RenderInfo->Material,
									(EProjectorBlending)ProjectorInfo->RenderInfo->MaterialBlendingOp,
									(EProjectorBlending)ProjectorInfo->RenderInfo->FrameBufferBlendingOp,
									ProjectorInfo->TwoSided
									);
#endif
								GProjectorBatchList = new(GSceneMem) TList<FProjectorRenderBatch*>(ProjectorBatch,GProjectorBatchList);
							}

							// Add the projector to the batch.

							ProjectorBatch->AddPrimitive(new(GSceneMem) FStaticProjectorPrimitive(ProjectorInfo));
						}
						RI->PopState();
					}

					// scion sz - get global index for sector
					INT SectorIdx = GetSectorIndex( Sector->OffsetX, Sector->OffsetY );
					if(SectorProjectors[SectorIdx])
					{
						RI->PushState();

						// Setup blending.
						RI->EnableLighting(0,0);

						// Set the vertex stream / index buffer
						// scion sz - use current block's vertex stream
						FVertexStream*	Streams[1] = { &SectorBlock->VertexStream };
#ifndef _XBOX
						RI->SetVertexStreams( VS_FixedFunction, Streams, 1 );

#else
						// scion sz - use terrain vertex shader
						RI->SetVertexStreams( VS_TerrainSpecial, Streams, 1 );
#endif

						// Render projectors for this sector.
						for( FDynamicTerrainProjector* DynamicProjector=SectorProjectors[SectorIdx];DynamicProjector;DynamicProjector=DynamicProjector->Next )
						{	
							// scion sz - set index buffer and make sure there are some clipped vertices to render
							INT BaseIndex = RI->SetDynamicIndexBuffer( DynamicProjector, 0 );
							if( DynamicProjector->ClippedNumTriangles == 0 )
								continue;

							FProjectorRenderInfo* ProjectorInfo = DynamicProjector->ProjectorInfo;
							
#ifdef _XBOX
							// scion sz - we can only handle simple bitmaps with the UProjectorMultiMaterial
							if( ProjectorInfo->GetProjectedBitmap() )
							{
								FMultiMaterialProjectorInfo ProjMultMatInfo( NULL, 1 );
								ProjMultMatInfo.AddProjectorEntry( ProjectorInfo );
								UProjectorMultiMaterial* ProjMultMat = ProjMultMatInfo.GetMaterial( (EProjectorBlending)ProjectorInfo->FrameBufferBlendingOp );
								RI->SetMaterial( ProjMultMat );								
							}
							else
							{
								RI->SetMaterial( ProjectorInfo->GetMaterial(SceneNode,NULL) );
							}
							
							// scion sz - unique world transform needed for each sector
							// NOTESZ - if a shadow projector was rendered with a skeletal mesh
							// it will overrite these constants .. that is why they must be reset here
							// instead of just once.
							RI->SetTerrainTransform( SectorBlock->WorldMat, HeightmapX, HeightmapY );
#else
							// just set the material
							RI->SetMaterial( ProjectorInfo->GetMaterial(SceneNode,NULL) );
#endif

							RI->DrawPrimitive(
								PT_TriangleList,
								BaseIndex,
								DynamicProjector->ClippedNumTriangles,
								DynamicProjector->MinIndex,
								DynamicProjector->MaxIndex
								);

							STAT_INC(GStats.DWORDStats( GEngineStats.STATS_Projector_Projectors ));
							STAT_INC_BY(GStats.DWORDStats( GEngineStats.STATS_Projector_Triangles ),DynamicProjector->ClippedNumTriangles);
						}

						RI->PopState();
					}
				}
				unguard;
			}

#ifndef _XBOX
			// Render wireframe lines for specific layers.
			if( GIsEditor && ShowGrid )
			{
				DECLARE_STATIC_UOBJECT( UConstantColor, WireframeColor, {} );
				DECLARE_STATIC_UOBJECT(
				UShader, WireframeShader, 
				{
					WireframeShader->Diffuse = WireframeColor;
					WireframeShader->Wireframe = 1;
				}
				);
				WireframeColor->Color = GetLevel()->Engine->C_TerrainWire;
				RI->EnableLighting(0,0);
				RI->SetMaterial(WireframeShader);

				for( INT s=0;s<VisibleSectorsCount;s++ )
				{
					UTerrainSector* Sector = Sectors(VisibleSectors[s]);
					FVertexStream*	Streams[1] = { &Sector->GetVertexStream() };
					RI->SetVertexStreams(VS_FixedFunction,Streams,1);

					for( INT p=0;p<Sector->RenderPasses.Num();p++ )
					{
						FTerrainSectorRenderPass& RenderPass = Sector->RenderPasses(p);
						UBOOL RenderGrid = 0;

						FTerrainRenderCombination* RenderCombination = RenderPass.GetRenderCombination(this);
						for( INT l=0;l<RenderCombination->Layers.Num();l++ )
						{
							if( ShowGrid&(1<<RenderCombination->Layers(l)) )
							{
								RenderGrid = 1;
								break;
							}
						}

						// Draw the grid for this renderpass
						if( RenderGrid )
						{
							FTerrainWireframeIndexBuffer WireframeIndexBuffer(&RenderPass);
							INT BaseIndex = RI->SetDynamicIndexBuffer(&WireframeIndexBuffer,0);
							RI->DrawPrimitive(PT_TriangleList,BaseIndex,RenderPass.NumTriangles,RenderPass.MinIndex,RenderPass.MaxIndex);					
						}
					}
				}
			}
#endif
		} // iterate sector blocks
		// Free the memory used to render terrain.
		Mark.Pop();
	}
#endif

	POP_HIT(SceneNode->Viewport);

	RI->PopState();

#ifdef CONSOLE
	RI->SetTransform(TT_LocalToWorld,FMatrix::Identity);
#else // no need to have this code on PS2
	if( GIsEditor && GTerrainTools.EditorMode == EM_TerrainEdit )
	{
		FLOAT W;
		FVector V, VPA = FVector(0,0,0);
		FColor Color;
		
		//
		// Vertex being pointed at.
		//

		if( GTerrainTools.GetCurrentTerrainInfo() == this && GTerrainTools.CurrentBrush->bShowRedVertex )
		{
			Color = FColor(255,0,0);
			VPA = GTerrainTools.PointingAt + FVector(0,0,32);
			DrawVertex( SceneNode, RI, VPA, Color );
		}

		//
		// Selected vertices.
		//

		if( GTerrainTools.CurrentBrush->bShowVertices )
			for( INT i=0;i<SelectedVertices.Num();i++ )
			{
				W = SelectedVertices(i).Weight;
				Color = FColor(W*255,W*255,W*255);
				
				V = Vertices( GetGlobalVertex(SelectedVertices(i).X, SelectedVertices(i).Y ) );
				V += FVector(0,0,32);

				// If this vertex is selected AND it's the one being pointed at, tint it red.
				if( V.X == VPA.X && V.Y == VPA.Y )
					Color = FColor(255,W*128,W*128 );

				DrawVertex( SceneNode, RI, V, Color );
			}

		//
		// Circle markers.
		//

		if( GTerrainTools.GetCurrentTerrainInfo() == this )
		{
			if( GTerrainTools.CurrentBrush->bShowMarkers )
			{
				if( GTerrainTools.CurrentBrush->bUseInnerRadius )
					DrawCircleOnTerrain( RI, this, SceneNode->Viewport->TerrainPointAtLocation, GTerrainTools.GetInnerRadius(), FColor(255,255,0) );
				DrawCircleOnTerrain( RI, this, SceneNode->Viewport->TerrainPointAtLocation, GTerrainTools.GetOuterRadius(), FColor(160,160,0) );
			}

			//
			// Rectangle.
			//

			if( GTerrainTools.CurrentBrush->bShowRect )
				DrawRectOnTerrain( RI, this, GTerrainTools.CurrentBrush->GetRect(), FColor(160,160,0) );
		}
	}
#endif

	unguard;
}

void ATerrainInfo::UpdateDecorations( INT SectorIndex )
{
	guard(ATerrainInfo::UpdateDecorations);

	if ( !DecoLayerData.Num() )
	{
		guard(ATerrainInfo::UpdateDecorations::InitDecorations);
		DecoLayerData.AddZeroed( DecoLayers.Num() );
		for( INT LayerIndex=0; LayerIndex<DecoLayers.Num(); LayerIndex++ )
			DecoLayerData(LayerIndex).Sectors.AddZeroed( Sectors.Num() );
		unguard;
	}

	UTerrainSector* Sector = Sectors(SectorIndex);

	for (INT LayerIndex=0; LayerIndex<DecoLayers.Num(); LayerIndex++)
	{
		DecoLayerData(LayerIndex).Sectors(SectorIndex).DecoInfo.Empty();
		DecoLayerData(LayerIndex).Sectors(SectorIndex).Location = Sector->Location;
		DecoLayerData(LayerIndex).Sectors(SectorIndex).Radius	= Sector->Radius;

		FDecorationLayer& DecoLayer = DecoLayers(LayerIndex);
		INT Counter = 0;

		if ( !DecoLayer.StaticMesh || !DecoLayer.DensityMap )
			continue;

		UTexture* DensityMap = DecoLayer.DensityMap;
		UTexture* ScaleMap   = DecoLayer.ScaleMap;
		UTexture* ColorMap   = DecoLayer.ColorMap;

		// Initialize RNG per layer/ sector.
		appSRandInit(DecoLayer.Seed + SectorIndex);

		for (INT y=0; y<Sector->QuadsY; y++)
		{
			for (INT x=0; x<Sector->QuadsX; x++)
			{
				INT GlobalX = Sector->OffsetX + x;
				INT GlobalY = Sector->OffsetY + y;

				if( !DecoLayer.ShowOnInvisibleTerrain && !GetQuadVisibilityBitmap( GlobalX, GlobalY ) )
					continue;

				for (INT i=0; i<DecoLayer.MaxPerQuad; i++)
				{						
					FLOAT temp = GetLayerAlpha(GlobalX,GlobalY,0,DensityMap) / 255.f;			
					if (appSRand() < (temp * DecoLayer.DensityMultiplier.GetSRand()))
					{
						INT TempIndex = DecoLayerData(LayerIndex).Sectors(SectorIndex).DecoInfo.AddZeroed(1);
						FDecoInfo& DecoInfo = DecoLayerData(LayerIndex).Sectors(SectorIndex).DecoInfo( TempIndex );

						FLOAT RandX = appSRand();
						FLOAT RandY = appSRand();

						FVector Normal;
						FVector Location;
								
						// Calculate Z value.
						if ( RandX > RandY )	// 1,2,4
						{
							FVector DirX = Vertices(Sector->GetGlobalVertex( x, y )) - Vertices(Sector->GetGlobalVertex( x+1, y ));
							FVector DirY = Vertices(Sector->GetGlobalVertex( x+1, y+1 )) - Vertices(Sector->GetGlobalVertex( x+1, y ));
							Location	 = Vertices(Sector->GetGlobalVertex( x+1, y )) + DirX * (1-RandX) + DirY * RandY;
							Normal		 = (DirX ^ DirY).SafeNormal();
						}
						else					// 1,4,3
						{
							FVector DirX = Vertices(Sector->GetGlobalVertex( x, y )) - Vertices(Sector->GetGlobalVertex( x, y+1 ));
							FVector DirY = Vertices(Sector->GetGlobalVertex( x+1, y+1 )) - Vertices(Sector->GetGlobalVertex( x, y+1 ));
							Location	 = Vertices(Sector->GetGlobalVertex( x, y+1 )) + DirX * RandX + DirY * (1 - RandY);
							Normal		 = (DirX ^ DirY).SafeNormal();
						}

						if (Normal.Z < 0)
							Normal *= -1;

						if( Inverted )
							Normal *= -1;

						Normal = DecoLayer.AlignToTerrain ? Normal : Inverted ? FVector(0.f,0.f,-1.f) : FVector(0.f,0.f,1.f);

						FRotator Rotation = Normal.Rotation();
						// Objects are created along Z axis and not -X
						Rotation.Pitch -= 16384;
						if( DecoLayer.RandomYaw )
							Rotation.Yaw	= 65535 * appSRand();

						// Position/Rotation/Scale.
						DecoInfo.Location	= Location + Normal * DecoLayerOffset;
						DecoInfo.Rotation	= Rotation;
						DecoInfo.Scale		= DecoLayer.ScaleMultiplier.GetSRand();
						DecoInfo.Distance	= Counter++;

						// ScaleMap.
						if ( ScaleMap )
							DecoInfo.Scale *= (FVector)GetTextureColor( GlobalX, GlobalY, ScaleMap);

						// Terrain lighting vs constant color.
						if( DecoLayer.DisregardTerrainLighting )
							DecoInfo.Color = FColor( 127,127,127,255 );
						else
							DecoInfo.Color = Sector->GetVertexStream().Vertices( Sector->VertexStreamOffset + Sector->GetLocalVertex(x,y) ).Color;

						// ColorMap.
						if ( ColorMap )
						{
							FVector Color = GetTextureColor(GlobalX, GlobalY, ColorMap);
							DecoInfo.Color.R *= Color.X;
							DecoInfo.Color.G *= Color.Y;
							DecoInfo.Color.B *= Color.Z;
						}
					}
				}
			}
		}
	}
	unguard;
};

static INT DecoSortOrder = 1;
static INT Compare(FDecoInfo& A, FDecoInfo& B)
{
	return (A.Distance - B.Distance) * DecoSortOrder;
}

void ATerrainInfo::RenderDecorations(FLevelSceneNode* SceneNode,FRenderInterface* RI,FVisibilityInterface* VI)
{
	guard(ATerrainInfo::RenderDecorations);
	check( SceneNode );
	check( RI );
	check( VI );

	if( !TerrainMap || !DecoLayers.Num() )
		return;

	// scion sz - skip rendering of deco layers if detail level is too low
	if( Level && DecoDetailLevel > Level->DetailMode )
		return;

	if( GIsEditor )
	{
		// Hide this terrain if the terraininfo actor is hidden.
		if( IsHiddenEd() )
			return;
	}

	CheckComputeDataOnLoad();

	INT		RenderedTriangles	= 0,
			RenderedDecorations	= 0;
	FLOAT	MaxMeshExtent		= 0.f;


	guard(RenderDecorations::check1);
	// Find maximum mesh extent.
	for (INT i=0; i<DecoLayers.Num(); i++)
	{
		if ( DecoLayers(i).StaticMesh )
		{
			FVector Extent = DecoLayers(i).StaticMesh->BoundingBox.GetExtent();
			MaxMeshExtent = Max<FLOAT>( MaxMeshExtent, Extent.GetMax() * DecoLayers(i).ScaleMultiplier.GetMax().GetMax() );
		}
	}
	unguard;

	FVector& Origin = SceneNode->ViewOrigin;

	INT* SectorIndices = new INT[Sectors.Num()];
	guard(RenderDecorations::check2);
	// Roughly sort sectors by distance. (decolayer 0 is dominant)	
	for( INT s=Sectors.Num()-1; s>=0; s-- )
		SectorIndices[s] = s + (((INT) FDist( Origin, DecoLayerData(0).Sectors(s).Location )) << 16);
	if( DecoLayers(0).DrawOrder != SORT_NoSort )
	{
		IntSortOrder = (DecoLayers(0).DrawOrder == SORT_FrontToBack) ? -1 : 1;
		Sort( SectorIndices, Sectors.Num() );
	}
	for( INT s=Sectors.Num()-1; s>=0; s-- )
		SectorIndices[s] &= 0xFFFF;
	unguard;
	
	// Iterate through all sectors.
	for( INT s=Sectors.Num()-1; s>=0; s-- )
	{
		UTerrainSector* Sector = Sectors(SectorIndices[s]); check(Sector);		
		FBox			BoundingBox = Sector->BoundingBox;
			
		// Extent by maximum mesh size.
		BoundingBox = BoundingBox.ExpandBy( MaxMeshExtent );

		// We need our own visibility determination to avoid popping.
		if( !VI->Visible(BoundingBox) )
			continue;

		// Iterate through all deco layers.
		for (INT LayerIndex=0; LayerIndex<DecoLayers.Num(); LayerIndex++)
		{
			FDecorationLayer& DecoLayer = DecoLayers(LayerIndex);
					
			// Don't render if it doesn't make sense.
			if ( !DecoLayer.StaticMesh || !DecoLayer.ShowOnTerrain )
				continue;

			check( LayerIndex < DecoLayerData.Num() );
			FDecorationLayerData& DecoLayerDataEntry = DecoLayerData(LayerIndex);
			check( SectorIndices[s] < DecoLayerDataEntry.Sectors.Num() );
			FDecoSectorInfo& DecoSector = DecoLayerDataEntry.Sectors(SectorIndices[s]);

			// Early out if nothing to render.
			INT Amount = DecoSector.DecoInfo.Num();
			if ( !Amount )
				continue;

			// Bail out if sector too far away.
			FVector SectorLocation	= DecoSector.Location;
			FLOAT	SectorRadius	= DecoSector.Radius;
			if ( FDistSquared( Origin, SectorLocation ) > Square(DecoLayer.FadeoutRadius.Max + MaxMeshExtent + SectorRadius) )
				continue;

			FLOAT RMaxMin			= 1.f;
			if ( DecoLayer.FadeoutRadius.Max - DecoLayer.FadeoutRadius.Min )
				RMaxMin /= DecoLayer.FadeoutRadius.Max - DecoLayer.FadeoutRadius.Min;
	
			check( DecoSector.DecoInfo.Num() > 0 );
			FDecoInfo* DecoInfo = &DecoSector.DecoInfo(0);
			check(DecoInfo);
			
			INT Rendered = 0;
			for( INT DecoIndex=0; DecoIndex<Amount; DecoIndex++ )
			{
				FLOAT Distance		= FDist(Origin, DecoInfo->Location);
                DecoInfo->TempScale.Y = Distance;
				FLOAT DistanceScale = 1.f;	
								
				// Don't render if too far away.
				if ( Distance > DecoLayer.FadeoutRadius.Max )
				{
					DecoInfo->TempScale.X = -1;
					DecoInfo++;
					continue;
				}

				if ( Distance > DecoLayer.FadeoutRadius.Min )
					DistanceScale = 1 - (Distance - DecoLayer.FadeoutRadius.Min) * RMaxMin;
				
				DecoInfo->Distance		= (DecoInfo->Distance & 0xFFFF) | (((INT) Distance) << 19);
				DecoInfo->TempScale.X	= DistanceScale;//DecoInfo->Scale * DistanceScale;
				
				DecoInfo++;
				Rendered++;
			}

			RenderedDecorations += Rendered;

			// Bail out if nothing to render.
			if ( !Rendered )
				continue;
	
			guard(RenderDecorations::check3);
			// Set sunlights.
			if( DecoLayer.LitDirectional )
			{
				INT HWLightIndex = 0;
				for( INT LightIndex=0; LightIndex<Sector->LightInfos.Num(); LightIndex++)
				{
					if( Sector->LightInfos(LightIndex).LightActor->LightEffect == LE_Sunlight )
					{		
						FDynamicLight SunLight( Sector->LightInfos(LightIndex).LightActor );
						SunLight.Color = FPlane( 0.572f, 0.572f, 0.572f, 1.f );
						RI->SetLight( HWLightIndex++, &SunLight );
					}
				}
				for( INT LightIndex=HWLightIndex; LightIndex<8; LightIndex++ )
					RI->SetLight( LightIndex, NULL );
			}
			RI->EnableLighting( (DecoLayer.LitDirectional && !SceneNode->Viewport->IsWire()) ? 1 : 0, 0, 1, NULL, SceneNode->Viewport->Actor->RendMap == REN_LightingOnly );
			unguard;

			// Set vertex and index buffers.
			UStaticMesh*	StaticMesh = DecoLayer.StaticMesh;
			FVertexStream*	VertexStreams[9] = { &StaticMesh->VertexStream };
			INT				NumVertexStreams = 1;

			for(INT UVIndex = 0;UVIndex < StaticMesh->UVStreams.Num();UVIndex++)
				VertexStreams[NumVertexStreams++] = &StaticMesh->UVStreams(UVIndex);

#ifdef _XBOX//jg -- All of these changes are due to the compression
			RI->SetVertexStreams(VS_CompressedMesh,VertexStreams,NumVertexStreams);
			// Set the compression scale as a constant
			FLOAT CompressionValues[] =
			{
				StaticMesh->CompressionScale.X,
				StaticMesh->CompressionScale.Y,
				StaticMesh->CompressionScale.Z,
				1.f,
				StaticMesh->CompressionOffset.X,
				StaticMesh->CompressionOffset.Y,
				StaticMesh->CompressionOffset.Z,
				1.f
			};
			RI->SetVertexShaderConstant(VSC_COMPRESSION_SCALE,CompressionValues,2);
			// No indices used
			RI->SetIndexBuffer(NULL,0);
#else
			RI->SetVertexStreams(0,VertexStreams,NumVertexStreams);
			RI->SetIndexBuffer(&StaticMesh->IndexBuffer,0);
#endif

			guard(RenderDecorations::check4);
			// Sort by section to reduce amount of state changes.
			for (INT SectionIndex=0; SectionIndex<StaticMesh->Sections.Num(); SectionIndex++)
			{
				FStaticMeshSection& Section = StaticMesh->Sections(SectionIndex);
				UMaterial* Material	= StaticMesh->Materials(SectionIndex).Material ? StaticMesh->Materials(SectionIndex).Material : SceneNode->Viewport->Actor->Level->DefaultTexture->Get(SceneNode->Viewport->CurrentTime,SceneNode->Viewport);
			
				if( Section.NumPrimitives == 0 )
					continue;

				DECLARE_STATIC_UOBJECT( UColorModifier, ColorModifier, {} );
				if(SceneNode->Viewport->IsWire())
				{
					DECLARE_STATIC_UOBJECT( UShader, WireframeShader, { WireframeShader->Wireframe = 1; });
					ColorModifier->Material = WireframeShader;
				}
				else		
					ColorModifier->Material = Material;
				RI->SetMaterial( ColorModifier );

				RenderedTriangles += Rendered * Section.NumPrimitives;

				check( LayerIndex < DecoLayerData.Num() );
				FDecorationLayerData& DecoLayerDataEntry = DecoLayerData(LayerIndex);
				check( SectorIndices[s] < DecoLayerDataEntry.Sectors.Num() );
				FDecoSectorInfo& DecoSector = DecoLayerDataEntry.Sectors(SectorIndices[s]);

				check( DecoSector.DecoInfo.Num() > 0 );
				DecoInfo = &DecoSector.DecoInfo(0);
				check(DecoInfo);

				if( DecoLayer.DrawOrder != SORT_NoSort )
				{
					DecoSortOrder = (DecoLayer.DrawOrder == SORT_FrontToBack) ? 1 : -1;
					Sort( DecoInfo, DecoSector.DecoInfo.Num() );
				}

				// Finally render them all.
				for(INT i=0; i<Amount; i++)
				{
					// Skip if not visible (too far away).
					if ( DecoInfo->TempScale.X < 0 )
					{
						DecoInfo++;
						continue;
					}
					
					FMatrix	LocalToWorld = 
						FScaleMatrix(DecoInfo->Scale)		*
						FRotationMatrix(DecoInfo->Rotation) *
						FTranslationMatrix(DecoInfo->Location);
					
					RI->SetTransform(TT_LocalToWorld,LocalToWorld);
					
					FColor Color(DecoInfo->Color);
					Color.A *= DecoInfo->TempScale.X;
					RI->SetGlobalColor(Color);

					// Render the section.
					BEGIN_D3D_EVENT(BDE)(PIX_DECO,TEXT("%s S(%d)"),
						StaticMesh->GetName(),SectionIndex);
#ifdef _XBOX
					//scion jg -- Since sorting is gone, we discard the indices. We must
					// use the precompiled pushbuffer
					// Draw using the push buffer instead
					RI->RunPushBuffer(Section.pPushBuffer,1);
#else
					RI->DrawPrimitive(
						Section.IsStrip ? PT_TriangleStrip : PT_TriangleList,
						Section.FirstIndex,
						Section.NumPrimitives,
						Section.MinVertexIndex,
						Section.MaxVertexIndex
						);
#endif
					END_D3D_EVENT;
					DecoInfo++;
				}
			}
			unguard;
		}
	}

	delete [] SectorIndices;

	STAT_INC_BY(GStats.DWORDStats( GEngineStats.STATS_DecoLayer_Triangles ),RenderedTriangles);
	STAT_INC_BY(GStats.DWORDStats( GEngineStats.STATS_DecoLayer_Decorations ),RenderedDecorations);

	unguard;
}

IMPLEMENT_CLASS(UTerrainSectorBlock);

/* scion ======================================================================
* UTerrainSectorBlock::AddSector
* Author: sz
*
* Add a sector to this sector block
* ============================================================================
*/
UBOOL UTerrainSectorBlock::AddSector( UTerrainSector* Sector )
{
	guard(UTerrainSectorBlock::AddSector);

	UBOOL Success = 0;

	if( IsSectorInBlock( Sector ) )
	{
		Sectors.AddUniqueItem( Sector );
		Success = 1;
	}

	return Success;
	
	unguard;
}

/* scion ======================================================================
* UTerrainSectorBlock::IsSectorInBlock
* Author: sz
*
* Return true if sector should be contained by this sector block
* ============================================================================
*/
UBOOL UTerrainSectorBlock::IsSectorInBlock( UTerrainSector* Sector )
{
	guard(UTerrainSectorBlock::IsSectorInBlock);
	checkSlow( TerrainInfo );

	UBOOL Result=0;

	if( Sector->OffsetX >= OffsetX && 
		Sector->OffsetY >= OffsetY &&
		Sector->OffsetX < (OffsetX + SizeX) &&
		Sector->OffsetY < (OffsetY + SizeY) ) 
	{
		Result = 1;
	}
	
	return Result;
	
	unguard;
}

/* scion ======================================================================
* UTerrainSectorBlock::CalcOffsetMatrix
* Author: sz
*
* offset for this block concatenated with the world matrix for the terrain
* ============================================================================
*/
void UTerrainSectorBlock::CalcWorldMatrix()
{
	guard(UTerrainSectorBlock::CalcWorldMatrix);
	checkSlow( TerrainInfo );

	WorldMat = FTranslationMatrix( FVector( OffsetX, OffsetY, ((MAXWORD+1)/2) ) ) * TerrainInfo->ToWorld.Matrix();
			
	unguard;
}

/* scion ======================================================================
* UTerrainSectorBlock::UTerrainSectorBlock
* Author: sz
*
* constructor
* ============================================================================
*/
UTerrainSectorBlock::UTerrainSectorBlock( ATerrainInfo* InTerrainInfo, INT InOffsetX, INT InOffsetY, INT InSizeX, INT InSizeY ) :
	TerrainInfo(InTerrainInfo), 
	OffsetX(InOffsetX), 
	OffsetY(InOffsetY),
	SizeX(InSizeX),
	SizeY(InSizeY)
{
    // update the world matrix for this block
	CalcWorldMatrix();
}

/* scion ======================================================================
* UTerrainSectorBlock::Serialize
* Author: sz
*
* UObject interface
* ============================================================================
*/
void UTerrainSectorBlock::Serialize(FArchive& Ar)
{
	guard(UTerrainSectorBlock::Serialize);
	Super::Serialize(Ar);

#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
	if(Ar.LicenseeVer() == XBOX_PACKAGE_FORMAT)
	{
		Ar	<< OffsetX << OffsetY
			<< SizeX << SizeY
			<< WorldMat
			<< Sectors
			<< TerrainInfo
			<< VertexStream;
	}
#endif
	if( !(Ar.IsLoading() || Ar.IsSaving()) )
	{
		Ar	<< OffsetX << OffsetY
			<< SizeX << SizeY
			<< WorldMat
			<< Sectors
			<< TerrainInfo
			<< VertexStream;
	}

	unguard;
}

/* scion ======================================================================
* UTerrainSectorBlock::PostLoad
* Author: sz
*
* UObject interface
* ============================================================================
*/
void UTerrainSectorBlock::PostLoad()
{
	guard(UTerrainSectorBlock::PostLoad);
	Super::PostLoad();
	unguard;
}

/* scion ======================================================================
* UTerrainSectorBlock::Destroy
* Author: sz
*
* UObject interface
* ============================================================================
*/
void UTerrainSectorBlock::Destroy()
{
	guard(UTerrainSectorBlock::Destroy);
	Super::Destroy();
	unguard;
}

/*------------------------------------------------------------------------------
	The End.
------------------------------------------------------------------------------*/

