#ifndef __AVARIS_PLAYER_ITEM_H__
#define __AVARIS_PLAYER_ITEM_H__

#ifdef _XBOX
#include <xonline.h>
#include <uix.h>
#include "XHVVoiceSubsystem.h"

/* Okay -- Need to implement the ITitilePlayersListItem interface 
for use of the UIX Players feature. */
class AvarisPlayerItem : public ITitlePlayersListItem
{
public:
	AvarisPlayerItem
	(
		const XUID& X,
		const TCHAR* GamerTag,
		DWORD portNumber = 255,
		APlayerReplicationInfo* PRI = NULL,
		DWORD Flags = 0
	)
	: _Xuid(X)
	, _Name(GamerTag)
	, _portNumber(portNumber)
	, _Flags(Flags)
	, _RefCount(1)
	, _commstatus(UIX_VOICE_STATUS_NONE)
	{
		warnf(NAME_DevLive, TEXT("Registering new player (%s) with userID (0x%016I64X - 0x%08X)"), *_Name, _Xuid.qwUserID, _Xuid.dwUserFlags);
		SetPRI(PRI);
	}

	~AvarisPlayerItem() 
	{
		if ( _PRI )
			_PRI->VoiceChatStatus = 255;
	}
	STDMETHODIMP_(void) SetName(const FString & Name)
	{ 
		_Name = Name; 
	}
	STDMETHODIMP_(void) SetXUID(const XUID & X)
	{ 
		_Xuid = X; 
	}
	STDMETHODIMP_(void) SetPort(const DWORD portNumber)
	{ 
		_portNumber = portNumber;
	}
	STDMETHODIMP_(void) SetBitflags(const DWORD Flags)
	{
		_Flags = Flags;
	}
	STDMETHODIMP_(void) SetPRI(APlayerReplicationInfo *PRI)
	{ 
		_PRI = PRI;
		if ( _PRI )
		{
			_PRI->VoiceChatStatus = _commstatus;

			if ( _Name.Len() )
				PRI->eventInitPlayerStatCache();
		}
	}

	void SetVoiceStatus( UIX_VOICE_STATUS_TYPE NewType )
	{
		warnf(NAME_DevLive, TEXT("Setting VoiceStatus for (%s) (0x%016I64X - 0x%08X) to %i"), *_Name, _Xuid.qwUserID, _Xuid.dwUserFlags, NewType);
		_commstatus = NewType;
		if ( _PRI )
			_PRI->VoiceChatStatus = _commstatus;
	}

	// Note that WCHAR * is what is required by the interface
	STDMETHODIMP_(CONST WCHAR*) GetName() CONST
	{ 
		return appToUnicode(*_Name);
	}
	STDMETHODIMP_(CONST XUID*)  GetXUID() CONST
	{ 
		return &_Xuid; 
	}
	STDMETHODIMP_(DWORD)  GetPort() CONST
	{ 
		return _portNumber; 
	}
	STDMETHODIMP_(APlayerReplicationInfo *)  GetPRI() CONST
	{ 
		return _PRI; 
	}

	STDMETHODIMP_(BOOL) IsTalking() CONST
	{
		if (_portNumber == 255)
		{
			// Handle remote players by checking if they are talking to the player who
			// started the current UIX run
			UXHVVoiceSubsystem * XHV = UXHVVoiceSubsystem::getUXHVVoiceSubsystem();
			if ( XHV )
			{
				return XHV->IsTalking(_Xuid);
				/*
				this version is for split-screen voicechat support
				INT Port = XHV->GetLocalTalkerPort();
				if ( Port >= 0 && Port < XONLINE_MAX_LOGON_USERS )
					return XHV->IsTalking(_Xuid, Port);
				*/
			}
		}
		else
		{
			if( XOnlineIsUserGuest( _Xuid.dwUserFlags ) )
				return FALSE;

			XHV_LOCAL_TALKER_STATUS UIX_talker_status;
			appMemzero(&UIX_talker_status, sizeof(UIX_talker_status));
			UXHVVoiceSubsystem::getUXHVVoiceSubsystem()->GetLocalTalkerStatus( _portNumber, &UIX_talker_status);
			return UIX_talker_status.bIsTalking;
		}

		return false;
	}

	STDMETHODIMP_(UIX_VOICE_STATUS_TYPE) GetVoiceStatus() CONST
	{
		// Guests can't use a communicator.
		if( _portNumber < 4 && XOnlineIsUserGuest(_Xuid.dwUserFlags) )
			return UIX_VOICE_STATUS_NONE;
		return _commstatus;
	}

	STDMETHODIMP_(DWORD) GetBitflags() CONST 
	{
		return _Flags;
	}


	/* NOT CURRENTLY USED */
	STDMETHODIMP_(ULONG) AddRef() 
	{
		++_RefCount;
		return _RefCount;
	}
	STDMETHODIMP_(ULONG) Release() 
	{
		ULONG retval = --_RefCount;
		if (!_RefCount)
			delete this;
		return retval;
	}

private:
	XUID					_Xuid;
	FString					_Name;
	DWORD					_Flags;                          // Player flags
	DWORD					_portNumber;	// 0-based port number of player's controller. 255 means remote player
	UIX_VOICE_STATUS_TYPE	_commstatus;
	APlayerReplicationInfo* _PRI;
	ULONG					_RefCount;
};

#endif
#endif // __AVARIS_PLAYER_ITEM_H__