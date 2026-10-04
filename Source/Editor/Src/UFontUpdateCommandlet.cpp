/*=============================================================================
	UFontUpdateCommandlet.cpp: Unreal skeletal mesh batch importer
	Copyright 2002 Secret Level. All Rights Reserved.

Revision history:
	* Created by Josh Adams
=============================================================================*/

#include "EditorPrivate.h"
/*-----------------------------------------------------------------------------
	UConformCommandlet.
-----------------------------------------------------------------------------*/

class UFontUpdateCommandlet : public UCommandlet
{
	DECLARE_CLASS(UFontUpdateCommandlet,UCommandlet,CLASS_Transient,Editor);
	void StaticConstructor()
	{
		guard(UFontUpdateCommandlet::StaticConstructor);

		LogToStdout     = 0;
		IsClient        = 1;
		IsEditor        = 1;
		IsServer        = 1;
		LazyLoad        = 1;
		ShowErrorCount  = 1;

		unguard;
	}
	INT Main( const TCHAR* Parms )
	{
		guard(UBatchExportCommandlet::Main);

		GIsCriticalError = 0;

		FString Pkg;
		FString TextureFile;
		FString TexFileDir;
		if( !ParseToken(Parms, Pkg,0) )
		{
			warnf(TEXT("Texture package not specified"));
			return 1;
		}
		if( !ParseToken(Parms, TexFileDir,0) )
		{
			warnf(TEXT("Directory containing %s not specified"), *TextureFile);
			return 1;
		}
		if( !ParseToken(Parms, TextureFile,0) )
		{
			warnf(TEXT("Replacement Texture not specified"));
			return 1;
		}
		FString TextureName;
		TextureName = TextureFile.LeftChop(4);

		warnf( TEXT("Updating a %s with %s/%s..."), *TextureName, *TexFileDir, *TextureFile );

		UObject* TexPackage = LoadPackage(NULL, *Pkg, LOAD_NoWarn);
		if (!TexPackage)
		{
			warnf(TEXT("Unable to open %s"), *Pkg);
			return 1;
		}

		// get original texture
		UTexture* OldTexture = (UTexture*)UObject::StaticFindObject(UTexture::StaticClass(), ANY_PACKAGE, *TextureName);
		if (!OldTexture)
		{
			warnf(TEXT("Unable to find %s in %s"), *TextureName, *Pkg);
			return 1;
		}

		warnf( TEXT("Found %s..."), OldTexture->GetFullName());

		// make a new texture
		UClass* EditorEngineClass = UObject::StaticLoadClass( UEditorEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.EditorEngine"), NULL, LOAD_NoFail | LOAD_DisallowFiles, NULL );
		GEditor  = ConstructObject<UEditorEngine>( EditorEngineClass );
		GEditor->UseSound = 0;

		GEditor->Exec( *FString::Printf(TEXT("TEXTURE IMPORT FILE=\"%s\\%s\" NAME=\"TEMPFONTTEX\" PACKAGE=\"TEMPFONTPKG\" MIPS=1 MASKED=1 ALPHATEXTURE=0 COMPRESSION=%i"), *TexFileDir, *TextureFile, OldTexture->Format ) );
		UTexture* NewTexture = (UTexture*) UObject::StaticFindObject( UTexture::StaticClass(), ANY_PACKAGE, TEXT("TEMPFONTTEX"));
		if (!NewTexture)
		{
			warnf(TEXT("Unable to find TEMPFONTTEX in TEMPFONTPKG"));
			return 1;
		}

		if (OldTexture->Format != NewTexture->Format)
		{
			warnf(TEXT("Incompatible texture formats!"));
			return 1;
		}

		// copy the actual texture data over
		OldTexture->Mips = NewTexture->Mips;
		if (OldTexture->Palette && NewTexture->Palette)
			OldTexture->Palette->Colors = NewTexture->Palette->Colors;

		warnf( TEXT("Saving... %s"), TexPackage );
		SavePackage( TexPackage, NULL, RF_Standalone, *Pkg, GError, NULL );

		GIsRequestingExit=1;
		return 0;
		unguard;
	}
};
IMPLEMENT_CLASS(UFontUpdateCommandlet)

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
