/*=============================================================================
	GUI.cpp: GUI package implementation
	Copyright 1997-2004 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Brian Ladd
	* Revised by Ron Prestenback
=============================================================================*/
#if __STATIC_LINK
#if !defined(_XBOX) && defined(_WIN32)
#include <wtypes.h>
#endif
#endif

#include "GUI.h"

IMPLEMENT_PACKAGE(GUI);

#define NAMES_ONLY
#define AUTOGENERATE_NAME(name) GUI_API FName GUI_##name;
#define AUTOGENERATE_FUNCTION(cls,idx,name) IMPLEMENT_FUNCTION(cls,idx,name)
#include "GUIClasses.h"
#undef AUTOGENERATE_FUNCTION
#undef AUTOGENERATE_NAME
#undef NAMES_ONLY

// sjs --- import natives
#define NATIVES_ONLY
#define NAMES_ONLY
#define AUTOGENERATE_NAME(name)
#define AUTOGENERATE_FUNCTION(cls,idx,name)
#include "GUIClasses.h"
#undef AUTOGENERATE_FUNCTION
#undef AUTOGENERATE_NAME
#undef NATIVES_ONLY
#undef NAMES_ONLY
// --- sjs

void RegisterNamesGUI()
{
	#define NAMES_ONLY
	#define AUTOGENERATE_NAME(name) extern GUI_API FName GUI_##name; GUI_##name=FName(TEXT(#name),FNAME_Intrinsic);
	#define AUTOGENERATE_FUNCTION(cls,idx,name)
	#include "GUIClasses.h"
	#undef DECLARE_NAME
	#undef NAMES_ONLY
}

#if !__STATIC_LINK
	struct FGUIInitManager
    {
        FGUIInitManager()
        {
			RegisterNamesGUI();
        }
    } GUIInitManager;
#endif


