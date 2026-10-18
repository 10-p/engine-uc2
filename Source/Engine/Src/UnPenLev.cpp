/*=============================================================================
	UnPenLev.cpp: Unreal pending level class.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "EnginePrivate.h"
#include "UnNet.h"

#include "ULoadSaveManager.h"

/*-----------------------------------------------------------------------------
	UPendingLevel implementation.
-----------------------------------------------------------------------------*/

//
// Constructor.
//
UPendingLevel::UPendingLevel( UEngine* InEngine, const FURL& InURL )
:	ULevelBase( InEngine, InURL )
{}
IMPLEMENT_CLASS(UPendingLevel);


/*-----------------------------------------------------------------------------
	UNetPendingLevel implementation.
-----------------------------------------------------------------------------*/

// NetDriver->Time of the last control message the pending connection received (or 0 at creation).
static FLOAT GUC2HandshakeProgress = 0.f;

//
// Constructor.
//
UNetPendingLevel::UNetPendingLevel( UEngine* InEngine, const FURL& InURL )
:	UPendingLevel( InEngine, InURL )
{
	guard(UNetPendingLevel::UNetPendingLevel);

	UGameEngine* GameEngine = Cast<UGameEngine>( InEngine );

	// Init.
	Error     = TEXT("");
	NetDriver = NULL;
	bPreloadedLevel = false; //epic moelfke: see TryPreloadingLevel()	

#if 0
	// scion sz - this pending level object should not be removed
	// while the next map is loaded
	SetFlags( RF_TagPersist );
	// scion sz - unload previous level before creating a new pending net level	
	if( GameEngine )
	{	
        GameEngine->UnloadMap();        
	}
#endif

	// check if we're connecting to the same host as the previous level
	// this is only used in TryPreloadingLevel() but it's necessary to check it BEFORE NetDriver->InitConnect() because
	// that function may change the URL (e.g. Xbox encryption)
	if( GameEngine )
	{
		debugf(NAME_DevNet, TEXT("Last host %s, new host %s"), *(((UGameEngine*)InEngine)->LastURL.Host), *(URL.Host));
		bSameHostAsLast = ((UGameEngine*)InEngine)->LastURL.Host == URL.Host;
	}
	else
	{
		bSameHostAsLast = false;
	}

	// Handshake progress clock (Tick); one pending level exists at a time.
	GUC2HandshakeProgress = 0.f;

	// Try to create network driver.
	UClass* NetDriverClass = StaticLoadClass( UNetDriver::StaticClass(), NULL, TEXT("ini:Engine.Engine.NetworkDevice"), NULL, LOAD_NoFail, NULL );
	NetDriver = ConstructObject<UNetDriver>( NetDriverClass );
	if( NetDriver->InitConnect( this, URL, Error ) )
	{
		// Send initial message.
		NetDriver->ServerConnection->Logf( TEXT("HELLO REVISION=0 MINVER=%i VER=%i"), ENGINE_MIN_NET_VERSION, ENGINE_VERSION );
		NetDriver->ServerConnection->FlushNet();
	}
	else
	{
		delete NetDriver;
		NetDriver=NULL;
	}

	unguard;
}

/* epic ===============================================
* ::TryPreloadingLevel
* Author: moelfke
*
* Determines if it would be a good idea to load the level
* before handshaking with the server, and if so performs
* the loading, setting bPreloadedLevel to true on success.
* If the loading is attempted and fails for whatever
* reason, it continues handshaking normally as if the
* problem is a missing package the server might tell us
* how to download it.
*
* =====================================================
*/
void UNetPendingLevel::TryPreloadingLevel(UGameEngine* GameEngine)
{
	guard(UNetPendingLevel::TryPreloadingLevel)

	checkSlow(GameEngine);

	// if we're connecting to a level on the same server as we're currently on, assume that we'll
	// successfully connect and load the level in advance
	if (bSameHostAsLast)
	{
		debugf(TEXT("Attempting preload of new level on same server"));
		FString LoadError;
		GameEngine->LoadMap(URL, this, NULL, LoadError);
		if (LoadError == TEXT(""))
		{
			debugf(TEXT("Preload Succeeded"));
			bPreloadedLevel = true;
			// set the net driver back to us so that we can continue the handshaking process
			// NOTE: After successful handshaking, this must be reset to be connected to the newly loaded level!
			GameEngine->GLevel->NetDriver = NULL;
			NetDriver->Notify = this;
		}
		else
		{
			debugf(TEXT("Preload Failed"));
			// if we have an error, possibly we just don't have all the necessary packages, so do the normal procedure and try again then
		}
	}

	unguard;
}

//
// FNetworkNotify interface.
//
EAcceptConnection UNetPendingLevel::NotifyAcceptingConnection()
{
	guard(UNetPendingLevel::NotifyAcceptingConnection);
	return ACCEPTC_Reject;
	unguard;
}
void UNetPendingLevel::NotifyAcceptedConnection( class UNetConnection* Connection )
{
	guard(UNetPendingLevel::NotifyAcceptedConnection);
	unguard;
}
UBOOL UNetPendingLevel::NotifyAcceptingChannel( class UChannel* Channel )
{
	guard(UNetPendingLevel::NotifyAcceptingChannel);
	return 0;
	unguard;
}
ULevel* UNetPendingLevel::NotifyGetLevel()
{
	guard(UNetPendingLevel::NotifyGetLevel);
	return NULL;
	unguard;
}
void UNetPendingLevel::NotifyReceivedText( UNetConnection* Connection, const TCHAR* Text )
{
	guard(UNetPendingLevel::NotifyReceivedText);
	check(Connection==NetDriver->ServerConnection);
	debugf( NAME_DevNet, TEXT("PendingLevel received: %s  %f/%f"), Text, NetDriver->Time, Connection->LastReceiveTime );
	GUC2HandshakeProgress = NetDriver->Time;	// See Tick

	// This client got a response from the server.
	//scion bcl -- promoted from Championship
	if ( ParseCommand(&Text,TEXT("TRY_LAST_MATCH")) ) // sjs - try reconnecting to travelled server
	{
		//epic moelfke: commented out for testing
		//if( !ParseParam(appCmdLine(),TEXT("XREBOOTLIVE")) )
		//{
			// Modified the functionality from that found in Championship so that the
			// remote address is changed if this happens.
			NetDriver->Exec(TEXT("RETRY_XNET"));
		//}
		//else
		//{
		//	Engine->Client->Exec(*FString::Printf(TEXT("XLIVE TRY_LAST_MATCH")));
		//}
	}
	else if( ParseCommand(&Text,TEXT("UPGRADE")) )
	{
		// Report mismatch.
		INT RemoteMinVer=0, RemoteVer=0;
		Parse( Text, TEXT("MINVER="), RemoteMinVer );
		Parse( Text, TEXT("VER="),    RemoteVer    );
		if( ENGINE_VERSION < RemoteMinVer )
		{
			// Upgrade message.
			Engine->SetProgress( TEXT(""), TEXT(""), -1.f );
		}
		else
		{
			// Downgrade message.
			Engine->SetProgress( LocalizeError(TEXT("ConnectionFailed"),TEXT("Engine")), LocalizeError(TEXT("ServerOutdated"),TEXT("Engine")), 6.f );//!!localize
		}
	}
	else if( ParseCommand(&Text,TEXT("FAILURE")) )
	{
		// Report problem to user.
#ifndef _XBOX
		Engine->SetProgress( TEXT("Rejected By Server"), Text, 10.f );
#endif
		// FAILURE FORCED indicates that the server forfeited the match,
		// so this disconnection shouldn't count against us
		if ( ParseCommand(&Text, TEXT("FORCED")) )
		{
			// clear the disconnect flag
			ULoadSaveManager* Manager = Cast<ULoadSaveManager>(GLoadSaveManager);
			Manager->SetDisconnectFlag(0,0);
		}

		if ( Engine->Client && Engine->Client->Viewports.Num() )
		{
			UViewport* Viewport = Engine->Client->Viewports(0);
			if ( Viewport && Viewport->Actor )
			{
				FString FailText;
				if ( !Parse(Text, TEXT("?failmsg="), FailText) )
					FailText = Text;

				Error = FailText;
			}

			// don't attempt to send any more data to this connection, since it will have
			// already been closed by the server
			Connection->State = USOCK_Closed;
		}
	}
	else if( ParseCommand( &Text, TEXT("USES") ) )
	{
		// Dependency information.
		FPackageInfo& Info = *new(*Connection->PackageMap->List)FPackageInfo(NULL);
		TCHAR PackageName[NAME_SIZE]=TEXT("");
		Parse( Text, TEXT("GUID=" ), Info.Guid );
		Parse( Text, TEXT("GEN=" ),  Info.RemoteGeneration );
		Parse( Text, TEXT("SIZE="),  Info.FileSize );
		Info.DownloadSize = Info.FileSize;
		Parse( Text, TEXT("FLAGS="), Info.PackageFlags );
		Parse( Text, TEXT("PKG="), PackageName, ARRAY_COUNT(PackageName) );
		Parse( Text, TEXT("FNAME="), Info.URL );
		Info.Parent = CreatePackage(NULL,PackageName);
	}
	else if( ParseCommand(&Text,TEXT("USERFLAG")) )
	{
		Connection->UserFlags = appAtoi(Text);
	}
	else if( ParseCommand( &Text, TEXT("CHALLENGE") ) )
	{
		// Challenged by server.
		INT i=0;
		INT RemoteStats = 0;
		INT Sec=1;

		Parse( Text, TEXT("VER="), Connection->NegotiatedVer );
		Parse( Text, TEXT("CHALLENGE="), Connection->Challenge );
		Parse( Text, TEXT("STATS="), RemoteStats );
		Parse( Text, TEXT("SEC="),Sec);

		FURL PartialURL(URL);
		PartialURL.Host = TEXT("");
		for( i=URL.Op.Num()-1; i>=0; i-- )
			if( URL.Op(i).Left(5)==TEXT("game=") )
				URL.Op.Remove( i );
		//epic moelfke: add number of splitscreen clients
		UClient* Client = UEngine::GEngine->Client;
		if (Client && Client->SplitScreenMgr)
		{
			INT NumClients = 0;
			for (i = 0; i < Client->Viewports.Num(); i++)
			{
				if (Client->Viewports(i) && !Client->SplitScreenMgr->IsOverlayViewport(Client->Viewports(i)))
				{
					NumClients++;
				}
			}
			PartialURL.AddOption(*FString::Printf(TEXT("NumClients=%i"), NumClients));
		}
		NetDriver->ServerConnection->Logf( TEXT("NETSPEED %i"), Connection->CurrentNetSpeed );
		NetDriver->ServerConnection->Logf( TEXT("LOGIN RESPONSE=%i URL=%s"), Engine->ChallengeResponse(Connection->Challenge), *PartialURL.String() );
		NetDriver->ServerConnection->FlushNet();
	}
	else if( ParseCommand( &Text, TEXT("DLMGR") ) )
	{
		INT i = Connection->DownloadInfo.AddZeroed();
		Parse( Text, TEXT("CLASS="), Connection->DownloadInfo(i).ClassName );
		Parse( Text, TEXT("PARAMS="), Connection->DownloadInfo(i).Params );
		ParseUBOOL( Text, TEXT("COMPRESSION="), Connection->DownloadInfo(i).Compression );
		Connection->DownloadInfo(i).Class = StaticLoadClass( UDownload::StaticClass(), NULL, *Connection->DownloadInfo(i).ClassName, NULL, LOAD_NoWarn | LOAD_Quiet, NULL );
		if( !Connection->DownloadInfo(i).Class )
			Connection->DownloadInfo.Remove(i);
	}
	else if( ParseCommand( &Text, TEXT("WELCOME") ) )
	{
		// Server accepted connection.
		debugf( NAME_DevNet, TEXT("Welcomed by server: %s"), Text );

		// Parse welcome message.
		Parse( Text, TEXT("LEVEL="), URL.Map );
		ParseUBOOL( Text, TEXT("LONE="), LonePlayer );
		Parse( Text, TEXT("CHALLENGE="), Connection->Challenge );

		//epic moelfke: get the gametype the server is playing and add it to our URL
		FString Gametype;
		Parse(Text, TEXT("GAME="), Gametype);
		URL.AddOption(*FString::Printf(TEXT("GAME=%s"),*Gametype));
		// if we've already preloaded the level, we must make sure the LevelInfo's team game flag is correctly set
		if (bPreloadedLevel)
		{
			UGameEngine* GameEngine = Cast<UGameEngine>(Engine);
			checkSlow(GameEngine);
			checkSlow(GameEngine->GLevel);
			UClass* GameClass = StaticLoadClass(AGameInfo::StaticClass(), NULL, *Gametype, NULL, LOAD_NoFail, NULL);
			if (GameClass)
			{
				GameEngine->GLevel->GetLevelInfo()->bPendingNetTeamGame = GameClass->GetDefaultObject<AGameInfo>()->bTeamGame;
			}
		}

		// Make sure all packages we need are downloadable.
		for (INT i = 0; i < Connection->PackageMap->List->Num(); i++)
		{
			TCHAR Filename[256];
			FPackageInfo& Info = (*Connection->PackageMap->List)(i);
			if( !appFindPackageFile( Info.Parent->GetName(), &Info.Guid, Filename ) )
			{
				appSprintf( Filename, TEXT("%s%s"), Info.Parent->GetName(), DLLEXT );
				if( GFileManager->FileSize(Filename) <= 0 )
				{
					// We need to download this package.
					FilesNeeded++;
					Info.PackageFlags |= PKG_Need;
					if( !NetDriver->AllowDownloads || !(Info.PackageFlags & PKG_AllowDownload) )
					{
						Error = FString::Printf( TEXT("Downloading '%s' not allowed"), Info.Parent->GetName() );
						NetDriver->ServerConnection->State = USOCK_Closed;
						return;
					}
				}
			}
		}

		// Send first download request.
		ReceiveNextFile( Connection );

		// We have successfully connected.
		Success = 1;
	}
	//epic moelfke: server sends list of classes the client should fully load the assets for before sending a join message
	else if (ParseCommand(&Text, TEXT("LOAD")))
	{
		FString ClassName;
		Parse(Text, TEXT("CLASS="), ClassName);
		new(LoadClassNames) FString(ClassName);
		if (bPreloadedLevel)
		{
			UGameEngine* GameEngine = Cast<UGameEngine>(Engine);
			checkSlow(GameEngine);
			checkSlow(GameEngine->GLevel);

			// we may have already started DLOing things
			// so we must add the new class to the list here
			// note: if we preloaded the level at the beginning we may end up DLOing things twice
			// depending on the order we receive things from the server,
			// but that's better than the risk of missing something and crashing
			UClass* Class = (UClass*)UObject::StaticLoadObject(UClass::StaticClass(), NULL, *ClassName, NULL, LOAD_NoWarn, NULL);
			if (Class && Class->IsChildOf(AActor::StaticClass()))
			{
				GameEngine->GLevel->GetLevelInfo()->eventDynamicLoadAssetsFor(Class);
			}
		}
	}
	else if (ParseCommand(&Text, TEXT("UNLOAD")))
	{
		FString ClassName;
		Parse(Text, TEXT("CLASS="), ClassName);
		UBOOL bFound = false;
		for (INT i = 0; i < LoadClassNames.Num() && !bFound; i++)
		{
			if (LoadClassNames(i) == ClassName)
			{
				LoadClassNames.Remove(i, 1);
				bFound = true;
			}
		}

		if (!bFound)
		{
			debugf(NAME_Warning, TEXT("Received UNLOAD for class %s that is not in the LoadClassNames list"), *ClassName);
		}
	}
	else
	{
		// Other command.
	}
	unguard;
}
UBOOL UNetPendingLevel::TrySkipFile()
{
	guard(UNetPendingLevel::TrySkipFile);
	return NetDriver->ServerConnection->Download && NetDriver->ServerConnection->Download->TrySkipFile();
	unguard;
}
void UNetPendingLevel::NotifyReceivedFile( UNetConnection* Connection, INT PackageIndex, const TCHAR* InError, UBOOL Skipped )
{
	guard(UNetPendingLevel::NotifyReceivedFile);
	check(Connection->PackageMap->List->IsValidIndex(PackageIndex));

	// Map pack to package.
	FPackageInfo& Info = (*Connection->PackageMap->List)(PackageIndex);
	if( *InError )
	{
		if( Connection->DownloadInfo.Num() > 1 )
		{
			// Try with the next download method.
			Connection->DownloadInfo.Remove(0);
			ReceiveNextFile( Connection );
		}
		else
		{
			// If transfer failed, so propagate error.
			if( Error==TEXT("") )
				Error = FString::Printf( LocalizeError(TEXT("DownloadFailed"),TEXT("Engine")), Info.Parent->GetName(), InError );
		}
	}
	else
	{
		// Now that a file has been successfully received, mark its package as downloaded.
		check(Connection==NetDriver->ServerConnection);
		check(Info.PackageFlags&PKG_Need);
		Info.PackageFlags &= ~PKG_Need;
		FilesNeeded--;
		if( Skipped )
			Connection->PackageMap->List->Remove(PackageIndex);
		// Send next download request.
		ReceiveNextFile( Connection );
	}
	unguard;
}
void UNetPendingLevel::ReceiveNextFile( UNetConnection* Connection )
{
	guard(UNetPendingLevel::ReceiveNextFile);
	for (INT i = 0; i < Connection->PackageMap->List->Num(); i++)
	{
		if ((*Connection->PackageMap->List)(i).PackageFlags & PKG_Need)
		{
			Connection->ReceiveFile(i);
			return;
		}
	}
	// we've finished downloading everything we need
	if( Connection->Download )
		delete Connection->Download;

	//epic moelfke: if we've preloaded the level, now we have to set up the package map,
	//since that would not have happened in UGameEngine::LoadMap() due to us not knowing the required packages yet
	if (bPreloadedLevel)
	{
		checkSlow(NetDriver);
		// set up the package map
		guard(SetupPackageMap);
#ifndef FINAL_RELEASE
		try
		{
#endif
			BeginLoad();
			// first get all the linkers
			for (INT i = 0; i < Connection->PackageMap->List->Num(); i++)
			{
				(*Connection->PackageMap->List)(i).Linker = GetPackageLinker
				(
					(*Connection->PackageMap->List)(i).Parent,
					NULL,
					LOAD_Verify | LOAD_Throw | LOAD_NoWarn | LOAD_NoVerify,
					NULL,
					&(*Connection->PackageMap->List)(i).Guid
				);
			}
			// verify them
			for (INT i = 0; i < Connection->PackageMap->List->Num(); i++)
			{
				VerifyLinker((*Connection->PackageMap->List)(i).Linker);
			}
			EndLoad();

			// compute the package map
			Connection->PackageMap->Compute();
#ifndef FINAL_RELEASE
		}
		#if UNICODE
		catch( TCHAR* CatchError )
		#else
		catch( char* CatchError )
		#endif
		{
			// Safely failed loading.
			EndLoad();
			Error = CatchError;
			return;
		}
#endif
		unguard;
	}
	
	// load the game class
	UClass* GameClass = (UClass*)StaticLoadObject(UClass::StaticClass(), NULL, URL.GetOption(TEXT("GAME="), TEXT("Engine.GameInfo")), NULL, LOAD_NoWarn, NULL);
	checkSlow(GameClass && GameClass->IsChildOf(AGameInfo::StaticClass()));
	// if we have classes or XPRs we must start loading after the level has been loaded, do that now
	if (LoadClassNames.Num() || GameClass->GetDefaultObject<AGameInfo>()->RequiredXPRs.Num())
	{
		UGameEngine* GameEngine = Cast<UGameEngine>(Engine);
		checkSlow(GameEngine);
		checkSlow(GameEngine->GLevel);
		if (!bPreloadedLevel)
		{
			// need to load the level first
			// gameclass might get garbage collected, so need to reload it after the map
			GameClass = NULL;
			// errors here are fatal, so set Error in that case so the connection is aborted
			GameEngine->LoadMap(URL, this, NULL, Error);
			if (Error == TEXT(""))
			{
				// success
				bPreloadedLevel = true;
				// reload the game class
				GameClass = (UClass*)StaticLoadObject(UClass::StaticClass(), NULL, URL.GetOption(TEXT("GAME="), TEXT("Engine.GameInfo")), NULL, LOAD_NoWarn, NULL);
				checkSlow(GameClass && GameClass->IsChildOf(AGameInfo::StaticClass()));
			}
		}
		if (Error == TEXT(""))
		{

			debugf(TEXT("Need to load assets before sending join"));
			// send a keepalive message to the server so it knows we're still going
			Connection->Logf(TEXT("KEEPALIVE"));
			Connection->FlushNet();
#if USE_LINEAR_LOADING && LIN_LOAD
			// first load any gametype-required XPRs
			for (INT Index = 0; Index < GameClass->GetDefaultObject<AGameInfo>()->RequiredXPRs.Num(); Index++)
			{
				extern void LoadXPRGroup(const TCHAR*, UBOOL);
				LoadXPRGroup(*GameClass->GetDefaultObject<AGameInfo>()->RequiredXPRs(Index), FALSE);
			}
#endif
			// load assets for classes the server told us we need
			for (INT i = 0; i < LoadClassNames.Num(); i++)
			{
				UClass* Class = (UClass*)UObject::StaticLoadObject(UClass::StaticClass(), NULL, *LoadClassNames(i), NULL, LOAD_NoWarn, NULL);
				if (Class && Class->IsChildOf(AActor::StaticClass()))
				{
					GameEngine->GLevel->GetLevelInfo()->eventDynamicLoadAssetsFor(Class);
				}
			}

			if (GameEngine->GLevel->GetLevelInfo()->bForceDynamicLoadAll)
			{
				// hack to always DLO everything at once
				while (GameEngine->GLevel->GetLevelInfo()->RequiredXPRs.Num() || GameEngine->GLevel->GetLevelInfo()->DynamicLoadList.Num())
				{
					GInitRunaway();
					GameEngine->GLevel->GetLevelInfo()->eventTick(0.f);
				}
				return;
			}

			// spawn a PlayerController for each viewport in the newly loaded level so they can move around while
			// waiting for loading to complete
			// NOTE: The PlayerControllers are destroyed when a new one is replicated from the server
			// in APlayerController::SetPlayer()

			// iterate through the viewports and spawn a PlayerController for each
			USplitScreenMgr* SplitScreenMgr = GameEngine->Client->GetSplitScreenMgr();
			for (INT i = 0; i < GameEngine->Client->Viewports.Num(); i++)
			{
				if ( GameEngine->Client->Viewports(i) &&
					(!SplitScreenMgr || !SplitScreenMgr->IsOverlayViewport(GameEngine->Client->Viewports(i))) )
				{
					APlayerController* Actor = GameClass->GetDefaultObject<AGameInfo>()->eventSpawnNetSpectator(GameEngine->GLevel->GetLevelInfo());
					if (Actor)
					{
						Actor->GamepadID = appAtoi(URL.GetOption(TEXT("GAMEPADID="), appItoa(Actor->GamepadID)));

						// Possess the newly-spawned player.	
						Actor->SetPlayer(GameEngine->Client->Viewports(i));
						Actor->Role = ROLE_Authority;
						Actor->RemoteRole = ROLE_None;
						Actor->ShowFlags = SHOW_GameDefault;
						Actor->RendMap = REN_DynLight;

						// add the PC to our list
						TempPCs.AddItem(Actor);
					}
				}
			}
		}
	}

	unguard;
}

UBOOL UNetPendingLevel::NotifySendingFile( UNetConnection* Connection, FGuid Guid )
{
	guard(UNetPendingLevel::NotifySendingFile);

	// Server has requested a file from this client.
	debugf( NAME_DevNet, LocalizeError(TEXT("RequestDenied"),TEXT("Engine")) );
	return 0;

	unguard;
}

//
// Update the pending level's status.
//
void UNetPendingLevel::Tick( FLOAT DeltaTime )
{
	guard(UNetPendingLevel::Tick);
	
	check(NetDriver);
	check(NetDriver->ServerConnection);

	// Handle timed out or failed connection.
	if( NetDriver->ServerConnection->State==USOCK_Closed && Error==TEXT("") )
		Error = LocalizeError(TEXT("ConnectionFailed"),TEXT("Engine"));


	if ( Error != TEXT("") )
		return;

	// Update network driver.
	NetDriver->TickDispatch( DeltaTime );
	NetDriver->TickFlush();

	// A handshake that stops making progress before WELCOME fails after InitialConnectTimeout without a
	// control message from the server. The server's acks of our keep-alives keep the connection from timing out,
	// so a server that lost its CHALLENGE and never resends it (Epic's ack logic, which UnConn.cpp corrects; a server
	// built without that still does it) would leave the client waiting in the menu forever. Counted from the last
	// message received (GUC2HandshakeProgress), after this tick's packets were processed, so a long frame on this side
	// (software GL starting up) does not count. Downloads start only after WELCOME and are not limited by this.
	if( !Success && Error==TEXT("") && NetDriver->ServerConnection
	&&	NetDriver->Time - GUC2HandshakeProgress > NetDriver->InitialConnectTimeout )
	{
		debugf( NAME_DevNet, TEXT("Pending connection: no message from the server for %.0f s before WELCOME, giving up"), NetDriver->InitialConnectTimeout );
		Error = LocalizeError(TEXT("ConnectionFailed"),TEXT("Engine"));
	}
	
	//epic moelfke: tick any temporary spectator PlayerControllers
	if (bPreloadedLevel)
	{
		for (INT i = 0; i < TempPCs.Num(); i++)
		{
			if (TempPCs(i))
			{
				TempPCs(i)->Tick(DeltaTime, LEVELTICK_All);
			}
		}

		if (LoadClassNames.Num())
		{
			// since we're loading in the background, it may be some time before we can send the join, so send
			// a keepalive message every 5 seconds so the server knows we're still going
			static FLOAT NextKeepAliveTime = 5.0f;
			NextKeepAliveTime -= DeltaTime;
			if (NextKeepAliveTime < 0.f)
			{
				NetDriver->ServerConnection->Logf(TEXT("KEEPALIVE"));
				NetDriver->ServerConnection->FlushNet();
				NextKeepAliveTime += 5.0f;
			}
		}
	}

	unguard;
}
//
// Send JOIN to other end
//
void UNetPendingLevel::SendJoin()
{
	guard(UNetPendingLevel::SendJoin);

	SentJoin = 1;
	debugf( TEXT("Sending: JOIN") );
	NetDriver->ServerConnection->Logf( TEXT("JOIN") );
	NetDriver->ServerConnection->FlushNet();

	//epic moelfke: if we've loaded the level already, we need to return the net driver to it
	if (bPreloadedLevel)
	{
		UGameEngine* GameEngine = Cast<UGameEngine>(Engine);
		checkSlow(GameEngine);
		checkSlow(GameEngine->GLevel);
		GameEngine->GLevel->NetDriver = NetDriver;
		NetDriver->Notify = GameEngine->GLevel;
		NetDriver = NULL;
	}

	unguard;
}

void UNetPendingLevel::Destroy()
{
	//epic moelfke: if the level has been preloaded, we need to make sure it gets the NetDriver back
	if (bPreloadedLevel)
	{
		UGameEngine* GameEngine = Cast<UGameEngine>(Engine);
		if (NetDriver && GameEngine && GameEngine->GLevel)
		{
			// return the net driver to the active level
			GameEngine->GLevel->NetDriver = NetDriver;
			NetDriver->Notify = GameEngine->GLevel;
			NetDriver = NULL;
		}

		TempPCs.Empty();
	}

	Super::Destroy();
}

IMPLEMENT_CLASS(UNetPendingLevel);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

