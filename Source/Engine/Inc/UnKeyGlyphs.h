/*=============================================================================
	UnKeyGlyphs.h: H-5, player one's keys in place of the pad's button glyphs.

	The menus and the HUD show Xbox pad buttons (the button font T_Fonts2.Controller_Buttons_LG, a
	character per button). When player one (the window's viewport, the only one with the keyboard and
	the mouse: G-11) last touched the keyboard or the mouse, every button glyph drawn for them shows the
	key that does what the button does now:
	  - in a menu (the GUI controller draws: UICONTEXT_Menu) the key the menus take for that button
	    ([GUI.EmulatedJoypad], asked through MenuKey, which the GUI module sets);
	  - in the game (the HUD: UICONTEXT_Game), and for a glyph character with the high bit set (the
	    key-glyphs script set marks the in-game buttons that menus describe, e.g. the combo
	    instructions), the keyboard or mouse key bound to the command the pad button is bound to now
	    (UInput::Bindings of player one's viewport, so a rebinding is followed at once).
	The glyph's cell is kept (the same advance width and height): no layout moves. Phase 1 draws the key's
	name in the UI font; phase 2 a key cap from the sheet GUI.KeyGlyphSheet when the script set has one.
	It exists only with the script set key-glyphs (the class GUI.KeyGlyphs): a shipped GUI.u keeps the
	Xbox glyphs. [GUI.KeyGlyphs] ShowKeys=False turns it off. docs/porting/analysis/h5-key-glyphs.md.
=============================================================================*/

// Pad buttons as the glyphs name them: 0..15 are Joy1..Joy16 (XBC_A..XBC_RightThumb), then the sticks.
enum EKeyGlyphPad
{
	KGP_A = 0, KGP_B, KGP_X, KGP_Y, KGP_Black, KGP_White, KGP_LeftTrigger, KGP_RightTrigger,
	KGP_PadUp, KGP_PadDown, KGP_PadLeft, KGP_PadRight, KGP_Start, KGP_Back, KGP_LeftThumb, KGP_RightThumb,
	KGP_LeftStick, KGP_RightStick,
	KGP_MAX
};

// What a glyph resolves to: a key (EInputKey, 1..254) or one of these.
enum EKeyGlyphSpecial
{
	KGS_None	= -1,	// keep the pad glyph
	KGS_Arrows	= 256,	// the arrow keys (the menus' left stick)
	KGS_Move	= 257,	// the movement keys (the game's left stick)
	KGS_Mouse	= 258,	// the mouse (the game's right stick: look)
	KGS_NumPad	= 259,	// the number pad's 8/2/4/6 (the menus' right stick)
};

class ENGINE_API FKeyGlyphs
{
public:
	FKeyGlyphs();

	// SDL2Drv: player one touched the keyboard or the mouse (bKeys) or their pad. What: for the log.
	void NoteDevice( UBOOL bKeys, const TCHAR* What );
	UBOOL KeysActive() const { return bKeys; }

	// The GUI module: the key the menus take for a pad button (KGP_*), or KGS_*.
	typedef INT (*FMenuKeyFunc)( UViewport* Viewport, INT Pad );
	FMenuKeyFunc MenuKey;
	// The GUI module: the localised glyph character of a pad button (GUIController.XBoxButtonChars).
	void SetGlyphChar( TCHAR Ch, INT Pad );

	// UCanvas::Init: the button font gets a glyph c|0x80 identical to c (the "in-game button" mark).
	void PrepareButtonFont( UFont* Font );
	// UCanvas::Update: a new frame (bindings may have changed).
	void NewFrame() { CacheFrame++; }

	// DrawString: draw player one's key in the cell of button glyph Ch (canvas pixels X,Y,W,H). False: draw
	// the glyph.
	UBOOL DrawKey( UCanvas* Canvas, TCHAR Ch, FLOAT X, FLOAT Y, FLOAT W, FLOAT H, FPlane Color );

	// What glyph Ch shows for this canvas (KGS_None: the pad glyph); MoveLabel gets KGS_Move's text.
	INT Resolve( UCanvas* Canvas, TCHAR Ch, FString* MoveLabel=NULL );
	// The text of a key (phase 1).
	FString Label( INT Key, UViewport* Viewport ) const;

private:
	UBOOL	bKeys;					// player one's last device: keyboard+mouse
	INT		Gate;					// 0 unknown, 1 the script set is there, 2 it is not (or ShowKeys=False)
	BYTE	CharPad[256];			// glyph character -> KGP_* + 1 (0: not a button)
	UFont*	LabelFont;
	DWORD	CacheFrame;
	DWORD	CachedFrame[2][KGP_MAX];
	INT		Cached[2][KGP_MAX];
	INT		Logged[2][KGP_MAX];		// the last resolution logged (a log line on every change)
	FString	MoveText;

	UBOOL Available();
	INT GameKey( UViewport* Viewport, INT Pad );
	INT MoveKeys( UViewport* Viewport );
};

ENGINE_API extern FKeyGlyphs GKeyGlyphs;

// The key preferred among several bound to one command (the keys page's order: mouse buttons, Space, the left
// hand's letters, the other letters, digits, named keys, F-keys, the wheel). Lower is better.
ENGINE_API INT KeyGlyphRank( INT Key );
// A key the keyboard or the mouse can press (not a pad key, not an axis).
ENGINE_API UBOOL KeyGlyphIsKey( INT Key );
// UnCanvas.cpp's DrawString (no clipping, no ampersands), for the labels.
ENGINE_API FLOAT KeyGlyphDrawString( UCanvas* Canvas, UFont* Font, FLOAT ScaleX, FLOAT ScaleY, FLOAT X, FLOAT Y, const TCHAR* Text, FPlane Color );

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
