/*=============================================================================
	UnrealUtil.cpp: UnrealUtil Implementation.
	Copyright 1997-2004 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Ron Prestenback.
=============================================================================*/
#include "UnrealUtil.h"

IMPLEMENT_PACKAGE(UnrealUtil);

#if 0
#define NAMES_ONLY
#define AUTOGENERATE_NAME(name) UNREALUTIL_API FName GUI_##name;
#define AUTOGENERATE_FUNCTION(cls,idx,name) IMPLEMENT_FUNCTION(cls,idx,name)
#include "UnrealUtilClasses.h"
#undef AUTOGENERATE_FUNCTION
#undef AUTOGENERATE_NAME
#undef NAMES_ONLY

#define NATIVES_ONLY
#define NAMES_ONLY
#define AUTOGENERATE_NAME(name)
#define AUTOGENERATE_FUNCTION(cls,idx,name)
#include "UnrealUtilClasses.h"
#undef AUTOGENERATE_FUNCTION
#undef AUTOGENERATE_NAME
#undef NATIVES_ONLY
#undef NAMES_ONLY

void RegisterNamesUnrealUtil()
{
	#define NAMES_ONLY
	#define AUTOGENERATE_NAME(name) extern UNREALUTIL_API FName UNREALUTIL_##name; UNREALUTIL_##name=FName(TEXT(#name),FNAME_Intrinsic);
	#define AUTOGENERATE_FUNCTION(cls,idx,name)
	#include "UnrealUtilClasses.h"
	#undef DECLARE_NAME
	#undef NAMES_ONLY
}

#if !__STATIC_LINK
	struct FUnrealUtilInitManager
	{
		FUnrealUtilInitManager()
		{
			RegisterNamesUnrealUtil();
		}
	} UnrealUtilInitManager;
#endif	// !__STATIC_LINK
#endif

