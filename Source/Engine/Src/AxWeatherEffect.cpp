//=============================================================================
// AxWeatherEffect - implementation
// Copyright 2001 Digital Extremes - All Rights Reserved.
// Confidential.
//=============================================================================

// Mostly rewritten for performance by Daniel Vogel.

#include "EnginePrivate.h"
#include "UnRender.h"
#include "AxWeatherEffectHelper.h"

const	FLOAT	InvWeatherParticleMass	= 10.f;

static	INT		tmp_numCols				= 4.0f;    
static	INT		tmp_numRows				= 4.0f;
static	FLOAT	tmp_texU				= 0.0f;
static	FLOAT	tmp_texV				= 0.0f;

#define LerpInOut( r, x ) (((x) < (r)) ? (x)/(r) : (1.f-(x))/(1.f-(r)))

class FWeatherQuadStream : public FVertexStream
{
public:
	AxWeatherEffect*	pFx;
	INT					Revision,
						Renderable;
	FMatrix				WorldToCamera;

	void Init( AxWeatherEffect* pInFx, int inRenderable, FMatrix& inWorldToCamera )
	{
		pFx				= pInFx;
		WorldToCamera	= inWorldToCamera;
		Renderable		= inRenderable;
		Revision++;
	}

	FWeatherQuadStream()
	{
		pFx				= NULL;
		Revision		= 1;    
		Renderable		= 0;
	}

	virtual INT GetRevision()
	{
		return Revision;
	}

	virtual INT GetSize()
	{
		return Renderable * sizeof(DynamicVertex);
	}

	virtual INT GetStride()
	{
		return sizeof(DynamicVertex);
	}

	virtual INT GetComponents(FVertexComponent* OutComponents)
	{
		OutComponents[0].Type		= CT_Float3;
		OutComponents[0].Function	= FVF_Position;
		OutComponents[1].Type		= CT_Color;
		OutComponents[1].Function	= FVF_Diffuse;
		OutComponents[2].Type		= CT_Float2;
		OutComponents[2].Function	= FVF_TexCoord0;
		return 3;
	}

	virtual void GetStreamData(void* Dest)
	{
		DynamicVertex*	pVerts	= (DynamicVertex*)Dest;
		FWeatherPcl*	p		= &pFx->pcl(0);
				
		int numPcl = pFx->pcl.Num();
		for( int i=0; i<numPcl; i++ )
		{
			if( !p->Visible )
			{
				p++;
				continue;
			}

			FLOAT	t	= Clamp( 255.f * LerpInOut( 0.05f, p->Life ) * Max( p->Size * 0.5f, 1.0f ) * p->DistAtten, 0.f, 255.f );
						
			INT		Col = p->frame / tmp_numCols,
					Row = p->frame - tmp_numRows * Col;
	
			FLOAT	U1	= Row * tmp_texU,
					U2	= U1  + tmp_texU,
					V1	= Col * tmp_texV,
					V2	= V1  + tmp_texV;
	
			INT		Alpha, 
					Attenuation;

			Alpha = appRound( t );
			Attenuation = Alpha;

			DWORD Color = Attenuation | (Attenuation << 8) | (Attenuation << 16) | (Alpha << 24);

			if ( pFx->WeatherType == WT_Rain )
			{
				FVector Start	= WorldToCamera.TransformFVector(p->pos);
				FVector End		= WorldToCamera.TransformFVector(p->pos + p->Vel*.03f);

				pVerts->Point.X	= Start.X + p->Size;
				pVerts->Point.Y	= Start.Y;
				pVerts->Point.Z	= Start.Z;
				pVerts->Color	= Color;
				pVerts->U		= U2;
				pVerts->V		= V1;
				pVerts++;

				pVerts->Point.X	= Start.X - p->Size;
				pVerts->Point.Y	= Start.Y;
				pVerts->Point.Z	= Start.Z;
				pVerts->Color	= Color;
				pVerts->U		= U1;
				pVerts->V		= V1;
				pVerts++;

				pVerts->Point.X	= End.X - p->Size;
				pVerts->Point.Y	= End.Y;
				pVerts->Point.Z	= End.Z;
				pVerts->Color	= Color;
				pVerts->U		= U1;
				pVerts->V		= V2;
				pVerts++;

				pVerts->Point.X	= End.X + p->Size;
				pVerts->Point.Y	= End.Y;
				pVerts->Point.Z	= End.Z;
				pVerts->Color	= Color;
				pVerts->U		= U2;
				pVerts->V		= V2;
				pVerts++;
			}
			else
			{				
				FVector Center	= WorldToCamera.TransformFVector(p->pos);
	
				pVerts->Point.X	= Center.X;
				pVerts->Point.Y	= Center.Y + p->Size;
				pVerts->Point.Z	= Center.Z;
				pVerts->Color	= Color;
				pVerts->U		= U1;
				pVerts->V		= V1;
				pVerts++;

				pVerts->Point.X	= Center.X - p->Size;
				pVerts->Point.Y	= Center.Y;
				pVerts->Point.Z	= Center.Z;
				pVerts->Color	= Color;
				pVerts->U		= U1;
				pVerts->V		= V2;
				pVerts++;

				pVerts->Point.X	= Center.X;
				pVerts->Point.Y	= Center.Y - p->Size;
				pVerts->Point.Z	= Center.Z;
				pVerts->Color	= Color;
				pVerts->U		= U2;
				pVerts->V		= V2;
				pVerts++;

				pVerts->Point.X	= Center.X + p->Size;
				pVerts->Point.Y	= Center.Y;
				pVerts->Point.Z	= Center.Z;
				pVerts->Color	= Color;
				pVerts->U		= U2;
				pVerts->V		= V1;
				pVerts++;
			}

			p++;
		}
	}

#ifdef _XBOX
   /* scion ======================================================================
	* FillPushBuffer
	* Author: jg
	* 
	* Builds the weather particles directly into the push buffer
	*
	* input:	pPushBuffer - the pointer to start writing push buffer information
	*
	* ============================================================================
	*/
	DWORD* FillPushBuffer(DWORD* pPushBuffer)
	{
		DynamicVertex*	pVerts	= (DynamicVertex*)pPushBuffer;
		FWeatherPcl*	p		= &pFx->pcl(0);

		// Encode the primitive type and begin command into the push buffer
		*pPushBuffer++ = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END,1);
		*pPushBuffer++ = PT_QuadList;
		// The number of DWORDs each vert takes
		const DWORD dwDwordsPerVert = sizeof(DynamicVertex) / sizeof(DWORD);
		// This is the number of DWORDs we encode in a given inline array push
		// NOTE: It must break on primitive alignment
		const DWORD dwMaxInlineArray = (D3DPUSH_MAX_COUNT /
			sizeof(DynamicVertex)) * dwDwordsPerVert * 4;
		// Determines how many DWORDs we've pushed into the push buffer
		DWORD dwDwordsEncoded = 0;
		// How many particles are left
		DWORD dwRemaining = Renderable / 4;

		int numPcl = pFx->pcl.Num();
		for( int i=0; i<numPcl; i++ )
		{
			if( !p->Visible )
			{
				p++;
				continue;
			}

			// Check to see if we need to encode a inline array push
			if (dwDwordsEncoded % dwMaxInlineArray == 0)
			{
				// Figure out how many dwords we'll need for each particle
				DWORD dwNumThisBatch = Min<DWORD>(dwRemaining *
					sizeof(DynamicVertex),dwMaxInlineArray);
				// Build a new batch of verts to be processed
				*pPushBuffer++ = D3DPUSH_ENCODE(
					D3DPUSH_NOINCREMENT_FLAG | D3DPUSH_INLINE_ARRAY,
					dwNumThisBatch);
				// Make the vertex be at the correct position in the push buffer
				pVerts = (DynamicVertex*)pPushBuffer;
			}

			FLOAT	t	= Clamp( 255.f * LerpInOut( 0.05f, p->Life ) * Max( p->Size * 0.5f, 1.0f ) * p->DistAtten, 0.f, 255.f );
						
			INT		Col = p->frame / tmp_numCols,
					Row = p->frame - tmp_numRows * Col;
	
			FLOAT	U1	= Row * tmp_texU,
					U2	= U1  + tmp_texU,
					V1	= Col * tmp_texV,
					V2	= V1  + tmp_texV;
	
			INT		Alpha, 
					Attenuation;

			Alpha = appRound( t );
			Attenuation = Alpha;

			DWORD Color = Attenuation | (Attenuation << 8) | (Attenuation << 16) | (Alpha << 24);

			if ( pFx->WeatherType == WT_Rain )
			{
				FVector Start	= WorldToCamera.TransformFVector(p->pos);
				FVector End	= WorldToCamera.TransformFVector(p->pos + p->Vel*.03f);

				pVerts->Point.X	= Start.X + p->Size;
				pVerts->Point.Y	= Start.Y;
				pVerts->Point.Z	= Start.Z;
				pVerts->Color	= Color;
				pVerts->U		= U2;
				pVerts->V		= V1;
				pVerts++;

				pVerts->Point.X	= Start.X - p->Size;
				pVerts->Point.Y	= Start.Y;
				pVerts->Point.Z	= Start.Z;
				pVerts->Color	= Color;
				pVerts->U		= U1;
				pVerts->V		= V1;
				pVerts++;

				pVerts->Point.X	= End.X - p->Size;
				pVerts->Point.Y	= End.Y;
				pVerts->Point.Z	= End.Z;
				pVerts->Color	= Color;
				pVerts->U		= U1;
				pVerts->V		= V2;
				pVerts++;

				pVerts->Point.X	= End.X + p->Size;
				pVerts->Point.Y	= End.Y;
				pVerts->Point.Z	= End.Z;
				pVerts->Color	= Color;
				pVerts->U		= U2;
				pVerts->V		= V2;
				pVerts++;
			}
			else
			{				
				FVector Center	= WorldToCamera.TransformFVector(p->pos);
	
				pVerts->Point.X	= Center.X;
				pVerts->Point.Y	= Center.Y + p->Size;
				pVerts->Point.Z	= Center.Z;
				pVerts->Color	= Color;
				pVerts->U		= U1;
				pVerts->V		= V1;
				pVerts++;

				pVerts->Point.X	= Center.X - p->Size;
				pVerts->Point.Y	= Center.Y;
				pVerts->Point.Z	= Center.Z;
				pVerts->Color	= Color;
				pVerts->U		= U1;
				pVerts->V		= V2;
				pVerts++;

				pVerts->Point.X	= Center.X;
				pVerts->Point.Y	= Center.Y - p->Size;
				pVerts->Point.Z	= Center.Z;
				pVerts->Color	= Color;
				pVerts->U		= U2;
				pVerts->V		= V2;
				pVerts++;

				pVerts->Point.X	= Center.X + p->Size;
				pVerts->Point.Y	= Center.Y;
				pVerts->Point.Z	= Center.Z;
				pVerts->Color	= Color;
				pVerts->U		= U2;
				pVerts->V		= V1;
				pVerts++;
			}
			// Keep the pushbuffer pointer in sync
			pPushBuffer += sizeof(DynamicVertex);
			// Update the number of dwords encoded in the push buffer
			dwDwordsEncoded += sizeof(DynamicVertex);
			// One down, N number to go
			dwRemaining--;

			p++;
		}
		// Encode the end command into the push buffer
		*pPushBuffer++ = D3DPUSH_ENCODE(D3DPUSH_SET_BEGIN_END,1);
		*pPushBuffer++ = 0;
		// Return our out param
		return pPushBuffer;
	}
#endif

	virtual void GetRawStreamData(void ** Dest, INT FirstVertex )
	{
		*Dest = NULL;
	}
};

static FWeatherQuadStream weatherStream;

void AxWeatherEffect::CacheBlockers()
{
	// will this work in the postload?
	pclBlockers.Empty();
	checkSlow(XLevel);
	for( int i=0; i<XLevel->Actors.Num(); i++ )
		if (XLevel->Actors(i) && XLevel->Actors(i)->IsA(AVolume::StaticClass()))
			if( XLevel->Actors(i)->Tag == Tag ) // should be 'xWeatherEffect'
				pclBlockers.AddItem( (AVolume*)XLevel->Actors(i) );
}

void AxWeatherEffect::UpdateViewer( FLevelSceneNode* SceneNode )
{
	FLOAT DeltaTime = Clamp<FLOAT>( GetLevel()->TimeSeconds - LastRenderTime, 0.0001f, 10.f );
	LastRenderTime	= GetLevel()->TimeSeconds;

	// calc velocity
	eyeDir		= FVector(SceneNode->CameraToWorld.M[2][0], SceneNode->CameraToWorld.M[2][1], SceneNode->CameraToWorld.M[2][2]);
	eyeMoveVec	= SceneNode->ViewOrigin - eyePos;
	eyePos		= SceneNode->ViewOrigin;
	eyeVel		= eyeMoveVec.Size();
	if ( eyeVel > 0.001f )
	{
		eyeMoveVec *= 1.0f / eyeVel;
	}
	else
	{
		eyeVel		= 0.0f;
		eyeMoveVec	= FVector(0,0,0);
	}
	
	// move the spawn origin according to velocity and fudge
	spawnOrigin = SceneNode->ViewOrigin;    
	spawnVecU	= FVector( 280, 0, 0 );
	spawnVecV	= FVector( 0, 0, 280 );

	// extrapolate next eye position and bias spawn positions in that dir
	if ( eyeVel > 0.0f )
		spawnOrigin += eyeMoveVec * Clamp( eyeVel * (1.0f / DeltaTime), 0.f, 300.f );

	if ( PhysicsVolume && PhysicsVolume->Brush )
	{
		if ( !PhysicsVolume->IsA(ADefaultPhysicsVolume::StaticClass()) )
		{
			FCheckResult Hit(0);
			// check for containment
			if ( PhysicsVolume->Brush->PointCheck(Hit,PhysicsVolume,spawnOrigin,FVector(0,0,0),0)!=0 )
			{
				// if not contained, find nearest point on the volume for spawn origin
				FCheckResult Hit(0);
				FVector Start	= spawnOrigin,
						End		= Location;
				if( PhysicsVolume->Brush->LineCheck(Hit,PhysicsVolume, NULL, End,Start,FVector(0,0,0),0,0)==0 )
					spawnOrigin = Hit.Location;
			}
		}
	}
}

void AxWeatherEffect::SetZone( UBOOL bTest, UBOOL bForceRefresh )
{
	Super::SetZone(bTest,bForceRefresh);
/*
	if ( GIsEditor && XLevel )
	{
		APhysicsVolume* NewVolume = PhysicsVolume;
		for ( INT i=0; i<XLevel->Actors.Num(); i++ )
		{
			APhysicsVolume *Next = Cast<APhysicsVolume>(XLevel->Actors(i));
			if ( Next && Next->Encompasses(Location) && (Next->Priority > NewVolume->Priority) )
				NewVolume = Next;
		}
		PhysicsVolume = NewVolume;
	}

	CacheBlockers();
*/
}

void AxWeatherEffect::PreCalc()
{
	if ( pcl.Num() != numParticles )
	{
		pcl.Empty();
		pcl.AddZeroed( numParticles );
	}

	eyePos		= Location;
	spawnOrigin = Location;
   	numFrames	= (int)(numCols * numRows);
	texU		= 1.0f / numCols;
	texV		= 1.0f / numRows;

	if ( XLevel )
		CacheBlockers();
}

void AxWeatherEffect::PostEditLoad()
{
	SetZone(0,0);
	PreCalc();
	CacheBlockers();
}

void AxWeatherEffect::PostEditChange()
{
	Super::PostEditChange();
	PreCalc();
	SetZone(0,0);
}

void AxWeatherEffect::PostLoad()
{
	Super::PostLoad();
	PreCalc();
}

void AxWeatherEffect::Destroy()
{
	Super::Destroy();
}

void AxWeatherEffect::Spawned()
{
	Super::Spawned();
	PreCalc();
	for ( int i=0; i<pcl.Num(); i++ )
		InitParticle( pcl(i) );
}

FORCEINLINE void AxWeatherEffect::InitParticle( FWeatherPcl& t )
{
	t.pos			= spawnOrigin + Position.GetRand();
	t.Vel			= spawnVel * Speed.GetRand();
	t.Life			= 1.0f;
	FLOAT LifeSecs	= Life.GetRand();
	t.InvLifeSpan	= 1.0f / LifeSecs;
	t.Size			= Size.GetRand();
	t.HitTime		= 0.0f;
	t.frame			= qRand() % (int)numFrames;

	for( int i=0; i<pclBlockers.Num(); i++ )
	{
		// this does not support rotated volumes for extra speed
		FCheckResult Hit(0);
		FVector Start	= t.pos - pclBlockers(i)->Location + pclBlockers(i)->PrePivot,
				End		= Start + t.Vel * LifeSecs;
		if ( pclBlockers(i)->Brush->LineCheck(Hit,NULL,NULL,End,Start,FVector(0,0,0),0,0)==0 )
		{
			t.HitTime = 1.0f - Hit.Time;
			break; // this is questionable, should check for closer impacts, but I don't want to slow this down anymore
		}
	}
}

UBOOL AxWeatherEffect::Tick( FLOAT deltaTime, ELevelTick TickType )
{
	guard(AxWeatherEffect::Tick);
	FUNC_CLOCK(SetupCyc,GStats.DWORDStats( GEngineStats.STATS_Particle_SpriteSetupCycles ));

	if ( bHidden || 
		(Level->NetMode == NM_DedicatedServer) || 
		(DetailLevel > Level->DetailMode) )
	{
		return Super::Tick( deltaTime, TickType );
	}


	Box.Min		= Location;
	Box.Max		= Location;
	numActive	= 0;
	
	FWeatherPcl*	p				= &pcl(0);
	FLOAT			invEyeDistSqr	= 1.0f / Square(maxPclEyeDist);
	INT				numPcl			= pcl.Num();
	for( INT i=0; i<numPcl; i++ )
	{
		p->Life -= deltaTime * p->InvLifeSpan;
	    
		// recycle expired
		if ( p->Life <= 0.0f )
		{
			p->pos			= spawnOrigin + Position.GetRand();
			p->Vel			= spawnVel * Speed.GetRand();
			p->Life			= 1.0f;
			FLOAT LifeSecs	= Life.GetRand();
			p->InvLifeSpan	= 1.0f / LifeSecs;
			p->Size			= Size.GetRand();
			p->HitTime		= 0.0f;
			p->frame		= qRand() % (int)numFrames;

			for ( int j=0; j<pclBlockers.Num(); j++ )
			{
				// this does not support rotated volumes for extra speed
				FCheckResult Hit(0);
				FVector Start	= p->pos - pclBlockers(j)->Location + pclBlockers(j)->PrePivot,
						End		= Start + p->Vel * LifeSecs;
				if ( pclBlockers(j)->Brush->LineCheck(Hit,NULL,NULL,End,Start,FVector(0,0,0),0,0)==0 )
				{
					p->HitTime = 1.0f - Hit.Time;
					break; // this is questionable, should check for closer impacts, but I don't want to slow this down anymore
				}
			}
       		numActive++;
			Box += p->pos;
			p++;
			continue;
		}

		// skip updates for particles in collision stasis
		if ( p->Life < p->HitTime )
		{
			numActive++;
			p++;
			continue;
		}

		// do update
		p->pos += p->Vel * deltaTime;
		p->DistAtten = 1.0f - ((p->pos - eyePos).SizeSquared()*invEyeDistSqr);
		if ( p->DistAtten <= 0.0f )
			p->Life = 0.0f;

		// noise
		if ( WeatherType == WT_Snow && qFRand() <= deviation)
		{
			float burst = 20.0f;
			p->Vel.X += -burst + qFRand() * ( burst + burst );
			p->Vel.Y += -burst + qFRand() * ( burst + burst );
			if ( Abs(p->Vel.X) > 40.0f )
				p->Vel.X *= 0.75f;
			if ( Abs(p->Vel.Y) > 40.0f )
				p->Vel.Y *= 0.75f;
		}

		numActive++;
		Box += p->pos;
		p++;
	}

	unguard;

	return Super::Tick( deltaTime, TickType );
	
}

void AxWeatherEffect::Render( FLevelSceneNode* SceneNode, FRenderInterface* RI )
{
	if ( SceneNode->Viewport->IsOrtho() )
		return;

	FUNC_CLOCK(RenCyc,GStats.DWORDStats( GEngineStats.STATS_Particle_RenderCycles ));

	UpdateViewer( SceneNode );
	LastRenderTime = GetLevel()->TimeSeconds;

	if( !GIsEditor )
		GetLevel()->FarMoveActor( this, SceneNode->ViewOrigin);

	if ( Skins.Num() == 0 || !Skins(0) || !SceneNode->Viewport->bShowWeather )
		return;

	tmp_texU			= texU;
	tmp_texV			= texV;
	tmp_numCols			= numCols;
	tmp_numRows			= numRows;

	RI->SetMaterial( Skins(0) );
	RI->EnableLighting(0);

	RI->SetTransform(TT_WorldToCamera,FMatrix::Identity);
	RI->SetTransform(TT_LocalToWorld, FMatrix::Identity);

	// cull behind particles, need to first determine 'renderable' amount for dynamic buffer lock.
	FPlane			eyePlane( SceneNode->ViewOrigin, eyeDir);
	INT				NumVisible	= 0;
	FWeatherPcl*	p			= &pcl(0);
	
	int pclNum = pcl.Num();
	for( INT i=0; i<pclNum; i++ )
	{
		if( p->Life <= 0.0f || eyePlane.PlaneDot( p->pos ) < 0.0f || (p->Life < p->HitTime) )
		{
			p->Visible = 0;
		}
		else
		{
			p->Visible = 1;
			NumVisible++;
		}
		p++;
	}

	weatherStream.Init(this,4*Min(NumVisible,5400),SceneNode->WorldToCamera);

#ifdef _XBOX
	BEGIN_D3D_EVENT(BDE)(PIX_PARTICLE,TEXT("%s"),GetName());
	//scion jg -- Build these verts directly into the push buffer
	// Figure out how big of a push buffer we'll need. We 5 DWORDs for the
	// basic push buffer command, plus the size of the particles and any
	// extra inline array commands
	DWORD dwParticleSize = sizeof(DynamicVertex) * weatherStream.Renderable;
	DWORD dwSizeNeeded = 5 * sizeof(DWORD) + dwParticleSize +
		(dwParticleSize / (D3DPUSH_MAX_COUNT / sizeof(DynamicVertex)) *
		sizeof(DWORD));
	// Request our chunk of push buffer
	DWORD* pPushBuffer = SceneNode->Viewport->RI->BeginPush(dwSizeNeeded,
		VS_FixedFunction,&weatherStream);
	if (pPushBuffer != NULL)
	{
		// Fill the buffer and release our hold on it
		SceneNode->Viewport->RI->EndPush(
			weatherStream.FillPushBuffer(pPushBuffer));
	}
	END_D3D_EVENT;
#else
	INT	FirstVertex = SceneNode->Viewport->RI->SetDynamicStream(VS_FixedFunction,&weatherStream);
	static const INT VerticesPerParticle = 4; 
	FRawIndexBuffer	weatherIndices;
	weatherIndices.Indices.Add( weatherStream.Renderable / 4 * 6 );
	_WORD* VBIndex = &weatherIndices.Indices(0);

	for (INT point=0; point < weatherStream.Renderable / 4 - 1; point++)
	{
		// first poly
		*(VBIndex++) = 0 + point*VerticesPerParticle; // TL
		*(VBIndex++) = 1 + point*VerticesPerParticle; // BL
		*(VBIndex++) = 2 + point*VerticesPerParticle; // TR

		// second poly
		*(VBIndex++) = 0 + point*VerticesPerParticle; // BL
		*(VBIndex++) = 2 + point*VerticesPerParticle; // BR
		*(VBIndex++) = 3 + point*VerticesPerParticle; // TR
	}

	INT	BaseIndex = RI->SetDynamicIndexBuffer(&weatherIndices,FirstVertex);
	SceneNode->Viewport->RI->DrawPrimitive(	PT_TriangleList, 
		BaseIndex,	weatherStream.Renderable / 2, 0, weatherStream.Renderable * VerticesPerParticle - 1 );
#endif 

	// Reset the camera transform.
	RI->SetTransform(TT_WorldToCamera, SceneNode->WorldToCamera);

	STAT_INC_BY(GStats.DWORDStats( GEngineStats.STATS_Particle_Particles	),NumVisible);
}

IMPLEMENT_CLASS(AxWeatherEffect);
