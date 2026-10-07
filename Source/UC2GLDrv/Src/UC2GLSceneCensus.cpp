/*=============================================================================
	UC2GLSceneCensus.cpp: what a level contains that the Xbox-only projector, terrain
	and shadow paths would draw differently (M7b; docs/porting/analysis/projectors.md).
	Debug only, not Epic's.

	Console: GLSCENECENSUS (runs at the start of the next frame). One line per projector
	actor (decals, shadows, static map projectors), per terrain and a summary:

	  UC2GLDrv: scene projector <class> <name> at (x,y,z) rot (p,y,r) scale s
	             tex=<material> [<class>] fb=<op> mat=<op> fov=<deg> trace=<units> datt=<scale>
	             flags=<letters> draw=<DrawType> prims=<StaticPrims> life=<s or ->
	             on: bsp=<nodes> sm=<static mesh actors> terrain=<sectors> actors=<actors> dyn=<0/1>
	  UC2GLDrv: scene terrain <name> at (x,y,z) layers=<n> [<texture>(t if transparent) ...]
	             sectors=<n> combinations=<method:layers ...> passes: weightmap=<n> combined=<n> alphamap=<n>
	  UC2GLDrv: scene summary: projectors <n> (static <n>, gradient <n>, shadows <n>, decals <n>), terrains <n>, ...

	Flag letters: B/T/S/A = projects on BSP/terrain/static meshes/actors, c = bClipBSP,
	m = bClipStaticMesh, g = bGradient, k = bProjectOnBackfaces, u = bProjectOnUnlit,
	a = bProjectOnAlpha, L = bLevelStatic, D = bDynamicAttach, s = bStatic, h = bHidden.
=============================================================================*/

#include "UC2GLDrv.h"

static const TCHAR* BlendOpName( BYTE Op )
{
	switch( Op )
	{
	case PB_None:		return TEXT("none");
	case PB_Modulate:	return TEXT("modulate");
	case PB_AlphaBlend:	return TEXT("alpha");
	case PB_Add:		return TEXT("add");
	default:			return TEXT("?");
	}
}

static UBOOL InfoIsOf( FStaticProjectorInfo* Info, FProjectorRenderInfo* RenderInfo )
{
	return Info && RenderInfo && Info->RenderInfo == RenderInfo;
}

void UC2GL_RunSceneCensus( UUC2GLRenderDevice* RenDev, UViewport* Viewport )
{
	guard(UC2GL_RunSceneCensus);
	ULevel* Level = Viewport->Actor ? Viewport->Actor->GetLevel() : NULL;
	if( !Level )
	{
		debugf( NAME_Init, TEXT("UC2GLDrv: scene census: no level") );
		return;
	}
	UClient* Client = UTexture::__Client;
	debugf( NAME_Init, TEXT("UC2GLDrv: scene census of %s (client: Decals=%i Projectors=%i; device SupportsZBIAS=%i)"),
		Level->GetOuter() ? Level->GetOuter()->GetName() : TEXT("?"),
		Client ? (INT)Client->Decals : -1, Client ? (INT)Client->Projectors : -1, (INT)RenDev->SupportsZBIAS );

	INT NumProjectors = 0, NumStatic = 0, NumGradient = 0, NumShadows = 0, NumDecals = 0, NumBatched = 0, NumClip = 0;
	INT NumTerrains = 0, NumCombined = 0, NumSectors3 = 0;
	UModel* Model = Level->Model;

	for( INT i=0; i<Level->Actors.Num(); i++ )
	{
		AProjector* P = Cast<AProjector>( Level->Actors(i) );
		if( !P || P->bDeleteMe )
			continue;
		NumProjectors++;
		FProjectorRenderInfo* RenderInfo = P->RenderInfo;

		// Where its projected geometry is.
		INT OnBsp = 0, OnSM = 0, OnTerrain = 0, OnActors = 0, Dyn = 0;
		if( RenderInfo )
		{
			if( Model )
				for( INT n=0; n<Model->Nodes.Num(); n++ )
				{
					FBspNode& Node = Model->Nodes(n);
					for( INT k=0; k<Node.Projectors.Num(); k++ )
						if( InfoIsOf( Node.Projectors(k), RenderInfo ) ) { OnBsp++; break; }
				}
			for( INT a=0; a<Level->Actors.Num(); a++ )
			{
				AActor* A = Level->Actors(a);
				if( !A )
					continue;
				UBOOL Hit = 0;
				for( INT k=0; k<A->StaticMeshProjectors.Num() && !Hit; k++ )
					if( InfoIsOf( A->StaticMeshProjectors(k), RenderInfo ) ) { OnSM++; Hit = 1; }
				for( INT k=0; k<A->Projectors.Num() && !Hit; k++ )
					if( A->Projectors(k) == RenderInfo ) { OnActors++; Hit = 1; }
				ATerrainInfo* T = Cast<ATerrainInfo>( A );
				if( T )
					for( INT s=0; s<T->Sectors.Num(); s++ )
						if( T->Sectors(s) )
							for( INT k=0; k<T->Sectors(s)->Projectors.Num(); k++ )
								if( InfoIsOf( T->Sectors(s)->Projectors(k), RenderInfo ) ) { OnTerrain++; break; }
			}
			for( INT k=0; k<Level->DynamicProjectors.Num(); k++ )
				if( Level->DynamicProjectors(k) == RenderInfo ) Dyn = 1;
		}

		FString Flags;
		if( P->bProjectBSP )			Flags += TEXT("B");
		if( P->bProjectTerrain )		Flags += TEXT("T");
		if( P->bProjectStaticMesh )		Flags += TEXT("S");
		if( P->bProjectActor )			Flags += TEXT("A");
		if( P->bClipBSP )				Flags += TEXT("c");
		if( P->bClipStaticMesh )		Flags += TEXT("m");
		if( P->bGradient )				Flags += TEXT("g");
		if( P->bProjectOnBackfaces )	Flags += TEXT("k");
		if( P->bProjectOnUnlit )		Flags += TEXT("u");
		if( P->bProjectOnAlpha )		Flags += TEXT("a");
		if( P->bLevelStatic )			Flags += TEXT("L");
		if( P->bDynamicAttach )			Flags += TEXT("D");
		if( P->bStatic )				Flags += TEXT("s");
		if( P->bHidden )				Flags += TEXT("h");

		FString Life = TEXT("-");
		if( RenderInfo && RenderInfo->Expires != 0.0 )
			Life = FString::Printf( TEXT("%.1f"), (FLOAT)(RenderInfo->Expires - Level->TimeSeconds) );

		FString Extra;
		AShadowProjector* SP = Cast<AShadowProjector>( P );
		if( SP )
		{
			NumShadows++;
			Extra = FString::Printf( TEXT(" shadow-of=%s blob=%i active=%i"), SP->ShadowActor ? SP->ShadowActor->GetName() : TEXT("None"), (INT)SP->bBlobShadow, (INT)SP->bShadowActive );
			if( SP->ShadowTexture )
				Extra += FString::Printf( TEXT(" size=%ix%i darkness=%i"), SP->ShadowTexture->UScaledSize, SP->ShadowTexture->VScaledSize, (INT)SP->ShadowTexture->ShadowDarkness );
		}
		if( appStrfind( P->GetClass()->GetName(), TEXT("Decal") ) )
			NumDecals++;
		if( P->bStatic || P->bLevelStatic )
			NumStatic++;
		if( P->bGradient )
			NumGradient++;
		if( P->bClipBSP || P->bClipStaticMesh )
			NumClip++;
		if( P->StaticPrims.Num() || P->DrawType == DT_StaticProjectorBatch )
			NumBatched++;

		debugf( NAME_Init, TEXT("UC2GLDrv: scene projector %s %s at (%.0f,%.0f,%.0f) rot (%i,%i,%i) scale %.2f tex=%s [%s] fb=%s mat=%s fov=%i trace=%i datt=%.1f flags=%s draw=%i prims=%i life=%s on: bsp=%i sm=%i terrain=%i actors=%i dyn=%i%s"),
			P->GetClass()->GetName(), P->GetName(), P->Location.X, P->Location.Y, P->Location.Z,
			P->Rotation.Pitch, P->Rotation.Yaw, P->Rotation.Roll, P->DrawScale,
			P->ProjTexture ? P->ProjTexture->GetPathName() : TEXT("None"), P->ProjTexture ? P->ProjTexture->GetClass()->GetName() : TEXT("-"),
			BlendOpName(P->FrameBufferBlendingOp), BlendOpName(P->MaterialBlendingOp), P->FOV, P->MaxTraceDistance, P->DistanceAttenScale,
			*Flags, (INT)P->DrawType, P->StaticPrims.Num(), *Life, OnBsp, OnSM, OnTerrain, OnActors, Dyn, *Extra );
	}

	static const TCHAR* MethodName[3] = { TEXT("weightmap"), TEXT("combined"), TEXT("alphamap") };
	for( INT i=0; i<Level->Actors.Num(); i++ )
	{
		ATerrainInfo* T = Cast<ATerrainInfo>( Level->Actors(i) );
		if( !T || T->bDeleteMe )
			continue;
		NumTerrains++;
		FString LayerList;
		INT Layers = 0;
		for( INT l=0; l<ARRAY_COUNT(T->Layers) && T->Layers[l].AlphaMap && T->Layers[l].Texture; l++ )
		{
			UTexture* Tex = Cast<UTexture>( T->Layers[l].Texture );
			LayerList += FString::Printf( TEXT(" %s%s"), T->Layers[l].Texture->GetName(), (!Tex || Tex->IsTransparent()) ? TEXT("(t)") : TEXT("") );
			Layers++;
		}
		FString Combos;
		for( INT c=0; c<T->RenderCombinations.Num(); c++ )
		{
			FTerrainRenderCombination& RC = T->RenderCombinations(c);
			Combos += FString::Printf( TEXT(" %s:%i"), (RC.RenderMethod >= 0 && RC.RenderMethod < 3) ? MethodName[RC.RenderMethod] : TEXT("?"), RC.Layers.Num() );
			if( RC.RenderMethod == RM_CombinedWeightMap )
				NumCombined++;
		}
		INT Passes[3] = { 0, 0, 0 }, Sectors = 0, Sectors3 = 0;
		for( INT s=0; s<T->Sectors.Num(); s++ )
		{
			UTerrainSector* S = T->Sectors(s);
			if( !S )
				continue;
			Sectors++;
			UBOOL Has3 = 0;
			for( INT p=0; p<S->RenderPasses.Num(); p++ )
			{
				INT c = S->RenderPasses(p).RenderCombinationNum;
				if( c < 0 || c >= T->RenderCombinations.Num() )
					continue;
				INT m = T->RenderCombinations(c).RenderMethod;
				if( m >= 0 && m < 3 )
					Passes[m]++;
				if( m == RM_CombinedWeightMap )
					Has3 = 1;
			}
			Sectors3 += Has3;
		}
		NumSectors3 += Sectors3;
		debugf( NAME_Init, TEXT("UC2GLDrv: scene terrain %s at (%.0f,%.0f,%.0f) layers=%i [%s ] sectors=%i (%i with a three-layer pass) combinations=[%s ] passes: weightmap=%i combined=%i alphamap=%i"),
			T->GetName(), T->Location.X, T->Location.Y, T->Location.Z, Layers, *LayerList, Sectors, Sectors3, *Combos, Passes[0], Passes[1], Passes[2] );
	}

	debugf( NAME_Init, TEXT("UC2GLDrv: scene summary: projectors %i (static %i, gradient %i, clip %i, shadows %i, decals %i, Xbox-batched %i), terrains %i (combined combinations %i, sectors with a three-layer pass %i), terrain layers per pass %i"),
		NumProjectors, NumStatic, NumGradient, NumClip, NumShadows, NumDecals, NumBatched, NumTerrains, NumCombined, NumSectors3,
		RenDev->GetRenderCaps()->MaxSimultaneousTerrainLayers );
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
