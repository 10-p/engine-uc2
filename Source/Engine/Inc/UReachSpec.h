/*=============================================================================
	UReachSpec.h: AI reach specs.
	Copyright 1997-2002 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Steven Polge 3/97
=============================================================================*/

/*
supports() -
 returns true if it supports the requirements of aPawn.  Distance is not considered.
*/
inline const UBOOL supports (INT iRadius, INT iHeight, INT moveFlags, INT iMaxFallVelocity) const;
const UBOOL IsForced() const;

UReachSpec* operator+ (const UReachSpec &Spec) const;
int defineFor (ANavigationPoint *begin, ANavigationPoint * dest, APawn * Scout);
int operator<= (const UReachSpec &Spec);
int operator== (const UReachSpec &Spec);
FPlane PathColor();
int BotOnlyPath();
void Init();
int findBestReachable(AScout *Scout);
UBOOL PlaceScout(AScout *Scout); 

