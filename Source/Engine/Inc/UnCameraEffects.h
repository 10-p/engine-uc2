/*=============================================================================
	UnCameraEffects.h: Camera effect definitions.
	Copyright 2002 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Andrew Scheidecker
=============================================================================*/
#if SUPPORTS_PRAGMA_PACK
#pragma pack (push,4)	// script mirrors are laid out with /Zp4 (as the generated headers); 64-bit pointers sit on 4
#endif

//
//	UMotionBlur
//

class ENGINE_API UMotionBlur : public UCameraEffect
{
	DECLARE_CLASS(UMotionBlur,UCameraEffect,0,Engine);

public:

    BYTE				BlurAlpha GCC_PACK(4);	// first member: keep it out of the parent's tail padding under GCC
	FAuxRenderTarget*	RenderTargets[2];
	FLOAT				LastFrameTime;

	// UObject interface.

	void Destroy();

	// UCameraEffect interface.

	virtual void PreRender(UViewport* Viewport,FRenderInterface* RI);
	virtual void PostRender(UViewport* Viewport,FRenderInterface* RI);
};

//
//	USubActionCameraEffect
//

class ENGINE_API USubActionCameraEffect : public UMatSubAction
{
	DECLARE_CLASS(USubActionCameraEffect,UMatSubAction,0,Engine);

public:

	UCameraEffect*	CameraEffect;
	FLOAT			StartAlpha,
					EndAlpha;
	BITFIELD		DisableAfterDuration : 1 GCC_PACK(4);
	BITFIELD		AllowDuplicate : 1;

	// UMatSubAction interface.

	virtual UBOOL Update(FLOAT Percent,class ASceneManager* SceneManager);
	virtual FString GetStatString();
};
#if SUPPORTS_PRAGMA_PACK
#pragma pack (pop)
#endif
