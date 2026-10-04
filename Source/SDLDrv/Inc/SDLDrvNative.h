/*=============================================================================
	SDLDrvNative.h: Native function lookup table for static libraries.
	Copyright 2000 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Daniel Vogel (based on XDrv).

=============================================================================*/

#ifndef SDLDRVNATIVE_H
#define SDLDRVNATIVE_H

#if __STATIC_LINK

/* No native execs. */

#define AUTO_INITIALIZE_REGISTRANTS_SDLDRV \
	USDLClient::StaticClass(); \
	USDLViewport::StaticClass();

#endif

#endif
