/*=============================================================================
	UCC.cpp: Unreal command-line launcher.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Tim Sweeney.
=============================================================================*/

#if _MSC_VER	// MinGW takes the portable path (FFileManagerUnix, POSIX threading, argv)
	#include <windows.h>
#else
	#include <errno.h>
	#include <sys/stat.h>
#endif
#include <malloc.h>
#include <stdio.h>

// Core and Engine
#include "Engine.h"

#if __STATIC_LINK

// Extra stuff for static links for Engine.
//#include "UnCon.h"
#include "UnRender.h"
#include "UnNet.h"

// NullNetDriver static stuff
//#include "NullNetDriver.h"

#if !defined(_MSC_VER)
// Every package linked into the portable ucc, as XboxLaunch/Src/Launch.cpp does for the static XBE.
#include "ULoadSaveManager.h"
#include "UnNullRenderDevice.h"
#include "UnIpDrv.h"
#include "UnTcpNetDriver.h"
#include "UnTcpNetDriverXbox.h"
#include "UnIpDrvCommandlets.h"
#include "UWeb.h"
#include "UWebNative.h"
#include "UnrealGame.h"
#include "UCElements.h"
#include "UCGameplay.h"
#include "GUI.h"
#if UC2_WITH_UC2ALAUDIO	// the cooked-audio check commandlet (UC2ALAudio.UC2AudioCheck)
#include "UC2ALAudio.h"
#endif
#if UC2_WITH_UC2MOVIE	// the movie player and its self-test commandlet (UC2Movie.UC2MovieSelfTest)
#include "UC2Movie.h"
#endif
#if UC2_WITH_EDITOR	// the script compiler (ucc-bin make), the header exporter and the editor commandlets
void UC2InitializeEditorRegistrants( INT& Lookup );	// Editor/Src/UC2EditorRegistrants.cpp
UBOOL UC2VerifyEditorClasses();						// Editor/Src/UC2EditorRegistrants.cpp
#endif
#endif

#endif

INT GFilesOpen, GFilesOpened;

/*-----------------------------------------------------------------------------
	Global variables.
-----------------------------------------------------------------------------*/

// General.
#if _MSC_VER
	extern "C" {HINSTANCE hInstance;}
#endif
extern "C" {TCHAR GPackage[64]=TEXT("UCC");}

// Log.
#include "FOutputDeviceFile.h"
FOutputDeviceFile Log;

// Error.
#include "FOutputDeviceAnsiError.h"
FOutputDeviceAnsiError Error;

// Feedback.
#include "FFeedbackContextAnsi.h"
FFeedbackContextAnsi Warn;

// File manager.
#if _MSC_VER
	#include "FFileManagerWindows.h"
	FFileManagerWindows FileManager;
	#include "FMallocAnsi.h"
	FMallocAnsi Malloc;
#elif __PSX2_EE__
	#include "FFileManagerPSX2.h"
	FFileManagerPSX2 FileManager;
	#include "FMallocAnsi.h"
	FMallocAnsi Malloc;
#elif __LINUX_X86__ || defined(__MINGW32__)
	#include "FFileManagerUnix.h"
	FFileManagerUnix FileManager;
	#include "FMallocAnsi.h"
	FMallocAnsi Malloc;
#else
	#include "FFileManagerAnsi.h"
	FFileManagerAnsi FileManager;
	#include "FMallocAnsi.h"
	FMallocAnsi Malloc;
#endif

// Config.
#include "FConfigCacheIni.h"

//jg -- Thread synchronization factory
#if _MSC_VER
#include "UnThreadingWindows.h"
FSynchronizeFactoryWin SynchFactory;
FThreadFactoryWin ThreadFactory;
#else
#include "UnThreadingPosix.h"
FSynchronizeFactoryPosix SynchFactory;
FThreadFactoryPosix ThreadFactory;
#endif

/*-----------------------------------------------------------------------------
	Main.
-----------------------------------------------------------------------------*/

// Unreal command-line applet executor.
FString RightPad( FString In, INT Count )
{
	while( In.Len()<Count )
		In += TEXT(" ");
	return In;
}
static INT Compare( FString& A, FString& B )
{
	return appStricmp( *A, *B );
}
void ShowBanner( FOutputDevice& Warn )
{
	Warn.Logf( TEXT("=======================================") );
	Warn.Logf( TEXT("ucc.exe: UnrealOS execution environment") );
	Warn.Logf( TEXT("Copyright 2001 Epic Games Inc") );
	Warn.Logf( TEXT("=======================================") );
	Warn.Logf( TEXT("") );
}

#if __DO_LINUX_MCHECKING__
extern "C"
{
    int mcheck(void *feh);
}
#endif

#if __STATIC_LINK && !defined(_MSC_VER)
// "ucc verifyclasses": compare the script layout of every native class with the C++ layout, using the lists the
// generated class headers carry for the check in UGameEngine::LoadMap. That check only runs once a map has
// loaded, which is too late to diagnose a layout problem that breaks loading itself. Returns the mismatch count
// as seen in the log ("VERIFY: ..." lines).
extern CORE_API UBOOL GLayoutWarnOnly;
extern CORE_API INT GLayoutMismatches;
extern CORE_API INT GetNativeStructSize( UStruct* Struct );
extern CORE_API INT GetNativePlaceholderCount();
extern CORE_API void GetNativePlaceholder( INT i, const TCHAR*& Package, const TCHAR*& Struct, const TCHAR*& Property, INT& Size, INT& Kind );
extern CORE_API UBOOL IsNativePlaceholder( UProperty* Property );
extern CORE_API const TCHAR* GetNativePlaceholderCppTypeAt( INT i );
// `verifyclasses DUMP=<file>` writes the linked layout of every class, script struct and function (its
// parameters) for tools/dev/layout-check.py, which compares it with the DWARF of the same binary. Opt-in on
// every width (the 32-bit dump is the reference); nothing else changes without DUMP=.
static void DumpScriptLayouts( const TCHAR* DumpFile )
{
	FArchive* Out = GFileManager->CreateFileWriter( DumpFile );
	if( !Out )
		return;
	for( TObjectIterator<UStruct> It; It; ++It )
	{
		const TCHAR* Kind = It->GetClass()==UClass::StaticClass() ? TEXT("class") : It->GetClass()==UStruct::StaticClass() ? TEXT("struct")
			: It->GetClass()==UFunction::StaticClass() ? TEXT("function") : NULL;
		if( !Kind )
			continue;
		INT NativeSize = GetNativeStructSize(*It);
		FString Line = FString::Printf( TEXT("S %s %s %i %i\n"), Kind, It->GetPathName(), It->GetPropertiesSize(), NativeSize );
		Out->Serialize( (void*)*Line, Line.Len() );
		for( TFieldIterator<UProperty,CLASS_IsAUProperty> P(*It); P; ++P )
		{
			if( P.GetStruct()!=*It )
				continue;
			UArrayProperty* A = Cast<UArrayProperty>(*P);
			UBoolProperty* B = Cast<UBoolProperty>(*P);
			INT Placeholder = IsNativePlaceholder(*P) ? 1 : 0;
			FString PLine = FString::Printf( TEXT("P %s %i %i %i %s %i %i %i\n"), P->GetName(), P->Offset, P->ElementSize, P->ArrayDim,
				P->GetClass()->GetName(), B ? (INT)B->BitMask : 0, A ? A->Inner->ElementSize : 0, Placeholder );
			Out->Serialize( (void*)*PLine, PLine.Len() );
		}
	}
	delete Out;
	debugf( TEXT("VERIFY: layout dump written to %s"), DumpFile );
}

// The layout check. The shipped packages were compiled for 32-bit pointers, so "C++ and script agree"
// means: the property system, linking those packages with this target's rules (object references a pointer wide
// aligned to 4, arrays/strings sizeof(FArray), the native placeholders of UnClass.cpp), gives every native class
// the size and member offsets the C++ compiler gives it.

// Collects every object a piece of bytecode refers to (SerializeExpr on a plain archive only reads).
class FArchiveScriptRefs : public FArchive
{
public:
	TArray<UObject*> Refs;
	FArchive& operator<<( UObject*& Obj ) { if( Obj ) Refs.AddUniqueItem( Obj ); return *this; }
};

static UBOOL VerifyNativeLayouts( const TCHAR* DumpFile )
{
	UBOOL Mismatch = false;
	// 1. Every class with a C++ declaration: C++ sizeof against the linked script size (UStruct::Link counted
	//    and logged each mismatch while loading; this lists the checked ones too).
	INT Checked=0;
	for( TObjectIterator<UClass> It; It; ++It )
	{
		INT NativeSize = GetNativeStructSize( *It );
		if( NativeSize<0 || !It->GetPropertiesSize() )
			continue;
		Checked++;
		if( NativeSize!=It->GetPropertiesSize() )
		{
			debugf( TEXT("VERIFY: Class %s size problem; Script=%i C++=%i (native size check)"), It->GetPathName(), It->GetPropertiesSize(), NativeSize );
			Mismatch = true;
		}
	}
	debugf( TEXT("VERIFY: native size check: %i classes, %i mismatches while linking"), Checked, GLayoutMismatches );
	if( GLayoutMismatches )
		Mismatch = true;
	// 2. Every placeholder entry names a property that exists and is linked at its C++ size.
	for( INT i=0; i<GetNativePlaceholderCount(); i++ )
	{
		const TCHAR *Pkg, *StructName, *PropName; INT Size, Kind;
		GetNativePlaceholder( i, Pkg, StructName, PropName, Size, Kind );
		UStruct* Struct = FindObject<UStruct>( ANY_PACKAGE, StructName );
		UProperty* Found = NULL;
		if( Struct )
			for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(Struct); It; ++It )
				if( It.GetStruct()==Struct && appStricmp(It->GetName(),PropName)==0 )
					Found = *It;
		if( !Found || !IsNativePlaceholder(Found) )
		{
			debugf( TEXT("VERIFY: placeholder %s.%s.%s NOT FOUND (stale table entry or package not loaded)"), Pkg, StructName, PropName );
			Mismatch = true;
		}
		else
		{
			debugf( TEXT("VERIFY: placeholder %s.%s.%s offset %i, C++ size %i (%s)"), Pkg, StructName, PropName, Found->Offset, Size, Kind ? TEXT("array element") : TEXT("member") );
			// The header exporter writes a PTRINT row as `PTRINT X[ArrayDim]` / `TArrayNoInit<PTRINT> X`; the
			// size Link gives it must be that spelling's size.
			const TCHAR* CppType = GetNativePlaceholderCppTypeAt( i );
			INT Spelled = Kind ? (INT)sizeof(PTRINT) : Found->ArrayDim * (INT)sizeof(PTRINT);
			if( CppType && appStrcmp(CppType,TEXT("PTRINT"))==0 && Size!=Spelled )
			{
				debugf( TEXT("VERIFY: placeholder %s.%s.%s exported as PTRINT (%i bytes) but linked at %i"), Pkg, StructName, PropName, Spelled, Size );
				Mismatch = true;
			}
		}
	}
	// 3. No bytecode reads or writes a placeholder: script would see only the low 4 bytes of the C++ data.
	INT Scanned=0;
	for( TObjectIterator<UStruct> It; It; ++It )
	{
		if( !It->Script.Num() )
			continue;
		FArchiveScriptRefs Ar;
		INT iCode = 0;
		while( iCode < It->Script.Num() )
			It->SerializeExpr( iCode, Ar );
		Scanned++;
		for( INT j=0; j<Ar.Refs.Num(); j++ )
		{
			UProperty* P = Cast<UProperty>( Ar.Refs(j) );
			if( P && IsNativePlaceholder(P) )
			{
				debugf( TEXT("VERIFY: bytecode of %s refers to native placeholder %s"), It->GetPathName(), P->GetPathName() );
				Mismatch = true;
			}
		}
	}
	debugf( TEXT("VERIFY: bytecode scan: %i scripts read for placeholder references"), Scanned );
	if( DumpFile && *DumpFile )
		DumpScriptLayouts( DumpFile );
	return !Mismatch;
}

static UBOOL VerifyNativeClasses()
{
	static const TCHAR* Packages[] = { TEXT("Engine"), TEXT("IpDrv"), TEXT("UnrealGame"), TEXT("UCElements"), TEXT("UCGameplay"), TEXT("GUI"),
		TEXT("UWeb") };	// UWeb: its hand-written mirrors carry placeholders
	for( INT i=0; i<ARRAY_COUNT(Packages); i++ )
	{
		debugf( TEXT("VERIFY: loading package %s"), Packages[i] );
		UObject::LoadPackage( NULL, Packages[i], LOAD_NoFail );
	}
	UBOOL Mismatch = false;
	#define VERIFY_CLASS_SIZES
	#define NAMES_ONLY
	#define AUTOGENERATE_NAME(name)
	#define AUTOGENERATE_FUNCTION(cls,idx,name)
	#include "EngineClasses.h"
	#include "IpDrvClasses.h"
	#include "UnrealGameClasses.h"
	#include "UCElementsClasses.h"
	#include "UCGameplayClasses.h"
	#include "GUIClasses.h"
	#undef AUTOGENERATE_FUNCTION
	#undef AUTOGENERATE_NAME
	#undef NAMES_ONLY
	#undef VERIFY_CLASS_SIZES
#if UC2_WITH_EDITOR	// the Editor package's script/C++ layouts too
	debugf( TEXT("VERIFY: loading package Editor") );
	UObject::LoadPackage( NULL, TEXT("Editor"), LOAD_NoFail );
	if( !UC2VerifyEditorClasses() )
		Mismatch = true;
#endif
	FString DumpFile;
	Parse( appCmdLine(), TEXT("DUMP="), DumpFile );
	if( !VerifyNativeLayouts( *DumpFile ) )
		Mismatch = true;
	debugf( TEXT("VERIFY: %s"), Mismatch ? TEXT("C++ / script class layout MISMATCH") : TEXT("all native class layouts match") );
	return !Mismatch;
}
#endif

#if __STATIC_LINK && !defined(_MSC_VER)
// "ucc profiletest save|load|make|list|exists|delete <profile> <export.txt> [Character=<class>]": the save
// game format across pointer widths.
// save: a profile of the configured class with a deterministic non-default value in every plain property
// (ints, floats, bytes, bools, names, strings), written through ULoadSaveManager as the game does
// (System/Profiles/<profile>/AVARIS_PROFILE.PRO); load: read it back. Either way every property is exported as
// text to <export.txt>, so a profile saved by one build and loaded by another can be compared with the values
// it was saved with (tools/dev/check-64bit.sh `saves`). Returns success.
static UBOOL ProfileTest( const TCHAR* Parms )
{
	FString Mode, Name, OutFile;
	if( !ParseToken(Parms,Mode,0) || !ParseToken(Parms,Name,0) || !ParseToken(Parms,OutFile,0) )
		return 0;
	UObject::LoadPackage( NULL, TEXT("Engine"), LOAD_NoFail );
	ULoadSaveManager* Manager = ConstructObject<ULoadSaveManager>( ULoadSaveManager::StaticClass() );
	Manager->Init( NULL );
	// "list" writes the names ListGameObjects finds (one per line) to <export.txt>; "exists" and "delete" return
	// whether the profile <profile> exists / was deleted (Profiles/<name>/AVARIS_PROFILE.PRO).
	if( Mode==TEXT("LIST") )
	{
		TArray<FString> Names;
		UBOOL Listed = Manager->ListGameObjects( &Names );
		FString Text;
		for( INT i=0; i<Names.Num(); i++ )
			Text += Names(i) + TEXT("\n");
		appSaveStringToFile( Text, *OutFile );
		return Listed;
	}
	if( Mode==TEXT("EXISTS") )
		return Manager->ExistsGameObject( Name );
	if( Mode==TEXT("DELETE") )
		return Manager->DeleteGameObject( Name );
	UClass* Cls = Manager->getGameProfileClass() ? Manager->getGameProfileClass() : UPlayerProfile::StaticClass();
	UPlayerProfile* Profile = ConstructObject<UPlayerProfile>( Cls );
	UBOOL Ok;
	// "make" saves a profile with the class's default values (a new player's profile, playable, unlike "save"'s
	// test pattern) and, given Character=<class>, that character (tools/dev/net-test.py profile-login).
	if( Mode==TEXT("MAKE") )
	{
		FString Character;
		if( Parse( Parms, TEXT("CHARACTER="), Character ) )
			Profile->CharacterClassName = Character;
		Profile->ProfileName = Name;
		Profile->SetDirty();
		Profile->SetTransient( false );
		Ok = Profile->SaveProfile();
	}
	else if( Mode==TEXT("SAVE") )
	{
		INT n = 0;
		for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(Cls); It; ++It, n++ )
		{
			if( It->PropertyFlags & (CPF_Transient|CPF_Native) )
				continue;
			for( INT j=0; j<It->ArrayDim; j++ )
			{
				BYTE* Data = (BYTE*)Profile + It->Offset + j*It->ElementSize;
				if( Cast<UIntProperty>(*It) )
					*(INT*)Data = 1000 + 7*n + j;
				else if( Cast<UFloatProperty>(*It) )
					*(FLOAT*)Data = n + 0.25f*j + 0.5f;
				else if( Cast<UByteProperty>(*It) )
				{
					UEnum* Enum = ((UByteProperty*)*It)->Enum;
					*Data = (BYTE)(Enum && Enum->Names.Num() ? (n+j) % Enum->Names.Num() : (3*n+j) & 0x7f);
				}
				else if( Cast<UBoolProperty>(*It) )
					*(BITFIELD*)Data ^= ((UBoolProperty*)*It)->BitMask;
				else if( Cast<UNameProperty>(*It) )
					*(FName*)Data = FName( It->GetName() );
				else if( Cast<UStrProperty>(*It) )
					*(FString*)Data = FString::Printf( TEXT("%s%i"), It->GetName(), j );
			}
		}
		Profile->ProfileName = Name;
		Profile->SetDirty();
		Profile->SetTransient( false );
		Ok = Profile->SaveProfile();
	}
	else
		Ok = Profile->LoadProfile( Name );
	FStringOutputDevice Out;
	UObject::ExportProperties( Out, Cls, (BYTE*)Profile, 0, NULL, NULL );
	appSaveStringToFile( Out, *OutFile );
	return Ok;
}
#endif

#if __STATIC_LINK && !defined(_MSC_VER)
// saveconfig task: "ucc saveconfigtest static|instance|gameinfo|keys|get <Class> <Property> [<Value>] [load=<Package>]".
// Script's SaveConfig() / StaticSaveConfig() and the SAVECONFIGKEYS command without a game: the default of
// <Property> of <Class> is set to <Value> (if given), then
//   static   calls the native behind StaticSaveConfig() (UObject::execStaticSaveConfig) on the class,
//   instance calls the native behind SaveConfig() (UObject::execSaveConfig) on the class default object,
//   gameinfo calls it on a new INSTANCE of the class with the property set on that instance (a GameInfo: not saved),
//   keys     runs "SAVECONFIGKEYS <Class> <Property>" through UObject::StaticExec.
//   get      prints the class default of <Property> ("GET <Class> <Property>"), which shows what the ini gave it.
// The natives are called as the script VM calls them (a frame whose code is the end-of-parameters token), so
// [Engine.Engine] ScriptSaveConfig (and -ini: for it) decide whether anything is written. The ini the class
// belongs to is what the checks read (tools/dev/sc-saveconfig.sh, natively and on the wasm build under Node,
// where it goes through the web build's file layer). Returns success.
static UBOOL SaveConfigTest( const TCHAR* Parms )
{
	FString Mode, ClassName, PropName, Value;
	if( !ParseToken(Parms,Mode,0) || !ParseToken(Parms,ClassName,0) || !ParseToken(Parms,PropName,0) )
		return 0;
	ParseToken( Parms, Value, 0 );
	// LOAD=<Package>: load that package first (a game has them all loaded; a save looks at the classes that are)
	FString LoadPackageName;
	if( Parse( Parms, TEXT("LOAD="), LoadPackageName ) )
		UObject::LoadPackage( NULL, *LoadPackageName, LOAD_NoWarn );
	UClass* Cls = UObject::StaticLoadClass( UObject::StaticClass(), NULL, *ClassName, NULL, LOAD_NoWarn, NULL );
	UProperty* Prop = Cls ? FindField<UProperty>( Cls, *PropName ) : NULL;
	if( !Cls || !Prop )
	{
		GWarn->Logf( TEXT("SAVECONFIGTEST: no %s %s"), Cls ? TEXT("property") : TEXT("class"), Cls ? *PropName : *ClassName );
		return 0;
	}
	UObject* Defaults = Cls->GetDefaultObject();
	if( Value.Len() )
		Prop->ImportText( *Value, (BYTE*)Defaults + Prop->Offset, 0 );
	BYTE Code[4] = { EX_EndFunctionParms, 0, 0, 0 };
	if( Mode==TEXT("STATIC") || Mode==TEXT("INSTANCE") )
	{
		FFrame Stack( Defaults );
		Stack.Code = Code;
		BYTE Result[64];
		if( Mode==TEXT("STATIC") )
			Defaults->execStaticSaveConfig( Stack, Result );
		else
			Defaults->execSaveConfig( Stack, Result );
		return 1;
	}
	if( Mode==TEXT("GAMEINFO") )
	{
		// an INSTANCE of the class (a GameInfo: the live rules of a match, DeathMatch.AdjustSkill's call): the property is
		// set on the instance, then the native behind SaveConfig() runs on it
		UObject* Instance = ConstructObject<UObject>( Cls );
		if( Value.Len() )
			Prop->ImportText( *Value, (BYTE*)Instance + Prop->Offset, 0 );
		FFrame Stack( Instance );
		Stack.Code = Code;
		BYTE Result[64];
		Instance->execSaveConfig( Stack, Result );
		return 1;
	}
	if( Mode==TEXT("KEYS") )
		return UObject::StaticExec( *FString::Printf(TEXT("SAVECONFIGKEYS %s %s"), *ClassName, *PropName), *GWarn );
	if( Mode==TEXT("GET") )		// the class default as the engine holds it: GWarn gets "GET <Class> <Property>"'s answer
		return UObject::StaticExec( *FString::Printf(TEXT("GET %s %s"), *ClassName, *PropName), *GWarn );
	return 0;
}
#endif

int main( int argc, char* argv[] )
{
    #if __LINUX__ || defined(__MINGW32__)
        setbuf(stdout, NULL);
        setbuf(stderr, NULL);
        #if __DO_LINUX_MCHECKING__
            mcheck(NULL);
        #endif
    #endif

	// (The static lookup tables are filled after appInit, with the class registrants; see below.)

	//scion jg -- Default to having an error
	INT ErrorLevel = 1;
	GIsStarted     = 1;
#ifndef _DEBUG
	try
#endif
	{
		GIsGuarded = 1;

		#if !_MSC_VER
		// Set module name.
		strcpy( GModule, "ucc" );
		//!!vogel: pathname might be too long
		//appStrcpy( GModule, argv[0] );
		#endif
		
		// Parse command line.
		// 4096, Core's GCmdLine (-ini: tokens make long lines; Epic's 1024 had no bound check here). A token that does
		// not fit is dropped whole.
		TCHAR CmdLine[4096], *CmdLinePtr=CmdLine;
		*CmdLinePtr = 0;
		#if _MSC_VER
			ANSICHAR* Ch = GetCommandLineA();
			while( *Ch && *Ch!=' ' )
				Ch++;
			while( *Ch==' ' )
				Ch++;
			while( *Ch )
				*CmdLinePtr++ = *Ch++;
			*CmdLinePtr++ = 0;
		#else
			for( INT i=2; i<argc; i++ )
			{
				if( appStrlen(CmdLine) + strlen(argv[i]) + 2 >= ARRAY_COUNT(CmdLine) )
				{
					printf( "ucc: command line longer than %i characters: '%s' and what follows are dropped\n", (int)ARRAY_COUNT(CmdLine), argv[i] );
					break;
				}
				if( i>2 )
					appStrcat( CmdLine, TEXT(" ") );
				appStrcat( CmdLine, ANSI_TO_TCHAR(argv[i]) );
			}
		#endif

#if __STATIC_LINK && !defined(_MSC_VER)
		// verifyclasses lists every layout mismatch the class linker finds instead of stopping at the first.
		if( argc>1 && appStricmp(argv[1],"verifyclasses")==0 )
			GLayoutWarnOnly = 1;
#endif
		//jg -- Set the pointer to the synchronization factory
		GSynchronizeFactory = &SynchFactory;
		GThreadFactory = &ThreadFactory;

		// Init engine core.
		appInit( TEXT("Avaris"), CmdLine, &Malloc, &Log, &Error, &Warn, &FileManager, FConfigCacheIni::Factory, 1 ); //!!

		// Init static classes: the native lookup tables and class registrants of every linked package,
		// in the order XboxLaunch/Src/Launch.cpp uses for the static XBE.
		#if __STATIC_LINK
			for( INT k=0; k<ARRAY_COUNT(GNativeLookupFuncs); k++ )
				GNativeLookupFuncs[k] = NULL;
			INT Lookup = 0;
			GNativeLookupFuncs[Lookup++] = &FindCoreUObjectNative;
			GNativeLookupFuncs[Lookup++] = &FindCoreUCommandletNative;
			AUTO_INITIALIZE_REGISTRANTS_ENGINE;
		#if !defined(_MSC_VER)
			AUTO_INITIALIZE_REGISTRANTS_IPDRV;
			AUTO_INITIALIZE_REGISTRANTS_UWEB;
			// UWeb's hand-written static header registers its classes but not its native lookups.
			GNativeLookupFuncs[Lookup++] = &FindUWebUWebResponseNative;
			GNativeLookupFuncs[Lookup++] = &FindUWebUWebRequestNative;
			AUTO_INITIALIZE_REGISTRANTS_UNREALGAME;
			AUTO_INITIALIZE_REGISTRANTS_UCELEMENTS;
			AUTO_INITIALIZE_REGISTRANTS_UCGAMEPLAY;
			AUTO_INITIALIZE_REGISTRANTS_GUI;
#if UC2_WITH_UC2ALAUDIO
			AUTO_INITIALIZE_REGISTRANTS_UC2ALAUDIO;
#endif
#if UC2_WITH_UC2MOVIE
			AUTO_INITIALIZE_REGISTRANTS_UC2MOVIE;
#endif
#if UC2_WITH_EDITOR	// what AUTO_INITIALIZE_REGISTRANTS_EDITOR does (Editor/Src/UC2EditorRegistrants.cpp)
			UC2InitializeEditorRegistrants( Lookup );
#endif
			check( Lookup < ARRAY_COUNT(GNativeLookupFuncs) );
			RegisterNamesGUI();
			RegisterNamesUCElements();
			RegisterNamesUCGameplay();
		#endif
		#endif

		// Set up meta package readers.
//		for( i=0; i<GSys->MetaPackages.Num(); i++ )
//		{
//			printf("%s\n", *GSys->MetaPackages(i));
//			((FFileManagerPSX2*) GFileManager)->AddMetaArchive( *GSys->MetaPackages(i), &Error );
//		}

		// Get the ucc stuff going.	
		UObject::SetLanguage(TEXT("int"));
		FString Token = argc>1 ? appFromAnsi(argv[1]) : TEXT("");
		TArray<FRegistryObjectInfo> List;
		UObject::GetRegistryObjects( List, UClass::StaticClass(), UCommandlet::StaticClass(), 0 );
		GIsClient = GIsServer = GIsEditor = GIsScriptable = GIsUCC = 1;
		GLazyLoad = 0;
		UBOOL Help = 0;
		DWORD LoadFlags = LOAD_NoWarn | LOAD_Quiet;
		if( Token==TEXT("") )
		{
			ShowBanner( Warn );
			Warn.Logf( TEXT("Use \"ucc help\" for help") );
		}
#if __STATIC_LINK && !defined(_MSC_VER)
		else if( Token==TEXT("VERIFYCLASSES") )
		{
			UBOOL Ok = VerifyNativeClasses();
			Warn.Logf( TEXT("%s (details: the VERIFY lines in the log)"), Ok ? TEXT("All native class layouts match") : TEXT("Class layout mismatches found") );
			ErrorLevel = Ok ? 0 : 1;
		}
#endif
#if __STATIC_LINK && !defined(_MSC_VER)
		else if( Token==TEXT("PROFILETEST") )
		{
			const TCHAR* Parms = appCmdLine();
			UBOOL Ok = ProfileTest( Parms );
			Warn.Logf( TEXT("PROFILETEST: %s"), Ok ? TEXT("ok") : TEXT("FAILED") );
			ErrorLevel = Ok ? 0 : 1;
		}
#endif
#if __STATIC_LINK && !defined(_MSC_VER)
		else if( Token==TEXT("SAVECONFIGTEST") )	// saveconfig task
		{
			UBOOL Ok = SaveConfigTest( appCmdLine() );
			Warn.Logf( TEXT("SAVECONFIGTEST: %s"), Ok ? TEXT("ok") : TEXT("FAILED") );
			ErrorLevel = Ok ? 0 : 1;
		}
#endif
		else if( Token==TEXT("HELP") )
		{
			ShowBanner( Warn );
			verify(UObject::StaticLoadClass( UCommandlet::StaticClass(), NULL, TEXT("Core.Commandlet"), NULL, LOAD_NoFail, NULL )==UCommandlet::StaticClass());
			const TCHAR* Tmp = appCmdLine();
			GIsEditor = 0; // To enable loading localized text.
			if( !ParseToken( Tmp, Token, 0 ) )
			{
				INT i;
				Warn.Logf( TEXT("Usage:") );
				Warn.Logf( TEXT("   ucc <command> <parameters>") );
				Warn.Logf( TEXT("") );
				Warn.Logf( TEXT("Commands for \"ucc\":") );
				TArray<FString> Items;
				for( i=0; i<List.Num(); i++ )
				{
					UClass* Class = UObject::StaticLoadClass( UCommandlet::StaticClass(), NULL, *List(i).Object, NULL, LoadFlags, NULL );
					if( Class )
					{
						UCommandlet* Default = (UCommandlet*)Class->GetDefaultObject();
						new(Items)FString( FString(TEXT("   ucc ")) + RightPad(Default->HelpCmd,21) + TEXT(" ") + Default->HelpOneLiner );
					}
				}
				new(Items)FString( TEXT("   ucc help <command>        Get help on a command") );
				Sort( &Items(0), Items.Num() );
				for( i=0; i<Items.Num(); i++ )
					Warn.Log( Items(i) );
			}
			else
			{
				Help = 1;
				goto Process;
			}
		}
		else
		{
			// Look it up.
			if( Token==TEXT("Make") )
				LoadFlags |= LOAD_DisallowFiles;
		Process:
			UClass* Class = UObject::StaticLoadClass( UCommandlet::StaticClass(), NULL, *Token, NULL, LoadFlags, NULL );
			if( !Class )
				Class = UObject::StaticLoadClass( UCommandlet::StaticClass(), NULL, *(Token+TEXT("Commandlet")), NULL, LoadFlags, NULL );
			if( !Class )
			{
				INT i;
				for( i=0; i<List.Num(); i++ )
				{
					FString Str = List(i).Object;
					while( Str.InStr(TEXT("."))>=0 )
						Str = Str.Mid(Str.InStr(TEXT("."))+1);
					if( Token==Str || Token+TEXT("Commandlet")==Str )
						break;
				}
				if( i<List.Num() )
					Class = UObject::StaticLoadClass( UCommandlet::StaticClass(), NULL, *List(i).Object, NULL, LoadFlags, NULL );
			}
			if( Class )
			{
				UCommandlet* Default = (UCommandlet*)Class->GetDefaultObject();
				if( Help )
				{
					// Get help on it.
					if( Default->HelpUsage!=TEXT("") )
					{
						Warn.Logf( TEXT("Usage:") );
						Warn.Logf( TEXT("   ucc %s"), *Default->HelpUsage );
					}
					if( Default->HelpParm[0]!=TEXT("") )
					{
						Warn.Logf( TEXT("") );
						Warn.Logf( TEXT("Parameters:") );
						for( INT i=0; i<ARRAY_COUNT(Default->HelpParm) && Default->HelpParm[i]!=TEXT(""); i++ )
							Warn.Logf( TEXT("   %s %s"), *RightPad(Default->HelpParm[i],16), *Default->HelpDesc[i] );
					}
					if( Default->HelpWebLink!=TEXT("") )
					{
						Warn.Logf( TEXT("") );
						Warn.Logf( TEXT("For more info, see") );
						Warn.Logf( TEXT("   %s"), *Default->HelpWebLink );
					}
				}
				else
				{
					// Run it.
					if( Default->LogToStdout )
					{
						Warn.AuxOut = GLog;
						GLog        = &Warn;
					}
					if( Default->ShowBanner )
					{
						ShowBanner( Warn );
					}
					debugf( TEXT("Executing %s"), Class->GetFullName() );
					GIsClient = Default->IsClient;
					GIsServer = Default->IsServer;
					GIsEditor = Default->IsEditor;
					GLazyLoad = Default->LazyLoad;
					UCommandlet* Commandlet = ConstructObject<UCommandlet>( Class );
					// Keep it alive while it runs: nothing references the commandlet object, and the garbage collection
					// in the server's LoadMap would free it under its own Main() (Commandlet->ShowErrorCount below).
					Commandlet->AddToRoot();
					Commandlet->InitExecution();
					Commandlet->ParseParms( appCmdLine() );
					Commandlet->Main( appCmdLine() );
					if( Commandlet->ShowErrorCount )
					{
                        if ( Warn.ErrorCount == 0)
						    warnf( TEXT("Success - %d error(s), %d warning(s)"), Warn.ErrorCount, Warn.WarningCount );
                        else
                        {
						    warnf( TEXT("Failure - %d error(s), %d warning(s)"), Warn.ErrorCount, Warn.WarningCount );
                        }
					}
					//scion jg -- Determine if there is an error or not
					if (Warn.ErrorCount == 0 && Warn.WarningCount == 0)
					{
						ErrorLevel = 0;
					}
					if( Default->LogToStdout )
					{
						Warn.AuxOut = NULL;
						GLog        = &Log;
					}
				}
			}
			else
			{
				ShowBanner( Warn );
				Warn.Logf( TEXT("Commandlet %s not found"), *Token );
			}
		}
		appPreExit();
		GIsGuarded = 0;
	}
#ifndef _DEBUG
	catch( ... )
	{
		// Crashed.
		ErrorLevel = 1;
		GIsGuarded = 0;
		Error.HandleError();
	}
#endif
	appExit();
	GIsStarted = 0;
	return ErrorLevel;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
