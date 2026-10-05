/*=============================================================================
	UC2ALDevice.cpp: OpenAL binding, and the Xbox's I3DL2 reverb presets as EFX.

	Native builds load libopenal (the openal-soft the build fetched, copied beside
	uc2.bin; else the system's) with dlopen when the audio device starts, so a
	machine without it still runs the game, silently. Under Emscripten (M11) the
	library is linked in and the table takes its symbols directly.
=============================================================================*/

#include "UC2ALAudioPrivate.h"

FUC2AL GAL;
static void* GALHandle = NULL;

UBOOL UC2ALBind( FString& Error )
{
	guard(UC2ALBind);
	if( GALHandle )
		return 1;
#ifdef __EMSCRIPTEN__
	Error = TEXT("UC2ALAudio is not built for the web yet (M11)");
	return 0;
#else
	// Beside the executable first (what tools/dev/mkrun-uc2.sh puts there), then the system's.
#ifdef _WIN32
	// Windows (M12): the build's own OpenAL32.dll (LoadLibrary), else an installed OpenAL (the system's
	// OpenAL32.dll router, or OpenAL Soft's soft_oal.dll).
	const TCHAR* Names[] = { TEXT("OpenAL32.dll"), TEXT("soft_oal.dll") };
#else
	const TCHAR* Names[] = { TEXT("libopenal.so.1"), TEXT("libopenal.so") };
#endif
	for( INT i=0; i<ARRAY_COUNT(Names) && !GALHandle; i++ )
	{
		FString Local = FString(appBaseDir()) + Names[i];
		GALHandle = appGetSystemDllHandle( *Local );
		if( !GALHandle )
			GALHandle = appGetSystemDllHandle( Names[i] );
	}
	if( !GALHandle )
	{
		Error = FString::Printf( TEXT("no OpenAL library (%s) beside the game or on the system"), Names[0] );
		return 0;
	}
	appMemzero( &GAL, sizeof(GAL) );
#define UC2AL_BIND(Name,Type) \
	GAL.Name = (Type)appGetDllExport( GALHandle, TEXT(#Name) ); \
	if( !GAL.Name ) { Error = TEXT("the OpenAL library has no ") TEXT(#Name); return 0; }
	UC2AL_CORE_FUNCS(UC2AL_BIND)
#undef UC2AL_BIND
	return 1;
#endif
	unguard;
}

void UC2ALUnbind()
{
	// The library stays loaded: its threads and atexit handlers belong to it, and unloading buys nothing.
	appMemzero( &GAL, sizeof(GAL) );
	GALHandle = NULL;
}

UBOOL UC2ALBindEFX()
{
	guard(UC2ALBindEFX);
#define UC2AL_BIND_EFX(Name,Type) \
	GAL.Name = (Type)GAL.alGetProcAddress( #Name ); \
	if( !GAL.Name ) return 0;
	UC2AL_EFX_FUNCS(UC2AL_BIND_EFX)
#undef UC2AL_BIND_EFX
	return 1;
	unguard;
}

ALenum UC2ALFormat( INT Channels )
{
	return Channels==2 ? AL_FORMAT_STEREO16 : AL_FORMAT_MONO16;
}

/*-----------------------------------------------------------------------------
	Reverb presets: DSI3DL2_ENVIRONMENT_PRESET_* of the Xbox SDK's DSound.h (XDK 5849),
	which XACTAudioSubsystem.cpp's GetAudioEffectFromEnum maps from EZoneAudioEffect.
	Fields: Room, RoomHF (mB), RoomRolloff, DecayTime (s), DecayHFRatio, Reflections (mB),
	ReflectionsDelay (s), Reverb (mB), ReverbDelay (s), Diffusion (%), Density (%), HFReference (Hz).
-----------------------------------------------------------------------------*/

const FUC2I3DL2 GUC2ZonePresets[UC2_NUM_ZONE_PRESETS] =
{
	{ TEXT("Default"),         -1000,   -100, 0.0f,  1.49f, 0.83f,  -2602, 0.007f,    200, 0.011f, 100.0f, 100.0f, 5000.0f },
	{ TEXT("Default2"),       -10000,      0, 0.0f,  1.00f, 0.50f, -10000, 0.020f, -10000, 0.040f, 100.0f, 100.0f, 5000.0f },
	{ TEXT("Generic"),         -1000,   -100, 0.0f,  1.49f, 0.83f,  -2602, 0.007f,    200, 0.011f, 100.0f, 100.0f, 5000.0f },
	{ TEXT("PaddedCell"),      -1000,  -6000, 0.0f,  0.17f, 0.10f,  -1204, 0.001f,    207, 0.002f, 100.0f, 100.0f, 5000.0f },
	{ TEXT("Room"),            -1000,   -454, 0.0f,  0.40f, 0.83f,  -1646, 0.002f,     53, 0.003f, 100.0f, 100.0f, 5000.0f },
	{ TEXT("Bathroom"),        -1000,  -1200, 0.0f,  1.49f, 0.54f,   -370, 0.007f,   1030, 0.011f, 100.0f,  60.0f, 5000.0f },
	{ TEXT("LivingRoom"),      -1000,  -6000, 0.0f,  0.50f, 0.10f,  -1376, 0.003f,  -1104, 0.004f, 100.0f, 100.0f, 5000.0f },
	{ TEXT("StoneRoom"),       -1000,   -300, 0.0f,  2.31f, 0.64f,   -711, 0.012f,     83, 0.017f, 100.0f, 100.0f, 5000.0f },
	{ TEXT("Auditorium"),      -1000,   -476, 0.0f,  4.32f, 0.59f,   -789, 0.020f,   -289, 0.030f, 100.0f, 100.0f, 5000.0f },
	{ TEXT("ConcertHall"),     -1000,   -500, 0.0f,  3.92f, 0.70f,  -1230, 0.020f,     -2, 0.029f, 100.0f, 100.0f, 5000.0f },
	{ TEXT("Cave"),            -1000,      0, 0.0f,  2.91f, 1.30f,   -602, 0.015f,   -302, 0.022f, 100.0f, 100.0f, 5000.0f },
	{ TEXT("Arena"),           -1000,   -698, 0.0f,  7.24f, 0.33f,  -1166, 0.020f,     16, 0.030f, 100.0f, 100.0f, 5000.0f },
	{ TEXT("Hangar"),          -1000,  -1000, 0.0f, 10.05f, 0.23f,   -602, 0.020f,    198, 0.030f, 100.0f, 100.0f, 5000.0f },
	{ TEXT("CarpetedHallway"), -1000,  -4000, 0.0f,  0.30f, 0.10f,  -1831, 0.002f,  -1630, 0.030f, 100.0f, 100.0f, 5000.0f },
	{ TEXT("Hallway"),         -1000,   -300, 0.0f,  1.49f, 0.59f,  -1219, 0.007f,    441, 0.011f, 100.0f, 100.0f, 5000.0f },
	{ TEXT("StoneCorridor"),   -1000,   -237, 0.0f,  2.70f, 0.79f,  -1214, 0.013f,    395, 0.020f, 100.0f, 100.0f, 5000.0f },
	{ TEXT("Alley"),           -1000,   -270, 0.0f,  1.49f, 0.86f,  -1204, 0.007f,     -4, 0.011f, 100.0f, 100.0f, 5000.0f },
	{ TEXT("Forest"),          -1000,  -3300, 0.0f,  1.49f, 0.54f,  -2560, 0.162f,   -613, 0.088f,  79.0f, 100.0f, 5000.0f },
	{ TEXT("City"),            -1000,   -800, 0.0f,  1.49f, 0.67f,  -2273, 0.007f,  -2217, 0.011f,  50.0f, 100.0f, 5000.0f },
	{ TEXT("Mountains"),       -1000,  -2500, 0.0f,  1.49f, 0.21f,  -2780, 0.300f,  -2014, 0.100f,  27.0f, 100.0f, 5000.0f },
	{ TEXT("Quarry"),          -1000,  -1000, 0.0f,  1.49f, 0.83f, -10000, 0.061f,    500, 0.025f, 100.0f, 100.0f, 5000.0f },
	{ TEXT("Plain"),           -1000,  -2000, 0.0f,  1.49f, 0.50f,  -2466, 0.179f,  -2514, 0.100f,  21.0f, 100.0f, 5000.0f },
	{ TEXT("ParkingLot"),      -1000,      0, 0.0f,  1.65f, 1.50f,  -1363, 0.008f,  -1153, 0.012f, 100.0f, 100.0f, 5000.0f },
	{ TEXT("SewerPipe"),       -1000,  -1000, 0.0f,  2.81f, 0.14f,    429, 0.014f,    648, 0.021f,  80.0f,  60.0f, 5000.0f },
	{ TEXT("Underwater"),      -1000,  -4000, 0.0f,  1.49f, 0.10f,   -449, 0.007f,   1700, 0.011f, 100.0f, 100.0f, 5000.0f },
	{ TEXT("NoReverb"),       -10000, -10000, 0.0f,  1.00f, 1.00f, -10000, 0.000f, -10000, 0.000f,   0.0f,   0.0f, 5000.0f },
};

static FLOAT MBToGain( INT MB )
{
	return MB<=-10000 ? 0.f : appPow( 10.f, MB/2000.f );
}

//
// I3DL2 -> EFX. Both are the same reverb model (EFX's standard reverb is I3DL2's parameter set; the EAX
// reverb adds the HF reference and modulation): levels from mB to linear gain, diffusion and density from
// percent to 0..1, everything clamped to the EFX ranges (I3DL2 allows a few values EFX does not, e.g. a
// 0 s reflections delay is fine but a 0.1 s minimum decay time applies).
//
void UC2ALLoadReverb( ALuint Effect, UBOOL bEAX, const FUC2I3DL2& P )
{
	guard(UC2ALLoadReverb);
	if( bEAX )
	{
		GAL.alEffecti( Effect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB );
		GAL.alEffectf( Effect, AL_EAXREVERB_DENSITY,				Clamp( P.Density/100.f, 0.f, 1.f ) );
		GAL.alEffectf( Effect, AL_EAXREVERB_DIFFUSION,			Clamp( P.Diffusion/100.f, 0.f, 1.f ) );
		GAL.alEffectf( Effect, AL_EAXREVERB_GAIN,				Clamp( MBToGain(P.Room), 0.f, 1.f ) );
		GAL.alEffectf( Effect, AL_EAXREVERB_GAINHF,				Clamp( MBToGain(P.RoomHF), 0.f, 1.f ) );
		GAL.alEffectf( Effect, AL_EAXREVERB_DECAY_TIME,			Clamp( P.DecayTime, 0.1f, 20.f ) );
		GAL.alEffectf( Effect, AL_EAXREVERB_DECAY_HFRATIO,		Clamp( P.DecayHFRatio, 0.1f, 2.f ) );
		GAL.alEffectf( Effect, AL_EAXREVERB_REFLECTIONS_GAIN,	Clamp( MBToGain(P.Reflections), 0.f, 3.16f ) );
		GAL.alEffectf( Effect, AL_EAXREVERB_REFLECTIONS_DELAY,	Clamp( P.ReflectionsDelay, 0.f, 0.3f ) );
		GAL.alEffectf( Effect, AL_EAXREVERB_LATE_REVERB_GAIN,	Clamp( MBToGain(P.Reverb), 0.f, 10.f ) );
		GAL.alEffectf( Effect, AL_EAXREVERB_LATE_REVERB_DELAY,	Clamp( P.ReverbDelay, 0.f, 0.1f ) );
		GAL.alEffectf( Effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR,	Clamp( P.RoomRolloff, 0.f, 10.f ) );
		GAL.alEffectf( Effect, AL_EAXREVERB_HFREFERENCE,			Clamp( P.HFReference, 1000.f, 20000.f ) );
	}
	else
	{
		GAL.alEffecti( Effect, AL_EFFECT_TYPE, AL_EFFECT_REVERB );
		GAL.alEffectf( Effect, AL_REVERB_DENSITY,				Clamp( P.Density/100.f, 0.f, 1.f ) );
		GAL.alEffectf( Effect, AL_REVERB_DIFFUSION,				Clamp( P.Diffusion/100.f, 0.f, 1.f ) );
		GAL.alEffectf( Effect, AL_REVERB_GAIN,					Clamp( MBToGain(P.Room), 0.f, 1.f ) );
		GAL.alEffectf( Effect, AL_REVERB_GAINHF,				Clamp( MBToGain(P.RoomHF), 0.f, 1.f ) );
		GAL.alEffectf( Effect, AL_REVERB_DECAY_TIME,			Clamp( P.DecayTime, 0.1f, 20.f ) );
		GAL.alEffectf( Effect, AL_REVERB_DECAY_HFRATIO,			Clamp( P.DecayHFRatio, 0.1f, 2.f ) );
		GAL.alEffectf( Effect, AL_REVERB_REFLECTIONS_GAIN,		Clamp( MBToGain(P.Reflections), 0.f, 3.16f ) );
		GAL.alEffectf( Effect, AL_REVERB_REFLECTIONS_DELAY,		Clamp( P.ReflectionsDelay, 0.f, 0.3f ) );
		GAL.alEffectf( Effect, AL_REVERB_LATE_REVERB_GAIN,		Clamp( MBToGain(P.Reverb), 0.f, 10.f ) );
		GAL.alEffectf( Effect, AL_REVERB_LATE_REVERB_DELAY,		Clamp( P.ReverbDelay, 0.f, 0.1f ) );
		GAL.alEffectf( Effect, AL_REVERB_ROOM_ROLLOFF_FACTOR,	Clamp( P.RoomRolloff, 0.f, 10.f ) );
	}
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
