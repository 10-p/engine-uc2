/*=============================================================================
	HTTPDownload.h: Unreal HTTP File Download
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Jack Porter
=============================================================================*/
#ifndef UHTTPDownload_H
#define UHTTPDownload_H

enum EHTTPState
{
	HTTP_Initialized		=0,
	HTTP_Resolving			=1,
	HTTP_Resolved			=2,
	HTTP_Connecting			=3,
	HTTP_ReceivingHeader	=4,
	HTTP_ReceivingData		=5,
	HTTP_Closed				=6
};

class UHTTPDownload : public UDownload
{
	// Package IpDrv, not Engine. Epic's DLL build registered the class under the DLL's package (IMPLEMENT_CLASS
	// uses GPackage there), so it is IpDrv.HTTPDownload, the name Default.ini's DownloadManagers and
	// [IpDrv.HTTPDownload] use; the static link takes this argument.
	DECLARE_CLASS(UHTTPDownload,UDownload,CLASS_Transient|CLASS_Config,IpDrv);
    	
	// Config.
	FStringNoInit	ProxyServerHost;
	INT				ProxyServerPort;

	// Variables.
	BYTE			HTTPState;
	SOCKADDR_IN		LocalAddr;
	SOCKADDR_IN		ServerAddr;
	INT				ServerSocket;
	FResolveInfo*	ResolveInfo;
	FURL			DownloadURL;
	TArray<BYTE>	ReceivedData;
	TArray<FString>	Headers;
	DOUBLE			ConnectStartTime;

	// Constructors.
	void StaticConstructor();
	UHTTPDownload();

	// UObject interface.
	void Destroy();
	void Serialize( FArchive& Ar );

	// UDownload Interface.
	void ReceiveFile( UNetConnection* InConnection, INT PackageIndex, const TCHAR *Params=NULL, UBOOL InCompression=0 );
	UBOOL TrySkipFile();
	void Tick(void);

	// UHTTPDownload Interface.
	UBOOL FetchData();
};

#endif
/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

