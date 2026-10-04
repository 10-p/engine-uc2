/*=============================================================================
	UnRenderBatch.cpp: Render batching code.
	Copyright 2002 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/

#include "EnginePrivate.h"
#include "UnRenderPrivate.h"

/* scion ======================================================================
* class FProjectorPrimVertexStream
* Author: sz
* 
* Vertex stream interface for a single static projector primitive.  Provides
* direct access to the vertex stream data stored in the primitive.
* ============================================================================
*/
class FProjectorPrimVertexStream : public FVertexStream
{
public:

	FProjectorRenderPrimitive*	Primitive;
	INT	Size, Stride;

	FProjectorPrimVertexStream( FProjectorRenderPrimitive* InPrimitive,INT InSize )
	{
		Primitive = InPrimitive;
		Size = InSize;

		Stride = sizeof( FStaticProjectorVertex );
	}

	// FRenderResource interface.
	virtual INT GetRevision() { return 1; }

	// FVertexStream interface.
	virtual INT GetSize() { return Size; }
	virtual INT GetStride() { return Stride; }
	virtual INT GetComponents( FVertexComponent* Components )
	{
		INT	NumComponents = 0;

		Components[NumComponents].Type = CT_Float3;
		Components[NumComponents].Function = FVF_Position;
		NumComponents++;

#ifdef _XBOX
		// scion sz - added normals so we can attenuate on GPU.
		// used packed normals on xbox
		Components[NumComponents].Type = CT_NormPacked3;
		Components[NumComponents].Function = FVF_Normal;
		NumComponents++;
		// ProjectedPosition, Attenuation not needed since they are dynamically
		// generated on the GPU
#else
		Components[NumComponents].Type = CT_Float3;
		Components[NumComponents].Function = FVF_Normal;
		NumComponents++;		
#endif

#if DYNAMIC_PROJ_STREAM
		Components[NumComponents].Type = CT_Color;
		Components[NumComponents].Function = FVF_Diffuse;
		NumComponents++;

		Components[NumComponents].Type = CT_Float3;
		Components[NumComponents].Function = FVF_TexCoord0;
		NumComponents++;
#endif
		return NumComponents;
	}
	// direct access to vertex stream data
	virtual void GetRawStreamData( void** Dest, INT FirstVertex ) 
	{
		Primitive->GetRawVertices( Dest, FirstVertex );
	}
	// copy vertex data
	virtual void GetStreamData( void* Dest )
	{
		checkSlow( Primitive );
		Primitive->GetVertices( (BYTE*)Dest );
	}
};

/* scion ======================================================================
* class FProjectorPrimIndexBuffer
* Author: sz
* 
* Index buffer interface for a single static projector primitive.  Provides
* direct access to the index buffer data stored in the primitive.
* ============================================================================
*/
class FProjectorPrimIndexBuffer : public FIndexBuffer
{
public:

	FProjectorRenderPrimitive*	Primitive;
	INT	Size;

	FProjectorPrimIndexBuffer( FProjectorRenderPrimitive* InPrimitive, INT InSize )
	{
		Primitive = InPrimitive;
		Size = InSize;
	}

	// FRenderResource interface.
	virtual INT GetRevision() { return 1; }

	// FIndexBuffer interface.
	virtual INT GetSize() { return Size; }
	virtual INT GetIndexSize() { return sizeof(_WORD); }	
	// direct access to index buffer
	virtual void* GetContentsPtr(void)
	{
		void* RawIndices = NULL;
		Primitive->GetRawIndices( &RawIndices );
		return RawIndices;
	}
	// copy index data
	virtual void GetContents( void* Data )
	{
		Primitive->GetIndices( (_WORD*)Data, 0 );
	}
};


#if DYNAMIC_PROJ_STREAM
//
//	FCompositeVertexStream
//
class FCompositeVertexStream : public FVertexStream
{
public:

	FProjectorRenderBatchDef*	Batch;
	INT						Size,
							Stride;

	// Constructor.

	FCompositeVertexStream(FProjectorRenderBatchDef* InBatch,INT InSize)
	{
		Batch = InBatch;
		Size = InSize;

		if(Batch->BaseMaterial)
			Stride = sizeof(FProjectorBatchVertex) + sizeof(FLOAT) * 2;
		else
			Stride = sizeof(FProjectorBatchVertex);
	}

	// FRenderResource interface.

	virtual INT GetRevision() { return 1; }

	// FVertexStream interface.

	virtual INT GetSize() { return Size; }
	virtual INT GetStride() { return Stride; }
	virtual INT GetComponents(FVertexComponent* Components)
	{
		INT	NumComponents = 0;

		Components[NumComponents].Type = CT_Float3;
		Components[NumComponents].Function = FVF_Position;
		NumComponents++;
		
#ifdef _XBOX
		// scion sz - added normals so we can attenuate on GPU.
		// used packed normals on xbox
		Components[NumComponents].Type = CT_NormPacked3;
		Components[NumComponents].Function = FVF_Normal;
		NumComponents++;
		// ProjectedPosition, Attenuation not needed since they are dynamically
		// generated on the GPU
#else
		Components[NumComponents].Type = CT_Float3;
		Components[NumComponents].Function = FVF_Normal;
		NumComponents++;
#endif

#if DYNAMIC_PROJ_STREAM
		Components[NumComponents].Type = CT_Color;
		Components[NumComponents].Function = FVF_Diffuse;
		NumComponents++;

		Components[NumComponents].Type = CT_Float3;
		Components[NumComponents].Function = FVF_TexCoord0;
		NumComponents++;
#endif		
		if(Batch->BaseMaterial)
		{
			Components[NumComponents].Type = CT_Float2;
			Components[NumComponents].Function = FVF_TexCoord1;
			NumComponents++;
		}

		return NumComponents;
	}
	virtual void GetRawStreamData(void** Dest,INT FirstVertex) 
	{
		*Dest = NULL;
	}
	virtual void GetStreamData(void* Dest)
	{
		guard(FCompositeVertexStream::GetStreamData);

		FRenderBatchChunk<FProjectorRenderPrimitive>*	CurrentChunk = Batch->FirstChunk;
		INT												ChunkPrimitiveIndex = 0;
		BYTE*											DestVertex = (BYTE*)Dest;

		for(INT PrimitiveIndex = 0;PrimitiveIndex < Batch->NumPrimitives;PrimitiveIndex++)
		{
			FProjectorRenderPrimitive*	Primitive = CurrentChunk->Primitives[ChunkPrimitiveIndex];

			Primitive->GetVertices(DestVertex);
			DestVertex += Stride * Primitive->NumVertices;

			ChunkPrimitiveIndex++;

			if(ChunkPrimitiveIndex >= BATCH_CHUNK_SIZE)
			{
				CurrentChunk = CurrentChunk->NextChunk;
				ChunkPrimitiveIndex = 0;
			}
		}

		unguard;
	}
};

//
//	FCompositeIndexBuffer
//
class FCompositeIndexBuffer : public FIndexBuffer
{
public:

	FProjectorRenderBatchDef*	Batch;
	INT						Size;

	// Constructor.

	FCompositeIndexBuffer(FProjectorRenderBatchDef* InBatch,INT InSize)
	{
		Batch = InBatch;
		Size = InSize;
	}

	// FRenderResource interface.

	virtual INT GetRevision() { return 1; }

	// FIndexBuffer interface.

	virtual INT GetSize() { return Size; }
	virtual INT GetIndexSize() { return sizeof(_WORD); }
	virtual void GetContents(void* Data)
	{
		guard(FCompositeIndexBuffer::GetContents);

		FRenderBatchChunk<FProjectorRenderPrimitive>*	CurrentChunk = Batch->FirstChunk;
		INT												ChunkPrimitiveIndex = 0;
		_WORD*											DestIndex = (_WORD*) Data;
		_WORD											BaseVertexIndex = 0;

		for(INT PrimitiveIndex = 0;PrimitiveIndex < Batch->NumPrimitives;PrimitiveIndex++)
		{
			FProjectorRenderPrimitive*	Primitive = CurrentChunk->Primitives[ChunkPrimitiveIndex];

			Primitive->GetIndices(DestIndex,BaseVertexIndex);
			DestIndex += Primitive->NumIndices;
			BaseVertexIndex += Primitive->NumVertices;

			ChunkPrimitiveIndex++;

			if(ChunkPrimitiveIndex >= BATCH_CHUNK_SIZE)
			{
				CurrentChunk = CurrentChunk->NextChunk;
				ChunkPrimitiveIndex = 0;
			}
		}

		unguard;
	}
};
#endif // DYNAMIC_PROJ_STREAM


//
//	FProjectorRenderBatchDef::Render
//
#if DYNAMIC_PROJ_STREAM
void FProjectorRenderBatchDef::Render(FSceneNode* SceneNode,FRenderInterface* RI)
{
	guard(FProjectorRenderBatchDef::Render);
	FUNC_CLOCK(Proj,GStats.DWORDStats( GEngineStats.STATS_Projector_RenderCycles ));

	//RI->PushState();
	//RI->EnableLighting(0,1,0,NULL,0);
	//RI->SetZBias(1);

	DECLARE_STATIC_UOBJECT(
	UProjectorMaterial,
	ProjectorMaterial,
	{
		ProjectorMaterial->bStaticProjector = 1;
	}
	);

	ProjectorMaterial->Projected = ProjectedMaterial;
	ProjectorMaterial->BaseMaterial = BaseMaterial;
	ProjectorMaterial->BaseMaterialBlending = BaseMaterialBlending;
	ProjectorMaterial->FrameBufferBlending = FramebufferBlending;
	ProjectorMaterial->bStaticProjector = 1;
	ProjectorMaterial->bTwoSided = TwoSided;
	RI->SetMaterial(ProjectorMaterial);

	FCompositeVertexStream	VertexStream(this,VertexBufferSize);
	FCompositeIndexBuffer	IndexBuffer(this,IndexBufferSize);
	INT	BaseVertexIndex = RI->SetDynamicStream(0,&VertexStream);
	INT	BaseIndex = RI->SetDynamicIndexBuffer(&IndexBuffer,BaseVertexIndex);
	RI->DrawPrimitive(
		PT_TriangleList,
		BaseIndex,
		IndexBufferSize / (3 * sizeof(_WORD)),
		0,
		VertexBufferSize / VertexStride - 1
		);

	STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_Projector_Projectors),NumPrimitives);
	STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_Projector_Triangles),IndexBufferSize / (3 * sizeof(_WORD)));

	//RI->PopState();

	unguard;
}
#elif defined(_XBOX)
/* scion ======================================================================
* FProjectorRenderBatchDef::Render
* Author: sz
* 
* Render batched projectors.
* ============================================================================
*/
void FProjectorRenderBatchDef::Render(FSceneNode* SceneNode,FRenderInterface* RI)
{
	guard(FProjectorRenderBatchDef::Render);
	FUNC_CLOCK(Proj,GStats.DWORDStats( GEngineStats.STATS_BSP_ProjectorRenderCycles ));

	// scion sz - 
	// instead of modifying stream data on the CPU and using dynamic vertex steams
	// we can use the GPU to do all the per-vertex projector calculations and so
	// we can just set the projector primitive streams directly.
	if( RenderInfo &&
		RenderInfo->Projector && 
		RenderInfo->GetProjectedBitmap() )
	{
		//RI->PushState();
		//RI->EnableLighting(0,1,0,NULL,0);
		//RI->SetZBias(1);

		// setup vertex shader enabled material that will handle the
		// projector for this batch
		FMultiMaterialProjectorInfo ProjMultMatInfo( NULL, RenderInfo->Projector->bClipBSP );
		ProjMultMatInfo.AddProjectorEntry( RenderInfo );
		UProjectorMultiMaterial* ProjMultMat = ProjMultMatInfo.GetMaterial( (EProjectorBlending)RenderInfo->FrameBufferBlendingOp );
		RI->SetMaterial( ProjMultMat );

		// iterate through batched primititves
		for( TList<FProjectorRenderPrimitive*>* RenderPrimEntry=RenderPrimList; RenderPrimEntry; RenderPrimEntry=RenderPrimEntry->Next )
		{
			FProjectorRenderPrimitive*	Primitive = RenderPrimEntry->Element;
			checkSlow( Primitive );

			// setup static vertex/index buffers that just interface to the
			// vertex data stored in each FProjectorREnderPrimitive.
			// The FProjectorRenderPrimitive must allow direct access to its 
			// vertex/index data.
			INT PrimVertexBufferSize = Primitive->NumVertices * sizeof(FProjectorBatchVertex);
			INT PrimIndexBufferSize = Primitive->NumIndices * sizeof(_WORD);
			FProjectorPrimVertexStream	VertexStream( Primitive, PrimVertexBufferSize );
			FProjectorPrimIndexBuffer	IndexBuffer( Primitive, PrimIndexBufferSize );

			// scion sz - both the vertex stream and the index buffer for
			// each static projector primitive are used directly instead of
			// using a dynamic stream/buffer. 
			BEGIN_D3D_EVENT(BDE)(PIX_PROJECTOR,TEXT("Batched (Static) BSP Projector"));
			RI->DrawPrimitiveUP(
				VS_NotBoned,
				PT_TriangleList,
				Primitive->NumIndices / 3,
				&VertexStream,
				&IndexBuffer
				);
			END_D3D_EVENT;

			STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_Projector_Projectors),1);
			STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_Projector_Triangles),PrimIndexBufferSize / (3 * sizeof(_WORD)));
		}		

		//RI->PopState();
	}
	

	unguard;
}

/* scion ======================================================================
* FProjectorRenderBatchBSPNodes::Render
* Author: sz
* 
* Render batched dynamic projectors on bsp surfaces using the Begin/End push 
* buffer API.
* 
* ============================================================================
*/
void FProjectorRenderBatchBSPNodes::Render(FSceneNode* SceneNode,FRenderInterface* RI)
{
	guard(FProjectorRenderBatchBSPNodes::Render);
	FUNC_CLOCK(Proj,GStats.DWORDStats( GEngineStats.STATS_BSP_ProjectorRenderCycles ));

	if( RenderInfo &&
		RenderInfo->Projector && 
		RenderInfo->GetProjectedBitmap() )
	{
		//RI->PushState();
		//RI->EnableLighting(0,1,0,NULL,0);
		//RI->SetZBias(1);		
        
		//FLineBatcher TestLine(RI);

		UShadowBitmapMaterial* pShadowBitmap = Cast<UShadowBitmapMaterial>( RenderInfo->GetProjectedBitmap() );
		if( pShadowBitmap && 
			pShadowBitmap->Dirty )
		{
			// force update of shadow material so that its casting geometry will render
			// otherwise, it will probably overwrite the VSC_COMPRESSION_SCALE values
			pShadowBitmap->Get( SceneNode->Viewport->CurrentTime, SceneNode->Viewport );
			// reset some state			
			RI->EnableLighting(0,1,0,NULL,0);
			RI->SetZBias(1);
		}

		RI->SetTransform( TT_LocalToWorld, FMatrix::Identity );

		// setup vertex shader enabled material that will handle the
		// projector for this batch
		FMultiMaterialProjectorInfo ProjMultMatInfo( NULL, RenderInfo->Projector->bClipBSP );
		ProjMultMatInfo.AddProjectorEntry( RenderInfo );
		UProjectorMultiMaterial* ProjMultMat = ProjMultMatInfo.GetMaterial( (EProjectorBlending)RenderInfo->FrameBufferBlendingOp );
		RI->SetMaterial( ProjMultMat );

		// count total number of vertices needed for all the batched primitives
		INT TotalPrimVerts = 0;
		for( TList<FProjectorRenderPrimitive*>* RenderPrimEntry=RenderPrimList; RenderPrimEntry; RenderPrimEntry=RenderPrimEntry->Next )
		{
			if( RenderPrimEntry->Element->NumVertices > 2 ) {
				TotalPrimVerts += (RenderPrimEntry->Element->NumVertices-2)*3;
			}
		}
		// total size of all vertex data
		DWORD dwPrimSize = sizeof(FProjectorBatchVertex) * TotalPrimVerts;
		// headers needed
		DWORD dwHeaderSize = 5 + (dwPrimSize / (D3DPUSH_MAX_COUNT / sizeof(FProjectorBatchVertex)));
		// push buffer size needed for verts and for opcodes
		DWORD dwSizeNeeded = dwPrimSize + dwHeaderSize * sizeof(DWORD);
		// the vertex stream is needed for setting the stream declaration
		FProjectorPrimVertexStream VertexStream(NULL,dwPrimSize);
		// not using an index buffer (vertices set directly)
		RI->SetIndexBuffer(NULL,0);

		// so we know what we are looking at in PIX
		BEGIN_D3D_EVENT(BDE)(PIX_PROJECTOR,TEXT("Batched (Dynamic) BSP Projector"));
		// get push buffer of appropriate size
		DWORD* pdwBuff = RI->BeginPush( dwSizeNeeded, VS_Normal, &VertexStream, 1, 1 );
		if( !pdwBuff ) 
		{
			debugf( NAME_DevD3D, TEXT("BeginPush failed for dynamic projector: %s"), RenderInfo->Projector->GetName() );
		}
		else
		{
			// The number of DWORDs each vert takes
			const DWORD dwDwordsPerVert = sizeof(FProjectorBatchVertex) / sizeof(DWORD);
			// This is the number of DWORDs we encode in a given inline array push
			// NOTE: It must break on primitive alignment (3 verts min)
			const DWORD dwMaxInlineArray = ( D3DPUSH_MAX_COUNT / sizeof(FProjectorBatchVertex) ) * dwDwordsPerVert * 3;
			// Determines how many DWORDs we've pushed into the push buffer
			DWORD dwDwordsEncoded = 0;			
			// Encode the primitive type and begin command into the push buffer
			*pdwBuff++ = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END,1);
			*pdwBuff++ = PT_TriangleList;
			// first vertex in buffer
			FProjectorBatchVertex*	Vertex = (FProjectorBatchVertex*)pdwBuff;
			// remaining verts to render
			INT PrimVertsToRender = TotalPrimVerts;

			// iterate through batched primititves
			for( TList<FProjectorRenderPrimitive*>* RenderPrimEntry=RenderPrimList; RenderPrimEntry; RenderPrimEntry=RenderPrimEntry->Next )
			{
				FDynamicBspProjectorPrimitive*	Primitive = (FDynamicBspProjectorPrimitive*)RenderPrimEntry->Element;
                // tri fans sharing this first vertex
				FProjectorBatchVertex* FirstVertex = Vertex;			
				// Generate vertices.
				for( INT v=2; v < Primitive->NumVertices && PrimVertsToRender > 0; v++ )
				{
					// Check to see if we need to encode an inline array push
					if( dwDwordsEncoded % dwMaxInlineArray == 0)
					{
						// Figure out how many dwords we'll need for each prim
						DWORD dwNumThisBatch = Min<DWORD>( PrimVertsToRender * dwDwordsPerVert, dwMaxInlineArray );
						// Build a new batch of verts to be processed
						*pdwBuff++ = D3DPUSH_ENCODE( D3DPUSH_NOINCREMENT_FLAG|D3DPUSH_INLINE_ARRAY, dwNumThisBatch );
						// Make the vertex be at the correct position in the push buffer
						Vertex = (FProjectorBatchVertex*)pdwBuff;
					}

					// first vertex
					if( v==2 )
					{
						// set first vertex
						Vertex->WorldPosition = Primitive->Vertices[0].WorldPosition;
						Vertex->Normal = Primitive->Vertices[0].Normal;
						FirstVertex = Vertex;
					}
					else
					{
						// copy first vertex
						Vertex->WorldPosition = FirstVertex->WorldPosition;
						Vertex->Normal = FirstVertex->Normal;					
					}
					Vertex++;
					pdwBuff += dwDwordsPerVert;
					dwDwordsEncoded += dwDwordsPerVert;				

					// second vertex
					Vertex->WorldPosition = Primitive->Vertices[v-1].WorldPosition;
					Vertex->Normal = Primitive->Vertices[v-1].Normal;
					Vertex++;
					pdwBuff += dwDwordsPerVert;
					dwDwordsEncoded += dwDwordsPerVert;

					// third vertex
					Vertex->WorldPosition = Primitive->Vertices[v].WorldPosition;
					Vertex->Normal = Primitive->Vertices[v].Normal;
					Vertex++;
					pdwBuff += dwDwordsPerVert;
					dwDwordsEncoded += dwDwordsPerVert;

					// decrement remaining prim count
					PrimVertsToRender -= 3;

					/*
					FProjectorBatchVertex* pVerts[]={ Vertex-3,Vertex-2,Vertex-1 };
					TestLine.DrawLine( pVerts[1]->WorldPosition, pVerts[0]->WorldPosition, FColor(255,0,0) );
					TestLine.DrawLine( pVerts[2]->WorldPosition, pVerts[0]->WorldPosition, FColor(0,255,0) );
					TestLine.DrawLine( pVerts[2]->WorldPosition, pVerts[1]->WorldPosition, FColor(0,0,255) );
					*/
				}			
			}

			// terminate command for the push buffer
			*pdwBuff++ = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END,1);
			*pdwBuff++ = 0;
            // execute push
			RI->EndPush(pdwBuff);		
		}

		END_D3D_EVENT;

		//TestLine.Flush();

		STAT_INC_BY( GStats.DWORDStats(GEngineStats.STATS_Projector_Triangles), TotalPrimVerts/3 );
		STAT_INC_BY( GStats.DWORDStats(GEngineStats.STATS_Projector_Projectors), 1 );

		//RI->PopState();
	}

	unguard;
}

/* scion ======================================================================
 * EncodeIndicesIntoPushBuffer
 * Author: sz
 * 
 * Encodes the indices for the triangles of the given kdop leaf nodes 
 * into a push buffer. See UnTerrain.cpp for original code used for rendering
 * terrain things.
 *
 * ============================================================================
 */
void EncodeIndicesIntoPushBuffer( _WORD* NodeIndices, INT NumNodeIndices,
	const TArray<FkDOPNode>& Nodes, 
	const TArray<FkDOPCollisionTriangle>& Triangles,
	FRenderInterface* pRI )
{
	guard(EncodeIndicesAsPushBuffer);
	check( NumNodeIndices && NumNodeIndices );	

	// this static is kind of ghetto but it allows us to copy the indices all at once
	static TArray<_WORD> Indices;
	Indices.Reset();
	DWORD dwIndexCount = 0;
	// Loop through the nodes and figure out how many indices we are
	// going to encode, so we can calculate how large of a pushbuffer we'll
	// need to use
	for( INT Idx = 0; Idx < NumNodeIndices; Idx++ )
	{
		const FkDOPNode& Node = Nodes( NodeIndices[Idx] );
		checkSlow( Node.bIsLeaf );
		INT CurIndex = Indices.Add( Node.NumTriangles * 3 );
		for( INT TriIdx=0; TriIdx < Node.NumTriangles; TriIdx++ )
		{
			Indices(CurIndex++) = Triangles( TriIdx + Node.StartIndex ).v1;
			Indices(CurIndex++) = Triangles( TriIdx + Node.StartIndex ).v2;
			Indices(CurIndex++) = Triangles( TriIdx + Node.StartIndex ).v3;
		}
	}
	// if odd number of indices then append a special pad index
	if( Indices.Num()%2 )
	{
        Indices.AddItem( D3DPUSH_DRAW_INDEX_LAST );
	}
	dwIndexCount = Indices.Num();
	// Every so many indices we need to encode a new begin indices command.
	// We need 2 DWORDs for the primitive type and 1 for each begin indices
	// command and 2 DWORDs for the terminator
	DWORD dwHeaderSize = (dwIndexCount / MAX_INDICES_IN_PUSH) + 5;
	// Come up with a total for all indices and the header(s)
	DWORD dwNumBytes = dwIndexCount * sizeof(_WORD) + 
		dwHeaderSize * sizeof(DWORD);
	// Get a pointer directly into the main push buffer
	DWORD* pdwBuff = (DWORD*)pRI->BeginPush(dwNumBytes);
	// Encode the start command & primitive type (tri list)
	*pdwBuff++ = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END,1);
	*pdwBuff++ = PT_TriangleList;
	// Used to keep track of how many indices encoded thus far
	DWORD dwEncodedCount = 0;
	// Size of the batch (MAX_INDICES_IN_PUSH or less)
	DWORD dwNumThisBatch = 0;
	// How many indices are left 
	DWORD dwIndicesLeft = Indices.Num();
	// Maintains the source pointer for copies
	DWORD* pdwIndices = (DWORD*)&Indices(0);
	// Now loop through the render nodes and add their indices in
	while( dwEncodedCount < dwIndexCount )
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

		// Figure out how many indices we can copy in one call
		DWORD dwCopy = Min<DWORD>(dwNumThisBatch,dwIndicesLeft);
		// Use a fast copy rather than hand encoding each one
		appMemcpy(pdwBuff,pdwIndices,dwCopy * sizeof(_WORD));
		// Increment pointer positions to reflect the new source/dest
		pdwBuff += dwCopy / sizeof(_WORD);
		pdwIndices += dwCopy / sizeof(_WORD);
		// Update the various counters
		dwEncodedCount += dwCopy;
		dwIndicesLeft -= dwCopy;
		dwNumThisBatch -= dwCopy;		       
	}
	// Encode the terminator info
	*pdwBuff++ = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END,1);
	*pdwBuff++ = 0;
	// Let go of the push buffer
	pRI->EndPush(pdwBuff);
	STAT_INC_BY( GStats.DWORDStats(GEngineStats.STATS_Projector_Triangles), Indices.Num()/3 );	
	unguard;
}

/* scion ======================================================================
* FProjectorRenderBatchStaticMesh::Render
* Author: sz
* 
* Render batched dynamic projectors on static meshes using the Begin/End push 
* buffer API.
* 
* ============================================================================
*/
void FProjectorRenderBatchStaticMesh::Render(FSceneNode* SceneNode,FRenderInterface* RI)
{
	guard(FProjectorRenderBatchStaticMesh::Render);
	FUNC_CLOCK(ProjCyc,GStats.DWORDStats(GEngineStats.STATS_StaticMesh_ProjectorRenderCycles));

	if( RenderInfo &&
		RenderInfo->Projector && 
		RenderInfo->GetProjectedBitmap() )
	{
		RI->PushState();

		// setup vertex shader enabled material that will handle the
		// projector for this batch
		FMultiMaterialProjectorInfo ProjMultMatInfo( NULL, RenderInfo->Projector->bClipStaticMesh );
		ProjMultMatInfo.AddProjectorEntry( RenderInfo );
		UProjectorMultiMaterial* ProjMultMat = ProjMultMatInfo.GetMaterial( (EProjectorBlending)RenderInfo->FrameBufferBlendingOp );

		UShadowBitmapMaterial* pShadowBitmap = Cast<UShadowBitmapMaterial>( RenderInfo->GetProjectedBitmap() );
		if( pShadowBitmap && 
			pShadowBitmap->Dirty )
		{
			// force update of shadow material so that its casting geometry will render
			// otherwise, it will probably overwrite the VSC_COMPRESSION_SCALE values
			pShadowBitmap->Get( SceneNode->Viewport->CurrentTime, SceneNode->Viewport );
		}

		RI->EnableLighting(0,1,0,NULL,0);	
		RI->SetZBias(1);

		for( TList<FProjectorRenderPrimitive*>* RenderPrimEntry=RenderPrimList; RenderPrimEntry; RenderPrimEntry=RenderPrimEntry->Next )
		{
			// the batched primitive
			FProjectorRenderPrimStaticMeshClippedNodes* Prim = (FProjectorRenderPrimStaticMeshClippedNodes*)RenderPrimEntry->Element;
			// source static mesh for clipped nodes
			UStaticMesh* StaticMesh = Prim->StaticMeshActor->StaticMesh;
			// need the source actor iot set l2w
			if( Prim->StaticMeshActor->bDeleteMe )
				continue;
			FDynamicActor* DynActor = Prim->StaticMeshActor->GetActorRenderData();
			// cull mode
			RI->SetCullMode( (DynActor->Determinant < 0.f) ? CM_CCW : CM_CW );	
			// set l2w - this is needed since the static mesh verts are in local space
			RI->SetTransform( TT_LocalToWorld, DynActor->LocalToWorld );
			// we can't avoid setting the material for each static mesh since
			// the l2w is unique for each static mesh
			RI->SetMaterial( ProjMultMat );
			// set vertex stream for this static mesh
			FVertexStream*	VertexStreams[] = { &StaticMesh->VertexStream };
			RI->SetVertexStreams( VS_CompressedMesh, VertexStreams, 1 );
			// the vertex stream for static mesh is stored as packed vectors 
			// set the scale/bias for unpacking them		
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
			// use the compression scale for the packed vertex positions
			RI->SetVertexShaderConstant(VSC_COMPRESSION_SCALE,CompressionValues,2);
			// indices encoded directly into the push buffer so not needed here
			RI->SetIndexBuffer( NULL,0 );

			if( Prim->NumNodeIndices )
			{
				BEGIN_D3D_EVENT(BDE)(PIX_PROJECTOR,TEXT("Batched (Dynamic) SM clipped Projector"));
				// copy indices directly to push buffer from kdop nodes
				EncodeIndicesIntoPushBuffer( 
					Prim->NodeIndices, 
					Prim->NumNodeIndices,
					StaticMesh->kDOPTree.Nodes, 
					StaticMesh->kDOPTree.Triangles,
					RI 
					);
				END_D3D_EVENT;
			}
			else
			{
				BEGIN_D3D_EVENT(BDE)(PIX_PROJECTOR,TEXT("Batched (Dynamic) SM default Projector"));
				// just draw static mesh sections using its 
				// default static push buffer
				for( INT SectIdx=0; SectIdx < StaticMesh->Sections.Num(); SectIdx++ )
				{
					FStaticMeshSection& Section = StaticMesh->Sections(SectIdx);
					if( Section.NumPrimitives > 0 )
					{
						RI->RunPushBuffer( Section.pPushBuffer );
						STAT_INC_BY( GStats.DWORDStats(GEngineStats.STATS_Projector_Triangles), Section.NumTriangles );
					}					
				}
				END_D3D_EVENT;
			}            

#if 0
			// draw some lines
			FLineBatcher TestLine(RI);
			FPackedPosition::SetCompressionVectors(	&StaticMesh->CompressionScale,
			&StaticMesh->CompressionInvScale, &StaticMesh->CompressionOffset );
			// all static mesh nodes
			const TArray<FkDOPNode>& Nodes = StaticMesh->kDOPTree.Nodes;
			// all static mesh tris
			const TArray<FkDOPCollisionTriangle>& Triangles = StaticMesh->kDOPTree.Triangles;
			// all static mesh verts
			const TUmaArray<FStaticMeshVertex>& Verts = StaticMesh->VertexStream.Vertices;
			for( INT NodeIdx=0; NodeIdx < Prim->NumNodeIndices; NodeIdx++ )
			{
				// a node that interesected the projector's frustum
				const FkDOPNode& Node = Nodes( Prim->NodeIndices[NodeIdx] );
				checkSlow( Node.bIsLeaf );

				for( INT TriIdx=0; TriIdx < Node.NumTriangles; TriIdx++ )
				{
					// the triangles in the current node
					const FkDOPCollisionTriangle& Tri = Triangles( Node.StartIndex + TriIdx );
					static FColor LineColor(64,255,64);

					FVector P1( Verts(Tri.v1).Position ),
					P2( Verts(Tri.v2).Position ),
					P3( Verts(Tri.v3).Position );

					TestLine.DrawLine( P1, P2, LineColor );
					TestLine.DrawLine( P2, P3, LineColor );
					TestLine.DrawLine( P3, P1, LineColor );
				}
			}

			TestLine.Flush();
			FPackedPosition::ClearCompressionVectors();
#endif

		}

		STAT_INC_BY( GStats.DWORDStats(GEngineStats.STATS_Projector_Projectors), 1 );
		RI->PopState();
	}

	unguard;
}

/* scion ======================================================================
* FProjectorRenderBatchStaticMesh::AddClippedStaticMeshPrims
* Author: sz
* 
* Given a static mesh. Return list of kDOPNodes that clip against the projector
* used in this batch.
* 
* ============================================================================
*/
void FProjectorRenderBatchStaticMesh::AddClippedStaticMeshPrims( AActor* StaticMeshActor )
{
	guard(FProjectorRenderBatchStaticMesh::AddClippedStaticMeshPrims);
	check(StaticMeshActor);
	check(StaticMeshActor->DrawType==DT_StaticMesh && StaticMeshActor->StaticMesh);
	check(RenderInfo && RenderInfo->Projector);
	FUNC_CLOCK(ProjCyc,GStats.DWORDStats(GEngineStats.STATS_StaticMesh_ProjectorRenderCycles));

	if( StaticMeshActor->bDeleteMe )
		return;

	// static mesh this projector is touching
	UStaticMesh* StaticMesh = StaticMeshActor->StaticMesh;
	// list of kdop nodes (leaves) that intersect the projector's frustum 
	static TArray<_WORD> ResultNodes;
	ResultNodes.Reset();

	// min # of triangles that a static mesh must have before we
	// use the frustum queries on its kdop nodes
	const INT MIN_TRI_CUTOFF=2500;
	if( StaticMesh->kDOPTree.Triangles.Num() < MIN_TRI_CUTOFF )
	{
		// just draw using the static push buffer for the stat mesh 
		// no clipping of kdop nodes necessary since the mesh is pretty small
		AddPrimitive( new(GSceneMem) FProjectorRenderPrimStaticMeshClippedNodes( StaticMeshActor, ResultNodes ) );
		return;
	}

	// Set the compression scale & offset so that 
	// vertices are unpacked correctly
	FPackedPosition::SetCompressionVectors(	&StaticMesh->CompressionScale,
		&StaticMesh->CompressionInvScale, &StaticMesh->CompressionOffset );

	// query kdop tree to test against frustum
	FkDOPFrustumQuery QueryInfo( StaticMeshActor, RenderInfo->FrustumPlanes, 6, ResultNodes );
	StaticMesh->kDOPTree.FrustumQuery( QueryInfo );

	// if we found some intersecting nodes
	if( ResultNodes.Num() )
	{
		// this constant represents the cutoff point for decided whether to draw
		// using directly encoded clipped indices or the complete original staticmesh 
		const FLOAT MIN_CLIP_CUTOFF=0.5f;
		if( QueryInfo.ReturnNumTris > (MIN_CLIP_CUTOFF*StaticMesh->kDOPTree.Triangles.Num()) )
		{
			// remove the clipped nodes. This defaults to just 
			// using the static mesh geometry for rendering)
			ResultNodes.Reset();
		}		
		// create a primitive from the clipped kdop nodes
		AddPrimitive( new(GSceneMem) FProjectorRenderPrimStaticMeshClippedNodes( StaticMeshActor, ResultNodes ) );
	}

	// Wipe the compression scale & offset on the compressor class
	// so that we can find errors faster
	FPackedPosition::ClearCompressionVectors();

	unguard;
}

#else // (!defined(_XBOX) && defined(XBOX_ASSET_FORMAT))
// scion sz - some function stubs
void FProjectorRenderBatchDef::Render(FSceneNode* SceneNode,FRenderInterface* RI)
{
	check( 0 && "not used with XBOX_ASSET_FORMAT" );
}
void FProjectorRenderBatchBSPNodes::Render(FSceneNode* SceneNode,FRenderInterface* RI)
{
	check( 0 && "not used with XBOX_ASSET_FORMAT" );
}
void EncodeIndicesIntoPushBuffer( _WORD* NodeIndices, INT NumNodeIndices,
	const TArray<FkDOPNode>& Nodes, 
	const TArray<FkDOPCollisionTriangle>& Triangles,
	FRenderInterface* pRI )
{
	check( 0 && "not used with XBOX_ASSET_FORMAT" );
}
void FProjectorRenderBatchStaticMesh::Render(FSceneNode* SceneNode,FRenderInterface* RI)
{
	check( 0 && "not used with XBOX_ASSET_FORMAT" );
}
void FProjectorRenderBatchStaticMesh::AddClippedStaticMeshPrims( AActor* StaticMeshActor )
{
	check( 0 && "not used with XBOX_ASSET_FORMAT" );
}

#endif //!DYNAMIC_PROJ_STREAM

// scion sz - not used on xbox
#ifndef _XBOX
//
//	FStaticMeshBatchVertexStream::FStaticMeshBatchVertexStream
//

FStaticMeshBatchVertexStream::FStaticMeshBatchVertexStream(UMaterial* Material)
{
	Size = 0;

	// Count the number of required UVs.

	BYTE	RequiredUVStreams = Material->RequiredUVStreams();

	BYTE UVStreamMask = 0xff;
	for(INT UVIndex = 0;UVIndex < 8;UVIndex++,UVStreamMask <<= 1)
		if(!(RequiredUVStreams & UVStreamMask))
		{
			NumUVs = UVIndex;
			break;
		}

	// Calculate the stride.

	Normal = 1;//Material->RequiresNormal();
	Stride = (Normal ? sizeof(FStaticMeshBatchNormalVertex) : sizeof(FStaticMeshBatchVertex)) + sizeof(FLOAT) * 2 * NumUVs;
}

//
//	FStaticMeshBatchVertexStream::GetComponents
//

INT FStaticMeshBatchVertexStream::GetComponents(FVertexComponent* Components)
{
	INT	NumComponents = 0;

	Components[NumComponents].Type = CT_Float3;
	Components[NumComponents].Function = FVF_Position;
	NumComponents++;

	if(Normal)
	{
		Components[NumComponents].Type = CT_Float3;
		Components[NumComponents].Function = FVF_Normal;
		NumComponents++;
	}

	Components[NumComponents].Type = CT_Color;
	Components[NumComponents].Function = FVF_Diffuse;
	NumComponents++;

	for(INT UVIndex = 0;UVIndex < NumUVs;UVIndex++)
	{
		Components[NumComponents].Type = CT_Float2;
		Components[NumComponents].Function = (EFixedVertexFunction)(FVF_TexCoord0 + UVIndex);
		NumComponents++;
	}

	return NumComponents;
}

//
//	FStaticMeshBatchVertexStream::GetStreamData
//

void FStaticMeshBatchVertexStream::GetStreamData(void* Dest)
{
	guard(FStaticMeshBatchVertexStream::GetStreamData);

	FVector*	DestPosition = (FVector*)Dest;
	FVector*	DestNormal = Normal ? (DestPosition + 1) : DestPosition;
	FColor*		DestLighting = (FColor*)(DestNormal + 1);
	FLOAT*		DestUVs = (FLOAT*)(DestLighting + 1);

	for(INT ElementIndex = 0;ElementIndex < Batch->Elements.Num();ElementIndex++)
	{
		AActor*					Actor = Batch->Elements(ElementIndex).Actor;
		FDynamicActor*			DynamicActor = Actor->GetActorRenderData();
		UStaticMeshInstance*	StaticMeshInstance = NULL;
		UStaticMesh*			StaticMesh = Actor->StaticMesh;
		FStaticMeshSection&		Section = StaticMesh->Sections(Batch->Elements(ElementIndex).SectionIndex);

		if(Actor->StaticMeshInstance)
		{
			// Verify the actor's instance lighting matches the static mesh.

			UBOOL	Matches = 1;

			if(Actor->StaticMeshInstance->ColorStream.Colors.Num() != StaticMesh->VertexStream.Vertices.Num())
				Matches = 0;

			if(Matches)
			{
				for(INT LightIndex = 0;LightIndex < Actor->StaticMeshInstance->Lights.Num();LightIndex++)
					if(Actor->StaticMeshInstance->Lights(LightIndex).VisibilityBits.Num() != (StaticMesh->VertexStream.Vertices.Num() + 7) / 8)
					{
						Matches = 0;
						break;
					}
			}

			if(Matches)
				StaticMeshInstance = Actor->StaticMeshInstance;
		}

		if(StaticMeshInstance)
			CalculateStaticMeshLighting(StaticMesh,StaticMeshInstance,DynamicActor);

		INT	NumExistingUVs = Min(NumUVs,StaticMesh->UVStreams.Num());

		for(INT VertexIndex = Section.MinVertexIndex;VertexIndex <= Section.MaxVertexIndex;VertexIndex++)
		{
			FStaticMeshVertex*		SrcVertex = &StaticMesh->VertexStream.Vertices(VertexIndex);

			*DestPosition = DynamicActor->LocalToWorld.TransformFVector(SrcVertex->Position);

			if(Normal)
				*DestNormal = DynamicActor->LocalToWorld.TransformNormal(SrcVertex->Normal);

			if(Actor->bUnlit)
				*DestLighting = FColor(127,127,127);
			else if(StaticMeshInstance)
			{
				*DestLighting = StaticMeshInstance->ColorStream.Colors(VertexIndex);
				*DestLighting += FColor(FPlane(DynamicActor->AmbientColor) * 0.5f);
			}
			else
				*DestLighting = FColor(FPlane(DynamicActor->AmbientColor) * 0.5f);

			for(INT UVIndex = 0;UVIndex < NumExistingUVs;UVIndex++)
			{
				DestUVs[UVIndex * 2 + 0] = StaticMesh->UVStreams(UVIndex).UVs(VertexIndex).U;
				DestUVs[UVIndex * 2 + 1] = StaticMesh->UVStreams(UVIndex).UVs(VertexIndex).V;
			}

			for(INT UVIndex = NumExistingUVs;UVIndex < NumUVs;UVIndex++)
			{
				DestUVs[UVIndex * 2 + 0] = 0.0f;
				DestUVs[UVIndex * 2 + 1] = 0.0f;
			}

			DestPosition = (FVector*)((BYTE*) DestPosition + Stride);
			DestNormal = (FVector*)((BYTE*) DestNormal + Stride);
			DestLighting = (FColor*)((BYTE*) DestLighting + Stride);
			DestUVs = (FLOAT*)((BYTE*) DestUVs + Stride);
		}
	}

	unguard;
}

//
//	FStaticMeshBatchIndexBuffer::FStaticMeshBatchIndexBuffer
//

FStaticMeshBatchIndexBuffer::FStaticMeshBatchIndexBuffer()
{
	Size = 0;
}

//
//	FStaticMeshBatchIndexBuffer::GetContents
//

void FStaticMeshBatchIndexBuffer::GetContents(void* Dest)
{
	guard(FStaticMeshBatchIndexBuffer::GetContents);

	_WORD*	DestIndex = (_WORD*) Dest;
	INT		BaseIndex = 0;

	for(INT ElementIndex = 0;ElementIndex < Batch->Elements.Num();ElementIndex++)
	{
		AActor*				Actor = Batch->Elements(ElementIndex).Actor;
		FDynamicActor*		DynamicActor = Actor->GetActorRenderData();
		UStaticMesh*		StaticMesh = Actor->StaticMesh;
		FStaticMeshSection*	Section = &StaticMesh->Sections(Batch->Elements(ElementIndex).SectionIndex);
		_WORD*				SrcIndex = &StaticMesh->IndexBuffer.Indices(Section->FirstIndex);

        if(DynamicActor->Determinant < 0.0f)
		{
			// The static mesh has a negative scale, reverse the vertex order.

			for(INT PrimitiveIndex = 0;PrimitiveIndex < Section->NumPrimitives;PrimitiveIndex++)
			{
				_WORD	TriangleIndices[3];
				
				TriangleIndices[0] = *SrcIndex++;
				TriangleIndices[1] = *SrcIndex++;
				TriangleIndices[2] = *SrcIndex++;

				*DestIndex++ = TriangleIndices[2] - Section->MinVertexIndex + BaseIndex;
				*DestIndex++ = TriangleIndices[1] - Section->MinVertexIndex + BaseIndex;
				*DestIndex++ = TriangleIndices[0] - Section->MinVertexIndex + BaseIndex;
			}
		}
		else
		{
			for(INT PrimitiveIndex = 0;PrimitiveIndex < Section->NumPrimitives;PrimitiveIndex++)
			{
				*DestIndex++ = *SrcIndex++ - Section->MinVertexIndex + BaseIndex;
				*DestIndex++ = *SrcIndex++ - Section->MinVertexIndex + BaseIndex;
				*DestIndex++ = *SrcIndex++ - Section->MinVertexIndex + BaseIndex;
			}
		}

		BaseIndex += Section->MaxVertexIndex - Section->MinVertexIndex + 1;
	}

	unguard;
}

//
//	FStaticMeshBatch::FStaticMeshBatch
//

FStaticMeshBatch::FStaticMeshBatch(UMaterial* InMaterial,UBOOL DisableSorting) : Vertices(InMaterial)
{
	guard(FStaticMeshBatch::FStaticMeshBatch);

	Material = InMaterial;
	Sorted = Material->RequiresSorting() && !DisableSorting;

	unguard;
}

//
//	FStaticMeshBatch::AddElement
//

INT FStaticMeshBatch::AddElement(AActor* Actor,INT SectionIndex)
{
	guard(FStaticMeshBatch::AddElement);

	FStaticMeshSection&	Section = Actor->StaticMesh->Sections(SectionIndex);
	FBatchElement		NewElement;

	check(!Section.IsStrip);

	if(Section.NumPrimitives == 0)
		return INDEX_NONE;

	NewElement.Actor = Actor;
	NewElement.SectionIndex = SectionIndex;
	NewElement.FirstIndex = Indices.Size / sizeof(_WORD);
	NewElement.NumPrimitives = Section.NumPrimitives;
	NewElement.MinVertexIndex = Vertices.Size / Vertices.Stride;
	NewElement.MaxVertexIndex = NewElement.MinVertexIndex + (Section.MaxVertexIndex - Section.MinVertexIndex);

	INT	Index = Elements.AddItem(NewElement);
	Indices.Size += Section.NumPrimitives * 3 * sizeof(_WORD);
	Vertices.Size += (Section.MaxVertexIndex - Section.MinVertexIndex + 1) * Vertices.Stride;

	return Index;

	unguard;
}

//
// FSortedStaticMeshBatchIndexBuffer
//

#if ASMLINUX
inline void FloatToInt(int *int_pointer, float f)
{
    __asm__ __volatile__
    (
        "  flds    (%%eax)  \n\t"
        "  frndint          \n\t"
        "  fistp   (%%edx)  \n\t"
            : /* output to *int_pointer is done by fistp */
            : "d" (int_pointer), "a" (&f)
            : "cc", "memory"
    );
}
#else
__forceinline void FloatToInt(int *int_pointer, float f)
{
    __asm  fld  f
    __asm  mov  edx,int_pointer
    __asm  FRNDINT
    __asm  fistp dword ptr [edx];
}
#endif

struct FTriangleSortInfo
{
	INT 	SortKey;
	INT		Indices[3];
};

static int CDECL TriSortCompare( const void *A, const void *B )
{
    return ((FTriangleSortInfo*)B)->SortKey - ((FTriangleSortInfo*)A)->SortKey;
}

class FSortedStaticMeshBatchIndexBuffer : public FIndexBuffer
{
public:

	FStaticMeshBatch*	Batch;
	INT*				VisibleElements;
	INT					NumVisibleElements;
	FVector				ViewOrigin;

	QWORD	CacheId;
	INT		Size;

	// Constructor.

	FSortedStaticMeshBatchIndexBuffer(FStaticMeshBatch* InBatch,INT* InVisibleElements,INT InNumVisibleElements,FVector InViewOrigin)
	{
		Batch = InBatch;
		VisibleElements = InVisibleElements;
		NumVisibleElements = InNumVisibleElements;
		ViewOrigin = InViewOrigin;

		// Calculate the size of the index buffer.

		Size = 0;

		for(INT ElementIndex = 0;ElementIndex < NumVisibleElements;ElementIndex++)
		{
			FStaticMeshBatch::FBatchElement&	Element = Batch->Elements(VisibleElements[ElementIndex]);

			Size += Element.NumPrimitives * 3 * sizeof(_WORD);
		}
	}

	// FRenderResource interface.

	virtual INT GetRevision() { return 1; }

	// FIndexBuffer interface.

	virtual INT GetSize() { return Size; }
	virtual INT GetIndexSize() { return sizeof(_WORD); }
	virtual void GetContents(void* Dest)
	{
		guard(FSortedStaticMeshBatchIndexBuffer::GetContents);
		FUNC_CLOCK(BatchSort,GStats.DWORDStats(GEngineStats.STATS_StaticMesh_BatchedSortCycles));

		_WORD* DestIndex = (_WORD*)Dest;

		for(INT ElementIndex = 0;ElementIndex < NumVisibleElements;ElementIndex++)
		{
			FStaticMeshBatch::FBatchElement&	Element = Batch->Elements(VisibleElements[ElementIndex]);
			FDynamicActor*						DynamicActor = Element.Actor->GetActorRenderData();
			FStaticMeshSection*					Section = &Element.Actor->StaticMesh->Sections(Element.SectionIndex);

			if((DynamicActor->BoundingSphere - ViewOrigin).SizeSquared() < Square(8192.0f))
			{
				FMemMark			MemMark(GSceneMem);
				FMatrix				WorldToLocal(DynamicActor->LocalToWorld.Inverse());
				FVector				LocalViewOrigin = WorldToLocal.TransformFVector(ViewOrigin);
				_WORD*				Indices = &Element.Actor->StaticMesh->IndexBuffer.Indices(Section->FirstIndex);
				FStaticMeshVertex*	Vertices = &Element.Actor->StaticMesh->VertexStream.Vertices(0);
				FTriangleSortInfo*	SortedTriangles = New<FTriangleSortInfo>(GSceneMem,Section->NumTriangles);

				for(INT TriangleIndex = 0;TriangleIndex < Section->NumTriangles;TriangleIndex++)
				{
					SortedTriangles[TriangleIndex].Indices[0] = Indices[TriangleIndex * 3 + 0];
					SortedTriangles[TriangleIndex].Indices[1] = Indices[TriangleIndex * 3 + 1];
					SortedTriangles[TriangleIndex].Indices[2] = Indices[TriangleIndex * 3 + 2];

					int d0,d1,d2;
					FloatToInt( &d0, (Vertices[Indices[TriangleIndex * 3 + 0]].Position - LocalViewOrigin).SizeSquared());
					FloatToInt( &d1, (Vertices[Indices[TriangleIndex * 3 + 1]].Position - LocalViewOrigin).SizeSquared());
					FloatToInt( &d2, (Vertices[Indices[TriangleIndex * 3 + 2]].Position - LocalViewOrigin).SizeSquared());
					SortedTriangles[TriangleIndex].SortKey = Max(d0,Max(d1,d2));
				}

				qsort( SortedTriangles, Section->NumTriangles, sizeof(FTriangleSortInfo), TriSortCompare );

				for(INT TriangleIndex = 0;TriangleIndex < Section->NumTriangles;TriangleIndex++)
				{
					if(DynamicActor->Determinant < 0.0f)
					{
						*DestIndex++ = SortedTriangles[TriangleIndex].Indices[2] - Section->MinVertexIndex + Element.MinVertexIndex;
						*DestIndex++ = SortedTriangles[TriangleIndex].Indices[1] - Section->MinVertexIndex + Element.MinVertexIndex;
						*DestIndex++ = SortedTriangles[TriangleIndex].Indices[0] - Section->MinVertexIndex + Element.MinVertexIndex;
					}
					else
					{
						*DestIndex++ = SortedTriangles[TriangleIndex].Indices[0] - Section->MinVertexIndex + Element.MinVertexIndex;
						*DestIndex++ = SortedTriangles[TriangleIndex].Indices[1] - Section->MinVertexIndex + Element.MinVertexIndex;
						*DestIndex++ = SortedTriangles[TriangleIndex].Indices[2] - Section->MinVertexIndex + Element.MinVertexIndex;
					}
				}

				MemMark.Pop();
			}
			else
			{
				_WORD*	SrcIndex = &Element.Actor->StaticMesh->IndexBuffer.Indices(Section->FirstIndex);

				if(DynamicActor->Determinant < 0.0f)
				{
					// The static mesh has a negative scale, reverse the vertex order.

					for(INT PrimitiveIndex = 0;PrimitiveIndex < Section->NumPrimitives;PrimitiveIndex++)
					{
						_WORD	TriangleIndices[3];
						
						TriangleIndices[0] = *SrcIndex++;
						TriangleIndices[1] = *SrcIndex++;
						TriangleIndices[2] = *SrcIndex++;

						*DestIndex++ = TriangleIndices[2] - Section->MinVertexIndex + Element.MinVertexIndex;
						*DestIndex++ = TriangleIndices[1] - Section->MinVertexIndex + Element.MinVertexIndex;
						*DestIndex++ = TriangleIndices[0] - Section->MinVertexIndex + Element.MinVertexIndex;
					}
				}
				else
				{
					for(INT PrimitiveIndex = 0;PrimitiveIndex < Section->NumPrimitives;PrimitiveIndex++)
					{
						*DestIndex++ = *SrcIndex++ - Section->MinVertexIndex + Element.MinVertexIndex;
						*DestIndex++ = *SrcIndex++ - Section->MinVertexIndex + Element.MinVertexIndex;
						*DestIndex++ = *SrcIndex++ - Section->MinVertexIndex + Element.MinVertexIndex;
					}
				}
			}
		}

		unguard;
	}
};

//
//	FStaticMeshBatchList::Render
//

void FStaticMeshBatchList::Render(FSceneNode* SceneNode,FRenderInterface* RI)
{
	guard(FStaticMeshBatchList::Render);

	FUNC_CLOCK(SMBatch,GStats.DWORDStats(GEngineStats.STATS_StaticMesh_BatchedRenderCycles));
	FUNC_CLOCK(SMRen,GStats.DWORDStats(GEngineStats.STATS_StaticMesh_RenderCycles));

	RI->SetTransform(TT_LocalToWorld,FMatrix::Identity);
	RI->SetCullMode(CM_CW);

	// Render solid batches.
	UBOOL ReportDynamicUploads = !SceneNode->Viewport->Precaching && SceneNode->Viewport->GetOuterUClient()->ReportDynamicUploads;

	for(INT BatchIndex = 0;BatchIndex < NumBatches;BatchIndex++)
	{
		FStaticMeshBatch&	Batch = Level->StaticMeshBatches(Batches[BatchIndex]);

		if(!Batch.Sorted)
		{
			RI->EnableLighting(0,SceneNode->Viewport->IsLit(),1,NULL,SceneNode->Viewport->Actor->RendMap == REN_LightingOnly);
			RI->SetMaterial(Batch.Material);

			FVertexStream*	VertexStreams[1] = { &Batch.Vertices };

//			DWORD ShaderType = RI->GetMaterialVertexShaderType(Batch.Material);

			INT UploadedSize = RI->SetVertexStreams(0,VertexStreams,1);
			if( UploadedSize && ReportDynamicUploads )
				debugf(TEXT("Uploading Unsorted Batch: Index=[%u] Size=[%u]"),BatchIndex,UploadedSize);
			RI->SetIndexBuffer(&Batch.Indices,0);

			for(INT ElementIndex = 0;ElementIndex < NumVisibleBatchElements[Batches[BatchIndex]];ElementIndex++)
			{
				FStaticMeshBatch::FBatchElement&	Element = Batch.Elements(VisibleBatchElements[Batches[BatchIndex]][ElementIndex]);

				RI->DrawPrimitive(PT_TriangleList,Element.FirstIndex,Element.NumPrimitives,Element.MinVertexIndex,Element.MaxVertexIndex);
				STAT_INC(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_BatchedUnsortedSections));
				STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_BatchedUnsortedTriangles),
					Element.NumPrimitives);
				STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_Triangles),Element.NumPrimitives);
			}

			STAT_INC(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_Batches));
		}
	}

	// Render sorted batches.

	for(INT BatchIndex = 0;BatchIndex < NumBatches;BatchIndex++)
	{
		FStaticMeshBatch&	Batch = Level->StaticMeshBatches(Batches[BatchIndex]);

		if(Batch.Sorted)
		{
			RI->EnableLighting(0,SceneNode->Viewport->IsLit(),1,NULL,SceneNode->Viewport->Actor->RendMap == REN_LightingOnly);
			RI->SetMaterial(Batch.Material);

			FVertexStream*						VertexStreams[1] = { &Batch.Vertices };
			FSortedStaticMeshBatchIndexBuffer	IndexBuffer(&Batch,VisibleBatchElements[Batches[BatchIndex]],NumVisibleBatchElements[Batches[BatchIndex]],SceneNode->ViewOrigin);

//			DWORD ShaderType = RI->GetMaterialVertexShaderType(Batch.Material);

			INT	UploadedSize = RI->SetVertexStreams(0,VertexStreams,1);
			
			if(UploadedSize && ReportDynamicUploads)
				debugf(TEXT("Uploading Sorted Batch: Index=[%u] Size=[%u]"),BatchIndex,UploadedSize);

			INT	BaseIndex = RI->SetDynamicIndexBuffer(&IndexBuffer,0);

			for(INT ElementIndex = 0;ElementIndex < NumVisibleBatchElements[Batches[BatchIndex]];ElementIndex++)
			{
				FStaticMeshBatch::FBatchElement&	Element = Batch.Elements(VisibleBatchElements[Batches[BatchIndex]][ElementIndex]);

				RI->DrawPrimitive(PT_TriangleList,BaseIndex,Element.NumPrimitives,Element.MinVertexIndex,Element.MaxVertexIndex);
				BaseIndex += Element.NumPrimitives * 3;

				STAT_INC(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_BatchedSortedSections));
				STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_BatchedSortedTriangles),Element.NumPrimitives);
				STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_Triangles),Element.NumPrimitives);
			}

			STAT_INC(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_Batches));
		}
	}

	// Reset the visible elements.

	NumBatches = 0;
	appMemzero(VisibleBatchElements,Level->StaticMeshBatches.Num() * sizeof(INT*));

	unguard;
}

#endif //!_XBOX
