/*=============================================================================
	UFontKerningCommandlet.cpp: Unreal skeletal mesh batch importer
	Copyright 2002 Secret Level. All Rights Reserved.

Revision history:
	* Created by Josh Adams - UFOntUpdateCommanlet
	* Pirated by Brian Ladd (Scion) to mess with Kerning
=============================================================================*/

#include "EditorPrivate.h"
/*-----------------------------------------------------------------------------
	UFontKerningCommandlet.
-----------------------------------------------------------------------------*/

class UFontKerningCommandlet : public UCommandlet
{
	DECLARE_CLASS(UFontKerningCommandlet,UCommandlet,CLASS_Transient,Editor);
	void StaticConstructor()
	{
		guard(UFontKerningCommandlet::StaticConstructor);

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
		FString FontName;
		FString newKerningString;
		UBOOL bSetKerning = false;
		INT	newKerning = 0;

		if( !ParseToken(Parms, Pkg, 0) )
		{
			warnf(TEXT("Texture package not specified"));
			return 1;
		}
		if( !ParseToken(Parms, FontName, 0) )
		{
			warnf(TEXT("Font name in  %s not specified"), *Pkg);
			return 1;
		}
		if( ParseToken(Parms, newKerningString, 0) )
		{
			bSetKerning = true;
			newKerning = appAtoi(*newKerningString);
		}

		warnf( TEXT("Kerning processing on %s"), *FontName );

		UObject* TexPackage = LoadPackage(NULL, *Pkg, LOAD_NoWarn);
		if (!TexPackage)
		{
			warnf(TEXT("Unable to open %s"), *Pkg);
			return 1;
		}
		
		warnf( TEXT("Package: %s"), TexPackage->GetFullName());
		
		// get the font
		UFont * OldFont = (UFont *)UObject::StaticFindObject(UFont::StaticClass(), TexPackage, *FontName);
		//FontArrayFonts(Index) = Cast<UFont>( StaticLoadObject( UFont::StaticClass(), NULL, *FontArrayNames(Index), NULL, LOAD_NoWarn, NULL ) );
		if (!OldFont)
		{
			warnf(TEXT("Unable to find %s in %s"), *FontName, *Pkg);
			return 1;
		}

		warnf( TEXT("Kerning %s = %d"), OldFont->GetFullName(), OldFont->Kerning);

		// Are we changing the kerning?
		if (bSetKerning) {
			OldFont->Kerning = newKerning;
			warnf( TEXT("New Kerning %s = %d"), OldFont->GetFullName(), OldFont->Kerning);
			warnf( TEXT("Saving... %s"), *Pkg );
			SavePackage( TexPackage, NULL, RF_Standalone, *Pkg, GError, NULL );
		}
		GIsRequestingExit=1;
		return 0;
		unguard;
	}
};
IMPLEMENT_CLASS(UFontKerningCommandlet)

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
