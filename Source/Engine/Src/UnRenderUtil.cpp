/*=============================================================================
	UnRenderUtil.cpp: Rendering utility implementations.
	Copyright 1997-2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/

#include "EnginePrivate.h"

//jg -- Header for xbox tristriping
#ifndef _XBOX
	#include "xbstrip.h"
#endif

//WD: added fvec.h and CONST_INT32_PS for F32vec4 classes and masking convenience respectively.
#if __HAS_SSE__
#include <fvec.h>

#define CONST_INT32_PS(N, V3, V2, V1, V0) \
	static const _MM_ALIGN16 int _##N[] = { V0, V1, V2, V3 }; /*little endian!*/ \
	const F32vec4 N = _mm_load_ps((float*)_##N);
#endif // #if __INTEL__


//
//  FConvexVolume::FConvexVolume
//

FConvexVolume::FConvexVolume()
{
	NumPlanes = 0;
}

//
//  FConvexVolume::SphereCheck
//

//scion jg -- Pass by ref not value
BYTE FConvexVolume::SphereCheck(const FSphere& Sphere)
{
	BYTE ClippingFlags	= CF_Inside;

//WD: added Pentium(r) 4 processor optimized version of SphereCheck()
	if( NumPlanes >= 4 )
	{
		F32vec4 mask;
		F32vec4 dist;
		F32vec4 bp_x, bp_y, bp_z, bp_w;
		F32vec4 s_x, s_y, s_z, s_w;
		F32vec4 tr0, tr1, tr2;

		INT Index;
		INT remainder = NumPlanes % 4;
		INT count = NumPlanes - remainder;

		s_x = _mm_load1_ps(&Sphere.X);
		s_y = _mm_load1_ps(&Sphere.Y);
		s_z = _mm_load1_ps(&Sphere.Z);
		s_w = _mm_load1_ps(&Sphere.W);

		for (Index = 0; Index < count; Index+=4)
		{
			// SIMD init of bounding planes' components
			tr0  = _mm_loadl_pi(tr0, (__m64*)&BoundingPlanes[Index].X);   // -- -- y0 x0
			tr0  = _mm_loadh_pi(tr0, (__m64*)&BoundingPlanes[Index+1].X); // y1 x1 y0 x0
			tr1  = _mm_loadl_pi(tr1, (__m64*)&BoundingPlanes[Index+2].X); // -- -- y2 x2
			tr1  = _mm_loadh_pi(tr1, (__m64*)&BoundingPlanes[Index+3].X); // y3 x3 y2 x2
			bp_x = _mm_shuffle_ps(tr0, tr1, _MM_SHUFFLE(2, 0, 2, 0));     // x3 x2 x1 x0
			bp_y = _mm_shuffle_ps(tr0, tr1, _MM_SHUFFLE(3, 1, 3, 1));     // y3 y2 y1 y0
			tr0  = _mm_loadl_pi(tr0, (__m64*)&BoundingPlanes[Index].Z);   // -- -- w0 z0
			tr0  = _mm_loadh_pi(tr0, (__m64*)&BoundingPlanes[Index+1].Z); // w1 z1 w0 z0
			tr1  = _mm_loadl_pi(tr1, (__m64*)&BoundingPlanes[Index+2].Z); // -- -- w2 z2
			tr1  = _mm_loadh_pi(tr1, (__m64*)&BoundingPlanes[Index+3].Z); // w3 z3 w2 z2
			bp_z = _mm_shuffle_ps(tr0, tr1, _MM_SHUFFLE(2, 0, 2, 0));     // z3 z2 z1 z0
			bp_w = _mm_shuffle_ps(tr0, tr1, _MM_SHUFFLE(3, 1, 3, 1));     // w3 w2 w1 w0

			// Bounding plane distance to sphere (p.X*o.X + p.Y*o.Y + p.Z*o.Z - p.W)
			dist = _mm_sub_ps(_mm_add_ps(_mm_add_ps(_mm_mul_ps(bp_x, s_x), _mm_mul_ps(bp_y, s_y)), _mm_mul_ps(bp_z, s_z)), bp_w);

			mask = cmpgt(dist, _mm_sub_ps(_mm_setzero_ps(), s_w));
			if (move_mask(mask)) // if at least one is true...
			{
				// At least one sphere is partially outside.
				ClippingFlags |= CF_Outside;

				mask = cmpgt(dist, s_w);
				if (move_mask(mask))
				{
					// At least one sphere is entirely outside.
					ClippingFlags &= ~CF_Inside;
					break;
				}
			}
		}
		if (remainder && (ClippingFlags & CF_Inside))
		{
			for (Index; Index < NumPlanes; Index++)
			{
				FLOAT Dist = BoundingPlanes[Index].PlaneDot(Sphere);

				if(Dist > -Sphere.W)
				{
					// The sphere is partially outside.
					ClippingFlags |= CF_Outside;

					if(Dist > Sphere.W)
					{
						// The sphere is entirely outside.
						ClippingFlags &= ~CF_Inside;
						break; // SL added
					}
				}
			}
		}
	}
	else
	{
	    for(INT Index = 0;Index < NumPlanes;Index++)
	    {
		    FLOAT Dist		= BoundingPlanes[Index].PlaneDot(Sphere);

		    if(Dist > -Sphere.W)
		    {
			    // The sphere is partially outside.

			    ClippingFlags |= CF_Outside;

			    if(Dist > Sphere.W)
			    {
				    // The sphere is entirely outside.

				    ClippingFlags &= ~CF_Inside;
				    break; // SL added
			    }
		    }
	    }
	}

	return ClippingFlags;
}
//WD: Don't compile original code...  I left this code here for Andrew's (or another developer's) convenience.
#if 0
{
	BYTE ClippingFlags	= CF_Inside;

	for(INT Index = 0;Index < NumPlanes;Index++)
	{
		FLOAT	Dist = BoundingPlanes[Index].PlaneDot(Sphere);

		if(Dist > -Sphere.W)
		{
			// The sphere is partially outside.

			ClippingFlags |= CF_Outside;

			if(Dist > Sphere.W)
			{
				// The sphere is entirely outside.

				ClippingFlags &= ~CF_Inside;
				 break; // SL added
			}
		}
	}

	return ClippingFlags;
}
#endif

//
//  FConvexVolume::BoxCheck
//

//scion jg -- Pass by ref not value
BYTE FConvexVolume::BoxCheck(const FVector& Origin,const FVector& Extent)
{
	// scion sz - box check stats
	FUNC_CLOCK(BoxTestCycles,GStats.DWORDStats( GEngineStats.STATS_Visibility_BoxTestCycles ));

	BYTE ClippingFlags	= CF_Inside;

//WD: added Pentium(r) 4 processor optimized version of BoxCheck()
	if( NumPlanes >= 4 )
	{
		CONST_INT32_PS(smask, ~(1<<31), ~(1<<31), ~(1<<31), ~(1<<31));

		F32vec4 mask,
				dist,
				pushout,
				bp_x, bp_y, bp_z, bp_w, // 4 bounding planes' x, y, z, and w components
				e_x, e_y, e_z, // extent's components, broadcasted across xmm registers
				o_x, o_y, o_z, // origin's components, broadcasted across xmm registers
				tr0, tr1, tr2; // temp registers 0-2

		// SIMD init of Origin vector's components
		o_x = _mm_load1_ps(&Origin.X);
		o_y = _mm_load1_ps(&Origin.Y);
		o_z = _mm_load1_ps(&Origin.Z);

		// SIMD init of extent (Normal) vector's components
		e_x = _mm_load1_ps(&Extent.X);
		e_y = _mm_load1_ps(&Extent.Y);
		e_z = _mm_load1_ps(&Extent.Z);

		INT remainder = NumPlanes % 4;
		INT count = NumPlanes - remainder;
		INT Index;

		for (Index = 0; Index < count; Index+=4)
		{
			// SIMD init of bounding planes' components
			tr0  = _mm_loadl_pi(tr0, (__m64*)&BoundingPlanes[Index].X);   // -- -- y0 x0
			tr0  = _mm_loadh_pi(tr0, (__m64*)&BoundingPlanes[Index+1].X); // y1 x1 y0 x0
			tr1  = _mm_loadl_pi(tr1, (__m64*)&BoundingPlanes[Index+2].X); // -- -- y2 x2
			tr1  = _mm_loadh_pi(tr1, (__m64*)&BoundingPlanes[Index+3].X); // y3 x3 y2 x2
			bp_x = _mm_shuffle_ps(tr0, tr1, _MM_SHUFFLE(2, 0, 2, 0));     // x3 x2 x1 x0
			bp_y = _mm_shuffle_ps(tr0, tr1, _MM_SHUFFLE(3, 1, 3, 1));     // y3 y2 y1 y0
			tr0  = _mm_loadl_pi(tr0, (__m64*)&BoundingPlanes[Index].Z);   // -- -- w0 z0
			tr0  = _mm_loadh_pi(tr0, (__m64*)&BoundingPlanes[Index+1].Z); // w1 z1 w0 z0
			tr1  = _mm_loadl_pi(tr1, (__m64*)&BoundingPlanes[Index+2].Z); // -- -- w2 z2
			tr1  = _mm_loadh_pi(tr1, (__m64*)&BoundingPlanes[Index+3].Z); // w3 z3 w2 z2
			bp_z = _mm_shuffle_ps(tr0, tr1, _MM_SHUFFLE(2, 0, 2, 0));     // z3 z2 z1 z0
			bp_w = _mm_shuffle_ps(tr0, tr1, _MM_SHUFFLE(3, 1, 3, 1));     // w3 w2 w1 w0

			// Bounding plane distance to origin (p.X*o.X + p.Y*o.Y + p.Z*o.Z - p.W)
			dist = _mm_sub_ps(_mm_add_ps(_mm_add_ps(_mm_mul_ps(bp_x, o_x), _mm_mul_ps(bp_y, o_y)), _mm_mul_ps(bp_z, o_z)), bp_w);

			// Push out distance (fabs(Extent.X*Plane.X) + fabs(Extent.Y*Plane.Y) + fabs(Extent.Z*Plane.Z))
			pushout = _mm_add_ps(_mm_add_ps(smask & _mm_mul_ps(bp_x, e_x), smask & _mm_mul_ps(bp_y, e_y)), smask & _mm_mul_ps(bp_z, e_z));

			mask = cmpgt(dist, _mm_sub_ps(_mm_setzero_ps(), pushout));
			if (move_mask(mask)) // if at least one is true...
			{
				// ...at least one box is partially outside
				ClippingFlags |= CF_Outside;
				
				mask = cmpgt(dist, pushout);
				if (move_mask(mask))  // if at least one is true...
				{
					// ...at least one box is entirely outside
					ClippingFlags &= ~CF_Inside;
					break;
				}
			}
		}
		if (remainder && (ClippingFlags & CF_Inside))
		{
			for (Index; Index < NumPlanes; Index++)
			{
				FPlane& Plane   = BoundingPlanes[Index];

				FLOAT Dist    = Plane.PlaneDot(Origin);
				FLOAT PushOut = fabs(Extent.X*Plane.X) + fabs(Extent.Y*Plane.Y) + fabs(Extent.Z*Plane.Z);

				if(Dist > -PushOut)
				{
					// The box is partially outside.

					ClippingFlags |= CF_Outside;

					if(Dist > PushOut)
					{
						// The box is entirely outside.

						ClippingFlags &= ~CF_Inside;
						break; // SL added
					}
				}
			}
		}
	}
	else
	{
		for (INT Index = 0; Index < NumPlanes; Index++)
		{
			FPlane& Plane   = BoundingPlanes[Index];
			//WD: removed 1 level of function calling
/*
			FLOAT   Dist    = Plane.PlaneDot(Origin),
			PushOut = FBoxPushOut(Extent,Plane);
*/
			FLOAT Dist    = Plane.PlaneDot(Origin);
			FLOAT PushOut = fabs(Extent.X*Plane.X) + fabs(Extent.Y*Plane.Y) + fabs(Extent.Z*Plane.Z);

			if(Dist > -PushOut)
			{
				// The sphere is partially outside.

				ClippingFlags |= CF_Outside;

				if(Dist > PushOut)
				{
					// The sphere is entirely outside.

					ClippingFlags &= ~CF_Inside;
					break; // SL added
				}
			}
		}
	}

	return ClippingFlags;
}
//WD: Don't compile original code...  I left this code here for Andrew's (or another developer's) convenience.
#if 0
{
	BYTE ClippingFlags	= CF_Inside;

	for(INT Index = 0;Index < NumPlanes;Index++)
	{
		FPlane&	Plane	= BoundingPlanes[Index];
		FLOAT	Dist	= Plane.PlaneDot(Origin),
				PushOut = FBoxPushOut(Extent,Plane);

		if(Dist > -PushOut)
		{
			// The sphere is partially outside.

			ClippingFlags |= CF_Outside;

			if(Dist > PushOut)
			{
				// The sphere is entirely outside.

				ClippingFlags &= ~CF_Inside;
				break; // SL added
			}
		}
	}

	return ClippingFlags;
}
#endif

//
//  FConvexVolume::ClipPolygon
//

FPoly FConvexVolume::ClipPolygon(FPoly Polygon)
{
	for(INT PlaneIndex = 0;PlaneIndex < NumPlanes;PlaneIndex++)
	{
		FPlane&	Plane = BoundingPlanes[PlaneIndex];

		if(!Polygon.Split(-FVector(Plane),Plane * Plane.W,1))
		{
			Polygon.NumVertices = 0;
			return Polygon;
		}
	}

	return Polygon;
}

//
// Temp Line Batcher
//

ENGINE_API FTempLineBatcher* GTempLineBatcher;


void FTempLineBatcher::Render(FRenderInterface* InRI, UBOOL InZTest)
{
	FLineBatcher kLines(InRI, InZTest);

	// lines
	for(INT i=0; i<LineArray.Num(); i++)
	{
		//scion jg -- Staying line support
		if (LineArray(i).NumFrames != 0)
		{
			// Leave negative values alone. They are "permanent"
			if (LineArray(i).NumFrames > 0)
			{
				// Decrement our count
				LineArray(i).NumFrames--;
			}
			// Now draw the line
			kLines.DrawLine(LineArray(i).Start, LineArray(i).End,
				LineArray(i).Color);
		}
		// See if this line needs to be removed or not
		if (LineArray(i).NumFrames == 0)
		{
			// Remove this line since it isn't drawing anymore
			LineArray.Remove(i);
			i--;
		}
	}
	
	// boxes
	for(INT i=0; i<BoxArray.Num(); i++)
	{
		//scion jg -- Staying box support
		if (BoxArray(i).NumFrames != 0)
		{
			// Leave negative values alone. They are "permanent"
			if (BoxArray(i).NumFrames > 0)
			{
				// Decrement our count
				BoxArray(i).NumFrames--;
			}
			// Now draw the box
			kLines.DrawBox(BoxArray(i).Box, BoxArray(i).Color);
		}
		// See if this box needs to be removed or not
		if (BoxArray(i).NumFrames == 0)
		{
			// Remove this box since it isn't drawing anymore
			BoxArray.Remove(i);
			i--;
		}
	}

	//epic moelfke: spheres
	for(INT i = 0; i < SphereArray.Num(); i++)
	{
		if (SphereArray(i).NumFrames != 0)
		{
			// Leave negative values alone. They are "permanent"
			if (SphereArray(i).NumFrames > 0)
			{
				// Decrement our count
				SphereArray(i).NumFrames--;
			}
			// Now draw the sphere
			kLines.DrawSphere(SphereArray(i).Center, SphereArray(i).Radius, SphereArray(i).Color, SphereArray(i).NumDivisions);
		}
		// See if this sphere needs to be removed or not
		if (SphereArray(i).NumFrames == 0)
		{
			// Remove this sphere since it isn't drawing anymore
			SphereArray.Remove(i);
			i--;
		}
	}
	
	kLines.Flush();
};

//
//  FLineBatcher::FLineBatcher
//

FLineBatcher::FLineBatcher( FRenderInterface* InRI, UBOOL InZTest )
{
	RI = InRI;
	ZTest = InZTest;
}

//
//  FLineBatcher::~FLineBatcher
//

FLineBatcher::~FLineBatcher()
{
	Flush();
}

//
//  FLineBatcher::Flush
//

void FLineBatcher::Flush( DWORD PolyFlags )
{
	guard(FLineBatcher::Flush);


	//!!MAT do something with PolyFlags


	if(Vertices.Num())
	{
		INT	BaseVertexIndex = RI->SetDynamicStream(VS_FixedFunction,this);

		static FSolidColorTexture	WhiteTexture(FColor(255,255,255));
		
		DECLARE_STATIC_UOBJECT( UProxyBitmapMaterial, HACKGAH, { HACKGAH->SetTextureInterface(&WhiteTexture); } );
		DECLARE_STATIC_UOBJECT( UFinalBlend, LineMaterial, { LineMaterial->Material = HACKGAH; } );
		LineMaterial->ZTest = ZTest;

		RI->EnableLighting(0,1);
		RI->SetMaterial(LineMaterial);
		RI->SetIndexBuffer(NULL,0);

		RI->DrawPrimitive(PT_LineList,BaseVertexIndex,Vertices.Num() / 2,0,Vertices.Num() - 1);

		Vertices.Empty();
	}

	unguard;
}

//
//  FLineBatcher::DrawLine
//

void FLineBatcher::DrawLine(FVector P1,FVector P2,FColor Color)
{
	guard(FLineBatcher::DrawLine);

	Color = Color.RenderColor();

	new(Vertices) FLineVertex(P1,Color);
	new(Vertices) FLineVertex(P2,Color);

	unguard;
}

//
//  FLineBatcher::DrawPoint
//

void FLineBatcher::DrawPoint(FSceneNode* SceneNode,FVector P,FColor Color)
{
	guard(FLineBatcher::DrawPoint);

	Color = Color.RenderColor();

	FLOAT	W = P.X * SceneNode->WorldToScreen.M[0][3] + P.Y * SceneNode->WorldToScreen.M[1][3] + P.Z * SceneNode->WorldToScreen.M[2][3] + SceneNode->WorldToScreen.M[3][3];
	FVector	X = SceneNode->CameraX * (W * 0.5f),
			Y = SceneNode->CameraY * (W * 0.5f);

	DrawLine(P - X - Y,P + X - Y,Color);
	DrawLine(P + X - Y,P + X + Y,Color);
	DrawLine(P + X + Y,P - X + Y,Color);
	DrawLine(P - X + Y,P - X - Y,Color);

	unguard;
}

//
//  FLineBatcher::DrawBox
//

void FLineBatcher::DrawBox(FBox Box,FColor Color)
{
	guard(FLineBatcher::DrawBox);

	Color = Color.RenderColor();

	FVector	B[2],P,Q;
	int i,j;

	B[0]=Box.Min;
	B[1]=Box.Max;

	for( i=0; i<2; i++ ) for( j=0; j<2; j++ )
	{
		P.X=B[i].X; Q.X=B[i].X;
		P.Y=B[j].Y; Q.Y=B[j].Y;
		P.Z=B[0].Z; Q.Z=B[1].Z;
		DrawLine(P,Q,Color);

		P.Y=B[i].Y; Q.Y=B[i].Y;
		P.Z=B[j].Z; Q.Z=B[j].Z;
		P.X=B[0].X; Q.X=B[1].X;
		DrawLine(P,Q,Color);

		P.Z=B[i].Z; Q.Z=B[i].Z;
		P.X=B[j].X; Q.X=B[j].X;
		P.Y=B[0].Y; Q.Y=B[1].Y;
		DrawLine(P,Q,Color);
	}

	unguard;
}

//
//  FLineBatcher::DrawCircle
//

void FLineBatcher::DrawCircle(FVector Base,FVector X,FVector Y,FColor Color,FLOAT Radius,INT NumSides)
{
	guard(FLineBatcher::DrawCircle);

	Color = Color.RenderColor();

	FLOAT	AngleDelta = 2.0f * PI / NumSides;
	FVector	LastVertex = Base + X * Radius;

	for(INT SideIndex = 0;SideIndex < NumSides;SideIndex++)
	{
		FVector	Vertex = Base + (X * appCos(AngleDelta * (SideIndex + 1)) + Y * appSin(AngleDelta * (SideIndex + 1))) * Radius;

		DrawLine(LastVertex,Vertex,Color);

		LastVertex = Vertex;
	}

	unguard;
}

//epic moelfke: sphere drawing support
void FLineBatcher::DrawSphere(const FVector& Base, FLOAT Radius, FColor Color, INT NumDivisions)
{
	guard(FLineBatcher::DrawSphere);

	Color = Color.RenderColor();

	FLOAT	AngleDelta = 2.0f * PI / NumDivisions;
	FLOAT	SegmentDist = SegmentDist = 2.0 * Radius / NumDivisions;

	// Horizontal circles change in scale
	INT HalfDivisions = NumDivisions / 2;
	for (INT SideIndex = -HalfDivisions; SideIndex < HalfDivisions; SideIndex++)
	{
		DrawCircle(Base - FVector(0,0,SideIndex*SegmentDist), FVector(1,0,0), FVector(0,1,0), Color, appSqrt(Radius*Radius - SideIndex*SegmentDist*SideIndex*SegmentDist), NumDivisions);
	}

	// Vertical circles change in angle
	for (INT SideIndex = 0; SideIndex < NumDivisions; SideIndex++)
	{
		DrawCircle(Base, FVector(0,0,1), FVector(1,0,0) * appCos(AngleDelta * SideIndex) + FVector(0,1,0) * appSin(AngleDelta * SideIndex), Color, Radius, NumDivisions);
	}

	unguard;
}

//
//  FLineBatcher::DrawCylinder
//

void FLineBatcher::DrawCylinder(FRenderInterface* RI,FVector Base,FVector X,FVector Y,FVector Z,FColor Color,FLOAT Radius,FLOAT HalfHeight,INT NumSides)
{
	guard(FLineBatcher::DrawCylinder);

	Color = Color.RenderColor();

	FLOAT	AngleDelta = 2.0f * PI / NumSides;
	FVector	LastVertex = Base + X * Radius;

	for(INT SideIndex = 0;SideIndex < NumSides;SideIndex++)
	{
		FVector	Vertex = Base + (X * appCos(AngleDelta * (SideIndex + 1)) + Y * appSin(AngleDelta * (SideIndex + 1))) * Radius;

		DrawLine(LastVertex - Z * HalfHeight,Vertex - Z * HalfHeight,Color);
		DrawLine(LastVertex + Z * HalfHeight,Vertex + Z * HalfHeight,Color);
		DrawLine(LastVertex - Z * HalfHeight,LastVertex + Z * HalfHeight,Color);

		LastVertex = Vertex;
	}

	unguard;
}

//
//  FLineBatcher::DrawDirectionalArrow
//

void FLineBatcher::DrawDirectionalArrow(FVector InLocation,FRotator InRotation,FColor InColor,FLOAT InDrawScale)
{
	guard(FLineBatcher::DrawDirectionalArrow);

	InColor = InColor.RenderColor();

	FCoords C = GMath.UnitCoords / InRotation;

	FLOAT Length = 48 * InDrawScale,
		SegmentLength = 16 * InDrawScale;

	DrawLine(InLocation + C.XAxis * Length,InLocation,InColor);
	DrawLine(InLocation + C.XAxis * Length,InLocation + C.XAxis * SegmentLength + C.YAxis * SegmentLength,InColor);
	DrawLine(InLocation + C.XAxis * Length,InLocation + C.XAxis * SegmentLength - C.YAxis * SegmentLength,InColor);
	DrawLine(InLocation + C.XAxis * Length,InLocation + C.XAxis * SegmentLength + C.ZAxis * SegmentLength,InColor);
	DrawLine(InLocation + C.XAxis * Length,InLocation + C.XAxis * SegmentLength - C.ZAxis * SegmentLength,InColor);

	unguard;
}

//
//  FLineBatcher::DrawConvexVolume
//
void FLineBatcher::DrawConvexVolume(FConvexVolume Volume,FColor Color)
{
	guard(FLineBatcher::DrawConvexVolume);

	Color = Color.RenderColor();

	for(INT FaceIndex = 0;FaceIndex < Volume.NumPlanes;FaceIndex++)
	{
		FPoly	Polygon;

		Polygon.Normal = Volume.BoundingPlanes[FaceIndex];
		Polygon.NumVertices = 4;

		FVector	Base = Polygon.Normal * Volume.BoundingPlanes[FaceIndex].W,
				AxisX,
				AxisY;

		Polygon.Normal.FindBestAxisVectors(AxisX,AxisY);

		Polygon.Vertex[0] = Base + AxisX * HALF_WORLD_MAX + AxisY * HALF_WORLD_MAX;
		Polygon.Vertex[1] = Base - AxisX * HALF_WORLD_MAX + AxisY * HALF_WORLD_MAX;
		Polygon.Vertex[2] = Base - AxisX * HALF_WORLD_MAX - AxisY * HALF_WORLD_MAX;
		Polygon.Vertex[3] = Base + AxisX * HALF_WORLD_MAX - AxisY * HALF_WORLD_MAX;

		for(INT PlaneIndex = 0;PlaneIndex < Volume.NumPlanes;PlaneIndex++)
		{
			if(PlaneIndex != FaceIndex)
			{
				if(!Polygon.Split(-FVector(Volume.BoundingPlanes[PlaneIndex]),Volume.BoundingPlanes[PlaneIndex] * Volume.BoundingPlanes[PlaneIndex].W))
				{
					Polygon.NumVertices = 0;
					break;
				}
			}
		}

		for(INT VertexIndex = 0;VertexIndex < Polygon.NumVertices;VertexIndex++)
			DrawLine(Polygon.Vertex[VertexIndex],Polygon.Vertex[(VertexIndex + 1) % Polygon.NumVertices],Color);
	}

	unguard;
}

//
//  FLineBatcher::GetRevision
//

INT FLineBatcher::GetRevision()
{
	return 1;
}

//
//  FLineBatcher::GetSize
//

INT FLineBatcher::GetSize()
{
	return Vertices.Num() * sizeof(FLineVertex);
}

//
//  FLineBatcher::GetStride
//

INT FLineBatcher::GetStride()
{
	return sizeof(FLineVertex);
}

//
//  FLineBatcher::GetComponents
//

INT FLineBatcher::GetComponents(FVertexComponent* OutComponents)
{
	OutComponents[0].Type = CT_Float3;
	OutComponents[0].Function = FVF_Position;
	OutComponents[1].Type = CT_Color;
	OutComponents[1].Function = FVF_Diffuse;

	return 2;
}

//
//  FLineBatcher::GetStreamData
//

void FLineBatcher::GetStreamData(void* Dest)
{
	appMemcpy(Dest,&Vertices(0),Vertices.Num() * sizeof(FLineVertex));
}

//
//  FLineBatcher::GetRawStreamData
//

void FLineBatcher::GetRawStreamData(void ** Dest, INT FirstVertex )
{
	  *Dest = &Vertices(FirstVertex);
}

//
//  FCanvasUtil::FCanvasUtil
//

FCanvasUtil::FCanvasUtil(FRenderTarget* RenderTarget,FRenderInterface* InRI) :
	bZTest(0)
{
	RI = InRI;

	FLOAT	SizeX = RenderTarget->GetWidth(),
			SizeY = RenderTarget->GetHeight();

	if( GIsOpenGL )
		CanvasToScreen = FTranslationMatrix(FVector(-SizeX / 2.0f,-SizeY / 2.0f,0.0f)) * FScaleMatrix(FVector(2.0f / SizeX,-2.0f / SizeY,1.0f));
	else
		CanvasToScreen = FTranslationMatrix(FVector(-SizeX / 2.0f - 0.5f,-SizeY / 2.0f - 0.5f,0.0f)) * FScaleMatrix(FVector(2.0f / SizeX,-2.0f / SizeY,1.0f));

	ScreenToCanvas = CanvasToScreen.Inverse();
}

//
//  FCanvasUtil::~FCanvasUtil
//

FCanvasUtil::~FCanvasUtil()
{
	Flush();
}

/* epic ===============================================
* ::Flush
* Author: rayd
*
* Batched version of flush iterates through Batches[]
* and sends each set of vertices to the RI.
*
* =====================================================
*/
void FCanvasUtil::Flush()
{
	guard(FCanvasUtil::Flush);
	FUNC_CLOCK(Flush,GStats.DWORDStats(GEngineStats.STATS_Canvas_Flush));
	// if we have anything to render,
	if (Batches.Num() > 0)
	{
		RI->PushState();
		RI->SetDistanceFog( 0, 0, 0, FColor(0,0,0) );
		RI->SetTransform(TT_LocalToWorld,FMatrix::Identity);
		RI->SetTransform(TT_WorldToCamera,FMatrix::Identity);
		RI->SetTransform(TT_CameraToScreen,CanvasToScreen);
		RI->EnableLighting(0,1);
		// start at the first batch and start the rendering process,
		for (BatchIdx = 0; BatchIdx < Batches.Num(); BatchIdx++)
		{
			FCanvasBatch &batch = Batches(BatchIdx);
			// make sure there are actually verts for this batch
			if (batch.NumPrimitives > 0)
			{
				UMaterial *renderMaterial = batch.Material;				
				if (STY_Normal < batch.RenderStyle && batch.RenderStyle < STY_MAX &&
					(batch.Material->RenderHint & MRH_Texture || batch.RenderStyle==STY_AlphaZ))
				{
					// setup the final blend for the render style
					DECLARE_STATIC_UOBJECT( UFinalBlend, FinalBlend, {} );
					FinalBlend->Material = batch.Material;
					FinalBlend->TwoSided = 0;
					FinalBlend->ZWrite = 0;
					FinalBlend->ZTest = bZTest;
					FinalBlend->ColorWriteEnable = 1;

					switch ((ERenderStyle)batch.RenderStyle)
					{
					case STY_Masked:
						FinalBlend->FrameBufferBlending = FB_Overwrite;
						FinalBlend->AlphaTest = 1;
						FinalBlend->AlphaRef = 127;
						renderMaterial = FinalBlend;				
						break;
					case STY_Translucent:
						FinalBlend->FrameBufferBlending = FB_Translucent;
						FinalBlend->AlphaTest = 0;
						renderMaterial = FinalBlend;				
						break;
					case STY_Modulated:
						FinalBlend->FrameBufferBlending = FB_Modulate;
						FinalBlend->AlphaTest = 0;
						renderMaterial = FinalBlend;				
						break;
					case STY_Alpha:
						FinalBlend->FrameBufferBlending = FB_AlphaBlend;
						FinalBlend->AlphaTest = 1;
						FinalBlend->AlphaRef = 0;
						renderMaterial = FinalBlend;				
						break;
					case STY_Additive:
						FinalBlend->FrameBufferBlending = FB_Brighten;
						FinalBlend->AlphaTest = 1;
						FinalBlend->AlphaRef = 0;
						renderMaterial = FinalBlend;				
						break;
					case STY_Subtractive:
						FinalBlend->FrameBufferBlending = FB_Darken;
						FinalBlend->AlphaTest = 0;
						renderMaterial = FinalBlend;				
						break;
					// scion sz - used for special mode to render only z values
					case STY_AlphaZ:
						FinalBlend->FrameBufferBlending = FB_Overwrite;
						FinalBlend->AlphaTest = 0;
						FinalBlend->ZWrite = 1;
						FinalBlend->ColorWriteEnable = 0;
						renderMaterial = FinalBlend;						
						break;
					}
				}
				RI->SetMaterial(renderMaterial);
				RI->SetIndexBuffer(NULL,0);
				RI->SetCullMode(CM_None);
		#ifdef _XBOX
				BEGIN_D3D_EVENT(BDE)(PIX_CANVAS,TEXT("FCanvasUtil"));
				//scion jg -- Copy the verts directly into the push buffer
				RI->DrawPrimitiveUP(0,batch.PrimitiveType,batch.NumPrimitives,this);
				END_D3D_EVENT;
		#else
				INT	BaseVertexIndex = RI->SetDynamicStream(0,this);
				RI->DrawPrimitive(batch.PrimitiveType,BaseVertexIndex,batch.NumPrimitives,0,batch.Vertices.Num() - 1);
		#endif
				// clean up this batch
				batch.Vertices.Empty();
				batch.BoundingBoxes.Empty();
				batch.Bounds.Min = FVector(0,0,0);
				batch.Bounds.Max = FVector(0,0,0);
				batch.Material = NULL;
				batch.NumPrimitives = 0;
			}
		}
		RI->PopState();
		// recycle batches instead of clearing, save on allocations
		Batches.Empty(Batches.Num());
	}
	unguard;
}

//
//  FCanvasUtil::DrawLine
//

void FCanvasUtil::DrawLine(FLOAT X1,FLOAT Y1,FLOAT X2,FLOAT Y2,FColor Color)
{
	guard(FCanvasUtil::DrawLine);

	Color = Color.RenderColor();

	static FSolidColorTexture	WhiteTexture(FColor(255,255,255));
	
	DECLARE_STATIC_UOBJECT( UProxyBitmapMaterial, HACKGAH, { HACKGAH->SetTextureInterface(&WhiteTexture); } );
	DECLARE_STATIC_UOBJECT( UFinalBlend, LineMaterial, { LineMaterial->Material = HACKGAH; } );

	// figure out the bounds for this primitive
	FBox tileBounds(FVector(X1,Y1,0),FVector(X2,Y2,0));
	// grab our batch index
	BeginPrimitive(PT_LineList,LineMaterial,255,tileBounds);
	// point to the proper set of vertices in the batch
	TArray<FCanvasVertex> &Vertices = Batches(BatchIdx).Vertices;

	new(Vertices) FCanvasVertex(FVector(X1,Y1,0),Color,0.0f,0.0f);
	new(Vertices) FCanvasVertex(FVector(X2,Y2,0),Color,0.0f,0.0f);

	Batches(BatchIdx).NumPrimitives++;

	unguard;
}

//
//  FCanvasUtil::DrawPoint
//

void FCanvasUtil::DrawPoint(FLOAT X1,FLOAT Y1,FLOAT X2,FLOAT Y2,FLOAT Z,FColor Color)
{
	guard(FCanvasUtil::DrawPoint);
	
	Color = Color.RenderColor();

	static FSolidColorTexture	WhiteTexture(FColor(255,255,255));
	
	DECLARE_STATIC_UOBJECT( UProxyBitmapMaterial, HACKGAH, { HACKGAH->SetTextureInterface(&WhiteTexture); } );
	DECLARE_STATIC_UOBJECT( UFinalBlend, LineMaterial, { LineMaterial->Material = HACKGAH; } );

	// figure out the bounds for this primitive
	FBox tileBounds(FVector(X1,Y1,Z),FVector(X2,Y2,Z));
	// grab our batch index
	BeginPrimitive(PT_LineList,LineMaterial,255,tileBounds);
	// point to the proper set of vertices in the batch
	TArray<FCanvasVertex> &Vertices = Batches(BatchIdx).Vertices;

	new(Vertices) FCanvasVertex(FVector(X1,Y1,Z),Color,0.0f,0.0f);
	new(Vertices) FCanvasVertex(FVector(X2,Y1,Z),Color,0.0f,0.0f);

	new(Vertices) FCanvasVertex(FVector(X2,Y1,Z),Color,0.0f,0.0f);
	new(Vertices) FCanvasVertex(FVector(X2,Y2,Z),Color,0.0f,0.0f);

	new(Vertices) FCanvasVertex(FVector(X2,Y2,Z),Color,0.0f,0.0f);
	new(Vertices) FCanvasVertex(FVector(X1,Y2,Z),Color,0.0f,0.0f);

	new(Vertices) FCanvasVertex(FVector(X1,Y2,Z),Color,0.0f,0.0f);
	new(Vertices) FCanvasVertex(FVector(X1,Y1,Z),Color,0.0f,0.0f);

	Batches(BatchIdx).NumPrimitives += 4;

	unguard;
}

/* epic ===============================================
* ::BeginPrimitive
* Author: rayd
*
* This should be called before any drawing occurs, as
* it finds the correct BatchIdx to build the vertices
* into.  Performs some basic bounds checks against
* previous batches to find the optimal insertion, or
* creates a new entry at the top of the list if no
* insertion is possible.
*
* TODO: support changes in the matrices
*
* =====================================================
*/
void FCanvasUtil::BeginPrimitive(EPrimitiveType primType, UMaterial *material, BYTE renderStyle, FBox &tileBounds)
{
	guard(FCanvasUtil::BeginPrimitive);
	FUNC_CLOCK(BeginPrimitive,GStats.DWORDStats(GEngineStats.STATS_Canvas_BeginPrimitive));
	// figure out which batch to use
	INT newBatchIdx = -1;
	INT recycleBatchIdx = -1;
	if (Batches.Num() > 0)
	{
		// start at the top and try to find a match
		for (BatchIdx = Batches.Num()-1; BatchIdx >= 0 && newBatchIdx == -1; BatchIdx--)
		{
			// if this is a valid batch,
			if (Batches(BatchIdx).Material != NULL &&
				Batches(BatchIdx).NumPrimitives > 0)
			{
				// if it's a match, then use this index
				if (Batches(BatchIdx).Material == material &&
					Batches(BatchIdx).PrimitiveType == primType &&
					Batches(BatchIdx).RenderStyle == renderStyle)
				{
					// set the new batch index for use
					newBatchIdx = BatchIdx;
				}
				else
				// otherwise check if this is overlapping and there is still a batch beneath
				if (Batches(BatchIdx).Bounds.Intersect(tileBounds))
				{
					// failed the quick test, now try a more accurate check,
					TArray<FBox> &bounds = Batches(BatchIdx).BoundingBoxes;
					// do a check against each individual tile in the batch, for accurate testing
					for (INT boundsIdx = 0; boundsIdx < bounds.Num(); boundsIdx++)
					{
						if (bounds(boundsIdx).Intersect(tileBounds))
						{
							// has an absolute overlap, so set the new index to the end of the list
							// and have it create a new batch
							//TODO: would an insert be more efficient as far as future batches are concerned?
							newBatchIdx = Batches.Num();
							// and skip anymore bounds checks
							break;
						}
					}
				}
			}
			else
			{
				// mark this index as recyclable
				recycleBatchIdx = BatchIdx;
			}
		}
	}
	// see if we need to create a new batch
	if (newBatchIdx < 0 ||
		newBatchIdx >= Batches.Num())
	{
		STAT_INC(GStats.DWORDStats(GEngineStats.STATS_Canvas_NumBatches));
		// recycle and index if available, otherwise append a new batch
		BatchIdx = recycleBatchIdx == -1 ? Batches.AddZeroed() : recycleBatchIdx;
		// fill in the info for this batch
		Batches(BatchIdx).Material = material;
		Batches(BatchIdx).PrimitiveType = primType;
		Batches(BatchIdx).RenderStyle = renderStyle;
	}
	else
	{
		BatchIdx = newBatchIdx;
	}
	// add to list of individual bounds for this batch
	Batches(BatchIdx).BoundingBoxes.AddItem(tileBounds);
	// and add to the overall bounds for this batch
	Batches(BatchIdx).Bounds += tileBounds;
	unguard;
}

/* epic ===============================================
* ::DrawTile
* Author: rayd
*
* Draws a texture to the screen, modified to support
* batching.
*
* =====================================================
*/
void FCanvasUtil::DrawTile(FLOAT X1,FLOAT Y1,FLOAT X2,FLOAT Y2,
	FLOAT U1,FLOAT V1,FLOAT U2,FLOAT V2,FLOAT Z,UMaterial* Material,
	FColor Color,BYTE RenderStyle,UBOOL bFlipUVsHoriz,UBOOL bFlipUVsVert,
	FMatrix* pTransformMat )
{
	guard(FCanvasUtil::DrawTile);

#ifdef _XBOX
	EPrimitiveType primType = PT_QuadList;
#else
	EPrimitiveType primType = PT_TriangleList;
#endif

	// scion sz - apply transform if available
	FVector TopLeft(X1,Y1,Z), 
			TopRight(X2,Y1,Z),
			BotLeft(X1,Y2,Z),
			BotRight(X2,Y2,Z);		
	if( pTransformMat )
	{
		TopLeft = pTransformMat->TransformFVector( TopLeft );
		TopRight = pTransformMat->TransformFVector( TopRight );
		BotLeft = pTransformMat->TransformFVector( BotLeft );
		BotRight = pTransformMat->TransformFVector( BotRight );
	}
	// figure out the bounds for this primitive
	FBox tileBounds(TopLeft,BotRight);
	// grab our batch index
	BeginPrimitive(primType,Material,RenderStyle,tileBounds);
	// get the real render color
	Color = Color.RenderColor();
	// figure out the texture dimensions
	INT		TextureWidth = Material ? Material->MaterialUSize() : 1,
			TextureHeight = Material? Material->MaterialVSize() : 1;
	FLOAT	InvTextureSizeX = 1.0f / TextureWidth,
			InvTextureSizeY = 1.0f / TextureHeight;
	// Handle flipping the UVs
	FLOAT UVX1 = bFlipUVsHoriz ? 1.f - (U1 * InvTextureSizeX) : (U1 * InvTextureSizeX);
	FLOAT UVX2 = bFlipUVsHoriz ? 1.f - (U2 * InvTextureSizeX) : (U2 * InvTextureSizeX);
	FLOAT UVY1 = bFlipUVsVert ? 1.f - (V1 * InvTextureSizeY) : (V1 * InvTextureSizeY);
	FLOAT UVY2 = bFlipUVsVert ? 1.f - (V2 * InvTextureSizeY) : (V2 * InvTextureSizeY);
	// point to the proper set of vertices in the batch
	TArray<FCanvasVertex> &Vertices = Batches(BatchIdx).Vertices;
#ifdef _XBOX
	//scion jg -- Use quad lists as they are smaller, less to copy
	new(Vertices) FCanvasVertex(TopLeft,Color,UVX1,UVY1);
	new(Vertices) FCanvasVertex(TopRight,Color,UVX2,UVY1);
	new(Vertices) FCanvasVertex(BotRight,Color,UVX2,UVY2);
	new(Vertices) FCanvasVertex(BotLeft,Color,UVX1,UVY2);

	Batches(BatchIdx).NumPrimitives += 1;
	STAT_INC(GStats.DWORDStats(GEngineStats.STATS_Canvas_NumPrimitives));
#else
	new(Vertices) FCanvasVertex(TopLeft,Color,UVX1,UVY1);
	new(Vertices) FCanvasVertex(TopRight,Color,UVX2,UVY1);
	new(Vertices) FCanvasVertex(BotRight,Color,UVX2,UVY2);

	new(Vertices) FCanvasVertex(TopLeft,Color,UVX1,UVY1);
	new(Vertices) FCanvasVertex(BotRight,Color,UVX2,UVY2);
	new(Vertices) FCanvasVertex(BotLeft,Color,UVX1,UVY2);

	Batches(BatchIdx).NumPrimitives += 2;
	STAT_INC_BY(GStats.DWORDStats(GEngineStats.STATS_Canvas_NumPrimitives),2);

	// force a flush for the editor (needed for hit detection to function properly)
	if (GIsEditor)
	{
		Flush();
	}
#endif

	unguard;
}

//
//	FCanvasUtil::DrawString
//

INT FCanvasUtil::DrawString(INT StartX,INT StartY,const TCHAR* Text,UFont* Font,FColor Color)
{
	guard(FCanvasUtil::DrawString);

	Color = Color.RenderColor();

	// Draw all characters in string.
	INT LineX = 0;
	INT bDrawUnderline = 0;
	INT UnderlineWidth = 0;
	for( INT i=0; Text[i]; i++ )
	{
		INT bUnderlineNext = 0;
		INT Ch = (TCHARU)Font->RemapChar(Text[i]);

		// Handle ampersand underlining.
		if( bDrawUnderline )
			Ch = (TCHARU)Font->RemapChar('_');
		if( Text[i]=='&' )
		{
			if( !Text[i+1] )
				break;
			if( Text[i+1]!='&' )
			{
				bUnderlineNext = 1;
				Ch = (TCHARU)Font->RemapChar(Text[i+1]);
			}
		}

		// Process character if it's valid.
		if( Ch < Font->Characters.Num() )
		{
			FFontCharacter& Char = Font->Characters(Ch);
		UTexture* Tex;
			if( Char.TextureIndex < Font->Textures.Num() && (Tex=Font->Textures(Char.TextureIndex))!=NULL )
		{
				// Compute character width.
				INT CharWidth;
				if( bDrawUnderline )
					CharWidth = Min(UnderlineWidth, Char.USize);
				else
					CharWidth = Char.USize;

				// Prepare for clipping.
				INT X      = LineX + StartX;
				INT Y      = StartY;
				INT CU     = Char.StartU;
				INT CV     = Char.StartV;
				INT CUSize = CharWidth;
				INT CVSize = Char.VSize;

				// Draw.
				DrawTile(X,Y,X + CUSize,Y + CVSize,CU,CV,CU + CUSize,CV + CVSize,1.0f/FAR_CLIPPING_PLANE,Tex,Color);

				// Update underline status.
				if( bDrawUnderline )
					CharWidth = UnderlineWidth;

				if( !bUnderlineNext )
					LineX += (INT) (CharWidth);
				else
					UnderlineWidth = Char.USize;

				bDrawUnderline = bUnderlineNext;
			}
		}
	}

	return LineX;

	unguard;
}

//
//  FCanvasUtil::GetRevision
//

INT FCanvasUtil::GetRevision()
{
	return 1;
}

//
//  FCanvasUtil::GetSize
//

INT FCanvasUtil::GetSize()
{
	return Batches(BatchIdx).Vertices.Num() * sizeof(FCanvasVertex);
}

//
//  FCanvasUtil::GetStride
//

INT FCanvasUtil::GetStride()
{
	return sizeof(FCanvasVertex);
}

//
//  FCanvasUtil::GetComponents
//

INT FCanvasUtil::GetComponents(FVertexComponent* OutComponents)
{
	OutComponents[0].Type = CT_Float3;
	OutComponents[0].Function = FVF_Position;
	OutComponents[1].Type = CT_Color;
	OutComponents[1].Function = FVF_Diffuse;
	OutComponents[2].Type = CT_Float2;
	OutComponents[2].Function = FVF_TexCoord0;

	return 3;
}

//
//  FCanvasUtil::GetStreamData
//

void FCanvasUtil::GetStreamData(void* Dest)
{
	appMemcpy(Dest,&(Batches(BatchIdx).Vertices(0)),Batches(BatchIdx).Vertices.Num() * sizeof(FCanvasVertex));
}

//
//  FCanvasUtil::GetRawStreamData
//

void FCanvasUtil::GetRawStreamData(void ** Dest, INT FirstVertex )
{
	*Dest = &(Batches(BatchIdx).Vertices(FirstVertex));
}

FScreenPrimUtil::FScreenPrimUtil(UViewport* Viewport, FRenderInterface* InRI) :
	SizeX(0), SizeY(0)
{
	guard( FScreenPrimUtil::FScreenPrimUtil );

	Material = NULL;
	NumPrimitives = 0;
	NumVerts = 0;

	// jg -- Be smarter about updating this matrix for speed
	Update(Viewport->SizeX,Viewport->SizeY,InRI);

#ifdef _XBOX
	PrimitiveType = PT_QuadList;
#else
	PrimitiveType = PT_TriangleStrip;
#endif

	unguard;
}

FScreenPrimUtil::FScreenPrimUtil(FRenderTarget* RenderTarget,FRenderInterface* InRI) :
	SizeX(0), SizeY(0)
{
	guard(FScreenPrimUtil::FScreenPrimUtil);

	Material = NULL;
	NumPrimitives = 0;
	NumVerts = 0;

	// jg -- Be smarter about updating this matrix for speed
	Update(RenderTarget->GetWidth(),RenderTarget->GetHeight(),InRI);

#ifdef _XBOX
	PrimitiveType = PT_QuadList;
#else
	PrimitiveType = PT_TriangleStrip;
#endif

	unguard;
}

// jg -- Empty constructor just zero. This is for globally reused screenprimutils
FScreenPrimUtil::FScreenPrimUtil(void) :
	SizeX(0), SizeY(0), RI(NULL), Material(NULL), NumPrimitives(0), NumVerts(0)
{
	guard(FScreenPrimUtil::FScreenPrimUtil);

#ifdef _XBOX
	PrimitiveType = PT_QuadList;
#else
	PrimitiveType = PT_TriangleStrip;
#endif

	unguard;
}

//
//  FScreenPrimUtil::~FScreenPrimUtil
//

FScreenPrimUtil::~FScreenPrimUtil()
{
	Flush();
}

//
//  FScreenPrimUtil::Flush
//

void FScreenPrimUtil::Flush()
{
	guard(FScreenPrimUtil::Flush);

	if(NumPrimitives > 0)
	{
		RI->PushState();

		RI->SetDistanceFog( 0, 0, 0, FColor(0,0,0) );
		RI->SetTransform(TT_LocalToWorld,FMatrix::Identity);
		RI->SetTransform(TT_WorldToCamera,FMatrix::Identity);
		RI->SetTransform(TT_CameraToScreen,CanvasToScreen);

//		DWORD ShaderType = RI->GetMaterialVertexShaderType(Material);

		RI->EnableLighting(0,1);
		RI->SetMaterial(Material);
		RI->SetIndexBuffer(NULL,0);

		RI->SetCullMode(CM_None);

#ifdef _XBOX
		BEGIN_D3D_EVENT(BDE)(PIX_SCREEN_PRIM,TEXT("FScreenPrimUtil"));
		//scion jg -- Copy the verts directly into the push buffer
		RI->DrawPrimitiveUP(0,PrimitiveType,NumPrimitives,this);
		END_D3D_EVENT;
#else
		INT	BaseVertexIndex = RI->SetDynamicStream(0,this);
		RI->DrawPrimitive(PrimitiveType,BaseVertexIndex,NumPrimitives,0,NumVerts);
#endif

		RI->PopState();

		NumVerts = 0;
		NumPrimitives = 0;
	}

	unguard;
}

//
//  FScreenPrimUtil::BeginPrimitive
//

void FScreenPrimUtil::BeginPrimitive(EPrimitiveType InType,UMaterial* InMaterial)
{
	guard(FScreenPrimUtil::BeginPrimitive);

	if(PrimitiveType != InType || Material != InMaterial )
	{
		Flush();
		PrimitiveType = InType;
		Material = InMaterial;
	}

	unguard;
}

//
//  FScreenPrimUtil::DrawTile
//

void FScreenPrimUtil::DrawTile(FLOAT X1,FLOAT Y1,FLOAT X2,FLOAT Y2,FLOAT U1[4],FLOAT V1[4],FLOAT U2[4],FLOAT V2[4],FLOAT Z,UMaterial* Material,FColor Color)
{
	guard(FScreenPrimUtil::DrawTile);

	Color = Color.RenderColor();

#ifdef _XBOX
	BeginPrimitive(PT_QuadList,Material);
	//jg -- Now that we've switched to a fixed number of items make sure
	// we won't overwrite memory
	if (NumVerts + 4 >= ARRAY_COUNT(Vertices))
	{
		Flush();
	}
	Vertices[NumVerts++].SetVertInfo(X1,Y1,Z,Color,U1,V1);
	Vertices[NumVerts++].SetVertInfo(X2,Y1,Z,Color,U2,V1);
	Vertices[NumVerts++].SetVertInfo(X2,Y2,Z,Color,U2,V2);
	Vertices[NumVerts++].SetVertInfo(X1,Y2,Z,Color,U1,V2);
	NumPrimitives += 1;

#else
	BeginPrimitive(PT_TriangleStrip,Material);
	//jg -- Now that we've switched to a fixed number of items make sure
	// we won't overwrite memory
	if (NumVerts + 4 >= ARRAY_COUNT(Vertices))
	{
		Flush();
	}
	Vertices[NumVerts++].SetVertInfo(X1,Y1,Z,Color,U1,V1);
	Vertices[NumVerts++].SetVertInfo(X2,Y1,Z,Color,U2,V1);
	Vertices[NumVerts++].SetVertInfo(X2,Y2,Z,Color,U2,V2);
	Vertices[NumVerts++].SetVertInfo(X1,Y2,Z,Color,U1,V2);
	NumPrimitives += 2;
#endif

	unguard;
}

//
//  FScreenPrimUtil::GetComponents
//

INT FScreenPrimUtil::GetComponents(FVertexComponent* OutComponents)
{
	INT NumComponents=0;

	OutComponents[0].Type = CT_Float3;
	OutComponents[0].Function = FVF_Position;
	NumComponents++;

	OutComponents[1].Type = CT_Color;
	OutComponents[1].Function = FVF_Diffuse;
	NumComponents++;

	OutComponents[2].Type = CT_Float2;
	OutComponents[2].Function = FVF_TexCoord0;
	NumComponents++;

	OutComponents[3].Type = CT_Float2;
	OutComponents[3].Function = FVF_TexCoord1;
	NumComponents++;

	OutComponents[4].Type = CT_Float2;
	OutComponents[4].Function = FVF_TexCoord2;
	NumComponents++;

	OutComponents[5].Type = CT_Float2;
	OutComponents[5].Function = FVF_TexCoord3;
	NumComponents++;

	return NumComponents;
}

//
//  FScreenPrimUtil::GetStreamData
//

void FScreenPrimUtil::GetStreamData(void* Dest)
{
	appMemcpy(Dest,Vertices,(NumVerts + 1) * sizeof(FScreenPrimVertex));
}

//
//  FScreenPrimUtil::GetRawStreamData
//

void FScreenPrimUtil::GetRawStreamData(void ** Dest, INT FirstVertex )
{
	check(FirstVertex < ARRAY_COUNT(Vertices));
	*Dest = &Vertices[FirstVertex];
}


//
//  FRawIndexBuffer::FRawIndexBuffer
//

FRawIndexBuffer::FRawIndexBuffer()
{
#ifdef _XBOX
	bIsUMA = 1;
#endif
	Revision = 0;
}

/* scion ======================================================================
 * FRawIndexBuffer::Stripify
 * Author: jg
 * 
 * Builds an indexed triangle strip from the existing index data. It can build
 * for optimal cache coherency or optimal index usage. If you are GPU bound,
 * then optimal cache coherency is the way to go. If you need the memory
 * reduction, then go with optimal index generation.
 *
 * input:	bCacheOptimize -- True optimizes for cache coherency. False for
 *			fewer indices
 *			bTriStrip -- Whether to generate strips or lists
 *
 * returns:	The number of primitives indexed
 *
 * ============================================================================
 */
INT FRawIndexBuffer::Stripify(UBOOL bCacheOptimize,UBOOL bTriStrip)
{
	guard(FRawIndexBuffer::Stripify);
#ifndef _XBOX
	// Returns how many indices come from the stripping process
	DWORD dwIndexCount = 0;
	// Returns the new list of indices for the
	_WORD* pwIndices = NULL;
	// Figure if we want cache coherency or memory savings
	DWORD dwFlags = bCacheOptimize ? OPTIMIZE_FOR_CACHE : OPTIMIZE_FOR_INDICES;
	// Whether to make strips or lists
	dwFlags |= bTriStrip ? OUTPUT_TRISTRIP : OUTPUT_TRILIST;
	// Use the xbox tristripper to generate a tristrip/list
	::Stripify(Indices.Num() / 3,&Indices(0),&dwIndexCount,&pwIndices,dwFlags);
	// Init the indices array
	Indices.Empty(dwIndexCount);
	Indices.Add(dwIndexCount);
	// Copy the new indices over
	appMemcpy(&Indices(0),pwIndices,dwIndexCount * sizeof(_WORD));
	// Don't leak the generated indices
	delete [] pwIndices;
	Revision++;
	// Return the number of primitives
	return bTriStrip ? dwIndexCount - 2 : dwIndexCount / 3;
#else
	return 0;
#endif
	unguard;
}

//
//  FRawIndexBuffer operator<<
//

FArchive& operator<<(FArchive& Ar,FRawIndexBuffer& I)
{
	return Ar << I.Indices << I.Revision;
}

//
//  FRawIndexBuffer::GetRevision
//

INT FRawIndexBuffer::GetRevision()
{
	return Revision;
}

//
//  FRawIndexBuffer::GetSize
//

INT FRawIndexBuffer::GetSize()
{
	return Indices.Num() * sizeof(_WORD);
}

//
//  FRawIndexBuffer::GetIndexSize
//

INT FRawIndexBuffer::GetIndexSize()
{
	return sizeof(_WORD);
}

//
//  FRawIndexBuffer::GetContents
//

void FRawIndexBuffer::GetContents(void* Data)
{
	appMemcpy(Data,&Indices(0),Indices.Num() * sizeof(_WORD));
}

/* scion ======================================================================
 * FRawIndexBuffer::GetContentsPtr
 * Author: jg
 *
 * Returns a direct pointer to the index data. It is used with UMA support on
 * the xbox. Note that index buffers aren't placed into contiguous memory since
 * they are copied to the push buffer with each draw command and contiguous
 * memory is slower to read from for the CPU.
 *
 * ============================================================================
 */
void* FRawIndexBuffer::GetContentsPtr(void)
{
	return Indices.Num() ? &Indices(0) : NULL;
}

//
//  FRaw32BitIndexBuffer::FRaw32BitIndexBuffer
//

FRaw32BitIndexBuffer::FRaw32BitIndexBuffer()
{
	Revision = 0;
}

//
//  FRaw32BitIndexBuffer operator<<
//

FArchive& operator<<(FArchive& Ar,FRaw32BitIndexBuffer& I)
{
	return Ar << I.Indices << I.Revision;
}

//
//  FRaw32BitIndexBuffer::GetRevision
//

INT FRaw32BitIndexBuffer::GetRevision()
{
	return Revision;
}

//
//  FRaw32BitIndexBuffer::GetSize
//

INT FRaw32BitIndexBuffer::GetSize()
{
	return Indices.Num() * sizeof(DWORD);
}

//
//  FRaw32BitIndexBuffer::GetIndexSize
//

INT FRaw32BitIndexBuffer::GetIndexSize()
{
	return sizeof(DWORD);
}

//
//  FRaw32BitIndexBuffer::GetContents
//

void FRaw32BitIndexBuffer::GetContents(void* Data)
{
	appMemcpy(Data,&Indices(0),Indices.Num() * sizeof(DWORD));
}

//
//  FRawColorStream::FRawColorStream
//

FRawColorStream::FRawColorStream()
{
	Revision = 0;
#ifdef _XBOX
	//scion jg -- UMA support for color streams
	bIsUMA = 1;
#endif
}

//
//  FRawColorStream operator<<
//

FArchive& operator<<(FArchive& Ar,FRawColorStream& ColorStream)
{
	return Ar	<< ColorStream.Colors
				<< ColorStream.Revision;
}

//
//  FRawColorStream::GetRevision
//

INT FRawColorStream::GetRevision()
{
	return Revision;
}

//
//  FRawColorStream::GetSize
//

INT FRawColorStream::GetSize()
{
	return Colors.Num() * sizeof(FColor);
}

//
//  FRawColorStream::GetStride
//

INT FRawColorStream::GetStride()
{
	return sizeof(FColor);
}

//
//  FRawColorStream::GetComponents
//

INT FRawColorStream::GetComponents(FVertexComponent* OutComponents)
{
	OutComponents[0].Type = CT_Color;
	OutComponents[0].Function = FVF_Diffuse;

	return 1;
}

//
//  FRawColorStream::GetStreamData
//

void FRawColorStream::GetStreamData(void* Dest)
{
	appMemcpy(Dest,&Colors(0),Colors.Num() * sizeof(FColor));
}

//
//  FRawColorStream::GetRawStreamData
//

void FRawColorStream::GetRawStreamData(void ** Dest, INT FirstVertex )
{
	*Dest = &Colors(FirstVertex);
}

//
//	FSolidColorTexture::FSolidColorTexture
//

FSolidColorTexture::FSolidColorTexture(FColor InColor)
{
	Color = InColor;

	Revision = 1;
}

//
//	FSolidColorTexture::GetRevision
//

INT FSolidColorTexture::GetRevision()
{
	return Revision;
}

//
//	FSolidColorTexture::GetWidth
//

INT FSolidColorTexture::GetWidth()
{
	return 1;
}

//
//	FSolidColorTexture::GetHeight
//

INT FSolidColorTexture::GetHeight()
{
	return 1;
}

//
//	FSolidColorTexture::GetUClamp
//

ETexClampMode FSolidColorTexture::GetUClamp()
{
	return TC_Wrap;
}

//
//	FSolidColorTexture::GetVClamp
//

ETexClampMode FSolidColorTexture::GetVClamp()
{
	return TC_Wrap;
}

//
//	FSolidColorTexture::GetFormat
//

ETextureFormat FSolidColorTexture::GetFormat()
{
	return TEXF_RGBA8;
}

//
//	FSolidColorTexture::GetNumMips
//

INT FSolidColorTexture::GetNumMips()
{
	return 1;
}

//
//	FSolidColorTexture::GetFirstMip
//

INT FSolidColorTexture::GetFirstMip()
{
	return 0;
}

//
//	FSolidColorTexture::GetTextureData
//

void FSolidColorTexture::GetTextureData(INT MipIndex,void* Dest,INT DestStride,ETextureFormat DestFormat,UBOOL ColoredMips)
{
	guard(FSolidColorTexture::GetTextureData);

	check(MipIndex == 0);
	check(DestFormat == TEXF_RGBA8);

	*((FColor*) Dest) = Color;

	unguard;
}

//
//	FSolidColorTexture::GetRawTextureData
//

void* FSolidColorTexture::GetRawTextureData(INT MipIndex)
{
	return &Color;
}

//
//	FSolidColorTexture::GetUTexture
//

UTexture* FSolidColorTexture::GetUTexture()
{
	return NULL;
}

//
//	FAuxRenderTarget::FAuxRenderTarget
//

FAuxRenderTarget::FAuxRenderTarget(INT InWidth,INT InHeight,ETextureFormat InFormat,INT InCreationFlags)
{
	Width = InWidth;
	Height = InHeight;
	Format = InFormat;
	CreationFlags = InCreationFlags;

	Revision = 1;
	//scion jg -- Make sure these default to not cached
	CacheIdColor = CacheIdDepth = INVALID_CACHEID;
}

//
//	FAuxRenderTarget::~FAuxRenderTarget
//

FAuxRenderTarget::~FAuxRenderTarget()
{
	guard(FAuxRenderTarget::~FAuxRenderTarget);
	if( UTexture::__Client &&
		UTexture::__Client->Engine &&
		UTexture::__Client->Engine->GRenDev	)
	{
		//scion jg -- Changed to reflect new resource caching
		if (CacheIdColor != INVALID_CACHEID)
		{
			UTexture::__Client->Engine->GRenDev->FlushResource( CacheIdColor, CH_Texture );
		}
		if (CacheIdDepth != INVALID_CACHEID)
		{
			UTexture::__Client->Engine->GRenDev->FlushResource( CacheIdDepth, CH_Texture );
		}
	}
	unguard;
}

//
//	FAuxRenderTarget::GetRevision
//

INT FAuxRenderTarget::GetRevision()
{
	return Revision;
}

//
//	FAuxRenderTarget::GetCacheId
//

_WORD FAuxRenderTarget::GetCacheId()
{
	if( MaterialUsageFlag == FRenderTarget::RT_MATERIAL_USE_BACKBUFFER )
	{
		return CacheIdColor;
	}
	else
	{
        return CacheIdDepth;
	}
}

void FAuxRenderTarget::SetCacheId(_WORD NewCacheID,URenderDevice* pRenDev)
{
	if( MaterialUsageFlag == FRenderTarget::RT_MATERIAL_USE_BACKBUFFER )
	{
		CacheIdColor = NewCacheID;
	}
	else
	{
        CacheIdDepth = NewCacheID;
	}
	// Copy the pointer for later clean up
	m_pRenDev = pRenDev;
}

//
//	FAuxRenderTarget::GetWidth
//

INT FAuxRenderTarget::GetWidth()
{
	return Width;
}

//
//	FAuxRenderTarget::GetHeight
//

INT FAuxRenderTarget::GetHeight()
{
	return Height;
}

//
//	FAuxRenderTarget::GetUClamp
//

ETexClampMode FAuxRenderTarget::GetUClamp()
{
	return TC_Clamp;
}

//
//	FAuxRenderTarget::GetVClamp
//

ETexClampMode FAuxRenderTarget::GetVClamp()
{
	return TC_Clamp;
}

//
//	FAuxRenderTarget::GetFormat
//

ETextureFormat FAuxRenderTarget::GetFormat()
{
	return Format;
}

//
//	FAuxRenderTarget::GetNumMips
//

INT FAuxRenderTarget::GetNumMips()
{
	return 1;
}

//
//	FAuxRenderTarget::GetFirstMip
//

INT FAuxRenderTarget::GetFirstMip()
{
	return 0;
}


/* scion ======================================================================
 * CalculateVertexCacheHits
 * Author: jg
 *
 * Stolen from a MS sample, this code checks for T&L cache coherency, the number
 * of degenerate triangles in the mesh, and the number of memory pages that are
 * read from (data cache misses).
 *
 * input:	bIsTriStrip UBOOL	Whether it is a tristrip or trilist
 *			dwVertexSize DWORD	The size of the vertex in the stream for data
 *								cache efficiency calculations
 *			pIndices _WORD*		Pointer to a chunk of memory containing the
 *								index data
 *			dwNumIndices DWORD	How large that chunk of memory is in WORDs
 *			pdwNumDegenTris DWORD* Out value for the degenerate count
 *			pdwNumCacheMisses DWORD* Out value for the cache miss count
 *			pdwNumPagesCrossed DWORD* Out value for the data cache miss count
 *
 * ============================================================================
 */
void ENGINE_API CalculateVertexCacheHits(UBOOL bIsTriStrip, DWORD dwVertexSize,
	_WORD* pIndices, DWORD dwNumIndices, DWORD* pdwNumDegenTris,
	DWORD* pdwNumCacheMisses, DWORD* pdwNumPagesCrossed)
{
	// Check arguments
	check(NULL != pdwNumDegenTris && NULL != pdwNumCacheMisses &&
		NULL != pdwNumPagesCrossed);

	// Initialize results
	(*pdwNumDegenTris) = 0;
	(*pdwNumCacheMisses)    = 0;
	(*pdwNumPagesCrossed)   = 1;

	// Simulate a vertex cache
	static const int CACHE_SIZE = 18;
	static const int PAGE_SIZE  = 4096;
	DWORD rgdwCache[CACHE_SIZE];
	INT   iCachePtr      = 0;
	DWORD dwLastPageAddr = 0;
	appMemset( rgdwCache, 0xff, sizeof(rgdwCache) );

	// Run all vertices through the sumilated vertex cache, tallying cache hits,
	// degenerate triangles, and pages crossed.
	for( DWORD i = 0; i < dwNumIndices; i++ )
	{
		// This makes all kinds of assumptions such as page size is 4k,
		// page across then back is ok, etc etc. Seems to be an ok
		// estimate on data locality though.
		DWORD dwPage = dwVertexSize * pIndices[i] / PAGE_SIZE;

		if( ( dwPage > dwLastPageAddr ) || ( dwPage+1 < dwLastPageAddr ) )
		{
			(*pdwNumPagesCrossed)++;
			dwLastPageAddr = dwPage;
		}

		// Update our count of degenerate tris
		if( bIsTriStrip && (i > 1) )
			if( ( pIndices[i-0] == pIndices[i-1] ) ||
				( pIndices[i-0] == pIndices[i-2] ) ||
				( pIndices[i-1] == pIndices[i-2] ) )
				(*pdwNumDegenTris)++;

		// Check to see if the vertex would be in the cache
		UBOOL bVertexInCache = 0;
		for( int cache_index = 0; cache_index < CACHE_SIZE; cache_index++ )
		{
			if( pIndices[i] == rgdwCache[cache_index] )
			{
				bVertexInCache = 1;
				break;
			}
		}

		if( bVertexInCache )
		{
			// Do nothing
		}
		else
		{
			// Keep track of cache misses
			(*pdwNumCacheMisses)++;

			// Add vertex to simulated cache
			rgdwCache[iCachePtr] = pIndices[i];
			iCachePtr = (iCachePtr + 1) % CACHE_SIZE;
		}
	}
}

/* scion ======================================================================
* FMultiSampleFilter::GenerateMultiSampleFilter
* Author: sz
*
* Usage:
*
* Inputs:
* ============================================================================
*/
void ENGINE_API FMultiSampleFilter::GenerateMultiSampleFilter( EMultiSampleType SampleType, FLOAT Scale )
{
	guard(FMultiSampleFilter::GenerateMultiSampleFilter);

	const INT NumPasses = 1;
	
	if( FilterList.Num() < NumPasses )
		FilterList.Add();

	if( FilterList.Num() > NumPasses )
		FilterList.Remove( NumPasses, FilterList.Num()-NumPasses );

	FFourSampleFilter& Filter = FilterList(0);

	switch( SampleType )
	{
	case FMultiSampleFilter::MULTI_SAMPLE_BOX:
		{
			// sample 0
			Filter.SamplePoints[0].OffsetU = -1.0f*Scale;
			Filter.SamplePoints[0].OffsetV = -1.0f*Scale;
			Filter.SamplePoints[0].Weight = 0.25f;

			// sample 1
			Filter.SamplePoints[1].OffsetU = 1.0f*Scale;
			Filter.SamplePoints[1].OffsetV = -1.0f*Scale;
			Filter.SamplePoints[1].Weight = 0.25f;

			// sample 2
			Filter.SamplePoints[2].OffsetU = -1.0f*Scale;
			Filter.SamplePoints[2].OffsetV = 1.0f*Scale;
			Filter.SamplePoints[2].Weight = 0.25f;

			// sample 3
			Filter.SamplePoints[3].OffsetU = 1.0f*Scale;
			Filter.SamplePoints[3].OffsetV = 1.0f*Scale;
			Filter.SamplePoints[3].Weight = 0.25f;
		}
		break;
	case FMultiSampleFilter::MULTI_SAMPLE_HORIZ_1221:
		{
			// sample 0
			Filter.SamplePoints[0].OffsetU = 3.0f*Scale;
			Filter.SamplePoints[0].OffsetV = 0.0f*Scale;
			Filter.SamplePoints[0].Weight = 1.0f/6.0f;

			// sample 1
			Filter.SamplePoints[1].OffsetU = 1.0f*Scale;
			Filter.SamplePoints[1].OffsetV = 0.0f*Scale;
			Filter.SamplePoints[1].Weight = 2.0f/6.0f;

			// sample 2
			Filter.SamplePoints[2].OffsetU = -1.0f*Scale;
			Filter.SamplePoints[2].OffsetV = 0.0f*Scale;
			Filter.SamplePoints[2].Weight = 2.0f/6.0f;

			// sample 3
			Filter.SamplePoints[3].OffsetU = -3.0f*Scale;
			Filter.SamplePoints[3].OffsetV = 0.0f*Scale;
			Filter.SamplePoints[3].Weight = 1.0f/6.0f;
				
		}
		break;
	case FMultiSampleFilter::MULTI_SAMPLE_VERT_1221:
		{
			// sample 0
			Filter.SamplePoints[0].OffsetU = 0.0f*Scale;
			Filter.SamplePoints[0].OffsetV = 3.0f*Scale;
			Filter.SamplePoints[0].Weight = 1.0f/6.0f;

			// sample 1
			Filter.SamplePoints[1].OffsetU = 0.0f*Scale;
			Filter.SamplePoints[1].OffsetV = 1.0f*Scale;
			Filter.SamplePoints[1].Weight = 2.0f/6.0f;

			// sample 2
			Filter.SamplePoints[2].OffsetU = 0.0f*Scale;
			Filter.SamplePoints[2].OffsetV = -1.0f*Scale;
			Filter.SamplePoints[2].Weight = 2.0f/6.0f;

			// sample 3
			Filter.SamplePoints[3].OffsetU = 0.0f*Scale;
			Filter.SamplePoints[3].OffsetV = -3.0f*Scale;
			Filter.SamplePoints[3].Weight = 1.0f/6.0f;
		}
		break;
	case FMultiSampleFilter::MULTI_SAMPLE_HORIZ_1331:
		{
			// sample 0
			Filter.SamplePoints[0].OffsetU = 3.0f*Scale;
			Filter.SamplePoints[0].OffsetV = 0.0f*Scale;
			Filter.SamplePoints[0].Weight = 1.0f/8.0f;

			// sample 1
			Filter.SamplePoints[1].OffsetU = 1.0f*Scale;
			Filter.SamplePoints[1].OffsetV = 0.0f*Scale;
			Filter.SamplePoints[1].Weight = 3.0f/8.0f;

			// sample 2
			Filter.SamplePoints[2].OffsetU = -1.0f*Scale;
			Filter.SamplePoints[2].OffsetV = 0.0f*Scale;
			Filter.SamplePoints[2].Weight = 3.0f/8.0f;

			// sample 3
			Filter.SamplePoints[3].OffsetU = -3.0f*Scale;
			Filter.SamplePoints[3].OffsetV = 0.0f*Scale;
			Filter.SamplePoints[3].Weight = 1.0f/8.0f;
		}
		break;
	case FMultiSampleFilter::MULTI_SAMPLE_VERT_1331:
		{
			// sample 0
			Filter.SamplePoints[0].OffsetU = 0.0f*Scale;
			Filter.SamplePoints[0].OffsetV = 3.0f*Scale;
			Filter.SamplePoints[0].Weight = 1.0f/8.0f;

			// sample 1
			Filter.SamplePoints[1].OffsetU = 0.0f*Scale;
			Filter.SamplePoints[1].OffsetV = 1.0f*Scale;
			Filter.SamplePoints[1].Weight = 3.0f/8.0f;

			// sample 2
			Filter.SamplePoints[2].OffsetU = 0.0f*Scale;
			Filter.SamplePoints[2].OffsetV = -1.0f*Scale;
			Filter.SamplePoints[2].Weight = 3.0f/8.0f;

			// sample 3
			Filter.SamplePoints[3].OffsetU = 0.0f*Scale;
			Filter.SamplePoints[3].OffsetV = -3.0f*Scale;
			Filter.SamplePoints[3].Weight = 1.0f/8.0f;
		}
		break;
	default:
		FilterList.Empty();
		break;
	}

	unguard;
}

/* scion ======================================================================
* FMultiSampleFilter::GenerateStreakFilter
* Author: sz
*
* Usage:
*
* Inputs:
* ============================================================================
*/
void ENGINE_API FMultiSampleFilter::GenerateStreakFilter( EMultiSampleType InType, FLOAT Angle, FLOAT Scale )
{
	guard(FMultiSampleFilter::GenerateStreakFilter);

	static FLOAT Attenuation = 0.68f;
	const INT NumPasses = 1;

	if( FilterList.Num() < NumPasses )
		FilterList.Add();

	if( FilterList.Num() > NumPasses )
		FilterList.Remove( NumPasses, FilterList.Num()-NumPasses );

	FFourSampleFilter& Filter = FilterList(0);

	switch( InType )
	{
	case FMultiSampleFilter::STREAK_SAMPLE_HORIZ:
		{
			FLOAT ScaledOffset = appPow( 4, Max(Scale-1.f,0.f) );

			// sample 0
			Filter.SamplePoints[0].OffsetU = 1.0f*ScaledOffset;
			Filter.SamplePoints[0].OffsetV = 0.0f;
			Filter.SamplePoints[0].Weight = appPow( Attenuation, 2.0f*ScaledOffset );

			// sample 1
			Filter.SamplePoints[1].OffsetU = 3.0f*ScaledOffset;
			Filter.SamplePoints[1].OffsetV = 0.0f;
			Filter.SamplePoints[1].Weight = appPow( Attenuation, 6.0f*ScaledOffset );

			// sample 2
			Filter.SamplePoints[2].OffsetU = -3.0f*ScaledOffset;
			Filter.SamplePoints[2].OffsetV = 0.0f;
			Filter.SamplePoints[2].Weight = appPow( Attenuation, 6.0f*ScaledOffset );

			// sample 3
			Filter.SamplePoints[3].OffsetU = -1.0f*ScaledOffset;
			Filter.SamplePoints[3].OffsetV = 0.0f;
			Filter.SamplePoints[3].Weight = appPow( Attenuation, 2.0f*ScaledOffset );
		}
		break;
	default:
		check(0);
		break;
	}

	unguard;
}

/* scion ======================================================================
 * FilterCopyRenderTarget
 * Author: sz
 *
 * Usage:
 *	Blur contents of one render target (source) into another (target) using
 *	a sampling function of multiple passes.
 *
 * Inputs:
 *	DestTarget	- renders output results into this render target
 *	SrcTarget	- treated as a material for doing input render target
 *  Sampler		- multiple convolution kernels (4 samples allowed per pass)
 *
 * ============================================================================
 */
void ENGINE_API FilterCopyRenderTarget( FRenderTarget* DestTarget, FRenderTarget* SrcTarget, UViewport* SrcViewport, FMultiSampleFilter& Sampler,
									   FRenderInterface* RI, FColor* CopyColor, UFinalBlend* FinalBlend, DWORD BlurBlendMode )
{
	guard(FilterCopyRenderTarget);

	check( DestTarget && SrcTarget && RI );

	// create a proxy material for the source render target so that we can use it to render with
	DECLARE_STATIC_UOBJECT( UProxyBitmapMaterial, SrcMaterial, {});
	SrcMaterial->SetTextureInterface(SrcTarget);

	// create 4 transform matrices for the source material (one for each sample)
	DECLARE_STATIC_UOBJECT(	UTexMatrix,	TexOffset0, 
	{ 
		TexOffset0->Material = SrcMaterial; 
		TexOffset0->Matrix.SetIdentity();
	});
	DECLARE_STATIC_UOBJECT(	UTexMatrix,	TexOffset1, 
	{ 
		TexOffset1->Material = SrcMaterial; 
		TexOffset1->Matrix.SetIdentity();
	});
	DECLARE_STATIC_UOBJECT(	UTexMatrix,	TexOffset2, 
	{ 
		TexOffset2->Material = SrcMaterial; 
		TexOffset2->Matrix.SetIdentity();
	});
	DECLARE_STATIC_UOBJECT(	UTexMatrix,	TexOffset3, 
	{ 
		TexOffset3->Material = SrcMaterial; 
		TexOffset3->Matrix.SetIdentity();
	});

	// create a 4 sample blur pixel shader
	DECLARE_STATIC_UOBJECT( UPSBlurMaterial, BlurPixelShader,
	{
		BlurPixelShader->BlurSourceMaterial = SrcMaterial;
		BlurPixelShader->BlurSourceOffset0 = TexOffset0;
		BlurPixelShader->BlurSourceOffset1 = TexOffset1;
		BlurPixelShader->BlurSourceOffset2 = TexOffset2;
		BlurPixelShader->BlurSourceOffset3 = TexOffset3;
	});

	UMaterial* BlurSrcMaterial = BlurPixelShader;

	// allow for a final blend to be used with the src target blur
	if( FinalBlend )
	{
		FinalBlend->Material = BlurPixelShader;
		BlurSrcMaterial = FinalBlend;
	}

	FLOAT U1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	FLOAT V1[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	FLOAT U2[4] = {1.0f, 1.0f, 1.0f, 1.0f};
	FLOAT V2[4] = {1.0f, 1.0f, 1.0f, 1.0f};

	if( SrcViewport )
	{
		//URenderDevice* RenDev = SrcViewport->RenDev;

		INT FullScreenSizeX = SrcTarget->GetWidth();
		INT FullScreenSizeY = SrcTarget->GetHeight();	

		FLOAT OffsetX = (FLOAT)SrcViewport->OffsetX / (FLOAT)FullScreenSizeX;
		FLOAT OffsetY = (FLOAT)SrcViewport->OffsetY / (FLOAT)FullScreenSizeY;
		FLOAT SizeX = (FLOAT)SrcViewport->SizeX / (FLOAT)FullScreenSizeX * SrcViewport->RenDev->FSAAScale[0];
		FLOAT SizeY = (FLOAT)SrcViewport->SizeY / (FLOAT)FullScreenSizeY * SrcViewport->RenDev->FSAAScale[1];

		U1[0]=OffsetX; U1[1]=OffsetX; U1[2]=OffsetX; U1[3]=OffsetX;
		V1[0]=OffsetY; V1[1]=OffsetY; V1[2]=OffsetY; V1[3]=OffsetY;
		U2[0]=OffsetX+SizeX; U2[1]=OffsetX+SizeX; U2[2]=OffsetX+SizeX; U2[3]=OffsetX+SizeX;
		V2[0]=OffsetY+SizeY; V2[1]=OffsetY+SizeY; V2[2]=OffsetY+SizeY; V2[3]=OffsetY+SizeY;
	}	

	FLOAT OneTexelU = 1.0f / (FLOAT)SrcTarget->GetWidth();
	FLOAT OneTexelV = 1.0f / (FLOAT)SrcTarget->GetHeight();

	FScreenPrimUtil TextureCanvas( DestTarget, RI );
	RI->SetRenderTarget( DestTarget );

	for( INT Pass=0; Pass < Sampler.FilterList.Num(); Pass++ )
	{
		FFourSampleFilter& Filter = Sampler.FilterList(Pass);

		// set the blur pixel shader weight coefficients
		for( INT i=0; i<4; i++ )
		{
			BlurPixelShader->Weights[i].R = Filter.SamplePoints[i].Weight;
			BlurPixelShader->Weights[i].G = Filter.SamplePoints[i].Weight;
			BlurPixelShader->Weights[i].B = Filter.SamplePoints[i].Weight;
			BlurPixelShader->Weights[i].A = Filter.SamplePoints[i].Weight;
		}
		
		// tex0 offset
		TexOffset0->Matrix.M[2][0] = OneTexelU * Filter.SamplePoints[0].OffsetU;
		TexOffset0->Matrix.M[2][1] = OneTexelV * Filter.SamplePoints[0].OffsetV;		

		// tex1 offset
		TexOffset1->Matrix.M[2][0] = OneTexelU * Filter.SamplePoints[1].OffsetU;
		TexOffset1->Matrix.M[2][1] = OneTexelV * Filter.SamplePoints[1].OffsetV;

		// tex2 offset
		TexOffset2->Matrix.M[2][0] = OneTexelU * Filter.SamplePoints[2].OffsetU;
		TexOffset2->Matrix.M[2][1] = OneTexelV * Filter.SamplePoints[2].OffsetV;

		// tex3 offset
		TexOffset3->Matrix.M[2][0] = OneTexelU * Filter.SamplePoints[3].OffsetU;
		TexOffset3->Matrix.M[2][1] = OneTexelV * Filter.SamplePoints[3].OffsetV;		

		// draw full screen quad into DestTarget
		static FColor ColorWhite(255,255,255,255);

		// blur pixel shader should blend results by adding
		// to existing buffer.
		if( Pass == 0 )
			BlurPixelShader->BlurBlendMode = BlurBlendMode;
		else
			BlurPixelShader->BlurBlendMode = BLUR_ADD;

		TextureCanvas.DrawTile(
			0, 0, DestTarget->GetWidth(), DestTarget->GetHeight(),
			U1, V1,	U2, V2,
			0.0f,
			BlurSrcMaterial,
			(CopyColor) ? (*CopyColor) : ColorWhite			
		);
		TextureCanvas.Flush();
	}
	//scion jg -- Pop the render target change
	RI->ResetRenderTarget();

	unguard;
}
