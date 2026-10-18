/*=============================================================================
	UnPrefab.h: Unreal prefab related classes.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Warren Marshall
=============================================================================*/

/*-----------------------------------------------------------------------------
	Constants.
-----------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------------
	UPrefab
-----------------------------------------------------------------------------*/

// A prefab.
//
class EDITOR_API UPrefab : public UObject
{
	// Package Editor, not Engine: the package Epic's Editor DLL registered it under (IMPLEMENT_CLASS uses
	// the DLL's GPackage there), so a prefab saved by Epic's UnrealEd imports Editor.Prefab. The static link takes this
	// argument. No shipped package or ini names the class.
	DECLARE_CLASS(UPrefab,UObject,CLASS_SafeReplace,Editor)

	// Variables.
	FName		FileType;
	FString T3DText;

	// Constructor.
	UPrefab()
	{
	}

	// UObject interface.
	void Serialize( FArchive& Ar );
	void Destroy();
	void PostLoad();
};

/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/
