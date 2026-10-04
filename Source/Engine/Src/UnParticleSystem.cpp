/*=============================================================================
	UnParticleSystem.cpp: Unreal Particle System
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Daniel Vogel
=============================================================================*/

#include "EnginePrivate.h"

/*-----------------------------------------------------------------------------
	AEmitter.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(AEmitter);
IMPLEMENT_CLASS(UParticleMaterial);

// Initialize.
void AEmitter::Initialize()
{
	if( !Initialized )
	{
		ParticleMaterial	= ConstructObject<UParticleMaterial>(UParticleMaterial::StaticClass());
		GlobalOffset		= GlobalOffsetRange.GetRand();
		TimeTillReset		= TimeTillResetRange.GetRand();
		Initialized			= 1;	
		OldLocation			= Location; // Laurent -- for particles interpolation
		BoundingBox.Init();
	}
}

// Check for projectors hitting the emitter.
INT AEmitter::CheckForProjectors()
{
	guard(AEmitter::CheckForProjectors);

	INT ProjIndex = 0;

	// Check the collision hash for projectors touching this emitter.
	guard(FindProjectors)
	if(Level->XLevel->Hash)
	{
		FMemMark Mark(GMem);
		FCheckResult* Hit = Level->XLevel->Hash->ActorPointCheck( GMem, Location, BoundingBox.GetExtent(), TRACE_Projectors, 0 );
		for( ;Hit; Hit=Hit->GetNext() )
		{
			AProjector* Projector = Cast<AProjector>(Hit->Actor);
			// Add to Projectors list in ParticleMaterial.
			if( Projector 
			&& Projector->bProjectParticles
			&& ((Projector->ProjectTag==NAME_None) || (Tag==Projector->ProjectTag)) 
			)
				Projector->UpdateParticleMaterial( ParticleMaterial, ProjIndex++ );
		}
		Mark.Pop();
	}
	unguard;

	// Limit to 4 projectors at this time.
	ParticleMaterial->NumProjectors = Clamp<INT>( ProjIndex, 0, 4 );
	return ParticleMaterial->NumProjectors;

	unguard;
}

// Check whether effect is dynamic
void AEmitter::Spawned()
{
	Super::Spawned();
	if( !GIsEditor )
		DeleteParticleEmitters = 1;
}

// PostScriptDestroyed
void AEmitter::PostScriptDestroyed()
{
	guard(AEmitter::Destroy);
#if 1
	if( DeleteParticleEmitters )
	{
		for (INT i=0; i<Emitters.Num(); i++)
		{
			if (Emitters(i) && (!Emitters(i)->bIsInstanced || !bNoDelete))
			{
				delete Emitters(i);
				Emitters(i) = NULL;
			}
		}
		delete ParticleMaterial;
		ParticleMaterial = NULL;
	}
#endif
	Super::PostScriptDestroyed();
	unguard;
}

// Kill - shutdown a particle system and make it auto-destroy.
void AEmitter::Kill()
{
	guard(AEmitter::Kill);
	for( INT i=0;i<Emitters.Num();i++ )
	{
		if( Emitters(i) )
		{
			Emitters(i)->AutomaticInitialSpawning	= 0;
			Emitters(i)->RespawnDeadParticles		= 0;
			Emitters(i)->InitialParticlesPerSecond	= 0;
			Emitters(i)->ParticlesPerSecond			= 0;
			Emitters(i)->AutoReset					= 0;
			Emitters(i)->AutoDestroy				= 0;
			Emitters(i)->KillPending				= 1;
			//scion jg -- Handle sub-emitters that need to be disabled when kill
			// pending is true
			Emitters(i)->Disabled = Emitters(i)->DisableWhenKillPending;

			if( Emitters(i)->FadeWhenKillPending && Emitters(i)->FadeOut )
			{
				for( INT j = 0; j < Emitters(i)->Particles.Num(); j++ )
				{
					if (Emitters(i)->Particles(j) != NULL)
					{
						FParticle* Particle = (FParticle*)Emitters(i)->Particles(j);
						Particle->Time = Emitters(i)->FadeOutStartTime;
					}
				}
			}
		}
	}
	AutoDestroy = 1;
	AutoReset	= 0;

#if 0
	//scion jg -- For debugging kill pending problems
	debugf(TEXT("KillPending for %s"),GetName());
#endif
	unguard;
}

void AEmitter::Reset()
{
	guard(AEmitter::Reset);
	for( INT i = 0; i < Emitters.Num(); i++ ) {
		if( Emitters(i) ) {
			Emitters(i)->Reset();
		}
	}
	unguard;
}

void AEmitter::Die()
{
	guard(AEmitter::Die);
	for( INT i = 0; i < Emitters.Num(); i++ ) {
		if( Emitters(i) ) {
			Emitters(i)->Die();
		}
	}
	
	unguard;
}


// Script execKill();
void AEmitter::execKill( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AEmitter::execKill);
	P_FINISH;
	Kill();
	unguardexecSlow;
}


void AEmitter::execReset( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AEmitter::execReset);
	P_FINISH;
	
	Reset();
	
	unguardexecSlow;
}

void AEmitter::execDie( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AEmitter::execDie);
	P_FINISH;
	
	Die();
	
	unguardexecSlow;
}



// Global tick
UBOOL AEmitter::Tick( FLOAT DeltaTime, enum ELevelTick TickType )
{
	guard(AEmitter::Tick);

	FUNC_CLOCK(PartTick,GStats.DWORDStats(GEngineStats.STATS_Game_ParticleTickCycles));

	if ( !Super::Tick(DeltaTime, TickType) )
		return false;

	if( !Initialized )
		Initialize();

	// Don't tick if ViewportsOnly or if actor just got deleted.
	// scion sz - don't tick if hidden
	if (bDeleteMe || 
		((TickType == LEVELTICK_ViewportsOnly) && !GIsEditor) ||		
		bHidden )
	{
		return true;
	}

	// Initialize bounding box.
	BoundingBox.Init();

	UBOOL AllDead			= true;
	UBOOL AllDisabled		= true;
	UseParticleProjectors	= false;

	// Route the ticking.
	for (INT i=0; i<Emitters.Num(); i++)
	{
		UParticleEmitter* Emitter = Emitters(i);

		// scion sz - don't tick subemitters if their detail level is too high
		if (!Emitter || Emitter->Disabled || Emitter->DetailLevel > Level->DetailMode  )
			continue;

		// Do we need to check for projectors?
		UseParticleProjectors |= Emitter->AcceptsProjectors;

		// This one is alive.
		AllDisabled = false;

		// In case it has been imported via T3D (cut & paste)
		if (!Emitter->Initialized)
		{
			// scion sz -
			// use attenuation factor to scale max particles in newly spawned subemitters
			Emitter->AttenMaxParticles( Level->SpawnParticleAtten );
		}

		// Set the owner.
		Emitter->Owner = this;

		// Set fogging behaviour.
		Emitter->RealDisableFogging = Emitter->DisableFogging  || DisableFogging;

		// Handle initial delay.
		if( Emitter->InitialDelay ) {
			Emitter->InitialDelay -= DeltaTime;
			if ( Emitter->InitialDelay > 0.f ) {
				//epic superville: Not all dead if one hasn't started creating emitters yet
				AllDead	= false;
				continue;
			}
			else {
				Emitter->InitialDelay = 0.f;
			}
		}
		
		// Handle global offset.
		Emitter->GlobalOffset = GlobalOffset;

		// scion sz - update relative rotations
		Emitter->UpdateRelativeRot(0);

		// Prime the particle system.
		if ( !Emitter->WarmedUp && Emitter->WarmupTicksPerSecond && Emitter->RelativeWarmupTime )
		{
			FLOAT PrimeTime = Emitter->LifetimeRange.GetCenter() * Emitter->RelativeWarmupTime;
			FLOAT DeltaTime = 1.f / Emitter->WarmupTicksPerSecond;
			for ( INT i=0; i < appTrunc(Emitter->WarmupTicksPerSecond * PrimeTime); i++)
				Emitter->UpdateParticles( DeltaTime );
			Emitter->WarmedUp = true;
		}

		// Local tick.
		if ( !Emitter->AllParticlesDead )
		{
			if (Emitter->Inactive)
				Emitter->InactiveTime += DeltaTime;
			else
				Emitter->InactiveTime = 0;

			// Don't tick if ParticleEmitter hasn't been visible for SecondsBeforeInactive seconds.
			// scion sz -- let particles tick if they should be autodestroyed
			if( AutoDestroy || !(Emitter->SecondsBeforeInactive && (Emitter->InactiveTime > Emitter->SecondsBeforeInactive)) )
			{
				// scion sz - try to cull some of the currently active particles 
				Emitter->SetDynamicAtten( Level->DynamicParticleAtten );
				// update
				Emitter->UpdateParticles( DeltaTime );
			}
	
			OldLocation = Location;	// Laurent -- for particles interpolation

			Emitter->Inactive	= true;
			AllDead				= false;
		}
		else if ( Emitters(i)->AutoReset && !AutoReset )
		{
			if ( (Emitters(i)->TimeTillReset-DeltaTime) > 0 )
			{
				Emitters(i)->TimeTillReset -= DeltaTime;
			}
			else
			{
				Emitters(i)->TimeTillReset = Emitters(i)->AutoResetTimeRange.GetRand();
				Emitters(i)->Reset();
			}
		}
		// AutoDestroy ParticleEmitter.
		else if ( Emitters(i)->AutoDestroy && !AutoDestroy )
		{
			Emitters(i)->Disabled = true;
		}

		// Expand bounding box.
		//scion jg -- Rather than transforming each relative particle, just
		// transform the box once
		if (Emitter->CoordinateSystem == PTCS_Relative)
		{
			FRotator& RelativeRot = (Emitter->bUseRotationOverride) ?
				Emitter->RotationOverride : Rotation;
			BoundingBox += Emitter->BoundingBox.TransformBy(
				FRotationMatrix(RelativeRot) * FTranslationMatrix(Location));
		}
		else
		{
            BoundingBox += Emitter->BoundingBox;
		}

		// TODO Volume Forces.
	}

	//scion superville: If no sub emitters set to auto destroy
	if( Emitters.Num() == 0 ) {
		AutoDestroy = 1;
		AllDisabled = 0;
		AllDead		= 1;
	}

	// AutoDelete Emitter - Emitters.Num() > 0 as ParticleEmitters don't get auto deleted
	// if global AutoDelete is true.
	//scion superville: Destroy if all are dead even if all are disabled
	if ( !GIsEditor && AllDead && AutoDestroy && bNoDelete == 0)
	{
			GetLevel()->DestroyActor( this );
			return true;
	}

	// AutoReset Emitter.
	if ( AllDead && AutoReset && !AutoDestroy )
	{
		if ( (TimeTillReset-DeltaTime) > 0 )
		{
			TimeTillReset -= DeltaTime;
		}
		else
		{
			GlobalOffset	= GlobalOffsetRange.GetRand();
			TimeTillReset	= TimeTillResetRange.GetRand();
			for (INT i=0; i<Emitters.Num(); i++)
				Emitters(i)->Reset();
		}
	}

	// Pass updated bounding box to visibility code.
	ClearRenderData();

	return true;
	unguard;
}

void AEmitter::Render(FDynamicActor* DynActor,FLevelSceneNode* SceneNode,TList<FDynamicLight*>* Lights,FRenderInterface* RI)
{
	guard(AEmitter::Render);

	if( !Initialized )
	{
		Initialize();
		return;
	}

	// Don't render if deleted.
	if( bDeleteMe )
		return;	

#ifndef FINAL_RELEASE
	INT	RenderedParticles = 0;
#endif
	FUNC_CLOCK(PartRen,GStats.DWORDStats( GEngineStats.STATS_Particle_RenderCycles ));
	// Check for projectors if needed.
	if( UseParticleProjectors )
		CheckForProjectors();

	// scion sz - emitters should not have any ambient
	RI->SetAmbientLight( FColor(0,0,0,0) );

	// Render emitters sequentially.
	UParticleEmitter* Emitter=NULL;
	for (INT i=0; i<Emitters.Num(); i++)
	{
		Emitter = Emitters(i);
		if( !Emitter || 
			!Emitter->Texture || 
			Emitter->Disabled || 
			// scion sz - don't draw subemitters if their detail level is too high
			Emitter->DetailLevel > Level->DetailMode ||
			// scion sz - filter post process sub emitters
			(SceneNode->FilterMode == FSceneNode::Filter_PostProcess && 
				(EPostProcessMaterialType)Emitter->HasPostProcessMaterial == POSTPROCESS_NONE) ||
			(SceneNode->FilterMode != FSceneNode::Filter_PostProcess && 
				(EPostProcessMaterialType)Emitter->HasPostProcessMaterial != POSTPROCESS_NONE) )
		{
			continue;
		}

		// scion sz - filter post process emitters even more
		if( SceneNode->FilterMode == FSceneNode::Filter_PostProcess &&
			RI->GetPostProcessStage() == FRenderInterface::PP_STAGE_RENDER_COLOR &&
			(EPostProcessMaterialType)Emitter->HasPostProcessMaterial != POSTPROCESS_BLEND )
		{
			continue;
		}

#ifndef FINAL_RELEASE			
		if( !(SceneNode->Viewport->Actor->ShowFlags & SHOW_Particles) ) 
			continue;
#endif

		Emitter->Owner = this;
		RI->PushState();
#ifndef FINAL_RELEASE
		RenderedParticles += Emitter->RenderParticles( DynActor, SceneNode, Lights, RI );
#else
		Emitter->RenderParticles( DynActor, SceneNode, Lights, RI );
#endif
		RI->PopState();
	}

#ifndef FINAL_RELEASE
	// Render bounding box.
	if( SceneNode->Viewport->bShowBounds )
	{
		FLineBatcher LineBatcher(RI);
		RI->SetTransform(TT_LocalToWorld, FMatrix::Identity);
		LineBatcher.DrawBox( BoundingBox, FColor(255,255,0) );
	}

	GStats.DWORDStats( GEngineStats.STATS_Particle_Particles ) += RenderedParticles;
#endif
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

