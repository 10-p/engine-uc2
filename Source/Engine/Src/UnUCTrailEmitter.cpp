/*=============================================================================
	UCTrailEmitter -- Laurent
	Tron - like trails...
	Based on non finished code by Daniel Vogel... 
		...and continued by Laurent Delayen
=============================================================================*/ 

#include "EnginePrivate.h"

/*-----------------------------------------------------------------------------
	UUCTrailEmitter.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(UUCTrailEmitter);

// linear interpolation
FLOAT FLinterp(FLOAT percent, FLOAT A, FLOAT B)
{
	return A + percent*(B-A);
}

// vector linear interpolation
void VLinterp(FLOAT percent, const FVector& V1, const FVector& V2, FVector& Result)
{
	Result.X = FLinterp( percent, V1.X, V2.X );
	Result.Y = FLinterp( percent, V1.Y, V2.Y );
	Result.Z = FLinterp( percent, V1.Z, V2.Z );
}

inline FColor UUCTrailEmitter::SetInitialPointColor(FColor ParticleColor)
{
	if ( TrailShadeType == PTTST_RandomStatic )
	{
		FLOAT PointOpacity = appFrand();

		if ( DrawStyle == PTDS_AlphaBlend ||
			DrawStyle == PTDS_Modulated ||
			DrawStyle == PTDS_AlphaModulate_MightNotFogCorrectly)
		{
			ParticleColor.A = ParticleColor.A * PointOpacity;
		}
		else
		{
			ParticleColor.R = ParticleColor.R * PointOpacity;
			ParticleColor.G = ParticleColor.G * PointOpacity;
			ParticleColor.B = ParticleColor.B * PointOpacity;
		}
	}

	return ParticleColor;
}

void UUCTrailEmitter::PostEditChange()
{
	Super::PostEditChange();
	MaxPointsPerTrail	= Max(3, MaxPointsPerTrail);
	PointLifeTime		= Max(0.f, PointLifeTime);
	DistanceThreshold	= Max(2.f, DistanceThreshold);


	CleanUp();
	Initialize( MaxParticles );
}

// scion sz -
void UUCTrailEmitter::AttenMaxParticles( FLOAT AttenFactor )
{
	guard(UUCTrailEmitter::AttenMaxParticles);

	MaxPointsPerTrail	= Max( 3, appCeil( (FLOAT)MaxPointsPerTrail*AttenFactor ) );
	PointLifeTime		= Max( 0.f, PointLifeTime*AttenFactor );

	Super::AttenMaxParticles( AttenFactor );

	unguard;
}

// Initializes the Emitter.
void UUCTrailEmitter::Initialize( INT InMaxParticles )
{
	guard(UUCTrailEmitter::Initialize);

	Super::Initialize( InMaxParticles );
	TrailData.Add( InMaxParticles * MaxPointsPerTrail );
	TrailInfo.Add( InMaxParticles );
	TrailData.Shrink();
	TrailInfo.Shrink();	

	unguard;
}

// CleanUp.
void UUCTrailEmitter::CleanUp()
{
	guard(UUCTrailEmitter::CleanUp);
	Super::CleanUp();
	TrailData.Empty();
	TrailInfo.Empty();
	unguard;
}

// Spawn particle.
void UUCTrailEmitter::SpawnParticle( INT Index, FLOAT SpawnTime, INT Flags, INT SpawnFlags, const FVector& LocalLocationOffset )
{
	guard(UUCTrailEmitter::SpawnParticle);

	Super::SpawnParticle( Index, SpawnTime, Flags, SpawnFlags, LocalLocationOffset );

	FParticle* Particle = (FParticle*)Particles(Index);
	check(Particle && "Failed to spawn particle");
	FVector	PointLocation	= Particle->Location;

	// Laurent -- trail's location...
	if ( Owner )
	{
		if ( TrailLocation == PTTL_FollowEmitter )
		{
			// Add emitter's velocity to particle
			TrailInfo(Index).LastEmitterLocation = Owner->Location;
		}
	}
	
	TrailInfo(Index).TrailIndex			= 1;
	TrailInfo(Index).NumPoints			= 2;
	TrailInfo(Index).LastLocation		= PointLocation;
	
	TrailData(Index * MaxPointsPerTrail + 0).Location	= PointLocation;
	TrailData(Index * MaxPointsPerTrail + 0).Color		= SetInitialPointColor(Particle->Color);
	TrailData(Index * MaxPointsPerTrail + 0).Size		= Particle->Size.X;
	TrailData(Index * MaxPointsPerTrail + 0).Time		= Owner->Level->TimeSeconds + PointLifeTime; // Laurent -- point lifetime

	TrailData(Index * MaxPointsPerTrail + 1).Location	= PointLocation;
	TrailData(Index * MaxPointsPerTrail + 1).Color		= SetInitialPointColor(Particle->Color);
	TrailData(Index * MaxPointsPerTrail + 1).Size		= Particle->Size.X;
	TrailData(Index * MaxPointsPerTrail + 1).Time		= Owner->Level->TimeSeconds + PointLifeTime; // Laurent -- point lifetime
	unguard;
}

// Update particles.
INT UUCTrailEmitter::UpdateParticles( FLOAT DeltaTime )
{
	guard(UUCTrailEmitter::UpdateParticles);
	INT Value	= Super::UpdateParticles( DeltaTime );

	// scion sz - make sure bounding box actually fits the particles
	for( INT PartIdx=0; PartIdx < Particles.Num(); PartIdx++ )
	{
		FParticle* Particle = (FParticle*)Particles(PartIdx);
		if( Particle != NULL && 
			Particle->Flags & PTF_Active )
		{
			FUCParticleTrailInfo& TrailInfoEntry = TrailInfo(PartIdx);
			INT TrailIndex = TrailInfoEntry.TrailIndex;

			for( INT Idx=0; Idx < TrailInfoEntry.NumPoints; Idx++ )
			{
				FUCParticleTrailData& DataPt = TrailData( PartIdx * MaxPointsPerTrail + TrailIndex );
				if( --TrailIndex < 0 ) {
					TrailIndex += MaxPointsPerTrail;
				}
				BoundingBox += DataPt.Location;
			}
		}
	}
	BoundingBox = BoundingBox.ExpandBy( MaxSizeScale );
	return Value;
	unguard;
}

void UUCTrailEmitter::UpdateParticle( FLOAT DeltaTime, INT Index )
{
	FParticle* Particle		= (FParticle*)Particles(Index);
	check(Particle && "Attempt to update NULL particle");
	INT TrailIndex			= TrailInfo(Index).TrailIndex;
	INT NumPoints			= TrailInfo(Index).NumPoints;
	INT Offset				= MaxPointsPerTrail * Index;
	FVector LocationOffset	= FVector(0,0,0);
	FVector	PointLocation	= Particle->Location;

	// Laurent -- trail's location...
	if ( Owner )
	{
		if ( TrailLocation == PTTL_FollowEmitter )
		{
			// Add emitter's velocity to particle
			Particle->Location += Owner->Location - TrailInfo(Index).LastEmitterLocation;
			TrailInfo(Index).LastEmitterLocation = Owner->Location;
			PointLocation = Particle->Location;
		}
	}


	// Laurent -- Kill Hack
	if ( KillPending && (NumPoints==2) )
	{
		Particle->Time = Particle->MaxLifetime + 1.0; // Kill next tick
	}

	// Laurent -- Remove points if pointlifetime is up
	if ( (PointLifeTime > 0.f) && (NumPoints > 2) )
	{
		INT LastPointIndex;
		INT bKeepChecking=1;
		while ( bKeepChecking && (NumPoints > 2) ) // Laurent -- keep killing until allowed to live...
		{
			bKeepChecking	= 0;
			LastPointIndex	= TrailIndex - (NumPoints-1);
			if ( LastPointIndex < 0 )	LastPointIndex += MaxPointsPerTrail;

			//debugf(TEXT("Removing 1 point - TI:%i NP:%i LPI:%i"), TrailIndex, NumPoints, LastPointIndex);

			if ( TrailData(Offset + LastPointIndex).Time < Owner->Level->TimeSeconds )
			{
				TrailInfo(Index).NumPoints	-= 1;
				NumPoints					-=1;	
				bKeepChecking				= 1;
			}

		}
	}

	// Generate new point if particle moved further than distance threshold.
	// Laurent -- Need to interpolate here, or we can't control trail size (when velocity is too high)
	FLOAT TravelledDist		= FDist(PointLocation, TrailInfo(Index).LastLocation);
	FLOAT DistThreshold		= DistanceThreshold;
	FVector LastLocation	= TrailInfo(Index).LastLocation;

	while ( !KillPending && (TravelledDist >= DistThreshold) )
	{
		// Wrap around if already at points limit.
		if( ++TrailIndex == MaxPointsPerTrail )
			TrailIndex = 0;
		
		NumPoints = Min( NumPoints+1, MaxPointsPerTrail );

		TrailInfo(Index).LastLocation = PointLocation;

		// Laurent -- Linear interpolation of location, to control trail's size
		VLinterp( DistThreshold/TravelledDist, LastLocation, PointLocation, TrailData(Offset + TrailIndex).Location );
		TrailData(Offset + TrailIndex).Color	= SetInitialPointColor(Particle->Color);
		TrailData(Offset + TrailIndex).Size		= Particle->Size.X;
		TrailData(Offset + TrailIndex).Time		= Owner->Level->TimeSeconds + PointLifeTime; // Laurent -- point lifetime


		DistThreshold += DistThreshold;
	}	
	
	if ( !KillPending ) // Laurent -- if kill pending, don't update trail, just let it die...
	{
		TrailData(Offset + TrailIndex).Location	= PointLocation;
		TrailData(Offset + TrailIndex).Color	= SetInitialPointColor(Particle->Color);
		TrailData(Offset + TrailIndex).Size		= Particle->Size.X;
		TrailData(Offset + TrailIndex).Time		= Owner->Level->TimeSeconds + PointLifeTime; // Laurent -- point lifetime
	}

	TrailInfo(Index).NumPoints	= NumPoints;
	TrailInfo(Index).TrailIndex = TrailIndex;

	// Laurent -- Shade Trail
	if ( (TrailShadeType > PTTST_RandomStatic && DrawStyle != PTDS_Regular) )
	{
		FLOAT	PointOpacity	= 1.f;

		for (int i=NumPoints; i>0; i--)
		{

			if ( TrailShadeType == PTTST_RandomDynamic )
				PointOpacity = appFrand();
			else if ( TrailShadeType == PTTST_Linear )
				PointOpacity = FLinterp( FLOAT(NumPoints-i)/FLOAT(NumPoints), 1.0, 0.0); 
			else if ( TrailShadeType == PTTST_PointLife && PointLifeTime>0.f )
				PointOpacity = 1.f - FLinterp( (TrailData(Offset + TrailIndex).Time - Owner->Level->TimeSeconds)/PointLifeTime,
											1.0, 0.0); 

			//debugf(TEXT("Trail Shade Type - TST:%i PN:%i NP:%i PO:%f"), TrailShadeType, i, NumPoints, PointOpacity);
			//debugf(TEXT("Trail Shade Type - PN:%i NP:%i PO:%f PT:%f LT:%f"), i, NumPoints, PointOpacity, TrailData(Offset + TrailIndex).Time, Owner->Level->TimeSeconds);

			if ( DrawStyle == PTDS_AlphaBlend ||
				DrawStyle == PTDS_Modulated ||
				DrawStyle == PTDS_AlphaModulate_MightNotFogCorrectly)
			{
				TrailData(Offset + TrailIndex).Color.A = Particle->Color.A * PointOpacity;
			}
			else
			{
				TrailData(Offset + TrailIndex).Color.R = Particle->Color.R * PointOpacity;
				TrailData(Offset + TrailIndex).Color.G = Particle->Color.G * PointOpacity;
				TrailData(Offset + TrailIndex).Color.B = Particle->Color.B * PointOpacity;
			}

			TrailIndex--;
			if ( TrailIndex < 0 )	TrailIndex += MaxPointsPerTrail;
		}

		//TrailIndex = TrailInfo(Index).TrailIndex;
	}

}


struct FUCTrailVertex
{
	FVector	Position;
	FColor	Diffuse;
	FLOAT	U,
			V;

	static INT GetComponents(FVertexComponent* OutComponents)
	{
		OutComponents[0].Type		= CT_Float3;
		OutComponents[0].Function	= FVF_Position;
		OutComponents[1].Type		= CT_Color;
		OutComponents[1].Function	= FVF_Diffuse;
		OutComponents[2].Type		= CT_Float2;
		OutComponents[2].Function	= FVF_TexCoord0;

		return 3;
	}
};

class FUCTrailVertexStream : public FParticleVertexStream<FUCTrailVertex>
{
public:

	UUCTrailEmitter*			Emitter;
	INT							NumVerts;
	FLevelSceneNode*			SceneNode;

	// Constructor.
	FUCTrailVertexStream( UUCTrailEmitter* InEmitter,INT InNumVerts, FLevelSceneNode* InSceneNode )
	{
		Emitter			= InEmitter;
		NumVerts		= InNumVerts;
		SceneNode		= InSceneNode;
	}

	virtual INT GetSize()
	{
		return NumVerts * sizeof(FUCTrailVertex);
	}

	// Generates the spark vertices procedurally.
	virtual void GetStreamData(void* Dest)
	{
#ifndef _XBOX
		Emitter->FillVertexBuffer( (FUCTrailVertex**) &Dest, SceneNode );
#endif
	}
};

// FUCTrailIndexBuffer
class FUCTrailIndexBuffer : public FIndexBuffer
{
public:

	UUCTrailEmitter*	Emitter;
	INT				NumIndices;

	// Constructor.
	FUCTrailIndexBuffer( UUCTrailEmitter* InEmitter, INT InNumIndices )
	{
		Emitter		= InEmitter;
		NumIndices	= InNumIndices;
	}

	virtual INT GetRevision()
	{
		return 1;
	}

	virtual INT GetSize()
	{
		return NumIndices * sizeof(_WORD);
	}

	virtual INT GetIndexSize()
	{
		return sizeof(_WORD);
	}

	virtual void GetContents(void* Data)
	{
		_WORD* WordData = (_WORD*)Data;
		INT VertexIndex = 0;

		for(INT ParticleIndex = 0;ParticleIndex < Emitter->ActiveParticles;ParticleIndex++)
		{
			FParticle* Particle = (FParticle*)Emitter->Particles(ParticleIndex);

			if( Particle == NULL || !(Particle->Flags & PTF_Active) )
				continue;

			if( Emitter->TrailInfo(ParticleIndex).NumPoints <= 1 )
				continue;

			// Do a second pass for crossed sheets.
			for( INT PassCount=0; PassCount<(Emitter->UseCrossedSheets ? 2 : 1); PassCount++ )
			{
				for( INT i=0; i<Emitter->TrailInfo(ParticleIndex).NumPoints-1; i++ )
				{
					*(WordData++) = 2*VertexIndex+0;
					*(WordData++) = 2*VertexIndex+1;
					*(WordData++) = 2*VertexIndex+2;
					*(WordData++) = 2*VertexIndex+2;
					*(WordData++) = 2*VertexIndex+1;
					*(WordData++) = 2*VertexIndex+3;
					VertexIndex++;
				}
				VertexIndex++;
			}
		}
	}
};

// scion sz -
// FillVertexBuffer
//
#ifdef _XBOX
INT UUCTrailEmitter::FillVertexBuffer( struct FUCTrailVertex** pVertices, INT SheetIndex, INT ParticleIndex, FVector& ViewLocation )
{
	guard(UUCTrailEmitter::FillVertexBuffer);

	FUCTrailVertex*	Vertex = (FUCTrailVertex*) *pVertices;
	FParticle* Particle = (FParticle*)Particles(ParticleIndex);

	check(Particle != NULL && (Particle->Flags & PTF_Active));	

	INT TrailIndex = TrailInfo(ParticleIndex).TrailIndex;	

	// Setup last, current and next location and wrap around index if necessary.
	FVector LastLocation	= FVector(0,0,0);
	FVector Location		= TrailData(ParticleIndex * MaxPointsPerTrail + TrailIndex).Location;

	// Setup size and color.
	FLOAT	Size			= TrailData(ParticleIndex * MaxPointsPerTrail + TrailIndex).Size;
	FColor	Color			= TrailData(ParticleIndex * MaxPointsPerTrail + TrailIndex).Color;

	// Setup next location and wrap around if necessary.
	if( --TrailIndex < 0 )
		TrailIndex += MaxPointsPerTrail;			
	FVector NextLocation	= TrailData(ParticleIndex * MaxPointsPerTrail + TrailIndex).Location;

	Color = Color.RenderColor();

	INT NumPoints = TrailInfo(ParticleIndex).NumPoints;

	//scion -- We are building these verts directly into the push buffer
	DWORD* pdwBuff = (DWORD*) *pVertices;
	// The number of DWORDs each vert takes
	const DWORD dwDwordsPerVert = sizeof(FUCTrailVertex) / sizeof(DWORD);
	// This is the number of DWORDs we encode in a given inline array push
	// NOTE: It must break on primitive alignment (4 verts min)
	const DWORD dwMaxInlineArray = ( D3DPUSH_MAX_COUNT / (dwDwordsPerVert*4) ) * (dwDwordsPerVert*4);
	// Determines how many DWORDs we've pushed into the push buffer
	DWORD dwDwordsEncoded = 0;

	// Generate vertices.
	for( INT i=0; i<NumPoints; i++ )
	{
		// Check to see if we need to encode an inline array push
		if (dwDwordsEncoded % dwMaxInlineArray == 0)
		{
			if( dwDwordsEncoded )
			{
				// handle case when trail vertices don't fit
				// into the push buffer.  Just break for now.
				break;

				*pdwBuff++ = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END,1);
				*pdwBuff++ = 0;				
			}

			// Encode the primitive type and begin command into the push buffer
			*pdwBuff++ = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END,1);
			*pdwBuff++ = PT_QuadStrip;
			// Figure out how many dwords we'll need for each particle
			DWORD dwNumThisBatch = Min<DWORD>( (NumPoints-i)*2*dwDwordsPerVert, dwMaxInlineArray );
			// Build a new batch of verts to be processed
			*pdwBuff++ = D3DPUSH_ENCODE(
				D3DPUSH_NOINCREMENT_FLAG | D3DPUSH_INLINE_ARRAY,
				dwNumThisBatch);
			// Make the vertex be at the correct position in the push buffer
			Vertex = (FUCTrailVertex*)pdwBuff;
		}

		FVector	Tangent;

		// No LastLocation.
		if( i == 0 )
		{
			Tangent			= (NextLocation - Location).SafeNormal();
		}
		// No NextLocation.
		else
			if( i == NumPoints-1 )
			{
				Tangent			= (Location - LastLocation).SafeNormal();
			}
			// Both Last and NextLocation are available.
			else
			{
				FVector LastDir	= Location - LastLocation;
				FVector NextDir	= NextLocation - Location;		
				Tangent			= ((LastDir + NextDir) / 2.f).SafeNormal();
			}

			// Twisted system.
			FVector ProjFront	= (Location - ViewLocation).SafeNormal();
			FVector Right		= (FVector(0,0,1) ^ Tangent).SafeNormal();
			FLOAT Angle			= UseCrossedSheets ? 16384 : MaxTrailTwistAngle * (Right | ProjFront);
			FVector Up			= !UseCrossedSheets || SheetIndex ? Right.RotateAngleAxis( Angle, Tangent ).SafeNormal() : Right;
			Up					*= Size;

			Vertex->Position	= Location + Up;
			Vertex->Diffuse		= Color;
			Vertex->U			= i / (FLOAT) NumPoints;
			Vertex->V			= 1.f;
			Vertex++;

			// Keep the push buffer pointer in synch
			pdwBuff += dwDwordsPerVert;
			// Update the number of dwords encoded in the push buffer
			dwDwordsEncoded += dwDwordsPerVert;

			Vertex->Position	= Location - Up;
			Vertex->Diffuse		= Color;
			Vertex->U			= i / (FLOAT) NumPoints;
			Vertex->V			= 0.f;
			Vertex++;

			// Keep the push buffer pointer in synch
			pdwBuff += dwDwordsPerVert;
			// Update the number of dwords encoded in the push buffer
			dwDwordsEncoded += dwDwordsPerVert;

			// Update last and current location.
			LastLocation		= Location;
			Location			= NextLocation;

			// Update size and color.
			Size				= TrailData(ParticleIndex * MaxPointsPerTrail + TrailIndex).Size;
			Color				= TrailData(ParticleIndex * MaxPointsPerTrail + TrailIndex).Color;

			// Update next location and wrap around if necessary.
			if( --TrailIndex < 0 )
				TrailIndex += MaxPointsPerTrail;
			NextLocation		=  TrailData(ParticleIndex * MaxPointsPerTrail + TrailIndex).Location;

	}

	//scion jg -- Add the terminate command for the push buffer
	*pdwBuff++ = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END,1);
	*pdwBuff++ = 0;	
	// Set where in the push buffer we ended up so that we can check for push
	// buffer overwrites
	*pVertices = (FUCTrailVertex*)pdwBuff;

	return 0;
	unguard;
}
#else
INT UUCTrailEmitter::FillVertexBuffer( struct FUCTrailVertex** pVertices, FLevelSceneNode* SceneNode )
{
	guard(UUCTrailEmitter::FillVertexBuffer);

	FVector ViewLocation = SceneNode->ViewOrigin;

	FUCTrailVertex*	Vertex = (FUCTrailVertex*) *pVertices;
	for(INT ParticleIndex = 0;ParticleIndex < ActiveParticles;ParticleIndex++)
	{
		FParticle* Particle = (FParticle*)Particles(ParticleIndex);

		if( Particle == NULL || !(Particle->Flags & PTF_Active) )
			continue;

		INT NumPoints = TrailInfo(ParticleIndex).NumPoints;
		if( NumPoints <= 1 )
			continue;

		// Do a second pass for crossed sheets.
		for( INT PassCount=0; PassCount<(UseCrossedSheets ? 2 : 1); PassCount++ )
		{	
			INT TrailIndex = TrailInfo(ParticleIndex).TrailIndex;

			// Setup last, current and next location and wrap around index if necessary.
			FVector LastLocation	= FVector(0,0,0);
			FVector Location		= TrailData(ParticleIndex * MaxPointsPerTrail + TrailIndex).Location;

			// Setup size and color.
			FLOAT	Size			= TrailData(ParticleIndex * MaxPointsPerTrail + TrailIndex).Size;
			FColor	Color			= TrailData(ParticleIndex * MaxPointsPerTrail + TrailIndex).Color;

			// Setup next location and wrap around if necessary.
			if( --TrailIndex < 0 )
				TrailIndex += MaxPointsPerTrail;			
			FVector NextLocation	= TrailData(ParticleIndex * MaxPointsPerTrail + TrailIndex).Location;

			Color = Color.RenderColor();

			// Generate vertices.
			for( INT i=0; i<NumPoints; i++ )
			{
				FVector	Tangent;

				// No LastLocation.
				if( i == 0 )
				{
					Tangent			= (NextLocation - Location).SafeNormal();
				}
				// No NextLocation.
				else
					if( i == NumPoints-1 )
					{
						Tangent			= (Location - LastLocation).SafeNormal();
					}
					// Both Last and NextLocation are available.
					else
					{
						FVector LastDir	= Location - LastLocation;
						FVector NextDir	= NextLocation - Location;		
						Tangent			= ((LastDir + NextDir) / 2.f).SafeNormal();
					}

					// Twisted system.
					FVector ProjFront	= (Location - ViewLocation).SafeNormal();
					FVector Right		= (FVector(0,0,1) ^ Tangent).SafeNormal();
					FLOAT Angle			= UseCrossedSheets ? 16384 : MaxTrailTwistAngle * (Right | ProjFront);
					FVector Up			= !UseCrossedSheets || PassCount ? Right.RotateAngleAxis( Angle, Tangent ).SafeNormal() : Right;
					Up					*= Size;

					Vertex->Position	= Location + Up;
					Vertex->Diffuse		= Color;
					Vertex->U			= i / (FLOAT) NumPoints;
					Vertex->V			= 1.f;
					Vertex++;

					Vertex->Position	= Location - Up;
					Vertex->Diffuse		= Color;
					Vertex->U			= i / (FLOAT) NumPoints;
					Vertex->V			= 0.f;
					Vertex++;

					// Update last and current location.
					LastLocation		= Location;
					Location			= NextLocation;

					// Update size and color.
					Size				= TrailData(ParticleIndex * MaxPointsPerTrail + TrailIndex).Size;
					Color				= TrailData(ParticleIndex * MaxPointsPerTrail + TrailIndex).Color;

					// Update next location and wrap around if necessary.
					if( --TrailIndex < 0 )
						TrailIndex += MaxPointsPerTrail;
					NextLocation		=  TrailData(ParticleIndex * MaxPointsPerTrail + TrailIndex).Location;

			}
		}
	}

	return 0;
	unguard;
}
#endif //_XBOX


// Renders all active particles
INT UUCTrailEmitter::RenderParticles(FDynamicActor* DynActor,FLevelSceneNode* SceneNode,TList<FDynamicLight*>* Lights,FRenderInterface* RI)
{
	guard(UUCTrailEmitter::RenderParticles);

	UParticleEmitter::RenderParticles( DynActor, SceneNode, Lights, RI );

	if( Texture == NULL )
		return 0;

	INT RenderedParticles = 0;

	// Render the particles.
	if( ActiveParticles > 0 )
	{
		// Calculate the number of active particles.
		INT NumVerts	= 0,
			NumTris		= 0,
			NumIndices	= 0;
		for( INT ParticleIndex = 0;ParticleIndex < ActiveParticles;ParticleIndex++ )
		{
			FParticle* Particle = (FParticle*)Particles(ParticleIndex);

			if( Particle != NULL && Particle->Flags & PTF_Active)
			{
				INT NumPoints = TrailInfo(ParticleIndex).NumPoints;
				if( NumPoints > 1 )
				{
					RenderedParticles++;
					NumTris		+= NumPoints * 2 - 2;
					NumVerts	+= NumPoints * 2;
					NumIndices	+= NumPoints * 6 - 6;
				}
			}
		}

		// Bail out if nothing to render.
		if( !RenderedParticles || !NumVerts || !NumIndices )
			return 0;

		//!!vogel: TODO 
		if( UseCrossedSheets )
		{
			NumTris		*= 2;
			NumVerts	*= 2;
			NumIndices	*= 2;
		}

		// Rotate & translate if needed.
		RI->SetTransform(TT_LocalToWorld,FMatrix::Identity);

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

		// Create the dynamic vertex stream and index buffer.
		FUCTrailVertexStream TrailVertices(this,NumVerts,SceneNode);	

#ifdef _XBOX
		RI->SetIndexBuffer(NULL,0);

		FVector ViewLocation = SceneNode->ViewOrigin;

		INT SheetsUsed = UseCrossedSheets ? 2 : 1;

		UBOOL InitialCommit = false;
		DWORD dwParticleSize, dwSizeNeeded, *pdwPushBuffer;
		for(INT ParticleIndex = 0; ParticleIndex < RenderedParticles; ParticleIndex++)
		{
			FParticle*	Particle = (FParticle*)Particles(ParticleIndex);
			if(Particle == NULL || !(Particle->Flags & PTF_Active) )
				continue;

			INT NumPoints = TrailInfo(ParticleIndex).NumPoints;
			if( NumPoints <= 1 )
				continue;

			// per sheet vertices required for each particle
			VerticesPerParticle = NumPoints*2;

			for (INT SheetIndex=0; SheetIndex < SheetsUsed; SheetIndex++)
			{
				BEGIN_D3D_EVENT(BDE)(PIX_PARTICLE,TEXT("%s"),GetName());
				dwParticleSize = sizeof(FUCTrailVertex) * VerticesPerParticle * RenderedParticles;
				dwSizeNeeded = 5 * sizeof(DWORD) + dwParticleSize + 
					( dwParticleSize / ((D3DPUSH_MAX_COUNT-5)*sizeof(DWORD)) * (5*sizeof(DWORD)) );

				if( !InitialCommit )
				{
					// Get a chunk of push buffer to use
					pdwPushBuffer = RI->BeginPush(dwSizeNeeded,0,&TrailVertices, 1, 1);
					InitialCommit = true;
				}
				else
				{
					pdwPushBuffer = RI->BeginPush(dwSizeNeeded,0,&TrailVertices, 0, 0);
				}
				// Build the lines into the push buffer
				FillVertexBuffer( (FUCTrailVertex**)&pdwPushBuffer, SheetIndex, ParticleIndex, ViewLocation );
				// We are done so, let go of the push buffer
				RI->EndPush(pdwPushBuffer);
				END_D3D_EVENT;
			}
		}

#else
		FUCTrailIndexBuffer  TrailIndices(this,NumIndices);

		// Set the particle vertex stream and index buffer.
		// The particle vertices aren't actually generated until now.
		INT	BaseVertexIndex = RI->SetDynamicStream(0,&TrailVertices),
			BaseIndex		= RI->SetDynamicIndexBuffer(&TrailIndices,BaseVertexIndex);

		RI->DrawPrimitive(
			PT_TriangleList, 
			BaseIndex,
			NumTris,
			0,
			NumVerts - 1
		);
#endif //_XBOX

	}

	return RenderedParticles;

	unguard;
}

void UUCTrailEmitter::Serialize( FArchive& Ar )
{
	guard(UUCTrailEmitter::Serialize);
	Super::Serialize( Ar );
	if( !Ar.IsLoading() && !Ar.IsSaving() )
	{
		Ar	<< TrailData
			<< TrailInfo;
	}		
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

