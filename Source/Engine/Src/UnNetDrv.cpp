/*=============================================================================
	UnNetDrv.cpp: Unreal network driver base class.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "EnginePrivate.h"
#include "UnNet.h"
#include "UnLinker.h"	// uc2 port: the static-index diagnostic in UPackageMapLevel::SerializeObject

/*-----------------------------------------------------------------------------
	UPackageMapLevel implementation.
-----------------------------------------------------------------------------*/

UBOOL UPackageMapLevel::CanSerializeObject( UObject* Obj )
{
	guardSlow(UPackageMapLevel::CanSerializeObject);
	AActor* Actor = Cast<AActor>(Obj);
	if( Actor && !Actor->bStatic && !Actor->bNoDelete )
	{
		UActorChannel* Ch = Connection->ActorChannels.FindRef(Actor);
		//old: induces a bit of lag. return Ch && Ch->OpenAcked;
		return Ch!=NULL; //new: reduces lag, increases bandwidth slightly.
	}
	else return 1;
	unguardSlow;
}
UBOOL UPackageMapLevel::SerializeObject( FArchive& Ar, UClass* Class, UObject*& Object )
{
	guard(UPackageMapLevel::SerializeObject);
	DWORD Index=0;
	if( Ar.IsLoading() )
	{
		Object = NULL;
		BYTE B=0; Ar.SerializeBits( &B, 1 );
		if( B )
		{
			// Dynamic actor or None.
			Ar.SerializeInt( Index, UNetConnection::MAX_CHANNELS );
			if( Index==0 )
			{
				Object = NULL;
			}
			else if
			(	!Ar.IsError()
			&&	Index>=0
			&&	Index<UNetConnection::MAX_CHANNELS
			&&	Connection->Channels[Index]
			&&	Connection->Channels[Index]->ChType==CHTYPE_Actor
			&&	!Connection->Channels[Index]->bClosing )
				Object = ((UActorChannel*)Connection->Channels[Index])->GetActor();
		}
		else
		{
			// Static object.
			Ar.SerializeInt( Index, MaxObjectIndex );
			if( !Ar.IsError() )
				Object = IndexToObject( Index, 1 );
			// uc2 port: diagnostic, which export a static index named when it resolves to nothing here.
			if( !Object && !Ar.IsError() )
			{
				INT Rest = Index;
				for( INT i=0; i<List->Num(); i++ )
				{
					FPackageInfo& Info = (*List)(i);
					if( Rest < Info.ObjectCount )
					{
						debugf( NAME_DevNet, TEXT("Static object index %i = %s export %i (%s %s) not available"), Index, Info.Parent ? Info.Parent->GetName() : TEXT("None"), Rest, *Info.Linker->GetExportClassName(Rest), *Info.Linker->ExportMap(Rest).ObjectName );
						break;
					}
					Rest -= Info.ObjectCount;
				}
			}
		}
		if( Object && !Object->IsA(Class) )
		{
			debugf(TEXT("Forged object: got %s, expecting %s"),Object->GetFullName(),Class->GetFullName());
			Object = NULL;
		}
		return 1;
	}
	else
	{
		AActor* Actor = Cast<AActor>(Object);
		if( Actor && !Actor->bStatic && !Actor->bNoDelete )
		{
			// Map dynamic actor through channel index.
			BYTE B=1; Ar.SerializeBits( &B, 1 );
			UActorChannel* Ch = Connection->ActorChannels.FindRef(Actor);
			UBOOL Mapped = 0;
			if( Ch )
			{
				Index  = Ch->ChIndex;
				Mapped = Ch->bOpenAcked;
			}
			Ar.SerializeInt( Index, UNetConnection::MAX_CHANNELS );
			return Mapped;
		}
		else if( !Object || (Index=ObjectToIndex(Object))==INDEX_NONE )
		{
			BYTE B=1; Ar.SerializeBits( &B, 1 );
			Ar.SerializeInt( Index, UNetConnection::MAX_CHANNELS );
			return 1;
		}
		else
		{
			// Map regular object.
			// Since mappability doesn't change dynamically, there is no advantage to setting Result!=0.
			BYTE B=0; Ar.SerializeBits( &B, 1 );
			Ar.SerializeInt( Index, MaxObjectIndex );
			return 1;
		}
	}
	unguardf(( TEXT("(%s %s)"), Class->GetFullName(), Object->GetFullName() ));
}
IMPLEMENT_CLASS(UPackageMapLevel);

/*-----------------------------------------------------------------------------
	UNetDriver implementation.
-----------------------------------------------------------------------------*/

UNetDriver::UNetDriver()
:	ClientConnections()
,	Time( 0.f )
,	LastReceiveTime( 0.f )
,	bStandByCheatFound( 0 )
,	DownloadManagers( E_NoInit )
{
	guard(UNetDriver::UNetDriver);
	RoleProperty       = FindObjectChecked<UProperty>( AActor::StaticClass(), TEXT("Role"      ) );
	RemoteRoleProperty = FindObjectChecked<UProperty>( AActor::StaticClass(), TEXT("RemoteRole") );
	MasterMap          = new UPackageMap;
	ProfileStats	   = ParseParam(appCmdLine(),TEXT("profilestats"));
	unguard;
}
void UNetDriver::StaticConstructor()
{
	guard(UNetDriver::StaticConstructor);

	//scion rwd - moved these above the property read, so that they really are defaults
	// Default values.
	MaxClientRate = 10000;
	MaxInternetClientRate = 7000;

	// Expose CPF_Config properties to be loaded from .ini.
	new(GetClass(),TEXT("ConnectionTimeout"),    RF_Public)UFloatProperty(CPP_PROPERTY(ConnectionTimeout    ), TEXT("Client"), CPF_Config );
	new(GetClass(),TEXT("InitialConnectTimeout"),RF_Public)UFloatProperty(CPP_PROPERTY(InitialConnectTimeout), TEXT("Client"), CPF_Config );
	new(GetClass(),TEXT("KeepAliveTime"),        RF_Public)UFloatProperty(CPP_PROPERTY(KeepAliveTime        ), TEXT("Client"), CPF_Config );
	new(GetClass(),TEXT("RelevantTimeout"),      RF_Public)UFloatProperty(CPP_PROPERTY(RelevantTimeout      ), TEXT("Client"), CPF_Config );
	new(GetClass(),TEXT("SpawnPrioritySeconds"), RF_Public)UFloatProperty(CPP_PROPERTY(SpawnPrioritySeconds ), TEXT("Client"), CPF_Config );
	new(GetClass(),TEXT("ServerTravelPause"),    RF_Public)UFloatProperty(CPP_PROPERTY(ServerTravelPause    ), TEXT("Client"), CPF_Config );
	new(GetClass(),TEXT("MaxClientRate"),		 RF_Public)UIntProperty  (CPP_PROPERTY(MaxClientRate        ), TEXT("Client"), CPF_Config );
	new(GetClass(),TEXT("MaxInternetClientRate"),RF_Public)UIntProperty  (CPP_PROPERTY(MaxInternetClientRate), TEXT("Client"), CPF_Config );
	new(GetClass(),TEXT("NetServerMaxTickRate"), RF_Public)UIntProperty  (CPP_PROPERTY(NetServerMaxTickRate ), TEXT("Client"), CPF_Config );
	new(GetClass(),TEXT("LanServerMaxTickRate"), RF_Public)UIntProperty  (CPP_PROPERTY(LanServerMaxTickRate ), TEXT("Client"), CPF_Config );
	new(GetClass(),TEXT("AllowDownloads"),       RF_Public)UBoolProperty (CPP_PROPERTY(AllowDownloads       ), TEXT("Client"), CPF_Config );
	new(GetClass(),TEXT("MaxDownloadSize"),	     RF_Public)UIntProperty  (CPP_PROPERTY(MaxDownloadSize      ), TEXT("Client"), CPF_Config );

	UArrayProperty* B = new(GetClass(),TEXT("DownloadManagers"),RF_Public)UArrayProperty( CPP_PROPERTY(DownloadManagers), TEXT("Client"), CPF_Config );
	B->Inner = new(B,TEXT("StrProperty0"),RF_Public)UStrProperty;

	unguard;
}
void UNetDriver::AssertValid()
{
	guard(UNetDriver::AssertValid);
	unguard;
}
void UNetDriver::TickFlush()
{
	guard(UNetDriver::TickFlush);

	// Poll all sockets.
	if( ServerConnection )
		ServerConnection->Tick();
	for( INT i=0; i<ClientConnections.Num(); i++ )
		ClientConnections(i)->Tick();

	unguard;
}
UBOOL UNetDriver::InitConnect( FNetworkNotify* InNotify, FURL& URL, FString& Error )
{
	guard(UNetDriver::Init);
	Notify = InNotify;
	return 1;
	unguard;
}
UBOOL UNetDriver::InitListen( FNetworkNotify* InNotify, FURL& URL, FString& Error )
{
	guard(UNetDriver::Init);
	Notify = InNotify;
	return 1;
	unguard;
}

static bool bEnableStandbyCheatDetection = true;
void UNetDriver::SetStandbyCheatStatus( UBOOL bStandbyEnabled )
{
	guardSlow(UNetDriver::SetStandbyCheatStatus);

	if ( bStandByCheatFound != bStandbyEnabled )		// don't send duplicate notifications
	{
		bStandByCheatFound = bStandbyEnabled;

		check(Notify);
		Notify->NotifyStandbyHack(bStandByCheatFound);
	}

	unguardSlow;
}
void UNetDriver::UpdateLastReceiveTime()
{
	guardSlow(UNetDriver::UpdateLastReceiveTime);

	SetStandbyCheatStatus(false);
	LastReceiveTime = Time;

	unguardSlow;
}

// determines whether this connection should be considered when detecting standby hacks
UBOOL UNetDriver::ShouldIncludeInStandbyCheck( const UNetConnection* Connection ) const
{
	guard(UNetDriver::ShouldIncludeInStandbyCheck);

	if ( Connection == NULL )
		return false;

	// if connection is still pending, ignore it. (still loading)
	if ( Connection->State == USOCK_Pending )
		return false;

	// if the connection doesn't have an actor yet, ignore it. (still loading)
	if ( Connection->Actor == NULL )
		return false;

	return true;

	unguard;
}

void UNetDriver::TickDispatch( FLOAT DeltaTime )
{
	guard(UNetDriver::TickDispatch);
	SendCycles=RecvCycles=0;

	// Get new time.
	Time += DeltaTime;

	//epic superville: Try to catch standby cheat
	// Description: Cable modems have a Standby button that can suspend out going packets
	// but still keeps a valid connection to the computer/XBox.  The server can then run around
	// and kill everyone, pickup powerups, whatever and then resume the standby.  Clients will then
	// just see themselves gib (if they were killed)
	// Catch: To catch this, we check to see if there has been too much time between incoming packets
	// from any clients. If there is a gap, notify the game engine to handle it.

	// Ignore hitches in frame rate that are more than half the cheat timeout
	// (G-10b: a driver may opt out of the check as a whole, StandbyCheckEnabled; every native driver keeps it.)
	if( bEnableStandbyCheatDetection && StandbyCheckEnabled() )
	{
		if ( DeltaTime > STANDBYCHEAT_FRAMERATECAP )
		{
			LastReceiveTime = Time;
		}

		// Ignore clients that are still loading
		// (No other hitches should happen during gameplay)
		INT NumClients = 0;
		for( INT i = 0; i < ClientConnections.Num(); i++ )
		{
			if ( ShouldIncludeInStandbyCheck(ClientConnections(i)) )
				NumClients++;
		}

		// if we've received at least one packet
		if( LastReceiveTime > 0 &&

			// and we're the server, we have at least one valid client connection,
			// and we haven't received a packet from any clients for STANDBYCHEAT_TIMEOUT seconds,
			// and we aren't currently loading stuff...
			((NumClients && Time - LastReceiveTime > STANDBYCHEAT_TIMEOUT) ||

			// or we're the client,
			// and the server connection is open (not pending or closed)
			// and we haven't received a packet from the server for STANDBYCHEAT_TIMEOUT * 3 seconds
			// (allow clients a larger margin of error, since we only have one connection at most, to base our calculations on)
			(ServerConnection && ServerConnection->State == USOCK_Open && Time - LastReceiveTime > STANDBYCHEAT_TIMEOUT * 3) )
		)
		{
			if ( !bStandByCheatFound )
			{
				debugf(NAME_DevNet, TEXT("Standby hack detected!  ValidClients:%i/%i   Time:%f  LastReceiveTime:%f  DeltaTime:%f"), NumClients, ClientConnections.Num(), Time, LastReceiveTime, DeltaTime);
	//			Exec(TEXT("SOCKETS"), *GLog);
			}

			SetStandbyCheatStatus(true);
		}
	}

	// Delete any straggler connections.
	if( !ServerConnection )
	{
		for( INT i=ClientConnections.Num()-1; i>=0; i-- )
		{
			if( ClientConnections(i)->State==USOCK_Closed )
			{
				delete ClientConnections(i);
			}
		}
	}

	if ( bStandByCheatFound &&
		ServerConnection == NULL &&
		ClientConnections.Num() == 0 )
	{
		debugf(NAME_DevLive, TEXT("No valid connections remain - disabling standby hack protection!"));
		SetStandbyCheatStatus(false);
	}

	unguard;
}
void UNetDriver::Serialize( FArchive& Ar )
{
	guard(UNetDriver::Serialize);
	Super::Serialize( Ar );

	// Prevent referenced objects from being garbage collected.
	Ar << ClientConnections << ServerConnection << MasterMap << RoleProperty << RemoteRoleProperty;

	unguard;
}
void UNetDriver::Destroy()
{
	guard(UNetDriver::Destroy);

	// Destroy server connection.
	guard(DestroyServerConnection);
	if( ServerConnection )
		delete ServerConnection;
	unguard;

	// Destroy client connections.
	guard(DestroyClientConnections);
	while( ClientConnections.Num() )
		delete ClientConnections( 0 );
	unguard;

	// Low level destroy.
	LowLevelDestroy();

	// Delete the master package map.
	delete MasterMap;

	Super::Destroy();
	unguard;
}
UBOOL UNetDriver::Exec( const TCHAR* Cmd, FOutputDevice& Ar )
{
	guard(UNetDriver::Exec);
	if( ParseCommand(&Cmd,TEXT("SOCKETS")) )
	{
		// Print list of open connections.
		Ar.Logf( TEXT("Connections:") );
		if( ServerConnection )
		{
			Ar.Logf( TEXT("   Server %s"), *ServerConnection->LowLevelDescribe() );
			for( INT i=0; i<ServerConnection->OpenChannels.Num(); i++ )
				Ar.Logf( TEXT("      Channel %i: %s"), ServerConnection->OpenChannels(i)->ChIndex, *ServerConnection->OpenChannels(i)->Describe() );
		}
		for( INT i=0; i<ClientConnections.Num(); i++ )
		{
			UNetConnection* Connection = ClientConnections(i);
			Ar.Logf( TEXT("   Client %s"), *Connection->LowLevelDescribe() );
			for( INT i=0; i<Connection->OpenChannels.Num(); i++ )
				Ar.Logf( TEXT("      Channel %i: %s"), Connection->OpenChannels(i)->ChIndex, *Connection->OpenChannels(i)->Describe() );
		}
		return 1;
	}
	else if ( ParseCommand(&Cmd,TEXT("STANDBYHACK")) )
	{
		UBOOL bStandby = appAtoi(Cmd);
		Notify->NotifyStandbyHack(bStandby);
		return 1;
	}
	else if ( ParseCommand(&Cmd,TEXT("TOGGLE_SP")) )
	{
		bEnableStandbyCheatDetection = !bEnableStandbyCheatDetection;
		Ar.Logf(TEXT("Standby cheat protection now %s"), bEnableStandbyCheatDetection ? TEXT("ENABLED") : TEXT("DISABLED"));
		return 1;
	}
	else if ( ParseCommand(&Cmd,TEXT("SHOW_SP")) )
	{
		Ar.Logf(TEXT("Standby cheat protection now %s"), bEnableStandbyCheatDetection ? TEXT("ENABLED") : TEXT("DISABLED"));
		return 1;
	}

	else return 0;
	unguard;
}
void UNetDriver::NotifyActorDestroyed( AActor* ThisActor )
{
	guard(UNetDriver::NotifyActorDestroyed);
	for( INT i=ClientConnections.Num()-1; i>=0; i-- )
	{
		UNetConnection* Connection = ClientConnections(i);
		if( ThisActor->bNetTemporary )
			Connection->SentTemporaries.RemoveItem( ThisActor );
		UActorChannel* Channel = Connection->ActorChannels.FindRef(ThisActor);
		if( Channel )
		{
			check(Channel->bOpenedLocally);
			Channel->Close();
		}
	}
	unguard;
}

void UNetDriver::PreServerTravel()
{
	guard(UNetDriver::PreServerTravel);

	for ( INT i = ClientConnections.Num() - 1; i >= 0; i-- )
	{
		UNetConnection* Connection = ClientConnections(i);
		if ( Connection && Connection->IsLoadingAssets() )
		{
			Connection->Logf(TEXT("FAILURE FORCED"));
			Connection->FlushNet();
		}
	}

	unguard;
}

/* scion ======================================================================
* UNetDriver::CreateChild
* Author: sz (ripped from UC1)
*
* Create a new child connection and add it to the existing parent connection.
* All transactions using the child connection are just routed to the parent
* conneciton.
* =============================================================================
*/
UChildConnection* UNetDriver::CreateChild(UNetConnection* Parent)
{
	guard(UNetDriver::CreateChild);

	debugf( NAME_DevSplitScreen, TEXT("Creating child connection with %s parent"), Parent->GetName() );
	UChildConnection* child = new UChildConnection();
	child->Driver = this;
	child->URL = FURL();
	child->State = Parent->State;
	child->URL.Host = Parent->URL.Host;
	child->Parent = Parent;
	child->PackageMap = Parent->PackageMap;
	child->CurrentNetSpeed = Parent->CurrentNetSpeed;
	Parent->Children.AddItem(child);
	return child;

	unguard;
}

IMPLEMENT_CLASS(UNetDriver);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

