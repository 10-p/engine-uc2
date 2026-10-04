/*=============================================================================
	UMakeCommandlet.cpp: UnrealEd script recompiler.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Tim Sweeney.
=============================================================================*/

#include "EditorPrivate.h"

/*-----------------------------------------------------------------------------
	UMakeCommandlet.
-----------------------------------------------------------------------------*/

class UMakeCommandlet : public UCommandlet
{
	DECLARE_CLASS(UMakeCommandlet,UCommandlet,CLASS_Transient,Editor);
	void StaticConstructor()
	{
		guard(StaticConstructor::StaticConstructor);

		LogToStdout     = 0;
		IsClient        = 1;
		IsEditor        = 1;
		IsServer        = 1;
		LazyLoad        = 1;
		ShowErrorCount  = 1;

		unguard;
	}

	// scion sz -
	UBOOL ImportClassConfigs( UPackage* ScriptPkg );
	UBOOL ImportClassLocalized( UPackage* ScriptPkg );

	INT Main( const TCHAR* Parms )
	{
		guard(UMakeCommandlet::Main);

		// Create the editor class.
		UClass* EditorEngineClass = UObject::StaticLoadClass( UEditorEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.EditorEngine"), NULL, LOAD_NoFail | LOAD_DisallowFiles, NULL );
		GEditor  = ConstructObject<UEditorEngine>( EditorEngineClass );
		GEditor->UseSound = 0;
		GEditor->InitEditor();
		GIsRequestingExit = 1; // Causes ctrl-c to immediately exit.
#if 1 // rwd
		TCHAR EndPkg[256];
		if (!Parse(appCmdLine(),TEXT("endpkg="),EndPkg,256))
		{
			EndPkg[0] = '\0';
		}
#endif
		// Load classes for editing.
		UClassFactoryUC* ClassFactory = new UClassFactoryUC;
		for( INT i=0; i<GEditor->EditPackages.Num(); i++ )
		{
			// Try to load class.
			const TCHAR* Pkg = *GEditor->EditPackages( i );
			FString Filename = FString(Pkg) + TEXT(".u");
			GWarn->Log( NAME_Heading, FString::Printf(TEXT("%s - %s"),Pkg,ParseParam(appCmdLine(), TEXT("DEBUG"))? TEXT("Debug") : TEXT("Release"))); //DEBUGGER
			if( !LoadPackage( NULL, *Filename, LOAD_NoWarn ) )
			{
				// Create package.
				GWarn->Log( TEXT("Analyzing...") );
				UPackage* PkgObject = CreatePackage( NULL, Pkg );

				// Try reading from package's .ini file.
				PkgObject->PackageFlags &= ~(PKG_AllowDownload|PKG_ClientOptional|PKG_ServerSideOnly);
				FString IniName = FString(TEXT("..")) * Pkg * TEXT("Classes") * Pkg + TEXT(".upkg");
				UBOOL B=0;
				if( GConfig->GetBool(TEXT("Flags"), TEXT("AllowDownload"), B, *IniName) && B )
					PkgObject->PackageFlags |= PKG_AllowDownload;
				if( GConfig->GetBool(TEXT("Flags"), TEXT("ClientOptional"), B, *IniName) && B )
					PkgObject->PackageFlags |= PKG_ClientOptional;
				if( GConfig->GetBool(TEXT("Flags"), TEXT("ServerSideOnly"), B, *IniName) && B )
					PkgObject->PackageFlags |= PKG_ServerSideOnly;
 
				// Rebuild the class from its directory.
				FString Spec = FString(TEXT("..")) * Pkg * TEXT("Classes") * TEXT("*.uc");
				TArray<FString> Files = GFileManager->FindFiles( *Spec, 1, 0 );
				if( Files.Num() == 0 )
					appErrorf( TEXT("Can't find files matching %s"), *Spec );
				for( INT i=0; i<Files.Num(); i++ )
				{
					// Import class.
					FString Filename  = FString(TEXT("..")) * Pkg * TEXT("Classes") * Files(i);
					FString ClassName = Files(i).LeftChop(3);
					ImportObject<UClass>( GEditor->Level, PkgObject, *ClassName, RF_Public|RF_Standalone, *Filename, NULL, ClassFactory );
				}

				// Verify that all script declared superclasses exist.
				for( TObjectIterator<UClass> ItC; ItC; ++ItC )
					if( ItC->ScriptText && ItC->GetSuperClass() )
						if( !ItC->GetSuperClass()->ScriptText )
							appErrorf( TEXT("Superclass %s of class %s not found"), ItC->GetSuperClass()->GetName(), ItC->GetName() );

				// Bootstrap-recompile changed scripts.
				GEditor->Bootstrapping = 1;
				UBOOL Success = GEditor->MakeScripts( NULL, GWarn, 0, 1, 1 );
				GEditor->Bootstrapping = 0;

				// scion sz - import localized/config values to defaults
				guard(ImportConfigs);
				if( Success && 
					ParseParam(appCmdLine(), TEXT("LOADCONFIGS")) ) 
				{
					static TCHAR* SkipPackages[] =
					{
						TEXT("Core"),
						TEXT("Editor"),
						TEXT("Engine"),
						TEXT("IPDrv"),
						TEXT("UnrealEd"),
						TEXT("UWeb")
					};
					UBOOL bSkip=0;
					for( INT Idx=0; Idx < ARRAY_COUNT(SkipPackages); Idx++ )
					{
						if( appStricmp(SkipPackages[Idx], PkgObject->GetName())==0 )
						{
							bSkip=1;
							break;
						}
					}
					if( !bSkip )
					{                        
						ImportClassConfigs( PkgObject );
					}
					// need option for what languange to use
					ImportClassLocalized( PkgObject );
				}                				
				unguard;

                if( !Success )
                {
                    warnf ( TEXT("Compile aborted due to errors.") );
                    break;
                }

				// Tag native classes in this package for export.
				INT ClassCount=0;
				for( INT i=0; i<FName::GetMaxNames(); i++ )
					if( FName::GetEntry(i) )
						FName::GetEntry(i)->Flags &= ~RF_TagExp;
				for( TObjectIterator<UClass> It; It; ++It )
					It->ClearFlags( RF_TagImp | RF_TagExp );
				for( TObjectIterator<UClass> It=TObjectIterator<UClass>(); It; ++It )
					if( It->GetOuter()==PkgObject && It->ScriptText && (It->GetFlags()&RF_Native) && !(It->ClassFlags&CLASS_NoExport) )
						ClassCount++, It->SetFlags( RF_TagExp );

				// Export the C++ header.
				if( ClassCount && !ParseParam(appCmdLine(),TEXT("NOH")) )
				{
					Filename = FString(TEXT("..")) * Pkg * TEXT("Inc") * Pkg + TEXT("Classes.h");
					debugf( TEXT("Autogenerating C++ header: %s"), *Filename );
					
					//scion superville: Add options to always generate headers and to force overwrites
					UBOOL ReplaceHeaders	= ParseParam( appCmdLine(), TEXT("ReplaceHeads") );
					UBOOL ForceHeaders		= ParseParam( appCmdLine(), TEXT("ForceHeads") );
					
					if( !UExporter::ExportToFile( UObject::StaticClass(), NULL, *Filename, !ReplaceHeaders, 1, ForceHeaders ) )
						break;
				}

				// Save package.
				ULinkerLoad* Conform = NULL;
				
				if( !ParseParam(appCmdLine(),TEXT("NOCONFORM")) )
				{
					BeginLoad();
					Conform = UObject::GetPackageLinker( CreatePackage(NULL,*(US+Pkg+TEXT("_OLD"))), *(FString(TEXT("..")) * TEXT("GUIRes") * Pkg + TEXT(".u")), LOAD_NoWarn|LOAD_NoVerify, NULL, NULL );
					EndLoad();
					if( Conform )
					{
						debugf( TEXT("Conforming: %s"), Pkg );
						for ( TObjectIterator<UClass> It; It; ++It )
						{
							if ( It->IsIn(PkgObject) )
								It->GetDefaultObject()->LoadConfig(1);
						}
					}
				}
				SavePackage( PkgObject, NULL, RF_Standalone, *(FString(Pkg)+TEXT(".u")), GError, Conform );
			}
#if 1 // rwd
			if (EndPkg[0] != '\0' &&
				!appStricmp(EndPkg,Pkg))
			{
				break;
			}
#endif
		}
		GIsRequestingExit=1;
		return 0;

		unguard;
	}
};

/* epic ===============================================
* ::ImportClassConfigs
* Author: sz
*
* Load class configs to class defaults
*
* =====================================================
*/
UBOOL UMakeCommandlet::ImportClassConfigs( UPackage* ScriptPkg )
{
	guard(UMakeCommandlet::ImportClassConfigs);
	check(ScriptPkg);

	debugf( TEXT("Loading configs for [%s]"), ScriptPkg->GetFullName() );

	for( TObjectIterator<UClass> It; It; ++It )
	{
		UClass* pClass = *It;
		if( pClass->IsIn(ScriptPkg) ) {
			pClass->GetDefaultObject()->LoadConfig(1);
		}

	}
	for( TObjectIterator<UClass> It; It; ++It )
	{
		UClass* pClass = *It;
		if( pClass->IsIn(ScriptPkg) ) {
			pClass->ClassFlags |= CLASS_ConfigLoaded;
		}
	}

	return 1;
	unguard;
}

/* epic ===============================================
* ::ImportClassLocalized
* Author: sz
*
* Load localized config values to class defaults.
*
* =====================================================
*/
UBOOL UMakeCommandlet::ImportClassLocalized( UPackage* ScriptPkg )
{
	guard(UMakeCommandlet::ImportClassLocalized);
	check(ScriptPkg);

	debugf( TEXT("Loading localized configs for [%s]"), ScriptPkg->GetFullName() );

	for( TObjectIterator<UClass> It; It; ++It )
	{
		UClass* pClass = *It;
		if( pClass->IsIn(ScriptPkg) ) {
			pClass->GetDefaultObject()->LoadLocalized();
		}
	}
	for( TObjectIterator<UClass> It; It; ++It )
	{
		UClass* pClass = *It;
		if( pClass->IsIn(ScriptPkg) ) {
			pClass->ClassFlags |= CLASS_LocalizedLoaded;
		}
	}

	return 1;
	unguard;
}

IMPLEMENT_CLASS(UMakeCommandlet)

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

