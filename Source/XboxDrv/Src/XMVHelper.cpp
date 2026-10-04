//-----------------------------------------------------------------------------
// File: WMVPlayer.cpp
//
// Desc: This helper class provides simple WMV decoding and playback 
//       functionality.  It will be expanded as new playback methods are 
//       exposed
//
// Hist: 2.7.03 - Created, based on work by Jeff Sullivan
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//-----------------------------------------------------------------------------

#include <xtl.h>
#include "XMVHelper.h"

#define SAFE_DELETE(p)       { delete (p);     (p)=NULL; }
#define SAFE_DELETE_ARRAY(p) { delete[] (p);   (p)=NULL; }
#define SAFE_RELEASE(p)      { if(p) { (p)->Release(); (p)=NULL; } }


// Funtion Prototypes for packet loading functions for loading from a file.
HRESULT CALLBACK GetNextPacket( DWORD dwContext,
                                void **ppPacket, 
                                DWORD* pOffsetToNextPacket );

HRESULT CALLBACK ReleasePreviousPacket( DWORD dwContext, 
                                        LONGLONG llNextReadByteOffset, 
                                        DWORD dwNextPacketSize );

// Funtion Prototypes for packet loading functions for loading from a block of memory.
HRESULT CALLBACK GetNextMemoryPacket( DWORD dwContext,
                                      void **ppPacket, 
                                      DWORD* pOffsetToNextPacket );

HRESULT CALLBACK ReleasePreviousMemoryPacket( DWORD dwContext, 
									          LONGLONG llNextReadByteOffset, 
                                              DWORD dwNextPacketSize );


//jg -- Dirty disc routine
void ShowDirtyDiscError(void);


//-----------------------------------------------------------------------------
// Name: CXMVPlayer()
// Desc: Constructor for CXMVPlayer
//-----------------------------------------------------------------------------
CXMVPlayer::CXMVPlayer()
{
	guard(CXMVPlayer::CXMVPlayer);
    m_pXMVDecoder = NULL;
    ZeroMemory( &m_VideoDesc, sizeof( m_VideoDesc ) );
    ZeroMemory( &m_AudioDesc, sizeof( m_AudioDesc ) );
    for ( UINT i=0; i<XMVPLAYER_NUMTEXTURES; i++ )
    {
        m_pTextures[i]  = NULL;
    }

    m_dwCurrentFrame = -1;      // Will be zero after we decode the first frame.

    m_bPlaying = FALSE;
    m_bOverlaysEnabled = FALSE;

    m_loadContext.hFile = INVALID_HANDLE_VALUE;
    m_loadContext.pInputBuffer = 0;
    m_physicalBuffer = 0;
	m_bIsInMemory = FALSE;
	unguard;
}




//-----------------------------------------------------------------------------
// Name: ~CXMVPlayer()
// Desc: Destructor for CXMVPlayer
//-----------------------------------------------------------------------------
CXMVPlayer::~CXMVPlayer()
{
	guard(CXMVPlayer::~CXMVPlayer);
    Destroy();
	unguard;
}




//-----------------------------------------------------------------------------
// Name: Destroy()
// Desc: Free all resources and clear are resource pointers and handles.
//-----------------------------------------------------------------------------
HRESULT CXMVPlayer::Destroy()
{
	guard(CXMVPlayer::Destroy);
    // Disable overlays if we were using them.
    if ( m_bOverlaysEnabled )
    {
        m_pDevice->EnableOverlay( FALSE );
        m_bOverlaysEnabled = FALSE;
    }

    // Free the XMV decoder.
    if ( NULL != m_pXMVDecoder )
    {
        m_pXMVDecoder->CloseDecoder();
        m_pXMVDecoder = NULL;
    }

    ZeroMemory( &m_VideoDesc, sizeof( m_VideoDesc ) );
    ZeroMemory( &m_AudioDesc, sizeof( m_AudioDesc ) );

    // Release our textures.
    for ( UINT i=0; i<XMVPLAYER_NUMTEXTURES; i++ )
    {
        if ( m_pTextures[i] )
		{
			//jg -- Check to see if we need to block until idle
			if (m_pTextures[i]->IsBusy() == TRUE)
			{
				m_pTextures[i]->BlockUntilNotBusy();
			}
            m_pTextures[i]->Release();
		}
        m_pTextures[i] = 0;
    }

    m_dwCurrentFrame = -1;

    m_bPlaying = FALSE;

    // Release any file handles we were using.
    if( INVALID_HANDLE_VALUE != m_loadContext.hFile )
    {
        CloseHandle( m_loadContext.hFile );
        m_loadContext.hFile = INVALID_HANDLE_VALUE;
    }

    // Free up memory used for playing a movie from memory.
    if ( m_loadContext.pInputBuffer )
    {
		//jg -- Changed from free() to delete. free() == assert();
        delete [] m_loadContext.pInputBuffer;
        m_loadContext.pInputBuffer = 0;
    }

    // Be sure to release the physical memory last!
    if( m_physicalBuffer )
    {
        XPhysicalFree( m_physicalBuffer );
        m_physicalBuffer = 0;
    }

    return S_OK;
	unguard;
}

/**
 * Figures out which audio stream to use based upon localization settings
 */
DWORD GetLocalizedAudioStream(void)
{
	// Default to english
	DWORD Return = 0;
	// Figure out which region we are in
	switch (XGetLanguage())
	{
		case XC_LANGUAGE_GERMAN:
		{
			Return = 1;
			break;
		}
		case XC_LANGUAGE_FRENCH:
		{
			Return = 2;
			break;
		}
		case XC_LANGUAGE_ITALIAN:
		{
			Return = 3;
			break;
		}
		case XC_LANGUAGE_SPANISH:
		{
			Return = 4;
			break;
		}
	}
	return Return;
}


//-----------------------------------------------------------------------------
// Name: FinishOpeningFile()
// Desc: Helper function for the three Open functions. Enables the audio streams,
// initializes the video descriptor, and allocates textures if needed.
//-----------------------------------------------------------------------------
HRESULT CXMVPlayer::FinishOpeningFile( D3DFORMAT format, LPDIRECT3DDEVICE8 pDevice, BOOL bAllocateTextures, BOOL bEnableAudio   )
{
	guard(CXMVPlayer::FinishOpeningFile);
    checkSlow( format == D3DFMT_YUY2 || format == D3DFMT_LIN_A8R8G8B8 );
    checkSlow( XMVPLAYER_NUMTEXTURES >= 2);

    HRESULT hr = S_OK;
	m_bEnableAudio = bEnableAudio;

    m_pXMVDecoder->GetVideoDescriptor( &m_VideoDesc );

	//jg -- Localize audio streams. Note XMVs with only one stream are ignored for
	// localization. Also if the localization stream is missing, then english is
	// used
	if (bEnableAudio && m_VideoDesc.AudioStreamCount > 0)
	{
		DWORD AudioStream = GetLocalizedAudioStream();
		// Check for an invalid audio stream
		if (AudioStream >= m_VideoDesc.AudioStreamCount)
		{
			debugf(TEXT("XMV doesn't support selected localized audio stream, falling back to English"));
			AudioStream = 0;
		}
		debugf(TEXT("Using localized audio stream (%d)"),AudioStream);
		// Get which stream we are using
		m_pXMVDecoder->GetAudioDescriptor( AudioStream, &m_AudioDesc );
		// Enable the specified stream
	    hr = m_pXMVDecoder->EnableAudioStream( AudioStream, 0, NULL, NULL);
		if ( FAILED( hr ) )
		{
			debugf( NAME_Warning, TEXT("Unable to enable audio stream 0 (error %x)\n"), hr );
	        Destroy();
		    return hr;
		}
		//jg -- Synchronize the movie to the audio stream
		m_pXMVDecoder->SetSynchronizationStream(AudioStream);
	} // if (bEnableAudio)

    for ( int i = 0; i < XMVPLAYER_NUMTEXTURES; i++ )
    {
        m_pTextures[i] = 0;
        if ( bAllocateTextures )
        {
            hr = pDevice->CreateTexture( m_VideoDesc.Width, m_VideoDesc.Height, 1, 0, format, 0, &m_pTextures[i] );
            if ( FAILED( hr ) )
            {
                debugf( NAME_Warning,  TEXT("Unable to create texture %d (error %x)\n"), i, hr );
                Destroy();
                return hr;
            }
        }
    }

    // Initialize what texture we are decoding to, if decoding for texture mapping.
    m_nDecodeTextureIndex = 0;

    // Initialize the various texture pointers for use when decoding for overlays.
    pShowingTexture = m_pTextures[0];
    pDecodingTexture = m_pTextures[1];
    pSubmittedTexture = 0;

    m_bPlaying = TRUE;

    return hr;
	unguard;
}




//-----------------------------------------------------------------------------
// Name: OpenFile()
// Desc: Create an XMV decoder object that reads from a file.
//-----------------------------------------------------------------------------
//scion jg -- Made the file name be pass by ref to avoid the copy
// and added the looping flag
HRESULT CXMVPlayer::OpenFile(const FString& Filename, D3DFORMAT format,
	LPDIRECT3DDEVICE8 pDevice, BOOL bAllocateTextures, BOOL bEnableAudio,
	BOOL bShouldLoop, BOOL bManualSync)
{
	guard(CXMVPlayer::OpenFile);
    HRESULT hr  = S_OK;

	//scion jg -- Flag for determining if this movie should loop or not
	m_bShouldLoop = bShouldLoop;
    if ( NULL == pDevice )
    {
		debugf( NAME_DevJoe, TEXT("Bad parameter to CXMVPlayer::OpenFile()\n" ));
        return E_FAIL;
    }

	//jg -- Make sure we can open movies that are cached on the HD or from Demo
	const FString& FileName = GFileManager->ResolvePath(*Filename);
	debugf(NAME_DevJoe,TEXT("Loading movie file (%s)"),FileName);

	//jg -- Allow for non-synchronized playback
	DWORD dwFlags = bManualSync == 0 ? XMVFLAG_SYNC_ON_NEXT_VBLANK : XMVFLAG_UNSYNCHRONIZED_PLAYBACK;
	//jg -- Make sure we turn it on at the decoder level
	dwFlags |= bShouldLoop ? XMVFLAG_FULL_LOOP : 0;
    hr = XMVDecoder_CreateDecoderForFile( dwFlags, (LPCSTR) TCHAR_TO_ANSI(*FileName), &m_pXMVDecoder );
    if ( FAILED( hr ) )
    {
		debugf( NAME_Warning, TEXT("Unable to create XMV Decoder for %s (error: %x)\n"), FileName, hr );
		//jg -- Universal disc error message
		ShowDirtyDiscError();
        return hr;
    }

    hr = FinishOpeningFile( format, pDevice, bAllocateTextures, bEnableAudio );

    return hr;
	unguard;
}




//-----------------------------------------------------------------------------
// Name: OpenFileForPackets()
// Desc: Create an XMV decoder object that uses the packet reading interface.
// Currently this just reads from a file, but it can be altered to read from
// custom formats, start partway through a file, etc.
//-----------------------------------------------------------------------------
HRESULT CXMVPlayer::OpenFileForPackets( FString lpFilename, D3DFORMAT format, LPDIRECT3DDEVICE8 pDevice, BOOL bAllocateTextures, BOOL bEnableAudio  )
{
	guard(CXMVPlayer::OpenFileForPackets);
    HRESULT hr = S_OK;

    // We need to read in the first 4K of data for the XMV player to initialize
    // itself from. This is most conveniently read as an array of DWORDS.
    DWORD    first4Kbytes[4096 / sizeof( DWORD )];

    // Clear entire context struct to zero
    ZeroMemory( &m_loadContext, sizeof( m_loadContext ) );

    // Open the input file.
    m_loadContext.hFile = CreateFile( (CHAR *)(*lpFilename), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 
                                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED | FILE_FLAG_NO_BUFFERING, 
                                NULL );

    if( m_loadContext.hFile == INVALID_HANDLE_VALUE )
    {
        Destroy();
        return E_INVALIDARG;
    }

    // Read the first page from the file. We opened it for 
    // overlapped IO so we do a pair of reads.
    m_loadContext.Overlapped.Offset     = 0;
    m_loadContext.Overlapped.OffsetHigh = 0;

    // Start the read.
    if( 0 == ReadFile( m_loadContext.hFile, first4Kbytes, sizeof( first4Kbytes ), NULL, &m_loadContext.Overlapped ) )
    {
        if( GetLastError() != ERROR_IO_PENDING )
        {
			//jg -- Universal disc error message
			ShowDirtyDiscError();
            Destroy();
            return E_FAIL;
        }
    }

    // Wait for the read to finish.
    DWORD dwBytesRead;
    if( !GetOverlappedResult( m_loadContext.hFile, &m_loadContext.Overlapped, &dwBytesRead, TRUE ) )
    {
        Destroy();
        return E_FAIL;
    }

    // Check size to make sure input is a valid XMV file.
    if( dwBytesRead != 4096 )
    {
        Destroy();
        return E_FAIL;
    }

    // Create an XMV decoder
    hr = XMVDecoder_CreateDecoderForPackets( XMVFLAG_SYNC_ON_NEXT_VBLANK, first4Kbytes, ( DWORD )&m_loadContext,
                                             GetNextPacket, ReleasePreviousPacket, &m_pXMVDecoder );
    if( FAILED( hr ) )
    {
        Destroy();
        return E_FAIL;
    }

    // The size of the first packet and the minimum size of the two packet buffers are stored in the
    // second and third DWORDS of the file. From xmv.h:
    // *    DWORD NextPacketSize  // The size of the next packet
    // *    DWORD ThisPacketSize  // The size of this packet
    // *    DWORD MaxPacketSize   // The size of the largest packet in the file
    DWORD dwThisPacketSize     = first4Kbytes[1];
    DWORD dwRequiredPacketSize = first4Kbytes[2];

    // Check for illegal parameters.
    if( dwThisPacketSize > dwRequiredPacketSize )
    {
        Destroy();
        return E_FAIL;
    }

    // XPhysicalAlloc is used so that 5.1 or compressed audio streams can be played.
    m_physicalBuffer = ( BYTE* )XPhysicalAlloc( dwRequiredPacketSize * 2, MAXULONG_PTR, 0, PAGE_READWRITE );

    // Save our information.
    m_loadContext.dwPacketSize    = dwRequiredPacketSize;
    m_loadContext.pLoadingPacket  = m_physicalBuffer;
    m_loadContext.pDecodingPacket = m_physicalBuffer + dwRequiredPacketSize;

    // Read the first packet.  We wind up re-reading the first 4096
    // bytes but it makes the logic for figuring out how much we read
    // a little bit easier...
    m_loadContext.Overlapped.Offset     = 0;
    m_loadContext.Overlapped.OffsetHigh = 0;

    if( 0 == ReadFile( m_loadContext.hFile, m_physicalBuffer, dwThisPacketSize, NULL,
                       &m_loadContext.Overlapped ) )
    {
        if( GetLastError() != ERROR_IO_PENDING )
        {
			//jg -- Universal disc error message
			ShowDirtyDiscError();
            Destroy();
            return E_FAIL;
        }
    }

    // Note - at this point the preceding read has *not* necessarily completed.
    // Don't try reading anything from that buffer until GetNextPacket has been
    // successfully called.

    hr = FinishOpeningFile( format, pDevice, bAllocateTextures, bEnableAudio );

    return hr;
	unguard;
}




//-----------------------------------------------------------------------------
// Name: OpenMovieFromMemory()
// Desc: Create an XMV decoder object that uses the packet reading interface to
// read from a block of memory. To simplify the memory management this function
// also allocates this block of memory and initializes it from a file.
//-----------------------------------------------------------------------------
HRESULT CXMVPlayer::OpenMovieFromMemory(D3DFORMAT format, LPDIRECT3DDEVICE8 pDevice,
	BOOL bAllocateTextures, BOOL bEnableAudio, BYTE* Data, DWORD Size,
	BOOL bShouldLoop, BOOL bManualSync)
{
	guard(CXMVPlayer::OpenMovieFromMemory);
    HRESULT hr = S_OK;

	//jg -- Flag for determining if this movie should loop or not
	m_bShouldLoop = bShouldLoop;

    //jg -- Point it to our buffer with our size
	m_bIsInMemory = TRUE;
	m_loadContext.pInputBuffer = Data;
	m_loadContext.inputSize = Size;

    // Check size to make sure input is a valid XMV file.
    if( m_loadContext.inputSize < 4096 )
    {
        Destroy();
        return E_FAIL;
    }

    // Get a DWORD pointer to the first 4K - needed by CreateDecoderForPackets
    DWORD* first4Kbytes = ( DWORD* )Data;

	//jg -- Allow for non-synchronized playback
	DWORD dwFlags = bManualSync == 0 ? XMVFLAG_SYNC_ON_NEXT_VBLANK : XMVFLAG_UNSYNCHRONIZED_PLAYBACK;
    // Create an XMV decoder
    hr = XMVDecoder_CreateDecoderForPackets( dwFlags, first4Kbytes, ( DWORD )&m_loadContext, GetNextMemoryPacket,
                                             ReleasePreviousMemoryPacket, &m_pXMVDecoder );
    if( FAILED( hr ) )
    {
        Destroy();
        return E_FAIL;
    }

    // The size of the first packet and the minimum size of the two packet buffers are stored in the
    // second and third DWORDS of the file. From xmv.h:
    // *    DWORD NextPacketSize  // The size of the next packet
    // *    DWORD ThisPacketSize  // The size of this packet
    // *    DWORD MaxPacketSize   // The size of the largest packet in the file
    DWORD dwThisPacketSize     = first4Kbytes[1];
    DWORD dwRequiredPacketSize = first4Kbytes[2];

    // Check for illegal parameters.
    if( dwThisPacketSize > dwRequiredPacketSize )
    {
        Destroy();
        return E_FAIL;
    }

    // XPhysicalAlloc is used so that 5.1 or compressed audio streams can be played.
    m_physicalBuffer = ( BYTE* )XPhysicalAlloc( dwRequiredPacketSize * 2, MAXULONG_PTR, 0, PAGE_READWRITE );

    // Save our information for the callback functions.
    // The size of our two memory blocks.
    m_loadContext.dwPacketSize    = dwRequiredPacketSize;
    // The addresses of our two memory blocks.
    m_loadContext.pLoadingPacket  = m_physicalBuffer;
    m_loadContext.pDecodingPacket = m_physicalBuffer + dwRequiredPacketSize;

    // Information about the block of memory the movie is stored in.
    m_loadContext.pInputBuffer = ( BYTE* )Data;
    m_loadContext.inputSize = m_loadContext.inputSize;
    m_loadContext.readOffset = 0;
    m_loadContext.currentPacketSize = dwThisPacketSize;

    hr = FinishOpeningFile( format, pDevice, bAllocateTextures, bEnableAudio );

    return hr;
	unguard;
}




//-----------------------------------------------------------------------------
// Name: AdvanceFrameForTexturing()
// Desc: Unpack the appropriate frames of data for use as textures.
//-----------------------------------------------------------------------------
LPDIRECT3DTEXTURE8 CXMVPlayer::AdvanceFrameForTexturing( LPDIRECT3DDEVICE8 pDevice,DWORD& XmvResult )
{
	guard(CXMVPlayer::AdvanceFrameForTexturing);
    // You must pass bAllocateTextures==TRUE to Open if you're going to use GetTexture/AdvanceFrame.
    check( m_pTextures[0] );

    LPDIRECT3DSURFACE8  pSurface;
    pDecodingTexture->GetSurfaceLevel( 0, &pSurface );

    // Decode some information to the current draw texture.
    XMVRESULT xr = XMV_NOFRAME;
    m_pXMVDecoder->GetNextFrame( pSurface, &xr, NULL );

    switch ( xr )
    {
        case XMV_NOFRAME:
            // Do nothing - we didn't get a frame.
            break;

        case XMV_NEWFRAME:
            ++m_dwCurrentFrame;
            // GetNextFrame produced a new frame. So, the texture we were decoding
            // to becomes available for drawing as a texture. 
            pShowingTexture = pDecodingTexture;

            // Setup for decoding to the next texture.
            m_nDecodeTextureIndex = ( m_nDecodeTextureIndex + 1 ) % XMVPLAYER_NUMTEXTURES;
            pDecodingTexture = m_pTextures[ m_nDecodeTextureIndex ];
            break;

        case XMV_ENDOFFILE:
			//scion jg -- Reset back to the beginning if should be a looping movie
			if (m_bShouldLoop == TRUE)
			{
				m_pXMVDecoder->Reset();
				xr = XMV_NEWFRAME;
				// Check for in memory movies and reset their context values
				if (m_bIsInMemory == TRUE)
				{
					// Reset where to start streaming from
					m_loadContext.readOffset = 0;
				}
			}
			else
			{
				m_bPlaying = FALSE;
			}
            break;

        case XMV_FAIL:
            // Data corruption or file read error. We'll treat that the same as
            // end of file.
            m_bPlaying = FALSE;
            break;
    }

	//jg -- Return the result so we can see if the pa
	XmvResult = xr;

    SAFE_RELEASE( pSurface );

    // If we haven't decoded the first frame then return zero.
    if ( m_dwCurrentFrame < 0 )
        return 0;

    return pShowingTexture;
	unguard;
}




//-----------------------------------------------------------------------------
// Name: AdvanceFrameForOverlays()
// Desc: Unpack the appropriate frames of data for use as an overlay.
//-----------------------------------------------------------------------------
LPDIRECT3DTEXTURE8 CXMVPlayer::AdvanceFrameForOverlays( LPDIRECT3DDEVICE8 pDevice )
{
	guard(CXMVPlayer::AdvanceFrameForOverlays);
    // You must pass bAllocateTextures==TRUE to Open if you're going to use GetTexture/AdvanceFrame.
    check( m_pTextures[0] );

    // You have to call CXMVPlayer::EnableOverlays() if you are going to use overlays.
    check( m_bOverlaysEnabled );

    // If a texture has been submitted to be used as an overlay then we have to
    // wait for GetUpdateOverlayState() to return TRUE before we can assume that
    // the previous texture has *stopped* being displayed. Once GetUpdateOverlayState()
    // returns TRUE then we know that pSubmittedTexture is being displayed, which
    // means that, pShowingTexture is available as a decoding target.
    if ( pSubmittedTexture )
    {
        // If GetOverlayUpdateStatus() returns FALSE then we can still proceed and
        // call GetNextFrame(), but we will pass NULL for the surface parameter.
        // Some work will still be done, but none of the surfaces will be altered.
        if ( pDevice->GetOverlayUpdateStatus() )
        {
            // The call to UpdateOverlay() with pSubmittedTexture must have taken
            // effect now, so pShowingTexture is available as a decoding target.
            check( !pDecodingTexture );
            pDecodingTexture = pShowingTexture;
            pShowingTexture = pSubmittedTexture;
            pSubmittedTexture = NULL;
        }
    }

    LPDIRECT3DSURFACE8  pSurface = NULL;
    if ( pDecodingTexture )
        pDecodingTexture->GetSurfaceLevel( 0, &pSurface );

    // Decode some information to the current draw texture, which may be NULL.
    // pDecodingTexture will be NULL if one texture has been submitted as a new
    // overlay but the other one is still being displayed as an overlay.
    // If pSurface is NULL GetNextFrame() will still do some work.
    XMVRESULT xr = XMV_NOFRAME;
    m_pXMVDecoder->GetNextFrame( pSurface, &xr, NULL );

    switch ( xr )
    {
        case XMV_NOFRAME:
            // Do nothing - we didn't get a frame.
            break;

        case XMV_NEWFRAME:
            ++m_dwCurrentFrame;
            // GetNextFrame produced a new frame. So, the texture we were decoding
            // to becomes available for displaying as an overlay. 

            // The other texture is not ready to be a decoding target. It is still
            // being displayed as an overlay. So, we assign the newly decoded
            // texture to pSubmittedTexture for the program to submit as an overlay,
            // but we don't yet move the previously submitted texture from pShowing
            // to pDecoding. That happens on a subsequent call to this function, after
            // GetOverlayUpdateStatus() returns TRUE to tell us that there are no
            // overlay swaps pending.
            check( pDecodingTexture );
            check( !pSubmittedTexture );
            pSubmittedTexture = pDecodingTexture;
            pDecodingTexture = NULL;
            break;

        case XMV_ENDOFFILE:
            m_bPlaying = FALSE;
            break;

        case XMV_FAIL:
            // Data corruption or file read error. We'll treat that the same as
            // end of file.
            m_bPlaying = FALSE;
            break;
    }

    SAFE_RELEASE( pSurface );

    // If we just unpacked a new frame then we return that texture
    // and the program must call UpdateOverlay() with the surface
    // from that texture.
    // If we didn't unpack a frame then the program should do nothing -
    // the previous overlay will continue to be displayed.
    if ( XMV_NEWFRAME == xr )
        return pSubmittedTexture;

    // No new frame to display.
    return 0;
	unguard;
}




//-----------------------------------------------------------------------------
// Name: TerminatePlayback()
// Desc: Calls XMVDecoder::TerminatePlayback()
//-----------------------------------------------------------------------------
void CXMVPlayer::TerminatePlayback()
{
	guard(CXMVPlayer::TerminatePlayback);
    m_pXMVDecoder->TerminatePlayback();
	unguard;
}




//-----------------------------------------------------------------------------
// Name: Play()
// Desc: Calls XMVDecoder::Play() to play the entire movie.
//-----------------------------------------------------------------------------
HRESULT CXMVPlayer::Play( DWORD Flags, RECT* pRect )
{
	guard(CXMVPlayer::Play);
    // You have to call Open before calling Play.
    check( m_pXMVDecoder );

    // Don't pass bAllocateTextures==TRUE to Open if you're going to use Play.
    check( !m_pTextures[0] );

    return m_pXMVDecoder->Play( Flags, pRect );
	unguard;
}




//-----------------------------------------------------------------------------
// Name: EnableOverlays()
// Desc: Enable the overlay planes for playing the movie in them, and record
// that the overlays should be disabled when Destroy() is called.
//-----------------------------------------------------------------------------
void CXMVPlayer::EnableOverlays( LPDIRECT3DDEVICE8 pDevice )
{
	guard(CXMVPlayer::EnableOverlays);
    m_pDevice = pDevice;
    pDevice->EnableOverlay( TRUE );
    m_bOverlaysEnabled = TRUE;
	unguard;
}




//-----------------------------------------------------------------------------
// Name: GetNextPacket()
// Desc: Callback function to get next packet from a file
//-----------------------------------------------------------------------------
/*static*/ HRESULT CALLBACK GetNextPacket( DWORD dwContext, VOID** ppPacket, 
                                DWORD* pOffsetToNextPacket )
{
	guard(GetNextPacket);
    LOAD_CONTEXT* pContext = ( LOAD_CONTEXT* )dwContext;
    if( NULL == pContext )
        return E_FAIL;

    // If the next packet is fully loaded then return it, 
    // otherwise return NULL.
    DWORD dwBytesRead;    
    if( GetOverlappedResult( pContext->hFile, &pContext->Overlapped, 
                             &dwBytesRead, FALSE ) )
    {
        // Make the old decoding packet pending.
        pContext->pPendingReleasePacket = pContext->pDecodingPacket;
        pContext->pDecodingPacket       = pContext->pLoadingPacket;
        pContext->pLoadingPacket        = NULL;

        // Offset to the next packet.
        *pOffsetToNextPacket = dwBytesRead;

        // Set *ppPacket to the data we just loaded.
        *ppPacket            = pContext->pDecodingPacket;
    }
    else
    {
        DWORD dwError = GetLastError();

        // If we're waiting on the IO to finish, just do nothing.
        if( dwError != ERROR_IO_INCOMPLETE )
            return HRESULT_FROM_WIN32( dwError );

        *ppPacket            = NULL;
        *pOffsetToNextPacket = 0;
    }

    return S_OK;
	unguard;
}




//-----------------------------------------------------------------------------
// Name: ReleasePreviousPacket()
// Desc: Callback function to release previous packet from a file
//-----------------------------------------------------------------------------
/*static*/ HRESULT CALLBACK ReleasePreviousPacket( DWORD dwContext, LONGLONG llNextReadByteOffset, 
                                        DWORD dwNextPacketSize )
{
	guard(ReleasePreviousPacket);
    LOAD_CONTEXT* pContext = ( LOAD_CONTEXT* )dwContext;
    if( NULL == pContext )
        return E_FAIL;

    if( dwNextPacketSize != 0 )
    {
        // Start the next load.
        pContext->Overlapped.Offset     = ( DWORD )( llNextReadByteOffset & 0xFFFFFFFF );
        pContext->Overlapped.OffsetHigh = ( DWORD )( llNextReadByteOffset >> 32 );

        // Check for bad input file - buffer overrun
        if( dwNextPacketSize > pContext->dwPacketSize )
            return E_FAIL;

        pContext->pLoadingPacket        = pContext->pPendingReleasePacket;
        pContext->pPendingReleasePacket = NULL;

        if( 0 == ReadFile( pContext->hFile, pContext->pLoadingPacket, 
                       dwNextPacketSize, NULL, &pContext->Overlapped ) )
        {
            if( GetLastError() != ERROR_IO_PENDING )
			{
				//jg -- Universal disc error message
				ShowDirtyDiscError();
                return HRESULT_FROM_WIN32( GetLastError() );
        }
    }
    }

    return S_OK;
	unguard;
}




//-----------------------------------------------------------------------------
// Name: GetNextMemoryPacket()
// Desc: Callback function to get next packet from a file,
// and setup for the next packet.
//-----------------------------------------------------------------------------
/*static*/ HRESULT CALLBACK GetNextMemoryPacket( DWORD dwContext, VOID** ppPacket, 
                                DWORD* pOffsetToNextPacket )
{
	guard(GetNextMemoryPacket);
    LOAD_CONTEXT* pContext = ( LOAD_CONTEXT* )dwContext;
    if( NULL == pContext )
        return E_FAIL;

    DWORD dwBytesRead = pContext->inputSize - pContext->readOffset;
    if ( pContext->currentPacketSize < dwBytesRead )
        dwBytesRead = pContext->currentPacketSize;

    memcpy( pContext->pLoadingPacket, pContext->pInputBuffer + pContext->readOffset , dwBytesRead );
    pContext->readOffset +=dwBytesRead;

    // Swap pointers so that next time we load it goes into the other packet block.
    BYTE* temp = pContext->pLoadingPacket;
    pContext->pLoadingPacket = pContext->pDecodingPacket;
    pContext->pDecodingPacket = temp;

    // Offset to the next packet.
    *pOffsetToNextPacket = dwBytesRead;

    // Set *ppPacket to the data we just loaded.
    *ppPacket = pContext->pDecodingPacket;

    return S_OK;
	unguard;
}




//-----------------------------------------------------------------------------
// Name: ReleasePreviousMemoryPacket()
// Desc: Callback function to release previous packet from a block of memory,
// and setup for the next packet.
//-----------------------------------------------------------------------------
/*static*/ HRESULT CALLBACK ReleasePreviousMemoryPacket( DWORD dwContext, LONGLONG llNextReadByteOffset, 
                                        DWORD dwNextPacketSize )
{
	guard(ReleasePreviousMemoryPacket);
    LOAD_CONTEXT* pContext = ( LOAD_CONTEXT* )dwContext;
    if( NULL == pContext )
        return E_FAIL;

    // Check for bad input file - buffer overrun
    if( dwNextPacketSize > pContext->dwPacketSize )
        return E_FAIL;

    // Record the size of the next packet we are supposed to read, for GetNextMemoryPacket.
    pContext->currentPacketSize = dwNextPacketSize;

    return S_OK;
	unguard;
}
