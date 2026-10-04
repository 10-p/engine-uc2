/*=============================================================================
	UMeshEmitter.h.
	Copyright 2001 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

	virtual void Initialize( INT InMaxParticles );
	virtual void CleanUp();
	virtual void PostEditChange();

	virtual INT  UpdateParticles( FLOAT DeltaTime );
#ifdef _XBOX
	//scion jg -- We need a slightly different function for writing to the push buffer
	virtual INT  FillPushBuffer( class FSpriteParticleVertex** Vertices,
		class FLevelSceneNode* SceneNode, DWORD dwNumParticles );
#endif
	virtual INT  FillVertexBuffer( class FSpriteParticleVertex* Vertices, class FLevelSceneNode* SceneNode );
	virtual INT  RenderParticles( FDynamicActor* DynActor, class FLevelSceneNode* SceneNode, TList<FDynamicLight*>* Lights, FRenderInterface* RI );

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

