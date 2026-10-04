#include "UnrealUtil.h"


IMPLEMENT_CLASS(URenameSectionCommandlet);

static void AddQuotes( FString& Str )
{
	if ( Str.Len() == 0 )
	{
		Str = TEXT("\"\"");
		return;
	}

	FString Result;

	if ( Str.Left(1) != TEXT("\"") )
		Result = TEXT("\"");

	// strip whitespace
	INT i;
	for ( i = 0; i < Str.Len(); i++ )
		if ( Str.Right(i) != TEXT(" ") )
			break;

	Result += (i > 0 ? Str.LeftChop(i) : Str);

	if ( Str.Right(1) != TEXT("\"") )
		Result += TEXT("\"");

	Str = Result;
}


INT URenameSectionCommandlet::Main( const TCHAR* Parms )
{
	guard(URenameSectionCommandlet::Main);

	GIsRequestingExit = 1; // Causes Ctrl-C to immediately exit

	TArray<FString> Files;
	Files = GFileManager->FindFiles( TEXT("../Maps/*.unr"), 1, 0 );

	FConfigCacheIni* Config = (FConfigCacheIni*)GConfig;

	TCHAR Wildcard[256];
	appSprintf( Wildcard, TEXT("*.%s"), *FURL::DefaultMapExt );

	for( INT DoCD=0; DoCD<1+(GCdPath[0]!=0); DoCD++ )
	{
		for( INT i=0; i<GSys->Paths.Num(); i++ )
		{
			if( appStrstr( *GSys->Paths(i), Wildcard ) )
			{
				TCHAR Tmp[256]=TEXT("");
				if( DoCD )
				{
					appStrcat( Tmp, GCdPath );
					appStrcat( Tmp, TEXT("System") PATH_SEPARATOR );
				}
				appStrcat( Tmp, *GSys->Paths(i) );
				*appStrstr( Tmp, Wildcard )=0;
				appStrcat( Tmp, Wildcard );

				Files += GFileManager->FindFiles(Tmp,1,0);
			}
		}
	}

	TCHAR Filename[256];
	for ( INT i = 0; i < Files.Num(); i++ )
	{
		appSprintf( Filename, TEXT("%s%s.int"), UObject::GetDefaultLocalizationFolder(), *Files(i).LeftChop(4) );
		if ( GFileManager->Exists(Filename) )
		{
			warnf(TEXT("Converting %s..."), Filename);
			FString Description = Config->GetStr(TEXT("General"), TEXT("Description"), Filename);
			Config->EmptySection(TEXT("General"), Filename);

			if ( Description == TEXT("") )
				Description = Config->GetStr(TEXT("LevelInfo"), TEXT("Description"), Filename);

			AddQuotes(Description);

			Config->SetString(TEXT("LevelInfo"), TEXT("Description"), *Description, Filename);
			if ( GFileManager->IsReadOnly(Filename) )
			{
				INT Code;
				warnf(TEXT("%s is read-only.  Attempting to checkout from perforce..."), Filename);
				void* Handle = appCreateProc( TEXT("p4"), *FString::Printf(TEXT("edit %s"), Filename) );
				while( !appGetProcReturnCode( Handle, &Code ) )
					appSleep(1);
			}
		}
	}

	return 0;

	unguard;
}