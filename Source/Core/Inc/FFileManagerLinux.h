/*=============================================================================
	FFileManagerLinux.h: Unreal Linux based file manager.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	engine-uc2 (M1): this was a stale UT2003-era stdio file manager (the
	original is in git history and in epic-sources). The portable file manager
	is now engine-ut2004's FFileManagerUnix.h; this header keeps the name the
	launchers include (UCC.cpp, SDLLaunch.cpp: `FFileManagerLinux FileManager;`).
=============================================================================*/

#ifndef _INCL_FFILEMANAGERLINUX_H_
#define _INCL_FFILEMANAGERLINUX_H_

#include "FFileManagerUnix.h"

typedef FFileManagerUnix FFileManagerLinux;

#endif

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
