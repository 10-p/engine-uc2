/* scion ======================================================================
* Author: sz
* ============================================================================
* URedigestAnimationsCommandlet.cpp
* 
* Given a wildcard list of animation packages. Reprocess the animation data.
*
* usage:
* redigestanimations pkgname(*.ukx)
* ============================================================================
*/

#include "EditorPrivate.h"

// get directory portion of the path
static FString GetDirName( const FString &Path )
{
	INT chopPoint;

	chopPoint = Max (Path.InStr( TEXT("/"), 1 ) + 1, Path.InStr( TEXT("\\"), 1 ) + 1);

	if (chopPoint < 0)
		chopPoint = Path.InStr( TEXT("*"), 1 );

	if (chopPoint < 0)
		return (TEXT(""));

	return (Path.Left( chopPoint ) );
}

class URedigestAnimationsCommandlet : public UCommandlet
{
	DECLARE_CLASS(URedigestAnimationsCommandlet,UCommandlet,CLASS_Transient,Editor);
	
	void StaticConstructor()
	{
		guard(URedigestAnimationsCommandlet::StaticConstructor);

		LogToStdout     = 0;
		IsClient        = 1;
		IsEditor        = 1;
		IsServer        = 1;
		LazyLoad        = 1;
		ShowErrorCount  = 1;

		unguard;
	}

	INT Main( const TCHAR* Parms )
	{
		guard(URedigestAnimationsCommandlet::Main);

		// get the filename (wildcards ok)
		FString WildCard;
		if( !ParseToken(Parms,WildCard,0) )
			appErrorf(TEXT("Package search mask not specified."));

		GLazyLoad = 0;

		// for each file found matching the wildcard
		TArray<FString> FilesFound = GFileManager->FindFiles( *WildCard, 1, 1 );
		// save directory name
		FString Path = GetDirName( WildCard );

		// process each file
		for( INT i=0; i < FilesFound.Num(); i++ )
		{
			// open the package matching that file
			FString Pkg = Path + FilesFound(i);
			// but only animation packages
			if( Pkg.InStr( TEXT(".ukx"),1) == INDEX_NONE )
				continue;

			warnf( TEXT("\nLoading package %s...\n"), *Pkg );
			UPackage* Package = Cast<UPackage>( LoadPackage(NULL,*Pkg,LOAD_NoWarn) );

			if( Package )
			{
				// for each animation set in the package
				for( TObjectIterator<UMeshAnimation> It; It; ++It )
				{
					UMeshAnimation* AnimSet = *It;

					// only check objects within our loaded package
					if( AnimSet->IsIn(Package) )
					{
						UBOOL bModified=0;
						
						warnf( TEXT("\nRedigesting anims for [%s]"), AnimSet->GetFullName() );

						// process anims
						RedigestAnimSet( AnimSet, &bModified );

						Package->bDirty |= bModified;
					}
				}

				// if anything was updated
				if( Package->bDirty )
				{
					// save the package
					warnf( TEXT("\nSaving package %s...\n"), *Pkg );
					SavePackage( Package, NULL, RF_Standalone, *Pkg, GError, NULL );
				}
				else
				{
					warnf( TEXT("\nNothing changed.. \n"), *Pkg );
				}
			}
			UObject::CollectGarbage(RF_Native);
		}			
		
		GIsRequestingExit=1;
		return 0;
		unguard;
	}

private:
	//
	// Double-accurate normalization and W-alignment to prepare for compression.
	//
	FQuat16 NormalizeAndQuantizeQuat( FQuat& Quaternion )
	{		
		DOUBLE X,Y,Z,W;
		X = Quaternion.X;
		Y = Quaternion.Y;
		Z = Quaternion.Z;
		W = Quaternion.W;

		DOUBLE SquareSum = X*X+Y*Y+Z*Z+W*W;
		if( SquareSum >= 1e-20 )
		{
			DOUBLE Scale = 1.0/appSqrt(SquareSum);
			X *= Scale; 
			Y *= Scale; 
			Z *= Scale;
			W *= Scale;
		}
		else 
		{	
			X = 0.0;
			Y = 0.0;
			Z = 0.0;
			W = 1.0;
		}

		if( W < 0 )
		{
			W = -W;
			X = -X;
			Y = -Y;
			Z = -Z;		
		}

		Quaternion.X = X;
		Quaternion.Y = Y;
		Quaternion.Z = Z;
		Quaternion.W = W;

		FQuat16 OutQuat;

		OutQuat.X = (INT)(Quaternion.X * (DOUBLE)Quant16BitFactor) + Quant16BitOffs;
		OutQuat.Y = (INT)(Quaternion.Y * (DOUBLE)Quant16BitFactor) + Quant16BitOffs;
		OutQuat.Z = (INT)(Quaternion.Z * (DOUBLE)Quant16BitFactor) + Quant16BitOffs;

		return OutQuat;
	}

	// reprocess the animation set
	// this is just used for some flextrack processing ATM
	void RedigestAnimSet( UMeshAnimation* AnimSet, UBOOL* bModified )
	{
		guard(URedigestAnimationsCommandlet::RedigestAnimSet);
        check(AnimSet);

		const INT StatMAX=4;
		DWORD StatNumConverted[StatMAX]={0,0,0,0};
		TCHAR* StatText[StatMAX]=
		{
			TEXT("ADT_Static converted to ADT_48BitStatic"),
			TEXT("ADT_Static converted to ADT_48BitStaticRotOnly"),
			TEXT("ADT_48BitRotOnly converted to ADT_48BitRotOnlyNoPos"),
			TEXT("ADT_Static converted to EMPTY")			
		};

		for( INT motIdx=0; motIdx < AnimSet->Moves.Num(); motIdx++ )
		{
			MotionChunk& Motion = AnimSet->Moves(motIdx);
			for( INT trackIdx=0; trackIdx < Motion.FlexTracks.Num(); trackIdx++ )
			{
                FlexTrackSlot& AnimTrack = Motion.FlexTracks(trackIdx);
				if( AnimTrack.FlexTrackPtr )
				{				
					// convert from old static type to new quantized type
					if( AnimTrack.FlexTrackPtr->GetClassType() == ADT_Static )
					{
						// save old track info
						FlexTrackStatic SavedTrack = *((FlexTrackStatic*)AnimTrack.FlexTrackPtr);
						FQuat SavedQuat = SavedTrack.Orientation.GetQuat();
						FQuat16 NewQuantizedQuat = NormalizeAndQuantizeQuat( SavedQuat );
						// convert to ..
						// quantized static track with position
						if( SavedTrack.Positions.Num() )
						{
							AnimTrack.AllocateTrack( ADT_48BitStatic );
							check( AnimTrack.FlexTrackPtr && AnimTrack.FlexTrackPtr->GetClassType()==ADT_48BitStatic );
							((FlexTrack48Static*)AnimTrack.FlexTrackPtr)->Orientation = NewQuantizedQuat;
							((FlexTrack48Static*)AnimTrack.FlexTrackPtr)->Position = SavedTrack.Positions(0);
							StatNumConverted[0]++;
						}
						// quantized static track with no position
						else
						{
							const FLOAT MinEpsilon=0.001f;
							if( Abs(SavedQuat.X) < MinEpsilon &&
								Abs(SavedQuat.Y) < MinEpsilon &&
								Abs(SavedQuat.Z) < MinEpsilon )
							{
								// zero rotation and zero position so don't need this entry
								AnimTrack.AllocateTrack( ADT_Empty );
								StatNumConverted[3]++;
							}
							else
							{
								AnimTrack.AllocateTrack( ADT_48BitStaticRotOnly );
								check( AnimTrack.FlexTrackPtr && AnimTrack.FlexTrackPtr->GetClassType()==ADT_48BitStaticRotOnly );
								((FlexTrack48StaticRotOnly*)AnimTrack.FlexTrackPtr)->Orientation = NewQuantizedQuat;
								StatNumConverted[1]++;
							}
						}
					}
					else if( AnimTrack.FlexTrackPtr->GetClassType() == ADT_48BitRotOnly )
					{
						// save old track info
						FlexTrack48RotOnly SavedTrack = *((FlexTrack48RotOnly*)AnimTrack.FlexTrackPtr);
						// convert to..
						// rotation only with no position
						if( SavedTrack.Positions.Num()==0 )
						{
							AnimTrack.AllocateTrack( ADT_48BitRotOnlyNoPos );
							check( AnimTrack.FlexTrackPtr && AnimTrack.FlexTrackPtr->GetClassType()==ADT_48BitRotOnlyNoPos );
							((FlexTrack48RotOnlyNoPos*)AnimTrack.FlexTrackPtr)->Orientations = SavedTrack.Orientations;
							((FlexTrack48RotOnlyNoPos*)AnimTrack.FlexTrackPtr)->TimeKeys = SavedTrack.TimeKeys;
							StatNumConverted[2]++;
						}						
					}
				}
			}
		}

		// mark package as modified
		UBOOL bLocalModified=0;
		for( INT i=0; i<StatMAX; i++ ) {
			if( StatNumConverted[i]>0 ) {
				bLocalModified=1;
			}
		}
		if( bModified ) {
				*bModified = bLocalModified;
		}

		// print some stats of what changed
		for( INT i=0; i<StatMAX; i++ )
		{
			if( StatNumConverted[i]>0 ) {
				warnf( TEXT("Num %s = %d"), StatText[i], StatNumConverted[i] );
			}			
		}

		unguard;
	}
};
IMPLEMENT_CLASS(URedigestAnimationsCommandlet)

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
