/*=============================================================================
	UnCamMgr.cpp: Unreal viewport manager, generic implementation.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "EnginePrivate.h"
#include "UnRender.h"
//scion bcl 
#include "ULoadSaveManager.h"

/*-----------------------------------------------------------------------------
	UClient implementation.
-----------------------------------------------------------------------------*/

//
// Constructor.
//
UClient::UClient()
{
	guard(UClient::UClient);

	// Hook in.
	UTexture::__Client = this;

	SplitScreenMgr = NULL;
	ViewportMgr = NULL;

	unguard;
}
void UClient::PostEditChange()
{
	guard(UClient::PostEditChange);
	Super::PostEditChange();
	Brightness = Clamp(Brightness,0.f,1.f);
    
	Contrast = Clamp( Contrast, 0.f, 2.f );
	Gamma = Clamp( Gamma, 0.5f, 2.5f );
	Engine->UpdateGamma();

	for(TObjectIterator<UTexture> It;It;++It)
        It->bRealtimeChanged = 1;

	SaveConfig();

	unguard;
}
void UClient::StaticConstructor()
{
	guard(UClient::StaticConstructor);

	PTextureDetail = new(GetClass(),TEXT("ETextureDetail"))UEnum( NULL );

	new(PTextureDetail->Names)FName( TEXT("UltraHigh")); // +4
	new(PTextureDetail->Names)FName( TEXT("VeryHigh") ); // +3
	new(PTextureDetail->Names)FName( TEXT("High")     ); // +2
	new(PTextureDetail->Names)FName( TEXT("Higher")   ); // +1
	new(PTextureDetail->Names)FName( TEXT("Normal")   ); // No-change
	new(PTextureDetail->Names)FName( TEXT("Lower")    ); // -1
	new(PTextureDetail->Names)FName( TEXT("Low")      ); // -2 
	new(PTextureDetail->Names)FName( TEXT("VeryLow")  ); // -3 
	new(PTextureDetail->Names)FName( TEXT("UltraLow") ); // -4

	new(GetClass(),TEXT("WindowedViewportX"),	RF_Public)UIntProperty  (CPP_PROPERTY(WindowedViewportX     ), TEXT("Client"),  CPF_Config );
	new(GetClass(),TEXT("WindowedViewportY"),	RF_Public)UIntProperty  (CPP_PROPERTY(WindowedViewportY     ), TEXT("Client"),  CPF_Config );
	new(GetClass(),TEXT("FullscreenViewportX"),	RF_Public)UIntProperty  (CPP_PROPERTY(FullscreenViewportX   ), TEXT("Client"),  CPF_Config );
	new(GetClass(),TEXT("FullscreenViewportY"),	RF_Public)UIntProperty  (CPP_PROPERTY(FullscreenViewportY	), TEXT("Client"),  CPF_Config );
	new(GetClass(),TEXT("Brightness"),			RF_Public)UFloatProperty(CPP_PROPERTY(Brightness			), TEXT("Display"), CPF_Config );
	new(GetClass(),TEXT("Contrast"),			RF_Public)UFloatProperty(CPP_PROPERTY(Contrast				), TEXT("Display"), CPF_Config );
	new(GetClass(),TEXT("Gamma"),				RF_Public)UFloatProperty(CPP_PROPERTY(Gamma					), TEXT("Display"), CPF_Config );
	new(GetClass(),TEXT("DynamicGamma"),		RF_Public)UBoolProperty	(CPP_PROPERTY(DynamicGamma			), TEXT("Display"), CPF_Config );
	new(GetClass(),TEXT("CaptureMouse"),		RF_Public)UBoolProperty (CPP_PROPERTY(CaptureMouse			), TEXT("Display"), CPF_Config );

    new(GetClass(),TEXT("TextureDetailWorld"),              RF_Public)UByteProperty (CPP_PROPERTY(TextureLODSet[LODSET_World]           ), TEXT("Display"), CPF_Config, PTextureDetail );
    new(GetClass(),TEXT("TextureDetailPlayerSkin"),         RF_Public)UByteProperty (CPP_PROPERTY(TextureLODSet[LODSET_PlayerSkin]      ), TEXT("Display"), CPF_Config, PTextureDetail );
    new(GetClass(),TEXT("TextureDetailWeaponSkin"),         RF_Public)UByteProperty (CPP_PROPERTY(TextureLODSet[LODSET_WeaponSkin]      ), TEXT("Display"), CPF_Config, PTextureDetail );
    new(GetClass(),TEXT("TextureDetailTerrain"),            RF_Public)UByteProperty (CPP_PROPERTY(TextureLODSet[LODSET_Terrain]         ), TEXT("Display"), CPF_Config, PTextureDetail );
    new(GetClass(),TEXT("TextureDetailInterface"),          RF_Public)UByteProperty (CPP_PROPERTY(TextureLODSet[LODSET_Interface]       ), TEXT("Display"), CPF_Config, PTextureDetail );
    new(GetClass(),TEXT("TextureDetailRenderMap"),          RF_Public)UByteProperty (CPP_PROPERTY(TextureLODSet[LODSET_RenderMap]       ), TEXT("Display"), CPF_Config, PTextureDetail );
    new(GetClass(),TEXT("TextureDetailLightmap"),           RF_Public)UByteProperty (CPP_PROPERTY(TextureLODSet[LODSET_Lightmap]        ), TEXT("Display"), CPF_Config, PTextureDetail );

	new(GetClass(),TEXT("ScreenFlashes"),		RF_Public)UBoolProperty (CPP_PROPERTY(ScreenFlashes			), TEXT("Display"), CPF_Config );
	new(GetClass(),TEXT("NoLighting"),			RF_Public)UBoolProperty (CPP_PROPERTY(NoLighting			), TEXT("Display"), CPF_Config );
	new(GetClass(),TEXT("MinDesiredFrameRate"),	RF_Public)UFloatProperty(CPP_PROPERTY(MinDesiredFrameRate	), TEXT("Display"), CPF_Config );
	new(GetClass(),TEXT("Decals"),				RF_Public)UBoolProperty (CPP_PROPERTY(Decals				), TEXT("Display"), CPF_Config );
	new(GetClass(),TEXT("NoDynamicLights"),		RF_Public)UBoolProperty (CPP_PROPERTY(NoDynamicLights		), TEXT("Display"), CPF_Config );
	new(GetClass(),TEXT("NoFractalAnim"),		RF_Public)UBoolProperty (CPP_PROPERTY(NoFractalAnim 		), TEXT("Display"), CPF_Config );
	new(GetClass(),TEXT("Coronas"),				RF_Public)UBoolProperty (CPP_PROPERTY(Coronas		 		), TEXT("Display"), CPF_Config );
	new(GetClass(),TEXT("DecoLayers"),			RF_Public)UBoolProperty (CPP_PROPERTY(DecoLayers	 		), TEXT("Display"), CPF_Config );	
	new(GetClass(),TEXT("Projectors"),			RF_Public)UBoolProperty (CPP_PROPERTY(Projectors	 		), TEXT("Display"), CPF_Config );	
	new(GetClass(),TEXT("ReportDynamicUploads"),RF_Public)UBoolProperty (CPP_PROPERTY(ReportDynamicUploads	), TEXT("Display"), CPF_Config );

	// scion sz - max distortion
	new(GetClass(),TEXT("MaxDistortion"),			RF_Public)UFloatProperty(CPP_PROPERTY(MaxDistortion		), TEXT("Display"), CPF_Config );

	// D-2 (UI scaling, docs/porting/analysis/ui-scale.md): where the 640x480 UI goes on a bigger window.
	UEnum* PUIScaleMode = new(GetClass(),TEXT("EUIScaleMode"))UEnum( NULL );
	new(PUIScaleMode->Names)FName( TEXT("Native640") );		// UISCALE_Native640
	new(PUIScaleMode->Names)FName( TEXT("Fit43") );			// UISCALE_Fit43
	UEnum* PUIMenuBackground = new(GetClass(),TEXT("EUIMenuBackground"))UEnum( NULL );
	new(PUIMenuBackground->Names)FName( TEXT("Box") );		// UIMENUBG_Box
	new(PUIMenuBackground->Names)FName( TEXT("Fill") );		// UIMENUBG_Fill
	new(GetClass(),TEXT("UIScaleMode"),			RF_Public)UByteProperty (CPP_PROPERTY(UIScaleMode			), TEXT("Display"), CPF_Config, PUIScaleMode );
	new(GetClass(),TEXT("UIMenuBackground"),	RF_Public)UByteProperty (CPP_PROPERTY(UIMenuBackground		), TEXT("Display"), CPF_Config, PUIMenuBackground );
	new(GetClass(),TEXT("UISharpText"),			RF_Public)UBoolProperty (CPP_PROPERTY(UISharpText			), TEXT("Display"), CPF_Config );

	unguard;
}
void UClient::Destroy()
{
	guard(UClient::Destroy);
	UTexture::__Client = NULL;
	
	//scion bcl -- LoadSaveManager stuff
	delete LoadSaveManager;
	LoadSaveManager = NULL;

	// scion sz -
	if( SplitScreenMgr )
	{
		delete SplitScreenMgr;
		SplitScreenMgr = NULL;
	}

	if( ViewportMgr )
	{
		delete ViewportMgr;
		ViewportMgr = NULL;
	}

	Super::Destroy();
	unguard;
}

//
// Command line.
//
UBOOL UClient::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UClient::Exec);
	// D-2: UISCALE [Native640|Fit43] [Box|Fill] [Sharp|Soft] switches the UI mapping for this session (the ini
	// keeps its UIScaleMode/UIMenuBackground/UISharpText); without arguments it says what is in use.
	if( ParseCommand(&Cmd,TEXT("UISCALE")) )
	{
		TCHAR Word[64];
		while( ParseToken( Cmd, Word, ARRAY_COUNT(Word), 0 ) )
		{
			if( appStricmp(Word,TEXT("Native640"))==0 )	UIScaleMode = UISCALE_Native640;
			else if( appStricmp(Word,TEXT("Fit43"))==0 )	UIScaleMode = UISCALE_Fit43;
			else if( appStricmp(Word,TEXT("Box"))==0 )		UIMenuBackground = UIMENUBG_Box;
			else if( appStricmp(Word,TEXT("Fill"))==0 )		UIMenuBackground = UIMENUBG_Fill;
			else if( appStricmp(Word,TEXT("Sharp"))==0 )	UISharpText = 1;
			else if( appStricmp(Word,TEXT("Soft"))==0 )		UISharpText = 0;
			else Ar.Logf( TEXT("UISCALE: unknown word '%s' (Native640, Fit43, Box, Fill, Sharp, Soft)"), Word );
		}
		Ar.Logf( TEXT("UISCALE: UIScaleMode=%s UIMenuBackground=%s UISharpText=%s"),
			UIScaleMode==UISCALE_Fit43 ? TEXT("Fit43") : TEXT("Native640"),
			UIMenuBackground==UIMENUBG_Fill ? TEXT("Fill") : TEXT("Box"),
			UISharpText ? TEXT("True") : TEXT("False") );
		return 1;
	}
	else
	if( ParseCommand(&Cmd,TEXT("BRIGHTNESS")) )
	{
		if( *Cmd == '+' )
			Brightness = Brightness >= 0.9f ? 0.f : Brightness + 0.05f;
		else
		if( *Cmd )
			Brightness = Clamp<FLOAT>( appAtof(Cmd), 0.f, 1.f );
		else
			Brightness = 0.5f;
		
		if( !DynamicGamma )
		{
			Engine->UpdateGamma();
			SaveConfig();
		}
		else
		{
			if( Viewports.Num() && Viewports(0)->Actor )
			{
				Viewports(0)->Actor->Level->LevelBrightness = Brightness;
			}
		}

		if( Viewports.Num() && Viewports(0)->Actor )//!!ugly
			Viewports(0)->Actor->eventClientMessage( *FString::Printf(TEXT("Brightness %i"), (INT)(Brightness*10)), NAME_None );
		return 1;
	}
	else
	if( ParseCommand(&Cmd,TEXT("CONTRAST")) )
	{
		if( *Cmd == '+' )
			Contrast = Contrast >= 0.9f ? 0.f : Contrast + 0.05f;
		else
		if( *Cmd )
			Contrast = Clamp<FLOAT>( appAtof(Cmd), 0.f, 2.f );
		else
			Contrast = 0.5f;
		
		if( !DynamicGamma )
		{
			Engine->UpdateGamma();
			SaveConfig();
		}
		else
		{
			if( Viewports.Num() && Viewports(0)->Actor )
			{
				Viewports(0)->Actor->Level->LevelContrast = Contrast;
			}
		}

		if( Viewports.Num() && Viewports(0)->Actor )//!!ugly
			Viewports(0)->Actor->eventClientMessage( *FString::Printf(TEXT("Contrast %i"), (INT)(Contrast*10)), NAME_None );
		return 1;
	}
	else
	if( ParseCommand(&Cmd,TEXT("GAMMA")) )
	{
		if( *Cmd == '+' )
			Gamma = Gamma >= 2.4f ? 0.5f : Gamma + 0.05f;
		else
		if( *Cmd )
			Gamma = Clamp<FLOAT>( appAtof(Cmd), 0.5f, 2.5f );
		else
			Gamma = 1.7f;
		
#ifdef _XBOX
		if( !DynamicGamma )
#endif
		{
			Engine->UpdateGamma();
			SaveConfig();
		}
#ifdef _XBOX
		else
		{
			if( Viewports.Num() && Viewports(0)->Actor )
			{
				Viewports(0)->Actor->Level->LevelGamma = Gamma;
			}
		}
#endif
		
		if( Viewports.Num() && Viewports(0)->Actor )//!!ugly
			Viewports(0)->Actor->eventClientMessage( *FString::Printf(TEXT("Gamma %1.1f"), Gamma), NAME_None );
		return 1;
	}
	// scion sz -
	else if( ParseCommand(&Cmd,TEXT("DYNAMICGAMMA")) )
	{
		if( ParseCommand(&Cmd,TEXT("0")) )
			DynamicGamma = 0;
		else if( ParseCommand(&Cmd,TEXT("1")) )
			DynamicGamma = 1;
		else
			DynamicGamma = !DynamicGamma;

		if( !DynamicGamma )
			Engine->UpdateGamma();

		return 1;
	}
	else if( ParseCommand(&Cmd,TEXT("LUMINOSITYBIAS")) )
	{
		FLOAT Bias = appAtof( *ParseToken(Cmd,0) );
		LuminosityBias = Bias;
		return 1;
	}
	// scion sz - testing only
	else if( ParseCommand(&Cmd,TEXT("SPLIT")) )
	{
		INT GamepadID=Viewports(0)->GamepadID;
		if( ParseCommand(&Cmd,TEXT("ADD")) )
		{
			Parse(Cmd, TEXT("ID="), GamepadID);
#ifndef _XBOX
			if( SplitScreenViewports() )	// uc2 port (G-6): the Xbox branch, on a client that can draw it
			{
				debugf( NAME_DevSplitScreen, TEXT("TEST: Adding splitscreen  GamepadID:%i"), GamepadID );
				if( BeginSplitScreen( this, GamepadID ) )
					GetSplitScreenMgr()->RefreshPlayerHUDs();
			}
			else
#endif
#ifdef _XBOX
			debugf( NAME_DevSplitScreen, TEXT("TEST: Adding splitscreen  GamepadID:%i"), GamepadID );
			BeginSplitScreen( this, GamepadID );
#else
			{
			UViewport* Viewport = Viewports(0);
			INT NewSizeY = Max<INT>( Viewport->SizeY * 0.5, 240 );
			Cast<UViewport>(Viewport->GetClass()->GetDefaultObject())->SizeY = Viewport->SizeY;
			debugf(NAME_DevSplitScreen, TEXT("Setting viewport height to %i"), NewSizeY);
			Viewport->SizeY = NewSizeY;
			}
#endif
		}

		if( ParseCommand(&Cmd,TEXT("REMOVE")) )
		{
#ifndef _XBOX
			if( SplitScreenViewports() )	// uc2 port (G-6): the Xbox branch below
			{
				USplitScreenMgr* SplitScreenMgr = GetSplitScreenMgr();
				check(SplitScreenMgr);
				// X-1: SPLIT REMOVE ID=<pad> removes the split player of that pad (a player three or four leaving
				// the character page, patch set players34; never player one's view); without ID the last one, as Epic's.
				INT RemoveID = -1;
				if( Parse( Cmd, TEXT("ID="), RemoveID ) )
				{
					UViewport* RemoveView = NULL;
					for( INT i=1; i<SplitScreenMgr->NumActiveSplitViews() && !RemoveView; i++ )
						if( SplitScreenMgr->GetSplitView(i) && SplitScreenMgr->GetSplitView(i)->GamepadID == RemoveID )
							RemoveView = SplitScreenMgr->GetSplitView(i);
					debugf( NAME_DevSplitScreen, TEXT("Split screen: removing the player of pad %i: %s"), RemoveID, RemoveView ? RemoveView->GetName() : TEXT("no such split view") );
					// removed as Epic's REMOVE removes the last one (no ClearQueriesForViewport: on a split view of
					// the menus, never drawn, it found another viewport's queries and crashed, measured in X-1)
					if( RemoveView )
					{
						SplitScreenMgr->RemoveSplitViewport( RemoveView );
						SplitScreenMgr->RefreshPlayerHUDs();
					}
				}
				else
				{
				debugf( NAME_DevSplitScreen, TEXT("TEST: Removing splitscreen") );
				if( SplitScreenMgr->NumActiveSplitViews() )
				{
					SplitScreenMgr->RemoveSplitViewport( SplitScreenMgr->GetSplitView( SplitScreenMgr->NumActiveSplitViews()-1 ) );
					SplitScreenMgr->RefreshPlayerHUDs();
				}
				}
			}
			else
#endif
#ifdef _XBOX
			debugf( NAME_DevSplitScreen, TEXT("TEST: Removing splitscreen") );
			USplitScreenMgr* SplitScreenMgr = GetSplitScreenMgr();
			check(SplitScreenMgr);

			if( SplitScreenMgr->NumActiveSplitViews() )
			{
				// remove last one
				UViewport* RemoveView = SplitScreenMgr->GetSplitView( SplitScreenMgr->NumActiveSplitViews()-1 );
				SplitScreenMgr->RemoveSplitViewport( RemoveView );
			}
#else
			{
			UViewport* Viewport = Viewports(0);
			INT NewSizeY = Cast<UViewport>(Viewport->GetClass()->GetDefaultObject())->SizeY;
			debugf(NAME_DevSplitScreen, TEXT("Setting viewport height to %i"), NewSizeY);

			Viewport->SizeY = NewSizeY;
			}
#endif
		}

		return 1;
	}
	// scion sz
	// create a viewport
	// don't use this.. testing only
	else if( ParseCommand(&Cmd, TEXT("NEWVP")) ) 
	{
		INT X = appAtoi( *ParseToken(Cmd,0) );
		INT Y = appAtoi( *ParseToken(Cmd,0) );
		INT XL = appAtoi( *ParseToken(Cmd,0) );
		INT YL = appAtoi( *ParseToken(Cmd,0) );

		UGameEngine* GameEngine = Cast<UGameEngine>(Engine); check(GameEngine);
		ViewportMgr->AddViewport( Viewports(0)->Actor, X, Y, XL, YL, 0 );
		ZSortViewports();

		return 1;
	}
	//scion jg -- Allow forwarding of execs to child viewports for handling
	else if (ParseCommand(&Cmd,TEXT("VIEWPORTS")))
	{
		// Iterate through the viewports and pass them the remaining exec
		for (INT Viewport = 0; Viewport < Viewports.Num(); Viewport++)
		{
			// If the viewport is valid, forward the exec to it
			if (Viewports(Viewport) != NULL)
			{
				Viewports(Viewport)->Exec(Cmd,Ar);
			}
		}
	}
	
	//scion bcl -- LoadSaveManager supports an Exec: give it a shot
	return LoadSaveManager && LoadSaveManager->Exec(Cmd, Ar);

	unguard;
}

//
// Init.
//
void UClient::Init( UEngine* InEngine )
{
	guard(UClient::Init);
	Engine = InEngine;

	UClass* LoadSaveManagerClass = StaticLoadClass( ULoadSaveManager::StaticClass(), NULL, TEXT("ini:Engine.Engine.LoadSaveManagerClass"), NULL, LOAD_NoFail, NULL );
	if (LoadSaveManagerClass) {
		LoadSaveManager = ConstructObject<ULoadSaveManager>(LoadSaveManagerClass, (UObject*)-1, NAME_None, RF_Keep); 
		if (LoadSaveManager)
			LoadSaveManager->Init(this);
	}

	// scion sz - create splitscreen manager
	if( !SplitScreenMgr )
	{
		SplitScreenMgr = ConstructObject<USplitScreenMgr>( USplitScreenMgr::StaticClass(), (UObject*)-1, NAME_None, RF_Keep );
		SplitScreenMgr->Init( this );
	}

	// scion sz - create viewport manager
	if( !ViewportMgr )
	{
		ViewportMgr = ConstructObject<UViewportMgr>( UViewportMgr::StaticClass(), (UObject*)-1, NAME_None, RF_Keep );
	}

#ifdef _XBOX
	//scion jg -- Turn dynamic gamma on by default
	DynamicGamma = 1;
	LuminosityBias = 1.0f;
#endif
	unguard;
}

//
// Flush.
//
void UClient::Flush( UBOOL AllowPrecache )
{
	guard(UClient::Flush);

	for( INT i=0; i<Viewports.Num(); i++ )
		if( Viewports(i)->RenDev )
			Viewports(i)->RenDev->Flush( Viewports(i));

	unguard;
}

//
// Update Gamma.
//
void UClient::UpdateGamma()
{
	guard(UClient::UpdateGamma);

	for( INT i=0; i<Viewports.Num(); i++ )
		if( Viewports(i)->RenDev )
			Viewports(i)->RenDev->UpdateGamma( Viewports(i) );
	
	unguard;
}

//
// Restore Gamma.
//
void UClient::RestoreGamma()
{
	guard(UClient::RestoreGamma);

	for( INT i=0; i<Viewports.Num(); i++ )
		if( Viewports(i)->RenDev )
			Viewports(i)->RenDev->RestoreGamma();
	
	unguard;
}

//
// Serializer.
//
void UClient::Serialize( FArchive& Ar )
{
	guard(UClient::Serialize);
	Super::Serialize( Ar );

	// Only serialize objects, since this can't be loaded or saved.
	Ar << Viewports;

	unguard;
}

INT UClient::GetTextureLODBias (ELODSet LODSet)
{
	guard(UClient::GetTextureLODBias);
    check (LODSet < LODSET_MAX);
    check (PTextureDetail);

    return (TextureLODSet[LODSet] - (PTextureDetail->Names.Num() / 2));

	unguard;
}

// Returns whether pressing "start" to activate a gamepad is allowed by the GUI
UBOOL UClient::AllowControllerJoin() const
{
	guard(UClient::AllowControllerJoin);

	UBOOL bResult=0;
	if ( Viewports.Num() )
	{
		UBaseGUIController* GUIController = Viewports(0)->GUIController;
		bResult = GUIController && GUIController->eventAllowControllerJoin();
	}

	return bResult;


	unguard;
}

/* scion ======================================================================
* ::CreateSplitScreenPlayer
* Author: sz
*
* Called when an unbound gamepad presses <start>.
* Creates a new player controller in the level and a viewport for it.
* Only works if the menus are active and the current page allows it.
*
*
* rjp - moved from XBoxClient.cpp and made into a static member function, for
*		debugging purposes on PC.
* ============================================================================
*/
UBOOL UClient::BeginSplitScreen( UClient* Client, INT GamepadID )
{
	guard(UClient::BeginSplitScreen);

	check(Client);	

	UBOOL bResult = Client->AllowControllerJoin();
	if ( bResult )
	{
		// get a pointer to the split screen manager
		USplitScreenMgr* SplitScreenMgr = Client->GetSplitScreenMgr();
		check(SplitScreenMgr);

		// spawn a default player bound to a new viewport
		bResult = SplitScreenMgr->CreateSplitPlayer( NULL, GamepadID ) != NULL;
	}

	return bResult;
	unguard;
}

/* scion ======================================================================
* ::ZSortViewports
* Author: sz
*
* Sorts client's viewports based on z offset. Smaller z offsets should
* be first on the list so that they draw first.
* ============================================================================
*/

FORCEINLINE INT Compare( UViewport*& A, UViewport*& B)
{
	// B>A returns >0
	// B==A returns 0
	// B<A returns <0
	return B->OffsetZ - A->OffsetZ;
}

void UClient::ZSortViewports()
{
	INT NumViewports = Viewports.Num();
	if( NumViewports > 2 )
	{
		// sort all viewports (except Viewports(0)) based on Z offset value
		Sort( &Viewports(1), NumViewports-1 );
	}
}


IMPLEMENT_CLASS(UClient);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

