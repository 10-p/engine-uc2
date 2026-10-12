/*=============================================================================
	UdpLinkWeb.h: where AUdpLink meets the web's synthetic LAN (netplay, web build only).

	A browser has no UDP, so UC2's System Link page (UCMenu.UC2Browser, fed by IpDrv.ClientBeaconReceiver)
	would never hear a beacon. WebRTCDrv answers the receiver's REPORT broadcasts from the servers the
	frontend's broker lists (Source/WebRTCDrv/Src/WebRTCLan.cpp). IpDrv owns this table and calls through it;
	WebRTCDrv fills it in when its classes register (AUTO_INITIALIZE_REGISTRANTS_WEBRTCDRV). So the
	dependency is one-way at run time and none at link time: a program that links IpDrv without WebRTCDrv
	(the web ucc-bin.js) leaves the table NULL and keeps Epic's socket code.

	Natively nothing here is used (the hooks are compiled under __EMSCRIPTEN__ only).
	Notebook: docs/porting/analysis/netplay-browser.md.
=============================================================================*/

#ifndef _INC_UDPLINKWEB
#define _INC_UDPLINKWEB

struct FUdpLinkWebHooks
{
	// A link bound a port (no socket is made). LinkId is the AUdpLink.
	void	(*Bind)( const void* LinkId, INT Port );
	// The link went away: drop what was queued for it.
	void	(*Unbind)( const void* LinkId );
	// A datagram the link sends (text mode); the LAN answers what it understands and swallows the rest.
	void	(*Send)( const void* LinkId, DWORD DestAddr, INT DestPort, const TCHAR* Text );
	// Called from the link's Tick (never from inside script): the next datagram for this link, if any.
	UBOOL	(*NextReply)( const void* LinkId, DWORD* OutAddr, INT* OutPort, FString& OutText );
};

// NULL unless WebRTCDrv is linked in and registered (the browser build).
extern FUdpLinkWebHooks* GUdpLinkWebHooks;

#endif // _INC_UDPLINKWEB

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
