//=============================================================================
//
// ParticleEditorComponent.cpp	
// Copyright(c) 2002 Demiurge Studios. All Rights Reserved.
// 
//=============================================================================

#include "UnrealEd.h"


/**
 * Utility function to make an enum edit that will have each emitter in it
 * Used for the "Blah from Other Emitter" properties
 */
WEnumEdit* ConstructEmitterPicker(
	WParticleEditorComponent* Parent,
	FString ToolName,
	INT UDNHelpTopic,
	AEmitter* Emitter)
{
	// This control is a list of all the other emitters in the system
	TArray<INT> ids;
	TArray<FString> names;
	ids.AddItem(-1);
	new(names)FString(TEXT("None"));

	// iterate over the other emitters and make entries for them
	for(INT i = 0; i < Emitter->Emitters.Num(); i++)
	{
		//scion jg -- Yet another crash fix
		if (Emitter->Emitters(i) != NULL)
		{
			ids.AddItem(i);
			new(names)FString(Emitter->Emitters(i)->PEName);
		}
		else
		{
			Emitter->Emitters.Remove(i);
			i--;
		}
	}

	UBOOL EmitterPicker = TRUE;
	return NEW WEnumEdit(Parent, ToolName, names, ids, PTOOL_None, UDNHelpTopic, EmitterPicker);
}


//=============================================================================
// WParticleEditorComponent 

WParticleEditorComponent::WParticleEditorComponent(WWindow* InOwnerWindow, WParticleEditorTab *InParentTab, class UParticleEmitter* InEditTarget, FString Name):
	WPropertyPage(InOwnerWindow), 
	ParentTab(InParentTab),
	EditTarget(InEditTarget),
	ComponentName(Name),
	Container(NULL),
	InitialVertOffset(20),
	SpaceBetweenTools(10),
	SecondColumnStart(180),
	ToolIndentSize(20),
	ExtraIndentSize(15),
	ExtraSpaceBetweenTools(15),
	IndentSize(5)
{
}



void WParticleEditorComponent::OnDestroy()
{
	delete Box;
	Box = NULL;

	delete Container;
	Container = NULL;

	for(INT i = 0; i < Tools.Num(); i++)
	{
		delete Tools(i).Tool;	Tools(i).Tool =	NULL;
		delete Tools(i).Label;	Tools(i).Label = NULL;
	}
	Tools.Empty();
}

void WParticleEditorComponent::LinkToolsToEmitter(UParticleEmitter* InEditTarget)
{
	EditTarget = InEditTarget;

	for(INT i = 0; i < Tools.Num(); i++)
	{
		if(!Tools(i).Tool->IsEnabled())
			Tools(i).Tool->Enable(TRUE);
	}
}



TArray<INT> WParticleEditorComponent::GetExpandState()
{
	TArray<INT> expandArray;
	expandArray.AddZeroed( Tools.Num() );

	for(INT i = 0; i < Tools.Num(); i++)
	{
		INT tmp = (INT)(Tools(i).Shown);
		expandArray(i) = tmp;
	}

	return expandArray;
}



TArray<DWORD> WParticleEditorComponent::GetToolState()
{
	TArray<DWORD> toolStateArray;
	toolStateArray.AddZeroed( Tools.Num() );

	for(INT i = 0; i < Tools.Num(); i++)
	{
		toolStateArray(i) = Tools(i).Tool->GetToolState();
	}

	return toolStateArray;
}



void WParticleEditorComponent::SetExpandState( TArray<INT> ExpandState )
{
	//Make sure same size
	if(ExpandState.Num() != Tools.Num())
		check(0);

	for(INT i = 0; i < Tools.Num(); i++)
	{
		UBOOL tmp = (UBOOL) ExpandState(i);
		Tools(i).Shown = tmp; 
		Tools(i).Tool->Show( tmp );
		if(Tools(i).Label)
			Tools(i).Label->SetExpand( tmp );
	}
}



void WParticleEditorComponent::SetToolState( TArray<DWORD> ToolState )
{
	//Make sure same size
	if(ToolState.Num() != Tools.Num())
		check(0);

	for(INT i = 0; i < Tools.Num(); i++)
	{
		Tools(i).Tool->SetToolState( ToolState(i) );
	}
}



void WParticleEditorComponent::SetParentTab(WParticleEditorTab* InParentTab)
{
	if(InParentTab)
	{
		SetParent(hWnd, InParentTab->hWnd);
		Show(TRUE);
	}
	else
	{
		SetParent(hWnd, GUnrealEd->hWndMain);
		Show(FALSE);
	}

	ParentTab = InParentTab;
	EditTarget = ParentTab ? ParentTab->GetEditTarget() : NULL;
	OwnerWindow = InParentTab;
}



const FString WParticleEditorComponent::GetComponentName()
{
	return ComponentName;
}


FPoint WParticleEditorComponent::GetSize() const
{
	FPoint Ret;
	Ret.X = 435;  // Right now the design desision to have all Components be the same width
	Ret.Y = const_cast<WParticleEditorComponent*>(this)->_SetToolAnchors(TRUE);
	return Ret;
}



void WParticleEditorComponent::OnCreate()
{
	WPropertyPage::OnCreate();

	CreateTools();
	SetToolAnchors();

	//this is opened last so it will be drawn on the bottom.
	//counterintuitively, things open first are drawn on top.
	Box = NEW WGroupBox(this);
	Box->OpenWindow(1); //(1, ?)
	Box->SetText(*ComponentName);
	Box->SetFont( (HFONT)GetStockObject(PS_BOLD_FONT) );

	Anchors.Set((DWORD)Box->hWnd, FWindowAnchor(hWnd, Box->hWnd, ANCHOR_TL, 0, 0, ANCHOR_BOTTOM | ANCHOR_WIDTH, GetSize().X, 0) );

	//finalize the placement of components given the anchors
	Container = NEW FContainer();
	Container->SetAnchors( &Anchors );
	PositionChildControls();
}


void WParticleEditorComponent::CreateTools()
{
}



void WParticleEditorComponent::AddTool(WParticleEditorTool* NewTool, ToolLabelType LabelType, UBOOL AddDown, INT xOffset, INT yOffset)
{
	//creates and opens the new Label for this Tool
	WParticleToolLabel *newLabel;
	if(LabelType == LABEL_Expand)
	{
		newLabel = NEW WParticleExpandingLabel(this, *(NewTool->GetToolName()), this, Tools.Num(), TRUE);
	}
	else if(LabelType == LABEL_ExpandClosed)
	{
		newLabel = NEW WParticleExpandingLabel(this, *(NewTool->GetToolName()), this, Tools.Num(), FALSE);
	}
	else if(LabelType == LABEL_Normal)
	{
		newLabel = NEW WParticleStaticLabel(this, *NewTool->GetToolName(), this, Tools.Num());
	}
	else 
	{
		newLabel = NULL;
	}
	
	if(newLabel != NULL)
	{
		newLabel->UDNHelpTopic = NewTool->UDNHelpTopic;
		newLabel->OpenWindow();
	}

	// adds it to the list of Tools with the right attributes
	ToolInfo ti;
	ti.Shown = LabelType != LABEL_ExpandClosed;
	ti.Tool = NewTool;
	ti.Label = newLabel;
	ti.LabelType = LabelType;
	ti.AddDown = AddDown;
	ti.xOffset = xOffset;
	ti.yOffset = yOffset;
	Tools.AddItem(ti);

	//opens the Tool 
	NewTool->OpenWindow(GetModuleHandleA("unrealed.exe"));

	//sets the showness of things
	NewTool->Show(LabelType != LABEL_ExpandClosed);
}



INT WParticleEditorComponent::_SetToolAnchors(UBOOL OnlyCalcSize)
{
	INT Top = InitialVertOffset;
	INT lastTop = Top;
	INT maxTop = Top;
	for(INT i = 0; i < Tools.Num(); i++)
	{
		if(Tools(i).AddDown)
			Top = maxTop;
		else
			Top = lastTop;

		lastTop = Top;

		if(Top + Tools(i).yOffset >= InitialVertOffset)
			Top += Tools(i).yOffset;

		if(Tools(i).LabelType != LABEL_None)
		{
			if(!OnlyCalcSize)
				Anchors.Set((DWORD)Tools(i).Label->hWnd, FWindowAnchor( hWnd, Tools(i).Label->hWnd, ANCHOR_TL, IndentSize + Tools(i).xOffset, Top, ANCHOR_WIDTH | ANCHOR_HEIGHT, Tools(i).Label->GetSize().X, Tools(i).Label->GetSize().Y ) );
			Top+= Tools(i).Label->GetSize().Y;
		}

		if(Tools(i).Shown)
		{
			if(!OnlyCalcSize)
			{
				INT extraIndent = (Tools(i).Tool->ToolFlags & PTOOL_ExtraIndent) ? ExtraIndentSize : 0;
				Anchors.Set((DWORD)Tools(i).Tool->hWnd, FWindowAnchor( hWnd, Tools(i).Tool->hWnd, ANCHOR_TL, ToolIndentSize + IndentSize + extraIndent + Tools(i).xOffset, Top, ANCHOR_WIDTH | ANCHOR_HEIGHT, Tools(i).Tool->GetSize().X, Tools(i).Tool->GetSize().Y) );
			}
			Top+= Tools(i).Tool->GetSize().Y + SpaceBetweenTools;
		}

		if(Top > maxTop)
			maxTop = Top;
	}

	if(Container && !OnlyCalcSize)
		PositionChildControls();

	return Top;
}



void WParticleEditorComponent::PositionChildControls()
{
	if( Container ) 
		Container->RefreshControls();
}



void WParticleEditorComponent::OnSize( DWORD Flags, INT NewX, INT NewY )
{
	WPropertyPage::OnSize(Flags, NewX, NewY);
	PositionChildControls();
}


void WParticleEditorComponent::ShowTool(INT ToolIndex, UBOOL Shown)
{
	if(ToolIndex < 0 || ToolIndex >= Tools.Num())
	{
		GLogWindow->Log( TEXT("Error.  WParticleEditorComponent::ShowTool - ToolIndex out of bounds") );
		check(0);
		return;
	}

	Tools(ToolIndex).Shown = Shown; 
	Tools(ToolIndex).Tool->Show(Shown);
	SetToolAnchors();
	if(ParentTab != NULL)
		ParentTab->SetComponentAnchors();
	GetParentTab()->Refresh();
}



void WParticleEditorComponent::ShowTool(WParticleEditorTool *ToolToShow, UBOOL Shown)
{
	INT ToolIndex = -1;
	
	for(INT i = 0; i < Tools.Num(); i++)
	{
		if(Tools(i).Tool == ToolToShow)
		{
			ToolIndex = i;
			break;
		}
	}

	ShowTool(ToolIndex, Shown);
}



void WParticleEditorComponent::RereadToolValues(UBOOL ForceUpdateAll)
{
	for(INT i = 0; i < Tools.Num(); i++)
	{
        DWORD flags = Tools(i).Tool->ToolFlags;
		if((flags & PTOOL_ListenForUpdate) == PTOOL_ListenForUpdate || ForceUpdateAll)
			Tools(i).Tool->GetValuesFromTarget();
	}
}




//=============================================================================
// WParticleGeneralComponent 

WParticleGeneralComponent::WParticleGeneralComponent(WWindow* InOwnerWindow, WParticleEditorTab *InParentTab, class UParticleEmitter* InEditTarget, FString Name):
	WParticleEditorComponent(InOwnerWindow, InParentTab, InEditTarget, Name)
{
}
		
void WParticleGeneralComponent::CreateTools()
{
	// AutoDestroy is not good to have in the editor
	// in the export-to-script dialogue instead

	// We've eliminated ParticlesPerSecond and renamed InitialParticlesPerSecond because the behaviour this is used for can be better achieved
	// with multiple emitters and it was VERY confusing to the users.

	WParticleEditorComponent::CreateTools();

	Disable	= NEW WBoolEdit(this, FString(TEXT("Disable")), TEXT("Disabled"), PTOOL_None, 2134 );
	MaxParticles = NEW WNoNegNumberEdit(this, FString(TEXT("Max Number of Particles")), PTOOL_RefreshOnChange, 2128 );
	Name = NEW WNameTool(this, FString(TEXT("Name")), PTOOL_None, 2137 );
	RespawnDeadParticles = NEW WBoolEdit(this, FString(TEXT("Respawn Dead Particles")), TEXT("RespawnDeadParticles"), PTOOL_RefreshOnChange, 2131 );

	ParticlesPerSecond = NEW WFloatEdit(this, FString(TEXT("Particles Per Second")), PTOOL_RefreshOnChange, 2000 );
	ParticlesPerSecond->SetMin(0);

	AutomaticSpawning = NEW WEnableEdit(this, FString(TEXT("Automatic Spawning")), TEXT("AutomaticInitialSpawning"), TRUE, PTOOL_RefreshOnChange, 2000);
	AutomaticSpawning->AddTool(ParticlesPerSecond);

	ScaleEmitter = NEW WScaleEmitterEdit(this, PTOOL_None );
	ScaleSpeed = NEW WSpeedScaleEdit(this, PTOOL_None );

	//scion jg -- Allow access to the bIsInstanced variable
	bIsInstanced = NEW WBoolEdit(this, FString(TEXT("Shared Sub-emitter Instance")), TEXT("bIsInstanced"), PTOOL_None, 2000 );
	AddTool(bIsInstanced		, LABEL_None, TRUE, 0, ExtraSpaceBetweenTools );

	AddTool(Disable				, LABEL_None);
	AddTool(MaxParticles		, LABEL_Normal);
	AddTool(Name				, LABEL_Normal, FALSE, SecondColumnStart, 0);
	AddTool(RespawnDeadParticles, LABEL_None);
	AddTool(AutomaticSpawning	, LABEL_None);
	AddTool(ParticlesPerSecond	, LABEL_Normal);
	AddTool(ScaleEmitter		, LABEL_Normal, TRUE, 0, ExtraSpaceBetweenTools);
	AddTool(ScaleSpeed			, LABEL_Normal);

	// scion sz - detail level for subemitter
	TArray<INT> DetailLevelIDs;
	TArray<FString> DetailLevelNames;
	DetailLevelIDs.AddItem(0);
	new(DetailLevelNames)FString(TEXT("DM_Low"));
	DetailLevelIDs.AddItem(1);
	new(DetailLevelNames)FString(TEXT("DM_High"));
	DetailLevelIDs.AddItem(2);
	new(DetailLevelNames)FString(TEXT("DM_SuperHigh"));
	DetailLevel = NEW WEnumEdit(this, FString(TEXT("Detail Level")), DetailLevelNames, DetailLevelIDs, PTOOL_None, 2000);
	AddTool(DetailLevel			, LABEL_Normal );
}	


void WParticleGeneralComponent::LinkToolsToEmitter(class UParticleEmitter* EditTarget)
{
	WParticleEditorComponent::LinkToolsToEmitter(EditTarget);

	Disable->SetTarget(); 
	MaxParticles->SetTarget( &(EditTarget->MaxParticles) );
	Name->SetTarget( &(EditTarget->PEName) );
	RespawnDeadParticles->SetTarget();
	ParticlesPerSecond->SetTarget( &(EditTarget->InitialParticlesPerSecond) );
	AutomaticSpawning->SetTarget();
	ScaleEmitter->SetTarget();
	ScaleSpeed->SetTarget();
	//scion jg -- Allow access to the bIsInstanced variable
	bIsInstanced->SetTarget();
	//scion sz - link detail level
	DetailLevel->SetTarget( (INT*)&(EditTarget->DetailLevel) );
}

//=============================================================================
// WParticleFadingColorComponent 

WParticleFadingColorComponent::WParticleFadingColorComponent(WWindow* InOwnerWindow, WParticleEditorTab *InParentTab, class UParticleEmitter* InEditTarget, FString Name):
	WParticleEditorComponent(InOwnerWindow, InParentTab, InEditTarget, Name)
{
}
	
void WParticleFadingColorComponent::CreateTools()
{
	WParticleEditorComponent::CreateTools();
	

	Opacity = NEW WPercentEdit(this, FString(TEXT("Opacity")), PTOOL_None, 2000);
	Fading = NEW WFadingEdit(this, PTOOL_ListenForUpdate, 2000);
	FadeOutFactor = NEW WColorMultEdit(this, FString(TEXT("Fade Out Factor")), PTOOL_None, 2120 );
	FadeInFactor = NEW WColorMultEdit(this, FString(TEXT("Fade In Factor")), PTOOL_None, 2123 );

	ColorMultiplier = NEW WRangeColorEdit(this, FString(TEXT("Color Multiplier")), PTOOL_ListenForUpdate, 2119 );
	ColorMultiplier->SetAbsMin(0.0);
	ColorMultiplier->SetAbsMax(1.0);

	UseColorScale =  NEW WEnableEdit(this, FString(TEXT("Use Color Scale")), TEXT("UseColorScale"), FALSE, PTOOL_None, 2116 );
	ColorScale = NEW WColorScaleEdit(this, PTOOL_UpdateTools, 2000 );
	ColorScaleRepeats = NEW WFloatEdit(this, FString(TEXT("Color Scale Repeats")), PTOOL_None, 2118);	
	ColorScaleRepeats->SetMin(0.0);
	UseColorScale->AddTool(ColorScale);
	UseColorScale->AddTool(ColorScaleRepeats);


	AddTool(Opacity,			LABEL_Normal, TRUE, 0, ExtraSpaceBetweenTools );
	AddTool(Fading,				LABEL_Normal, TRUE, 0, ExtraSpaceBetweenTools );
	AddTool(FadeOutFactor,		LABEL_ExpandClosed);
	AddTool(FadeInFactor,		LABEL_ExpandClosed);
	AddTool(ColorMultiplier,	LABEL_Expand, TRUE, 0, ExtraSpaceBetweenTools);
	AddTool(UseColorScale,		LABEL_None, TRUE, 0, ExtraSpaceBetweenTools);
	AddTool(ColorScale,			LABEL_Expand );
	AddTool(ColorScaleRepeats,	LABEL_Normal );
}


void WParticleFadingColorComponent::LinkToolsToEmitter(class UParticleEmitter* EditTarget)
{
	WParticleEditorComponent::LinkToolsToEmitter(EditTarget);

	Opacity->SetTarget( &(EditTarget->Opacity) );
	Fading->SetTarget();
	FadeOutFactor->SetTarget( &(EditTarget->FadeOutFactor) );
	FadeInFactor->SetTarget( &(EditTarget->FadeInFactor) );
	ColorMultiplier->SetTarget( &(EditTarget->ColorMultiplierRange) );
	UseColorScale->SetTarget();
	ColorScale->SetTarget();
	ColorScaleRepeats->SetTarget( &(EditTarget->ColorScaleRepeats) );
}





//=============================================================================
// WParticleRenderingComponent 

WParticleRenderingComponent::WParticleRenderingComponent(WWindow* InOwnerWindow, WParticleEditorTab *InParentTab, class UParticleEmitter* InEditTarget, FString Name):
	WParticleEditorComponent(InOwnerWindow, InParentTab, InEditTarget, Name)
{
}
		
void WParticleRenderingComponent::CreateTools()
{
	WParticleEditorComponent::CreateTools();
	
	DisableFogging = NEW WBoolEdit(this, FString(TEXT("Disable Fogging")), TEXT("DisableFogging"), PTOOL_None, 2135 );
	AlphaTest = NEW WBoolEdit(this, FString(TEXT("Alpha Test")), TEXT("AlphaTest"), PTOOL_None, 2146 );
	AlphaRef = NEW WIntEdit(this, FString(TEXT("Alpha Ref")), PTOOL_RefreshOnChange, 2145 );
	Z_Test = NEW WBoolEdit(this, FString(TEXT("Z-Test")), TEXT("ZTest"), PTOOL_None, 2148 );
	Z_Write = NEW WBoolEdit(this, FString(TEXT("Z-Write")), TEXT("ZWrite"), PTOOL_None, 2149 );

	AddTool(DisableFogging, LABEL_None, TRUE, 0, ExtraSpaceBetweenTools );
	AddTool(AlphaTest,		LABEL_None);
	AddTool(AlphaRef,		LABEL_Normal, FALSE, SecondColumnStart, 0);
	AddTool(Z_Test,			LABEL_None);
	AddTool(Z_Write,		LABEL_None, FALSE, SecondColumnStart, 0);
}

void WParticleRenderingComponent::LinkToolsToEmitter(class UParticleEmitter* EditTarget)
{
	WParticleEditorComponent::LinkToolsToEmitter(EditTarget);

	DisableFogging->SetTarget();
	AlphaTest->SetTarget();
	AlphaRef->SetTarget( &((EditTarget->AlphaRef)) );		
	Z_Test->SetTarget();
	Z_Write->SetTarget();
}


//=============================================================================
// WParticleMovementComponent 

WParticleMovementComponent::WParticleMovementComponent(WWindow* InOwnerWindow, WParticleEditorTab *InParentTab, class UParticleEmitter* InEditTarget, FString Name):
	WParticleEditorComponent(InOwnerWindow, InParentTab, InEditTarget, Name)
{
}
		
void WParticleMovementComponent::CreateTools()
{
	WParticleEditorComponent::CreateTools();

	StartVelocity = NEW WRangeVectorEdit(this, FString(TEXT("Start Velocity")), PTOOL_None, 2183 );	
	Acceleration = NEW WXYZVectorEdit(this, FString(TEXT("Acceleration")), PTOOL_None, 2100);

	TArray<INT> coordIDs;
	TArray<FString> coordNames;
	coordIDs.AddItem(0);
	new(coordNames)FString(TEXT("Independent"));
	coordIDs.AddItem(1);
	new(coordNames)FString(TEXT("Relative"));
	coordIDs.AddItem(2);
	new(coordNames)FString(TEXT("Absolute"));
	CoordinateSystem = NEW WEnumEdit(this, FString(TEXT("Coordinate System")), coordNames, coordIDs, PTOOL_None, 2127);

	VelocityLoss = NEW WRangeVectorEdit(this, FString(TEXT("Velocity Loss")), PTOOL_None, 2186);
	VelocityLoss->SetAbsMin(0.0);
	VelocityLoss->SetAbsMax(100.0);

	MaxVelocity	= NEW WXYZVectorEdit(this, FString(TEXT("Max Velocity (Absolute)")), PTOOL_None , 2185);
	MaxVelocity->SetMin(0.0f);

	MinSquaredVelocity = NEW WFloatEdit(this, FString(TEXT("Min Squared Velocity")), PTOOL_None, 2179 );

	AddVelocityFrom = ConstructEmitterPicker(this, FString(TEXT("Add Velocity From Other Emitter")), 2187, ParentTab->GetEmitter());
	AddVelocityMult = NEW WRangeVectorEdit(this, FString(TEXT("Add Velocity Multiplier")), PTOOL_None, 2188 );
	// Disable on None
	AddVelocityFrom->AddDisableTool(AddVelocityMult, 0);

	TArray<INT> velDirIDs;
	TArray<FString> velDirNames;
	velDirIDs.AddItem(0);
	new(velDirNames)FString(TEXT("None"));
	velDirIDs.AddItem(1);
	new(velDirNames)FString(TEXT("Start Position and Owner"));
	velDirIDs.AddItem(2);
	new(velDirNames)FString(TEXT("Owner and Start Position"));
	velDirIDs.AddItem(3);
	new(velDirNames)FString(TEXT("Add Radial"));
	GetVelocityDir = NEW WEnumEdit(this, FString(TEXT("Get Velocity Direction From:")), velDirNames, velDirIDs, PTOOL_None, 2189);
	StartVelocityRadial = NEW WRangeEdit(this, FString(TEXT("Start Velocity Radial")), PTOOL_None, 2184 );
	GetVelocityDir->AddDisableTool(StartVelocityRadial, 0);
	GetVelocityDir->AddDisableTool(StartVelocityRadial, 1);
	GetVelocityDir->AddDisableTool(StartVelocityRadial, 2);


	
	AddTool(CoordinateSystem, LABEL_Normal, TRUE, 0, ExtraSpaceBetweenTools );
	AddTool(StartVelocity, LABEL_Expand);
	AddTool(Acceleration, LABEL_Normal);
	AddTool(VelocityLoss, LABEL_ExpandClosed, TRUE, 0, ExtraSpaceBetweenTools );
	AddTool(MaxVelocity, LABEL_Normal);
	AddTool(MinSquaredVelocity, LABEL_Normal);
	AddTool(AddVelocityFrom, LABEL_Normal, TRUE, 0, ExtraSpaceBetweenTools );
	AddTool(AddVelocityMult, LABEL_ExpandClosed);
	AddTool(GetVelocityDir, LABEL_Normal);
	AddTool(StartVelocityRadial, LABEL_Normal );
}



void WParticleMovementComponent::LinkToolsToEmitter(class UParticleEmitter* EditTarget)
{
	WParticleEditorComponent::LinkToolsToEmitter(EditTarget);

	StartVelocity->SetTarget( &(EditTarget->StartVelocityRange) );
	Acceleration->SetTarget( &(EditTarget->Acceleration) );
	CoordinateSystem->SetTarget( (INT*)&(EditTarget->CoordinateSystem) );
	VelocityLoss->SetTarget( &(EditTarget->VelocityLossRange) );
	MaxVelocity->SetTarget( &(EditTarget->MaxAbsVelocity) );
	MinSquaredVelocity->SetTarget( &(EditTarget->MinSquaredVelocity) );
	AddVelocityFrom->SetTarget( &(EditTarget->AddVelocityFromOtherEmitter) );
	AddVelocityMult->SetTarget( &(EditTarget->AddVelocityMultiplierRange) );
	GetVelocityDir->SetTarget( (INT*)&(EditTarget->GetVelocityDirectionFrom) );
	StartVelocityRadial->SetTarget( &(EditTarget->StartVelocityRadialRange)  );
}




//=============================================================================
// WParticleBasicRotationComponent 

WParticleBasicRotationComponent::WParticleBasicRotationComponent(WWindow* InOwnerWindow, WParticleEditorTab *InParentTab, class UParticleEmitter* InEditTarget, FString Name):
	WParticleEditorComponent(InOwnerWindow, InParentTab, InEditTarget, Name)
{
}
	
	
void WParticleBasicRotationComponent::CreateTools()
{
	WParticleEditorComponent::CreateTools();

	TArray<INT> rotSourceIDs;
	TArray<FString> rotSourceNames;
	rotSourceIDs.AddItem(0);
	new(rotSourceNames)FString(TEXT("None"));
	rotSourceIDs.AddItem(1);
	new(rotSourceNames)FString(TEXT("Emitter"));
	rotSourceIDs.AddItem(2);
	new(rotSourceNames)FString(TEXT("Offset"));
	rotSourceIDs.AddItem(3);
	new(rotSourceNames)FString(TEXT("Normal"));
	UseRotationFrom = NEW WEnumEdit(this, FString(TEXT("Use Rotation From: ")), rotSourceNames, rotSourceIDs, PTOOL_None, 2150);
	RotationOffset = NEW WRotatorEdit(this, FString(TEXT("Rotation Offset")), PTOOL_None, 2152 );
	RotationNormal = NEW WXYZVectorEdit(this, FString(TEXT("Rotation Normal")), PTOOL_None, 2158 );

	/* - design choice that EffectAxis adds more confusion that benefit - same result can de achieved adjusting Location settings
	TArray<INT> axisIDs;
	TArray<FString> axisNames;
	axisIDs.AddItem(0);
	new(axisNames)FString(TEXT("Negative-X"));
	axisIDs.AddItem(1);
	new(axisNames)FString(TEXT("Positive-Z"));
	WEnumEdit *effectAxis = new WEnumEdit(this, FString(TEXT("Effect Axis")), axisNames, axisIDs, (INT*)&(EditTarget->EffectAxis), PTOOL_None, 2130);
	*/

	UseRotationFrom->AddDisableTool(RotationOffset, 0);
	UseRotationFrom->AddDisableTool(RotationOffset, 1);
	UseRotationFrom->AddDisableTool(RotationOffset, 3);

	UseRotationFrom->AddDisableTool(RotationNormal, 0);
	UseRotationFrom->AddDisableTool(RotationNormal, 1);
	UseRotationFrom->AddDisableTool(RotationNormal, 2);

	/* - design choice that EffectAxis adds more confusion that benefit - same result can de achieved adjusting Location settings
	rotFrom->AddDisableTool(effectAxis, 0);
	rotFrom->AddDisableTool(effectAxis, 1);
	rotFrom->AddDisableTool(effectAxis, 2);
	*/
	
	AddTool(UseRotationFrom,	LABEL_Normal, TRUE, 0, ExtraSpaceBetweenTools);
	AddTool(RotationOffset,		LABEL_Normal);
	AddTool(RotationNormal,		LABEL_Normal);

	/* - design choice that EffectAxis adds more confusion that benefit - same result can de achieved adjusting Location settings
	AddTool(, LABEL_ExpandClosed);
	*/
}


void WParticleBasicRotationComponent::LinkToolsToEmitter(class UParticleEmitter* EditTarget)
{
	WParticleEditorComponent::LinkToolsToEmitter(EditTarget);

	UseRotationFrom->SetTarget( (INT*)&(EditTarget->UseRotationFrom) );
	RotationOffset->SetTarget( &(EditTarget->RotationOffset) );	
	RotationNormal->SetTarget( &(EditTarget->RotationNormal) );	
}




//=============================================================================
// WParticleSpriteRotationComponent 

WParticleSpriteRotationComponent::WParticleSpriteRotationComponent(WWindow* InOwnerWindow, WParticleEditorTab *InParentTab, class UParticleEmitter* InEditTarget, FString Name):
	WParticleBasicRotationComponent(InOwnerWindow, InParentTab, InEditTarget, Name)
{
}
	
	
void WParticleSpriteRotationComponent::CreateTools()
{

	StartSpin = NEW WRangeEdit(this, FString(TEXT("Start Spin")), PTOOL_None , 2155);
	((WRangeEdit*)StartSpin)->SetAbsMin(-1.0);
	((WRangeEdit*)StartSpin)->SetAbsMax(1.0);

	SpinsPerSecond = NEW WRangeEdit(this, FString(TEXT("Spins Per Second")), PTOOL_None , 2154);

	SpinCCW	= NEW WPercentEdit(this, FString(TEXT("Spin CCW or CW")), PTOOL_None , 2153);

	SpinParticles = NEW WEnableEdit(this, FString(TEXT("Spin Particles")), TEXT("SpinParticles"), FALSE, PTOOL_None, 2151 );
	SpinParticles->AddTool(SpinsPerSecond);
	SpinParticles->AddTool(StartSpin);
	SpinParticles->AddTool(SpinCCW);


	TArray<INT> axisIDs;
	TArray<FString> axisNames;
	axisIDs.AddItem(0);
	new(axisNames)FString(TEXT("Facing Camera"));
	axisIDs.AddItem(1);
	new(axisNames)FString(TEXT("Along Movement Facing Camera"));
	//axisIDs.AddItem(2);
	//new(axisNames)FString(TEXT("Right")); // - design choice that Right adds more confusion that benefit - same result can be achieved by rotating original texture 90 degrees
	axisIDs.AddItem(4);
	new(axisNames)FString(TEXT("Specified Normal"));
	axisIDs.AddItem(5);
	new(axisNames)FString(TEXT("Along Movement Facing Normal"));
	//scion jg -- Along normal and camera facing
	axisIDs.AddItem(PTDU_NormalAndUp);
	new(axisNames)FString(TEXT("Along Normal Facing Camera"));
	//scion jg -- Along normal and camera facing
	axisIDs.AddItem(PTDU_EmitterRotationAndUp);
	new(axisNames)FString(TEXT("Along Emitter Rotation Facing Camera"));
	axisIDs.AddItem(3);
	new(axisNames)FString(TEXT("Perpendicular To Movement"));
	//axisIDs.AddItem(6);
	//new(axisNames)FString(TEXT("Specified Normal + Right")); - design choice that Right adds more confusion that benefit - same result can be achieved by rotating original texture 90 degrees
	FacingDirection = NEW WEnumEdit(this, FString(TEXT("Facing Direction - (pre spin)")), axisNames, axisIDs, PTOOL_None, 2000);
	ProjectionNormal = NEW WXYZVectorEdit(this, FString(TEXT("Projection Normal")), PTOOL_RefreshOnChange, 2000 );

	FacingDirection->AddDisableTool(ProjectionNormal, 0);
	FacingDirection->AddDisableTool(ProjectionNormal, 1);
	FacingDirection->AddDisableTool(ProjectionNormal, 5);
	FacingDirection->AddDisableTool(ProjectionNormal, 6);
	
	AddTool(SpinParticles,		LABEL_None, TRUE, 0, ExtraSpaceBetweenTools );
	AddTool(StartSpin,			LABEL_Expand );
	AddTool(SpinsPerSecond,		LABEL_Expand );
	AddTool(SpinCCW,			LABEL_Expand );
	AddTool(FacingDirection,	LABEL_Normal, TRUE, 0, ExtraSpaceBetweenTools);
	AddTool(ProjectionNormal,	LABEL_Normal);

	//putting this last ensures the order of the tools
	WParticleBasicRotationComponent::CreateTools();
}

void WParticleSpriteRotationComponent::LinkToolsToEmitter(class UParticleEmitter* EditTarget)
{
	WParticleBasicRotationComponent::LinkToolsToEmitter(EditTarget);

	SpinParticles->SetTarget();
	StartSpin->SetTarget( &(EditTarget->StartSpinRange.X) );
	SpinsPerSecond->SetTarget( &(EditTarget->SpinsPerSecondRange.X) );
	SpinCCW->SetTarget( &(EditTarget->SpinCCWorCW.X) );
	FacingDirection->SetTarget( (INT*)&(((USpriteEmitter*)EditTarget)->UseDirectionAs) );
	ProjectionNormal->SetTarget( &(((USpriteEmitter*)EditTarget)->ProjectionNormal) );
}




//=============================================================================
// WParticleMeshRotationComponent 

WParticleMeshRotationComponent::WParticleMeshRotationComponent(WWindow* InOwnerWindow, WParticleEditorTab *InParentTab, class UParticleEmitter* InEditTarget, FString Name):
	WParticleBasicRotationComponent(InOwnerWindow, InParentTab, InEditTarget, Name)
{
}
	
	
void WParticleMeshRotationComponent::CreateTools()
{
	WParticleEditorComponent::CreateTools();

	StartSpin = NEW WRangeRotatorEdit(this, FString(TEXT("Start Spin")), PTOOL_None , 2155);
	((WRangeRotatorEdit*)StartSpin)->SetAbsMin(-1.0);
	((WRangeRotatorEdit*)StartSpin)->SetAbsMax(1.0);
	SpinsPerSecond = NEW WRangeRotatorEdit(this, FString(TEXT("Spins Per Second")), PTOOL_None , 2154);
	SpinCCW	= NEW WXYZVectorEdit(this, FString(TEXT("Spin CCW or CW")), PTOOL_None , 2153);
	((WXYZVectorEdit*)(SpinCCW))->SetMin(0.0);
	((WXYZVectorEdit*)(SpinCCW))->SetMax(1.0);

	SpinParticles = NEW WEnableEdit(this, FString(TEXT("Spin Particles")), TEXT("SpinParticles"), FALSE, PTOOL_None, 2151 );
	SpinParticles->AddTool(StartSpin);
	SpinParticles->AddTool(SpinsPerSecond);
	SpinParticles->AddTool(SpinCCW);
	
	AddTool(SpinParticles,		LABEL_None, TRUE, 0, ExtraSpaceBetweenTools );
	AddTool(StartSpin,			LABEL_Expand );
	AddTool(SpinsPerSecond,		LABEL_Expand );
	AddTool(SpinCCW,			LABEL_Expand );

	//putting this last ensures the order of the tools
	WParticleBasicRotationComponent::CreateTools();
}


void WParticleMeshRotationComponent::LinkToolsToEmitter(class UParticleEmitter* EditTarget)
{
	WParticleBasicRotationComponent::LinkToolsToEmitter(EditTarget);

	SpinParticles->SetTarget();
	StartSpin->SetTarget( &(EditTarget->StartSpinRange) );
	SpinsPerSecond->SetTarget( &(EditTarget->SpinsPerSecondRange) );
	SpinCCW->SetTarget( &(EditTarget->SpinCCWorCW) );
}




//=============================================================================
// WParticleTextureComponent 

WParticleTextureComponent::WParticleTextureComponent(WWindow* InOwnerWindow, WParticleEditorTab *InParentTab, class UParticleEmitter* InEditTarget, FString Name):
	WParticleEditorComponent(InOwnerWindow, InParentTab, InEditTarget, Name)
{
}
		
void WParticleTextureComponent::CreateTools()
{
	WParticleEditorComponent::CreateTools();

	TexturePick = NEW WTexturePickEdit(this, FString(TEXT("Texture")), PTOOL_None, 2000);

	TArray<INT> drawStyleIDs;
	TArray<FString> drawStyleNames;
	drawStyleIDs.AddItem(0);
	new(drawStyleNames)FString(TEXT("Regular"));
	drawStyleIDs.AddItem(1);
	new(drawStyleNames)FString(TEXT("Alpha Blend"));
	drawStyleIDs.AddItem(2);
	new(drawStyleNames)FString(TEXT("Modulated"));
	drawStyleIDs.AddItem(3);
	new(drawStyleNames)FString(TEXT("Translucent"));
	drawStyleIDs.AddItem(5);
	new(drawStyleNames)FString(TEXT("Darken"));
	drawStyleIDs.AddItem(6);
	new(drawStyleNames)FString(TEXT("Brighten"));
	drawStyleIDs.AddItem(4);
	new(drawStyleNames)FString(TEXT("Alpha Modulate (Fog Problems)"));
	//scion jg -- Source darken effect
	drawStyleIDs.AddItem(7);
	new(drawStyleNames)FString(TEXT("Source Darken"));
	//scion jg -- end
	DrawStyle = NEW WEnumEdit(this, FString(TEXT("Draw Style")), drawStyleNames, drawStyleIDs, PTOOL_None, 2168);
		
	NumberUSub = NEW WIntEdit(this, FString(TEXT("Number of U-Subdivisions")), PTOOL_None, 2170 );
	NumberVSub = NEW WIntEdit(this, FString(TEXT("Number of V-Subdivisions")), PTOOL_None, 2171 );

	BlendDetweenSub	= NEW WEnableEdit(this, FString(TEXT("Blend Between Subdivisions")), TEXT("BlendBetweenSubdivisions"), FALSE, PTOOL_None, 2172 );
	UseRandomSub= NEW WEnableEdit(this, FString(TEXT("Use Random Subdivision")), TEXT("UseRandomSubdivision"), FALSE, PTOOL_None, 2177 );

	UseSubScale = NEW WEnableEdit(this, FString(TEXT("Use Subdivision Scale")), TEXT("UseSubdivisionScale"), FALSE, PTOOL_None, 2173 );
	SubScale = NEW WParticleFloatArrayTool(this, FString(TEXT("Subdivision Scale")), PTOOL_None, 2174);
	UseSubScale->AddTool(SubScale);	

	SubStart = NEW WIntEdit(this, FString(TEXT("Subdivision Start")), PTOOL_None, 2175 );
	SubEnd = NEW WIntEdit(this, FString(TEXT("Subdivision End")), PTOOL_None, 2176 );

	AddTool(TexturePick,		LABEL_Normal, TRUE, 0, ExtraSpaceBetweenTools );
	AddTool(DrawStyle,			LABEL_Normal);
	AddTool(NumberUSub,			LABEL_Normal );
	AddTool(NumberVSub,			LABEL_Normal, FALSE, SecondColumnStart, 0);
	AddTool(BlendDetweenSub,	LABEL_None);
	AddTool(UseRandomSub,		LABEL_None, FALSE, SecondColumnStart, 0);
	AddTool(UseSubScale,		LABEL_None, TRUE, 0, ExtraSpaceBetweenTools);
	AddTool(SubScale,			LABEL_Expand);
	AddTool(SubStart,			LABEL_Normal );
	AddTool(SubEnd,				LABEL_Normal, FALSE, SecondColumnStart, 0);
}


void WParticleTextureComponent::LinkToolsToEmitter(class UParticleEmitter* EditTarget)
{
	WParticleEditorComponent::LinkToolsToEmitter(EditTarget);

	TexturePick->SetTarget( (UMaterial **) &(EditTarget->Texture) );
	DrawStyle->SetTarget( (INT*)&(EditTarget->DrawStyle) );
	NumberUSub->SetTarget( &(EditTarget->TextureUSubdivisions) );
	NumberVSub->SetTarget( &(EditTarget->TextureVSubdivisions) );
	BlendDetweenSub->SetTarget();
	UseRandomSub->SetTarget();
	UseSubScale->SetTarget();
	SubScale->SetTarget( &(EditTarget->SubdivisionScale) );
	SubStart->SetTarget( &(EditTarget->SubdivisionStart) );
	SubEnd->SetTarget( &(EditTarget->SubdivisionEnd) );
}



//=============================================================================
// WParticleMeshTextureComponent 

WParticleMeshTextureComponent::WParticleMeshTextureComponent(WWindow* InOwnerWindow, WParticleEditorTab *InParentTab, class UParticleEmitter* InEditTarget, FString Name):
	WParticleEditorComponent(InOwnerWindow, InParentTab, InEditTarget, Name)
{
}
		
void WParticleMeshTextureComponent::CreateTools()
{
	WParticleEditorComponent::CreateTools();

	CustomTextureSet = NEW WParticleTextureArrayTool(this, FString(TEXT("Custom Texture Set")), PTOOL_None, 2000);

	TArray<INT> drawStyleIDs;
	TArray<FString> drawStyleNames;
	drawStyleIDs.AddItem(0);
	new(drawStyleNames)FString(TEXT("Regular"));
	drawStyleIDs.AddItem(1);
	new(drawStyleNames)FString(TEXT("Alpha Blend"));
	drawStyleIDs.AddItem(2);
	new(drawStyleNames)FString(TEXT("Modulated"));
	drawStyleIDs.AddItem(3);
	new(drawStyleNames)FString(TEXT("Translucent"));
	drawStyleIDs.AddItem(5);
	new(drawStyleNames)FString(TEXT("Darken"));
	drawStyleIDs.AddItem(6);
	new(drawStyleNames)FString(TEXT("Brighten"));
	drawStyleIDs.AddItem(4);
	new(drawStyleNames)FString(TEXT("Alpha Modulate (Fog Problems)"));
	DrawStyle = NEW WEnumEdit(this, FString(TEXT("Draw Style")), drawStyleNames, drawStyleIDs, PTOOL_None, 2168);
		
	NumberUSub = NEW WIntEdit(this, FString(TEXT("Number of U-Subdivisions")), PTOOL_None, 2170 );
	NumberVSub = NEW WIntEdit(this, FString(TEXT("Number of V-Subdivisions")), PTOOL_None, 2171 );

	BlendDetweenSub	= NEW WEnableEdit(this, FString(TEXT("Blend Between Subdivisions")), TEXT("BlendBetweenSubdivisions"), FALSE, PTOOL_None, 2172 );
	UseRandomSub= NEW WEnableEdit(this, FString(TEXT("Use Random Subdivision")), TEXT("UseRandomSubdivision"), FALSE, PTOOL_None, 2177 );

	UseSubScale = NEW WEnableEdit(this, FString(TEXT("Use Subdivision Scale")), TEXT("UseSubdivisionScale"), FALSE, PTOOL_None, 2173 );
	SubScale = NEW WParticleFloatArrayTool(this, FString(TEXT("Subdivision Scale")), PTOOL_None, 2174);
	UseSubScale->AddTool(SubScale);	

	SubStart = NEW WIntEdit(this, FString(TEXT("Subdivision Start")), PTOOL_None, 2175 );
	SubEnd = NEW WIntEdit(this, FString(TEXT("Subdivision End")), PTOOL_None, 2176 );

	AddTool(CustomTextureSet,	LABEL_Expand, TRUE, 0, ExtraSpaceBetweenTools );
	AddTool(DrawStyle,			LABEL_Normal);
	AddTool(NumberUSub,			LABEL_Normal );
	AddTool(NumberVSub,			LABEL_Normal, FALSE, SecondColumnStart, 0);
	AddTool(BlendDetweenSub,	LABEL_None);
	AddTool(UseRandomSub,		LABEL_None, FALSE, SecondColumnStart, 0);
	AddTool(UseSubScale,		LABEL_None, TRUE, 0, ExtraSpaceBetweenTools);
	AddTool(SubScale,			LABEL_Expand);
	AddTool(SubStart,			LABEL_Normal );
	AddTool(SubEnd,				LABEL_Normal, FALSE, SecondColumnStart, 0);
}


void WParticleMeshTextureComponent::LinkToolsToEmitter(class UParticleEmitter* EditTarget)
{
	WParticleEditorComponent::LinkToolsToEmitter(EditTarget);

	CustomTextureSet->SetTarget( &(ParentTab->GetEmitter()->Skins) );
	DrawStyle->SetTarget( (INT*)&(EditTarget->DrawStyle) );
	NumberUSub->SetTarget( &(EditTarget->TextureUSubdivisions) );
	NumberVSub->SetTarget( &(EditTarget->TextureVSubdivisions) );
	BlendDetweenSub->SetTarget();
	UseRandomSub->SetTarget();
	UseSubScale->SetTarget();
	SubScale->SetTarget( &(EditTarget->SubdivisionScale) );
	SubStart->SetTarget( &(EditTarget->SubdivisionStart) );
	SubEnd->SetTarget( &(EditTarget->SubdivisionEnd) );
}



//=============================================================================
// WParticleTimeComponent 

WParticleTimeComponent::WParticleTimeComponent(WWindow* InOwnerWindow, WParticleEditorTab *InParentTab, class UParticleEmitter* InEditTarget, FString Name):
	WParticleEditorComponent(InOwnerWindow, InParentTab, InEditTarget, Name)
{
}
		
void WParticleTimeComponent::CreateTools()
{
	WParticleEditorComponent::CreateTools();


	Lifetime = NEW WRangeEdit(this, FString(TEXT("Lifetime")), PTOOL_RefreshOnChange | PTOOL_UpdateTools , 2181);
	Lifetime->SetAbsMin(0.001f);
	InitialTime = NEW WRangeEdit(this, FString(TEXT("Initial Time")), PTOOL_RefreshOnChange , 2180);
	InitialTime->SetAbsMin(0.0f);
	InitialDelay = NEW WRangeEdit(this, FString(TEXT("Initial Delay")), PTOOL_None , 2182);
	InitialDelay->SetAbsMin(0.0f);
	SubdivisionPlayTime = NEW WRangeEdit(this, FString(TEXT("Subdivision Playback Time")), PTOOL_None , 21811);
	SubdivisionPlayTime->SetAbsMin(0.001f);

	SecondsBeforeInactive = NEW WFloatEdit(this, FString(TEXT("Seconds Before Inactive")), PTOOL_None, 2178 );
	RelativeWarmupTime = NEW WFloatEdit(this, FString(TEXT("Relative Warmup Time")), PTOOL_RefreshOnChange, 2191 );
	WarmupTicksPerSecond = NEW WFloatEdit(this, FString(TEXT("Warmup Ticks Per Second")), PTOOL_RefreshOnChange , 2190);


	AddTool(Lifetime,				LABEL_Expand, TRUE, 0, ExtraSpaceBetweenTools );
	AddTool(InitialTime,			LABEL_ExpandClosed );
	AddTool(InitialDelay,			LABEL_ExpandClosed );
	AddTool(SubdivisionPlayTime,	LABEL_ExpandClosed );
	AddTool(SecondsBeforeInactive,	LABEL_ExpandClosed);
	AddTool(RelativeWarmupTime,		LABEL_Normal);
	AddTool(WarmupTicksPerSecond,	LABEL_Normal, FALSE, SecondColumnStart, 0);
}


void WParticleTimeComponent::LinkToolsToEmitter(class UParticleEmitter* EditTarget)
{
	WParticleEditorComponent::LinkToolsToEmitter(EditTarget);

	Lifetime->SetTarget( &(EditTarget->LifetimeRange) );
	InitialTime->SetTarget( &(EditTarget->InitialTimeRange) );
	InitialDelay->SetTarget( &(EditTarget->InitialDelayRange) );
	SubdivisionPlayTime->SetTarget( &(EditTarget->SubdivisionPlayTimeRange) );
	SecondsBeforeInactive->SetTarget( &(EditTarget->SecondsBeforeInactive) );
	RelativeWarmupTime->SetTarget( &(EditTarget->RelativeWarmupTime) );
	WarmupTicksPerSecond->SetTarget( &(EditTarget->WarmupTicksPerSecond) );
}




//=============================================================================
// WParticleMeshComponent 
//


WParticleMeshComponent::WParticleMeshComponent(WWindow* InOwnerWindow, WParticleEditorTab *InParentTab, class UParticleEmitter* InEditTarget, FString Name):
	WParticleEditorComponent(InOwnerWindow, InParentTab, InEditTarget, Name)
{
}
		
void WParticleMeshComponent::CreateTools()
{
	WParticleEditorComponent::CreateTools();

	Mesh				= NEW WMeshPickEdit(this, FString(TEXT("Mesh")), PTOOL_None, 2000 );
	//scion jg -- Allow editing of our bUnlit flag
	bUnlit				= NEW WBoolEdit(this, FString(TEXT("bUnlit")), TEXT("bUnlit"), PTOOL_None, 2000 );	
	UseMeshBlendMode	= NEW WBoolEdit(this, FString(TEXT("Use Mesh Blend Mode")), TEXT("UseMeshBlendMode"), PTOOL_None, 2000 );	
	RenderTwoSided		= NEW WBoolEdit(this, FString(TEXT("Render Two Sided")), TEXT("RenderTwoSided"), PTOOL_None, 2000 );	
	UseParticleColor	= NEW WBoolEdit(this, FString(TEXT("Use Particle Color")), TEXT("UseParticleColor"), PTOOL_None, 2000 );

	AddTool(Mesh,				LABEL_Normal, TRUE, 0, ExtraSpaceBetweenTools );
	//scion jg -- Allow editing of our bUnlit flag
	AddTool(bUnlit,				LABEL_None);
	AddTool(UseMeshBlendMode,	LABEL_None);
	AddTool(RenderTwoSided,		LABEL_None);
	AddTool(UseParticleColor,	LABEL_None);

}


void WParticleMeshComponent::LinkToolsToEmitter(class UParticleEmitter* EditTarget)
{
	WParticleEditorComponent::LinkToolsToEmitter(EditTarget);

	Mesh->SetTarget( &(((UMeshEmitter*)EditTarget)->StaticMesh) ); 
	//scion jg -- Allow editing of our bUnlit flag
	bUnlit->SetTarget();
	UseMeshBlendMode->SetTarget();
	RenderTwoSided->SetTarget();
	UseParticleColor->SetTarget();
}


/*
=============================================================================
 WParticleSparkComponent 
 */


WParticleSparkComponent::WParticleSparkComponent(WWindow* InOwnerWindow, WParticleEditorTab *InParentTab, class UParticleEmitter* InEditTarget, FString Name):
	WParticleEditorComponent(InOwnerWindow, InParentTab, InEditTarget, Name)
{
}
		
void WParticleSparkComponent::CreateTools()
{
	WParticleEditorComponent::CreateTools();

	LineSegment			= NEW WRangeEdit(this, FString(TEXT("Line Segment")), PTOOL_RefreshOnChange, 2000 );
	TimeBeforeVisible	= NEW WRangeEdit(this, FString(TEXT("Time Before Visible")), PTOOL_None, 2000 );
	TimeBetweenSegments	= NEW WRangeEdit(this, FString(TEXT("Time Between Segments")), PTOOL_None, 2000 );

	AddTool(LineSegment,			LABEL_Expand, TRUE, 0, ExtraSpaceBetweenTools );
	AddTool(TimeBeforeVisible,		LABEL_Expand );
	AddTool(TimeBetweenSegments,	LABEL_Expand );
}

void WParticleSparkComponent::LinkToolsToEmitter(class UParticleEmitter* EditTarget)
{
	WParticleEditorComponent::LinkToolsToEmitter(EditTarget);

	LineSegment->SetTarget( &(((USparkEmitter*)EditTarget)->LineSegmentsRange) );
	TimeBeforeVisible->SetTarget( &(((USparkEmitter*)EditTarget)->TimeBeforeVisibleRange) );
	TimeBetweenSegments->SetTarget( &(((USparkEmitter*)EditTarget)->TimeBetweenSegmentsRange) );
}


//=============================================================================
// WParticleLocationComponent

WParticleLocationComponent::WParticleLocationComponent(WWindow* InOwnerWindow, WParticleEditorTab *InParentTab, class UParticleEmitter* InEditTarget, FString Name):
	WParticleEditorComponent(InOwnerWindow, InParentTab, InEditTarget, Name)
{
}
		
void WParticleLocationComponent::CreateTools()
{
	WParticleEditorComponent::CreateTools();

	TArray<INT> coordIDs;
	TArray<FString> coordNames;
	coordIDs.AddItem(0);
	new(coordNames)FString(TEXT("Box"));
	coordIDs.AddItem(1);
	new(coordNames)FString(TEXT("Sphere"));
	coordIDs.AddItem(2);
	new(coordNames)FString(TEXT("Polar"));
	StartLocationShape = NEW WEnumEdit(this, FString(TEXT("Start Location Shape")), coordNames, coordIDs, PTOOL_RefreshOnChange, 2141);

	StartLocationBox = NEW WRangeVectorEdit(this, FString(TEXT("Start Location Box")), PTOOL_None, 2139 );
	StartLocationSphereRadius = NEW WRangeEdit(this, FString(TEXT("Start Location Sphere Radius")), PTOOL_None , 2107);
	StartLocationPolar = NEW WRangeVectorEdit(this, FString(TEXT("Start Location Polar")), PTOOL_None, 2139 );

	StartLocationShape->AddDisableTool(StartLocationSphereRadius, 0);
	StartLocationShape->AddDisableTool(StartLocationSphereRadius, 2);

	StartLocationShape->AddDisableTool(StartLocationPolar, 0);
	StartLocationShape->AddDisableTool(StartLocationPolar, 1);

	StartLocationShape->AddDisableTool(StartLocationBox, 1);
	StartLocationShape->AddDisableTool(StartLocationBox, 2);

	StartLocationOffset = NEW WXYZVectorEdit(this, FString(TEXT("Start Location Offset")), PTOOL_None, 2138 );
	AddLocationFromOther = ConstructEmitterPicker(this, FString(TEXT("Add Location From Other Emitter")), 2140, ParentTab->GetEmitter());


	AddTool(StartLocationShape, LABEL_Normal, TRUE, 0, ExtraSpaceBetweenTools );
	AddTool(StartLocationBox, LABEL_Expand);
	AddTool(StartLocationSphereRadius, LABEL_Expand);
	AddTool(StartLocationPolar, LABEL_Expand);
	AddTool(StartLocationOffset, LABEL_Normal);
	AddTool(AddLocationFromOther, LABEL_Normal);
}


void WParticleLocationComponent::LinkToolsToEmitter(class UParticleEmitter* EditTarget)
{
	WParticleEditorComponent::LinkToolsToEmitter(EditTarget);

	StartLocationShape->SetTarget( (INT*)&(EditTarget->StartLocationShape) );
	StartLocationBox->SetTarget( &(EditTarget->StartLocationRange) );
	StartLocationSphereRadius->SetTarget( &(EditTarget->SphereRadiusRange) );
	StartLocationPolar->SetTarget( &(EditTarget->StartLocationPolarRange) );
	StartLocationOffset->SetTarget( &(EditTarget->StartLocationOffset) );
	AddLocationFromOther->SetTarget( &(EditTarget->AddLocationFromOtherEmitter) );
}



//=============================================================================
// WParticleSizeComponent 

WParticleSizeComponent::WParticleSizeComponent(WWindow* InOwnerWindow, WParticleEditorTab *InParentTab, class UParticleEmitter* InEditTarget, FString Name):
	WParticleEditorComponent(InOwnerWindow, InParentTab, InEditTarget, Name)
{
}
		
void WParticleSizeComponent::CreateTools()
{
	WParticleEditorComponent::CreateTools();


	UniformSize = NEW WBoolEdit(this, FString(TEXT("Uniform Size")), TEXT("UniformSize"), PTOOL_None, 2000 );
	StartSize = NEW WRangeVectorEdit(this, FString(TEXT("Start Size")), PTOOL_None, 2163);

	UseSizeScale = NEW WEnableEdit(this, FString(TEXT("Use Size Scale")), TEXT("UseSizeScale"), FALSE, PTOOL_None, 2000 );
	//scion jg -- Whether to also apply the owning emitters drawscale3d value
	bOwnerDrawScale = NEW WBoolEdit(this, FString(TEXT("Apply Owner's DrawScale3D")), TEXT("bUseEmitterDrawScale3D"), PTOOL_None, 2000 );
	ShrinkParticlesEx = NEW WEnableEdit(this, FString(TEXT("Shrink Particles Exponentially")), TEXT("UseRegularSizeScale"), TRUE, PTOOL_None, 2160 );
	SizeScaleRepeats = NEW WFloatEdit(this, FString(TEXT("Size Scale Repeats")), PTOOL_None, 2000 );
	SizeScale = NEW WParticleSizeScaleArrayTool(this, FString(TEXT("Size Scale")), PTOOL_None, 2161);

	UseSizeScale->AddTool(ShrinkParticlesEx);
	UseSizeScale->AddTool(SizeScaleRepeats);
	UseSizeScale->AddTool(SizeScale);

	ShrinkParticlesEx->AddTool(SizeScaleRepeats);
	ShrinkParticlesEx->AddTool(SizeScale);


	AddTool(UniformSize, LABEL_None, TRUE, 0, ExtraSpaceBetweenTools );
	AddTool(StartSize, LABEL_Expand );
	//scion jg -- Whether to also apply the owning emitters drawscale3d value
	AddTool(bOwnerDrawScale, LABEL_None, TRUE, 0, ExtraSpaceBetweenTools);
	AddTool(UseSizeScale, LABEL_None);
	AddTool(ShrinkParticlesEx, LABEL_None);
	AddTool(SizeScaleRepeats, LABEL_Normal);
	AddTool(SizeScale, LABEL_Expand );
}


void WParticleSizeComponent::LinkToolsToEmitter(class UParticleEmitter* EditTarget)
{
	UniformSize->SetTarget();
	StartSize->SetTarget( &(EditTarget->StartSizeRange) );
	UseSizeScale->SetTarget();
	ShrinkParticlesEx->SetTarget();
	SizeScaleRepeats->SetTarget( &(EditTarget->SizeScaleRepeats) );
	SizeScale->SetTarget( &(EditTarget->SizeScale)  );
	//scion jg -- Whether to also apply the owning emitters drawscale3d value
	bOwnerDrawScale->SetTarget();
}




//=============================================================================
// WParticleCollisionComponent 

WParticleCollisionComponent::WParticleCollisionComponent(WWindow* InOwnerWindow, WParticleEditorTab *InParentTab, class UParticleEmitter* InEditTarget, FString Name):
	WParticleEditorComponent(InOwnerWindow, InParentTab, InEditTarget, Name)
{
}

		
void WParticleCollisionComponent::CreateTools()
{
	WParticleEditorComponent::CreateTools();

	UseCollision = NEW WEnableEdit(this, FString(TEXT("Use Collision")), TEXT("UseCollision"), FALSE, PTOOL_None , 2101 );
	ExtentMultiplier = NEW WXYZVectorEdit(this, FString(TEXT("Extent Multiplier")), PTOOL_RefreshOnChange, 2102 ); 
	DampingFactor = NEW WRangeVectorEdit(this, FString(TEXT("Damping Factor")), PTOOL_None , 2103);
	UseRotationDamp = NEW WEnableEdit(this, FString(TEXT("Use Rotation Damping")), TEXT("DampRotation"), FALSE, PTOOL_None, 2000);
	RotationDampingFactor = NEW WRangeRotatorEdit(this, FString(TEXT("Rotation Damping Factor")), PTOOL_None, 2157 );
	UseCollisionPlanes = NEW WEnableEdit(this, FString(TEXT("Use Collision Planes")), TEXT("UseCollisionPlanes"), FALSE, PTOOL_None , 2104);
	CollisionPlanes = NEW WParticlePlaneArrayTool(this, FString(TEXT("Collision Planes")), PTOOL_None, 2105);
	UseMaxCollisions = NEW WEnableEdit(this, FString(TEXT("Use Max Collisions")), TEXT("UseMaxCollisions"), FALSE, PTOOL_RefreshOnChange, 2106 );
	MaxCollisions = NEW WRangeEdit(this, FString(TEXT("Max Collisions")), PTOOL_None , 2107);
	SpawnFromOtherEmitter = ConstructEmitterPicker(this, FString(TEXT("Spawn From Other Emitter")), 2108, ParentTab->GetEmitter());
	SpawnAmount = NEW WIntEdit(this, FString(TEXT("Spawn Amount")), PTOOL_None , 2109);
	UseSpawnedVelocityScale = NEW WEnableEdit(this, FString(TEXT("Use Spawned Velocity Scale")), TEXT("UseSpawnedVelocityScale"), FALSE, PTOOL_None, 2111 );
	SpawnedVelocityScale = NEW WRangeVectorEdit(this, FString(TEXT("Spawned Velocity Scale")), PTOOL_None , 2110);

// scion sz - not used
#if 0
	CollisionSoundArray = NEW WParticleSoundArrayTool(this, FString(TEXT("Collision Sound Array")), PTOOL_RefreshOnChange, 2000);
	
	TArray<INT> csIDs;
	TArray<FString> csNames;
	csIDs.AddItem(0);
	new(csNames)FString(TEXT("None"));
	csIDs.AddItem(1);
	new(csNames)FString(TEXT("Linear Global"));
	csIDs.AddItem(2);
	new(csNames)FString(TEXT("Linear Local"));
	csIDs.AddItem(3);
	new(csNames)FString(TEXT("Random"));
	CollisionSound = NEW WEnumEdit(this, FString(TEXT("Collision Sound")), csNames, csIDs, PTOOL_RefreshOnChange, 2112);
	
	CollisionSoundIndex = NEW WRangeEdit(this, FString(TEXT("Collision Sound Index")), PTOOL_None , 2113);
	CollisionSoundProbability = NEW WRangeEdit(this, FString(TEXT("Collision Sound Probability")), PTOOL_None , 2114);
#endif

	// Setup dependancies for enable/disable controls
	UseCollision->AddTool(ExtentMultiplier);
	UseCollision->AddTool(DampingFactor);
	UseCollision->AddTool(UseRotationDamp);
	UseCollision->AddTool(RotationDampingFactor);
	UseCollision->AddTool(UseCollisionPlanes);
	UseCollision->AddTool(CollisionPlanes);
	UseCollision->AddTool(UseMaxCollisions);
	UseCollision->AddTool(MaxCollisions);
	UseCollision->AddTool(SpawnFromOtherEmitter);
	UseCollision->AddTool(SpawnAmount);
	UseCollision->AddTool(UseSpawnedVelocityScale);
	UseCollision->AddTool(SpawnedVelocityScale);
// scion sz - not used
#if 0
	UseCollision->AddTool(CollisionSound);
	UseCollision->AddTool(CollisionSoundIndex);
	UseCollision->AddTool(CollisionSoundProbability);
	UseCollision->AddTool(CollisionSoundArray);
#endif

	UseRotationDamp->AddTool(RotationDampingFactor);

	UseCollisionPlanes->AddTool(CollisionPlanes);

	UseMaxCollisions->AddTool(MaxCollisions);

	UseSpawnedVelocityScale->AddTool(SpawnedVelocityScale);

	// Add all the Tools to the component
	AddTool(UseCollision, LABEL_None);
	AddTool(ExtentMultiplier, LABEL_Normal);
	AddTool(DampingFactor, LABEL_Expand);	
	AddTool(UseRotationDamp, LABEL_None);
	AddTool(RotationDampingFactor, LABEL_Expand);
	AddTool(UseCollisionPlanes, LABEL_None);
	AddTool(CollisionPlanes, LABEL_Normal);
	AddTool(UseMaxCollisions, LABEL_None);
	AddTool(MaxCollisions, LABEL_Normal);
	AddTool(SpawnFromOtherEmitter, LABEL_Normal);
	AddTool(SpawnAmount, LABEL_Normal);
	AddTool(UseSpawnedVelocityScale, LABEL_None);
	AddTool(SpawnedVelocityScale, LABEL_Normal);	
// scion sz - not used
#if 0
	AddTool(CollisionSound, LABEL_Normal);
	AddTool(CollisionSoundIndex, LABEL_Normal);
	AddTool(CollisionSoundProbability, LABEL_Normal);
	AddTool(CollisionSoundArray, LABEL_Expand);
#endif
}

void WParticleCollisionComponent::LinkToolsToEmitter(class UParticleEmitter* EditTarget)
{
	WParticleEditorComponent::LinkToolsToEmitter(EditTarget);

	UseCollision->SetTarget();		
	UseRotationDamp->SetTarget();
	ExtentMultiplier->SetTarget( &(EditTarget->ExtentMultiplier) );				
	DampingFactor->SetTarget( &(EditTarget->DampingFactorRange) );					
	RotationDampingFactor->SetTarget( &(EditTarget->RotationDampingFactorRange) );			
	UseCollisionPlanes->SetTarget();				
	CollisionPlanes->SetTarget( &(EditTarget->CollisionPlanes) );				
	UseMaxCollisions->SetTarget();				
	MaxCollisions->SetTarget( &(EditTarget->MaxCollisions) );					
	SpawnFromOtherEmitter->SetTarget( &(EditTarget->SpawnFromOtherEmitter) );			
	SpawnAmount->SetTarget( &(EditTarget->SpawnAmount) );					
	UseSpawnedVelocityScale->SetTarget();		
	SpawnedVelocityScale->SetTarget( &(EditTarget->SpawnedVelocityScaleRange) );			
// scion sz - not used
#if 0
	CollisionSound->SetTarget( (INT*)&(EditTarget->CollisionSound) );					
	CollisionSoundIndex->SetTarget( &(EditTarget->CollisionSoundIndex) );			
	CollisionSoundProbability->SetTarget( &(EditTarget->CollisionSoundProbability) );		
	CollisionSoundArray->SetTarget( &(EditTarget->Sounds) );			
#endif
}




//=============================================================================
// WParticleBeamComponent

WParticleBeamComponent::WParticleBeamComponent(WWindow* InOwnerWindow, WParticleEditorTab *InParentTab, class UParticleEmitter* InEditTarget, FString Name):
	WParticleEditorComponent(InOwnerWindow, InParentTab, InEditTarget, Name)
{
}
		
void WParticleBeamComponent::CreateTools()
{
	WParticleEditorComponent::CreateTools();

	NumberofBeamPlanes = NEW WIntEdit(this, FString(TEXT("Number of Beam Planes")), PTOOL_RefreshOnChange, 2000 );
	NumberofBeamPlanes->SetMin(0);

	BeamTextureUScale = NEW WFloatEdit(this, FString(TEXT("Beam Texture UScale")), PTOOL_None, 2000 );
	BeamTextureVScale = NEW WFloatEdit(this, FString(TEXT("Beam Texture VScale")), PTOOL_None, 2000 );

	TArray<INT> endDetByIDs;
	TArray<FString> endDetByNames;
	endDetByIDs.AddItem(6);
	new(endDetByNames)FString(TEXT("None"));
	endDetByIDs.AddItem(0);
	new(endDetByNames)FString(TEXT("Velocity"));
	endDetByIDs.AddItem(1);
	new(endDetByNames)FString(TEXT("Distance"));
	endDetByIDs.AddItem(2);
	new(endDetByNames)FString(TEXT("Offset"));
	endDetByIDs.AddItem(4);
	new(endDetByNames)FString(TEXT("TraceOffset"));
	endDetByIDs.AddItem(5);
	new(endDetByNames)FString(TEXT("OffsetAsAbsolute"));
	endDetByIDs.AddItem(3);
	new(endDetByNames)FString(TEXT("Actor"));	
	DetermineEndPointBy = NEW WEnumEdit(this, FString(TEXT("Determine End Point By")), endDetByNames, endDetByIDs, PTOOL_None, 2000);

	BeamDistance = NEW WRangeEdit(this, FString(TEXT("Beam Distance")), PTOOL_None, 2000 );
	BeamEndPoints = NEW WParticleBeamEndPointArrayTool(this, FString(TEXT("Beam End Points")), PTOOL_None, 2000 );

	DetermineEndPointBy->AddDisableTool(BeamDistance, 0);
	DetermineEndPointBy->AddDisableTool(BeamDistance, 3);
	DetermineEndPointBy->AddDisableTool(BeamDistance, 4);
	DetermineEndPointBy->AddDisableTool(BeamDistance, 5);

	DetermineEndPointBy->AddDisableTool(BeamEndPoints, 0);
	DetermineEndPointBy->AddDisableTool(BeamEndPoints, 1);

	AddTool(NumberofBeamPlanes,		LABEL_Normal, TRUE, 0, ExtraSpaceBetweenTools );
	AddTool(BeamTextureUScale,		LABEL_Normal );
	AddTool(BeamTextureVScale,		LABEL_Normal, FALSE, SecondColumnStart, 0);
	AddTool(DetermineEndPointBy,	LABEL_Expand);
	AddTool(BeamDistance,			LABEL_Expand );
	AddTool(BeamEndPoints,			LABEL_Expand );
}

void WParticleBeamComponent::LinkToolsToEmitter(class UParticleEmitter* EditTarget)
{
	WParticleEditorComponent::LinkToolsToEmitter(EditTarget);

	NumberofBeamPlanes->SetTarget( &(((UBeamEmitter*)EditTarget)->RotatingSheets) );		
	BeamTextureUScale->SetTarget( &(((UBeamEmitter*)EditTarget)->BeamTextureUScale) );		
	BeamTextureVScale->SetTarget( &(((UBeamEmitter*)EditTarget)->BeamTextureVScale) );		
	DetermineEndPointBy->SetTarget( (INT*)&(((UBeamEmitter*)EditTarget)->DetermineEndPointBy) );	
	BeamDistance->SetTarget( &(((UBeamEmitter*)EditTarget)->BeamDistanceRange) );			
	BeamEndPoints->SetTarget( &(((UBeamEmitter*)EditTarget)->BeamEndPoints) );			
}




//=============================================================================
// WParticleBeamNoiseComponent

WParticleBeamNoiseComponent::WParticleBeamNoiseComponent(WWindow* InOwnerWindow, WParticleEditorTab *InParentTab, class UParticleEmitter* InEditTarget, FString Name):
	WParticleEditorComponent(InOwnerWindow, InParentTab, InEditTarget, Name)
{
}
		
void WParticleBeamNoiseComponent::CreateTools()
{
	WParticleEditorComponent::CreateTools();

	LowFrequencyNoise = NEW WRangeVectorEdit(this, FString(TEXT("Low Frequency Noise")), PTOOL_None, 2000 );
	LowFrequencyPoints = NEW WIntEdit(this, FString(TEXT("Low Frequency Points")), PTOOL_RefreshOnChange, 2000 );
	LowFrequencyPoints->SetMin(2);
	HighFrequencyNoise = NEW WRangeVectorEdit(this, FString(TEXT("High Frequency Noise")), PTOOL_None, 2000 );
	HighFrequencyPoints = NEW WIntEdit(this, FString(TEXT("High Frequency Points")), PTOOL_RefreshOnChange, 2000 );
	HighFrequencyPoints->SetMin(2);

	UseHighFrequencyScale = NEW WEnableEdit(this, FString(TEXT("Use High Frequency Scale")), TEXT("UseHighFrequencyScale"), FALSE, PTOOL_None, 2000 );
	HighFrequencyScaleFactors = NEW WParticleBeamScaleArrayTool(this, FString(TEXT("High Frequency Scale Factors")), PTOOL_None, 2000 );
	HighFrequencyScaleRepeats = NEW WFloatEdit(this, FString(TEXT("High Frequency Scale Repeats")), PTOOL_None, 2000 );
	UseHighFrequencyScale->AddTool(HighFrequencyScaleFactors);
	UseHighFrequencyScale->AddTool(HighFrequencyScaleRepeats);

	UseLowFrequencyScale =  NEW WEnableEdit(this, FString(TEXT("Use Low Frequency Scale")), TEXT("UseLowFrequencyScale"), FALSE, PTOOL_None, 2000 );
	LowFrequencyScaleFactors = NEW WParticleBeamScaleArrayTool(this, FString(TEXT("Low Frequency Scale Factors")), PTOOL_None, 2000 );
	LowFrequencyScaleRepeats = NEW WFloatEdit(this, FString(TEXT("Low Frequency Scale Repeats")), PTOOL_None, 2000 );
	UseLowFrequencyScale->AddTool(LowFrequencyScaleFactors);
	UseLowFrequencyScale->AddTool(LowFrequencyScaleRepeats);

	NoiseDeterminesEndPoint = NEW WBoolEdit(this, FString(TEXT("Noise Determines End Point")), TEXT("NoiseDeterminesEndPoint"), PTOOL_None, 2000 );

	AddTool(LowFrequencyNoise,			LABEL_Expand, TRUE, 0, ExtraSpaceBetweenTools );
	AddTool(LowFrequencyPoints,			LABEL_Normal);
	AddTool(HighFrequencyNoise,			LABEL_Expand );
	AddTool(HighFrequencyPoints,		LABEL_Normal);
	AddTool(UseHighFrequencyScale,		LABEL_None);
	AddTool(HighFrequencyScaleFactors,	LABEL_Expand);
	AddTool(HighFrequencyScaleRepeats,	LABEL_Normal);
	AddTool(UseLowFrequencyScale,		LABEL_None);
	AddTool(LowFrequencyScaleFactors,	LABEL_Expand);
	AddTool(LowFrequencyScaleRepeats,	LABEL_Normal);
	AddTool(NoiseDeterminesEndPoint,	LABEL_None);
}

void WParticleBeamNoiseComponent::LinkToolsToEmitter(class UParticleEmitter* EditTarget)
{
	WParticleEditorComponent::LinkToolsToEmitter(EditTarget);

	LowFrequencyNoise->SetTarget( &(((UBeamEmitter*)EditTarget)->LowFrequencyNoiseRange) );			
	LowFrequencyPoints->SetTarget( &(((UBeamEmitter*)EditTarget)->LowFrequencyPoints) );			
	HighFrequencyNoise->SetTarget( &(((UBeamEmitter*)EditTarget)->HighFrequencyNoiseRange) );			
	HighFrequencyPoints->SetTarget( &(((UBeamEmitter*)EditTarget)->HighFrequencyPoints) );		
	UseHighFrequencyScale->SetTarget();		
	HighFrequencyScaleFactors->SetTarget( &(((UBeamEmitter*)EditTarget)->HFScaleFactors) );	
	HighFrequencyScaleRepeats->SetTarget( &(((UBeamEmitter*)EditTarget)->HFScaleRepeats) );	
	UseLowFrequencyScale->SetTarget();		
	LowFrequencyScaleFactors->SetTarget( &(((UBeamEmitter*)EditTarget)->LFScaleFactors) );	
	LowFrequencyScaleRepeats->SetTarget( &(((UBeamEmitter*)EditTarget)->LFScaleRepeats) );	
	NoiseDeterminesEndPoint->SetTarget();	
}




//=============================================================================
// WParticleBeamBranchingComponent

WParticleBeamBranchingComponent::WParticleBeamBranchingComponent(WWindow* InOwnerWindow, WParticleEditorTab *InParentTab, class UParticleEmitter* InEditTarget, FString Name):
	WParticleEditorComponent(InOwnerWindow, InParentTab, InEditTarget, Name)
{
}
		
void WParticleBeamBranchingComponent::CreateTools()
{
	UseBranching =  NEW WEnableEdit(this, FString(TEXT("Use Branching")), TEXT("UseBranching"), FALSE, PTOOL_None, 2000 );
	BranchProbability = NEW WRangeEdit(this, FString(TEXT("Branch Probability")), PTOOL_None, 2000 );
	BranchProbability->SetAbsMin(0.0);
	BranchProbability->SetAbsMax(1.0);
	BranchEmitter = ConstructEmitterPicker(this, FString(TEXT("Branch Emitter")), 2000, ParentTab->GetEmitter());
	BranchSpawnAmount = NEW WRangeEdit(this, FString(TEXT("Branch Spawn Amount")), PTOOL_None, 2000 );
	LinkupLifetime = NEW WBoolEdit(this, FString(TEXT("Linkup Lifetime")), TEXT("LinkupLifetime"), PTOOL_None, 2000 );

	UseBranching->AddTool(BranchProbability);	
	UseBranching->AddTool(BranchEmitter);	
	UseBranching->AddTool(BranchSpawnAmount);	
	UseBranching->AddTool(LinkupLifetime);
	
	AddTool(UseBranching,		LABEL_None, TRUE, 0, ExtraSpaceBetweenTools );
	AddTool(BranchProbability,	LABEL_Normal);
	AddTool(BranchEmitter,		LABEL_Normal);
	AddTool(BranchSpawnAmount,	LABEL_Normal);
	AddTool(LinkupLifetime,		LABEL_None);
}

void WParticleBeamBranchingComponent::LinkToolsToEmitter(class UParticleEmitter* EditTarget)
{
	WParticleEditorComponent::LinkToolsToEmitter(EditTarget);

	UseBranching->SetTarget();		
	BranchProbability->SetTarget( &(((UBeamEmitter*)EditTarget)->BranchProbability) );	
	BranchEmitter->SetTarget( &(((UBeamEmitter*)EditTarget)->BranchEmitter) );		
	BranchSpawnAmount->SetTarget( &(((UBeamEmitter*)EditTarget)->BranchSpawnAmountRange) );	
	LinkupLifetime->SetTarget();		
}


