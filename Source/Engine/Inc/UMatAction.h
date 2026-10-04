/*=============================================================================
	UMatAction.h.
	Copyright 2001 Epic Games, Inc. All Rights Reserved.
=============================================================================*/
	
virtual void PostEditChange();
virtual void PostLoad();

//scion jg -- Added so actions can be ticked too
virtual UBOOL Update( FLOAT InScenePct, class ASceneManager* InSM );

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
