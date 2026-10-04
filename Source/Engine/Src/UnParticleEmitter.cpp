/*=============================================================================
	UnParticleEmitter.cpp: Unreal Particle Emitter
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Daniel Vogel
=============================================================================*/

#include "EnginePrivate.h"
#include <math.h>

/*-----------------------------------------------------------------------------
	UParticleEmitter.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(UParticleEmitter);

//jg -- The singleton pool of particles
TPooledList<FParticle>::TPoolInstance<FParticle> TPooledList<FParticle>::PoolInstance;

//
// UnrealScript interface.
//
void UParticleEmitter::execSpawnParticle( FFrame& Stack, RESULT_DECL )
{
	guard(UParticleEmitter::execSpawnParticle);
	P_GET_INT(Amount);
	P_GET_UBOOL_OPTX( ReviveDeadParticles, 0 );
	P_FINISH;
	
	//scion superville: Potentially revive dead particles so that SpawnParticle called
	//					on an emitter with all dead particles and no respawning of dead particles
	//					doesn't get ignored.
	if( Amount > 0 && AllParticlesDead )
		AllParticlesDead = !ReviveDeadParticles;
	
	DeferredParticles += Amount;
	unguard;
}

//scion superville: Allow access to reset
void UParticleEmitter::execReset( FFrame& Stack, RESULT_DECL )
{
	guard(UParticleEmitter::execReset);
	
	P_FINISH;
	
	Reset();
	
	unguard;
}

//scion superville: Allow access to die
void UParticleEmitter::execDie( FFrame& Stack, RESULT_DECL )
{
	guard(UParticleEmitter::execDie);
	
	P_FINISH;
	
	Die();
	
	unguard;
}


void UParticleEmitter::execTrigger( FFrame& Stack, RESULT_DECL )
{
	guard(UParticleEmitter::execTrigger);
	P_FINISH;
	
	if( TriggerDisabled )
		Disabled = !Disabled;
	if( ResetOnTrigger )
		Reset();
	CurrentSpawnOnTrigger += appTrunc(SpawnOnTriggerRange.GetRand());
	if( CurrentSpawnOnTrigger )	
	{
		AllParticlesDead	= false;
		Inactive			= false;
	}

	unguard;
}


//
// General helper function.
//
static FORCEINLINE FVector RandomNormal()
{
	FLOAT Size;
	FVector V;
	do
	{
		V		= FVector(1-2*appFrand(), 1-2*appFrand(), 1-2*appFrand());
		Size	= V.Size();
	} while ( (Size > 1.0f) || (Size < 0.1f) );
	return V / Size;
}


// UObject functions.

// In our case equivalent to Spawned for Actors.
void UParticleEmitter::PostLoad()
{
	guard(UParticleEmitter::PostLoad);
	Super::PostLoad();

	// scion sz - force any particle that
	// is using a distortion material and
	// a translucent draw style to actually
	// use the correct draw style of AlphaBlend
	if( DrawStyle == PTDS_Translucent &&
		Texture && 
		Texture->IsPostProcess() )
	{
        DrawStyle = PTDS_AlphaBlend;
	}
	Initialize(MaxParticles);
	unguard;
}

void UParticleEmitter::Destroy()
{
	guard(UParticleEmitter::Destroy);
	CleanUp();
	Super::Destroy();
	unguard;
}

void UParticleEmitter::PostEditChange()
{
	guard(UParticleEmitter::PostEditChange);
	if ( (Particles.Num() != MaxParticles) || ResetAfterChange )
	{
		CleanUp();
		Initialize( MaxParticles );
	}
	// Make sure collision data is up- to date.
	if ( UseCollisionPlanes )
		for (INT i=0; i<CollisionPlanes.Num(); i++)
			CollisionPlanes(i).Normalize();
	unguard;
}

// scion sz -
// scale max particles by AttenFactor and reset this subemitter
void UParticleEmitter::AttenMaxParticles( FLOAT AttenFactor )
{
	guard(UParticleEmitter::AttenMaxParticles);

	InitAttenMax = Max<FLOAT>(AttenFactor,0.f);

	// reset particles in this subemitter
	CleanUp();
	// reinitialize with scaled MaxParticles
    Initialize( appCeil( (FLOAT)MaxParticles * InitAttenMax) );

	unguard;
}

// Helper functions.

void UParticleEmitter::Reset()
{
	guard(UParticleEmitter::Reset);

	ActiveParticles		= 0;
	ParticleIndex		= 0;
	OtherIndex			= 0;
	AllParticlesDead	= false;
	WarmedUp			= false;
	Owner				= NULL;
	InitialDelay		= InitialDelayRange.GetRand();
	TimeTillReset		= AutoResetTimeRange.GetRand();

	// scion sz - reset ay dynamic attenuation
	SetDynamicAtten( 1.f );

	//scion superville: Reset respawn dead particles
	RespawnDeadParticles = (RespawnDeadParticles || RespawnDeadParticlesOnReset);

	unguard;
}

void UParticleEmitter::Die()
{
	guard(UParticleEmitter::Die);
	
	RespawnDeadParticles = 0;

	unguard;
}

// scion sz -
// update the cached coords for this subemitter's 
// local relative rotations
void UParticleEmitter::UpdateRelativeRot( UBOOL bForce )
{
	guardSlow(UParticleEmitter::UpdateRelativeRot);
	
	static FRotator ZeroRot(0,0,0);

	if( bForce || 
		((UseRotationFrom == PTRS_Actor) && 
		 (Owner && LastOwnerRot != Owner->Rotation) || (!Owner && LastOwnerRot != ZeroRot))	 )
	{
		LastOwnerRot = Owner ? Owner->Rotation : ZeroRot;

		switch ( UseRotationFrom )
		{
		case PTRS_Actor:
			RelRotCoords = GMath.UnitCoords*RotationOffset*LastOwnerRot;
			break;
		case PTRS_Offset:
			RelRotCoords = GMath.UnitCoords*RotationOffset;
			break;
		case PTRS_Normal:
			{
				FRotator Rotator  = RotationNormal.Rotation();
				// Map Z to -X if effect was created along the Z axis instead of negative X
				if ( EffectAxis == PTEA_PositiveZ ) {
					Rotator.Pitch -= 16384;
				}
				RelRotAdjZCoords = GMath.UnitCoords*Rotator;
				RelRotCoords = GMath.UnitCoords*RotationNormal.Rotation();
				break;
			}
		default:
			break;
		}

		Accel = Acceleration;
		if( UseRotationFrom != PTRS_None &&
			bTransformAccel && 
			Accel.IsZero() == 0 )
		{
			Accel = Accel.TransformVectorBy( RelRotCoords );
		}
	}

	unguardSlow;
}

// scion sz -
// Attenuate the total number of active particles without
// resetting the entire subemitter. The size of the Particles 
// array is maintained, but only a portion of it will contain
// particles that are active.
void UParticleEmitter::SetDynamicAtten( FLOAT AttenFactor )
{
	guardSlow(UParticleEmitter::SetDynamicAtten);

	// skip if we already applied an initial attenuation to the
	// number of particles spawned
	if( AttenFactor >= InitAttenMax  ||
		AttenFactor < 0.f || AttenFactor > 1.f )
		return;

	// skip if almost equal
	static const MIN_ATTEN_DIFF = 0.2f;
	if( Abs(AttenFactor - DynAttenMaxActive) >= MIN_ATTEN_DIFF )
        return;

	// set new attenuation factor
	DynAttenMaxActive = AttenFactor;
    // shrink max num of active particles
	MaxActiveParticles = appTrunc( (FLOAT)Particles.Num() * DynAttenMaxActive );
	// sync active particles
	ActiveParticles = Min(MaxActiveParticles,ActiveParticles);
	// return any particle pool instances for unused entries 
	// in the particles array
	for( INT i=ActiveParticles; i < Particles.Num(); i++ )
	{
		if( Particles(i) )
		{
			// return instance to pool
			DestroyParticle((FParticle*)Particles(i));
			// null entry
			Particles(i) = NULL;
		}
	}

	unguardSlow;
}

void UParticleEmitter::Initialize( INT InMaxParticles )
{
	guard(UParticleEmitter::Initialized);
	//jg -- Presize the array so there is no slack taking up memory
	Particles.Empty( InMaxParticles );
	Particles.AddZeroed( InMaxParticles );

	Reset();

	RealExtentMultiplier= ExtentMultiplier;
	RealMeshNormal		= MeshNormal.SafeNormal();
	MaxActiveParticles	= InMaxParticles;
	Initialized			= true;
	HasPostProcessMaterial = false;
	// scion sz - start with no attenuation
	DynAttenMaxActive = 1.f;	

	// scion sz - update cached owner relative rotations	
    UpdateRelativeRot( 1 );
	Accel = Acceleration;

	unguard;
}

void UParticleEmitter::CleanUp()
{
	guard(UParticleEmitter::CleanUp);
	// Release all allocated particle memory
	for (INT Index = 0; Index < Particles.Num(); Index++)
	{
		if (Particles(Index) != NULL)
		{
			DestroyParticle((FParticle*)Particles(Index));
		}
	}
	Particles.Empty();

	ActiveParticles		= 0;
	ParticleIndex		= 0;
	Initialized			= false;
	unguard;
}

void UParticleEmitter::Scale( FLOAT ScaleFactor )
{
	guard(UParticleEmitter::Scale);
	StartLocationRange			*= ScaleFactor;
	StartSizeRange				*= ScaleFactor;
	StartVelocityRange			*= ScaleFactor;
	MaxAbsVelocity				*= ScaleFactor;
	StartLocationPolarRange.Z	*= ScaleFactor;
	unguard;
}


// Particle functions.

// Spawn one particle.
void UParticleEmitter::SpawnParticle( INT Index, FLOAT SpawnTime, INT Flags, INT SpawnFlags, const FVector& LocalLocationOffset )
{
	guard(UParticleEmitter::SpawnParticle);
	if( !MaxParticles || !Owner || KillPending || (LifetimeRange.Max <= 0) )
		return;

	//jg -- Check for a particle being there. If not allocate one.
	if (Particles(Index) == NULL)
	{
		Particles(Index) = (INT)CreateParticle();
	}
	FParticle& Particle = *(FParticle*)Particles(Index);

	Particle.Location	= StartLocationOffset;
	Particle.Velocity	= StartVelocityRange.GetRand();

	// Handle Shape.
	UBOOL ApplyAll = StartLocationShape == PTLS_All;
	if( ApplyAll || StartLocationShape == PTLS_Box )
		Particle.Location += StartLocationRange.GetRand();
	if( ApplyAll || StartLocationShape == PTLS_Sphere )
		Particle.Location += SphereRadiusRange.GetRand() * RandomNormal();
	if( ApplyAll || StartLocationShape == PTLS_Polar )	
	{
		FVector Polar = StartLocationPolarRange.GetRand();
		FLOAT X,Y,Z;
		X = Polar.Z * GMath.CosFloat(Polar.X * PI/32768.f)*GMath.SinFloat(Polar.Y * PI/32768.f);
		Y = Polar.Z * GMath.SinFloat(Polar.X * PI/32768.f)*GMath.SinFloat(Polar.Y * PI/32768.f);
		Z = Polar.Z * GMath.CosFloat(Polar.Y * PI/32768.f);
		Particle.Location += FVector(X,Y,Z);
	}

// scion sz - not used
#if 0
	Particle.ColorMultiplier.X = Particle.ColorMultiplier.Y = Particle.ColorMultiplier.Z = 1.f;
#endif

	// Handle spawning from mesh.
	if( (MeshSpawning != PTMS_None) && MeshSpawningStaticMesh )
	{
		guard(MeshSpawning);
		//jg -- Handle compression of static meshes
		// Set the compression scale & offset on the compressor class
		FPackedPosition::SetCompressionVectors(&MeshSpawningStaticMesh->CompressionScale,
			&MeshSpawningStaticMesh->CompressionInvScale,
			&MeshSpawningStaticMesh->CompressionOffset);

		INT MaxIndex = MeshSpawningStaticMesh->VertexStream.Vertices.Num() - 1;
		if( MaxIndex > 0 )
		{
			INT VertexIndex = (MeshSpawning == PTMS_Linear) ? CurrentMeshSpawningIndex++ : (appTrunc(appSRand() * MaxIndex));
			VertexIndex %= MaxIndex;
			//VertexIndex = Clamp<INT>( VertexIndex, 0, MaxIndex );

			if( SpawnOnlyInDirectionOfNormal )
			{
				FVector Normal = MeshSpawningStaticMesh->VertexStream.Vertices(VertexIndex).Normal;	
				if( (Normal | RealMeshNormal) < (1.f - 2.f * MeshNormalThresholdRange.GetRand()) )
				{	
					Particle.Flags &= ~PTF_Active;
					return;
				}
			}

			// scion sz - reordered some of these to minimize on appRand() calls.

			if( UniformMeshScale )
			{
				Particle.Location += MeshSpawningStaticMesh->VertexStream.Vertices(VertexIndex).Position * MeshScaleRange.X.GetRand();
			}
			else
			{
				Particle.Location += MeshSpawningStaticMesh->VertexStream.Vertices(VertexIndex).Position * MeshScaleRange.GetRand();
			}			

			if( VelocityFromMesh )
			{
				FVector VelocityDir = MeshSpawningStaticMesh->VertexStream.Vertices(VertexIndex).Normal;
				if( UniformVelocityScale )
				{
					Particle.Velocity += VelocityDir * VelocityScaleRange.X.GetRand();
				}
				else
				{
					Particle.Velocity += VelocityDir * VelocityScaleRange.GetRand();
				}				
			}

// scion sz - not used
#if 0
			if( UseColorFromMesh )
			{
				FColor Color = MeshSpawningStaticMesh->ColorStream.Colors(VertexIndex);
				Particle.ColorMultiplier.X = Color.R / 255.f;
				Particle.ColorMultiplier.Y = Color.G / 255.f;
				Particle.ColorMultiplier.Z = Color.B / 255.f;
			}
#endif
		}
		//jg -- Wipe the compression scale & offset on the compressor class
		// so that we can find errors faster
		FPackedPosition::ClearCompressionVectors();
		unguard;
	}

// scion sz - not used
#if 0
	// Handle Skeletal mesh spawning.
	if( UseSkeletalLocationAs != PTSU_None && MeshVertsAndNormals.Num() )
	{
	INT NumBones = MeshVertsAndNormals.Num() / 2;
		Particle.BoneIndex = Clamp<INT>(appTrunc(RelativeBoneIndexRange.GetRand() * NumBones), 0.f, NumBones - 1);	
		Particle.OldMeshLocation = MeshVertsAndNormals( Particle.BoneIndex * 2 ) * SkeletalScale;
		Particle.Location += Particle.OldMeshLocation;
	}
#endif

	OtherIndex++;
	if ( AddLocationFromOtherEmitter >= 0 )
	{
		UParticleEmitter* OtherEmitter = Owner->Emitters(AddLocationFromOtherEmitter);
		if (OtherEmitter->ActiveParticles > 0)
		{
			//scion jg -- Handle relative coordinate systems
			INT nIndex = OtherIndex % OtherEmitter->ActiveParticles;
			if (OtherEmitter->Particles(nIndex) != NULL)
			{
				FParticle* OtherParticle = (FParticle*)OtherEmitter->Particles(nIndex);
				// If they are both relative just use the other's location
				if ((CoordinateSystem == PTCS_Relative &&
					OtherEmitter->CoordinateSystem == PTCS_Relative) ||
					(CoordinateSystem != PTCS_Relative &&
					OtherEmitter->CoordinateSystem == PTCS_Relative))
				{
					Particle.Location += OtherParticle->Location;
				}
				else
				{
					Particle.Location += OtherParticle->Location - Owner->Location;
				}
			}
		}
	}
		
	if( UseRotationFrom != PTRS_None )
	{
	// Handle Rotation.
	switch ( UseRotationFrom )
	{
	case PTRS_Actor:
	case PTRS_Offset:
			Particle.Location = Particle.Location.TransformVectorBy( RelRotCoords );
		break;
	case PTRS_Normal:
			Particle.Location = Particle.Location.TransformVectorBy( RelRotAdjZCoords );
			break;
		}
	}
	
	if( UseRevolution )
	{
	Particle.RevolutionCenter		= RevolutionCenterOffsetRange.GetRand();
	Particle.RevolutionsPerSecond	= RevolutionsPerSecondRange.GetRand();

		if( CoordinateSystem == PTCS_Independent ) {
			Particle.RevolutionCenter += Owner->Location;
		}
	}

	if( CoordinateSystem == PTCS_Independent &&
		!(SpawnFlags & PSF_NoOwnerLocation) )
		{
		Particle.Location += Owner->Location + SpawnTime * Owner->Velocity;	
	}

	if ( !(SpawnFlags & PSF_NoGlobalOffset) )
		Particle.Location += GlobalOffset;

	Particle.Location	   += LocalLocationOffset;

	Particle.OldLocation	= Particle.Location;
	Particle.StartLocation	= Particle.Location;
// scion sz - not used
#if 0
	Particle.Mass			= StartMassRange.GetRand();
#endif	
	Particle.ColorMultiplier = ColorMultiplierRange.GetRand();
	Particle.MaxLifetime	= LifetimeRange.GetRand();
	Particle.SubdivisionPlayTime = SubdivisionPlayTimeRange.GetRand();
	Particle.Time			= SpawnTime + InitialTimeRange.GetRand();
	Particle.HitCount		= 0;
	Particle.Flags			= PTF_Active | Flags;
	
	if ( UniformSize )
	{
		Particle.StartSize.X = StartSizeRange.X.GetRand();
		Particle.StartSize.Y = Particle.StartSize.X;
		Particle.StartSize.Z = Particle.StartSize.X;
	}
	else
	{
		Particle.StartSize	= StartSizeRange.GetRand();
	}
	Particle.Size			= Particle.StartSize;
	Particle.Velocity		+= Accel * SpawnTime;
	Particle.VelocityMultiplier.X = Particle.VelocityMultiplier.Y = Particle.VelocityMultiplier.Z = 1.f;
	Particle.RevolutionsMultiplier.X = Particle.RevolutionsMultiplier.Y = Particle.RevolutionsMultiplier.Z = 1.f;
	
	if( UseRotationFrom != PTRS_None )
	{
		Particle.Velocity	= Particle.Velocity.TransformVectorBy( RelRotCoords );
	}

	if( GetVelocityDirectionFrom != PTVD_None )
	{
		FVector Direction;

		if( CoordinateSystem == PTCS_Relative )
			Direction = Particle.Location.SafeNormal();
		else
			Direction = (Owner->Location - Particle.Location).SafeNormal();

		switch( GetVelocityDirectionFrom )
		{
		case PTVD_StartPositionAndOwner:
			Particle.Velocity = -Particle.Velocity * Direction;
			break;
		case PTVD_OwnerAndStartPosition:
			Particle.Velocity = Particle.Velocity * Direction;
			break;
		case PTVD_AddRadial:
			Particle.Velocity += StartVelocityRadialRange.GetRand() * Direction;
		default:
			break;
		}
	}

	if ( AddVelocityFromOtherEmitter >= 0 )
	{
		UParticleEmitter* OtherEmitter = Owner->Emitters(AddVelocityFromOtherEmitter);
		if (OtherEmitter->ActiveParticles > 0)
		{
			FParticle* OtherParticle = (FParticle*)OtherEmitter->Particles(OtherIndex % OtherEmitter->ActiveParticles);
			if (OtherParticle != NULL)
			{
				Particle.Velocity += AddVelocityMultiplierRange.GetRand() * OtherParticle->Velocity;
			}
		}
	}

	Particle.Location += SpawnTime * Particle.Velocity;

// scion sz - not used
#if 0
	// Mass is stored as one over mass internally.
	if ( Particle.Mass )
		Particle.Mass = 1.f / Particle.Mass;
#endif
	
	Particle.StartSpin		= StartSpinRange.GetRand();
	Particle.SpinsPerSecond = SpinsPerSecondRange.GetRand();

	// Determine spin.
	if ( !(SpinCCWorCW.X<0.01f) && ((SpinCCWorCW.X>0.99f) || SpinCCWorCW.X > appFrand()) ) {
		Particle.SpinsPerSecond.X *= -1;
	}
	if ( !(SpinCCWorCW.Y<0.01f) && ((SpinCCWorCW.Y>0.99f) || SpinCCWorCW.Y > appFrand()) ) {
		Particle.SpinsPerSecond.Y *= -1;
	}
	if ( !(SpinCCWorCW.Z<0.01f) && ((SpinCCWorCW.Z>0.99f) || SpinCCWorCW.Z > appFrand()) ) {
		Particle.SpinsPerSecond.Z *= -1;
	}

	Particle.StartSpin		*= 0xFFFF;
	Particle.SpinsPerSecond *= 0xFFFF;

	if ( UseRandomSubdivision )
	{
		if ( SubdivisionEnd )
			Particle.Subdivision = appTrunc((SubdivisionEnd - SubdivisionStart) * appFrand() + SubdivisionStart);
		else
			Particle.Subdivision = appTrunc(appFrand() * TextureUSubdivisions * TextureVSubdivisions);
	}
	else
		Particle.Subdivision = -1;

	if ( (Particle.Time > Particle.MaxLifetime) && Particle.MaxLifetime )
		SpawnParticle( Index, fmod(Particle.Time,Particle.MaxLifetime) );

// scion sz - not used
#if 0
	// Play sound on spawning.
	if( (SpawningSound != PTSC_None) && Sounds.Num() )
	{
		INT SoundIndex = 0;
		switch ( SpawningSound )
		{
		case PTSC_LinearGlobal:
		case PTSC_LinearLocal:		
			SoundIndex = CurrentSpawningSoundIndex++;
			break;
		case PTSC_Random:
			SoundIndex = appTrunc(1000.f * appSRand());
			break;
		}
			
		SoundIndex %= appTrunc(SpawningSoundIndex.Size() ? SpawningSoundIndex.Size() + 1 : Sounds.Num());
		SoundIndex += appTrunc(SpawningSoundIndex.GetMin());
		SoundIndex = Clamp( SoundIndex, 0, Sounds.Num() - 1);

		/* play sound
		if( appSRand() <= (Sounds(SoundIndex).Probability.GetRand() * SpawningSoundProbability.GetRand()) )
				Owner->GetLevel()->Engine->Audio->PlaySound( Owner, SLOT_None, Sounds(SoundIndex).Sound, Particle.Location, Owner->TransientSoundVolume*Sounds(SoundIndex).Volume.GetRand(), Sounds(SoundIndex).Radius.GetRand(), Sounds(SoundIndex).Pitch.GetRand(), SF_NoUpdates, 0.f );
		*/
	}
#endif

	// Make sure we get ticked.
	AllParticlesDead = false;

	unguard;
}


// Spawn continuosly.
FLOAT UParticleEmitter::SpawnParticles( FLOAT OldLeftover, FLOAT Rate, FLOAT DeltaTime )
{
	guard(UParticleEmitter::SpawnParticles);
	
	if (Rate <= 0 || !Owner)
		return 0;

	FLOAT NewLeftover = OldLeftover + DeltaTime * Rate;
	INT Number = appTrunc(NewLeftover);
	NewLeftover = NewLeftover - Number;

	FLOAT Increment = 1.f / Rate;
	FLOAT StartTime = DeltaTime + OldLeftover * Increment - Increment;

	Number = Clamp(Number, 0, MaxActiveParticles);
	if( CurrentSpawnOnTrigger )
	{
		Number = Clamp( Number, 0, CurrentSpawnOnTrigger );
		CurrentSpawnOnTrigger -= Number;
	}

	//epic: superville - only spawn new particles if there is room (otherwise, will have to wait for some to die)
	if( bOnlySpawnIfSpace )
	{
		Number = Min<INT>( Number, MaxActiveParticles - ActiveParticles );
	}

	
	FLOAT	Percent;
	FVector OwnerLocationDelta(Owner->OldLocation - Owner->Location);
	for (INT i=0; i<Number; i++)
	{
		SpawnParticle( ParticleIndex, StartTime - i * Increment, PTF_InitialSpawn );

		// Laurent -- location interpolation hack!
		if ( CoordinateSystem == PTCS_Independent )
		{
			Percent = 1.f - FLOAT(i+1) / FLOAT(Number);
			((FParticle*)Particles(ParticleIndex))->Location += Percent * OwnerLocationDelta;	
		}

		ActiveParticles = Max(ActiveParticles, ParticleIndex+1);
		++ParticleIndex %= MaxActiveParticles;
	}

	return NewLeftover;
	unguard;
}


// Tick each individual particle.
#if __HAS_SSE__
// For some reason this didn't get inlined!
static FLOAT FLOAT_255 = 255.f;
static FORCEINLINE __m64 _mm_cvtps_pi16_EPIC(__m128 a)
{
	return _mm_packs_pi32(_mm_cvtps_pi32(a),_mm_cvtps_pi32(_mm_movehl_ps(a, a)));
}
#endif
INT UParticleEmitter::UpdateParticles( FLOAT DeltaTime )
{
	//scion jg -- This prevents instanced particles from being updated more
	// than once per frame
	if (bIsInstanced && LastUpdateTime == Owner->Level->TimeSeconds)
	{
		return 0;
	}
	// Update the frame time
	LastUpdateTime = Owner->Level->TimeSeconds;

	BoundingBox.Init();
	INT	DeadParticles = 0;
	
	// Verify range of critical variables.
	if ( Owner )
	{
		if ( AddLocationFromOtherEmitter >= 0 )
			AddLocationFromOtherEmitter = Clamp(AddLocationFromOtherEmitter, 0, Owner->Emitters.Num() - 1 );
		if ( AddVelocityFromOtherEmitter >= 0 )
			AddVelocityFromOtherEmitter = Clamp(AddVelocityFromOtherEmitter, 0, Owner->Emitters.Num() -1 );
		if ( SpawnFromOtherEmitter >= 0 )
			SpawnFromOtherEmitter = Clamp(SpawnFromOtherEmitter, 0, Owner->Emitters.Num() - 1 );
	}
	else
		return 0;

// scion sz - not used
#if 0
	// Skeletal mesh stuff.
	INT NumBones = 0;
	if( SkeletalMeshActor && UseSkeletalLocationAs != PTSU_None )
	{
		guard(SkeletalMeshGet);
		if( !SkeletalMeshActor->bDeleteMe && SkeletalMeshActor->Mesh && SkeletalMeshActor->Mesh->IsA(USkeletalMesh::StaticClass()) )
		{
			USkeletalMeshInstance* SkeletalMeshInstance  = (USkeletalMeshInstance*)SkeletalMeshActor->Mesh->MeshGetInstance(SkeletalMeshActor);
			NumBones = SkeletalMeshInstance->GetMeshJointsAndNormals( SkeletalMeshActor, &MeshVertsAndNormals);
		}
		unguard
	}
#endif

	// Spawning.
	FLOAT Rate;
	if ( ActiveParticles < MaxActiveParticles )
	{
		// Either spawn them continously...
		if ( AutomaticInitialSpawning )
		{
			Rate = MaxActiveParticles / LifetimeRange.GetCenter();
		}
		// ... or at a fixed rate.
		else
		{
			Rate = InitialParticlesPerSecond;
		}
	}
	else
	{
		Rate = ParticlesPerSecond; //!! TODO: PPSLightFactor
		//Rate = ParticlesPerSecond * PPSLightFactor * LightBrightness / 255.f;
	}

	// Spawning on trigger.
	if( CurrentSpawnOnTrigger )
		Rate += SpawnOnTriggerPPS;

	//scion jg -- Make Rate zero when killpending
	if (KillPending == 1)
	{
		Rate = 0;
	}

	// Actually spawn them.
	if ( Rate > 0 )
		PPSFraction = SpawnParticles( PPSFraction, Rate, DeltaTime );

	// Deferred spawning.
	if( !KillPending )
	{
		INT Amount = Clamp(DeferredParticles, 0, MaxActiveParticles);

		//epic: superville - only spawn new particles if there is room (otherwise, will have to wait for some to die)
		if( bOnlySpawnIfSpace )
		{
			Amount = Min<INT>( Amount, MaxActiveParticles - ActiveParticles );
		}

		for (INT i=0; i<Amount; i++)
		{
			SpawnParticle( ParticleIndex, 0 );
			ActiveParticles = Max(ActiveParticles, ParticleIndex+1);
			++ParticleIndex %= MaxActiveParticles;
		}
		DeferredParticles = 0;
	}

	// Laurent -- Count particles to respawn for interpolation
	INT	PtclRespawnCount	= 0;
	INT PtclRespawnIndex	= 0;
	FLOAT Percent			= 0.f;
	for (INT Index=0; Index<Min(MaxActiveParticles, ActiveParticles); Index++)
	{
		FParticle* Particle = (FParticle*)Particles(Index);
		
		if (Particle == NULL || !(Particle->Flags & PTF_Active) )
			continue;
	
		// Don't tick particle if it just got spawned via initial spawning.
		if ( !(Particle->Flags & PTF_InitialSpawn) )
			Particle->Time += DeltaTime;

		if ( Particle->Time > Particle->MaxLifetime )
		{
			if ( !RespawnDeadParticles )
				continue;

			PtclRespawnCount++;
		}
	}

	// Only particles 0..Min(MaxActiveParticles, ActiveParticles) are active.
	for (INT Index=0; Index<Min(MaxActiveParticles, ActiveParticles); Index++)
	{
		if (Particles(Index) == NULL)
		{
			DeadParticles++;
			continue;
		}

		FParticle& Particle = *(FParticle*)Particles(Index);
		
		if ( !(Particle.Flags & PTF_Active) )
		{
			// Clean up the dead particle
			DestroyParticle((FParticle*)Particles(Index));
			Particles(Index) = NULL;
			DeadParticles++;
			continue;
		}
		// Don't tick particles with PTF_NoTick
		UBOOL TickParticle = !(Particle.Flags & PTF_NoTick) || (CoordinateSystem == PTCS_Relative);
		// Don't tick particle if it just got spawned via initial spawning.
		if ( Particle.Flags & PTF_InitialSpawn )
		{
			TickParticle = false;
			Particle.Flags &= ~PTF_InitialSpawn;
		}
		//else
		//	Particle.Time += DeltaTime;

		if ( Particle.Time > Particle.MaxLifetime )
		{
			if ( !RespawnDeadParticles )
			{
				Particle.Flags &= ~PTF_Active;
				DeadParticles++;
				continue;
			}
			FLOAT NewTime = Particle.Time - Particle.MaxLifetime + InitialTimeRange.GetRand();
			if( Particle.MaxLifetime )
				NewTime = fmod( NewTime, Particle.MaxLifetime );
			else
				NewTime = 0.f;

			SpawnParticle( Index, NewTime );

			// Laurent -- location interpolation hack!
			if ( CoordinateSystem == PTCS_Independent )
			{
				Percent = 1.f - FLOAT(PtclRespawnIndex+1) / FLOAT(PtclRespawnCount);
				//debugf(TEXT("OwnerLocation - PI: %i P:%f LT:%f"), i, Percent, Owner->Level->TimeSeconds);
				Particle.Location += Percent * (Owner->OldLocation - Owner->Location);	
			}
			PtclRespawnIndex++;
		}
		else if ( TickParticle )
		{
			STAT_INC( GStats.DWORDStats(GEngineStats.STATS_Particle_ParticlesTicked) );

			Particle.Velocity	+= Accel * DeltaTime;
			Particle.OldLocation = Particle.Location;
			Particle.Location	+= DeltaTime * (Particle.Velocity * Particle.VelocityMultiplier);

// scion sz - not used
#if 0
			if( NumBones && UseSkeletalLocationAs == PTSU_Location )
			{
				FVector NewMeshLocation = MeshVertsAndNormals( Particle.BoneIndex * 2 ) * SkeletalScale;
				Particle.Location += NewMeshLocation - Particle.OldMeshLocation;
				Particle.OldMeshLocation = NewMeshLocation;
			}
#endif

			if( UseRevolution )
			{
				FVector Location			= Particle.Location - Particle.RevolutionCenter;
				Location = Location.RotateAngleAxis( 
					appTrunc( Particle.RevolutionsPerSecond.X * Particle.RevolutionsMultiplier.X * DeltaTime * 0xFFFF ), 
					GMath.UnitCoords.XAxis );
				Location = Location.RotateAngleAxis( 
					appTrunc( Particle.RevolutionsPerSecond.Y * Particle.RevolutionsMultiplier.Y * DeltaTime * 0xFFFF ), 
					GMath.UnitCoords.YAxis );
				Location = Location.RotateAngleAxis( 
					appTrunc( Particle.RevolutionsPerSecond.Z * Particle.RevolutionsMultiplier.Z * DeltaTime * 0xFFFF ), 
					GMath.UnitCoords.ZAxis );
				Particle.Location = Location + Particle.RevolutionCenter;
			}
		}

		UBOOL Collided = 0;
		if( UseCollision || UseCollisionPlanes )
		{
		// Colission detection.
			FVector HitNormal(0,0,0);
			FVector HitLocation(0,0,0);
		if ( TickParticle && (CoordinateSystem != PTCS_Relative) )
		{
			if ( UseCollision )
			{
				FCheckResult Hit;
				FVector Direction = (Particle.Location - Particle.OldLocation).SafeNormal() * RealExtentMultiplier * Particle.Size;
				// JAG: This was TRACE_AllBlocking, but this seems a better default...
				// Should pass in flags for this.
				if ( !Owner->GetLevel()->SingleLineCheck(
					Hit,
					Owner,
					Particle.Location + Direction,
					Particle.OldLocation,
					TRACE_LevelGeometry | TRACE_Level
					)
				)
				{
					HitNormal   = Hit.Normal;
					HitLocation = Hit.Location - Direction;
					Collided	= true;
				}
			}
			if ( UseCollisionPlanes && !Collided )
			{
				for (INT i=0; i<CollisionPlanes.Num(); i++)
				{
					FPlane& Plane = CollisionPlanes(i);
					if ( Sgn(Plane.PlaneDot(Particle.Location)) !=  Sgn(Plane.PlaneDot(Particle.OldLocation)) )
					{
						HitLocation = FLinePlaneIntersection( Particle.Location, Particle.OldLocation, Plane );
						HitNormal	= Plane;
						Collided	= true;
						break;
					}
				}
			}
		}
		
		// Handle collided particle.
		if ( Collided )
		{
// scion sz - not used
#if 0
			// Play sound on collision
			if( (CollisionSound != PTSC_None) && Owner->GetLevel()->Engine->Audio )
			{
				INT SoundIndex = 0;
				switch ( CollisionSound )
				{
				case PTSC_LinearGlobal:
					SoundIndex = CurrentCollisionSoundIndex++;
					break;
				case PTSC_LinearLocal:
					SoundIndex = Particle.HitCount;
					break;
				case PTSC_Random:
					SoundIndex = appTrunc(1000 * appSRand());
					break;
				}
				
				SoundIndex %= appTrunc(CollisionSoundIndex.Size() ? CollisionSoundIndex.Size() + 1 : Sounds.Num());
				SoundIndex += appTrunc(CollisionSoundIndex.Min);
				SoundIndex = Clamp( SoundIndex, 0, Sounds.Num() - 1 );

					/* play sound
				if( appSRand() <= (Sounds(SoundIndex).Probability.GetRand() * CollisionSoundProbability.GetRand()) )
					Owner->GetLevel()->Engine->Audio->PlaySound( Owner, SLOT_None, Sounds(SoundIndex).Sound, HitLocation, Owner->TransientSoundVolume*Sounds(SoundIndex).Volume.GetRand(), Sounds(SoundIndex).Radius.GetRand(), Sounds(SoundIndex).Pitch.GetRand(), SF_NoUpdates, 0.f );
				*/
			}
#endif

			// Update.
			if ( UseMaxCollisions && (Particle.HitCount+1 >= appFloor(MaxCollisions.GetRand())))
			{
				if( RespawnDeadParticles )
					SpawnParticle( Index, 0.5 * DeltaTime ); //!! HACK
				else
				{
					Particle.Flags &= ~PTF_Active;
					DeadParticles++;
					continue;
				}
			}			
			else
			{
				Particle.Velocity -= Accel * DeltaTime * 0.5f; //!! HACK
				Particle.Velocity  = Particle.Velocity.MirrorByVector( HitNormal );
				Particle.Velocity *= DampingFactorRange.GetRand();
				if (DampRotation)
					Particle.SpinsPerSecond *= RotationDampingFactorRange.GetRand();
				Particle.Location = HitLocation + HitNormal * 0.01f;
				Particle.OldLocation = Particle.Location;
				++Particle.HitCount;
			}
			
			// Spawn particle in another emitter on collision.
			if ( SpawnFromOtherEmitter >= 0 )
			{
				UParticleEmitter* OtherEmitter = Owner->Emitters(SpawnFromOtherEmitter);
				if ( OtherEmitter->Initialized && OtherEmitter->Owner && DeltaTime )
				{
					for (INT i=0; i<SpawnAmount; i++)
					{
						OtherEmitter->SpawnParticle( OtherEmitter->ParticleIndex, DeltaTime, 0, PSF_NoGlobalOffset | PSF_NoOwnerLocation, HitLocation + HitNormal * 0.01f );
// scion sz - not used
#if 0
						if ( UseSpawnedVelocityScale )
						{
							FParticle* OtherParticle = (FParticle*)OtherEmitter->Particles(OtherEmitter->ParticleIndex);
							OtherParticle->Velocity += HitNormal * SpawnedVelocityScaleRange.GetRand();
						}
#endif
						OtherEmitter->ActiveParticles = Max(OtherEmitter->ActiveParticles, OtherEmitter->ParticleIndex+1);
						(++OtherEmitter->ParticleIndex) %= OtherEmitter->MaxActiveParticles;
					}		
				}
			}
		}
		} 

		// Scaling over time.
		FLOAT TimeFactor	= 1.0f;
		FLOAT Time			= Particle.Time;
		FLOAT RelativeTime = 0.f;
		FPlane Color(1.f,1.f,1.f,1.f);

		if ( Particle.MaxLifetime )
			RelativeTime	= Clamp( Time / Particle.MaxLifetime, 0.f, 1.f );

		// Size scale.
		if ( UseSizeScale )
		{
			if ( UseRegularSizeScale )
				TimeFactor = TimeFactor / ( 1.f + Particle.Time );
			else if	( Particle.MaxLifetime )
			{
				FLOAT SizeRelativeTime = appFractional( RelativeTime * (SizeScaleRepeats+1) );
				FLOAT S1, R1, S2, R2, A;
				for (INT n=0; n<SizeScale.Num(); n++)
				{
					if ( SizeScale(n).RelativeTime >= SizeRelativeTime )
					{
						S2 = SizeScale(n).RelativeSize;
						R2 = SizeScale(n).RelativeTime;
						if (n)
						{
							S1 = SizeScale(n-1).RelativeSize;
							R1 = SizeScale(n-1).RelativeTime;
						}
						else
						{
							S1 = 1.f;
							R1 = 0.f;
						}
						if ( R2 )
							A = (SizeRelativeTime - R1) / (R2 - R1);
						else
							A = 1.f;

						// Interpolate between two scales.
						TimeFactor = Lerp(S1, S2, A);			
						break;
					}	
				}
			}	
		}
		Particle.Size = TimeFactor * Particle.StartSize;
		//scion jg -- Allow DrawScale from the owner to this particle
		if (Owner->bUseOwnerDrawScale3D && Owner->Owner)
		{
			// Get the draw scale 3D size for the Emitter->Owner
			Particle.Size *= Owner->Owner->DrawScale3D;
		}
		//scion jg -- Allow DrawScale from the emitter to effect this particle
		if (bUseEmitterDrawScale3D)
		{
			// Get the draw scale 3D size for the Emitter->Owner
			Particle.Size *= Owner->DrawScale3D;
		}
		//scion jg -- Adjust scale from the owning emitter if set
		if (Owner->bUseDrawScaleOnEmitters)
		{
			// Get the draw scale for the Emitter->Owner
			Particle.Size *= Owner->DrawScale;
		}


		// Velocity scale.
		if ( UseVelocityScale && Particle.MaxLifetime )
			{
				FLOAT VelocityRelativeTime = appFractional( RelativeTime * (VelocityScaleRepeats+1) );
				for (INT n=0; n<VelocityScale.Num(); n++)
				{
					if ( VelocityScale(n).RelativeTime >= VelocityRelativeTime )
					{
						FVector V1,
								V2 = VelocityScale(n).RelativeVelocity;
						FLOAT	R1,
								R2 = VelocityScale(n).RelativeTime;
						if (n)
						{
							V1 = VelocityScale(n-1).RelativeVelocity;
							R1 = VelocityScale(n-1).RelativeTime;
						}
						else
						{
							V1 = FVector(1.f,1.f,1.f);
							R1 = 0.f;
						}
						FLOAT A;
						if ( R2 )
							A = (VelocityRelativeTime - R1) / (R2 - R1);
						else
							A = 1;

						// Interpolate between two scales.
					Particle.VelocityMultiplier = Lerp(V1, V2, A);
						break;
					}	
				}
			}	

		// Revolution scale.
		if( UseRevolution &&
			UseRevolutionScale && 
			Particle.MaxLifetime )
			{
				FLOAT RevolutionRelativeTime = appFractional( RelativeTime * (RevolutionScaleRepeats+1) );
				for (INT n=0; n<RevolutionScale.Num(); n++)
				{
					if ( RevolutionScale(n).RelativeTime >= RevolutionRelativeTime )
					{
						FVector V1,
								V2 = RevolutionScale(n).RelativeRevolution;
						FLOAT	R1,
								R2 = RevolutionScale(n).RelativeTime;
						if (n)
						{
							V1 = RevolutionScale(n-1).RelativeRevolution;
							R1 = RevolutionScale(n-1).RelativeTime;
						}
						else
						{
							V1 = FVector(1.f,1.f,1.f);
							R1 = 0.f;
						}
						FLOAT A;
						if ( R2 )
							A = (RevolutionRelativeTime - R1) / (R2 - R1);
						else
							A = 1;

						// Interpolate between two scales.
					Particle.RevolutionsMultiplier = Lerp(V1, V2, A);
						break;
					}	
				}
			}	

		// Color scaling.
		if ( UseColorScale && Particle.MaxLifetime )
		{
			FLOAT ColorRelativeTime	= appFractional( RelativeTime * (ColorScaleRepeats+1) );
			FLOAT R1,R2,A;
			FColor *C1,*C2;
			static FColor ResultColor;
			for (INT n=0; n<ColorScale.Num(); n++)
			{
				if ( ColorScale(n).RelativeTime >= ColorRelativeTime )
				{
					C2 = &ColorScale(n).Color;					
					if (n)
					{
						C1 = &ColorScale(n-1).Color;
						R1 = ColorScale(n-1).RelativeTime;
					}
					else
					{
						static FColor AllWhite(255,255,255,255);
						C1 = &AllWhite;
						R1 = 0.f;
					}

					R2  = ColorScale(n).RelativeTime;
					A = R2 ? (ColorRelativeTime - R1) / (R2 - R1) : 1.f;
	
					// Interpolate between two colors.
					ResultColor.Lerp( *C1, *C2, A );
					Color.X = ResultColor.R*ONE_OVER_255;
					Color.Y = ResultColor.G*ONE_OVER_255;
					Color.Z = ResultColor.B*ONE_OVER_255;
					if ( DrawStyle == PTDS_AlphaBlend )
						Color.W = ResultColor.A*ONE_OVER_255;
					else
						Color.W = 1.f;

					break;			
				}
			}	
		}
		
		Color.X *= Particle.ColorMultiplier.X;
		Color.Y *= Particle.ColorMultiplier.Y;
		Color.Z *= Particle.ColorMultiplier.Z;
		
		// Fade In/ Out.
		if ( (FadeOut && (Time > FadeOutStartTime) && (Particle.MaxLifetime != FadeOutStartTime))
		||   (FadeIn  && (Time < FadeInEndTime)    && FadeInEndTime)
			)
		{
			FLOAT FadeFactor;
			FPlane MaxFade;

			if ( FadeOut && (Time > FadeOutStartTime) )
			{
				FadeFactor = Time - FadeOutStartTime;
				FadeFactor /= (Particle.MaxLifetime - FadeOutStartTime);
				MaxFade = FadeOutFactor;
			}
			else
			{
				
				FadeFactor = FadeInEndTime - Time;
				FadeFactor /= FadeInEndTime;
				MaxFade = FadeInFactor;
			}
			
			if ( DrawStyle == PTDS_Modulated )
			{
				Color.X = 0.5f;
				Color.Y = 0.5f;
				Color.Z = 0.5f;
				Color.W = 1.f - (FadeFactor * MaxFade.W);
			}
			else if ( DrawStyle == PTDS_AlphaBlend )
			{
				Color.W -= FadeFactor * MaxFade.W;
			}
			else
			{
				Color -= FadeFactor * MaxFade;
			}
			
		}

		// Added by Demiurge (PSE)
		if(Opacity < 1.f && DrawStyle != PTDS_Regular) //don't do Opacity for Regular blend mode
		{
			if ( DrawStyle == PTDS_AlphaBlend ||
				 DrawStyle == PTDS_Modulated ||
				 DrawStyle == PTDS_AlphaModulate_MightNotFogCorrectly)
			{
				 Color.W *= Opacity;
			}
			else
			{
				 Color.X *= Opacity;
				 Color.Y *= Opacity;
				 Color.Z *= Opacity;
			}
		}
		// End add by Demiurge (PSE)

#ifdef _XBOX //scion jg -- Merged in Dan's FPlane to FColor optimization
		{
			F32vec4 Dummy	=	_mm_mul_ps
								(
									_mm_loadu_ps(&Color.X)
									,
									_mm_load_ps1(&FLOAT_255)
								);

			Particle.Color	=	_mm_packs_pu16
								(
									_mm_cvtps_pi16_EPIC
									(
										_mm_max_ps
										(
											_mm_min_ps
											(
												_mm_shuffle_ps( Dummy, Dummy, _MM_SHUFFLE(3, 0, 1, 2)) //  B G R A
												,
												_mm_load_ps1(&FLOAT_255)
											)
											,
											_mm_setzero_ps()
										)							
									)
									,
									_mm_setzero_si64()
								).m64_u32[0];
			_mm_empty();
		}
#else
		Particle.Color = Color;
#endif

		// Bounding box creation.
		//scion jg -- Changed the relative positioning in the particle system
		BoundingBox += Particle.Location;

		// Clamping velocity.
		if ( MaxAbsVelocity.X )
			Particle.Velocity.X = Clamp( Particle.Velocity.X, -MaxAbsVelocity.X, MaxAbsVelocity.X );
		if ( MaxAbsVelocity.Y )
			Particle.Velocity.Y = Clamp( Particle.Velocity.Y, -MaxAbsVelocity.Y, MaxAbsVelocity.Y );
		if ( MaxAbsVelocity.Z )
			Particle.Velocity.Z = Clamp( Particle.Velocity.Z, -MaxAbsVelocity.Z, MaxAbsVelocity.Z );

		// Friction.
		Particle.Velocity -= Particle.Velocity * VelocityLossRange.GetRand() * DeltaTime;

		// Don't tick if particle is no longer moving.
		if (Collided && MinSquaredVelocity && ( Particle.Velocity.SizeSquared() < MinSquaredVelocity ) )
			Particle.Flags |= PTF_NoTick;

		// Used by trail emitter e.g.
		UpdateParticle( DeltaTime, Index );
	}

	// Account for SizeScale when expanding bounding box.
	FLOAT MaxScale = 1.f;
	if ( UseSizeScale && !UseRegularSizeScale )
	{
		for (INT i=0; i<SizeScale.Num(); i++)
			MaxScale = Max( SizeScale(i).RelativeSize, MaxScale );
	}
	
	// Subclasses use this to expand bounding box accordingly.
	MaxSizeScale = MaxScale;

	// Ugh, this is getting ugly. Subtle assumptions because of indirect spawning.
	AllParticlesDead =
		((DeadParticles >= MaxActiveParticles || ActiveParticles == DeadParticles) &&
		Rate == 0 && !RespawnDeadParticles);

	return ActiveParticles - DeadParticles;
}


INT UParticleEmitter::RenderParticles( FDynamicActor* DynActor, FLevelSceneNode* SceneNode, TList<FDynamicLight*>* Lights, FRenderInterface* RI )
{
	Inactive=false;
	return 0;
}

void UParticleEmitter::Serialize( FArchive& Ar )
{
	guard(UParticleEmitter::Serialize);

#ifndef _XBOX
	if (GIsEditor && Ar.IsSaving())
	{
		//epic moelfke: make sure our Outer is the level, not an emitter actor inside it
		AEmitter* Emitter = Cast<AEmitter>(GetOuter());
		if (Emitter && Emitter->GetLevel())
		{
			Rename(NULL, Emitter->GetLevel());
		}
		//epic moelfke: if multiple emitters in the same level reference this UParticleEmitter, we must mark it bIsInstanced
		if (!bIsInstanced)
		{
			// get our level
			ULevel* MyLevel = Cast<ULevel>(GetOuter());
			if (MyLevel)
			{
				INT Refs = 0;
				// search Emitters in the level for references to us
				for (INT i = 0; i < MyLevel->Actors.Num(); i++)
				{
					AEmitter* Emitter = Cast<AEmitter>(MyLevel->Actors(i));
					if (Emitter)
					{
						for (INT j = 0; j < Emitter->Emitters.Num(); j++)
						{
							if (Emitter->Emitters(j) == this)
							{
								Refs++;
							}
						}
					}
				}

				// if there were multiple references, bIsInstanced should be true
				if (Refs > 1)
				{
					warnf(NAME_Warning, TEXT("%s referenced by multiple Emitters, setting bIsInstanced to true"), GetFullName());
					bIsInstanced = true;
				}
			}
		}
	}
#endif

	Super::Serialize( Ar );	

	if( !Ar.IsLoading() && !Ar.IsSaving() )
	{
        Ar	<< Particles;

// scion sz - not used
#if 0
		Ar	<< MeshVertsAndNormals;
#endif

		//jg -- Make sure to account for those expensive FParticles
		for (INT Index = 0; Index < Particles.Num(); Index++)
		{
			if (Particles(Index) != 0)
			{
				Ar << *(FParticle*)Particles(Index);
			}
		}
	}

	unguard;
}


/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

