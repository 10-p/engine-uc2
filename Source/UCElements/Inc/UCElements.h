/*=============================================================================
	UCVehicle.h:  UCElements package
	Copyright 2003 Scion Studios.  All Rights Reserved.
=============================================================================*/

#ifndef _INC_UCELEMENTS
#define _INC_UCELEMENTS

/*-----------------------------------------------------------------------------
	Includes..
-----------------------------------------------------------------------------*/
#ifndef UCELEMENTS_API
	#define UCELEMENTS_API DLL_IMPORT
#endif

#include "Engine.h"

/*-----------------------------------------------------------------------------
UCElements Public Includes
-----------------------------------------------------------------------------*/
#include "UCElementsClasses.h"

// name registration for static linking
extern "C" { void RegisterNamesUCElements(void); }

/*-----------------------------------------------------------------------------
The End.
-----------------------------------------------------------------------------*/
#endif
