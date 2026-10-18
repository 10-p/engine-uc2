/*=============================================================================
	UC2ScriptSaveConfig.h: script SaveConfig() / StaticSaveConfig() on PC.
	Private to Core (UnCorSc.cpp, UnObj.cpp, UnMisc.cpp).

	Epic compiled UObject::execSaveConfig / execStaticSaveConfig out on PC and Xbox alike
	("rjp - calling SaveConfig() has mucho side-effects, so behavior should be identical on
	both PC and XBOX"; the Xbox reason is its certification rules about unprompted disk
	writes: settings persist through the profile there). On PC
	[Engine.Engine] ScriptSaveConfig (default True) turns the two natives back on; False
	is Epic's no-op. It is read once, at start-up (appInit), after the -ini: overrides.
=============================================================================*/

#ifndef UC2_SCRIPT_SAVECONFIG_H
#define UC2_SCRIPT_SAVECONFIG_H

// [Engine.Engine] ScriptSaveConfig, read from the config cache the first time it is asked (appInit asks once
// right after GConfig->Init and the -ini: overrides, so "first time" is start-up) and never again.
UBOOL UC2ScriptSaveConfigEnabled();

// What script's SaveConfig() / StaticSaveConfig() do when enabled: Object->SaveConfig() (every config property of
// its class), except for a globalconfig property that ANOTHER class deliberately sets (its defaultproperties): that
// one is left out, because the ini section it would go to is shared and would override that class's default.
// Logs one "Script SaveConfig: ..." line per call (the checks read it: a normal session has none).
void UC2SaveConfigFromScript( UObject* Object, UBOOL bStatic );

// Called by UClass::Link / UClass::Serialize right after a class default has loaded its config: records the
// inherited globalconfig properties this class deliberately gives another default than its superclass (UnObj.cpp).
void UC2NoteDefaultOverrides( UClass* Class );

// The SAVECONFIGKEYS console command (UObject::StaticExec): "SAVECONFIGKEYS <Class> <Property> [<Property>...]"
// writes the named config properties of the class's default object to the ini, and only those, the way
// UObject::SaveConfig writes a property. Always answers 1 (the command is ours). Honours ScriptSaveConfig.
UBOOL UC2SaveConfigKeys( const TCHAR* Args, FOutputDevice& Ar );

#endif
