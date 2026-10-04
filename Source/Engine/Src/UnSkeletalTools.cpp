/*=============================================================================
	UnSkeletalTools.cpp: Misc Unreal mesh and animation code for 
	editing, importing and other non-game-time processing.	
	Mostly internal mesh data management.

	Copyright 2001,2002   Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Erik de Neve	 

	Todo: 
	Create cleaner division of processing/sorting/normal-calculation for rigid and smooth vertices.

=============================================================================*/ 

#include "EnginePrivate.h"
#include "UnRender.h"


// Forwards..
class TTempModel;
void CoherenceFaceSort( FStaticLODModel* Model );
void ModelDataReSort( FStaticLODModel* Model );
void RecalculateSmoothNormals( FStaticLODModel* Model );
void RecalculateLODNormals( FStaticLODModel* NewLOD, TArray<FVector>& TempNormals );


/*-----------------------------------------------------------------------------
	USkeletalMesh preprocessing: LOD/normals/hardware rendering chunking.
-----------------------------------------------------------------------------*/

QSORT_RETURN CDECL CompareBoneWeight( const FVertInfluence* A, const FVertInfluence* B )
{
	if		( A->VertIndex  > B->VertIndex ) return  1;
	else if ( A->VertIndex  < B->VertIndex ) return -1;
	else if ( A->Weight     < B->Weight    ) return  1;
	else if ( A->Weight     > B->Weight    ) return -1;
	else if ( A->BoneIndex  > B->BoneIndex ) return  1;
	else if ( A->BoneIndex  < B->BoneIndex ) return -1;
	else                                     return  0;	
}


//
// Temporary LOD model, used to reorganize rigid, smooth parts etc.
//

class TTempModel
{	
public:

	TArray<FVertInfluence> Influences;
	TArray<FMeshWedge>     Wedges;
	TArray<FMeshFace>      Faces;
	TArray<FVector>        Points;	
	TArray<FVector>        Normals;

	TTempModel(  FStaticLODModel* Model )
	{ 
		Influences.Empty();
		Influences.Add( Model->Influences.Num() );
		for( INT i=0; i< Model->Influences.Num(); i++)
			Influences(i) = Model->Influences(i);

		Wedges.Empty();
		Wedges.Add( Model->Wedges.Num() );
		for( INT i=0; i< Model->Wedges.Num(); i++)
			Wedges(i) = Model->Wedges(i);

		Faces.Empty();
		Faces.Add( Model->Faces.Num() );
		for( INT i=0; i< Model->Faces.Num(); i++)
			Faces(i) = Model->Faces(i);

		Points.Empty();
		Points.Add( Model->Points.Num() );
		for( INT i=0; i< Model->Points.Num(); i++)
			Points(i) = Model->Points(i);
	}

	void CopyInto( FStaticLODModel* Model )
	{
		Model->Influences.Empty();
		Model->Influences.Add( Influences.Num() );
		for( INT i=0; i< Influences.Num(); i++)
			Model->Influences(i) = Influences(i);

		Model->Wedges.Empty();
		Model->Wedges.Add( Wedges.Num() );
		for( INT i=0; i< Wedges.Num(); i++)
			Model->Wedges(i) = Wedges(i);

		Model->Faces.Empty();
		Model->Faces.Add( Faces.Num() );
		for( INT i=0; i< Faces.Num(); i++)
			Model->Faces(i) = Faces(i);

		Model->Points.Empty();
		Model->Points.Add( Points.Num() );
		for( INT i=0; i< Points.Num(); i++)
			Model->Points(i) = Points(i);
	}

	void AddNormals( TArray<FVector>& Normals )
	{
		Normals.Empty();
		Normals.Add( Normals.Num() );
		for( INT i=0; i< Normals.Num(); i++)
			Normals(i) = Normals(i);
	}
};

//
// Create all render-specific (but serializable) data for this LOD : the 'compiled' rendering stream, 
// mesh sections, and index buffer.
//
void CreateSkinningStream( FStaticLODModel* NewLOD, USkeletalMesh* ParentMesh )
{
	guard(CreateSkinningStream);
	// v 2.0 SkinningStream:
	//
	// 1xxxxxxxx - same as others, but copy result TO the buffer (on top)
	// 1111nnnnn - copy result FROM the buffer at index n
	//
	// - preshifted the dupe-buffer-index and bone index
	// 
	// - Safeguard that influences are limited to 7 max.....
	//	
	UBOOL bVerbose = ( GIsEditor || GIsUCC );

	TArray<INT> VertMarkers;
	TArray<INT> WedgeMarkers;
	TArray<INT> VertDupeIdx;

	// Clean up. Allow multiple calls to CreateSkinningStream for same model.
	NewLOD->SmoothSections.Empty();
	NewLOD->SmoothVerts.Empty();
	NewLOD->SkinningStream.Empty();	
	NewLOD->SmoothIndexBuffer.Indices.Empty();	

	// Set SmoothVerts... 
	NewLOD->SmoothVerts.Add( NewLOD->Points.Num() ); 

	for( INT i=0; i<NewLOD->Points.Num(); i++ ) 
	{			
		NewLOD->SmoothVerts(i).Point = NewLOD->Points(i); 
	}	
	// Recalculate packed normals.
	RecalculateSmoothNormals( NewLOD );

	VertMarkers.AddZeroed( NewLOD->SmoothVerts.Num());
	VertDupeIdx.AddZeroed( NewLOD->SmoothVerts.Num());
	WedgeMarkers.AddZeroed( NewLOD->Wedges.Num());

	DWORD UniqueDuplicates = 0;	

	// Predigest loop to detect where points for multiple wedges are first calculated.
	for( INT w=0; w<NewLOD->Wedges.Num(); w++)
	{				
		INT VertIdx = NewLOD->Wedges(w).iVertex; 
		VertMarkers(VertIdx)++;
		// Mark first occurrence of a vertex in a wedge..
		if( VertMarkers(VertIdx) == 1)
		{
			WedgeMarkers(w)= -1; 
		}
	}

	// Now: Total buffer size known, and which vertices need to go in a buffer/come from a buffer...
	// Wedges that need to store a vertex are ( -1)
	// Now mark wedges that need to retrieve one.
	for( INT w=0; w<NewLOD->Wedges.Num(); w++)
	{				
		INT VertIdx = NewLOD->Wedges(w).iVertex; 

		if (VertMarkers(VertIdx) > 1) // non unique vertex: wedge needs store or load special-case.
		{
			// Wedge has unique first vertex occurrence: store..-> 0
			if( WedgeMarkers(w) == -1) 
			{
				WedgeMarkers(w) = 0; // Indicate first-of-multiple - otherwise, -1 (unique vertex+wedge).
				//debugf(TEXT("Storage command for wedge %i vertex %i into top of buffer = %i "),w,VertIdx,UniqueDuplicates);
				UniqueDuplicates++;
				VertDupeIdx(VertIdx) = UniqueDuplicates; // location in buffer (+1)				
			}
			else if( WedgeMarkers(w) == 0 ) // retrieval needed
			{
				WedgeMarkers(w) = VertDupeIdx(VertIdx);
			}
		}
	}

	VertDupeIdx.Empty();
	NewLOD->DupVertCount = UniqueDuplicates;

	// debugf(TEXT("## Duplicates-to-store total : %i"),NewLOD->DupVertCount);
	
	// Marker arrays:
	// - know which wedge marks first occurrence of a point
	// - for recurring points, know where off the temp stack to get them.
	
	INT InfIdx = 0;
	INT BufSizeSim = 0; 

	// Wedges already sorted in render order.
	// Process wedges to create transformation stream (section-independent)
	for( INT w=0; w<NewLOD->Wedges.Num(); w++)
	{		
		FMeshWedge& ThisWedge = NewLOD->Wedges(w);
		//     Add either single bone + UV  
		//  or Add multiple weights+indices
		//  or Add back-index + UV  if vertex alread processed
		INT VertIdx = ThisWedge.iVertex; //ThisWedge.iVertex;
	
		if( WedgeMarkers(w) > 0 ) // 3d Point already computed - get it from the dupe buffer; index premultiplied with 6 (normal+vertex) floats
		{			
			NewLOD->SkinningStream.AddItem( (DWORD)(( WedgeMarkers(w)-1 )*6) | (DWORD)0xF0000000 );			
			// Add U and V.
			NewLOD->SkinningStream.AddItem(*((DWORD*)&ThisWedge.TexUV.U));
			NewLOD->SkinningStream.AddItem(*((DWORD*)&ThisWedge.TexUV.V));
		}
		else   
		{
			// Count influences: 
			INT LookIdx = InfIdx;

			INT InfluenceCount = 0;
			while ( (NewLOD->Influences(LookIdx).VertIndex == VertIdx) && ( NewLOD->Influences.Num() > LookIdx) )
			{			
				InfluenceCount++;
				LookIdx++;
			}	

			// debugf(TEXT(" Influence count for vertex %i  is %i "),VertIdx ,InfluenceCount);
			if( InfluenceCount == 0 ) 
			{				
				debugf(TEXT("NULL VERTEX INFLUENCE DETECTED."));				
			}	

			if( InfluenceCount > 7 )
			{
				debugf(TEXT("INFLUENCES OVERFLOW: [%i]"),InfluenceCount);
				InfluenceCount = 7;
			}

			// if( ForceSingleInfluence ) InfluenceCount = 1;
			
			// First in any influence seqeuence: indicator bits to store the number of influences, or a copy-back instruction.			
			DWORD IndicatorBits = ((DWORD)(Min(8,InfluenceCount)-1)) << (DWORD)(32-4);			

			// Set high bit IF this one has to go into the duplicate-vertex-buffer at render time.
			if( WedgeMarkers(w) == 0 )
			{
				IndicatorBits |= 0x80000000;				
				BufSizeSim++;				
			}
	
			for( INT i=0; i<InfluenceCount; i++)
			{
				FLOAT  Weight = 0;
				INT    BoneIndex = 0;
				
				BoneIndex = NewLOD->Influences(InfIdx+i).BoneIndex;

				// Safety check (esp. for when importing non-compatible meshes as static LOD models.)
				if( BoneIndex >= ParentMesh->RefSkeleton.Num() )
					BoneIndex = 0;

				if( InfluenceCount > 0)
				{
					Weight = NewLOD->Influences(InfIdx+i).Weight;					
				}	

				// assumes bone maximum of 1024
				DWORD CompiledInfluence =  IndicatorBits |  (((DWORD)( Weight * 65535.f )) << 12 )  |  (DWORD)(BoneIndex * 6);
				
				// Store.
				NewLOD->SkinningStream.AddItem( CompiledInfluence );
				
				// Subsequent influences have no need for indicator tags.
				IndicatorBits = 0;
			}

			// Special fixup case of missing influences...
			if( InfluenceCount == 0)
			{
				if( WedgeMarkers(w) == 0 )
				{
					IndicatorBits |= 0x80000000;				
					BufSizeSim++;				
				}
				InfluenceCount = 1;
				FLOAT Weight = 1.0f;
				INT   BoneIndex = 0;
				DWORD IndicatorBits = (DWORD)( 0 ) << (DWORD)(32-4);			
				DWORD CompiledInfluence =  IndicatorBits |  (((DWORD)( Weight * 65535.f )) << 12 )  |  (DWORD)(BoneIndex * 6);
				NewLOD->SkinningStream.AddItem( CompiledInfluence );				
			}

			InfIdx = LookIdx; // Advance influences.
					
			// Add U and V.
			NewLOD->SkinningStream.AddItem(*((DWORD*)&ThisWedge.TexUV.U));
			NewLOD->SkinningStream.AddItem(*((DWORD*)&ThisWedge.TexUV.V));

			// Mark as processed with location in output stream.
			VertMarkers( VertIdx ) = -1; 
		}
	}
	NewLOD->SkinningStream.AddItem( (DWORD)0xFFFFFFFF ); // End-Of-Stream marker.

	// debugf(TEXT("Created SkinningStream, size: %i    Temp vertex dupes: %i"),NewLOD->SkinningStream.Num(),BufSizeSim);	

	// Tests-interpret entire stream for debugging.
	if( 0 )
	{
		INT BufSizeSim = 0;
		INT RSIndex=0;
		INT SimVertex = 0;
		while( RSIndex < NewLOD->SkinningStream.Num() )
		{
			DWORD R = NewLOD->SkinningStream(RSIndex);

			// Trailing DWORD>..
			debugf(TEXT(">> STREAM [%5i]  hex: %8X  Indicator [%3i]  - boneindex: [%4i] "),
				RSIndex,
				R,			
				(INT)((DWORD)R >> (DWORD)(32-4)),
				(INT)(R & (DWORD)0x0FFF)/6 
			);
			
			INT WeightNum = (INT)((DWORD)R >> (DWORD)(32-4));

			if( (R & 0x80000000) && (R < 0xF0000000 ) ) // high bit set but no 0xF: calculate-and-store .
				WeightNum = (INT)((DWORD)(R & 0x7FFFFFFF) >> (DWORD)(32-4));

			if( (R & 0xF0000000 ) != 0xF0000000 )
			{
				if( R & 0x80000000 ) 
				{
					BufSizeSim++;
					debugf(TEXT("[ Copy to buffer top - total: %i]"),BufSizeSim);					
				}

				for( INT i=0; i<WeightNum+1; i++)
				{									
					DWORD R=NewLOD->SkinningStream(RSIndex);
					debugf(TEXT("     WEIGHTS(%i)  [%5i]  hex: %8X  Indicator [%3i]  Weight [%f] BoneIndex [%4i] "),
						i,
						RSIndex,
						R,			
						(INT)(R >> (32-4)),
						(1.f/65535.f) * (FLOAT)(((DWORD)R >> (DWORD)(12)) & (DWORD)0xFFFF), 
						(INT)(R & (DWORD)0x0FFF)/6 
					);
					
					RSIndex++;
				}			
			}
			else // Copy from duplicate buffer.
			{
				debugf(TEXT(" Copy back from buffer index [%i]  Curr. Vertex: %i "),( R & 0x0fffffff )/6,  SimVertex );
				RSIndex++;
			}

			RSIndex+=2; //skip U&V
			SimVertex++;
		}
	}
	
	// Fill SmoothIndexBuffer from entire face array.	
	for( INT f=0; f<NewLOD->Faces.Num(); f++)
	{
		for( INT i=0; i<3; i++)
		{
			NewLOD->SmoothIndexBuffer.Indices.AddItem( NewLOD->Faces(f).iWedge[i] );
		}
	}
        	
	if( ++ NewLOD->SmoothIndexBuffer.Revision < 1 ) 
		NewLOD->SmoothIndexBuffer.Revision = 1; 

	// Number of wedges the SkinningStream will produce at render time, as reported back via the vertex stream's GetSize (without having
	// to load the LazyArray Wedges !)
	NewLOD->SmoothStreamWedges = NewLOD->Wedges.Num(); 	
	
	// Digest the sections.
	FSkelMeshSection* Section = NULL;
	for(INT FaceIndex = 0;FaceIndex < NewLOD->Faces.Num(); FaceIndex++)
	{
		FMeshFace ThisFace = NewLOD->Faces(FaceIndex);
		INT  ThisMatIndex = ThisFace.MeshMaterialIndex;			
		if( !Section || ( ThisMatIndex != Section->MaterialIndex ) )
		{
			// Create a new static mesh section.
			Section = new( NewLOD->SmoothSections)FSkelMeshSection;
			Section->FirstFace = FaceIndex;
			Section->MaterialIndex = ThisFace.MeshMaterialIndex;						
			Section->TotalFaces = 0;			
			Section->MinIndex = NewLOD->Wedges.Num();
			Section->MaxIndex = 0;
		}
		Section->TotalFaces++;
		for( INT i=0; i<3; i++)
		{
			Section->MinIndex = Min(Section->MinIndex,ThisFace.iWedge[i]);
			Section->MaxIndex = Max(Section->MaxIndex,ThisFace.iWedge[i]);
		}
	}	

	for(INT s=0; s<NewLOD->SmoothSections.Num(); s++)
	{		
		NewLOD->SmoothSections(s).TotalVerts = ( NewLOD->SmoothSections(s).MaxIndex - NewLOD->SmoothSections(s).MinIndex )+1; 
		
		if( bVerbose) 
		{
			debugf(TEXT(" Smooth section constructed: [%3i], MinIndex  %3i, MaxIndex %3i, Material %3i Faces %3i FirstFace %3i Wedges: %i"),
			s,
			NewLOD->SmoothSections(s).MinIndex,
			NewLOD->SmoothSections(s).MaxIndex,
			NewLOD->SmoothSections(s).MaterialIndex,
			NewLOD->SmoothSections(s).TotalFaces,
			NewLOD->SmoothSections(s).FirstFace,
			NewLOD->SmoothStreamWedges
			);
		}
	}

	unguard;
}


//
// Resort and prepare the skinning stream for this LOD model
//
void FinalizeLodModel( FStaticLODModel* NewModel, USkeletalMesh* ParentMesh, UBOOL bSlowTaskAllowed )
{	
	guard( FinalizeLodModel);

	// Coherence-sort the (raw) faces.
	if( bSlowTaskAllowed ) CoherenceFaceSort( NewModel );  
	
	// Draw-order-sort the raw wedges and vertices as they are encountered through the face order.
	ModelDataReSort( NewModel );
	// NOTE: from here on, the raw mesh data is out of sync with LODModel data.	

	// Standard rigidparts extraction could be done here.

	// Prepare skinning stream for ComputeSkinVerts.
	CreateSkinningStream( NewModel , ParentMesh );

	unguard;
}
	

void LODModelErase( FStaticLODModel* Model )
{
	guard( LODModelErase );

	Model->SkinningStream.Empty();
	Model->SmoothVerts.Empty();	
	Model->SmoothSections.Empty();
	Model->RigidSections.Empty();

	Model->SmoothIndexBuffer.Indices.Empty();
	Model->RigidIndexBuffer.Indices.Empty();
	Model->RigidVertexStream.Vertices.Empty();

	Model->ActiveBoneIndices.Empty();
	Model->Influences.Empty();
	Model->Wedges.Empty();
	Model->Faces.Empty();
	Model->Points.Empty();

	Model->DisplayFactor = 0.0f; // Lod factor at which this LOD (or a lower one) kicks in.
	Model->LODHysteresis = 0.0f; // Flexible DisplayFactor range delta allowed to prevent flickering between lods.
	Model->DupVertCount = 0;  // Required duplicate vertices buffer size during skinning.	
	Model->MaxInfluences = 0; // For the smooth sections; if 0, they can use simpler rendering scheme.
	Model->bUniqueSubset = false; // Whether this LOD originated from a standalone, artist-defined mesh.
	Model->bUseSmoothing = false; // Flag: construct/redigest itself with smoothing groups.

	unguard;
}



//
// Total memory footprint; done explicitly, because we need to distinguish 
// specifically between Lazy arrays vs render-time data.
//
INT USkeletalMesh::MemFootprint( UBOOL RenderDataOnly )
{ 
	guard( USkeletalMesh::MemFootprint );
	INT MemTotal = 0;

	// Count all the on-disk lazyarrays too if requested.
	if( !RenderDataOnly )
	{
		// Base lazyarrays.
		RawVerts.Load();
		RawNormals.Load();
		RawWedges.Load();
		RawFaces.Load();
		RawInfluences.Load();
		RawCollapseWedges.Load();
		RawFaceLevel.Load();

		MemTotal += RawVerts.Num()          * sizeof( FVector );
		MemTotal += RawNormals.Num()        * sizeof( FMeshNorm );
		MemTotal += RawWedges.Num()         * sizeof( FMeshWedge );
		MemTotal += RawFaces.Num()          * sizeof( VTriangle );
		MemTotal += RawInfluences.Num()     * sizeof( FVertInfluence );
		MemTotal += RawCollapseWedges.Num() * sizeof( _WORD );
		MemTotal += RawFaceLevel.Num()      * sizeof( _WORD );

		// Dynamic arrays themselves take up 12 bytes; lazy arrays take 24.
		MemTotal += 24 * 7;
	}

	// scion sz -
	// LOD Models
	//
	for( INT i=0; i< LODModels.Num(); i++)
	{
		MemTotal += LODFootprint( i, RenderDataOnly );
	}

	// scion sz -
	// ULodMesh.FaceLevel array was being loaded for rendering when it wasn't needed
	// redigesting meshes will remove this array. Added to stats just to confirm its removal.
	MemTotal += FaceLevel.Num() * sizeof( _WORD );

	// General non-lazyarray data.
	MemTotal += RefBasesInverse.Num() * sizeof( FCoords );
	MemTotal += TagAliases.Num() * sizeof( FName );
	MemTotal += TagNames.Num() * sizeof( FName );
	MemTotal += TagCoords.Num() * sizeof( FCoords );

	// Dynamic arrays (including obsolete empty ones)
	MemTotal += 12 * (4 +  8);

	return MemTotal; 

	unguard;
}

// 
// Footprint for one specific LOD.
//
INT USkeletalMesh::LODFootprint( INT LODIndex, UBOOL RenderDataOnly )
{    
	guard(USkeletalMesh::LODFootprint);

	if( LODIndex < 0 || LODIndex >= LODModels.Num() )
		return 0;

	INT MemTotal = 0;	
	FStaticLODModel* LODModel = &LODModels(LODIndex);

	if( !RenderDataOnly )
	{
		LODModel->Influences.Load();
		LODModel->Wedges.Load();
		LODModel->Faces.Load();
		LODModel->Points.Load();

		MemTotal += LODModel->Influences.Num() * sizeof(FVertInfluence);
		MemTotal += LODModel->Wedges.Num() * sizeof(FMeshWedge);
		MemTotal += LODModel->Faces.Num() * sizeof(FMeshFace);
		MemTotal += LODModel->Points.Num() * sizeof(FVector);

		// Dynamic arrays 
		MemTotal += 24 * 4;
	}

	if( LODModel->GPUMultiBoneSections.Num() )
	{
		// scion sz -
		// GPU streams/arrays mem footprint
		MemTotal += LODModel->GPUMultiBoneSections.Num() * sizeof( FSkelMeshSection );
		MemTotal += sizeof( FRawIndexBuffer );
		MemTotal += LODModel->GPUIndexBuffer.Indices.Num() * sizeof( _WORD );
		MemTotal += sizeof( FSkinGPUVertexStream );
		MemTotal += LODModel->GPUVertexStream.Vertices.Num() * sizeof( FSkinGPUVertex );
		MemTotal += LODModel->GPUBoneChunks.Num() * sizeof( FGPUBoneChunk );
	}
	else
	{
		MemTotal += LODModel->SkinningStream.Num() * sizeof( DWORD );
		MemTotal += LODModel->SmoothVerts.Num() * sizeof( FSkinPoint );
		MemTotal += LODModel->SmoothSections.Num() * sizeof( FSkelMeshSection );
		MemTotal += LODModel->RigidSections.Num() * sizeof( FSkelMeshSection );
		MemTotal += LODModel->ActiveBoneIndices.Num() * sizeof( _WORD );

		// Vertex and index buffers.
		MemTotal += sizeof( FRawIndexBuffer );
		MemTotal += sizeof( FRawIndexBuffer );
		MemTotal += sizeof( FSkinVertexStream );
		MemTotal += LODModel->SmoothIndexBuffer.Indices.Num() * sizeof( _WORD );
		MemTotal += LODModel->RigidIndexBuffer.Indices.Num() * sizeof( _WORD );
		MemTotal += LODModel->RigidVertexStream.Vertices.Num() * sizeof( FAnimMeshVertex );
	}

	// Additional variables..
	MemTotal += 7 * sizeof ( DWORD );

	// Dynamic arrays 
	MemTotal += 12 * 5;

	return MemTotal;

	unguard;
}

//
// Generate a LOD level; hark back to older LOD data as our source if necessary for backward compatibility.
// Note, 0th LOD level needs to be generated explicitly also.
// TODO: more options needed for creation...
//

#define MAXLODLEVELS 8

void USkeletalMesh::GenerateLodModel( INT LodLevel, FLOAT ReductionFactor, FLOAT DisplayFactor, INT MaxInfluences, UBOOL bSlowTaskAllowed )
{
	guard(USkeletalMesh::GenerateLodModel);

	if (( LodLevel > MAXLODLEVELS ) || (LodLevel < 0 ))
		return;
	
	RawVerts.Load();
	RawNormals.Load();
	RawWedges.Load();
	RawFaces.Load();
	RawInfluences.Load();
	RawCollapseWedges.Load();
	RawFaceLevel.Load();

	// Don't waste memory..
	RawNormals.Empty();

	// Special-case deletion of model.. works only if it's the highest level.
	if( ReductionFactor == 0.f )
	{
		if ( LodLevel == LODModels.Num()-1 )
		{
			LODModels.Remove(LodLevel); //#SKEL
		}
		return;		
	}	

	// Make sure requested slot is valid. Empty LOD levels are allowed.
	while( LodLevel >= LODModels.Num() )
	{
		new(LODModels)FStaticLODModel(); // 'new' necessary - constructor sets up cacheID and revision.
	}

	FStaticLODModel* NewModel = &LODModels( LodLevel );

	// Clear any existing data.
	LODModelErase( NewModel );
	
	NewModel->DisplayFactor = DisplayFactor;

	if( (ReductionFactor != 1.0f) )
	{
		// Check valid collapse data.
		if( RawCollapseWedges.Num() )
		{		
			INT VertexSubset = Points.Num(); 
			FLOAT TargetSubset    = Max( (FLOAT)LODMinVerts, (FLOAT)RawVerts.Num() * ReductionFactor  );
			VertexSubset          = Min( appRound(TargetSubset), RawVerts.Num() );
			
			// 'Render' NewModel data from the ref.pose into this NewModel.
			for( INT FaceIdx = 0; FaceIdx < RawFaces.Num(); FaceIdx++)
			{							
				if( RawFaceLevel( FaceIdx ) <= VertexSubset ) 
				{
					VTriangle OldFace = RawFaces(FaceIdx);
					VTriangle NewFace;				
					NewFace.WedgeIndex[0] = NewFace.WedgeIndex[1] = NewFace.WedgeIndex[2] = 0;

					for( INT v=0; v<3; v++ )
					{
						INT WedgeIdx = OldFace.WedgeIndex[v];
						INT LODVertIndex = RawWedges(WedgeIdx).iVertex;
						// Go down LOD wedge collapse list until below the current LOD vertex count.
						while( LODVertIndex >= VertexSubset && WedgeIdx )
						{
							WedgeIdx = RawCollapseWedges(WedgeIdx);
							// Paranoid safeguard - this must not crash even with questionable old mesh data.
							if( WedgeIdx < 0 || WedgeIdx >= RawWedges.Num() )
							{
								debugf(TEXT("WrongCollapse.. wedgeIdx %i"),WedgeIdx);
								WedgeIdx = 0;
							}							
							LODVertIndex = RawWedges(WedgeIdx).iVertex;
						}
						NewFace.WedgeIndex[v] = WedgeIdx;
					}		
					NewFace.MatIndex = OldFace.MatIndex;				
					
					if( ( NewFace.WedgeIndex[0] != NewFace.WedgeIndex[1] ) && ( NewFace.WedgeIndex[0] != NewFace.WedgeIndex[2] ) ) 
					{
						FMeshFace ModelFace;
						ModelFace.iWedge[0] = NewFace.WedgeIndex[0];
						ModelFace.iWedge[1] = NewFace.WedgeIndex[1];
						ModelFace.iWedge[2] = NewFace.WedgeIndex[2];
						ModelFace.MeshMaterialIndex = NewFace.MatIndex;
						NewModel->Faces.AddItem( ModelFace );
					}
				}
			}
		}
	}
	else // Special case 0th LOD - copy uncollapsed faces.
	{
		NewModel->Faces.Add( RawFaces.Num() );
		// Copy face information.
		for( INT f=0; f<RawFaces.Num(); f++)
		{
			NewModel->Faces(f).MeshMaterialIndex = RawFaces(f).MatIndex;
			for( INT i=0; i<3; i++)
			{
				NewModel->Faces(f).iWedge[i] = RawFaces(f).WedgeIndex[i];
			}			
		}
	}


	// Copy rest of information ( all will get properly culled in ModelDataResort. )

	// Copy Wedge information.
	NewModel->Wedges.Add(RawWedges.Num());
	for( INT i=0; i<RawWedges.Num(); i++)
	{
		NewModel->Wedges(i) = RawWedges(i);
	} 	

	NewModel->Points.Add(RawVerts.Num());	
	for( INT i=0; i<RawVerts.Num(); i++)
	{			
		NewModel->Points(i) = RawVerts(i);		
	}

	// Copy influences information.
	NewModel->Influences.Add( RawInfluences.Num());
	for( INT i=0; i<RawInfluences.Num(); i++)
	{
		NewModel->Influences(i) = RawInfluences(i);		
	}		

	// Good default (just enough to avoid multi-frame flicker).				
	NewModel->LODHysteresis= 0.02f; 

	// Set allowed max number of bone influences per vertex.
	NewModel->MaxInfluences = Min( Max(MaxInfluences,1),7 );

	// Sort data and create predigested skinning stream.
	FinalizeLodModel( NewModel, this, bSlowTaskAllowed );

	unguard;	
}


//
// Use a raw NewModel mesh's data as a lod level for this mesh. Requirements: compatible material indices, compatible skeleton.
//
void USkeletalMesh::InsertLodModel( INT LodLevel, USkeletalMesh* RawMesh, FLOAT DisplayFactor, UBOOL bSlowTaskAllowed )
{
	guard( USkeletalMesh::InsertLodModel );

	if (( LodLevel > MAXLODLEVELS ) || (LodLevel < 0 ))
		return;

	// Make sure requested slot is valid. Empty LOD levels are allowed.
	while( LodLevel >= LODModels.Num() )
	{
		new(LODModels)FStaticLODModel(); // 'new' necessary - constructor sets up cacheID and revision.
	}
	FStaticLODModel* NewModel = &LODModels( LodLevel );

	// 'Draw' mesh's full LOD into the (raw) structures - including (smooth) sections/materials, material indices assumed compatible.
	// Section/material info: inferred from face's material indices.

	RawMesh->RawVerts.Load();
	RawMesh->RawFaces.Load();
	RawMesh->RawWedges.Load();
	RawMesh->RawNormals.Load();		
	RawMesh->RawInfluences.Load();
	RawMesh->RawCollapseWedges.Load();
	RawMesh->RawFaceLevel.Load();
	
	if( !RawMesh->RawVerts.Num() || 
		!RawMesh->RawFaces.Num() || 
		!RawMesh->RawWedges.Num() || 
		!RawMesh->RawInfluences.Num() 
		)
		return; // Avoid invalid input.

	// Clean out any existing data.
	LODModelErase(NewModel);

	// Safety - ensure we don't run out of material slots with the new mesh's material indices.
	INT NewMaxMatIndex = 0;
	for( INT i=0; i<RawMesh->RawFaces.Num(); i++)
	{
		if( RawMesh->RawFaces(i).MatIndex <0 )
			RawMesh->RawFaces(i).MatIndex = 0;

		NewMaxMatIndex = Max( NewMaxMatIndex, (INT)RawMesh->RawFaces(i).MatIndex );
	}

	// Expand slots if necessary..
	while( MeshMaterials.Num() <= NewMaxMatIndex )
	{
		  MeshMaterials.AddZeroed();
	}

	// Copy raw data.
	NewModel->Points.Add( RawMesh->RawVerts.Num());
	for( INT i=0; i<RawMesh->RawVerts.Num(); i++)
	{
		NewModel->Points(i) = RawMesh->RawVerts(i);
	}

	NewModel->Wedges.Add( RawMesh->RawWedges.Num());
	for( INT i=0; i<RawMesh->RawWedges.Num(); i++)
	{
		NewModel->Wedges(i) = RawMesh->RawWedges(i);
	}

	NewModel->Influences.Add( RawMesh->RawInfluences.Num());
	for( INT i=0; i<RawMesh->RawInfluences.Num(); i++)
	{
		NewModel->Influences(i) = RawMesh->RawInfluences(i);
	}

	NewModel->Faces.Add( RawMesh->RawFaces.Num());
	for( INT i=0; i<RawMesh->RawFaces.Num(); i++)
	{
		FMeshFace ModelFace;
		ModelFace.iWedge[0] = RawMesh->RawFaces(i).WedgeIndex[0];
		ModelFace.iWedge[1] = RawMesh->RawFaces(i).WedgeIndex[1];
		ModelFace.iWedge[2] = RawMesh->RawFaces(i).WedgeIndex[2];
		ModelFace.MeshMaterialIndex = RawMesh->RawFaces(i).MatIndex;
		NewModel->Faces(i) = ModelFace;
	}			

	// Allow maximum number of influences; with explicitly imported LOD levels,
	// any reduction is assumed to have been done in the source art.
	NewModel->MaxInfluences = 7; 

	// Indicate this LOD is made with external data and not to be 
	// automatically updated with the auto-generated LOD sets.
	NewModel->bUniqueSubset = true;
	
	// Sort data and create predigested skinning stream.
	FinalizeLodModel( NewModel, this, true );

	unguard;
}


// 
// Recalculate normals for the smoothskinned vertices.
//
void RecalculateSmoothNormals( FStaticLODModel* NewLOD )
{
	guard( UnSkeletalTools_CalculateNormals );

	if( NewLOD->Faces.Num()==0 ) return;

	TArray <FVector> TempNormals;

	TempNormals.AddZeroed( NewLOD->SmoothVerts.Num() );
		
	// Go over triangles and create face normals (in 'actor' space) by using dot products and accumulate them into the vertices.
	for( INT i=0; i<NewLOD->Faces.Num(); i++ )
	{
		INT VIdx[3]; // Vertex indices.
		for( INT v=0; v<3; v++)
		{
			VIdx[v]= NewLOD->Wedges( NewLOD->Faces(i).iWedge[v] ).iVertex;
		}

		FVector FaceNormal = ( NewLOD->SmoothVerts( VIdx[0] ).Point - NewLOD->SmoothVerts( VIdx[1] ).Point ) 
			               ^ ( NewLOD->SmoothVerts( VIdx[2] ).Point - NewLOD->SmoothVerts( VIdx[0] ).Point );
		// Don't normalize at this point - the surface of the triangle is factored in due to the cross product and lends appropriate bias to the normal.
		// Accumulate into normals of all vertices that make up this face.
		TempNormals(VIdx[0]) += FaceNormal;
		TempNormals(VIdx[1]) += FaceNormal;
		TempNormals(VIdx[2]) += FaceNormal;
	}	

	// Store normalized per-vertex normal.
	for( INT i=0; i<TempNormals.Num(); i++)
	{   			
		TempNormals(i) = TempNormals(i) / appSqrt( TempNormals(i).SizeSquared() + 0.0001f);		
		// Pack and assign.
		NewLOD->SmoothVerts(i).Normal = FMeshNorm( appRound( 511.f+TempNormals(i).X * 511.f),
				  				 				   appRound( 511.f+TempNormals(i).Y * 511.f),
												   appRound( 511.f+TempNormals(i).Z * 511.f));
	}
	unguard;
}


//
// Calculate temporary normals for this LOD model only.
//
void RecalculateLODNormals( FStaticLODModel* NewLOD, TArray<FVector>& TempNormals )
{
	guard( UnSkeletalTools_CalculateNormals );

	if( NewLOD->Faces.Num()==0 ) return;

	TempNormals.Empty();
	TempNormals.AddZeroed( NewLOD->Points.Num() );

	// Go over triangles and create face normals (in 'actor' space) by using dot products and accumulate them into the vertices.
	for( INT i=0; i<NewLOD->Faces.Num(); i++ )
	{
		INT VIdx[3]; // Vertex indices.
		for( INT v=0; v<3; v++)
		{
			VIdx[v]= NewLOD->Wedges( NewLOD->Faces(i).iWedge[v] ).iVertex;
		}

		FVector FaceNormal = ( NewLOD->Points( VIdx[0] ) - NewLOD->Points( VIdx[1] ) ) ^ 
			                 ( NewLOD->Points( VIdx[2] ) - NewLOD->Points( VIdx[0] ) );
		// Don't normalize at this point - the surface of the triangle is factored in due to the cross product and lends appropriate bias to the normal.
		// Accumulate into normals of all vertices that make up this face.
		TempNormals(VIdx[0]) += FaceNormal;
		TempNormals(VIdx[1]) += FaceNormal;
		TempNormals(VIdx[2]) += FaceNormal;
	}	

	// Store normalized per-vertex normal.
	for( INT i=0; i<TempNormals.Num(); i++)
	{   			
		TempNormals(i) = TempNormals(i) / appSqrt( TempNormals(i).SizeSquared() + 0.0001f);				
	}
	unguard;
}

// Force limits N influences (hardware preprocessing)
#define BONEINFLUENCELIMIT (3) 

void USkeletalMesh::NormalizeInfluences( INT LodLevel )
{
	guard(USkeletalMesh::NormalizeInfluences);

/*
	// Influences match the 3D points, not the wedges.
	InfluenceIndex.Empty();
	InfluenceIndex.Add(Points.Num());
	
	//Mark as uninitialized.
	for( INT i=0; i<InfluenceIndex.Num(); i++ )
	{   		
		InfluenceIndex(i).InfIndex[0] = 0xFFFF;
		InfluenceIndex(i).InfIndex[1] = 0xFFFF;
		InfluenceIndex(i).InfIndex[2] = 0xFFFF;
		InfluenceIndex(i).InfIndex[3] = 0xFFFF;	

		InfluenceIndex(i).BoneIndex[0] = 0;
		InfluenceIndex(i).BoneIndex[1] = 0;
		InfluenceIndex(i).BoneIndex[2] = 0;
		InfluenceIndex(i).BoneIndex[3] = 0;	
	}

	// Initialize the Influence-index array (for each POINT / vertex , knows whick bones influence it with what weight).
	for( INT w=0; w< BoneWeightIdx.Num(); w++ )
	{
		INT Index  = BoneWeightIdx(w).WeightIndex;
		INT Number = BoneWeightIdx(w).Number;
		if( Number > 0 )
		{
			for( INT b=Index; b<(Index+Number); b++ )
			{
				INT VertIndex = BoneWeights(b).PointIndex;

				// if (VertIndex < FrameVerts )   // LOD check...
				// FLOAT Weight = (FLOAT)BoneWeights(b).BoneWeight * ( 1.f/65535.f );

				// Store known weights and influence index per vertex, up to 4.
				INT InfCounter = 0;
				while( ( InfluenceIndex(VertIndex).InfIndex[InfCounter] != 0xFFFF) && InfCounter < BONEINFLUENCELIMIT )
					InfCounter++;
				
				// Use first noninitialized slot.
				if( InfCounter < BONEINFLUENCELIMIT ) // < 4 to obey the Playstation limit...
				{
					InfluenceIndex(VertIndex).BoneIndex[InfCounter] = w ; // bone index.
					InfluenceIndex(VertIndex).InfIndex[InfCounter] = b ;  // boneweights/influence index
				}

				// 4 influence slots were already full ?
				if( InfCounter >= BONEINFLUENCELIMIT ) // >= 4? : Zero out this bone's influence if we ran out of slots.
				{
					BoneWeights(b).BoneWeight = 0; 
				}
			}
		}
	}
	
	//  Restriction of max influences per vertex;:make sure these all add up to 1 (65536)
	for( i=0; i<InfluenceIndex.Num(); i++)
	{
		DWORD TotalWeight = 0;
		INT WeightCount = 0;

		// Find total
		for( INT j=0; j<4; j++) 
		{
			INT Index = InfluenceIndex(i).InfIndex[j];
			if( Index !=0xFFFF )
			{
				TotalWeight += BoneWeights( Index ).BoneWeight;
				WeightCount++;
			}			
		}

		// Ensure the influences add up to 65535 ( with some tolerance... )
		if( (TotalWeight > 0xFFFF) || (TotalWeight < 0xFFF0) ) // Rescaling needed ?
		{			
			FLOAT WeightScale = 65535.f/((FLOAT)TotalWeight + 0.01f);
			for( INT j=0; j<WeightCount; j++)
			{
				INT Index = InfluenceIndex(i).InfIndex[j];				
				BoneWeights( Index ).BoneWeight = (INT) ( WeightScale * (FLOAT)BoneWeights( Index ).BoneWeight);
			}
		}
	}
	//debugf(TEXT("Generated %i InfIndices"),InfluenceIndex.Num());

*/
	unguard;
}


// Calculate normals - only for PS2.
void USkeletalMesh::CalculateNormals(TArray <FVector>& Normals, UBOOL Displace )
{
	guard(USkeletalMesh::CalculateNormals);

	// Already filled?
	if( Normals.Num() ) 
		return;

	TArray <FVector> TempNormals;
	TempNormals.AddZeroed(Points.Num());
	
	// Go over triangles and create face normals (in 'actor' space) by using dot products and accumulate them into the vertices.
	for( INT i=0; i<Faces.Num(); i++ )
	{
		INT V[3]; // Vertex indices.
		for( INT v=0; v<3; v++)
		{
			V[v]= Wedges(Faces(i).iWedge[v]).iVertex;
		}
		FVector FaceNormal = (Points(V[0])-Points(V[1])) ^ (Points(V[2])-Points(V[0]));

		// Don't normalize at this point - the surface of the triangle is factored in due to the cross product and
		// lends appropriate bias to the normal.		
		// Accumulate into normals of all vertices that make up this face.
		TempNormals(V[0]) += FaceNormal;
		TempNormals(V[1]) += FaceNormal;
		TempNormals(V[2]) += FaceNormal;
	}

	Normals.Add(Points.Num());

	// Store normalized per-vertex normal.
	for( INT i=0; i<Normals.Num(); i++)
	{   			
		FVector NewNormal = TempNormals(i) / appSqrt(TempNormals(i).SizeSquared() + 0.001f);
		// Store displaced point when software-skinning.
		// NOTE - Keep displacement relatively large, otherwise float accuracy problems cause shimmering
		// lighting despite the fact that DX8 auto-normalizes the final normal lengths in hardware.
		if( Displace ) NewNormal = ( NewNormal * 1.0f) + Points(i); 
		Normals(i) = NewNormal;
	}

	unguard;
}


//
// Retrieve raw data from mesh either from data on import, or from pre-existing older-version .UKX or even .U mesh data.
//

void USkeletalMesh::ReconstructRawMesh()
{
	guard(ReconstructRawMesh);

	UBOOL bVerbose = ( GIsEditor || GIsUCC );

	// Fast reconstruction of full (non-LOD) data into 'raw' arrays.	

	RawVerts.Load();	
	RawFaces.Load();
	RawWedges.Load();
	RawNormals.Load();
	RawInfluences.Load();
	RawCollapseWedges.Load();
	RawFaceLevel.Load();

	RawVerts.Empty();
	RawWedges.Empty();
	RawFaces.Empty();
	RawInfluences.Empty();
	RawCollapseWedges.Empty();
	RawFaceLevel.Empty();
	
	INT BlendsLimit = Min( MultiBlends.Num(), 7);
	// Unwind the raw weights; then, after NewPointIndexes has been filled, remap 'em all.
	for( INT m=0; m< BlendsLimit ; m++)
	{
		if( m==0) // Single influence special case. 
		{		
			INT WeightIdx = MultiBlends(m).WeightBase;
			for( INT v=0; v<MultiBlends(m).PointIndices.Num(); v++) // All points with a single influence.
			{
				FVertInfluence NewWeight;
				NewWeight.VertIndex =  MultiBlends(m).PointIndices(v);
				NewWeight.BoneIndex =  Weights(WeightIdx).BoneIndex;					
				NewWeight.Weight = 1.0f; // Special-case single weight...

				// Safeguard bone bounds.
				if( NewWeight.BoneIndex >= RefSkeleton.Num())
				{
					debugf(TEXT("Spurious bone index on single influence bone: %i weight %f "),NewWeight.BoneIndex,NewWeight.Weight);
					NewWeight.BoneIndex=0;
					NewWeight.Weight = 0.0f; 
				}			
				
				RawInfluences.AddItem( NewWeight );
				WeightIdx++;							
			}
		}
		else if( m>0 ) // Multiple influences (can be more than 4...)
		{								
			INT WeightIdx = MultiBlends(m).WeightBase;
			for( INT v=0; v<MultiBlends(m).PointIndices.Num(); v++)
			{					
				INT VIndex = MultiBlends(m).PointIndices(v);
				// Multiple influences for Vertex  VIndex:
				for(INT n=0; n<(m+1); n++) 
				{
					FVertInfluence NewWeight;
					NewWeight.VertIndex =  VIndex;
					NewWeight.BoneIndex =  Weights(WeightIdx).BoneIndex;
					NewWeight.Weight = ( 1.0f/65535.f )*(FLOAT)Weights(WeightIdx).BoneWeight;

					// Safeguard against spurious bone indices.
					if( NewWeight.BoneIndex >= RefSkeleton.Num())
					{
						debugf(TEXT("Spurious bone index on %i th influence bone: %i weight %f "),v,NewWeight.BoneIndex,NewWeight.Weight);
						NewWeight.BoneIndex=0;
						NewWeight.Weight = 0.0f; 
						
					}
					
					RawInfluences.AddItem( NewWeight );
					WeightIdx++;
				}												
			}
		}
	}

	/*
	if( bVerbose) 
	{
		debugf(TEXT("Raw influences: %i  extracted from [%i] multiblends."),RawInfluences.Num(),MultiBlends.Num());	
		for( INT m=0; m< MultiBlends.Num(); m++)
		{
			debugf(TEXT("[%i]-bone influences: %i "),m+1,MultiBlends(m).PointIndices.Num() );
		}		
	}
	*/

	TArray<INT> NewWedgeIndexes;
	NewWedgeIndexes.AddZeroed(Wedges.Num());
	TArray<INT> NewPointIndexes;
	NewPointIndexes.AddZeroed(Points.Num());

	// Accumulate new wedge and point arrays.
	for( INT f=0; f<Faces.Num(); f++)
	{
		VTriangle NewFace;
		NewFace.MatIndex = Faces(f).MeshMaterialIndex;
		NewFace.SmoothingGroups = 1; // Smoothing information is lost when retrieving raw mesh from digested USkeletalMesh.

		for( INT v=0; v<3; v++)
		{
			NewFace.WedgeIndex[v] = Faces(f).iWedge[v];
		}
		RawFaces.AddItem(NewFace);
	}

	for( INT i=0; i<Wedges.Num(); i++)
	{
		FMeshWedge NewWedge;
		NewWedge.TexUV = Wedges(i).TexUV;
		NewWedge.iVertex = Wedges(i).iVertex; 
		RawWedges.AddItem(NewWedge);		
	}

	for( INT i=0; i<Points.Num(); i++)
	{
		FVector NewVert;
		NewVert = Points(i);		
		RawVerts.AddItem( NewVert );
	}

	for( INT i=0; i<CollapseWedgeThus.Num(); i++)
	{
		RawCollapseWedges.AddItem( CollapseWedgeThus(i) );
	}

	for( INT i=0; i<FaceLevel.Num(); i++)
	{
		RawFaceLevel.AddItem( FaceLevel(i) );
	}

	if( bVerbose) debugf(TEXT(" Raw data: Faces %i  Wedges %i Verts %i Collapses %i OldFaces %i OldWedges %i OldPoints %i "), 
		RawFaces.Num(),
		RawWedges.Num(),
		RawVerts.Num(),
		RawCollapseWedges.Num(),
		Faces.Num(),
		Wedges.Num(),
		Points.Num()		
	);

	// Old data, no longer needed.	
	Faces.Empty();
	Wedges.Empty();
	Points.Empty();
	CollapseWedgeThus.Empty();
	MultiBlends.Empty();
	Weights.Empty();
	InfluenceIndex.Empty(); 
	Weights.Empty();
	// scion sz
	FaceLevel.Empty();

	unguard;
}



//
// Preprocess: generate vertex buffers and sections for hardware rendering.
// Note: some of these are better moved to PostLoad ?
// 

INT USkeletalMesh::RenderPreProcess()
{
	guard(USkeletalMesh::RenderPreProcess);

#ifdef __PSX2_EE__
//	void HardwarePreProcess( USkeletalMesh& Mesh );
//	HardwarePreProcess(*this);
#endif

	return 1;
	unguard;
}


//
// Warning: unlike vertex animation, these bounds only reflect the size 
// of the reference pose. Actual animations are linked in at runtime
// so we can never be exactly sure of the effective bounding box.
// 
// NOTE: We can adjust the bounding box interactively in the editor
// when using .UKX package based content.
//

void USkeletalMeshInstance::MeshBuildBounds()
{
	guard(USkeletalMeshInstance::MeshBuildBounds);

	USkeletalMesh* Mesh= (USkeletalMesh*)GetMesh();

	GWarn->StatusUpdatef( 0, 0, TEXT("Bounding skeletal mesh") );

	// We'll need to build the extends with reference-pose vertices.
	Mesh->RawVerts.Load();
	TArray<FVector>RawPoints;
	for( INT i=0; i<Mesh->RawVerts.Num(); i++)
	{
		RawPoints.AddItem(Mesh->RawVerts(i));
	}

	Mesh->BoundingBox    = FBox   ( &RawPoints(0), RawPoints.Num() );
	Mesh->BoundingSphere = FSphere( &RawPoints(0), RawPoints.Num() );

	FBox Temp = Mesh->BoundingBox;

	// Extend by 2 to compensate for the fact that the skeletal bounds reflect the reference pose only...
	// Skeletal meshes - don't have frames beyond 0.
	/*
	Mesh->BoundingBox.Min = 2.0f*Temp.Min-(Temp.Min + Temp.Max)*0.5f;
    Mesh->BoundingBox.Max = 2.0f*Temp.Max-(Temp.Min + Temp.Max)*0.5f;
	*/

	// Tighter: scale up by + S x the distance from the midpoint; conservative bounds for psx2 players/models.
	FVector MidMesh = 0.5f*(Temp.Min + Temp.Max);
	Mesh->BoundingBox.Min = Temp.Min + 1.0f*(Temp.Min - MidMesh);  // 0.5 ?
    Mesh->BoundingBox.Max = Temp.Max + 1.0f*(Temp.Max - MidMesh);  //

	// Tuck up the bottom. - this rarely extends lower than a reference pose's ( having its feet on the floor.)
	Mesh->BoundingBox.Min.Z = Temp.Min.Z + 0.1f*(Temp.Min.Z - MidMesh.Z); //#SKEL

	Mesh->BoundingSphere.W *= 1.4f;
	
	unguard;
}




void USkeletalMesh::FlipFaces()
{
	guard(USkeletalMesh::FlipFaces);
	// Physique flipping ('bones') doesn't always correctly flip the faces - so do it here specifically if needed.	
	for(INT i=0; i<Faces.Num(); i++)
	{
		// Change handedness on faces.
		INT Wedge0 = Faces(i).iWedge[0];
		Faces(i).iWedge[0]=Faces(i).iWedge[1];
		Faces(i).iWedge[1]=Wedge0;
	}
	debugf(TEXT("Flipping all faces for model %s"),GetName());
	unguard;
}



/*
void DetermineSections( USkeletalMesh* Mesh)
{
	//
	// Create 'material' sections, with proper max/min & material indices, but most of the rest will be set dynamically;
	//

	FMeshSection* Section = NULL;
	for(INT FaceIndex = 0;FaceIndex < Mesh->Faces.Num(); FaceIndex++)
	{
		FMeshFace ThisFace = Mesh->Faces(FaceIndex);
		INT  ThisMatIndex = ThisFace.MeshMaterialIndex;
			
		if( !Section || ( ThisMatIndex != Section->MaterialIndex ) )
		{
			// debugf(TEXT("New section %i for material index %i"),SmoothSections.Num(),ThisMatIndex );
		
			// Create a new static mesh section.
			Section = new( Mesh->SmoothSections) FMeshSection;
			Section->FirstFace = FaceIndex;
			Section->MaterialIndex = ThisFace.MeshMaterialIndex;
			// Indices filled later, dynamically
			// Section->FirstIndex = LODChunks[LodIdx].Indexbuffer.Indices.Num();
			Section->MinIndex = Mesh->Wedges.Num()-1; // Updated below.
			Section->MaxIndex = 0; // Updated below.
			Section->TotalFaces = 0;
		}

		// For each vertex in the triangle determine the minimum/maximum index;
		// Not compatible with LOD since extra vertex-buffer vertices have been created
		// for new collapsed faces.
		//for(INT VertexIndex = 0;VertexIndex < 3;VertexIndex++)
		//{
		//	INT ThisVertIdx = ThisFace.iWedge[VertexIndex]; 
		//	Update the current section's minimum/maximum index hints.
		//	Section->MinIndex = Min<_WORD>(Section->MinIndex, ThisVertIdx );
		//	Section->MaxIndex = Max<_WORD>(Section->MaxIndex, ThisVertIdx );
		//}
		
		// Update the section's triangle count.
		Section->TotalFaces++;
	}
	
	//
	// Min/MaxIndex: must encompass all of a material, not assuming any ordering.
	//
	for(INT s=0; s<Mesh->SmoothSections.Num(); s++)
	{
		INT ThisMat = Mesh->SmoothSections(s).MaterialIndex;
		INT NewMin =  Mesh->SmoothSections(s).MinIndex;
		INT NewMax =  Mesh->SmoothSections(s).MaxIndex;
		
		for(INT w=0; w<Mesh->Wedges.Num(); w++)
		{
			if( ThisMat == WedgeMaterials(w) )
			{
				if( NewMin > w )
					NewMin = w;
				if( NewMax < w )
					NewMax = w;				
			}
		}
		
		Mesh->SmoothSections(s).FirstIndex = 0;
		Mesh->SmoothSections(s).MinIndex   = NewMin;
		Mesh->SmoothSections(s).MaxIndex   = NewMax;
		Mesh->SmoothSections(s).TotalVerts = (NewMax-NewMin)+1;
	}
	
	// Section report. 
	
	//{for(INT s=0;s<Mesh->SmoothSections.Num();s++)
	//{
		//debugf(TEXT("Min, maxindex for section %i   %i  %i mat:%i "),(INT)s,(INT)Mesh->SmoothSections(s).MinIndex, (INT)Mesh->SmoothSections(s).MaxIndex,(INT)Mesh->SmoothSections(s).MaterialIndex);	//}}
	
}

*/


//
// Mesh Faces: Sort then anew, for a specific LOD, by 'stripping'
//
void CoherenceFaceSort( FStaticLODModel* Model )
{
	guard(CoherenceFaceSort);

	UBOOL bVerbose = ( GIsEditor || GIsUCC );

	//
	// Super simple but effective: sort faces in order of most-vertices (wedges)-in-common, and to 'snake' around
	// triangles that were already hooked up.
	//
	// Slow O(n^2) triangle search is sufficient for now..
	//
	// Material order will be conserved.
	//
	// Note: can be made more hardware-vertex-buffer friendly by also evaluating 2, 3+ faces back for matching vertices 
	//
	// 

	TArray<INT> FaceStrip;
	TArray<INT> FaceMark;
	TArray<INT> VertMark;
	VertMark.AddZeroed(Model->Wedges.Num());
	FaceMark.AddZeroed(Model->Faces.Num());
	INT NextFace = 0; 
	
	FaceStrip.AddItem(NextFace);

	{for( INT i=0; i<(Model->Faces.Num()-1); i++ )
	{	
		FaceMark(NextFace) = 1;	
		INT StripMaterial = Model->Faces(NextFace).MeshMaterialIndex;

		INT FaceWedges[3];
		for( INT f=0; f<3; f++)
		{
			FaceWedges[f] = Model->Faces(NextFace).iWedge[f];
			VertMark(FaceWedges[f])=1;
		}

		INT BestWedgeMatch = -1;		
		INT BestCommon = -1;

		// Any others with wedges in common ?
		for( INT c=0; c<Model->Faces.Num(); c++)
		{			
			if( FaceMark(c) == 0 ) // unlinked face?
			{
				INT VertsCommon = 0;
				for( INT m=0; m<3; m++)
				{
					for( INT n=0; n<3; n++)
					{
						// Wedges in common with previous triangle is cool
						if( FaceWedges[n] == Model->Faces(c).iWedge[m] )
							VertsCommon+=16;
						// Favoring touching triangles that have already been done, for 'flood'-filling.
						VertsCommon += VertMark( Model->Faces(c).iWedge[m] );
					}
				}
				if( Model->Faces(c).MeshMaterialIndex != StripMaterial)
				{
					VertsCommon = 0;
				}
				// Will settle for this face if we found nothing else at all.
				if( (VertsCommon > BestCommon) || (BestWedgeMatch == -1) )
				{
					BestCommon = VertsCommon;
					BestWedgeMatch = c;
				}
			}			
		}

		// Assign it.
		FaceStrip.AddItem(BestWedgeMatch);	
		NextFace = BestWedgeMatch;		
	}}

	// Apply the FaceStrip sort order.
	TArray<FMeshFace> NewFaces;

	for( INT i=0; i<Model->Faces.Num(); i++ )
	{
		NewFaces.AddItem(Model->Faces(FaceStrip(i)));
	}

	for( INT i=0; i<Model->Faces.Num(); i++ )
	{
		Model->Faces(i) = NewFaces(i);
	}

	NewFaces.Empty();
	FaceStrip.Empty();
	FaceMark.Empty();

	if( bVerbose ) debugf(TEXT(" Coherence sorting completed."));

	unguard;
}


//
// Resorts wedges and vertices in first-come order according to faces, and also gets rid of unused wedges/vertices.
//
void ModelDataReSort( FStaticLODModel* Model )
{
	guard( ModelDataReSort );

	UBOOL bVerbose = ( GIsEditor || GIsUCC );

	// Resort RawWedges and RawVerts according to RawFaces.

	TArray<INT> NewWedgeIndexes;
	NewWedgeIndexes.AddZeroed( Model->Wedges.Num());
	TArray<INT> NewPointIndexes;
	NewPointIndexes.AddZeroed( Model->Points.Num());

	TArray<FMeshWedge> NewWedges;	
	TArray<FVector> NewVerts;	

	INT WedgeCounter = 0;
	INT PointCounter = 0;

	// Accumulate new wedge and point arrays on the fly.
	for( INT f=0; f<Model->Faces.Num(); f++)
	{
		// Add vertices/wedges in Face order. 
		for( INT v=0; v<3; v++)
		{
			INT OldWIdx = Model->Faces(f).iWedge[v];

			// If 0, first use; otherwise, already in our new list at (Index-1).
			if (NewWedgeIndexes( OldWIdx ) == 0)
			{								
				NewWedgeIndexes( OldWIdx ) = ++WedgeCounter;
				// New wedge- but is it a new vertex ?
				INT OldVIdx = Model->Wedges(OldWIdx ).iVertex;				
				// If 0, first use; otherwise, already in our new list at (Index-1).
				if( NewPointIndexes( OldVIdx )== 0)
				{										
					NewPointIndexes( OldVIdx ) = ++PointCounter;
					// Add this new point.					
					FVector NewVert = Model->Points( OldVIdx );
					NewVerts.AddItem( NewVert );
				}
				// Add this new wedge.
				FMeshWedge NewWedge;
				NewWedge.TexUV = Model->Wedges( OldWIdx  ).TexUV;
				NewWedge.iVertex = NewPointIndexes(OldVIdx)-1; // New vertex index... not: NewWedgeIndexes( OldWIdx ) -1;
				NewWedges.AddItem( NewWedge);
			}		

			// Ensure wedge index is updated.
			Model->Faces(f).iWedge[v] = NewWedgeIndexes(OldWIdx)-1;
		}				
	}

	// Copy the new Wedges and Verts.
	Model->Wedges.Empty();
	for( INT i=0; i<NewWedges.Num(); i++)
	{
		new(Model->Wedges)FMeshWedge(NewWedges(i));
	}
	
	Model->Points.Empty();   
	for( INT i=0; i<NewVerts.Num(); i++)
	{	
		new(Model->Points)FVector( NewVerts(i));
	}
	
	// Remap weights' indices to the new vertices.
	for( INT i=0; i<Model->Influences.Num(); i++)
	{
		Model->Influences(i).VertIndex = NewPointIndexes( Model->Influences(i).VertIndex )-1;
	}

	// Sort the weights by Point index....
	appQsort( &Model->Influences(0), Model->Influences.Num(), sizeof(FVertInfluence), (QSORT_COMPARE)CompareBoneWeight );

	if( 1 ) // influence cleanup 
	{
		// Remove runs of more than allowed number of weights...
		INT LastVert = -1;
		INT InfRun = 0;
		for(  INT i=0; i<Model->Influences.Num(); i++ )
		{		
			if( ( LastVert != Model->Influences(i).VertIndex ) )
			{
				InfRun = 0;
			}
			InfRun++;

			LastVert = Model->Influences(i).VertIndex;
			
			// More than allowed number of runs - or if a vertex is out of bounds: mark weight for deletion.
			if( ( InfRun >  Model->MaxInfluences ) || ( LastVert >= Model->Points.Num()) )
			{
				Model->Influences(i).Weight = 0.0f;
			}
		}

		INT RemovedInfs = 0;
		#define MINWEIGHT (0.01f) // 1% is the minimal allowed influence....	
		// Delete.
		for( INT i=Model->Influences.Num()-1; i>=0; i-- )
		{
			if( Model->Influences(i).Weight < MINWEIGHT )
			{
				Model->Influences.Remove(i);
				RemovedInfs++;
			}
		}

		if( bVerbose) debugf(TEXT("Redundant influences removed for LOD : %i  Influences-per-vertex limit: %i "),RemovedInfs,Model->MaxInfluences); 
	}
	
	//
	// Renormalize all weights, regardless of removed influences.
	//

	if( bVerbose ) debugf(TEXT("Recalibrate %i weights...."),Model->Influences.Num());

	if( 1 ) 
	{		
		INT InfRun = 0;
		INT LastRun = 0;
		INT LastVert = -1;
		FLOAT TotalWeight = 0;
		for( INT i=0; i<Model->Influences.Num(); i++ )
		{
			if(LastVert != Model->Influences(i).VertIndex)
			{
				LastRun = InfRun;
				InfRun = 0;
				// Normalize the run...
				if( LastRun && (TotalWeight != 1.0f) )
				{				
					FLOAT WeightAdjust = 1.f/TotalWeight;
					for( int r=0; r<LastRun; r++)
					{
						Model->Influences(i-r-1).Weight *=WeightAdjust;						
					}
				}
				TotalWeight = 0.f;				
				LastVert = Model->Influences(i).VertIndex;							
			}
			InfRun++;
			TotalWeight+= Model->Influences(i).Weight;			
		}
	}
		
	unguard;
}


//
// Extract rigid parts from already digested (in UKX..) mesh.
//
// Issue a 'stream' redigest and make that use only the remaining/duplicated smooth-skinned verts.
// Can assume raw data already properly stored.
//

//
class TempRawChunk
{
	public:
	INT BoneIndex;
	INT Flag;
	TArray<FMeshFace> Faces;
};
class BoneInfluence
{
	public:
	INT Bone;
	FLOAT Weight;
};
class VertForces
{
	public:
	TArray<BoneInfluence> Influences; 
};

enum RigidFaceStates
{
	RFS_Raw            =  0,
	RFS_Smooth         = -1,
	RFS_Rigid          = -2,
	RFS_ConfirmedRigid = -3,
};

void USkeletalMesh::ExtractRigidParts( INT LODModelIndex, INT PartMinVerts, INT MaxParts, INT SectionMethod )
{
	guard(USkeletalMesh::ExtractRigidParts);	

	UBOOL bVerbose = ( GIsEditor || GIsUCC );

	FStaticLODModel& Model = this->LODModels(LODModelIndex);
	
	// Model raw data.
	Model.Wedges.Load();
	Model.Faces.Load();
	Model.Points.Load();
	Model.Influences.Load();

	Model.RigidVertexStream.Vertices.Empty();
	Model.RigidSections.Empty();	
	if( ++ Model.RigidVertexStream.Revision < 1 )
		Model.RigidVertexStream.Revision = 1;
	if( ++ Model.RigidIndexBuffer.Revision < 1 )
		Model.RigidIndexBuffer.Revision = 1;			

	//
	// Recalculate normals.
	//
	TArray<FVector> TempNormals;
	RecalculateLODNormals( &Model, TempNormals );
	
	//
    // Extracting rigid parts to individual pools of faces/verts.	
	//

	// Fill complete rigid vertex stream ( #TODO: CULL ?! This one duplicates ALL verts - not just rigid ones ... )
	for(INT v = 0; v < Model.Wedges.Num(); v++)
	{
		FAnimMeshVertex NewVert;		
		NewVert.U = Model.Wedges(v).TexUV.U;
		NewVert.V = Model.Wedges(v).TexUV.V;
		NewVert.Position = Model.Points( Model.Wedges(v).iVertex );
		NewVert.Normal   = TempNormals( Model.Wedges(v).iVertex ); 		
		NewVert.Normal.Normalize();
		Model.RigidVertexStream.Vertices.AddItem(NewVert);
	}

	// No software skinning callback for rigid segments.
	Model.RigidVertexStream.bStreamCallback = false; 

	//
	// Main bone index for each vertex; -1 if multiple indices...
	// TArray<INT> SingleBoneIndexes; 
	// SingleBoneIndexes.AddZeroed( Model.Points.Num() );
	//

	TArray<TempRawChunk> SolidChunks;
	TArray<INT> FaceMarkers;  // Flags faces' digestion status.
	FaceMarkers.AddZeroed( Model.Faces.Num() );

	// Accumulate bone forces for each vertex.
	TArray<VertForces> PointBones;
	PointBones.AddZeroed( Model.Points.Num() );	
	
	if( SectionMethod == MSM_SinglePiece )
	{
		// Single-piece-forcing: all bone influences become single bone 0 influences.
		for( INT i=0; i<PointBones.Num(); i++)
		{
			BoneInfluence NewBone;
			NewBone.Bone = 0;
			NewBone.Weight = 1.0f;
			PointBones(i).Influences.AddItem( NewBone );
		}
	}
	else
	{
		for( INT i=0; i<Model.Influences.Num(); i++)
		{
			BoneInfluence NewBone;
			NewBone.Bone = Model.Influences(i).BoneIndex;
			NewBone.Weight = Model.Influences(i).Weight;
			PointBones(Model.Influences(i).VertIndex).Influences.AddItem(NewBone);		
		}
	}

	//
	// TODO: optionally cull/sort influences for more tolerant to-rigid conversion...
	//


	// Mark only truly rigidizable tris.
	for( INT f=0; f< Model.Faces.Num(); f++)
	{
		INT CommonBone = -1;
		for( INT v=0; v<3; v++)
		{
			INT PointIdx = Model.Wedges( Model.Faces( f ).iWedge[v] ).iVertex;		
			// Only single influence of same bone for all three verts counts as rigid.
			if( PointBones( PointIdx ).Influences.Num()==1 )
			{
				INT VertBone = PointBones( PointIdx ).Influences(0).Bone;
				if( CommonBone == -1) 
					CommonBone = VertBone;
				else if( CommonBone != VertBone )
					CommonBone = -2; // Discard.
			}
			else
			{
				CommonBone = -2; //Discard.
			}			
		}		

		if( CommonBone >= 0 )		
			FaceMarkers(f) = CommonBone;
		else
			FaceMarkers(f) = RFS_Smooth; // Face stretches between mult. bones or is dangling with no influences.
	}

	//
	// Accumulate all truly rigid chunks - flood-fill style - until exhausted.
	//
	INT AllFaceCount = Model.Faces.Num();		
	INT TotalRigidFaces = 0;

	TArray<INT> NewChunkFaces;

	while( AllFaceCount )
	{	
		INT CurrentFace = -1;
		INT CommonBone = -1;
		INT CommonMaterial = -1;

		// Get first unprocessed face.
		for( INT f=0; f< Model.Faces.Num(); f++)
		{
			if( FaceMarkers(f) >= 0)
			{
				CurrentFace = f;
				break;
			}
		}

		// All eligible faces processed already ?
		if( CurrentFace < 0 )
			break;
		
		// Add first face..
		CommonBone = FaceMarkers( CurrentFace );
		CommonMaterial = Model.Faces( CurrentFace ).MeshMaterialIndex;
		FaceMarkers( CurrentFace ) = RFS_Rigid; // Mark as processed.
		NewChunkFaces.AddItem( CurrentFace);
		AllFaceCount--;		
		
		// Find ALL connecting faces with the common bone - add these to chunk.
		for( INT f=0; f< Model.Faces.Num(); f++)
		{
			if( FaceMarkers(f) >= 0)
			{
				if( FaceMarkers(f) == CommonBone &&  Model.Faces(f).MeshMaterialIndex == CommonMaterial )
				{
					FaceMarkers(f) = RFS_Rigid;
					NewChunkFaces.AddItem(f);					
					AllFaceCount--;
				}
			}
		}	
		
		// Save the newly accumulated chunk: only if: "MinFaces" faces or bigger.
		if( CommonBone >= 0 && ( NewChunkFaces.Num()*3 > PartMinVerts) ) // Kludgy vertex criterium.
		{			
			INT NewIdx = SolidChunks.AddZeroed();
			SolidChunks(NewIdx).BoneIndex = CommonBone;
			for( INT i=0; i< NewChunkFaces.Num(); i++)
			{
				// Mark new added face as succesfully rigid.
				FaceMarkers( NewChunkFaces(i) ) = RFS_ConfirmedRigid;

				FMeshFace NewFace;
				NewFace.iWedge[0] = Model.Faces(NewChunkFaces(i)).iWedge[0];
				NewFace.iWedge[1] = Model.Faces(NewChunkFaces(i)).iWedge[1]; 
				NewFace.iWedge[2] = Model.Faces(NewChunkFaces(i)).iWedge[2];
				NewFace.MeshMaterialIndex = Model.Faces( NewChunkFaces(i) ).MeshMaterialIndex;
				SolidChunks(NewIdx).Faces.AddItem( NewFace );				
			}

			TotalRigidFaces += NewChunkFaces.Num();
						
			if( bVerbose ) debugf(TEXT(" Solid chunk %i has %i faces, BONE: %i Meshmaterialindex %i "),SolidChunks.Num(),SolidChunks(NewIdx).Faces.Num(), SolidChunks(NewIdx).BoneIndex, SolidChunks(NewIdx).Faces(0).MeshMaterialIndex );
		}
		NewChunkFaces.Empty();
	}
	
	INT FaceIndex = 0;


	// Save the data before processing.
	TTempModel TempModel( &Model );	

	// Dump indices into index buffer and establish the sections.
	Model.RigidSections.AddZeroed(SolidChunks.Num());

	for( INT c=0; c<SolidChunks.Num(); c++)
	{
		FSkelMeshSection* Section = &Model.RigidSections(c);	
		Section->FirstIndex = Model.RigidIndexBuffer.Indices.Num();				
		Section->MinIndex = Model.RigidVertexStream.Vertices.Num();		
		Section->MaxIndex = 0;
		
		// Dump rigid indices
		for( INT f=0; f<SolidChunks(c).Faces.Num(); f++)
		{
			for( INT i=0; i<3; i++)
			{
				// Add indices
				_WORD ThisWedgeIdx = SolidChunks(c).Faces(f).iWedge[i];
				Model.RigidIndexBuffer.Indices.AddItem( ThisWedgeIdx );				

				// keep track of min/max wedge index for section.
				Section->MinIndex = Min( Section->MinIndex, ThisWedgeIdx );
				Section->MaxIndex = Max( Section->MaxIndex, ThisWedgeIdx );				
			}
		}
				
		// Find the min & maxindex (vertex index) for this section.				
		Section->TotalVerts = SolidChunks(c).Faces.Num()*3; // The total number of vertices // or the span  of vertices used ?!?!?
		Section->TotalWedges = 0; // Not relevant.
		Section->MaxInfluences = SolidChunks(c).BoneIndex;  // Re-using MaxInfluences as the bone index...
		Section->MaterialIndex = SolidChunks(c).Faces.Num() ? SolidChunks(c).Faces(0).MeshMaterialIndex : 0; // Material index.
		Section->FirstFace = FaceIndex;                       // First face (pointing into index buffer) for the section.
		Section->TotalFaces = SolidChunks(c).Faces.Num();     // Total primitives to render.		

		FaceIndex += SolidChunks(c).Faces.Num();
	}	

	// Digestion 'SectionMethod' options:
	//	MSM_SmoothOnly,    // Smooth (software transformed) sections only.
	//	MSM_RigidOnly,     // Only draw rigid parts, throw away anything that's not rigid.
	//	MSM_Mixed,         // Convert suitable mesh parts to rigid and draw remaining sections smoothly (software transformation).
	//  MSM_SinglePiece,   // Freeze all as a single static piece just as in the refpose.
	//	MSM_ForcedRigid,   // Force smooth parts into rigid along main bones etc.
			
	// Blank out smooth sections - ONLY if the conversion was complete AND not too many parts.
	if( SolidChunks.Num() <= MaxParts && (SolidChunks.Num() > 0) ) 
	{
		if( bVerbose ) debugf(TEXT("Rigid conversion: Total faces: %i  Rigid faces: %i  Total rigid sections: %i "),Model.Faces.Num(), TotalRigidFaces, SolidChunks.Num() );		

		// There are valid rigid sections, so discard previous smooth sections always; if we want any they'll be regenerated.
		Model.SmoothStreamWedges = 0;
		Model.SmoothSections.Empty();			
		Model.SmoothVerts.Empty();
		Model.SkinningStream.Empty();	
		Model.SmoothIndexBuffer.Indices.Empty();			
	
		if( SectionMethod == MSM_Mixed )
		{						
			if( bVerbose ) debugf(TEXT("LODModel total faces: %i"),Model.Faces.Num() );

			// Cull SmoothVerts/points/faces to only do the partial smooth mesh left.
			for( INT f=Model.Faces.Num()-1; f>=0; f--)
			{
				if( FaceMarkers(f) == RFS_ConfirmedRigid )
				{
					Model.Faces.Remove(f);
				}
			}						

			if( bVerbose ) debugf(TEXT("Smooth-skinned faces left after culling rigid faces: %i"),Model.Faces.Num() );

			if( Model.Faces.Num() ) // Any left? Create software sections.
			{			
				// Re-cohere the remaining faces as well as possible.
				CoherenceFaceSort( &Model );

				// Based on faces, cull all points and wedges, and reindexes everything accordingly.
				ModelDataReSort( &Model ); 

				// Create a new skinning stream with just the remaining smooth data.
				CreateSkinningStream( &Model, this );

				if( bVerbose ) debugf(TEXT("Size of partial smooth data after skinstream creation: Faces %i Verts %i Wedges %i Infs %i "),Model.Faces.Num(),Model.Points.Num(),Model.Wedges.Num(),Model.Influences.Num() );
			}
		}
	}
	else // Rigid part collection deemed too fragmented:
	{						
		Model.RigidVertexStream.Vertices.Empty();
		Model.RigidIndexBuffer.Indices.Empty();
		Model.RigidSections.Empty();	
	}

	// Restore lodmodel data regardless of any rigidparts processing/culling.
	TempModel.CopyInto( &Model );

	unguard;
}


//
// Sequence list order management. 
//
struct SortSeqTag
{
	FName SequenceName;
	INT   OriginalIndex;	
};


static QSORT_RETURN CDECL CompareSequenceName( const SortSeqTag* A, const SortSeqTag* B )
{
	FString NameA( *(A->SequenceName) );
	FString NameB( *(B->SequenceName) );

	     if( NameA > NameB ) return  1;
	else if( NameA < NameB ) return -1;
	else return  0;	
}

//
// Sort all sequences by sequence name - affects Moves and Animseqs.
//
INT UMeshAnimation::SortSequencesByName()
{
	// Sort FNAMES to obtain ReorderIndices, copy into new array, erase old array, copy into old array with new order.	
	
	if( (AnimSeqs.Num() < 2 ) || (AnimSeqs.Num() != Moves.Num() ) )
		return 0;	

	TArray<MotionChunk> TempMoves;
	TempMoves.AddZeroed(Moves.Num());
	TArray<FMeshAnimSeq> TempSeqData;
	TempSeqData.AddZeroed(Moves.Num());
	TArray<SortSeqTag> ReorderSeqTags;
	ReorderSeqTags.AddZeroed(Moves.Num());	
	for( INT n=0; n<Moves.Num(); n++)
	{
		// Setup sort data.
		ReorderSeqTags(n).SequenceName = AnimSeqs(n).Name;
		ReorderSeqTags(n).OriginalIndex = n;
				
		// Copy all data.				
                TempMoves(n).FlexTracks.AddZeroed( Moves(n).FlexTracks.Num()); 
		TempMoves(n) = Moves(n);
		Moves(n).Erase();      		
		// Store the animseqs.
		TempSeqData(n) = AnimSeqs(n);
		AnimSeqs(n).Erase();								
	}	

	appQsort( &ReorderSeqTags(0), ReorderSeqTags.Num(), sizeof(SortSeqTag), (QSORT_COMPARE) CompareSequenceName );

	for( INT n=0; n<Moves.Num(); n++)
	{				
		INT OrigIndex = ReorderSeqTags(n).OriginalIndex;
		Moves(n)   = TempMoves( OrigIndex );
		TempMoves( OrigIndex ).Erase();
		AnimSeqs(n)= TempSeqData( OrigIndex );
		TempSeqData( OrigIndex ).Erase();
	}

	for( INT n=0; n<TempMoves.Num(); n++)
	{
		TempMoves(n).Erase();
	}

	return Moves.Num();
}

//
// If indices are valid, moves around a single animation sequence.
//
INT UMeshAnimation::MoveSequenceByIndex( INT SourceIdx, INT DestIdx )
{
	if(( SourceIdx == DestIdx ) ||
	   ( SourceIdx < 0 || SourceIdx >= Moves.Num()) ||
	   ( DestIdx < 0   || DestIdx   >= Moves.Num()) ||
	   ( AnimSeqs.Num() != Moves.Num() ) )
		return -1;

	// Special (only practical) case:  move a sequence up or down. 
	// Just swap both the Moves and AnimSeqs.
	if( abs( SourceIdx-DestIdx) == 1 )
	{
		MotionChunk TempMoveDest;
		appMemzero( (void*)&TempMoveDest,sizeof(MotionChunk));		
		TempMoveDest = Moves(DestIdx);

		// Lazy arrays need to be loaded before they can be shuffled.
		
		//Moves( DestIdx ).RawTracks.Load();
		//Moves( SourceIdx ).RawTracks.Load();		

		Moves( DestIdx ).Erase();
		Moves( DestIdx ) = Moves( SourceIdx );		

		Moves( SourceIdx ).Erase();
		Moves( SourceIdx ) = TempMoveDest;  

		TempMoveDest.Erase();

		FMeshAnimSeq  TempSeqData = AnimSeqs(DestIdx);
		appMemzero( (void*)&TempSeqData,sizeof(FMeshAnimSeq));

		TempSeqData = AnimSeqs( DestIdx );

		AnimSeqs( DestIdx ).Erase();
		AnimSeqs( DestIdx ) = AnimSeqs( SourceIdx );

		AnimSeqs( SourceIdx ).Erase();
		AnimSeqs( SourceIdx ) = TempSeqData;

		TempSeqData.Erase();

		return DestIdx;
	}

	// TODO: Implement general case.
	// ...

	return -1;	
}




//
// UMeshAnimation and related animation processing code.
//

// Memory footprint 
INT UMeshAnimation::MemFootprint()
{
	guard(UMeshAnimation::MemFootprint);

	INT TotalMem = 0;
	for(INT i=0;i<Moves.Num();i++)
	{
		TotalMem += Moves(i).CalculateMemFootprint();
	}
	return TotalMem;
	unguard;
}

// Footprint for one particular sequence (if it can be found)
INT UMeshAnimation::SequenceMemFootprint( FName SeqName)
{
	guard(UMeshAnimation::SequenceMemFootprint);

	INT TotalMem = 0;
	INT SeqIdx= -1;

	// Find sequence.
	for( INT i=0; i<AnimSeqs.Num(); i++ )
	{
		if( SeqName == AnimSeqs(i).Name )
		{
			SeqIdx = i;
		}
	}
	if( SeqIdx >= 0 )
	{
		TotalMem = Moves(SeqIdx).CalculateMemFootprint(false);
	}	
	return TotalMem;
	unguard;
}

// Modify the root orientation/translation for a certain sequence in this UMeshAnimation
UBOOL UMeshAnimation::AdjustMovement( FName SeqName, FCoords AdjustCoords )
{
	guard(UMeshAnimation::AdjustMovement);
	MotionChunk* MoveData = NULL;
	for( INT i=0; i<AnimSeqs.Num(); i++ )
	{
		if( SeqName == AnimSeqs(i).Name )
			MoveData = &( Moves(i) );
	}	
	if( MoveData )
	{
		// Apply the AdjustCoordsto all the root's keys.
		for( INT i=0; i< MoveData->AnimTracks(0).KeyTime.Num(); i++)
		{
			// Add the offset, if applicable.
			if( AdjustCoords.Origin != FVector(0,0,0) )
				MoveData->AnimTracks(0).KeyPos(i) += AdjustCoords.Origin;

			// Add the rotation to the quaternion by 'appending' the quaternion...?			
			FQuat& MoveQuat = MoveData->AnimTracks(0).KeyQuat(i);
			if( MoveQuat.W != 0.f )
			{			
				FCoords OrigRot = FQuaternionCoords( MoveQuat);
				OrigRot *= AdjustCoords;
				MoveQuat = FCoordsQuaternion(OrigRot);
			}
		}
		return true;
	}
	return false;
	unguard;
}

// Conforminging - detect bone matches, re-order bones accordingly, patch missing ones in from the reference skeleton if available.								
void UMeshAnimation::ConformBones( UMeshAnimation* DestAnimObject, USkeletalMesh* ReferenceMesh )
{
	guard(UMeshAnimation::ConformBones)
	UBOOL BonesChanged = false;

	if(!DestAnimObject)
		return;

	if( DestAnimObject->RefBones.Num() != RefBones.Num() )
		BonesChanged = true;

	INT OriginalBoneCount = RefBones.Num();

	TArray <INT> BoneRemap;
	BoneRemap.AddZeroed( DestAnimObject->RefBones.Num());

	INT LinkedBoneCount = 0;
	for( INT b=0; b<BoneRemap.Num(); b++)
	{
		BoneRemap(b) = -1;
		for( INT j=0; j< RefBones.Num(); j++)
		{
			if( DestAnimObject->RefBones(b).Name == RefBones(j).Name )
			{
				BoneRemap(b) = j;
				LinkedBoneCount++;
				if( b != j )
				{
					BonesChanged = true;
				}
			}
		}				
	}

	if( LinkedBoneCount != RefBones.Num() )
		BonesChanged = true;

	// scion sz -
	// Any bones that exist in this->RefBones but not in DestAnimObject->RefBones
	// are considered to be new bones and they must be appended to the current ref skeleton
	//
	TArray<INT>			AppendBoneIdxs;		// Indices into RefBones for each new bone
	TArray<FNamedBone>  AppendBones;		// Copies of RefBones for each new bone
	for( INT b=0; b<RefBones.Num(); b++)
	{
		// try to find each bone in the DestAnimObject hierarchy
		UBOOL Found=0;
		for( INT j=0; j< DestAnimObject->RefBones.Num(); j++)
		{
			if( DestAnimObject->RefBones(j).Name == RefBones(b).Name )
			{
				Found=1;
				break;
			}
		}

		// if the bone was not found it is a new bone and should be appended to the hierarchy
		if( !Found )
		{
			AppendBoneIdxs.AddItem( b );
			AppendBones.AddItem( RefBones(b) );
		}
	}

	// scion sz -
	// if there are new bones then bones changed =true
	if( AppendBoneIdxs.Num() > 0 )
		BonesChanged = true;
		
	// Any difference in total bone numbers or non-perfect bonenames-match triggers animation reconstruction.
	if( !BonesChanged )
		return;

	// With newly mapped bones, recreate all animation tracks.
	TArray<MotionChunk> NewMoves;	

	// Remap Moves.Num() sequences.
	for( INT i=0; i< Moves.Num(); i++)
	{
		INT NewIdx = NewMoves.AddZeroed();
		MotionChunk* NewMove = &NewMoves(NewIdx);		

		NewMove->AnimTracks.AddZeroed( BoneRemap.Num() );

		NewMove->InternalVersion = Moves(i).InternalVersion;
		NewMove->RootSpeed3D = Moves(i).RootSpeed3D;
		NewMove->StartBone = Moves(i).StartBone;
		NewMove->TrackTime = Moves(i).TrackTime;
				
		// Fill all animtracks in new-bone order.
		for( INT b=0; b< NewMove->AnimTracks.Num(); b++)
		{
			INT SourceBoneIdx = BoneRemap(b);
			// Try get reference skeleton position/rotation if remapping was not possible.			
			if( SourceBoneIdx < 0 )
			{
				FQuat RefQuat = FQuat(0,0,0,0);
				// If the bone exists in our currently liked mesh, retrieve the angle/offset from it.
				if( ReferenceMesh )
				{
					INT RefBoneIdx = ((USkeletalMeshInstance*) ReferenceMesh->MeshGetInstance(NULL))->MatchRefBone( DestAnimObject->RefBones(b).Name );
					if( RefBoneIdx > 0 )
					{
						RefQuat = ReferenceMesh->RefSkeleton(RefBoneIdx).BonePos.Orientation;						
					}				
				}
				// Add first and only item - static track.
				NewMove->AnimTracks(b).KeyQuat.AddItem(RefQuat);
				NewMove->AnimTracks(b).KeyTime.AddItem( 0.0f );
				// scion sz -
				// removed position data since we can just use bone lengths
			}
			else
			{
				//Get the motion data for this track however many keys there are - from the source BoneRemap(b) bone.
				for( int m=0; m< Moves(i).AnimTracks( SourceBoneIdx ).KeyQuat.Num(); m++)
					NewMove->AnimTracks(b).KeyQuat.AddItem( Moves(i).AnimTracks( SourceBoneIdx ).KeyQuat(m) );				
				for( int m=0; m< Moves(i).AnimTracks( SourceBoneIdx ).KeyPos.Num(); m++)
					NewMove->AnimTracks(b).KeyPos.AddItem(  Moves(i).AnimTracks( SourceBoneIdx ).KeyPos(m) );				
				for( int m=0; m< Moves(i).AnimTracks( SourceBoneIdx ).KeyTime.Num(); m++)
					NewMove->AnimTracks(b).KeyTime.AddItem( Moves(i).AnimTracks( SourceBoneIdx ).KeyTime(m) );				
			}
		}

		// scion sz -
		// add animation tracks of new appended bones
		for( INT b=0; b< AppendBoneIdxs.Num(); b++)
		{
			INT SourceBoneIdx = AppendBoneIdxs(b);
			check( SourceBoneIdx >= 0 && TEXT("Invalid bone index"));

			INT TrackIndex = NewMove->AnimTracks.AddZeroed();
			AnalogTrack& AnimTrack = NewMove->AnimTracks(TrackIndex);

			// Get the motion data for this track however many keys there are - from the source BoneAppend(b) bone.
			for( int m=0; m< Moves(i).AnimTracks( SourceBoneIdx ).KeyQuat.Num(); m++)
				AnimTrack.KeyQuat.AddItem( Moves(i).AnimTracks( SourceBoneIdx ).KeyQuat(m) );				
			for( int m=0; m< Moves(i).AnimTracks( SourceBoneIdx ).KeyPos.Num(); m++)
				AnimTrack.KeyPos.AddItem(  Moves(i).AnimTracks( SourceBoneIdx ).KeyPos(m) );				
			for( int m=0; m< Moves(i).AnimTracks( SourceBoneIdx ).KeyTime.Num(); m++)
				AnimTrack.KeyTime.AddItem( Moves(i).AnimTracks( SourceBoneIdx ).KeyTime(m) );

		}
		
	}

	// Replace 'refbones'.
	RefBones.Empty();
	RefBones.Add( DestAnimObject->RefBones.Num());
	for( INT b=0; b< DestAnimObject->RefBones.Num(); b++)
	{
		RefBones(b)= DestAnimObject->RefBones(b);
	}

	// scion sz -
	// merge new bones from this animation set
	for( INT b=0; b< AppendBones.Num(); b++)
	{
		RefBones.AddItem( AppendBones(b) );
	}

	// Replace moves with 'newmoves'.
	if( NewMoves.Num() > 0 )
	{
		for( INT m=0; m< Moves.Num(); m++)
			Moves(m).Erase();

		Moves.Empty();
		Moves.AddZeroed( NewMoves.Num() );

		for( INT i=0; i< NewMoves.Num(); i++)
		{			
			Moves(i) = NewMoves(i);
			NewMoves(i).Erase();
			
			// BoneIndices (never used..)
			/*
			Moves(i).BoneIndices.Empty();
			Moves(i).BoneIndices.Add(RefBones.Num());
			for(INT b=0; b< RefBones.Num(); b++)
			{
				Moves(i).BoneIndices(b) = b;
			}
			*/
		}
		debugf(TEXT("Conformed %i animation sequence(s). Original bone count: %i conformed to %i bones."), NewMoves.Num(), OriginalBoneCount, RefBones.Num() );
	}	

	unguard;
}

/* scion ======================================================================
* ConformMotion
* Author: sz
* 
* Checks every Moves entry to see if it has a unique track for each bone.
* If it doesn't then a new entry is added.
*
* ============================================================================
*/
void UMeshAnimation::ConformMoves( UMeshAnimation* NewAnimObject, USkeletalMesh* ReferenceMesh )
{
	guard(UMeshAnimation::ConformMoves);
	
	for( INT m=0; m< Moves.Num(); m++)
	{
		MotionChunk& Motion = Moves(m);

		// motion data is in compressed flextrack form
		if( Motion.FlexTracks.Num() )
		{
			INT MissingTracks = NewAnimObject->RefBones.Num() - Motion.FlexTracks.Num();
			if( MissingTracks > 0 )
			{
				INT StartIdx = Motion.FlexTracks.AddZeroed( MissingTracks );
				for( INT b=StartIdx; b < Motion.FlexTracks.Num(); b++ )
				{
					// create a static flex track
					Motion.FlexTracks(b).AllocateTrack( ADT_Static );

					FQuat RefQuat = FQuat(0,0,0,0);
					if( ReferenceMesh )
					{
						// Try to use the orientation from the ReferenceMesh bind pose
						INT RefBoneIdx = ((USkeletalMeshInstance*) ReferenceMesh->MeshGetInstance(NULL))->MatchRefBone( NewAnimObject->RefBones(b).Name );
						if( RefBoneIdx > 0 )
						{
							RefQuat = ReferenceMesh->RefSkeleton(RefBoneIdx).BonePos.Orientation;
							RefQuat.Normalize();
							if( RefQuat.W < 0)
							{
								RefQuat.X = -RefQuat.X;
								RefQuat.Y = -RefQuat.Y;
								RefQuat.Z = -RefQuat.Z;
								RefQuat.W = -RefQuat.W;
							}
						}				
					}

					// store single orientation
					((FlexTrackStatic*)(Motion.FlexTracks(b).FlexTrackPtr))->Orientation = FQuatPacked( RefQuat );
				}
			}
		}
		// motion data is using raw analog data
		else
		{
			INT MissingTracks = NewAnimObject->RefBones.Num() - Motion.AnimTracks.Num();
			if( MissingTracks > 0 )
			{
				INT StartIdx = Motion.AnimTracks.AddZeroed( MissingTracks );
				for( INT b=StartIdx; b < Motion.AnimTracks.Num(); b++ )
				{
					FQuat RefQuat = FQuat(0,0,0,0);
					if( ReferenceMesh )
					{
						// Try to use the orientation from the ReferenceMesh bind pose
						INT RefBoneIdx = ((USkeletalMeshInstance*) ReferenceMesh->MeshGetInstance(NULL))->MatchRefBone( NewAnimObject->RefBones(b).Name );
						if( RefBoneIdx > 0 )
						{
							RefQuat = ReferenceMesh->RefSkeleton(RefBoneIdx).BonePos.Orientation;
						}				
					}
					// Add first and only item - static track.
					Motion.AnimTracks(b).KeyQuat.AddItem( RefQuat );
					Motion.AnimTracks(b).KeyTime.AddItem( 0.0f );
				}
			}
		}
	}

	unguard;
}

class ENGINE_API EStatClass
{
public:

	DWORD AllPosBytes;
	DWORD AllQuatBytes;
	DWORD AllMiscBytes;
	DWORD AllTimeBytes;

	EStatClass()
	{
		AllPosBytes = 0;
		AllQuatBytes = 0;
		AllMiscBytes = 0;
		AllTimeBytes = 0;		
	}


};

EStatClass EStats;

INT MotionChunk::CalculateMemFootprint( UBOOL RenderDataOnly )
{	
	guard(MotionChunk::CalculateMemFootprint);

	INT DumpStats = 0;

	//#ASKEL - animation stat code is temporary.

	// if( RenderDataOnly )
	//	DumpStats = 1;

	#define VERBOSEFOOTPRINT 0		
		
	INT TotalMem = 0;

	INT TotalQuatMem   = 0;
	INT TotalPosMem    = 0;
	INT TotalTimeMem   = 0;
	INT TotalATrackMem = 0;

	INT TotalFlexMem = 0;

	INT TimeNum = 0;
	INT PosNum  = 0;
	INT QuatNum = 0;	


	// Flextracks memory.
	if( FlexTracks.Num() )
	{
		for(INT f=0;f<FlexTracks.Num(); f++)
		{
			if( FlexTracks(f).FlexTrackPtr )
				TotalFlexMem += FlexTracks(f).FlexTrackPtr->FootPrint();
		}
		TotalMem += TotalFlexMem;

		return TotalMem;
	}



	TotalMem += TotalFlexMem;

	// Classic animtracks memory.
	for(INT j=0;j< AnimTracks.Num(); j++)
	{
		TotalMem += sizeof(DWORD); // Flags
		TotalMem += sizeof(FQuat)* AnimTracks(j).KeyQuat.Num();
		TotalMem += sizeof(FVector)* AnimTracks(j).KeyPos.Num();
		TotalMem += sizeof(FLOAT)* AnimTracks(j).KeyTime.Num();

		TimeNum += AnimTracks(j).KeyTime.Num();
		QuatNum += AnimTracks(j).KeyQuat.Num();
		PosNum +=  AnimTracks(j).KeyPos.Num();

		TotalQuatMem += sizeof(FQuat)* AnimTracks(j).KeyQuat.Num();
		TotalTimeMem += sizeof(FLOAT)* AnimTracks(j).KeyTime.Num();
		TotalPosMem += sizeof(FVector)* AnimTracks(j).KeyPos.Num();

		if(0)
		{
			// Dup specific track..#VSKEL
			if( AnimTracks(j).KeyPos.Num() > 1 )
			{			
				if( j==51 && AnimTracks(j).KeyPos.Num()==170 && AnimTracks(j).KeyPos.Num() == AnimTracks(j).KeyQuat.Num() )
				{
					debugf(TEXT(" Pos track for bone [%i], posvectors [%i], total time keys [%i] quat keys: (%i) lastkeytime(%f) "),j,AnimTracks(j).KeyPos.Num(),AnimTracks(j).KeyTime.Num(),AnimTracks(j).KeyQuat.Num(),
						AnimTracks(j).KeyTime(AnimTracks(j).KeyTime.Num()-1) );
					// dup ALL quats and pos for this track
					for(INT i=0; i<AnimTracks(j).KeyPos.Num(); i++)
					{
						debugf(TEXT(" Pos %6f %6f %6f  Quat %6f %6f %6f %6f "), 
							AnimTracks(j).KeyPos(i).X,
							AnimTracks(j).KeyPos(i).Y,
							AnimTracks(j).KeyPos(i).Z,
							AnimTracks(j).KeyQuat(i).X,
							AnimTracks(j).KeyQuat(i).Y,
							AnimTracks(j).KeyQuat(i).Z,
							AnimTracks(j).KeyQuat(i).W
							);
					}
				}

			}
		}

		//////////////////
		//if( AnimTracks(j) .KeyQuat.Num() == 170 )
		// Dump the 'jump'...

		if(0)
		{
			debugf(TEXT(" Last key time %f total fquats: %i "), AnimTracks(j).KeyTime(AnimTracks(j).KeyTime.Num()-1),AnimTracks(j).KeyQuat.Num() ); //

			if( AnimTracks(j).KeyTime(AnimTracks(j).KeyTime.Num()-1) == 66.0f ) //
			{
				debugf(TEXT("Quat track for bone[%i] Posvectors [%i] lastkeytime(%f) "),j,AnimTracks(j).KeyPos.Num(),AnimTracks(j).KeyTime(AnimTracks(j).KeyTime.Num()-1) );
				// dup ALL quats and pos for this track
				for(INT i=0; i<AnimTracks(j).KeyQuat.Num(); i++)
				{
					debugf(TEXT(" Quat %6f %6f %6f %6f "), 
						AnimTracks(j).KeyQuat(i).X,
						AnimTracks(j).KeyQuat(i).Y,
						AnimTracks(j).KeyQuat(i).Z,
						AnimTracks(j).KeyQuat(i).W
						);
				}
			}
		}

	}
	TotalMem += sizeof(FQuat)* RootTrack.KeyQuat.Num();
	TotalMem += sizeof(FVector)* RootTrack.KeyPos.Num();
	TotalMem += sizeof(FLOAT)* RootTrack.KeyTime.Num();

	TotalQuatMem +=sizeof(FQuat)* RootTrack.KeyQuat.Num();
	TotalTimeMem +=sizeof(FLOAT)* RootTrack.KeyTime.Num();
	TotalPosMem  +=sizeof(FVector)* RootTrack.KeyPos.Num();
	TotalATrackMem +=sizeof(AnalogTrack)*AnimTracks.Num();

	TotalMem += sizeof(AnalogTrack)* AnimTracks.Num();

	EStats.AllPosBytes+=TotalPosMem;
	EStats.AllQuatBytes+=TotalQuatMem;
	EStats.AllMiscBytes+=TotalATrackMem;
	EStats.AllTimeBytes+=TotalTimeMem;


	if( DumpStats )
	{
		//#VSKEL Dump separate Quat, Pos and Time byte counts
		// out to the log
		debugf(TEXT("==========================================="));
		debugf(TEXT(" Sizes for animation track with [%f] duration "),this->TrackTime);
		debugf(TEXT("                                [%i] poskey   bytes"),TotalPosMem);
		debugf(TEXT("                                [%i] quatkeys bytes"),TotalQuatMem);
		debugf(TEXT("                                [%i] timekeys bytes"),TotalTimeMem);
		debugf(TEXT("                                [%i] atrack bytes"),TotalATrackMem);
		debugf(TEXT(" bones (%i) Timekeys (%i) Quats (%i) Posss (%i) "),AnimTracks.Num(),TimeNum,QuatNum,PosNum);
		debugf(TEXT("   total mem: [%i]   "),TotalMem);
		debugf(TEXT("================================"));
		debugf(TEXT("                            [%i] pos all  bytes"),EStats.AllPosBytes);
		debugf(TEXT("                            [%i] quat all bytes"),EStats.AllQuatBytes);
		debugf(TEXT("                            [%i] time all bytes"),EStats.AllTimeBytes);
		debugf(TEXT("                            [%i] misc all bytes"),EStats.AllMiscBytes);
		debugf(TEXT("==========================================="));		
	}

	return TotalMem;
	unguard;
}




#ifndef _XBOX
enum EBoneMethod
{
	EBone_AllBones = 0,
	EBone_CreateSubset = 1 ,	
};

//
// Add a new GPU stream section. Processes the desired face subset, and put only the affected wedges, indexes, bones into a new section.
//
void AddNewGPUStreamSection( FStaticLODModel* LODModel, UMeshInstance* MInst, TArray<INT>& ChunkFaces, INT BoneMethod, INT MaterialIndex )
{	
#if !defined(_XBOX) && !defined(XBOX_ASSET_FORMAT)
	USkeletalMesh* SkelMesh = (USkeletalMesh*)(MInst->GetMesh());
	
	// Temp boneindices to collect only relevant bones for this chunk.
	TArray<INT> TempChunkBones;
	TArray<INT> TempBonesRemap;
	TempBonesRemap.AddZeroed( SkelMesh->RefSkeleton.Num() );

	// Remaps wedge indices to indices into the fresh GPUVertexStream.
	TArray<INT> WedgeRemapper; 
	WedgeRemapper.Add(  LODModel->Wedges.Num() );
	for( INT w=0; w<WedgeRemapper.Num(); w++)
		WedgeRemapper(w) = -1;

	// Temp faces to store the new indices..
	TArray<FMeshFace> NewTempFaces;
	NewTempFaces.Add( ChunkFaces.Num() );

   // Accumulate (unique) wedges from given faces.
	TArray<INT> ChunkWedges;
	for( INT f=0; f< ChunkFaces.Num(); f++)
	{
		for( INT w=0; w<3; w++)
		{						
			// Newfaces contains index into local-only wedges.
			NewTempFaces( f ).iWedge[w] = ChunkWedges.AddUniqueItem(  LODModel->Faces(  ChunkFaces(f)  ).iWedge[w] );
		}
	}
	
	// 
	// Turn chunk's wedges into vertex stream.
	//
	INT CurrentNewWedges = ChunkWedges.Num();
	TArray<INT> ProcessedVertexMarkers;
	ProcessedVertexMarkers.Add(LODModel->SmoothVerts.Num());
	for( INT i=0; i< ProcessedVertexMarkers.Num(); i++)
	{
		ProcessedVertexMarkers(i) = -1;
	}

	INT FirstNewGPUVert = LODModel->GPUVertexStream.Vertices.Num();

	for( INT t=0; t< ChunkWedges.Num(); t++)
	{			
		INT WedgeIdx = ChunkWedges(t);	
		INT PointIndex = LODModel->Wedges( WedgeIdx ).iVertex;

		FSkinGPUVertex NewGPUVert; 

		// This particular wedge already stream-ized before  ?
		if( ProcessedVertexMarkers(PointIndex) != -1 )
		{
			// Copy 3d point already hooked up with influences, and replace the UV with the one on this wedge.
			NewGPUVert		= LODModel->GPUVertexStream.Vertices( ProcessedVertexMarkers(PointIndex) );  //#ERROR - mixed-method meshes crash here.
			NewGPUVert.U	= LODModel->Wedges(WedgeIdx).TexUV.U;
			NewGPUVert.V	= LODModel->Wedges(WedgeIdx).TexUV.V;

			LODModel->GPUVertexStream.Vertices.AddItem( NewGPUVert );
		}
		else
		{
			// Accumulate weights for each vertex.
		
			// SLOW search... over all influences for our vertex.
			INT InfluenceCount = 0;
			INT InfluenceStart = 0;
			INT LookIdx = InfluenceStart;
			while( ( LODModel->Influences(LookIdx).VertIndex != PointIndex ) && ( LookIdx < LODModel->Influences.Num() ) )
			{
				LookIdx++;
			}			
			InfluenceStart = LookIdx;
			// All influences for a particular vertex are guaranteed contiguous.
			while( ( LODModel->Influences(LookIdx).VertIndex == PointIndex ) && ( LookIdx < LODModel->Influences.Num() ) )
			{			
				InfluenceCount++;
				LookIdx++;
			}			

			TArray<FVertInfluence> Weights; // Temp weights for this vertex.
			// Process the ones found.
			for( INT i=0; i<InfluenceCount; i++)
			{	
				FVertInfluence NewInf;
				INT BoneIndex = LODModel->Influences(InfluenceStart+i).BoneIndex;
				//	Safety check (esp. for when importing non-compatible meshes as static LOD models.)
				if( BoneIndex >= SkelMesh->RefSkeleton.Num() )
					BoneIndex = 0;
				
				NewInf.Weight = LODModel->Influences( InfluenceStart+i ).Weight;
				NewInf.BoneIndex = BoneIndex;
					
				Weights.AddItem( NewInf );
				//debugf(TEXT(" Retrieved weight for point %i  weight %f  bone %i "), PointIndex, NewInf.Weight, NewInf.BoneIndex); 			
			}

			// Assumes influences are properly sorted from high to low already.
			INT WeightLimit = 4;
			if( Weights.Num() == 0 )
			{
				debugf(TEXT("Warning: vertex %i does not have any bone influences in the GPU stream"),PointIndex );
			}

			for( INT w=0; w< 4;  w++)
			{
				if(  w < Min( WeightLimit, Weights.Num())  )  
				{
					NewGPUVert.Weight[w] =(FLOAT)  Weights(w).Weight  ; // Needs floating point weight
					NewGPUVert.Index[w] =	(FLOAT) (Weights(w).BoneIndex * 3); // Must be *3 for vshader direct-indexing.

					// Tally bone usage if subset extraction was requested.
					if(  BoneMethod == EBone_CreateSubset )
					{
						TempBonesRemap( Weights(w).BoneIndex ) =  TempChunkBones.AddUniqueItem( Weights(w).BoneIndex );
					}
				}
				else 
				{
					NewGPUVert.Weight[w] = 0.0f;;
					NewGPUVert.Index[w] =	0.0f;
				}		
			}	
			
			NewGPUVert.Position = LODModel->SmoothVerts( PointIndex ).Point;   //Points(PointIndex);
			NewGPUVert.Normal = LODModel->SmoothVerts( PointIndex ).Normal.Vector();
			NewGPUVert.U = LODModel->Wedges( WedgeIdx ).TexUV.U;
			NewGPUVert.V = LODModel->Wedges( WedgeIdx ).TexUV.V;

			// scion sz - normalize weights so they always add up to 1.0
            FLOAT fWeightTotal = NewGPUVert.Weight[0] + NewGPUVert.Weight[1] + NewGPUVert.Weight[2] + NewGPUVert.Weight[3];
			if( fWeightTotal > 0.f )
			{
				NewGPUVert.Weight[0] /= fWeightTotal;
				NewGPUVert.Weight[1] /= fWeightTotal;
				NewGPUVert.Weight[2] /= fWeightTotal;
				NewGPUVert.Weight[3] /= fWeightTotal;
			}

			ProcessedVertexMarkers( PointIndex ) = LODModel->GPUVertexStream.Vertices.Num();// Store pointer to mark wedge as processed.			
			LODModel->GPUVertexStream.Vertices.AddItem( NewGPUVert );
			
		}	// All 4 weights	.
	}	//For all unique wedges.
	
	INT LastNewGPUVert = LODModel->GPUVertexStream.Vertices.Num() - 1;

	// _If_ bone SUBSET usage was requested, remap all the  bone indices in the GPU stream.
	if( BoneMethod == EBone_CreateSubset )
	{
		for( INT g=FirstNewGPUVert; g<= LastNewGPUVert; g++)
		{
			for( INT w=0; w<4; w++)
			{				
				INT OldBone = appRound( LODModel->GPUVertexStream.Vertices(g).Index[w] / 3.0f ) ;
				LODModel->GPUVertexStream.Vertices(g).Index[w] =  3.0f  * (FLOAT)TempBonesRemap( OldBone );
			}
		}
	}

	// Now fill the section info, index buffer, and chunk's bones.
	INT CurrentBoneSubset = LODModel->GPUBoneChunks.AddZeroed(1);  // Always at least allocate a new chunk array regardless of BoneMethod.
	LODModel->GPUBoneChunks( CurrentBoneSubset ).FirstBoneSlot = 0;

	// Only if using a subset was allowed - otherwise, let subset remain zero-elements which tells the renderer to upload the full set of bones and not change the set.
	if( BoneMethod == EBone_CreateSubset )
	{
		for( INT b=0; b<TempChunkBones.Num(); b++)
		{
			LODModel->GPUBoneChunks( CurrentBoneSubset).ChunkBoneIndices.AddItem( TempChunkBones(b) );
			// debugf(TEXT(" ## Newly added tempchunk bone to slot [%i] is %i "),b,TempChunkBones(b)); 
		}
	}
	
	// Fill the Index buffer to draw the faces.
	INT CurrentSectionFirstIndexIndex = LODModel->GPUIndexBuffer.Indices.Num();
	INT CurrentSectionMinVertexIndex = MAXINT;
	INT CurrentSectionMaxVertexIndex = 0;

	// Put proper stream indices for each face into index buffer.
	for( INT f=0; f<ChunkFaces.Num(); f++)
	{	
		for( INT t=0; t<3; t++)
		{	
			// Must remap - will point into the current section's new stretch !!   // ( RCurrentSectionMinVertexIndex  + NewTempFaces(f).iWedge[t] )  );
			INT NewVertIdx =   NewTempFaces(f).iWedge[t] + FirstNewGPUVert;			
			LODModel->GPUIndexBuffer.Indices.AddItem( NewVertIdx  );
			CurrentSectionMinVertexIndex = Min( CurrentSectionMinVertexIndex, NewVertIdx );
			CurrentSectionMaxVertexIndex = Max( CurrentSectionMaxVertexIndex, NewVertIdx );			
		}
	}	

	//
	// Finally, fill in the new section information.	 
    //
	INT NewSectionIdx = LODModel->GPUMultiBoneSections.AddZeroed(1);
	FSkelMeshSection& NewSection = LODModel->GPUMultiBoneSections( NewSectionIdx );

	NewSection.FirstIndex = CurrentSectionFirstIndexIndex;  // The offset of the first INDEX index in the index buffer. 
	NewSection.MinIndex  =  CurrentSectionMinVertexIndex; //The smallest vertex index used by this section  [used in DrawPrimitive]
	NewSection.MaxIndex = CurrentSectionMaxVertexIndex;   //The largest vertex index used by this section. [used in DrawPrimitive]
	NewSection.TotalVerts =  CurrentNewWedges; //  // The total number of vertices 
	NewSection.TotalWedges =  CurrentNewWedges; //
	NewSection.MaxInfluences = 4; // // Largest number of per-vertex bone influences	
	NewSection.TotalFaces =  ChunkFaces.Num();//  [used in DrawPrimitive]
	NewSection.MaterialIndex = MaterialIndex; //	
	NewSection.ShaderIndex = MaterialIndex; //	
	NewSection.FirstFace = CurrentSectionFirstIndexIndex / 3; // First face for this section.
#endif
}

struct FaceDigester
{
	TArray<INT> BoneInfs;
	TArray<INT> NeighborFaces;
	INT					 SingleStepInfluences;  	
};

struct PointDigester
{
	TArray<INT> BoneInfs;
	TArray<FLOAT> Weights;
};

struct BoneDigester
{
	TArray<INT> FaceInfs;
	INT SharedBones;
};


//
// Construct the  GPU-specific  stream/sections/indexbuffers for this LOD model. 
//
void FStaticLODModel::ConstructGPUStream( UMeshInstance* MInst, INT MaxBonesPerChunk )
{
#ifndef XBOX_ASSET_FORMAT
	// Construct the mesh from the 'raw' data. 
	Influences.Load();
	Wedges.Load();
	Faces.Load();
	Points.Load();

	USkeletalMesh* SkelMesh = (USkeletalMesh*)(MInst->GetMesh());
	GPUVertexStream.MeshInstance = MInst;
	GPUVertexStream.AnimMesh = SkelMesh;
	GPUVertexStream.bStreamCallback = false;

	//debugf(TEXT(" Converting skeletal GPU vertex stream for [%s]  lod level %i -done. verts: %i "),SkelMesh->GetName(),Lod,SkelMesh->LODModels(CurrentLODLevel).GPUVertexStream.Vertices.Num() ); 

	// Delete any GPU data for this LODModel if it already exists:
	GPUMultiBoneSections.Empty();
	GPUIndexBuffer.Indices.Empty();
	GPUVertexStream.Vertices.Empty();
	GPUBoneChunks.Empty();

	//
	// Total bones smaller than the upper limit ? Add entire mesh as one chunk and we're done....
	//	
	if( SkelMesh->RefSkeleton.Num() <= MaxBonesPerChunk )
	{
		// Convert per section.  		
		for(  INT SectionIndex=0; SectionIndex < SmoothSections.Num(); SectionIndex++ )
		{
			// Prepare all of this mesh section's faces to become a GPUstream section.
			FSkelMeshSection* ThisMeshSection = &( SmoothSections(SectionIndex) );			
			TArray<INT> SubStreamFaces; // Face indices for a sub stream.		
			for( INT f=0; f<ThisMeshSection->TotalFaces; f++)
			{
				SubStreamFaces.AddItem(  ThisMeshSection->FirstFace + f  );
			}
			AddNewGPUStreamSection( this, MInst,  SubStreamFaces, EBone_AllBones,  ThisMeshSection->MaterialIndex  );			// EBone_CreateSubset ... EBone_AllBones						
		}

		// Important - if left at 0, will crash renderer.
		 GPUIndexBuffer.Revision = 1;  
		 GPUVertexStream.Revision = 1; 

		 return;
	}

	//
	// *  Determine how to best subdivide a mesh into chunks which obey the MaxBonesPerChunk -   then hand those off to AddNewGPUStreamSection.
	//
	// Steps:  - Determine which (material) sections exceed the bone limit
	//              - Split up those sections.
	//              - Feed all these in original order  to the AddNewGPUStreamSection.
	//

	// Store bone influences in per-point format for fast processing.
	TArray<PointDigester> PointInfos;
	PointInfos.AddZeroed( Points.Num() );
	for( INT i=0; i<Influences.Num(); i++)
	{
		INT PointIdx = Influences(i).VertIndex;
		PointInfos( PointIdx).BoneInfs.AddItem( Influences(i).BoneIndex );
		PointInfos( PointIdx).Weights.AddItem( Influences(i).Weight );
	}

    //
	// Main section loop where we either digest the entire section into the GPU stream, or break it down first.
	//
	for(  INT SectionIndex=0; SectionIndex < SmoothSections.Num(); SectionIndex++ )
	{

		FSkelMeshSection* ThisMeshSection = &( SmoothSections(SectionIndex) );
		// First, determine the bone requirements of the current chunk.
		TArray<INT> RequiredBoneSet;
		for( INT f=0; f< ThisMeshSection->TotalFaces; f++ )
		{
			for( INT i=0; i<3; i++)
			{
				PointDigester* PointInf = & PointInfos(  Wedges( Faces(ThisMeshSection->FirstFace + f). iWedge[i] ).iVertex  );
				for( INT b=0; b< PointInf->BoneInfs.Num(); b++)
				{
					RequiredBoneSet.AddUniqueItem(  PointInf->BoneInfs(b) );
				}
			}			
		}
		
		//debugf(TEXT("Required bone set for section %i  totals:  %i bones."),SectionIndex,RequiredBoneSet.Num() ); 

		if( RequiredBoneSet.Num() <= MaxBonesPerChunk )
		{
			// Prepare all of this mesh section's faces to become a GPUstream section.			
			TArray<INT> SubStreamFaces; // Face indices for a sub stream.		
			for( INT f=0; f<ThisMeshSection->TotalFaces; f++)
			{
				SubStreamFaces.AddItem(  ThisMeshSection->FirstFace + f   );
			}				
			 AddNewGPUStreamSection( this, MInst,  SubStreamFaces, EBone_CreateSubset,  ThisMeshSection->MaterialIndex  );			// EBone_CreateSubset ... EBone_AllBones						
		}
		else 
		{
			//
			// Determine how to split up this section in smaller subsets of faces !
			// 
			// Simplest approaches:
			//   Start gnawing at one arbitrary face (won't have more than 3x4 = 12  influences)  and keep choosing from the adjacent
			//    ones the one adding the least 'new' influences and most existing influences;  when the limit is reached, 'hack' out this peace and GPU-stream it.
			// 

			// First, accumulate all bone  influences for each face.
			TArray<INT> MainStreamFaces;
			TArray<FaceDigester> FaceData;
			TArray<BoneDigester> BoneData;
			FaceData.AddZeroed( ThisMeshSection->TotalFaces );		
			BoneData.AddZeroed( SkelMesh->RefSkeleton.Num() );
			for( INT f=0; f< FaceData.Num(); f++ )
			{
				MainStreamFaces.AddItem( f ); // Simple array of all faces to nibble subsections from.
				for( INT i=0; i<3; i++)
				{
					PointDigester* PointInf = & PointInfos(  Wedges( Faces( ThisMeshSection->FirstFace + f ). iWedge[i] ).iVertex  );
					for( INT b=0; b< PointInf->BoneInfs.Num(); b++)
					{
						// All  arrays use faces/indices *relative*  to FirstFace.
						INT Bone =  PointInf->BoneInfs(b) ;
						FaceData(f).BoneInfs.AddUniqueItem( Bone );
						BoneData( Bone ).FaceInfs.AddUniqueItem( f ); 
					}
				}				
			}

			//
			// Main loop from which we spit out bonelimit-compliant subsections.
			//			
			while (  MainStreamFaces.Num() )
			{
				TArray<INT> SubStreamFaces;
				TArray<INT> CombinedBoneInfluences;				
				
				INT FirstFace = MainStreamFaces(0);
				SubStreamFaces.AddItem( FirstFace );
				MainStreamFaces.RemoveItem( FirstFace );
				
				INT TryThisFaceIndex = 0;
				// Concatenate faces while keeping count of total influences.
				INT EligibleCounter = MainStreamFaces.Num();

				// We're hoping that faces have already been ordered with maximal face-coherence, so we won't need to do any connectivity searches...
				while( EligibleCounter > 0 )
				{					
					INT CurrentFace = MainStreamFaces( TryThisFaceIndex );
					EligibleCounter--;					
					TArray<INT> TryCombine;
					TryCombine = CombinedBoneInfluences; // Copies all...
					
					// Test combination
					for ( INT c=0; c< FaceData(  CurrentFace  ).BoneInfs.Num(); c++)
					{
						TryCombine.AddUniqueItem(  FaceData( CurrentFace ).BoneInfs(c) );
					}
					// Safe to add face ?
					if( TryCombine.Num() <= MaxBonesPerChunk ) 
					{
						// Make the new array 'final'.
						CombinedBoneInfluences = TryCombine;
						SubStreamFaces.AddItem( ThisMeshSection->FirstFace + CurrentFace ); // SubStreamFaces deals in real face indices..
						MainStreamFaces.RemoveItem( CurrentFace );
						// TryThisFaceIndex remains the same - points at NEXT  face if available.
					}
					else
					{
						TryThisFaceIndex ++; // Skip the face that added too many influences.
					}
				}					
				//debugf(TEXT("Adding new subsection from section %i, with total %i boneinfluences and %i faces "),SectionIndex, CombinedBoneInfluences.Num(), SubStreamFaces.Num() );	 
				AddNewGPUStreamSection(this, MInst, SubStreamFaces, EBone_CreateSubset, ThisMeshSection->MaterialIndex );				
			}
		}// Hacking-off-subsections part.
	} // Section loop.

	// Important - if left at 0, will crash renderer.
	GPUIndexBuffer.Revision = 1;  
	GPUVertexStream.Revision = 1; 

	// scion sz -
	// Delete all smooth-skinning sections, buffers and streams
	// this is done when USkeletalMesh is serialized on XBOX only since
	// we want these streams to be around when rendering on PC so we
	// must still save them in the package

	//
	//           TODO:
	//              - Currently conversion doesn't properly work (or crash) when the input mesh has any parts other than Smooth Sections.
	//              - A postprocessing step could determine whether consecutive sections can combine their bone upload requirements, and if necessary for that, remap some bone-indices in sctions of the vertex streams that were already constructed.
	//              - With that, sections should share vertices ! It's currently wasteful, each section duplicates any verts it needs.  There will be plenty of sections differing in material but NOT in bone usage, for the typical character mesh....	
	//
#endif
}
#else //_XBOX
void FStaticLODModel::ConstructGPUStream( UMeshInstance* MInst, INT MaxBonesPerChunk ) {}
#endif 