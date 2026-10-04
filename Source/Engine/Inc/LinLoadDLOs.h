/**
 * This file contains all DLOs needed for linear loading
 */


/**
 * Generic function that loads a list of classes
 */
FORCEINLINE void LoadClassDLOs(const TCHAR* Classes[],UBOOL bBypassGC = FALSE)
{
	guard(LoadClassDLOs);
	// Load all of the specified classes
	for (INT Index = 0; Classes[Index] != NULL; Index++)
	{
#if defined(XBOX_ASSET_FORMAT) && !defined(_XBOX)
		LoadObject<UClass>(NULL,Classes[Index],NULL,LOAD_NoFail,NULL);
#else
		UClass* Loaded = LoadObject<UClass>(NULL,Classes[Index],NULL,LOAD_NoFail,NULL);
		// Determine if we wish to keep this forever
		if (bBypassGC == TRUE)
		{
			Loaded->AddToRoot();
		}
#endif
	}
	unguard;
}

/**
 * Generic function that loads a list of objects
 */
FORCEINLINE void LoadObjectDLOs(const TCHAR* Objs[],UBOOL bBypassGC = FALSE)
{
	guard(LoadObjectDLOs);
	// Load all of the specified objects
	for (INT Index = 0; Objs[Index] != NULL; Index++)
	{
#if defined(XBOX_ASSET_FORMAT) && !defined(_XBOX)
		LoadObject<UObject>(NULL,Objs[Index],NULL,LOAD_NoFail,NULL);
#else
		UObject* Loaded = LoadObject<UObject>(NULL,Objs[Index],NULL,LOAD_NoFail,NULL);
		// Determine if we wish to keep this forever
		if (bBypassGC == TRUE)
		{
			Loaded->AddToRoot();
		}
#endif
	}
	unguard;
}

/**
 * Generic function that loads a list of objects and moves them to a new outer
 */
FORCEINLINE void LoadObjectDLOs(TArray<UObject*>& LoadedObjs,const TCHAR* Objs[],
	UBOOL bBypassGC = FALSE)
{
	guard(LoadObjectDLOs);
	// Load all of the specified objects
	for (INT Index = 0; Objs[Index] != NULL; Index++)
	{
#if defined(XBOX_ASSET_FORMAT) && !defined(_XBOX)
		UObject* Object = LoadObject<UObject>(NULL,Objs[Index],NULL,LOAD_NoFail,NULL);
		// Add this object to the list
		LoadedObjs.AddUniqueItem(Object);
#else
		UObject* Loaded = LoadObject<UObject>(NULL,Objs[Index],NULL,LOAD_NoFail,NULL);
		// Determine if we wish to keep this forever
		if (bBypassGC == TRUE)
		{
			Loaded->AddToRoot();
		}
#endif
	}
	unguard;
}

/** The list of objects that the menu needs */
const TCHAR* MenuDLOs[] =
{
	"T_Portrait.Portrait_Char_Anubis",
	"T_Portrait.Portrait_Char_Apophis",
	"T_Portrait.Portrait_Char_Arclite",
	"T_Portrait.Portrait_Char_Brock",
	"T_Portrait.Portrait_Char_Calypso",
	"T_Portrait.Portrait_Char_Devastation",
	"T_Portrait.Portrait_Char_Garek",
	"T_Portrait.Portrait_Char_Gorge",
	"T_Portrait.Portrait_Char_Jackhammer",
	"T_Portrait.Portrait_Char_Judas",
	"T_Portrait.Portrait_Char_Kraag",
	"T_Portrait.Portrait_Char_Lauren",
	"T_Portrait.Portrait_Char_Lilith",
	"T_Portrait.Portrait_Char_Malcolm",
	"T_Portrait.Portrait_Char_MercAnubis",
	"T_Portrait.Portrait_Char_NecrisSelket",
	"T_Portrait.Portrait_Char_Null_LG",
	"T_Portrait.Portrait_Char_Raiden",
	"T_Portrait.Portrait_Char_Random",
	"T_Portrait.Portrait_Char_Raptor",
	"T_Portrait.Portrait_Char_Sapphire",
	"T_Portrait.Portrait_Char_Selket",
	"T_Portrait.Portrait_Char_Sobek",
	"T_Portrait.Portrait_Char_Syzygy",
	"T_Portrait.Portrait_Char_Szalor",
	"T_Portrait.Portrait_Char_Torg",
	"T_Portrait.Portrait_Gametype_CTF",
	"T_Portrait.Portrait_Gametype_DM",
	"T_Portrait.Portrait_Gametype_NS",
	"T_Portrait.Portrait_Gametype_OVF",
	"T_Portrait.Portrait_Gametype_Survival",
	"T_Portrait.Portrait_SP_Asc00",
	"T_Portrait.UI_Arena_AcidRain",
	"T_Portrait.UI_Arena_Apex",
	"T_Portrait.UI_Arena_Aqueduct",
	"T_Portrait.UI_Arena_Ascension",
	"T_Portrait.UI_Arena_AxonCompressor",
	"T_Portrait.UI_Arena_Azure",
	"T_Portrait.UI_Arena_Cancer",
	"T_Portrait.UI_Arena_ChemicalDawn",
	"T_Portrait.UI_Arena_Coldfusion",
	"T_Portrait.UI_Arena_Deadbolt",
	"T_Portrait.UI_Arena_Decay",
	"T_Portrait.UI_Arena_DeepCore",
	"T_Portrait.UI_Arena_Downfall",
	"T_Portrait.UI_Arena_Echo",
	"T_Portrait.UI_Arena_Emperor",
	"T_Portrait.UI_Arena_Eternal",
	"T_Portrait.UI_Arena_Falls",
	"T_Portrait.UI_Arena_Fury",
	"T_Portrait.UI_Arena_Gauntlet][",
	"T_Portrait.UI_Arena_Horizon",
	"T_Portrait.UI_Arena_Judgment",
	"T_Portrait.UI_Arena_Legacy",
	"T_Portrait.UI_Arena_Meridian",
	"T_Portrait.UI_Arena_Monoxide",
	"T_Portrait.UI_Arena_Morbias3",
	"T_Portrait.UI_Arena_Nexus",
	"T_Portrait.UI_Arena_Obelisk",
	"T_Portrait.UI_Arena_Octane",
	"T_Portrait.UI_Arena_Overflow",
	"T_Portrait.UI_Arena_Phoenix",
	"T_Portrait.UI_Arena_Praxis",
	"T_Portrait.UI_Arena_Prism",
	"T_Portrait.UI_Arena_Recoil",
	"T_Portrait.UI_Arena_Remnant",
	"T_Portrait.UI_Arena_Sever",
	"T_Portrait.UI_Arena_SkyReach",
	"T_Portrait.UI_Arena_SolarFlare",
	"T_Portrait.UI_Arena_Sovereign",
	"T_Portrait.UI_Arena_Tempest",
	"T_Portrait.UI_Arena_TotalControl",
	"T_Portrait.UI_Arena_Transistor",
	"T_Portrait.UI_Arena_Turmoil",
	"T_Portrait.UI_Arena_Undertow",
	"T_Portrait.UI_Arena_ViperPit",
	"T_Portrait.UI_Arena_Whisper",
	"T_Portrait.UI_Arena_Zenith",
	"T_Portrait.UI_Armory_CanisterBio_PortraitMov",
	"T_Portrait.UI_Armory_CanisterGrenade_PortraitMov",
	"T_Portrait.UI_Armory_Flak_PortraitMov",
	"T_Portrait.UI_Armory_RipJack",
	"T_Portrait.UI_Armory_RL_PortraitMov",
	"T_Portrait.UI_Armory_Shock_PortraitMov2",
	"T_Portrait.UI_Armory_Sniper_PortraitMov",
	"T_Portrait.UI_Armory_Stinger_PortraitMov",
	"T_Portrait.Portrait_Char_Random",
	"T_UI_2.Portrait_Unlockable_01_AscPre1",
	"T_UI_2.Portrait_Unlockable_03_AscPre3",
	"T_UI_2.Portrait_Unlockable_04_AscPre6",
	"T_UI_2.Portrait_Unlockable_05_AscPre7",
	"T_UI_2.Portrait_Unlockable_06_Asc01",
	"T_UI_2.Portrait_Unlockable_07_Asc02",
	"T_UI_2.Portrait_Unlockable_08_Asc03",
	"T_UI_2.Portrait_Unlockable_09_Asc04",
	"T_UI_2.Portrait_Unlockable_10_Asc05",
	"T_UI_2.Portrait_Unlockable_11_Asc06",
	"T_UI_2.Portrait_Unlockable_12_Asc07",
	"T_UI_2.Portrait_Unlockable_13_Asc09",
	"T_UI_2.Portrait_Unlockable_14_Asc10",
	"T_UI_2.Portrait_Unlockable_15_Asc11",
	"T_UI_2.Portrait_Unlockable_15_AscFin1",
	"T_UI_2.Portrait_Unlockable_16_AscFin2",
	"T_UI_2.Portrait_Unlockable_18_AscFin4",
	"T_UI_2.Portrait_Unlockable_19_AscFin6",
	"T_Portrait.Portrait_Char_Anubis02",
	"T_Portrait.Portrait_Char_Apophis",
	"T_Portrait.Portrait_Char_Arclite02",
	"T_Portrait.Portrait_Char_Brock02",
	"T_Portrait.Portrait_Char_Calypso",
	"T_Portrait.Portrait_Char_Devastation",
	"T_Portrait.Portrait_Char_Devastation02",
	"T_Portrait.Portrait_Char_Garek",
	"T_Portrait.Portrait_Char_Gorge",
	"T_Portrait.Portrait_Char_Gorge02",
	"T_Portrait.Portrait_Char_Jackhammer",
	"T_Portrait.Portrait_Char_Judas",
	"T_Portrait.Portrait_Char_Kraag",
	"T_Portrait.Portrait_Char_Lauren02",
	"T_Portrait.Portrait_Char_Lilith",
	"T_Portrait.Portrait_Char_Malcolm",
	"T_Portrait.Portrait_Char_Malcolm02",
	"T_Portrait.Portrait_Char_MercAnubis",
	"T_Portrait.Portrait_Char_NecrisSelket",
	"T_Portrait.Portrait_Char_Raiden",
	"T_Portrait.Portrait_Char_Raiden02",
	"T_Portrait.Portrait_Char_Raptor02",
	"T_Portrait.Portrait_Char_Sapphire02",
	"T_Portrait.Portrait_Char_Selket02",
	"T_Portrait.Portrait_Char_Sobek02",
	"T_Portrait.Portrait_Char_Syzygy",
	"T_Portrait.Portrait_Char_Szalor02",
	"T_Portrait.Portrait_Char_Torg02",
	TEXT("T_Portrait.SP.Portrait_SP_TotalControl"),
	TEXT("T_Portrait.SP.Portrait_SP_Skyreach"),
	TEXT("T_Portrait.SP.Portrait_SP_Recoil"),
	TEXT("T_Portrait.SP.Portrait_SP_Whisper"),
	TEXT("T_Portrait.SP.Portrait_SP_AcidRain"),
	TEXT("T_Portrait.SP.Portrait_SP_Apex"),
	TEXT("T_Portrait.Icon.Portrait_Icon_Corrosion_0"),
	TEXT("T_Portrait.Icon.Portrait_Icon_Corrosion_1"),
	TEXT("T_Portrait.Icon.Portrait_Icon_Sapphire_0"),
	TEXT("T_Portrait.Icon.Portrait_Icon_Sapphire_1"),
	TEXT("T_Portrait.character_sec.Portrait_Char_Sec_Devastation"),
	TEXT("T_Portrait.character_sec.Portrait_Char_Sec_Brock"),
	TEXT("T_Portrait.character_sec.Portrait_Char_Sec_Arclite"),
	TEXT("T_Portrait.character_sec.Portrait_Char_Sec_Raptor"),
	TEXT("T_Portrait.SP.Portrait_SP_Transistor"),
	TEXT("T_Portrait.SP.Portrait_SP_Turmoil"),
	TEXT("T_Portrait.SP.Portrait_SP_Aqueduct"),
	TEXT("T_Portrait.SP.Portrait_SP_Downfall"),
	TEXT("T_Portrait.SP.Portrait_SP_Emperor"),
	TEXT("T_Portrait.SP.Portrait_SP_Gauntlet]["),
	TEXT("T_Portrait.SP.Portrait_SP_MetalStorm"),
	TEXT("T_Portrait.SP.Portrait_SP_Monoxide"),
	TEXT("T_Portrait.SP.Portrait_SP_Octane"),
	TEXT("T_Portrait.SP.Portrait_SP_ViperPit"),
	TEXT("T_Portrait.SP.Portrait_SP_Asc00"),
	TEXT("T_Portrait.SP.Portrait_SP_Asc01"),
	TEXT("T_Portrait.SP.Portrait_SP_Asc11"),
	TEXT("T_Portrait.SP.Portrait_SP_Ascension"),
	TEXT("T_Portrait.SP.Portrait_SP_AxonCompressor"),
	TEXT("T_Portrait.SP.Portrait_SP_Azure"),
	TEXT("T_Portrait.SP.Portrait_SP_Cancer"),
	TEXT("T_Portrait.SP.Portrait_SP_ColdFusion"),
	TEXT("T_Portrait.SP.Portrait_SP_Decay"),
	TEXT("T_Portrait.SP.Portrait_SP_DeepCore"),
	TEXT("T_Portrait.SP.Portrait_SP_Echo"),
	TEXT("T_Portrait.SP.Portrait_SP_Eternal"),
	TEXT("T_Portrait.SP.Portrait_SP_EvilHands"),
	TEXT("T_Portrait.SP.Portrait_SP_Falls"),
	TEXT("T_Portrait.SP.Portrait_SP_Fury"),
	TEXT("T_Portrait.SP.Portrait_SP_Horizon"),
	TEXT("T_Portrait.SP.Portrait_SP_Legacy"),
	TEXT("T_Portrait.SP.Portrait_SP_Meridian"),
	TEXT("T_Portrait.SP.Portrait_SP_Morbias3"),
	TEXT("T_Portrait.SP.Portrait_SP_Nexus"),
	TEXT("T_Portrait.SP.Portrait_SP_Remnant"),
	TEXT("T_Portrait.SP.Portrait_SP_Sovereign"),
	TEXT("T_Portrait.SP.Portrait_SP_Prism"),
	TEXT("T_Portrait.SP.Portrait_SP_Praxis"),
	TEXT("T_Portrait.SP.Portrait_SP_Obelisk"),
	TEXT("T_Portrait.SP.Portrait_SP_Tempest"),
	TEXT("T_Portrait.SP.Portrait_SP_Sever"),
	TEXT("T_Portrait.SP.Portrait_SP_SolarFlare"),
	TEXT("T_Portrait.SP.Portrait_SP_Undertow"),
	TEXT("T_Portrait.SP.Portrait_SP_Phoenix"),
	TEXT("T_Portrait.SP.Portrait_SP_Overflow"),
	TEXT("T_Portrait.SP.Portrait_SP_ChemicalDawn"),
	TEXT("T_Portrait.SP.Portrait_SP_Judgment"),
	TEXT("T_Portrait.SP.Portrait_SP_Deadbolt"),
	TEXT("T_Portrait.SP.Portrait_SP_Zenith"),
	"T_Portrait.Portrait_Gametype_TDM",
	"T_UI_2.UI_Main_RightVertBar_Under",
	"T_UI_2.UI_Main_RightVertBar",
	"T_UI_2.UI_Start_Logo",
	"T_UI_2.UI_Start_PressNubs",
	"T_UI_2.UI_Start_MovieMask",
	"t_ui_2.UI_Main_List_Item",
	"t_ui_2.UI_Main_List_SelectedItem",
	"t_ui_2.UI_Main_InfoTop",
	"t_ui_2.UI_Main_Key_Dingy",
	"t_ui_2.UI_Main_Key_Hover",
	"t_ui_2.UI_Main_DownArrow",
	"t_ui_2.UI_Main_UpArrow",
	"T_Portrait.SP.Portrait_SP_Locked",
	"t_ui_2.UI_Main_Checkbox_off",
	"t_ui_2.UI_Main_CharInfo_Adrenaline",
	"T_HUD.HUD_Combo_Fill",
	"T_HUD.HUD_Combo_Line",
	"T_Portrait.Portrait_Char_Sec_Anubis",
	"T_Portrait.Portrait_Char_Sec_Apophis",
	"T_Portrait.Portrait_Char_Sec_Arclite",
	"T_Portrait.Portrait_Char_Sec_Brock",
	"T_Portrait.Portrait_Char_Sec_Calypso",
	"T_Portrait.Portrait_Char_Sec_Devastation",
	"T_Portrait.Portrait_Char_Sec_Garek",
	"T_Portrait.Portrait_Char_Sec_Gorge",
	"T_Portrait.Portrait_Char_Sec_Jackhammer",
	"T_Portrait.Portrait_Char_Sec_Judas",
	"T_Portrait.Portrait_Char_Sec_Kraag",
	"T_Portrait.Portrait_Char_Sec_Lauren",
	"T_Portrait.Portrait_Char_Sec_Lilith",
	"T_Portrait.Portrait_Char_Sec_Malcolm",
	"T_Portrait.Portrait_Char_Sec_MercAnubis",
	"T_Portrait.Portrait_Char_Sec_NecrisSelket",
	"T_Portrait.Portrait_Char_Sec_Raiden",
	"T_Portrait.Portrait_Char_Sec_Raptor",
	"T_Portrait.Portrait_Char_Sec_Sapphire",
	"T_Portrait.Portrait_Char_Sec_Selket",
	"T_Portrait.Portrait_Char_Sec_Sobek",
	"T_Portrait.Portrait_Char_Sec_Syzygy",
	"T_Portrait.Portrait_Char_Sec_Szalor",
	"T_Portrait.Portrait_Char_Sec_Torg",
	TEXT("T_Portrait.Locked.Portrait_Locked_Anubis"),
	TEXT("T_Portrait.Locked.Portrait_Locked_Torg"),
	TEXT("T_Portrait.Locked.Portrait_Locked_Brock"),
	TEXT("T_Portrait.Locked.Portrait_Locked_Selket"),
	TEXT("T_Portrait.Locked.Portrait_Locked_Devastation"),
	TEXT("T_Portrait.Locked.Portrait_Locked_Malcolm"),
	TEXT("T_Portrait.Locked.Portrait_Locked_Gorge"),
	TEXT("T_Portrait.Locked.Portrait_Locked_Raiden"),
	TEXT("t_ui_2.ScoreBoard.UI_ScoreBoards_DedBG"),
	TEXT("t_ui_2.Start.UI_Start_Logo"),
	TEXT("t_ui_2.Main.UI_Main_CharInfo_Adrenaline"),
	TEXT("T_HUD.Combo.HUD_Combo_Fill"),
	TEXT("T_HUD.Combo.HUD_Combo_Line"),
	TEXT("T_Portrait.Portrait_Icon_Complete"),
	TEXT("T_Portrait.Portrait_Char_Complete"),
	NULL
};

/** List of classes needed for the menu */
const TCHAR* MenuClassDLOs[] =
{
	"UCGameplay.AscensionRung",
	"UCGameplay.TournamentRung",
	"UCGameplay.ChallengesRung",
	"UCGameplay.UCOverflowGame",
	"UCGameplay.NaliSlaughterGame",
	"UCGameplay.UCSurvivalGame",
	"UCMenu.Arena_PanelMaplist",
	"UCMenu.UC2Extras",
	"UCMenu.UC2OnlineOptions",
	"UCMenu.UC2Profile",
	"UCMenu.UC2Settings_Main",
	"UnrealGame.CinematicHud",
	NULL
};

/**
 * Loads the list of objects needed in the menu (no classes)
 */
void LoadMenuObjects(void)
{
	guard(LoadMenuObjects);
	LoadObjectDLOs(MenuDLOs);
	unguard;
}

/**
 * Loads the list of classes needed in the menu (no classes)
 */
void LoadMenuClasses(void)
{
	guard(LoadMenuObjects);
	LoadClassDLOs(MenuClassDLOs);
	unguard;
}

/** List of objects needed throughout the game */
const TCHAR* GeneralDLOs[] =
{
	TEXT("T_HUD.HUD_Combos_01"),
	TEXT("T_HUD.HUD_Combos_02"),
	TEXT("T_HUD.HUD_Weapon_Melee"),
	TEXT("T_HUD.HUD_Weapon_Race"),
	TEXT("T_HUD.HUD_Weapon_Ranged"),
	TEXT("T_HUD.HUD_Weapon_HUDParts"),
	"T_Portrait.Portrait_Icon_Anubis_0",
	"T_Portrait.Portrait_Icon_Anubis_1",
	"T_Portrait.Portrait_Icon_Apophis_0",
	"T_Portrait.Portrait_Icon_Arclite_0",
	"T_Portrait.Portrait_Icon_Arclite_1",
	"T_Portrait.Portrait_Icon_Brock_0",
	"T_Portrait.Portrait_Icon_Brock_1",
	"T_Portrait.Portrait_Icon_Devastation_0",
	"T_Portrait.Portrait_Icon_Devastation_1",
	"T_Portrait.Portrait_Icon_Gorge_0",
	"T_Portrait.Portrait_Icon_Gorge_1",
	"T_Portrait.Portrait_Icon_Lauren_0",
	"T_Portrait.Portrait_Icon_Lauren_1",
	"T_Portrait.Portrait_Icon_Malcolm_0",
	"T_Portrait.Portrait_Icon_Malcolm_1",
	"T_Portrait.Portrait_Icon_MercAnubis_0",
	"T_Portrait.Portrait_Icon_MercAnubis_1",
	"T_Portrait.Portrait_Icon_Raiden_0",
	"T_Portrait.Portrait_Icon_Raiden_1",
	"T_Portrait.Portrait_Icon_Random",
	"T_Portrait.Portrait_Icon_Raptor_0",
	"T_Portrait.Portrait_Icon_Raptor_1",
	"T_Portrait.Portrait_Icon_Sapphire_0",
	"T_Portrait.Portrait_Icon_Sapphire_1",
	"T_Portrait.Portrait_Icon_Selket_0",
	"T_Portrait.Portrait_Icon_Selket_1",
	"T_Portrait.Portrait_Icon_Sobek_0",
	"T_Portrait.Portrait_Icon_Sobek_1",
	"T_Portrait.Portrait_Icon_Szalor_0",
	"T_Portrait.Portrait_Icon_Szalor_1",
	"T_Portrait.Portrait_Icon_Torg_0",
	"T_Portrait.Portrait_Icon_Torg_1",
	"T_Portrait.Portrait_Icon_Apophis",
	"T_Portrait.Portrait_Icon_Calypso",
	"T_Portrait.Portrait_Icon_Calypso_0",
	"T_Portrait.Portrait_Icon_Corrosion",
	"T_Portrait.Portrait_Icon_Darius",
	"T_Portrait.Portrait_Icon_Garek",
	"T_Portrait.Portrait_Icon_Garek_0",
	"T_Portrait.Portrait_Icon_Hyena",
	"T_Portrait.Portrait_Icon_Jackhammer",
	"T_Portrait.Portrait_Icon_Jackhammer_0",
	"T_Portrait.Portrait_Icon_Judas",
	"T_Portrait.Portrait_Icon_Judas_0",
	"T_Portrait.Portrait_Icon_Korig",
	"T_Portrait.Portrait_Icon_Kraag",
	"T_Portrait.Portrait_Icon_Kraag_0",
	"T_Portrait.Portrait_Icon_Lilith",
	"T_Portrait.Portrait_Icon_Lilith_0",
	"T_Portrait.Portrait_Icon_Memphis",
	"T_Portrait.Portrait_Icon_Mirage",
	"T_Portrait.Portrait_Icon_NecrisSelket_0",
	"T_Portrait.Portrait_Icon_NecrisSelket_1",
	"T_Portrait.Portrait_Icon_Nepthys",
	"T_Portrait.Portrait_Icon_Ra",
	"T_Portrait.Portrait_Icon_Syzygy",
	"T_Portrait.Portrait_Icon_Szyzgy_0",
	"T_ui_2.UI_Main_Key_Back",
	"T_ui_2.UI_Main_List_SelectedItem",
	"T_Portrait.UI_Arena_Null_Portrait",
	"T_Portrait.Portrait_Icon_Lock_0",
	"T_ui_2.UI_Main_Checkbox_off",
	"T_UI_2.UI_Main_Box03",
	"T_Fonts2.SuiGeneris_12pt",
	"T_Fonts2.SuiGeneris_18pt",
	"T_Fonts2.SuiGeneris_28pt",
	"T_Fonts2.Controller_Buttons_LG",
	"T_Fonts2.SuiGeneris_24pt",
	"T_Fonts2.SuiGeneris_20pt",
	"T_Fonts2.SuiGeneris_40pt",
	TEXT("t_ui_2.Main.UI_Main_Block"),
	TEXT("t_ui_2.Main.UI_Main_Box01_Outline01"),
	TEXT("t_ui_2.Main.UI_Main_LeftVertBar_Under"),
	TEXT("t_ui_2.Main.UI_Main_LeftVertBar"),
	TEXT("t_ui_2.Main.UI_Main_TitleBullet"),
	TEXT("t_ui_2.Main.UI_Main_CornerDots"),
	TEXT("t_ui_2.Main.UI_Main_TitlePlate_Under"),
	TEXT("t_ui_2.Main.UI_Main_TitlePlate"),
	TEXT("t_ui_2.Main.UI_Minus"),
	TEXT("t_ui_2.Main.UI_Plus"),
	TEXT("t_ui_2.Main.UI_Main_Hover"),
	TEXT("T_Portrait.Icon.Portrait_Icon_Lock_1"),
	TEXT("t_ui_2.Main.UI_Main_Box01"),
	TEXT("t_ui_2.Main.UI_Main_InfoTop"),
	TEXT("t_ui_2.Main.UI_Main_Box08"),
	TEXT("T_FX.Flares.FX_Flares_05"),
	TEXT("T_FX.Flares.NakhtiPrmaryFlare"),
	TEXT("T_FX.Flares.BoxFlare2"),
	TEXT("T_FX.Particles.FX_Part_Pickup_DeRes_Core"),
	TEXT("S_FX.Particles.Cube"),
	TEXT("T_FX.Particles.FX_Part_Shock_Combo_Distort2_Alpha"),
	TEXT("T_FX.Beams.FX_Beams_LaserTex"),
	TEXT("T_FX.Flares.FX_Flares_04"),
	TEXT("T_FX.Flares.FX_Flares_01"),
	TEXT("T_FX.Particles.FX_Part_Smokepuff2"),
	TEXT("T_FX.Smoke.SmokeExplo"),
	TEXT("T_FX.Flares.TechBit01"),
	TEXT("S_FX.Particles.Dom1"),
	TEXT("S_FX.Particles.Dom4"),
	TEXT("T_FX.Flares.box10"),
	TEXT("T_FX.Flares.RazorJackTail"),
	TEXT("T_FX.Flares.WavyA01"),
	TEXT("Engine.GRADIENT_Fade"),
	TEXT("Engine.MenuWhite"),
	TEXT("Engine.MenuBlack"),
	TEXT("Engine.MenuGray"),
	TEXT("Engine.BlobTexture"),
	TEXT("Engine.WhiteOpaque"),
	TEXT("T_FX.ColorRamps.FX_ColorRamps_TeleportIn"),
	TEXT("t_ui_2.Main.UI_Main_WhiteBlock"),
	TEXT("T_FX.ColorRamps.UI_PauseMenuRamp"),
	TEXT("T_FX.ColorRamps.BerserkRamp"),
	TEXT("T_FX.ColorRamps.FX_ColorRamps_Flash"),
	TEXT("T_FX.ColorRamps.FX_ColorRamps_SniperOverlay"),
	TEXT("T_FX.Particles.T_FX_Part_twobytwoblack"),
	TEXT("t_ui_2.ScoreBoard.UI_Scoreboard_bgBack"),
	TEXT("t_ui_2.ScoreBoard.UI_Scoreboard_bgLine"),
	TEXT("t_ui_2.ScoreBoard.UI_Scoreboard_bgBorder"),
	TEXT("t_ui_2.Main.UI_Main_Next_Arrow"),
	TEXT("t_ui_2.Main.UI_Button_Black"),
	TEXT("t_ui_2.Main.UI_Main_Previous_Arrow"),
	TEXT("t_ui_2.Main.UI_Button_White"),
	TEXT("t_ui_2.Main.UI_Main_List_Item"),
	TEXT("T_HUD.CrossHair.HUD_Crosshair_01"),
	TEXT("T_Crosshair.Misc.CH_Misc_04"),
	TEXT("Engine.WhiteTexture"),
	TEXT("T_CharacterSkins.Skarrj.Skarrj_spec2"),
	TEXT("T_FX.Skins.FX_DamageSkin"),
	TEXT("T_CharacterSkins.NakhtiMale.Rim_Light_Mask"),
	TEXT("T_WeaponSkins.Rim_Light_Mask_Leftside"),
	TEXT("S_FX.Particles.DistortRing"),
	TEXT("S_FX.Particles.Plant"),
	TEXT("S_FX.FX_Part_Main_Explo"),
	TEXT("T_CharacterSkins.Necris_Leather_Sphere_Map"),
	TEXT("T_Combo_UDamage_Temp.FlameGradient"),
	TEXT("T_Combo_UDamage_Temp.fire3"),
	TEXT("T_Combo_UDamage_Temp.FlickerFlare"),
	TEXT("Engine.WhiteSquareTexture"),
	TEXT("t_ui_2.Characters.UI_Char_2PSelector"),
	TEXT("T_FX.Particles.Blade_Heat2"),
	TEXT("t_ui_2.Controller.UI_Controller_Tab"),
	TEXT("t_ui_2.Controller.UI_Controller_IMG"),
	TEXT("t_ui_2.Controller.UI_Controller_Lines02"),
	TEXT("t_ui_2.Controller.UI_Controller_Lines01"),
	TEXT("t_ui_2.ScoreBoard.UI_ScoreBoards_DedBG"),
	TEXT("t_ui_2.Start.UI_Start_Logo"),
	TEXT("t_ui_2.Main.UI_Main_CharInfo_Adrenaline"),
	TEXT("T_HUD.Combo.HUD_Combo_Fill"),
	TEXT("T_HUD.Combo.HUD_Combo_Line"),
	TEXT("t_ui_2.Main.UI_Main_Key_Dingy"),
	TEXT("t_ui_2.Main.UI_Main_Key_Hover"),
	NULL
};

/** List of overdose specific loads */
const TCHAR* OverdoseClasses[] =
{
	TEXT("UCGameplay.UCOverflowGame"),
	TEXT("UCGameplay.UCHUD_Overflow"),
	TEXT("UCGameplay.UCHUD_Overflow_Split"),
	TEXT("UCGameplay.UCScoreboard_Overflow"),
	TEXT("UCEffects.UCFX_OverflowGoal_Green"),
	TEXT("UCEffects.UCFX_OverflowGoal_Green_Score"),
	TEXT("UCEffects.UCFX_OverflowBall_Green"),
	TEXT("UCEffects.UCFX_OverflowBall_Green_Held"),
	TEXT("UCEffects.UCFX_OverflowGoal_White"),
	TEXT("UCEffects.UCFX_OverflowGoal_White_Score"),
	TEXT("UCEffects.UCFX_OverflowBall_White"),
	TEXT("UCEffects.UCFX_OverflowBall_White_Held"),
	NULL
};

/** List of overdose specific loads */
const TCHAR* OverdoseObjects[] =
{
	TEXT("S_FX.Particles.OverdoseBall"),
	TEXT("S_Team.Team_CTF_FlagBaseB"),
	NULL
};

/**
 * Loads the needed items for a overdose game
 *
 * @param NewOuter the level to copy the object into
 */
void LoadOverdoseClassesObjects(TArray<UObject*>& LoadedObjs)
{
	guard(LoadOverdoseClassesObjects);
	LoadObjectDLOs(LoadedObjs,OverdoseObjects,FALSE);
	LoadClassDLOs(OverdoseClasses,FALSE);
	unguard;
}

/** List of CTF specific objects */
const TCHAR* CTFObjects[] =
{
	TEXT("S_Pickups.CTF_Flag.S_PU_CTF_Flag_Blue"),
	TEXT("T_Pickups.CTF_Flag.T_PU_CTF_flag_Banner01"),
	TEXT("T_Pickups.CTF_Flag.CTF_Flag_Lines_SD"),
	TEXT("T_Pickups.CTF_Flag.CTF_Flag_Lines_CB"),
	TEXT("T_Pickups.CTF_Flag.CTF_Flag_diffuse"),
	TEXT("T_Pickups.CTF_Flag.CTF_Flag_Lines_Tile"),
	TEXT("T_Pickups.CTF_Flag.CTF_Flag_Lines_Panner"),
	TEXT("T_Pickups.CTF_Flag.CTF_Flag_Lines_diffuse"),
	TEXT("S_Pickups.CTF_Flag.S_PU_CTF_Flag_Red"),
	NULL
};

/** List of CTF classes */
const TCHAR* CTFClasses[] =
{
	TEXT("UCGameplay.CTFGame"),
	TEXT("UCGameplay.UCHUD_CTF"),
	TEXT("UCGameplay.UCHUD_CTF_SPLIT"),
	TEXT("UCEffects.UCFX_CTFFlag_Blue"),
	TEXT("UCEffects.UCFX_CTFFlagBase_Blue"),
	TEXT("UCEffects.UCFX_CTFFlagDrop_Blue"),
	TEXT("UCEffects.UCFX_CTFFlag_Red"),
	TEXT("UCEffects.UCFX_CTFFlagBase_Red"),
	TEXT("UCEffects.UCFX_CTFFlagDrop_Red"),
	TEXT("UCEffects.UCCTFBlueFlag"),
	TEXT("UCEffects.UCCTFRedFlag"),
	NULL
};

/**
 * Loads the needed items for a CTF game. Builds a list of assets that
 * were loaded so they can be exported
 *
 * @param NewOuter the level to copy the object into
 */
void LoadCTFClassesObjects(TArray<UObject*>& LoadedObjs)
{
	guard(LoadCTFClassesObjects);
	LoadObjectDLOs(LoadedObjs,CTFObjects,FALSE);
	LoadClassDLOs(CTFClasses,FALSE);
	unguard;
}

/** List of classes needed throughout the game */
const TCHAR* GeneralClassDLOs[] =
{
	TEXT("UCGameplay.UCHUD_Split"),
	TEXT("UCGameplay.UCHUD_Training"),
	TEXT("UCGameplay.UCHUD_TeamGame"),
	TEXT("UCGameplay.UCHUD_TeamGame_Split"),
	TEXT("UCGameplay.UCHUD_Survival"),
	TEXT("UCGameplay.UCHUD_Survival_Split"),
	TEXT("UCGameplay.AscensionRung"),
	TEXT("UCGameplay.TournamentRung"),
	TEXT("UCGameplay.ChallengesRung"),
	NULL
};

/** List of common pawn/weapon classes needed for everyone */
const TCHAR* GeneralFXClasses[] =
{
	TEXT("Engine.Console"),
	TEXT("UCMenu.InGame_MainMenu"),
	TEXT("UCMenu.ConfirmJoinFriendMenu"),
	TEXT("UCMenu.ScoreboardPanelStats"),
	TEXT("UCMenu.UC2RungDone"),
	TEXT("UCMenu.UC2CharacterInfoPage"),
	TEXT("UCGameplay.UCScoreboard_Survival"),
	TEXT("UCGameplay.UCScoreboard_NaliSlaughter"),
	TEXT("UCGameplay.UCScoreboard_CTF"),
	TEXT("UCGameplay.UCScoreboard_TeamGame"),
	TEXT("UCEffects.UCEM_Pickup_Res"),
	TEXT("UCEffects.UCEM_Pickup_DeRes"),
	TEXT("UCEffects.UCFX_Combo_UDamage_ExpandingPawn"),
	TEXT("UCEffects.UCFX_Combo_UDamage_Effect"),
	TEXT("UCEffects.UCFX_Combo_UDamage_Effect_1P"),
	TEXT("UCEffects.UCEM_Trail_UDamage"),
	TEXT("UCCharacters.UCCombo_UDamage"),
	TEXT("UCCharacters.UCCombo_Invisibility"),
	TEXT("UCCharacters.UCCombo_Awaken"),
	TEXT("UCEffects.UCEM_WpnFX_DeRes_3p"),
	TEXT("UCEffects.UCEM_Misc_WorldBoundary"),
	TEXT("UCEffects.UCFX_DeRes"),
	TEXT("UCInventory.UCPickup_Adrenaline_Small"),
	TEXT("UCInventory.UCPickup_Adrenaline_Super"),
	TEXT("UCInventory.UCPickup_Ammo_Energy"),
	TEXT("UCInventory.UCPickup_Ammo_Energy_Small"),
	TEXT("UCInventory.UCPickup_Ammo_Explosive"),
	TEXT("UCInventory.UCPickup_Ammo_Explosive_Small"),
	TEXT("UCInventory.UCPickup_Health_Medium"),
	TEXT("UCInventory.UCPickup_Health_Small"),
	TEXT("UCInventory.UCPickup_Health_Super"),
	TEXT("UCInventory.UCPickup_PowerUp_Invis"),
	TEXT("UCInventory.UCPickup_PowerUp_UDamage"),
	TEXT("UCEffects.UCEM_RadioactiveGlow_High"),
	TEXT("UCEffects.UCEM_RadioactiveGlow_Medium"),
	TEXT("UCEffects.UCEM_RadioactiveGlow_Low"),
	TEXT("UCMutators.CampFireBomb"),
	NULL
};

/** List of classes that we always want to preload into Script.lin, Natives*.xpr */
const TCHAR* GeneralPreLoadClasses[] =
{
	TEXT("UCGameplay.UCPawn"),
	// Race base pawn data
	TEXT("UCCharacters.UCPawn_Nakhti"),
	TEXT("UCCharacters.UCPawn_Necris"),
	TEXT("UCCharacters.UCPawn_Liandri"),
	TEXT("UCCharacters.UCPawn_Skaarj"),
	TEXT("UCCharacters.UCPawn_Human"),
	TEXT("UCCharacters.UCPawn_Jugg"),
	// Race weapons
	TEXT("UCGameplay.UCWeap_Race"),
	TEXT("UCGameplay.UCWeap_Race_AltCharge"),
	TEXT("UCInventory.UCWeap_Race_Liandri"),
	TEXT("UCInventory.UCWeap_Race_Nakhti"),
	TEXT("UCInventory.UCWeap_Race_Necris"),
	TEXT("UCInventory.UCWeap_Race_Raiden"),
	TEXT("UCInventory.UCWeap_Race_Skaarj"),
	TEXT("UCInventory.UCWeap_Race_Terran"),
	// Race melee weapons
	TEXT("UCGameplay.UCWeap_Melee"),
	TEXT("UCGameplay.UCWeap_Melee_Jugg"),
	TEXT("UCGameplay.UCWeap_Melee_Liandri"),
	TEXT("UCGameplay.UCWeap_Melee_Nakhti"),
	TEXT("UCGameplay.UCWeap_Melee_Necris"),
	TEXT("UCGameplay.UCWeap_Melee_Skaarj"),
	TEXT("UCGameplay.UCWeap_Melee_Terran"),
	// Ranged weapons
	TEXT("UCGameplay.UCWeap_Ranged"),
	TEXT("UCInventory.UCWeap_CanisterGun"),
	TEXT("UCInventory.UCWeap_CanisterGun_Bio"),
	TEXT("UCInventory.UCWeap_CanisterGun_Gren"),
	TEXT("UCInventory.UCWeap_FlakCannon"),
	TEXT("UCInventory.UCWeap_RazorJack"),
	TEXT("UCInventory.UCWeap_RocketLauncher"),
	TEXT("UCInventory.UCWeap_ShockRifle_New"),
	TEXT("UCInventory.UCWeap_SniperRifle"),
	TEXT("UCInventory.UCWeap_Stinger"),
	TEXT("UCMutators.UCWeap_Instagib"),
	NULL
};

/**
 * Loads the list of objects needed for all gametypes
 */
void LoadGeneralObjects(void)
{
	guard(LoadGeneralObjects);
	LoadObjectDLOs(GeneralDLOs,TRUE);
	unguard;
}

/**
 * Loads the list of classes needed in the menu (no classes)
 */
void LoadGeneralClasses(void)
{
	guard(LoadGeneralClasses);
#ifdef _XBOX
	LoadClassDLOs(GeneralClassDLOs,TRUE);
#endif
	LoadClassDLOs(GeneralFXClasses,TRUE);
	// Loop through the preload classes and load all their stuff
	for (INT Index = 0; GeneralPreLoadClasses[Index] != NULL; Index++)
	{
		// Load the class that we are going to preload
		UClass* UCClass = LoadObject<UClass>(NULL,GeneralPreLoadClasses[Index],
			NULL,0,NULL);
		check(UCClass);
		// These need to hang around too
		UCClass->AddToRoot();
		TArray<FDLOInfo> DLOList;
		// Get the list of items it needs
		UCClass->GetDefaultActor()->eventBuildLoadList(DLOList, 0, 0);
		// Loop through the items and load them
		for (INT Index2 = 0; Index2 < DLOList.Num(); Index2++)
		{
#if defined(XBOX_ASSET_FORMAT) && !defined(_XBOX)
			// Load the object that it needs
			UObject::StaticLoadObject(DLOList(Index2).ObjectClass,
				NULL,*FString::Printf(TEXT("%s.%s"),*DLOList(Index2).PackageName,
				*DLOList(Index2).ObjectName),NULL,LOAD_NoFail,NULL);
#else
			// Load the object that it needs
			UObject* Loaded = UObject::StaticLoadObject(DLOList(Index2).ObjectClass,
				NULL,*FString::Printf(TEXT("%s.%s"),*DLOList(Index2).PackageName,
				*DLOList(Index2).ObjectName),NULL,LOAD_NoFail,NULL);
			// Make stay around forever
			Loaded->AddToRoot();
#endif
		}
	}
	unguard;
}

#ifdef LIN_SAVE
/** The name of the XPRs to auto load during lin save*/
const TCHAR* LoadXPRNames[] =
{
	TEXT("Melee"),
	TEXT("Race"),
	TEXT("Race_AltCharge"),
	TEXT("Ranged"),
	TEXT("CanisterGun"),
	TEXT("Bio"),
	TEXT("Gren"),
	TEXT("FlakCannon"),
	TEXT("Melee_Anubis"),
	TEXT("Melee_Anubis_Tutorial"),
	TEXT("Melee_Apophis"),
	TEXT("Melee_Arclite"),
	TEXT("Melee_Brock"),
	TEXT("Melee_Devastation"),
	TEXT("Melee_Gorge"),
	TEXT("Melee_Lauren"),
	TEXT("Melee_Malcolm"),
	TEXT("Melee_Raiden"),
	TEXT("Melee_Raptor"),
	TEXT("Melee_Sapphire"),
	TEXT("Melee_Selket"),
	TEXT("Melee_Sobek"),
	TEXT("Melee_Szalor"),
	TEXT("Melee_Torgr"),
	TEXT("Race_Liandri"),
	TEXT("Race_Liandri_Devastation"),
	TEXT("Race_Nakhti"),
	TEXT("Race_Necris"),
	TEXT("Race_Raiden"),
	TEXT("Race_Skaarj"),
	TEXT("Race_Terran"),
	TEXT("Race_Terran_Gorge"),
	TEXT("Race_Terran_Malcolm"),
	TEXT("RazorJack"),
	TEXT("RocketLauncher"),
	TEXT("ShockRifle"),
	TEXT("SniperRifle"),
	TEXT("Stinger"),
	TEXT("Instagib"),
	TEXT("Nali"),
	TEXT("Human"),
	TEXT("Malcolm"),
	TEXT("Sapphire"),
	TEXT("Jugg"),
	TEXT("Arclite"),
	TEXT("Gorge"),
	TEXT("Jackhammer"),
	TEXT("Liandri"),
	TEXT("Corrosion"),
	TEXT("Devastation"),
	TEXT("Raptor"),
	TEXT("SecurityBotVOne"),
	TEXT("Syzygy"),
	TEXT("Nakhti"),
	TEXT("Anubis"),
	TEXT("Anubis_Tutorial"),
	TEXT("Apophis"),
	TEXT("Darius"),
	TEXT("Hyena"),
	TEXT("Memphis"),
	TEXT("Mirage"),
	TEXT("Nepthys"),
	TEXT("Ra"),
	TEXT("Selket"),
	TEXT("Sobek"),
	TEXT("Sobek_Tutorial"),
	TEXT("Necris"),
	TEXT("Brock"),
	TEXT("Calypso"),
	TEXT("Judas"),
	TEXT("Lauren"),
	TEXT("Lilith"),
	TEXT("Necris_Selket"),
	TEXT("Skaarj"),
	TEXT("Garek"),
	TEXT("Korig"),
	TEXT("Kraag"),
	TEXT("Szalor"),
	TEXT("Torgr"),
	TEXT("Raiden"),
	TEXT("Tutorial_Dummy"),
	NULL
};
#endif