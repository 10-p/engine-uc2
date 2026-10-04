/* scion ======================================================================
* Author: sz (updates)
*
* Original Author: Jon Rihan (jrihan@outhouse-productions.com)
* Last Updated: 02 February 2003
* ============================================================================
*
*	HISTORY:
*	v1.3 (scion)
*		- Added proper midpoint offset for bone spawning
*		- Use Quad list with push buffers for xbox
*		- created FUCRibbonVertexStream and FUCRibbonIndexBuffer to be consistent
*		  with other emitters
*		- SampleRate and DecayRate are defined in samples per second
*		- DecayRate is used only with bDecayPoints to specify how fast to
*		  decay the ribbon points once the emitter has been killed
*
*	v1.2
*		- fixed ribbon shaking when tweening so ribbon is now more geometrically
*		  stable on curved paths (only i=1 and last points actually move now, UVs are
*		  adjusted to compansate)
*		- SampleTimer is always set to 0.f after points are added/removed for better
*		  handling of very large DeltaTime updates
*		- fixed bug that randomly prevented ribbon to show up
*		- added some basic particle velocity support
*		- added bLengthBasedTextureU: when true, makes the texture U coord distrubution
*		  dependant on the ribbon's length to reduce texture warping
*
*	v1.1
*		- fixed start offset bug
*		- added bUseBoneDistance var to use distance between start/end
*		  bones as point width
*		- added NumSheets var for additional rotated sheets
*		- added bUseSheetScale and SheetScale array for scaling ribbon
*		  sheet widths individually
*		- added support for RotationOffset
*
*	v1.0
*		- initial version
*
*	NOTES:
*		- uses Particles(0).Size.X to scale the width of the ribbon
*
*
* ============================================================================
*/

#include "EnginePrivate.h"

IMPLEMENT_CLASS(UUCRibbonEmitter);

#define VerticesPerPoint		2
#define IndicesPerPoint			2

FLOAT FLinterp(FLOAT percent, FLOAT A, FLOAT B);
void VLinterp(FLOAT percent, const FVector& V1, const FVector& V2, FVector& Result);

class FUCRibbonVertex
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

class FUCRibbonVertexStream : public FParticleVertexStream<FUCRibbonVertex>
{
public:

	UUCRibbonEmitter*			Emitter;
	INT							NumVerts;
	FLevelSceneNode*			SceneNode;

	// Constructor.
	FUCRibbonVertexStream( UUCRibbonEmitter* InEmitter,INT InNumVerts, FLevelSceneNode* InSceneNode )
	{
		Emitter			= InEmitter;
		NumVerts		= InNumVerts;
		SceneNode		= InSceneNode;
	}

	virtual INT GetSize()
	{
		return NumVerts * sizeof(FUCRibbonVertex);
	}

	virtual void GetStreamData(void* Dest)
	{
#ifndef _XBOX
		void* VertexStream = Dest;
		for( INT Sheet=0; Sheet < Emitter->SheetsUsed; Sheet++ )
		{
			VertexStream = Emitter->FillVertexBuffer( (FUCRibbonVertex**) &VertexStream, SceneNode, Sheet );
		}
#endif
	}
};

class FUCRibbonIndexBuffer : public FIndexBuffer
{
public:

	UUCRibbonEmitter*	Emitter;
	INT					NumIndices;

	// Constructor.
	FUCRibbonIndexBuffer( UUCRibbonEmitter* InEmitter, INT InNumIndices )
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

		if( !Emitter->Particles.Num() )
			return;

		if( Emitter->Particles(0) != NULL &&
			!(((FParticle*)Emitter->Particles(0))->Flags == PTF_Active) )
			return;

		if( Emitter->RibbonPoints.Num() < 2 )
			return;

		for( INT Sheet=0; Sheet < Emitter->SheetsUsed; Sheet++ )
		{
			for (INT point=0; point < Emitter->RibbonPoints.Num(); point++)
			{
				*(WordData++) = 0 + VertexIndex*IndicesPerPoint;
				*(WordData++) = 1 + VertexIndex*IndicesPerPoint;
				VertexIndex++;
			}
		}
	}
};

void* UUCRibbonEmitter::FillVertexBuffer( class FUCRibbonVertex** pVertices, FLevelSceneNode* SceneNode, INT Sheet )
{
	guard(UUCTrailEmitter::FillVertexBuffer);

	FUCRibbonVertex* Vertex = (FUCRibbonVertex*) *pVertices;
	FUCRibbonVertex* VertexStart = Vertex;
	FParticle* Particle = (FParticle*)Particles(0);
	check(Particle && "Attempt to render NULL particles");
	FColor VertColour = Particle->Color;

	FLOAT TimeCoef;
	TimeCoef = Clamp(SampleTimer, 0.f, RealSampleRate)/RealSampleRate;

	FLOAT RelativeTime;
	if (Particle->MaxLifetime)
		RelativeTime = Particle->Time / Particle->MaxLifetime;
	else RelativeTime = 0.f;

	FLOAT GlobalScale = GetGlobalScale(RelativeTime);

	FVector lastTopPos(0,0,0), lastBtmPos(0,0,0);
	FVector vertAxis, vertLoc;
	FLOAT vertLength;

	FLOAT sheetAngle = PI * FLOAT(Sheet)/FLOAT(SheetsUsed);
	INT iAngle = appTrunc(sheetAngle*65536.f)/(2.f*PI);

	FLOAT sheetScale = GlobalScale;
	if (bUseSheetScale && SheetScale.Num())
		sheetScale = GlobalScale*SheetScale(Sheet % SheetScale.Num());

	// build vertex list
	FLOAT topDistTotal = 0.f;
	FLOAT btmDistTotal = 0.f;

#ifdef _XBOX
	//scion -- We are building these verts directly into the push buffer
	DWORD* pdwBuff = (DWORD*) *pVertices;
	// The number of DWORDs each vert takes
	static const DWORD dwDwordsPerVert = sizeof(FUCRibbonVertex) / sizeof(DWORD);
	// This is the number of DWORDs we encode in a given inline array push
	// NOTE: It must break on primitive alignment (4 verts min)
	static const DWORD dwMaxInlineArray = ( D3DPUSH_MAX_COUNT / (dwDwordsPerVert*4) ) * (dwDwordsPerVert*4);
	// Determines how many DWORDs we've pushed into the push buffer
	DWORD dwDwordsEncoded = 0;
#endif //_XBOX

	bool bShouldTween = false;
	for (INT i=0; i<RibbonPoints.Num(); i++)
	{

#ifdef _XBOX
		// Check to see if we need to encode an inline array push
		if (dwDwordsEncoded % dwMaxInlineArray == 0)
		{
			if( dwDwordsEncoded )
			{
				// handle case when trail vertices don't fit
				// into the push buffer.  Just break for now.
				break;
			}

			// Encode the primitive type and begin command into the push buffer
			*pdwBuff++ = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END,1);
			*pdwBuff++ = PT_QuadStrip;
			// Figure out how many dwords we'll need for each particle
			DWORD dwNumThisBatch = Min<DWORD>( (RibbonPoints.Num()-i)*2*dwDwordsPerVert, dwMaxInlineArray );
			// Build a new batch of verts to be processed
			*pdwBuff++ = D3DPUSH_ENCODE( D3DPUSH_NOINCREMENT_FLAG | D3DPUSH_INLINE_ARRAY, dwNumThisBatch );
			// Make the vertex be at the correct position in the push buffer
			Vertex = (FUCRibbonVertex*)pdwBuff;
		}
#endif //_XBOX

		bShouldTween = ((bSamplePoints && (RibbonPoints.Num() == RealNumPoints))	// tween if max points and sampling
			|| (bSamplePoints && bDecayPoints && bDecaying));					// tween if decaying but sampling enabled

		FLOAT u_offset, target_offset, texU=0;
		INT point_count = RibbonPoints.Num();
		bool bGrowing = (bSamplePoints && !bDecaying && point_count < RealNumPoints);

		vertAxis = RibbonPoints(i).AxisNormal;
		vertLoc = RibbonPoints(i).Location;
		vertLength = RibbonPoints(i).Width*sheetScale;

		//if (i > 0)
		//	ctrDistTotal += FDist(RibbonPoints(i-1).Location, vertLoc);

		// handle sheet rotation

		// !!FIXME: fix potential unrotated axis bug caused while tweening
		//        (add directional axis?)

		// NOTE: currently need to avoid points created at the same location if using sheets
		if (SheetsUsed > 1)
		{
			FVector axis;

			if (i > 1)
				axis = (vertLoc - RibbonPoints(i-1).Location).SafeNormal();
			else
			{
				axis = (RibbonPoints(i+1).Location - vertLoc).SafeNormal();
				if ((axis == FVector(0.f,0.f,0.f)) && (RibbonPoints.Num() > 2))
					axis = (RibbonPoints(i+2).Location - vertLoc).SafeNormal();
			}

			if (axis != FVector(0.f,0.f,0.f))
				vertAxis = vertAxis.RotateAngleAxis(iAngle, axis);
			//debugf(TEXT("axis: (%f,%f,%f)"), axis.X, axis.Y, axis.Z);
			//debugf(TEXT("vertAxis: (%f,%f,%f)"), vertAxis.X, vertAxis.Y, vertAxis.Z);
		}

		// calculate the texture U coord

		// NEW CODE (v1.2)

		// NOTE: This code works fine for high sample rates but there is a
		//       glitch with the U tex-coord at index i==1 when the ribbon is
		//       growing, and there's a quick U snap just before decaying
		//       starts. However, this problem virtually invisible for most uses.

		// try to compensate for the U coord tween distortion
		if (i == 0)
			texU = 0.f;
		else if (i == point_count-1)
			texU = 1.f;
		else if (!bLengthBasedTextureU)
		{
			if ((bShouldTween && bDecaying) || bGrowing)
			{
				// ribbon is either growing or shrinking in size
				u_offset = 1.f / (point_count-1);

				if (bDecaying)
				{
					// !!FIXME:
					//	- work out a way to get rid of the U-snap when decaying starts
					//	- fix moving decay UVs: BUG! U of i==1 approaches 0.0 as point_count approaches 0
					target_offset = 1.f/(point_count-2);

					//VertColour = FColor(255, 0, 0);

					u_offset = FLinterp(TimeCoef, u_offset, target_offset);
					texU = Clamp(u_offset*i, 0.f, 1.f);
				}
				else
				{
					// !!FIXME: works ok but needs tweeking to make U of i==1 more uniform
					target_offset = 1.f/point_count;

					//VertColour = FColor(0, 255, 0);

					u_offset = FLinterp(TimeCoef, u_offset, target_offset);
					texU = Clamp(u_offset*i + TimeCoef*u_offset, 0.f, 1.f);
				}
			}
			else
			{
				// ribbon is constant size
				u_offset = 1.f / (point_count-1);
				texU = Clamp(u_offset*i + u_offset*TimeCoef, 0.f, 1.f);
			}
		}
		else // texture U values for other points are calculated later
			texU = 0.f;

		// only i=1 and i=RibbonPoints.Num()-1 points are tweened
		bool bTweenFront = (i==1 && !bDecaying);
		bool bTweenBack = ((i == RibbonPoints.Num()-1) && !bGrowing);

		FLOAT AlphaFade = VertColour.A;
		if( bFadeRibbonAlpha )
			AlphaFade = appTrunc( VertColour.A * (FLOAT)(RibbonPoints.Num()-i) / (FLOAT)RibbonPoints.Num() );

		// top vertex
		Vertex->Position = vertLoc + vertAxis*vertLength*(1.f - PointOriginOffset);
		if ( bShouldTween && (bTweenFront || bTweenBack) )
			VLinterp( TimeCoef, Vertex->Position, lastTopPos, Vertex->Position );			

		if (i > 0)
			topDistTotal += FDist(Vertex->Position, lastTopPos);
		lastTopPos = Vertex->Position;

		Vertex->Color = VertColour;
		Vertex->Color.A = AlphaFade;
		Vertex->U = texU*RibbonTextureUScale;
		Vertex->V = 0.0f;
		Vertex++;

#ifdef _XBOX
		// Keep the push buffer pointer in synch
		pdwBuff += dwDwordsPerVert;
		// Update the number of dwords encoded in the push buffer
		dwDwordsEncoded += dwDwordsPerVert;
#endif

		// bottom vertex
		Vertex->Position = vertLoc - vertAxis*vertLength*PointOriginOffset;
		if ( bShouldTween && (bTweenFront || bTweenBack) )
			VLinterp( TimeCoef, Vertex->Position, lastBtmPos, Vertex->Position );

		if (i > 0)
			btmDistTotal += FDist(Vertex->Position, lastBtmPos);
		lastBtmPos = Vertex->Position;

		Vertex->Color = VertColour;
		Vertex->Color.A = AlphaFade;			
		Vertex->U = texU*RibbonTextureUScale;
		Vertex->V = RibbonTextureVScale;
		Vertex++;

#ifdef _XBOX
		// Keep the push buffer pointer in synch
		pdwBuff += dwDwordsPerVert;
		// Update the number of dwords encoded in the push buffer
		dwDwordsEncoded += dwDwordsPerVert;
#endif

	}

#ifndef _XBOX
	// length weighted U coords
	if (bLengthBasedTextureU)
	{
		INT numVerts = RibbonPoints.Num()*VerticesPerPoint;
		FLOAT topU = 0.f;
		FLOAT btmU = 0.f;

		for (INT i=2; i<numVerts-2; i+=2)
		{
			FLOAT topDist = FDist(VertexStart[i].Position, VertexStart[i-2].Position);
			FLOAT btmDist = FDist(VertexStart[i+1].Position, VertexStart[i-1].Position);

			topU += topDist/topDistTotal;
			btmU += btmDist/btmDistTotal;

			VertexStart[i].U = topU * RibbonTextureUScale;
			VertexStart[i+1].U = btmU * RibbonTextureUScale;
		}
	}
#endif

#ifdef _XBOX
	//scion -- Add the terminate command for the push buffer
	*pdwBuff++ = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END,1);
	*pdwBuff++ = 0;	
	// Set where in the push buffer we ended up so that we can check for push
	// buffer overwrites
	*pVertices = (FUCRibbonVertex*)pdwBuff;
#endif //_XBOX

	return Vertex;

	unguard;
}


// UObject functions.

// In our case equivalent to Spawned for Actors.
void UUCRibbonEmitter::PostLoad()
{
	guard(UUCRibbonEmitter::PostLoad);
	MaxParticles = 1;
	Super::PostLoad();
	unguard;
}

void UUCRibbonEmitter::Destroy()
{
	guard(UUCRibbonEmitter::Destroy);
	Super::Destroy();
	unguard;
}

void UUCRibbonEmitter::PostEditChange()
{
	guard(UUCRibbonEmitter::PostEditChange);
	MaxParticles = 1;
	Super::PostEditChange();
	unguard;
}

// CleanUp.
void UUCRibbonEmitter::CleanUp()
{
	guard(UUCRibbonEmitter::CleanUp);
	Super::CleanUp();

	if (bSamplePoints)
		RibbonPoints.Empty();

	unguard;
}

void UUCRibbonEmitter::Scale( FLOAT ScaleFactor )
{
	guard(UUCRibbonEmitter::Scale);
	Super::Scale( ScaleFactor );
	unguard;
}

FLOAT UUCRibbonEmitter::GetGlobalScale(FLOAT RelativeTime)
{
	if (UseSizeScale && Particles(0) != NULL)
	{
		return ((FParticle*)Particles(0))->Size.X;
	}

	return 1.f;
}

FLOAT UUCRibbonEmitter::GetPointScale(FLOAT RelativeTime, INT PointNum)
{
	return 1.f;
}

void UUCRibbonEmitter::GetNewRibbonPoint(FVector *PointLocation, FVector *PointAxis, FLOAT *PointWidth)
{
	guard(UUCRibbonEmitter::GetNewRibbonPoint_changedbyJoe);
	check( PointLocation && PointAxis && PointWidth );

	if (!Owner)
		return;

	bool bCanUseBones;
	guard(UUCRibbonEmitter::Check0);
	bCanUseBones = bUseBones && Owner->Owner && Owner->Owner->Mesh
		&& Owner->Owner->Mesh->IsA(USkeletalMesh::StaticClass())
		&& (BoneNameStart != NAME_None);
	unguard;

	AActor *RelativeTo = NULL;
	guard(UUCRibbonEmitter::Check1);	
	if ((Owner->Owner) && (GetPointAxisFrom==PAXIS_BoneNormal || GetPointAxisFrom==PAXIS_StartBoneDirection))
		RelativeTo = Owner->Owner;
	else RelativeTo = Owner;
	check( RelativeTo );
	unguard;

	FCoords Coords = GMath.UnitCoords / Owner->Rotation;

	*PointWidth = RibbonWidth; // set the ribbon width
	FLOAT BoneWidth=0;

	// get the axis and point location from the bone coords
	if (bCanUseBones)
	{		
		FCoords start_coords, end_coords;
		AActor* actor = Owner->Owner;
		INT startBoneIdx=-1, endBoneIdx=-1;
		FVector bone_loc(0,0,0);
		USkeletalMeshInstance* SMI = NULL;
		guard(UUCRibbonEmitter::Check2);
		SMI = (actor && actor->Mesh) ? Cast<USkeletalMeshInstance>(actor->Mesh->MeshGetInstance(actor)) : NULL;
		unguard;
		if (!SMI)
			return;		

		guard(getting start/end bone index);
		startBoneIdx = SMI->MatchRefBone( BoneNameStart );
		endBoneIdx = SMI->MatchRefBone( BoneNameEnd );
		unguard;

		guard(UUCRibbonEmitter::Check3);
		if (startBoneIdx < 0)
		{
			warnf(TEXT("WARNING: Can't find start bone=[%s],mesh=[%s],actor=[%s] reverting to PAXIS_AxisNormal"), 
				*BoneNameStart, actor->Mesh->GetName(), actor->GetName() );
			bUseBones = false;
			GetPointAxisFrom = PAXIS_AxisNormal;
		}

		if (GetPointAxisFrom==PAXIS_BoneNormal && (startBoneIdx >= 0 && endBoneIdx < 0))
		{
			warnf(TEXT("WARNING: Can't find end bone=[%s],mesh=[%s],actor=[%s] reverting to PAXIS_StartBoneDirection"),
				*BoneNameEnd, actor->Mesh->GetName(), actor->GetName() );
			GetPointAxisFrom = PAXIS_StartBoneDirection;
		}
		unguard;

		FVector end_offset, start_offset;
		if( startBoneIdx >= 0 && endBoneIdx >= 0 )
		{
			start_coords = SMI->GetBoneCoords(startBoneIdx);
			end_coords = SMI->GetBoneCoords(endBoneIdx);			

			guard(UUCRibbonEmitter::Check4);
			switch ( CoordinateSystem )
			{
			case PTCS_Independent:
			case PTCS_Absolute:
				start_offset = StartBoneOffset.TransformVectorBy(GMath.UnitCoords*SMI->GetBoneRotation(startBoneIdx, 1));
				end_offset = EndBoneOffset.TransformVectorBy(GMath.UnitCoords*SMI->GetBoneRotation(endBoneIdx, 1));
				*PointAxis = ((end_coords.Origin + end_offset) - (start_coords.Origin + start_offset)).SafeNormal();

				if (!RotationOffset.IsZero()) // apply rotation offset
					*PointAxis = PointAxis->TransformPointBy(GMath.UnitCoords*RotationOffset);

				BoneWidth = ((end_coords.Origin + end_offset) - (start_coords.Origin + start_offset)).Size();
				if (bUseBoneDistance)
					*PointWidth = BoneWidth;

				*PointLocation = start_coords.Origin + start_offset + (*PointAxis) * BoneWidth/2.0f;
				break;

			case PTCS_Relative:
				start_offset = StartBoneOffset.TransformVectorBy(GMath.UnitCoords*SMI->GetBoneRotation(startBoneIdx, 1));
				start_offset.TransformVectorBy(GMath.UnitCoords/RelativeTo->Rotation);

				end_offset = EndBoneOffset.TransformVectorBy(GMath.UnitCoords*SMI->GetBoneRotation(endBoneIdx, 1));
				end_offset.TransformVectorBy(GMath.UnitCoords/RelativeTo->Rotation);

				*PointAxis = ((end_coords.Origin + end_offset) - (start_coords.Origin + start_offset)).SafeNormal();

				BoneWidth = ((end_coords.Origin + end_offset) - (start_coords.Origin + start_offset)).Size();
				if (bUseBoneDistance)
					*PointWidth = BoneWidth;

				*PointLocation = start_coords.Origin + start_offset - RelativeTo->Location;
				*PointLocation = PointLocation->TransformVectorBy(GMath.UnitCoords/RelativeTo->Rotation);
				*PointAxis = PointAxis->TransformVectorBy(GMath.UnitCoords/RelativeTo->Rotation);

				if (!RotationOffset.IsZero()) // apply rotation offset
					*PointAxis = PointAxis->TransformPointBy(GMath.UnitCoords*RotationOffset);
				break;

			default:
				start_offset = StartBoneOffset.TransformVectorBy(GMath.UnitCoords*SMI->GetBoneRotation(startBoneIdx, 1));
				end_offset = EndBoneOffset.TransformVectorBy(GMath.UnitCoords*SMI->GetBoneRotation(endBoneIdx, 1));
				*PointAxis = ((end_coords.Origin + end_offset) - (start_coords.Origin + start_offset)).SafeNormal();

				if (!RotationOffset.IsZero()) // apply rotation offset
					*PointAxis = PointAxis->TransformPointBy(GMath.UnitCoords*RotationOffset);

				BoneWidth = ((end_coords.Origin + end_offset) - (start_coords.Origin + start_offset)).Size();
				if (bUseBoneDistance)
					*PointWidth = BoneWidth;

				*PointLocation = start_coords.Origin;
				break;
			}
			unguard;

			guard(UUCRibbonEmitter::Check5);
			if( GetPointAxisFrom != PAXIS_BoneNormal )
			{
				switch (GetPointAxisFrom)
				{
				case PAXIS_OwnerX:
					*PointAxis = Coords.XAxis;
					break;
				case PAXIS_OwnerY:
					*PointAxis = Coords.YAxis;
					break;
				case PAXIS_OwnerZ:
					*PointAxis = Coords.ZAxis;
					break;
				case PAXIS_AxisNormal:
					*PointAxis = AxisNormal.SafeNormal();
					break;
				default:
					*PointAxis = FVector(0,0,1);
					break;
				}

				if( CoordinateSystem == PTCS_Relative )					
				{
					*PointAxis = PointAxis->TransformVectorBy(GMath.UnitCoords/RelativeTo->Rotation);
				}

				if (!RotationOffset.IsZero()) // apply rotation offset
				{
					*PointAxis = PointAxis->TransformPointBy(GMath.UnitCoords*RotationOffset);
				}
			}
			unguard;

			return; // sampled new point
		}

		guard(UUCRibbonEmitter::Check6);
		if ((GetPointAxisFrom == PAXIS_StartBoneDirection) && (startBoneIdx >= 0))
		{
			start_offset = StartBoneOffset.TransformVectorBy(GMath.UnitCoords*SMI->GetBoneRotation(startBoneIdx, 1));
			start_coords = SMI->GetBoneCoords(startBoneIdx);

			*PointAxis = SMI->GetBoneRotation(startBoneIdx, 1).Vector().SafeNormal();

			switch ( CoordinateSystem )
			{
			case PTCS_Independent:
			case PTCS_Absolute:
				*PointLocation = start_coords.Origin + start_offset;
				break;
			case PTCS_Relative:
				*PointLocation = (start_coords.Origin + start_offset) - RelativeTo->Location;
				*PointLocation = PointLocation->TransformVectorBy(GMath.UnitCoords/RelativeTo->Rotation);
				*PointAxis = PointAxis->TransformVectorBy(GMath.UnitCoords/RelativeTo->Rotation);
				break;

			default:
				*PointLocation = start_coords.Origin + start_offset;
				break;
			}		


			if (!RotationOffset.IsZero()) // apply rotation offset
				*PointAxis = PointAxis->TransformPointBy(GMath.UnitCoords*RotationOffset);

			return; // sampled new point
		}
		unguard;

		guard(UUCRibbonEmitter::Check7);
		if (bUseBones)
		{
			// use start bone location and specified axis type
			switch (GetPointAxisFrom)
			{
			case PAXIS_OwnerX:
				*PointAxis = Coords.XAxis;
				break;

			case PAXIS_OwnerY:
				*PointAxis = Coords.YAxis;
				break;

			case PAXIS_OwnerZ:
				*PointAxis = Coords.ZAxis;
				break;

			case PAXIS_AxisNormal:
				*PointAxis = AxisNormal.SafeNormal();
				break;

			default:
				*PointAxis = FVector(0,0,1);
				break;
			}

			start_offset = StartBoneOffset.TransformVectorBy(GMath.UnitCoords*SMI->GetBoneRotation(startBoneIdx, 1));
			start_coords = SMI->GetBoneCoords(startBoneIdx);

			switch ( CoordinateSystem )
			{
			case PTCS_Independent:
			case PTCS_Absolute:
				*PointLocation = start_coords.Origin + start_offset;
				break;
			case PTCS_Relative:
				*PointLocation = (start_coords.Origin + start_offset) - RelativeTo->Location;
				*PointLocation = PointLocation->TransformVectorBy(GMath.UnitCoords/RelativeTo->Rotation);
				*PointAxis = PointAxis->TransformVectorBy(GMath.UnitCoords/RelativeTo->Rotation);
				break;

			default:
				*PointLocation = start_coords.Origin + start_offset;
				break;
			}

			if (!RotationOffset.IsZero()) // apply rotation offset
				*PointAxis = PointAxis->TransformPointBy(GMath.UnitCoords*RotationOffset);

			return; // sampled new point
		}
		unguard;
	}

	// not using bones
	guard(UUCRibbonEmitter::Check8);
	switch (GetPointAxisFrom)
	{
		case PAXIS_OwnerX:
			*PointAxis = Coords.XAxis;
			break;

		case PAXIS_OwnerY:
			*PointAxis = Coords.YAxis;
			break;

		case PAXIS_OwnerZ:
			*PointAxis = Coords.ZAxis;
			break;

		case PAXIS_AxisNormal:
			*PointAxis = AxisNormal.SafeNormal();
			break;


		default:
			*PointAxis = FVector(0,0,1);
			break;
	}

	switch ( CoordinateSystem )
	{
		case PTCS_Independent:
		case PTCS_Absolute:
			*PointLocation = Owner->Location + StartLocationOffset;
			break;
		case PTCS_Relative: // FIXME: hrm..
			*PointLocation = (Owner->Location + StartLocationOffset) - RelativeTo->Location;
			break;

		default:
			*PointLocation = Owner->Location + StartLocationOffset;
			break;
	}

	if (!RotationOffset.IsZero()) // apply rotation offset
		*PointAxis = PointAxis->TransformPointBy(GMath.UnitCoords*RotationOffset);
	unguard;

	unguard;
}

void UUCRibbonEmitter::Initialize( INT InMaxParticles )
{
	guard(UUCRibbonEmitter::Initialize);
	Super::Initialize( InMaxParticles );

	// set up the emitter
	RealSampleRate = 1.0f/SampleRate;
	RealDecayRate = 1.0f/DecayRate;
	RealNumPoints = NumPoints;

	// ensure a point is generated as soon as the emitter starts
	SampleTimer = RealSampleRate;
	DecayTimer = -RealSampleRate;

	if ( NumSheets > 0 )
		SheetsUsed = NumSheets;
	else
		SheetsUsed = 1;

	unguard;
}

void UUCRibbonEmitter::SpawnParticle( INT Index, FLOAT SpawnTime, INT Flags, INT SpawnFlags, const FVector& LocalLocationOffset )
{
	guard(UUCRibbonEmitter::SpawnParticle);
	Super::SpawnParticle( Index, SpawnTime, Flags, SpawnFlags, LocalLocationOffset );

	if (bSamplePoints)
		RibbonPoints.Empty();

	RealSampleRate = 1.0f/SampleRate;
	RealDecayRate = 1.0f/DecayRate;
	RealNumPoints = NumPoints;
	
	SampleTimer = RealSampleRate;
	DecayTimer = -RealSampleRate;

	unguard;
}

INT UUCRibbonEmitter::UpdateParticles( FLOAT DeltaTime )
{
	guard(UUCRibbonEmitter::UpdateParticles);
	INT Value = Super::UpdateParticles(DeltaTime);

	FParticle* Particle = Particles.Num() ? (FParticle*)Particles(0) : NULL;
	if (Particle == NULL || !Owner || !(Particle->Flags & PTF_Active) ||
		//scion jg -- Check for a deleted base
		Owner->Base == NULL || Owner->Base->bDeleteMe == 1)
	{
		return Value;
	}

	Particle->Location = Owner->Location;


	//scion rwd - width decay
	if (WidthDecay != 0.f)
	{
		RibbonWidth -= (WidthDecay * DeltaTime);
	}

	// ensure bounding box surrounds ribbon strip
	for (INT i=0; i<RibbonPoints.Num(); i++ )
	{
		BoundingBox += RibbonPoints(i).Location + RibbonPoints(i).AxisNormal*RibbonPoints(i).Width*(1.f - PointOriginOffset);
		BoundingBox += RibbonPoints(i).Location - RibbonPoints(i).AxisNormal*RibbonPoints(i).Width*PointOriginOffset;
	}

	if( !bSamplePoints )
		return RibbonPoints.Num();

	// sample new ribbon point
	FVector pointLoc(0,0,0), pointAxis(0,0,0);
	FLOAT pointWidth= abs(RibbonWidth);
	GetNewRibbonPoint(&pointLoc, &pointAxis, &pointWidth);
	if (pointAxis == FVector(0,0,0))
		return RibbonPoints.Num(); // GetNewRibbonPoint() failed

	// update first point
	bool bIsFirstUpdatePass = false;
	if (RibbonPoints.Num() == 0)
	{
		// this is the first update pass since the RibbonPoints array was emptied
		RibbonPoints.Insert(0);
		bIsFirstUpdatePass = true;
	}

	// always set the first point to the currently sampled value
	RibbonPoints(0).Location = pointLoc;
	RibbonPoints(0).AxisNormal = pointAxis;
	RibbonPoints(0).Width = pointWidth;

	// basic particle velocity support
	FVector addVel = Particle->Velocity;
	addVel = addVel.TransformVectorBy(GMath.UnitCoords * Owner->Rotation);
	if( !addVel.IsZero() )
	{
		for (INT i=1; i<RibbonPoints.Num(); i++)
			RibbonPoints(i).Location += addVel*DeltaTime;

		LastSampledPoint.Location += addVel*DeltaTime;
	}	

	// sample timer has the time elapsed since the last sample was added
	SampleTimer += DeltaTime;
	if (SampleTimer >= RealSampleRate)
	{
		// get the next point
		if ( bIsFirstUpdatePass // don't make sample test on first update pass
			|| (FDist(LastSampledPoint.Location, pointLoc) >= MinSampleDist)
			|| ((LastSampledPoint.AxisNormal | pointAxis) <= MinSampleDot) )
		{
			RibbonPoints.Insert(1);
			RibbonPoints(1).Location = pointLoc;
			RibbonPoints(1).AxisNormal = pointAxis;
			RibbonPoints(1).Width = pointWidth;

			bDecaying = false;
			SampleTimer = 0.f;

			// save point data for sample tests later
			LastSampledPoint = RibbonPoints(1);
		}
	}

	if( RibbonPoints.Num() > RealNumPoints )
		RibbonPoints.Remove(RibbonPoints.Num()-1);

	// remove points from last->first every RealDecayRate time interval
	if( KillPending &&
		bDecayPoints &&
		RealDecayRate > 0 )
	{
		// decay timer has the time elapsed since the last removal
		DecayTimer += DeltaTime;
		if( DecayTimer >= RealDecayRate &&
			RibbonPoints.Num() > 1 )
		{
			while( RibbonPoints.Num() && DecayTimer > 0 )
			{
				RibbonPoints.Remove(RibbonPoints.Num() - 1);
				DecayTimer -= RealDecayRate;
			}
			bDecaying = true;
			DecayTimer = 0.f;
		}
	}

	return RibbonPoints.Num();
	unguard;
}

// Renders all active particles
INT UUCRibbonEmitter::RenderParticles(FDynamicActor* DynActor,FLevelSceneNode* SceneNode,TList<FDynamicLight*>* Lights,FRenderInterface* RI)
{
	guard(UUCRibbonEmitter::RenderParticles);

	UParticleEmitter::RenderParticles( DynActor, SceneNode, Lights, RI );

	// draw all the segments of the ribbon triangle list
	FParticle* Particle = Particles.Num() ? (FParticle*)Particles(0) : NULL;
	if ((RibbonPoints.Num() < 2) || Particle == NULL || !(Particle->Flags & PTF_Active))
		return 0;	

	// Rotate & translate effect if it is a relative coordinate system.
	AActor *RelativeTo = NULL;
	if ((Owner->Owner) && (GetPointAxisFrom==PAXIS_BoneNormal || GetPointAxisFrom==PAXIS_StartBoneDirection))
		RelativeTo = Owner->Owner;
	else RelativeTo = Owner;

	if ( CoordinateSystem == PTCS_Relative )
	{
		FMatrix	LocalToWorld = FRotationMatrix(RelativeTo->Rotation) * FTranslationMatrix(RelativeTo->Location);
		RI->SetTransform(TT_LocalToWorld,LocalToWorld);
	}
	else
		RI->SetTransform(TT_LocalToWorld,FMatrix::Identity);

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

	// Create the dynamic vertex stream and index buffer.
	INT NumVerts = SheetsUsed * RibbonPoints.Num() * VerticesPerPoint;
	INT NumIndices = SheetsUsed * RibbonPoints.Num() * IndicesPerPoint;
	INT NumTris	= SheetsUsed * (RibbonPoints.Num()-1) * 2;

	FUCRibbonVertexStream RibbonVertices( this, NumVerts, SceneNode );

#ifdef _XBOX
	RI->SetIndexBuffer(NULL,0);
	
	UBOOL InitialCommit = 0;
	DWORD dwParticleSize, dwSizeNeeded, *pdwPushBuffer;
	for( INT Sheet=0; Sheet < SheetsUsed; Sheet++ )
	{
		BEGIN_D3D_EVENT(BDE)(PIX_PARTICLE,TEXT("%s"),GetName());
		dwParticleSize = sizeof(FUCRibbonVertex) * RibbonPoints.Num() * VerticesPerPoint;
		dwSizeNeeded = 5 * sizeof(DWORD) + dwParticleSize + ( dwParticleSize / ((D3DPUSH_MAX_COUNT-5)*sizeof(DWORD)) * (5*sizeof(DWORD)) );
		if( !InitialCommit )
		{
			// Get a chunk of push buffer to use
			pdwPushBuffer = RI->BeginPush(dwSizeNeeded,0,&RibbonVertices, 1, 1);
			InitialCommit = 1;
		}
		else
		{
			pdwPushBuffer = RI->BeginPush(dwSizeNeeded,0,&RibbonVertices, 0, 0);
		}
		// Build the lines into the push buffer
		FillVertexBuffer( (FUCRibbonVertex**)&pdwPushBuffer, SceneNode, Sheet );
		// We are done so, let go of the push buffer
		RI->EndPush(pdwPushBuffer);
		END_D3D_EVENT;
	}

#else
	FUCRibbonIndexBuffer  RibbonIndices( this, NumIndices );

	// Set the particle vertex stream and index buffer.
	// The particle vertices aren't actually generated until now.
	INT	BaseVertexIndex = RI->SetDynamicStream( 0, &RibbonVertices ),
		BaseIndex		= RI->SetDynamicIndexBuffer( &RibbonIndices, BaseVertexIndex );

	RI->DrawPrimitive(
		PT_TriangleStrip,
		BaseIndex,
		NumTris,
		0,
		NumIndices-1
		);

#endif //_XBOX

	return 1;

	unguard;
}
