/* scion ======================================================================
* Author: sz
* ============================================================================
* (moved GetRelevantLights implementation used to reside in UnRenderVisibility.cpp)
*
* scion sz -
* This file contains the light relevancy/visibility/merging code needed
* to dynamically light an actor
* ============================================================================
*/

#include "EnginePrivate.h"
#include "UnRenderPrivate.h"

#ifndef FLT_MAX
#define FLT_MAX         3.402823466e+38F        /* max value */
#endif

#if 0
#ifdef CLOCK
#undef CLOCK
#define CLOCK(Name,Stat)
#endif
#ifdef UNCLOCK
#undef UNCLOCK
#define UNCLOCK()
#endif
#endif

/* scion ======================================================================
* FActorLightCache
* Author:
* 
* scion sz -
* The light cache stores the most recent lights that are relevant to the actor.
* Basically just stores visibility info for each light.
* =============================================================================
*/
struct FActorLightCache
{
	struct FActorLightInfluence
	{
		AActor*			LightActor;			// actual light actor
		DOUBLE			TraceTime;			// last time a visibility trace was done
		UBOOL			Visible;			// is currently visible
		FLOAT			Alpha;				// to fade light

		FActorLightInfluence(){}

		FActorLightInfluence( AActor* InLightActor )
		{
			LightActor = InLightActor;
			TraceTime = 0;
			Visible = 0;
			Alpha=0.f;
		}
	};

	enum 
	{ 
		MAX_CACHE_LIGHT_INFLUENCES = 8,	// Max entries in Lights[]		
	};

	// list of lights that are currently relevant to the actor (ie. sort keys > 0)
	FActorLightInfluence	Lights[MAX_CACHE_LIGHT_INFLUENCES];
	INT						NumLights;
	// last update time for this actor's lights
	DOUBLE					LastUpdateTime;			
	
	void Init()
	{
		NumLights = 0;
		appMemzero( Lights, sizeof(Lights) );
		LastUpdateTime = 0.f;
	}

	// find an existing LightActor in Lights[]
	FORCEINLINE FActorLightInfluence* FindEntry( AActor* LightActor )
	{
		guardSlow(FActorLightCache::FindEntry);

		FActorLightInfluence* FoundEntry = NULL;
		for( INT Index=0; Index < NumLights; Index++ )
		{
            if( Lights[Index].LightActor == LightActor )
			{
				FoundEntry = &Lights[Index];
				break;
			}
		}
		return FoundEntry;

		unguardSlow;
	}

	// set entry in Lights[] using existing Entry
	FORCEINLINE void SetEntry( INT Index, FActorLightInfluence* Entry )
	{
		guardSlow(FActorLightCache::SetEntry);
		checkSlow( Index < MAX_CACHE_LIGHT_INFLUENCES && Entry );

		appMemcpy( &Lights[Index], Entry, sizeof(FActorLightInfluence) );

		unguardSlow;
	}

	// set entry in Lights[] using LightActor
	FORCEINLINE void SetEntry( INT Index, AActor* LightActor )
	{
		guardSlow(FActorLightCache::SetEntry);
		checkSlow( Index < MAX_CACHE_LIGHT_INFLUENCES && LightActor );

		FActorLightInfluence TempEntry(LightActor);
		appMemcpy( &Lights[Index], &TempEntry, sizeof(FActorLightInfluence) );

		unguardSlow;
	}

	// copy array of FActorLightInfluence* into Lights[]
	FORCEINLINE void CopyNewLights( FActorLightInfluence* NewLights, INT NewNumLights )
	{
		guardSlow(FActorLightCache::CopyNewLights);
		checkSlow( NewNumLights <= MAX_CACHE_LIGHT_INFLUENCES );

		if( NewLights && NewNumLights )
		{
			appMemcpy( Lights, NewLights, NewNumLights*sizeof(FActorLightInfluence) );
		}
		else if( NewNumLights == 0 )
		{
			appMemzero( Lights, sizeof(Lights) );
		}
		NumLights = NewNumLights;

		unguardSlow;
	}

	// cleanup non existant lights in the light cache
	FORCEINLINE void RemoveInvalidActorEntries()
	{
		guardSlow(FActorLightCache::RemoveInvalidActorEntries);

		INT NumStillActive = 0;
		AActor* LightActor;
		for( INT LightIdx=0; LightIdx < NumLights; LightIdx++ )
		{
			LightActor = Lights[LightIdx].LightActor;
			if( LightActor && 
				/*
				LightActor == UObject::GetIndexedObject( Lights[LightIdx].Index ) && 
				LightActor->GetClass() == Lights[LightIdx].Class &&
				*/
				!LightActor->bDeleteMe &&
				LightActor->GetLightRenderData() )
			{
				if( LightIdx != NumStillActive )
				{
					Lights[NumStillActive] = Lights[LightIdx];
				}
				NumStillActive++;
			}
			else
			{
				Lights[LightIdx].LightActor = NULL;
			}
		}
		NumLights = NumStillActive;

		unguardSlow;
	}	
};

FORCEINLINE INT Compare(FDynamicLight* A,FDynamicLight* B)
{
	return B->SortKey - A->SortKey;
}

FORCEINLINE INT Compare(FActorLightCache::FActorLightInfluence& A,FActorLightCache::FActorLightInfluence& B)
{
	return Compare( A.LightActor->GetLightRenderData(), B.LightActor->GetLightRenderData() );
}

struct FConsiderLight
{
    FDynamicLight* DynamicLight;
	FActorLightCache::FActorLightInfluence* ExistingEntry;
};

FORCEINLINE INT Compare(FConsiderLight& A,FConsiderLight& B)
{
	return Compare( A.DynamicLight, B.DynamicLight );
}

/* scion ======================================================================
* ::CalcSortKey
* Author: Epic
* 
* scion sz -
* Uses ratio of light distance to light radius with light brightness to
* calculate sort the key for a light.  Higher values mean stronger light
* influences.  A sort key < 0 means that the light is out of range or its
* brightness is 0.  Sunlights should always have the highest sort keys.
* =============================================================================
*/
FORCEINLINE void CalcSortKey(FDynamicLight* DynamicLight,FDynamicActor* DynamicActor,FSphere& RealBoundingSphere)
{ 
	AActor*	LightActor = DynamicLight->Actor;
	checkSlow(LightActor);

	if(LightActor->LightEffect == LE_Sunlight)
	{
		DynamicLight->SortKey = (MAXINT>>1) - Max(1024.f - LightActor->LightBrightness, 0.f);  
	}
// just treat spotlights as point lights
#if 0
	else if(LightActor->LightEffect == LE_Spotlight || LightActor->LightEffect == LE_StaticSpot)
	{		
		FLOAT SpotDot = (RealBoundingSphere - DynamicLight->Position).SafeNormal() | DynamicLight->Direction;
		if( SpotDot > 0 && SpotDot > Square(1.f-DynamicLight->LightCone/256.f) ) 
		{	
			DynamicLight->SortKey = appRound((1.0f - (DynamicLight->Position - RealBoundingSphere).SizeSquared() / Square(DynamicLight->Radius+RealBoundingSphere.W)) * LightActor->LightBrightness * 1024);			
		}
		else
		{
			DynamicLight->SortKey = 0;
		}
	}
#endif
	else
	{
		DynamicLight->SortKey = appRound((1.0f - (DynamicLight->Position - RealBoundingSphere).SizeSquared() / Square(DynamicLight->Radius+RealBoundingSphere.W)) * LightActor->LightBrightness * 1024);
	}
}

void ShowLightDebugInfo( FSceneNode* SceneNode, FDynamicActor* DynamicActor, FActorLightCache* LightCache );

/* scion ======================================================================
* ::GetRelevantLights
* Author: sz/Epic
* 
* =============================================================================
*/
TList<FDynamicLight*>* GetRelevantLights(FSceneNode* SceneNode,FDynamicActor* DynamicActor,FSphere& RealBoundingSphere,FDynamicLight** Consider,INT NumConsider)
{
	guard(GetRelevantLights);
	FUNC_CLOCK(MeshLightCycles,GStats.DWORDStats(GEngineStats.STATS_Visibility_MeshLightCycles));
	check( DynamicActor && DynamicActor->Actor );
	static INT MaxLights=VS_Lights_Directional_MAX+VS_Lights_Point_MAX;
	static FLOAT LIGHT_MAX_ELAPSED_TIME				=1.0f;
	static FLOAT LIGHT_VISIBILITY_UPDATE_TIME		=0.17f;		// elapsed time per light before it is checked for visibility
	static FLOAT LIGHT_BLEND_RATE					=0.07f;

	AActor*					RenderActor = DynamicActor->Actor;	
	FDynamicLight*			DynamicLight = NULL;
	DOUBLE					CurrentTime = SceneNode->Viewport->CurrentTime;

	// get the light cache entry for this RenderActor or make a new one
	FActorLightCache* LightCache = NULL;
	FCacheItem*	CacheItem = NULL;
	QWORD CacheId = 0;
	CacheId = MakeCacheID(CID_ActorLightCache,RenderActor);	
	LightCache = (FActorLightCache*)GCache.Get(CacheId,CacheItem);
	if(!LightCache)
	{
		LightCache = (FActorLightCache*)GCache.Create(CacheId,CacheItem,sizeof(FActorLightCache));
		LightCache->Init();		
	}

	// only update actors that need their lighting changed
	if( DynamicActor->NeedsLightUpdate )
	{
		guard(LIGHT_UPDATE_TIME);
		// list of new relevant lights (any light with a sort key > 0 goes in here)	
		static TArray<FConsiderLight> NewLights;
		NewLights.Reset();

		// calc sort keys for lights to be considered
		// add any with a sort key > 0 to NewLights[]
		for( INT ConsiderIndex = 0; ConsiderIndex < NumConsider; ConsiderIndex++ )	
		{	
			CalcSortKey( Consider[ConsiderIndex],DynamicActor,RealBoundingSphere );				
			if( Consider[ConsiderIndex]->SortKey > 0 )
			{
				NewLights.Add();
				NewLights.Last().DynamicLight = Consider[ConsiderIndex];
				NewLights.Last().ExistingEntry = LightCache->FindEntry( Consider[ConsiderIndex]->Actor );
			}
		}

		// sort NewLights[]
		Sort( (FConsiderLight*)NewLights.GetData(), NewLights.Num() );

		// replace light cache entries with new lights
		static TArray<FActorLightCache::FActorLightInfluence> NewLightCacheEntries;
		NewLightCacheEntries.Reset();
		for( INT NewLightIdx=0; 
			NewLightIdx < NewLights.Num() && 
			NewLightCacheEntries.Num() < FActorLightCache::MAX_CACHE_LIGHT_INFLUENCES; 
		NewLightIdx++ )
		{
			// try to find an existing entry in the light cache for this light
			FActorLightCache::FActorLightInfluence* ExistingEntry = NewLights(NewLightIdx).ExistingEntry;
			if( ExistingEntry )
			{
				// since we found an existing entry we can reuse the visibility and trace time
				NewLightCacheEntries.AddItem( *ExistingEntry );
			}
			else
			{
				// if we didn't find an entry then just create a new one with defaults
				NewLightCacheEntries.AddItem( FActorLightCache::FActorLightInfluence( NewLights(NewLightIdx).DynamicLight->Actor ) );
			}				
		}
		// copy new entries to the light cache array
		LightCache->CopyNewLights( (FActorLightCache::FActorLightInfluence*)NewLightCacheEntries.GetData(), NewLightCacheEntries.Num() );
		unguard;

		guard(LIGHT_VISIBILITY_UPDATE_TIME);
		if( !RenderActor->bLightingVisibility )
		{
			// assume all lights are visible if we don't want to check visibility
			for( INT LightCacheIndex=0; LightCacheIndex < LightCache->NumLights && LightCacheIndex < MaxLights; LightCacheIndex++ )
			{
				FActorLightCache::FActorLightInfluence&	LightCacheEntry = LightCache->Lights[LightCacheIndex];
				LightCacheEntry.Visible = 1;
				LightCacheEntry.TraceTime = CurrentTime;
			}
		}
		else
		{
			// bias visibility update time based on distance lod factor
			// actors that are further away have less frequent lighting traces
			const FLOAT MAX_TRACETIME_BIAS = 5.f;
			FLOAT TraceTimeBias = 1.f / Clamp<FLOAT>( DynamicActor->DistanceLODFactor, 1.f/MAX_TRACETIME_BIAS, 1.0f );
			TraceTimeBias *= RenderActor->Level->bDropDetail ? 1.5f : 1.0f;
			// update visibility for lights		
			INT NumLightsVisible=0;
			for( INT LightCacheIndex=0; LightCacheIndex < LightCache->NumLights && NumLightsVisible < MaxLights; LightCacheIndex++ )
			{
				FActorLightCache::FActorLightInfluence&	LightCacheEntry = LightCache->Lights[LightCacheIndex];
				DynamicLight = LightCacheEntry.LightActor->GetLightRenderData();
				// only update visibility in LIGHT_VISIBILITY_UPDATE_TIME intervals
				if( (LightCacheEntry.TraceTime + LIGHT_VISIBILITY_UPDATE_TIME*TraceTimeBias) <= CurrentTime )
				{
					static FVector		TraceStart,TraceEnd;
					static FCheckResult	Hit;
					TraceStart = RealBoundingSphere;
					if( LightCacheEntry.LightActor->LightEffect == LE_Sunlight )
					{
						TraceEnd = RealBoundingSphere + (-DynamicLight->Direction * 10000.f);
					}
					else
					{
						TraceEnd = DynamicLight->Position;
					}
					// trace against all geometry
					LightCacheEntry.Visible = SceneNode->Viewport->Actor->XLevel->SingleLineCheck( Hit,
						RenderActor,TraceEnd,TraceStart, TRACE_ShadowCast|TRACE_World|TRACE_StopAtFirstHit );
					LightCacheEntry.TraceTime = CurrentTime;
					NumLightsVisible += LightCacheEntry.Visible;
				}
			}
		}
		
		unguard;

		// disable fading for single pass actors
		if( RenderActor->bSinglePassLighting ||
			(LightCache->LastUpdateTime + LIGHT_MAX_ELAPSED_TIME) <= CurrentTime )
		{
			if( RenderActor->bSinglePassLighting ) {
				DynamicActor->NeedsLightUpdate = 0;
			}
			for( INT LightCacheIndex=0; LightCacheIndex < LightCache->NumLights; LightCacheIndex++ )
			{
				FActorLightCache::FActorLightInfluence&	LightCacheEntry = LightCache->Lights[LightCacheIndex];
				LightCacheEntry.Alpha = 1.f * (FLOAT)LightCacheEntry.Visible;
			}
		}
		LightCache->LastUpdateTime = CurrentTime;
	}

	TList<FDynamicLight*>*	ActorLights	= NULL;	
	TList<FDynamicLight*>*	ActorLightsTail	= NULL;		
	INT NumLightsUsed=0;
	for( INT LightCacheIndex=0; LightCacheIndex < LightCache->NumLights && NumLightsUsed < MaxLights; LightCacheIndex++ )
	{
		FActorLightCache::FActorLightInfluence&	LightCacheEntry = LightCache->Lights[LightCacheIndex];
		DynamicLight = LightCacheEntry.LightActor->GetLightRenderData();
		// don't need to blend single pass lit actors
		if( !RenderActor->bSinglePassLighting )
		{
			// blend in visible light
			if( LightCacheEntry.Visible )
			{
				LightCacheEntry.Alpha = Min<FLOAT>( 1.0f, LightCacheEntry.Alpha + LIGHT_BLEND_RATE );
				// dynamic lights should blend in instantly
				if( DynamicLight->Dynamic ) 
				{
					LightCacheEntry.Alpha = 1.f;
				}

			}
			// blend out not visible light
			else if( !LightCacheEntry.Visible )
			{
				LightCacheEntry.Alpha = Max<FLOAT>( 0.0f, LightCacheEntry.Alpha - LIGHT_BLEND_RATE );
			}
		}

		// try to find a visible light
		if( LightCacheEntry.Alpha == 0.f )
			continue;

		// light Alpha modulates its final color
		DynamicLight->Alpha = LightCacheEntry.Alpha;		

		// add to tail of output linked list of lights
		if( !ActorLights )
		{
			ActorLights = AddLightListEntry( DynamicLight, NULL );
			ActorLightsTail = ActorLights;
		}
		else
		{
			ActorLightsTail->Next = AddLightListEntry( DynamicLight, NULL );
			ActorLightsTail = ActorLightsTail->Next;
		}

		// keep track of actors referencing dynamic lights in case they are destroyed
		if( DynamicLight->Dynamic )
		{
			DynamicLight->AddActorRef(DynamicActor);
		}

		NumLightsUsed++;
	}


#ifndef RELEASE_FINAL	
	// debug text
	ShowLightDebugInfo( SceneNode, DynamicActor, LightCache );
	// show lines to lights
	if( SceneNode->Viewport->Actor->RendMap == REN_LightingOnly &&
		DynamicActor->Actor->IsA(APawn::StaticClass()) )
	{	
		INT TotalPtLts=0;
		INT TotalDirLts=0;
		static INT MaxDirLts = VS_Lights_Directional_MAX;
		static INT MaxPtLts = VS_Lights_Point_MAX;

		FLineBatcher LineBatcher( SceneNode->Viewport->RI );
		for( TList<FDynamicLight*>* LightList = ActorLights; LightList; LightList = LightList->Next )
		{
			FDynamicLight* DynamicLight = LightList->Element;
			if( !DynamicLight )
				continue;

			if( DynamicLight->IsSunLight() &&
				TotalDirLts < MaxDirLts )
			{
				TotalDirLts++;
				LineBatcher.DrawLine( RealBoundingSphere,
					RealBoundingSphere + (-DynamicLight->Direction * 10000.f), DynamicLight->Color*DynamicLight->Alpha );
			}
			else if( DynamicLight->LightEffect != LE_Ambient )
			{
                if( TotalPtLts < MaxPtLts )
				{
					TotalPtLts++;
					LineBatcher.DrawLine( RealBoundingSphere, DynamicLight->Position, DynamicLight->Color*DynamicLight->Alpha );
					LineBatcher.DrawCircle( DynamicLight->Position, 
						GMath.UnitCoords.XAxis,
						GMath.UnitCoords.ZAxis,
						DynamicLight->Color*DynamicLight->Alpha,
						50.f,
						10 );
				}
				else if( TotalDirLts < MaxDirLts )
				{
					TotalDirLts++;
					LineBatcher.DrawLine( RealBoundingSphere, DynamicLight->Position, DynamicLight->Color*DynamicLight->Alpha );
				}
			}
		}
		LineBatcher.Flush();
	}
#endif

	CacheItem->Unlock();
	// no need to keep any cached entries for the single pass lit case
	if( RenderActor->bSinglePassLighting )
	{
		GCache.Flush( CacheId );
	}	

	return ActorLights;	
	
	unguardf((*FString::Printf(TEXT("DynamicActor(0x%x)->Actor(%s)"),DynamicActor,
		DynamicActor->Actor ? DynamicActor->Actor->GetName() : TEXT("NULL"))));
}



/* scion ======================================================================
* ::ShowLightDebugInfo
* Author: 
* 
* Draws light debug info to screen
* =============================================================================
*/
#ifdef FINAL_RELEASE
void ShowLightDebugInfo( FSceneNode* SceneNode, FDynamicActor* DynamicActor, FActorLightCache* LightCache ) {}
#else
void _DrawStat(UCanvas* Canvas,FColor Color,INT X,const TCHAR* Format,...)
{
	guard(DrawStat);
	TCHAR	TempStr[4096];
	GET_VARARGS(TempStr,ARRAY_COUNT(TempStr),Format,Format);
	Canvas->Color = Color;
	Canvas->CurX = X;
	Canvas->WrappedPrintf(Canvas->MedFont,0,TempStr);
	Canvas->CurY -= 4;	
	unguard;
}
void ShowLightDebugInfo( FSceneNode* SceneNode, FDynamicActor* DynamicActor, FActorLightCache* LightCache )
{
	checkSlow( SceneNode && DynamicActor && DynamicActor->Actor && LightCache );
	AActor*	RenderActor = DynamicActor->Actor;

	// Actor's light debugging
	if( SceneNode->Viewport->Actor->GetLevel()->Engine->bShowLightStats && 
		/*RenderActor->IsA(APawn::StaticClass()) && */
		RenderActor->IsDTMesh() &&
		SceneNode && 
		SceneNode->Viewport && 
		SceneNode->Viewport->Canvas && 
		SceneNode->Viewport->LodSceneNode == SceneNode )
	{
		UCanvas* Canvas = SceneNode->Viewport->Canvas;

		Canvas->CurY += 86; //skip line

		FString	LightString;			
		LightString = FString::Printf(TEXT(" Lighting - actor:%s sphere:%3.2f,%3.2f,%3.2f,%3.2f time:%3.2f"), 
			RenderActor->GetName(), DynamicActor->BoundingSphere.X, DynamicActor->BoundingSphere.Y, DynamicActor->BoundingSphere.Z, DynamicActor->BoundingSphere.W, SceneNode->Viewport->CurrentTime );

		_DrawStat(Canvas, FColor(255,255,0),4,TEXT("%s"),*LightString); //Title string

		Canvas->CurY += 3;
		INT Number = 0;

		for( INT LightCacheIndex=0; LightCacheIndex < LightCache->NumLights; LightCacheIndex++ )
		{
			FActorLightCache::FActorLightInfluence&	LightCacheEntry = LightCache->Lights[LightCacheIndex];
			FDynamicLight*							DynamicLight = LightCacheEntry.LightActor->GetLightRenderData();

			//if( DynamicLight->SortKey <= 0 )
			//	continue;

			if( DynamicLight->Actor )
			{
				LightString = FString::Printf(TEXT("(%s) Col %1.1f %1.1f %1.1f %1.1f] v=%u"), 
					DynamicLight->Actor->GetName(), 
					DynamicLight->Color.X,
					DynamicLight->Color.Y,
					DynamicLight->Color.Z,
					DynamicLight->Alpha,
					LightCacheEntry.Visible
					);

				_DrawStat(Canvas,FColor(255,255,0),4,TEXT("%s"),*LightString);
			}
			Number++;
		}			
	}
}
#endif


