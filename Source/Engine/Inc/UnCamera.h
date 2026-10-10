/*=============================================================================
	UnViewport.h: Unreal viewport object.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

/*-----------------------------------------------------------------------------
	UCanvas.
-----------------------------------------------------------------------------*/

// Constants.
enum {MAX_COLOR_BYTES=9}; // Maximum expected frame buffer depth = 8 bytes = 64-bit.

//
// Blitting types.
//
enum EViewportBlitFlags
{
	// Bitflags.
	BLIT_Fullscreen     = 0x0001, // Fullscreen.
	BLIT_Temporary      = 0x0002, // Temporary viewport.
	BLIT_Direct3D       = 0x0010, // Create Direct3D along with DirectDraw.
	BLIT_NoWindowChange = 0x0020, // Don't resize existing window frame.
	BLIT_NoWindowFrame  = 0x0040, // Turn off the window frame.
	BLIT_OpenGL			= 0x0080, // OpenGL rendering.
	BLIT_HardwarePaint  = 0x0100, // Window should be repainted in hardware when drawn.

	// Special.
	BLIT_ParameterFlags	= BLIT_NoWindowChange, // Only parameters to ResizeViewport, not permanent flags.
};

/*-----------------------------------------------------------------------------
	FHitCause.
-----------------------------------------------------------------------------*/

//
// Information about a hit-test cause.
//
struct ENGINE_API FHitCause
{
	class FHitObserver* Observer;
	UViewport* Viewport;
	DWORD Buttons;
	FLOAT MouseX;
	FLOAT MouseY;
	FHitCause( FHitObserver* InObserver, UViewport* InViewport, DWORD InButtons, FLOAT InMouseX, FLOAT InMouseY )
	: Observer( InObserver ), Viewport( InViewport ), Buttons( InButtons ), MouseX( InMouseX ), MouseY( InMouseY )
	{}
};

/*-----------------------------------------------------------------------------
	FHitObserver.
-----------------------------------------------------------------------------*/

//
// Hit observer class for receiving hit-test notification.
//
class ENGINE_API FHitObserver
{
public:
	virtual void Click( const struct FHitCause& Cause, const struct HHitProxy& Hit ) {}
};

/*-----------------------------------------------------------------------------
	UViewport.
-----------------------------------------------------------------------------*/

// Information for rendering the viewport (detail level settings).
enum ERenderType
{
	REN_None				= 0,	// Hide completely.
	REN_Wire				= 1,	// Wireframe of EdPolys.
	REN_Zones				= 2,	// Show zones and zone portals.
	REN_Polys				= 3,	// Flat-shaded Bsp.
	REN_PolyCuts			= 4,	// Flat-shaded Bsp with normals displayed.
	REN_DynLight			= 5,	// Illuminated texture mapping.
	REN_PlainTex			= 6,	// Plain texture mapping.
	REN_LightingOnly		= 7,	// Untextured, lit geometry
	REN_DepthComplexity		= 8,	// Show per-pixel depth complexity.
	REN_OrthXY				= 13,	// Orthogonal overhead (XY) view.
	REN_OrthXZ				= 14,	// Orthogonal XZ view.
	REN_OrthYZ				= 15,	// Orthogonal YZ view.
	REN_TexView				= 16,	// Viewing a texture (no actor).
	REN_TexBrowser			= 17,	// Viewing a texture browser (no actor).
	REN_StaticMeshBrowser	= 18,	// Viewing a static mesh browser (no actor).
	REN_MeshView			= 19,	// Viewing a mesh.
	REN_Prefab				= 20,	// Viewing a prefab.
	REN_PrefabCompiled		= 21,	// Viewing a prefab (after the prefab level has been compiled).
	REN_TerrainHeightmap	= 22,	// Terrain heightmap info
	REN_TerrainLayers		= 23,	// Terrain layers
	REN_TerrainDecoLayers	= 24,	// Terrain decoration Layers
	REN_Animation			= 26,	// Animations
	REN_MatineeScenes		= 27,
	REN_MatineeActions		= 28,
	REN_MatineeSubActions	= 29,
	REN_MatineePreview		= 30,
	REN_MaterialEditor		= 31,	// Material editor
	REN_TexBrowserUsed		= 32,	// Browses texture which are in use in the current level
	REN_TexBrowserMRU		= 33,	// Browses the most recently used textures

#ifdef WITH_LIPSINC
	REN_LIPSinc				= 34,	// LIPSinc
	REN_MAX					= 35,
#else
	REN_MAX					= 34,
#endif

};

enum EMaterialViewType
{
	MVT_TEXTURE,		// Regular, flat texture view - fill the window
	MVT_CUBE,			// Map texture on a cube
	MVT_SPHERE,			// Map texture on a sphere
};

// Viewport capabilities.
enum EViewportCaps
{
	CC_RGB565			= 1,	// RGB 565 format (otherwise 555).
	CC_Mask				= 0xff, // Caps mask which affects cached rendering info.
};

// ShowFlags for viewport.
enum EViewportShowFlags
{
	SHOW_Frame				= 0x00000001, 	// Show world bounding cube.
	SHOW_ActorRadii			= 0x00000002, 	// Show actor collision radii.
	SHOW_Backdrop			= 0x00000004, 	// Show background scene.
	SHOW_Actors				= 0x00000008,	// Show actors.
	SHOW_Coords				= 0x00000010,	// Show brush/actor coords.
	SHOW_ActorIcons			= 0x00000020,	// Show actors as icons.
	SHOW_Brush				= 0x00000040,	// Show the active brush.
	SHOW_StandardView		= 0x00000080,	// Viewport is a standard view.
	SHOW_EdViewport			= 0x00000100,	// This is a special editor viewport and shouldn't have it's level reset by level loads
	SHOW_ChildWindow		= 0x00000200,	// Show as true child window.
	SHOW_MovingBrushes		= 0x00000400,	// Show moving brushes.
	SHOW_PlayerCtrl			= 0x00000800,	// Player controls are on.
	SHOW_Paths				= 0x00001000,   // Show paths.
	SHOW_KarmaMassProps     = 0x00002000,   // Show Karma mass properties (centre-of-mass, inertia tensor)
	SHOW_RealTime			= 0x00004000,	// Update window in realtime.
	SHOW_StaticMeshes		= 0x00008000,	// Show static meshes.
	SHOW_EventLines			= 0x00010000,   // Show event lines (from triggers, etc)
	SHOW_SelectionHighlight	= 0x00020000,   // Show green highlights on selections?
	SHOW_Terrain			= 0x00040000,	// Show terrain.
	SHOW_DistanceFog		= 0x00080000,	// Show distance fog.
	SHOW_MatRotations		= 0x00100000,	// Show rotation indicators (matinee preview windows)
	SHOW_MatPaths			= 0x00200000,	// Show the path itself (matinee preview windows)
	SHOW_Coronas			= 0x00400000,	// Show coronas.
	SHOW_Volumes			= 0x00800000,	// Show volume brushes.
	SHOW_Particles			= 0x01000000,	// Show pahticuls.
	SHOW_BSP				= 0x02000000,	// Show BSP surfaces.
	SHOW_ActorInfo			= 0x04000000,	// Shows extended information about actors (class name, event, tag, etc)
	SHOW_KarmaPrimitives    = 0x08000000,   // Show Karma collision primitives.
	SHOW_FluidSurfaces		= 0x10000000,	// Show simulated fluid surfaces.
	SHOW_Projectors			= 0x20000000,	// Show projectors.
	SHOW_NoFallbackMaterials= 0x40000000,	// Don't evaluate fallback materials.
	SHOW_Collision          = 0x80000000,	// Show simple collision for static meshes etc.
	// scion sz - default view settings for the game
	SHOW_GameDefault		= SHOW_Backdrop | SHOW_Actors | SHOW_StaticMeshes | SHOW_Terrain | SHOW_DistanceFog | SHOW_PlayerCtrl | SHOW_RealTime | SHOW_Coronas | SHOW_Particles | SHOW_BSP | SHOW_FluidSurfaces | SHOW_Projectors
};

// Mouse buttons and commands.
enum EMouseButtons	
{
	MOUSE_Left				= 0x00000001,	// Left mouse button.
	MOUSE_Right				= 0x00000002,	// Right mouse button.
	MOUSE_Middle 			= 0x00000004,	// Middle mouse button.
	MOUSE_FirstHit			= 0x00000008,	// Sent when a mouse button is initially hit.
	MOUSE_LastRelease		= 0x00000010,	// Sent when last mouse button is released.
	MOUSE_SetMode			= 0x00000020,	// Called when a new viewport mode is first set.
	MOUSE_ExitMode			= 0x00000040,	// Called when the existing mode is changed.
	MOUSE_Ctrl				= 0x00000080,	// Ctrl is pressed.
	MOUSE_Shift				= 0x00000100,	// Shift is pressed.
	MOUSE_Alt				= 0x00000200,	// Alt is pressed.
	MOUSE_LeftDouble		= 0x00000400,	// Left double click.
	MOUSE_MiddleDouble		= 0x00000800,	// Middle double click.
	MOUSE_RightDouble		= 0x00001000,	// Right double click.
};

// Mouse control styles - used by the editor
enum EMouseControl
{
	MOUSEC_Locked		= 0,		// Mouse is locked in center of the captured window
	MOUSEC_Free			= 1,		// Mouse is free to roam around the captured window
};

//
//	FViewportRenderTarget
//

class FViewportRenderTarget : public FRenderTarget
{
public:

	UViewport*	Viewport;

	// Constructor.

	FViewportRenderTarget(UViewport* InViewport)
	{
		Viewport = InViewport;
	}

	// FBaseTexture interface.

	virtual INT GetWidth();
	virtual INT GetHeight();
	virtual INT GetFirstMip() { return 0; }
	virtual INT GetNumMips() { return 1; }
	virtual ETextureFormat GetFormat();
	// scion sz - these should be TC_Clamp when using linear textures
	virtual ETexClampMode GetUClamp() { return TC_Clamp; }
	virtual ETexClampMode GetVClamp() { return TC_Clamp; }

	// FRenderResource interface.

	virtual INT GetRevision() { return 1; }
};

//
// A viewport object, which associates an actor (which defines
// most view parameters) with a Windows window.
//
class ENGINE_API UViewport : public UPlayer
{
	DECLARE_ABSTRACT_CLASS(UViewport,UPlayer,CLASS_Transient,Engine)
	DECLARE_WITHIN(UClient)

	enum { INVALID_GAMEPAD_ID = 255	};

	// Referenced objects.
	class UCanvas*			Canvas;		// Viewport's painting canvas.
	class UInput*			Input;		// Input system.
	class URenderDevice*	RenDev;		// Render device.

	// Normal variables.
	UObject*		MiscRes;		// Used in in modes like EM_TEXVIEW.
	FName			Group;			// Group for editing.
	DOUBLE			LastUpdateTime;	// Time of last update.
	INT				DirtyViewport;	// Count how many times the viewport needs to be repainted.
	INT				SizeX, SizeY;   // Buffer X & Y resolutions.
	INT				OffsetX, OffsetY,			// scion sz - viewport offset from top left (splitscreen).
					OffsetZ;					// Z offset is used to sort the viewports and draw them in back-to-front order.
												// Viewports with smaller Z offset values draw first.
	FLOAT			ScaleFOVX, ScaleFOVY;		// scion sz - viewport fov scale (splitscreen)
	FLOAT			ScaleX,ScaleY;	// Scale factor.
	INT				ColorBytes;		// Engine only runs in 32bit color.
	INT				FrameCount;		// Frame count, incremented when locked.
	DWORD			Caps;			// Capabilities (CC_).
	UBOOL			Current;		// If this is the current input viewport.
	UBOOL			Dragging;		// Dragging mouse.
	DWORD			Buttons;		// The current set of mouse buttons being held down (used in the editor)
	FVector			OrigCursor;		// The position where the mouse was originally clicked in the viewport.
	UBOOL			FullscreenOnly; // Engine requires desktop set to 32 bpp for windowed mode.
	FVector			TerrainPointAtLocation;	// The 3D location currently being pointed at on the terrain
	UBOOL			UseSoftwareRasterizer;

	FViewportRenderTarget	RenderTarget;	// A dummy render target that exposes the viewport's size to interfaces that take a FRenderTarget.

	// The current position of the mouse both on the screen, and within this viewports client area (using the X, Y ... Z is unused)
	FVector			MouseScreenPos, MouseClientPos;

	UBOOL			bLockOnSelectedActors;	// Update the location/rotation of selected actors when the camera is moved?
	AActor*			LockedActor;			// The actor we're locked to
	class UPlayerProfile*	Profile;	// Profile current associated with the viewport -- rjp

	void LockOnActor( AActor* InActor )
	{
		if( !bLockOnSelectedActors ) return;

		LockedActor = InActor;
		if( InActor )
		{
			Actor->Location = InActor->Location;
			Actor->Rotation = InActor->Rotation;
		}
	}

	// Editor and debugging mode render effects, stat flags.
	UBOOL			bShowLargeVertices;	 // Show large vertices on brushes/meshes/etc?
	UBOOL           bShowBounds;     // Show visibility bounding boxes.
	UBOOL           bShowBones;      // Draw skeleton for skeletal meshes.
	UBOOL           bHideSkin;       // Hide/draw skin for meshes.
	UBOOL           bShowNormals;    // Draw bone-influence-number-color coded normals on mesh vertices.
	UBOOL			bShowGrid;		 // Draws grid for tweaking FOV/ distortion.
	UBOOL			bShowProjectorHulls;	// scion sz - show projector bounds/frustum
	UBOOL			bDebugShowGamma;		// scion sz - turn on to see gamma curve and luminosity histogram	

	// scion sz -
	// viewport rendering switches
	UBOOL			bShowWeather;			// turn weather effects on/off
	UBOOL			bShowCameraEffects;		// turn camera effects on/off
	UBOOL			bForceBlobShadows;		// if on then shadows always shown as blob texture
	UBOOL			bShowMenu;				// only render the viewport interactions
	UBOOL			bShowScene;				// allows rendering of current level
	UBOOL			bDisableDraw;			// if on then the viewport will not be drawn

	// Cinematics.
	UBOOL			bRenderCinematics;
	FLOAT			CinematicsRatio;

	// Level traveling.
	FURL			PlayerURL;
	ETravelType		TravelType;
	FStringNoInit	TravelURL;
	UBOOL			bTravelItems;

	// Frame buffer info; only valid when locked.
	DOUBLE			CurrentTime;	// Time when initially locked.
	BYTE*			ScreenPointer;	// Pointer to screen frame buffer, or NULL if none.
	INT				Stride;			// Stride in pixels.

	// Rendering data; only valid when locked.
	class FRenderInterface*	RI;
	class FSceneNode*		LodSceneNode;

	// Stencil mask; only valid when locked.  The next unused stencil bit.
	BYTE			NextStencilMask;

	// Precaching flag.
	UBOOL	Precaching,
			PendingFrame;

	// Hit testing.
	UBOOL			HitTesting;		// Whether hit-testing.
	INT				HitX, HitY;		// Hit rectangle top left.
	INT				HitXL, HitYL;	// Hit size.
	TArray<INT>		HitSizes;		// Currently pushed hit sizes.

	// Saved-actor parameters.
	FLOAT SavedOrthoZoom, SavedFovAngle;
	INT SavedShowFlags, SavedRendMap, SavedMisc1, SavedMisc2;

	// Current game pad ID for this viewport/playercontroller
	BYTE GamepadID;

	// Constructor.
	UViewport();

	// UObject interface.
	void Destroy();
	void Serialize( FArchive& Ar );

	// FArchive interface.
	void Serialize( const TCHAR* Data, EName MsgType );

	// UPlayer interface.
	void ReadInput( FLOAT DeltaSeconds );
	BYTE GetGamepadID() { return GamepadID; }

	// UViewport interface.
	virtual UBOOL Lock( BYTE* HitData=NULL, INT* HitSize=NULL );
	virtual void Unlock();
	virtual void Present();

	virtual UBOOL SetDrag( UBOOL NewDrag );
	virtual UBOOL IsFullscreen()=0;
	virtual UBOOL ResizeViewport( DWORD BlitType, INT X=INDEX_NONE, INT Y=INDEX_NONE, UBOOL bSaveSize=true )=0;
	virtual void SetModeCursor()=0;
	virtual void UpdateWindowFrame()=0;
	virtual void OpenWindow( DWORD ParentWindow, UBOOL Temporary, INT NewX, INT NewY, INT OpenX, INT OpenY )=0;
	virtual void CloseWindow()=0;
	virtual void UpdateInput( UBOOL Reset, FLOAT DeltaSeconds )=0;
	virtual void* GetWindow()=0;
	virtual void* GetServer();
	virtual void UpdateMousePosition() {}
	virtual void SetMouseCapture( UBOOL Capture, UBOOL Clip, UBOOL FocusOnly=0 )=0;
	virtual void Repaint( UBOOL Blit, FLOAT DeltaSeconds = 0.f )=0;
	virtual UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar=*GLog );
	virtual void TryRenderDevice( const TCHAR* ClassName, INT NewX, INT NewY, UBOOL Fullscreen ) {}
	virtual	TCHAR* GetLocalizedKeyName( EInputKey Key )=0;


	// Functions.
	void ExecuteHits( const FHitCause& Cause, BYTE* HitData, INT HitSize, TCHAR* HitOverrideClass=NULL, FColor* HitColor=NULL, AActor** HitActor=NULL );
	void PushHit( const struct HHitProxy& Hit, INT Size );
	void PopHit( UBOOL bForce );
	UBOOL IsWire() const;
	void ExecMacro( const TCHAR* Filename, FOutputDevice& Ar=*GLog );

	// UViewport inlines.
	BYTE* _Screen( INT X, INT Y )
	{
		return ScreenPointer + (X+Y*Stride)*ColorBytes;
	}
	UBOOL IsOrtho() const
	{
		return Actor && (Actor->RendMap==REN_OrthXY||Actor->RendMap==REN_OrthXZ||Actor->RendMap==REN_OrthYZ);
	}
	UBOOL IsPerspective() const
	{
		return Actor && (Actor->RendMap==REN_Wire||Actor->RendMap==REN_Zones||Actor->RendMap==REN_Polys||Actor->RendMap==REN_PolyCuts||Actor->RendMap==REN_DynLight||Actor->RendMap==REN_PlainTex||Actor->RendMap==REN_LightingOnly||Actor->RendMap==REN_MatineePreview||(Actor->RendMap==REN_TexView&&Actor->Misc1!=MVT_TEXTURE));
	}
	UBOOL IsTopView() const
	{
		return Actor && Actor->RendMap==REN_OrthXY;
	}
	UBOOL IsRealtime() const
	{
		return Actor && (Actor->ShowFlags&(SHOW_RealTime | SHOW_PlayerCtrl));
	}
	UBOOL IsLit() const
	{
		return Actor && (Actor->RendMap==REN_DynLight||Actor->RendMap==REN_LightingOnly||Actor->RendMap==REN_DepthComplexity||Actor->RendMap==REN_MatineePreview||(Actor->RendMap==REN_TexView&&Actor->Misc1!=MVT_TEXTURE));
	}
	UBOOL IsDepthComplexity() const
	{
		return Actor && (Actor->RendMap == REN_DepthComplexity);
	}
	// Determines if the viewport is a viewport where the user can be editing the level (i.e. not a browser viewport)
	UBOOL IsEditing() const
	{
		return Actor && ( IsOrtho() || Actor->RendMap==REN_Wire || Actor->RendMap==REN_Zones || Actor->RendMap==REN_Polys || Actor->RendMap==REN_PolyCuts || Actor->RendMap==REN_DynLight || Actor->RendMap==REN_PlainTex || Actor->RendMap==REN_LightingOnly || Actor->RendMap==REN_DepthComplexity );
	}
	// Refresh all viewports.
	static void RefreshAll()
	{
		for( TObjectIterator<UViewport> It; It; ++It )
			It->DirtyViewport = 1;
	}

#ifdef _XBOX
	virtual UNetConnection* GetServerConnection();
#endif

	//scion sz - get this viewport's index in the client Viewports[] array
	INT GetViewportIndex();

	//scion sz - moved up from derived class
	//scion jg -- Helper method for determining whether the current gamepad is ours
	UBOOL IsOurGamepad(int GamepadID);
	//scion jg -- Helper method for setting the gamepad id
	void SetGamepadID(int GamepadID);

	// Helper for retrieving a URL initialized from this player's profile.
	UBOOL InitializeURL( FURL& out_URL );
	//epic moelfke: added accessor for setting travel URL
	virtual void SetClientTravel(const TCHAR* NextURL, UBOOL bItems, ETravelType NextTravelType);
};

// Viewport hit-testing macros.
#ifndef _XBOX //scion jg -- No push/pop macros on the xbox
#define PUSH_HIT(Viewport,expr) if( (Viewport)->HitTesting ) (Viewport)->PushHit(expr,sizeof(expr));
#define POP_HIT(Viewport)       if( (Viewport)->HitTesting ) (Viewport)->PopHit(0);
#define POP_HIT_FORCE(Viewport) if( (Viewport)->HitTesting ) (Viewport)->PopHit(1);
#else
#define PUSH_HIT(Viewport,expr)
#define POP_HIT(Viewport)
#define POP_HIT_FORCE(Viewport)
#endif
/*-----------------------------------------------------------------------------
	Base hit proxy.
-----------------------------------------------------------------------------*/

// Hit proxy declaration macro.
#define DECLARE_HIT_PROXY(cls,parent) \
	const TCHAR* GetName() const \
		{ return TEXT(#cls); } \
	UBOOL IsA( const TCHAR* Str ) const \
		{ return appStricmp(TEXT(#cls),Str)==0 || parent::IsA(Str); }

// Base class for detecting user-interface hits.
struct ENGINE_API HHitProxy
{
	union
	{
		mutable INT Size;
		HHitProxy* Parent;
	};
	FColor HitColor;
	virtual const TCHAR* GetName() const
	{
		return TEXT("HHitProxy");
	}
	virtual UBOOL IsA( const TCHAR* Str ) const
	{
		return appStricmp(TEXT("HHitProxy"),Str)==0;
	}
	virtual void Click( const FHitCause& Cause )
	{
		Cause.Observer->Click( Cause, *this );
	}
	virtual AActor* GetActor()
	{
		return NULL;
	}
};

/*-----------------------------------------------------------------------------
	UClient.
-----------------------------------------------------------------------------*/

//scion bcl -- added loadsavemanager
class ULoadSaveManager;

// D-2 (UI scaling, docs/porting/analysis/ui-scale.md): [<Client>] UIScaleMode and UIMenuBackground.
enum EUIScaleMode
{
	UISCALE_Native640	= 0,	// the canvas is the window's pixels: the 640x480 UI drawn 1:1 from the top-left (before D-2)
	UISCALE_Fit43		= 1,	// the canvas is the 640x480 design space, drawn in a centred 4:3 box of the window
};
enum EUIMenuBackground
{
	UIMENUBG_Box		= 0,	// a menu's full-screen background stays in the 4:3 box
	UIMENUBG_Fill		= 1,	// it fills the window, aspect kept (cropped top and bottom on a wide window)
};

//
// Client, responsible for tracking viewports.
//
class ENGINE_API UClient : public UObject
{
	DECLARE_ABSTRACT_CLASS(UClient,UObject,CLASS_Config,Engine)

	// Variables.
	UEngine*			Engine;
	TArray<UViewport*>	Viewports;

	// Configurable.
	BITFIELD	CaptureMouse;
	BITFIELD	ScreenFlashes;
	BITFIELD	NoLighting;
	BITFIELD	Decals;
	BITFIELD	NoDynamicLights;
	BITFIELD    NoFractalAnim;
	BITFIELD	Coronas;
	BITFIELD	DecoLayers;
	BITFIELD	Projectors;
	BITFIELD	ReportDynamicUploads;
	INT			WindowedViewportX;
	INT			WindowedViewportY;
	INT			FullscreenViewportX;
	INT			FullscreenViewportY;
	FLOAT		Brightness;
	FLOAT		Contrast;
	FLOAT		Gamma;
	UBOOL		DynamicGamma;						// scion sz - enable dynamic gamma updates based on scene luminosity
	FLOAT		LuminosityBias;						// scion sz - scale luminosity pixels after we get them from the back buffer
	INT			TextureLODSet[LODSET_MAX];
	FLOAT		MinDesiredFrameRate;
	UViewport*	LastCurrent;						// The viewport that was last current
	FLOAT		MaxDistortion;						// scion sz - maximum screen space distortion offset in pixels
	BYTE		UIScaleMode;						// D-2: EUIScaleMode
	BYTE		UIMenuBackground;					// D-2: EUIMenuBackground (Fit43 only)
	BITFIELD	UISharpText;						// D-2: a scaled-up UI takes its glyphs from bigger sizes of the font

	// scion sz -
	class UViewportMgr*		ViewportMgr;
	class USplitScreenMgr*	SplitScreenMgr;
	
    UEnum *PTextureDetail;

	class UInteractionMaster* InteractionMaster;	// Input router
	ULoadSaveManager * LoadSaveManager;

	// Constructors.
	UClient();
	void StaticConstructor();

	// UObject interface.
	void Serialize( FArchive& Ar );
	void Destroy();
	void PostEditChange();

	// UClient interface.
	virtual void Init( UEngine* InEngine )=0;
	virtual void Flush( UBOOL AllowPrecache );
	virtual void UpdateGamma();
	virtual void RestoreGamma();
	virtual void ShowViewportWindows( DWORD ShowFlags, int DoShow )=0;
	virtual void EnableViewportWindows( DWORD ShowFlags, int DoEnable )=0;
	virtual void Tick( float DeltaSeconds )=0;
	virtual UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar=*GLog )=0;
	virtual class UViewport* NewViewport( const FName Name )=0;
	virtual void MakeCurrent( UViewport* NewViewport )=0;
	virtual UViewport* GetLastCurrent()=0;
	virtual void ZSortViewports();
	virtual void GetInitScreenSize( INT& SizeX, INT& SizeY )=0;
	// uc2 port (G-6): the capability the Xbox client has by construction. A client that draws split-screen
	// viewports as rectangles of one screen (UXboxClient; SDL2Drv's USDL2Client) gets the Xbox's split-screen
	// paths (SPLIT ADD/REMOVE make and remove real players); WinDrv's one-window-per-viewport client keeps
	// Epic's PC stand-ins.
	virtual UBOOL SplitScreenViewports() { return 0; }

    INT GetTextureLODBias (ELODSet LODSet);

	UBOOL AllowControllerJoin() const;
	static UBOOL BeginSplitScreen( UClient* Client, INT GamepadID );

	FORCEINLINE USplitScreenMgr* GetSplitScreenMgr() { return SplitScreenMgr; }
	FORCEINLINE UViewportMgr* GetViewportMgr() { return ViewportMgr; }
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

