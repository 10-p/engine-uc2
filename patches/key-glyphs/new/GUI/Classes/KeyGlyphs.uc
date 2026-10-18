// =====================================================================================================================
// KeyGlyphs (script set key-glyphs): player one's keys in place of the pad's button glyphs.
//
// The engine draws, for player one while they use the keyboard or the mouse, the key that does what a pad button does
// in place of that button's glyph (Source/Engine/Src/UnKeyGlyphs.cpp). It does so only when this class exists: a
// shipped GUI.u keeps the Xbox glyphs. [GUI.KeyGlyphs] ShowKeys=False in the system ini turns it off.
//
// A menu names the menus' buttons; a text that describes the game's buttons inside a menu (the combo instructions of
// the character pages) is marked with InGame, and the engine then shows the key bound to that button's command in the
// game.
// =====================================================================================================================
class KeyGlyphs extends Object
	abstract;

// Phase 2: the key caps and mouse icons the engine draws (GUI.KeyGlyphSheet: an 11x11 grid of 46-pixel cells, the
// glyph at +1,+1, 44x44; which key is in which cell: Source/Engine/Src/UnKeyGlyphsSheet.h). Both are made by
// tools/dev/make-key-glyphs.py. Without it the engine writes the key's name instead.
#exec TEXTURE IMPORT NAME=KeyGlyphSheet FILE=Textures\KeyGlyphSheet.tga MIPS=ON ALPHATEXTURE=1 UCLAMPMODE=CLAMP VCLAMPMODE=CLAMP

var Texture Sheet;		// keeps the sheet in the package (and loaded with the class)

// Marks every button glyph of S (the 0x02 code and its character) as a game button: the character gets the high bit.
static final function string InGame( string S )
{
	local int i;
	local string Out, C;

	for ( i = 0; i < Len(S); i++ )
	{
		C = Mid(S, i, 1);
		Out $= C;
		if ( Asc(C) == 2 && i + 1 < Len(S) )
		{
			i++;
			C = Mid(S, i, 1);
			if ( Asc(C) > 0 && Asc(C) < 128 )
				C = Chr(Asc(C) + 128);
			Out $= C;
		}
	}
	return Out;
}

defaultproperties
{
	Sheet=Texture'GUI.KeyGlyphSheet'
}
