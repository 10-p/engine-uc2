/*=============================================================================
	UnGUI.cpp: See .UC for for info
	Copyright 1997-2002 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Joe Wilcox
	* Revised by Brian Ladd
	* Revised by Ron Prestenback
=============================================================================*/
#ifdef _XBOX
#include "XboxMovie.h"
#include "D3DDrv.h"
#endif

//jg -- DO NOT BREAK MY PCH SETTINGS!
#include "GUI.h"
#include "GUIPrivate.h"

#include "UnPlayerStats.h"

#ifdef _XBOX
#include "XboxLive.h"
#endif

IMPLEMENT_CLASS(UGUIImage);
IMPLEMENT_CLASS(UGUIPage);
IMPLEMENT_CLASS(UGUIFont);
IMPLEMENT_CLASS(UGUILabel);
IMPLEMENT_CLASS(UGUIButton);
IMPLEMENT_CLASS(UGUICheckBoxButton);
IMPLEMENT_CLASS(UGUIEditBox);
IMPLEMENT_CLASS(UGUIScrollBarBase);
IMPLEMENT_CLASS(UGUIVertScrollZone);
IMPLEMENT_CLASS(UGUIVertScrollButton);
IMPLEMENT_CLASS(UGUIVertGripButton);
IMPLEMENT_CLASS(UGUIVertScrollBar);
IMPLEMENT_CLASS(UGUIPanel);
IMPLEMENT_CLASS(UGUIStyles);
IMPLEMENT_CLASS(UGUIMenuOption);

IMPLEMENT_CLASS(UPropertyManagerBase);
IMPLEMENT_CLASS(UGUIScrollingPanel);
IMPLEMENT_CLASS(UGUIScrollFrame);
IMPLEMENT_CLASS(UProfilePreview);
IMPLEMENT_CLASS(UStatsManager);

// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIPage
// =======================================================================================================================================================
// =======================================================================================================================================================
UBOOL UGUIPage::NativeKeyEvent(BYTE& iKey, BYTE& State, FLOAT Delta )
{
	guard(UGUIPage::NativeKeyEvent);

	if ( INVALIDMENU )
		return false;

	UGUIController* C = Controller;
	if ( Super::NativeKeyEvent(iKey, State, Delta) )
		return true;

	// Handle Close	-- Add support for aborting
	if ( State == IST_Release && iKey == IK_Escape && delegateOnCanClose(this,1) )
		C->eventCloseMenu(1,0);

//debug -->
// do console hotkey detection correctly
	if ( State == IST_Release && C->ViewportOwner && C->ViewportOwner->Console )
	{
		UConsole* Console = Cast<UConsole>(C->ViewportOwner->Console);
		if ( Console && iKey == Console->ConsoleKey )
		{
			State = IST_Press;
			return false;
		}
	}
//debug <--

	return true;

	unguardobj;
}

UBOOL UGUIPage::XControllerEvent(BYTE Id, eXControllerCodes iCode)
{
	guard(GUIPage::XControllerEvent);
//scion bcl fixed GUIPage to respect OnXControllerEvent's that return true
//			(that consume the input)

	if ( INVALIDMENU )
		return false;

	UGUIController* C = Controller;
	if (Super::XControllerEvent(Id,iCode))
		return true;

	BYTE InputMode = delegateGetMenuInputMode();
	if ((iCode==XC_Back || iCode == XC_B))
	{
		if ( InputMode == MIM_Bound || InputMode == MIM_NonBlocking )
		{
			UViewport* Viewport = Cast<UViewport>(C->eventGetViewport(ActiveViewportIndex));
			if ( Viewport && Viewport->GamepadID != Id )
			{
				if ( InputMode == MIM_Bound )
				{
					SHOWCODEHANDLER(this,Id,iCode);
					return true;
				}

				return false;
			}
		}

		C->eventRemoveMenu(this, true, false);
		SHOWCODEHANDLER(this,Id,iCode);
		return true;
	}

	if ( InputMode != MIM_NonBlocking )
	{
		SHOWCODEHANDLER(this,Id,iCode);
		return true;
	}

	return false;

	unguardobj;
}


void UGUIPage::UpdateTimers(float DeltaTime)
{
	guard(UGUIPage::UpdateTimers);

	// We want to ignore any new timer created during this cycle
	INT TimerCnt = Timers.Num();

	for (INT i = 0; i<TimerCnt; i++)
	{
		UGUIComponent *Timer = Timers(i);
		if ( Timer == NULL )
		{
			Timers.Remove(i--);
			continue;
		}

		Timer->TimerCountdown -= DeltaTime;
		Timer->TimerIndex = i;

		if (Timer->TimerCountdown <= 0.0 && Timer->TimerInterval > 0.0)
		{
			Timer->eventTimer();

			if (Timer->bTimerRepeat)
			{
				Timer->TimerCountdown += Timer->TimerInterval;
				// In case TimerInterval is too small
				if (Timer->TimerCountdown <= 0.0)
					Timer->TimerCountdown = Timer->TimerInterval;
			}
			else
			{
				Timer->TimerCountdown = 0.0;
				Timer->TimerInterval = 0.0;
			}
		}
		// Remove any defunct timer
		if (Timer->TimerInterval <= 0.0)
		{
			Timer->TimerIndex = -1;
			Timers.Remove(i);
			i--;
			TimerCnt--;		// Make sure we dont trigger timers created in this timer cycle
		}
	}
	unguardobj;
}

UBOOL UGUIPage::MousePressed(UBOOL IsRepeat)
{
	guard(UGUIPage::MousePressed);

	if ( INVALIDMENU )
		return false;

	UGUIController* C = Controller;

	if ( !IsRepeat && C->ActiveControl == this && PerformHitTest(C->MouseX,C->MouseY) )
	{
		UGUIComponent* Focused = C->eventGetFocusedControl();
		if ( Focused )
			Focused->NativeInvalidate(this);
	}

	if ( Super::MousePressed(IsRepeat) )
		return true;

	return false;

	unguardobj;

}

UBOOL UGUIPage::MouseReleased()
{
	guard(UGUIPage::MouseReleased);

	if ( INVALIDMENU )
		return false;

	UGUIController* C = Controller;
	if ( Super::MouseReleased() )
		return true;

	return ( PerformHitTest(C->MouseX, C->MouseY) );

	unguardobj;
}

void UGUIPage::Draw( UCanvas* Canvas )
{
	guard(UGUIPage::Draw);

	Super::Draw(Canvas);

	if ( Canvas && Controller && eventShouldDrawInviteIcon(Canvas) )
	{
		Canvas->Style = STY_Alpha;
		if ( bPendingFriendInvite && Controller->FriendInviteIcon )
			Canvas->DrawIcon(Controller->FriendInviteIcon, InviteIconX, InviteIconY, InviteIconW, InviteIconH, 100, FColor(255,255,255,255));
		else if ( bPendingGameInvite && Controller->GameInviteIcon )
			Canvas->DrawIcon(Controller->GameInviteIcon, InviteIconX, InviteIconY, InviteIconW, InviteIconH, 100, FColor(255,255,255,255));
	}
	unguardobj;
}


// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIPanel
// =======================================================================================================================================================
// =======================================================================================================================================================
void UGUIPanel::execRender( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIPanel::execRender);

	P_GET_OBJECT(UCanvas,Canvas);
	P_FINISH;

	SaveCanvasState(Canvas);

	SpecialPreDraw(Canvas);

	RestoreCanvasState(Canvas);


	Draw(Canvas);
	RestoreCanvasState(Canvas);
	bRendered = 1;

	unguardexec;
}

void UGUIPanel::SpecialPreDraw( UCanvas* Canvas )
{
	guard(UGUIScrollingPanel::SpecialPreDraw);

	if ( DELEGATE_IS_SET(OnPreRender) && delegateOnPreRender(Canvas, this) )
		return;

	Super::PreDraw(Canvas);
	UpdateBounds();

	// delegateOnPreDraw() should return true if it has modified the dimensions of this component
	// Update bounds again so that this frame's Draw uses the correct bounds
	if ( DELEGATE_IS_SET(OnPreDraw) && delegateOnPreDraw(Canvas) )
		UpdateBounds(1);

	unguard;
}

void UGUIPanel::Draw(UCanvas* Canvas)
{
	guard(UGUPanel::Draw);

	if (!bVisible || INVALIDRENDER )
		return;

	FLOAT X(Bounds[0]), Y(Bounds[1]), W(Bounds[2] - Bounds[0]), HT(Bounds[3] - Bounds[1]);

	// Draw the background
	if (Background!=NULL)
	{
		Canvas->Color = BackgroundColor;
		Canvas->Style = BackgroundRStyle;
		FLOAT mW(Background->MaterialUSize()), mH(Background->MaterialVSize());

		switch ( BackgroundStyle )
		{
		case ISTY_Normal:
			Canvas->DrawTile(Background, X, Y, mW, mH, 0, 0, mW, mH, 0, Canvas->Color.Plane()); 
			break;

		case ISTY_Stretched:
			Canvas->DrawTileStretched(Background, X, Y, W, HT);
			break;

		default:
			Canvas->DrawTileScaleBound(Background, X, Y, W, HT );
            break;
		}
		
	}

	UGUIComponent::Draw(Canvas);

	FLOAT ForcedAlpha, LastForcedAlpha = Canvas->ForcedAlpha;
	if ( ComponentColor.A != 255 )
	{
		ForcedAlpha = FLOAT(ComponentColor.A) / 255.f;
		Canvas->ForcedAlpha *= ForcedAlpha;
	}

	if ( Style )
		Style->Draw(Canvas, MenuState, X, Y, W, HT, NULL);

	DrawControls(Canvas);

	Canvas->ForcedAlpha = LastForcedAlpha;
	
	unguardobj;

}

// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIImage
// =======================================================================================================================================================
// =======================================================================================================================================================
void UGUIImage::PreDraw(UCanvas* Canvas)
{
	guard(UGUIImage::PreDraw);

	Super::PreDraw(Canvas);

	if ( bSkippedDraw )
		bReset = 1;

	bSkippedDraw = 1;

	unguardobj;
}

void UGUIImage::Draw(UCanvas* Canvas)
{
	guard(GUIImage::Draw);

	if ( !bVisible || INVALIDRENDER )
		return;

	Super::Draw(Canvas);
	if ( !Image )
		return;

	if ( bReset && Image->GetClass() == UMovieMaterial::StaticClass() )
		Cast<UMovieMaterial>(Image)->bReset = 1;

	bSkippedDraw = 0;
	bReset = 0;

	DECLAREBOUNDS;

	//scion bcl -- Modified GUIImage style handling (and modified code to look like GUILable code).
	// GUIImage should obey style settings (if there are any).
	if (Style == NULL) 
	{
		// Draw the background starting in the upper left of the image

		Canvas->Style = ImageRenderStyle;
		Canvas->Color = ComponentColor;

		INT mU = Image->MaterialUSize(), mV = Image->MaterialVSize();

		switch (ImageStyle)
		{
		case ISTY_Normal:
			{
				FLOAT X,Y, XL, YL;

				if (U != -1)
					X = U;
				else
					X = 0;

				if (V != -1)
					Y = V;
				else
					Y = 0;

				if (UL == -1)
					XL = Min<FLOAT>(AW, mU);
				else
					XL = UL;

				if (VL == -1)
					YL = Min<FLOAT>(AH, mV);
				else
					YL = VL;

				switch ( ImageAlign )
				{
				case IMGA_Center:
					AL -= (XL / 2);
					AT -= (YL / 2);
					break;
				case IMGA_BottomRight:
					AL = (AL + AW) - XL;
					AT = (AT + AH) - YL;
					break;
				}
				Canvas->DrawTile(Image, AL, AT, XL, YL,X,Y, XL, YL, 0, Canvas->Color.Plane());
				break;
			}

		case ISTY_Stretched:
			{
				FLOAT X,Y, XL, YL;

				if (U != -1)
					X = U;
				else
					X = 0;

				if (V != -1)
					Y = V;
				else
					Y = 0;

				if (UL == -1)
					XL = Min<FLOAT>(ActualWidth(), mU);
				else
					XL = UL;

				if (VL == -1)
					YL = Min<FLOAT>(ActualHeight(), mV);
				else
					YL = VL;

				switch ( ImageAlign )
				{
				case IMGA_Center:
					AL = AL - (XL / 2);
					AT = AT - (YL / 2);
					break;
				case IMGA_BottomRight:
					AL = (AL + AW) - XL;
					AT = (AT + AH) - YL;
					break;
				}

				Canvas->DrawTileStretchedUV(Image, AL, AT, AW, AH, X, Y, XL, YL);
				break;
			}
		case ISTY_Scaled:
		{
			if ( U < 0 && V < 0 && UL < 0 && VL < 0 )
			{
				if ( ImageAlign == IMGA_Center )
				{
					AL -= (AW / 2);
					AT -= (AH / 2);
				}
				Canvas->DrawTileScaleBound(Image, AL, AT, AW, AH );
			}

			else
			{
				FLOAT X,Y, XL, YL;

				if ( U == -1 )
					X = 0;
				else X = U;

				if ( V == -1 )
					Y = 0;
				else Y = V;

				if ( UL == -1 )
					XL = Min<FLOAT>(AW, mU);
				else
					XL = UL;

				if ( VL == -1 )
					YL = Min<FLOAT>(AH, mV);
				else
					YL = VL;

				if ( ImageAlign == IMGA_Center )
				{
					AL -= (AW / 2);
					AT -= (AH / 2);
				}
				Canvas->DrawTile(Image, AL, AT, AW, AH, X, Y, XL, YL, 0, Canvas->Color.Plane());		
			}
			break;
		}

		case ISTY_Bound:
			Canvas->DrawTileBound(Image, AL, AT, AW, AH );
			break;

		case ISTY_Justified:
			Canvas->DrawTileJustified(Image, AL, AT, AW, AH, ImageAlign );
			break;
		}
	}
	else
	{
		UBOOL bUseComponentColor = bOverrideStyle || ColorFrame.Num() > 0;

		// Let's have images obey style settings; note that with a style they don't use the X1, X2, etc.
		Style->DrawImage(Canvas, MenuState, ActualLeft(), ActualTop(), ActualWidth(), ActualHeight(), Image, bUseComponentColor?&ComponentColor:NULL);
		return;
	}

	unguardobj;
}

// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUILabel
// =======================================================================================================================================================
// =======================================================================================================================================================
void UGUILabel::PreDraw(UCanvas* Canvas)
{
	guard(GUILabel::PreDraw);

	if ( INVALIDRENDER )
		return;

	UGUIController* C = Controller;

	Super::PreDraw(Canvas);

	FLOAT AH(ActualHeight()), AW(ActualWidth());

	UFont* SavedFont = Canvas->Font;
	UFont* DrawFont = NULL;

	if ( Style && MenuState >= 0 && MenuState < 5 && Style->Fonts[MenuState] != NULL )
		DrawFont = Style->Fonts[MenuState]->eventGetFont(C->ResX);
	else
	{
		UGUIFont* Font = ActualTextFont ? ActualTextFont : C->eventGetMenuFont(TextFont);
		if ( Font )
			DrawFont = Font->eventGetFont(C->ResX);
	}

	if ( DrawFont )
	{
		Canvas->Font = DrawFont;
		if ( bAutoSizeHorz || bAutoSizeVert )
		{
			FLOAT	NewWidth(0.f), 
					NewHeight(0.f);

			if ( Style && Style->Fonts[MenuState] )
			{
				NewWidth += Style->BorderOffsets[0] + Style->BorderOffsets[2];
				NewHeight += Style->BorderOffsets[1] + Style->BorderOffsets[3];
			}

			FLOAT XL=0, YL=0;

			Canvas->XBoxClippedStrLen( Canvas->Font, ScaleX, ScaleY, XL, YL, *Caption );

			NewWidth += XL;
			NewHeight += YL;

			if ( bAutoSizeHorz )
			{
				FLOAT	AMin = AutoHorzRange.Min != INDEX_NONE ? ActualWidth(AutoHorzRange.Min) : INDEX_NONE,
						AMax = AutoHorzRange.Max != INDEX_NONE ? ActualWidth(AutoHorzRange.Max) : INDEX_NONE;

				NewWidth += ActualWidth(AutoSizePadding[0]) + ActualWidth(AutoSizePadding[2]);

				if ( AMin != INDEX_NONE && (AMax == INDEX_NONE || AMax > AMin) && NewWidth < AMin )
					NewWidth = AMin;

				if ( AMax != INDEX_NONE && (AMin == INDEX_NONE || AMax > AMin) && NewWidth > AMax )
					NewWidth = AMax;

				CaptionLines.Empty();
				if ( Caption.Len() )
				{
					if ( bMultiLine )
						Canvas->WrapStringToArray(*Caption, &CaptionLines, NewWidth, DrawFont, ScaleX, ScaleY);
					else
						new(CaptionLines) FString(*Caption);
				}
			}
			else if ( bMultiLine )
			{
				CaptionLines.Empty();
				if ( Caption.Len() )
				{
					// Convert the caption into an array of strings that can be drawn within out bounds
					// If this isn't a multiline label, just make the caption the only member of the array.
					Canvas->WrapStringToArray(*Caption, &CaptionLines, ClientBounds[2] - ClientBounds[0], DrawFont, ScaleX, '|');
					NewHeight += YL * (CaptionLines.Num() - 1);
				}
			}
			else
			{
				CaptionLines.Empty();
				if ( Caption.Len() )
					new(CaptionLines) FString(*Caption);
			}

			if ( bAutoSizeVert )
			{
				FLOAT	AMin = AutoVertRange.Min != INDEX_NONE ? ActualHeight(AutoVertRange.Min) : INDEX_NONE,
						AMax = AutoVertRange.Max != INDEX_NONE ? ActualHeight(AutoVertRange.Max) : INDEX_NONE;

				NewHeight += ActualHeight(AutoSizePadding[1]) + ActualHeight(AutoSizePadding[3]);

				if ( AMin != INDEX_NONE && (AMax == INDEX_NONE || AMax > AMin) && NewHeight < AMin )
					NewHeight = AMin;

				if ( AMax != INDEX_NONE && (AMin == INDEX_NONE || AMax > AMin) && NewHeight > AMax )
					NewHeight = AMax;
			}

			if ( !bAutoSizeHorz )
				NewWidth = WinWidth;

			if ( !bAutoSizeVert )
				NewHeight = WinHeight;

			if ( ScalingType == SCALE_All || ScalingType == SCALE_X )
				NewWidth = RelativeWidth(NewWidth);
			else NewWidth = ActualWidth(NewWidth);

			if ( ScalingType == SCALE_All || ScalingType == SCALE_Y )
				NewHeight = RelativeHeight(NewHeight);
			else NewHeight = ActualHeight(NewHeight);

			SetDims(NewWidth, NewHeight, WinLeft, WinTop);

		}
		else
		{
			CaptionLines.Empty();
			if ( Caption.Len() )
			{
				if ( bMultiLine )
					Canvas->WrapStringToArray(*Caption, &CaptionLines, ClientBounds[2] - ClientBounds[0], DrawFont, ScaleX, '|');

				else
					new(CaptionLines) FString(*Caption);
			}
		}

		static const FLOAT FloatTolerance = 0.00001f;
		if ( Abs(ActualWidth() - AW) >= FloatTolerance || Abs(ActualHeight() - AH) >= FloatTolerance )
		{
			UpdateBounds();
			eventlayoutChanged(Canvas);
		}
	}

	Canvas->Font = SavedFont;

	unguardobj;
}

void UGUILabel::Draw(UCanvas* Canvas)
{
	guard(UGUILabel::Draw);

	if ( !bVisible || INVALIDRENDER )
		return;

	UGUIController* C = Controller;

	Super::Draw(Canvas);
	FLOAT X(Bounds[0]), Y(Bounds[1]), XL(Bounds[2] - Bounds[0]), YL(Bounds[3] - Bounds[1]);
	if ( !CheckCanvasClip(Canvas,X,Y,XL,YL) )
		return;

	UBOOL bUseComponentColor = bOverrideStyle || ColorFrame.Num() > 0;
	FColor& DrawColor = Style != NULL && !bUseComponentColor ? Style->FontColors[MenuState] : ComponentColor;
	BYTE	DrawStyle = Style != NULL ? Style->RStyles[MenuState] : TextStyle;
	UFont*  DrawFont  = NULL;
	
	if ( Style )
	{
		// Draw the background if the label isn't transparent
		if ( !bTransparent )
			Style->Draw(Canvas, MenuState, X, Y, XL, YL, bUseComponentColor ? &ComponentColor : NULL );

		if ( MenuState >= 0 && MenuState < 5 && Style->Fonts[MenuState] != NULL )
			DrawFont = Style->Fonts[MenuState]->eventGetFont(C->ResX);

	}
	else if ( !bTransparent )
		Canvas->DrawTile(C->DefaultPens[0], X, Y, XL, YL, 0, 0, 32, 32, 0, BackColor.Plane());


	// Determine which font we should be using, if DrawFont hasn't already been set to the style's font
	if ( DrawFont == NULL )
	{
		if ( ActualTextFont == NULL )
			ActualTextFont = C->eventGetMenuFont(TextFont);

		if ( ActualTextFont != NULL )
			DrawFont = ActualTextFont->eventGetFont(C->ResX);
	}

	// If we have no font, we can't draw text
	if ( DrawFont == NULL )
		return;

	// Adjust our parameters for drawing the text
	X = ClientBounds[0], Y = ClientBounds[1];
	XL = ClientBounds[2] - ClientBounds[0];
	YL = ClientBounds[3] - ClientBounds[1];

	Canvas->Color = DrawColor;
	Canvas->Style = DrawStyle;
	Canvas->Font = DrawFont;

	FLOAT CaptionXL, CaptionYL;
	
	// First get the height of the font
	Canvas->ClippedStrLen(DrawFont, ScaleX, ScaleY, CaptionXL, CaptionYL, TEXTSAMPLE);
	for ( INT i = 0; i < CaptionLines.Num(); i++ )
	{
		Canvas->DrawXboxTextJustified(TextAlign, X, Y, X + XL, Y + CaptionYL, ScaleX, ScaleY, bUseComponentColor ? ComponentColor.A : 255, TEXT("%s"), *CaptionLines(i));
		Y += CaptionYL;

		// If drawing the next line would cause the text to overflow our boundries, just stop here.
		if ( appFloor(Y + CaptionYL) > appRound(ClientBounds[3]) )
			break;
	}
	unguardobj;
}


// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIButton
// =======================================================================================================================================================
// =======================================================================================================================================================
void UGUIButton::Draw(UCanvas* Canvas)
{
	guard(GUIButton::Draw);

	if ( !bVisible || Style == NULL || INVALIDRENDER )
		return;

	// Draw the button
	Super::Draw(Canvas);

	UBOOL bUseComponentColor = bOverrideStyle || ColorFrame.Num() > 0;
	Style->Draw(Canvas,MenuState, Bounds[0],Bounds[1],Bounds[2] - Bounds[0],Bounds[3] - Bounds[1], bUseComponentColor ? &ComponentColor : NULL);

	if ( Caption.Len() )
		Style->DrawText(Canvas, MenuState, ClientBounds[0], ClientBounds[1], ClientBounds[2] - ClientBounds[0], ClientBounds[3] - ClientBounds[1], 1, *Caption, bUseComponentColor ? &ComponentColor : NULL );
	
	if ( Graphic == NULL )
		return;

	Canvas->Color = ComponentColor;

	DECLAREBOUNDS;
	if (bClientBound)
	{
		AL = ClientBounds[0];
		AT = ClientBounds[1];
		AW = ClientBounds[2] - ClientBounds[0];
		AH = ClientBounds[3] - ClientBounds[1];
	}

	FLOAT mW = Graphic->MaterialUSize();
	FLOAT mH = Graphic->MaterialVSize();

	// MC: Clip to Bounds/ClientBounds
	FLOAT dW = Min<FLOAT>(AW, mW);
	FLOAT dH = Min<FLOAT>(AH, mH);

	switch ( Position )
	{
	case ICP_Stretched:
		Canvas->DrawTileStretched(Graphic, AL, AT, AW, AH );
		break;

	case ICP_Scaled:
		Canvas->DrawTileScaleBound(Graphic, AL, AT, AW, AH );
		break;

	case ICP_Bound:
		Canvas->DrawTileBound(Graphic, AL, AT, AW, AH ) ;			
		break;

	case ICP_Aligned:
		{
			FLOAT X, Y;
			switch ( Alignment )
			{
			case ICA_UpperRight:
				X = (AL + AW) - mW, Y = AT;
				break;

			case ICA_LowerLeft:
				X = AL, Y = (AT + AH) - mH;
				break;

			case ICA_LowerRight:
				X = (AL + AW) - mW, Y = (AT + AH) - mH;
				break;

			case ICA_Center:
				X = AL + ((AW - dW)/2);
				Y = AT + ((AH - dH)/2);
				break;

			default:
				X = AL, Y = AT;
				break;
			}

			Canvas->DrawTile(Graphic, X, Y, dW, dH, (mW-dW)/2, (mH-dH)/2, dW, dH, 0, Canvas->Color.Plane());
			break;
		}

	default:
		if ( Style )
		{
			UBOOL bUseComponentColor = bOverrideStyle || ColorFrame.Num() > 0;
			Style->DrawImage(Canvas, MenuState, AL, AT, AW, AH, Graphic, bUseComponentColor ? &ComponentColor : NULL );
		}
		else
			Canvas->DrawTile(Graphic, AL, AT, dW, dH, 0, 0, dW, dH, 0, Canvas->Color.Plane());
	}

	unguardobj;
}

// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUICheckBoxButton
// =======================================================================================================================================================
// =======================================================================================================================================================
void UGUICheckBoxButton::Draw(UCanvas* Canvas)
{
	guard(GUIButton::Draw);

	if (!bVisible || Style == NULL || INVALIDRENDER )
		return;

	if ( !bChecked )
	{
		UMaterial* SavedGraphic = Graphic;
		Graphic = NULL;

		Super::Draw(Canvas);
		Graphic = SavedGraphic;
		return;
	}

	Super::Draw(Canvas);

	unguardobj;
}
// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIEditBox
// =======================================================================================================================================================
// =======================================================================================================================================================
void UGUIEditBox::Draw(UCanvas* Canvas)
{
	guard(GUIEditBox::Draw);

	if ( (!bVisible) || (Style==NULL) || INVALIDRENDER )
		return;

	Super::Draw(Canvas);

	FString Storage,FinalDraw;
	Storage = TextStr;

	if ( (bMaskText) && (Storage.Len()>0) )
	{
		for (INT MaskIndex=0;MaskIndex<Storage.Len();MaskIndex++)
			Storage[MaskIndex] = *TEXT("#");
	}

	FLOAT OldClipX = Canvas->ClipX;
	FLOAT OldClipY = Canvas->ClipY;

	Canvas->Color = Style->FontColors[MenuState];

	FLOAT XL,YL,BoxWidth=ClientBounds[2]-ClientBounds[0];
	
	if ( (Storage.Len() != LastLength) || (CaretPos!=LastCaret) )
	{
		// Recaculate FirstVis

		if (CaretPos<=FirstVis)
			FirstVis = Max(0,CaretPos-1);
		else
		{
			FinalDraw = Storage.Mid(FirstVis,CaretPos-FirstVis);
			Style->TextSize(Canvas,MenuState,*FinalDraw,XL,YL);

			while ( (XL>BoxWidth) && (FirstVis<Storage.Len()) )
			{
				FirstVis++;
				FinalDraw = Storage.Mid(FirstVis,CaretPos-FirstVis);
				Style->TextSize(Canvas,MenuState,*FinalDraw,XL,YL);
			}
		}

	}
	LastLength = Storage.Len();

	if (bReadOnly)
			FirstVis = 0;

	FinalDraw = Storage.Mid(FirstVis,Storage.Len()-FirstVis);

	// Display Cursor/select-all block. Behind text.

	if (!bReadOnly && bHasFocus)
	{

		if ( (FirstVis==CaretPos) || (FinalDraw.Len()==0) )
		{
			Canvas->ClippedStrLen( Canvas->Font, 1, 1, XL, YL, TEXTSAMPLE);
			XL = 0;
			bAllSelected=false;
		}
		else
		{
			FString TmpString = FinalDraw.Mid(0,CaretPos-FirstVis);
			Canvas->ClippedStrLen( Canvas->Font, 1, 1, XL, YL, *TmpString );
		}

		FLOAT CursorY = ActualTop() + (ActualHeight()/2) - (YL/2);

		Canvas->Style=5;	// Alpha

		if(bAllSelected)
		{
			Canvas->Color = FColor(255,255,255,0.5f * Controller->CursorFade);
			Canvas->DrawTile(Controller->DefaultPens[0],ClientBounds[0],CursorY,XL,YL,0,0,3,1, 0, Canvas->Color.Plane());
		}
		else
		{
			Canvas->Color = FColor(255,255,255,Controller->CursorFade);
			Canvas->DrawTile(Controller->DefaultPens[0],ClientBounds[0]+XL,CursorY,3,YL,0,0,3,1, 0, Canvas->Color.Plane());
		}
	}


	Canvas->CurX   = ClientBounds[0];
	Canvas->CurY   = Bounds[1];
	Canvas->ClipX  = ClientBounds[2];
	Canvas->ClipY  = Bounds[3];

	Style->DrawText(Canvas, MenuState, ClientBounds[0], ClientBounds[1], ClientBounds[2]-ClientBounds[0], ClientBounds[3] - ClientBounds[1], 0, *FinalDraw);

	Canvas->ClipX = OldClipX;
	Canvas->ClipY = OldClipY;

	unguardobj;
}


// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIMenuOption
// =======================================================================================================================================================
// =======================================================================================================================================================
void UGUIMenuOption::PreDrawControls(UCanvas* Canvas)
{
	guard(UGUIMenuOption::PreDrawControls);

	if ( MyLabel == NULL || MyComponent == NULL || INVALIDRENDER )
		return;

	DECLAREBOUNDS;

	UGUIComponent* Comp = NULL;
	for ( INT i = 0; i < Controls.Num(); i++ )
	{
		Comp = Controls(i);
		if ( Comp != MyLabel && Comp != MyComponent )
			PreDrawControl(Canvas,Comp);
	}

	if ( bVerticalLayout )
	{
		if (CaptionWidth <= 1.f)
			MyLabel->WinHeight = Max<FLOAT>(AH * CaptionWidth,0.f);
		else
			MyLabel->WinHeight = CaptionWidth;

		MyLabel->WinLeft = AL;

		if ( !MyLabel->bAutoSizeHorz )
			MyLabel->WinWidth = AW;

		MyComponent->WinLeft = MyLabel->WinLeft;

		// Figure out MyComponent new Height
		if (!bSquare)
		{
			MyComponent->WinWidth = Max<FLOAT>(AW,0.f);
			if (ComponentWidth == -1.f)
				MyComponent->WinHeight = Max<FLOAT>(AH - MyLabel->ActualHeight(),0.f);
			else if ( ComponentWidth > 0 )
				MyComponent->WinHeight = Max<FLOAT>(AH * ComponentWidth,0.f);
			else ComponentWidth = 0;
		}
		else
		{
			if (ComponentWidth == -1.f)
				MyComponent->WinHeight = Max<FLOAT>(AH - MyLabel->ActualHeight(),0.f);
			else MyComponent->WinHeight = Max<FLOAT>(ComponentWidth >= 0 && ComponentWidth <= 1 ? AH * ComponentWidth : ComponentWidth,0.f);
			MyComponent->WinWidth = MyComponent->WinHeight;
		}

		if (bFlipped)
		{
			// Label will be on the beneath the component
			switch ( ComponentJustification )
			{
			case TXTA_Left:
				MyComponent->WinTop = AT;
				break;

			case TXTA_Center:
				MyComponent->WinTop = ((((AT + AH) - MyLabel->WinHeight) - AT) / 2) - MyComponent->WinHeight / 2;
				break;

			case TXTA_Right:
				MyComponent->WinTop = ((AT + AH) - MyLabel->WinHeight) - MyComponent->WinHeight;
				break;
			}

			switch ( VertLabelJustification )
			{
			case TXTA_Left:
                MyLabel->WinTop = Max<FLOAT>(MyComponent->WinTop + MyComponent->WinHeight,0.f);
				break;
				
			case TXTA_Center:
				{
                    FLOAT X = MyComponent->WinTop + MyComponent->WinHeight;
					FLOAT XL = (AT + AH) - X;
                    MyLabel->WinTop = Max<FLOAT>(X + ((XL / 2) - (MyLabel->WinHeight / 2)),0.f);
					break;
				}

			case TXTA_Right:
				MyLabel->WinTop = (AT + AH) - MyLabel->WinHeight;
				break;
			}
		}
		else
		{
			switch ( VertLabelJustification )
			{
			case TXTA_Left:
				MyLabel->WinTop = AT;
				break;

			case TXTA_Center:
				{
					FLOAT X2 = (AT + AH) - MyComponent->WinHeight;
					FLOAT XL = X2 - AT;

					MyLabel->WinTop = AT + (XL / 2 - MyLabel->WinHeight / 2);
				}
				break;

			case TXTA_Right:
				MyLabel->WinTop = ((AT + AH) - MyComponent->WinHeight) - MyLabel->WinHeight;
				break;
			}

			switch ( ComponentJustification )
			{
			case TXTA_Left:
                MyComponent->WinTop = Max<FLOAT>(MyLabel->WinTop + MyLabel->WinHeight,0.f);
				break;

			case TXTA_Center:
				{
					FLOAT X = MyLabel->WinTop + MyLabel->WinHeight;
					FLOAT XL = (AT + AH) - X;
					MyComponent->WinTop = Max<FLOAT>(X + ((XL / 2) - (MyComponent->WinHeight / 2)),0.f);
					break;
				}

			case TXTA_Right:
				MyComponent->WinTop = (AT + AH) - MyComponent->WinHeight;
				break;
			}
		}
	}
	else
	{
		if ( CaptionWidth <= 1.f )
			MyLabel->WinWidth = Max<FLOAT>(AW * CaptionWidth,0.f);
		else
			MyLabel->WinWidth = CaptionWidth;
	
		MyLabel->WinHeight = AH;
		MyLabel->WinTop = AT;

		MyComponent->WinHeight = AH;
		FLOAT CompWidth;
		if (ComponentWidth == -1.f)
			CompWidth = Max<FLOAT>(AW - MyLabel->ActualWidth(), 0.f);
		else if (ComponentWidth > 0)
			CompWidth = Max<FLOAT>(AW * ComponentWidth, 0.f);
		else CompWidth = 0.f;
	
		if ( bSquare )
			MyComponent->WinWidth = Max<FLOAT>(MyComponent->WinHeight,0.f);
		else MyComponent->WinWidth = CompWidth;
	
		MyComponent->WinTop = AT;
		// Figure out the Lefts.
	
		if (bFlipped)
		{
			if ( bSquare )
                MyLabel->WinLeft = Max<FLOAT>(AL + CompWidth,0.f);
			else MyLabel->WinLeft = Max<FLOAT>( (AL + AW) - MyLabel->WinWidth, 0.f );
			switch ( ComponentJustification )
			{
			case TXTA_Left:
				MyComponent->WinLeft = AL;
				break;

			case TXTA_Center:
				{
					FLOAT XL = MyLabel->WinLeft - AL;
					MyComponent->WinLeft = AL + ( XL / 2 - MyComponent->WinWidth / 2);
					break;
				}
			
			case TXTA_Right:
				MyComponent->WinLeft = MyLabel->WinLeft - MyComponent->WinWidth;
			}
	
		}
		else
		{
			MyLabel->WinLeft = AL;
			switch ( ComponentJustification )
			{
			case TXTA_Left:
				MyComponent->WinLeft = AL + MyLabel->WinWidth;
				break;

			case TXTA_Center:
				{
					FLOAT X = AL + MyLabel->WinWidth;
					FLOAT XL = (AL + AW) - X;
					MyComponent->WinLeft = Max<FLOAT>(X + ((XL / 2) - (MyComponent->WinWidth / 2)), X);
					break;
				}

			case TXTA_Right:
				MyComponent->WinLeft = (AL + AW) - MyComponent->WinWidth;
				break;
			}
		}
	}

	PreDrawControl(Canvas, MyLabel);
	PreDrawControl(Canvas, MyComponent);

	unguardobj;
}

void UGUIMenuOption::execPreDraw( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIMenuOption::execPreDraw);

	P_GET_OBJECT(UCanvas,Canvas);
	P_FINISH;

	if ( INVALIDRENDER )
		return;

	SaveCanvasState(Canvas);
	PreDraw(Canvas);
	UpdateBounds();
	RestoreCanvasState(Canvas);

	unguardexec;
}

void UGUIMenuOption::execDraw( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIMenuOption::execDraw);

	P_GET_OBJECT(UCanvas,Canvas);
	P_FINISH;

	if ( INVALIDRENDER || !bVisible )
		return;

	SaveCanvasState(Canvas);
	Draw(Canvas);
	RestoreCanvasState(Canvas);

	unguardexec;
}

void UGUIMenuOption::Draw(UCanvas* Canvas)
{
	guard(UGUIMenuOption::Draw);

	UGUIComponent::Draw(Canvas);

	if ( Style != NULL )
	{
		UBOOL bUseComponentColor = bOverrideStyle || ColorFrame.Num() > 0;
		Style->Draw( 
			Canvas, MenuState,
			Bounds[0], Bounds[1],
			Bounds[2] - Bounds[0], 
			Bounds[3] - Bounds[1], 
			bUseComponentColor 
				? &ComponentColor
				: NULL 
			);
	}

	FLOAT ForcedAlpha, LastForcedAlpha = Canvas->ForcedAlpha;
	if ( ComponentColor.A != 255 )
	{
		ForcedAlpha = FLOAT(ComponentColor.A) / 255.f;
		Canvas->ForcedAlpha *= ForcedAlpha;
	}

	DrawControls(Canvas);
	Canvas->ForcedAlpha = LastForcedAlpha;

	unguard;
}

// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIVertScrollZone
// =======================================================================================================================================================
// =======================================================================================================================================================
void UGUIVertScrollZone::Draw(UCanvas* Canvas)
{
	guard(UGUIVertScrollZone::Draw);

	if ( !bVisible || Style == NULL || ActualWidth() <= 1 || ActualHeight() <= 1 || INVALIDRENDER )
		return;

	Super::Draw(Canvas);
	Style->Draw(Canvas, MenuState, Bounds[0], Bounds[1], Bounds[2] - Bounds[0], Bounds[3] - Bounds[1]);

	unguardobj;
}

// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIVertScrollBar
// =======================================================================================================================================================
// =======================================================================================================================================================
void UGUIVertScrollBar::PreDrawControls( UCanvas* Canvas )
{
	guard(UGUIVertScrollBar::PreDrawControls);

	if ( !bVisible || !MyList || MyList->Elements.Num() < 1 || INVALIDRENDER )
		return;
	
	DECLAREBOUNDS;
	FLOAT ZoneSize(0.f), ZeroPos(0.f), ButtonHeight(AW);
	
	if ( MyUpButton )
	{
		if ( MyUpButton->Graphic )
			ButtonHeight = Max<FLOAT>(ButtonHeight,MyUpButton->Graphic->MaterialVSize());

		MyUpButton->SetDims(AW, ButtonHeight, AL, AT);
	}

	if ( MyDownButton )
	{
		if ( MyDownButton->Graphic )
			ButtonHeight = Max<FLOAT>(ButtonHeight, MyDownButton->Graphic->MaterialVSize());

		MyDownButton->SetDims(AW, ButtonHeight, AL, AT + AH - ButtonHeight);
	}

	if ( MyScrollZone )
	{
		MyScrollZone->SetDims( AW, AH - (2 * ButtonHeight), AL, AT + ButtonHeight );
		ZoneSize = MyScrollZone->ActualHeight();
		ZeroPos = MyScrollZone->ActualTop();
	}

	for ( INT i = 0; i < Controls.Num(); i++ )
	{
		// Call predraw on everything except the grip button here
		if ( Controls(i) != MyGripButton )
			PreDrawControl(Canvas,Controls(i));
	}

	if ( MyList )
		GripHeight = ZoneSize * ((FLOAT)MyList->ItemsPerPage / (FLOAT)MyList->Elements.Num());

	// Calculate the grip
	if ( GripHeight < 6 )
		GripHeight = 6;

	ZoneSize -= GripHeight;
	FLOAT NewGripPos = ZeroPos + ZoneSize * GripTop;
	
	if ( MyGripButton )
	{
		MyGripButton->SetDims(AW, GripHeight, AL, NewGripPos);
		PreDrawControl(Canvas,MyGripButton);
	}

	unguardobj;
}


// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIScrollingPanel
// =======================================================================================================================================================
// =======================================================================================================================================================
void UGUIScrollingPanel::Draw( UCanvas* Canvas )
{
	guard(UGUIScrollingPanel::Draw);

	if ( Canvas == NULL || Controller == NULL )
		return;

	INT OldClipX = Canvas->ClipX,
		OldClipY = Canvas->ClipY,
		OldOrgX = Canvas->OrgX,
		OldOrgY = Canvas->OrgY;

	Canvas->OrgX = Bounds[0];
	Canvas->OrgY = Bounds[1];
	Canvas->ClipX = Bounds[2];
	Canvas->ClipY = Bounds[3];

	// scion sz - set the region of the canvas where we want to allow drawing to show 
	Canvas->SetCanvasMask( Bounds[0], Bounds[1], Bounds[2], Bounds[3] );

	if ( !DELEGATE_IS_SET(OnDraw) || !delegateOnDraw(Canvas) )
		Super::Draw(Canvas);

	if ( DELEGATE_IS_SET(OnRender) )
		delegateOnRender(Canvas);

	// scion sz - reset masked canvas region
	Canvas->ClearCanvasMask();

	Canvas->ClipX = OldClipX;
	Canvas->ClipY = OldClipY;
	Canvas->OrgX = OldOrgX;
	Canvas->OrgY = OldOrgY;

	unguardobj;
}

void UGUIScrollFrame::DrawControls(UCanvas* Canvas)
{
	guard(UGUIScrollFrame::DrawControl);

	if ( !Canvas )
		return;

	if ( MenuOwner == NULL )
	{
		Super::DrawControls(Canvas);
		return;
	}

	TArray<INT> FocusedIndex;
	for ( INT i = 0; i < Controls.Num(); i++ )
	{
		UGUIComponent* Comp = Controls(i);
		if ( Comp == NULL )
			continue;

		// don't draw this component if the panel's bottom bound is higher than our top bound
		if ( Comp->Bounds[3] < MenuOwner->Bounds[1] )
			continue;

		// if the panel's top bound is lower than our bottom bound, stop here
		if ( Comp->Bounds[1] > MenuOwner->Bounds[3] )
			break;

		if ( bPopFocusedControl && eventIsFocused(Comp) )
			FocusedIndex.AddItem(i);
		else
			DrawControl(Canvas, Comp);
	}

	for ( INT i = 0; i < FocusedIndex.Num(); i++ )
	{
		if ( Controls.IsValidIndex(FocusedIndex(i)) )
			DrawControl(Canvas, Controls(FocusedIndex(i)));
	}

	unguardobj;
}

// =======================================================================================================================================================
// =======================================================================================================================================================
// UProfilePreview
// =======================================================================================================================================================
// =======================================================================================================================================================
void UProfilePreview::execLoadProfileValues( FFrame& Stack, RESULT_DECL )
{
	guard(UProfilePreview::execLoadProfileValues);

	P_GET_STR(ProfileName);
	P_FINISH;

	check(thePlayerController == NULL);

	if (Stats == NULL)
	{
		Stats = Cast<UPlayerStats>(StaticConstructObject(UPlayerStats::StaticClass()));
		Stats->Initialize();
	}
	else
	{
		Stats->ResetValues();
	}

	UBOOL& BoolResult = *(UBOOL*)Result = 0;
	if ( LoadProfile(ProfileName) )
	{
		eventSetProfileName(ProfileName);
		BoolResult = 1;
	}

	unguardexec;
}