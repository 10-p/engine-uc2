/*=============================================================================
	AEmitter.h.
	Copyright 2001-2002 Epic Games, Inc. All Rights Reserved.
=============================================================================*/

	// UObject/ AActor Functions.
	virtual UBOOL Tick( FLOAT DeltaTime, enum ELevelTick TickType );
	virtual void Spawned();
	virtual void PostScriptDestroyed();
	
	// AEmitter Functions.
	virtual void  Initialize();
	virtual INT   CheckForProjectors();
	virtual void  Kill();
	virtual void  Reset();
	virtual void  Die();

	void Render(FDynamicActor* Actor,class FLevelSceneNode* SceneNode,TList<class FDynamicLight*>* Lights,FRenderInterface* RI);
	void RenderEditorInfo(FLevelSceneNode* SceneNode,FRenderInterface* RI, FDynamicActor* FDA);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

