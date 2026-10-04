#pragma once

#include <Xhv.h>
#include "UnTcpNetDriverXbox.h"

class UVoiceSubsystem;

/* epic ===============================================
* UXHVVoiceSubsystem
* Author: rayd
*
* Implementation of XHV api for voice communication over
* networked games.  Each client maintains a list of
* talkers registered, all managed by the server so as
* have all talkers in sync.	 See the XDK documentation
* for an overview of the XHV specific implementation.
*
* Clients detect whether or not they have a mic, and if
* so notify the server, who in turn notifies all clients.
* Unregistration happens in the same fashion.  Muting is
* all handled client-side at the receiving end as to
* simplify the server's role, and masking is the sender's
* responsibility.
*
* All voice data is packed into the VDP packet in
* TcpipConnectionXbox, by filling the VoicePackets
* array.  See UnAudio.h for the packet format, etc.
*
* NOTE: The XHV engine will not initialize itself until
* entering a network game, as it requires XOnline, and
* if not initialized in the correct order, XHV will conflict
* with the Live initialization.
*
* NOTE: This implementation is currently setup assuming
* the server will always be a voice client.  If this is
* not the case, then it might be worth optimizing a bit.
*
* =====================================================
*/
class XBOXAUDIO_API UXHVVoiceSubsystem : public UVoiceSubsystem, public ITitleXHV
{
	DECLARE_CLASS(UXHVVoiceSubsystem,UVoiceSubsystem,0,XboxAudio)

	UViewport *Viewport;

	UXHVVoiceSubsystem();
	void StaticConstructor();
	void Destroy();

	UBOOL Init(UAudioSubsystem *audio);
	UBOOL Shutdown();
	UBOOL Exec(const TCHAR* Cmd, FOutputDevice& Ar=*GLog);
	void Update();
	void NotifyLevelChange();
	void SetViewport(UViewport *inViewport);
	UBOOL Initialized() { return bInitialized; }

	// server-only
	void ReceivedNewPlayer( const XUID& inXUID );

	// ITitleXHV interface
    STDMETHODIMP CommunicatorStatusUpdate(DWORD dwPort, XHV_VOICE_COMMUNICATOR_STATUS status);
	STDMETHODIMP LocalChatDataReady(DWORD dwLocalPort, DWORD dwSize, void *pData);

	void MuteTalker(const XUID& MuteeXUID);
	void UnMuteTalker(const XUID& UnMuteeXUID);

	//bcl -- Brought over some simple helper functions from XHVVoiceManager code in the XDK sample code.
	INT GetLocalTalkerPort() { return LocalTalkerPort;	}
	UBOOL IsTalking(const XUID& xuidRemoteTalker) { return bInitialized && XHVEngine->IsTalking(xuidRemoteTalker); }
	UBOOL IsTalking(const XUID& xuidRemoteTalker, DWORD dwPort );
	HRESULT GetLocalTalkerStatus( DWORD dwPort, XHV_LOCAL_TALKER_STATUS* pLocalTalkerStatus )
	{ return Initialized() ? XHVEngine->GetLocalTalkerStatus( dwPort, pLocalTalkerStatus ) : -1; }


	virtual void HandleVoiceNotification(ENotifyType NotifyType, UTcpipConnectionXbox* SrcConnection, XUID SrcXUID, BYTE Data[MAX_VDATA_SIZE]);

	IXHVEngine* GetXHVEngine() { return XHVEngine; }

	//bcl -- This is a singleton in practice though it may not be implemented exactly as one.
	//       So, this function returns a pointer at the one.
	static UXHVVoiceSubsystem * getUXHVVoiceSubsystem() { return theUXHVVoiceSubsystem; }

	void SetVolume(FLOAT Volume);

protected:
	//bcl -- The pointer at the singleton instance of this class
	static UXHVVoiceSubsystem * theUXHVVoiceSubsystem;

	UBOOL				bInitialized;
	UBOOL				bPendingRequest;			// waiting to register with the server
	UBOOL				bVoiceThroughSpeakers;		// currently set to have all voice through speakers
	IXHVEngine*			XHVEngine;					// XHV engine pointer

	struct FVoiceInfo
	{
		UNetConnection			*Player;			// connection associated with this talker, only valid for servers
		XUID					PlayerID;			// player ID needed for XHV
	};
	TArray<FVoiceInfo>			Voices;				// all remote voices registered
	INT							LocalTalkerPort;	// current port of the local talker

	TArray<ULONGLONG>			ServerMuteList;		// Only valid on server.

	UBOOL IsServer();
	UBOOL IsClient();
	UBOOL IsXboxLiveMatch();

	// client/server safe talker registration functions
	UBOOL RegisterSelf();
	void UnregisterSelf();
	void ServerUpdateCommStatus( BYTE Mode );

	// server only functions
	void BroadcastRegisterTalker(const XUID& inXUID,UNetConnection *srcConn = NULL);
	void BroadcastUnRegisterTalker(const XUID& inXUID);

	// client only functions instigated by server
	UBOOL RegisterTalker(const XUID& inXUID,UNetConnection *inPlayer = NULL);
	void UnRegisterTalker(const XUID& inXUID);

	// client only functions
	void ServerMuteTalker(UTcpipConnectionXbox* MuterConnection, const XUID& MuterXUID, const XUID& MuteeXUID);
	void ServerUnMuteTalker(UTcpipConnectionXbox* UnMuterConnection, const XUID& UnMuterXUID, const XUID& UnMuteeXUID);
	void SetOutputMode( BYTE Mode );
	void UpdateCommStatus(const XUID& inPlayerID, BYTE Mode );
	void ToggleVoiceOutput(const XUID& inXUID, UBOOL bUseSpeakers);

	UBOOL IsMuted(const XUID& MuterXUID, const XUID& MuteeXUID);

	// called from netconnection
	void HandleIncomingVoicePacket(UNetConnection *inPlayer,FVoicePacket &inPkt);

	void SendVoiceNotification(ENotifyType NotifyType, const XUID& SrcXUID, const XUID& DestXUID, const BYTE Data[MAX_VDATA_SIZE]);
	void BroadcastVoiceNotification(ENotifyType NotifyType, const XUID& SrcXUID, const BYTE Data[MAX_VDATA_SIZE]);

	// called from HandleIncomingVoicePacket
	void ReceiveVoicePacket(const XUID& inXUID, void *Data, _WORD DataSize);

	DWORD GetMicrophonePort();
	UBOOL IsActivePort(INT Port);

	APlayerReplicationInfo* MatchPRIToXUID(const XUID& inPlayerID);
	APlayerReplicationInfo* MatchPRIToName(FString& playerName);

	//bcl -- Find the index of a given XUID in the voices list. Returns -1 on failure
	INT PlayerVoiceInfoNdx(const XUID& inXUID);

	UTcpipConnectionXbox* GetConnectionByXUID(const XUID& PlayerXUID);
	XUID GetXUIDByConnection(UTcpipConnectionXbox* Connection);
};
