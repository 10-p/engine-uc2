/*=============================================================================
	GUILists.cpp: GUI List Components
	Copyright 2004, Epic Games, Inc. All Rights Reserved.

Revision history:
	* Moved from UnGUI.cpp by Ron Prestenback
=============================================================================*/

//jg -- DO NOT BREAK MY PCH SETTINGS!
#include "GUI.h"
#include "GUIPrivate.h"

IMPLEMENT_CLASS(UGUIListBoxBase);
IMPLEMENT_CLASS(UGUIListBox);
IMPLEMENT_CLASS(UGUIMultiColumnListBox);
IMPLEMENT_CLASS(UGUIScrollTextBox);


IMPLEMENT_CLASS(UGUIListBase);
IMPLEMENT_CLASS(UGUIVertList);
IMPLEMENT_CLASS(UGUIHorzList);
IMPLEMENT_CLASS(UGUIMultiColumnList);
IMPLEMENT_CLASS(UGUIMultiColumnListHeader);
IMPLEMENT_CLASS(UGUIScrollText);

static UBOOL IsNumeric( FString& Test )
{
	guardSlow(IsNumeric);

	if ( Test.Len() == 0 )
		return 0;

	TArray<TCHAR> Arr = Test.GetCharArray();
	for ( INT i = 0; i < Arr.Num() - 1; i++ )
	{
		if ( !appIsDigit(Arr(i)) )
			return 0;
	}

	return 1;

	unguardSlow;
}

// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIListBoxBase
// =======================================================================================================================================================
// =======================================================================================================================================================
void UGUIListBoxBase::PreDrawControls(UCanvas* Canvas)
{

	guard(UGUIListBoxBase::PreDrawControls);

	if ( INVALIDRENDER )
		return;

	if ( MyList == NULL )
		return;

	INT Count = MyList->eventItemCount();
	if ( bVisible )
	{
		if ( !bVisibleWhenEmpty )
		{
			if ( MyList->bVisible != Count > 0 )
				MyList->eventSetVisibility(Count > 0);
		}

		UBOOL ScrollBarVisible = !MyList->bShowAllItems && MyList->ItemsPerPage > 0 && Count > MyList->ItemsPerPage;
		if ( bHideScrollBar )
			ScrollBarVisible = 0;

		if ( MyScrollBar && MyScrollBar->bVisible != ScrollBarVisible )
			MyScrollBar->eventSetVisibility(ScrollBarVisible);
	}

	FLOAT Width = Max<FLOAT>(ActualWidth() - (MyScrollBar && MyScrollBar->bVisible ? MyScrollBar->ActualWidth() : 0), 2.f);
	FLOAT Height = ActualHeight();
	FLOAT AL = ActualLeft();
	FLOAT AT = ActualTop();

	if ( MyList->bShowAllItems )
	{
		FLOAT ListHeight = (MyList->ItemHeight * Count) + (MyList->interItemGutter * Max(Count - 1, 0));
		Height = Max<FLOAT>(ActualHeight(),ListHeight);
		SetRelativeDims(Width, Height, AL, AT);
	}

	MyList->SetDims(Width, Height, AL, AT);
	if (MyScrollBar)
		MyScrollBar->SetDims( MyScrollBar->ActualWidth(), Height, AL + MyList->ActualWidth(), AT );

	Super::PreDrawControls(Canvas);

	unguardobj;
}

// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIMultiColumnListBox
// =======================================================================================================================================================
// =======================================================================================================================================================

void UGUIMultiColumnListBox::PreDrawControls(UCanvas* Canvas)
{
	guard(UGUIMultiColumnListBox::PreDrawControls);

	if (INVALIDRENDER)
		return;

	if (MyScrollBar==NULL || Header==NULL || MyList==NULL)
		return;

	INT Count = MyList->eventItemCount();
	if ( bVisible )
	{
		if ( !bVisibleWhenEmpty )
		{
			if ( MyList->bVisible != Count )
				MyList->eventSetVisibility(Count > 0);
		}

		if ( MyScrollBar->bVisible != (MyList->ItemsPerPage > 0 && Count > MyList->ItemsPerPage) )
			MyScrollBar->eventSetVisibility(MyList->ItemsPerPage > 0 && Count > MyList->ItemsPerPage);
	}

	UGUIComponent* Comp = NULL;
	for ( INT i = 0; i < Controls.Num(); i++ )
	{
		Comp = Controls(i);
		if ( Comp != MyList && Comp != MyScrollBar && Comp != Header )
			PreDrawControl(Canvas, Comp);
	}

	DECLAREBOUNDS;
	FLOAT HeaderSize = Header->ActualHeight();

	if ( Header->bVisible != AH > HeaderSize )
		Header->eventSetVisibility( AH > HeaderSize );

	Header->SetDims( AW, HeaderSize, AL, AT );
	PreDrawControl(Canvas, Header);

	if ( !Header->bVisible )
		HeaderSize = 0.f;

	FLOAT Height = AH, ScrollSize = MyScrollBar->bVisible ? HeaderSize * 0.75 : 0;
	if ( MyList->bShowAllItems )
	{
		FLOAT ListHeight = (MyList->ItemHeight * Count) + (MyList->interItemGutter * Max(Count - 1, 0));
		Height = Max<FLOAT>( AH, ListHeight + HeaderSize );
		SetRelativeDims(WinWidth, Height, WinLeft, WinTop);
	}

	MyList->SetDims( AW - ScrollSize, Height - HeaderSize, AL, AT + HeaderSize );
	PreDrawControl(Canvas, MyList);

	MyScrollBar->SetDims( ScrollSize, Height - HeaderSize, AL + AW - ScrollSize, AT + HeaderSize );
	PreDrawControl(Canvas, MyScrollBar);

	if ( List && !List->bInitialised )
		List->eventInitializeColumns(Canvas);

	unguardobj;
}

// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIListBase
// =======================================================================================================================================================
// =======================================================================================================================================================
static UGUIListBase* GSList;
static INT Compare( FGUIListElem& A, FGUIListElem& B )
{
	if(GSList)
		return GSList->delegateCompareItem(A, B);
	else
		return appStricmp( *(A.item), *(B.item) );
}

void UGUIListBase::execSortList( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIListBase::SortList);

	P_FINISH;

	// GSList being set indicates we should use the script delegate to compare elements.
	// If no delegate is set - we sort alphabetically.
	if(DELEGATE_IS_SET(CompareItem))
		GSList=this;
	else
		GSList=NULL;

	
	Sort( &Elements(0), eventItemCount() );

	unguardexec;
}

void UGUIListBase::DrawItem(UCanvas* Canvas, INT Item, FLOAT X, FLOAT Y, FLOAT W, FLOAT H)
{
	guard(UGUIListBase::DrawItem);
	
	INT itemMenuState = (MenuState == MSAT_Focused) ? MSAT_Blurry : MenuState;

	if (ItemStyle != NULL) {
		// Draw the bounding box
		ItemStyle->Draw(Canvas, itemMenuState, X, Y, W, H);

		if ( Item == Index )
		{
			itemMenuState = MSAT_Focused;

			FLOAT lastAlpha = Canvas->ForcedAlpha;
			Canvas->ForcedAlpha *= 0.75f;
			ItemStyle->Draw( Canvas, itemMenuState, X, Y, W, H);
			Canvas->ForcedAlpha = lastAlpha;
		}

		// Adjust the bounds to account for the bounding box graphic
		Y += ItemStyle->BorderOffsets[1];
		H -= ItemStyle->BorderOffsets[3] + ItemStyle->BorderOffsets[1];

		X += ItemStyle->BorderOffsets[0];
		W -= ItemStyle->BorderOffsets[2] + ItemStyle->BorderOffsets[0];

		ItemStyle->DrawText(Canvas, itemMenuState, X,Y,W,H, TextAlign, *(Elements(Item).item));
	} else
		Style->DrawText(Canvas, itemMenuState, X,Y,W,H, TextAlign, *(Elements(Item).item));
	unguardobj;
}

// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIVertList
// =======================================================================================================================================================
// =======================================================================================================================================================

void UGUIVertList::PreDraw(UCanvas* Canvas)
{
	guard(UGUIVertList::PreDraw);

	if ( INVALIDRENDER )
		return;

	if ( ItemStyle && ItemStyle->Fonts[MenuState] )
		Canvas->Font = ItemStyle->Fonts[MenuState]->eventGetFont(Canvas->SizeX);

	if (Canvas->Font == NULL && Style && Style->Fonts[MenuState] )
		Canvas->Font = Style->Fonts[MenuState]->eventGetFont(Canvas->SizeX);

	if ( Canvas->Font == NULL )
		return;

	Super::PreDraw(Canvas);

	FLOAT XL, YL;
	Canvas->ClippedStrLen( Canvas->Font, 1, 1, XL, YL, TEXTSAMPLE );
	ItemHeight = YL;
	if ( ItemStyle )
		ItemHeight += ItemStyle->BorderOffsets[1] + ItemStyle->BorderOffsets[3];

	ItemHeight += ItemPadding;

	FLOAT Y = ClientBounds[1] + ItemHeight;
	for ( ItemsPerPage = 0; Y <= ClientBounds[3] && ItemsPerPage < 1000; Y += ItemHeight, ItemsPerPage++ )
		Y += interItemGutter;

	if ( MyScrollBar )
	{
		MyScrollBar->WinTop = ClientBounds[1] + interItemGutter * 0.5f;
		MyScrollBar->WinHeight = ItemsPerPage * (ItemHeight + interItemGutter);
	}

	if ( ItemsPerPage > 0 && Index >= Top + ItemsPerPage )
		eventSetTopItem(Index,0);

	unguardobj;
}

void UGUIVertList::Draw(UCanvas* Canvas)
{
	guard(UGUIVertList::Draw);

	INT Count = eventItemCount();
	if ( !bVisible || Style == NULL || (Count == 0 && !bVisibleWhenEmpty) || INVALIDRENDER )
		return;

	UGUIController* C = Controller;

	Super::Draw(Canvas);

	Style->Draw( Canvas, MenuState, Bounds[0], Bounds[1], Bounds[2] - Bounds[0], Bounds[3] - Bounds[1]);
	INT X = ClientBounds[0], Y = ClientBounds[1] + interItemGutter/2, XL = ClientBounds[2] - ClientBounds[0];

	INT SelIndex = Index;
	BYTE TempMenuState;

	if ( bHotTrack && C->HasMouseMoved() && C->ActiveControl == this && C->ActivePage == PageOwner )
	{
		if ( PerformHitTest(C->MouseX,C->MouseY) )
			SelIndex = Min( Top + appFloor( (C->MouseY - ClientBounds[1]) / (ItemHeight+interItemGutter)), Count - 1 );
		else SelIndex = INDEX_NONE;
	}

	for ( INT i = Top; i < Top + ItemsPerPage; i++ )
	{
		if ( i >= Count )
			break;

		TempMenuState = MenuState;
		// Check if this is the selected item

		UBOOL bIsSelected = (i == SelIndex);

		if( !(DELEGATE_IS_SET(OnDrawItem)) || !delegateOnDrawItem(Canvas, i, X, Y, XL, ItemHeight, bIsSelected) )
		{
/*			if (bIsSelected)
			{
				if ( (MenuState==MSAT_Focused)  || (MenuState==MSAT_Pressed) )
				{
					if (SelectedImage==NULL)
						Canvas->DrawTile(Controller->DefaultPens[0], ClientBounds[0],Y, ClientBounds[2] - ClientBounds[0],ItemHeight,0,0,32,32, 0.0f, SelectedBKColor,FPlane(0,0,0,0));
					else
					{
						Canvas->Color = SelectedBKColor;
						Canvas->DrawTileStretched(SelectedImage,ClientBounds[0],Y,ClientBounds[2] - ClientBounds[0],ItemHeight);
					}
				}

				MenuState=MSAT_Pressed;
			}
			else if( MenuState==MSAT_Pressed ) // Never draw unselected items as pressed
				MenuState=MSAT_Focused;
*/
			DrawItem( Canvas, i, X, Y, XL, ItemHeight );
		}
		MenuState=TempMenuState;
		Y+=ItemHeight+interItemGutter;
	}

	unguardobj;
}

// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIHorzList
// =======================================================================================================================================================
// =======================================================================================================================================================

void UGUIHorzList::PreDraw(UCanvas* Canvas)
{
	guard(UGUIHorzList::PreDraw);
	if ( INVALIDRENDER )
		return;

	if ( ItemStyle && ItemStyle->Fonts[MenuState] )
		Canvas->Font = ItemStyle->Fonts[MenuState]->eventGetFont(Canvas->SizeX);

	if (Canvas->Font == NULL && Style && Style->Fonts[MenuState] )
		Canvas->Font = Style->Fonts[MenuState]->eventGetFont(Canvas->SizeX);

	if ( Canvas->Font == NULL )
		return;

	Super::PreDraw(Canvas);

	FLOAT XL, YL;
	INT Count = eventItemCount();
	ItemWidth = 0;

	// Couple of steps to find the best value for ItemWidth

	// First, set ItemWidth to the longest item
	for (INT i = 0; i < Count; ++i)
	{
		Canvas->ClippedStrLen( Canvas->Font, 1, 1, XL, YL, *Elements(i).item );
		ItemWidth = Max<INT>(ItemWidth, XL);
	}

	// Then add in any additional spacing from ItemStyle or ItemPadding
	if ( ItemStyle )
		ItemWidth += ItemStyle->BorderOffsets[0] + ItemStyle->BorderOffsets[2];

	ItemWidth += ItemPadding;

	// Now, figure out how many items we can fit on a single "page" of the list

	FLOAT X = ClientBounds[0] + ItemWidth;
	for ( ItemsPerPage = 0; X <= ClientBounds[2] && ItemsPerPage < 1000; X += ItemWidth, ItemsPerPage++ )
		X += interItemGutter;

	// Then if we're supposed to auto fill, adjust ItemWidth so that ItemsPerPage number of elements fills
	// out entire client-space
	if ( bExpandToFill )
		ItemWidth = (ClientBounds[2] - ClientBounds[0] - interItemGutter * ItemsPerPage) / ItemsPerPage;

	if ( MyScrollBar )
		MyScrollBar->WinWidth = ItemsPerPage * ItemWidth + (Max(ItemsPerPage - 1, 0) * interItemGutter);

	if ( ItemsPerPage > 0 && Index >= Top + ItemsPerPage )
		eventSetTopItem(Index,0);

	unguardobj;
}
//epic bcl -- modified to use PreDraw for width and items/page calculations
void UGUIHorzList::Draw(UCanvas* Canvas)
{
	guard(UGUIHorzList::Draw);

	INT Count = eventItemCount();
	if ( !bVisible || Style == NULL || (Count == 0 && !bVisibleWhenEmpty) || INVALIDRENDER )
		return;

	UGUIController* C = Controller;

	Super::Draw(Canvas);
	Style->Draw( Canvas, MenuState, Bounds[0], Bounds[1], Bounds[2] - Bounds[0], Bounds[3] - Bounds[1]);

	INT Y = ClientBounds[1], YL = ClientBounds[3] - ClientBounds[1], XL = ClientBounds[2] - ClientBounds[0];

	// Attempt to center the items within the area we have for drawing
	INT X = bCenterVisibleElements
		? (ClientBounds[0] + XL / 2) - ((ItemWidth * ItemsPerPage + interItemGutter * ItemsPerPage) / 2)
		: ClientBounds[0] + interItemGutter / 2;

		
		
	INT SelIndex = Index;
	if ( bHotTrack && C->HasMouseMoved() && C->ActiveControl == this && C->ActivePage == PageOwner )
	{
		if ( PerformHitTest(C->MouseX,C->MouseY) )
			SelIndex = Min( Top + appFloor( (C->MouseX - ClientBounds[0]) / (ItemWidth+interItemGutter)), Count - 1 );
		else SelIndex = INDEX_NONE;
	}

	for ( INT i = Top; i < Top + ItemsPerPage; i++ )
	{
		if ( i >= Count )
			break;

		UBOOL bIsSelected = (i == SelIndex);
		if( !(DELEGATE_IS_SET(OnDrawItem)) || !delegateOnDrawItem( Canvas, i, X, Y, ItemWidth, YL, bIsSelected ) )
			DrawItem( Canvas, i, X, Y, ItemWidth, YL);

		X+=ItemWidth+interItemGutter;
	}

	unguardobj;
}

// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIMultiColumnList
// =======================================================================================================================================================
// =======================================================================================================================================================

static INT MLSortOrder=1;
static INT Compare( FMultiColumnSortData& A, FMultiColumnSortData& B )
{
	return MLSortOrder*appStricmp( *(A.SortString), *(B.SortString) );
}

void UGUIMultiColumnList::execGetListIndex( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIMultiColumnList::execGetListIndex);

	P_GET_INT(YourArrayIndex);
	P_FINISH;

	*(INT*)Result = FindSortIndex(YourArrayIndex);
	unguardexec;
}

INT UGUIMultiColumnList::FindSortIndex( INT SortItem ) const
{
	guard(UGUIMultiColumnList::FindSortIndex);

	for ( INT i = 0; i < SortData.Num(); i++ )
		if ( SortData(i).SortItem == SortItem )
			return i;

	return INDEX_NONE;

	unguard;
}

void UGUIMultiColumnList::execAddedItem(FFrame& Stack, RESULT_DECL )
{
    guard(UGUIMultiColumnList::execAddedItem);

	P_GET_INT_OPTX(YourArrayIndex,INDEX_NONE);
	P_FINISH;

	if ( YourArrayIndex == INDEX_NONE )
		YourArrayIndex = SortData.Num();

	FString SortString = delegateGetSortString(YourArrayIndex);
	if ( !IsNumeric(SortString) )
		SortString += appItoa(YourArrayIndex);

	FMultiColumnSortData* NewItem = new(SortData) FMultiColumnSortData();
	appMemzero(NewItem, sizeof(FMultiColumnSortData));

	NewItem->SortItem = YourArrayIndex;
	NewItem->SortString = SortString;

	InvSortData.AddItem(InvSortData.Num());

	unguardexec;
}

void UGUIMultiColumnList::execUpdatedItem(FFrame& Stack, RESULT_DECL )
{
    guard(UGUIMultiColumnList::execUpdatedItem);
	P_GET_INT(YourArrayIndex);
	P_FINISH;

	INT i = FindSortIndex(YourArrayIndex);
	if ( SortData.IsValidIndex(i) )
	{
		SortData(i).SortString = delegateGetSortString(YourArrayIndex);
		if ( !IsNumeric(SortData(i).SortString) )
			SortData(i).SortString += appItoa(SortData(i).SortItem);
	}

	unguardexec;
}

void UGUIMultiColumnList::execRemovedItem( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIMultiColumnList::execRemovedItem);

	P_GET_INT(YourArrayIndex);
	P_FINISH;

	INT i = FindSortIndex(YourArrayIndex);
	if ( SortData.IsValidIndex(i) )
	{
		SortData.Remove(i);
		InvSortData.Remove(i);
		// fix up SortItems
		for( INT x=0; x < SortData.Num(); x++ )
			if( SortData(x).SortItem >= YourArrayIndex )
				SortData(x).SortItem--;

	}

	unguardexec;
}

void UGUIMultiColumnList::execChangeSortOrder(FFrame& Stack, RESULT_DECL )
{
    guard(UGUIMultiColumnList::execChangeSortOrder);
	P_FINISH;

	for( INT i=0;i<SortData.Num();i++ )
	{
		SortData(i).SortString = delegateGetSortString(SortData(i).SortItem);
		if ( !IsNumeric(SortData(i).SortString) )
			SortData(i).SortString += appItoa(SortData(i).SortItem);
	}

	unguardexec;
}

void UGUIMultiColumnList::execSortList(FFrame& Stack, RESULT_DECL )
{
    guard(UGUIMultiColumnList::execSortList);
	P_FINISH;

	if( SortColumn == -1 )
		return;

	if( bSortDescending )
		MLSortOrder = -1;
	else
		MLSortOrder = 1;

	check(SortData.Num() == InvSortData.Num());
	TMap<INT,INT> TempSortData;

	for ( INT i = 0; i < SortData.Num(); i++ )
		TempSortData.Set( SortData(i).SortItem, i );

	Sort( &SortData(0), SortData.Num() );

	// update reverse map
	for ( INT i = 0; i < SortData.Num(); i++ )
	{
		INT j = TempSortData.FindRef(SortData(i).SortItem);
		InvSortData(j) = i;
	}

	bNeedsSorting = 0;

	unguardexec;
}

// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIMultiColumnListHeader
// =======================================================================================================================================================
// =======================================================================================================================================================
void UGUIMultiColumnListHeader::PreDraw(UCanvas* Canvas)
{
	guard(UGUIMultiColumnListHeader::PreDraw);
	if( Style )
	{
		FLOAT XL, YL;
		Style->TextSize(Canvas, MenuState, TEXTSAMPLE, XL, YL);
		WinHeight = YL * 1.2;
	}
	Super::PreDraw(Canvas);
	unguardobj;
}

UBOOL UGUIMultiColumnListHeader::MousePressed(UBOOL IsRepeat)
{
	guard(UGUIMultiColumnListHeader::MousePressed);

	if ( !Super::MousePressed(IsRepeat) )
		return false;

	if( !IsRepeat )
	{
		ClickingCol = -1;
		SizingCol = -1;
		FLOAT x = ActualLeft();
		for( INT i=0;i<MyList->ColumnWidths.Num();i++ )
		{
			x += MyList->ColumnWidths(i);
			if( x >= (FLOAT)Controller->MouseX-2.f && x <= (FLOAT)Controller->MouseX+2.f )
			{
				bCaptureMouse = 1;
				SizingCol = i;
			}
			else
			if( SizingCol >= 0 )
				return true;
		}

		x = ActualLeft();
		for( INT i=0;i<MyList->ColumnWidths.Num();i++ )
		{
			if( (FLOAT)Controller->MouseX >= x+5 && (FLOAT)Controller->MouseX <= x+MyList->ColumnWidths(i)-5 && MyList->ColumnWidths(i) > 10 )
			{
				ClickingCol = i;

				if( MyList->SortColumn == i )
				{
					MyList->bSortDescending = !MyList->bSortDescending;
				}
				else if(MyList->SortColumn != -1)
				{
					MyList->SortColumn = i;
					MyList->bSortDescending = 0;
//					Controller->PlayClickSound(CS_Up);
				}

				MyList->eventOnSortChanged();
				return true;
			}
			x += MyList->ColumnWidths(i);
		}
	}
	return true;
	unguardobj;
}

UBOOL UGUIMultiColumnListHeader::MouseReleased()
{
	guard(UGUIMultiColumnListHeader::MousePressed);
	Super::MouseReleased();
	ClickingCol = -1;
	SizingCol = -1;
	return true;
	unguardobj;
}

UBOOL UGUIMultiColumnListHeader::MouseMove(INT XDelta, INT YDelta)
{
	guard(UGUIMultiColumnListHeader::MouseMove);
	Super::MouseMove(XDelta,YDelta);

	if( MenuState==MSAT_Pressed && SizingCol >= 0 && SizingCol<MyList->ColumnWidths.Num() )
	{
		FLOAT x = ActualLeft();
		for( INT i=0;i<SizingCol;i++ )
			x += MyList->ColumnWidths(i);
		MyList->ColumnWidths(SizingCol) = Clamp<FLOAT>( (FLOAT)Controller->MouseX - x, 0, ActualLeft() + ActualWidth() - x );
		MyList->delegateOnColumnSized(SizingCol);
	}
	else
	{
		bCaptureMouse = 0;
		SizingCol = -1;
	}
	return false;
	unguardobj;
}

UBOOL UGUIMultiColumnListHeader::MouseHover()
{
	guard(UGUIMultiColumnListHeader::MouseMove);
	Super::MouseHover();

	// Update curosr if we are just waving the cursor over the thing. Dont care which one it is.
	INT OnColSize = -1;
	FLOAT x = ActualLeft();
	for( INT i=0; i<MyList->ColumnWidths.Num() && OnColSize == -1; i++ )
	{
		x += MyList->ColumnWidths(i);
		if( x >= (FLOAT)Controller->MouseX-2.f && x <= (FLOAT)Controller->MouseX+2.f )
		{
			OnColSize = i;
		}
	}

	if(OnColSize != -1)
		MouseCursorIndex = 5;
	else
		MouseCursorIndex = 0;

	return false;

	unguardobj;
}


void UGUIMultiColumnListHeader::Draw(UCanvas* Canvas)
{
	guard(UGUIMultiColumnListHeader::Draw);

	if ( (!bVisible) || (Style==NULL) )
		return;

	Super::Draw(Canvas);

	DECLAREBOUNDS;

	FLOAT X = AL;

    //for( INT i=0;i<MyList->ColumnWidths.Num() && i<MyList->ColumnHeadings.Num();i++ )
    for( INT i = 0; i < MyList->ColumnWidths.Num(); i++ )  // assumes that ColumnHeadings size >= ColumnWidths.Num()
	{
		FLOAT W = Min<FLOAT>( MyList->ColumnWidths(i), AL + AW - X );

		if ( i < MyList->Columns.Num() )
		{
			FLOAT XL, YL;
			Style->TextSize(Canvas, MenuState, *MyList->Columns(i).Text, XL, YL);

			// try to expand the column if the text is longer than the column width
			// we do this on console because we don't have a mouse
			if ( XL > W && X + XL < AL + AW )
			{
				MyList->ColumnWidths(i) = XL;
				W = XL;
			}
		}

		if( W > 0 )
		{
			BYTE DrawState = i == MyList->SortColumn ? MSAT_Focused
				: i == ClickingCol ? MSAT_Pressed : MSAT_Blurry;

			Style->Draw( Canvas, DrawState, X, AT, W, AH );

			if ( i < MyList->Columns.Num() )
			{
				Style->DrawText( Canvas, DrawState, 
								X + Style->BorderOffsets[0],
								AT + Style->BorderOffsets[1],
								W - Style->BorderOffsets[0] - Style->BorderOffsets[2],
								AH - Style->BorderOffsets[1] - Style->BorderOffsets[3],
								MyList->Columns(i).Alignment, *MyList->Columns(i).Text );
			}
			X += W;
		}
    }

	FLOAT X2 = AW + AL;
	if( X < X2 )
		Style->Draw( Canvas, MSAT_Blurry, X, AT, X2 - X, AH );

	unguardobj;
}


// =======================================================================================================================================================
// =======================================================================================================================================================
// UGUIScrollText
// =======================================================================================================================================================
// =======================================================================================================================================================

void UGUIScrollText::PreDraw(UCanvas* Canvas)
{
	guard(UGUIScrollText::PreDraw);
	if ( Style == NULL || INVALIDRENDER )
		return;

	Super::PreDraw(Canvas);

	INT cWidth = Bounds[2] - Bounds[0] - Style->BorderOffsets[0] - Style->BorderOffsets[2];

	UFont* Font = Canvas->Font;
	if ( Style->Fonts[MSAT_Blurry] )
		Font = Style->Fonts[MSAT_Blurry]->eventGetFont(Canvas->SizeX);

	// Re-wrap entire content (eg. we have changed resolution)
	if (bNewContent || cWidth != oldWidth)
	{
		// Carve big string into seperate strings of the right length
		TArray<FString> Strings;
		Canvas->WrapStringToArray(*Content, &Strings, cWidth, Font, 1.f, **Separator);

		// Then fill array of elements with each string
		Elements.Empty();
		for(INT i=0; i<Strings.Num(); i++)
		{
			Elements.AddZeroed();
			Elements(i).item = Strings(i);
		}

		if (Elements.Num() == 0)
			VisibleLines = -1;
		else
			VisibleLines=0;
		VisibleChars=0;
		oldWidth = cWidth;
		bNewContent = false;
	}

	// Note if we are viewing the bottom of the text.
	UBOOL bIsAtBottom = 0;
	if( (Elements.Num() - 1) < (Top + ItemsPerPage) )
		bIsAtBottom = 1;

	// Add on any text in the NewText string to the end of the scroll text.
	if( NewText.Len() && ItemsPerPage > 0)
	{
		// Add new text to end of Content
		Content += (Separator + NewText);

		// Wrap just our new text, and add to array of strings.
		TArray<FString> Strings;
		Canvas->WrapStringToArray(*NewText, &Strings, cWidth, Font, 1.f, **Separator);

		
		for(INT i=0; i<Strings.Num(); i++)
		{
			INT newIx = Elements.AddZeroed();
			Elements(newIx).item = Strings(i);
		}
	
		// If we have too much text, remove some.
		INT ExcessLines = Elements.Num() - MaxHistory;
		if(MaxHistory > 0 && ExcessLines > 0)
		{
			INT RemoveChars = 0;
			for(INT i=0; i<ExcessLines; i++)
				RemoveChars += (Elements(i).item.Len() + 1); // Add extra character in case there was a charage return

			Elements.Remove(0, ExcessLines);

			// If we would be separating an xbox button code and its caption, back up a character
			if ( RemoveChars > 0 && **Content.Mid(RemoveChars-1) == 2 )
				RemoveChars--;

			Content = Content.Mid(RemoveChars);
		}

		// If we were at the bottom of the text, scroll down as it goes.
		if(bIsAtBottom)
		{
			Top = Max(Elements.Num() - ItemsPerPage, 0);
			if (MyScrollBar && Elements.Num()>0)
				MyScrollBar->eventAlignThumb();
		}

		// Empty 'NewText'
		NewText = TEXT("");
	}

	unguardobj;
}

void UGUIScrollText::Draw(UCanvas *Canvas)
{
	guard(UGUIScrollText::Draw);

	if ( !bVisible || Style == NULL || (Elements.Num() == 0 && !bVisibleWhenEmpty) || INVALIDRENDER )
		return;

	if ( !bStopped )
	{
		// Find our best Top
		INT NewTop = Max(VisibleLines - ItemsPerPage + 1, 0);
		if ( NewTop >= 0)
		{
			if (Top != NewTop)
			{
				Top = NewTop;
				if (MyScrollBar && Elements.Num()>0)
					MyScrollBar->eventAlignThumb();
			}
		}
	}

	Index = -1;
	Super::Draw(Canvas);

	unguardobj;
}

void UGUIScrollText::DrawItem(UCanvas* Canvas, INT Item, FLOAT X, FLOAT Y, FLOAT W, FLOAT H)
{

	guard(UGUIScrollText::DrawItem);

	if ( INVALIDRENDER )
		return;

	UGUIStyles* DStyle = ItemStyle ? ItemStyle : Style;
	BYTE DrawState = MenuState == MSAT_Disabled
		? MSAT_Disabled
		: bAlwaysFocused
			? MSAT_Focused
			: MenuState;

	// This determines whether this line should be drawn with the moving cursor
	if (!bStopped && Item == VisibleLines)
	{
		FString Elem(Elements(Item).item.Left(VisibleChars));

		FLOAT OldAlpha = Canvas->ForcedAlpha;
		Canvas->ForcedAlpha *= 0.75;

		Canvas->Style=5;	// Alpha
		DStyle->DrawText(Canvas, DrawState, X, Y, W, H, TextAlign, *Elem);
		Canvas->ForcedAlpha = OldAlpha;

		FLOAT XL, YL;
		DStyle->TextSize( Canvas, DrawState, *Elem, XL, YL );

		switch(TextAlign)
		{
		case TXTA_Left:	X += XL;				break;
		case TXTA_Center: X += ((W + XL) / 2); 	break;
		case TXTA_Right: X += W;				break;
		}
		if ( Canvas->ForcedAlpha > 0.f )
		{
			Canvas->Color = FColor(255,255,255,Controller->FastCursorFade);
			Canvas->DrawTile(Controller->DefaultPens[0], X, Y, YL/2,YL,0,0,1,1, 0, Canvas->Color.Plane());
		}
	}
	else if ( bStopped || Item < VisibleLines )
		DStyle->DrawText(Canvas, DrawState, X,Y,W,H, TextAlign, *(Elements(Item).item));

	unguardobj;

}

// Find current word under cursor.
void UGUIScrollText::execGetWordUnderCursor( FFrame& Stack, RESULT_DECL )
{
	guard(UGUIScrollText::execGetWordUnderCursor);

	P_FINISH;

	if ( Controller == NULL )
		return;

	*(FString*)Result = FString(TEXT(""));

	if ( ( Controller->MouseX < ClientBounds[0] ) ||
		 ( Controller->MouseX > ClientBounds[2] ) ||
		 ( Controller->MouseY < ClientBounds[1] ) ||
		 ( Controller->MouseY > ClientBounds[3] ) ||
		 (ItemsPerPage==0) )
		return;

	// First figure out the line
	INT row = Controller->MouseY - ClientBounds[1];
	INT ClickLineIx = Top + (row / ItemHeight);

	// If line is outside range, do nothing.
	if(ClickLineIx < 0 || ClickLineIx > Elements.Num() - 1)
		return;

	FString ClickLine = Elements(ClickLineIx).item;

	// Then find the word that we clicked on
	UFont *Font = Style->Fonts[MSAT_Blurry]->eventGetFont( Controller->ResX );
	FLOAT currPos = ClientBounds[0]; // Starting from the left bound - work along until we pass our selection point.

	for(INT i=0; i<ClickLine.Len(); i++)
	{
		// If this is a colour character - skip it and the three following characters
		if( int(ClickLine[i]) == 0x1B )
		{
			i += 3;
			continue;
		}
			
		FLOAT Width, Height;
		Font->GetCharSize(ClickLine[i], Width, Height);
		currPos += Width + Font->Kerning;

		if(currPos > Controller->MouseX)
		{
			// We now have the character we clicked on. Do nothing if its a space
			if(ClickLine[i] == ' ')
				return;

			// Now crawl left and right to find ends of this word.
			INT startPos = i, endPos = i;

			while(startPos > 0 && ClickLine[startPos-1] != ' ')
				startPos--;

			while(endPos < (ClickLine.Len()-1) && ClickLine[endPos+1] != ' ')
				endPos++;

			FString ClickString = ClickLine.Mid(startPos, endPos-startPos+1);

			*(FString*)Result = ClickString;
			return;
		}
	}


	unguardexec;
}


/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
