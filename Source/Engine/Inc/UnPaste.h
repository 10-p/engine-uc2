/*=============================================================================
	UnPaste.h: pasted text, typed into the text field that has the keyboard.

	The client hands over the text of a paste (SDL2Drv: Ctrl+V or Shift+Insert with the system clipboard; on
	the web the page's paste event). It goes only to a text field, as typed characters go there: the console's
	line while the console is typing, else the menus' text field (the GUI module's part, GMenuPaste), each
	character as UEngine::Key delivers a typed one. With neither, nothing is typed.
=============================================================================*/

// The most characters one paste types: more than any text field of the game holds (the menus' 30 at most, the
// console's line shows fewer than this), and a bound on the work a clipboard of any size can cause.
#define PASTE_MAX_CHARACTERS 1024

// Where a paste goes.
enum EPasteTarget
{
	PASTE_None		= 0,	// no text field takes typing
	PASTE_Console	= 1,	// the console's line (Console.uc, state Typing: the console and the in-game chat)
	PASTE_Menu		= 2,	// the menus' text field
};

// The GUI module: whether the menus of Viewport have a text field that typed characters reach; with Text, types
// it there. NULL until the GUI module is loaded.
typedef UBOOL (*FMenuPasteFunc)( UViewport* Viewport, const TCHAR* Text );
ENGINE_API extern FMenuPasteFunc GMenuPaste;

// Where a paste on Viewport would go now.
ENGINE_API EPasteTarget PasteTarget( UViewport* Viewport );
// Types Text there (characters the keyboard can type: Latin-1, no control characters; the client converts) and
// says where it went.
ENGINE_API EPasteTarget PasteText( UViewport* Viewport, const TCHAR* Text );

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
