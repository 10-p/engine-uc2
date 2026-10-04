/*=============================================================================
	UnMaterial.cpp: Unreal texture / material related classes.
	Copyright 1997-1999 Epic Games, Inc. All Rights Reserved.

	Revision history:
		* Created by Jack Porter
=============================================================================*/


#include "EnginePrivate.h"

/*----------------------------------------------------------------------------
	UMaterial.
----------------------------------------------------------------------------*/

UBOOL UMaterial::CheckCircularReferences( TArray<UMaterial*>& History )
{
	guard(UMaterial::CheckCircularReferences);
	if( History.FindItemIndex(this) != INDEX_NONE )
		return 0;
	return 1;
	unguard;
}

UMaterial::UMaterial()
{
}

void UMaterial::Serialize( FArchive& Ar )
{
	//!!vogel: material duplication code hack.
	if( !(GUglyHackFlags & 8) )
		Super::Serialize( Ar );
}

void UMaterial::execMaterialUSize( FFrame& Stack, RESULT_DECL )
{
	guard(UMaterial::execMaterialUSize);
	P_FINISH;
	*(INT*)Result = MaterialUSize();
	unguard;
}
IMPLEMENT_FUNCTION( UMaterial, -1, execMaterialUSize );

void UMaterial::execMaterialVSize( FFrame& Stack, RESULT_DECL )
{
	guard(UMaterial::execMaterialVSize);
	P_FINISH;
	*(INT*)Result = MaterialVSize();
	unguard;
}
IMPLEMENT_FUNCTION( UMaterial, -1, execMaterialVSize );


IMPLEMENT_CLASS(UMaterial);

/*----------------------------------------------------------------------------
	Misc material stuff
----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(UBitmapMaterial);
IMPLEMENT_CLASS(UProxyBitmapMaterial);
IMPLEMENT_CLASS(URenderedMaterial);
IMPLEMENT_CLASS(UTexCoordMaterial);
IMPLEMENT_CLASS(UColorModifier);
IMPLEMENT_CLASS(UMultiPassMaterial);

/*----------------------------------------------------------------------------
	Shaders
----------------------------------------------------------------------------*/

//
// UShader
//
UBOOL UShader::CheckCircularReferences( TArray<UMaterial*>& History )
{
	guard(UShader::CheckCircularReferences);
	if( !Super::CheckCircularReferences(History))
		return 0;
	
	INT NewItem = History.Add();
	History(NewItem) = this;

	if(	( Diffuse				&& !Diffuse->CheckCircularReferences(History)				) ||
		( Opacity				&& !Opacity->CheckCircularReferences(History)				) ||
		( Specular				&& !Specular->CheckCircularReferences(History)				) ||
		( SpecularityMask		&& !SpecularityMask->CheckCircularReferences(History)		) ||
		( SelfIllumination		&& !SelfIllumination->CheckCircularReferences(History)		) ||
		( SelfIlluminationMask	&& !SelfIlluminationMask->CheckCircularReferences(History)	) ||
		( Detail				&& !Detail->CheckCircularReferences(History)				) )
		return 0;

	History.Remove(NewItem);
	return 1;
	unguard;
}

BYTE UShader::RequiredUVStreams()
{
	BYTE	UVStreamMask = 1;

	if(Diffuse)
		UVStreamMask |= Diffuse->RequiredUVStreams();

	if(Opacity)
		UVStreamMask |= Opacity->RequiredUVStreams();

	if(Specular)
		UVStreamMask |= Specular->RequiredUVStreams();

	if(SpecularityMask)
		UVStreamMask |= SpecularityMask->RequiredUVStreams();

	if(SelfIllumination)
		UVStreamMask |= SelfIllumination->RequiredUVStreams();

	if(SelfIlluminationMask)
		UVStreamMask |= SelfIlluminationMask->RequiredUVStreams();

	if(Detail)
		UVStreamMask |= Detail->RequiredUVStreams();

	return UVStreamMask;
}

UBOOL UShader::RequiresSorting()
{
	return
		(OutputBlending == OB_Normal && Opacity)	||
		 OutputBlending == OB_Modulate				||
		 OutputBlending == OB_Brighten				||
		 OutputBlending == OB_Darken				||
		 OutputBlending == OB_Translucent;
}

UBOOL UShader::RequiresNormal()
{
	if(Diffuse && Diffuse->RequiresNormal())
		return 1;

	if(Opacity && Opacity->RequiresNormal())
		return 1;

	if(Specular && Specular->RequiresNormal())
		return 1;

	if(SpecularityMask && SpecularityMask->RequiresNormal())
		return 1;

	if(SelfIllumination && SelfIllumination->RequiresNormal())
		return 1;

	if(SelfIlluminationMask && SelfIlluminationMask->RequiresNormal())
		return 1;

	if(Detail && Detail->RequiresNormal())
		return 1;

	return 0;
}

UBOOL UShader::IsTransparent()
{
	return RequiresSorting();
}

INT UShader::MaterialUSize()
{
	if( Diffuse )
		return Diffuse->MaterialUSize();
	if( SelfIllumination )
		return SelfIllumination->MaterialUSize();
	return 0;
}

INT UShader::MaterialVSize()
{
	if( Diffuse )
		return Diffuse->MaterialVSize();
	if( SelfIllumination )
		return SelfIllumination->MaterialVSize();
	return 0;
}

void UShader::PostEditChange()
{
	guard(UShader::PostEditChange);
	Super::PostEditChange();

	// Mark the package as dirty
	UObject* Pkg = this;
	while( Pkg->GetOuter() )	Pkg = Pkg->GetOuter();
	if( Pkg && Pkg->IsA(UPackage::StaticClass() ) )
		((UPackage*)Pkg)->bDirty = 1;
	
	unguard;
}

void UShader::PreSetMaterial(FLOAT TimeSeconds)
{
	guard(UShader::PreSetMaterial);
	if(Diffuse)
		Diffuse->PreSetMaterial(TimeSeconds);
	if(Opacity)
		Opacity->PreSetMaterial(TimeSeconds);
	if(Specular)
		Specular->PreSetMaterial(TimeSeconds);
	if(SpecularityMask)
		SpecularityMask->PreSetMaterial(TimeSeconds);
	if(SelfIllumination)
		SelfIllumination->PreSetMaterial(TimeSeconds);
	if(SelfIlluminationMask)
		SelfIlluminationMask->PreSetMaterial(TimeSeconds);
	if(Detail)
		Detail->PreSetMaterial(TimeSeconds);
	unguard;
}

IMPLEMENT_CLASS(UShader);

//
// UCombiner
//
UBOOL UCombiner::CheckCircularReferences( TArray<UMaterial*>& History )
{
	guard(UCombiner::CheckCircularReferences);
	if( !Super::CheckCircularReferences(History))
		return 0;
	
	INT NewItem = History.Add();
	History(NewItem) = this;

	if(	( Material1	&& !Material1->CheckCircularReferences(History) ) ||
		( Material2	&& !Material2->CheckCircularReferences(History) ) ||
		( Mask		&& !Mask->CheckCircularReferences(History) ) )
		return 0;

	History.Remove(NewItem);
	return 1;
	unguard;
}

void UCombiner::PostEditChange()
{
	guard(UCombiner::PostEditChange);
	Super::PostEditChange();

	// Mark the package as dirty
	UObject* Pkg = this;
	while( Pkg->GetOuter() )	Pkg = Pkg->GetOuter();
	if( Pkg && Pkg->IsA(UPackage::StaticClass() ) )
		((UPackage*)Pkg)->bDirty = 1;
	
	unguard;
}

void UCombiner::PreSetMaterial(FLOAT TimeSeconds)
{
	guard(UCombiner::PreSetMaterial);
	if( Material1 )
		Material1->PreSetMaterial(TimeSeconds);
	if( Material2 )
		Material2->PreSetMaterial(TimeSeconds);	
	if( Mask && Mask!=Material1 && Mask!=Material2 )
		Mask->PreSetMaterial(TimeSeconds);
	unguard;
}

IMPLEMENT_CLASS(UCombiner);


/*----------------------------------------------------------------------------
	Constant Materials.
----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(UConstantMaterial);
IMPLEMENT_CLASS(UConstantColor);


FColor UFadeColor::GetColor(FLOAT TimeSeconds)
{
	guard(UFadeColor::GetColor);

	FLOAT	Time = (TimeSeconds + FadePhase) / FadePeriod;

	if(ColorFadeType == FC_Sinusoidal)
	{
		FLOAT	Percent = 0.5f + appCos(Time * PI * 0.5f) * 0.5f;

		return FColor(Color1.Plane() * Percent + Color2.Plane() * (1.0f - Percent));
	}
	else if(ColorFadeType == FC_Linear)
	{
		INT		Cycle = appFloor(Time);
		FLOAT	Percent = (Cycle % 2) ? (Time - Cycle) : 1.0f - (Time - Cycle);

		return FColor(Color1.Plane() * Percent + Color2.Plane() * (1.0f - Percent));
	}
	else
		return Color1;

	unguard;
}

IMPLEMENT_CLASS(UFadeColor);


/*----------------------------------------------------------------------------
	Modifiers.
----------------------------------------------------------------------------*/

//
// UModifier
//
UBOOL UModifier::CheckCircularReferences( TArray<UMaterial*>& History )
{
	guard(UModifier::CheckCircularReferences);
	if( !Super::CheckCircularReferences(History))
		return 0;
	
	INT NewItem = History.Add();
	History(NewItem) = this;

	if( Material && !Material->CheckCircularReferences(History) )
		return 0;

	History.Remove(NewItem);
	return 1;
	unguard;
}

void UModifier::PostEditChange()
{
	guard(UModifier::PostEditChange);
	Super::PostEditChange();

	// Mark the package as dirty
	UObject* Pkg = this;
	while( Pkg->GetOuter() )	Pkg = Pkg->GetOuter();
	if( Pkg && Pkg->IsA(UPackage::StaticClass() ) )
		((UPackage*)Pkg)->bDirty = 1;
	
	unguard;
}

IMPLEMENT_CLASS(UModifier);

//
// UTexCoordModifier
//
INT UTexModifier::MaterialUSize()
{
	INT USize = 0;
	if( Material )
		USize = Material->MaterialUSize();
	return USize;
}

INT UTexModifier::MaterialVSize()
{
	INT VSize = 0;
	if( Material )
		VSize = Material->MaterialVSize();
	return VSize;
}

BYTE UTexModifier::RequiredUVStreams()
{
	if(TexCoordSource >= TCS_Stream0 && TexCoordSource <= TCS_Stream7)
		return (Material ? Material->RequiredUVStreams() : 1) | (1 << (TexCoordSource - TCS_Stream0));
	else
		return Material ? Material->RequiredUVStreams() : 1;
}

UBOOL UTexModifier::GetValidated()
{
	if( Material )
		return Material->GetValidated();
	return 1;
}

void UTexModifier::SetValidated( UBOOL InValidated )
{
	if( Material )
		Material->SetValidated(InValidated);
}

IMPLEMENT_CLASS(UTexModifier);

//
// UTexPannerTriggered
//
FMatrix* UTexPannerTriggered::GetMatrix(FLOAT TimeSeconds)
{
	guard(UTexPannerTriggered::GetMatrix);

	FLOAT FakeTime;
	if( TriggeredTime < 0.f )
		FakeTime = 0.f;
	else
	{
		FLOAT FakeTimeMax = StopAfterPeriod / PanRate;
		if( Reverse )
		{
			FakeTime = FakeTimeMax - (TimeSeconds - TriggeredTime);
			if( FakeTime < 0.f )
				FakeTime = 0.f;
		}
		else
		{
			FakeTime = (TimeSeconds - TriggeredTime);
			if( FakeTime > FakeTimeMax )
				FakeTime = FakeTimeMax;
		}
	}

	return UTexPanner::GetMatrix( GIsEditor ? TimeSeconds : FakeTime );

	unguard;
}
IMPLEMENT_CLASS(UTexPannerTriggered);

//
// UTexPanner
//
#define TEXPANNER_PANMAX 1024
FMatrix* UTexPanner::GetMatrix(FLOAT TimeSeconds)
{
	FVector D = PanDirection.Vector();
	FLOAT Su = TimeSeconds * PanRate * D.X / TEXPANNER_PANMAX;
	FLOAT Sv = TimeSeconds * PanRate * D.Y / TEXPANNER_PANMAX;
	FLOAT  du = (Su - appFloor(Su)) * TEXPANNER_PANMAX;
	FLOAT  dv = (Sv - appFloor(Sv)) * TEXPANNER_PANMAX;
	M = FMatrix (
					FPlane(1,0,0,0),
					FPlane(0,1,0,0),
					FPlane(du,dv,1,0),
					FPlane(0,0,0,1)
				);
	return &M;
}
IMPLEMENT_CLASS(UTexPanner);

//
// UTexScaler
//
FMatrix* UTexScaler::GetMatrix(FLOAT TimeSeconds)
{
	M = FMatrix (
					FPlane(1.f/UScale,0,0,0),
					FPlane(0,1.f/VScale,0,0),
					FPlane(UOffset/MaterialUSize(),VOffset/MaterialVSize(),1,0),
					FPlane(0,0,0,1)
				);
	return &M;
}
IMPLEMENT_CLASS(UTexScaler);

//
// UTexRotator
//
FMatrix* UTexRotator::GetMatrix(FLOAT TimeSeconds)
{
	switch( TexRotationType)
	{
	case TR_ConstantlyRotating:
		{
			FRotator R = Rotation * TimeSeconds;
			R.Pitch	&= 0xFFFF;
			R.Yaw	&= 0xFFFF;
			R.Roll	&= 0xFFFF;
			M = FRotationMatrix(R);
		}
		break;
	case TR_OscillatingRotation:
		{
			FLOAT S_Pitch = TimeSeconds * OscillationRate.Pitch / 0xFFFF;
			FLOAT d_Pitch = (FLOAT)OscillationAmplitude.Pitch * appSin( 2.f * PI * ((S_Pitch - appFloor(S_Pitch)) + OscillationPhase.Pitch) );
			FLOAT S_Yaw = TimeSeconds * OscillationRate.Yaw / 0xFFFF;
			FLOAT d_Yaw = (FLOAT)OscillationAmplitude.Yaw * appSin( 2.f * PI * ((S_Yaw - appFloor(S_Yaw)) + OscillationPhase.Yaw) );
			FLOAT S_Roll = TimeSeconds * OscillationRate.Roll / 0xFFFF;
			FLOAT d_Roll = (FLOAT)OscillationAmplitude.Roll * appSin( 2.f * PI * ((S_Roll - appFloor(S_Roll)) + OscillationPhase.Roll) );

			M = FRotationMatrix(Rotation + FRotator(d_Pitch, d_Yaw, d_Roll));
		}
		break;
	default:
		M = FRotationMatrix(Rotation);
		break;
	}
	
	if( UOffset != 0.f || VOffset != 0.f )
	{
		FLOAT du = UOffset/MaterialUSize();
		FLOAT dv = VOffset/MaterialVSize();
		
		M = FMatrix(
					FPlane(1,0,0,0),
					FPlane(0,1,0,0),
					FPlane(-du,-dv,1,0),
					FPlane(0,0,0,1)
			) *
			M *
			FMatrix(
					FPlane(1,0,0,0),
					FPlane(0,1,0,0),
					FPlane(du,dv,1,0),
					FPlane(0,0,0,1)
			);
	}

	return &M;
}
IMPLEMENT_CLASS(UTexRotator);

//
// UTexOscillatorTriggered
//
FMatrix* UTexOscillatorTriggered::GetMatrix(FLOAT TimeSeconds)
{
	guard(UTexOscillatorTriggered::GetMatrix);

	FLOAT FakeTime;
	if( TriggeredTime < 0.f )
		FakeTime = 0.f;
	else
	{
		FLOAT FakeTimeMax = StopAfterPeriod / Max<FLOAT>( UOscillationRate, VOscillationRate );
		if( Reverse )
		{
			FakeTime = FakeTimeMax - (TimeSeconds - TriggeredTime);
			if( FakeTime < 0.f )
				FakeTime = 0.f;
		}
		else
		{
			FakeTime = (TimeSeconds - TriggeredTime);
			if( FakeTime > FakeTimeMax )
				FakeTime = FakeTimeMax;
		}
	}

	return UTexOscillator::GetMatrix( GIsEditor ? TimeSeconds : FakeTime );

	unguard;
}
IMPLEMENT_CLASS(UTexOscillatorTriggered);

//
// UTexOscillator
//
void UTexOscillator::PostEditChange()
{
	guard(UTexOscillator::PostEditChange);
	Super::PostEditChange();
	LastSu = LastSv = 0.f;
	unguard;
}
FMatrix* UTexOscillator::GetMatrix(FLOAT TimeSeconds)
{
	M = FMatrix::Identity;

	FLOAT Su = TimeSeconds * UOscillationRate;
	FLOAT Sv = TimeSeconds * VOscillationRate;

	switch( UOscillationType )
	{
	case OT_Jitter:
		{
			if( LastSu < 1.f || LastSu > Su + 1.f )
				LastSu = UOscillationPhase + appFloor(Su);
			if( Su-LastSu > 1.f )
			{
				CurrentUJitter = appFrand() * UOscillationAmplitude;
				LastSu = UOscillationPhase + appFloor(Su);
			}
			M.M[2][0] = CurrentUJitter;
		}
		break;
	case OT_Pan:
		{
			FLOAT  du = UOscillationAmplitude * appSin( 2.f * PI * (Su - appFloor(Su)) + 2.f * PI * UOscillationPhase );
			M.M[2][0] = du;
		}
		break;
	case OT_Stretch:
		{
			FLOAT  du = UOscillationAmplitude * appSin( 2.f * PI * (Su - appFloor(Su)) + 2.f * PI * UOscillationPhase );
			M.M[0][0] = 1.f+du;
		}
		break;
	case OT_StretchRepeat:
		{
			FLOAT  du = UOscillationAmplitude * appSin( 0.5f * PI * (Su - appFloor(Su)) + 2.f * PI * UOscillationPhase );
			M.M[0][0] = 1.f+du;
		}
		break;
	}

	switch( VOscillationType )
	{
	case OT_Jitter:
		{
			if( LastSv < 1.f || LastSu > Sv + 1.f )
				LastSv = VOscillationPhase + appFloor(Sv);
			if( Sv-LastSv > 1.f )
			{
				CurrentVJitter = appFrand() * VOscillationAmplitude;
				LastSv = VOscillationPhase + appFloor(Sv);
			}
			M.M[2][1] = CurrentVJitter;
		}
		break;
	case OT_Pan:
		{
			FLOAT  dv = VOscillationAmplitude * appSin( 2.f * PI * (Sv - appFloor(Sv)) + 2.f * PI * VOscillationPhase );
			M.M[2][1] = dv;
		}
		break;
	case OT_Stretch:
		{
			FLOAT  dv = VOscillationAmplitude * appSin( 2.f * PI * (Sv - appFloor(Sv)) + 2.f * PI * VOscillationPhase );
			M.M[1][1] = 1.f+dv;
		}
		break;
	case OT_StretchRepeat:
		{
			FLOAT  dv = VOscillationAmplitude * appSin( 0.5f * PI * (Sv - appFloor(Sv)) + 2.f * PI * VOscillationPhase );
			M.M[1][1] = 1.f+dv;
		}
		break;
	}

	if( UOffset != 0.f || VOffset != 0.f )
	{
		FLOAT du = UOffset/MaterialUSize();
		FLOAT dv = VOffset/MaterialVSize();
		
		M = FMatrix(
					FPlane(1,0,0,0),
					FPlane(0,1,0,0),
					FPlane(-du,-dv,1,0),
					FPlane(0,0,0,1)
			) *
			M *
			FMatrix(
					FPlane(1,0,0,0),
					FPlane(0,1,0,0),
					FPlane(du,dv,1,0),
					FPlane(0,0,0,1)
			);
	}
	
	return &M;
}
IMPLEMENT_CLASS(UTexOscillator);

//
// UTexEnvMap.
//
FMatrix* UTexEnvMap::GetMatrix(FLOAT TimeSeconds)
{
	static FMatrix TexSpMat(FPlane(0.5f,0.f,0.f,0.f),
							FPlane(0.0f,-0.5f,0.f,0.f),
                            FPlane(0.0f,0.f,1.0f,0.f),
                            FPlane(0.5f,0.5f,0.f,0.f));

	switch(EnvMapType)
	{
	case EM_WorldSpace:
		TexCoordSource = TCS_WorldEnvMapCoords;
		TexCoordCount = TCN_3DCoords;
		break;
	case EM_CameraSpace:
		TexCoordSource = TCS_CameraEnvMapCoords;
		TexCoordCount = TCN_3DCoords;
		break;

	// scion sz -
	// old method of doing sphere mapping that just used the camera direction directly
	// see FD3DRenderInterface::ApplyTexModifier for TCS_CameraEnvMapNormals
	case EM_SphereMapOld:
		TexCoordSource = TCS_CameraEnvMapNormals;		
		TexCoordCount = TCN_3DCoords;
		TexCoordProjected = false;
		// Generate spheremap texture coords from the camera space normal. This has
		// two steps. First, tell D3D to use the vertex normal (in camera space) as
		// texture coordinates. Then, we setup a texture matrix to transform these
		// texcoords from (-1,+1) view space to (0,1) texture space. This way,
		// the normal can be used to look up a texel in the spheremap.
		return &TexSpMat;

	// scion sz -
	// new method of doing sphere mapping that aligns the camera with the current object being rendered
	// see FD3DRenderInterface::ApplyTexModifier for TCS_CameraToActorEnvMapNormals
	case EM_SphereMap:
		TexCoordSource = TCS_CameraToActorEnvMapNormals;
		TexCoordCount = TCN_3DCoords;
		TexCoordProjected = false;
		// same as above
		return &TexSpMat;

	// scion sz -
	// applies current camera's projection matrix to the camera space vertex position
	// to generate UVs
	case EM_ProjectSpace:
		TexCoordSource = TCS_CameraProjCoords;
		TexCoordCount = TCN_4DCoords;
		TexCoordProjected = true;
		return &TexSpMat;
	}

	return NULL;
}
IMPLEMENT_CLASS(UTexEnvMap);

//
// UTexMatrix
//
IMPLEMENT_CLASS(UTexMatrix);

//
// UTexCoordSource
//

void UTexCoordSource::PostEditChange()
{
	Super::PostEditChange();

	SourceChannel = Clamp(SourceChannel,0,7);
	TexCoordSource = (ETexCoordSource) (TCS_Stream0 + SourceChannel);
}

IMPLEMENT_CLASS(UTexCoordSource);

//
// UVertexColor
//

IMPLEMENT_CLASS(UVertexColor);

//
// UFinalBlend.
//

UBOOL UFinalBlend::RequiresSorting()
{
	return
		FrameBufferBlending == FB_Modulate		||
		FrameBufferBlending == FB_AlphaBlend	||
		FrameBufferBlending == FB_AlphaModulate_MightNotFogCorrectly ||
		FrameBufferBlending == FB_Translucent	||
		FrameBufferBlending == FB_Darken		||
		FrameBufferBlending == FB_Brighten;
}

UBOOL UFinalBlend::IsTransparent()
{
	return RequiresSorting();
}

void UFinalBlend::PostEditChange()
{
	guard(UFinalBlend::PostEditChange);
	Super::PostEditChange();

	// Mark the package as dirty
	UObject* Pkg = this;
	while( Pkg->GetOuter() )	Pkg = Pkg->GetOuter();
	if( Pkg && Pkg->IsA(UPackage::StaticClass() ) )
		((UPackage*)Pkg)->bDirty = 1;
	
	unguard;
}

UBOOL UFinalBlend::GetValidated()
{
	if( Material )
		return Material->GetValidated();
	return 1;
}

void UFinalBlend::SetValidated( UBOOL InValidated )
{
	if( Material )
		Material->SetValidated(InValidated);
}
IMPLEMENT_CLASS(UFinalBlend);

//
// UMaterialSwitch
//
UBOOL UMaterialSwitch::CheckCircularReferences( TArray<UMaterial*>& History )
{
	guard(UMaterialSwitch::CheckCircularReferences);
	if( !Super::CheckCircularReferences(History))
		return 0;
	
	INT NewItem = History.Add();
	History(NewItem) = this;

	for( INT i=0;i<Materials.Num();i++ )
		if( Materials(i) && !Materials(i)->CheckCircularReferences(History) )
			return 0;

	History.Remove(NewItem);
	return 1;
	unguard;
}

void UMaterialSwitch::PostEditChange()
{
	guard(UMaterialSwitch::PostEditChange);
	Super::PostEditChange();
	Material = Current >=0 && Current < Materials.Num() ? Materials(Current) : NULL;
	unguard;
}

IMPLEMENT_CLASS(UMaterialSwitch)

//
// UMaterialSequence
//
void UMaterialSequence::PostEditChange()
{
	guard(UMaterialSequence::PostEditChange);
	Super::PostEditChange();
	CurrentTime = 0.f;
	LastTime = 0.f;
	TotalTime = 0.f;
	for( INT i=0;i<SequenceItems.Num();i++ )
		TotalTime += SequenceItems(i).Time;
	unguard;
}

void UMaterialSequence::Serialize(FArchive& Ar)
{
	guard(UMaterialSequence::Serialize);
	// Make sure we don't store a reference to our internal Combiner.
	if( Ar.IsSaving() )
		Material = NULL;
	Super::Serialize(Ar);
	unguard;
}

void UMaterialSequence::PreSetMaterial(FLOAT TimeSeconds)
{
	guard(UMaterialSequence::PreSetMaterial);
	if( !Paused )
	{
		// Level change.
		if( LastTime > TimeSeconds )
		{
			LastTime = 0.f;
			CurrentTime = 0.f;
		}
		else
		if( LastTime > 0.f )
			CurrentTime += TimeSeconds-LastTime;
		LastTime = TimeSeconds;

		// Handle Looping
		if( Loop && TotalTime > 0.f && CurrentTime > TotalTime )
		{
			CurrentTime -= appFloor(CurrentTime/TotalTime) * TotalTime;
			while( CurrentTime > TotalTime )
				CurrentTime -= TotalTime;
		}

		FLOAT CountTime = 0.f;
		for( INT i=0;i<SequenceItems.Num();i++ )
		{
			if( CurrentTime >= CountTime && (CurrentTime < CountTime + SequenceItems(i).Time || (!Loop && i==SequenceItems.Num()-1) ) )
			{
                switch( SequenceItems(i).Action )
				{
				case MSA_ShowMaterial:
					Material = SequenceItems(i).Material;
					break;
				case MSA_FadeToMaterial:
					if( CurrentTime >= CountTime + SequenceItems(i).Time )
						Material = SequenceItems(i).Material;
					else
					{
						DECLARE_STATIC_UOBJECT( UConstantColor, ConstantColor, {} );
						DECLARE_STATIC_UOBJECT( UCombiner, Combiner,
							{
								Combiner->Mask = ConstantColor;
								Combiner->CombineOperation = CO_AlphaBlend_With_Mask;
							}
						);
						ConstantColor->Color = FColor(255,255,255,Clamp<INT>( appRound(255.f * ((CurrentTime-CountTime)/SequenceItems(i).Time)), 0, 255) );
						INT prev = i-1;
						if( prev < 0 )
							prev = SequenceItems.Num()-1;
						Combiner->Material1 = SequenceItems(prev).Material;
						Combiner->Material2 = SequenceItems(i).Material;
						Material = Combiner;
					}
					break;

				}
				break;
			}
			CountTime += SequenceItems(i).Time;
		}
	}
	Super::PreSetMaterial(TimeSeconds);
	unguard;
}

UBOOL UMaterialSequence::CheckCircularReferences( TArray<class UMaterial*>& History )
{
	guard(UMaterialSequence::CheckCircularReferences);
	if( !Super::CheckCircularReferences(History))
		return 0;
	
	INT NewItem = History.Add();
	History(NewItem) = this;

	for( INT i=0;i<SequenceItems.Num();i++ )
		if( SequenceItems(i).Material && !SequenceItems(i).Material->CheckCircularReferences(History) )
			return 0;

	History.Remove(NewItem);
	return 1;

	unguard;
}

IMPLEMENT_CLASS(UMaterialSequence);

//
// UOpacityModifier
//
IMPLEMENT_CLASS(UOpacityModifier);

/*----------------------------------------------------------------------------
	ConvertPolyFlagsToMaterial
----------------------------------------------------------------------------*/

//!! OLDVER
UMaterial* UMaterial::ConvertPolyFlagsToMaterial( UMaterial* InMaterial, DWORD InPolyFlags )
{
	guard(UMaterial::ConvertPolyFlagsToMaterial);
	
	// Create material for shiny environment mapping
	UTexture* Texture;
	if( (Texture=Cast<UTexture>(InMaterial)) != NULL && Texture->OLDEnvironmentMap )
	{
		FString NewName = FString::Printf(TEXT("%s_%sShiny"), Texture->GetName(), (InPolyFlags&PF_AlphaTexture) ? TEXT("Const") : TEXT("") );
		FString NewFinalName = NewName;

		if( InPolyFlags&PF_AlphaTexture )
			NewFinalName += TEXT("_Alpha");

		UShader* NewMaterial = FindObject<UShader>( ANY_PACKAGE, *NewFinalName );
		if( !NewMaterial )
		{
			FString TexCoordName = FString::Printf(TEXT("%s_EnvMapCoords"), Texture->OLDEnvironmentMap->GetName() );
			UTexEnvMap* EnvMapTex = FindObject<UTexEnvMap>( ANY_PACKAGE, *TexCoordName );
			if( !EnvMapTex )
			{
				EnvMapTex = ConstructObject<UTexEnvMap>( UTexEnvMap::StaticClass(), Texture->OLDEnvironmentMap->GetOuter(), FName(*TexCoordName), RF_Public|RF_Standalone );
				switch( Texture->OLDEnvMapTransformType )
				{
				case EMTT_ViewSpace:	EnvMapTex->EnvMapType = EM_CameraSpace; break;
				case EMTT_WorldSpace:	EnvMapTex->EnvMapType = EM_WorldSpace; break;
				}
				EnvMapTex->Material = Texture->OLDEnvironmentMap;
			}

			NewMaterial = ConstructObject<UShader>( UShader::StaticClass(), InMaterial->GetOuter(), FName(*NewName), RF_Public|RF_Standalone );	
			NewMaterial->Diffuse = Texture;
			NewMaterial->Specular = EnvMapTex;

			if( InPolyFlags&PF_AlphaTexture )
			{
				NewMaterial->Opacity = Texture;
				//!!MAT
				//NewMaterial->SpecularityMask = FColor(0,0,0,255*Texture->OLDSpecular);
			}
			else
				NewMaterial->SpecularityMask = Texture;
		}

		return NewMaterial;
	}

	// Create material for alpha texture
	//!!MAT
	//!! should unlit be handled this way?
	if( InPolyFlags&PF_Unlit )
	{
		FString NewName = FString::Printf(TEXT("%s_Unlit%s"), InMaterial->GetName(), InPolyFlags&PF_TwoSided?TEXT("TwoSided"):TEXT(""));
		UShader* NewMaterial = FindObject<UShader>( ANY_PACKAGE, *NewName );
		if( !NewMaterial )
		{
			NewMaterial = ConstructObject<UShader>( UShader::StaticClass(), InMaterial->GetOuter(), FName(*NewName), RF_Public|RF_Standalone );
			NewMaterial->TwoSided = InPolyFlags&PF_TwoSided?1:0;
			NewMaterial->SelfIllumination = InMaterial;
		}
		return NewMaterial;
	}

	// Create material for alpha texture
	if( InPolyFlags&PF_AlphaTexture )
	{
		FString NewName = FString::Printf(TEXT("%s_Alpha%s"), InMaterial->GetName(), InPolyFlags&PF_TwoSided?TEXT("TwoSided"):TEXT(""));
		UShader* NewMaterial = FindObject<UShader>( ANY_PACKAGE, *NewName );
		if( !NewMaterial )
		{
			NewMaterial = ConstructObject<UShader>( UShader::StaticClass(), InMaterial->GetOuter(), FName(*NewName), RF_Public|RF_Standalone );
			NewMaterial->TwoSided = InPolyFlags&PF_TwoSided?1:0;
			NewMaterial->Diffuse = InMaterial;
			NewMaterial->Opacity = InMaterial;
		}
		return NewMaterial;
	}

	// Create material for masked texture
	if( InPolyFlags&PF_Masked )
	{
		FString NewName = FString::Printf(TEXT("%s_Masked%s"), InMaterial->GetName(), InPolyFlags&PF_TwoSided?TEXT("TwoSided"):TEXT(""));
		UShader* NewMaterial = FindObject<UShader>( ANY_PACKAGE, *NewName );
		if( !NewMaterial )
		{
			NewMaterial = ConstructObject<UShader>( UShader::StaticClass(), InMaterial->GetOuter(), FName(*NewName), RF_Public|RF_Standalone );
			NewMaterial->TwoSided = InPolyFlags&PF_TwoSided?1:0;
			NewMaterial->Diffuse = InMaterial;
			NewMaterial->Opacity = InMaterial;
			NewMaterial->OutputBlending = OB_Masked;
		}
		return NewMaterial;
	}

	// Create material for modulated texture
	if( InPolyFlags&PF_Modulated )
	{
		FString NewName = FString::Printf(TEXT("%s_Modulated%s"), InMaterial->GetName(), InPolyFlags&PF_TwoSided?TEXT("TwoSided"):TEXT(""));
		UShader* NewMaterial = FindObject<UShader>( ANY_PACKAGE, *NewName );
		if( !NewMaterial )
		{
			NewMaterial = ConstructObject<UShader>( UShader::StaticClass(), InMaterial->GetOuter(), FName(*NewName), RF_Public|RF_Standalone );
			NewMaterial->TwoSided = InPolyFlags&PF_TwoSided?1:0;
			NewMaterial->Diffuse = InMaterial;
			NewMaterial->OutputBlending = OB_Modulate;
		}
		return NewMaterial;
	}

	// Create material for translucent texture
	if( InPolyFlags&PF_Translucent )
	{
		FString NewName = FString::Printf(TEXT("%s_Translucent%s"), InMaterial->GetName(), InPolyFlags&PF_TwoSided?TEXT("TwoSided"):TEXT(""));
		UShader* NewMaterial = FindObject<UShader>( ANY_PACKAGE, *NewName );
		if( !NewMaterial )
		{
			NewMaterial = ConstructObject<UShader>( UShader::StaticClass(), InMaterial->GetOuter(), FName(*NewName), RF_Public|RF_Standalone );
			NewMaterial->TwoSided = InPolyFlags&PF_TwoSided?1:0;
			NewMaterial->Diffuse = InMaterial;
			NewMaterial->OutputBlending = OB_Translucent;
		}
		return NewMaterial;
	}

	// Create material for double-sided texture
	if( InPolyFlags&PF_TwoSided )
	{
		FString NewName = FString::Printf(TEXT("%s_TwoTwoSided"), InMaterial->GetName());
		UShader* NewMaterial = FindObject<UShader>( ANY_PACKAGE, *NewName );
		if( !NewMaterial )
		{
			NewMaterial = ConstructObject<UShader>( UShader::StaticClass(), InMaterial->GetOuter(), FName(*NewName), RF_Public|RF_Standalone );
			NewMaterial->TwoSided = InPolyFlags&PF_TwoSided?1:0;
			NewMaterial->Diffuse = InMaterial;
		}
		return NewMaterial;
	}

	return InMaterial;
	unguard;
}


IMPLEMENT_CLASS(UMovieMaterial);

/* scion ======================================================================
 * Author: jg
 * ============================================================================
 *
 * This class bridges the layering between Engine and D3DDrv.
 *
 * ============================================================================
 */
class FMovieTexture : public FBaseTexture
{
private:

	UMovieMaterial* pMovieMaterial;

public:

	FMovieTexture(UMovieMaterial* pMovie)
	{
		pMovieMaterial = pMovie;
	}

	// Never cache this as there is no "real" data in the UMovieMaterial
	// This will be manually fixed up
	virtual INT GetRevision() { return 0; }

	virtual INT GetWidth() { return pMovieMaterial->USize; }
	virtual INT GetHeight()  { return pMovieMaterial->VSize; }
	virtual INT GetFirstMip()  { return 0; }
	virtual INT GetNumMips() { return 1; }
	virtual ETextureFormat GetFormat() { return TEXF_RGBA8; }

	virtual ETexClampMode GetUClamp() { return TC_Clamp; }
	virtual ETexClampMode GetVClamp() { return TC_Clamp; }
};

/* scion ======================================================================
 * UMovieMaterial::GetRenderInterface
 * Author: jg
 *
 * Returns the FMovieTexture pointer for this object. Constructs one if needed.
 *
 * ============================================================================
 */
FBaseTexture* UMovieMaterial::GetRenderInterface(void)
{
	// Allocate a new render interface if needed
	if (RenderInterface == NULL)
	{
		RenderInterface =
			(INT)NEW FMovieTexture(this);
	}
	return (FBaseTexture*)RenderInterface;
}

/* scion ======================================================================
 * UMovieMaterial::Destroy
 * Author: jg
 *
 * Calls the common clean up method upon object deletion
 *
 * ============================================================================
 */
void UMovieMaterial::Destroy(void)
{
	guard(UMovieMaterial::Destroy);
	// scion sz - make sure to free resources when destroyed
	Dispose();
	// Call UObject's Destroy so we don't get the "failed to route destroy" msg
	Super::Destroy();
	unguard;
}

/**
 * Common method for freeing all allocated memory
 */
void UMovieMaterial::Dispose(void)
{
	guard(UMovieMaterial::Dispose);
	// Clean up the thread before anything else
	if (Thread != NULL)
	{
		FRunnableThread* pThread = NULL;
		{
			check(ThreadSynch != NULL);
			// Synchronize thread access
			FScopeLock sl((FSynchronize*)ThreadSynch);
			pThread = (FRunnableThread*)Thread;
		}
		if (pThread != NULL)
		{
			// The thread will update the Thread value
			pThread->Kill(TRUE,250);
		}
		// This thread will zero this field on exit, so verify
		check(Thread == NULL);
	}
	// Destroy the render interface if it exists
	if (RenderInterface != NULL)
	{
		delete ((FBaseTexture*)RenderInterface);
		RenderInterface = NULL;
	}
	// Now destroy the decoder if it exists
	if (MovieDecoder != NULL)
	{
		delete (FMoviePlayer*)MovieDecoder;
		MovieDecoder = NULL;
	}
	if (ThreadSynch != NULL)
	{
		// Delete the synchronization object
		GSynchronizeFactory->Destroy((FSynchronize*)ThreadSynch);
		ThreadSynch = NULL;
	}
	if (InMemoryDecodingBlock != NULL)
	{
		delete ((BYTE*)InMemoryDecodingBlock);
	}
	unguard;
}

/* scion ======================================================================
 * UMovieMaterial::execDispose
 * Author: jg
 *
 * This is a script callable wrapper around the common cleanup method
 *
 * ============================================================================
 */
void UMovieMaterial::execDispose(FFrame &Stack,RESULT_DECL)
{
	P_FINISH;
	Dispose();
}

/**
 * This is a script callable wrapper around the common loading method
 */
void UMovieMaterial::execStartStreaming(FFrame &Stack,RESULT_DECL)
{
	P_FINISH;
#ifdef _XBOX
	if (bDecodeFromMemory == 1)
	{
		LoadInMemoryMovie();
	}
#else
	debugf(TEXT("Can't stream movie on the PC"));
#endif
}

/**
 * This is a script callable wrapper around the common method for stopping
 */
void UMovieMaterial::execStopStreaming(FFrame &Stack,RESULT_DECL)
{
	P_FINISH;
#ifdef _XBOX
	if (bDecodeFromMemory == 1)
	{
		StopLoadOfInMemoryMovie();
	}
#else
	debugf(TEXT("Can't stop streaming movie on the PC"));
#endif
}

/**
 * Allocates any threaded vars and starts the background loading process
 */
void UMovieMaterial::LoadInMemoryMovie(void)
{
	guard(UMovieMaterial::LoadInMemoryMovie);
	// Only do this once no matter how many times it is called
	if (ThreadSynch == NULL)
	{
		// Create a synch object so the two threads can safely look at vars
		ThreadSynch = (INT)GSynchronizeFactory->CreateCriticalSection();
		check(ThreadSynch);
		bDoneBackgroundLoading = FALSE;
		// Create our runnable
		FRunnable* BackgroundTask = NEW FBackgroundMovieLoading(this);
		check(BackgroundTask);
		// Create the thread as autodelete
		FRunnableThread* pThread = GThreadFactory->CreateThread(BackgroundTask,
			FALSE,FALSE);
//			TRUE,TRUE);
		check(pThread);
		Thread = (INT)pThread;
	}
	unguard;
}

/**
 * Stops the loading of the movie if still active
 */
void UMovieMaterial::StopLoadOfInMemoryMovie(void)
{
	guard(UMovieMaterial::StopLoadOfInMemoryMovie);
	FRunnableThread* pThread = NULL;
	{
		// Lock the thread var for the duration of access
		FScopeLock sl((FSynchronize*)ThreadSynch);
		pThread = (FRunnableThread*)Thread;
	}
	if (pThread != NULL)
	{
		pThread->Kill();
	}
	unguard;
}

/**
 * Opens the file for the movie that is to be loaded. Figures out the size
 * of the movie.
 *
 * @return True if initialization was successful, false otherwise
 */
UBOOL UMovieMaterial::FBackgroundMovieLoading::Init(void)
{
	check(Movie && Movie->ThreadSynch);
	FScopeLock sl((FSynchronize*)Movie->ThreadSynch);
	// Make sure we don't need to abort before we start
	if (Movie->bDoneBackgroundLoading == FALSE)
	{
		// Try to open the movie file
		File = GFileManager->CreateFileReader(*Movie->MovieFileName);
		if (File != NULL)
		{
			AmountLeft = (DWORD)File->TotalSize();
			// Allocate the buffer for holding the full movie size
			Movie->InMemoryDecodingBlock = (INT) NEW BYTE[AmountLeft];
			check(Movie->InMemoryDecodingBlock);
			// Store the size for the movie player
			Movie->InMemoryBlockSize = AmountLeft;
			debugf(TEXT("Background movie opened %s size %d"),
				*Movie->MovieFileName,AmountLeft);
		}
	}
	return File != NULL;
}

/**
 * Loops through checking for a stop request or completion of the reading
 */
DWORD UMovieMaterial::FBackgroundMovieLoading::Run(void)
{
	guard(UMovieMaterial::FBackgroundMovieLoading::Run);
	UBOOL bDone = 0;
	LOCAL_CLOCK(LoadTime);
	// While we aren't done
	while (bDone == FALSE)
	{
		UBOOL bShouldBail = FALSE;
		{
			// Lock thread access
			FScopeLock sl((FSynchronize*)Movie->ThreadSynch);
			bShouldBail = bRequestingExit;			
		}
		// If we have more to stream in
		if (bShouldBail == FALSE && AmountLeft > 0)
		{
			// Determine how much to read this time
			INT ReadAmount = AmountLeft < 4096 ? AmountLeft : 4096;
			// There is no thread synch on this as it should not be touched
			BYTE* Buffer = (BYTE*)Movie->InMemoryDecodingBlock;
			// Read the block in
			File->Serialize((void*)&Buffer[AmountRead],ReadAmount);
			// Update our positions so we can track when we are done
			AmountRead += ReadAmount;
			AmountLeft -= ReadAmount;
			// Stop reading if we are done
			bDone = AmountLeft == 0;
		}
	}
	debugf(TEXT("Background movie finished loading...amount read %d"),
		AmountRead);
	debugf(TEXT("Background loading took %f seconds"),
		LOCAL_UNCLOCK(LoadTime) * GSecondsPerCycle);
	return 0;
	unguard;
}

/**
 * Tell the background process that we wish to abort reading.
 */
void UMovieMaterial::FBackgroundMovieLoading::Stop(void)
{
	guard(UMovieMaterial::FBackgroundMovieLoading::Stop);
	check(Movie->ThreadSynch);
	// Lock thread access
	FScopeLock sl((FSynchronize*)Movie->ThreadSynch);
	bRequestingExit = TRUE;
	unguard;
}

/**
 * Tells the movie material that it's done loading. It does this in a thread
 * safe manner.
 */
void UMovieMaterial::FBackgroundMovieLoading::Exit(void)
{
	guard(UMovieMaterial::FBackgroundMovieLoading::Exit);
	check(Movie->ThreadSynch);
	// Lock thread access
	FScopeLock sl((FSynchronize*)Movie->ThreadSynch);
	// Mark all of our stuff complete
	Movie->bDoneBackgroundLoading = TRUE;
	// Zero out the thread so it doesn't get deleted twice
	Movie->Thread = NULL;
	// Delete the FArchive since we don't need it anymore
	delete File;
	unguard;
}

/* scion ======================================================================
 * UColorModifier::PreSetMaterial
 * Author: jg
 *
 * Determines whether it needs to update the color variable and animates the
 * color between the start and end range based upon material settings
 *
 * input:	TimeSeconds		The level time that has elapsed
 *
 * ============================================================================
 */
void UColorModifier::PreSetMaterial(FLOAT TimeSeconds)
{
	guard(UColorModifier::PreSetMaterial);
	// Update the contained material if valid
	if (Material != NULL)
	{
		Material->PreSetMaterial(TimeSeconds);
	}
	// If we need to change the color based upon time figure out which mode
	if (ColorInterpMode > CIM_None)
	{
		// Set the start time if needed
		if (LastTime == 0.f)
		{
			LastTime = TimeSeconds;
		}
		// Figure out the change in time
		FLOAT flDelta = TimeSeconds - LastTime;
		// Clamp this between zero and max time
		ElapsedTime = Clamp(ElapsedTime + (InterpolateDir * flDelta),0.f,
			InterpolationTime);
		// Figure out the alpha to use to blend between the colors
		FLOAT flAlpha = ElapsedTime / InterpolationTime;
		// Do a different color calculation based upon mode
		switch (ColorInterpMode)
		{
			case CIM_Cycle:
			{
				// Blend between the colors
				Color.Lerp(StartColor,EndColor,flAlpha);
				// Now wrap back around to zero if needed
				if (flAlpha >= 1.f)
				{
					ElapsedTime = 0.f;
				}
				break;
			}
			case CIM_ReverseAtEnd:
			{
				// Blend between the colors
				Color.Lerp(StartColor,EndColor,flAlpha);
				// Now flip the sign and have it go back through the colors
				if (flAlpha >= 1.f || flAlpha <= 0.f)
				{
					InterpolateDir *= -1.f;
				}
				break;
			}
			case CIM_RandomInRange:
			{
				// Adjust the color only at either end of the alpha
				if (flAlpha >= 1.f)
				{
					flAlpha = appFrand();
					// Blend between the colors
					Color.Lerp(StartColor,EndColor,flAlpha);
					// Restart the timer
					ElapsedTime = 0.f;
				}
				break;
			}
			case CIM_Once:
			{
				// Don't interpolate if we've reached the end
				if (flAlpha <= 1.f)
				{
					// Blend between the colors
					Color.Lerp(StartColor,EndColor,flAlpha);
				}
				break;
			}
		}
		// Set last update time
		LastTime = TimeSeconds;
	}
	unguard;
}


//scion rwd - custom precache for materials
void UMaterial::Precache(UObject *src,TMap<UObject*,BYTE> *masterPrecacheList)
{
#ifdef _XBOX
	((UGameEngine*)UEngine::GEngine)->GLevel->GetLevelInfo()->PrecacheMaterials.AddUniqueItem(this);
#endif
}

/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/
