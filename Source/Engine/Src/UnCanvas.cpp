/*=============================================================================
	UnCanvas.cpp: Unreal canvas rendering.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Tim Sweeney
	* 31/3/99 Updated revision history - Jack Porter
=============================================================================*/


#include "EnginePrivate.h"
#include "UnLinker.h"	// A font's package export table (sharper text)
#include "wordwrap.h"

#ifdef _XBOX
#define DEFAULT_FONT TEXT("T_Fonts2.SuiGeneris_12pt")
#else
#define DEFAULT_FONT TEXT("WarfareFonts.DefaultFont")
#endif

static UFont* FallbackFont=NULL;

/*-----------------------------------------------------------------------------
	UCanvas scaled sprites.
-----------------------------------------------------------------------------*/

//
// Draw arbitrary aligned rectangle.
//
void UCanvas::DrawTile
(
	UMaterial*		Material,
	FLOAT			X,
	FLOAT			Y,
	FLOAT			XL,
	FLOAT			YL,
	FLOAT			U,
	FLOAT			V,
	FLOAT			UL,
	FLOAT			VL,
	FLOAT			Z,
	FPlane			DrawColor,
	UBOOL bFlipUVsHoriz,
	UBOOL bFlipUVsVert,
	FMatrix* pTransformMat )
{
    if (pCanvasUtil != NULL &&
		Material != NULL)
	{
		DrawColor.W *= ForcedAlpha;

		// If we are modulating the alpha channel, force the style to alpha
		if ( DrawColor.W != 1.0 )
			Style = STY_Alpha;

		// A tile that covers the whole design space is a full-screen layer, not a UI element: the game's
		// fades, flashes and overlays (UCPlayerController's blood and ColorOverlay) cover the whole viewport,
		// stretched; a menu page's background follows UIMenuBackground (Box: stays in the box; Fill: the
		// whole viewport, its picture scaled to cover it with the aspect kept, the overflow cropped).
		UBOOL bLayer = 0;
		if( pCanvasUtil->bUIBoxed && !pTransformMat && XL > 0.f && YL > 0.f
		&&	X <= 0.5f && Y <= 0.5f && X + XL >= SizeX - 0.5f && Y + YL >= SizeY - 0.5f )
		{
			UClient* Client = Viewport->GetOuterUClient();
			UBOOL bMenu = pCanvasUtil->UIContext == UICONTEXT_Menu;
			UBOOL bFill = !bMenu || (Client && Client->UIMenuBackground == UIMENUBG_Fill);
			// Say once per material what happened to it (which layers these are).
			static TArray<UMaterial*> Reported;
			if( Reported.FindItemIndex(Material) == INDEX_NONE && Reported.Num() < 256 )
			{
				Reported.AddItem( Material );
				debugf( NAME_Log, TEXT("UIScale: full-screen tile %s (%s) %s"), Material->GetFullName(),
					bMenu ? TEXT("menu") : TEXT("game"),
					!bFill ? TEXT("stays in the box") : bMenu ? TEXT("covers the viewport, aspect kept") : TEXT("stretched to the viewport") );
			}
			if( bFill )
			{
				FLOAT X1, Y1, X2, Y2;
				GetTargetInDesign( X1, Y1, X2, Y2 );
				if( bMenu )
				{
					// cover: scale the tile about its centre until it covers the viewport, keep what shows
					FLOAT K = Max( (X2 - X1) / XL, (Y2 - Y1) / YL );
					FLOAT CX = X + XL * 0.5f, CY = Y + YL * 0.5f;
					FLOAT SX1 = CX - XL * K * 0.5f, SY1 = CY - YL * K * 0.5f;
					FLOAT FU1 = (X1 - SX1) / (XL * K), FU2 = (X2 - SX1) / (XL * K);
					FLOAT FV1 = (Y1 - SY1) / (YL * K), FV2 = (Y2 - SY1) / (YL * K);
					FLOAT NewU = U + UL * FU1, NewUL = UL * (FU2 - FU1);
					FLOAT NewV = V + VL * FV1, NewVL = VL * (FV2 - FV1);
					U = NewU; UL = NewUL; V = NewV; VL = NewVL;
				}
				X = X1; Y = Y1; XL = X2 - X1; YL = Y2 - Y1;
				bLayer = 1;
			}
		}
		// The menus are clipped to the box, as the Xbox's 640x480 screen clipped them (UC2's pages have
		// decorations that run past x=640: a black header bar over the menu level otherwise). The HUD is not:
		// its markers on actors (WorldToScreen) may be anywhere in the viewport.
		if( !bLayer && pCanvasUtil->bUIBoxed && !pTransformMat && pCanvasUtil->UIContext == UICONTEXT_Menu && XL != 0.f && YL != 0.f )
		{
			if( XL < 0.f ) { X += XL; XL = -XL; U += UL; UL = -UL; }
			if( YL < 0.f ) { Y += YL; YL = -YL; V += VL; VL = -VL; }
			FLOAT CX2 = SizeX, CY2 = SizeY;
			if( X >= CX2 || Y >= CY2 || X + XL <= 0.f || Y + YL <= 0.f )
				return;
			if( X < 0.f )			{ FLOAT F = -X / XL;				U += UL * F; UL -= UL * F; XL += X; X = 0.f; }
			if( Y < 0.f )			{ FLOAT F = -Y / YL;				V += VL * F; VL -= VL * F; YL += Y; Y = 0.f; }
			if( X + XL > CX2 )		{ FLOAT F = (X + XL - CX2) / XL;	UL -= UL * F; XL = CX2 - X; }
			if( Y + YL > CY2 )		{ FLOAT F = (Y + YL - CY2) / YL;	VL -= VL * F; YL = CY2 - Y; }
		}

		// scion sz - note that Z doesn't actually get used (it was always 0 anyway)
		// set the canvas CurZ value instead
		pCanvasUtil->DrawTile(X,Y,
			X + XL,Y + YL,
			U,V,
			U + UL,V + VL,
			CurZ/FAR_CLIPPING_PLANE,
			Material,DrawColor,Style,bFlipUVsHoriz,bFlipUVsVert,
			pTransformMat );
	}
}

//
// Draw titling pattern.
//
void UCanvas::DrawPattern
(
	UMaterial*		Material,
	FLOAT			X,
	FLOAT			Y,
	FLOAT			XL,
	FLOAT			YL,
	FLOAT			Scale,
	FLOAT			OrgX,
	FLOAT			OrgY,
	FLOAT			Z,
	FPlane			Color
)
{
	guard(UCanvas::DrawPattern);
	DrawTile( Material, X, Y, XL, YL, (X-OrgX)*Scale + Material->MaterialUSize(), (Y-OrgY)*Scale + Material->MaterialVSize(), XL*Scale, YL*Scale, Z, Color);
	unguard;
}

//
// Draw a scaled sprite.  Takes care of clipping.
// XSize and YSize are in pixels.
//
void UCanvas::DrawIcon
(
	UMaterial*			Material,
	FLOAT				ScreenX,
	FLOAT				ScreenY,
	FLOAT				XSize,
	FLOAT				YSize,
	FLOAT				Z,
	FPlane				Color
)
{
	guard(UCanvas::DrawIcon);
	DrawTile( Material, ScreenX, ScreenY, XSize, YSize, 0, 0, Material->MaterialUSize(), Material->MaterialVSize(), Z, Color);
	unguard;
}

/*-----------------------------------------------------------------------------
	Clip window.
-----------------------------------------------------------------------------*/

void UCanvas::SetClip( FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL )
{
	guard(UCanvas::SetClip);

	CurX  = 0;
	CurY  = 0;
	OrgX  = X;
	OrgY  = Y;
	ClipX = XL;
	ClipY = YL;

	unguard;
}

/*-----------------------------------------------------------------------------
	UCanvas basic text functions.
-----------------------------------------------------------------------------*/
TCHAR Ellipsis[4] = TEXT("...");

/*-----------------------------------------------------------------------------
	Sharper text in a scaled-up UI.

	UC2's text is laid out with bitmap fonts made for 640x480 ("T_Fonts2.SuiGeneris_12pt"), so a UI drawn x2.25
	or x4.5 magnifies the glyph textures. The same package has the family in other sizes; by glyph height in
	texels: 12pt 16, 18pt 16 (the same bitmap), 20pt 20, 24pt 21, 28pt 26, 40pt 60 (digits only). With
	UISharpText on, text keeps the small font's layout (advances: the metrics every script and the GUI measured
	text with) and each glyph takes its pixels from the smallest taller size that gives at least one texel per
	screen pixel (or the tallest that has the character), drawn at the small glyph's height with its own aspect,
	its ink centred where the small glyph's ink is. Nothing changes at 640x480 (no box) or for fonts without a
	"<Family>_<N>pt" family in their package (the Xbox button glyphs).
-----------------------------------------------------------------------------*/

struct FSharpFontFamily
{
	UFont*			Font;		// the font text is laid out with
	INT				Pt;			// its point size (by name)
	INT				Height;		// its glyphs' height in texels (the names lie: SuiGeneris_18pt is the 12pt bitmap)
	TArray<INT>		Pts;		// taller sizes of its family, by glyph height, ascending
	TArray<INT>		Heights;
	TArray<UFont*>	Fonts;
};

// A font's glyph height in texels: the tallest printable ASCII glyph it has.
static INT FontHeight( UFont* Font )
{
	INT H = 0;
	for( INT c=32; c<127; c++ )
	{
		INT Ch = (TCHARU)Font->RemapChar( (TCHAR)c );
		if( Ch != NULLCHARACTER && Ch < Font->Characters.Num() )
			H = Max( H, Font->Characters(Ch).VSize );
	}
	return H;
}
static TArray<FSharpFontFamily> GSharpFonts;

// "<Family>_<N>pt" -> N (and the family prefix up to the underscore), or 0.
static INT ParseFontPt( const FString& Name, FString* Family )
{
	INT Len = Name.Len();
	if( Len < 4 || appToUpper(Name[Len-1]) != 'T' || appToUpper(Name[Len-2]) != 'P' )
		return 0;
	INT i = Len - 3;
	while( i >= 0 && appIsDigit(Name[i]) )
		i--;
	if( i == Len - 3 || i < 1 || Name[i] != '_' )
		return 0;
	if( Family )
		*Family = Name.Left( i + 1 );
	return appAtoi( *Name.Mid( i + 1 ) );
}

// The ink of each glyph of a font: the columns whose alpha is above 4/15, read from the font's DXT3 page (UC2's
// fonts are DXT3), as the centre in texels from the glyph box's left edge; -1 when unknown (another format,
// the page's data not in memory) or empty (a space). Built once per font.
struct FGlyphInk
{
	UFont*			Font;
	TArray<FLOAT>	Centre;
};
static TArray<FGlyphInk> GGlyphInk;

static FLOAT GlyphInkCentre( UFont* Font, INT Ch )
{
	INT Index = INDEX_NONE;
	for( INT i=0; i<GGlyphInk.Num(); i++ )
		if( GGlyphInk(i).Font == Font )
		{
			Index = i;
			break;
		}
	if( Index == INDEX_NONE )
	{
		Index = GGlyphInk.AddZeroed();
		FGlyphInk& G = GGlyphInk(Index);
		G.Font = Font;
		G.Centre.Add( Font->Characters.Num() );
		for( INT c=0; c<Font->Characters.Num(); c++ )
		{
			G.Centre(c) = -1.f;
			FFontCharacter& Char = Font->Characters(c);
			UTexture* Tex = Char.TextureIndex < Font->Textures.Num() ? Font->Textures(Char.TextureIndex) : NULL;
			if( !Tex || Tex->Format != TEXF_DXT3 || !Tex->Mips.Num() || Char.USize <= 0 || Char.VSize <= 0 )
				continue;
			FMipmap& Mip = Tex->Mips(0);
			Mip.DataArray.Load();
			INT BlocksX = Mip.USize / 4;
			if( BlocksX <= 0 || Mip.DataArray.Num() < (Mip.USize / 4) * (Mip.VSize / 4) * 16 )
				continue;
			const BYTE* Data = &Mip.DataArray(0);
			INT Left = Char.USize, Right = -1;
			for( INT y=Char.StartV; y<Char.StartV+Char.VSize && y<Mip.VSize; y++ )
				for( INT x=Char.StartU; x<Char.StartU+Char.USize && x<Mip.USize; x++ )
				{
					const BYTE* Block = Data + ((y >> 2) * BlocksX + (x >> 2)) * 16;
					INT Row = Block[(y & 3) * 2] | (Block[(y & 3) * 2 + 1] << 8);
					if( ((Row >> ((x & 3) * 4)) & 15) > 4 )
					{
						Left = Min( Left, x - Char.StartU );
						Right = Max( Right, x - Char.StartU );
					}
				}
			if( Right >= Left )
				G.Centre(c) = (Left + Right + 1) * 0.5f;
		}
	}
	FGlyphInk& G = GGlyphInk(Index);
	return Ch >= 0 && Ch < G.Centre.Num() ? G.Centre(Ch) : -1.f;
}

// The family of Font with bigger sizes, and in Start the index of the size to try first (the smallest that gives
// at least one texel per screen pixel at this magnification, or the biggest); NULL when there is none.
static FSharpFontFamily* SharpFontFor( UFont* Font, FLOAT Magnification, INT& Start )
{
	guardSlow(SharpFontFor);
	if( !Font || Magnification < 1.2f )
		return NULL;
	INT Index = INDEX_NONE;
	for( INT i=0; i<GSharpFonts.Num(); i++ )
		if( GSharpFonts(i).Font == Font )
		{
			Index = i;
			break;
		}
	if( Index == INDEX_NONE )
	{
		// Once per font: find its family's bigger sizes in its package. They are kept loaded (root set), and so
		// is the font itself (this table refers to it by address).
		Index = GSharpFonts.AddZeroed();
		FSharpFontFamily& F = GSharpFonts(Index);
		F.Font = Font;
		Font->AddToRoot();
		FString Family;
		F.Pt = ParseFontPt( Font->GetName(), &Family );
		F.Height = FontHeight( Font );
		ULinkerLoad* Linker = Font->GetLinker();
		FString Found;
		if( F.Pt > 0 && Linker && Font->GetOuter() )
		{
			for( INT e=0; e<Linker->ExportMap.Num(); e++ )
			{
				FString Name = *Linker->ExportMap(e).ObjectName;
				FString OtherFamily;
				INT Pt = ParseFontPt( Name, &OtherFamily );
				if( Pt <= F.Pt || appStricmp( *OtherFamily, *Family ) != 0 )
					continue;
				UFont* Big = Cast<UFont>( UObject::StaticLoadObject( UFont::StaticClass(), NULL,
					*FString::Printf( TEXT("%s.%s"), Font->GetOuter()->GetPathName(), *Name ), NULL, LOAD_NoWarn | LOAD_Quiet, NULL ) );
				if( !Big || !Big->Characters.Num() )
					continue;
				INT Height = FontHeight( Big );
				if( Height <= F.Height )
					continue;		// not taller (SuiGeneris_18pt is the 12pt bitmap again)
				Big->AddToRoot();
				INT At = 0;
				while( At < F.Heights.Num() && F.Heights(At) < Height )
					At++;
				F.Pts.Insert( At );
				F.Pts(At) = Pt;
				F.Heights.Insert( At );
				F.Heights(At) = Height;
				F.Fonts.Insert( At );
				F.Fonts(At) = Big;
			}
			for( INT j=0; j<F.Pts.Num(); j++ )
			{
				// how many of the printable ASCII characters each size has (the letters are what text needs)
				INT Have = 0;
				for( INT c=32; c<127; c++ )
					if( F.Fonts(j)->RemapChar( (TCHAR)c ) != NULLCHARACTER )
						Have++;
				INT Ink = 0;
				for( INT c=32; c<127; c++ )
				{
					INT Ch = (TCHARU)F.Fonts(j)->RemapChar( (TCHAR)c );
					if( Ch != NULLCHARACTER && GlyphInkCentre( F.Fonts(j), Ch ) >= 0.f )
						Ink++;
				}
				Found += FString::Printf( TEXT(" %ipt(%i texels high, %i/95 characters, ink of %i read)"), F.Pts(j), F.Heights(j), Have, Ink );
			}
		}
		debugf( NAME_Log, TEXT("UIScale: sharper text for %s (%i texels high):%s"), Font->GetPathName(), F.Height, Found.Len() ? *Found : TEXT(" (no taller size in its package)") );
	}
	FSharpFontFamily& F = GSharpFonts(Index);
	if( !F.Fonts.Num() )
		return NULL;
	FLOAT Want = F.Height * Magnification * 0.95f;
	Start = F.Heights.Num() - 1;
	for( INT j=0; j<F.Heights.Num(); j++ )
		if( F.Heights(j) >= Want )
		{
			Start = j;
			break;
		}
	return &F;
	unguardSlow;
}

//
// Draw a string of characters.
// - returns pixels drawn
//
static FLOAT DrawString
(
	UCanvas*		Canvas,
	UFont*			Font,
    FLOAT           ScaleX,
    FLOAT           ScaleY,
	FLOAT			DrawX,
	FLOAT			DrawY,
	const TCHAR*	Text,
	FPlane			Color,
	UBOOL			bClip,
	UBOOL			bHandleApersand
)
{
	guardSlow(DrawString);

    if ( !Canvas->pCanvasUtil )
        return 0;

    int i;

	// Draw all characters in string.
	FLOAT LineX = 0;
	UBOOL bDrawUnderline = 0;
	FLOAT UnderlineWidth = 0;

	// Get the size needed to display the ellipsis characters
	FLOAT EllipsisWidth, EllipsisHeight;
	Font->GetCharSize('.', EllipsisWidth, EllipsisHeight);
	EllipsisWidth = (EllipsisWidth * 3 + Font->Kerning * 2) * ScaleX;

	// Whether we already checked if we need to draw the ellipsis character
	bool bCheckedEllipsis = Font == Canvas->XBoxButtonFont;

	// The bigger sizes of this font to take the glyphs' pixels from, when the UI is scaled up
	FSharpFontFamily* Sharp = NULL;
	INT SharpStart = 0;
	if( Canvas->IsUIBoxed() )
	{
		UClient* Client = Canvas->Viewport ? Canvas->Viewport->GetOuterUClient() : NULL;
		if( Client && Client->UISharpText )
			Sharp = SharpFontFor( Font, Canvas->GetUIScale() * ScaleY, SharpStart );
	}

	for( i=0; Text[i]; i++ )
	{
		UBOOL bUnderlineNext = 0;
		INT Ch = (TCHARU)Font->RemapChar(Text[i]);
		TCHAR SourceChar = Text[i];		// The character Ch is (for the sharp glyph)

		// Handle ampersand underlining.
		if( bHandleApersand )
		{
			if( bDrawUnderline )
				Ch = (TCHARU)Font->RemapChar('_');
			if( Text[i]=='&' )
			{
				if( !Text[i+1] )
					break;
				if( Text[i+1]!='&' )
				{
					bUnderlineNext = 1;
					Ch = (TCHARU)Font->RemapChar(Text[i+1]);
					SourceChar = Text[i+1];
				}
			}
		}

		{
			UTexture* Tex;
			FFontCharacter& Char = Font->Characters(Ch);
			// A pad button of player one, who uses the keyboard: their key in the glyph's cell, the advance kept
			// (UnKeyGlyphs.cpp). A cell outside the clip region is not drawn either way.
			if( Font == Canvas->XBoxButtonFont && !bHandleApersand && Ch != NULLCHARACTER && Char.USize > 0 && Char.VSize > 0 )
			{
				FLOAT KX = LineX + DrawX, KW = Char.USize * ScaleX, KH = Char.VSize * ScaleY;
				if( (!bClip || (KX + KW > 0 && KX <= Canvas->ClipX && DrawY + KH > 0 && DrawY <= Canvas->ClipY))
				&&	GKeyGlyphs.DrawKey( Canvas, SourceChar, KX, DrawY, KW, KH, Color ) )
				{
					LineX += (Char.USize + Canvas->SpaceX + Font->Kerning) * ScaleX;
					continue;
				}
			}
			if( Char.TextureIndex < Font->Textures.Num() && (Tex=Font->Textures(Char.TextureIndex))!=NULL )
			{
				// Compute character width.
				FLOAT CharWidth;
				if( bDrawUnderline )
					CharWidth = Min<FLOAT>(UnderlineWidth, Char.USize);
				else
					CharWidth = Char.USize;

				// Prepare for clipping.
				FLOAT X      = LineX + DrawX;
				FLOAT Y      = DrawY;
				FLOAT CU     = Char.StartU;
				FLOAT CV     = Char.StartV;
				FLOAT CUSize = CharWidth;
				FLOAT CVSize = Char.VSize;
				FLOAT AdjustedCharUSize = CUSize * ScaleX;
				FLOAT AdjustedCharVSize = CVSize * ScaleY;

				// Draw if it passes clip test.
				if
				(	(!bClip)
				||	(X+AdjustedCharUSize>0 && X<=Canvas->ClipX && Y+AdjustedCharVSize>0 && Y<=Canvas->ClipY) )
				{
					if( bClip )
					{
						// If this is where we'd need to start drawing the ellipsis, check to see how many
						// characters are left to draw
						if ( Text[i+1] && !bCheckedEllipsis && X + EllipsisWidth + AdjustedCharUSize > Canvas->ClipX )
						{
							bCheckedEllipsis = true;
							FLOAT RemainingXL, RemainingYL;
							Canvas->ClippedStrLen(Font, ScaleX, ScaleY, RemainingXL, RemainingYL, &Text[i]);
							if ( appFloor(X + RemainingXL) > appRound(Canvas->ClipX) )
							{
								// The font would overrun the clipping region - draw the ellipsis characters instead
								return LineX + DrawString(Canvas, Font, ScaleX, ScaleY, X, Y, Ellipsis, Color, 0, 0);
							}
						}

						if( X < 0.f )
						{
							CU-=X;
							CUSize+=X;
							X=0;
						}
						if( Y < 0.f )
						{
							CV-=Y;
							CVSize+=Y;
							Y=0;
						}
						if( X + AdjustedCharUSize > Canvas->ClipX )
						{
							FLOAT OldSize = AdjustedCharUSize;
							AdjustedCharUSize = Canvas->ClipX-X;
							FLOAT Percent = AdjustedCharUSize / OldSize;
							CUSize = CUSize * Percent;
						}
						if( Y+AdjustedCharVSize > Canvas->ClipY )
						{
							FLOAT OldSize = AdjustedCharVSize;
							AdjustedCharVSize=Canvas->ClipY-Y;
							FLOAT Percent = AdjustedCharVSize / OldSize;
							CVSize = CVSize * Percent;
						}
					}

					FLOAT		TileX = Canvas->OrgX + X,
								TileY = Canvas->OrgY + Y;

					// A whole glyph (not clipped, not an underline) from a taller size of the font. The taller
					// sizes do not all have every character (SuiGeneris_40pt has digits only): the first one,
					// going down from the preferred one, that has this character gives it; none: the font's own.
					if( Sharp && !bDrawUnderline && CU == Char.StartU && CV == Char.StartV && CUSize == Char.USize && CVSize == Char.VSize
					&&	Char.USize > 0 && Char.VSize > 0 && Ch != NULLCHARACTER )
					{
						for( INT k=SharpStart; k>=0; k-- )
						{
							UFont* Big = Sharp->Fonts(k);
							INT BigCh = (TCHARU)Big->RemapChar( SourceChar );
							if( BigCh == NULLCHARACTER || BigCh >= Big->Characters.Num() )
								continue;
							FFontCharacter& BigChar = Big->Characters(BigCh);
							UTexture* BigTex;
							if( BigChar.USize <= 0 || BigChar.VSize <= 0 || BigChar.TextureIndex >= Big->Textures.Num()
							||	(BigTex = Big->Textures(BigChar.TextureIndex)) == NULL )
								continue;
							// The bigger bitmap's glyphs are not the small ones' proportions (rounding at 16 texels:
							// 'f' is 5x16 at 12pt and 7x26 at 28pt, 'o' 10x16 and 15x26) and their ink sits elsewhere
							// in the box, so the glyph keeps its own aspect at the small glyph's height and its ink
							// is centred where the small glyph's ink is (the box's centre when the ink is unknown);
							// stretched into the small box instead, narrow letters showed gaps ("pro file").
							FLOAT BigWidth = AdjustedCharVSize * BigChar.USize / (FLOAT)BigChar.VSize;
							FLOAT SmallInk = GlyphInkCentre( Font, Ch ), BigInk = GlyphInkCentre( Big, BigCh );
							if( SmallInk >= 0.f && BigInk >= 0.f )
								TileX += SmallInk * AdjustedCharUSize / Char.USize - BigInk * BigWidth / BigChar.USize;
							else
								TileX += (AdjustedCharUSize - BigWidth) * 0.5f;
							AdjustedCharUSize = BigWidth;
							Tex = BigTex;
							CU = BigChar.StartU; CV = BigChar.StartV; CUSize = BigChar.USize; CVSize = BigChar.VSize;
							break;
						}
					}

                    Canvas->DrawTile( Tex, TileX, TileY, AdjustedCharUSize, AdjustedCharVSize,
                                      CU, CV, CUSize, CVSize, 0.f, Color);
				}
				// Update underline status.
				if( bDrawUnderline )
					CharWidth = UnderlineWidth;

				if( !bUnderlineNext )
					LineX += (CharWidth + Canvas->SpaceX + Font->Kerning) * ScaleX;
				else
					UnderlineWidth = Char.USize;

				bDrawUnderline = bUnderlineNext;
			}
		}
	}

	return LineX;

	unguardSlow;
}

// DrawString for the key labels (UnKeyGlyphs.cpp).
ENGINE_API FLOAT KeyGlyphDrawString( UCanvas* Canvas, UFont* Font, FLOAT ScaleX, FLOAT ScaleY, FLOAT X, FLOAT Y, const TCHAR* Text, FPlane Color )
{
	return DrawString( Canvas, Font, ScaleX, ScaleY, X, Y, Text, Color, 0, 0 );
}

//scion superville
//scion bcl -- added the kerning value to the width of all characters
UFont* pCurrentFont = NULL;
static FLOAT StaticWrapScale = 1.f;			// Scaling to apply to the text begin wrapped
static FLOAT StaticXboxFontRatio = 1.f;		// scaling to apply to Xbox font character widths, if xbox font and normal font heights differ
static UFont* StaticWrapButtonFont = NULL;	// Xbox button font - set in WrapStringToArray
static bool bStaticUseButtonFont=false;		// Next character should use the XBox button controller font.

FLOAT GetCharWidthW( WCHAR C, UBOOL bIncludeKerning )
{
	FLOAT Width, Height;
	pCurrentFont->GetCharSize( C, Width, Height );
	if ( bIncludeKerning )
		Width += pCurrentFont->Kerning;
	return (Width * StaticWrapScale);
}

FLOAT GetCharWidthXBox( WCHAR C, UBOOL bIncludeKerning )
{
	FLOAT Width, Height;

	if ( C == 2 )
	{
		bStaticUseButtonFont = true;
		return 0;
	}

	if ( bStaticUseButtonFont )
	{
		bStaticUseButtonFont = false;
		StaticWrapButtonFont->GetCharSize(C,Width,Height);
		return Width * StaticXboxFontRatio * StaticWrapScale;
	}

	pCurrentFont->GetCharSize( C, Width, Height );
	if ( bIncludeKerning )
		Width += pCurrentFont->Kerning;
	return Width * StaticWrapScale;
}

// Notice: This will actually append to the array for continous wrapping, does not support tabs yet
void UCanvas::WrapStringToArray( const TCHAR *Text, TArray<FString>* OutArray, FLOAT Width, UFont* MainFont, FLOAT ScaleX, TCHAR EOL)
{
	guardSlow(UCanvas::WrapStringToArray);

	check(Text != NULL);
	check(OutArray != NULL);
	
	if (*Text == '\0')
		return;

	if (MainFont == NULL)
		MainFont = Font;


//scion superville: Use WordWrap on XBOX
//scion bcl -- removed previous wordwrap code. Now relies on MS wordwrap code.
	// Store font pointer so call back function can use it
	pCurrentFont = MainFont;

// rjp -- store scale and button font for the callback as well.
	StaticWrapScale = ScaleX;
	StaticWrapButtonFont = XBoxButtonFont;

	// Set call back function and options for word wrap lib
	LineBreak_SetOption( 0, WW_PROHIBITION );
	
	// The version that scans for XBox button codes is slightly slower, so
	// only use it if we have xbox control characters in the string
	if ( appStrchr(Text, 2) )
	{
		FLOAT XL, XboxXL, YL, XboxYL;
		pCurrentFont->GetCharSize('A', XL, YL);
		XBoxButtonFont->GetCharSize('A', XboxXL, XboxYL);

		// If we had both XBoxButton characters and standard characters in the text,
		// figure out the difference between the normal font's character height and
		// the xbox font character height.  Scale the character width by this amount,
		// so that the xbox characters don't become distorted if the main font is smaller
		// than the xbox font.
		if ( XboxYL > 0 && YL > 0 )
			StaticXboxFontRatio = YL / XboxYL;
		else StaticXboxFontRatio = 1.f;
		LineBreak_SetCallback( GetCharWidthXBox, NULL );
	}
	else
	{
		StaticXboxFontRatio = 1.f;
        LineBreak_SetCallback( GetCharWidthW, NULL );
	}

	const WCHAR *pEOL		= NULL;
	const WCHAR *pNextLine	= NULL;

	INT wLength = 2 * (appStrlen(Text)+1);
	const WCHAR* Temp = appToUnicode(Text, (WCHAR*)appAlloca(wLength));

	do
	{
		// Loop through start of text
		// and add an empty line to the array for each
		// starting EndOfLine (EOL) marker
		while( *Temp == EOL )
		{
			INT iAdded = OutArray->AddZeroed();

			(*OutArray)(iAdded) = FString(TEXT(""));
			Temp++;
		}

		// If nothing after EOL markers, break loop
		if( *Temp == 0 )
			break;

		// Get the pointer to the next line given
		// The start of the first line and the width of the line
		pNextLine = WordWrap_FindNextLineW( Temp, Width, (LPCWSTR*)&pEOL, EOL );

		// If there is a valid last character
		if( pEOL ) 
		{
			// The text count to use is the offset from the front
			INT Count	 = (pEOL + 1) - Temp;

			// Add space to the array
			INT AddedIdx = OutArray->AddZeroed();
			FString& NewString = (*OutArray)(AddedIdx);

			// And insert the left most characters as the line text
			TArray<TCHAR>& Ar = NewString.GetCharArray();
			Ar.Add(Count + 1);
			Ar(Count) = 0;
			for ( INT i = 0; i < Count; i++ )
				Ar(i) = FromUnicode(Temp[i]);
		}

		// Update text to be the start of the next line
		Temp = pNextLine;
	} while( Temp );

	StaticWrapScale = 1.f;
	StaticWrapButtonFont = NULL;
	bStaticUseButtonFont = false;

	unguardSlow;
}

void UCanvas::execWrapStringToArray( FFrame& Stack, RESULT_DECL )
{
	guard(UCanvas::execWrapStringToArray);
	P_GET_STR(InText);
	P_GET_TARRAY_REF(OutArray, FString);
	P_GET_FLOAT(Width);
	P_GET_FLOAT_OPTX(ScaleX,1.f);
	P_GET_STR_OPTX(EOL, TEXT("|"));
	P_FINISH;

	WrapStringToArray(*InText, OutArray, Width, Font, ScaleX, EOL[0]);

	unguard;
}
IMPLEMENT_FUNCTION( UCanvas, -1, execWrapStringToArray );

//
// Compute size and optionally print text with word wrap.
//!!For the next generation, redesign to ignore CurX,CurY.
//
void UCanvas::WrappedPrint( ERenderStyle Style, FLOAT& XL, FLOAT& YL, UFont* Font, FLOAT ScaleX, FLOAT ScaleY, UBOOL Center, const TCHAR* Text )
{
	guard(UCanvas::WrappedPrint);

	XL = YL = 0;

	if( ClipX<0 || ClipY<0 )
		return;
	//scion rwd - instead of assert, just set a default font
	if (Font == NULL)
	{
		Font = FallbackFont;
		checkSlow(Font);
	}
	FPlane DrawColor = Color.Plane();

	// Process each word until the current line overflows.
	do
	{
		INT iCleanWordEnd=0, iTestWord;
		FLOAT TestXL=CurX, CleanXL=0;
		FLOAT TestYL=0,    CleanYL=0;
		UBOOL GotWord=0;

		FLOAT CharIncrement = SpaceX + Font->Kerning;
		for( iTestWord=0; Text[iTestWord]!=0 && Text[iTestWord]!='\n'; )
		{
			FLOAT ChW, ChH;
			Font->GetCharSize(Text[iTestWord], ChW, ChH);

			if ( Text[iTestWord+1] )
				ChW += CharIncrement;

			TestXL += ChW * ScaleX;
			TestYL  = Max(TestYL, (ChH + SpaceY) * ScaleY);

			if( TestXL>ClipX )
				break;

			iTestWord++;
			UBOOL WordBreak = Text[iTestWord]==' ' || Text[iTestWord]=='\n' || Text[iTestWord]==0;
			if( WordBreak || !GotWord )
			{
				iCleanWordEnd = iTestWord;
				CleanXL       = TestXL;
				CleanYL       = TestYL;
				GotWord       = GotWord || WordBreak;
			}
		}

		if( iCleanWordEnd==0 )
			break;

		// Sucessfully split this line, now draw it.
		if( Style!=STY_None && OrgY+CurY<Viewport->SizeY && OrgY+CurY+CleanYL>0 )
		{
			FString TextLine(Text);
			FLOAT LineX = Center ? CurX + (ClipX-CleanXL) / 2 : CurX;
			LineX += DrawString( this, Font, ScaleX, ScaleY, LineX, CurY, *(TextLine.Left(iCleanWordEnd)), DrawColor, 0, 0 );
			CurX = LineX;
		}

		// Update position.
		CurX  = 0;
		CurY += CleanYL;
		YL   += CleanYL;
		XL    = Max(XL,CleanXL);
		Text += iCleanWordEnd;

		// Skip whitespace after word wrap.
		while( *Text==' ' )
			Text++;
	}
	while( *Text );

	unguardf(( TEXT("(%s)"), Text ));
}

/*-----------------------------------------------------------------------------
	UCanvas derived text functions.
-----------------------------------------------------------------------------*/

//
// Calculate the size of a string built from a font, word wrapped
// to a specified region.
//
void VARARGS UCanvas::WrappedStrLenf( UFont* Font, FLOAT& XL, FLOAT& YL, const TCHAR* Fmt, ... )
{
	TCHAR Text[4096];
	GET_VARARGS( Text, ARRAY_COUNT(Text), Fmt, Fmt );

	guard(UCanvas::WrappedStrLenf);
	WrappedPrint( STY_None, XL, YL, Font, 1.0F, 1.0F, 0, Text );
	unguard;
}

//
// Wrapped printf.
//
void VARARGS UCanvas::WrappedPrintf( UFont* Font, UBOOL Center, const TCHAR* Fmt, ... )
{
	TCHAR Text[4096];
	GET_VARARGS( Text, ARRAY_COUNT(Text), Fmt, Fmt );

	guard(UCanvas::WrappedPrintf);
	FLOAT XL=0, YL=0;
	WrappedPrint( STY_Normal, XL, YL, Font, 1.0F, 1.0F, Center, Text );
	unguard;
}

//
// Calculate the size of a string built from a font, word wrapped
// to a specified region.
//
void VARARGS UCanvas::WrappedStrLenf( UFont* Font, FLOAT ScaleX, FLOAT ScaleY, FLOAT& XL, FLOAT& YL, const TCHAR* Fmt, ... )
{
	TCHAR Text[4096];
	GET_VARARGS( Text, ARRAY_COUNT(Text), Fmt, Fmt );

	guard(UCanvas::WrappedStrLenf);
	WrappedPrint( STY_None, XL, YL, Font, ScaleX, ScaleY, 0, Text );
	unguard;
}

//
// Wrapped printf.
//
void VARARGS UCanvas::WrappedPrintf( UFont* Font, FLOAT ScaleX, FLOAT ScaleY, UBOOL Center, const TCHAR* Fmt, ... )
{
	TCHAR Text[4096];
	GET_VARARGS( Text, ARRAY_COUNT(Text), Fmt, Fmt );

	guard(UCanvas::WrappedPrintf);
	FLOAT XL=0, YL=0;
	WrappedPrint( STY_Normal, XL, YL, Font, ScaleX, ScaleY, Center, Text );
	unguard;
}

void UCanvas::ClippedStrLen( UFont* Font, FLOAT ScaleX, FLOAT ScaleY, FLOAT& XL, FLOAT& YL, const TCHAR* Text )
{
	XL = 0;
	YL = 0;

	if (Font == NULL)
		Font = this->Font;

	if (Font == NULL)
		return;

	FLOAT CharIncrement = SpaceX + Font->Kerning;
	FLOAT TotalXL(0.f), TotalYL(0.f);

	for( INT i=0; Text[i]; i++)
	{
    	FLOAT W, H;
		Font->GetCharSize( Text[i], W, H );

		if( Text[i + 1] )
	        W += CharIncrement;

		TotalXL += W * ScaleX;

		if ( TotalYL < H * ScaleY )
			TotalYL = H * ScaleY;
	}

	XL = TotalXL;
	YL = TotalYL;
}

void UCanvas::ClippedPrint( UFont* Font, FLOAT ScaleX, FLOAT ScaleY, UBOOL Center, const TCHAR* Text )
{
	FPlane DrawColor = Color.Plane();
	DrawString( this, Font, ScaleX, ScaleY, CurX, CurY, Text, DrawColor, 1, 0 );
}

/*-----------------------------------------------------------------------------
	UCanvas object functions.
-----------------------------------------------------------------------------*/

void UCanvas::Init( UViewport* InViewport )
{
	guard(UCanvas::UCanvas);
	Viewport = InViewport;

	//scion rwd - load a default font
	Font = Cast<UFont>(StaticLoadObject(UFont::StaticClass(),NULL,DEFAULT_FONT,NULL,LOAD_NoWarn,NULL));
	FallbackFont = Font;
	checkSlow(Font);

	if ( !GIsEditor )
	{
		// Load default fonts
		TinyFont	= Cast<UFont>( StaticLoadObject( UFont::StaticClass(), NULL, *TinyFontName, NULL, LOAD_NoWarn, NULL ) );
		if( !TinyFont )
		{
			warnf(TEXT("Could not load stock Tiny font %s"), *TinyFontName);
			TinyFont=FindObjectChecked<UFont>(ANY_PACKAGE,TEXT("DefaultFont"));
		}
		SmallFont	= Cast<UFont>( StaticLoadObject( UFont::StaticClass(), NULL, *SmallFontName, NULL, LOAD_NoWarn, NULL ) );
		if( !SmallFont )
		{
			warnf(TEXT("Could not load stock Small font %s"), *SmallFontName);
			SmallFont=FindObjectChecked<UFont>(ANY_PACKAGE,TEXT("DefaultFont"));
		}
		MedFont		= Cast<UFont>( StaticLoadObject( UFont::StaticClass(), NULL, *MedFontName, NULL, LOAD_NoWarn, NULL ) );
		if( !MedFont )
		{
			warnf(TEXT("Could not load stock Medium font %s"), *MedFontName);
			MedFont=FindObjectChecked<UFont>(ANY_PACKAGE,TEXT("DefaultFont"));
		}

		XBoxButtonFont = Cast<UFont>(StaticLoadObject(UFont::StaticClass(),NULL,*XBoxButtonFontName,NULL,LOAD_NoWarn,NULL));
		checkSlow(XBoxButtonFont);
		// The button font's in-game mark (c|0x80 draws c), UnKeyGlyphs.h
		GKeyGlyphs.PrepareButtonFont( XBoxButtonFont );
	}
	unguard;
}
void UCanvas::Update()
{
	guard(UCanvas::Update);

	// Call UnrealScript to reset.
	eventReset();

	// The key glyphs look at the bindings again (they may have changed since the last frame)
	GKeyGlyphs.NewFrame();

	if ( !pCanvasUtil )
		pCanvasUtil = NEW FCanvasUtil(&Viewport->RenderTarget,Viewport->RI);

	// The client's UIScaleMode (never in the editor).
	UClient* Client = Viewport->GetOuterUClient();
	ApplyUIBox( !GIsEditor && Client && Client->UIScaleMode == UISCALE_Fit43 );

	pCanvasUtil->UIContext = UICONTEXT_Game;
    pCanvasUtil->RI = Viewport->RI;

	unguard;
}

/*-----------------------------------------------------------------------------
	The UI's 640x480 design space in a centred box.

	UC2 lays out every menu and HUD element in 640x480 pixels (the Xbox's screen). With UIScaleMode=Fit43 the
	viewport canvas presents that space to script and to the GUI (ClipX/ClipY/SizeX/SizeY = 640x480) and its
	CanvasToScreen puts it into the largest 4:3 rectangle of the viewport, centred: scale
	s = min(W/640, H/480), origin ((W-640s)/2, (H-480s)/2) floored to whole pixels. The 3D world is not drawn
	through the canvas and keeps the whole viewport. At 640x480 this is the identity and the code below takes
	Epic's path. A split-screen viewport gets 640 x 480*ScaleY (the shape UC2's split
	HUD is laid out for, see below) fitted the same way into its own rectangle.
-----------------------------------------------------------------------------*/

void UCanvas::ApplyUIBox( UBOOL bWanted )
{
	guard(UCanvas::ApplyUIBox);

	if( !pCanvasUtil )
		return;

	FLOAT W = Viewport->SizeX, H = Viewport->SizeY;
	FLOAT DW = 640.f, DH = 480.f;
	// A split-screen viewport's design space is 640 wide and its share of 480 high. UC2's split HUD
	// (UCHUD_Split, WeaponSelectorSplit.huc, UCHUD.SetSplitPosition) is laid out for the Xbox's top/bottom
	// halves, 640x240, and knows no other shape ("if we are going to support more than 2 split-screen, this
	// function needs to be adjusted"); a quarter of the screen therefore shows that 640x240 layout fitted
	// into its rectangle, reticle in its centre, instead of the left half of it (640*ScaleX).
	if( Viewport->ScaleY > 0.f && Viewport->ScaleY < 1.f )
		DH *= Viewport->ScaleY;

	FLOAT S = 1.f, BX = 0.f, BY = 0.f;
	if( bWanted && W > 0.f && H > 0.f )
	{
		S  = Min( W / DW, H / DH );
		BX = appFloor( (W - DW * S) * 0.5f );
		BY = appFloor( (H - DH * S) * 0.5f );
	}
	// The identity (a 640x480 window, or UIScaleMode=Native640): Epic's Update().
	UBOOL bBoxed = bWanted && W > 0.f && H > 0.f && !( S == 1.f && BX == 0.f && BY == 0.f && DW == W && DH == H );

	pCanvasUtil->bUIBoxed	= bBoxed;
	pCanvasUtil->UIScale	= bBoxed ? S : 1.f;
	pCanvasUtil->UIOriginX	= bBoxed ? BX : 0.f;
	pCanvasUtil->UIOriginY	= bBoxed ? BY : 0.f;
	pCanvasUtil->UITargetX	= W;
	pCanvasUtil->UITargetY	= H;

	if( !bBoxed )
	{
		// Copy size parameters from viewport.
		ClipX = Viewport->SizeX;
		SizeX = appTrunc( ClipX );
		ClipY = Viewport->SizeY;
		SizeY = appTrunc( ClipY );
	}
	else
	{
		ClipX = DW;
		SizeX = appTrunc( ClipX );
		ClipY = DH;
		SizeY = appTrunc( ClipY );
	}
	pCanvasUtil->CanvasToScreen = UICanvasToScreen();
	pCanvasUtil->ScreenToCanvas = pCanvasUtil->CanvasToScreen.Inverse();

	unguard;
}

FMatrix UCanvas::UICanvasToScreen() const
{
	if( !pCanvasUtil || !pCanvasUtil->bUIBoxed )
	{
		// offset top left corner to center of viewport (0,0)
		FVector OffsetVec( -ClipX * 0.5f - 0.5f, -ClipY * 0.5f - 0.5f, 0.0f );
		// scale back to canonical [-1,1]
		FVector ScaleVec( 2.0f / ClipX, -2.0f / ClipY, 1.0f );
		// translate to center then scale
		FTranslationMatrix	OffsetMat( OffsetVec );
		FScaleMatrix		ScaleMat( ScaleVec );
		return OffsetMat * ScaleMat;
	}
	// design pixels -> target pixels (scale, then the box's corner), then the same mapping as above on the
	// target's pixels (including Epic's half-pixel offset, which stays one target pixel wide)
	FLOAT W = pCanvasUtil->UITargetX, H = pCanvasUtil->UITargetY;
	return FScaleMatrix( FVector( pCanvasUtil->UIScale, pCanvasUtil->UIScale, 1.0f ) )
		 * FTranslationMatrix( FVector( pCanvasUtil->UIOriginX - W * 0.5f - 0.5f, pCanvasUtil->UIOriginY - H * 0.5f - 0.5f, 0.0f ) )
		 * FScaleMatrix( FVector( 2.0f / W, -2.0f / H, 1.0f ) );
}

UBOOL UCanvas::IsUIBoxed() const
{
	return pCanvasUtil && pCanvasUtil->bUIBoxed;
}

FLOAT UCanvas::GetUIScale() const
{
	return IsUIBoxed() ? pCanvasUtil->UIScale : 1.f;
}

void UCanvas::DesignToTarget( FLOAT& X, FLOAT& Y ) const
{
	if( IsUIBoxed() )
	{
		X = pCanvasUtil->UIOriginX + X * pCanvasUtil->UIScale;
		Y = pCanvasUtil->UIOriginY + Y * pCanvasUtil->UIScale;
	}
}

void UCanvas::TargetToDesign( FLOAT& X, FLOAT& Y ) const
{
	if( IsUIBoxed() )
	{
		X = (X - pCanvasUtil->UIOriginX) / pCanvasUtil->UIScale;
		Y = (Y - pCanvasUtil->UIOriginY) / pCanvasUtil->UIScale;
	}
}

void UCanvas::GetTargetInDesign( FLOAT& X1, FLOAT& Y1, FLOAT& X2, FLOAT& Y2 ) const
{
	X1 = 0.f;
	Y1 = 0.f;
	X2 = pCanvasUtil ? pCanvasUtil->UITargetX : (FLOAT)Viewport->SizeX;
	Y2 = pCanvasUtil ? pCanvasUtil->UITargetY : (FLOAT)Viewport->SizeY;
	TargetToDesign( X1, Y1 );
	TargetToDesign( X2, Y2 );
}

void UCanvas::SetUIContext( BYTE Context )
{
	if( pCanvasUtil )
		pCanvasUtil->UIContext = Context;
}

void UCanvas::NotifyLevelChange()
{
	guard(UCanvas::NotifyLevelChange);

	// Clear the reference to the fallback font, since it needs to be garbage collected during level changes
	if ( FallbackFont != NULL )
		FallbackFont = NULL;

	unguard;
}
/*-----------------------------------------------------------------------------
	UCanvas natives.
-----------------------------------------------------------------------------*/

void UCanvas::execStrLen( FFrame& Stack, RESULT_DECL )
{
	guard(UCanvas::execStrLen);

	P_GET_STR(InText);
	P_GET_FLOAT_REF(XL);
	P_GET_FLOAT_REF(YL);
	P_FINISH;

	FLOAT OldCurX, OldCurY;
	OldCurX = CurX;
	OldCurY = CurY;

	CurX = 0;
	CurY = 0;
	WrappedStrLenf( Font, 1.f, 1.f, *XL, *YL, TEXT("%s"), *InText );
	CurY = OldCurY;
	CurX = OldCurX;

	unguardexec;
}
IMPLEMENT_FUNCTION( UCanvas, 464, execStrLen );

void UCanvas::execDrawText( FFrame& Stack, RESULT_DECL )
{
	guard(UCanvas::execDrawText);
	P_GET_STR(InText);
	P_GET_UBOOL_OPTX(CR,1);
	P_GET_FLOAT_OPTX(ScaleX,1.f);
	P_GET_FLOAT_OPTX(ScaleY,1.f);
	P_FINISH;

	if( Style == STY_None )
        return;

	//scion rwd - instead of assert, just set a default font
	if (Font == NULL)
	{
		Font = FallbackFont;
		if ( Font == NULL )
		{
			Font = FallbackFont = Cast<UFont>(StaticLoadObject(UFont::StaticClass(),NULL,DEFAULT_FONT,NULL,LOAD_NoWarn,NULL));
		}

		checkSlow(Font);
	}
	FLOAT XL=0, YL=0;
//	unclock(GScriptCycles);
//	clock(GStats.DWORDStats(GEngineStats.STATS_Game_CanvasCycles));
	if( Style!=STY_None )
		WrappedPrint( (ERenderStyle)Style, XL, YL, Font, ScaleX, ScaleY, bCenter, *InText );

//    unclock(GStats.DWORDStats(GEngineStats.STATS_Game_CanvasCycles));
//	clock(GScriptCycles);
	CurX += XL;
	CurYL = Max(CurYL,YL);
	if( CR )
	{
		CurX  = 0;
		CurY += CurYL;
		CurYL = 0;
	}

	unguardexec;
}
IMPLEMENT_FUNCTION( UCanvas, 465, execDrawText );

void UCanvas::execDrawTile( FFrame& Stack, RESULT_DECL )
{
	guard(UCanvas::execDrawTile);
	P_GET_OBJECT(UMaterial,Mat);
	P_GET_FLOAT(XL);
	P_GET_FLOAT(YL);
	P_GET_FLOAT(U);
	P_GET_FLOAT(V);
	P_GET_FLOAT(UL);
	P_GET_FLOAT(VL);
	P_GET_UBOOL_OPTX(bFlipUVsHoriz, 0);
	P_GET_UBOOL_OPTX(bFlipUVsVert, 0);
	P_GET_ROTATOR_OPTX(Transform, FRotator(0,0,0));
	P_FINISH;
	if( !Mat )
	{
		Stack.Logf( TEXT("DrawTile: Missing Material") );
		return;
	}
//	unclock(GScriptCycles);
//	clock(GStats.DWORDStats(GEngineStats.STATS_Game_CanvasCycles));
	if( Style!=STY_None )
	{
		if (Transform.IsZero())
		{
			DrawTile(Mat, OrgX + CurX, OrgY + CurY, XL, YL, U, V, UL, VL, CurZ, Color.Plane(), bFlipUVsHoriz, bFlipUVsVert);
		}
		else
		{
			FRotationMatrix TransformMat(Transform);
			DrawTile(Mat, OrgX + CurX, OrgY + CurY, XL, YL, U, V, UL, VL, CurZ, Color.Plane(), bFlipUVsHoriz, bFlipUVsVert, &TransformMat);
		}
	}
//	unclock(GStats.DWORDStats(GEngineStats.STATS_Game_CanvasCycles));
//	clock(GScriptCycles);
	CurX += XL + SpaceX;
	CurYL = Max(CurYL,YL);
	unguardexec;
}
IMPLEMENT_FUNCTION( UCanvas, 466, execDrawTile );

/* scion ======================================================================
* UCanvas::execDrawActorTile
* Author: sz
*
* see UCanvas::DrawActorTile
* Note that the current X,Y values are used to position the tile
* ============================================================================
*/
void UCanvas::execDrawActorTile( FFrame& Stack, RESULT_DECL )
{
	guard(UCanvas::execDrawActorTile);
	P_GET_OBJECT(AActor,Actor);
	P_GET_FLOAT(XL);
	P_GET_FLOAT(YL);
	P_GET_ROTATOR_OPTX(CameraRot, FRotator(0,0,0));
	P_GET_VECTOR_OPTX(CameraPos, FVector(100.f,0.f,0.f));
	P_GET_FLOAT_OPTX(DisplayFOV, 90.f);
	P_FINISH;

	DrawActorTile
	(
		Actor,
		OrgX+CurX,
		OrgY+CurY,
		XL,
		YL,
		CameraRot,
		CameraPos,
		DisplayFOV
	);

	unguardexec;
}
IMPLEMENT_FUNCTION( UCanvas, INDEX_NONE, execDrawActorTile );

/* scion ======================================================================
 * UCanvas::DrawActorTile
 * Author: sz
 *
 * Draws a 3D actor within a specified viewport region on the canvas.
 * This function works the same as DrawTile() except that an actor is passed
 * in as well as a camera distance and a display FOV.
 *
 * Inputs:
 * Actor - 3D actor to render
 * X,Y,XL,YL - (X,Y) origin offset, Width, and Height
 * CameraRot - Camera rotation (default looks down positive x-axis)
 * CameraPos - Camera position
 * DisplayFOV - Field of View
 * ============================================================================
 */
void UCanvas::DrawActorTile( AActor* Actor, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FRotator& CameraRot, FVector& CameraPos, FLOAT DisplayFOV )
{
	guard(UCanvas::DrawActor)

	if (!Actor || Actor->bDeleteMe)
	{
		return;
	}

	if ( !pCanvasUtil )
		return;

	// draw any pending canvas primitives
	pCanvasUtil->Flush();

	// save viewport state
	FVector		SavedLocation = Viewport->Actor->Location;
	FRotator	SavedRotation = Viewport->Actor->Rotation;
	FLOAT		SavedScaleX = Viewport->ScaleX,
				SavedScaleY = Viewport->ScaleY;
	INT			SavedSizeX = Viewport->SizeX,
				SavedSizeY = Viewport->SizeY,
				SavedOffsetX = Viewport->OffsetX,
				SavedOffsetY = Viewport->OffsetY;

	// set camera location/rotation and the size of the viewport
	Viewport->Actor->Location = CameraPos;
	Viewport->Actor->Rotation = CameraRot;
	// set viewport scale
	Viewport->ScaleX = 1.f;
	Viewport->ScaleY = 1.f;
	if( IsUIBoxed() )
	{
		// The tile is in design pixels; the 3D viewport needs the viewport's own
		FLOAT X2 = X + XL, Y2 = Y + YL;
		DesignToTarget( X, Y );
		DesignToTarget( X2, Y2 );
		Viewport->SizeX = appTrunc(X2 - X);
		Viewport->SizeY = appTrunc(Y2 - Y);
		Viewport->OffsetX = appTrunc(X)+SavedOffsetX;
		Viewport->OffsetY = appTrunc(Y)+SavedOffsetY;
	}
	else
	{
	// set viewport size
	Viewport->SizeX = appTrunc(XL*SavedScaleX);
	Viewport->SizeY = appTrunc(YL*SavedScaleY);
	// make sure to offset from the start of the current viewport
	Viewport->OffsetX = appTrunc(X*SavedScaleX)+SavedOffsetX;
	Viewport->OffsetY = appTrunc(Y*SavedScaleY)+SavedOffsetY;
	}

	// set viewport offset/scale
	Viewport->RI->SetViewport( Viewport->OffsetX, Viewport->OffsetY, Viewport->SizeX, Viewport->SizeY );	

	// z buffer clear (only affects the viewport area)
	Viewport->RI->Clear(0,FColor(0,0,0),1, 1.f,0);

	// setup a scene node and draw the actor with it
	Viewport->RI->SetCullMode( CM_CW );
	FActorSceneNode SceneNode
	(
		Viewport,
		&Viewport->RenderTarget,
		Actor,
		Viewport->Actor,
		Viewport->Actor->Location,
		Viewport->Actor->Rotation,
		DisplayFOV
	);
	SceneNode.Render(Viewport->RI);

	// restore viewport state
	Viewport->RI->ResetViewport();
	Viewport->Actor->Location = SavedLocation;
	Viewport->Actor->Rotation = SavedRotation;
	Viewport->ScaleX = SavedScaleX;
	Viewport->ScaleY = SavedScaleY;
	Viewport->SizeX = SavedSizeX;
	Viewport->SizeY = SavedSizeY;
	Viewport->OffsetX = SavedOffsetX;
	Viewport->OffsetY = SavedOffsetY;

	unguard;
}

void UCanvas::DrawActor( AActor* Actor, UBOOL WireFrame, UBOOL ClearZ, FLOAT DisplayFOV )
{
    guard(UCanvas::DrawActor)

    if ( pCanvasUtil )
        pCanvasUtil->Flush();

    if (!Actor || Actor->bDeleteMe)
	{
		return;
	}
	// AutoFOV, the canvas-drawn actor pass.
	// The first-person weapon is drawn here (UCWeapon.RenderOverlays: Canvas.DrawActor(self,false,true,
	// DisplayFOV), its own FOV, 60 by default) as a separate scene node, so the world's override does not
	// reach it: without this it would keep its 4:3 horizontal FOV while the world widened, and look bigger
	// and cut off at the bottom. Its placement (Pawn.CalcDrawOffset) divides by the weapon's DisplayFOV, not
	// by the player's FOV, so it needs no pinning.
	// A caller that gave no FOV (execDrawActor's default: combo effects, Inventory.RenderOverlays) means
	// the player's FOV: inside the frame that is the base value the override replaced, not the overridden
	// one (which would correct it twice), outside the frame the actor's own.
	{
		UBOOL InFrame = GAutoFovScopeActor && GAutoFovScopeActor == Viewport->Actor;
		if( DisplayFOV < 0.f )
			DisplayFOV = InFrame ? GAutoFovFrameBase : Viewport->Actor->FovAngle;
		INT FovX, FovY;
		ufrontAutoFovSize( Viewport, FovX, FovY );
		FLOAT DrawFOV = ufrontWeaponFov( DisplayFOV, ufrontEffectiveFov( DisplayFOV, FovX, FovY ) );
		if( InFrame && Actor->IsA(AWeapon::StaticClass()) )
			ufrontLogWeaponFov( FovX, FovY, DisplayFOV, DrawFOV );
		DisplayFOV = DrawFOV;
	}

	AActor*		CameraActor = Viewport->Actor;
	FVector		CameraLocation = Viewport->Actor->Location;
	FRotator	CameraRotation = Viewport->Actor->Rotation;

	Viewport->Actor->eventPlayerCalcView( 0.f, CameraActor,CameraLocation,CameraRotation,0);

//	unclock(GScriptCycles);
//	clock(GStats.DWORDStats(GEngineStats.STATS_Game_CanvasCycles));
	INT OldRendMap;
	OldRendMap = Viewport->Actor->RendMap;
	if( WireFrame )
		Viewport->Actor->RendMap = REN_Wire;
	if( ClearZ )
		Viewport->RI->Clear(0,FColor(0,0,0),1,1.0f,0);
	{
		FScopedSplitFovScale SplitFov( Viewport, DisplayFOV );	// The actor's FOV in a split-screen viewport
		FActorSceneNode(Viewport,&Viewport->RenderTarget,Actor,CameraActor,CameraLocation,CameraRotation,DisplayFOV).Render(Viewport->RI);
	}
	Viewport->Actor->RendMap = OldRendMap;
//	unclock(GStats.DWORDStats(GEngineStats.STATS_Game_CanvasCycles));
//	clock(GScriptCycles);


    unguard;
}

void UCanvas::execDrawActor( FFrame& Stack, RESULT_DECL )
{
	guard(UCanvas::execDrawActor);

	P_GET_OBJECT(AActor, Actor);
	P_GET_UBOOL(WireFrame);
	P_GET_UBOOL_OPTX(ClearZ, 0);
	// -1 = "not given" (the player's FOV); DrawActor resolves it (AutoFOV needs to know which).
	P_GET_FLOAT_OPTX(DisplayFOV,-1.f);
	P_FINISH;

    DrawActor( Actor, WireFrame, ClearZ, DisplayFOV );

	unguardexec;
}
IMPLEMENT_FUNCTION( UCanvas, 467, execDrawActor );

void UCanvas::execDrawTileClipped( FFrame& Stack, RESULT_DECL )
{
	guard(UCanvas::execDrawTileClipped);
	P_GET_OBJECT(UMaterial,Mat);
	P_GET_FLOAT(XL);
	P_GET_FLOAT(YL);
	P_GET_FLOAT(U);
	P_GET_FLOAT(V);
	P_GET_FLOAT(UL);
	P_GET_FLOAT(VL);
	P_FINISH;

	if( !Mat )
	{
		Stack.Logf( TEXT("DrawTileClipped: Missing Material") );
		return;
	}


	// Clip to ClipX and ClipY
	if( XL > 0 && YL > 0 )
	{		
		if( CurX<0 )
			{FLOAT C=CurX*UL/XL; U-=C; UL+=C; XL+=CurX; CurX=0;}
		if( CurY<0 )
			{FLOAT C=CurY*VL/YL; V-=C; VL+=C; YL+=CurY; CurY=0;}
		if( XL>ClipX-CurX )
			{UL+=(ClipX-CurX-XL)*UL/XL; XL=ClipX-CurX;}
		if( YL>ClipY-CurY )
			{VL+=(ClipY-CurY-YL)*VL/YL; YL=ClipY-CurY;}
	
//		unclock(GScriptCycles);
//		clock(GStats.DWORDStats(GEngineStats.STATS_Game_CanvasCycles));
		if( Style!=STY_None )
			DrawTile
			(
				Mat,
				OrgX+CurX,
				OrgY+CurY,
				XL,
				YL,
				U,
				V,
				UL,
				VL,
				CurZ,
				Color.Plane()
			);
//		unclock(GStats.DWORDStats(GEngineStats.STATS_Game_CanvasCycles));
//		clock(GScriptCycles);

		CurX += XL + SpaceX;
		CurYL = Max(CurYL,YL);
	}

	unguardexec;
}
IMPLEMENT_FUNCTION( UCanvas, 468, execDrawTileClipped );

void UCanvas::execDrawTextClipped( FFrame& Stack, RESULT_DECL )
{
	guard(UCanvas::execDrawTextClipped);
	P_GET_STR(InText);
	P_GET_UBOOL_OPTX(CheckHotKey, 0);
	P_GET_FLOAT_OPTX(ScaleX,1.f);
	P_GET_FLOAT_OPTX(ScaleY,1.f);
	P_FINISH;

	if( !Font )
	{
		Stack.Logf( TEXT("DrawTextClipped: No font") );
		return;
	}

	check(Font);

	FPlane DrawColor = Color.Plane();
//	unclock(GScriptCycles);
//	clock(GStats.DWORDStats(GEngineStats.STATS_Game_CanvasCycles));
	DrawString( this, Font, ScaleX, ScaleY, CurX, CurY, *InText, DrawColor, 1, CheckHotKey );
//	unclock(GStats.DWORDStats(GEngineStats.STATS_Game_CanvasCycles));
//	clock(GScriptCycles);

	unguardexec;
}
IMPLEMENT_FUNCTION( UCanvas, 469, execDrawTextClipped );

void UCanvas::execTextSize( FFrame& Stack, RESULT_DECL )
{
	guard(UCanvas::execTextSize);
	P_GET_STR(InText);
	P_GET_FLOAT_REF(XL);
	P_GET_FLOAT_REF(YL);
	P_GET_FLOAT_OPTX(ScaleX,1.f);
	P_GET_FLOAT_OPTX(ScaleY,1.f);
	P_FINISH;

  	//scion rwd - instead of assert, just set a default font
	if (Font == NULL)
	{
		Font = FallbackFont;
		if ( Font == NULL )
		{
			FallbackFont = Font = Cast<UFont>(StaticLoadObject(UFont::StaticClass(),NULL,DEFAULT_FONT,NULL,LOAD_NoWarn,NULL));
		}
		checkSlow(Font);
	}

	ClippedStrLen( Font, ScaleX, ScaleY, *XL, *YL, *InText );

	unguardexec;
}
IMPLEMENT_FUNCTION( UCanvas, 470, execTextSize );

// execDrawPortal
// Written by Andrew Scheidecker
// Edited by Brandon Reinhart
void UCanvas::execDrawPortal( FFrame& Stack, RESULT_DECL )
{
	guard(UCanvas::execDrawPortal);
	P_GET_INT(X);
	P_GET_INT(Y);
	P_GET_INT(Width);
	P_GET_INT(Height);
	P_GET_OBJECT(AActor,CamActor);
	P_GET_VECTOR(CamLocation);
	P_GET_ROTATOR(CamRotation);
	P_GET_INT_OPTX(FOV, 90);
	P_GET_UBOOL_OPTX(ClearZ, 1);
	P_FINISH;

	if( IsUIBoxed() )
	{
		// Design pixels -> the viewport's own (the portal is a 3D viewport and a clear in target pixels)
		FLOAT X1 = X, Y1 = Y, X2 = X + Width, Y2 = Y + Height;
		DesignToTarget( X1, Y1 );
		DesignToTarget( X2, Y2 );
		X = appFloor(X1); Y = appFloor(Y1); Width = appFloor(X2) - X; Height = appFloor(Y2) - Y;
	}

	// Save state.

//	unclock(GScriptCycles);
//	clock(GStats.DWORDStats(GEngineStats.STATS_Game_CanvasCycles));
	Viewport->RI->PushState();

	FLOAT	SavedFovAngle = Viewport->Actor->FovAngle;

	// Clear the portal viewport.

	FCanvasUtil(&Viewport->RenderTarget,Viewport->RI).DrawTile(
		X,Y,
		X + Width,Y + Height,
		0,0,
		0,0,
		1,
		NULL,
//!!MAT		PF_NoZTest,
		FColor(0,0,0)
		);

	Viewport->RI->SetViewport(X,Y,Width,Height);

	// Render the scene.

	FCameraSceneNode	SceneNode(Viewport,&Viewport->RenderTarget,CamActor,CamLocation,CamRotation,FOV);

	Viewport->Actor->FovAngle = FOV;

	SceneNode.Render(Viewport->RI);

	// Restore state.

	Viewport->Actor->FovAngle = SavedFovAngle;
	//scion jg -- Pop the viewport change
	Viewport->RI->ResetViewport();
	Viewport->RI->PopState();
//	unclock(GStats.DWORDStats(GEngineStats.STATS_Game_CanvasCycles));
//	clock(GScriptCycles);

	unguardexec;
}
IMPLEMENT_FUNCTION( UCanvas, 480, execDrawPortal );

void UCanvas::execDrawTileStretched( FFrame& Stack, RESULT_DECL )
{
	guard(UCanvas::execDrawTileStretched);

	P_GET_OBJECT(UMaterial,Mat);
	P_GET_FLOAT(AWidth);
	P_GET_FLOAT(AHeight);
	P_FINISH;

	DrawTileStretched(Mat,CurX, CurY, AWidth, AHeight);

	unguard;

}
IMPLEMENT_FUNCTION( UCanvas, -1, execDrawTileStretched );
void UCanvas::execDrawTileStretchedUV( FFrame& Stack, RESULT_DECL )
{
	guard(UCanvas::execDrawTileStretchedUV);

	P_GET_OBJECT(UMaterial,Mat);
	P_GET_FLOAT(AWidth);
	P_GET_FLOAT(AHeight);
	P_GET_FLOAT(U);
	P_GET_FLOAT(V);
	P_GET_FLOAT(UL);
	P_GET_FLOAT(VL);
	P_FINISH;

	DrawTileStretchedUV(Mat,CurX, CurY, AWidth, AHeight, U, V, UL, VL);

	unguard;

}
IMPLEMENT_FUNCTION( UCanvas, -1, execDrawTileStretchedUV );

void UCanvas::DrawTileStretched(UMaterial* Mat, FLOAT Left, FLOAT Top, FLOAT AWidth, FLOAT AHeight)
{

	guard(UCanvas::DrawTileStretched);

	CurX = Left;
	CurY = Top;

	if( !Mat )
	{
		debugf( TEXT("DrawTile: Missing Material") );
		return;
	}
	
	if (Style==NULL)
		return;

	// Get the size of the image

	FLOAT mW = Mat->MaterialUSize();
	FLOAT mH = Mat->MaterialVSize();

	DrawTileStretchedUV(Mat, Left, Top, AWidth, AHeight, 0, 0, mW, mH);
	unguard;
}
void UCanvas::DrawTileStretchedUV(UMaterial* Mat, FLOAT Left, FLOAT Top, FLOAT AWidth, FLOAT AHeight, 
								  FLOAT U, FLOAT V, FLOAT UL, FLOAT VL, FMatrix* pTransformMat )
{
	guard(UCanvas::DrawTileStretchedUV);

	CurX = Left;
	CurY = Top;

	if( !Mat )
	{
		debugf( TEXT("DrawTileStretchedUV: Missing Material") );
		return;
	}

	UBOOL bFlipUL = 0, bFlipVL = 0;
	if( UL < 0 )
	{
		U += UL;
		UL = -UL;
		bFlipUL = true;
	}

	if ( VL < 0 )
	{
		V += VL;
		VL = -VL;
		bFlipVL = true;
	}

	if (Style==NULL)
		return;

	// Get the size of the image

	FLOAT mW = UL;
	FLOAT mH = VL;

	// Get the midpoints of the image

	FLOAT MidX = mW/2.0f;
	FLOAT MidY = mH/2.0f;

	// Grab info about the scaled image
	FLOAT AbsWidth = fabs(AWidth);
	FLOAT AbsHeight = fabs(AHeight);

	FLOAT SmallTileW = appCopySign(AbsWidth - mW, AWidth);
	FLOAT SmallTileH = appCopySign(AbsHeight - mH, AHeight);

	FLOAT afX, afY;     // absolute values of fX and fY; these are calculated using known non-negative values and then copysign is used to
	FLOAT fX, fY;		// set the signed values here. afX is always used when refering into the image and fX to refer to the screen.

	// Draw the spans first

		// Top and Bottom

	if (mW<AbsWidth)
	{
		afX = MidX;

		if (mH>AbsHeight)
			afY = AbsHeight/2.f;
		else
			afY = MidY;
		
		fX = appCopySign(afX, AWidth);
		fY = appCopySign(afY, AHeight);
		DrawTile(Mat, CurX+fX,	CurY,					SmallTileW,		fY,		U+MidX,	V,			1,		afY,	0, Color, bFlipUL, bFlipVL, pTransformMat);
		DrawTile(Mat, CurX+fX,	CurY+AHeight-fY,		SmallTileW,		fY,		U+MidX,	V+mH-afY,	1,		afY,	0, Color, bFlipUL, bFlipVL, pTransformMat);
	}
	else
		afX = AbsWidth / 2.f;

		// Left and Right

	if (mH<AbsHeight)
	{
		afY = MidY;

		fX = appCopySign(afX, AWidth);
		fY = appCopySign(afY, AHeight);
		DrawTile(Mat, CurX,				CurY+fY,	fX,	SmallTileH,		U,			V+afY, afX,	1,	0,	Color, bFlipUL, bFlipVL, pTransformMat);
		DrawTile(Mat, CurX+AWidth-fX,	CurY+fY,	fX,	SmallTileH,		U+mW-afX,	V+afY, afX,	1,	0,	Color, bFlipUL, bFlipVL, pTransformMat);
	}
	else
		afY = AbsHeight / 2.f;

		// Center

	fX = appCopySign(afX, AWidth);
	fY = appCopySign(afY, AHeight);
	if ( (mH<AbsHeight) && (mW<AbsWidth) )
		DrawTile(Mat, CurX+fX, CurY+fY, SmallTileW, SmallTileH, U+afX, V+afY, 1, 1, 0, Color, bFlipUL, bFlipVL, pTransformMat);

	// Draw the 4 corners.

	DrawTile(Mat, CurX,				CurY,				fX, fY, U,			V,			afX, afY, 0,Color, bFlipUL, bFlipVL, pTransformMat);//topleft
	DrawTile(Mat, CurX+AWidth-fX,	CurY,				fX, fY,	U+mW-afX,	V,			afX, afY, 0,Color, bFlipUL, bFlipVL, pTransformMat);//topright
	DrawTile(Mat, CurX,				CurY+AHeight-fY,	fX, fY, U,			V+mH-afY,	afX, afY, 0,Color, bFlipUL, bFlipVL, pTransformMat);//bottomleft
	DrawTile(Mat, CurX+AWidth-fX,	CurY+AHeight-fY,	fX, fY,	U+mW-afX,	V+mH-afY,	afX, afY, 0,Color, bFlipUL, bFlipVL, pTransformMat);//bottomright

	unguard;
}


void UCanvas::execDrawTileScaled( FFrame& Stack, RESULT_DECL )
{
	guard(UCanvas::execDrawTileStretched);

	P_GET_OBJECT(UMaterial,Mat);
	P_GET_FLOAT(NewXScale);
	P_GET_FLOAT(NewYScale);
	P_FINISH;

	DrawTileScaled(Mat,CurX,CurY,NewXScale,NewYScale);

	unguard;

}

IMPLEMENT_FUNCTION( UCanvas, -1, execDrawTileScaled );


void UCanvas::DrawTileBound(UMaterial* Mat, FLOAT Left, FLOAT Top, FLOAT Width, FLOAT Height)
{
	guard(UCanvas::DrawTileBound);

	if (!Mat)
		debugf(TEXT("DrawTileBound: Missing Material"));

	if (Style==NULL)
		return;

	DrawTile( Mat, Left, Top, Width, Height, 0, 0, Width, Height, 0, Color);

	unguard;
}

void UCanvas::DrawTileScaleBound(UMaterial* Mat, FLOAT Left, FLOAT Top, FLOAT Width, FLOAT Height)
{
	guard(UCanvas::DrawTileScaleBound);

	if (!Mat)
		debugf(TEXT("DrawTileScaleBound: Missing Material"));

	if (Style==NULL)
		return;

	FLOAT mW = Mat->MaterialUSize();
	FLOAT mH = Mat->MaterialVSize();

	DrawTile( Mat, Left, Top, Width, Height, 0, 0, mW, mH, 0, Color);

	unguard;
}

IMPLEMENT_FUNCTION( UCanvas, -1, execDrawTileJustified );

void UCanvas::execDrawTileJustified( FFrame& Stack, RESULT_DECL )
{
	guard(UCanvas::execDrawTileJustified);
	P_GET_OBJECT(UMaterial,Mat);
	P_GET_BYTE(Just);
	P_GET_FLOAT(AWidth);
	P_GET_FLOAT(AHeight);
	P_FINISH;

	DrawTileJustified(Mat, CurX, CurY, AWidth, AHeight, Just);

	unguard;
}


// Justification is: 0 = left/top, 1 = Center, 2 = bottom/right
void UCanvas::DrawTileJustified(UMaterial* Mat, FLOAT Left, FLOAT Top, FLOAT Width, FLOAT Height, BYTE Justification)
{
	guard(UCanvas::DrawTileScaleBound);

	if (!Mat)
		debugf(TEXT("DrawTileScaleBound: Missing Material"));

	if (Style==NULL)
		return;

	FLOAT mW = Mat->MaterialUSize();
	FLOAT mH = Mat->MaterialVSize();

	if (mW <= 0.0 || mH <= 0.0)
		return;

	// Find scaling proportions
	FLOAT MatProps = mH/mW;
	FLOAT BndProps = Height/Width;

	if (MatProps == BndProps)
		DrawTile( Mat, Left, Top, Width, Height, 0, 0, mW, mH, 0, Color);
	else if (MatProps > BndProps)	// Stretch to fit Height
	{
	FLOAT NewWidth = Width * BndProps / MatProps;
	FLOAT NewLeft = Left;

		if (Justification == 1)			// Centered
			NewLeft += ((Width - NewWidth) / 2.0);
		else if (Justification == 2)	// RightBottom
			NewLeft += Width - NewWidth;

		DrawTile( Mat, NewLeft, Top, NewWidth, Height, 0, 0, mW, mH, 0, Color);
	}
	else							// Stretch to fit Width
	{
	FLOAT NewHeight = Height * MatProps / BndProps;
	FLOAT NewTop = Top;

		if (Justification == 1)			// Centered
			NewTop += ((Height - NewHeight) / 2.0);
		else if (Justification == 2)	// RightBottom
			NewTop += Height - NewHeight;

		DrawTile( Mat, Left, NewTop, Width, NewHeight, 0, 0, mW, mH, 0, Color);
	}
	unguard;
}

void UCanvas::DrawTileScaled(UMaterial* Mat, FLOAT Left, FLOAT Top, FLOAT NewXScale, FLOAT NewYScale)
{
	guard(UCanvas::DrawTileScaled);

	if( !Mat )
	{
		debugf( TEXT("DrawTile: Missing Material") );
		return;
	}
	
	if (Style==NULL)
		return;

	FLOAT mW = Mat->MaterialUSize();
	FLOAT mH = Mat->MaterialVSize();

	DrawTile( Mat, Left, Top, mW*NewXScale, mH*NewYScale, 0, 0, mW, mH, 0, Color);

	unguard;
}

IMPLEMENT_FUNCTION( UCanvas, -1, execDrawTextJustified );

void UCanvas::execDrawTextJustified( FFrame& Stack, RESULT_DECL )
{
	guard(UCanvas::execDrawTextJustified);
	P_GET_STR(InText);
	P_GET_BYTE(Just);
	P_GET_FLOAT(X1);
	P_GET_FLOAT(Y1);
	P_GET_FLOAT(X2);
	P_GET_FLOAT(Y2);
	P_FINISH;

	DrawTextJustified(Just,X1,Y1,X2,Y2,1.f,1.f,TEXT("%s"),*InText);

	unguard;
}


void VARARGS UCanvas::DrawTextJustified(BYTE Justification, FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, FLOAT ScaleX, FLOAT ScaleY, const TCHAR* Fmt, ... )
{
	guard(UCanvas::DrawTextJustified);
	
	TCHAR Text[4096];
	GET_VARARGS( Text, ARRAY_COUNT(Text), Fmt, Fmt );

	FLOAT XL,YL;
	CurX = 0;
	CurY = 0;
	ClippedStrLen(  Font, ScaleX, ScaleY, XL, YL, Text );

	// Adjust the Y so the Font is centered in the bounding box
	CurY = ((y2-y1) / 2) - (YL/2);

	if (Justification == 0)						// Left
		CurX = 0;
	else if (Justification == 1)				// Center
	{
		if( XL > x2-x1 ) // align left when there's no room
			CurX = 0;
		else
			CurX = ((x2-x1) / 2) - (XL/2);
	}
	else if (Justification == 2)				// Right
		CurX = (x2-x1) - XL;

	FLOAT OldClipX = ClipX;
	FLOAT OldClipY = ClipY;
	FLOAT OldOrgX = OrgX;
	FLOAT OldOrgY = OrgY;

	// Clip to box
	OrgX = x1;
	OrgY = y1;
	ClipX = x2-x1;
	ClipY = y2-y1;

	ClippedPrint(Font, ScaleX, ScaleY, false, Text);

	ClipX = OldClipX;
	ClipY = OldClipY;
	OrgX = OldOrgX;
	OrgY = OldOrgY;


	unguard;

}

/* epic ===============================================
* ::XBoxClippedStrLen
* Author: Ron Prestenback
*
* Optimized version of ClippedStrLen, with additional support
* for embedding Xbox controller button characters in the
* string.
* Any characters prefaced with STX (CHAR 2) are sized using the XBox
* controller button font.
======================================================*/
void UCanvas::XBoxClippedStrLen( UFont* DrawFont, FLOAT ScaleX, FLOAT ScaleY, FLOAT& XL, FLOAT& YL, const TCHAR* Text )
{
	guardSlow(XBoxClippedStrLen);

	XL = 0;
	YL = 0;

	if ( DrawFont == NULL || XBoxButtonFont == NULL )
		return;

	FLOAT CharIncrement = SpaceX + DrawFont->Kerning;
	const TCHAR* T = Text;
	FLOAT W = 0;
	FLOAT H = 0;
	FLOAT regXL = 0.f, XBoxXL = 0.f;
	FLOAT regYL = 0.f, XBoxYL = 0.f;

	while ( *T )
	{
		// If the next character is the STX character
		if ( *T == 2 )
		{
			// skip it, checking for termination
			if ( *++T == NULL )
				break;

			// then use the XBoxButtonFont to size the next character
			// (never add kerning to XBoxButtonFont)
			XBoxButtonFont->GetCharSize(*T, W, H);

			// add the character's width to the total xbox width
			// must apply scaling to each letter individually, since this is how it
			// will be done in the word-wrapping code
			XBoxXL += W * ScaleX;

			// if this is the tallest character, use its height
			if ( XBoxYL < H )
				XBoxYL = H;
		}
		else
		{
			// get the dimensions of the next character
			DrawFont->GetCharSize(*T, W, H);

			// If we have more characters, add the font kerning
			if ( *(T + 1) )
				W += CharIncrement;

			// add the character's width to the total width
			regXL += W;

			// if this is the tallest character, use its height
			if ( regYL < H )
				regYL = H;
		}

		// Advance the character pointer
		T++;
	}

	// make sure we know how much to scale the xbox characters
	if ( regYL == 0 )
	{
		FLOAT Unused;
		DrawFont->GetCharSize('A', Unused, regYL);
	}
	

	// If we had both XBoxButton characters and standard characters in the text,
	// figure out the difference between the normal font's character height and
	// the xbox font character height.  Scale the character width by this amount,
	// so that the xbox characters don't become distorted if the main font is smaller
	// than the xbox font.
	if ( XBoxYL > 0 && regYL > 0 )
		XBoxXL *= (regYL / XBoxYL);

	XL = (regXL + XBoxXL) * ScaleX;
	YL = regYL * ScaleY;

	unguardSlow;
}

/* epic ===============================================
* ::DrawXboxTextJustified
* Author: Ron Prestenback
*
* Identical to DrawTextJustified, with additional support
* for embedding Xbox controller button characters in the
* string.
* Any characters prefaced with (STX) are drawn using the XBox
* controller button font.
======================================================*/
void VARARGS UCanvas::DrawXboxTextJustified(BYTE Justification, FLOAT x1, FLOAT y1, FLOAT x2, FLOAT y2, FLOAT ScaleX, FLOAT ScaleY, BYTE XboxButtonAlpha, const TCHAR* Fmt, ... )
{
	guard(UCanvas::DrawXboxTextJustified);
	
	TCHAR Text[4096], *pText = Text;
	GET_VARARGS( Text, ARRAY_COUNT(Text), Fmt, Fmt );

	FLOAT XL,YL;
	CurX = 0;
	CurY = 0;

	// Get the length of the entire string, in order to figure out where to begin drawing
	XBoxClippedStrLen( Font, ScaleX, ScaleY, XL, YL, Text );

	switch ( Justification )
	{
	case 0:	// Left
		CurX = 0;
		break;

	case 1:	// Center
		if( XL > x2-x1 ) // align left when there's no room
			CurX = 0;
		else
			CurX = ((x2-x1) / 2) - (XL/2);

		break;

	case 2:	// Right
		CurX = (x2 - x1) - XL;
		break;
	}

	FLOAT OldClipX = ClipX;
	FLOAT OldClipY = ClipY;
	FLOAT OldOrgX = OrgX;
	FLOAT OldOrgY = OrgY;

	// Clip to box
	OrgX = x1;
	OrgY = y1;
	ClipX = x2-x1;
	ClipY = y2-y1;
	FLOAT MidY = (y2 - y1) / 2;

	BYTE PrevAlpha = Color.A; 

	// Search the string for the XBox control character.
	TCHAR* XboxChar = appStrchr(pText, 2);
	while ( XboxChar )
	{
		FLOAT FontYL;

		// advance past and terminate the STX code
		*XboxChar++ = 0;

		// Get the dimensions of the first part of the string which is using the standard font
		ClippedStrLen(Font, ScaleX, ScaleY, XL, FontYL, pText);

		// Adjust the Y so the Font is centered in the bounding box
		CurY = Max<FLOAT>(0, MidY - FontYL / 2);

		Color.A = PrevAlpha;

		// Draw the part of the string that came before the STX code
		ClippedPrint(Font, ScaleX, ScaleY, 0, pText);

		// advance the current drawing position past the characters we just drew
		CurX += XL;

		TCHAR ButtonChar[2] = {0,0};
		*ButtonChar = *XboxChar++;

		// Get the dimensions of the next character using the xbox font.
		ClippedStrLen(XBoxButtonFont, ScaleX, ScaleY, XL, FontYL, ButtonChar);

		// Adjust the Y so the Font is centered in the bounding box
		CurY = Max<FLOAT>(0, MidY - FontYL / 2);
		
		// Next, draw the xbox button character
		// The two fonts might be different sizes, so adjust x scaling so that the character
		// doesn't appear distorted when it's drawn.
		FLOAT OffsetScale = YL / FontYL;

		// always use full alpha for xbox button characters
		Color.A = (FLOAT)XboxButtonAlpha * ForcedAlpha;
		ClippedPrint( XBoxButtonFont, ScaleX * OffsetScale, ScaleY * OffsetScale, false, ButtonChar );

		// advance the current drawing position past the characters we just drew
		CurX += XL * OffsetScale;
		pText = XboxChar;

		// Search for any additional STX codes
		XboxChar = appStrchr(pText, 2);
	}

	// Adjust the Y so the Font is centered in the bounding box
	CurY = Max<FLOAT>(0, MidY - YL / 2);

	Color.A = PrevAlpha;
	ClippedPrint(Font, ScaleX, ScaleY, false, pText);

	ClipX = OldClipX;
	ClipY = OldClipY;
	OrgX = OldOrgX;
	OrgY = OldOrgY;


	unguard;
}

//scion rwd - canvas matrix support
void UCanvas::execSetCanvasMatrix(FFrame& Stack,RESULT_DECL)
{
	P_GET_VECTOR_OPTX(newScale,FVector(1,1,1));
	P_GET_ROTATOR_OPTX(newRotation,FRotator(0,0,0));
	P_FINISH;

	// just rebuild the util matrices
	if (pCanvasUtil != NULL)
	{
		pCanvasUtil->Flush();
		if( IsUIBoxed() )
		{
			// The same rotation and clip-space scale, composed with the box's mapping
			pCanvasUtil->CanvasToScreen = FRotationMatrix(newRotation) * UICanvasToScreen() *
										   FScaleMatrix(newScale);
		}
		else
		pCanvasUtil->CanvasToScreen = (FRotationMatrix(newRotation) *
									   FTranslationMatrix(FVector(-SizeX / 2.0f - 0.5f,-SizeY / 2.0f - 0.5f,0.0f)) *
									   FScaleMatrix(FVector(2.0f / SizeX,-2.0f / SizeY,1.0f) * newScale));
		pCanvasUtil->ScreenToCanvas = pCanvasUtil->CanvasToScreen.Inverse();
	}
	else
	{
		debugf(NAME_Error,TEXT("Missing canvas util for execSetCanvasMatrix()"));
	}
}
IMPLEMENT_FUNCTION(UCanvas,-1,execSetCanvasMatrix);

//scion rwd - force a flush of the util
void UCanvas::execForceFlush(FFrame& Stack,RESULT_DECL)
{
	P_FINISH;
	if (pCanvasUtil != NULL)
	{
		pCanvasUtil->Flush();
	}
}
IMPLEMENT_FUNCTION(UCanvas,-1,execForceFlush);

//scion bcl -- as above but called from C++
void UCanvas::Flush()
{
	if (pCanvasUtil != NULL)
	{
		pCanvasUtil->Flush();
	}
}

/* scion ======================================================================
* UCanvas::execSetCanvasMask
* Author: sz
*
* see UCanvas::SetCanvasMask
* ============================================================================
*/
void UCanvas::execSetCanvasMask( FFrame& Stack, RESULT_DECL )
{
	guard(UCanvas::execSetCanvasMaskRegion);
	P_GET_FLOAT(X1);
	P_GET_FLOAT(Y1);
	P_GET_FLOAT(X2);
	P_GET_FLOAT(Y2);
	P_GET_FLOAT_OPTX(Z,0.f);
	P_FINISH;

	SetCanvasMask( X1, Y1, X2, Y2, Z );

	unguardexec;
}
IMPLEMENT_FUNCTION( UCanvas, INDEX_NONE, execSetCanvasMask );

/* scion ======================================================================
* UCanvas::SetCanvasMask
* Author: sz
*
* Clear all parts of the screen to the max depth value and then set the mask
* region to the Z value specified.  This allows any draw calls that follow
* to only show up in the masked region as long as their depth values are 
* smaller than the Z specified.
* ============================================================================
*/
void UCanvas::SetCanvasMask( FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2, FLOAT Z )
{
	guard(UCanvas::SetCanvasMask);

	if ( !pCanvasUtil )
		return;

	// draw any pending canvas primitives
	pCanvasUtil->Flush();

	// z buffer clear of entire viewport to the closest z value possible (z=0)
	const FLOAT CLOSEST_Z = 0.f;
	Viewport->RI->Clear(0,FColor(0,0,0),1,CLOSEST_Z,1);

	// constant color material
	DECLARE_STATIC_UOBJECT( UConstantColor, ConstantColor,{
		ConstantColor->Color = FColor(255,255,255,255);
	})

	// disable color writes since we only want to affect depth buffer values
	Viewport->RI->SetColorWriteMask( CWM_COLORWRITEENABLE_NONE );

	// draw tile to clear z values
	// z writes are enabled for STY_AlphaZ and color writes are disabled
	pCanvasUtil->DrawTile(
		X1, Y1,
		X2, Y2,
		0,0,1,1,
		Z/FAR_CLIPPING_PLANE,
		ConstantColor,
		ConstantColor->Color,
		STY_AlphaZ
	);
	pCanvasUtil->Flush();

	// re-enable color writes
	Viewport->RI->SetColorWriteMask( CWM_COLORWRITEENABLE_COLORONLY );
	
	// enable ztest on the canvas so pixels can be rejected
	// if they fall outside the masked region
	pCanvasUtil->bZTest = 1;

	unguard;
}

/* scion ======================================================================
* UCanvas::execClearCanvasMask
* Author: sz
*
* see UCanvas::ClearCanvasMask
* ============================================================================
*/
void UCanvas::execClearCanvasMask( FFrame& Stack, RESULT_DECL )
{
	guard(UCanvas::execClearCanvasMask);
	P_FINISH;

	ClearCanvasMask();

	unguardexec;
}
IMPLEMENT_FUNCTION( UCanvas, INDEX_NONE, execClearCanvasMask );

/* scion ======================================================================
* UCanvas::ClearCanvasMask
* Author: sz
*
* Clear Z values
* ============================================================================
*/
void UCanvas::ClearCanvasMask()
{
	guard(UCanvas::ClearCanvasMask);

	if( !pCanvasUtil )
		return;

	// draw any pending canvas primitives in masked region
	pCanvasUtil->Flush();

	// disable further z tests (reset to default)
	pCanvasUtil->bZTest = 0;

	// clear depth values again
	Viewport->RI->Clear(0,FColor(0,0,0),1,1.f,0);

	unguard;
}

/* epic ===============================================
* ::execDrawTextDualFont
* Author: moelfke
*
* Draws Text using two fonts. Any words with a %
* before them are drawn using SecondaryFont, all others
* using the font specified by the Canvas's Font property.
* For drawing button graphics, etc. in the middle of text.
*
* If XL is specified, the text is word wrapped if a line
* exceeds that length. If EOL is specified, all instances
* of its first character are replaced with newlines in the
* drawn text.
*
* NOTE: The CurX and CurY properties of the Canvas are
* the starting point.
* =====================================================
*/
void UCanvas::execDrawTextDualFont(FFrame& Stack, RESULT_DECL)
{
	guard(UCanvas::execDrawTextDualFont);

	P_GET_STR(Text);
	P_GET_OBJECT(UFont, SecondaryFont);
	P_GET_FLOAT_OPTX(ScaleX, 1.f);
	P_GET_FLOAT_OPTX(ScaleY, 1.f);
	P_GET_FLOAT_OPTX(XL, 0.f);
	P_GET_STR_OPTX(EOL, TEXT(""));
	P_FINISH;

	if (Style != STY_None)
	{
		// if we don't have a primary font, load a default one
		if (Font == NULL)
		{
			Font = FallbackFont;
			if ( Font == NULL )
			{
                FallbackFont = Font = Cast<UFont>(StaticLoadObject(UFont::StaticClass(),NULL,DEFAULT_FONT,NULL,LOAD_NoWarn,NULL));
			}
			checkSlow(Font);
		}
		FPlane DrawColor = Color.Plane();

		const TCHAR* TextArray = *Text;
		TCHAR EOLChar = (*EOL)[0];

		FLOAT YLTest, XLTest;
		Font->GetCharSize('A', XLTest, YLTest);
		FLOAT YL = FLOAT(YLTest);

		FLOAT StartingX = CurX;
		FLOAT EdgeX = CurX + XL; // right edge of each line
		UFont* PrimaryFont = Font; // remember primary font for switching between it and secondary font

		while (*TextArray != 0)
		{
			// process newline characters
			while (*TextArray == '\n' || *TextArray == EOLChar)
			{
				CurY += YL;
				CurX = StartingX;
				TextArray++;
			}

			FString Word;
			// if this word starts with a %
			if (*TextArray == '%')
			{
				// use the secondary font for this word, and skip the % character
				Font = SecondaryFont;
				TextArray++;
			}
			else
			{
				// otherwise use the primary font
				Font = PrimaryFont;
			}

			// process characters until we hit whitespace
			FLOAT CurrentXL = 0.f;
			FLOAT MaxYL = 0;
			while (*TextArray != 0 && *TextArray != ' ' && *TextArray != '\n' && *TextArray != EOLChar)
			{
				// get the size of the character, add the size to our current length
				FLOAT CharW, CharH;
				Font->GetCharSize(*TextArray, CharW, CharH);
				MaxYL = Max<FLOAT>(MaxYL, CharH * ScaleY);
				CurrentXL += (FLOAT(CharW) + SpaceX) * ScaleX;
				// hack: don't use secondary font's kerning value because for the fonts we're using it looks really bad
				if (Font == PrimaryFont)
				{
					CurrentXL += Font->Kerning * ScaleX;
				}
				// add it to the word
				Word += *TextArray;
				// advance to the next character
				TextArray++;
			}
			
			UBOOL bNeedToWrap = (XL > 0.f && CurX + CurrentXL > EdgeX);

			// process spaces after the word (always use the primary font for spaces so spacing is consistent)
			if (Font == PrimaryFont)
			{
				while (*TextArray == ' ')
				{
					FLOAT CharW, CharH;
					Font->GetCharSize(*TextArray, CharW, CharH);
					FLOAT SpaceXL = (FLOAT(CharW) + SpaceX + Font->Kerning) * ScaleX;
					// only include the space if it won't change whether or not we have to move this word to the next line
					if (XL <= 0.f || bNeedToWrap || CurX + CurrentXL + SpaceXL <= EdgeX)
					{
						Word += *TextArray;
						CurrentXL += SpaceXL;
					}
					TextArray++;
				}
			}
			
			// move this word to the next line, if necessary
			if (bNeedToWrap)
			{
				CurY += YL;
				CurX = StartingX;
			}

			// if using the secondary font, scale it so it's not too much bigger than the primary font
			FLOAT FinalScaleX = ScaleX;
			FLOAT FinalScaleY = ScaleY;
			FLOAT OldCurY = CurY;
			if (Font != PrimaryFont && MaxYL > YL * 1.5)
			{
				FLOAT ScalingFactor = FLOAT(YL * 1.5) / FLOAT(MaxYL);
				FinalScaleX *= ScalingFactor;
				FinalScaleY *= ScalingFactor;
				CurrentXL *= ScalingFactor;
				// center it in the space available
				CurY -= (FLOAT(MaxYL) * ScalingFactor - FLOAT(YL)) / 2.f;
			}
			// draw the word
			DrawString(this, Font, FinalScaleX, FinalScaleY, CurX, CurY, *Word, DrawColor, false, false);
			CurX += CurrentXL;
			CurY = OldCurY;
		}

		CurYL = Max(CurYL, YL);
		Font = PrimaryFont;
	}

	unguardexec;
}
IMPLEMENT_FUNCTION(UCanvas,-1,execDrawTextDualFont);

IMPLEMENT_CLASS(UCanvas);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

