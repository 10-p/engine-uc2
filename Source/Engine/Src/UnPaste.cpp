/*=============================================================================
	UnPaste.cpp: pasted text, typed into the text field that has the keyboard (UnPaste.h).
=============================================================================*/

#include "EnginePrivate.h"
#include "UnPaste.h"

ENGINE_API FMenuPasteFunc GMenuPaste = NULL;

ENGINE_API EPasteTarget PasteTarget( UViewport* Viewport )
{
	guard(PasteTarget);
	if( !Viewport )
		return PASTE_None;
	// The console takes typed characters before the menus do (UGUIController::NativeKeyType gives way to it).
	UConsole* Console = Cast<UConsole>( Viewport->Console );
	if( Console && Console->bTyping )
		return PASTE_Console;
	return GMenuPaste && GMenuPaste( Viewport, NULL ) ? PASTE_Menu : PASTE_None;
	unguard;
}

ENGINE_API EPasteTarget PasteText( UViewport* Viewport, const TCHAR* Text )
{
	guard(PasteText);
	EPasteTarget Target = PasteTarget( Viewport );
	if( Target == PASTE_Console )
	{
		// Each character as the window's WM_CHAR delivers a typed one: the console's KeyType inserts it at the caret.
		UEngine* Engine = Viewport->GetOuterUClient()->Engine;
		for( const TCHAR* P = Text; *P; P++ )
			Engine->Key( Viewport, (EInputKey)(BYTE)*P, *P );
	}
	else if( Target == PASTE_Menu )
		GMenuPaste( Viewport, Text );
	return Target;
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
