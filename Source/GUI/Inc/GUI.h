/*=============================================================================
	GUI.h: Interface header for GUI package
	Copyright 1997-2004 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Brian Ladd
	* Revised by Ron Prestenback
=============================================================================*/
#ifndef __GUI_H__
#define __GUI_H__

#include "Engine.h"

#ifndef GUI_API
#define GUI_API DLL_IMPORT
#endif	// GUI_API

extern "C" { void RegisterNamesGUI(void); }

#if SUPPORTS_PRAGMA_PACK
#pragma pack(push,4)
#endif	// SUPPORTS_PRAGMA_PACK

struct FGUIListElem
{
	FString item;
	UObject* ExtraData;
	FString ExtraStrData;
};

#if SUPPORTS_PRAGMA_PACK
#pragma pack(pop)
#endif	// SUPPORTS_PRAGMA_PACK

#if __STATIC_LINK
  class UUIXInteraction;
  #include "GUIClasses.h"
  #include "UnUIXInteraction.h"
#else
  #include "GUIClasses.h"
#endif	// __STATIC_LINK

#if __STATIC_LINK
#define NAMES_ONLY
#define NATIVE_DEFS_ONLY
#define AUTOGENERATE_NAME(name)
#define AUTOGENERATE_FUNCTION(cls,idx,name)
#include "GUIClasses.h"
#undef AUTOGENERATE_FUNCTION
#undef AUTOGENERATE_NAME
#undef NATIVE_DEFS_ONLY
#undef NAMES_ONLY
#endif	// __STATIC_LINK
#endif // __GUI_H__
