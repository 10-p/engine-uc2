/*=============================================================================
	UnRenderBSP.cpp: BSP rendering code.
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/

#include "EnginePrivate.h"
#include "UnRenderPrivate.h"

//
//	FBspNodeList::AddNode
//

void FBspNodeList::AddNode(INT NodeIndex)
{
	FBspNode&	Node = Model->Nodes(NodeIndex);

	if(Node.iFirstVertex != INDEX_NONE)
	{
		Nodes[NumNodes++] = NodeIndex;
		NumTriangles += Node.NumVertices - 2;
	}
}

//
//	FStaticBspProjectorPrimitive
//

// Constructor.
FStaticBspProjectorPrimitive::FStaticBspProjectorPrimitive(FStaticProjectorInfo* InProjectorInfo): FStaticProjectorPrimitive(InProjectorInfo)
{
	// scion sz - use static indices
	NumIndices = ProjectorInfo->Indices.Num();
}

// GetIndices
void FStaticBspProjectorPrimitive::GetIndices(_WORD* DestIndices,_WORD BaseVertexIndex)
{
	// scion sz - copy static indices
	for( INT Index = 0; Index < ProjectorInfo->Indices.Num(); Index++ )
	{			
		*DestIndices++ = ProjectorInfo->Indices(Index) + BaseVertexIndex;
	}
}

//
//  FDynamicBspProjectorPrimitive
//

// Constructor.
FDynamicBspProjectorPrimitive::FDynamicBspProjectorPrimitive(UMaterial* InBaseMaterial,FProjectorRenderInfo* InRenderInfo,FProjectorBatchVertex* InVertices,INT InNumVertices)
{
	BaseMaterial = InBaseMaterial;
	RenderInfo = InRenderInfo;
	Vertices = InVertices;
	NumVertices = InNumVertices;

	// scion sz -
	// store indices for this dynamic projector geometry
	NumIndices = Max<INT>( 0, (NumVertices - 2) * 3 );
	Indices = New<_WORD>( GSceneMem, NumIndices );
	for( INT VertexIdx = 2, Idx = 0; Idx < NumIndices; VertexIdx++ )
	{
		Indices[Idx++] = 0;
		Indices[Idx++] = VertexIdx - 1;
		Indices[Idx++] = VertexIdx;
	}
}

// GetIndices
void FDynamicBspProjectorPrimitive::GetIndices(_WORD* DestIndices,_WORD BaseVertexIndex)
{
	// scion sz - copy indices
	for( INT Index = 0; Index < NumIndices; Index++ )
	{			
		*DestIndices++ = Indices[Index] + BaseVertexIndex;
	}
}

// GetVertices
void FDynamicBspProjectorPrimitive::GetVertices(BYTE* DestVertices)
{
	// scion sz - vertex data updated on GPU
#if DYNAMIC_PROJ_STREAM
	FVector	WorldDirection = RenderInfo->Projector->Rotation.Vector();
	FLOAT	InvMaxTraceDistance = 1.0f / RenderInfo->Projector->MaxTraceDistance;
	for(INT VertexIndex = 0;VertexIndex < NumVertices;VertexIndex++)
	{
		FProjectorBatchVertex*	DestVertex = (FProjectorBatchVertex*)DestVertices;

		DestVertex->WorldPosition = Vertices[VertexIndex].WorldPosition;
		DestVertex->ProjectedPosition = RenderInfo->Matrix.TransformFVector(DestVertex->WorldPosition);

		// scion sz - store normals			
		DestVertex->Normal = Vertices[VertexIndex].Normal;

		// Calculate the vertex attenuation.
		FLOAT	LifeAttenuation = RenderInfo->Expires == 0.0 ? 1.0 : Clamp<FLOAT>(RenderInfo->Expires - RenderInfo->LastRenderTime,0.0f,1.0f);

		FLOAT	DirectionalAttenuation = 1.0f;

		FVector Normal = DestVertex->Normal;
		if(!(RenderInfo->ProjectorFlags & PRF_ProjectOnBackfaces))
			DirectionalAttenuation = Max(Normal | -WorldDirection,0.0f);

		FLOAT	DistanceAttenuation = 1.0f;

		if(RenderInfo->ProjectorFlags & PRF_Gradient)
			DistanceAttenuation = Clamp(1.0f - ((DestVertex->WorldPosition - RenderInfo->Projector->Location) | WorldDirection) * InvMaxTraceDistance,0.0f,1.0f);

		BYTE	AttenuationByte = Clamp<INT>(appFloor(DirectionalAttenuation * DistanceAttenuation * LifeAttenuation * 255.0f),0,255);
		DestVertex->Attenuation = FColor(AttenuationByte,AttenuationByte,AttenuationByte,AttenuationByte);

		DestVertices += sizeof(FProjectorBatchVertex);

		if(BaseMaterial)
		{
			FLOAT*	DestUV = (FLOAT*)DestVertices;
			DestUV[0] = 0.0f;
			DestUV[1] = 0.0f;
			DestVertices += sizeof(FLOAT) * 2;
		}
	}
#endif
}

// scion sz -
void FDynamicBspProjectorPrimitive::GetRawVertices( void** DestVertices, INT FirstVertex )
{
	checkSlow( FirstVertex < NumVertices );
	*DestVertices = &Vertices[FirstVertex];
}

// scion sz -
void FDynamicBspProjectorPrimitive::GetRawIndices( void** DestIndices )
{
	*DestIndices = Indices;
}

//
//  FBspDrawList::AddNode
//

void FBspDrawList::AddNode(INT NodeIndex,FDynamicLight** InDynamicLights,INT NumDynamicLights,FProjectorRenderInfo** InDynamicProjectors,INT NumDynamicProjectors,FLevelSceneNode* SceneNode)
{
	guard(FBspDrawList::AddNode);

	FBspSection&	Section = Model->Sections(SectionIndex);
	FBspNode&		Node = Model->Nodes(NodeIndex);
	FBspSurf&		Surf = Model->Surfs(Node.iSurf);

	FBspNodeList::AddNode(NodeIndex);

	if(!(Section.PolyFlags & PF_Unlit) && Section.iLightMapTexture != INDEX_NONE)
	{
		// Add draw lists for dynamic lights hitting this node.

		for(INT LightIndex = 0;LightIndex < NumDynamicLights;LightIndex++)
		{
			FDynamicLight*		Light = InDynamicLights[LightIndex];

			if(!UPDATE_DYNAMIC_LIGHTMAPS && Light->Dynamic  
				// scion sz - skip skeletal only dynamic lights
				&& Light->Actor->AffectType != AT_AffectSkeletal )
			{
				FBspLightDrawList*	LightDrawList = NULL;

				// See if this node is actually hit by the light.

				if(Light->Actor->LightEffect == LE_Sunlight)
				{
					if(!((-Light->Direction | Node.Plane) > 0.0f || (-Light->Direction | Node.Plane) < 0.0f && (Surf.PolyFlags & PF_TwoSided)))
						continue;
				}
				else
				{
					if((Light->Position - Node.ExclusiveSphereBound).SizeSquared() > Square(Light->Radius + Node.ExclusiveSphereBound.W))
						continue;
				}

				// Find a draw list for this light.

				for(TList<FBspLightDrawList*>* LightDrawListPtr = DynamicLights;LightDrawListPtr;LightDrawListPtr = LightDrawListPtr->Next)
				{
					if(LightDrawListPtr->Element->Light == Light)
					{
						LightDrawList = LightDrawListPtr->Element;
						break;
					}
				}

				// If there isn't a draw list yet, create one.

				if(!LightDrawList)
				{
					LightDrawList = new(GSceneMem) FBspLightDrawList(this,Light);
					DynamicLights = new(GSceneMem) TList<FBspLightDrawList*>(LightDrawList,DynamicLights);
				}

				// Add the node to the draw list.

				LightDrawList->AddNode(NodeIndex);
			}
		}
	}

	CLOCK(Proj,GStats.DWORDStats( GEngineStats.STATS_BSP_ProjectorRenderCycles ));

	// Remove old projectors on this node.
	// This is done in a seperate pass to allow caching of FStaticProjectorInfo pointers.

	for(INT ProjectorIndex = 0;ProjectorIndex < Node.Projectors.Num();ProjectorIndex++)
	{
		FStaticProjectorInfo*	ProjectorInfo = Node.Projectors(ProjectorIndex);

		// See if this projector is actually visible.

		if(!ProjectorInfo->RenderInfo->Render( SceneNode->Viewport->Actor->GetViewTarget()->Level->TimeSeconds ))
		{
			Node.Projectors(ProjectorIndex) = ProjectorInfo->RemoveRef();
			Node.Projectors.Remove(ProjectorIndex--);
			continue;
		}
	}
	
	// Add draw lists for static projectors hitting this node.	
	for(INT ProjectorIndex = 0;ProjectorIndex < Node.Projectors.Num();ProjectorIndex++)
	{
		FStaticProjectorInfo*	ProjectorInfo = Node.Projectors(ProjectorIndex);

		// See if this projector is actually visible.

		if((Section.PolyFlags & PF_Unlit) && !(ProjectorInfo->RenderInfo->ProjectorFlags & PRF_ProjectOnUnlit))
			continue;

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
			// Add the static projector primitive to the batch only once 
			ProjectorBatch->AddPrimitive(new(GSceneMem) FStaticBspProjectorPrimitive(ProjectorInfo));
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

#if DYNAMIC_PROJ_STREAM		
        // Add the projector to the batch.
		ProjectorBatch->AddPrimitive(new(GSceneMem) FStaticBspProjectorPrimitive(ProjectorInfo));
#endif
		
	}	

	// Add draw lists for dynamic projectors hitting this node.
	for(INT ProjectorIndex = 0;ProjectorIndex < NumDynamicProjectors;ProjectorIndex++)
	{
		FProjectorRenderInfo*	ProjectorRenderInfo = InDynamicProjectors[ProjectorIndex];

		// See if this projector is actually visible.

		if((Section.PolyFlags & PF_Unlit) && !(ProjectorRenderInfo->ProjectorFlags & PRF_ProjectOnUnlit))
			continue;
		
		if( !ProjectorRenderInfo->Projector->bProjectBSP )
			continue;

		// scion sz - only add this node if it's not backfacing relative to projector
		if( !ProjectorRenderInfo->Projector->bProjectOnBackfaces &&
			(Node.Plane|ProjectorRenderInfo->FrustumPlanes[0]) > 0 )
			continue;

		// Clip the projector.

		FPoly	Poly;

		Poly.Init();

		for( INT v=0;v<Node.NumVertices;v++ )
			Poly.Vertex[v] = Model->Points(Model->Verts(Node.iVertPool + v).pVertex);

		Poly.Normal = Node.Plane;
		Poly.NumVertices = Node.NumVertices;

		for( INT p=0;p<6;p++ )
		{
			FPlane	Plane = ProjectorRenderInfo->FrustumPlanes[p];

			if(!Poly.Split(Plane,Plane * Plane.W,1))
			{
				Poly.NumVertices = 0;
				break;
			}
		}

		if(Poly.NumVertices > 2)
		{
			// Determine the base material.

			UMaterial*	BaseMaterial = NULL;
			UBOOL		TwoSided = 0;

#ifndef _XBOX

			if(ProjectorRenderInfo->MaterialBlendingOp != PB_None)
				BaseMaterial = Section.Material;
			else
			{
				UShader*		BaseShader = Cast<UShader>(Section.Material);
				UTexture*		BaseTexture = Cast<UTexture>(Section.Material);	
				UFinalBlend*	BaseFinalBlend = Cast<UFinalBlend>(Section.Material);

				if(BaseTexture && (BaseTexture->bMasked || BaseTexture->bAlphaTexture))
					BaseMaterial = Section.Material;
				else if(BaseShader)
				{
					if(BaseShader->Opacity)
						BaseMaterial = Section.Material;

					TwoSided = BaseShader->TwoSided;
				}
				else if(BaseFinalBlend && BaseFinalBlend->FrameBufferBlending == FB_AlphaBlend)
				{
					BaseMaterial = Section.Material;
					TwoSided = BaseFinalBlend->TwoSided;
				}
			}
#endif

			// Find an existing batch for the projector.
			FProjectorRenderBatch*	ProjectorBatch = NULL;
			for(TList<FProjectorRenderBatch*>* ProjectorBatchList = GProjectorBatchList;ProjectorBatchList;ProjectorBatchList = ProjectorBatchList->Next)
			{
				if( ProjectorBatchList->Element->MatchProjectorInfo( ProjectorRenderInfo, BaseMaterial, TwoSided, FProjectorRenderBatch::BATCH_DynamicBSP ) )
				{
					ProjectorBatch = ProjectorBatchList->Element;
					break;
				}
			}

			// Create a new batch if necessary.
			if(!ProjectorBatch)
			{
#if DYNAMIC_PROJ_STREAM
				ProjectorBatch = new(GSceneMem) FProjectorRenderBatchDef(
													BaseMaterial,
													ProjectorRenderInfo->Material,
													(EProjectorBlending)ProjectorRenderInfo->MaterialBlendingOp,
													(EProjectorBlending)ProjectorRenderInfo->FrameBufferBlendingOp,
													TwoSided
													);
#else
				// scion sz - only batch dynamic projectors for bsp 
				ProjectorBatch = new(GSceneMem) FProjectorRenderBatchBSPNodes( ProjectorRenderInfo );
#endif
				GProjectorBatchList = new(GSceneMem) TList<FProjectorRenderBatch*>(ProjectorBatch,GProjectorBatchList);
			}

			// Add the projector to the batch.
			FProjectorBatchVertex*	ProjectorVertices = New<FProjectorBatchVertex>(GSceneMem,Poly.NumVertices);
			// fill in vertices
			for(INT VertexIndex = 0;VertexIndex < Poly.NumVertices;VertexIndex++)
			{
				ProjectorVertices[VertexIndex].WorldPosition = Poly.Vertex[Poly.NumVertices - VertexIndex - 1];
				// scion sz - store normals
				ProjectorVertices[VertexIndex].Normal = Node.Plane;
			}
            // add dynamic projector primitive to this batch
			ProjectorBatch->AddPrimitive(new(GSceneMem) FDynamicBspProjectorPrimitive(BaseMaterial,ProjectorRenderInfo,ProjectorVertices,Poly.NumVertices));
		}
	}

	UNCLOCK();

	unguard;
}

//
//  FBspIndexBuffer
//

class FBspIndexBuffer : public FIndexBuffer
{
public:

	FBspNodeList*	NodeList;
	_WORD			MinIndex,
					MaxIndex;

	// Constructor.

	FBspIndexBuffer(FBspNodeList* InNodeList)
	{
		NodeList = InNodeList;

		MinIndex = 32767;
		MaxIndex = 0;
	}

	// FRenderResource interface.

	virtual INT GetRevision() { return 1; }

	// FIndexBuffer interface.

	virtual INT GetSize() { return NodeList->NumTriangles * 3 * sizeof(_WORD); }
	virtual INT GetIndexSize() { return sizeof(_WORD); }
	virtual void GetContents(void* Data)
	{
		_WORD* WordData = (_WORD*)Data;
		// Generate indices for the nodes in the draw list.

		for(INT NodeIndex = 0;NodeIndex < NodeList->NumNodes;NodeIndex++)
		{
			FBspNode&	Node = NodeList->Model->Nodes(NodeList->Nodes[NodeIndex]);

			if(Node.NumVertices > 0)
			{
				for(INT VertexIndex = 2;VertexIndex < Node.NumVertices;VertexIndex++)
				{
					*WordData++ = Node.iFirstVertex;
					*WordData++ = Node.iFirstVertex + VertexIndex;
					*WordData++ = Node.iFirstVertex + VertexIndex - 1;
				}

				MinIndex = Min<_WORD>(Node.iFirstVertex,MinIndex);
				MaxIndex = Max<_WORD>(Node.iFirstVertex + Node.NumVertices - 1,MaxIndex);
			}
		}
	}
};

//
//  FBspDrawList::Render
//

void FBspDrawList::Render(FLevelSceneNode* SceneNode,FRenderInterface* RI,FProjectorRenderInfo** InDynamicProjectors,INT NumDynamicProjectors)
{
	guard(FBspDrawList::Render);

	FUNC_CLOCK(BSPRenCyc,GStats.DWORDStats(GEngineStats.STATS_BSP_RenderCycles));

	if(NumTriangles > 0)
	{
		FBspSection&	Section = Model->Sections(SectionIndex);

		RI->PushState();

		// Set the material.

		UBOOL			RenderBatched = !SceneNode->Viewport->HitTesting;
		FBaseTexture*	LightMap = NULL;

		if(SceneNode->Viewport->IsLit() && !(Section.PolyFlags & PF_Unlit))
		{
			if(Section.iLightMapTexture != INDEX_NONE && Section.iLightMapTexture < Model->LightMapTextures.Num())
			{
				FLightMapTexture* LightMapTexture = &Model->LightMapTextures(Section.iLightMapTexture);

				if( SceneNode->Viewport->RenDev->UseCompressedLightmaps && (LightMapTexture->StaticTexture.Revision == LightMapTexture->Revision) )
					LightMap = &LightMapTexture->StaticTexture;
				else
					LightMap = LightMapTexture;
			}
		}

#ifndef FINAL_RELEASE
		if(SceneNode->Viewport->IsWire())
		{
			DECLARE_STATIC_UOBJECT(
				UShader,
				BspWireframeShader,
				{
					BspWireframeShader->Wireframe = 1;
				}
				);

			RI->SetMaterial(BspWireframeShader);
		}
		else if(SceneNode->Viewport->Actor->RendMap == REN_Zones || SceneNode->Viewport->Actor->RendMap == REN_Polys || SceneNode->Viewport->Actor->RendMap == REN_PolyCuts)
		{
			DECLARE_STATIC_UOBJECT(
				UShader,
				BspZoneShader,
				{
				}
				);

			RI->EnableLighting(1,0);
			RI->SetMaterial(BspZoneShader);

			RenderBatched = 0;
		}
		else
#endif
		{
			RI->EnableLighting(0,0,1,LightMap,SceneNode->Viewport->Actor->RendMap == REN_LightingOnly);
			//scion jg -- Handle team coloring if needed
			if (Section.Material->RenderHint & MRH_TeamEvironShader)
			{
				// Get the BSP node so we can get the zone it's in
				FBspNode& Node = Model->Nodes(Nodes[0]);
				// Get the zone for this node checking which side it's on
				AZoneInfo* pZone = SceneNode->Level->GetZoneActor(Node.iZone[1]);
				UPSTeamShader* pShader = (UPSTeamShader*)Section.Material;
				// Figure out which team it belongs to and color it
				pShader->Color = pZone->TeamColor;
			}
			RI->SetMaterial(Section.Material);
		}

		// Set the vertex stream.

		FVertexStream*	Streams[] = { &Section.Vertices };
		RI->SetVertexStreams(VS_FixedFunction,Streams,1);

		// Build an index buffer with only the nodes in the draw list in it.

		FBspIndexBuffer	IndexBuffer(this);

		INT	BaseIndex = RI->SetDynamicIndexBuffer(&IndexBuffer,0);

		if(Section.PolyFlags & (PF_TwoSided | PF_Portal))
			RI->SetCullMode(CM_None);

		if(RenderBatched)
		{
			// Render all the nodes in one chunk.
			BEGIN_D3D_EVENT(BDE)(PIX_BSP,TEXT("BSP"));
			RI->DrawPrimitive(PT_TriangleList,BaseIndex,NumTriangles,IndexBuffer.MinIndex,IndexBuffer.MaxIndex);
			END_D3D_EVENT;
		}
		else
		{
			// Render the nodes one by one to perform hit testing.

			INT	VertexIndex = 0;

			for(INT NodeIndex = 0;NodeIndex < NumNodes;NodeIndex++)
			{
				FBspNode&	Node = Model->Nodes(Nodes[NodeIndex]);
				FBspSurf&	Surf = Model->Surfs(Node.iSurf);

				if(SceneNode->Viewport->Actor->RendMap == REN_Polys || SceneNode->Viewport->Actor->RendMap == REN_PolyCuts || SceneNode->Viewport->Actor->RendMap == REN_Zones)
				{
					FVector	Color;

					if(SceneNode->Viewport->Actor->RendMap == REN_Polys)
					{
						INT Index = Section.Material ? Section.Material->GetIndex() : 0;
						Color = FVector((Index * 67) & 255,(Index * 1371) & 255,(Index * 1991) & 255) / 256.0f;
					}
					else if(SceneNode->Viewport->Actor->RendMap != REN_Zones || SceneNode->Model->NumZones==0 )
					{
						Color = FColor(255,255,255); //Color = Section.Material->MipZero.Plane() * (0.5f + (Nodes[NodeIndex] & 7) / 16.0f);
					}
					else
					{
						INT		iZone = Node.iZone[Surf.Plane.PlaneDot(SceneNode->ViewOrigin) >= 0.0f];

						if(iZone == 0)
							Color = FColor(255,255,255); //Color = Section.Material->MipZero.Plane();
						else
							Color = FVector((iZone * 67) & 255,(iZone * 1371) & 255,(iZone * 1991) & 255) / 256.0f;

						Color *= (0.5f + (Nodes[NodeIndex] & 7) / 16.0f);
					}

					RI->SetAmbientLight(FColor(Color));
				}

				PUSH_HIT(SceneNode->Viewport,HBspSurf(Node.iSurf));
				RI->DrawPrimitive(PT_TriangleList,BaseIndex + VertexIndex,Node.NumVertices - 2,IndexBuffer.MinIndex,IndexBuffer.MaxIndex);
				POP_HIT(SceneNode->Viewport);

				VertexIndex += (Node.NumVertices - 2) * 3;
			}
		}

		if(!SceneNode->Viewport->HitTesting)
		{
			if(SceneNode->Viewport->IsLit() && !(Section.PolyFlags & PF_Unlit) && DynamicLights)
			{
				// Render the dynamic lights.

				for(TList<FBspLightDrawList*>* LightDrawListPtr = DynamicLights;LightDrawListPtr;LightDrawListPtr = LightDrawListPtr->Next)
					LightDrawListPtr->Element->Render(SceneNode,RI);
			}

#ifndef FINAL_RELEASE
			if((Section.PolyFlags & PF_Selected) && (SceneNode->Viewport->Actor->ShowFlags & SHOW_SelectionHighlight))
			{
				// Render the selection highlight.

				static FSolidColorTexture	SelectionTexture(FColor(10,5,60,255));

				DECLARE_STATIC_UOBJECT(
					UProxyBitmapMaterial,
					BspSelectionBitmapMaterial,
					{
						BspSelectionBitmapMaterial->SetTextureInterface(&SelectionTexture);
					}
					);

				DECLARE_STATIC_UOBJECT(
					UFinalBlend,
					BspSelectionFinalBlend,
					{
						BspSelectionFinalBlend->Material = BspSelectionBitmapMaterial;
						BspSelectionFinalBlend->FrameBufferBlending = FB_Brighten;
						BspSelectionFinalBlend->ZWrite = 0;
						//BspSelectionFinalBlend->TwoSided = (Surf.PolyFlags & PF_TwoSided) ? 1 : 0;
					}
					);

				RI->EnableLighting(0,0);
				RI->SetMaterial(BspSelectionFinalBlend);
				RI->DrawPrimitive(PT_TriangleList,BaseIndex,NumTriangles,IndexBuffer.MinIndex,IndexBuffer.MaxIndex);
			}
#endif
		}

// scion sz - not used
#if 0
			// scion sz - render dynamic projectors directly on xbox
			CLOCK(DynProj,GStats.DWORDStats( GEngineStats.STATS_BSP_ProjectorRenderCycles ));
			// Setup blending.
			RI->EnableLighting(0,0);
			// set poly offset
			RI->SetZBias(1);

			for(INT ProjectorIndex = 0;ProjectorIndex < NumDynamicProjectors;ProjectorIndex++)
			{
				FProjectorRenderInfo*	ProjectorRenderInfo = InDynamicProjectors[ProjectorIndex];

				// See if this projector is actually visible.
				if( !ProjectorRenderInfo->Render(SceneNode->Viewport->Actor->GetViewTarget()->Level->TimeSeconds) ||
					((Section.PolyFlags & PF_Unlit) && !(ProjectorRenderInfo->ProjectorFlags & PRF_ProjectOnUnlit)) )
					continue;

				INT	VertexIndex = 0;
				for( INT NodeIndex = 0; NodeIndex < NumNodes; NodeIndex++ )
				{
					FBspNode&	Node = Model->Nodes(Nodes[NodeIndex]);

					// Clip the projector.
					FPoly	Poly;
					Poly.Init();
					for( INT v=0;v<Node.NumVertices;v++ )
						Poly.Vertex[v] = Model->Points(Model->Verts(Node.iVertPool + v).pVertex);
					Poly.Normal = Node.Plane;
					Poly.NumVertices = Node.NumVertices;
					for( INT p=0;p<6;p++ )
					{
						FPlane	Plane = ProjectorRenderInfo->FrustumPlanes[p];
						if(!Poly.Split(Plane,Plane * Plane.W,1))
						{
							Poly.NumVertices = 0;
							break;
						}
					}
					if( Poly.NumVertices > 0 )
					{
						// scion sz -
						// only add to the multiprojector material if the projector has a simple bitmap
						if( ProjectorRenderInfo->GetProjectedBitmap() )
						{
							FMultiMaterialProjectorInfo ProjMultMatInfo( Section.Material, ProjectorRenderInfo->Projector->bClipBSP );
							ProjMultMatInfo.AddProjectorEntry( ProjectorRenderInfo );
							UProjectorMultiMaterial* ProjMultMat = ProjMultMatInfo.GetMaterial( (EProjectorBlending)ProjectorRenderInfo->FrameBufferBlendingOp );
							RI->SetMaterial( ProjMultMat );
						}
						else
						{
							// render the section using the default projector material
							RI->SetMaterial( ProjectorRenderInfo->GetMaterial(SceneNode,NULL) );					
						}

						BEGIN_D3D_EVENT(BDE)(PIX_BSP,TEXT("BSP"));
						RI->DrawPrimitive(PT_TriangleList,BaseIndex + VertexIndex,Node.NumVertices - 2,IndexBuffer.MinIndex,IndexBuffer.MaxIndex);
						END_D3D_EVENT;
					}

					VertexIndex += (Node.NumVertices - 2) * 3;
				}				
			}

			UNCLOCK();
#endif

		RI->PopState();
	}

#ifndef FINAL_RELEASE
	// Update the stats.
	GStats.DWORDStats( GEngineStats.STATS_BSP_Sections )++;
	GStats.DWORDStats( GEngineStats.STATS_BSP_Nodes ) += NumNodes;
	GStats.DWORDStats( GEngineStats.STATS_BSP_Triangles ) += NumTriangles;
#endif
	unguard;
}

//
//	FBspLinearLightAttenuationTexture
//

class FBspLinearLightAttenuationTexture : public FTexture
{
public:

	INT		WidthHeight;

	// Constructor.

	FBspLinearLightAttenuationTexture(INT InWidthHeight)
	{
		WidthHeight = InWidthHeight;
	}

	// FRenderResource interface.

	virtual INT GetRevision() { return 1; }

	// FBaseTexture interface.

	virtual INT GetWidth() { return WidthHeight; }
	virtual INT GetHeight() { return WidthHeight; }
	virtual INT GetNumMips() { return 1; }
	virtual INT GetFirstMip() { return 0; }
	virtual ETextureFormat GetFormat() { return TEXF_RGBA8; }
	virtual ETexClampMode GetUClamp() { return TC_Clamp; }
	virtual ETexClampMode GetVClamp() { return TC_Clamp; }

	// FTexture interface.

	virtual void* GetRawTextureData(INT MipIndex) { return NULL; }
	virtual void UnloadRawTextureData( INT MipIndex ) {}
	virtual void GetTextureData(INT MipIndex,void* Dest,INT DestStride,ETextureFormat DestFormat,UBOOL ColoredMips)
	{
		guard(FBspLightAttenuationTexture::GetTextureData);

		check(DestFormat == TEXF_RGBA8);
		check(DestStride == WidthHeight * 4);
		check(MipIndex == 0);

		FColor*	TexturePointer = (FColor*) Dest;
		FLOAT	Center = (WidthHeight - 1) / 2.0f,
				Radius = (WidthHeight - 2) - Center;

		for(INT Y = 0;Y < WidthHeight;Y++)
		{
			for(INT X = 0;X < WidthHeight;X++)
			{
				FLOAT	Distance = appSqrt(Square(X - Center) + Square(Y - Center));

				if(Distance < Radius)
					*TexturePointer++ = FColor(FPlane(0.5f,0.5f,0.5f,1) * (1.0f - Distance / Radius));
				else
					*TexturePointer++ = FColor(0,0,0,0);
			}
		}

		unguard;
	}
	virtual UTexture* GetUTexture() { return NULL; }
};

//
//	FBspQuadraticLightAttenuationTexture
//

class FBspQuadraticLightAttenuationTexture : public FTexture
{
public:

	INT		WidthHeight;

	// Constructor.

	FBspQuadraticLightAttenuationTexture(INT InWidthHeight)
	{
		WidthHeight = InWidthHeight;
	}

	// FRenderResource interface.

	virtual INT GetRevision() { return 1; }

	// FBaseTexture interface.

	virtual INT GetWidth() { return WidthHeight; }
	virtual INT GetHeight() { return WidthHeight; }
	virtual INT GetNumMips() { return 1; }
	virtual INT GetFirstMip() { return 0; }
	virtual ETextureFormat GetFormat() { return TEXF_RGBA8; }
	virtual ETexClampMode GetUClamp() { return TC_Clamp; }
	virtual ETexClampMode GetVClamp() { return TC_Clamp; }

	// FTexture interface.

	virtual void UnloadRawTextureData(INT MipIndex) {}
	virtual void* GetRawTextureData(INT MipIndex) { return NULL; }
	virtual void GetTextureData(INT MipIndex,void* Dest,INT DestStride,ETextureFormat DestFormat,UBOOL ColoredMips)
	{
		guard(FBspQuadraticLightAttenuationTexture::GetTextureData);

		check(DestFormat == TEXF_RGBA8);
		check(DestStride == WidthHeight * 4);
		check(MipIndex == 0);

		FColor*	TexturePointer = (FColor*) Dest;
		FLOAT	Center = (WidthHeight - 1) / 2.0f,
				RadiusSquared = Square<FLOAT>((WidthHeight - 2) - Center);

		for(INT Y = 0;Y < WidthHeight;Y++)
		{
			for(INT X = 0;X < WidthHeight;X++)
			{
				FLOAT	DistanceSquared = Square(X - Center) + Square(Y - Center);

				if(DistanceSquared < RadiusSquared)
					*TexturePointer++ = FColor(FPlane(1.0f,1.0f,1.0f,1) * (1.0f - appSqrt(DistanceSquared / RadiusSquared)));
				else
					*TexturePointer++ = FColor(0,0,0,0);
			}
		}

		unguard;
	}
	virtual UTexture* GetUTexture() { return NULL; }
};

//
//	FBspLightVertex
//

struct FBspLightVertex
{
	FVector	Position;
	FColor	Color;
	FLOAT	U,
			V,
			U2,
			V2;
};

//
//	FBspLightVertexStream
//

class FBspLightVertexStream : public FVertexStream
{
public:

	FBspLightDrawList*	DrawList;
	FDynamicLight*		Light;

	// Constructor.

	FBspLightVertexStream(FBspLightDrawList* InDrawList,FDynamicLight* InLight)
	{
		guard(FBspLightVertexStream::FBspLightVertexStream);

		DrawList = InDrawList;
		Light = InLight;

		unguard;
	}

	// ProjectAttenuationMap

	void ProjectAttenuationMap(FBspLightVertex* DestVertex,FBspVertex* SourceVertex,FVector& U,FVector& V,FLOAT PlaneRadius,FColor Color)
	{
		DestVertex->Position = SourceVertex->Position;
		DestVertex->U = SourceVertex->U;
		DestVertex->V = SourceVertex->V;
		DestVertex->U2 = 0.5f + ((SourceVertex->Position - Light->Position) | U) / PlaneRadius / 2.0f;
		DestVertex->V2 = 0.5f + ((SourceVertex->Position - Light->Position) | V) / PlaneRadius / 2.0f;
		DestVertex->Color = Color;
	}

	// FRenderResource interface.

	virtual INT GetRevision() { return 1; }

	// FVertexStream interface.

	virtual INT GetSize() { return DrawList->NumTriangles * 3 * sizeof(FBspLightVertex); }
	virtual INT GetStride() { return sizeof(FBspLightVertex); }

	virtual INT GetComponents(FVertexComponent* Components)
	{
#if !defined(_XBOX)
		Components[0] = FVertexComponent(CT_Float3,FVF_Position);
		Components[1] = FVertexComponent(CT_Color,FVF_Diffuse);
		Components[2] = FVertexComponent(CT_Float2,FVF_TexCoord0);
		Components[3] = FVertexComponent(CT_Float2,FVF_TexCoord1);
#else
		Components[0].Type = CT_Float3;
		Components[0].Function = FVF_Position;
		Components[1].Type = CT_Color;
		Components[1].Function = FVF_Diffuse;
		Components[2].Type = CT_Float2;
		Components[2].Function = FVF_TexCoord0;
		Components[3].Type = CT_Float2;
		Components[3].Function = FVF_TexCoord1;
#endif	
		return 4;
	}
	virtual void GetStreamData(void* Dest)
	{
		guard(FBspLightVertexStream::GetStreamData);

		// Generate vertices for the nodes in the draw list.

		FBspLightVertex*	VertexPointer = (FBspLightVertex*) Dest;
		FBspVertex*			StaticVertices = &DrawList->Model->Sections(DrawList->SectionIndex).Vertices.Vertices(0);
		FLOAT				RadiusSquared = Square(Light->Radius);

		for(INT NodeIndex = 0;NodeIndex < DrawList->NumNodes;NodeIndex++)
		{
			FBspNode&	Node = DrawList->Model->Nodes(DrawList->Nodes[NodeIndex]);
			FVector		U,
						V;
			FLOAT		PlaneDot = Node.Plane.PlaneDot(Light->Position),
						DistanceSquared = Square(PlaneDot),
						PlaneRadius = appSqrt(RadiusSquared - DistanceSquared);
			FColor		Color;

			if(Light->Actor->LightEffect == LE_QuadraticNonIncidence)
				Color = FColor(appSqrt(1.0f - DistanceSquared / RadiusSquared) * Light->Color);
			else
				Color = FColor(appSqrt(1.0f - Abs(PlaneDot) / Light->Radius) * Light->Color);

			Color.A = 255;
			Color	= Color.RenderColor();

			Node.Plane.FindBestAxisVectors(U,V);

			// Calculate the vertices for the first triangle in the node.

			ProjectAttenuationMap(&VertexPointer[0],&StaticVertices[Node.iFirstVertex + 0],U,V,PlaneRadius,Color);
			ProjectAttenuationMap(&VertexPointer[1],&StaticVertices[Node.iFirstVertex + 2],U,V,PlaneRadius,Color);
			ProjectAttenuationMap(&VertexPointer[2],&StaticVertices[Node.iFirstVertex + 1],U,V,PlaneRadius,Color);

			// Calculate the vertices for the rest of the triangles in the node.

			FBspLightVertex*	LastVertex = &VertexPointer[1];

			for(INT TriangleIndex = 1;TriangleIndex < Node.NumVertices - 2;TriangleIndex++)
			{
				appMemcpy(&VertexPointer[TriangleIndex * 3 + 0],&VertexPointer[0],sizeof(FBspLightVertex));
				appMemcpy(&VertexPointer[TriangleIndex * 3 + 2],LastVertex,sizeof(FBspLightVertex));

				ProjectAttenuationMap(&VertexPointer[TriangleIndex * 3 + 1],&StaticVertices[Node.iFirstVertex + TriangleIndex + 2],U,V,PlaneRadius,Color);
				LastVertex = &VertexPointer[TriangleIndex * 3 + 1];
			}

			VertexPointer += (Node.NumVertices - 2) * 3;
		}

		unguard;
	}
	virtual void GetRawStreamData(void** Dest,INT FirstVertex) { }
};

//
//	FBspLightDrawList::Render
//

void FBspLightDrawList::Render(FLevelSceneNode* SceneNode,FRenderInterface* RI)
{
	guard(FBspLightDrawList::Render);

	static FBspLinearLightAttenuationTexture	LinearAttenuationTexture(64);
	static FBspQuadraticLightAttenuationTexture	QuadraticAttenuationTexture(64);

	FUNC_CLOCK(DynLightCyc,GStats.DWORDStats(GEngineStats.STATS_BSP_DynamicLightingCycles));

	if( NumTriangles > 0 && 
			(Light->Actor->LightEffect == LE_None || 
			 Light->Actor->LightEffect == LE_NonIncidence || 
			 Light->Actor->LightEffect == LE_QuadraticNonIncidence) )
	{
		FBspSection&	Section = Model->Sections(SectionIndex);
		RI->PushState();
		
		// scion sz - no stencil masking needed from these bsp light sheets
		RI->EnableStencil(0);

		// Setup blending.

		DECLARE_STATIC_UOBJECT(
		UFinalBlend,
			DynamicLightFinalBlend,
		{
			DynamicLightFinalBlend->ZWrite = 0;			
		}
		);
		DynamicLightFinalBlend->AlphaTest = 0;
		DynamicLightFinalBlend->AlphaRef = 0;

#ifndef FINAL_RELEASE
		if(SceneNode->Viewport->Actor->RendMap == REN_LightingOnly)
		{
			DECLARE_STATIC_UOBJECT(
			UProxyBitmapMaterial,
				WhiteTextureMaterial,
			{
				static FSolidColorTexture	WhiteTexture(FColor(255,255,255,255));

				WhiteTextureMaterial->SetTextureInterface(&WhiteTexture);
			}
			);

			DynamicLightFinalBlend->FrameBufferBlending = FB_Translucent;
			DynamicLightFinalBlend->Material = WhiteTextureMaterial;
		}
		else
#endif
		{
			UMaterial*		Diffuse = NULL;
			UMaterial*		Opacity = NULL;
			UShader*		Shader = Cast<UShader>(Section.Material);
			UTexture*		Texture = Cast<UTexture>(Section.Material);
			UFinalBlend*	FinalBlend = Cast<UFinalBlend>(Section.Material);

			if(Shader)
			{
				Diffuse = Shader->Diffuse;

				if(Shader->Opacity)
				{
					Opacity = Shader->Opacity;
					DynamicLightFinalBlend->FrameBufferBlending = FB_Brighten;
				}
				else
					DynamicLightFinalBlend->FrameBufferBlending = FB_Translucent;

				if(Shader->OutputBlending == OB_Masked)
					DynamicLightFinalBlend->FrameBufferBlending = FB_Translucent;
			}
			else if(Texture)
			{
				Diffuse = Texture;

				if(Texture->bAlphaTexture)
				{
					Opacity = Texture;
					DynamicLightFinalBlend->FrameBufferBlending = FB_Brighten;
				}
				else if(Texture->bMasked)
				{
					Opacity = Texture;
					DynamicLightFinalBlend->AlphaTest = 1;
					DynamicLightFinalBlend->AlphaRef = 127;
					DynamicLightFinalBlend->FrameBufferBlending = FB_Translucent;
				}
				else
					DynamicLightFinalBlend->FrameBufferBlending = FB_Translucent;
			}
			else if(FinalBlend)
			{
				switch(FinalBlend->FrameBufferBlending)
				{
				case FB_AlphaBlend:
					Diffuse = FinalBlend->Material;
					Opacity = FinalBlend->Material;
					DynamicLightFinalBlend->FrameBufferBlending = FB_Brighten;
					break;
				default:
					Diffuse = FinalBlend->Material;
					DynamicLightFinalBlend->FrameBufferBlending = FB_Translucent;
					break;
				};

				DynamicLightFinalBlend->AlphaTest = FinalBlend->AlphaTest;
				DynamicLightFinalBlend->AlphaRef = FinalBlend->AlphaRef;
			}
			else if(Section.Material)
				Diffuse = Section.Material;
			else
				Diffuse = CastChecked<UBitmapMaterial>(Cast<UMaterial>(UMaterial::StaticClass()->GetDefaultObject())->DefaultMaterial);

			DynamicLightFinalBlend->Material = Diffuse;

			if(Opacity)
			{
				if(Opacity != Diffuse)
				{
					DECLARE_STATIC_UOBJECT(
					UCombiner,
						OpacityCombiner,
					{}
					);

					OpacityCombiner->Material1 = Diffuse;
					OpacityCombiner->Mask = Opacity;
					OpacityCombiner->CombineOperation = CO_Use_Color_From_Material1;
					OpacityCombiner->AlphaOperation = AO_Use_Mask;

					DynamicLightFinalBlend->Material = OpacityCombiner;
				}
			}
		}

		if(Light->Actor->LightEffect == LE_QuadraticNonIncidence)
			RI->EnableLighting(0,1,1,&QuadraticAttenuationTexture,0);
		else
			RI->EnableLighting(0,1,1,&LinearAttenuationTexture,0);

		RI->SetMaterial(DynamicLightFinalBlend);

		// Build a vertex stream containing the nodes' vertices with the attenuation map projected.

		FBspLightVertexStream	VertexStream(this,Light);

		INT	BaseIndex = RI->SetDynamicStream(VS_FixedFunction,&VertexStream);

		RI->SetIndexBuffer(NULL,0);

		// Render the light.

		RI->DrawPrimitive(PT_TriangleList,BaseIndex,NumTriangles);

		// scion sz - restore stencil
		RI->EnableStencil(1);

		RI->PopState();
	}

#ifndef FINAL_RELEASE
	GStats.DWORDStats(GEngineStats.STATS_BSP_DynamicLights)++;
	GStats.DWORDStats(GEngineStats.STATS_BSP_Nodes) += NumNodes;
	GStats.DWORDStats(GEngineStats.STATS_BSP_Triangles) += NumTriangles;
#endif
	
	unguard;
}

//
//  FBspStencilDrawList::Render
//

void FBspStencilDrawList::Render(FLevelSceneNode* SceneNode,FRenderInterface* RI)
{
	guard(FBspStencilDrawList::Render);

	FUNC_CLOCK(StencilCyc,GStats.DWORDStats(GEngineStats.STATS_Stencil_RenderCycles));

	if(NumTriangles > 0)
	{
		RI->PushState();

		// Setup blending.

		static FSolidColorTexture	StencilTexture(FColor(128,128,128));

		DECLARE_STATIC_UOBJECT(
			UProxyBitmapMaterial,
			StencilTextureMaterial,
			{
				StencilTextureMaterial->SetTextureInterface(&StencilTexture);
			}
			);

		DECLARE_STATIC_UOBJECT(
			UFinalBlend,
			StencilFinalBlend,
			{
				StencilFinalBlend->Material = StencilTextureMaterial;
				StencilFinalBlend->FrameBufferBlending = FB_Overwrite;
				StencilFinalBlend->ZWrite = 1;
				StencilFinalBlend->TwoSided = 1;
			}
			);

		RI->EnableLighting(0,0);
		RI->SetMaterial(StencilFinalBlend);

		if(SceneNode->StencilMask)
			RI->SetStencilOp(CF_NotEqual,SceneNode->StencilMask,SceneNode->StencilMask,SO_Keep,SO_Keep,SO_Replace,StencilMask);
		else
			RI->SetStencilOp(CF_Equal,~StencilMask & ~DEPTH_COMPLEXITY_MASK(SceneNode->Viewport),~StencilMask & ~DEPTH_COMPLEXITY_MASK(SceneNode->Viewport),SO_Keep,SO_Keep,SO_Replace,StencilMask);

		// Set the vertex stream.

		FVertexStream*	Streams[] = { &Model->Sections(SectionIndex).Vertices };
		RI->SetVertexStreams(VS_FixedFunction,Streams,1);

		// Build an index buffer containing only the nodes in the draw list.

		FBspIndexBuffer	IndexBuffer(this);

		INT	BaseIndex = RI->SetDynamicIndexBuffer(&IndexBuffer,0);

		// Draw the stencil polygons.

		RI->DrawPrimitive(PT_TriangleList,BaseIndex,NumTriangles,IndexBuffer.MinIndex,IndexBuffer.MaxIndex);

		//scion jg -- Restore previous stencil state
		RI->ResetStencilOp();

		RI->PopState();
	}

#ifndef FINAL_RELEASE
	GStats.DWORDStats(GEngineStats.STATS_Stencil_Nodes) += NumNodes;
	GStats.DWORDStats(GEngineStats.STATS_Stencil_Triangles) += NumTriangles;
#endif

	unguard;
}
