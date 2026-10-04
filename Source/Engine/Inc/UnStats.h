/*=============================================================================
	UnStats.h: Performance statistics utilities.
	Copyright 1997-2002 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
		* Rewritten multiple times by Andrew Scheidecker and Daniel Vogel
		* Last rewrite by Daniel Vogel for .csv support
=============================================================================*/

enum EStatsType
{
	STATSTYPE_Render		= 0,
	STATSTYPE_Game,
	STATSTYPE_Hardware,
	STATSTYPE_Anim,
	STATSTYPE_Matinee,
	STATSTYPE_Audio,
	STATSTYPE_Net,
	STATSTYPE_Canvas,
	STATSTYPE_Temp,
	STATSTYPE_MAX
};

enum EStatsDataType
{
	STATSDATATYPE_DWORD		= 0,
	STATSDATATYPE_FLOAT,
	STATSDATATYPE_STRING,
	STATSDATATYPE_MAX
};

enum EStatsUnit
{
	STATSUNIT_Default		= 0,
	STATSUNIT_Combined_Default_MSec,
	STATSUNIT_Combined_Default_Default,
	STATSUNIT_Byte,
	STATSUNIT_KByte,
	STATSUNIT_MByte,
	STATSUNIT_MSec
};

enum EStatsPresent
{
	STATSPRESENT_Log	= 1<<1,
	STATSPRESENT_Screen	= 1<<2,
};

class ENGINE_API FStats
{
public:
	FLOAT	FPSAvg;

	// Init Data.
	UBOOL			Registered[STATSTYPE_MAX];

	// Stats Data.
	TArray<DWORD>	DWORDStats;
	TArray<DWORD>	DWORDOldStats;
	TArray<FString>	DWORDStatsDescriptions;
	TArray<FString>	DWORDStatsSectionDescriptions;

	TArray<FLOAT>	FLOATStats;
	TArray<FLOAT>	FLOATOldStats;
	TArray<FString>	FLOATStatsDescriptions;
	TArray<FString>	FLOATStatsSectionDescriptions;

	TArray<FString>	FSTRINGStats;
	TArray<FString> FSTRINGOldStats;
	TArray<FString>	FSTRINGStatsDescriptions;
	TArray<FString>	FSTRINGStatsSectionDescriptions;

	struct FSingleStatsInfo
	{
		INT				Index;
		EStatsDataType	DataType;
		EStatsUnit		StatsUnit;
	};

	TArray<FSingleStatsInfo> Stats[STATSTYPE_MAX];

	struct FMovingAverage
	{		
		INT CycIndex;
		INT CycBufferSize;
		INT Count;
		EStatsDataType	DataType;
		TArray<DWORD>   Samples;
	};

	TArray<FMovingAverage> Averages;

	//jg -- Hacked in for Steve to track listen server performance
	UBOOL bUseSteveStat;
	UBOOL bUseTempStat;

	// Interface.
	FStats();
	INT RegisterStats( EStatsType Type, EStatsDataType DataType, FString Description, FString SectionDescription, EStatsUnit StatsUnit );
	void Render(class UViewport* Viewport, class UEngine* Engine);
	// scion sz -
	void RenderUnitType( class UViewport* Viewport, class UEngine* Engine, EStatsUnit UnitType, void* TypeDataPtr, DWORD Present=STATSPRESENT_Screen );
	void UpdateString( FString& Result, UBOOL Descriptions );
	void Clear();
	void CalcMovingAverage( INT StatIndex, DWORD CycleSize );
};


class ENGINE_API FEngineStats
{
public:
	INT		STATS_FirstEntry,
		
			STATS_Frame_TotalCycles,
			STATS_Frame_RenderCycles,

#ifdef WITH_KARMA
			STATS_Karma_Collision,
			STATS_Karma_CollisionContactGen,
			STATS_Karma_TrilistGen,
			STATS_Karma_RagdollTrilist,
			STATS_Karma_Dynamics,
			STATS_Karma_physKarma,
			STATS_Karma_physKarma_Con,
			STATS_Karma_physKarmaRagDoll,
			STATS_Karma_Temp,
#endif

#ifdef  WITH_LIPSINC
			
			STATS_LIPSinc_NumActiveLIPSincAnims,
			STATS_LIPSinc_TotalSetupCycles,
			STATS_LIPSinc_TotalCurveEvalCycles,
			STATS_LIPSinc_TotalBlendCycles,
			STATS_LIPSinc_TotalBlendInCycles,
			STATS_LIPSinc_TotalNormalBlendCycles,
			STATS_LIPSinc_TotalBlendOutCycles,
			STATS_LIPSinc_TotalFinalBlendCycles,
			STATS_LIPSinc_TotalUpdateCycles,
			
#endif

			STATS_BSP_RenderCycles,
			STATS_BSP_Sections,
			STATS_BSP_Nodes,
			STATS_BSP_Triangles,
			STATS_BSP_DynamicLightingCycles,
			STATS_BSP_DynamicLights,
			STATS_BSP_CollisionCycles,
			STATS_BSP_ProjectorRenderCycles,		// scion sz -

			STATS_LightMap_Updates,
			STATS_LightMap_Cycles,

			STATS_Projector_RenderCycles,
			STATS_Projector_AttachCycles,
			STATS_Projector_Projectors,
			STATS_Projector_Triangles,
			
			STATS_Stencil_RenderCycles,
			STATS_Stencil_Nodes,
			STATS_Stencil_Triangles,

			STATS_Visibility_SetupCycles,
			STATS_Visibility_MaskTests,
			STATS_Visibility_MaskRejects,
			STATS_Visibility_BoxTests,			
			STATS_Visibility_BoxRejects,
			STATS_Visibility_BoxTestCycles,			// scion sz -
			STATS_Visibility_TraverseCycles,
			STATS_Visibility_ScratchBytes,
			STATS_Visibility_MeshLightCycles,
			STATS_Visibility_VisibleObjects,			

			//jg -- Stats for hardware occlusion
			STATS_HWOcclusion_OccludedObjects,
			STATS_HWOcclusion_OcclusionTests,
			STATS_HWOcclusion_WaitingTests,
			STATS_HWOcclusion_OcclusionTestCycles,

			STATS_Terrain_RenderCycles,
			STATS_Terrain_ProjectorRenderCycles,	// scion sz -
			STATS_Terrain_CollisionCycles,
			STATS_Terrain_Sectors,
			STATS_Terrain_Triangles,
			STATS_Terrain_DrawPrimitives,

			STATS_DecoLayer_RenderCycles,
			STATS_DecoLayer_Triangles,
			STATS_DecoLayer_Decorations,

			STATS_Matinee_TickCycles,

			STATS_Mesh_SkinCycles,
			STATS_Mesh_ResultCycles,
			STATS_Mesh_LODCycles,
			STATS_Mesh_SkelCycles,
			STATS_Mesh_PoseCycles,
			STATS_Mesh_RigidCycles,
			STATS_Mesh_DrawCycles,
			STATS_Mesh_ProjectorRenderCycles,		// scion sz -
	
			STATS_Particle_SpriteSetupCycles,
			STATS_Particle_Particles,
            STATS_Particle_ParticlesTicked,
			STATS_Particle_RenderCycles,

			STATS_StaticMesh_Batches,
			STATS_StaticMesh_BatchedSortedSections,
			STATS_StaticMesh_BatchedSortedTriangles,
			STATS_StaticMesh_BatchedSortCycles,
			STATS_StaticMesh_BatchedUnsortedSections,
			STATS_StaticMesh_BatchedUnsortedTriangles,
			STATS_StaticMesh_BatchedRenderCycles,
			STATS_StaticMesh_UnbatchedSortedSections,
			STATS_StaticMesh_UnbatchedSortedTriangles,
			STATS_StaticMesh_UnbatchedSortCycles,
			STATS_StaticMesh_UnbatchedUnsortedSections,
			STATS_StaticMesh_UnbatchedUnsortedTriangles,
			STATS_StaticMesh_UnbatchedRenderCycles,
			STATS_StaticMesh_Triangles,
			STATS_StaticMesh_RenderCycles,
			STATS_StaticMesh_CollisionCycles,
			STATS_StaticMesh_CollisionCacheHits,
			STATS_StaticMesh_CollisionCacheMisses,
			STATS_StaticMesh_CollisionCacheFlushes,
			STATS_StaticMesh_ProjectorRenderCycles,		// scion sz -

			STATS_Batch_Batches,
			STATS_Batch_Primitives,
			STATS_Batch_RenderCycles,
#ifdef _XBOX
			//scion jg -- These are my push buffer stats so I can see which type, how
			// much memory is being used by type, and rendering times
			STATS_Cached_PushBuffer_Runnable,
			STATS_Cached_PushBuffer_RunnableMem,
			STATS_Cached_PushBuffer_RunAsCopy,
			STATS_Cached_PushBuffer_RunAsCopyMem,
			STATS_PushBuffer_Runnable,
			STATS_PushBuffer_RunnableMem,
			STATS_PushBuffer_RunAsCopy,
			STATS_PushBuffer_RunAsCopyMem,
			STATS_PushBuffer_RunPushBufferCycles,
			STATS_PushBuffer_RunPushBufferCalls,
			STATS_PushBuffer_RunPushBufferSMTris,
			STATS_PushBuffer_DrawUPCycles,
			STATS_PushBuffer_DrawUPCalls,
			STATS_PushBuffer_DrawUPCopied,
			STATS_PushBuffer_BeginPushCycles,
			STATS_PushBuffer_BeginPushCalls,
			STATS_PushBuffer_BeginPushMem,
			//scion jg -- Track how expensive movie materials are
			STATS_MovieMaterial_Cycles,
			STATS_MovieMaterial_Num,
#endif
			//scion jg -- Track how expensive coronas are
			STATS_Stats_CoronaRenderCycles,
			//scion jg -- Track how expensive our camera effects are
			STATS_Stats_CameraEffectsCycles,

			STATS_Stats_RenderCycles,

			STATS_Game_ScriptCycles,
			STATS_Game_ScriptTickCycles,
			STATS_Game_ActorTickCycles,
			STATS_Game_FindPathCycles,
			STATS_Game_SeePlayerCycles,
			STATS_Game_SpawningCycles,
			STATS_Game_AudioTickCycles,
			STATS_Game_CleanupDestroyedCycles,
			STATS_Game_UnusedCycles,
			STATS_Game_NetTickCycles,
			STATS_Game_ParticleTickCycles,
			STATS_Game_CanvasCycles,
			STATS_Game_PhysicsCycles,
			STATS_Game_MoveCycles,
			STATS_Game_NumMoves,
			STATS_Game_MLCheckCycles,
			STATS_Game_MPCheckCycles,
			STATS_Game_UpdateRenderData,
			STATS_Game_HUDPostRender,
			STATS_Game_InteractionPreRender,
			STATS_Game_InteractionPostRender,
			STATS_Game_ScriptDebugTime,

			STATS_Canvas_BeginPrimitive,
			STATS_Canvas_Flush,
			STATS_Canvas_NumPrimitives,
			STATS_Canvas_NumBatches,

			STATS_Fluid_SimulateCycles,
			STATS_Fluid_VertexGenCycles,
			STATS_Fluid_RenderCycles,

			STATS_Corona_CollisionCycles,

			STATS_Net_Ping,
			STATS_Net_Channels,
			// scion sz - max channels ever active
			STATS_Net_MaxChannels,
			STATS_Net_InUnordered,
			STATS_Net_OutUnordered,
			STATS_Net_InPacketLoss,
			STATS_Net_OutPacketLoss,
			STATS_Net_InPackets,
			STATS_Net_OutPackets,
			STATS_Net_InBunches,
			STATS_Net_OutBunches,
			STATS_Net_InBytes,
			STATS_Net_OutBytes,
			STATS_Net_Speed,
			STATS_Net_NumReps,
			STATS_Net_NumRPC,
			STATS_Net_NumPV,
			//scion rwd - add more net stats
			STATS_Net_ReceivedBunchCyles,
			STATS_Net_ServetTickClientCyles,
			STATS_Net_ReceivedRawBunchCyles,

			// scion sz - temp stats
			STATS_Temp_T1,
			STATS_Temp_T2,
			STATS_Temp_T3,
			STATS_Temp_T4,
			STATS_Temp_T5,
			STATS_Temp_T6,
			STATS_Temp_T7,
			STATS_Temp_T8,
			STATS_Temp_T9,
			STATS_Temp_T10,
			
			STATS_LastEntry;

	FEngineStats();
	void Init();
};

#ifdef _XBOX
class FPrintLocalMem
{
	MEMORYSTATUS MemStatusBefore;	
	MEMORYSTATUS MemStatusAfter;

	const TCHAR* Text;
	FOutputDevice* pLog;

public:
	FPrintLocalMem( const TCHAR* InText, FOutputDevice* InLog=GLog ) :
	  Text(InText),
	  pLog(InLog)
	  {
		  check(pLog);
		  GlobalMemoryStatus( &MemStatusBefore );
	  }
	  ~FPrintLocalMem()
	  {
		  GlobalMemoryStatus( &MemStatusAfter );
		  DWORD dwMemUsed = 0;
		  UBOOL bNegative = (MemStatusAfter.dwAvailPhys > MemStatusBefore.dwAvailPhys) ? 1:0;
		  if( bNegative ) {
			  dwMemUsed = MemStatusAfter.dwAvailPhys - MemStatusBefore.dwAvailPhys;
		  }
		  else {
			  dwMemUsed = MemStatusBefore.dwAvailPhys - MemStatusAfter.dwAvailPhys;
		  }
		  if( dwMemUsed > 0 ) 
		  {
			  pLog->Logf( TEXT("LOCALMEM: [%s] memused: %s%5.1f KB"), Text?Text:TEXT(""), (bNegative)?TEXT("-"):TEXT("+"),dwMemUsed/1024.f );
		  }
	  }
	  static void CurrentMemUsed( const TCHAR* InText, FOutputDevice* InLog=GLog )
	  {
		  check(InLog);
		  MEMORYSTATUS MemStatusCurrent;
		  GlobalMemoryStatus( &MemStatusCurrent );
		  DWORD dwMemUsed = MemStatusCurrent.dwTotalPhys - MemStatusCurrent.dwAvailPhys;
		  InLog->Logf( TEXT("CURRENTMEM: [%s] memused: %5.1f KB"), InText?InText:TEXT(""), dwMemUsed/1024.f );
	  }
};
#else
class FPrintLocalMem
{
public:
	FPrintLocalMem( const TCHAR* InText, FOutputDevice* InLog=GLog ) {}
	~FPrintLocalMem()
	{
		debugf( TEXT("LOCALMEM: NOT IMPLEMENTED") );
	}
	static void CurrentMemUsed( const TCHAR* InText )
	{
		debugf( TEXT("CURRENTMEM: NOT IMPLEMENTED") );
	}
};
#endif