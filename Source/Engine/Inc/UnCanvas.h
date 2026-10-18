/*=============================================================================
	UnCanvas.h: Unreal canvas definition.
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/
#if SUPPORTS_PRAGMA_PACK
#pragma pack (push,4)	// script mirrors are laid out with /Zp4 (as the generated headers); 64-bit pointers sit on 4
#endif

/*
	UCanvas
	A high-level rendering interface used to render objects on the HUD.
*/

class ENGINE_API UCanvas : public UObject
{
	DECLARE_CLASS(UCanvas,UObject,CLASS_Transient,Engine);
	NO_DEFAULT_CONSTRUCTOR(UCanvas);
public:

	// Variables.
	UFont*			Font;
	FLOAT			SpaceX, SpaceY;
	FLOAT			OrgX, OrgY;
	FLOAT			ClipX, ClipY;
	FLOAT			CurX, CurY;
	FLOAT			CurZ;
	BYTE			Style;
	FLOAT			ForcedAlpha;
	FLOAT			CurYL;
	FColor			Color;
	BITFIELD		bCenter:1;
	BITFIELD		bNoSmooth:1;
	INT				SizeX, SizeY;
	UFont			*TinyFont, *SmallFont, *MedFont;
	FStringNoInit	TinyFontName, SmallFontName, MedFontName;
	FStringNoInit	XBoxButtonFontName;
	UFont*			XBoxButtonFont;
	UViewport*		Viewport;
    FCanvasUtil*    pCanvasUtil; // shared canvas util for batching successive canvas rendering funcs

	// UCanvas interface.
	virtual void Init( UViewport* InViewport );
	virtual void Update();
	virtual void NotifyLevelChange();
	virtual void DrawTile( UMaterial* Material, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FLOAT U, FLOAT V, FLOAT UL, FLOAT VL, FLOAT Z, 
		FPlane Color,UBOOL bFlipUVsHoriz = 0,UBOOL bFlipUVsVert = 0, FMatrix* pTransformMat=NULL );

	virtual void DrawIcon( UMaterial* Material, FLOAT ScreenX, FLOAT ScreenY, FLOAT XSize, FLOAT YSize, FLOAT Z, FPlane Color);
	virtual void DrawPattern( UMaterial* Material, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FLOAT Scale, FLOAT OrgX, FLOAT OrgY, FLOAT Z, FPlane Color);
	virtual void VARARGS WrappedStrLenf( UFont* Font, FLOAT& XL, FLOAT& YL, const TCHAR* Fmt, ... );
	virtual void VARARGS WrappedPrintf( UFont* Font, UBOOL Center, const TCHAR* Fmt, ... );

	virtual void VARARGS WrappedStrLenf( UFont* Font, FLOAT ScaleX, FLOAT ScaleY, FLOAT& XL, FLOAT& YL, const TCHAR* Fmt, ... );
	virtual void VARARGS WrappedPrintf( UFont* Font, FLOAT ScaleX, FLOAT ScaleY, UBOOL Center, const TCHAR* Fmt, ... );
	//epic moelfke: made WrappedPrint() public so we don't have to call the slower WrappedPrintf() from outside UCanvas
	void WrappedPrint( ERenderStyle Style, FLOAT& XL, FLOAT& YL, UFont* Font, FLOAT ScaleX, FLOAT ScaleY, UBOOL Center, const TCHAR* Text );

	virtual void WrapStringToArray( const TCHAR* Text, TArray<FString> *OutArray, FLOAT Width, UFont *Font=NULL, FLOAT ScaleX=1.f, const TCHAR EOL='\n' );
	virtual void ClippedStrLen( UFont* Font, FLOAT ScaleX, FLOAT ScaleY, FLOAT& XL, FLOAT& YL, const TCHAR* Text );
	virtual void ClippedPrint( UFont* Font, FLOAT ScaleX, FLOAT ScaleY, UBOOL Center, const TCHAR* Text );
	virtual void XBoxClippedStrLen( UFont* Font, FLOAT ScaleX, FLOAT ScaleY, FLOAT& XL, FLOAT& YL, const TCHAR* Text );

	void DrawActor( AActor* Actor, UBOOL WireFrame, UBOOL ClearZ, FLOAT DisplayFOV );
	void DrawActorTile( AActor* Actor, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FRotator& CameraRot, FVector& CameraPos, FLOAT DisplayFOV );	// scion sz

	void virtual DrawTileStretched(UMaterial* Mat, FLOAT Left, FLOAT Top, FLOAT AWidth, FLOAT AHeight);
	void virtual DrawTileStretchedUV(UMaterial* Mat, FLOAT Left, FLOAT Top, FLOAT AWidth, FLOAT AHeight, 
		FLOAT U, FLOAT V, FLOAT UL, FLOAT VL, FMatrix* pTransformMat=NULL );

	void virtual DrawTileScaled(UMaterial* Mat, FLOAT Left, FLOAT Top, FLOAT NewXScale, FLOAT NewYScale);
	void virtual DrawTileBound(UMaterial* Mat, FLOAT Left, FLOAT Top, FLOAT Width, FLOAT Height);
	void virtual DrawTileJustified(UMaterial* Mat, FLOAT Left, FLOAT Top, FLOAT Width, FLOAT Height, BYTE Justification);
	void virtual DrawTileScaleBound(UMaterial* Mat, FLOAT Left, FLOAT Top, FLOAT Width, FLOAT Height);
	void virtual VARARGS DrawTextJustified(BYTE Justification, FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, FLOAT ScaleX, FLOAT ScaleY, const TCHAR* Fmt, ... );
	virtual void VARARGS DrawXboxTextJustified(BYTE Justification, FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, FLOAT ScaleX, FLOAT ScaleY, BYTE XboxButtonAlpha, const TCHAR* Fmt, ... );

	void SetCanvasMask( FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2, FLOAT Z=FAR_CLIPPING_PLANE );
	void ClearCanvasMask();

	virtual void SetClip( FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL );

	// UI scaling. Non-virtual, no data: the script layout of Canvas is
	// untouched; the state lives in pCanvasUtil. With [<Client>] UIScaleMode=Fit43 the canvas (ClipX/ClipY,
	// SizeX/SizeY, every Draw*) speaks the 640x480 design pixels of the UI and CanvasToScreen maps them into a
	// centred box of the viewport; "target" pixels are the viewport's own.
	void ApplyUIBox( UBOOL bWanted );		// recompute size and matrix (Update() calls it with the client's setting)
	UBOOL IsUIBoxed() const;
	void DesignToTarget( FLOAT& X, FLOAT& Y ) const;
	void TargetToDesign( FLOAT& X, FLOAT& Y ) const;
	FLOAT GetUIScale() const;
	void GetTargetInDesign( FLOAT& X1, FLOAT& Y1, FLOAT& X2, FLOAT& Y2 ) const;	// the whole viewport in design pixels
	void SetUIContext( BYTE Context );		// UICONTEXT_Game or UICONTEXT_Menu (the GUI controller while it draws)
	FMatrix UICanvasToScreen() const;		// the base CanvasToScreen of this frame (SetCanvasMatrix composes on it)

	// Natives.
	DECLARE_FUNCTION(execStrLen)
	DECLARE_FUNCTION(execDrawText)
	DECLARE_FUNCTION(execDrawTile)
	DECLARE_FUNCTION(execDrawActor)
	DECLARE_FUNCTION(execDrawTileClipped)
	DECLARE_FUNCTION(execDrawTextClipped)
	DECLARE_FUNCTION(execTextSize)
	DECLARE_FUNCTION(execDrawPortal)
	DECLARE_FUNCTION(execWrapStringToArray)		// mc
	DECLARE_FUNCTION(execDrawActorTile)			// scion sz
	DECLARE_FUNCTION(execSetCanvasMask)
	DECLARE_FUNCTION(execClearCanvasMask)


	// jmw

	DECLARE_FUNCTION(execDrawTileStretched);
	DECLARE_FUNCTION(execDrawTileStretchedUV);	//scion
	DECLARE_FUNCTION(execDrawTileJustified);
	DECLARE_FUNCTION(execDrawTileScaled);
	DECLARE_FUNCTION(execDrawTextJustified);

	//scion rwd - canvas matrix support
	DECLARE_FUNCTION(execSetCanvasMatrix);
	//scion rwd - force a flush of the util
	DECLARE_FUNCTION(execForceFlush);
	//scion bcl -- as above but from C++
	void Flush();
	//epic moelfke: draws text using two fonts. Words beginning with % are drawn using the secondary font.
	//(for drawing button graphics, etc. in the middle of text)
	DECLARE_FUNCTION(execDrawTextDualFont);

    void eventReset()
    {
		static FName NAME_Reset = FName(TEXT("Reset"));
        ProcessEvent(FindFunctionChecked(NAME_Reset), NULL);
    }
};

#if SUPPORTS_PRAGMA_PACK
#pragma pack (pop)
#endif
