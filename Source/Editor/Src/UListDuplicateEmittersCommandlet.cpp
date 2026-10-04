/*=============================================================================
	UListDuplicateEmittersCommandlet.cpp: Loads up the EditPackages and
			lists all the Emitter classes that are identical.
	Copyright 2005 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Matt Oelfke
=============================================================================*/

#include "EditorPrivate.h"

// returns true if the classes have duplicate emitters
static UBOOL AreDuplicateEmitterClasses(UClass* A, UClass* B)
{
	AEmitter* EmitterA = A->GetDefaultObject<AEmitter>();
	AEmitter* EmitterB = B->GetDefaultObject<AEmitter>();

	// must have same number of subemitters and that number must be greater than 0
	if (EmitterA->Emitters.Num() != EmitterB->Emitters.Num() || EmitterA->Emitters.Num() <= 0)
	{
		return false;
	}

	for (INT i = 0; i < EmitterA->Emitters.Num(); i++)
	{
		// check that either both emitters have a valid reference or both emitters don't
		if ((EmitterA->Emitters(i) != NULL) != (EmitterB->Emitters(i) != NULL))
		{
			return false;
		}
		else if (EmitterA->Emitters(i) != NULL)
		{
			// each subemitter must be of the same class
			if (EmitterA->Emitters(i)->GetClass() != EmitterB->Emitters(i)->GetClass())
			{
				return false;
			}

			// compare property values
			for (TFieldIterator<UProperty, CLASS_IsAUProperty> It(EmitterA->Emitters(i)->GetClass()); It; ++It)
			{
				if ( !It->Identical((BYTE*)EmitterA->Emitters(i) + It->Offset, (BYTE*)EmitterB->Emitters(i) + It->Offset) &&
					It->GetFName() != NAME_Outer )
				{
					return false;
				}
			}
		}
	}

	return true;
}

class UListDuplicateEmittersCommandlet : public UCommandlet
{
	DECLARE_CLASS(UListDuplicateEmittersCommandlet,UCommandlet,CLASS_Transient,Editor);
	
	void StaticConstructor()
	{
		guard(UListDuplicateEmittersCommandlet::StaticConstructor);

		LogToStdout     = 0;
		IsClient        = 1;
		IsEditor        = 1;
		IsServer        = 1;
		LazyLoad        = 0;
		ShowErrorCount  = 1;

		unguard;
	}

	INT Main(const TCHAR* Parms)
	{
		guard(UListDuplicateEmittersCommandlet::Main);

		// Create the editor class.
		UClass* EditorEngineClass = UObject::StaticLoadClass(UEditorEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.EditorEngine"), NULL, LOAD_NoFail | LOAD_DisallowFiles, NULL);
		GEditor = ConstructObject<UEditorEngine>(EditorEngineClass);
		GEditor->UseSound = 0;
        GEditor->InitEditor();

		GIsRequestingExit = 1; // Causes ctrl-c to immediately exit.

		// load all the EditPackages
		warnf(TEXT("Loading EditPackages..."));
		BeginLoad();
		for (INT i = 0; i < GEditor->EditPackages.Num(); i++)
		{
			if (!LoadPackage(NULL, *GEditor->EditPackages(i), LOAD_NoWarn))
			{
				appErrorf(TEXT("Can't find edit package '%s'"), *GEditor->EditPackages(i));
			}
		}
		EndLoad();

		// create a list of emitter classes
		warnf(TEXT("Finding Emitter classes..."));
		TArray<UClass*> EmitterClasses;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->IsChildOf(AEmitter::StaticClass()))
			{
				EmitterClasses.AddItem(*It);
			}
		}

		// search for duplicate emitters
		for (INT i = 0; i < EmitterClasses.Num(); i++)
		{
			for (INT j = i + 1; j < EmitterClasses.Num(); j++)
			{
				if (AreDuplicateEmitterClasses(EmitterClasses(i), EmitterClasses(j)))
				{
					warnf(TEXT("Class %s is identical to %s"), EmitterClasses(i)->GetPathName(), EmitterClasses(j)->GetPathName());
				}
			}
		}

		return 0;

		unguard;
	}
};
IMPLEMENT_CLASS(UListDuplicateEmittersCommandlet)

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/