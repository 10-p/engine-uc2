/*=============================================================================
	UCComboSceneManager.h:
	Copyright 2003 Scion Studios. All Rights Reserved.
=============================================================================*/

// Updates the position of the combo manager if it is attached to another actor
virtual UBOOL Tick(FLOAT DeltaTime,enum ELevelTick TickType);

// Used to adjust the viewer position based upon the attached actor and the
// relative location that the combo manager was spawned at.
virtual void UpdateViewerLocation(FLOAT InScenePct);

// Takes an existing scene manager and copies the properties so that this
// one can be exported out to a script class.
void CopySceneManager(ASceneManager* pSceneManager);