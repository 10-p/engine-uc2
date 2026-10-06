/*=============================================================================
	UnStats.cpp: In game performance statistics utilities.
	Copyright 1997-2000 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
		* Rewritten multiple times by Andrew Scheidecker and Daniel Vogel
		* Last rewrite by Daniel Vogel for .csv support
=============================================================================*/


#include "EnginePrivate.h"
#include "UnNet.h"

#ifdef _XBOX
// Undo any Windows defines.
#undef BYTE
#undef WORD
#undef DWORD
#undef INT
#undef FLOAT
#undef MAXBYTE
#undef MAXWORD
#undef MAXDWORD
#undef MAXINT
#undef CDECL

// Make sure HANDLE is defined.
#ifndef _WINDOWS_
	#define HANDLE void*
	#define HINSTANCE void*
#endif
#include <xtl.h>
#include <XbDm.h>
#endif

#ifdef _XBOX //scion jg -- Try to draw these in a more useful location
#define MAYBE_WRAP				if( (Canvas->CurY + 40) >= Viewport->SizeY ) {CurX += 200; Canvas->CurY = 24;}
#else
#define MAYBE_WRAP				if( (Canvas->CurY + 32) >= Viewport->SizeY ) {CurX += 200; Canvas->CurY = 8;}
#endif
#define DRAW_STRING(Arg)		MAYBE_WRAP Canvas->CurX = CurX; Canvas->WrappedPrintf(Canvas->TinyFont,0,*Arg);

#define TIME_ARGUMENTS(Arg)		(FLOAT) (DWORDOldStats(GEngineStats.Arg) * GSecondsPerCycle * 1000.0f), *DWORDStatsDescriptions(GEngineStats.Arg) )
#define DWORD_ARGUMENTS(Arg)	DWORDOldStats(GEngineStats.Arg), *DWORDStatsDescriptions(GEngineStats.Arg) )

#define TIME_ARGUMENTS_EX(Arg)		(FLOAT) (Arg) * GSecondsPerCycle * 1000.0f, #Arg )
#define DWORD_ARGUMENTS_EX(Arg)	(Arg), #Arg )

#define DRAW_HEADLINE(Arg)		Canvas->Color = FColor(255,0,0); Canvas->CurY += 4; Dummy = Arg; DRAW_STRING( Dummy ); Canvas->Color = FColor(255,255,255);

#define TIME_ARGUMENTS_OWN_DESC(Arg1,Arg2)		(FLOAT) (DWORDOldStats(GEngineStats.Arg1) * GSecondsPerCycle * 1000.0f), Arg2 )
#define DWORD_ARGUMENTS_OWN_DESC(Arg1,Arg2)		DWORDOldStats(GEngineStats.Arg1), Arg2 )

#define DWORD_PRINTF			FString::Printf(TEXT("%u %s")
#define TIME_PRINTF				FString::Printf(TEXT("%2.2f %s ms")

#ifdef _XBOX
/**
 * Hack to render stat net stats even in final release
 */
void RenderStatNet(UViewport* Viewport,UEngine* Engine)
{
	FString Dummy;
	UCanvas* Canvas	= Viewport->Canvas;

	INT CurX = 38;
	Canvas->CurY = 28;

	// Update net stats (yeah, this is a hack).
	if(Viewport->Actor->XLevel->NetDriver != NULL)
	{
		// Reset the stats.
		DWORD STATS_Net_Ping = 0;
		DWORD STATS_Net_Channels = 0;
		static DWORD STATS_Net_MaxChannels = 0;
		static DWORD STATS_Net_MaxChannelsOld = 0;
		DWORD STATS_Net_InUnordered = 0;
		DWORD STATS_Net_OutUnordered = 0;
		DWORD STATS_Net_InPacketLoss = 0;
		DWORD STATS_Net_OutPacketLoss = 0;
		DWORD STATS_Net_InPackets = 0;
		DWORD STATS_Net_OutPackets = 0;
		DWORD STATS_Net_InBunches = 0;
		DWORD STATS_Net_OutBunches = 0;
		DWORD STATS_Net_InBytes = 0;
		DWORD STATS_Net_OutBytes = 0;
		DWORD STATS_Net_Speed = 0;

		UNetDriver* NetDriver = Viewport->Actor->XLevel->NetDriver;
		// If this is a client
		if (NetDriver->ServerConnection != NULL)
		{
			UNetConnection* Conn = NetDriver->ServerConnection;

			INT	NumChannels = 0;
			for(INT ChannelIndex = 0;ChannelIndex < UNetConnection::MAX_CHANNELS;ChannelIndex++)
			{
				NumChannels += (Conn->Channels[ChannelIndex] != NULL);
			}

			STATS_Net_Ping = (DWORD) 1000.0f*Conn->BestLag;
			STATS_Net_Channels = (DWORD) NumChannels;
			// scion sz - keep track of max active channels
			STATS_Net_MaxChannels = Max<DWORD>( STATS_Net_MaxChannelsOld, NumChannels );
			STATS_Net_MaxChannelsOld = Max<DWORD>(STATS_Net_MaxChannels,STATS_Net_MaxChannelsOld);

			STATS_Net_InUnordered = (DWORD) Conn->InOrder;
			STATS_Net_OutUnordered = (DWORD) Conn->OutOrder;
			STATS_Net_InPacketLoss = (DWORD) Conn->InLoss;
			STATS_Net_OutPacketLoss = (DWORD) Conn->OutLoss;
			STATS_Net_InPackets = (DWORD) Conn->InPackets;
			STATS_Net_OutPackets = (DWORD) Conn->OutPackets;
			STATS_Net_InBunches = (DWORD) Conn->InBunches;
			STATS_Net_OutBunches = (DWORD) Conn->OutBunches;
			STATS_Net_InBytes = (DWORD) Conn->InRate;
			STATS_Net_OutBytes = (DWORD) Conn->OutRate;
			STATS_Net_Speed = (DWORD) Conn->CurrentNetSpeed;
		}
		// If this is a server
		else if (NetDriver->ClientConnections.Num() > 0)
		{
			INT NumClientConnections = NetDriver->ClientConnections.Num();
			// For each client connection
			for (INT ConnIdx = 0; ConnIdx < NumClientConnections; ConnIdx++)
			{
				UNetConnection* Conn = NetDriver->ClientConnections(ConnIdx);

				// Find the number of channels open for this connection.
				INT	NumChannels = 0;
				for(INT ChannelIndex = 0; ChannelIndex < UNetConnection::MAX_CHANNELS; ChannelIndex++)
				{
					NumChannels += (Conn->Channels[ChannelIndex] != NULL);
				}
                
				// Update the stats to take this connection into account.
				STATS_Net_Ping += (DWORD) 1000.0f*Conn->BestLag;
				STATS_Net_Channels += (DWORD) NumChannels;
				STATS_Net_MaxChannels = Max<DWORD>( STATS_Net_MaxChannelsOld, STATS_Net_Channels );
				STATS_Net_MaxChannelsOld = Max<DWORD>(STATS_Net_MaxChannels,STATS_Net_MaxChannelsOld);
				STATS_Net_InUnordered += (DWORD) Conn->InOrder;
				STATS_Net_OutUnordered += (DWORD) Conn->OutOrder;
				STATS_Net_InPacketLoss += (DWORD) Conn->InLoss;
				STATS_Net_OutPacketLoss += (DWORD) Conn->OutLoss;
				STATS_Net_InPackets += (DWORD) Conn->InPackets;
				STATS_Net_OutPackets += (DWORD) Conn->OutPackets;
				STATS_Net_InBunches += (DWORD) Conn->InBunches;
				STATS_Net_OutBunches += (DWORD) Conn->OutBunches;
				STATS_Net_InBytes += (DWORD) Conn->InRate;
				STATS_Net_OutBytes += (DWORD) Conn->OutRate;
				STATS_Net_Speed += (DWORD) Conn->CurrentNetSpeed;					
			}

			// Make sure we compute the average for the ping.
			STATS_Net_Ping /= NumClientConnections;
		}
		DRAW_HEADLINE( TEXT("- Net ----------") );
		DRAW_STRING( (DWORD_PRINTF , DWORD_ARGUMENTS_EX ( STATS_Net_Ping				) ));
		DRAW_STRING( (DWORD_PRINTF , DWORD_ARGUMENTS_EX ( STATS_Net_Channels				) ));
		DRAW_STRING( (DWORD_PRINTF , DWORD_ARGUMENTS_EX ( STATS_Net_MaxChannels				) ));
		DRAW_STRING( (DWORD_PRINTF , DWORD_ARGUMENTS_EX ( STATS_Net_InUnordered				) ));
		DRAW_STRING( (DWORD_PRINTF , DWORD_ARGUMENTS_EX ( STATS_Net_OutUnordered				) ));
		DRAW_STRING( (DWORD_PRINTF , DWORD_ARGUMENTS_EX ( STATS_Net_InPacketLoss				) ));
		DRAW_STRING( (DWORD_PRINTF , DWORD_ARGUMENTS_EX ( STATS_Net_OutPacketLoss				) ));
		DRAW_STRING( (DWORD_PRINTF , DWORD_ARGUMENTS_EX ( STATS_Net_InPackets				) ));
		DRAW_STRING( (DWORD_PRINTF , DWORD_ARGUMENTS_EX ( STATS_Net_OutPackets				) ));
		DRAW_STRING( (DWORD_PRINTF , DWORD_ARGUMENTS_EX ( STATS_Net_InBunches				) ));
		DRAW_STRING( (DWORD_PRINTF , DWORD_ARGUMENTS_EX ( STATS_Net_OutBunches				) ));
		DRAW_STRING( (DWORD_PRINTF , DWORD_ARGUMENTS_EX ( STATS_Net_InBytes				) ));
		DRAW_STRING( (DWORD_PRINTF , DWORD_ARGUMENTS_EX ( STATS_Net_OutBytes				) ));
		DRAW_STRING( (DWORD_PRINTF , DWORD_ARGUMENTS_EX ( STATS_Net_Speed				) ));
	}
}
#endif

#ifndef FINAL_RELEASE

// Global stats.
FStats GStats;
FEngineStats GEngineStats;

FStats::FStats()
{
	for( INT i=0; i<STATSTYPE_MAX; i++ )
		Registered[i] = 0;
	bUseTempStat=0;
}

INT FStats::RegisterStats( EStatsType Type, EStatsDataType DataType, FString Description, FString SectionDescription, EStatsUnit StatsUnit )
{
	guard(FStats::RegisterStats);
	INT Index		= -1;
	switch( DataType )
	{
	case STATSDATATYPE_DWORD:
		Index = DWORDStats.Add( 1 );
		DWORDOldStats.Add( 1 );
		new(DWORDStatsDescriptions)FString(Description);
		new(DWORDStatsSectionDescriptions)FString(SectionDescription + TEXT(" ") + Description);
		break;
	case STATSDATATYPE_FLOAT:
		Index = FLOATStats.Add( 1 );
		FLOATOldStats.Add( 1 );
		new(FLOATStatsDescriptions)FString(Description);
		new(FLOATStatsSectionDescriptions)FString(SectionDescription + TEXT(" ") + Description);
		break;
	case STATSDATATYPE_STRING:
		Index = FSTRINGStats.Add( 1 );
		FSTRINGOldStats.Add( 1 );
		new(FSTRINGStatsDescriptions)FString(Description);
		new(FSTRINGStatsSectionDescriptions)FString(SectionDescription + TEXT(" ") + Description);
		break;
	}

	FSingleStatsInfo SI;
	SI.Index		= Index;
	SI.DataType		= DataType;
	SI.StatsUnit	= StatsUnit;

	Stats[Type].AddItem( SI );

	return Index;
	unguard;
}

void FStats::UpdateString( FString& Result, UBOOL Descriptions )
{
	guard(FStats::UpdateString);
	LOCAL_CLOCK(UpdateCycles);
	if( Descriptions )
	{
		for( INT i=0; i<STATSTYPE_MAX; i++ )
		{
			for( INT n=0; n<Stats[i].Num(); n++ )
			{
				Result += FString::Printf(TEXT("%s,"), *DWORDStatsSectionDescriptions(Stats[i](n).Index) );
			}
		}
	}
	else
	{
		for( INT i=0; i<STATSTYPE_MAX; i++ )
		{
			for( INT n=0; n<Stats[i].Num(); n++ )
			{
				switch( Stats[i](n).DataType )
				{
				case STATSDATATYPE_DWORD:
					{
						DWORD Value = DWORDStats(Stats[i](n).Index);
						switch( Stats[i](n).StatsUnit )
						{
						case STATSUNIT_Default:
						case STATSUNIT_Combined_Default_MSec:
						case STATSUNIT_Combined_Default_Default:
						case STATSUNIT_Byte:
						case STATSUNIT_KByte:
						case STATSUNIT_MByte:
							Result += FString::Printf(TEXT("%u,"), Value );
							break;
						case STATSUNIT_MSec:
							Result += FString::Printf(TEXT("%f,"), (FLOAT) (Value * GSecondsPerCycle * 1000.0f) );
							break;
						}
						break;
					}
				case STATSDATATYPE_FLOAT:
				case STATSDATATYPE_STRING:
					appErrorf(TEXT("Implement me!"));
				}
			}
		}
	}
	
	// Stat update time.
	if( Descriptions )
		Result += TEXT("Stats Update");
	else
		Result += FString::Printf(TEXT("%f"), (FLOAT) (LOCAL_UNCLOCK(UpdateCycles) * GSecondsPerCycle * 1000.0f) );
	Result += TEXT("\n");

	unguard;
}

void FStats::Clear()
{
	guard(FStats::Clear);

	// Update old stats.
	if( DWORDStats.Num() )
		appMemcpy( &DWORDOldStats(0), &DWORDStats(0), DWORDStats.Num() * sizeof(DWORD) );
	if( FLOATStats.Num() )
		appMemcpy( &FLOATOldStats(0), &FLOATStats(0), FLOATStats.Num() * sizeof(FLOAT) );
	for( INT i=0; i<FSTRINGStats.Num(); i++ )
		FSTRINGOldStats(i) = FSTRINGStats(i);

	// Clear new ones.
	if( DWORDStats.Num() )
		appMemzero( &DWORDStats(0), DWORDStats.Num() * sizeof(DWORD) );
	if( FLOATStats.Num() )
		appMemzero( &FLOATStats(0), FLOATStats.Num() * sizeof(FLOAT) );
	for( INT i=0; i<FSTRINGStats.Num(); i++ )
		FSTRINGStats(i) = TEXT("");

	unguard;
}


void FStats::CalcMovingAverage( INT StatIndex, DWORD CycleSize )
{
#define DISCARDFIRST 3
	// Automatic custom-size cyclic running average for MSEC timing parameters.

	DWORD* LatestCycles = &(GStats.DWORDStats( StatIndex ));
	
	INT NewElements = (StatIndex+1) -  Averages.Num();
	if( NewElements > 0 )
		Averages.AddZeroed( NewElements );	

	if( Averages(StatIndex).CycBufferSize != (INT)CycleSize )
	{
		Averages(StatIndex).CycBufferSize = CycleSize;
		Averages(StatIndex).Samples.Empty();
		Averages(StatIndex).Samples.AddZeroed( CycleSize );
		Averages(StatIndex).Count = 0;
	}
	
	Averages(StatIndex).Count = Min( Averages(StatIndex).CycBufferSize + DISCARDFIRST, Averages(StatIndex).Count + 1 );

	if( Averages(StatIndex).Count > DISCARDFIRST )
	{
		// Store and compute running average.
		INT TotalElements = Averages(StatIndex).Count - DISCARDFIRST;
				
		Averages(StatIndex).Samples( Averages(StatIndex).CycIndex ) = *LatestCycles;
		SQWORD DAv = 0;
		for( INT i=0; i< TotalElements; i++ )
			DAv += Averages(StatIndex).Samples(i);

		Averages(StatIndex).CycIndex = ( Averages(StatIndex).CycIndex + 1) % Averages(StatIndex).CycBufferSize;
		*LatestCycles = (INT)(DAv/(SQWORD)TotalElements);
	}		
}

#define DRAW_ALL(Arg)			\
for( INT i=0; i<GStats.Stats[Arg].Num(); i++ )	\
{	\
	FSingleStatsInfo* StatsInfo = &GStats.Stats[Arg](i);	\
	switch( StatsInfo->StatsUnit )	\
	{	\
		case STATSUNIT_Default:	\
		case STATSUNIT_Byte:	\
		{	\
			DRAW_STRING( FString::Printf(TEXT("%u %s"), DWORDOldStats(StatsInfo->Index), *DWORDStatsDescriptions(StatsInfo->Index)) );	\
			break;	\
		}	\
		case STATSUNIT_KByte:	\
		{	\
			DRAW_STRING( FString::Printf(TEXT("%u KByte %s"), DWORDOldStats(StatsInfo->Index) / 1024, *DWORDStatsDescriptions(StatsInfo->Index)) );	\
			break;	\
		}	\
		case STATSUNIT_MByte:	\
		{	\
			DRAW_STRING( FString::Printf(TEXT("%u MByte %s"), DWORDOldStats(StatsInfo->Index) / 1024 / 1024, *DWORDStatsDescriptions(StatsInfo->Index)) );	\
			break;	\
		}	\
		case STATSUNIT_MSec:	\
		{	\
			DRAW_STRING( FString::Printf(TEXT("%2.1f %s ms"), (FLOAT)(DWORDOldStats(StatsInfo->Index) * GSecondsPerCycle * 1000.0f), *DWORDStatsDescriptions(StatsInfo->Index) ));	\
			break;	\
		}	\
		case STATSUNIT_Combined_Default_MSec:	\
		{	\
			FSingleStatsInfo* NextInfo = StatsInfo + 1;	\
			DRAW_STRING( FString::Printf(TEXT("%2.1f %s ms [%u]"), (FLOAT)(DWORDOldStats(NextInfo->Index) * GSecondsPerCycle * 1000.0f), *DWORDStatsDescriptions(StatsInfo->Index), DWORDOldStats(StatsInfo->Index)) );	\
			i++;	\
			break;	\
		}	\
		case STATSUNIT_Combined_Default_Default:	\
		{	\
			FSingleStatsInfo* NextInfo = StatsInfo + 1;	\
			DRAW_STRING( FString::Printf(TEXT("In: %i, Out: %i %s"), DWORDOldStats(StatsInfo->Index), DWORDOldStats(NextInfo->Index), *DWORDStatsDescriptions(StatsInfo->Index) ));	\
			i++;	\
			break;	\
		}	\
	}	\
}


void FStats::Render(class UViewport* Viewport, class UEngine* Engine)
{
	guard(FStats::Render);

	UCanvas* Canvas	= Viewport->Canvas;
	FString Dummy;

	// Render the framerate.
	if(Engine->bShowFrameRate)
	{
		FLOAT FrameTime		= DWORDOldStats(GEngineStats.STATS_Frame_TotalCycles) * GSecondsPerCycle;
		FPSAvg				= FPSAvg ? FPSAvg * 0.98f + FrameTime * 0.02f : FrameTime;
		INT	AvgFrameRate	= FPSAvg ? 1.f / FPSAvg : 0,
			FrameRate		= 1.f / FrameTime;

		FLOAT	Width			= 0,
				Height			= 0;

		if(AvgFrameRate < 20)
			Canvas->Color = FColor(255,0,0);
		else if ( AvgFrameRate < 30 )
			Canvas->Color = FColor(255,0,255);
		else if(AvgFrameRate < 40)
			Canvas->Color = FColor(255,255,0);
		else
			Canvas->Color = FColor(0,255,0);

		Canvas->CurX = 0;
		Canvas->CurY = 0;

		Dummy = FString::Printf(TEXT("%u avg FPS"),AvgFrameRate);
		Canvas->WrappedStrLenf(Canvas->MedFont,Width,Height,*Dummy);

		Canvas->CurX = Viewport->SizeX - Width - 16;
		Canvas->CurY = 96;
		Canvas->WrappedPrintf(Canvas->MedFont,0,*FString::Printf(TEXT("%u cur FPS"),FrameRate));
		Canvas->CurX = Viewport->SizeX - Width - 16;
		Canvas->WrappedPrintf(Canvas->MedFont,0,*Dummy);
	}

#ifdef _XBOX //scion jg -- Try to draw these in a more useful place
	INT CurX		= 38;
	Canvas->CurY	= 28;
#else
	INT CurX		= 8;
	Canvas->CurY	= 8;
#endif

	//jg -- Listen server stats hack
	if (bUseSteveStat == 1)
	{
		CalcMovingAverage(GEngineStats.STATS_Net_ReceivedBunchCyles,1000);
		CalcMovingAverage(GEngineStats.STATS_Net_ServetTickClientCyles,1000);
		CalcMovingAverage(GEngineStats.STATS_Net_ReceivedRawBunchCyles,1000);
		DRAW_HEADLINE( TEXT("- Steve Stats ---") );
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Net_ReceivedBunchCyles) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Net_ServetTickClientCyles) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Net_ReceivedRawBunchCyles) ));
		CalcMovingAverage(GEngineStats.STATS_Frame_TotalCycles,1000);
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Frame_TotalCycles) ));
		static INT Count = 0;
		if (++Count >= 1000)
		{
			// Once every thousand times, dump this to the log
			debugf(*(TIME_PRINTF , TIME_ARGUMENTS ( STATS_Net_ReceivedBunchCyles) ));
			debugf(*(TIME_PRINTF , TIME_ARGUMENTS ( STATS_Net_ServetTickClientCyles) ));
			debugf(*(TIME_PRINTF , TIME_ARGUMENTS ( STATS_Net_ReceivedRawBunchCyles) ));
			debugf(*(TIME_PRINTF , TIME_ARGUMENTS ( STATS_Frame_TotalCycles) ));
			Count = 0;
		}
	}

	if( bUseTempStat )
	{
		static INT AvgNumFrames=10;

		CalcMovingAverage(GEngineStats.STATS_Temp_T1,AvgNumFrames);
		CalcMovingAverage(GEngineStats.STATS_Temp_T2,AvgNumFrames);
		CalcMovingAverage(GEngineStats.STATS_Temp_T3,AvgNumFrames);
		CalcMovingAverage(GEngineStats.STATS_Temp_T4,AvgNumFrames);
		CalcMovingAverage(GEngineStats.STATS_Temp_T5,AvgNumFrames);
		CalcMovingAverage(GEngineStats.STATS_Temp_T6,AvgNumFrames);
		CalcMovingAverage(GEngineStats.STATS_Temp_T7,AvgNumFrames);
		CalcMovingAverage(GEngineStats.STATS_Temp_T8,AvgNumFrames);
		CalcMovingAverage(GEngineStats.STATS_Temp_T9,AvgNumFrames);
		CalcMovingAverage(GEngineStats.STATS_Temp_T10,AvgNumFrames);

		DRAW_HEADLINE( TEXT("- Temp Stats ---") );
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Temp_T1) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Temp_T2) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Temp_T3) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Temp_T4) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Temp_T5) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Temp_T6) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Temp_T7) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Temp_T8) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Temp_T9) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Temp_T10) ));
	}

	// Render stats.
	if( Engine->bShowRenderStats )
	{
		DRAW_HEADLINE( TEXT("- Frame ---------") );
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Frame_TotalCycles			) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Frame_RenderCycles			) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Stats_RenderCycles			) ));

		DRAW_HEADLINE( TEXT("- BSP -----------") );
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_BSP_RenderCycles				) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_BSP_ProjectorRenderCycles) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_BSP_Sections					) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_BSP_Nodes					) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_BSP_Triangles				) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_BSP_DynamicLightingCycles	) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_BSP_DynamicLights			) ));

		DRAW_HEADLINE( TEXT("- LightMap ------") );
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_LightMap_Cycles				) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_LightMap_Updates				) ));

		DRAW_HEADLINE( TEXT("- Projector -----") );
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Projector_RenderCycles		) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Projector_AttachCycles		) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_Projector_Projectors			) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_Projector_Triangles			) ));
		
		DRAW_HEADLINE( TEXT("- Stencil -------") );
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Stencil_RenderCycles			) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_Stencil_Nodes				) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_Stencil_Triangles			) ));

		DRAW_HEADLINE( TEXT("- Visibility ----") );
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Visibility_SetupCycles		) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_Visibility_MaskTests			) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_Visibility_MaskRejects		) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_Visibility_BoxTests			) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_Visibility_BoxRejects		) ));
		DRAW_STRING( (TIME_PRINTF,	TIME_ARGUMENTS ( STATS_Visibility_BoxTestCycles		) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Visibility_TraverseCycles	) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_Visibility_ScratchBytes		) ));
		CalcMovingAverage( GEngineStats.STATS_Visibility_MeshLightCycles	,40);
		DRAW_STRING( (TIME_PRINTF ,	TIME_ARGUMENTS ( STATS_Visibility_MeshLightCycles	) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_Visibility_VisibleObjects	) ));

		//jg -- Hardware occlusion testing stats
		DRAW_HEADLINE( TEXT("- HWOcclusion ----") );
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_HWOcclusion_OcclusionTestCycles		) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_HWOcclusion_OcclusionTests		) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_HWOcclusion_WaitingTests		) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_HWOcclusion_OccludedObjects		) ));

		DRAW_HEADLINE( TEXT("- Terrain ------") );
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Terrain_RenderCycles			) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Terrain_ProjectorRenderCycles) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_Terrain_Sectors				) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_Terrain_Triangles			) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_Terrain_DrawPrimitives		) ));

		DRAW_HEADLINE( TEXT("- DecoLayer ----") );
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_DecoLayer_RenderCycles		) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_DecoLayer_Triangles			) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_DecoLayer_Decorations		) ));

		DRAW_HEADLINE( TEXT("- Mesh ---------") );
		CalcMovingAverage( GEngineStats.STATS_Mesh_SkinCycles	,40);
		CalcMovingAverage( GEngineStats.STATS_Mesh_ResultCycles	,40);
		CalcMovingAverage( GEngineStats.STATS_Mesh_LODCycles	,40);
		CalcMovingAverage( GEngineStats.STATS_Mesh_PoseCycles	,40);
		CalcMovingAverage( GEngineStats.STATS_Mesh_SkelCycles	,40);
		CalcMovingAverage( GEngineStats.STATS_Mesh_RigidCycles	,40);
		CalcMovingAverage( GEngineStats.STATS_Mesh_DrawCycles	,40);		
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Mesh_SkinCycles				) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Mesh_ResultCycles			) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Mesh_LODCycles				) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Mesh_PoseCycles				) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Mesh_SkelCycles				) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Mesh_RigidCycles				) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Mesh_DrawCycles				) ));
		
		DRAW_HEADLINE( TEXT("- Particle -----") );
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Particle_SpriteSetupCycles	) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_Particle_Particles			) ));
        DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_Particle_ParticlesTicked 	) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Particle_RenderCycles		) ));
		// scion sz - current particle attenuation 
		UGameEngine* GameEngine = Cast<UGameEngine>( Engine );
		if( GameEngine && GameEngine->Client && GameEngine->GLevel )
		{
			ALevelInfo* pLevelInfo = GameEngine->GLevel->GetLevelInfo();
			DRAW_STRING( FString::Printf(TEXT("(%0.2f) %0.2f Atten%%"),pLevelInfo->DynamicParticleAtten, pLevelInfo->SpawnParticleAtten) );
		}

		DRAW_HEADLINE( TEXT("- StaticMesh ---") );
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_StaticMesh_Triangles		) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_StaticMesh_RenderCycles	) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_StaticMesh_ProjectorRenderCycles) ));
#ifndef _XBOX
		//scion jg -- These stats are useless on the xbox
		Canvas->CurY += 4;
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_StaticMesh_Batches					) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_StaticMesh_BatchedSortedTriangles	) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_StaticMesh_BatchedSortedSections		) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_StaticMesh_BatchedSortCycles			) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_StaticMesh_BatchedUnsortedSections	) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_StaticMesh_BatchedUnsortedTriangles	) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_StaticMesh_BatchedRenderCycles		) ));
#endif
		Canvas->CurY += 4;
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_StaticMesh_UnbatchedSortedTriangles	) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_StaticMesh_UnbatchedSortedSections	) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_StaticMesh_UnbatchedSortCycles		) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_StaticMesh_UnbatchedUnsortedSections	) ));
#ifndef _XBOX
		//scion jg -- These stats are useless on the xbox
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_StaticMesh_UnbatchedUnsortedTriangles) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_StaticMesh_UnbatchedRenderCycles		) ));

		DRAW_HEADLINE( TEXT("- Batch --------") );
		DRAW_STRING( (DWORD_PRINTF,	DWORD_ARGUMENTS( STATS_Batch_Batches				) ));
		DRAW_STRING( (DWORD_PRINTF,	DWORD_ARGUMENTS( STATS_Batch_Primitives				) ));
		DRAW_STRING( (TIME_PRINTF,	TIME_ARGUMENTS ( STATS_Batch_RenderCycles			) ));
#endif
#ifdef _XBOX
		//scion jg -- Stats reporting of my push buffer usage
		DRAW_HEADLINE( TEXT("- PushBuffer----") );
		// Since stats are cleared each frame copy over the values
		GStats.DWORDStats(GEngineStats.STATS_PushBuffer_Runnable) =
			GEngineStats.STATS_Cached_PushBuffer_Runnable;
		GStats.DWORDStats(GEngineStats.STATS_PushBuffer_RunnableMem) =
			GEngineStats.STATS_Cached_PushBuffer_RunnableMem;
		GStats.DWORDStats(GEngineStats.STATS_PushBuffer_RunAsCopy) =
			GEngineStats.STATS_Cached_PushBuffer_RunAsCopy;
		GStats.DWORDStats(GEngineStats.STATS_PushBuffer_RunAsCopyMem) =
			GEngineStats.STATS_Cached_PushBuffer_RunAsCopyMem;
		DRAW_STRING( (DWORD_PRINTF,	DWORD_ARGUMENTS( STATS_PushBuffer_Runnable				) ));
		DRAW_STRING( (DWORD_PRINTF,	DWORD_ARGUMENTS( STATS_PushBuffer_RunnableMem				) ));
		DRAW_STRING( (DWORD_PRINTF,	DWORD_ARGUMENTS( STATS_PushBuffer_RunAsCopy				) ));
		DRAW_STRING( (DWORD_PRINTF,	DWORD_ARGUMENTS( STATS_PushBuffer_RunAsCopyMem				) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_PushBuffer_RunPushBufferCycles		) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_PushBuffer_RunPushBufferCalls	) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_PushBuffer_RunPushBufferSMTris	) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_PushBuffer_DrawUPCycles		) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_PushBuffer_DrawUPCalls	) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_PushBuffer_DrawUPCopied	) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_PushBuffer_BeginPushCycles		) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_PushBuffer_BeginPushCalls	) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_PushBuffer_BeginPushMem	) ));
		//scion jg -- Track how expensive movie materials are
		DRAW_HEADLINE( TEXT("- MovieMaterial-") );
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_MovieMaterial_Cycles		) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_MovieMaterial_Num) ));
#endif
		DRAW_HEADLINE( TEXT("- Corona--------") );
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Stats_CoronaRenderCycles		) ));
		DRAW_HEADLINE( TEXT("- Camera Effects") );
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Stats_CameraEffectsCycles	) ));
	}

	// Render game stats.
	if( Engine->bShowGameStats )
	{
		DRAW_HEADLINE( TEXT("- Collision ----") );
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_StaticMesh_CollisionCycles	) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_BSP_CollisionCycles			) ));		
		//CalcMovingAverage( GEngineStats.STATS_Terrain_CollisionCycles	,90);
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Terrain_CollisionCycles		) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Corona_CollisionCycles		) ));

#ifdef WITH_KARMA
		DRAW_HEADLINE( TEXT("- Karma --------") );
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Karma_Collision			    ) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Karma_CollisionContactGen	) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Karma_TrilistGen				) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Karma_RagdollTrilist			) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Karma_Dynamics				) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Karma_physKarma				) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Karma_physKarma_Con			) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Karma_physKarmaRagDoll		) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Karma_Temp					) ));
#endif
		
#ifdef WITH_LIPSINC

		DRAW_HEADLINE( TEXT("- LIPSinc --------") );
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_LIPSinc_NumActiveLIPSincAnims) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_LIPSinc_TotalCurveEvalCycles ) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_LIPSinc_TotalBlendInCycles   ) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_LIPSinc_TotalNormalBlendCycles) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_LIPSinc_TotalBlendOutCycles  ) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_LIPSinc_TotalBlendCycles		) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_LIPSinc_TotalUpdateCycles	) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_LIPSinc_TotalFinalBlendCycles) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_LIPSinc_TotalSetupCycles		) ));

#endif

		DRAW_HEADLINE( TEXT("- Fluid  --------") );
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Fluid_SimulateCycles			) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Fluid_VertexGenCycles	    ) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Fluid_RenderCycles			) ));

		DRAW_HEADLINE( TEXT("- Game ---------") );
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Game_ScriptCycles			) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Game_ScriptTickCycles		) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Game_ActorTickCycles			) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Game_FindPathCycles			) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Game_SeePlayerCycles			) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Game_SpawningCycles			) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Game_AudioTickCycles			) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Game_CleanupDestroyedCycles	) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Game_UnusedCycles			) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Game_NetTickCycles			) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Game_ParticleTickCycles		) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Game_CanvasCycles			) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Game_PhysicsCycles			) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Game_MoveCycles				) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_Game_NumMoves				) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Game_MLCheckCycles			) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Game_MPCheckCycles			) ));	
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Game_UpdateRenderData		) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Game_HUDPostRender			) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Game_InteractionPreRender	) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Game_InteractionPostRender	) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Game_ScriptDebugTime	) ));


		/* SCRIPTTIME
		if ( GStats.DWORDStats(GEngineStats.STATS_Game_ActorTickCycles) * GSecondsPerCycle * 1000.0f > 30.f )
		{
			debugf(TEXT(" ****************************************************************"));
			debugf(TEXT(" ActorTick Cycles %f"), GStats.DWORDStats(GEngineStats.STATS_Game_ActorTickCycles) * GSecondsPerCycle * 1000.0f);
			debugf(TEXT(" Script Cycles %f"), GScriptCycles * GSecondsPerCycle * 1000.0f);
			debugf(TEXT(" ScriptTick Cycles %f"), GStats.DWORDStats(GEngineStats.STATS_Game_ScriptTickCycles) * GSecondsPerCycle * 1000.0f);
			debugf(TEXT(" FindPath Cycles %f"), GStats.DWORDStats(GEngineStats.STATS_Game_FindPathCycles) * GSecondsPerCycle * 1000.0f);
			debugf(TEXT(" SeePlayer Cycles %f"), GStats.DWORDStats(GEngineStats.STATS_Game_SeePlayerCycles) * GSecondsPerCycle * 1000.0f);
			debugf(TEXT(" Spawning Cycles %f"), GStats.DWORDStats(GEngineStats.STATS_Game_SpawningCycles) * GSecondsPerCycle * 1000.0f);
			debugf(TEXT(" CleanupDestroyed Cycles %f"), GStats.DWORDStats(GEngineStats.STATS_Game_CleanupDestroyedCycles) * GSecondsPerCycle * 1000.0f);
			debugf(TEXT(" AudioTick Cycles %f"), GStats.DWORDStats(GEngineStats.STATS_Game_AudioTickCycles) * GSecondsPerCycle * 1000.0f);
			debugf(TEXT(" Canvas Cycles %f"), GStats.DWORDStats(GEngineStats.STATS_Game_CanvasCycles) * GSecondsPerCycle * 1000.0f);
			debugf(TEXT(" Physics Cycles %f"), GStats.DWORDStats(GEngineStats.STATS_Game_PhysicsCycles) * GSecondsPerCycle * 1000.0f);
			debugf(TEXT(" Move Cycles %f"), GStats.DWORDStats(GEngineStats.STATS_Game_MoveCycles) * GSecondsPerCycle * 1000.0f);
			debugf(TEXT(" MLCheck Cycles %f"), GStats.DWORDStats(GEngineStats.STATS_Game_MLCheckCycles) * GSecondsPerCycle * 1000.0f);
			debugf(TEXT(" MPCheck Cycles %f"), GStats.DWORDStats(GEngineStats.STATS_Game_MPCheckCycles) * GSecondsPerCycle * 1000.0f);
			debugf(TEXT(" UpdateRenderData Cycles %f"), GStats.DWORDStats(GEngineStats.STATS_Game_UpdateRenderData) * GSecondsPerCycle * 1000.0f);
			debugf(TEXT(" ParticleTick Cycles %f"), GStats.DWORDStats(GEngineStats.STATS_Game_ParticleTickCycles) * GSecondsPerCycle * 1000.0f);
			debugf(TEXT(" NetTick Cycles %f"), GStats.DWORDStats(GEngineStats.STATS_Game_NetTickCycles) * GSecondsPerCycle * 1000.0f);
			debugf(TEXT(" HUD PostRender Cycles %f"), GStats.DWORDStats(GEngineStats.STATS_Game_HUDPostRender) * GSecondsPerCycle * 1000.0f);
		}
		*/
	}

	if (Engine->bShowCanvasStats)
	{
		DRAW_HEADLINE( TEXT("- Canvas ---------") );
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Canvas_BeginPrimitive		) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_Canvas_Flush					) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_Canvas_NumPrimitives			) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_Canvas_NumBatches			) ));
	}

	if( Engine->bShowStatsSummary )
	{
		DRAW_HEADLINE( TEXT("- Summary- -----") );
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS_OWN_DESC( STATS_StaticMesh_Triangles		, TEXT("StaticMesh triangles")	) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS_OWN_DESC ( STATS_Frame_TotalCycles			, TEXT("Frame")					) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS_OWN_DESC( STATS_BSP_Triangles				, TEXT("BSP triangles")			) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS_OWN_DESC ( STATS_Visibility_TraverseCycles	, TEXT("BSP traverse")			) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS_OWN_DESC( STATS_Visibility_VisibleObjects	, TEXT("Visible objects")		) ));
		DRAW_STRING( (TIME_PRINTF , TIME_ARGUMENTS ( STATS_HWOcclusion_OcclusionTestCycles		) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_HWOcclusion_OcclusionTests		) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_HWOcclusion_WaitingTests		) ));
		DRAW_STRING( (DWORD_PRINTF, DWORD_ARGUMENTS( STATS_HWOcclusion_OccludedObjects		) ));
	}

	// Render hardware stats.
	if( Engine->bShowHardwareStats )
	{
		DRAW_HEADLINE( TEXT("- Hardware -----") );
		DRAW_ALL(STATSTYPE_Hardware);
	}

	// Render audio stats.
	if( Engine->bShowAudioStats )
	{
		DRAW_HEADLINE( TEXT("- Audio --------") );
		DRAW_ALL(STATSTYPE_Audio);
	}

	// Render network stats.
	if( Engine->bShowNetStats )
	{
		// Update net stats (yeah, this is a hack).
		if(Viewport->Actor->XLevel->NetDriver != NULL)
		{
			UNetDriver* NetDriver = Viewport->Actor->XLevel->NetDriver;
			// If this is a client
			if (NetDriver->ServerConnection != NULL)
			{
				UNetConnection* Conn = NetDriver->ServerConnection;

				INT	NumChannels = 0;
				for(INT ChannelIndex = 0;ChannelIndex < UNetConnection::MAX_CHANNELS;ChannelIndex++)
				{
					NumChannels += (Conn->Channels[ChannelIndex] != NULL);
				}

				DWORDStats(GEngineStats.STATS_Net_Ping			) = (DWORD) 1000.0f*Conn->BestLag;
				DWORDStats(GEngineStats.STATS_Net_Channels		) = (DWORD) NumChannels;
				// scion sz - keep track of max active channels
				DWORDStats(GEngineStats.STATS_Net_MaxChannels	) = (DWORD) Max<DWORD>( DWORDOldStats(GEngineStats.STATS_Net_MaxChannels), NumChannels );
				DWORDStats(GEngineStats.STATS_Net_InUnordered	) = (DWORD) Conn->InOrder;
				DWORDStats(GEngineStats.STATS_Net_OutUnordered	) = (DWORD) Conn->OutOrder;
				DWORDStats(GEngineStats.STATS_Net_InPacketLoss	) = (DWORD) Conn->InLoss;
				DWORDStats(GEngineStats.STATS_Net_OutPacketLoss	) = (DWORD) Conn->OutLoss;
				DWORDStats(GEngineStats.STATS_Net_InPackets		) = (DWORD) Conn->InPackets;
				DWORDStats(GEngineStats.STATS_Net_OutPackets	) = (DWORD) Conn->OutPackets;
				DWORDStats(GEngineStats.STATS_Net_InBunches		) = (DWORD) Conn->InBunches;
				DWORDStats(GEngineStats.STATS_Net_OutBunches	) = (DWORD) Conn->OutBunches;
				DWORDStats(GEngineStats.STATS_Net_InBytes		) = (DWORD) Conn->InRate;
				DWORDStats(GEngineStats.STATS_Net_OutBytes		) = (DWORD) Conn->OutRate;
				DWORDStats(GEngineStats.STATS_Net_Speed			) = (DWORD) Conn->CurrentNetSpeed;
			}
			// If this is a server
			else if (NetDriver->ClientConnections.Num() > 0)
			{
				// Reset the stats.
				DWORDStats(GEngineStats.STATS_Net_Ping			) = 0;
				DWORDStats(GEngineStats.STATS_Net_Channels		) = 0;
				DWORDStats(GEngineStats.STATS_Net_MaxChannels	) = 0;
				DWORDStats(GEngineStats.STATS_Net_InUnordered	) = 0;
				DWORDStats(GEngineStats.STATS_Net_OutUnordered	) = 0;
				DWORDStats(GEngineStats.STATS_Net_InPacketLoss	) = 0;
				DWORDStats(GEngineStats.STATS_Net_OutPacketLoss	) = 0;
				DWORDStats(GEngineStats.STATS_Net_InPackets		) = 0;
				DWORDStats(GEngineStats.STATS_Net_OutPackets	) = 0;
				DWORDStats(GEngineStats.STATS_Net_InBunches		) = 0;
				DWORDStats(GEngineStats.STATS_Net_OutBunches	) = 0;
				DWORDStats(GEngineStats.STATS_Net_InBytes		) = 0;
				DWORDStats(GEngineStats.STATS_Net_OutBytes		) = 0;
				DWORDStats(GEngineStats.STATS_Net_Speed			) = 0;

				INT NumClientConnections = NetDriver->ClientConnections.Num();
				// For each client connection
				for (INT ConnIdx = 0; ConnIdx < NumClientConnections; ConnIdx++)
				{
					UNetConnection* Conn = NetDriver->ClientConnections(ConnIdx);

					// Find the number of channels open for this connection.
					INT	NumChannels = 0;
					for(INT ChannelIndex = 0; ChannelIndex < UNetConnection::MAX_CHANNELS; ChannelIndex++)
					{
						NumChannels += (Conn->Channels[ChannelIndex] != NULL);
					}
                    
					// Update the stats to take this connection into account.
					DWORDStats(GEngineStats.STATS_Net_Ping			) += (DWORD) 1000.0f*Conn->BestLag;
					DWORDStats(GEngineStats.STATS_Net_Channels		) += (DWORD) NumChannels;
					DWORDStats(GEngineStats.STATS_Net_MaxChannels	)  = (DWORD) Max<DWORD>(DWORDStats(GEngineStats.STATS_Net_MaxChannels), NumChannels);
					DWORDStats(GEngineStats.STATS_Net_InUnordered	) += (DWORD) Conn->InOrder;
					DWORDStats(GEngineStats.STATS_Net_OutUnordered	) += (DWORD) Conn->OutOrder;
					DWORDStats(GEngineStats.STATS_Net_InPacketLoss	) += (DWORD) Conn->InLoss;
					DWORDStats(GEngineStats.STATS_Net_OutPacketLoss	) += (DWORD) Conn->OutLoss;
					DWORDStats(GEngineStats.STATS_Net_InPackets		) += (DWORD) Conn->InPackets;
					DWORDStats(GEngineStats.STATS_Net_OutPackets	) += (DWORD) Conn->OutPackets;
					DWORDStats(GEngineStats.STATS_Net_InBunches		) += (DWORD) Conn->InBunches;
					DWORDStats(GEngineStats.STATS_Net_OutBunches	) += (DWORD) Conn->OutBunches;
					DWORDStats(GEngineStats.STATS_Net_InBytes		) += (DWORD) Conn->InRate;
					DWORDStats(GEngineStats.STATS_Net_OutBytes		) += (DWORD) Conn->OutRate;
					DWORDStats(GEngineStats.STATS_Net_Speed			) += (DWORD) Conn->CurrentNetSpeed;					
				}

				// Make sure we compute the average for the ping.
				DWORDStats(GEngineStats.STATS_Net_Ping			) /= NumClientConnections;

				DWORDStats(GEngineStats.STATS_Net_MaxChannels	) = (DWORD) Max<DWORD>(DWORDStats(GEngineStats.STATS_Net_MaxChannels), DWORDOldStats(GEngineStats.STATS_Net_MaxChannels));
			}
		}
		DRAW_HEADLINE( TEXT("- Net ----------") );
		DRAW_ALL(STATSTYPE_Net);
	}

	unguard;
}

FEngineStats::FEngineStats()
{
	guard(FEngineStats::FEngineStats);
	appMemset( &STATS_FirstEntry, 0xFF, (DWORD)((PTRINT) &STATS_LastEntry - (PTRINT) &STATS_FirstEntry) );
#ifdef _XBOX
	//scion jg -- Init push buffer memory stats to zero not -1
	STATS_Cached_PushBuffer_Runnable = 0;
	STATS_Cached_PushBuffer_RunnableMem = 0;
	STATS_Cached_PushBuffer_RunAsCopy = 0;
	STATS_Cached_PushBuffer_RunAsCopyMem = 0;
#endif
	unguard;
}

void FEngineStats::Init()
{
	guard(FEngineStats::Init);

	// If already initialized retrieve indices from GStats.
	if( GStats.Registered[STATSTYPE_Render] )
	{
		INT* Dummy = &STATS_Frame_TotalCycles;
		for( INT i=0; i<GStats.Stats[STATSTYPE_Render].Num(); i++ )
			*(Dummy++) = GStats.Stats[STATSTYPE_Render](i).Index;

		Dummy = &STATS_Game_ScriptCycles;
		for( INT i=0; i<GStats.Stats[STATSTYPE_Game].Num(); i++ )
			*(Dummy++) = GStats.Stats[STATSTYPE_Game](i).Index;

		Dummy = &STATS_Net_Ping;
		for( INT i=0; i<GStats.Stats[STATSTYPE_Net].Num(); i++ )
			*(Dummy++) = GStats.Stats[STATSTYPE_Net](i).Index;

		Dummy = &STATS_Canvas_BeginPrimitive;
		for( INT i=0; i<GStats.Stats[STATSTYPE_Canvas].Num(); i++ )
			*(Dummy++) = GStats.Stats[STATSTYPE_Canvas](i).Index;

		return;
	}

	// Register all stats with GStats.
	STATS_Frame_TotalCycles					= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Frame"				), TEXT("Frame"		), STATSUNIT_MSec					);
	STATS_Frame_RenderCycles				= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Render"			), TEXT("Frame"		), STATSUNIT_MSec					);
	
	STATS_BSP_RenderCycles					= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Render"			), TEXT("BSP"		), STATSUNIT_MSec					);
	STATS_BSP_ProjectorRenderCycles			= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Projectors"		), TEXT("BSP"		), STATSUNIT_MSec					);
	STATS_BSP_Sections						= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Sections"			), TEXT("BSP"		), STATSUNIT_Default				);
	STATS_BSP_Nodes							= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Nodes"				), TEXT("BSP"		), STATSUNIT_Default				);
	STATS_BSP_Triangles						= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Triangles"			), TEXT("BSP"		), STATSUNIT_Default				);
	STATS_BSP_DynamicLightingCycles			= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("DynamicLighting"	), TEXT("BSP"		), STATSUNIT_MSec					);
	STATS_BSP_DynamicLights					= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("DynamicLights"		), TEXT("BSP"		), STATSUNIT_Default				);	

	STATS_LightMap_Updates					= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Updates"			), TEXT("LightMap"	), STATSUNIT_Default				);
	STATS_LightMap_Cycles					= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Time"				), TEXT("LightMap"	), STATSUNIT_MSec					);

	STATS_Projector_RenderCycles			= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Render"			), TEXT("Projector"	), STATSUNIT_MSec					);
	STATS_Projector_AttachCycles			= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Attach"			), TEXT("Projector"	), STATSUNIT_MSec					);
	STATS_Projector_Projectors				= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Projectors"		), TEXT("Projector"	), STATSUNIT_Default				);
	STATS_Projector_Triangles				= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Triangles"			), TEXT("Projector"	), STATSUNIT_Default				);
			
	STATS_Stencil_RenderCycles				= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Render"			), TEXT("Stencil"	), STATSUNIT_MSec					);
	STATS_Stencil_Nodes						= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Nodes"				), TEXT("Stencil"	), STATSUNIT_Default				);
	STATS_Stencil_Triangles					= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Triangles"			), TEXT("Stencil"	), STATSUNIT_Default				);

	STATS_Visibility_SetupCycles			= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Setup"				), TEXT("Visibility"), STATSUNIT_MSec					);
	STATS_Visibility_MaskTests				= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("MaskTests"			), TEXT("Visibility"), STATSUNIT_Default				);
	STATS_Visibility_MaskRejects			= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("MaskRejects"		), TEXT("Visibility"), STATSUNIT_Default				);
	STATS_Visibility_BoxTests				= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("BoxTests"			), TEXT("Visibility"), STATSUNIT_Default				);
	STATS_Visibility_BoxRejects				= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("BoxRejects"		), TEXT("Visibility"), STATSUNIT_Default				);
	STATS_Visibility_BoxTestCycles			= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("BoxTestCycles"		), TEXT("Visibility"), STATSUNIT_MSec					);
	STATS_Visibility_TraverseCycles			= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Traverse"			), TEXT("Visibility"), STATSUNIT_MSec					);
	STATS_Visibility_ScratchBytes			= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("ScratchBytes"		), TEXT("Visibility"), STATSUNIT_KByte					);
	STATS_Visibility_MeshLightCycles		= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("MeshLight"			), TEXT("Visibility"), STATSUNIT_MSec					);
	STATS_Visibility_VisibleObjects			= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Visible Objects"	), TEXT("Visibility"), STATSUNIT_Default				);

	//jg -- Hardware occlusion stats
	STATS_HWOcclusion_OcclusionTestCycles	= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Cycles"			), TEXT("HWOcclusion"), STATSUNIT_MSec					);
	STATS_HWOcclusion_OcclusionTests		= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Tests"	), TEXT("HWOcclusion"), STATSUNIT_Default				);
	STATS_HWOcclusion_WaitingTests			= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Waiting Tests"	), TEXT("HWOcclusion"), STATSUNIT_Default				);
	STATS_HWOcclusion_OccludedObjects		= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Occluded"	), TEXT("HWOcclusion"), STATSUNIT_Default				);

	STATS_Terrain_RenderCycles				= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Render"			), TEXT("Terrain"	), STATSUNIT_MSec					);	
	STATS_Terrain_ProjectorRenderCycles		= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Projector"			), TEXT("Terrain"	), STATSUNIT_MSec					);
	STATS_Terrain_Sectors					= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Sectors"			), TEXT("Terrain"	), STATSUNIT_Default				);
	STATS_Terrain_Triangles					= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Triangles"			), TEXT("Terrain"	), STATSUNIT_Default				);
	STATS_Terrain_DrawPrimitives			= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("DrawPrimitives"	), TEXT("Terrain"	), STATSUNIT_Default				);	

	STATS_DecoLayer_RenderCycles			= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Render"			), TEXT("DecoLayer"	), STATSUNIT_MSec					);
	STATS_DecoLayer_Triangles				= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Triangles"			), TEXT("DecoLayer"	), STATSUNIT_Default				);
	STATS_DecoLayer_Decorations				= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Decorations"		), TEXT("DecoLayer"	), STATSUNIT_Default				);

	STATS_Matinee_TickCycles				= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Tick"				), TEXT("Matinee"	), STATSUNIT_MSec					);

	STATS_Mesh_SkinCycles					= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Skin"				), TEXT("Mesh"		), STATSUNIT_MSec					);
	STATS_Mesh_ResultCycles					= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Result"			), TEXT("Mesh"		), STATSUNIT_MSec					);
	STATS_Mesh_LODCycles					= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("LOD"				), TEXT("Mesh"		), STATSUNIT_MSec					);
	STATS_Mesh_SkelCycles					= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Skel"				), TEXT("Mesh"		), STATSUNIT_MSec					);
	STATS_Mesh_PoseCycles					= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Pose"				), TEXT("Mesh"		), STATSUNIT_MSec					);
	STATS_Mesh_RigidCycles					= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Rigid"				), TEXT("Mesh"		), STATSUNIT_MSec					);
	STATS_Mesh_DrawCycles					= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Draw"				), TEXT("Mesh"		), STATSUNIT_MSec					);
	
	STATS_Particle_SpriteSetupCycles		= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("SpriteSetup"		), TEXT("Particle"	), STATSUNIT_MSec					);
	STATS_Particle_Particles				= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Particles"			), TEXT("Particle"	), STATSUNIT_Default				);
    STATS_Particle_ParticlesTicked  		= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("ParticlesTicked"	), TEXT("Particle"	), STATSUNIT_Default				);
	STATS_Particle_RenderCycles				= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Render"			), TEXT("Particle"	), STATSUNIT_MSec					);

	STATS_StaticMesh_Triangles	= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Triangles"), TEXT("StaticMesh"), STATSUNIT_Default );
	STATS_StaticMesh_RenderCycles	= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Triangles"), TEXT("StaticMesh"), STATSUNIT_MSec );
	STATS_StaticMesh_ProjectorRenderCycles	= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Projectors"), TEXT("StaticMesh"	), STATSUNIT_MSec );

	STATS_StaticMesh_Batches					= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Batches"					), TEXT("StaticMesh"), STATSUNIT_Default	);
	STATS_StaticMesh_BatchedSortedSections		= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("BatchedSortedSections"		), TEXT("StaticMesh"), STATSUNIT_Default	);
	STATS_StaticMesh_BatchedSortedTriangles		= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("BatchedSortedTriangles"	), TEXT("StaticMesh"), STATSUNIT_Default	);
	STATS_StaticMesh_BatchedSortCycles			= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("BatchedSort"				), TEXT("StaticMesh"), STATSUNIT_MSec		);
	STATS_StaticMesh_BatchedUnsortedSections	= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("BatchedUnsortedSections"	), TEXT("StaticMesh"), STATSUNIT_Default	);
	STATS_StaticMesh_BatchedUnsortedTriangles	= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("BatchedUnsortedTriangles"	), TEXT("StaticMesh"), STATSUNIT_Default	);
	STATS_StaticMesh_BatchedRenderCycles		= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("BatchedRender"				), TEXT("StaticMesh"), STATSUNIT_MSec		);

#ifdef _XBOX
	//scion jg -- Change the text to be more meaningful on the xbox
	STATS_StaticMesh_UnbatchedSortedSections	= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("SortedSections"	), TEXT("StaticMesh"), STATSUNIT_Default	);
	STATS_StaticMesh_UnbatchedSortedTriangles	= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("SortedTriangles"	), TEXT("StaticMesh"), STATSUNIT_Default	);
	STATS_StaticMesh_UnbatchedSortCycles		= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Sort"				), TEXT("StaticMesh"), STATSUNIT_MSec		);
	STATS_StaticMesh_UnbatchedUnsortedSections	= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Sections"	), TEXT("StaticMesh"), STATSUNIT_Default	);
	STATS_StaticMesh_UnbatchedUnsortedTriangles	= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Triangles"), TEXT("StaticMesh"), STATSUNIT_Default	);
	STATS_StaticMesh_UnbatchedRenderCycles		= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Render"			), TEXT("StaticMesh"), STATSUNIT_MSec		);
#else
	STATS_StaticMesh_UnbatchedSortedSections	= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("UnbatchedSortedSections"	), TEXT("StaticMesh"), STATSUNIT_Default	);
	STATS_StaticMesh_UnbatchedSortedTriangles	= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("UnbatchedSortedTriangles"	), TEXT("StaticMesh"), STATSUNIT_Default	);
	STATS_StaticMesh_UnbatchedSortCycles		= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("UnbatchedSort"				), TEXT("StaticMesh"), STATSUNIT_MSec		);
	STATS_StaticMesh_UnbatchedUnsortedSections	= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("UnbatchedUnsortedSections"	), TEXT("StaticMesh"), STATSUNIT_Default	);
	STATS_StaticMesh_UnbatchedUnsortedTriangles	= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("UnbatchedUnsortedTriangles"), TEXT("StaticMesh"), STATSUNIT_Default	);
	STATS_StaticMesh_UnbatchedRenderCycles		= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("UnbatchedRender"			), TEXT("StaticMesh"), STATSUNIT_MSec		);
#endif

	//scion jg -- Keep track of how piggy coronas are
	STATS_Stats_CoronaRenderCycles = GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("CoronaRender"			), TEXT("Corona"), STATSUNIT_MSec		);
	//scion jg -- Keep track of how much time we spend in camera effects
	STATS_Stats_CameraEffectsCycles = GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("CameraEffects"			), TEXT("Effects Time"), STATSUNIT_MSec		);

#ifdef _XBOX
	//scion jg -- These are my push buffer stats so I can see which type, how
	// much memory is being used by type, and rendering times
	STATS_PushBuffer_Runnable = GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Runnable"	), TEXT("PushBuffer"), STATSUNIT_Default	);
	STATS_PushBuffer_RunnableMem = GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Runnable Mem"	), TEXT("PushBuffer"), STATSUNIT_Default	);
	STATS_PushBuffer_RunAsCopy = GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("RunAsCopy"	), TEXT("PushBuffer"), STATSUNIT_Default	);
	STATS_PushBuffer_RunAsCopyMem = GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("RunAsCopy Mem"	), TEXT("PushBuffer"), STATSUNIT_Default	);
	STATS_PushBuffer_RunPushBufferCycles = GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("RunPushBuffer"	), TEXT("PushBuffer"), STATSUNIT_MSec	);
	STATS_PushBuffer_RunPushBufferCalls  = GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("RunPushBuffer"	), TEXT("PushBuffer"), STATSUNIT_Default	);
	STATS_PushBuffer_RunPushBufferSMTris  = GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("RPB SM Tris"	), TEXT("PushBuffer"), STATSUNIT_Default	);
	STATS_PushBuffer_DrawUPCycles = GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("DrawUP"	), TEXT("PushBuffer"), STATSUNIT_MSec	);
	STATS_PushBuffer_DrawUPCalls = GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("DrawUP"	), TEXT("PushBuffer"), STATSUNIT_Default	);
	STATS_PushBuffer_DrawUPCopied = GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("DrawUPCopied"	), TEXT("PushBuffer"), STATSUNIT_Default	);
	STATS_PushBuffer_BeginPushCycles = GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("BeginPush"	), TEXT("PushBuffer"), STATSUNIT_MSec	);
	STATS_PushBuffer_BeginPushCalls = GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("BeginPush"	), TEXT("PushBuffer"), STATSUNIT_Default	);
	STATS_PushBuffer_BeginPushMem = GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("BeginPushMem"	), TEXT("PushBuffer"), STATSUNIT_Default	);
	//scion jg -- Track how expensive movie materials are
	STATS_MovieMaterial_Cycles = GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Cycles"	), TEXT("MovieMaterial"), STATSUNIT_MSec	);
	STATS_MovieMaterial_Num = GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Number"	), TEXT("MovieMaterial"), STATSUNIT_Default	);
#endif

	STATS_Batch_Batches						= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Batches"			), TEXT("Batch"		), STATSUNIT_Default				);
	STATS_Batch_Primitives					= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Primitives"		), TEXT("Batch"		), STATSUNIT_Default				);
	STATS_Batch_RenderCycles				= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Render"			), TEXT("Batch"		), STATSUNIT_MSec					);

	STATS_Stats_RenderCycles				= GStats.RegisterStats( STATSTYPE_Render, STATSDATATYPE_DWORD, TEXT("Stats Render"		), TEXT("Frame"		), STATSUNIT_MSec					);

	STATS_StaticMesh_CollisionCycles		= GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("StaticMesh"				), TEXT("Collision" ),	STATSUNIT_MSec			);
	STATS_StaticMesh_CollisionCacheHits		= GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("StaticMesh Cache Hits"		), TEXT("" ),			STATSUNIT_Default		);
	STATS_StaticMesh_CollisionCacheMisses	= GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("StaticMesh Cache Misses"	), TEXT("" ),			STATSUNIT_Default		);
	STATS_StaticMesh_CollisionCacheFlushes	= GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("StaticMesh Cache Flushes"	), TEXT("" ),			STATSUNIT_Default		);
	STATS_BSP_CollisionCycles				= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("BSP"						), TEXT("Collision"	),	STATSUNIT_MSec			);
	STATS_Terrain_CollisionCycles			= GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("Terrain"					), TEXT("Collision"	),	STATSUNIT_MSec			);
	STATS_Corona_CollisionCycles			= GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("Corona"					), TEXT("Collision"	),	STATSUNIT_MSec			);

#ifdef WITH_KARMA
	STATS_Karma_Collision					= GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("Collision"				), TEXT("Karma"	    ), STATSUNIT_MSec					);
	STATS_Karma_CollisionContactGen			= GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("  ContactGen"			), TEXT("Karma"	    ), STATSUNIT_MSec					);
	STATS_Karma_TrilistGen					= GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("  TrilistGen"			), TEXT("Karma"	    ), STATSUNIT_MSec					);
	STATS_Karma_RagdollTrilist				= GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("RagdollTrilist"		), TEXT("Karma"	    ), STATSUNIT_MSec					);
	STATS_Karma_Dynamics					= GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("Dynamics"				), TEXT("Karma"	    ), STATSUNIT_MSec					);
	STATS_Karma_physKarma					= GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("physKarma"				), TEXT("Karma"	    ), STATSUNIT_MSec					);
	STATS_Karma_physKarma_Con				= GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("Constraints"  			), TEXT("Karma"	    ), STATSUNIT_MSec					);
	STATS_Karma_physKarmaRagDoll			= GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("physKarmaRagdoll"		), TEXT("Karma"	    ), STATSUNIT_MSec					);
	STATS_Karma_Temp						= GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("Temp"					), TEXT("Karma"	    ), STATSUNIT_MSec					);
#endif

#ifdef WITH_LIPSINC

	STATS_LIPSinc_NumActiveLIPSincAnims = GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("Active"			), TEXT("LIPSinc"	), STATSUNIT_Default				);
	STATS_LIPSinc_TotalCurveEvalCycles	= GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("CurveEval"			), TEXT("LIPSinc"	), STATSUNIT_MSec					);
	STATS_LIPSinc_TotalBlendInCycles    = GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("BlendIn"			), TEXT("LIPSinc"	), STATSUNIT_MSec					);
	STATS_LIPSinc_TotalNormalBlendCycles= GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("NormalBlend"		), TEXT("LIPSinc"	), STATSUNIT_MSec					);
	STATS_LIPSinc_TotalBlendOutCycles   = GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("BlendOut"			), TEXT("LIPSinc"	), STATSUNIT_MSec					);
	STATS_LIPSinc_TotalBlendCycles		= GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("TotalBlend"		), TEXT("LIPSinc"	), STATSUNIT_MSec					);
	STATS_LIPSinc_TotalUpdateCycles		= GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("Update"			), TEXT("LIPSinc"	), STATSUNIT_MSec					);
	STATS_LIPSinc_TotalFinalBlendCycles = GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("FinalBlend"        ), TEXT("LIPSinc"   ), STATSUNIT_MSec					);
	STATS_LIPSinc_TotalSetupCycles		= GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("Total"				), TEXT("LIPSinc"	), STATSUNIT_MSec					);

#endif

	STATS_Game_ScriptCycles					= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("Script"			), TEXT("Game"		), STATSUNIT_MSec					);
	STATS_Game_ScriptTickCycles				= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("ScriptTick"		), TEXT("Game"		), STATSUNIT_MSec					);
	STATS_Game_ActorTickCycles				= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("Actor"				), TEXT("Game"		), STATSUNIT_MSec					);
	STATS_Game_FindPathCycles				= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("Path"				), TEXT("Game"		), STATSUNIT_MSec					);
	STATS_Game_SeePlayerCycles				= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("See"				), TEXT("Game"		), STATSUNIT_MSec					);
	STATS_Game_SpawningCycles				= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("Spawning"			), TEXT("Game"		), STATSUNIT_MSec					);
	STATS_Game_AudioTickCycles				= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("Audio"				), TEXT("Game"		), STATSUNIT_MSec					);
	STATS_Game_CleanupDestroyedCycles		= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("CleanupDestroyed"	), TEXT("Game"		), STATSUNIT_MSec					);
	STATS_Game_UnusedCycles					= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("Unused"			), TEXT("Game"		), STATSUNIT_MSec					);
	STATS_Game_NetTickCycles				= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("Net"				), TEXT("Game"		), STATSUNIT_MSec					);
	STATS_Game_ParticleTickCycles			= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("Particle"			), TEXT("Game"		), STATSUNIT_MSec					);
	STATS_Game_CanvasCycles					= GStats.RegisterStats( STATSTYPE_Game,   STATSDATATYPE_DWORD, TEXT("Canvas"			), TEXT("Game"		), STATSUNIT_MSec					);
	STATS_Game_NumMoves						= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("Move"				), TEXT("Game"		), STATSUNIT_Combined_Default_MSec	);
	STATS_Game_MoveCycles					= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("Move"				), TEXT("Game"		), STATSUNIT_MSec					);
	STATS_Game_PhysicsCycles				= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("Physics"			), TEXT("Game"		), STATSUNIT_MSec					);
	STATS_Game_MLCheckCycles				= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("MLChecks"			), TEXT("Game"		), STATSUNIT_MSec					);
	STATS_Game_MPCheckCycles				= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("MPChecks"			), TEXT("Game"		), STATSUNIT_MSec					);
	STATS_Game_UpdateRenderData				= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("RenderData"		), TEXT("Game"		), STATSUNIT_MSec					);
	STATS_Game_HUDPostRender				= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("HUD PostRender"	), TEXT("Game"		), STATSUNIT_MSec					);
	STATS_Game_InteractionPreRender			= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("Interaction Pre"	), TEXT("Game"		), STATSUNIT_MSec					);
	STATS_Game_InteractionPostRender		= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("Interaction Post"	), TEXT("Game"		), STATSUNIT_MSec					);
	STATS_Game_ScriptDebugTime				= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("ScriptDebug"		), TEXT("Game"		), STATSUNIT_MSec					);
	STATS_Fluid_SimulateCycles				= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("Simulate"			), TEXT("Fluid"		), STATSUNIT_MSec					);
	STATS_Fluid_VertexGenCycles				= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("VertexGen"			), TEXT("Fluid"		), STATSUNIT_MSec					);
	STATS_Fluid_RenderCycles				= GStats.RegisterStats( STATSTYPE_Game,	  STATSDATATYPE_DWORD, TEXT("Render"			), TEXT("Fluid"		), STATSUNIT_MSec					);

	STATS_Canvas_BeginPrimitive				= GStats.RegisterStats( STATSTYPE_Canvas, STATSDATATYPE_DWORD, TEXT("BeginPrimitive"	), TEXT("Canvas"	), STATSUNIT_MSec					);
	STATS_Canvas_Flush						= GStats.RegisterStats( STATSTYPE_Canvas, STATSDATATYPE_DWORD, TEXT("Flush"				), TEXT("Canvas"	), STATSUNIT_MSec					);
	STATS_Canvas_NumPrimitives				= GStats.RegisterStats( STATSTYPE_Canvas, STATSDATATYPE_DWORD, TEXT("# Primitives"		), TEXT("Canvas"	), STATSUNIT_Combined_Default_MSec	);
	STATS_Canvas_NumBatches					= GStats.RegisterStats( STATSTYPE_Canvas, STATSDATATYPE_DWORD, TEXT("# Batches"			), TEXT("Canvas"	), STATSUNIT_Combined_Default_MSec	);

	STATS_Net_Ping							= GStats.RegisterStats( STATSTYPE_Net,	  STATSDATATYPE_DWORD, TEXT("Ping"				), TEXT("Net"		), STATSUNIT_Default					);
	STATS_Net_Channels						= GStats.RegisterStats( STATSTYPE_Net,	  STATSDATATYPE_DWORD, TEXT("Channels"			), TEXT("Net"		), STATSUNIT_Default					);
	STATS_Net_MaxChannels					= GStats.RegisterStats( STATSTYPE_Net,	  STATSDATATYPE_DWORD, TEXT("MaxChannels"		), TEXT("Net"		), STATSUNIT_Default					);
	STATS_Net_InUnordered					= GStats.RegisterStats( STATSTYPE_Net,	  STATSDATATYPE_DWORD, TEXT("Unorderd"			), TEXT("Net"		), STATSUNIT_Combined_Default_Default	);
	STATS_Net_OutUnordered					= GStats.RegisterStats( STATSTYPE_Net,	  STATSDATATYPE_DWORD, TEXT("Unordered"			), TEXT("Net"		), STATSUNIT_Default					);
	STATS_Net_InPacketLoss					= GStats.RegisterStats( STATSTYPE_Net,	  STATSDATATYPE_DWORD, TEXT("PacketLoss"		), TEXT("Net"		), STATSUNIT_Combined_Default_Default	);
	STATS_Net_OutPacketLoss					= GStats.RegisterStats( STATSTYPE_Net,	  STATSDATATYPE_DWORD, TEXT("PacketLoss"		), TEXT("Net"		), STATSUNIT_Default					);
	STATS_Net_InPackets						= GStats.RegisterStats( STATSTYPE_Net,	  STATSDATATYPE_DWORD, TEXT("Packets"			), TEXT("Net"		), STATSUNIT_Combined_Default_Default	);
	STATS_Net_OutPackets					= GStats.RegisterStats( STATSTYPE_Net,	  STATSDATATYPE_DWORD, TEXT("Packets"			), TEXT("Net"		), STATSUNIT_Default					);
	STATS_Net_InBunches						= GStats.RegisterStats( STATSTYPE_Net,	  STATSDATATYPE_DWORD, TEXT("Bunches"			), TEXT("Net"		), STATSUNIT_Combined_Default_Default	);
	STATS_Net_OutBunches					= GStats.RegisterStats( STATSTYPE_Net,	  STATSDATATYPE_DWORD, TEXT("Bunches"			), TEXT("Net"		), STATSUNIT_Default					);
	STATS_Net_InBytes						= GStats.RegisterStats( STATSTYPE_Net,	  STATSDATATYPE_DWORD, TEXT("Bytes"				), TEXT("Net"		), STATSUNIT_Combined_Default_Default	);
	STATS_Net_OutBytes						= GStats.RegisterStats( STATSTYPE_Net,	  STATSDATATYPE_DWORD, TEXT("Bytes"				), TEXT("Net"		), STATSUNIT_Default					);
	STATS_Net_Speed							= GStats.RegisterStats( STATSTYPE_Net,	  STATSDATATYPE_DWORD, TEXT("Speed"				), TEXT("Net"		), STATSUNIT_Default					);
	STATS_Net_NumReps						= GStats.RegisterStats( STATSTYPE_Net,	  STATSDATATYPE_DWORD, TEXT("Reps"				), TEXT("Game"		), STATSUNIT_Default				);
	STATS_Net_NumRPC						= GStats.RegisterStats( STATSTYPE_Net,	  STATSDATATYPE_DWORD, TEXT("RPC"				), TEXT("Game"		), STATSUNIT_Default				);
	STATS_Net_NumPV							= GStats.RegisterStats( STATSTYPE_Net,	  STATSDATATYPE_DWORD, TEXT("PV"				), TEXT("Game"		), STATSUNIT_Default				);
	STATS_Net_ReceivedBunchCyles			= GStats.RegisterStats( STATSTYPE_Net,	  STATSDATATYPE_DWORD, TEXT("RecBunch"		), TEXT("Net"		), STATSUNIT_MSec					);
	STATS_Net_ReceivedRawBunchCyles			= GStats.RegisterStats( STATSTYPE_Net,	  STATSDATATYPE_DWORD, TEXT("RecRawBunch"		), TEXT("Net"		), STATSUNIT_MSec					);
	STATS_Net_ServetTickClientCyles			= GStats.RegisterStats( STATSTYPE_Net,	  STATSDATATYPE_DWORD, TEXT("SrvTickClient"		), TEXT("Net"		), STATSUNIT_MSec					);

	STATS_Temp_T1							= GStats.RegisterStats( STATSTYPE_Temp,	STATSDATATYPE_DWORD, TEXT("T1"), TEXT("Temp"), STATSUNIT_MSec	);
	STATS_Temp_T2							= GStats.RegisterStats( STATSTYPE_Temp,	STATSDATATYPE_DWORD, TEXT("T2"), TEXT("Temp"), STATSUNIT_MSec	);
	STATS_Temp_T3							= GStats.RegisterStats( STATSTYPE_Temp,	STATSDATATYPE_DWORD, TEXT("T3"), TEXT("Temp"), STATSUNIT_MSec	);
	STATS_Temp_T4							= GStats.RegisterStats( STATSTYPE_Temp,	STATSDATATYPE_DWORD, TEXT("T4"), TEXT("Temp"), STATSUNIT_MSec	);
	STATS_Temp_T5							= GStats.RegisterStats( STATSTYPE_Temp,	STATSDATATYPE_DWORD, TEXT("T5"), TEXT("Temp"), STATSUNIT_MSec	);
	STATS_Temp_T6							= GStats.RegisterStats( STATSTYPE_Temp,	STATSDATATYPE_DWORD, TEXT("T6"), TEXT("Temp"), STATSUNIT_MSec	);
	STATS_Temp_T7							= GStats.RegisterStats( STATSTYPE_Temp,	STATSDATATYPE_DWORD, TEXT("T7"), TEXT("Temp"), STATSUNIT_MSec	);
	STATS_Temp_T8							= GStats.RegisterStats( STATSTYPE_Temp,	STATSDATATYPE_DWORD, TEXT("T8"), TEXT("Temp"), STATSUNIT_MSec	);
	STATS_Temp_T9							= GStats.RegisterStats( STATSTYPE_Temp,	STATSDATATYPE_DWORD, TEXT("T9"), TEXT("Temp"), STATSUNIT_MSec	);
	STATS_Temp_T10							= GStats.RegisterStats( STATSTYPE_Temp,	STATSDATATYPE_DWORD, TEXT("T10"), TEXT("Temp"), STATSUNIT_MSec	);

	// Initialized.
	GStats.Registered[STATSTYPE_Render] = 1;
	GStats.Registered[STATSTYPE_Game]	= 1;
	GStats.Registered[STATSTYPE_Net]	= 1;
	GStats.Registered[STATSTYPE_Canvas] = 1;
	GStats.Registered[STATSTYPE_Temp] = 1;

	unguard;
}

// scion sz -
// show all stats of a certain type that are >= the TypeData value
void FStats::RenderUnitType( class UViewport* Viewport, class UEngine* Engine, EStatsUnit UnitType, void* TypeDataPtr, DWORD Present )
{
    guard(FStats::RenderUnitTypeMsec);
	check( TypeDataPtr );

	if( Present & STATSPRESENT_Log )
	{
		debugf( TEXT("=================") );
	}

	UCanvas* Canvas = NULL;
	INT CurX = 0;
	if( (Present & STATSPRESENT_Screen)
		&& Viewport )
	{
		Canvas = Viewport->Canvas;
#ifdef _XBOX
		CurX			= 38;
		Canvas->CurY	= 28;
#else
		CurX			= 8;
		Canvas->CurY	= 8;
#endif
		Canvas->Color = FColor(255,255,0);
	}	

	for( INT TypeIdx=0; TypeIdx < STATSTYPE_MAX; TypeIdx++ )
	{
		for( INT StatIdx=0; StatIdx < GStats.Stats[TypeIdx].Num(); StatIdx++ )	
		{
			FSingleStatsInfo& StatsInfo = GStats.Stats[TypeIdx](StatIdx);
			if( StatsInfo.StatsUnit == UnitType )
			{
				switch( UnitType )
				{
				case STATSUNIT_Default:
				case STATSUNIT_Byte:
					{
						DWORD Stat = DWORDOldStats(StatsInfo.Index);
						if( Stat >= *((DWORD*)TypeDataPtr) )
						{
							if( (Present & STATSPRESENT_Screen) && Canvas )
							{
								DRAW_STRING( FString::Printf(TEXT("%u %s"), Stat, *DWORDStatsDescriptions(StatsInfo.Index)) );
							}
							if( Present & STATSPRESENT_Log )
							{
								debugf( TEXT("%u %s"), Stat, *DWORDStatsDescriptions(StatsInfo.Index) );
							}
						}						
					}	
					break;
				case STATSUNIT_KByte:
					{
						DWORD Stat = DWORDOldStats(StatsInfo.Index)/1024;
						if( Stat >= *((DWORD*)TypeDataPtr) )
						{
							if( (Present & STATSPRESENT_Screen) && Canvas )
							{
								DRAW_STRING( FString::Printf(TEXT("%u KByte %s"), Stat, *DWORDStatsDescriptions(StatsInfo.Index)) );
							}
							if( Present & STATSPRESENT_Log )
							{
								debugf( TEXT("%u KByte %s"), Stat, *DWORDStatsDescriptions(StatsInfo.Index) );
							}
						}						
					}
					break;
				case STATSUNIT_MByte:
					{
						DWORD Stat = DWORDOldStats(StatsInfo.Index)/1024/1024;
						if( Stat >= *((DWORD*)TypeDataPtr) )
						{
							if( (Present & STATSPRESENT_Screen) && Canvas )
							{
								DRAW_STRING( FString::Printf(TEXT("%u MByte %s"), Stat, *DWORDStatsDescriptions(StatsInfo.Index)) );
							}
							if( Present & STATSPRESENT_Log )
							{
								debugf( TEXT("%u MByte %s"), Stat, *DWORDStatsDescriptions(StatsInfo.Index) );
							}
						}						
					}
					break;
				case STATSUNIT_MSec:
					{
						FLOAT Stat = ((FLOAT)DWORDOldStats(StatsInfo.Index)) * GSecondsPerCycle * 1000.0f;
						if( Stat >= *((FLOAT*)TypeDataPtr) )
						{
							if( (Present & STATSPRESENT_Screen) && Canvas )
							{
								DRAW_STRING( FString::Printf(TEXT("%2.1f %s ms"), Stat, *DWORDStatsDescriptions(StatsInfo.Index) ));
							}
							if( Present & STATSPRESENT_Log )
							{
								debugf( TEXT("%2.1f %s ms"), Stat, *DWORDStatsDescriptions(StatsInfo.Index) );
							}
						}						
					}
					break;
				}
			}			
		}
	}
	unguard;
}

#endif