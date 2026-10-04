/*=============================================================================
	ULevelSummary.h: Level summary.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

	// Constructors.
	ULevelSummary()
	{}

	// UObject interface.
	void PostLoad()
	{
		guard(ULevelSummary::PostLoad);
		Super::PostLoad();
		const TCHAR* Text=NULL;
		//scion jg -- Changed to use the globally set language by defualt
		Text = Localize( TEXT("LevelInfo0"), TEXT("Title"), GetOuter()->GetName(), UObject::GetLanguage(), 1 );
		if( *Text )
			Title = Text;
		unguard;
	}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

