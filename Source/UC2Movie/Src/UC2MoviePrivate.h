/*=============================================================================
	UC2MoviePrivate.h: the cooked-movie player (Ogg Theora video, Ogg Vorbis sound).

	One FUC2MoviePlayer plays one movie: a Theora stream (Textures/Movies/*.ogv, Movies/*.ogv) and,
	optionally, a Vorbis stream (the same .ogv, or a per-language Movies/*_<lang>.oga). Each stream has its own
	reader on its own file handle and pulls only its own pages, so memory stays bounded (one 16 KB read
	buffer, the pages of one stream, one decoded picture) whatever the interleaving of the file.
	No thread: everything happens in Advance(), called by the render device or the full-screen loop.
	Design and evidence: docs/porting/analysis/video.md.
=============================================================================*/

#ifndef _INC_UC2MOVIEPRIVATE
#define _INC_UC2MOVIEPRIVATE

#include "UC2Movie.h"
#include <ogg/ogg.h>
#include <theora/theoradec.h>
#include <vorbis/codec.h>

/*-----------------------------------------------------------------------------
	FUC2OggReader: the packets of one logical stream of an Ogg file, read through GFileManager.
-----------------------------------------------------------------------------*/

enum EUC2OggCodec { OGGC_None, OGGC_Theora, OGGC_Vorbis };

struct FUC2OggReader
{
	FArchive*			Ar;
	INT					FileSize, FilePos;
	ogg_sync_state		Sync;
	ogg_stream_state	Stream;
	UBOOL				bSync, bStream, bEOF;
	INT					Serial;
	EUC2OggCodec		Codec;
	FString				Path;
	ogg_int64_t			LastGranule;	// of the selected stream, from the file's tail (-1: unknown)

	FUC2OggReader();
	~FUC2OggReader() { Close(); }
	// Opens Path and selects the first logical stream of the codec. 0: no such file or stream.
	UBOOL Open( const TCHAR* InPath, EUC2OggCodec InCodec );
	void Close();
	// The next packet of the stream (valid until the next call). 0 at the end of the stream.
	UBOOL NextPacket( ogg_packet& Op );
	// Back to the first packet (the identification header). 0 if the file cannot be read again.
	UBOOL Rewind();
private:
	UBOOL NextPage( ogg_page& Page );
	void ScanTail();
};

/*-----------------------------------------------------------------------------
	FUC2MoviePlayer.
-----------------------------------------------------------------------------*/

class FUC2MoviePlayer : public FPortableMoviePlayer
{
public:
	FUC2MoviePlayer();
	~FUC2MoviePlayer();

	// FPortableMoviePlayer.
	UBOOL Open( const TCHAR* VideoFile, const TCHAR* AudioFile, DWORD Flags, UAudioSubsystem* Audio );
	UBOOL Advance();
	UBOOL IsPlaying() { return bOpen && !bEnded; }
	UBOOL HasEnded() { return bEnded; }
	void  Pause( UBOOL bPause );
	void  Rewind();
	INT   GetWidth() { return Width; }
	INT   GetHeight() { return Height; }
	INT   GetFrame() { return ShownFrame; }
	INT   GetFrameCount() { return FrameCount; }
	FLOAT GetFrameRate() { return Fps; }
	DOUBLE GetTime() { return Time; }
	INT   GetRevision() { return Revision; }
	const BYTE* GetBGRA() { return BGRA.Num() ? &BGRA(0) : NULL; }
	const TCHAR* GetDescription() { return *Description; }

	// Test hooks (UC2MovieSelfTest): a clock other than appSeconds(), a sound sink other than the audio
	// device, and the decoded Y'CbCr of the picture shown.
	DOUBLE (*TestClock)();
	FMovieAudioStream* TestAudio;
	UBOOL GetYCbCr( th_ycbcr_buffer& Out );
	INT   GetAudioRate() { return Rate; }
	INT   GetAudioChannels() { return Channels; }
	INT   GetAudioFrames() { return TotalAudioFrames; }		// of the stream, by its last granule
	INT   GetPassFrames() { return PassFrames; }			// sound frames per pass (padded to the video)
	INT   GetDecodedAudioFrames() { return AudioDecodedTotal; }
	INT   GetPasses() { return Pass; }
	// Every BGRA conversion: the player's own (fixed point); the self-test compares it with the float formula.
	static void ConvertYCbCr( const th_ycbcr_buffer& Y, INT PicX, INT PicY, INT W, INT H, BYTE* Out );

private:
	// state
	UBOOL				bOpen, bEnded, bPaused;
	DWORD				Flags;
	FString				Description;
	// video
	FUC2OggReader		VideoReader;
	th_info				Info;
	th_comment			Comment;
	th_setup_info*		Setup;
	th_dec_ctx*			Ctx;
	ogg_packet			FirstData;			// the first data packet, read while parsing the headers
	TArray<BYTE>		FirstDataBytes;
	UBOOL				bHaveFirstData;
	INT					Width, Height, FrameCount;
	FLOAT				Fps;
	INT					DecodedFrame;		// last frame fed to the decoder this pass (-1: none)
	UBOOL				bPictureDirty;		// a decoded frame changed the picture since the last conversion
	INT					ShownFrame, Revision;
	TArray<BYTE>		BGRA;
	// sound
	UBOOL				bAudio;
	FUC2OggReader		AudioReader;
	vorbis_info			VI;
	vorbis_comment		VC;
	vorbis_dsp_state	VD;
	vorbis_block		VB;
	UBOOL				bVorbisInit, bVorbisDsp;
	INT					Rate, Channels;
	INT					TotalAudioFrames;	// of the stream (last granule): decoding stops there (end trim)
	INT					PassFrames;			// sound frames per pass: the stream padded with silence to the video
	INT					AudioDecoded;		// frames delivered in the current sound pass (stream + padding)
	INT					AudioDecodedTotal;	// frames delivered since open
	UBOOL				bAudioDone;			// every frame of every pass written (non-looping)
	FMovieAudioStream*	Stream;
	UAudioSubsystem*	AudioDevice;
	TArray<SWORD>		Pcm;
	// clock
	DOUBLE				Time;				// movie time within the current pass
	INT					Pass;				// passes completed (loops)
	DOUBLE				RealStart, PauseStart, PausedTotal;
	DOUBLE				StreamPassStart;	// stream frame at which the current pass starts (with sound)

	DOUBLE Now() { return TestClock ? TestClock() : appSeconds(); }
	UBOOL OpenVideo( const TCHAR* File );
	UBOOL RestartVideo();
	UBOOL DecodeTo( INT Frame );
	void  Convert();
	UBOOL OpenAudio( const TCHAR* File );
	UBOOL RestartAudio();
	void  CloseAudioDecoder();
	INT   ReadAudio( SWORD* Out, INT MaxFrames );
	void  FeedAudio();
	DOUBLE PassSeconds() { return Fps > 0.f ? FrameCount / (DOUBLE)Fps : 0.0; }
};

#endif // _INC_UC2MOVIEPRIVATE

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
