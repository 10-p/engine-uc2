/*=============================================================================
	UnRenderStaticMesh.cpp: Static mesh rendering code.
	Copyright 2002 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/

#include "EnginePrivate.h"
#include "UnRenderPrivate.h"

//
// FSortedIndexBuffer
//

#if !defined(_MSC_VER) && (!defined(__i386__) || defined(UT_NO_X86_INTRIN))
// Not 32-bit x86: the C equivalent of the x87 sequence below (round to nearest in the current mode).
inline void FloatToInt(int *int_pointer, float f)
{
	*int_pointer = (int)lrintf(f);
}
#elif ASMLINUX
inline void FloatToInt(int *int_pointer, float f)
{
    __asm__ __volatile
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


class FSortedIndexBuffer : public FIndexBuffer
{
public:

#if defined(_XBOX)
	// MSM: It makes more sense to integerize the sort keys rather
	// than the delta between the sort keys.
	struct FTriangleSortInfo
	{
        INT     SortKey;
		INT		Indices[3];

		friend INT Compare(FTriangleSortInfo& A,FTriangleSortInfo& B)
		{
			return (B.SortKey - A.SortKey);
		}
	};
#else
	struct FTriangleSortInfo
	{
        FLOAT   SortKey;
		INT		Indices[3];

		friend INT Compare(FTriangleSortInfo& A,FTriangleSortInfo& B)
		{
			return (INT)(B.SortKey - A.SortKey);
		}
	};
#endif

	UStaticMesh*	StaticMesh;
	INT				SectionIndex;
	FVector			ViewOrigin;
    FPlane          ViewPlane;
	_WORD			MinVertexIndex;
	UBOOL			ReverseVertexOrder;


	// Constructor.

	FSortedIndexBuffer(UStaticMesh* InStaticMesh,INT InSectionIndex,FVector InViewOrigin,_WORD InMinVertexIndex,UBOOL InReverseVertexOrder = 0)
	{
		StaticMesh = InStaticMesh;
		SectionIndex = InSectionIndex;
		ViewOrigin = InViewOrigin;
		MinVertexIndex = InMinVertexIndex;
		ReverseVertexOrder = InReverseVertexOrder;
	}

	// FRenderResource interface.

	virtual INT GetRevision() { return 1; }

	// FIndexBuffer interface.

	virtual INT GetSize()
	{
		return StaticMesh->Sections(SectionIndex).NumTriangles * 3 * sizeof(_WORD);
	}
	
	virtual INT GetIndexSize()
	{
		return sizeof(_WORD);
	}

	virtual void GetContents(void* VoidDest)
	{
		guard(FSortedIndexBuffer::GetContents);

		_WORD* Dest = (_WORD*)VoidDest;

		FMemMark					MemMark(GSceneMem);
		FStaticMeshSection*			Section = &StaticMesh->Sections(SectionIndex);
		_WORD*						Indices = &StaticMesh->IndexBuffer.Indices(Section->FirstIndex);
		FStaticMeshVertex*			Vertices = &StaticMesh->VertexStream.Vertices(0);
		FTriangleSortInfo*			SortedTriangles = New<FTriangleSortInfo>(GSceneMem,Section->NumTriangles);

		for(INT TriangleIndex = 0;TriangleIndex < Section->NumTriangles;TriangleIndex++)
		{
#if defined(_XBOX)
			SortedTriangles[TriangleIndex].Indices[0] = Indices[TriangleIndex * 3 + 0];
			SortedTriangles[TriangleIndex].Indices[1] = Indices[TriangleIndex * 3 + 1];
			SortedTriangles[TriangleIndex].Indices[2] = Indices[TriangleIndex * 3 + 2];
			
			float f0 = (Vertices[Indices[TriangleIndex * 3 + 0]].Position - ViewOrigin).SizeSquared();
			float f1 = (Vertices[Indices[TriangleIndex * 3 + 1]].Position - ViewOrigin).SizeSquared();
			float f2 = (Vertices[Indices[TriangleIndex * 3 + 2]].Position - ViewOrigin).SizeSquared();
			int key;
			
			__asm
			{
				movss xmm0,[f0]
				maxss xmm0,[f1]
				maxss xmm0,[f2]
				cvttss2si eax,xmm0
				mov [key],eax
			}

			SortedTriangles[TriangleIndex].SortKey = key;
#elif 1 // sjs
            SortedTriangles[TriangleIndex].Indices[0] = Indices[TriangleIndex * 3 + 0];
			SortedTriangles[TriangleIndex].Indices[1] = Indices[TriangleIndex * 3 + 1];
			SortedTriangles[TriangleIndex].Indices[2] = Indices[TriangleIndex * 3 + 2];

            int d0,d1,d2;
            FloatToInt( &d0, (Vertices[Indices[TriangleIndex * 3 + 0]].Position - ViewOrigin).SizeSquared());
            FloatToInt( &d1, (Vertices[Indices[TriangleIndex * 3 + 1]].Position - ViewOrigin).SizeSquared());
            FloatToInt( &d2, (Vertices[Indices[TriangleIndex * 3 + 2]].Position - ViewOrigin).SizeSquared());
            SortedTriangles[TriangleIndex].SortKey = Max(d0,Max(d1,d2));
#endif
		}

		Sort(SortedTriangles,Section->NumTriangles);

		for(INT TriangleIndex = 0;TriangleIndex < Section->NumTriangles;TriangleIndex++)
		{
			if(ReverseVertexOrder)
			{
				*Dest++ = SortedTriangles[TriangleIndex].Indices[2] - Section->MinVertexIndex + MinVertexIndex;
				*Dest++ = SortedTriangles[TriangleIndex].Indices[1] - Section->MinVertexIndex + MinVertexIndex;
				*Dest++ = SortedTriangles[TriangleIndex].Indices[0] - Section->MinVertexIndex + MinVertexIndex;
			}
			else
			{
				*Dest++ = SortedTriangles[TriangleIndex].Indices[0] - Section->MinVertexIndex + MinVertexIndex;
				*Dest++ = SortedTriangles[TriangleIndex].Indices[1] - Section->MinVertexIndex + MinVertexIndex;
				*Dest++ = SortedTriangles[TriangleIndex].Indices[2] - Section->MinVertexIndex + MinVertexIndex;
			}
		}

		MemMark.Pop();

		unguard;
	}
};

//
//	CalculateStaticMeshLighting
//
// scion sz - not used on xbox
#ifndef _XBOX
void CalculateStaticMeshLighting(UStaticMesh* StaticMesh,UStaticMeshInstance* StaticMeshInstance,FDynamicActor* Owner)
{
	guard(CalculateStaticMeshLighting);

	// Update the static lighting stream.

	appMemzero(&StaticMeshInstance->ColorStream.Colors(0),StaticMeshInstance->ColorStream.Colors.Num() * sizeof(FColor));
	StaticMeshInstance->ColorStream.Revision++;

	for(INT LightIndex = 0;LightIndex < StaticMeshInstance->Lights.Num();LightIndex++)
	{
		FStaticMeshLightInfo*	LightInfo = &StaticMeshInstance->Lights(LightIndex);

		LightInfo->Applied = 0;

		if(LightInfo->LightActor)
		{
			FDynamicLight*	Light = LightInfo->LightActor->GetLightRenderData();
    
		    if(Light && !Light->Changed && !Light->Dynamic)
		    {
			    FStaticMeshVertex*	VertexPtr = &StaticMesh->VertexStream.Vertices(0);
			    FColor*				ColorPtr = &StaticMeshInstance->ColorStream.Colors(0);
			    BYTE*				BitPtr = LightInfo->VisibilityBits.Num() ? &LightInfo->VisibilityBits(0) : NULL;
			    BYTE				BitMask = 1;
    
			    for(INT VertexIndex = 0;VertexIndex < StaticMesh->VertexStream.Vertices.Num();VertexIndex++)
			    {
				    FVector	SamplePoint = Owner->LocalToWorld.TransformFVector(VertexPtr->Position),
						    SampleNormal = Owner->LocalToWorld.TransformNormal(VertexPtr->Normal).SafeNormal();
    
				    if(*BitPtr & BitMask)
					{
					    *ColorPtr += FColor(0.5f * Light->Color * Light->SampleIntensity(SamplePoint, SampleNormal));
					}
    
				    ColorPtr++;
				    VertexPtr++;
				    BitMask <<= 1;
    
				    if(!BitMask)
				    {
					    BitPtr++;
					    BitMask = 1;
				    }
			    }
    
			    LightInfo->Applied = 1;
		    }
		}
	}

	// Blend the vertex color/alpha into the color stream.

	FColor*	SrcPtr = &StaticMesh->ColorStream.Colors(0);
	FColor*	DestPtr = &StaticMeshInstance->ColorStream.Colors(0);

	for(INT VertexIndex = 0;VertexIndex < StaticMesh->VertexStream.Vertices.Num();VertexIndex++)
	{
		if(StaticMesh->UseVertexColor)
		{
			DestPtr->R = (BYTE) ((INT)SrcPtr->R * (INT)DestPtr->R / (INT)255);
			DestPtr->G = (BYTE) ((INT)SrcPtr->G * (INT)DestPtr->G / (INT)255);
			DestPtr->B = (BYTE) ((INT)SrcPtr->B * (INT)DestPtr->B / (INT)255);
		}

		DestPtr->A = SrcPtr->A;

		SrcPtr++;
		DestPtr++;
	}

	unguard;
}
#endif

//
//	DrawSection
//

void DrawSection(UStaticMesh* StaticMesh,INT SectionIndex,UMaterial* Material,FRenderInterface* RI)
{
	FStaticMeshSection*	Section = &StaticMesh->Sections(SectionIndex);

	INT NumPasses = 0;

	if(Section->NumPrimitives > 0)
	{
		NumPasses = RI->SetMaterial(Material);

		// Render the section.
		BEGIN_D3D_EVENT(BDE)(PIX_STATIC_MESH,TEXT("%s Sect(%d)"),
			StaticMesh->GetName(),SectionIndex);
#ifdef _XBOX
		//scion jg -- Since sorting is gone, we discard the indices. We must
		// use the precompiled pushbuffer
		RI->RunPushBuffer(Section->pPushBuffer);
		STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_PushBuffer_RunPushBufferSMTris),
			Section->NumTriangles * NumPasses);
#else
		RI->DrawPrimitive(
			Section->IsStrip ? PT_TriangleStrip : PT_TriangleList,
			Section->FirstIndex,
			Section->NumPrimitives,
			Section->MinVertexIndex,
			Section->MaxVertexIndex
			);
#endif
		END_D3D_EVENT;
	}

	STAT_INC(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_UnbatchedUnsortedSections));
	STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_UnbatchedUnsortedTriangles),
		Section->NumTriangles * NumPasses);
	STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_Triangles),
		Section->NumTriangles * NumPasses);
}

//
//	SortAlphaBlended
//
// [Engine.GameEngine] SortAlphaBlended, read once at the first translucent draw: alpha-blended static mesh sections draw
// their separate objects back to front (DrawSortedSection) and alpha-blended decoration layers draw back to front
// (ATerrainInfo::RenderDecorations). Compiled default True, False under _XBOX, whose draw paths have no such sort: the
// setting does nothing there.
//
UBOOL SortAlphaBlended()
{
	static INT	Value = -1;
	if(Value < 0)
	{
#ifdef _XBOX
		UBOOL	Setting = 0;
#else
		UBOOL	Setting = 1;
#endif
		GConfig->GetBool(TEXT("Engine.GameEngine"),TEXT("SortAlphaBlended"),Setting,GIni);
		Value = Setting ? 1 : 0;
	}
	return Value;
}

#ifdef _XBOX

#define D3DCULLMODE 147

/* scion ======================================================================
 * DrawSortedSection
 * Author: jg
 * 
 * Draws a translucent static mesh using a 2 pass technique. It draws all back
 * faces first and then draws all front faces. This allows us to discard the
 * indices altogether and use our precompiled push buffers. NOTE: This makes
 * all translucent static meshes be 2 sided.
 *
 * input:	SceneNode -- The scene node being rendered
 *			Owner -- The owning static mesh actor
 *			StaticMesh -- The static mesh being drawn
 *			SectionIndex -- The section of the static mesh being drawn
 *			Material -- The translucent material to use
 *
 * ============================================================================
 */
void DrawSortedSection(FLevelSceneNode* SceneNode,FDynamicActor* Owner,
	UStaticMesh* StaticMesh,INT SectionIndex,UMaterial* Material,
	FRenderInterface* RI)
{
	FUNC_CLOCK(SMRenderCycles,GStats.DWORDStats(GEngineStats.STATS_StaticMesh_UnbatchedRenderCycles));
	FStaticMeshSection*	Section = &StaticMesh->Sections(SectionIndex);
	// Only attempt to draw if the pushbuffer is valid
	if (Section->NumPrimitives > 0 && Section->pPushBuffer != NULL)
	{
		// Commit the material
		INT NumPasses = RI->SetMaterial(Material);
		// only draw using two passes if static mesh is using
		// a two-sided material 
		if( RI->GetCullMode() == CM_None )
		{
			// Draw back faces first
			RI->SetCullMode(SceneNode->Determinant < 0.0f ? CM_CW : CM_CCW);
			// Draw using the push buffer
			RI->RunPushBuffer(Section->pPushBuffer);
			// Draw front faces now
			RI->SetRenderStateDirect(D3DCULLMODE,
				SceneNode->Determinant < 0.0f ? CM_CCW : CM_CW);
			// Draw again without committing the render states this time. If
			// this is a multi-pass material then we need to commit states
			RI->RunPushBuffer(Section->pPushBuffer,0,NumPasses > 1);
		}
		else
		{
			// Draw section only once
			RI->RunPushBuffer(Section->pPushBuffer);
		}		

		// Update stats
		STAT_INC(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_UnbatchedSortedSections));
		STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_UnbatchedSortedTriangles),
			Section->NumTriangles * NumPasses);
		STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_Triangles),
			Section->NumTriangles * NumPasses);
		STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_PushBuffer_RunPushBufferSMTris),
			Section->NumTriangles * NumPasses);
	}
}
#else
//
//	IsAlphaBlended
//
// Whether a material blends over what is behind it by its alpha (FB_AlphaBlend, its premultiplied form, a Shader's
// OB_Normal with an opacity, an alpha texture): the blends whose result depends on the draw order. Additive,
// modulating, brightening and darkening blends do not, nor do masked or opaque materials. As in the render device's
// CheckMaterial, the innermost FinalBlend of the modifier chain sets the framebuffer blending.
//
UBOOL IsAlphaBlended(UMaterial* Material)
{
	UFinalBlend* FinalBlend = NULL;
	for( INT Depth = 0; Depth < 32 && Material && Material->IsA(UModifier::StaticClass()); Depth++ )
	{
		if( Material->IsA(UFinalBlend::StaticClass()) )
			FinalBlend = (UFinalBlend*)Material;
		Material = ((UModifier*)Material)->Material;
	}
	if( FinalBlend )
		return FinalBlend->FrameBufferBlending == FB_AlphaBlend || FinalBlend->FrameBufferBlending == FB_AlphaModulate_MightNotFogCorrectly;
	if( UShader* Shader = Cast<UShader>(Material) )
		return Shader->OutputBlending == OB_Normal && Shader->Opacity;
	if( UTexture* Texture = Cast<UTexture>(Material) )
		return Texture->bAlphaTexture && !Texture->bMasked;
	return 0;
}

//
//	FSectionObjects
//
// The separate objects of a static mesh section, which the section's translucent draw orders back to front as the
// translucent list orders actors. An object is the triangles joined by shared vertices together with every such part
// whose bounding box meets theirs: parts that touch or overlap (a canopy's leaf cards, a decal over its model) are one
// object, which keeps its triangles in the section's order, the order its artist layered them in. They do not depend
// on the view, so they are worked out once per section and kept while the level is.
//
struct FSectionObjects
{
	UStaticMesh*	StaticMesh;
	INT				SectionIndex;
	INT				FirstIndex,
					NumPrimitives,
					NumTriangles;
	TArray<INT>		Objects;	// each triangle's object, in the section's order
	TArray<FBox>	Boxes;		// each object's bounding box
};

struct FSectionPartBox
{
	FBox	Box;
	INT		Part;

	friend INT Compare(FSectionPartBox& A,FSectionPartBox& B)
	{
		return A.Box.Min.X < B.Box.Min.X ? -1 : (A.Box.Min.X > B.Box.Min.X ? 1 : 0);
	}
};

static INT FindObjectRoot(INT* Parent,INT Index)
{
	while(Parent[Index] != Index)
	{
		Parent[Index] = Parent[Parent[Index]];
		Index = Parent[Index];
	}
	return Index;
}

static void JoinObjects(INT* Parent,INT A,INT B)
{
	A = FindObjectRoot(Parent,A);
	B = FindObjectRoot(Parent,B);
	if(A != B)
		Parent[Max(A,B)] = Min(A,B);
}

//
// A stripped section unrolled into a list: its degenerate joining triangles dropped, its odd triangles turned back to
// the strip's winding. Returns the triangle count; Triangles (three indices each) lives in GSceneMem.
//
static INT UnrollSectionTriangles(UStaticMesh* StaticMesh,FStaticMeshSection* Section,_WORD*& Triangles)
{
	_WORD*	Indices = &StaticMesh->IndexBuffer.Indices(Section->FirstIndex);
	INT		NumTriangles = 0;

	Triangles = New<_WORD>(GSceneMem,Section->NumPrimitives * 3);
	for(INT PrimitiveIndex = 0;PrimitiveIndex < Section->NumPrimitives;PrimitiveIndex++)
	{
		_WORD	A, B, C;
		if(Section->IsStrip)
		{
			A = Indices[PrimitiveIndex];
			B = Indices[PrimitiveIndex + 1];
			C = Indices[PrimitiveIndex + 2];
			if(A == B || B == C || A == C)
				continue;
			if(PrimitiveIndex & 1)
				Exchange(A,B);
		}
		else
		{
			A = Indices[PrimitiveIndex * 3 + 0];
			B = Indices[PrimitiveIndex * 3 + 1];
			C = Indices[PrimitiveIndex * 3 + 2];
		}
		Triangles[NumTriangles * 3 + 0] = A;
		Triangles[NumTriangles * 3 + 1] = B;
		Triangles[NumTriangles * 3 + 2] = C;
		NumTriangles++;
	}
	return NumTriangles;
}

static TArray<FSectionObjects*>	GSectionObjects;
static ULevel*					GSectionObjectsLevel = NULL;

static FSectionObjects* GetSectionObjects(ULevel* Level,UStaticMesh* StaticMesh,INT SectionIndex,_WORD* Triangles,INT NumTriangles)
{
	FStaticMeshSection*	Section = &StaticMesh->Sections(SectionIndex);

	if(Level != GSectionObjectsLevel)
	{
		for(INT EntryIndex = 0;EntryIndex < GSectionObjects.Num();EntryIndex++)
			delete GSectionObjects(EntryIndex);
		GSectionObjects.Empty();
		GSectionObjectsLevel = Level;
	}

	FSectionObjects*	Entry = NULL;
	for(INT EntryIndex = 0;EntryIndex < GSectionObjects.Num();EntryIndex++)
	{
		FSectionObjects*	Other = GSectionObjects(EntryIndex);
		if(Other->StaticMesh == StaticMesh && Other->SectionIndex == SectionIndex)
		{
			if(Other->FirstIndex == Section->FirstIndex && Other->NumPrimitives == Section->NumPrimitives && Other->NumTriangles == NumTriangles)
				return Other;
			Entry = Other;
			break;
		}
	}
	if(!Entry)
	{
		Entry = new FSectionObjects;
		GSectionObjects.AddItem(Entry);
	}
	Entry->StaticMesh = StaticMesh;
	Entry->SectionIndex = SectionIndex;
	Entry->FirstIndex = Section->FirstIndex;
	Entry->NumPrimitives = Section->NumPrimitives;
	Entry->NumTriangles = NumTriangles;
	Entry->Objects.Empty(NumTriangles);
	Entry->Objects.Add(NumTriangles);
	Entry->Boxes.Empty();

	FStaticMeshVertex*	Vertices = &StaticMesh->VertexStream.Vertices(0);
	INT					MinVertex = Section->MinVertexIndex,
						NumVertices = Section->MaxVertexIndex - Section->MinVertexIndex + 1;
	FMemMark			MemMark(GSceneMem);

	// The parts: vertices joined by the triangles, and each part's bounding box.

	INT*	Parent = New<INT>(GSceneMem,NumVertices);
	for(INT VertexIndex = 0;VertexIndex < NumVertices;VertexIndex++)
		Parent[VertexIndex] = VertexIndex;
	for(INT TriangleIndex = 0;TriangleIndex < NumTriangles;TriangleIndex++)
	{
		JoinObjects(Parent,Triangles[TriangleIndex * 3 + 0] - MinVertex,Triangles[TriangleIndex * 3 + 1] - MinVertex);
		JoinObjects(Parent,Triangles[TriangleIndex * 3 + 1] - MinVertex,Triangles[TriangleIndex * 3 + 2] - MinVertex);
	}

	FBox*	Boxes = New<FBox>(GSceneMem,NumVertices);
	INT		NumParts = 0;
	for(INT VertexIndex = 0;VertexIndex < NumVertices;VertexIndex++)
		Boxes[VertexIndex] = FBox(0);
	for(INT TriangleIndex = 0;TriangleIndex < NumTriangles;TriangleIndex++)
	{
		FBox&	Box = Boxes[FindObjectRoot(Parent,Triangles[TriangleIndex * 3] - MinVertex)];
		if(!Box.IsValid())
			NumParts++;
		for(INT Corner = 0;Corner < 3;Corner++)
			Box += Vertices[Triangles[TriangleIndex * 3 + Corner]].Position;
	}

	// The objects: parts whose boxes meet, found by a sweep along X.

	FSectionPartBox*	PartBoxes = New<FSectionPartBox>(GSceneMem,Max(NumParts,1));
	INT					NumPartBoxes = 0;
	for(INT VertexIndex = 0;VertexIndex < NumVertices;VertexIndex++)
		if(Boxes[VertexIndex].IsValid())
		{
			PartBoxes[NumPartBoxes].Box = Boxes[VertexIndex];
			PartBoxes[NumPartBoxes].Part = VertexIndex;
			NumPartBoxes++;
		}
	Sort(PartBoxes,NumPartBoxes);
	for(INT First = 0;First < NumPartBoxes;First++)
	{
		FBox&	A = PartBoxes[First].Box;
		for(INT Second = First + 1;Second < NumPartBoxes && PartBoxes[Second].Box.Min.X <= A.Max.X;Second++)
		{
			FBox&	B = PartBoxes[Second].Box;
			if(B.Min.Y <= A.Max.Y && B.Max.Y >= A.Min.Y && B.Min.Z <= A.Max.Z && B.Max.Z >= A.Min.Z)
				JoinObjects(Parent,PartBoxes[First].Part,PartBoxes[Second].Part);
		}
	}

	// Number the objects, each triangle's object and each object's box.

	INT*	Numbers = New<INT>(GSceneMem,NumVertices);
	for(INT VertexIndex = 0;VertexIndex < NumVertices;VertexIndex++)
		Numbers[VertexIndex] = INDEX_NONE;
	for(INT PartIndex = 0;PartIndex < NumPartBoxes;PartIndex++)
	{
		INT	Root = FindObjectRoot(Parent,PartBoxes[PartIndex].Part);
		if(Numbers[Root] == INDEX_NONE)
		{
			Numbers[Root] = Entry->Boxes.Num();
			Entry->Boxes.AddItem(FBox(0));
		}
		Entry->Boxes(Numbers[Root]) += PartBoxes[PartIndex].Box;
	}
	for(INT TriangleIndex = 0;TriangleIndex < NumTriangles;TriangleIndex++)
		Entry->Objects(TriangleIndex) = Numbers[FindObjectRoot(Parent,Triangles[TriangleIndex * 3] - MinVertex)];

	MemMark.Pop();
	return Entry;
}

//
//	FSortedSectionIndexBuffer
//
// A section's triangles (unrolled) with its objects back to front from a point in the mesh's space, by the distance
// of their box's centre, each object's triangles in the section's order.
//
class FSortedSectionIndexBuffer : public FIndexBuffer
{
public:

	struct FObjectKey
	{
		FLOAT	Key;
		INT		Object;

		friend INT Compare(FObjectKey& A,FObjectKey& B)
		{
			if(A.Key != B.Key)
				return A.Key < B.Key ? 1 : -1;
			return A.Object - B.Object;
		}
	};

	_WORD*	Triangles;
	INT*	Order;
	INT		NumTriangles;

	// Constructor: the order lives in GSceneMem.

	FSortedSectionIndexBuffer(FSectionObjects* Objects,_WORD* InTriangles,INT InNumTriangles,FVector ViewOrigin)
	{
		Triangles = InTriangles;
		NumTriangles = InNumTriangles;
		Order = New<INT>(GSceneMem,Max(NumTriangles,1));

		INT			NumObjects = Objects->Boxes.Num();
		FObjectKey*	Keys = New<FObjectKey>(GSceneMem,Max(NumObjects,1));
		INT*		Starts = NewZeroed<INT>(GSceneMem,NumObjects + 1);
		for(INT ObjectIndex = 0;ObjectIndex < NumObjects;ObjectIndex++)
		{
			Keys[ObjectIndex].Key = (Objects->Boxes(ObjectIndex).GetCenter() - ViewOrigin).SizeSquared();
			Keys[ObjectIndex].Object = ObjectIndex;
		}
		Sort(Keys,NumObjects);

		// The objects' places in the order, then their triangles counted into them.

		INT*	Ranks = New<INT>(GSceneMem,Max(NumObjects,1));
		for(INT Rank = 0;Rank < NumObjects;Rank++)
			Ranks[Keys[Rank].Object] = Rank;
		for(INT TriangleIndex = 0;TriangleIndex < NumTriangles;TriangleIndex++)
			Starts[Ranks[Objects->Objects(TriangleIndex)] + 1]++;
		for(INT Rank = 0;Rank < NumObjects;Rank++)
			Starts[Rank + 1] += Starts[Rank];
		for(INT TriangleIndex = 0;TriangleIndex < NumTriangles;TriangleIndex++)
			Order[Starts[Ranks[Objects->Objects(TriangleIndex)]]++] = TriangleIndex;
	}

	// FRenderResource interface.

	virtual INT GetRevision() { return 1; }

	// FIndexBuffer interface.

	virtual INT GetSize() { return NumTriangles * 3 * sizeof(_WORD); }
	virtual INT GetIndexSize() { return sizeof(_WORD); }

	virtual void GetContents(void* VoidDest)
	{
		_WORD* Dest = (_WORD*)VoidDest;
		for(INT Index = 0;Index < NumTriangles;Index++)
		{
			_WORD* Triangle = &Triangles[Order[Index] * 3];
			*Dest++ = Triangle[0];
			*Dest++ = Triangle[1];
			*Dest++ = Triangle[2];
		}
	}
};

//
//	DrawSortedSection
//

void DrawSortedSection(FLevelSceneNode* SceneNode,FDynamicActor* Owner,UStaticMesh* StaticMesh,INT SectionIndex,UMaterial* Material,FRenderInterface* RI)
{
	FStaticMeshSection*	Section = &StaticMesh->Sections(SectionIndex);

	// An alpha-blended section does not write depth: with SortAlphaBlended its separate objects are drawn back to front
	// from the eye, so that a nearer canopy covers a farther one of the same mesh.
	if(Section->NumPrimitives > 0 && SortAlphaBlended() && IsAlphaBlended(Material))
	{
		INT					NumPasses = RI->SetMaterial(Material);
		FMemMark			MemMark(GSceneMem);
		_WORD*				Triangles;
		INT					NumTriangles = UnrollSectionTriangles(StaticMesh,Section,Triangles);
		FSectionObjects*	Objects = GetSectionObjects(SceneNode->Level,StaticMesh,SectionIndex,Triangles,NumTriangles);

		if(NumTriangles > 0)
		{
			FSortedSectionIndexBuffer	IndexBuffer(Objects,Triangles,NumTriangles,Owner->LocalToWorld.Inverse().TransformFVector(SceneNode->ViewOrigin));
			INT							BaseIndex = RI->SetDynamicIndexBuffer(&IndexBuffer,0);
			RI->DrawPrimitive(PT_TriangleList,BaseIndex,NumTriangles,Section->MinVertexIndex,Section->MaxVertexIndex);
			RI->SetIndexBuffer(&StaticMesh->IndexBuffer,0);
		}
		MemMark.Pop();

		STAT_INC(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_UnbatchedSortedSections));
		STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_UnbatchedSortedTriangles),
			Section->NumTriangles * NumPasses);
		STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_Triangles),
			Section->NumTriangles * NumPasses);
		return;
	}

	if(Section->NumPrimitives > 0)
	{
		// Commit the material
		INT NumPasses = RI->SetMaterial(Material);
		// only draw using two passes if static mesh is using
		// a two-sided material 
		if( RI->GetCullMode() == CM_None )
		{
			// Draw back faces first
			RI->SetCullMode(SceneNode->Determinant < 0.0f ? CM_CW : CM_CCW);
			// Draw using the push buffer
			RI->DrawPrimitive(
				Section->IsStrip ? PT_TriangleStrip : PT_TriangleList,
				Section->FirstIndex,
				Section->NumPrimitives,
				Section->MinVertexIndex,
				Section->MaxVertexIndex
				);
			// Draw front faces now
			RI->SetCullMode(SceneNode->Determinant < 0.0f ? CM_CCW : CM_CW);
			// Draw again without committing the render states this time. If
			// this is a multi-pass material then we need to commit states
			RI->DrawPrimitive(
				Section->IsStrip ? PT_TriangleStrip : PT_TriangleList,
				Section->FirstIndex,
				Section->NumPrimitives,
				Section->MinVertexIndex,
				Section->MaxVertexIndex
				);
		}
		else
		{
			// Draw section only once
			RI->DrawPrimitive(
				Section->IsStrip ? PT_TriangleStrip : PT_TriangleList,
				Section->FirstIndex,
				Section->NumPrimitives,
				Section->MinVertexIndex,
				Section->MaxVertexIndex
				);
		}		
		STAT_INC(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_UnbatchedSortedSections));
		STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_UnbatchedSortedTriangles),
			Section->NumTriangles * NumPasses);
		STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_Triangles),
			Section->NumTriangles * NumPasses);
	}
}
#endif

//
//	RenderStaticMesh
//

void RenderStaticMesh(FDynamicActor* Owner,FLevelSceneNode* SceneNode,TList<FDynamicLight*>* Lights,TList<FProjectorRenderInfo*>* Projectors,FRenderInterface* RI)
{
	guard(RenderStaticMesh);

	FUNC_CLOCK(SMRenderCycles,GStats.DWORDStats(GEngineStats.STATS_StaticMesh_RenderCycles));
	FUNC_CLOCK(SMURenderCycles,GStats.DWORDStats(GEngineStats.STATS_StaticMesh_UnbatchedRenderCycles));

	AActor*					Actor = Owner->Actor;
	UStaticMesh*			StaticMesh = Actor->StaticMesh;
	UStaticMeshInstance*	StaticMeshInstance = NULL;

	if(!StaticMesh->VertexStream.Vertices.Num())
		return;

#ifndef FINAL_RELEASE
	if(Actor->bSelected && (SceneNode->Viewport->Actor->ShowFlags & SHOW_SelectionHighlight))
	{
		// Highlight the static mesh.

		RI->EnableLighting(1,1,0,NULL,SceneNode->Viewport->Actor->RendMap == REN_LightingOnly,Owner->BoundingSphere);
		RI->SetAmbientLight(FColor(102,255,102));
	}
	else
	if(Actor->GetAmbientLightingActor()->bUnlit || !SceneNode->Viewport->IsLit())
#else
	if(Actor->GetAmbientLightingActor()->bUnlit)
#endif
	{
		// Disable lighting for this staticmesh.

		RI->EnableLighting(1,1,0,NULL,SceneNode->Viewport->Actor->RendMap == REN_LightingOnly,Owner->BoundingSphere);
		RI->SetAmbientLight(FColor(255,255,255));
	}
	else if(!SceneNode->Viewport->IsWire())
	{
#ifdef _XBOX
		//scion jg -- Don't check for lighting mismatches on the xbox
 		StaticMeshInstance = Actor->StaticMeshInstance;
#else
		if(Actor->StaticMeshInstance)
		{
			// Verify the actor's instance lighting matches the static mesh.

			UBOOL	Matches = 1;

			if(Actor->StaticMeshInstance->ColorStream.Colors.Num() != StaticMesh->VertexStream.Vertices.Num())
			{
				Matches = 0;
			}

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
#endif

// scion sz - shouldn't ever update the static mesh color streams dynamically on xbox
#ifndef _XBOX
		if(StaticMeshInstance)
		{
			// Determine whether the static lighting stream needs to be updated.

			UBOOL	UpdateStaticLighting = 0;

			for(INT LightIndex = 0;LightIndex < StaticMeshInstance->Lights.Num();LightIndex++)
			{
				FStaticMeshLightInfo*	LightInfo = &StaticMeshInstance->Lights(LightIndex);

                if( !LightInfo->LightActor )
                {
                    StaticMeshInstance->Lights.Remove(LightIndex);
                    LightIndex = Min(0,LightIndex-1);
                    continue;
                }

				FDynamicLight*			Light = LightInfo->LightActor->GetLightRenderData();

				if((!Light || Light->Dynamic || Light->Changed) == LightInfo->Applied)
				{
					UpdateStaticLighting = 1;
					break;
				}
			}

			// Update the static lighting.

			if(UpdateStaticLighting)
				CalculateStaticMeshLighting(StaticMesh,StaticMeshInstance,Owner);
		}
#endif

		// Set the hardware lights for the actor.
        FSphere lightSphere = Owner->BoundingSphere;
        if( Owner->Actor->bSpecialLit && !Owner->Actor->bStaticLighting )
            lightSphere.W = -1.0f;

		// scion sz - only enable static lighting for static mesh instances
		UBOOL bUseStaticLighting = (GIsEditor) ? 1 : StaticMeshInstance!=NULL;

		if(Owner->Actor->StaticSectionBatches.Num() != StaticMesh->Sections.Num())
		{			
			RI->EnableLighting(1, bUseStaticLighting, 1,NULL,SceneNode->Viewport->Actor->RendMap == REN_LightingOnly,lightSphere);
			RI->SetAmbientLight(FColor(Owner->AmbientColor.operator FPlane() * 0.5f));
		}
		else
		{
			UBOOL	UseHardwareLighting = 0;

			for(TList<FDynamicLight*>* LightList = Lights;LightList;LightList = LightList->Next)
				if(!StaticMeshInstance || LightList->Element->Dynamic || LightList->Element->Changed)
				{
					UseHardwareLighting = 1;
					break;
				}

			RI->EnableLighting(UseHardwareLighting, bUseStaticLighting, 1,NULL,SceneNode->Viewport->Actor->RendMap == REN_LightingOnly,lightSphere);
			RI->SetAmbientLight(FColor(0,0,0));
		}

		INT	NumHardwareLights = 0;

		for(TList<FDynamicLight*>* LightList = Lights;LightList;LightList = LightList->Next)
		{
			if(!StaticMeshInstance || LightList->Element->Dynamic || LightList->Element->Changed)
				RI->SetLight(NumHardwareLights++,LightList->Element);
		}
	}

#ifndef _XBOX
	if(SceneNode->Viewport->IsWire() ||
		//scion jg -- Draw slow collision meshes in wire frame
		(SceneNode->Viewport->GetOuterUClient()->Engine->bShowSlowCollisionMeshes &&
		Actor->bCollideActors &&
		(Actor->bBlockNonZeroExtentTraces || Actor->bBlockActors) && StaticMesh &&
		!StaticMesh->CollisionModel))
	{
	    // Set the local to world transform.
    
	    RI->SetTransform(TT_LocalToWorld,Owner->LocalToWorld);

		// Determine the wireframe color.

		UEngine*	Engine = SceneNode->Viewport->GetOuterUClient()->Engine;
		FColor		WireColor = Actor->IsA(AMover::StaticClass()) ? Engine->C_Mover : Engine->C_StaticMesh;

		RI->EnableLighting(1,0,0,NULL,0,Owner->BoundingSphere);

		if(Actor->bSelected && (SceneNode->Viewport->Actor->ShowFlags & SHOW_SelectionHighlight))
			RI->SetAmbientLight(WireColor);
		else
			RI->SetAmbientLight(FColor(WireColor.Plane() * 0.5f));

		static FSolidColorTexture	WhiteTexture(FColor(255,255,255));

		DECLARE_STATIC_UOBJECT( UProxyBitmapMaterial, HACKGAH, { HACKGAH->SetTextureInterface(&WhiteTexture); } );
		DECLARE_STATIC_UOBJECT( UFinalBlend, LineMaterial, { LineMaterial->Material = HACKGAH; } );

		RI->SetMaterial(LineMaterial);

		// Set the vertex stream and index buffer.

		FVertexStream*	VertexStreams[1] = { &StaticMesh->VertexStream };

		RI->SetVertexStreams(VS_FixedFunction,VertexStreams,1);
		RI->SetIndexBuffer(&StaticMesh->WireframeIndexBuffer,0);

		// Draw the wireframe static mesh.

		RI->DrawPrimitive(
			PT_LineList,
			0,
			StaticMesh->WireframeIndexBuffer.Indices.Num() / 2,
			0,
			StaticMesh->VertexStream.Vertices.Num() - 1
			);

#ifndef FINAL_RELEASE
		for(INT SectionIndex = 0;SectionIndex < StaticMesh->Sections.Num();SectionIndex++)
			GStats.DWORDStats(GEngineStats.STATS_StaticMesh_Triangles) += StaticMesh->Sections(SectionIndex).NumTriangles;
#endif
	}
	else
#endif
	{

// scion sz - static mesh batching not used on xbox
#ifndef _XBOX

		// Set the vertex streams and index buffer.
		if(Owner->Actor->StaticSectionBatches.Num() == StaticMesh->Sections.Num())
		{
			RI->SetCullMode(CM_CW);

			for(INT Pass = 0;Pass < 2;Pass++)
			{
				for(INT SectionIndex = 0;SectionIndex < StaticMesh->Sections.Num();SectionIndex++)
				{
					FBatchReference&	Ref = Owner->Actor->StaticSectionBatches(SectionIndex);

					if(Ref.ElementIndex != INDEX_NONE)
					{
						FStaticMeshBatch&	Batch = SceneNode->Level->StaticMeshBatches(Ref.BatchIndex);

						UMaterial*	Material = StaticMesh->GetSkin(Owner->Actor,SectionIndex);
						UBOOL		RequiresSorting = Material->RequiresSorting() && (Owner->BoundingSphere - SceneNode->ViewOrigin).SizeSquared() < Square(8192.0f);

						RI->SetMaterial(Material);

//						DWORD ShaderType = RI->GetMaterialVertexShaderType(Material);

						FVertexStream*	VertexStreams[1] = { &Batch.Vertices };
						UBOOL			ReportDynamicUploads = !SceneNode->Viewport->Precaching && SceneNode->Viewport->GetOuterUClient()->ReportDynamicUploads;
						INT				UploadedSize = RI->SetVertexStreams(0,VertexStreams,1);

						if(UploadedSize && ReportDynamicUploads)
							debugf(TEXT("Uploading Batched StaticMesh: Actor=[%s] StaticMesh=[%s], Size=[%i]"), Owner->Actor->GetPathName(), Owner->Actor->StaticMesh->GetPathName(), UploadedSize );

						FStaticMeshBatch::FBatchElement&	Element = Batch.Elements(Ref.ElementIndex);

						if(Element.NumPrimitives > 0)
						{
							INT	BaseIndex;

							if(Pass == 0 && !RequiresSorting)
							{
								STAT_INC(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_UnbatchedUnsortedSections));
								STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_UnbatchedUnsortedTriangles),Element.NumPrimitives);
								STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_Triangles),Element.NumPrimitives);

								RI->SetIndexBuffer(&Batch.Indices,0);
								BaseIndex = Element.FirstIndex;
							}
							else if(Pass == 1 && RequiresSorting)
							{
								STAT_INC(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_UnbatchedSortedSections));
								STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_UnbatchedSortedTriangles),Element.NumPrimitives);
								STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_Triangles),Element.NumPrimitives);

								// Build a sorted index buffer.
								FMatrix WorldToLocal(Owner->LocalToWorld.Inverse());
								FSortedIndexBuffer	IndexBuffer(StaticMesh,SectionIndex,WorldToLocal.TransformFVector(SceneNode->ViewOrigin),Element.MinVertexIndex,Owner->Determinant < 0.0f);
								BaseIndex = RI->SetDynamicIndexBuffer(&IndexBuffer,0);
							}
							else
								continue;

							// Render the section.

							RI->DrawPrimitive(
								PT_TriangleList,
								BaseIndex,
								Element.NumPrimitives,
								Element.MinVertexIndex,
								Element.MaxVertexIndex
								);

							// Render the projectors hitting the section.

							AActor* ProjectorBase = Owner->Actor->GetProjectorBase();
#ifndef FINAL_RELEASE
							if(ProjectorBase->Projectors.Num() && !SceneNode->Viewport->IsWire() && (SceneNode->Viewport->Actor->ShowFlags & SHOW_Projectors))
#else
							if(ProjectorBase->Projectors.Num())
#endif
							{
								FUNC_CLOCK(ProjCyc,GStats.DWORDStats(GEngineStats.STATS_StaticMesh_ProjectorRenderCycles));

								RI->PushState();

								for(INT ProjectorIndex = 0;ProjectorIndex < ProjectorBase->Projectors.Num();ProjectorIndex++)
								{
									FProjectorRenderInfo*	ProjectorInfo = ProjectorBase->Projectors(ProjectorIndex);

									if(!ProjectorInfo->Render( Owner->Actor->Level->TimeSeconds ))
									{
										ProjectorBase->Projectors.Remove(ProjectorIndex--);
										continue;
									}

									// Setup blending.

									RI->EnableLighting(0,0);
									RI->SetMaterial(ProjectorInfo->GetMaterial(SceneNode,StaticMesh->GetSkin(Owner->Actor,SectionIndex)));

									// Render the projector.

									RI->DrawPrimitive(
										PT_TriangleList,
										BaseIndex,//Element.FirstIndex,
										Element.NumPrimitives,
										Element.MinVertexIndex,
										Element.MaxVertexIndex
										);

									STAT_INC(GStats.DWORDStats(GEngineStats.STATS_Projector_Projectors));
									STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_Projector_Triangles),Element.NumPrimitives);
								}

								RI->PopState();
							}

							// Render the dynamic projectors hitting the static mesh.

#ifndef FINAL_RELEASE
							if(!SceneNode->Viewport->IsWire() && (SceneNode->Viewport->Actor->ShowFlags & SHOW_Projectors))
#endif
							{
								FUNC_CLOCK(DynProj,GStats.DWORDStats( GEngineStats.STATS_StaticMesh_ProjectorRenderCycles ));

								RI->PushState();

								for(TList<FProjectorRenderInfo*>* ProjectorList = Projectors;ProjectorList;ProjectorList = ProjectorList->Next)
								{
									FProjectorRenderInfo*	ProjectorInfo = ProjectorList->Element;

									if(!ProjectorInfo->Render( Owner->Actor->Level->TimeSeconds ))
										continue;

									// Setup blending.

									RI->EnableLighting(0,0);
									RI->SetMaterial(ProjectorInfo->GetMaterial(SceneNode,StaticMesh->GetSkin(Owner->Actor,SectionIndex)));

									// Render the projector.

									RI->DrawPrimitive(
										PT_TriangleList,
										BaseIndex,//Element.FirstIndex,
										Element.NumPrimitives,
										Element.MinVertexIndex,
										Element.MaxVertexIndex
									);

									STAT_INC(GStats.DWORDStats(GEngineStats.STATS_Projector_Projectors));
									STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_Projector_Triangles),
										Element.NumPrimitives);
								}

								RI->PopState();
							}
						}
					}
				}
			}
		}
		else
#endif //!_XBOX

		{
			// Set the local to world transform.

			RI->SetTransform(TT_LocalToWorld,Owner->LocalToWorld);

			// scion sz - handle mirroring from negative scale values
			// if Det < 0 then we know that WorldMat._11*WorldMat._22*WorldMat_33 < 0
			RI->SetCullMode( (Owner->Determinant < 0.f) ? CM_CCW : CM_CW );

		    // Determine the required UV streams.
    
		    BYTE	RequiredUVStreams = 0;
    
		    for(INT MaterialIndex = 0;MaterialIndex < StaticMesh->Materials.Num();MaterialIndex++)
			    RequiredUVStreams |= StaticMesh->GetSkin(Owner->Actor,MaterialIndex)->RequiredUVStreams();
    
		    // Set the vertex streams.
    
		    FVertexStream*	VertexStreams[16];
		    INT				NumVertexStreams = 0;
    
		    VertexStreams[NumVertexStreams++] = &StaticMesh->VertexStream;
    
			// scion sz - FVF_Specular
		    if(StaticMesh->UseVertexColor)
			    VertexStreams[NumVertexStreams++] = &StaticMesh->ColorStream;
		    else
			    VertexStreams[NumVertexStreams++] = &StaticMesh->AlphaStream;
    
			// scion sz - FVF_Diffuse
		    if(StaticMeshInstance)
			    VertexStreams[NumVertexStreams++] = &StaticMeshInstance->ColorStream;
    
		    BYTE UVStreamMask = 0xff;
    
		    for(INT UVIndex = 0;UVIndex < StaticMesh->UVStreams.Num();UVIndex++,UVStreamMask <<= 1)
			    if(RequiredUVStreams & UVStreamMask)
				    VertexStreams[NumVertexStreams++] = &StaticMesh->UVStreams(UVIndex);

#ifdef _XBOX//jg -- Only do the compression rendering on the xbox
			// scion sz - enable blending with color/alpha streams
			QWORD ShaderType = VS_CompressedMesh | VS_StaticMeshVertexColor;
			if( StaticMeshInstance ){
				ShaderType |= VS_StaticMeshVertexColorDiffuse;
			}
			
			//jg -- Set the compression scale as a constant
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
#else
			QWORD ShaderType = VS_FixedFunction;
#endif

			UBOOL	ReportDynamicUploads	= !SceneNode->Viewport->Precaching && SceneNode->Viewport->GetOuterUClient()->ReportDynamicUploads;			
			INT		UploadedSize			= RI->SetVertexStreams(ShaderType,VertexStreams,NumVertexStreams);			
			if( UploadedSize && ReportDynamicUploads )
				debugf(TEXT("Uploading StaticMesh: Actor=[%s] StaticMesh=[%s], Size=[%i]"), Owner->Actor->GetPathName(), Owner->Actor->StaticMesh->GetPathName(), UploadedSize );

#ifndef _XBOX//jg -- No indices on the xbox
			RI->SetIndexBuffer(&StaticMesh->IndexBuffer,0);
#endif
    
		    // Draw the static mesh normally.
    
		    for(INT Pass = 0;Pass < 2;Pass++)
		    {
			    for(INT SectionIndex = 0;SectionIndex < StaticMesh->Sections.Num();SectionIndex++)
			    {
				    UMaterial*	Material = StaticMesh->GetSkin(Owner->Actor,SectionIndex);
					//scion jg -- Team coloring handling
					if (Material->RenderHint & MRH_TeamEvironShader)
					{
						UPSTeamShader* pShader = (UPSTeamShader*)Material;
						// Figure out which team it belongs to and color it
						pShader->Color = Owner->Actor->Region.Zone->TeamColor;
					}
				    UBOOL		RequiresSorting =	(Owner->Actor->AttachmentBone == NAME_None &&	// scion sz - do not sort attached actors (weapons,etc.)
													 Material->RequiresSorting() && 
													 (Owner->BoundingSphere - SceneNode->ViewOrigin).SizeSquared() < Square(8192.0f) );

    
				    if(Pass == 0 && !RequiresSorting)
					    DrawSection(StaticMesh,SectionIndex,Material,RI);
				    else if(Pass == 1 && RequiresSorting)
					    DrawSortedSection(SceneNode,Owner,StaticMesh,SectionIndex,Material,RI);
			    }
		    }

#if !DYNAMIC_PROJ_STREAM

			// scion sz -
			// Clip static mesh against projectors' frustums and batch the resulting
			// kDOP node leaves. These are to be rendered later.  
#ifndef FINAL_RELEASE
			if(!SceneNode->Viewport->IsWire() && (SceneNode->Viewport->Actor->ShowFlags & SHOW_Projectors))
#endif
			{
				FUNC_CLOCK(ProjCyc,GStats.DWORDStats(GEngineStats.STATS_StaticMesh_ProjectorRenderCycles));
                
				// dynamic projectors
				for(TList<FProjectorRenderInfo*>* ProjectorList = Projectors;ProjectorList;ProjectorList = ProjectorList->Next)
				{
					FProjectorRenderInfo*	ProjectorInfo = ProjectorList->Element;

					if(!ProjectorInfo->Render( Owner->Actor->Level->TimeSeconds ))
						continue;

					if( !ProjectorInfo->Projector->bProjectStaticMesh )
						continue;

					// Find an existing batch for the projector.
					FProjectorRenderBatch*	ProjectorBatch = NULL;
					for(TList<FProjectorRenderBatch*>* ProjectorBatchList = GProjectorBatchList;ProjectorBatchList;ProjectorBatchList = ProjectorBatchList->Next)
					{
						if( ProjectorBatchList->Element->MatchProjectorInfo( ProjectorInfo, NULL, 0, FProjectorRenderBatch::BATCH_DynamicStaticMesh ) )
						{
							ProjectorBatch = ProjectorBatchList->Element;
							break;
						}
					}

					// Create a new batch if necessary.
					if(!ProjectorBatch)
					{
						ProjectorBatch = new(GSceneMem) FProjectorRenderBatchStaticMesh( ProjectorInfo );
						GProjectorBatchList = new(GSceneMem) TList<FProjectorRenderBatch*>( ProjectorBatch, GProjectorBatchList );
					}

					// clip the static mesh prims and add them to the batch
					((FProjectorRenderBatchStaticMesh*)ProjectorBatch)->AddClippedStaticMeshPrims( Owner->Actor );
				}

				// base actor attached projectors
				AActor* ProjectorBase = Owner->Actor->GetProjectorBase();
				for(INT ProjectorIndex = 0;ProjectorIndex < ProjectorBase->Projectors.Num();ProjectorIndex++)
		        {
			        FProjectorRenderInfo*	ProjectorInfo = ProjectorBase->Projectors(ProjectorIndex);
        
					if(!ProjectorInfo->Render( Owner->Actor->Level->TimeSeconds ))
			        {
				        ProjectorBase->Projectors.Remove(ProjectorIndex--);
				        continue;
			        }

					// Find an existing batch for the projector.
					FProjectorRenderBatch*	ProjectorBatch = NULL;
					for(TList<FProjectorRenderBatch*>* ProjectorBatchList = GProjectorBatchList;ProjectorBatchList;ProjectorBatchList = ProjectorBatchList->Next)
					{
						if( ProjectorBatchList->Element->MatchProjectorInfo( ProjectorInfo, NULL, 0, FProjectorRenderBatch::BATCH_DynamicStaticMesh ) )
						{
							ProjectorBatch = ProjectorBatchList->Element;
							break;
						}
					}

					// Create a new batch if necessary.
					if(!ProjectorBatch)
					{
						ProjectorBatch = new(GSceneMem) FProjectorRenderBatchStaticMesh( ProjectorInfo );
						GProjectorBatchList = new(GSceneMem) TList<FProjectorRenderBatch*>( ProjectorBatch, GProjectorBatchList );
					}

					// clip the static mesh prims and add them to the batch
					((FProjectorRenderBatchStaticMesh*)ProjectorBatch)->AddClippedStaticMeshPrims( Owner->Actor );
				}

			}
	
#else //DYNAMIC_PROJ_STREAM
			// Render the projectors hitting the static mesh.
	        AActor* ProjectorBase = Owner->Actor->GetProjectorBase();
#ifndef FINAL_RELEASE
			if(ProjectorBase->Projectors.Num() && !SceneNode->Viewport->IsWire() && (SceneNode->Viewport->Actor->ShowFlags & SHOW_Projectors))
#else
	        if(ProjectorBase->Projectors.Num())
#endif
	        {
				FUNC_CLOCK(ProjCyc,GStats.DWORDStats(GEngineStats.STATS_StaticMesh_ProjectorRenderCycles));

				// Setup blending.
				RI->EnableLighting(0,0);
				RI->SetZBias(1);
        
#ifndef _XBOX//jg -- No indices on the xbox
				RI->SetIndexBuffer(&StaticMesh->IndexBuffer,0);
#endif
        
		        for(INT ProjectorIndex = 0;ProjectorIndex < ProjectorBase->Projectors.Num();ProjectorIndex++)
		        {
			        FProjectorRenderInfo*	ProjectorInfo = ProjectorBase->Projectors(ProjectorIndex);
        
					if(!ProjectorInfo->Render( Owner->Actor->Level->TimeSeconds ))
			        {
				        ProjectorBase->Projectors.Remove(ProjectorIndex--);
				        continue;
			        }
        
			        for(INT SectionIndex = 0;SectionIndex < StaticMesh->Sections.Num();SectionIndex++)
			        {
				        FStaticMeshSection*	Section = &StaticMesh->Sections(SectionIndex);
        
				        if( Section->NumPrimitives <= 0 )
					        continue;
        
				        // Render the projector.

#ifdef _XBOX
						// scion sz - we can only handle simple bitmaps with the UProjectorMultiMaterial
						UBitmapMaterial* ProjectedBitmap = ProjectorInfo->GetProjectedBitmap();
						if( ProjectedBitmap )
						{
							UShadowBitmapMaterial* pShadowBitmap = Cast<UShadowBitmapMaterial>( ProjectedBitmap );
							if( pShadowBitmap && 
								pShadowBitmap->Dirty )
							{
								// force update of shadow material so that its casting geometry will render
								pShadowBitmap->Get( SceneNode->Viewport->CurrentTime, SceneNode->Viewport );
								// reset local to world 
								RI->SetTransform(TT_LocalToWorld,Owner->LocalToWorld);
								// reset vertex stream
								RI->SetVertexStreams(ShaderType,VertexStreams,NumVertexStreams);
								// reset the compression scale constants
								RI->SetVertexShaderConstant(VSC_COMPRESSION_SCALE,CompressionValues,2);
							}

							FMultiMaterialProjectorInfo ProjMultMatInfo( StaticMesh->GetSkin(Owner->Actor,SectionIndex), 
								ProjectorInfo->Projector->bClipStaticMesh );
							ProjMultMatInfo.AddProjectorEntry( ProjectorInfo );
							UProjectorMultiMaterial* ProjMultMat = ProjMultMatInfo.GetMaterial( (EProjectorBlending)ProjectorInfo->FrameBufferBlendingOp );
							
							DrawSection( StaticMesh, SectionIndex, ProjMultMat, RI );
						}
						else
						{
							DrawSection( StaticMesh, SectionIndex, ProjectorInfo->GetMaterial(SceneNode,StaticMesh->GetSkin(Owner->Actor,SectionIndex)), RI );
						}
#else
						DrawSection( StaticMesh, SectionIndex, ProjectorInfo->GetMaterial(SceneNode,StaticMesh->GetSkin(Owner->Actor,SectionIndex)), RI );
#endif			        
        
				        STAT_INC(GStats.DWORDStats(GEngineStats.STATS_Projector_Projectors));
				        STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_Projector_Triangles),Section->NumPrimitives);
			        }
		        }

				RI->SetZBias(0);
	        }

			// Render the dynamic projectors hitting the static mesh.
#ifndef FINAL_RELEASE
			if(!SceneNode->Viewport->IsWire() && (SceneNode->Viewport->Actor->ShowFlags & SHOW_Projectors))
#endif
			{
				FUNC_CLOCK(ProjCyc,GStats.DWORDStats(GEngineStats.STATS_StaticMesh_ProjectorRenderCycles));

				// Setup blending.
				RI->EnableLighting(0,0);
				RI->SetZBias(1);

#ifndef _XBOX//jg -- No indices on the xbox
				RI->SetIndexBuffer(&StaticMesh->IndexBuffer,0);
#endif

				for(TList<FProjectorRenderInfo*>* ProjectorList = Projectors;ProjectorList;ProjectorList = ProjectorList->Next)
				{
					FProjectorRenderInfo*	ProjectorInfo = ProjectorList->Element;

					if(!ProjectorInfo->Render( Owner->Actor->Level->TimeSeconds ))
						continue;

					if( !ProjectorInfo->Projector->bProjectStaticMesh )
						continue;

					for(INT SectionIndex = 0;SectionIndex < StaticMesh->Sections.Num();SectionIndex++)
					{
						FStaticMeshSection*	Section = &StaticMesh->Sections(SectionIndex);

						if( Section->NumPrimitives <= 0 )
							continue;

						// Render the projector.

#ifdef _XBOX
						// scion sz - we can only handle simple bitmaps with the UProjectorMultiMaterial
						UBitmapMaterial* ProjectedBitmap = ProjectorInfo->GetProjectedBitmap();
						if( ProjectedBitmap )
						{
							UShadowBitmapMaterial* pShadowBitmap = Cast<UShadowBitmapMaterial>( ProjectedBitmap );
							if( pShadowBitmap && 
								pShadowBitmap->Dirty )
							{
								// force update of shadow material so that its casting geometry will render
								pShadowBitmap->Get( SceneNode->Viewport->CurrentTime, SceneNode->Viewport );
								// reset local to world 
								RI->SetTransform(TT_LocalToWorld,Owner->LocalToWorld);
								// reset vertex stream
								RI->SetVertexStreams(ShaderType,VertexStreams,NumVertexStreams);
								// reset the compression scale constants
								RI->SetVertexShaderConstant(VSC_COMPRESSION_SCALE,CompressionValues,2);
							}

							FMultiMaterialProjectorInfo ProjMultMatInfo( StaticMesh->GetSkin(Owner->Actor,SectionIndex), 
								ProjectorInfo->Projector->bClipStaticMesh);
							ProjMultMatInfo.AddProjectorEntry( ProjectorInfo );
							UProjectorMultiMaterial* ProjMultMat = ProjMultMatInfo.GetMaterial( (EProjectorBlending)ProjectorInfo->FrameBufferBlendingOp );
							DrawSection( StaticMesh, SectionIndex, ProjMultMat, RI );
						}
						else
						{
							DrawSection( StaticMesh, SectionIndex, ProjectorInfo->GetMaterial(SceneNode,StaticMesh->GetSkin(Owner->Actor,SectionIndex)), RI );
						}
#else
						DrawSection( StaticMesh, SectionIndex, ProjectorInfo->GetMaterial(SceneNode,StaticMesh->GetSkin(Owner->Actor,SectionIndex)), RI );
#endif

						STAT_INC(GStats.DWORDStats(GEngineStats.STATS_Projector_Projectors));
						STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_Projector_Triangles),Section->NumPrimitives);
					}
				}

				RI->SetZBias(0);
			}
#endif //DYNAMIC_PROJ_STREAM

		}
	}

	// Batch projectors on this static mesh.

#ifndef FINAL_RELEASE
	if(!SceneNode->Viewport->IsWire() && (SceneNode->Viewport->Actor->ShowFlags & SHOW_Projectors))
#endif
	{
		FUNC_CLOCK(ProjCyc,GStats.DWORDStats(GEngineStats.STATS_StaticMesh_ProjectorRenderCycles));

		for(INT ProjectorIndex = 0;ProjectorIndex < Actor->StaticMeshProjectors.Num();ProjectorIndex++)
		{
			FStaticProjectorInfo*	ProjectorInfo = Actor->StaticMeshProjectors(ProjectorIndex);

			if(!ProjectorInfo->RenderInfo->Render( Owner->Actor->Level->TimeSeconds ))
			{
				Actor->StaticMeshProjectors(ProjectorIndex) = ProjectorInfo->RemoveRef();
				Actor->StaticMeshProjectors.Remove(ProjectorIndex--);
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
	}

	// Draw the vertex normals.
#ifndef FINAL_RELEASE
	if(SceneNode->Viewport->bShowNormals)
	{
		FLineBatcher	LineBatcher(RI);

		for(INT VertexIndex = 0;VertexIndex < StaticMesh->VertexStream.Vertices.Num();VertexIndex++)
		{
			FStaticMeshVertex&	Vertex = StaticMesh->VertexStream.Vertices(VertexIndex);

			LineBatcher.DrawLine(Vertex.Position,Vertex.Position + Vertex.Normal * 8.0f,FColor(255,255,0));
		}
	}

#ifdef WITH_KARMA
	// Draw Karma collision primitives
	if(StaticMesh->KPhysicsProps && Actor->bBlockKarma)
	{
		McdModelID model = Actor->getKModel();
		if(model)
			KModelDraw(model, KGData->DebugDrawOpt, KLineDraw);
		else
			StaticMesh->KPhysicsProps->Draw(RI, SceneNode->Viewport->Actor->ShowFlags);
	}
#endif

	// If we have a simple collision model, and its being used, draw it.
	if(SceneNode->Viewport->Actor->ShowFlags & SHOW_Collision)
	{
		if (StaticMesh->CollisionModel && 
			(StaticMesh->UseSimpleBoxCollision || StaticMesh->UseSimpleLineCollision || StaticMesh->UseSimpleKarmaCollision) )
		{
			UEngine* Engine = SceneNode->Viewport->GetOuterUClient()->Engine;

			for(INT NodeIndex = 0;NodeIndex < StaticMesh->CollisionModel->Nodes.Num();NodeIndex++)
			{
				FBspNode&	Node = StaticMesh->CollisionModel->Nodes(NodeIndex);

				for(INT VertexIndex = 0;VertexIndex < Node.NumVertices;VertexIndex++)
				{
					FLineBatcher(RI).DrawLine(
						StaticMesh->CollisionModel->Points(StaticMesh->CollisionModel->Verts(Node.iVertPool + VertexIndex).pVertex),
						StaticMesh->CollisionModel->Points(StaticMesh->CollisionModel->Verts(Node.iVertPool + ((VertexIndex + 1) % Node.NumVertices)).pVertex),
						Engine->C_ScaleBoxHi
						);
				}
			}
		}
		//scion rwd - use separate sm as collision
		else if (StaticMesh->CollisionStaticMesh != NULL)
		{
			UStaticMesh *SM = StaticMesh->CollisionStaticMesh;
			// Set the local to world transform.
			RI->SetTransform(TT_LocalToWorld,Owner->LocalToWorld);
			UEngine*	Engine = SceneNode->Viewport->GetOuterUClient()->Engine;
			RI->EnableLighting(1,0,0,NULL,0,Owner->BoundingSphere);
			RI->SetAmbientLight(FColor(Engine->C_StaticMesh.Plane() * 0.5f));
			static FSolidColorTexture	WhiteTexture(FColor(255,255,255));

			DECLARE_STATIC_UOBJECT( UProxyBitmapMaterial, HACKGAH, { HACKGAH->SetTextureInterface(&WhiteTexture); } );
			DECLARE_STATIC_UOBJECT( UFinalBlend, LineMaterial, { LineMaterial->Material = HACKGAH; } );

			RI->SetMaterial(LineMaterial);

			// Set the vertex stream and index buffer.
			FVertexStream*	VertexStreams[1] = { &SM->VertexStream };
			RI->SetVertexStreams(VS_FixedFunction,VertexStreams,1);
			RI->SetIndexBuffer(&SM->WireframeIndexBuffer,0);

			// Draw the wireframe static mesh.
			RI->DrawPrimitive(
				PT_LineList,
				0,
				SM->WireframeIndexBuffer.Indices.Num() / 2,
				0,
				SM->VertexStream.Vertices.Num() - 1
				);
		}
	}
#endif
	unguard;
}

// scion sz - not used on xbox
#ifndef _XBOX

//
//	RenderBatchedStaticMesh
//
void RenderBatchedStaticMesh(FStaticMeshBatchList& BatchList,FDynamicActor* DynamicActor,FLevelSceneNode* SceneNode,FRenderInterface* RI)
{
	guard(RenderBatchedStaticMesh);

	AActor*			Actor = DynamicActor->Actor;
	UStaticMesh*	StaticMesh = Actor->StaticMesh;

	if( !StaticMesh ) 
		return;

	// Batch the static mesh's sections for rendering.

	if(Actor->StaticSectionBatches.Num() == StaticMesh->Sections.Num())
	{
		for(INT SectionIndex = 0;SectionIndex < StaticMesh->Sections.Num();SectionIndex++)
		{
			FBatchReference&	Ref = Actor->StaticSectionBatches(SectionIndex);

			if(Ref.ElementIndex != INDEX_NONE)
			{
				if(!BatchList.VisibleBatchElements[Ref.BatchIndex])
				{
					BatchList.VisibleBatchElements[Ref.BatchIndex] = New<INT>(GSceneMem,SceneNode->Level->StaticMeshBatches(Ref.BatchIndex).Elements.Num());
					BatchList.NumVisibleBatchElements[Ref.BatchIndex] = 0;
					BatchList.Batches[BatchList.NumBatches++] = Ref.BatchIndex;
				}

				BatchList.VisibleBatchElements[Ref.BatchIndex][BatchList.NumVisibleBatchElements[Ref.BatchIndex]++] = Ref.ElementIndex;
			}
		}
	}

	// Batch projectors on this static mesh.
#ifndef FINAL_RELEASE
	if(!SceneNode->Viewport->IsWire() && (SceneNode->Viewport->Actor->ShowFlags & SHOW_Projectors))
#endif
	{
		AActor* ProjectorBase = Actor->GetProjectorBase();

		for(INT ProjectorIndex = 0;ProjectorIndex < ProjectorBase->StaticMeshProjectors.Num();ProjectorIndex++)
		{
			FStaticProjectorInfo*	ProjectorInfo = ProjectorBase->StaticMeshProjectors(ProjectorIndex);

			if(!ProjectorInfo->RenderInfo->Render( DynamicActor->Actor->Level->TimeSeconds ))
			{
				ProjectorBase->StaticMeshProjectors(ProjectorIndex) = ProjectorInfo->RemoveRef();
				ProjectorBase->StaticMeshProjectors.Remove(ProjectorIndex--);
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
	}

	// Draw Karma collision primitives.
#ifndef FINAL_RELEASE
#ifdef WITH_KARMA
	if(StaticMesh->KPhysicsProps && Actor->bBlockKarma)
	{
		McdModelID model = Actor->getKModel();
		if(model)
			KModelDraw(model, KGData->DebugDrawOpt, KLineDraw);
		else
			StaticMesh->KPhysicsProps->Draw(RI, SceneNode->Viewport->Actor->ShowFlags);
	}
#endif

	// If we have a simple collision model, and its being used, draw it.

	if(SceneNode->Viewport->Actor->ShowFlags & SHOW_Collision &&
		StaticMesh->CollisionModel && 
		(StaticMesh->UseSimpleBoxCollision || StaticMesh->UseSimpleLineCollision || StaticMesh->UseSimpleKarmaCollision) )
	{
		UEngine* Engine = SceneNode->Viewport->GetOuterUClient()->Engine;

		RI->PushState();
		RI->SetTransform(TT_LocalToWorld,DynamicActor->LocalToWorld);

		for(INT NodeIndex = 0;NodeIndex < StaticMesh->CollisionModel->Nodes.Num();NodeIndex++)
		{
			FBspNode&	Node = StaticMesh->CollisionModel->Nodes(NodeIndex);

			for(INT VertexIndex = 0;VertexIndex < Node.NumVertices;VertexIndex++)
			{
				FLineBatcher(RI).DrawLine(
					StaticMesh->CollisionModel->Points(StaticMesh->CollisionModel->Verts(Node.iVertPool + VertexIndex).pVertex),
					StaticMesh->CollisionModel->Points(StaticMesh->CollisionModel->Verts(Node.iVertPool + ((VertexIndex + 1) % Node.NumVertices)).pVertex),
					Engine->C_ScaleBoxHi
					);
			}
		}

		RI->PopState();
	}
#endif
	unguard;
}

#endif //!_XBOX
