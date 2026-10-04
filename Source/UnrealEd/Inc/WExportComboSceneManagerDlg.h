/*=============================================================================
	WExportComboSceneManager.h:
	Copyright 2003 Scion Studios. All Rights Reserved.
=============================================================================*/

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * This class is the base class for spawned combo camera manipulation. It
 * spawns in any interpolation points that are needed with positions relative
 * to this actor. It then updates all actions to use the correct interp points.
 * If attached to an actor, it will adjust all positions relative to the
 * attached actor as it moves through the world.
 *
 * ============================================================================
 */
class WExportComboSceneManagerDlg : public WDialog
{
	DECLARE_WINDOWCLASS(WExportComboSceneManagerDlg,WDialog,UnrealEd)

private:
	// Contains the name of the package the file is being saved in
	FString PackageName;
	// Contains the name of the class to save the file as
	FString ClassName;
	// Button objects so that their delegates can call our funcs when pressed
	WButton OkButton;
	WButton CancelButton;

public:
	// Constructor.
	WExportComboSceneManagerDlg(WWindow* pOwner);

	// Handles the user selecting the OK button. Populates & validates the
	// names.
	void OnOk(void);

// Accessors
	// Gets the package name
	const TCHAR* GetPackageCtlText(void) { return *PackageName; }
	// Gets the class name
	const TCHAR* GetClassCtlText(void) { return *ClassName; }
};
