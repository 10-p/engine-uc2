/*=============================================================================
	UnPlayer.h: Unreal player class.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#ifndef __UNPLAYER_H__
#define __UNPLAYER_H__

/*-----------------------------------------------------------------------------
	UPlayer.
-----------------------------------------------------------------------------*/

#include "Engine.h"
#if SUPPORTS_PRAGMA_PACK
#pragma pack (push,4)	// script mirrors are laid out with /Zp4 (as the generated headers); 64-bit pointers sit on 4
#endif

class UNetConnection;

//
// A player, the base class of UViewport (local players) and UNetConnection (remote players).
//
class ENGINE_API UPlayer : public UObject, public FOutputDevice, public FExec
{
	DECLARE_ABSTRACT_CLASS(UPlayer,UObject,CLASS_Transient|CLASS_Config,Engine)

	// Objects.
	APlayerController*	Actor;
	UInteraction*		Console;
	
	BITFIELD		bWindowsMouseAvailable:1;
	BITFIELD		bShowWindowsMouse:1;
	BITFIELD		bSuspendPrecaching:1;
	BITFIELD		bFullScreen:1; //scion capps
	FLOAT			WindowsMouseX;
	FLOAT			WindowsMouseY;
	INT				CurrentNetSpeed, ConfiguredInternetSpeed, ConfiguredLanSpeed;
	BYTE			SelectedCursor;

	class UInteractionMaster*	InteractionMaster;
    TArrayNoInit<class UInteraction*> LocalInteractions;

	class UBaseGUIController* GUIController;

	// Constructor.
	UPlayer();

	// UObject interface.
	void Serialize( FArchive& Ar );
	virtual void Destroy();

	// FExec interface.
	UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar );

	// UPlayer interface.
	virtual void ReadInput( FLOAT DeltaSeconds )=0;

	//scion jg -- Native for determing buttom depression amount
	DECLARE_FUNCTION(execGetAnalogButtonAmount);
	//scion jg -- Virtual that is overloaded by the xbox viewport
	virtual BYTE GetAnalogButtonAmount(INT ButtonIndex) { return 255; }

	DECLARE_FUNCTION(execGetGamepadID);
	virtual BYTE GetGamepadID() { return 254; }

#ifdef _XBOX
	//epic rwd - voice support
	virtual UNetConnection* GetServerConnection() { return NULL; }
#endif
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

#if SUPPORTS_PRAGMA_PACK
#pragma pack (pop)
#endif
#endif // __UNPLAYER_H__