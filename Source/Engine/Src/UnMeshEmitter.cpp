/*=============================================================================
	UMeshEmitter.cpp: Unreal Mesh Emitter
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Daniel Vogel
=============================================================================*/

#include "EnginePrivate.h"

/*-----------------------------------------------------------------------------
	UMeshEmitter.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(UMeshEmitter);

void UMeshEmitter::PostEditChange()
{
	guard(UMeshEmitter::PostEditChange);
	Super::PostEditChange();
	CleanUp();
	Initialize( MaxParticles );
	unguard;
}

// Initializes the Emitter.
void UMeshEmitter::Initialize( INT InMaxParticles )
{
	guard(UMeshEmitter::Initialize);
	Super::Initialize( InMaxParticles );
	if ( StaticMesh )
	{
		MeshExtent.X = Max( StaticMesh->BoundingBox.Min.X, StaticMesh->BoundingBox.Max.X );
		MeshExtent.Y = Max( StaticMesh->BoundingBox.Min.Y, StaticMesh->BoundingBox.Max.Y );
		MeshExtent.Z = Max( StaticMesh->BoundingBox.Min.Z, StaticMesh->BoundingBox.Max.Z );
		MeshExtent -= StaticMesh->BoundingBox.GetCenter();
	}
	RealExtentMultiplier = ExtentMultiplier * MeshExtent;
	unguard;
}

// Update active particles.
INT UMeshEmitter::UpdateParticles( FLOAT DeltaTime )
{
	guard(UMeshEmitter::UpdateParticles);
	INT Value = Super::UpdateParticles( DeltaTime );

	// scion sz - relative emitters' bboxes are transformed during AEmitter::Tick()
	// so don't use the world position of the owner
	if( !BoundingBox.IsValid() )
	{
		BoundingBox += (CoordinateSystem == PTCS_Relative) ? FVector(0,0,0) : Owner->Location;
	}

	FLOAT ExpandBy = MaxSizeScale;
	if( UniformSize )
		ExpandBy *= StartSizeRange.X.GetMax();
	else
		ExpandBy *= Max3(StartSizeRange.X.GetMax(), StartSizeRange.Y.GetMax(), StartSizeRange.Z.GetMax());

	BoundingBox = BoundingBox.ExpandBy( ExpandBy * Max3( MeshExtent.X, MeshExtent.Y, MeshExtent.Z ) );
	if( Owner )
		BoundingBox = BoundingBox.ExpandBy( Owner->PrePivot.Size() * ExpandBy );

	return Value;
	unguard;
}

// Render particles.
//!! TODO: retrive lights for bounding box!
INT UMeshEmitter::RenderParticles(FDynamicActor* DynActor,FLevelSceneNode* SceneNode,TList<FDynamicLight*>* Lights,FRenderInterface* RI)
{
	guard(UMeshEmitter::RenderParticles);

	UParticleEmitter::RenderParticles( DynActor, SceneNode, Lights, RI );

	if ( StaticMesh == NULL )
		return 0;

	//scion jg -- Changed how this is determined so that lighting could be
	// enabled separately from the mesh blending mode
	INT UseMeshLighting = bUnlit == 0 &&
		!SceneNode->Viewport->IsWire() && SceneNode->Viewport->IsLit();

	FSphere BoundingSphere = FSphere(&BoundingBox.Min,2);
	RI->EnableLighting(UseMeshLighting, 0, UseMeshLighting, NULL, SceneNode->Viewport->Actor->RendMap == REN_LightingOnly, BoundingSphere );

	// mask emitter from berserk
	UBOOL bUseStencil = bMaskBerserk && RI->IsDefaultRenderTarget();
	if( bUseStencil )
	{
		RI->SetStencilOp(CF_Always,0xBE,0xFFFFFFFF,SO_Keep,SO_Keep,SO_Replace,
			0xFF);
	}

	if( UseMeshLighting )
	{
		// Need dynamic lighting information.
		Owner->bUnlit = false;

		// Set ambient light.
		RI->SetAmbientLight( DynActor->AmbientColor );

		// Find lights affecting this particle system.
		INT	NumHardwareLights = 0;
		for(TList<FDynamicLight*>* LightList = Lights;LightList;LightList = LightList->Next)
			RI->SetLight(NumHardwareLights++,LightList->Element);
	}

	FVertexStream*	VertexStreams[9] = { &StaticMesh->VertexStream };
	INT				NumVertexStreams = 1;

	for(INT UVIndex = 0;UVIndex < StaticMesh->UVStreams.Num();UVIndex++)
		VertexStreams[NumVertexStreams++] = &StaticMesh->UVStreams(UVIndex);

#ifdef _XBOX//jg -- All of these changes are due to the compression
	RI->SetVertexStreams(VS_CompressedMesh,VertexStreams,NumVertexStreams);
	// Set the compression scale as a constant
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
	// No indices used
	RI->SetIndexBuffer(NULL,0);
#else
	RI->SetVertexStreams(0,VertexStreams,NumVertexStreams);
	RI->SetIndexBuffer(&StaticMesh->IndexBuffer,0);
#endif

	INT RenderedParticles = 0;
	for (INT SectionIndex=0; SectionIndex<StaticMesh->Sections.Num(); SectionIndex++)
	{
		//scion jg -- Keep track of the passes for accurate triangle counts
		INT NumPasses = 0;
		FStaticMeshSection&	Section	 = StaticMesh->Sections(SectionIndex);

		UMaterial*			Material = NULL;
		if( SectionIndex > -1 && SectionIndex < Skins.Num() )
		{
			Material = Skins(SectionIndex);
		}
		if( !Material )
		{
			// levels already have mesh emitters that use the actor's skins array, so we should still fallback to this
			Material = StaticMesh->GetSkin(Owner,SectionIndex);			
		}
		if (!Material)
		{
			Material = CastChecked<UMaterial>(UMaterial::StaticClass()->GetDefaultObject())->DefaultMaterial;
		}
		
		if( Section.NumPrimitives == 0 )
			continue;

		if( !UseMeshBlendMode )
		{
			Owner->ParticleMaterial->ParticleBlending			= DrawStyle;
			if( SceneNode->Viewport->Actor->RendMap == REN_LightingOnly )
				Owner->ParticleMaterial->BitmapMaterial			= NULL;
			else
				Owner->ParticleMaterial->BitmapMaterial			= Cast<UBitmapMaterial>(Material);
			Owner->ParticleMaterial->BlendBetweenSubdivisions	= 0;
			Owner->ParticleMaterial->RenderTwoSided				= RenderTwoSided;
			Owner->ParticleMaterial->UseTFactor					= 1;
			Owner->ParticleMaterial->AlphaTest					= AlphaTest;
			Owner->ParticleMaterial->AlphaRef					= AlphaRef;
			Owner->ParticleMaterial->AcceptsProjectors			= AcceptsProjectors;
			Owner->ParticleMaterial->ZTest						= ZTest;
			Owner->ParticleMaterial->ZWrite						= ZWrite;
			Owner->ParticleMaterial->Wireframe					= SceneNode->Viewport->IsWire();

			NumPasses = RI->SetMaterial(Owner->ParticleMaterial);
		}
		else
		{
			// Set texture and blending.
			DECLARE_STATIC_UOBJECT( UColorModifier, ColorModifier, {} );
#ifndef _XBOX
			if(SceneNode->Viewport->IsWire())
			{
				DECLARE_STATIC_UOBJECT( UShader, WireframeShader, { WireframeShader->Wireframe = 1; });
				NumPasses = RI->SetMaterial( WireframeShader );
			}
			else
#endif
			{
				if( UseParticleColor )
				{
					ColorModifier->Material = Material;
					NumPasses = RI->SetMaterial( ColorModifier );
				}
				else
					NumPasses = RI->SetMaterial( Material );
			}
		}

		INT Index = 0;
		for (INT n = 0; n < ActiveParticles && Index < Particles.Num(); n++, Index++)
		{
			FParticle* Particle = (FParticle*)Particles(Index);
			if (Particle == NULL || !(Particle->Flags & PTF_Active))
			{
				continue;
			}
			RenderedParticles++;

			FMatrix	LocalToWorld;
			FVector Location = Particle->Location;
			FRotator Rotation(0,0,0);

			if( SpinParticles )
			{
				Rotation.Yaw	= (INT) (Particle->StartSpin.X + Particle->Time * Particle->SpinsPerSecond.X);
				Rotation.Pitch	= (INT) (Particle->StartSpin.Y + Particle->Time * Particle->SpinsPerSecond.Y);
				Rotation.Roll	= (INT) (Particle->StartSpin.Z + Particle->Time * Particle->SpinsPerSecond.Z);
			}
			else
			{
				// scion sz - handle user specified rotations
				switch( UseRotationFrom )
				{
				case PTRS_Actor:
					{
						if( CoordinateSystem != PTCS_Relative )
						{
							Rotation = Owner->Rotation;
						}
					}
					break;
				case PTRS_Offset:
					{
						Location = Location.TransformVectorBy(GMath.UnitCoords*RotationOffset);
					}
					break;
				case PTRS_Normal:
					{
						Rotation = RotationNormal.Rotation();
					}
					break;
				}
			}

			if(CoordinateSystem == PTCS_Relative )
			{
					LocalToWorld = 	FTranslationMatrix(-Owner->PrePivot) * 
									FScaleMatrix(Particle->Size) *
									FRotationMatrix(Rotation) * 
									FTranslationMatrix(Location) *
									FRotationMatrix(Owner->Rotation) * 
									FTranslationMatrix(Owner->Location + Owner->PrePivot + ((Owner->Base && Owner->AttachmentBone == NAME_None) ? Owner->Base->PrePivot : FVector(0,0,0)) );
			}
			else
			{
					LocalToWorld =	FTranslationMatrix(-Owner->PrePivot) * 
									FScaleMatrix(Particle->Size) *
									FRotationMatrix(Rotation) * 
									FTranslationMatrix(Location + Owner->PrePivot + ((Owner->Base && Owner->AttachmentBone == NAME_None) ? Owner->Base->PrePivot : FVector(0,0,0)) );
			}
			
			RI->SetTransform(TT_LocalToWorld,LocalToWorld);
			RI->SetGlobalColor(Particle->Color);

			BEGIN_D3D_EVENT(BDE)(PIX_PARTICLE,TEXT("MeshEmit %s Sect(%d)"),
				GetName(),SectionIndex);
#ifdef _XBOX
			//scion jg -- Indices are gone so use the push buffer to render
			RI->RunPushBuffer(Section.pPushBuffer,1);
			STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_PushBuffer_RunPushBufferSMTris),
				Section.NumTriangles * NumPasses);
#else
			RI->DrawPrimitive(
				Section.IsStrip ? PT_TriangleStrip : PT_TriangleList,
				Section.FirstIndex,
				Section.NumPrimitives,
				Section.MinVertexIndex,
				Section.MaxVertexIndex
				);
#endif
			END_D3D_EVENT;
			//scion jg -- Keep track of the number of static mesh triangles drawn
			STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_StaticMesh_Triangles),
				Section.NumTriangles * NumPasses);

			Particle = (FParticle*)Particles(Index++);
		}
	}

	// reset masked stencil 
	if( bUseStencil )
	{
		RI->ResetStencilOp();
	}

	return RenderedParticles;

	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

