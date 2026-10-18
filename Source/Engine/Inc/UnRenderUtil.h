/*=============================================================================
	UnRenderUtil.h: Rendering utility definitions.
	Copyright 1997-2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/

//
//  EClippingFlag
//
enum EClippingFlag
{
	CF_Inside = 0x1,
	CF_Outside = 0x2
};

//
//  FConvexVolume
//
class ENGINE_API FConvexVolume
{
public:

	enum { MAX_VOLUME_PLANES = 32 };

#if defined(_XBOX)
	SArray<FPlane,MAX_VOLUME_PLANES> BoundingPlanes;
#else
	FPlane	BoundingPlanes[MAX_VOLUME_PLANES];
#endif

	INT		NumPlanes;

	// Constructor
	FConvexVolume();

	// SphereCheck - Determines whether a sphere is inside of the volume, outside of the volume, or both.  Returns EClippingFlag.
	//scion jg -- Pass by ref not value
	BYTE SphereCheck(const FSphere& Sphere);

	// BoxCheck - Determines whether a box is inside of the volume, outside of the volume, or both.  Returns EClippingFlag.
	//scion jg -- Pass by ref not value
	BYTE BoxCheck(const FVector& Origin,const FVector& Extent);

	// ClipPolygon
	FPoly ClipPolygon(FPoly Polygon);
};

//
//	FLineVertex
//
class ENGINE_API FLineVertex
{
public:

	FVector	Position;
	FColor	Diffuse;

	// Constructors.
	FLineVertex() {}
	FLineVertex(FVector InPosition,FColor InDiffuse)
	{
		Position = InPosition;
		Diffuse = InDiffuse;
	}
};

//
//	FLineBatcher
//
class ENGINE_API FLineBatcher : public FVertexStream
{
public:

	TArray<FLineVertex>		Vertices;
	UBOOL					ZTest;
	FRenderInterface*		RI;

	// Constructor/destructor.

	FLineBatcher(FRenderInterface* InRI, UBOOL InZTest=1);
	~FLineBatcher();

	// Flush - Renders all buffered lines.
	void Flush( DWORD PolyFlags=0 );

	// DrawLine - Buffers a line for rendering.
	void DrawLine(FVector P1,FVector P2,FColor Color);

	// DrawPoint - Buffers a point for rendering.  Renders the point as a square of connected lines.
	void DrawPoint(class FSceneNode* SceneNode,FVector P,FColor Color);

	// DrawBox - Buffers a wireframe box for rendering.
	void DrawBox(FBox Box,FColor Color);

	// DrawCircle - Buffers a wireframe circle for rendering.
	void DrawCircle(FVector Base,FVector X,FVector Y,FColor Color,FLOAT Radius,INT NumSides);

	//epic moelfke: DrawSphere - Buffers a wireframe sphere for rendering.
	void DrawSphere(const FVector& Base, FLOAT Radius, FColor Color, INT NumDivisions);

	// DrawCylinder - Buffers a wireframe cylinder for rendering.
	void DrawCylinder(FRenderInterface* RI,FVector Base,FVector X,FVector Y,FVector Z,FColor Color,FLOAT Radius,FLOAT HalfHeight,INT NumSides);

	// DrawDirectionalArrow
	void DrawDirectionalArrow(FVector InLocation,FRotator InRotation,FColor InColor,FLOAT InDrawScale = 1.0f);

	// DrawConvexVolume
	void DrawConvexVolume(FConvexVolume Volume,FColor Color);

	// FRenderResource interface.
	virtual INT GetRevision();


	virtual INT GetSize();
	virtual INT GetStride();
	virtual INT GetComponents(FVertexComponent* OutComponents);
	virtual void GetStreamData(void* Dest);
	virtual void GetRawStreamData(void ** Dest, INT FirstVertex );
};

// Slight hack. If we dont have a FRenderInterface handy - use GTempLineBatcher
// All lines/boxes drawn in world space.
class ENGINE_API FTempLineBatcher
{
public:
	//scion jg -- Staying line struct
	struct FStayingLine
	{
		// Start point
		FVector Start;
		// End point
		FVector End;
		// The color to draw it with
		FColor Color;
		// The number of frames the box should hang around
		INT NumFrames;
		// Constructor
		FStayingLine(const FVector& _Start,const FVector& _End,
			const FColor& _Color,INT _NumFrames) :
			Start(_Start), End(_End), Color(_Color), NumFrames(_NumFrames)
		{
		}
	};
	// lines
    TArray<FStayingLine> LineArray;

	//scion jg -- Staying boxes struct
	struct FStayingBox
	{
		// The box to draw
		FBox Box;
		// The color to draw it with
		FColor Color;
		// The number of frames the box should hang around
		INT NumFrames;
		// Constructor
		FStayingBox(const FBox& _Box,const FColor& _Color,INT _NumFrames) :
			Box(_Box), Color(_Color), NumFrames(_NumFrames)
		{
		}
	};

	// boxes
    TArray<FStayingBox>  BoxArray;

	//epic moelfke: staying sphere struct
	struct FStayingSphere
	{
		FVector Center;
		FLOAT Radius;
		FColor Color;
		INT NumDivisions;
		INT NumFrames;

		FStayingSphere(const FVector& _Center, FLOAT _Radius, const FColor& _Color, INT _NumDivisions, INT _NumFrames) :
			Center(_Center), Radius(_Radius), Color(_Color), NumDivisions(_NumDivisions), NumFrames(_NumFrames)
		{
		}
	};

	TArray<FStayingSphere> SphereArray;

	// Constructor / Destructor

	FTempLineBatcher() {};
	~FTempLineBatcher() {};	

	// Interface

	void AddLine(const FVector& P1,const FVector& P2,const FColor& Color,INT NumFrames = 1)
	{
		new(LineArray)FStayingLine(P1,P2,Color,NumFrames);
	}

	void AddBox(const FBox& Box,const FColor& Color,INT NumFrames = 1)
	{
		new(BoxArray)FStayingBox(Box,Color,NumFrames);
	}

	void AddStayingLine(const FVector& P1,const FVector& P2,const FColor& Color,INT NumFrames = -1)
	{
		new(LineArray)FStayingLine(P1,P2,Color,NumFrames);
	}

	void AddSphere(const FVector& Center, FLOAT Radius, const FColor& Color, INT NumDivisions, INT NumFrames = 1)
	{
		new(SphereArray)FStayingSphere(Center, Radius, Color, NumDivisions, NumFrames);
	}

	void ClearStayingLines()
	{
		LineArray.Empty();
	}


	void Render(FRenderInterface* InRI, UBOOL InZTest=1);

	//scion jg -- Whether there is anything to render or not
	FORCEINLINE UBOOL NeedsToRender(void)
	{
		return LineArray.Num() || BoxArray.Num() || SphereArray.Num();
	}
};

//
//	FCanvasVertex
//
class ENGINE_API FCanvasVertex
{
public:

	FVector	Position;
	FColor	Diffuse;
	FLOAT	U,
			V;

	// Constructors.
	FCanvasVertex() {}
	FCanvasVertex(FVector InPosition,FColor InDiffuse,FLOAT InU,FLOAT InV)
	{
		Position = InPosition;
		Diffuse = InDiffuse;
		U = InU;
		V = InV;
	}
};

//
//	FCanvasUtil
//	Utilities for rendering stuff in "canvas" space.
//	Canvas space is in pixel coordinates, 0 through to viewport width/height.
//
//epic rwd - changed to support batching
// FCanvasUtil::UIContext.
enum EUIContext
{
	UICONTEXT_Game	= 0,	// the HUD, script, the engine's own 2D
	UICONTEXT_Menu	= 1,	// the GUI controller's pages (their full-screen backgrounds follow UIMenuBackground)
};

class ENGINE_API FCanvasUtil : public FVertexStream
{
public:
	struct FCanvasBatch
	{
		EPrimitiveType			PrimitiveType;
		UMaterial*				Material;
		INT						NumPrimitives;
		TArray<FCanvasVertex>	Vertices;

		BYTE					RenderStyle;		// matches AActor::ERenderStyle, used a BYTE to avoid header mess

		TArray<FBox>			BoundingBoxes;		// individual boxes for each primitive
		FBox					Bounds;				// overall bounds for quick checks
	};
	TArray<FCanvasBatch>		Batches;
	INT							BatchIdx;			// current index to Batches[] that we are writing to, set by BeginPrimitive()

	FRenderInterface*			RI;
	FMatrix						ScreenToCanvas,
								CanvasToScreen;

	UBOOL						bZTest;				// scion sz - to enable z-buffer test

	// UI scaling: set by UCanvas::Update() for the viewport canvas.
	// With [<Client>] UIScaleMode=Fit43 the canvas's coordinates are the 640x480 design pixels of the game's UI
	// and CanvasToScreen puts them into a centred box of the target; a canvas built anywhere else (camera
	// effects, scripted textures, the editor) keeps the target's pixels (bUIBoxed 0, scale 1, origin 0).
	UBOOL						bUIBoxed;			// coordinates are design pixels mapped into the box
	FLOAT						UIScale;			// target pixels per design pixel
	FLOAT						UIOriginX,			// the box's top-left corner on the target, in target pixels
								UIOriginY;
	FLOAT						UITargetX,			// the target's size in pixels
								UITargetY;
	BYTE						UIContext;			// who draws: UICONTEXT_Game (HUD, script) or UICONTEXT_Menu (the GUI)

    // Constructor/destructor.
	FCanvasUtil(FRenderTarget* RenderTarget,FRenderInterface* InRI);
	~FCanvasUtil();

	//epic rwd - responsible for setting the proper BatchIdx before drawing anything
	void BeginPrimitive(EPrimitiveType primType, UMaterial *material, BYTE renderStyle, FBox &tileBounds);

	// Flush - Renders all buffered primitives.
	void Flush();

	// DrawLine - Buffers a line for rendering.
	void DrawLine(FLOAT X1,FLOAT Y1,FLOAT X2,FLOAT Y2,FColor Color);

	// DrawPoint - Buffers a point for rendering.
	void DrawPoint(FLOAT X1,FLOAT Y1,FLOAT X2,FLOAT Y2,FLOAT Z,FColor Color);

	// DrawTile - Buffers a textured tile for rendering.
	void DrawTile(
		FLOAT X1,
		FLOAT Y1,
		FLOAT X2,
		FLOAT Y2,
		FLOAT U1,
		FLOAT V1,
		FLOAT U2,
		FLOAT V2,
		FLOAT Z,
		UMaterial* Material,
		FColor Color,
		BYTE RenderStyle = 0,
		UBOOL bFlipUVsHoriz = 0,
		UBOOL bFlipUVsVert = 0,
		FMatrix* pTransformMat=NULL
		);

	// DrawString - Buffers a string for rendering.  Returns the width of the string rendered.
	INT DrawString(
		INT StartX,
		INT StartY,
		const TCHAR* Text,
		class UFont* Font,
		FColor Color
		);

	// FRenderResource interface.
	virtual INT GetRevision();

	// FVertexStream interface.
	virtual INT GetSize();
	virtual INT GetStride();
	virtual INT GetComponents(FVertexComponent* OutComponents);
	virtual void GetStreamData(void* Dest);
	virtual void GetRawStreamData(void ** Dest, INT FirstVertex );
};

//
//	FScreenPrimUtil
//
class ENGINE_API FScreenPrimUtil : public FVertexStream
{
public:
	class FScreenPrimVertex
	{
	public:

		FVector	Position;
		FColor	Diffuse;
		struct UV {
            FLOAT U,V;
		} UVs[4];
		
		// Constructors.
		FScreenPrimVertex() {}
		FScreenPrimVertex(const FVector& InPosition,FColor InDiffuse, FLOAT InU[4], FLOAT InV[4] )
		{
			Position = InPosition;
			Diffuse = InDiffuse;
			UVs[0].U = InU[0];		UVs[0].V = InV[0];
			UVs[1].U = InU[1];		UVs[1].V = InV[1];
			UVs[2].U = InU[2];		UVs[2].V = InV[2];
			UVs[3].U = InU[3];		UVs[3].V = InV[3];
		}
		//jg -- Set the values directly without the constructor
		void SetVertInfo(FLOAT X,FLOAT Y,FLOAT Z,FColor InDiffuse,FLOAT InU[4],
			FLOAT InV[4])
		{
			Position.X = X;
			Position.Y = Y;
			Position.Z = Z;
			Diffuse = InDiffuse;
			UVs[0].U = InU[0];		UVs[0].V = InV[0];
			UVs[1].U = InU[1];		UVs[1].V = InV[1];
			UVs[2].U = InU[2];		UVs[2].V = InV[2];
			UVs[3].U = InU[3];		UVs[3].V = InV[3];
		}
	};

	FScreenPrimVertex		Vertices[16];
	EPrimitiveType			PrimitiveType;
	UMaterial*				Material;
	_WORD					NumPrimitives;
	_WORD					NumVerts;

	FRenderInterface*		RI;
	FMatrix					CanvasToScreen;
	// jg -- Cache the last known sizes of the viewport so we can update
	// only when these change
	_WORD SizeX;
	_WORD SizeY;
	// jg -- Function to determine if we need to update the CanvasToScreen
	// matrix
	FORCEINLINE UBOOL NeedsUpdate(_WORD NewX,_WORD NewY)
	{
		return NewX != SizeX || NewY != SizeY;
	}
	// jg -- Rebuilds the CanvasToScreen matrix if needed
	FORCEINLINE void Update(_WORD NewX,_WORD NewY,FRenderInterface* pRI)
	{
		// Update the render interface
		RI = pRI;
		// Only rebuild the matrix if needed
		if (NeedsUpdate(NewX,NewY))
		{
			SizeX = NewX;
			SizeY = NewY;
			CanvasToScreen =
				FTranslationMatrix(FVector(-((FLOAT)SizeX) / 2.0f - 0.5f,-((FLOAT)SizeY) / 2.0f - 0.5f,0.0f)) *
				FScaleMatrix(FVector(2.0f / (FLOAT)SizeX,-2.0f / (FLOAT)SizeY,1.0f));
		}
	}

	// Constructor/destructor.
	FScreenPrimUtil(void);
	FScreenPrimUtil(UViewport* Viewport, FRenderInterface* InRI);
	FScreenPrimUtil(FRenderTarget* RenderTarget,FRenderInterface* InRI);
	~FScreenPrimUtil();

	// Flush - Renders all buffered primitives.
	void Flush();

	// BeginPrimitive - Prepares to render a primitive with the given attributes.  Automatically flushes if necessary.

	void BeginPrimitive(EPrimitiveType InType,UMaterial* InMaterial);

	// DrawTile - Buffers a textured tile for rendering.
	void DrawTile(
		FLOAT X1,
		FLOAT Y1,
		FLOAT X2,
		FLOAT Y2,
		FLOAT U1[4],
		FLOAT V1[4],
		FLOAT U2[4],
		FLOAT V2[4],
		FLOAT Z,
		UMaterial* Material,
		FColor Color
		);

	// FRenderResource interface.
	virtual INT GetRevision() { return 1; }

	// FVertexStream interface.
	virtual INT GetSize() { return (NumVerts + 1) * sizeof(FScreenPrimVertex);	}
	virtual INT GetStride() { return sizeof(FScreenPrimVertex);	}
	virtual INT GetComponents(FVertexComponent* OutComponents);
	virtual void GetStreamData(void* Dest);
	virtual void GetRawStreamData(void ** Dest, INT FirstVertex );	
};

//
//	FRawIndexBuffer
//
class ENGINE_API FRawIndexBuffer : public FIndexBuffer
{
public:

	TArray<_WORD>	Indices;
	INT				Revision;

	// Constructor.
	FRawIndexBuffer();

	//jg --  Builds strips or lists optimized for cache coherency or memory usage
	virtual INT Stripify(UBOOL bCacheOptimize,UBOOL bTriStrip);

	// Serialization.
	ENGINE_API friend FArchive& operator<<(FArchive& Ar,FRawIndexBuffer& I);

	// FRenderResource interface.
	virtual INT GetRevision();

	// FIndexBuffer interface.
	virtual INT GetSize();
	virtual void GetContents(void* Data);
	virtual INT GetIndexSize();
	//scion jg -- Allow direct access to the index buffer data
	virtual void* GetContentsPtr(void);
};


//
//	FRaw32BitIndexBuffer
//
class ENGINE_API FRaw32BitIndexBuffer : public FIndexBuffer
{
public:

	TArray<DWORD>	Indices;
	INT				Revision;

	// Constructor.
	FRaw32BitIndexBuffer();

	// Serialization.
	ENGINE_API friend FArchive& operator<<(FArchive& Ar,FRaw32BitIndexBuffer& I);

	// FRenderResource interface.
	virtual INT GetRevision();

	// FIndexBuffer interface.
	virtual INT GetSize();
	virtual void GetContents(void* Data);
	virtual INT GetIndexSize();
};

//
//	FRawColorStream
//
class ENGINE_API FRawColorStream : public FVertexStream
{
public:

#if defined(_XBOX) || defined(XBOX_ASSET_FORMAT)
	//scion jg -- UMA support for color streams
	TUmaArray<FColor>		Colors;
#else
	TArray<FColor>			Colors;
#endif
	INT						Revision;

	// Constructor.
	FRawColorStream();

	// Serializer.
	friend ENGINE_API FArchive& operator<<(FArchive& Ar,FRawColorStream& ColorStream);

	// FRenderResource interface.
	virtual INT GetRevision();

	// FVertexStream interface.
	virtual INT GetSize();
	virtual INT GetStride();
	virtual INT GetComponents(FVertexComponent* OutComponents);
	virtual void GetStreamData(void* Dest);
	virtual void GetRawStreamData(void ** Dest, INT FirstVertex );
};

//
//  FSolidColorTexture
//

class ENGINE_API FSolidColorTexture : public FTexture
{
public:

	FColor	Color;

	INT		Revision;

	// Constructor.

	FSolidColorTexture(FColor InColor);

	// FRenderResource interface.

	virtual INT GetRevision();

	// FBaseTexture interface.

	virtual INT GetWidth();
	virtual INT GetHeight();
	virtual ETexClampMode GetUClamp();
	virtual ETexClampMode GetVClamp();
	virtual ETextureFormat GetFormat();
	virtual INT GetNumMips();
	virtual INT GetFirstMip();

	// FTexture interface.

	virtual void GetTextureData(INT MipIndex,void* Dest,INT DestStride,ETextureFormat DestFormat,UBOOL ColoredMips);
	virtual void* GetRawTextureData(INT MipIndex);
	virtual void UnloadRawTextureData( INT MipIndex ) {}

	virtual UTexture* GetUTexture();
};

//
//	FAuxRenderTarget
//

class ENGINE_API FAuxRenderTarget : public FRenderTarget
{
public:
	_WORD			CacheIdColor, CacheIdDepth;
	INT				Revision,
					Width,
					Height;
	ETextureFormat	Format;	

	// Constructor.

	FAuxRenderTarget(INT InWidth,INT InHeight,ETextureFormat InFormat,INT InCreationFlags=RT_CREATE_COLOR|RT_CREATE_DEPTH);
	~FAuxRenderTarget();

	// FRenderResource interface.

	virtual INT GetRevision();
	//scion jg -- Special case render target cache functions
	virtual _WORD GetCacheId(void);
	virtual void SetCacheId(_WORD NewCacheID,URenderDevice* pRenDev);

	// FBaseTexture interface.

	virtual INT GetWidth();
	virtual INT GetHeight();
	virtual ETexClampMode GetUClamp();
	virtual ETexClampMode GetVClamp();
	virtual ETextureFormat GetFormat();
	virtual INT GetNumMips();
	virtual INT GetFirstMip();
};

// scion sz
// stores offsets/weights corresponding to a 4 tap filter
class ENGINE_API FFourSampleFilter
{
public:
	struct FSamplePoint {
		FLOAT	OffsetU,
				OffsetV,
				Weight;
	};

	struct FSamplePoint			SamplePoints[4];
	FFourSampleFilter()	
	{
		appMemzero( SamplePoints, sizeof(SamplePoints) );
	}
};

// scion sz
// creates multiple sample kernels
class ENGINE_API FMultiSampleFilter
{
public:
	enum EMultiSampleType {
		MULTI_SAMPLE_BOX,			// box filter - sample weights( 1/4, 1/4, 1/4, 1/4 ), offsets( -1u, -1v, +1u, +1v )
		MULTI_SAMPLE_HORIZ_1221,	// horizontal separable filter - sample weights( 1/6, 2/6, 2/6, 1/6 ), offsets( -1u, -3u, +3u, +1u )
		MULTI_SAMPLE_VERT_1221,		// vertical separable filter - sample weights( 1/6, 2/6, 2/6, 1/6 ), offsets( -1v, -3v, +3v, +1v )
		MULTI_SAMPLE_HORIZ_1331,	// horizontal separable filter - sample weights( 1/8, 3/8, 3/8, 1/8 ), offsets( -1u, -3u, +3u, +1u )
		MULTI_SAMPLE_VERT_1331,		// vertical separable filter - sample weights( 1/8, 2/8, 2/8, 1/8 ), offsets( -1v, -3v, +3v, +1v )
		STREAK_SAMPLE_HORIZ
	};

	TArray<FFourSampleFilter>	FilterList;

	FMultiSampleFilter() {}
	FMultiSampleFilter( EMultiSampleType InType, FLOAT Scale=1.0f )
	{
		if( InType < STREAK_SAMPLE_HORIZ )
			GenerateMultiSampleFilter( InType, Scale );
		else
			GenerateStreakFilter( InType, 0.f, Scale );
	}

	void GenerateMultiSampleFilter( EMultiSampleType InType, FLOAT Scale=1.0f );
	void GenerateStreakFilter( EMultiSampleType InType, FLOAT Angle, FLOAT Scale=1.0f );
};

// The picture the post-process sizes and distances refer to. Epic's camera
// effects are made for the Xbox's 640x480 back buffer: their targets are fractions of it and
// their taps and insets are its pixels. With a reference height R (FRenderCaps::
// PostProcessReferenceHeight, normally 480) the reference picture is R lines tall and as wide as
// the back buffer's aspect gives (square pixels), whatever the real resolution; one of its pixels
// is PixelScale = H / R back-buffer pixels. Without a reference (R = 0) it is the back buffer
// itself and PixelScale is 1: Epic's behaviour.
struct FPostProcessReference
{
	FLOAT	Width,			// the reference picture, in its own pixels
			Height,
			PixelScale;		// back-buffer pixels per reference pixel
};
ENGINE_API FPostProcessReference GetPostProcessReference( INT BackBufferWidth, INT BackBufferHeight );

// What FilterCopyRenderTarget adds to UPSBlurMaterial::BlurBlendMode (EBlurBlendMode, 0..3) for a
// pass with TapScale 2 or 4: every tap of the pass is to be the mean of TapScale reads 2 source texels apart along X
// and/or Y (UC2_BLUR_FILL_4: four reads, else two), computed in the shader (UC2GLDrv: the fill variant of PS_Blur).
#define UC2_BLUR_MODE_MASK	0x1f
#define UC2_BLUR_FILL_4		0x20
#define UC2_BLUR_FILL_X		0x40
#define UC2_BLUR_FILL_Y		0x80

// scion sz
// Used for different blurring and sampling routines
// TapScale (1, 2 or 4; RenDevPostProcessTargetScale) for a target-to-target pass of a chain whose
// targets have that many times the Xbox's texels per axis: the same blur on the screen, sampled more finely
// (UnRenderUtil.cpp). Ignored for a pass from the back buffer (SrcViewport).
void ENGINE_API FilterCopyRenderTarget( FRenderTarget* DestTarget,
										FRenderTarget* SrcTarget,
										UViewport* SrcViewport, 
										FMultiSampleFilter& Sampler,
										FRenderInterface* RI,
										FColor* CopyColor=NULL,
										class UFinalBlend* FinalBlend=NULL,
										DWORD BlurBlendMode=0,
										INT TapScale=1 );
