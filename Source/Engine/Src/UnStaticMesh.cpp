/*=============================================================================
	UnStaticMesh.cpp: Static mesh class implementation.
	Copyright 1997-2002 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/

#include "EnginePrivate.h"
#include "UnRenderPrivate.h"

IMPLEMENT_CLASS(UStaticMesh);
IMPLEMENT_CLASS(UStaticMeshInstance);
IMPLEMENT_CLASS(AStaticMeshActor);

//
//	UStaticMesh::UStaticMesh
//

UStaticMesh::UStaticMesh()
{
}

//
//	UStaticMesh::StaticConstructor
//

void UStaticMesh::StaticConstructor()
{
	guard(UStaticMesh::StaticConstructor);

//scion rwd - remove some unused props for xbox build
#ifndef _XBOX
	new(GetClass()->HideCategories) FName(NAME_Object);
#endif

	new(GetClass(),TEXT("UseSimpleLineCollision"),RF_Public)	UBoolProperty(CPP_PROPERTY(UseSimpleLineCollision),TEXT(""),CPF_Edit);
	new(GetClass(),TEXT("UseSimpleBoxCollision"),RF_Public)		UBoolProperty(CPP_PROPERTY(UseSimpleBoxCollision),TEXT(""),CPF_Edit);
	new(GetClass(),TEXT("UseSimpleKarmaCollision"),RF_Public)	UBoolProperty(CPP_PROPERTY(UseSimpleKarmaCollision),TEXT(""),CPF_Edit);
	new(GetClass(),TEXT("UseVertexColor"),RF_Public)			UBoolProperty(CPP_PROPERTY(UseVertexColor),TEXT(""),CPF_Edit);
	//jg -- Properies for enabling tristrips
	new(GetClass(),TEXT("UseTriStrips"),RF_Public) UBoolProperty(CPP_PROPERTY(UseTriStrips),TEXT(""),CPF_Edit);
	// Default to using strips
	UseTriStrips = OldUseTriStrips = 1;
	new(GetClass(),TEXT("bCacheOptimize"),RF_Public) UBoolProperty(CPP_PROPERTY(bCacheOptimize),TEXT(""),CPF_Edit);
	// Default to optimizing for memory
	bCacheOptimize = OldbCacheOptimize = 0;

	FArchive ArDummy;
	UStruct* MaterialStruct = new(GetClass(),TEXT("StaticMeshMaterial")) UStruct(NULL);
	new(MaterialStruct,TEXT("Material"),RF_Public)			UObjectProperty(EC_CppProperty,STRUCT_OFFSET(FStaticMeshMaterial,Material),TEXT(""),CPF_Edit,UMaterial::StaticClass());
	new(MaterialStruct,TEXT("EnableCollision"),RF_Public)	UBoolProperty(EC_CppProperty,STRUCT_OFFSET(FStaticMeshMaterial,EnableCollision),TEXT(""),CPF_Edit);
	MaterialStruct->SetPropertiesSize(sizeof(FStaticMeshMaterial));
	MaterialStruct->Link(ArDummy,NULL);

	UArrayProperty*	A	= new(GetClass(),TEXT("Materials"),RF_Public)	UArrayProperty(CPP_PROPERTY(Materials),TEXT(""),CPF_Edit | CPF_EditConstArray);
	A->Inner			= new(A,TEXT("StructProperty0"),RF_Public)		UStructProperty(EC_CppProperty,0,TEXT(""),CPF_Edit,MaterialStruct);

	UseSimpleLineCollision = 0;
	UseSimpleBoxCollision = 1;
	UseSimpleKarmaCollision = 1;
	UseVertexColor = 0;

	//scion rwd
	new(GetClass(),TEXT("CollisionStaticMesh"),RF_Public)		UObjectProperty(CPP_PROPERTY(CollisionStaticMesh),TEXT(""),CPF_Edit,UStaticMesh::StaticClass());
	CollisionStaticMesh = NULL;

	unguard;
}

//
//	UStaticMesh::PostEditChange
//

void UStaticMesh::PostEditChange()
{
	guard(UStaticMesh::PostEditChange);

	Super::PostEditChange();

	// Check if we need to rebuild the static mesh.
	UBOOL Rebuild = 0;
	// See if the user wants to change strip mode
	if (UseTriStrips == OldUseTriStrips &&
		bCacheOptimize == OldbCacheOptimize)
	{
		// Check materials for changes
		for(INT MaterialIndex = 0;MaterialIndex < Materials.Num();MaterialIndex++)
		{
			FStaticMeshMaterial*	Material = &Materials(MaterialIndex);

			if(Material->EnableCollision != Material->OldEnableCollision)
			{
				Material->OldEnableCollision = Material->EnableCollision;
				Rebuild = 1;
				break;
			}
		}
	}
	else
	{
		// Changed strip status
		OldUseTriStrips = UseTriStrips;
		OldbCacheOptimize = bCacheOptimize;
		Rebuild = 1;
	}
	if (Rebuild)
	{
		Build();
	}
	unguard;
}

//
// UStaticMesh::Destroy
//

void UStaticMesh::Destroy()
{
	guard(UStaticMesh::Destroy);

#ifdef WITH_KARMA
	KTermStaticMeshCollision(this);

	// Destroy any inner's of this static mesh. This should only happen in the editor i think.
	if(KPhysicsProps && !GIsGarbageCollecting)
	{
		delete KPhysicsProps;
		KPhysicsProps = NULL;
	}
#endif

#ifdef _XBOX
	//scion jg -- Clean up any compiled pushbuffers
	for (INT nSection = 0; nSection < Sections.Num(); nSection++)
	{
		if (Sections(nSection).pPushBuffer != NULL)
		{
			delete Sections(nSection).pPushBuffer;
		}
	}
#endif

	Super::Destroy();
	unguard;
}

//
//	UStaticMesh::Rename
//
void UStaticMesh::Rename( const TCHAR* InName, UObject* NewOuter )
{
	guard(UStaticMesh::Rename);

	// Also rename the collision mode (if present), if the outer has changed.
    if( NewOuter && CollisionModel && CollisionModel->GetOuter() == GetOuter() )
		CollisionModel->Rename( CollisionModel->GetName(), NewOuter );

	// Rename the static mesh
    Super::Rename( InName, NewOuter );

	unguard;
}

//
//	FStaticMeshCollisionPrimitive
//

class FStaticMeshCollisionPrimitive
{
public:

	TArray<INT>		Children;
	FBox			BoundingBox;
	FPlane			Plane;
	FVector			Vertices[3];
	UMaterial*		Material;

	FStaticMeshCollisionPrimitive()
	{
		Material = NULL;
	}

	~FStaticMeshCollisionPrimitive()
	{
	}

	friend FArchive& operator<<(FArchive& Ar,FStaticMeshCollisionPrimitive& P)
	{
		Ar	<< P.Children
			<< P.BoundingBox;

		if(!P.Children.Num())
		{
			FPlane	SidePlanes[3];

			Ar	<< P.Plane;

			if(Ar.Ver() < 83)
			{
				Ar 	<< SidePlanes[0]
					<< SidePlanes[1]
					<< SidePlanes[2];
			}

			if(Ar.Ver() >= 80)
			{
				Ar	<< P.Vertices[0]
					<< P.Vertices[1]
					<< P.Vertices[2];
			}
			else
			{
				FIntersectPlanes3(P.Vertices[0],P.Plane,SidePlanes[2],SidePlanes[0]);
				FIntersectPlanes3(P.Vertices[1],P.Plane,SidePlanes[0],SidePlanes[1]);
				FIntersectPlanes3(P.Vertices[2],P.Plane,SidePlanes[1],SidePlanes[2]);
			}

			if(Ar.Ver() >= 95)
				Ar << P.Material;
		}

		return Ar;
	}
};

//
//	FStaticMeshOldVertex
//

struct FStaticMeshOldVertex
{
	FVector	Position,
			Normal;
	FLOAT	U,
			V;

	// Serializer.

	friend FArchive& operator<<(FArchive& Ar,FStaticMeshOldVertex& V)
	{
		Ar	<< V.Position
			<< V.Normal
			<< V.U
			<< V.V;

		return Ar;
	}
};

//
//	FStaticMeshOldCollisionTriangle
//

struct FStaticMeshOldCollisionTriangle
{
public:

	FPlane		Plane;
	FPlane		SidePlanes[3];
	INT			VertexIndices[3];
	INT			MaterialIndex;
	INT			CollisionTag;

	// Serializer.
	friend FArchive& operator<<(FArchive& Ar,FStaticMeshOldCollisionTriangle& T)
	{
		return Ar	<< T.Plane
					<< T.SidePlanes[0]
					<< T.SidePlanes[1]
					<< T.SidePlanes[2]
					<< AR_INDEX(T.VertexIndices[0])
					<< AR_INDEX(T.VertexIndices[1])
					<< AR_INDEX(T.VertexIndices[2])
					<< AR_INDEX(T.MaterialIndex);
	}
};


//
//	FStaticMeshOldCollisionNode
//

struct FStaticMeshOldCollisionNode
{
public:

	INT		TriangleIndex,
			CoplanarIndex,
			ChildIndices[2];

	FBox	BoundingBox;

	// Serializer.
	friend FArchive& operator<<(FArchive& Ar,FStaticMeshOldCollisionNode& N)
	{
		return Ar	<< AR_INDEX(N.TriangleIndex)
					<< AR_INDEX(N.CoplanarIndex)
					<< AR_INDEX(N.ChildIndices[0])
					<< AR_INDEX(N.ChildIndices[1])
					<< N.BoundingBox;
	}
};


//
//	FStaticMeshTriangle serializer.
//

FArchive& operator<<(FArchive& Ar,FStaticMeshTriangle& T)
{
	if(Ar.Ver() < 111)
	{
		FStaticMeshOldVertex	TempVertices[3];

		Ar	<< TempVertices[0]
			<< TempVertices[1]
			<< TempVertices[2];

		T.Vertices[0] = TempVertices[0].Position;
		T.UVs[0][0].U = TempVertices[0].U;
		T.UVs[0][0].V = TempVertices[0].V;
		T.Colors[0] = FColor(255,255,255,255);

		T.Vertices[1] = TempVertices[1].Position;
		T.UVs[1][0].U = TempVertices[1].U;
		T.UVs[1][0].V = TempVertices[1].V;
		T.Colors[1] = FColor(255,255,255,255);

		T.Vertices[2] = TempVertices[2].Position;
		T.UVs[2][0].U = TempVertices[2].U;
		T.UVs[2][0].V = TempVertices[2].V;
		T.Colors[2] = FColor(255,255,255,255);

		T.NumUVs = 1;
	}
	else
	{
		Ar	<< T.Vertices[0]
			<< T.Vertices[1]
			<< T.Vertices[2]
			<< T.NumUVs;

		for(INT UVIndex = 0;UVIndex < T.NumUVs;UVIndex++)
			Ar	<< T.UVs[0][UVIndex]
				<< T.UVs[1][UVIndex]
				<< T.UVs[2][UVIndex];

		Ar	<< T.Colors[0]
			<< T.Colors[1]
			<< T.Colors[2];
	}

	if(Ar.Ver() < 112)
	{
		FVector	Normal;
		Ar << Normal;

		Ar << T.LegacyMaterial << T.LegacyPolyFlags;
	}
	else
		Ar << T.MaterialIndex;

	Ar << T.SmoothingMask;
	
	return Ar;
}

//
//	UStaticMesh::Serialize
//

void UStaticMesh::Serialize(FArchive& Ar)
{
	guard(UStaticMesh::Serialize);

	Super::Serialize(Ar);

	Ar	<< Sections
		<< BoundingBox;

	//jg -- Read the compression scale values if the right versions
	// This must be done before the vertex streams so that the xbox
	// version auto compresses correctly
	if (Ar.Ver() >= 135)
	{
		Ar << CompressionScale << CompressionInvScale
			<< UseTriStrips << OldUseTriStrips << bCacheOptimize
			<< OldbCacheOptimize;
		// Load in the compression offset
		if (Ar.Ver() >= 137)
		{
			Ar << CompressionOffset;
		}
	}
	else
	{
		CompressionScale = FVector(1.f,1.f,1.f);
		CompressionInvScale = FVector(1.f,1.f,1.f);
		CompressionOffset = FVector(0.f,0.f,0.f);
	}
	// Set the compression scale & inverse scale on the compressor class
	FPackedPosition::SetCompressionVectors(&CompressionScale,
		&CompressionInvScale,&CompressionOffset);

	if(Ar.Ver() >= 112)
	{
		Ar << VertexStream
			<< ColorStream
			<< AlphaStream
			<< UVStreams
			<< IndexBuffer;

		//jg -- We don't support wireframe on the xbox so don't bother with the data
		if (Ar.LicenseeVer() != XBOX_PACKAGE_FORMAT)
		{
			Ar << WireframeIndexBuffer;
		}

		Ar << CollisionModel;

		// kDOP collision code
		Ar << kDOPTree;

		if(Ar.Ver() < 114)
			Ar 	<< UseSimpleLineCollision
				<< UseSimpleBoxCollision
				<< UseVertexColor;
	}

	//jg -- No need to have the raw triangles on the xbox either
	if (Ar.LicenseeVer() != XBOX_PACKAGE_FORMAT)
	{
		UBOOL SavedLazyLoad = GLazyLoad;
		GLazyLoad = 1;
		Ar << RawTriangles;
		GLazyLoad = SavedLazyLoad;
	}

	if( Ar.IsLoading() )
    {
		Ar << InternalVersion;
	}
    else if( Ar.IsSaving() )
    {
		InternalVersion = STATICMESH_VERSION;
		Ar << InternalVersion;
    }

	// Content authentication.
	Ar << AuthenticationKey;

	//jg -- Wipe the compression scale & offset on the compressor class
	// so that we can find errors faster
	FPackedPosition::ClearCompressionVectors();

	unguard;
}

//
//	UStaticMesh::PostLoad
//
void UStaticMesh::PostLoad()
{
	guard(UStaticMesh::PostLoad);

	Super::PostLoad();

#ifndef CONSOLE // our data is already loaded and preprocessed on PS2
	if(InternalVersion == INDEX_NONE)
	{
		if(!RawTriangles.Num())	
			RawTriangles.Load();

		// Derive the raw triangle data from the optimized static mesh data.
		for(INT SectionIndex = 0;SectionIndex < Sections.Num();SectionIndex++)
		{
			FStaticMeshSection&	Section = Sections(SectionIndex);

			if(Section.IsStrip)
			{
				for(INT TriangleIndex = 0;TriangleIndex < Section.NumPrimitives;TriangleIndex++)
				{
					INT	Indices[3];

					if(TriangleIndex & 1)
					{
						Indices[0] = LegacyIndexBuffer->Indices(Section.LegacyFirstIndex + TriangleIndex + 1);
						Indices[1] = LegacyIndexBuffer->Indices(Section.LegacyFirstIndex + TriangleIndex + 0);
						Indices[2] = LegacyIndexBuffer->Indices(Section.LegacyFirstIndex + TriangleIndex + 2);
					}
					else
					{
						Indices[0] = LegacyIndexBuffer->Indices(Section.LegacyFirstIndex + TriangleIndex + 0);
						Indices[1] = LegacyIndexBuffer->Indices(Section.LegacyFirstIndex + TriangleIndex + 1);
						Indices[2] = LegacyIndexBuffer->Indices(Section.LegacyFirstIndex + TriangleIndex + 2);
					}

					if(Indices[0] == Indices[1] || Indices[1] == Indices[2] || Indices[0] == Indices[2])
						continue;

					FStaticMeshTriangle*	Triangle = new(RawTriangles) FStaticMeshTriangle;

					Triangle->LegacyMaterial = Section.LegacyMaterial;
					Triangle->SmoothingMask = 0;
					Triangle->NumUVs = 1;

					for(INT VertexIndex = 0;VertexIndex < 3;VertexIndex++)
					{
						Triangle->Vertices[VertexIndex] = LegacyVertexBuffer->Vertices(Indices[VertexIndex]).Position;
						Triangle->UVs[VertexIndex][0].U = LegacyVertexBuffer->Vertices(Indices[VertexIndex]).U;
						Triangle->UVs[VertexIndex][0].V = LegacyVertexBuffer->Vertices(Indices[VertexIndex]).V;
						Triangle->Colors[VertexIndex] = FColor(255,255,255,255);
					}
				}
			}
			else
			{
				for(INT TriangleIndex = 0;TriangleIndex < Section.NumTriangles;TriangleIndex++)
				{
					INT	Indices[3];

					Indices[0] = LegacyIndexBuffer->Indices(Section.LegacyFirstIndex + TriangleIndex * 3 + 0);
					Indices[1] = LegacyIndexBuffer->Indices(Section.LegacyFirstIndex + TriangleIndex * 3 + 1);
					Indices[2] = LegacyIndexBuffer->Indices(Section.LegacyFirstIndex + TriangleIndex * 3 + 2);

					FStaticMeshTriangle*	Triangle = new(RawTriangles) FStaticMeshTriangle;

					Triangle->LegacyMaterial = Section.LegacyMaterial;
					Triangle->SmoothingMask = 0;
					Triangle->NumUVs = 1;

					for(INT VertexIndex = 0;VertexIndex < 3;VertexIndex++)
					{
						Triangle->Vertices[VertexIndex] = LegacyVertexBuffer->Vertices(Indices[VertexIndex]).Position;
						Triangle->UVs[VertexIndex][0].U = LegacyVertexBuffer->Vertices(Indices[VertexIndex]).U;
						Triangle->UVs[VertexIndex][0].V = LegacyVertexBuffer->Vertices(Indices[VertexIndex]).V;
						Triangle->Colors[VertexIndex] = FColor(255,255,255,255);
					}
				}
			}
		}
	}

	LegacyVertexBuffer = NULL;
	LegacyIndexBuffer = NULL;
	LegacyWireframeIndexBuffer = NULL;

	//!!OLDVER
	if( InternalVersion <= 6 && InternalVersion != INDEX_NONE )
	{
		// Convert polyflags to materials
		if(!RawTriangles.Num())
			RawTriangles.Load();

		DWORD LastPolyFlags = 0;
		UMaterial* LastMaterial = NULL;
		UMaterial* NewMaterial = NULL;

		for( INT i=0;i<RawTriangles.Num();i++ )
		{
			FStaticMeshTriangle& Tri = RawTriangles(i);
			if( Tri.LegacyMaterial )
			{
				if( Tri.LegacyMaterial != LastMaterial || Tri.LegacyPolyFlags != LastPolyFlags )
				{
					NewMaterial = Tri.LegacyMaterial->ConvertPolyFlagsToMaterial( Tri.LegacyMaterial, Tri.LegacyPolyFlags );
					LastMaterial = Tri.LegacyMaterial;
					LastPolyFlags = Tri.LegacyPolyFlags;
				}

				if( Tri.LegacyMaterial != NewMaterial )
				{
					Tri.LegacyMaterial = NewMaterial;
					Tri.LegacyPolyFlags = 0;
				}
			}
		}
	}

	if(InternalVersion < 8)
	{
		// Convert direct material references to material indices.

		if(!RawTriangles.Num())
			RawTriangles.Load();

		FStaticMeshMaterial*	CurrentMaterial = NULL;
		INT						CurrentMaterialIndex = INDEX_NONE;

		for(INT TriangleIndex = 0;TriangleIndex < RawTriangles.Num();TriangleIndex++)
		{
			FStaticMeshTriangle*	Triangle = &RawTriangles(TriangleIndex);

			if(!CurrentMaterial || Triangle->LegacyMaterial != CurrentMaterial->Material)
			{
				CurrentMaterialIndex = Materials.Num();
				CurrentMaterial = new(Materials) FStaticMeshMaterial(Triangle->LegacyMaterial);
			}

			Triangle->MaterialIndex = CurrentMaterialIndex;
			Triangle->LegacyMaterial = NULL;
		}
	}

	if(InternalVersion < STATICMESH_VERSION)
		Build();

#endif
#ifdef _XBOX
#if !defined(XBOX_ASSET_FORMAT)
	//scion jg -- Compile each section's indices
	for (INT nSection = 0; nSection < Sections.Num(); nSection++)
	{
		if (Sections(nSection).NumPrimitives > 0)
		{
			// Compile differently if strips or not
			if (Sections(nSection).IsStrip)
			{
				Sections(nSection).pPushBuffer = appCompileIndicesToPushBuffer(
					PT_TriangleStrip,Sections(nSection).NumPrimitives + 2,
					&IndexBuffer.Indices(Sections(nSection).FirstIndex),0);
			}
			else
			{
				Sections(nSection).pPushBuffer = appCompileIndicesToPushBuffer(
					PT_TriangleList,Sections(nSection).NumPrimitives * 3,
					&IndexBuffer.Indices(Sections(nSection).FirstIndex),0);
			}
		}
		else
		{
			// Make sure to zero this out so we don't accidentally try to
			// free it when it is destroyed
			Sections(nSection).pPushBuffer = NULL;
		}
	}
	//scion jg -- Change the memory settings on the vertices so the CPU
	// can access them without the write combined penalty
	if (VertexStream.Vertices.Num() > 0)
	{
		appMemCPUFastAccess(&VertexStream.Vertices(0),
			sizeof(FStaticMeshVertex) * VertexStream.Vertices.Num());
	}
	//scion jg -- Memory savings
	WireframeIndexBuffer.Indices.Empty();
//#if !DYNAMIC_PROJ_STREAM 
#if 0
	//jg -- We can dump index buffers, since we use push buffers instead
	IndexBuffer.Indices.Empty();
#endif
#else
	//jg -- Fix up pushbuffers from XPR files
	SetAllMeshData(TRUE);
	// These should be empty, but just in case
	IndexBuffer.Indices.Empty();
#endif
#endif
	unguard;
}

/**
 * Common routine for fixing up pushbuffer pointers
 */
void UStaticMesh::SetAllMeshData(UBOOL bCheckResults)
{
	guard(UStaticMesh::SetAllMeshData);
#ifdef _XBOX
	// Work through each section
	for (INT nSection = 0; nSection < Sections.Num(); nSection++)
	{
		if (Sections(nSection).NumPrimitives > 0)
		{
			// If we've exported to an XPR
			if (IndexBuffer.Indices.Num() == 0)
			{
				check(Sections(nSection).pPushBuffer == NULL);
				// Try to fix them up
				Sections(nSection).pPushBuffer = GetPushbufferFromXPR(
					*FString::Printf(STATICMESH_PB_PF,GetName(),nSection));
#ifndef FINAL_RELEASE
				if (bCheckResults == TRUE)
				{
					check(Sections(nSection).pPushBuffer);
				}
#endif
			}
			else
			{
				// Compile differently if strips or not
				if (Sections(nSection).IsStrip)
				{
					Sections(nSection).pPushBuffer = appCompileIndicesToPushBuffer(
						PT_TriangleStrip,Sections(nSection).NumPrimitives + 2,
						&IndexBuffer.Indices(Sections(nSection).FirstIndex),0);
				}
				else
				{
					Sections(nSection).pPushBuffer = appCompileIndicesToPushBuffer(
						PT_TriangleList,Sections(nSection).NumPrimitives * 3,
						&IndexBuffer.Indices(Sections(nSection).FirstIndex),0);
				}
			}
		}
		else
		{
			// Make sure to zero this out so we don't accidentally try to
			// free it when it is destroyed
			Sections(nSection).pPushBuffer = NULL;
		}
	}
	// All TUmaArray data for this static mesh is contained in an XPR file
	// so we need to do some pointer fix up
	INT Count = 0;
	void* Data = NULL;
	// Don't fix up data that was serialized the old fashioned way
	if (VertexStream.Vertices.IsInXPR())
	{
		// Find the vert data
		verify(GetTUmaArrayFromXPR(*FString::Printf(VERTEXSTREAM_PF,GetName()),Count,Data) ||
			bCheckResults == FALSE);
		// Now fix up the array with that
		VertexStream.Vertices.FixUpArray(Count,Data);
	}
	// Don't fix up data that was serialized the old fashioned way
	if (ColorStream.Colors.IsInXPR())
	{
		// Find the color stream data
		verify(GetTUmaArrayFromXPR(*FString::Printf(COLORSTREAM_PF,GetName()),Count,Data) ||
			bCheckResults == FALSE);
		// Now fix up the array with that
		ColorStream.Colors.FixUpArray(Count,Data);
	}
	// Don't fix up data that was serialized the old fashioned way
	if (AlphaStream.Colors.IsInXPR())
	{
		// Find the color stream data
		verify(GetTUmaArrayFromXPR(*FString::Printf(ALPHASTREAM_PF,GetName()),Count,Data) ||
			bCheckResults == FALSE);
		// Now fix up the array with that
		AlphaStream.Colors.FixUpArray(Count,Data);
	}
	// Fix each of the UV streams
	for (INT Index = 0; Index < UVStreams.Num(); Index++)
	{
		// Don't fix up data that was serialized the old fashioned way
		if (UVStreams(Index).UVs.IsInXPR())
		{
			// Find the color stream data
			verify(GetTUmaArrayFromXPR(*FString::Printf(UVSTREAM_PF,GetName(),Index),
				Count,Data) || bCheckResults == FALSE);
			// Now fix up the array with that
			UVStreams(Index).UVs.FixUpArray(Count,Data);
		}
	}
#endif
	unguard;
}

/**
 * Common routine for clearing pushbuffer pointers
 */
void UStaticMesh::ClearAllMeshData(void)
{
	guard(UStaticMesh::ClearAllMeshData);
#ifdef _XBOX
	// Work through each section
	for (INT nSection = 0; nSection < Sections.Num(); nSection++)
	{
		// Don't fix up the pushbuffer if we did it previously
		// (prevents a small leak)
		if (Sections(nSection).pPushBuffer != NULL)
		{
			delete Sections(nSection).pPushBuffer;
		}
		Sections(nSection).pPushBuffer = NULL;
	}
	// Flush the vertex streams
	VertexStream.Flush();
	ColorStream.Flush();
	AlphaStream.Flush();
	// Flush each of the UV streams
	for (INT Index = 0; Index < UVStreams.Num(); Index++)
	{
		UVStreams(Index).Flush();
	}
#endif
	unguard;
}

//
//	UStaticMesh::GetRenderBoundingBox
//

FBox UStaticMesh::GetRenderBoundingBox(const AActor* Owner)
{
	return BoundingBox;
}

//
//	UStaticMesh::GetRenderBoundingSphere
//

FSphere UStaticMesh::GetRenderBoundingSphere(const AActor* Owner)
{
	return BoundingSphere;
}

FVector UStaticMesh::GetEncroachExtent(AActor* Owner)
{
	return GetCollisionBoundingBox(Owner).GetExtent();
}

FVector UStaticMesh::GetEncroachCenter(AActor* Owner)
{
	return GetCollisionBoundingBox(Owner).GetCenter();
}

//
//	UStaticMesh::Illuminate
//

// scion sz - not used on xbox
#ifdef _XBOX
void UStaticMesh::Illuminate(AActor* Owner,UBOOL ChangedOnly) {}
#else
void UStaticMesh::Illuminate(AActor* Owner,UBOOL ChangedOnly)
{
	guard(UStaticMesh::Illuminate);
    AMover*	MoverOwner = (Owner && Owner->IsAMover()) ? Cast<AMover>(Owner) : NULL;
	if(Owner->bStatic || (MoverOwner && !MoverOwner->bDynamicLightMover))
	{
		if(!(Owner->bHiddenEd || Owner->bHiddenEdGroup))
		{
			// Calculate the actor's bounding sphere.

			FDynamicActor*	DynamicActor = Owner->GetActorRenderData();
			FMatrix&		LocalToWorld = DynamicActor->LocalToWorld;
			FSphere&		BoundingSphere = DynamicActor->BoundingSphere;

			// Determine relevant lights.

			TArray<AActor*>	Lights;
			UBOOL			Changed = 0;

			for(INT LeafIndex = 0;LeafIndex < Owner->Leaves.Num();LeafIndex++)
			{
				FLeaf&	Leaf = Owner->XLevel->Model->Leaves(Owner->Leaves(LeafIndex));

				if(Leaf.iPermeating >= 0)
				{
					for(INT iPermeating = Leaf.iPermeating;Owner->XLevel->Model->Lights(iPermeating);iPermeating++)
					{
						AActor*	LightActor = Owner->XLevel->Model->Lights(iPermeating);

						if( LightActor->bHiddenEd || LightActor->bHiddenEdGroup ||
							//scion jg -- Reject skeletal only lights
							LightActor->AffectType == AT_AffectSkeletal)
                            continue;

						if(LightActor && LightActor->LightType != LT_None && LightActor->bSpecialLit == Owner->bSpecialLit && !LightActor->bDeleteMe && !LightActor->bDynamicLight)
						{
							AActor*	ExistingLight = NULL;

							for(INT LightIndex = 0;LightIndex < Lights.Num();LightIndex++)
							{
								if(Lights(LightIndex) == LightActor)
								{
									ExistingLight = Lights(LightIndex);
									break;
								}
							}

							if(!ExistingLight)
							{
								if(LightActor->LightEffect == LE_Sunlight)
								{
									if(Cast<ASkyZoneInfo>(LightActor->Region.Zone) || LightActor->Region.ZoneNumber == Leaf.iZone)
									{
										Lights.AddItem(LightActor);

										if(LightActor->bLightChanged)
											Changed = 1;
									}
								}
								else if((LightActor->Location - BoundingSphere).SizeSquared() < Square(LightActor->WorldLightRadius() + BoundingSphere.W))
								{
									Lights.AddItem(LightActor);

									if(LightActor->bLightChanged)
										Changed = 1;
								}
							}
						}
					}
				}
			}

			if(!ChangedOnly || Owner->bLightChanged || !Owner->StaticMeshInstance)
			{
				// Initialize the static mesh instance data.

				Owner->StaticMeshInstance = new(Owner->GetOuter(),NAME_None,0) UStaticMeshInstance();

				Owner->StaticMeshInstance->ColorStream.Colors.AddZeroed(VertexStream.Vertices.Num());
				Owner->StaticMeshInstance->ColorStream.Revision++;
			}
			else
			{
				// Remove old lights.

				for(INT LightIndex = 0;LightIndex < Owner->StaticMeshInstance->Lights.Num();LightIndex++)
					if(Owner->StaticMeshInstance->Lights(LightIndex).LightActor->bLightChanged)
						Owner->StaticMeshInstance->Lights.Remove(LightIndex--);

				Owner->StaticMeshInstance->ColorStream.Revision++;
			}

			// Add new lights.

			for(INT LightIndex = 0;LightIndex < Lights.Num();LightIndex++)
			{
				AActor*	LightActor = Lights(LightIndex);

				if(!ChangedOnly || Owner->bLightChanged || LightActor->bLightChanged)
				{
					FStaticMeshLightInfo*	LightInfo = new(Owner->StaticMeshInstance->Lights) FStaticMeshLightInfo(LightActor);
					BYTE*					BitPtr = &LightInfo->VisibilityBits(LightInfo->VisibilityBits.Add((VertexStream.Vertices.Num() + 7) / 8));
					BYTE					BitMask = 1;
					FVector					LightDirection = LightActor->Rotation.Vector();
					UBOOL					IsSunlight = LightInfo->LightActor->LightEffect == LE_Sunlight;

					for(INT VertexIndex = 0;VertexIndex < VertexStream.Vertices.Num();VertexIndex++)
					{
						FVector	SamplePoint = LocalToWorld.TransformFVector(VertexStream.Vertices(VertexIndex).Position),
								SampleNormal = LocalToWorld.TransformNormal(VertexStream.Vertices(VertexIndex).Normal).SafeNormal();

						if(!IsSunlight && ((LightActor->Location - SamplePoint) | SampleNormal) < 0.0f)
							*BitPtr &= ~BitMask;
						else if(IsSunlight && (LightDirection | SampleNormal) > 0.0f)
							*BitPtr &= ~BitMask;
						else if(!Owner->bStaticLighting)
						{
							FCheckResult	Hit(0);

							if(IsSunlight)// && Owner->XLevel->SingleLineCheck(Hit,Owner,SamplePoint - LightDirection * HALF_WORLD_MAX,SamplePoint - LightDirection * 4.0f,TRACE_ShadowCast|TRACE_Level|TRACE_Actors))
								*BitPtr |= BitMask;
							else if(!IsSunlight)
								*BitPtr |= BitMask;
							else
								*BitPtr &= ~BitMask;
						}
						else
						{
							FCheckResult	Hit(0);

							if(IsSunlight && Owner->XLevel->SingleLineCheck(Hit,NULL,SamplePoint - LightDirection * HALF_WORLD_MAX,SamplePoint - LightDirection * 4.0f,TRACE_ShadowCast|TRACE_Level|TRACE_Actors))
								*BitPtr |= BitMask;
							else if(!IsSunlight && Owner->XLevel->SingleLineCheck(Hit,NULL,SamplePoint + (LightActor->Location - SamplePoint).SafeNormal() * 4.0f,LightActor->Location,TRACE_ShadowCast|TRACE_Level|TRACE_Actors))
								*BitPtr |= BitMask;
							else
								*BitPtr &= ~BitMask;
						}

						BitMask <<= 1;

						if(!BitMask)
						{
							BitMask = 1;
							BitPtr++;
						}
					}
				}
			}

			CalculateStaticMeshLighting( this, Owner->StaticMeshInstance, DynamicActor );
			Owner->bLightChanged = 0;
		}
	}
	else
		Owner->StaticMeshInstance = NULL;

	unguard;
}
#endif

//
//	UStaticMeshInstance::Serialize
//

void UStaticMeshInstance::Serialize(FArchive& Ar)
{
	guard(UStaticMeshInstance::Serialize);
	Super::Serialize(Ar);
	//jg -- Removed no longer needed backwards compatibility serialization code
	Ar << ColorStream;
	// Don't serialize these during conversion to make packages smaller
	// and load faster
	if (Ar.LicenseeVer() != XBOX_PACKAGE_FORMAT)
	{
		Ar << Lights;
	}
	unguard;
}

/**
 * jg -- Wire up the color stream to the XPR loaded data
 */
void UStaticMeshInstance::PostLoad(void)
{
	guard(UStaticMesh::PostLoad);
	Super::PostLoad();
#if defined(_XBOX) && defined(XBOX_ASSET_FORMAT)
	// All TUmaArray data for this static mesh instance is contained in an
	// XPR file so we need to do some pointer fix up
	INT Count = 0;
	void* Data = NULL;
	// Don't fix up data that was serialized the old fashioned way
	if (ColorStream.Colors.IsInXPR())
	{
		// Find the color stream data
		verify(GetTUmaArrayFromXPR(*FString::Printf(COLORSTREAM_PF,GetName()),Count,Data));
		// Now fix up the array with that
		ColorStream.Colors.FixUpArray(Count,Data);
	}
#endif
	unguard;
}


//scion rwd - custom precache for meshes
void UStaticMesh::Precache(UObject *src,TMap<UObject*,BYTE> *masterPrecacheList)
{
#ifdef _XBOX
	((UGameEngine*)UEngine::GEngine)->GLevel->GetLevelInfo()->PrecacheStaticMeshes.AddItem(this);
#endif
}
