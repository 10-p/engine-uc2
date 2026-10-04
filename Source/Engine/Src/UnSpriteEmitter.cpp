/*=============================================================================
	UnSpriteEmitter.cpp: Unreal Sprite Emitter
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Daniel Vogel
=============================================================================*/

#include "EnginePrivate.h"

/*-----------------------------------------------------------------------------
	USpriteEmitter.
-----------------------------------------------------------------------------*/

#define VerticesPerParticle		4
#define IndicesPerParticle		6
#ifdef _XBOX
//scion jg -- Use quads for sprite particles
#define PrimitivesPerParticle	1
#else
#define PrimitivesPerParticle	2
#endif

//
// FSpriteParticleVertex
//
class FSpriteParticleVertex
{
public:

	FVector	Position;
	FColor	Color;
	FLOAT	U,
			V,
			U2,
			V2;

	static INT GetComponents(FVertexComponent* OutComponents)
	{
		OutComponents[0].Type = CT_Float3;
		OutComponents[0].Function = FVF_Position;
		OutComponents[1].Type = CT_Color;
		OutComponents[1].Function = FVF_Diffuse;
		OutComponents[2].Type = CT_Float2;
		OutComponents[2].Function = FVF_TexCoord0;
		OutComponents[3].Type = CT_Float2;
		OutComponents[3].Function = FVF_TexCoord1;
        return 4;
	}
};

//
// FSpriteParticleVertexStream
//
class FSpriteParticleVertexStream : public FParticleVertexStream<FSpriteParticleVertex>
{
public:

	USpriteEmitter*		Emitter;
	INT					NumParticles;
	FLevelSceneNode*	SceneNode;

	// Constructor.
	FSpriteParticleVertexStream(USpriteEmitter* InEmitter,INT InNumParticles,FLevelSceneNode* InSceneNode)
	{
		Emitter			= InEmitter;
		NumParticles	= InNumParticles;
		SceneNode		= InSceneNode;
	}

	virtual INT GetSize()
	{
		return NumParticles * sizeof(FSpriteParticleVertex) * VerticesPerParticle;
	}

	// Generates the sprite vertices procedurally.
	virtual void GetStreamData(void* Dest)
	{
		FUNC_CLOCK(RenCyc,GStats.DWORDStats( GEngineStats.STATS_Particle_SpriteSetupCycles ));
		Emitter->FillVertexBuffer( (FSpriteParticleVertex*) Dest, SceneNode );
	}
};

//
//
// FSpriteParticleIndexBuffer
class FSpriteParticleIndexBuffer : public FIndexBuffer
{
public:

	INT		NumParticles;

	// Constructor.
	FSpriteParticleIndexBuffer( INT InNumParticles )
	{
		NumParticles	= InNumParticles;
	}

	virtual INT GetRevision()
	{
		return 1;
	}

	virtual INT GetSize()
	{
		return NumParticles * sizeof(_WORD) * IndicesPerParticle;
	}

	virtual INT GetIndexSize()
	{
		return sizeof(_WORD);
	}

	virtual void GetContents(void* Data)
	{
		_WORD* WordData = (_WORD*)Data;
		for(INT VerticesIndex=0; VerticesIndex<NumParticles; VerticesIndex++)
		{
			*(WordData++) = 4*VerticesIndex+0;
			*(WordData++) = 4*VerticesIndex+1;
			*(WordData++) = 4*VerticesIndex+2;
			*(WordData++) = 4*VerticesIndex+2;
			*(WordData++) = 4*VerticesIndex+1;
			*(WordData++) = 4*VerticesIndex+3;
		}
	}
};

IMPLEMENT_CLASS(USpriteEmitter);

void USpriteEmitter::PostEditChange()
{
	Super::PostEditChange();
	CleanUp();
	Initialize( MaxParticles );
}

// Initializes the Emitter.
void USpriteEmitter::Initialize( INT InMaxParticles )
{
	guard(USpriteEmitter::Initialize);
	Super::Initialize( InMaxParticles );
	RealProjectionNormal = ProjectionNormal.SafeNormal();
	UniformSize = UniformSize || (UseDirectionAs == PTDU_None);
#ifdef __PSX2_EE__
	PreCachePS2Emitter((void**)&PS2Data, MaxParticles, VerticesPerParticle); // we are doing a mini strip
#endif
	unguard;
}

// Update particles.
INT USpriteEmitter::UpdateParticles( FLOAT DeltaTime )
{
	guard(USpriteEmitter::UpdateParticles);
	//scion jg -- Don't tick multiple times if instanced
	if (bIsInstanced && LastUpdateTime == Owner->Level->TimeSeconds)
	{
		return 0;
	}
	INT Value	= Super::UpdateParticles( DeltaTime );
	//jg -- Only do the math if the box is valid, otherwise add the location
	if( !BoundingBox.IsValid() && 
		CoordinateSystem!=PTCS_Relative )
	{
		BoundingBox += Owner->Location;
	}

	FLOAT ExpandBy = MaxSizeScale;
	if( UniformSize )
		ExpandBy *= StartSizeRange.X.GetMax();
	else
		ExpandBy *= Max(StartSizeRange.X.GetMax(), StartSizeRange.Y.GetMax());
	BoundingBox = BoundingBox.ExpandBy( ExpandBy );

	return Value;
	unguard;
}

// CleanUp.
void USpriteEmitter::CleanUp()
{
	guard(USpriteEmitter::CleanUp);
	Super::CleanUp();
	unguard;
}

#ifdef _XBOX
/* scion ======================================================================
 * USpriteEmitter::FillPushBuffer
 * Author: jg
 * 
 * Builds the sprite emitter vertices directly into the push buffer
 *
 * input:	Vertices		The in/out pointer for the push buffer
 *			SceneNode		The scene node we are rendering with (transform info)
 *			dwNumParticles	The number of particles being written into the PB
 *
 * ============================================================================
 */
INT USpriteEmitter::FillPushBuffer(FSpriteParticleVertex** Vertices,
	FLevelSceneNode* SceneNode, DWORD dwNumParticles)
{
	FVector ViewLocation = SceneNode->ViewOrigin;

	FVector ProjBase     = SceneNode->Deproject(FPlane(0,0,0,1));	
	FVector ProjUp		 = SceneNode->Deproject(FPlane(0,-1000,0,1)) - ProjBase;
	FVector ProjRight	 = SceneNode->Deproject(FPlane(1000,0,0,1)) - ProjBase;
	FVector ProjFront	 = ProjRight ^ ProjUp;

	// Transform coordinate system.
	if ( CoordinateSystem == PTCS_Relative )
	{
		FCoords InvOwnerRotCoords( GMath.UnitCoords );
		InvOwnerRotCoords /= Owner->Rotation;
		ViewLocation	= ViewLocation	.TransformPointBy ( InvOwnerRotCoords / Owner->Location );
		ProjUp			= ProjUp		.TransformVectorBy( InvOwnerRotCoords );
		ProjRight		= ProjRight		.TransformVectorBy( InvOwnerRotCoords );
		ProjFront		= ProjFront		.TransformVectorBy( InvOwnerRotCoords );
	} 
	
	// Rotate normal if actor gets rotated.
	FVector RotatedProjectionNormal = RealProjectionNormal;
	if ( UseRotationFrom == PTRS_Actor && Owner)
		RotatedProjectionNormal = RotatedProjectionNormal.TransformVectorBy( GMath.UnitCoords * Owner->Rotation );

	// Better safe than sorry.
	ProjUp.Normalize();
	ProjRight.Normalize();
	ProjFront.Normalize();

	//scion jg -- We are building these verts directly into the push buffer

	DWORD* pdwBuff = (DWORD*)*Vertices;
	// Encode the primitive type and begin command into the push buffer
	*pdwBuff++ = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END,1);
	*pdwBuff++ = PT_QuadList;
	// The number of DWORDs each vert takes
	const DWORD dwDwordsPerVert = sizeof(FSpriteParticleVertex) / sizeof(DWORD);
	// This is the number of DWORDs we encode in a given inline array push
	// NOTE: It must break on primitive alignment
	const DWORD dwMaxInlineArray = (D3DPUSH_MAX_COUNT /
		sizeof(FSpriteParticleVertex)) * dwDwordsPerVert * 4;
	// Determines how many DWORDs we've pushed into the push buffer
	DWORD dwDwordsEncoded = 0;
	// Get the pointer of where we'll start building verts
	FSpriteParticleVertex* Vertex = (FSpriteParticleVertex*)pdwBuff;

	for(INT Index=0; Index<ActiveParticles; Index++)
	{
		FParticle* Particle = (FParticle*)Particles(Index);

		if(Particle == NULL || !(Particle->Flags & PTF_Active))
			continue;
		
		// Color.
		FColor Color( Particle->Color );

		// Time.
		FLOAT Time			= Particle->Time;
		FLOAT RelativeTime;
		if( Particle->SubdivisionPlayTime )
		{
			RelativeTime = Time / Particle->SubdivisionPlayTime;
			RelativeTime = RelativeTime - ((INT)RelativeTime);
		}
		else if( Particle->MaxLifetime )
		{
			RelativeTime	= Clamp( Time/Particle->MaxLifetime, 0.0f, 1.0f );
		}
		else
		{
			RelativeTime	= 0.f;
		}

		// Projection code.
		FVector Up;
		FVector Right;
		FVector ProjTemp;
		FVector Direction;

		if ( UseDirectionAs == PTDU_Normal )
		{
			Direction = RotatedProjectionNormal;
		}
		else if ( UseDirectionAs == PTDU_Forward )
		{
			Direction = Particle->Location - Particle->OldLocation;
			Direction.Normalize();
		}
		else
		{
			Direction = Particle->Location - Particle->OldLocation;
			Direction.Normalize();
			if ( (UseDirectionAs == PTDU_UpAndNormal) || (UseDirectionAs == PTDU_RightAndNormal) )
			{
				ProjTemp = Direction ^ RealProjectionNormal;
				//ProjTemp.Normalize();
			}
			else
			{
				//scion jg -- Along normal and camera facing
				if (UseDirectionAs == PTDU_NormalAndUp)
				{
					Direction = RealProjectionNormal;
				}
				//scion jg -- Along emitter's rotation and camera facing
				if (UseDirectionAs == PTDU_EmitterRotationAndUp)
				{
					Direction = Owner->Rotation.Vector().SafeNormal();
				}
				ProjTemp = Direction ^ (Particle->Location - ViewLocation);
				ProjTemp.Normalize();
			}
		}

		FVector RotationAxis = ProjFront;
		switch ( UseDirectionAs )
		{
		case PTDU_None:
			Up	  = ProjUp    * Particle->Size.X; //!!(Particle->Size.Y ? Particle->Size.Y : Particle->Size.X);
			Right = ProjRight * Particle->Size.X;
			break;
		case PTDU_Scale:
			Up	  = ProjUp    * Particle->Size.Y;
			Right = ProjRight * Particle->Size.X;
			break;
		case PTDU_Up:
		case PTDU_UpAndNormal:
		//scion jg -- Along normal and camera facing
		case PTDU_NormalAndUp:
		//scion jg -- Direction along emitter's rotation and align
		case PTDU_EmitterRotationAndUp:
			Up	  = Direction * Particle->Size.Y;
			Right = ProjTemp  * Particle->Size.X;
			break;
		case PTDU_Right:
		case PTDU_RightAndNormal:
			Up    = ProjTemp  * Particle->Size.Y;
			Right = Direction * Particle->Size.X;
			break;
		case PTDU_Forward:
		case PTDU_Normal:
			Up    = (Direction ^ Direction.GetNonParallel()) * Particle->Size.Y;
			Right = (Direction ^ Up).SafeNormal() * Particle->Size.X;
			RotationAxis = Direction;
			break;
		default:
			Up	  = FVector(0,0,0);
			Right = FVector(0,0,0);
			break;
		}

		// Rotation/ Spinning.
		if ( SpinParticles )
		{
			INT Angle;
			if ( Particle->SpinsPerSecond.X < 0 )
				Angle = 0xFFFF - (appRound(Particle->StartSpin.X + Time * -Particle->SpinsPerSecond.X) & 0xFFFF);
			else
				Angle = appRound(Particle->StartSpin.X + Time * Particle->SpinsPerSecond.X) & 0xFFFF;

			Up    =    Up.RotateAngleAxis(Angle, RotationAxis);
			Right = Right.RotateAngleAxis(Angle, RotationAxis);
		}

		// UV generation.
		FLOAT UMin  = 0; FLOAT UMax  = 1; FLOAT VMin  = 0; FLOAT U2Min = 0;
		FLOAT U2Max = 1; FLOAT VMax  = 1; FLOAT V2Min = 0; FLOAT V2Max = 1;

		FLOAT FU = 1.f;
		FLOAT FV = 1.f;
		INT Section = Particle->Subdivision;

		if (TextureUSubdivisions && TextureVSubdivisions)
		{
			FU	= 1.f / TextureUSubdivisions;
			FV	= 1.f / TextureVSubdivisions;

			if ( Section != -1 )
			{
				VMin  = (Section % TextureVSubdivisions) * FV;
				VMax  = Min( VMin + FV, 1.f );

				UMin  = (Section / TextureVSubdivisions) * FU;
				UMax  = Min( UMin + FU, 1.f );
			}
			else if ( Particle->MaxLifetime )
			{
				INT SubDivs	= TextureUSubdivisions * TextureVSubdivisions;
			
				if (UseSubdivisionScale)
				{
					INT t;
					for (t=0; t<SubdivisionScale.Num(); t++)
						if (RelativeTime <= SubdivisionScale(t))
							break;
					Section = t;
				}
				else if (SubdivisionEnd)
				{
					SubDivs = Max( 1, SubdivisionEnd - SubdivisionStart );
					Section	= appFloor(RelativeTime * SubDivs) + SubdivisionStart;
				}
				else
				{
					Section	= appFloor(RelativeTime * SubDivs);
				}
				
				Section = Clamp( Section, 0, TextureUSubdivisions * TextureVSubdivisions - 1 );	

				VMin  = (Section % TextureVSubdivisions) * FV;
				VMax  = Min( VMin + FV, 1.f );

				UMin  = (Section / TextureVSubdivisions) * FU;
				UMax  = Min( UMin + FU, 1.f );
			
				if ( BlendBetweenSubdivisions )
				{
					if ( ++Section != SubDivs )
					{
						V2Min  = (Section % TextureVSubdivisions) * FV;
						V2Max  = V2Min + FV;
	
						U2Min  = (Section / TextureVSubdivisions) * FU;
						U2Max  = U2Min + FU;
					}
					else
					{
						// SZ - blend last section with first
						V2Min = 0.0f;
						V2Max = FV;
						U2Min = 0.0f;
						U2Max = FU;
					}
					FLOAT Alpha;
					//TODO: move range check to PostEditChange?
					if ( UseSubdivisionScale && (SubdivisionScale.Num() > SubDivs) )
					{
						FLOAT Offset;
						if ( --Section == 0 )
							Offset = 0.f;
						else
							Offset = SubdivisionScale( Section - 1 );
						FLOAT Length = Min(1.f, SubdivisionScale( Section )) - Offset;
						Alpha  = (RelativeTime - Offset ) / Length;
					}
					else
					{
						FLOAT Temp  = RelativeTime * SubDivs;
						Alpha = Temp - appFloor(Temp);
					}
					Color.A = (BYTE)(255 * Alpha);
				}
				Section--;
			}
		}	

		Color = Color.RenderColor();

		// TODO: add option to flip up

		// Check to see if we need to encode a inline array push
		if (dwDwordsEncoded % dwMaxInlineArray == 0)
		{
			// Figure out how many dwords we'll need for each particle
			DWORD dwNumThisBatch = Min<DWORD>(dwNumParticles *
				sizeof(FSpriteParticleVertex),dwMaxInlineArray);
			// Build a new batch of verts to be processed
			*pdwBuff++ = D3DPUSH_ENCODE(
				D3DPUSH_NOINCREMENT_FLAG | D3DPUSH_INLINE_ARRAY,
				dwNumThisBatch);
			// Make the vertex be at the correct position in the push buffer
			Vertex = (FSpriteParticleVertex*)pdwBuff;
		}

		Vertex->Position	= Particle->Location - Up - Right;
		Vertex->Color		= Color;
		Vertex->U			= UMin;
		Vertex->U2			= U2Min;
		Vertex->V			= VMax;
		Vertex->V2			= V2Max;
		Vertex++;
		// Keep the push buffer pointer in synch
		pdwBuff += dwDwordsPerVert;
		// Update the number of dwords encoded in the push buffer
		dwDwordsEncoded += dwDwordsPerVert;

		Vertex->Position	= Particle->Location - Up + Right;
		Vertex->Color		= Color;
		Vertex->U			= UMax;
		Vertex->U2			= U2Max;
		Vertex->V			= VMax;
		Vertex->V2			= V2Max;
		Vertex++;
		// Keep the push buffer pointer in synch
		pdwBuff += dwDwordsPerVert;
		// Update the number of dwords encoded in the push buffer
		dwDwordsEncoded += dwDwordsPerVert;

		Vertex->Position	= Particle->Location + Up + Right;
		Vertex->Color		= Color;
		Vertex->U			= UMax;
		Vertex->U2			= U2Max;
		Vertex->V			= VMin;
		Vertex->V2			= V2Min;
		Vertex++;
		// Keep the push buffer pointer in synch
		pdwBuff += dwDwordsPerVert;
		// Update the number of dwords encoded in the push buffer
		dwDwordsEncoded += dwDwordsPerVert;

		Vertex->Position	= Particle->Location + Up - Right;
		Vertex->Color		= Color;
		Vertex->U			= UMin;
		Vertex->U2			= U2Min;
		Vertex->V			= VMin;
		Vertex->V2			= V2Min;
		Vertex++;
		// Keep the push buffer pointer in synch
		pdwBuff += dwDwordsPerVert;
		// Update the number of dwords encoded in the push buffer
		dwDwordsEncoded += dwDwordsPerVert;
		// Decrement the number of particles remaining
		dwNumParticles--;
	}

	//scion jg -- Add the terminate command for the push buffer
	*pdwBuff++ = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END,1);
	*pdwBuff++ = 0;
	// Set where in the push buffer we ended up so that we can check for push
	// buffer overwrites
	*Vertices = (FSpriteParticleVertex*)pdwBuff;

	return 0;
}
#endif


// Fills vertex buffer with data - called indirectly.
INT USpriteEmitter::FillVertexBuffer( FSpriteParticleVertex* Vertices, FLevelSceneNode* SceneNode )
{
	FVector ViewLocation = SceneNode->ViewOrigin;

	FVector ProjBase     = SceneNode->Deproject(FPlane(0,0,0,1));	
	FVector ProjUp		 = SceneNode->Deproject(FPlane(0,-1000,0,1)) - ProjBase;
	FVector ProjRight	 = SceneNode->Deproject(FPlane(1000,0,0,1)) - ProjBase;
	FVector ProjFront	 = ProjRight ^ ProjUp;

	// Transform coordinate system.
	if ( CoordinateSystem == PTCS_Relative )
	{
		ViewLocation	= ViewLocation	.TransformPointBy ( GMath.UnitCoords / Owner->Rotation / Owner->Location );
		ProjUp			= ProjUp		.TransformVectorBy( GMath.UnitCoords / Owner->Rotation );
		ProjRight		= ProjRight		.TransformVectorBy( GMath.UnitCoords / Owner->Rotation );
		ProjFront		= ProjFront		.TransformVectorBy( GMath.UnitCoords / Owner->Rotation );
	} 
	
	// Rotate normal if actor gets rotated.
	FVector RotatedProjectionNormal = RealProjectionNormal;
	if ( UseRotationFrom == PTRS_Actor && Owner)
		RotatedProjectionNormal = RotatedProjectionNormal.TransformVectorBy( GMath.UnitCoords * Owner->Rotation );

	// Better safe than sorry.
	ProjUp.Normalize();
	ProjRight.Normalize();
	ProjFront.Normalize();

	FSpriteParticleVertex* Vertex = Vertices;

	for(INT Index=0; Index<ActiveParticles; Index++)
	{
		FParticle* Particle = (FParticle*)Particles(Index);

		if(Particle == NULL || !(Particle->Flags & PTF_Active))
			continue;
		
		// Color.
		FColor Color = Particle->Color;

		// Time.
		FLOAT Time			= Particle->Time;
		FLOAT RelativeTime;
		if( Particle->SubdivisionPlayTime )
		{
			RelativeTime = Time / Particle->SubdivisionPlayTime;
			RelativeTime = RelativeTime - ((INT)RelativeTime);
		}
		else if( Particle->MaxLifetime )
		{
			RelativeTime	= Clamp( Time/Particle->MaxLifetime, 0.0f, 1.0f );
		}
		else
		{
			RelativeTime	= 0.f;
		}

		// Projection code.
		FVector Up;
		FVector Right;
		FVector ProjTemp;
		FVector Direction;

		if ( UseDirectionAs == PTDU_Normal )
		{
			Direction = RotatedProjectionNormal;
		}
		else if ( UseDirectionAs == PTDU_Forward )
		{
			Direction = Particle->Location - Particle->OldLocation;
			Direction.Normalize();
		}
		else
		{
			Direction = Particle->Location - Particle->OldLocation;
			Direction.Normalize();
			if ( (UseDirectionAs == PTDU_UpAndNormal) || (UseDirectionAs == PTDU_RightAndNormal) )
			{
				ProjTemp = Direction ^ RealProjectionNormal;
				//ProjTemp.Normalize();
			}
			else
			{
				//scion jg -- Along normal and camera facing
				if (UseDirectionAs == PTDU_NormalAndUp)
				{
					Direction = RealProjectionNormal;
				}
				//scion jg -- Along emitter's rotation and camera facing
				if (UseDirectionAs == PTDU_EmitterRotationAndUp)
				{
					Direction = Owner->Rotation.Vector().SafeNormal();
				}
				ProjTemp = Direction ^ (Particle->Location - ViewLocation);
				ProjTemp.Normalize();
			}
		}

		FVector RotationAxis = ProjFront;
		switch ( UseDirectionAs )
		{
		case PTDU_None:
			Up	  = ProjUp    * Particle->Size.X; //!!(Particle->Size.Y ? Particle->Size.Y : Particle->Size.X);
			Right = ProjRight * Particle->Size.X;
			break;
		case PTDU_Scale:
			Up	  = ProjUp    * Particle->Size.Y;
			Right = ProjRight * Particle->Size.X;
			break;
		case PTDU_Up:
		case PTDU_UpAndNormal:
		//scion jg -- Along normal and camera facing
		case PTDU_NormalAndUp:
		//scion jg -- Direction along emitter's rotation and align
		case PTDU_EmitterRotationAndUp:
			Up	  = Direction * Particle->Size.Y;
			Right = ProjTemp  * Particle->Size.X;
			break;
		case PTDU_Right:
		case PTDU_RightAndNormal:
			Up    = ProjTemp  * Particle->Size.Y;
			Right = Direction * Particle->Size.X;
			break;
		case PTDU_Forward:
		case PTDU_Normal:
			Up    = (Direction ^ Direction.GetNonParallel()) * Particle->Size.Y;
			Right = (Direction ^ Up).SafeNormal() * Particle->Size.X;
			RotationAxis = Direction;
			break;
		default:
			Up	  = FVector(0,0,0);
			Right = FVector(0,0,0);
			break;
		}

		// Rotation/ Spinning.
		if ( SpinParticles )
		{
			INT Angle;
			if ( Particle->SpinsPerSecond.X < 0 )
				Angle = 0xFFFF - (appRound(Particle->StartSpin.X + Time * -Particle->SpinsPerSecond.X) & 0xFFFF);
			else
				Angle = appRound(Particle->StartSpin.X + Time * Particle->SpinsPerSecond.X) & 0xFFFF;

			Up    =    Up.RotateAngleAxis(Angle, RotationAxis);
			Right = Right.RotateAngleAxis(Angle, RotationAxis);
		}

		// UV generation.
		FLOAT UMin  = 0; FLOAT UMax  = 1; FLOAT VMin  = 0; FLOAT U2Min = 0;
		FLOAT U2Max = 1; FLOAT VMax  = 1; FLOAT V2Min = 0; FLOAT V2Max = 1;

		FLOAT FU = 1.f;
		FLOAT FV = 1.f;
		INT Section = Particle->Subdivision;

		if (TextureUSubdivisions && TextureVSubdivisions)
		{
			FU	= 1.f / TextureUSubdivisions;
			FV	= 1.f / TextureVSubdivisions;

			if ( Section != -1 )
			{
				VMin  = (Section % TextureVSubdivisions) * FV;
				VMax  = Min( VMin + FV, 1.f );

				UMin  = (Section / TextureVSubdivisions) * FU;
				UMax  = Min( UMin + FU, 1.f );
			}
			else if ( Particle->MaxLifetime )
			{
				INT SubDivs	= TextureUSubdivisions * TextureVSubdivisions;
			
				if (UseSubdivisionScale)
				{
					INT t;
					for (t=0; t<SubdivisionScale.Num(); t++)
						if (RelativeTime <= SubdivisionScale(t))
							break;
					Section = t;
				}
				else if (SubdivisionEnd)
				{
					SubDivs = Max( 1, SubdivisionEnd - SubdivisionStart );
					Section	= appFloor(RelativeTime * SubDivs) + SubdivisionStart;
				}
				else
				{
					Section	= appFloor(RelativeTime * SubDivs);
				}
				
				Section = Clamp( Section, 0, TextureUSubdivisions * TextureVSubdivisions - 1 );	

				VMin  = (Section % TextureVSubdivisions) * FV;
				VMax  = Min( VMin + FV, 1.f );

				UMin  = (Section / TextureVSubdivisions) * FU;
				UMax  = Min( UMin + FU, 1.f );
			
				if ( BlendBetweenSubdivisions )
				{
					FLOAT Alpha;
					//TODO: move range check to PostEditChange?
					if ( UseSubdivisionScale && (SubdivisionScale.Num() > SubDivs) )
					{
						FLOAT Offset;
						if ( --Section == 0 )
							Offset = 0.f;
						else
							Offset = SubdivisionScale( Section - 1 );
						FLOAT Length = Min(1.f, SubdivisionScale( Section )) - Offset;
						Alpha  = (RelativeTime - Offset ) / Length;
					}
					else
					{
						FLOAT Temp  = RelativeTime * SubDivs;
						Alpha = Temp - appFloor(Temp);
					}
					Color.A = (BYTE)(255 * Alpha);

					if ( ++Section != SubDivs )
					{
						V2Min  = (Section % TextureVSubdivisions) * FV;
						V2Max  = V2Min + FV;
	
						U2Min  = (Section / TextureVSubdivisions) * FU;
						U2Max  = U2Min + FU;
					}
					else
					{
						// SZ - blend last section with first
						V2Min = 0.0f;
						V2Max = FV;
						U2Min = 0.0f;
						U2Max = FU;
					}
				}
				Section--;
			}
		}	

		Color = Color.RenderColor();

		// TODO: add option to flip up

		Vertex->Position	= Particle->Location - Up - Right;
		Vertex->Color		= Color;
		Vertex->U			= UMin;
		Vertex->U2			= U2Min;
		Vertex->V			= VMax;
		Vertex->V2			= V2Max;
		Vertex++;

		Vertex->Position	= Particle->Location - Up + Right;
		Vertex->Color		= Color;
		Vertex->U			= UMax;
		Vertex->U2			= U2Max;
		Vertex->V			= VMax;
		Vertex->V2			= V2Max;
		Vertex++;
			
		Vertex->Position	= Particle->Location + Up - Right;
		Vertex->Color		= Color;
		Vertex->U			= UMin;
		Vertex->U2			= U2Min;
		Vertex->V			= VMin;
		Vertex->V2			= V2Min;
		Vertex++;

		Vertex->Position	= Particle->Location + Up + Right;
		Vertex->Color		= Color;
		Vertex->U			= UMax;
		Vertex->U2			= U2Max;
		Vertex->V			= VMin;
		Vertex->V2			= V2Min;
		Vertex++;
	}

	return 0;
}


// Renders all active particles
INT USpriteEmitter::RenderParticles(FDynamicActor* DynActor,FLevelSceneNode* SceneNode,TList<FDynamicLight*>* Lights,FRenderInterface* RI)
{
	guard(USpriteEmitter::RenderParticles);

	// Handles clock'ing and other high level options.
//	UParticleEmitter::RenderParticles( DynActor, SceneNode, Lights, RI );
	Inactive=false;

	if ( Texture == NULL )
		return 0;
	
	INT RenderedParticles = 0;

	// Render all active particles.
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

		// Create the dynamic vertex stream and index buffer.
		FSpriteParticleVertexStream SpriteParticleVertices(this,RenderedParticles,SceneNode);
#ifndef _XBOX
		FSpriteParticleIndexBuffer  SpriteParticleIndices(RenderedParticles);
#endif

		// Rotate & translate if needed.
		if ( CoordinateSystem == PTCS_Relative )
		{
			FMatrix	LocalToWorld = FRotationMatrix(Owner->Rotation) * 
				FTranslationMatrix(Owner->Location + Owner->PrePivot + ((Owner->Base && Owner->AttachmentBone == NAME_None) ? Owner->Base->PrePivot : FVector(0,0,0)) );
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


		//DWORD ShaderType = RI->GetMaterialVertexShaderType(Owner->ParticleMaterial->BitmapMaterial);

#ifdef _XBOX
		FUNC_CLOCK(SetCyc,GStats.DWORDStats( GEngineStats.STATS_Particle_SpriteSetupCycles ));
		//scion jg -- We are building the verts directly into the pushbuffer
		// so no indices are needed
		RI->SetIndexBuffer(NULL,0);
		// Figure out how big of a push buffer we'll need. We 5 DWORDs for the
		// basic push buffer command, plus the size of the particles and any
		// extra inline array commands
		DWORD dwParticleSize = sizeof(FSpriteParticleVertex) * 4 * RenderedParticles;
		DWORD dwSizeNeeded = 5 * sizeof(DWORD) + dwParticleSize +
			(dwParticleSize / (D3DPUSH_MAX_COUNT / sizeof(FSpriteParticleVertex)) *
			sizeof(DWORD));
		BEGIN_D3D_EVENT(BDE)(PIX_PARTICLE,TEXT("%s"),GetName());
		// Get a chunk of push buffer to use
		DWORD* pdwPushBuffer = RI->BeginPush(dwSizeNeeded,VS_Normal,
			&SpriteParticleVertices);
		// On the off chance we didn't get a push buffer pointer
		if (pdwPushBuffer != NULL)
		{
			// Build the quads into the push buffer
			FillPushBuffer((FSpriteParticleVertex**)&pdwPushBuffer,SceneNode,
				(DWORD)RenderedParticles);
			// We are done so, let go of the push buffer
			RI->EndPush(pdwPushBuffer);
		}
		else
		{
			debugf(NAME_DevD3D,TEXT("Failed to render sprite emitter %s"),
				GetName());
		}
		END_D3D_EVENT;
#else
		// Set the particle vertex stream and index buffer. The particle vertices aren't actually 
		// generated until now.
		INT	BaseVertexIndex = RI->SetDynamicStream(0,&SpriteParticleVertices);
		INT	BaseIndex		= RI->SetDynamicIndexBuffer(&SpriteParticleIndices,BaseVertexIndex);

		RI->DrawPrimitive(
			PT_TriangleList, 
			BaseIndex,
			RenderedParticles * PrimitivesPerParticle,
			0,
			RenderedParticles * VerticesPerParticle - 1
			);
#endif
	}

	return RenderedParticles;

	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

