//=============================================================================
// AxWeatherEffectHelper - 
// Helper routines for xWeatherEffect/AxWeatherEffect
//=============================================================================

#ifndef _AX_WEATHER_EFFECT_HELPER_H
#define _AX_WEATHER_EFFECT_HELPER_H

#include "EnginePrivate.h"
#include "UnRender.h"

// -- particle system helpers

struct ForceData
{
	FVector forceOrigin;
	float forceRadiusSqr;
	float invForceRadius;
	FVector velocityNormal;
	float forceScale;
	float forceVelocity;
	int forceType;
};

bool CalcForceData( ForceData& forceData, AActor* forceActor );
void CalcForceAtPoint( ForceData* forces, int numForces, FVector& pt, FVector& accumForce );

// -- vertex stream helpers

#define PCL_NOT_VIS_MASK 0x80
#define PCL_NO_DRAW      0x40
#define C_RGBA(r, g, b, a)	(((a) << 24) | ((r) << 16) | ((g) << 8) | (b))

struct DynamicVertex
{
	FVector	Point;
	DWORD	Color;
	FLOAT	U,V;
};

int RenderSpritePcl( DynamicVertex*& pVerts, FWeatherPcl& pcl, FMatrix& mat );
int RenderLinePcl( DynamicVertex*& pVerts, FWeatherPcl& pcl, FMatrix& mat );
void SetTempValues( AxWeatherEffect* pInFx );

#endif // _AX_WEATHER_EFFECT_HELPER_H