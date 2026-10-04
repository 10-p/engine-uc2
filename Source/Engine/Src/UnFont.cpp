/*=============================================================================
	UnFont.cpp: Unreal font code.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "Engine.h"
#include "EnginePrivate.h"
//#include "UnRender.h"

/*------------------------------------------------------------------------------
	OLD font page support.
------------------------------------------------------------------------------*/

struct FOLDFontCharacter
{
	// Variables.
	INT StartU, StartV;
	INT USize, VSize;

	// Serializer.
	friend FArchive& operator<<( FArchive& Ar, FOLDFontCharacter& Ch )
	{
		guard(FOLDFontCharacter<<);
		return Ar << Ch.StartU << Ch.StartV << Ch.USize << Ch.VSize;
		unguard;
	}
};

//
// A font page.
//
struct FOLDFontPage
{
	// Variables.
	UTexture* Texture;
	TArray<FOLDFontCharacter> Characters;

	// Serializer.
	friend FArchive& operator<<( FArchive& Ar, FOLDFontPage& Ch )
	{
		guard(FOLDFontPage<<);
		return Ar << Ch.Texture << Ch.Characters;
		unguard;
	}
};

/*------------------------------------------------------------------------------
	UFont implementation.
------------------------------------------------------------------------------*/

UFont::UFont()
{}

void UFont::Serialize( FArchive& Ar )
{
	guard(UFont::Serialize);
	Super::Serialize( Ar );
	UBOOL GSavedLazyLoad = GLazyLoad;
	GLazyLoad = 1;

	if( Ar.Ver() < 122 )
	{
		TArray<FOLDFontPage> OldPages;
		INT CharactersPerPage;
		Ar << OldPages << CharactersPerPage;

		// convert old format.
		for( INT i=0;i<OldPages.Num();i++ )
		{
			Textures.AddItem( OldPages(i).Texture );
			for( INT c=0;c<OldPages(i).Characters.Num();c++ )
			{
                INT j = Characters.AddZeroed();
				Characters(j).TextureIndex = i;
				Characters(j).StartU = OldPages(i).Characters(c).StartU;
				Characters(j).StartV = OldPages(i).Characters(c).StartV;
				Characters(j).USize  = OldPages(i).Characters(c).USize;
				Characters(j).VSize  = OldPages(i).Characters(c).VSize;
			}	
		}
	}
	else
	{
		Ar << Characters << Textures;
	}

	if( Ar.Ver() >= 119 )
		Ar << Kerning;

#ifndef _XBOX
	if( !GLazyLoad )
    {
		for( INT t=0;t<Textures.Num();t++ )
			if( Textures(t) )
				for( INT i=0; i<Textures(t)->Mips.Num(); i++ )
					Textures(t)->Mips(i).DataArray.Load();
    }
#endif

	GLazyLoad = GSavedLazyLoad;
	if( Ar.Ver() >= 69 )
		Ar << CharRemap << IsRemapped;
	else
		IsRemapped = 0;
	unguardobj;
}

void UFont::GetCharSize(TCHAR InCh, FLOAT& Width, FLOAT& Height)
{
	guardSlow(UFont::GetCharSize);

	Width=0;
	Height=0;

	INT Ch = (TCHARU)RemapChar(InCh);
	if ( Ch >= Characters.Num() )
		Ch = NULLCHARACTER;

	FFontCharacter &Char = Characters(Ch);
	Width = Char.USize;
	Height = Char.VSize;

	unguardSlow;
}

void UFont::StrLen(const TCHAR* Text, FLOAT& XL, FLOAT& YL, FLOAT ScaleX, FLOAT ScaleY, INT SpaceX )
{
	XL = 0;
	YL = 0;	
	FLOAT interCharSpace = 0;
	FLOAT interCharDelta = SpaceX + Kerning;
	for( INT i=0; Text[i]; ++i)
	{
		FLOAT W, H;
		GetCharSize(Text[i], W, H );

		if ( Text[i+1] )
			W = (W + interCharSpace) * ScaleX;
		else W *= ScaleX;

		H *= ScaleY;

		XL += W;

		if(YL < H)
			YL = H;	

		interCharSpace += interCharDelta; // initially 0; then interCharDelta's initial value
		interCharDelta -= interCharDelta; // initially SpaceX + Kerning; then 0 from then on
		// Done to eliminate an if statement in here (no branching inside the for) and make
		// sure only spaces between the characters are counted
	}
} // StrLen

IMPLEMENT_CLASS(UFont);

/*------------------------------------------------------------------------------
	The End.
------------------------------------------------------------------------------*/

