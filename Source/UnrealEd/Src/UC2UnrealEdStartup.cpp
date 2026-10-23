/*=============================================================================
	UC2UnrealEdStartup.cpp: what WinMain (Main.cpp) needs on the GCC build of UnrealEd.exe.

	- The static registration of every package linked into the program. Epic built the editor with
	  every package a DLL, where each IMPLEMENT_CLASS registers itself when its DLL loads; under
	  __STATIC_LINK a class exists once something calls its StaticClass(). The launchers make those
	  calls for the game's packages (SDLLaunch.cpp, UCC.cpp); this list adds the editor's. UnrealEd's
	  option proxies and texture aligners are declared in its headers, not in script, so they are
	  named one by one.
	- The editor's drivers in its own system ini (UC2Editor.ini).
=============================================================================*/

#include "UnrealEd.h"

#if __STATIC_LINK && !defined(_MSC_VER)

#include "ULoadSaveManager.h"
#include "UnNullRenderDevice.h"
#include "UnNet.h"
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
#include "WinDrv.h"
#include "UC2WGLDrv.h"
#if UC2_WITH_UC2ALAUDIO
#include "UC2ALAudio.h"
#endif
#if UC2_WITH_UC2MOVIE
#include "UC2Movie.h"
#endif

EDITOR_API void UC2InitializeEditorRegistrants( INT& Lookup );	// Editor/Src/UC2EditorRegistrants.cpp

void UC2InitializeUnrealEdRegistrants()
{
	guard(UC2InitializeUnrealEdRegistrants);

	for( INT k=0; k<ARRAY_COUNT(GNativeLookupFuncs); k++ )
		GNativeLookupFuncs[k] = NULL;
	INT Lookup = 0;
	GNativeLookupFuncs[Lookup++] = &FindCoreUObjectNative;
	GNativeLookupFuncs[Lookup++] = &FindCoreUCommandletNative;
	AUTO_INITIALIZE_REGISTRANTS_ENGINE;
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
	AUTO_INITIALIZE_REGISTRANTS_UC2GLDRV;
	AUTO_INITIALIZE_REGISTRANTS_UC2WGLDRV;
	UC2InitializeEditorRegistrants( Lookup );
	// (Window's one class, UWindowManager, is declared in Window.cpp and registers itself when InitWindowing makes it.)
	AUTO_INITIALIZE_REGISTRANTS_WINDRV;
	AUTO_INITIALIZE_REGISTRANTS_UNREALED;
	UOptionsProxy::StaticClass();
	UOptionsBrushScale::StaticClass();
	UOptions2DShaper::StaticClass();
	UOptions2DShaperSheet::StaticClass();
	UOptions2DShaperExtrude::StaticClass();
	UOptions2DShaperExtrudeToPoint::StaticClass();
	UOptions2DShaperExtrudeToBevel::StaticClass();
	UOptions2DShaperRevolve::StaticClass();
	UOptions2DShaperBezierDetail::StaticClass();
	UOptionsSurfBevel::StaticClass();
	UOptionsTexAlign::StaticClass();
	UOptionsTexAlignPlanar::StaticClass();
	UOptionsTexAlignCylinder::StaticClass();
	UOptionsNewTerrain::StaticClass();
	UOptionsNewTerrainLayer::StaticClass();
	UOptionsMapScale::StaticClass();
	UOptionsMatNewCameraPath::StaticClass();
	UOptionsMatNewStaticMesh::StaticClass();
	UOptionsDupTexture::StaticClass();
	UOptionsRotateActors::StaticClass();
	UOptionsNewClassFromSel::StaticClass();
	UTexAligner::StaticClass();
	UTexAlignerPlanar::StaticClass();
	UTexAlignerDefault::StaticClass();
	UTexAlignerBox::StaticClass();
	UTexAlignerFace::StaticClass();
	check( Lookup < ARRAY_COUNT(GNativeLookupFuncs) );
	RegisterNamesGUI();
	RegisterNamesUCElements();
	RegisterNamesUCGameplay();

	unguard;
}

#endif

#ifndef _MSC_VER

//
// The editor's drivers. UnrealEd.exe reads its own system ini, UC2Editor.ini, made from Default.ini on the first
// run like the game's Avaris.ini; Default.ini names the game's SDL client and its SDL-bound GL device, and the
// editor draws in WinDrv's Win32 windows with the WGL device. Written to UC2Editor.ini, so the game's ini is untouched.
//
void UC2SelectEditorDrivers()
{
	guard(UC2SelectEditorDrivers);

	const TCHAR* Client = TEXT("WinDrv.WindowsClient");
	FString IniClient = GConfig->GetStr( TEXT("Engine.Engine"), TEXT("ViewportManager") );
	if( appStricmp( *IniClient, Client ) != 0 )
	{
		debugf( NAME_Init, TEXT("ViewportManager=%s: the editor uses %s"), *IniClient, Client );
		GConfig->SetString( TEXT("Engine.Engine"), TEXT("ViewportManager"), Client );
	}

	// A device that draws in a WinDrv window: the WGL device, or the null device.
	const TCHAR* Device = TEXT("UC2WGLDrv.UC2WGLRenderDevice");
	FString IniDevice = GConfig->GetStr( TEXT("Engine.Engine"), TEXT("RenderDevice") );
	if( appStricmp( *IniDevice, Device ) != 0 && appStricmp( *IniDevice, TEXT("Engine.NullRenderDevice") ) != 0 )
	{
		debugf( NAME_Init, TEXT("RenderDevice=%s: the editor uses %s"), *IniDevice, Device );
		GConfig->SetString( TEXT("Engine.Engine"), TEXT("RenderDevice"), Device );
	}

	unguard;
}

#endif

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
