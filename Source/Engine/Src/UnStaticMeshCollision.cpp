/*=============================================================================
	UnStaticMeshCollision.cpp: Static mesh collision code.
	Copyright 2002 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/

#include "EnginePrivate.h"

//scion jg -- Common elements that I moved from here into a header
#include "UnCollision.h"

//!!vogel: won't compile but is useful for reference
#define OLD_COLLISION_CODE 0

#define INDEX_NONE_COLLISION 65535


UBOOL UStaticMesh::UseCylinderCollision( const AActor* Owner )
{
	guardSlow(UStaticMesh::UseCylinderCollision);

	return Owner->bUseCylinderCollision;
	unguardSlow;
}

//
//	UStaticMesh::GetCollisionBoundingBox
//

FBox UStaticMesh::GetCollisionBoundingBox(const AActor* Owner) const
{
	//scion rwd - use a second staticmesh as collision
	if (CollisionStaticMesh != NULL &&
		CollisionStaticMesh != this)
	{
		return CollisionStaticMesh->GetCollisionBoundingBox(Owner);
	}

	FBox Result;
	if(Owner->bUseCylinderCollision)
		Result = UPrimitive::GetCollisionBoundingBox(Owner);
	else
	{
		Result = BoundingBox.TransformBy(Owner->LocalToWorld());
		if(CollisionModel)
			Result += CollisionModel->GetCollisionBoundingBox(Owner);
	}

#ifdef WITH_KARMA
	// If this actor is bBlockKarma, when ensure its collision bounding box also includes 
	// the Karma primitive bounding box. This also keep the bounding box up to date.
	if(Owner->bBlockKarma)
	{
		McdModelID model = Owner->getKModel();
		if(model)
		{
			MeVector3 min, max;
			FVector umin, umax;
			McdModelGetAABB(model, min, max);
			KME2UPosition(&umin, min);
			KME2UPosition(&umax, max);
			Result += FBox(umin, umax);
		}
	}
#endif

	return Result;
}

//
//	UStaticMesh::LineCheck
//

//scion jg -- Changed to pass by ref
UBOOL UStaticMesh::LineCheck(FCheckResult& Result,AActor* Owner,AActor* Source,const FVector& End,const FVector& Start,const FVector& Extent,DWORD ExtraNodeFlags,DWORD TraceFlags)
{
	guard(UStaticMesh::LineCheck);
	//scion rwd - use a second staticmesh as collision
	if (CollisionStaticMesh != NULL &&
		CollisionStaticMesh != this &&
		(!Extent.IsZero() || (TraceFlags & TRACE_IgnoreExtent)) )
	{
		return CollisionStaticMesh->LineCheck(Result,Owner,Source,End,Start,Extent,ExtraNodeFlags,TraceFlags);
	}
	FUNC_CLOCK(CollCyc,GStats.DWORDStats(GEngineStats.STATS_StaticMesh_CollisionCycles));
	UBOOL	Hit = 0,
			ZeroExtent = (Extent.IsZero());

	if(Owner->bUseCylinderCollision)
	{
		Hit = !UPrimitive::LineCheck(Result,Owner,Source,End,Start,Extent,ExtraNodeFlags,TraceFlags);
	}
	//jg -- Don't do line checks against the collision model for zero extent
	// checks as this breaks all decals, emitters, projectors, etc.
	//epic superville: Allow line checks that should ingnore extent info (ie pawn dodge traces)
	else if( CollisionModel && 
			 UseSimpleBoxCollision && 
			 (ZeroExtent == 0 || (TraceFlags & TRACE_IgnoreExtent)) && 
			 !(TraceFlags & TRACE_ShadowCast) )
	{
		Hit = !CollisionModel->LineCheck(Result,Owner,Source,End,Start,Extent,ExtraNodeFlags,TraceFlags);
	}
	else if(kDOPTree.Nodes.Num())
	{
		Result.Time = 1.0f;

		//jg -- Handle compression of static meshes
		// Set the compression scale & offset on the compressor class
		FPackedPosition::SetCompressionVectors(&CompressionScale,
			&CompressionInvScale,&CompressionOffset);

		if(ZeroExtent)
		{
			//scion jg -- kDOP Collision check
			FkDOPLineCollisionCheck kDOPCheck(&Result,Owner,this,Start,End);
			Hit = kDOPTree.LineCheck(kDOPCheck);
			if (Hit == 1)
			{
				// Transform the hit normal to world space if there was a hit
				// This is deferred until now because multiple triangles might get
				// hit in the search and I want to delay the expensive transformation
				// as late as possible
				Result.Normal = kDOPCheck.GetHitNormal();
			}
		}
		else
		{
			//scion jg -- kDOP Collision check
			FkDOPBoxCollisionCheck kDOPCheck(&Result,Owner,this,Start,End,Extent);
			Hit = kDOPTree.BoxCheck(kDOPCheck);
			if (Hit == 1)
			{
				// Transform the hit normal to world space if there was a hit
				// This is deferred until now because multiple triangles might get
				// hit in the search and I want to delay the expensive transformation
				// as late as possible
				Result.Normal = kDOPCheck.GetHitNormal();
			}
			//TEMP Hack (fix for 2k4 indeterminent start bug)
			else if (kDOPCheck.bUseHack == 1)
			{
				// Get the direction of the trace so we can back up
				const FVector& Dir = (End - Start).SafeNormal();
				// Create a new start back some units
				FVector NewStart = Start - (Dir * 5.f);
				// Run a new trace
				FkDOPBoxCollisionCheck kDOPCheck2(&Result,Owner,this,NewStart,End,Extent);
				Hit = kDOPTree.BoxCheck(kDOPCheck2);
				if(Hit)
				{
					Result.Normal = kDOPCheck2.GetHitNormal();
					Result.Actor = Owner;
					Result.Primitive = this;
					Result.Time = Clamp(Result.Time - Clamp(0.1f,0.1f / (End - NewStart).Size(),4.0f / (End - NewStart).Size()),0.0f,1.0f);
					Result.Location = NewStart + (End - NewStart) * Result.Time;
				}
				return !Hit;
			}
		}

		if(Hit)
		{
			Result.Actor = Owner;
			Result.Primitive = this;
			Result.Time = Clamp(Result.Time - Clamp(0.1f,0.1f / (End - Start).Size(),4.0f / (End - Start).Size()),0.0f,1.0f);
			Result.Location = Start + (End - Start) * Result.Time;
		}
		//jg -- Wipe the compression scale & offset on the compressor class
		// so that we can find errors faster
		FPackedPosition::ClearCompressionVectors();
	}

	return !Hit;

	unguard;
}

//
//	UStaticMesh::PointCheck
//

//scion jg -- Changed to pass by ref
UBOOL UStaticMesh::PointCheck(FCheckResult& Result,AActor* Owner,const FVector& Location,const FVector& Extent,DWORD ExtraNodeFlags)
{
	guard(UStaticMesh::PointCheck);
	FUNC_CLOCK(PC,GStats.DWORDStats(GEngineStats.STATS_StaticMesh_CollisionCycles));
	//scion rwd - use a second staticmesh as collision
	if (CollisionStaticMesh != NULL &&
		CollisionStaticMesh != this &&
		!Extent.IsZero())
	{
		return CollisionStaticMesh->PointCheck(Result,Owner,Location,Extent,ExtraNodeFlags);
	}
	UBOOL	Hit = 0;

	if(Owner->bUseCylinderCollision)
		Hit = !UPrimitive::PointCheck(Result,Owner,Location,Extent,ExtraNodeFlags);
	else if(CollisionModel && UseSimpleBoxCollision)
		Hit = !CollisionModel->PointCheck(Result,Owner,Location,Extent,ExtraNodeFlags);
	else if(kDOPTree.Nodes.Num())
	{
		//jg -- Handle compression of static meshes
		// Set the compression scale & offset on the compressor class
		FPackedPosition::SetCompressionVectors(&CompressionScale,
			&CompressionInvScale,&CompressionOffset);
		//scion jg -- kDOP Collision check
		FkDOPPointCollisionCheck kDOPCheck(&Result,Owner,this,Location,Extent);
		Hit = kDOPTree.PointCheck(kDOPCheck);
		// Transform the hit normal to world space if there was a hit
		// This is deferred until now because multiple triangles might get
		// hit in the search and I want to delay the expensive transformation
		// as late as possible. Same thing holds true for the hit location
		if (Hit == 1)
		{
			Result.Normal = kDOPCheck.GetHitNormal();
			Result.Location = kDOPCheck.GetHitLocation();
		}
		if(Hit)
		{
			// Now calculate the location of the hit in world space
			Result.Actor = Owner;
			Result.Primitive = this;
		}
		//jg -- Wipe the compression scale & offset on the compressor class
		// so that we can find errors faster
		FPackedPosition::ClearCompressionVectors();
	}

	return !Hit;

	unguard;
}
