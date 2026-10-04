/*=============================================================================
	UnInteractionMaster.h: Unreal UModel definition.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Joe Wilcox
=============================================================================*/

UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar );
void DisplayCopyright(void);
int MasterProcessKeyType(EInputKey iKey, TCHAR Unicode );
int MasterProcessKeyEvent(EInputKey iKey, EInputAction State, FLOAT Delta );
int MasterProcessJoyInput(INT ControlIndex, EInputKey iKey, FLOAT Axis);
void MasterProcessTick(float DeltaTime);
// scion sz - pass viewport instead of canvas
void MasterProcessPreRender(UViewport* Viewport);
void MasterProcessPostRender(UViewport* Viewport);
void MasterProcessMessage(const FString& Msg, FLOAT MsgLife);

UBaseGUIController* GetGUIController( UViewport* InViewport );

