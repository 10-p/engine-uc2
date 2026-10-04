/*=============================================================================
	Editor.h: Unreal editor public header file.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

#ifndef _INC_EDITOR
#define _INC_EDITOR

/*-----------------------------------------------------------------------------
	Dependencies.
-----------------------------------------------------------------------------*/

#include "Engine.h"
#include "UnPrefab.h"	// UnrealEd Prefabs

struct EDITOR_API FBuilderPoly
{
	TArray<INT> VertexIndices;
	INT Direction;
	FName ItemName;
	INT PolyFlags;
	FBuilderPoly()
	: VertexIndices(), Direction(0), ItemName(NAME_None)
	{}
};

#include "Perlin.h"
#include "EditorClasses.h"

/*-----------------------------------------------------------------------------
	Editor public.
-----------------------------------------------------------------------------*/

#define dED_MAX_VIEWPORTS	16
#define MAX_EDCMD 512   // Max Unrealed->Editor Exec command string length.

//
// The editor object.
//

EDITOR_API extern class UEditorEngine* GEditor;

// Texture alignment.
enum ETAxis
{
    TAXIS_X                 = 0,
    TAXIS_Y                 = 1,
    TAXIS_Z                 = 2,
    TAXIS_WALLS             = 3,
    TAXIS_AUTO              = 4,
};

//
// Importing object properties.
//
EDITOR_API const TCHAR* ImportProperties
(
	UClass*				ObjectClass,
	BYTE*				Object,
	ULevel*				Level,
	const TCHAR*		Data,
	UObject*			InParent,
	FFeedbackContext*	Warn,
	INT					Depth
);

//
// GBuildStaticMeshCollision - Global control for building static mesh collision on import.
//

extern UBOOL GBuildStaticMeshCollision;

//
// Creating a static mesh from an array of triangles.
//
EDITOR_API UStaticMesh* CreateStaticMesh(TArray<FStaticMeshTriangle>& Triangles,TArray<FStaticMeshMaterial>& Materials,UObject* Outer,FName Name);

//
// Converting models to static meshes.
//
void GetBrushTriangles(TArray<FStaticMeshTriangle>& Triangles,TArray<FStaticMeshMaterial>& Materials,ABrush* Brush,UModel* Model);
EDITOR_API UStaticMesh* CreateStaticMeshFromBrush(UObject* Outer,FName Name,ABrush* Brush,UModel* Model);

//
// Converting static meshes back to brushes.
//
EDITOR_API void CreateModelFromStaticMesh(UModel* Model,AActor* StaticMeshActor);

//
// Converting actors using meshes to static meshes.
//
void GetActorTriangles(TArray<FStaticMeshTriangle>& Triangles,TArray<FStaticMeshMaterial>& Materials,AActor* Actor);
EDITOR_API UStaticMesh* CreateStaticMeshFromActor(UObject* Outer,FName Name,AActor* Actor);

//
// Editor mode settings.
//
// These are also referenced by help files and by the editor client, so
// they shouldn't be changed.
//
enum EEditorMode
{
	EM_None 				= 0,	// Gameplay, editor disabled.
	EM_ViewportMove			= 1,	// Move viewport normally.
	EM_ViewportZoom			= 2,	// Move viewport with acceleration.
	EM_ActorRotate			= 5,	// Rotate actors.
	EM_ActorScale			= 8,	// Scale actors.
	EM_TexturePan			= 11,	// Pan textures.
	EM_TextureRotate		= 13,	// Rotate textures.
	EM_TextureScale			= 14,	// Scale textures.
	EM_ActorSnapScale		= 18,	// Actor snap-scale.
	EM_TexView				= 19,	// Viewing textures.
	EM_TexBrowser			= 20,	// Browsing textures.
	EM_StaticMeshBrowser	= 21,	// Browsing static meshes.
	EM_MeshView				= 22,	// Viewing mesh.
	EM_MeshBrowser			= 23,	// Browsing mesh.
	EM_BrushClip			= 24,	// Brush Clipping.
	EM_VertexEdit			= 25,	// Multiple Vertex Editing.
	EM_FaceDrag				= 26,	// Face Dragging.
	EM_Polygon				= 27,	// Free hand polygon drawing
	EM_TerrainEdit			= 28,	// Terrain editing.
	EM_PrefabBrowser		= 29,	// Browsing prefabs.
	EM_Matinee				= 30,	// Movie editing.
	EM_EyeDropper			= 31,	// Eyedropper
	EM_Animation			= 32,	// Viewing animation.
	EM_FindActor			= 33,	// Find Actor
	EM_MaterialEditor		= 34,	// Material editor
	EM_Geometry				= 35,	// Geometry editing mode

#ifdef WITH_LIPSINC
	EM_LIPSinc				= 36,   // Browsing LIPSinc
#endif

	EM_NewCameraMove		= 50,
};

/*-----------------------------------------------------------------------------
	Hit proxies.
-----------------------------------------------------------------------------*/

// Hit a texture view.
struct HTextureView : public HHitProxy
{
	DECLARE_HIT_PROXY(HTextureView,HHitProxy)
	UMaterial* Material;
	INT ViewX, ViewY;
	HTextureView( UMaterial* InMaterial, INT InX, INT InY ) : Material(InMaterial), ViewX(InX), ViewY(InY) {}
	void Click( const FHitCause& Cause );
};

// Hit a global pivot.
struct HGlobalPivot : public HHitProxy
{
	DECLARE_HIT_PROXY(HGlobalPivot,HHitProxy)
	FVector Location;
	HGlobalPivot( FVector InLocation ) : Location(InLocation) {}
};

// Hit a browser texture.
struct HBrowserMaterial : public HHitProxy
{
	DECLARE_HIT_PROXY(HBrowserMaterial,HHitProxy)
	UMaterial* Material;
	HBrowserMaterial( UMaterial* InMaterial ) : Material(InMaterial) {}
};

// Hit the backdrop.
struct HBackdrop : public HHitProxy
{
	DECLARE_HIT_PROXY(HBackdrop,HHitProxy)
	FVector Location;
	HBackdrop( FVector InLocation ) : Location(InLocation) {}
	void Click( const FHitCause& Cause );
};

/*-----------------------------------------------------------------------------
	FScan.
-----------------------------------------------------------------------------*/

typedef void (*POLY_CALLBACK)( UModel* Model, INT iSurf );

/*-----------------------------------------------------------------------------
	FConstraints.
-----------------------------------------------------------------------------*/

//
// General purpose movement/rotation constraints.
//
class EDITOR_API FConstraints
{
public:
	// Functions.
	virtual void Snap( FVector& Point, FVector GridBase )=0;
	virtual void Snap( FRotator& Rotation )=0;
	virtual UBOOL Snap( ULevel* Level, FVector& Location, FVector GridBase, FRotator& Rotation )=0;
};

/*-----------------------------------------------------------------------------
	FConstraints.
-----------------------------------------------------------------------------*/

//
// General purpose movement/rotation constraints.
//
class EDITOR_API FEditorConstraints : public FConstraints
{
public:
	// Variables.
	BITFIELD	GridEnabled:1;		// Grid on/off.
	BITFIELD	SnapVertices:1;		// Snap to nearest vertex within SnapDist, if any.
	FLOAT		SnapDistance;		// Distance to check for snapping.
	FVector		GridSize;			// Movement grid.
	UBOOL		RotGridEnabled;		// Rotation grid on/off.
	FRotator	RotGridSize;		// Rotation grid.

	// Functions.
	virtual void Snap( FVector& Point, FVector GridBase );
	virtual void Snap( FRotator& Rotation );
	virtual UBOOL Snap( ULevel* Level, FVector& Location, FVector GridBase, FRotator& Rotation );
};

/*-----------------------------------------------------------------------------
	UEditorEngine definition.
-----------------------------------------------------------------------------*/

class EDITOR_API UEditorEngine : public UEngine
{
	DECLARE_CLASS(UEditorEngine,UEngine,CLASS_Transient|CLASS_Config,Editor)

	// Objects.
	ULevel*					 Level;
	UModel*					 TempModel;
	UMaterial*				 CurrentMaterial;
	UStaticMesh*			 CurrentStaticMesh;
	UMesh*					 CurrentMesh;
	UClass*					 CurrentClass;
	class UTransactor*		 Trans;
	class UTextBuffer*		 Results;
	class WObjectProperties* ActorProperties;
	class WObjectProperties* LevelProperties;
	class WConfigProperties* Preferences;
	class WProperties*       UseDest;
	INT                      AutosaveCounter;
	INT						 Pad[3];

	// Graphics.
	UTexture *Bad;
	UTexture *Bkgnd, *BkgndHi, *BadHighlight, *MaterialArrow, *MaterialBackdrop;

	// Static Meshes
	UStaticMesh* TexPropCube;
	UStaticMesh* TexPropSphere;

	// Toggles.
	BITFIELD				FastRebuild  :1;
	BITFIELD				Bootstrapping:1;

	// Variables.
	INT						AutoSaveIndex;
	INT						AutoSaveCount;
	INT						Mode;
	INT						TerrainEditBrush;
	DWORD					ClickFlags;
	FLOAT					MovementSpeed;
	UObject*				ParentContext;
	FVector					ClickLocation;			// Where the user last clicked in the world
	FPlane					ClickPlane;

	// Tools.
	TArray<UObject*>		Tools;
	UClass*					BrowseClass;

	// Constraints.
	FEditorConstraints		Constraints;

	// Advanced.
	BITFIELD	UseSizingBox:1;		// Shows sizing information in the top left corner of the viewports
	BITFIELD	UseAxisIndicator:1;	// Displays an axis indictor in the bottom left corner of the viewports
	FLOAT FovAngle;
	BITFIELD GodMode:1;
	BITFIELD AutoSave:1;
	BYTE AutosaveTimeMinutes;
	FStringNoInit GameCommandLine;
	TArray<FString> EditPackages;
	BITFIELD	AlwaysShowTerrain:1;			// Always show the terrain in the overhead 2D view?
	BITFIELD	UseActorRotationGizmo:1;		// Use the gizmo for rotating actors?
	BITFIELD	LoadEntirePackageWhenSaving:1;	// Will load the entire package into memory before attempting to save it
	BITFIELD    ViewCorrected3DDrag:1;			//scion capps For dragging in 3D view, uses view angle.

	// Constructor.
	void StaticConstructor();
	UEditorEngine();

	// UObject interface.
	void Destroy();
	void Serialize( FArchive& Ar );

	// UEngine interface.
	void Init();
	void InitEditor();

	void Tick( FLOAT DeltaSeconds );
	void Draw( UViewport* Viewport, FLOAT DeltaSeconds, UBOOL Blit=1, BYTE* HitData=NULL, INT* HitSize=NULL ) { check(0); }
	void MouseDelta( UViewport* Viewport, DWORD Buttons, FLOAT DX, FLOAT DY ) { check(0); }
	void MousePosition( UViewport* Viewport, DWORD Buttons, FLOAT X, FLOAT Y ) { check(0); }
	void MouseWheel( UViewport* Viewport, DWORD Buttons, INT Delta ) { check(0); }
	void Click( UViewport* Viewport, DWORD Buttons, FLOAT X, FLOAT Y ) { check(0); }
	void UnClick( UViewport* Viewport, DWORD Buttons, INT MouseX, INT MouseY ) { check(0); }
	void SetClientTravel( UPlayer* Viewport, const TCHAR* NextURL, UBOOL bItems, ETravelType TravelType ) {}
	
	// UnEdSrv.cpp
	virtual UBOOL SafeExec( const TCHAR* Cmd, FOutputDevice& Ar=*GLog );
	void ExecMacro( const TCHAR* Filename, FOutputDevice& Ar );
	UBOOL Exec( const TCHAR* Cmd, FOutputDevice& Ar=*GLog );
	UBOOL Exec_StaticMesh( const TCHAR* Str, FOutputDevice& Ar );
	UBOOL Exec_Brush( const TCHAR* Str, FOutputDevice& Ar );
	UBOOL Exec_Paths( const TCHAR* Str, FOutputDevice& Ar );
	UBOOL Exec_BSP( const TCHAR* Str, FOutputDevice& Ar );
	UBOOL Exec_Light( const TCHAR* Str, FOutputDevice& Ar );
	UBOOL Exec_Map( const TCHAR* Str, FOutputDevice& Ar );
	UBOOL Exec_Select( const TCHAR* Str, FOutputDevice& Ar );
	UBOOL Exec_Poly( const TCHAR* Str, FOutputDevice& Ar );
	UBOOL Exec_Texture( const TCHAR* Str, FOutputDevice& Ar );
	UBOOL Exec_Transaction( const TCHAR* Str, FOutputDevice& Ar );
	UBOOL Exec_Obj( const TCHAR* Str, FOutputDevice& Ar );
	UBOOL Exec_Class( const TCHAR* Str, FOutputDevice& Ar );
	UBOOL Exec_Camera( const TCHAR* Str, FOutputDevice& Ar );
	UBOOL Exec_Level( const TCHAR* Str, FOutputDevice& Ar );
	UBOOL Exec_Terrain( const TCHAR* Str, FOutputDevice& Ar );
	UBOOL Exec_Audio( const TCHAR* Str, FOutputDevice& Ar );
	UBOOL Exec_BrushClip( const TCHAR* Str, FOutputDevice& Ar );
	UBOOL Exec_Anim( const TCHAR* Str, FOutputDevice& Ar );
	UBOOL Exec_Fluid( const TCHAR* Str, FOutputDevice& Ar );

	// Pivot handling.
	virtual FVector GetPivotLocation() { return FVector(0,0,0); }
	virtual void SetPivot( FVector NewPivot, UBOOL SnapPivotToGrid, UBOOL MoveActors, UBOOL bIgnoreAxis ) {}
	virtual void ResetPivot() {}


	// General functions.
	virtual void Cleanse( UBOOL Redraw, const TCHAR* TransReset );
	virtual void FinishAllSnaps( ULevel* Level ) { check(0); }
	virtual void RedrawLevel( ULevel* Level );
	virtual void ResetSound();
	virtual void NoteSelectionChange( ULevel* Level ) { check(0); }
	virtual AActor* AddActor( ULevel* Level, UClass* Class, FVector V, UBOOL bSilent = 0 ) { check(0); return NULL; }
	virtual void NoteActorMovement( ULevel* Level ) { check(0); }
	virtual UTransactor* CreateTrans();
	UViewport* GetCurrentViewport();
	void RedrawAllViewports( UBOOL bLevelViewportsOnly );
	void RedrawCurrentViewport();

	// Editor CSG virtuals from UnEdCsg.cpp.
	virtual void csgPrepMovingBrush( ABrush* Actor );
	virtual void csgCopyBrush( ABrush* Dest, ABrush* Src, DWORD PolyFlags, DWORD ResFlags, UBOOL NeedsPrep );
	virtual ABrush*	csgAddOperation( ABrush* Actor, ULevel* Level, DWORD PolyFlags, ECsgOper CSG );
	virtual void csgRebuild( ULevel* Level );
	virtual const TCHAR* csgGetName( ECsgOper CsgOper );

	// Editor EdPoly/BspSurf assocation virtuals from UnEdCsg.cpp.
	virtual INT polyFindMaster( UModel* Model, INT iSurf, FPoly& Poly );
	virtual void polyUpdateMaster( UModel* Model, INT iSurf, INT UpdateTexCoords );
	virtual void polyGetLinkedPolys( ABrush* InBrush, FPoly* InPoly, TArray<FPoly>* InPolyList );
	virtual void polyGetOuterEdgeList( TArray<FPoly>* InPolyList, TArray<FEdge>* InEdgeList );
	virtual void polySplitOverlappingEdges( TArray<FPoly>* InPolyList, TArray<FPoly>* InResult );

	// Bsp Poly search virtuals from UnEdCsg.cpp.
	virtual void polySetAndClearPolyFlags( UModel* Model, DWORD SetBits, DWORD ClearBits, INT SelectedOnly, INT UpdateMaster );

	// Selection.
	virtual void SelectActor( ULevel* Level, AActor* Actor, UBOOL bSelect = 1, UBOOL bNotify = 1 ) {}
	virtual void SelectNone( ULevel* Level, UBOOL Notify, UBOOL BSPSurfs = 1 ) {}
	virtual void SelectBSPSurf( ULevel* Level, INT iSurf, UBOOL bSelect = 1, UBOOL bNotify = 1 ) {}

	// Bsp Poly selection virtuals from UnEdCsg.cpp.
	virtual void polySelectAll ( UModel* Model );
	virtual void polySelectMatchingGroups( UModel* Model );
	virtual void polySelectMatchingItems( UModel* Model );
	virtual void polySelectCoplanars( UModel* Model );
	virtual void polySelectAdjacents( UModel* Model );
	virtual void polySelectAdjacentWalls( UModel* Model );
	virtual void polySelectAdjacentFloors( UModel* Model );
	virtual void polySelectAdjacentSlants( UModel* Model );
	virtual void polySelectMatchingBrush( UModel* Model );
	virtual void polySelectMatchingTexture( UModel* Model );
	virtual void polySelectReverse( UModel* Model );
	virtual void polyMemorizeSet( UModel* Model );
	virtual void polyRememberSet( UModel* Model );
	virtual void polyXorSet( UModel* Model );
	virtual void polyUnionSet( UModel* Model );
	virtual void polyIntersectSet( UModel* Model );
	virtual void polySelectZone( UModel *Model );

	// Poly texturing virtuals from UnEdCsg.cpp.
	virtual void polyTexPan( UModel* Model, INT PanU, INT PanV, INT Absolute );
	virtual void polyTexScale( UModel* Model,FLOAT UU, FLOAT UV, FLOAT VU, FLOAT VV, UBOOL Absolute );

	// Map brush selection virtuals from UnEdCsg.cpp.
	virtual void mapSelectOperation( ULevel* Level, ECsgOper CSGOper );
	virtual void mapSelectFlags(ULevel* Level, DWORD Flags );
	virtual void mapSelectFirst( ULevel* Level );
	virtual void mapSelectLast( ULevel* Level );
	virtual void mapBrushGet( ULevel* Level );
	virtual void mapBrushPut( ULevel* Level );
	virtual void mapSendToFirst( ULevel* Level );
	virtual void mapSendToLast( ULevel* Level );
	virtual void mapSendToSwap( ULevel* Level );
	virtual void mapSetBrush( ULevel* Level, enum EMapSetBrushFlags PropertiesMask, _WORD BrushColor, FName Group, DWORD SetPolyFlags, DWORD ClearPolyFlags, DWORD CSGOper, INT DrawType );

	// Bsp virtuals from UnBsp.cpp.
	virtual void bspRepartition( ULevel* Level, INT iNode, UBOOL Simple );
	virtual INT bspAddVector( UModel* Model, FVector* V, UBOOL Exact );
	virtual INT bspAddPoint( UModel* Model, FVector* V, UBOOL Exact );
	virtual INT bspNodeToFPoly( UModel* Model, INT iNode, FPoly* EdPoly );
	virtual void bspBuild( UModel* Model, enum EBspOptimization Opt, INT Balance, INT PortalBias, INT RebuildSimplePolys, INT iNode );
	virtual void bspRefresh( UModel* Model, UBOOL NoRemapSurfs );
	virtual void bspCleanup( UModel* Model );
	virtual void bspBuildBounds( UModel* Model );
	virtual void bspBuildFPolys( UModel* Model, UBOOL SurfLinks, INT iNode );
	virtual void bspMergeCoplanars( UModel* Model, UBOOL RemapLinks, UBOOL MergeDisparateTextures );
	virtual INT bspBrushCSG( ABrush* Actor, UModel* Model, DWORD PolyFlags, ECsgOper CSGOper, UBOOL RebuildBounds, UBOOL MergePolys = 1 );
	virtual void bspOptGeom( UModel* Model );
	virtual void bspValidateBrush( UModel* Brush, INT ForceValidate, INT DoStatusUpdate );
	virtual void bspUnlinkPolys( UModel* Brush );
	virtual INT	bspAddNode( UModel* Model, INT iParent, enum ENodePlace ENodePlace, DWORD NodeFlags, FPoly* EdPoly );

	// Shadow virtuals (UnShadow.cpp).
	virtual void shadowIlluminateBsp( ULevel* Level, UBOOL SelectedOnly, UBOOL ChangedOnly );

	// Mesh functions (UnMeshEd.cpp).
	virtual void meshVertImport( const TCHAR* MeshName, UObject* InParent, const TCHAR* AnivFname, const TCHAR* DataFname, UBOOL Unmirror, UBOOL ZeroTex, INT UnMirrorTex, FLODProcessInfo* LODInfo);
	virtual void meshSkelImport( const TCHAR* MeshName, UObject* InParent, const TCHAR* SkinFname, UBOOL Unmirror, UBOOL ZeroTex, UBOOL LinkMaterials, FLODProcessInfo* LODInfo );	
	virtual void meshVertLODProcess( UVertMesh* Mesh, TArray<FMeshTri>& RawTris, FLODProcessInfo* LODInfo);
	virtual void meshSkelLODProcess( USkeletalMesh* Mesh, FLODProcessInfo* LODInfo, USkelImport* RawData );		
	virtual void meshDropFrames( UVertMesh* Mesh, INT StartFrame, INT NumFrame );

	// Skeletal animation, digest and linkup functions (UnMeshEd.cpp).	
	virtual INT  animGetBoneIndex( UMeshAnimation* Anim, FName TempFname );
	virtual void animationImport( const TCHAR* AnimName, UObject* InParent, const TCHAR* DataFname, UBOOL Unmirror, FLOAT CompDefault ); 	
	virtual void digestMovementRepertoire( UMeshAnimation* Anim );
	virtual void movementDigest( UMeshAnimation* Anim, INT Index );
	virtual void movementCompress( UMeshAnimation* Anim, INT MoveIndex, INT KeyQuotum, FLOAT KeyReduction );	
	virtual void movementFlexConvert( UMeshAnimation* Anim, INT MoveIndex = -1 );
	virtual void movementFlexCompress( UMeshAnimation* Anim, INT MoveIndex, INT KeyQuotum, FLOAT KeyReduction );

	// Visibility.
	virtual void TestVisibility( ULevel* Level, UModel* Model, int A, int B );

	// Scripts.
	virtual int MakeScripts( UClass* BaseClass, FFeedbackContext* Warn, UBOOL MakeAll, UBOOL Booting, UBOOL MakeSubclasses );
	virtual int CheckScripts( FFeedbackContext* Warn, UClass* Class, FOutputDevice& Ar );

	// Topics.
	virtual void Get( const TCHAR* Topic, const TCHAR* Item, FOutputDevice& Ar );
	virtual void Set( const TCHAR* Topic, const TCHAR* Item, const TCHAR* Value );

	void brushclipDeleteMarkers();
	void polygonDeleteMarkers();
};

/*-----------------------------------------------------------------------------
	Parameter parsing functions.
-----------------------------------------------------------------------------*/

EDITOR_API UBOOL GetFVECTOR( const TCHAR* Stream, const TCHAR* Match, FVector& Value );
EDITOR_API UBOOL GetFVECTOR( const TCHAR* Stream, FVector& Value );
EDITOR_API UBOOL GetFROTATOR( const TCHAR* Stream, const TCHAR* Match, FRotator& Rotation, int ScaleFactor );
EDITOR_API UBOOL GetFROTATOR( const TCHAR* Stream, FRotator& Rotation, int ScaleFactor );
EDITOR_API UBOOL GetBEGIN( const TCHAR** Stream, const TCHAR* Match );
EDITOR_API UBOOL GetEND( const TCHAR** Stream, const TCHAR* Match );
EDITOR_API TCHAR* SetFVECTOR( TCHAR* Dest, const FVector* Value );
EDITOR_API UBOOL GetFSCALE( const TCHAR* Stream, FScale& Scale );

/*-----------------------------------------------------------------------------
	Texture aligner types.
-----------------------------------------------------------------------------*/

enum ETexAligner
{
	TXA_None		= -1,
	TXA_Planar		= 0,	// Planar alignment
	TXA_Cylinder	= 1,	// Cylindrical alignment
};

class EDITOR_API UTextureExporterPCX : public UExporter
{
	DECLARE_CLASS(UTextureExporterPCX,UExporter,0,Editor)
	void StaticConstructor();
	UBOOL ExportBinary( UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn );
};

class EDITOR_API UTextureExporterBMP : public UExporter
{
	DECLARE_CLASS(UTextureExporterBMP,UExporter,0,Editor)
	void StaticConstructor();
	UBOOL ExportBinary( UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn );
};
class EDITOR_API UTextureExporterTGA : public UExporter
{
	DECLARE_CLASS(UTextureExporterTGA,UExporter,0,Editor)
	void StaticConstructor();
	UBOOL ExportBinary( UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn );
};

// sjs ---
class EDITOR_API UTextureExporterDDS : public UExporter
{
	DECLARE_CLASS(UTextureExporterDDS,UExporter,0,Editor)
	void StaticConstructor();
	UBOOL ExportBinary( UObject* Object, const TCHAR* Type, FArchive& Ar, FFeedbackContext* Warn );
};
// --- sjs

// This is the directoy referenced in the RDF file
#define OUT_TEXTURES_RDF_SRC_PF TEXT("\tSource .\\Source\\%s.%s\r\n")
// Used to generate the file name for binary export
#define OUT_TEXTURES_EXPORT_PF TEXT("..\\XboxTextures\\Source\\%s.%s")
// For exporting cubemap faces
#define OUT_CUBEMAP_RDF_XP_PF TEXT("\tSourceXP .\\Source\\%s.%s\r\n")
#define OUT_CUBEMAP_RDF_XN_PF TEXT("\tSourceXN .\\Source\\%s.%s\r\n")
#define OUT_CUBEMAP_RDF_YP_PF TEXT("\tSourceYP .\\Source\\%s.%s\r\n")
#define OUT_CUBEMAP_RDF_YN_PF TEXT("\tSourceYN .\\Source\\%s.%s\r\n")
#define OUT_CUBEMAP_RDF_ZP_PF TEXT("\tSourceZP .\\Source\\%s.%s\r\n")
#define OUT_CUBEMAP_RDF_ZN_PF TEXT("\tSourceZN .\\Source\\%s.%s\r\n")

/**
 * This class exports the text information needed to build a RDF file which
 * compiles a set of textures into a bundle that can be loaded efficiently.
 */
class EDITOR_API UMaterialExporterRDF : public UExporter
{
	DECLARE_CLASS(UMaterialExporterRDF,UExporter,0,Editor)

protected:
	/** The list of textures to ignore because they've been exported */
	TArray<FName> IgnoreList;
	/** The list of textures that was processed by the export */
	TArray<FName> ProcessedList;

	/**
	 * Returns the file extension for the texture format type
	 *
	 * @param Format The texture format to find the extension for
	 *
	 * @return The file extension to use for exporting
	 */
	const TCHAR* ExtensionFromFormat(ETextureFormat Format);

	/**
	 * Returns a string indicating the D3D format
	 *
	 * @param Format The texture format to find the extension for
	 *
	 * @return The desired D3D format
	 */
	const TCHAR* GetD3DFormat(ETextureFormat Format);

	/**
	 * Exports the specified texure to the archive
	 *
	 * @param pTexture the texture to export
	 * @param Ar the output device to write to
	 */
	void AddTexture(UTexture* pTexture,FOutputDevice& Ar);

	/**
	 * Exports the specified cubemap to the archive
	 *
	 * @param pCubemap the cubemap to export
	 * @param Ar the output device to write to
	 */
	void AddCubemap(UCubemap* pCubemap,FOutputDevice& Ar);

public:
	/**
	 * Registers the supported export type
	 */
	void StaticConstructor(void);

	/**
	 * Exports the text information for the texture header to a archive
	 *
	 * @param Object the object to export
	 & @param Type ignored
	 * @param Ar The archive to output the text to
	 * @param Warn the device to write warnings to
	 *
	 * @return True if sucessful false otherwise
	 */
	UBOOL ExportText(UObject* Object,const TCHAR* Type,FOutputDevice& Ar,
		FFeedbackContext* Warn );

	/**
	 * Sets the list of textures to ignore
	 *
	 * @param InIgnoreList The list of textures to skip exporting on
	 */
	void SetIgnoreList(const TArray<FName>& InIgnoreList);

	/**
	 * Returns the set of materials that were processed
	 *
	 * @param CopyTo The list of textures that were just processed
	 */
	void GetProcessedList(TArray<FName>& CopyTo);
};

#define OUT_ARRAY_EXPORT_RDF_PF TEXT(".\\Source\\%s.uma")
#define OUT_ARRAY_EXPORT_PF TEXT("..\\XboxTextures\\Source\\%s.uma")

//#include "UnLinker.h"

/**
 * This exporter knows how to export the various arrays contained within a
 * static mesh and skeletal mesh as bulk data that is loaded into contiguous
 * memory
 */
class EDITOR_API UBulkDataExporterRDF : public UExporter
{
	DECLARE_CLASS(UBulkDataExporterRDF,UExporter,0,Editor);

protected:
	/**
	 * The list of static meshes/skeletal meshes to ignore because
	 * they've been exported already
	 */
	TArray<FName> IgnoreList;
	/**
	 * The list of static meshes/skeletal meshes that was processed
	 * by the export
	 */
	TArray<FName> ProcessedList;
	/**
	 * The export map used to export each object in a given maps order
	 */
	TArray<FObjectExport>* ExportMap;
	/**
	 * Used for hand exporting animation sets
	 */
	UMeshAnimation* AnimSet;
	/**
	 * Whether to ignore all animation sets during export or not
	 */
	UBOOL bIgnoreAnimSets;
	/**
	 * List of objects to always export
	 */
	TArray<UObject*> AlwaysExport;
	/**
	 * Whether to ignore all skelet meshes during export or not
	 */
	UBOOL bIgnoreSkelMesh;
	/**
	 * Used for hand exporting skeletal meshes
	 */
	USkeletalMesh* SkeletalMesh;

	/**
	 * Generic routine for dumping the text information of the array to the file
	 *
	 * @param ArrayName the name of the array being added
	 * @param ArrayCount the number of items in the array
	 * @param ElementSize the size in bytes of the contained entity
	 * @param Data pointer to the data for exporting
	 * @param Ar the archive to write to
	 */
	void AddNamedArray(const FString& ArrayName,DWORD ArrayCount,DWORD ElementSize,
		void* Data,FOutputDevice& Ar);

	/**
	 * Creates the file and fills it with the passed in data
	 *
	 * @param FileName the name of the file to create
	 * @param NumBytes the number of bytes to write out
	 * @param Data the data to write to the file
	 */
	UBOOL ExportBinaryArray(const TCHAR* FileName,DWORD NumBytes,void* Data);

	/**
	 * Adds a static mesh to the RDF file and gets it all of its arrays
	 * exported
	 *
	 * @param StaticMesh the static mesh to add
	 * @param Ar the archive we are writing to
	 */
	void AddStaticMesh(UStaticMesh* StaticMesh,FOutputDevice& Ar);

	/**
	 * Adds a static mesh instance to the RDF file
	 *
	 * @param StaticMeshInst the static mesh instance to add
	 * @param Ar the archive we are writing to
	 */
	void AddStaticMeshInstance(UStaticMeshInstance* StaticMeshInst,
		FOutputDevice& Ar);

	/**
	 * Adds a skeletal mesh to the RDF file and gets it all of its sections
	 * exported
	 *
	 * @param SkeletalMesh the skeletal mesh to add
	 * @param Ar the archive we are writing to
	 */
	void AddSkeletalMesh(USkeletalMesh* SkeletalMesh,FOutputDevice& Ar);

	/**
	 * Adds a UModel's BSP vertex data to the RDF file
	 *
	 * @param Model the model to export BSP verts on
	 * @param Ar the archive we are writing to
	 */
	void AddModel(UModel* Model,FOutputDevice& Ar);

	/**
	 * Adds a static projector primitive to the RDF file and gets all of its
	 * arrays exported
	 *
	 * @param Prim the static projector to export
	 * @param Ar the archive we are writing to
	 */
	void AddProjectorPrimitive(UStaticProjectorClippedPrim* Prim,
		FOutputDevice& Ar);

	/**
	 * Adds an animation set to the RDF file
	 *
	 * @param Anim the animation set to export
	 * @param Ar the archive we are writing to
	 */
	void AddMeshAnimation(UMeshAnimation* Anim,FOutputDevice& Ar);

	/**
	 * Exports all of the objects in the export map in dependency order
	 *
	 * @param Ar the archive to write text information to
	 */
	void ExportViaExportMap(FOutputDevice& Ar);

	/**
	 * Exports all of the objects in the export map in dependency order
	 *
	 * @param Ar the archive to write text information to
	 */
	void ExportViaIterator(FOutputDevice& Ar);

public:
	/**
	 * Registers the supported export type
	 */
	void StaticConstructor();

	/**
	 * Exports the text information for the various arrays to a archive
	 *
	 * @param Object the object to export
	 * @param Type ignored
	 * @param Ar The archive to output the text to
	 * @param Warn the device to write warnings to
	 *
	 * @return True if sucessful false otherwise
	 */
	UBOOL ExportText(UObject* Object,const TCHAR* Type,FOutputDevice& Ar,
		FFeedbackContext* Warn );

	/**
	 * Sets the export map that will be used to export objects in dependency
	 * order
	 */
	FORCEINLINE void SetExportMap(TArray<FObjectExport>* Map)
	{
		ExportMap = Map;
	}

	/**
	 * Sets the animation set that we want exported
	 */
	FORCEINLINE void SetAnimSet(UMeshAnimation* Anim)
	{
		AnimSet = Anim;
	}

	/**
	 * Sets the skeletal mesh that we are exporting
	 */
	FORCEINLINE void SetSkeletalMesh(USkeletalMesh* Skel)
	{
		SkeletalMesh = Skel;
	}

	/**
	 * Sets whether we should ignore animations or not
	 */
	FORCEINLINE void IgnoreAnimSets(void)
	{
		bIgnoreAnimSets = TRUE;
	}

	/**
	 * Sets the list of textures to ignore
	 *
	 * @param InIgnoreList The list of textures to skip exporting on
	 */
	void SetIgnoreList(const TArray<FName>& InIgnoreList)
	{
		guard(SetIgnoreList);
		// Copy the passed in ones
		IgnoreList = InIgnoreList;
		unguard;
	}

	/**
	 * Returns the set of materials that were processed
	 *
	 * @param CopyTo The list of textures that were just processed
	 */
	void GetProcessedList(TArray<FName>& CopyTo)
	{
		guard(SetIgnoreList);
		// Copy our list into this one
		CopyTo = ProcessedList;
		unguard;
	}

	/**
	 * Sets the list of objects that we want to guarantee export on
	 */
	void SetAlwaysExport(TArray<UObject*> ForceExport)
	{
		guard(SetAlwaysExport);
		AlwaysExport = ForceExport;
		unguard;
	}
};

#define OUT_PB_EXPORT_RDF_PF TEXT(".\\Source\\%s.pb")
#define OUT_PB_EXPORT_PF TEXT("..\\XboxTextures\\Source\\%s.pb")

/**
 * This exporter knows how to export the various arrays contained within a
 * static mesh and skeletal mesh as bulk data that is loaded into contiguous
 * memory
 */
class EDITOR_API UPushbufferExporterRDF : public UExporter
{
	DECLARE_CLASS(UPushbufferExporterRDF,UExporter,0,Editor);

protected:
	/**
	 * The list of static meshes/skeletal meshes to ignore because
	 * they've been exported already
	 */
	TArray<FName> IgnoreList;
	/**
	 * The list of static meshes/skeletal meshes that was processed
	 * by the export
	 */
	TArray<FName> ProcessedList;
	/**
	 * The export map used to export each object in a given maps order
	 */
	TArray<FObjectExport>* ExportMap;
	/**
	 * List of objects to always export
	 */
	TArray<UObject*> AlwaysExport;
	/**
	 * Used for hand exporting skeletal meshes
	 */
	USkeletalMesh* SkeletalMesh;

	/**
	 * Saves the pushbuffer to a file referenced by the RDF section
	 *
	 * @param FileName the name of the file to write this to
	 * @param NumBytes the number of bytes to write to the file
	 * @param Data the raw pushbuffer data to write to the file
	 */
	UBOOL ExportPushbuffer(const TCHAR* FileName,DWORD NumBytes,void* Data);

	/**
	 * Common method of dumping the pushbuffer meta info to the RDF file
	 *
	 * @param PBName the name of the array being added
	 * @param PushBuffer the PushBuffer to write otu
	 * @param ElementSize the size in bytes of the contained entity
	 * @param Data pointer to the data for exporting
	 * @param Ar the archive to write to
	 */
	void AddNamedPB(const FString& PBName,FPushBuffer* PushBuffer,FOutputDevice& Ar);

	/**
	 * Adds a static mesh to the RDF file
	 *
	 * @param StaticMesh the static mesh to add
	 * @param Ar the archive we are writing to
	 */
	void AddStaticMesh(UStaticMesh* StaticMesh,FOutputDevice& Ar);

	/**
	 * Adds a skeletal mesh to the RDF file
	 *
	 * @param SkeletalMesh the skeletal mesh to add
	 * @param Ar the archive we are writing to
	 */
	void AddSkeletalMesh(USkeletalMesh* SkeletalMesh,FOutputDevice& Ar);

	/**
	 * Adds a static projector primitive to the RDF file
	 *
	 * @param Prim the static projector to export
	 * @param Ar the archive we are writing to
	 */
	void AddProjectorPrimitive(UStaticProjectorClippedPrim* Prim,
		FOutputDevice& Ar);

	/**
	 * Exports all of the objects in the export map in dependency order
	 *
	 * @param Ar the archive to write text information to
	 */
	void ExportViaExportMap(FOutputDevice& Ar);

	/**
	 * Exports all of the objects in the export map in dependency order
	 *
	 * @param Ar the archive to write text information to
	 */
	void ExportViaIterator(FOutputDevice& Ar);

public:
	/**
	 * Registers the supported export type
	 */
	void StaticConstructor();

	/**
	 * Exports the text information for the various arrays to a archive
	 *
	 * @param Object the object to export
	 * @param Type ignored
	 * @param Ar The archive to output the text to
	 * @param Warn the device to write warnings to
	 *
	 * @return True if sucessful false otherwise
	 */
	UBOOL ExportText(UObject* Object,const TCHAR* Type,FOutputDevice& Ar,
		FFeedbackContext* Warn );

	/**
	 * Sets the export map that will be used to export objects in dependency
	 * order
	 */
	FORCEINLINE void SetExportMap(TArray<FObjectExport>* Map)
	{
		ExportMap = Map;
	}

	/**
	 * Sets the list of textures to ignore
	 *
	 * @param InIgnoreList The list of textures to skip exporting on
	 */
	void SetIgnoreList(const TArray<FName>& InIgnoreList)
	{
		guard(SetIgnoreList);
		// Copy the passed in ones
		IgnoreList = InIgnoreList;
		unguard;
	}

	/**
	 * Returns the set of materials that were processed
	 *
	 * @param CopyTo The list of textures that were just processed
	 */
	void GetProcessedList(TArray<FName>& CopyTo)
	{
		guard(SetIgnoreList);
		// Copy our list into this one
		CopyTo = ProcessedList;
		unguard;
	}

	/**
	 * Sets the list of objects that we want to guarantee export on
	 */
	void SetAlwaysExport(TArray<UObject*> ForceExport)
	{
		guard(SetAlwaysExport);
		AlwaysExport = ForceExport;
		unguard;
	}

	/**
	 * Sets the skeletal mesh that we are exporting
	 */
	FORCEINLINE void SetSkeletalMesh(USkeletalMesh* Skel)
	{
		SkeletalMesh = Skel;
	}
};
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
#endif
