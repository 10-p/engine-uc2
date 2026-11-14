// bot-variants: the bots of UCCharacters.u without a slot of their own on the character page, each the second switch
// of one slot of its weight class (patches/bot-variants/README.md)
class BotVariants extends Object
	abstract;

// The weapons and the seven adrenaline combos are the class's own (its RequiredEquipment and the ComboList of its
// LoadSpecificReferences), as indices of CharacterInformation's tables.
struct BotData
{
	var int			Slot;					// the slot: an index of CharacterInformation.Characters
	var string		Name;					// its CharacterInformation.BotNames entry
	var string		ClassName;
	var string		IconName;				// the slot's icon while the cursor is elsewhere
	var string		SelectedIconName;		// and under the cursor
	var byte		RaceWeaponIdx;
	var byte		MeleeWeaponIdx;
	var array<byte>	Combos;					// CharacterInformation.ComboClasses indices, in the ComboList's order
};

var const array<BotData> Bots;

// The bot of a slot (an index of Bots), or -1.
static final function int Find( int Slot )
{
	local int i;

	for ( i = 0; i < default.Bots.Length; i++ )
		if ( default.Bots[i].Slot == Slot )
			return i;
	return -1;
}

// The bot of a pawn class name (an index of Bots), or -1.
static final function int FindClass( string ClassName )
{
	local int i;

	if ( ClassName == "" )
		return -1;
	for ( i = 0; i < default.Bots.Length; i++ )
		if ( default.Bots[i].ClassName ~= ClassName )
			return i;
	return -1;
}

// The bot's index among the characters a profile unlocks (PlayerProfile.IsCharacterAvailable: the slots, then one per
// CharacterInformation.BotNames entry), or -1.
static final function int UnlockIndex( int b )
{
	if ( b < 0 || b >= default.Bots.Length )
		return -1;
	return class'CharacterInformation'.static.GetCharacterIndex(default.Bots[b].Name, true, true);
}

static final function string BotName( int b )
{
	if ( b < 0 || b >= default.Bots.Length )
		return "";
	return default.Bots[b].Name;
}

static final function string BotClass( int b )
{
	if ( b < 0 || b >= default.Bots.Length )
		return "";
	return default.Bots[b].ClassName;
}

static final function Material Icon( int b, bool bSelected )
{
	local string IconName;

	if ( b < 0 || b >= default.Bots.Length )
		return None;
	if ( bSelected )
		IconName = default.Bots[b].SelectedIconName;
	else
		IconName = default.Bots[b].IconName;
	return Material(DynamicLoadObject(IconName, class'Material'));
}

defaultproperties
{
	Bots(0)=(Slot=2,Name="Corrosion",ClassName="UCCharacters.UCPawn_Liandri_Corrosion",IconName="T_Portrait.Icon.Portrait_Icon_Corrosion_0",SelectedIconName="T_Portrait.Icon.Portrait_Icon_Corrosion_1",RaceWeaponIdx=4,MeleeWeaponIdx=5,Combos=(4,20,34,37,31,24,0))
	Bots(1)=(Slot=6,Name="Korig",ClassName="UCCharacters.UCPawn_Skaarj_Korig",IconName="T_Portrait.Icon.Portrait_Icon_Korig",SelectedIconName="T_Portrait.Icon.Portrait_Icon_Korig",RaceWeaponIdx=9,MeleeWeaponIdx=14,Combos=(3,20,21,25,31,22,0))
	Bots(2)=(Slot=9,Name="Mirage",ClassName="UCCharacters.UCPawn_Nakhti_Mirage",IconName="T_Portrait.Icon.Portrait_Icon_Mirage",SelectedIconName="T_Portrait.Icon.Portrait_Icon_Mirage",RaceWeaponIdx=7,MeleeWeaponIdx=4,Combos=(26,20,9,15,31,40,0))
	Bots(3)=(Slot=8,Name="Hyena",ClassName="UCCharacters.UCPawn_Nakhti_Hyena",IconName="T_Portrait.Icon.Portrait_Icon_Hyena",SelectedIconName="T_Portrait.Icon.Portrait_Icon_Hyena",RaceWeaponIdx=7,MeleeWeaponIdx=4,Combos=(26,20,6,15,31,40,0))
	Bots(4)=(Slot=11,Name="Nepthys",ClassName="UCCharacters.UCPawn_Nakhti_Nepthys",IconName="T_Portrait.Icon.Portrait_Icon_Nepthys",SelectedIconName="T_Portrait.Icon.Portrait_Icon_Nepthys",RaceWeaponIdx=7,MeleeWeaponIdx=10,Combos=(26,20,10,15,32,40,0))
	Bots(5)=(Slot=5,Name="Ra",ClassName="UCCharacters.UCPawn_Nakhti_Ra",IconName="T_Portrait.Icon.Portrait_Icon_Ra",SelectedIconName="T_Portrait.Icon.Portrait_Icon_Ra",RaceWeaponIdx=7,MeleeWeaponIdx=7,Combos=(26,20,13,15,31,40,0))
	Bots(6)=(Slot=14,Name="Memphis",ClassName="UCCharacters.UCPawn_Nakhti_Memphis",IconName="T_Portrait.Icon.Portrait_Icon_Memphis",SelectedIconName="T_Portrait.Icon.Portrait_Icon_Memphis",RaceWeaponIdx=7,MeleeWeaponIdx=10,Combos=(26,20,6,15,32,40,0))
	Bots(7)=(Slot=1,Name="Darius",ClassName="UCCharacters.UCPawn_Nakhti_Darius",IconName="T_Portrait.Icon.Portrait_Icon_Darius",SelectedIconName="T_Portrait.Icon.Portrait_Icon_Darius",RaceWeaponIdx=7,MeleeWeaponIdx=7,Combos=(26,20,10,15,31,40,0))
}
