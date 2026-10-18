// players34: the armory for three or four players (UC2Arena_Character.CloseComplete opens it
// when more than two have joined; one player keeps Epic's UC2Arena_Armory, two its UC2Arena_DualArmory). Four quarters
// in a 2x2 grid (Armory_PanelQuad: Epic's split-screen panel at half height), player one top left, two top right, three
// bottom left, four bottom right (the corners of their 3P/4P tags); with three players the fourth quarter stays empty.
// Players one and two go through Epic's UC2Arena_DualArmory code (their panels are pnl_PlayerArmory, the GUI's two focus
// slots); players three and four through Players34XControl (their buttons, from UC2Arena_DualBase) and their own panels.
// Every player chooses an explosive and an energy weapon; the match starts when all are ready (IsFinished). B takes
// a player's last choice back; with nothing chosen anywhere, B of player one or two goes back to the character page
// (Epic's rule; players three and four's B then does nothing). Their loadout goes into their (transient) profile, which
// makes their spawn URL (GameStartInfo.GetSpawnURL: ?EnergyWeapon= ?ExplosiveWeapon=).
class UC2Arena_QuadArmory extends UC2Arena_DualArmory;

var()	Armory_PanelQuad	pnl_Players34[2];	// players three and four (child viewports 2 and 3); None when absent
var()	float				PanelTop[2];		// the two rows of quarters

function CreateArmoryPanel()
{
	local int i;
	local Armory_PanelQuad P;
	local PlayerController PC;

	for ( i = 0; i < Min(Controller.GetViewportCount(true), 4); i++ )
	{
		if ( (i < MAX_VIEWPORTS && pnl_PlayerArmory[i] != None) || (i >= MAX_VIEWPORTS && pnl_Players34[i - MAX_VIEWPORTS] != None) )
			continue;

		P = new(Self) class'Armory_PanelQuad';
		P.PlayerIndex = i;
		P.bOwnInput = i >= MAX_VIEWPORTS;
		P.ScalingType = SCALE_All;
		P.BoundingType = SCALE_All;
		P.SetDirty = SetDirty;
		P.ShowNextMenu = ShowNextMenu;
		P.UpdateStatus = PanelStatus;
		P.WinLeft = PanelLeft[i % 2];
		P.WinTop = PanelTop[i / 2];

		WeightedInsertControl(P);

		PC = PlayerOwner(i);
		if ( PC != None )
			P.SetControllerMask(1 << PC.GamepadID, true);

		P.UpdateCharIndex();

		if ( i < MAX_VIEWPORTS )
			pnl_PlayerArmory[i] = P;
		else
			pnl_Players34[i - MAX_VIEWPORTS] = P;
	}
	ArmoryStatus("page opened");
}

function bool IsFinished()
{
	local int i;

	for ( i = 0; i < 2; i++ )
		if ( pnl_Players34[i] != None && !pnl_Players34[i].IsReady() )
			return false;

	return Super.IsFinished();
}

// a button of player three's or four's pad (UC2Arena_DualBase.InternalOnXControllerEvent): their own panel's
function bool Players34XControl( int PlayerIndex, eXControllerCodes Code )
{
	local Armory_PanelQuad P;
	local PlayerController PC;

	if ( bBlockingAnimation || Controller.ActivePage != Self || PlayerIndex < MAX_VIEWPORTS || PlayerIndex >= 4 )
		return true;

	P = pnl_Players34[PlayerIndex - MAX_VIEWPORTS];
	if ( P == None )
		return true;

	P.PadInput(Code);
	PC = PlayerOwner(PlayerIndex);
	ArmoryStatus("player" @ (PlayerIndex + 1) @ "port" @ (PC != None ? int(PC.GamepadID) : 255) @ GetEnum(enum'eXControllerCodes', Code));
	return true;
}

function bool BClicked()
{
	local int i, PlayerIndex;

	PlayerIndex = GetPlayerIndex();
	if ( PlayerIndex >= MAX_VIEWPORTS )
		return true;

	// players three or four have chosen something: players one and two cannot leave the page yet (as Epic's rule for
	// the other panel)
	if ( pnl_PlayerArmory[PlayerIndex] != None && pnl_PlayerArmory[PlayerIndex].SelExplosiveIndex == -1 )
	{
		for ( i = 0; i < 2; i++ )
		{
			if ( pnl_Players34[i] != None && pnl_Players34[i].SelExplosiveIndex != -1 )
			{
				ArmoryStatus("player" @ (PlayerIndex + 1) @ "B: players three or four have chosen");
				return true;
			}
		}
	}

	if ( Super.BClicked() )
	{
		ArmoryStatus("player" @ (PlayerIndex + 1) @ "B");
		return true;
	}
	return false;
}

function ConfirmLaunchGame( GUIPage ClosingMenu, bool bCancelled )
{
	local int PlayerIndex;

	PlayerIndex = GetPlayerIndex();
	if ( PlayerIndex < MAX_VIEWPORTS )
	{
		Super.ConfirmLaunchGame(ClosingMenu, bCancelled);
		return;
	}

	if ( bCancelled && PlayerIndex < 4 && pnl_Players34[PlayerIndex - MAX_VIEWPORTS] != None )
		pnl_Players34[PlayerIndex - MAX_VIEWPORTS].TakeBack();

	Super(UC2Arena_Armory).ConfirmLaunchGame(ClosingMenu, bCancelled);
}

function UpdateSelectedCharacter( string CharacterClassName )
{
	local int PlayerIndex;

	PlayerIndex = GetPlayerIndex();
	if ( PlayerIndex < MAX_VIEWPORTS )
	{
		Super.UpdateSelectedCharacter(CharacterClassName);
		return;
	}

	if ( Controller.bCurMenuInitialized && PlayerIndex < 4 && pnl_Players34[PlayerIndex - MAX_VIEWPORTS] != None )
		pnl_Players34[PlayerIndex - MAX_VIEWPORTS].UpdateCharIndex(CharacterClassName);

	Super(UC2Arena_Armory).UpdateSelectedCharacter(CharacterClassName);
}

function LoadSettings()
{
	local int i;

	for ( i = 0; i < 2; i++ )
		if ( pnl_Players34[i] != None )
			pnl_Players34[i].Load();

	Super.LoadSettings();
}

function SaveSettings( optional bool bNoWrite )
{
	local int i;

	for ( i = 0; i < 2; i++ )
		if ( pnl_Players34[i] != None )
			pnl_Players34[i].Save();

	Super.SaveSettings(bNoWrite);
}

function Refresh( bool bCharacterOnly )
{
	local int i;

	Super.Refresh(bCharacterOnly);
	for ( i = 0; i < 2; i++ )
		if ( pnl_Players34[i] != None )
			pnl_Players34[i].RefreshPanel();
}

function GetInfoText( out string InfoTitle, out string InfoText )
{
	if ( GetPlayerIndex() < MAX_VIEWPORTS )
		Super.GetInfoText(InfoTitle, InfoText);
}

function bool ValidXControl( byte iCode )
{
	if ( GetPlayerIndex() >= MAX_VIEWPORTS )
		return false;

	return Super.ValidXControl(iCode);
}

function BeginOpenAnimation()
{
	local int i;
	local Armory_PanelQuad P;

	Super.BeginOpenAnimation();

	// players three and four slide in as one and two: from the left and from the right
	for ( i = 0; i < 2; i++ )
	{
		P = pnl_Players34[i];
		if ( P == None )
			continue;

		if ( i == 0 )
		{
			P.WinLeft = P.RelativeLeft(-P.ActualWidth());
			P.Animate(PanelLeft[0] + 3, P.WinTop, 4 * SecondsPerFrame);
			P.Animate(PanelLeft[0], P.WinTop, 2 * SecondsPerFrame);
		}
		else
		{
			P.WinLeft = P.RelativeLeft(640);
			P.Animate(PanelLeft[1] - 3, P.WinTop, 4 * SecondsPerFrame);
			P.Animate(PanelLeft[1], P.WinTop, 2 * SecondsPerFrame);
		}
	}
}

function BeginTitleCancelFade()
{
	local int i;
	local Armory_PanelQuad P;

	Super.BeginTitleCancelFade();

	for ( i = 0; i < 2; i++ )
	{
		P = pnl_Players34[i];
		if ( P == None )
			continue;

		P.Animate(P.WinLeft + (i == 0 ? 3 : -3), P.WinTop, 2 * SecondsPerFrame);
		P.Animate(i == 0 ? P.RelativeLeft(-P.ActualWidth()) : P.RelativeLeft(640), P.WinTop, 4 * SecondsPerFrame);
	}
}

// the panels' UpdateStatus delegate: a list moved or a weapon was chosen (players one and two: their focused list)
function PanelStatus()
{
	ArmoryStatus("port" @ Controller.ControllerId);
}

// "players34 armory (<why>): 1P <panel>; 2P <panel>; 3P <panel>; 4P <panel>; players <n>" (DevUI), <panel> as
// Armory_PanelQuad.StatusText or "none"
final function ArmoryStatus( string Why )
{
	local int i;
	local string S;
	local Armory_PanelQuad P;

	for ( i = 0; i < 4; i++ )
	{
		if ( i < MAX_VIEWPORTS )
			P = Armory_PanelQuad(pnl_PlayerArmory[i]);
		else
			P = pnl_Players34[i - MAX_VIEWPORTS];

		S @= (i + 1) $ "P" @ (P != None ? P.StatusText() : "none") $ ";";
	}
	log("players34 armory (" $ Why $ "):" $ S @ "players" @ Controller.GetViewportCount(true), 'DevUI');
}

defaultproperties
{
	PanelLeft(0)=44
	PanelLeft(1)=324
	PanelTop(0)=68
	PanelTop(1)=245
}
