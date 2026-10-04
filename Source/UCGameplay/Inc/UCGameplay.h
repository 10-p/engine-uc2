/*=============================================================================
	UCGameplay.h:  UCGameplay package
	Copyright 2003 Scion Studios.  All Rights Reserved.
=============================================================================*/

#ifndef _INC_UCGAMEPLAY
#define _INC_UCGAMEPLAY

/*-----------------------------------------------------------------------------
	Includes..
-----------------------------------------------------------------------------*/
#ifndef UCGAMEPLAY_API
	#define UCGAMEPLAY_API DLL_IMPORT
#endif

#include "Engine.h"
#include "UnrealGame.h"

/*-----------------------------------------------------------------------------
UCGameplay Public Includes
-----------------------------------------------------------------------------*/
#include "UCCameraEffects.h"
#include "UCGameplayClasses.h"

// name registration for static linking
extern "C" { void RegisterNamesUCGameplay(void); }

/*-----------------------------------------------------------------------------
The End.
-----------------------------------------------------------------------------*/
#endif