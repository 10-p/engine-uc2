/*=============================================================================
	UC2ALAudioPrivate.h: internals of the UC2 OpenAL audio device.

	Three layers, one file each:
	  UC2AudioData.cpp       the cooked data: cue tables, wave bank indexes, Vorbis decoding
	  UC2ALDevice.cpp        OpenAL: binding (dlopen natively, static on the web), device,
	                         context, EFX reverb with the I3DL2 presets of the Xbox
	  UC2ALAudioSubsystem.cpp  the XACT behaviour: banks, cues, variations, emitters,
	                         voices, music, categories, zones, pause, level change
=============================================================================*/

#ifndef _INC_UC2ALAUDIOPRIVATE
#define _INC_UC2ALAUDIOPRIVATE

#include "UC2ALAudio.h"

// Natively nothing calls OpenAL by name (the library is loaded at run time). On the web it is linked in, and
// UC2ALDevice.cpp takes the prototypes to fill the table with the symbols themselves.
#ifndef UC2AL_WITH_PROTOTYPES
#define AL_NO_PROTOTYPES 1
#define ALC_NO_PROTOTYPES 1
#endif
#include "AL/al.h"
#include "AL/alc.h"
#include "AL/alext.h"
#include "AL/efx.h"

/*-----------------------------------------------------------------------------
	OpenAL entry points. Natively libopenal is loaded with dlopen when the device
	starts (no library, no device: the game runs silent); under Emscripten (M11)
	the same table is filled with the statically linked symbols.
-----------------------------------------------------------------------------*/

#define UC2AL_CORE_FUNCS(X) \
	X(alcOpenDevice, LPALCOPENDEVICE) \
	X(alcCloseDevice, LPALCCLOSEDEVICE) \
	X(alcCreateContext, LPALCCREATECONTEXT) \
	X(alcDestroyContext, LPALCDESTROYCONTEXT) \
	X(alcMakeContextCurrent, LPALCMAKECONTEXTCURRENT) \
	X(alcGetError, LPALCGETERROR) \
	X(alcGetString, LPALCGETSTRING) \
	X(alcGetIntegerv, LPALCGETINTEGERV) \
	X(alcIsExtensionPresent, LPALCISEXTENSIONPRESENT) \
	X(alcGetProcAddress, LPALCGETPROCADDRESS) \
	X(alGetString, LPALGETSTRING) \
	X(alGetError, LPALGETERROR) \
	X(alIsExtensionPresent, LPALISEXTENSIONPRESENT) \
	X(alGetProcAddress, LPALGETPROCADDRESS) \
	X(alDistanceModel, LPALDISTANCEMODEL) \
	X(alDopplerFactor, LPALDOPPLERFACTOR) \
	X(alListenerf, LPALLISTENERF) \
	X(alListener3f, LPALLISTENER3F) \
	X(alListenerfv, LPALLISTENERFV) \
	X(alGenSources, LPALGENSOURCES) \
	X(alDeleteSources, LPALDELETESOURCES) \
	X(alSourcef, LPALSOURCEF) \
	X(alSource3f, LPALSOURCE3F) \
	X(alSourcei, LPALSOURCEI) \
	X(alSource3i, LPALSOURCE3I) \
	X(alGetSourcei, LPALGETSOURCEI) \
	X(alSourcePlay, LPALSOURCEPLAY) \
	X(alSourceStop, LPALSOURCESTOP) \
	X(alSourcePause, LPALSOURCEPAUSE) \
	X(alSourceRewind, LPALSOURCEREWIND) \
	X(alSourceQueueBuffers, LPALSOURCEQUEUEBUFFERS) \
	X(alSourceUnqueueBuffers, LPALSOURCEUNQUEUEBUFFERS) \
	X(alGenBuffers, LPALGENBUFFERS) \
	X(alDeleteBuffers, LPALDELETEBUFFERS) \
	X(alBufferData, LPALBUFFERDATA) \
	X(alBufferiv, LPALBUFFERIV)

#define UC2AL_EFX_FUNCS(X) \
	X(alGenEffects, LPALGENEFFECTS) \
	X(alDeleteEffects, LPALDELETEEFFECTS) \
	X(alEffecti, LPALEFFECTI) \
	X(alEffectf, LPALEFFECTF) \
	X(alGenFilters, LPALGENFILTERS) \
	X(alDeleteFilters, LPALDELETEFILTERS) \
	X(alFilteri, LPALFILTERI) \
	X(alFilterf, LPALFILTERF) \
	X(alGenAuxiliaryEffectSlots, LPALGENAUXILIARYEFFECTSLOTS) \
	X(alDeleteAuxiliaryEffectSlots, LPALDELETEAUXILIARYEFFECTSLOTS) \
	X(alAuxiliaryEffectSloti, LPALAUXILIARYEFFECTSLOTI) \
	X(alAuxiliaryEffectSlotf, LPALAUXILIARYEFFECTSLOTF)

struct FUC2AL
{
#define UC2AL_DECLARE(Name,Type) Type Name;
	UC2AL_CORE_FUNCS(UC2AL_DECLARE)
	UC2AL_EFX_FUNCS(UC2AL_DECLARE)
#undef UC2AL_DECLARE
};
extern FUC2AL GAL;

/*-----------------------------------------------------------------------------
	The cooked data (format: docs/porting/analysis/audio.md, "Cooked format").
-----------------------------------------------------------------------------*/

enum { UC2_LOOP_INFINITE = 65535 };

// Playlist types of a play event (XACT 'Playlist Type').
enum EUC2Playlist
{
	PL_Random				= 0,
	PL_Ordered				= 1,
	PL_Shuffle				= 2,
	PL_ParameterControlled	= 3,
	PL_RandomNoRepeat		= 4,
	PL_OrderedFromRandom	= 5,
};

// How a wave is stored. Cooked: an Ogg Vorbis stream in the bank's .ogp. Xbox (M16): an entry of a shipped .xwb.
enum EUC2WaveFormat
{
	WF_Vorbis		= 0,
	WF_PCM8			= 1,
	WF_PCM16		= 2,
	WF_XboxADPCM	= 3,
	WF_WMA			= 4,	// M_Adoratrice.xwb: an ASF file in the bank; not decoded (no WMA decoder)
};

struct FUC2Wave
{
	FString		Name;
	INT			Offset, Size, Rate, Channels, Frames, LoopStart, LoopLength;
	INT			Format;			// EUC2WaveFormat; Offset/Size: bytes in the bank's data file (.ogp, or the .xwb)
	UBOOL		bMissing;
	ALuint		Buffer;			// decoded as is (0 = not yet)
	ALuint		MonoBuffer;		// downmixed for 3D voices (stereo waves only)
	UBOOL		bDecodeFailed;
	DOUBLE		LastUsed;		// M16: the decoded-wave cache evicts the least recently used buffers
	FUC2Wave() : Offset(0), Size(0), Rate(0), Channels(0), Frames(0), LoopStart(0), LoopLength(0), Format(WF_Vorbis),
		bMissing(0), Buffer(0), MonoBuffer(0), bDecodeFailed(0), LastUsed(0.0) {}
};

struct FUC2WaveBank
{
	FString				Name;		// wave bank name (localised banks share their base bank's name)
	FString				File;		// index file it was read from, e.g. A_Announcer_deu.uc2wb
	FString				Lang;
	FString				DataFile;	// path relative to the cooked Sounds directory
	UBOOL				bStream;	// music: streamed from its own file
	UBOOL				bPersist;
	UBOOL				bXbox;		// M16: a shipped .xwb (DataFile is the .xwb; waves are read from it one by one)
	TArray<FUC2Wave>	Waves;
	TArray<BYTE>		Data;		// the .ogp, loaded on first use (cooked banks only)
	UBOOL				bDataTried;
	FUC2WaveBank() : bStream(0), bPersist(0), bXbox(0), bDataTried(0) {}
};

struct FUC2Variation
{
	FString		BankName;
	INT			Bank;			// index into the loaded wave banks, resolved on first play (-1 = not yet)
	INT			Entry, Weight;
	FUC2Variation() : Bank(-1), Entry(0), Weight(-1) {}
};

// Set Volume / Set Pitch events. Low-pass, LFO, envelope, mix bin and loop-start events are kept by the
// parser only as counts (see FUC2Sound::IgnoredEvents): the Xbox voice processor features have no
// equivalent here yet.
enum EUC2EventKind { EV_SetVolume, EV_SetPitch };
enum EUC2EventMode { EM_Set, EM_Random, EM_Fade };
struct FUC2Event
{
	BYTE	Kind, Mode;
	UBOOL	bRelative;
	INT		Time, Steps, Duration, Low, High;
};

struct FUC2Track
{
	UBOOL					bHasPlay;
	INT						PlayTime, Delay, Loops, Playlist, PitchLow, PitchHigh, VolLow, VolHigh;
	UBOOL					bNewVariation;
	TArray<FUC2Variation>	Variations;
	TArray<FUC2Event>		Events;
	// playlist state, shared by every play of this sound (as XACT keeps it in the bank)
	INT						LastVariation;
	TArray<INT>				Bag;
	FUC2Track() : bHasPlay(0), PlayTime(0), Delay(0), Loops(0), Playlist(0), PitchLow(0), PitchHigh(0),
		VolLow(0), VolHigh(0), bNewVariation(0), LastVariation(-1) {}
};

struct FUC2Rpc
{
	FString		Name;
	INT			Track;
	TArray<INT>	X, Y;
	FLOAT Evaluate( FLOAT Value01 ) const;
};

struct FUC2Sound
{
	FString				Name;
	INT					Priority;
	INT					Volume;			// mB, the sound's own volume plus its group's (the .xsb bakes both in)
	INT					Pitch;			// cents
	INT					Category;		// UAudioSubsystem::EVolumeCategory, or -1
	UBOOL				bHeadroomBoost;
	INT					I3DL2;			// reverb send, mB
	UBOOL				bIs3D;
	FLOAT				MinDistance, MaxDistance, Rolloff;
	INT					LengthMs;		// -1: loops forever
	INT					Loops;
	INT					IgnoredEvents;
	TArray<FUC2Rpc>		Rpcs;
	TArray<FUC2Track>	Tracks;
	FUC2Sound() : Priority(0), Volume(0), Pitch(0), Category(-1), bHeadroomBoost(0), I3DL2(0), bIs3D(0),
		MinDistance(1.f), MaxDistance(1e9f), Rolloff(1.f), LengthMs(0), Loops(0), IgnoredEvents(0) {}
};

struct FUC2Cue
{
	FString			Name;		// "Bank\Cue" as in the project (A_Music: just the song)
	TArray<INT>		Sounds;
	TArray<INT>		Weights;
};

struct FUC2SoundBank
{
	FString				Name;
	UBOOL				bPersist;
	TArray<FUC2Sound>	Sounds;
	TArray<FUC2Cue>		Cues;
	TMap<FString,INT>	CueIndex;	// cue name (case-insensitive) -> Cues index
	FUC2SoundBank() : bPersist(0) {}
};

// Parsing. Return 0 and fill Error when the file is unusable.
UBOOL UC2LoadSoundBank( const TCHAR* Path, FUC2SoundBank& Bank, FString& Error );
UBOOL UC2LoadWaveBank( const TCHAR* Path, FUC2WaveBank& Bank, FString& Error );

/*-----------------------------------------------------------------------------
	M16: the shipped Xbox banks (UC2XboxBanks.cpp). Formats in tools/uc2xwb.py, tools/uc2xsb.py
	and docs/porting/analysis/xbox-audio-runtime.md; read little-endian byte by byte (no
	structure casts), so the readers are the same on every target.
-----------------------------------------------------------------------------*/

// <Bank>[_<lang>].xwb -> the same wave index a .uc2wb gives (Format, Offset, Size, Frames, LoopStart/Length in
// frames). Only the header and the entry table are read; DataFile = the .xwb's file name.
UBOOL UC2LoadXwbWaveBank( const TCHAR* Path, FUC2WaveBank& Bank, FString& Error );
// <Bank>.xsb -> the same tables a .uc2cues gives. Cue lengths need the waves' lengths: the entry tables of the
// wave banks the .xsb names are read from the same directory (<Name>.xwb, the base language, as the cook did).
UBOOL UC2LoadXsbSoundBank( const TCHAR* Path, FUC2SoundBank& Bank, FString& Error );
// Xbox ADPCM (36 bytes per channel per block, 64 frames per block) to interleaved 16-bit PCM: returns the frames
// written to Out (room for Size/(36*Channels)*64*Channels samples). The same arithmetic as tools/uc2xwb.py.
INT UC2DecodeXboxAdpcm( const BYTE* Data, INT Size, INT Channels, SWORD* Out );
// One wave of either source to interleaved 16-bit PCM: Vorbis from Bank.Data (loaded by the caller), an Xbox
// entry read from <SoundsDir><Bank.DataFile>. Frames of a Vorbis wave are cut to the index's length.
UBOOL UC2DecodeWave( const FUC2WaveBank& Bank, const FUC2Wave& W, const TCHAR* SoundsDir, TArray<SWORD>& Pcm,
	INT& Channels, INT& Rate, FString& Error, INT* Trimmed=NULL );

// Music from a streamed .xwb entry (M_<Song>.xwb), read from the file a block at a time.
struct FUC2XwbStream
{
	FArchive*		Ar;
	INT				Format, Channels, Rate, Start, Size, Pos;	// Pos: bytes of the entry consumed
	INT				Frames, Position;
	TArray<SWORD>	Pending;		// decoded frames not handed out yet
	INT				PendingAt;
	FUC2XwbStream() : Ar(NULL), Format(0), Channels(0), Rate(0), Start(0), Size(0), Pos(0), Frames(0), Position(0),
		PendingAt(0) {}
	~FUC2XwbStream() { Close(); }
	UBOOL Open( const TCHAR* Path, const FUC2Wave& W, FString& Error );
	INT Read( SWORD* Out, INT MaxFrames );
	UBOOL Rewind();
	void Close();
};

// Vorbis from memory. A wave decodes to whole Vorbis blocks when the decoder does not apply the end-of-stream
// granule position (libvorbis 1.0's vorbisfile does not, for about one wave in ten of ours): Frames > 0 cuts
// the result to the source's length from the index (Trimmed = frames removed).
UBOOL UC2DecodeVorbis( const BYTE* Data, INT Size, TArray<SWORD>& Pcm, INT& Channels, INT& Rate, FString& Error,
	INT Frames=0, INT* Trimmed=NULL );

struct FUC2VorbisStream
{
	void*			Impl;
	INT				Channels, Rate;
	INT				Frames, Position;	// Frames > 0: the stream ends there (see UC2DecodeVorbis)
	FUC2VorbisStream() : Impl(NULL), Channels(0), Rate(0), Frames(0), Position(0) {}
	~FUC2VorbisStream() { Close(); }
	UBOOL Open( TArray<BYTE>& InData, FString& Error );	// takes over InData (swapped out)
	INT Read( SWORD* Out, INT MaxFrames );				// frames read, 0 at the end
	UBOOL Rewind();
	void Close();
};

/*-----------------------------------------------------------------------------
	OpenAL device helpers (UC2ALDevice.cpp).
-----------------------------------------------------------------------------*/

UBOOL UC2ALBind( FString& Error );				// load libopenal and resolve the core entry points
void UC2ALUnbind();
UBOOL UC2ALBindEFX();							// after a context is current
ALenum UC2ALFormat( INT Channels );

// OpenAL Soft's own log (alsoft_set_log_callback, when the library has it), captured while the device opens:
// it is the only place that says which backend was chosen and why the others were not.
void UC2ALCaptureLog( UBOOL bOn );
// After the device opened (or failed to): the backend chosen ("" if none was), and one line saying which
// backends were tried in which order and what happened to each. Warnings and errors the library logged
// meanwhile go to Problems (one per line), for the log.
FString UC2ALBackendReport( const TCHAR* Order, FString& Chosen, TArray<FString>& Problems );

// The 26 I3DL2 listener presets of the Xbox's DSound.h, in EZoneAudioEffect order (ZoneInfo.uc).
struct FUC2I3DL2
{
	const TCHAR*	Name;
	INT				Room, RoomHF;
	FLOAT			RoomRolloff, DecayTime, DecayHFRatio;
	INT				Reflections;
	FLOAT			ReflectionsDelay;
	INT				Reverb;
	FLOAT			ReverbDelay, Diffusion, Density, HFReference;
};
enum { UC2_NUM_ZONE_PRESETS = 26 };
extern const FUC2I3DL2 GUC2ZonePresets[UC2_NUM_ZONE_PRESETS];
void UC2ALLoadReverb( ALuint Effect, UBOOL bEAX, const FUC2I3DL2& P );

/*-----------------------------------------------------------------------------
	Offline test hooks (UC2ALAudioSubsystem.cpp), used by UC2AudioSelfTest with a device
	initialised with LoopbackTest: the listener sits at the origin looking down +X (Unreal
	axes), time is simulated, the mix is rendered into memory.
-----------------------------------------------------------------------------*/

struct FUC2TestPlay
{
	INT				Inst;			// cue instance id (0: nothing started)
	FUC2Sound*		Sound;
	FUC2Wave*		Wave;			// the variation of track 0
	FUC2WaveBank*	WaveBank;
	INT				Variation;
	FLOAT			VarVolume, VarPitch;
	UBOOL			b3DVoice;
};
UBOOL UC2TestPlay( UUC2ALAudioSubsystem* Audio, const TCHAR* Cue, FVector Location, UBOOL b2D, FLOAT Volume, FUC2TestPlay& Out );
// Renders until every cue has stopped (+50 ms), or for MaxSeconds when bWhileSounding is 0. Stereo float, 48 kHz.
INT UC2TestRender( UUC2ALAudioSubsystem* Audio, FLOAT MaxSeconds, TArray<FLOAT>& Out, UBOOL bWhileSounding=1 );
void UC2TestStopAll( UUC2ALAudioSubsystem* Audio );
UBOOL UC2TestSetHRTF( UUC2ALAudioSubsystem* Audio, UBOOL bOn, FString& Status );
enum { UC2_TEST_RATE = 48000 };

#endif // _INC_UC2ALAUDIOPRIVATE

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
