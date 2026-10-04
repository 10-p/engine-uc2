/*=============================================================================
	UnActor.h: AActor class FORCEINLINEs.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Tim Sweeney
        * Aug 30, 1996: Mark added PLevel
		* Oct 19, 1996: Tim redesigned to eliminate redundency
=============================================================================*/

/*-----------------------------------------------------------------------------
	FActorLink.
-----------------------------------------------------------------------------*/

//
// Linked list of actors.
//
struct FActorLink
{
	// Variables.
	AActor*     Actor;
	FActorLink* Next;

	// Functions.
	FActorLink( AActor* InActor, FActorLink* InNext )
	: Actor(InActor), Next(InNext)
	{}
};

/*-----------------------------------------------------------------------------
	AActor FORCEINLINEs.
-----------------------------------------------------------------------------*/

//
// Brush checking.
//
FORCEINLINE UBOOL AActor::IsBrush()       const {return Brush!=NULL && ((AActor *)this)->IsABrush();}
FORCEINLINE UBOOL AActor::IsStaticBrush() const {return Brush!=NULL && ((AActor *)this)->IsABrush() && bStatic && !((AActor *)this)->IsAVolume(); }
FORCEINLINE UBOOL AActor::IsMovingBrush() const {return StaticMesh!=NULL && ((AActor *)this)->IsAMover();}
FORCEINLINE UBOOL AActor::IsVolumeBrush() const {return Brush!=NULL && ((AActor *)this)->IsAVolume();}
FORCEINLINE UBOOL AActor::IsEncroacher()  const {return bCollideActors && IsMovingBrush();}

//
// See if this actor is owned by TestOwner.
//
FORCEINLINE UBOOL AActor::IsOwnedBy( const AActor* TestOwner ) const
{
	guardSlow(AActor::IsOwnedBy);
	for( const AActor* Arg=this; Arg; Arg=Arg->Owner )
	{
		if( Arg == TestOwner )
			return 1;
	}
	return 0;
	unguardSlow;
}

//
// Get the top-level owner of an actor.
//
FORCEINLINE AActor* AActor::GetTopOwner()
{
	AActor* Top;
	for( Top=this; Top->Owner; Top=Top->Owner );
	return Top;
}


//
// See if this actor is in the specified zone.
//
FORCEINLINE UBOOL AActor::IsInZone( const AZoneInfo* TestZone ) const
{
	return Region.Zone!=Level ? Region.Zone==TestZone : 1;
}

//
// Return whether this actor's movement is based on another actor.
//
FORCEINLINE UBOOL AActor::IsBasedOn( const AActor* Other ) const
{
	guard(AActor::IsBasedOn);
	for( const AActor* Test=this; Test!=NULL; Test=Test->Base )
		if( Test == Other )
			return 1;
	return 0;
	unguard;
}

//
// Return the level of an actor.
//
FORCEINLINE class ULevel* AActor::GetLevel() const
{
	return XLevel;
}

//
// Return the actor's view rotation.
//
FORCEINLINE FRotator AActor::GetViewRotation()
{
	guardSlow(AActor::GetViewRotation);
	return (IsA(APawn::StaticClass()) && ((APawn*)this)->Controller) ? ((APawn*)this)->Controller->Rotation : Rotation;
	unguardSlow;
}


FORCEINLINE FDynamicActor* AActor::GetActorRenderData()
{
	guard(GetActorRenderData);
	// scion sz - shouldn't try to get the render data for actors with bDeleteMe 
	check(!bDeleteMe || GIsEditor);

	if(!ActorRenderData)
		ActorRenderData = NEW FDynamicActor(this);

	if(ActorRenderData->Revision != RenderRevision)
		UpdateRenderData();

	return ActorRenderData;
	unguardf(( TEXT("%s"), GetName() ));
}

FORCEINLINE FDynamicLight* AActor::GetLightRenderData()
{
	guard(GetLightRenderData);
	// scion sz - shouldn't try to get the render data for actors with bDeleteMe 
	check(!bDeleteMe || GIsEditor);

	if(!LightRenderData)
	{
		if( bDynamicLight )
			LightRenderData = NEW FVolatileDynamicLight(this);
		else
			LightRenderData = NEW FDynamicLight(this);
	}

	if(LightRenderData->Revision != RenderRevision)
		LightRenderData->Update();

	return LightRenderData;
	unguardf(( TEXT("%s"), GetName() ));
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

