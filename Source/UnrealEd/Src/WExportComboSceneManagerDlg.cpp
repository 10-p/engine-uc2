/*=============================================================================
	WExportComboSceneManager.h:
	Copyright 2003 Scion Studios. All Rights Reserved.
=============================================================================*/

#include "UnrealEd.h"

#include "WExportComboSceneManagerDlg.h"

/* scion ======================================================================
 * WExportComboSceneManagerDlg::WExportComboSceneManagerDlg
 * Author: jg
 * 
 * Constructor.
 *
 *
 * input: pOwner WWindow The parent window pointer for this dialog
 *
 * ============================================================================
 */
WExportComboSceneManagerDlg::WExportComboSceneManagerDlg(WWindow* pOwner) :
	WDialog(FName(TEXT("ExportComboSceneManager")),IDD_EXPORTCOMBOMANAGER,
	pOwner), OkButton(this,IDOK,FDelegate(this,(TDelegate)OnOk)),
	CancelButton(this,IDCANCEL,FDelegate(this,(TDelegate)EndDialogFalse))
{
}

/* scion ======================================================================
 * WExportComboSceneManagerDlg::OnOk
 * Author: jg
 * 
 * Validates that the user filled in all of the information. Stores the valid
 * info in the member vars.
 *
 * ============================================================================
 */
void WExportComboSceneManagerDlg::OnOk(void)
{
	guard(WExportComboSceneManagerDlg::OnOk);
	char szBuffer[MAX_PATH];
	// Get the data from the control
	::GetDlgItemTextA(hWnd,IDC_PACKAGE,szBuffer,MAX_PATH-1);
	// Copy the results into an engine friendly format
	PackageName = ANSI_TO_TCHAR(szBuffer);
	// Get the data from the control
	::GetDlgItemTextA(hWnd,IDC_CLASSNAME,szBuffer,MAX_PATH-1);
	// Copy the results into an engine friendly format
	ClassName = ANSI_TO_TCHAR(szBuffer);
	// Now verify that the user set this up correctly
	if (ClassName.Len() != 0 && PackageName.Len() != 0)
	{
		EndDialogTrue();
	}
	else
	{
		::MessageBoxA(hWnd,"Please enter both a package name and a class name",
			"Missing information",MB_OK | MB_ICONEXCLAMATION);
	}
	unguard;
}
