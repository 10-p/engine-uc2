/*=============================================================================
	UC2FrameTrace.cpp: a per-frame trace of what the first viewport draws around
	the player's own pawn. Quiet unless asked for.

	For every drawn frame the trace says where the camera of that frame was, where the
	pawn was drawn and where on the screen that is, so a pawn that lags or snaps in front
	of the third-person camera shows up as numbers.

	Console command (also the SDL2 test channel's "exec ...", the web's ue_exec_command):
	  FRAMETRACE <frames> [tag]   one "FrameTrace:" line for each of the next <frames>
	                              frames the first viewport draws with a pawn to view
	                              (0 stops it)

	A line (all positions world units, rotations Unreal units, screen in viewport pixels):
	  frametrace <tag> <i> t=<Level.TimeSeconds> dt=<the frame's DeltaSeconds> cam=(x,y,z) camrot=(p,y,r)
	  fov=<the FOV drawn with, AutoFOV included> view=<W>x<H> pawn=(x,y,z) vel=(x,y,z)
	  bob=(x,y,z) root=(x,y,z) scr=(x,y) rscr=(x,y) phys=<n> look=<mouse|pad|->
	  mouse=(dx,dy,n) wall=<appSeconds> lag=<z>
	pawn: the view target's Location; bob: its WalkBob; root: the skeletal mesh's root bone as
	posed for this frame (world space; = pawn when the mesh is not skeletal); scr / rscr: pawn /
	root projected with this frame's camera as FCameraSceneNode does; look: which device owns the
	look (UCPlayerInput.bMouseOwnsLook), "-" without one; mouse: the viewport's relative mouse motion
	since the frame before (counts, and how many motion events brought them); wall: the time the line
	was written; lag: how far the mesh is drawn from its resting place, vertically (PrePivot -
	DesiredPrePivot, the step smoothing of a walking pawn).
=============================================================================*/

#include "EnginePrivate.h"

static FName FrameTraceLogName()
{
	// RF_Native keeps a run-time name out of the purge of unreferenced names (UnObj.cpp
	// RemoveNames), as for UC2NetTest.cpp's NetProbe.
	static FName Name;
	if( Name==NAME_None )
	{
		Name = FName( TEXT("FrameTrace") );
		Name.SetFlags( RF_Native );
	}
	return Name;
}
#define FRAMETRACE ((EName)FrameTraceLogName().GetIndex())

static INT		GFrameTraceLeft  = 0;
static INT		GFrameTraceIndex = 0;
static FString	GFrameTraceTag;
static INT		GFrameTraceMouseX = 0;
static INT		GFrameTraceMouseY = 0;
static INT		GFrameTraceMouseEvents = 0;

//
// One relative mouse motion as the viewport received it (USDL2Viewport, SDL_MOUSEMOTION).
//
ENGINE_API void UC2FrameTraceMouse( INT DX, INT DY )
{
	GFrameTraceMouseX += DX;
	GFrameTraceMouseY += DY;
	GFrameTraceMouseEvents++;
}

ENGINE_API UBOOL UC2FrameTraceExec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UC2FrameTraceExec);
	if( !ParseCommand(&Cmd,TEXT("FRAMETRACE")) )
		return 0;
	FString Frames, Tag;
	ParseToken( Cmd, Frames, 0 );
	if( !ParseToken( Cmd, Tag, 0 ) )
		Tag = TEXT("f");
	GFrameTraceLeft  = Max( 0, appAtoi(*Frames) );
	GFrameTraceIndex = 0;
	GFrameTraceTag   = Tag;
	debugf( FRAMETRACE, TEXT("frametrace %s armed for %i frames"), *GFrameTraceTag, GFrameTraceLeft );
	return 1;
	unguard;
}

//
// A world point in the viewport's pixels, as FCameraSceneNode::UpdateMatrices projects it for a
// player viewport (ScaleX = ScaleY = 1; ScaleFOVX/Y are 1 outside split screen).
//
static UBOOL ProjectToViewport( UViewport* Viewport, const FVector& CamLoc, const FRotator& CamRot, FLOAT Fov, const FVector& P, FLOAT& OutX, FLOAT& OutY )
{
	FCoords	C		= GMath.UnitCoords / CamRot;
	FVector	D		= P - CamLoc;
	FLOAT	Fwd		= D | C.XAxis;
	FLOAT	Right	= D | C.YAxis;
	FLOAT	Up		= D | C.ZAxis;
	OutX = OutY = -1.f;
	if( Fwd < 1.f || Viewport->SizeX <= 0 || Viewport->SizeY <= 0 )
		return 0;
	FLOAT	SX		= Viewport->ScaleFOVX > 0.f ? Viewport->ScaleFOVX : 1.f;
	FLOAT	SY		= Viewport->ScaleFOVY > 0.f ? Viewport->ScaleFOVY : 1.f;
	FLOAT	TanX	= appTan( Fov * PI / 360.f * SX );
	FLOAT	TanY	= appTan( Fov * PI / 360.f * SY );
	FLOAT	Aspect	= (FLOAT)Viewport->SizeX / (FLOAT)Viewport->SizeY;
	OutX = 0.5f * Viewport->SizeX * (1.f + Right / (Fwd * TanX));
	OutY = 0.5f * Viewport->SizeY * (1.f - Up * Aspect / (Fwd * TanY));
	return 1;
}

static const TCHAR* LookOwner( APlayerController* PC )
{
	UObject* Input = PC ? (UObject*)PC->PlayerInput : NULL;
	UBoolProperty* P = Input ? FindField<UBoolProperty>( Input->GetClass(), TEXT("bMouseOwnsLook") ) : NULL;
	if( !P )
		return TEXT("-");
	return (*(BITFIELD*)((BYTE*)Input + P->Offset) & P->BitMask) ? TEXT("mouse") : TEXT("pad");
}

//
// Called by UGameEngine::Draw after the frame was drawn, with the camera it was drawn with (the
// AutoFOV scope still applied: FovAngle is the FOV of the picture).
//
ENGINE_API void UC2FrameTraceDraw( UViewport* Viewport, const FVector& CamLoc, const FRotator& CamRot, FLOAT DeltaSeconds )
{
	guard(UC2FrameTraceDraw);
	const INT MouseX = GFrameTraceMouseX, MouseY = GFrameTraceMouseY, MouseEvents = GFrameTraceMouseEvents;
	GFrameTraceMouseX = GFrameTraceMouseY = GFrameTraceMouseEvents = 0;
	if( GFrameTraceLeft <= 0 || !Viewport || !Viewport->Actor )
		return;
	APlayerController* PC = Viewport->Actor;
	APawn* Pawn = PC->ViewTarget && PC->ViewTarget->IsAPawn() ? (APawn*)PC->ViewTarget : PC->Pawn;
	if( !Pawn || Pawn->bDeleteMe )
		return;
	GFrameTraceLeft--;

	FLOAT Fov = PC->FovAngle;
	FVector Root = Pawn->Location;
	if( Pawn->Mesh && Pawn->Mesh->IsA(USkeletalMesh::StaticClass()) )
	{
		USkeletalMeshInstance* Inst = (USkeletalMeshInstance*)Pawn->Mesh->MeshGetInstance( Pawn );
		if( Inst )
			Root = Inst->GetBoneCoords( 0 ).Origin;
	}
	FLOAT SX, SY, RX, RY;
	ProjectToViewport( Viewport, CamLoc, CamRot, Fov, Pawn->Location, SX, SY );
	ProjectToViewport( Viewport, CamLoc, CamRot, Fov, Root, RX, RY );
	debugf( FRAMETRACE,
		TEXT("frametrace %s %i t=%.5f dt=%.5f cam=(%.2f,%.2f,%.2f) camrot=(%i,%i,%i) fov=%.3f view=%ix%i pawn=(%.2f,%.2f,%.2f) vel=(%.1f,%.1f,%.1f) bob=(%.2f,%.2f,%.2f) root=(%.2f,%.2f,%.2f) scr=(%.2f,%.2f) rscr=(%.2f,%.2f) phys=%i look=%s mouse=(%i,%i,%i) wall=%.5f lag=%.3f"),
		*GFrameTraceTag, GFrameTraceIndex++,
		PC->Level ? PC->Level->TimeSeconds : 0.f, DeltaSeconds,
		CamLoc.X, CamLoc.Y, CamLoc.Z, CamRot.Pitch, CamRot.Yaw, CamRot.Roll,
		Fov, Viewport->SizeX, Viewport->SizeY,
		Pawn->Location.X, Pawn->Location.Y, Pawn->Location.Z,
		Pawn->Velocity.X, Pawn->Velocity.Y, Pawn->Velocity.Z,
		Pawn->WalkBob.X, Pawn->WalkBob.Y, Pawn->WalkBob.Z,
		Root.X, Root.Y, Root.Z,
		SX, SY, RX, RY,
		(INT)Pawn->Physics,
		LookOwner( PC ),
		MouseX, MouseY, MouseEvents, appSeconds(),
		Pawn->PrePivot.Z - Pawn->DesiredPrePivot.Z );
	if( GFrameTraceLeft == 0 )
		debugf( FRAMETRACE, TEXT("frametrace %s done after %i frames"), *GFrameTraceTag, GFrameTraceIndex );
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
