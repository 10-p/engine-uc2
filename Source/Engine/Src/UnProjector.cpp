/*=============================================================================
	UnProjector.cpp: Projected textures & Decals
	Copyright 2000 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

#include "EnginePrivate.h"
#include "UnRenderPrivate.h"
#include <float.h>

ENGINE_API DWORD GProjectorRenderInfoGeneration = 0;	// UnProjector.h

//
// FProjectorRenderInfo::GetMaterial
//
UMaterial* FProjectorRenderInfo::GetMaterial(FSceneNode* SceneNode,UMaterial* BaseMaterial)
{
	guard(FProjectorRenderInfo::GetMaterial);

	DECLARE_STATIC_UOBJECT(
		UProjectorMaterial,
		ProjectorMaterial,
		{
		}
	);

	UShader*		BaseShader = Cast<UShader>(BaseMaterial);
	UFinalBlend*	BaseFinalBlend = Cast<UFinalBlend>(BaseMaterial);

	if(BaseShader)
		ProjectorMaterial->bTwoSided = BaseShader->TwoSided;
	else if(BaseFinalBlend)
		ProjectorMaterial->bTwoSided = BaseFinalBlend->TwoSided;
	else
		ProjectorMaterial->bTwoSided = 0;

	ProjectorMaterial->Gradient = GradientTexture;
	ProjectorMaterial->Projected = Material;
	ProjectorMaterial->BaseMaterial = BaseMaterial;
	ProjectorMaterial->BaseMaterialBlending = MaterialBlendingOp;
	ProjectorMaterial->FrameBufferBlending = FrameBufferBlendingOp;
	ProjectorMaterial->Matrix = Matrix;
	ProjectorMaterial->GradientMatrix = GradientMatrix;

	ProjectorMaterial->bProjected = (ProjectorFlags & PRF_Projected) ? 1 : 0;
	ProjectorMaterial->bProjectOnUnlit = (ProjectorFlags & PRF_ProjectOnUnlit) ? 1 : 0;
	ProjectorMaterial->bGradient = (ProjectorFlags & PRF_Gradient) ? 1 : 0;
	ProjectorMaterial->bProjectOnAlpha = (ProjectorFlags & PRF_ProjectOnAlpha) ? 1 : 0;
	ProjectorMaterial->bProjectOnBackfaces = (ProjectorFlags & PRF_ProjectOnBackfaces) ? 1 : 0;

	ProjectorMaterial->bStaticProjector = 0;

	return ProjectorMaterial;

	unguard;
}

/* scion ======================================================================
* UProjectorMultiMaterial::AddProjectorEntry
* Author: sz
*
* Add the projector render info to the list of entries for this batched
* prjector material.
* ============================================================================
*/
void UProjectorMultiMaterial::AddProjectorEntry( FProjectorRenderInfo* ProjRenInfo )
{
	guardSlow( UProjectorMultiMaterial::AddProjectorEntry );
	checkSlow( ProjRenInfo );
	checkSlow( FrameBufferBlending == ProjRenInfo->FrameBufferBlendingOp );

	if( ProjRenInfo->Projector )
	{
		RenderEntries.AddItem( ProjRenInfo );
	}	

	unguardSlow;
}

/* scion ======================================================================
* UProjectorMultiMaterial::ResetProjectorEntries
* Author: sz
*
* Reset projector entries array.
* ============================================================================
*/
void UProjectorMultiMaterial::ResetProjectorEntries()
{
	guardSlow( UProjectorMultiMaterial::ResetProjectorEntries );

	RenderEntries.Reset();

	unguardSlow;
}

/*-----------------------------------------------------------------------------
	UProjectorPrimitive
-----------------------------------------------------------------------------*/
UProjectorPrimitive* UProjectorPrimitive::GProjectorPrimitive = NULL;

/* scion ======================================================================
* UProjectorPrimitive::GetStaticInstance
* Author: sz
*
* ============================================================================
*/
UProjectorPrimitive* UProjectorPrimitive::GetStaticInstance()
{    
	guard(UProjectorPrimitive::GetStaticInstance);

	if( !GProjectorPrimitive )
		GProjectorPrimitive = ConstructObject<UProjectorPrimitive>(UProjectorPrimitive::StaticClass());

	return GProjectorPrimitive;

	unguard;
}

UBOOL UProjectorPrimitive::UseCylinderCollision( const AActor* Owner )
{
	guardSlow(UProjectorPrimitive::UseCylinderCollision);

	return false;
	unguardSlow;
}

FBox UProjectorPrimitive::GetCollisionBoundingBox( const AActor* Actor ) const
{
	guard(UProjectorPrimitive::GetCollisionBoundingBox);
	return ((AProjector*)Actor)->Box;
	unguard;
}

//scion jg -- Changed to pass by ref
UBOOL UProjectorPrimitive::PointCheck
(
	FCheckResult	&Result,
	AActor			*Actor,
	const FVector&	Location,
	const FVector&	Extent,
	DWORD           ExtraNodeFlags
)
{
	AProjector* Projector = Cast<AProjector>(Actor);
	for( INT i=0;i<6;i++ )
	{
		FLOAT PushOut = FBoxPushOut( Projector->FrustumPlanes[i], Extent );
		FLOAT Dist = Projector->FrustumPlanes[i].PlaneDot(Location);
		if( Dist < -PushOut )
			return 1;
	}

	// Also check against planes of bounding box.
	FBox bigBox = Projector->Box;
	bigBox.Max += Extent;
	bigBox.Min -= Extent;

	FPlane Planes[6];
	Planes[0] = FPlane( 0.f, 0.f, 1.0, bigBox.Min.Z);
	Planes[1] = FPlane( 0.f, 0.f,-1.0,-bigBox.Max.Z);
	Planes[2] = FPlane( 1.f, 0.f, 0.0, bigBox.Min.X);
	Planes[3] = FPlane(-1.f, 0.f, 0.0,-bigBox.Max.X);
	Planes[4] = FPlane( 0.f, 1.f, 0.0, bigBox.Min.Y);
	Planes[5] = FPlane( 0.f,-1.f, 0.0,-bigBox.Max.Y);

	for( INT i=0;i<6;i++ )
	{
		FLOAT Dist = Planes[i].PlaneDot(Location);
		if( Dist < 0 )
			return 1;
	}

	Result.Actor    = Projector;
	Result.Normal   = Projector->Rotation.Vector();
	Result.Location = Location;
	return 0;

}

//scion jg -- Changed to pass by ref
UBOOL UProjectorPrimitive::LineCheck
(
	FCheckResult&	Result,
	AActor*			Owner,
	AActor*			Source,
	const FVector&	End,
	const FVector&	Start,
	const FVector&	Extent,
	DWORD           ExtraNodeFlags,
	DWORD			TraceFlags
)
{
	guard(UProjectorPrimitive::LineCheck);

	if( !Owner )
		return 1;

	AProjector* Projector = Cast<AProjector>(Owner);

	FBox bigBox = Projector->Box;
	bigBox.Max += Extent;
	bigBox.Min -= Extent;

	// Move the frustum's planes out to take the extent into account
	FPlane Planes[12];
	Planes[0]  = FPlane( Projector->FrustumVertices[0]-FBoxPushOut(Projector->FrustumPlanes[0], Extent)*FVector(Projector->FrustumPlanes[0]), FVector(Projector->FrustumPlanes[0]) );
	Planes[1]  = FPlane( Projector->FrustumVertices[0]-FBoxPushOut(Projector->FrustumPlanes[1], Extent)*FVector(Projector->FrustumPlanes[1]), FVector(Projector->FrustumPlanes[1]) );
	Planes[2]  = FPlane( Projector->FrustumVertices[1]-FBoxPushOut(Projector->FrustumPlanes[2], Extent)*FVector(Projector->FrustumPlanes[2]), FVector(Projector->FrustumPlanes[2]) );
	Planes[3]  = FPlane( Projector->FrustumVertices[2]-FBoxPushOut(Projector->FrustumPlanes[3], Extent)*FVector(Projector->FrustumPlanes[3]), FVector(Projector->FrustumPlanes[3]) );
	Planes[4]  = FPlane( Projector->FrustumVertices[3]-FBoxPushOut(Projector->FrustumPlanes[4], Extent)*FVector(Projector->FrustumPlanes[4]), FVector(Projector->FrustumPlanes[4]) );
	Planes[5]  = FPlane( Projector->FrustumVertices[4]-FBoxPushOut(Projector->FrustumPlanes[5], Extent)*FVector(Projector->FrustumPlanes[5]), FVector(Projector->FrustumPlanes[5]) );
	Planes[6]  = FPlane( 0.f, 0.f, 1.0, bigBox.Min.Z);
	Planes[7]  = FPlane( 0.f, 0.f,-1.0,-bigBox.Max.Z);
	Planes[8]  = FPlane( 1.f, 0.f, 0.0, bigBox.Min.X);
	Planes[9]  = FPlane(-1.f, 0.f, 0.0,-bigBox.Max.X);
	Planes[10] = FPlane( 0.f, 1.f, 0.0, bigBox.Min.Y);
	Planes[11] = FPlane( 0.f,-1.f, 0.0,-bigBox.Max.Y);

#if 1
	FLOAT tnear, tfar, t, vn, vd ;
	INT	fnorm_num = INDEX_NONE, bnorm_num = INDEX_NONE; // front/back face # hit

	FVector delta = End - Start;
	FVector dir = delta.SafeNormal();
	FLOAT maxTime = delta.Size();

	tnear = -FLT_MAX;
    tfar = maxTime;

	// Test each plane in polyhedron
	for(INT i=0; i<12; i++)
	{
		FPlane FlipPlane = Planes[i].Flip();

		// Have to negate these, because frustum planes face inwards.
		vd = FlipPlane | dir; // Component of ray along plane normal
		//vn = FlipPlane.PlaneDot(Start); // Distance of start from plane
		vn = FlipPlane.PlaneDot(Start); // Distance of start from plane

		if ( vd == 0.0f ) 
		{
			// ray is parallel to plane - check if ray origin is inside plane's half-space
			if ( vn > 0.0f )
				return 1 ; // MISS. Ray origin is outside half-space */
		} 
		else 
		{
			// ray not parallel - get distance to plane
			t = -vn / vd ;

			if ( vd < 0.0f ) 
			{
				// front face - T is a near point
				if ( t > tfar )
					return 1; // MISS. Hit a front face beyond current furthes back hit (or end of line).

				if ( t > tnear ) 
				{
					// hit near face, update normal
					fnorm_num = i;
					tnear = t;
				}
			} 
			else 
			{
				if( t < 0.0f ) // MISS. Hit a back face, but before start of line.
					return 1;

				if ( t < tfar ) 
				{
					// hit far face, update normal
					bnorm_num = i;
					tfar = t;
				}
			}
		}
	}

	// survived all tests

	if ( tnear >= 0.0f ) 
	{
		check(fnorm_num != INDEX_NONE);

		// outside, hitting front face
		Result.Time			= tnear / maxTime;
		Result.Location		= Start + (Result.Time * delta);
		Result.Actor		= Owner;
		Result.Primitive	= NULL;
		Result.Normal		= -1 * Planes[fnorm_num];
		return 0 ;
	} 
	else
	{
		if ( tfar < maxTime ) 
		{
			// inside, hitting back face. JTODO: Right results here?
			Result.Time			= 0.f;
			Result.Location		= Start;
			Result.Actor		= Owner;
			Result.Primitive	= NULL;
			Result.Normal		= -1 * dir;
			return 0;
		} 
		else 
		{
			// inside, but back face beyond tmax
			Result.Time			= 0.f;
			Result.Location		= Start;
			Result.Actor		= Owner;
			Result.Primitive	= NULL;
			Result.Normal		= -1 * dir;
			return 0;
		}
	}

#else
	// Check if the start and end are both inside the frustum
	for( INT i=0;i<6;i++ )
		if( Planes[i].PlaneDot(Start) < 0 || Planes[i].PlaneDot(End) < 0 )
			goto NotBothInside;

	Result.Time      = 0.f;
	Result.Location  = Start;
	Result.Actor     = Owner;
	Result.Primitive = NULL;
	Result.Normal	 = (Start-End).SafeNormal();
	return 0;

	NotBothInside:;

	// Check for intersection with the frustum
	FVector Direction = End-Start;
	FVector Hit = End;
	FVector HitNormal = (Start-End).SafeNormal();
	UBOOL GotHit=0;
	for( INT i=0;i<6;i++ )
	{
		if( Abs(Direction|Planes[i]) > KINDA_SMALL_NUMBER )
		{
			FVector Int = FLinePlaneIntersection( Start, End, Planes[i] );
			for( INT j=0;j<6;j++ )
			{
				if( j != i )
				{
					if( Planes[j].PlaneDot(Int) < 0 )
						goto RejectHit;					
				}
				if( ((Int-Start)|Direction)>0.f && (!GotHit || (Int-Start).SizeSquared()<(Hit-Start).SizeSquared() ) )
				{
					GotHit = 1;
					Hit = Int;
					HitNormal = Planes[j];
				}
				RejectHit:;
			}
		}
	}
	if( !GotHit )
		return 1;

	Result.Time      = ((Hit-Start)|Direction)/Direction.SizeSquared();

	if(Result.Time > 1.0f || Result.Time < 0.0f)
		return 1;

	Result.Location  = Hit;
	Result.Actor     = Owner;
	Result.Primitive = NULL;
	return 0;
#endif
	unguard;
}

void UProjectorPrimitive::Destroy()
{
	guard(UProjectorPrimitive::Destroy);
	GProjectorPrimitive = NULL;
	Super::Destroy();
	unguard;
}

IMPLEMENT_CLASS(UProjectorPrimitive);

/*-----------------------------------------------------------------------------
	FProjectorAttachedInfo
-----------------------------------------------------------------------------*/

FProjectorRenderInfo::FProjectorRenderInfo( AProjector* InProjector, FLOAT InExpires )
{
	Projector				= InProjector;
	ReferenceCount			= 0;
	Expires					= InExpires;
	LastRenderTime			= 0;
	Material				= InProjector->ProjTexture;
	MaterialBlendingOp		= (EProjectorBlending) InProjector->MaterialBlendingOp;
	FrameBufferBlendingOp	= (EProjectorBlending) InProjector->FrameBufferBlendingOp;
	Matrix					= InProjector->Matrix;
	InverseMatrix			= Matrix.Inverse();
	BoundingBoxCenter		= InProjector->Box.GetCenter();
	BoundingBoxExtent		= InProjector->Box.GetExtent();
	appMemcpy(FrustumPlanes,InProjector->FrustumPlanes,sizeof(FrustumPlanes));
	GradientTexture			= InProjector->GradientTexture;
	GradientMatrix			= InProjector->GradientMatrix;
	ProjectorFlags			= ((InProjector->FOV==0) ? 0 : PRF_Projected) | 
							  (InProjector->bProjectOnUnlit ? PRF_ProjectOnUnlit : 0 ) |
							  (InProjector->bGradient && InProjector->GradientTexture ? PRF_Gradient : 0) |
							  (InProjector->bProjectOnAlpha ? PRF_ProjectOnAlpha : 0 ) |
							  (InProjector->bProjectOnBackfaces ? PRF_ProjectOnBackfaces : 0);

	// scion sz -
	// see if this projector has a simple bitmap material							
	ProjectedBitmap = MaterialCast<UBitmapMaterial,MRH_BitmapMaterial>( Material );
	if( !ProjectedBitmap )
	{
		UCombiner* Combiner = MaterialCast<UCombiner,MRH_Combiner>( Material );
		UModifier* Modifier = MaterialCast<UModifier,MRH_Modifier>( Material );
		while( Modifier && !ProjectedBitmap && !Combiner )
		{
			Combiner = MaterialCast<UCombiner,MRH_Combiner>( Material );
			ProjectedBitmap = MaterialCast<UBitmapMaterial,MRH_BitmapMaterial>( Modifier->Material );
			Modifier = MaterialCast<UModifier,MRH_Modifier>( Modifier->Material );
		}

		if( !ProjectedBitmap )
		{
			if( Combiner )
			{
				// try first material
				Modifier = MaterialCast<UModifier,MRH_Modifier>( Combiner->Material1 );
				ProjectedBitmap = MaterialCast<UBitmapMaterial,MRH_BitmapMaterial>( Combiner->Material1 );
				while( Modifier && !ProjectedBitmap )
				{
					ProjectedBitmap = MaterialCast<UBitmapMaterial,MRH_BitmapMaterial>( Modifier->Material );
					Modifier = MaterialCast<UModifier,MRH_Modifier>( Modifier->Material );
				}
				if( !ProjectedBitmap )
				{
					// try second material
					Modifier = MaterialCast<UModifier,MRH_Modifier>( Combiner->Material2 );
					ProjectedBitmap = MaterialCast<UBitmapMaterial,MRH_BitmapMaterial>( Combiner->Material2 );
					while( Modifier && !ProjectedBitmap )
					{
						ProjectedBitmap = MaterialCast<UBitmapMaterial,MRH_BitmapMaterial>( Modifier->Material );
						Modifier = MaterialCast<UModifier,MRH_Modifier>( Modifier->Material );
					}
				}				
			}
			if( !ProjectedBitmap ) 
			{
				// then give up
				debugf( NAME_Error, TEXT("Projector [%s] has invalid Material [%s]"), Projector->GetFullName(), Material->GetFullName() );
			}			
		}
	}	
}

/*-----------------------------------------------------------------------------
	AProjector
-----------------------------------------------------------------------------*/

UPrimitive* AProjector::GetPrimitive()
{
	guard(AProjector::GetPrimitive);
	
	// scion sz - use the static batched primitive
	// if we have one
	if( DrawType == DT_StaticProjectorBatch &&
		StaticPrims.Num() )
	{
        return StaticPrims(0);        
	}

	// scion sz - 
	return UProjectorPrimitive::GetStaticInstance();

	unguard;
}

void AProjector::UpdateParticleMaterial(class UParticleMaterial* ParticleMaterial, INT ProjectorIndex )
{
	guard(AProjector::UpdateParticleMaterial);
	ParticleMaterial->Projectors[ProjectorIndex].BitmapMaterial = Cast<UBitmapMaterial>(ProjTexture);
	ParticleMaterial->Projectors[ProjectorIndex].Matrix			= Matrix;
	ParticleMaterial->Projectors[ProjectorIndex].Projected		= (FOV!=0);
	ParticleMaterial->Projectors[ProjectorIndex].BlendMode		= FrameBufferBlendingOp;
	unguard;
}

void AProjector::execAttachProjector( FFrame& Stack, RESULT_DECL )
{
	guard(AProjector::execAttachProjector);
	P_FINISH;
	Attach();
	unguard;
}

void AProjector::execDetachProjector( FFrame& Stack, RESULT_DECL )
{
	guard(AProjector::execDetachProjector);
	P_GET_UBOOL_OPTX(bForce,0);
	P_FINISH;
	Detach(bForce);
	unguard;
}

void AProjector::execAbandonProjector( FFrame& Stack, RESULT_DECL )
{
	guard(AProjector::execAbandonProjector);
	P_GET_FLOAT_OPTX(Lifetime,0.f);
	P_FINISH;
	if( RenderInfo && Lifetime != 0.f )
    {
		RenderInfo->Expires = Level->TimeSeconds + Lifetime;
    }

//	Abandon();
	unguard;
}

void AProjector::execAttachActor( FFrame& Stack, RESULT_DECL )
{
	guard(AProjector::execAttachActor);
	P_GET_OBJECT(AActor,Actor);
	P_FINISH;
	
	if( !UTexture::__Client->Projectors )
		return;

	if( RenderInfo && Actor ) 
	{
		// Remove old projectors.

		for(INT ProjectorIndex = 0;ProjectorIndex < Actor->Projectors.Num();ProjectorIndex++)
			if(!Actor->Projectors(ProjectorIndex)->Render( Level->TimeSeconds ))
				Actor->Projectors.Remove(ProjectorIndex--);

		// Add the new projector.

		if( Actor->Projectors.FindItemIndex(RenderInfo) == INDEX_NONE )
			Actor->Projectors.AddItem(RenderInfo->AddReference());
	}
	unguard;
}

void AProjector::execDetachActor( FFrame& Stack, RESULT_DECL )
{
	guard(AProjector::execDetachActor);
	P_GET_OBJECT(AActor,Actor);
	P_FINISH;

	if( !UTexture::__Client->Projectors )
		return;

	if( RenderInfo && Actor ) 
	{
		INT i = Actor->Projectors.FindItemIndex(RenderInfo);
		if( i != INDEX_NONE )
		{
			RenderInfo->RemoveReference();
			Actor->Projectors.Remove(i);
		}
	}
	unguard;
}

/* scion ======================================================================
* AProjector::execUpdate
* Author: sz
*
* update projector matrices
* ============================================================================
*/
void AProjector::execUpdate( FFrame& Stack, RESULT_DECL )
{
	guardSlow( AProjector::execUpdate );
	P_FINISH;

    CalcMatrix();

	unguardSlow;
}

UBOOL AProjector::ShouldTrace(AActor *SourceActor, DWORD TraceFlags)
{
	if(TraceFlags & TRACE_Projectors)
		return 1;
	return Super::ShouldTrace(SourceActor, TraceFlags);
}

void AProjector::RenderWireframe(FRenderInterface* RI)
{
	RI->SetTransform(TT_LocalToWorld,FMatrix::Identity);

	FLineBatcher LineBatcher(RI);
	LineBatcher.DrawLine( FrustumVertices[0], FrustumVertices[1], FColor(0,255,255) );
	LineBatcher.DrawLine( FrustumVertices[1], FrustumVertices[2], FColor(0,255,255) );
	LineBatcher.DrawLine( FrustumVertices[2], FrustumVertices[3], FColor(0,255,255) );
	LineBatcher.DrawLine( FrustumVertices[3], FrustumVertices[0], FColor(0,255,255) );
	LineBatcher.DrawLine( FrustumVertices[4], FrustumVertices[5], FColor(0,255,255) );
	LineBatcher.DrawLine( FrustumVertices[5], FrustumVertices[6], FColor(0,255,255) );
	LineBatcher.DrawLine( FrustumVertices[6], FrustumVertices[7], FColor(0,255,255) );
	LineBatcher.DrawLine( FrustumVertices[7], FrustumVertices[4], FColor(0,255,255) );
	LineBatcher.DrawLine( FrustumVertices[0], FrustumVertices[4], FColor(0,255,255) );
	LineBatcher.DrawLine( FrustumVertices[1], FrustumVertices[5], FColor(0,255,255) );
	LineBatcher.DrawLine( FrustumVertices[2], FrustumVertices[6], FColor(0,255,255) );
	LineBatcher.DrawLine( FrustumVertices[3], FrustumVertices[7], FColor(0,255,255) );

	if( ProjTexture && 
		FOV > 1)
	{
		//scion sz - draw some lines to the origin
		FCoords DirectionCoords = GMath.UnitCoords / Rotation;
		FVector Direction = DirectionCoords.XAxis;
		FLOAT TanHalfFOV = appTan(0.5f*FOV*PI/180.f);
		FVector FrustumOrigin = Location - Direction * (0.5*DrawScale*ProjTexture->MaterialUSize() / TanHalfFOV);
		LineBatcher.DrawLine( FrustumVertices[0], FrustumOrigin, FColor(0,100,255) );
		LineBatcher.DrawLine( FrustumVertices[1], FrustumOrigin, FColor(0,100,255) );
		LineBatcher.DrawLine( FrustumVertices[2], FrustumOrigin, FColor(0,100,255) );
		LineBatcher.DrawLine( FrustumVertices[3], FrustumOrigin, FColor(0,100,255) );
	}

	LineBatcher.DrawBox( Box, FColor(255,255,0) );
}

void AProjector::CalcMatrix()
{
	guard(AProjector::CalcMatrix);
	if( !ProjTexture )
		return;
	//!! assumes decal is square

	// Setup frustum planes.
	FCoords DirectionCoords = GMath.UnitCoords / Rotation;

	FVector Direction = DirectionCoords.XAxis;
	FVector UpVector = DirectionCoords.ZAxis;
	FVector LeftVector = -DirectionCoords.YAxis;

	//!!vogel: what about DrawScale3D?
	FLOAT USize = DrawScale*ProjTexture->MaterialUSize();
	FLOAT VSize = DrawScale*ProjTexture->MaterialVSize();
	FLOAT Radius = appSqrt(0.25f*USize*USize + 0.25f*VSize*VSize);

	if( Physics == PHYS_Rotating )
	{
		FrustumVertices[0] = Location + Radius * (UpVector + LeftVector);
		FrustumVertices[1] = Location + Radius * (UpVector - LeftVector);
		FrustumVertices[2] = Location + Radius * (-UpVector - LeftVector);
		FrustumVertices[3] = Location + Radius * (-UpVector + LeftVector);
	}
	else
	{
		FrustumVertices[0] = Location + Radius * (UpVector + LeftVector).SafeNormal();
		FrustumVertices[1] = Location + Radius * (UpVector - LeftVector).SafeNormal();
		FrustumVertices[2] = Location + Radius * (-UpVector - LeftVector).SafeNormal();
		FrustumVertices[3] = Location + Radius * (-UpVector + LeftVector).SafeNormal();
	}

	FrustumPlanes[0] = FPlane( Location, Direction );
	if( FOV == 0 )
	{
		FrustumPlanes[1] = FPlane( FrustumVertices[0], -(UpVector + LeftVector).SafeNormal() );
		FrustumPlanes[2] = FPlane( FrustumVertices[1], -(UpVector - LeftVector).SafeNormal() );
		FrustumPlanes[3] = FPlane( FrustumVertices[2], -(-UpVector - LeftVector).SafeNormal() );
		FrustumPlanes[4] = FPlane( FrustumVertices[3], -(-UpVector + LeftVector).SafeNormal() );

		FCoords WorldToTex = FCoords( Location + 0.5f*LeftVector*USize + 0.5f*UpVector*VSize,
				-LeftVector/USize, -UpVector/VSize, Direction );

		Matrix = WorldToTex.Matrix();
		
		FrustumVertices[4] = FrustumVertices[0]+MaxTraceDistance*Direction;
		FrustumVertices[5] = FrustumVertices[1]+MaxTraceDistance*Direction;
		FrustumVertices[6] = FrustumVertices[2]+MaxTraceDistance*Direction;
		FrustumVertices[7] = FrustumVertices[3]+MaxTraceDistance*Direction;
	}
	else
	{
		FLOAT TanHalfFOV = appTan(0.5f*FOV*PI/180.f);
		FLOAT CosHalfFOV = appCos(0.5f*FOV*PI/180.f);
		FVector FrustumOrigin = Location - Direction * (0.5*USize / TanHalfFOV);
				
		FrustumPlanes[1] = FPlane( FrustumOrigin, FrustumVertices[1], FrustumVertices[0] );
		FrustumPlanes[2] = FPlane( FrustumOrigin, FrustumVertices[2], FrustumVertices[1] );
		FrustumPlanes[3] = FPlane( FrustumOrigin, FrustumVertices[3], FrustumVertices[2] );
		FrustumPlanes[4] = FPlane( FrustumOrigin, FrustumVertices[0], FrustumVertices[3] );
		FMatrix ProjectionMatrix, ViewMatrix;
		FCoords WorldToTex = FCoords( FrustumOrigin, -LeftVector, -UpVector, Direction );
		ViewMatrix = WorldToTex.Matrix();
		appMemzero( &ProjectionMatrix, sizeof(ProjectionMatrix));
		ProjectionMatrix.M[0][0] = 0.5f/TanHalfFOV;
		ProjectionMatrix.M[2][0] = 0.5;
		ProjectionMatrix.M[1][1] = 0.5f/TanHalfFOV;
		ProjectionMatrix.M[2][1] = 0.5;
		ProjectionMatrix.M[2][2] = 1.f;
		ProjectionMatrix.M[3][3] = 1.f;
		Matrix = ViewMatrix * ProjectionMatrix;//MatrixMultiply( Matrix, ViewMatrix, ProjectionMatrix );

		FrustumVertices[4] = FrustumVertices[0]+MaxTraceDistance*(FrustumVertices[0]-FrustumOrigin).SafeNormal() / CosHalfFOV;
		FrustumVertices[5] = FrustumVertices[1]+MaxTraceDistance*(FrustumVertices[1]-FrustumOrigin).SafeNormal() / CosHalfFOV;
		FrustumVertices[6] = FrustumVertices[2]+MaxTraceDistance*(FrustumVertices[2]-FrustumOrigin).SafeNormal() / CosHalfFOV;
		FrustumVertices[7] = FrustumVertices[3]+MaxTraceDistance*(FrustumVertices[3]-FrustumOrigin).SafeNormal() / CosHalfFOV;
	}
	FrustumPlanes[5] = FPlane( FrustumVertices[7], -Direction );

	if( bGradient && GradientTexture )
	{
		FLOAT GradientTexels = GradientTexture->VSize-2;
		FLOAT Texel = MaxTraceDistance/GradientTexture->VSize;
		FCoords GradientCoords = FCoords( Location - Direction*Texel,
			FVector(0,0,0), Direction/(MaxTraceDistance*GradientTexture->VSize/GradientTexels), FVector(0,0,0) );
		GradientMatrix = GradientCoords.Matrix();
	}

	Box.Init();
	for( INT i=0;i<8;i++ )
		Box += FrustumVertices[i];

	if( RenderInfo )
	{
		RenderInfo->Matrix = Matrix;
		RenderInfo->InverseMatrix = Matrix.Inverse();
	}

	unguard;
}

//
//	FTriangleClipper
//

class FTriangleClipper
{
public:

	enum EProjectorClipCode
	{
		PCC_MinX = 0x01,
		PCC_MaxX = 0x02,
		PCC_MinY = 0x04,
		PCC_MaxY = 0x08,
		PCC_MinZ = 0x10,
		PCC_MaxZ = 0x20
	};

	struct FUnclippedVertex
	{
		FVector	WorldPosition,
				ProjectedPosition;
		// scion sz - need normals too
		// for GPU attenuation and clipping
		FVector Normal;

		FLOAT	Attenuation;
		BYTE	ClipCode;
	};

	struct FClippedVertex : public FUnclippedVertex
	{
		INT		SourceVertices[2];
		BYTE	ClipAxis;
	};

	AActor*						Actor;
	TArray<FUnclippedVertex>	UnclippedVertices;
	TArray<FClippedVertex>		ClippedVertices;
	TArray<INT>					VertexMap;

	FProjectorRenderInfo*		RenderInfo;
	FLOAT						MinZ,
								MaxZ;

	// GetVertex

	FUnclippedVertex& GetVertex(INT VertexIndex)
	{
		if(VertexIndex < 0)
			return UnclippedVertices(-VertexIndex - 1);
		else
			return ClippedVertices(VertexIndex);
	}

	// CalculateClipCode

	void CalculateClipCode(FUnclippedVertex& Vertex)
	{
		Vertex.ClipCode = 0;

		if(Vertex.ProjectedPosition.X < -0.001f)
			Vertex.ClipCode |= PCC_MinX;

		if(Vertex.ProjectedPosition.X > Vertex.ProjectedPosition.Z + 0.001f)
			Vertex.ClipCode |= PCC_MaxX;

		if(Vertex.ProjectedPosition.Y < -0.001f)
			Vertex.ClipCode |= PCC_MinY;

		if(Vertex.ProjectedPosition.Y > Vertex.ProjectedPosition.Z + 0.001f)
			Vertex.ClipCode |= PCC_MaxY;

		if(Vertex.ProjectedPosition.Z < MinZ - 0.001f)
			Vertex.ClipCode |= PCC_MinZ;

		if(Vertex.ProjectedPosition.Z > MaxZ + 0.001f)
			Vertex.ClipCode |= PCC_MaxZ;
	}

	// AddClippedVertex

	INT AddClippedVertex(INT V1,INT V2,BYTE ClipAxis)
	{
		// Create a new clipped vertex.

		INT				ClippedVertexIndex = ClippedVertices.Num();
		FClippedVertex*	ClippedVertex = new(ClippedVertices) FClippedVertex;

		ClippedVertex->SourceVertices[0] = V1;
		ClippedVertex->SourceVertices[1] = V2;
		ClippedVertex->ClipAxis = ClipAxis;

		// Find the source vertices.

		FUnclippedVertex&	SV1 = GetVertex(V1);
		FUnclippedVertex&	SV2 = GetVertex(V2);

		// Determine the location of the intersection with the edge.

		FVector	WorldSlope = SV2.WorldPosition - SV1.WorldPosition,
				ProjectedSlope = SV2.ProjectedPosition - SV1.ProjectedPosition;
		FLOAT	AttenuationSlope = SV2.Attenuation - SV1.Attenuation;
		FPlane	ClipPlane;

		switch(ClipAxis)
		{
		case PCC_MinX:
			ClipPlane = FPlane(FVector(-1,0,0).SafeNormal(),0);
			break;
		case PCC_MaxX:
			ClipPlane = FPlane(FVector(1,0,-1).SafeNormal(),0);
			break;
		case PCC_MinY:
			ClipPlane = FPlane(FVector(0,-1,0).SafeNormal(),0);
			break;
		case PCC_MaxY:
			ClipPlane = FPlane(FVector(0,1,-1).SafeNormal(),0);
			break;
		case PCC_MinZ:
			ClipPlane = FPlane(0,0,-1,-MinZ);
			break;
		case PCC_MaxZ:
			ClipPlane = FPlane(0,0,1,MaxZ);
			break;
		default:
			ClipPlane = FPlane(0,0,0,0);
			break;
		};

		FLOAT	StartDist = ClipPlane.PlaneDot(SV1.ProjectedPosition),
				EndDist = ClipPlane.PlaneDot(SV2.ProjectedPosition),
				Time = -StartDist / (EndDist - StartDist);

		// Calculate the clipped vertex.

		ClippedVertex->WorldPosition = SV1.WorldPosition + Time * WorldSlope;
		ClippedVertex->ProjectedPosition = SV1.ProjectedPosition + Time * ProjectedSlope;
		ClippedVertex->Attenuation = SV1.Attenuation + Time * AttenuationSlope;
		// scion sz - interpolated normal
		ClippedVertex->Normal = Lerp<FVector>( SV1.Normal, SV2.Normal, Time );
		ClippedVertex->Normal.Normalize();

		CalculateClipCode(*ClippedVertex);

		return ClippedVertexIndex;
	}

	// ClipTriangle

	void ClipTriangle(TArray<_WORD>& Indices,INT V1,INT V2,INT V3)
	{
		INT					VertexIndices[2][16],
							NumVertices[2],
							DestVertexBuffer = 0;
		FUnclippedVertex*	SrcVertices[3] = { &GetVertex(V1), &GetVertex(V2), &GetVertex(V3) };

		if(SrcVertices[0]->ClipCode & SrcVertices[1]->ClipCode & SrcVertices[2]->ClipCode)
			return;

		VertexIndices[0][0] = V1;
		VertexIndices[0][1] = V2;
		VertexIndices[0][2] = V3;
		NumVertices[0] = 3;

		EProjectorClipCode	ClipCodes[6] = { PCC_MinX, PCC_MaxX, PCC_MinY, PCC_MaxY, PCC_MinZ, PCC_MaxZ };
		for(INT PlaneIndex = 0;PlaneIndex < 6;PlaneIndex++)
		{
			INT	PrevVertexIndex = VertexIndices[DestVertexBuffer][NumVertices[DestVertexBuffer] - 1];

			DestVertexBuffer = 1 - DestVertexBuffer;
			NumVertices[DestVertexBuffer] = 0;

			for(INT VertexIndex = 0;VertexIndex < NumVertices[1 - DestVertexBuffer];VertexIndex++)
			{
				FUnclippedVertex&	Vertex = GetVertex(VertexIndices[1 - DestVertexBuffer][VertexIndex]);
				FUnclippedVertex&	PrevVertex = GetVertex(PrevVertexIndex);
				if(Vertex.ClipCode & ClipCodes[PlaneIndex])
				{
					if(!(PrevVertex.ClipCode & ClipCodes[PlaneIndex]))
					{
						// Previous vertex in, current vertex out.

						VertexIndices[DestVertexBuffer][NumVertices[DestVertexBuffer]++] = AddClippedVertex(
							PrevVertexIndex,
							VertexIndices[1 - DestVertexBuffer][VertexIndex],
							ClipCodes[PlaneIndex]
							);
					}
				}
				else
				{
					if(PrevVertex.ClipCode & ClipCodes[PlaneIndex])
					{
						// Previous vertex out, current vertex in.

						VertexIndices[DestVertexBuffer][NumVertices[DestVertexBuffer]++] = AddClippedVertex(
							PrevVertexIndex,
							VertexIndices[1 - DestVertexBuffer][VertexIndex],
							ClipCodes[PlaneIndex]
							);

						VertexIndices[DestVertexBuffer][NumVertices[DestVertexBuffer]++] = VertexIndices[1 - DestVertexBuffer][VertexIndex];
					}
					else
					{
						// Both vertices inside.
						VertexIndices[DestVertexBuffer][NumVertices[DestVertexBuffer]++] = VertexIndices[1 - DestVertexBuffer][VertexIndex];
					}
				}
				PrevVertexIndex = VertexIndices[1 - DestVertexBuffer][VertexIndex];
			}

			if(NumVertices[DestVertexBuffer] == 0)
				break;
		}

		if(NumVertices[DestVertexBuffer])
		{
			INT	FirstVertex = VertexMap.AddUniqueItem(VertexIndices[DestVertexBuffer][0]),
				PrevVertex = VertexMap.AddUniqueItem(VertexIndices[DestVertexBuffer][1]);
			for(INT VertexIndex = 2;VertexIndex < NumVertices[DestVertexBuffer];VertexIndex++)
			{
				Indices.AddItem(FirstVertex);
				Indices.AddItem(PrevVertex);
				PrevVertex = VertexMap.AddUniqueItem(VertexIndices[DestVertexBuffer][VertexIndex]);
				Indices.AddItem(PrevVertex);
			}
		}
	}

	//
	//	Constructor
	//

	FTriangleClipper(AActor* InActor,FProjectorRenderInfo* InRenderInfo)
	{
		guard(FTriangleClipper::FTriangleClipper);

		Actor = InActor;
		RenderInfo = InRenderInfo;
		check(RenderInfo->Projector);
		check(RenderInfo->Material);

		//jg -- Handle compression of static meshes
		// Set the compression scale & offset on the compressor class
		FPackedPosition::SetCompressionVectors(&Actor->StaticMesh->CompressionScale,
			&Actor->StaticMesh->CompressionInvScale,&Actor->StaticMesh->CompressionOffset);

		FMatrix&	LocalToWorld = Actor->GetActorRenderData()->LocalToWorld;
		FMatrix		WorldToLocal = LocalToWorld.Inverse();
		FMatrix		L2WInvTranspose = WorldToLocal.Transpose();
		FVector		WorldDirection = RenderInfo->Projector->Rotation.Vector(),
					LocalDirection = WorldToLocal.TransformNormal(-WorldDirection).SafeNormal();
		FLOAT		Determinant = Actor->GetActorRenderData()->Determinant,
					InvMaxTraceDistance = 1.0f / RenderInfo->Projector->MaxTraceDistance;

		MinZ = 0.5*RenderInfo->Projector->DrawScale*RenderInfo->Material->MaterialUSize() / appTan(0.5f*RenderInfo->Projector->FOV*PI/180.f);
		MaxZ = MinZ + RenderInfo->Projector->MaxTraceDistance;

		UnclippedVertices.Add(Actor->StaticMesh->VertexStream.Vertices.Num());

		for(INT VertexIndex = 0;VertexIndex < Actor->StaticMesh->VertexStream.Vertices.Num();VertexIndex++)
		{
			FStaticMeshVertex*	SrcVertex = &Actor->StaticMesh->VertexStream.Vertices(VertexIndex);
			FUnclippedVertex*	DestVertex = &UnclippedVertices(VertexIndex);

			// Transform the vertex into world space.
			DestVertex->WorldPosition = LocalToWorld.TransformFVector(SrcVertex->Position);

			// scion sz - copy normals (in world space)
			DestVertex->Normal = L2WInvTranspose.TransformNormal( SrcVertex->Normal );

			// Calculate the vertex's clip code.
			DestVertex->ProjectedPosition = RenderInfo->Matrix.TransformFVector(DestVertex->WorldPosition);
			CalculateClipCode(*DestVertex);

			// Calculate the vertex's attenuation.
			FLOAT	DirectionalAttenuation = 1.0f;

			if(!(RenderInfo->ProjectorFlags & PRF_ProjectOnBackfaces))
				DirectionalAttenuation = Max(SrcVertex->Normal | LocalDirection,0.0f);

			FLOAT	DistanceAttenuation = 1.0f;

			if(RenderInfo->ProjectorFlags & PRF_Gradient)
				DistanceAttenuation = Clamp(1.0f - ((DestVertex->WorldPosition - RenderInfo->Projector->Location) | WorldDirection) * InvMaxTraceDistance,0.0f,1.0f);

			DestVertex->Attenuation = DirectionalAttenuation * DistanceAttenuation;
		}

		for(INT SectionIndex = 0;SectionIndex < Actor->StaticMesh->Sections.Num();SectionIndex++)
		{
			FStaticMeshSection*		Section = &Actor->StaticMesh->Sections(SectionIndex);
			FStaticProjectorInfo*	ProjectorInfo = NEW FStaticProjectorInfo;

			for(INT TriangleIndex = 0;TriangleIndex < Section->NumPrimitives;TriangleIndex++)
			{
				_WORD	Indices[3];

				// scion sz - support for tri stripped indices
				if( Section->IsStrip )
				{
					if(TriangleIndex & 1)
					{
						Indices[0] = Actor->StaticMesh->IndexBuffer.Indices(Section->FirstIndex + TriangleIndex + 1);
						Indices[1] = Actor->StaticMesh->IndexBuffer.Indices(Section->FirstIndex + TriangleIndex + 0);
						Indices[2] = Actor->StaticMesh->IndexBuffer.Indices(Section->FirstIndex + TriangleIndex + 2);
					}
					else
					{
						Indices[0] = Actor->StaticMesh->IndexBuffer.Indices(Section->FirstIndex + TriangleIndex + 0);
						Indices[1] = Actor->StaticMesh->IndexBuffer.Indices(Section->FirstIndex + TriangleIndex + 1);
						Indices[2] = Actor->StaticMesh->IndexBuffer.Indices(Section->FirstIndex + TriangleIndex + 2);
					}

					// skip degenerates
					if(Indices[0] == Indices[1] || Indices[1] == Indices[2] || Indices[0] == Indices[2])
						continue;
				}
				else
				{
					Indices[0] = Actor->StaticMesh->IndexBuffer.Indices(Section->FirstIndex + TriangleIndex * 3 + 0);
					Indices[1] = Actor->StaticMesh->IndexBuffer.Indices(Section->FirstIndex + TriangleIndex * 3 + 1);
					Indices[1] = Actor->StaticMesh->IndexBuffer.Indices(Section->FirstIndex + TriangleIndex * 3 + 2);
				};

				if(UnclippedVertices(Indices[0]).ClipCode & UnclippedVertices(Indices[1]).ClipCode & UnclippedVertices(Indices[2]).ClipCode)
					continue;

				if(UnclippedVertices(Indices[0]).Attenuation + UnclippedVertices(Indices[1]).Attenuation + UnclippedVertices(Indices[2]).Attenuation == 0.0f)
					continue;

				if(RenderInfo->Projector->bClipStaticMesh && (UnclippedVertices(Indices[0]).ClipCode | UnclippedVertices(Indices[1]).ClipCode | UnclippedVertices(Indices[2]).ClipCode))	
				{
					if(Determinant < 0.0f)
						ClipTriangle(ProjectorInfo->Indices,-Indices[2] - 1,-Indices[1] - 1,-Indices[0] - 1);
					else
						ClipTriangle(ProjectorInfo->Indices,-Indices[0] - 1,-Indices[1] - 1,-Indices[2] - 1);
				}
				else
				{
					if(Determinant < 0.0f)
					{
						ProjectorInfo->Indices.AddItem(VertexMap.AddUniqueItem(-Indices[2] - 1));
						ProjectorInfo->Indices.AddItem(VertexMap.AddUniqueItem(-Indices[1] - 1));
						ProjectorInfo->Indices.AddItem(VertexMap.AddUniqueItem(-Indices[0] - 1));
					}
					else
					{
						ProjectorInfo->Indices.AddItem(VertexMap.AddUniqueItem(-Indices[0] - 1));
						ProjectorInfo->Indices.AddItem(VertexMap.AddUniqueItem(-Indices[1] - 1));
						ProjectorInfo->Indices.AddItem(VertexMap.AddUniqueItem(-Indices[2] - 1));
					}
				}
			}

			if(!ProjectorInfo->Indices.Num())
			{
				ProjectorInfo = ProjectorInfo->RemoveRef();
				continue;
			}

			ProjectorInfo->Vertices.Add(VertexMap.Num());

			for(INT VertexIndex = 0;VertexIndex < VertexMap.Num();VertexIndex++)
			{
				FUnclippedVertex&	SourceVertex = GetVertex(VertexMap(VertexIndex));

				ProjectorInfo->Vertices(VertexIndex).WorldPosition = SourceVertex.WorldPosition;
				// scion sz - copy normals
				ProjectorInfo->Vertices(VertexIndex).Normal = SourceVertex.Normal;
#if DYNAMIC_PROJ_STREAM
				ProjectorInfo->Vertices(VertexIndex).Attenuation = SourceVertex.Attenuation;
				//scion jg -- Copy the projected position so we don't recalc
				ProjectorInfo->Vertices(VertexIndex).ProjectedPosition = SourceVertex.ProjectedPosition;
#endif
			}

			// Determine whether base material will be used in rendering.

			ProjectorInfo->BaseMaterial = NULL;
			ProjectorInfo->TwoSided = 0;

			if(RenderInfo->MaterialBlendingOp != PB_None)
				ProjectorInfo->BaseMaterial = Actor->StaticMesh->GetSkin(Actor,SectionIndex);
			else
			{
				UMaterial*		BaseMaterial = Actor->StaticMesh->GetSkin(Actor,SectionIndex);
				UShader*		BaseShader = Cast<UShader>(BaseMaterial);
				UTexture*		BaseTexture = Cast<UTexture>(BaseMaterial);	
				UFinalBlend*	BaseFinalBlend = Cast<UFinalBlend>(BaseMaterial);

				if(BaseTexture && (BaseTexture->bMasked || BaseTexture->bAlphaTexture))
					ProjectorInfo->BaseMaterial = BaseMaterial;
				else if(BaseShader)
				{
					if(BaseShader->Opacity)
						ProjectorInfo->BaseMaterial = BaseMaterial;

					ProjectorInfo->TwoSided = BaseShader->TwoSided;
				}
				else if(BaseFinalBlend && BaseFinalBlend->FrameBufferBlending == FB_AlphaBlend)
				{
					ProjectorInfo->BaseMaterial = BaseMaterial;
					ProjectorInfo->TwoSided = BaseFinalBlend->TwoSided;
				}
			}

			if(ProjectorInfo->BaseMaterial)
			{
				ProjectorInfo->BaseUVs.Add(VertexMap.Num());

				for(INT VertexIndex = 0;VertexIndex < VertexMap.Num();VertexIndex++)
				{
					if(Actor->StaticMesh->UVStreams.Num() && VertexMap(VertexIndex) < 0)
					{
						FStaticMeshUV*	SrcUV = &Actor->StaticMesh->UVStreams(0).UVs(-VertexMap(VertexIndex) - 1);

						ProjectorInfo->BaseUVs(VertexIndex).U = SrcUV->U;
						ProjectorInfo->BaseUVs(VertexIndex).V = SrcUV->V;
					}
					else
					{
						ProjectorInfo->BaseUVs(VertexIndex).U = 0.0f;
						ProjectorInfo->BaseUVs(VertexIndex).V = 0.0f;
					}
				}
			}

			ProjectorInfo->RenderInfo = RenderInfo->AddReference();
			Actor->StaticMeshProjectors.AddItem(ProjectorInfo);
			VertexMap.Empty();
		}

		//jg -- Wipe the compression scale & offset on the compressor class
		// so that we can find errors faster
		FPackedPosition::ClearCompressionVectors();
		unguard;
	}
};

/* scion ======================================================================
* AProjector::Attach
* Author: Epic
*
* ============================================================================
*/
void AProjector::Attach()
{
	guard(AProjector::Attach);
	
	FUNC_CLOCK(AttachCyc,GStats.DWORDStats(GEngineStats.STATS_Projector_AttachCycles));

	if( !ProjTexture  ||  Level->NetMode == NM_DedicatedServer)
	{
		return;
	}

	CalcMatrix();

	if( GIsEditor )
	{
		OldLocation = Location;
		SetZone( 0, 1 );
	}

	if( !RenderInfo )
	{
		RenderInfo = NEW FProjectorRenderInfo(this);
	}
	RenderInfo->AddReference();	

	//jg -- If this is set it came from serialization so don't reclip
	if (StaticPrims.Num() > 0)
	{
		return;
	}

	if(bDynamicAttach)
	{
		XLevel->DynamicProjectors.AddItem(RenderInfo->AddReference());
	}
	else
	{
		// Check terrain
		if( bProjectTerrain )
		{
			guard(FindSectors);
			for( INT i=0;i<Region.Zone->Terrains.Num();i++ )
			{
				ATerrainInfo*	T = Region.Zone->Terrains(i);
				//scion jg -- Don't attach if the LDs asked it not to
				if (T->bAcceptsProjectors == 0)
				{
					continue;
				}

				FBox			LocalBoundingBox = Box.TransformBy(T->ToHeightmap);
				INT				MinX = appFloor(LocalBoundingBox.Min.X),
								MaxX = appFloor(LocalBoundingBox.Max.X),
								MinY = appFloor(LocalBoundingBox.Min.Y),
								MaxY = appFloor(LocalBoundingBox.Max.Y),
								MinSectorX = MinX / T->TerrainSectorSize,
								MaxSectorX = MaxX / T->TerrainSectorSize,
								MinSectorY = MinY / T->TerrainSectorSize,
								MaxSectorY = MaxY / T->TerrainSectorSize;

				for(INT Y = MinSectorY;Y <= MaxSectorY;Y++)
				{
					if(Y >= 0 && Y < T->SectorsY)
					{
						for(INT X = MinSectorX;X <= MaxSectorX;X++)
						{
							if(X >= 0 && X < T->SectorsX)
							{
								UTerrainSector*	S = T->Sectors(X + Y * T->SectorsX);

								if(S->BoundingBox.Intersect(Box))
									S->AttachProjector(
										this,
										RenderInfo,
										Clamp(MinX - X * T->TerrainSectorSize,0,S->QuadsX - 1),
										Clamp(MinY - Y * T->TerrainSectorSize,0,S->QuadsY - 1),
										Clamp(MaxX - X * T->TerrainSectorSize,0,S->QuadsX - 1),
										Clamp(MaxY - Y * T->TerrainSectorSize,0,S->QuadsY - 1)
										);
							}
						}
					}
				}
			}
			unguard;
		}

		// Check BSP.
		if( bProjectBSP )
		{
			TArray<INT> Nodes;
			Level->XLevel->Model->ConvexVolumeMultiCheck( Box, FrustumPlanes, 6, Rotation.Vector(), Nodes );
			// scion sz - reuse static projector batch
			FStaticProjectorInfo* StaticProjInfo=NULL;
			for( INT i=0;i<Nodes.Num();i++ ) 
			{
				StaticProjInfo = Level->XLevel->Model->AttachProjector( 
					Nodes(i), 
					RenderInfo, 
					bClipBSP ? FrustumPlanes : NULL, 
					StaticProjInfo	);
			}
		}
		
		// Check actors.
		if( bProjectStaticMesh || bProjectActor )
		{
			guard(FindActors)
			if(Level->XLevel->Hash)
			{
				FMemMark Mark(GMem);
				FCheckResult* Hit = Level->XLevel->Hash->ActorEncroachmentCheck( GMem, 
					this, Location, Rotation, TRACE_AcceptProjectors, 0 );
				for( ;Hit; Hit=Hit->GetNext() )
				{
					if( Hit->Actor->bAcceptsProjectors && ( ProjectTag==NAME_None || Hit->Actor->Tag==ProjectTag ) )
					{
						if( bProjectStaticMesh && Hit->Actor->StaticMesh )
						{

							// scion sz - clip against static meshes
							// the clipped vertices for all geometry 
							// are batched together later
							if( Hit->Actor->bStatic &&
								Hit->Actor->StaticMesh->IndexBuffer.Indices.Num() )
							{
								// scion sz
								// Fix for memory leak since projector info added by FTriangleClipper only gets cleaned up if the actor it
								// is being attached to actually renders.
								// Clean up all exsisting projector infos for this actor
								for( INT ProjectorIndex=0; ProjectorIndex < Hit->Actor->StaticMeshProjectors.Num(); ProjectorIndex++ )
								{
									FStaticProjectorInfo*	ProjectorInfo = Hit->Actor->StaticMeshProjectors(ProjectorIndex);
									if(!ProjectorInfo->RenderInfo->Render( Hit->Actor->Level->TimeSeconds ))
									{
										Hit->Actor->StaticMeshProjectors(ProjectorIndex) = ProjectorInfo->RemoveRef();
										Hit->Actor->StaticMeshProjectors.Remove(ProjectorIndex--);
										continue;
									}
								}

								FTriangleClipper(Hit->Actor,RenderInfo);
							}
							else
							{
								Hit->Actor->AttachProjector(this);
							}
						}
						else if( bProjectActor && Hit->Actor->StaticMesh==NULL )							
						{
							Hit->Actor->AttachProjector(this);
						}
					}
				}
				Mark.Pop();
			}
			unguard;
		}

		// scion sz - batch the geometry generated by this bLevelStatic projector
		// (this should be done during the asset conversion stage)
		//RebuildStaticPrimsBatch();
	}
	unguard;
}

void AProjector::Detach( UBOOL Force )
{
	guard(AProjector::Detach);
	if( RenderInfo )
	{
		GProjectorRenderInfoGeneration++;
		RenderInfo->Projector = NULL;
		RenderInfo->Expires = Level->TimeSeconds ? Level->TimeSeconds : -1;
		if( Force )
			RenderInfo->LastRenderTime = 0;
		RenderInfo->RemoveReference();
		RenderInfo = NULL;
	}
	// scion sz - remove the batched static geometry
    StaticPrims.Empty();
	unguard;
}

void AProjector::Abandon()
{
	guard(AProjector::Abandon);
	if( RenderInfo )
	{
		GProjectorRenderInfoGeneration++;
        RenderInfo->Projector = NULL; 
		RenderInfo->RemoveReference();
		RenderInfo = NULL;
	}
	unguard;
}

/* scion ======================================================================
* FMultiMaterialProjectorInfo::FMultiMaterialProjectorInfo
* Author: sz
*
* Resets each UProjectorMultiMaterial entry
* ============================================================================
*/
FMultiMaterialProjectorInfo::FMultiMaterialProjectorInfo( UMaterial* BaseMaterial, UBOOL bEnableFrustumClip )
{
	guardSlow(FMultiMaterialProjectorInfo::FMultiMaterialProjectorInfo);

	for(INT Blend=0; Blend<PB_MAX; Blend++ ) {
		UProjectorMultiMaterial* ProjMultMat = GetMaterial((EProjectorBlending)Blend);
		ProjMultMat->bFrustumClipPixels = bEnableFrustumClip;
		ProjMultMat->BaseMaterial = BaseMaterial;
		ProjMultMat->RenderEntries.Reset();
	}

	unguardSlow;
}

/* scion ======================================================================
* FMultiMaterialProjectorInfo::GetMaterial
* Author: sz
*
* Generates a static array of UProjectorMultiMaterial objects.
* ============================================================================
*/
UProjectorMultiMaterial* FMultiMaterialProjectorInfo::GetMaterial( EProjectorBlending Type )
{
	guardSlow(FMultiMaterialProjectorInfo::GetMaterial);

	DECLARE_STATIC_UOBJECT_ARRAY( UProjectorMultiMaterial, Materials, PB_MAX, 
	{
		for(INT Blend=0; Blend<PB_MAX; Blend++ ) {
			Materials[Blend]->FrameBufferBlending = (EProjectorBlending)Blend;
		}
	});
	
	return Materials[Type];

	unguardSlow;
}

/* scion ======================================================================
* FMultiMaterialProjectorInfo::AddProjectorEntry
* Author: sz
*
* Add the projector info to the batched list of entries with the same
* blending mode.
* ============================================================================
*/
void FMultiMaterialProjectorInfo::AddProjectorEntry( FProjectorRenderInfo* ProjRendInfo )
{
	guardSlow(FProjectorRenderInfo::UpdateMultiMaterial);
	checkSlow( ProjRendInfo );

	UProjectorMultiMaterial* ProjMultMat = GetMaterial( (EProjectorBlending)ProjRendInfo->FrameBufferBlendingOp );
	checkSlow( ProjMultMat );

	ProjMultMat->AddProjectorEntry( ProjRendInfo );

	unguardSlow;
}

/* scion ======================================================================
* UStaticProjectorClippedPrim::StaticConstructor
* Author: sz
*
* ============================================================================
*/
void UStaticProjectorClippedPrim::StaticConstructor()
{
	guard(UStaticProjectorClippedPrim::StaticConstructor);
	unguard;
}

/* scion ======================================================================
* UStaticProjectorClippedPrim::Serialize
* Author: sz
*
* ============================================================================
*/
void UStaticProjectorClippedPrim::Serialize( FArchive& Ar )
{
	guard(UStaticProjectorClippedPrim::Serialize);

	Super::Serialize(Ar);

	Ar << VertexStream << UVStream << IndexBuffer << BaseMaterial;

#ifdef _XBOX
		// scion sz - count push buffer bytes
		if( !Ar.IsSaving() && !Ar.IsLoading() )
		{
			if( pPushBuffer )
				pPushBuffer->Serialize( Ar );
		}
#endif

	unguard;
}

/* scion ======================================================================
* UStaticProjectorClippedPrim::PostLoad
* Author: sz
*
* creates a static push buffer and removes indices
* ============================================================================
*/
void UStaticProjectorClippedPrim::PostLoad()
{
	guard(UStaticProjectorClippedPrim::PostLoad);

	Super::PostLoad();
#ifdef _XBOX
#ifdef XBOX_ASSET_FORMAT
	// Don't fix up data that was serialized the old fashioned way
	if (VertexStream.Vertices.IsInXPR())
	{
		INT Count;
		void* Data;
		// Find the vert data
		GetTUmaArrayFromXPR(*FString::Printf(PROJECTOR_VS_PF,GetName()),Count,Data);
		// Now fix up the array with that
		VertexStream.Vertices.FixUpArray(Count,Data);
	}
	// Try to fix them up
	pPushBuffer = GetPushbufferFromXPR(*FString::Printf(PROJECTOR_PB_PF,GetName()));
	// Try to compile it if not found
	if (pPushBuffer == NULL)
	{	
		pPushBuffer = appCompileIndicesToPushBuffer(
			PT_TriangleList,IndexBuffer.Indices.Num(),
			&IndexBuffer.Indices(0),0);
	}
	// index buffer won't be needed since we compiled them to 
	// a static push buffer
	if( pPushBuffer )
	{
		IndexBuffer.Indices.Empty();
	}
#else
	// create a compiled push buffer for indices of this prim
	if( IndexBuffer.Indices.Num() > 2 )
	{	
		pPushBuffer = appCompileIndicesToPushBuffer(
			PT_TriangleList,
			IndexBuffer.Indices.Num(),
			&IndexBuffer.Indices(0),
			0 
			);
	}

	// index buffer won't be needed since we compiled them to 
	// a static push buffer
	if( pPushBuffer )
	{
		IndexBuffer.Indices.Empty();
	}
#endif
#endif

	unguard;
}

/* scion ======================================================================
* UStaticProjectorClippedPrim::Destroy
* Author: sz
*
* ============================================================================
*/
void UStaticProjectorClippedPrim::Destroy()
{
	guard(UStaticProjectorClippedPrim::Destroy);

	// free push buffer
	if (pPushBuffer)
	{
		delete pPushBuffer;
		pPushBuffer = NULL;
	}

	UObject::Destroy();

	unguard;
}

/* scion ======================================================================
* AProjector::RebuildStaticPrimsBatch
* Author: sz
*
* After a static projector has been attached to all level surfaces we can
* create a single batch for all of its clipped geometry.  This function
* iterates over all bsp nodes, static meshes, and terrain sectors and collects
* the clipped geometry for this projector.  Once the geometry is added, it
* is removed from its source object.
*
* Assumes that this projector has already been attached to level geometry and
* that it won't be detached and re-attached. Everything will still work, it will
* just be a slow operation since everything has to be recached.
* ============================================================================
*/
void AProjector::RebuildStaticPrimsBatch()
{
	guard(AProjector::RebuildStaticPrimsBatch);

	// only for static projectors
#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
	if( bDynamicAttach) {
#else
	if( bDynamicAttach || GIsEditor ) {
#endif
		return;
	}

	// remove any existing batches for this projector (destructors called)
	StaticPrims.Empty();

	// mark as not hidden so that we can render this as an actor
	bHidden = 0;
	// set render mode for this actor
	DrawType = DT_StaticProjectorBatch;
	// refilter if needed
	StaticFilterState = FS_Maybe;

	// keep track of which ones we have used
	TArray<FStaticProjectorInfo*> UsedInfos;

	// BSP static projectors
	if( bProjectBSP )
	for( TObjectIterator<UModel> It; It; ++It )
	{
		UModel* Model = *It;
		for( INT NodeIdx=0; NodeIdx < Model->Nodes.Num(); NodeIdx++ )
		{
            FBspNode& Node = Model->Nodes(NodeIdx);
			for( INT ProjIdx=0; ProjIdx < Node.Projectors.Num(); ProjIdx++ )
			{
				// each bsp node has a list of static projectors
				FStaticProjectorInfo* StaticInfo = Node.Projectors(ProjIdx);
				
				// make sure this info is one of ours
				if( !StaticInfo || StaticInfo->RenderInfo != RenderInfo )
					continue;

                // see if we already used this info
				INT FoundIdx = UsedInfos.FindItemIndex( StaticInfo );
				if( FoundIdx == INDEX_NONE )
				{
					// add its primitives to the batch
					AddStaticPrimToBatch( StaticInfo );					
                    // add it to used list			
					UsedInfos.AddItem( StaticInfo );
				}
				// remove projector entry from node
				Node.Projectors.Remove( ProjIdx-- );
				// decrement its ref count
				if( StaticInfo->RemoveRef() == NULL && 
					FoundIdx != INDEX_NONE )
				{
					// remove from used list since we know that
					// it is not referenced anywhere else
					UsedInfos.Remove( FoundIdx );
				}
			}
		}
	}

	// Terrain static projectors
	if( bProjectTerrain )
	for( TObjectIterator<ATerrainInfo> It; It; ++It )
	{
		ATerrainInfo* TerrainInfo = *It;
		for( INT SecIdx=0; SecIdx < TerrainInfo->Sectors.Num(); SecIdx++ )
		{
			UTerrainSector* Sector = TerrainInfo->Sectors( SecIdx );

			if( !Sector )
				continue;

			for( INT ProjIdx=0; ProjIdx < Sector->Projectors.Num(); ProjIdx++ )
			{
				// each terrain sector has a list of static projectors
				FStaticProjectorInfo* StaticInfo = Sector->Projectors( ProjIdx );

				// make sure this info is one of ours
				if( !StaticInfo || StaticInfo->RenderInfo != RenderInfo )
					continue;

				// see if we already used this info
				INT FoundIdx = UsedInfos.FindItemIndex( StaticInfo );
				if( FoundIdx == INDEX_NONE )
				{
					// add its primitives to the batch
					AddStaticPrimToBatch( StaticInfo );					
                    // add it to used list					
					UsedInfos.AddItem( StaticInfo );
				}
				// remove projector entry from node
				Sector->Projectors.Remove( ProjIdx-- );
				// decrement its ref count
				if( StaticInfo->RemoveRef() == NULL && 
					FoundIdx != INDEX_NONE )
				{
					// remove from used list since we know that
					// it is not referenced anywhere else
					UsedInfos.Remove( FoundIdx );
				}
			}				
		}
	}

	// Static mesh static projectors
	if( bProjectStaticMesh )
	for( TObjectIterator<AStaticMeshActor> It; It; ++It )
	{
		AStaticMeshActor* StaticMeshActor = *It;
		for( INT ProjIdx=0; ProjIdx < StaticMeshActor->StaticMeshProjectors.Num(); ProjIdx++ )
		{
			FStaticProjectorInfo* StaticInfo = StaticMeshActor->StaticMeshProjectors( ProjIdx );

			// make sure this info is one of ours
			if( !StaticInfo || StaticInfo->RenderInfo != RenderInfo )
				continue;

			// see if we already used this info
			INT FoundIdx = UsedInfos.FindItemIndex( StaticInfo );
			if( FoundIdx == INDEX_NONE )
			{
				// add its primitives to the batch
				AddStaticPrimToBatch( StaticInfo );					
				// add it to used list					
				UsedInfos.AddItem( StaticInfo );
			}
			// remove projector entry from node
			StaticMeshActor->StaticMeshProjectors.Remove( ProjIdx-- );
			// decrement its ref count
			if( StaticInfo->RemoveRef() == NULL && 
				FoundIdx != INDEX_NONE )
			{
				// remove from used list since we know that
				// it is not referenced anywhere else
				UsedInfos.Remove( FoundIdx );
			}
		}
	}

	for( INT Idx=0; Idx < StaticPrims.Num(); Idx++ )
	{
		UStaticProjectorClippedPrim* CurPrim = StaticPrims(Idx);		
		if( CurPrim ) 
		{
			// update bounding box/sphere for vis checks
			CurPrim->BoundingBox.Init();
			for( INT vIdx=0; vIdx < CurPrim->VertexStream.Vertices.Num(); vIdx++ )
			{
				CurPrim->BoundingBox += CurPrim->VertexStream.Vertices(vIdx).WorldPosition;
			}
#if !defined(_XBOX) && defined(XBOX_ASSET_FORMAT)
			// Pre-size the array
			FStaticProjectorVertex* pArray = (FStaticProjectorVertex*)appMalloc(
				CurPrim->VertexStream.Vertices.Num() * sizeof(FStaticProjectorVertex));
			// Copy the vert into the array that will be exported
			appMemcpy(pArray,&CurPrim->VertexStream.Vertices(0),
				sizeof(FStaticProjectorVertex) * CurPrim->VertexStream.Vertices.Num());
			// Stick the data in the TUmaArray directly
			CurPrim->VertexStream.XboxVertices.FixUpArray(
				CurPrim->VertexStream.Vertices.Num(),pArray);
#endif
			// update bounding sphere for vis
			CurPrim->BoundingSphere = FSphere( &CurPrim->BoundingBox.Min, 2 );

#ifndef XBOX_ASSET_FORMAT
			// PostLoad each prim iot rebuild static push buffers
			CurPrim->PostLoad();
#endif
		}
	}	

	unguard;
}

/* scion ======================================================================
* AProjector::AddStaticPrimToBatch
* Author: sz
* 
* Adds all the prims in a single FStaticProjectorInfo to the current batch
*
* - need to handle the BaseMaterial
* ============================================================================
*/
void AProjector::AddStaticPrimToBatch( FStaticProjectorInfo* pStaticInfo )
{
	guard(AProjector::AddStaticPrimToBatch);
	check(pStaticInfo);

	if( !pStaticInfo->Vertices.Num() || pStaticInfo->Indices.Num() < 3 )
		return;

	// find an entry with the same base material
	// the base material may be NULL
	INT PrimIdx = INDEX_NONE;
	for( INT Idx=0; Idx < StaticPrims.Num(); Idx++ )
	{
		UStaticProjectorClippedPrim* CurPrim = StaticPrims(Idx);
		if( CurPrim /*&& 
			CurPrim->BaseMaterial == pStaticInfo->BaseMaterial*/ )
		{
			PrimIdx = Idx;
            break;            
		}
	}
	// create a new entry if existing not found
	if( PrimIdx == INDEX_NONE )
	{
		PrimIdx = StaticPrims.AddItem( 
			ConstructObject<UStaticProjectorClippedPrim>( 
				UStaticProjectorClippedPrim::StaticClass(), 
				XLevel, 
				NAME_None, 
				RF_Public )
			);
		//StaticPrims(PrimIdx)->BaseMaterial = pStaticInfo->BaseMaterial;
		StaticPrims(PrimIdx)->BaseMaterial = NULL;
	}
	UStaticProjectorClippedPrim* Prim = StaticPrims(PrimIdx);

	if( !Prim ) {
		debugf( NAME_Error, TEXT("failed to construct UStaticProjectorClippedPrim") );
		return;
	}
	
	// append new vertices (remember start index)
	INT BeginVertIdx = Prim->VertexStream.Vertices.Add( pStaticInfo->Vertices.Num() );
	// copy verts
	for( INT Idx=0; Idx < pStaticInfo->Vertices.Num(); Idx++ )
	{
		Prim->VertexStream.Vertices(Idx+BeginVertIdx) = pStaticInfo->Vertices(Idx); 
	}
	// copy indices
	INT BeginIdx = Prim->IndexBuffer.Indices.Add( pStaticInfo->Indices.Num() );
	// add start index offset to indices
	for( INT Idx=0; Idx < pStaticInfo->Indices.Num(); Idx++ )
	{
		Prim->IndexBuffer.Indices(Idx+BeginIdx) = pStaticInfo->Indices(Idx) + BeginVertIdx; 
	}
#if 0
	// add UVs from base geometry if the projector needs to render using
	// the base material of the geometry
	if( pStaticInfo->BaseMaterial )
	{
		INT BeginUVIdx = Prim->UVStream.UVs.Add( pStaticInfo->BaseUVs.Num() );
		// copy UVs
		for( INT Idx=0; Idx < pStaticInfo->BaseUVs.Num(); Idx++ )
		{
			Prim->UVStream.UVs(Idx+BeginUVIdx) = pStaticInfo->BaseUVs(Idx); 
		}
		check( Prim->UVStream.UVs.Num() == Prim->VertexStream.Vertices.Num() && "missing UVs" );
	}
#endif
	// mark resource to be cached
	Prim->VertexStream.Revision++;
	
	unguard;
}

/* scion ======================================================================
* AProjector::RenderStaticPrimsBatch
* Author: sz
*
* ============================================================================
*/
void AProjector::RenderStaticPrimsBatch( FSceneNode* SceneNode, FRenderInterface* RI )
{
	guard(AProjector::RenderStaticPrimsBatch);
	FUNC_CLOCK(Proj,GStats.DWORDStats( GEngineStats.STATS_Projector_RenderCycles ));

	if( !StaticPrims.Num() || (UTexture::__Client && !UTexture::__Client->Projectors) )
		return;

	BEGIN_D3D_EVENT(BDE)(PIX_PROJECTOR,TEXT("Batched (Static) Projector"));

	RI->PushState();
	RI->SetTransform( TT_LocalToWorld, FMatrix::Identity );	
	RI->EnableLighting(0,1,0,NULL,0);
	RI->SetZBias(1);

	for( INT PrimIdx=0; PrimIdx < StaticPrims.Num(); PrimIdx++ )
	{
		UStaticProjectorClippedPrim* Prim = StaticPrims(PrimIdx);
		if( !Prim )
			continue;		

		// setup vertex shader enabled material that will handle the
		// projector for this batch
		FMultiMaterialProjectorInfo ProjMultMatInfo( Prim->BaseMaterial, bClipBSP );
		ProjMultMatInfo.AddProjectorEntry( RenderInfo );
		UProjectorMultiMaterial* ProjMultMat = ProjMultMatInfo.GetMaterial( (EProjectorBlending)RenderInfo->FrameBufferBlendingOp );
		RI->SetMaterial( ProjMultMat );

		// set vertex stream for this prim batch
		INT NumStreams=0;
		FVertexStream*	VertexStreams[2];
		VertexStreams[NumStreams++] = &Prim->VertexStream;
		// set uv stream if we are rendering with a base material
		if( Prim->BaseMaterial && 
			Prim->UVStream.UVs.Num() )
		{
			VertexStreams[NumStreams++] = &Prim->UVStream;
		}
		// set streams
		RI->SetVertexStreams( VS_Normal, VertexStreams, NumStreams );

		// draw using either a static push buffer or an index buffer
#ifdef _XBOX
		if( Prim->pPushBuffer )
		{
			RI->RunPushBuffer( Prim->pPushBuffer );
		}
		else
#endif
		{
			RI->SetIndexBuffer( &Prim->IndexBuffer, 0 );
			RI->DrawPrimitive( 
				PT_TriangleList, 
				0, 
				Prim->IndexBuffer.Indices.Num() / 3 
				);
		}

		STAT_INC_BY( GStats.DWORDStats(GEngineStats.STATS_Projector_Projectors),1 );
		STAT_INC_BY( GStats.DWORDStats(GEngineStats.STATS_Projector_Triangles),Prim->IndexBuffer.Indices.Num() / 3 );		
	}

	END_D3D_EVENT

	RI->PopState();

	unguard;
}



IMPLEMENT_CLASS(AProjector);
IMPLEMENT_CLASS(UProjectorMaterial);
IMPLEMENT_CLASS(UProjectorMultiMaterial);
// scion sz - native AShadowProjector 
IMPLEMENT_CLASS(AShadowProjector);
// scion sz - 
IMPLEMENT_CLASS(UStaticProjectorClippedPrim);


/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
