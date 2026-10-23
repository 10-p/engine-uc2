/*=============================================================================
	UCHUD.cpp: See .UC for info
	Copyright 2003 Scion Studios.  All Rights Reserved.

Revision history:
	* Created by Mike Capps
=============================================================================*/
#include "UCGameplay.h"

IMPLEMENT_CLASS(AUCHUD);

// =======================================================================================================================================================
// =======================================================================================================================================================
// AUCHUD
// =======================================================================================================================================================
// =======================================================================================================================================================

FColor AUCHUD::GetColor(FName colorName)
{
	FColor newColor(255,255,255,255);
	INT idx = 0;
	for (; idx < Colors.Num(); idx++)
	{
		if (Colors(idx).ColorName == colorName)
		{
			newColor = Colors(idx).ActualColor;
			break;
		}
	}
#ifndef FINAL_RELEASE
	// if we didn't find it, log a warning
	if (idx == Colors.Num())
	{
		debugf(NAME_Warning, TEXT("Failed to find color matching colorName %s"), *colorName);
	}
#endif

	return newColor;
}

void AUCHUD::execGetColor(FFrame &Stack, RESULT_DECL)
{
	guard(AUCHUD::execGetColor);

	P_GET_NAME(colorName);
	P_FINISH;

	*(FColor*)Result = GetColor(colorName);

	unguardexec;
}

void AUCHUD::DrawIcon(UCanvas *C, UMaterial *material, FIconCoords &coords, FIconPosn &posn)
{
	guardSlow(AUCHUD::DrawIcon);

	checkSlow(C != NULL);
	checkSlow(material != NULL);
	// fill in UL/VL if not specified
	if (coords.UL == 0 &&
		coords.VL == 0)
	{
		coords.UL = material->MaterialUSize();
		coords.VL = material->MaterialVSize();
	}

	FLOAT X = posn.X + SafeZoneOffsetX,
		Y = posn.Y + SafeZoneOffsetY;

	C->Style = (coords.Style != STY_None) ? coords.Style : STY_Alpha;
	if (coords.Alpha != 0)
	{
		C->Color.A = coords.Alpha;
	}
	if (coords.bDrawStretched)
	{
		C->DrawTileStretchedUV(material, C->OrgX + X, C->OrgY + Y, posn.W, posn.H, coords.U, coords.V, coords.UL, coords.VL);
	}
	else
	{
		C->DrawTile(material, C->OrgX + X, C->OrgY + Y, posn.W, posn.H, coords.U, coords.V, coords.UL, coords.VL, 0, C->Color);
	}

	unguardSlow;
}

void AUCHUD::execDrawIcon ( FFrame& Stack, RESULT_DECL )
{
	guard(AUCHUD::execDrawIcon);
	P_GET_OBJECT(UCanvas, can);
	P_GET_OBJECT(UMaterial, mat);
	P_GET_STRUCT_OPTX(FIconCoords, coords, FIconCoords());
	P_GET_STRUCT_OPTX(FIconPosn, posn, FIconPosn());
	P_FINISH;
	
	DrawIcon(can,mat,coords,posn);

	unguardexec;
}

void AUCHUD::execDrawVitals(FFrame &Stack, RESULT_DECL)
{
	guard(AUCHUD::execDrawVitals);

	P_GET_OBJECT(UCanvas, C);
	P_GET_OBJECT(APawn, P);
	P_FINISH;

	DrawVitalsAt(C, P, 0, 0);

	unguardexec;
}

// DrawVitals on a window wider than the UI box (GetSideShifts): the vitals and the active combos LeftShift design
// pixels from their 4:3 place, the negative combos RightShift; the combo menu stays centred.
void AUCHUD::execDrawVitalsWide(FFrame &Stack, RESULT_DECL)
{
	guard(AUCHUD::execDrawVitalsWide);

	P_GET_OBJECT(UCanvas, C);
	P_GET_OBJECT(APawn, P);
	P_GET_INT(LeftShift);
	P_GET_INT(RightShift);
	P_FINISH;

	DrawVitalsAt(C, P, LeftShift, RightShift);

	unguardexec;
}

void AUCHUD::DrawVitalsAt( UCanvas* C, APawn* P, INT LeftShift, INT RightShift )
{
	guard(AUCHUD::DrawVitalsAt);

	if (P)
	{
		// the vitals are drawn at SafeZoneOffsetX, moved by LeftShift for their part
		INT OldOffsetX = SafeZoneOffsetX;
		SafeZoneOffsetX += LeftShift;

		DrawVitalsBackground(C, P);
		DrawVitalsOutlines(C, P);

		DrawHealthMeter(C, P);
		if (P->bDrawAdrenalineOnHUD)
		{
			DrawAdrenalineMeter(C, P);
		}
#ifndef FINAL_RELEASE
		if (bDebugNumbers)
		{
			DrawVitalsDebug(C, P);
		}
#endif
		SafeZoneOffsetX = OldOffsetX;

		AUCPawn* UCP = Cast<AUCPawn>(P);
		if (UCP)
		{
			DrawCombos(C, UCP, LeftShift, RightShift);
		}
	}

	unguard;
}

// The size DrawTextDualFont gives a text on one line, measured by its own layout with nothing drawn.
void AUCHUD::execTextSizeDualFont(FFrame &Stack, RESULT_DECL)
{
	guard(AUCHUD::execTextSizeDualFont);

	P_GET_OBJECT(UCanvas, C);
	P_GET_STR(Text);
	P_GET_OBJECT(UFont, SecondaryFont);
	P_GET_FLOAT_REF(XL);
	P_GET_FLOAT_REF(YL);
	P_FINISH;

	*XL = 0.f;
	*YL = 0.f;
	if (C && SecondaryFont)
	{
		FLOAT OldX = C->CurX, OldY = C->CurY, OldYL = C->CurYL;
		C->CurX = 0.f;
		C->CurY = 0.f;
		C->CurYL = 0.f;
		C->DrawTextDualFont(*Text, SecondaryFont, 1.f, 1.f, 0.f, 0, 0);
		*XL = C->CurX;
		*YL = C->CurYL;
		C->CurX = OldX;
		C->CurY = OldY;
		C->CurYL = OldYL;
	}

	unguardexec;
}

// The side groups' shifts on a window wider than the UI box: how far the window reaches past the 640-wide design
// space on each side, in whole design pixels (LeftShift 0 or less, RightShift 0 or more); 0 and 0 when the UI is
// not boxed.
void AUCHUD::execGetSideShifts(FFrame &Stack, RESULT_DECL)
{
	guard(AUCHUD::execGetSideShifts);

	P_GET_OBJECT(UCanvas, C);
	P_GET_INT_REF(LeftShift);
	P_GET_INT_REF(RightShift);
	P_FINISH;

	*LeftShift = 0;
	*RightShift = 0;
	if (C && C->IsUIBoxed())
	{
		FLOAT X1, Y1, X2, Y2;
		C->GetTargetInDesign(X1, Y1, X2, Y2);
		*LeftShift = -Max(0, appRound(-X1));
		*RightShift = Max(0, appRound(X2 - C->SizeX));
	}

	unguardexec;
}

//	DRAW SHARED BACKGROUND
void AUCHUD::DrawVitalsBackground( UCanvas* C, APawn* P )
{
	guard(AUCHUD::DrawVitalsBackground);
	checkSlow(C);
	checkSlow(P);

	// cache names for getting colors
	static FName NAME_Vitals_Back = FName(TEXT("Vitals_Back"));
	static FName NAME_Adren_Disabled = FName(TEXT("Adren_Disabled"));

	C->Color = GetColor(NAME_Vitals_Back);

	// Draw a semi-transparent shader background behind the health region
	DrawIcon(C,tHUD,coord_HealthBackground,pos_HealthBackground);

	//epic moelfke: only if adrenaline enabled
	if (P->bDrawAdrenalineOnHUD)
	{
		if (!P->bAdrenalineEnabled)
		{
			C->Color = GetColor(NAME_Adren_Disabled);
		}

		// Draw a semi-tranparent shader behind the adrenaline area

		// Left adrenaline bar
		DrawIcon(C,tHUD,coord_AdrenBackground,pos_AdrenBackgroundLeft);

		// right adrenaline bar
		DrawIcon(C,tHUD,coord_AdrenBackground,pos_AdrenBackgroundRight);

		// adrenaline symbol thing
		DrawIcon(C,tHUD,coord_AdrenTabBackground,pos_AdrenTabBackground);
	}

	unguard;
}

// DRAW SHARED OUTLINES
void AUCHUD::DrawVitalsOutlines( UCanvas* C, APawn* P )
{
	guard(AUCHUD::DrawVitalsOutlines);
	checkSlow(C);
	checkSlow(P);

	// cache names for getting colors
	static FName NAME_Vitals_Line = FName(TEXT("Vitals_Line"));
	static FName NAME_Adren_Disabled = FName(TEXT("Adren_Disabled"));

	C->Color = GetColor(NAME_Vitals_Line);

	// Draw the outline around the health bars
	DrawIcon(C,tHUD,coord_HealthOutline,pos_HealthOutline);	

	//epic moelfke: only if adrenaline enabled
	if (P->bDrawAdrenalineOnHUD)
	{
		if (!P->bAdrenalineEnabled)
		{
			C->Color = GetColor(NAME_Adren_Disabled);
		}

		// Draw the outline around the adrenaline bars

		// left adrenaline bar
		DrawIcon(C,tHUD,coord_AdrenOutlineLeft,pos_AdrenOutlineLineLeft);

		// right adrenaline bar
		DrawIcon(C,tHUD,coord_AdrenLineRight,pos_AdrenOutlineRight);

		// adrenaline icon symbol thing
		DrawIcon(C,tHUD,coord_AdrenTabOutline,pos_AdrenTabOutline);
	}


	unguard;
}

// DRAW HEALTH
void AUCHUD::DrawHealthMeter( UCanvas* C, APawn* P )
{
	guard(AUCHUD::DrawHealthMeter);
	checkSlow(C);
	checkSlow(P);

	// cache names for getting colors
	static FName NAME_Symbols		= FName(TEXT("Symbols"));
	static FName NAME_Health_Fill	= FName(TEXT("Health_Fill"));
	static FName NAME_Health_Drain	= FName(TEXT("Health_Drain"));
	static FName NAME_Health_Amt	= FName(TEXT("Health_Amt"));

	// health symbol
	C->Color = GetColor(NAME_Symbols);
	DrawIcon(C,tHUD,coord_HealthSymbol,pos_HealthSymbol);			

	FLOAT fHealthPercentage = Min<FLOAT>(1.f,P->Health/(FLOAT)P->MaxHealthAmount);
	FLOAT fAdjustPortion = fLastHealthPercentage - fHealthPercentage;

	// if health has been gained, flip the values around so bar slowly increases to new value
	UBOOL bHealthGain = 0;
	if (fAdjustPortion < 0.f)
	{
		bHealthGain = 1;
		fHealthPercentage += fAdjustPortion;
		fAdjustPortion = -fAdjustPortion;
	}

	// If pos_HealthFill.W is less than zero, we're in split screen, so we need to remap the
	// health bar so that the fill doesn't get drawn backwards
	UBOOL bRemapHealthBar = pos_HealthFill.W < 0;

	FLOAT X = C->OrgX + pos_HealthFill.X + SafeZoneOffsetX,
		Y = C->OrgY + pos_HealthFill.Y + SafeZoneOffsetY;

	// Draw Red Diminished Health Section
	if (fAdjustPortion > 0.01)
	{
		fAdjustPortion		+= fHealthPercentage;

		if( bHealthGain )
		{
			C->Color = GetColor( NAME_Health_Fill );
		}
		else
		{
			C->Color = GetColor( NAME_Health_Drain );
		}

		FLOAT XL = pos_HealthFill.W * fAdjustPortion;
		FLOAT NewX = bRemapHealthBar
			? X + pos_HealthFill.W * (1.f - fAdjustPortion)
			: X;

		FLOAT UL = coord_HealthFill.UL * fAdjustPortion;
		FLOAT U = bRemapHealthBar
			? coord_HealthFill.U + (coord_HealthFill.UL - UL)
			: coord_HealthFill.U;

		C->DrawTile(tHUD,
			NewX, Y,
			XL, pos_HealthFill.H,
			U, coord_HealthFill.V,
			UL, coord_HealthFill.VL,
			0,
			C->Color);
	}

	// Draw health bar
	if ( fHealthPercentage > 0.01 ) // don't draw if not enough to make it worthwhile
	{
		C->Color = GetColor(NAME_Health_Amt);
		C->Color.A = HealthAlpha;

		FLOAT XL = pos_HealthFill.W * fHealthPercentage;
		FLOAT NewX = bRemapHealthBar
			? X + pos_HealthFill.W * (1.f - fHealthPercentage)
			: X;

		FLOAT UL = coord_HealthFill.UL * fHealthPercentage;
		FLOAT U = bRemapHealthBar 
			? coord_HealthFill.U + (coord_HealthFill.UL - UL)
			: coord_HealthFill.U;


		C->DrawTile(tHUD,
			NewX, Y,
			XL, pos_HealthFill.H,
			U, coord_HealthFill.V,
			UL, coord_HealthFill.VL,
			0,
			C->Color);
	}

	// draw health over starting levels, and tip
	if (fLastSuperHealthPercentage > 0.f || P->Health > P->MaxHealthAmount)
	{
		fHealthPercentage = Max<FLOAT>(0.f, float(P->Health - P->MaxHealthAmount) / float(P->SuperHealthAmount - P->MaxHealthAmount));
		FLOAT fAdjustPortion = fLastSuperHealthPercentage - fHealthPercentage;

		X = C->OrgX + pos_HiHealthFill.X + SafeZoneOffsetX;
		Y = C->OrgY + pos_HiHealthFill.Y + SafeZoneOffsetY;
		// if health has been gained, flip the values around so bar slowly increases to new value
		UBOOL bHealthGain = 0;
		if (fAdjustPortion < 0.f)
		{
			bHealthGain = 1;
			fHealthPercentage += fAdjustPortion;
			fAdjustPortion = -fAdjustPortion;
		}

		// Draw Red Diminished Health Section
		if (fAdjustPortion > 0.01)
		{
			fAdjustPortion		+= fHealthPercentage;

			if( bHealthGain )
			{
				C->Color = GetColor( NAME_Health_Fill );
			}
			else
			{
				C->Color = GetColor( NAME_Health_Drain );
			}

			FLOAT XL = pos_HiHealthFill.W * fAdjustPortion;
			FLOAT NewX = bRemapHealthBar
				? X + pos_HiHealthFill.W * (1.f - fAdjustPortion)
				: X;

			FLOAT UL = coord_HiHealthFill.UL * fAdjustPortion;
			FLOAT U = bRemapHealthBar
				? coord_HiHealthFill.U + (coord_HiHealthFill.UL - UL)
				: coord_HiHealthFill.U;

			C->DrawTile(tHUD,
				NewX, Y,
				XL, pos_HiHealthFill.H,
				U, coord_HiHealthFill.V,
				UL, coord_HiHealthFill.VL,
				0,
				C->Color);
		}
		// Draw health bar
		if ( fHealthPercentage > 0.01 ) // don't draw if not enough to make it worthwhile
		{
			C->Color = GetColor(NAME_Health_Amt);
			FLOAT XL = pos_HiHealthFill.W * fHealthPercentage;
			FLOAT NewX = bRemapHealthBar
				? X + pos_HiHealthFill.W * (1.f - fHealthPercentage)
				: X;

			FLOAT UL = coord_HiHealthFill.UL * fHealthPercentage;
			FLOAT U = bRemapHealthBar
				? coord_HiHealthFill.U + (coord_HiHealthFill.U - UL)
				: coord_HiHealthFill.U;

			C->DrawTile(tHUD,
				NewX, Y,
				XL, pos_HiHealthFill.H,
				U, coord_HiHealthFill.V,
				UL, coord_HiHealthFill.VL,
				0,
				C->Color);
		}
	}
	unguard;
}

// DRAW ADRENALINE
void AUCHUD::DrawAdrenalineMeter( UCanvas* C, APawn* P )
{
	guard(AUCHUD::DrawAdrenalineMeter);
	checkSlow(C);
	checkSlow(P);

	// cache names for getting colors
	static FName NAME_Symbols = FName(TEXT("Symbols"));
	static FName NAME_Adren_Fill = FName(TEXT("Adren_Fill"));
	static FName NAME_Adren_Bright = FName(TEXT("Adren_Bright"));
	static FName NAME_Adren_Normal = FName(TEXT("Adren_Normal"));
	static FName NAME_Adren_Disabled = FName(TEXT("Adren_Disabled"));

	// determine the percentage of adrenaline we currently have
	FLOAT fAdrenPercentage = P->Adrenaline / P->AdrenalineMax;

	UBOOL bAdrenIncreased = fAdrenPercentage > fLastAdrenPercentage;
	UBOOL bAdrenDecreased = fAdrenPercentage < fLastAdrenPercentage;

	// If we picked up some adrenaline
	if ( bAdrenIncreased )
	{
		// clamp the increase in percentage so that the bar appears to slowly fill up 
		fAdrenPercentage = Min<FLOAT>( fLastAdrenPercentage + 0.01f, fAdrenPercentage );
		fLastAdrenPercentage = fAdrenPercentage;
	}
	else if ( bAdrenDecreased )
	{
		// likewise, if we lost some adrenaline, clamp the decrease in percentage so that
		// the bar appears to dwindle away
		fAdrenPercentage = Max<FLOAT>( fLastAdrenPercentage - 0.01f, fAdrenPercentage );
		fLastAdrenPercentage = fAdrenPercentage;
	}

	// adren symbol
	C->Color = GetColor(NAME_Symbols);
	DrawIcon(C,tHUD,coord_AdrenSymbol,pos_AdrenSymbol);				

	// Initialize the X / Y position of the adrenaline bar based on the position
	// of the adrenaline outline
	FLOAT X = pos_AdrenOutlineLineLeft.X + 1, Y = pos_AdrenOutlineLineLeft.Y + 1;

	// draw all the adrenaline chunks
	for (INT i = 0; i < ARRAY_COUNT(AdrenalineBlock); i++)
	{
		FAdrenalineChunk& Block = AdrenalineBlock[i];
		// determine how much of this chunk should be displayed, based on the current amount of adrenaline
		FLOAT chunkPct = Clamp<FLOAT>((fAdrenPercentage - (i * 0.1f))/0.1f,0.f,1.f);

		// if we don't have enough adrenaline to draw this chunk at all, just
		// make sure the chunk is in the correct state, then move on to the next one
		if (chunkPct <= 0.f)
		{
			SetAdrenalineChunkState(Block, ADST_None, 0.f);
			continue;
		}

		// otherwise, if adrenaline is enabled
		else if (P->bAdrenalineEnabled)
		{
			// otherwise, we need to find out if this chunk should be changing state, based on the chunkPct
			// combined with the chunk's state time remaining (how much time is left to its current state)

			// if the chunkPct is less than 0.98 (0.98 to account for precision errors?),
			// then it should be in the PartialFill state, which is an untimed state
			if (chunkPct < 0.98f || Block.ChunkState == ADST_None)
			{
				SetAdrenalineChunkState(Block, ADST_PartialFill, 0.f);

				// use the "not-yet-ready" color
				Block.ChunkColor = GetColor(NAME_Adren_Fill);
			}
			else
			{
				// check the chunk's state timer 
				if ( Block.NextStateChange < Level->TimeSeconds )
				{
                    // if it's less than Level->TimeSeconds, it's time to advance the chunk's state, and reset 
					// the state timer to whatever value is associated with that chunk state
					switch ( Block.ChunkState )
					{
					// if the chunk is in the partial fill state
					case ADST_PartialFill:
						// change state to filled
						// and set the state timer to the number of seconds that should pass before the chunk can 
						// enter the popping state -- this equates to the delay after a chunk has become completely
						// filled before it will begin the popping animation (less than a second)
						SetAdrenalineChunkState(Block, ADST_Filled, AdrenFillPause);
						break;

					case ADST_Filled:
						// if we're in the filled state, change the chunk's state to the popping state,
						// and set the timer to flash time
						SetAdrenalineChunkState(Block, ADST_Popping, AdrenPopSeconds);

						// since we've just transitioned out of the filled state, we'll use the same color
						Block.ChunkColor.Lerp(GetColor(NAME_Adren_Bright), GetLerpAmount(Block));
						break;

					case ADST_Popping:
						// if we're in the popping state, change the chunk's state to the full state,
						// which is an untimed state
						SetAdrenalineChunkState(Block, ADST_Full, AdrenFadeSeconds);
						Block.ChunkColor.Lerp(GetColor(NAME_Adren_Normal), 0.5f);
						break;

					case ADST_Full:
						// if we are the final chunk of our group
						if ( i == 4 || i == 9 )
						{
							// begin the knight rider animation for all blocks up to and including this one
							for ( INT j = 0; j <= i; j++ )
								SetAdrenalineChunkState(AdrenalineBlock[j], ADST_WaveWait, AdrenWaveSeconds * AdrenWaveMultiplier * (j+1));
						}

						break;

					case ADST_PulseWait:
						// the pulse cycle time has ended, so we need to generate another pulse for this block
						// then setup the chunk to animate up to the brighter color
						SetAdrenalineChunkState(Block, ADST_PulsePopping, AdrenPopSeconds);
						break;

					case ADST_PulsePopping:
						SetAdrenalineChunkState(Block, ADST_PulseFull, AdrenFadeSeconds);
						Block.ChunkColor.Lerp(GetColor(NAME_Adren_Normal), 0.5f);
						break;

					case ADST_PulseFull:
						SetAdrenalineChunkState(Block, ADST_PulseWait, AdrenPulseCycleSeconds);
						break;

					case ADST_WaveWait:
						SetAdrenalineChunkState(Block, ADST_WavePassA, AdrenWaveSeconds);
						break;

					case ADST_WavePassA:
						SetAdrenalineChunkState(Block, ADST_WavePassB, AdrenWaveSeconds);
						break;

					case ADST_WavePassB:
						SetAdrenalineChunkState(Block, ADST_WavePassC, AdrenWaveSeconds);
						break;

					case ADST_WavePassC:
						if ( i == 4 || i == 9 )
						{
							// make sure all blocks are set to cycle at the exact same moment
							for ( INT j = 0; j <= i; j++ )
                                SetAdrenalineChunkState( AdrenalineBlock[j], ADST_PulseWait, (AdrenPulseCycleSeconds + (AdrenWaveSeconds * AdrenWaveMultiplier * (UCONST_MAX_ADREN_CHUNKS - i - 1))));
						}
						break;

					default:
						debugf(TEXT("Unknown state in DrawAdrenalineMeter"));
						break;
					}
				}
				else
				{
					// otherwise, modify the color of the chunk based on its state
					switch ( Block.ChunkState )
					{
					case ADST_Filled:
						break;

					case ADST_Popping:
						// lerp between the dull color and a brighter version of the filled color
						Block.ChunkColor.Lerp(GetColor(NAME_Adren_Bright), GetLerpAmount(Block));
						break;

					case ADST_Full:
						Block.ChunkColor = GetColor(NAME_Adren_Normal);
						Block.NextStateChange = Level->TimeSeconds;
						Block.StateDuration = 0.f;
						break;

					case ADST_PulseWait:
						break;

					case ADST_PulsePopping:
						// lerp between the dull color and a brighter version of the filled color
						Block.ChunkColor.Lerp(GetColor(NAME_Adren_Bright), GetLerpAmount(Block));
						break;


					case ADST_PulseFull:
						Block.ChunkColor.Lerp(GetColor(NAME_Adren_Normal), GetLerpAmount(Block));
						break;

					case ADST_WaveWait:
						break;

					case ADST_WavePassA:
						Block.ChunkColor.Lerp(GetColor(NAME_Adren_Fill), GetLerpAmount(Block));
						break;

					case ADST_WavePassB:
						Block.ChunkColor.Lerp(GetColor(NAME_Adren_Bright), GetLerpAmount(Block));
						break;

					case ADST_WavePassC:
						Block.ChunkColor.Lerp(GetColor(NAME_Adren_Normal), GetLerpAmount(Block));
						break;

					}
				}
			}
		}
		else
			Block.ChunkColor = GetColor(NAME_Adren_Disabled);


		// draw the adrenaline chunk to the canvas
		C->DrawTile(tHUD,
			X + SafeZoneOffsetX, Y + SafeZoneOffsetY,
			pos_AdrenChunk.W * chunkPct, pos_AdrenChunk.H,
			coord_AdrenChunk.U, coord_AdrenChunk.V,
			coord_AdrenChunk.UL * chunkPct, coord_AdrenChunk.VL,
			0,
			Block.ChunkColor);

		// advance our X position to prepare to draw the next chunk
		// the 6 used here is arbitrary based on trial and error, to have the chunks
		// line up properly
		X += pos_AdrenChunk.W - 6;

		// If we've just drawn the 5th chunk, we'll need to reset our position
		// to the next adrenaline bar, which is on the right side, and a little lower
		// once again, based to the position of the adrenaline bar's outline
		if (i == 4)
		{
			X = pos_AdrenOutlineRight.X + 1;
			Y = pos_AdrenOutlineRight.Y + 1;
		}
	}

	unguard;
}

FLOAT AUCHUD::GetLerpAmount( const FAdrenalineChunk& Block ) const
{
	checkSlow(Block.StateDuration > 0.f);

	// determine how much of the total duration should be used for this lerp percentage calculation.
	// this allows non-linear lerps (but it can't be 0, or we'll have a division-by-zero)
	FLOAT FrameDuration = Max<FLOAT>(0.0001f, Block.StateDuration * AdrenCurveMultiplier);

	return Max<FLOAT>(0.f, 1.f - ((Block.NextStateChange - Level->TimeSeconds) / FrameDuration));
}

void AUCHUD::SetAdrenalineChunkState( FAdrenalineChunk& Block, EAdrenalineChunkState NewState, float Duration ) const
{
	guard(AUCHUD::SetAdrenalineChunkState);

	// set the block's state to the new state
	Block.ChunkState = NewState;

	// set the block's duration
	Block.StateDuration = Duration;

	// setup the block's next state change by adding current time to the duration.
	Block.NextStateChange = Level->TimeSeconds + Duration;

	unguard;
}

// Called from PlayerViewTargetChanged - indicates that we should reset the states of all our adrenaline blocks
// to immediately represent where they should be in the animation loop
void AUCHUD::execResetAdrenaline( FFrame& Stack, RESULT_DECL )
{
	guard(AUCHUD::execResetAdrenaline);

	P_FINISH;

	// determine the amount of adrenaline we now have
	APawn* P = PlayerOwner ? Cast<APawn>(PlayerOwner->ViewTarget) : NULL;
	FLOAT fAdrenPercentage = P ? P->Adrenaline / P->AdrenalineMax : 0.f;

	// ideally, we'd like to be able to change the state for
	// each adrenaline chunk to match up exactly with what the new
	// viewtarget is seeing, but this isn't possible (or it isn't worth it anyway)
	// So, just try to determine which state each block is most likely to be in
	// and force the block into that state
	for ( INT i = 0; i < ARRAY_COUNT(AdrenalineBlock); i++ )
	{
		FAdrenalineChunk& Block = AdrenalineBlock[i];

		// determine how much of this chunk should be displayed, based on the current amount of adrenaline
		FLOAT chunkPct = Clamp<FLOAT>((fAdrenPercentage - (i * 0.1f))/0.1f,0.f,1.f);

		// if we don't have enough adrenaline to draw this chunk at all, just
		// make sure the chunk is in the correct state, then move on to the next one
		if (chunkPct <= 0.f)
		{
			SetAdrenalineChunkState(Block, ADST_None, 0.f);
			continue;
		}

		// otherwise, if adrenaline is enabled
		else if (P->bAdrenalineEnabled)
		{
			// if the chunkPct is less than 0.98 (0.98 to account for precision errors?),
			// then it should be in the PartialFill state, which is an untimed state
			// (that is, the next state change will happen as a result of some event, not
			// a timer).
			if (chunkPct < 0.98f)
			{
				SetAdrenalineChunkState(Block, ADST_PartialFill, 0.f);

				// use the "not-yet-ready" color
				Block.ChunkColor = GetColor(TEXT("Adren_Fill"));
			}
			else
			{
				// change the color of this block to the "full" color
				Block.ChunkColor = GetColor(TEXT("Adren_Normal"));

				// if this is the last chunk of a group, then this group was [probably] already full.
				// We may be able to skip straight to the pulsing phase of the animation cycle, so that
				// we don't perform the wave animation each time we change view targets
				if ( i == 4 || i == 9 )
				{
					for ( INT j = 0; j < i; j++ )
						SetAdrenalineChunkState(AdrenalineBlock[j], ADST_PulsePopping, AdrenPopSeconds);
				}
				else
				{
					// otherwise, force this chunk straight into the main (full) state, 
					// and let the regular adrenaline bar rendering code handle the rest
					SetAdrenalineChunkState(Block, ADST_Full, 0.f);
				}
			}
		}
		else
			Block.ChunkColor = GetColor(TEXT("Adren_Disabled"));
	}


	unguardexec;
}

void AUCHUD::DrawVitalsDebug( UCanvas* C, APawn* P )
{
	guard(AUCHUD::DrawVitalsDebug);
	checkSlow(C);
	checkSlow(P);

	//debug superville: Draw numbers on hud
	if( !C->Font ) 
	{
		C->Font = Cast<UFont>(StaticLoadObject(UFont::StaticClass(),NULL,TEXT("T_Fonts2.SuiGeneris_18pt"),NULL,LOAD_NoWarn,NULL));
	}

	if( C->Style != STY_None )
	{
		FLOAT XL=0, YL=0;
		C->CurX = 0;
		C->CurY = C->SizeY / 3.0;
		C->ClippedStrLen( C->Font, 1.0, 1.0, XL, YL, TEXT("Health: ###" ) );
		C->WrappedPrintf( C->Font, false, TEXT("Health: %d"), P->Health );
		C->CurY += (YL * 1.1);
		C->WrappedPrintf( C->Font, false, TEXT("Adren: %d"), INT(P->Adrenaline) );
		if (P->Weapon)
		{
			C->CurY += (YL * 1.1);
			C->WrappedPrintf(C->Font, false, TEXT("Ammo: %d"), INT(P->Weapon->eventGetAmmoAmount()));
		}
	}

	unguard;
}

/* epic ===============================================
* ::DrawCombo
* Author: moelfke
*
* Draws a single combo icon
*
* =====================================================
*/
void AUCHUD::DrawCombo(UCanvas* C, UClass* ComboClass, AUCCombo* Combo, FIconPosn ComboPosn, const FColor& ComboColor, const FColor& ComboFillColor)
{
	guard(AUCHUD::DrawCombo);

	checkSlow(ComboClass || Combo);

	// if active combo, draw the duration as a fill
	FLOAT Percent = 0.f;
	if (Combo && !bDebugComboHUD)
	{
		// start drawing 'perc' from top, draw for '1-perc' distance, using that part of the texture map
		Percent = Min<FLOAT>(0.85f, (Combo->TotalDuration - Combo->Duration) / Combo->TotalDuration);
	}

	ComboPosn.X += SafeZoneOffsetX;
	ComboPosn.Y += SafeZoneOffsetY;

	// draw the fill
	C->DrawTile( ComboIconBaseMaterial, C->OrgX + ComboPosn.X, C->OrgY + ComboPosn.Y + (ComboPosn.H * Percent),
				ComboPosn.W, (ComboPosn.H * (1.f - Percent)), ComboIconFillIndexX * UCONST_COMBO_ICON_SIZE,
				ComboIconFillIndexY * UCONST_COMBO_ICON_SIZE + UCONST_COMBO_ICON_SIZE * Percent, UCONST_COMBO_ICON_SIZE,
				UCONST_COMBO_ICON_SIZE * (1.f - Percent), C->CurZ, ComboFillColor.Plane() );
	// draw the outline
	C->DrawTile( ComboIconBaseMaterial, C->OrgX + ComboPosn.X, ComboPosn.Y, ComboPosn.W, ComboPosn.H,
				ComboIconOutlineIndexX * UCONST_COMBO_ICON_SIZE, ComboIconOutlineIndexY * UCONST_COMBO_ICON_SIZE,
				UCONST_COMBO_ICON_SIZE, UCONST_COMBO_ICON_SIZE, C->CurZ, ComboColor.Plane() );
	
	// draw the center icon
	if (!Combo)
	{
		Combo = ComboClass->GetDefaultObject<AUCCombo>();
	}
	if (Combo->IconMaterial)
	{
		C->DrawTile( Combo->IconMaterial, C->OrgX + ComboPosn.X + (ComboPosn.W - UCONST_COMBO_ICON_SIZE) * 0.5f,
					C->OrgY + ComboPosn.Y + (ComboPosn.H - UCONST_COMBO_ICON_SIZE) * 0.5f, UCONST_COMBO_ICON_SIZE,
					UCONST_COMBO_ICON_SIZE, Combo->IconIndexX * UCONST_COMBO_ICON_SIZE, Combo->IconIndexY * UCONST_COMBO_ICON_SIZE,
					UCONST_COMBO_ICON_SIZE, UCONST_COMBO_ICON_SIZE, C->CurZ, cHUDWhite.Plane() );
	}
	else
	{
		debugf(NAME_Warning, TEXT("%s has no icon material!"), Combo->GetName());
	}

	unguard;
}

/* epic ===============================================
* ::DrawCombos
* Author: moelfke
*
* Draws icons for all of P's currently active combos
* and the combo menu if it is active
*
* =====================================================
*/
void AUCHUD::DrawCombos(UCanvas* C, AUCPawn* P, INT LeftShift, INT RightShift)
{
	guard(AUCHUD::DrawCombos);

	// cache names for getting colors
	static FName NAME_Combo_Neg = FName(TEXT("Combo_Neg"));
	static FName NAME_Combo_Neg_Fill = FName(TEXT("Combo_Neg_Fill"));
	static FName NAME_Combo = FName(TEXT("Combo"));
	static FName NAME_Combo_Fill = FName(TEXT("Combo_Fill"));

	// draw active combos
	INT NegComboCount = 0;
	INT PosComboCount = 0;
	for (AUCCombo* Combo = P->CurrentCombo; Combo; Combo = Combo->NextCombo)
	{
		if (!Combo->bNoDraw)
		{
			// the negative combos keep to the right side, the active ones to the left
			if (Combo->bNegativeCombo || bDebugNegComboHUD)
			{
				FIconPosn Posn = pos_NegCombo(NegComboCount++);
				Posn.X += RightShift;
				DrawCombo(C, Combo->GetClass(), Combo, Posn, GetColor(NAME_Combo_Neg), GetColor(NAME_Combo_Neg_Fill));
			}
			else
			{
				FIconPosn Posn = pos_ActiveCombo(PosComboCount++);
				Posn.X += LeftShift;
				DrawCombo(C, Combo->GetClass(), Combo, Posn, GetColor(NAME_Combo), GetColor(NAME_Combo_Fill));
			}

			Combo->eventDrawCombo(C, this);
		}
	}

	DrawComboMenu(C, P);

	unguard;
}

/* epic ===============================================
* ::DrawComboMenu
* Author: moelfke
*
* Draws the combo menu, if active
*
* =====================================================
*/
void AUCHUD::DrawComboMenu(UCanvas* C, AUCPawn* P)
{
	guard(AUCHUD::DrawComboMenu);

	// cache names for getting colors
	static FName NAME_Combo_Inactive = FName(TEXT("Combo_Inactive"));
	static FName NAME_Combo_Inactive_Fill = FName(TEXT("Combo_Inactive_Fill"));
	static FName NAME_Combo = FName(TEXT("Combo"));
	static FName NAME_Combo_Fill = FName(TEXT("Combo_Fill"));

	// draw combo menu, if activated or we're debugging the HUD
	AUCPlayerController* UCP = Cast<AUCPlayerController>(PlayerOwner);
	if (bDebugComboHUD || (UCP && UCP->ComboLevel != 0 && !P->bIsStunned))
	{
		// the menu's icons (pos_Combo, about the centre) with the middle of their extent on the canvas's centre
		FLOAT MinX = 0.f, MaxX = 0.f, MinY = 0.f, MaxY = 0.f;
		for (INT i = 0; i < UCONST_COMBO_INDEX_AWAKEN && i < pos_Combo.Num(); i++)
		{
			const FIconPosn& Posn = pos_Combo(i);
			MinX = i ? Min(MinX, Posn.X) : Posn.X;
			MaxX = i ? Max(MaxX, Posn.X + Posn.W) : Posn.X + Posn.W;
			MinY = i ? Min(MinY, Posn.Y) : Posn.Y;
			MaxY = i ? Max(MaxY, Posn.Y + Posn.H) : Posn.Y + Posn.H;
		}
		FLOAT CenterX = C->SizeX * 0.5f - (MinX + MaxX) * 0.5f;
		FLOAT CenterY = C->SizeY * 0.5f - (MinY + MaxY) * 0.5f;
		for (INT i = 0; i < UCONST_COMBO_INDEX_AWAKEN; i++)
		{
			AUCCombo* DefaultCombo = P->ComboList(i) ? P->ComboList(i)->GetDefaultObject<AUCCombo>() : NULL;
			if (DefaultCombo && !DefaultCombo->bNoDraw)
			{
				// center this icon on the screen
				FIconPosn DrawComboPosn;
				DrawComboPosn.X = CenterX + pos_Combo(i).X;
				DrawComboPosn.Y = CenterY + pos_Combo(i).Y;
				DrawComboPosn.W = pos_Combo(i).W;
				DrawComboPosn.H = pos_Combo(i).H;
				// if on a different combo level or is currently invalid 
				// (checked by script every so often through ComboClass.CheckCondiational)
				if ( !bDebugComboHUD && 
						((i >= 3 && UCP->ComboLevel == 1) || 
						 (i < 3 && UCP->ComboLevel == 2)  ||
						 (P->Adrenaline < DefaultCombo->AdrenalineCost) ||
						 (i >= 0 && i < P->ComboListValid.Num() && P->ComboListValid(i) == 0))	  )
				{
					// draw grayed out
					DrawCombo(C, DefaultCombo->GetClass(), NULL, DrawComboPosn, GetColor(NAME_Combo_Inactive), GetColor(NAME_Combo_Inactive_Fill));
				}
				else
				{
					// otherwise draw normally
					DrawCombo(C, DefaultCombo->GetClass(), NULL, DrawComboPosn, GetColor(NAME_Combo), GetColor(NAME_Combo_Fill));
				}
				// draw the combo button overlay
				if ((i < 3 && UCP->ComboLevel == 1) || (i >= 3 && UCP->ComboLevel == 2) || bDebugComboHUD)
				{
					C->Color = cHUDWhite;
					// draw this relative to the combo icon
					C->CurX = DrawComboPosn.X + pos_ComboButtons(i).X + SafeZoneOffsetX;
					C->CurY = DrawComboPosn.Y + pos_ComboButtons(i).Y + SafeZoneOffsetY;
					FLOAT XL = 0, YL = 0;
					switch (i % 3)
					{
						case 0: // left trigger
							C->WrappedPrint((ERenderStyle)C->Style, XL, YL, ButtonFont, 1.f, 1.f, C->bCenter, *XboxButtons(8).ButtonCode);
							break;
						case 1: // A button
							C->WrappedPrint((ERenderStyle)C->Style, XL, YL, ButtonFont, 1.f, 1.f, C->bCenter, TEXT("A"));
							break;
						case 2: // right trigger
							C->WrappedPrint((ERenderStyle)C->Style, XL, YL, ButtonFont, 1.f, 1.f, C->bCenter, *XboxButtons(9).ButtonCode);
							break;
						default:
							debugf(NAME_Warning, TEXT("Invalid item %d on combo list, don't know what button image to draw"), i);
							break;
					}

					// if we're not in splitscreen
					if (!GetLevel() || !GetLevel()->Engine || !GetLevel()->Engine->Client || GetLevel()->Engine->Client->Viewports.Num() < 2)
					{
						// load font if necessary
						if (!SmallFont)
						{
							SmallFont = (UFont*)StaticLoadObject(UFont::StaticClass(), NULL, *SmallFontName, NULL, LOAD_NoWarn, NULL);
							checkSlow(SmallFont);
						}
						// determine positioning for combo name
						switch (i % 3)
						{
							// WrappedPrint() calls here done with STY_None to get the length of the string without drawing anything
							case 0: // right aligned
								C->WrappedPrint(STY_None, XL, YL, SmallFont, 1.f, 1.f, 0, *DefaultCombo->ExecMessage);
								C->CurX = CenterX + pos_Combo(i).X + pos_ComboName(i).X - XL;
								C->CurY = CenterY + pos_Combo(i).Y + pos_ComboName(i).Y;
								break;
							case 1: // centered
								C->WrappedPrint(STY_None, XL, YL, SmallFont, 1.f, 1.f, 0, *DefaultCombo->ExecMessage);
								C->CurX = CenterX + pos_Combo(i).X + pos_ComboName(i).X - XL * 0.5;
								C->CurY = CenterY + pos_Combo(i).Y + pos_ComboName(i).Y;
								break;
							case 2: // left aligned
								C->CurX = CenterX + pos_Combo(i).X + pos_ComboName(i).X;
								C->CurY = CenterY + pos_Combo(i).Y + pos_ComboName(i).Y;
								break;
							default:
								debugf(NAME_Warning, TEXT("Invalid item %d on combo list, don't know how to align combo name text"), i);
								break;
						}
						// draw the combo name
						C->WrappedPrint((ERenderStyle)C->Style, XL, YL, SmallFont, 1.f, 1.f, C->bCenter, *DefaultCombo->ExecMessage);
					}
				}
			}
		}
	}

	unguard;
}

/* epic ===============================================
* ::execDrawComboMenu
* Author: moelfke
*
* Draws only the combo menu, if active
*
* NOTE: DrawVitals() includes this, so don't call both
*
* =====================================================
*/
void AUCHUD::execDrawComboMenu(FFrame& Stack, RESULT_DECL)
{
	guardSlow(AUCHUD::execDrawComboMenu);

	P_GET_OBJECT(UCanvas, C);
	P_GET_OBJECT(AUCPawn, P);
	P_FINISH;

	if (C && P)
	{
		DrawComboMenu(C, P);
	}
	else
	{
		debugf(NAME_Warning, TEXT("Call to DrawComboMenu() with invalid Canvas or UCPawn!"));
	}

	unguardexecSlow;
}

/* epic ===============================================
* ::DrawCompassIcon
* Author: moelfke
*
* Helper for drawing the parts of the compass that are
* actually composed of four pieces rotated and pasted together
*
* =====================================================
*/
FORCEINLINE void AUCHUD::DrawCompassIcon(UCanvas* C, FIconCoords& Coords, FIconPosn& Posn)
{
	guardSlow(AUCHUD::DrawCompassIcon);

	if (Coords.Alpha != 0)
	{
		C->Color.A = Coords.Alpha;
	}

	FLOAT X = Posn.X + SafeZoneOffsetX,
		Y = Posn.Y + SafeZoneOffsetY;

	C->DrawTile(tHUD, X, Y, Posn.W, Posn.H, Coords.U, Coords.V, Coords.UL, Coords.VL, 0.f, C->Color);
	C->DrawTile(tHUD, X + Posn.W * 2.0, Y, -Posn.W, Posn.H, Coords.U, Coords.V, Coords.UL, Coords.VL, 0.f, C->Color);
	C->DrawTile(tHUD, X, Y + Posn.H * 2.0, Posn.W, -Posn.H, Coords.U, Coords.V, Coords.UL, Coords.VL, 0.f, C->Color);
	C->DrawTile(tHUD, X + Posn.W * 2.0, Y + Posn.H * 2.0, -Posn.W, -Posn.H, Coords.U, Coords.V, Coords.UL, Coords.VL, 0.f, C->Color);

	unguardSlow;
}

/* epic ===============================================
* ::DrawCompass
* Author: moelfke
*
* Draws the compass. The native code handles drawing the
* various gametype-inspecific pieces that make up the
* compass. This function should be overridden in script
* to draw arrows pointing at game objectives, etc
*
* =====================================================
*/
void AUCHUD::execDrawCompass(FFrame& Stack, RESULT_DECL)
{
	guard(AUCHUD::execDrawCompass);

	P_GET_OBJECT(UCanvas, C);
	P_GET_OBJECT(AUCPawn, P);
	P_FINISH;

	static FName NAME_CompassGradientFill = FName(TEXT("CompassGradientFill"));
	static FName NAME_CompassRings = FName(TEXT("CompassRings"));

	if (C)
	{
		// draw fill
		C->Color = GetColor(NAME_CompassGradientFill);
		DrawCompassIcon(C, coord_CompassGradientFill, pos_CompassGradientFill);
		// draw rings
		C->Color = GetColor(NAME_CompassRings);
		DrawCompassIcon(C, coord_CompassBorder, pos_CompassBorder);
		DrawCompassIcon(C, coord_CompassInnerRing, pos_CompassInnerRing);
		// draw cross
		C->Color = cHUDWhite;
		DrawCompassIcon(C, coord_CompassLatLong, pos_CompassLatLong);
		// draw center dot
		C->Color = GetColor(NAME_CompassRings);
		DrawCompassIcon(C, coord_CompassCenterDot, pos_CompassCenterDot);
		// draw rotating tick marks
		CompassCardsRot->Rotation.Yaw = PlayerOwner->Rotation.Yaw * 0.5;
		C->DrawTile(CompassCardsRot, pos_CompassCardsRot.X + SafeZoneOffsetX, pos_CompassCardsRot.Y + SafeZoneOffsetY, pos_CompassCardsRot.W, pos_CompassCardsRot.H, 0.f, 0.f, CompassCardsRot->MaterialUSize(), CompassCardsRot->MaterialVSize(), 0.f, cCompassCardsRot);
	}
	else
	{
		debugf(NAME_Warning, TEXT("NULL Canvas passed into AUCHUD::execDrawCompass"));
	}

	unguardexec;
}

void AUCHUD::execDrawFFALeaderboard(FFrame& Stack, RESULT_DECL)
{
	guard(AUCHUD::execDrawFFALeaderboard);

	P_GET_OBJECT(UCanvas, C);
	P_FINISH;

	if (C && PlayerOwner && ScoreBoard && PlayerOwner->GameReplicationInfo)
	{
		// load font if necessary
		if (!SmallFont)
		{
			SmallFont = (UFont*)StaticLoadObject(UFont::StaticClass(), NULL, *SmallFontName, NULL, LOAD_NoWarn, NULL);
			check(SmallFont && SmallFont->IsValid());
		}

		// from 1 to pos_LeaderBoard.Length names displayed
		INT NumPlayers = Max(1, Min(PlayerOwner->GameReplicationInfo->PRIArray.Num(), pos_LeaderBoard.Num()));

		INT BiggerScore = 10;
		UBOOL bNegative = 0;
		for (INT i = 0; i < NumPlayers && i < PlayerOwner->GameReplicationInfo->PRIArray.Num(); i++)
		{
			if (PlayerOwner->GameReplicationInfo->PRIArray(i))
			{
				bNegative = bNegative | (PlayerOwner->GameReplicationInfo->PRIArray(i)->Score < 0.f);
				while (Abs(PlayerOwner->GameReplicationInfo->PRIArray(i)->Score) > BiggerScore)
				{
					BiggerScore *= 10;
				}
			}
		}
		if (bNegative)
		{
			BiggerScore *= -1;
		}
		// BiggerScore is next larger power of ten than the largest score of those being displayed

		// get screen space needed for score
		FLOAT ScoreColumnXL, ScoreColumnYL;
		C->ClippedStrLen(SmallFont, 0.9f, 0.9f, ScoreColumnXL, ScoreColumnYL, *FString::Printf(TEXT(" %i"), BiggerScore));

		// draw the entries
		UBOOL bDrewMe = 0;
		INT PlayersDrawn = 0;
		for (INT i = 0; i < PlayerOwner->GameReplicationInfo->PRIArray.Num() && PlayersDrawn < NumPlayers; i++)
		{
			APlayerReplicationInfo* PRI = PlayerOwner->GameReplicationInfo->PRIArray(i);
			if (PRI && !PRI->bDedicatedServerSpectator)
			{
				// draw leaderboard graphics
				C->Color = cEnemyColor;
				DrawIcon(C, tHUD, coord_LeaderBoardBackground(PlayersDrawn), pos_LeaderBoard(PlayersDrawn));
				C->Color = cLeaderColor;
				DrawIcon(C, tHUD, coord_LeaderBoardOutline(PlayersDrawn), pos_LeaderBoard(PlayersDrawn));

				C->Color = cHUDWhite;
				if (PRI == PlayerOwner->PlayerReplicationInfo)
				{
					bDrewMe = 1;
				}
				else if (PlayersDrawn == NumPlayers - 1 && !bDrewMe && PlayerOwner->PlayerReplicationInfo)
				{
					// haven't drawn self yet, so put you in last spot for pity
					PRI = PlayerOwner->PlayerReplicationInfo;
					bDrewMe = 1;
				}
				else
				{
					C->Color.A = 100;
				}

				// draw score
				FString ScoreString = FString::Printf(TEXT("%i"), INT(PRI->Score));
				FLOAT ScoreXL, ScoreYL;
				C->ClippedStrLen(SmallFont, 0.9f, 0.9f, ScoreXL, ScoreYL, *ScoreString);
				C->CurX = (pos_LeaderBoardName(PlayersDrawn).X - ScoreXL) + SafeZoneOffsetX;
				C->CurY = pos_LeaderBoardName(PlayersDrawn).Y + SafeZoneOffsetY;
				C->WrappedPrint((ERenderStyle)C->Style, ScoreXL, ScoreYL, SmallFont, 0.9f, 0.9f, C->bCenter, *ScoreString);

				// draw name
				FString NameString = PRI->PlayerName;
				FLOAT NameXL, NameYL;
				C->ClippedStrLen(SmallFont, 0.9f, 0.9f, NameXL, NameYL, *NameString);
				FLOAT TextScale = 0.9f;
				FLOAT PosY = pos_LeaderBoardName(PlayersDrawn).Y;
				FLOAT AvailableXL = pos_LeaderBoard(PlayersDrawn).W - ScoreColumnXL - 15.f;
				if (NameXL > AvailableXL)
				{
					// Attempt to scale the letters down to fit into the space
					// if the name is <= 10 characters....any more than that and the
					// scaled characters will be too small
					if (NameString.Len() > 10)
					{
						NameString = NameString.Left(8) + TEXT("...");
						C->ClippedStrLen(SmallFont, 0.9f, 0.9f, NameXL, NameYL, *NameString);
					}

					FLOAT Factor = Min(1.f, AvailableXL / NameXL);
					TextScale *= Factor;
					NameXL *= Factor;
					PosY += (NameYL - NameYL * Factor) * 0.5;
				}
				C->CurX = (pos_LeaderBoardName(PlayersDrawn).X - ScoreColumnXL - NameXL) + SafeZoneOffsetX;
				C->CurY = PosY + SafeZoneOffsetY;
				C->WrappedPrint((ERenderStyle)C->Style, NameXL, NameYL, SmallFont, TextScale, TextScale, C->bCenter, *NameString);

				PlayersDrawn++;
			}
		}
	}

	unguardexec;
}