/*=============================================================================
Progress : Progress indicator for the Package Builder

Revision history:
* Created by Joel Van Eenwyk

=============================================================================*/

class WDlgProgressBuilder : public WDialog
{
	DECLARE_WINDOWCLASS(WDlgProgressBuilder,WDialog,UnrealEd)

	WProgressBar progressFinal;
	WProgressBar progressMaterial;
	WProgressBar progressStaticMesh;

	// Variables.

	// Constructor.
	WDlgProgressBuilder( UObject* InContext, WWindow* InOwnerWindow )
		:	WDialog				( TEXT("Progress"), IDDIALOG_PROGRESS, InOwnerWindow )
		,	progressFinal		( this, IDPG_PROGRESS_FINAL )
		,	progressMaterial	( this, IDPG_PROGRESS_MATERIAL )
		,	progressStaticMesh	( this, IDPG_PROGRESS_STATICMESH )
	{
	}

	// WDialog interface.
	void OnInitDialog()
	{
		guard(WDlgProgressBuilder::OnInitDialog);
		WDialog::OnInitDialog();
		unguard;
	}
	virtual void DoModeless( UBOOL bShow )
	{
		guard(WDlgProgressBuilder::DoModeless);
		_Windows.AddItem( this );
		hWnd = CreateDialogParamA( hInstance, MAKEINTRESOURCEA(IDDIALOG_PROGRESS_BUILDER), OwnerWindow?OwnerWindow->hWnd:NULL, (DLGPROC)StaticDlgProc, (LPARAM)this);
		if( !hWnd )
			appGetLastError();
		Show( bShow );
		unguard;
	}
	void OnCancel()
	{
	}
};

/*-----------------------------------------------------------------------------
The End.
-----------------------------------------------------------------------------*/