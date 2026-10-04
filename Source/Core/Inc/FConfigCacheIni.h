/*=============================================================================
	FConfigCacheIni.h: Unreal config file reading/writing.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

/*-----------------------------------------------------------------------------
	Config cache.
-----------------------------------------------------------------------------*/

// One section in a config file.
//epic moelfke: changed from TMultiMap<FString,FString> to TMultiMap<FName,FString>
class FConfigSection : public TMultiMap<FName,FString>
{
public:
	// rjp --
	UBOOL HasQuotes( const FString& Test ) const
	{
		guardSlow(FConfigSection::HasQuotes);
		return Test.Left(1) == TEXT("\"") && Test.Right(1) == TEXT("\"");
		unguardSlow;
	}
	UBOOL operator==( const FConfigSection& Other ) const
	{
		guardSlow(FConfigSection::operator==);
		if ( Pairs.Num() != Other.Pairs.Num() )
			return 0;

		TMapBase<FName,FString>::TIterator My((TMapBase<FName,FString>&)*this), Their((TMapBase<FName,FString>&)Other);
		while ( My && Their )
		{
			if (My.Key() != Their.Key())
				return 0;

			const FString& MyValue = My.Value(), &TheirValue = Their.Value();
			if ( appStrcmp(*MyValue,*TheirValue) &&
				(!HasQuotes(MyValue) || appStrcmp(*TheirValue,*MyValue.Mid(1,MyValue.Len()-2))) &&
				(!HasQuotes(TheirValue) || appStrcmp(*MyValue,*TheirValue.Mid(1,TheirValue.Len()-2))) )
				return 0;

			++My, ++Their;
		}
		return 1;
		unguardSlow;
	}
	UBOOL operator!=( const FConfigSection& Other ) const
	{
		guardSlow(FConfigSection::operator!=);
		return ! (FConfigSection::operator==(Other));
		unguardSlow;
	}
	// -- rjp
};

// One config file.
class FConfigFile : public TMap<FString,FConfigSection>
{
public:
	UBOOL Dirty, NoSave, Quotes;
	FConfigFile()
	: Dirty( 0 )
	, NoSave( 0 )
	, Quotes( 0 )
	{}

	//
	// Parse a hex digit.
	//
	//epic moelfke: added here for proper handling of escaped characters in quoted strings in config files, see below
	static FORCEINLINE INT HexDigit(TCHAR c)
	{
		INT Result = 0;

		if (c >= '0' && c <= '9')
		{
			Result = c - '0';
		}
		else if (c >= 'a' && c <= 'f')
		{
			Result = c + 10 - 'a';
		}
		else if (c >= 'A' && c <= 'F')
		{
			Result = c + 10 - 'A';
		}
		else
		{
			Result = 0;
		}
		
		return Result;
	}

	void Read( const TCHAR* Filename )
	{
		guard(FConfigFile::Read);
		Empty();
		FString Text;
		if( appLoadFileToString( Text, Filename ) )
		{
			TCHAR* Ptr = const_cast<TCHAR*>( *Text );
			FConfigSection* CurrentSection = NULL;
			UBOOL Done = 0;
			while( !Done )
			{
				// Advance past new line characters
				while( *Ptr=='\r' || *Ptr=='\n' )
					Ptr++;

				// Store the location of the first character of this line
				TCHAR* Start = Ptr;

				// Advance the char pointer until we hit a newline character
				while( *Ptr && *Ptr!='\r' && *Ptr!='\n' )
					Ptr++;

				// If this is the end of the file, we're done
				if( *Ptr==0 )
					Done = 1;

				// Terminate the current line, and advance the pointer to the next character in the stream
				*Ptr++ = 0;

				// If the first character in the line is [ and last char is ], this line indicates a section name
				if( *Start=='[' && Start[appStrlen(Start)-1]==']' )
				{
					// Remove the brackets
					Start++;
					Start[appStrlen(Start)-1] = 0;

					// If we don't have an existing section by this name, add one
					CurrentSection = Find( Start );
					if( !CurrentSection )
						CurrentSection = &Set( Start, FConfigSection() );
				}

				// Otherwise, if we're currently inside a section, and we haven't reached the end of the stream
				else if( CurrentSection && *Start )
				{
					// Ignore any lines that don't contain a key-value pair
					TCHAR* Value = appStrstr(Start,TEXT("="));
					if( Value )
					{
						// Terminate the propertyname, advancing past the =
						*Value++ = 0;

						// If this line is delimited by quotes
						if( *Value=='\"' )
						{
							Value++;
							//epic moelfke: fixed handling of escaped characters in quoted string
							FString ProcessedValue;
							while (*Value && *Value != '\"')
							{
								if (*Value != '\\') // unescaped character
								{
									ProcessedValue += *Value++;
								}
								else if (*++Value == '\\') // escaped forward slash "\\"
								{
									ProcessedValue += '\\';
									Value++;
								}
								else if (*Value == '\"') // escaped double quote "\""
								{
									ProcessedValue += '\"';
									Value++;
								}
								else // some other escape sequence, assume it's a hex character value
								{
									ProcessedValue += HexDigit(Value[0])*16 + HexDigit(Value[1]);
									Value += 2;
								}
							}

							// Add this pair to the current FConfigSection
							CurrentSection->Add(Start, *ProcessedValue);
							
						}
						else
						{
							// Add this pair to the current FConfigSection
							CurrentSection->Add(Start, Value);
						}
					}
				}
			}
		}
		unguard;
	}
	UBOOL Write( const TCHAR* Filename )
	{
		guard(FConfigFile::Write);

		if( !Dirty || NoSave )
			return 1;

		FString Text;
		for( TIterator It(*this); It; ++It )
		{
            #if WIN32
                TCHAR *newline = TEXT("\r\n");
            #else
                TCHAR *newline = TEXT("\n");
            #endif

			/* rjp -- The Quotes member is only set when exporting .int from the editor, and the whole concept doesn't work
			very well (when you have a localized INT var, you don't want quotes, even if the FConfigFile's Quotes == 1

			Where we really run into a problem is when a string value begins with a space...since quotes are stripped from the file
			when it's read, these quotes are lost.  String values that begin with a space will be ignored the next time they are imported
			from the game. 
			
			The easiest fix for this is to check if this value begins with a space, and if so, add the quotes so that the value will
			be imported correctly in the future.
			*/
			Text += FString::Printf( TEXT("[%s]%s"), *It.Key(), newline );
			for( FConfigSection::TIterator It2(It.Value()); It2; ++It2 )
			{
				UBOOL bUseQuotes = Quotes || (**It2.Value() == ' ');
				Text += FString::Printf( TEXT("%s=%s%s%s%s"), *It2.Key(), bUseQuotes ? TEXT("\"") : TEXT(""), *It2.Value(), bUseQuotes ? TEXT("\"") : TEXT(""), newline );
			}
			Text += FString::Printf( newline );
		}

		UBOOL bResult = appSaveStringToFile( Text, Filename );
		Dirty = !bResult;

		// only clear dirty flag if write was successful
		return bResult;

		unguard;
	}
	void Dump(FOutputDevice& Ar)
	{
		guard(FConfigFile::Dump);

		TMap<FString,FConfigSection>::Dump(Ar);

		for (TMap<FString,FConfigSection>::TIterator It(*this); It; ++It)
		{
			debugf(TEXT("%s"), *It.Key());
			It.Value().Dump(Ar);
		}

		unguard;
	}
	UBOOL operator==( const FConfigFile& Other ) const
	{
		guardSlow(FConfigFile::operator==);

		if ( Pairs.Num() != Other.Pairs.Num() )
			return 0;

		for ( TMapBase<FString,FConfigSection>::TIterator It((TMapBase<FString,FConfigSection>&)*this), OtherIt((TMapBase<FString,FConfigSection>&)Other); It && OtherIt; ++It, ++OtherIt)
		{
			if ( It.Key() != OtherIt.Key() )
				return 0;

			if ( It.Value() != OtherIt.Value() )
				return 0;
		}

		return 1;

		unguardSlow;
	}

	UBOOL operator!=( const FConfigFile& Other ) const
	{
		guardSlow(FConfigFile::operator!=);
		return ! (FConfigFile::operator==(Other));

		unguardSlow;
	}
};

// Set of all cached config files.
class FConfigCacheIni : public FConfigCache, public TMap<FString,FConfigFile>
{
public:
	// Basic functions.
	FString SystemIni, UserIni;
	FConfigCacheIni()
	{}
	~FConfigCacheIni()
	{
		guard(FConfigCacheIni::~FConfigCacheIni);
#ifndef _XBOX//scion jg -- Don't write out the INI files on the xbox
		Flush( 1 );
#endif
		unguard;
	}

	/*==========================================================================================================
	function	FConfigCacheIni::ValidateFileName
	Desc:		Converts the special or short versions of .ini filenames into the full version.
				If the filename specified is User or System (special values that represent the user and system
				ini), changes the filename to the value specified on the commandline (by -ini= or -userini=),
				or nothing was specified on the command line, changes the filename to the default user/system
				.ini filenames.

				If the filename does not end with .ini, appends .ini to the filename.

	last modified by:	Ron Prestenback
	==========================================================================================================*/
	void ValidateFileName( TCHAR* Filename )
	{
		guard(FConfigCacheIni::ValidateFileName);

		if ( Filename == NULL )
			return;

		INT Len = appStrlen(Filename);
		if( Len<5 || (Filename[Len-4]!='.' && Filename[Len-5]!='.') )
			appStrcat( Filename, TEXT(".ini") );

		// Automatically translate generic filenames.
		if( appStricmp(Filename,TEXT("User.ini"))==0 )
			appStrcpy( Filename, *UserIni );
		else if( appStricmp(Filename,TEXT("System.ini"))==0 )
			appStrcpy(Filename,*SystemIni);

		unguard;
	}
	FConfigFile* Find( const TCHAR* InFilename, UBOOL CreateIfNotFound )
	{
		guard(FConfigCacheIni::Find);

		// If filename not specified, use default.
		TCHAR Filename[256];
		appStrcpy( Filename, InFilename ? InFilename : *SystemIni );

		ValidateFileName(Filename);

		// Get file.
		FConfigFile* Result = TMap<FString,FConfigFile>::Find( Filename );
		if( !Result && (CreateIfNotFound || GFileManager->FileSize(Filename)>=0)  )
		{
			Result = &Set( Filename, FConfigFile() );
			Result->Read( Filename );
		}
		return Result;

		unguard;
	}
	void Flush( UBOOL Read, const TCHAR* InFilename=NULL, const TCHAR* OutFilename=NULL )
	{
		guard(FConfigCacheIni::Flush);

		if ( InFilename )
		{
			TCHAR Filename[256];
			appStrcpy( Filename, InFilename );
			ValidateFileName(Filename);

			FConfigFile* File = Find(Filename, 0);
			if ( File )
			{

				File->Write( OutFilename ? OutFilename : Filename );
			}

			if ( Read )
			{
				Remove(Filename);
			}
		}
		else
		{
			for( TIterator It(*this); It; ++It )
			{
				if (OutFilename != NULL)
				{
					It.Value().Write( OutFilename );
				}
				else
				{
					It.Value().Write( *It.Key() );
				}
			}

			if ( Read )
			{
				Empty();
			}
		}
		unguard;
	}
	void UnloadFile( const TCHAR* Filename )
	{
		guard(FConfigCacheIni::UnloadFile);
		FConfigFile* File = Find( Filename, 0 );
		if( File )
		{
			Remove( Filename );
		}
		unguard;
	}
	void UnloadAll()
	{
		Empty();
	}
	void Detach( const TCHAR* Filename )
	{
		guard(FConfigCacheIni::Detach);
		FConfigFile* File = Find( Filename, 1 );
		if( File )
			File->NoSave = 1;
		unguard;
	}
	//scion ===========================================================
	//	SCION Function: FConfigCacheIni::GetString
	//	Author: superville
	//
	//	Description: Retrieves string from given config file for 
	//		given section and key name.
	//
	//	Input:	const TCHAR* Section	- Name of section in config file (ie "Engine.GameInfo" )
	//			const TCHAR* Key		- Name of variable value to find in section (ie "GameName")
	//				  TCHAR* Value		- Value of variable in section - Return buffer (ie "CTF")
	//				  INT	 Size		- Size of VALUE return buffer
	//			const TCHAR* Filename	- Name of file to search in (ie "Core.int")
	//	Output: TRUE if Value buffer was set successfully, FALSE otherwise
	//	Last Modified: 01/13/09 (superville)
	// ================================================================
	UBOOL GetString( const TCHAR* Section, const TCHAR* Key, TCHAR* Value, INT Size, const TCHAR* Filename )
	{
		guard(FConfigCacheIni::GetString);
		*Value = 0;
		FConfigFile* File = Find( Filename, 0 );
		if( !File )
			return 0;
		FConfigSection* Sec = File->Find( Section );
		if( !Sec )
			return 0;
		FString* PairString = Sec->Find( Key );
		if( !PairString )
			return 0;
		appStrncpy( Value, **PairString, Size );
		return 1;
		unguard;
	}
	UBOOL GetSection( const TCHAR* Section, TCHAR* Result, INT Size, const TCHAR* Filename )
	{
		guard(FConfigCacheIni::GetSection);
		*Result = 0;
		FConfigFile* File = Find( Filename, 0 );
		if( !File )
			return 0;
		FConfigSection* Sec = File->Find( Section );
		if( !Sec )
			return 0;
		TCHAR* End = Result;
		for( FConfigSection::TIterator It(*Sec); It && End-Result+appStrlen(*It.Key())+1<Size; ++It )
			End += appSprintf( End, TEXT("%s=%s"), *It.Key(), *It.Value() ) + 1;
		*End++ = 0;
		return 1;
		unguard;
	}
	TMultiMap<FName,FString>* GetSectionPrivate( const TCHAR* Section, UBOOL Force, UBOOL Const, const TCHAR* Filename )
	{
		guard(FConfigCacheIni::GetSectionPrivate);
		FConfigFile* File = Find( Filename, Force );
		if( !File )
			return NULL;
		FConfigSection* Sec = File->Find( Section );
		if( !Sec && Force )
			Sec = &File->Set( Section, FConfigSection() );
		if( Sec && (Force || !Const) )
			File->Dirty = 1;
		return Sec;
		unguard;
	}
	void SetString( const TCHAR* Section, const TCHAR* Key, const TCHAR* Value, const TCHAR* Filename )
	{
		guard(FConfigCacheIni::SetString);
		FConfigFile* File = Find( Filename, 1 );
		FConfigSection* Sec  = File->Find( Section );
		if( !Sec )
			Sec = &File->Set( Section, FConfigSection() );
		FString* Str = Sec->Find( Key );
		if( !Str )
		{
			Sec->Add( Key, Value );
			File->Dirty = 1;
		}
		else if( appStricmp(**Str,Value)!=0 )
		{
			File->Dirty = (appStrcmp(**Str,Value)!=0);
			*Str = Value;
		}
		unguard;
	}
	void EmptySection( const TCHAR* Section, const TCHAR* Filename )
	{
		guard(FConfigCacheIni::EmptySection);
		FConfigFile* File = Find( Filename, 0 );
		if( File )
		{
			FConfigSection* Sec = File->Find( Section );

		// rjp -- remove the section name if there are no more properties for this section
			if( Sec )
			{
				if ( FConfigSection::TIterator(*Sec) )
					Sec->Empty();

				File->Remove(Section);
				if (File->Num())
				{
					File->Dirty = 1;
					Flush(0, Filename);
				}
				else GFileManager->Delete(Filename);
					
			}	// -- rjp
		}
		unguard;
	}
	void Init( const TCHAR* InSystem, const TCHAR* InUser, UBOOL RequireConfig )
	{
		guard(FConfigCacheIni::Init);
		SystemIni = InSystem;
		UserIni   = InUser;
		unguard;
	}
	void Exit()
	{
		guard(FConfigCacheIni::Exit);
		Flush( 1 );
		unguard;
	}
	void Dump( FOutputDevice& Ar )
	{
		guard(FConfigCacheIni::Dump);
		Ar.Log( TEXT("Files map:") );
		TMap<FString,FConfigFile>::Dump( Ar );

		// scion sz - display filename entries
		for( TMap<FString,FConfigFile>::TIterator It(*this); It; ++It )
		{
			debugf( TEXT("%s"), *It.Key() );
			It.Value().Dump(Ar);
		}

		unguard;
	}

	// Derived functions.
	UBOOL GetString
	(
		const TCHAR* Section,
		const TCHAR* Key,
		FString&     Str,
		const TCHAR* Filename
	)
	{
		guard(FConfigCacheIni::GetString);
		TCHAR Temp[4096]=TEXT("");
		UBOOL Result = GetString( Section, Key, Temp, ARRAY_COUNT(Temp), Filename );
		Str = Temp;
		return Result;
		unguard;
	}
	const TCHAR* GetStr( const TCHAR* Section, const TCHAR* Key, const TCHAR* Filename )
	{
		guard(FConfigCacheIni::GetStr);
		TCHAR* Result = appStaticString1024();
		GetString( Section, Key, Result, 1024, Filename );
		return Result;
		unguard;
	}
	UBOOL GetInt
	(
		const TCHAR*	Section,
		const TCHAR*	Key,
		INT&			Value,
		const TCHAR*	Filename
	)
	{
		guard(FConfigCacheIni::GetInt);
		TCHAR Text[80]; 
		if( GetString( Section, Key, Text, ARRAY_COUNT(Text), Filename ) )
		{
			Value = appAtoi(Text);
			return 1;
		}
		return 0;
		unguard;
	}
	UBOOL GetFloat
	(
		const TCHAR*	Section,
		const TCHAR*	Key,
		FLOAT&			Value,
		const TCHAR*	Filename
	)
	{
		guard(FConfigCacheIni::GetFloat);
		TCHAR Text[80]; 
		if( GetString( Section, Key, Text, ARRAY_COUNT(Text), Filename ) )
		{
			Value = appAtof(Text);
			return 1;
		}
		return 0;
		unguard;
	}
	UBOOL GetBool
	(
		const TCHAR*	Section,
		const TCHAR*	Key,
		UBOOL&			Value,
		const TCHAR*	Filename
	)
	{
		guard(FConfigCacheIni::GetBool);
		TCHAR Text[80]; 
		if( GetString( Section, Key, Text, ARRAY_COUNT(Text), Filename ) )
		{
			Value
			=		!appStricmp(Text,TEXT("On"))
				||	!appStricmp(Text,TEXT("True"))
				||	!appStricmp(Text,TEXT("Yes"))
				||	!appStricmp(Text,GYes)
				||	!appStricmp(Text,GTrue)
				||	!appStricmp(Text,TEXT("1"));
			return 1;
		}
		return 0;
		unguard;
	}
	INT GetArray
	(
		const TCHAR* Section,
		const TCHAR* Key,
		TArray<FString>& out_Arr,
		const TCHAR* Filename/* =NULL  */)
	{
		guard(FConfigCacheIni::GetArray);

		out_Arr.Empty();
		FConfigFile* File = Find( Filename, 0 );
		if ( File != NULL )
		{
			FConfigSection* Sec = File->Find( Section );
			if ( Sec != NULL )
			{
				TArray<FString> RemapArray;
				Sec->MultiFind(Key, RemapArray);

				// TMultiMap::MultiFind will return the results in reverse order
				out_Arr.AddZeroed(RemapArray.Num());
				for ( INT RemapIndex = RemapArray.Num() - 1, Index = 0; RemapIndex >= 0; RemapIndex--, Index++ )
				{
					out_Arr(Index) = RemapArray(RemapIndex);
				}
			}
		}

		return out_Arr.Num();
		unguard;
	}
	void SetInt
	(
		const TCHAR* Section,
		const TCHAR* Key,
		INT			 Value,
		const TCHAR* Filename
	)
	{
		guard(FConfigCacheIni::SetInt);
		TCHAR Text[30];
		appSprintf( Text, TEXT("%i"), Value );
		SetString( Section, Key, Text, Filename );
		unguard;
	}
	void SetFloat
	(
		const TCHAR*	Section,
		const TCHAR*	Key,
		FLOAT			Value,
		const TCHAR*	Filename
	)
	{
		guard(FConfigCacheIni::SetFloat);
		TCHAR Text[30];
		appSprintf( Text, TEXT("%f"), Value );
		SetString( Section, Key, Text, Filename );
		unguard;
	}
	void SetBool
	(
		const TCHAR* Section,
		const TCHAR* Key,
		UBOOL		 Value,
		const TCHAR* Filename
	)
	{
		guard(FConfigCacheIni::SetBool);
		SetString( Section, Key, Value ? GTrue : GFalse, Filename );
		unguard;
	}
	void SetArray
	(
		const TCHAR* Section,
		const TCHAR* Key,
		const TArray<FString>& Value,
		const TCHAR* Filename /* = NULL  */
	)
	{
		guard(FConfigCacheIni::SetArray);
		FConfigFile* File = Find( Filename, 1 );
		FConfigSection* Sec  = File->Find( Section );
		if( !Sec )
			Sec = &File->Set( Section, FConfigSection() );

		if ( Sec->Remove(Key) > 0 )
			File->Dirty = 1;

		for ( INT i = 0; i < Value.Num(); i++ )
		{
			Sec->Add(Key, *Value(i));
			File->Dirty = 1;
		}
		unguard;
	}

	// Static allocator.
	static FConfigCache* Factory()
	{
		return NEW FConfigCacheIni();
	}
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

