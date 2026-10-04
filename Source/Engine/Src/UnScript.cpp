/*=============================================================================
	UnScript.cpp: UnrealScript engine support code.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

Description:
	UnrealScript execution and support code.

Revision history:
	* Created by Tim Sweeney
=============================================================================*/

#include "EnginePrivate.h"
#include "UnRender.h"
#include "UnNet.h"

#ifndef _XBOX
#include "OpCode.h"
#endif

//scion rwd - added forward declaration of this func to be used by funcs like RadiusPlaySoundCue
static inline void InternalProcessRemoteFunction(AActor*,UNetConnection*,UFunction*,void*,FFrame*,UBOOL);

//
// Generalize animation retrieval to work for both skeletal meshes (animation sits in Actor->SkelAnim->AnimSeqs) and
// classic meshes (Mesh->AnimSeqs) For backwards compatibility....
//

/*-----------------------------------------------------------------------------
	Tim's physics modes.
-----------------------------------------------------------------------------*/

/* unused - vogel
static FLOAT Splerp( FLOAT F )
{
	FLOAT S = Square(F);
	return (1.0f/16.0f)*S*S - (1.0f/2.0f)*S + 1;
}
*/
//
// Moving brush.
//
void AMover::physMovingBrush( FLOAT DeltaTime )
{
	guard(AMover::physMovingBrush);

	FVector OldLocation = Location;

	INT NewKeyNum     = Clamp( (INT)KeyNum, (INT)0, (INT)ARRAY_COUNT(KeyPos) );
	FLOAT TotalTime = DeltaTime;

	if( bInterpolating )
	{
		// If we are moving, turn off all attached anti-portals

		for (INT AP=0;AP<AntiPortals.Num();AP++)
		{
			if(AntiPortals(AP)->DrawType != DT_None)
				AntiPortals(AP)->SetDrawType(DT_None);
		}
	}

	while( bInterpolating && DeltaTime>0.0f )
	{
		// We are moving.
		FLOAT NewAlpha = PhysAlpha + DeltaTime * PhysRate;
		if( NewAlpha > 1.0f )
		{
			DeltaTime *= (NewAlpha - 1.0f) / (NewAlpha - PhysAlpha);
			NewAlpha   = 1.0f;
		}
		else DeltaTime = 0.0f;

		// Compute alpha.
		FLOAT RenderAlpha;
		if( MoverGlideType == MV_GlideByTime )
		{
			// Make alpha time-smooth and time-continuous.
			// f(0)=0, f(1)=1, f'(0)=f'(1)=0.
			RenderAlpha = 3.0f*NewAlpha*NewAlpha - 2.0f*NewAlpha*NewAlpha*NewAlpha;
		}
		else RenderAlpha = NewAlpha;

		// Move.
		FCheckResult Hit(1.0f);
		if( GetLevel()->MoveActor
		(
			this,
			OldPos + ((BasePos + KeyPos[NewKeyNum]) - OldPos) * RenderAlpha - Location,
			OldRot + ((BaseRot + KeyRot[NewKeyNum]) - OldRot) * RenderAlpha,
			Hit
		) )
		{
			// Successfully moved.
			PhysAlpha = NewAlpha;
			if( NewAlpha == 1.0f )
			{
				// Just finished moving.
				bInterpolating = 0;
				eventKeyFrameReached();

				if (NewKeyNum==0)
				{
					// If we have stopped at the intial frame, flag it

					for (INT AP=0;AP<AntiPortals.Num();AP++)
					{
						if(AntiPortals(AP)->DrawType != DT_AntiPortal)
							AntiPortals(AP)->SetDrawType(DT_AntiPortal);
					}
				}
			}
		}
	}
	Velocity = (Location - OldLocation)/TotalTime;
	unguard;
}

//
// Initialize execution.
//
void AActor::InitExecution()
{
	guard(AActor::InitExecution);

	UObject::InitExecution();

	check(GetStateFrame());
	check(GetStateFrame()->Object==this);
	check(GetLevel()!=NULL);
	check(GetLevel()->Actors(0)!=NULL);
	check(GetLevel()->Actors(0)==Level);
	check(Level!=NULL);

	unguardobj;
}

/*-----------------------------------------------------------------------------
	Natives.
-----------------------------------------------------------------------------*/

//////////////////////
// Console Commands //
//////////////////////

void AActor::execConsoleCommand( FFrame& Stack, RESULT_DECL )
{
	guard(UObject::execConsoleCommand);

	P_GET_STR(Command);
	P_GET_UBOOL_OPTX(bWriteToLog, false);
	P_FINISH;

	FStringOutputDevice StrOut;

	if (bWriteToLog)
	{
		GetLevel()->Engine->Exec(*Command, *GLog);
	}
	else
	{
		GetLevel()->Engine->Exec(*Command, StrOut);
	}

	*(FString*)Result = *StrOut;

	unguard;
}

/////////////////////////////
// Log and error functions //
/////////////////////////////

void AActor::execError( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execError);

	P_GET_STR(S);
	P_FINISH;

	Stack.Log( *S );
	GetLevel()->DestroyActor( this );

	unguardexecSlow;
}

//////////////////////////
// Clientside functions //
//////////////////////////

void APlayerController::execGetPlayerNetworkAddress( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerController::execGetPlayerNetworkAddress);
	P_FINISH;

	if( Player && Player->IsA(UNetConnection::StaticClass()) )
		*(FString*)Result = Cast<UNetConnection>(Player)->LowLevelGetRemoteAddress();
	else
		*(FString*)Result = TEXT("");
	unguard;
}

void APlayerController::execGetServerNetworkAddress( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerController::execGetPlayerNetworkAddress);
	P_FINISH;

	if( GetLevel() && GetLevel()->NetDriver && GetLevel()->NetDriver->ServerConnection )
		*(FString*)Result = GetLevel()->NetDriver->ServerConnection->LowLevelGetRemoteAddress();
	else
		*(FString*)Result = TEXT("");
	unguard;
}

void APlayerController::execCopyToClipboard( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerController::execCopyToClipboard);
	P_GET_STR(Text);
	P_FINISH;
	appClipboardCopy(*Text);
	unguard;
}

void APlayerController::execPasteFromClipboard( FFrame& Stack, RESULT_DECL )
{
	guard(APlayerController::execCopyToClipboard);
	P_GET_STR(Text);
	P_FINISH;
	*(FString*)Result = appClipboardPaste();
	unguard;
}

void ALevelInfo::execDetailChange( FFrame& Stack, RESULT_DECL )
{
	guard(ALevelInfo::execDetailChange);

	P_GET_BYTE(NewDetailMode);
	P_FINISH;

	XLevel->DetailChange((EDetailMode)NewDetailMode);

	unguard;
}

void ALevelInfo::execGetLocalURL( FFrame& Stack, RESULT_DECL )
{
	guardSlow(ALevelInfo::execGetLocalURL);

	P_FINISH;

	*(FString*)Result = GetLevel()->URL.String();

	unguardexecSlow;
}

void ALevelInfo::execGetDefaultURL( FFrame& Stack, RESULT_DECL )
{
	guard(ALevelInfo::execGetDefaultURL);

	P_GET_STR(Option);
	P_FINISH;

	FURL URL;
	URL.LoadURLConfig( TEXT("DefaultPlayer"), TEXT("User") );

	*(FString*)Result = FString( URL.GetOption(*(Option + FString(TEXT("="))), TEXT("")) );
	unguard;
}

void ALevelInfo::execIsDemoBuild( FFrame& Stack, RESULT_DECL )
{
	guardSlow(ALevelInfo::execIsDemoBuild);
	P_FINISH;

#if DEMOVERSION
	*(UBOOL*)Result = 1;
#else
	*(UBOOL*)Result = 0;
#endif

	unguardexecSlow;
}

//scion capps Accessor function for script.
void ALevelInfo::execIsXBox( FFrame& Stack, RESULT_DECL )
{
	guardSlow(ALevelInfo::execIsXBox);
	P_FINISH;

#ifdef _XBOX
	*(UBOOL*)Result = 1;
#else
	*(UBOOL*)Result = 0;
#endif

	unguardexecSlow;
}

void ALevelInfo::execIsLinLoad( FFrame& Stack, RESULT_DECL )
{
	guardSlow(ALevelInfo::execIsLinLoad);
	P_FINISH;
#if LIN_LOAD
	*(UBOOL*)Result = 1;
#else
	*(UBOOL*)Result = 0;
#endif
	unguardexecSlow;
}

void ALevelInfo::execGetAddressURL( FFrame& Stack, RESULT_DECL )
{
	guardSlow(ALevelInfo::execGetAddressURL);

	P_FINISH;

	*(FString*)Result = FString::Printf( TEXT("%s:%i"), *GetLevel()->URL.Host, GetLevel()->URL.Port );

	unguardexecSlow;
}

////////////////////////////////
// Latent function initiators //
////////////////////////////////

void AActor::execSleep( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSleep);

	P_GET_FLOAT(Seconds);
	P_FINISH;

	GetStateFrame()->LatentAction = EPOLL_Sleep;
	LatentFloat  = Seconds;

	unguardexecSlow;
}

void AActor::execFinishAnim( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execFinishAnim);

	P_GET_INT_OPTX(Channel, 0);
	P_FINISH;

	LatentFloat = Channel;
	StartAnimPoll();
	unguardexecSlow;
}

void AActor::StartAnimPoll()
{
	// Part of ending the zero-channel main animation.
	if( Mesh )
	{				
		Mesh->MeshGetInstance(this);
		if( MeshInstance->IsAnimLooping(LatentFloat) )
			MeshInstance->AnimStopLooping(LatentFloat);

		// If animation is (re)playing, wait for it to finish.
		if( IsAnimating(LatentFloat) && !MeshInstance->IsAnimPastLastFrame(LatentFloat) )
			GetStateFrame()->LatentAction = EPOLL_FinishAnim;
	}	
}

void AController::StartAnimPoll()
{
	if ( !Pawn )
		return;

	if( Pawn->Mesh )
	{				
		Pawn->Mesh->MeshGetInstance(this);
		if( Pawn->MeshInstance->IsAnimLooping(LatentFloat) )
			Pawn->MeshInstance->AnimStopLooping(LatentFloat);

		// If animation is (re)playing, wait for it to finish.
		if( Pawn->IsAnimating(LatentFloat) && !Pawn->MeshInstance->IsAnimPastLastFrame(LatentFloat) )
			GetStateFrame()->LatentAction = EPOLL_FinishAnim;
	}	
}


UBOOL AActor::CheckAnimFinished(INT Channel)
{
	if ( !Mesh )
		return true;
	Mesh->MeshGetInstance(this);
	return ( !IsAnimating(Channel) || MeshInstance->IsAnimPastLastFrame(Channel) );
}

UBOOL AController::CheckAnimFinished(INT Channel)
{
	if ( !Pawn || !Pawn->Mesh )
		return true;
	Pawn->Mesh->MeshGetInstance(this);
	return ( !Pawn->IsAnimating(Channel) || Pawn->MeshInstance->IsAnimPastLastFrame(Channel) );
}

void AActor::execFinishInterpolation( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execFinishInterpolation);

	P_FINISH;

	GetStateFrame()->LatentAction = EPOLL_FinishInterpolation;

	unguardexecSlow;
}

///////////////////////////
// Slow function pollers //
///////////////////////////

void AActor::execPollSleep( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execPollSleep);

	FLOAT DeltaSeconds = *(FLOAT*)Result;
	if( (LatentFloat-=DeltaSeconds) < 0.5 * DeltaSeconds )
	{
		// Awaken.
		GetStateFrame()->LatentAction = 0;
	}
	unguardexecSlow;
}
IMPLEMENT_FUNCTION( AActor, EPOLL_Sleep, execPollSleep );

void AActor::execPollFinishAnim( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execPollFinishAnim);

	if( CheckAnimFinished(LatentFloat) )
		GetStateFrame()->LatentAction = 0;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( AActor, EPOLL_FinishAnim, execPollFinishAnim );

void AActor::execPollFinishInterpolation( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execPollFinishInterpolation);

	if( !bInterpolating )
		GetStateFrame()->LatentAction = 0;

	unguardexecSlow;
}
IMPLEMENT_FUNCTION( AActor, EPOLL_FinishInterpolation, execPollFinishInterpolation );

/////////////////////////
// Animation functions //
/////////////////////////
void AActor::execGetMeshName( FFrame& Stack, RESULT_DECL )
{
	guard(AActor::GetMeshName);
	P_FINISH;


	if(Mesh)
		*(FString*)Result = FString(Mesh->GetName());
	else
		*(FString*)Result = FString(TEXT(""));

	unguard;
}

void AActor::PlayAnim(INT Channel,FName SequenceName, FLOAT PlayAnimRate, FLOAT TweenTime, INT Loop)
{
	guard(AActor::PlayAnim);

	//scion superville: DON'T USE TWEENING!
#ifdef _DEBUG
	if( TweenTime != 0.f && bDebug ) {
		debugf( TEXT("ACTOR %s USES TWEENING!!! BAD BAD BAD - TweenTime %f Seq %s"), GetName(), TweenTime, *SequenceName );
	}
#endif
	TweenTime = 0.f;

	
	if( !Mesh )
	{
		if ( !bHidden )
		{
			//debugf(TEXT("%s playanim with no mesh!"),GetName());
		}
		return;
	}
	Mesh->MeshGetInstance(this);
	if (MeshInstance)
	{
		MeshInstance->PlayAnim(Channel, SequenceName, PlayAnimRate, TweenTime, Loop);
	}
	

	//scion superville: Mirror animations
	//scion sz - only mirror to attachments that don't use owner driven animation
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->PlayAnim( Channel, SequenceName, PlayAnimRate, TweenTime, Loop );
			}
	}

	unguard;
}

void AActor::execPlayAnim( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execPlayAnim);

	P_GET_NAME(SequenceName);
	P_GET_FLOAT_OPTX(PlayAnimRate,1.f);
	P_GET_FLOAT_OPTX(TweenTime,0.f); // NOTE: universal default was "-1.0f" for a long time... - Erik
	P_GET_INT_OPTX(Channel, 0);
	P_FINISH;

    if (SequenceName != NAME_None) {
        PlayAnim(Channel,SequenceName, PlayAnimRate, TweenTime,0);
    }
	
	unguardexecSlow;
}

void AActor::execPlayLIPSincAnim( FFrame& Stack, RESULT_DECL )
{
	guard(AActor::execPlayLIPSincAnim);
	
	P_GET_NAME(LIPSincAnimName);
	P_GET_FLOAT_OPTX(Volume,1.f);
	P_GET_FLOAT_OPTX(Radius,1.f);
	P_GET_FLOAT_OPTX(Pitch,1.f);
	P_FINISH;

#ifdef WITH_LIPSINC
	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()) )
	{
		((USkeletalMeshInstance*)Mesh->MeshGetInstance(this))->PlayLIPSincAnim(LIPSincAnimName, Volume, Radius ? Radius : GAudioDefaultRadius, Pitch);
	}
	else
	{
		debugf(TEXT("%s is not a USkeletalMesh: PlayLIPSicAnim"),GetName());
	}
#endif	
	
	unguardexec;
}

void AActor::execIsPlayingLIPSincAnim( FFrame& Stack, RESULT_DECL )
{
	guard(AActor::execIsPlayingLIPSincAnim);

	P_FINISH;

#ifdef WITH_LIPSINC
	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()) )
	{
		*(DWORD*)Result = ((USkeletalMeshInstance*)Mesh->MeshGetInstance(this))->IsPlayingLIPSincAnim();
	}
	else
	{
		debugf(TEXT("%s is not a USkeletalMesh: IsPlayingLIPSincAnim"),GetName());
	}
#endif

	unguardexec;
}

void AActor::execCurrentLIPSincAnim( FFrame& Stack, RESULT_DECL )
{
	guard(AActor::execCurrentLIPSincAnim);

	P_FINISH;

#ifdef WITH_LIPSINC
	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()) )
	{
		*(FString*)Result = ((USkeletalMeshInstance*)Mesh->MeshGetInstance(this))->CurrentLIPSincAnim();
	}
	else
	{
		debugf(TEXT("%s is not a USkeletalMesh: CurrentLIPSincAnim"),GetName());
	}
#endif

	unguardexec;
}

void AActor::execHasLIPSincAnim( FFrame& Stack, RESULT_DECL )
{
	guard(AActor::execHasLIPSincAnim);

	P_GET_NAME(LIPSincAnimName);
	P_FINISH;

#ifdef WITH_LIPSINC
	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()) )
	{
		*(DWORD*)Result = ((USkeletalMeshInstance*)Mesh->MeshGetInstance(this))->HasLIPSincAnim( LIPSincAnimName );
	}
	else
	{
		debugf(TEXT("%s is not a USkeletalMesh: HasLIPSincAnim"),GetName());
	}
#endif

	unguardexec;
}

void AActor::execStopLIPSincAnim( FFrame& Stack, RESULT_DECL )
{
	guard(AActor::execStopLIPSincAnim);

	P_FINISH;

#ifdef WITH_LIPSINC
	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()) )
	{
		*(DWORD*)Result = ((USkeletalMeshInstance*)Mesh->MeshGetInstance(this))->StopLIPSincAnim();
	}
	else
	{
		debugf(TEXT("%s is not a USkeletalMesh: StopLIPSincAnim"),GetName());
	}
#endif

	unguardexec;
}

void AActor::execLoopAnim( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execLoopAnim);

	P_GET_NAME(SequenceName);
	P_GET_FLOAT_OPTX(PlayAnimRate,1.f);
	P_GET_FLOAT_OPTX(TweenTime,0.f);
	P_GET_INT_OPTX(Channel, 0);
	P_FINISH;

    if (SequenceName != NAME_None) {
        PlayAnim(Channel,SequenceName,PlayAnimRate,TweenTime,1);
    }

	unguardexecSlow;
}

void AActor::execTweenAnim( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execTweenAnim);

	P_GET_NAME(SequenceName);
	P_GET_FLOAT(TweenTime);
	P_GET_INT_OPTX(Channel, 0);
	P_FINISH;

    if (SequenceName != NAME_None) {
        PlayAnim(Channel,SequenceName,0.f,TweenTime,0);
    }

	unguardexecSlow;
}

void AActor::execIsAnimating( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execIsAnimating);

	P_GET_INT_OPTX(Channel, 0);
	P_FINISH;

	*(DWORD*)Result = IsAnimating(Channel);

	unguardexecSlow;
}

void AActor::execStopAnimating( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execStopAnimating);

	P_GET_UBOOL_OPTX(ClearAllButBase, 0);
	P_FINISH;

	//scion superville: accessor function
	StopAnimating( ClearAllButBase );

	unguardexecSlow;
}

// GDC-hackish..
void AActor::execFreezeAnimAt( FFrame& Stack, RESULT_DECL )
{
	guardSlow( AActor::execFreezeAtAnimFrame);
	P_GET_FLOAT( Time );
	P_GET_INT_OPTX( Channel, 0 )
	P_FINISH;
	
	//scion superville: accessor function
	FreezeAnimAt( Time, Channel );

	unguardexecSlow;
}

void AActor::execSetAnimFrame( FFrame& Stack, RESULT_DECL )
{
	guardSlow( AActor::execSetAnimFrame);
	P_GET_FLOAT( Frame );
	P_GET_INT_OPTX( Channel, 0 )
	P_GET_INT_OPTX( UnitFlag, 0 )
	P_FINISH;
	
	//scion superville: accessor function
	SetAnimFrame( Channel, Frame, UnitFlag );

	unguardexecSlow;
}

// return true if than channel is animating, and animframe < 0
void AActor::execIsTweening( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execIsTweening);

	P_GET_INT(Channel);
	P_FINISH;

	*(DWORD*)Result = 0;

	if( Mesh )
	{
		Mesh->MeshGetInstance(this);
		*(DWORD*)Result = ( MeshInstance->IsAnimTweening( Channel ) ) ? 1 : 0;
	}
	
	unguardexecSlow;
}

void AActor::execHasAnim( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execHasAnim);

	P_GET_NAME(SequenceName);
	P_FINISH;

	// Check for a certain anim sequence.
	if( Mesh )
	{
		Mesh->MeshGetInstance(this);
		HMeshAnim HSeq = MeshInstance->GetAnimNamed( SequenceName );
		if( HSeq )
		{
			*(DWORD*)Result = 1;
		} else
			*(DWORD*)Result = 0;
	} else Stack.Logf( TEXT("HasAnim: No mesh") );
	unguardexecSlow;
}

void AActor::execLinkSkelAnim( FFrame& Stack, RESULT_DECL )
{
	guard(AActor::execLinkSkelAnim);	

	P_GET_OBJECT(UMeshAnimation, NewAnim);
	P_GET_OBJECT_OPTX(USkeletalMesh, NewMesh, NULL);
	P_FINISH;	

	check(!Mesh || Mesh->IsValid());

	// Set main skel anim for this instance.
	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()) )
	{
		USkeletalMeshInstance* MeshInstance = (USkeletalMeshInstance*)Mesh->MeshGetInstance(this);
		check(MeshInstance);
		if (!MeshInstance->SetSkelAnim(NewAnim, NewMesh))
		{
			debugf(TEXT("Trying to assign invalid animation object or into a bad channel for MeshInstance [%s]"), Mesh->MeshGetInstance(this)->GetName());
		}
	}
	else
	{
		debugf(NAME_Warning, TEXT("Attempt to assign MeshAnimation to actor [%s] with no mesh!"), GetName());
	}

	unguardexec;
}

#ifdef WITH_KARMA

static UBOOL MeshIsCompatible(UMesh* Mesh, UMesh* newMesh)
{
	guard(MeshIsCompatible);

	// Check we have two skeletal meshes - otherwise deemed 'incompatible'
	if(!newMesh || !Mesh)
		return 0;

	USkeletalMesh* skelMesh = Cast<USkeletalMesh>(Mesh);
	USkeletalMesh* newSkelMesh = Cast<USkeletalMesh>(newMesh);
	if(!skelMesh || !newSkelMesh)
		return 0;

	// Must have same number of bones
	if( newSkelMesh->RefSkeleton.Num() != skelMesh->RefSkeleton.Num() )
		return 0;

	// Check heirarchy
	for(INT i=0; i<skelMesh->RefSkeleton.Num(); i++)
	{
		if( skelMesh->RefSkeleton(i).ParentIndex != newSkelMesh->RefSkeleton(i).ParentIndex )
			return 0;
	}

	return 1;

	unguard;
}

#endif // WITH_KARMA

void AActor::execLinkMesh( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execLinkMesh);	
	P_GET_OBJECT(UMesh, NewMesh);
	P_GET_UBOOL_OPTX( bKeepAnim, 0);
	P_FINISH;	

	if ( Mesh != NewMesh )
	{
		if( NewMesh && NewMesh->IsA(USkeletalMesh::StaticClass()) )
		{
			// If we are currently in ragdoll, check the mesh has the same heirarchy before replacing it.
			if(Mesh)
			{
#ifdef WITH_KARMA
				USkeletalMeshInstance* inst = (USkeletalMeshInstance*)Mesh->MeshGetInstance(this);

				// If we are currently running ragdoll, skeletons have to exactly match
				if(Physics == PHYS_KarmaRagDoll && inst && inst->KSkelIsInitialised)
				{
					if( !MeshIsCompatible( Mesh, NewMesh ) )
						return;
				}
#endif

				//#Skel - Other subclasses need specific setmesh to update meshinstances properly.
				if( bKeepAnim )
					((USkeletalMeshInstance*)Mesh->MeshGetInstance(this))->SetMesh( NewMesh );
			}
		}
		Mesh = NewMesh;
		bNetDirty = true;
	}
	unguardexecSlow;
}

void AActor::execBoneRefresh( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execBoneRefresh);	
	P_FINISH;		
	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()) )
	{
		((USkeletalMeshInstance*)Mesh->MeshGetInstance(this))->ForceBoneRefresh();
	}		
	else
	{
		debugf(TEXT("Actor %s has no valid skeletal mesh yet !!!"),this->GetName()); //#SKEL
	}
	unguardexecSlow;
}

void AActor::execHasBone( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execHasBone);
	P_GET_NAME(BoneName);
	P_FINISH;

	if(Mesh && Mesh->IsA(USkeletalMesh::StaticClass()) ) {
		Mesh->MeshGetInstance(this);
		INT BoneIdx = ((USkeletalMeshInstance*)MeshInstance)->MatchRefBone( BoneName );
		*(UBOOL*)Result = (BoneIdx > -1);	
	}
	else {
		*(UBOOL*)Result = 0;	
	}
	unguardexecSlow;
}

void AActor::execGetBoneCoords( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execGetBoneCoords);
	P_GET_NAME(BoneName);
	P_FINISH;
	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()) )
	{				
		Mesh->MeshGetInstance(this);
		INT BoneIdx = ((USkeletalMeshInstance*)MeshInstance)->MatchRefBone( BoneName );
		if( BoneIdx >= 0)
		{
			*(FCoords*)Result = ((USkeletalMeshInstance*)MeshInstance)->GetBoneCoords(BoneIdx);
		}
		else
		{
			*(FCoords*)Result = ((USkeletalMeshInstance*)MeshInstance)->GetBoneCoords(0);
			//debugf(TEXT("Warning: Invalid bone index (%i) for GetBoneRotation."),BoneIdx);
			//*(FCoords*)Result = GMath.UnitCoords;
		}
	}	
	unguardexecSlow;
}

void AActor::execGetBoneRotation( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execGetBoneRotation);
	P_GET_NAME(BoneName);
	P_GET_INT_OPTX( Space, 0 ); // 0 local, 1 global, 2 relative reference pose (?)
	P_FINISH;
	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()) )
	{				
		Mesh->MeshGetInstance(this);
		INT BoneIdx = ((USkeletalMeshInstance*)MeshInstance)->MatchRefBone( BoneName );
		if( BoneIdx >= 0)
		{
			*(FRotator*)Result = ((USkeletalMeshInstance*)MeshInstance)->GetBoneRotation(BoneIdx,Space);
		}
		else
		{
			debugf(TEXT("Warning: Invalid bone index (%i) for GetBoneRotation."),BoneIdx);
			*(FRotator*)Result = FRotator(0,0,0);
		}
	}	
	unguardexecSlow;
}

void AActor::execGetClosestBone( FFrame& Stack, RESULT_DECL )
{
    guard(AActor::execGetClosestBone);
    P_GET_VECTOR(loc);
    P_GET_VECTOR(ray);
    P_GET_FLOAT_REF(outboneDist);
    P_GET_NAME_OPTX(biasBone,NAME_None);
    P_GET_FLOAT_OPTX(biasDist,0.0f);
	P_FINISH;

    *(FName*)Result = NAME_None;

    UBOOL RayTest = 0;
    biasDist = Square(biasDist);
    FPlane RayPlane;
    if( ray.SizeSquared() > 0.0 )
    {
        RayPlane = FPlane(loc,ray);
        RayTest = 1;
    }

	guard(GetClosestBone Mesh access);
	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()) )
	{
		guard(1st if);
        USkeletalMeshInstance*	pInst = (USkeletalMeshInstance*)Mesh->MeshGetInstance(this);
        USkeletalMesh*			pSkel = (USkeletalMesh*)Mesh;

		if( pInst && pSkel )
		{
			// Evaluate skeleton if not already present / updated #debug - use instance's last game GTicks
			if(!pInst->SpaceBases.Num() || (pInst->LastGTicks < GTicks))
			{
				AActor* Owner = pInst->GetActor();
				if( Owner )
				{
					INT DummyVerts;
					guard(get frame)
					pInst->GetFrame( Owner, NULL, NULL, 0, DummyVerts, GF_BonesOnly);
					unguard;
				}
			}
			FName bestName = NAME_None;
			float bestDist = 999999.9f;
			FMatrix MeshToWorldMatrix = pInst->MeshToWorld();

			for( INT t=0; t<pSkel->TagAliases.Num(); t++)
			{
				INT BoneIdx = pInst->MatchRefBone( pSkel->TagAliases(t) );
				if( pInst->SpaceBases.Num() && BoneIdx != INDEX_NONE && BoneIdx < pInst->SpaceBases.Num() )
				{
					bool excludeBone = false;
					// exclude zero scale bones from this test and bones whose parent is zero scale
					for( int i=BoneIdx; i!=0; i=pSkel->RefSkeleton(i).ParentIndex )
					{
						for(INT s=0; s<pInst->Scalers.Num(); s++ )
						{
							if( pInst->Scalers(s).Bone == i && pInst->Scalers(s).ScaleUniform == 0.0f )
							{
								excludeBone = true;
								break;
							}
						}
						if( excludeBone )
							break;
					}
					if( excludeBone )
						continue;

					FVector Origin = MeshToWorldMatrix.TransformFVector( pInst->SpaceBases(BoneIdx).Origin );

					float dist = 0.0f;
					if (RayTest)
					{
						// calc distance to line along ray
						float planeDist = RayPlane.PlaneDot(Origin);
						if( planeDist <= 0.0f )
						{
							dist = (Origin - loc).SizeSquared();
						}
						else
						{
							FVector t = loc + RayPlane * planeDist;
							dist = (Origin - t).SizeSquared();
						}
					}
					else
					{
						dist = (Origin - loc).SizeSquared();
					}
					//debugf(TEXT("Bone: %s dist: %f"), *pSkel->TagAliases(t), appSqrt(dist));
					if( dist < bestDist )
					{
						bestName = pSkel->TagAliases(t);
						bestDist = dist;
					}
					// see if we have come near to bias bone
					if( biasBone != NAME_None && biasBone == pSkel->TagAliases(t) && dist <= biasDist )
					{
						//debugf(TEXT("Using Bias Bone: %s dist: %f"), *pSkel->TagAliases(t), appSqrt(dist));
						bestName = biasBone;
						bestDist = dist;
						*(FName*)Result = bestName;
						return;
					}
				}
			}
			*(FName*)Result = bestName;
		}
		unguard;
    }
    else if( Mesh )
	{
		guard(2nd if);
        int numAttach = Mesh->MeshGetInstance(this)->GetAttachCount();
        if ( numAttach )
        {
            FName name;
            FMatrix matrix;
            FName bestName = NAME_None;
            float bestDist = 99999.9f;
            for( int i=0; i<numAttach; i++ )
            {
                if( !Mesh->MeshGetInstance(this)->GetAttachIdx(i, name, matrix ) )
                    continue;

                FVector offset( matrix.M[3][0], matrix.M[3][1], matrix.M[3][2] );
                float dist = (offset - loc).Size();
                if( dist < bestDist )
                {
                    bestDist = dist;
                    bestName = name;
                    *outboneDist = dist;
                }
            }
            *(FName*)Result = bestName;
        }
		unguard;
    }
	unguard;
    unguard;
}



void AActor::execGetRootLocation( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execGetRootLocation);
	P_FINISH;
	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()) )
	{				
		Mesh->MeshGetInstance(this);
		*(FVector*)Result = ((USkeletalMeshInstance*)MeshInstance)->GetRootLocation();
	}
	unguardexecSlow;
}

void AActor::execAnimBlendParams( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execAnimBlendParams);
	P_GET_INT(Channel);
	P_GET_FLOAT_OPTX(BlendAlpha,0.0f);
	P_GET_FLOAT_OPTX(InRate,0.0f);
	P_GET_FLOAT_OPTX(OutRate,0.0f);
	P_GET_NAME_OPTX(StartBoneName, NAME_None);	
	P_GET_UBOOL_OPTX(bGlobalPose, 0);
	P_FINISH;
	
	//scion superville: Accessor function
	SetBlendParams( Channel, BlendAlpha, InRate, OutRate, StartBoneName, bGlobalPose );

	unguardexecSlow;
}

void AActor::execAnimBlendToAlpha( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execAnimBlendToAlpha);
	P_GET_INT(Channel);
	P_GET_FLOAT(TargetAlpha);
	P_GET_FLOAT(TimeInterval);	
	P_FINISH;
	
	//scion superville: Accessor function
	BlendToAlpha( Channel, TargetAlpha, TimeInterval );
	
	unguardexecSlow;
}

void AActor::execGetNotifyChannel( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execGetNotifyChannel);
	P_FINISH;

	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()) )
	{				
		*(DWORD*)Result = (DWORD)((USkeletalMeshInstance*)Mesh->MeshGetInstance(this))->LastNotifyStage;
	}
	else
	*(DWORD*)Result = 0; //Default stage.

	unguardexecSlow;	
}

void AActor::execEnableChannelNotify( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execEnableChannelNotify);
	P_GET_INT(Channel);	
	P_GET_INT_OPTX(Switch,1);	
	P_FINISH;

	//scion superville: Accessor function
	EnableChannelNotify( Channel, Switch );

	unguardexecSlow;	
}

void AActor::execSetBoneScale( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetBoneScale);
	P_GET_INT(Slot);
	P_GET_FLOAT_OPTX(BoneScale, 1.0f);
	P_GET_NAME_OPTX(BoneName,NAME_None);	
	P_FINISH;
	// Set or clear bone scaler.
	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()))
	{
		((USkeletalMeshInstance*)Mesh->MeshGetInstance(this))->SetBoneScale( Slot, BoneScale, BoneName);
	}
	unguardexecSlow;
}

void AActor::execSetBoneLocation( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetBoneLocation);
	//P_GET_INT(Slot);
	P_GET_NAME( BoneName );	
	P_GET_VECTOR_OPTX( BoneTrans, FVector(0.0f,0.0f,0.0f) );
	P_GET_FLOAT_OPTX( Alpha, 1.0f );
	P_FINISH;

	// Set or clear bone locator.
	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()))
	{
		((USkeletalMeshInstance*)Mesh->MeshGetInstance(this))->SetBoneLocation( BoneName, BoneTrans, Alpha );
	}
	unguardexecSlow;
}

void AActor::execSetBoneRotation( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetBoneRotation);
	P_GET_NAME( BoneName );	
	P_GET_ROTATOR_OPTX( BoneTurn, FRotator(0,0,0) );
	P_GET_INT_OPTX( Space, 0 );   // 0 local, 1 global
	P_GET_FLOAT_OPTX( Alpha, 1.0f );
	P_FINISH;

	// Set or clear bone rotator.
	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()))
	{
		((USkeletalMeshInstance*)Mesh->MeshGetInstance(this))->SetBoneRotation( BoneName, BoneTurn, Space, Alpha );
	}
	unguardexecSlow;	
}


// native final function bool SetBoneDirection( name BoneName, rotator BoneTurn,  optional vector BoneTrans, optional float Alpha );

// 'Bend' bone direction (axis-angle style?) in world coordinates...
void AActor::execSetBoneDirection( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetBoneDirection);
	//P_GET_INT(Slot);
	P_GET_NAME( BoneName );	
	P_GET_ROTATOR( BoneTurn );
	P_GET_VECTOR_OPTX( BoneTrans, FVector(0.0f,0.0f,0.0f) );
	P_GET_FLOAT_OPTX( Alpha, 1.0f );
	P_GET_INT_OPTX( Space, 0 );   // 0 local, 1 global
	P_FINISH;	

	// Set or clear bone direction controller.
	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()))
	{
		((USkeletalMeshInstance*)Mesh->MeshGetInstance(this))->SetBoneDirection( BoneName, BoneTurn, BoneTrans, Alpha, Space);
	}
	unguardexecSlow;	
}

// native final function GetAnimParams( int Channel, out name Sequence, out float AnimFrame, out float AnimRate );

void AActor::execGetAnimParams( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execGetAnimParams);
	P_GET_INT(Channel);
	P_GET_NAME_REF( OutSeqName );
	P_GET_FLOAT_REF( OutAnimFrame );
	P_GET_FLOAT_REF( OutAnimRate );
	//scion superville: retrieve alpha also
	P_GET_FLOAT_REF( OutAnimAlpha );
	P_FINISH;	

	*OutSeqName = NAME_None;
	*OutAnimFrame = 0.0f;
	*OutAnimRate = 0.0f;

	if( Mesh )		
	{	
		Mesh->MeshGetInstance(this);

		*OutSeqName = MeshInstance->GetActiveAnimSequence( Channel );
		*OutAnimFrame = MeshInstance->GetActiveAnimFrame( Channel );
		*OutAnimRate = MeshInstance->GetActiveAnimRate( Channel );
		
		//scion superville: retrieve alpha also
		USkeletalMeshInstance* SkelInstance = Cast<USkeletalMeshInstance>(MeshInstance);
		if( SkelInstance )
			*OutAnimAlpha = SkelInstance->GetBlendAlpha( Channel );
		else
			*OutAnimAlpha = 0;
	}


	unguardexecSlow;	
}


// native final function bool AnimIsInGroup( int Channel, name GroupName );
// Determine if currently playing animation of channel (default 0) is member of given group

void AActor::execAnimIsInGroup( FFrame& Stack, RESULT_DECL )
{
	guardSlow( AActor::execAnimIsInGroup );
	P_GET_INT( Channel );
	P_GET_NAME( GroupName )
	P_FINISH;	

	*(DWORD*)Result = 0;
	if( Mesh )
	{		
		Mesh->MeshGetInstance(this);
		FName ThisAnimSeq = NAME_None;
		ThisAnimSeq = MeshInstance->GetActiveAnimSequence( Channel );
		if( ThisAnimSeq != NAME_None )
		{
			HMeshAnim HSeq = MeshInstance->GetAnimNamed( ThisAnimSeq );
			if( HSeq )
			{
				*(DWORD*)Result = MeshInstance->AnimIsInGroup(HSeq, GroupName)? 1 : 0 ;
			}
		}
	}
	unguardexecSlow;	
}

///////////////
// Collision //
///////////////

void AActor::execSetCollision( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetCollision);

	P_GET_UBOOL_OPTX(NewCollideActors,bCollideActors);
	P_GET_UBOOL_OPTX(NewBlockActors,  bBlockActors  );
	P_GET_UBOOL_OPTX(NewBlockPlayers, bBlockPlayers );
	P_FINISH;

	SetCollision( NewCollideActors, NewBlockActors, NewBlockPlayers );

	unguardexecSlow;
}

void AActor::execOnlyAffectPawns( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execOnlyAffectPawns);

	P_GET_UBOOL(bNewAffectPawns);
	P_FINISH;

	if( bCollideActors && (bNewAffectPawns != bOnlyAffectPawns))
	{
		SetCollision(false, bBlockActors, bBlockPlayers);
		bOnlyAffectPawns = bNewAffectPawns;
		SetCollision(true, bBlockActors, bBlockPlayers);
	}

	unguardexecSlow;
}

void AActor::execSetCollisionSize( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetCollisionSize);

	P_GET_FLOAT(NewRadius);
	P_GET_FLOAT(NewHeight);
	P_FINISH;

	SetCollisionSize( NewRadius, NewHeight );

	// Return boolean success or failure.
	*(DWORD*)Result = 1;

	unguardexecSlow;
}

void AActor::execSetBase( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetFloor);

	P_GET_OBJECT(AActor,NewBase);
	P_GET_VECTOR_OPTX(NewFloor, GMath.UnitCoords.ZAxis );
	P_FINISH;

	SetBase( NewBase,NewFloor );

	unguardexecSlow;
}

void AActor::execSetDrawScale( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetDrawScale);

	P_GET_FLOAT(NewScale);
	P_FINISH;

	SetDrawScale(NewScale);

	unguardexecSlow;
}

void AActor::execSetStaticMesh( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetStaticMesh);

	P_GET_OBJECT(UStaticMesh,NewStaticMesh);
	P_FINISH;

	SetStaticMesh(NewStaticMesh);

	unguardexecSlow;
}

void AActor::execSetDrawType( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetDrawType);

	P_GET_BYTE(NewDrawType);
	P_FINISH;

	SetDrawType(EDrawType(NewDrawType));

	unguardexecSlow;
}

void AActor::execSetDrawScale3D( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetCollisionSize3D);

	P_GET_VECTOR(NewScale3D);
	P_FINISH;

	SetDrawScale3D(NewScale3D);

	unguardexecSlow;
}

//scion superville: Animation mirroring accessor functions
void AActor::SetAnimFrame( INT Channel, FLOAT NewFrame, INT UnitFlag )
{
	if( Mesh ) {
		Mesh->MeshGetInstance(this);
		if (MeshInstance)
		{
			MeshInstance->SetAnimFrame(Channel, NewFrame, UnitFlag);
		}
	}

	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetAnimFrame( Channel, NewFrame, UnitFlag );
			}
	}
}

void AActor::SetAnimRangeStart( INT Channel, FLOAT NewFrame )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	if( SkMeshInstance ) {
		SkMeshInstance->SetAnimRangeStart( Channel, NewFrame );
	}


	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetAnimRangeStart( Channel, NewFrame );
			}
	}
}

void AActor::SetAnimRangeEnd( INT Channel, FLOAT NewFrame )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	if( SkMeshInstance ) {
		SkMeshInstance->SetAnimRangeEnd( Channel, NewFrame );
	}


	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetAnimRangeEnd( Channel, NewFrame );
			}
	}
}

void AActor::SetCachedFrame( INT Channel, FLOAT NewFrame )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	if( SkMeshInstance ) {
		SkMeshInstance->SetCachedFrame( Channel, NewFrame );
	}


	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetCachedFrame( Channel, NewFrame );
			}
	}
}

void AActor::SetCachedSeq( INT Channel, FName NewSeq )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	if( SkMeshInstance ) {
		SkMeshInstance->SetCachedSeq( Channel, NewSeq );
	}

	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetCachedSeq( Channel, NewSeq );
			}
	}
}

void AActor::SetMeshAnimIndex( INT Channel, INT Index )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	if( SkMeshInstance ) {
		SkMeshInstance->SetMeshAnimIndex( Channel, Index );
	}

	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetMeshAnimIndex( Channel, Index );
			}
	}
}

void AActor::SetStartBone( INT Channel, INT NewBone )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	if( SkMeshInstance ) {
		SkMeshInstance->SetStartBone( Channel, NewBone );
	}

	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetStartBone( Channel, NewBone );
			}
	}
}

void AActor::SetTweening( INT Channel, UBOOL B )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	if( SkMeshInstance ) {
		SkMeshInstance->SetTweening( Channel, B );
	}

	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetTweening( Channel, B );
			}
	}
}

void AActor::SetAltRange( INT Channel, UBOOL B )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	if( SkMeshInstance ) {
		SkMeshInstance->SetAltRange( Channel, B );
	}

	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetAltRange( Channel, B );
			}
	}
}

void AActor::SetNotifyIgnore( INT Channel, UBOOL B )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	if( SkMeshInstance ) {
		SkMeshInstance->SetNotifyIgnore( Channel, B );
	}

	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetNotifyIgnore( Channel, B );
			}
	}
}

void AActor::SetAnimFinished( INT Channel, UBOOL B )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	if( SkMeshInstance ) {
		SkMeshInstance->SetAnimFinished( Channel, B );
	}

	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetAnimFinished( Channel, B );
			}
	}
}

void AActor::SetAnimLoop( INT Channel, UBOOL B )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	if( SkMeshInstance ) {
		SkMeshInstance->SetAnimLoop( Channel, B );
	}
	
	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetAnimLoop( Channel, B );
			}
	}
}
void AActor::SetTimedBlend( INT Channel, UBOOL B )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	if( SkMeshInstance ) {
		SkMeshInstance->SetTimedBlend( Channel, B );
	}

	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetTimedBlend( Channel, B );
			}
	}
}

void AActor::SetTweenProgress( INT Channel, FLOAT Time )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	if( SkMeshInstance ) {
		SkMeshInstance->SetTweenProgress( Channel, Time );
	}

	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetTweenProgress( Channel, Time );
			}
	}
}

void AActor::SetBlendInTime( INT Channel, FLOAT Time )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	if( SkMeshInstance ) {
		SkMeshInstance->SetBlendInTime( Channel, Time );
	}

	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetBlendInTime( Channel, Time );
			}
	}
}

void AActor::SetBlendOutTime( INT Channel, FLOAT Time )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	if( SkMeshInstance ) {
		SkMeshInstance->SetBlendOutTime( Channel, Time );
	}

	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetBlendOutTime( Channel, Time );
			}
	}
}

void AActor::SetBlendTargetAlpha( INT Channel, FLOAT Alpha )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	if( SkMeshInstance ) {
		SkMeshInstance->SetBlendTargetAlpha( Channel, Alpha );
	}

	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetBlendTargetAlpha( Channel, Alpha );
			}
	}
}

void AActor::SetBlendTargetInterval( INT Channel, FLOAT Time )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	if( SkMeshInstance ) {
		SkMeshInstance->SetBlendTargetInterval( Channel, Time );
	}

	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetBlendTargetInterval( Channel, Time );
			}
	}
}


void AActor::SetGlobalPose( INT Channel, UBOOL B )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	if( SkMeshInstance ) {
		SkMeshInstance->SetGlobalPose( Channel, B );
	}

	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetGlobalPose( Channel, B );
			}
	}
}

void AActor::SetAnimNotify( INT Channel, UBOOL B )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	if( SkMeshInstance ) {
		SkMeshInstance->SetAnimNotify( Channel, B );
	}

	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetAnimNotify( Channel, B );
			}
	}
}


void AActor::SetAnimLast( INT Channel, FLOAT Frame )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	if( SkMeshInstance ) {
		SkMeshInstance->SetAnimLast( Channel, Frame );
	}

	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetAnimLast( Channel, Frame );
			}
	}
}

void AActor::FreezeAnimAt( FLOAT Time, INT Channel )
{
	if( Mesh ) {
		Mesh->MeshGetInstance(this);
		if (MeshInstance)
		{
			MeshInstance->FreezeAnimAt(Time, Channel);
		}
	}

	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->FreezeAnimAt( Time, Channel );
			}
	}
}

void AActor::StopAnimating( UBOOL ClearAllButBase )
{
	// make all channels freeze in current pose
	if( Mesh )
	{
		Mesh->MeshGetInstance(this);
		
		USkeletalMeshInstance* SkelInstance = Cast<USkeletalMeshInstance>(MeshInstance);
		if(SkelInstance)
		{
			SkelInstance->StopAnimating(ClearAllButBase);
		}
		else if (MeshInstance)
		{
			MeshInstance->StopAnimating();
		}
	}

	//scion superville: Mirror animations
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->StopAnimating(ClearAllButBase);
			}
	}
}

UBOOL AActor::EnableChannelNotify( INT Channel, INT Switch )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);


	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->EnableChannelNotify( Channel, Switch );
			}
	}	
	
	if( SkMeshInstance )
		return SkMeshInstance->EnableChannelNotify( Channel, Switch );
		
	return 0;	
}

void AActor::BlendToAlpha( INT Channel, FLOAT TargetAlpha, FLOAT TimeInterval )
{
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->BlendToAlpha( Channel, TargetAlpha, TimeInterval );
			}
	}

	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()) ) {				
		Mesh->MeshGetInstance(this);
		// Assign the blend parameters to the current AnimStage channel.
		if (MeshInstance)
		{
			((USkeletalMeshInstance*)MeshInstance)->BlendToAlpha(Channel, TargetAlpha, TimeInterval);
		}
	}
	
}

UBOOL AActor::SetBlendParams( INT Channel, FLOAT BlendAlpha, FLOAT InTime, FLOAT OutTime, FName StartBoneName, UBOOL bGlobalPose )
{
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetBlendParams( Channel, BlendAlpha, InTime, OutTime, StartBoneName, bGlobalPose );
			}
	}

	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()) ) {				
		Mesh->MeshGetInstance(this);
		// Assign the blend parameters to the current AnimStage channel.
		if (MeshInstance)
		{
			return ((USkeletalMeshInstance*)MeshInstance)->SetBlendParams(Channel, BlendAlpha, InTime, OutTime, StartBoneName, bGlobalPose);
		}
	}
	
	return 0;
}

void AActor::SetBlendAlpha( INT Channel, FLOAT NewAlpha )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetBlendAlpha( Channel, NewAlpha );
			}
	}	
	
	if( SkMeshInstance )
		SkMeshInstance->SetBlendAlpha( Channel, NewAlpha );
}

void AActor::UpdateBlendAlpha( INT Channel, FLOAT GoalAlpha, FLOAT StepSize )
{
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->UpdateBlendAlpha( Channel, GoalAlpha, StepSize );
			}
	}	

	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);

	if( SkMeshInstance )
		SkMeshInstance->UpdateBlendAlpha( Channel, GoalAlpha, StepSize );
}

void AActor::SetAnimSequence( INT Channel, FName NewSequence )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetAnimSequence( Channel, NewSequence );
			}
	}	
	
	if( SkMeshInstance )
		SkMeshInstance->SetAnimSequence( Channel, NewSequence );
}

void AActor::SetAnimRate( INT Channel, FLOAT NewRate )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetAnimRate( Channel, NewRate );
			}
	}	
	
	if( SkMeshInstance )
		SkMeshInstance->SetAnimRate( Channel, NewRate );
}

void AActor::ScaleAnimRate( INT Channel, FLOAT Scale )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);

	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->ScaleAnimRate( Channel, Scale );
			}
	}	

	if( SkMeshInstance )
		SkMeshInstance->ScaleAnimRate( Channel, Scale );
}

void AActor::SetOldAnimRate( INT Channel, FLOAT NewRate )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);

	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetOldAnimRate( Channel, NewRate );
			}
	}	

	if( SkMeshInstance )
		SkMeshInstance->SetOldAnimRate( Channel, NewRate );
}

void AActor::SetOrigRate( INT Channel, FLOAT NewRate )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);

	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetOrigRate( Channel, NewRate );
			}
	}	

	if( SkMeshInstance )
		SkMeshInstance->SetOrigRate( Channel, NewRate );
}

void AActor::SetTweenRate( INT Channel, FLOAT NewRate )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);

	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->SetTweenRate( Channel, NewRate );
			}
	}	

	if( SkMeshInstance )
		SkMeshInstance->SetTweenRate( Channel, NewRate );
}


void AActor::CopyAnimation( INT SrcChannel, INT DestChannel, AActor* DestActor /*default arg = NULL*/ )
{
	USkeletalMeshInstance *SkMeshInstance = Cast<USkeletalMeshInstance>(MeshInstance);
	
	if( bMirrorAnimToAttachments ) {
		for( INT i = 0; i < Attached.Num(); i++ )
		{
			if (Attached(i) && Attached(i)->bMirrorAnimFromOwner && !Attached(i)->bAnimByOwner)
			{
				Attached(i)->CopyAnimation( SrcChannel, DestChannel, DestActor );
			}
		}
	}	
	
	if( SkMeshInstance )
	{
		SkMeshInstance->CopyAnimation( SrcChannel, DestChannel, DestActor );
	}
}


///////////
// Audio //
///////////

/* scion ===============================================
* ::execPlaySoundCue()
* Author: rayd
*
* See Actor.uc for function information.
*
* =====================================================
*/
void AActor::execPlaySoundCue(FFrame &Stack,RESULT_DECL)
{
	guardSlow(AActor::execPlaySoundCue);
	P_GET_NAME(cue);
	P_GET_UBOOL_OPTX(bAmbient,0);
	P_GET_FLOAT_OPTX(Volume,0.f);
	P_FINISH;

	// Don't play any sounds if actor has them disabled
	if( bDisableActorSounds								|| 
		(Owner		&& Owner->bDisableActorSounds)		|| 
		(Instigator && Instigator->bDisableActorSounds) )
	{
		if (bAmbient)
		{
			AmbientSoundCue = cue;
		}
		return;
	}

	// if passed an ambient, then just call PlayAmbientSoundCue()
	if (bAmbient)
	{
		PlayAmbientSoundCue(cue);
	}
	else
	if (cue != NAME_None &&
		GetLevel()->Engine->Audio != NULL)
	{
		// otherwise play the sound normally
		GetLevel()->Engine->Audio->PlaySoundCue(this, cue, 0, Volume);
	}
	unguardexecSlow;
}

/* scion ===============================================
* ::execLocalPlaySoundCue()
* Author: rayd
*
* See Actor.uc for function information.
*
* =====================================================
*/
void AActor::execLocalPlaySoundCue(FFrame &Stack,RESULT_DECL)
{
	guardSlow(AActor::execLocalPlaySoundCue);
	P_GET_NAME(cue);
	P_GET_UBOOL_OPTX(bAmbient,0);
	P_GET_FLOAT_OPTX(Volume,0.f);
	P_FINISH;

	// Don't play any sounds if actor has them disabled
	if( bDisableActorSounds								|| 
		(Owner		&& Owner->bDisableActorSounds)		|| 
		(Instigator && Instigator->bDisableActorSounds) )
	{
		if (bAmbient)
		{
			AmbientSoundCue = cue;
		}
		return;
	}

	// if passed an ambient, then just call PlayAmbientSoundCue()
	if (bAmbient)
	{
		PlayAmbientSoundCue(cue);
	}
	else
	if (cue != NAME_None &&
		GetLevel()->Engine->Audio != NULL)
	{
		GetLevel()->Engine->Audio->PlaySoundCue(this, cue, 0, Volume);
	}
	unguardexecSlow;
}

/* scion ===============================================
* ::PlayAmbientSoundCue
* Author: rayd
*
* Sets the ambient sound cue to the specified cue, and
* notifies the audio system of the change.
*
* =====================================================
*/
void AActor::PlayAmbientSoundCue(FName cue)
{
	guardSlow(AActor::PlayAmbientSoundCue);

	// if we're not already playing this ambient sound
	if (cue != AmbientSoundCue)
	{
		// if audio has been initialized
		if (GetLevel()->Engine->Audio != NULL)
		{
			debugf(NAME_DevSound, TEXT("%s PlayAmbientSoundCue %s, old ambient: %s"), GetName(), *cue, *AmbientSoundCue);
			// notify the audio system that an ambient has been played
			GetLevel()->Engine->Audio->PlaySoundCue(this, cue, 1);
		}
		// set the new snd as our current ambient sound for reference
		AmbientSoundCue = cue;
	}

	unguardSlow;
}

// warning: must match function parameters of script PlaySoundCue() function
struct FPlaySoundFuncParms
{
	FName cue;
	UBOOL bAmbient;
	FLOAT Volume;
};

void AActor::execRadiusPlaySoundCue(FFrame &Stack,RESULT_DECL)
{
	guard(AActor::execRadiusPlaySoundCue);
	// read the function params
	P_GET_NAME(cue);
	P_FINISH;

	// Don't play any sounds if actor has them disabled
	if( bDisableActorSounds								|| 
		(Owner		&& Owner->bDisableActorSounds)		|| 
		(Instigator && Instigator->bDisableActorSounds) )
	{
		return;
	}

	if (Role == ROLE_Authority && cue != NAME_None)
	{
		// grab the radius for this cue
		FLOAT cueRadius = 0.f;
		if (GetLevel()->Engine->Audio != NULL)
		{
			cueRadius = GetLevel()->Engine->Audio->GetSoundCueRadius(cue);
			// always play the sound locally
			GetLevel()->Engine->Audio->PlaySoundCue(this, cue);
		}
#ifndef _XBOX
		// dedicated server on pc hack, fake a default radius
		if (Level->NetMode == NM_DedicatedServer)
		{
			cueRadius = 2500.f;
		}
#endif
		// if the cue has a radius and we are a server,
		if (cueRadius > 0.f &&
			(Level->NetMode == NM_DedicatedServer || Level->NetMode == NM_ListenServer))
		{
			// can't replicate the call if we're bDeleteMe; log a warning instead
			if (bDeleteMe)
			{
				debugf(NAME_Warning, TEXT("Attempt to replicate sound %s on destroyed Actor %s from %s"), *cue, GetName(), Stack.Node->GetFullName());
			}
			else
			{
				cueRadius *= cueRadius;
				// look up the function we are going to replicate if necessary
				// warning: assumes PlaySoundCue() is final
				static UFunction* playSoundFunc = FindFunction(FName(TEXT("PlaySoundCue")));;
				// setup the parms
				FPlaySoundFuncParms funcParams;
				funcParams.cue = cue;
				funcParams.bAmbient = 0;
				funcParams.Volume = 0.f;
				// check each controller,
				for (AController *controller = Level->ControllerList; controller != NULL; controller = controller->nextController)
				{
					APlayerController *playerController = controller->IsAPlayerController() ? (APlayerController*)controller : NULL;
					// if this is a valid remote client,
					if (playerController != NULL &&
						playerController->Player != NULL &&
						playerController->Player->IsA(UNetConnection::StaticClass()))
					{
						AActor *testActor = NULL;
						// compiler was getting messed up using ?, so please forgive the mess
						if (playerController->ViewTarget != NULL)
						{
							testActor = playerController->ViewTarget;
						}
						else
						{
							testActor = playerController;
						}
						// if they are within the radius,
						if (IsOwnedBy(testActor) || (testActor->Location - Location).SizeSquared() <= cueRadius)
						{
							// replicate the function call
							InternalProcessRemoteFunction(this, (UNetConnection*)playerController->Player, playSoundFunc, &funcParams, NULL, 1);
						}
					}
				}
			}
		}
	}
	unguardexec;
}

void AActor::execLocalStopSoundCue(FFrame &Stack, RESULT_DECL)
{
	P_GET_NAME(cue);
	P_FINISH;
	/*
	if (GetLevel()->Engine->Audio != NULL)
	{
        GetLevel()->Engine->Audio->StopSoundCue(this,cue);
	}
	*/
}

UAudioSubsystem::EVolumeCategory GetVolumeCategory(const TCHAR *category)
{
	if (!appStricmp(category,TEXT("Music")))
	{
		return UAudioSubsystem::VOLUME_Music;
	}
	else
	if (!appStricmp(category,TEXT("FX")))
	{
		return UAudioSubsystem::VOLUME_FX;
	}
	else
	if (!appStricmp(category,TEXT("Voice")))
	{
		return UAudioSubsystem::VOLUME_Voice;
	}
	else if ( !appStricmp(category,TEXT("VoiceChat")) )
	{
		return UAudioSubsystem::VOLUME_VoiceChat;
	}
	// error?
	return UAudioSubsystem::VOLUME_Music;
}

void AActor::execSetCategoryVolume(FFrame &Stack,RESULT_DECL)
{
	P_GET_NAME(category);
	P_GET_FLOAT(newVolume);
	P_FINISH;
	if (GetLevel()->Engine->Audio != NULL)
	{
		GetLevel()->Engine->Audio->SetCategoryVolume(GetVolumeCategory(*category),newVolume);
	}
}

void AActor::execGetCategoryVolume(FFrame &Stack,RESULT_DECL)
{
	P_GET_NAME(category);
	P_FINISH;
	if (GetLevel()->Engine->Audio != NULL)
	{
		*(FLOAT*)Result = GetLevel()->Engine->Audio->GetCategoryVolume(GetVolumeCategory(*category));
	}
}

void AActor::execGetSoundCueDuration(FFrame &Stack,RESULT_DECL)
{
	guard(AActor::GetSoundCueDuration);
	P_GET_NAME(cue);
	P_FINISH;
	FLOAT duration = 0.f;
	if (cue != NAME_None &&
		GetLevel()->Engine->Audio != NULL)
	{
		duration = GetLevel()->Engine->Audio->GetSoundCueDuration(cue);
	}
	*(FLOAT*)Result = duration;
	unguard;
}

void AActor::execGetSoundCueRadius(FFrame &Stack,RESULT_DECL)
{
	guard(AActor::GetSoundCueRadius);
	P_GET_NAME(cue);
	P_FINISH;
	FLOAT radius = 0.f;
	if (cue != NAME_None &&
		GetLevel()->Engine->Audio != NULL)
	{
		radius = GetLevel()->Engine->Audio->GetSoundCueRadius(cue);
	}
	*(FLOAT*)Result = radius;
	unguard;
}

void AActor::execSetActorSounds(FFrame& Stack, RESULT_DECL)
{
	guard(AActor::SetActorSounds);

	P_GET_UBOOL(bEnable);
	P_GET_OBJECT_OPTX(AActor, IgnoreActor, NULL);
	P_FINISH;

	// if we're enabling and sounds are currently disabled or vice-versa
	if (bEnable == bDisableActorSounds)
	{
		// iterate through all dynamic actors
		for (INT i = GetLevel()->iFirstDynamicActor; i < GetLevel()->Actors.Num(); i++)
		{
			AActor* TestActor = GetLevel()->Actors(i);
			// if this actor is, is owned by, or has Instigator of this actor and it has an ambient sound
			if ( TestActor && (TestActor == this || TestActor->Owner == this || TestActor->Instigator == this) &&
				TestActor != IgnoreActor && TestActor->AmbientSoundCue != NAME_None )
			{
				// if we're disabling sounds, turn off the ambient, otherwise turn it back on
				if (bEnable)
				{
					//clear AmbientSoundCue on the actor so the sound system doesn't get confused and think there's
					//already a sound playing
					FName AmbientCue = TestActor->AmbientSoundCue;
					TestActor->AmbientSoundCue = NAME_None;
					GetLevel()->Engine->Audio->PlaySoundCue(TestActor, AmbientCue, 1);
				}
				else
				{
					// NOTE: When disabling, AmbientSoundCue is intentionally left alone on the actor so we know
					// what cue to restore when sounds are reenabled
					GetLevel()->Engine->Audio->PlaySoundCue(TestActor, NAME_None, 1);
				}
			}
		}

		bDisableActorSounds = !bEnable;
	}

	unguardexec;
}

void AActor::execPlayMusic(FFrame &Stack,RESULT_DECL)
{
	P_GET_STR(Song);
	P_GET_FLOAT(FadeTime);
	P_FINISH;
	if (GetLevel()->Engine->Audio != NULL)
	{
		*(INT*)Result = GetLevel()->Engine->Audio->PlayMusic(Song,FadeTime);
	}
}

/**
* Stops any ambient sounds so they no longer stream from DVD while either
* caching or loading of the loading movie is going on
* 
* @param Level the level to iterate actors on
*/
void StopAmbientSounds(ULevel* Level)
{
	guard(StopAmbientSounds);
	// Iterate the actor list stopping their sounds
	for (INT Index = 0; Index < Level->Actors.Num(); Index++)
	{
		AActor* Actor = Level->Actors(Index);
		// If the actor is playing an ambient, snuff it out
		if (Actor != NULL && Actor->AmbientSoundCue != NAME_None)
		{
			// Stop playing please
			Level->Engine->Audio->PlaySoundCue(Actor,NAME_None,TRUE,0.f);
			// And do not play ever again
			Actor->AmbientSoundCue = NAME_None;
		}
	}
	unguard;
}

void AActor::execStopMusic(FFrame &Stack,RESULT_DECL)
{
	P_GET_INT(Handle);
	P_GET_FLOAT(FadeTime);
	P_FINISH;
	if (GetLevel()->Engine->Audio != NULL)
	{
		GetLevel()->Engine->Audio->StopMusic(Handle,FadeTime);
	}
}

void AActor::execStopAllMusic(FFrame &Stack,RESULT_DECL)
{
	P_GET_FLOAT(FadeTime);
	P_FINISH;
	if (GetLevel()->Engine->Audio != NULL)
	{
		GetLevel()->Engine->Audio->StopAllMusic(FadeTime);
	}
}

void AActor::execStopAmbientSounds( FFrame& Stack, RESULT_DECL )
{
	guard(AActor::execStopAmbientSounds);

	P_FINISH;

	StopAmbientSounds(GetLevel());

	unguardexec;
}

void APlayerController::execGetBuildVersion( FFrame& Stack, RESULT_DECL )
{
	guard(AActor::execGetBuildVersion);

	P_FINISH;

	*(FString*)Result = LocalizeGeneral( TEXT("BuildVersion"),  TEXT("Core"));
		
	unguardexec;
}

//////////////
// Movement //
//////////////

void AActor::execMove( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execMove);

	P_GET_VECTOR(Delta);
	P_FINISH;

	FCheckResult Hit(1.0f);
	*(DWORD*)Result = GetLevel()->MoveActor( this, Delta, Rotation, Hit );

	unguardexecSlow;
}

void AActor::execSetLocation( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetLocation);

	P_GET_VECTOR(NewLocation);
	P_GET_UBOOL_OPTX(bDoChecks, 1); //epic moelfke: added bDoChecks - whether or not to check collision/encroachment
	P_GET_UBOOL_OPTX(bKeepBase, 0); //epic moelfke: added bKeepBase - whether or not to keep Base
	P_FINISH;
	*(DWORD*)Result = GetLevel()->FarMoveActor(this, NewLocation, 0, !bDoChecks, bKeepBase);
	unguardexecSlow;
}

void AActor::execSetRelativeLocation( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetLocation);

	P_GET_VECTOR(NewLocation);
	P_FINISH;

	if ( AttachmentBone != NAME_None )
	{
		RelativeLocation = NewLocation;
		*(DWORD*)Result = 1;
	}
	else if ( Base )
	{
		if(bHardAttach && !bBlockActors && !bBlockPlayers)
		{
			/* nuking HardRelMatrix from actor
			HardRelMatrix.M[3][0] = NewLocation.X;
			HardRelMatrix.M[3][1] = NewLocation.Y;
			HardRelMatrix.M[3][2] = NewLocation.Z;
			RelativeLocation	  = NewLocation;
			
			// Work out what the new child rotation is
			FMatrix BaseTM		= FRotationMatrix(Base->Rotation) * FTranslationMatrix(Base->Location);
			FMatrix NewWorldTM	= HardRelMatrix * BaseTM;
			
			NewLocation = FVector(NewWorldTM.M[3][0],NewWorldTM.M[3][1],NewWorldTM.M[3][2]);
			*(DWORD*)Result = GetLevel()->FarMoveActor( this, NewLocation ,false,false,true );
			*/

			// Remember new relative position
			RelativeLocation = NewLocation;

			// Using this new relative transform, calc new wold transform and update current position to match.
			FMatrix HardRelMatrix = FMatrix(RelativeLocation, RelativeRotation);
			FMatrix BaseTM = FMatrix(Base->Location, Base->Rotation);
			FMatrix NewWorldTM = HardRelMatrix * BaseTM;

			NewLocation = NewWorldTM.GetOrigin();
			*(DWORD*)Result = GetLevel()->FarMoveActor( this, NewLocation,false,false,true );
		}
		else 
		{
			RelativeLocation = NewLocation;
			FCoords Coords	= GMath.UnitCoords / Base->Rotation;
			NewLocation		= Base->Location + NewLocation.X * Coords.XAxis + NewLocation.Y * Coords.YAxis + NewLocation.Z * Coords.ZAxis;
			*(DWORD*)Result = GetLevel()->FarMoveActor( this, NewLocation ,false,false,true );
		}
	}

	unguardexecSlow;
}

void AActor::execSetRotation( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetRotation);

	P_GET_ROTATOR(NewRotation);
	P_GET_UBOOL_OPTX(bUpdateAttachments,0);
	P_FINISH;

	FCheckResult Hit(1.0f);
	*(DWORD*)Result = GetLevel()->MoveActor( this, FVector(0,0,0), NewRotation, Hit );

	//scion superville: Force attachment updates if necessary
	if( bUpdateAttachments )
	{
		UpdateAttachments();
	}
	
	unguardexecSlow;
}

void AActor::execSetRelativeRotation( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetRelativeRotation);

	P_GET_ROTATOR(NewRotation);
	P_FINISH;

	if ( AttachmentBone != NAME_None )
	{
		RelativeRotation = NewRotation;
		*(DWORD*)Result = 1;
	}
	else
	{
		if ( Base )
		{
			if(bHardAttach && !bBlockActors && !bBlockPlayers)
			{
				/* nuking HardRelMatrix from actor
				FVector currentRelLoc;
				currentRelLoc.X = HardRelMatrix.M[3][0];
				currentRelLoc.Y = HardRelMatrix.M[3][1];
				currentRelLoc.Z = HardRelMatrix.M[3][2];
	
				// We make a new HardRelMatrix using the new rotation and the existing position.
				HardRelMatrix = FRotationMatrix(NewRotation) * FTranslationMatrix(currentRelLoc);

				// Work out what the new chile rotation is
				FMatrix BaseTM = FRotationMatrix(Base->Rotation) * FTranslationMatrix(Base->Location);
				FMatrix NewWorldTM = HardRelMatrix * BaseTM;
				NewRotation = (GMath.UnitCoords * NewWorldTM.Coords()).OrthoRotation();
				*/
				// We make a new HardRelMatrix using the new rotation and the existing position.
				FMatrix HardRelMatrix = FMatrix(RelativeLocation, NewRotation);
				RelativeLocation = HardRelMatrix.GetOrigin();
				RelativeRotation = HardRelMatrix.Rotator();
	
				// Work out what the new chile rotation is
				FMatrix BaseTM = FMatrix(Base->Location, Base->Rotation);
				FMatrix NewWorldTM = HardRelMatrix * BaseTM;
				NewRotation = NewWorldTM.Rotator();
			}
			else
			{
			FCoords Coords = GMath.UnitCoords / Base->Rotation;
			FCoords PartCoords = GMath.UnitCoords / NewRotation;
			PartCoords = PartCoords * Coords.Transpose();
			NewRotation = PartCoords.OrthoRotation();
		}
		}
		FCheckResult Hit(1.0f);
		*(DWORD*)Result = GetLevel()->MoveActor( this, FVector(0,0,0), NewRotation, Hit );
	}
	unguardexecSlow;
}


//
// Explicitly attach an object to / detach from an actor, thereby setting it as the base.
//
void AActor::execAttachToBone( FFrame& Stack, RESULT_DECL )
{
	//native final function bool AttachToBone( actor Attachment, name BoneName );
	guardSlow(AActor::execAttachToActorBone);	
	P_GET_OBJECT(AActor,Attachment);
	P_GET_NAME(BoneName);
	P_FINISH;
	
	*(DWORD*)Result = AttachToBone( Attachment, BoneName );

	unguardexecSlow;
}

void AActor::execDetachFromBone( FFrame& Stack, RESULT_DECL )
{
	//native final function bool DetachFromBone( actor Attachment );
	guardSlow(AActor::execDetachFromBone);
	P_GET_OBJECT(AActor,Attachment);
	P_FINISH;

	//scion rwd - added null attachment check
	if (Attachment != NULL)
	{
		*(DWORD*)Result = DetachFromBone( Attachment );
	}
	else
	{
		*(DWORD*)Result = 0;
	}

	unguardexecSlow;
}



///////////////
// Relations //
///////////////

void AActor::execSetOwner( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetOwner);

	P_GET_ACTOR(NewOwner);
	P_FINISH;

	SetOwner( NewOwner );

	unguardexecSlow;
}

//////////////////
// Line tracing //
//////////////////

void AActor::execTrace( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execTrace);

	P_GET_VECTOR_REF(HitLocation);
	P_GET_VECTOR_REF(HitNormal);
	P_GET_VECTOR(TraceEnd);
	P_GET_VECTOR_OPTX(TraceStart,Location);
	P_GET_UBOOL_OPTX(bTraceActors,bCollideActors);
	P_GET_VECTOR_OPTX(TraceExtent,FVector(0,0,0));
	P_GET_OBJECT_REF(UMaterial, Material);
	P_GET_INT_OPTX(ExtraTraceFlags,0);
	P_FINISH;

	// Trace the line.
	FCheckResult Hit(1.0f);
	DWORD TraceFlags;
	if( bTraceActors )
		TraceFlags = TRACE_ProjTargets;
	else
		TraceFlags = TRACE_World;

	if( Material )
		TraceFlags |= TRACE_Material;

	//scion superville: Decode extra trace flags
	if( ExtraTraceFlags & UCONST_TRACEFLAG_BULLETWHIP ) {
		TraceFlags |= TRACE_BulletTrace;
	}
	if( ExtraTraceFlags & UCONST_TRACEFLAG_IGNOREEXTENT ) {
		TraceFlags |= TRACE_IgnoreExtent;
	}
	if( ExtraTraceFlags & UCONST_TRACEFLAG_CAMERA ) {
		TraceFlags |= TRACE_Camera;
	}
	if( ExtraTraceFlags & UCONST_TRACEFLAG_MELEE ) 
	{
		TraceFlags |= TRACE_Melee;
	}
	if( ExtraTraceFlags & UCONST_TRACEFLAG_WEAPON )
	{
		TraceFlags |= TRACE_Weapon;
	}

	//debug
/*	if(  TraceFlags & TRACE_BulletTrace ) {
		FVector Dir = (TraceEnd - TraceStart).GetNormalized();
		debugf( TEXT("Start %f %f %f End %f %f %f Dir %f %f %f"),
			TraceStart.X,
			TraceStart.Y,
			TraceStart.Z,
			TraceEnd.X,
			TraceEnd.Y,
			TraceEnd.Z,
			Dir.X,
			Dir.Y,
			Dir.Z );
	}*/

	AActor *TraceActor = this;
	AController *C = Cast<AController>(this);
	if ( C && C->Pawn )
		TraceActor = C->Pawn;
	GetLevel()->SingleLineCheck( Hit, TraceActor, TraceEnd, TraceStart, TraceFlags, TraceExtent );

	//debug
/*	if( TraceFlags & TRACE_BulletTrace ) {
		GTempLineBatcher->AddStayingLine(TraceStart,TraceEnd,FColor(255,255,255,255));
		FVector Dir = (Hit.Location - TraceStart).GetNormalized();
		debugf( TEXT("HitDir %f %f %f"), Dir.X, Dir.Y, Dir.Z );
	}*/

	*(AActor**)Result = Hit.Actor;
	*HitLocation      = Hit.Location;
	*HitNormal        = Hit.Normal;
	if( Material )
		*Material = Hit.Material;
	unguardexecSlow;
}

void AActor::execFastTrace( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execTrace);

	P_GET_VECTOR_REF(HitLocation);
	P_GET_VECTOR_REF(HitNormal);
	P_GET_VECTOR(TraceEnd);
	P_GET_VECTOR_OPTX(TraceStart,Location);
	P_GET_INT_OPTX(ExtraTraceFlags, 0);
	P_FINISH;

	//scion superville: Decode extra trace flags
	DWORD TraceFlags = TRACE_World | TRACE_StopAtFirstHit;

	if( ExtraTraceFlags & UCONST_TRACEFLAG_BULLETWHIP ) {
		TraceFlags |= TRACE_BulletTrace;
	}
	if( ExtraTraceFlags & UCONST_TRACEFLAG_IGNOREEXTENT ) {
		TraceFlags |= TRACE_IgnoreExtent;
	}
	if( ExtraTraceFlags & UCONST_TRACEFLAG_CAMERA ) {
		TraceFlags |= TRACE_Camera;
	}
	if( ExtraTraceFlags & UCONST_TRACEFLAG_MELEE ) {
		TraceFlags |= TRACE_Melee;
	}
	if( ExtraTraceFlags & UCONST_TRACEFLAG_WEAPON )
	{
		TraceFlags |= TRACE_Weapon;
	}

	// Trace the line.
	FCheckResult Hit(1.f);
	GetLevel()->SingleLineCheck( Hit, this, TraceEnd, TraceStart, TraceFlags );

	*(DWORD*)Result		= !Hit.Actor;
	*HitLocation		=  Hit.Location;
	*HitNormal			=  Hit.Normal;

	unguardexecSlow;
}

///////////////////////
// Spawn and Destroy //
///////////////////////

void AActor::execSpawn( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSpawn);

	P_GET_OBJECT(UClass,SpawnClass);
	P_GET_OBJECT_OPTX(AActor,SpawnOwner,NULL);
	P_GET_NAME_OPTX(SpawnName,NAME_None);
	P_GET_VECTOR_OPTX(SpawnLocation,Location);
	P_GET_ROTATOR_OPTX(SpawnRotation,Rotation);
	P_FINISH;

	// SCRIPTTIME FLOAT OldClock = GStats.DWORDStats(GEngineStats.STATS_Game_SpawningCycles);
	FUNC_CLOCK(SpawnCyc,GStats.DWORDStats(GEngineStats.STATS_Game_SpawningCycles));
	// Spawn and return actor.
	AActor* Spawned = SpawnClass ? GetLevel()->SpawnActor
	(
		SpawnClass,
		NAME_None,
		SpawnLocation,
		SpawnRotation,
		NULL,
		0,
		0,
		SpawnOwner,
		Instigator
	) : NULL;
	if( Spawned && (SpawnName != NAME_None) )
		Spawned->Tag = SpawnName;
	*(AActor**)Result = Spawned;
	/* SCRIPTTIME
	if ( Spawned )
		debugf(TEXT("%s Spawned %s took %f"),GetName(),Spawned->GetName(), (GStats.DWORDStats(GEngineStats.STATS_Game_SpawningCycles) - OldClock) * GSecondsPerCycle * 1000.0f);
	else
		debugf(TEXT("%s Failed spawn took %f"),GetName(),(GStats.DWORDStats(GEngineStats.STATS_Game_SpawningCycles) - OldClock) * GSecondsPerCycle * 1000.0f);
	*/
	unguardexecSlow;
}

// scion sz - 
// added optional bools for force deleting actors during detail changes
void AActor::execDestroy( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execDestroy);
	P_GET_UBOOL_OPTX( bNetForce, 0 );
	P_GET_UBOOL_OPTX( bForceStatic, 0 );
	P_GET_UBOOL_OPTX( bForceNoDelete, 0 );
	P_FINISH;
	
	*(DWORD*)Result = GetLevel()->DestroyActor( this, bNetForce, bForceStatic, bForceNoDelete );

	unguardexecSlow;
}

////////////
// Timing //
////////////

void AActor::execSetTimer( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSetTimer);

	P_GET_FLOAT(NewTimerRate);
	P_GET_UBOOL(bLoop);
	P_FINISH;

	TimerCounter = 0.0f;
	TimerRate    = NewTimerRate;
	bTimerLoop   = bLoop;

	unguardexecSlow;
}

/*-----------------------------------------------------------------------------
	Native iterator functions.
-----------------------------------------------------------------------------*/

void AActor::execAllActors( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execAllActors);

	// Get the parms.
	P_GET_OBJECT(UClass,BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_GET_NAME_OPTX(TagName,NAME_None);
	P_FINISH;

	BaseClass = BaseClass ? BaseClass : AActor::StaticClass();
	INT iActor=0;

	PRE_ITERATOR;
		// Fetch next actor in the iteration.
		*OutActor = NULL;
		while( iActor<GetLevel()->Actors.Num() && *OutActor==NULL )
		{
			AActor* TestActor = GetLevel()->Actors(iActor++);
			if(	TestActor && TestActor->IsA(BaseClass) && (TagName==NAME_None || TestActor->Tag==TagName) )
				*OutActor = TestActor;
		}
		if( *OutActor == NULL )
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

	unguardexecSlow;
}

void AActor::execDynamicActors( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execDynamicActors);

	// Get the parms.
	P_GET_OBJECT(UClass,BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_GET_NAME_OPTX(TagName,NAME_None);
	P_FINISH;
	
	BaseClass = BaseClass ? BaseClass : AActor::StaticClass();
	INT iActor = GetLevel()->iFirstDynamicActor;

	PRE_ITERATOR;
		// Fetch next actor in the iteration.
		*OutActor = NULL;
		while( iActor<GetLevel()->Actors.Num() && *OutActor==NULL )
		{
			AActor* TestActor = GetLevel()->Actors(iActor++);
			if(	TestActor && TestActor->IsA(BaseClass) && (TagName==NAME_None || TestActor->Tag==TagName) )
				*OutActor = TestActor;
		}
		if( *OutActor == NULL )
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

	unguardexecSlow;
}

void AActor::execChildActors( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execChildActors);

	P_GET_OBJECT(UClass,BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_FINISH;

	BaseClass = BaseClass ? BaseClass : AActor::StaticClass();
	INT iActor=0;

	PRE_ITERATOR;
		// Fetch next actor in the iteration.
		*OutActor = NULL;
		while( iActor<GetLevel()->Actors.Num() && *OutActor==NULL )
		{
			AActor* TestActor = GetLevel()->Actors(iActor++);
			if(	TestActor && TestActor->IsA(BaseClass) && TestActor->IsOwnedBy( this ) )
				*OutActor = TestActor;
		}
		if( *OutActor == NULL )
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

	unguardexecSlow;
}

void AActor::execBasedActors( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execBasedActors);

	P_GET_OBJECT(UClass,BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_FINISH;

	BaseClass = BaseClass ? BaseClass : AActor::StaticClass();
	INT iActor=0;

	PRE_ITERATOR;
		// Fetch next actor in the iteration.
		*OutActor = NULL;
		while( iActor<GetLevel()->Actors.Num() && *OutActor==NULL )
		{
			AActor* TestActor = GetLevel()->Actors(iActor++);
			if(	TestActor && TestActor->IsA(BaseClass) && TestActor->Base==this )
				*OutActor = TestActor;
		}
		if( *OutActor == NULL )
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

	unguardexecSlow;
}

void AActor::execTouchingActors( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execTouchingActors);

	P_GET_OBJECT(UClass,BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_FINISH;

	BaseClass = BaseClass ? BaseClass : AActor::StaticClass();
	INT iTouching=0;

	PRE_ITERATOR;
		// Fetch next actor in the iteration.
		*OutActor = NULL;
		for( iTouching; iTouching<Touching.Num() && *OutActor==NULL; iTouching++ )
		{
			AActor* TestActor = Touching(iTouching);
			if(	TestActor && TestActor->IsA(BaseClass) )
				*OutActor = TestActor;
		}
		if( *OutActor == NULL )
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

	unguardexecSlow;
}

/* epic ===============================================
* ::execAttachedActors
* Author: moelfke
*
* Returns all actors attached to this actor that are of
* the specified class or a subclass.
*
* =====================================================
*/
void AActor::execAttachedActors( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execAttachedActors);

	P_GET_OBJECT(UClass,BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_FINISH;

	BaseClass = BaseClass ? BaseClass : AActor::StaticClass();
	INT i = 0;

	PRE_ITERATOR;
		// Fetch next actor in the iteration.
		*OutActor = NULL;
		for( ; i < Attached.Num() && *OutActor == NULL; i++)
		{
			AActor* TestActor = Attached(i);
			// if this actor is of a class we want
			if (TestActor && TestActor->IsA(BaseClass))
			{
				// return it
				*OutActor = TestActor;
			}
		}
		if (*OutActor == NULL)
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

	unguardexecSlow;
}

void AActor::execTraceActors( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execTraceActors);

	P_GET_OBJECT(UClass,BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_GET_VECTOR_REF(HitLocation);
	P_GET_VECTOR_REF(HitNormal);
	P_GET_VECTOR(End);
	P_GET_VECTOR_OPTX(Start,Location);
	P_GET_VECTOR_OPTX(TraceExtent,FVector(0,0,0));
	P_FINISH;

	FMemMark Mark(GMem);
	BaseClass         = BaseClass ? BaseClass : AActor::StaticClass();
	FCheckResult* Hit = GetLevel()->MultiLineCheck( GMem, End, Start, TraceExtent, Level, TRACE_AllColliding, this );

	PRE_ITERATOR;
		if( Hit )
		{
			if ( Hit->Actor && Hit->Actor->IsA(BaseClass) )
			{
				*OutActor    = Hit->Actor;
				*HitLocation = Hit->Location;
				*HitNormal   = Hit->Normal;
			}
			Hit = Hit->GetNext();
		}
		else
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			*OutActor = NULL;
			break;
		}
	POST_ITERATOR;
	Mark.Pop();

	unguardexecSlow;
}

void AActor::execRadiusActors( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execRadiusActors);

	P_GET_OBJECT(UClass,BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_GET_FLOAT(Radius);
	P_GET_VECTOR_OPTX(TraceLocation,Location);
	P_FINISH;

	BaseClass = BaseClass ? BaseClass : AActor::StaticClass();
	INT iActor=0;

	PRE_ITERATOR;
		// Fetch next actor in the iteration.
		*OutActor = NULL;
		while( iActor<GetLevel()->Actors.Num() && *OutActor==NULL )
		{
			AActor* TestActor = GetLevel()->Actors(iActor++);
			if
			(	TestActor
			&&	TestActor->IsA(BaseClass)
			&&	(TestActor->Location - TraceLocation).SizeSquared() < Square(Radius + TestActor->CollisionRadius) )
				*OutActor = TestActor;
		}
		if( *OutActor == NULL )
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

	unguardexecSlow;
}

void AActor::execVisibleActors( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execVisibleActors);

	P_GET_OBJECT(UClass,BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_GET_FLOAT_OPTX(Radius,0.0f);
	P_GET_VECTOR_OPTX(TraceLocation,Location);
	P_FINISH;

	BaseClass = BaseClass ? BaseClass : AActor::StaticClass();
	INT iActor=0;
	FCheckResult Hit(1.f);

	PRE_ITERATOR;
		// Fetch next actor in the iteration.
		*OutActor = NULL;
		while( iActor<GetLevel()->Actors.Num() && *OutActor==NULL )
		{
			AActor* TestActor = GetLevel()->Actors(iActor++);
			if
			(	TestActor
			&& !TestActor->bHidden
			&&	TestActor->IsA(BaseClass)
			&&	(Radius==0.0f || (TestActor->Location-TraceLocation).SizeSquared() < Square(Radius)) )
			{
				GetLevel()->SingleLineCheck( Hit, this, TestActor->Location, TraceLocation, TRACE_World|TRACE_StopAtFirstHit );
				if ( !Hit.Actor || (Hit.Actor == TestActor) )
					*OutActor = TestActor;
			}
		}
		if( *OutActor == NULL )
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

	unguardexecSlow;
}

void AActor::execVisibleCollidingActors( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execVisibleCollidingActors);

	P_GET_OBJECT(UClass,BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_GET_FLOAT(Radius);
	P_GET_VECTOR_OPTX(TraceLocation,Location);
	P_GET_UBOOL_OPTX(bIgnoreHidden, 0);
	P_FINISH;

	BaseClass = BaseClass ? BaseClass : AActor::StaticClass();
	FMemMark Mark(GMem);
	FCheckResult* Link=GetLevel()->Hash->ActorRadiusCheck( GMem, TraceLocation, Radius, 0 );
	
	PRE_ITERATOR;
		// Fetch next actor in the iteration.
		*OutActor = NULL;
		FCheckResult Hit(1.f);
		if ( Link )
		{
			while ( Link )
			{
				if( !Link->Actor
					||	!Link->Actor->IsA(BaseClass)
					||  (bIgnoreHidden && Link->Actor->bHidden) )
				{
					Link=Link->GetNext();
				}
				else
				{
					GetLevel()->SingleLineCheck( Hit, this, Link->Actor->Location, TraceLocation, TRACE_World );
					if ( Hit.Actor && (Hit.Actor != Link->Actor) )
						Link=Link->GetNext();
					else
						break;
				}
			}

			if ( Link )
			{
				*OutActor = Link->Actor;
				Link=Link->GetNext();
			}
		}
		if ( *OutActor == NULL )
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

	Mark.Pop();
	unguardexecSlow;
}

void AActor::execCollidingActors( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execCollidingActors);

	P_GET_OBJECT(UClass,BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_GET_FLOAT(Radius);
	P_GET_VECTOR_OPTX(TraceLocation,Location);
	P_FINISH;

	BaseClass = BaseClass ? BaseClass : AActor::StaticClass();
	FMemMark Mark(GMem);
	FCheckResult* Link=GetLevel()->Hash->ActorRadiusCheck( GMem, TraceLocation, Radius, 0 );
	
	PRE_ITERATOR;
		// Fetch next actor in the iteration.
		*OutActor = NULL;
		FCheckResult Hit(1.f);
		if ( Link )
		{
			while ( Link )
			{
				if( !Link->Actor
					||	!Link->Actor->IsA(BaseClass) )
				{
					Link=Link->GetNext();
				}
				else
					break;
			}

			if ( Link )
			{
				*OutActor = Link->Actor;
				Link=Link->GetNext();
			}
		}
		if ( *OutActor == NULL )
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

	Mark.Pop();
	unguardexecSlow;
}

/**
 iterator execLocalPlayerControllers()
 returns all locally rendered/controlled player controllers (typically 1 per client, unless split screen)
*/
void AActor::execLocalPlayerControllers( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execLocalPlayerControllers);

	P_GET_ACTOR_REF(OutActor);
	P_FINISH;

	INT iPlayers = 0;

	PRE_ITERATOR;
		// Fetch next actor in the iteration.
		*OutActor = NULL;
		if (UEngine::GEngine->Client)
		{
			for (iPlayers; iPlayers < UEngine::GEngine->Client->Viewports.Num() && *OutActor == NULL; iPlayers++)
			{
				if (UEngine::GEngine->Client->Viewports(iPlayers))
				{
					*OutActor = UEngine::GEngine->Client->Viewports(iPlayers)->Actor;
				}
			}
		}
		if (*OutActor == NULL)
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

	unguardexecSlow;
}

void AZoneInfo::execZoneActors( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AZoneInfo::execZoneActors);

	P_GET_OBJECT(UClass,BaseClass);
	P_GET_ACTOR_REF(OutActor);
	P_FINISH;

	BaseClass = BaseClass ? BaseClass : AActor::StaticClass();
	INT iActor=0;

	PRE_ITERATOR;
		// Fetch next actor in the iteration.
		*OutActor = NULL;
		while( iActor<GetLevel()->Actors.Num() && *OutActor==NULL )
		{
			AActor* TestActor = GetLevel()->Actors(iActor++);
			if
			(	TestActor
			&&	TestActor->IsA(BaseClass)
			&&	TestActor->IsInZone(this) )
				*OutActor = TestActor;
		}
		if( *OutActor == NULL )
		{
			Stack.Code = &Stack.Node->Script(wEndOffset + 1);
			break;
		}
	POST_ITERATOR;

	unguardexecSlow;
}

/*-----------------------------------------------------------------------------
	Script processing function.
-----------------------------------------------------------------------------*/

//
// Execute the state code of the actor.
//
void AActor::ProcessState( FLOAT DeltaSeconds )
{
	if
	(	GetStateFrame()
	&&	GetStateFrame()->Code
	&&	(Role>=ROLE_Authority || (GetStateFrame()->StateNode->StateFlags & STATE_Simulated))
	&&	!IsPendingKill() )
	{
		UState* OldStateNode = GetStateFrame()->StateNode;
		guard(AActor::ProcessState);
		// SCRIPTTIME FLOAT StartTime = GScriptCycles;
		if( ++GScriptEntryTag == 1 )
		{
			GScriptCycles -= appCycles();
		}

		// If a latent action is in progress, update it.
		if( GetStateFrame()->LatentAction )
			(this->*GNatives[GetStateFrame()->LatentAction])( *GetStateFrame(), (BYTE*)&DeltaSeconds );

		// Execute code.
		INT NumStates=0;
		while( !bDeleteMe && GetStateFrame()->Code && !GetStateFrame()->LatentAction )
		{
			BYTE Buffer[MAX_CONST_SIZE];
			GetStateFrame()->Step( this, Buffer );
			if( GetStateFrame()->StateNode!=OldStateNode )
			{
				OldStateNode = GetStateFrame()->StateNode;
				if( ++NumStates > 4 )
				{
					//GetStateFrame().Logf( "Pause going from %s to %s", xx, yy );
					break;
				}
			}
		}
		if( --GScriptEntryTag == 0 )
		{
			GScriptCycles += appCycles();
			/* SCRIPTTIME
			FLOAT PT = (GScriptCycles - StartTime) * GSecondsPerCycle * 1000.0f;
			if ( PT > 0.01f )
			{
				debugf(TEXT("%s processSTATE"),GetName());
				if ( PT > 0.1f )
					debugf(TEXT("***STATE processing time %f"),PT);
				else
					debugf(TEXT("   STATE processing time %f"),PT);
			}
			*/
		}
		unguardf(( TEXT("Object %s, Old State %s, New State %s"), GetFullName(), OldStateNode->GetFullName(), GetStateFrame()->StateNode->GetFullName() ));
	}
}

//
// Internal RPC calling.
//
static inline void InternalProcessRemoteFunction
(
	AActor*			Actor,
	UNetConnection*	Connection,
	UFunction*		Function,
	void*			Parms,
	FFrame*			Stack,
	UBOOL			IsServer
)
{
	guardSlow(InternalProcessRemoteFunction);
	
	STAT_INC(GStats.DWORDStats(GEngineStats.STATS_Net_NumRPC));

	// scion sz -
	// from UC1. Route RPC calls to actual connection
	if( Connection->IsA(UChildConnection::StaticClass()) )
	{
		Connection = ((UChildConnection*)Connection)->Parent;
	}
	
	// Make sure this function exists for both parties.
	FClassNetCache* ClassCache = Connection->PackageMap->GetClassNetCache( Actor->GetClass() );
	if( !ClassCache )
		return;
	FFieldNetCache* FieldCache = ClassCache->GetFromField( Function );
	if( !FieldCache )
		return;

	// Get the actor channel.
	UActorChannel* Ch = Connection->ActorChannels.FindRef(Actor);
	if( !Ch )
	{
		if( IsServer )
			Ch = (UActorChannel *)Connection->CreateChannel( CHTYPE_Actor, 1 );
		if( !Ch )
			return;
		if( IsServer )
		{
			//debugf(NAME_DevNet, TEXT("Creating channel due to calling replicated function %s on Actor %s"), Function->GetName(), Actor->GetName());
			Ch->SetChannelActor( Actor );
		}
	}

	// Make sure initial channel-opening replication has taken place.
	if( Ch->OpenPacketId==MAXWORD )
	{
		if( !IsServer )
			return;
		Ch->ReplicateActor();
	}

	// Form the RPC preamble.
	FOutBunch Bunch( Ch, 0 );
	//debugf(TEXT("   Call %s"),Function->GetFullName());
	Bunch.WriteInt( FieldCache->FieldNetIndex, ClassCache->GetMaxIndex() );

	// Form the RPC parameters.
	if( Stack )
	{
		appMemzero( Parms, Function->ParmsSize );
        for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(Function); It && (It->PropertyFlags & (CPF_Parm|CPF_ReturnParm))==CPF_Parm; ++It )
			Stack->Step( Stack->Object, (BYTE*)Parms + It->Offset );
		checkSlow(*Stack->Code==EX_EndFunctionParms);
	}
	for( TFieldIterator<UProperty,CLASS_IsAUProperty> It(Function); It && (It->PropertyFlags & (CPF_Parm|CPF_ReturnParm))==CPF_Parm; ++It )
	{
		if( Connection->PackageMap->ObjectToIndex(*It)!=INDEX_NONE )
		{
			UBOOL Send = 1;
			if( !Cast<UBoolProperty>(*It,CLASS_IsAUBoolProperty) )
			{
				Send = !It->Matches(Parms,NULL,0);
				Bunch.WriteBit( Send );
			}
			if( Send )
				It->NetSerializeItem( Bunch, Connection->PackageMap, (BYTE*)Parms + It->Offset );
		}
	}

	//scion rwd - bNetDebug
	if (Actor->bNetDebug)
	{
		debugf(TEXT("bNetDebug %2.1f - Sending RPC: %s %s, bytes: %d"),
			   Actor->Level->TimeSeconds,
			   Actor->GetName(),
			   Function->GetName(),
			   Bunch.GetNumBytes());
	}

	// Reliability.
	//warning: RPC's might overflow, preventing reliable functions from getting through.
	if( Function->FunctionFlags & FUNC_NetReliable )
	{
		Bunch.SetbReliable( 1 );
	}

	// Send the bunch.
	if( !Bunch.IsError() )
	{
		Ch->SendBunch( &Bunch, 1 );
	}
	else
	{
		//scion rwd - added some more info to make this warning useful
		debugf( NAME_DevNet, TEXT("RPC bunch overflowed actor: %s func: %s parm size: %d"),
				Actor->GetName(),
				Function->GetName(),
				DWORD(Function->ParmsSize));
	}

	unguardSlow;
}

//
// Return whether a function should be executed remotely.
//
UBOOL AActor::ProcessRemoteFunction( UFunction* Function, void* Parms, FFrame* Stack )
{
	guard(AActor::ProcessRemoteFunction);

	// Quick reject.
	if( (Function->FunctionFlags & FUNC_Static) || bDeleteMe )
		return 0;
#ifndef FINAL_RELEASE //scion rwd - debug check for simulated returns
	if (Level->NetMode == NM_Client &&
		Role <= ROLE_SimulatedProxy &&
		!(Function->FunctionFlags & (FUNC_Simulated | FUNC_Native)) &&
		Function->GetReturnProperty() != NULL)
	{
		debugf(NAME_DevNet, TEXT("Warning: !simulated %s returning null result on client"), Function->GetFullName());
	}
#endif
	UBOOL Absorb = (Role<=ROLE_SimulatedProxy) && !(Function->FunctionFlags & (FUNC_Simulated | FUNC_Native));
	if( GetLevel()->DemoRecDriver )
	{
		if( GetLevel()->DemoRecDriver->ServerConnection )
			return Absorb;
		ProcessDemoRecFunction( Function, Parms, Stack );
	}
	if( Level->NetMode==NM_Standalone )
		return 0;
	if( !(Function->FunctionFlags & FUNC_Net) )
		return Absorb;

	// Check if the actor can potentially call remote functions.
	// FIXME - cleanup finding top - or make function
    APlayerController* Top = GetTopPlayerController();//Cast<APlayerController>(GetTopOwner());
	UNetConnection* ClientConnection = NULL;
    //scion rwd - mongo's multicast replication
    if
	(	(Role==ROLE_Authority) &&
        !(Function->FunctionFlags & FUNC_Multicast) &&
        (Top==NULL || (ClientConnection=Cast<UNetConnection>(Top->Player))==NULL) )
    {
		return Absorb;
    }

	// scion sz -
	// from UC1. Route RPC calls to actual connection
	if( ClientConnection && ClientConnection->IsA(UChildConnection::StaticClass()) )
	{
		ClientConnection = ((UChildConnection*)ClientConnection)->Parent;
	}
	
	// See if UnrealScript replication condition is met.
	while( Function->GetSuperFunction() )
		Function = Function->GetSuperFunction();
	UBOOL Val=0;
	FFrame( this, Function->GetOwnerClass(), Function->RepOffset, NULL ).Step( this, &Val );
	if( !Val )
		return Absorb;

	// Get the connection.
	UBOOL           IsServer   = Level->NetMode==NM_DedicatedServer || Level->NetMode==NM_ListenServer;
    //scion rwd - mongo's multicast replication
    if( Function->FunctionFlags & FUNC_Multicast )
    {
		if (XLevel->NetDriver)
		{
			//scion rwd - store index to be reset post RPC
			BYTE *codeIdx = NULL;
			if (Stack)
			{
				codeIdx = Stack->Code;
			}

			for (INT i = 0; i < XLevel->NetDriver->ClientConnections.Num(); i++)
			{
				UNetConnection* Connection = XLevel->NetDriver->ClientConnections(i);
				// if the connection is not valid, skip it
				if (Connection->State != USOCK_Open || !Connection->Actor)
				{
					continue;
				}
				// If saturated and function is unimportant, skip it.
				if (!(Function->FunctionFlags & FUNC_NetReliable) && !Connection->IsNetReady(0))
				{
					continue;
				}

				// don't create this channel if the actor is not relevant
				// unless this was recently spawned (semi-hack)
				if (!Connection->ActorChannels.FindRef(this) && GetLevel()->TimeSeconds - SpawnedTime > 0.2f)
				{
					continue;
				}

				// Send function data to remote.
				InternalProcessRemoteFunction(this, Connection, Function, Parms, Stack, IsServer);

				//scion rwd - restore index, since all the params were just read past
				if (Stack)
				{
					Stack->Code = codeIdx;
				}
			}
		}
        // if we are running a multicast function on a listen server, run it locally in addition to replicating out
        if (Level->NetMode == NM_ListenServer)
        {
            return 0;
        }
    }
    else
    {
        UNetConnection* Connection = IsServer ? ClientConnection : GetLevel()->NetDriver->ServerConnection;
		//scion jg -- Don't assert, just log an error
        if (Connection != NULL)
		{
			// If saturated and function is unimportant, skip it.
			if( !(Function->FunctionFlags & FUNC_NetReliable) && !Connection->IsNetReady(0) )
				return 1;

			// Send function data to remote.
			InternalProcessRemoteFunction( this, Connection, Function, Parms, Stack, IsServer );
		}
		else
		{
			debugf(TEXT("NULL Connection in ProcessRemoteFunction()"));
		}
    }

    return 1;

	unguardf(( TEXT("(%s)"), Function->GetFullName() ));
}

// Replicate a function call to a demo recording file
void AActor::ProcessDemoRecFunction( UFunction* Function, void* Parms, FFrame* Stack )
{
	guard(AActor::ProcessDemoRecFunction);

	// Check if the function is replicatable
	if( (Function->FunctionFlags & (FUNC_Static|FUNC_Net))!=FUNC_Net || bNetTemporary )
		return;

	UBOOL IsNetClient = (Level->NetMode == NM_Client);

	// Check if actor was spawned locally in a client-side demo
	if(IsNetClient && Role == ROLE_Authority)
		return;

	// See if UnrealScript replication condition is met.
	while( Function->GetSuperFunction() )
		Function = Function->GetSuperFunction();

	UBOOL Val=0;
	if(IsNetClient)
		Exchange(RemoteRole, Role);
	bDemoRecording = 1;
	bClientDemoRecording = IsNetClient;
	FFrame( this, Function->GetOwnerClass(), Function->RepOffset, NULL ).Step( this, &Val );
	bDemoRecording = 0;
	bClientDemoRecording = 0;
	if(IsNetClient)
		Exchange(RemoteRole, Role);
	bClientDemoNetFunc = 0;
	if( !Val )
		return;

	// Get the connection.
	if( !GetLevel()->DemoRecDriver->ClientConnections.Num() )
		return;
	UNetConnection* Connection = GetLevel()->DemoRecDriver->ClientConnections(0);
	if(!Connection )
		return;

	// Send function data to remote.
	BYTE* SavedCode = Stack ? Stack->Code : NULL;
	InternalProcessRemoteFunction( this, Connection, Function, Parms, Stack, 1 );
	if( Stack )
		Stack->Code = SavedCode;

	unguardf(( TEXT("(%s/%s)"), GetName(), Function->GetFullName() ));
}

/*-----------------------------------------------------------------------------
	GameInfo
-----------------------------------------------------------------------------*/

FString GetClassificationSection( EValueClassification Type )
{
	switch ( Type )
	{
	case VC_Nonstandard:	return TEXT("Non-standard");
	case VC_Modified:		return TEXT("Modified");
	case VC_Pure:			return TEXT("Pure");
	default:
		debugf(TEXT("GetClassificationSection: Invalid value specified for PropType  '%i'"), Type);
		break;
	}

	return TEXT("");
}

UBOOL AGameInfo::HasPropValue( EValueClassification PropType, const FString& PropertyPath, INT TestValue ) const
{
	guard(AGameInfo::HasPropValue);

	FString SectionName = GetClassificationSection(PropType);
	FString Value;
	if ( GConfig->GetString(*SectionName, *PropertyPath, Value, TEXT("ServerRules.ini")) )
	{
		// found the value in this section

		// get the list of individual ranges
		TArray<FString> RangeArray;
		Value.ParseIntoArray(TEXT(","), &RangeArray);

		for ( INT i = 0; i < RangeArray.Num(); i++ )
		{
			FString& CurrentRange = RangeArray(i);

			// if this value is a ranged value
			INT pos = CurrentRange.InStr(TEXT("-"));
			if ( pos != INDEX_NONE )
			{
				INT MinValue = pos == 0
					? INDEX_NONE		// this range has no min value (e.g. Range=-xx)
					: appAtoi(*CurrentRange.Left(pos));

				INT MaxValue = pos == CurrentRange.Len() - 1
					? INDEX_NONE		// this range has no max value (e.g. Range=xx- )
					: appAtoi(*CurrentRange.Mid(pos+1));

				check(MinValue != MaxValue);
				UBOOL bGreaterThanMinValue = MinValue != INDEX_NONE && TestValue >= MinValue;
				UBOOL bLessThanMaxValue = MaxValue != INDEX_NONE && TestValue <= MaxValue;

				if ( bGreaterThanMinValue && (bLessThanMaxValue || MaxValue == INDEX_NONE) )
					return true;

				if ( bLessThanMaxValue && MinValue == INDEX_NONE )
					return true;
			}
			else if ( TestValue == appAtoi(*CurrentRange) )
				return true;
		}
	}

	return false;

	unguard;
}

EValueClassification AGameInfo::GetPropertyClassification( const FString& PropName, INT PropValue ) const
{
	guard(AGameInfo::GetPropertyClassification);

	EValueClassification Result = VC_Pure;
	FString PropertyPath = FString::Printf(TEXT("%s.%s"), GetClass()->GetName(), *PropName);

	if ( HasPropValue(VC_Nonstandard,PropertyPath,PropValue) )
	{
		Result = VC_Nonstandard;
	}
	else if ( HasPropValue(VC_Modified,PropertyPath,PropValue) )
	{
		Result = VC_Modified;
	}
	else
	{
		// check the global sections
		PropertyPath = FString::Printf(TEXT("Global.%s"), *PropName);
		if ( HasPropValue(VC_Nonstandard,PropertyPath,PropValue) )
		{
			Result = VC_Nonstandard;
		}
		else if ( HasPropValue(VC_Modified,PropertyPath,PropValue) )
		{
			Result = VC_Modified;
		}
	}

	return Result;
	unguard
}

/*==========================================================================================================
function	AGameInfo::GetMutatorClassification
Desc:		Determines if using any of the specified mutators in this gametype affect this game's classification.

input:		MutatorClassNames
				array of mutators class names (class name only - no package name).

return:		Returns the highest common classification for any of the mutators in the list for this gametype.

last modified by:	Ron Prestenback
==========================================================================================================*/
EValueClassification AGameInfo::GetMutatorClassification( TArray<FString>& MutatorClassNames ) const
{
    guard(AGameInfo::GetMutatorClassification);

	if ( MutatorClassNames.Num() == 0 )
		return VC_Pure;

	FString GamePath = FString::Printf(TEXT("%s.Mutators"), GetClass()->GetName());
	FString GlobalPath(TEXT("Global.Mutators"));

	TArray<FString> Mutators;
	FString NonStandardSection = GetClassificationSection(VC_Nonstandard),
			ModifiedSection = GetClassificationSection(VC_Modified),
			PureSection = GetClassificationSection(VC_Pure);

	// if any mutators are classified as "Pure" for this gametype
	// remove them from the list of mutators to consider
	if ( GConfig->GetArray(*PureSection, *GamePath, Mutators, TEXT("ServerRules")) > 0 )
	{
		for ( INT MutatorIndex = MutatorClassNames.Num() - 1; MutatorIndex >= 0; MutatorIndex-- )
		{
			for ( INT SectionIndex = 0; SectionIndex < Mutators.Num(); SectionIndex++ )
			{
				if ( Mutators(SectionIndex) == MutatorClassNames(MutatorIndex) )
				{
					MutatorClassNames.Remove(MutatorIndex);
				}
			}
		}
	}

	// if any mutators are classified as "Pure" for all gametypes
	// remove them from the list of mutators to consider
	if ( MutatorClassNames.Num() && GConfig->GetArray(*PureSection, *GlobalPath, Mutators, TEXT("ServerRules")) > 0 )
	{
		for ( INT MutatorIndex = MutatorClassNames.Num() - 1; MutatorIndex >= 0; MutatorIndex-- )
		{
			for ( INT SectionIndex = 0; SectionIndex < Mutators.Num(); SectionIndex++ )
			{
				if ( Mutators(SectionIndex) == MutatorClassNames(MutatorIndex) )
				{
					MutatorClassNames.Remove(MutatorIndex);
				}
			}
		}
	}

	// if any mutators are classified as "Non-standard" for this gametype,
	// the entire mutator classification is Non-standard
	if ( MutatorClassNames.Num() && GConfig->GetArray(*NonStandardSection, *GamePath, Mutators, TEXT("ServerRules")) > 0 )
	{
		for ( INT MutatorIndex = 0; MutatorIndex < MutatorClassNames.Num(); MutatorIndex++ )
		{
			for ( INT SectionIndex = 0; SectionIndex < Mutators.Num(); SectionIndex++ )
			{
				if ( Mutators(SectionIndex) == MutatorClassNames(MutatorIndex) )
				{
					return VC_Nonstandard;
				}
			}
		}
	}

	// if any mutators are classified as "Non-standard" for all gametypes,
	// the entire mutator classification is Non-standard
	if ( MutatorClassNames.Num() && GConfig->GetArray(*NonStandardSection, *GlobalPath, Mutators, TEXT("ServerRules")) > 0 )
	{
		for ( INT MutatorIndex = 0; MutatorIndex < MutatorClassNames.Num(); MutatorIndex++ )
		{
			for ( INT SectionIndex = 0; SectionIndex < Mutators.Num(); SectionIndex++ )
			{
				if ( Mutators(SectionIndex) == MutatorClassNames(MutatorIndex) )
				{
					return VC_Nonstandard;
				}
			}
		}
	}

	// if any mutators are classified as "Modified" for this gametype,
	// the entire mutator classification is Modified
	if ( MutatorClassNames.Num() && GConfig->GetArray(*ModifiedSection, *GamePath, Mutators, TEXT("ServerRules")) > 0 )
	{
		for ( INT MutatorIndex = 0; MutatorIndex < MutatorClassNames.Num(); MutatorIndex++ )
		{
			for ( INT SectionIndex = 0; SectionIndex < Mutators.Num(); SectionIndex++ )
			{
				if ( Mutators(SectionIndex) == MutatorClassNames(MutatorIndex) )
				{
					return VC_Modified;
				}
			}
		}
	}

	// if any mutators are classified as "Modified" for all gametypes,
	// the entire mutator classification is Modified
	if ( MutatorClassNames.Num() && GConfig->GetArray(*ModifiedSection, *GlobalPath, Mutators, TEXT("ServerRules")) > 0 )
	{
		for ( INT MutatorIndex = 0; MutatorIndex < MutatorClassNames.Num(); MutatorIndex++ )
		{
			for ( INT SectionIndex = 0; SectionIndex < Mutators.Num(); SectionIndex++ )
			{
				if ( Mutators(SectionIndex) == MutatorClassNames(MutatorIndex) )
				{
					return VC_Modified;
				}
			}
		}
	}


	// If we're here, none of the mutators specified were found to have any effect on the classification
	// for the current gametype.
	return VC_Pure;

    unguard;
}



/*==========================================================================================================
function	AGameInfo::CalculatePurity
Desc:		Calucates the server purity level for the current game.

last modified by:	Ron Prestenback
==========================================================================================================*/
EValueClassification AGameInfo::CalculatePurity() const
{
    guard(AGameInfo::CalculatePurity);

	EValueClassification Result = VC_Pure;

	// the following criteria are part of the purity calculation
	// number of mutators, goalscore, timelimit, difficulty, number of bots,
	// maxplayers, private slots, allow handicap

	// value initialized to -1 to offset counting the game default mutator,
	// which might be anywhere in the mutator list, but is guaranteed to
	// be somewhere in the mutator list.
	AMutator* DefaultMutator = AMutator::StaticClass()->GetDefaultObject<AMutator>();
	TArray<FString> MutatorClasses;
	for ( AMutator* M = BaseMutator; M; M = M->NextMutator )
	{
		// if this is the Mutator class, skip it
		if ( M->GetClass() == AMutator::StaticClass() )
			continue;
	
		// if this is the game default mutator (indicated by having no FriendlyName, or having
		// the same FriendlyName as Mutator), skip it
		if ( M->FriendlyName.Len() == 0 || M->FriendlyName == DefaultMutator->FriendlyName )
			continue;

		new(MutatorClasses) FString(M->GetClass()->GetName());
	}

	// Check to see if any of our mutators would cause our classification to change.
	Result = Max(Result, GetMutatorClassification(MutatorClasses));

	// MutatorClasses should now contain only those mutators which weren't listed in
	// the [Pure] section of the ServerRules.ini file.
	Result = Max(Result, GetPropertyClassification(TEXT("MutatorCount"), MutatorClasses.Num()));
	Result = Max(Result, GetPropertyClassification(TEXT("GoalScore"), GoalScore));
	Result = Max(Result, GetPropertyClassification(TEXT("TimeLimit"), TimeLimit));
	Result = Max(Result, GetPropertyClassification(TEXT("GameDifficulty"), GameDifficulty));
	Result = Max(Result, GetPropertyClassification(TEXT("NumBots"), NumBots));
	Result = Max(Result, GetPropertyClassification(TEXT("MaxPlayers"), MaxPlayers));
	Result = Max(Result, GetPropertyClassification(TEXT("PrivateSlots"), NumReservedSlots));
	Result = Max(Result, GetPropertyClassification(TEXT("AllowHandicap"), bAllowPlayerHandicaps));

	return Result;
    unguard;
}

void AGameInfo::execGetRuleClassification( FFrame& Stack, RESULT_DECL )
{
	guard(AGameInfo::execGetRuleClassification);

	P_GET_STR(PropName);
	P_GET_INT(PropValue);
	P_FINISH;

	*(BYTE*)Result = GetPropertyClassification(PropName,PropValue);
	unguardexec;
}

void AGameInfo::execGetMutatorClassification( FFrame& Stack, RESULT_DECL )
{
	guard(AGameInfo::execGetMutatorClassification);

	P_GET_TARRAY_REF(MutatorClasses,FString);
	P_FINISH;

	*(BYTE*)Result = GetMutatorClassification(*MutatorClasses);

	unguardexec;
}

void AGameInfo::execCalculatePurity( FFrame& Stack, RESULT_DECL )
{
	guard(AGameInfo::execCalculatePurity);

	P_FINISH;

	*(BYTE*)Result = CalculatePurity();
	unguardexec;
}

struct FSkillLevelItem
{
	BYTE SkillLevel, Count;

	FSkillLevelItem()
	: Count(0), SkillLevel(0)
	{}
};

static INT Compare( FSkillLevelItem& ItemA, FSkillLevelItem& ItemB )
{
	INT Result = ItemB.Count - ItemA.Count;
	if ( Result == 0 )
		Result = ItemA.SkillLevel - ItemB.SkillLevel;
	return Result;
}

/*==========================================================================================================
function	AGameInfo::CalculateAverageSkill
Desc:		Calculates the representative skill level of the current session.  The most common skill level is
			returned.  In cases where there are more than one skill level occur equally, the median skill level
			of all players is returned.

return:		Returns PLAYERSKILL_Adept if a skill couldn't be calculated.

last modified by:	Ron Prestenback
==========================================================================================================*/
BYTE AGameInfo::CalculateAverageSkill() const
{
    guard(AGameInfo::CalculateAverageSkill);

	BYTE Result = UCONST_PLAYERSKILL_MAX / 2;
	if ( GameReplicationInfo != NULL )
	{
		if ( GameReplicationInfo->PRIArray.Num() )
		{
			FSkillLevelItem Items[UCONST_PLAYERSKILL_MAX];
			for ( INT i = 0; i < UCONST_PLAYERSKILL_MAX; i++ )
				Items[i].SkillLevel = i;

			for ( INT i = 0; i < GameReplicationInfo->PRIArray.Num(); i++ )
			{
				APlayerReplicationInfo* PRI = GameReplicationInfo->PRIArray(i);
				if ( PRI && PRI->PlayerSkill < UCONST_PLAYERSKILL_MAX )
				{
					Items[PRI->PlayerSkill].Count++;
				}
			}

			Sort( Items, UCONST_PLAYERSKILL_MAX );
			if ( Items[0].Count == Items[1].Count )
			{
				INT FirstItem = 0;
				INT LastItem = UCONST_PLAYERSKILL_MAX - 1;
				while ( LastItem > FirstItem && Items[LastItem].Count == 0 )
					LastItem--;

				INT MiddleItem = (LastItem - FirstItem) / 2;
				Result = Items[MiddleItem].SkillLevel;
			}
			else
			{
				Result = Items[0].SkillLevel;
			}
		}
	}

	return Result;
    unguard;
}


//
// Network
//
void AGameInfo::execGetNetworkNumber( FFrame& Stack, RESULT_DECL )
{
	guard(AGameInfo::execGetNetworkNumber);
	P_FINISH;

	*(FString*)Result = XLevel->NetDriver ? XLevel->NetDriver->LowLevelGetNetworkNumber() : FString(TEXT(""));

	unguardexec;
}

//scion bcl -- promoted from Championship
/* Hooks to the net driver (if there is one) to permit it to get control just before level change; permits the crypto keys
   to be sequestered and reused. Should return false if there is any problem; this version ALWAYS returns true.
 */
void AGameInfo::execPreServerTravel( FFrame& Stack, RESULT_DECL )
{
	guard(AGameInfo::execPreServerTravel);
	P_FINISH;

	if( XLevel->NetDriver )
		XLevel->NetDriver->PreServerTravel();

	*(UBOOL*)Result = 1;

	unguardexec;
}

/* epic ===============================================
* ::execPendingPrecacheNewPlayer
* Author: moelfke
*
* Notifies any pending connections that the given player
* has joined, so they can load the player's assets
* before fully joining the game themselves
*
* =====================================================
*/
void AGameInfo::execPendingPrecacheNewPlayer(FFrame& Stack, RESULT_DECL)
{
	guard(AGameInfo::execPendingPrecacheNewPlayer);

	P_GET_OBJECT(AController, NewPlayer);
	P_FINISH;

	if (GetLevel()->NetDriver && NewPlayer->PlayerReplicationInfo && NewPlayer->PlayerReplicationInfo->PawnClass)
	{
		// ask pawn class if it requires any other classes to be fully loaded
		TArray<FString> RequiredClasses;
		NewPlayer->PlayerReplicationInfo->PawnClass->GetDefaultObject<APawn>()->eventGetLoadClasses(RequiredClasses);
		for (INT i = 0; i < GetLevel()->NetDriver->ClientConnections.Num(); i++)
		{
			UNetConnection* Connection = GetLevel()->NetDriver->ClientConnections(i);
			if ( Connection->IsLoadingAssets() )
			{
				Connection->Logf(TEXT("LOAD CLASS=%s"), NewPlayer->PlayerReplicationInfo->PawnClass->GetPathName());
				for (INT j = 0; j < RequiredClasses.Num(); j++)
				{
					Connection->Logf(TEXT("LOAD CLASS=%s"), *RequiredClasses(j));
				}
			}
		}
	}

	unguardexec;
}

/* epic ===============================================
* ::execPendingUnloadPlayer
* Author: moelfke
*
* Notifies any pending connections that a player using
* the given PawnClass has left, so they can unload
* the player's assets
*
* =====================================================
*/
void AGameInfo::execPendingUnloadPlayer(FFrame& Stack, RESULT_DECL)
{
	guard(AGameInfo::execPendingUnloadPlayer);

	P_GET_OBJECT(UClass, PawnClass);
	P_FINISH;

	if (GetLevel()->NetDriver && PawnClass)
	{
		// ask pawn class if it required any other classes to be loaded (and therefore must be unloaded)
		TArray<FString> RequiredClasses;
		PawnClass->GetDefaultObject<APawn>()->eventGetLoadClasses(RequiredClasses);
		for (INT i = 0; i < GetLevel()->NetDriver->ClientConnections.Num(); i++)
		{
			UNetConnection* Connection = GetLevel()->NetDriver->ClientConnections(i);
			if ( Connection->IsLoadingAssets() )
			{
				Connection->Logf(TEXT("UNLOAD CLASS=%s"), PawnClass->GetPathName());
				for (INT j = 0; j < RequiredClasses.Num(); j++)
				{
					Connection->Logf(TEXT("UNLOAD CLASS=%s"), *RequiredClasses(j));
				}
			}
		}
	}

	unguardexec;
}

//
// Deathmessage parsing.
//
void AGameInfo::execParseKillMessage( FFrame& Stack, RESULT_DECL )
{
	guard(AGameInfo::execParseKillMessage);
	P_GET_STR(KillerName);
	P_GET_STR(VictimName);
	P_GET_STR(KillMessage);
	P_FINISH;

	FString Message, Temp;
	INT Offset;

	Temp = KillMessage;

	Offset = Temp.InStr(TEXT("%k"));
	if (Offset != -1)
	{
		Message = Temp.Left(Offset);
		Message += KillerName;
		Message += Temp.Right(Temp.Len() - Offset - 2);
		Temp = Message;
	}

	Offset = Temp.InStr(TEXT("%o"));
	if (Offset != -1)
	{
		Message = Temp.Left(Offset);
		Message += VictimName;
		Message += Temp.Right(Temp.Len() - Offset - 2);
	}

	*(FString*)Result = Message;

	unguardexec;
}
//scion bcl P_GET_COLOR moved to UnScript.h (where it belongs)

void AActor::execMultiply_ColorFloat( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execMultiply_ColorFloat);

	P_GET_COLOR(A);
	P_GET_FLOAT(B);
	P_FINISH;

	A.R = (BYTE) (A.R * B);
	A.G = (BYTE) (A.G * B);
	A.B = (BYTE) (A.B * B);
	*(FColor*)Result = A;

	unguardexecSlow;
}	

void AActor::execMultiply_FloatColor( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execMultiply_FloatColor);

	P_GET_FLOAT (A);
	P_GET_COLOR(B);
	P_FINISH;

	B.R = (BYTE) (B.R * A);
	B.G = (BYTE) (B.G * A);
	B.B = (BYTE) (B.B * A);
	*(FColor*)Result = B;

	unguardexecSlow;
}	

void AActor::execAdd_ColorColor( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execAdd_ColorColor);

	P_GET_COLOR(A);
	P_GET_COLOR(B);
	P_FINISH;

	A.R = A.R + B.R;
	A.G = A.G + B.G;
	A.B = A.B + B.B;
	*(FColor*)Result = A;

	unguardexecSlow;
}

void AActor::execSubtract_ColorColor( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execSubtract_ColorColor);

	P_GET_COLOR(A);
	P_GET_COLOR(B);
	P_FINISH;

	A.R = A.R - B.R;
	A.G = A.G - B.G;
	A.B = A.B - B.B;
	*(FColor*)Result = A;

	unguardexecSlow;
}

void AVolume::execEncompasses( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execEncompasses);

	P_GET_ACTOR(InActor);
	P_FINISH;

	*(DWORD*)Result = Encompasses(InActor->Location);
	unguardexecSlow;
}

void AHUD::execDraw3DLine( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AHUD::execDraw3DLine);

	P_GET_VECTOR(Start);
	P_GET_VECTOR(End);
	P_GET_COLOR(LineColor);
	P_FINISH;

	if ( PlayerOwner && PlayerOwner->Player )
	{
		UViewport *V = Cast<UViewport>(PlayerOwner->Player);
		if ( V )
		{
			//scion superville: Use global line batcher
			GTempLineBatcher->AddLine(Start,End,LineColor);
		}
	}
	unguardexecSlow;
}

//scion ===========================================================
//	SCION Function: execDraw3dBox
//	Author: superville
//
//	Description: Draws a 3D box around the given location
//
//	Input:		FVector BoxLoc	 - location of box center
//				FVector BoxSize	 - dimensions of box
//				FColor	BoxColor - color of box lines
//	Last Modified: 12/15/03 (superville)
// ================================================================
void AHUD::execDraw3DBox( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AHUD::execDraw3DBox);

	P_GET_VECTOR(BoxLoc);
	P_GET_VECTOR(BoxSize);
	P_GET_COLOR(BoxColor);
	P_FINISH;

	if ( PlayerOwner && PlayerOwner->Player )
	{
		UViewport *V = Cast<UViewport>(PlayerOwner->Player);
		if ( V )
		{
			//scion superville: Use global line batcher
			FBox Box( BoxLoc + BoxSize, BoxLoc - BoxSize );
			GTempLineBatcher->AddBox( Box, BoxColor );
		}
	}
	unguardexecSlow;
}

/*---------------------------------------------------------------------------------------
	Projectors
---------------------------------------------------------------------------------------*/

void AActor::AttachProjector( AProjector* Projector )
{
	// Remove old projectors.

	for(INT ProjectorIndex = 0;ProjectorIndex < Projectors.Num();ProjectorIndex++)
		if(!Projectors(ProjectorIndex)->Render( Level->TimeSeconds ))
			Projectors.Remove(ProjectorIndex--);

	// Add the new projector.

	Projectors.AddUniqueItem( Projector->RenderInfo->AddReference() );
}

void AActor::DetachProjector( AProjector* Projector )
{
	Projector->RenderInfo->RemoveReference();
	Projectors.RemoveItem( Projector->RenderInfo );
}

//
// AActor::GetProjectorBase - who's projector list to use.
//
AActor* AActor::GetProjectorBase()
{
	if( bCollideActors || !bAcceptsProjectors )
		return this;

	if( Base )
		return Base->GetProjectorBase();

	return this;
}

//
//	AActor::execGetRenderBoundingSphere
//

void AActor::execGetRenderBoundingSphere(FFrame& Stack,RESULT_DECL)
{
	guard(AActor::execGetRenderBoundingSphere);

	P_FINISH;

	FMatrix	_LocalToWorld;

	if(IsDTMesh() && Mesh)
		_LocalToWorld = Mesh->MeshGetInstance(this)->MeshToWorld();
	else
		_LocalToWorld = LocalToWorld();

	*(FSphere*)Result = GetPrimitive()->GetRenderBoundingSphere(this).TransformBy(_LocalToWorld);

	unguardexec;
}

//
//	AActor::execDrawDebugLine
//

void AActor::execDrawDebugLine(FFrame& Stack,RESULT_DECL)
{
	guard(AActor::execDrawDebugLine);

	P_GET_VECTOR(LineStart);
	P_GET_VECTOR(LineEnd);
	P_GET_BYTE(R);
	P_GET_BYTE(G);
	P_GET_BYTE(B);
	P_FINISH;

	GTempLineBatcher->AddLine(LineStart, LineEnd, FColor(R, G, B));

	unguard;
}

//epic moelfke: adding support for drawing spheres
void AActor::execDrawDebugSphere(FFrame& Stack,RESULT_DECL)
{
	guard(AActor::execDrawDebugSphere);

	P_GET_VECTOR(Base);
	P_GET_FLOAT(Radius);
	P_GET_INT(NumDivisions);
	P_GET_BYTE(R);
	P_GET_BYTE(G);
	P_GET_BYTE(B);
	P_FINISH;

	GTempLineBatcher->AddSphere(Base, FColor(R, G, B), Radius, NumDivisions);

	unguard;
}

void AActor::execClock(FFrame& Stack,RESULT_DECL)
{
	guard(AActor::execClock);
	P_GET_FLOAT_REF( Time );
	P_FINISH;
	*Time -= appCycles();
	unguard;
}

void AActor::execUnClock(FFrame& Stack,RESULT_DECL)
{
	guard(AActor::execUnClock);
	P_GET_FLOAT_REF( Time );
	P_FINISH;
	*Time += appCycles();
	*Time = *Time * GSecondsPerCycle * 1000.f;
	unguard;
}

//scion superville: Allow access from script
void AActor::execFindSlopeRotation( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execClampRotation);

	P_GET_VECTOR(FloorNormal);
	P_GET_ROTATOR(NewRotation);
	P_FINISH;
	
	*(FRotator*)Result = FindSlopeRotation( FloorNormal, NewRotation );
	
	unguardexecSlow;
}

/*
 // ======================================================================================
 // ClampRotation
 // Clamps the given rotation accoring to the base and the limits passed
 // input:	Rotator RotToLimit - Rotation to limit
 //			Rotator Base	   - Base reference for limiting
 //			Rotator Limits	   - Component wise limits
 // output: out RotToLimit	   - Actual limited rotator
 //			return BOOL	true if within limits, false otherwise
 // effects:  Alters the passed in Rotation by reference and calls event OverRotated
 //			  when limit has been exceeded
 // notes:	  It cannot change const Rotations passed in (ie Actor::Rotation) so you need to
 //			  make a copy first
 // ======================================================================================
*/
void AActor::execClampRotation( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execClampRotation);

	P_GET_ROTATOR_REF(out_RotToLimit)
	P_GET_ROTATOR(rBase);
	P_GET_ROTATOR(rUpperLimits);
	P_GET_ROTATOR(rLowerLimits);
	P_FINISH;

	FRotator rOriginal;
	FRotator rAdjusted;

	rOriginal = *out_RotToLimit;
	rOriginal = rOriginal.Normalize();
	rAdjusted = rOriginal;

	rBase = rBase.Normalize();

	rAdjusted = (rAdjusted-rBase).Normalize();

	if( rUpperLimits.Pitch >= 0 )
		rAdjusted.Pitch = Min( rAdjusted.Pitch,  rUpperLimits.Pitch	);
	if( rLowerLimits.Pitch >= 0 )
		rAdjusted.Pitch = Max( rAdjusted.Pitch, -rLowerLimits.Pitch	);
		
	if( rUpperLimits.Yaw >= 0 )
		rAdjusted.Yaw   = Min( rAdjusted.Yaw,    rUpperLimits.Yaw	);
	if( rLowerLimits.Yaw >= 0 )
		rAdjusted.Yaw   = Max( rAdjusted.Yaw,   -rLowerLimits.Yaw	);
		
	if( rUpperLimits.Roll >= 0 )
		rAdjusted.Roll  = Min( rAdjusted.Roll,   rUpperLimits.Roll	);
	if( rLowerLimits.Roll >= 0 )
		rAdjusted.Roll  = Max( rAdjusted.Roll,  -rLowerLimits.Roll	);

	rAdjusted = (rAdjusted + rBase).Normalize();

	*out_RotToLimit = rAdjusted;
	if( *out_RotToLimit != rOriginal ) {
		eventOverRotated( rOriginal, *out_RotToLimit );
		*(UBOOL*)Result = 0;
	}
	else {
		*(UBOOL*)Result = 1;
	}

	unguardexecSlow;
}

//scion superville: return time of animation in seconds
void AActor::execGetAnimDuration( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execGetAnimDuration);

	P_GET_NAME(SequenceName);
	P_GET_FLOAT_OPTX(PlayAnimRate,1.f);
	P_GET_FLOAT_OPTX(CurrentFrame,0.f);
	P_FINISH;

	*(FLOAT*)Result = GetAnimDuration( SequenceName, PlayAnimRate, CurrentFrame );
	
	unguardexecSlow;
}

FLOAT AActor::GetAnimDuration( FName SequenceName, FLOAT PlayAnimRate, FLOAT CurrentFrame )
{
	if( Mesh ) 
	{
		Mesh->MeshGetInstance(this);
		HMeshAnim	InAnim		= MeshInstance->GetAnimNamed( SequenceName );
		FLOAT		FrameCount	= MeshInstance->AnimGetFrameCount( InAnim );
		FLOAT		Rate		= MeshInstance->AnimGetRate( InAnim ) * PlayAnimRate;
		FLOAT		Duration	= (FrameCount - (CurrentFrame * FrameCount)) / Rate;

		return Duration;
	}

	return 0.f;
}

void AActor::execCopyAnimation( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execCopyAnimation);

	P_GET_INT(SrcChannel);
	P_GET_INT(DestChannel);
	P_GET_ACTOR_OPTX(DestActor,this);
	P_FINISH;

	CopyAnimation( SrcChannel, DestChannel, DestActor );
	
	unguardexecSlow;
}


void AActor::execGetBlendAlphaInfo( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execGetBlendAlphaInfo);

	P_GET_INT(Channel);
	P_GET_FLOAT_REF(CurrentAlpha);
	P_GET_FLOAT_REF(CurrentTargetAlpha);
	P_GET_FLOAT_REF(RemainingTime);
	P_FINISH;

	*RemainingTime		= 0.f;
	*CurrentAlpha		= 0.f;
	*CurrentTargetAlpha	= 0.f;

	if( Mesh && Mesh->IsA(USkeletalMesh::StaticClass()) ) {				
		Mesh->MeshGetInstance(this);
		USkeletalMeshInstance* SkelMeshInstance = (USkeletalMeshInstance*)MeshInstance;
		if( SkelMeshInstance->ValidateAnimChannel( Channel ) ) {
			MeshAnimChannel Blend = SkelMeshInstance->Blends(Channel);
			*CurrentAlpha		= Blend.BlendAlpha;
			*CurrentTargetAlpha	= Blend.BlendTargetAlpha;
			*RemainingTime		= Blend.BlendTargetInterval;
		}
	}

	unguardexecSlow;
}

void AActor::execIsOwnedBy( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execIsOwnedBy);

	P_GET_ACTOR(A);
	P_FINISH;

	*(UBOOL*)Result = IsOwnedBy( A );
	
	unguardexecSlow;
}



//scion jg -- Merged in this code from UDN to copy a mesh's materials to its
// skins array
void AActor::execCopyMaterialsToSkins( FFrame& Stack, RESULT_DECL )
{
	P_GET_UBOOL_OPTX( bSetDefault, 0 );
	P_FINISH;

	CopyMaterialsToSkins(bSetDefault);
}

//scion superville: Expose gravity accessor function
void AActor::execGetGravity( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execGetGravity);

	P_FINISH;

	*(FVector*)Result = GetGravity();

	unguardexecSlow;	
}

void AActor::execGetLandingTime( FFrame& Stack, RESULT_DECL )
{
	guardSlow(AActor::execGetLandingTime);

	P_GET_VECTOR(BaseLocation);
	P_GET_VECTOR(LandLocation);
	P_GET_VECTOR( Vel );
	P_GET_UBOOL_OPTX(bTrace,0);
	P_FINISH;

	*(FLOAT*)Result = GetLandingTime( BaseLocation, LandLocation, Vel, bTrace );

	unguardexecSlow;	
}




//scion rwd - call a function like a normal timer
void AActor::execSetFuncTimer(FFrame &Stack,RESULT_DECL)
{
	P_GET_NAME(FuncName);
	P_GET_FLOAT(Rate);
	P_GET_UBOOL_OPTX(bLoop,0);
	P_GET_UBOOL_OPTX(bAddToTimer, 0);
	P_GET_UBOOL_OPTX(bAllowMultiple, 0);
	P_GET_FLOAT_OPTX(Delay,0.f);
	P_FINISH;

#if DEBUG_LOGGING
	if( GetLevel()->GetLevelInfo()->NetMode == NM_Client && Role < ROLE_SimulatedProxy )
	{
		debugf( TEXT("WARNING: Actor [%s] trying to use SetFuncTimer [%s] when Role < ROLE_SimulatedProxy - will not call UpdateTimer"), 
			GetName(),
			*FuncName );
	}
#endif // DEBUG_LOGGING

	SetFuncTimer( FuncName, Rate, bLoop, bAddToTimer, bAllowMultiple, Delay );
}

void AActor::execDisableFuncTimer(FFrame &Stack,RESULT_DECL)
{
	P_GET_NAME(FuncName);
	P_GET_UBOOL_OPTX(bAll,0);
	P_FINISH;

	DisableFuncTimer( FuncName, bAll );
}

void AActor::execFuncTimerIsActive(FFrame &Stack,RESULT_DECL)
{
	P_GET_NAME(FuncName);
	P_FINISH;

	*(UBOOL*)Result = FuncTimerIsActive( FuncName );
}

void AActor::execGetFuncTimerCount(FFrame& Stack, RESULT_DECL)
{
	guardSlow(AActor::execGetFuncTimerCount);
	
	P_GET_NAME(FuncName);
	P_FINISH;

	*(FLOAT*)Result = GetFuncTimerCount( FuncName );

	unguardexecSlow;
}

void AActor::execGetFuncTimerRate(FFrame& Stack, RESULT_DECL)
{
	guardSlow(AActor::execGetFuncTimerRate);

	P_GET_NAME(FuncName);
	P_FINISH;

	*(FLOAT*)Result = GetFuncTimerRate( FuncName );

	unguardexecSlow;
}

//scion rwd - push/pop state support
void AAIController::execPushState(FFrame &Stack, RESULT_DECL)
{
	guard(PushState);
	P_GET_NAME(NewState);
	P_GET_NAME_OPTX(NewLabel,NAME_None);
	P_FINISH;
	if (NewState != NAME_None &&
		GetStateFrame() != NULL &&
		!bDeleteMe)
	{
		// gather the current state info
		FStateInfo StateInfo;
		StateInfo.Node		= (INT)GetStateFrame()->Node;
		StateInfo.StateNode = (INT)GetStateFrame()->StateNode;
		StateInfo.Code		= (INT)GetStateFrame()->Code;
		StateInfo.StateName = GetStateFrame()->StateNode ? GetStateFrame()->StateNode->GetFName() : FName(NAME_None);

		// and do a normal transition to the new state, skipping notification
		EGotoState StateResult = GotoState(NewState,false,true);
		if (StateResult != GOTOSTATE_Success)
		{
			// if we failed to transition, pop back to the last state
			UState *StateNode = (UState*)StateInfo.StateNode;
			GetStateFrame()->Node = (UStruct*)StateInfo.Node;
			GetStateFrame()->StateNode = StateNode;
			GetStateFrame()->Code = (BYTE*)StateInfo.Code;
			GetStateFrame()->ProbeMask = (StateNode->ProbeMask | GetClass()->ProbeMask) & StateNode->IgnoreMask;
		}
		else
		{
			// if passed a label, goto that too, otherwise default to begin
			GotoLabel(NewLabel == NAME_None?FName(NAME_Begin):NewLabel);
			// otherwise add our current stateinfo to the stack
			StateStack.AddItem(StateInfo);
			// send pushed notification
			eventPushed();
		}
	}
	unguardexec;
}

void AAIController::execPopState(FFrame &Stack, RESULT_DECL)
{
	guard(PopState);
	P_GET_UBOOL_OPTX(bPopAll,false);
	P_FINISH;
	if (GetStateFrame() != NULL &&
		!bDeleteMe)
	{
		// figure out which state to pop back to
		INT Idx = 0;	// assume to the bottom unless !bPopAll
		if (!bPopAll)
		{
			Idx = StateStack.Num()-1;
		}
		// apply the state if a valid index
		if (Idx >= 0 &&
			Idx < StateStack.Num())
		{
			// send popped notification
			eventPopped();
#ifndef _XBOX
			/* UDebugger - rjp
			Process any pending debug information in this state prior to transitioning to a new state
			todo - the debugger stacknode for this state node must be manually popped in order to maintain
			consistency in the udebugger's callstack.  Something still isn't right here - the udebugger works
			fine for GUI work (which doesn't use PushState/PopState), but doesn't work during a match in UC2...
			I suspect that the problem has to do with where I'm pushing a new stacknode for the state node
			that we're returning to...but it doesn't seem worth fixing, at this time.
			 */
			if ( GDebugger )
			{
				for ( INT tempIdx = Idx; tempIdx < StateStack.Num(); tempIdx++ )
                    GDebugger->DebugInfo( this, GetStateFrame(), DI_PrevStackState, 0, 0 );
			}
#endif

			FStateInfo& StateInfo = StateStack(Idx);
			UState *StateNode = (UState*)StateInfo.StateNode;
			GetStateFrame()->Node = (UStruct*)StateInfo.Node;
			GetStateFrame()->StateNode = StateNode;
			GetStateFrame()->Code = (BYTE*)StateInfo.Code;
			GetStateFrame()->ProbeMask = (StateNode->ProbeMask | GetClass()->ProbeMask) & StateNode->IgnoreMask;
			GetStateFrame()->LatentAction = 0;
			// and remove up to and including the state we just popped from the stack
			if (Idx == 0)
			{
				StateStack.Empty();
			}
			else
			{
				StateStack.Remove(Idx,1);
			}
			// if returning to a previous state, notify it that it is continuing
			if (GetStateFrame()->StateNode != GetClass())
			{
				eventContinueState();
#ifndef _XBOX

				if ( GDebugger )
					GDebugger->DebugInfo( this, GetStateFrame(), DI_NewStackState, 0, 0 );
#endif
			}
		}
	}
	unguardexec;
}

// override gotostate to pop the state stack
void AAIController::execGotoState(FFrame &Stack,RESULT_DECL)
{
	// check to see if we should send the popped notification
	// only if a state was specified (first parameter wasn't omitted)
	if (*(Stack.Code) != EX_Nothing)
	{
		INT StateIdx = StateStack.Num();
		if (StateIdx > 0)
		{
			eventPopped();

			// clear out the state stack
			StateStack.Empty();
		}
	}
	UObject::execGotoState(Stack,Result);
}

static UBOOL IsInState(UState *Node,FName StateName)
{
	for (UState *Test = Node; Test != NULL; Test = Test->GetSuperState())
	{
		if (Test->GetFName() == StateName)
		{
			return 1;
		}
	}
	return 0;
}

// override isinstate to check the state stack
void AAIController::execIsInState(FFrame &Stack,RESULT_DECL)
{
	P_GET_NAME(StateName);
	P_FINISH;

	UBOOL bResult = 0;
	// check the state stack first
	for (INT i = 0; bResult == 0 && i < StateStack.Num(); i++)
	{
		bResult = IsInState((UState*)(StateStack(i).StateNode),StateName);
	}
	// if no luck, try our current state
	if (bResult == 0 &&
		GetStateFrame() != NULL)
	{
		bResult = IsInState(GetStateFrame()->StateNode,StateName);
	}
	*(DWORD*)Result = bResult;
}

/**
 * Script to C++ thunk for caching a file on the hard drive asynchronously
 *
 * @param Stack the stack of the script code
 * @param RESULT_DECL the result of the call
 */
void ALevelInfo::execBackgroundCacheFile(FFrame& Stack,RESULT_DECL)
{
	guard(ALevelInfo::execBackgroundCacheFile);

	P_GET_STR(FileName);
	P_FINISH;

	// If there is a file cache manager, tell it to cache the file
	if (GFileCacheManager != NULL)
	{
		GFileCacheManager->AddFileToCacheList(FileName);
	}

	unguard;
}

/**
 * Script to C++ thunk for caching a file on the hard drive asynchronously
 *
 * @param Stack the stack of the script code
 * @param RESULT_DECL the result of the call
 */
void ALevelInfo::execIsCachingDone(FFrame& Stack,RESULT_DECL)
{
	guard(ALevelInfo::execIsCachingDone);
	P_FINISH;
	// Assume it's done if there isn't a cache manager
	UBOOL bIsDone = 1;
	// If there is a file cache manager, check its status
	if (GFileCacheManager != NULL)
	{
		bIsDone = GFileCacheManager->IsCachingFiles() == 0;
	}
	*(UBOOL*)Result = bIsDone;
	unguard;
}

/**
 * Script to C++ thunk for loading the loading movie into a block of RAM
 * so that it can be played without needing to touch the disk
 *
 * @param Stack the stack of the script code
 * @param RESULT_DECL the result of the call
 */
void ALevelInfo::execLoadLoadingMovie(FFrame& Stack,RESULT_DECL)
{
	guard(ALevelInfo::execLoadLoadingMovie);
	P_FINISH;
	// Stop all sounds to prevent competition for the DVD
	StopAmbientSounds(GetLevel());
	// Force the cleanup of pending delete actors
	XLevel->CleanupDestroyed(1);
	// Dump any memory being used by the object pool
	UObject::FlushObjectPool();
#ifdef _XBOX
	// Now that there is enough memory to spool the movie into ram, load it
	extern void LoadLoadingMovie(void);
	LoadLoadingMovie();
#endif
	*(DWORD*)Result = 0;
	unguard;
}

/**
 * Script to C++ thunk. Returns true if the movie is ready, false otherwise
 *
 * @param Stack the stack of the script code
 * @param RESULT_DECL the result of the call
 */
void ALevelInfo::execIsLoadingMovieReady(FFrame& Stack,RESULT_DECL)
{
	guard(ALevelInfo::execIsLoadingMovieReady);
	P_FINISH;
#ifdef _XBOX
	// Now that there is enough memory to spool the movie into ram, load it
	extern UBOOL IsLoadingMovieReady(void);
	*(DWORD*)Result = IsLoadingMovieReady();
#else
	*(DWORD*)Result = 1;
#endif
	unguard;
}

/**
 * Script to C++ thunk. Loads the texture, UMA, and pushbuffer XPRs for the
 * specified name
 *
 * @param Stack the stack of the script code
 * @param RESULT_DECL the result of the call
 */
void ALevelInfo::execLoadXPRGroup(FFrame& Stack,RESULT_DECL)
{
	guard(ALevelInfo::execLoadXPRGroup);
	P_GET_STR(FileName);
	P_FINISH;
#ifdef _XBOX
	extern void LoadXPRGroup(const TCHAR* Name,UBOOL bAsynchLoad);
	// Loads a group of XPR files for a character/weapon name
	LoadXPRGroup(*FileName,TRUE);
#endif
	*(DWORD*)Result = 0;
	unguard;
}

/**
 * Script to C++ thunk. Verifies that all the XPR files in the group have
 * been loaded. Since they are loaded asynchronously, this will be polled.
 *
 * @param Stack the stack of the script code
 * @param RESULT_DECL the result of the call
 */
void ALevelInfo::execIsXPRGroupLoaded(FFrame& Stack,RESULT_DECL)
{
	guard(ALevelInfo::execIsXPRGroupLoaded);
	P_GET_STR(FileName);
	P_FINISH;
#if defined(XBOX_ASSET_FORMAT) && defined(_XBOX)
	extern UBOOL IsXPRGroupLoaded(const TCHAR* Name);
	// Checks to see if a group of XPR files for a character/weapon name are loaded
	*(DWORD*)Result = IsXPRGroupLoaded(*FileName);
#else
	*(DWORD*)Result = 1;
#endif
	unguard;
}

/**
 * Script to C++ thunk. Checks to see if reducing the ref count will unload them
 *
 * @param Stack the stack of the script code
 * @param RESULT_DECL the result of the call
 */
void ALevelInfo::execWouldXPRGroupUnload(FFrame& Stack,RESULT_DECL)
{
	guard(ALevelInfo::execWouldXPRGroupUnload);
	P_GET_STR(FileName);
	P_FINISH;
#if defined(XBOX_ASSET_FORMAT) && defined(_XBOX)
	extern UBOOL WouldXPRGroupUnload(const TCHAR* Name);
	// Checks to see if a group of XPR files have a ref count of 1
	*(DWORD*)Result = WouldXPRGroupUnload(*FileName);
#else
	*(DWORD*)Result = 0;
#endif
	unguard;
}

/**
 * Script to C++ thunk. Unloads a group of XPR files (reduces the ref count)
 *
 * @param Stack the stack of the script code
 * @param RESULT_DECL the result of the call
 */
void ALevelInfo::execUnloadXPRGroup(FFrame& Stack,RESULT_DECL)
{
	guard(ALevelInfo::execUnloadXPRGroup);
	P_GET_STR(FileName);
	P_FINISH;
#ifdef _XBOX
	extern void UnloadXPRGroup(const TCHAR* Name);
	// Loads a group of XPR files for a character/weapon name
	UnloadXPRGroup(*FileName);
#endif
	*(DWORD*)Result = 0;
	unguard;
}

/* epic ===============================================
* ::execSetPauser
* Author: moelfke
*
* Sets Pauser to NewPauser and, if bNotify is true or
* omitted, notifies any relevant subsystems (audio, etc)
* of the change. Gameplay is paused anytime Pauser != NULL
*
* =====================================================
*/
void ALevelInfo::execSetPauser(FFrame& Stack, RESULT_DECL)
{
	guard(ALevelInfo::execSetPauser);

	P_GET_OBJECT(APlayerReplicationInfo, NewPauser);
	P_GET_UBOOL_OPTX(bNotify, 1);
	P_FINISH;

	APlayerReplicationInfo* OldPauser = Pauser;
	Pauser = NewPauser;
	bNetDirty = 1;

	// if we were unpaused and now we are paused or vice versa
	if (bNotify && ((OldPauser == NULL) != (Pauser == NULL)))
	{
		NotifyPauseChange(Pauser != NULL);
	}

	unguard;
}

FORCEINLINE void ALevelInfo::NotifyPauseChange(UBOOL bNowPaused)
{
	guard(ALevelInfo::NotifyPauseChange);
	//jg -- Check the audio device before making calls on it
	if (GetLevel()->Engine->Audio != NULL)
	{
		GetLevel()->Engine->Audio->NotifyPauseChange(bNowPaused);
	}
	unguard;
}

/* epic ===============================================
* ::execGetClosestNavigationPointTo
* Author: moelfke
*
* Iterates through the NavigationPointList and returns
* the one closest to TargetLocation
*
* =====================================================
*/
void ALevelInfo::execGetClosestNavigationPointTo(FFrame& Stack, RESULT_DECL)
{
	guard(ALevelInfo::execSetPauser);

	P_GET_VECTOR(TargetLocation);
	P_FINISH;

	if (NavigationPointList)
	{
		ANavigationPoint* Best = NavigationPointList;
		FLOAT BestDistSquared = (Best->Location - TargetLocation).SizeSquared();
		for (ANavigationPoint* N = NavigationPointList->nextNavigationPoint; N; N = N->nextNavigationPoint)
		{
			// if this one is closer than the best, set it as our new best
			FLOAT TestDistSquared = (N->Location - TargetLocation).SizeSquared();
			if (TestDistSquared < BestDistSquared)
			{
				Best = N;
				BestDistSquared = TestDistSquared;
			}
		}

		// return the best result found
		*(ANavigationPoint**)Result = Best;
	}
	else
	{
		// no NavigationPointList so we can't do anything
		*(ANavigationPoint**)Result = NULL;
	}

	unguard;
}

/* epic ===============================================
* ::execAllNavigationPoints
* Author: moelfke
*
* Returns all NavigationPoints in the NavigationPointList
* that are of the specified class or a subclass
*
* =====================================================
*/
void ALevelInfo::execAllNavigationPoints(FFrame& Stack, RESULT_DECL)
{
	guard(ALevelInfo::execAllNavigationPoints);

	P_GET_OBJECT(UClass, BaseClass);
	P_GET_OBJECT_REF(ANavigationPoint, OutNav);
	P_FINISH;

	ANavigationPoint* CurrentNav = NavigationPointList;

	// if we have a valid subclass of NavigationPoint
	if (BaseClass && BaseClass != ANavigationPoint::StaticClass())
	{
		PRE_ITERATOR;
			// get the next NavigationPoint in the iteration
			*OutNav = NULL;
			while (CurrentNav && *OutNav == NULL)
			{
				if (CurrentNav->IsA(BaseClass))
				{
					*OutNav = CurrentNav;
				}
				CurrentNav = CurrentNav->nextNavigationPoint;
			}
			if (*OutNav == NULL)
			{
				Stack.Code = &Stack.Node->Script(wEndOffset + 1);
				break;
			}
		POST_ITERATOR;
	}
	else
	{
		// do a faster iteration that doesn't check IsA()
		PRE_ITERATOR;
			// get the next NavigationPoint in the iteration
			if (CurrentNav)
			{
				*OutNav = CurrentNav;
				CurrentNav = CurrentNav->nextNavigationPoint;
			}
			else
			{
				// we're out of NavigationPoints
				*OutNav = NULL;
				Stack.Code = &Stack.Node->Script(wEndOffset + 1);
				break;
			}
		POST_ITERATOR;
	}

	unguardexec;
}

/* epic ===============================================
* ::execPostDynamicLoad
* Author: moelfke
*
* Performs any necessary post-DynamicLoadObject fixup
* on the given object
*
* =====================================================
*/
void ALevelInfo::execPostDynamicLoad(FFrame& Stack, RESULT_DECL)
{
	guard(ALevelInfo::execPostDynamicLoad);

	P_GET_OBJECT(UObject, Obj);
	P_FINISH;

#if defined(_XBOX) && defined(XBOX_ASSET_FORMAT)
	// for meshes, we need to re-call PostLoad() to make sure they get the proper data from XPR
	if (Obj)
	{
		USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(Obj);
		UStaticMesh* StaticMesh = Cast<UStaticMesh>(Obj);
		if (SkelMesh != NULL)
		{
			SkelMesh->ClearAllMeshData();
			SkelMesh->SetAllMeshData();
		}
		else if (StaticMesh != NULL)
		{
			StaticMesh->ClearAllMeshData();
			StaticMesh->SetAllMeshData();
		}
	}
#endif

	unguardexec;
}

/* epic ===============================================
* ::execPreUnload
* Author: moelfke
*
* Call before unloading any XPRs to clean up objects
* that might have been using data from them
*
* WARNING: May destroy Obj
*
* =====================================================
*/
void ALevelInfo::execPreUnload(FFrame& Stack, RESULT_DECL)
{
	guard(ALevelInfo::execPreUnload);

	P_GET_OBJECT(UObject, Obj);
	P_FINISH;

	// if object is invalid or it's native/persistent and shouldn't be deleted/reloaded, ignore it
	if (!Obj || (Obj->GetFlags() & RF_Native) || (Obj->GetFlags() & RF_TagPersist))
	{
		return;
	}

	GetLevel()->CleanupDestroyed(true);

#if defined(_XBOX) && defined(XBOX_ASSET_FORMAT)
	if (Obj->IsA(UTexture::StaticClass()))
	{
		FBaseTexture* Resource = ((UTexture*)Obj)->GetRenderInterface();
		if (Resource)
		{
			Resource->Flush();
		}
	}
	else if (Obj->IsA(USkeletalMesh::StaticClass()))
	{
		// if any objects are referencing the skeletal mesh, reload its data from XPR
		// warning: only checking dynamic actors in the level
		// since we don't use direct refs in defaultproperties, this is safe
		UBOOL bFound = false;
		for (INT i = GetLevel()->iFirstDynamicActor; i < GetLevel()->Actors.Num() && !bFound; i++)
		{
			if (GetLevel()->Actors(i) && GetLevel()->Actors(i)->Mesh == Obj)
			{
				if (GetLevel()->Actors(i)->bDestroyOnOwnerLogout)
				{
					// we should kill the actor instead of counting the mesh as referenced
					GetLevel()->DestroyActor(GetLevel()->Actors(i));
					GetLevel()->CleanupDestroyed(true);
				}
				else
				{
					debugf(TEXT("Not deleting %s because referenced by %s"), Obj->GetFullName(), GetLevel()->Actors(i)->GetFullName());
					((USkeletalMesh*)Obj)->ClearAllMeshData();
					bFound = true;
				}
			}
		}
		// if the mesh was not found, delete it
		// warning: assuming that if the mesh is referenced, the XPR it is using is still around
		if (!bFound)
		{
			debugf(TEXT("Deleting %s"), Obj->GetFullName());
			delete Obj;
		}
	}
	else if (Obj->IsA(UStaticMesh::StaticClass()))
	{
		((UStaticMesh*)Obj)->ClearAllMeshData();
	}
	else if (Obj->IsA(UMeshAnimation::StaticClass()))
	{
		// verify that no skeletal mesh instances are using this animation
		UBOOL bFound = false;
		for (TObjectIterator<UPrimitive> It; It && !bFound; ++It)
		{
			USkeletalMeshInstance* MInst = Cast<USkeletalMeshInstance>(*It);
			if (MInst)
			{
				for (INT i = 0; i < MInst->AnimLinkups.Num(); i++)
				{
					if (MInst->AnimLinkups(i).Anim == Obj)
					{
						debugf(TEXT("Not deleting %s because referenced by %s"), Obj->GetFullName(), It->GetFullName());
						bFound = true;
					}
				}
			}
			else if (It->IsA(USkeletalMesh::StaticClass()) && ((USkeletalMesh*)*It)->DefaultAnim == Obj)
			{
				debugf(TEXT("Not deleting %s because referenced by %s"), Obj->GetFullName(), It->GetFullName());
				bFound = true;
			}	
		}
		// if the animation was not found, delete it
		// warning: assuming that if the anim is referenced, the XPR it is using is still around
		if (!bFound)
		{
			debugf(TEXT("Deleting %s"), Obj->GetFullName());
			delete Obj;
		}
	}
	// warning: ignoring static meshes, as it isn't safe to delete them due to direct refs all over the place
	// assuming there's no duplicate refs to them, the next time they're needed they'll get DLO'ed by LevelInfo
	// and get PostDynamicLoad() called for them to fix their data
#endif

	unguardexec;
}

/* epic ===============================================
* ::execPostUnload
* Author: moelfke
*
* Call after unloading any XPRs to reinitialize objects
* that might have been using data from the now unloaded
* XPRs and need to be hooked up to the remaining XPRs
*
* =====================================================
*/
void ALevelInfo::execPostUnload(FFrame& Stack, RESULT_DECL)
{
	guard(ALevelInfo::execPostUnload);

	P_GET_OBJECT(UObject, Obj);
	P_FINISH;

	// if object is invalid or it's native/persistent and shouldn't be deleted/reloaded, ignore it
	if (!Obj || (Obj->GetFlags() & RF_Native) || (Obj->GetFlags() & RF_TagPersist))
	{
		return;
	}

#if defined(_XBOX) && defined(XBOX_ASSET_FORMAT)
	if (Obj->IsA(USkeletalMesh::StaticClass()))
	{
		((USkeletalMesh*)Obj)->SetAllMeshData();
	}
	else if (Obj->IsA(UStaticMesh::StaticClass()))
	{
		((UStaticMesh*)Obj)->SetAllMeshData();
	}
#endif

	unguard;
}

//jg -- Moved the code from DynamicLoadAll to here for reuse
void ALevelInfo::WaitForDynamicLoadingComplete(void)
{
	guard(ALevelInfo::WaitForDynamicLoadingComplete);
	while (DynamicLoadList.Num() || LoadingXPRs.Num())
	{
		GInitRunaway();
		eventDynamicLoadNext();
	}
	unguard;
}


/* epic ===============================================
* ::execDynamicLoadAll
* Author: moelfke
*
* Blocks until all dynamic loads have completed. Used
* in some single player matches where we must not delay
* spawning of things because it would break triggers
* and such. Must be native to avoid crashing due to
* the script recursion limit.
*
* =====================================================
*/
void ALevelInfo::execDynamicLoadAll(FFrame& Stack, RESULT_DECL)
{
	guard(ALevelInfo::execDynamicLoadAll);

	P_FINISH;

	WaitForDynamicLoadingComplete();

	unguard;
}

void AActor::execMoveVectorTo(FFrame& Stack, RESULT_DECL)
{
	P_GET_VECTOR_REF(out_Result);
	P_GET_VECTOR(Old);
	P_GET_VECTOR(Desired);
	P_GET_VECTOR(InterpSpeed);
	P_GET_FLOAT(DeltaTime);
	P_GET_UBOOL(bSkipNormalize);
	P_FINISH;

	MoveVectorTo(*out_Result, Old, Desired, InterpSpeed, DeltaTime, bSkipNormalize);
}

void AActor::MoveVectorTo(FVector& out_Result, const FVector& Old, const FVector& Desired, const FVector& InterpSpeed, FLOAT DeltaTime, UBOOL bSkipNormalize)
{
	guardSlow(AActor::MoveVectorTo);

	// If points are coincident return
	FVector Dir = Desired - Old;
	if (Dir.IsZero())
	{
		out_Result = Desired;
	}
	else
	{
		// Happy accident... leaving out the normalize gives a smooth feel to the camera interp
		if (!bSkipNormalize)
		{
			Dir.Normalize();
		}

		// Figure out where new point would be
		// If new point is farther from the old point than the desired point
		// we passed the desired point and can set new to desired
		out_Result = Old + Dir * DeltaTime * InterpSpeed;

		FVector OldToDesired = Desired - Old;
		FVector OldToNew = out_Result - Old;

		// Check distance to each component
		// If dist from old to new is longer than dist from old to desired
		// the component has exceeded it's destination (b/c lines are colinear)
		// and the value should just be set
		if (Square(OldToDesired.X) <= Square(OldToNew.X))
		{
			out_Result.X = Desired.X;
		}
		if (Square(OldToDesired.Y) <= Square(OldToNew.Y))
		{
			out_Result.Y = Desired.Y;
		}
		if (Square(OldToDesired.Z) <= Square(OldToNew.Z))
		{
			out_Result.Z = Desired.Z;
		}
	}

	unguardSlow;
}

/* epic ===============================================
* ::execGetLatentAction
* Author: moelfke
*
* Returns the name of the latent function this Actor is
* currently executing, or "None" if none
*
* =====================================================
*/
void AActor::execGetLatentAction(FFrame& Stack, RESULT_DECL)
{
	guard(AActor::execGetLatentAction);

	P_FINISH;

	if (GetStateFrame() && GetStateFrame()->Code && GetStateFrame()->LatentAction)
	{
		// this sucks but there's no nice way to get from the LatentAction number to the script name of the function that set it
		switch (GetStateFrame()->LatentAction)
		{
			case 384: // EPOLL_Sleep
				*(FString*)Result = TEXT("Sleep");
				break;
			case 385: // EPOLL_FinishAnim
				*(FString*)Result = TEXT("FinishAnim");
				break;
			case 302: // EPOLL_FinishInterpolation
				*(FString*)Result = TEXT("FinishInterpolation");
				break;
			case 501: // AI_PollMoveTo
				*(FString*)Result = TEXT("MoveTo");
				break;
			case 503: // AI_PollMoveToward
				*(FString*)Result = TEXT("MoveToward");
				break;
			case 509: // AI_PollFinishRotation
				*(FString*)Result = TEXT("FinishRotation");
				break;
			case 528: // AI_PollWaitForLanding
				*(FString*)Result = TEXT("WaitForLanding");
				break;
			case 511: // AI_PollWaitToSeeEnemy
				*(FString*)Result = TEXT("WaitToSeeEnemy");
				break;
			default:
				*(FString*)Result = FString::Printf(TEXT("%i"), (INT)GetStateFrame()->LatentAction);
				break;
		}
	}
	else
	{
		*(FString*)Result = TEXT("None");
	}

	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

