/*=============================================================================
	UnParticleSystem.h: Unreal Particle System.
	Copyright 2001 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Daniel Vogel
=============================================================================*/

enum EParticleFlags
{
	PTF_None			= 0,
	PTF_Active			= 1,
	PTF_NoTick			= 2,
	PTF_InitialSpawn	= 4,
	PTF_Respawn			= 8		// Laurent -- Location interpolation
};

enum EParticleSpawnFlags
{
	PSF_None			= 0,
	PSF_NoGlobalOffset	= 1,
	PSF_NoOwnerLocation	= 2
};

struct FParticleBeamData
{
	FVector		Location;
	FLOAT		t;

	friend FArchive &operator<<( FArchive& Ar, FParticleBeamData& D )
	{
		if( !Ar.IsSaving() && !Ar.IsLoading() )
		{
			Ar << D.Location << D.t;
		}
		return Ar;
	}
};

struct FParticleBeamEndPoint
{
	INT				DetermineEndPointBy;
	FName			ActorTag;
	FRangeVector	Offset;
	FLOAT			Weight;

	friend FArchive &operator<<( FArchive& Ar, FParticleBeamEndPoint& D )
	{
		if( !Ar.IsSaving() && !Ar.IsLoading() )
		{
			Ar << D.DetermineEndPointBy << D.ActorTag << D.Offset << D.Weight;
		}
		return Ar;
	}
};


struct FParticleBeamScale
{
	FVector		FrequencyScale;
	FLOAT		RelativeLength;		// always in range [0..1]

	friend FArchive &operator<<( FArchive& Ar, FParticleBeamScale& D )
	{
		if( !Ar.IsSaving() && !Ar.IsLoading() )
		{
			Ar << D.FrequencyScale << D.RelativeLength;
		}
		return Ar;
	}
};

struct FParticleSparkData
{
	FLOAT		TimeBeforeVisible;
	FLOAT		TimeBetweenSegments;
	FVector		StartLocation;
	FVector		StartVelocity;

	friend FArchive &operator<<( FArchive& Ar, FParticleSparkData& D )
	{
		if( !Ar.IsSaving() && !Ar.IsLoading() )
		{
			Ar << D.TimeBeforeVisible << D.TimeBetweenSegments << D.StartLocation << D.StartVelocity;
		}
		return Ar;
	}
};

struct FParticleTrailData
{
	FVector		Location;
	FColor		Color;
	FLOAT		Size;
	DOUBLE		Time;

	friend FArchive &operator<<( FArchive& Ar, FParticleTrailData& D )
	{
		if( !Ar.IsSaving() && !Ar.IsLoading() )
		{
			Ar << D.Location << D.Color << D.Size << D.Time;
		}
		return Ar;
	}
};

struct FParticleTrailInfo
{
	INT			TrailIndex;
	INT			NumPoints;
	FVector		LastLocation;

	friend FArchive &operator<<( FArchive& Ar, FParticleTrailInfo& D )
	{
		if( !Ar.IsSaving() && !Ar.IsLoading() )
		{
			Ar << D.TrailIndex << D.NumPoints << D.LastLocation;
		}
		return Ar;
	}
};

struct FUCParticleTrailData // -- Laurent
{
	FVector		Location;
	FColor		Color;
	FLOAT		Size;
	DOUBLE		Time;

	friend FArchive &operator<<( FArchive& Ar, FUCParticleTrailData& D )
	{
		if( !Ar.IsSaving() && !Ar.IsLoading() )
		{
			Ar << D.Location << D.Color << D.Size << D.Time;
		}
		return Ar;
	}
};

struct FUCParticleTrailInfo // -- Laurent
{
	INT			TrailIndex;
	INT			NumPoints;
	FVector		LastLocation;		// last point location (to compute new points)
	FVector		LastEmitterLocation;

	friend FArchive &operator<<( FArchive& Ar, FUCParticleTrailInfo& D )
	{
		if( !Ar.IsSaving() && !Ar.IsLoading() )
		{
			Ar << D.TrailIndex << D.NumPoints << D.LastLocation << D.LastEmitterLocation;
		}
		return Ar;
	}
};

struct FParticleTimeScale
{
	FLOAT		RelativeTime;		// always in range [0..1]
	FLOAT		RelativeSize;

	friend FArchive &operator<<( FArchive& Ar, FParticleTimeScale& D )
	{
		if( !Ar.IsSaving() && !Ar.IsLoading() )
		{
			Ar << D.RelativeTime << D.RelativeSize;
		}
		return Ar;
	}
};

struct FParticleRevolutionScale
{
	FLOAT		RelativeTime;		// always in range [0..1]
	FVector		RelativeRevolution;

	friend FArchive &operator<<( FArchive& Ar, FParticleRevolutionScale& D )
	{
		if( !Ar.IsSaving() && !Ar.IsLoading() )
		{
			Ar << D.RelativeTime << D.RelativeRevolution;
		}
		return Ar;
	}
};

struct FParticleColorScale
{
	FLOAT		RelativeTime;		// always in range [0..1]
	FColor		Color;

	friend FArchive &operator<<( FArchive& Ar, FParticleColorScale& D )
	{
		if( !Ar.IsSaving() && !Ar.IsLoading() )
		{
			Ar << D.RelativeTime << D.Color;
		}
		return Ar;
	}
};

struct FParticleVelocityScale
{
	FLOAT	RelativeTime;		// always in range [0..1]
	FVector	RelativeVelocity;

	friend FArchive &operator<<( FArchive& Ar, FParticleVelocityScale& D )
	{
		if( !Ar.IsSaving() && !Ar.IsLoading() )
		{
			Ar << D.RelativeTime << D.RelativeVelocity;
		}
		return Ar;
	}
};

class USound;
struct FParticleSound
{
	USound*	Sound;
	FRange	Radius;
	FRange	Pitch;
	INT		Weight;
	FRange	Volume;
	FRange	Probability;

	friend FArchive &operator<<( FArchive& Ar, FParticleSound& D )
	{
		if( !Ar.IsSaving() && !Ar.IsLoading() )
		{
			Ar << D.Radius << D.Pitch << D.Weight << D.Volume << D.Probability;
		}
		return Ar;
	}
};

struct FParticle
{
	FVector		Location;
	FVector		OldLocation;
	FVector		Velocity;
	FVector		StartSize;
	FVector		SpinsPerSecond;
	FVector		StartSpin;
	FVector		RevolutionCenter;
	FVector		RevolutionsPerSecond;
	FVector		RevolutionsMultiplier;
	FVector		Size;
	FVector		StartLocation;
	FVector		ColorMultiplier;
	FVector		VelocityMultiplier;
// scion sz - not used
//	FVector		OldMeshLocation;
	FColor		Color;
	FLOAT		Time;
	FLOAT		MaxLifetime;
// scion sz - not used
//	FLOAT		Mass;
	INT			HitCount;
	INT			Flags;
	INT			Subdivision;
	FLOAT		SubdivisionPlayTime;
// scion sz - not used
//	INT			BoneIndex;

	friend FArchive &operator<<( FArchive& Ar, FParticle& D )
	{
		if( !Ar.IsSaving() && !Ar.IsLoading() )
		{
			Ar	<< D.Location 
				<< D.OldLocation
				<< D.Velocity
				<< D.StartSize
				<< D.SpinsPerSecond
				<< D.StartSpin
				<< D.RevolutionCenter
				<< D.RevolutionsPerSecond
				<< D.RevolutionsMultiplier
				<< D.Size
				<< D.StartLocation
				<< D.ColorMultiplier
				<< D.VelocityMultiplier
// scion sz - not used
//				<< D.OldMeshLocation
				<< D.Color
				<< D.Time
				<< D.MaxLifetime
// scion sz - not used
//				<< D.Mass
				<< D.HitCount
				<< D.Flags
				<< D.Subdivision
				<< D.SubdivisionPlayTime;
// scion sz - not used
//				<< D.BoneIndex;
		}
		return Ar;
	}
};

//scion superville: Spawn arbitrary actors at collision locations
struct FCollisonSpawnInfo
{
	UClass	   *ActorClass;
	FLOAT		DistFromHit;
	FVector		LocationOffset;
	FRotator	RotationOffset;
};


//
//	FParticleVertexStream
//
template<class VertexClass> class FParticleVertexStream : public FVertexStream
{
public:

	TArray<VertexClass>			Vertices;

	FParticleVertexStream()
	{
	}

	virtual INT GetRevision()
	{
		return 1;
	}

	virtual INT GetSize()
	{
		return Vertices.Num() * sizeof(VertexClass);
	}

	virtual INT GetStride()
	{
		return sizeof(VertexClass);
	}

	virtual INT GetComponents(FVertexComponent* OutComponents)
	{
		return VertexClass::GetComponents(OutComponents);
	}

	virtual void GetStreamData(void* Dest)
	{
		appMemcpy(Dest,&Vertices(0),Vertices.Num() * sizeof(VertexClass));
	}
	virtual void GetRawStreamData(void ** Dest, INT FirstVertex )
	{
		*Dest = &Vertices(FirstVertex);
	}
};

/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/

