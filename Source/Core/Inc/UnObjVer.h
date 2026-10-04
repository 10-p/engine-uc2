/*=============================================================================
	UnObjVer.h: Unreal object version.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

/*-----------------------------------------------------------------------------
	Version coding.
-----------------------------------------------------------------------------*/

// Offset demo version number by 1000
#if DEMOVERSION
#define DEMO_VERSION_OFFSET 0
#else
#define DEMO_VERSION_OFFSET 1000
#endif


// Earliest engine build that is network compatible with this one.
#define ENGINE_MIN_NET_VERSION (1000+DEMO_VERSION_OFFSET)

// Engine build number, for displaying to end users.
#define ENGINE_VERSION (1226+DEMO_VERSION_OFFSET)

// Base protocol version to negotiate in network play.
#define ENGINE_NEGOTIATION_VERSION (1000+DEMO_VERSION_OFFSET)

// Prevents incorrect files from being loaded.
#define PACKAGE_FILE_TAG 0x9E2A83C1


// Package file version #  history :
// 124 - Added BoneCollisionSpheres, BoneCollisionBoxes & KPhysicsProps to USkeletalMesh - JAG
// 125 - Added BoneCollisionBoxesModels (UModels corresponding to the BoneCollisionBoxes array) - JAG
// 126 - Changed static mesh collision code - vogel
//	Version   127:  GPU vertex streams added to USkeletalMesh FStaticLODModel.
// 	Version   128:  GPU bone arrays  added to USkeletalMesh FStaticLODModel.
//	Version   129:  jg -- kDOP collision changes for static meshes
//	Version	  130:  sz -- Changes for preserving skeleton bone lengths:
//					FNamedBone updated to store ref pose positions, FlexTrack48RotOnly updated, FlexTrackStatic updated
//  Version   131:  Reduced property object size
//  Version	  132:  sz -- Added TranslationMethod to FNamedBone
//	Version   133:  jg -- Added MaxInstances && NumPoolable to UClass serialization
//	Version   134:  jg -- Removed the InclusiveSphereBound from FBspNode
//	Version   135:  jg -- Added compression support for static meshes
//	Version   136:  jg -- Added compression support for skeletal meshes
//	Version   137:  jg -- Added automatic compression error fixing for static meshes
//  Version   138:  rwd -- Changes to function hash, memory savings, etc
//	Version   139:  jg -- Removed the ClassGuid from UClass
//	Version   140:  jg -- Removed Karma
//	Version   141:  jg -- Removed the ZoneDist array
//	Version   142:  jg -- Removed the MaxInstances & NumPoolable from UClass serialization (8 version lifetime on that)
//	Version   143:  jg -- Changed the LatentAction from an int to a word (2 bytes)
//	Version   144:  jg -- More BSP bloat cleanup FLeaf::iVolumetric was removed
//	Version   145:  jg -- Changed the serialization of AR_INDEX to be fast files sizes are slightly larger but who cares
//	Version   146:  jg -- Changed FBox to no longer store IsValid but compute it
//	Version	  147:  sz -- Changed UMeshAnimtion to load motion chunks from a single block of memory
//	Version   148:  sz -- DELTA_DEFAULTS. Tagged properties serialized by ref instead of by name.
//	Version   149:  sz -- Trimming some fat off of animations. Removed BoneIndices, Changed/Added some flex tracks
//	Version   150:  jg -- Reduced the memory used by ULinkers
//	Version   151:  jg -- Added the bIsInXPR for bulk loading of UMA data
#define PACKAGE_FILE_VERSION 151

//jg -- The version of our data that means it is in Xbox form
#define XBOX_PACKAGE_FORMAT 0x01

// Licensee Version Number.
#ifdef XBOX_ASSET_FORMAT
	//jg -- This is used to save our assets in their final form. Various serialize
	// functions look at the PACKAGE_FILE_VERSION_LICENSEE to see if the asset was
	// saved in the compact Xbox form or not. Different serialization semantics
	// happen based upon this flag.
	#define PACKAGE_FILE_VERSION_LICENSEE	(XBOX_PACKAGE_FORMAT)
#else
	#define PACKAGE_FILE_VERSION_LICENSEE	0x00
#endif

#define VERSION_ENGINE_ONLY				0x00

// The earliest file version which we can load with complete
// backwards compatibility. Must be at least PACKAGE_FILE_VERSION.
#define PACKAGE_MIN_VERSION 60

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

