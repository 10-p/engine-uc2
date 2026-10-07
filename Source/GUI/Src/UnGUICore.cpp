/*=============================================================================
	UnGUICore.cpp: See .UC for for info
	Copyright 1997-2004 Epic Games, Inc. All Rights Reserved.

	Contains the non composite controls

Revision history:
	* Created by Michel Comeau
	* Revised by Brian Ladd
	* Revised by Ron Prestenback
=============================================================================*/

//jg -- DO NOT BREAK MY PCH SETTINGS!
#include "GUI.h"
#include "GUIPrivate.h"

#undef MM_MAX

#include "UCGameplay.h"
#include "ULoadSaveManager.h"
#include "FContentManager.h"

const FLOAT BIGFLOAT = 99999.f;

void GUIappSprintf( INT MaxLen, TCHAR* Dest, const TCHAR* Fmt, ... )
{
	GET_VARARGS( Dest, MaxLen, Fmt, Fmt );
}

IMPLEMENT_CLASS(UGUI);
IMPLEMENT_CLASS(UGUIController);
IMPLEMENT_CLASS(UGUIComponent);
IMPLEMENT_CLASS(UGUIMultiComponent);

static INT Compare( FString& A, FString& B )
{
	return appStricmp(*A, *B);
}

// Identical to UObject::IsValid(), but this version fails quietly
UBOOL IsValidObject( UObject* Obj )
{
	guard(UGUIComponent::IsValidObject);

	if( !Obj || Obj->GetIndex() == INDEX_NONE )
		return 0;

	UObject* IndexedObject = UObject::GetIndexedObject(Obj->GetIndex());
	if ( IndexedObject == NULL || IndexedObject != Obj )
		return 0;

	return 1;
	unguard;
}

TCHAR* GetKeyStateName( BYTE State )
{
	switch(State)
	{
	case IST_Press:   return TEXT("IST_Press");
	case IST_Hold:    return TEXT("IST_Hold");
	case IST_Release: return TEXT("IST_Release");
	case IST_Repeat:  return TEXT("IST_Repeat");
	}

	return TEXT("");
}

TCHAR* GetXCodeName( BYTE XCode )
{
	switch ( XCode )
	{
		case XC_A:				return TEXT("XC_A");
		case XC_B:				return TEXT("XC_B");
		case XC_X:				return TEXT("XC_X");
		case XC_Y:				return TEXT("XC_Y");
		case XC_Black:			return TEXT("XC_Black");
		case XC_White:			return TEXT("XC_White");
		case XC_LeftTrigger:	return TEXT("XC_LeftTrigger");
		case XC_RightTrigger:	return TEXT("XC_RightTrigger");
		case XC_PadUp:			return TEXT("XC_PadUp");
		case XC_PadDown:		return TEXT("XC_PadDown");
		case XC_PadLeft:		return TEXT("XC_PadLeft");
		case XC_PadRight:		return TEXT("XC_PadRight");
		case XC_Start:			return TEXT("XC_Start");
		case XC_Back: 			return TEXT("XC_Back");
		case XC_LeftThumb:		return TEXT("XC_LeftThumb");
		case XC_RightThumb:		return TEXT("XC_RightThumb");
		case XC_Up:				return TEXT("XC_Up");
		case XC_Down:			return TEXT("XC_Down");
		case XC_Right:			return TEXT("XC_Right");
		case XC_Left:			return TEXT("XC_Left");
		case XC_AltUp:			return TEXT("XC_AltUp");
		case XC_AltDown:		return TEXT("XC_AltDown");
		case XC_AltLeft:		return TEXT("XC_AltLeft");
		case XC_AltRight:		return TEXT("XC_AltRight");
	}

	return TEXT("");
}

UBOOL CheckCanvasClip( UCanvas* C, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL )
{
	guardSlow(CheckCanvasClip);

	if ( C == NULL )
		return false;

	if ( X > C->ClipX || X + XL < C->OrgX )
		return false;

	if ( Y > C->ClipY || Y + YL < C->OrgY )
		return false;

	return true;

	unguardSlow;
}


// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIController
// =======================================================================================================================================================
// =======================================================================================================================================================

void UGUIController::RenderDesignMode( UCanvas* Canvas )
{
	guard(UGUIController::RenderDesignMode);

	checkSlow(Canvas);

	UViewport* Viewport = Master->Client->Viewports(0);

	Canvas->CurX = 0;
	Canvas->CurY = 0;

	FPlane Empty(0.f, 0.f, 0.f, 0.f);
	UFont* OldFont = Canvas->Font;
	FColor OldColor = Canvas->Color, Black(0,0,0,255);
	BYTE OldStyle = Canvas->Style;

	UGUIFont* MyFont = eventGetMenuFont(TEXT("Sui12pt"));
	if ( MyFont == NULL )
		return;

	UFont* CFont = MyFont->eventGetFont(640);
	if (CFont==NULL)
		return;

	Canvas->Font = CFont;

	TCHAR Position[64];

	// Display help
	if ( KeyDown(Viewport,IK_F1) )
	{
		FLOAT X = 0, Y = 0, XL = 0, YL = 0;
		const TCHAR* Hint, *Desc;

		Canvas->ClippedStrLen(Canvas->Font, 1.0, 1.0, XL, YL, TEXT("Qq"));
		for ( INT i = 0; i < DesignModeHints.Num(); i++ )
		{
			Hint = *DesignModeHints(i).Key, Desc = *DesignModeHints(i).Description;
			Canvas->DrawTile(DefaultPens[0], X, Y, ResX, YL, 0, 0, 32, 32, 0.f, Black);
			Canvas->DrawTextJustified( TXTA_Left, X, Y, ResX, Y + YL, 1.f, 1.f, TEXT("%s%s"), Hint, Desc );
			Y += YL;
		}
	}
	else
	{
		// Draw state
		FLOAT XL = 0, YL = 0;
		appSprintf( Position, TEXT("E:%i  I:%i  H:%i  P:%i"), DesignerVisible(), bInteractiveMode, bHighlightCurrent, bDrawFullPaths );

		Canvas->ClippedStrLen(Canvas->Font, 1.0, 1.0, XL, YL, Position);
		Canvas->DrawTile(DefaultPens[0], 0, 0, XL, YL, 0, 0, 32, 32, 0.f, Black);
		Canvas->DrawTextJustified( TXTA_Left, 0, 0, XL, YL, 1.f, 1.f, TEXT("%s"), Position);
	}

	// Draw the mouse cursor position in the upper right corner of the screen
	FLOAT X = ResX, Y=0, XL = 0, YL = 0;
	appSprintf( Position, TEXT("X:%.2f Y:%.2f"), MouseX, MouseY );

	Canvas->ClippedStrLen( Canvas->Font, 1.0, 1.0, XL, YL, Position );
	Canvas->DrawTile(DefaultPens[0], X - XL, Y, XL, YL, 0, 0, 32, 32, 0.0f, Black);
	Canvas->DrawTextJustified( 2, 0, Y, X, YL, 1.f, 1.f, TEXT("%s"), Position );


	if ( bHighlightCurrent )
	{
		TCHAR MenuPath[2048];
		TArray<FString> Lines;
		INT i = 0;

		// Set the starting position
		Y = Canvas->ClipY;

		// Don't display this data if shift is pressed
		if ( !ShiftPressed )
		{
			Y -= YL;
			Canvas->DrawTile(DefaultPens[0], 0,Y,Canvas->SizeX, YL,0,0,32,32, 0.0f, Black);

			UGUIComponent* Focused = eventGetFocusedControl();
			if (Focused)
			{
				GUIappSprintf( ARRAY_COUNT(MenuPath), MenuPath, TEXT("Focused - MS:%i CM:%i TO:%i %s"),Focused->MenuState,Focused->bCaptureMouse, Focused->bTabStop ? Focused->TabOrder : -1, bDrawFullPaths ? Focused->GetMenuPath() : Focused->GetName() );

				Lines.Empty();
				Canvas->WrapStringToArray( MenuPath, &Lines, Canvas->SizeX );

				for ( i = Lines.Num() - 1; i > 0; i-- )
				{
					Canvas->DrawTextJustified(0, 0, Y, Canvas->SizeX, Y + YL, 1.f, 1.f, TEXT("»» %s"), *Lines(i));

					Y -= YL;
					Canvas->DrawTile(DefaultPens[0], 0,Y,Canvas->SizeX, YL,0,0,32,32, 0.0f, Black);
				}
				Canvas->DrawTextJustified(0,0,Y,Canvas->SizeX, Y + YL, 1.f, 1.f, *Lines(i));
			}
			else
				Canvas->DrawTextJustified(0,0,Y,Canvas->SizeX,Y + YL, 1.f, 1.f, TEXT("Focused - None"));

			Y -= YL;
			Canvas->DrawTile(DefaultPens[0], 0,Y,Canvas->SizeX, YL,0,0,32,32, 0.0f, Black);

			// Draw the data for the ActiveControl & FocusedControl
			if (ActiveControl!=NULL)
			{
				GUIappSprintf(ARRAY_COUNT(MenuPath), MenuPath, TEXT("Active - MS:%i CM:%i TO:%i (%.2f,%.2f,%.2f,%.2f) %s"),ActiveControl->MenuState,ActiveControl->bCaptureMouse,ActiveControl->bTabStop ? ActiveControl->TabOrder : -1,ActiveControl->ActualLeft(),ActiveControl->ActualTop(),ActiveControl->ActualWidth(),ActiveControl->ActualHeight(), bDrawFullPaths ? ActiveControl->GetMenuPath() : ActiveControl->GetName());

				Lines.Empty();
				Canvas->WrapStringToArray( MenuPath, &Lines, Canvas->SizeX );

				for ( i = Lines.Num() - 1; i > 0; i-- )
				{
					Canvas->DrawTextJustified(0, 0, Y, Canvas->SizeX, Y + YL, 1.f, 1.f, TEXT("»» %s"), *Lines(i));
					Y -= YL;
					Canvas->DrawTile(DefaultPens[0], 0,Y,Canvas->SizeX, YL,0,0,32,32, 0.0f, Black);
				}
				Canvas->DrawTextJustified(0,0,Y,Canvas->SizeX, Y + YL, 1.f, 1.f, *Lines(i));
			}
			else
				Canvas->DrawTextJustified(0,0,Y,Canvas->SizeX,Y + YL, 1.f, 1.f, TEXT("Active - None"));

			Y -= YL;
			Canvas->DrawTile(DefaultPens[0], 0,Y,Canvas->SizeX, YL,0,0,32,32, 0.0f, Black);

			// Draw the MoveControl data
			if (MoveControl)
			{
				GUIappSprintf(ARRAY_COUNT(MenuPath), MenuPath, TEXT("%s MS:%i (%.2f %.2f %.2f %.2f) :Move"),
					bDrawFullPaths ? MoveControl->GetMenuPath(MenuPath) : MoveControl->GetName(),
					MoveControl->MenuState,
					bDrawActualValues ? MoveControl->ActualLeft() : MoveControl->WinLeft,
					bDrawActualValues ? MoveControl->ActualTop() : MoveControl->WinTop,
					bDrawActualValues ? MoveControl->ActualWidth() : MoveControl->WinWidth,
					bDrawActualValues ? MoveControl->ActualHeight() : MoveControl->WinHeight);

				Lines.Empty();
				Canvas->WrapStringToArray( MenuPath, &Lines, ResX );
				for ( i = Lines.Num() - 1; i > 0; i-- )
				{
					Canvas->DrawTextJustified(2, 0, Y, Canvas->SizeX, Y + YL, 1.f, 1.f, TEXT("%s ««"), *Lines(i));
					Y -= YL;
					Canvas->DrawTile(DefaultPens[0], 0,Y,Canvas->SizeX, YL,0,0,32,32, 0.0f, Black);
				}
				Canvas->DrawTextJustified(2,0,Y,Canvas->SizeX, Y + YL, 1.f, 1.f, *Lines(i));

/*				Y -= YL;
				Canvas->DrawTile(DefaultPens[0], 0,Y,Canvas->SizeX,YL,0,0,32,32, 0.0f, Black,Empty);
				Canvas->DrawTextJustified(2,0,Y,Canvas->SizeX,Y+YL,TEXT("%s %s %s MS:%i (%.2f %.2f %.2f %.2f) :Move"),
					bDrawFullPaths ? MoveControl->GetMenuPath(MenuPath) : MoveControl->GetName(),
					MoveControl->Style ? MoveControl->Style->GetName() : TEXT("None"),
					MoveControl->Style && MoveControl->Style->Fonts[MoveControl->MenuState]
						? *((MoveControl->Style->Fonts[MoveControl->MenuState])->KeyName) : TEXT("None"),
					MoveControl->MenuState,
					MoveControl->ActualLeft(),MoveControl->ActualTop(),MoveControl->ActualWidth(), MoveControl->ActualHeight());
*/
				Canvas->Color = FColor(255,0,0,255);

				// Draw the bounding rectangle
				Canvas->DrawTileStretched(WhiteBorder, MoveControl->Bounds[0], MoveControl->Bounds[1],
								MoveControl->Bounds[2]-MoveControl->Bounds[0], MoveControl->Bounds[3]-MoveControl->Bounds[1]);

				Canvas->Color = OldColor;
				Canvas->Style = OldStyle;
			}

			if ( TopControl )
			{
				// Draw the bounding rectangle around the top control
				Canvas->Color = FColor(0,0,255,255);

				Canvas->DrawTileStretched( WhiteBorder, TopControl->Bounds[0], TopControl->Bounds[1],
								TopControl->Bounds[2] - TopControl->Bounds[0], TopControl->Bounds[3] - TopControl->Bounds[1]);

				Canvas->Color = OldColor;
			}
		}

		// Draw the focus chain
		if (ActivePage!=NULL && CtrlPressed && AltPressed)
		{
			FLOAT X=0;
			Y=0;

			UGUIComponent* C = ActivePage;

			while ( C )
			{
				Canvas->DrawTile(DefaultPens[0], 0,Y,Canvas->SizeX,Y+YL,0,0,32,32, 0.0f, Black);
				Canvas->DrawTextJustified(0,X,Y,Canvas->SizeX,Y+YL, 1.f, 1.f,TEXT("%s TabOrder:%i State:%i Vis:%i Style:%s"),bDrawFullPaths ? C->GetMenuPath(MenuPath) : C->GetName(),C->bTabStop?C->TabOrder:-1, C->MenuState,C->bVisible,C->Style?C->Style->GetName():TEXT("None"));

				C = C->eventGetFocusedControl();
				X += 5;
				Y += YL;
			}
		}
	}

	else if ( !ShiftPressed )
	{
		if ( MoveControl )
		{
			Canvas->Color = FColor(255,0,0,255);

				// Draw the bounding rectangle
			Canvas->DrawTileStretched(WhiteBorder, MoveControl->Bounds[0], MoveControl->Bounds[1],
					MoveControl->Bounds[2]-MoveControl->Bounds[0], MoveControl->Bounds[3]-MoveControl->Bounds[1]);
		}

		if ( TopControl )
		{
			// Draw the bounding rectangle around the top control
			Canvas->Color = FColor(0,0,255,255);

			Canvas->DrawTileStretched( WhiteBorder, TopControl->Bounds[0], TopControl->Bounds[1],
							TopControl->Bounds[2] - TopControl->Bounds[0], TopControl->Bounds[3] - TopControl->Bounds[1]);
		}
	}

	Canvas->Font = OldFont;
	Canvas->Color = OldColor;
	Canvas->Style = OldStyle;

	unguard;
}

UBOOL UGUIController::DesignKeyEvent( BYTE iKey, BYTE State, FLOAT delta )
{
	guard(UGUIController::DesignKeyEvent);

	if ( !bModAuthor )
		return false;

	if ( iKey == IK_D && State == IST_Release && CtrlPressed && AltPressed )
	{
		bDesignMode = !bDesignMode;
		SaveConfig();

		if ( bDesignMode && ActivePage )
			SetMoveControl(ActivePage);
		else SetMoveControl(NULL);

		ResetInput();
		return true;
	}

	if ( iKey == IK_E && State == IST_Release && CtrlPressed && AltPressed )
	{
		if ( DesignerVisible() )
			ShowProperties(NULL);
		else ShowProperties(ActivePage);

		ResetInput();
		return true;
	}

	if ( !bDesignMode && !DesignerVisible() )
		return false;


	// Events that should happen in interactive mode - can be overridden by holding Ctrl
	if ( bInteractiveMode || CtrlPressed )
	{
		// Events that should happen on key release
		if ( State == IST_Release )
		{
			switch ( iKey )
			{
			case IK_T:	// Set a new topcontrol
				if ( TopControl )
				{
					// If we have a top control, but it isn't the current move control (or the MoveControl is NULL),
					// make it the move control
					if ( TopControl != MoveControl )
						SetMoveControl(TopControl);

					else TopControl = NULL;
				}

				else if ( MoveControl )
					TopControl = MoveControl;

				return true;

			case IK_H:
				bHighlightCurrent = !bHighlightCurrent;
				SaveConfig();
				return true;

			case IK_I:
				bInteractiveMode = !bInteractiveMode;
				return true;

			case IK_Tab:
				if ( AltPressed )
					return true;

				{
					UGUIComponent* OldMoveCtrl = MoveControl, *Top = TopControl ? TopControl : ActivePage;
					if ( !Top->SpecialHit(1) || MoveControl == OldMoveCtrl )
						SetMoveControl(NULL);
				}

				return true;

			case IK_P:
				bDrawFullPaths = !bDrawFullPaths;
				SaveConfig();
				return true;

			case IK_F:
				bDrawActualValues = !bDrawActualValues;
				SaveConfig();
				return true;

			case IK_LeftMouse:
				if ( CtrlPressed || (bInteractiveMode && !bIgnoreNextRelease) )
				{
					if ( MoveControl && !MoveControl->WithinBounds(MouseX,MouseY) )
						SetMoveControl(ActiveControl);
					else
					{
						UGUIComponent* OldMoveCtrl = MoveControl;
						if ( !ActivePage->SpecialHit(0) || MoveControl == OldMoveCtrl )
							SetMoveControl(NULL);
					}
				}

				bIgnoreNextRelease = 0;
				return true;

			case IK_U: // update the current component's values
				if ( DesignerVisible() && MoveControl )
				{
					ShowProperties(MoveControl);
					return true;
				}

			case IK_R:
				ResetInput();
				return true;

			// Insert additional handlers here
			}
		}

		// Events that happen on key press
		else if ( State == IST_Press )
		{
			if ( iKey == IK_LeftMouse )
			{
				// Make sure the mouse is within bounds
				if ( !ValidMousePos() )
				{
					// Out of bounds
					ResetInput();
					return true;
				}
			}

			if ( MoveControl )
			{
				FLOAT scale = AltPressed ? 5.0 : 1.0;

				// Handle positioning
				switch ( iKey )
				{
				case IK_Left:           eventMoveFocused(MoveControl, -1,0,0,0,ResX,ResY, scale);	return true;
				case IK_Right:          eventMoveFocused(MoveControl, 1,0,0,0,ResX,ResY, scale);	return true;
				case IK_Up:             eventMoveFocused(MoveControl, 0,-1,0,0,ResX,ResY, scale);	return true;
				case IK_Down:           eventMoveFocused(MoveControl, 0,1,0,0,ResX,ResY, scale);	return true;
				case IK_GreyPlus:       eventMoveFocused(MoveControl, 0,0,1,0,ResX,ResY, scale);	return true;
				case IK_GreyMinus:      eventMoveFocused(MoveControl, 0,0,-1,0,ResX,ResY, scale);	return true;
				case IK_Equals:         eventMoveFocused(MoveControl, 0,0,0,1,ResX,ResY, scale);	return true;
				case IK_Minus:          eventMoveFocused(MoveControl, 0,0,0,-1,ResX,ResY, scale);	return true;
				case IK_MouseWheelUp:   SetMoveControl(MoveControl == ActivePage ? ActiveControl : MoveControl->MenuOwner);     return true;
				case IK_MouseWheelDown: SetMoveControl(MoveControl->eventGetFocusedControl() ? MoveControl->eventGetFocusedControl() : eventGetFocusedControl()); return true;
				case IK_C:
				{
					FLOAT AL = MoveControl->ActualLeft(), AT = MoveControl->ActualTop(), AW = MoveControl->ActualWidth(), AH = MoveControl->ActualHeight();
					if ( AltPressed )
					{
						// convert abs value to scaled value
//						MoveControl->ScalingType = SCALE_All;
						MoveControl->BoundingType = SCALE_All;
					}

					if ( ShiftPressed )
					{
						MoveControl->bConvertBoundX = false;
						MoveControl->bConvertBoundY = false;
					}

					MoveControl->WinHeight = MoveControl->RelativeHeight( AH );
					MoveControl->WinWidth = MoveControl->RelativeWidth( AW );
					MoveControl->WinLeft = MoveControl->RelativeLeft( AL );
					MoveControl->WinTop = MoveControl->RelativeTop( AT );

					appClipboardCopy( *(FString::Printf(TEXT("\t\tWinWidth=%f%s\t\tWinHeight=%f%s\t\tWinLeft=%f%s\t\tWinTop=%f%s"), MoveControl->WinWidth, LINE_TERMINATOR, MoveControl->WinHeight, LINE_TERMINATOR, MoveControl->WinLeft, LINE_TERMINATOR, MoveControl->WinTop, LINE_TERMINATOR)));
					return true;
				}

				case IK_X:	// Export all properties
				{
					FLOAT AL = MoveControl->ActualLeft(), AT = MoveControl->ActualTop(), AW = MoveControl->ActualWidth(), AH = MoveControl->ActualHeight();
					if ( AltPressed )
					{
						// convert abs value to scaled value
//						MoveControl->ScalingType = SCALE_All;
						MoveControl->BoundingType = SCALE_All;
					}

					if ( ShiftPressed )
					{
						MoveControl->bConvertBoundX = false;
						MoveControl->bConvertBoundY = false;
					}

					MoveControl->WinHeight = MoveControl->RelativeHeight( AH );
					MoveControl->WinWidth = MoveControl->RelativeWidth( AW );
					MoveControl->WinLeft = MoveControl->RelativeLeft( AL );
					MoveControl->WinTop = MoveControl->RelativeTop( AT );

					FStringOutputDevice Ar;

					for ( FObjectIterator It; It; ++It )
						It->ClearFlags(RF_TagImp|RF_TagExp);

					MoveControl->SetFlags( RF_TagImp );

					// Mark all inner properties of this class as already exported so that they aren't exported
					for ( TFieldIterator<UProperty,CLASS_IsAUProperty> It(MoveControl->GetClass()); It; ++It )
					{
						if ( It->Port() )
						{
							if ( Cast<UObjectProperty>(*It,CLASS_IsAUObjectProperty) )
							{
								for ( INT i = 0; i < It->ArrayDim; i++ )
								{
									UObject* Obj = *(UObject**)((BYTE*)MoveControl + It->Offset + i * It->ElementSize);
									if ( Obj )
										Obj->SetFlags(RF_TagImp);
								}
							}

							else
							{
								UArrayProperty* ArrayProp = Cast<UArrayProperty>(*It);
								if ( ArrayProp && Cast<UObjectProperty>(ArrayProp->Inner,CLASS_IsAUObjectProperty) )
								{
									for ( INT i = 0; i < ArrayProp->ArrayDim; i++ )
									{
										FArray* Array = (FArray*)((BYTE*)MoveControl + ArrayProp->Offset + i * ArrayProp->ElementSize);

										for ( INT j = 0; j < Array->Num(); j++ )
										{
											UObject* Obj = ((UObject**)Array->GetData())[j];
											if ( Obj )
												Obj->SetFlags(RF_TagImp);
										}
									}
								}
							}
						}
					}
					UObject::ExportProperties(Ar, MoveControl->GetClass(), (BYTE*)MoveControl, 4, MoveControl->GetClass(), (BYTE*)MoveControl->GetClass()->GetDefaultObject() );
					appClipboardCopy( *Ar );
					return true;
				}

				case IK_L:
					debugf(TEXT(" *** "));
					MoveControl->ShowLinks();
					debugf(TEXT(" *** "));
					return true;
				}

			}
		}

	// Handle moving and resizing of controls
		else if ( State == IST_Axis && MoveControl )
		{
			bForceMouseCheck = false;
			FLOAT dX = delta, dY = delta;

			FLOAT Scale = ViewportOwner->bShowWindowsMouse ? 1.f : MenuMouseSens;
			UViewport* Viewport = Cast<UViewport>(ViewportOwner);

			if ( iKey == IK_MouseX )
			{
				if ( (AltPressed && !bInteractiveMode) || (bInteractiveMode && KeyDown(Viewport,IK_LeftMouse)) )
				{
					// move component
					MouseX += (dX * MenuMouseSens);
					eventMoveFocused(MoveControl, 1, 0, 0, 0, ResX, ResY, dX * Scale);
					bIgnoreNextRelease = 1;
					return true;
				}

				else if ( bInteractiveMode && KeyDown(Viewport,IK_RightMouse) )
				{
					// resize component
					MouseX += (dX * MenuMouseSens);
					eventMoveFocused(MoveControl, 0, 0, 1, 0, ResX, ResY, dX * Scale);
					return true;
				}
			}
			else if ( iKey == IK_MouseY )
			{
				if ( (AltPressed && !bInteractiveMode) || (bInteractiveMode && KeyDown(Viewport,IK_LeftMouse)) )
				{
					// move component
					MouseY -= (dY * MenuMouseSens);
					eventMoveFocused(MoveControl, 0, -1, 0, 0, ResX, ResY, dY * Scale);
					bIgnoreNextRelease = 1;
					return true;
				}

				else if ( bInteractiveMode && KeyDown(Viewport,IK_RightMouse) )
				{
					// resize component
					MouseY -= (dY * MenuMouseSens);
					eventMoveFocused(MoveControl, 0, 0, 0, -1, ResX, ResY, dY * Scale);
					return true;
				}
			}
		}

		// Events that should only happen in interactive mode
		if ( bInteractiveMode )
		{
		}

		// These are the keys which should be swallowed entirely while in design mode
		if ( iKey == IK_LeftMouse || iKey == IK_RightMouse || iKey == IK_Tab || iKey == IK_Left || iKey == IK_Right ||
			iKey == IK_Up || iKey == IK_Down || iKey == IK_MouseWheelDown || iKey == IK_MouseWheelUp )
			return true;
	}

	return false;
	unguard;
}

UBOOL UGUIController::ValidMousePos()
{
	guard(UGUIController::ValidMousePos);

	INT ptX, ptY;
	GetCursorPos( ptX, ptY );
	// D-2: the cursor may be anywhere in the viewport, also outside the UI's box (design pixels below 0 or
	// beyond ResX/ResY there)
	FLOAT X1 = 0.f, Y1 = 0.f, X2 = ResX, Y2 = ResY;
	UViewport* Viewport = Cast<UViewport>(ViewportOwner);
	if ( Viewport && Viewport->Canvas && Viewport->Canvas->IsUIBoxed() )
		Viewport->Canvas->GetTargetInDesign( X1, Y1, X2, Y2 );
	if ( ptX > X2 || ptY > Y2 || ptX < appFloor(X1) || ptY < appFloor(Y1) )
	{
		// Out of bounds
		return 0;
	}

	return 1;

	unguard;
}

void UGUIController::GetCursorPos( INT& ptX, INT& ptY )
{
	guard(UGUIController::GetCursorPosition);

	UViewport* Viewport = Cast<UViewport>(ViewportOwner);
	if ( Viewport )
	{
		if ( Viewport->IsFullscreen() )
		{
			ptX = MouseX, ptY = MouseY;
			return;
		}

		Viewport->UpdateMousePosition();
		// D-2: window pixels -> the canvas's (design pixels when the UI is boxed)
		FLOAT X = Viewport->WindowsMouseX, Y = Viewport->WindowsMouseY;
		if ( Viewport->Canvas )
			Viewport->Canvas->TargetToDesign( X, Y );
		ptX = appFloor( X );
		ptY = appFloor( Y );
	}
	unguard;
}

void UGUIController::ResetInput()
{
	guard(UGUIController::ResetInput);

	CtrlPressed = 0;
	AltPressed = 0;
	ShiftPressed = 0;

	bIgnoreNextRelease = 0;
	bIgnoreUntilPress = 0;
	bForceMouseCheck = 0;

	unguard;
}

void  UGUIController::NativeMessage(const FString Msg, FLOAT MsgLife)
{
}

// NativeTick is responsible for faking mouse button repeats and setting the fade value for
// the text cursor.

void  UGUIController::NativeTick(FLOAT DeltaTime)
{
	guard(UGUIController::NativeTick);

	ClearCloseQueue();
	if ( DeleteStack.Num() )
		eventClearDeleteStack();

	if ( (ViewportOwner==NULL) || (ViewportOwner->Actor==NULL) || (ViewportOwner->Actor->Level==NULL) )
		return;

	// Use real seconds for TimeBased
	DeltaTime /= ViewportOwner->Actor->Level->TimeDilation;

	// Force RenderDelta to maintain 10 fps (simulated)
	RenderDelta = Min(DeltaTime, 0.1f);

	// Reopen any menus that want to be reopened.
	// this should only be true during the first or second tick
	// after a level change.
	if ( TravelMenus.Num() > 0 )
	{
		eventRestoreTravelMenus();
	}

	if ( !bActive )
		return;

	if ( bAttractEnabled )
		IdleSeconds += RenderDelta;

	UGUIPage* TestPage = ActivePage;
	INT LastPage = 0;

	INT compnum = MenuStack.Num() - 1;
	for (INT i = compnum; i >= 0; i--)
	{
		TestPage = MenuStack(i);
		if ( TestPage )
		{
			if ( !TestPage->bRenderWorld )
			{
				LastPage = i;
				break;
			}
		}
	}

	CursorFade+= 255 * (DeltaTime) * CursorStep;
	if (CursorFade<=0.0)
	{
		CursorFade=0;
		CursorStep = 1;
	}
	else if (CursorFade>=255.0)
	{
		CursorFade=255;
		CursorStep = -1;
	}

	FastCursorFade+= 8192 * (DeltaTime) * FastCursorStep;
	if (FastCursorFade<=0.0)
	{
		FastCursorFade=0;
		FastCursorStep = 1;
	}
	else if (FastCursorFade>=255.0)
	{
		FastCursorFade=255;
		FastCursorStep = -1;
	}

	//scion bcl -- from 2K4 - update timers on all active pages
	for ( INT i = MenuStack.Num() - 1; i >= 0; i-- )
	{
		TestPage = MenuStack(i);
		if ( TestPage )
		{
			TestPage->UpdateTimers(DeltaTime);

			
			/* rjp --
			Update any gamepad ID's that are out of sync.  This normally happens when the menus
			are reopened after playing a match.  The player's associated viewport hasn't received
			a gamepad ID by the time the menus are opened, so the menu assigns its ControllerMask
			using the invalid GamepadID of the player's viewport.
			*/
				
			// a ControllerMask of 0 would disallow any input
			if ( TestPage->ControllerMask == 0 )
			{
				UViewport* Viewport = Cast<UViewport>(eventGetViewport(TestPage->ActiveViewportIndex));
				if ( Viewport && Viewport->GamepadID != UViewport::INVALID_GAMEPAD_ID )
					TestPage->eventAssignControllerMask();
			}
		}
	}

#ifdef _XBOX
	if ( IdleSeconds > MaxIdleSeconds && !eventShowingTeaser() )
	{
		eventBeginTeaser();
		IdleSeconds = 0;
	}
#endif

#if 0

	UViewport* Viewport = Master->Client->Viewports(0);
	if ( KeyDown(Viewport,XBC_Start) && KeyDown(Viewport,XBC_Back) )
	{
		RestartSeconds += RenderDelta;
		if ( RestartSeconds > 2.f )
			Viewport->Exec(TEXT("RESTARTTITLE"), *GLog);
	}
	else
		RestartSeconds = 0;
#endif

	unguard;
}

/*-----------------------------------------------------------------------------
	M10: keyboard and mouse as a pad in the menus.

	The menus act on pad codes (XControllerEvent). Epic's PC build turned some keys into
	those codes with a hard-wired table (letters A B X Y K W, Enter = Start, Escape = Back),
	which collided with WASD and with typing. The table is now ini data, section
	[GUI.EmulatedJoypad] of the system ini:
	    <engine key>=<pad code>[/<pad code while a text field is on the page>]
	pad codes: A B X Y Black White LeftTrigger RightTrigger PadUp PadDown PadLeft PadRight
	Start Back LeftThumb RightThumb, AltUp AltDown AltLeft AltRight (right stick), Type (the
	key types or edits text), Raw (not converted). Without the section, Epic's table applies.
	docs/porting/analysis/controls.md has the defaults and the reasons.
-----------------------------------------------------------------------------*/

enum EEmulatedPadSpecial
{
	EJP_Raw		= 0,	// the key reaches the menus as itself
	EJP_Type	= 1,	// text entry: characters arrive through NativeKeyType, editing keys edit
	EJP_AltUp	= 2,
	EJP_AltDown	= 3,
	EJP_AltLeft	= 4,
	EJP_AltRight= 5,
	EJP_Invalid	= 255,
};

struct FEmulatedPadKey
{
	BYTE Code;		// XBC_A..XBC_RightThumb or EJP_*
	BYTE TextCode;	// the same while the active page has a text field
};

static FEmulatedPadKey	GEmulatedPad[IK_MAX];
static UBOOL			GEmulatedPadLoaded = 0;

static BYTE ParseEmulatedPadCode( const TCHAR* Name )
{
	static const struct { const TCHAR* Name; BYTE Code; } Codes[] =
	{
		{ TEXT("A"), XBC_A }, { TEXT("B"), XBC_B }, { TEXT("X"), XBC_X }, { TEXT("Y"), XBC_Y },
		{ TEXT("Black"), XBC_Black }, { TEXT("White"), XBC_White },
		{ TEXT("LeftTrigger"), XBC_LeftTrigger }, { TEXT("RightTrigger"), XBC_RightTrigger },
		{ TEXT("PadUp"), XBC_PadUp }, { TEXT("PadDown"), XBC_PadDown }, { TEXT("PadLeft"), XBC_PadLeft }, { TEXT("PadRight"), XBC_PadRight },
		{ TEXT("Start"), XBC_Start }, { TEXT("Back"), XBC_Back },
		{ TEXT("LeftThumb"), XBC_LeftThumb }, { TEXT("RightThumb"), XBC_RightThumb },
		{ TEXT("AltUp"), EJP_AltUp }, { TEXT("AltDown"), EJP_AltDown }, { TEXT("AltLeft"), EJP_AltLeft }, { TEXT("AltRight"), EJP_AltRight },
		{ TEXT("Type"), EJP_Type }, { TEXT("Raw"), EJP_Raw },
	};
	for( INT i=0; i<ARRAY_COUNT(Codes); i++ )
		if( appStricmp(Name, Codes[i].Name)==0 )
			return Codes[i].Code;
	return EJP_Invalid;
}

// An engine key by its EInputKey name without "IK_" ("Enter", "W", "LeftMouse"...).
static UBOOL FindEmulatedPadKey( const TCHAR* Name, BYTE& Key )
{
	UEnum* InputKeys = FindObject<UEnum>( ANY_PACKAGE, TEXT("EInputKey") );
	if( !InputKeys )
		return 0;
	for( INT i=0; i<InputKeys->Names.Num() && i<IK_MAX; i++ )
	{
		const TCHAR* KeyName = *InputKeys->Names(i);
		if( appStrnicmp(KeyName, TEXT("IK_"), 3)==0 && appStricmp(KeyName+3, Name)==0 )
		{
			Key = (BYTE)i;
			return 1;
		}
	}
	return 0;
}

static void LoadEmulatedPad()
{
	guard(LoadEmulatedPad);
	GEmulatedPadLoaded = 1;
	appMemzero( GEmulatedPad, sizeof(GEmulatedPad) );

	TMultiMap<FName,FString>* Section = GConfig->GetSectionPrivate( TEXT("GUI.EmulatedJoypad"), 0, 1 );
	if( Section )
	{
		TMultiMap<FName,FString>::TIterator First(*Section);
		if( !First )
			Section = NULL;
	}
	if( !Section )
	{
		// Epic's PC table (UnGUICore.cpp before M10).
		static const BYTE Epic[][2] =
		{
			{ IK_Up, XBC_PadUp }, { IK_Down, XBC_PadDown }, { IK_Left, XBC_PadLeft }, { IK_Right, XBC_PadRight },
			{ IK_A, XBC_A }, { IK_B, XBC_B }, { IK_X, XBC_X }, { IK_Y, XBC_Y }, { IK_K, XBC_Black }, { IK_W, XBC_White },
			{ IK_LeftBracket, XBC_LeftTrigger }, { IK_RightBracket, XBC_RightTrigger },
			{ IK_Semicolon, XBC_LeftThumb }, { IK_SingleQuote, XBC_RightThumb }, { IK_Enter, XBC_Start }, { IK_Escape, XBC_Back },
			{ IK_NumPad8, EJP_AltUp }, { IK_NumPad2, EJP_AltDown }, { IK_NumPad4, EJP_AltLeft }, { IK_NumPad6, EJP_AltRight },
		};
		for( INT i=0; i<ARRAY_COUNT(Epic); i++ )
			GEmulatedPad[Epic[i][0]].Code = GEmulatedPad[Epic[i][0]].TextCode = Epic[i][1];
		debugf( NAME_Init, TEXT("GUI: no [GUI.EmulatedJoypad] section, using Epic's key table") );
		return;
	}

	INT Count = 0;
	for( TMultiMap<FName,FString>::TIterator It(*Section); It; ++It )
	{
		BYTE Key = 0;
		FString Value = It.Value(), Normal, Text;
		if( !Value.Split( TEXT("/"), &Normal, &Text ) )
			Normal = Text = Value;
		BYTE Code = ParseEmulatedPadCode( *Normal ), TextCode = ParseEmulatedPadCode( *Text );
		if( !FindEmulatedPadKey( *It.Key(), Key ) || Code==EJP_Invalid || TextCode==EJP_Invalid )
		{
			debugf( NAME_Warning, TEXT("[GUI.EmulatedJoypad] %s=%s: unknown key or pad code, ignored"), *It.Key(), *Value );
			continue;
		}
		GEmulatedPad[Key].Code     = Code;
		GEmulatedPad[Key].TextCode = TextCode;
		Count++;
	}
	debugf( NAME_Init, TEXT("GUI: %i keys from [GUI.EmulatedJoypad]"), Count );
	unguard;
}

//
// The text field of the active page, if it has one that can be typed into: either the focused
// control (Epic's edit boxes) or the field of an on-screen keyboard page (UC2KeyboardPanel's
// ed_Text, which never has the focus: its key buttons do).
//
static UGUIEditBox* FindEditBox( UGUIComponent* C, INT Depth )
{
	if( !C || Depth > 32 || !C->bVisible || C->MenuState==MSAT_Disabled )
		return NULL;
	UGUIEditBox* Edit = Cast<UGUIEditBox>( C );
	if( Edit )
		return Edit->bReadOnly ? NULL : Edit;
	UGUIMultiComponent* Multi = Cast<UGUIMultiComponent>( C );
	if( Multi )
		for( INT i=0; i<Multi->Controls.Num(); i++ )
			if( (Edit = FindEditBox( Multi->Controls(i), Depth+1 )) != NULL )
				return Edit;
	return NULL;
}

static UGUIEditBox* TextEntryTarget( UGUIController* C )
{
	return C->ActivePage ? FindEditBox( C->ActivePage, 0 ) : NULL;
}

static UBOOL IsTypingKey( BYTE iKey )
{
	return iKey==IK_Space || iKey==IK_Backspace || iKey==IK_Delete
		|| (iKey>=IK_0 && iKey<=IK_9) || (iKey>=IK_A && iKey<=IK_Z)
		|| (iKey>=IK_NumPad0 && iKey<=IK_GreySlash)
		|| (iKey>=IK_Semicolon && iKey<=IK_Tilde) || (iKey>=IK_LeftBracket && iKey<=IK_SingleQuote);
}

static void MouseClickPress( UGUIController* C );
static void MouseClickRelease( UGUIController* C );

static BYTE EmulatedPadCode( BYTE iKey, UBOOL TextEntry )
{
	if( !GEmulatedPadLoaded )
		LoadEmulatedPad();
	return TextEntry ? GEmulatedPad[iKey].TextCode : GEmulatedPad[iKey].Code;
}

/*
	NativeKeyEvent returns true when it has processed the keystroke (which it does
	any time that it is active).

	input:
	BYTE & iKey - the keycode of the key pressed
 */

BYTE UGUIController::GetXCode( BYTE iKey )
{
#ifndef _XBOX
	if ( bEmulatedJoypad )
	{
		BYTE Code = EmulatedPadCode( iKey, TextEntryTarget(this)!=NULL );
		if ( Code >= XBC_A && Code <= XBC_RightThumb )
			return Code;
	}
#endif

	return iKey;
}

// -- NOTE: The UGUIController always swallows input when it's active;
UBOOL UGUIController::NativeKeyEvent(BYTE& iKey, BYTE& State, FLOAT Delta )
{

	guard(UGUIController::NativeKeyEvent);

	UBOOL bValidDelta = fabs(Delta) >= JoyDeadZone;

	if (Master==NULL || Master->Client==NULL || Master->Client->Viewports.Num()==0 || Master->Client->Viewports(0)==NULL)
		return false;

	if ( ViewportOwner && ViewportOwner->Console && ViewportOwner->Console->bVisible )
		return false;

#if defined(_DEBUG) && defined(_XBOX)
	if ( State != IST_Axis || bValidDelta )
		debugf(NAME_DevUI, TEXT("KeyEvent  Key:%s  State:%s  RepeatKey:%u  ControllerMask:%u  Engine->ActiveControllerId:%u"), Cast<UViewport>(ViewportOwner)->Input->GetKeyName(EInputKey(iKey)), GetKeyStateName(State), RepeatKey[ControllerId], ControllerMask, Master->Client->Engine->ActiveControllerId);
#endif

	// Controller insert/removal notification
	if ( iKey == XBC_CtrlRemove || iKey == XBC_CtrlInsert )
	{
		if ( State == IST_Release )
			ResetInput();

		return false;
	}

	// Filter out unwanted controllers
//	if ( State != IST_Axis )
//		debugf(TEXT("ControllerMask:%u   Engine->ActiveControllerId:%u"), ControllerMask, Master->Client->Engine->ActiveControllerId);

#ifdef _XBOX
	// Reset IdleSeconds if we receive any key-input
	if ( State != IST_Axis || bValidDelta )
	{
		if ( eventShowingTeaser() )
		{
			if ( IdleSeconds > 0.5f && State == IST_Release )
			{
				eventEndTeaser();
				IdleSeconds = 0;
			}

			if ( DeleteStack.Num() )
				eventClearDeleteStack();

			return true;
		}

		IdleSeconds = 0;
	}
#endif

	BYTE NewControllerId = Master->Client->Engine->ActiveControllerId;

	if ( !(ControllerMask & (1 << NewControllerId)) )
		return true;

	// scion sz -
	// only process input for a single child viewport if one of them is currently set to active
	if( ControllerMask == 255 && ActiveChildViewport && 
		ActiveChildViewport->Actor && 
		ActiveChildViewport->Actor->GamepadID != NewControllerId )
	{
		// swallow input from all other viewports if we have an ActiveChildViewport
		// (generally, when we're showing the menu during a match)
		return true;
	}

	if ( ActivePage && ActivePage->ControllerMask != 255 &&
		((ActivePage->ControllerMask & (1<<NewControllerId)) == 0) && 
		ActivePage->delegateGetMenuInputMode() == MIM_Bound )
		return true;

	INT ViewportIndex = 
		// this is necessary to be able to use design mode on the start menu on xbox 
#ifndef FINAL_RELEASE
		bDesignMode ? 0 : 
#endif
		FindViewportIndex(NewControllerId);

	if ( ViewportIndex == INDEX_NONE )
		// here is where we process input from unbound gamepads, if we need to
		return true;

	UViewport* Viewport = Master->Client->Viewports(ViewportIndex);

	// set currently active controller
	ControllerId = NewControllerId;

	// update the viewport's Input KeyDown table.
	if ( Viewport->Input )
		Viewport->Input->PreProcess(EInputKey(GetXCode(iKey)), EInputAction(State), Delta);

	// =============================================================================================
	// Pass the raw input to the Controllers NeedRawKeyPress delegate (for KeyConfigs)
	// =============================================================================================
	if ( DELEGATE_IS_SET(OnNeedRawKeyPress) )
	{
		if (State==IST_Release)
			delegateOnNeedRawKeyPress(iKey);

		if ( DeleteStack.Num() )
			eventClearDeleteStack();

		return true;
	}

	if (iKey==IK_Enter && AltPressed)
	{
		ResetInput();
		return true;
	}

	// =============================================================================================
	// Track SHIFT/ALT/CTRL state for easy look up
	// =============================================================================================
	ShiftPressed = KeyDown(Viewport,IK_Shift) || KeyDown(Viewport,IK_LShift) || KeyDown(Viewport,IK_RShift);
	AltPressed = KeyDown(Viewport,IK_Alt) || KeyDown(Viewport,IK_UnknownA4) || KeyDown(Viewport,IK_UnknownA5);
	CtrlPressed = KeyDown(Viewport,IK_Ctrl) || KeyDown(Viewport,IK_LControl) || KeyDown(Viewport,IK_RControl);

	// =============================================================================================
	// DESIGN MODE
	// =============================================================================================
	if ( bModAuthor && DesignKeyEvent(iKey, State, Delta) )
		return true;

	// =============================================================================================
	// Support XBox Controller Emulation via keys for development on the PC
	// =============================================================================================

#ifndef _XBOX
	if ( bEmulatedJoypad )
	{
		// M10: [GUI.EmulatedJoypad] (see LoadEmulatedPad).
		UGUIEditBox* TextBox = TextEntryTarget( this );
		BYTE Code = EmulatedPadCode( iKey, TextBox!=NULL );
		if ( TextBox && Code == EJP_Raw && IsTypingKey(iKey) )
			Code = EJP_Type;	// a key that makes a character types it while there is a text field
		if ( Code >= XBC_A && Code <= XBC_RightThumb )
			iKey = Code;
		else if ( Code >= EJP_AltUp && Code <= EJP_AltRight )
		{
			// Right thumbstick, as Epic's NumPad 8/2/4/6.
			Delta = (Code==EJP_AltUp || Code==EJP_AltRight) ? 1.f : -1.f;
			bValidDelta = State == IST_Press;
			iKey = (Code==EJP_AltUp || Code==EJP_AltDown) ? XBC_RightStickY : XBC_RightStickX;
		}
		else if ( Code == EJP_Type && TextBox && TextBox != eventGetFocusedControl() )
		{
			// An on-screen keyboard page: the field is not the focused control (its key buttons
			// are), so editing keys go to it directly; characters come through NativeKeyType.
			// Nothing else on the page sees a typing key.
			if ( iKey==IK_Backspace || iKey==IK_Delete || iKey==IK_Home || iKey==IK_End )
			{
				BYTE K = iKey, S = State;
				TextBox->NativeKeyEvent( K, S, Delta );
			}
			if ( DeleteStack.Num() )
				eventClearDeleteStack();
			return true;
		}
	}
#endif

	// =============================================================================================
	// Pass the raw input to the Focused Control
	// =============================================================================================
	UGUIComponent* Focused = eventGetFocusedControl();
	if ( Focused!=NULL && Focused->RawXController(ControllerId,iKey,State,Delta) )
	{
		if ( DeleteStack.Num() )
			eventClearDeleteStack();

		return true;
	}

	// =============================================================================================
	// Track the Mouse
	// =============================================================================================

	if ( (iKey==IK_MouseX) || (iKey==IK_MouseY) || bForceMouseCheck )
	{
		if ( !ValidMousePos() )
			return true;

		bForceMouseCheck = false;

		FLOAT dX=0.0f;
		FLOAT dY=0.0f;

		// D-2: the deltas are window pixels; MouseX/MouseY are the canvas's (design pixels when the UI is in a
		// box, ui-scale.md), so the software cursor keeps the speed it has on the window
		UViewport* MouseViewport = Cast<UViewport>(ViewportOwner);
		FLOAT MouseScale = (MouseViewport && MouseViewport->Canvas) ? MouseViewport->Canvas->GetUIScale() : 1.f;

		if (iKey==IK_MouseX)
		{
			dX = Delta;
			MouseX+= (dX*MenuMouseSens) / MouseScale;
		}
		else if (iKey==IK_MouseY)
		{
			dY = Delta;
			MouseY-= (dY * MenuMouseSens) / MouseScale;
		}

		// As long as we are not capturing the mouse, make sure the Active Control is up to date

		LookUnderCursor(dX,dY);
		if ( iKey == IK_MouseX || iKey == IK_MouseY )
			return true;
	}

	//=============================================================================================
	// Handle key repeat
	//=============================================================================================

	// =============================================================================================
	// Process XBox Controller joystick events.  At this point, if we are in mouse emulation mode,
	// the right thumb stick has already been converted in to mouse events.  To make life easier
	// we allow only 1 joystick input passed to the menu at a time.  All other joystick input
	// is ignored until it's cleared
	// =============================================================================================

	if (State == IST_Axis)
	{
		if ( !bValidDelta )	// Deadzone is set in the ini (default = +/- 0.3) so if in the zone, it's released
		{
			// Only signal a release if this is the last key pressed
			if (RepeatKey[ControllerId] == iKey)
			{
				State = IST_Release;
				RepeatKey[ControllerId] = 0;
			}
			else
				// otherwise, ignore it
				return true;
		}

		// If the new key is different, see if the repeat key is an axis.  If it is, ditch it
		else
		{
			if ( RepeatKey[ControllerId] == iKey )
			{
				State = IST_Hold;
			}

			else
			{
				if ( (RepeatKey[ControllerId] == XBC_RightStickX || RepeatKey[ControllerId] == XBC_RightStickY) &&
					(iKey == XBC_LeftStickX || iKey == XBC_LeftStickY) )
					return true;

				if ( (RepeatKey[ControllerId] == XBC_LeftStickX || RepeatKey[ControllerId] == XBC_LeftStickY) &&
					(iKey == XBC_RightStickX || iKey == XBC_RightStickY) )
					return true;

				State = IST_Press;
				RepeatKey[ControllerId] = iKey;
			}
		}
	}

	DOUBLE CurrentTime = appSeconds();
	if ( State == IST_Hold )
	{
		// check if we are ready to allow a key repeat for this gamepad
		if ( CurrentTime < RepeatTime[ControllerId] )
			return true;

		if ( ActivePage && ActivePage->IgnoredRepeatKey == iKey )
			return true;

		// enough time has passed - send another keypress
		State = IST_Press;
		RepeatTime[ControllerId] = CurrentTime + ButtonRepeatDelay * 0.5;
	}

	else if ( State == IST_Press )
		RepeatTime[ControllerId] = CurrentTime + ButtonRepeatDelay * 1.5;

	// =============================================================================================
	// Process the two joysticks on the XBox Controller (fold in to cursor input)
	// =============================================================================================
	if ( bValidDelta )
	{
		switch ( iKey )
		{
		case XBC_LeftStickX:
			DistributeXControllerEvent( ControllerId, Delta > 0.f ? XC_PadRight : XC_PadLeft );
			break;

		case XBC_LeftStickY:
			DistributeXControllerEvent( ControllerId, Delta > 0.f ? XC_PadUp : XC_PadDown );
			break;

		case XBC_RightStickX:
			DistributeXControllerEvent( ControllerId, Delta > 0.f ? XC_AltRight : XC_AltLeft );
			break;

		case XBC_RightStickY:
			DistributeXControllerEvent( ControllerId, Delta > 0.f ? XC_AltUp : XC_AltDown );
			break;
		}

		if ( DeleteStack.Num() )
			eventClearDeleteStack();

		return true;
	}

	// =============================================================================================
	// Process the cusor pad on the XBox Controller (Remember, Joystick movement is converted to cursors)
	// =============================================================================================
	//scion bcl -- Note that folding Start and A together is no longer a TCR (FEB 2004), just a best
	//practice. They are now split apart.
	else if ( iKey >= XBC_A && iKey <= XBC_RightThumb  )	// Analog Buttons
	{
		if ( ControllerMask != 255 &&	// if there is currently a controller unplugged
			iKey == XBC_Start )			// and the user is pressing start
		{
			// only process the key release - this prevents UIX from taking any actions
			// when the start button is released
			if ( State == IST_Release )
				DistributeXControllerEvent(ControllerId, XC_Start);

			return true;
		}

		else
		if ( State==IST_Press || State==IST_Repeat )
		{
			switch (iKey)
			{
			case XBC_A:				DistributeXControllerEvent(ControllerId,XC_A);				break;
			case XBC_B:				DistributeXControllerEvent(ControllerId,XC_B);				break;
			case XBC_X:				DistributeXControllerEvent(ControllerId,XC_X);				break;
			case XBC_Y:				DistributeXControllerEvent(ControllerId,XC_Y);				break;
			case XBC_Black:			DistributeXControllerEvent(ControllerId,XC_Black);			break;
			case XBC_White:			DistributeXControllerEvent(ControllerId,XC_White);			break;
			case XBC_LeftTrigger:	DistributeXControllerEvent(ControllerId,XC_LeftTrigger);	break;
			case XBC_RightTrigger:	DistributeXControllerEvent(ControllerId,XC_RightTrigger);	break;
			case XBC_PadUp:			DistributeXControllerEvent(ControllerId,XC_PadUp);			break;
			case XBC_PadDown:		DistributeXControllerEvent(ControllerId,XC_PadDown);		break;
			case XBC_PadLeft:		DistributeXControllerEvent(ControllerId,XC_PadLeft);		break;
			case XBC_PadRight:		DistributeXControllerEvent(ControllerId,XC_PadRight);		break;
			case XBC_Start:			DistributeXControllerEvent(ControllerId,XC_Start);			break;
			case XBC_Back: 			DistributeXControllerEvent(ControllerId,XC_Back);			break;
			case XBC_LeftThumb:		DistributeXControllerEvent(ControllerId,XC_LeftThumb);		break;
			case XBC_RightThumb:	DistributeXControllerEvent(ControllerId,XC_RightThumb);		break;
			}

			if ( DeleteStack.Num() )
				eventClearDeleteStack();

			return true;
		}
	}

	// =============================================================================================
	// Handle Mouse button input
	// =============================================================================================

	if (iKey==IK_LeftMouse)
	{
		if (State==IST_Press  || State==IST_Repeat)			// Process Mouse Presses
		{
#ifndef _XBOX
			if ( State==IST_Press )
				MouseClickPress( this );
#endif
			MousePressed(0);
		}
		else if (State==IST_Repeat)		// Process Mouse Repeat
		{
			if (!bIgnoreNextRelease)
				MousePressed(1);
		}
		else if (State==IST_Release)	// Process Mouse Release
		{
			if (!bIgnoreNextRelease)
			{
				MouseReleased();
#ifndef _XBOX
				MouseClickRelease( this );
#endif
			}
			bIgnoreNextRelease=false;
		}

		if ( DeleteStack.Num() )
			eventClearDeleteStack();

		return true;

	}

	// =============================================================================================
	// Pass key events along to the pages.  Start with the active page, then
	// proceed upward in the stack
	// =============================================================================================
	if (State==IST_Repeat)
		State=IST_Press;	// Fold Repeats in to Mouse Presses; includes joe-wilcox's fix from UDN

//debug --> process console hot key
	if ( State == IST_Release && ViewportOwner->Console )
	{
		UConsole* Console = Cast<UConsole>(ViewportOwner->Console);
		if ( Console && iKey == Console->ConsoleKey )
		{
			State = IST_Press;
			return false;
		}
	}
//debug <--

#ifndef _XBOX
	if ( ( ActivePage!=NULL) && ( ActivePage->NativeKeyEvent(iKey, State, Delta) ) )
	{
		if ( DeleteStack.Num() )
			eventClearDeleteStack();

		return true;
	}

	// The Active Page didn't swallow it.. 		pass it along
	for (INT i=MenuStack.Num()-1;i>=0; i--)
	{
		if (MenuStack(i) != ActivePage)
		{
			if ( MenuStack(i)->NativeKeyEvent(iKey, State, Delta) )
			{
				if ( DeleteStack.Num() )
					eventClearDeleteStack();

				return true;
			}
		}
	}
#endif

	return true;

	unguard;
} // NativeKeyEvent


void UGUIController::DistributeXControllerEvent( BYTE Id, eXControllerCodes Code )
{
	guardSlow(UGUIController::DistributeXControllerEvent);

	UGUIPage* Page = ActivePage;
	if ( Page && Page->XControllerEvent(Id,Code) )
		return;

	for ( INT i = MenuStack.Num() - 1; i >= 0; i-- )
	{
		Page = MenuStack(i);
		if ( Page && Page != ActivePage )
		{
			UGUIPage* PrevPage = (i + 1 < MenuStack.Num() - 1) ? MenuStack(i+1) : NULL;
			if ( !PrevPage || (PrevPage && PrevPage->delegateGetMenuInputMode() == MIM_NonBlocking) )
			{
				BYTE OldMenuState = Page->MenuState;

				// Won't accept XControllerEvents if it isn't focused, so temporarily set its menustate to MSAT_Focused.
				Page->MenuState = MSAT_Focused;

				if ( Page->XControllerEvent(Id,Code) )
				{
					Page->MenuState = OldMenuState;
					break;
				}

				Page->MenuState = OldMenuState;
			}

			else break;
		}
	}

	unguardSlow;
}

// Pre Render is used for sizing/positioning

void  UGUIController::NativePreRender(UCanvas* Canvas)
{

	guard(GUIController::NativePreRender);

//	if ( !Canvas || !Master || !Master->Client || Master->Client->Viewports.Num()==0 || !Master->Client->Viewports(0) )
	if ( !ViewportOwner || !Canvas /*|| bControllerUnplugged*/ )
		return;

	UViewport* Viewport = CastChecked<UViewport>(ViewportOwner);
	// D-2: the canvas's size, which is the UI's 640x480 design space when the UI is in a centred box
	// (UIScaleMode=Fit43, docs/porting/analysis/ui-scale.md) and the viewport's pixels otherwise (as before)
	ResX = Canvas->SizeX;
	ResY = Canvas->SizeY;

	// If we are using the Windows Mouse.. pass it along before any rendering
	Viewport->bShowWindowsMouse = Viewport->bWindowsMouseAvailable;

#ifndef _XBOX
	if ( bCurMenuInitialized && ActivePage && ActivePage->MenuState != MSAT_Focused )
	{
		// the active page's menu state has been changed to blurry because of window switching while debugging
		ActivePage->MenuState = MSAT_Focused;
	}
#endif

	UGUIPage* page = NULL;

	// Move through the stack and Draw each Page.  Each page will then iterate though all of
	// its components and render them
	for ( INT i = 0; i < MenuStack.Num(); i++ )
	{
		page = MenuStack(i);
		if ( page )
		{
#ifdef _DEBUG
			// For debugging first-render "snapping"
			if ( !page->bPositioned && appIsDebuggerPresent() )
				debugf(TEXT("First render for %s"), page->GetName());
#endif

			page->SaveCanvasState(Canvas);
			if ( OBJ_DELEGATE_IS_SET(page,OnPreRender) && page->delegateOnPreRender(Canvas, page) )
			{
				page->RestoreCanvasState(Canvas);
				continue;
			}

			page->PreDraw(Canvas);
			page->UpdateBounds();

			if ( OBJ_DELEGATE_IS_SET(page,OnPreDraw) && page->delegateOnPreDraw(Canvas) )
				page->UpdateBounds(1);

			page->RestoreCanvasState(Canvas);
		}
	}

	if ( DeleteStack.Num() )
		eventClearDeleteStack();

	unguard;

}

// NativePostRender passes render control to each component.  It starts with all components
// on the Active page, and then continues until all components are rendered

void  UGUIController::NativePostRender(UCanvas* Canvas)
{

	guard(GUIController::NativePostRender);

	if ( !Canvas || !ViewportOwner /*|| bControllerUnplugged*/ )
		return;

	Canvas->CurX = 0;
	Canvas->Style=1;
	// D-2: what the pages draw now is menu: clipped to the UI's box as the Xbox's screen clipped it, a full-screen
	// page background follows UIMenuBackground (UnCanvas.cpp, ui-scale.md)
	Canvas->SetUIContext( UICONTEXT_Menu );

	UGUIPage* page = NULL;
	TArray<UGUIPage*> Menus;

	UBOOL bEnableRender=1;

	TArray<FLOAT> AlphaScale;
	FLOAT CurrentAlphaScale = 1.f;

	AlphaScale.Insert(0);
	AlphaScale(0) = CurrentAlphaScale;

	for ( INT i = MenuStack.Num() - 1; i >= 0; i-- )
	{
		page = MenuStack(i);
		if ( page )
		{
			if ( bEnableRender || page->bAlwaysRender )
			{
				AlphaScale.Insert(0);
				AlphaScale(0) = page->bAlwaysRender ? 1.f : CurrentAlphaScale;
				Menus.Insert(0);
				Menus(0) = page;

				FLOAT FadePercentage = OverlayFadePercent;
				if ( page->eventShouldFadeParent(FadePercentage) )
					CurrentAlphaScale *= FadePercentage;
			}

			if ( !page->bRenderWorld )
				bEnableRender = false;
		}
	}

	for ( INT i = 0; i < Menus.Num(); i++ )
	{
		page = Menus(i);

		// Apply any alpha scaling
		FLOAT LastAlpha = Canvas->ForcedAlpha;

		Canvas->ForcedAlpha *= AlphaScale(i);
		page->SaveCanvasState(Canvas);

		if ( !OBJ_DELEGATE_IS_SET(page,OnDraw) || !page->delegateOnDraw(Canvas) )
			page->Draw(Canvas);

		if ( OBJ_DELEGATE_IS_SET(page,OnRender) )
			page->delegateOnRender(Canvas);

		page->RestoreCanvasState(Canvas);
		page->bRendered = 1;

		// Restore previous alpha
		Canvas->ForcedAlpha = LastAlpha;
	}

	// D-2: the pages are drawn; the designer's frames and the cursor are not clipped to the UI's box
	Canvas->SetUIContext( UICONTEXT_Game );

	if ( bDesignMode )
		RenderDesignMode(Canvas);
	else if ( MoveControl && DesignerVisible() )
	{
		if ( ActivePage )
			ActivePage->SaveCanvasState(Canvas);

		Canvas->Color = FColor(255,0,0,255);
		Canvas->DrawTileStretched(WhiteBorder,MoveControl->Bounds[0], MoveControl->Bounds[1],
				MoveControl->Bounds[2] - MoveControl->Bounds[0], MoveControl->Bounds[3] - MoveControl->Bounds[1]);

		if ( ActivePage )
			ActivePage->RestoreCanvasState(Canvas);
	}

	// Render the Mouse Cursor if we are not using the windows cusor.  Otherwise set the cursor index
	if (!bHideMouseCursor && !ViewportOwner->bShowWindowsMouse)
	{

		UMaterial* MouseMat;
		FVector MouseOffset;

		if (ActiveControl!=NULL)
		{
			MouseMat = MouseCursors(ActiveControl->MouseCursorIndex);
			MouseOffset = MouseCursorOffset(ActiveControl->MouseCursorIndex);
		}
		else
		{
			MouseMat = MouseCursors(0);
			MouseOffset = MouseCursorOffset(0);
		}

		if (MouseMat!=NULL)
		{
			FColor OldColor = Canvas->Color;
			BYTE OldCanvasStyle = Canvas->Style;

			Canvas->Color = FColor(255,255,255,255);
			Canvas->Style = STY_Alpha;

			FLOAT MouseScale = Max<FLOAT>(Canvas->SizeX / 640, 1.f);
			// D-2: the software cursor goes over the whole viewport, also outside the UI's box
			FLOAT CX1 = 0.f, CY1 = 0.f, CX2 = Canvas->SizeX, CY2 = Canvas->SizeY;
			if ( Canvas->IsUIBoxed() )
				Canvas->GetTargetInDesign( CX1, CY1, CX2, CY2 );
			FLOAT MX = Clamp<FLOAT>(MouseX, CX1, CX2);
			FLOAT MY = Clamp<FLOAT>(MouseY, CY1, CY2);

			FLOAT MouseSizeX = MouseMat->MaterialUSize()*MouseScale;
			FLOAT MouseSizeY = MouseMat->MaterialVSize()*MouseScale;

			INT DrawMouseX = MX - (MouseOffset.X * MouseSizeX);
			INT DrawMouseY = MY - (MouseOffset.Y * MouseSizeY);


			// Clamp the software mouse
			LastMouseX = MouseX;
			LastMouseY = MouseY;

			MouseX = MX;
			MouseY = MY;

			// If the Window's mouse is available, add code here to use it instead

			Canvas->DrawTile
			(
				MouseMat,
				DrawMouseX,
				DrawMouseY,
				MouseSizeX,
				MouseSizeY,
				0,
				0,
				MouseMat->MaterialUSize(),
				MouseMat->MaterialVSize(),
				Canvas->CurZ,
				Canvas->Color.Plane()
			);

			Canvas->Color = OldColor;
			Canvas->Style = OldCanvasStyle;
		}
	}
	else
	{
		LastMouseX = MouseX;
		MouseX = ViewportOwner->WindowsMouseX;
		MouseY = ViewportOwner->WindowsMouseY;
		Canvas->TargetToDesign( MouseX, MouseY );	// D-2: window pixels -> the canvas's

		if ( ActiveControl )
			ViewportOwner->SelectedCursor = ActiveControl->MouseCursorIndex;
	}

	if ( DeleteStack.Num() )
		eventClearDeleteStack();

	unguard;
}

void UGUIController::execMouseEmulation( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIController::execMouseEmulation);

	P_GET_UBOOL(bOk);
	P_FINISH;

	if (!bIsConsole)	// This function is Console only
		return;

	bJoyMouse = bOk;
	bHideMouseCursor = !bOk;

	if (bJoyMouse)
	{
		MouseX = (Master->Client->Viewports(0)->SizeX) / 2;
		MouseY = (Master->Client->Viewports(0)->SizeY) / 2;
		if ( Master->Client->Viewports(0)->Canvas )
			Master->Client->Viewports(0)->Canvas->TargetToDesign( MouseX, MouseY );	// D-2
	}

	unguardexec;
}

void UGUIController::execGetMapList(FFrame& Stack, RESULT_DECL )
{
	guard(GUIController::execGetMapList);

	P_GET_STR(Prefix);
	P_GET_OBJECT(UGUIListBase, List);
	P_FINISH

	TCHAR Wildcard[256];
	appSprintf( Wildcard, TEXT("*.%s"), *FURL::DefaultMapExt );

	List->Elements.Empty();

	TArray<FString> Maps;

	for( INT DoCD=0; DoCD<1+(GCdPath[0]!=0); DoCD++ )
	{
		for( INT i=0; i<GSys->Paths.Num(); i++ )
		{
			if( appStrstr( *GSys->Paths(i), Wildcard ) )
			{
				TCHAR Tmp[256]=TEXT("");
				if( DoCD )
				{
					appStrcat( Tmp, GCdPath );
					appStrcat( Tmp, TEXT("System") PATH_SEPARATOR );
				}
				appStrcat( Tmp, *GSys->Paths(i) );
				*appStrstr( Tmp, Wildcard )=0;
				appStrcat( Tmp, *Prefix );
				appStrcat( Tmp, Wildcard );

				Maps += GFileManager->FindFiles(Tmp,1,0);
			}
		}
	}

	if ( Maps.Num() )
		Sort( &Maps(0), Maps.Num() );

	for ( INT i = 0; i < Maps.Num(); i++ )
	{
		INT k=List->Elements.AddZeroed();

		FString MapName = Maps(i).LeftChop(4);
		List->Elements(k).ExtraStrData = MapName;
		List->Elements(k).item = MapName.Mid(MapName.InStr(TEXT("-")) + 1);
	}

	unguardexec;
}

void UGUIController::execGetCurrentRes(FFrame& Stack, RESULT_DECL )
{
	guard(GUIController::execGetCurrentRes);

	P_FINISH;

	if ( Master==NULL || Master->Client==NULL || Master->Client->Viewports.Num()==0 || Master->Client->Viewports(0)==NULL)
		*(FString*)Result = FString::Printf(TEXT(""));
	else
		*(FString*)Result = FString::Printf(TEXT("%ix%i"), Master->Client->Viewports(0)->SizeX,Master->Client->Viewports(0)->SizeY);

	unguardexec;
}

void UGUIController::execGetMainMenuClass(FFrame& Stack, RESULT_DECL )
{
	guard(GUIController::execMainMenuClass);

	P_FINISH;

	*(FString*)Result = *Cast<UGameEngine>(Master->Client->Engine)->MainMenuClass;

	unguardexec;
}

void UGUIController::execFindViewportIndex( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIController::execFindViewportIndex);

	P_GET_OBJECT(UPlayer,V);
	P_GET_UBOOL_OPTX(bRequireValid,0);
	P_FINISH;

//	if ( ActiveChildViewport && !bRequireValid)
//		V = ActiveChildViewport;

	INT i;
	for ( i = 0; i < ChildViewports.Num(); i++ )
	{
		if( ChildViewports(i) == V )
			break;
	}

	*(INT*)Result = ChildViewports.IsValidIndex(i)
		? i
		: bRequireValid
			? INDEX_NONE
			: 0;

	unguardexec;
}

void UGUIController::execHasCorruptedContent(FFrame& Stack, RESULT_DECL)
{
	guard(UGUIController::execHasCorruptedContent);

	P_FINISH;

	UBOOL bHasCorruptedContent;

	FContentManager* ContentManager = FContentManager::GetInstance();
	if (ContentManager != NULL)
	{
		bHasCorruptedContent = ContentManager->HasCorruptedContent();
	}
	else
	{
		bHasCorruptedContent = FALSE;
	}

	*(UBOOL*) Result = 	bHasCorruptedContent;

	unguardexec;
}

void UGUIController::execDeleteCorruptedContent(FFrame& Stack, RESULT_DECL)
{
	guard(UGUIController::execDeleteCorruptedContent);

	P_FINISH;

	FContentManager* ContentManager = FContentManager::GetInstance();
	if (ContentManager != NULL)
	{
		ContentManager->DeleteCorruptedContent();
	}

	unguardexec;
}

void UGUIController::execFindControllerPortIndex(FFrame& Stack, RESULT_DECL )
{
	guard(UGUIController::execFindControllerPortIndex);

	P_GET_BYTE(ControllerPortId);
	P_FINISH;

	*(INT*)Result = ChildViewports.Num() ? INDEX_NONE : 0;

	if ( ControllerPortId == UViewport::INVALID_GAMEPAD_ID )
	{
		BYTE InputMode = ActivePage ? ActivePage->delegateGetMenuInputMode() : MIM_Bound;
		ControllerPortId = InputMode == MIM_Cooperative ? 0 : ControllerId;
	}

	if ( ControllerMask == 255 && ActiveChildViewport )
		ControllerPortId = ActiveChildViewport->GamepadID;

	for ( INT i = 0; i < ChildViewports.Num(); i++ )
	{
		if ( ChildViewports(i) && ChildViewports(i)->GamepadID == ControllerPortId )
		{
			*(INT*)Result = i;
			break;
		}
	}

	unguardexec;
}

void UGUIController::execResetSplitScreen( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIController::execResetSplitScreen);
	P_FINISH;

	*(UBOOL*)Result = 0;
	if ( Master && Master->Client && Master->Client->Viewports.Num() )
	{
		// first, make sure we don't have any menus open that are linked to one of the viewports we'll be deleting
		UViewport* OriginalViewport = Master->Client->Viewports(0);
		for ( INT i = 0; i < MenuStack.Num(); i++ )
		{
			UGUIPage* Page = MenuStack(i);
			if ( ChildViewports.IsValidIndex(Page->ActiveViewportIndex) &&
				ChildViewports(Page->ActiveViewportIndex) != OriginalViewport )
			{
				debugf(NAME_DevUI, TEXT("ResetSplitScreen failed - Menu %i (%s) still referencing split-screen viewport!"), i, Page->GetClass()->GetName());
				return;
			}
		}

		USplitScreenMgr* SplitScreenMgr = Master->Client->GetSplitScreenMgr();
		if ( SplitScreenMgr )
		{
			SplitScreenMgr->ResetState();
			*(UBOOL*)Result = 1;
		}
	}

	unguardexec;
}

void UGUIController::execGetMenuFont(FFrame& Stack, RESULT_DECL )
{
	guard(GUIController::execGetMenuFont);

	P_GET_STR(FontName);
	P_FINISH;

	if (FontStack.Num()==0)
	{
		*(UObject**) Result = NULL;
		return;
	}

	for (INT i=0;i<FontStack.Num();i++)
	{
		if ( FontName == FontStack(i)->KeyName )
		{
			*(UObject**) Result = FontStack(i);
			return;
		}
	}

	*(UObject**) Result = NULL;

	unguardexec;
}

void UGUIController::execGetStyle(FFrame& Stack, RESULT_DECL )
{
	guard(GUIController:execGetStyle);

	P_GET_STR(StyleName);
	P_FINISH;

	if (StyleStack.Num()==0)
	{
		*(UObject**) Result = NULL;
		return;
	}

	for (INT i=0;i<StyleStack.Num();i++)
	{
		if ( StyleName == StyleStack(i)->KeyName )
		{
			*(UObject**) Result = StyleStack(i);
			return;
		}
	}

	*(UObject**) Result = NULL;

	unguardexec;
}

void UGUIController::execClearCloseQueue( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIController::execClearCloseQueue);

	P_GET_UBOOL_OPTX(bForce,0);
	P_FINISH;

	ClearCloseQueue(bForce);
	unguardexec;
}

void UGUIController::execKeyDown( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIController::execKeyDown);

	P_GET_OBJECT(UViewport,V);
	P_GET_BYTE(iKey);
	P_FINISH;

	if ( V && V->Input )
		*(UBOOL*)Result = KeyDown(V,iKey);

	unguardexec;
}

static INT Compare( FMutatorRecord& A, FMutatorRecord& B )
{
	return appStrcmp(*A.FriendlyName, *B.FriendlyName);
}

void UGUIController::execSortMutatorList( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIController::execSortMutatorList);

	P_FINISH;

	if ( Mutators.Num() )
	{
		Sort( &Mutators(0), Mutators.Num() );
	}

	unguardexec;
}

void UGUIController::execResetDesigner( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIController::execResetDesigner);

	P_FINISH;

	ResetDesigner();
	unguardexec;
}

void UGUIController::execSetMoveControl( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIController::execSetMoveControl);

	P_GET_OBJECT(UGUIComponent,NewMoveControl);
	P_FINISH;

	SetMoveControl(NewMoveControl);

	unguardexec;
}

void UGUIController::SetMoveControl( UGUIComponent* C )
{
	guard(UGUIController::SetMoveControl);

	MoveControl = C;
	if ( MoveControl )
		ShowProperties(MoveControl);

	unguard;
}

void UGUIController::CreatePropertyWindow()
{
	guard(UGUIController::CreatePropertyWindow);

#ifndef _XBOX
	if ( !Master || !Master->Client || !Master->Client->Viewports.Num() )
		return;

#ifdef WIN32
	UViewport* Port = Master->Client->Viewports(0);
	Port->Exec(TEXT("ENDFULLSCREEN"));

	if ( !Designer )
	{
		UClass* ManagerClass = LoadClass<UPropertyManagerBase>(NULL, TEXT("GUIDesigner.PropertyManager"), NULL, LOAD_NoWarn, NULL);
		if ( ManagerClass )
			Designer = (PTRINT)ConstructObject<UPropertyManagerBase>(ManagerClass);
	}

	if ( Designer )
	{
		((UPropertyManagerBase*)Designer)->SetWindow(Port->GetWindow());
		((UPropertyManagerBase*)Designer)->SetParent(this);
	}
#endif	// WIN32
#endif	// _XBOX

	unguard;
}

void UGUIController::HidePropertyWindow()
{
	guard(UGUIController::HidePropertyWindow);

	if ( Designer )
	{
		((UPropertyManagerBase*)Designer)->SetCurrent(NULL);
		((UPropertyManagerBase*)Designer)->Show(0);
	}

	unguard;
}

void UGUIController::ResetDesigner()
{
	guard(UGUIController::ResetDesigner);
 	if ( Designer )
	{
		HidePropertyWindow();
		delete ((UPropertyManagerBase*)Designer);
	}

	Designer = NULL;
	unguard;
}

UBOOL UGUIController::DesignerVisible()
{
	guard(UGUIController::DesignerVisible);

	if ( !Designer )
		return 0;

	return ((UPropertyManagerBase*)Designer)->IsVisible();
	unguard;
}

UBOOL UGUIController::ShowProperties(UGUIComponent* Comp )
{
	guard(UGUIController::ShowProperties);

	if ( !Designer )
		CreatePropertyWindow();

	if ( Designer )
	{
		if ( Comp )
			((UPropertyManagerBase*)Designer)->SetCurrent( (UObject**)&Comp );
		else ((UPropertyManagerBase*)Designer)->SetCurrent(NULL);
		RepeatKey[ControllerId] = 0;
	}

	return Designer != NULL;

	unguard;
}

void UGUIController::ClearCloseQueue( UBOOL bForce )
{
	guard(UGUIController::ClearCloseQueue);

	while ( CloseQueue.Num() )
	{
		FCloseMenuItem& Item = CloseQueue(0);

		if ( Item.ReplaceParams.MenuClassName.Len() )
		{
			eventReplaceSpecificMenu(
				Item.Menu, 
				Item.ReplaceParams.MenuClassName, 
				Item.bCancelled, 
				Item.ReplaceParams.Param1, 
				Item.ReplaceParams.Param2
				);
		}
		else
			eventRemoveMenu(Item.Menu, Item.bCancelled, bForce);

		CloseQueue.Remove(0);
	}

	unguard;
}

void UGUIController::Destroy()
{
	guard(UGUIController::Destroy);

	ResetDesigner();
	if ( Designer )
		delete ((UPropertyManagerBase*)Designer);

	Designer = NULL;

	Super::Destroy();

	unguard;
}

void UGUIController::execResetInput( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIController::execResetInput);

	P_FINISH;

	ResetInput();
	unguardexec;
}

void UGUIController::execPassAlongKeystroke( FFrame & Stack, RESULT_DECL )
{
	guard(UGUIController::execPassAlongKeystroke);
	P_GET_BYTE(iKey);
	P_GET_BYTE(State);
	P_GET_FLOAT(Delta);

	P_FINISH;
	// =============================================================================================
	// Pass key events along to the pages.  Start with the active page, then
	// proceed upward in the stack
	// =============================================================================================

	if (State==IST_Release)
		State=IST_Press;	// Fold Repeats in to Mouse Presses

	if ( ( ActivePage!=NULL) && ( ActivePage->NativeKeyEvent(iKey, State, Delta) ) )
		return;

	// The Active Page didn't swallow it.. 		pass it along

	for (INT i=MenuStack.Num()-1;i>=0; i--)
	{
		if (MenuStack(i) != ActivePage)
		{
			if ( MenuStack(i)->NativeKeyEvent(iKey, State, Delta) )
				return;
		}
	}
	unguardexec;
} // execPassAlongKeystroke

UBOOL UGUIController::NativeKeyType(BYTE& iKey, TCHAR Unicode )
{
	guard(GUIController::NativeKeyType);

	// If console is visible, route all input to the console
	if ( ViewportOwner && ViewportOwner->Console && ViewportOwner->Console->bVisible )
		return false;

	if (ActivePage==NULL)
		return true;

	if ( bModAuthor && bDesignMode && iKey == IK_Cancel )
		return true;

	if ( !ActivePage->NativeKeyType(iKey, Unicode) )
	{
		// The Active Page didn't swallow it.. pass it along

		for (INT i=MenuStack.Num()-1; i>=0; i--)
		{
			if (MenuStack(i) != ActivePage)
			{
				if ( MenuStack(i)->NativeKeyType(iKey, Unicode) )
					return true;
			}
		}

#ifndef _XBOX
		// M10: an on-screen keyboard page's field takes what a real keyboard types.
		// (Its delegate directly: the field is display-only for focus purposes, bAcceptsInput false,
		// so UGUIComponent::NativeKeyType would refuse; UC2KeyboardPanel calls the same handler for
		// its own key buttons.)
		UGUIEditBox* TextBox = bEmulatedJoypad ? TextEntryTarget( this ) : NULL;
		if ( TextBox && TextBox != eventGetFocusedControl() && (BYTE)Unicode >= 32 && OBJ_DELEGATE_IS_SET(TextBox,OnKeyType) )
		{
			debugf( NAME_DevUI, TEXT("Typed '%c' into %s"), Unicode, TextBox->GetMenuPath() );
			TextBox->delegateOnKeyType( iKey, FString::Printf(TEXT("%c"), Unicode) );
		}
#endif
	}

	return true;

	unguard;
}

// The Mouse Button was pressed

UBOOL UGUIController::MousePressed(UBOOL IsRepeat)
{
	guard(UGUIController::MousePressed);

	bIgnoreUntilPress = false;

	if (ActiveControl && ActiveControl->bCaptureMouse && ActiveControl->PageOwner == ActivePage )
	{
		ActiveControl->MousePressed(IsRepeat);
		return true;
	}

	UGUIPage* page = ActivePage;
	if ( page && page->MousePressed(IsRepeat) )
		return true;

	for ( INT i = MenuStack.Num() - 1; i >= 0; i-- )
	{
		page = MenuStack(i);
		if ( page && page != ActivePage && page->MousePressed(IsRepeat) )
			return true;
	}

	return false;

	unguard;

}

// The Mouse Button was released

UBOOL UGUIController::MouseReleased()
{
	guard(UGUIController::MouseReleased);

	if (bIgnoreUntilPress)
		return false;

#ifndef _XBOX
	if ( ViewportOwner && ViewportOwner->bShowWindowsMouse && !ValidMousePos() )
		return true;
#endif

	if ( ActiveControl && ActiveControl->MenuState == MSAT_Pressed && /*ActiveControl->bCaptureMouse*/ ActiveControl->PageOwner == ActivePage )
	{
		UGUIComponent* C = ActiveControl;
		if ( /*!C->IsAnimating() &&*/ (C->MouseReleased() || C->bCaptureMouse) )
			return true;
	}

	UGUIPage* page = ActivePage;
	if ( page && (page->MouseReleased()) )
		return true;

	for ( INT i = MenuStack.Num() - 1; i >= 0; i-- )
	{
		page = MenuStack(i);
		if ( page && page != ActivePage && page->MouseReleased() )
			return true;
	}

	return false;

	unguard;
}

UGUIComponent* UGUIController::UnderCursor(FLOAT MouseX, FLOAT MouseY)
{
	if (ActivePage!=NULL)
	{
		return ActivePage->UnderCursor(MouseX, MouseY);
	}
	return NULL;
}

void UGUIController::LookUnderCursor(FLOAT dX, FLOAT dY)
{
#if 0
	UGUIComponent* Under = UnderCursor(MouseX, MouseY);

	// Handle moving over new control
	if (ActiveControl)
	{
		if ( (ActiveControl->MenuState!=MSAT_Pressed) || (!ActiveControl->bCaptureMouse) )
		{
			// Remove the first one

			if ( ActiveControl != Under)
			{
				if (ActiveControl->MenuState==MSAT_Watched)
					ActiveControl->eventMenuStateChange(MSAT_Blurry);

			}

			if ( (Under!=NULL) && (Under->MenuState==MSAT_Blurry) )
			{
					Under->eventMenuStateChange(MSAT_Watched);

					// Mouse over sound
					if (ActiveControl != Under)
					{
						eventPlayClickSound(CS_Focus);
					}
			}

			ActiveControl = Under;

			// Call MouseHover on the thing the mouse is on.
			if(ActiveControl != NULL)
				ActiveControl->MouseHover();
		}
		else
			ActiveControl->MouseMove(dX,dY);
	}
	else if (Under)
	{
		ActiveControl = Under;
		if ( ActiveControl->MenuState==MSAT_Blurry )
		{
			ActiveControl->eventMenuStateChange(MSAT_Watched);

			// Mouse over sound
			if(ActiveControl->bMouseOverSound)
				eventPlayClickSound(CS_Focus);
		}

		if(ActiveControl)
			ActiveControl->MouseHover();
	}
#else
	ActiveControl = UnderCursor(MouseX, MouseY);

#ifndef _XBOX
	// M10: moving the mouse over a menu item selects it, as the D-pad would (only on real
	// motion, so a resting cursor never takes the focus back from keyboard or pad navigation).
	if ( (dX != 0.f || dY != 0.f) && ActiveControl && ActiveControl->bTabStop && ActiveControl->bAcceptsInput &&
		ActiveControl->MenuState == MSAT_Blurry && ActiveControl->PageOwner == ActivePage &&
		ActiveControl != eventGetFocusedControl() && ActiveControl->eventCanAcceptFocus(0) )
	{
		debugf( NAME_DevUI, TEXT("Mouse hover selects %s"), ActiveControl->GetMenuPath() );
		ActiveControl->eventSetFocus( NULL );
	}
#endif
#endif
}

#ifndef _XBOX
//
// M10: a left click is A on the item under the cursor. Epic's MousePressed/MouseReleased give the
// item the focus and call its OnClick; UC2's pad-driven items have no OnClick (they act on
// XControllerEvent), and its lists act on their selected row, not the row under the cursor.
// So: on press, a list selects the row under the cursor (its own InternalOnClick, which Epic
// binds to OnClick but UC2 pages replace); on release over the same item, an item that has no
// OnClick but an XControllerEvent handler gets XC_A.
//
static UGUIComponent* GClickTarget = NULL;

static void MouseClickPress( UGUIController* C )
{
	GClickTarget = NULL;
	if ( !C->bEmulatedJoypad )
		return;
	UGUIComponent* Under = C->UnderCursor( C->MouseX, C->MouseY );
	if ( !Under || Under->PageOwner != C->ActivePage )
		return;
	GClickTarget = Under;
	if ( Under->IsA(UGUIListBase::StaticClass()) )
	{
		UFunction* Select = Under->FindFunction( FName(TEXT("InternalOnClick"), FNAME_Find) );
		if ( Select && Select->ParmsSize >= (INT)sizeof(UGUIComponent*) )
		{
			BYTE* Parms = (BYTE*)appAlloca( Select->ParmsSize );
			appMemzero( Parms, Select->ParmsSize );
			*(UGUIComponent**)Parms = Under;
			Under->ProcessEvent( Select, Parms );
			debugf( NAME_DevUI, TEXT("Mouse click selects the row under the cursor in %s"), Under->GetMenuPath() );
		}
	}
}

static void MouseClickRelease( UGUIController* C )
{
	UGUIComponent* Target = GClickTarget;
	GClickTarget = NULL;
	if ( !C->bEmulatedJoypad || !Target || !IsValidObject(Target) || Target->PageOwner != C->ActivePage )
		return;
	if ( C->UnderCursor( C->MouseX, C->MouseY ) != Target )
		return;
	if ( OBJ_DELEGATE_IS_SET(Target,OnClick) || !OBJ_DELEGATE_IS_SET(Target,OnXControllerEvent) )
		return;		// Epic's OnClick did it, or nothing would react to A either
	if ( Target != C->eventGetFocusedControl() )
		Target->eventSetFocus( NULL );
	debugf( NAME_DevUI, TEXT("Mouse click is A on %s"), Target->GetMenuPath() );
	C->DistributeXControllerEvent( C->ControllerId, XC_A );
}
#endif

UBOOL UGUIController::HasMouseMoved( FLOAT ErrorMargin )
{
	return Abs(MouseX - LastMouseX) > Abs(ErrorMargin) || Abs(MouseY - LastMouseY) > Abs(ErrorMargin);
}


// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIFonts
// =======================================================================================================================================================
// =======================================================================================================================================================

void UGUIFont::execGetFont(FFrame& Stack, RESULT_DECL )
{
	guard(GUIFont::execGetFont);

	P_GET_INT(XRes);
	P_FINISH;

	INT Index;
	if ( (XRes<800) || (bFixedSize) )
		Index = 0;
	else if (XRes<1024)
		Index = 1;
	else if (XRes<1280)
		Index = 2;
	else if (XRes<1600)
		Index = 3;
	else
		Index = 4;

	Index = Min<INT>(Index,FontArrayNames.Num()-1);

	if( Index < 0)
		*(UObject**) Result = NULL;

	if( Index >= FontArrayFonts.Num() )
		FontArrayFonts.AddZeroed(1+Index-FontArrayFonts.Num());

	if( FontArrayFonts(Index) == NULL )
	{
		FontArrayFonts(Index) = Cast<UFont>( StaticLoadObject( UFont::StaticClass(), NULL, *FontArrayNames(Index), NULL, LOAD_NoWarn, NULL ) );
		if( FontArrayFonts(Index) == NULL )
			warnf(TEXT("Warning: %s could not load font %s"), GetName(), *FontArrayNames(Index) );
	}

	*(UObject**) Result = FontArrayFonts(Index);

	unguardexec;
}

// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIStyles
// =======================================================================================================================================================
// =======================================================================================================================================================

void UGUIStyles::execDraw(FFrame& Stack, RESULT_DECL )	// UScript stubs
{
	guard(UGUIStyles::execDraw);

	P_GET_OBJECT(UCanvas, pCanvas);
	P_GET_BYTE(State);
	P_GET_FLOAT(Left);
	P_GET_FLOAT(Top);
	P_GET_FLOAT(Width);
	P_GET_FLOAT(Height);
	P_FINISH;

	Draw(pCanvas,State,Left,Top,Width,Height);

	unguardexec;
}

void UGUIStyles::execDrawText(FFrame& Stack, RESULT_DECL ) // UScript Stubs
{
	guard(UGUIStyles::execDrawText);

	P_GET_OBJECT(UCanvas, pCanvas);
	P_GET_BYTE(State);
	P_GET_FLOAT(Left);
	P_GET_FLOAT(Top);
	P_GET_FLOAT(Width);
	P_GET_FLOAT(Height);
	P_GET_BYTE(Just);
	P_GET_STR(Text);
	P_GET_COLOR_OPTX(OverrideColor, FColor(0,0,0,0));
	P_FINISH;

	DrawText(pCanvas,State,Left,Top,Width,Height,Just, *Text, OverrideColor != FColor(0,0,0,0) ? &OverrideColor : NULL);

	unguardexec;
}


void UGUIStyles::execTextSize(FFrame& Stack, RESULT_DECL )
{
	guard(UGUIStyles::execTextSize);

	P_GET_OBJECT(UCanvas, pCanvas);
	P_GET_BYTE(State);
	P_GET_STR(Text);
	P_GET_FLOAT_REF(XL);
	P_GET_FLOAT_REF(YL);
	P_GET_FLOAT_OPTX(ScaleX,1.f);
	P_GET_FLOAT_OPTX(ScaleY,1.f);
	P_FINISH;

	TextSize(pCanvas,State,*Text,*XL,*YL,ScaleX,ScaleY);

	unguardexec;
}

// Draw outputs the Style object to the canvas using the current menu state

void UGUIStyles::Draw(UCanvas* Canvas, BYTE MenuState, FLOAT Left, FLOAT Top, FLOAT Width, FLOAT Height, FColor * OverrideColor)
{

	guard(UGUIStyles::Draw);
	DrawImage(Canvas, MenuState, Left, Top, Width, Height, Images[MenuState], OverrideColor);
	unguardobj;
}
void UGUIStyles::DrawImage(UCanvas* Canvas, BYTE MenuState, FLOAT Left, FLOAT Top, FLOAT Width, FLOAT Height, UMaterial * Image, FColor * OverrideColor)
{
	guard(UGUIStyles::DrawImage);

	if ( (MenuState<0) || (MenuState>5) || (delegateOnDraw(Canvas,MenuState,Left,Top,Width,Height) ) || (Image==NULL)  )
		return;

	FLOAT mW = Image->MaterialUSize();
	FLOAT mH = Image->MaterialVSize();

	Canvas->Style = RStyles[MenuState];
	Canvas->Color = OverrideColor?*OverrideColor:ImgColors[MenuState];

	//scion bcl -- Cleaned up naming of cases
	switch (ImgStyle[MenuState])
	{
	case ISTY_Normal:
		Canvas->DrawTile(Image, Left, Top, mW, mH, 0, 0, mW, mH, 0, Canvas->Color.Plane());
		break;

	case ISTY_Stretched:
		Canvas->DrawTileStretched(Image, Left, Top, Width, Height);
		break;

	case ISTY_Scaled:
		Canvas->DrawTileScaleBound(Image, Left, Top, Width, Height);
		break;
	}

	unguardobj;
} // DrawImage

// Draw Text outputs text of the current style to the canvas using the current menu state
void UGUIStyles::DrawText(UCanvas* Canvas, BYTE MenuState, FLOAT Left, FLOAT Top, FLOAT Width, FLOAT Height, BYTE Just, const TCHAR* Text, FColor * OverrideColor)
{
	guard(UGUIStyles::DrawText)

	BYTE OldStyle   = Canvas->Style;
	FColor OldColor = Canvas->Color;

	UFont* OldFont  = Canvas->Font;

	// Snap location and size to integer amounts in screen space.
	Left = appRound(Left);
	Top = appRound(Top);
	Width = appRound(Width);
	Height = appRound(Height);


	if ( !CheckCanvasClip(Canvas,Left,Top,Width,Height) )
		return;

	if ( (MenuState<0) ||
		 (MenuState>5) ||
		 (Fonts[MenuState]==NULL) ||
		 (delegateOnDrawText(Canvas,MenuState,Left,Top,Width,Height,Just,FString::Printf(TEXT("%c"), Text)))  )
		return;

	INT FontClipX = Canvas->SizeX;

	Canvas->Style	= RStyles[MenuState];
	Canvas->Color	= OverrideColor?*OverrideColor:FontColors[MenuState];

	Canvas->Font = Fonts[MenuState]->eventGetFont(FontClipX);
	if (Canvas->Font!=NULL)
		Canvas->DrawXboxTextJustified(Just, Left, Top, Left+Width, Top+Height, 1.f, 1.f, OverrideColor ? OverrideColor->A : 255, TEXT("%s"), Text);

	Canvas->Style = OldStyle;
	Canvas->Color = OldColor;
	Canvas->Font  = OldFont;

	unguardobj;
}

void UGUIStyles::TextSize(UCanvas* Canvas, BYTE MenuState, const TCHAR* Test, FLOAT& XL, FLOAT& YL, FLOAT ScaleX, FLOAT ScaleY)
{
	guard(UGUIStyles::TextSize);

	FLOAT FontClipX = Canvas->SizeX;

	Canvas->Font	= Fonts[MenuState]->eventGetFont(FontClipX);
	if (Canvas->Font!=NULL)
	{
		if (*Test == '\0')
		{
			Canvas->ClippedStrLen( Canvas->Font, ScaleX, ScaleY, XL, YL, TEXTSAMPLE );
			XL = 0;
		}
		else
			Canvas->XBoxClippedStrLen( Canvas->Font,ScaleX,ScaleY,XL,YL, Test);
	}

	unguardobj;
}

// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIComponent
// =======================================================================================================================================================
// =======================================================================================================================================================

// PreDraw - Taken from 2K4 for animation

void UGUIComponent::PreDraw(UCanvas* Canvas)
{
	guard(GUIComponent::PreDraw);

	if ( INVALIDRENDER )
		return;

	UGUIController* C = Controller;

	DECLAREBOUNDS;

	// Animate
	if ( MotionFrame.Num() )
	{
		// Grab the current frame
		FVector& Dest = MotionFrame(0);

		// Update the counter
		Dest.Z -= C->RenderDelta;

		// Try to indicate arrival as close as possible to the desired time -
		// If the remaining time is less than half of RenderDelta, trigger the OnArrival
		// during this tick, otherwise, wait until next tick (we'll assume RenderDelta
		// is a constant, even though this is not the case)
		UBOOL bTravelling = Dest.Z >= C->RenderDelta * 0.5;
		FLOAT DestX(ActualLeft(Dest.X)), DestY(ActualTop(Dest.Y));

		if ( bTravelling )	// If we're not there yet
		{
			if ( AL != DestX )
			{
				FLOAT Result = AL + ((DestX - AL) * (C->RenderDelta/Dest.Z));

				// Result should always be between the current position and the destination position
				WinLeft = RelativeLeft(Clamp<FLOAT>( Result,
					Min<FLOAT>(AL, DestX),
					Max<FLOAT>(AL, DestX) ));
			}

			if (AT != DestY)
			{

				FLOAT Result = AT + ((DestY - AT) * (C->RenderDelta/Dest.Z));

				// Result should always be between the current position and the destination position
				WinTop = RelativeTop(Clamp<FLOAT>( Result,
					Min(AT, DestY),
					Max(AT, DestY)));
			}
		}

		else
		{
			WinLeft = Dest.X;
			WinTop = Dest.Y;

			// Send notification that we've arrived at this keyframe
			if ( DELEGATE_IS_SET(OnArrival) )
				delegateOnArrival(this,ANIMATION_Position);

			MotionFrame.Remove(0);

			INT Ndx, Start;
			if ( MotionFrame.Num() < 1 ) 
			{
				switch(LoopAnimation[ANIMATION_Position]) 
				{
				case ANIMATIONLOOP_None:
					eventEndAnimation(this,ANIMATION_Position);
					break;
				case ANIMATIONLOOP_Continuous:
					for (Ndx = 0; Ndx < echoMotionFrame.Num(); ++Ndx) 
					{
						MotionFrame.AddItem(echoMotionFrame(Ndx));
					}
					break;
				case ANIMATIONLOOP_Bounce:
					for ( Ndx = echoMotionFrame.Num() - 1, Start = 0; Ndx > Start; Ndx--, Start++ )
						echoMotionFrame.SwapElements(Ndx, Start);

					for ( Ndx = 1; Ndx < echoMotionFrame.Num(); Ndx++ )
						MotionFrame.AddItem(echoMotionFrame(Ndx));
					break;
				}
			}
		}

	}

	if ( SizeFrame.Num() )
	{
		// Grab the current frame
		FVector& Dest = SizeFrame(0);

		// Update the counter
		Dest.Z -= C->RenderDelta;

		// Try to indicate arrival as close as possible to the desired time -
		// If the remaining time is less than half of RenderDelta, trigger the OnArrival
		// during this tick, otherwise, wait until next tick (we'll assume RenderDelta
		// is a constant, even though this is not the case)
		UBOOL bSizing = Dest.Z >= C->RenderDelta * 0.5;
		FLOAT DestX(ActualWidth(Dest.X)), DestY(ActualHeight(Dest.Y));

		if ( bSizing )
		{
			if ( AW != DestX )
			{
				FLOAT Result = AW + ((DestX - AW) * (C->RenderDelta/Dest.Z));

				// Result should always be between the current position and the destination position
				WinWidth = RelativeWidth(Clamp<FLOAT>( Result,
					Min<FLOAT>( AW, DestX ),
					Max<FLOAT>( AW, DestX )) );
			}

			if ( AH != DestY )
			{
				FLOAT Result = AH + ((DestY - AH) * (C->RenderDelta/Dest.Z));

				// Result should always be between the current position and the destination position
				WinHeight = RelativeHeight(Clamp<FLOAT>( Result,
					Min<FLOAT>( AH, DestY ),
					Max<FLOAT>( AH, DestY )) );
			}
		}
		else
		{
			WinWidth = Dest.X;
			WinHeight = Dest.Y;

			// Send notification that we've arrived at this keyframe
			if ( DELEGATE_IS_SET(OnArrival) )
				delegateOnArrival(this, ANIMATION_Dimension);

			SizeFrame.Remove(0);

			INT Ndx, Start;
			if ( SizeFrame.Num() < 1 )
			{
				switch(LoopAnimation[ANIMATION_Dimension]) 
				{
				case ANIMATIONLOOP_None:
					eventEndAnimation(this,ANIMATION_Dimension);
					break;
				case ANIMATIONLOOP_Continuous:
					for (Ndx = 0; Ndx < echoSizeFrame.Num(); ++Ndx) 
					{
						SizeFrame.AddItem(echoSizeFrame(Ndx));
					}
					break;
				case ANIMATIONLOOP_Bounce:
					for ( Ndx = echoSizeFrame.Num() - 1, Start = 0; Ndx > Start; Ndx--, Start++ )
						echoSizeFrame.SwapElements(Ndx, Start);

					for ( Ndx = 1; Ndx < echoSizeFrame.Num(); Ndx++ )
						SizeFrame.AddItem(echoSizeFrame(Ndx));
					break;
				}
			}
		}

	}

	if ( ColorFrame.Num() )
	{
		// Grab the current frame
		FColorRecord& Dest = ColorFrame(0);

		// Update the counter
		Dest.Time -= C->RenderDelta;

		// Try to indicate arrival as close as possible to the desired time -
		// If the remaining time is less than half of RenderDelta, trigger the OnArrival
		// during this tick, otherwise, wait until next tick (we'll assume RenderDelta
		// is a constant, even though this is not the case)
		UBOOL bPulsing = Dest.Time >= C->RenderDelta * 0.5;
		if ( bPulsing )	// If we're not there yet
		{
			if ( ComponentColor != Dest.targetColor )
			{
				FLOAT FrameDuration = Max<FLOAT>(0.0001f, Dest.Time * Dest.Multiplier);
				ComponentColor.Lerp(Dest.targetColor, (C->RenderDelta/FrameDuration));
			}
		}

		else
		{
			ComponentColor = Dest.targetColor;

			// Send notification that we've arrived at this keyframe
			if ( DELEGATE_IS_SET(OnArrival) )
				delegateOnArrival(this,ANIMATION_Color);

			ColorFrame.Remove(0);

			INT Ndx, Start;
			if ( ColorFrame.Num() < 1 ) 
			{
				switch(LoopAnimation[ANIMATION_Color]) 
				{
				case ANIMATIONLOOP_None:
					eventEndAnimation(this,ANIMATION_Color);
					break;


				case ANIMATIONLOOP_Continuous:
					// Replay the animation loop by copying all frames from the storage array
					// back into the main array
					for (Ndx = 0; Ndx < echoColorFrame.Num(); ++Ndx) 
					{
						ColorFrame.AddItem(echoColorFrame(Ndx));
					}
					break;

				case ANIMATIONLOOP_Bounce:
					// Reverse the order of the storage array before copying it into the main array
					for ( Ndx = echoColorFrame.Num() - 1, Start = 0; Ndx > Start; Ndx--, Start++ )
						echoColorFrame.SwapElements(Ndx, Start);

					// Replay the animation loop by copying all frames from the storage array
					// back into the main array

					// Notice that in this type of animation loop, we do not want to add
					// the first element, so that the animation flows smoothly back and forth
					for ( Ndx = 1; Ndx < echoColorFrame.Num(); Ndx++ )
						ColorFrame.AddItem(echoColorFrame(Ndx));

					break;
				}
			}
		}
	}

	UpdateBounds();

	unguardobj;
}

// Draw should always be subclassed in the individual components, and they should ALWAYS call their super first to
// obtain their state.

void UGUIComponent::Draw(UCanvas* Canvas)
{
	guard(GUIComponent::Draw);

	if ( !bVisible || INVALIDRENDER )
		return;

	Canvas->CurX = ActualLeft();
	Canvas->CurY = ActualTop();

	if (Style!=NULL)
	{
		UBOOL bUseComponentColor = bOverrideStyle || ColorFrame.Num() > 0;

		Canvas->Color = bUseComponentColor ? ComponentColor : Style->FontColors[MenuState];
		Canvas->Style = Style->RStyles[MenuState];

		if (Style->Fonts[MenuState]!=NULL)
			Canvas->Font  = Style->Fonts[MenuState]->eventGetFont(Controller->ResX);
	}

	unguardobj;
}

void UGUIComponent::execNeedsRender( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIComponent::execNeedsRender);

	P_GET_UBOOL_OPTX(bPropagate,0);
	P_FINISH;

	*(UBOOL*)Result = NeedsRender(bPropagate);

	unguardexec;
}

void UGUIComponent::execSpecialHit( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIComponent::execSpecialHit);

	P_GET_UBOOL_OPTX(bForce,0);
	P_FINISH;

	*(UBOOL*)Result = SpecialHit(bForce);
	unguardexec;
}

void UGUIComponent::execGetMenuPath( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIComponent::execGetMenuPath);

	P_FINISH;

	*(FString*)Result = GetMenuPath();

	unguardexec;
}

TCHAR* UGUIComponent::GetMenuPath( TCHAR* Str ) const
{
	guard(UGUIComponent::GetMenuPath);

	TCHAR* Result = Str ? Str : appStaticString1024();
	*Result = 0;
	if( MenuOwner )
	{
		MenuOwner->GetMenuPath( Result );
		appStrncat(Result, TEXT("->"), 1024);
	}
	appStrncat( Result, GetName(), 1024 );

	return Result;
	unguardobj;
}

void UGUIComponent::ShowLinks() const
{
	guard(UGUIComponent::ShowLinks);

	debugf(TEXT("Links for %s"), GetName());
	for ( INT i = 0; i < 4; i++ )
	{
		debugf(TEXT("  %i)  %s"), i, Links[i] ? Links[i]->GetName() : TEXT("None"));
	}

	if ( MenuOwner )
		MenuOwner->ShowLinks();

	unguardobj;
}

UBOOL UGUIComponent::NeedsRender( UBOOL bPropagate ) const
{
	guard(UGUIComponent::NeedsRender);

	DECLAREBOUNDS;

	static const FLOAT FloatTolerance = 0.001f;
	FLOAT XL = Bounds[2] - Bounds[0], YL = Bounds[3] - Bounds[1];
	return
		Abs(AL - Bounds[0]) > FloatTolerance ||
		Abs(AT - Bounds[1]) > FloatTolerance ||
		Abs(AW - XL) > FloatTolerance ||
		Abs(AH - YL) > FloatTolerance;

	unguardobj;
}

void UGUIComponent::UpdateBounds(UBOOL bPropagate)
{

	guard(GUIComponent::UpdateBounds);

	// if Scaled, calculate the bounds

	Bounds[0] = ActualLeft();
	Bounds[1] = ActualTop();
	Bounds[2] = ActualLeft()+ActualWidth();
	Bounds[3] = ActualTop()+ActualHeight();

	if (Style!=NULL)
	{
		ClientBounds[0] = Bounds[0]+Style->BorderOffsets[0];
		ClientBounds[1] = Bounds[1]+Style->BorderOffsets[1];
		ClientBounds[2] = Bounds[2]-Style->BorderOffsets[2];
		ClientBounds[3] = Bounds[3]-Style->BorderOffsets[3];
	}
	else
	{
		ClientBounds[0] = Bounds[0];
		ClientBounds[1] = Bounds[1];
		ClientBounds[2] = Bounds[2];
		ClientBounds[3] = Bounds[3];
	}

	bPositioned = 1;
	unguard;
}


// PerformHitTest checks the current Mouse X/Y position to see if it's touching this
// component.

UBOOL UGUIComponent::PerformHitTest(INT MouseX, INT MouseY)
{
	guard(GUIComponent::PerformHitTest);

	if ( MenuState == MSAT_Disabled || !bVisible || bNeverFocus )
		return 0;

	if (DELEGATE_IS_SET(OnHitTest) )
		delegateOnHitTest(MouseX,MouseY);

	// Perform simple box collision
	return WithinBounds(MouseX,MouseY);

	unguard;

}

/* scion ===============================================
 * UGUIComponent::execClientXControllerEvent
 * Author: brian
 *
 * The client side of the XControllerEvent chain. This
 * is where the native XController event is finally called.
 * The input is twice cooked: Once the raw input is cooked
 * into the iCode (second script parameter). Then everything
 * is passed to the server where the player controller
 * producing the input is cooked into the player number.
 *
 * input:	native script input
 *			  byte iPlayer - player number
 *			  byte iCode - Xbox controller action code
 *
 * return:	bool
 *
 * modified by:	brian
 * =====================================================
 */
void UGUIComponent::execClientXControllerEvent(FFrame& Stack, RESULT_DECL )
{
	guard(GUIComponent::execClientControllerEvent);
	P_GET_BYTE(iPlayer);
	P_GET_BYTE(iCode);
	P_FINISH;

	*(UBOOL*)Result = XControllerEvent(iPlayer, eXControllerCodes(iCode));

	unguard;
} // execClientXControllerEvent

void UGUIComponent::execActualWidth(FFrame& Stack, RESULT_DECL )
{
	guard(GUIComponent::execActualWidth);
	P_GET_FLOAT_OPTX(Val,BIGFLOAT);
	P_FINISH;

	*(FLOAT*)Result = ActualWidth(Val);

	unguardexec;
}

void UGUIComponent::execActualHeight(FFrame& Stack, RESULT_DECL )
{
	guard(GUIComponent::execActualHeight);
	P_GET_FLOAT_OPTX(Val,BIGFLOAT);
	P_FINISH;

	*(FLOAT*)Result = ActualHeight(Val);

	unguardexec;
}

void UGUIComponent::execActualLeft(FFrame& Stack, RESULT_DECL )
{
	guard(GUIComponent::execActualLeft);
	P_GET_FLOAT_OPTX(Val,BIGFLOAT);
	P_FINISH;

	*(FLOAT*)Result = ActualLeft(Val);

	unguardexec;
}

void UGUIComponent::execActualTop(FFrame& Stack, RESULT_DECL )
{
	guard(GUIComponent::execActualTop);
	P_GET_FLOAT_OPTX(Val,BIGFLOAT);
	P_FINISH;

	*(FLOAT*)Result = ActualTop(Val);

	unguardexec;
}

void UGUIComponent::execRelativeLeft( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIComponent::execRelativeLeft);

	P_GET_FLOAT_OPTX(RealLeft,BIGFLOAT);
	P_FINISH;

	*(FLOAT*)Result = RelativeLeft(RealLeft);
	unguardexec;
}

void UGUIComponent::execRelativeTop( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIComponent::execRelativeTop);
	P_GET_FLOAT_OPTX(RealTop,BIGFLOAT);
	P_FINISH;

	*(FLOAT*)Result = RelativeTop(RealTop);
	unguardexec;
}

void UGUIComponent::execRelativeWidth( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIComponent::execRelativeWidth);
	P_GET_FLOAT_OPTX(RealWidth,BIGFLOAT);
	P_FINISH;

	*(FLOAT*)Result = RelativeWidth(RealWidth);

	unguardexec;
}

void UGUIComponent::execRelativeHeight( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIComponent::execRelativeHeight);
	P_GET_FLOAT_OPTX(RealHeight,BIGFLOAT);
	P_FINISH;

	*(FLOAT*)Result = RelativeHeight(RealHeight);
	unguardexec;
}

// Returns the actual width (including scaling) of a component
FLOAT UGUIComponent::ActualWidth( FLOAT Val ) const
{
	guard(UGUIComponent::ActualWidth);

	if ( Val == BIGFLOAT )
		Val = WinWidth;

	if ( Abs(Val) < 2.f && bConvertScaleX )
	{
		if ( MenuOwner && (ScalingType == SCALE_All || ScalingType == SCALE_X) )
			return MenuOwner->ActualWidth() * Val;
		else
			return Val * (Controller ? Controller->ResX : 640.f);
	}
	else
		return Val;

	unguardobj;
}


// Returns the actual height (including scaling) of a component
FLOAT UGUIComponent::ActualHeight( FLOAT Val ) const
{
	guard(UGUIComponent::ActualHeight);

	if ( Val == BIGFLOAT )
		Val = WinHeight;

	if ( Abs(Val) < 2.f && bConvertScaleY )
	{
		if ( MenuOwner && (ScalingType == SCALE_All || ScalingType == SCALE_Y) )
			return MenuOwner->ActualHeight() * Val;
		else
			return Val * (Controller ? Controller->ResY : 480.f);
	}
	else
		return Val;

	unguardobj;
}


// Returns the actual left (including scaling) of a component
FLOAT UGUIComponent::ActualLeft( FLOAT Val ) const
{
	guard(UGUIComponent::ActualLeft);

	if ( Val == BIGFLOAT )
		Val = WinLeft;

	if ( Abs(Val) < 2.f && bConvertBoundX )
	{
		if ( MenuOwner && (BoundingType == SCALE_All || BoundingType == SCALE_X)  )
			return MenuOwner->ActualLeft() + (MenuOwner->ActualWidth() * Val);

		return Val * (Controller ? Controller->ResX : 640.f);
	}
	else
	{
		if ( (BoundingType == SCALE_All || BoundingType == SCALE_X) && (MenuOwner!=NULL) )
			return MenuOwner->ActualLeft() + Val;

		return Val;
	}

	unguardobj;
}


// Returns the actual top (including scaling) of a component
FLOAT UGUIComponent::ActualTop( FLOAT Val ) const
{
	guard(UGUIComponent::ActualTop);

	if ( Val == BIGFLOAT )
		Val = WinTop;

	if ( Abs(Val) < 2.f && bConvertBoundY )
	{
		if ( MenuOwner && (BoundingType == SCALE_All || BoundingType == SCALE_Y) )
		{
			FLOAT MOT = MenuOwner->ActualTop();
			FLOAT MOH = MenuOwner->ActualHeight();

			return MOT + ( MOH * Val );
		}

		return Val * (Controller ? Controller->ResY : 480.f);
	}
	else
	{
		if ( MenuOwner && (BoundingType == SCALE_All || BoundingType == SCALE_Y) )
			return MenuOwner->ActualTop() + Val;

		return Val;
	}

	unguardobj;
}

FLOAT UGUIComponent::RelativeLeft( FLOAT Value ) const
{
	guard(UGUIComponent::RelativeLeft);

	if ( Value == BIGFLOAT )
		Value = WinLeft;

	if ( Abs(Value) > 2.0f || !bConvertBoundX )
	{
		FLOAT MOL = 0.f, MOW = (Controller ? Controller->ResX : 640.f);
		if ( MenuOwner && (BoundingType == SCALE_All || BoundingType == SCALE_X))
		{
			MOL = MenuOwner->ActualLeft();
			MOW = MenuOwner->ActualWidth();
		}

		Value -= MOL;
		if ( bConvertBoundX && MOW )
			Value /= MOW;
	}

	return Value;

	unguardobj;
}

FLOAT UGUIComponent::RelativeTop( FLOAT Value ) const
{
	guard(UGUIComponent::RelativeTop);

	if ( Value == BIGFLOAT )
		Value = WinTop;

	if ( Abs(Value) > 2.0f || !bConvertBoundY )
	{
		FLOAT MOT = 0.f, MOH = (Controller ? Controller->ResY : 480.f);
		if ( MenuOwner && (BoundingType == SCALE_All || BoundingType == SCALE_Y) )
		{
			MOT = MenuOwner->ActualTop();
			MOH = MenuOwner->ActualHeight();
		}

		Value -= MOT;
		if ( bConvertBoundY && MOH )
			Value /= MOH;
	}


	return Value;

	unguardobj;
}

FLOAT UGUIComponent::RelativeWidth( FLOAT Value ) const
{
	guard(UGUIComponent::RelativeWidth);

	if ( Value == BIGFLOAT )
		Value = WinWidth;

	if ( Abs(Value) > 2.0f || !bConvertScaleX )
	{
		FLOAT MOW = (Controller ? Controller->ResX : 640.f);
		if ( MenuOwner && (ScalingType == SCALE_All || ScalingType == SCALE_X) )
			MOW = MenuOwner->ActualWidth();

		if ( bConvertScaleX && MOW )
			Value /= MOW;
	}


	return Value;

	unguardobj;
}


FLOAT UGUIComponent::RelativeHeight( FLOAT Value ) const
{
	guard(UGUIComponent::RelativeHeight);

	if ( Value == BIGFLOAT )
		Value = WinHeight;

	if ( Abs(Value) > 2.0f || !bConvertScaleX )
	{
		FLOAT MOH = (Controller ? Controller->ResY : 480.f);
		if ( MenuOwner && (ScalingType == SCALE_All || ScalingType == SCALE_Y))
			MOH = MenuOwner->ActualHeight();

		if ( bConvertScaleY && MOH )
			Value /= MOH;
	}

	return Value;
	unguardobj;
}


// - SaveCanvasState and RestoreCanvasState are called before and after the actual component is rendered to make sure the
//   canvas is kept in the proper state for any other action.

void UGUIComponent::SaveCanvasState(UCanvas* Canvas)
{
	if (SavedCanvasStateDepth++ > 0) return;
	SaveX	  = Canvas->CurX;
	SaveY	  = Canvas->CurY;
	SaveColor = Canvas->Color;
	SaveStyle = Canvas->Style;
	SaveFont  = Canvas->Font;
}

void UGUIComponent::RestoreCanvasState(UCanvas* Canvas)
{
	if (--SavedCanvasStateDepth > 0) return;
	Canvas->CurX  = SaveX;
	Canvas->CurY  = SaveY;
	Canvas->Color = SaveColor;
	Canvas->Style = SaveStyle;
	Canvas->Font  = SaveFont;
	SaveFont = NULL;
}

// It's useful for each component to be able to act upon mouse/presses and releases

UBOOL UGUIComponent::MousePressed(UBOOL IsRepeat)
{
	guard(GUIComponet::MousePressed);

	if ( INVALIDMENU )
		return false;

	UGUIController* C = Controller;

	// Attempt to SetFocus to this control
	if ( MenuState == MSAT_Disabled || !PerformHitTest(C->MouseX, C->MouseY) )
		return false;

	eventSetFocus(NULL);
	eventMenuStateChange(MSAT_Pressed);

	if ( DELEGATE_IS_SET(OnMousePressed) )
		delegateOnMousePressed(this,IsRepeat);

	// If we never focus we want this to act as a click
	if (bRepeatClick)
	{
		eventPlayAction(OnClickSound);

		if ( DELEGATE_IS_SET(OnClick) )
			delegateOnClick(this);
	}

	return true;

	unguardobj;
}

UBOOL UGUIComponent::MouseReleased()
{
	guard(GUIComponet::MouseReleased);

	if ( INVALIDMENU )
		return false;

	UGUIController* C = Controller;
	if (MenuState==MSAT_Pressed && bVisible)
	{
		if ( (MenuOwner!=NULL) && (Cast<UGUIMultiComponent>(MenuOwner)!=NULL) )
		{
			if (MenuOwner->eventGetFocusedControl() == this)
				eventMenuStateChange(MSAT_Focused);
			else
				eventMenuStateChange(MSAT_Blurry);
		}
		else
			eventMenuStateChange(MSAT_Blurry);
	}

	if ( !PerformHitTest(C->MouseX, C->MouseY) )
		return false;

	if (!bRepeatClick)
	{
		// Play sound _before_ executing action
		eventPlayAction(OnClickSound);

		// Check for Double Click

		if ( (DELEGATE_IS_SET(OnDblClick) ) && (appSeconds() <C->LastClickTime+C->DblClickWindow) && (C->LastClickX == C->MouseX && C->LastClickY == C->MouseY) )
		{
			delegateOnDblClick(this);
			if (C)
			{
				C->LastClickX=-1;
				C->LastClickY=-1;
				C->LastClickTime=0.0f;
			}
		}
		else
		{
			delegateOnClick(this);
			if (C)
			{
				C->LastClickX = C->MouseX;
				C->LastClickY = C->MouseY;
				C->LastClickTime = appSeconds();
			}
		}
	}

	return true;
	unguardobj;
}

// By Default, when the mouse is moved, components need to be searched to see if they are effected.  If this function returns true, then
// This component has used the input so go no further.  It should always be assumed that

UBOOL UGUIComponent::MouseMove(INT XDelta, INT YDelta)
{
	guard(GUIComponent::MouseMove)

	if ( (MenuState==MSAT_Pressed) && (bCaptureMouse) )
		return delegateOnCapturedMouseMove(XDelta, YDelta);

	return false;

	unguardobj;
}

// When moving over things that have not captured the mouse, we might still want to know (ie changing cursor shape)
UBOOL UGUIComponent::MouseHover()
{
	guard(GUIComponent::MouseHover)

	return false;

	unguardobj;
}


// Input events should be handled by the subclasses

UBOOL UGUIComponent::NativeKeyEvent(BYTE& iKey, BYTE& State, FLOAT Delta )
{
	guard(GUIComponent::NativeKeyEvent);

	if ( INVALIDMENU )
		return false;

	if ( !bCaptureTabs && iKey == IK_Tab && (State == IST_Press || State == IST_Hold) )
	{
		if ( Controller->CtrlPressed )
		{
			if ( Controller->ShiftPressed )
			{
				if ( eventPrevPage() )
					return true;
			}
			else if ( eventNextPage() )
				return true;
		}
		else
		{
			if ( Controller->ShiftPressed )
			{
				if ( eventPrevControl(NULL) )
					return true;
			}
			else if ( eventNextControl(NULL) )
				return true;
		}
	}
	else if ( DELEGATE_IS_SET(OnKeyEvent) )
		return delegateOnKeyEvent(iKey,State,Delta);

	return false;

	unguardobj;
}

UBOOL UGUIComponent::NativeKeyType(BYTE& iKey, TCHAR Unicode )
{
	guard(GUIComponent::NativeKeyType);

	if ( INVALIDMENU || !eventCanAcceptInput() )
		return false;

	if ( DELEGATE_IS_SET(OnKeyType) )
        return delegateOnKeyType(iKey,FString::Printf(TEXT("%c"), Unicode));

	return false;
	unguardobj;
}

UBOOL UGUIComponent::XControllerEvent(BYTE Id, eXControllerCodes iCode)
{
	guard(GUIComponent::XControllerEvent);

	if ( INVALIDMENU )
		return false;

	if (MenuState!=MSAT_Focused)
		return false;

	if ( !(ControllerMask & (1 << Id)) )
	{
		debugf(NAME_DevUI, TEXT("Received XControllerEvent from invalid Id (%s): ControllerMask:%u  Input Id:%u"), GetMenuPath(), ControllerMask, Id);
		return false;
	}

	if (DELEGATE_IS_SET(OnXControllerEvent) )
	{
		if ( delegateOnXControllerEvent(this,Id,iCode) )
		{
			SHOWCODEHANDLER(this,Id,iCode);
			return true;
		}

		return false;
	}

	return false;

	unguardobj;
}

UBOOL UGUIComponent::RawXController(BYTE Id, BYTE& iKey, BYTE& State, FLOAT Axis)
{
	guard(GUIComponent::RawXController);

	if ( DELEGATE_IS_SET(OnRawXController) )
		return delegateOnRawXController(this,Id, iKey, State, Axis);
	else if ( PageOwner && PageOwner != this )
		return PageOwner->RawXController(Id, iKey, State, Axis);
	else
		return false;

	unguardobj;
}

void UGUIComponent::CloneDims(UGUIComponent* From)
{
	guard(GUIComponent::CloneDims);

	if ( From )
		SetRelativeDims(From->ActualLeft(), From->ActualHeight(), From->ActualLeft(), From->ActualTop());

	unguardobj;
}

void  UGUIComponent::SetDims(FLOAT Width, FLOAT Height, FLOAT Left, FLOAT Top)
{
	guard(GUIComponent::SetDims);

	WinWidth = Width;
	WinHeight = Height;
	WinLeft = Left;
	WinTop = Top;

	unguardobj;
}

void UGUIComponent::SetRelativeDims( FLOAT Width, FLOAT Height, FLOAT Left, FLOAT Top )
{
	guard(UGUIComponent::SetAdjustedDims);

	if ( !Controller )
		return;

	Width = RelativeWidth(Width);
	Height = RelativeHeight(Height);
	Left = RelativeLeft(Left);
	Top = RelativeTop(Top);

	SetDims(Width, Height, Left, Top);

	unguardobj;
}

UBOOL UGUIComponent::SpecialHit( UBOOL bForce )
{
	guard(UGUIComponent::SpecialHit);

	if ( INVALIDMENU )
		return false;

	UGUIController* C = Controller;

	if ( bForce || WithinBounds(C->MouseX, C->MouseY) )
	{
		if (C->MoveControl == this)
			C->MoveControl = NULL;
		else if (C->MoveControl == NULL || (!bForce && !C->MoveControl->WithinBounds(C->MouseX, C->MouseY)))
		{
			C->SetMoveControl(this);
			return true;
		}

	}

	return false;
	unguardobj;
}

UGUIComponent* UGUIComponent::UnderCursor(FLOAT MouseX, FLOAT MouseY)
{
	guard(UGUIComponent::UnderCursor);

	if ((bVisible) && (MenuState!=MSAT_Disabled) && ( PerformHitTest(MouseX,MouseY) ))
		return this;

	return NULL;
	unguardobj;
}

void UGUIComponent::NativeInvalidate(UGUIComponent* Who)
{
	delegateOnInvalidate(Who);
}

void UGUIComponent::execSetTimer(FFrame& Stack, RESULT_DECL )
{
	guard(UGUIComponent::execSetTimer);
	// Verify that the control is really on the good page
	P_GET_FLOAT(Interval);
	P_GET_UBOOL_OPTX(bRepeat, false);
	P_FINISH;

	// Find Component's Page
	if ( PageOwner == NULL )
	{
		PageOwner = eventGetOwnerPage();
		if (PageOwner == NULL)
			return;
	}

	if (TimerIndex < 0 && Interval > 0.0f)
		TimerIndex = PageOwner->Timers.AddItem(this);

	TimerInterval = Interval;
	TimerCountdown = Interval;
	bTimerRepeat = false;
	if (TimerInterval > 0.0f)
		bTimerRepeat = bRepeat;

	unguardexec;
}

void UGUIComponent::execKillTimer(FFrame& Stack, RESULT_DECL )
{
	guard(UGUIComponent::execKillTimer);
	P_FINISH;

	TimerInterval=0.0;
	TimerCountdown=0.0;
	bTimerRepeat=false;

	unguardexec;
}

#ifdef _DEBUG

struct GUIReferencedObject
{
	UProperty* Prop;
	UObject* Obj;

	GUIReferencedObject( UProperty* InProp = NULL, UObject* InObj = NULL )
		: Prop(InProp), Obj(InObj)
	{ }
};

static void CheckRefs( BYTE* Data, UStruct* Base, TArray<GUIReferencedObject>& Refs )
{
	guard(CheckRefs);

	if ( Data == NULL )
		return;

	UObject* Ptr = NULL;
	for ( UProperty* Prop = Base->RefLink; Prop; Prop = Prop->NextRef )
	{
		if ( Prop->GetOwnerClass() == UObject::StaticClass() )
			continue;

		if ( Cast<UObjectProperty>(Prop,CLASS_IsAUObjectProperty) )
		{
			for ( INT i = 0; i < Prop->ArrayDim; i++ )
			{
				Ptr = *(UObject**)(Data + Prop->Offset + i * Prop->ElementSize);
				if ( Ptr )
				{
					UBOOL bFound = false;
					for ( INT x = 0; x < Refs.Num(); x++ )
					{
						if ( Refs(x).Obj == Ptr )
						{
							bFound = true;
							break;
						}
					}
					if ( !bFound )
						new(Refs) GUIReferencedObject(Prop, Ptr);
				}
			}
		}
		else
		{
			UArrayProperty* ArrayProp = Cast<UArrayProperty>(Prop);
			if ( ArrayProp )
			{
				if ( Cast<UObjectProperty>(ArrayProp->Inner,CLASS_IsAUObjectProperty) )
				{
					for ( INT i = 0; i < ArrayProp->ArrayDim; i++ )
					{
						FArray* Array = (FArray*)(Data + Prop->Offset + i * Prop->ElementSize);
						if ( Array )
						{
							for ( INT j = 0; j < Array->Num(); j++ )
							{
								Ptr = (UObject*)(((UObject**)Array->GetData())[j]);
								if ( Ptr )
								{
									UBOOL bFound = false;
									for ( INT x = 0; x < Refs.Num(); x++ )
									{
										if ( Refs(x).Obj == Ptr )
										{
											bFound = true;
											break;
										}
									}
									if ( !bFound )
										new(Refs) GUIReferencedObject(Prop, Ptr);
								}
							}
						}
					}
				}
			}
			else
			{
				UStructProperty* StructProp = Cast<UStructProperty>(Prop);
				if ( StructProp )
				{
					for ( INT i = 0; i < StructProp->ArrayDim; i++ )
						CheckRefs((Data + StructProp->Offset + i * StructProp->ElementSize), StructProp->Struct, Refs);
				}
			}
			UDelegateProperty* DelegateProp = Cast<UDelegateProperty>(Prop);
			if ( DelegateProp )
			{
				for ( INT i = 0; i < DelegateProp->ArrayDim; i++ )
				{
					FScriptDelegate& Del = *(FScriptDelegate*)(Data + DelegateProp->Offset + i * DelegateProp->ElementSize);
					UObject* Ptr = Del.Object;
					if ( Ptr )
					{
						UBOOL bFound = false;
						for ( INT x = 0; x < Refs.Num(); x++ )
						{
							if ( Refs(x).Obj == Ptr )
							{
								bFound = true;
								break;
							}
						}
						if ( !bFound )
						{
							// only track delegate references if the object is invalid
							if ( !IsValidObject(Ptr) )
								new(Refs) GUIReferencedObject(Prop, Ptr);
						}
					}
				}
			}
		}
	}

	unguard;
}

static void ShowRefs( UObject* Comp )
{
	TArray<GUIReferencedObject> Refs;

	CheckRefs((BYTE*)Comp,Comp->GetClass(),Refs);

	if ( Refs.Num() > 0 )
		debugf(NAME_DevUI, TEXT("Deleting object with refs: %s"), Cast<UGUIComponent>(Comp) ? Cast<UGUIComponent>(Comp)->GetMenuPath() : Comp->GetName());
	for ( INT i = 0; i < Refs.Num(); i++ )
	{
		GUIReferencedObject& Ref = Refs(i);
		debugf(NAME_DevUI, TEXT("    %s:  %s"), Ref.Prop->GetPathName(Ref.Prop->GetOwnerClass()), !IsValidObject(Ref.Obj) ? TEXT("Invalid") : Ref.Obj->GetName());
	}
}

#endif
// Find all objects referenced by this object
static void FindReferencedObjects( TArray<UObject*>& InnerObjects, void* Data, UStruct* Base )
{
	guard(ParseInnerObjects);

	check(Base);
	check(Data);

	UObject* Ptr = NULL;
	
	for ( UProperty* Prop = Base->RefLink; Prop; Prop = Prop->NextRef )
	{
		if ( Cast<UObjectProperty>(Prop,CLASS_IsAUObjectProperty) )
		{
			for ( INT i = 0; i < Prop->ArrayDim; i++ )
			{
				Ptr = *(UObject**)((BYTE*)Data + Prop->Offset + i * Prop->ElementSize);
				if ( Ptr && IsValidObject(Ptr) && Ptr->GetOuter() )
					InnerObjects.AddUniqueItem(Ptr);
			}
		}
		else
		{
			UArrayProperty* ArrayProp = Cast<UArrayProperty>(Prop);
			if ( ArrayProp )
			{
				if ( Cast<UObjectProperty>(ArrayProp->Inner,CLASS_IsAUObjectProperty) )
				{
					for ( INT i = 0; i < ArrayProp->ArrayDim; i++ )
					{
						FArray* Array = (FArray*)((BYTE*)Data + Prop->Offset + i * Prop->ElementSize);
						if ( Array )
						{
							for ( INT j = 0; j < Array->Num(); j++ )
							{
								Ptr = (((UObject**)Array->GetData())[j]);
								if ( Ptr && IsValidObject(Ptr) && Ptr->GetOuter() )
									InnerObjects.AddUniqueItem(Ptr);
							}
						}
					}
				}
			}
			else
			{
				UStructProperty* StructProp = Cast<UStructProperty>(Prop);
				if ( StructProp )
				{
					for ( INT i = 0; i < StructProp->ArrayDim; i++ )
						FindReferencedObjects(InnerObjects, ((BYTE*)Data + StructProp->Offset + i * StructProp->ElementSize), StructProp->Struct);
				}
			}
			UDelegateProperty* DelegateProp = Cast<UDelegateProperty>(Prop);
			if ( DelegateProp )
			{
				for ( INT i = 0; i < DelegateProp->ArrayDim; i++ )
				{
					FScriptDelegate& Del = *(FScriptDelegate*)((BYTE*)Data + DelegateProp->Offset + i * DelegateProp->ElementSize);
					UObject* Ptr = Del.Object;
					if ( Ptr && IsValidObject(Ptr) && Ptr->GetOuter() )
						InnerObjects.AddUniqueItem(Ptr);
				}
			}
		}
	}

	unguard;
}

void UGUIComponent::execGetReferencedObjects( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIComponent::execGetReferencedObjects);

	P_GET_TARRAY_REF(InnerObjects,UObject*);
	P_FINISH;

	InnerObjects->Empty();
	FindReferencedObjects(*InnerObjects, this, GetClass());
	unguardexec;
}

// Copied from UObject::execdelete, modifed to iterate through object properties
// and clear any that are set to the object about to be deleted
void UGUIComponent::execSafeDelete( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIComponent::execSafeDelete);

	P_GET_OBJECT(UObject,Obj);
	P_FINISH;

	// Let bad callers know what's going on
	checkSlow(Obj != NULL && Obj->IsValid());
	checkSlow(Obj->GetClass()->ClassFlags & CLASS_Poolable);

	// Search our object properties for any that are pointing to this object
	// clear their value prior to deleting the object
	GetClass()->CleanupDestroyed((BYTE*)this, Obj);

#ifdef _DEBUG
	ShowRefs(Obj);
#endif
#if 0
	UObject::CheckDanglingRefs(Obj);
#endif

	// Let it clean up correctly (call destroy, etc)
	Obj->ConditionalDestroy();
	// Go ahead and delete the object, which if it is pooled will result in it
	// returning the available pool
	delete Obj;

	unguard;
}


// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIMultiComponent
// =======================================================================================================================================================
// =======================================================================================================================================================

void UGUIMultiComponent::execInitializeControls(FFrame& Stack, RESULT_DECL)
{
	guard(UGUIMultiComponent::execInitializeControls);
	P_FINISH;

	InitializeControls();

	unguardexec;
}

void UGUIMultiComponent::InitializeControls()
{
	guard(UGUIMultiComponent::InitializeControls);

	if (Controls.Num()>0)		// Controls array already initialized
		return;

	UClass* Cls = this->GetClass();
	UGUIComponent* Ptr = NULL;

	//epic moelfke: use PropertyLink instead of a slower TFieldIterator
	for (UProperty* It = Cls->PropertyLink; It; It = It->PropertyLinkNext)
	{
		if ( It->PropertyFlags & CPF_Automated )
		{
			for ( INT i = 0; i < It->ArrayDim; i++ )
			{
				if ( It->IsA(UArrayProperty::StaticClass()) )
				{
					UProperty* Prop = ((UArrayProperty*)It)->Inner;
					if (Prop->IsA(UObjectProperty::StaticClass()) && ((UObjectProperty*)Prop)->PropertyClass->IsChildOf(UGUIComponent::StaticClass()))
					{
						FArray* Array = (FArray*)((BYTE*)this + It->Offset + i * It->ElementSize);
						{
							if ( Array )
							{
								for ( INT j = 0; j < Array->Num(); j++ )
								{
									Ptr = (UGUIComponent*)( ((UObject**)Array->GetData())[j] );
									if ( Ptr )
									{
										INT k = 0;
										for ( ; k < Controls.Num(); k++ )
											if ( Ptr->RenderWeight <= Controls(k)->RenderWeight )
												break;

										Controls.Insert(k);
										Controls(k) = Ptr;
									}
								}
							}
						}
					}
				}
				else if (It->IsA(UObjectProperty::StaticClass()) && ((UObjectProperty*)It)->PropertyClass->IsChildOf(UGUIComponent::StaticClass()))
				{
					Ptr = *(UGUIComponent**)((BYTE*)this + It->Offset + i * It->ElementSize);
					if ( Ptr )
					{
						INT j = 0;
						for (; j < Controls.Num(); j++)
							if ( Ptr->RenderWeight <= Controls(j)->RenderWeight )
								break;

						Controls.Insert(j);
						Controls(j) = Ptr;
					}
				}
			}
		}
	}

	unguardobj;
}

UBOOL UGUIMultiComponent::NeedsRender( UBOOL bPropagate ) const
{
	guard(UGUIMultiComponent::NeedsRender);

	UBOOL bResult = UGUIComponent::NeedsRender(bPropagate);
	if ( !bResult && bPropagate )
	{
		for ( INT i = 0; i < Controls.Num() && !bResult; i++ )
		{
			if ( Controls(i) )
                bResult = Controls(i)->NeedsRender(bPropagate);
		}
	}

	return bResult;

	unguardobj;
}

UBOOL UGUIMultiComponent::PerformHitTest(INT MouseX, INT MouseY)
{
	guard(GUIMultiComponent::PerformHitTest);

	if ( !bVisible )
		return 0;

	for (INT i=0;i<Controls.Num();i++)
		if ( Controls(i) && Controls(i)->PerformHitTest(MouseX, MouseY) )
			return 1;

	return Super::PerformHitTest(MouseX,MouseY);

	unguardobj;
}

void UGUIMultiComponent::PreDraw(UCanvas* Canvas)
{
	guard(UGUIMultiComponent::PreDraw);

	if ( INVALIDRENDER )
		return;

	Super::PreDraw(Canvas);
	PreDrawControls(Canvas);

	unguardobj;
}

void UGUIMultiComponent::Draw(UCanvas* Canvas)
{
	guard(GUIMultiComponent::Draw);

	if ( !bVisible || INVALIDRENDER )
		return;

	Super::Draw(Canvas);

	FLOAT ForcedAlpha, LastForcedAlpha = Canvas->ForcedAlpha;
	if ( ComponentColor.A != 255 )
	{
		ForcedAlpha = FLOAT(ComponentColor.A) / 255.f;
		Canvas->ForcedAlpha *= ForcedAlpha;
	}

	DrawControls(Canvas);

	Canvas->ForcedAlpha = LastForcedAlpha;

	unguardobj;
}

void UGUIMultiComponent::PreDrawControls( UCanvas* Canvas )
{
	guard(UGUIMultiComponent::PreDrawControls);

	for ( INT i = 0; i < Controls.Num(); i++ )
		PreDrawControl(Canvas, Controls(i));

	unguardobj;
}

void UGUIMultiComponent::PreDrawControl( UCanvas* Canvas, UGUIComponent* Comp )
{
	guard(UGUIMultiComponent::PreDrawControl);

	if ( !Comp || !Canvas )
		return;

	Comp->SaveCanvasState(Canvas);

	if ( OBJ_DELEGATE_IS_SET(Comp,OnPreRender) && Comp->delegateOnPreRender(Canvas,Comp) )
	{
		Comp->RestoreCanvasState(Canvas);
		return;
	}

	Comp->PreDraw(Canvas);

	// delegateOnPreDraw() should return true if it has modified the dimensions of this component
	// Update bounds again so that this frame's Draw uses the correct bounds
	if ( OBJ_DELEGATE_IS_SET(Comp,OnPreDraw) && Comp->delegateOnPreDraw(Canvas) )
		Comp->UpdateBounds(1);

	Comp->RestoreCanvasState(Canvas);

	unguardobj;
}

void UGUIMultiComponent::DrawControls( UCanvas* Canvas )
{
	guard(UGUIMultiComponent::DrawControls);

	TArray<INT> FocusedIndex;

	for ( INT i = 0; i < Controls.Num(); i++ )
	{
		if ( bPopFocusedControl && eventIsFocused(Controls(i)) )
			FocusedIndex.AddItem(i);
		else DrawControl(Canvas, Controls(i));
	}

	for ( INT i = 0; i < FocusedIndex.Num(); i++ )
	{
		if ( Controls.IsValidIndex(FocusedIndex(i)) )
			DrawControl(Canvas, Controls(FocusedIndex(i)));
	}


	unguardobj;
}

void UGUIMultiComponent::DrawControl( UCanvas* Canvas, UGUIComponent* Comp )
{
	guard(UGUIMultiComponent::DrawControl);

	if ( !Canvas || !Comp )
		return;

	Comp->SaveCanvasState(Canvas);

	if ( !OBJ_DELEGATE_IS_SET(Comp,OnDraw) || !Comp->delegateOnDraw(Canvas) )
		Comp->Draw(Canvas);

	if ( OBJ_DELEGATE_IS_SET(Comp,OnRender) )
		Comp->delegateOnRender(Canvas);

	Comp->RestoreCanvasState(Canvas);
	Comp->bRendered = 1;

	unguardobj;
}

void UGUIMultiComponent::UpdateBounds( UBOOL bPropagate )
{
	guard(UGUIMultiComponent::UpdateBounds);

	Super::UpdateBounds(bPropagate);
	if ( bPropagate )
	{
		for ( INT i = 0; i < Controls.Num(); i++ )
		{
			if ( Controls(i) )
				Controls(i)->UpdateBounds(bPropagate);
		}
	}

	unguardobj;
}

UBOOL UGUIMultiComponent::NativeKeyEvent(BYTE& iKey, BYTE& State, FLOAT Delta )
{
	guard(UGUIMultiComponent::NativeKeyEvent);

	UGUIComponent* Focused = eventGetFocusedControl();
	if ( (Focused!=NULL) && (Focused->NativeKeyEvent(iKey, State, Delta)) )
		return true;

	if (MenuState==MSAT_Focused)
		return Super::NativeKeyEvent(iKey, State, Delta);

	return false;

	unguardobj;
}

UBOOL UGUIMultiComponent::NativeKeyType(BYTE& iKey, TCHAR Unicode )
{

	guard(UGUIMultiComponent::NativeKeyType);

	UGUIComponent* Focused = eventGetFocusedControl();
	if ( (Focused!=NULL) && (Focused->NativeKeyType(iKey, Unicode)) )
		return true;

	if (MenuState==MSAT_Focused)
		return Super::NativeKeyType(iKey, Unicode);

	return false;

	unguardobj;
}

UBOOL UGUIMultiComponent::XControllerEvent(BYTE Id, eXControllerCodes iCode)
{
	guard(GUIMultiComponent::XControllerEvent);

	if ( !(ControllerMask & (1 << Id)) )
		return false;

	if ( Super::XControllerEvent(Id,iCode) )
		return true;

	UGUIComponent* Focused = eventGetFocusedControl();
	if ( Focused && Focused->XControllerEvent(Id,iCode) )
		return true;

	for ( INT i = 0; i < Controls.Num(); i++ )
	{
		if ( Controls(i) && Controls(i) != Focused && Controls(i)->XControllerEvent(Id,iCode) )
			return true;
	}

	// Handle Navigation
	if ( Focused && iCode >= XC_PadUp && iCode <= XC_PadRight )
	{
		BYTE Index = iCode - XC_PadUp;
		for ( UGUIComponent* nextComponent = Focused->Links[Index]; nextComponent;
			nextComponent = nextComponent->Links[Index] )
		{
			if ( nextComponent == Focused )
				break;

			if ( nextComponent->eventCanAcceptFocus(0) )
			{
				nextComponent->eventSetFocus(NULL);
				SHOWCODEHANDLER(this,Id,iCode);
				return true;
			}
		}
	}

	return false;

	unguardobj;
}

UGUIComponent* UGUIMultiComponent::UnderCursor(FLOAT MouseX, FLOAT MouseY)
{

	guard(UGUIMultiComponent::UnderCursor);
	UGUIComponent* tComp(NULL), *Comp, *Focused = eventGetFocusedControl();

	if (Focused!=NULL)
	{
		tComp = Focused->UnderCursor(MouseX,MouseY);
		if (tComp!=NULL)
			return tComp;
	}

	for (INT i=Controls.Num() - 1; i >= 0; i--)
	{
		Comp = Controls(i);
		if ( Comp && Comp != Focused )
		{
			tComp = Comp->UnderCursor(MouseX, MouseY);
			if (tComp)
				return tComp;
		}
	}

	return Super::UnderCursor( MouseX, MouseY );

	unguardobj;
}

void UGUIMultiComponent::NativeInvalidate(UGUIComponent* Who)
{
	Super::NativeInvalidate(Who);

	UGUIComponent* Focused = eventGetFocusedControl();
	if (Focused!=NULL)
		Focused->NativeInvalidate(Who);
}

UBOOL UGUIMultiComponent::SpecialHit( UBOOL bForce )
{

	guard(UGUIMultiComponent::SpecialHit);

	if (Super::SpecialHit(bForce))
		return true;

	UGUIComponent* Comp = NULL;
	for (INT i = Controls.Num() - 1; i >= 0; i-- )
	{
		Comp = Controls(i);
		if ( Comp && Comp->SpecialHit(bForce) )
			return true;
	}

	return false;

	unguardobj;

}

UBOOL UGUIMultiComponent::MousePressed(UBOOL IsRepeat)
{
	guard(UGUIMultiComponent::MousePressed);

	UGUIComponent* Focused = eventGetFocusedControl();
	if ( Focused!=NULL && Focused->MousePressed(IsRepeat) )
		return true;

	UGUIComponent* Comp = NULL;
	for (INT i=Controls.Num()-1;i>=0;i--)
	{
		Comp = Controls(i);
		if ( Comp && Comp != Focused && Comp->MousePressed(IsRepeat) )
			return true;
	}

	if ( bAcceptsInput )
		return Super::MousePressed(IsRepeat);

	return false;;

	unguardobj;

}

UBOOL UGUIMultiComponent::MouseReleased()
{
	guard(UGUIMultiComponent::MousePressed);

	if ( bCaptureMouse )
		return Super::MouseReleased();

	UGUIComponent* Focused = eventGetFocusedControl();
	if ( Focused && Focused->MouseReleased() )
		return true;

	UGUIComponent* Comp;
	for (INT i=Controls.Num()-1;i>=0;i--)
	{
		Comp = Controls(i);
		if ( Comp && Comp != Focused && Comp->MouseReleased() )
			return true;
	}


	if ( bAcceptsInput )
		return Super::MouseReleased();
	return false;

	unguardobj;
}

// Iterators
void UGUI::execAllObjectProperties( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UGUI::execAllObjectProperties);

	P_GET_OBJECT(UClass,PropertyClass);
	P_GET_OBJECT_REF(UProperty,Prop);
	P_GET_OBJECT(UClass,BaseClass);
	P_GET_UBOOL_OPTX(bIgnoreInherited,0);
	P_FINISH;

	BaseClass = BaseClass ? BaseClass : UGUIComponent::StaticClass();
	TFieldIterator<UProperty,CLASS_IsAUProperty> It(BaseClass);


	PRE_ITERATOR;

		*Prop = NULL;
		while ( It && *Prop == NULL )
		{
			if ( PropertyClass == NULL || It->IsA(PropertyClass) )
			{
				if ( bIgnoreInherited && !It->GetOwnerClass()->IsChildOf(BaseClass) )
				{
					++It; continue;
				}

				*Prop = *It;
			}

			++It;
		}

		if ( *Prop == NULL )
		{
			Stack.Code = &Stack.Node->Script(wEndOffset+1);
			break;
		}

	POST_ITERATOR;

	unguardexecSlow;
}

void UGUIComponent::execAllInnerObjects( FFrame& Stack, RESULT_DECL )
{
	guardSlow(UGUIComponent::execAllInnerObjects);

	P_GET_OBJECT_REF(UGUIComponent,Inner);
	P_FINISH;

	TObjectIterator<UObject> It;

	PRE_ITERATOR;

		*Inner = NULL;
		while ( It && *Inner == NULL )
		{
			UGUIComponent* C = Cast<UGUIComponent>(*It);
			if ( C && (C->GetOuter() == this || C->MenuOwner == this) )
				*Inner = C;

			++It;
		}

		if ( *Inner == NULL )
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}

	POST_ITERATOR;

	unguardexecSlow;
}
