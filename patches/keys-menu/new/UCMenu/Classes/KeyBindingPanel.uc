/*==============================================================================
  The key list of the keyboard and mouse page (UC2Settings_Keys): a caption, two
  column headings and a listbox whose rows the page draws (action, key 1, key 2).
  Built like WeaponPriorityPanel so it sits in an ItemSelectionPanel and animates
  with the other items of a settings page.

  Ours (patch set keys-menu), not Epic's.
==============================================================================*/

class KeyBindingPanel extends Settings_ListPanel;
// (no include of GUI.huc: the class builds on its own from patches/keys-menu; it uses none of its macros)

var automated	GUILabel			lbl_Caption;		// "KEYS"
var automated	GUILabel			lbl_Key1, lbl_Key2;	// column headings, over the key columns
var automated	AvarisListBox		lb_Keys;			// one row per action
var()			AvarisVertList		li_Keys;			// the list of the listbox

// where the key columns start, as a fraction of a row's width (inside the item style's borders)
var()			float				ColumnKey1, ColumnKey2;

// the key column the keyboard or pad acts on (0 = key 1, 1 = key 2); set by the page
var()	transient	int				Column;

// colours of the column headings: the column the keyboard acts on, and the other one
var()			color				ActiveColumnColor, ColumnColor;

/*==========================================================================================================
function	::GetColumnLeft
Desc:		Left edge (pixels) of a key column inside a row of the given bounds.
==========================================================================================================*/
final function float GetColumnLeft( int Col, float X, float W )
{
	if ( Col == 0 )
		return X + W * ColumnKey1;
	return X + W * ColumnKey2;
}

/*==========================================================================================================
function	::GetRowInner
Desc:		A row's text area: the row bounds less the item style's borders (as GUIListBase's DrawItem).
==========================================================================================================*/
final function GetRowInner( out float X, out float Y, out float W, out float H )
{
	local GUIStyles S;

	S = li_Keys.ItemStyle;
	if ( S == None )
		return;

	Y += S.BorderOffsets[1];
	H -= S.BorderOffsets[3] + S.BorderOffsets[1];
	X += S.BorderOffsets[0];
	W -= S.BorderOffsets[2] + S.BorderOffsets[0];
}

/*==========================================================================================================
function	::ColumnAt
Desc:		The key column under a screen X position, or -1 when X is over the action names.
==========================================================================================================*/
final function int ColumnAt( float MouseX )
{
	local float X, Y, W, H;

	X = li_Keys.ClientBounds[0];
	W = li_Keys.ClientBounds[2] - li_Keys.ClientBounds[0];
	GetRowInner(X, Y, W, H);

	if ( MouseX >= GetColumnLeft(1, X, W) )
		return 1;
	if ( MouseX >= GetColumnLeft(0, X, W) )
		return 0;
	return -1;
}

function bool PreDrawPanel( Canvas C )
{
	local float X, Y, W, H;
	local bool bResult;

	bResult = Super.PreDrawPanel(C);

	// the headings sit over the key columns of the rows
	X = li_Keys.ClientBounds[0];
	W = li_Keys.ClientBounds[2] - li_Keys.ClientBounds[0];
	if ( W > 0 )
	{
		GetRowInner(X, Y, W, H);
		lbl_Key1.WinLeft = lbl_Key1.RelativeLeft(GetColumnLeft(0, X, W));
		lbl_Key2.WinLeft = lbl_Key2.RelativeLeft(GetColumnLeft(1, X, W));
	}

	if ( MenuState == MSAT_Focused || li_Keys.MenuState == MSAT_Focused )
	{
		lbl_Key1.ComponentColor = Column == 0 ? ActiveColumnColor : ColumnColor;
		lbl_Key2.ComponentColor = Column == 1 ? ActiveColumnColor : ColumnColor;
	}
	else
	{
		lbl_Key1.ComponentColor = ColumnColor;
		lbl_Key2.ComponentColor = ColumnColor;
	}
	// fade with the caption while the page animates (its colour is overridden then), opaque otherwise
	if ( lbl_Caption.bOverrideStyle )
	{
		lbl_Key1.ComponentColor.A = lbl_Caption.ComponentColor.A;
		lbl_Key2.ComponentColor.A = lbl_Caption.ComponentColor.A;
	}

	return bResult;
}

// =====================================================================================================================
// =====================================================================================================================
//  Settings_ListPanel Interface
// =====================================================================================================================
// =====================================================================================================================
function AvarisVertList GetList()
{
	return li_Keys;
}

// the panel keeps the width the page gives it (the rows are drawn by the page, not by their text)
function float GetAdjustedWidth( Canvas C )
{
	return -1;
}

function SetCaption( string NewCaption )
{
	Super.SetCaption(NewCaption);
	lbl_Caption.Caption = NewCaption;
}

// =====================================================================================================================
// =====================================================================================================================
//  GUIComponent Interface
// =====================================================================================================================
// =====================================================================================================================
event InitComponent(GUIController InController, GUIComponent InOwner)
{
	Super.InitComponent(InController, InOwner);

	li_Keys = AvarisVertList(lb_Keys.List);
	li_Keys.bUseStart = false;
	lbl_Caption.Hint = Hint;
}

function GUIComponent GetHoverControl()
{
	return lbl_Caption;
}

event Free()
{
	li_Keys = None;
	Super.Free();
}

function PerformOpenPulse(float OffsetDelay, float WaitDelay)
{
	local color PulseColor;

	lbl_Caption.OnArrival = OpenPulseArrival;
	lbl_Caption.OnEndAnimation = OpenPulseComplete;

	PulseColor = lbl_Caption.ComponentColor;
	lbl_Caption.Pulse(PulseColor, OffsetDelay);
	lb_Keys.Pulse(lb_Keys.ComponentColor, OffsetDelay);

	// when the item reaches this point, the block's fly-in animation begins (OnArrival)
	PulseColor.A = 220;
	lbl_Caption.Pulse(PulseColor, WaitDelay);

	PulseColor.A = 255;
	lbl_Caption.Pulse(PulseColor, 7 * SecondsPerFrame - WaitDelay);

	lbl_Caption.Pulse(lbl_Caption.GetComponentColor(true), 4 * SecondsPerFrame);

	lb_Keys.Pulse(lb_Keys.GetComponentColor(true), 11 * SecondsPerFrame);
}

function PerformClosePulse(float Delay, optional float Accelerator)
{
	local color PulseColor;

	bOverrideStyle = false;
	ComponentColor.A = 255;

	PulseColor = lbl_Caption.GetComponentColor(true);
	lbl_Caption.ComponentColor = PulseColor;
	lbl_Caption.bOverrideStyle = true;

	lbl_Caption.OnArrival = ClosePulseArrival;
	lbl_Caption.OnEndAnimation = ClosePulseArrivalComplete;

	lbl_Caption.Pulse(PulseColor, Delay);

	PulseColor.A = 255;
	lbl_Caption.Pulse(PulseColor, 3 * SecondsPerFrame);

	PulseColor.A = 0;
	if ( Accelerator == 0 )
		Accelerator = 1;
	lbl_Caption.Pulse(PulseColor, 8 * SecondsPerFrame, ,Accelerator);

	PulseColor = lb_Keys.GetComponentColor(true);
	lb_Keys.ComponentColor = PulseColor;
	lb_Keys.bOverrideStyle = true;
	lb_Keys.Pulse(PulseColor, Delay);

	PulseColor.A = 0;
	lb_Keys.Pulse(PulseColor, 11 * SecondsPerFrame,, Accelerator);
	lb_Keys.OnEndAnimation = ClosePulseComplete;
}

protected final function OpenPulseArrival( GUIComponent Sender, EAnimationType Type )
{
	OnArrival(Self, Type);
}

protected final function ClosePulseArrival( GUIComponent Sender, EAnimationType Type )
{
	OnArrival(Sender, Type);
}

protected final function OpenPulseComplete( GUIComponent Sender, EAnimationType Type )
{
	Sender.bOverrideStyle = false;
	Sender.OnArrival = None;
	Sender.OnEndAnimation = None;

	lb_Keys.bOverrideStyle = false;
	OnEndAnimation(Self, Type);
}

protected final function ClosePulseArrivalComplete( GUIComponent Sender, EAnimationType Type )
{
	Sender.OnArrival = None;
	Sender.OnEndAnimation = None;
}

protected final function ClosePulseComplete( GUIComponent Sender, EAnimationType Type )
{
	Sender.OnEndAnimation = None;
	OnEndAnimation(Self,Type);
}

function ResetOpenAnimation()
{
	lbl_Caption.ResetOpenAnimation();
	lb_Keys.ResetOpenAnimation();
}

function ResetCloseAnimation()
{
	Super.ResetCloseAnimation();
	lbl_Caption.ResetCloseAnimation();
	lb_Keys.ResetCloseAnimation();
}

DefaultProperties
{
	// Settings_ListPanel
	PanelCaption="KEYS"
	Hint="ENTER OR CLICK: CHANGE A KEY  X OR DELETE: CLEAR IT"

	// KeyBindingPanel
	ColumnKey1=0.43
	ColumnKey2=0.715
	ActiveColumnColor=(R=255,G=128,B=0,A=255)
	ColumnColor=(R=200,G=200,B=200,A=255)

	Begin object class=GUILabel name=KeysCaptionLabel
		WinWidth=0.40
		WinHeight=0.12
		WinLeft=0
		WinTop=0
		StyleName="Sui24pt_C8C8C8FF"
		BoundingType=SCALE_All
		ScalingType=SCALE_All
	End object
	lbl_Caption=KeysCaptionLabel

	Begin object class=GUILabel name=Key1HeadingLabel
		Caption="KEY 1"
		WinWidth=0.22
		WinHeight=0.10
		WinLeft=0.43
		WinTop=0.02
		StyleName="Sui18pt_C8C8C8FF"
		bOverrideStyle=true
		ComponentColor=(R=200,G=200,B=200,A=255)
		BoundingType=SCALE_All
		ScalingType=SCALE_All
	End object
	lbl_Key1=Key1HeadingLabel

	Begin object class=GUILabel name=Key2HeadingLabel
		Caption="KEY 2"
		WinWidth=0.22
		WinHeight=0.10
		WinLeft=0.715
		WinTop=0.02
		StyleName="Sui18pt_C8C8C8FF"
		bOverrideStyle=true
		ComponentColor=(R=200,G=200,B=200,A=255)
		BoundingType=SCALE_All
		ScalingType=SCALE_All
	End object
	lbl_Key2=Key2HeadingLabel

	Begin object class=AvarisListBox name=KeysListBox
		WinWidth=1.0
		WinHeight=0.86
		WinLeft=0
		WinTop=0.14
		StyleName="ListBox"
		bNeverFocus=false
		BoundingType=SCALE_All
		ScalingType=SCALE_All
	End object
	lb_Keys=KeysListBox
}
