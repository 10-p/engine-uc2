/*=============================================================================
	XBoxAudio.h: Unreal XBox Audio public header file.
	Copyright 1997-2001 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Daniel Vogel.
=============================================================================*/

#ifndef _INC_XBOXAUDIO
#define _INC_XBOXAUDIO

/*----------------------------------------------------------------------------
	API.
----------------------------------------------------------------------------*/

#ifndef XBOXAUDIO_API
	#define XBOXAUDIO_API DLL_IMPORT
#endif

/*-----------------------------------------------------------------------------
	Dependencies.
-----------------------------------------------------------------------------*/

#pragma pack (push,8)
#include "dsound.h"
#include "dsstdfx.h"
#pragma pack (pop)

#include "Core.h"
#include "Engine.h"
#include "UnRender.h"

/*-----------------------------------------------------------------------------
	Global variables.
-----------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------------
	Audio compiler specific includes.
-----------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------------
	Audio public includes.
-----------------------------------------------------------------------------*/

#include "XACTAudioSubsystem.h"
#include "XHVVoiceSubsystem.h"


#define AUTO_INITIALIZE_REGISTRANTS_XBOXAUDIO	\
	UXACTAudioSubsystem::StaticClass(); \
	UXHVVoiceSubsystem::StaticClass(); \
	

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
#endif
