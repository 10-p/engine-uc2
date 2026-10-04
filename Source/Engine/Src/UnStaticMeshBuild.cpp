/*=============================================================================
	UnStaticMeshBuild.cpp: Static mesh building.
	Copyright 1997-2002 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/

#include "EnginePrivate.h"

//scion jg -- This code shouldn't be run on the xbox
#if !defined(_XBOX) && !defined(XBOX_ASSET_FORMAT)

//
//	PointsEqual
//

inline UBOOL PointsEqual(FVector& V1,FVector& V2)
{
	if(Abs(V1.X - V2.X) > THRESH_POINTS_ARE_SAME * 4.0f)
		return 0;

	if(Abs(V1.Y - V2.Y) > THRESH_POINTS_ARE_SAME * 4.0f)
		return 0;

	if(Abs(V1.Z - V2.Z) > THRESH_POINTS_ARE_SAME * 4.0f)
		return 0;

	return 1;
}

//
//	NormalsEqual
//

inline UBOOL NormalsEqual(FVector& V1,FVector& V2)
{
	if(Abs(V1.X - V2.X) > THRESH_NORMALS_ARE_SAME * 4.0f)
		return 0;

	if(Abs(V1.Y - V2.Y) > THRESH_NORMALS_ARE_SAME * 4.0f)
		return 0;

	if(Abs(V1.Z - V2.Z) > THRESH_NORMALS_ARE_SAME * 4.0f)
		return 0;

	return 1;
}

//
//	FindVertexIndex
//

INT FindVertexIndex(UStaticMesh* StaticMesh,FVector Position,FVector Normal,FColor Color,FStaticMeshUV* UVs,INT NumUVs)
{
	guard(FindVertexIndex);

	FLOAT	TolerableErrorU = 1.0f / 1024.0f,
			TolerableErrorV = 1.0f / 1024.0f;

	// Find any identical vertices already in the vertex buffer.

	INT	VertexBufferIndex = INDEX_NONE;

	for(INT VertexIndex = 0;VertexIndex < StaticMesh->VertexStream.Vertices.Num();VertexIndex++)
	{
		// Compare vertex position and normal.

		FStaticMeshVertex*	CompareVertex = &StaticMesh->VertexStream.Vertices(VertexIndex);

		if(!PointsEqual(CompareVertex->Position,Position))
			continue;

		if(!NormalsEqual(CompareVertex->Normal,Normal))
			continue;

		// Compare vertex color.

		if(StaticMesh->ColorStream.Colors(VertexIndex) != Color)
			continue;

		// Compare vertex UVs.

		UBOOL	UVsMatch = 1;

		for(INT UVIndex = 0;UVIndex < NumUVs;UVIndex++)
		{
			FStaticMeshUV*	CompareUV = &StaticMesh->UVStreams(UVIndex).UVs(VertexIndex);

			if(Abs(CompareUV->U - UVs[UVIndex].U) > TolerableErrorU)
			{
				UVsMatch = 0;
				break;
			}

			if(Abs(CompareUV->V - UVs[UVIndex].V) > TolerableErrorV)
			{
				UVsMatch = 0;
				break;
			}
		}

		if(!UVsMatch)
			continue;

		// The vertex matches!

		VertexBufferIndex = VertexIndex;
		break;
	}

	// If there is no identical vertex already in the vertex buffer...

	if(VertexBufferIndex == INDEX_NONE)
	{
		// Add a new vertex to the vertex streams.

		FStaticMeshVertex	Vertex;

		Vertex.Position = Position;
		Vertex.Normal = Normal;

		VertexBufferIndex = StaticMesh->VertexStream.Vertices.AddItem(Vertex);

		verify(StaticMesh->ColorStream.Colors.AddItem(Color) == VertexBufferIndex);
		verify(StaticMesh->AlphaStream.Colors.AddItem(FColor(255,255,255,Color.A)) == VertexBufferIndex);

		for(INT UVIndex = 0;UVIndex < NumUVs;UVIndex++)
			verify(StaticMesh->UVStreams(UVIndex).UVs.AddItem(UVs[UVIndex]) == VertexBufferIndex);

		for(INT UVIndex = NumUVs;UVIndex < StaticMesh->UVStreams.Num();UVIndex++)
			verify(StaticMesh->UVStreams(UVIndex).UVs.AddZeroed() == VertexBufferIndex);
	}

	return VertexBufferIndex;

	unguard;
}

//
//	FStaticMeshEdge
//

struct FStaticMeshEdge
{
	INT	Vertices[2];
	INT	Triangles[2];
};

//
//	FindEdgeIndex
//

INT FindEdgeIndex(TArray<FStaticMeshEdge>& Edges,FStaticMeshEdge& Edge)
{
	guard(FindEdgeIndex);

	for(INT EdgeIndex = 0;EdgeIndex < Edges.Num();EdgeIndex++)
	{
		FStaticMeshEdge&	OtherEdge = Edges(EdgeIndex);

		if(OtherEdge.Vertices[0] == Edge.Vertices[1] && OtherEdge.Vertices[1] == Edge.Vertices[0])
		{
			OtherEdge.Triangles[1] = Edge.Triangles[0];

			return EdgeIndex;
		}
	}

	new(Edges) FStaticMeshEdge(Edge);

	return Edges.Num() - 1;

	unguard;
}

//
//	ClassifyTriangleVertices
//

ESplitType ClassifyTriangleVertices(FPlane Plane,FVector* Vertices)
{
	ESplitType	Classification = SP_Coplanar;

	for(INT VertexIndex = 0;VertexIndex < 3;VertexIndex++)
	{
		FLOAT	Dist = Plane.PlaneDot(Vertices[VertexIndex]);

		if(Dist < -0.0001f)
		{
			if(Classification == SP_Front)
				Classification = SP_Split;
			else if(Classification != SP_Split)
				Classification = SP_Back;
		}
		else if(Dist >= 0.0001f)
		{
			if(Classification == SP_Back)
				Classification = SP_Split;
			else if(Classification != SP_Split)
				Classification = SP_Front;
		}
	}

	return Classification;
}
#endif

//
//	BuildCollisionBsp
//

INT BuildCollisionBsp(UStaticMesh* StaticMesh,INT* TriangleIndices,INT NumTriangles,BYTE* TriangleClassifications, TArray<FBox> &BoundingBoxes )
{
	guard(BuildCollisionBsp);
#if USE_BSP_COLL //scion jg -- Removed since BSP collision is gone
	if(NumTriangles)
	{
		FMemMark			MemMark(GMem);
		FStaticMeshVertex*	VertexStream = &StaticMesh->VertexStream.Vertices(0);

		// Choose a triangle to split the rest.

		INT	BestSplitterIndex = TriangleIndices[0],
			BestScore = MAXINT,
			BestNumFront = 0,
			BestNumBack = 0,
			BestNumCoplanar = 0,
			Inc = Max(1,NumTriangles / 60);

		for(INT TriangleIndex = 0;TriangleIndex < NumTriangles;TriangleIndex += Inc)
		{
			INT								SplitterIndex = TriangleIndices[TriangleIndex];
			INT								NumFront = 0,
											NumBack = 0,
											NumCoplanar = 0;

			for(INT OtherTriangleIndex = 0;OtherTriangleIndex < NumTriangles;OtherTriangleIndex++)
			{
				ESplitType	Classification = (ESplitType) TriangleClassifications[SplitterIndex * StaticMesh->CollisionTriangles.Num() + TriangleIndices[OtherTriangleIndex]];

				if(Classification == SP_Front || Classification == SP_Split)
					NumFront++;

				if(Classification == SP_Back || Classification == SP_Split)
					NumBack++;

				if(Classification == SP_Coplanar)
					NumCoplanar++;
			}

			INT	Score = NumFront + NumBack + NumCoplanar;

			if(Score < BestScore || (Score == BestScore && Abs(NumFront - NumBack) < Abs(BestNumFront - BestNumBack)))
			{
				BestSplitterIndex = TriangleIndices[TriangleIndex];
				BestNumFront = NumFront;
				BestNumBack = NumBack;
				BestNumCoplanar = NumCoplanar;
				BestScore = Score;
			}
		}

		// Create a node for the splitter.

		FStaticMeshCollisionTriangle*	Triangle = &StaticMesh->CollisionTriangles(BestSplitterIndex);
		INT							NodeIndex = StaticMesh->CollisionNodes.Num();
		FStaticMeshCollisionNode*	Node = new(StaticMesh->CollisionNodes) FStaticMeshCollisionNode;
		new(BoundingBoxes) FBox(0);

		Node->TriangleIndex = BestSplitterIndex;

		// Build a list of back, front and coplanar triangles relative to the node's plane.

		INT*	FrontTriangleIndices = New<INT>(GMem,BestNumFront);
		INT*	BackTriangleIndices = New<INT>(GMem,BestNumBack);
		INT*	CoplanarTriangleIndices = New<INT>(GMem,BestNumCoplanar);
		INT		NumFront = 0,
				NumBack = 0,
				NumCoplanar = 0;

		for(INT TriangleIndex = 0;TriangleIndex < NumTriangles;TriangleIndex++)
		{
			INT	SplitIndex = TriangleIndices[TriangleIndex];

			if(SplitIndex != BestSplitterIndex)
			{
				FStaticMeshCollisionTriangle*	SplitTriangle = &StaticMesh->CollisionTriangles(SplitIndex);
				FVector							Vertices[3];

				for(INT VertexIndex = 0;VertexIndex < 3;VertexIndex++)
					Vertices[VertexIndex] = VertexStream[SplitTriangle->VertexIndices[VertexIndex]].Position;

				ESplitType	Classification = (ESplitType) TriangleClassifications[BestSplitterIndex * StaticMesh->CollisionTriangles.Num() + SplitIndex];

				if(Classification == SP_Coplanar)
					CoplanarTriangleIndices[NumCoplanar++] = SplitIndex;

				if(Classification == SP_Front || Classification == SP_Split)
					FrontTriangleIndices[NumFront++] = SplitIndex;

				if(Classification == SP_Back || Classification == SP_Split)
					BackTriangleIndices[NumBack++] = SplitIndex;
			}
		}

		// Recursively split both sets of triangles.
		// Node pointer invalid after child BuildCollisionBsp calls cause the nodes array to be reallocated.
		// In addition to that, VC doesn't realize that the first BuildCollisionBsp call may change the nodes array,
		// so it caches the pointer to CollisionNodes(NodeIndex).
		// Yay!

		INT	BackChildIndex = BuildCollisionBsp(StaticMesh,BackTriangleIndices,NumBack,TriangleClassifications, BoundingBoxes),
			FrontChildIndex = BuildCollisionBsp(StaticMesh,FrontTriangleIndices,NumFront,TriangleClassifications, BoundingBoxes),
			CoplanarChildIndex = BuildCollisionBsp(StaticMesh,CoplanarTriangleIndices,NumCoplanar,TriangleClassifications, BoundingBoxes);

		StaticMesh->CollisionNodes(NodeIndex).CoplanarIndex = CoplanarChildIndex;
		StaticMesh->CollisionNodes(NodeIndex).ChildIndices[0] = BackChildIndex;
		StaticMesh->CollisionNodes(NodeIndex).ChildIndices[1] = FrontChildIndex;

		// Calculate the node's bounding box.
#if 0
		FBox	BoundingBox(0);

		if(CoplanarChildIndex != INDEX_NONE)
			BoundingBox += StaticMesh->CollisionNodes(CoplanarChildIndex).BoundingBox;

		if(BackChildIndex != INDEX_NONE)
			BoundingBox += StaticMesh->CollisionNodes(BackChildIndex).BoundingBox;

		if(FrontChildIndex != INDEX_NONE)
			BoundingBox += StaticMesh->CollisionNodes(FrontChildIndex).BoundingBox;

		for(INT VertexIndex = 0;VertexIndex < 3;VertexIndex++)
			BoundingBox += VertexStream[Triangle->VertexIndices[VertexIndex]].Position;

		StaticMesh->CollisionNodes(NodeIndex).BoundingBox = BoundingBox;
#else
		FBox& BoundingBox = BoundingBoxes(NodeIndex);

		if(CoplanarChildIndex != INDEX_NONE)
			BoundingBox += BoundingBoxes(CoplanarChildIndex);

		if(BackChildIndex != INDEX_NONE)
			BoundingBox += BoundingBoxes(BackChildIndex);

		if(FrontChildIndex != INDEX_NONE)
			BoundingBox += BoundingBoxes(FrontChildIndex);

		for(INT VertexIndex = 0;VertexIndex < 3;VertexIndex++)
			BoundingBox += VertexStream[Triangle->VertexIndices[VertexIndex]].Position;
#endif
		MemMark.Pop();

		return NodeIndex;
	}
	else
#endif
		return INDEX_NONE;

	unguard;
}

//
//	UStaticMesh::Build
//

void UStaticMesh::Build()
{
	guard(UStaticMesh::Build);

#if !defined(_XBOX) && !defined(XBOX_ASSET_FORMAT)

	GWarn->BeginSlowTask(*FString::Printf(TEXT("(%s) Building"),GetPathName()),1);

	// Mark the parent package as dirty.

	UObject* Outer = GetOuter();
	while( Outer && Outer->GetOuter() )
		Outer = Outer->GetOuter();
	if( Outer && Cast<UPackage>(Outer) )
		Cast<UPackage>(Outer)->bDirty = 1;

	// Clear old data.

	Sections.Empty();

	VertexStream.Vertices.Empty();
	ColorStream.Colors.Empty();
	AlphaStream.Colors.Empty();
	UVStreams.Empty();

	IndexBuffer.Indices.Empty();
	WireframeIndexBuffer.Indices.Empty();

	// Load the source data.

	if(!RawTriangles.Num())
		RawTriangles.Load();

	// Calculate triangle normals.

	TArray<FVector>	TriangleNormals(RawTriangles.Num());

	for(INT TriangleIndex = 0;TriangleIndex < RawTriangles.Num();TriangleIndex++)
	{
		FStaticMeshTriangle*	Triangle = &RawTriangles(TriangleIndex);

		TriangleNormals(TriangleIndex) = FPlane(
											Triangle->Vertices[2],
											Triangle->Vertices[1],
											Triangle->Vertices[0]
											);
	}

	// Initialize static mesh sections.

	TArray<FRawIndexBuffer>	SectionIndices;

	for(INT MaterialIndex = 0;MaterialIndex < Materials.Num();MaterialIndex++)
	{
		new(Sections) FStaticMeshSection();
		new(SectionIndices) FRawIndexBuffer();
	}

	// Create the necessary number of UV streams.

	for(INT TriangleIndex = 0;TriangleIndex < RawTriangles.Num();TriangleIndex++)
	{
		FStaticMeshTriangle*	Triangle = &RawTriangles(TriangleIndex);

		while(UVStreams.Num() < Triangle->NumUVs)
		{
			FStaticMeshUVStream*	UVStream = new(UVStreams) FStaticMeshUVStream();
			UVStream->CoordinateIndex = UVStreams.Num() - 1;
		};
	}

	//scion jg -- Array for building the kDOP tree
	TArray<FkDOPBuildCollisionTriangle> kDOPBuildTriangles;

	// Process each triangle.
    INT	NumDegenerates = 0; 
	for(INT TriangleIndex = 0;TriangleIndex < RawTriangles.Num();TriangleIndex++)
	{
		FStaticMeshTriangle*	Triangle = &RawTriangles(TriangleIndex);
		FStaticMeshSection*		Section = &Sections(Triangle->MaterialIndex);
		FStaticMeshMaterial*	Material = &Materials(Triangle->MaterialIndex);

        if( PointsEqual(Triangle->Vertices[0],Triangle->Vertices[1])
		    ||	PointsEqual(Triangle->Vertices[0],Triangle->Vertices[2])
		    ||	PointsEqual(Triangle->Vertices[1],Triangle->Vertices[2]) )
		{
            NumDegenerates++;
			continue;
		}

		GWarn->StatusUpdatef(TriangleIndex,RawTriangles.Num(),TEXT("(%s) Indexing vertices..."),GetPathName());

		// Calculate smooth vertex normals.

		FVector	Normals[3];

		for(INT VertexIndex = 0;VertexIndex < 3;VertexIndex++)
			Normals[VertexIndex] = FVector(0,0,0);

		for(INT OtherTriangleIndex = 0;OtherTriangleIndex < RawTriangles.Num();OtherTriangleIndex++)
		{
			FStaticMeshTriangle*	OtherTriangle = &RawTriangles(OtherTriangleIndex);

			if((Triangle->SmoothingMask & OtherTriangle->SmoothingMask) || (TriangleIndex == OtherTriangleIndex))
			{
				for(INT VertexIndex = 0;VertexIndex < 3;VertexIndex++)
				{
					for(INT OtherVertexIndex = 0;OtherVertexIndex < 3;OtherVertexIndex++)
					{
						if(PointsEqual(Triangle->Vertices[VertexIndex],OtherTriangle->Vertices[OtherVertexIndex]))
						{
							Normals[VertexIndex] += TriangleNormals(OtherTriangleIndex);
							break;
						}
					}
				}
			}
		}

		for(INT VertexIndex = 0;VertexIndex < 3;VertexIndex++)
			Normals[VertexIndex].Normalize();

		// Index the triangle's vertices.

		INT	VertexIndices[3];

		for(INT VertexIndex = 0;VertexIndex < 3;VertexIndex++)
			VertexIndices[VertexIndex] = FindVertexIndex(
											this,
											Triangle->Vertices[VertexIndex],
											Normals[VertexIndex],
											Triangle->Colors[VertexIndex],
											Triangle->UVs[VertexIndex],
											Triangle->NumUVs
											);

		// Reject degenerate triangles.

		if(VertexIndices[0] == VertexIndices[1] || VertexIndices[1] == VertexIndices[2] || VertexIndices[0] == VertexIndices[2])
			continue;

		// Put the indices in the section index buffer.

		for(INT VertexIndex = 0;VertexIndex < 3;VertexIndex++)
			SectionIndices(Triangle->MaterialIndex).Indices.AddItem(VertexIndices[VertexIndex]);

		Section->NumTriangles++;
		Section->NumPrimitives++;

		if(Material->EnableCollision && TriangleNormals(TriangleIndex).Size() > 0.0f)
		{
			// Create a collision triangle.

			//scion jg -- Build a new kDOP collision triangle
			new(kDOPBuildTriangles)FkDOPBuildCollisionTriangle(VertexIndices[0],
				VertexIndices[1],VertexIndices[2],Triangle);
		}
	}

	if(NumDegenerates)
    	debugf(TEXT("%s StaticMesh had %i degenerates"), GetName(), NumDegenerates );

	VertexStream.Revision++;
	ColorStream.Revision++;
	AlphaStream.Revision++;

	for(INT UVIndex = 0;UVIndex < UVStreams.Num();UVIndex++)
		UVStreams(UVIndex).Revision++;

	// Build a list of wireframe edges in the static mesh.
	debugf(TEXT("Creating wireframe indices for %s"),GetName());
	FRawIndexBuffer WireBase;
	for(INT SectionIndex = 0;SectionIndex < SectionIndices.Num();SectionIndex++)
	{
		FStaticMeshSection*	Section = &Sections(SectionIndex);

		GWarn->StatusUpdatef(SectionIndex,Sections.Num(),TEXT("(%s) Building wireframe view..."),GetPathName());

		if (SectionIndices(SectionIndex).Indices.Num())
		{
			Sections(SectionIndex).NumPrimitives = 
				SectionIndices(SectionIndex).Stripify(0,0);
			Sections(SectionIndex).IsStrip = 0;

			Section->FirstIndex = WireBase.Indices.Num();

			_WORD*	DestPtr = &WireBase.Indices(WireBase.Indices.Add(SectionIndices(SectionIndex).Indices.Num()));
			_WORD*	SrcPtr = &SectionIndices(SectionIndex).Indices(0);

			Section->MinVertexIndex = *SrcPtr;
			Section->MaxVertexIndex = *SrcPtr;

			for(INT Index = 0;Index < SectionIndices(SectionIndex).Indices.Num();Index++)
			{
				Section->MinVertexIndex = Min(*SrcPtr,Section->MinVertexIndex);
				Section->MaxVertexIndex = Max(*SrcPtr,Section->MaxVertexIndex);

				*DestPtr++ = *SrcPtr++;
			}
		}
	}
	TArray<FStaticMeshEdge>	Edges;
	for(INT TriangleIndex = 0;TriangleIndex < WireBase.Indices.Num() / 3;TriangleIndex++)
	{
		_WORD*	TriangleIndices = &WireBase.Indices(TriangleIndex * 3);

		for(INT EdgeIndex = 0;EdgeIndex < 3;EdgeIndex++)
		{
			FStaticMeshEdge	Edge;

			Edge.Vertices[0] = TriangleIndices[EdgeIndex];
			Edge.Vertices[1] = TriangleIndices[(EdgeIndex + 1) % 3];
			Edge.Triangles[0] = TriangleIndex;
			Edge.Triangles[1] = -1;

			FindEdgeIndex(Edges,Edge);
		}
	}

	for(INT EdgeIndex = 0;EdgeIndex < Edges.Num();EdgeIndex++)
	{
		FStaticMeshEdge&	Edge = Edges(EdgeIndex);

		WireframeIndexBuffer.Indices.AddItem(Edge.Vertices[0]);
		WireframeIndexBuffer.Indices.AddItem(Edge.Vertices[1]);
	}

	WireframeIndexBuffer.Revision++;

	// Build a cache optimized triangle list for each section and copy it into the shared index buffer.
	debugf(TEXT("Building %s %s for %s"),
		bCacheOptimize ? TEXT("cache optimized") : TEXT("index optimized"),
		UseTriStrips ? TEXT("tristrips") : TEXT("trilists"),GetName());
	for(INT SectionIndex = 0;SectionIndex < SectionIndices.Num();SectionIndex++)
	{
		FStaticMeshSection*	Section = &Sections(SectionIndex);

		GWarn->StatusUpdatef(SectionIndex,Sections.Num(),TEXT("(%s) Optimizing render data..."),GetPathName());

		if (SectionIndices(SectionIndex).Indices.Num())
		{
			//jg -- Build indices and optimize for perf or memory
			Sections(SectionIndex).NumPrimitives = 
				SectionIndices(SectionIndex).Stripify(bCacheOptimize,UseTriStrips);
			Sections(SectionIndex).IsStrip = UseTriStrips;

			Section->FirstIndex = IndexBuffer.Indices.Num();

			_WORD*	DestPtr = &IndexBuffer.Indices(IndexBuffer.Indices.Add(SectionIndices(SectionIndex).Indices.Num()));
			_WORD*	SrcPtr = &SectionIndices(SectionIndex).Indices(0);

			Section->MinVertexIndex = *SrcPtr;
			Section->MaxVertexIndex = *SrcPtr;

			for(INT Index = 0;Index < SectionIndices(SectionIndex).Indices.Num();Index++)
			{
				Section->MinVertexIndex = Min(*SrcPtr,Section->MinVertexIndex);
				Section->MaxVertexIndex = Max(*SrcPtr,Section->MaxVertexIndex);

				*DestPtr++ = *SrcPtr++;
			}
		}
	}

	IndexBuffer.Revision++;

	// Calculate the bounding box.

	BoundingBox = FBox(0);

	for(INT VertexIndex = 0;VertexIndex < VertexStream.Vertices.Num();VertexIndex++)
		BoundingBox += VertexStream.Vertices(VertexIndex).Position;

	BoundingSphere = FSphere(&BoundingBox.Min,2);

	//jg -- Calculate compression scale & inv scale
	ComputeCompressionScales();

	//scion jg -- Build the kDOP tree
	debugf(TEXT("Building kDOP collision for static mesh %s"),GetName());
	kDOPTree.Build(VertexStream.Vertices,kDOPBuildTriangles);

	GWarn->EndSlowTask();
#endif
	unguard;
}

/* scion ======================================================================
 * UStaticMesh::ComputeCompressionScales
 * Author: jg
 * 
 * Takes the existing bounding box, checks each vert for quantization errors,
 * and adjusts the bounding box/compression scales until there are no more
 * errors.
 *
 * ============================================================================
 */
void UStaticMesh::ComputeCompressionScales(void)
{
	guard(UStaticMesh::ComputeCompressionScales);
#if !defined(_XBOX) && !defined(XBOX_ASSET_FORMAT)
	debugf(TEXT("Checking for quantization errors for %s"),GetFullName());
	FBox Box(BoundingBox);
	UBOOL bDone = 0;
	// While we still have quantization errors
	while (bDone == 0)
	{
		// Use the center of the box as the way move verts to the origin
		CompressionOffset = Box.GetCenter();
		// Use the extent of the box as the normalization factor
		CompressionScale = Box.GetExtent();
		// Make sure none of the axis are zero
		CompressionScale.X = Max<FLOAT>(CompressionScale.X,0.000001f);
		CompressionScale.Y = Max<FLOAT>(CompressionScale.Y,0.000001f);
		CompressionScale.Z = Max<FLOAT>(CompressionScale.Z,0.000001f);
		CompressionInvScale.X = 1.f / CompressionScale.X;
		CompressionInvScale.Y = 1.f / CompressionScale.Y;
		CompressionInvScale.Z = 1.f / CompressionScale.Z;
		// Count how many errors there are
		INT nErrorCount = 0;
		// Use this to scale the box by if there are errors
		FLOAT MaxError = 0.f;
		// Now go through the triangles checking for quantization errors
		for (INT nVert = 0; nVert < VertexStream.Vertices.Num();
			nVert++)
		{
			// Grab the vert to check
			FVector& Check = VertexStream.Vertices(nVert).Position;
			// Check for quantization range errors
			FLOAT x = (Check.X  - CompressionOffset.X) *
				CompressionInvScale.X;
			FLOAT y = (Check.Y  - CompressionOffset.Y) *
				CompressionInvScale.Y;
			FLOAT z = (Check.Z  - CompressionOffset.Z) *
				CompressionInvScale.Z;
			if (x > 1.f || x < -1.f || y > 1.f || y < -1.f ||
				z > 1.f || z < -1.f)
			{
				nErrorCount++;
				MaxError = Max<FLOAT>(MaxError,
					Max3<FLOAT>(fabs(x),fabs(y),fabs(z)));
			}
		}
		// If there are errors scale the box up by the error amount and
		// run through the error checking again
		if (nErrorCount > 0)
		{
			debugf(TEXT("Found %d errors, scaling by %f"),nErrorCount,
				MaxError);
			Box = Box.ExpandBy(Box.GetExtent() * MaxError);
		}
		else
		{
			bDone = 1;
		}
	}
#endif
	unguard;
}
