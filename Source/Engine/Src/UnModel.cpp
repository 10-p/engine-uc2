/*=============================================================================
	UnModel.cpp: Unreal model functions
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "EnginePrivate.h"

/*-----------------------------------------------------------------------------
	Struct serializers.
-----------------------------------------------------------------------------*/

/* scion ======================================================================
 * operator<<
 * Author: jg
 * 
 * Rewrote to shrink all of the data structures used for BSP
 *
 * input:	Ar -- The archive to serialize from/to
 *			Surf -- The FBspSurf to serialize
 *
 * ============================================================================
 */
ENGINE_API FArchive& operator<<( FArchive& Ar, FBspSurf& Surf )
{
	guard(FBspSurf<<);
	Ar << Surf.Material;
	Ar << Surf.PolyFlags;
	if (Ar.LicenseeVer() == XBOX_PACKAGE_FORMAT)
	{
		Ar << AR_INDEX(Surf.pBase) << AR_INDEX(Surf.vNormal);
		Ar << AR_INDEX(Surf.vTextureU) << AR_INDEX(Surf.vTextureV);
		Ar << AR_INDEX(Surf.iBrushPoly);
	}
	else
	{
		INT pBase = Surf.pBase;
		INT vNormal = Surf.vNormal;
		INT vTextureU = Surf.vTextureU;
		INT vTextureV = Surf.vTextureV;
		INT iBrushPoly = Surf.iBrushPoly;

		Ar << AR_INDEX(pBase) << AR_INDEX(vNormal);
		Ar << AR_INDEX(vTextureU) << AR_INDEX(vTextureV);
		Ar << AR_INDEX(iBrushPoly);

		if (Ar.IsLoading())
		{
			checkSlow(pBase < 32767);
			Surf.pBase = pBase;
			checkSlow(vNormal < 32767);
			Surf.vNormal = vNormal;
			checkSlow(vTextureU < 32767);
			Surf.vTextureU = vTextureU;
			checkSlow(vTextureV < 32767);
			Surf.vTextureV = vTextureV;
			checkSlow(iBrushPoly < 32767);
			Surf.iBrushPoly = iBrushPoly;
		}
	}
	Ar << Surf.Actor;
	Ar << Surf.Plane;
	Ar << Surf.LightMapScale;
	return Ar;
	unguard;
}

/* scion ======================================================================
 * operator<<
 * Author: jg
 * 
 * Rewrote to shrink all of the data structures used for BSP
 *
 * input:	Ar -- The archive to serialize from/to
 *			Poly -- The FPoly to serialize
 *
 * ============================================================================
 */
ENGINE_API FArchive& operator<<( FArchive& Ar, FPoly& Poly )
{
	guard(FPoly<<);
	Ar << AR_INDEX(Poly.NumVertices);
	Ar << Poly.Base << Poly.Normal << Poly.TextureU << Poly.TextureV;
	for( INT i=0; i<Poly.NumVertices; i++ )
		Ar << Poly.Vertex[i];
	Ar << Poly.PolyFlags;
	Ar << Poly.Actor << Poly.Material << Poly.ItemName;
	Ar << AR_INDEX(Poly.iLink) << AR_INDEX(Poly.iBrushPoly);

	Ar << Poly.LightMapScale;

	return Ar;
	unguard;
}

/* scion ======================================================================
 * operator<<
 * Author: jg
 * 
 * Rewrote to shrink all of the data structures used for BSP. I also removed
 * the InclusiveSphereBound member since it wasn't used anywhere.
 *
 * input:	Ar -- The archive to serialize from/to
 *			N -- The FBspNode to serialize
 *
 * ============================================================================
 */
ENGINE_API FArchive& operator<<( FArchive& Ar, FBspNode& N )
{
	guard(FBspNode<<);
	Ar << N.Plane << N.ZoneMask << N.NodeFlags << AR_INDEX(N.iVertPool);
	if (Ar.LicenseeVer() == XBOX_PACKAGE_FORMAT)
	{
		Ar << AR_INDEX(N.iSurf);
		Ar << AR_INDEX(N.iChild[0]) << AR_INDEX(N.iChild[1])
			<< AR_INDEX(N.iChild[2]);
		Ar << AR_INDEX(N.iCollisionBound) << AR_INDEX(N.iRenderBound);
		Ar << N.ExclusiveSphereBound;
		Ar << N.iZone[0] << N.iZone[1];
		Ar << N.NumVertices;
		Ar << N.iLeaf[0] << N.iLeaf[1];
		Ar << N.iSection << N.iFirstVertex << N.iLightMap;
	}
	else
	{
		INT iSurf = N.iSurf;
		Ar << AR_INDEX(iSurf);

		INT iChild[3] = { N.iChild[0], N.iChild[1], N.iChild[2] };
		Ar << AR_INDEX(iChild[0]) << AR_INDEX(iChild[1]) << AR_INDEX(iChild[2]);

		INT iRenderBound = N.iRenderBound;
		Ar << AR_INDEX(N.iCollisionBound) << AR_INDEX(iRenderBound);

		Ar << N.ExclusiveSphereBound;
		if (Ar.Ver() < 134)
		{
			FPlane DeadSphere;
			Ar << DeadSphere;
		}

		Ar << N.iZone[0] << N.iZone[1];
		Ar << N.NumVertices;

		INT iLeaf[2] = { N.iLeaf[0], N.iLeaf[1] };
		Ar << iLeaf[0] << iLeaf[1];

		INT iSection = N.iSection;
		INT iFirstVertex = N.iFirstVertex;
		INT iLightMap = N.iLightMap;
		Ar << iSection << iFirstVertex << iLightMap;

		if(!Ar.IsSaving() && !Ar.IsLoading())
			Ar << N.Projectors;

		if( Ar.IsLoading() )
		{
			N.NodeFlags &= ~(NF_IsNew|NF_IsFront|NF_IsBack);

			checkSlow(iSurf < 32767);
			N.iSurf = iSurf;

			checkSlow(iChild[0] < 32767);
			N.iChild[0] = iChild[0];
			checkSlow(iChild[1] < 32767);
			N.iChild[1] = iChild[1];
			checkSlow(iChild[2] < 32767);
			N.iChild[2] = iChild[2];

			checkSlow(iRenderBound < 32767);
			N.iRenderBound = iRenderBound;

			checkSlow(iLeaf[0] < 32767);
			N.iLeaf[0] = iLeaf[0];
			checkSlow(iLeaf[1] < 32767);
			N.iLeaf[1] = iLeaf[1];

			checkSlow(iSection < 32767);
			N.iSection = iSection;
			checkSlow(iFirstVertex < 32767);
			N.iFirstVertex = iFirstVertex;
			checkSlow(iLightMap < 32767);
			N.iLightMap = iLightMap;
		}
	}
	if (Ar.IsLoading())
	{
		N.NodeFlags &= ~(NF_IsNew|NF_IsFront|NF_IsBack);
	}
	return Ar;
	unguard;
}

/*---------------------------------------------------------------------------------------
	UModel object implementation.
---------------------------------------------------------------------------------------*/

void UModel::Serialize( FArchive& Ar )
{
	guard(UModel::Serialize);
	Super::Serialize( Ar );

	Ar << Vectors << Points << Nodes << Surfs << Verts << NumSharedSides << NumZones;
	for( INT i=0; i<NumZones; i++ )
		Ar << Zones[i];
	Ar << Polys;
	if( Polys && !Ar.IsTrans() )
	{
		Ar.Preload( Polys );
	}

	if(Ar.Ver() < 92)
	{
		TArray<FLightMapIndex>	LightMap;
		TArray<BYTE>			LightBits;

		Ar << LightMap << LightBits;
	}
	else if(Ar.Ver() < 105)
	{
		TArray<FLightMapIndex>	LightMapIndex;
		TArray<FLightBitmap>	LightBitmaps;

		Ar << LightMapIndex << LightBitmaps;
	}

	Ar << Bounds << LeafHulls << Leaves << Lights;
	Ar << RootOutside << Linked;

	if(Ar.Ver() < 92)
	{
	}
	else if(Ar.Ver() < 93)
	{
		FBspVertexStream	VertexStream;

		Ar << VertexStream;
	}
	else if(Ar.Ver() < 105)
	{
		TArray<FBspVertexStream>	VertexStreams;

		Ar << VertexStreams;
	}
	else
		Ar << Sections;

	if(Ar.Ver() >= 105 && Ar.Ver() < 107)
	{
		TArray<UTexture*>	OldTextures;
		Ar << OldTextures;
	}

	if(Ar.Ver() >= 105)
	{

// scion sz - 
// LightMap data not needed on Xbox
#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
		if( Ar.IsLoading() )
		{
			TArray<FLightMap> TempLightMaps;
			Ar << TempLightMaps;
		}
		else
		{
			LightMaps.Empty();
			Ar << LightMaps;
		}
#else
		Ar << LightMaps;
#endif

		if(Ar.Ver() < 107 && Ar.IsLoading())
			LightMaps.Empty();
	}

	if(Ar.Ver() >= 107 && Ar.Ver() < 110)
	{
		INT	NumLightMapTextures = 0;
		Ar << NumLightMapTextures;
	}

	if(Ar.Ver() >= 110)
		Ar << LightMapTextures;

	if( !Ar.IsLoading() && !Ar.IsSaving() ){
		Ar << bInvalidLightsRemoved;
	}

	unguard;
}
void UModel::PostLoad()
{
	guard(UModel::PostLoad);
	bInvalidLightsRemoved=0;
	for( INT i=0; i<Nodes.Num(); i++ )
		Surfs(Nodes(i).iSurf).Nodes.AddItem(i);
	Super::PostLoad();

//jg -- TUmaArray fix up pass
#if defined(_XBOX) && defined(XBOX_ASSET_FORMAT)
	INT Count = 0;
	void* Data = NULL;
	// Fix each of the bsp sections
	for (INT Index = 0; Index < Sections.Num(); Index++)
	{
		// Don't fix up data that was serialized the old fashioned way
		if (Sections(Index).Vertices.Vertices.IsInXPR())
		{
			// Find the color stream data
			GetTUmaArrayFromXPR(*FString::Printf(UMODEL_BSP_VS_PF,GetName(),Index),
				Count,Data);
			// Now fix up the array with that
			Sections(Index).Vertices.Vertices.FixUpArray(Count,Data);
		}
	}
#endif
	unguard;
}
void UModel::ModifySurf( INT Index, INT UpdateMaster )
{
	guard(UModel::ModifySurf);

	Surfs.ModifyItem( Index );
	FBspSurf& Surf = Surfs(Index);
	if( UpdateMaster && Surf.Actor )
		Surf.Actor->Brush->Polys->Element.ModifyItem( Surf.iBrushPoly );

	unguard;
}
void UModel::ModifyAllSurfs( INT UpdateMaster )
{
	guard(UModel::ModifyAllSurfs);

	for( INT i=0; i<Surfs.Num(); i++ )
		ModifySurf( i, UpdateMaster );

	unguard;
}
void UModel::ModifySelectedSurfs( INT UpdateMaster )
{
	guard(UModel::ModifySelectedSurfs);

	for( INT i=0; i<Surfs.Num(); i++ )
		if( Surfs(i).PolyFlags & PF_Selected )
			ModifySurf( i, UpdateMaster );

	unguard;
}
void UModel::Destroy()
{
	guard(UModel::Destroy);
	// Ensure all projectors are destroyed
	for( INT i=0; i<Nodes.Num(); i++ )
	{
		FBspNode& Node = Nodes(i);
		INT j;
		while( (j=Node.Projectors.Num()) > 0)
		{
			Node.Projectors(j-1)->RenderInfo->RemoveReference();
			Node.Projectors(j - 1)->RemoveRef();
			Node.Projectors.Remove(j-1);
		}
	}
	Super::Destroy();
	unguard;
}

void UModel::Rename( const TCHAR* InName, UObject* NewOuter )
{
	guard(UModel::Rename);

	// Also rename the UPolys.
    if( NewOuter && Polys && Polys->GetOuter() == GetOuter() )
		Polys->Rename(Polys->GetName(), NewOuter);

    Super::Rename( InName, NewOuter );

	unguard;
}

IMPLEMENT_CLASS(UModel);

/*---------------------------------------------------------------------------------------
	UModel implementation.
---------------------------------------------------------------------------------------*/

//
// Lock a model.
//
void UModel::Modify( UBOOL DoTransArrays )
{
	guard(UModel::Modify);

	// Modify all child objects.
	//warning: Don't modify self because model contains a dynamic array.
	if( Polys   ) Polys->Modify();

	unguard;
}

//
// Empty the contents of a model.
//
void UModel::EmptyModel( INT EmptySurfInfo, INT EmptyPolys )
{
	guard(UModel::EmptyModel);

	// Ensure all projectors are destroyed
	for( INT i=0; i<Nodes.Num(); i++ )
	{
		FBspNode& Node = Nodes(i);
		INT j;
		while( (j=Node.Projectors.Num()) > 0)
		{
			Node.Projectors(j-1)->RenderInfo->RemoveReference();
			if( Node.Projectors(j-1) )
				Node.Projectors(j-1) = Node.Projectors(j-1)->RemoveRef();
			Node.Projectors.Remove(j-1);		
		}
	}

	Nodes			.Empty();
	Bounds			.Empty();
	LeafHulls		.Empty();
	Leaves			.Empty();
	Verts			.Empty();
	Lights			.Empty();
	LightMaps		.Empty();
	DynamicLightMaps.Empty();
	LightMapTextures.Empty();
	Sections		.Empty();

	if( EmptySurfInfo )
	{
		Vectors.Empty();
		Points.Empty();
		Surfs.Empty();
	}
	if( EmptyPolys )
	{
		Polys = new( GetOuter(), NAME_None, RF_Transactional )UPolys;
	}

	// Init variables.
	NumZones		= 0;
	NumSharedSides	= 4;
	NumZones = 0;
	for( INT i=0; i<FBspNode::MAX_ZONES; i++ )
	{
		Zones[i].ZoneActor    = NULL;
	}	

	unguard;
}


//
// Create a new model and allocate all objects needed for it.
//
UModel::UModel( ABrush* Owner, UBOOL InRootOutside )
:	RootOutside	( InRootOutside )
,	Surfs		( this )
,	Vectors		( this )
,	Points		( this )
,	Verts		( this )
,	Nodes		( this )
{
	guard(UModel::UModel);
	SetFlags( RF_Transactional );
	EmptyModel( 1, 1 );
	if( Owner )
	{
		Owner->Brush = this;
		Owner->InitPosRotScale();
	}	
	unguard;
}

//
// Build the model's bounds (min and max).
//
void UModel::BuildBound()
{
	guard(UModel::BuildBound);
	if( Polys && Polys->Element.Num() )
	{
		TArray<FVector> Points;
		for( INT i=0; i<Polys->Element.Num(); i++ )
			for( INT j=0; j<Polys->Element(i).NumVertices; j++ )
				Points.AddItem(Polys->Element(i).Vertex[j]);
		BoundingBox    = FBox( &Points(0), Points.Num() );
		BoundingSphere = FSphere( &Points(0), Points.Num() );
	}
	else BoundingBox = FBox(0);
	unguard;
}

//
// Transform this model by its coordinate system.
//
void UModel::Transform( ABrush* Owner )
{
	guard(UModel::Transform);
	check(Owner);

	Polys->Element.ModifyAllItems();

	FModelCoords Coords;
	FLOAT Orientation = Owner->BuildCoords( &Coords, NULL );
	for( INT i=0; i<Polys->Element.Num(); i++ )
		Polys->Element( i ).Transform( Coords, Owner->PrePivot, Owner->Location, Orientation );

	unguard;
}

//
// Returns whether a BSP leaf is potentially visible from another leaf.
//
UBOOL UModel::PotentiallyVisible( INT iLeaf1, INT iLeaf2 )
{
	// This is the amazing superfast patent-pending 1 cpu cycle potential visibility 
	// algorithm programmed by the great Tim Sweeney!
	return 1;
}

/*---------------------------------------------------------------------------------------
	UModel basic implementation.
---------------------------------------------------------------------------------------*/

//
// Shrink all stuff to its minimum size.
//
void UModel::ShrinkModel()
{
	guard(UModel::ShrinkModel);

	Vectors		.Shrink();
	Points		.Shrink();
	Verts		.Shrink();
	Nodes		.Shrink();
	Surfs		.Shrink();
	if( Polys     ) Polys    ->Element.Shrink();
	Bounds		.Shrink();
	LeafHulls	.Shrink();

	unguard;
}

//
//	BoxLeavesRecurse
//
static TArray<INT> NodeCache;

void BoxLeavesRecurse(UModel* Model,INT iNode,FVector Origin,FVector Extent,TArray<INT>&OutLeaves)
{

	if( NodeCache.Num() < Model->Nodes.Num() )
    {
        NodeCache.Add( Model->Nodes.Num() - NodeCache.Num() );
    }
	INT*	iNodes = &NodeCache(0);

	INT		Index = 0;

	iNodes[0] = iNode;


	float min = Min( Min( Extent.X, Extent.Y ), Extent.Z );
    float max = Max( Max( Extent.X, Extent.Y ), Extent.Z );

    if( (min * 2.0f) > max ) // treat box as sphere!
    {
        FLOAT PushOut = Extent.Size();
        while( Index >= 0 )
	    {
		    iNode = iNodes[Index--];
		    FBspNode& Node = Model->Nodes(iNode);
		    FLOAT Distance = Node.Plane.PlaneDot(Origin);
		    if(Distance < PushOut)
		    {
			    if(Node.iBack != INDEX_NONE)
				    iNodes[++Index] = Node.iBack;
			    else if(Node.iLeaf[0] != INDEX_NONE)
				    OutLeaves.AddItem(Node.iLeaf[0]);
		    }

		    if(-Distance < PushOut)
		    {
			    if(Node.iFront != INDEX_NONE)
				    iNodes[++Index] = Node.iFront;
			    else if(Node.iLeaf[1] != INDEX_NONE)
				    OutLeaves.AddItem(Node.iLeaf[1]);
		    }
	    }
    }
    else
    {
	    while( Index >= 0 )
	    {
		    iNode = iNodes[Index--];

		    FBspNode& Node			= Model->Nodes(iNode);
		    register FLOAT PushOut	= FBoxPushOut(Node.Plane,Extent);
		    register FLOAT Distance	= Node.Plane.PlaneDot(Origin);

		    if(Distance < PushOut)
		    {
			    if(Node.iBack != INDEX_NONE)
				    iNodes[++Index] = Node.iBack;
			    else if(Node.iLeaf[0] != INDEX_NONE)
				    OutLeaves.AddItem(Node.iLeaf[0]);
		    }

		    if(-Distance < PushOut)
		    {
			    if(Node.iFront != INDEX_NONE)
				    iNodes[++Index] = Node.iFront;
			    else if(Node.iLeaf[1] != INDEX_NONE)
				    OutLeaves.AddItem(Node.iLeaf[1]);
		    }
	    }
    }
}

//
//	UModel::BoxLeaves
//	Determine the leaves touched by a bounding box.
//
// scion sz - pass array as param to avoid copy
void UModel::BoxLeaves(TArray<INT>& Result,FBox Box)
{
	guard(UModel::BoxLeaves);

	FVector		Origin, Extent;
	Box.GetCenterAndExtents( Origin, Extent );

	if(Nodes.Num())
		BoxLeavesRecurse(this,0,Origin,Extent,Result);

	unguardf((TEXT("%s (%f %f %f) (%f %f %f)"),GetPathName(),Box.Min.X,Box.Min.Y,Box.Min.Z,Box.Max.X,Box.Max.Y,Box.Max.Z));
}

//
//	UModel::Render
//

void UModel::Render(FDynamicActor* Owner,FLevelSceneNode* SceneNode,FRenderInterface* RI)
{
	guard(UModel::Render);

	ABrush*	Actor = Cast<ABrush>(Owner->Actor);
#ifndef _XBOX
	ULevel*	Level = SceneNode->Viewport->Actor->GetLevel();
#endif
	UModel*	Model = Actor->Brush;

	// Only draw brushes in wireframe viewports.

#ifndef _XBOX
	if(GIsEditor && Actor == Level->Brush())
	{
		if(!(SceneNode->Viewport->Actor->ShowFlags & SHOW_Brush) != 0)
			return;
	}
	else
#endif
		if(Actor->IsStaticBrush())
	{
		if(!SceneNode->Viewport->IsWire() && !Actor->bSelected)
			return;
	}

	// Figure out drawing colors.

	UEngine*	Engine = SceneNode->Viewport->GetOuterUClient()->Engine;
	FColor		WireColor,
				VertexColor,
				PivotColor;

#ifndef _XBOX
	if(GIsEditor && Actor == Level->Brush())
		WireColor =										Engine->C_BrushWire;
	else 
#endif
		if(Actor->IsStaticBrush())
		WireColor = Actor->bColored ?					Actor->BrushColor :
					Actor->CsgOper == CSG_Subtract ?	Engine->C_SubtractWire :
					Actor->CsgOper != CSG_Add ?			Engine->C_GreyWire :
					(Actor->PolyFlags & PF_Portal) ?	Engine->C_SemiSolidWire :
					(Actor->PolyFlags & PF_NotSolid) ?	Engine->C_NonSolidWire :
					(Actor->PolyFlags & PF_Semisolid) ?	Engine->C_ScaleBoxHi :
														Engine->C_AddWire;
	else if(Actor->IsVolumeBrush())
		WireColor =										Engine->C_Volume;

	if(!Actor->bSelected || !(SceneNode->Viewport->Actor->ShowFlags & SHOW_SelectionHighlight))
		WireColor = FColor(WireColor.Plane() * 0.5f);

	VertexColor = FColor(WireColor.Plane() * 1.2f);
	PivotColor = WireColor;

	// Calculate the local to world transform.

	FMatrix	LocalToWorld = Actor->LocalToWorld();

	// Set the local to world transform.

	RI->SetTransform(TT_LocalToWorld,LocalToWorld);

	// Set rendering modes.

	RI->EnableLighting(0, 1);

	DECLARE_STATIC_UOBJECT(
		UShader,
		BrushWireframeShader,
		{
		}
		);

	RI->SetMaterial(BrushWireframeShader);

	// Draw all edges.

	FLineBatcher	LineBatcher(RI,0);

	PUSH_HIT(SceneNode->Viewport,HActor(Actor));

	for(INT PolygonIndex = 0;PolygonIndex < Model->Polys->Element.Num();PolygonIndex++)
	{
		FPoly&	Poly = Model->Polys->Element(PolygonIndex);

		for(INT VertexIndex = 0;VertexIndex < Poly.NumVertices;VertexIndex++)
			LineBatcher.DrawLine(Poly.Vertex[VertexIndex],Poly.Vertex[(VertexIndex + 1) % Poly.NumVertices],WireColor);
	}

	LineBatcher.Flush();

	POP_HIT(SceneNode->Viewport);

	// Draw all vertices.

#ifdef _XBOX
	if(Actor->bSelected)
#else
	if((GIsEditor && Actor == Level->Brush()) || Actor->bSelected)
#endif
	{
		DECLARE_STATIC_UOBJECT( UFinalBlend, VertexBlend, {} );
		VertexBlend->Material = Cast<UTexture>(UObject::StaticFindObject( UTexture::StaticClass(), ANY_PACKAGE, TEXT("Engine.LargeVertex") ));
		check(VertexBlend->Material);
		VertexBlend->TwoSided = 0;
		VertexBlend->ZTest = 0;

		// Draw the brush vertices.

		FCanvasUtil	CanvasUtil(&SceneNode->Viewport->RenderTarget,RI);

		for(INT PolygonIndex = 0;PolygonIndex < Model->Polys->Element.Num();PolygonIndex++)
		{
			FPoly&	Poly = Model->Polys->Element(PolygonIndex);

			for(INT VertexIndex = 0;VertexIndex < Poly.NumVertices;VertexIndex++)
			{
				FVector	V = CanvasUtil.ScreenToCanvas.TransformFVector(SceneNode->Project(LocalToWorld.TransformFVector(Poly.Vertex[VertexIndex])));
				PUSH_HIT(SceneNode->Viewport,HBrushVertex(Actor,LocalToWorld.TransformFVector(Poly.Vertex[VertexIndex])));
				if( SceneNode->Viewport->bShowLargeVertices && Actor->bSelected )
					CanvasUtil.DrawTile(
						V.X - (VertexBlend->MaterialUSize()/2),
						V.Y - (VertexBlend->MaterialVSize()/2),
						V.X + (VertexBlend->MaterialUSize()/2),
						V.Y + (VertexBlend->MaterialVSize()/2),
						0,0,
						VertexBlend->MaterialUSize(),VertexBlend->MaterialVSize(),
						V.Z,
						VertexBlend,
						VertexColor
						);
				else
					CanvasUtil.DrawPoint(V.X - 1,V.Y - 1,V.X + 1,V.Y + 1,V.Z,VertexColor);

				if(SceneNode->Viewport->HitTesting)
					CanvasUtil.Flush();

				POP_HIT(SceneNode->Viewport);
			}
		}

		CanvasUtil.Flush();

		// Draw the brush pivot point.

		FVector	PivotPoint = LocalToWorld.TransformFVector(Actor->PrePivot);

		FVector	V = CanvasUtil.ScreenToCanvas.TransformFVector(SceneNode->Project(PivotPoint));
		PUSH_HIT(SceneNode->Viewport,HBrushVertex(Actor,PivotPoint));
		if( SceneNode->Viewport->bShowLargeVertices )
			CanvasUtil.DrawTile(
				V.X - (VertexBlend->MaterialUSize()/2),
				V.Y - (VertexBlend->MaterialVSize()/2),
				V.X + (VertexBlend->MaterialUSize()/2),
				V.Y + (VertexBlend->MaterialVSize()/2),
				0,0,
				VertexBlend->MaterialUSize(),VertexBlend->MaterialVSize(),
				V.Z,
				VertexBlend,
				VertexColor
				);
		else
			CanvasUtil.DrawPoint(V.X - 1,V.Y - 1,V.X + 1,V.Y + 1,V.Z,PivotColor);

		CanvasUtil.Flush();

		POP_HIT(SceneNode->Viewport);
	}

#ifdef WITH_KARMA
	// If this is a blocking volume set up to kBlockKarma - draw the collision.
	if(Actor->bBlockKarma && Actor->getKModel())
	{
		McdModelID model = Actor->getKModel();
		KModelDraw(model, KGData->DebugDrawOpt, KLineDraw);
	}
#endif

	unguard;
}

//
// UModel::AttachProjector
//
FStaticProjectorInfo* UModel::AttachProjector( INT iNode, FProjectorRenderInfo* RenderInfo, FPlane* FrustumPlanes, FStaticProjectorInfo* InProjectorInfo )
{
	guard(UModel::AttachProjector);

	FBspNode&	Node = Nodes(iNode);
	FBspSurf&	Surf = Surfs(Node.iSurf);

	// scion sz - don't project on portals and invisible surfaces
	if( Surf.PolyFlags & PF_Invisible ||
		Node.NodeFlags & NF_NotVisBlocking )
		return InProjectorInfo;

#if DYNAMIC_PROJ_STREAM
	FVector		WorldDirection = RenderInfo->Projector->Rotation.Vector();
	FLOAT		InvMaxTraceDistance = 1.0f / RenderInfo->Projector->MaxTraceDistance;
#endif

	// Remove old projectors.

	for(INT ProjectorIndex = 0;ProjectorIndex < Node.Projectors.Num();ProjectorIndex++)
		if(!Node.Projectors(ProjectorIndex)->RenderInfo->Render(RenderInfo->Projector->Level->TimeSeconds))
		{
			Node.Projectors(ProjectorIndex) = Node.Projectors(ProjectorIndex)->RemoveRef();
			Node.Projectors.Remove(ProjectorIndex--);
		}

#if DYNAMIC_PROJ_STREAM
	FStaticProjectorInfo* ProjectorInfo = NEW FStaticProjectorInfo;
#else
	// scion sz - try to re-use the existing static projector for better batching, or
	// create a static projector with the node's unclipped vertices.
	FStaticProjectorInfo* ProjectorInfo = (InProjectorInfo && InProjectorInfo->RenderInfo == RenderInfo 
		/*&& InProjectorInfo->BaseMaterial == Surf.Material*/) ? InProjectorInfo : NEW FStaticProjectorInfo;
	if( ProjectorInfo == InProjectorInfo )
		ProjectorInfo->AddRef();
#endif

	// keep track of first vertex index for this projector's vertices
	INT StartVertexIdx = ProjectorInfo->Vertices.Num();

	for(INT VertexIndex = 0;VertexIndex < Node.NumVertices;VertexIndex++)
	{
		FStaticProjectorVertex*	DestVertex = new(ProjectorInfo->Vertices) FStaticProjectorVertex;

		DestVertex->WorldPosition = Points(Verts(Node.iVertPool + Node.NumVertices - VertexIndex - 1).pVertex);

		// scion sz - store normals
		DestVertex->Normal = Node.Plane;

#if DYNAMIC_PROJ_STREAM
		// Calculate the vertex's attenuation.
		FLOAT	DirectionalAttenuation = 1.0f;

		if(!(RenderInfo->ProjectorFlags & PRF_ProjectOnBackfaces))
			DirectionalAttenuation = Max(Node.Plane | -WorldDirection,0.0f);

		FLOAT	DistanceAttenuation = 1.0f;

		if(RenderInfo->ProjectorFlags & PRF_Gradient)
			DistanceAttenuation = Clamp(1.0f - ((DestVertex->WorldPosition - RenderInfo->Projector->Location) | WorldDirection) * InvMaxTraceDistance,0.0f,1.0f);

		DestVertex->Attenuation = DirectionalAttenuation * DistanceAttenuation;
		//scion jg -- Calculate the projected position only once
		DestVertex->ProjectedPosition = RenderInfo->Matrix.TransformFVector(
			DestVertex->WorldPosition);
#endif
	}

// scion sz - not used
#if 0
	if(RenderInfo->MaterialBlendingOp != PB_None)
		ProjectorInfo->BaseMaterial = Surf.Material;
	else
	{	
		UMaterial*		BaseMaterial = Surf.Material;
		UShader*		BaseShader = Cast<UShader>(BaseMaterial);
		UTexture*		BaseTexture = Cast<UTexture>(BaseMaterial);	
		UFinalBlend*	BaseFinalBlend = Cast<UFinalBlend>(BaseMaterial);

		if(BaseTexture && (BaseTexture->bMasked || BaseTexture->bAlphaTexture))
			ProjectorInfo->BaseMaterial = BaseMaterial;
		else if(BaseShader)
		{
			if(BaseShader->Opacity)
				ProjectorInfo->BaseMaterial = BaseMaterial;

			ProjectorInfo->TwoSided = BaseShader->TwoSided;
		}
		else if(BaseFinalBlend && BaseFinalBlend->FrameBufferBlending == FB_AlphaBlend)
		{
			ProjectorInfo->BaseMaterial = BaseMaterial;
			ProjectorInfo->TwoSided = BaseFinalBlend->TwoSided;
		}
	}
#else
	ProjectorInfo->BaseMaterial = Surf.Material;
	ProjectorInfo->TwoSided = 0;
#endif

	if(ProjectorInfo->BaseMaterial)
	{
		FVector	TextureBase = Points(Surf.pBase),
				TextureX = Vectors(Surf.vTextureU) / ProjectorInfo->BaseMaterial->MaterialUSize(),
				TextureY = Vectors(Surf.vTextureV) / ProjectorInfo->BaseMaterial->MaterialVSize();

		ProjectorInfo->BaseUVs.Add(ProjectorInfo->Vertices.Num()-StartVertexIdx);

		for(INT VertexIndex = StartVertexIdx;VertexIndex < ProjectorInfo->Vertices.Num();VertexIndex++)
		{
			ProjectorInfo->BaseUVs(VertexIndex).U = (ProjectorInfo->Vertices(VertexIndex).WorldPosition - TextureBase) | TextureX;
			ProjectorInfo->BaseUVs(VertexIndex).V = (ProjectorInfo->Vertices(VertexIndex).WorldPosition - TextureBase) | TextureY;
		}
	}


	if(FrustumPlanes)
	{
		// Clip the node to the projection frustum

		for(INT PlaneIndex = 0;PlaneIndex < 6;PlaneIndex++)
		{
			TArray<FLOAT>	Dots(ProjectorInfo->Vertices.Num());

			for(INT VertexIndex = StartVertexIdx;VertexIndex < ProjectorInfo->Vertices.Num();VertexIndex++)
				Dots(VertexIndex) = FrustumPlanes[PlaneIndex].PlaneDot(ProjectorInfo->Vertices(VertexIndex).WorldPosition);

			INT	PrevVertexIndex = ProjectorInfo->Vertices.Num() - 1;
			for(INT VertexIndex = StartVertexIdx;VertexIndex < ProjectorInfo->Vertices.Num();VertexIndex++)
			{
				FStaticProjectorVertex	PrevVertex = ProjectorInfo->Vertices(PrevVertexIndex);
				FStaticProjectorVertex	Vertex = ProjectorInfo->Vertices(VertexIndex);
				FLOAT					Dot = Dots(VertexIndex),
										PrevDot = Dots(PrevVertexIndex);

				if(Dot * PrevDot < 0.0f)
				{
					// sign change, this plane clips this node's edge
					FLOAT	Time = -PrevDot / (Dot - PrevDot);
					ProjectorInfo->Vertices.Insert(VertexIndex);
					ProjectorInfo->Vertices(VertexIndex).WorldPosition = PrevVertex.WorldPosition + (Vertex.WorldPosition - PrevVertex.WorldPosition) * Time;
#if DYNAMIC_PROJ_STREAM
					ProjectorInfo->Vertices(VertexIndex).Attenuation = PrevVertex.Attenuation + (Vertex.Attenuation - PrevVertex.Attenuation) * Time;
					//scion jg -- Calculate the projected position only once
					ProjectorInfo->Vertices(VertexIndex).ProjectedPosition = PrevVertex.ProjectedPosition +
						(Vertex.ProjectedPosition - PrevVertex.ProjectedPosition) * Time;
#endif
					if(ProjectorInfo->BaseMaterial)
					{
						ProjectorInfo->BaseUVs.Insert(VertexIndex);
						ProjectorInfo->BaseUVs(VertexIndex).U = ProjectorInfo->BaseUVs(VertexIndex).U + (ProjectorInfo->BaseUVs(VertexIndex).U - ProjectorInfo->BaseUVs(PrevVertexIndex).U) * Time;
						ProjectorInfo->BaseUVs(VertexIndex).V = ProjectorInfo->BaseUVs(VertexIndex).V + (ProjectorInfo->BaseUVs(VertexIndex).V - ProjectorInfo->BaseUVs(PrevVertexIndex).V) * Time;
					}

					Dots.Insert(VertexIndex);
					Dots(VertexIndex) = 0.0f;
					VertexIndex++;
				}

				PrevVertexIndex = VertexIndex;
			}

			// Remove clipped away vertices.
			INT	RemoveVertexIndex = StartVertexIdx;
			for(INT DotIndex = StartVertexIdx;DotIndex < Dots.Num();DotIndex++)
			{
				if(Dots(DotIndex) < 0.0f)
					ProjectorInfo->Vertices.Remove(RemoveVertexIndex);
				else
					RemoveVertexIndex++;
			}		
		}
	}

	// scion sz -
	// store indices for this static projector geometry
	// these are needed since they are no longer generated dynamically
	INT NumIndices = Max<INT>( 0, (ProjectorInfo->Vertices.Num()-StartVertexIdx - 2) * 3 );
	INT StartIndex = ProjectorInfo->Indices.Num();
	ProjectorInfo->Indices.AddZeroed( NumIndices );
	for( INT VertexIdx=StartVertexIdx+2, Idx=StartIndex; Idx < ProjectorInfo->Indices.Num(); VertexIdx++ )
	{
		ProjectorInfo->Indices(Idx++) = StartVertexIdx;
		ProjectorInfo->Indices(Idx++) = VertexIdx - 1;
		ProjectorInfo->Indices(Idx++) = VertexIdx;
	}

	if( (ProjectorInfo->Vertices.Num()-StartVertexIdx) > 0 )
	{
		ProjectorInfo->RenderInfo = RenderInfo->AddReference();
		Node.Projectors.AddItem(ProjectorInfo);
	}
	else
		ProjectorInfo = ProjectorInfo->RemoveRef();

	return ProjectorInfo;

	unguard;
}

//
//	UModel::ClearRenderData
//

void UModel::ClearRenderData(URenderDevice* RenDev)
{
	guard(UModel::ClearRenderData);

	if(Sections.Num())
	{
		if(RenDev)
		{
			// Flush the render data from device memory.

			for(INT SectionIndex = 0;SectionIndex < Sections.Num();SectionIndex++)
			{
				if (Sections(SectionIndex).Vertices.GetCacheId() != INVALID_CACHEID)
				{
					RenDev->FlushResource(Sections(SectionIndex).Vertices.GetCacheId(),CH_VertexBuffer);
				}
			}
		}

		// Clear the render data.

		for(INT NodeIndex = 0;NodeIndex < Nodes.Num();NodeIndex++)
		{
			Nodes(NodeIndex).iSection = INDEX_NONE;
			Nodes(NodeIndex).iFirstVertex = INDEX_NONE;
		}

		Sections.Empty();
	}

	unguard;
}

/* scion ======================================================================
* UModel::HasInvalidLeafLights
* Author: sz
* 
* Returns true if any Lights are invalid
*
* =============================================================================
*/
UBOOL UModel::HasInvalidLeafLights()
{
	guard(UModel::HasInvalidLeafLights);
	UBOOL Result=0;
	if( !bInvalidLightsRemoved ) 
	{
		for( INT LightIdx=0; LightIdx < Lights.Num(); LightIdx++ )
		{
			if( Lights(LightIdx) == (AActor*)FLeaf::INVALID_LIGHT ) {
				Result = 1;
				break;
			}
		}
	}	
	return Result;
	unguard;
}

/* scion ======================================================================
* UModel::RemoveInvalidLeafLights
* Author: sz
* 
* Removes all the invalid lights from the Lights[] array. And, reassigns indices
* into the Lights[] array for all bsp leaves.
*
* =============================================================================
*/
struct FStartEndPair
{
	INT StardIdx,EndIdx;
};
struct FRemappedRanges
{
	FStartEndPair OldRange;
	FStartEndPair NewRange;
};
void UModel::RemoveInvalidLeafLights()
{
	guard(UModel::RemoveInvalidLeafLights);
	// remapped ranges so we can fixup leaf indices
	TArray<FRemappedRanges> Remappings;
	// new light list containing only valid lights
	TArray<AActor*> NewLights;
	// iterate over all lights
	for( INT LightIdx=0; LightIdx < Lights.Num(); LightIdx++ )
	{
		// keep track of the remapped ranges so that
		// the leaf indices can be fixed-up
		Remappings.Add();
		FRemappedRanges& Range = Remappings.Last();
		Range.OldRange.StardIdx = LightIdx;
		Range.NewRange.StardIdx = NewLights.Num();
		// iterate until NULL entry (end of range)
        while( Lights(LightIdx) && LightIdx < Lights.Num() )
		{
			// only add valid lights to the new list
			if( Lights(LightIdx) != (AActor*)FLeaf::INVALID_LIGHT )
			{
				NewLights.AddItem( Lights(LightIdx) );
			}
            LightIdx++;
		}
		Range.OldRange.EndIdx = LightIdx;
		Range.NewRange.EndIdx = NewLights.Num();
		NewLights.AddItem( NULL );
	}
	debugf( NAME_DevSam, TEXT("BSP: removed invalid leaf lights OldLights=%d NewLights=%d"), Lights.Num(), NewLights.Num() );
	// copy new lights
	Lights = NewLights;
	// remap leaf permeating light indices to match the NewLights[] entries
	for( INT LeafIdx=0; LeafIdx < Leaves.Num(); LeafIdx++ )
	{
		FLeaf& Leaf = Leaves(LeafIdx);
		if( Leaf.iPermeating >= 0 )
		{
			// find its matching pair of ranges
			UBOOL bFound=0;
			for( INT i=0; i<Remappings.Num() && !bFound; i++ )
			{
				FRemappedRanges& Range = Remappings(i);
				if( Leaf.iPermeating >= Range.OldRange.StardIdx &&
					Leaf.iPermeating <= Range.OldRange.EndIdx )
				{
					Leaf.iPermeating = Range.NewRange.StardIdx;
					bFound=1;
				}				
			}
			if( !bFound )
			{
				Leaf.iPermeating = -1;
				debugf( TEXT("Invalid leaf iPermeating index!") );
			}
		}
	}
	// should only need to do this once
	bInvalidLightsRemoved=1;
    
	unguard;
}

//
//  UModel::BuildRenderData
//

void UModel::BuildRenderData()
{
	guard(UModel::BuildRenderData);

#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
	// Don't ever rebuild on xbox or during asset conversion
	return;
#endif

	ClearRenderData(NULL);

	// Construct the sections.

	for(INT NodeIndex = 0;NodeIndex < Nodes.Num();NodeIndex++)
	{
		FBspNode&	Node = Nodes(NodeIndex);
		FBspSurf&	Surf = Surfs(Node.iSurf);
		FLightMap*	LightMap = Node.iLightMap != INDEX_NONE ? &LightMaps(Node.iLightMap) : NULL;
		UMaterial*	Material = Surf.Material ? Surf.Material : CastChecked<UMaterial>(UMaterial::StaticClass()->GetDefaultObject())->DefaultMaterial;
		DWORD		PolyFlags = Surf.PolyFlags & (PF_Unlit | PF_Selected | PF_TwoSided);
		INT			iLightMapTexture = LightMap ? LightMap->iTexture : INDEX_NONE;

		if(Node.NumVertices > 0)
		{
			// Find or create a section for this node.

			FBspSection*	Section = NULL;

			for(INT SectionIndex = 0;SectionIndex < Sections.Num();SectionIndex++)
				if(Sections(SectionIndex).Material == Material && Sections(SectionIndex).PolyFlags == PolyFlags && Sections(SectionIndex).iLightMapTexture == iLightMapTexture && Sections(SectionIndex).Vertices.Vertices.Num() + Node.NumVertices < MAXWORD)
				{
					Section = &Sections(SectionIndex);
					break;
				}

			if(!Section)
			{
				Section = new(Sections) FBspSection();
				Section->Material = Material;
				Section->PolyFlags = PolyFlags;
				Section->iLightMapTexture = iLightMapTexture;
			}

			Node.iSection = Section - &Sections(0);
			Node.iFirstVertex = Section->Vertices.Vertices.Add(Node.NumVertices);

			FCoords		TexCoords = FCoords(
										Points(Surf.pBase),
										Vectors(Surf.vTextureU),
										Vectors(Surf.vTextureV),
										Vectors(Surf.vNormal)
										);
			FMatrix		WorldToTexture = TexCoords.Matrix();
			FLOAT		MaterialUSize = Material->MaterialUSize(),
						MaterialVSize = Material->MaterialVSize();

			FBspVertex*	Vertices = &Section->Vertices.Vertices(Node.iFirstVertex);

			FLOAT	AverageU = 0.0f,
					AverageV = 0.0f;

			for(INT VertexIndex = 0;VertexIndex < Node.NumVertices;VertexIndex++)
			{
				FVector	WorldPosition = Points(Verts(Node.iVertPool + VertexIndex).pVertex),
						TexturePosition = WorldToTexture.TransformFVector(WorldPosition);

				Vertices[VertexIndex].Position = WorldPosition;
				Vertices[VertexIndex].U = TexturePosition.X / MaterialUSize;
				Vertices[VertexIndex].V = TexturePosition.Y / MaterialVSize;

				AverageU += Vertices[VertexIndex].U;
				AverageV += Vertices[VertexIndex].V;

				if(LightMap)
				{
					FVector	LightMapPosition = LightMap->WorldToLightMap.TransformFVector(Vertices[VertexIndex].Position);

					Vertices[VertexIndex].U2 = ((FLOAT) LightMap->OffsetX + LightMapPosition.X) / LIGHTMAP_TEXTURE_WIDTH;
					Vertices[VertexIndex].V2 = ((FLOAT) LightMap->OffsetY + LightMapPosition.Y) / LIGHTMAP_TEXTURE_HEIGHT;
				}
				
				Vertices[VertexIndex].Normal = Node.Plane;
			}

			FLOAT	IntMidU = appRound(AverageU / Node.NumVertices),
					IntMidV = appRound(AverageV / Node.NumVertices);

			for(INT VertexIndex = 0;VertexIndex < Node.NumVertices;VertexIndex++)
			{
				Vertices[VertexIndex].U -= IntMidU;
				Vertices[VertexIndex].V -= IntMidV;
			}

			Section->NumNodes++;
			Section->Vertices.Revision++;
		}
	}

	unguard;
}

/*---------------------------------------------------------------------------------------
	The End.
---------------------------------------------------------------------------------------*/

