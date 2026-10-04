/*=============================================================================
	UnGUIUtils.cpp: GUI utility commandlets - they keep me sane
	Copyright 1997-2004 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Ron Prestenback.
=============================================================================*/

#include "GUI.h"
#include "UnrealUtil.h"

/*-----------------------------------------------------------------------------
	UGUIBaseCommandlet.
-----------------------------------------------------------------------------*/

void UGUIBaseCommandlet::LoadClassTree()
{
	guard(UGUIBaseCommandlet::LoadClassTree);

	Classes = new FClassTree(UGUI::StaticClass());

	warnf(TEXT(" Loading GUI.u..."));
	LoadPackage(NULL, TEXT("GUI.u"), LOAD_NoFail);

	warnf(TEXT(" Loading UCMenu.u..."));
	LoadPackage(NULL,TEXT("UCMenu.u"), LOAD_NoFail);

	warnf(TEXT(" Building class tree..."));
	for ( TObjectIterator<UClass> It; It; ++It )
		Classes->AddClass(*It);

	unguard;
}

/*-----------------------------------------------------------------------------
	UTestClassTreeCommandlet.
-----------------------------------------------------------------------------*/
INT UTestClassTreeCommandlet::Indent = 0;
INT UTestClassTreeCommandlet::Main( const TCHAR* Parms )
{
	guard(UTestClassTreeCommandlet::Main);

	GIsRequestingExit = 1;

	LoadClassTree();

	LogClasses( Classes );

	warnf(TEXT(" Found %i GUI classes."), Classes->Num());

	return 0;

	unguard;
}

void UTestClassTreeCommandlet::LogClasses( FClassTree* Node )
{
	guard(UTestClassTreeCommandlet::LogClasses);

	check(Node);

	warnf(TEXT("%s%s"), appSpc(Indent), Node->GetClass()->GetName());

	Indent += 3;

	TArray<FClassTree*> Children;
	Node->GetChildClasses(Children);

	for ( INT i = 0; i < Children.Num(); i++ )
		LogClasses(Children(i));

	Indent -= 3;

	unguard;
}

/*-----------------------------------------------------------------------------
	UVerifyStylesCommandlet.
-----------------------------------------------------------------------------*/

INT UVerifyStylesCommandlet::Main( const TCHAR* Parms )
{
	guard(UVerifyStylesCommandlet::Main);


	LoadClassTree();


	const FClassTree* StyleNode = Classes->FindNode( UGUIStyles::StaticClass() );
	const FClassTree* FontNode = Classes->FindNode( UGUIFont::StaticClass() );

	RegisterStyle(StyleNode);
	RegisterFont(FontNode);

	const FClassTree* Base = Classes->FindNode(UGUIComponent::StaticClass());

	PurgeUsedStyles(Base);
	//PurgeUsedFonts();


	warnf(TEXT("%i unused styles..."), UnusedStyles.Num());
	warnf(TEXT(""));

	for ( TMapBase<UClass*,FString>::TIterator It(UnusedStyles); It; ++It )
	{
		warnf(TEXT("%s : \"%s\""), It.Key()->GetName(), *It.Value());
	}

	warnf(TEXT(""));
	warnf(TEXT("Style declaration:"));
	warnf(TEXT(""));
	for ( INT i = 0; i  < UsedStyles.Num(); i++ )
		warnf(TEXT("\tStyleNames(%i)=\"%s\""), i, UsedStyles(i)->GetPathName());

	return 0;

	unguard;
}

void UVerifyStylesCommandlet::RegisterFont( const FClassTree* FontNode )
{
	guard(UVerifyStylesCommandlet::RegisterFont);

	check(FontNode);

	UGUIFont* Font = Cast<UGUIFont>(FontNode->GetClass()->GetDefaultObject());

	if ( Font->KeyName.Len() )
		UnusedFonts.Add(Font->GetClass(), *Font->KeyName);

	TArray<FClassTree*> Fonts;
	FontNode->GetChildClasses(Fonts);
	for ( INT i = 0; i < Fonts.Num(); i++ )
		RegisterFont(Fonts(i));

	unguard;
}

void UVerifyStylesCommandlet::RegisterStyle( const FClassTree* StyleNode )
{
	guard(UVerifyStylesCommandlet::RegisterStyle);

	check(StyleNode);

	UGUIStyles* Style = Cast<UGUIStyles>(StyleNode->GetClass()->GetDefaultObject());

	if ( Style->KeyName.Len() )
		UnusedStyles.Add(Style->GetClass(),*Style->KeyName);

	TArray<FClassTree*> Styles;
	StyleNode->GetChildClasses(Styles);

	for ( INT i = 0; i < Styles.Num(); i++ )
		RegisterStyle(Styles(i));

	unguard;
}

void UVerifyStylesCommandlet::PurgeUsedStyles( const FClassTree* Node )
{
	guard(UVerifyStylesCommandlet::PurgeLists);

	check(Node);
	UGUIComponent* Comp = Cast<UGUIComponent>(Node->GetClass()->GetDefaultObject());
	PurgeInnerStyles(Comp);

	TArray<FClassTree*> ChildClasses;
	Node->GetChildClasses(ChildClasses);

	for ( INT i = 0; i < ChildClasses.Num(); i++ )
		PurgeUsedStyles(ChildClasses(i));

	unguard;
}

void UVerifyStylesCommandlet::PurgeInnerStyles( UGUIComponent* Comp )
{
	guard(UVerifyStylesCommandlet::PurgeInnerStyles);
	
	if ( Comp == NULL )
		return;

	BYTE* Data = (BYTE*)Comp;
	UClass* Cls = Comp->GetClass();
	UGUIComponent* Ptr = NULL;

	if ( Comp->StyleName.Len() )
	{
		for ( TMapBase<UClass*,FString>::TIterator It(UnusedStyles); It; ++It )
			if ( It.Value() == Comp->StyleName )
			{
				UsedStyles.AddUniqueItem(It.Key());
				It.RemoveCurrent();
				break;
			}
	}

	UGUIListBoxBase* ListBox = Cast<UGUIListBoxBase>(Comp);
	if ( ListBox && ListBox->ItemStyleName.Len() )
	{
		for ( TMapBase<UClass*,FString>::TIterator It(UnusedStyles); It; ++It )
			if ( It.Value() == ListBox->ItemStyleName )
			{
				UsedStyles.AddUniqueItem(It.Key());
				It.RemoveCurrent();
				break;
			}
	}

	UGUIListBase* List = Cast<UGUIListBase>(Comp);
	if ( List && List->ItemStyleName.Len() )
	{
		for ( TMapBase<UClass*,FString>::TIterator It(UnusedStyles); It; ++It )
			if ( It.Value() == List->ItemStyleName )
			{
				UsedStyles.AddUniqueItem(It.Key());
				It.RemoveCurrent();
				break;
			}
	}

	for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(Cls); It; ++It )
	{
		for ( INT i = 0; i < It->ArrayDim; i++ )
		{
			if ( It->IsA(UArrayProperty::StaticClass()) )
			{
				UProperty* Prop = Cast<UArrayProperty>(*It)->Inner;
				if ( Prop->IsA(UObjectProperty::StaticClass()) && Cast<UObjectProperty>(Prop)->PropertyClass->IsChildOf(UGUIComponent::StaticClass()) )
				{
					FArray* Array = (FArray*)((BYTE*)Data + It->Offset + i * It->ElementSize);
					{
						if ( Array )
						{
							for ( INT j = 0; j < Array->Num(); j++ )
							{
								Ptr = (UGUIComponent*)( ((UObject**)Array->GetData())[j] );
								if ( Ptr )
									PurgeInnerStyles(Ptr);
							}
						}
					}
				}
			}
			else if ( It->IsA(UObjectProperty::StaticClass()) && Cast<UObjectProperty>(*It)->PropertyClass->IsChildOf(UGUIComponent::StaticClass()) )
			{
				Ptr = *(UGUIComponent**)((BYTE*)Data + It->Offset + i * It->ElementSize);
				if ( Ptr )
					PurgeInnerStyles(Ptr);
			}
		}
	}

	unguard;
}

INT UDumpFontCommandlet::Main( const TCHAR* Parms )
{
	guard(UDumpFontCommandlet::Main);

	UFont* Font = Cast<UFont>(StaticLoadObject(UFont::StaticClass(),NULL,Parms,NULL,LOAD_NoWarn,NULL));
	if ( Font == NULL )
	{
		warnf(TEXT("Failed to load Font '%s'!"), Parms);
		return 1;
	}

	if ( Font->IsRemapped )
	{
		TCHAR str[5];
		for ( TMap<_WORD,_WORD,8>::TIterator It(Font->CharRemap); It; ++It )
		{
			TCHAR Remap = It.Key();
			TCHAR RealChar = It.Value();

			FFontCharacter& Character = Font->Characters(RealChar);
			str[0] = Remap;
			str[1] = 0;
			warnf(TEXT("Character %u: %u '%s'  StartU:%i USize:%i  StartV:%i VSize:%i"), RealChar, Remap, str, Character.StartU, Character.USize, Character.StartV, Character.VSize);
		}
	}
	else
	{
		TCHAR str[5];

		for ( INT i = 0; i < Font->Characters.Num(); i++ )
		{
			if ( i != 10 && i != 13 )
			{
				FFontCharacter& Character = Font->Characters(i);
				str[0] = i;
				str[1] = 0;

				warnf(TEXT("Character %i: '%s'  StartU:%i USize:%i  StartV:%i VSize:%i"), i, str, Character.StartU, Character.USize, Character.StartV, Character.VSize);
			}
		}
	}

	return 0;

	unguard;
}

INT UCompareFontsCommandlet::Main( const TCHAR* Parms )
{
	guard(UCompareFontsCommandlet::Main);

	GIsRequestingExit = 1; // Causes ctrl-c to immediately exit.

	FString FirstFontName, SecondFontName;
	if ( !ParseToken(Parms, FirstFontName, 0) || !ParseToken(Parms, SecondFontName, 0) )
	{
		warnf(TEXT("You must specify two fonts to compare! syntax: comparefonts fontpackage1.font fontpackage2.font"));
		return 1;
	}

	UFont* FirstFont = Cast<UFont>(StaticLoadObject(UFont::StaticClass(),NULL,*FirstFontName,NULL,LOAD_NoWarn,NULL));
	UFont* SecondFont = Cast<UFont>(StaticLoadObject(UFont::StaticClass(),NULL,*SecondFontName,NULL,LOAD_NoWarn,NULL));

	if ( FirstFont == NULL )
	{
		warnf(TEXT("Failed to load Font '%s'!"), *FirstFontName);
		return 1;
	}

	if ( SecondFont == NULL )
	{
		warnf(TEXT("Failed to load Font '%s'!"), *SecondFontName);
		return 1;
	}

	warnf(TEXT("Comparing font '%s' to font '%s'"), *FirstFontName, *SecondFontName);

	if ( FirstFont->IsRemapped != SecondFont->IsRemapped )
	{
		warnf(TEXT("Fonts have mismatched mapping:  %i  %i"), FirstFont->IsRemapped, SecondFont->IsRemapped);
		return 1;
	}

	if ( FirstFont->IsRemapped )
	{
		TMap<_WORD,_WORD,8>::TIterator FirstIt(FirstFont->CharRemap);
		TMap<_WORD,_WORD,8>::TIterator SecondIt(SecondFont->CharRemap);

		TCHAR strA[2], strB[2];
		for ( ; FirstIt && SecondIt; ++FirstIt, ++SecondIt )
		{
			if ( FirstIt == NULL )
			{
				warnf(TEXT("End of first font!"));
				break;
			}

			if ( SecondIt == NULL )
			{
				warnf(TEXT("End of second font!"));
				break;
			}

			_WORD FirstCharIndex = FirstIt.Value();
			_WORD SecondCharIndex = SecondIt.Value();
			_WORD FirstCharValue = FirstIt.Key();
			_WORD SecondCharValue = SecondIt.Key();

			if ( FirstCharIndex != SecondCharIndex )
			{
				warnf(TEXT("Character index mismatch - first: %i  second: %i"), FirstCharIndex, SecondCharIndex);
			}

			if ( FirstCharValue != SecondCharValue )
			{
				warnf(TEXT("Character value mismatch for index %i - first:%i  second: %i"), FirstCharIndex, FirstCharValue, SecondCharValue);
			}

			strA[0] = FirstIt.Key();
			strA[1] = 0;

			strB[0] = SecondIt.Key();
			strB[1] = 0;
			debugf(TEXT("Character   First: %i (%s)  Second: %i (%s)"), FirstIt.Key(), strA, SecondIt.Key(), strB);
		}
	}
	else
	{
		if ( FirstFont->Characters.Num() != SecondFont->Characters.Num() )
		{
			warnf(TEXT("Fonts have unequal number of characters!  First:%i  Second:%i"), FirstFont->Characters.Num(), SecondFont->Characters.Num());
		}

		TCHAR str[5];

		INT Index = 0;
		while ( true )
		{
			if ( Index != 10 && Index != 13 )
			{
				if ( Index >= FirstFont->Characters.Num() )
				{
					warnf(TEXT("End of first font (%i)..."), Index);
					break;
				}

				if ( Index >= SecondFont->Characters.Num() )
				{
					warnf(TEXT("End of second font (%i)..."), Index);
					break;
				}

				FFontCharacter& FirstCharacter = FirstFont->Characters(Index);
				FFontCharacter& SecondCharacter = SecondFont->Characters(Index);

				str[0] = Index;
				str[1] = 0;

				if ( FirstCharacter.StartU != SecondCharacter.StartU )
				{
					warnf(TEXT("StartU mismatch for character %i (%s) - First:%i  Second:%i"), Index, str, FirstCharacter.StartU, SecondCharacter.StartU);
				}
					
				if ( FirstCharacter.StartV != SecondCharacter.StartV )
				{
					warnf(TEXT("StartV mismatch for character %i (%s) - First:%i  Second:%i"), Index, str, FirstCharacter.StartV, SecondCharacter.StartV);
				}

				if ( FirstCharacter.USize != SecondCharacter.USize )
				{
					warnf(TEXT("USize mismatch for character %i (%s) - First:%i  Second:%i"), Index, str, FirstCharacter.USize, SecondCharacter.USize);
				}

				if ( FirstCharacter.VSize != SecondCharacter.VSize )
				{
					warnf(TEXT("VSize mismatch for character %i (%s) - First:%i  Second:%i"), Index, str, FirstCharacter.VSize, SecondCharacter.VSize);
				}
			}

			Index++;
		}
	}

	return 0;

	unguard;
}

IMPLEMENT_CLASS(UGUIBaseCommandlet);
IMPLEMENT_CLASS(UTestClassTreeCommandlet);
IMPLEMENT_CLASS(UVerifyStylesCommandlet);
IMPLEMENT_CLASS(UDumpFontCommandlet);
IMPLEMENT_CLASS(UCompareFontsCommandlet);
