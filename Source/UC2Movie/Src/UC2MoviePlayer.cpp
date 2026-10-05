/*=============================================================================
	UC2MoviePlayer.cpp: FUC2MoviePlayer, the cooked-movie player (Ogg Theora + Vorbis).

	The behaviour is the Xbox's CXMVPlayer (XboxDrv/Src/XMVHelper.cpp) as the engine used it:
	  - the picture follows the sound's clock (XMV SetSynchronizationStream on the chosen track, :232), or real
	    time without sound; MOVIE_ManualSync = XMVFLAG_UNSYNCHRONIZED_PLAYBACK, one frame per Advance;
	  - looping (XMVFLAG_FULL_LOOP): frame 0 follows the last frame (AdvanceFrameForTexturing :556-566);
	  - nothing to show until the first frame is decoded (:586), the last picture stays after the end.
	Frame n of a movie is on screen while the clock is in [n/fps, (n+1)/fps). The sound is padded with silence
	to the video's length (the cooked sound ends one frame early: the XMVs' tracks end at the last picture's
	start), so with sound the clock reaches the end of the last picture when the sound has played out, and a
	loop is exactly FrameCount/fps long for picture and sound alike.
=============================================================================*/

#include "UC2MoviePrivate.h"

/*-----------------------------------------------------------------------------
	FUC2OggReader.
-----------------------------------------------------------------------------*/

enum { OGG_READ_CHUNK = 16384 };

FUC2OggReader::FUC2OggReader()
:	Ar(NULL), FileSize(0), FilePos(0), bSync(0), bStream(0), bEOF(0), Serial(-1), Codec(OGGC_None), LastGranule(-1)
{}

void FUC2OggReader::Close()
{
	if( bStream )
		ogg_stream_clear( &Stream );
	if( bSync )
		ogg_sync_clear( &Sync );
	bStream = bSync = 0;
	if( Ar )
		delete Ar;
	Ar = NULL;
	Serial = -1;
}

static EUC2OggCodec PacketCodec( const unsigned char* Body, long Bytes )
{
	if( Bytes >= 7 && Body[0] == 0x80 && appMemcmp( Body+1, "theora", 6 ) == 0 )
		return OGGC_Theora;
	if( Bytes >= 7 && Body[0] == 0x01 && appMemcmp( Body+1, "vorbis", 6 ) == 0 )
		return OGGC_Vorbis;
	return OGGC_None;
}

UBOOL FUC2OggReader::NextPage( ogg_page& Page )
{
	while( ogg_sync_pageout( &Sync, &Page ) != 1 )
	{
		INT N = Min<INT>( OGG_READ_CHUNK, FileSize - FilePos );
		if( N <= 0 )
		{
			bEOF = 1;
			return 0;
		}
		char* Buffer = ogg_sync_buffer( &Sync, N );
		Ar->Serialize( Buffer, N );
		ogg_sync_wrote( &Sync, N );
		FilePos += N;
	}
	return 1;
}

UBOOL FUC2OggReader::Open( const TCHAR* InPath, EUC2OggCodec InCodec )
{
	guard(FUC2OggReader::Open);
	Close();
	Path = InPath;
	Codec = InCodec;
	Ar = GFileManager->CreateFileReader( InPath );
	if( !Ar )
		return 0;
	FileSize = Ar->TotalSize();
	FilePos = 0;
	bEOF = 0;
	ogg_sync_init( &Sync );
	bSync = 1;
	// The beginning-of-stream pages of every logical stream come first: pick the first of our codec.
	ogg_page Page;
	while( NextPage( Page ) )
	{
		if( !ogg_page_bos( &Page ) )
			break;
		if( Serial == -1 && PacketCodec( Page.body, Page.body_len ) == Codec )
		{
			Serial = ogg_page_serialno( &Page );
			ogg_stream_init( &Stream, Serial );
			bStream = 1;
			ogg_stream_pagein( &Stream, &Page );
		}
	}
	if( Serial == -1 )
	{
		Close();
		return 0;
	}
	// That first non-BOS page may already be ours.
	if( !bEOF && ogg_page_serialno( &Page ) == Serial )
		ogg_stream_pagein( &Stream, &Page );
	ScanTail();
	return 1;
	unguard;
}

// The granule position of the stream's last page (Theora: the last frame; Vorbis: the sample count), read
// from the end of the file, then back to where reading was.
void FUC2OggReader::ScanTail()
{
	LastGranule = -1;
	INT Pos = Ar->Tell();
	for( INT Window = 65536; Window <= 1048576 && LastGranule < 0; Window *= 4 )
	{
		INT Start = Max( 0, FileSize - Window );
		INT Len = FileSize - Start;
		TArray<BYTE> Tail;
		Tail.Add( Len );
		Ar->Seek( Start );
		Ar->Serialize( &Tail(0), Len );
		for( INT i=0; i+27 <= Len; i++ )
		{
			if( Tail(i) != 'O' || Tail(i+1) != 'g' || Tail(i+2) != 'g' || Tail(i+3) != 'S' || Tail(i+4) != 0 )
				continue;
			INT Segments = Tail(i+26);
			if( i + 27 + Segments > Len )
				continue;
			INT BodyLen = 0;
			for( INT s=0; s<Segments; s++ )
				BodyLen += Tail(i+27+s);
			if( i + 27 + Segments + BodyLen > Len )
				continue;
			ogg_int64_t Granule = 0;
			for( INT b=7; b>=0; b-- )
				Granule = (Granule << 8) | Tail(i+6+b);
			INT PageSerial = Tail(i+14) | (Tail(i+15)<<8) | (Tail(i+16)<<16) | (Tail(i+17)<<24);
			if( PageSerial == Serial && Granule >= 0 )
				LastGranule = Granule;
			i += 26 + Segments + BodyLen;
		}
		if( Start == 0 )
			break;
	}
	Ar->Seek( Pos );
}

UBOOL FUC2OggReader::NextPacket( ogg_packet& Op )
{
	if( !bStream )
		return 0;
	for( ;; )
	{
		INT R = ogg_stream_packetout( &Stream, &Op );
		if( R == 1 )
			return 1;
		if( R < 0 )
			continue;	// a hole in the data: take what follows
		ogg_page Page;
		do
		{
			if( !NextPage( Page ) )
				return 0;
		}
		while( ogg_page_serialno( &Page ) != Serial );
		ogg_stream_pagein( &Stream, &Page );
	}
}

UBOOL FUC2OggReader::Rewind()
{
	if( !Ar || !bStream )
		return 0;
	Ar->Seek( 0 );
	FilePos = 0;
	bEOF = 0;
	ogg_sync_reset( &Sync );
	ogg_stream_reset( &Stream );
	return 1;
}

/*-----------------------------------------------------------------------------
	FUC2MoviePlayer: setup.
-----------------------------------------------------------------------------*/

FUC2MoviePlayer::FUC2MoviePlayer()
:	TestClock(NULL), TestAudio(NULL), bOpen(0), bEnded(0), bPaused(0), Flags(0), Setup(NULL), Ctx(NULL),
	bHaveFirstData(0), Width(0), Height(0), FrameCount(0), Fps(0.f), DecodedFrame(-1), bPictureDirty(0),
	ShownFrame(-1), Revision(0), bAudio(0), bVorbisInit(0), bVorbisDsp(0), Rate(0), Channels(0),
	TotalAudioFrames(0), PassFrames(0), AudioDecoded(0), AudioDecodedTotal(0), bAudioDone(0), Stream(NULL),
	AudioDevice(NULL), Time(0.0), Pass(0), RealStart(0.0), PauseStart(0.0), PausedTotal(0.0), StreamPassStart(0.0)
{
	appMemzero( &FirstData, sizeof(FirstData) );
	th_info_init( &Info );
	th_comment_init( &Comment );
}

FUC2MoviePlayer::~FUC2MoviePlayer()
{
	guard(FUC2MoviePlayer::~FUC2MoviePlayer);
	if( Stream && Stream != TestAudio )
		delete Stream;
	Stream = NULL;
	CloseAudioDecoder();
	if( bVorbisInit )
	{
		vorbis_comment_clear( &VC );
		vorbis_info_clear( &VI );
	}
	if( Ctx )
		th_decode_free( Ctx );
	if( Setup )
		th_setup_free( Setup );
	th_comment_clear( &Comment );
	th_info_clear( &Info );
	unguard;
}

UBOOL FUC2MoviePlayer::OpenVideo( const TCHAR* File )
{
	guard(FUC2MoviePlayer::OpenVideo);
	if( !VideoReader.Open( File, OGGC_Theora ) )
	{
		debugf( NAME_Log, TEXT("UC2Movie: %s has no Theora stream"), File );
		return 0;
	}
	ogg_packet Op;
	while( VideoReader.NextPacket( Op ) )
	{
		INT R = th_decode_headerin( &Info, &Comment, &Setup, &Op );
		if( R < 0 )
		{
			debugf( NAME_Log, TEXT("UC2Movie: %s: bad Theora header (%i)"), File, R );
			return 0;
		}
		if( R == 0 )
		{
			// The first data packet: keep it for the first DecodeTo.
			FirstData = Op;
			FirstDataBytes.Empty();
			if( Op.bytes > 0 )
			{
				FirstDataBytes.Add( Op.bytes );
				appMemcpy( &FirstDataBytes(0), Op.packet, Op.bytes );
			}
			bHaveFirstData = 1;
			break;
		}
	}
	if( !Setup || !bHaveFirstData )
		return 0;
	Ctx = th_decode_alloc( &Info, Setup );
	if( !Ctx )
		return 0;
	Width		= Info.pic_width;
	Height		= Info.pic_height;
	Fps			= Info.fps_denominator ? (FLOAT)Info.fps_numerator / Info.fps_denominator : 30.f;
	FrameCount	= VideoReader.LastGranule >= 0 ? (INT)th_granule_frame( Ctx, VideoReader.LastGranule ) + 1 : 0;
	if( FrameCount <= 0 )
	{
		debugf( NAME_Log, TEXT("UC2Movie: %s: no frame count (no granule position at the end)"), File );
		return 0;
	}
	BGRA.Empty();
	BGRA.AddZeroed( Width * Height * 4 );
	DecodedFrame = -1;
	return 1;
	unguard;
}

UBOOL FUC2MoviePlayer::RestartVideo()
{
	if( Ctx )
		th_decode_free( Ctx );
	Ctx = NULL;
	if( !VideoReader.Rewind() )
		return 0;
	ogg_packet Op;
	bHaveFirstData = 0;
	while( VideoReader.NextPacket( Op ) )
	{
		if( th_packet_isheader( &Op ) )
			continue;
		FirstData = Op;
		FirstDataBytes.Empty();
		if( Op.bytes > 0 )
		{
			FirstDataBytes.Add( Op.bytes );
			appMemcpy( &FirstDataBytes(0), Op.packet, Op.bytes );
		}
		bHaveFirstData = 1;
		break;
	}
	Ctx = th_decode_alloc( &Info, Setup );
	DecodedFrame = -1;
	return Ctx != NULL && bHaveFirstData;
}

UBOOL FUC2MoviePlayer::OpenAudio( const TCHAR* File )
{
	guard(FUC2MoviePlayer::OpenAudio);
	if( !AudioReader.Open( File, OGGC_Vorbis ) )
		return 0;	// a movie without sound: fine
	vorbis_info_init( &VI );
	vorbis_comment_init( &VC );
	bVorbisInit = 1;
	ogg_packet Op;
	for( INT i=0; i<3; i++ )
	{
		if( !AudioReader.NextPacket( Op ) || vorbis_synthesis_headerin( &VI, &VC, &Op ) < 0 )
		{
			debugf( NAME_Log, TEXT("UC2Movie: %s: bad Vorbis header %i"), File, i );
			AudioReader.Close();
			return 0;
		}
	}
	vorbis_synthesis_init( &VD, &VI );
	vorbis_block_init( &VD, &VB );
	bVorbisDsp = 1;
	Rate = VI.rate;
	Channels = VI.channels;
	// The end-of-stream granule position is the stream's length: decoding stops there (the last Vorbis block
	// decodes past it; libvorbisfile trims the same way, ffmpeg's decoder does not).
	TotalAudioFrames = AudioReader.LastGranule >= 0 ? (INT)AudioReader.LastGranule : MAXINT;
	return 1;
	unguard;
}

void FUC2MoviePlayer::CloseAudioDecoder()
{
	if( bVorbisDsp )
	{
		vorbis_block_clear( &VB );
		vorbis_dsp_clear( &VD );
	}
	bVorbisDsp = 0;
}

UBOOL FUC2MoviePlayer::RestartAudio()
{
	CloseAudioDecoder();
	if( !AudioReader.Rewind() )
		return 0;
	ogg_packet Op;
	for( INT i=0; i<3; i++ )
		if( !AudioReader.NextPacket( Op ) )
			return 0;
	vorbis_synthesis_init( &VD, &VI );
	vorbis_block_init( &VD, &VB );
	bVorbisDsp = 1;
	AudioDecoded = 0;
	return 1;
}

UBOOL FUC2MoviePlayer::Open( const TCHAR* VideoFile, const TCHAR* AudioFile, DWORD InFlags, UAudioSubsystem* Audio )
{
	guard(FUC2MoviePlayer::Open);
	Flags = InFlags;
	AudioDevice = Audio;
	if( !OpenVideo( VideoFile ) )
		return 0;
	Description = FString::Printf( TEXT("Theora %ix%i, %i frames at %.3g fps"), Width, Height, FrameCount, Fps );
	if( AudioFile && *AudioFile && (Flags & MOVIE_Audio) && OpenAudio( AudioFile ) )
	{
		// The pass: the video's length in sound frames, or the sound's when it is longer.
		PassFrames = Max<INT>( appRound( FrameCount * (DOUBLE)Rate / Fps ), TotalAudioFrames == MAXINT ? 0 : TotalAudioFrames );
		if( TestAudio )
			Stream = TestAudio;
		else if( AudioDevice )
			Stream = AudioDevice->OpenMovieAudio( Rate, Channels, (Flags & MOVIE_ExclusiveAudio) != 0 );
		if( Stream )
		{
			bAudio = 1;
			Description += FString::Printf( TEXT(", Vorbis %i Hz %i ch from %s (%i frames, padded to %i)"), Rate,
				Channels, AudioFile, TotalAudioFrames, PassFrames );
		}
		else
		{
			Description += TEXT(", sound off (no audio device)");
			CloseAudioDecoder();
			AudioReader.Close();
		}
	}
	else if( Flags & MOVIE_Audio )
		Description += TEXT(", no sound");
	if( Flags & MOVIE_Loop )
		Description += TEXT(", looping");
	if( Flags & MOVIE_ManualSync )
		Description += TEXT(", one frame per update");
	bOpen = 1;
	bEnded = 0;
	RealStart = Now();
	PausedTotal = 0.0;
	return 1;
	unguard;
}

/*-----------------------------------------------------------------------------
	FUC2MoviePlayer: decoding.
-----------------------------------------------------------------------------*/

UBOOL FUC2MoviePlayer::DecodeTo( INT Target )
{
	// Every frame must go through the decoder (Theora frames predict from the previous ones), but only the
	// last one is converted. A bounded number per call: a slow decoder lets the picture lag for a moment
	// rather than stall the game.
	INT Budget = 90;
	while( DecodedFrame < Target && Budget-- > 0 )
	{
		ogg_packet Op;
		if( bHaveFirstData )
		{
			Op = FirstData;
			Op.packet = FirstDataBytes.Num() ? &FirstDataBytes(0) : NULL;
			bHaveFirstData = 0;
		}
		else if( !VideoReader.NextPacket( Op ) )
			return 0;	// the stream has fewer frames than its granule said: hold the last picture
		ogg_int64_t Granule = -1;
		INT R = th_decode_packetin( Ctx, &Op, &Granule );
		if( R == 0 )
			bPictureDirty = 1;	// TH_DUPFRAME (a zero-byte packet): the same picture again
		else if( R != TH_DUPFRAME )
		{
			static UBOOL Logged = 0;
			if( !Logged )
				debugf( NAME_Log, TEXT("UC2Movie: Theora packet %i rejected (%i)"), DecodedFrame+1, R );
			Logged = 1;
		}
		DecodedFrame++;
	}
	return DecodedFrame >= Target;
}

UBOOL FUC2MoviePlayer::GetYCbCr( th_ycbcr_buffer& Out )
{
	return Ctx && th_decode_ycbcr_out( Ctx, Out ) == 0;
}

// BT.601, limited range (Y 16-235, Cb/Cr 16-240), in 8.8 fixed point: the matrix every SD video of the time
// (WMV2 included) is coded with; chroma replicated over its 2x2 block (4:2:0). Output B,G,R,A.
void FUC2MoviePlayer::ConvertYCbCr( const th_ycbcr_buffer& Y, INT PicX, INT PicY, INT W, INT H, BYTE* Out )
{
	INT XDec = Y[0].width > Y[1].width ? 1 : 0;
	INT YDec = Y[0].height > Y[1].height ? 1 : 0;
	for( INT y=0; y<H; y++ )
	{
		const BYTE* PY = Y[0].data + (PicY + y) * Y[0].stride + PicX;
		INT CY = (PicY + y) >> YDec;
		const BYTE* PU = Y[1].data + CY * Y[1].stride;
		const BYTE* PV = Y[2].data + CY * Y[2].stride;
		BYTE* O = Out + y * W * 4;
		for( INT x=0; x<W; x++ )
		{
			INT CX = (PicX + x) >> XDec;
			INT C = 298 * ((INT)PY[x] - 16);
			INT D = (INT)PU[CX] - 128;
			INT E = (INT)PV[CX] - 128;
			INT R = (C + 409*E + 128) >> 8;
			INT G = (C - 100*D - 208*E + 128) >> 8;
			INT B = (C + 516*D + 128) >> 8;
			O[0] = (BYTE)Clamp( B, 0, 255 );
			O[1] = (BYTE)Clamp( G, 0, 255 );
			O[2] = (BYTE)Clamp( R, 0, 255 );
			O[3] = 255;
			O += 4;
		}
	}
}

void FUC2MoviePlayer::Convert()
{
	th_ycbcr_buffer Y;
	if( GetYCbCr( Y ) )
	{
		ConvertYCbCr( Y, Info.pic_x, Info.pic_y, Width, Height, &BGRA(0) );
		Revision++;
	}
	bPictureDirty = 0;
}

// Sound frames for the stream: the decoded Vorbis cut at its end-of-stream granule, then silence up to the
// pass length; looping starts the stream again, seamlessly.
INT FUC2MoviePlayer::ReadAudio( SWORD* Out, INT MaxFrames )
{
	INT Done = 0;
	INT Restarts = 0;
	while( Done < MaxFrames )
	{
		if( AudioDecoded >= PassFrames )
		{
			if( (Flags & MOVIE_Loop) && PassFrames > 0 && Restarts++ < 2 && RestartAudio() )
				continue;
			break;
		}
		INT Left = Min( MaxFrames - Done, PassFrames - AudioDecoded );
		if( AudioDecoded < TotalAudioFrames && bVorbisDsp )
		{
			float** P = NULL;
			INT N = vorbis_synthesis_pcmout( &VD, &P );
			if( N > 0 )
			{
				INT Take = Min( Min( N, Left ), TotalAudioFrames - AudioDecoded );
				for( INT i=0; i<Take; i++ )
					for( INT c=0; c<Channels; c++ )
					{
						INT V = appFloor( P[c][i] * 32768.f + 0.5f );
						Out[(Done+i)*Channels + c] = (SWORD)Clamp( V, -32768, 32767 );
					}
				vorbis_synthesis_read( &VD, Take );
				Done += Take;
				AudioDecoded += Take;
				AudioDecodedTotal += Take;
				continue;
			}
			ogg_packet Op;
			if( !AudioReader.NextPacket( Op ) )
			{
				TotalAudioFrames = AudioDecoded;	// shorter than its granule said: pad from here
				continue;
			}
			if( vorbis_synthesis( &VB, &Op ) == 0 )
				vorbis_synthesis_blockin( &VD, &VB );
			continue;
		}
		// Padding to the end of the pass.
		appMemzero( Out + Done*Channels, Left * Channels * sizeof(SWORD) );
		Done += Left;
		AudioDecoded += Left;
		AudioDecodedTotal += Left;
	}
	return Done;
}

void FUC2MoviePlayer::FeedAudio()
{
	if( !bAudio || !Stream || bAudioDone )
		return;
	INT Free = Stream->FreeFrames();
	while( Free > 0 )
	{
		INT Want = Min( Free, 4096 );
		if( Pcm.Num() < Want * Channels )
		{
			Pcm.Empty( Want * Channels );
			Pcm.Add( Want * Channels );
		}
		INT Got = ReadAudio( &Pcm(0), Want );
		if( Got <= 0 )
		{
			bAudioDone = 1;
			break;
		}
		Stream->Write( &Pcm(0), Got );
		Free -= Got;
	}
}

/*-----------------------------------------------------------------------------
	FUC2MoviePlayer: the clock.
-----------------------------------------------------------------------------*/

UBOOL FUC2MoviePlayer::Advance()
{
	guard(FUC2MoviePlayer::Advance);
	if( !bOpen )
		return 0;
	INT OldRevision = Revision;
	if( Flags & MOVIE_ManualSync )
	{
		// One frame per call, whatever the time (the "manual synch" of MovieMaterial).
		if( !bEnded && !bPaused )
		{
			INT Next = ShownFrame + 1;
			if( Next >= FrameCount )
			{
				if( Flags & MOVIE_Loop )
				{
					RestartVideo();
					Pass++;
					Next = 0;
				}
				else
				{
					bEnded = 1;
					Next = FrameCount - 1;
				}
			}
			DecodeTo( Next );
			ShownFrame = DecodedFrame;
			Time = ShownFrame / (DOUBLE)Fps;
		}
		if( bPictureDirty )
			Convert();
		FeedAudio();
		return Revision != OldRevision;
	}

	FeedAudio();
	if( bEnded )
		return 0;

	// The clock: the sound played so far, or real time without sound.
	DOUBLE PassLen = bAudio ? PassFrames / (DOUBLE)Rate : PassSeconds();
	INT Frame;
	if( bAudio )
	{
		DOUBLE Played = Stream->PlayedFrames() - StreamPassStart;
		while( Played >= PassFrames && PassFrames > 0 )
		{
			if( !(Flags & MOVIE_Loop) )
			{
				bEnded = 1;
				break;
			}
			StreamPassStart += PassFrames;
			Played -= PassFrames;
			Pass++;
			RestartVideo();
		}
		Time = bEnded ? PassLen : Played / Rate;
		// frame = floor(Played * fps / rate), in integers where it matters (1470 sound frames a picture)
		Frame = (INT)appFloor( Played * Fps / Rate + 1e-6 );
	}
	else
	{
		DOUBLE T = (bPaused ? PauseStart : Now()) - RealStart - PausedTotal;
		// Not updated for more than a second (a MovieMaterial nobody looked at): carry on from the picture
		// shown rather than decode the whole gap at once.
		if( ShownFrame >= 0 && T - (ShownFrame + 1) / (DOUBLE)Fps > 1.0 )
		{
			RealStart += T - (ShownFrame + 1) / (DOUBLE)Fps;
			T = (ShownFrame + 1) / (DOUBLE)Fps;
		}
		while( T >= PassLen && PassLen > 0.0 )
		{
			if( !(Flags & MOVIE_Loop) )
			{
				bEnded = 1;
				break;
			}
			RealStart += PassLen;
			T -= PassLen;
			Pass++;
			RestartVideo();
		}
		Time = bEnded ? PassLen : T;
		Frame = (INT)appFloor( T * Fps + 1e-6 );
	}
	Frame = Clamp( Frame, 0, FrameCount - 1 );
	if( Frame < DecodedFrame )
		RestartVideo();
	DecodeTo( Frame );
	ShownFrame = DecodedFrame;
	if( bPictureDirty )
		Convert();
	return Revision != OldRevision;
	unguard;
}

void FUC2MoviePlayer::Pause( UBOOL bPause )
{
	if( !!bPause == !!bPaused )
		return;
	bPaused = bPause;
	if( bPause )
		PauseStart = Now();
	else
		PausedTotal += Now() - PauseStart;
	if( Stream )
		Stream->Pause( bPause );
}

void FUC2MoviePlayer::Rewind()
{
	guard(FUC2MoviePlayer::Rewind);
	if( !bOpen )
		return;
	RestartVideo();
	if( bAudio )
	{
		// A fresh sound stream: whatever is queued belongs to the old position.
		if( Stream && Stream != TestAudio )
		{
			delete Stream;
			Stream = AudioDevice ? AudioDevice->OpenMovieAudio( Rate, Channels, (Flags & MOVIE_ExclusiveAudio) != 0 ) : NULL;
		}
		RestartAudio();
		StreamPassStart = Stream ? Stream->PlayedFrames() : 0.0;
		bAudio = Stream != NULL;
		bAudioDone = 0;
	}
	bEnded = 0;
	Pass = 0;
	RealStart = Now();
	PausedTotal = 0.0;
	if( bPaused )
		PauseStart = RealStart;
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
