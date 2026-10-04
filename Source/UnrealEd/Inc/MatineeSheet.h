/*=============================================================================
	MatineeSheet : Property sheet for editing Matinee paths
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Warren Marshall

    Work-in-progress todo's:

=============================================================================*/

#include <stdio.h>

// --------------------------------------------------------------
//
// WPageScenes
//
// --------------------------------------------------------------

struct {
	TCHAR ToolTip[64];
	INT ID;
} ToolTips_PageScenes[] = {
	TEXT("Add New Scene"), IDPB_NEW,
	TEXT("Duplicate Scene"), IDPB_DUPLICATE,
	TEXT("Delete Scene"), IDPB_DELETE,
	TEXT("Open a Preview Window"), IDPB_PREVIEW,
	NULL, 0
};

class WPageScenes : public WPropertyPage
{
	DECLARE_WINDOWCLASS(WPageScenes,WPropertyPage,Window)

	WPropertySheet* PropSheet;

	WObjectProperties* PropertyWindow;

	WButton *NewButton, *DuplicateButton, *DeleteButton, *PreviewButton;
	WGroupBox *ScenesBox;
	WScrollBar* ScrollBar;

	WToolTip* ToolTipCtrl;

	UViewport* Viewport;

	HBITMAP NewBitmap, DuplicateBitmap, DeleteBitmap, PreviewBitmap;

	// Structors.
	WPageScenes ( WWindow* InOwnerWindow )
	:	WPropertyPage( InOwnerWindow )
	{
		PropertyWindow = NEW WObjectProperties( NAME_None, CPF_Edit, TEXT(""), this, 1 );
		PropertyWindow->ShowTreeLines = 0;

		NewButton = DuplicateButton = DeleteButton = PreviewButton = NULL;
		ScenesBox = NULL;
		ScrollBar = NULL;

		Viewport = NULL;

		NewBitmap = (HBITMAP)LoadImageA( hInstance, MAKEINTRESOURCEA(IDBM_NEW), IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS );	check(NewBitmap);
		DuplicateBitmap = (HBITMAP)LoadImageA( hInstance, MAKEINTRESOURCEA(IDBM_DUPLICATE), IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS );	check(DuplicateBitmap);
		DeleteBitmap = (HBITMAP)LoadImageA( hInstance, MAKEINTRESOURCEA(IDBM_DELETE), IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS );	check(DeleteBitmap);
		PreviewBitmap = (HBITMAP)LoadImageA( hInstance, MAKEINTRESOURCEA(IDBM_MAT_PREVIEW), IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS );	check(PreviewBitmap);
	}

	virtual void OpenWindow( INT InDlgId, HMODULE InHMOD )
	{
		guard(WPageScenes::OpenWindow);
		WPropertyPage::OpenWindow( InDlgId, InHMOD );

		// Create child controls and let the base class determine their proper positions.
		ScenesBox = NEW WGroupBox( this, IDGP_SCENES );
		ScenesBox->OpenWindow( 1, 0 );
		ScrollBar = NEW WScrollBar( this, IDSB_SCENES );
		ScrollBar->OpenWindow( 1, 0, 0, 10, 10 );
		NewButton = NEW WButton( this, IDPB_NEW, FDelegate(this,(TDelegate)OnNew) );
		NewButton->OpenWindow( 1, 0, 0, 10, 10, TEXT("N") );
		DuplicateButton = NEW WButton( this, IDPB_DUPLICATE, FDelegate(this,(TDelegate)OnDuplicate) );
		DuplicateButton->OpenWindow( 1, 0, 0, 10, 10, TEXT("N") );
		DeleteButton = NEW WButton( this, IDPB_DELETE, FDelegate(this,(TDelegate)OnDelete) );
		DeleteButton->OpenWindow( 1, 0, 0, 10, 10, TEXT("X") );
		PreviewButton = NEW WButton( this, IDPB_PREVIEW, FDelegate(this,(TDelegate)OnPreview) );
		PreviewButton->OpenWindow( 1, 0, 0, 10, 10, TEXT("X") );

		PlaceControl( ScenesBox );
		PlaceControl( ScrollBar );
		PlaceControl( NewButton );
		PlaceControl( DuplicateButton );
		PlaceControl( DeleteButton );
		PlaceControl( PreviewButton );

		Finalize();

		NewButton->SetBitmap( NewBitmap );
		DuplicateButton->SetBitmap( DuplicateBitmap );
		DeleteButton->SetBitmap( DeleteBitmap );
		PreviewButton->SetBitmap( PreviewBitmap );

		// Viewport
		FName Name = TEXT("MatineeScenes");
		Viewport = GUnrealEd->Client->NewViewport( Name );
		GUnrealEd->Level->SpawnViewActor( Viewport );
		Viewport->Actor->ShowFlags = SHOW_StandardView | SHOW_ChildWindow;
		Viewport->Actor->RendMap   = REN_MatineeScenes;
		Viewport->Actor->Misc1 = 0;
		Viewport->Actor->Misc2 = 0;
		Viewport->Group = NAME_None;
		Viewport->MiscRes = NULL;
		Viewport->Input->Init( Viewport );

		RECT rc;
		::GetWindowRect( GetDlgItem( hWnd, IDSC_VIEWPORT_SCENES ), &rc );
		::ScreenToClient( hWnd, (POINT*)&rc.left );
		::ScreenToClient( hWnd, (POINT*)&rc.right );
		Viewport->OpenWindow( (DWORD)hWnd, 0, (rc.right - rc.left), (rc.bottom - rc.top), rc.left, rc.top );

		PropertyWindow->OpenChildWindow( IDSC_PROPERTIES );
		PropertyWindow->Root.Sorted = 1;
		PropertyWindow->SetNotifyHook( GUnrealEd );
		PropertyWindow->bAllowForceRefresh = 0;

		ToolTipCtrl = NEW WToolTip(this);
		ToolTipCtrl->OpenWindow();
		for( INT tooltip = 0 ; ToolTips_PageScenes[tooltip].ID > 0 ; ++tooltip )
			ToolTipCtrl->AddTool( GetDlgItem( hWnd, ToolTips_PageScenes[tooltip].ID ), ToolTips_PageScenes[tooltip].ToolTip, tooltip );

		unguard;
	}
	void OnDestroy()
	{
		guard(WPageScenes::OnDestroy);
		WPropertyPage::OnDestroy();

		::DestroyWindow( PropertyWindow->hWnd );
		PropertyWindow->Root.SetObjects( NULL, 0 );
		delete PropertyWindow;

		::DestroyWindow( ScenesBox->hWnd );
		::DestroyWindow( ScrollBar->hWnd );
		::DestroyWindow( NewButton->hWnd );
		::DestroyWindow( DuplicateButton->hWnd );
		::DestroyWindow( DeleteButton->hWnd );
		::DestroyWindow( PreviewButton->hWnd );

		delete ScenesBox;
		delete ScrollBar;
		delete NewButton;
		delete DuplicateButton;
		delete DeleteButton;
		delete PreviewButton;

		delete ToolTipCtrl;
		delete Viewport;

		DeleteObject( NewBitmap );
		DeleteObject( DuplicateBitmap );
		DeleteObject( DeleteBitmap );
		DeleteObject( PreviewBitmap );

		unguard;
	}
	virtual void Refresh()
	{
		guard(WPageScenes::Refresh);
		WPropertyPage::Refresh();

		RefreshViewport();
		RefreshProperties();
		RefreshScrollBar();

		unguard;
	}
	void RefreshViewport()
	{
		guard(WPageScenes::RefreshViewport);
		Viewport->Repaint( 1 );
		unguard;
	}
	void RefreshProperties()
	{
		guard(WPageScenes::RefreshProperties);

		// Playing with bAllowForceRefresh like this minimizes flashing in the window when selecting
		// actors in the level itself
		PropertyWindow->bAllowForceRefresh = 1;

		ASceneManager* SM = GMatineeTools.GetCurrent();
		if( SM )
			PropertyWindow->Root.SetObjects( (UObject**)&SM, 1 );
		else
			PropertyWindow->Root.SetObjects( NULL, 0 );

		PropertyWindow->bAllowForceRefresh = 0;

		unguard;
	}
	void RefreshScrollBar()
	{
		guard(WPageScenes::RefreshScrollBar);

		if( !ScrollBar ) return;

		// Set the scroll bar to have a valid range.
		SCROLLINFO si;
		si.cbSize = sizeof(SCROLLINFO);
		si.fMask = SIF_DISABLENOSCROLL | SIF_RANGE | SIF_POS | SIF_PAGE;
		si.nPage = Viewport->SizeY;
		si.nMin = 0;
		si.nMax = GMatineeTools.SceneScrollMax+Viewport->SizeY;
		si.nPos = GMatineeTools.SceneScrollPos;
		SetScrollInfo( ScrollBar->hWnd, SB_CTL, &si, TRUE );

		unguard;
	}
	virtual void OnVScroll( WPARAM wParam, LPARAM lParam )
	{
		if( (HWND)lParam == ScrollBar->hWnd )
		{
			switch(LOWORD(wParam)) {

				case SB_LINEUP:
					GMatineeTools.SceneScrollPos -= 32;
					GMatineeTools.SceneScrollPos = Max( GMatineeTools.SceneScrollPos, 0 );
					break;

				case SB_LINEDOWN:
					GMatineeTools.SceneScrollPos += 32;
					GMatineeTools.SceneScrollPos = Min( GMatineeTools.SceneScrollPos, GMatineeTools.SceneScrollMax );
					break;

				case SB_PAGEUP:
					GMatineeTools.SceneScrollPos -= 64;
					GMatineeTools.SceneScrollPos = Max( GMatineeTools.SceneScrollPos, 0 );
					break;

				case SB_PAGEDOWN:
					GMatineeTools.SceneScrollPos += 64;
					GMatineeTools.SceneScrollPos = Min( GMatineeTools.SceneScrollPos, GMatineeTools.SceneScrollMax );
					break;

				case SB_THUMBTRACK:
					GMatineeTools.SceneScrollPos = (short int)HIWORD(wParam);
					break;
			}

			RefreshScrollBar();
			RefreshViewport();
		}
	}
	void OnNew()
	{
		guard(WPageScenes::OnNew);

		UViewport* Viewport = GUnrealEd->GetCurrentViewport();
		if( Viewport )
			GUnrealEd->AddActor( GUnrealEd->Level, ASceneManager::StaticClass(), Viewport->Actor->Location );
		RefreshViewport();

		unguard;
	}
	void OnDuplicate()
	{
		guard(WPageScenes::OnDuplicate);

		ASceneManager* SM = GMatineeTools.GetCurrent();
		if( SM )
		{
			GUnrealEd->Level->Modify();
			ASceneManager* NewSM = Cast<ASceneManager>( GUnrealEd->Level->SpawnActor( ASceneManager::StaticClass(), NAME_None, SM->Location + FVector(32,32,0), FRotator(0,0,0), SM ) );
			GMatineeTools.SetCurrent( GUnrealEd, GUnrealEd->Level, NewSM );

			/*
			// Create new actions
			NewSM->Actions.Empty();
			for( INT a = 0 ; a < SM->Actions.Num() ; ++a )
			{
				UMatAction *NewMA, *MA;

				MA = SM->Actions(a);

				NewSM->Actions.InsertZeroed( a );
				NewMA = NewSM->Actions(a) = Cast<UMatAction>( GUnrealEd->StaticConstructObject( MA->GetClass(), GUnrealEd->Level, NAME_None ) );

				CopyAction( MA, NewMA );

				// Create new subactions
				NewSM->Actions(a)->SubActions.Empty();
				for( INT sa = 0 ; sa < SM->Actions(a)->SubActions.Num() ; ++sa )
				{
					UMatSubAction *NewMSA, *MSA;

					MSA = SM->Actions(a)->SubActions(sa);
					NewMSA = CastChecked<UMatSubAction>(GUnrealEd->StaticConstructObject( MSA->GetClass(), GUnrealEd->Level, NAME_None, RF_Public|RF_Standalone, MSA, GError, 1 ));
					NewSM->Actions(a)->SubActions.InsertZeroed( sa );
					NewSM->Actions(a)->SubActions(sa) = NewMSA;

					//MSA = SM->Actions(a)->SubActions(sa);
					//NewSM->Actions(a)->SubActions.InsertZeroed( sa );
					//NewMSA = NewSM->Actions(a)->SubActions(sa) = Cast<UMatSubAction>( GUnrealEd->StaticConstructObject( MSA->GetClass(), GUnrealEd->Level, NAME_None ) );
					//CopySubAction( MSA, NewMSA );
				}
			}
				*/
		}

		RefreshProperties();
		RefreshViewport();

		unguard;
	}
	void OnDelete()
	{
		guard(WPageScenes::OnDelete);

		ASceneManager* SM = GMatineeTools.GetCurrent();
		if( SM )
			GUnrealEd->Level->DestroyActor( SM );

		GMatineeTools.SetCurrent( GUnrealEd, GUnrealEd->Level, NULL );
		GUnrealEd->RedrawLevel( GUnrealEd->Level );
		RefreshViewport();
		PropSheet->RefreshPages();

		unguard;
	}
	void OnPreview()
	{
		guard(WPageScenes::OnPreview);
		PostMessageX( OwnerWindow->OwnerWindow->OwnerWindow->hWnd, WM_COMMAND, IDMN_SCENE_PREVIEW, 0 );
		unguard;
	}
	//scion jg -- Resize the child windows when the sheet changes size
	void OnSize(DWORD dwFlags,INT NewX,INT NewY)
	{
		guard(WPageScenes::OnSize);
		RECT rc;
		::GetWindowRect( GetDlgItem( hWnd, IDSC_VIEWPORT_SCENES ), &rc );
		::ScreenToClient( hWnd, (POINT*)&rc.left );
		::ScreenToClient( hWnd, (POINT*)&rc.right );
		unguard;
	}
};

// --------------------------------------------------------------
//
// WPageActions
//
// --------------------------------------------------------------

struct {
	TCHAR ToolTip[64];
	INT ID;
} ToolTips_PageActions[] = {
	TEXT("Add New Action"), IDPB_NEW,
	TEXT("Duplicate Action"), IDPB_DUPLICATE,
	TEXT("Delete Action"), IDPB_DELETE,
	TEXT("Move Action Up"), IDPB_MOVE_UP,
	TEXT("Move Action Down"), IDPB_MOVE_DOWN,
	TEXT("Open a Preview Window"), IDPB_PREVIEW,
	NULL, 0
};

#define IDMN_ACTION_BASE	20000
#define IDMN_ACTION_MAX		20100

class FActionType
{
public:
	FActionType() {}
	FActionType( INT InID, FString InName, UClass* InClass ) : ID(InID), Name(InName), Class(InClass) {}
	~FActionType() {}

	FString Name;
	INT ID;
	UClass* Class;
};

TArray<FActionType> GActionTypes;

class WPageActions : public WPropertyPage
{
	DECLARE_WINDOWCLASS(WPageActions,WPropertyPage,Window)

	WPropertySheet* PropSheet;

	WObjectProperties* PropertyWindow;

	WButton *NewButton, *DuplicateButton, *DeleteButton, *MoveUpButton, *MoveDownButton, *PreviewButton;
	WGroupBox *ActionsBox;
	WScrollBar* ScrollBar;

	WToolTip* ToolTipCtrl;

	UViewport* Viewport;

	HBITMAP NewBitmap, DuplicateBitmap, DeleteBitmap, MoveUpBitmap, MoveDownBitmap, PreviewBitmap;;

	// Structors.
	WPageActions ( WWindow* InOwnerWindow )
	:	WPropertyPage( InOwnerWindow )
	{
		PropertyWindow = NEW WObjectProperties( NAME_None, CPF_Edit, TEXT(""), this, 1 );
		PropertyWindow->ShowTreeLines = 0;

		NewButton = DuplicateButton = DeleteButton = MoveUpButton = MoveDownButton = PreviewButton = NULL;
		ActionsBox = NULL;
		ScrollBar = NULL;

		Viewport = NULL;

		NewBitmap = (HBITMAP)LoadImageA( hInstance, MAKEINTRESOURCEA(IDBM_NEW_PLUS), IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS );	check(NewBitmap);
		DuplicateBitmap = (HBITMAP)LoadImageA( hInstance, MAKEINTRESOURCEA(IDBM_DUPLICATE), IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS );	check(DuplicateBitmap);
		DeleteBitmap = (HBITMAP)LoadImageA( hInstance, MAKEINTRESOURCEA(IDBM_DELETE), IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS );	check(DeleteBitmap);
		MoveUpBitmap = (HBITMAP)LoadImageA( hInstance, MAKEINTRESOURCEA(IDBM_MOVE_UP), IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS );	check(MoveUpBitmap);
		MoveDownBitmap = (HBITMAP)LoadImageA( hInstance, MAKEINTRESOURCEA(IDBM_MOVE_DOWN), IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS );	check(MoveDownBitmap);
		PreviewBitmap = (HBITMAP)LoadImageA( hInstance, MAKEINTRESOURCEA(IDBM_MAT_PREVIEW), IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS );	check(PreviewBitmap);
	}

	virtual void OpenWindow( INT InDlgId, HMODULE InHMOD )
	{
		guard(WPageActions::OpenWindow);
		WPropertyPage::OpenWindow( InDlgId, InHMOD );

		// Create child controls and let the base class determine their proper positions.
		ActionsBox = NEW WGroupBox( this, IDGP_ACTIONS );
		ActionsBox->OpenWindow( 1, 0 );
		ScrollBar = NEW WScrollBar( this, IDSB_ACTIONS );
		ScrollBar->OpenWindow( 1, 0, 0, 10, 10 );
		MoveUpButton = NEW WButton( this, IDPB_MOVE_UP, FDelegate(this,(TDelegate)OnMoveUp) );
		MoveUpButton->OpenWindow( 1, 0, 0, 10, 10, TEXT("+") );
		MoveDownButton = NEW WButton( this, IDPB_MOVE_DOWN, FDelegate(this,(TDelegate)OnMoveDown) );
		MoveDownButton->OpenWindow( 1, 0, 0, 10, 10, TEXT("-") );
		NewButton = NEW WButton( this, IDPB_NEW, FDelegate(this,(TDelegate)OnNew) );
		NewButton->OpenWindow( 1, 0, 0, 10, 10, TEXT("N") );
		DuplicateButton = NEW WButton( this, IDPB_DUPLICATE, FDelegate(this,(TDelegate)OnDuplicate) );
		DuplicateButton->OpenWindow( 1, 0, 0, 10, 10, TEXT("D") );
		DeleteButton = NEW WButton( this, IDPB_DELETE, FDelegate(this,(TDelegate)OnDelete) );
		DeleteButton->OpenWindow( 1, 0, 0, 10, 10, TEXT("X") );
		PreviewButton = NEW WButton( this, IDPB_PREVIEW, FDelegate(this,(TDelegate)OnPreview) );
		PreviewButton->OpenWindow( 1, 0, 0, 10, 10, TEXT("X") );

		PlaceControl( ActionsBox );
		PlaceControl( ScrollBar );
		PlaceControl( MoveUpButton );
		PlaceControl( MoveDownButton );
		PlaceControl( NewButton );
		PlaceControl( DuplicateButton );
		PlaceControl( DeleteButton );
		PlaceControl( PreviewButton );

		Finalize();

		NewButton->SetBitmap( NewBitmap );
		DuplicateButton->SetBitmap( DuplicateBitmap );
		DeleteButton->SetBitmap( DeleteBitmap );
		MoveUpButton->SetBitmap( MoveUpBitmap );
		MoveDownButton->SetBitmap( MoveDownBitmap );
		PreviewButton->SetBitmap( PreviewBitmap );

		// Viewport
		FName Name = TEXT("MatineeActions");
		Viewport = GUnrealEd->Client->NewViewport( Name );
		GUnrealEd->Level->SpawnViewActor( Viewport );
		Viewport->Actor->ShowFlags = SHOW_StandardView | SHOW_ChildWindow;
		Viewport->Actor->RendMap   = REN_MatineeActions;
		Viewport->Actor->Misc1 = 0;
		Viewport->Actor->Misc2 = 0;
		Viewport->Group = NAME_None;
		Viewport->MiscRes = NULL;
		Viewport->Input->Init( Viewport );

		RECT rc;
		::GetWindowRect( GetDlgItem( hWnd, IDSC_VIEWPORT_ACTIONS ), &rc );
		::ScreenToClient( hWnd, (POINT*)&rc.left );
		::ScreenToClient( hWnd, (POINT*)&rc.right );
		Viewport->OpenWindow( (DWORD)hWnd, 0, (rc.right - rc.left), (rc.bottom - rc.top), rc.left, rc.top );

		PropertyWindow->OpenChildWindow( IDSC_PROPERTIES );
		PropertyWindow->Root.Sorted = 0;
		PropertyWindow->SetNotifyHook( GUnrealEd );
		PropertyWindow->bAllowForceRefresh = 0;

		ToolTipCtrl = NEW WToolTip(this);
		ToolTipCtrl->OpenWindow();
		for( INT tooltip = 0 ; ToolTips_PageActions[tooltip].ID > 0 ; ++tooltip )
			ToolTipCtrl->AddTool( GetDlgItem( hWnd, ToolTips_PageActions[tooltip].ID ), ToolTips_PageActions[tooltip].ToolTip, tooltip );

		unguard;
	}
	void OnDestroy()
	{
		guard(WPageActions::OnDestroy);
		WPropertyPage::OnDestroy();

		::DestroyWindow( PropertyWindow->hWnd );
		PropertyWindow->Root.SetObjects( NULL, 0 );
		delete PropertyWindow;

		::DestroyWindow( ActionsBox->hWnd );
		::DestroyWindow( ScrollBar->hWnd );
		::DestroyWindow( NewButton->hWnd );
		::DestroyWindow( DuplicateButton->hWnd );
		::DestroyWindow( DeleteButton->hWnd );
		::DestroyWindow( MoveUpButton->hWnd );
		::DestroyWindow( MoveDownButton->hWnd );
		::DestroyWindow( PreviewButton->hWnd );

		delete ActionsBox;
		delete ScrollBar;
		delete NewButton;
		delete DuplicateButton;
		delete DeleteButton;
		delete MoveUpButton;
		delete MoveDownButton;
		delete PreviewButton;

		delete ToolTipCtrl;
		delete Viewport;

		DeleteObject( NewBitmap );
		DeleteObject( DuplicateBitmap );
		DeleteObject( DeleteBitmap );
		DeleteObject( MoveUpBitmap );
		DeleteObject( MoveDownBitmap );
		DeleteObject( PreviewBitmap );

		unguard;
	}
	virtual void Refresh()
	{
		guard(WPageActions::Refresh);
		WPropertyPage::Refresh();

		RefreshViewport();
		RefreshProperties();
		RefreshScrollBar();

		unguard;
	}
	void RefreshViewport()
	{
		guard(WPageActions::RefreshViewport);
		Viewport->Repaint( 1 );
		unguard;
	}
	void RefreshProperties()
	{
		guard(WPageActions::RefreshProperties);

		// Playing with bAllowForceRefresh like this minimizes flashing in the window when selecting
		// actors in the level itself
		PropertyWindow->bAllowForceRefresh = 1;

		UMatAction* Action = GMatineeTools.GetCurrentAction();
		if( Action )
			PropertyWindow->Root.SetObjects( (UObject**)&Action, 1 );
		else
			PropertyWindow->Root.SetObjects( NULL, 0 );

		PropertyWindow->bAllowForceRefresh = 0;

		unguard;
	}
	void RefreshScrollBar()
	{
		guard(WPageActions::RefreshScrollBar);

		if( !ScrollBar ) return;

		// Set the scroll bar to have a valid range.
		SCROLLINFO si;
		si.cbSize = sizeof(SCROLLINFO);
		si.fMask = SIF_DISABLENOSCROLL | SIF_RANGE | SIF_POS | SIF_PAGE;
		si.nPage = Viewport->SizeY;
		si.nMin = 0;
		si.nMax = GMatineeTools.ActionScrollMax+Viewport->SizeY;
		si.nPos = GMatineeTools.ActionScrollPos;
		SetScrollInfo( ScrollBar->hWnd, SB_CTL, &si, TRUE );

		unguard;
	}
	virtual void OnVScroll( WPARAM wParam, LPARAM lParam )
	{
		if( (HWND)lParam == ScrollBar->hWnd )
		{
			switch(LOWORD(wParam)) {

				case SB_LINEUP:
					GMatineeTools.ActionScrollPos -= 32;
					GMatineeTools.ActionScrollPos = Max( GMatineeTools.ActionScrollPos, 0 );
					break;

				case SB_LINEDOWN:
					GMatineeTools.ActionScrollPos += 32;
					GMatineeTools.ActionScrollPos = Min( GMatineeTools.ActionScrollPos, GMatineeTools.ActionScrollMax );
					break;

				case SB_PAGEUP:
					GMatineeTools.ActionScrollPos -= 64;
					GMatineeTools.ActionScrollPos = Max( GMatineeTools.ActionScrollPos, 0 );
					break;

				case SB_PAGEDOWN:
					GMatineeTools.ActionScrollPos += 64;
					GMatineeTools.ActionScrollPos = Min( GMatineeTools.ActionScrollPos, GMatineeTools.ActionScrollMax );
					break;

				case SB_THUMBTRACK:
					GMatineeTools.ActionScrollPos = (short int)HIWORD(wParam);
					break;
			}

			RefreshScrollBar();
			RefreshViewport();
		}
	}
	void OnMoveUp()
	{
		guard(WPageActions::OnMoveUp);

		ASceneManager* SM = GMatineeTools.GetCurrent();
		if( SM )
		{
			INT Idx = GMatineeTools.GetActionIdx( GMatineeTools.GetCurrent(), GMatineeTools.GetCurrentAction() );
			if( Idx > -1 && Idx > 0 )
				Exchange<UMatAction*>( SM->Actions(Idx), SM->Actions(Idx-1) );
		}

		RefreshViewport();

		unguard;
	}
	void OnMoveDown()
	{
		guard(WPageActions::OnMoveDown);

		ASceneManager* SM = GMatineeTools.GetCurrent();
		if( SM )
		{
			INT Idx = GMatineeTools.GetActionIdx( GMatineeTools.GetCurrent(), GMatineeTools.GetCurrentAction() );
			if( Idx > -1 && Idx < SM->Actions.Num()-1 )
				Exchange<UMatAction*>( SM->Actions(Idx), SM->Actions(Idx+1) );
		}

		RefreshViewport();

		unguard;
	}
	void OnNew()
	{
		guard(WPageActions::OnNew);

		// Grab all the subclasses of UMatAction
		GActionTypes.Empty();
		INT ID = IDMN_ACTION_BASE;
		for( TObjectIterator<UClass> It; It; ++It )
			if( It->IsChildOf(UMatAction::StaticClass())  && !(It->ClassFlags&CLASS_Abstract) )
			{
				new(GActionTypes)FActionType( ID, It->GetName(), *It );
				ID++;
			}

		// Create a popup menu
		HMENU menu = CreatePopupMenu();
		MENUITEMINFOA mif;

		mif.cbSize = sizeof(MENUITEMINFO);
		mif.fMask = MIIM_TYPE | MIIM_ID;
		mif.fType = MFT_STRING;

		for( INT x = 0 ; x < GActionTypes.Num() ; ++x )
		{
			FActionType* AT = &(GActionTypes(x));

			mif.dwTypeData = (LPSTR)TCHAR_TO_ANSI( *(AT->Name) );
			mif.wID = AT->ID;

			InsertMenuItemA( menu, 99999, FALSE, &mif );
		}

		RECT rc;
		::GetWindowRect( NewButton->hWnd, &rc );
		TrackPopupMenu( menu,
			TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
			rc.left, rc.bottom, 0,
			hWnd, NULL);
		DestroyMenu( menu );

		unguard;
	}
	void OnDuplicate()
	{
		guard(WPageActions::OnDuplicate);

		ASceneManager* SM = GMatineeTools.GetCurrent();
		UMatAction* MA = GMatineeTools.GetCurrentAction();
		if( SM && MA )
		{
			INT Idx = GMatineeTools.GetActionIdx( GMatineeTools.GetCurrent(), GMatineeTools.GetCurrentAction() );
			Idx++;

			MA = SM->Actions( Idx-1 );
			UMatAction* NewMA = CastChecked<UMatAction>(GUnrealEd->StaticConstructObject( MA->GetClass(), GUnrealEd->Level, NAME_None, RF_Public|RF_Standalone, MA, GError ));
			SM->Actions.InsertZeroed( Idx );
			SM->Actions( Idx ) = NewMA;

			GMatineeTools.SetCurrentAction( NewMA );
		}

		RefreshProperties();
		RefreshViewport();

		unguard;
	}
	void OnDelete()
	{
		guard(WPageActions::OnDelete);

		ASceneManager* SM = GMatineeTools.GetCurrent();
		INT Idx = GMatineeTools.GetActionIdx( GMatineeTools.GetCurrent(), GMatineeTools.GetCurrentAction() );
		if( SM && Idx > -1 )
		{
			SM->Actions.Remove(Idx);

			UMatAction* NewCurrent = NULL;
			if( SM->Actions.Num() )
			{
				if( SM->Actions.Num()-1 > Idx )
					Idx = 0;
				NewCurrent = SM->Actions(Idx);
			}
			GMatineeTools.SetCurrentAction( NewCurrent );
		}

		RefreshViewport();
		PropSheet->RefreshPages();

		unguard;
	}
	void OnCommand( INT Command )
	{
		guard(WPageActions::OnCommand);

		if( Command >= IDMN_ACTION_BASE && Command <= IDMN_ACTION_MAX )
		{
			ASceneManager* SM = GMatineeTools.GetCurrent();
			if( SM )
			{
				INT Idx = GMatineeTools.GetActionIdx( GMatineeTools.GetCurrent(), GMatineeTools.GetCurrentAction() );
				Idx++;
				SM->Actions.InsertZeroed( Idx );
				SM->Actions(Idx) = Cast<UMatAction>( GUnrealEd->StaticConstructObject( GActionTypes(Command-IDMN_ACTION_BASE).Class, GUnrealEd->Level, NAME_None ) );

				GMatineeTools.SetCurrentAction( SM->Actions(Idx) );
			}

			RefreshProperties();
			RefreshViewport();
		}

		WWindow::OnCommand(Command);

		unguard;
	}
	void OnPreview()
	{
		guard(WPageActions::OnPreview);
		PostMessageX( OwnerWindow->OwnerWindow->OwnerWindow->hWnd, WM_COMMAND, IDMN_SCENE_PREVIEW, 0 );
		unguard;
	}
};

// --------------------------------------------------------------
//
// WPageSubActions
//
// --------------------------------------------------------------

struct {
	TCHAR ToolTip[64];
	INT ID;
} ToolTips_PageSubActions[] = {
	TEXT("Add New SubAction"), IDPB_NEW,
	TEXT("Duplicate SubAction"), IDPB_DUPLICATE,
	TEXT("Delete SubAction"), IDPB_DELETE,
	TEXT("Move SubAction Up"), IDPB_MOVE_UP,
	TEXT("Move SubAction Down"), IDPB_MOVE_DOWN,
	TEXT("Open a Preview Window"), IDPB_PREVIEW,
	NULL, 0
};

#define IDMN_SUBACTION_BASE		20000
#define IDMN_SUBACTION_MAX		20100

class FSubActionType
{
public:
	FSubActionType() {}
	FSubActionType( INT InID, FString InName, UClass* InClass ) : ID(InID), Name(InName), Class(InClass) {}
	~FSubActionType() {}

	FString Name;
	INT ID;
	UClass* Class;
};

TArray<FSubActionType> GSubActionTypes;

class WPageSubActions : public WPropertyPage
{
	DECLARE_WINDOWCLASS(WPageSubActions,WPropertyPage,Window)

	WPropertySheet* PropSheet;

	WObjectProperties* PropertyWindow;

	WButton *NewButton, *DuplicateButton, *DeleteButton, *MoveUpButton, *MoveDownButton, *PreviewButton;
	WGroupBox *SubActionsBox;
	WScrollBar* ScrollBar;

	WToolTip* ToolTipCtrl;

	UViewport* Viewport;

	HBITMAP NewBitmap, DuplicateBitmap, DeleteBitmap, MoveUpBitmap, MoveDownBitmap, PreviewBitmap;

	// Structors.
	WPageSubActions ( WWindow* InOwnerWindow )
	:	WPropertyPage( InOwnerWindow )
	{
		PropertyWindow = NEW WObjectProperties( NAME_None, CPF_Edit, TEXT(""), this, 1 );
		PropertyWindow->ShowTreeLines = 0;

		NewButton = DuplicateButton = DeleteButton = MoveUpButton = MoveDownButton = PreviewButton = NULL;
		SubActionsBox = NULL;
		ScrollBar = NULL;

		Viewport = NULL;

		NewBitmap = (HBITMAP)LoadImageA( hInstance, MAKEINTRESOURCEA(IDBM_NEW_PLUS), IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS );	check(NewBitmap);
		DuplicateBitmap = (HBITMAP)LoadImageA( hInstance, MAKEINTRESOURCEA(IDBM_DUPLICATE), IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS );	check(DuplicateBitmap);
		DeleteBitmap = (HBITMAP)LoadImageA( hInstance, MAKEINTRESOURCEA(IDBM_DELETE), IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS );	check(DeleteBitmap);
		MoveUpBitmap = (HBITMAP)LoadImageA( hInstance, MAKEINTRESOURCEA(IDBM_MOVE_UP), IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS );	check(MoveUpBitmap);
		MoveDownBitmap = (HBITMAP)LoadImageA( hInstance, MAKEINTRESOURCEA(IDBM_MOVE_DOWN), IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS );	check(MoveDownBitmap);
		PreviewBitmap = (HBITMAP)LoadImageA( hInstance, MAKEINTRESOURCEA(IDBM_MAT_PREVIEW), IMAGE_BITMAP, 0, 0, LR_LOADMAP3DCOLORS );	check(PreviewBitmap);
	}

	virtual void OpenWindow( INT InDlgId, HMODULE InHMOD )
	{
		guard(WPageSubActions::OpenWindow);
		WPropertyPage::OpenWindow( InDlgId, InHMOD );

		// Create child controls and let the base class determine their proper positions.
		SubActionsBox = NEW WGroupBox( this, IDGP_SUBACTIONS );
		SubActionsBox->OpenWindow( 1, 0 );
		ScrollBar = NEW WScrollBar( this, IDSB_SUBACTIONS );
		ScrollBar->OpenWindow( 1, 0, 0, 10, 10 );
		MoveUpButton = NEW WButton( this, IDPB_MOVE_UP, FDelegate(this,(TDelegate)OnMoveUp) );
		MoveUpButton->OpenWindow( 1, 0, 0, 10, 10, TEXT("+") );
		MoveDownButton = NEW WButton( this, IDPB_MOVE_DOWN, FDelegate(this,(TDelegate)OnMoveDown) );
		MoveDownButton->OpenWindow( 1, 0, 0, 10, 10, TEXT("-") );
		NewButton = NEW WButton( this, IDPB_NEW, FDelegate(this,(TDelegate)OnNew) );
		NewButton->OpenWindow( 1, 0, 0, 10, 10, TEXT("N") );
		DuplicateButton = NEW WButton( this, IDPB_DUPLICATE, FDelegate(this,(TDelegate)OnDuplicate) );
		DuplicateButton->OpenWindow( 1, 0, 0, 10, 10, TEXT("D") );
		DeleteButton = NEW WButton( this, IDPB_DELETE, FDelegate(this,(TDelegate)OnDelete) );
		DeleteButton->OpenWindow( 1, 0, 0, 10, 10, TEXT("X") );
		PreviewButton = NEW WButton( this, IDPB_PREVIEW, FDelegate(this,(TDelegate)OnPreview) );
		PreviewButton->OpenWindow( 1, 0, 0, 10, 10, TEXT("X") );

		PlaceControl( SubActionsBox );
		PlaceControl( ScrollBar );
		PlaceControl( MoveUpButton );
		PlaceControl( MoveDownButton );
		PlaceControl( NewButton );
		PlaceControl( DuplicateButton );
		PlaceControl( DeleteButton );
		PlaceControl( PreviewButton );

		Finalize();

		NewButton->SetBitmap( NewBitmap );
		DuplicateButton->SetBitmap( DuplicateBitmap );
		DeleteButton->SetBitmap( DeleteBitmap );
		MoveUpButton->SetBitmap( MoveUpBitmap );
		MoveDownButton->SetBitmap( MoveDownBitmap );
		PreviewButton->SetBitmap( PreviewBitmap );

		// Viewport
		FName Name = TEXT("MatineeSubActions");
		Viewport = GUnrealEd->Client->NewViewport( Name );
		GUnrealEd->Level->SpawnViewActor( Viewport );
		Viewport->Actor->ShowFlags = SHOW_StandardView | SHOW_ChildWindow;
		Viewport->Actor->RendMap   = REN_MatineeSubActions;
		Viewport->Actor->Misc1 = 0;
		Viewport->Actor->Misc2 = 0;
		Viewport->Group = NAME_None;
		Viewport->MiscRes = NULL;
		Viewport->Input->Init( Viewport );

		RECT rc;
		::GetWindowRect( GetDlgItem( hWnd, IDSC_VIEWPORT_SUBACTIONS ), &rc );
		::ScreenToClient( hWnd, (POINT*)&rc.left );
		::ScreenToClient( hWnd, (POINT*)&rc.right );
		Viewport->OpenWindow( (DWORD)hWnd, 0, (rc.right - rc.left), (rc.bottom - rc.top), rc.left, rc.top );

		PropertyWindow->OpenChildWindow( IDSC_PROPERTIES );
		PropertyWindow->Root.Sorted = 0;
		PropertyWindow->SetNotifyHook( GUnrealEd );
		PropertyWindow->bAllowForceRefresh = 0;

		ToolTipCtrl = NEW WToolTip(this);
		ToolTipCtrl->OpenWindow();
		for( INT tooltip = 0 ; ToolTips_PageSubActions[tooltip].ID > 0 ; ++tooltip )
			ToolTipCtrl->AddTool( GetDlgItem( hWnd, ToolTips_PageSubActions[tooltip].ID ), ToolTips_PageSubActions[tooltip].ToolTip, tooltip );

		unguard;
	}
	void OnDestroy()
	{
		guard(WPageSubActions::OnDestroy);
		WPropertyPage::OnDestroy();

		::DestroyWindow( PropertyWindow->hWnd );
		PropertyWindow->Root.SetObjects( NULL, 0 );
		delete PropertyWindow;

		::DestroyWindow( SubActionsBox->hWnd );
		::DestroyWindow( ScrollBar->hWnd );
		::DestroyWindow( NewButton->hWnd );
		::DestroyWindow( DuplicateButton->hWnd );
		::DestroyWindow( DeleteButton->hWnd );
		::DestroyWindow( MoveUpButton->hWnd );
		::DestroyWindow( MoveDownButton->hWnd );
		::DestroyWindow( PreviewButton->hWnd );

		delete SubActionsBox;
		delete ScrollBar;
		delete NewButton;
		delete DuplicateButton;
		delete DeleteButton;
		delete MoveUpButton;
		delete MoveDownButton;
		delete PreviewButton;

		delete ToolTipCtrl;
		delete Viewport;

		DeleteObject( NewBitmap );
		DeleteObject( DuplicateBitmap );
		DeleteObject( DeleteBitmap );
		DeleteObject( MoveUpBitmap );
		DeleteObject( MoveDownBitmap );
		DeleteObject( PreviewBitmap );

		unguard;
	}
	virtual void Refresh()
	{
		guard(WPageSubActions::Refresh);
		WPropertyPage::Refresh();

		RefreshViewport();
		RefreshProperties();
		RefreshScrollBar();

		unguard;
	}
	void RefreshViewport()
	{
		guard(WPageSubActions::RefreshViewport);
		Viewport->Repaint( 1 );
		unguard;
	}
	void RefreshProperties()
	{
		guard(WPageSubActions::RefreshProperties);

		// Playing with bAllowForceRefresh like this minimizes flashing in the window when selecting
		// actors in the level itself
		PropertyWindow->bAllowForceRefresh = 1;

		UMatSubAction* SubAction = GMatineeTools.GetCurrentSubAction();
		if( SubAction )
			PropertyWindow->Root.SetObjects( (UObject**)&SubAction, 1 );
		else
			PropertyWindow->Root.SetObjects( NULL, 0 );

		PropertyWindow->bAllowForceRefresh = 0;

		unguard;
	}
	void RefreshScrollBar()
	{
		guard(WPageSubActions::RefreshScrollBar);

		if( !ScrollBar ) return;

		// Set the scroll bar to have a valid range.
		SCROLLINFO si;
		si.cbSize = sizeof(SCROLLINFO);
		si.fMask = SIF_DISABLENOSCROLL | SIF_RANGE | SIF_POS | SIF_PAGE;
		si.nPage = Viewport->SizeY;
		si.nMin = 0;
		si.nMax = GMatineeTools.SubActionScrollMax+Viewport->SizeY;
		si.nPos = GMatineeTools.SubActionScrollPos;
		SetScrollInfo( ScrollBar->hWnd, SB_CTL, &si, TRUE );

		unguard;
	}
	virtual void OnVScroll( WPARAM wParam, LPARAM lParam )
	{
		if( (HWND)lParam == ScrollBar->hWnd )
		{
			switch(LOWORD(wParam)) {

				case SB_LINEUP:
					GMatineeTools.SubActionScrollPos -= 32;
					GMatineeTools.SubActionScrollPos = Max( GMatineeTools.SubActionScrollPos, 0 );
					break;

				case SB_LINEDOWN:
					GMatineeTools.SubActionScrollPos += 32;
					GMatineeTools.SubActionScrollPos = Min( GMatineeTools.SubActionScrollPos, GMatineeTools.SubActionScrollMax );
					break;

				case SB_PAGEUP:
					GMatineeTools.SubActionScrollPos -= 64;
					GMatineeTools.SubActionScrollPos = Max( GMatineeTools.SubActionScrollPos, 0 );
					break;

				case SB_PAGEDOWN:
					GMatineeTools.SubActionScrollPos += 64;
					GMatineeTools.SubActionScrollPos = Min( GMatineeTools.SubActionScrollPos, GMatineeTools.SubActionScrollMax );
					break;

				case SB_THUMBTRACK:
					GMatineeTools.SubActionScrollPos = (short int)HIWORD(wParam);
					break;
			}

			RefreshScrollBar();
			RefreshViewport();
		}
	}
	void OnMoveUp()
	{
		guard(WPageSubActions::OnMoveUp);

		UMatAction* MA = GMatineeTools.GetCurrentAction();
		if( MA )
		{
			INT Idx = GMatineeTools.GetSubActionIdx( GMatineeTools.GetCurrentSubAction() );
			if( Idx > -1 && Idx > 0 )
				Exchange<UMatSubAction*>( MA->SubActions(Idx), MA->SubActions(Idx-1) );
		}

		RefreshViewport();

		unguard;
	}
	void OnMoveDown()
	{
		guard(WPageSubActions::OnMoveDown);

		UMatAction* MA = GMatineeTools.GetCurrentAction();
		if( MA )
		{
			INT Idx = GMatineeTools.GetSubActionIdx( GMatineeTools.GetCurrentSubAction() );
			if( Idx > -1 && Idx < MA->SubActions.Num()-1 )
				Exchange<UMatSubAction*>( MA->SubActions(Idx), MA->SubActions(Idx+1) );
		}

		RefreshViewport();

		unguard;
	}
	void OnNew()
	{
		guard(WPageSubActions::OnNew);

		// Grab all the subclasses of UMatSubAction
		GSubActionTypes.Empty();
		INT ID = IDMN_SUBACTION_BASE;
		for( TObjectIterator<UClass> It; It; ++It )
			if( It->IsChildOf(UMatSubAction::StaticClass())  && !(It->ClassFlags&CLASS_Abstract) )
			{
				new(GSubActionTypes)FSubActionType( ID, It->GetName(), *It );
				ID++;
			}

		// Create a popup menu
		HMENU menu = CreatePopupMenu();
		MENUITEMINFOA mif;

		mif.cbSize = sizeof(MENUITEMINFO);
		mif.fMask = MIIM_TYPE | MIIM_ID;
		mif.fType = MFT_STRING;

		for( INT x = 0 ; x < GSubActionTypes.Num() ; ++x )
		{
			FSubActionType* AT = &(GSubActionTypes(x));

			mif.dwTypeData = (LPSTR)TCHAR_TO_ANSI( *(AT->Name) );
			mif.wID = AT->ID;

			InsertMenuItemA( menu, 99999, FALSE, &mif );
		}

		RECT rc;
		::GetWindowRect( NewButton->hWnd, &rc );
		TrackPopupMenu( menu,
			TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
			rc.left, rc.bottom, 0,
			hWnd, NULL);
		DestroyMenu( menu );

		unguard;
	}
	void OnDuplicate()
	{
		guard(WPageSubActions::OnDuplicate);

		UMatAction* MA = GMatineeTools.GetCurrentAction();
		UMatSubAction* MSA = GMatineeTools.GetCurrentSubAction();
		if( MA && MSA )
		{
			INT Idx = GMatineeTools.GetSubActionIdx( GMatineeTools.GetCurrentSubAction() );
			Idx++;

			MSA = MA->SubActions( Idx-1 );
			UMatSubAction* NewMSA = CastChecked<UMatSubAction>(GUnrealEd->StaticConstructObject( MSA->GetClass(), GUnrealEd->Level, NAME_None, RF_Public|RF_Standalone, MSA, GError ));
			MA->SubActions.InsertZeroed( Idx );
			MA->SubActions( Idx ) = NewMSA;

			GMatineeTools.SetCurrentSubAction( NewMSA );
		}

		RefreshProperties();
		RefreshViewport();
		unguard;
	}
	void OnDelete()
	{
		guard(WPageSubActions::OnDelete);

		UMatAction* MA = GMatineeTools.GetCurrentAction();
		if( MA )
		{
			INT Idx = GMatineeTools.GetSubActionIdx( GMatineeTools.GetCurrentSubAction() );
			if( Idx != INDEX_NONE )
			{
				MA->SubActions.Remove(Idx);

				UMatSubAction* NewCurrent = NULL;
				if( MA->SubActions.Num() )
				{
					if( MA->SubActions.Num()-1 > Idx )
						Idx = 0;
					NewCurrent = MA->SubActions(Idx);
				}
				GMatineeTools.SetCurrentSubAction( NewCurrent );
			}
		}

		RefreshViewport();
		PropSheet->RefreshPages();

		unguard;
	}
	void OnCommand( INT Command )
	{
		guard(WPageSubActions::OnCommand);

		if( Command >= IDMN_SUBACTION_BASE && Command <= IDMN_SUBACTION_MAX )
		{
			UMatAction* MA = GMatineeTools.GetCurrentAction();
			if( MA )
			{
				INT Idx = GMatineeTools.GetSubActionIdx( GMatineeTools.GetCurrentSubAction() );
				Idx++;
				MA->SubActions.InsertZeroed( Idx );
				MA->SubActions(Idx) = Cast<UMatSubAction>( GUnrealEd->StaticConstructObject( GSubActionTypes(Command-IDMN_SUBACTION_BASE).Class, GUnrealEd->Level, NAME_None ) );

				GMatineeTools.SetCurrentSubAction( MA->SubActions(Idx) );
			}

			RefreshProperties();
			RefreshViewport();
		}

		WWindow::OnCommand(Command);

		unguard;
	}
	void OnPreview()
	{
		guard(WPageSubActions::OnPreview);
		PostMessageX( OwnerWindow->OwnerWindow->OwnerWindow->hWnd, WM_COMMAND, IDMN_SCENE_PREVIEW, 0 );
		unguard;
	}
};

// --------------------------------------------------------------
//
// WPageMatTools
//
// --------------------------------------------------------------

struct {
	TCHAR ToolTip[64];
	INT ID;
} ToolTips_PageMatTools[] = {
	TEXT("Add Interpolation Point at Camera Location/Rotation"), IDPB_ADD_IP,
	TEXT("Add Look Target at Camera Location"), IDPB_ADD_LOOKTARGET,
	TEXT("Show Time Window"), IDPB_TIME_WINDOW,
	NULL, 0
};

class WPageMatTools : public WPropertyPage
{
	DECLARE_WINDOWCLASS(WPageMatTools,WPropertyPage,Window)

	WPropertySheet* PropSheet;

	WButton *AddIPButton, *AddLookTargetButton, *TimeWindowButton;
	WGroupBox *MatToolsBox;

	WToolTip* ToolTipCtrl;

	// Structors.
	WPageMatTools ( WWindow* InOwnerWindow )
	:	WPropertyPage( InOwnerWindow )
	{
		AddIPButton = AddLookTargetButton = NULL;
		MatToolsBox = NULL;
	}

	virtual void OpenWindow( INT InDlgId, HMODULE InHMOD )
	{
		guard(WPageMatTools::OpenWindow);
		WPropertyPage::OpenWindow( InDlgId, InHMOD );

		// Create child controls and let the base class determine their proper positions.
		MatToolsBox = NEW WGroupBox( this, IDGP_TOOLS );
		MatToolsBox->OpenWindow( 1, 0 );
		AddIPButton = NEW WButton( this, IDPB_ADD_IP, FDelegate(this,(TDelegate)OnAddIP) );
		AddIPButton->OpenWindow( 1, 0, 0, 10, 10, TEXT("") );
		AddLookTargetButton = NEW WButton( this, IDPB_ADD_LOOKTARGET, FDelegate(this,(TDelegate)OnAddLookTarget) );
		AddLookTargetButton->OpenWindow( 1, 0, 0, 10, 10, TEXT("") );

		PlaceControl( MatToolsBox );
		PlaceControl( AddIPButton );
		PlaceControl( AddLookTargetButton );

		Finalize();

		ToolTipCtrl = NEW WToolTip(this);
		ToolTipCtrl->OpenWindow();
		for( INT tooltip = 0 ; ToolTips_PageMatTools[tooltip].ID > 0 ; ++tooltip )
			ToolTipCtrl->AddTool( GetDlgItem( hWnd, ToolTips_PageMatTools[tooltip].ID ), ToolTips_PageMatTools[tooltip].ToolTip, tooltip );

		unguard;
	}
	void OnDestroy()
	{
		guard(WPageMatTools::OnDestroy);
		WPropertyPage::OnDestroy();

		::DestroyWindow( MatToolsBox->hWnd );
		::DestroyWindow( AddIPButton->hWnd );
		::DestroyWindow( AddLookTargetButton->hWnd );

		delete MatToolsBox;
		delete AddIPButton;
		delete AddLookTargetButton;

		delete ToolTipCtrl;
		
		unguard;
	}
	virtual void Refresh()
	{
		guard(WPageMatTools::Refresh);
		WPropertyPage::Refresh();

		unguard;
	}
	void OnAddIP()
	{
		guard(WPageMatTools::OnAddIP);

		UViewport* Viewport = GUnrealEd->GetCurrentViewport();
		if( Viewport )
		{
			AInterpolationPoint* IP = Cast<AInterpolationPoint>(GUnrealEd->AddActor( GUnrealEd->Level, AInterpolationPoint::StaticClass(), Viewport->Actor->Location ));
			if( IP )
				IP->Rotation = Viewport->Actor->Rotation;
			Viewport->Repaint(1);
		}

		unguard;
	}
	void OnAddLookTarget()
	{
		guard(WPageMatTools::OnAddLookTarget);

		UViewport* Viewport = GUnrealEd->GetCurrentViewport();
		if( Viewport )
		{
			GUnrealEd->AddActor( GUnrealEd->Level, ALookTarget::StaticClass(), Viewport->Actor->Location );
			Viewport->Repaint(1);
		}

		unguard;
	}
};

// --------------------------------------------------------------
//
// WMatineeSheet
//
// --------------------------------------------------------------

class WMatineeSheet : public WWindow
{
	DECLARE_WINDOWCLASS(WMatineeSheet,WWindow,Window)

	WPropertySheet* PropSheet;
	WPageScenes* ScenesPage;
	WPageActions* ActionsPage;
	WPageSubActions* SubActionsPage;
	WPageMatTools* MatToolsPage;

	TArray<WMatineePreview*> PreviewWindows;

	// Structors.
	WMatineeSheet( FName InPersistentName, WWindow* InOwnerWindow )
	:	WWindow( InPersistentName, InOwnerWindow )
	{
		PropSheet = NULL;
	}

	// WMatineeSheet interface.
	void OpenWindow()
	{
		guard(WMatineeSheet::OpenWindow);
		MdiChild = 0;
		PerformCreateWindowEx
		(
			NULL,
			TEXT("Matinee"),
			//scion jg -- Changed to allow minimize
			WS_OVERLAPPEDWINDOW,
			0, 0,
			0, 0,
			OwnerWindow ? OwnerWindow->hWnd : NULL,
			NULL,
			hInstance
		);
 
		unguard;
	}
	void OnCreate()
	{
		guard(WMatineeSheet::OnCreate);
		WWindow::OnCreate();

		// Create the sheet
		PropSheet = NEW WPropertySheet( this, IDPS_TERRAIN_EDIT );
		PropSheet->OpenWindow( 1, 0, 0 );

		// Create the pages for the sheet
		ScenesPage = NEW WPageScenes( PropSheet->Tabs );
		ScenesPage->OpenWindow( IDPP_MAT_SCENES, GetModuleHandleA("unrealed.exe") );
		ScenesPage->PropSheet = PropSheet;
		PropSheet->AddPage( ScenesPage );

		ActionsPage = NEW WPageActions( PropSheet->Tabs );
		ActionsPage->OpenWindow( IDPP_MAT_ACTIONS, GetModuleHandleA("unrealed.exe") );
		ActionsPage->PropSheet = PropSheet;
		PropSheet->AddPage( ActionsPage );

		SubActionsPage = NEW WPageSubActions( PropSheet->Tabs );
		SubActionsPage->OpenWindow( IDPP_MAT_SUBACTIONS, GetModuleHandleA("unrealed.exe") );
		SubActionsPage->PropSheet = PropSheet;
		PropSheet->AddPage( SubActionsPage );

		MatToolsPage = NEW WPageMatTools( PropSheet->Tabs );
		MatToolsPage->OpenWindow( IDPP_MAT_TOOLS, GetModuleHandleA("unrealed.exe") );
		MatToolsPage->PropSheet = PropSheet;
		PropSheet->AddPage( MatToolsPage );

		PropSheet->SetCurrent( 0 );

		// Resize the property sheet to surround the pages properly.
		RECT rect;
		::GetClientRect( ScenesPage->hWnd, &rect );
		::SetWindowPos( hWnd, HWND_TOP, 0, 0, rect.right + 32, rect.bottom + 64, SWP_NOMOVE );

		PositionChildControls();

		// Remove the "X" button.
		//scion jg -- Changed this so I could minimize but not maximize
		LONG Style = GetWindowLongA( hWnd, GWL_STYLE );
		Style &= ~WS_MAXIMIZEBOX;
		SetWindowLongA( hWnd, GWL_STYLE, Style );

		unguard;
	}
	void OnDestroy()
	{
		guard(WMatineeSheet::OnDestroy);
		WWindow::OnDestroy();

		delete ScenesPage;
		delete ActionsPage;
		delete SubActionsPage;
		delete MatToolsPage;
		delete PropSheet;
	
		CloseAllPreviews();

		unguard;
	}
	void OnSize( DWORD Flags, INT NewX, INT NewY )
	{
		guard(WMatineeSheet::OnSize);
		WWindow::OnSize(Flags, NewX, NewY);
		PositionChildControls();
		InvalidateRect( hWnd, NULL, FALSE );
		unguard;
	}
	void PositionChildControls()
	{
		guard(WMatineeSheet::PositionChildControls);

		if( !PropSheet
				|| !::IsWindow( PropSheet->hWnd ) )
			return;

		FRect CR = GetClientRect();
		::MoveWindow( PropSheet->hWnd, 0, 0, CR.Width(), CR.Height(), 1 );

		unguard;
	}
	INT OnSetCursor()
	{
		guard(WMatineeSheet::OnSetCursor);
		WWindow::OnSetCursor();
		SetCursor(LoadCursorIdX(NULL,IDC_ARROW));
		return 0;
		unguard;
	}
	void Refresh()
	{
		guard(WMatineeSheet::Refresh);
		ASceneManager* SM = GMatineeTools.GetCurrent();
		SetText( *FString::Printf(TEXT("Matinee - %s"), SM ? *SM->Tag : TEXT("No Scene") ) );
		unguard;
	}
	void OnCommand( INT Command )
	{
		guard(WMatineeSheet::OnCommand);

		switch( Command )
		{
			case IDMN_SCENE_PREVIEW:
			{
				if( !GMatineeTools.GetCurrent() )
				{
					appMsgf(0,TEXT("You have to select a scene first."));
					return;
				}
				//scion jg -- Check for a matinee of no duration
				ASceneManager* SM = GMatineeTools.GetCurrent();
				if (SM->TotalSceneTime <= 0.0)
				{
					::MessageBox(hWnd,TEXT("This scene has no duration"),
						TEXT("Matinee Error"),MB_ICONINFORMATION);
					return;
				}
				//scion jg -- Validate all the interpolation points are set before
				// trying a preview or it will crash
				UBOOL bOk = 1;
				// Loop through each action and check its interpolation points
				for (INT nAction = 0; nAction < SM->Actions.Num() && bOk; nAction++)
				{
					bOk = SM->Actions(nAction)->IntPoint != NULL;
				}
				// All intepolation points valid?
				if (bOk == 0)
				{
					::MessageBox(hWnd,
						TEXT("Missing an interpolation point in one of the actions"),
						TEXT("Matinee Error"),MB_ICONINFORMATION);
					return;
				}
				bOk = 1;
				// Loop through each action and check its interpolation points
				for (INT nAction = 0; nAction < SM->Actions.Num() && bOk; nAction++)
				{
					for (INT nSubAction = 0;
						nSubAction < SM->Actions(nAction)->SubActions.Num() && bOk;
						nSubAction++)
					{
						USubActionOrientation* pSub = Cast<USubActionOrientation>(
							SM->Actions(nAction)->SubActions(nSubAction));
						if (pSub &&
							pSub->CamOrientation.CamOrientation == CAMORIENT_LookAtActor)
						{
							bOk = pSub->CamOrientation.LookAt != NULL;
						}
					}
				}
				if (bOk == 0)
				{
					::MessageBox(hWnd,
						TEXT("Missing the LookAt parameter for a SubActionOrientation object"),
						TEXT("Matinee Error"),MB_ICONINFORMATION);
					return;
				}
				// All intepolation points valid?
				if (bOk == 0)
				{
					::MessageBox(hWnd,
						TEXT("Missing an interpolation point in one of the actions"),
						TEXT("Matinee Error"),MB_ICONINFORMATION);
					return;
				}
				// Only after being valid should the preview go

				// If this path already has a preview window open, don't open a new one.
				for( INT x = 0 ; x < PreviewWindows.Num() ; ++x )
					if( PreviewWindows(x)->SM == GMatineeTools.GetCurrent() )
					{
						delete PreviewWindows(x);
						PreviewWindows.Remove(x);
						break;
					}

				WMatineePreview* Wnd = NEW WMatineePreview( TEXT("MatineePreview"), OwnerWindow, GMatineeTools.GetCurrent() );
				PreviewWindows.AddItem( Wnd );
				Wnd->OpenWindow();
			}
			break;

			case WM_MAT_PREVIEW_CLOSING:
			{
				WMatineePreview* wmp = (WMatineePreview*)LastlParam;
				for( INT x = 0 ; x < PreviewWindows.Num() ; ++x )
				{
					if( PreviewWindows(x) == wmp )
					{
						PreviewWindows.RemoveItem(wmp);
						delete wmp;
						break;
					}
				}
			}
			break;

			default:
				WWindow::OnCommand(Command);
				break;
		}

		unguard;
	}
	// Closes all open preview windows
	void CloseAllPreviews()
	{
		guard(WMatineeSheet::CloseAllPreviews);

		for( INT x = 0 ; x < PreviewWindows.Num() ; ++x )
		{
			WMatineePreview* pw = PreviewWindows(x);
			delete pw;
		}
		PreviewWindows.Empty();

		unguard;
	}
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
