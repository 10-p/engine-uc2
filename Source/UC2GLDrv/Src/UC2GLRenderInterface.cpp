/*=============================================================================
	UC2GLRenderInterface.cpp: UC2GLDrv's FRenderInterface.

	Structure from engine-ut2004's OpenGLDrv (OpenGLRenderInterface.cpp: saved state stack,
	deferred lights/matrices, CommitStreams, DrawPrimitive); behaviour from UC2's D3DDrv
	(D3DRenderInterface.cpp: UC2's state stacks, SetMaterial, light selection, stencil,
	colour write masks, what the PC build does with the post-process interface).

	How D3D state becomes GL state (the rules every function below follows):
	- Clip space: the engine's projections are D3D's (z in [0,1], pixel centres on integer
	  coordinates). CommitMatrices appends a fix-up so GL rasterises exactly like D3D:
	  z' = 2z - w, a half-pixel shift, and a vertical flip for render-target textures so they
	  are stored top row first as D3D's are (the default target is not flipped: Present
	  draws it straight).
	- Culling and front faces follow from that: D3D's CW is the GL back face; flipped targets
	  swap the two.
	- Viewports are D3D rectangles (origin top left), converted per target.
	- Texture stages are D3D8 stages (UC2GLD3DTypes.h) translated to texture_env_combine
	  (+ ATI_texture_env_combine3 for the three-operand ops) in SetMaterialBlending.
	- Vertex colours are D3DCOLOR; the resources swizzle them on upload.
=============================================================================*/

#include "UC2GLDrv.h"

//
// Log a message once per call site (R1 diagnostics for paths that are deferred).
//
#define UC2GL_LOG_ONCE(...) { static UBOOL Logged = 0; if( !Logged ) { Logged = 1; debugf( NAME_Init, __VA_ARGS__ ); } }

/*-----------------------------------------------------------------------------
	FOpenGLRenderInterface::FOpenGLSavedState.
-----------------------------------------------------------------------------*/

FOpenGLRenderInterface::FOpenGLSavedState::FOpenGLSavedState()
{
	ZBias				= 0;
	ColorWriteMask		= CWM_COLORWRITEENABLE_COLORONLY;

	LocalToWorld		= FMatrix::Identity;
	WorldToCamera		= FMatrix::Identity;
	CameraToScreen		= FMatrix::Identity;

	VertexShader		= NULL;
	appMemzero(Streams,sizeof(Streams));
	appMemzero(StreamStrides,sizeof(StreamStrides));
	NumStreams			= 0;

	IndexBuffer			= NULL;
	IndexBufferBase		= 0;

	CullMode			= CM_None;

	UseDetailTexturing	= 1;
	UseDynamicLighting	= 0;
	UseStaticLighting	= 1;
	LightingModulate2X	= 0;
	LightingOnly		= 0;
	Lightmap			= NULL;
	AmbientLightColor	= FColor(0,0,0,0);
	LitSphere			= FSphere(FVector(0,0,0),0);

	DistanceFogEnabled	= 0;
	DistanceFogStart	= 0.0f;
	DistanceFogEnd		= 0.0f;
	DistanceFogColor	= FColor(0,0,0,0);

	appMemzero(MaterialPasses,sizeof(MaterialPasses));
	NumMaterialPasses	= 0;
	CurrentMaterialState= NULL;
	LastMaterial		= NULL;

	VertexShaderEnum	= 0;
	ForceDefSphereMap	= 0;
}

/*-----------------------------------------------------------------------------
	FOpenGLRenderInterface.
-----------------------------------------------------------------------------*/

FOpenGLRenderInterface::FOpenGLRenderInterface(UUC2GLRenderDevice* InRenDev)
{
	RenDev			= InRenDev;
	Viewport		= NULL;
	PrecacheMode	= PRECACHE_None;

	SavedStateIndex	= 0;
	CurrentState	= &SavedStates[SavedStateIndex];

	appMemzero( LightTypeTotals, sizeof(LightTypeTotals) );
	appMemzero( Lights, sizeof(Lights) );
	appMemzero( PixelShaderConstants, sizeof(PixelShaderConstants) );
	AmbientColor		= FColor(0,0,0,0);
	LightsDirty			= 1;
	MatricesDirty		= 1;
	StencilEnabled		= 0;
	DepthEnabled		= 1;

	BackBufferCopyTarget	= NULL;
	PostProcessStage		= PP_STAGE_NONE;

	DynamicGamma			= NULL;
	appMemzero( DistortionScale, sizeof(DistortionScale) );
	appMemzero( ColorRemapConstant, sizeof(ColorRemapConstant) );
	PointSampleNudge		= 0;
	InOcclusionQuery		= 0;
	WireIndexBuffer			= 0;
}

FOpenGLRenderInterface::~FOpenGLRenderInterface()
{
}

//
// Forget what is bound in GL: the next draw re-applies the material pass.
//
void FOpenGLRenderInterface::InvalidateHardwareState()
{
	CurrentState->CurrentMaterialState = NULL;
}

//
//	FOpenGLRenderInterface::Locked
//
void FOpenGLRenderInterface::Locked( UViewport* InViewport, BYTE* InHitData, INT* InHitSize )
{
	guard(FOpenGLRenderInterface::Locked);

	Viewport = InViewport;

	// The default target: the device's main framebuffer.
	SavedRenderTargetState.Reset();
	FOpenGLRenderTargetState* RTState = SavedRenderTargetState.GetCurrentState();
	RTState->RenderTarget	= RenDev->DefaultRenderTarget;
	RTState->Framebuffer	= RenDev->MainFramebuffer;
	RTState->Width			= RenDev->MainWidth;
	RTState->Height			= RenDev->MainHeight;
	RTState->Flipped		= 0;
	RenDev->glBindFramebuffer( GL_FRAMEBUFFER, RTState->Framebuffer );

	SavedStencilState.Reset();
	SavedViewportState.Reset();

	// Setup the initial state (scion sz - added offsetX/Y).
	SetViewport( Viewport->OffsetX, Viewport->OffsetY, Viewport->SizeX, Viewport->SizeY );

	ApplyStencilState();
	if( StencilEnabled )
		RenDev->glEnable( GL_STENCIL_TEST );
	else
		RenDev->glDisable( GL_STENCIL_TEST );

	// Save the render state.
	SavedStateIndex = 0;
	CurrentState = &SavedStates[0];
	PushState();

	InvalidateHardwareState();
	LightsDirty		= 1;
	MatricesDirty	= 1;

	unguard;
}

//
//	FOpenGLRenderInterface::Unlocked
//
void FOpenGLRenderInterface::Unlocked()
{
	guard(FOpenGLRenderInterface::Unlocked);

	// scion jg -- Restore viewport state
	ResetViewport();

	// Restore the initial state.
	PopState();

	// Leave the main framebuffer bound (a render target left set would be an engine bug;
	// D3DDrv only checks it in debug builds).
	if( SavedRenderTargetState.GetStateIndex() != 0 )
	{
		UC2GL_LOG_ONCE( TEXT("UC2GLDrv: render target stack not empty at Unlock (%i)"), SavedRenderTargetState.GetStateIndex() );
		SavedRenderTargetState.Reset();
	}
	RenDev->glBindFramebuffer( GL_FRAMEBUFFER, RenDev->MainFramebuffer );

	Viewport = NULL;

	unguard;
}

//
//	FOpenGLRenderInterface::PushState
//
void FOpenGLRenderInterface::PushState()
{
	guard(FOpenGLRenderInterface::PushState);

	check(SavedStateIndex+1 < MAX_STATESTACKDEPTH);

	CurrentState = &SavedStates[SavedStateIndex+1];
	appMemcpy( CurrentState, &SavedStates[SavedStateIndex], sizeof(FOpenGLSavedState) );
	SavedStateIndex++;

	for(INT PassIndex = 0;PassIndex < CurrentState->NumMaterialPasses;PassIndex++)
		CurrentState->MaterialPasses[PassIndex]->NumRefs++;

	unguard;
}

//
//	FOpenGLRenderInterface::PopState
//
void FOpenGLRenderInterface::PopState()
{
	guard(FOpenGLRenderInterface::PopState);

	check( SavedStateIndex > 0 );

	FOpenGLSavedState&	OldState	= SavedStates[SavedStateIndex--];
	FOpenGLSavedState&	NewState	= SavedStates[SavedStateIndex];
	CurrentState = &NewState;

	// Matrices, cull mode, z bias, fog and ambient come back with the state (the D3D
	// driver re-sets them in its deferred state).
	MatricesDirty = 1;
	if( OldState.ZBias != NewState.ZBias )
		RenDev->glPolygonOffset( 0.0f, -(FLOAT)NewState.ZBias );
	ApplyCullMode( NewState.CullMode );
	if( NewState.DistanceFogEnabled )
	{
		FPlane FogColor = NewState.DistanceFogColor.Plane();
		RenDev->glEnable( GL_FOG );
		RenDev->glFogf( GL_FOG_START, NewState.DistanceFogStart );
		RenDev->glFogf( GL_FOG_END, NewState.DistanceFogEnd );
		RenDev->glFogfv( GL_FOG_COLOR, (GLfloat*)&FogColor );
	}
	else
		RenDev->glDisable( GL_FOG );
	AmbientColor = NewState.AmbientLightColor;

	// scion jg -- Clear any lights that are set
	for( INT LightIndex=0; LightIndex<8; LightIndex++ )
		Lights[LightIndex].Enabled = 0;
	LightsDirty = 1;

	// Restore material state.
	for(INT PassIndex = 0;PassIndex < OldState.NumMaterialPasses;PassIndex++)
	{
		if(--OldState.MaterialPasses[PassIndex]->NumRefs == 0)
			MaterialStatePool.FreeState(OldState.MaterialPasses[PassIndex]);
		OldState.MaterialPasses[PassIndex] = NULL;
	}

	CurrentState->CurrentMaterialState = NULL;

	unguard;
}

/*-----------------------------------------------------------------------------
	Render targets and viewports.
-----------------------------------------------------------------------------*/

//
//	FOpenGLRenderInterface::SetRenderTarget
//
UBOOL FOpenGLRenderInterface::SetRenderTarget(FRenderTarget* InRenderTarget)
{
	guard(FOpenGLRenderInterface::SetRenderTarget);

	FRenderTarget* UseRenderTarget = InRenderTarget ? InRenderTarget : RenDev->DefaultRenderTarget;

	GLuint	Framebuffer;
	INT		Width, Height;
	UBOOL	Flipped;

	if( UseRenderTarget == RenDev->DefaultRenderTarget )
	{
		Framebuffer	= RenDev->MainFramebuffer;
		Width		= RenDev->MainWidth;
		Height		= RenDev->MainHeight;
		Flipped		= 0;
	}
	else
	{
		FOpenGLTexture* GLTexture = NULL;
		_WORD CacheId = UseRenderTarget->GetCacheId();
		if( CacheId != INVALID_CACHEID )
			GLTexture = (FOpenGLTexture*)RenDev->GetCachedResource(CacheId,CH_Texture);
		if( GLTexture == NULL )
		{
			GLTexture = new FOpenGLTexture(RenDev,UseRenderTarget);
			RenDev->CacheResource(GLTexture,CH_Texture);
			UseRenderTarget->SetCacheId(GLTexture->CacheId,RenDev);
		}
		if( GLTexture->CachedRevision != UseRenderTarget->GetRevision() )
		{
			if( GLTexture->Cache(UseRenderTarget) )
				return 0;
		}
		if( !GLTexture->FramebufferID )
			return 0;
		GLTexture->LastFrameUsed = RenDev->FrameCounter;

		Framebuffer	= GLTexture->FramebufferID;
		Width		= GLTexture->CachedWidth;
		Height		= GLTexture->CachedHeight;
		Flipped		= 1;
	}

	// scion sz - if we are not using the default render target make sure to enable
	// color writes for everything including alpha
	if( UseRenderTarget == RenDev->DefaultRenderTarget )
		SetColorWriteMask( CWM_COLORWRITEENABLE_COLORONLY );
	else
		SetColorWriteMask( CWM_COLORWRITEENABLE_ALL );

	GLuint OldFramebuffer = SavedRenderTargetState.GetCurrentState()->Framebuffer;
	FOpenGLRenderTargetState* RTState = SavedRenderTargetState.Push();
	RTState->RenderTarget	= InRenderTarget;
	RTState->Framebuffer	= Framebuffer;
	RTState->Width			= Width;
	RTState->Height			= Height;
	RTState->Flipped		= Flipped;
	if( OldFramebuffer != Framebuffer )
		RenDev->glBindFramebuffer( GL_FRAMEBUFFER, Framebuffer );

	// Push the new viewport state too.
	SetViewport( 0, 0, Width, Height );
	InvalidateHardwareState();

	return 1;

	unguard;
}

//
//	FOpenGLRenderInterface::ResetRenderTarget
//
void FOpenGLRenderInterface::ResetRenderTarget(void)
{
	guard(FOpenGLRenderInterface::ResetRenderTarget);

	GLuint OldFramebuffer = SavedRenderTargetState.GetCurrentState()->Framebuffer;
	SavedRenderTargetState.Pop();
	FOpenGLRenderTargetState* NewState = SavedRenderTargetState.GetCurrentState();

	SetColorWriteMask( NewState->RenderTarget == RenDev->DefaultRenderTarget ? CWM_COLORWRITEENABLE_COLORONLY : CWM_COLORWRITEENABLE_ALL );

	if( NewState->Framebuffer != OldFramebuffer )
		RenDev->glBindFramebuffer( GL_FRAMEBUFFER, NewState->Framebuffer );

	ResetViewport();
	InvalidateHardwareState();

	unguard;
}

FRenderTarget* FOpenGLRenderInterface::GetRenderTarget()
{
	return SavedRenderTargetState.GetCurrentState()->RenderTarget;
}

UBOOL FOpenGLRenderInterface::IsDefaultRenderTarget(void)
{
	return SavedRenderTargetState.GetCurrentState()->RenderTarget == RenDev->DefaultRenderTarget;
}

//
// The current viewport rectangle (D3D: origin top left) in the current target's GL
// window coordinates.
//
void FOpenGLRenderInterface::ApplyViewport()
{
	FOpenGLRenderTargetState*	RTState = SavedRenderTargetState.GetCurrentState();
	FOpenGLViewportState*		VPState = SavedViewportState.GetCurrentState();

	INT	X = VPState->ViewportX,
		W = VPState->ViewportWidth,
		H = VPState->ViewportHeight,
		Y = RTState->Flipped ? VPState->ViewportY : RTState->Height - VPState->ViewportY - H;

	RenDev->glViewport( X, Y, W, H );
	RenDev->glScissor( X, Y, W, H );
	MatricesDirty = 1;
}

void FOpenGLRenderInterface::SetViewport(INT X,INT Y,INT Width,INT Height)
{
	guard(FOpenGLRenderInterface::SetViewport);
	FOpenGLViewportState* VPState = SavedViewportState.Push();
	VPState->ViewportX		= X;
	VPState->ViewportY		= Y;
	VPState->ViewportWidth	= Width;
	VPState->ViewportHeight	= Height;
	ApplyViewport();
	unguard;
}

// scion sz - simple Xbox only interface for screen space offset
void FOpenGLRenderInterface::SetScreenSpaceOffset( FLOAT OffsetX, FLOAT OffsetY )
{
}

void FOpenGLRenderInterface::ResetViewport(void)
{
	guard(FOpenGLRenderInterface::ResetViewport);
	SavedViewportState.Pop();
	ApplyViewport();
	unguard;
}

void FOpenGLRenderInterface::GetViewport(INT* X,INT* Y,INT* Width,INT* Height)
{
	FOpenGLViewportState* VPState = SavedViewportState.GetCurrentState();
	if( X )			*X = VPState->ViewportX;
	if( Y )			*Y = VPState->ViewportY;
	if( Width )		*Width = VPState->ViewportWidth;
	if( Height )	*Height = VPState->ViewportHeight;
}

//
//	FOpenGLRenderInterface::Clear
//
void FOpenGLRenderInterface::Clear(UBOOL UseColor,FColor Color,UBOOL UseDepth,FLOAT Depth,UBOOL UseStencil,DWORD Stencil)
{
	guard(FOpenGLRenderInterface::Clear);

	// D3D's Clear clears the viewport rectangle and ignores the write masks; GL's honours
	// the scissor (= viewport) and the masks, so open the masks for it.
	GLbitfield ClearFlags = 0;
	if( UseColor )
	{
		RenDev->glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
		RenDev->glClearColor( Color.R / 255.f, Color.G / 255.f, Color.B / 255.f, Color.A / 255.f );
		ClearFlags |= GL_COLOR_BUFFER_BIT;
	}
	if( UseDepth )
	{
		RenDev->glDepthMask( GL_TRUE );
		RenDev->glClearDepth( Depth );
		ClearFlags |= GL_DEPTH_BUFFER_BIT;
	}
	if( UseStencil && (RenDev->UseStencil || GIsEditor) )
	{
		RenDev->glStencilMask( 0xff );
		RenDev->glClearStencil( Stencil & 0xff );
		ClearFlags |= GL_STENCIL_BUFFER_BIT;
	}
	if( ClearFlags )
		RenDev->glClear( ClearFlags );

	// M7: copies of the main framebuffer are stale now (UC2GLPostProcess.cpp).
	if( UseColor && SavedRenderTargetState.GetCurrentState()->Framebuffer == RenDev->MainFramebuffer )
		RenDev->MainColorVersion++;
	if( UseDepth )
		RenDev->MainDepthVersion++;

	// Masks come back with the next pass and the stencil state.
	ApplyStencilState();
	InvalidateHardwareState();

	unguard;
}

void FOpenGLRenderInterface::SetColorWriteMask( DWORD ColorMask )
{
	CurrentState->ColorWriteMask = ColorMask;
}

//
// scion jg -- Allows the direct setting of a renderstate bypassing the deferred commit.
// Only the Xbox push-buffer paths call it (D3DCULLMODE in UnRenderStaticMesh.cpp).
//
void FOpenGLRenderInterface::SetRenderStateDirect(BYTE State,DWORD Value)
{
	if( State == 147 /* D3DCULLMODE, as UnRenderStaticMesh.cpp defines it */ )
		ApplyCullMode( (ECullMode)Value );
	else
		UC2GL_LOG_ONCE( TEXT("UC2GLDrv: SetRenderStateDirect(%i) ignored (Xbox-only path)"), (INT)State );
}

//
// Vertex shader constants: PC's fixed-function path has no use for them.
// M6: the spliced GLSL vertex shaders take these as uniforms.
//
void FOpenGLRenderInterface::SetVertexShaderConstant(INT Register,const void* pConstantData,DWORD ConstantCount)
{
}

void FOpenGLRenderInterface::SetPixelShaderConstant( INT Register, const void* Data, INT Count )
{
	for( INT i=0; i<Count && Register+i < 8; i++ )
		appMemcpy( PixelShaderConstants[Register+i], (const FLOAT*)Data + i*4, sizeof(FLOAT)*4 );
}

// Hit testing is editor-only (D3DDrv reads back pixels); the game never hits.
void FOpenGLRenderInterface::PushHit(const BYTE* Data,INT Count)
{
}

void FOpenGLRenderInterface::PopHit(INT Count,UBOOL Force)
{
}

/*-----------------------------------------------------------------------------
	Culling, lighting, fog, transforms.
-----------------------------------------------------------------------------*/

//
// D3D's CW (cull clockwise faces as seen on screen) is GL's back face with GL_CCW front
// faces; a flipped (render-target) image mirrors the winding.
//
void FOpenGLRenderInterface::ApplyCullMode( ECullMode CullMode )
{
	if( CullMode == CM_None )
	{
		RenDev->glDisable( GL_CULL_FACE );
		return;
	}
	UBOOL CullCW = (CullMode == CM_CW);
	if( SavedRenderTargetState.GetCurrentState()->Flipped )
		CullCW = !CullCW;
	RenDev->glEnable( GL_CULL_FACE );
	RenDev->glFrontFace( GL_CCW );
	RenDev->glCullFace( CullCW ? GL_BACK : GL_FRONT );
}

void FOpenGLRenderInterface::SetCullMode(ECullMode CullMode)
{
	guard(FOpenGLRenderInterface::SetCullMode);
	CurrentState->CullMode = CullMode;
	ApplyCullMode( CullMode );
	unguard;
}

ECullMode FOpenGLRenderInterface::GetCullMode()
{
	return CurrentState->CullMode;
}

void FOpenGLRenderInterface::SetAmbientLight(FColor Color)
{
	CurrentState->AmbientLightColor = Color;
	AmbientColor = Color;
	LightsDirty = 1;
}

void FOpenGLRenderInterface::EnableLighting(UBOOL UseDynamic, UBOOL UseStatic, UBOOL Modulate2X, FBaseTexture* LightmapTexture, UBOOL LightingOnly, const FSphere& LitSphere )
{
	guard(FOpenGLRenderInterface::EnableLighting);

	FOpenGLTexture* Lightmap = NULL;
	if( LightmapTexture )
		Lightmap = CacheTexture( LightmapTexture );

	CurrentState->Lightmap				= Lightmap;
	CurrentState->LightingOnly			= LightingOnly;
	CurrentState->UseDynamicLighting	= UseDynamic;
	CurrentState->UseStaticLighting		= UseStatic;
	CurrentState->LightingModulate2X	= Modulate2X;
	CurrentState->LitSphere				= LitSphere;
	LightsDirty = 1;

	unguard;
}

//
//	UnrealAttenuation (UC2: the 2X removed from the distance attenuation).
//
static FLOAT UnrealAttenuation(FLOAT Distance,FLOAT Radius)
{
	if(Distance <= Radius)
	{
		FLOAT	A = Distance / Radius,
				B = (2 * A * A * A - 3 * A * A + 1);
		return B;
	}
	else
		return 0.0f;
}

//
//	FOpenGLRenderInterface::SetLight: FD3DRenderInterface::SetLight. On PC every light ends up
//	directional (VS_Lights_Point_MAX is 0): sun/directional lights as such, the others as a
//	direction from the lit object scaled by the light's attenuation there.
//
void FOpenGLRenderInterface::SetLight(INT LightIndex,FDynamicLight* Light,FLOAT Scale)
{
	guard(FOpenGLRenderInterface::SetLight);

	// scion sz - ambient light type for adding to ambient term
	if( Light && Light->LightEffect == LE_Ambient )
	{
		// D3DDrv writes CurrentState->AmbientLightColor + FColor(...) to RS_AMBIENT: FColor has no
		// operator+, so MSVC added the two colours as DWORDs (carries between channels
		// included). Same here.
		AmbientColor = FColor( CurrentState->AmbientLightColor.DWColor() + FColor(Light->Color * (Light->Alpha*0.5f*Scale)).DWColor() );
		LightsDirty = 1;
		return;
	}

	if( LightIndex < 0 || LightIndex >= 8 )
		return;

	UBOOL bEnableLight = 0;
	if( Light )
	{
		if( LightIndex == 0 )
			appMemzero( LightTypeTotals, sizeof(LightTypeTotals) );

		INT TotalDirLts		= GetLightTypeCount(D3DLIGHT_DIRECTIONAL);
		INT TotalPointLts	= GetLightTypeCount(D3DLIGHT_POINT);
		static INT MaxDirLts = VS_Lights_Directional_MAX;
		static INT MaxPtLts  = VS_Lights_Point_MAX;

		if( CurrentState->LightingModulate2X )
			Scale *= 0.5f;

		if( TotalDirLts < MaxDirLts || TotalPointLts < MaxPtLts )
		{
			FOpenGLLight* LightGL = &Lights[LightIndex];
			appMemzero( LightGL, sizeof(FOpenGLLight) );

			BYTE LightEffect = Light->LightEffect;

			if( LightEffect == LE_Sunlight || LightEffect == LE_Directional )
			{
				if( TotalDirLts < MaxDirLts )
				{
					LightGL->Type		= D3DLIGHT_DIRECTIONAL;
					LightGL->Direction	= Light->Direction;
					if( LightEffect == LE_Sunlight )
						Scale *= 1.75f;
					bEnableLight = 1;
				}
			}
			else if( TotalPointLts < MaxPtLts )
			{
				LightGL->Position = Light->Position;

				if( LightEffect == LE_QuadraticNonIncidence || CurrentState->LitSphere.W == -1.0f )
				{
					LightGL->Type			= D3DLIGHT_POINT;
					LightGL->Attenuation0	= 0.0f;
					LightGL->Attenuation1	= 0.0f;
					LightGL->Attenuation2	= 8.0f / Square(Light->Radius);
					LightGL->Range			= Light->Radius;
				}
				else
				{
					FLOAT	CenterDistance = Max(0.1f,(CurrentState->LitSphere - Light->Position).Size()),
							MaxDistance = Clamp(CenterDistance + CurrentState->LitSphere.W,Light->Radius * 0.05f,Light->Radius * 0.9f),
							MinDistance = Clamp(CenterDistance - CurrentState->LitSphere.W,Light->Radius * 0.1f,Light->Radius * 0.95f),
							MinAttenuation = 1.0f / UnrealAttenuation(MinDistance,Light->Radius),
							MaxAttenuation = 1.0f / UnrealAttenuation(MaxDistance,Light->Radius);

					if( Abs(MinAttenuation - MaxAttenuation) < SMALL_NUMBER )
					{
						LightGL->Attenuation0 = MinAttenuation;
						LightGL->Attenuation1 = 0.0f;
					}
					else
					{
						LightGL->Attenuation0 = Max(0.01f,MinAttenuation - (MaxAttenuation - MinAttenuation) / (MaxDistance - MinDistance) * MinDistance);
						LightGL->Attenuation1 = Max(0.0f,(MinAttenuation - LightGL->Attenuation0) / MinDistance);
					}
					LightGL->Attenuation2	= 0.0f;
					LightGL->Range			= (256.0f - LightGL->Attenuation0) / Max(0.01f,LightGL->Attenuation1);

					if( LightEffect == LE_StaticSpot || LightEffect == LE_Spotlight )
					{
						LightGL->Type		= D3DLIGHT_SPOT;
						LightGL->Direction	= Light->Direction;
						LightGL->Phi		= 1.15f * appAcos( Square( 1.f - (FLOAT)(Light->LightCone)/256.f ) );
						LightGL->Theta		= 0.05f * LightGL->Phi;
						LightGL->Falloff	= 1.00f;
					}
					else
						LightGL->Type		= D3DLIGHT_POINT;
				}
				LightGL->Attenuation0 = Max( 0.f, LightGL->Attenuation0 );
				LightGL->Attenuation1 = Max( 0.f, LightGL->Attenuation1 );
				LightGL->Attenuation2 = Max( 0.f, LightGL->Attenuation2 );
				bEnableLight = 1;
			}
			// scion sz - try to approximate this light with just a directional
			else if( TotalDirLts < MaxDirLts )
			{
				LightGL->Type		= D3DLIGHT_DIRECTIONAL;
				LightGL->Direction	= FVector( CurrentState->LitSphere.X - Light->Position.X, CurrentState->LitSphere.Y - Light->Position.Y, CurrentState->LitSphere.Z - Light->Position.Z );
				// scale light color by dist attenuation amount
				Scale *= Light->SampleLightAtten( CurrentState->LitSphere );
				bEnableLight = 1;
			}

			if( bEnableLight )
			{
				LightGL->Diffuse = FPlane( Light->Color.X * Light->Alpha * Scale, Light->Color.Y * Light->Alpha * Scale, Light->Color.Z * Light->Alpha * Scale, 1.0f );
				IncrementLightType( LightGL->Type );
			}
		}
	}

	Lights[LightIndex].Enabled = bEnableLight;
	LightsDirty = 1;

	unguard;
}

void FOpenGLRenderInterface::SetGlobalColor(FColor Color)
{
	guard(FOpenGLRenderInterface::SetGlobalColor);

	FOpenGLMaterialState* Pass = CurrentState->MaterialPasses[CurrentState->NumMaterialPasses-1];
	Pass->TFactorColor = Color.DWColor();

	// RS_TEXTUREFACTOR is global in D3D: update the units of the applied pass now.
	if( CurrentState->CurrentMaterialState )
	{
		FPlane TFactor = FColor(CurrentState->CurrentMaterialState->TFactorColor = Color.DWColor()).Plane();
		for( INT s=0; s<Max<INT>(1,CurrentState->CurrentMaterialState->StagesUsed); s++ )
		{
			RenDev->glActiveTexture( GL_TEXTURE0 + s );
			RenDev->glTexEnvfv( GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, (GLfloat*)&TFactor );
		}
	}

	unguard;
}

void FOpenGLRenderInterface::SetNPatchTesselation( FLOAT Tesselation )
{
	// N-patches are off on PC (UseNPatches=False) and do not exist in GL.
}

void FOpenGLRenderInterface::SetDistanceFog(UBOOL Enable,FLOAT FogStart,FLOAT FogEnd,FColor FogColor)
{
	guard(FOpenGLRenderInterface::SetDistanceFog);

	if( Enable )
	{
		CurrentState->DistanceFogEnabled	= 1;
		CurrentState->DistanceFogColor		= FogColor;

		// Workaround for drivers which don't handle fogstart == fogend correctly.
		if( Abs(FogStart - FogEnd) < 1.f )
			FogStart = Max( 0.f, FogEnd - 1.f );

		CurrentState->DistanceFogStart	= FogStart;
		CurrentState->DistanceFogEnd	= FogEnd;

		// Linear fog on the eye-space depth: D3D's W fog in world units (the PC driver's
		// table-fog path divides by FAR_CLIPPING_PLANE only to express the same in Z).
		FPlane Color = FogColor.Plane();
		RenDev->glEnable( GL_FOG );
		RenDev->glFogf( GL_FOG_START, FogStart );
		RenDev->glFogf( GL_FOG_END, FogEnd );
		RenDev->glFogfv( GL_FOG_COLOR, (GLfloat*)&Color );
	}
	else
	{
		CurrentState->DistanceFogEnabled = 0;
		RenDev->glDisable( GL_FOG );
	}

	unguard;
}

void FOpenGLRenderInterface::SetTransform(ETransformType Type,const FMatrix& Matrix)
{
	if( Type == TT_LocalToWorld )
		CurrentState->LocalToWorld = Matrix;
	else if( Type == TT_WorldToCamera )
	{
		CurrentState->WorldToCamera = Matrix;
		LightsDirty = 1;
	}
	else if( Type == TT_CameraToScreen )
		CurrentState->CameraToScreen = Matrix;
	MatricesDirty = 1;
}

void FOpenGLRenderInterface::SetZBias(INT ZBias)
{
	guard(FOpenGLRenderInterface::SetZBias);
	ZBias *= 5;
	if( CurrentState->ZBias != ZBias )
		RenDev->glPolygonOffset( 0.0f, -(FLOAT)ZBias );
	CurrentState->ZBias = ZBias;
	unguard;
}

/*-----------------------------------------------------------------------------
	Stencil and depth.
-----------------------------------------------------------------------------*/

static GLenum GLCompareFunc( ECompareFunction Func )
{
	switch( Func )
	{
	case CF_Never:			return GL_NEVER;
	case CF_Less:			return GL_LESS;
	case CF_Equal:			return GL_EQUAL;
	case CF_LessEqual:		return GL_LEQUAL;
	case CF_Greater:		return GL_GREATER;
	case CF_NotEqual:		return GL_NOTEQUAL;
	case CF_GreaterEqual:	return GL_GEQUAL;
	case CF_Always:
	default:				return GL_ALWAYS;
	}
}

static GLenum GLStencilOp( EStencilOp Op )
{
	switch( Op )
	{
	case SO_Zero:			return GL_ZERO;
	case SO_Replace:		return GL_REPLACE;
	case SO_IncrementSat:	return GL_INCR;
	case SO_DecrementSat:	return GL_DECR;
	case SO_Invert:			return GL_INVERT;
	case SO_Increment:		return GL_INCR_WRAP;
	case SO_Decrement:		return GL_DECR_WRAP;
	case SO_Keep:
	default:				return GL_KEEP;
	}
}

void FOpenGLRenderInterface::ApplyStencilState()
{
	FOpenGLStencilState* State = SavedStencilState.GetCurrentState();
	RenDev->glStencilFunc( GLCompareFunc(State->StencilTest), State->StencilRef & 0xff, State->StencilMask & 0xff );
	RenDev->glStencilOp( GLStencilOp(State->StencilFailOp), GLStencilOp(State->StencilZFailOp), GLStencilOp(State->StencilPassOp) );
	RenDev->glStencilMask( State->StencilWriteMask & 0xff );
}

void FOpenGLRenderInterface::SetStencilOp(ECompareFunction Test,DWORD Ref,DWORD Mask,EStencilOp FailOp,EStencilOp ZFailOp,EStencilOp PassOp,DWORD WriteMask)
{
	guard(FOpenGLRenderInterface::SetStencilOp);
	FOpenGLStencilState* State = SavedStencilState.Push();
	State->StencilTest		= Test;
	State->StencilRef		= Ref;
	State->StencilMask		= Mask;
	State->StencilFailOp	= FailOp;
	State->StencilZFailOp	= ZFailOp;
	State->StencilPassOp	= PassOp;
	State->StencilWriteMask	= WriteMask;
	ApplyStencilState();
	unguard;
}

void FOpenGLRenderInterface::ResetStencilOp(void)
{
	guard(FOpenGLRenderInterface::ResetStencilOp);
	SavedStencilState.Pop();
	ApplyStencilState();
	unguard;
}

void FOpenGLRenderInterface::EnableStencil(UBOOL bEnable)
{
	StencilEnabled = bEnable;
	if( bEnable )
		RenDev->glEnable( GL_STENCIL_TEST );
	else
		RenDev->glDisable( GL_STENCIL_TEST );
}

void FOpenGLRenderInterface::EnableDepth(UBOOL bEnable)
{
	DepthEnabled = bEnable;
	if( bEnable )
		RenDev->glEnable( GL_DEPTH_TEST );
	else
		RenDev->glDisable( GL_DEPTH_TEST );
	RenDev->glDepthMask( bEnable ? GL_TRUE : GL_FALSE );
}

void FOpenGLRenderInterface::SetPrecacheMode( EPrecacheMode InPrecacheMode )
{
	PrecacheMode = InPrecacheMode;
}

EPrecacheMode FOpenGLRenderInterface::GetPrecacheMode(void)
{
	return PrecacheMode;
}

/*-----------------------------------------------------------------------------
	Textures and materials.
-----------------------------------------------------------------------------*/

FOpenGLTexture* FOpenGLRenderInterface::CacheTexture(FBaseTexture* Texture)
{
	guard(FOpenGLRenderInterface::CacheTexture);

	if( !Texture )
		return NULL;

	_WORD CacheId = Texture->GetCacheId();
	FOpenGLTexture* GLTexture = NULL;
	if( CacheId != INVALID_CACHEID )
		GLTexture = (FOpenGLTexture*)RenDev->GetCachedResource(CacheId,CH_Texture);
	if( GLTexture == NULL )
	{
		GLTexture = new FOpenGLTexture(RenDev,Texture);
		RenDev->CacheResource(GLTexture,CH_Texture);
		Texture->SetCacheId(GLTexture->CacheId,RenDev);
	}

	if( GLTexture->CachedRevision != Texture->GetRevision() )
		GLTexture->Cache(Texture);

	GLTexture->LastFrameUsed = RenDev->FrameCounter;

	return GLTexture;

	unguard;
}

DWORD FOpenGLRenderInterface::GetStageCountForPass(INT Pass)
{
	if( CurrentState != NULL && Pass > 0 && Pass <= CurrentState->NumMaterialPasses )
		return CurrentState->MaterialPasses[Pass - 1]->StagesUsed;
	return 0;
}

//
//	FOpenGLRenderInterface::SetMaterial: FD3DRenderInterface::SetMaterial.
//
INT FOpenGLRenderInterface::SetMaterial(UMaterial* InMaterial, FString* ErrorString, UMaterial** ErrorMaterial, INT* NumPasses)
{
	guard(FOpenGLRenderInterface::SetMaterial);

	// Set default texture if Material is NULL
	if( !InMaterial || (PrecacheMode == PRECACHE_VertexBuffers) )
		InMaterial = Cast<UMaterial>(UMaterial::StaticClass()->GetDefaultObject())->DefaultMaterial;

	// keep track of last material set
	CurrentState->LastMaterial = InMaterial;

	// Release old material state.
	for(INT PassIndex = 0;PassIndex < CurrentState->NumMaterialPasses;PassIndex++)
	{
		if(--CurrentState->MaterialPasses[PassIndex]->NumRefs == 0)
			MaterialStatePool.FreeState(CurrentState->MaterialPasses[PassIndex]);
		CurrentState->MaterialPasses[PassIndex] = NULL;
	}

	CurrentState->NumMaterialPasses = 1;
	CurrentState->MaterialPasses[0] = MaterialStatePool.AllocateState(&DefaultPass);

	// Check for circular material references
	if( GIsEditor )
	{
		static TArray<UMaterial*> History;
		History.Empty();
		if( !InMaterial->CheckCircularReferences(History) )
		{
			INT ErrorIndex = History.Num()-1;
			if( ErrorIndex >= 0 )
			{
				if( ErrorMaterial ) *ErrorMaterial = History(ErrorIndex);
				if( ErrorString ) *ErrorString = FString::Printf(TEXT("Circular material reference in %s"), History(ErrorIndex)->GetName() );
			}
			CurrentState->NumMaterialPasses = 1;
			return CurrentState->NumMaterialPasses;
		}
	}

	if( CurrentState->LightingOnly )
	{
		SetLightingOnlyMaterial();
	}
	else
	{
		FOpenGLModifierInfo			ModifierInfo;
		UShader*					Shader;
		UBitmapMaterial*			BitmapMaterial;
		UConstantMaterial*			ConstantMaterial;
		UCombiner*					Combiner;
		UParticleMaterial*			ParticleMaterial;
		UTerrainMaterial*			TerrainMaterial;
		UProjectorMaterial*			ProjectorMaterial;
		UProjectorMultiMaterial*	ProjectorMultiMaterial;
		UMaterial*					NonModifier;
		UPixelShaderMaterial*		PixelShaderMaterial;
		UMultiPassMaterial*			MultiPassMaterial;
		UMovieMaterial*				pMovieMaterial;

		// Keep going until we have an renderable material.
		for(;;)
		{
			// Notify material it's being set.
			InMaterial->PreSetMaterial( Viewport->Actor->Level->TimeSeconds );

			UBOOL Result = 0;
			if( (NonModifier=PixelShaderMaterial=CheckMaterial<UPixelShaderMaterial,MRH_PixelShaderMaterial>(this, InMaterial, &ModifierInfo))!=NULL)
				Result = SetPixelShaderMaterial( PixelShaderMaterial, ModifierInfo, ErrorString, ErrorMaterial );
			else if( (NonModifier=Shader=CheckMaterial<UShader,MRH_Shader>(this, InMaterial, &ModifierInfo)) != NULL )
				Result = SetShaderMaterial( Shader, ModifierInfo, ErrorString, ErrorMaterial );
			else if( (NonModifier=Combiner=CheckMaterial<UCombiner,MRH_Combiner>(this, InMaterial, &ModifierInfo))!=NULL )
				Result = SetSimpleMaterial( Combiner, ModifierInfo, ErrorString, ErrorMaterial );
			else if( (NonModifier=ConstantMaterial=CheckMaterial<UConstantMaterial,MRH_ConstantMaterial>(this, InMaterial, &ModifierInfo))!=NULL )
				Result = SetSimpleMaterial( ConstantMaterial, ModifierInfo, ErrorString, ErrorMaterial );
			else if( (NonModifier=pMovieMaterial=CheckMaterial<UMovieMaterial,MRH_MovieMaterial>(this, InMaterial, &ModifierInfo))!=NULL)
				Result = SetMovieMaterial( pMovieMaterial, ModifierInfo, ErrorString, ErrorMaterial );
			else if( (NonModifier=BitmapMaterial=CheckMaterial<UBitmapMaterial,MRH_BitmapMaterial>(this, InMaterial, &ModifierInfo))!=NULL )
				Result = SetSimpleMaterial( BitmapMaterial, ModifierInfo, ErrorString, ErrorMaterial );
			else if( (NonModifier=TerrainMaterial=CheckMaterial<UTerrainMaterial,MRH_TerrainMaterial>(this, InMaterial, &ModifierInfo))!=NULL)
				Result = SetTerrainMaterial( TerrainMaterial, ModifierInfo, ErrorString, ErrorMaterial );
			else if( (NonModifier=ParticleMaterial=CheckMaterial<UParticleMaterial,MRH_ParticleMaterial>(this, InMaterial, &ModifierInfo))!=NULL)
				Result = SetParticleMaterial( ParticleMaterial, ModifierInfo, ErrorString, ErrorMaterial );
			else if( (NonModifier=ProjectorMultiMaterial=CheckMaterial<UProjectorMultiMaterial,MRH_ProjectorMultiMaterial>(this, InMaterial, &ModifierInfo))!=NULL)
				Result = SetProjectorMultiMaterial( ProjectorMultiMaterial, ModifierInfo, ErrorString, ErrorMaterial );
			else if( (NonModifier=ProjectorMaterial=CheckMaterial<UProjectorMaterial,MRH_ProjectorMaterial>(this, InMaterial, &ModifierInfo))!=NULL)
				Result = SetProjectorMaterial( ProjectorMaterial, ModifierInfo, ErrorString, ErrorMaterial );
			else if( (NonModifier=MultiPassMaterial=CheckMaterial<UMultiPassMaterial,MRH_MultiPassMaterial>(this, InMaterial, &ModifierInfo))!=NULL)
				Result = SetMultiPassMaterial( MultiPassMaterial, ModifierInfo, ErrorString, ErrorMaterial );
			else
				break;

			// Material is renderable.
			if( Result )
				break;

			// Use the default texture as a fallback.
			InMaterial = Cast<UMaterial>(UMaterial::StaticClass()->GetDefaultObject())->DefaultMaterial;

			// Clear any state we got part way through setting.
			for(INT PassIndex = 0;CurrentState->MaterialPasses[PassIndex];PassIndex++)
			{
				MaterialStatePool.FreeState(CurrentState->MaterialPasses[PassIndex]);
				CurrentState->MaterialPasses[PassIndex] = NULL;
			}
			CurrentState->MaterialPasses[0] = MaterialStatePool.AllocateState(&DefaultPass);
			CurrentState->NumMaterialPasses = 1;
		}
	}

	if( NumPasses )
		*NumPasses = CurrentState->NumMaterialPasses;

	CurrentState->CurrentMaterialState = NULL;

	// scion jg -- Return the number of passes this material needs
	return CurrentState->NumMaterialPasses;

	unguard;
}

UMaterial* FOpenGLRenderInterface::GetLastMaterial()
{
	return CurrentState->LastMaterial;
}

// PC: VS_FixedFunction for every material (D3DDrv returns before looking at it).
DWORD FOpenGLRenderInterface::GetMaterialVertexShaderType(UMaterial* Material)
{
	return VS_FixedFunction;
}

// The engine adds vertex shader fragments it wants; PC's fixed function ignores them.
void FOpenGLRenderInterface::AddVertexShaderEnum( QWORD VertexShaderEnum )
{
	CurrentState->VertexShaderEnum |= VertexShaderEnum;
}

//
// Material precaching: D3DDrv's interface default (nothing). Textures are cached on use.
//
void FOpenGLRenderInterface::PrecacheMaterial(UMaterial* Material)
{
}

/*-----------------------------------------------------------------------------
	Committing a material pass: D3D8 stages -> texture_env_combine.
-----------------------------------------------------------------------------*/

static GLenum GLBlendFactor( D3DBLEND Blend )
{
	switch( Blend )
	{
	case D3DBLEND_ZERO:			return GL_ZERO;
	case D3DBLEND_ONE:			return GL_ONE;
	case D3DBLEND_SRCCOLOR:		return GL_SRC_COLOR;
	case D3DBLEND_INVSRCCOLOR:	return GL_ONE_MINUS_SRC_COLOR;
	case D3DBLEND_SRCALPHA:		return GL_SRC_ALPHA;
	case D3DBLEND_INVSRCALPHA:	return GL_ONE_MINUS_SRC_ALPHA;
	case D3DBLEND_DESTALPHA:	return GL_DST_ALPHA;
	case D3DBLEND_INVDESTALPHA:	return GL_ONE_MINUS_DST_ALPHA;
	case D3DBLEND_DESTCOLOR:	return GL_DST_COLOR;
	case D3DBLEND_INVDESTCOLOR:	return GL_ONE_MINUS_DST_COLOR;
	case D3DBLEND_SRCALPHASAT:	return GL_SRC_ALPHA_SATURATE;
	default:					return GL_ONE;
	}
}

static GLenum GLBlendEquation( D3DBLENDOP Op )
{
	switch( Op )
	{
	case D3DBLENDOP_SUBTRACT:		return GL_FUNC_SUBTRACT;
	case D3DBLENDOP_REVSUBTRACT:	return GL_FUNC_REVERSE_SUBTRACT;
	case D3DBLENDOP_MIN:			return GL_MIN;
	case D3DBLENDOP_MAX:			return GL_MAX;
	case D3DBLENDOP_ADD:
	default:						return GL_FUNC_ADD;
	}
}

static GLint GLAddress( D3DTEXTUREADDRESS Address )
{
	switch( Address )
	{
	case D3DTADDRESS_WRAP:			return GL_REPEAT;
	case D3DTADDRESS_MIRROR:		return GL_MIRRORED_REPEAT;
	case D3DTADDRESS_BORDER:		return GL_CLAMP_TO_BORDER;
	case D3DTADDRESS_CLAMP:
	case D3DTADDRESS_MIRRORONCE:
	default:						return GL_CLAMP_TO_EDGE;
	}
}

//
// One GL combiner input: source and operand for a D3DTA_* argument.
//
struct FGLCombineArg
{
	GLint	Source,
			Operand;
};

static FGLCombineArg GLArg( DWORD Arg, UBOOL Alpha )
{
	FGLCombineArg Result;
	switch( Arg & D3DTA_SELECTMASK )
	{
	case D3DTA_DIFFUSE:		Result.Source = GL_PRIMARY_COLOR;	break;
	case D3DTA_TEXTURE:		Result.Source = GL_TEXTURE;			break;
	case D3DTA_TFACTOR:		Result.Source = GL_CONSTANT;		break;
	case D3DTA_SPECULAR:	Result.Source = GL_PRIMARY_COLOR;	break;	// no GL combiner source for COLOR1 (never used by UC2's PC stages)
	case D3DTA_TEMP:											// only with D3DPMISCCAPS_TSSARGTEMP, which we do not report
	case D3DTA_CURRENT:
	default:				Result.Source = GL_PREVIOUS;		break;	// unit 0's PREVIOUS is the primary colour, as D3D's stage 0 CURRENT is diffuse
	}
	UBOOL Complement	= (Arg & D3DTA_COMPLEMENT) != 0,
		  Replicate		= Alpha || (Arg & D3DTA_ALPHAREPLICATE) != 0;
	if( Replicate )
		Result.Operand = Complement ? GL_ONE_MINUS_SRC_ALPHA : GL_SRC_ALPHA;
	else
		Result.Operand = Complement ? GL_ONE_MINUS_SRC_COLOR : GL_SRC_COLOR;
	return Result;
}

static FGLCombineArg GLFixedArg( GLint Source, GLint Operand )
{
	FGLCombineArg Result;
	Result.Source	= Source;
	Result.Operand	= Operand;
	return Result;
}

//
// Translate one D3D texture stage operation (colour or alpha half) into
// GL_COMBINE_RGB/ALPHA, three sources with operands, and the scale.
//
static void TranslateStageOp( UUC2GLRenderDevice* RenDev, D3DTEXTUREOP Op, DWORD Arg0, DWORD Arg1, DWORD Arg2, UBOOL Alpha, GLint& Mode, FGLCombineArg* Src, FLOAT& Scale )
{
	Scale = 1.0f;
	Src[0] = GLArg( Arg1, Alpha );
	Src[1] = GLArg( Arg2, Alpha );
	Src[2] = GLArg( Arg0, Alpha );
	GLint OpA = Alpha ? GL_SRC_ALPHA : GL_SRC_COLOR;
	UBOOL Combine3 = RenDev->SUPPORTS_GL_ATI_texture_env_combine3;

	switch( Op )
	{
	case D3DTOP_SELECTARG1:
		Mode = GL_REPLACE;
		break;
	case D3DTOP_SELECTARG2:
		Mode = GL_REPLACE;
		Src[0] = Src[1];
		break;
	case D3DTOP_MODULATE4X:
		Scale = 4.0f;
		Mode = GL_MODULATE;
		break;
	case D3DTOP_MODULATE2X:
		Scale = 2.0f;
		Mode = GL_MODULATE;
		break;
	case D3DTOP_MODULATE:
		Mode = GL_MODULATE;
		break;
	case D3DTOP_ADD:
		Mode = GL_ADD;
		break;
	case D3DTOP_ADDSIGNED2X:
		Scale = 2.0f;
		Mode = GL_ADD_SIGNED;
		break;
	case D3DTOP_ADDSIGNED:
		Mode = GL_ADD_SIGNED;
		break;
	case D3DTOP_SUBTRACT:
		Mode = GL_SUBTRACT;
		break;
	case D3DTOP_BLENDDIFFUSEALPHA:
	case D3DTOP_BLENDTEXTUREALPHA:
	case D3DTOP_BLENDFACTORALPHA:
	case D3DTOP_BLENDCURRENTALPHA:
		// Arg1 * a + Arg2 * (1-a): INTERPOLATE = Src0*Src2 + Src1*(1-Src2).
		Mode = GL_INTERPOLATE;
		Src[2] = GLFixedArg(	Op == D3DTOP_BLENDDIFFUSEALPHA ? GL_PRIMARY_COLOR :
								Op == D3DTOP_BLENDTEXTUREALPHA ? GL_TEXTURE :
								Op == D3DTOP_BLENDFACTORALPHA  ? GL_CONSTANT :
																 GL_PREVIOUS, GL_SRC_ALPHA );
		break;
	case D3DTOP_LERP:
		// Arg0 * Arg1 + (1-Arg0) * Arg2
		Mode = GL_INTERPOLATE;
		Src[2] = GLArg( Arg0, Alpha );
		break;
	case D3DTOP_DOTPRODUCT3:
		Mode = Alpha ? GL_REPLACE : GL_DOT3_RGBA;
		break;
	case D3DTOP_MULTIPLYADD:
	case D3DTOP_MODULATEALPHA_ADDCOLOR:
	case D3DTOP_MODULATECOLOR_ADDALPHA:
	case D3DTOP_MODULATEINVALPHA_ADDCOLOR:
	case D3DTOP_MODULATEINVCOLOR_ADDALPHA:
	case D3DTOP_BLENDTEXTUREALPHAPM:
	case D3DTOP_ADDSMOOTH:
	{
		// a*b + c: MODULATE_ADD_ATI = Src0*Src2 + Src1.
		FGLCombineArg A, B, C;
		FGLCombineArg A1 = GLArg( Arg1, Alpha ), A2 = GLArg( Arg2, Alpha );
		switch( Op )
		{
		case D3DTOP_MULTIPLYADD:				A = A1;	B = A2;	C = GLArg( Arg0, Alpha );	break;	// Arg0 + Arg1*Arg2
		case D3DTOP_MODULATEALPHA_ADDCOLOR:		A = GLFixedArg( A1.Source, GL_SRC_ALPHA );			B = A2;	C = A1;	break;	// Arg1 + Arg1.a*Arg2
		case D3DTOP_MODULATECOLOR_ADDALPHA:		A = A1;	B = A2;	C = GLFixedArg( A1.Source, GL_SRC_ALPHA );	break;	// Arg1*Arg2 + Arg1.a
		case D3DTOP_MODULATEINVALPHA_ADDCOLOR:	A = GLFixedArg( A1.Source, GL_ONE_MINUS_SRC_ALPHA );	B = A2;	C = A1;	break;	// (1-Arg1.a)*Arg2 + Arg1
		case D3DTOP_MODULATEINVCOLOR_ADDALPHA:	A = GLFixedArg( A1.Source, Alpha ? GL_ONE_MINUS_SRC_ALPHA : GL_ONE_MINUS_SRC_COLOR );	B = A2;	C = GLFixedArg( A1.Source, GL_SRC_ALPHA );	break;
		case D3DTOP_BLENDTEXTUREALPHAPM:		A = A2;	B = GLFixedArg( GL_TEXTURE, GL_ONE_MINUS_SRC_ALPHA );	C = A1;	break;	// Arg1 + Arg2*(1-tex.a)
		case D3DTOP_ADDSMOOTH:
		default:								A = A2;	B = GLFixedArg( A1.Source, Alpha ? GL_ONE_MINUS_SRC_ALPHA : GL_ONE_MINUS_SRC_COLOR );	C = A1;	break;	// Arg1 + Arg2*(1-Arg1)
		}
		if( Combine3 )
		{
			Mode	= GL_MODULATE_ADD_ATI;
			Src[0]	= A;
			Src[1]	= C;
			Src[2]	= B;
		}
		else
		{
			// Without combine3 there is no three-operand op: keep the modulation (the larger
			// term in every UC2 use) and log once.
			UC2GL_LOG_ONCE( TEXT("UC2GLDrv: GL_ATI_texture_env_combine3 missing; D3DTOP %i approximated"), (INT)Op );
			Mode	= GL_MODULATE;
			Src[0]	= A;
			Src[1]	= B;
		}
		break;
	}
	case D3DTOP_DISABLE:
	case D3DTOP_BUMPENVMAP:
	case D3DTOP_BUMPENVMAPLUMINANCE:
	case D3DTOP_PREMODULATE:
	default:
		Mode = GL_REPLACE;
		Src[0] = GLFixedArg( GL_PREVIOUS, OpA );
		break;
	}
}

//
//	FOpenGLRenderInterface::SetMaterialBlending: FD3DRenderInterface::SetMaterialBlending with
//	the D3D render and stage states applied as GL state.
//
void FOpenGLRenderInterface::SetMaterialBlending( FOpenGLMaterialState* NewMaterialState, ECullMode CullMode )
{
	guard(FOpenGLRenderInterface::SetMaterialBlending);

	if( CurrentState->CurrentMaterialState == NewMaterialState )
		return;
	RenDev->StatMaterials++;

	// Passes of pixel-shader materials get their program in DrawPrimitive (BeginProgramPass);
	// everything below is state both paths share (the combiner set-up is simply unused then).

	// Blending.
	if( NewMaterialState->AlphaBlending )
	{
		RenDev->glEnable( GL_BLEND );
		RenDev->glBlendFunc( GLBlendFactor(NewMaterialState->SrcBlend), GLBlendFactor(NewMaterialState->DestBlend) );
		RenDev->glBlendEquation( GLBlendEquation(NewMaterialState->BlendOp) );
	}
	else
		RenDev->glDisable( GL_BLEND );

	// Alpha test (D3DCMP_GREATER against AlphaRef).
	RenDev->glAlphaFunc( GL_GREATER, NewMaterialState->AlphaRef / 255.f );
	if( NewMaterialState->AlphaTest )
		RenDev->glEnable( GL_ALPHA_TEST );
	else
		RenDev->glDisable( GL_ALPHA_TEST );

	// (GLES has no polygon mode, and gl4es' emulation of it does not survive real buffer
	// objects: wireframe passes are drawn as lines by DrawPrimitive there.)
	if( !RenDev->IsGLES )
		RenDev->glPolygonMode( GL_FRONT_AND_BACK, NewMaterialState->FillMode == FM_Wireframe ? GL_LINE : GL_FILL );

	// Depth.
	RenDev->glDepthFunc( NewMaterialState->ZTest ? GL_LEQUAL : GL_ALWAYS );
	RenDev->glDepthMask( (NewMaterialState->ZWrite && DepthEnabled) ? GL_TRUE : GL_FALSE );

	ApplyCullMode( NewMaterialState->TwoSided ? CM_None : CullMode );

	// Texture stages.
	FPlane TFactor = FColor(NewMaterialState->TFactorColor).Plane();
	INT StagesUsed = Min<INT>( NewMaterialState->StagesUsed, RenDev->NumTextureUnits );
	if( NewMaterialState->StagesUsed > RenDev->NumTextureUnits )
		UC2GL_LOG_ONCE( TEXT("UC2GLDrv: pass uses %i stages, only %i texture units"), NewMaterialState->StagesUsed, RenDev->NumTextureUnits );

	for( INT s=0; s<StagesUsed; s++ )
	{
		FOpenGLMaterialStateStage&	Stage	= NewMaterialState->Stages[s];
		FOpenGLTexture*				Texture	= (Stage.Texture && Stage.Texture->TextureID) ? Stage.Texture : NULL;
		GLenum						Target	= (Texture && Texture->IsCubemap) ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;

		RenDev->glActiveTexture( GL_TEXTURE0 + s );
		if( Target == GL_TEXTURE_CUBE_MAP )
		{
			RenDev->glDisable( GL_TEXTURE_2D );
			RenDev->glEnable( GL_TEXTURE_CUBE_MAP );
		}
		else
		{
			RenDev->glDisable( GL_TEXTURE_CUBE_MAP );
			RenDev->glEnable( GL_TEXTURE_2D );
		}
		// M7: the default render target is sampled through its copy (UC2GLPostProcess.cpp).
		RenDev->glBindTexture( Target, Texture ? RenDev->GetSampledTexture( Texture ) : RenDev->NullTextureID );

		if( Texture )
		{
			// Addressing and filtering are per stage in D3D, per texture object in GL.
			RenDev->glTexParameteri( Target, GL_TEXTURE_WRAP_S, GLAddress(Stage.TextureAddressU) );
			RenDev->glTexParameteri( Target, GL_TEXTURE_WRAP_T, GLAddress(Stage.TextureAddressV) );
			if( Target == GL_TEXTURE_CUBE_MAP )
				RenDev->glTexParameteri( Target, GL_TEXTURE_WRAP_R, GLAddress(Stage.TextureAddressW) );

			UBOOL	Mipped		= Texture->CachedNumMips - Texture->CachedFirstMip > 1;
			UBOOL	MinLinear	= Stage.MinFilter != D3DTEXF_POINT,
					MagLinear	= Stage.MagFilter != D3DTEXF_POINT;
			GLint	MinFilter;
			if( !Mipped || Stage.MipFilter == D3DTEXF_NONE )
				MinFilter = MinLinear ? GL_LINEAR : GL_NEAREST;
			else if( Stage.MipFilter == D3DTEXF_POINT )
				MinFilter = MinLinear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST;
			else
				MinFilter = MinLinear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR;
			RenDev->glTexParameteri( Target, GL_TEXTURE_MIN_FILTER, MinFilter );
			RenDev->glTexParameteri( Target, GL_TEXTURE_MAG_FILTER, MagLinear ? GL_LINEAR : GL_NEAREST );
			if( RenDev->SUPPORTS_GL_EXT_texture_filter_anisotropic )
				RenDev->glTexParameterf( Target, GL_TEXTURE_MAX_ANISOTROPY_EXT, (Stage.MinFilter == D3DTEXF_ANISOTROPIC) ? Max<FLOAT>(1.f,RenDev->LevelOfAnisotropy) : 1.f );
		}

		if( Stage.TextureMipLODBias < -100.f )
			Stage.TextureMipLODBias = RenDev->DefaultTexMipBias;
		RenDev->glTexEnvf( GL_TEXTURE_FILTER_CONTROL, GL_TEXTURE_LOD_BIAS, RenDev->NoLODBias ? 0.f : Stage.TextureMipLODBias );

		// Combiner.
		GLint			ColorMode, AlphaMode;
		FGLCombineArg	ColorSrc[3], AlphaSrc[3];
		FLOAT			ColorScale, AlphaScale;
		TranslateStageOp( RenDev, Stage.ColorOp, Stage.ColorArg0, Stage.ColorArg1, Stage.ColorArg2, 0, ColorMode, ColorSrc, ColorScale );
		TranslateStageOp( RenDev, Stage.AlphaOp, Stage.AlphaArg0, Stage.AlphaArg1, Stage.AlphaArg2, 1, AlphaMode, AlphaSrc, AlphaScale );
		if( ColorMode == GL_DOT3_RGBA )
			AlphaMode = GL_REPLACE;	// DOT3_RGBA writes alpha itself; the alpha combiner is ignored

		RenDev->glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE );
		RenDev->glTexEnvfv( GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, (GLfloat*)&TFactor );
		RenDev->glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB, ColorMode );
		RenDev->glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA, AlphaMode );
		for( INT i=0; i<3; i++ )
		{
			RenDev->glTexEnvi( GL_TEXTURE_ENV, GL_SRC0_RGB + i, ColorSrc[i].Source );
			RenDev->glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_RGB + i, ColorSrc[i].Operand );
			RenDev->glTexEnvi( GL_TEXTURE_ENV, GL_SRC0_ALPHA + i, AlphaSrc[i].Source );
			RenDev->glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA + i, AlphaSrc[i].Operand );
		}
		RenDev->glTexEnvf( GL_TEXTURE_ENV, GL_RGB_SCALE, ColorScale );
		RenDev->glTexEnvf( GL_TEXTURE_ENV, GL_ALPHA_SCALE, AlphaScale );
	}

	// Disable all unused texture stages.
	for( INT s=StagesUsed; s<RenDev->NumTextureUnits; s++ )
	{
		RenDev->glActiveTexture( GL_TEXTURE0 + s );
		RenDev->glDisable( GL_TEXTURE_2D );
		RenDev->glDisable( GL_TEXTURE_CUBE_MAP );
	}
	RenDev->glActiveTexture( GL_TEXTURE0 );

	// scion sz - colour write mask (emissive, distortion...). Alpha holds the emissive
	// amount on Xbox; the default target does not write it.
	DWORD Mask = NewMaterialState->ColorWriteMask == CWM_COLORWRITEENABLE_NONE ? CWM_COLORWRITEENABLE_NONE : (CurrentState->ColorWriteMask | NewMaterialState->ColorWriteMask);
	RenDev->glColorMask(
		(Mask & CWM_COLORWRITEENABLE_RED)   ? GL_TRUE : GL_FALSE,
		(Mask & CWM_COLORWRITEENABLE_GREEN) ? GL_TRUE : GL_FALSE,
		(Mask & CWM_COLORWRITEENABLE_BLUE)  ? GL_TRUE : GL_FALSE,
		(Mask & CWM_COLORWRITEENABLE_ALPHA) ? GL_TRUE : GL_FALSE );

	CurrentState->CurrentMaterialState = NewMaterialState;

	unguard;
}

/*-----------------------------------------------------------------------------
	Vertex streams and index buffers.
-----------------------------------------------------------------------------*/

INT FOpenGLRenderInterface::SetVertexStreams(QWORD VertexShaderEnum,FVertexStream** Streams,INT NumStreams)
{
	guard(FOpenGLRenderInterface::SetVertexStreams);

	// Unset any additional old streams.
	for(INT StreamIndex = NumStreams;StreamIndex < CurrentState->NumStreams;StreamIndex++)
		CurrentState->Streams[StreamIndex] = NULL;

	// Build the shader declarations.
	FShaderDeclaration	ShaderDeclaration;
	ShaderDeclaration.NumStreams = NumStreams;
	for(INT StreamIndex = 0;StreamIndex < NumStreams;StreamIndex++)
		ShaderDeclaration.Streams[StreamIndex] = FStreamDeclaration(Streams[StreamIndex]);

	CurrentState->VertexShader		= RenDev->GetVertexShader( VertexShaderEnum, ShaderDeclaration );
	CurrentState->VertexShaderEnum	= VertexShaderEnum;

	INT Size = 0;

	for(INT StreamIndex = 0;StreamIndex < NumStreams;StreamIndex++)
	{
		_WORD CacheId = Streams[StreamIndex]->GetCacheId();
		FOpenGLVertexStream* GLVertexStream = NULL;
		if( CacheId != INVALID_CACHEID )
			GLVertexStream = (FOpenGLVertexStream*)RenDev->GetCachedResource(CacheId,CH_VertexBuffer);
		if( GLVertexStream == NULL )
		{
			GLVertexStream = new FOpenGLVertexStream(RenDev,Streams[StreamIndex]);
			RenDev->CacheResource(GLVertexStream,CH_VertexBuffer);
			Streams[StreamIndex]->SetCacheId(GLVertexStream->CacheId,RenDev);
		}

		if( GLVertexStream->CachedRevision != Streams[StreamIndex]->GetRevision() )
		{
			Size += Streams[StreamIndex]->GetSize();
			GLVertexStream->Cache(Streams[StreamIndex]);
		}

		GLVertexStream->LastFrameUsed = RenDev->FrameCounter;

		CurrentState->Streams[StreamIndex]			= GLVertexStream;
		CurrentState->StreamStrides[StreamIndex]	= Streams[StreamIndex]->GetStride();
	}

	CurrentState->NumStreams = NumStreams;

	return Size;

	unguard;
}

INT FOpenGLRenderInterface::SetDynamicStream(QWORD VertexShaderEnum,FVertexStream* Stream)
{
	guard(FOpenGLRenderInterface::SetDynamicStream);

	if( !RenDev->DynamicVertexStream )
		RenDev->DynamicVertexStream = new FOpenGLDynamicVertexStream(RenDev);

	INT	BaseVertexIndex = RenDev->DynamicVertexStream->AddVertices(Stream),
		Stride = Stream->GetStride();

	CurrentState->Streams[0]		= RenDev->DynamicVertexStream;
	CurrentState->StreamStrides[0]	= Stride;
	for(INT StreamIndex = 1;StreamIndex < CurrentState->NumStreams;StreamIndex++)
		CurrentState->Streams[StreamIndex] = NULL;
	CurrentState->NumStreams = 1;

	FShaderDeclaration	ShaderDeclaration;
	ShaderDeclaration.NumStreams = 1;
	ShaderDeclaration.Streams[0] = FStreamDeclaration(Stream);
	CurrentState->VertexShader		= RenDev->GetVertexShader( VertexShaderEnum, ShaderDeclaration );
	CurrentState->VertexShaderEnum	= VertexShaderEnum;

	return BaseVertexIndex;

	unguard;
}

INT FOpenGLRenderInterface::SetIndexBuffer(FIndexBuffer* IndexBuffer,INT BaseVertexIndex)
{
	guard(FOpenGLRenderInterface::SetIndexBuffer);

	UBOOL RequiresCaching = 0;
	if( IndexBuffer )
	{
		_WORD CacheId = IndexBuffer->GetCacheId();
		FOpenGLIndexBuffer* GLIndexBuffer = NULL;
		if( CacheId != INVALID_CACHEID )
			GLIndexBuffer = (FOpenGLIndexBuffer*)RenDev->GetCachedResource(CacheId,CH_IndexBuffer);
		if( GLIndexBuffer == NULL )
		{
			GLIndexBuffer = new FOpenGLIndexBuffer(RenDev,IndexBuffer);
			RenDev->CacheResource(GLIndexBuffer,CH_IndexBuffer);
			IndexBuffer->SetCacheId(GLIndexBuffer->CacheId,RenDev);
		}

		if( GLIndexBuffer->CachedRevision != IndexBuffer->GetRevision() )
		{
			GLIndexBuffer->Cache(IndexBuffer);
			RequiresCaching |= 1;
		}

		GLIndexBuffer->LastFrameUsed = RenDev->FrameCounter;

		CurrentState->IndexBuffer		= GLIndexBuffer;
		CurrentState->IndexBufferBase	= BaseVertexIndex;
	}
	else
	{
		CurrentState->IndexBuffer		= NULL;
		CurrentState->IndexBufferBase	= 0;
	}

	return RequiresCaching ? IndexBuffer->GetSize() : 0;

	unguard;
}

INT FOpenGLRenderInterface::SetDynamicIndexBuffer(FIndexBuffer* IndexBuffer,INT BaseVertexIndex)
{
	guard(FOpenGLRenderInterface::SetDynamicIndexBuffer);

	FOpenGLDynamicIndexBuffer* DynamicIndexBuffer;
	if( IndexBuffer->GetIndexSize() == sizeof(DWORD) )
	{
		if( !RenDev->DynamicIndexBuffer32 )
			RenDev->DynamicIndexBuffer32 = new FOpenGLDynamicIndexBuffer(RenDev,sizeof(DWORD));
		DynamicIndexBuffer = RenDev->DynamicIndexBuffer32;
	}
	else
	{
		if( !RenDev->DynamicIndexBuffer16 )
			RenDev->DynamicIndexBuffer16 = new FOpenGLDynamicIndexBuffer(RenDev,sizeof(_WORD));
		DynamicIndexBuffer = RenDev->DynamicIndexBuffer16;
	}

	INT	BaseIndex = DynamicIndexBuffer->AddIndices(IndexBuffer);

	CurrentState->IndexBuffer		= DynamicIndexBuffer;
	CurrentState->IndexBufferBase	= BaseVertexIndex;

	return BaseIndex;

	unguard;
}

/*-----------------------------------------------------------------------------
	Committing deferred state and drawing.
-----------------------------------------------------------------------------*/

//
// Lights are specified with the view matrix as modelview, so GL keeps them in camera
// space as D3D does.
//
void FOpenGLRenderInterface::CommitLights()
{
	guard(FOpenGLRenderInterface::CommitLights);

	if( CurrentState->UseDynamicLighting )
	{
		RenDev->glMatrixMode( GL_MODELVIEW );
		RenDev->glLoadMatrixf( (GLfloat*)&CurrentState->WorldToCamera );
		MatricesDirty = 1;

		for( INT LightIndex=0; LightIndex<8; LightIndex++ )
		{
			FOpenGLLight&	Light = Lights[LightIndex];
			GLenum			GLLight = GL_LIGHT0 + LightIndex;
			if( !Light.Enabled )
			{
				RenDev->glDisable( GLLight );
				continue;
			}
			FLOAT Position[4];
			if( Light.Type == D3DLIGHT_DIRECTIONAL )
			{
				// D3D's direction is where the light goes; GL wants the direction to it.
				FVector Dir = -Light.Direction.SafeNormal();
				Position[0] = Dir.X; Position[1] = Dir.Y; Position[2] = Dir.Z; Position[3] = 0.f;
				RenDev->glLightf( GLLight, GL_SPOT_CUTOFF, 180.f );
				RenDev->glLightf( GLLight, GL_CONSTANT_ATTENUATION, 1.f );
				RenDev->glLightf( GLLight, GL_LINEAR_ATTENUATION, 0.f );
				RenDev->glLightf( GLLight, GL_QUADRATIC_ATTENUATION, 0.f );
			}
			else
			{
				Position[0] = Light.Position.X; Position[1] = Light.Position.Y; Position[2] = Light.Position.Z; Position[3] = 1.f;
				RenDev->glLightf( GLLight, GL_CONSTANT_ATTENUATION, Light.Attenuation0 );
				RenDev->glLightf( GLLight, GL_LINEAR_ATTENUATION, Light.Attenuation1 );
				RenDev->glLightf( GLLight, GL_QUADRATIC_ATTENUATION, Light.Attenuation2 );
				if( Light.Type == D3DLIGHT_SPOT )
				{
					FLOAT SpotDir[3] = { Light.Direction.X, Light.Direction.Y, Light.Direction.Z };
					RenDev->glLightfv( GLLight, GL_SPOT_DIRECTION, SpotDir );
					RenDev->glLightf( GLLight, GL_SPOT_CUTOFF, Clamp<FLOAT>( Light.Phi * 0.5f * 180.f / PI, 0.f, 90.f ) );
					RenDev->glLightf( GLLight, GL_SPOT_EXPONENT, Light.Falloff );
				}
				else
					RenDev->glLightf( GLLight, GL_SPOT_CUTOFF, 180.f );
			}
			RenDev->glLightfv( GLLight, GL_POSITION, Position );
			RenDev->glLightfv( GLLight, GL_DIFFUSE, (GLfloat*)&Light.Diffuse );
			RenDev->glEnable( GLLight );
		}

		FPlane Ambient = AmbientColor.Plane();
		Ambient.W = 1.f;
		RenDev->glLightModelfv( GL_LIGHT_MODEL_AMBIENT, (GLfloat*)&Ambient );
		RenDev->glEnable( GL_LIGHTING );
	}
	else
		RenDev->glDisable( GL_LIGHTING );

	LightsDirty = 0;

	unguard;
}

//
// Modelview = LocalToWorld * WorldToCamera; projection = CameraToScreen * the D3D-to-GL
// clip fix-up (see the file header).
//
void FOpenGLRenderInterface::GetGLMatrices( FMatrix& ModelView, FMatrix& Projection )
{
	ModelView = CurrentState->LocalToWorld * CurrentState->WorldToCamera;

	FOpenGLRenderTargetState*	RTState = SavedRenderTargetState.GetCurrentState();
	FOpenGLViewportState*		VPState = SavedViewportState.GetCurrentState();
	FLOAT	FlipY	= RTState->Flipped ? -1.f : 1.f,
			Width	= Max<INT>( VPState->ViewportWidth, 1 ),
			Height	= Max<INT>( VPState->ViewportHeight, 1 );

	// The half-pixel shift that puts D3D's pixel centres (integer coordinates) on GL's. M7:
	// a pass that point-samples a render target is drawn 1/32 pixel short of it, so that a
	// read on a texel boundary takes the texel to the right and below, as hardware does
	// (UC2GLPostProcess.cpp, convention 4).
	FLOAT	Shift	= PointSampleNudge ? (1.f - 1.f/16.f) : 1.f;

	FMatrix Fixup = FMatrix::Identity;
	Fixup.M[1][1] = FlipY;
	Fixup.M[2][2] = 2.f;
	Fixup.M[3][0] = Shift / Width;
	Fixup.M[3][1] = -FlipY * Shift / Height;
	Fixup.M[3][2] = -1.f;

	Projection = CurrentState->CameraToScreen * Fixup;
}

void FOpenGLRenderInterface::CommitMatrices()
{
	guard(FOpenGLRenderInterface::CommitMatrices);

	FMatrix ModelView, Projection;
	GetGLMatrices( ModelView, Projection );
	RenDev->glMatrixMode( GL_MODELVIEW );
	RenDev->glLoadMatrixf( (GLfloat*)&ModelView );
	RenDev->glMatrixMode( GL_PROJECTION );
	RenDev->glLoadMatrixf( (GLfloat*)&Projection );
	RenDev->glMatrixMode( GL_MODELVIEW );

	MatricesDirty = 0;

	unguard;
}

//
// Where the texture coordinate sets of the current streams are, by FVF_TexCoordN (D3D binds
// them by register, D3DVSDE_TEXCOORDn, not by order).
//
struct FGLTexCoordSet
{
	GLuint	Buffer;
	INT		NumCoords,
			Stride,
			Offset;
};
static FGLTexCoordSet	GTexCoordSets[8];
static UBOOL			GStreamHasSpecular;

void FOpenGLRenderInterface::CommitStreams( INT FirstIndex )
{
	guard(FOpenGLRenderInterface::CommitStreams);

	UBOOL	NoIndexBuffer	= CurrentState->IndexBuffer == NULL;
	INT		BaseVertex		= NoIndexBuffer ? FirstIndex : CurrentState->IndexBufferBase;
	UBOOL	HasNormals = 0, HasDiffuse = 0, HasSpecular = 0, HasPosition = 0;

	appMemzero( GTexCoordSets, sizeof(GTexCoordSets) );

	FOpenGLVertexShader* Shader = CurrentState->VertexShader;
	for( INT StreamIndex=0; Shader && StreamIndex<CurrentState->NumStreams && StreamIndex<Shader->Declaration.NumStreams; StreamIndex++ )
	{
		FOpenGLVertexStream* Stream = CurrentState->Streams[StreamIndex];
		if( !Stream )
			continue;
		INT	Stride		= CurrentState->StreamStrides[StreamIndex],
			Offset		= Stride * BaseVertex;
		RenDev->glBindBuffer( GL_ARRAY_BUFFER, Stream->BufferID );

		FStreamDeclaration& Decl = Shader->Declaration.Streams[StreamIndex];
		for( INT ComponentIndex=0; ComponentIndex<Decl.NumComponents; ComponentIndex++ )
		{
			FVertexComponent&	Component = Decl.Components[ComponentIndex];
			INT					NumCoords = 0;
			switch( Component.Type )
			{
			case CT_Float4:	NumCoords = 4; break;
			case CT_Float3:	NumCoords = 3; break;
			case CT_Float2:	NumCoords = 2; break;
			case CT_Float1:	NumCoords = 1; break;
			case CT_Color:	NumCoords = 1; break;
			default:
				UC2GL_LOG_ONCE( TEXT("UC2GLDrv: vertex component type %i not supported (Xbox format)"), (INT)Component.Type );
				break;
			}
			const BYTE* Pointer = (const BYTE*)(PTRINT)Offset;

			switch( Component.Function )
			{
			case FVF_Position:
				RenDev->glVertexPointer( NumCoords, GL_FLOAT, Stride, Pointer );
				HasPosition = 1;
				break;
			case FVF_Normal:
				RenDev->glNormalPointer( GL_FLOAT, Stride, Pointer );
				HasNormals = 1;
				break;
			case FVF_Diffuse:
				RenDev->glColorPointer( 4, GL_UNSIGNED_BYTE, Stride, Pointer );
				HasDiffuse = 1;
				break;
			case FVF_Specular:
				RenDev->glSecondaryColorPointer( 3, GL_UNSIGNED_BYTE, Stride, Pointer );
				HasSpecular = 1;
				break;
			case FVF_TexCoord0:
			case FVF_TexCoord1:
			case FVF_TexCoord2:
			case FVF_TexCoord3:
			case FVF_TexCoord4:
			case FVF_TexCoord5:
			case FVF_TexCoord6:
			case FVF_TexCoord7:
			{
				FGLTexCoordSet& Set = GTexCoordSets[Component.Function - FVF_TexCoord0];
				Set.Buffer		= Stream->BufferID;
				Set.NumCoords	= NumCoords;
				Set.Stride		= Stride;
				Set.Offset		= Offset;
				break;
			}
			default:
				break;
			}
			Offset += NumCoords * 4;
		}
	}

	if( HasPosition )
		RenDev->glEnableClientState( GL_VERTEX_ARRAY );
	else
		RenDev->glDisableClientState( GL_VERTEX_ARRAY );
	if( HasNormals )
		RenDev->glEnableClientState( GL_NORMAL_ARRAY );
	else
	{
		RenDev->glDisableClientState( GL_NORMAL_ARRAY );
	}

	// Vertex colours (D3D: COLOR1). Unlit: passed through, white when absent. Lit: they are
	// the emissive term when COLORVERTEX (UseStaticLighting) is on, as D3DDrv sets
	// EMISSIVEMATERIALSOURCE = COLOR1; with no colour or COLORVERTEX off the emissive is 0.
	if( HasDiffuse )
		RenDev->glEnableClientState( GL_COLOR_ARRAY );
	else
	{
		RenDev->glDisableClientState( GL_COLOR_ARRAY );
		RenDev->glColor4f( 1.f, 1.f, 1.f, 1.f );
	}
	if( CurrentState->UseDynamicLighting )
	{
		if( HasDiffuse && CurrentState->UseStaticLighting )
			RenDev->glEnable( GL_COLOR_MATERIAL );
		else
		{
			static const FLOAT Black[4] = { 0.f, 0.f, 0.f, 1.f };
			RenDev->glDisable( GL_COLOR_MATERIAL );
			RenDev->glMaterialfv( GL_FRONT_AND_BACK, GL_EMISSION, Black );
		}
	}
	else
		RenDev->glDisable( GL_COLOR_MATERIAL );

	// Vertex specular (COLOR2) is added after texturing only with SPECULARENABLE and no
	// lighting (lighting replaces it with the computed specular, which is zero here).
	GStreamHasSpecular = HasSpecular;
	FOpenGLMaterialState* Pass = CurrentState->CurrentMaterialState;
	if( HasSpecular && Pass && Pass->SpecularEnable && !CurrentState->UseDynamicLighting )
	{
		RenDev->glEnableClientState( GL_SECONDARY_COLOR_ARRAY );
		RenDev->glEnable( GL_COLOR_SUM );
	}
	else
	{
		RenDev->glDisableClientState( GL_SECONDARY_COLOR_ARRAY );
		RenDev->glDisable( GL_COLOR_SUM );
	}

	unguard;
}

//
// Texture coordinates of the current pass: the D3D stage's TEXCOORDINDEX (a vertex set or a
// camera-space generator) and TEXTURETRANSFORMFLAGS/transform, as GL arrays, texgen and
// texture matrices.
//
void FOpenGLRenderInterface::CommitTexCoords( INT Pass )
{
	guard(FOpenGLRenderInterface::CommitTexCoords);

	FOpenGLMaterialState*	MaterialState = CurrentState->MaterialPasses[Pass];
	INT						StagesUsed = Min<INT>( MaterialState->StagesUsed, RenDev->NumTextureUnits );

	for( INT s=0; s<RenDev->NumTextureUnits; s++ )
	{
		RenDev->glActiveTexture( GL_TEXTURE0 + s );
		RenDev->glClientActiveTexture( GL_TEXTURE0 + s );

		if( s >= StagesUsed )
		{
			RenDev->glDisableClientState( GL_TEXTURE_COORD_ARRAY );
			RenDev->glDisable( GL_TEXTURE_GEN_S );
			RenDev->glDisable( GL_TEXTURE_GEN_T );
			RenDev->glDisable( GL_TEXTURE_GEN_R );
			RenDev->glDisable( GL_TEXTURE_GEN_Q );
			continue;
		}

		FOpenGLMaterialStateStage&	Stage	= MaterialState->Stages[s];
		DWORD						SetIndex= Stage.TexCoordIndex & 0xFFFF,
									Gen		= Stage.TexCoordIndex & 0xFFFF0000;
		INT							InputDims;

		if( Gen == D3DTSS_TCI_PASSTHRU )
		{
			RenDev->glDisable( GL_TEXTURE_GEN_S );
			RenDev->glDisable( GL_TEXTURE_GEN_T );
			RenDev->glDisable( GL_TEXTURE_GEN_R );
			RenDev->glDisable( GL_TEXTURE_GEN_Q );
			FGLTexCoordSet* Set = SetIndex < 8 ? &GTexCoordSets[SetIndex] : NULL;
			if( Set && Set->NumCoords )
			{
				RenDev->glBindBuffer( GL_ARRAY_BUFFER, Set->Buffer );
				RenDev->glTexCoordPointer( Set->NumCoords, GL_FLOAT, Set->Stride, (const BYTE*)(PTRINT)Set->Offset );
				RenDev->glEnableClientState( GL_TEXTURE_COORD_ARRAY );
				InputDims = Set->NumCoords;
			}
			else
			{
				RenDev->glDisableClientState( GL_TEXTURE_COORD_ARRAY );
				InputDims = 2;
			}
		}
		else
		{
			RenDev->glDisableClientState( GL_TEXTURE_COORD_ARRAY );
			GLint Mode =	Gen == D3DTSS_TCI_CAMERASPACENORMAL   ? GL_NORMAL_MAP :
							Gen == D3DTSS_TCI_CAMERASPACEPOSITION ? GL_EYE_LINEAR :
																	GL_REFLECTION_MAP;
			RenDev->glTexGeni( GL_S, GL_TEXTURE_GEN_MODE, Mode );
			RenDev->glTexGeni( GL_T, GL_TEXTURE_GEN_MODE, Mode );
			RenDev->glTexGeni( GL_R, GL_TEXTURE_GEN_MODE, Mode );
			RenDev->glEnable( GL_TEXTURE_GEN_S );
			RenDev->glEnable( GL_TEXTURE_GEN_T );
			RenDev->glEnable( GL_TEXTURE_GEN_R );
			RenDev->glDisable( GL_TEXTURE_GEN_Q );
			InputDims = 3;
		}

		// D3D: the transform applies only with TEXTURETRANSFORMFLAGS != DISABLE. Its 2D input
		// is (u,v,1,0) where GL's is (s,t,0,1), and a non-projected D3D lookup ignores the last
		// output where GL always divides by q.
		FMatrix	Matrix	= FMatrix::Identity;
		DWORD	Count	= Stage.TexCoordCount & 0xFF;
		UBOOL	Projected = (Stage.TexCoordCount & D3DTTFF_PROJECTED) != 0;
		if( Stage.TextureTransformsEnabled && Count )
		{
			Matrix = (FMatrix)Stage.TextureTransformMatrix;
			if( InputDims <= 2 )
			{
				for( INT j=0; j<4; j++ )
				{
					Matrix.M[3][j] = Stage.TextureTransformMatrix.m[2][j];
					Matrix.M[2][j] = 0.f;
				}
			}
			if( Projected )
			{
				INT From = Clamp<INT>( (INT)Count - 1, 0, 3 );
				if( From != 3 )
					for( INT i=0; i<4; i++ )
						Matrix.M[i][3] = Matrix.M[i][From];
			}
			else
			{
				for( INT i=0; i<4; i++ )
					Matrix.M[i][3] = (i == 3) ? 1.f : 0.f;
			}
		}
		RenDev->glMatrixMode( GL_TEXTURE );
		RenDev->glLoadMatrixf( (GLfloat*)&Matrix );
	}

	RenDev->glMatrixMode( GL_MODELVIEW );
	RenDev->glActiveTexture( GL_TEXTURE0 );
	RenDev->glClientActiveTexture( GL_TEXTURE0 );

	unguard;
}

//
//	FOpenGLRenderInterface::DrawPrimitive
//
void FOpenGLRenderInterface::DrawPrimitive(EPrimitiveType PrimitiveType,INT FirstIndex,INT NumPrimitives,INT MinIndex,INT MaxIndex)
{
	guard(FOpenGLRenderInterface::DrawPrimitive);

	if( NumPrimitives <= 0 || !CurrentState->VertexShader || InOcclusionQuery )
		return;

	GLenum	Type;
	INT		Count;
	switch( PrimitiveType )
	{
	case PT_TriangleList:	Type = GL_TRIANGLES;		Count = NumPrimitives * 3;	break;
	case PT_TriangleStrip:	Type = GL_TRIANGLE_STRIP;	Count = NumPrimitives + 2;	break;
	case PT_TriangleFan:	Type = GL_TRIANGLE_FAN;		Count = NumPrimitives + 2;	break;
	case PT_PointList:		Type = GL_POINTS;			Count = NumPrimitives;		break;
	case PT_LineList:		Type = GL_LINES;			Count = NumPrimitives * 2;	break;
	case PT_LineStrip:		Type = GL_LINE_STRIP;		Count = NumPrimitives + 1;	break;
	default:
		UC2GL_LOG_ONCE( TEXT("UC2GLDrv: primitive type %i not supported"), (INT)PrimitiveType );
		return;
	}

	if( LightsDirty )
		CommitLights();
	if( MatricesDirty )
		CommitMatrices();

	for( INT Pass=0; Pass<CurrentState->NumMaterialPasses; Pass++ )
	{
		// M7: a stage that samples the default render target gets an up-to-date copy of it.
		PrepareSampledTargets( CurrentState->MaterialPasses[Pass] );
		if( MatricesDirty )
			CommitMatrices();

		SetMaterialBlending( CurrentState->MaterialPasses[Pass], CurrentState->CullMode );

		// Fog hacks needed for translucent objects.
		UBOOL RestoreFogColor = 0;
		if( CurrentState->DistanceFogEnabled && CurrentState->CurrentMaterialState->OverrideFogColor )
		{
			FPlane FogColor = CurrentState->CurrentMaterialState->OverriddenFogColor.Plane();
			RenDev->glFogfv( GL_FOG_COLOR, (GLfloat*)&FogColor );
			RestoreFogColor = 1;
		}

		// Pixel-shader materials: program, uniforms and generic attributes (UC2GLShaders.cpp).
		UBOOL Programmed = BeginProgramPass( Pass, FirstIndex );
		if( !Programmed )
		{
			CommitStreams( FirstIndex );
			CommitTexCoords( Pass );
		}

		if( RenDev->IsGLES && CurrentState->CurrentMaterialState->FillMode == FM_Wireframe && Type >= GL_TRIANGLES && DrawWireframe( Type, FirstIndex, Count ) )
			;
		else if( CurrentState->IndexBuffer )
		{
			INT IndexSize = CurrentState->IndexBuffer->IndexSize;
			RenDev->glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, CurrentState->IndexBuffer->BufferID );
			if( MinIndex != INDEX_NONE && MaxIndex != INDEX_NONE && MaxIndex >= MinIndex )
				RenDev->glDrawRangeElements( Type, MinIndex, MaxIndex, Count, IndexSize == 4 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, (const BYTE*)(PTRINT)(FirstIndex * IndexSize) );
			else
				RenDev->glDrawElements( Type, Count, IndexSize == 4 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, (const BYTE*)(PTRINT)(FirstIndex * IndexSize) );
		}
		else
			RenDev->glDrawArrays( Type, 0, Count );

		if( Programmed )
			EndProgramPass();
		RenDev->StatDraws++;
		RenDev->StatPrimitives += NumPrimitives;

		if( RestoreFogColor )
		{
			FPlane FogColor = CurrentState->DistanceFogColor.Plane();
			RenDev->glFogfv( GL_FOG_COLOR, (GLfloat*)&FogColor );
		}
	}

	if( RenDev->CheckGLErrorsPerDraw )
	{
		FOpenGLMaterialState* Last = CurrentState->NumMaterialPasses ? CurrentState->MaterialPasses[CurrentState->NumMaterialPasses-1] : NULL;
		RenDev->CheckGLError( *FString::Printf( TEXT("draw: primitive type %i, %i primitives, %i passes, last pass: pixel shader %i, %i stages, wireframe %i, material %s"),
			(INT)PrimitiveType, NumPrimitives, CurrentState->NumMaterialPasses, Last ? (INT)Last->PixelShader : -1, Last ? Last->StagesUsed : 0, Last ? (INT)(Last->FillMode == FM_Wireframe) : 0,
			GetLastMaterial() ? GetLastMaterial()->GetPathName() : TEXT("None") ) );
	}

	// M7: copies of the main framebuffer are stale now (UC2GLPostProcess.cpp).
	if( SavedRenderTargetState.GetCurrentState()->Framebuffer == RenDev->MainFramebuffer )
		RenDev->MainColorVersion++;
	RenDev->MainDepthVersion++;

	unguard;
}

//
// M11: a wireframe pass on GLES. The edges of the triangles the draw call describes, as a
// line list in a scratch index buffer (D3DFILL_WIREFRAME draws every edge; shared edges are
// drawn twice here as there). Returns 0 if the indices are not at hand; the caller then
// draws the triangles filled.
//
UBOOL FOpenGLRenderInterface::DrawWireframe( GLenum Type, INT FirstIndex, INT Count )
{
	TArray<DWORD> Lines;
	FOpenGLIndexBuffer* IndexBuffer = CurrentState->IndexBuffer;
	const BYTE*	Source		= NULL;
	INT			IndexSize	= 0;
	if( IndexBuffer )
	{
		IndexSize = IndexBuffer->IndexSize;
		if( IndexBuffer == RenDev->DynamicIndexBuffer16 || IndexBuffer == RenDev->DynamicIndexBuffer32 )
		{
			FOpenGLDynamicIndexBuffer* Dynamic = (FOpenGLDynamicIndexBuffer*)IndexBuffer;
			INT First = FirstIndex - Dynamic->ShadowBase;
			if( First < 0 || (First + Count) * IndexSize > Dynamic->Shadow.Num() )
				return 0;
			Source = &Dynamic->Shadow( First * IndexSize );
		}
		else
		{
			FIndexBuffer* Engine = (FIndexBuffer*)IndexBuffer->RenderResource;
			if( !Engine || Engine->GetSize() < (FirstIndex + Count) * IndexSize || Engine->GetIndexSize() != IndexSize )
				return 0;
			if( RenDev->ScratchBuffer.Num() < Engine->GetSize() )
				RenDev->ScratchBuffer.Add( Engine->GetSize() - RenDev->ScratchBuffer.Num() );
			Engine->GetContents( &RenDev->ScratchBuffer(0) );
			Source = &RenDev->ScratchBuffer( FirstIndex * IndexSize );
		}
	}
	#define UC2GL_WIRE_INDEX(i) ( Source ? (IndexSize == 4 ? ((const DWORD*)Source)[i] : (DWORD)((const _WORD*)Source)[i]) : (DWORD)(i) )
	DWORD MaxIndex = 0;
	INT Triangles = Type == GL_TRIANGLES ? Count / 3 : Count - 2;
	for( INT t=0; t<Triangles; t++ )
	{
		DWORD V[3];
		if( Type == GL_TRIANGLES )
			{ V[0] = UC2GL_WIRE_INDEX(t*3); V[1] = UC2GL_WIRE_INDEX(t*3+1); V[2] = UC2GL_WIRE_INDEX(t*3+2); }
		else if( Type == GL_TRIANGLE_STRIP )
			{ V[0] = UC2GL_WIRE_INDEX(t); V[1] = UC2GL_WIRE_INDEX(t+1); V[2] = UC2GL_WIRE_INDEX(t+2); }
		else
			{ V[0] = UC2GL_WIRE_INDEX(0); V[1] = UC2GL_WIRE_INDEX(t+1); V[2] = UC2GL_WIRE_INDEX(t+2); }
		if( V[0] == V[1] || V[1] == V[2] || V[0] == V[2] )
			continue;	// degenerate (strip stitching)
		for( INT e=0; e<3; e++ )
		{
			Lines.AddItem( V[e] );
			Lines.AddItem( V[(e+1)%3] );
			MaxIndex = Max( MaxIndex, V[e] );
		}
	}
	#undef UC2GL_WIRE_INDEX
	if( !Lines.Num() )
		return 1;
	if( !WireIndexBuffer )
		RenDev->glGenBuffers( 1, &WireIndexBuffer );
	RenDev->glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, WireIndexBuffer );
	if( MaxIndex < 65536 )
	{
		TArray<_WORD> Short;
		Short.Add( Lines.Num() );
		for( INT i=0; i<Lines.Num(); i++ )
			Short(i) = (_WORD)Lines(i);
		RenDev->glBufferData( GL_ELEMENT_ARRAY_BUFFER, Short.Num() * sizeof(_WORD), &Short(0), GL_STREAM_DRAW );
		RenDev->glDrawElements( GL_LINES, Lines.Num(), GL_UNSIGNED_SHORT, NULL );
	}
	else if( RenDev->CapUIntIndex )
	{
		RenDev->glBufferData( GL_ELEMENT_ARRAY_BUFFER, Lines.Num() * sizeof(DWORD), &Lines(0), GL_STREAM_DRAW );
		RenDev->glDrawElements( GL_LINES, Lines.Num(), GL_UNSIGNED_INT, NULL );
	}
	return 1;
}

/*-----------------------------------------------------------------------------
	What PC's D3DDrv does not implement (Xbox paths).
	The post-process interface (back-buffer copy and material, CopyDepthBuffer, dynamic
	gamma, the depth-of-field ramp) is in UC2GLPostProcess.cpp since M7.
-----------------------------------------------------------------------------*/

// GPU skinning is Xbox-only; PC skins on the CPU (UsePCSkeletalShader=False).
void FOpenGLRenderInterface::SetSkinBones( INT FirstBoneDestIndex, FCoords* FirstBone, INT BoneCount )
{
}

// UpdateDOFRampLookupTexture is in UC2GLMaterialState.cpp, as in D3DDrv; its GL body in
// UC2GLPostProcess.cpp.

void FOpenGLRenderInterface::SetTerrainTransform( FMatrix& WorldMat, INT HeightMapX, INT HeightMapY )
{
	// Xbox-only terrain vertex shader constants.
}

//
// Occlusion queries: PC's D3DDrv answers "done" without writing the pixel count (undefined).
// We answer the same, with the count defined: visible.
//
UBOOL FOpenGLRenderInterface::BeginOcclusionQuery(void)
{
	// M11: the engine's query geometry (wireframe boxes with every write off) is not drawn
	// while there is nothing to count for it: see DrawPrimitive.
	InOcclusionQuery = 1;
	return 1;
}

UBOOL FOpenGLRenderInterface::EndOcclusionQuery(DWORD QueryNum)
{
	InOcclusionQuery = 0;
	return 1;
}

UBOOL FOpenGLRenderInterface::GetOcclusionQueryResults(DWORD QueryNum,DWORD& dwPixels)
{
	dwPixels = 0x7fffffff;
	return 1;
}

FLOAT FOpenGLRenderInterface::GetPixelAspectRatio()
{
	return 1.f;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
