/*=============================================================================
	UnSparkEmitter.cpp: Unreal Spark Emitter
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Daniel Vogel
=============================================================================*/ 

#include "EnginePrivate.h"

/*-----------------------------------------------------------------------------
	USparkEmitter.
-----------------------------------------------------------------------------*/

//!! TODO: TimeBeforeVisible?!

IMPLEMENT_CLASS(USparkEmitter);

void USparkEmitter::PostEditChange()
{
	Super::PostEditChange();
	CleanUp();
	Initialize( MaxParticles );
}

// Initializes the Emitter.
void USparkEmitter::Initialize( INT InMaxParticles )
{
	guard(USparkEmitter::Initialize);
	Super::Initialize( InMaxParticles );
	NumSegments = (INT)Max( 2.f, LineSegmentsRange.GetMax() );
	VerticesPerParticle		= 2 * NumSegments;
	IndicesPerParticle		= 2 * NumSegments;
	PrimitivesPerParticle	= NumSegments;
	SparkData.Add( InMaxParticles );
	SparkData.Shrink();
	
	unguard;
}

// Update particles.
INT USparkEmitter::UpdateParticles( FLOAT DeltaTime )
{
	guard(USparkEmitter::UpdateParticles);
	INT Value	= Super::UpdateParticles( DeltaTime );
	BoundingBox = BoundingBox.ExpandBy( MaxSizeScale );
	return Value;
	unguard;
}

// CleanUp.
void USparkEmitter::CleanUp()
{
	guard(USparkEmitter::CleanUp);
	Super::CleanUp();
	SparkData.Empty();
	unguard;
}

void USparkEmitter::SpawnParticle( INT Index, FLOAT SpawnTime, INT Flags, INT SpawnFlags, const FVector& LocalLocationOffset )
{
	guard(USparkEmitter::SpawnParticle);
	Super::SpawnParticle( Index, SpawnTime, Flags, SpawnFlags, LocalLocationOffset );
	// Get the particle that was just spawned
	FParticle* Particle = (FParticle*)Particles(Index);
	check(Particle && "Failed to spawn particle");
	// Now update the corresponding spark data
	SparkData(Index).TimeBeforeVisible	 = TimeBeforeVisibleRange.GetRand();
	SparkData(Index).TimeBetweenSegments = TimeBetweenSegmentsRange.GetRand(); 
	SparkData(Index).StartLocation		 = Particle->Location;
	SparkData(Index).StartVelocity		 = Particle->Velocity;
	unguard;
}

struct FSparkVertex
{
	FVector	Position;
	FColor	Diffuse;
	FLOAT	U,
			V;

	static INT GetComponents(FVertexComponent* OutComponents)
	{
		OutComponents[0].Type = CT_Float3;
		OutComponents[0].Function = FVF_Position;
		OutComponents[1].Type = CT_Color;
		OutComponents[1].Function = FVF_Diffuse;
		OutComponents[2].Type = CT_Float2;
		OutComponents[2].Function = FVF_TexCoord0;

		return 3;
	}
};

class FSparkVertexStream : public FParticleVertexStream<FSparkVertex>
{
public:

	USparkEmitter*				Emitter;
	INT							NumParticles;

	// Constructor.
	FSparkVertexStream(USparkEmitter* InEmitter,INT InNumParticles)
	{
		Emitter = InEmitter;
		NumParticles = InNumParticles;
	}

	virtual INT GetSize()
	{
		return NumParticles * Emitter->VerticesPerParticle * sizeof(FSparkVertex);
	}

	// Generates the spark vertices procedurally.
	virtual void GetStreamData(void* Dest)
	{
#ifndef _XBOX
		Emitter->FillVertexBuffer( (FSparkVertex**) &Dest, NumParticles );
#endif
	}
};

// Fills vertex buffer with data - called indirectly.
INT USparkEmitter::FillVertexBuffer( FSparkVertex** pVertices, INT NumParticles )
{
	FSparkVertex*	Vertex = (FSparkVertex*) *pVertices;
	FLOAT			RNumSegments = 1.0f / NumSegments;

	//scion jg -- We are building these verts directly into the push buffer
#ifdef _XBOX
	DWORD* pdwBuff = (DWORD*) *pVertices;
	// Encode the primitive type and begin command into the push buffer
	*pdwBuff++ = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END,1);
	*pdwBuff++ = PT_LineList;
	// The number of DWORDs each vert takes
	const DWORD dwDwordsPerVert = sizeof(FSparkVertex) / sizeof(DWORD);
	// This is the number of DWORDs we encode in a given inline array push
	// NOTE: It must break on primitive alignment
	const DWORD dwMaxInlineArray = ( D3DPUSH_MAX_COUNT / (dwDwordsPerVert*VerticesPerParticle) ) * dwDwordsPerVert*VerticesPerParticle;
	// Determines how many DWORDs we've pushed into the push buffer
	DWORD dwDwordsEncoded = 0;
	// Get the pointer of where we'll start building verts
	Vertex = (FSparkVertex*)pdwBuff;
#endif //_XBOX

	for(INT ParticleIndex = 0;ParticleIndex < ActiveParticles;ParticleIndex++)
	{
		FParticle*	Particle = (FParticle*)Particles(ParticleIndex);

		if(Particle == NULL || !(Particle->Flags & PTF_Active))
			continue;

#ifdef _XBOX
		// Check to see if we need to encode a inline array push
		if (dwDwordsEncoded % dwMaxInlineArray == 0)
		{
			// Figure out how many dwords we'll need for each particle
			DWORD dwNumThisBatch = Min<DWORD>(VerticesPerParticle * NumParticles * dwDwordsPerVert,dwMaxInlineArray);
			// Build a new batch of verts to be processed
			*pdwBuff++ = D3DPUSH_ENCODE(
				D3DPUSH_NOINCREMENT_FLAG | D3DPUSH_INLINE_ARRAY,
				dwNumThisBatch);
			// Make the vertex be at the correct position in the push buffer
			Vertex = (FSparkVertex*)pdwBuff;
		}
#endif //_XBOX

		for(INT SegmentIndex = 0;SegmentIndex < NumSegments;SegmentIndex++)
		{
			FLOAT	t		 = Max( 0.f, Particle->Time - SegmentIndex * SparkData(ParticleIndex).TimeBetweenSegments );
			FVector	Location = SparkData(ParticleIndex).StartLocation + SparkData(ParticleIndex).StartVelocity*t + Acceleration*t*t;

			t				 = Max( 0.f, Particle->Time - (SegmentIndex+1) * SparkData(ParticleIndex).TimeBetweenSegments );
			FVector	Location2= SparkData(ParticleIndex).StartLocation + SparkData(ParticleIndex).StartVelocity*t + Acceleration*t*t;
			FColor  Color    = Particle->Color.RenderColor();

			Vertex->Position	= Location;
			Vertex->Diffuse		= Color;
			Vertex->U			= 0.f;
			Vertex->V			= RNumSegments * SegmentIndex;
			Vertex++;

#ifdef _XBOX
			// Keep the push buffer pointer in synch
			pdwBuff += dwDwordsPerVert;
			// Update the number of dwords encoded in the push buffer
			dwDwordsEncoded += dwDwordsPerVert;
#endif //_XBOX

			Vertex->Position	= Location2;
			Vertex->Diffuse		= Color;
			Vertex->U			= 1.f;
			Vertex->V			= RNumSegments * SegmentIndex;
			Vertex++;

#ifdef _XBOX
			// Keep the push buffer pointer in synch
			pdwBuff += dwDwordsPerVert;
			// Update the number of dwords encoded in the push buffer
			dwDwordsEncoded += dwDwordsPerVert;
#endif //_XBOX

		}

		// keep track of number of particles processed
		NumParticles--;
	}

#ifdef _XBOX
	*pdwBuff++ = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END,1);
	*pdwBuff++ = 0;	
	*pVertices = (FSparkVertex*)pdwBuff;
#endif //_XBOX

	return 0;
}

// Renders all active particles
INT USparkEmitter::RenderParticles(FDynamicActor* DynActor,FLevelSceneNode* SceneNode,TList<FDynamicLight*>* Lights,FRenderInterface* RI)
{
	guard(USparkEmitter::RenderParticles);

	UParticleEmitter::RenderParticles( DynActor, SceneNode, Lights, RI );

	if ( Texture == NULL )
		return 0;

	INT RenderedParticles = 0;

	// Render the particles.
	if ( ActiveParticles > 0 )
	{
		// Calculate the number of active particles.
		for(INT ParticleIndex = 0;ParticleIndex < ActiveParticles;ParticleIndex++)
		{
			FParticle* Particle = (FParticle*)Particles(ParticleIndex);

			if(Particle != NULL && Particle->Flags & PTF_Active)
				RenderedParticles++;
		}

		// Bail out if nothing to render.
		if (!RenderedParticles)
			return 0;

		// Create the dynamic vertex stream and index buffer.
		FSparkVertexStream SparkVertices(this,RenderedParticles);

		// Rotate & translate if needed.
		if ( CoordinateSystem == PTCS_Relative )
		{
			FMatrix	LocalToWorld = FRotationMatrix(Owner->Rotation) * FTranslationMatrix(Owner->Location + Owner->PrePivot + ((Owner->Base && Owner->AttachmentBone == NAME_None) ? Owner->Base->PrePivot : FVector(0,0,0)) );

			RI->SetTransform(TT_LocalToWorld,LocalToWorld);
		} 
		else 
		{
			RI->SetTransform(TT_LocalToWorld,FTranslationMatrix(Owner->PrePivot + ((Owner->Base && Owner->AttachmentBone == NAME_None) ? Owner->Base->PrePivot : FVector(0,0,0)) ));
		}

		// Set texture and blending.
		Owner->ParticleMaterial->ParticleBlending			= DrawStyle;
		if( SceneNode->Viewport->Actor->RendMap == REN_LightingOnly )
			Owner->ParticleMaterial->BitmapMaterial			= NULL;
		else
			Owner->ParticleMaterial->BitmapMaterial			= Texture;
		Owner->ParticleMaterial->BlendBetweenSubdivisions	= 0;
		Owner->ParticleMaterial->RenderTwoSided				= 1;
		Owner->ParticleMaterial->UseTFactor					= 0;
		Owner->ParticleMaterial->AlphaTest					= AlphaTest;
		Owner->ParticleMaterial->AlphaRef					= AlphaRef;
		Owner->ParticleMaterial->AcceptsProjectors			= AcceptsProjectors;
		Owner->ParticleMaterial->ZTest						= ZTest;
		Owner->ParticleMaterial->ZWrite						= ZWrite;
		Owner->ParticleMaterial->Wireframe					= SceneNode->Viewport->IsWire();

		RI->EnableLighting(0,1);
		RI->SetMaterial(Owner->ParticleMaterial);

#ifdef _XBOX
		
		//scion jg -- We are building the verts directly into the pushbuffer
		// so no indices are needed
		RI->SetIndexBuffer(NULL,0);
		// Figure out how big of a push buffer we'll need. We 5 DWORDs for the
		// basic push buffer command, plus the size of the particles and any
		// extra inline array commands
		DWORD dwParticleSize = sizeof(FSparkVertex) * VerticesPerParticle * RenderedParticles;
		DWORD dwSizeNeeded = 5 * sizeof(DWORD) + dwParticleSize +			
			( dwParticleSize / (D3DPUSH_MAX_COUNT*sizeof(DWORD)) ) * sizeof(DWORD);
		BEGIN_D3D_EVENT(BDE)(PIX_PARTICLE,TEXT("%s"),GetName());
		// Get a chunk of push buffer to use
		DWORD* pdwPushBuffer = RI->BeginPush(dwSizeNeeded,0,&SparkVertices);
		// Build the lines into the push buffer
		FillVertexBuffer( (FSparkVertex**)&pdwPushBuffer, RenderedParticles );
		// We are done so, let go of the push buffer
		RI->EndPush(pdwPushBuffer);
		END_D3D_EVENT;
#else
		// Set the particle vertex stream and index buffer.
		// The particle vertices aren't actually generated until now.
		INT	BaseVertexIndex = RI->SetDynamicStream(0,&SparkVertices);

		RI->SetIndexBuffer(NULL,0);

		// Draw the particles.
		RI->DrawPrimitive(
			PT_LineList,
			BaseVertexIndex,
			RenderedParticles * PrimitivesPerParticle
			);
#endif


		
	}

	return RenderedParticles;

	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

