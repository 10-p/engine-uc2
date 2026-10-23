/*==============================================================================
  Keyboard and mouse settings (PC): the mouse's sensitivity and invert, and the
  keys of every action, rebindable in game. Opened from the settings page of the
  main menu and of the pause menu (UC2Settings_Main, "KEYBOARD / MOUSE"); not
  shown on the Xbox, whose pad layouts are UC2Settings_Controller's.

  Where things live:
  - a key's binding is User.ini [Engine.Input] <Key>=<command> (Engine.Input's
    per-key config strings). The page edits a copy of every keyboard and mouse
    key's binding and, when it is left (B), writes the changed ones with
    "SET Input <Key> <command>" (the live viewports and the class default) and
    "SAVECONFIGKEYS Engine.Input <Key>..." (User.ini, those keys only). Pad keys
    (Joy*, JoyX..) are never read or written here: the profile's controller
    layout owns them.
  - the mouse: User.ini [UCGameplay.UCPlayerInput] MouseLookSensitivity and
    bInvertMouseLook (the mouse-look set's; not in the profile, whose one invert flag is the pad's).
  - defaults: the PC bindings of DefUser.ini (System/DefUser.ini natively and in
    the browser), read when White (DEFAULTS) is pressed.
  Black (CANCEL) drops the changes made since the page opened; B keeps them.

  A key is changed by selecting an action and a key column (Left/Right) and
  pressing Enter (pad A) or clicking the cell, then pressing the new key or mouse
  button; it is captured raw (GUIController.OnNeedRawKeyPress), so letters and
  mouse buttons do not navigate. In a capture Escape cancels, Backspace or Delete
  clears the cell, a pad button cancels. A key another action has is taken from
  it (that action loses it: its cell is cleared; the
  cell's old key is unbound); the line at the bottom says what changed.

  Ours (patch set keys-menu), not Epic's.
==============================================================================*/

class UC2Settings_Keys extends UC2Settings_PageBase;
// (no include of GUI.huc: the class builds on its own from patches/keys-menu; its one macro is written out)

// key codes (Engine.Interactions.EInputKey)
const K_LeftMouse		= 0x01;
const K_RightMouse		= 0x02;
const K_MiddleMouse		= 0x04;
const K_Backspace		= 0x08;
const K_Escape			= 0x1B;
const K_Delete			= 0x2E;
const K_0				= 0x30;
const K_9				= 0x39;
const K_A				= 0x41;
const K_Z				= 0x5A;
const K_Mouse4			= 0xC1;
const K_Mouse8			= 0xC5;
const K_WheelUp			= 0xEC;
const K_WheelDown		= 0xED;
const K_Count			= 255;

const MAXSLOTS = 2;

// One row of the key list: an action and the command its keys run (DefUser.ini's PC bindings use exactly these
// commands; a key belongs to the row whose command it runs, compared without spaces and case).
struct KeyAction
{
	var() string	Caption;	// shown in the list
	var() string	Command;	// the binding a key gets
	var() string	Hint;		// shown on the bottom line while the row is selected
	var() string	Order;		// preferred display order of keys ("W,Up"), others follow
	var() bool		bFixed;		// shown, not changeable here (the menu key, the console key)
};

var()	array<KeyAction>	Actions;

var()	moSlider			sl_MouseSens;
var()	moCheckbox			chk_MouseInvert;
var()	KeyBindingPanel		pnl_Keys;
var()	AvarisVertList		li_Keys;

var()	localized	string	sMouseSensCaption, sChangeKey, sClearKey, sPressKey, sCapturePrompt,
							sCancelled, sCleared, sNothingToClear, sAlreadyBound, sBound, sTaken,
							sReplaced, sFixedRow, sConsoleKey, sPadButton, sNotBindable, sDefaultsLoaded,
							sChangesCancelled, sNoKey, sMore, sMovedColumn, sSensHint, sDefaultsMissing;

var()	string				ConsoleCaption;		// the console key's row (not an Engine.Input binding)

// every key: its engine name ("" = not a keyboard or mouse button key), the binding the viewport has (Live) and the
// one being edited (Work), and Work without spaces in lower case (for matching commands)
var()	transient	array<string>	KeyNames;
var()	transient	array<string>	Live, Work, WorkNorm;
var()	transient	array<string>	ActionNorm;
// per row: the keys shown in the two columns (-1 = none) and how many more keys run the same command
var()	transient	array<int>		RowKey1, RowKey2, RowMore;
// per row and column, while the page is open: the key the player put in that cell (it stays in that column), -2 for a
// cell the player cleared (stays empty), -1 for neither (the keys are shown in their display order)
var()	transient	array<int>		Pin1, Pin2;

var()	transient	bool			bCapturing, bDefaultsMissing;
var()	transient	int				CaptureRow, CaptureColumn;
var()	transient	float			LiveSens, DefaultSens;
var()	transient	bool			bLiveInvert, bDefaultInvert;
var()	transient	byte			ConsoleKey;

// =====================================================================================================================
// =====================================================================================================================
//  Keys and bindings
// =====================================================================================================================
// =====================================================================================================================

static final function string Norm( string S )
{
	S = ReplaceSubstr(S, " ", "");
	S = ReplaceSubstr(S, "	", "");
	return Locs(S);
}

final function Object GetInputObject()
{
	local PlayerController PC;

	PC = PlayerOwner();
	if ( PC == None )
		return None;
	return PC.GetRawInputObject();
}

/*==========================================================================================================
function	::InitKeyNames
Desc:		The engine names of the keys this page edits: keyboard keys and mouse buttons (the wheel included);
			not the pad (Joy*, JoyX..), the mouse axes, or the unnamed codes.
==========================================================================================================*/
final function InitKeyNames()
{
	local int i;
	local string S;

	KeyNames.Length = K_Count;
	for ( i = 0; i < K_Count; i++ )
	{
		S = string(GetEnum(enum'EInputKey', i));
		if ( Left(S, 3) ~= "IK_" )
			S = Mid(S, 3);
		else S = "";

		if ( S ~= "None" || Left(S, 7) ~= "Unknown" || Left(S, 3) ~= "Joy" || S ~= "Unicode"
			|| S ~= "MouseX" || S ~= "MouseY" || S ~= "MouseZ" || S ~= "MouseW" )
			S = "";

		KeyNames[i] = S;
	}
}

final function bool IsPageKey( int K )
{
	return K > 0 && K < KeyNames.Length && KeyNames[K] != "";
}

/*==========================================================================================================
function	::KeyCaption
Desc:		How a key is written in the list and on the bottom line.
==========================================================================================================*/
final function string KeyCaption( int K )
{
	local string N;

	if ( K < 0 || K >= KeyNames.Length )
		return "";

	N = KeyNames[K];
	switch ( Locs(N) )
	{
	case "leftmouse":		return "LEFT MOUSE";
	case "rightmouse":		return "RIGHT MOUSE";
	case "middlemouse":		return "MIDDLE MOUSE";
	case "mousewheelup":	return "WHEEL UP";
	case "mousewheeldown":	return "WHEEL DOWN";
	case "pageup":			return "PAGE UP";
	case "pagedown":		return "PAGE DOWN";
	case "capslock":		return "CAPS LOCK";
	case "numlock":			return "NUM LOCK";
	case "scrolllock":		return "SCROLL LOCK";
	case "printscrn":		return "PRINT SCREEN";
	case "up":				return "UP ARROW";
	case "down":			return "DOWN ARROW";
	case "left":			return "LEFT ARROW";
	case "right":			return "RIGHT ARROW";
	case "greystar":		return "NUM *";
	case "greyplus":		return "NUM +";
	case "greyminus":		return "NUM -";
	case "greyslash":		return "NUM /";
	case "numpadperiod":	return "NUM .";
	// punctuation by name: a lone "," or "." is hard to see in the list
	case "semicolon":		return "SEMICOLON";
	case "equals":			return "EQUALS";
	case "comma":			return "COMMA";
	case "minus":			return "MINUS";
	case "period":			return "PERIOD";
	case "slash":			return "SLASH";
	case "tilde":			return "TILDE";
	case "leftbracket":		return "[";
	case "rightbracket":	return "]";
	case "backslash":		return "BACKSLASH";
	case "singlequote":		return "APOSTROPHE";
	case "lshift":			return "LEFT SHIFT";
	case "rshift":			return "RIGHT SHIFT";
	case "lcontrol":		return "LEFT CTRL";
	case "rcontrol":		return "RIGHT CTRL";
	}

	if ( Left(N, 6) ~= "NumPad" )
		return "NUM" @ Mid(N, 6);
	if ( Left(N, 5) ~= "Mouse" )
		return "MOUSE" @ Mid(N, 5);

	return Caps(N);
}

/*==========================================================================================================
function	::KeyRank
Desc:		Display order of the keys of a row: the row's Order list first, then letters, mouse buttons, digits,
			everything else; ties by key code.
==========================================================================================================*/
final function int KeyRank( int Row, int K )
{
	local array<string> Parts;
	local int i;

	if ( Actions[Row].Order != "" && Split(Actions[Row].Order, Parts, ",") )
	{
		for ( i = 0; i < Parts.Length; i++ )
			if ( Parts[i] ~= KeyNames[K] )
				return i * 1000 + K;
	}

	if ( K >= K_A && K <= K_Z )
		return 10000 + K;
	if ( K == K_LeftMouse || K == K_RightMouse || K == K_MiddleMouse || (K >= K_Mouse4 && K <= K_Mouse8) || K == K_WheelUp || K == K_WheelDown )
		return 11000 + K;
	if ( K >= K_0 && K <= K_9 )
		return 12000 + K;
	return 13000 + K;
}

final function int FindActionByNorm( string N )
{
	local int i;

	if ( N == "" )
		return -1;
	for ( i = 0; i < ActionNorm.Length; i++ )
		if ( ActionNorm[i] == N )
			return i;
	return -1;
}

/*==========================================================================================================
function	::RebuildRows
Desc:		Works out which keys each row shows, from Work. Called after every change.
==========================================================================================================*/
final function RebuildRows()
{
	local int Row, K, i, j, Tmp;
	local array<int> Found;

	RowKey1.Length = Actions.Length;
	RowKey2.Length = Actions.Length;
	RowMore.Length = Actions.Length;

	for ( Row = 0; Row < Actions.Length; Row++ )
	{
		Found.Length = 0;
		if ( Actions[Row].Command == "" )
		{
			// the console key: not a binding
			if ( ConsoleKey > 0 )
				Found[0] = ConsoleKey;
		}
		else
		{
			for ( K = 1; K < K_Count; K++ )
				if ( KeyNames[K] != "" && WorkNorm[K] == ActionNorm[Row] )
					Found[Found.Length] = K;
		}

		// insertion sort by display rank
		for ( i = 1; i < Found.Length; i++ )
		{
			Tmp = Found[i];
			for ( j = i; j > 0 && KeyRank(Row, Found[j-1]) > KeyRank(Row, Tmp); j-- )
				Found[j] = Found[j-1];
			Found[j] = Tmp;
		}

		// the cells the player set this time keep what was put there; the rest are filled in display order
		RowKey1[Row] = TakePinned(Found, Pin1[Row]);
		RowKey2[Row] = TakePinned(Found, Pin2[Row]);
		if ( RowKey1[Row] == -1 && Pin1[Row] != -2 && Found.Length > 0 )
		{
			RowKey1[Row] = Found[0];
			Found.Remove(0, 1);
		}
		if ( RowKey2[Row] == -1 && Pin2[Row] != -2 && Found.Length > 0 )
		{
			RowKey2[Row] = Found[0];
			Found.Remove(0, 1);
		}
		RowMore[Row] = Found.Length;
	}
}

// the pinned key out of the row's keys, if it is still one of them; -1 otherwise
final function int TakePinned( out array<int> Found, int Pin )
{
	local int i;

	if ( Pin < 0 )
		return -1;
	for ( i = 0; i < Found.Length; i++ )
		if ( Found[i] == Pin )
		{
			Found.Remove(i, 1);
			return Pin;
		}
	return -1;
}

final function SetPin( int Row, int Col, int K )
{
	if ( Row < 0 || Row >= Pin1.Length )
		return;
	if ( Col == 0 )
		Pin1[Row] = K;
	else Pin2[Row] = K;
}

final function ClearPins()
{
	local int i;

	Pin1.Length = Actions.Length;
	Pin2.Length = Actions.Length;
	for ( i = 0; i < Actions.Length; i++ )
	{
		Pin1[i] = -1;
		Pin2[i] = -1;
	}
}

// one DevUI line with every row as the list shows it (for the tests and for a player's log): ROW=KEY1,KEY2[+n]
final function LogRows( string Why )
{
	local int Row;
	local string S;

	for ( Row = 0; Row < Actions.Length; Row++ )
		S = S $ (Row > 0 ? "; " : "") $ Actions[Row].Caption $ "=" $ (RowKey1[Row] >= 0 ? KeyNames[RowKey1[Row]] : "")
			$ "," $ (RowKey2[Row] >= 0 ? KeyNames[RowKey2[Row]] : "") $ (RowMore[Row] > 0 ? "+" $ RowMore[Row] : "");
	log("KeyBinds: rows (" $ Why $ "):" @ S, 'DevUI');
}

final function int SlotKey( int Row, int Col )
{
	if ( Row < 0 || Row >= RowKey1.Length )
		return -1;
	if ( Col == 0 )
		return RowKey1[Row];
	return RowKey2[Row];
}

final function SetWork( int K, string Command )
{
	Work[K] = Command;
	WorkNorm[K] = Norm(Command);
}

/*==========================================================================================================
function	::LoadBindings
Desc:		Work = what the viewport's input object has now (or, for the defaults, DefUser.ini's PC bindings).
==========================================================================================================*/
final function LoadBindings( bool bDefaults )
{
	local Object RawInput;
	local int K, Missing, Found;
	local string V;

	RawInput = GetInputObject();
	Live.Length = K_Count;
	Work.Length = K_Count;
	WorkNorm.Length = K_Count;

	for ( K = 0; K < K_Count; K++ )
	{
		if ( KeyNames[K] == "" )
			continue;

		if ( RawInput != None )
			Live[K] = RawInput.GetPropertyText(KeyNames[K]);

		if ( bDefaults )
		{
			V = "";
			if ( !GetConfigValue("DefUser", "Engine.Input", KeyNames[K], V) )
			{
				V = "";
				Missing++;
			}
			else Found++;
			SetWork(K, V);
		}
		else SetWork(K, Live[K]);
	}

	// no DefUser.ini (or not ours): keep the keys as they are rather than clear them all
	if ( bDefaults && Found < 20 )
	{
		for ( K = 0; K < K_Count; K++ )
			if ( KeyNames[K] != "" )
				SetWork(K, Live[K]);
		log("KeyBinds: DefUser.ini [Engine.Input] not found (" $ Found @ "keys): the keys stay as they are", 'DevUI');
		bDefaultsMissing = true;
	}
	else bDefaultsMissing = false;

	ClearPins();
	if ( bDefaults && !bDefaultsMissing )
		log("KeyBinds: defaults loaded from DefUser.ini [Engine.Input];" @ Missing @ "keys have no line there and are cleared", 'DevUI');

	RebuildRows();
}

/*==========================================================================================================
function	::SaveBindings
Desc:		Every key whose binding changed goes to the viewports and the class default (SET Input) and to
			User.ini (SAVECONFIGKEYS, a few keys a command). Nothing else is written.
==========================================================================================================*/
final function SaveBindings()
{
	local PlayerController PC;
	local int K, n, Total;
	local string Batch, Changed;

	PC = PlayerOwner();
	if ( PC == None )
		return;

	for ( K = 1; K < K_Count; K++ )
	{
		if ( KeyNames[K] == "" || Work[K] == Live[K] )
			continue;

		PC.ConsoleCommand("SET Input" @ KeyNames[K] @ Work[K]);
		Live[K] = Work[K];
		Changed = Changed $ (Changed != "" ? ", " : "") $ KeyNames[K] $ "=" $ Work[K];

		Batch = Batch @ KeyNames[K];
		n++;
		Total++;
		if ( n == 16 )
		{
			PC.ConsoleCommand("SAVECONFIGKEYS Engine.Input" $ Batch);
			Batch = "";
			n = 0;
		}
	}
	if ( n > 0 )
		PC.ConsoleCommand("SAVECONFIGKEYS Engine.Input" $ Batch);

	if ( Total > 0 )
		log("KeyBinds: saved" @ Total @ "key(s) to User.ini [Engine.Input]:" @ Changed, 'DevUI');
}

// =====================================================================================================================
// =====================================================================================================================
//  The mouse
// =====================================================================================================================
// =====================================================================================================================

static final function string FloatText( float F, int Decimals )
{
	local int Scale, i, Whole, Frac;
	local string S;

	Scale = 1;
	for ( i = 0; i < Decimals; i++ )
		Scale *= 10;

	i = int(Abs(F) * Scale + 0.5);
	Whole = i / Scale;
	Frac = i - Whole * Scale;
	S = string(Whole);
	if ( Decimals > 0 )
	{
		S = S $ ".";
		for ( Scale /= 10; Scale > 1 && Frac < Scale; Scale /= 10 )
			S = S $ "0";
		S = S $ string(Frac);
	}
	if ( F < 0 )
		S = "-" $ S;
	return S;
}

final function UpdateSensCaption()
{
	local float V;

	V = sl_MouseSens.GetValue();
	sl_MouseSens.SetCaption(sMouseSensCaption @ FloatText(V, 1));
	// UCPlayerInput.AddMouseLook: 0.022 degrees x MouseLookSensitivity per count at a 90-degree view
	sl_MouseSens.SetHint(ReplaceSubstr(ReplaceSubstr(sSensHint, "%deg%", FloatText(V * 0.022, 4)), "%default%", FloatText(DefaultSens, 1)));
}

final function LoadMouse( bool bDefaults )
{
	local PlayerController PC;
	local UCPlayerInput UCPI;
	local string V;

	PC = PlayerOwner();
	if ( PC != None )
		UCPI = UCPlayerInput(PC.PlayerInput);

	if ( UCPI != None )
	{
		LiveSens = UCPI.MouseLookSensitivity;
		bLiveInvert = UCPI.bInvertMouseLook;
	}
	else
	{
		LiveSens = class'UCPlayerInput'.default.MouseLookSensitivity;
		bLiveInvert = class'UCPlayerInput'.default.bInvertMouseLook;
	}

	DefaultSens = 5.0;
	bDefaultInvert = false;
	if ( GetConfigValue("DefUser", "UCGameplay.UCPlayerInput", "MouseLookSensitivity", V) && float(V) > 0 )
		DefaultSens = float(V);
	if ( GetConfigValue("DefUser", "UCGameplay.UCPlayerInput", "bInvertMouseLook", V) )
		bDefaultInvert = V ~= "True";

	if ( bDefaults )
	{
		sl_MouseSens.SetValue(DefaultSens);
		chk_MouseInvert.SetValue(bDefaultInvert, true);
	}
	else
	{
		sl_MouseSens.SetValue(LiveSens);
		chk_MouseInvert.SetValue(bLiveInvert, true);
	}
	UpdateSensCaption();
}

final function SaveMouse()
{
	local PlayerController PC;
	local UCPlayerInput UCPI;
	local float V;
	local bool B;

	PC = PlayerOwner();
	if ( PC == None )
		return;
	UCPI = UCPlayerInput(PC.PlayerInput);

	V = sl_MouseSens.GetValue();
	if ( Abs(V - LiveSens) > 0.0001 )
	{
		if ( UCPI != None )
			UCPI.MouseLookSensitivity = V;
		class'UCPlayerInput'.default.MouseLookSensitivity = V;
		PC.ConsoleCommand("SAVECONFIGKEYS UCGameplay.UCPlayerInput MouseLookSensitivity");
		LiveSens = V;
		log("KeyBinds: mouse sensitivity" @ FloatText(V, 2) @ "saved to User.ini", 'DevUI');
	}

	B = chk_MouseInvert.IsChecked();
	if ( B != bLiveInvert )
	{
		if ( UCPI != None )
			UCPI.bInvertMouseLook = B;
		class'UCPlayerInput'.default.bInvertMouseLook = B;
		PC.ConsoleCommand("SAVECONFIGKEYS UCGameplay.UCPlayerInput bInvertMouseLook");
		bLiveInvert = B;
		log("KeyBinds: mouse invert" @ B @ "saved to User.ini", 'DevUI');
	}
}

/*==========================================================================================================
function	::SliderClick
Desc:		A click on the sensitivity slider (GUISlider answers only the pad's left/right): on the minus or plus
			end, one step; on the bar, the value under the cursor.
==========================================================================================================*/
final function bool SliderClick( GUIComponent Sender )
{
	local GUISlider S;
	local float X, AL, AW, EndW, V;

	S = GUISlider(Sender);
	if ( S == None )
		return false;

	X = Controller.MouseX;
	AL = S.ActualLeft();
	AW = S.ActualWidth();
	EndW = S.tex_Minus.MaterialUSize();

	if ( X < AL + EndW )
		S.AdjustValue(-S.Step);
	else if ( X > AL + AW - EndW )
		S.AdjustValue(S.Step);
	else if ( AW > 2 * EndW )
	{
		V = S.MinValue + (X - (AL + EndW)) / (AW - 2 * EndW) * (S.MaxValue - S.MinValue);
		if ( S.Step > 0 )
			V = int(V / S.Step + 0.5) * S.Step;
		S.SetValue(V);
	}
	return true;
}

// =====================================================================================================================
// =====================================================================================================================
//  Capture: the next key or mouse button pressed goes to the selected cell
// =====================================================================================================================
// =====================================================================================================================

final function string RowCaption( int Row )
{
	if ( Row < 0 || Row >= Actions.Length )
		return "";
	return Actions[Row].Caption;
}

final function SetStatus( string S )
{
	ChangeHint(S);
	log("KeyBinds:" @ S, 'DevUI');
}

final function StartCapture( int Row, int Col )
{
	if ( bCapturing || Row < 0 || Row >= Actions.Length )
		return;

	pnl_Keys.Column = Col;
	if ( Actions[Row].bFixed )
	{
		PlayAction(CS_Error);
		SetStatus(ReplaceSubstr(sFixedRow, "%action%", RowCaption(Row)));
		return;
	}

	bCapturing = true;
	CaptureRow = Row;
	CaptureColumn = Col;
	Controller.OnNeedRawKeyPress = KeyCaptured;
	PlayAction(CS_Click);
	SetStatus(ReplaceSubstr(ReplaceSubstr(sCapturePrompt, "%action%", RowCaption(Row)), "%column%", string(Col + 1)));
}

final function StopCapture()
{
	bCapturing = false;
	if ( Controller != None )
		Controller.OnNeedRawKeyPress = None;
}

final function bool KeyCaptured( byte NewKey )
{
	local int Row, Col;

	Row = CaptureRow;
	Col = CaptureColumn;
	StopCapture();

	log("KeyBinds: captured" @ KeyNames[NewKey] @ "(" $ NewKey $ ") for" @ RowCaption(Row) @ "key" @ (Col + 1), 'DevUI');

	if ( NewKey == K_Escape )
		SetStatus(sCancelled);
	else if ( NewKey == K_Backspace || NewKey == K_Delete )
		ClearSlot(Row, Col);
	else if ( (NewKey >= 0xC8 && NewKey <= 0xD7) || (NewKey >= 0xE8 && NewKey <= 0xEB) || (NewKey >= 0xF0 && NewKey <= 0xF3) )
		SetStatus(sPadButton);
	else if ( ConsoleKey > 0 && NewKey == ConsoleKey )
		SetStatus(ReplaceSubstr(sConsoleKey, "%key%", KeyCaption(NewKey)));
	else if ( !IsPageKey(NewKey) )
		SetStatus(sNotBindable);
	else
		AssignKey(Row, Col, NewKey);

	return true;
}

/*==========================================================================================================
function	::AssignKey
Desc:		The key K goes to the row's column; the column's old key is unbound. If another row had K, that row
			loses it (its cell is cleared); a key with a command no
			row lists (Epic's SwitchWeapon on the number keys, the gamma keys...) loses it, and the bottom line
			says which.
==========================================================================================================*/
final function AssignKey( int Row, int Col, int K )
{
	local int Old, Prev, PrevCol;
	local string PrevCommand, Msg;

	Old = SlotKey(Row, Col);
	if ( Old == K )
	{
		SetStatus(ReplaceSubstr(ReplaceSubstr(sAlreadyBound, "%key%", KeyCaption(K)), "%action%", RowCaption(Row)));
		return;
	}

	PrevCommand = Work[K];
	Prev = FindActionByNorm(WorkNorm[K]);
	if ( Prev == Row )
	{
		// K is already one of this row's keys: it only changes column (the bindings stay as they are)
		if ( SlotKey(Row, 1 - Col) == K )
			SetPin(Row, 1 - Col, Old >= 0 ? Old : -2);
		SetPin(Row, Col, K);
		RebuildRows();
		PlayAction(CS_Modify);
		SetStatus(ReplaceSubstr(ReplaceSubstr(ReplaceSubstr(sMovedColumn, "%key%", KeyCaption(K)), "%action%", RowCaption(Row)), "%column%", string(Col + 1)));
		return;
	}
	if ( Prev >= 0 && Actions[Prev].bFixed )
	{
		PlayAction(CS_Error);
		SetStatus(ReplaceSubstr(sFixedRow, "%action%", RowCaption(Prev)));
		return;
	}

	// the column K is shown in on the other row, which is cleared there
	PrevCol = -1;
	if ( Prev >= 0 )
	{
		if ( SlotKey(Prev, 0) == K )
			PrevCol = 0;
		else if ( SlotKey(Prev, 1) == K )
			PrevCol = 1;
	}

	SetWork(K, Actions[Row].Command);
	SetPin(Row, Col, K);
	Msg = ReplaceSubstr(ReplaceSubstr(sBound, "%key%", KeyCaption(K)), "%action%", RowCaption(Row));

	if ( Old >= 0 )
		SetWork(Old, "");
	if ( Prev >= 0 )
	{
		if ( PrevCol >= 0 )
			SetPin(Prev, PrevCol, -2);
		Msg = Msg @ ReplaceSubstr(ReplaceSubstr(sTaken, "%action%", RowCaption(Prev)), "%key%", KeyCaption(K));
	}

	if ( Prev < 0 && PrevCommand != "" )
		Msg = Msg @ ReplaceSubstr(sReplaced, "%command%", PrevCommand);

	bDirty = true;
	RebuildRows();
	PlayAction(CS_Accept);
	SetStatus(Msg);
}

final function ClearSlot( int Row, int Col )
{
	local int K;

	if ( Row < 0 || Row >= Actions.Length )
		return;
	if ( Actions[Row].bFixed )
	{
		PlayAction(CS_Error);
		SetStatus(ReplaceSubstr(sFixedRow, "%action%", RowCaption(Row)));
		return;
	}

	K = SlotKey(Row, Col);
	if ( K < 0 )
	{
		SetStatus(sNothingToClear);
		return;
	}

	SetWork(K, "");
	SetPin(Row, Col, -2);
	bDirty = true;
	RebuildRows();
	PlayAction(CS_Modify);
	SetStatus(ReplaceSubstr(ReplaceSubstr(sCleared, "%key%", KeyCaption(K)), "%action%", RowCaption(Row)));
}

// =====================================================================================================================
// =====================================================================================================================
//  The list
// =====================================================================================================================
// =====================================================================================================================

final function string CellText( int Row, int Col )
{
	local int K;
	local string S;

	if ( bCapturing && Row == CaptureRow && Col == CaptureColumn )
		return sPressKey;

	K = SlotKey(Row, Col);
	if ( K < 0 )
		return sNoKey;

	S = KeyCaption(K);
	if ( Col == 1 && RowMore[Row] > 0 )
		S = S @ ReplaceSubstr(sMore, "%n%", string(RowMore[Row]));
	return S;
}

/*==========================================================================================================
function	::DrawKeyRow
Desc:		The list's OnDrawItem: the row as GUIListBase's DrawItem draws it (item style box, the selected row
			highlighted), then the action and its two keys in their columns; the cell the keyboard acts on in
			orange while the list has the focus.
==========================================================================================================*/
final function bool DrawKeyRow( Canvas C, int Item, float X, float Y, float W, float H, bool bSelected )
{
	local GUIStyles S;
	local eMenuState St;
	local float OldAlpha, X1, X2;
	local color Hot, NoColor;
	local bool bListFocused;

	S = li_Keys.ItemStyle;
	if ( S == None || Item < 0 || Item >= Actions.Length )
		return false;

	bListFocused = li_Keys.MenuState == MSAT_Focused;
	if ( bListFocused )
		St = MSAT_Blurry;
	else St = li_Keys.MenuState;

	S.Draw(C, St, X, Y, W, H);
	if ( Item == li_Keys.Index )
	{
		St = MSAT_Focused;
		OldAlpha = C.ForcedAlpha;
		C.ForcedAlpha *= 0.75;
		S.Draw(C, St, X, Y, W, H);
		C.ForcedAlpha = OldAlpha;
	}

	pnl_Keys.GetRowInner(X, Y, W, H);
	X1 = pnl_Keys.GetColumnLeft(0, X, W);
	X2 = pnl_Keys.GetColumnLeft(1, X, W);

	Hot = pnl_Keys.ActiveColumnColor;
	NoColor.R = 0;	// all zero: no override (GUIStyles.DrawText)
	NoColor.G = 0;
	NoColor.B = 0;
	NoColor.A = 0;
	S.DrawText(C, St, X, Y, X1 - X - 4, H, TXTA_Left, RowCaption(Item));

	if ( Item == li_Keys.Index && (bListFocused || bCapturing) && pnl_Keys.Column == 0 )
		S.DrawText(C, St, X1, Y, X2 - X1 - 4, H, TXTA_Left, CellText(Item, 0), Hot);
	else S.DrawText(C, St, X1, Y, X2 - X1 - 4, H, TXTA_Left, CellText(Item, 0), NoColor);

	if ( Item == li_Keys.Index && (bListFocused || bCapturing) && pnl_Keys.Column == 1 )
		S.DrawText(C, St, X2, Y, X + W - X2, H, TXTA_Left, CellText(Item, 1), Hot);
	else S.DrawText(C, St, X2, Y, X + W - X2, H, TXTA_Left, CellText(Item, 1), NoColor);

	return true;
}

// keyboard and pad on the list: A changes the selected cell, X clears it, Left/Right pick the column
final function bool KeyListXEvent( GUIComponent Sender, byte Id, eXControllerCodes iCode )
{
	if ( bCapturing )
		return true;

	switch ( iCode )
	{
	case XC_A:
		StartCapture(li_Keys.Index, pnl_Keys.Column);
		return true;

	case XC_X:
		ClearSlot(li_Keys.Index, pnl_Keys.Column);
		return true;

	case XC_Left:
	case XC_PadLeft:
		if ( pnl_Keys.Column != 0 )
			PlayAction(CS_Modify);
		pnl_Keys.Column = 0;
		return true;

	case XC_Right:
	case XC_PadRight:
		if ( pnl_Keys.Column != 1 )
			PlayAction(CS_Modify);
		pnl_Keys.Column = 1;
		return true;
	}

	return li_Keys.InternalOnXControllerEvent(Sender, Id, iCode);
}

// the Delete key (a raw key in the menus' key table) clears the selected cell
final function bool KeyListKeyEvent( out byte Key, out byte State, float Delta )
{
	if ( !bCapturing && Key == K_Delete && State == 1 )		// IST_Press
	{
		ClearSlot(li_Keys.Index, pnl_Keys.Column);
		return true;
	}
	return li_Keys.InternalOnKeyEvent(Key, State, Delta);
}

// a mouse click on a row (the controller has already selected the row under the cursor): change the key column
// under the cursor, or the selected column when the click is on the action's name
final function bool KeyListClick( GUIComponent Sender )
{
	local int Col;

	if ( bCapturing )
		return true;

	Col = pnl_Keys.ColumnAt(Controller.MouseX);
	if ( Col < 0 )
		Col = pnl_Keys.Column;
	StartCapture(li_Keys.Index, Col);
	return true;
}

final function KeyRowChanged( GUIComponent Sender )
{
	if ( !bCapturing && li_Keys.Index >= 0 && li_Keys.Index < Actions.Length )
		ChangeHint(Actions[li_Keys.Index].Hint);
}

// =====================================================================================================================
// =====================================================================================================================
//  The page
// =====================================================================================================================
// =====================================================================================================================

final function ValueChange( GUIComponent Sender )
{
	if ( Sender == sl_MouseSens )
	{
		UpdateSensCaption();
		ChangeHint(sl_MouseSens.Hint);
	}

	InternalOnChange(Sender);
	bDirty = true;
}

function InternalOnChange( GUIComponent Sender )
{
	local GUIComponent Comp;
	local string NewCaption;

	if ( Sender == pnl_Options )
		Comp = pnl_Options.GetFocusedControl();
	else Comp = Sender;

	if ( Comp == chk_MouseInvert )
		NewCaption = chk_MouseInvert.IsChecked() ? Caps(class'Localizer'.default.sUninvert) : Caps(class'Localizer'.default.sInvert);
	else if ( moSlider(Comp) != None )
		NewCaption = Caps(class'Localizer'.default.sAdjust);
	else if ( Comp == pnl_Keys || Comp == li_Keys || Comp == pnl_Keys.lb_Keys )
		NewCaption = sChangeKey;

	if ( NewCaption != "" )
	{
		ftr_ControlPanel.pnl_Button.SetButtonCaption(XC_A, NewCaption);
		ftr_ControlPanel.pnl_Button.ShowButton(XC_A);
	}

	if ( Comp == pnl_Keys || Comp == li_Keys || Comp == pnl_Keys.lb_Keys )
	{
		ftr_ControlPanel.pnl_Button.ShowButton(XC_X);
		// the slider left its left/right glyphs on the A button (UpdateActionButton changes only what it knows)
		SetAButtonCode(Controller.GetXBoxCharacter(XC_A));
	}
	else ftr_ControlPanel.pnl_Button.HideButton(XC_X);

	UpdateActionButton(Comp);
}

final function FillList()
{
	local int i;

	li_Keys.Clear();
	for ( i = 0; i < Actions.Length; i++ )
		li_Keys.AddWithHint(Actions[i].Caption, , string(i), Actions[i].Hint);

	if ( li_Keys.Index < 0 && Actions.Length > 0 )
		li_Keys.SetIndex(0);
}

function InitOptions()
{
	pnl_Options.AddItem( sl_MouseSens );
	pnl_Options.AddItem( chk_MouseInvert );
	pnl_Options.AddItem( pnl_Keys );

	Super.InitOptions();

	sl_MouseSens.MySlider.bMouseOverSound = true;
}

function LoadSettings()
{
	local bool bTemp;
	local GUIComponent Focused;

	bTemp = Controller.bCurMenuInitialized;
	Controller.bCurMenuInitialized = false;

	if ( bCapturing )
		StopCapture();

	LoadBindings(bRestoreDefaults);
	LoadMouse(bRestoreDefaults);
	LogRows(bRestoreDefaults ? "defaults" : "loaded");
	log("KeyBinds: mouse sensitivity" @ FloatText(sl_MouseSens.GetValue(), 2) @ "invert" @ chk_MouseInvert.IsChecked(), 'DevUI');

	Focused = GetFocusedControl();
	if ( Focused != None )
		InternalOnChange(Focused);
	else InternalOnChange(sl_MouseSens);

	Controller.bCurMenuInitialized = bTemp;
	Super.LoadSettings();

	if ( bRestoreDefaults )
		SetStatus(bDefaultsMissing ? sDefaultsMissing : sDefaultsLoaded);
}

function SaveSettings( optional bool bNoWrite )
{
	if ( bCapturing )
		StopCapture();

	SaveBindings();
	SaveMouse();
	LogRows("saved");

	Super.SaveSettings(bNoWrite);
}

function CancelChanges()
{
	Super.CancelChanges();
	SetStatus(sChangesCancelled);
}

function InitButtonIcons()
{
	ftr_ControlPanel.AddStandardButton(XC_A, sChangeKey);
	ftr_ControlPanel.AddButton(XC_X, sClearKey, 150, 0);
	ftr_ControlPanel.AddButton(XC_White, Caps(Caps(class'Localizer'.default.sDefaults)), 250, 0);
	ftr_ControlPanel.AddButton(XC_Black, Caps(class'Localizer'.default.sCancel), 390, 0);
	Super.InitButtonIcons();
}

// =====================================================================================================================
// =====================================================================================================================
//  GUIComponent Interface
// =====================================================================================================================
// =====================================================================================================================

event InitComponent( GUIController InController, GUIComponent InOwner )
{
	local int i;

	InitKeyNames();
	ConsoleKey = class'Console'.default.ConsoleKey;

	ActionNorm.Length = Actions.Length;
	for ( i = 0; i < Actions.Length; i++ )
	{
		if ( Actions[i].Command == "" && Actions[i].bFixed )
			Actions[i].Caption = ConsoleCaption;
		ActionNorm[i] = Norm(Actions[i].Command);
	}

	Super.InitComponent(InController, InOwner);

	li_Keys = pnl_Keys.li_Keys;
	li_Keys.OnDrawItem = DrawKeyRow;
	li_Keys.OnXControllerEvent = KeyListXEvent;
	li_Keys.OnKeyEvent = KeyListKeyEvent;
	li_Keys.OnClick = KeyListClick;
	li_Keys.OnChange = KeyRowChanged;
	FillList();

	sl_MouseSens.MySlider.Step = 0.1;
	sl_MouseSens.MySlider.OnClick = SliderClick;
}

event Closed( GUIComponent Sender, bool bCancelled )
{
	if ( bCapturing )
		StopCapture();

	Super.Closed(Sender, bCancelled);
}

event Free()
{
	if ( bCapturing )
		StopCapture();

	sl_MouseSens = None;
	chk_MouseInvert = None;
	pnl_Keys = None;
	li_Keys = None;

	Super.Free();
}

DefaultProperties
{
	SubTitle="KEYBOARD / MOUSE"
	bRequiresProfile=false

	sMouseSensCaption=" MOUSE SENSITIVITY"
	sChangeKey="CHANGE"
	sClearKey="CLEAR"
	sPressKey="PRESS A KEY"
	sCapturePrompt="PRESS A KEY. ESCAPE: CANCEL, BACKSPACE: CLEAR"
	sCancelled="NOT CHANGED"
	sCleared="%key% CLEARED (NO LONGER %action%)"
	sNothingToClear="NO KEY TO CLEAR THERE"
	sAlreadyBound="%key% IS ALREADY %action%"
	sBound="%key%: %action%."
	sTaken="%action% LOST %key%."
	sReplaced="(IT WAS: %command%)"
	sFixedRow="%action% CANNOT BE CHANGED HERE"
	sConsoleKey="%key% OPENS THE CONSOLE: CHOOSE ANOTHER KEY"
	sPadButton="PAD BUTTONS ARE SET ON THE CONTROLLER PAGE"
	sNotBindable="THAT KEY CANNOT BE USED"
	sDefaultsLoaded="DEFAULT KEYS LOADED (CANCEL UNDOES IT, BACK KEEPS IT)"
	sChangesCancelled="CHANGES UNDONE"
	sDefaultsMissing="DEFAULT KEYS NOT FOUND (DEFUSER.INI): KEYS NOT CHANGED"
	sNoKey="-"
	sMore="+%n%"
	sMovedColumn="%key% IS NOW KEY %column% OF %action%"
	sSensHint="%deg% DEGREES PER MOUSE COUNT (THE DEFAULT IS %default%)"
	ConsoleCaption="CONSOLE"

	// The actions, from DefUser.ini's PC bindings (Engine/Config/DefUser.ini [Engine.Input]) and the commands
	// they run. Command must be the exact binding DefUser.ini uses.
	Actions(0)=(Caption="MOVE FORWARD",Command="MoveForward",Hint="WALK OR RUN FORWARD",Order="W,Up")
	Actions(1)=(Caption="MOVE BACK",Command="MoveBackward",Hint="WALK OR RUN BACKWARD",Order="S,Down")
	Actions(2)=(Caption="STRAFE LEFT",Command="StrafeLeft",Hint="STEP SIDEWAYS TO THE LEFT",Order="A,Left")
	Actions(3)=(Caption="STRAFE RIGHT",Command="StrafeRight",Hint="STEP SIDEWAYS TO THE RIGHT",Order="D,Right")
	Actions(4)=(Caption="JUMP / DODGE",Command="SmartJump 0",Hint="JUMP; WHILE MOVING: DODGE (OR WALL DODGE IN THE AIR)",Order="Space")
	Actions(5)=(Caption="JUMP / DODGE 2",Command="SmartJump 1",Hint="JUMP OR DODGE, NEVER A COMBO (THE PAD'S STICK CLICK)",Order="Ctrl")
	Actions(6)=(Caption="JUMP ONLY",Command="Jump",Hint="JUMP STRAIGHT UP, NO DODGE",Order="Backspace")
	Actions(7)=(Caption="TURN LEFT",Command="Axis aMouseX Speed=-4.0",Hint="TURN LEFT WITH A KEY (NONE BY DEFAULT: THE MOUSE TURNS)")
	Actions(8)=(Caption="TURN RIGHT",Command="Axis aMouseX Speed=+4.0",Hint="TURN RIGHT WITH A KEY (NONE BY DEFAULT: THE MOUSE TURNS)")
	Actions(9)=(Caption="LOOK UP",Command="Axis aMouseY Speed=+4.0",Hint="LOOK UP WITH A KEY",Order="PageUp")
	Actions(10)=(Caption="LOOK DOWN",Command="Axis aMouseY Speed=-4.0",Hint="LOOK DOWN WITH A KEY",Order="PageDown")
	Actions(11)=(Caption="CENTER VIEW",Command="CenterView",Hint="LEVEL THE VIEW WHILE HELD",Order="End,Home")
	Actions(12)=(Caption="FIRE",Command="Fire",Hint="FIRE; IN MELEE: ATTACK",Order="LeftMouse,Alt")
	Actions(13)=(Caption="ALT-FIRE / SHIELD",Command="AltFire | Shield 1 | onrelease Shield 0",Hint="ALTERNATE FIRE; IN MELEE: BLOCK AND REFLECT",Order="RightMouse")
	Actions(14)=(Caption="MELEE WEAPON",Command="MeleeWeapon | MeleeHeavyAttack | MeleePounceAttack",Hint="SWITCH TO MELEE; IN MELEE: HEAVY ATTACK, IN THE AIR: POUNCE",Order="E,B")
	Actions(15)=(Caption="RANGED WEAPON",Command="MeleeFinishAttack | NextWeapon",Hint="BACK TO A RANGED WEAPON; NEXT RANGED WEAPON; FINISHING MOVE",Order="Q,Y")
	Actions(16)=(Caption="NEXT WEAPON",Command="NextWeapon",Hint="NEXT RANGED WEAPON",Order="MouseWheelUp,MouseWheelDown,N,Slash")
	Actions(17)=(Caption="PISTOLS",Command="RaceWeapon",Hint="SWITCH TO THE PISTOLS")
	Actions(18)=(Caption="ENERGY WEAPON",Command="EnergyWeapon",Hint="SWITCH TO THE ENERGY WEAPON")
	Actions(19)=(Caption="EXPLOSIVE WEAPON",Command="ExplosiveWeapon",Hint="SWITCH TO THE EXPLOSIVE WEAPON")
	Actions(20)=(Caption="LOCK ON",Command="Lock 1 | onrelease Lock 0",Hint="HOLD TO LOCK ON TO AN ENEMY; WITH A DIRECTION: NEXT TARGET",Order="MiddleMouse,L")
	Actions(21)=(Caption="ADRENALINE COMBOS",Command="button bCombo | ComboPressed",Hint="HOLD FOR THE COMBO MENU",Order="X")
	Actions(22)=(Caption="USE",Command="Use",Hint="USE A SWITCH OR AN OBJECT",Order="F,Enter")
	Actions(23)=(Caption="1ST / 3RD PERSON",Command="BlackButton | ToggleThirdCam",Hint="CHANGE THE VIEW; ON THE SCOREBOARD: NEXT PAGE",Order="C,K")
	Actions(24)=(Caption="TAUNTS / VOICE",Command="WhiteButton | VoiceMenuNext",Hint="OPEN THE VOICE MENU, THEN SEND; ON THE SCOREBOARD: PREVIOUS PAGE",Order="V")
	Actions(25)=(Caption="VOICE MENU UP",Command="VoiceMenuUp",Hint="PREVIOUS LINE OF THE VOICE MENU")
	Actions(26)=(Caption="VOICE MENU DOWN",Command="VoiceMenuDown",Hint="NEXT LINE OF THE VOICE MENU")
	Actions(27)=(Caption="CHAT",Command="Talk",Hint="TYPE A MESSAGE TO EVERYONE",Order="T")
	Actions(28)=(Caption="TEAM CHAT",Command="TeamTalk",Hint="TYPE A MESSAGE TO YOUR TEAM")
	Actions(29)=(Caption="SCOREBOARD",Command="ShowScores",Hint="SHOW THE SCORES",Order="Tab,F1")
	Actions(30)=(Caption="SCREENSHOT",Command="shot",Hint="SAVE A SCREENSHOT",Order="F9")
	Actions(31)=(Caption="MENU",Command="ShowMenu",Hint="THE PAUSE MENU (ALWAYS ESCAPE)",Order="Escape",bFixed=true)
	Actions(32)=(Caption="CONSOLE",Command="",Hint="THE CONSOLE (THE KEY LEFT OF 1)",bFixed=true)

	Begin Object Class=ItemSelectionPanel Name=KeysOptionPanel
		WinWidth=0.853125
		WinHeight=0.601250
		WinLeft=0.071875
		WinTop=0.157708
		ItemSpacing=10
		OnChange=InternalOnChange
	End Object
	pnl_Options=KeysOptionPanel

	Begin Object Class=moSlider Name=MouseSensSlider
		WinWidth=460
		WinHeight=0.083750
		WinLeft=0.076563
		WinTop=0.372917
		Caption=" MOUSE SENSITIVITY"
		Hint="HOW FAR THE MOUSE TURNS THE VIEW"
		LabelStyleName="Sui24pt_C8C8C8FF"
		bVerticalLayout=True
		ComponentWidth=0.3
		OnChange=ValueChange
		VertLabelJustification=TXTA_Center
		ComponentJustification=TXTA_Right
		minValue=0.0
		minSettingValue=0.1
		maxValue=10.0
		bPropagateState=false
		TabOrder=0
	End Object
	sl_MouseSens=MouseSensSlider

	Begin Object Class=moCheckbox Name=MouseInvertCheckbox
		WinLeft=49
		WinTop=141
		WinWidth=0.509376
		WinHeight=0.043750
		Caption=" INVERT MOUSE LOOK"
		LabelStyleName="Sui24pt_C8C8C8FF"
		Hint="MOVING THE MOUSE FORWARD LOOKS DOWN (THE PAD'S INVERT IS ON THE CONTROLLER PAGE)"
		OnChange=ValueChange
		bPropagateState=false
		TabOrder=1
	End Object
	chk_MouseInvert=MouseInvertCheckbox

	Begin Object Class=KeyBindingPanel Name=KeysPanel
		WinWidth=502
		WinHeight=190
		WinLeft=0.054124
		WinTop=139.5
		TabOrder=2
	End Object
	pnl_Keys=KeysPanel
}
