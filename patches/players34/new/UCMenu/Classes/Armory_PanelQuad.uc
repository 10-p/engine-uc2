// players34: one player's quarter of the four-player armory (UC2Arena_QuadArmory). Epic's
// split-screen armory panel (Armory_PanelDual: the explosive and the energy lists, the short loadout) at half its
// height: the two lists side by side, the loadout under them.
// Players one and two use it as Epic's: the GUI's focus slots and the panel's controller mask. Players three and four
// have no focus slot of their own (the GUI has two), so their panel never takes the GUI's focus (bOwnInput): the page
// hands it their buttons (PadInput) and the panel moves its own lists, and draws the one in use as focused.
class Armory_PanelQuad extends Armory_PanelDual;

var() float ListWidth, ListHeight, EnergyLeft;	// the lists: width (of the quarter), height (of the screen), the energy
												// list's left (design pixels in the quarter: the list panels' bounds
												// are not converted, as Armory_PanelDual's WinTop=105.3)
var() float LoadoutTop, LoadoutHeight;			// the loadout under them: top (of the quarter), height (of the screen)
var() float TextScaleX, TextScaleY, LabelScale;	// the weapon names (condensed to fit half a quarter), the loadout's captions
var transient bool bOwnInput;					// players three and four: no GUI focus, PadInput drives it
var transient int ActiveList;					// bOwnInput: 0 the explosive list, 1 the energy list, -1 none

event InitComponent(GUIController InController, GUIComponent InOwner)
{
	local int i;

	Super.InitComponent(InController, InOwner);

	pnl_Explosive.WinLeft = 0;
	pnl_Explosive.WinTop = 0;
	pnl_Explosive.WinWidth = ListWidth;
	pnl_Explosive.WinHeight = ListHeight;

	pnl_Energy.WinLeft = EnergyLeft;
	pnl_Energy.WinTop = 0;
	pnl_Energy.WinWidth = ListWidth;
	pnl_Energy.WinHeight = ListHeight;

	pnl_Loadout.WinLeft = 0;
	pnl_Loadout.WinTop = LoadoutTop;
	pnl_Loadout.WinWidth = 1;
	pnl_Loadout.WinHeight = LoadoutHeight;

	for ( i = 0; i < chk_Explosive.Length; i++ )
		ScaleLabel(chk_Explosive[i].MyLabel, TextScaleX, TextScaleY);
	for ( i = 0; i < chk_Energy.Length; i++ )
		ScaleLabel(chk_Energy[i].MyLabel, TextScaleX, TextScaleY);

	ScaleLabel(pnl_Loadout.lbl_Explosive, LabelScale, LabelScale);
	ScaleLabel(pnl_Loadout.lbl_Energy, LabelScale, LabelScale);
	if ( ShortLoadoutPanel(pnl_Loadout) != None )
	{
		ScaleLabel(ShortLoadoutPanel(pnl_Loadout).lbl_Melee, LabelScale, LabelScale);
		ScaleLabel(ShortLoadoutPanel(pnl_Loadout).lbl_Race, LabelScale, LabelScale);
	}
}

final function ScaleLabel( GUILabel L, float X, float Y )
{
	if ( L != None )
	{
		L.ScaleX = X;
		L.ScaleY = Y;
	}
}

// players three and four: never the GUI's focus (it would be a focus slot of player one or two)
event SetFocus( GUIComponent Sender )
{
	if ( !bOwnInput )
		Super.SetFocus(Sender);
}

event bool FocusFirst( GUIComponent Sender )
{
	if ( bOwnInput )
		return false;

	return Super.FocusFirst(Sender);
}

// the lists are side by side: left and right go from one to the other too (Epic's down and up still do)
function MapControls()
{
	Super.MapControls();
	pnl_Explosive.Links[NDIR_Right] = pnl_Energy;
	pnl_Energy.Links[NDIR_Left] = pnl_Explosive;
}

// players three and four: Epic's Load without its pnl_Explosive.SetFocus (that would take a focus slot of player one
// or two)
function Load()
{
	local PlayerProfile Profile;

	if ( !bOwnInput )
	{
		Super.Load();
		return;
	}

	LoadExplosiveList();
	LoadEnergyList();

	Profile = PlayerOwner(PlayerIndex).GetProfile();
	if ( Profile != None )
	{
		pnl_Loadout.SetRaceIcon(CharInfo.GetCharacterRaceIndex(CharIndex, Profile.UsingVariant()));
		pnl_Loadout.SetMeleeIcon(CharInfo.GetCharacterMeleeIndex(CharIndex, Profile.UsingVariant()));
	}
	pnl_Loadout.HighlightEnergy(false);
	ShowList(0);
}

// the list in use drawn as focused (Armory_PanelBase.ExplosiveDrawItem/EnergyDrawItem draw the highlight of a focused
// list); the GUI's focus is not touched
final function ShowList( int NewList )
{
	ActiveList = NewList;
	SetListState(li_Explosive, NewList == 0);
	SetListState(li_Energy, NewList == 1);
	if ( NewList == 0 )
	{
		pnl_Loadout.ActivateEnergy(false);
		pnl_Loadout.ActivateExplosive(true);
	}
	else if ( NewList == 1 )
	{
		pnl_Loadout.ActivateExplosive(false);
		pnl_Loadout.ActivateEnergy(true);
	}
}

final function SetListState( GUIComponent C, bool bInUse )
{
	if ( bInUse )
		C.MenuState = MSAT_Focused;
	else if ( C.MenuState == MSAT_Focused )
		C.MenuState = MSAT_Blurry;
}

// a button of player three's or four's pad (from UC2Arena_QuadArmory.Players34XControl): the panel's own version of
// what the lists, ExplosiveOnClick, EnergyOnClick and BClicked do for players one and two
function PadInput( eXControllerCodes Code )
{
	switch ( Code )
	{
	case XC_PadUp:		Move(-1);	break;
	case XC_PadDown:	Move(1);	break;
	case XC_PadLeft:	if ( !IsReady() ) ShowList(0);	break;
	case XC_PadRight:	if ( !IsReady() ) ShowList(1);	break;
	case XC_A:
	case XC_Start:		Choose();	break;
	case XC_B:
	case XC_Back:		TakeBack();	break;
	}
}

final function Move( int Dir )
{
	local AvarisVertList L;
	local int i;

	if ( IsReady() || ActiveList < 0 )
		return;

	L = li_Explosive;
	if ( ActiveList == 1 )
		L = li_Energy;

	// not the list's Up/Down: at its ends they give the GUI's focus to the linked list
	i = L.Index + Dir;
	if ( i >= 0 && i < L.ItemCount() )
	{
		L.SetIndex(i);
		return;
	}

	// past the end of a list: on to the other one (Epic's Links down and up)
	if ( Dir > 0 && ActiveList == 0 )
		ShowList(1);
	else if ( Dir < 0 && ActiveList == 1 )
		ShowList(0);
}

final function Choose()
{
	if ( IsReady() || ActiveList < 0 )
		return;

	if ( ActiveList == 0 )
	{
		ChooseExplosive();
		ShowList(1);
		UpdateStatus();
		return;
	}

	// as EnergyOnClick: no explosive chosen yet takes the checked one (the profile's)
	if ( SelExplosiveIndex == -1 )
	{
		li_Explosive.Index = Max(0, GetCheckedIndex(li_Explosive));
		ChooseExplosive();
	}
	ChooseEnergy();
	ShowList(-1);
	UpdateStatus();
	ShowNextMenu();
}

final function ChooseExplosive()
{
	local int i, ListIdx;

	ListIdx = li_Explosive.Index;
	for ( i = 0; i < ExplosiveWeaponCount; i++ )
		chk_Explosive[i].SetValue(i == ListIdx, true);

	SetExplosiveWeapon(GetExplosiveWeaponIndex(li_Explosive.GetItem()));
}

final function ChooseEnergy()
{
	local int i, ListIdx;

	ListIdx = li_Energy.Index;
	for ( i = 0; i < EnergyWeaponCount; i++ )
		chk_Energy[i].SetValue(i == ListIdx);

	SetEnergyWeapon(GetEnergyWeaponIndex(li_Energy.GetItem()));
}

// B: the last choice taken back (as BClicked); false when there was none
final function bool TakeBack()
{
	if ( SelEnergyIndex != -1 )
	{
		SetEnergyWeapon(-1);
		li_Energy.SetIndex(Max(0, GetCheckedIndex(li_Energy)));
		ShowList(1);
		UpdateStatus();
		return true;
	}

	if ( SelExplosiveIndex != -1 )
	{
		SetExplosiveWeapon(-1);
		li_Explosive.SetIndex(Max(0, GetCheckedIndex(li_Explosive)));
		ShowList(0);
		UpdateStatus();
		return true;
	}

	return false;
}

// the panel's state for the page's log line: "<list in use: x, e or -> <explosive row>/<energy row> sel
// <explosive>/<energy>[ ready <explosive class>/<energy class>]"
final function string StatusText()
{
	local string S;

	if ( bOwnInput )
		S = ActiveList == 0 ? "x" : (ActiveList == 1 ? "e" : "-");
	else
		S = li_Explosive.MenuState == MSAT_Focused ? "x" : (li_Energy.MenuState == MSAT_Focused ? "e" : "-");

	S @= li_Explosive.Index $ "/" $ li_Energy.Index @ "sel" @ SelExplosiveIndex $ "/" $ SelEnergyIndex;
	if ( IsReady() )
		S @= "ready" @ CharInfo.ExplosiveWeaponClassName[SelExplosiveIndex] $ "/" $ CharInfo.EnergyWeaponClassName[SelEnergyIndex];
	return S;
}

defaultproperties
{
	WinWidth=0.425
	WinHeight=0.364583
	ListWidth=0.49
	ListHeight=0.216736
	EnergyLeft=138.7
	LoadoutTop=0.6
	LoadoutHeight=0.145
	TextScaleX=0.55
	TextScaleY=0.85
	LabelScale=0.65
	ActiveList=-1
}
