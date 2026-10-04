/*=============================================================================
	Engine.h: Unreal engine public header file.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

#ifndef __ENGINE_H__
#define __ENGINE_H__

/*----------------------------------------------------------------------------
	API.
----------------------------------------------------------------------------*/

#ifndef ENGINE_API
	#define ENGINE_API DLL_IMPORT
#endif

/*-----------------------------------------------------------------------------
	Dependencies.
-----------------------------------------------------------------------------*/

#ifdef _XBOX
	#include <xtl.h>
	#include <xonline.h>
#endif

#include "Core.h"

/*-----------------------------------------------------------------------------
	Global variables.
-----------------------------------------------------------------------------*/

ENGINE_API extern class FMemStack			GEngineMem;
ENGINE_API extern class FMemCache			GCache;
#ifndef FINAL_RELEASE
ENGINE_API extern class FStats				GStats;
ENGINE_API extern class FEngineStats		GEngineStats;
ENGINE_API extern class FStatGraph*			GStatGraph;
#endif
ENGINE_API extern class FTempLineBatcher*	GTempLineBatcher;
ENGINE_API extern class FMatineeTools		GMatineeTools;
ENGINE_API extern class UGlobalTempObjects* GGlobalTempObjects;

#define DECLARE_STATIC_UOBJECT( ObjectClass, ObjectName, ExtraJunk ) \
			static ObjectClass* ObjectName = NULL;	\
			if( !ObjectName )	\
			{ \
				ObjectName = ConstructObject<ObjectClass>(ObjectClass::StaticClass()); \
				GGlobalTempObjects->AddGlobalObject((UObject**)&ObjectName); \
				ExtraJunk; \
			}

// scion sz -
// because I like static uobjects SO much
#define DECLARE_STATIC_UOBJECT_ARRAY( ObjectClass, ArrayName, ArrayNum, ExtraJunk )	\
			static ObjectClass* ArrayName[ArrayNum];	\
			static UBOOL ArrayName##INIT=0;				\
			if( !ArrayName##INIT || (ArrayNum && !ArrayName[0]) ) {					\
				for( INT i=0; i<ArrayNum; i++ ) {		\
					ArrayName[i] = ConstructObject<ObjectClass>(ObjectClass::StaticClass());	\
					GGlobalTempObjects->AddGlobalObject((UObject**)&ArrayName[i]);	\
				} \
				ArrayName##INIT=1;	\
				ExtraJunk;	\
			}

/*-----------------------------------------------------------------------------
	Engine compiler specific includes.
-----------------------------------------------------------------------------*/

#if __GNUG__ || __MWERKS__ || __LINUX__
	#include "UnEngineGnuG.h"
#endif

/*-----------------------------------------------------------------------------
	Replication specific defines
-----------------------------------------------------------------------------*/
//epic superville: Retrive pointer of type c with name v from the RecentMap
// c -> Class for retrieving the offset for the property
// v -> Name of the variable to retrieve
// r -> Type to cast the return pointer to
// p -> Variable to assign to

#define GETPROP(obj,var,type,targ) \
	static UProperty *sp##var = FindObjectChecked<UProperty>(A##obj::StaticClass(),TEXT(#var));\
	type* targ = (type*)Channel->GetMapValueAddr(sp##var->Offset, bNetTemporary);\
	if( !targ )\
	{\
		targ = (type*)(GetClass()->GetDefaultsSafe(sp##var->Offset));\
	}\

// check if using a bool inside DOREP, which breaks replication
#ifdef _DEBUG
	#define CheckNotBool(var) \
		if (sp##var->GetClass()->ClassFlags & CLASS_IsAUBoolProperty)\
		{\
			debugf(NAME_Warning, TEXT("Replicating bool %s using DOREP!"), sp##var->GetName());\
		}
#else
#if defined(_MSC_VER)
	#define CheckNotBool __noop
#else
	#define CheckNotBool(...) ((void)0) /* __noop is an MSVC intrinsic: the argument is not evaluated */
#endif
#endif

#define DOREP(obj,var,type) \
	{\
		guard(DOREP::obj::var);\
		GETPROP(obj,var,type,targ);\
		CheckNotBool(var);\
		if( NEQ(var,*targ,Map,Channel) )\
		{\
 			*Ptr++ = sp##var->RepIndex;\
		}\
		unguard;\
	}

#define DOREPBOOL(obj,var)\
	{\
		guard(DOREPBOOL::obj::var);\
		GETPROP(obj,var,BITFIELD,targ);\
		checkSlow(sp##var->GetClass()->ClassFlags & CLASS_IsAUBoolProperty);\
		if (NEQ(var, (*((BITFIELD*)targ) & ((UBoolProperty*)sp##var)->BitMask) ? BITFIELD(1) : BITFIELD(0), Map, Channel))\
		{\
			*Ptr++ = sp##var->RepIndex;\
		}\
		unguard;\
	}

#define DOREPARRAY(obj,var,type) \
	{\
		guard(DOREPARRAY::obj::var);\
		GETPROP(obj,var,type,targ);\
		for( INT i = 0; i < ARRAY_COUNT(var); i++ )\
		{\
			if( NEQ(var[i],targ[i],Map,Channel) )\
			{\
				*Ptr++ = sp##var->RepIndex + i;\
			}\
		}\
		unguard;\
	}

/*-----------------------------------------------------------------------------
	Size of the world.
-----------------------------------------------------------------------------*/

#define WORLD_MAX		524288.0	/* Maximum size of the world */
#define HALF_WORLD_MAX	262144.0	/* Half the maximum size of the world */
#define HALF_WORLD_MAX1	262143.0	/* Half the maximum size of the world - 1*/
#define MIN_ORTHOZOOM	250.0		/* Limit of 2D viewport zoom in */
#define MAX_ORTHOZOOM	16000000.0	/* Limit of 2D viewport zoom out */

// Karma Includes
#if 0//jg -- Removed
#ifndef WITH_KARMA
#  define WITH_KARMA
#endif
#endif

#ifdef WITH_KARMA
#if SUPPORTS_PRAGMA_PACK
#pragma pack (push,8)
#endif
    #include "MePrecision.h"
    #include "Mst.h"
    #include "McdTriangleList.h"
    #include "MeAssetDB.h"
    #include "MeAssetFactory.h"
#if SUPPORTS_PRAGMA_PACK
#pragma pack (pop)
#endif

    // These need to be public for UnrealEd etc.
    const MeReal K_ME2UScale = (MeReal)50;
    const MeReal K_U2MEScale = (MeReal)0.02;

	const MeReal K_Rad2U = (MeReal)10430.2192;
	const MeReal K_U2Rad = (MeReal)0.000095875262;
#endif

// LIPSinc Includes
//jg -- We've decided not to use this
#if 0
	#ifndef WITH_LIPSINC
		#define WITH_LIPSINC
	#endif
#endif

/*-----------------------------------------------------------------------------
	Editor callback codes.
-----------------------------------------------------------------------------*/
enum EUnrealEdCallbacks
{
	EDC_None						= 0,
	EDC_Browse						= 1,
	EDC_UseCurrent					= 2,
	EDC_CurTexChange				= 10,
	EDC_CurStaticMeshChange			= 11,
	EDC_SelPolyChange				= 20,
	EDC_SelChange					= 21,
	EDC_RtClickTexture				= 23,
	EDC_RtClickStaticMesh			= 24,
	EDC_RtClickPoly					= 26,
	EDC_RtClickActor				= 27,
	EDC_RtClickWindow				= 28,
	EDC_RtClickWindowCanAdd			= 29,
	EDC_MapChange					= 42,
	EDC_ViewportUpdateWindowFrame	= 43,
	EDC_SurfProps					= 44,
	EDC_SaveMap						= 45,
	EDC_SaveMapAs					= 46,
	EDC_LoadMap						= 47,
	EDC_PlayMap						= 48,
	EDC_CamModeChange				= 49,
	EDC_RedrawAllViewports			= 50,
	EDC_ViewportsDisableRealtime	= 51,
	EDC_ActorPropertiesChange		= 52,
	EDC_RtClickTerrainLayer			= 53,
	EDC_RtClickActorStaticMesh		= 54,
	EDC_RefreshEditor				= 55,
	EDC_DisplayLoadErrors			= 56,
	EDC_RtClickMatScene				= 57,
	EDC_RtClickAnimSeq              = 58,
	EDC_RedrawCurrentViewport		= 59,
	EDC_MaterialTreeClick			= 60,
	EDC_RtClickMatAction			= 61,

#ifdef WITH_LIPSINC
	EDC_LIPSincAnimListRtClick		= 62,
#endif
};

// Used with the EDC_RefreshEditor callback to selectively refresh parts of the editor.
enum ERefreshEditor
{
	ERefreshEditor_Misc					= 1,	// All other things besides browsers
	ERefreshEditor_ActorBrowser			= 2,
	ERefreshEditor_GroupBrowser			= 4,
	ERefreshEditor_MeshBrowser			= 8,
	ERefreshEditor_AnimationBrowser		= 16,
	ERefreshEditor_MusicBrowser			= 32,
	ERefreshEditor_PrefabBrowser		= 64,
	ERefreshEditor_SoundBrowser			= 128,
	ERefreshEditor_StaticMeshBrowser	= 256,
	ERefreshEditor_TextureBrowser		= 512,
	ERefreshEditor_Matinee				= 1024,
	ERefreshEditor_Terrain				= 2048,

#ifdef WITH_LIPSINC
	ERefreshEditor_LIPSincBrowser		= 4096,
#endif
};

#ifdef WITH_LIPSINC
	#define ERefreshEditor_AllBrowsers		ERefreshEditor_GroupBrowser | ERefreshEditor_MeshBrowser | ERefreshEditor_AnimationBrowser | ERefreshEditor_MusicBrowser | ERefreshEditor_PrefabBrowser | ERefreshEditor_SoundBrowser | ERefreshEditor_StaticMeshBrowser | ERefreshEditor_LIPSincBrowser
#else
	#define ERefreshEditor_AllBrowsers		ERefreshEditor_GroupBrowser | ERefreshEditor_MeshBrowser | ERefreshEditor_AnimationBrowser | ERefreshEditor_MusicBrowser | ERefreshEditor_PrefabBrowser | ERefreshEditor_SoundBrowser | ERefreshEditor_StaticMeshBrowser
#endif


// Camera orientations for Matinee (this is mirrored in Object.uc - keep all definitions in sync!)
enum ECamOrientation
{
	CAMORIENT_None = 0,
	CAMORIENT_LookAtActor = 1,
	CAMORIENT_FacePath = 2,
	CAMORIENT_Interpolate = 3,
	CAMORIENT_Dolly = 4,
	CAMORIENT_MAX = 5,
};

// Struct for storing Matinee camera orientations (this is mirrored in MatObject.uc and SceneManager.uc - keep all definitions in sync!)
class UMatSubAction;
struct ENGINE_API FOrientation
{
	// Exposed in editor

	INT	CamOrientation;
	AActor* LookAt;
	FLOAT EaseInTime;
	INT bReversePitch;
	INT bReverseYaw;
	INT bReverseRoll;
	//scion jg -- Allow for an offset from the LookAt actor's location
	FVector LookAtOffset;
	//epic superville: Give the option to find actor by tag
	BITFIELD bLookAtByTag:1 GCC_PACK(4);
	FName	LookAtTag;

	// Work varibles
	UMatSubAction* MA;		// Used entirely for comparison purposes (this is the subaction this orientation belongs to)
	FLOAT PctInStart, PctInEnd, PctInDuration;
	FRotator StartingRotation;		// The original rotation we were at when the change started

	FOrientation()
		//epic superville: commented out because it interfered with struct import from script files
		: /*CamOrientation(CAMORIENT_FacePath),*/ LookAt(NULL), EaseInTime(0), MA(NULL), bReverseYaw(0), bReversePitch(0), bReverseRoll(0)
	{}

	FOrientation& operator=( const FOrientation Other )
	{
		CamOrientation = Other.CamOrientation;
		LookAt = Other.LookAt;
		
		//scion jg -- Handle look at offset
		LookAtOffset = Other.LookAtOffset;
		
		//epic superville: Handle look at by tag
		bLookAtByTag		= Other.bLookAtByTag;
		LookAtTag			= Other.LookAtTag;

		EaseInTime = Other.EaseInTime;
		StartingRotation = Other.StartingRotation;
		PctInStart = Other.PctInStart;
		PctInEnd = Other.PctInEnd;
		PctInDuration = Other.PctInDuration;
		MA = Other.MA;
		bReverseYaw = Other.bReverseYaw;
		bReversePitch = Other.bReversePitch;
		bReverseRoll = Other.bReverseRoll;

		return *this;
	}
	UBOOL operator!=( const FOrientation& Other ) const
	{
		return (MA != Other.MA);
	}
};

// Ease In and Ease Out Interpolation is done by calculating the
// acceleration which will cover exactly half the distance in half
// the specified time. That acceleration is applied for the first
// half, then the inverse is applied for the second half to slow
// down so the data point stops as it reaches the target point.

class FInterpolator
{
public:
	FInterpolator()
	{
		bDone = 1;
		_value = 1.f;
	}
	~FInterpolator()
	{}
	UBOOL Start( FLOAT InTime )
	{
		bDone = 0;
		if(InTime <= 0)
		{
			bDone = 1;
			return 0;
		}
			_value = 0.f;
		_speed = 0.0f;
		_acceleration = 1.f/(InTime*InTime/4.f);
		_remainingTime = _totalTime = InTime;
		return true;
	}
	void Tick(float deltaTime)
	{
		if( IsDone() )	return;

		_remainingTime -= deltaTime;
		if(_remainingTime < _totalTime/2.f)
		{
			// Deceleration
			_speed -= _acceleration * deltaTime;
		}
		else
		{
			// Acceleration
			_speed += _acceleration * deltaTime;
		}
		_value += _speed*deltaTime;
		bDone = (_remainingTime < 0.f);
	}
	float GetValue()
	{
		return _value;
	}
	UBOOL IsDone() const
	{
		return bDone;
	}

	UBOOL bDone;
	float _value;
	float _remainingTime;
	float _totalTime;
	float _speed;
	float _acceleration;
};

/*-----------------------------------------------------------------------------
	FRotatorF.
-----------------------------------------------------------------------------*/

// A floating point version of FRotator.  Used by Matinee so precision doesn't get lost in camera rotation calcs.
class ENGINE_API FRotatorF
{
public:
	// Variables.
	FLOAT Pitch; // Looking up and down (0=Straight Ahead, +Up, -Down).
	FLOAT Yaw;   // Rotating around (running in circles), 0=East, +North, -South.
	FLOAT Roll;  // Rotation about axis of screen, 0=Straight, +Clockwise, -CCW.

	// Serializer.
	friend FArchive& operator<<( FArchive& Ar, FRotatorF& R )
	{
		return Ar << R.Pitch << R.Yaw << R.Roll;
	}

	// Constructors.
	FRotatorF() {}
	FRotatorF( FLOAT InPitch, FLOAT InYaw, FLOAT InRoll )
	:	Pitch(InPitch), Yaw(InYaw), Roll(InRoll) {}
	FRotatorF( FRotator InRotator )
		:	Pitch(InRotator.Pitch), Yaw(InRotator.Yaw), Roll(InRotator.Roll) {}

	FRotator Rotator()
	{
		return FRotator( (INT)Pitch, (INT)Yaw, (INT)Roll );
	}

	FRotatorF operator*( FLOAT In ) const
	{
		return FRotatorF( Pitch*In, Yaw*In, Roll*In );
	}
	FRotatorF operator+( FRotatorF In ) const
	{
		return FRotatorF( Pitch+In.Pitch, Yaw+In.Yaw, Roll+In.Roll );
	}
	FRotatorF operator-( FRotatorF In ) const
	{
		return FRotatorF( Pitch-In.Pitch, Yaw-In.Yaw, Roll-In.Roll );
	}

	FRotatorF operator*=( FLOAT In )
	{
		Pitch *= In;		Yaw *= In;		Roll *= In;
		return *this;
	}
	FRotatorF operator+=( FRotatorF In )
	{
		Pitch += In.Pitch;		Yaw += In.Yaw;		Roll += In.Roll;
		return *this;
	}
	FRotatorF operator-=( FRotatorF In )
	{
		Pitch -= In.Pitch;		Yaw -= In.Yaw;		Roll -= In.Roll;
		return *this;
	}

	inline FVector Vector()
	{
		return (GMath.UnitCoords / Rotator()).XAxis;
	}
};

typedef class FDynamicActor* FActorRenderDataPtr;
typedef class FDynamicLight* FLightRenderDataPtr;

typedef struct FProjectorRenderInfo* FProjectorRenderInfoPtr;
typedef struct FStaticProjectorInfo* FStaticMeshProjectorRenderInfoPtr;

struct FParticleProjectorInfo
{
	class UBitmapMaterial*	BitmapMaterial;
	FMatrix					Matrix;
	UBOOL					Projected;
	INT						BlendMode;
};

struct FBatchReference
{
	INT	BatchIndex,
		ElementIndex;
};

/*-----------------------------------------------------------------------------
   SCION INCLUDES
-----------------------------------------------------------------------------*/
//scion capps Support for team name display in HUD
struct ENGINE_API FPlayerNameInfo
{
    FStringNoInit mInfo;
    FPlane mColor;
    FLOAT mXPos;
    FLOAT mYPos;
};

/*-----------------------------------------------------------------------------
	Engine public includes.
-----------------------------------------------------------------------------*/

#include "UnRebuildTools.h"		// Tools used by UnrealEd for rebuilding the level.
#include "Bezier.h"				// Class for computing bezier curves
#include "UnStats.h"			// Performance stat gathering.
#include "UnObj.h"				// Standard object definitions.
#include "UnPrim.h"				// Primitive class.
#include "UnConvexVolume.h"		// Convex volume primitive.
#include "UnRenderResource.h"	// Render resource objects.
#include "PushBuffer.h"//jg -- Include our generic pushbuffer definition
#include "UnRenDev.h"			// Rendering interface definition.
#include "UnRenderUtil.h"		// Rendering utilities.
#include "UnMaterial.h"			// Materials
#include "UnPixelShaderMaterial.h"	// Pixel Shader Materials
#include "UnTex.h"				// Texture and palette.
#include "UnModel.h"			// Model class.
#include "UnAnim.h"				// Animation.
#include "UnComponents.h"		// Forward declarations of object components of actors
#include "UnParticleSystem.h"	// Particle System.
#include "UnVolume.h"			// Volume brushes.
#include "UnVoiceChat.h"		// Voice chat declarations.
#include "UnForceFeedbackWaveform.h" // Xbox gamepad vibration support
#include "UnPlayerStats.h"		// Xbox Live player statistics.
#include "EngineClasses.h"		// All actor classes.
#include "UnPhysic.h"			// Physics constants
#include "UnReach.h"			// Reach specs.
#include "UnURL.h"				// Uniform resource locators.
#include "UnLevel.h"			// Level object.
#include "UnIn.h"				// Input system.
#include "UnPlayer.h"			// Player class.
#include "UnEngine.h"			// Unreal engine.
#include "UnGame.h"				// Unreal game engine.
#include "UnCanvas.h"			// Canvas interface.
#include "UnCamera.h"			// Viewport subsystem.
#include "UnCameraEffects.h"	// Camera effects.
#include "UnRender.h"			// High-level rendering definitions.
#include "UnProjector.h"		// Projected textures and Decals.
#include "UnMesh.h"				// Mesh base.
#include "UnLodMesh.h"			// LOD mesh base class.
#include "UnSkeletalMesh.h"		// Skeletal animated mesh.
#include "UnVertMesh.h"			// Vertex animated mesh.
#include "UnActor.h"			// Actor inlines.
#include "UnAudio.h"			// Audio code.
#include "UnTerrain.h"			// Terrain objects.
#include "UnTerrainTools.h"		// Terrain tools.
#include "UnStaticMesh.h"		// Static T&L meshes.
#include "UnMatineeTools.h"		// Matinee tools.
#include "UnFluidSurface.h"		// Fluid Surface
#include "UnStatGraph.h"		// Stat drawing utility.

/*-----------------------------------------------------------------------------
	Hit proxies.
-----------------------------------------------------------------------------*/

// Hit an axis indicator on a gizmo
struct HGizmoAxis : public HHitProxy
{
	DECLARE_HIT_PROXY(HGizmoAxis,HHitProxy)
	AActor* Actor;
	INT Axis;
	HGizmoAxis( AActor* InActor, INT InAxis ) : Actor(InActor), Axis(InAxis) {}
	virtual AActor* GetActor()
	{
		return Actor;
	}

};

// Hit an actor vertex.
struct HActorVertex : public HHitProxy
{
	DECLARE_HIT_PROXY(HActorVertex,HHitProxy)
	AActor* Actor;
	FVector Location;
	HActorVertex( AActor* InActor, FVector InLocation ) : Actor(InActor), Location(InLocation) {}
	virtual AActor* GetActor()
	{
		return Actor;
	}
};

// Hit a bezier control point
struct HBezierControlPoint : public HHitProxy
{
	DECLARE_HIT_PROXY(HBezierControlPoint,HHitProxy)
	UMatAction* MA;
	UBOOL bStart;		// Is this the starting(=0) or ending(=1) control point?
	HBezierControlPoint( UMatAction* InMA, UBOOL InStart ) : MA(InMA), bStart(InStart) {}

	UBOOL operator==( const HBezierControlPoint& BCP ) const
	{
		return (MA==BCP.MA && bStart==BCP.bStart);
	}
};

/*-----------------------------------------------------------------------------
	Terrain editing brush types.
-----------------------------------------------------------------------------*/

enum ETerrainBrush
{
	TB_None				= -1,
	TB_VertexEdit		= 0,	// Select/Drag Vertices on heightmap
	TB_Paint			= 1,	// Paint on selected layer
	TB_Smooth			= 2,	// Does a filter on the selected vertices
	TB_Noise			= 3,	// Adds random noise into the selected vertices
	TB_Flatten			= 4,	// Flattens the selected vertices to the height of the vertex which was initially clicked
	TB_TexturePan		= 5,	// Pans the texture on selected layer
	TB_TextureRotate	= 6,	// Rotates the texture on selected layer
	TB_TextureScale		= 7,	// Scales the texture on selected layer
	TB_Select			= 8,	// Selects areas of the terrain for copying, generating new terrain, etc
	TB_Visibility		= 9,	// Toggles terrain sectors on/off
	TB_Color			= 10,	// Paints color into the RGB channels of layers
	TB_EdgeTurn			= 11,	// Turns edges of terrain triangulation
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
#endif // __ENGINE_H__
