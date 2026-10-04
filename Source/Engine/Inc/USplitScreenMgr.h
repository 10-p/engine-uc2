/* scion ======================================================================
* Class: USplitScreenMgr
* Author: sz
* Implementation
* =============================================================================
*/

// class USplitScreenMgr {

public:	
	APlayerController* CreateSplitPlayer( UViewport* Viewport, INT GamepadID );
	
	UViewport* CreateSplitViewport();	
	
	UBOOL RemoveSplitViewport( UViewport* Viewport );

	void MatchSplitViewportsToActors();

	void ResetState();
	
	void NotifyLevelChange();

	void NotifyReceivedClientPlayer();
	
	UBOOL IsSplitScreenViewport( UViewport* Viewport );

	UBOOL IsOverlayViewport( UViewport* Viewport );
	
	void ClearBackground();

	void UpdateSplitViewports();
	
	void UpdateSizes();
	
	void UpdateDetailLevels();

	void UpdateDisplayFlags();
	
	FORCEINLINE INT NumActiveSplitViews()
	{
		return ActiveSplitViews.Num();
	}

	FORCEINLINE UViewport* GetSplitView( INT Idx )
	{
		if( Idx < ActiveSplitViews.Num() )
		{
            return ActiveSplitViews(Idx);
		}
		return NULL;
	}

	void Init( UClient* InClient )
	{
		check(InClient);
		Client = InClient;
	}

private:
	UBOOL AddSplitEntry( UViewport* Viewport );
	
	UBOOL RemoveSplitEntry( UViewport* Viewport );
	
	UBOOL IsMenuGameActive();

	UViewport* SetupFirstView();
	
	UViewport* SetupOverlayViewport();
	
	UBOOL RemoveOverlayViewport();
	
	void InitSplitViewport( UViewport* NewViewport );
	
	void SetViewportDetail( UViewport* Viewport, FSplitScreenDetailLevel& Detail );
	
	void SetClientDetail( UClient* Client, FSplitScreenDetailLevel& Detail );
	
	void SetLevelDetail( ULevel* Level, FSplitScreenDetailLevel& Detail );

// } class USplitScreenMgr

	
	