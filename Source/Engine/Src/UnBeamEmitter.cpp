/*=============================================================================
	UnBeamEmitter.cpp: Unreal Beam Emitter
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Daniel Vogel
=============================================================================*/ 

#include "EnginePrivate.h"

/*-----------------------------------------------------------------------------
	USpriteEmitter.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(UBeamEmitter);

void UBeamEmitter::PostEditChange()
{
	guard(UBeamEmitter::PostEditChange);
	Super::PostEditChange();
	CleanUp();
	Initialize( MaxParticles );
	unguard;
}

// scion sz -
// adjust number of sheets and high/low frequency points
// based on the LOD attenuation factor
void UBeamEmitter::AttenMaxParticles( FLOAT AttenFactor )
{
	guard(UUCTrailEmitter::AttenMaxParticles);

	const INT	MinRotatingSheets=1,
				MinHighFrequencyPoints=3,
				MinLowFrequencyPoints=3;

	if( RotatingSheets > MinRotatingSheets ) {
		RotatingSheets = Max( MinRotatingSheets, appCeil( (FLOAT)RotatingSheets*AttenFactor ) );
	}

	if( HighFrequencyPoints > MinHighFrequencyPoints ) {
		HighFrequencyPoints = Max( MinHighFrequencyPoints, appCeil( (FLOAT)HighFrequencyPoints*AttenFactor ) );
	}

	if( LowFrequencyPoints > MinLowFrequencyPoints ) {
		LowFrequencyPoints = Max( MinLowFrequencyPoints, appCeil( (FLOAT)LowFrequencyPoints*AttenFactor ) );
	}

	Super::AttenMaxParticles( AttenFactor );

	unguard;
}


// Initializes the Emitter.
void UBeamEmitter::Initialize( INT InMaxParticles )
{
	guard(UBeamEmitter::Initialize);
	Super::Initialize( InMaxParticles );
	if ( RotatingSheets )
		SheetsUsed = RotatingSheets;
	else
		SheetsUsed = 1;
	VerticesPerParticle		= 2 * (HighFrequencyPoints) * SheetsUsed;
#ifdef _XBOX
	// scion sz - quad strips .. "Only on Xbox!(TM)"
	PrimitivesPerParticle	= 1 * (HighFrequencyPoints-1) * SheetsUsed;
	IndicesPerParticle		= 0;
#else
	PrimitivesPerParticle	= 2 * (HighFrequencyPoints-1) * SheetsUsed;
	IndicesPerParticle		= 6 * (HighFrequencyPoints-1) * SheetsUsed;	
#endif
	HFPoints.Add( HighFrequencyPoints*InMaxParticles );
	LFPoints.Add( LowFrequencyPoints*InMaxParticles );

	HFPoints.Shrink();
	LFPoints.Shrink();

	UpdateActorHitList();
	
	unguard;
}


// CleanUp.
void UBeamEmitter::CleanUp()
{
	guard(UBeamEmitter::CleanUp);
	HFPoints.Empty();
	LFPoints.Empty();
	Super::CleanUp();
	unguard;
}


// Finds first Actor with corresponding tag.
void UBeamEmitter::UpdateActorHitList()
{
	if (!Owner)
		return;
	HitActors.Empty();
	HitActors.Add( BeamEndPoints.Num() );
	ULevel* Level = Owner->GetLevel();
	for (INT i=0; i<BeamEndPoints.Num(); i++)
	{
		HitActors(i) = NULL;
		for (INT n=0; n<Level->Actors.Num(); n++)
		{
			if ( Level->Actors(n) && (Level->Actors(n)->Tag == BeamEndPoints(i).ActorTag  || Level->Actors(n)->GetFName() == BeamEndPoints(i).ActorTag))
			{
				HitActors(i) = Level->Actors(n);
				break;
			}
		}
	}
}


void UBeamEmitter::Scale( FLOAT ScaleFactor )
{
	guard(UBeamEmitter::Scale);
	Super::Scale( ScaleFactor );
	for (INT i=0; i<BeamEndPoints.Num(); i++)
		BeamEndPoints(i).Offset *= ScaleFactor;
	BeamDistanceRange		*= ScaleFactor;
	LowFrequencyNoiseRange  *= ScaleFactor;
	HighFrequencyNoiseRange *= ScaleFactor;
	unguard;
}


void UBeamEmitter::SpawnParticle( INT Index, FLOAT SpawnTime, INT Flags, INT SpawnFlags, const FVector& LocalLocationOffset )
{
	guard(UBeamEmitter::SpawnParticle);
	
	Super::SpawnParticle( Index, SpawnTime, Flags, SpawnFlags, LocalLocationOffset );

	check(Particles(Index) != NULL && "Failed to spawn particle");
	FParticle& Particle = *(FParticle*)Particles(Index);

	FVector	Direction = FVector(0,0,0);
	FVector	EndPoint  = FVector(0,0,0);
	UBOOL SetEndPoint = false;

	// Calculate which actor/ offset to use for endpoint determination.
	INT EPIndex       = -1;
	switch ( DetermineEndPointBy )
	{
	case PTEP_Offset:
	case PTEP_OffsetAsAbsolute:
	case PTEP_TraceOffset:
	case PTEP_Actor:
		if ( BeamValueSum )
		{
			FLOAT Random = appFrand() * BeamValueSum;
			FLOAT Sum	 = 0;
			for (INT i=0; i<BeamEndPoints.Num(); i++)
			{
				if ( (Sum < Random) && (Random <= (Sum + BeamEndPoints(i).Weight) ) )
				{
					EPIndex = i;
					break;
				}
				Sum += BeamEndPoints(i).Weight;
			}

		}
		if( (EPIndex == -1) && BeamEndPoints.Num() )
			EPIndex = 0;
		break;
	default:
		break;
	}

	if ( (DetermineEndPointBy == PTEP_TraceOffset) && (CoordinateSystem == PTCS_Relative) )
			debugf(TEXT("PTCS_Relative is not supported in conjunction with PTEP_TraceOffset"));

	// Setup endpoint.
	INT EndPointType = DetermineEndPointBy;
	if( EndPointType != PTEP_None && DetermineEndPointBy != PTEP_Velocity && DetermineEndPointBy != PTEP_Distance && EPIndex != -1 )
	{
		EndPointType = BeamEndPoints(EPIndex).DetermineEndPointBy;		
	}

	switch ( EndPointType )
	{
	case PTEP_Velocity :
		Direction = Particle.MaxLifetime * Particle.Velocity;
		break;
	case PTEP_Distance :
		Particle.Velocity.Normalize();
		Direction = BeamDistanceRange.GetRand() * Particle.Velocity;
		break;
	case PTEP_Offset :
		if ( EPIndex != -1 )
		{
			FVector Offset = BeamEndPoints(EPIndex).Offset.GetRand();
			Direction = Offset;
			EndPoint  = Offset + Particle.Location;
		}
		SetEndPoint = true;
		break;
	case PTEP_OffsetAsAbsolute:
		if ( EPIndex != -1 )
		{
			FVector Offset = BeamEndPoints(EPIndex).Offset.GetRand();
			Direction = Offset - Particle.Location;
			EndPoint  = Offset;
		}
		SetEndPoint = true;
		break;
	case PTEP_TraceOffset :
		if ( EPIndex != -1 )
		{
			FVector Offset = BeamEndPoints(EPIndex).Offset.GetRand();
			Direction = Offset;
			EndPoint  = Offset + Particle.Location;

			FCheckResult Hit;
			if ( !Owner->GetLevel()->SingleLineCheck( 
				Hit,
				Owner,  
				EndPoint,
				Particle.Location,
				TRACE_AllBlocking
				))
			{
				EndPoint  = Hit.Location;
				Direction = EndPoint - Particle.Location;
			}
		}
		SetEndPoint = true;
		break;
	case PTEP_Actor :
		if ( (EPIndex != -1) && (EPIndex < HitActors.Num()) && HitActors(EPIndex) )
		{
			Direction = HitActors(EPIndex)->Location - Particle.Location;
			EndPoint  = HitActors(EPIndex)->Location;
			if( TriggerEndpoint )
				HitActors(EPIndex)->eventTrigger( Owner, NULL );
		}
		SetEndPoint = true;
		break;
	default:
		break;
	}	

	if ( NoiseDeterminesEndPoint )
		SetEndPoint = false;

	SetHighLowFrequencyPoints( Index, SpawnTime, Particle, Direction, EndPoint, SetEndPoint );

	unguard;
}

void UBeamEmitter::SetHighLowFrequencyPoints( INT Index, FLOAT SpawnTime, FParticle& Particle, FVector &Direction, FVector &EndPoint, UBOOL SetEndPoint )
{
	guard(UBeamEmitter::SetHighLowFrequencyPoints);	

	// Handle low & high frequency noise by modulating the high frequency on top of the low frequency one.
	// Sorry for the code complexity  -- vogel
	FLOAT RHF    = 1.f / (HighFrequencyPoints-1);
	FLOAT RLF    = 1.f / (LowFrequencyPoints-1);
	FLOAT TH     = RHF;
	FLOAT TL     = 0;
	FLOAT RLFPs = 1.f / LowFrequencyPoints;
	FLOAT RHFPs = 1.f / HighFrequencyPoints;
	FVector* LFPoint = &LFPoints(Index * LowFrequencyPoints);
	*(LFPoint++) = Particle.Location;
	FVector LFNoise = FVector(0,0,0);
	for ( INT i=1; i<LowFrequencyPoints; i++ )
	{
		if ( UseLowFrequencyScale )
		{
			//!!TODO: see UnParticleEmitter.cpp
			FLOAT RelativeLength = appFractional( i * RLFPs * (LFScaleRepeats+1) );
			for (INT n=0; n<LFScaleFactors.Num(); n++)
			{
				if ( LFScaleFactors(n).RelativeLength >= RelativeLength )
				{
					FLOAT R1;
					FLOAT R2  = LFScaleFactors(n).RelativeLength;
					FVector V1;
					FVector V2 = LFScaleFactors(n).FrequencyScale;
					if (n)
					{
						V1 = LFScaleFactors(n-1).FrequencyScale;
						R1 = LFScaleFactors(n-1).RelativeLength;
					}
					else
					{
						V1 = FVector(1,1,1);
						R1 = 0.f;
					}
					FLOAT A;
					if ( R2 )
						A = (RelativeLength - R1) / (R2 - R1);
					else
						A = 1.f;

					// Interpolate between two vectors and apply noise.
					LFNoise = Lerp(V1, V2, A) * HighFrequencyNoiseRange.GetRand();
					break;			
				}
			}	
		}
		else
			LFNoise = LowFrequencyNoiseRange.GetRand();
		*(LFPoint++) = Particle.Location + Direction * i * RLF + LFNoise;
	}
	if ( SetEndPoint )
		*(--LFPoint) = EndPoint; 
	LFPoint = &LFPoints(Index * LowFrequencyPoints);
	FParticleBeamData* HFPoint = &HFPoints(Index * HighFrequencyPoints);
	HFPoint->Location	= *LFPoint;
	HFPoint->t			= 0;
	HFPoint++;
	FVector HFNoise = FVector(0,0,0);
	for ( INT i=1, CurrentLFPoint=0; i<HighFrequencyPoints; i++ )
	{
		if ( UseHighFrequencyScale )
		{
			FLOAT RelativeLength = appFractional( i * RHFPs * (HFScaleRepeats+1) );
			for (INT n=0; n<HFScaleFactors.Num(); n++)
			{
				if ( HFScaleFactors(n).RelativeLength >= RelativeLength )
				{
					FLOAT R1;
					FLOAT R2  = HFScaleFactors(n).RelativeLength;
					FVector V1;
					FVector V2 = HFScaleFactors(n).FrequencyScale;
					if (n)
					{
						V1 = HFScaleFactors(n-1).FrequencyScale;
						R1 = HFScaleFactors(n-1).RelativeLength;
					}
					else
					{
						V1 = FVector(1,1,1);
						R1 = 0.f;
					}
					FLOAT A;
					if ( R2 )
						A = (RelativeLength - R1) / (R2 - R1);
					else
						A = 1.f;

					// Interpolate between two vectors and apply noise.
					HFNoise = Lerp(V1, V2, A) * HighFrequencyNoiseRange.GetRand();
					break;			
				}
			}
		}
		else
			HFNoise = HighFrequencyNoiseRange.GetRand();

		if( CurrentLFPoint < LowFrequencyPoints-1 )
		{
			TL = TH * (LowFrequencyPoints-1);
			HFPoint->Location = (1-TL) * (*LFPoint) + TL * (*(LFPoint+1)) + HFNoise;
			HFPoint->t = i * RHF;
		}
		else
		{
			// scion sz - if we reach the last LFPoint then just use it as the endpoint
			HFPoint->Location = (*LFPoint);
			HFPoint->t = i * RHF;
		}		

		// Spawn particle in another emitter.
		if ( UseBranching 
			&&   (i>=BranchHFPointsRange.Min && i<=BranchHFPointsRange.Max) 
			&&   (BranchProbability.GetRand() >= appFrand()) 
			)
		{
			if ( BranchEmitter >= 0 )
			{
				UParticleEmitter* OtherEmitter = Owner->Emitters(BranchEmitter);
				if ( OtherEmitter->Initialized ) //&& SpawnTime ) //!! vogel: ?!
				{
					// In case OtherEmitter hasn't been ticked yet.
					OtherEmitter->Owner = Owner;

					INT Amount = (INT)BranchSpawnAmountRange.GetRand();
					for (INT i=0; i<Amount; i++)
					{
						OtherEmitter->SpawnParticle( OtherEmitter->ParticleIndex, SpawnTime, 0, PSF_NoGlobalOffset | PSF_NoOwnerLocation, HFPoint->Location );
						// Let particles die at the same time.
						if ( LinkupLifetime )
						{
							FParticle* OtherParticle = (FParticle*)OtherEmitter->Particles(OtherEmitter->ParticleIndex);
							OtherParticle->Time = Particle.Time;
							OtherParticle->MaxLifetime = Particle.MaxLifetime;
						}
						OtherEmitter->ActiveParticles = Max(OtherEmitter->ActiveParticles, OtherEmitter->ParticleIndex+1);
						(++OtherEmitter->ParticleIndex) %= OtherEmitter->MaxActiveParticles;
					}		   
				}
			}
		}

		HFPoint++;
		TH += RHF;
		if ( TH >= RLF)
		{
			TH -= RLF;
			LFPoint++;
			CurrentLFPoint++;
		}
	}
	if ( SetEndPoint )
		(--HFPoint)->Location = EndPoint;

	unguard;
}


INT UBeamEmitter::UpdateParticles( FLOAT DeltaTime )
{
	guard(UBeamEmitter::UpdateParticles)

	BeamValueSum = 0;
	for (INT i=0; i<BeamEndPoints.Num(); i++)
		BeamValueSum += BeamEndPoints(i).Weight;

	// Update list of actors to hit.
	if ( HitActors.Num() != BeamEndPoints.Num() )
		UpdateActorHitList();

	// update endpoints that are attached to actors
	for( INT i=0; i<BeamEndPoints.Num(); i++ )
	{		
		if( BeamEndPoints(i).DetermineEndPointBy == PTEP_Actor ||
			(BeamEndPoints(i).DetermineEndPointBy == PTEP_None && DetermineEndPointBy == PTEP_Actor) )
		{
			if( !HitActors(i) || 
				(HitActors(i) && BeamEndPoints(i).ActorTag != HitActors(i)->Tag) )
			{
				UpdateActorHitList();
			}

			if( (i < HitActors.Num()) && HitActors(i) )
			{
				INT Index = i%Particles.Num();
				FParticle* Particle = (FParticle*)Particles(Index);
				check(Particle && "Attempt to update NULL particle");
				Particle->Location = StartLocationOffset + Owner->Location;
				Particle->RevolutionCenter += Owner->Location;

				FVector Direction = HitActors(i)->Location - Particle->Location;
				FVector EndPoint  = HitActors(i)->Location;

				SetHighLowFrequencyPoints( i, 0, *Particle, Direction, EndPoint, true );		
				
				if( TriggerEndpoint )
					HitActors(i)->eventTrigger( Owner, NULL );
			}
		}
	}

	// Verify range of critical variables.
	if ( Owner )
	{
		if ( BranchEmitter >= 0 )
			BranchEmitter = Clamp(BranchEmitter, 0, Owner->Emitters.Num() - 1 );
	}
	else
		return 0;
		
	INT Value =	Super::UpdateParticles( DeltaTime );
	
	TimeSinceLastDynamicNoise += DeltaTime;

	BoundingBox.Init();
	//!! TODO simply add start/ end and expand by noise
	for (INT Index=0; Index<ActiveParticles; Index++)
	{
		FParticle* Particle = (FParticle*)Particles(Index);
		if ( Particle == NULL || !(Particle->Flags & PTF_Active) )
			continue;
			
		// Bounding box.
		BoundingBox += HFPoints(Index * HighFrequencyPoints).Location;
		BoundingBox += HFPoints((Index+1) * HighFrequencyPoints - 1).Location;
	}

	// Expand the bounding box.
	FLOAT Expand = 0;
	// Take into account noise.
	Expand += Max3(
		HighFrequencyNoiseRange.X.Size(), 
		HighFrequencyNoiseRange.Y.Size(), 
		HighFrequencyNoiseRange.Z.Size());
	Expand += Max3( 
		LowFrequencyNoiseRange.X.Size(), 
		LowFrequencyNoiseRange.Y.Size(), 
		LowFrequencyNoiseRange.Z.Size());
	BoundingBox = BoundingBox.ExpandBy( MaxSizeScale * StartSizeRange.X.GetMax() * Expand );

	return Value;

	unguard;
}

/*
	FBeamVertex
*/

class FBeamVertex
{
public:

	FVector	Position;
	FColor	Color;
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

// scion sz -
// FBeamVertexStream
//
class FBeamVertexStream : public FParticleVertexStream<FBeamVertex>
{
public:

	UBeamEmitter*		Emitter;
	INT					NumParticles;
	FLevelSceneNode*	SceneNode;

	// Constructor.
	FBeamVertexStream(UBeamEmitter* InEmitter,INT InNumParticles,FLevelSceneNode* InSceneNode)
	{
		Emitter			= InEmitter;
		NumParticles	= InNumParticles;
		SceneNode		= InSceneNode;
	}

	virtual INT GetSize()
	{
		return NumParticles * sizeof(FBeamVertex) * Emitter->VerticesPerParticle;
	}

	// Generates the sprite vertices procedurally.
	virtual void GetStreamData(void* Dest)
	{
#ifndef _XBOX
		Emitter->FillVertexBuffer( (FBeamVertex**) &Dest, NumParticles, SceneNode );
#endif
	}
};

// scion sz -
// FBeamIndexBuffer
//
class FBeamIndexBuffer : public FIndexBuffer
{
public:

	UBeamEmitter*	Emitter;
	INT				NumParticles;

	// Constructor.
	FBeamIndexBuffer( UBeamEmitter* InEmitter, INT InNumParticles )
	{
		Emitter		= InEmitter;
		NumParticles	= InNumParticles;
	}

	virtual INT GetRevision()
	{
		return 1;
	}

	virtual INT GetSize()
	{
		return NumParticles * Emitter->IndicesPerParticle * sizeof(_WORD);
	}

	virtual INT GetIndexSize()
	{
		return sizeof(_WORD);
	}

	virtual void GetContents(void* Data)
	{
		_WORD* VBIndex = (_WORD*)Data;

		INT RenderedParticles = 0;
		for(INT ParticleIndex = 0;ParticleIndex < NumParticles;ParticleIndex++)
		{
			FParticle*	Particle = (FParticle*)Emitter->Particles(ParticleIndex);

			if( Particle == NULL || !(Particle->Flags & PTF_Active) )
				continue;

			for (INT r=0; r<Emitter->SheetsUsed; r++)
			{
				for (INT n=0; n<Emitter->HighFrequencyPoints-1; n++)
				{
					*(VBIndex++) = 2*(n+0 + r*Emitter->HighFrequencyPoints) + 0 + Emitter->VerticesPerParticle * RenderedParticles;
					*(VBIndex++) = 2*(n+0 + r*Emitter->HighFrequencyPoints) + 1 + Emitter->VerticesPerParticle * RenderedParticles;
					*(VBIndex++) = 2*(n+1 + r*Emitter->HighFrequencyPoints) + 0 + Emitter->VerticesPerParticle * RenderedParticles;
					*(VBIndex++) = 2*(n+0 + r*Emitter->HighFrequencyPoints) + 1 + Emitter->VerticesPerParticle * RenderedParticles;
					*(VBIndex++) = 2*(n+1 + r*Emitter->HighFrequencyPoints) + 1 + Emitter->VerticesPerParticle * RenderedParticles;
					*(VBIndex++) = 2*(n+1 + r*Emitter->HighFrequencyPoints) + 0 + Emitter->VerticesPerParticle * RenderedParticles;
				}
			}

			RenderedParticles++;
		}
	}
};

// scion sz -
// FillVertexBuffer
//
#ifdef _XBOX
INT UBeamEmitter::FillVertexBuffer( class FBeamVertex** pVertices, INT SheetIndex, INT ParticleIndex, FVector& ViewLocation )
{
	guard(UBeamEmitter::FillVertexBuffer);	

	FBeamVertex* Vertex = (FBeamVertex*) *pVertices;
	FParticle* Particle = (FParticle*)Particles(ParticleIndex);

	check(Particle && (Particle->Flags & PTF_Active));

	// Color.
	FColor Color = Particle->Color.RenderColor();

	// Time.
	FLOAT Time			= Particle->Time;
	FLOAT RelativeTime;
	if ( Particle->MaxLifetime )
		RelativeTime	= Clamp( Time / Particle->MaxLifetime, 0.f, 1.f );
	else
		RelativeTime	= 0.f;

	// Projection code.
	FVector Up;
	FVector Right;
	FVector Direction;

	// Dynamic noise.
	INT		ApplyDynamicNoise		= 1;
	FLOAT	Divisor					= DynamicTimeBetweenNoiseRange.GetRand();
	if( Divisor )
	{
		ApplyDynamicNoise			= (INT) (TimeSinceLastDynamicNoise / Divisor);
		TimeSinceLastDynamicNoise	= fmodf( TimeSinceLastDynamicNoise, Divisor );
	}

	FParticleBeamData* BeamPoint = &HFPoints(ParticleIndex * HighFrequencyPoints);
	FVector OldLocation	= 2 * BeamPoint->Location - (BeamPoint+1)->Location; //!! sort of hack

	//scion -- We are building these verts directly into the push buffer
	DWORD* pdwBuff = (DWORD*) *pVertices;
	// The number of DWORDs each vert takes
	const DWORD dwDwordsPerVert = sizeof(FBeamVertex) / sizeof(DWORD);
	// This is the number of DWORDs we encode in a given inline array push
	// NOTE: It must break on primitive alignment (4 verts min)
	const DWORD dwMaxInlineArray = ( D3DPUSH_MAX_COUNT / (dwDwordsPerVert*4) ) * (dwDwordsPerVert*4);
	// Determines how many DWORDs we've pushed into the push buffer
	DWORD dwDwordsEncoded = 0;
	
	for (INT n=0; n<HighFrequencyPoints; n++)
	{
		// Check to see if we need to encode an inline array push
		if (dwDwordsEncoded % dwMaxInlineArray == 0)
		{
			if( dwDwordsEncoded )
			{
				*pdwBuff++ = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END,1);
				*pdwBuff++ = 0;

				// if the number of points in this beam particle is too long
				// then split it into multiple quad strips
				// Back up one beam point and continue encoding quads
				n = Max(n-1,1);
				BeamPoint = &HFPoints(ParticleIndex * HighFrequencyPoints + n);
				FParticleBeamData* LastBeamPoint = &HFPoints(ParticleIndex * HighFrequencyPoints + n - 1);
				OldLocation = LastBeamPoint->Location;
			}

			// Encode the primitive type and begin command into the push buffer
			*pdwBuff++ = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END,1);
			*pdwBuff++ = PT_QuadStrip;
   			// Figure out how many dwords we'll need for each particle
			DWORD dwNumThisBatch = Min<DWORD>( (HighFrequencyPoints-n)*2*dwDwordsPerVert, dwMaxInlineArray );
			// Build a new batch of verts to be processed
			*pdwBuff++ = D3DPUSH_ENCODE(
				D3DPUSH_NOINCREMENT_FLAG | D3DPUSH_INLINE_ARRAY,
				dwNumThisBatch);
			// Make the vertex be at the correct position in the push buffer
			Vertex = (FBeamVertex*)pdwBuff;
		}

		if( ApplyDynamicNoise && n > DynamicHFNoisePointsRange.Min && n < DynamicHFNoisePointsRange.Max )
			BeamPoint->Location += ApplyDynamicNoise * DynamicHFNoiseRange.GetRand();

		Right = BeamPoint->Location - OldLocation;
		Up	  = Right ^  (OldLocation - ViewLocation);
		Up.Normalize();
		if ( SheetIndex )
		{
			Right.Normalize();
			Up = Up.RotateAngleAxis( SheetIndex * (0x8000/ SheetsUsed), Right );
		}
		Up *= Particle->Size.X;

		Vertex->Position	= BeamPoint->Location + Up;
		Vertex->Color		= Color;
		Vertex->U			= BeamPoint->t * BeamTextureUScale;
		Vertex->V			= 0;
		Vertex++;

		// Keep the push buffer pointer in synch
		pdwBuff += dwDwordsPerVert;
		// Update the number of dwords encoded in the push buffer
		dwDwordsEncoded += dwDwordsPerVert;

		Vertex->Position	= BeamPoint->Location - Up;
		Vertex->Color		= Color;
		Vertex->U			= BeamPoint->t * BeamTextureUScale;
		Vertex->V			= BeamTextureVScale;
		Vertex++;

		// Keep the push buffer pointer in synch
		pdwBuff += dwDwordsPerVert;
		// Update the number of dwords encoded in the push buffer
		dwDwordsEncoded += dwDwordsPerVert;

		OldLocation = BeamPoint->Location;
		BeamPoint++;
	}

	//scion jg -- Add the terminate command for the push buffer
	*pdwBuff++ = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END,1);
	*pdwBuff++ = 0;	
	// Set where in the push buffer we ended up so that we can check for push
	// buffer overwrites
	*pVertices = (FBeamVertex*)pdwBuff;

	return 0;
	unguard;
}
#else
INT UBeamEmitter::FillVertexBuffer( class FBeamVertex** pVertices, INT NumParticles, FLevelSceneNode* SceneNode )
{
	guard(UBeamEmitter::FillVertexBuffer);

	FRotator RelativeRot = (bUseRotationOverride) ? RotationOverride : Owner->Rotation;
	FVector ViewLocation = SceneNode->ViewOrigin;

	// Transform coordinate system.
	if ( CoordinateSystem == PTCS_Relative )
		ViewLocation = ViewLocation.TransformPointBy ( GMath.UnitCoords / RelativeRot / Owner->Location );

	FBeamVertex* Vertex = (FBeamVertex*) *pVertices;

	// Render all active particles.
	INT RenderedParticles = 0;
	for (INT Index=0; Index<NumParticles; Index++)
	{	
		FParticle* Particle = (FParticle*)Particles(Index);
		if (Particle == NULL || !(Particle->Flags & PTF_Active))
			continue;

		// Color.
		FColor Color = Particle->Color;

		// Time.
		FLOAT Time			= Particle->Time;
		FLOAT RelativeTime;
		if ( Particle->MaxLifetime )
			RelativeTime	= Clamp( Time / Particle->MaxLifetime, 0.f, 1.f );
		else
			RelativeTime	= 0.f;

		// Projection code.
		FVector Up;
		FVector Right;
		FVector Direction;

		// Dynamic noise.
		INT		ApplyDynamicNoise		= 1;
		FLOAT	Divisor					= DynamicTimeBetweenNoiseRange.GetRand();
		if( Divisor )
		{
			ApplyDynamicNoise			= (INT) (TimeSinceLastDynamicNoise / Divisor);
			TimeSinceLastDynamicNoise	= fmodf( TimeSinceLastDynamicNoise, Divisor );
		}

		Color = Color.RenderColor();

		// Rendering.
		for (INT r=0; r<SheetsUsed; r++)
		{
			FParticleBeamData* BeamPoint = &HFPoints(Index * HighFrequencyPoints);
			FVector OldLocation	= 2 * BeamPoint->Location - (BeamPoint+1)->Location; //!! sort of hack

			for (INT n=0; n<HighFrequencyPoints; n++)
			{
				if( ApplyDynamicNoise && n > DynamicHFNoisePointsRange.Min && n < DynamicHFNoisePointsRange.Max )
					BeamPoint->Location += ApplyDynamicNoise * DynamicHFNoiseRange.GetRand();

				Right = BeamPoint->Location - OldLocation;
				Up	  = Right ^  (OldLocation - ViewLocation);
				Up.Normalize();
				if ( r )
				{
					Right.Normalize();
					Up = Up.RotateAngleAxis( r * (0x8000/ SheetsUsed), Right );
				}
				Up *= Particle->Size.X;

				Vertex->Position	= BeamPoint->Location + Up;
				Vertex->Color		= Color;
				Vertex->U			= BeamPoint->t * BeamTextureUScale;
				Vertex->V			= 0;
				Vertex++;

				Vertex->Position	= BeamPoint->Location - Up;
				Vertex->Color		= Color;
				Vertex->U			= BeamPoint->t * BeamTextureUScale;
				Vertex->V			= BeamTextureVScale;
				Vertex++;

				OldLocation = BeamPoint->Location;
				BeamPoint++;
			}
		}

		RenderedParticles++;
	}

	return 0;

	unguard;
}
#endif //_XBOX


// Renders all active particles
INT UBeamEmitter::RenderParticles(FDynamicActor* DynActor,FLevelSceneNode* SceneNode,TList<FDynamicLight*>* Lights,FRenderInterface* RI)
{
	guard(UBeamEmitter::RenderParticles);

	UParticleEmitter::RenderParticles( DynActor, SceneNode, Lights, RI );

	if ( (Texture == NULL) )
		return 0;

	INT RenderedParticles = 0;
	if( ActiveParticles > 0 )
	{
		// Calculate the number of active particles.
		for(INT ParticleIndex = 0;ParticleIndex < ActiveParticles;ParticleIndex++)
		{
			FParticle*	Particle = (FParticle*)Particles(ParticleIndex);

			if(Particle != NULL && Particle->Flags & PTF_Active)
				RenderedParticles++;
		}

		// Bail out if nothing to render.
		if (!RenderedParticles)
			return 0;

		FRotator RelativeRot = (bUseRotationOverride) ? RotationOverride : Owner->Rotation;

		// Rotate & translate beam effect if it is a relative coordinate system.
		if ( CoordinateSystem == PTCS_Relative )
		{			
			FMatrix	LocalToWorld = FRotationMatrix(RelativeRot) * 
				FTranslationMatrix(Owner->Location + Owner->PrePivot + 
									((Owner->Base && Owner->AttachmentBone == NAME_None) ? Owner->Base->PrePivot : FVector(0,0,0)) );
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
		Owner->ParticleMaterial->BlendBetweenSubdivisions	= BlendBetweenSubdivisions && !UseRandomSubdivision;
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

		// Create dynamic vertex stream for the beam verts
		FBeamVertexStream BeamVertices(this,RenderedParticles,SceneNode);	

#ifdef _XBOX
		RI->SetIndexBuffer(NULL,0);

		FVector ViewLocation = SceneNode->ViewOrigin;

		// Transform coordinate system.
		if ( CoordinateSystem == PTCS_Relative )
			ViewLocation = ViewLocation.TransformPointBy ( GMath.UnitCoords / RelativeRot / Owner->Location );		

		UBOOL InitialCommit = false;
		DWORD dwParticleSize, dwSizeNeeded, *pdwPushBuffer;
		for(INT ParticleIndex = 0;ParticleIndex < RenderedParticles;ParticleIndex++)
		{
			FParticle*	Particle = (FParticle*)Particles(ParticleIndex);
			if( Particle == NULL || !(Particle->Flags & PTF_Active) )
				continue;

			for (INT SheetIndex=0; SheetIndex < SheetsUsed; SheetIndex++)
			{
				BEGIN_D3D_EVENT(BDE)(PIX_PARTICLE,TEXT("%s"),GetName());
				dwParticleSize = sizeof(FBeamVertex) * VerticesPerParticle/SheetsUsed * RenderedParticles;
				dwSizeNeeded = 5 * sizeof(DWORD) + dwParticleSize + 
					( dwParticleSize / ((D3DPUSH_MAX_COUNT-5)*sizeof(DWORD)) * (5*sizeof(DWORD)) );

				if( !InitialCommit )
				{
					// Get a chunk of push buffer to use
					pdwPushBuffer = RI->BeginPush(dwSizeNeeded,0,&BeamVertices, 1, 1);
					InitialCommit = true;
				}
				else
				{
					pdwPushBuffer = RI->BeginPush(dwSizeNeeded,0,&BeamVertices, 0, 0);
				}

				// Build the lines into the push buffer
				FillVertexBuffer( (FBeamVertex**)&pdwPushBuffer, SheetIndex, ParticleIndex, ViewLocation );
				// We are done so, let go of the push buffer
				RI->EndPush(pdwPushBuffer);
				END_D3D_EVENT;
			}
		}

#else
		// Create index buffer
		FBeamIndexBuffer  BeamIndices(this,RenderedParticles);

		INT	BaseVertexIndex = RI->SetDynamicStream(0,&BeamVertices),
			BaseIndex		= RI->SetDynamicIndexBuffer(&BeamIndices,BaseVertexIndex);

		for (INT r=0; r < SheetsUsed; r++)
		{
			RI->DrawPrimitive(
				PT_TriangleList,
				BaseIndex + RenderedParticles * IndicesPerParticle / SheetsUsed * r,
				RenderedParticles * PrimitivesPerParticle / SheetsUsed,
				0,
				RenderedParticles * VerticesPerParticle - 1
				);

		}

#endif //_XBOX

	}

	return RenderedParticles;

	unguard;
}

void UBeamEmitter::Serialize( FArchive& Ar )
{
	guard(UBeamEmitter::Serialize);
	Super::Serialize( Ar );
	if( !Ar.IsLoading() && !Ar.IsSaving() )
	{
		Ar	<< HFPoints
			<< LFPoints
			<< HitActors;
	}		
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

