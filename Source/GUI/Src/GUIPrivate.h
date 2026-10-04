#ifndef __GUIPRIVATE_H__

/*=============================================================================
	GUIPrivate.h: GUI private header file.
	Copyright 2004 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

/*-----------------------------------------------------------------------------
	GUI private includes.
-----------------------------------------------------------------------------*/

#ifndef INVALIDMENU
#define INVALIDMENU ( Controller == NULL || (!bAllowStandalone && Controller->bActive && MenuOwner == NULL && !this->IsA(UGUIPage::StaticClass())) )
#endif

#ifndef INVALIDRENDER
#define INVALIDRENDER ( Canvas == NULL || INVALIDMENU )
#endif

#ifndef DECLAREBOUNDS
#define DECLAREBOUNDS FLOAT AW(ActualWidth()), AH(ActualHeight()), AL(ActualLeft()), AT(ActualTop())
#endif

void GUIappSprintf( INT MaxLen, TCHAR* Dest, const TCHAR* Fmt, ... );

// conversion enum - it would be ideal to be able to use eXControllerCodes, but these won't match the actual input key code
enum eXBoxControllerCode
{
    XBC_A					=	200,	// IK_Joy1
    XBC_B					=	201,	// IK_Joy2
    XBC_X					=	202,	// IK_Joy3
    XBC_Y					=	203,	// IK_Joy4
    XBC_Black				=	204,	// IK_Joy5
    XBC_White				=	205,	// IK_Joy6
    XBC_LeftTrigger			=	206,	// IK_Joy7
    XBC_RightTrigger		=	207,	// IK_Joy8
    XBC_PadUp				=	208,	// IK_Joy9
    XBC_PadDown				=	209,	// IK_Joy10
    XBC_PadLeft				=	210,	// IK_Joy11
    XBC_PadRight			=	211,	// IK_Joy12
    XBC_Start				=	212,	// IK_Joy13
    XBC_Back				=	213,	// IK_Joy14
    XBC_LeftThumb			=	214,	// IK_Joy15
    XBC_RightThumb			=	215,	// IK_Joy16
	XBC_RightStickX			=	232,	// IK_JoyU
    XBC_RightStickY			=	233,	// IK_JoyV
	XBC_CtrlInsert			=	234,	// IK_JoySlider1
	XBC_CtrlRemove			=	235,	// IK_JoySlider2
    XBC_LeftStickX			=	240,	// IK_JoyX
	XBC_LeftStickY			=	241,	// IK_JoyY
    XBC_MAX					=	255,
};

TCHAR* GetKeyStateName( BYTE State );
TCHAR* GetXCodeName( BYTE XCode );
UBOOL IsValidObject( UObject* Obj );

#ifndef FINAL_BUILD
#define SHOWCODEHANDLER(comp,playerid,xcode)	debugf(NAME_DevUI, TEXT("%s handled %s for id %i"), IsValidObject(comp) ? comp->GetMenuPath() : TEXT("UNKNOWN"), GetXCodeName(xcode), playerid);
#else
#define SHOWCODEHANDLER(comp,playerid,xcode)
#endif

// Used for calls to TextSize()
#ifndef TEXTSAMPLE
#define TEXTSAMPLE TEXT("ÅÇA0")
#endif

UBOOL CheckCanvasClip( UCanvas* C, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL );

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
#endif	// __GUIPRIVATE_H__