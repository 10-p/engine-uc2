/*=============================================================================
	UnVertexShaderConfigs.cpp: 
	Copyright 2004 Scion Studios, LLC. All Rights Reserved.

	Revision history:
		* Created by Sam Zamani
=============================================================================*/

#include "EnginePrivate.h"

IMPLEMENT_CLASS( UVertexShaderConfigs );
IMPLEMENT_CLASS( UUserVertexShaderConfigs );

/* scion ======================================================================
* UVertexShaderConfigs::CleanOldEntries
* Author: sz
* 
* remove stale entries and combine some of the lighting cases
* ============================================================================
*/
void UVertexShaderConfigs::CleanOldEntries()
{
	TArray<FString> NewEnums;
	for( INT Idx=0; Idx < VertexShaderEnums.Num(); Idx++ )
	{
		FString& OriginalEnumStr = VertexShaderEnums(Idx);
		QWORD VertexShaderEnum = (QWORD) appAtoi64( *OriginalEnumStr );

		UBOOL	bHasPoint=0,
				bHasSpot=0,
				bHasDirectional=0,
				bHasNone=1;

		for( INT LightIdx=0; LightIdx<VS_Lights_MAX; LightIdx++ )
		{
			EVertexShaderLightType LightType = (EVertexShaderLightType)VS_GETLIGHTTYPE(LightIdx,VertexShaderEnum);
			if( LightType > VS_Light_Type_None )
				bHasNone=0;

			switch( LightType )
			{
			case VS_Light_Type_Point:
				bHasPoint=1;
				break;
			case VS_Light_Type_Spot:
				bHasSpot=1;
				break;
			case VS_Light_Type_Directional:
				bHasDirectional=1;
				break;
			}
		}

		if( bHasPoint || bHasSpot ) 
		{
			continue;
		}
		else if( VertexShaderEnum & VS_Lit )
		{
			/*
			for( INT LightIdx=0; LightIdx<VS_Lights_Directional_MAX; LightIdx++ )
			{
				VS_SETLIGHTTYPE( LightIdx, VS_Light_Type_Directional, VertexShaderEnum );
			}
			for( INT LightIdx=VS_Lights_Directional_MAX; LightIdx<VS_Lights_MAX; LightIdx++ )
			{
				VS_SETLIGHTTYPE( LightIdx, VS_Light_Type_None, VertexShaderEnum );
			}
			*/
			for( INT LightIdx=0; LightIdx<VS_Lights_MAX; LightIdx++ )
			{
				VS_SETLIGHTTYPE( LightIdx, VS_Light_Type_None, VertexShaderEnum );
			}

			FString NewVertexShaderEnumStr( appI64toa((SQWORD)VertexShaderEnum) );
			if( NewEnums.FindItemIndex(NewVertexShaderEnumStr) == INDEX_NONE )
			{
				new(NewEnums) FString(NewVertexShaderEnumStr);
			}
		}
		else
		{
			if( NewEnums.FindItemIndex(OriginalEnumStr) == INDEX_NONE )
			{
				new(NewEnums) FString(OriginalEnumStr);
			}
		}		
	}

	debugf( TEXT("Removed %i invalid entries"), VertexShaderEnums.Num()-NewEnums.Num() );

	VertexShaderEnums.Empty();

	for( INT i=0; i < NewEnums.Num(); i++ )
	{
		new(VertexShaderEnums) FString(NewEnums(i));		
	}

	SaveConfig();

}


/* scion ======================================================================
* UVertexShaderConfigs::PrecacheVertexShaderEnums
* Author: sz
* 
* generate the vertex shader microcode for each entry in VertexShaderEnums
* ============================================================================
*/
void UVertexShaderConfigs::PrecacheVertexShaderEnums( URenderDevice* RenDev )
{
	guard(UVertexShaderConfigs::PrecacheVertexShaderEnums);
	check(RenDev);
	LOCAL_CLOCK(Time);

	for( INT i=0; i < VertexShaderEnums.Num(); i++ )
	{
		// creates a new entry in RI->RenDev->CombinedVertexShaders for this vertex 
		// shader combination if one does not already exist
		QWORD VertexShaderEnum = (QWORD) appAtoi64( *VertexShaderEnums(i) );
		RenDev->LoadVertexShaderEnum( VertexShaderEnum );
	}
	debugf(TEXT("Vertex shader precaching took %f seconds"),
		GSecondsPerCycle * LOCAL_UNCLOCK(Time));
	unguard;
}

/* scion ======================================================================
* UVertexShaderConfigs::AddVertexShaderEnum
* Author: sz
* 
* adds a new vertex shader enum to VertexShaderEnums and saves to ini
* returns false if it already exists
* ============================================================================
*/	
UBOOL UVertexShaderConfigs::AddVertexShaderEnum( QWORD NewVertexShaderEnum )
{
	guard(UVertexShaderConfigs::AddVertexShaderEnum);

	FString NewVertexShaderEnumStr( appI64toa((SQWORD)NewVertexShaderEnum) );

	UBOOL Found = 0;
	for( INT i=0; i < VertexShaderEnums.Num(); i++ )
	{
		// check to see if already exists
		if( VertexShaderEnums(i) == NewVertexShaderEnumStr )
		{
			Found = 1;
			break;
		}
	}

	if( !Found )
	{
		new(VertexShaderEnums) FString(NewVertexShaderEnumStr);
		SaveConfig();
	}

	return !Found;

	unguard;
}

IMPLEMENT_CLASS(UVertexShaderCache);
/* ============================================================================
 * UVertexShaderCache::AddToCache
 * Author: jg
 * 
 * Takes a compiled vertex shader and stores it in the vertex shaders array for
 * serialization.
 *
 * inputs:	VSEnum -- The vertex shader enum for this shader
 *			VSByteCode -- The compiled vertex shader byte code
 *			SizeInBytes -- The size of the compiled byte code
 *
 * ============================================================================
 */
void UVertexShaderCache::AddToCache(QWORD VSEnum,void* VSByteCode,
	DWORD SizeInBytes)
{
	guard(UVertexShaderCache::AddToCache);
	// Split the enum into two INTs
	INT High = ((VSEnum >> 32) & 0xFFFFFFFF);
	INT Low = (VSEnum & 0xFFFFFFFF);
	// Default to not found
	UBOOL bFound = 0;
	// Verify that this config is not already in the cache
	for (INT Index = 0; Index < VertexShaders.Num() && bFound == 0; Index++)
	{
		FVertexShader& VShader = VertexShaders(Index);
		bFound = VShader.VSEnumHigh == High && VShader.VSEnumLow == Low;
#ifdef _DEBUG
		if (bFound)
		{
			DWORD High = VShader.VSEnumHigh;
			DWORD Low = VShader.VSEnumLow;
			QWORD Debug = (((QWORD)High << 32) | (QWORD)Low);
			check(Debug == VSEnum);
		}
#endif
	}
	// Only add if it isn't already in the cache
	if (bFound == 0)
	{
		// Add a new struct to the array
		INT Index = VertexShaders.AddZeroed();
		FVertexShader& VShader = VertexShaders(Index);
		// Set the vertex shader enum in the two fields
		VShader.VSEnumHigh = High;
		VShader.VSEnumLow = Low;
		// Now add the number of bytes needed to hold the vertex shader
		VShader.VSByteCode.AddZeroed(SizeInBytes);
		// Copy the data
		appMemcpy(&VShader.VSByteCode(0),VSByteCode,SizeInBytes);
	}
	unguard;
}
