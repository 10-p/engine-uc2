/*=============================================================================
	UnRender.cpp: High-level rendering code.
	Copyright 2001-2002 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/

#include "EnginePrivate.h"
#include "UnRenderPrivate.h"

// scion sz - keeps track of considered lights
TArray<FDynamicLight*> FDynamicLight::MarkedUsed;

//
//	FSceneNode::FSceneNode
//
FSceneNode::FSceneNode(UViewport* InViewport,FRenderTarget* InRenderTarget)
{
	Parent = NULL;
	Viewport = InViewport;
	RenderTarget = InRenderTarget;
	Recursion = 0;
	FilterMode = Filter_All;
}

//
//	FSceneNode::FSceneNode
//
FSceneNode::FSceneNode(FSceneNode* InParent)
{
	Parent = InParent;
	Viewport = Parent->Viewport;
	RenderTarget = Parent->RenderTarget;
	Recursion = Parent->Recursion + 1;

	WorldToCamera = InParent->WorldToCamera;
	CameraToScreen = InParent->CameraToScreen;
	ScreenToCamera = InParent->ScreenToCamera;
	CameraToWorld = InParent->CameraToWorld;

	WorldToScreen = InParent->WorldToScreen;
	ScreenToWorld = InParent->ScreenToWorld;

	ViewOrigin = Parent->ViewOrigin;
	CameraX = InParent->CameraX;
	CameraY = InParent->CameraY;

	Determinant = WorldToScreen.Determinant();
	FilterMode = Filter_All;
}

//
//	FSceneNode::~FSceneNode
//
FSceneNode::~FSceneNode()
{
}

//
//	FSceneNode::Project
//
FPlane FSceneNode::Project(FVector V)
{
	FPlane	Result = WorldToScreen.TransformFPlane(FPlane(V,1));
	FLOAT	RHW = 1.0f / Result.W;

	return FPlane(Result.X * RHW,Result.Y * RHW,Result.Z * RHW,Result.W);
}

//
//	FSceneNode::Deproject
//
FVector FSceneNode::Deproject(FPlane P)
{
	return ScreenToWorld.TransformFPlane(FPlane(P.X * P.W,P.Y * P.W,P.Z * P.W,P.W));
}


//
//	FCoronaRender
//
FCoronaRender::FCoronaRender()
{
    Reset();
}

void FCoronaRender::Reset()
{
	Delta		= 0.f;
	LastTime	= 0;
	iFree		= 0;
	iVisible	= 0;
	appMemzero(CoronaLights, sizeof(CoronaLights));
	appMemzero(VisibleLights, sizeof(VisibleLights));
}

void FCoronaRender::AddVisibleLight( AActor* Light )
{
	INT i;
	for( i=0; i<iVisible; i++ )
		if( VisibleLights[i] == Light )
			break;
	if( i == iVisible )
		VisibleLights[iVisible++] = Light;

	iVisible %= MAX_CORONA_LIGHTS;
}

UBOOL FCoronaRender::IsOccluded( FLevelSceneNode* SceneNode, AActor* Actor )
{
	guard(FCoronaRender::IsOccluded);

	UBOOL	Occluded,
			Traced = false;

	INT i;
	for( i=0; i<MAX_CORONA_LIGHTS; i++ )
		if( CoronaLights[i].Actor == Actor )
			break;

	if( i<MAX_CORONA_LIGHTS && ((LastTime - CoronaLights[i].LastTrace) < (appFrand()*0.5f + 0.5f)) )
	{
		Occluded	= CoronaLights[i].LastOccluded;
	}
	else
	{
		FCheckResult Hit;
		Occluded = !Actor->GetLevel()->SingleLineCheck(
						Hit,
						SceneNode->Viewport->Actor->bBehindView ? NULL : SceneNode->ViewActor,
						Actor->Location,
						SceneNode->ViewOrigin,
						//scion jg -- Stripped off the trace pawn flag since
						// the player might occlude the corona but if it does
						// it is most likely somewhat transparent
						TRACE_Corona | TRACE_World | TRACE_StopAtFirstHit,
//						TRACE_Corona | TRACE_World | TRACE_StopAtFirstHit | TRACE_Pawns,
						FVector(0,0,0)
		);
		Traced = true;
	}
	
	if( i<MAX_CORONA_LIGHTS )
	{
		CoronaLights[i].LastOccluded = Occluded;
		if( Traced )
			CoronaLights[i].LastTrace = LastTime;
	}

	return Occluded;
	unguard;
}

void FCoronaRender::AddCoronaLight( FLevelSceneNode* SceneNode, AActor* Actor, FLOAT Delta )
{
	guard(FCoronaRender::AddCoronaLight);
	FUNC_CLOCK(CoronaCyc,GStats.DWORDStats(GEngineStats.STATS_Corona_CollisionCycles));

	if
	(	Actor
	&&	Actor->bCorona
	&&	Actor->Skins.Num()
	&&	Cast<UTexture>(Actor->Skins(0))
	&& !Actor->bDeleteMe
	&& !IsOccluded( SceneNode, Actor )
	)
	{		
		INT i;
		for( i=0; i<MAX_CORONA_LIGHTS; i++ )
			if( CoronaLights[i].Actor == Actor )
				break;
		if( i<MAX_CORONA_LIGHTS )
		{
			//scion jg -- Changed to fade better
			CoronaLights[i].Bright = 0;
		}
		else
		{
			while( iFree<MAX_CORONA_LIGHTS && CoronaLights[iFree].Actor )
				iFree++;
			if( iFree<MAX_CORONA_LIGHTS )
			{
				CoronaLights[iFree].Actor	= Actor;
				CoronaLights[iFree].iActor	= Actor->GetLevel()->GetActorIndex(Actor);
				//scion jg -- Changed to fade better
				CoronaLights[iFree].Bright			= 0;
				CoronaLights[iFree].LastOccluded	= 0;
				CoronaLights[iFree].LastTrace		= LastTime;
			}
		}
	}

	unguard;
}

void FCoronaRender::RenderCoronas( FLevelSceneNode* SceneNode, FRenderInterface* RI )
{
	guard(FCoronaRender::RenderCoronas);
	//scion jg -- Keep track of how expensive these are
	FUNC_CLOCK(RenCyc,GStats.DWORDStats(GEngineStats.STATS_Stats_CoronaRenderCycles));

	FSpriteVertexStream CoronaVertices;

	// Backward compatibility mojo.
	Delta	 = Min<FLOAT>( 3.0f, 8.0f * (SceneNode->Viewport->CurrentTime - LastTime) );
	LastTime = SceneNode->Viewport->CurrentTime;

	// Fade out existing coronas, and remove if they've faded out completely.
	//scion jg -- Changed how the fade is calculated. It is now an accelerating curve
	for( INT i=0; i<MAX_CORONA_LIGHTS; i++ )
		if( CoronaLights[i].Actor && (CoronaLights[i].Bright += ((CoronaLights[i].Bright * 0.5f) + Delta)) >= 1.f )
			CoronaLights[i].Actor = NULL;

	// Update coronas.
	for( INT i=0; i<MAX_CORONA_LIGHTS; i++ )
		AddCoronaLight( SceneNode, VisibleLights[i], Delta );

	// Reset arrays & indices.
	iFree = 0;

	// Coronas shouldn't be fogged.
	RI->SetDistanceFog( 0, 0, 0, 0 );

	// Set identity matrix.
	RI->SetTransform(TT_LocalToWorld,FMatrix::Identity);

	// Render the coronas.
	INT Count = 0;
	for( INT i=0; i<MAX_CORONA_LIGHTS; i++ )
	{
		AActor* Actor = CoronaLights[i].Actor;
		ALight* Light = Cast<ALight>(Actor);

		if(		!Actor
			||	!((CoronaLights[i].iActor <= SceneNode->Viewport->Actor->GetLevel()->Actors.Num()) &&
				(SceneNode->Viewport->Actor->GetLevel()->Actors(CoronaLights[i].iActor)==Actor))
			||	Actor->bDeleteMe
			||	!Actor->Skins.Num()
		)
		{
			CoronaLights[i].Actor = NULL;
			continue;
		}
		
		PUSH_HIT(SceneNode->Viewport,HActor(Actor));

		FVector ProjBase     = SceneNode->Deproject(FPlane(0,0,0,1));	
		FVector ProjUp		 = SceneNode->Deproject(FPlane(0,-1,0,1)) - ProjBase;
		FVector ProjRight	 = SceneNode->Deproject(FPlane(1,0,0,1)) - ProjBase;
		FVector ProjFront	 = ProjRight ^ ProjUp;

		ProjUp.Normalize();
		ProjRight.Normalize();
		ProjFront.Normalize();

		BYTE    H		= Actor->LightHue;
		FVector Hue		= (H<86) ? FVector((85-H)/85.f,(H-0)/85.f,0) : (H<171) ? FVector(0,(170-H)/85.f,(H-85)/85.f) : FVector((H-170)/85.f,0,(255-H)/84.f);
		FLOAT	Alpha	= Actor->LightSaturation / 255.0f;
		//scion jg -- Changed for fade calculation
		FPlane  Color	= (Hue + Alpha * (FVector(1,1,1) - Hue)) * (1.f - CoronaLights[i].Bright);
		FLOAT Distance	= FDist(SceneNode->ViewOrigin,Actor->Location);
		Distance		= Clamp<FLOAT>( Distance / 1000.f, 0.01f, 10.f );

		FVector Right, Up;

		if ( (Actor->DrawScale3D.X != Actor->DrawScale3D.Y) && (Actor->DrawScale3D.Z > 0) )
		{
			// Non square coronas.
			FVector X		= FVector(1,0,0).TransformVectorBy( GMath.UnitCoords * Actor->Rotation );
			FVector Y		= FVector(0,1,0).TransformVectorBy( GMath.UnitCoords * Actor->Rotation );
			FVector Z		= FVector(0,0,1).TransformVectorBy( GMath.UnitCoords * Actor->Rotation );
			FLOAT   Angle	= appSqrt((1 - Abs(ProjFront | X)) * (1 - Abs(ProjFront | Y)));
			Color			= FColor(Color * Angle);
			Right			= X;
			Up				= Y;
		}
		else
		{
			// Square Coronas.
			Right			= ProjRight;
			Up				= ProjUp;
			
			// Rotate corona dependend on distance.
			if( Light && (Light->CoronaRotation || Light->CoronaRotationOffset) )
			{
				FLOAT Angle;
				Angle	= Light->CoronaRotation * 16384.f * Distance + Light->CoronaRotationOffset;
				Right	= Right.RotateAngleAxis( Angle, ProjFront );
				Up		= Up.RotateAngleAxis( Angle, ProjFront );
			}
		}

		FLOAT	CoronaFudgeFactor	= 512.f,
				ScaleX				= Actor->DrawScale * Actor->DrawScale3D.X * Distance * CoronaFudgeFactor,
				ScaleY				= Actor->DrawScale * Actor->DrawScale3D.Y * Distance * CoronaFudgeFactor;

		// Clamp size.
		if( Light )
		{
			ScaleX = Clamp( ScaleX, Light->MinCoronaSize, Light->MaxCoronaSize );
			ScaleY = Clamp( ScaleY, Light->MinCoronaSize, Light->MaxCoronaSize );
		}

		Right	*= ScaleX;
		Up		*= ScaleY;

		FColor RenderColor	= Color;
		RenderColor			= RenderColor.RenderColor();
		RenderColor.A		= 255;

		CoronaVertices.Vertices[0].Position	= Actor->Location - Right - Up;
		CoronaVertices.Vertices[0].U		= 0.f;
		CoronaVertices.Vertices[0].V		= 0.f;
		CoronaVertices.Vertices[0].Diffuse	= RenderColor;
			
		CoronaVertices.Vertices[1].Position	= Actor->Location + Right - Up;
		CoronaVertices.Vertices[1].U		= 1.f;
		CoronaVertices.Vertices[1].V		= 0.f;
		CoronaVertices.Vertices[1].Diffuse	= RenderColor;
			
		CoronaVertices.Vertices[2].Position	= Actor->Location + Right + Up;
		CoronaVertices.Vertices[2].U		= 1.f;
		CoronaVertices.Vertices[2].V		= 1.f;
		CoronaVertices.Vertices[2].Diffuse	= RenderColor;
			
		CoronaVertices.Vertices[3].Position	= Actor->Location - Right + Up;
		CoronaVertices.Vertices[3].U		= 0.f;
		CoronaVertices.Vertices[3].V		= 1.f;
		CoronaVertices.Vertices[3].Diffuse	= RenderColor;

		// Draw the sprite.
#ifndef _XBOX
		//scion jg -- Only do this on the PC
		INT BaseVertexIndex = RI->SetDynamicStream(VS_FixedFunction,&CoronaVertices);
#endif

		UBOOL AlphaTest = 0;
		UTexture* Texture = Cast<UTexture>(Actor->Skins(0));
		if( Texture && Texture->bAlphaTexture )
			AlphaTest = 1;

		DECLARE_STATIC_UOBJECT( UFinalBlend, FinalBlend, {} );
		FinalBlend->Material			= Actor->Skins(0);
		FinalBlend->TwoSided			= 1;
		FinalBlend->FrameBufferBlending = FB_Translucent;
		FinalBlend->ZWrite				= 0;
		FinalBlend->ZTest				= 0;
		FinalBlend->AlphaTest			= AlphaTest;
		FinalBlend->AlphaRef			= 0;

		RI->EnableLighting(0,1);
		RI->SetMaterial( Light && Light->UseOwnFinalBlend ? Actor->Skins(0) : FinalBlend );

#ifdef _XBOX
		BEGIN_D3D_EVENT(BDE)(PIX_CORONA,TEXT("FCoronaRender"));
		//scion jg -- Use faster rendering method
		RI->DrawPrimitiveUP(VS_FixedFunction,PT_TriangleFan,2,&CoronaVertices);
		END_D3D_EVENT;
#else
		RI->SetIndexBuffer(NULL,0);
		RI->DrawPrimitive(PT_TriangleFan,BaseVertexIndex,2);
#endif
		POP_HIT( SceneNode->Viewport );

		Count++;
	}
	unguard;
}


//
//	DrawSprite
//
static void DrawSprite(AActor* Actor,FLevelSceneNode* SceneNode,FRenderInterface* RI)
{
	guard(DrawSprite);

	// Calculate the sprite right and down vectors.

	FVector	SpriteX = SceneNode->CameraX.SafeNormal() * Actor->DrawScale * Actor->DrawScale3D.X * Actor->Texture->MaterialUSize() / 2.0f,
			SpriteY = -SceneNode->CameraY.SafeNormal() * Actor->DrawScale * Actor->DrawScale3D.Y * Actor->Texture->MaterialUSize() / 2.0f;

	// Calculate the display color.

	FPlane	ColorPlane = FPlane(1.0f,1.0f,1.0f,1.0f);

#ifndef FINAL_RELEASE
	if(GIsEditor)
	{
		if(SceneNode->Viewport->Actor->ShowFlags & SHOW_SelectionHighlight && Actor->bSelected)
			ColorPlane = FPlane(0.5f,0.9f,0.5f,1.0f);
		else if(Actor->LightType != LT_None)
			ColorPlane = FGetHSV(Actor->LightHue,Actor->LightSaturation,255);
	}
#endif

	// Setup the sprite vertices.
	FSpriteVertexStream	SpriteVertices;
	FColor	Color = FColor(ColorPlane);
	FVector	SpriteBase = Actor->Location;

	UMaterial* Material = Actor->Texture;
	UTexture* Texture = Cast<UTexture>(Material);
	if( Texture )
	{
		DECLARE_STATIC_UOBJECT( UFinalBlend, FinalBlend, {} );
		FinalBlend->Material = Texture;
		FinalBlend->TwoSided = 1;

		if( Texture->bMasked || Actor->Style==STY_Masked )
		{
			FinalBlend->FrameBufferBlending = FB_Overwrite;
			FinalBlend->ZWrite				= 1;
			FinalBlend->ZTest				= 1;
			FinalBlend->AlphaTest			= 1;
			FinalBlend->AlphaRef			= 127;
		}
		else
		if( Texture->bAlphaTexture )
		{
			FinalBlend->FrameBufferBlending = FB_AlphaBlend;
			FinalBlend->ZWrite				= 0;
			FinalBlend->ZTest				= 1;
			FinalBlend->AlphaTest			= 1;
			FinalBlend->AlphaRef			= 0;
		}
		else
		if( Actor->Style==STY_Translucent )
		{
			FinalBlend->FrameBufferBlending = FB_Translucent;
			FinalBlend->ZWrite				= 0;
			FinalBlend->ZTest				= 1;
			FinalBlend->AlphaTest			= 0;
		}
		else
		if( Actor->Style==STY_Modulated )
		{
			FinalBlend->FrameBufferBlending = FB_Modulate;
			FinalBlend->ZWrite				= 0;
			FinalBlend->ZTest				= 1;
			FinalBlend->AlphaTest			= 0;
		}
		else
		if( Actor->Style==STY_Additive )
		{
			FinalBlend->FrameBufferBlending = FB_Brighten;
			FinalBlend->ZWrite				= 0;
			FinalBlend->ZTest				= 1;
			FinalBlend->AlphaTest			= 0;
		}
		else
		if( Actor->Style==STY_Subtractive )
		{
			FinalBlend->FrameBufferBlending = FB_Darken;
			FinalBlend->ZWrite				= 0;
			FinalBlend->ZTest				= 1;
			FinalBlend->AlphaTest			= 0;
		}

		Material = FinalBlend;
	}

	SpriteVertices.Vertices[0].Position = SpriteBase - SpriteX - SpriteY;
	SpriteVertices.Vertices[0].Diffuse = Color;
	SpriteVertices.Vertices[0].U = 0.0f;
	SpriteVertices.Vertices[0].V = 0.0f;

	SpriteVertices.Vertices[1].Position = SpriteBase + SpriteX - SpriteY;
	SpriteVertices.Vertices[1].Diffuse = Color;
	SpriteVertices.Vertices[1].U = 1.0f;
	SpriteVertices.Vertices[1].V = 0.0f;

	SpriteVertices.Vertices[2].Position = SpriteBase + SpriteX + SpriteY;
	SpriteVertices.Vertices[2].Diffuse = Color;
	SpriteVertices.Vertices[2].U = 1.0f;
	SpriteVertices.Vertices[2].V = 1.0f;

	SpriteVertices.Vertices[3].Position = SpriteBase - SpriteX + SpriteY;
	SpriteVertices.Vertices[3].Diffuse = Color;
	SpriteVertices.Vertices[3].U = 0.0f;
	SpriteVertices.Vertices[3].V = 1.0f;

	// Draw the sprite.

	INT BaseVertexIndex = RI->SetDynamicStream(VS_FixedFunction,&SpriteVertices);

	RI->SetTransform(TT_LocalToWorld,FMatrix::Identity);
	//!!MAT
	//RI->SetBlending(PolyFlags | PF_TwoSided | Texture->PolyFlags);
	//RI->SetTexture(0,Texture->GetRenderInterface());
	RI->EnableLighting(0,1);
	RI->SetMaterial(Material);
	RI->SetIndexBuffer(NULL,0);

	RI->DrawPrimitive(PT_TriangleFan,BaseVertexIndex,2);

	unguard;
}

//  scion sz -
//	HasEmissiveMaterial
//

UBOOL HasEmissiveMaterial( AActor* Actor )
{
	guard(HasEmissiveMaterial);

	if(Actor->DrawType == DT_StaticMesh && Actor->StaticMesh)
	{
		for(INT MaterialIndex = 0;MaterialIndex < Actor->StaticMesh->Materials.Num();MaterialIndex++)
		{
			UMaterial*	Material = Actor->StaticMesh->GetSkin(Actor,MaterialIndex);
			if( Material && Material->IsEmissive() )
				return 1;
		}
	}
	else if( Actor->IsDTMesh() && Actor->Mesh )
	{
		ULodMesh* LodMesh = (ULodMesh*)Actor->Mesh;
		for(INT MaterialIndex = 0;MaterialIndex < LodMesh->Materials.Num();MaterialIndex++)
		{
			UMaterial* Material = LodMesh->GetSkin( Actor, MaterialIndex );
			if( Material && Material->IsEmissive() )
				return 1;
		}
	}
	else if( Actor->DrawType == DT_Particle )
	{
#if 0
		AEmitter* Emitter = Cast<AEmitter>(Actor);
		UBOOL HasEmissiveMaterial = 0;
		//jg -- Make sure the emitter is valid
		if (Emitter != NULL)
		{
			for( INT SubEmitterIndex=0;
				SubEmitterIndex < Emitter->Emitters.Num() && HasEmissiveMaterial == 0;
				SubEmitterIndex++ )
			{
				UParticleEmitter* SubEmitter = Emitter->Emitters(SubEmitterIndex);

				HasEmissiveMaterial = SubEmitter && 
					SubEmitter->Texture && 
					SubEmitter->Texture->IsEmissive();
			}
			HasEmissiveMaterial |= Emitter->ParticleMaterial && 
				Emitter->ParticleMaterial->BitmapMaterial && 
				Emitter->ParticleMaterial->BitmapMaterial->IsEmissive();
		}
		return HasEmissiveMaterial;
#else
		// just treat all emissive emitters as translucent
		return 0;
#endif
	}
	else if(Actor->DrawType == DT_Sprite && Actor->Texture )
	{
		return 0;
	}
	else if( Actor->DrawType == DT_FluidSurface )
	{
		for(INT SkinIndex = 0;SkinIndex < Actor->Skins.Num();SkinIndex++)
		{
			if( Actor->Skins(SkinIndex) && Actor->Skins(SkinIndex)->IsEmissive() )
			{
				return 1;
			}
		}
	}

	return 0;

	unguardf( (*FString::Printf( TEXT("Actor(%s)"), Actor->GetName())) );
}

//  scion sz -
//	HasPostProcessMaterial
//

EPostProcessMaterialType HasPostProcessMaterial( AActor* Actor )
{
	guard(HasPostProcessMaterial);

	if(Actor->DrawType == DT_StaticMesh && Actor->StaticMesh)
	{
		for(INT MaterialIndex = 0;MaterialIndex < Actor->StaticMesh->Materials.Num();MaterialIndex++)
		{
			UMaterial*	Material = Actor->StaticMesh->GetSkin(Actor,MaterialIndex);
			if( Material )
				return Material->IsPostProcess();
		}
	}
	else if( Actor->IsDTMesh() && Actor->Mesh )
	{
		ULodMesh* LodMesh = (ULodMesh*)Actor->Mesh;
		for(INT MaterialIndex = 0;MaterialIndex < LodMesh->Materials.Num();MaterialIndex++)
		{
			UMaterial* Material = LodMesh->GetSkin( Actor, MaterialIndex );
			if( Material )
				return Material->IsPostProcess();
		}
	}
	else if( Actor->DrawType == DT_Particle && Actor->IsA(AEmitter::StaticClass()) )
	{
		AEmitter* Emitter = Cast<AEmitter>(Actor);

		INT FoundPostProcessMaterial=0;
		for( INT SubEmitterIndex=0; SubEmitterIndex < Emitter->Emitters.Num(); SubEmitterIndex++ )
		{
			UParticleEmitter* SubEmitter = Emitter->Emitters(SubEmitterIndex);

			if( !SubEmitter )
				continue;

			if( SubEmitter->HasPostProcessMaterial )
			{
				FoundPostProcessMaterial=Max(FoundPostProcessMaterial,SubEmitter->HasPostProcessMaterial);
				continue;
			}
				
			if( SubEmitter->Texture )
			{
				SubEmitter->HasPostProcessMaterial = (INT)SubEmitter->Texture->IsPostProcess();
				FoundPostProcessMaterial=Max(FoundPostProcessMaterial,SubEmitter->HasPostProcessMaterial);
				continue;
			}
			
			// for mesh emitters we have to check each static mesh skin
			if( SubEmitter->IsA(UMeshEmitter::StaticClass()) )
			{
                UMeshEmitter* SubMeshEmitter = Cast<UMeshEmitter>( SubEmitter );
				if( !SubMeshEmitter->UseMeshBlendMode &&
					Emitter->ParticleMaterial &&
					Emitter->ParticleMaterial->BitmapMaterial )
				{					
					SubEmitter->HasPostProcessMaterial = (INT)Emitter->ParticleMaterial->BitmapMaterial->IsPostProcess();
					FoundPostProcessMaterial=Max(FoundPostProcessMaterial,SubEmitter->HasPostProcessMaterial);
				}
				else if( SubMeshEmitter->StaticMesh )
				{
					UMaterial* SubMeshMat = SubMeshEmitter->StaticMesh->GetSkin(Actor,0);
					if( SubMeshMat )
					{
						SubEmitter->HasPostProcessMaterial = (INT)SubMeshMat->IsPostProcess();
						FoundPostProcessMaterial=Max(FoundPostProcessMaterial,SubEmitter->HasPostProcessMaterial);
					}
				}
			}
		}

		if( FoundPostProcessMaterial )
			return (EPostProcessMaterialType)FoundPostProcessMaterial;

		if( Emitter->ParticleMaterial && 
			Emitter->ParticleMaterial->BitmapMaterial )
		{
			return Emitter->ParticleMaterial->BitmapMaterial->IsPostProcess();
		}
		else 
		{
			return POSTPROCESS_NONE;
		}
	}
	else if(Actor->DrawType == DT_Sprite && Actor->Texture )
	{
		return Actor->Texture->IsPostProcess();
	}
	else if( Actor->DrawType == DT_FluidSurface )
	{
		for(INT SkinIndex = 0;SkinIndex < Actor->Skins.Num();SkinIndex++)
		{
			if( Actor->Skins(SkinIndex) )
			{
				return Actor->Skins(SkinIndex)->IsPostProcess();
			}
		}
	}

	return POSTPROCESS_NONE;

	unguard;
}

//
//	HasTranslucentMaterial
//

UBOOL HasTranslucentMaterial(AActor* Actor)
{
	guard(RequiresSorting);

	if(Actor->bDisableSorting)
		return 0;

	if(Actor->DrawType == DT_StaticMesh && Actor->StaticMesh)
	{
		for(INT MaterialIndex = 0;MaterialIndex < Actor->StaticMesh->Materials.Num();MaterialIndex++)
		{
			UMaterial*	Material = Actor->StaticMesh->GetSkin(Actor,MaterialIndex);
			if(Material && 
				Material->RequiresSorting() )
				return 1;
		}
	}
	else if( Actor->IsDTMesh() && Actor->Mesh )
	{
		ULodMesh* LodMesh = (ULodMesh*)Actor->Mesh;
		for(INT MaterialIndex = 0;MaterialIndex < LodMesh->Materials.Num();MaterialIndex++)
		{
			UMaterial* Material = LodMesh->GetSkin( Actor, MaterialIndex );
			if( Material && Material->RequiresSorting() )
				return 1;
		}
	}
	else if(Actor->DrawType == DT_Brush ||
			Actor->DrawType == DT_AntiPortal ||
			Actor->DrawType == DT_Particle )
	{
		return 1;
	}
	else if(Actor->DrawType == DT_Sprite && Actor->Texture )
	{
		return 1;
	}
	else if( Actor->DrawType == DT_FluidSurface )
	{
		for(INT SkinIndex = 0;SkinIndex < Actor->Skins.Num();SkinIndex++)
		{
			if( Actor->Skins(SkinIndex) && Actor->Skins(SkinIndex)->RequiresSorting() )
			{
				return 1;
			}
		}
	}

	return 0;

	unguard;
}

// scion sz -
// traverse attached actors recursively. return if a post process material is found
//
UBOOL HasPostProcessMaterialAttachment( AActor* InActor )
{
	UBOOL Found=0;
	for( INT AttachmentIndex = 0; AttachmentIndex < InActor->Attached.Num() && !Found; AttachmentIndex++ )
	{
		if( InActor->Attached(AttachmentIndex) )
		{
			// if a post process material is found return
			if( HasPostProcessMaterial( InActor->Attached(AttachmentIndex) ) )
				return 1;
			else
				Found = HasPostProcessMaterialAttachment( InActor->Attached(AttachmentIndex) );
		}
	}
	return Found;
}

// scion sz -
// traverse attached actors recursively. return if a translucent material is found
//
UBOOL HasTranslucentMaterialAttachment( AActor* InActor )
{
	UBOOL Found=0;
	for( INT AttachmentIndex = 0; AttachmentIndex < InActor->Attached.Num() && !Found; AttachmentIndex++ )
	{
		if( InActor->Attached(AttachmentIndex) )
		{
			// if a post process material is found return
			if( HasTranslucentMaterial( InActor->Attached(AttachmentIndex) ) )
				return 1;
			else
				Found = HasTranslucentMaterialAttachment( InActor->Attached(AttachmentIndex) );
		}
	}
	return Found;
}

// scion sz -
// traverse attached actors recursively. return if a emissive material is found
//
UBOOL HasEmissiveMaterialAttachment( AActor* InActor )
{
	UBOOL Found=0;
	for( INT AttachmentIndex = 0; AttachmentIndex < InActor->Attached.Num() && !Found; AttachmentIndex++ )
	{
		if( InActor->Attached(AttachmentIndex) )
		{
			// if a post process material is found return
			if( HasEmissiveMaterial( InActor->Attached(AttachmentIndex) ) )
				return 1;
			else
				Found = HasEmissiveMaterialAttachment( InActor->Attached(AttachmentIndex) );
		}
	}
	return Found;
}

//
//	FDynamicActor::FDynamicActor
//

FDynamicActor::FDynamicActor(AActor* InActor)
{
	Actor = InActor;
	VisibilityTag = 0;
	Revision = INDEX_NONE;
    PredictedBox.Init();
	NeedsLightUpdate = 1;
	IsPostProcess=POSTPROCESS_NONE;
	IsTranslucent=0;
	IsEmissive=0;
	NeedsSortUpdate=1;
	DistanceLODFactor=1.f;
	// scion sz - used for bUseLightingFromBase=
	ActorLights = NULL;
	ActorProjectors = NULL;
	LastLightUpdateTime = 0.f;

	if( Actor->DrawType == DT_StaticProjectorBatch )
	{
		// don't care about sorting for these
		NeedsSortUpdate=0;
	}

	//jg -- Set initial occlusion state
	// Don't do any occlusion testing in the editor
	bNeedsOcclusionTest = InActor->bUseHWOcclusionTests && GIsEditor == 0;
	bCanUseDeferredOcclusionTest = InActor->bUseDeferredHWOcclusionTests && GIsEditor == 0;
	bIsVisibleInViewport = 0xFF;
	bNeedsDeferredQueryInViewport = 0;
	for (INT Index = 0; Index < MAX_SS_VIEWPORTS; Index++)
	{
		Queries[Index] = NULL;
	}
}

//
//	FDynamicActor::Update
//
	
void FDynamicActor::Update()
{
	guard(FDynamicActor::Update);

	if( Actor->DrawType != DT_StaticProjectorBatch )
	{
		if( Actor->IsDTMesh() && Actor->Mesh )
		{
			// Set up specific mesh-to-world matrix.
			LocalToWorld = Actor->Mesh->MeshGetInstance(Actor)->MeshToWorld();		
		}
		else
		{			
			LocalToWorld = Actor->LocalToWorld();
		}
		// scion sz - not used
		//WorldToLocal = LocalToWorld.Inverse();
		Determinant = LocalToWorld.Determinant();

		if(Actor->DrawType == DT_Particle)
		{			
#ifdef _XBOX
			BoundingBox = ((AEmitter*)Actor)->BoundingBox;
#else
			if( Actor->IsA(AEmitter::StaticClass()) )
			{
				BoundingBox = Cast<AEmitter>(Actor)->BoundingBox;
			}
			else if( Actor->IsA(AxWeatherEffect::StaticClass()) )
			{
				BoundingBox = ((AxWeatherEffect*)Actor)->Box.ExpandBy( 65535.0f );
			}
			if ( GIsEditor )
			{
				BoundingBox += Actor->Location;
				BoundingBox = BoundingBox.ExpandBy( Abs(Actor->DrawScale) * Actor->DrawScale3D.GetAbsMax() );
			}
#endif
			BoundingSphere = FSphere(&BoundingBox.Min,2);
		}
		else 
		{
			UPrimitive* Prim = Actor->GetPrimitive();
			if( Prim )
			{
				BoundingBox = Prim->GetRenderBoundingBox(Actor).TransformBy(LocalToWorld);
				BoundingSphere = Prim->GetRenderBoundingSphere(Actor).TransformBy(LocalToWorld);
			}
			else
			{
				BoundingBox = FBox(0);
				BoundingBox += Actor->Location;
			}
		}
	}
	// scion sz - DT_StaticProjectorBatch
	else
	{
		UPrimitive* Prim = Actor->GetPrimitive();
		if( Prim )
		{
			// l2w always identity for batched projector prims
			BoundingBox = Prim->GetRenderBoundingBox(Actor);
			BoundingSphere = Prim->GetRenderBoundingSphere(Actor);
		}
		else
		{
			BoundingBox = FBox(0);
			BoundingBox += Actor->Location;
		}		
	}

	//jg -- Check for a super large bounding box that shouldn't be tested
	// This is for two reasons: the first being fill, and the second being
	// that large meshes are almost always visible anyway
	if (NeedsOcclusionTest())
	{
		if ((BoundingBox.Max - BoundingBox.Min).SizeSquared() > MAX_BOX_SIZE)
		{
			SetNeedsOcclusionTest(0);
		}
		else
		{
			SetNeedsOcclusionTest(Actor->bUseHWOcclusionTests);
		}
	}

	unguard;
}


//  scion sz -
//	FDynamicActor::UpdateSorting
//
void FDynamicActor::UpdateSorting()
{
	guardSlow(FDynamicActor::UpdateSorting);

	//
	// NOTE about draw order:
	//
	// 1. Solid Actors render (unsorted)
	//
	// 2. Emissive Actors render (unsorted)
	//		- assuming that no Emissive actors are never Translucent or PostProcess
	//
	// 3. Translucent Actors render (sorted back-to-front)
	//		- This includes Translucent BSP planes
	//
	// 4. Post Process Actors render (unsorted)
	//		- There is a fullscreen copy of the back buffer before we start rendering these
	//		- Emitters may have subemitters that are both Translucent & PostProcess
	//		so, those actors may draw twice
	//

	if( NeedsSortUpdate || Actor->bSkinsChanged )
	{
		NeedsSortUpdate=0;
		Actor->bSkinsChanged=0;

		// flag emissive actors
		IsEmissive = HasEmissiveMaterial(Actor);

		IsPostProcess = POSTPROCESS_NONE;
		IsTranslucent = 0;

		// emissive actors should never be Translucent/PostProcess
		// since they use alpha for masking emissive pixels
		if( !IsEmissive )
		{
			// flag PostProcess actor
			IsPostProcess = HasPostProcessMaterial(Actor);

			if( IsPostProcess == POSTPROCESS_NONE )
			{
				// flag Translucent actor
				IsTranslucent = HasTranslucentMaterial(Actor);
			}
			else if( Actor->DrawType == DT_Particle )
			{
				// force add to deferred translucent draw list
				// since some of the subemitters may be translucent
                IsTranslucent = 1;
			}
		}
	}

	unguardfSlow( (*FString::Printf( TEXT("Actor(%s) %u"), Actor->GetName(), Actor->bDeleteMe)) );
}

//
//	FDynamicActor::Render
//
void FDynamicActor::Render(FLevelSceneNode* SceneNode,TList<FDynamicLight*>* Lights,TList<FProjectorRenderInfo*>* Projectors,FRenderInterface* RI)
{
	guard(FDynamicActor::Render);

	PUSH_HIT(SceneNode->Viewport,HActor(Actor));

#ifndef FINAL_RELEASE
	if(SceneNode->Viewport->bShowBounds)
	{
		// Render bounding box.

		RI->SetTransform(TT_LocalToWorld,FMatrix::Identity);
		FLineBatcher(RI).DrawBox(BoundingBox,FColor(255,0,0));
        FLineBatcher(RI).DrawBox(PredictedBox,FColor(0,255,0));
	}
#endif

	//scion jg -- Orient an arbitrary mesh toward the current viewport actor
	// This should be done in the update render data code, but there is no
	// scene node information in that chain of calls.
	if (GIsEditor == 0 && Actor->bFacePlayer &&
		SceneNode->Viewport->Actor != NULL)
	{
		APlayerController* pController =
			(APlayerController*)SceneNode->Viewport->Actor;
		// Take the vector from the actor to the player, convert that to
		// a rotation and copy that to the actor's rotation
		FVector ActorToViewer = pController->CurrentCameraLocation -
			Actor->Location;
		// Don't cause the thing to lean over the player
		ActorToViewer.Z = 0.f;
		Actor->Rotation = ActorToViewer.Rotation();
		// If this is a static mesh, we need to update LocalToWorld
		// and WorldToLocal
		if(Actor->DrawType == DT_StaticMesh && Actor->StaticMesh)
		{

			// Take the vector from the actor to the player, convert that to
			// a rotation and apply that rotation to the LocalToWorld
			LocalToWorld = Actor->LocalToWorld();
			// scion sz - not used
			//WorldToLocal = LocalToWorld.Inverse();
		}
	}

	Actor->LastRenderTime = Actor->Level->TimeSeconds;
	if(Actor->DrawType == DT_StaticMesh && Actor->StaticMesh)
	{
		//scion superville: Recompute transfomration matrices for static meshes that
		//  have had their prepivots changed
		if( Actor->PrePivot != Actor->OldPrePivot ) 
		{
			LocalToWorld = Actor->LocalToWorld();
			// scion sz - not used
			//WorldToLocal = LocalToWorld.Inverse();
			Actor->OldPrePivot = Actor->PrePivot;
		}

		RenderStaticMesh(this,SceneNode,Lights,Projectors,RI);
	}
	else if( Actor->IsDTMesh() && Actor->Mesh )
	{
		// scion sz -
		// Attachments are update and added to deferred draw lists
		// during bsp node processing. See AddActorToDeferredDrawLists().

		// just render the mesh
		Actor->Mesh->MeshGetInstance(Actor)->Render(this,SceneNode,Lights,Projectors,RI);
	}
	else if(Actor->DrawType == DT_Sprite && Actor->Texture && SceneNode->Viewport->Actor->RendMap!=REN_MatineePreview)
	{
		DrawSprite(Actor,SceneNode,RI);
	}
	else if(Actor->DrawType == DT_Particle && Actor->IsA(AEmitter::StaticClass()) )
	{
		Cast<AEmitter>(Actor)->Render(this,SceneNode,Lights,RI);
		if(GIsEditor && Actor->Texture)
			DrawSprite(Actor,SceneNode,RI);
	}
	else if(Actor->DrawType == DT_FluidSurface && Actor->IsA(AFluidSurfaceInfo::StaticClass()) )
	{
		Cast<AFluidSurfaceInfo>(Actor)->Render(this, SceneNode, Lights, RI);
	}
	else if(Actor->IsA( AxWeatherEffect::StaticClass() ) )
	{
		((AxWeatherEffect*)Actor)->Render( SceneNode, RI );
		if(GIsEditor && Actor->Texture)
			DrawSprite(Actor,SceneNode,RI);
	}	
	else if(Actor->DrawType == DT_Brush && Actor->IsA(ABrush::StaticClass()) && Actor->Brush)
	{
		Actor->Brush->Render(this,SceneNode,RI);
	}
	// scion sz - render static projector batches
	else if( Actor->DrawType == DT_StaticProjectorBatch )
	{
        AProjector* Projector = Cast<AProjector>( Actor );
		Projector->RenderStaticPrimsBatch( SceneNode, RI );
	}	

#ifndef FINAL_RELEASE
	// AntiPortal
	if( (Actor->DrawType == DT_AntiPortal) && Actor->AntiPortal
		&& ((GIsEditor && !(SceneNode->Viewport->Actor->ShowFlags & SHOW_PlayerCtrl))
		|| (!GIsEditor && (SceneNode->Viewport->Actor->ShowFlags & SHOW_Volumes))))
	{
		FLineBatcher	LineBatcher(RI,0);
		FColor			LineColor = Actor->bSelected ? FColor(255,128,0) : FColor(128,64,0);

		RI->SetTransform(TT_LocalToWorld,LocalToWorld);

		for(INT FaceIndex = 0;FaceIndex < Actor->AntiPortal->Faces.Num();FaceIndex++)
		{
			FConvexVolumeFace*	Face = &Actor->AntiPortal->Faces(FaceIndex);

			for(INT VertexIndex = 0;VertexIndex < Face->Vertices.Num();VertexIndex++)
				LineBatcher.DrawLine(Face->Vertices(VertexIndex),Face->Vertices((VertexIndex + 1) % Face->Vertices.Num()),LineColor);
		}
	}

	if(GIsEditor )
		Actor->RenderEditorInfo(SceneNode,RI,this);
#endif

#ifndef FINAL_RELEASE
	if( SceneNode->Viewport->Actor->ShowFlags & SHOW_ActorRadii )
	{
		FLineBatcher LineBatcher(RI);
		UEngine* Engine = SceneNode->Viewport->GetOuterUClient()->Engine;
		if( Actor->bDirectional )
		{
			RI->SetTransform(TT_LocalToWorld,FMatrix::Identity);
			LineBatcher.DrawDirectionalArrow( Actor->Location, Actor->Rotation, Engine->C_ActorArrow, Actor->DrawScale );
		}
		RI->SetTransform(TT_LocalToWorld,FMatrix::Identity);

		if( Actor->IsMovingBrush() )
		{
			LineBatcher.DrawBox( BoundingBox, Engine->C_ActorArrow );
		}
		else if( !Actor->IsBrush() )
		{
			if( Actor->bCollideActors )
			{
				LineBatcher.DrawCylinder( RI, Actor->Location, FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), Engine->C_BrushWire, Actor->CollisionRadius, Actor->CollisionHeight, 16 );

				//epic superville: Help debug weapon cylinders
				if( Actor->IsAPawn() )
				{
					APawn *P = Cast<APawn>(Actor);
					FVector CylLocation = P->Location;
					FVector WpnExtent	= P->GetWeaponExtent( &CylLocation );

					LineBatcher.DrawCylinder( RI, CylLocation, FVector(1,0,0), FVector(0,1,0), FVector(0,0,1), Engine->C_Select, WpnExtent.X, WpnExtent.Z, 16 );
				}
			}

		}

		// Light radius.
		if( Actor->LightType!=LT_None && (Actor->LightBrightness > 0.0f) && (Actor->LightRadius > 0.0f) )
		{
			FPlane XPlane(SceneNode->CameraToWorld.M[0][0], SceneNode->CameraToWorld.M[0][1], SceneNode->CameraToWorld.M[0][2], SceneNode->CameraToWorld.M[0][3]);
			FPlane YPlane(SceneNode->CameraToWorld.M[1][0], SceneNode->CameraToWorld.M[1][1], SceneNode->CameraToWorld.M[1][2], SceneNode->CameraToWorld.M[1][3]);
			LineBatcher.DrawCircle( Actor->Location, XPlane, YPlane, Engine->C_ActorArrow, Actor->WorldLightRadius(), 16 );
		}
	}
#endif
	POP_HIT(SceneNode->Viewport);

	unguard;
}

/* scion ======================================================================
* FDynamicActor::RemoveVolatileLight
* Author: sz
* 
* =============================================================================
*/
void FDynamicActor::RemoveVolatileLight(class FVolatileDynamicLight* pDynLight )
{
	guard(FDynamicActor::RemoveVolatileLight);
	if( !pDynLight || !ActorLights )
		return;

	FLightList*	pFound = NULL;

	// head entry 
	if( pDynLight == ActorLights->Element )
	{
        pFound = ActorLights;
		ActorLights = ActorLights->Next;
	}
	// list entry
	else
	{
		for( FLightList* pCur = ActorLights; pCur->Next; pCur = pCur->Next )
		{
			if( pDynLight == pCur->Next->Element )
			{
				pFound = pCur->Next;
				pCur->Next = pCur->Next->Next;
				break;
			}
		}
	}

	if( pFound )
	{
		DestroyLightList( pFound );
	}	
	
	unguard;
}

void AEmitter::RenderEditorInfo(FLevelSceneNode* SceneNode,FRenderInterface* RI, FDynamicActor* FDA)
{
	guard(AEmitter::RenderEditorInfo);

	Super::RenderEditorInfo(SceneNode,RI,FDA);
	if( Texture )
		DrawSprite(this,SceneNode,RI);
	unguard;
}

void AFluidSurfaceInfo::RenderEditorInfo(FLevelSceneNode* SceneNode,FRenderInterface* RI, FDynamicActor* FDA)
{
	guard(AFluidSurfaceInfo::RenderEditorInfo);

	Super::RenderEditorInfo(SceneNode,RI,FDA);
	if( Texture )
		DrawSprite(this,SceneNode,RI);
	unguard;
}

//
//	FLevelSceneNode::FLevelSceneNode
//
FLevelSceneNode::FLevelSceneNode(UViewport* InViewport,FRenderTarget* InRenderTarget) : FSceneNode(InViewport,InRenderTarget)
{
	Level = Viewport->Actor->XLevel;
	Model = Level->Model;
	ViewZone = INDEX_NONE;
	InvisibleZone = INDEX_NONE;
	ViewActor = NULL;

	StencilMask = 0;

	// If necessary, build model render data.
	if(!Model->Sections.Num())
		Model->BuildRenderData();

	if(!Level->BuiltRenderData)
		Level->BuildRenderData();

	// scion sz - remove unnecessary entries from list of leaf-permeating lights
	if( Model->HasInvalidLeafLights() )
		Model->RemoveInvalidLeafLights();

}

//
//	FLevelSceneNode::FLevelSceneNode
//
FLevelSceneNode::FLevelSceneNode(FLevelSceneNode* InParent,INT InViewZone,FMatrix LocalToParent) : FSceneNode(InParent)
{
	Level = InParent->Level;
	Model = InParent->Model;
	ViewZone = InViewZone;
	InvisibleZone = INDEX_NONE;
	ViewActor = NULL;

	StencilMask = Viewport->NextStencilMask;
	Viewport->NextStencilMask <<= 1;

	FMatrix	ParentToLocal = LocalToParent.Inverse();

	WorldToCamera = LocalToParent * Parent->WorldToCamera;
	CameraToWorld = Parent->CameraToWorld * ParentToLocal;

	WorldToScreen = WorldToCamera * CameraToScreen;
	ScreenToWorld = ScreenToCamera * CameraToWorld;

	Determinant = WorldToScreen.Determinant();

	ViewOrigin = ParentToLocal.TransformFVector(Parent->ViewOrigin);

	CameraX = ScreenToWorld.TransformNormal(FVector(2.0f / Viewport->SizeX,0,0));
	CameraY = ScreenToWorld.TransformNormal(FVector(0,2.0f / Viewport->SizeY,0));
}

//
//	FLevelSceneNode::~FLevelSceneNode
//
FLevelSceneNode::~FLevelSceneNode()
{
}

//
//	FLevelSceneNode::GetViewFrustum
//
FConvexVolume FLevelSceneNode::GetViewFrustum()
{
	FConvexVolume	ViewFrustum;
	FLOAT			Determinant = WorldToScreen.Determinant();

	if(Viewport->IsOrtho())
	{
		FVector	ViewSides[2][2][2];
		FLOAT	TempSigns[2] = { -1.0f, 1.0f };

		for(INT X = 0;X < 2;X++)
			for(INT Y = 0;Y < 2;Y++)
				for(INT Z = 0;Z < 2;Z++)
					ViewSides[X][Y][Z] = Deproject(FPlane(TempSigns[X],TempSigns[Y],TempSigns[Z],1.0f));

		if(Determinant < 0.0f)
		{
			ViewFrustum.BoundingPlanes[0] = FPlane(ViewSides[0][0][0],ViewSides[0][0][1],ViewSides[1][0][1]);
			ViewFrustum.BoundingPlanes[1] = FPlane(ViewSides[1][0][0],ViewSides[1][0][1],ViewSides[1][1][1]);
			ViewFrustum.BoundingPlanes[2] = FPlane(ViewSides[1][1][0],ViewSides[1][1][1],ViewSides[0][1][1]);
			ViewFrustum.BoundingPlanes[3] = FPlane(ViewSides[0][1][0],ViewSides[0][1][1],ViewSides[0][0][1]);
		}
		else
		{
			ViewFrustum.BoundingPlanes[0] = FPlane(ViewSides[0][0][0],ViewSides[1][0][1],ViewSides[0][0][1]);
			ViewFrustum.BoundingPlanes[1] = FPlane(ViewSides[1][0][0],ViewSides[1][1][1],ViewSides[1][0][1]);
			ViewFrustum.BoundingPlanes[2] = FPlane(ViewSides[1][1][0],ViewSides[0][1][1],ViewSides[1][1][1]);
			ViewFrustum.BoundingPlanes[3] = FPlane(ViewSides[0][1][0],ViewSides[0][0][1],ViewSides[0][1][1]);
		}

		ViewFrustum.NumPlanes = 4;
	}
	else
	{
		FVector	ViewSides[4];
		FLOAT	TempSigns[2] = { -1.0f, 1.0f };

		for(INT X = 0;X < 2;X++)
			for(INT Y = 0;Y < 2;Y++)
				ViewSides[X * 2 + Y] = Deproject(FPlane(TempSigns[X],TempSigns[Y],0.0f,NEAR_CLIPPING_PLANE));

		if(Determinant < 0.0f)
		{
			ViewFrustum.BoundingPlanes[0] = FPlane(ViewOrigin,ViewSides[1],ViewSides[0]);
			ViewFrustum.BoundingPlanes[1] = FPlane(ViewOrigin,ViewSides[0],ViewSides[2]);
			ViewFrustum.BoundingPlanes[2] = FPlane(ViewOrigin,ViewSides[2],ViewSides[3]);
			ViewFrustum.BoundingPlanes[3] = FPlane(ViewOrigin,ViewSides[3],ViewSides[1]);
		}
		else
		{
			ViewFrustum.BoundingPlanes[0] = FPlane(ViewOrigin,ViewSides[0],ViewSides[1]);
			ViewFrustum.BoundingPlanes[1] = FPlane(ViewOrigin,ViewSides[2],ViewSides[0]);
			ViewFrustum.BoundingPlanes[2] = FPlane(ViewOrigin,ViewSides[3],ViewSides[2]);
			ViewFrustum.BoundingPlanes[3] = FPlane(ViewOrigin,ViewSides[1],ViewSides[3]);
		}

		if(!Viewport->IsWire())
		{
			// Calculate the far clipping plane.

			AZoneInfo*	ViewZoneInfo = Level->GetZoneActor(ViewZone);		
			FLOAT		FarClip = (ViewZoneInfo->bDistanceFog && (Viewport->Actor->ShowFlags & SHOW_DistanceFog)) ? Max(ViewZoneInfo->DistanceFogEnd,Viewport->Actor->CurrentDistanceFogEnd) : FAR_CLIPPING_PLANE;
			FVector		Z = (Deproject(FPlane(0,0,0,NEAR_CLIPPING_PLANE)) - ViewOrigin).SafeNormal();

			ViewFrustum.BoundingPlanes[4] = FPlane(ViewOrigin + Z * FarClip,Z);
			ViewFrustum.NumPlanes = 5;
		}
		else
			ViewFrustum.NumPlanes = 4;
	}

	return ViewFrustum;
}

#ifdef __PSX2_EE__
//
//	FLevelSceneNode::GetOverflowFrustum
//
FConvexVolume FLevelSceneNode::GetOverflowFrustum()
{
	FConvexVolume	ViewFrustum;
	FLOAT			Determinant = WorldToScreen.Determinant();

	if(Viewport->IsOrtho())
	{
		FVector	ViewSides[2][2][2];
		FLOAT	TempSigns[2] = { -1.0f, 1.0f };

		for(INT X = 0;X < 2;X++)
			for(INT Y = 0;Y < 2;Y++)
				for(INT Z = 0;Z < 2;Z++)
					ViewSides[X][Y][Z] = Deproject(FPlane(TempSigns[X],TempSigns[Y],TempSigns[Z],1.0f));

		if(Determinant < 0.0f)
		{
			ViewFrustum.BoundingPlanes[0] = FPlane(ViewSides[0][0][0],ViewSides[0][0][1],ViewSides[1][0][1]);
			ViewFrustum.BoundingPlanes[1] = FPlane(ViewSides[1][0][0],ViewSides[1][0][1],ViewSides[1][1][1]);
			ViewFrustum.BoundingPlanes[2] = FPlane(ViewSides[1][1][0],ViewSides[1][1][1],ViewSides[0][1][1]);
			ViewFrustum.BoundingPlanes[3] = FPlane(ViewSides[0][1][0],ViewSides[0][1][1],ViewSides[0][0][1]);
		}
		else
		{
			ViewFrustum.BoundingPlanes[0] = FPlane(ViewSides[0][0][0],ViewSides[1][0][1],ViewSides[0][0][1]);
			ViewFrustum.BoundingPlanes[1] = FPlane(ViewSides[1][0][0],ViewSides[1][1][1],ViewSides[1][0][1]);
			ViewFrustum.BoundingPlanes[2] = FPlane(ViewSides[1][1][0],ViewSides[0][1][1],ViewSides[1][1][1]);
			ViewFrustum.BoundingPlanes[3] = FPlane(ViewSides[0][1][0],ViewSides[0][0][1],ViewSides[0][1][1]);
		}

		ViewFrustum.NumPlanes = 4;
	}
	else
	{
		FVector	ViewSides[4];
		FLOAT	TempSigns[2] = { -1.4f, 1.4f };
//		FLOAT	TempSigns[2] = { -2.8f, 2.8f };

		for(INT X = 0;X < 2;X++)
			for(INT Y = 0;Y < 2;Y++)
				ViewSides[X * 2 + Y] = Deproject(FPlane(TempSigns[X],TempSigns[Y],0.0f,NEAR_CLIPPING_PLANE));

		if(Determinant < 0.0f)
		{
			ViewFrustum.BoundingPlanes[0] = FPlane(ViewOrigin,ViewSides[1],ViewSides[0]);
			ViewFrustum.BoundingPlanes[1] = FPlane(ViewOrigin,ViewSides[0],ViewSides[2]);
			ViewFrustum.BoundingPlanes[2] = FPlane(ViewOrigin,ViewSides[2],ViewSides[3]);
			ViewFrustum.BoundingPlanes[3] = FPlane(ViewOrigin,ViewSides[3],ViewSides[1]);
		}
		else
		{
			ViewFrustum.BoundingPlanes[0] = FPlane(ViewOrigin,ViewSides[0],ViewSides[1]);
			ViewFrustum.BoundingPlanes[1] = FPlane(ViewOrigin,ViewSides[2],ViewSides[0]);
			ViewFrustum.BoundingPlanes[2] = FPlane(ViewOrigin,ViewSides[3],ViewSides[2]);
			ViewFrustum.BoundingPlanes[3] = FPlane(ViewOrigin,ViewSides[1],ViewSides[3]);
		}

		ViewFrustum.NumPlanes = 4;
	}

	return ViewFrustum;
}
#endif // __PSX2_EE__
//
//	FLevelSceneNode::FilterActor
//
UBOOL FLevelSceneNode::FilterActor(AActor* Actor, UBOOL DrawAttached )
{
	UBOOL ShouldDraw=1;

	if(!(Viewport->Actor->ShowFlags & SHOW_Actors) && !Actor->IsA(ABrush::StaticClass()))
		return 0;

#ifndef FINAL_RELEASE
	//WD: rearranged if() statement arguments
	if(!(Viewport->Actor->ShowFlags & SHOW_StaticMeshes) && Actor->StaticMesh)
		return 0;

	//WD: rearranged if() statement arguements
	if(!(Viewport->Actor->ShowFlags & SHOW_FluidSurfaces) && Actor->IsA(AFluidSurfaceInfo::StaticClass()))
		return 0;

	// Draw blocking volumes if SHOW_Collision or KDRAW_Collision is set.
	if( (Viewport->Actor->ShowFlags & SHOW_Collision) && Actor->IsA(ABlockingVolume::StaticClass()) )
		return ShouldDraw;
#endif
	//WD: changed if(GIsEditor) to if(!GIsEditor), and swapped instruction blocks
	if( !GIsEditor )
	{
		if(Actor->bCorona)
		{
			Actor->DrawType = DT_None;
			return ShouldDraw;
		}
		else
		{
			if((Viewport->Actor->ShowFlags & SHOW_Volumes) && Actor->IsVolumeBrush())
				return ShouldDraw;
			else if(Actor->bHidden)
				return 0;
			else if ( Actor->bOnlyDrawIfAttached && !Actor->Base )
				return 0;
				
			//scion superville: Don't draw if OwnerNoSee, even if behindview
			if( Actor->IsNoRender( Viewport ) ) {
				return 0;
			}
			
			if( Actor->IsOwnedBy(ViewActor) ) {
				// if in first person and not transitioning and owner should not see - return 0
				if( !Viewport->Actor->bBehindView			&&
					!Viewport->Actor->bTransThirdToFirst	&&
					Actor->bOwnerNoSee )
				{
					return 0;				
				}
			}
			else if( Actor->bOnlyOwnerSee ) {
				return 0;
			}
		}

		// scion sz -
		if( Actor->DetailLevel > Actor->Level->DetailMode )		
			return 0;
	}
#ifndef FINAL_RELEASE
	else
	{
		if( (Actor->IsVolumeBrush() || Cast<AAntiPortalActor>(Actor)) && !(Viewport->Actor->ShowFlags & SHOW_Volumes))
			return 0;

		if(Actor->IsMovingBrush() && !(Viewport->Actor->ShowFlags & SHOW_MovingBrushes))
			return 0;

		if( Actor->IsHiddenEd() )
			return 0;
	}
#endif
	// scion sz -
	// an actor is always drawn along with its base and not on its own (iff the base is a skeletal mesh)
	if( !DrawAttached && Actor->Base && Actor->Base->Mesh )
		return 0;

	return ShouldDraw;
}

//	scion sz -
//	FLevelSceneNode::FilterAttachment
//
UBOOL FLevelSceneNode::FilterAttachment(AActor* AttachedActor)
{
	return 1;

#if 0
	if( FilterMode == FSceneNode::Filter_All )
	{
		return 1;
	}
	else if( FilterMode == FSceneNode::Filter_Solid )
	{
		FDynamicActor* DynamicActor = AttachedActor->GetActorRenderData();
		if( DynamicActor->IsPostProcess ||
			DynamicActor->IsTranslucent ||
			DynamicActor->IsEmissive )
		{
			return 0;
		}
		return 1;
	}
	else if( FilterMode == FSceneNode::Filter_Translucent )
	{
		FDynamicActor* DynamicActor = AttachedActor->GetActorRenderData();
		if( DynamicActor->IsTranslucent ||
			DynamicActor->IsEmissive ||
			AttachedActor->DrawType == DT_Particle )
		{
			return 1;
		}
		return 0;
	}
	else if( FilterMode == FSceneNode::Filter_PostProcess )
	{
		FDynamicActor* DynamicActor = AttachedActor->GetActorRenderData();
		if( DynamicActor->IsPostProcess )
		{
			return 1;
		}
	}
	return 0;
#endif
}


//
//	FLevelSceneNode::FilterProjector
//

UBOOL FLevelSceneNode::FilterProjector(AProjector* Actor)
{
    if( !Actor )
		return 1;

	if(Viewport->IsWire())
			return 0;

	if ( Actor->IsOwnedBy(ViewActor) ? (Actor->bOwnerNoSee && !Viewport->Actor->bBehindView) : Actor->bOnlyOwnerSee )
			return 0;

	// scion sz -
	if( Actor->DetailLevel > Actor->Level->DetailMode )		
		return 0;

	return 1;
}

//
//  FLevelSceneNode::Render
//

void FLevelSceneNode::Render(FRenderInterface* RI)
{
	guard(FLevelSceneNode::Render);	

	DECLARE_STATIC_UOBJECT(
		UProxyBitmapMaterial,
		HackMaterial,
		{
			static FSolidColorTexture	BlackTexture(FColor(0,0,0));
			HackMaterial->SetTextureInterface(&BlackTexture);
		}
		);

	DECLARE_STATIC_UOBJECT(
		UFinalBlend,
		StencilFinalBlend,
		{
			StencilFinalBlend->Material = HackMaterial;
			StencilFinalBlend->FrameBufferBlending = FB_Invisible;
			StencilFinalBlend->ZWrite = 1;
			StencilFinalBlend->ZTest = 0;
		}
		);

	RI->PushState();

	// Calculate distance fog.
	UBOOL bVolumeFog = 0;

	if ( !Parent && Viewport->Actor )
	{
		APhysicsVolume *V = Viewport->Actor->GetViewTarget()->PhysicsVolume;
		APawn *P = (Viewport->Actor->ViewTarget && Viewport->Actor->ViewTarget->IsAPawn()) ? (APawn*)Viewport->Actor->ViewTarget : NULL;
		if ( P )
			V = P->HeadVolume;
		if ( V && V->bDistanceFog )
		{
			UBOOL bShowFog = GEdShowFogInViewports && !Viewport->IsWire();
			bVolumeFog = true;
			RI->SetDistanceFog( bShowFog, V->DistanceFogStart, V->DistanceFogEnd, V->DistanceFogColor );
		}
	}

	if(!bVolumeFog)
	{
		AZoneInfo*	ViewZoneInfo = Level->GetZoneActor(ViewZone);
		APlayerController*	Player			= Viewport->Actor;

		// Interpolate between fog zones.
		if( !Parent && Viewport->IsRealtime() )
		{
			// Handle zone changes.
			if( Player->LastZone != ViewZone )
			{
				// Use correct fog color if transitioning from a zone without fog.
				if( ViewZoneInfo->bDistanceFog && (Player->LastDistanceFogStart == FAR_CLIPPING_PLANE) )
					Player->LastDistanceFogColor = ViewZoneInfo->DistanceFogColor;

				// Don't transition out of zone 0
				if( Player->LastZone == 0 )
					Player->TimeSinceLastFogChange	= 1000000.f; //!! can't use FLT_MAX as value will be increased.
				else
					Player->TimeSinceLastFogChange	= 0.f;

				Player->LastZone = ViewZone;
			}

			FLOAT	Alpha = ViewZoneInfo->DistanceFogBlendTime ? Player->TimeSinceLastFogChange / ViewZoneInfo->DistanceFogBlendTime : 1.f;

			FLOAT	WantedDistanceFogStart,
					WantedDistanceFogEnd;
			FColor	WantedDistanceFogColor;

			// Handle case of transitioning from a fog zone to one without fog.
			if( ViewZoneInfo->bDistanceFog )
			{
				WantedDistanceFogStart	= ViewZoneInfo->DistanceFogStart;
				WantedDistanceFogEnd	= ViewZoneInfo->DistanceFogEnd;
				WantedDistanceFogColor	= ViewZoneInfo->DistanceFogColor;
			}
			else
			{
				WantedDistanceFogStart	= FAR_CLIPPING_PLANE;
				WantedDistanceFogEnd	= FAR_CLIPPING_PLANE;
				WantedDistanceFogColor	= Player->LastDistanceFogColor;
			}

			// Blend completed - also, don't transition into zone 0.
			if( Alpha > 1.f || ViewZone == 0 )
			{
				Player->LastDistanceFogStart	= WantedDistanceFogStart;
				Player->LastDistanceFogEnd		= WantedDistanceFogEnd;
				Player->LastDistanceFogColor	= WantedDistanceFogColor;
				Player->TimeSinceLastFogChange	= 1000000.f; //!! can't use FLT_MAX as value will be increased.
				Alpha = 1.f;
			}

			// Lerp between fog state.
			FLOAT	DistanceFogStart	= Lerp( Player->LastDistanceFogStart, WantedDistanceFogStart, Alpha ),
					DistanceFogEnd		= Lerp( Player->LastDistanceFogEnd  , WantedDistanceFogEnd  , Alpha );
			FColor	DistanceFogColor;	// Can't lerp directly as it will cast to DWORD and lerp there.
			
			DistanceFogColor.R = Lerp( Player->LastDistanceFogColor.R, WantedDistanceFogColor.R, Alpha );
			DistanceFogColor.G = Lerp( Player->LastDistanceFogColor.G, WantedDistanceFogColor.G, Alpha );
			DistanceFogColor.B = Lerp( Player->LastDistanceFogColor.B, WantedDistanceFogColor.B, Alpha );

			// Keep track of current max fog distance for culling.
			Player->CurrentDistanceFogEnd = DistanceFogEnd;

			UBOOL UseFog = ViewZoneInfo->bDistanceFog || Alpha < 1.f;
			RI->SetDistanceFog(UseFog && (Viewport->Actor->ShowFlags & SHOW_DistanceFog),DistanceFogStart,DistanceFogEnd,DistanceFogColor);
		}
		else
		{
			RI->SetDistanceFog(ViewZoneInfo->bDistanceFog && (Viewport->Actor->ShowFlags & SHOW_DistanceFog),ViewZoneInfo->DistanceFogStart,ViewZoneInfo->DistanceFogEnd,ViewZoneInfo->DistanceFogColor);
		}

	}

	RenderLevel(this,RI);

#ifdef WITH_KARMA
	if(!Parent && Level->KWorld)
	{
		// go through world drawing constraints
		// done here instead of tick, so we get contacts etc. even if Karma is paused etc.
		KLevelDebugDrawConstraints(Level);
	}
#endif

#ifndef FINAL_RELEASE
	// Render lines from elsewhere (collision debugging etc.)
	if(GTempLineBatcher)
	{
		RI->PushState();
		RI->SetTransform(TT_WorldToCamera,WorldToCamera);
		RI->SetTransform(TT_CameraToScreen,CameraToScreen);
		GTempLineBatcher->Render(RI);
		RI->PopState();
	}
#endif


	if ( Viewport->Actor->myHUD )
		Viewport->Actor->myHUD->eventWorldSpaceOverlays();

#ifndef FINAL_RELEASE
	// Render the graph
	if(GStatGraph)
		GStatGraph->Render(Viewport, RI);
#endif

	RI->PopState();

	unguard;
}

/*-----------------------------------------------------------------------------
	D-8: AutoFOV (UnRender.h; docs/porting/analysis/console-fov.md).
-----------------------------------------------------------------------------*/

ENGINE_API APlayerController*	GAutoFovScopeActor		= NULL;
ENGINE_API FLOAT				GAutoFovFrameBase		= 90.0f;
ENGINE_API FLOAT				GAutoFovFrameEffective	= 90.0f;

// Read once from [ufront.AutoFOV] of the system ini (the template's section: a free section, so the
// frontend's -ini:System:[ufront.AutoFOV]:AutoFOV=0 reaches it); AUTOFOV on the console changes the mode
// for the session.
static UBOOL	GAutoFovParsed		= 0;
static INT		GAutoFovMode		= AUTOFOV_Adaptive;
static FLOAT	GAutoFovRef			= 4.0f / 3.0f;
static FLOAT	GAutoFovWeapon		= 1.0f;
static INT		GAutoFovLastX		= -1;
static INT		GAutoFovLastY		= -1;
static FLOAT	GAutoFovLastBase	= -1.0f;
static INT		GAutoFovLastMode	= -1;
static INT		GAutoFovWeapLastX	= -1;
static INT		GAutoFovWeapLastY	= -1;
static FLOAT	GAutoFovWeapLastBase = -1.0f;

static const TCHAR* AutoFovModeName( INT Mode )
{
	return Mode==AUTOFOV_Off ? TEXT("Off") : Mode==AUTOFOV_HorPlus ? TEXT("HorPlus") : TEXT("Adaptive");
}

static void ufrontParseAutoFov()
{
	guard(ufrontParseAutoFov);
	GAutoFovParsed = 1;
	INT   Mode = GAutoFovMode;
	FLOAT Ref  = GAutoFovRef;
	FLOAT Weap = GAutoFovWeapon;
	if( GConfig )
	{
		GConfig->GetInt  ( TEXT("ufront.AutoFOV"), TEXT("AutoFOV"),         Mode );
		GConfig->GetFloat( TEXT("ufront.AutoFOV"), TEXT("ReferenceAspect"), Ref  );
		GConfig->GetFloat( TEXT("ufront.AutoFOV"), TEXT("WeaponScale"),     Weap );
	}
	GAutoFovMode   = Clamp( Mode, (INT)AUTOFOV_Off, (INT)AUTOFOV_Adaptive );
	GAutoFovRef    = (Ref > 0.01f) ? Ref : (4.0f / 3.0f);
	GAutoFovWeapon = Clamp( Weap, 0.0f, 4.0f );
	debugf( NAME_Log, TEXT("[AutoFOV] mode=%i referenceAspect=%f weaponScale=%f"), GAutoFovMode, GAutoFovRef, GAutoFovWeapon );
	unguard;
}

ENGINE_API void ufrontAutoFovSize( UViewport* Viewport, INT& SizeX, INT& SizeY )
{
	// G-6: per viewport aspect. (D-8 used SizeX/ScaleX x SizeY/ScaleY for a split view, the window's.)
	SizeX = Viewport ? Viewport->SizeX : 0;
	SizeY = Viewport ? Viewport->SizeY : 0;
}

ENGINE_API UBOOL ufrontSplitFovScale( UViewport* Viewport, FLOAT Fov, FLOAT& OutScale )
{
	if( !GAutoFovParsed )
		ufrontParseAutoFov();
	if( GAutoFovMode == AUTOFOV_Off || !Viewport || Viewport->ScaleX <= 0.01f || Viewport->ScaleY <= 0.01f
	||	(Viewport->ScaleX == 1.f && Viewport->ScaleY == 1.f) )
		return 0;
	DOUBLE H = Clamp( Fov, 1.0f, 170.0f ) * PI / 360.0;
	OutScale = (FLOAT)( appAtan( Viewport->ScaleX * appTan( H ) ) / H );
	return 1;
}

FScopedSplitFovScale::FScopedSplitFovScale( UViewport* InViewport, FLOAT Fov )
:	Viewport( InViewport )
,	SavedX( InViewport ? InViewport->ScaleFOVX : 1.f )
,	SavedY( InViewport ? InViewport->ScaleFOVY : 1.f )
,	bSet( 0 )
{
	FLOAT Scale;
	if( ufrontSplitFovScale( Viewport, Fov, Scale ) )
	{
		Viewport->ScaleFOVX = Viewport->ScaleFOVY = Scale;
		bSet = 1;
	}
}

FScopedSplitFovScale::~FScopedSplitFovScale()
{
	if( bSet )
	{
		Viewport->ScaleFOVX = SavedX;
		Viewport->ScaleFOVY = SavedY;
	}
}

// Aspect / reference, or 1 when AutoFOV must leave the FOV alone (off, degenerate, at the reference, or
// narrower in Adaptive mode).
static DOUBLE AutoFovWiden( INT SizeX, INT SizeY )
{
	if( !GAutoFovParsed )
		ufrontParseAutoFov();
	if( GAutoFovMode == AUTOFOV_Off || SizeX <= 0 || SizeY <= 0 )
		return 1.0;
	DOUBLE K = ((DOUBLE)SizeX / (DOUBLE)SizeY) / (DOUBLE)GAutoFovRef;
	// At the reference aspect nothing may change, bit for bit (640x480, 1024x768): the ini's 1.333333 is
	// not exactly 4/3, and 2*atan(tan(x)) is not exactly x in floating point.
	if( Abs(K - 1.0) < 1e-4 )
		return 1.0;
	if( GAutoFovMode == AUTOFOV_Adaptive && K < 1.0 )
		return 1.0;
	return K;
}

ENGINE_API FLOAT ufrontEffectiveFov( FLOAT BaseFov, INT SizeX, INT SizeY, UBOOL bLog )
{
	guard(ufrontEffectiveFov);
	DOUBLE K = AutoFovWiden( SizeX, SizeY );
	FLOAT Eff = BaseFov;
	if( K != 1.0 )
	{
		DOUBLE Base = Clamp( BaseFov, 1.0f, 170.0f );
		Eff = (FLOAT)( appAtan( appTan( Base * PI / 360.0 ) * K ) * 360.0 / PI );
		Eff = Clamp( Eff, 1.0f, 170.0f );
	}
	// One line whenever the inputs change (viewport size, base FOV by more than half a degree, mode): the
	// template's format, which the frontend's AutoFOV test reads. A zoom costs one line per frame of its
	// interpolation, once.
	if( bLog && ( SizeX != GAutoFovLastX || SizeY != GAutoFovLastY || Abs(BaseFov - GAutoFovLastBase) > 0.5f || GAutoFovMode != GAutoFovLastMode ) )
	{
		GAutoFovLastX    = SizeX;
		GAutoFovLastY    = SizeY;
		GAutoFovLastBase = BaseFov;
		GAutoFovLastMode = GAutoFovMode;
		if( GAutoFovMode != AUTOFOV_Off )
			debugf( NAME_Log, TEXT("[AutoFOV] viewport=%ix%i aspect=%f base=%f effective=%f"),
				SizeX, SizeY, SizeY > 0 ? (FLOAT)SizeX / (FLOAT)SizeY : 0.f, BaseFov, Eff );
	}
	return Eff;
	unguard;
}

ENGINE_API FLOAT ufrontBaseFov( FLOAT EffectiveFov, INT SizeX, INT SizeY )
{
	guard(ufrontBaseFov);
	DOUBLE K = AutoFovWiden( SizeX, SizeY );
	if( K == 1.0 )
		return EffectiveFov;
	DOUBLE Eff = Clamp( EffectiveFov, 1.0f, 170.0f );
	return Clamp( (FLOAT)( appAtan( appTan( Eff * PI / 360.0 ) / K ) * 360.0 / PI ), 1.0f, 170.0f );
	unguard;
}

ENGINE_API FLOAT ufrontWeaponFov( FLOAT BaseFov, FLOAT EffectiveFov )
{
	guard(ufrontWeaponFov);
	if( !GAutoFovParsed )
		ufrontParseAutoFov();
	if( GAutoFovMode == AUTOFOV_Off || EffectiveFov == BaseFov || Abs(GAutoFovWeapon - 1.0f) < 0.001f )
		return EffectiveFov;
	// Blend in tan space (linear on screen): 0.5 lands visually half-way.
	DOUBLE TanBase = appTan( Clamp( BaseFov, 1.0f, 170.0f ) * PI / 360.0 );
	DOUBLE TanEff  = appTan( Clamp( EffectiveFov, 1.0f, 170.0f ) * PI / 360.0 );
	DOUBLE TanOut  = Max( TanBase + ( TanEff - TanBase ) * GAutoFovWeapon, 0.001 );
	return Clamp( (FLOAT)( appAtan( TanOut ) * 360.0 / PI ), 1.0f, 170.0f );
	unguard;
}

// The first-person weapon's line (latched like the world's): what it was asked for and what is drawn.
ENGINE_API void ufrontLogWeaponFov( INT SizeX, INT SizeY, FLOAT BaseFov, FLOAT DrawFov )
{
	if( GAutoFovMode == AUTOFOV_Off )
		return;
	if( SizeX != GAutoFovWeapLastX || SizeY != GAutoFovWeapLastY || Abs(BaseFov - GAutoFovWeapLastBase) > 0.5f )
	{
		GAutoFovWeapLastX = SizeX;
		GAutoFovWeapLastY = SizeY;
		GAutoFovWeapLastBase = BaseFov;
		debugf( NAME_Log, TEXT("[AutoFOV] weapon viewport=%ix%i base=%f drawFov=%f"), SizeX, SizeY, BaseFov, DrawFov );
	}
}

ENGINE_API UBOOL ufrontAutoFovExec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(ufrontAutoFovExec);
	if( !ParseCommand(&Cmd,TEXT("AUTOFOV")) )
		return 0;
	if( !GAutoFovParsed )
		ufrontParseAutoFov();
	TCHAR Word[64];
	if( ParseToken( Cmd, Word, ARRAY_COUNT(Word), 0 ) )
	{
		if( appStricmp(Word,TEXT("Off"))==0 || appStricmp(Word,TEXT("0"))==0 )				GAutoFovMode = AUTOFOV_Off;
		else if( appStricmp(Word,TEXT("HorPlus"))==0 || appStricmp(Word,TEXT("1"))==0 )		GAutoFovMode = AUTOFOV_HorPlus;
		else if( appStricmp(Word,TEXT("Adaptive"))==0 || appStricmp(Word,TEXT("2"))==0 )	GAutoFovMode = AUTOFOV_Adaptive;
		else Ar.Logf( TEXT("AUTOFOV: unknown mode '%s' (Off, HorPlus, Adaptive or 0, 1, 2)"), Word );
	}
	Ar.Logf( TEXT("[AutoFOV] mode=%i (%s) referenceAspect=%f weaponScale=%f"), GAutoFovMode, AutoFovModeName(GAutoFovMode), GAutoFovRef, GAutoFovWeapon );
	return 1;
	unguard;
}

//
//	FCameraSceneNode::FCameraSceneNode
//
FCameraSceneNode::FCameraSceneNode(UViewport* InViewport,FRenderTarget* InRenderTarget,AActor* CameraActor,FVector CameraLocation,FRotator CameraRotation,FLOAT CameraFOV) : FLevelSceneNode(InViewport,InRenderTarget)
{
	guard(FCameraSceneNode::FCameraSceneNode);

	// Determine which zone the camera is in.
	FPointRegion	CameraZone = Model->PointRegion(CameraActor->Level,CameraLocation);

	ViewOrigin		= CameraLocation;
	ViewActor		= CameraActor;
	ViewZone		= (GIsEditor && Viewport->IsWire()) || Viewport->Precaching ? 0 : Model->PointRegion(ViewActor->Level,ViewOrigin).ZoneNumber;
	ViewRotation	= CameraRotation;
	ViewFOV			= CameraFOV;

	UpdateMatrices();

	unguard;
}

//
//	FCameraSceneNode::UpdateMatrices
//
void FCameraSceneNode::UpdateMatrices()
{
	// Initialize the view matrix.
	WorldToCamera = FTranslationMatrix(-ViewOrigin);

	// scale factor needed to get square pixels
	static FLOAT PixelRatioScale = Viewport->RI->GetPixelAspectRatio();

	if(!Viewport->IsOrtho())
		WorldToCamera = WorldToCamera * FInverseRotationMatrix(ViewRotation);

	if(Viewport->Actor->RendMap == REN_OrthXY)
		WorldToCamera = WorldToCamera * FMatrix(
									FPlane(Viewport->ScaleX,	0,					0,					0),
									FPlane(0,					-Viewport->ScaleY,	0,					0),
									FPlane(0,					0,					-1,					0),
									FPlane(0,					0,					-ViewOrigin.Z,		1));
	else if(Viewport->Actor->RendMap == REN_OrthXZ)
		WorldToCamera = WorldToCamera * FMatrix(
									FPlane(Viewport->ScaleX,	0,					0,					0),
									FPlane(0,					0,					-1,					0),
									FPlane(0,					Viewport->ScaleY,	0,					0),
									FPlane(0,					0,					-ViewOrigin.Y,		1));
	else if(Viewport->Actor->RendMap == REN_OrthYZ)
		WorldToCamera = WorldToCamera * FMatrix(
									FPlane(0,					0,					1,					0),
									FPlane(Viewport->ScaleX,	0,					0,					0),
									FPlane(0,					Viewport->ScaleY,	0,					0),
									FPlane(0,					0,					ViewOrigin.X,		1));
	else
		WorldToCamera = WorldToCamera * FMatrix(
									FPlane(0,					0,					1,	0),
									FPlane(Viewport->ScaleX,	0,					0,	0),
									FPlane(0,					Viewport->ScaleY,	0,	0),
									FPlane(0,					0,					0,	1));

	CameraToWorld = WorldToCamera.Inverse();

	// Initialize the projection matrix.
	if(Viewport->IsOrtho())
	{
		FLOAT	Zoom = Viewport->Actor->OrthoZoom / (Viewport->SizeX * 15.0f);

		CameraToScreen = FOrthoMatrix(Zoom * RenderTarget->GetWidth() / 2,Zoom * RenderTarget->GetHeight() / 2,0.5f / HALF_WORLD_MAX,HALF_WORLD_MAX);
	}
	else
	{
		FLOAT		FOVX	= ViewFOV * PI / 360.0f;
		FLOAT		FOVY	= FOVX;

		// scion sz - use viewport fov scale
		FOVX *= Viewport->ScaleFOVX;
		FOVY *= Viewport->ScaleFOVY;

		FLOAT AspectRatio = (FLOAT) (RenderTarget->GetWidth()*Viewport->ScaleX) / (RenderTarget->GetHeight()*Viewport->ScaleY) * PixelRatioScale;

		CameraToScreen = FPerspectiveMatrix(
							FOVX,
							FOVY,
							1.f,
							AspectRatio,
							NEAR_CLIPPING_PLANE,
							FAR_CLIPPING_PLANE
							);
	}

	ScreenToCamera = CameraToScreen.Inverse();

	WorldToScreen = WorldToCamera * CameraToScreen;
	ScreenToWorld = ScreenToCamera * CameraToWorld;

	Determinant = WorldToScreen.Determinant();

	CameraX = ScreenToWorld.TransformNormal(FVector(2.0f / Viewport->SizeX,0,0));
	CameraY = ScreenToWorld.TransformNormal(FVector(0,2.0f / Viewport->SizeY,0));

#ifdef __GCN__
	CameraToScreen  = FMatrix(FPlane(0.0f, ViewFOV, NEAR_CLIPPING_PLANE, FAR_CLIPPING_PLANE), FPlane(), FPlane(), FPlane());
#endif
}


//
//	FCameraSceneNode::~FCameraSceneNode
//
FCameraSceneNode::~FCameraSceneNode()
{
}

//
//	FCameraSceneNode::Render
//

void FCameraSceneNode::Render(FRenderInterface* RI)
{
	guard(FCameraSceneNode::Render);

	RI->PushState();

	if(Viewport->Actor->RendMap == REN_DepthComplexity)
		RI->SetStencilOp(CF_Always,0,0,SO_Keep,SO_Keep,SO_Increment,DEPTH_COMPLEXITY_MASK(Viewport));

	// Clear the screen.
	if( !GIsEditor && Viewport->IsWire() )
		RI->Clear();

	// Render the level.

	FLevelSceneNode::Render(RI);

	// Use the stencil buffer to render depth complexity.

	if(Viewport->Actor->RendMap == REN_DepthComplexity)
	{
		DECLARE_STATIC_UOBJECT(
			UProxyBitmapMaterial,
			HackMaterial,
			{
				static FSolidColorTexture	WhiteTexture(FColor(255,255,255));
				HackMaterial->SetTextureInterface(&WhiteTexture);
			}
			);

		DECLARE_STATIC_UOBJECT(
			UFinalBlend,
			DepthComplexityBlend,
			{
				DepthComplexityBlend->ZTest = 0;
				DepthComplexityBlend->Material = HackMaterial;
			}
			);

		FLOAT	LegendHeight = Viewport->SizeY / 16.0f;

		for(DWORD Stencil = 0;Stencil < 4;Stencil++)
		{
			RI->SetStencilOp(CF_Equal,(Stencil + 1) | ~DEPTH_COMPLEXITY_MASK(Viewport),DEPTH_COMPLEXITY_MASK(Viewport),SO_Keep,SO_Keep,SO_Keep,0);

			FCanvasUtil(&Viewport->RenderTarget,RI).DrawTile(
				0,0,
				Viewport->SizeX - 32,Viewport->SizeY,
				0,0,
				0,0,
				0,
				DepthComplexityBlend,
				FColor(
					Stencil * 64,
					255,
					0
					)
				);

			RI->ResetStencilOp();

			RI->SetStencilOp(CF_Always,0,0,SO_Keep,SO_Keep,SO_Keep,0);

			FCanvasUtil(&Viewport->RenderTarget,RI).DrawTile(
				Viewport->SizeX - 32,Stencil * LegendHeight,
				Viewport->SizeX,(Stencil + 1) * LegendHeight,
				0,0,
				0,0,
				0.0f,
				DepthComplexityBlend,
				FColor(
					Stencil * 64,
					255,
					0
					)
				);

			RI->ResetStencilOp();
		}

		for(DWORD Stencil = 4;Stencil < 16;Stencil++)
		{
			RI->SetStencilOp(CF_Equal,(Stencil + 1) | ~DEPTH_COMPLEXITY_MASK(Viewport),DEPTH_COMPLEXITY_MASK(Viewport),SO_Keep,SO_Keep,SO_Keep,0);

			FCanvasUtil(&Viewport->RenderTarget,RI).DrawTile(
				0,0,
				Viewport->SizeX - 32,Viewport->SizeY,
				0,0,
				0,0,
				0,
				DepthComplexityBlend,
				FColor(
					255,
					231 - (Stencil - 4) * 21,
					0
					)
				);

			RI->ResetStencilOp();

			RI->SetStencilOp(CF_Always,0,0,SO_Keep,SO_Keep,SO_Keep,0);

			FCanvasUtil(&Viewport->RenderTarget,RI).DrawTile(
				Viewport->SizeX - 32,Stencil * LegendHeight,
				Viewport->SizeX,(Stencil + 1) * LegendHeight,
				0,0,
				0,0,
				0.0f,
				DepthComplexityBlend,
				FColor(
					255,
					231 - (Stencil - 4) * 21,
					0
					)
				);

			RI->ResetStencilOp();
		}
	}

	UpdateVertexPools(Viewport);

	if ( Viewport->Canvas->pCanvasUtil )
        Viewport->Canvas->pCanvasUtil->Flush();

	//scion jg -- Restore stencil state
	if(Viewport->Actor->RendMap == REN_DepthComplexity)
	{
		RI->ResetStencilOp();
	}

	RI->PopState();

	unguard;
}

//
//	FPlayerSceneNode::FPlayerSceneNode
//

FPlayerSceneNode::FPlayerSceneNode(UViewport* InViewport,FRenderTarget* InRenderTarget,AActor* CameraActor,FVector CameraLocation,FRotator CameraRotation,FLOAT CameraFOV) : FCameraSceneNode(InViewport,InRenderTarget,CameraActor,CameraLocation,CameraRotation,CameraFOV)
{
}

//jg -- Movie hack
UBOOL GRenderLevel = TRUE;

//
//	FPlayerSceneNode::Render
//

void FPlayerSceneNode::Render(FRenderInterface* RI)
{
	guard(FPlayerSceneNode::Render);

	UGameEngine* Engine = Cast<UGameEngine>(UEngine::GEngine);
	UBOOL bDedicatedServer = Engine && Engine->GLevel && Engine->GLevel->GetLevelInfo() &&
							 Engine->GLevel->GetLevelInfo()->Game &&
							 Engine->GLevel->GetLevelInfo()->Game->bDedicatedServer;

	APlayerController*	Controller = Viewport->Actor;

	if (bDedicatedServer)
	{
		RI->PushState();
		RI->Clear();

		// Update the console.
		Viewport->Canvas->Update();

		if ( Viewport->Canvas->pCanvasUtil )
			Viewport->Canvas->pCanvasUtil->Flush();

		// Send all the Interaction PreRenders

		CLOCK(IntPreRen,GStats.DWORDStats(GEngineStats.STATS_Game_InteractionPreRender));

		if ( Viewport->InteractionMaster )
			Viewport->InteractionMaster->MasterProcessPreRender(Viewport);

		UNCLOCK();

		if ( Viewport->Canvas->pCanvasUtil )
			Viewport->Canvas->pCanvasUtil->Flush();

#ifndef FINAL_RELEASE
		if( !GIsEditor && (Viewport->Actor->ShowFlags & SHOW_Actors) )
#endif
		{
			GUglyHackFlags |= 1;
			FUNC_CLOCK(HudCyc,GStats.DWORDStats(GEngineStats.STATS_Game_HUDPostRender));

			if ( Viewport->Actor->myHUD )
				Viewport->Actor->myHUD->eventPostRender(Viewport->Canvas);

			if ( Viewport->Canvas->pCanvasUtil )
				Viewport->Canvas->pCanvasUtil->Flush();

			GUglyHackFlags &= ~1;
		}

		// Send along PostRenders to the Interactions

		CLOCK(IntPostRen,GStats.DWORDStats(GEngineStats.STATS_Game_InteractionPostRender));

		if ( Viewport->InteractionMaster )
			Viewport->InteractionMaster->MasterProcessPostRender(Viewport);

		UNCLOCK();

#ifndef FINAL_RELEASE
		// Render performance stats.
		CLOCK(StatsCyc,GStats.DWORDStats(GEngineStats.STATS_Stats_RenderCycles));
		GStats.Render( Viewport, Level->Engine );		
		UNCLOCK();
#endif
#ifdef _XBOX
		//jg -- Hack to display "stat net" for debugging purposes
		extern UBOOL GShowStatNet;
		if (GShowStatNet == TRUE)
		{
			extern void RenderStatNet(UViewport* Viewport,UEngine* Engine);
			RenderStatNet(Viewport,Level->Engine);
		}
#endif

		if ( Viewport->Canvas->pCanvasUtil )
			Viewport->Canvas->pCanvasUtil->Flush();

		RI->PopState();
	}
	else
	{
		RI->PushState();

		if(Viewport->Actor->RendMap == REN_DepthComplexity)
			RI->SetStencilOp(CF_Always,0,0,SO_Keep,SO_Keep,SO_Increment,DEPTH_COMPLEXITY_MASK(Viewport));

		// Clear the screen.
		if(!GIsEditor && Viewport->IsWire())
			RI->Clear();

		// Update the console.
		Viewport->Canvas->Update();

		if ( Viewport->Canvas->pCanvasUtil )
			Viewport->Canvas->pCanvasUtil->Flush();

		// Send all the Interaction PreRenders

		CLOCK(IntPreRen,GStats.DWORDStats(GEngineStats.STATS_Game_InteractionPreRender));

		if ( Viewport->InteractionMaster )
			Viewport->InteractionMaster->MasterProcessPreRender(Viewport);

		UNCLOCK();

		// Fiddle with the projection matrix to produce a letterbox viewport.

		if( Viewport->bRenderCinematics )
		{
			RI->Clear( 1, FColor(0,0,0), 0, 0, 0, 0 );
			Viewport->ScaleFOVY = 1.f / Viewport->CinematicsRatio;
			FLOAT BandScale = Viewport->ScaleFOVY;
			// D-8: ScaleFOVY multiplies the vertical half-ANGLE of the view. When AutoFOV has widened the
			// view (ViewFOV is the effective FOV of this frame), the band must show the vertical extent the
			// 4:3 band shows, so the scale is applied to the base FOV and widened in tan space as the
			// horizontal is; at the reference aspect (no widening) Epic's line above stands alone.
			if( GAutoFovScopeActor == Viewport->Actor && ViewFOV != GAutoFovFrameBase && ViewFOV == GAutoFovFrameEffective )
			{
				DOUBLE HalfBase = Clamp( GAutoFovFrameBase, 1.0f, 170.0f ) * PI / 360.0;
				DOUBLE HalfEff  = ViewFOV * PI / 360.0;
				DOUBLE K        = appTan( HalfEff ) / appTan( HalfBase );
				Viewport->ScaleFOVY = (FLOAT)( appAtan( appTan( HalfBase * BandScale ) * K ) / HalfEff );
			}
			UpdateMatrices();
			FLOAT SizeY		= Viewport->SizeY * BandScale;
			RI->SetViewport( 0, (Viewport->SizeY - SizeY ) / 2, Viewport->SizeX, SizeY );
		}

		// Prerender camera effects.

		INT	LastCameraEffect = 0;

		// scion sz -
		// only certain camera effects work with non-fullscreen viewports
		UBOOL bIsFullScreenViewport = Viewport->IsFullscreen();

		// scion sz -
		// check if camera effects are enabled
		if( Viewport->bShowCameraEffects )
		{
			FUNC_CLOCK(CECyc,GStats.DWORDStats(GEngineStats.STATS_Stats_CameraEffectsCycles));
			for(INT EffectIndex = Controller->CameraEffects.Num() - 1;EffectIndex >= 0;EffectIndex--)
			{
				if( Controller->CameraEffects(EffectIndex)->Enabled )
				{
					if( Controller->CameraEffects(EffectIndex)->FullscreenOnly && !bIsFullScreenViewport )
						continue;

					Controller->CameraEffects(EffectIndex)->PreRender(Viewport,RI);
					LastCameraEffect = EffectIndex;

					if(Controller->CameraEffects(EffectIndex)->FinalEffect)
						break;
				}
			}
		}	

		//jg -- Last second hack to make movie rendering smooth in final match
		if (GRenderLevel == TRUE)
		{
			FLevelSceneNode::Render(RI);
		}
		else
		{
			// uc2 port: nothing else clears the colour buffer of a frame without the level (on the Xbox a movie
			// covered it); TOGGLERENDERLEVEL then shows the HUD on black (D-2's measurements, ui-scale.md).
			RI->Clear( 1, FColor(0,0,0), 0, 1.f, 0, 0 );
		}

		if ( Viewport->Canvas->pCanvasUtil )
			Viewport->Canvas->pCanvasUtil->Flush();

		// scion sz
		// draw 3d hud items here so that they are affected by camera effects
#ifndef FINAL_RELEASE
		if( !GIsEditor && (Viewport->Actor->ShowFlags & SHOW_Actors) )
#endif
		{
			GUglyHackFlags |= 1;
			FUNC_CLOCK(HudCyc,GStats.DWORDStats(GEngineStats.STATS_Game_HUDPostRender));
			if ( Viewport->Actor->myHUD )
				Viewport->Actor->myHUD->eventPostRender3DItems(Viewport->Canvas);
			GUglyHackFlags &= ~1;
		}

		// Undo the letterbox viewport.

		if( Viewport->bRenderCinematics )
		{
			Viewport->ScaleFOVY = 1.f;
			//scion jg -- Pop the viewport change
			RI->ResetViewport();
			UpdateMatrices();
		}

		// PostRender camera effects.

		if( Viewport->bShowCameraEffects )
		{
			FUNC_CLOCK(CECyc,GStats.DWORDStats(GEngineStats.STATS_Stats_CameraEffectsCycles));
			for(INT EffectIndex = LastCameraEffect;EffectIndex < Controller->CameraEffects.Num();EffectIndex++)
			{
				if( Controller->CameraEffects(EffectIndex)->Enabled )
				{
					if( Controller->CameraEffects(EffectIndex)->FullscreenOnly && !bIsFullScreenViewport )
						continue;

					Controller->CameraEffects(EffectIndex)->PostRender(Viewport,RI);

					if(Controller->CameraEffects(EffectIndex)->FinalEffect)
						break;
				}
			}
		}

		// scion sz - keep a one-frame-lagged copy of the back buffer
		// for any materials expecting to use the back buffer copy
		RI->UpdateBackBufferCopyTarget();

		// scion sz --
		// Dyanmic Gamma Adjustment
		RI->UpdateDynamicGamma();

		// Render the HUD and first person weapon.

#ifndef FINAL_RELEASE
		if( !GIsEditor && (Viewport->Actor->ShowFlags & SHOW_Actors) )
#endif
		{
			GUglyHackFlags |= 1;
			FUNC_CLOCK(HudCyc,GStats.DWORDStats(GEngineStats.STATS_Game_HUDPostRender));

			if ( Viewport->Actor->myHUD )
				Viewport->Actor->myHUD->eventPostRender(Viewport->Canvas);

			if ( Viewport->Canvas->pCanvasUtil )
				Viewport->Canvas->pCanvasUtil->Flush();

			GUglyHackFlags &= ~1;
		}

		// Send along PostRenders to the Interactions

		CLOCK(IntPostRen,GStats.DWORDStats(GEngineStats.STATS_Game_InteractionPostRender));

		if ( Viewport->InteractionMaster )
			Viewport->InteractionMaster->MasterProcessPostRender(Viewport);

		UNCLOCK();

#ifndef FINAL_RELEASE
		// Render performance stats.
		CLOCK(StatsCyc,GStats.DWORDStats(GEngineStats.STATS_Stats_RenderCycles));
		GStats.Render( Viewport, Level->Engine );
		if( Level->Engine->bShowSlowestStats )
		{
			GStats.RenderUnitType( Viewport, Level->Engine, STATSUNIT_MSec, &Level->Engine->MinStatMSec );
		}
		UNCLOCK();
#endif

		// Use the stencil buffer to render depth complexity.

#ifndef FINAL_RELEASE
		if(Viewport->Actor->RendMap == REN_DepthComplexity)
		{
			DECLARE_STATIC_UOBJECT(
				UProxyBitmapMaterial,
				HackMaterial,
				{
					static FSolidColorTexture	WhiteTexture(FColor(255,255,255));
					HackMaterial->SetTextureInterface(&WhiteTexture);
				}
				);

			DECLARE_STATIC_UOBJECT(
				UFinalBlend,
				DepthComplexityBlend,
				{
					DepthComplexityBlend->ZTest = 0;
					DepthComplexityBlend->Material = HackMaterial;
				}
				);

			FLOAT	LegendHeight = Viewport->SizeY / 16.0f;

			for(DWORD Stencil = 0;Stencil < 4;Stencil++)
			{
				RI->SetStencilOp(CF_Equal,(Stencil + 1) | ~DEPTH_COMPLEXITY_MASK(Viewport),DEPTH_COMPLEXITY_MASK(Viewport),SO_Keep,SO_Keep,SO_Keep,0);

				FCanvasUtil(&Viewport->RenderTarget,RI).DrawTile(
					0,0,
					Viewport->SizeX - 32,Viewport->SizeY,
					0,0,
					0,0,
					0,
					DepthComplexityBlend,
					FColor(
						Stencil * 64,
						255,
						0
						)
					);

				RI->ResetStencilOp();
				RI->SetStencilOp(CF_Always,0,0,SO_Keep,SO_Keep,SO_Keep,0);

				FCanvasUtil(&Viewport->RenderTarget,RI).DrawTile(
					Viewport->SizeX - 32,Stencil * LegendHeight,
					Viewport->SizeX,(Stencil + 1) * LegendHeight,
					0,0,
					0,0,
					0.0f,
					DepthComplexityBlend,
					FColor(
						Stencil * 64,
						255,
						0
						)
					);
				RI->ResetStencilOp();
			}

			for(DWORD Stencil = 4;Stencil < 16;Stencil++)
			{
				RI->SetStencilOp(CF_Equal,(Stencil + 1) | ~DEPTH_COMPLEXITY_MASK(Viewport),DEPTH_COMPLEXITY_MASK(Viewport),SO_Keep,SO_Keep,SO_Keep,0);

				FCanvasUtil(&Viewport->RenderTarget,RI).DrawTile(
					0,0,
					Viewport->SizeX - 32,Viewport->SizeY,
					0,0,
					0,0,
					0,
					DepthComplexityBlend,
					FColor(
						255,
						231 - (Stencil - 4) * 21,
						0
						)
					);

				RI->ResetStencilOp();

				RI->SetStencilOp(CF_Always,0,0,SO_Keep,SO_Keep,SO_Keep,0);

				FCanvasUtil(&Viewport->RenderTarget,RI).DrawTile(
					Viewport->SizeX - 32,Stencil * LegendHeight,
					Viewport->SizeX,(Stencil + 1) * LegendHeight,
					0,0,
					0,0,
					0.0f,
					DepthComplexityBlend,
					FColor(
						255,
						231 - (Stencil - 4) * 21,
						0
						)
					);

				RI->ResetStencilOp();
			}
		}
#endif

		UpdateVertexPools(Viewport);

#ifdef _XBOX
		//jg -- Hack to display "stat net" for debugging purposes
		extern UBOOL GShowStatNet;
		if (GShowStatNet == TRUE)
		{
			extern void RenderStatNet(UViewport* Viewport,UEngine* Engine);
			RenderStatNet(Viewport,Level->Engine);
		}
#endif

		if ( Viewport->Canvas->pCanvasUtil )
			Viewport->Canvas->pCanvasUtil->Flush();

	#ifndef FINAL_RELEASE
		//scion jg -- Reset stencil state
		if(Viewport->Actor->RendMap == REN_DepthComplexity)
		{
			RI->ResetStencilOp();
		}
	#endif

		RI->PopState();
	}

	unguard;
}

//
//	FActorSceneNode::FActorSceneNode
//
FActorSceneNode::FActorSceneNode(UViewport* InViewport,FRenderTarget* InRenderTarget,AActor* InActor,AActor* CameraActor,FVector CameraLocation,FRotator CameraRotation,FLOAT CameraFOV) : FCameraSceneNode(InViewport,InRenderTarget,CameraActor,CameraLocation,CameraRotation,CameraFOV)
{
	RenderActor = InActor;
}

/* scion ======================================================================
 * class FActorSceneNodeDeferredActors
 * Author: sz
 * 
 * Holds unsorted lists of solid, emissive, translucent, and postprocess actors
 * ============================================================================
 */
class FActorSceneNodeDeferredActors
{
public:
	enum EDeferredType
	{
		SOLID_TYPE,
		EMISSIVE_TYPE,
		TRANSLUCENT_TYPE,
		POSTPROCESS_TYPE,
		MAX_TYPE
	};
	// list of deferred actors
	TList<FDynamicActor*>* DrawLists[MAX_TYPE];

	// everything needed for rendering each actor
	FRenderInterface* RI;
	FActorSceneNode* SceneNode;
	TList<FDynamicLight*>*	ActorLights;
	TList<FProjectorRenderInfo*>* ActorProjectors;
	
	// constructor
	FActorSceneNodeDeferredActors( FRenderInterface* InRI,
		FActorSceneNode* InSceneNode, TList<FDynamicLight*>* InActorLights,
		TList<FProjectorRenderInfo*>* InActorProjectors ) :
		RI(InRI),
		SceneNode(InSceneNode),
		ActorLights(InActorLights),
		ActorProjectors(InActorProjectors)
	{
		check( RI && SceneNode );
		appMemzero( DrawLists, sizeof(DrawLists) );
	}

	FORCEINLINE void AddToList( FDynamicActor* DynamicActor, EDeferredType Type )
	{
		DrawLists[Type] = new(GSceneMem) TList<FDynamicActor*>( DynamicActor, DrawLists[Type] );
	}

	// add a dynamic actor and all of its attachments
	// to their corresponding deferred lists
	void AddActor( FDynamicActor* DynamicActor )
	{
		guard(FActorSceneNodeDeferredActors::AddActor);

		// refresh sorting flags
		DynamicActor->UpdateSorting();
		
		// update any mesh attachments and add them to their draw lists 
		if( DynamicActor->Actor->IsDTMesh() &&
			DynamicActor->Actor->Mesh != NULL &&
			DynamicActor->Actor->Attached.Num() )
		{
			USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(DynamicActor->Actor->Mesh);
			if( SkelMesh )
			{
				// update the animation for this parent actor
				USkeletalMeshInstance *MInst= ((USkeletalMeshInstance*)SkelMesh->MeshGetInstance(DynamicActor->Actor));
				if( MInst ) 
				{
					INT LODRequest;
					MInst->GetFrame(DynamicActor->Actor,SceneNode,NULL,0,LODRequest,GF_BonesOnly);
				}				

				for( INT Idx=0; Idx < DynamicActor->Actor->Attached.Num(); Idx++ )
				{
					AActor* AttachedActor = DynamicActor->Actor->Attached(Idx);
					if ( AttachedActor && !AttachedActor->bDeleteMe )
					{
						// check for bHidden,etc
						UBOOL bShouldDrawAttachment = SceneNode->FilterAttachment( AttachedActor );
						if( bShouldDrawAttachment ) 
						{
							bShouldDrawAttachment = SceneNode->FilterActor( AttachedActor, 1 );
						}
						// if attached to a bone and not already updated
						if( bShouldDrawAttachment &&
							AttachedActor->AttachmentBone != NAME_None &&
							AttachedActor->bForceUpdateWhenAttached == 0 )
						{
							// update location
							bShouldDrawAttachment = SkelMesh->SetAttachmentLocation( DynamicActor->Actor, AttachedActor );					 
						}
						// if valid bone attachment or just a valid attachment
						if( bShouldDrawAttachment )	
						{
							// refresh 
							AttachedActor->ClearRenderData();
							// get render data (may update if necessary)
							FDynamicActor* AttachedDynActor = AttachedActor->GetActorRenderData();
							// recurse on attached actor to add it to one of the deferred lists
							AddActor( AttachedDynActor );
						}
					}
				}
			}			
		}

		// Add to list of Emissive items. If Emissive then assume !PostPRocess && !Translucent
		if( DynamicActor->IsEmissive )
		{
			AddToList( DynamicActor, EMISSIVE_TYPE );
		}
		// Add to list of Translucent items.  These are sorted. Translucent BSP nodes are also in this list.
		else if( DynamicActor->IsTranslucent )
		{
			AddToList( DynamicActor, TRANSLUCENT_TYPE );
			
			// if IsPostProcess && IsTranslucent then it is a blended distortion material
			if( DynamicActor->IsPostProcess > POSTPROCESS_NONE )
			{
				AddToList( DynamicActor, POSTPROCESS_TYPE );
			}
		}
		// Add to list of PostProcess items. Note that only emitters can be both PostProcess && Translucent
		else if( DynamicActor->IsPostProcess > POSTPROCESS_NONE )
		{
			AddToList( DynamicActor, POSTPROCESS_TYPE );
		}
		// default list for all solids
		else
		{			
			AddToList( DynamicActor, SOLID_TYPE );
		}		

		unguard;
	}
	
	// check if there are any actors to render of the
	// given type
	FORCEINLINE UBOOL HasActorsType( EDeferredType Type )
	{
		guard(FActorSceneNodeDeferredActors::HasActorsType);		
		return( DrawLists[Type] != NULL );
		unguard;
	}

	// render all the actors of the given type
	FORCEINLINE void Render( EDeferredType Type )
	{	
		guard(FActorSceneNodeDeferredActors::Render);

		// skip if nothing to render
		if( DrawLists[Type] == NULL )
			return;

		// solids should overwrite the stencil cut-outs for pawns
		// used for camera effects (berskert,etc)
		UBOOL bUseStencil = RI->IsDefaultRenderTarget() && (Type == SOLID_TYPE || Type == EMISSIVE_TYPE);
		if( bUseStencil )
		{
			RI->SetStencilOp(CF_Always,0xCE,0xFFFFFFFF,SO_Keep,SO_Keep,SO_Replace,0xFF);
		}

		// render distortion items
		if( Type == POSTPROCESS_TYPE )
		{
			FPostProcessRender PostProcessDraw( DrawLists[Type], SceneNode, RI );
			PostProcessDraw.Render();
		}
		// everything else
		else
		{
			for( TList<FDynamicActor*>* RenderList = DrawLists[Type]; RenderList; RenderList = RenderList->Next )
			{
				RenderList->Element->Render( SceneNode, ActorLights, ActorProjectors, RI );
			}
		}

		// restore stencil state
		if( bUseStencil )
		{
			RI->ResetStencilOp();
		}

		unguard;
	}	
};

//
//	FActorSceneNode::Render
//
void FActorSceneNode::Render(FRenderInterface* RI)
{
	guard(FActorSceneNode::Render);

	if( RenderActor->bDeleteMe )
		return;

	FMemMark	MemMark(GSceneMem);

	RI->PushState();

	// Set the view and projection matrices.

	if( Viewport->Actor->UseFixedVisibility )
		RI->SetTransform(TT_WorldToCamera,Viewport->Actor->RenderWorldToCamera);
	else
		RI->SetTransform(TT_WorldToCamera,WorldToCamera);
	RI->SetTransform(TT_CameraToScreen,CameraToScreen);

	// Cache actor rendering data.
	FDynamicActor*	DynamicActor = RenderActor->GetActorRenderData();

	// is this a shadow actor scene node
	UBOOL bShadowScene = IsShadowSceneNode();

	// Find lights that affect this actor.
	if( !bShadowScene && 
		Viewport->IsLit() && 
		DynamicActor->LastLightUpdateTime != RenderActor->Level->TimeSeconds )
	{
		// need to update these
		DynamicActor->EmptyLights();
		DynamicActor->EmptyProjectors();
		// set cur time since it is updating
		DynamicActor->LastLightUpdateTime = RenderActor->Level->TimeSeconds;

		FDynamicLight*	Consider[256];
		INT				NumConsider = 0;

		for(INT LeafIndex = 0;LeafIndex < RenderActor->Leaves.Num();LeafIndex++)
		{
			FLeaf&	Leaf = Model->Leaves(RenderActor->Leaves(LeafIndex));

			if(Leaf.iPermeating >= 0 && NumConsider < 256)
				for(INT PermeatingIndex = Leaf.iPermeating;Model->Lights(PermeatingIndex);PermeatingIndex++)
				{
					AActor*			LightActor = Model->Lights(PermeatingIndex);

					// scion sz - skip invalidated lights
					if( LightActor == (AActor*)FLeaf::INVALID_LIGHT ||
						// shouldn't ever happen since these are static lights 
						// but doesn't hurt to check
						LightActor->bDeleteMe )
						continue;

					FDynamicLight*	DynamicLight = LightActor->GetLightRenderData();

					// scion sz - skip if already in use instead of 
					// searching the consider[] array for each light
					if( DynamicLight->IsUsed() ) {
						continue;
					}
					// mark as used
					DynamicLight->MarkUsed();

					// Cull lights.
					if(RenderActor->DrawType == DT_StaticMesh && RenderActor->bStatic && !RenderActor->bLightChanged && !(DynamicLight->Dynamic || DynamicLight->Changed))
						continue;

					if(DynamicLight->Actor->bSpecialLit != RenderActor->bSpecialLit)
						continue;

					FLOAT	DistanceSquared = (DynamicLight->Position - DynamicActor->BoundingSphere).SizeSquared();

					if(LightActor->LightEffect != LE_Sunlight && DistanceSquared > Square(DynamicLight->Radius + DynamicActor->BoundingSphere.W))
						continue;

					// scion sz - removed
					/*
					UBOOL	Exists = 0;
					for(INT ConsiderIndex = 0;ConsiderIndex < NumConsider;ConsiderIndex++)
						if(Consider[ConsiderIndex] == DynamicLight)
						{
							Exists = 1;
							break;
						}

					if(Exists)
						continue;
					*/

					Consider[NumConsider++] = DynamicLight;

					if(NumConsider >= 256)
						break;
				}
		}

		// scion sz - also use dynamic lights
		for( INT ActorIdx=Level->iFirstDynamicActor; ActorIdx < Level->Actors.Num(); ActorIdx++ )
		{
			AActor* LightActor = Level->Actors(ActorIdx);
			if( LightActor && 
				!LightActor->bDeleteMe && 
				!LightActor->bStatic &&
				LightActor->LightType != LT_None &&
				LightActor->bDynamicLight )
			{
				FDynamicLight*	DynamicLight = LightActor->GetLightRenderData();

				// skip if already in use
				if( DynamicLight->IsUsed() ) {
					continue;
				}
				// mark as used
				DynamicLight->MarkUsed();
				// update the light since we know it is dynamic
				DynamicLight->Update();
                // dist squared to actor
				FLOAT DistanceSquared = (DynamicLight->Position - DynamicActor->BoundingSphere).SizeSquared();
				// ignore light if this actor is outside of its radius
				if(LightActor->LightEffect != LE_Sunlight && DistanceSquared > Square(DynamicLight->Radius + DynamicActor->BoundingSphere.W))
						continue;
                // use the light
				Consider[NumConsider++] = DynamicLight;
                // make sure we dont have too many
				if(NumConsider >= 256)
						break;
                
			}
		}

		// scion sz - clear used flags
		FDynamicLight::UnMarkAllUsed();

		DynamicActor->ActorLights = GetRelevantLights(this,DynamicActor,DynamicActor->BoundingSphere,Consider,NumConsider);
	}

	// Find dynamic projectors that affect this actor.
	if( !bShadowScene &&
		RenderActor->bAcceptsProjectors)
	{
		for(INT ProjectorIndex = 0;ProjectorIndex < Level->DynamicProjectors.Num();ProjectorIndex++)
		{
			FProjectorRenderInfo*	ProjectorInfo = Level->DynamicProjectors(ProjectorIndex);

			if(!ProjectorInfo->Render(Level->GetLevelInfo()->TimeSeconds))
			{
				// Replace this projector with the last dynamic projector, and remove an item from the end of the array.
				Level->DynamicProjectors(ProjectorIndex--) = Level->DynamicProjectors(Level->DynamicProjectors.Num() - 1);
				Level->DynamicProjectors.Remove(Level->DynamicProjectors.Num() - 1);
				continue;
			}

			check(ProjectorInfo->Projector); // There should never be a dynamic projector without an attached actor!

			if(FilterProjector(ProjectorInfo->Projector))
			{
				if(!ProjectorInfo->Projector->bProjectActor && RenderActor->Mesh)
					continue;

				if(!ProjectorInfo->Projector->bProjectStaticMesh && RenderActor->StaticMesh)
					continue;

				UBOOL	Inside = 1;
				FVector	ActorCenter = DynamicActor->BoundingBox.GetCenter(),
						ActorExtent = DynamicActor->BoundingBox.GetExtent();

				for(INT PlaneIndex = 0;PlaneIndex < 6;PlaneIndex++)
				{
					FLOAT	PushOut = FBoxPushOut(ProjectorInfo->FrustumPlanes[PlaneIndex],ActorExtent),
							Dist = ProjectorInfo->FrustumPlanes[PlaneIndex].PlaneDot(ActorCenter);

					if(Dist < -PushOut)
					{
						Inside = 0;
						break;
					}
				}

				if(Inside)
				{
					DynamicActor->ActorProjectors = AddProjectorListEntry( ProjectorInfo, DynamicActor->ActorProjectors );
				}
			}
		}
	}

#if 0
	if( !bShadowScene ) 
	{
		// scion sz - 
		// this is only needed if the sniper rifle
		// back buffer material is using a blurred copy
		RI->UpdateBackBufferCopyTarget();
	}
#endif

	if( FilterAttachment( DynamicActor->Actor ) )
	{
		// scion sz - 
		// create lists of deferred actors
		FActorSceneNodeDeferredActors DeferActors( RI, this, DynamicActor->ActorLights, DynamicActor->ActorProjectors );
		// add actor to the deferred lists
		DeferActors.AddActor( DynamicActor );
		// render solids
		DeferActors.Render( FActorSceneNodeDeferredActors::SOLID_TYPE );
		// render emissives
		DeferActors.Render( FActorSceneNodeDeferredActors::EMISSIVE_TYPE );
		// render translucents
		DeferActors.Render( FActorSceneNodeDeferredActors::TRANSLUCENT_TYPE );
		// render post process
		DeferActors.Render( FActorSceneNodeDeferredActors::POSTPROCESS_TYPE );
	}	

	RI->PopState();

	MemMark.Pop();

	if( !bShadowScene )
		GProjectorBatchList = NULL;

	unguard;
}

//
//	FSkySceneNode::FSkySceneNode
//
FSkySceneNode::FSkySceneNode(FLevelSceneNode* InParent,INT InViewZone) :
	FLevelSceneNode(
		InParent,
		InViewZone,
		FTranslationMatrix(-InParent->Level->GetZoneActor(InViewZone)->Location) *
			FInverseRotationMatrix(InParent->Level->GetZoneActor(InViewZone)->Rotation) *
			FTranslationMatrix(InParent->ViewOrigin)
		)
{
}

//
// MirrorMatrix
//

FMatrix MirrorMatrix(FPlane Plane)
{
	FVector	Origin = -2 * Plane * -Plane.W,
			X = FVector(1,0,0) - 2 * Plane * (FVector(1,0,0) | Plane),
			Y = FVector(0,1,0) - 2 * Plane * (FVector(0,1,0) | Plane),
			Z = FVector(0,0,1) - 2 * Plane * (FVector(0,0,1) | Plane);
	FMatrix	Mirrored(
				FPlane(	X.X,			Y.X,			Z.X,			0	),
				FPlane(	X.Y,			Y.Y,			Z.Y,			0	),
				FPlane(	X.Z,			Y.Z,			Z.Z,			0	),
				FPlane(	-Origin | X,	-Origin | Y,	-Origin | Z,	1	)
				);

	return Mirrored;
}

//
//	FMirrorSceneNode::FMirrorSceneNode
//
FMirrorSceneNode::FMirrorSceneNode(FLevelSceneNode* InParent,FPlane MirrorPlane,INT InViewZone,INT InMirrorSurface) :
	FLevelSceneNode(
		InParent,
		InViewZone,
		MirrorMatrix(
			MirrorPlane
			)
		)
{
	MirrorSurface = InMirrorSurface;
}

/* scion =====================================================================
* FInteractionRenderSceneNode::FInteractionRenderSceneNode
* Author: sz
* ============================================================================
* Initialize rendering setting for this scene (set view matrix, actor zone, etc)
* (splitscreen)
* ============================================================================
*/
FInteractionRenderSceneNode::FInteractionRenderSceneNode( UViewport* InViewport, FRenderTarget* InRenderTarget, AActor* CameraActor, FVector CameraLocation, FRotator CameraRotation, FLOAT CameraFOV ) :
	FCameraSceneNode(
		InViewport,
		InRenderTarget,
		CameraActor,
		CameraLocation,
		CameraRotation,
		CameraFOV 
		)
{
}

/* scion =====================================================================
* FInteractionRenderSceneNode::Render
* Author: sz
* ============================================================================
* Scene node that only renders the menu (interactions)
* It also can render camera effects. (splitscreen)
* ============================================================================
*/
void FInteractionRenderSceneNode::Render( FRenderInterface* RI )
{
	guard(FInteractionRenderSceneNode::Render);
	
	RI->PushState();

	APlayerController*	Controller = Viewport->Actor;

	// Update the canvas render settings
	Viewport->Canvas->Update();

	// only certain camera effects work with non-fullscreen viewports
	UBOOL bIsFullScreenViewport = Viewport->IsFullscreen();
	INT	LastCameraEffect = 0;

	// PreRender camera effects
	if( Viewport->bShowCameraEffects )
	{
		FUNC_CLOCK(CECyc,GStats.DWORDStats(GEngineStats.STATS_Stats_CameraEffectsCycles));
		for(INT EffectIndex = Controller->CameraEffects.Num() - 1;EffectIndex >= 0;EffectIndex--)
		{
			if( Controller->CameraEffects(EffectIndex)->Enabled )
			{
				if( Controller->CameraEffects(EffectIndex)->FullscreenOnly && !bIsFullScreenViewport )
					continue;

				Controller->CameraEffects(EffectIndex)->PreRender(Viewport,RI);
				LastCameraEffect = EffectIndex;

				if(Controller->CameraEffects(EffectIndex)->FinalEffect)
					break;
			}
		}
	}

	// PostRender camera effects.
	if( Viewport->bShowCameraEffects )
	{
		FUNC_CLOCK(CECyc,GStats.DWORDStats(GEngineStats.STATS_Stats_CameraEffectsCycles));
		for(INT EffectIndex = LastCameraEffect;EffectIndex < Controller->CameraEffects.Num();EffectIndex++)
		{
			if( Controller->CameraEffects(EffectIndex)->Enabled )
			{
				if( Controller->CameraEffects(EffectIndex)->FullscreenOnly && !bIsFullScreenViewport )
					continue;

				Controller->CameraEffects(EffectIndex)->PostRender(Viewport,RI);

				if(Controller->CameraEffects(EffectIndex)->FinalEffect)
					break;
			}
		}
	}	

	// Send along PreRenders to the Interactions	
	if ( Viewport->InteractionMaster )
	{
		FUNC_CLOCK(IntPreRen,GStats.DWORDStats(GEngineStats.STATS_Game_InteractionPreRender));
		Viewport->InteractionMaster->MasterProcessPreRender(Viewport);
	}
	
	// Send along PostRenders to the Interactions	
	if ( Viewport->InteractionMaster )
	{
		FUNC_CLOCK(IntPostRen,GStats.DWORDStats(GEngineStats.STATS_Game_InteractionPostRender));
		Viewport->InteractionMaster->MasterProcessPostRender(Viewport);
	}

	RI->PopState();

	unguard;
}

//
//  Globals.
//
FSceneMemStack	GSceneMem;
// scion sz -
//FSceneMemStack GSceneActorLightingMem;
// scion sz -
// the singleton pool of light lists
#if defined(_MSC_VER)
TPooledList<FLightList>::TPoolInstance<FLightList> TPooledList<FLightList>::PoolInstance;
#else
template<> TPooledList<FLightList>::TPoolInstance<FLightList> TPooledList<FLightList>::PoolInstance{};
#endif
// the singleton pool of projector lists
#if defined(_MSC_VER)
TPooledList<FProjectorList>::TPoolInstance<FProjectorList> TPooledList<FProjectorList>::PoolInstance;
#else
template<> TPooledList<FProjectorList>::TPoolInstance<FProjectorList> TPooledList<FProjectorList>::PoolInstance{};
#endif


//
//  FSceneMemStack::FSceneMemStack
//
FSceneMemStack::FSceneMemStack()
{
	Init(32*1024);
}

//
//  FSceneMemStack::~FSceneMemStack
//
FSceneMemStack::~FSceneMemStack()
{
	Exit();
}


/* scion ======================================================================
 * DebugDrawRenderTarget
 * Author: sz
 * ============================================================================
 * For debug. Render a render target to the screen
 * ============================================================================
 */
void DebugDrawRenderTarget( FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2, FColor& Color,
	FRenderTarget* InTarget, FSceneNode* SceneNode, FRenderInterface* RI )
{
	check(InTarget && RI);

	// proxy bitmap for the render target surface
	DECLARE_STATIC_UOBJECT( UProxyBitmapMaterial, SrcMaterial, {});
	SrcMaterial->SetTextureInterface(InTarget);
	
	// matrix for u,v to s,t conversion
	DECLARE_STATIC_UOBJECT(	UTexMatrix,	SrcMaterialMat, 
	{ 
		SrcMaterialMat->Material = SrcMaterial; 
		SrcMaterialMat->Matrix.SetIdentity();
	});

	// in case we have alpha
	DECLARE_STATIC_UOBJECT( UFinalBlend, SrcMaterialFB, 
	{
		SrcMaterialFB->FrameBufferBlending=FB_AlphaBlend;
		SrcMaterialFB->ZWrite=0;
		SrcMaterialFB->ZTest=0;	
		SrcMaterialFB->Material=SrcMaterialMat;
	});

	FLOAT U1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	FLOAT V1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	FLOAT U2[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	FLOAT V2[4] = {1.0f, 1.0f, 1.0f, 1.0f};

	FScreenPrimUtil( SceneNode->Viewport, RI).DrawTile(
		X1, Y1, X2, Y2,
		U1, V1,	U2, V2,
		0.0f,
		SrcMaterialFB,
		Color
		);
}

/* scion ======================================================================
 * RenderPostProcess
 * Author: sz
 * ============================================================================
 *
 * Distortion rendering is done in the following order:
 *
 * 1. Render using blend color for all actors with blend type distortion materials 
 * 2. copy the current zbuffer to a resized off-screen target
 * 2. Render all actors with distortion to an off-screen accumulation target
 * 3. Use the off-screen target to render a full screen quad that distorts the screen
 *
 * ============================================================================
 */
void FPostProcessRender::Render()
{
	guard(RenderPostProcess);

	if( !PPDrawList )
		return;

	// UC2 port (M7): the rest of this function was "#ifdef _XBOX". It runs when the render
	// device has the post-process capability (FRenderCaps::XboxPostProcess). Without it the
	// list is empty anyway: no material answers IsPostProcess() (PSDistortionBaseMaterial).
#ifndef _XBOX
	if( !RenDevHasXboxPostProcess() )
		return;
#endif
#if OLD_DISTORT_METHOD 
	// old technique - renders distortion in-place

	// blit back buffer texture
	RI->UpdateBackBufferCopyTarget();

	// second render distortion as just accumulated values
	RI->SetPostProcessStage( FRenderInterface::PP_STAGE_RENDER_DISTORTION );
	SceneNode->FilterMode = FSceneNode::Filter_PostProcess;
	for(TList<FDynamicActor*>* PostProcessDrawList = PPDrawList; PostProcessDrawList; PostProcessDrawList = PostProcessDrawList->Next)
	{
		FDynamicActor* DynamicActor = PostProcessDrawList->Element;
		checkSlow(DynamicActor);

		// render actor
		RI->PushState();
		RI->SetCullMode(DynamicActor->Determinant * SceneNode->Determinant < 0.0f ? CM_CCW : CM_CW);
		DynamicActor->Render( SceneNode,NULL,NULL,RI );
		RI->PopState();
		
		STAT_INC(GStats.DWORDStats( GEngineStats.STATS_Visibility_VisibleObjects ));
	}
#else
	// render any deferred actors that use the back buffre
	SceneNode->FilterMode = FSceneNode::Filter_PostProcess;
	for(TList<FDynamicActor*>* PostProcessDrawList = PPDrawList; PostProcessDrawList; PostProcessDrawList = PostProcessDrawList->Next)
	{
		FDynamicActor* DynamicActor = PostProcessDrawList->Element;
		checkSlow(DynamicActor);

		if( DynamicActor->IsPostProcess != POSTPROCESS_BACKBUFFER )
			continue;

		// render actor
		RI->PushState();
		RI->SetCullMode(DynamicActor->Determinant * SceneNode->Determinant < 0.0f ? CM_CCW : CM_CW);
		DynamicActor->Render( SceneNode,NULL,NULL,RI );
		DynamicActor->SetVisible(SceneNode->Viewport,1);
		RI->PopState();

		STAT_INC(GStats.DWORDStats( GEngineStats.STATS_Visibility_VisibleObjects ));
	}

	// get a pointer to the off-screen accumulation target
	FRenderTarget* BBCopyTarget = RI->GetBackBufferCopyTarget();
    // copy the current z-buffer to the off-screen target z-buffer
	RI->CopyDepthBuffer( BBCopyTarget, RI->GetRenderTarget() );
	// set render target the offscreen accumulation target
	RI->SetRenderTarget( BBCopyTarget );
	// clear to 0 rgb since that represents 0 distortion
	// clear to 0 alpha and then enable alpha writes when rendering
	// geometry.  This allows for only drawing areas with distortion later
	RI->Clear(1,FColor(0,0,255,0),0,0.f,0,0);

	// first, render distortion as just accumulated values
	RI->SetPostProcessStage( FRenderInterface::PP_STAGE_ACCUMULATE );

	// Render Post Processing effects - Distortions, Refraction, etc.
	SceneNode->FilterMode = FSceneNode::Filter_PostProcess;
	for(TList<FDynamicActor*>* PostProcessDrawList = PPDrawList; PostProcessDrawList; PostProcessDrawList = PostProcessDrawList->Next)
	{
		FDynamicActor* DynamicActor = PostProcessDrawList->Element;
		checkSlow(DynamicActor);

		if( DynamicActor->IsPostProcess != POSTPROCESS_BLEND &&
			DynamicActor->IsPostProcess != POSTPROCESS_NOBLEND )
			continue;

		// render actor
		RI->PushState();
		RI->SetCullMode(DynamicActor->Determinant * SceneNode->Determinant < 0.0f ? CM_CCW : CM_CW);
		DynamicActor->Render( SceneNode,NULL,NULL,RI );
		DynamicActor->SetVisible(SceneNode->Viewport,1);
		RI->PopState();

		STAT_INC(GStats.DWORDStats( GEngineStats.STATS_Visibility_VisibleObjects ));
	}
	// restore filter mode
	SceneNode->FilterMode = FSceneNode::Filter_All;
	// restore viewport and render target
	RI->ResetRenderTarget();

	// second, render fullscreen distortion 
	RI->SetPostProcessStage( FRenderInterface::PP_STAGE_RENDER_DISTORTION );
	// this is the accumulation target
	UMaterial* BBCopyMat = RI->GetBackBufferCopyMaterial();
	// tex matrix modifier for u,v to s,t conversion
	DECLARE_STATIC_UOBJECT( UTexMatrix, BBCopyMatTexMatrix,
	{
		BBCopyMatTexMatrix->Matrix.SetIdentity();
		BBCopyMatTexMatrix->Material = BBCopyMat;
	});
	// distortion material for fullscreen quad
	DECLARE_STATIC_UOBJECT( UPSDistortionMaterial, DistortMat, 
	{
		DistortMat->DistortionMaterial = BBCopyMatTexMatrix;
		// fill rate optimization iot use AlphaRef test to reject pixels
		DistortMat->Mask = BBCopyMatTexMatrix;
	});
	// maximum screen space distortion in pixels
	DistortMat->DistortionAmount = (UTexture::__Client) ? UTexture::__Client->MaxDistortion : 20.f;
	// uvs
	static FLOAT U1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	static FLOAT V1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	static FLOAT U2[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	static FLOAT V2[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	static FColor FullColor(255,255,255,255);
	// render a full screen quad using the distortion accumulation target
	// this will actually distort the back buffer	
	FScreenPrimUtil( SceneNode->Viewport, RI).DrawTile(
		0, 0, SceneNode->Viewport->SizeX, SceneNode->Viewport->SizeY,
		U1, V1,	U2, V2,
		0.0f,
		DistortMat,
		FullColor
		);

	// last, render color values from blended distortion materials
	RI->SetPostProcessStage( FRenderInterface::PP_STAGE_RENDER_COLOR );

	// Render Post Processing effects - Distortions, Refraction, etc.
	SceneNode->FilterMode = FSceneNode::Filter_PostProcess;
	for(TList<FDynamicActor*>* PostProcessDrawList = PPDrawList; PostProcessDrawList; PostProcessDrawList = PostProcessDrawList->Next)
	{
		FDynamicActor* DynamicActor = PostProcessDrawList->Element;
		checkSlow(DynamicActor);

		if( DynamicActor->IsPostProcess != POSTPROCESS_BLEND )
			continue;

		// render actor
		RI->PushState();
		RI->SetCullMode(DynamicActor->Determinant * SceneNode->Determinant < 0.0f ? CM_CCW : CM_CW);
		DynamicActor->Render( SceneNode,NULL,NULL,RI );
		DynamicActor->SetVisible(SceneNode->Viewport,1);
		RI->PopState();
		
		STAT_INC(GStats.DWORDStats( GEngineStats.STATS_Visibility_VisibleObjects ));
	}
	// restore filter mode
	SceneNode->FilterMode = FSceneNode::Filter_All;

	RI->SetPostProcessStage( FRenderInterface::PP_STAGE_NONE );

	//DEBUG
	static UBOOL DEBUGSZ_ShowBBCopy=0;
	if( DEBUGSZ_ShowBBCopy )
	{
		FColor BBColor(255,255,255,127);
		DebugDrawRenderTarget( 20,20,150,120,BBColor,BBCopyTarget,SceneNode,RI );
	}
	
#endif

	unguard;
}