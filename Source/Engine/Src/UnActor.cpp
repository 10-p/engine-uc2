/*=============================================================================
	UnActor.cpp: AActor implementation
	Copyright 1997-2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "EnginePrivate.h"
#include "UnNet.h"

/*-----------------------------------------------------------------------------
	AActor object implementations.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(AActor);
IMPLEMENT_CLASS(ALight);
IMPLEMENT_CLASS(AClipMarker);
IMPLEMENT_CLASS(APolyMarker);
IMPLEMENT_CLASS(AWeapon);
IMPLEMENT_CLASS(ANote);
IMPLEMENT_CLASS(ALevelInfo);
IMPLEMENT_CLASS(AGameInfo);
IMPLEMENT_CLASS(ACamera);
IMPLEMENT_CLASS(AZoneInfo);
IMPLEMENT_CLASS(ASkyZoneInfo);
IMPLEMENT_CLASS(UReachSpec);
IMPLEMENT_CLASS(APathNode);
IMPLEMENT_CLASS(ANavigationPoint);
IMPLEMENT_CLASS(APathManager);
IMPLEMENT_CLASS(ASmallNavigationPoint);
IMPLEMENT_CLASS(AScout);
IMPLEMENT_CLASS(AInterpolationPoint);
IMPLEMENT_CLASS(ADecoration);
IMPLEMENT_CLASS(AProjectile);
IMPLEMENT_CLASS(ATeleporter);
IMPLEMENT_CLASS(APlayerStart);
IMPLEMENT_CLASS(AKeypoint);
IMPLEMENT_CLASS(AAmbientSound);
IMPLEMENT_CLASS(AInventory);
IMPLEMENT_CLASS(AInventorySpot);
IMPLEMENT_CLASS(AAIMarker);
IMPLEMENT_CLASS(ATriggers);
IMPLEMENT_CLASS(ATrigger);
IMPLEMENT_CLASS(AHUD);
IMPLEMENT_CLASS(ASavedMove);
IMPLEMENT_CLASS(ALiftCenter);
IMPLEMENT_CLASS(ALiftExit);
IMPLEMENT_CLASS(AInfo);
IMPLEMENT_CLASS(AReplicationInfo);
IMPLEMENT_CLASS(APlayerReplicationInfo);
IMPLEMENT_CLASS(AInternetInfo);
IMPLEMENT_CLASS(AGameReplicationInfo);
IMPLEMENT_CLASS(ULevelSummary);
IMPLEMENT_CLASS(AAmmo);
IMPLEMENT_CLASS(APickup);
IMPLEMENT_CLASS(AAmmunition);
IMPLEMENT_CLASS(AController);
IMPLEMENT_CLASS(AAIController);
IMPLEMENT_CLASS(AMutator);
IMPLEMENT_CLASS(ADoor);
IMPLEMENT_CLASS(UDamageType);
IMPLEMENT_CLASS(ABrush);
IMPLEMENT_CLASS(AAIScript);
IMPLEMENT_CLASS(ALineOfSightTrigger);
IMPLEMENT_CLASS(AVolume);
IMPLEMENT_CLASS(APhysicsVolume);
IMPLEMENT_CLASS(ADefaultPhysicsVolume);
IMPLEMENT_CLASS(ABlockingVolume);
IMPLEMENT_CLASS(ATeamInfo);
IMPLEMENT_CLASS(AInventoryAttachment);
IMPLEMENT_CLASS(AWeaponAttachment);
IMPLEMENT_CLASS(AAntiPortalActor);
IMPLEMENT_CLASS(AJumpPad);
IMPLEMENT_CLASS(AJumpDest);
IMPLEMENT_CLASS(AAvoidMarker);
IMPLEMENT_CLASS(ACarriedObject);
IMPLEMENT_CLASS(AMinimalGameInfo);
IMPLEMENT_CLASS(AExtraPlayerReplicationInfo); //epic moelfke
IMPLEMENT_CLASS(ARootMotionActor);

/*-----------------------------------------------------------------------------
	Replication.
-----------------------------------------------------------------------------*/

#if !FORCE_EXTREME_PACKING   // with this #define, BITFIELD is BYTE. --ryan.
static inline UBOOL NEQ(BITFIELD A,BITFIELD B,UPackageMap* Map,UActorChannel* Channel) {return A!=B;}
#endif

static inline UBOOL NEQ(BYTE A,BYTE B,UPackageMap* Map,UActorChannel* Channel) {return A!=B;}
static inline UBOOL NEQ(INT A,INT B,UPackageMap* Map,UActorChannel* Channel) {return A!=B;}
static inline UBOOL NEQ(FLOAT& A,FLOAT& B,UPackageMap* Map,UActorChannel* Channel) {return *(INT*)&A!=*(INT*)&B;}
static inline UBOOL NEQ(FVector& A,FVector& B,UPackageMap* Map,UActorChannel* Channel) {return ((INT*)&A)[0]!=((INT*)&B)[0] || ((INT*)&A)[1]!=((INT*)&B)[1] || ((INT*)&A)[2]!=((INT*)&B)[2];}
static inline UBOOL NEQ(FRotator& A,FRotator& B,UPackageMap* Map,UActorChannel* Channel) {return A.Pitch!=B.Pitch || A.Yaw!=B.Yaw || A.Roll!=B.Roll;}
static inline UBOOL NEQ(UObject* A,UObject* B,UPackageMap* Map,UActorChannel* Channel)
{
	if( Map->CanSerializeObject(A) )
		return A!=B; 
	Channel->bActorMustStayDirty = true;

	//debugf(TEXT("%s Must stay dirty because of %s"),Channel->Actor->GetName(),A->GetName());
	return (B!=NULL);
}
static inline UBOOL NEQ(FName& A,FName B,UPackageMap* Map,UActorChannel* Channel) {return *(INT*)&A!=*(INT*)&B;}
static inline UBOOL NEQ(FColor& A,FColor& B,UPackageMap* Map,UActorChannel* Channel) {return *(INT*)&A!=*(INT*)&B;}
static inline UBOOL NEQ(FPlane& A,FPlane& B,UPackageMap* Map,UActorChannel* Channel) {return
((INT*)&A)[0]!=((INT*)&B)[0] || ((INT*)&A)[1]!=((INT*)&B)[1] ||
((INT*)&A)[2]!=((INT*)&B)[2] || ((INT*)&A)[3]!=((INT*)&B)[3];}

//jg -- Replaced the copy construction of both A & B with const ref
static inline UBOOL NEQ(const FString& A,const FString& B,UPackageMap* Map,UActorChannel* Channel)
{
	return A!=B;
}

//jg -- Replaced the copy construction of both A & B with const ref
static inline UBOOL NEQ(const FCompressedPosition& A, const FCompressedPosition& B,UPackageMap* Map,UActorChannel* Channel)
{
	return 1; // only try to replicate in compressed form if already know location has changed
}

static inline UBOOL NEQ(BYTE A[8], BYTE B[8], UPackageMap* Map,UActorChannel *Channel)
{
	for (INT idx = 0; idx < 8; idx++)
	{
		if (A[idx] != B[idx])
		{
			return 1;
		}
	}
	return 0;
}

/* epic ===============================================
* ::AddToRecentMap
* Author: moelfke
*
* Adds It to RecentMap if necessary, creating a new
* BYTE array for the recent values and initializing
* it to the value in Default at its offset
*
* =====================================================
*/
FORCEINLINE void AActor::AddToRecentMap(UProperty* It, BYTE* Default, TMap<_WORD, TArray<BYTE>*>& RecentMap)
{
	guardSlow(AActor::AddToRecentMap);

	// If property is replicated
	// and it's not a bool property or its offset isn't already in the recent map
	if ( (It->PropertyFlags & CPF_Net) &&
		(!(It->GetClass()->ClassFlags & CLASS_IsAUBoolProperty) || !RecentMap.Find(It->Offset)) )
	{	
		// Retrieve property size
		INT PropSize = It->GetSize();

		// Create array of bytes with exactly the property size (no slack)
		// DO NOT FORGET TO CLEAN UP! (done in UActorChannel::Destroy())
		TArray<BYTE>* Value = NEW TArray<BYTE>(PropSize);

		// Set values of array
#ifndef _XBOX // don't care about config variables on xbox since the user can't edit them so they should be the same on both sides
		// if it's a config variable, set the recent data to 0 to force it to replicate
		// FIXME should check if value is already 0 and set to 1 instead in that case
		if (It->PropertyFlags & CPF_Config)
		{
			UBoolProperty* BoolProperty = Cast<UBoolProperty>(It, CLASS_IsAUBoolProperty);
			if (BoolProperty)
			{
				*((DWORD*)&((*Value)(0))) &= ~BoolProperty->BitMask;
			}
			else
			{
				appMemzero(&((*Value)(0)), PropSize);
			}
		}
		else
#endif
		{
			// set the recent data to the class's default value
			if (It->PropertyFlags & CPF_NeedCtorLink)
			{
				// need CopyCompleteValue() so any necessary construction occurs
				appMemzero(&((*Value)(0)), It->GetSize());
				It->CopyCompleteValue(&((*Value)(0)), Default + It->Offset);
			}
			else
			{
				// direct memory copy to save CPU
				appMemcpy(&((*Value)(0)), Default + It->Offset, PropSize);
			}
		}
		
		// Hash the value by offset
		RecentMap.Add(It->Offset, Value);
	}

	unguardSlow;
}

void AActor::NetDirty(UProperty* property)
{
	guardSlow(AActor::Dirty);
	if ( property && (property->PropertyFlags & CPF_Net) )
	{
		// test and make sure actor not getting dirtied too often!
		bNetDirty = true;
		if (bImmediatelyReplicateWhenDirty)
		{
			NetUpdateTime = Level ? (Level->TimeSeconds - 1.f) : -1.f;
		}
	}
	unguardSlow;
}

/* epic ===============================================
* ::CreateRecentMap
* Author: moelfke
*
* Creates the RecentMap for a UActorChannel. Using a
* virtual function for this allows us to avoid wasting
* space on net properties of a class's parent that the
* child class never replicates.
*
* =====================================================
*/
void AActor::CreateRecentMap(TMap<_WORD, TArray<BYTE>*>& RecentMap)
{
	guard(AActor::CreateRecentMap);

	UObject* Default = StaticConstructObject(GetClass());

	for (UProperty* It = GetClass()->PropertyLink; It; It = It->PropertyLinkNext)
	{
		AddToRecentMap(It, (BYTE*)Default, RecentMap);
	}

	delete Default;

	unguard;
}

INT* AActor::GetOptimizedRepList( BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel )
{
	guard(AActor::GetOptimizedRepList);
	if( StaticClass()->ClassFlags & CLASS_NativeReplication )
	{
		if ( bSkipActorPropertyReplication && !bNetInitial )
			return Ptr;

		if ( bReplicateMovement )
		{
			if ( RemoteRole != ROLE_AutonomousProxy )
			{
				UBOOL bAlreadyLoc = false;

				// If the actor was based and is no longer, send the location!
				GETPROP(Actor,Base,AActor*,RecentBase);
				if (!bCompressedPosition && !Base && *RecentBase)
                {
		            static UProperty* spLocation = FindObjectChecked<UProperty>(AActor::StaticClass(),TEXT("Location"));
		            *Ptr++ = spLocation->RepIndex;
					bAlreadyLoc = true;
                }

				DOREP(Actor,Base,AActor*);
				DOREPBOOL(Actor,bOnlyDrawIfAttached);

				if( Base && !Base->bWorldGeometry )
				{
					DOREP(Actor,RelativeLocation,FVector);
					DOREP(Actor,RelativeRotation,FRotator);
					DOREP(Actor,AttachmentBone,FName);

					if (!bCompressedPosition && !(*RecentBase))
					{
						DOREP(Actor,Location,FVector);
					}
				}
				else if( !bCompressedPosition && ((RemoteRole < ROLE_SimulatedProxy) || bNetInitial || bUpdateSimulatedPosition) )
				{
					if( !bNetInitial && !bAlreadyLoc )
					{
						DOREP(Actor,Location,FVector);
					}

					if( (IsDTMesh() || DrawType==DT_StaticMesh) && (!bNetInitial || !bNetInitialRotation) )
					{
						DOREP(Actor,Rotation,FRotator);
					}
				}
				if( Physics==PHYS_Rotating )
				{
					DOREPBOOL(Actor,bFixedRotationDir);
					DOREPBOOL(Actor,bRotateToDesired);
					DOREP(Actor,RotationRate,FRotator);
					DOREP(Actor,DesiredRotation,FRotator);
				}

				if( RemoteRole == ROLE_SimulatedProxy )
				{
					if( !bCompressedPosition && (bNetInitial || bUpdateSimulatedPosition) ) 
					{
						DOREP(Actor,Physics,BYTE);
						DOREP(Actor,Velocity,FVector);

						//scion superville: Replicate gravity scale with velocity
						DOREP(Actor,GravityScale,FLOAT);
					}
					else if( bNetInitial )
					{
						DOREP(Actor,Physics,BYTE);
						DOREP(Actor,GravityScale,FLOAT);
					}
				}
				else // dumb proxy
				{
					DOREP(Actor,Physics,BYTE);
					if ( Physics == PHYS_Falling ) 
					{
						DOREP(Actor,Velocity,FVector);

						//scion superville: Replicate gravity scale with velocity
						DOREP(Actor,GravityScale,FLOAT);
					}
				}
			}
			else if ( bNetInitial )
			{
				if( IsDTMesh() || DrawType==DT_StaticMesh )
				{
					DOREP(Actor,Rotation,FRotator);
				}
				DOREP(Actor,GravityScale,FLOAT);
			}
		}

		if( bNetDirty )
		{
			//scion rwd - replicate draw attributes
			if( bReplicateDrawAttributes )
			{
				DOREP(Actor,DrawScale,FLOAT);
				//DOREP(Actor,DrawScale3D); // Doesn't work in networking, because of vector rounding
				DOREP(Actor,DrawType,BYTE);
				DOREPBOOL(Actor,bOnlyOwnerSee);
				DOREP(Actor,Texture,UMaterial*);
			}
			DOREPBOOL(Actor,bCollideActors);
			DOREPBOOL(Actor,bCollideWorld);
			DOREP(Actor,Style,BYTE);
			//epic moelfke: added bReplicatePrePivot
			if (bReplicatePrePivot && (IsDTMesh() || DrawType == DT_StaticMesh))
			{
				DOREP(Actor,PrePivot,FVector);
			}
			if( bCollideActors || bCollideWorld )
			{
				DOREPBOOL(Actor,bProjTarget);
				DOREPBOOL(Actor,bBlockActors);
				DOREPBOOL(Actor,bBlockPlayers);
				DOREP(Actor,CollisionRadius,FLOAT);
				DOREP(Actor,CollisionHeight,FLOAT);
			}
			if( !bSkipActorPropertyReplication )
			{
				// skip these if bSkipActorPropertyReplication, because if they aren't relevant to the client, bNetInitial never gets cleared
				// which obviates bSkipActorPropertyReplication
				
				
				//scion superville: always replicate owner
				DOREP(Actor,Owner,AActor*);

				if( bReplicateInstigator )
				{
					DOREP(Actor,Instigator,APawn*);
				}
			}
			if( IsDTMesh() )
			{
				DOREP(Actor,AmbientGlow,BYTE);
				DOREPBOOL(Actor,bUnlit);
				if( !bNoRepMesh )
				{
					DOREP(Actor,Mesh,UMesh*);
				}
			}
			else if( DrawType==DT_StaticMesh )
			{
				DOREP(Actor,StaticMesh,UStaticMesh*);
			}

			DOREP(Actor,LightType,BYTE);

			//epic moelfke: removed lighting-related properties from replication because we never change them outside defaultproperties
 		}
		DOREPBOOL(Actor,bHidden);
		DOREP(Actor,Role,BYTE);

		if( !(bClientDemoRecording && bTearOff) )
		{
			DOREP(Actor,RemoteRole,BYTE);
		}

		DOREPBOOL(Actor,bNetOwner);
		DOREPBOOL(Actor,bTearOff);
		DOREPBOOL(Actor,bHardAttach);

	}
	return Ptr;
	unguard;
}

INT* APawn::GetOptimizedRepList( BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel )
{
	guard(APawn::GetOptimizedRepList);

	if( bNetOwner && !bSkipActorPropertyReplication && bNetDirty)
	{
		DOREP(Pawn,Inventory,AInventory*);
	}

	bCompressedPosition = false;
	if ( !bNetOwner || bDemoRecording )
	{
		GETPROP(Actor,Base,AActor*,RecentBase);
		if( bUseCompressedPosition )
		{
			if (Base ? Base->bWorldGeometry : !(*RecentBase))
			{
				GETPROP(Actor,Location,FVector,RecentLocation);
				GETPROP(Actor,Rotation,FRotator,RecentRotation);
				GETPROP(Actor,Velocity,FVector,RecentVelocity);

				if( Velocity != *RecentVelocity )
				{
					LastLocTime = Level->TimeSeconds;
					bCompressedPosition = true;
					PawnPosition.Location = Location;
					PawnPosition.Rotation = Rotation;
					PawnPosition.Velocity = Velocity;

					DOREP(Pawn,PawnPosition,FCompressedPosition);

					*RecentLocation = Location;
					*RecentRotation = Rotation;
					*RecentVelocity = Velocity;
				}
				else if( (Level->TimeSeconds - LastLocTime > 1.f) && 
						(Physics == PHYS_Walking) ) 
				{
					// force location update every second
					LastLocTime = Level->TimeSeconds;
					bCompressedPosition = true;
					PawnPosition.Location = Location;
					PawnPosition.Rotation = Rotation;

					if( PawnPosition.Velocity == FVector(0.f,0.f,-1.f) )
					{
						PawnPosition.Velocity = FVector(0.f,0.f,-2.f);
					}
					else 
					{
						PawnPosition.Velocity = FVector(0.f,0.f,-1.f);
					}

					DOREP(Pawn,PawnPosition,FCompressedPosition);

					*RecentLocation = Location;
					*RecentRotation = Rotation;
					*RecentVelocity = PawnPosition.Velocity;
				}
			}
			else
			{
				// Just replicate rotation
				DOREP(Actor,Rotation,FRotator);
			}
		}

		if( !bTearOff && !bNetOwner && (Level->TimeSeconds - Channel->LastFullUpdateTime < 0.09f) )
		{
 			DOREP(Pawn,PlayerReplicationInfo,APlayerReplicationInfo*);
			return Ptr;
		}
	}
	Channel->LastFullUpdateTime = Level->TimeSeconds;

	Ptr = Super::GetOptimizedRepList(Defaults,Retire,Ptr,Map,Channel);

	if (bNetDirty)
	{
		//epic moelfke: replicate health/superhealth max
		if (bNetInitial)
		{
			DOREP(Pawn,MaxHealthAmount,INT);
			DOREP(Pawn,SuperHealthAmount,INT);
		}

		DOREP(Pawn,PlayerReplicationInfo,APlayerReplicationInfo*);
		DOREP(Pawn,Health,INT);
		
		//scion superville
		DOREPBOOL(Pawn,bIsStunned);
		DOREPBOOL(Pawn,bStunLocked);
		DOREP(Pawn,StunDuration,FLOAT);

		DOREPBOOL(Pawn,bIsShielded);
		DOREPBOOL(Pawn,bShieldFailed);
		DOREPBOOL(Pawn,bShieldBroken);

		DOREPBOOL(Pawn,bIsLockable);
		DOREPBOOL(Pawn,bUseDesiredPrePivot);
		DOREPBOOL(Pawn,bLockBreakable);
		
		//scion rwd - replicate byte instead of float
		DOREP(Pawn,RepAdrenaline,BYTE);

		GETPROP(Pawn,Controller,AController*,RecentController);
		if ((bNetOwner || RecentController) && (!Controller || Map->CanSerializeObject(Controller)))
		{
			DOREP(Pawn,Controller,AController*);
		}
		if (bNetOwner)
		{
			DOREP(Pawn,GroundSpeed,FLOAT);
			DOREP(Pawn,AirSpeed,FLOAT);
			DOREP(Pawn,AccelRate,FLOAT);
			DOREP(Pawn,JumpZ,FLOAT);
			DOREP(Pawn,AirControl,FLOAT);
			
			//epic superville: replicate scenemanager
			DOREP(Pawn,CurrentSceneManager,ASceneManager*);
			
			//scion superville: Scion replication additions
			DOREP(Pawn,ExplosiveAmmoAmount,FLOAT);
			DOREP(Pawn,EnergyAmmoAmount,FLOAT);
			DOREP(Pawn,StunMeter,FLOAT);
			DOREPBOOL(Pawn,bAdrenalineEnabled);
			DOREPBOOL(Pawn,bDrawAdrenalineOnHUD);
		}
		
		//epic moelfke: replicate if adrenaline enabled
		DOREPBOOL(Pawn,bAdrenalineEnabled);
		DOREPBOOL(Pawn,bDrawAdrenalineOnHUD);
		
		DOREP(Pawn,HitDamageType,UClass*);
		DOREPBOOL(Pawn,bSimulateGravity);

		if (bTearOff)
		{
			DOREP(Pawn,TearOffMomentum,FVector);
		}
	}
	
	if (!bNetOwner)
	{
		//epic superville: Replicate spine pitch to remote clients
		DOREP(Pawn,SpinePitch,BYTE);
	}

	return Ptr;
	unguard;
}

INT* AController::GetOptimizedRepList( BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel )
{
	guard(AController::GetOptimizedRepList);
	Ptr = Super::GetOptimizedRepList(Defaults,Retire,Ptr,Map,Channel);

	if( StaticClass()->ClassFlags & CLASS_NativeReplication )
	{
		if( bNetDirty )
		{
			DOREP(Controller,PlayerReplicationInfo,APlayerReplicationInfo*);
			DOREP(Controller,Pawn,APawn*);

			if( bNetOwner )
			{
				DOREP(Controller,PawnClass,UClass*);
				
				//scion superville: Replication Additions - see script for comments
				DOREP(Controller,Enemy,APawn*);
				DOREP(Controller,Target,AActor*);
				//epic moelfke: replicate infinite ammo flag
				DOREPBOOL(Controller,bInfiniteAmmo);
		
				if (bNetInitial)
				{
					DOREPBOOL(Controller,bLockingAllowed);
				}
			}
		}

		if( bDemoRecording )
		{
			DOREP(Actor,Rotation,FRotator);
			DOREP(Controller,Pawn,APawn*);
			if( RemoteRole==ROLE_AutonomousProxy )
			{
				DOREP(Actor,Location,FVector);
			}
		}
	}

	return Ptr;
	unguard;
}

INT* APlayerController::GetOptimizedRepList( BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel )
{
	guard(APlayerController::GetOptimizedRepList);
	Ptr = Super::GetOptimizedRepList(Defaults,Retire,Ptr,Map,Channel);
	
	if( StaticClass()->ClassFlags & CLASS_NativeReplication )
	{
		if( bNetOwner )
		{
			if( bNetDirty )
			{
				DOREP(PlayerController,CameraType,BYTE);	
		
				//epic superville - replicate camera mode enforcement
				DOREPBOOL(PlayerController,bFirstPersonOnly);
				DOREPBOOL(PlayerController,bThirdPersonOnly);
			}
			
			if( bNetInitial )
			{
				// scion sz - replicate gamepad id request to clients
				DOREP(PlayerController,NetGamepadID,BYTE);
			}
		}
	}

	return Ptr;
	unguard;
}

INT* AMover::GetOptimizedRepList( BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel )
{
	guard(AMover::GetOptimizedRepList);
	Ptr = Super::GetOptimizedRepList(Defaults,Retire,Ptr,Map,Channel);
	
	if( StaticClass()->ClassFlags & CLASS_NativeReplication )
	{
		if( bNetDirty )
		{
			DOREP(Mover,SimOldPos,FVector);
			DOREP(Mover,SimOldRotPitch,INT);
			DOREP(Mover,SimOldRotYaw,INT);
			DOREP(Mover,SimOldRotRoll,INT);
			DOREP(Mover,SimInterpolate,FVector);
			DOREP(Mover,RealPosition,FVector);
			DOREP(Mover,RealRotation,FRotator);
		}

		DOREP(Actor,Velocity,FVector);
	}

	return Ptr;
	unguard;
}

INT* APhysicsVolume::GetOptimizedRepList( BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel )
{
	guard(APhysicsVolume::GetOptimizedRepList);

	Ptr = Super::GetOptimizedRepList(Defaults,Retire,Ptr,Map,Channel);

	if( StaticClass()->ClassFlags & CLASS_NativeReplication )
	{
		if( !bSkipActorPropertyReplication || bNetInitial )
		{
			DOREP(Actor,Location,FVector);
			DOREP(Actor,Rotation,FRotator);
			DOREP(Actor,Base,AActor*);
			if( Base && !Base->bWorldGeometry )
			{
				DOREP(Actor,RelativeLocation,FVector);
				DOREP(Actor,RelativeRotation,FRotator);
				DOREP(Actor,AttachmentBone,FName);
			}
		}

		if ( bNetDirty )
		{
			DOREP(PhysicsVolume,Gravity,FVector);
		}
	}
	
	return Ptr;
	unguard;
}

INT* ALevelInfo::GetOptimizedRepList( BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel )
{
	guard(ALevelInfo::GetOptimizedRepList);
	// only replicate needed actor properties
	Ptr = Super::GetOptimizedRepList(Defaults,Retire,Ptr,Map,Channel);
	
	if( StaticClass()->ClassFlags & CLASS_NativeReplication )
	{
		if( bNetDirty )
		{
			DOREP(LevelInfo,Pauser,APlayerReplicationInfo*);
			DOREP(LevelInfo,TimeDilation,FLOAT);
			DOREP(LevelInfo,DefaultGravity,FLOAT);
			//scion rwd - replicate gri
			DOREP(LevelInfo,GRI,AGameReplicationInfo*);
		}
		
		if( bNetInitial )
		{
			// scion sz - 
			// replicate the server detail level to clients
			DOREP(LevelInfo,DetailMode,BYTE);
		}
	}

	return Ptr;
	unguard;
}

void AReplicationInfo::CreateRecentMap(TMap<_WORD, TArray<BYTE>*>& RecentMap)
{
	guard(AReplicationInfo::CreateRecentMap);

	if (!bSkipActorPropertyReplication)
	{
		Super::CreateRecentMap(RecentMap);
	}
	else
	{
		UObject* Default = StaticConstructObject(GetClass());

		static UProperty* OwnerProperty = FindField<UProperty>(GetClass(), TEXT("Owner"));
		checkSlow(OwnerProperty);
		AddToRecentMap(OwnerProperty, (BYTE*)Default, RecentMap);

		// iterate through all our properties, stopping when we get to Actor's because we already added the Actor properties we care about
		for (UProperty* It = GetClass()->PropertyLink; It && It->GetOuter() != AActor::StaticClass(); It = It->PropertyLinkNext)
		{
			AddToRecentMap(It, (BYTE*)Default, RecentMap);
		}

		delete Default;
	}

	unguard;
}

INT* AReplicationInfo::GetOptimizedRepList( BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel )
{
	guard(AReplicationInfo::GetOptimizedRepList);
	if ( !bSkipActorPropertyReplication )
	{
		Ptr = Super::GetOptimizedRepList(Defaults,Retire,Ptr,Map,Channel);
	}
	return Ptr;
	unguard;
}

INT* APlayerReplicationInfo::GetOptimizedRepList( BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel )
{
	guard(APlayerReplicationInfo::GetOptimizedRepList);
	Ptr = Super::GetOptimizedRepList(Defaults,Retire,Ptr,Map,Channel);

	if( StaticClass()->ClassFlags & CLASS_NativeReplication )
	{
		if( bNetDirty )
		{
			DOREP(PlayerReplicationInfo,Score,FLOAT);
			DOREP(PlayerReplicationInfo,Deaths,FLOAT);
			DOREPBOOL(PlayerReplicationInfo,bHasFlag);
			DOREP(PlayerReplicationInfo,PlayerSkill,BYTE);
			if( !bNetOwner || bDemoRecording )
			{
				DOREP(PlayerReplicationInfo,Ping,INT);

				//epic rwd - replicate XUID
				DOREPARRAY(PlayerReplicationInfo,XUID,BYTE);

				//epic bcl -- replicate the gamertag
				DOREP(PlayerReplicationInfo,GamerTag,FStringNoInit);
			}

 			DOREP(PlayerReplicationInfo,PlayerVolume,AVolume*);
			DOREP(PlayerReplicationInfo,PlayerZone,AZoneInfo*);
			DOREP(PlayerReplicationInfo,TeamSwitches,INT);
			DOREP(PlayerReplicationInfo,PlayerID,INT);
			DOREP(PlayerReplicationInfo,PlayerName,FStringNoInit);
			DOREP(PlayerReplicationInfo,Team,ATeamInfo*);
			DOREP(PlayerReplicationInfo,TeamID,INT);
			DOREP(PlayerReplicationInfo,VoiceType,UClass*);
			DOREPBOOL(PlayerReplicationInfo,bIsFemale);
			DOREPBOOL(PlayerReplicationInfo,bIsSpectator);
			DOREPBOOL(PlayerReplicationInfo,bOnlySpectator);
			DOREPBOOL(PlayerReplicationInfo,bWaitingPlayer);
			DOREPBOOL(PlayerReplicationInfo,bDead);
			DOREPBOOL(PlayerReplicationInfo,bReadyToPlay);
			DOREPBOOL(PlayerReplicationInfo,bOutOfLives);
			DOREP(PlayerReplicationInfo,replicatedLocation,FVector);
			DOREP(PlayerReplicationInfo,replicatedRotation,FRotator);

			DOREP(PlayerReplicationInfo,Handicap,FLOAT);

			if ( bNetInitial )
			{
				DOREPBOOL(PlayerReplicationInfo,bBot);
				DOREP(PlayerReplicationInfo,StartTime,FLOAT);
				DOREPBOOL(PlayerReplicationInfo,bDedicatedServerSpectator);
				//epic moelfke: replicate ExtraPRI (hook for mutators to add extra info)
				DOREP(PlayerReplicationInfo,ExtraPRI,AExtraPlayerReplicationInfo*);

				DOREP(PlayerReplicationInfo,EnergyWeaponClassName,FStringNoInit);
				DOREP(PlayerReplicationInfo,ExplosiveWeaponClassName,FStringNoInit);

				if( bNetOwner )
				{
					DOREP(Actor,Owner,AActor*);
				}
			}

			//scion rwd - added numlives
			DOREP(PlayerReplicationInfo,NumLives,INT);
			//scion rwd - replicate pawn class
			DOREP(PlayerReplicationInfo,PawnClass,UClass*);
			//epic moelfke: replicate if player is done loading
			DOREPBOOL(PlayerReplicationInfo,bDoneLoading);

			//epic moelfke: replicate respawn delay
			if (bNetOwner)
			{
				DOREP(PlayerReplicationInfo,RespawnDelay,BYTE);
			}
		}
	}
	
	return Ptr;
	unguard;
}

//epic moelfke
INT* AExtraPlayerReplicationInfo::GetOptimizedRepList(BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel)
{
	guard(AExtraPlayerReplicationInfo::GetOptimizedRepList);

	Ptr = Super::GetOptimizedRepList(Defaults, Retire, Ptr, Map, Channel);

	if (StaticClass()->ClassFlags & CLASS_NativeReplication)
	{
		if( bNetInitial )
		{
			DOREP(ExtraPlayerReplicationInfo,NextExtraPRI,AExtraPlayerReplicationInfo*);

			if( bNetOwner )
			{
				DOREP(Actor,Owner,AActor*);
			}
		}
	}

	return Ptr;

	unguard;
}

INT* AGameReplicationInfo::GetOptimizedRepList( BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel )
{
	guard(AGameReplicationInfo::GetOptimizedRepList);
	Ptr = Super::GetOptimizedRepList(Defaults,Retire,Ptr,Map,Channel);

	if( StaticClass()->ClassFlags & CLASS_NativeReplication )
	{
		if( bNetDirty )
		{
			DOREP(GameReplicationInfo,RemainingMinute,INT);
			DOREPBOOL(GameReplicationInfo,bStopCountDown);
			DOREPBOOL(GameReplicationInfo,bMatchHasBegun);
			//scion rwd - bStartedCountDown loving
			DOREPBOOL(GameReplicationInfo,bStartedCountDown);
			//epic bcl -- promoted from UC1 so game types can set the join/invite status
			DOREPBOOL(GameReplicationInfo,bJoinable);
			DOREPBOOL(GameReplicationInfo,bInvitable);
			DOREP(GameReplicationInfo,Winner,AActor*);
			DOREP(GameReplicationInfo,MatchID,INT);

			DOREPARRAY(GameReplicationInfo,CarriedObjectState,BYTE);
			DOREPARRAY(GameReplicationInfo,Teams,ATeamInfo*);

			//scion rwd - game ended rep
			DOREPBOOL(GameReplicationInfo,bGameEnded);

			if ( !bMatchHasBegun )
			{
				DOREPBOOL(GameReplicationInfo,bMatchFull);
			}

			if( bNetInitial )
			{
				DOREP(GameReplicationInfo,GameClass,UClass*);
				DOREPBOOL(GameReplicationInfo,bTeamGame);
				//epic moelfke: no longer send RemainingTime - see PlayerController.uc, function ServerSendRemainingTime()
				DOREP(GameReplicationInfo,ElapsedTime,INT);
				DOREP(GameReplicationInfo,ServerName,FStringNoInit);
				DOREP(GameReplicationInfo,ShortName,FStringNoInit);
				DOREP(GameReplicationInfo,AdminName,FStringNoInit);
				DOREP(GameReplicationInfo,AdminEmail,FStringNoInit);
				DOREP(GameReplicationInfo,ServerRegion,INT);
				DOREP(GameReplicationInfo,MOTDLine1,FStringNoInit);
				DOREP(GameReplicationInfo,MOTDLine2,FStringNoInit);
				DOREP(GameReplicationInfo,MOTDLine3,FStringNoInit);
				DOREP(GameReplicationInfo,MOTDLine4,FStringNoInit);
				DOREP(GameReplicationInfo,GoalScore,INT);
				DOREP(GameReplicationInfo,TimeLimit,INT);
				DOREP(GameReplicationInfo,MaxLives,INT);
				//epic moelfke: replicate bDedicatedServer for xbox dedicated server support
				DOREPBOOL(GameReplicationInfo,bDedicatedServer);
				DOREPBOOL(GameReplicationInfo,bAllowHandicap);
				DOREPBOOL(GameReplicationInfo,bHasRaceWeapons);
				DOREPBOOL(GameReplicationInfo,bHasMeleeWeapons);
			}
		}
	}

	return Ptr;
	unguard;
}
INT* ATeamInfo::GetOptimizedRepList( BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel )
{
	guard(ATeamInfo::GetOptimizedRepList);
	Ptr = Super::GetOptimizedRepList(Defaults,Retire,Ptr,Map,Channel);

	if( StaticClass()->ClassFlags & CLASS_NativeReplication )
	{
		if( bNetDirty )
		{
			DOREP(TeamInfo,Score,FLOAT);
			DOREP(TeamInfo,HomeBase,AActor*);
		}

		if( bNetInitial )
		{
			DOREP(TeamInfo,TeamName,FStringNoInit);
			DOREP(TeamInfo,TeamIndex,INT);
			DOREP(TeamInfo,TeamIcon,UMaterial*);
		}
	}

	return Ptr;
	unguard;
}

/* epic ===============================================
* ::execSetOnlyReplicateHidden
* Author: moelfke
*
* Sets the value of bOnlyReplicateHidden. This accessor
* is necessary because when it is true, the replication
* code does not set up recent data for most Actor
* replicated properties. This means it is only safe to
* change this value on the tick the Pickup was spawned;
* setting it after could result in attempting to
* replicate properties that there is no recent data for.
*
* =====================================================
*/
void APickup::execSetOnlyReplicateHidden(FFrame& Stack, RESULT_DECL)
{
	guard(APickup::execSetOnlyReplicateHidden);

	P_GET_UBOOL(bNewValue);
	P_FINISH;

	if (SpawnedTime == Level->TimeSeconds)
	{
		bOnlyReplicateHidden = bNewValue;
	}
	else
	{
		debugf(NAME_Warning, TEXT("Call to Pickup::SetOnlyReplicateHidden() on %s had no effect because at least one tick has passed since the Pickup was spawned!"), GetName());
	}

	unguardexec;
}

void APickup::CreateRecentMap(TMap<_WORD, TArray<BYTE>*>& RecentMap)
{
	guard(APickup::CreateRecentMap);

	if (!bOnlyReplicateHidden)
	{
		Super::CreateRecentMap(RecentMap);
	}
	else
	{
		UObject* Default = StaticConstructObject(GetClass());

		// add bHidden, Location and Rotation, the only Actor properties that will be replicated
		static UProperty* ActorReplicatedProps[2] = {	FindField<UProperty>(GetClass(), TEXT("bHidden")),
														FindField<UProperty>(GetClass(), TEXT("Rotation"))	};
		for (INT i = 0; i < ARRAY_COUNT(ActorReplicatedProps); i++)
		{
			checkSlow(ActorReplicatedProps[i]);
			AddToRecentMap(ActorReplicatedProps[i], (BYTE*)Default, RecentMap);
		}

		// iterate through all our properties, stopping when we get to Actor's because we already added the Actor properties we care about
		for (UProperty* It = GetClass()->PropertyLink; It && It->GetOuter() != AActor::StaticClass(); It = It->PropertyLinkNext)
		{
			AddToRecentMap(It, (BYTE*)Default, RecentMap);
		}

		delete Default;
	}

	unguard;
}

INT* APickup::GetOptimizedRepList( BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel )
{
	guard(APickup::GetOptimizedRepList);

	if( StaticClass()->ClassFlags & CLASS_NativeReplication )
	{
		if( bOnlyReplicateHidden )
		{
			DOREPBOOL(Actor,bHidden);
			if( bNetInitial )
			{
				DOREP(Actor,Rotation,FRotator);
			}
		}
		else 
		{
			if( bNetOwner && !bSkipActorPropertyReplication && bNetDirty)
			{
				DOREP(Pickup,Inventory,AInventory*);
			}
			Ptr = Super::GetOptimizedRepList(Defaults,Retire,Ptr,Map,Channel);
		}
	}

	return Ptr;

	unguard;
}

INT* AInventory::GetOptimizedRepList( BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel )
{
	guard(AInventory::GetOptimizedRepList);

	Ptr = Super::GetOptimizedRepList(Defaults,Retire,Ptr,Map,Channel);

	if( StaticClass()->ClassFlags & CLASS_NativeReplication )
	{
		if( bNetOwner && !bSkipActorPropertyReplication && bNetDirty)
		{
			DOREP(Inventory,Inventory,AInventory*);
		}
		if( bNetOwner && bNetDirty )
		{
			DOREP(Inventory,ThirdPersonActor,AActor*);
			//scion superville: Replicate dual wpn attachment
			DOREP(Inventory,ThirdPersonActorDual,AActor*);
			DOREP(Inventory,Charge,INT);			
		}
	}

	return Ptr;
	unguard;
}

INT* AWeapon::GetOptimizedRepList( BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel )
{
	guard(AWeapon::GetOptimizedRepList);

	Ptr = Super::GetOptimizedRepList(Defaults,Retire,Ptr,Map,Channel);

	if( StaticClass()->ClassFlags & CLASS_NativeReplication )
	{
		if ( bNetOwner && bNetDirty )
		{
			DOREP(Weapon,AmmoAmount,FLOAT);
		}
	}

	return Ptr;
	unguard;
}

INT* ACarriedObject::GetOptimizedRepList( BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel )
{
	guard(ACarriedObject::GetOptimizedRepList);

	Ptr = Super::GetOptimizedRepList(Defaults,Retire,Ptr,Map,Channel);

	if( StaticClass()->ClassFlags & CLASS_NativeReplication )
	{
		if( bNetDirty )
		{
			DOREP(CarriedObject,Holder,APawn*);
			DOREPBOOL(CarriedObject,bHome);
			DOREPBOOL(CarriedObject,bHeld);
			DOREP(CarriedObject,HolderPRI,APlayerReplicationInfo*);
		}
	}

	return Ptr;
	unguard;
}

INT* AWeaponAttachment::GetOptimizedRepList( BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel )
{
	guard(AWeaponAttachment::GetOptimizedRepList);

	Ptr = Super::GetOptimizedRepList(Defaults,Retire,Ptr,Map,Channel);

	if( StaticClass()->ClassFlags & CLASS_NativeReplication )
	{
		if( bNetDirty ) 
		{
			if( bReplicateToAll	||
				!bNetOwner		||	
				bDemoRecording	||
				bRepClientDemo	)
			{
				DOREP(WeaponAttachment,FlashCount,BYTE);
				DOREP(WeaponAttachment,FiringMode,FName);

				//scion superville: Set when DownWeapon state is called
				DOREPBOOL(WeaponAttachment,bPendingDestroy);
			}
		}
	}

	return Ptr;
	unguard;
}

INT* AAmmunition::GetOptimizedRepList( BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel )
{
	guard(AAmmunition::GetOptimizedRepList);
		
	Ptr = Super::GetOptimizedRepList(Defaults,Retire,Ptr,Map,Channel);

	if( StaticClass()->ClassFlags & CLASS_NativeReplication )
	{
		if( bNetOwner && bNetDirty )
		{
			DOREP(Ammunition,AmmoAmount,FLOAT);
		}
	}

	return Ptr;
	unguard;
}

UBOOL AActor::IsAnimating(int Channel) const
{
	if( IsDTMesh() && Mesh )
	{
		Mesh->MeshGetInstance(this);
		return MeshInstance->IsAnimating( Channel );
	}
	return false;
}

void AActor::UpdateAnimation(FLOAT DeltaSeconds)
{
	guardSlow(AActor::UpdateAnimation);

	if( bMirrorAnimFromOwner )
		return;

	if( Mesh )	
		Mesh->MeshGetInstance(this)->UpdateAnimation(DeltaSeconds);

	unguardSlow;
}

void AActor::NotifyAnimEnd( int Channel )
{
	eventAnimEnd(Channel);
}

#ifdef WITH_LIPSINC
void AActor::NotifyLIPSincAnimEnd()
{
	eventLIPSincAnimEnd();
}
#endif

void AActor::execSuggestFallVelocity( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSuggestFallVelocity);

	P_GET_VECTOR(Destination);
	P_GET_VECTOR(Start);
	P_GET_FLOAT(MaxZ);
	P_GET_FLOAT(MaxXYSpeed);
	P_GET_FLOAT_OPTX(GravScale, GravityScale);
	P_FINISH;

	*(FVector*)Result = SuggestFallVelocity(Destination, Start, MaxXYSpeed, 0.f, MaxZ, MaxXYSpeed, GravScale);
	unguardSlow;
}

/*-----------------------------------------------------------------------------
	AActor networking implementation.
-----------------------------------------------------------------------------*/

//
// Static variables for networking.
//
static FVector   SavedLocation;
static FVector   SavedRelativeLocation;
static FRotator  SavedRotation;
static FRotator  SavedRelativeRotation;
static AActor*   SavedBase;
static DWORD     SavedCollision;
static FLOAT	 SavedRadius;
static FLOAT     SavedHeight;
static FVector	 SavedSimInterpolate;
static FLOAT	 SavedDrawScale;
static FVector	 SavedDrawScale3D;
static BYTE		 SavedDrawType;
static FCompressedPosition SavedPawnPosition;
static FLOAT	SavedGravity;
static UBOOL	SavedHardAttach;
static AActor*	SavedTeam;
static FColor	SavedTeamColor;
static INT		SavedEnergyAmmoAmount;
static INT		SavedExplosiveAmmoAmount;
static INT		SavedFlashCount;
static UBOOL	SavedPendingDestroy;
static BYTE		SavedPhysics;
//epic moelfke: for notifying Controller when items are added to its Pawn's Inventory chain
static AInventory* SavedInventory;
//epic moelfke: for notifying subsystems when game becomes paused/unpaused
static APlayerReplicationInfo* SavedPauser;
//epic moelfke: for notifying PlayerReplicationInfo when PawnClass changes
static UClass* SavedPawnClass;

//epic superville: For failing shield function calls
static UBOOL	SavedShieldFailed;
static UBOOL	SavedShieldBroken;

//epic moelfke: note when Pawn's PlayerReplicationInfo is received so we can update for its team (if any)
static APlayerReplicationInfo* SavedPlayerReplicationInfo;

//
// Skins.
//
UMaterial* AActor::GetSkin( INT Index )
{
	if( Index < Skins.Num() )
		return Skins(Index);
	return NULL;
}

//
// Net priority.
//
FLOAT AActor::GetNetPriority( FVector& ViewPos, FVector& ViewDir, AActor* Sent, FLOAT Time, FLOAT Lag )
{
	guardSlow(AActor::GetNetPriority);
	if ( bAlwaysRelevant )
		return NetPriority * Time * ::Min(NetUpdateFrequency * 0.1f, 1.f);
	else
		return NetPriority * Time;
	unguardSlow;
}

//
// Always called immediately before properties are received from the remote.
//
void AActor::PreNetReceive()
{
	guard(AActor::PreNetReceive);
	SavedLocation   = Location;
	SavedRotation   = Rotation;
	SavedRelativeLocation = RelativeLocation;
	SavedRelativeRotation = RelativeRotation;
	SavedBase       = Base;
	SavedCollision  = bCollideActors;
	SavedRadius		= CollisionRadius;
	SavedHeight     = CollisionHeight;
	SavedDrawScale	= DrawScale;
	SavedDrawScale3D= DrawScale3D;
	SavedDrawType = DrawType;
	SavedPhysics = Physics;
	if( bCollideActors )
		GetLevel()->Hash->RemoveActor( this );
	unguard;
}

void APawn::PreNetReceive()
{
	guard(AActor::PreNetReceive);
	//epic moelfke: for notifying Controller when items are added to its Pawn's Inventory chain
	SavedInventory = Inventory;

	SavedPawnPosition = PawnPosition;

	//scion superville: Notify when ammo amounts have changed
	SavedEnergyAmmoAmount		= EnergyAmmoAmount;
	SavedExplosiveAmmoAmount	= ExplosiveAmmoAmount;

	//epic superville: Notify when shield has failed or been broken
	SavedShieldFailed	= bShieldFailed;
	SavedShieldBroken	= bShieldBroken;

	//epic moelfke: note when Pawn's PlayerReplicationInfo is received so we can update for its team (if any)
	SavedPlayerReplicationInfo = PlayerReplicationInfo;

	AActor::PreNetReceive();
	unguard;
}

void AInventory::PreNetReceive()
{
	guardSlow(AInventory::PreNetReceive);

	Super::PreNetReceive();

	//epic moelfke: for notifying Controller when items are added to its Pawn's Inventory chain
	SavedInventory = Inventory;

	unguardSlow;
}

void AMover::PreNetReceive()
{
	guard(AMover::PreNetReceive);

	SavedSimInterpolate = SimInterpolate;
	AActor::PreNetReceive();
	unguard;
}

void ALevelInfo::PreNetReceive()
{
	guard(ALevelInfo::PreNetReceive);

	SavedGravity = DefaultGravity;
	//epic moelfke: for notifying subsystems when game becomes paused/unpaused
	SavedPauser = Pauser;
	AActor::PreNetReceive();
	unguard;
}

void ALevelInfo::PostNetReceive()
{
	guard(ALevelInfo::PostNetReceive);

	if ( DefaultGravity != SavedGravity )
		GetDefaultPhysicsVolume()->Gravity.Z = DefaultGravity;
	//epic moelfke: notify subsystems when game becomes paused/unpaused
	if (Pauser != SavedPauser)
	{
		NotifyPauseChange(Pauser != NULL);
	}

	// scion sz - 
	// force client detail to synchronize with server detail setting
	if( NetMode == NM_Client )
	{
		// handle client detail level
		eventClientLevelDetailChange();
		// delete any actors marked destroyed
		XLevel->CleanupDestroyed(1);
		// force any that remain to the lowest detail level so
		// that they will show up
		XLevel->ForceDetailLevel( DM_Low );
	}

	AActor::PostNetReceive();
	unguard;
}
//
// Always called immediately after properties are received from the remote.
//
void AActor::PostNetReceive()
{
	guard(AActor::PostNetReceive);

	Exchange ( Location,        SavedLocation  );
	Exchange ( Rotation,        SavedRotation  );
	Exchange ( RelativeLocation,        SavedRelativeLocation  );
	Exchange ( RelativeRotation,        SavedRelativeRotation  );
	Exchange ( Base,            SavedBase      );
	ExchangeB( bCollideActors,  SavedCollision );
	Exchange ( CollisionRadius, SavedRadius    );
	Exchange ( CollisionHeight, SavedHeight    );
	Exchange ( DrawScale, SavedDrawScale       );
	Exchange ( DrawScale3D, SavedDrawScale3D   );
	Exchange ( DrawType, SavedDrawType       );

	if( bCollideActors )
		GetLevel()->Hash->AddActor( this );
	if( CollisionRadius!=SavedRadius || CollisionHeight!=SavedHeight )
		SetCollisionSize( SavedRadius, SavedHeight );
	if( bCollideActors!=SavedCollision )
		SetCollision( SavedCollision, bBlockActors, bBlockPlayers );
	if( Location!=SavedLocation )
		PostNetReceiveLocation();
	if( Rotation!=SavedRotation )
	{
		PostNetReceiveRotation();
	}
	if ( DrawScale!=SavedDrawScale )
		SetDrawScale(SavedDrawScale);
	if ( DrawScale3D!=SavedDrawScale3D )
		SetDrawScale3D(SavedDrawScale3D);
	if ( DrawType != SavedDrawType )
		SetDrawType(EDrawType(SavedDrawType));

	if ( (Physics != SavedPhysics)
		&& ((Physics == PHYS_Karma) || (SavedPhysics == PHYS_Karma) || (Physics == PHYS_KarmaRagDoll) || (SavedPhysics == PHYS_KarmaRagDoll)) )
	{
		Exchange ( Physics, SavedPhysics );
		setPhysics(SavedPhysics);
	}
	UBOOL bBaseChanged = ( Base!=SavedBase );
	if( bBaseChanged )
	{
		// Base changed.
		if( SavedBase )
		{
			eventBump( SavedBase );
			SavedBase->eventBump( this );
		}
		SetBase( SavedBase );
		//epic moelfke: removed setting SavedRelativeLocation/SavedRelativeRotation here if RelativeLocation and RelativeRotation
		//weren't received this tick because it was causing the client to overwrite the properties with values out of sync with the server
	}

	if ( Base && !Base->bWorldGeometry )
	{
		if ( bAlwaysZeroBoneOffset && Base->Mesh && Base->Mesh->IsA(USkeletalMesh::StaticClass()) )
			SavedRelativeLocation = FVector(0.f,0.f,0.f);

		if ( AttachmentBone != NAME_None )
		{
			RelativeLocation = SavedRelativeLocation;
			RelativeRotation = SavedRelativeRotation;
		}
		else if ( !bHardAttach )
		{
			if ( bBaseChanged || (RelativeLocation != SavedRelativeLocation) )
			{
				GetLevel()->FarMoveActor( this, Base->Location + SavedRelativeLocation, 0, 1, 1 );
				RelativeLocation = SavedRelativeLocation;
			}
			if ( bBaseChanged || (RelativeRotation != SavedRelativeRotation) )
			{
				FCheckResult Hit;
				FCoords Coords = GMath.UnitCoords / Base->Rotation;
				FCoords PartCoords = GMath.UnitCoords / SavedRelativeRotation;
				PartCoords = PartCoords * Coords.Transpose();
				FRotator NewRotation = PartCoords.OrthoRotation();
				GetLevel()->MoveActor( this, FVector(0,0,0), NewRotation, Hit, 0, 0, 0, 1 );
			}
		}
	}
	//epic moelfke: keep RelativeLocation and RelativeRotation changes even if no base (perhaps it is on its way down the pipe)
	else
	{
		RelativeLocation = SavedRelativeLocation;
		RelativeRotation = SavedRelativeRotation;
	}

	bJustTeleported = 0;

    if ( bNetNotify )
        eventPostNetReceive();
	unguard;
}

void AActor::PostNetReceiveLocation()
{
	guardSlow(AActor::PostNetReceiveLocation);

	GetLevel()->FarMoveActor( this, SavedLocation, 0, 1, 1 );
	unguardSlow;
}

void AActor::PostNetReceiveRotation()
{
	guardSlow(AActor::PostNetReceiveRotation);

	FCheckResult Hit;
	GetLevel()->MoveActor( this, FVector(0,0,0), SavedRotation, Hit, 0, 0, 0, 1 );

	unguardSlow;
}

void APawn::PostNetReceive()
{
	guardSlow(APawn::PostNetReceive);

	//epic moelfke: notify Controller when items are added to its Pawn's Inventory chain
	if (Inventory != SavedInventory && Inventory != NULL && Controller != NULL)
	{
		Controller->eventNotifyAddInventory(Inventory);
	}

	if ( (SavedPawnPosition.Location != PawnPosition.Location)
		|| (SavedPawnPosition.Rotation != PawnPosition.Rotation)
		|| (SavedPawnPosition.Velocity != PawnPosition.Velocity) )
	{
		Location = PawnPosition.Location;
		Velocity = PawnPosition.Velocity;

		//epic superville: If this is our pawn, just set the rotation
		if( IsSpectatorTarget() )
		{
			// Set desired rotation so that clients can lerp to it
			DesiredRotation = PawnPosition.Rotation.Normalize();
		}
		else
		{
			Rotation = PawnPosition.Rotation;
		}
	}
	AActor::PostNetReceive();
	
	//scion superville: Notify when ammo amounts have changed
	if( SavedEnergyAmmoAmount	 != EnergyAmmoAmount ) {
		eventAmmoCountChanged( SavedEnergyAmmoAmount, EnergyAmmoAmount, (BYTE)AC_Energy );
	}
	if( SavedExplosiveAmmoAmount != ExplosiveAmmoAmount ) {
		eventAmmoCountChanged( SavedExplosiveAmmoAmount, ExplosiveAmmoAmount, (BYTE)AC_Explosive );
	}

	//epic moelfke: note when Pawn's PlayerReplicationInfo is received so we can update for its team (if any)
	if (SavedPlayerReplicationInfo != PlayerReplicationInfo && PlayerReplicationInfo && PlayerReplicationInfo->Team)
	{
		bUpdateAlphaActors = true;
	}

	//epic superville: Notify when shield has failed or broken
	if( SavedShieldFailed != bShieldFailed ||
		SavedShieldBroken != bShieldBroken )
	{
		eventShieldBrokenOrFailed( bShieldBroken, bShieldFailed );
	}

	unguardSlow;
}

void APawn::PostNetReceiveLocation()
{
	guardSlow(APawn::PostNetReceiveLocation);

	//scion jg -- Debug. Make sure there are no divide by zeros going on
	check(Location == Location && Acceleration == Acceleration &&
		Velocity == Velocity);

	if( Role == ROLE_SimulatedProxy )
	{
		FCheckResult Hit(1.f);
		if ( GetLevel()->EncroachingWorldGeometry(Hit,SavedLocation,GetCylinderExtent(),0,Level) )
		{
			if ( CollisionRadius == GetClass()->GetDefaultActor()->CollisionRadius )
				SetCollisionSize(CollisionRadius - 1.f, CollisionHeight - 1.f);
			bSimGravityDisabled = true;
		}
		else if ( Velocity.IsZero() )
			bSimGravityDisabled = true;
		else
		{
			SavedLocation.Z += 2.f;
			bSimGravityDisabled = false;
		}
		FVector OldLocation = Location;
		GetLevel()->FarMoveActor( this, SavedLocation, 0, 1, 1 );
		if ( !bSimGravityDisabled )
	{
		// smooth out movement of other players to account for frame rate induced jitter
		// look at whether location is a reasonable approximation already
		// if so only partially correct
			FVector Dir = OldLocation - Location;
			FLOAT StartError = Dir.Size();
			if ( StartError > 4.f )
		{
				StartError = ::Min(0.5f * StartError,CollisionRadius);
				Dir.Normalize();
				moveSmooth(StartError * Dir);
			}
		}

	}
	else
	{
		AActor::PostNetReceiveLocation();
	}
	unguardSlow;
}

void APawn::PostNetReceiveRotation()
{
	guardSlow(APawn::PostNetReceiveRotation);

	// If not a spectator target, do normal stuff
	if( !IsSpectatorTarget() )
	{
		AActor::PostNetReceiveRotation();
	}

	unguardSlow;
}

void AInventory::PostNetReceive()
{
	guard(AInventory::PostNetReceive);

	//epic moelfke: notify Controller when items are added to its Pawn's Inventory chain
	if (Inventory != SavedInventory && Inventory && Instigator && Instigator->Controller)
	{
		Instigator->Controller->eventNotifyAddInventory(Inventory);
	}

	unguard;
}

void AMover::PostNetReceive()
{
	guard(AMover::PostNetReceive);

	AActor::PostNetReceive();
	if( SavedSimInterpolate != SimInterpolate )
	{
		OldPos = SimOldPos;
		OldRot.Yaw = SimOldRotYaw;
		OldRot.Pitch = SimOldRotPitch;
		OldRot.Roll = SimOldRotRoll;
		PhysAlpha = SimInterpolate.X * 0.01f;
		PhysRate = SimInterpolate.Y * 0.01f;
		INT keynums = (INT) SimInterpolate.Z;
		KeyNum = keynums & 255;
		PrevKeyNum = keynums >> 8;
		setPhysics(PHYS_MovingBrush);
		bInterpolating = true;
	}
	unguard;
}

void APlayerReplicationInfo::PreNetReceive()
{
	Super::PreNetReceive();
	SavedTeam = Team;
	//epic moelfke: for notifying PlayerReplicationInfo when PawnClass changes
	SavedPawnClass = PawnClass;
}

#ifdef _XBOX
#include "UnUIXInteraction.h"
#endif

void APlayerReplicationInfo::PostNetReceive()
{
	guard(APlayerReplicationInfo::PostNetReceive);

	//scion rwd - handle new team
	if (Team != SavedTeam &&
		Team != NULL &&
		Owner != NULL)
	{
		((AController*)Owner)->eventReceivedTeam();
	}

	//epic moelfke: notify when PawnClass changes
	if (PawnClass != SavedPawnClass)
	{
		eventPawnClassChanged();
	}

	AActor::PostNetReceive();
	if ( Level->NetMode == NM_Client )
	{
		if( GetLevel()->NetDriver &&
			GetLevel()->Engine->Client->Viewports(0)->Actor->PlayerReplicationInfo == this )
			Ping -= (INT) ((GetLevel()->NetDriver->ServerConnection->AverageFrameTime * 1000) / 2);
		if( Ping < 0 )
			Ping = 0;

		if ( PreviousName != PlayerName )
		{
			OldName = PreviousName;
			PreviousName = PlayerName;
			eventClientNameChange();
		}
	}
#ifdef _XBOX
	UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
	if ( UIX )
	{
		UBOOL bShouldRegisterPRI = UIX->SignedIn() && !bBot;
		if ( bShouldRegisterPRI )
		{
			BYTE Zero[12];
			appMemzero(&Zero,12);

			UBOOL bHasXUID = appMemcmp(&XUID, &Zero, 12);

			// this player logged out of xbox live, remove the player from the
			// UIX player list.
			if ( bRegisteredUIX && !bHasXUID )
			{
				UIX->UnregisterPRI(this);
			}

			// the XUID just replicated to us, or the player just logged [again] 
			// - register this player in the UIX player list
			else if ( !bRegisteredUIX && bHasXUID )
			{
				UIX->RegisterPRI(this);
			}
		}
	}
#endif

	unguard;
}

void APlayerReplicationInfo::Destroy()
{
	guard(APlayerReplicationInfo::Destroy);

#if _XBOX
	if ( bRegisteredUIX )
	{
		UUIXInteraction* UIX = UUIXInteraction::getUUIXInteraction();
		if ( UIX )
			UIX->UnregisterPRI(this);
	}
#endif

	AActor::Destroy();
	unguard;
}

void AWeaponAttachment::PreNetReceive()
{
	guard(AWeaponAttachment::PreNetReceive);

	AActor::PreNetReceive();

	SavedFlashCount		= FlashCount;

	//scion superville: Save pending destroy flag to see if it has changed
	SavedPendingDestroy = bPendingDestroy;

	unguard;
}

void AWeaponAttachment::PostNetReceive()
{
	guard(AWeaponAttachment::PostNetReceive);


	// If FlashCount changes, or bAutoFire changes state,
	// Trigger the ThirdPersonEffects

	AActor::PostNetReceive();

	if( Instigator				&&
		Instigator == Base		&&
		!Instigator->bDeleteMe	&&
		(!Instigator->IsLocallyControlled() || bReplicateToAll ||
			(Instigator->Controller && Instigator->Controller->bDemoOwner)) )
	{
		//scion superville: just store values and check instead of using Instigator->FlashCount
		//					(that method didn't work well with dual pistols who both changed flash count seperately)
		if (!bPendingDestroy &&
			!bDeleteMe)
		{
			if( SavedFlashCount != FlashCount )
			{
				eventThirdPersonEffects();
			}
		}

		//scion superville: Inform client of pending destroy
		if( SavedPendingDestroy != bPendingDestroy )
		{
			eventPendingDestroy();
		}
	}

	unguard;
}

/*-----------------------------------------------------------------------------
	APlayerPawn implementation.
-----------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------------
	AActor.
-----------------------------------------------------------------------------*/

void AActor::Destroy()
{
	guard(AActor::Destroy);

	INT i;
	while( (i=Projectors.Num()) > 0)
	{
		Projectors(i-1)->RemoveReference();
		Projectors.Remove(i-1);		
	}
		
	for(INT ProjectorIndex = 0;ProjectorIndex < StaticMeshProjectors.Num();ProjectorIndex++)
	{
		StaticMeshProjectors(ProjectorIndex)->RenderInfo->RemoveReference();
		StaticMeshProjectors(ProjectorIndex) = StaticMeshProjectors(ProjectorIndex)->RemoveRef();
		check( StaticMeshProjectors(ProjectorIndex) == NULL );
	}

	StaticMeshProjectors.Empty();

	if(LightRenderData)
	{
		delete LightRenderData;
		LightRenderData = NULL;
	}

	if(ActorRenderData)
	{
		delete ActorRenderData;
		ActorRenderData = NULL;
	}

#ifdef WITH_KARMA
    KTermActorKarma(this);
	if(XLevel)
		check(XLevel->KContactGenActors.FindItemIndex(this) == INDEX_NONE);

	// If this is not a garbage-collection destroy, manually destroy the KParams.
	if( KParams && !GIsGarbageCollecting )
	{
		delete KParams;
		KParams = NULL;
	}
#endif

    if( this->IsInOctree() )
	{
		if(XLevel && XLevel->Hash)
			XLevel->Hash->RemoveActor(this);
		else
			debugf( TEXT("Destroying Actor (%s) that is in Octree - but can't find Octree."), this->GetName() );
	}

	// scion sz - if this actor is static then make sure to remove it from the level
	if (bStatic && XLevel)
	{
		for( INT LeafIdx=0; LeafIdx < XLevel->LeafRenderInfo.Num(); LeafIdx++ )
		{
			// each leaf has a list of static actors to render
			FLeafRenderInfo& LeafRenderInfo = XLevel->LeafRenderInfo( LeafIdx );
			// see if this actor is in the list of static actors
			INT FoundIdx = LeafRenderInfo.RenderActors.FindItemIndex( this );
			if( FoundIdx != INDEX_NONE )
			{
				// remove the entry for this actor
				LeafRenderInfo.RenderActors.Remove( FoundIdx );
			}
		}
	}

	UObject::Destroy();

	unguard;
}

void AActor::PostLoad()
{
	guard(AActor::PostLoad);
	Super::PostLoad();

	if( GetClass()->ClassFlags & CLASS_Localized )
		LoadLocalized();
	if( Brush )
		Brush->SetFlags( RF_Transactional );
	if( Brush && Brush->Polys )
		Brush->Polys->SetFlags( RF_Transactional );

	// check for empty Attached entries
	for ( INT i=0; i<Attached.Num(); i++ )
		if ( (Attached(i) == NULL) || (Attached(i)->Base != this) )
		{
			Attached.Remove(i);
			i--;
		}
	unguard;
}

void AActor::ProcessEvent( UFunction* Function, void* Parms, void* Result )
{
	guardSlow(AActor::ProcessEvent);
	if( Level->bBegunPlay && !GIsGarbageCollecting )
		Super::ProcessEvent( Function, Parms, Result );
	unguardSlow;
}

void AActor::PostEditChange()
{
	guard(AActor::PostEditChange);
	Super::PostEditChange();

	for ( INT i=0; i<Attached.Num(); i++ )
		if ( Attached(i) == NULL )
		{
			Attached.Remove(i);
			i--;
		}
	if( GIsEditor )
		bLightChanged = 1;

	ClearRenderData();
	unguard;
}


// Called before we have done undo things.
void AActor::PreEditUndo()
{
	guard(AActor::PreEditUndo);

	// Before we do undo things to the actor, we remove it from the octree.

	// Don't bother doing anything if we dont have collision on.
	if(bCollideActors)
	{
		ULevel* level = GetLevel();
		if( !level || !level->Hash )
			return;

		level->Hash->RemoveActor(this);
	}

	unguard;
}

// Called after we have done undo things.
void AActor::PostEditUndo()
{
	guard(AActor::PostEditUndo);

	// After we have done undo things to the actor, we put it back in to the octree.
	// The undo stuff may have messed with the actors octree info, but we know for sure
	// it isn't in there because we removed it in PreEditUndo, so we reset it here.
	ClearOctreeData();

	if(bCollideActors)
	{
		ULevel* level = GetLevel();
		if( !level || !level->Hash )
			return;

		level->Hash->AddActor(this);
	}

	unguard;
}

// WARNING: This does NOT remove the Actor from the Octree. Almost always you should use Hash->RemoveActor.
// This is just to re-zero the data if you are sure the actor is not in the octree.
void AActor::ClearOctreeData()
{
	guard(AActor::ClearOctreeData);

	OctreeNodes.Empty();

	unguard;
}


//
// Set the actor's collision properties.
//
void AActor::SetCollision
(
	UBOOL NewCollideActors,
	UBOOL NewBlockActors,
	UBOOL NewBlockPlayers
)
{
	guard(AActor::SetCollision);

	//scion jg -- Bail if nothing has changed
	if (bCollideActors == NewCollideActors && bBlockActors == NewBlockActors &&
		bBlockPlayers == NewBlockPlayers)
	{
		return;
	}

	UBOOL OldCollideActors = bCollideActors;

	//scion jg -- We need to decrement the base's collidable attachment
	// count if we were in there and are no longer collidable. If the
	// converse is true, then we need to increment the counter
	if (Base != NULL)
	{
		if (bCollideActors == 1 && bCollideWorld == 0 &&
			NewCollideActors == 0)
		{
			// Decrement the count
			if (Base->NumAttachedWithCollision > 0)
			{
				Base->NumAttachedWithCollision--;
			}
		}
		else if (bCollideActors == 0 && NewCollideActors == 1)
		{
			// Increment the count
			checkSlow(Base->NumAttachedWithCollision < 255);
			Base->NumAttachedWithCollision++;
		}
	}

	// Remove this actor from the hash.
	if( bCollideActors && GetLevel()->Hash )
		GetLevel()->Hash->RemoveActor( this );

	// Untouch everything if we're turning collision off.
	if( bCollideActors && !NewCollideActors )
	{
		for( int i=0; i<Touching.Num(); )
		{
			if( Touching(i) )
				Touching(i)->EndTouch( this, 0 );
			else
				i++;
		}
	}

	// Set properties.
	bCollideActors = NewCollideActors;
	bBlockActors   = NewBlockActors;
	bBlockPlayers  = NewBlockPlayers;

	// Add this actor to the hash.
	if( bCollideActors && GetLevel()->Hash )
		GetLevel()->Hash->AddActor( this );

	// Touch.
	if( NewCollideActors && !OldCollideActors )
	{
		//epic moelfke: keep a TArray of previously iterated actors
		//necessary because ActorEncroachmentCheck() is bugged and can return the same actor twice
		//BeginTouch() handles that case (Actor already in Touching array), however, if an eventTouch()
		//call results in the actor being removed from the Touching array (MoveActor(), another SetCollision(), etc),
		//that check will fail and an Actor could get multiple touches in this one loop.
		TArray<AActor*> AlreadyTouched;
		FMemMark Mark(GMem);
		FCheckResult* FirstHit = GetLevel()->Hash ? GetLevel()->Hash->ActorEncroachmentCheck( GMem, this, Location, Rotation, TRACE_AllColliding, 0 ) : NULL;	
		for( FCheckResult* Test = FirstHit; Test; Test=Test->GetNext() )
		{
			if(	Test->Actor!=this &&
				!Test->Actor->IsJoinedTo(this) &&
				Test->Actor != Level )
			{
				if (!IsBlockedBy(Test->Actor) && AlreadyTouched.FindItemIndex(Test->Actor) == INDEX_NONE)
				{
					AlreadyTouched.AddItem(Test->Actor);
					BeginTouch( Test->Actor );
				}
			}
		}
		Mark.Pop();
	}

	bNetDirty = true; //for network replication

#ifdef WITH_KARMA
	if(bBlockKarma && !GIsEditor) // Never generate contacts in the editor
	{
		// If turning on bCollideActors for an actor with bBlockKarma true and in a Karma physics mode,
		// add it to the KActorContactGen list.
		if(bCollideActors && !OldCollideActors && (Physics == PHYS_KarmaRagDoll || Physics == PHYS_Karma) )
			KActorContactGen(this, 1);
		// If turning off bCollideActors, ensure it is no longer in the contact gen list, and remove any existing pairs.
		else if(!bCollideActors && OldCollideActors)
		{
			KActorContactGen(this, 0);
			KGoodbyeActorAffectedPairs(this);
		}
	}
#endif

	unguard;
}

//
// Set collision size.
//
void AActor::SetCollisionSize( FLOAT NewRadius, FLOAT NewHeight )
{
	guard(AActor::SetCollisionSize);

	// Untouch this actor.
	if( bCollideActors && GetLevel() && GetLevel()->Hash )
		GetLevel()->Hash->RemoveActor( this );

	// Set properties.
	CollisionRadius = NewRadius;
	CollisionHeight = NewHeight;

	// Touch this actor.
	if( bCollideActors && GetLevel() && GetLevel()->Hash )
		GetLevel()->Hash->AddActor( this );

	/* FIXME - commented out for UT2003, until I have time to get it in w/ no issues
	// check that touching array is correct.
	if ( !bTest )
		GetLevel()->CheckEncroachment(this,Location,Rotation,true);
	*/
	bNetDirty = true;	// for network replication
	unguard;
}

void AActor::SetDrawScale( FLOAT NewScale )
{
	guard(AActor::SetDrawScale);

	if( NewScale == DrawScale )
		return;

	if( bCollideActors && GetLevel()->Hash )
		GetLevel()->Hash->RemoveActor( this );

	DrawScale = NewScale;

	if( bCollideActors && GetLevel()->Hash )
		GetLevel()->Hash->AddActor( this );
	bNetDirty = true;	// for network replication
	ClearRenderData();

	eventDrawScaleChanged();

	unguard;
}

void AActor::SetStaticMesh( UStaticMesh* NewStaticMesh)
{
	guard(AActor::SetStaticMesh);

	if ( StaticMesh == NewStaticMesh )
		return;
	if( bCollideActors && GetLevel()->Hash )
		GetLevel()->Hash->RemoveActor( this );

	StaticMesh = NewStaticMesh;

	if( bCollideActors && GetLevel()->Hash )
		GetLevel()->Hash->AddActor( this );

	bNetDirty = true;
	ClearRenderData();

	// scion sz - force update of sorting flags
	bSkinsChanged=1;

	unguard;
}

void AActor::SetDrawType( EDrawType NewDrawType  )
{
	guard(AActor::SetDrawScale);

	if( bCollideActors && GetLevel()->Hash )
		GetLevel()->Hash->RemoveActor( this );

	DrawType = NewDrawType;

	if( bCollideActors && GetLevel()->Hash )
		GetLevel()->Hash->AddActor( this );
	bNetDirty = true;	// for network replication
	ClearRenderData();

	// scion sz - force update of sorting flags
	bSkinsChanged=1;

	unguard;
}

void AActor::SetDrawScale3D( FVector NewScale3D )
{
	guard(AActor::SetDrawScale3D);

	if( NewScale3D == DrawScale3D )
		return;

	if( bCollideActors && GetLevel()->Hash )
		GetLevel()->Hash->RemoveActor( this );

	DrawScale3D = NewScale3D;

	if( bCollideActors && GetLevel()->Hash )
		GetLevel()->Hash->AddActor( this );
	bNetDirty = true;	// for network replication
	ClearRenderData();

	eventDrawScale3DChanged();

	unguard;
}

/* Update relative rotation - called by ULevel::MoveActor()
 don't update RelativeRotation if attached to a bone -
 if attached to a bone, only update RelativeRotation directly
*/
void AActor::UpdateRelativeRotation()
{
	guard(AActor::UpdateRelativeRotation);

	if ( !Base || Base->bWorldGeometry || (AttachmentBone != NAME_None) )
		return;

	// update RelativeRotation which is the rotation relative to the base's rotation
	FCoords BaseCoords = GMath.UnitCoords * Base->Rotation;
	FCoords Coords = GMath.UnitCoords / Rotation;

	FCoords RelativeCoords = Coords/BaseCoords.OrthoRotation();
	RelativeRotation = RelativeCoords.OrthoRotation();

	unguard;
}



//
// Return whether this actor overlaps another.
// Called normally from MoveActor, to see if we should 'untouch' things.
// Normally - the only things that can overlap an actor are:
// Volumes, Projectors and FluidSurfaces
// However, we also use this test during ActorEncroachmentCheck, so we support
// Encroachers (ie. Movers and Karma actors) overlapping actors.
//
UBOOL AActor::IsOverlapping( AActor* Other, FCheckResult* Hit )
{
	guardSlow(AActor::IsOverlapping);
	checkSlow(Other!=NULL);

	if ( (this->IsBrush() && Other->IsBrush()) || (Other == Level) )
	{
		// We cannot detect whether these actors are overlapping so we say they aren't.
		return 0;
	}

	// Things dont overlap themselves
	if(this == Other)
		return 0;

	// Things that do encroaching (movers, karma actors etc.) can't encroach each other!
	if(this->IsEncroacher() && Other->IsEncroacher())
		return 0;

	// Things that are joined together dont overlap.
	if( this->IsJoinedTo(Other) || Other->IsJoinedTo(this) )
		return 0;

	// If one only affects pawns, and the other isn't a pawn, dont check.
	if (this->bOnlyAffectPawns && !Other->IsAPawn())
		return 0;

	if (Other->bOnlyAffectPawns && !this->IsAPawn())
		return 0;

	// If one thing doesn't block non-zero traces, they can't overlap
	if(!this->bBlockNonZeroExtentTraces || !Other->bBlockNonZeroExtentTraces)
		return 0;

	AActor* PrimitiveActor = this;
	AActor* BoxActor = Other;
	
	// Projectors should always use their primitive - fastest option.
	if( this->IsA(AProjector::StaticClass()) )
	{
		PrimitiveActor = this;
		BoxActor = Other;	
	}
	else
	if( Other->IsA(AProjector::StaticClass()) )
	{
		BoxActor = this;
		PrimitiveActor = Other;	
	}
	// If one is a volume or fluid surface- make sure you us its primitive.
	else if( this->IsVolumeBrush() || this->IsA(AFluidSurfaceInfo::StaticClass()) )
	{
		PrimitiveActor = this;
		BoxActor = Other;	
	}
	else
	if( Other->IsVolumeBrush() || Other->IsA(AFluidSurfaceInfo::StaticClass()) )
	{
		BoxActor = this;
		PrimitiveActor = Other;	
	}
	// If one is an encroacher, use its primitive.
	// This is quite expensive, but should only happen for pushing pawns around,
	// where you need the accuracy.
	else
	if(this->IsEncroacher())
	{
		PrimitiveActor = this;
		BoxActor = Other;	
	}
	else
	if(Other->IsEncroacher())
	{
		BoxActor = this;
		PrimitiveActor = Other;	
	}
	else
	{

#if 1
//scion superville: Use box trace instead
		FCheckResult *PointHits = GetLevel()->MultiPointCheck( GMem, Location, GetCylinderExtent(), 0, GetLevel()->GetLevelInfo(), true );
		for( FCheckResult *CR = PointHits; CR; CR = CR->GetNext() ) {
			if( CR->Actor == Other )
				return 1;
		}
		return 0;
#else		
		// See if cylinder actors are overlapping.
		return
			( (Square(Location.Z - Other->Location.Z) < Square(CollisionHeight + Other->CollisionHeight))
			&&	(Square(Location.X - Other->Location.X) + Square(Location.Y - Other->Location.Y)
				< Square(CollisionRadius + Other->CollisionRadius)) );
#endif
	}

	// PointCheck primitive with box
	FCheckResult TestHit;
	if(Hit==NULL)
		Hit = &TestHit;
	
#if 1
	return PrimitiveActor->GetPrimitive()->PointCheck( *Hit, PrimitiveActor,
			BoxActor->GetPrimitive()->GetEncroachCenter(BoxActor),
			BoxActor->GetPrimitive()->GetEncroachExtent(BoxActor), 0 )==0;
#else
	// DEBUGGING! Print point check if it takes too long.
	DWORD Time=0;
	clock(Time);

	UBOOL pCheck = PrimitiveActor->GetPrimitive()->PointCheck( *Hit, PrimitiveActor,
			BoxActor->GetPrimitive()->GetEncroachCenter(BoxActor),
			BoxActor->GetPrimitive()->GetEncroachExtent(BoxActor), 0 )==0;

	unclock(Time);
	FLOAT mSec = Time * GSecondsPerCycle * 1000.f;
	if(mSec > 1.f)
		debugf(TEXT("IOL: Testing: P:%s - B:%s Time: %f"), PrimitiveActor->GetName(), BoxActor->GetName(), mSec );

	return pCheck;
#endif

	unguardSlow;
}

//
// Get the actor's primitive.
//
UPrimitive* AActor::GetPrimitive()
{
	guardSlow(AActor::GetPrimitive);

	if		( Mesh				)	return Mesh;
	else if ( StaticMesh )			return (UPrimitive*) StaticMesh;
	else if	( AntiPortal		)	return AntiPortal;
	else if	( GetLevel()->Engine)	return GetLevel()->Engine->Cylinder;
	else							return NULL;
	unguardSlow;
}

UPrimitive* ABrush::GetPrimitive()
{
	guardSlow(ABrush::GetPrimitive);
	if		( Brush				)	return Brush;
	else if	( GetLevel()->Engine)	return GetLevel()->Engine->Cylinder;
	else							return NULL;
	unguardSlow;
}

/*-----------------------------------------------------------------------------
	Actor touch minions.
-----------------------------------------------------------------------------*/

static UBOOL TouchTo( AActor* Actor, AActor* Other )
{
	guard(TouchTo);
	check(Actor);
	check(Other);
	check(Actor!=Other);

	for ( INT j=0; j<Actor->Touching.Num(); j++ )
	  if ( Actor->Touching(j) == Other )
		return 1;

	// Make Actor touch TouchActor.
	Actor->Touching.AddItem(Other);
	Actor->eventTouch( Other );

	// See if first actor did something that caused an UnTouch.
	INT i = 0;
	return ( Actor->Touching.FindItem(Other,i) );

	unguard;
}

//
// Note that TouchActor has begun touching Actor.
//
// This routine is reflexive.
//
// Handles the case of the first-notified actor changing its touch status.
//
void AActor::BeginTouch( AActor* Other )
{
	guard(AActor::BeginTouch);

	// Perform reflective touch.
	if( TouchTo( this, Other ) )
		TouchTo( Other, this );

	unguard;
}

//
// Note that TouchActor is no longer touching Actor.
//
// If NoNotifyActor is specified, Actor is not notified but
// TouchActor is (this happens during actor destruction).
//
void AActor::EndTouch( AActor* Other, UBOOL bNoNotifySelf )
{
	guard(AActor::EndTouch);
	check(Other!=this);

	// Notify Actor.
	INT i=0;
	if ( !bNoNotifySelf && Touching.FindItem(Other,i) )
	{
		eventUnTouch( Other );
	}
	Touching.RemoveItem(Other);

	if ( Other->Touching.FindItem(this,i) )
	{
		Other->eventUnTouch( this );
		Other->Touching.RemoveItem(this);
	}
	unguard;
}

/*-----------------------------------------------------------------------------
	AActor member functions.
-----------------------------------------------------------------------------*/

//
// Destroy the actor.
//
void AActor::Serialize( FArchive& Ar )
{
	guard(AActor::Serialize);
	Super::Serialize( Ar );
	if(!Ar.IsLoading() && !Ar.IsSaving())
		Ar	<< Projectors
			<< StaticMeshProjectors;
	unguard;
}

void AActor::ClearRenderData()
{
	RenderRevision++;

	// Remove antiportals from the old zones they touch.

	if(!GIsEditor && AntiPortal)
	{
		for(INT LeafIndex = 0;LeafIndex < Leaves.Num();LeafIndex++)
		{
			FLeaf&	Leaf = XLevel->Model->Leaves(Leaves(LeafIndex));

			XLevel->ZoneRenderInfo(Leaf.iZone).AntiPortals.RemoveItem(this);
		}
	}
}

static FORCEINLINE UBOOL ContainsBox( const FBox& a, const FBox& b )
{
    return a.IsValid() && FPointBoxIntersection(b.Min,a) &&
		FPointBoxIntersection(b.Max,a);
}

float const PredictedBoxTweak = 128.0f;
void AActor::UpdateRenderData()
{
	guard(AActor::UpdateRenderData);

	FUNC_CLOCK(UpRenCyc,GStats.DWORDStats(GEngineStats.STATS_Game_UpdateRenderData));	

    if( bDeleteMe ) // touch events dispatched from moveactor can cause this
        return;

	checkSlow(ActorRenderData);
	ActorRenderData->Update();

	// scion sz - moved here instead of in FDynamicActor::Update
	ActorRenderData->Revision = RenderRevision;

	static FColor ColorBk = FColor(0,0,0,0);
	static FColor ColorWt = FColor(255,255,255,0);
	ActorRenderData->AmbientColor = ColorBk;
    if( !GIsEditor && ContainsBox( ActorRenderData->PredictedBox, ActorRenderData->BoundingBox ) )
    {
        if( bUnlit )
		{
			// scion sz - don't use ambient glow for unlit actors
			ActorRenderData->AmbientColor = ColorWt;
		}		
		else
		{
			// scion sz - only update ambient if this actor is the lighting base
			// otherwise just use the base's ambient
			AActor* AmbientLitActor = GetAmbientLightingActor();
			if( AmbientLitActor != this )
			{
				FDynamicActor* BaseDynActor = AmbientLitActor->GetActorRenderData();
				ActorRenderData->AmbientColor = BaseDynActor->AmbientColor;
				ActorRenderData->AmbientVector = BaseDynActor->AmbientVector;			
			}
			else
			{
				FLOAT	AmbientGlow;
				if( AmbientLitActor->AmbientGlow == 255 )
					AmbientGlow = 0.25f + 0.2f * appSin(8.0f * Level->TimeSeconds);
				else
					AmbientGlow = AmbientLitActor->AmbientGlow / 255.0;

				ActorRenderData->AmbientColor = FColor(ActorRenderData->AmbientVector + FVector(AmbientGlow,AmbientGlow,AmbientGlow));
			}
		}		
// scion sz - not used
#ifndef _XBOX
		// Add antiportals to the zones they touch.
		if(!GIsEditor && DrawType == DT_AntiPortal && AntiPortal)
		{
			for(INT LeafIndex = 0;LeafIndex < Leaves.Num();LeafIndex++)
			{
				FLeaf&	Leaf = XLevel->Model->Leaves(Leaves(LeafIndex));

				XLevel->ZoneRenderInfo(Leaf.iZone).AntiPortals.AddUniqueItem(this);
			}
		}
#endif
        return;
    }

    // todo: add some bbox prediction for emitters
    if ( Physics == PHYS_Rotating )
    {
        FVector center, extents;
        ActorRenderData->BoundingBox.GetCenterAndExtents(center, extents);
        ActorRenderData->PredictedBox.Min = ActorRenderData->PredictedBox.Max = center;
        ActorRenderData->PredictedBox = ActorRenderData->PredictedBox.ExpandBy(extents.Size()*1.2f);
    }
    else if ( Physics == PHYS_None )
    {
        ActorRenderData->PredictedBox = ActorRenderData->BoundingBox;
    }
    else
    {
        ActorRenderData->PredictedBox = ActorRenderData->BoundingBox;
        //ActorRenderData->PredictedBox += (ActorRenderData->BoundingBox.Min + Velocity * 1.5f);
        //ActorRenderData->PredictedBox += (ActorRenderData->BoundingBox.Max + Velocity * 1.5f);
        ActorRenderData->PredictedBox = ActorRenderData->PredictedBox.ExpandBy(PredictedBoxTweak);
    }

	// scion sz - typically faster than reallocating
    Leaves.Reset();
	// scion sz - pass array to avoid copy
	XLevel->Model->BoxLeaves(Leaves,ActorRenderData->PredictedBox);
	if(ForcedVisibilityZoneTag != NAME_None)
	{
		for(INT LeafIndex = 0;LeafIndex < Leaves.Num();LeafIndex++)
		{
			FLeaf&		Leaf = XLevel->Model->Leaves(Leaves(LeafIndex));
			AZoneInfo*	ZoneInfo = XLevel->GetZoneActor(Leaf.iZone);

			if(ZoneInfo->Tag != ForcedVisibilityZoneTag)
				Leaves.Remove(LeafIndex--);
		}
	}

	// Calculate the ambient lighting.
	if( bUnlit )
	{
		// scion sz - don't use zone ambient for unlit actors
		ActorRenderData->AmbientColor = ColorWt;
		ActorRenderData->AmbientVector = FVector(0.f,0.f,0.f);
	}
	else
	{
		// scion sz - only update ambient if this actor is the lighting base
		// otherwise just use the base's ambient
		AActor* AmbientLitActor = GetAmbientLightingActor();
		if( AmbientLitActor != this )
		{
			FDynamicActor* BaseDynActor = AmbientLitActor->GetActorRenderData();
			ActorRenderData->AmbientColor = BaseDynActor->AmbientColor;
            ActorRenderData->AmbientVector = BaseDynActor->AmbientVector;			
		}
		else
		{
			FVector	AmbientVector(0,0,0);
			for(INT LeafIndex = 0;LeafIndex < Leaves.Num();LeafIndex++)
			{
				FLeaf&		Leaf = XLevel->Model->Leaves(Leaves(LeafIndex));
				AZoneInfo*	ZoneInfo = XLevel->GetZoneActor(Leaf.iZone);
				FVector&	ZoneAmbientVector = ZoneInfo->AmbientVector;

				AmbientVector.X = Max(AmbientVector.X,ZoneAmbientVector.X);
				AmbientVector.Y = Max(AmbientVector.Y,ZoneAmbientVector.Y);
				AmbientVector.Z = Max(AmbientVector.Z,ZoneAmbientVector.Z);
			}
			ActorRenderData->AmbientVector = AmbientVector;

			FLOAT	AmbientGlow;
			if( AmbientLitActor->AmbientGlow == 255 )
				AmbientGlow = 0.25f + 0.2f * appSin(8.0f * Level->TimeSeconds);
			else
				AmbientGlow = AmbientLitActor->AmbientGlow / 255.0;

			ActorRenderData->AmbientColor = FColor(ActorRenderData->AmbientVector + FVector(AmbientGlow,AmbientGlow,AmbientGlow));
		}		
	}	

// scion sz - not used
#ifndef _XBOX
	// Add antiportals to the zones they touch.
	if(!GIsEditor && DrawType == DT_AntiPortal && AntiPortal)
	{
		for(INT LeafIndex = 0;LeafIndex < Leaves.Num();LeafIndex++)
		{
			FLeaf&	Leaf = XLevel->Model->Leaves(Leaves(LeafIndex));

			XLevel->ZoneRenderInfo(Leaf.iZone).AntiPortals.AddUniqueItem(this);
		}
	}
#endif

	unguardf((TEXT("%s"),GetFullName()));
}

/*-----------------------------------------------------------------------------
	Relations.
-----------------------------------------------------------------------------*/

//
// Change the actor's owner.
//
void AActor::SetOwner( AActor *NewOwner )
{
	guard(AActor::SetOwner);

	// Sets this actor's parent to the specified actor.
	if( Owner != NULL )
		Owner->eventLostChild( this );

	Owner = NewOwner;

	if( Owner != NULL )
		Owner->eventGainedChild( this );
	bNetDirty = true;
	unguard;
}

//
// Change the actor's base.
//
void AActor::SetBase(AActor* NewBase, const FVector& NewFloor, int bNotifyActor)
{
	guard(AActor::SetBase);
	
	// Verify no recursion.
	for( AActor* Loop=NewBase; Loop!=NULL; Loop=Loop->Base )
		if ( Loop == this )
			return;

	if( NewBase != Base )
	{
		// Notify old base, unless it's the level or terrain (but not movers).
		if( Base && !Base->bWorldGeometry )
		{
			AActor* OldBase = Base;
			Base->Attached.RemoveItem(this);
			Base->eventDetach( this );
			//epic moelfke: bail if Detach() calls SetBase() again
			if (OldBase != Base)
			{
				return;
			}
			//scion jg -- Decrement the collidable attachment count if needed
			if (bCollideActors || bCollideWorld)
			{
				checkSlow(Base->NumAttachedWithCollision > 0);
				Base->NumAttachedWithCollision--;
			}
			// Now check for hard attachment without branching
			Base->NumAttachedWithHardAttach -= bHardAttach;
		}

		// Set base.
		Base = NewBase;
		if ( Base && !Base->bWorldGeometry && (AttachmentBone == NAME_None) && !bOnlyDrawIfAttached )
		{
			if ( !bHardAttach || (Role == ROLE_Authority) )
			{
				RelativeLocation = Location - Base->Location;
				UpdateRelativeRotation();
			}

			// Calculate the transform of this actor relative to its base.
			if(bHardAttach)
			{
				FMatrix BaseInvTM = FTranslationMatrix(-Base->Location) * FInverseRotationMatrix(Base->Rotation);
				FMatrix ChildTM = FRotationMatrix(Rotation) * FTranslationMatrix(Location);

				FMatrix HardRelMatrix =  ChildTM * BaseInvTM;
				RelativeLocation = HardRelMatrix.GetOrigin();
				RelativeRotation = HardRelMatrix.Rotator();
			}
		}

		// Notify new base, unless it's the level.
		if( Base && !Base->bWorldGeometry )
		{
			//scion jg -- Increment the collidable attachment count if needed
			if (bCollideActors || bCollideWorld)
			{
				checkSlow(Base->NumAttachedWithCollision < 255);
				Base->NumAttachedWithCollision++;
			}
			// Now check for hard attachment without branching
			Base->NumAttachedWithHardAttach += bHardAttach;
			Base->Attached.AddItem(this);
			Base->eventAttach(this);
		}

		// Notify this actor of his new floor.
		if ( bNotifyActor )
			eventBaseChange();

		// scion sz - 
		// force anything based to a pawn to use lighting from base
		AActor* LitBase=Base;
		while( LitBase && !LitBase->bWorldGeometry && LitBase != Level )
		{
			if( LitBase->bUseLightingFromBase ||
				LitBase->IsAPawn() )
			{
				bUseLightingFromBase = 1;
				break;
			}
			LitBase = LitBase->Base;
		}
	}
	if ( (!Base || Base->bWorldGeometry) && (Level->NetMode != NM_Client) )
		AttachmentBone = NAME_None;

	unguard;
}

//
//	Same as IsBasedOn.  Nuke?
//
inline UBOOL AActor::IsJoinedTo( const AActor *Other) const
{
	guard(AActor::IsJoinedTo);

	// so that Actors can't be jointed together!
	for( const AActor* Test=this; Test!=NULL; Test=Test->Base )
		if (Test == Other)
			return 1;
	return 0;

	unguard;
}

//
// Determine if BlockingActor should block actors of the given class.
// This routine needs to be reflexive or else it will create funky
// results, i.e. A->IsBlockedBy(B) <-> B->IsBlockedBy(A).
//
inline UBOOL AActor::IsBlockedBy( const AActor* Other, DWORD TraceFlags ) const
{
	guardSlow(AActor::IsBlockedBy);
	checkSlow(this!=NULL);
	checkSlow(Other!=NULL);

	if( Other->bWorldGeometry )
		return bCollideWorld;
	else if(IsEncroacher() && Other->bIgnoreEncroachers)
		return false;
	else if(Other->IsEncroacher() && bIgnoreEncroachers)
		return false;
	else if( Other->IsBrush() || Other->IsEncroacher() )
	{
		//epic superville: Movers should block camera if they have the block camera flag checked
		if( (TraceFlags & TRACE_Camera) )
			return Other->bBlockCamera;

		return bCollideWorld && (GetPlayerPawn() ? Other->bBlockPlayers : Other->bBlockActors);
	}
	else if ( IsBrush() || IsEncroacher() )
	{
		if ( IsMovingBrush() && ((AActor*)Other)->IsAPlayerController() )
			return false;
		return Other->bCollideWorld && (Other->GetPlayerPawn() ? bBlockPlayers : bBlockActors);
	}
	else
		return ( (GetPlayerPawn() || ((AActor*)this)->IsAProjectile()) ? Other->bBlockPlayers : Other->bBlockActors)
		&&	   ( (Other->GetPlayerPawn() || ((AActor*)Other)->IsAProjectile()) ? bBlockPlayers : bBlockActors);

	unguardSlow;
}

UBOOL AActor::AttachToBone( AActor* Attachment, FName BoneName )
{
	guardSlow(AActor::AttachToBone);
	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()) )
	{				
		// Verify that the bone exists in the aliases or bones list.
		USkeletalMeshInstance* Instance = (USkeletalMeshInstance*)Mesh->MeshGetInstance(this);
		if (Instance)
		{
			INT BoneIndex = Instance->MatchRefBone(BoneName);
			if (BoneIndex > -1 && Attachment && Attachment->IsValid())
			{
				// No checks for previous..
				Attachment->AttachmentBone = BoneName;
				// Attach to base
				Attachment->SetBase(this, FVector(0,0,1));
				//debugf(TEXT("UnActor.AttachToBone: attachment [%s] made to bone named [%s] in actor [%s]'s skeleton."),Attachment->GetName(),*BoneName,GetName());
				return 1;
			}
			else
			{
				if (Attachment && Attachment->IsValid())
				{
					debugf(TEXT("AttachToBone: %s no bone or socket named [%s] found in actor [%s]'s skeleton."), Attachment->GetName(), *BoneName, GetName());
				}
				else
				{
					debugf(TEXT("AttachToBone: Invalid attachment for bone [%s] in actor [%s]'s skeleton."), *BoneName, GetName());
				}
			}
		}
	}
	else
	{
		debugf(NAME_Warning, TEXT("AttachToBone: Attempt to attach [%s] to actor [%s] with no mesh!"), Attachment ? Attachment->GetName() : TEXT("None"), GetName());
	}
	return 0;
	unguardSlow;
}

UBOOL AActor::DetachFromBone( AActor* Attachment )
{
	guardSlow(AActor::DetachFromBone);
	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()) )
	{				
		// Remove it..
		Attachment->SetBase( NULL, FVector(0,0,1) );
		Attachment->AttachmentBone = NAME_None;
		return 1; // Add more useful success/fail reporting ?
	}
	return 0;
	unguardSlow;
}

void APawn::SetBase(AActor* NewBase, const FVector& NewFloor, int bNotifyActor)
{
	guard(APawn::SetBase);

	Floor = NewFloor;
	Super::SetBase(NewBase,NewFloor,bNotifyActor);
	unguard;
}

/*-----------------------------------------------------------------------------
	Special editor support.
-----------------------------------------------------------------------------*/
AActor* AActor::GetHitActor()
{
	return this;
}

/*---------------------------------------------------------------------------------------
	Brush class implementation.
---------------------------------------------------------------------------------------*/

void ABrush::InitPosRotScale()
{
	guard(ABrush::InitPosRotScale);
	check(Brush);
	
	MainScale = GMath.UnitScale;
	PostScale = GMath.UnitScale;
	Location  = FVector(0,0,0);
	Rotation  = FRotator(0,0,0);
	PrePivot  = FVector(0,0,0);

	unguard;
}
void ABrush::PostLoad()
{
	guard(ABrush::PostLoad);
	Super::PostLoad();
	unguard;
}

/*---------------------------------------------------------------------------------------
	Tracing check implementation.
	ShouldTrace() returns true if actor should be checked for collision under the conditions
	specified by traceflags
---------------------------------------------------------------------------------------*/

UBOOL AActor::ShouldTrace(AActor *SourceActor, DWORD TraceFlags)
{
	guard(AActor::ShouldTrace);

	// Skip actors without bShadowCast when raytracing for shadows.
	if( Level && this!=Level && (!this->bShadowCast) && (TraceFlags & TRACE_ShadowCast) )
		return false;

	// Check bOnlyAffectPawns flag. APawn has its own ShouldTrace, so this actor can't be one.
	if( SourceActor && SourceActor->bOnlyAffectPawns )
		return false;

	//scion superville: Camera trace purely determined by bBlockCamera flag
	if( TraceFlags & TRACE_Camera )
		return bBlockCamera;

	if( this->bOnlyAffectPawns && SourceActor && !SourceActor->IsAPawn() )
		return false;

	if(TraceFlags & TRACE_AcceptProjectors)
		return bAcceptsProjectors;

	if((TraceFlags & TRACE_Corona) &&
		((StaticMesh && DrawType == DT_StaticMesh) || (Mesh && IsDTMesh())) )
		return true;

	if( TraceFlags & TRACE_ShadowCast )
		return bShadowCast;
	else if ( bWorldGeometry )
		return (TraceFlags & TRACE_LevelGeometry);
	else if( TraceFlags & TRACE_Others )
	{
		if( TraceFlags & TRACE_OnlyProjActor )
		{
			if(bProjTarget || (bBlockActors && bBlockPlayers) )
				return true;
		}
		else if ( TraceFlags & TRACE_Blocking )
		{
			if ( SourceActor && SourceActor->IsBlockedBy(this,TraceFlags) )
				return true;
		}
		else
			return true;
	}
		
	return false;
	unguard;
}

void AActor::CopyMaterialsToSkins(UBOOL bSetDefault)
{
	guard(CopyMaterialsToSkins);
	AActor* DefaultActor = NULL;
	if( bSetDefault ) {
		DefaultActor = GetClass()->GetDefaultActor();
	}

	if (IsDTMesh() && Mesh != NULL)
	{
		Skins.Empty();
		Skins.AddZeroed(((ULodMesh*)Mesh)->Materials.Num());
		for (INT i=0; i<((ULodMesh*)Mesh)->Materials.Num(); i++)
		{
			Skins(i) = ((ULodMesh*)Mesh)->Materials(i);
		}

		if( DefaultActor ) {
			DefaultActor->Skins.Empty();
			DefaultActor->Skins.AddZeroed(((ULodMesh*)Mesh)->Materials.Num());
			for (INT i=0; i<((ULodMesh*)Mesh)->Materials.Num(); i++)
			{
				DefaultActor->Skins(i) = ((ULodMesh*)Mesh)->Materials(i);
			}
		}
	}
	else if (DrawType == DT_StaticMesh && StaticMesh != NULL)
	{
		Skins.Empty();
		Skins.AddZeroed(((UStaticMesh*)StaticMesh)->Materials.Num());
		for (INT i=0; i<((UStaticMesh*)StaticMesh)->Materials.Num(); i++)
		{
			Skins(i) = ((UStaticMesh*)StaticMesh)->GetSkin(this,i);
		}

		if( DefaultActor ) {
			DefaultActor->Skins.Empty();
			DefaultActor->Skins.AddZeroed(((UStaticMesh*)StaticMesh)->Materials.Num());
			for (INT i=0; i<((UStaticMesh*)StaticMesh)->Materials.Num(); i++)
			{
				DefaultActor->Skins(i) = ((UStaticMesh*)StaticMesh)->GetSkin(this,i);
			}
		}
	}
	unguard;
}

UBOOL AVolume::ShouldTrace(AActor *SourceActor, DWORD TraceFlags)
{
	guard(AVolume::ShouldTrace);

	// Skip actors without bShadowCast when raytracing for shadows.
	if( !this->bShadowCast && (TraceFlags & TRACE_ShadowCast) )
		return false;

	if( SourceActor && SourceActor->bOnlyAffectPawns )
		return false;

	//scion superville: Camera trace purely determined by bBlockCamera flag
	if( TraceFlags & TRACE_Camera )
		return bBlockCamera;

	if( this->bOnlyAffectPawns && SourceActor && !SourceActor->IsAPawn() )
		return false;

	//scion superville: If ignore projectiles, skip if query from projectile
	if (this->bIgnoreProjectiles && SourceActor && SourceActor->IsAProjectile())
		return false;

	if ( bWorldGeometry && (TraceFlags & TRACE_LevelGeometry) )
		return true;
	if ( TraceFlags & TRACE_Volumes )
	{
		if( TraceFlags & TRACE_OnlyProjActor )
		{
			if( bProjTarget || (bBlockActors && bBlockPlayers) )
				return true;
		}
		else if ( TraceFlags & TRACE_Blocking )
		{
			if ( SourceActor && SourceActor->IsBlockedBy(this,TraceFlags) )
				return true;
		}
		else
			return true;
	}
	return false;
	unguard;
}

void AProjectile::CreateRecentMap(TMap<_WORD, TArray<BYTE>*>& RecentMap)
{
	guard(AProjectile::CreateRecentMap);

	UObject* Default = StaticConstructObject(GetClass());

	// add the only Actor properties that will be replicated
	static UProperty* ActorReplicatedProps[7] = {	FindField<UProperty>(GetClass(), TEXT("Rotation")),
													FindField<UProperty>(GetClass(), TEXT("Velocity")),
													FindField<UProperty>(GetClass(), TEXT("Physics")),
													FindField<UProperty>(GetClass(), TEXT("DrawScale")),
													FindField<UProperty>(GetClass(), TEXT("DrawType")),
													FindField<UProperty>(GetClass(), TEXT("Instigator")),
													FindField<UProperty>(GetClass(), TEXT("LightType"))	};
	for (INT i = 0; i < ARRAY_COUNT(ActorReplicatedProps); i++)
	{
		checkSlow(ActorReplicatedProps[i]);
		AddToRecentMap(ActorReplicatedProps[i], (BYTE*)Default, RecentMap);
	}

	// iterate through all our properties, stopping when we get to Actor's because we already added the Actor properties we care about
	for (UProperty* It = GetClass()->PropertyLink; It && It->GetOuter() != AActor::StaticClass(); It = It->PropertyLinkNext)
	{
		AddToRecentMap(It, (BYTE*)Default, RecentMap);
	}

	delete Default;

	unguard;
}

INT* AProjectile::GetOptimizedRepList( BYTE* Defaults, FPropertyRetirement* Retire, INT* Ptr, UPackageMap* Map, UActorChannel* Channel )
{
	guard(AProjectile::GetOptimizedRepList);

	// only replicate rotation initially
	if( bNetInitial )
	{
		DOREP(Actor,Rotation,FRotator);
	}
	// only replicate velocity initially, except for special cases
	if( bNetInitial || bUpdateSimulatedPosition )
	{
		DOREP(Actor,Velocity,FVector);
	}
	// only replicate physics initially, except for special cases
	if( bNetInitial || bAlwaysRepPhysic )
	{
		DOREP(Actor,Physics,BYTE);
	}
	// only replicate draw attributes if necessary
	if( bReplicateDrawAttributes )
	{
		DOREP(Actor,DrawScale,FLOAT);
		DOREP(Actor,DrawType,BYTE);
	}

	if( bNetDirty ) 
	{
		DOREPBOOL(Projectile,bIgnoreMe);
	}

	// always replicate instigator
	DOREP(Actor,Instigator,APawn*);
	// always replicate light changes
	DOREP(Actor,LightType,BYTE);

	return Ptr;
	unguard;
}

// scion sz -
void ALight::PostLoad()
{
	guard(ALight::PostLoad);
	Super::PostLoad();

#ifdef _XBOX
	// destroy actor if it is a static light that only affects world objects and
	// isn't being used as a corona
	if( !bCorona &&
		AffectType == AT_AffectWorld &&
		XLevel )
	{	
		XLevel->DestroyActor( this,0,1,1 );
	}

	// force static lights to never LOD out
	if( bStatic )
	{
		DetailLevel = DM_Low;
	}	
#endif

	unguard;
}

// scion sz -
void ALight::Destroy()
{
	guard(ALight::Destroy);
	Super::Destroy();

#ifdef _XBOX
	if( !XLevel )
		return;

	// remove this light from bsp leaves that it was permeating
	for( INT LightIdx=0; LightIdx < XLevel->Model->Lights.Num(); LightIdx++ )
	{
		// find matching light entries for the bsp leaves
		if( XLevel->Model->Lights(LightIdx) == this )
		{
			// set the actor to an invalid light 
			XLevel->Model->Lights(LightIdx) = (AActor*)FLeaf::INVALID_LIGHT;
		}
	}
#endif

	unguard;
}

/*-----------------------------------------------------------------------------
	The end.
-----------------------------------------------------------------------------*/

