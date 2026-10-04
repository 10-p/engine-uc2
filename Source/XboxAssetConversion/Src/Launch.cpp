/* ============================================================================
 * Author: jg
 * ============================================================================
 *
 * This EXE handles loading PC version content and saves it in the compact Xbox
 * format. It resaves each map, static mesh, animation, skeletal mesh, BSP, etc.
 * in a special set of directories. All asset conversion is done on the PC to
 * minimize the load times on the Xbox. Vertex shaders are spliced and optimized
 * on the PC since that process fragments memory badly and is super slow. All
 * pushbuffers are compiled on the PC so that loading is as fast as possible.
 *
 * ============================================================================
 */

#define STRICT
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>

#include <malloc.h>
#include <stdio.h>
#include <io.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>
#include <direct.h>

#include "Core.h"
#include "Unlinker.h"
#include "Engine.h"
#include "Editor.h"
#include "ULoadSaveManager.h"

#if __STATIC_LINK
#include "UnRender.h"
#include "UnNullRenderDevice.h"
#include "UnNet.h"
#include "D3DDrv.h"
#include "UnrealGame.h"
#include "UCElements.h"
#undef MM_MAX
#include "UCGameplay.h"
#include "GUI.h"
#include "WinDrv.h"
#include "UnIpDrv.h"
#include "UnTcpNetDriver.h"
#include "UnTcpNetDriverXbox.h"
#include "UnIpDrvCommandlets.h"
#endif

// General.
extern "C" {HINSTANCE hInstance;}
extern "C" {TCHAR GPackage[64]=TEXT("Avaris");}

// Memory allocator.
#include "FMallocAnsi.h"
FMallocAnsi Malloc;

// Log file.
#include "FOutputDeviceFile.h"
FOutputDeviceFile Log;

// Error handler.
#include "FOutputDeviceWindowsError.h"
FOutputDeviceWindowsError Error;

// Feedback.
#include "FFeedbackContextAnsi.h"
FFeedbackContextAnsi Warn;

// File manager.
#include "FFileManagerWindows.h"
FFileManagerWindows FileManager;

// Config.
#include "FConfigCacheIni.h"

//jg -- Thread synchronization factory
#include "UnThreadingWindows.h"
FSynchronizeFactoryWin SynchFactory;
FThreadFactoryWin ThreadFactory;

UBOOL	GLinearLoad				= 0;
UBOOL	GLinearSave				= 0;
DWORD	GLinearBytesSerialized	= 0;

// Linear file support
#include "FFileManagerLinear.h"

// Lists of items to load
#include "LinLoadDLOs.h"

// Set back savepackage so we can export correctly
TArray<FObjectExport>* GExportMap = NULL;

// Conversion types
enum EConversionTypes
{
	ECT_VertexShaders =		0x1,
	ECT_Maps =				0x2,
	ECT_StaticMeshes =		0x4,
	ECT_SkeletalMeshes =	0x8,
	ECT_Textures =			0x10,
	ECT_Script =			0x20,
	ECT_FileSizes =			0x40,
	ECT_TextureReport =		0x80,
	ECT_Characters =		0x100,
	ECT_AnimSets =			0x200,
	ECT_ALL =				0xFFFFFFFF
};


// Defines for the various paths used
// Static mesh dirs
#define IN_STATIC_MESH_DIR TEXT("..\\StaticMeshes")
#define OUT_STATIC_MESH_DIR TEXT("..\\XboxStaticMeshes")
#define IN_STATIC_MESH_DIR_PF TEXT("..\\StaticMeshes\\%s")
#define OUT_STATIC_MESH_DIR_PF TEXT("..\\XboxStaticMeshes\\%s")
#define STATIC_MESH_EXT TEXT("*.usx")

// Vertex shader dir
#define VERTEX_SHADER_DIR TEXT("..\\XboxVertexShaders")
#define VERTEX_SHADER_PACKAGE TEXT("..\\XboxVertexShaders\\VertexShaders.xvs")

// Map dirs
//#define FAST_TEST 1
#ifdef FAST_TEST
#define IN_MAPS_DIR TEXT("..\\FastMaps")
#define OUT_MAPS_DIR TEXT("..\\XboxFastMaps")
#define IN_MAPS_DIR_PF TEXT("..\\FastMaps\\%s")
#define OUT_MAPS_DIR_PF TEXT("..\\XboxFastMaps\\%s")
#else
#define IN_MAPS_DIR TEXT("..\\Maps")
#define OUT_MAPS_DIR TEXT("..\\XboxMaps")
#define IN_MAPS_DIR_PF TEXT("..\\Maps\\%s")
#define OUT_MAPS_DIR_PF TEXT("..\\XboxMaps\\%s")
#endif

// Skeletal dirs
#define IN_SKELETAL_MESH_DIR TEXT("..\\Animations")
#define OUT_SKELETAL_MESH_DIR TEXT("..\\XboxAnimations")
#define IN_SKELETAL_MESH_DIR_PF TEXT("..\\Animations\\%s")
#define OUT_SKELETAL_MESH_DIR_PF TEXT("..\\XboxAnimations\\%s")
#define SKELETAL_MESH_EXT TEXT("*.ukx")

// Exported texture dirs
#define OUT_TEXTURES_DIR TEXT("..\\XboxTextures")
#define IN_TEXTURES_DIR TEXT("..\\Textures")
#define IN_TEXTURES_PF TEXT("..\\Textures\\%s")
#define OUT_TEXTURES_PF TEXT("..\\XboxTextures\\%s")
#define OUT_TEXTURES_SRC_DIR TEXT("..\\XboxTextures\\Source")
#define OUT_TEXTURES_RDF_PF TEXT("..\\XboxTextures\\%s.rdf")
#define OUT_NATIVES_RDF TEXT("..\\XboxTextures\\Natives.rdf")
#define OUT_NATIVES_UMA_RDF TEXT("..\\XboxTextures\\Natives_uma.rdf")
#define OUT_NATIVES_PB_RDF TEXT("..\\XboxTextures\\Natives_pb.rdf")
#define BUNDLER_TOOL TEXT("uc2bundler")

// TUmaArray RDF name format
#define OUT_TUMAARRAY_RDF_PF TEXT("..\\XboxTextures\\%s_uma.rdf")
// Pushbuffer RDF name format
#define OUT_PUSHBUFFER_RDF_PF TEXT("..\\XboxTextures\\%s_pb.rdf")
// Animation sets export name
#define OUT_ANIMSET_RDF_PF TEXT("..\\XboxTextures\\%s_a_uma.rdf")

// Header file for generating file size object
#define FILE_SIZES_HEADER TEXT("..\\XboxDrv\\Inc\\XboxFileSizes.h")

// Texture report
#define TEXTURE_REPORT_FILE TEXT("UncompressedTextures.txt")

/**
 * Common method for showing progress to the console
 */
void PrintProgress(const TCHAR* Progress)
{
	warnf(NAME_Progress,TEXT("%-60s"),Progress);
}

/**
 * Common method for showing errors to the console
 */
void PrintError(const TCHAR* Error)
{
	warnf(TEXT("%-60s"),Error);
}

/**
 * Common method for showing categories of work on the console
 */
void PrintHeading(const TCHAR* Heading)
{
	warnf(NAME_Heading,TEXT("%s"),Heading);
}

/**
 * Searches all native objects for asset references. It builds a list of objects
 * that should be ignored due to them being referenced by native code.
 */
class FArchiveNativeAssetRefs : public FArchive
{
public:
	/**
	 * List of items to ignore
	 */
	TArray<UObject*> IgnoreList;

	/**
	 * Initializes variables and starts the serialization search
	 *
	 * @param InSearchObject The object to start the search on
	 */
	FArchiveNativeAssetRefs(void)
	{
		// Mark natives for searching
		for (FObjectIterator It; It; ++It)
		{
			if (It->GetFlags() & RF_Native)
			{
				It->SetFlags(RF_TagExp);
			}
		}
		// Now search for RF_Native and objects marked for searching
		for (FObjectIterator It; It; ++It)
		{
			if ((It->GetFlags() & (RF_Native | RF_TagExp)) ||
				It->IsA(UClass::StaticClass()))
			{
				// Don't search this object twice
				It->ClearFlags(RF_TagExp);
				// Search it for asset refs
				It->Serialize(*this);
			}
		}
	}

	/**
	 * Serializes the reference to the object
	 */
	FArchive& operator<<( class UObject*& Obj )
	{
		// Add any asset to the ignore list
		if (Cast<UPrimitive>(Obj) || Cast<UMaterial>(Obj))
		{
			// Add to the list to skip later
			IgnoreList.AddUniqueItem(Obj);
			// Clear this so it won't be searched twice
			Obj->ClearFlags(RF_TagExp);
			// Recursively descend through this object's props
			Obj->Serialize(*this);
		}
		return *this;
	}
};

/**
 * Walks an object graph identifying which objects should be moved into a
 * given level. Those are bulk assets that need to be manipulated later.
 */
class FArchiveAssetMover : public FArchive
{
public:
	/**
	 * Holds a list of objects that need to be renamed into the level package
	 */
	TArray<UObject*> ObjectsToMove;
	/**
	 * The package that this search started with
	 */
	UObject* RootPackage;
	/**
	 * List of items to ignore
	 */
	TArray<UObject*>& IgnoreList;

	/**
	 * Initializes variables and starts the serialization search
	 *
	 * @param InSearchObject The object to start the search on
	 */
	FArchiveAssetMover(UObject* InSearchObject,
		TArray<UObject*>& InIgnoreList) : IgnoreList(InIgnoreList)
	{
		// Mark everything for searching
		for (FObjectIterator It; It; ++It)
		{
			It->SetFlags(RF_TagExp);
		}
		RootPackage = InSearchObject;
		// Now search through finding assets to move
		InSearchObject->Serialize(*this);
		// Get the outer of this object
		UObject* Outer = InSearchObject->GetOuter();
		check(Outer);
		// Some objects might be parallel rooted to the search object so
		// serialize any object with a common outer
		for (FObjectIterator It; It; ++It)
		{
			if (It->IsIn(Outer))
			{
				It->Serialize(*this);
			}
		}
	}

	/**
	 * Serializes the reference to the object
	 */
	FArchive& operator<<( class UObject*& Obj )
	{
		UBOOL bWasAdded = FALSE;
		// We need to skip packages
		if (Obj != NULL && Obj->GetLinker())
		{
			// Skip resources loaded in code packages
			if (Obj->GetLinker()->LinksToCode() == FALSE)
			{
				// Only add if it isn't already in the package, isn't to be
				// skipped, and hasn't already been added
				if (Obj->IsIn(RootPackage) == FALSE &&
					Obj->IsIn(RootPackage->GetOuter()) == FALSE &&
					(Obj->GetFlags() & RF_Native) == FALSE &&
					IgnoreList.FindItemIndex(Obj) == INDEX_NONE &&
					ObjectsToMove.FindItemIndex(Obj) == INDEX_NONE &&
					Cast<UPackage>(Obj) == NULL &&
					Cast<UClass>(Obj) == NULL)
				{
					// Add to the list to move
					ObjectsToMove.AddItem(Obj);
					// Make sure to recurse through these objects because
					// they might import meshes or other textures
					bWasAdded = TRUE;
				}
			}
		}
		// Search the referenced object
		if (Obj != NULL)
		{
			// Have we searched this yet?
			if (Obj->GetFlags() & RF_TagExp &&
				(Cast<AActor>(Obj) || Cast<UPrimitive>(Obj) || Cast<UMaterial>(Obj) || bWasAdded))
			{
				Obj->ClearFlags(RF_TagExp);
				// Recursively descend through this object's props
				Obj->Serialize(*this);
			}
		}
		return *this;
	}
};

/* ============================================================================
 * ::CleanDirectory
 * Author: jg
 * 
 * Deletes the directory and all files in it. Makes sure there is a directory
 * with the specified name.
 *
 * ============================================================================
 */
void CleanDirectory(const TCHAR* Dir)
{
	guard(CleanDirectory);
	if (GFileManager->DeleteDirectory(Dir,0,1) == 0)
	{
		appErrorf(TEXT("Failed to remove directory tree: %s"),Dir);
	}
	if (GFileManager->MakeDirectory(Dir,1) == 0)
	{
		appErrorf(TEXT("Failed to create directory: %s"),Dir);
	}
	unguard;
}

/**
 * Launches an executable and waits for it to finish
 */
void LaunchExecutable(TCHAR* ExePath,const TCHAR* WorkingDir)
{
	guard(LaunchExecutable);
	STARTUPINFO si;
	// Init the startup info
	appMemzero(&si,sizeof(si));
	si.cb = sizeof(si);
	// Now init the process struct
	PROCESS_INFORMATION pi;
	appMemzero(&pi,sizeof(pi));
	// Launch the bundler tool
	if (CreateProcess(NULL,ExePath,NULL,NULL,0,
		BELOW_NORMAL_PRIORITY_CLASS,NULL,WorkingDir,&si,&pi))
	{
		// Wait for it to finish
		WaitForSingleObject(pi.hProcess,INFINITE);
		// Clean up the handles
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}
	else
	{
		PrintError(*FString::Printf(TEXT("Failed to execute (%s) in directory (%s)"),
			ExePath,WorkingDir));
	}
	unguard;
}

/**
 * Calls our special version of the bundler tool to process the RDF
 * file and make a XPR file.
 *
 * @param RDFName The name of the RDF file to process
 */
void CreateXPRFromRDF(const TCHAR* RDFName)
{
	guard(CreateXPRFromRDF);
	PrintProgress(*FString::Printf(TEXT("Bundling %s"),RDFName));
	// Build the path to the exe
	TCHAR Dir[_MAX_PATH];
	GetCurrentDirectory(_MAX_PATH,Dir);
	FString ExePath = FString::Printf(TEXT("\"%s\\%s\" %s -q"),Dir,BUNDLER_TOOL,
		RDFName);
	// Run the bundler on this RDF
	LaunchExecutable((TCHAR*)*ExePath,OUT_TEXTURES_DIR);
	// Build the XPR name from the RDF name
	FString XPRName(RDFName);
	XPRName = XPRName.Left(XPRName.Len() - 4);
	XPRName += TEXT(".xpr");
	// Verify that it created the file
	if (GFileManager->FileSize(*XPRName) <= 0)
	{
		PrintError(*FString::Printf(TEXT("\r\nFailed to bundle data into %s"),XPRName));
	}
	unguard;
}

/**
 * Removes the mips data from any texture in the specified package
 *
 * @param Package the package data to strip out
 */
void StripTextureData(UObject* Package)
{
	guard(StripTextureData);
	PrintProgress(*FString::Printf(TEXT("Stripping texture data from %s"),Package->GetName()));
	// Find each texture in the package and strip out the 
	for (TObjectIterator<UTexture> It; It; ++It)
	{
		if (It->IsIn(Package))
		{
			// Truncate the mips data
			It->Mips.Empty();
		}
	}
	unguard;
}

/**
 * Loads the list of commonly needed items for a character
 */
void LoadCommonObjects(TArray<FName>& IgnoreList)
{
	guard(LoadCommonObjects);
	// Load the generically used objects
	LoadGeneralObjects();
	LoadGeneralClasses();
	unguard;
}

/**
 * Places all editor objects in the ignore list
 *
 * @param IgnoreList the list of items to ignore
 */
void BuildEditorIgnoreList(TArray<FName>& IgnoreList)
{
	guard(BuildEditorIgnoreList);
	// Find the editor & pickup packages
	UObject* Editor = UObject::StaticFindObject(UPackage::StaticClass(),
		NULL,TEXT("Editor"),1);
	check(Editor);
	// Find all objects in the editor package and ignore
	for (FObjectIterator It; It; ++It)
	{
		if (It->IsIn(Editor))
		{
			IgnoreList.AddUniqueItem(It->GetFName());
		}
	}
	unguard;
}

/**
 * Exports the level specific TUmaArray RDF file and builds the XPR
 *
 * @param Package the level to export data for
 */
void CreateTUmaArrayXPRs(const TCHAR* OutName,TArray<FName>& IgnoreList,
	TArray<FObjectExport>* ExportMap,TArray<FName>& ProcessedList,
	const TArray<UObject*>& ForceExport,UBOOL bIgnoreAnims = TRUE,
	UBOOL bAddToIgnoreList = FALSE)
{
	guard(CreateTUmaArrayXPRs);
	// This gets GCed each time through
	UBulkDataExporterRDF* pExporter =
		ConstructObject<UBulkDataExporterRDF>(UBulkDataExporterRDF::StaticClass());
	// Set the ignore & export items
	pExporter->SetIgnoreList(IgnoreList);
	pExporter->SetExportMap(ExportMap);
	pExporter->SetAlwaysExport(ForceExport);
	// Tell the exporter to skip animations
	if (bIgnoreAnims == TRUE)
	{
		pExporter->IgnoreAnimSets();
	}
	// Create the RDF file that will be processed by the bundler
	UExporter::ExportToFile(pExporter,pExporter,OutName,0,0,1);
	// Verify that the file was created
	if (GFileManager->FileSize(OutName) > 0)
	{
		// Execute the bundler tool
		CreateXPRFromRDF(OutName);
	}
	else
	{
		PrintError(*FString::Printf(TEXT("Failed to create %s"),OutName));
	}
	// Get the list of processed items for finding their packages later
	pExporter->GetProcessedList(ProcessedList);
	if (bAddToIgnoreList)
	{
		// Add the processed ones to the ignore list
		IgnoreList += ProcessedList;
	}
	unguard;
}

/**
 * Exports the level specific pushbuffer data to a RDF file and builds the XPR
 *
 * @param Package the level to export data for
 */
void CreatePushbufferXPRs(const TCHAR* OutName,TArray<FName>& IgnoreList,
	TArray<FObjectExport>* ExportMap,const TArray<UObject*>& ForceExport,
	UBOOL bAddToIgnoreList = FALSE)
{
	guard(CreateLevelTUmaArrayXPRs);
	// This gets GCed each time through
	UPushbufferExporterRDF* pExporter =
		ConstructObject<UPushbufferExporterRDF>(UPushbufferExporterRDF::StaticClass());
	// Set the ignore & export items
	pExporter->SetIgnoreList(IgnoreList);
	pExporter->SetExportMap(ExportMap);
	pExporter->SetAlwaysExport(ForceExport);
	// Create the RDF file that will be processed by the bundler
	UExporter::ExportToFile(pExporter,pExporter,OutName,0,0,1);
	// Verify that the file was created
	if (GFileManager->FileSize(OutName) > 0)
	{
		// Execute the bundler tool
		CreateXPRFromRDF(OutName);
	}
	else
	{
		PrintError(*FString::Printf(TEXT("Failed to create %s"),OutName));
	}
	if (bAddToIgnoreList)
	{
		TArray<FName> Processed;
		// Add the processed ones to the ignore list
		pExporter->GetProcessedList(Processed);
		IgnoreList += Processed;
	}
	unguard;
}

/**
 * Exports textures to RDF/XPR files for all native classes that are loaded
 *
 * @param IgnoreList The list of textures found when loading natives
 */
void CreateNativesRDF(TArray<FName>& IgnoreList)
{
	guard(CreateNativesRDF);
	PrintProgress(TEXT("Creating natives.rdf"));
	// Add all editor objects to the ignore list
	BuildEditorIgnoreList(IgnoreList);
	// Load the commonly needed stuff
	LoadCommonObjects(IgnoreList);
	// This gets GCed each time through
	UMaterialExporterRDF* pExporter = ConstructObject<UMaterialExporterRDF>(
		UMaterialExporterRDF::StaticClass());
	// Create the RDF file that will be processed by the bundler
	UExporter::ExportToFile(pExporter,pExporter,OUT_NATIVES_RDF,0,0,1);
	TArray<FName> ProcessedList;
	// Build the list of materials to ignore for future exports
	pExporter->GetProcessedList(ProcessedList);
	// Add these to the list to ignore
	IgnoreList += ProcessedList;
	// Execute the bundler tool
	CreateXPRFromRDF(OUT_NATIVES_RDF);
	// Now build the other native XPRs
	CreatePushbufferXPRs(OUT_NATIVES_PB_RDF,IgnoreList,NULL,TArray<UObject*>());
	CreateTUmaArrayXPRs(OUT_NATIVES_UMA_RDF,IgnoreList,NULL,ProcessedList,
		TArray<UObject*>(),FALSE,TRUE);
	unguard;
}

/**
 * Exports the level specific texture RDF file and builds the XPR
 *
 * @param Package the level to export data for
 * @param IgnoreList the list of previously exported textures
 */
void CreateLevelTextureXPRs(UObject* Package,TArray<FName>& IgnoreList)
{
	guard(CreateLevelTextureXPRs);
	// Let Mike see progress on the build machines window
	PrintProgress(*FString::Printf(TEXT("Creating RDF for %s"),Package->GetName()));
	// Use the package name as the name of the RDF for this map
	FString OutName = FString::Printf(OUT_TEXTURES_RDF_PF,
		Package->GetName());
	// This gets GCed each time through
	UMaterialExporterRDF* pExporter =
		ConstructObject<UMaterialExporterRDF>(UMaterialExporterRDF::StaticClass());
	// Don't export these again
	pExporter->SetIgnoreList(IgnoreList);
	// Create the RDF file that will be processed by the bundler
	UExporter::ExportToFile(Package,pExporter,*OutName,0,0,1);
	// Verify that the file was created
	if (GFileManager->FileSize(*OutName) > 0)
	{
		// Execute the bundler tool
		CreateXPRFromRDF(*OutName);
	}
	else
	{
		PrintError(*FString::Printf(TEXT("Failed to create %s"),*OutName));
	}
	unguard;

}

/**
 * Exports the level specific TUmaArray RDF file and builds the XPR
 *
 * @param Package the level to export data for
 */
void CreateTUmaArrayXPRs(UObject* Package,TArray<FName>& IgnoreList,
	TArray<FObjectExport>* ExportMap,
	const TArray<UObject*>& ForceExport = TArray<UObject*>())
{
	guard(CreateLevelTUmaArrayXPRs);
	// Let Mike see progress on the build machines window
	PrintProgress(*FString::Printf(TEXT("Creating TUmaArray RDF for %s"),
		Package->GetName()));
	// Use the package name as the name of the RDF for this map
	FString OutName = FString::Printf(OUT_TUMAARRAY_RDF_PF,
		Package->GetName());
	TArray<FName> ProcessedList;
	CreateTUmaArrayXPRs(*OutName,IgnoreList,ExportMap,ProcessedList,
		ForceExport);
	unguard;
}

/**
 * Exports the level specific pushbuffer data to a RDF file and builds the XPR
 *
 * @param Package the level to export data for
 */
void CreatePushbufferXPRs(UObject* Package,TArray<FName>& IgnoreList,
	TArray<FObjectExport>* ExportMap,
	const TArray<UObject*>& ForceExport = TArray<UObject*>())
{
	guard(CreateLevelTUmaArrayXPRs);
	// Let Mike see progress on the build machines window
	PrintProgress(*FString::Printf(TEXT("Creating pushbuffer RDF for %s"),
		Package->GetName()));
	// Use the package name as the name of the RDF for this map
	FString OutName = FString::Printf(OUT_PUSHBUFFER_RDF_PF,
		Package->GetName());
	CreatePushbufferXPRs(*OutName,IgnoreList,ExportMap,ForceExport);
	unguard;
}

/** 
 * Exports the level specific RDF files and builds the XPRs
 *
 * @param Package the level to export data for
 * @param IgnoreList the list of previously exported textures
 * @param ExportMap the export table to use for exporting
 * @param ForceExport a list of objects to always export
 */
void CreateLevelXPRs(UObject* Package,TArray<FName>& IgnoreList,
	TArray<FObjectExport>* ExportMap,const TArray<UObject*>& ForceExport)
{
	guard(CreateLevelXPRs);
	// Don't create an XPR for entry
	if (appStrnicmp(Package->GetName(),TEXT("Entry"),5) != 0)
	{
		// Do level textures
		CreateLevelTextureXPRs(Package,IgnoreList);
		// Now do level bulk TUmaArray data
		CreateTUmaArrayXPRs(Package,IgnoreList,ExportMap,ForceExport);
		// Now do the pushbuffer data for the level
		CreatePushbufferXPRs(Package,IgnoreList,ExportMap,ForceExport);
	}
	unguard;
}

/* ============================================================================
 * ::ConvertPackages
 * Author: jg
 * 
 * Loads and resaves a list of packages
 *
 * inputs:	InDir -- The source directory
 *			OutDir -- The directory to write the converted files to
 *			Ext -- The package extension to scan for
 *			InDirPF -- The source directory printf string
 *			OutDirPF -- The dest directory printf string
 *
 * ============================================================================
 */
void ConvertPackages(const TCHAR* InDir,const TCHAR* OutDir,const TCHAR* Ext,
	const TCHAR* InDirPF,const TCHAR* OutDirPF)
{
	guard(ConvertPackage);
	// Make sure the destination folders are cleared and created
	CleanDirectory(OutDir);
	// Get a list of the packages
	TArray<FString> List = GFileManager->FindFiles(
		*FString::Printf(TEXT("%s\\%s"),InDir,Ext),1,0);
	// Loop through each package and save it to the new location
	for (INT Index = 0; Index < List.Num(); Index++)
	{
		// Build the in/out filenames
		const FString& PackageName = List(Index);
		FString InName = FString::Printf(InDirPF,PackageName);
		FString OutName = FString::Printf(OutDirPF,PackageName);
		PrintProgress(*FString::Printf(TEXT("Loading %s"),*InName));
		// Load the package which will get the data in Xbox format
		UObject* Package = NULL;
		try
		{
			Package = UObject::LoadPackage(NULL,*InName,0);
		}
		catch(...)
		{
		}
		if (Package != NULL)
		{
			try
			{
				PrintProgress(*FString::Printf(TEXT("Saving %s"),*OutName));
				// Save the data in Xbox format
				UObject::SavePackage(Package,NULL,RF_Standalone,*OutName,GError);
			}
			catch(...)
			{
				PrintError(*FString::Printf(TEXT("Save failed for %s"),*OutName));
			}
		}
		else
		{
			PrintError(*FString::Printf(TEXT("Failed to load %s"),*InName));
		}
		try
		{
			// Discard the loaded objects
			UObject::CollectGarbage(RF_Native);
		}
		catch(...)
		{
		}
	}
	unguard;
}

/**
 * Iterates through all terrain actors and rebuild the render data for the
 * optimized format
 */
void ConvertTerrain(void)
{
	guard(ConvertTerrain);
	// Iterate through the objects and find all terrain
	for (TObjectIterator<ATerrainInfo> It; It; ++It)
	{
		// Force the terrain to rebuild
		It->CheckComputeDataOnLoad();
	}
	unguard;
}

/**
 * Takes all of the non-code imports and renames them into the level's package
 *
 * @param Level the level to move assets into
 */
void MergeAssetsIntoLevel(UObject* Level,TArray<FName>& IgnoreList)
{
	guard(MergeAssetsIntoLevel);
	// Finds all assets referenced by native classes
	FArchiveNativeAssetRefs Ignore;
	// Build a list of assets to move into the level
	FArchiveAssetMover Mover(Level,Ignore.IgnoreList);
	// Now move the full list of objects over
	for (INT Index = 0; Index < Mover.ObjectsToMove.Num(); Index++)
	{
		UObject* Object = Mover.ObjectsToMove(Index);
		// Ignore the ones from the native loading process
		if (IgnoreList.FindItemIndex(Object->GetFName()) == INDEX_NONE)
		{
			// Create a terse name for it to save nametable space
			// cla stands for converted level asset
			const FString& NewName = FString::Printf(TEXT("cla%d"),Index);
//			Object->Rename(Object->GetName(),Level);
			Object->Rename(*NewName,Level);
		}
		else
		{
			debugf(TEXT("Object (%s) is on the ignore list"),Object->GetName());
		}
	}
	unguard;
}

/**
 * Iterates all projectors for a level and rebuilds them so they clip
 * their geometry
 *
 * @param Level the level to rebuild projectors for
 */
void RebuildProjectors(ULevel* Level)
{
	guard(RebuildProjectors);
	// scion sz - create collision hash and add all actors to it
	Level->SetActorCollision( 1 );
	check( Level->Hash );
	// scion sz - only keep the static mesh actors in the octree
	for( INT ActorIdx=0; ActorIdx < Level->Actors.Num(); ActorIdx++ )
	{
        AActor* Actor = Level->Actors(ActorIdx);
		if( Actor && 
			Actor->StaticMesh == NULL )
		{
            Level->Hash->RemoveActor( Actor );			
		}
	}
	// Find each projector in the actor list and rebuild
	for (INT ActorIdx = 0; ActorIdx < Level->Actors.Num(); ActorIdx++)
	{
		AProjector* Projector = Cast<AProjector>(Level->Actors(ActorIdx));
		if (Projector &&
			Projector->bLevelStatic &&
			Projector->bDynamicAttach == FALSE)
		{
			// Figure out which primitives this hits
			Projector->Attach();
			// Now clip them into a static primitive
			Projector->RebuildStaticPrimsBatch();
		}
	}
	unguard;
}

/* ============================================================================
 * ::ConvertMaps
 * Author: jg
 * 
 * Loads each map and saves it out to the new directory. This will convert the
 * BSP and assets stored in MyLevel. It also forces terrain to build it's xbox
 * optimized data structures. It moves all non-code imports into the level to
 * reduce ULinker overhead and to make certain exporting of bulk mesh data
 * easier.
 *
 * ============================================================================
 */
void ConvertMaps(TArray<FName>& IgnoreList)
{
	guard(ConvertMaps);
	// Get a list of the maps to convert
#if 1
	TArray<FString> MapList = GFileManager->FindFiles(
		*FString::Printf(TEXT("%s\\%s"),IN_MAPS_DIR,TEXT("*.unr")),1,0);
#else
	TArray<FString> MapList;
//	new(MapList)FString(TEXT("menu.unr"));
	new(MapList)FString(TEXT("od-cancer.unr"));
//	new(MapList)FString(TEXT("act-*.unr"));
//	TArray<FString> MapList = GFileManager->FindFiles(
//		*FString::Printf(TEXT("%s\\%s"),IN_MAPS_DIR,TEXT("ctf-Total*.unr")),1,0);
#endif
	// Loop through each map and save it to the new location
	for (INT Index = 0; Index < MapList.Num(); Index++)
	{
		// Build the in/out filenames
		const FString& MapName = MapList(Index);
		// Don't convert autosaves
		if (MapName.Left(4) != TEXT("Auto"))
		{
			FString InName = FString::Printf(IN_MAPS_DIR_PF,MapName);
			FString OutName = FString::Printf(OUT_MAPS_DIR_PF,MapName);
			PrintProgress(*FString::Printf(TEXT("Loading %s"),*InName));
			// Load the package which will get the data in Xbox format
			UObject* Package = UObject::LoadPackage(NULL,*InName,0);
			if (Package != NULL)
			{
				// Make sure we have a level object
				ULevel* Level = FindObject<ULevel>(Package,TEXT("MyLevel"));
				check(Level);
				// Rebuild the terrain so the weight maps get serialized
				ConvertTerrain();
				// Force build all static projectors so they are exported
				RebuildProjectors(Level);
				// List of objects to force export
				TArray<UObject*> ForceExport;
				// Figure out type
				UBOOL bIsMenu = appStrnicmp(*MapName,TEXT("menu"),4) == 0;
				UBOOL bIsCTFGame = appStrnicmp(*MapName,TEXT("CTF"),3) == 0;
				UBOOL bIsOverdose = appStrnicmp(*MapName,TEXT("OD"),2) == 0;
				// Do the special preloading of assets by type
				if (bIsMenu == TRUE)
				{
					LoadMenuClasses();
					LoadMenuObjects();
				}
				else if (bIsCTFGame == TRUE)
				{
					LoadCTFClassesObjects(ForceExport);
				}
				else if (bIsOverdose == TRUE)
				{
					LoadOverdoseClassesObjects(ForceExport);
				}
				// Move all the assets into the level package so we can
				// reduce linker overhead and strip out bulk data
				MergeAssetsIntoLevel(Level,IgnoreList);
				PrintProgress(*FString::Printf(TEXT("Reordering export table %s"),
					*OutName));
				// Keep a copy of the export map so we can export in order of
				// use to make loading times faster
				TArray<FObjectExport> ExportMap;
				GExportMap = &ExportMap;
				// This will recreate the export table so I can use it to
				// export all data in the same exact order it will be loaded
				// in. This allows for fixup optimizations
				UObject::SavePackage(Package,Level,0,*OutName,GError,NULL);
				// Use the common method for creating level XPRs
				CreateLevelXPRs(Package,IgnoreList,GExportMap,ForceExport);
				// We don't need the mips for any texture in the level so 
				// discard them
				StripTextureData(Package);
				GExportMap = NULL;
				// Save the changes out
				PrintProgress(*FString::Printf(TEXT("Saving %s"),*OutName));
				UObject::SavePackage(Package,Level,0,*OutName,GError,NULL);
				// Reset loader
				UObject::ResetLoaders(NULL,0,1);
			}
			else
			{
				PrintError(*FString::Printf(TEXT("\r\nFailed to load %s"),*InName));
			}
			// Discard the loaded objects
			UObject::CollectGarbage(RF_Native);
		}
		// This is in here in case there is a problem with an asset that is
		// linked natively preventing the level from being cleaned up
		UObject::StaticExec(TEXT("obj refs class=Level name=MyLevel"));
	}
	unguard;
}

/* ============================================================================
 * ::AddShadersToCache
 * Author: jg
 * 
 * Iterates through a set of strings that represents vertex shader enums and
 * adds the compiled versions to the vertex shader cache object
 *
 * inputs:	pCache -- The vertex shader cache object to add to
 *			RenDev -- The render device used during compilation
 *			Enums -- The list of vertex shader enums to add
 *
 * ============================================================================
 */
void AddShadersToCache(UVertexShaderCache* pCache,UD3DRenderDevice* RenDev,
	const TArrayNoInit<FString>& Enums)
{
	guard(AddShadersToCache);
	// For each shader enum in the global vertex shader config
	for (INT Index = 0; Index < Enums.Num(); Index++)
	{
		PrintProgress(*FString::Printf(TEXT("Compiling %s"),*Enums(Index)));
		// Convert the string into a QWORD
		QWORD VertexShaderEnum = (QWORD)appAtoi64(*Enums(Index));
		// Create a new optimized shader with this enum
		FD3DCombinedVertexShader* pVShader = NEW
			FD3DCombinedVertexShader(RenDev,VertexShaderEnum);
		// Add the enum to the serializer object
		pCache->AddToCache(VertexShaderEnum,pVShader->CombinedMicrocode,
			pVShader->CombinedMicrocodeSize);
		// We are done with this so dump it
		delete pVShader;
	}
	unguard;
}

/* ============================================================================
 * ::CreateVertexShaders
 * Author: jg
 * 
 * Creates a package with all of the vertex shaders pre-built in it, so that
 * there are no runtime compilation of shaders that are in our cache. Shaders
 * not in one of our configurations will still be run time compiled
 *
 * ============================================================================
 */
void CreateVertexShaders(void)
{
	guard(CreateVertexShaders);
	// Make sure the destination folder is cleared and created
	CleanDirectory(VERTEX_SHADER_DIR);
	// Create the package
	UPackage* pPackage = UObject::CreatePackage(NULL,VERTEX_SHADER_PACKAGE);
	check(pPackage);
	// Now create our serializer object in that package
	UVertexShaderCache* pCache = ConstructObject<UVertexShaderCache>(
		UVertexShaderCache::StaticClass(),pPackage,
		FName(TEXT("VertexShaderCache")),RF_Public | RF_Standalone);
	check(pCache);
	// Create the D3D driver
	UClass* RenDevClass = UObject::StaticLoadClass(URenderDevice::StaticClass(),
		NULL,TEXT("ini:Engine.Engine.RenderDevice"),NULL,LOAD_NoFail,NULL);
	UD3DRenderDevice* RenDev = ConstructObject<UD3DRenderDevice>(RenDevClass);
	RenDev->Init();
	// Force the config to be loaded for the two objects
	RenDev->VertexShaderConfigs->LoadConfig();
	RenDev->UserVertexShaderConfigs->LoadConfig();
	// Add all of the global vertex shaders to the cache
	AddShadersToCache(pCache,RenDev,RenDev->VertexShaderConfigs->VertexShaderEnums);
	// Add all of the user vertex shaders to the cache
	AddShadersToCache(pCache,RenDev,RenDev->UserVertexShaderConfigs->VertexShaderEnums);
	// Save the package and GC
	UObject::SavePackage(pPackage,NULL,RF_Standalone,VERTEX_SHADER_PACKAGE,GError);
	// Discard the loaded objects
	UObject::CollectGarbage(RF_Native);
	unguard;
}

/* ============================================================================
 * ::OutputFileSizesStart
 * Author: jg
 * 
 * Starts the declaration of the TOC object
 *
 * ============================================================================
 */
void OutputFileSizesStart(FOutputDevice& Ar)
{
	guard(OutputFileSizesStart);
	Ar.Log(TEXT("#include \"XboxFileSizesPre.h\"\r\n"));
	Ar.Log(TEXT("\tstatic FFileData FileSystemTOC[] = \r\n"));
	Ar.Log(TEXT("\t{\r\n"));
	unguard;
}

/* ============================================================================
 * ::OutputFileSizesEnd
 * Author: jg
 * 
 * Ends the declaration of the TOC object
 *
 * ============================================================================
 */
void OutputFileSizesEnd(FOutputDevice& Ar)
{
	guard(OutputFileSizesEnd);
	Ar.Log(TEXT("\t\t{ NULL, NULL, 0 }\r\n"));
	Ar.Log(TEXT("\t};\r\n"));
	Ar.Log(TEXT("#include \"XboxFileSizesPost.h\"\r\n"));
	unguard;
}

/* ============================================================================
 * ::AddFilesToFileSizes
 * Author: jg
 * 
 * Dumps the information for the specified file/directory
 *
 * ============================================================================
 */
void AddFilesToFileSizes(FOutputDevice& Ar,const TCHAR* DvdPath,
	const TCHAR* Path,const TCHAR* File)
{
	guard(AddFilesToFileSizes);
	FString FilePath = FString::Printf(TEXT("%s%s"),Path,File);
	// Get the list of files to process
	TArray<FString> Files = GFileManager->FindFiles(*FilePath,1,0);
	// Dump the information for each file in the list
	for (INT Index = 0; Index < Files.Num(); Index++)
	{
		const TCHAR* FileName = *Files(Index);
		FString FullPath = FString::Printf(TEXT("%s%s"),Path,FileName);
		// Get the file size (note this must be post conversion to be correct)
		INT FileSize = GFileManager->FileSize(*FullPath);
		// Now dump this file's info
		if (DvdPath != NULL)
		{
			Ar.Logf(TEXT("\t\t{ TEXT(\"%s\"), TEXT(\"%s\"), %d },\r\n"),
				DvdPath,FileName,FileSize);
		}
		else
		{
			Ar.Logf(TEXT("\t\t{ NULL, TEXT(\"%s\"), %d },\r\n"),
				FileName,FileSize);
		}
	}
	unguard;
}

/* ============================================================================
 * ::GenerateFileSizes
 * Author: jg
 * 
 * Goes through all of our directories figuring out the sizes of files and spits
 * those out into a table of contents class that is used for checking cached
 * state and by linear loads for file sizes without the original files around
 *
 * ============================================================================
 */
void GenerateFileSizes(void)
{
	guard(GenerateFileSizes);
	FStringOutputDevice Ar;
	// Spit out the starting stuff that is not file specific
	OutputFileSizesStart(Ar);
	// Add each set of files to the TOC as we encounter them
	AddFilesToFileSizes(Ar,TEXT("D:\\\\System\\\\"),TEXT(""),TEXT("*.u"));
	AddFilesToFileSizes(Ar,TEXT("D:\\\\System\\\\"),TEXT(""),TEXT("*.ini"));
	AddFilesToFileSizes(Ar,TEXT("D:\\\\System\\\\"),TEXT(""),TEXT("*.int"));
	AddFilesToFileSizes(Ar,TEXT("D:\\\\System\\\\Ladders\\\\"),TEXT("Ladders\\"),TEXT("*.ini"));
	// LIN files must come before their corresponding UNR files
	AddFilesToFileSizes(Ar,TEXT("D:\\\\System\\\\"),TEXT(""),TEXT("script.lin"));
	AddFilesToFileSizes(Ar,TEXT("D:\\\\XboxMaps\\\\"),TEXT("..\\XboxMaps\\"),TEXT("*.lin"));
	AddFilesToFileSizes(Ar,TEXT("D:\\\\XboxMaps\\\\"),TEXT("..\\XboxMaps\\"),TEXT("*.unr"));
	AddFilesToFileSizes(Ar,TEXT("D:\\\\XboxAnimations\\\\"),TEXT("..\\XboxAnimations\\"),TEXT("*.ukx"));
	AddFilesToFileSizes(Ar,TEXT("D:\\\\XboxTextures\\\\"),TEXT("..\\XboxTextures\\"),TEXT("*.xpr"));
	AddFilesToFileSizes(Ar,TEXT("D:\\\\XboxTextures\\\\"),TEXT("..\\XboxTextures\\"),TEXT("*.utx"));
	AddFilesToFileSizes(Ar,TEXT("D:\\\\XboxStaticMeshes\\\\"),TEXT("..\\XboxStaticMeshes\\"),TEXT("*.usx"));
	AddFilesToFileSizes(Ar,TEXT("D:\\\\XboxVertexShaders\\\\"),TEXT("..\\XboxVertexShaders\\"),TEXT("*.xvs"));
	AddFilesToFileSizes(Ar,TEXT("D:\\\\System\\\\Localization\\\\int\\\\"),TEXT(".\\Localization\\int\\"),TEXT("*.int"));
	AddFilesToFileSizes(Ar,TEXT("D:\\\\System\\\\Localization\\\\fra\\\\"),TEXT(".\\Localization\\fra\\"),TEXT("*.fra"));
	AddFilesToFileSizes(Ar,TEXT("D:\\\\System\\\\Localization\\\\deu\\\\"),TEXT(".\\Localization\\deu\\"),TEXT("*.deu"));
	AddFilesToFileSizes(Ar,TEXT("D:\\\\System\\\\Localization\\\\ita\\\\"),TEXT(".\\Localization\\ita\\"),TEXT("*.ita"));
	AddFilesToFileSizes(Ar,TEXT("D:\\\\System\\\\Localization\\\\spa\\\\"),TEXT(".\\Localization\\spa\\"),TEXT("*.spa"));
	AddFilesToFileSizes(Ar,NULL,TEXT("..\\Textures\\movies\\"),TEXT("*.xmv"));
	AddFilesToFileSizes(Ar,NULL,TEXT("..\\Textures\\"),TEXT("*.xpr"));
	AddFilesToFileSizes(Ar,NULL,TEXT("..\\Sounds\\"),TEXT("*.xwb"));
	AddFilesToFileSizes(Ar,NULL,TEXT("..\\Sounds\\"),TEXT("*.xsb"));
	// Close the structure definition up
	OutputFileSizesEnd(Ar);
	// Make sure that we get rid of the old
	GFileManager->Delete(FILE_SIZES_HEADER);
	// Now output the header file
	appSaveStringToFile(Ar,FILE_SIZES_HEADER,GFileManager);
	unguard;
}

/* ============================================================================
 * ::LogUncompressedTextures
 * Author: jg
 * 
 * Loads each map and checks for uncompressed textures. Any uncompressed
 * textures are logged to a file.
 *
 * ============================================================================
 */
void LogUncompressedTextures(void)
{
	guard(LogUncompressedTextures);
	FStringOutputDevice Ar;
	// Build the ignore list so we don't dump non-map textures
	TArray<UTexture*> IgnoreList;
	for (TObjectIterator<UTexture> It; It; ++It)
	{
		// Get the render interface so we can query properties
		FBaseTexture* pTex = It->GetRenderInterface();
		if (pTex->GetFormat() != TEXF_DXT1 &&
			pTex->GetFormat() != TEXF_DXT3 &&
			pTex->GetFormat() != TEXF_DXT5)
		{
			IgnoreList.AddUniqueItem(*It);
		}
	}
	// Get a list of the maps to convert
	TArray<FString> MapList = GFileManager->FindFiles(
		*FString::Printf(TEXT("%s\\%s"),IN_MAPS_DIR,TEXT("*.unr")),1,0);
	// Loop through each map and save it to the new location
	for (INT Index = 0; Index < MapList.Num(); Index++)
	{
		const FString& MapName = MapList(Index);
		// Don't convert autosaves
		if (MapName.Left(4) != TEXT("Auto"))
		{
			PrintProgress(*FString::Printf(TEXT("Checking %s"),*MapName));
			// Load the package which will get the data in Xbox format
			UObject* Package = UObject::LoadPackage(NULL,*MapName,0);
			if (Package == NULL)
			{
				PrintError(*FString::Printf(TEXT("Failed to load %s"),*MapName));
			}
			// Search the loaded textures for uncompressed ones
			for (TObjectIterator<UTexture> It; It; ++It)
			{
				// Get the render interface so we can query properties
				FBaseTexture* pTex = It->GetRenderInterface();
				// Add uncompressed textures to the report
				if (pTex->GetFormat() != TEXF_DXT1 &&
					pTex->GetFormat() != TEXF_DXT3 &&
					pTex->GetFormat() != TEXF_DXT5 &&
					IgnoreList.FindItemIndex(*It) == INDEX_NONE)
				{
					Ar.Logf(TEXT("Texture '%s' in map (%s)\r\n"),
						It->GetFullName(),*MapName);
				}
			}
			// Discard the loaded objects
			UObject::CollectGarbage(RF_Native);
		}
	}
	// Dump the report of uncompressed textures
	appSaveStringToFile(Ar,TEXTURE_REPORT_FILE,GFileManager);
	unguard;
}

/**
 * Author: jg
 * 
 * Loads each texture package and strips out the texture data since it has
 * already been exported to RDF/XPR format
 */
void StripAllTextureData(void)
{
	guard(StripAllTextureData);
	// Get a list of the maps to convert
	TArray<FString> PackageList = GFileManager->FindFiles(
		*FString::Printf(TEXT("%s\\%s"),IN_TEXTURES_DIR,TEXT("*.utx")),1,0);
	// Loop through each texture package and save it to the new location
	for (INT Index = 0; Index < PackageList.Num(); Index++)
	{
		// Build the input filename
		const FString& PackageName = PackageList(Index);
		FString InName = FString::Printf(IN_TEXTURES_PF,PackageName);
		// Load the package which will get the data in Xbox format
		UObject* Package = UObject::LoadPackage(NULL,*InName,0);
		if (Package != NULL)
		{
			// Remove the mips data
			StripTextureData(Package);
			// Use the package name as the name of the RDF for this map
			FString OutName = FString::Printf(OUT_TEXTURES_PF,PackageList(Index));
			try
			{
				PrintProgress(*FString::Printf(TEXT("Saving %s"),*OutName));
				// Save the data in Xbox format
				UObject::SavePackage(Package,NULL,RF_Standalone,*OutName,GError);
			}
			catch(...)
			{
				PrintError(*FString::Printf(TEXT("Save failed for %s"),*OutName));
			}
		}
		else
		{
			PrintError(*FString::Printf(TEXT("Failed to load %s"),*InName));
		}
		try
		{
			// Discard the loaded objects
			UObject::CollectGarbage(RF_Native);
		}
		catch(...)
		{
			GWarn->Flush();
		}
	}
	unguard;
}

/**
 * Exports the textures that aren't currently on the ignore list
 *
 * @param IgnoreList the list of objects to ignore
 * @param RdfName the name of the RDF group being exported
 */
void ExportCharacterWeaponTextures(TArray<FName>& IgnoreList,
	const TCHAR* RdfName)
{
	guard(ExportCharacterData);
	// This gets GCed each time through
	UMaterialExporterRDF* pExporter = ConstructObject<UMaterialExporterRDF>(
		UMaterialExporterRDF::StaticClass());
	// Use the character name as the name of the RDF for this map
	FString OutName = FString::Printf(OUT_TEXTURES_RDF_PF,RdfName);
	// Don't include stuff that's been exported
	pExporter->SetIgnoreList(IgnoreList);
	// Create the RDF file that will be processed by the bundler
	UExporter::ExportToFile(pExporter,pExporter,*OutName,0,0,1);
	TArray<FName> ProcessedList;
	// Make sure that we don't export the same thing twice
	pExporter->GetProcessedList(ProcessedList);
#if 0//jg -- Temp until we fix duplicate references
	IgnoreList += ProcessedList;
#endif
	// Execute the bundler tool
	CreateXPRFromRDF(*OutName);
	unguard;
}

/**
 * Creates a new package with the specified name and the list of
 * objects to populate it with
 *
 * @param PackageName the name of the package to create
 * @param LoadedObjects the list of objects to rename into it
 */
UPackage* CreatePackageWithAssets(const TCHAR* PackageName,
	TArray<UObject*> LoadedObjects)
{
	guard(CreatePackageWithAssets);
	// Create the object with the specified name
	UPackage* Package = ConstructObject<UPackage>(UPackage::StaticClass(),NULL,
		FName(PackageName));
	check(Package);
	// Create a report of what was moved
	FStringOutputDevice Ar;
	Ar.Logf(TEXT("Objects moved into package %s:\r\n"),PackageName);
	// Move each object into this package
	for (INT Index = 0; Index < LoadedObjects.Num(); Index++)
	{
		UObject* Object = LoadedObjects(Index);
		// Only move assets nothing else
		if (Object != NULL && Object->GetLinker() &&
			Object->GetLinker()->LinksToCode() == FALSE &&
			(Object->IsA(UMaterial::StaticClass()) ||
			Object->IsA(USkeletalMesh::StaticClass()) ||
			Object->IsA(UStaticMesh::StaticClass())))
		{
			Ar.Logf(TEXT("%s\r\n"),Object->GetFullName());
			// Move the object into the new package keeping the same name
			Object->Rename(Object->GetName(),Package);
		}
	}
	// Save the report
	appSaveStringToFile(Ar,
		*FString::Printf(TEXT("..\\XboxTextures\\%s.txt"),PackageName),
		GFileManager);
	return Package;
	unguard;
}

/**
 * Compares the loaded objects to the processed set and find the package for
 * each object that was exported
 *
 * @param LoadedObjects the list of objects that were loaded
 * @param ProcessedList the list of items that were exported
 * @param LoadedPackages (out) the list of package that need stripping
 */
void BuildPackageSet(TArray<UObject*>& LoadedObjects,
	TArray<FName>& ProcessedList,TArray<FName>& LoadedPackages)
{
	guard(BuildPackageSet);
	// Now find all the packages that were touched by this
	for (INT Index = 0; Index < LoadedObjects.Num(); Index++)
	{
		UObject* Object = LoadedObjects(Index);
		// Don't process package style objects or ones not in the processed list
		if (Object && Object->GetOuter() &&
			ProcessedList.FindItemIndex(Object->GetFName()) != INDEX_NONE)
		{
			// Find the package by searching up the outer tree
			UObject* Outer = Object->GetOuter();
			while (Outer->GetOuter() != NULL)
			{
				Outer = Outer->GetOuter();
			}
			// Add the package name to the array if not there
			LoadedPackages.AddUniqueItem(Outer->GetFName());
		}
	}
	unguard;
}

/**
 * Loads the specified class, gets the needed objects/classes, loads them and
 * then exports the assets that come from that
 *
 * @param IgnoreList the list of objects to ignore
 * @param RdfName the name of the RDF group being exported
 * @param Class the class to load and get the items for
 * @param LoadedPackages the list of packages loaded during conversion
 * @param ProcessedObjects the full list of all object converted
 * @param bTeam whether this is a team XPR group or not
 */
void ExportCharacterWeaponData(TArray<FName>& IgnoreList,
	const TCHAR* RdfName,const TCHAR* Class,
	TArray<FName>& LoadedPackages,TArray<FName>& ProcessedObjects,
	UBOOL bTeam)
{
	guard(ExportCharacterData);
	PrintProgress(*FString::Printf(TEXT("Loading character data for %s"),RdfName));
	TArray<UObject*> LoadedObjects;
	// Load the base class for all commonly needed items
	UClass* UCClass = LoadObject<UClass>(NULL,Class,NULL,0,NULL);
	TArray<FDLOInfo> DLOList;
	// Get the list of items it needs
	UCClass->GetDefaultActor()->eventBuildLoadList(DLOList, bTeam, 1);
	// Loop through the items and load them
	for (INT Index = 0; Index < DLOList.Num(); Index++)
	{
		// Load this object and add it to be moved
		LoadedObjects.AddItem(UObject::StaticLoadObject(
			DLOList(Index).ObjectClass,NULL,
			*FString::Printf(TEXT("%s.%s"),*DLOList(Index).PackageName,
			*DLOList(Index).ObjectName),NULL,0,NULL));
	}
	PrintProgress(*FString::Printf(TEXT("Building texture XPR for %s"),RdfName));
	// Export textures to RDF/XPR
	ExportCharacterWeaponTextures(IgnoreList,RdfName);
	// Now build the uma XPR
	PrintProgress(*FString::Printf(TEXT("Building UMA XPR for %s"),RdfName));
	TArray<FName> ProcessedList;
	CreateTUmaArrayXPRs(*FString::Printf(OUT_TUMAARRAY_RDF_PF,RdfName),IgnoreList,
		NULL,ProcessedList,TArray<UObject*>());
	// Append to our global list
	ProcessedObjects += ProcessedList;
	// Build the list of packages that need stripping
	BuildPackageSet(LoadedObjects,ProcessedList,LoadedPackages);
	// And finally the pushbffer XPR
	PrintProgress(*FString::Printf(TEXT("Building pushbuffer XPR for %s"),RdfName));
	CreatePushbufferXPRs(*FString::Printf(OUT_PUSHBUFFER_RDF_PF,RdfName),
		IgnoreList,NULL,TArray<UObject*>());
	// Reset all loaders to force reloads
	UObject::ResetLoaders(NULL,0,1);
	// Do a GC
	UObject::CollectGarbage(RF_Native);
	unguard;
}

/**
 * Marks each UMA array as being in an XPR
 *
 * @param StaticMesh the mesh to make the changes on
 */
void StripStaticMesh(UStaticMesh* StaticMesh)
{
	guard(StripStaticMesh);
	// Make sure the array is valid
	if (StaticMesh->VertexStream.Vertices.Num() > 0)
	{
		StaticMesh->VertexStream.Vertices.MarkAsInXPR();
	}
	// Make sure the array is valid
	if (StaticMesh->ColorStream.Colors.Num() > 0)
	{
		StaticMesh->ColorStream.Colors.MarkAsInXPR();
	}
	// Make sure the array is valid
	if (StaticMesh->AlphaStream.Colors.Num() > 0)
	{
		StaticMesh->AlphaStream.Colors.MarkAsInXPR();
	}
	// Add each of the UV streams
	for (INT Index = 0; Index < StaticMesh->UVStreams.Num(); Index++)
	{
		// Make sure the array is valid
		if (StaticMesh->UVStreams(Index).UVs.Num() > 0)
		{
			StaticMesh->UVStreams(Index).UVs.MarkAsInXPR();
		}
	}
	unguard;
}

/**
 * Marks each UMA array as being in an XPR
 *
 * @param SkeletalMesh the mesh to make the changes on
 */
void StripSkeletalMesh(USkeletalMesh* SkeletalMesh)
{
	guard(StripSkeletalMesh);
	// Export each of the skeletal mesh LOD vertex streams
	for (INT Index = 0; Index < SkeletalMesh->LODModels.Num(); Index++)
	{
		// Make sure the array is valid
		if (SkeletalMesh->LODModels(Index).GPUVertexStream.Vertices.Num() > 0)
		{
			SkeletalMesh->LODModels(Index).GPUVertexStream.Vertices.MarkAsInXPR();
		}
	}
	unguard;
}

/**
 * Loads the package, searches it for objects that were changed, and stips
 * their data. Finally saves the package after stripping the data.
 *
 * @param LoadedPackages the list of packages to update
 * @param ProcessedList the list of objects that were exported
 */
void StripChangedPackages(TArray<FName>& LoadedPackages,
	TArray<FName>& ProcessedList)
{
	guard(StripChangedPackages);
	// Now go through the packages list, saving each one
	for (INT Index = 0; Index < LoadedPackages.Num(); Index++)
	{
		// Don't process texture packages
		if (appStrnicmp(*LoadedPackages(Index),TEXT("T_"),2) != 0)
		{
			// Load the package and all it's objects
			UObject* Package = UObject::LoadPackage(NULL,*LoadedPackages(Index),0);
			// Iterate all objects and zero their mesh data
			for (FObjectIterator It; It; ++It)
			{
				// Skip objects outside of ones in this package and was exported
				if (It->IsIn(Package) &&
					ProcessedList.FindItemIndex(It->GetFName()) != INDEX_NONE)
				{
					// Mark the various items as being in XPRs
					if (It->IsA(UStaticMesh::StaticClass()))
					{
						StripStaticMesh((UStaticMesh*)*It);
					}
					else if (It->IsA(USkeletalMesh::StaticClass()))
					{
						StripSkeletalMesh((USkeletalMesh*)*It);
					}
				}
			}
			FString OutName;
			// Check for static versus skeletal mesh
			if (appStrnicmp(*LoadedPackages(Index),TEXT("S_"),2) == 0)
			{
				// Build the output file name
				OutName = FString::Printf(TEXT("..\\XboxStaticMeshes\\%s.usx"),
					*LoadedPackages(Index));
			}
			else
			{
				// Build the output file name
				OutName = FString::Printf(TEXT("..\\XboxAnimations\\%s.ukx"),
					*LoadedPackages(Index));
			}
			// Save the package
			UObject::SavePackage(Package,NULL,RF_Standalone,*OutName,GError);
		}
	}
	unguard;
}


/** List of body meshes used by each character */
const TCHAR* BodyMeshes[] =
{
	"K_Male.Malcom_Body",
	"K_Skaarj.Arclite_Body",
	"K_Skaarj.Gorge_Body",
	"K_Female.Devastation_Body",
	"K_Skaarj.Raptor_Body",
	"K_Male.SecurityBot1_Body",
	"K_Male.NM_Body",
	"K_Male.NK_Merc_Anubis_body",
	"K_Male.Apophis_body",
	"K_Male.Hyena_Body",
	"K_Female.Nephtys_Body",
	"K_Female.NF_Body",
	"K_Male.Sobek_body",
	"K_Male.Brock_Body",
	"K_Female.Lilith_Body",
	"K_Female.Lauren_Body",
	"K_Female.Selket_Necris_Body",
	"K_Skaarj.Szalor_Body",
	"K_Skaarj.Torg_Body",
	"K_Male.Raiden_Body",
	NULL
};

/**
 * Exports the body and head meshes to their own XPR groups
 *
 * @param IgnoreList the list of items that
 */
void ExportBodyMeshes(TArray<FName>& IgnoreList)
{
	guard(ExportBodyMeshes);
	// Create our exporter
	UBulkDataExporterRDF* pExporter =
		ConstructObject<UBulkDataExporterRDF>(UBulkDataExporterRDF::StaticClass());
	// Create our pushbuffer exporter
	UPushbufferExporterRDF* pPBExporter =
		ConstructObject<UPushbufferExporterRDF>(UPushbufferExporterRDF::StaticClass());
	// Find and export our needed objects
	for (INT Index = 0; BodyMeshes[Index] != NULL; Index++)
	{
		USkeletalMesh* SkelMesh = LoadObject<USkeletalMesh>(NULL,BodyMeshes[Index],
			NULL,LOAD_NoFail,NULL);
		// Don't export something that was exported in the natives
		if (IgnoreList.FindItemIndex(SkelMesh->GetFName()) == INDEX_NONE)
		{
			// Set the item to export
			pExporter->SetSkeletalMesh(SkelMesh);
			pPBExporter->SetSkeletalMesh(SkelMesh);
			// Build the name to export
			FString OutName = FString::Printf(OUT_TUMAARRAY_RDF_PF,SkelMesh->GetName());
			// Create the RDF file that will be processed by the bundler
			UExporter::ExportToFile(pExporter,pExporter,*OutName,0,0,1);
			// Verify that the file was created
			if (GFileManager->FileSize(*OutName) > 0)
			{
				// Execute the bundler tool
				CreateXPRFromRDF(*OutName);
			}
			else
			{
				PrintError(*FString::Printf(TEXT("Failed to create %s"),*OutName));
			}
			// Build the name to export
			OutName = FString::Printf(OUT_PUSHBUFFER_RDF_PF,SkelMesh->GetName());
			// Create the RDF file that will be processed by the bundler
			UExporter::ExportToFile(pPBExporter,pPBExporter,*OutName,0,0,1);
			// Verify that the file was created
			if (GFileManager->FileSize(*OutName) > 0)
			{
				// Execute the bundler tool
				CreateXPRFromRDF(*OutName);
			}
			else
			{
				PrintError(*FString::Printf(TEXT("Failed to create %s"),*OutName));
			}
			// Don't export twice so add to our ignore list
			IgnoreList.AddItem(SkelMesh->GetFName());
		}
	}
	unguard;
}

/** The list of characters to generate */
const TCHAR* CharacterClasses[] =
{
	TEXT("UCCharacters.UCPawn_Ambient_Nali"),
	TEXT("UCCharacters.UCPawn_Human"),
	TEXT("UCCharacters.UCPawn_Human_Malcolm"),
	TEXT("UCCharacters.UCPawn_Human_Sapphire"),
	TEXT("UCCharacters.UCPawn_Jugg"),
	TEXT("UCCharacters.UCPawn_Jugg_Arclite"),
	TEXT("UCCharacters.UCPawn_Jugg_Gorge"),
	TEXT("UCCharacters.UCPawn_Jugg_Jackhammer"),
	TEXT("UCCharacters.UCPawn_Liandri"),
	TEXT("UCCharacters.UCPawn_Liandri_Corrosion"),
	TEXT("UCCharacters.UCPawn_Liandri_Devastation"),
	TEXT("UCCharacters.UCPawn_Liandri_Raptor"),
	TEXT("UCCharacters.UCPawn_Liandri_SecurityBotVOne"),
	TEXT("UCCharacters.UCPawn_Liandri_Syzygy"),
	TEXT("UCCharacters.UCPawn_Nakhti"),
	TEXT("UCCharacters.UCPawn_Nakhti_Anubis"),
	TEXT("UCCharacters.UCPawn_Nakhti_Anubis_Tutorial"),
	TEXT("UCCharacters.UCPawn_Nakhti_Apophis"),
	TEXT("UCCharacters.UCPawn_Nakhti_Darius"),
	TEXT("UCCharacters.UCPawn_Nakhti_Hyena"),
	TEXT("UCCharacters.UCPawn_Nakhti_Memphis"),
	TEXT("UCCharacters.UCPawn_Nakhti_Mirage"),
	TEXT("UCCharacters.UCPawn_Nakhti_Nepthys"),
	TEXT("UCCharacters.UCPawn_Nakhti_Ra"),
	TEXT("UCCharacters.UCPawn_Nakhti_Selket"),
	TEXT("UCCharacters.UCPawn_Nakhti_Sobek"),
	TEXT("UCCharacters.UCPawn_Nakhti_Sobek_Tutorial"),
	TEXT("UCCharacters.UCPawn_Necris"),
	TEXT("UCCharacters.UCPawn_Necris_Brock"),
	TEXT("UCCharacters.UCPawn_Necris_Calypso"),
	TEXT("UCCharacters.UCPawn_Necris_Judas"),
	TEXT("UCCharacters.UCPawn_Necris_Lauren"),
	TEXT("UCCharacters.UCPawn_Necris_Lilith"),
	TEXT("UCCharacters.UCPawn_Necris_Selket"),
	TEXT("UCCharacters.UCPawn_Skaarj"),
	TEXT("UCCharacters.UCPawn_Skaarj_Garek"),
	TEXT("UCCharacters.UCPawn_Skaarj_Korig"),
	TEXT("UCCharacters.UCPawn_Skaarj_Kraag"),
	TEXT("UCCharacters.UCPawn_Skaarj_Szalor"),
	TEXT("UCCharacters.UCPawn_Skaarj_Torgr"),
	TEXT("UCCharacters.UCPawn_Special_Raiden"),
	TEXT("UCCharacters.UCPawn_Tutorial_Dummy"),
	NULL
};
/** The name of the XPRs to generate */
const TCHAR* CharacterNames[] =
{
	TEXT("Nali"),
	TEXT("Human"),
	TEXT("Malcolm"),
	TEXT("Sapphire"),
	TEXT("Jugg"),
	TEXT("Arclite"),
	TEXT("Gorge"),
	TEXT("Jackhammer"),
	TEXT("Liandri"),
	TEXT("Corrosion"),
	TEXT("Devastation"),
	TEXT("Raptor"),
	TEXT("SecurityBotVOne"),
	TEXT("Syzygy"),
	TEXT("Nakhti"),
	TEXT("Anubis"),
	TEXT("Anubis_Tutorial"),
	TEXT("Apophis"),
	TEXT("Darius"),
	TEXT("Hyena"),
	TEXT("Memphis"),
	TEXT("Mirage"),
	TEXT("Nepthys"),
	TEXT("Ra"),
	TEXT("Selket"),
	TEXT("Sobek"),
	TEXT("Sobek_Tutorial"),
	TEXT("Necris"),
	TEXT("Brock"),
	TEXT("Calypso"),
	TEXT("Judas"),
	TEXT("Lauren"),
	TEXT("Lilith"),
	TEXT("Necris_Selket"),
	TEXT("Skaarj"),
	TEXT("Garek"),
	TEXT("Korig"),
	TEXT("Kraag"),
	TEXT("Szalor"),
	TEXT("Torgr"),
	TEXT("Raiden"),
	TEXT("Tutorial_Dummy"),
	NULL
};

/** The weapon classes to generate XPRs for */
const TCHAR* WeaponClasses[] =
{
	TEXT("UCInventory.UCWeap_Melee_Anubis"),
	TEXT("UCInventory.UCWeap_Melee_Anubis_Tutorial"),
	TEXT("UCInventory.UCWeap_Melee_Apophis"),
	TEXT("UCInventory.UCWeap_Melee_Arclite"),
	TEXT("UCInventory.UCWeap_Melee_Brock"),
	TEXT("UCInventory.UCWeap_Melee_Devastation"),
	TEXT("UCInventory.UCWeap_Melee_Gorge"),
	TEXT("UCInventory.UCWeap_Melee_Lauren"),
	TEXT("UCInventory.UCWeap_Melee_Malcolm"),
	TEXT("UCInventory.UCWeap_Melee_Raiden"),
	TEXT("UCInventory.UCWeap_Melee_Raptor"),
	TEXT("UCInventory.UCWeap_Melee_Sapphire"),
	TEXT("UCInventory.UCWeap_Melee_Selket"),
	TEXT("UCInventory.UCWeap_Melee_Sobek"),
	TEXT("UCInventory.UCWeap_Melee_Szalor"),
	TEXT("UCInventory.UCWeap_Melee_Torgr"),
	TEXT("UCInventory.UCWeap_Race_Liandri"),
	TEXT("UCInventory.UCWeap_Race_Nakhti"),
	TEXT("UCInventory.UCWeap_Race_Necris"),
	TEXT("UCInventory.UCWeap_Race_Raiden"),
	TEXT("UCInventory.UCWeap_Race_Skaarj"),
	TEXT("UCInventory.UCWeap_Race_Terran"),
	TEXT("UCInventory.UCWeap_Race_Liandri_Devastation"),
	TEXT("UCInventory.UCWeap_Race_Terran_Gorge"),
	TEXT("UCInventory.UCWeap_Race_Terran_Malcolm"),
	TEXT("UCInventory.UCWeap_CanisterGun"),
	TEXT("UCInventory.UCWeap_CanisterGun_Bio"),
	TEXT("UCInventory.UCWeap_CanisterGun_Gren"),
	TEXT("UCInventory.UCWeap_FlakCannon"),
	TEXT("UCInventory.UCWeap_RazorJack"),
	TEXT("UCInventory.UCWeap_RocketLauncher"),
	TEXT("UCInventory.UCWeap_ShockRifle_New"),
	TEXT("UCInventory.UCWeap_SniperRifle"),
	TEXT("UCInventory.UCWeap_Stinger"),
	TEXT("UCMutators.UCWeap_Instagib"),
	NULL
};

/** The name of the XPRs to generate */
const TCHAR* WeaponNames[] =
{
	TEXT("Melee_Anubis"),
	TEXT("Melee_Anubis_Tutorial"),
	TEXT("Melee_Apophis"),
	TEXT("Melee_Arclite"),
	TEXT("Melee_Brock"),
	TEXT("Melee_Devastation"),
	TEXT("Melee_Gorge"),
	TEXT("Melee_Lauren"),
	TEXT("Melee_Malcolm"),
	TEXT("Melee_Raiden"),
	TEXT("Melee_Raptor"),
	TEXT("Melee_Sapphire"),
	TEXT("Melee_Selket"),
	TEXT("Melee_Sobek"),
	TEXT("Melee_Szalor"),
	TEXT("Melee_Torgr"),
	TEXT("Race_Liandri"),
	TEXT("Race_Nakhti"),
	TEXT("Race_Necris"),
	TEXT("Race_Raiden"),
	TEXT("Race_Skaarj"),
	TEXT("Race_Terran"),
	TEXT("Race_Liandri_Devastation"),
	TEXT("Race_Terran_Gorge"),
	TEXT("Race_Terran_Malcolm"),
	TEXT("CanisterGun"),
	TEXT("Bio"),
	TEXT("Gren"),
	TEXT("FlakCannon"),
	TEXT("RazorJack"),
	TEXT("RocketLauncher"),
	TEXT("ShockRifle"),
	TEXT("SniperRifle"),
	TEXT("Stinger"),
	TEXT("Instagib"),
	NULL
};

/** List of hands XPRs to create */
const TCHAR* HandClasses[] =
{
	TEXT("UCCharacters.UCHands_Human_Female"),
	TEXT("UCCharacters.UCHands_Human_Malcolm"),
	TEXT("UCCharacters.UCHands_Jugg_Male"),
	TEXT("UCCharacters.UCHands_Jugg_Male_Arclite"),
	TEXT("UCCharacters.UCHands_Jugg_Male_Jackhammer"),
	TEXT("UCCharacters.UCHands_Liandri"),
	TEXT("UCCharacters.UCHands_Liandri_Devastation"),
	TEXT("UCCharacters.UCHands_Liandri_Syzygy"),
	TEXT("UCCharacters.UCHands_Nakhti_Female"),
	TEXT("UCCharacters.UCHands_Nakhti_Male"),
	TEXT("UCCharacters.UCHands_Nakhti_Male_Anubis"),
	TEXT("UCCharacters.UCHands_Nakhti_Male_Apophis"),
	TEXT("UCCharacters.UCHands_Nakhti_Male_Sobek"),
	TEXT("UCCharacters.UCHands_Necris_Female"),
	TEXT("UCCharacters.UCHands_Necris_Male"),
	TEXT("UCCharacters.UCHands_Skaarj"),
	TEXT("UCCharacters.UCHands_Skaarj_Garek"),
	TEXT("UCCharacters.UCHands_Skaarj_Kraag"),
	TEXT("UCCharacters.UCHands_Skaarj_Torgr"),
	TEXT("UCCharacters.UCHands_Special_Raiden"),
	NULL
};

/** The XPR names that will be generated */
const TCHAR* HandNames[] =
{
	TEXT("UCHands_Human_Female"),
	TEXT("UCHands_Human_Malcolm"),
	TEXT("UCHands_Jugg_Male"),
	TEXT("UCHands_Jugg_Male_Arclite"),
	TEXT("UCHands_Jugg_Male_Jackhammer"),
	TEXT("UCHands_Liandri"),
	TEXT("UCHands_Liandri_Devastation"),
	TEXT("UCHands_Liandri_Syzygy"),
	TEXT("UCHands_Nakhti_Female"),
	TEXT("UCHands_Nakhti_Male"),
	TEXT("UCHands_Nakhti_Male_Anubis"),
	TEXT("UCHands_Nakhti_Male_Apophis"),
	TEXT("UCHands_Nakhti_Male_Sobek"),
	TEXT("UCHands_Necris_Female"),
	TEXT("UCHands_Necris_Male"),
	TEXT("UCHands_Skaarj"),
	TEXT("UCHands_Skaarj_Garek"),
	TEXT("UCHands_Skaarj_Kraag"),
	TEXT("UCHands_Skaarj_Torgr"),
	TEXT("UCHands_Special_Raiden"),
	NULL
};

/**
 * Builds the character files. First the common ones, then the per character
 * files.
 */
void BuildCharacterFiles(TArray<FName>& IgnoreList)
{
	guard(BuildCharacterFiles);
	// Export all head/body meshes first (order dependent!)
	ExportBodyMeshes(IgnoreList);
	TArray<FName> LoadedPackages;
	TArray<FName> ProcessedObjects;
	// Build the XPRs for each character
	for (INT Index = 0; CharacterClasses[Index] != NULL; Index++)
	{
		ExportCharacterWeaponData(IgnoreList,CharacterNames[Index],
			CharacterClasses[Index],LoadedPackages,ProcessedObjects,FALSE);
	}
	// Build the team XPRs for each character
	for (INT Index = 0; CharacterClasses[Index] != NULL; Index++)
	{
		ExportCharacterWeaponData(IgnoreList,*FString::Printf(TEXT("%s_Team"),
			CharacterNames[Index]),CharacterClasses[Index],
			LoadedPackages,ProcessedObjects,TRUE);
	}
	// Now for each weapon
	for (INT Index = 0; WeaponClasses[Index] != NULL; Index++)
	{
		ExportCharacterWeaponData(IgnoreList,WeaponNames[Index],
			WeaponClasses[Index],LoadedPackages,ProcessedObjects,FALSE);
	}
	// Now for each set of hands
	for (INT Index = 0; HandClasses[Index] != NULL; Index++)
	{
		ExportCharacterWeaponData(IgnoreList,HandNames[Index],
			HandClasses[Index],LoadedPackages,ProcessedObjects,FALSE);
	}
#if 0 // Now done as a post process step
	// Now that everything was processed strip exported data
	StripChangedPackages(LoadedPackages,ProcessedObjects);
#endif
	unguard;
}

/** List of animation sets to export */
const TCHAR* AnimSets[] =
{
	TEXT("K_Male.Male_Common"),
	TEXT("K_Male.MA_Specific"),
	TEXT("K_Male.SO_Specific"),
	TEXT("K_Male.AP_Specific"),
	TEXT("K_Male.BR_Specific"),
	TEXT("K_Male.RD_Specific"),
	TEXT("K_Female.Female_Common"),
	TEXT("K_Female.LA_Specific"),
	TEXT("K_Female.NF_Specific"),
	TEXT("K_Female.SA_Specific"),
	TEXT("K_Female.DV_Specific"),
	TEXT("K_Skaarj.Skaarj_Common"),
	TEXT("K_Skaarj.GO_Specific"),
	TEXT("K_Skaarj.RA_Specific"),
	TEXT("K_Weapons.RWeapTerran"),
	TEXT("K_Weapons.RWeapTerran_Gorge"),
	TEXT("K_Weapons.RWeapTerran_Malcolm"),
	TEXT("K_Weapons.RWeapSkaarj"),
	TEXT("K_Weapons.RWeapNakhti"),
	TEXT("K_Weapons.RWeapLiandri"),
	TEXT("K_Weapons.HandsMalcolm_RW"),
	TEXT("K_Weapons.HandsGorge_RW"),
	TEXT("K_Weapons.HandsLauren_RW"),
	TEXT("K_Weapons.HandsNM_RW"),
	TEXT("K_Weapons.HandsLiandri_SR"),
	TEXT("K_Weapons.HandsSK_SR"),
	TEXT("K_Weapons.GrenadeLauncher"),
	TEXT("K_Weapons.ShockRifle"),
	TEXT("K_Weapons.SniperRifle"),
	TEXT("K_Weapons.HandsNM_SR"),
	TEXT("K_Weapons.Razorjack"),
	TEXT("K_Weapons.RocketLauncher"),
	TEXT("K_Weapons.FlakCannon"),
	TEXT("K_Weapons.Minigun"),
	TEXT("K_Weapons.HandsSK_XR"),
	TEXT("K_Weapons.HandsNM_XR"),
	TEXT("K_Weapons.HandsLiandri_XR"),
	TEXT("K_Weapons.HandsSK_RW"),
	TEXT("K_Weapons.HandsLiandri_RW"),
	TEXT("K_Nali_Slaughter.Nali_Slaughter"),
	NULL
};

/**
 * List of the animation packages that we need
 */
const TCHAR* AnimPackages[] = 
{
	TEXT("K_Male"),
	TEXT("K_Female"),
	TEXT("K_Skaarj"),
	TEXT("K_Weapons"),
	TEXT("K_Nali_Slaughter"),
	NULL
};

/**
 * Loads the animation sets used in the game, creates XPRs out of the animation
 * data, and resaves the packages to strip out the exported data.
 *
 * @param IgnoreList the list of previously exported animations to skip
 */
void ExportAnimSets(TArray<FName>& IgnoreList)
{
	guard(ExportAnimSets);
	// Holds the list of packages that need saving
	TArray<UPackage*> Packages;
	// Load the needed packages from the "cooked" directory
	for (INT Index = 0; AnimPackages[Index] != NULL; Index++)
	{
		// Load the package and all it's objects
		UPackage* Package = Cast<UPackage>(UObject::LoadPackage(NULL,
			*FString::Printf(TEXT("..\\XboxAnimations\\%s.ukx"),AnimPackages[Index]),0));
		Packages.AddItem(Package);
	}
	// Create our exporter
	UBulkDataExporterRDF* pExporter =
		ConstructObject<UBulkDataExporterRDF>(UBulkDataExporterRDF::StaticClass());
	// Find and export our needed objects
	for (INT Index = 0; AnimSets[Index] != NULL; Index++)
	{
		// Find the object. It was loaded during loadpackage
		UMeshAnimation* Anim = FindObject<UMeshAnimation>(ANY_PACKAGE,
			AnimSets[Index],TRUE);
		check(Anim);
		// Don't export something that was exported in the natives
		if (IgnoreList.FindItemIndex(Anim->GetFName()) == INDEX_NONE)
		{
			// Set the item to export
			pExporter->SetAnimSet(Anim);
			// Build the name to export
			FString OutName = FString::Printf(OUT_ANIMSET_RDF_PF,Anim->GetName());
			// Create the RDF file that will be processed by the bundler
			UExporter::ExportToFile(pExporter,pExporter,*OutName,0,0,1);
			// Verify that the file was created
			if (GFileManager->FileSize(*OutName) > 0)
			{
				// Execute the bundler tool
				CreateXPRFromRDF(*OutName);
			}
			else
			{
				PrintError(*FString::Printf(TEXT("Failed to create %s"),*OutName));
			}
		}
	}
	// Now save each package we modified
	for(INT Index = 0; Index < Packages.Num(); Index++)
	{
		// Build the output file name
		FString OutName = FString::Printf(TEXT("..\\XboxAnimations\\%s.ukx"),
			Packages(Index)->GetName());
		// Save the package
		UObject::SavePackage(Packages(Index),NULL,RF_Standalone,*OutName,GError);
	}
	unguard;
}

/**
 * Rips the data from the static mesh
 *
 * @param StaticMesh the static mesh to perform surgery on
 */
void StripMeshData(UStaticMesh* StaticMesh)
{
	guard(StripMeshData);
	// Make sure the array is valid
	if (StaticMesh->VertexStream.Vertices.Num() > 0)
	{
		// Tell the TUmaArray that the data will come from an XPR
		StaticMesh->VertexStream.Vertices.MarkAsInXPR();
	}
	// Make sure the array is valid
	if (StaticMesh->ColorStream.Colors.Num() > 0)
	{
		// Tell the TUmaArray that the data will come from an XPR
		StaticMesh->ColorStream.Colors.MarkAsInXPR();
	}
	// Make sure the array is valid
	if (StaticMesh->AlphaStream.Colors.Num() > 0)
	{
		// Tell the TUmaArray that the data will come from an XPR
		StaticMesh->AlphaStream.Colors.MarkAsInXPR();
	}
	// Add each of the UV streams
	for (INT Index = 0; Index < StaticMesh->UVStreams.Num(); Index++)
	{
		// Make sure the array is valid
		if (StaticMesh->UVStreams(Index).UVs.Num() > 0)
		{
			// Tell the TUmaArray that the data will come from an XPR
			StaticMesh->UVStreams(Index).UVs.MarkAsInXPR();
		}
	}
	// Rip out the indices too
	StaticMesh->IndexBuffer.Indices.Empty();
	unguard;
}

/**
 * Loads all of the static mesh packages and rips the data out of each mesh
 */
void StripStaticMeshData(void)
{
	// Get a list of the packages
	TArray<FString> List = GFileManager->FindFiles(
		TEXT("..\\XboxStaticMeshes\\*.usx"),1,0);
	// Loop through each package and save it to the new location
	for (INT Index = 0; Index < List.Num(); Index++)
	{
		// Build the in/out filenames
		const FString& PackageName = FString::Printf(OUT_STATIC_MESH_DIR_PF,List(Index));
		PrintProgress(*FString::Printf(TEXT("Loading %s"),*PackageName));
		// Load the package which will get the data in Xbox format
		UObject* Package = NULL;
		try
		{
			Package = UObject::LoadPackage(NULL,*PackageName,0);
			// Now iterate through all static meshes and rip out their data
			for (TObjectIterator<UStaticMesh> It; It; ++It)
			{
				StripMeshData(*It);
			}
		}
		catch(...)
		{
		}
		if (Package != NULL)
		{
			try
			{
				PrintProgress(*FString::Printf(TEXT("Saving %s"),*PackageName));
				// Save the data in Xbox format
				UObject::SavePackage(Package,NULL,RF_Standalone,*PackageName,GError);
			}
			catch(...)
			{
				PrintError(*FString::Printf(TEXT("Save failed for %s"),*PackageName));
			}
		}
		else
		{
			PrintError(*FString::Printf(TEXT("Failed to load %s"),*PackageName));
		}
		try
		{
			// Discard the loaded objects
			UObject::CollectGarbage(RF_Native);
		}
		catch(...)
		{
		}
	}
}

/**
 * Rips the data from the skeletal mesh
 *
 * @param SkeletalMesh the skeletal mesh to perform surgery on
 */
void StripMeshData(USkeletalMesh* SkeletalMesh)
{
	guard(StripMeshData);
	// Rip the data out of each of the skeletal mesh LOD vertex streams
	for (INT Index = 0; Index < SkeletalMesh->LODModels.Num(); Index++)
	{
		// Make sure the array is valid
		if (SkeletalMesh->LODModels(Index).GPUVertexStream.Vertices.Num() > 0)
		{
			// Tell the TUmaArray that the data will come from an XPR
			SkeletalMesh->LODModels(Index).GPUVertexStream.Vertices.MarkAsInXPR();
		}
	}
	unguard;
}

/**
 * Loads all of the static mesh packages and rips the data out of each mesh
 */
void StripSkeletalMeshData(void)
{
	// Get a list of the packages
	TArray<FString> List = GFileManager->FindFiles(
		TEXT("..\\XboxAnimations\\*.ukx"),1,0);
	// Loop through each package and save it to the new location
	for (INT Index = 0; Index < List.Num(); Index++)
	{
		// Build the in/out filenames
		const FString& PackageName = FString::Printf(OUT_SKELETAL_MESH_DIR_PF,
			List(Index));
		PrintProgress(*FString::Printf(TEXT("Loading %s"),*PackageName));
		// Load the package which will get the data in Xbox format
		UObject* Package = NULL;
		try
		{
			Package = UObject::LoadPackage(NULL,*PackageName,0);
			// Now iterate through all skeletal meshes and rip out their data
			for (TObjectIterator<USkeletalMesh> It; It; ++It)
			{
				StripMeshData(*It);
			}
		}
		catch(...)
		{
		}
		if (Package != NULL)
		{
			try
			{
				PrintProgress(*FString::Printf(TEXT("Saving %s"),*PackageName));
				// Save the data in Xbox format
				UObject::SavePackage(Package,NULL,RF_Standalone,*PackageName,GError);
			}
			catch(...)
			{
				PrintError(*FString::Printf(TEXT("Save failed for %s"),*PackageName));
			}
		}
		else
		{
			PrintError(*FString::Printf(TEXT("Failed to load %s"),*PackageName));
		}
		try
		{
			// Discard the loaded objects
			UObject::CollectGarbage(RF_Native);
		}
		catch(...)
		{
		}
	}
}

/**
 * @author: jg
 * 
 * Main function for converting each asset type. Calls each helper function in
 * turn.
 *
 * @param ConvertFlags The conversion options desired
 *
 */
void ConvertAssets(DWORD ConvertFlags)
{
	guard(ConvertAssets);
	if (ConvertFlags & ECT_VertexShaders)
	{
		PrintHeading(TEXT("Creating Vertex Shaders"));
		// Build a package for the vertex shaders so we don't load time
		// compile them
		CreateVertexShaders();
	}
	if (ConvertFlags & ECT_SkeletalMeshes)
	{
		PrintHeading(TEXT("Converting Skeletal Meshes"));
		// Convert all skeletal meshes
		ConvertPackages(IN_SKELETAL_MESH_DIR,OUT_SKELETAL_MESH_DIR,
			SKELETAL_MESH_EXT,IN_SKELETAL_MESH_DIR_PF,
			OUT_SKELETAL_MESH_DIR_PF);
	}
	if (ConvertFlags & ECT_StaticMeshes)
	{
		PrintHeading(TEXT("Converting Static Meshes"));
		// Convert all static mesh packages
		ConvertPackages(IN_STATIC_MESH_DIR,OUT_STATIC_MESH_DIR,
			STATIC_MESH_EXT,IN_STATIC_MESH_DIR_PF,
			OUT_STATIC_MESH_DIR_PF);
	}
	TArray<FName> IgnoreList;
	if (ConvertFlags & ECT_Maps || ConvertFlags & ECT_Characters)
	{
		// Make sure the destination folders are cleared and created
		CleanDirectory(OUT_MAPS_DIR);
		CleanDirectory(OUT_TEXTURES_DIR);
		CleanDirectory(OUT_TEXTURES_SRC_DIR);
		PrintHeading(TEXT("Bunlding Native Data"));
		// Handle natives separately from level textures. Use the ignore list to
		// keep from processing them repeatedly
		CreateNativesRDF(IgnoreList);
		if (ConvertFlags & ECT_Maps)
		{
			PrintHeading(TEXT("Converting Maps"));
			// Convert all maps
			ConvertMaps(IgnoreList);
		}
		if (ConvertFlags & ECT_Characters)
		{
			PrintHeading(TEXT("Building Character Data"));
			// Build the XPR files for each character
			BuildCharacterFiles(IgnoreList);
		}
		PrintHeading(TEXT("Stripping Static Meshes"));
		// Now strip out all mesh data from static meshes
		StripStaticMeshData();
		PrintHeading(TEXT("Stripping Skeletal Meshes"));
		// And do the same to skeletal meshes
		StripSkeletalMeshData();
	}
	if (ConvertFlags & ECT_AnimSets)
	{
		PrintHeading(TEXT("Exporting Animation Sets"));
		// Loads the animation sets used in the game and exports them to XPRs
		ExportAnimSets(IgnoreList);
		// Now strip animation sets from the K_*.ukx files
	}
	if (ConvertFlags & ECT_Textures)
	{
		PrintHeading(TEXT("Stripping Texture Data"));
		// Loads a package and strips out all texture data from the package
		StripAllTextureData();
	}
	// NOTE: This step must be done last or the sizes won't be correct
	if (ConvertFlags == ECT_FileSizes)
	{
		PrintHeading(TEXT("Generating File Size Class"));
		GenerateFileSizes();
	}
	if (ConvertFlags == ECT_TextureReport)
	{
		PrintHeading(TEXT("Creating Uncompressed Texture Report"));
		LogUncompressedTextures();
	}
	unguard;
}

/* ============================================================================
 * ::main
 * Author: jg
 * 
 * Entry point for the app. Sets up all static initialization.
 *
 * ============================================================================
 */
INT main(INT ArgC,char* ArgV[])
{
	// Change the process priority for this task to not hammer Mike's machine
	SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS);

	// Remember instance.
	INT ErrorLevel = 0;
	GIsStarted     = 1;

	//!!
	GMalloc = &Malloc;

	//jg -- Set the pointer to the synchronization factory
	GSynchronizeFactory = &SynchFactory;
	GThreadFactory = &ThreadFactory;

	// Init core.
	GIsClient = GIsGuarded = 1;

	extern TCHAR GCmdLine[];
	TCHAR* CopyAt = GCmdLine;
	// Fix up command line for the engine
	for (INT Index = 0; Index < ArgC; Index++)
	{
		TCHAR* Parm = ANSI_TO_TCHAR(ArgV[Index]);
		appStrcpy(CopyAt,Parm);
		CopyAt += appStrlen(Parm);
		appStrcpy(CopyAt,TEXT(" "));
		CopyAt++;
	}
	appInit( GPackage, GCmdLine, &Malloc, &Log, &Error, &Warn, &FileManager, FConfigCacheIni::Factory, 1 );

	FLOAT StartTime = appSeconds();

    // Static linking.
#if __STATIC_LINK
    for( INT k=0; k<ARRAY_COUNT(GNativeLookupFuncs); k++ )
		GNativeLookupFuncs[k] = NULL;
    INT Lookup = 0;
    // Core natives.
    GNativeLookupFuncs[Lookup++] = &FindCoreUObjectNative;
    GNativeLookupFuncs[Lookup++] = &FindCoreUCommandletNative;
	UExporter::StaticClass();
    // auto-generated lookups and statics
	AUTO_INITIALIZE_REGISTRANTS_ENGINE;
	
	// Hand do editor regestrants since the package is a mess
	UTextureExporterBMP::StaticClass();
	UTextureExporterDDS::StaticClass();
	UTextureExporterPCX::StaticClass();
	UTextureExporterTGA::StaticClass();
	UMaterialExporterRDF::StaticClass();
	UBulkDataExporterRDF::StaticClass();
	UPushbufferExporterRDF::StaticClass();

	AUTO_INITIALIZE_REGISTRANTS_D3DDRV;
	AUTO_INITIALIZE_REGISTRANTS_IPDRV;
	AUTO_INITIALIZE_REGISTRANTS_WINDRV;
	AUTO_INITIALIZE_REGISTRANTS_UNREALGAME;
	AUTO_INITIALIZE_REGISTRANTS_UCELEMENTS;
	AUTO_INITIALIZE_REGISTRANTS_UCGAMEPLAY;
	AUTO_INITIALIZE_REGISTRANTS_GUI;
    check( Lookup < ARRAY_COUNT(GNativeLookupFuncs) );
#endif

	// Init mode.
	GIsServer     = 1;
	GIsClient     = !ParseParam(appCmdLine(),TEXT("SERVER"));
	GIsEditor     = 1;
	GIsUCC = 1;
	GIsScriptable = 1;
	GLazyLoad = 0;

	// Engine initialization.
	FLOAT Dummy = appSeconds();
	// Init as if we are the editor
	UClass* EditorEngineClass	= UObject::StaticLoadClass( UEditorEngine::StaticClass(), NULL, TEXT("ini:Engine.Engine.EditorEngine"), NULL, LOAD_NoFail | LOAD_DisallowFiles, NULL );
	GEditor						= ConstructObject<UEditorEngine>( EditorEngineClass );
	GEditor->UseSound = 0;
	GEditor->InitEditor();

	Log.Logf(TEXT("GEditor->InitEditor(): %f"), (FLOAT) (appSeconds() - Dummy));

	// Start main engine loop.
	Log.Logf(TEXT("Starting Engine"));

	for( TObjectIterator<UClass> It; It; ++It )
		It->LoadConfig();
	
	GEngineStats.Init();

	Log.Logf( TEXT("Startup time %f"),(FLOAT) (appSeconds() - StartTime));

	// Print some memory stats.
	MEMORYSTATUS MemStatus;
	GlobalMemoryStatus( &MemStatus );
	Log.Logf(TEXT("MEMORY: %d KByte of physical memory in use"), (MemStatus.dwTotalPhys - MemStatus.dwAvailPhys) / 1024 );

	// Begin guarded code.
	guard(MainLoop);
	{
		DWORD ConvertFlags = appStrfind(appCmdLine(),TEXT("all")) ? ECT_ALL : 0;
		// Figure out which items we want converted
		ConvertFlags |= appStrfind(appCmdLine(),TEXT("vertexshaders")) ? ECT_VertexShaders : 0;
		ConvertFlags |= appStrfind(appCmdLine(),TEXT("maps")) ? ECT_Maps : 0;
		ConvertFlags |= appStrfind(appCmdLine(),TEXT("staticmeshes")) ? ECT_StaticMeshes : 0;
		ConvertFlags |= appStrfind(appCmdLine(),TEXT("skelmeshes")) ? ECT_SkeletalMeshes : 0;
		ConvertFlags |= appStrfind(appCmdLine(),TEXT("script")) ? ECT_Script : 0;
		ConvertFlags |= appStrfind(appCmdLine(),TEXT("filesizes")) ? ECT_FileSizes : 0;
		ConvertFlags |= appStrfind(appCmdLine(),TEXT("texreport")) ? ECT_TextureReport : 0;
		ConvertFlags |= appStrfind(appCmdLine(),TEXT("characters")) ? ECT_Characters : 0;
		ConvertFlags |= appStrfind(appCmdLine(),TEXT("textures")) ? ECT_Textures : 0;
		ConvertFlags |= appStrfind(appCmdLine(),TEXT("animations")) ? ECT_AnimSets : 0;
		// Now do the conversions
		ConvertAssets(ConvertFlags);
		appPreExit();
		GIsGuarded = 0;
	}
	unguard;

	// Final shut down.
	appExit();
	GIsStarted = 0;

	return ErrorLevel;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
