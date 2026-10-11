// =====================================================================================================================
// KeyGlyphs (H-5, script set key-glyphs): player one's keys in place of the pad's button glyphs.
//
// The engine draws, for player one while they use the keyboard or the mouse, the key that does what a pad button does
// in place of that button's glyph (Source/Engine/Src/UnKeyGlyphs.cpp). It does so only when this class exists: a
// shipped GUI.u keeps the Xbox glyphs. [GUI.KeyGlyphs] ShowKeys=False in the system ini turns it off.
//
// A menu names the menus' buttons; a text that describes the game's buttons inside a menu (the combo instructions of
// the character pages) is marked with InGame, and the engine then shows the key bound to that button's command in the
// game. docs/porting/analysis/h5-key-glyphs.md.
// =====================================================================================================================
class KeyGlyphs extends Object
	abstract;

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
