/*=============================================================================
	FFileManagerLinux.h: Unreal Linux based file manager.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	The file manager of every non-MSVC target, MinGW included, is FFileManagerUnix.h;
	this header keeps the name the launchers include (UCC.cpp, SDLLaunch.cpp:
	`FFileManagerLinux FileManager;`).
=============================================================================*/

#ifndef _INCL_FFILEMANAGERLINUX_H_
#define _INCL_FFILEMANAGERLINUX_H_

#include "FFileManagerUnix.h"

typedef FFileManagerUnix FFileManagerLinux;

#endif

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
